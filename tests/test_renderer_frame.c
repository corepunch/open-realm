#include "test.h"
#include "renderer/r_local.h"
#include "renderer/r_shader.h"
#include <stdarg.h>
extern SDL_Window *window;

static struct {
    Uint64 clock;
    uint32_t swaps, delays, slept, sets, ends, stages, shader_resets;
    uint32_t current_time, worker_frames, serial_frames;
    uint32_t client_prepares, client_renders, client_frames, worker_msec;
    uint32_t pause_applies, pending_request_applies;
    int vsync, applied, maxfps;
    bool shader_cache, cache_applied;
    bool server_worker, online_mode, external_control, fast_forward;
    bool pending_request, worker_release_timed_out;
    bool client_worker_ready, server_client_ready, prepare_enters_loading;
    bool prepare_replaces_server_world, prepare_disables_worker;
    renderPass_t passes[8];
} frame_test;
static SDL_sem *frame_test_release_worker;

static cstring_t frame_cvar(cstring_t name, cstring_t fallback) {
    if (!strcmp(name, "r_vsync")) return frame_test.vsync ? "1" : "0";
    if (!strcmp(name, "r_program_cache")) return frame_test.shader_cache ? "1" : "0";
    return fallback;
}
static int frame_integer(cstring_t name, int fallback) {
    if (!strcmp(name, "com_maxfps")) return frame_test.maxfps;
    if (!strcmp(name, "com_server_worker")) return frame_test.server_worker;
    if (!strcmp(name, "online_mode")) return frame_test.online_mode;
    if (!strcmp(name, "com_fast_forward")) return frame_test.fast_forward;
    return fallback;
}
static Uint32 frame_ticks(void) { return (Uint32)(frame_test.clock / 1000); }
static Uint64 frame_counter(void) { frame_test.clock += 100; return frame_test.clock; }
static void frame_delay(Uint32 ms) {
    frame_test.delays++; frame_test.slept += ms; frame_test.clock += ms * 1000;
}
static void frame_swap(SDL_Window *target) { T_EQ(target, window); frame_test.swaps++; }
static int frame_set_interval(int interval) {
    frame_test.sets++; frame_test.applied = interval; return 0;
}
static int frame_get_interval(void) { return frame_test.applied; }
static void frame_reset_shader(bool cache) {
    frame_test.shader_resets++; frame_test.cache_applied = cache; R_ResetShaderBinding(cache);
}
static void frame_cap(GLenum value) { (void)value; }
static void frame_depth(GLboolean value) { (void)value; }
static void frame_mask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    (void)r; (void)g; (void)b; (void)a;
}
static uint32_t frame_profile_reports;
static char frame_profile_output[512];
static Uint64 frame_frequency(void) { return 1000000; }
static int frame_profile_printf(FILE *stream, cstring_t format, ...) {
    (void)stream; va_list args; va_start(args, format);
    int size = vsnprintf(frame_profile_output, sizeof(frame_profile_output), format, args);
    va_end(args); frame_profile_reports++; return size;
}

/* Exercise real presentation/pacing with controlled configuration and captured clock, OS and profiler calls. */
#define SDL_Delay frame_delay
#define SDL_GL_SwapWindow frame_swap
#define SDL_GL_SetSwapInterval frame_set_interval
#define SDL_GL_GetSwapInterval frame_get_interval
#define SDL_GetTicks frame_ticks
#define SDL_GetPerformanceCounter frame_counter
#define R_ResetShaderBinding frame_reset_shader
#define glEnable frame_cap
#define glDisable frame_cap
#define glDepthMask frame_depth
#define glDepthFunc frame_cap
#define glCullFace frame_cap
#define glColorMask frame_mask
#define glClear frame_cap
#include "renderer/r_main.c"
#undef glClear
#undef glColorMask
#undef glCullFace
#undef glDepthFunc
#undef glDepthMask
#undef glDisable
#undef glEnable
#undef R_ResetShaderBinding
#define Cvar_Integer frame_integer
#undef main
#define main FrameTestEngineMain
#define SDL_GetPerformanceFrequency frame_frequency
#define fprintf frame_profile_printf
#define BZ_EXPERIMENTAL_SERVER_WORKER 1
#include "common/main.c"
#undef BZ_EXPERIMENTAL_SERVER_WORKER
#undef fprintf
#undef SDL_GetPerformanceFrequency
#undef main
#undef Cvar_Integer
#undef SDL_GetPerformanceCounter
#undef SDL_GetTicks
#undef SDL_GL_GetSwapInterval
#undef SDL_GL_SetSwapInterval
#undef SDL_GL_SwapWindow
#undef SDL_Delay

void R_ProfileStage(renderPass_t pass) {
    if (frame_test.stages < sizeof(frame_test.passes) / sizeof(*frame_test.passes))
        frame_test.passes[frame_test.stages] = pass;
    frame_test.stages++;
}
void R_ProfileEndFrame(void) { frame_test.ends++; }
void R_ProfileBeginFrame(void) {}
void R_InvalidateTextureBindingCache(void) {}
void R_SetTextureBindingCacheEnabled(bool enabled) { (void)enabled; }
void R_GetTextureBindingCacheStats(uint32_t *requests, uint32_t *skipped, uint32_t *bind_calls, uint32_t *unit_calls) {
    if (requests) *requests = 0;
    if (skipped) *skipped = 0;
    if (bind_calls) *bind_calls = 0;
    if (unit_calls) *unit_calls = 0;
}

uint32_t Sys_Milliseconds(void) { return frame_test.current_time; }
void Cbuf_AddText(cstring_t text) { (void)text; }
void Cbuf_Execute(void) {}
void Com_Quit(void) {}
bool SV_NeedsFrame(void) { return true; }
bool SV_HasExternalControl(void) { return frame_test.external_control; }
bool SV_LocalClientReadyForWorker(void) { return frame_test.server_client_ready; }
bool CL_LocalServerWorkerReady(void) { return frame_test.client_worker_ready; }
bool SV_ProcessPendingRequests(void) {
    if (!frame_test.pending_request) return false;
    frame_test.pending_request = false;
    frame_test.pending_request_applies++;
    return true;
}
void SV_ApplyDeferredPauseCvar(void) { frame_test.pause_applies++; }
void SV_Frame(uint32_t msec) { (void)msec; frame_test.serial_frames++; }
void SV_FrameWorker(uint32_t msec) {
    /* Timeout is a deadlock guard, not a performance/timing assertion. */
    if (frame_test_release_worker && SDL_SemWaitTimeout(frame_test_release_worker, 2000) != 0)
        frame_test.worker_release_timed_out = true;
    frame_test.worker_frames++; frame_test.worker_msec = msec;
}
void CL_FramePrepare(uint32_t msec) {
    (void)msec; frame_test.client_prepares++;
    if (frame_test.prepare_enters_loading) frame_test.client_worker_ready = false;
    if (frame_test.prepare_replaces_server_world) frame_test.server_client_ready = false;
    if (frame_test.prepare_disables_worker) frame_test.server_worker = false;
}
void CL_FrameRender(uint32_t msec) {
    (void)msec; frame_test.client_renders++;
    if (frame_test_release_worker) SDL_SemPost(frame_test_release_worker);
}
void CL_Frame(uint32_t msec) { (void)msec; frame_test.client_frames++; }

/* The real frame entry owns external-state invalidation and samples the live diagnostic toggle once. */
TEST(renderer_frame, begin_frame_resets_shader_binding_and_honors_live_cache_mode) {
    memset(&frame_test, 0, sizeof(frame_test)); ri.CvarString = frame_cvar; drawable_dirty = false;
    FOR_LOOP(i, 2) {
        frame_test.shader_cache = !i;
        R_BeginFrame(); T_EQ(frame_test.shader_resets, i + 1);
        T_EQ(frame_test.cache_applied, !i);
        shaderBindStats_t stats = R_GetShaderBindStats(); T_EQ(stats.requests, 0); T_EQ(stats.binds, 0);
    }
}

/* Presentation must honor live VSync changes while leaving frame pacing to the main loop. */
TEST(renderer_frame, end_frame_presents_without_sleep_and_keeps_vsync_changes) {
    FOR_LOOP(i, 2) {
        memset(&frame_test, 0, sizeof(frame_test));
        frame_test.vsync = i; frame_test.clock = 100000;
        ri.CvarString = frame_cvar; r_swapinterval = -999;
        R_EndFrame(); R_EndFrame();
        T_EQ(frame_test.swaps, 2); T_EQ(frame_test.delays, 0); T_EQ(frame_test.ends, 2);
        T_EQ(frame_test.sets, 1); T_EQ(frame_test.applied, i);
        T_EQ(frame_test.stages, 2); T_EQ(frame_test.passes[0], RP_SWAP);
        frame_test.vsync = !i;
        R_EndFrame();
        T_EQ(frame_test.swaps, 3); T_EQ(frame_test.sets, 2); T_EQ(frame_test.applied, !i);
        T_EQ(frame_test.delays, 0);
    }
}

TEST(renderer_frame, main_loop_limiter_waits_only_for_remaining_capped_budget) {
    int const rates[] = {30, 60, 120};
    FOR_LOOP(i, 3) {
        memset(&frame_test, 0, sizeof(frame_test));
        frame_test.maxfps = rates[i]; frame_test.clock = 5000;
        Com_LimitFrameRate(0, 1000000, false);
        T_ASSERT(frame_test.delays > 0); T_ASSERT(frame_test.slept > 0);
        T_ASSERT(frame_test.clock >= 1000000 / rates[i]);
        T_ASSERT(frame_test.clock < 1000000 / rates[i] + 300);
    }
    FOR_LOOP(i, 4) {
        memset(&frame_test, 0, sizeof(frame_test));
        frame_test.maxfps = i == 0 ? 0 : i == 1 ? -1 : 30;
        frame_test.clock = i == 3 ? 40000 : 5000;
        Com_LimitFrameRate(0, 1000000, i == 2);
        T_EQ(frame_test.delays, 0);
    }
}

TEST(renderer_frame, cpu_report_counts_complete_stage_windows_and_resets_when_disabled) {
    frame_profile_reports = 0; frame_profile_output[0] = 0;
    Com_ProfileFrame(false, false, 0, 0, 0, 0, 0, 0, 0);
    Com_ProfileFrame(true, false, 0, 10000, 10000, 20000, 20000, 50000, 100000);
    T_EQ(frame_profile_reports, 0);
    Com_ProfileFrame(true, false, 0, 1010000, 1010000, 1020000, 1020000, 1060000, 1110000);
    T_EQ(frame_profile_reports, 1);
    T_ASSERT(strstr(frame_profile_output, "loops=2"));
    T_ASSERT(strstr(frame_profile_output, "elapsed_ms=1100.000"));
    T_ASSERT(strstr(frame_profile_output, "online_ms=20.000"));
    T_ASSERT(strstr(frame_profile_output, "server_main_ms=70.000"));
    T_ASSERT(strstr(frame_profile_output, "server_main_max_ms=40.000"));
    T_ASSERT(strstr(frame_profile_output, "client_ms=100.000"));
    Com_ProfileFrame(false, false, 0, 0, 0, 0, 0, 0, 0);
    Com_ProfileFrame(true, false, 0, 2000000, 2000000, 2010000, 2010000, 2020000, 2030000);
    T_EQ(frame_profile_reports, 1);
    Com_ProfileFrame(false, false, 0, 0, 0, 0, 0, 0, 0);
}

TEST(renderer_frame, local_server_worker_waits_between_frames_and_shuts_down_cleanly) {
    memset(&frame_test, 0, sizeof(frame_test));
    frame_test.server_worker = true;
    frame_test.client_worker_ready = true;
    frame_test.server_client_ready = true;
    frame_test.fast_forward = true;
    frame_test.current_time = 100;
    frame_state = (openrealm_frame_state_t){ .previous_time = 80 };

    OpenRealm_Frame(&frame_state);
    T_EQ(frame_test.client_prepares, 1);
    T_EQ(frame_test.client_renders, 1);
    T_EQ(frame_test.client_frames, 0);
    T_EQ(frame_test.serial_frames, 0);

    frame_test.current_time = 120;
    OpenRealm_Frame(&frame_state); /* joins the first worker tick, then queues the next */
    Com_ServerWorkerShutdown();
    Com_ServerWorkerShutdown();
    T_EQ(frame_test.worker_frames, 2);
    T_EQ(frame_test.worker_msec, FRAMETIME);
    T_EQ(frame_test.client_prepares, 2);
    T_EQ(frame_test.client_renders, 2);
    T_EQ(frame_test.serial_frames, 0);
}

/* The worker is released by rendering; the loop must not wait for its tick
 * before producing that frame. */
TEST(renderer_frame, server_worker_can_overlap_client_render_without_a_frame_delay) {
    memset(&frame_test, 0, sizeof(frame_test));
    frame_test_release_worker = SDL_CreateSemaphore(0);
    T_NOT_NULL(frame_test_release_worker);
    frame_test.server_worker = true;
    frame_test.client_worker_ready = true;
    frame_test.server_client_ready = true;
    frame_test.clock = 100000;
    frame_state = (openrealm_frame_state_t){ .previous_time = 80 };

    OpenRealm_Frame(&frame_state);
    Com_ServerWorkerShutdown();
    T_EQ(frame_test.worker_frames, 1u);
    T_ASSERT(!frame_test.worker_release_timed_out);
    T_EQ(frame_test.serial_frames, 0u);
    T_EQ(frame_test.client_prepares, 1u);
    T_EQ(frame_test.client_renders, 1u);
    T_EQ(frame_test.worker_msec, 20u);
    SDL_DestroySemaphore(frame_test_release_worker);
    frame_test_release_worker = NULL;
}

TEST(renderer_frame, disabling_local_server_worker_joins_before_serial_fallback) {
    memset(&frame_test, 0, sizeof(frame_test));
    frame_test.server_worker = true;
    frame_test.client_worker_ready = true;
    frame_test.server_client_ready = true;
    frame_test.clock = 100000;
    frame_state = (openrealm_frame_state_t){ .previous_time = 80 };
    OpenRealm_Frame(&frame_state);

    frame_test.server_worker = false;
    frame_test.clock = 120000;
    OpenRealm_Frame(&frame_state);
    Com_ServerWorkerShutdown();
    T_EQ(frame_test.worker_frames, 1u);
    T_EQ(frame_test.serial_frames, 1u);
    T_EQ(frame_test.client_prepares, 1u);
    T_EQ(frame_test.client_renders, 1u);
    T_EQ(frame_test.client_frames, 1u);
    T_EQ(frame_test.pause_applies, 1u);
}

TEST(renderer_frame, pending_server_request_short_circuits_after_worker_join) {
    memset(&frame_test, 0, sizeof(frame_test));
    frame_test.server_worker = true;
    frame_test.client_worker_ready = true;
    frame_test.server_client_ready = true;
    frame_test.clock = 100000;
    frame_state = (openrealm_frame_state_t){ .previous_time = 80 };
    OpenRealm_Frame(&frame_state);

    frame_test.pending_request = true;
    frame_test.clock = 120000;
    OpenRealm_Frame(&frame_state);
    Com_ServerWorkerShutdown();
    T_EQ(frame_test.worker_frames, 1u);
    T_EQ(frame_test.pending_request_applies, 1u);
    T_EQ(frame_test.pause_applies, 1u);
    T_EQ(frame_test.client_prepares, 1u);
    T_EQ(frame_test.client_renders, 1u);
    T_EQ(frame_state.previous_time, 120u);
    T_EQ(frame_state.frame_count, 2u);
}

TEST(renderer_frame, online_and_externally_controlled_frames_stay_serial) {
    FOR_LOOP(i, 2) {
        memset(&frame_test, 0, sizeof(frame_test));
        frame_test.server_worker = true;
        frame_test.client_worker_ready = true;
        frame_test.server_client_ready = true;
        frame_test.online_mode = i == 0;
        frame_test.external_control = i != 0;
        frame_test.current_time = 100;
        frame_state = (openrealm_frame_state_t){ .previous_time = 80 };
        OpenRealm_Frame(&frame_state);
        T_EQ(frame_test.serial_frames, 1);
        T_EQ(frame_test.worker_frames, 0);
        T_EQ(frame_test.client_frames, 1);
        T_EQ(frame_test.client_prepares, 0);
        T_EQ(frame_test.client_renders, 0);
        Com_ServerWorkerShutdown();
    }
}

TEST(renderer_frame, loading_remains_serial_until_client_is_active) {
    memset(&frame_test, 0, sizeof(frame_test));
    frame_test.server_worker = true;
    frame_test.server_client_ready = true;
    frame_test.clock = 100000;
    frame_state = (openrealm_frame_state_t){ .previous_time = 80 };

    OpenRealm_Frame(&frame_state);
    T_EQ(frame_test.serial_frames, 1u); T_EQ(frame_test.worker_frames, 0u);
    T_EQ(frame_test.client_frames, 1u); T_EQ(frame_test.client_prepares, 0u);
    frame_test.client_worker_ready = true;
    frame_test.server_client_ready = false;
    frame_test.clock = 120000;
    OpenRealm_Frame(&frame_state);
    T_EQ(frame_test.serial_frames, 2u); T_EQ(frame_test.worker_frames, 0u);
    frame_test.server_client_ready = true;
    frame_test.clock = 140000;
    OpenRealm_Frame(&frame_state);
    Com_ServerWorkerShutdown();
    T_EQ(frame_test.worker_frames, 1u); T_EQ(frame_test.serial_frames, 2u);
    T_EQ(frame_test.client_prepares, 1u); T_EQ(frame_test.client_renders, 1u);
}

TEST(renderer_frame, leaving_active_world_joins_worker_and_returns_to_serial) {
    memset(&frame_test, 0, sizeof(frame_test));
    frame_test.server_worker = true;
    frame_test.client_worker_ready = frame_test.server_client_ready = true;
    frame_test.clock = 100000;
    frame_state = (openrealm_frame_state_t){ .previous_time = 80 };
    OpenRealm_Frame(&frame_state);
    frame_test.client_worker_ready = false;
    frame_test.clock = 120000;
    OpenRealm_Frame(&frame_state);
    Com_ServerWorkerShutdown();
    T_EQ(frame_test.worker_frames, 1u); T_EQ(frame_test.serial_frames, 1u);
    T_EQ(frame_test.client_prepares, 1u); T_EQ(frame_test.client_renders, 1u);
    T_EQ(frame_test.client_frames, 1u);
}

TEST(renderer_frame, map_change_during_client_prepare_cancels_worker_submission) {
    FOR_LOOP(i, 2) {
        memset(&frame_test, 0, sizeof(frame_test));
        frame_test.server_worker = true;
        frame_test.client_worker_ready = frame_test.server_client_ready = true;
        frame_test.prepare_enters_loading = i == 0;
        frame_test.prepare_replaces_server_world = i != 0;
        frame_test.clock = 100000;
        frame_state = (openrealm_frame_state_t){ .previous_time = 80 };
        OpenRealm_Frame(&frame_state);
        T_EQ(frame_test.worker_frames, 0u); T_EQ(frame_test.serial_frames, 1u);
        T_EQ(frame_test.client_prepares, 1u); T_EQ(frame_test.client_renders, 1u);
        T_EQ(frame_test.client_frames, 0u);
        frame_test.clock = 120000;
        OpenRealm_Frame(&frame_state);
        T_EQ(frame_test.serial_frames, 2u); T_EQ(frame_test.client_frames, 1u);
        T_EQ(frame_test.worker_frames, 0u);
        Com_ServerWorkerShutdown();
    }
}

TEST(renderer_frame, disabling_cvar_during_prepare_uses_serial_server_once) {
    memset(&frame_test, 0, sizeof(frame_test));
    frame_test.server_worker = true;
    frame_test.client_worker_ready = frame_test.server_client_ready = true;
    frame_test.prepare_disables_worker = true;
    frame_test.clock = 100000;
    frame_state = (openrealm_frame_state_t){ .previous_time = 80 };
    OpenRealm_Frame(&frame_state);
    Com_ServerWorkerShutdown();
    T_EQ(frame_test.worker_frames, 0u); T_EQ(frame_test.serial_frames, 1u);
    T_EQ(frame_test.client_prepares, 1u); T_EQ(frame_test.client_renders, 1u);
    T_EQ(frame_test.client_frames, 0u);
}
