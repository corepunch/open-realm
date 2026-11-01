#include "test.h"
#include "renderer/r_local.h"
#include <stdarg.h>

static struct {
    uint64_t clock, ns;
    uint32_t gen, del, begin, end, reads, polls, current;
    int mode, bits, disjoint;
    bool supported, arb, ready, missing, invalidate;
    char log[8192];
} prof_test;

static Uint64 prof_clock(void) { return prof_test.clock; }
static Uint64 prof_freq(void) { return 1000000; }
static cstring_t prof_cvar(cstring_t name, cstring_t fallback) {
    (void)fallback; T_STREQ(name, "r_profile");
    return prof_test.mode == 2 ? "2" : prof_test.mode == 1 ? "1" : "0";
}
static SDL_bool prof_ext(char const *name) {
    return (prof_test.arb ? !strcmp(name, "GL_ARB_timer_query") : prof_test.supported) ? SDL_TRUE : SDL_FALSE;
}
static void prof_gen(GLsizei count, GLuint *ids) { FOR_LOOP(i, count) ids[i] = ++prof_test.gen; }
static void prof_del(GLsizei count, GLuint const *ids) {
    FOR_LOOP(i, count) T_ASSERT(ids[i] > 0);
    prof_test.del += count;
}
static void prof_begin(GLenum target, GLuint id) {
    T_EQ(target, 0x88BF); T_EQ(prof_test.current, 0); T_ASSERT(id > 0);
    prof_test.current = id; prof_test.begin++;
}
static void prof_end(GLenum target) {
    T_EQ(target, 0x88BF); T_ASSERT(prof_test.current > 0);
    prof_test.current = 0; prof_test.end++;
}
static void prof_bits(GLenum target, GLenum param, GLint *value) {
    T_EQ(target, 0x88BF); T_EQ(param, 0x8864); *value = prof_test.bits;
}
static void prof_ready(GLuint id, GLenum param, GLuint *value) {
    T_ASSERT(id > 0); T_EQ(param, 0x8867); prof_test.polls++; *value = prof_test.ready;
}
static void prof_result(GLuint id, GLenum param, GLuint64 *value) {
    T_ASSERT(id > 0); T_EQ(param, 0x8866); T_ASSERT(prof_test.ready);
    T_ASSERT(id > prof_test.del);
    prof_test.reads++; *value = prof_test.ns;
    if (prof_test.invalidate) prof_test.disjoint = 1;
}
static void prof_integer(GLenum param, GLint *value) {
    T_EQ(param, 0x8FBB); *value = prof_test.disjoint; prof_test.disjoint = 0;
}
static void *prof_proc(char const *name) {
    if (prof_test.missing) return NULL;
#define PROF_PROC(sym, func) if (!strcmp(name, sym) || !strcmp(name, sym "EXT")) return (void *)func
    PROF_PROC("glGenQueries", prof_gen); PROF_PROC("glDeleteQueries", prof_del);
    PROF_PROC("glBeginQuery", prof_begin); PROF_PROC("glEndQuery", prof_end);
    PROF_PROC("glGetQueryiv", prof_bits); PROF_PROC("glGetQueryObjectuiv", prof_ready);
    PROF_PROC("glGetQueryObjectui64v", prof_result);
#undef PROF_PROC
    T_ASSERT(false); return NULL;
}
static int prof_log(FILE *file, char const *format, ...) {
    (void)file;
    size_t used = strlen(prof_test.log);
    va_list args;
    va_start(args, format);
    int result = vsnprintf(prof_test.log + used, sizeof(prof_test.log) - used, format, args);
    va_end(args);
    return result;
}

/* Drive the production query lifecycle, replacing only wall clock and GL/SDL boundaries. */
#define SDL_GetPerformanceCounter prof_clock
#define SDL_GetPerformanceFrequency prof_freq
#define SDL_GL_ExtensionSupported prof_ext
#define SDL_GL_GetProcAddress prof_proc
#define glGetIntegerv prof_integer
#define fprintf prof_log
#include "renderer/r_profile.c"
#undef fprintf
#undef glGetIntegerv
#undef SDL_GL_GetProcAddress
#undef SDL_GL_ExtensionSupported
#undef SDL_GetPerformanceFrequency
#undef SDL_GetPerformanceCounter

static void prof_reset(int mode) {
    R_ProfileShutdown();
    memset(&prof_test, 0, sizeof(prof_test));
    prof_test.mode = mode; prof_test.supported = true; prof_test.bits = 64;
    prof_test.ns = 1000;
    ri.CvarString = prof_cvar;
}

TEST(renderer_profile, async_results_are_not_read_early_and_use_64_bit_nanoseconds) {
    prof_reset(2);
    R_ProfileBeginFrame();
    prof_test.clock = 2000; R_ProfileStage(RP_WORLD);
    prof_test.clock = 5000; R_ProfileStage(RP_SWAP);
    prof_test.clock = 6000; R_ProfileEndFrame();
    T_EQ(r_prof.cpu[RP_OTHER], 2000); T_EQ(r_prof.cpu[RP_WORLD], 3000);
    T_EQ(r_prof.cpu[RP_SWAP], 1000); T_EQ(prof_test.begin, 1); T_EQ(prof_test.end, 1);
    R_ProfileBeginFrame(); R_ProfileEndFrame();
    T_EQ(prof_test.reads, 0); T_EQ(r_prof.samples, 0);
    prof_test.ready = true;
    prof_test.ns = 5000000000ULL; prof_test.clock = 6000000;
    R_ProfileBeginFrame();
    T_EQ(prof_test.reads, 1); T_EQ(r_prof.samples, 1); T_EQ(r_prof.gpu, 5000000000ULL);
    R_ProfileEndFrame();
    R_ProfileShutdown();
}

TEST(renderer_profile, disjoint_discards_outstanding_frames_and_new_measurements_recover) {
    prof_reset(2);
    R_ProfileBeginFrame(); R_ProfileEndFrame();
    prof_test.ready = true; prof_test.disjoint = 1;
    R_ProfileBeginFrame(); R_ProfileEndFrame();
    T_EQ(prof_test.reads, 0); T_EQ(r_prof.samples, 0); T_EQ(r_prof.disjoint, 1);
    FOR_LOOP(i, R_GPU_STRIDE) { R_ProfileBeginFrame(); R_ProfileEndFrame(); }
    T_EQ(r_prof.samples, 1); T_EQ(prof_test.reads, 1);
    R_ProfileShutdown();
    prof_reset(2); prof_test.ready = true; prof_test.invalidate = true;
    R_ProfileBeginFrame(); R_ProfileEndFrame();
    T_EQ(r_prof.samples, 0); T_EQ(r_prof.disjoint, 1); T_EQ(r_prof.gpu, 0);
    R_ProfileShutdown();
}

TEST(renderer_profile, invalid_driver_durations_are_reported_instead_of_becoming_gpu_timings) {
    FOR_LOOP(i, 2) {
        prof_reset(2); prof_test.ready = true;
        prof_test.ns = i ? 2000000000ULL : UINT64_MAX;
        R_ProfileBeginFrame(); R_ProfileEndFrame();
        T_EQ(r_prof.samples, 0); T_EQ(r_prof.invalid, 1); T_EQ(r_prof.gpu, 0);
        prof_test.clock = 1000001;
        R_ProfileBeginFrame(); R_ProfileEndFrame();
        T_NOT_NULL(strstr(prof_test.log, "invalid=1 frame_ms=-1.000"));
        R_ProfileShutdown();
    }
}

TEST(renderer_profile, busy_ring_drops_samples_without_waiting) {
    prof_reset(2);
    FOR_LOOP(i, R_GPU_FRAMES * R_GPU_STRIDE + 1) { R_ProfileBeginFrame(); R_ProfileEndFrame(); }
    T_EQ(prof_test.reads, 0); T_EQ(r_prof.busy, 1);
    T_EQ(prof_test.begin, R_GPU_FRAMES); T_EQ(prof_test.end, prof_test.begin);
    R_ProfileShutdown();
}

TEST(renderer_profile, disable_and_context_restart_release_pending_queries) {
    prof_reset(2);
    R_ProfileBeginFrame(); R_ProfileEndFrame();
    prof_test.mode = 0; R_ProfileBeginFrame(); R_ProfileStage(RP_WORLD); R_ProfileEndFrame();
    T_EQ(prof_test.del, R_GPU_FRAMES); T_ASSERT(!r_prof.enabled);
    uint32_t gen = prof_test.gen;
    prof_test.mode = 2; prof_test.ready = true;
    R_ProfileBeginFrame(); R_ProfileEndFrame();
    T_EQ(prof_test.reads, 1); T_EQ(prof_test.gen, gen * 2);
    R_ProfileShutdown();
    T_EQ(prof_test.del, gen * 2);
}

TEST(renderer_profile, unavailable_gpu_keeps_cpu_timings_and_particle_counters) {
    FOR_LOOP(i, 4) {
        prof_reset(i == 3 ? 1 : 2);
        if (i == 0) prof_test.supported = false;
        if (i == 1) prof_test.missing = true;
        if (i == 2) prof_test.bits = 0;
        R_ProfileBeginFrame();
        R_ProfileParticles(&(particlePerf_t){ .alive = 10, .batches = 2, .texfull = 1, .blends = 1, .texsum = 6, .bytes = 2048 });
        prof_test.clock = 1000001; R_ProfileEndFrame();
        T_EQ(prof_test.gen, 0);
        T_NOT_NULL(strstr(prof_test.log, "[R_CPU] frames=1 frame_ms=1000.001"));
        T_NOT_NULL(strstr(prof_test.log, "alive=10.0 batches=2.0 tex_limit=1.0 blend_limit=1.0 textures=3.00 upload_kb=2.0"));
        if (i < 3) T_NOT_NULL(strstr(prof_test.log, "unavailable:"));
    }
    prof_reset(2); prof_test.arb = true;
    R_ProfileBeginFrame(); R_ProfileEndFrame();
    T_ASSERT(r_prof.gpu_ok); T_ASSERT(!r_prof.ext);
    R_ProfileShutdown();
    ri.CvarString = NULL;
}
