#include "r_local.h"
#include "r_profile.h"

#define R_GPU_FRAMES 8 // frames; bounded pending-query ring; skip sampling rather than wait for the GPU
#define R_GPU_STRIDE 16 // frames; sample one in sixteen to limit timer-query interference on tile GPUs
#define R_GPU_SLOP_NS 1000000 // nanoseconds; allow 1 ms host-clock rounding when validating the GPU interval bound
#define R_GPU_ELAPSED 0x88BF // GL token; TIME_ELAPSED_EXT/ARB measures completed commands in nanoseconds
#define R_GPU_DISJOINT 0x8FBB // GL token; GPU_DISJOINT_EXT invalidates pending measurements after clock changes
#define R_GPU_BITS 0x8864 // GL token; QUERY_COUNTER_BITS tests whether elapsed queries contain useful information
#define R_GPU_RESULT 0x8866 // GL token; QUERY_RESULT is read only after the sampled frame reports available
#define R_GPU_READY 0x8867 // GL token; QUERY_RESULT_AVAILABLE polls without waiting for unfinished queries

#ifdef _WIN32
#define R_GPU_CALL APIENTRY
#else
#define R_GPU_CALL
#endif
typedef struct {
    void (R_GPU_CALL *gen)(GLsizei, GLuint *);
    void (R_GPU_CALL *del)(GLsizei, GLuint const *);
    void (R_GPU_CALL *begin)(GLenum, GLuint);
    void (R_GPU_CALL *end)(GLenum);
    void (R_GPU_CALL *bits)(GLenum, GLenum, GLint *);
    void (R_GPU_CALL *ready)(GLuint, GLenum, GLuint *);
    void (R_GPU_CALL *result)(GLuint, GLenum, GLuint64 *);
} gpuTimer_t;

typedef struct {
    GLuint id;
    uint64_t start;
    bool pending;
} gpuFrame_t;

static struct {
    gpuTimer_t api;
    gpuFrame_t slots[R_GPU_FRAMES], *sample;
    uint64_t cpu[RP_COUNT], gpu, freq, start, mark;
    uint32_t frames, serial, samples, disjoint, busy, invalid;
    particlePerf_t parts;
    renderPass_t pass;
    int mode;
    bool enabled, gpu_ok, ext, active;
} r_prof;
static char const *const r_pass_names[RP_COUNT] = {
    "other", "fog", "world", "entities", "alpha", "particles", "scale", "ui", "swap", "sleep"
};

/* Each context owns its queries; disabling profiling and video restart release pending names too. */
void R_ProfileShutdown(void) {
    if (r_prof.active) r_prof.api.end(R_GPU_ELAPSED);
    if (r_prof.gpu_ok)
        FOR_LOOP(i, R_GPU_FRAMES) r_prof.api.del(1, &r_prof.slots[i].id);
    memset(&r_prof, 0, sizeof(r_prof));
}

/* Query entry points differ between ES EXT and desktop ARB, but the interval contract is identical. */
static void R_ProfileInitGPU(void) {
    gpuTimer_t *api = &r_prof.api;
    GLint bits = 0;
    r_prof.ext = SDL_GL_ExtensionSupported("GL_EXT_disjoint_timer_query") == SDL_TRUE;
    if (!r_prof.ext && !SDL_GL_ExtensionSupported("GL_ARB_timer_query")) {
        fprintf(stderr, "[R_GPU] unavailable: no elapsed timer extension; CPU profiling remains active\n");
        return;
    }
#define R_GPU_PROC(field, name) do { \
    void *addr = SDL_GL_GetProcAddress(r_prof.ext ? name "EXT" : name); \
    memcpy(&api->field, &addr, sizeof(api->field)); \
} while (0)
    R_GPU_PROC(gen, "glGenQueries"); R_GPU_PROC(del, "glDeleteQueries");
    R_GPU_PROC(begin, "glBeginQuery"); R_GPU_PROC(end, "glEndQuery");
    R_GPU_PROC(bits, "glGetQueryiv"); R_GPU_PROC(ready, "glGetQueryObjectuiv");
    R_GPU_PROC(result, "glGetQueryObjectui64v");
#undef R_GPU_PROC
    if (!api->gen || !api->del || !api->begin || !api->end || !api->bits || !api->ready || !api->result) {
        fprintf(stderr, "[R_GPU] unavailable: advertised timer entry point missing\n");
        return;
    }
    api->bits(R_GPU_ELAPSED, R_GPU_BITS, &bits);
    if (!bits) {
        fprintf(stderr, "[R_GPU] unavailable: elapsed counter has zero bits\n");
        return;
    }
    r_prof.gpu_ok = true;
    FOR_LOOP(i, R_GPU_FRAMES) api->gen(1, &r_prof.slots[i].id);
    fprintf(stderr, "[R_GPU] timer=%s bits=%d stride=%d pending_frames=%d\n", r_prof.ext ? "EXT_disjoint" : "ARB", bits, R_GPU_STRIDE, R_GPU_FRAMES);
}

/* A disjoint clock invalidates every outstanding frame, including intervals completed before polling. */
static bool R_ProfileDisjoint(void) {
    GLint value = 0;
    if (r_prof.ext) glGetIntegerv(R_GPU_DISJOINT, &value);
    if (!value) return false;
    FOR_LOOP(i, R_GPU_FRAMES) r_prof.slots[i].pending = false;
    r_prof.disjoint++;
    return true;
}

/* Read completed frames only; segmented queries on tile GPUs need not represent disjoint pass costs. */
static void R_ProfilePoll(void) {
    uint64_t gpu = 0;
    uint32_t samples = 0;
    if (R_ProfileDisjoint()) return;
    FOR_LOOP(i, R_GPU_FRAMES) {
        gpuFrame_t *slot = r_prof.slots + i;
        GLuint ready = GL_FALSE;
        if (!slot->pending) continue;
        r_prof.api.ready(slot->id, R_GPU_READY, &ready);
        if (!ready) continue;
        GLuint64 ns = 0;
        r_prof.api.result(slot->id, R_GPU_RESULT, &ns);
        slot->pending = false;
        /* Some mobile drivers return UINT64_MAX even with ready=true and disjoint=false. */
        double limit = (SDL_GetPerformanceCounter() - slot->start) * (1000000000.0 / r_prof.freq);
        if (ns == UINT64_MAX || ns > limit + R_GPU_SLOP_NS) { r_prof.invalid++; continue; }
        gpu += ns;
        samples++;
    }
    if (R_ProfileDisjoint()) return;
    r_prof.gpu += gpu;
    r_prof.samples += samples;
}

/* CPU passes stay separate; one GPU interval spans all submission, ending before presentation. */
void R_ProfileStage(renderPass_t pass) {
    if (!r_prof.enabled) return;
    uint64_t now = SDL_GetPerformanceCounter();
    r_prof.cpu[r_prof.pass] += now - r_prof.mark;
    r_prof.mark = now; r_prof.pass = pass;
    if (r_prof.active && pass >= RP_SWAP) { r_prof.api.end(R_GPU_ELAPSED); r_prof.active = false; }
}

/* Enable lazily and sample into a free slot; profiling must never wait for a pending GPU frame. */
void R_ProfileBeginFrame(void) {
    int mode = atoi(ri.CvarString("r_profile", "0"));
    if (mode != r_prof.mode) R_ProfileShutdown();
    if (!mode) return;
    if (!r_prof.enabled) {
        r_prof.enabled = true; r_prof.mode = mode;
        r_prof.freq = SDL_GetPerformanceFrequency();
        r_prof.start = SDL_GetPerformanceCounter();
        if (mode > 1) R_ProfileInitGPU();
    }
    r_prof.sample = NULL;
    if (r_prof.gpu_ok) {
        R_ProfilePoll();
        if (!(r_prof.serial % R_GPU_STRIDE)) {
            FOR_LOOP(i, R_GPU_FRAMES) {
                gpuFrame_t *slot = r_prof.slots + i;
                if (slot->pending) continue;
                r_prof.sample = slot;
                slot->start = SDL_GetPerformanceCounter();
                r_prof.api.begin(R_GPU_ELAPSED, slot->id); r_prof.active = true;
                break;
            }
            if (!r_prof.sample) r_prof.busy++;
        }
    }
    r_prof.serial++;
    r_prof.mark = SDL_GetPerformanceCounter(); r_prof.pass = RP_OTHER;
    R_ProfileStage(RP_OTHER);
}

/* World and portrait passes contribute to the same rendered-frame totals. */
void R_ProfileParticles(particlePerf_t const *stats) {
    if (!r_prof.enabled) return;
    r_prof.parts.alive += stats->alive; r_prof.parts.batches += stats->batches;
    r_prof.parts.texfull += stats->texfull; r_prof.parts.blends += stats->blends;
    r_prof.parts.texsum += stats->texsum; r_prof.parts.bytes += stats->bytes;
}

/* CPU times are submission/wait wall times, GPU times are delayed valid sampled-frame means. */
void R_ProfileEndFrame(void) {
    if (!r_prof.enabled) return;
    uint64_t now = SDL_GetPerformanceCounter();
    r_prof.cpu[r_prof.pass] += now - r_prof.mark;
    if (r_prof.active) { r_prof.api.end(R_GPU_ELAPSED); r_prof.active = false; }
    if (r_prof.sample) r_prof.sample->pending = true;
    r_prof.sample = NULL;
    if (r_prof.gpu_ok) R_ProfilePoll();
    r_prof.frames++;
    if (now - r_prof.start < r_prof.freq) return;
    double total = 0, scale = 1000.0 / r_prof.freq / r_prof.frames;
    FOR_LOOP(i, RP_COUNT) total += r_prof.cpu[i] * scale;
    fprintf(stderr, "[R_CPU] frames=%u frame_ms=%.3f", r_prof.frames, total);
    FOR_LOOP(i, RP_COUNT) fprintf(stderr, " %s_ms=%.3f", r_pass_names[i], r_prof.cpu[i] * scale);
    fprintf(stderr, "\n[R_PARTICLES] frames=%u alive=%.1f batches=%.1f tex_limit=%.1f blend_limit=%.1f textures=%.2f upload_kb=%.1f\n", r_prof.frames, (double)r_prof.parts.alive / r_prof.frames, (double)r_prof.parts.batches / r_prof.frames, (double)r_prof.parts.texfull / r_prof.frames, (double)r_prof.parts.blends / r_prof.frames, r_prof.parts.batches ? (double)r_prof.parts.texsum / r_prof.parts.batches : 0, (double)r_prof.parts.bytes / 1024 / r_prof.frames);
    if (r_prof.gpu_ok) {
        fprintf(stderr, "[R_GPU] samples=%u disjoint=%u busy=%u invalid=%u frame_ms=%.3f\n", r_prof.samples, r_prof.disjoint, r_prof.busy, r_prof.invalid, r_prof.samples ? r_prof.gpu / 1000000.0 / r_prof.samples : -1.0);
    }
    memset(r_prof.cpu, 0, sizeof(r_prof.cpu)); r_prof.gpu = 0;
    memset(&r_prof.parts, 0, sizeof(r_prof.parts));
    r_prof.frames = r_prof.samples = r_prof.disjoint = r_prof.busy = r_prof.invalid = 0;
    r_prof.start = now;
}
