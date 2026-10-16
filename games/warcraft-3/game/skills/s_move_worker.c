#include "s_skills.h"
#include <SDL2/SDL.h>

/* A single static frontier has one owner. The executor never chooses work
 * budgets, completion ticks or commit order; those remain in routing.c. */
static bool path_worker_enabled;
static SDL_Thread *path_thread;
static SDL_mutex *path_mutex;
static SDL_cond *path_wake, *path_done;
static void (*path_worker_callback)(void *);
static void *path_worker_data;
static bool path_busy, path_stopping;

static int path_worker_main(void *unused) {
    SDL_LockMutex(path_mutex);
    for (;;) {
        while (!path_busy && !path_stopping) SDL_CondWait(path_wake, path_mutex);
        if (path_stopping) break;
        void (*work)(void *) = path_worker_callback;
        void *data = path_worker_data;
        SDL_UnlockMutex(path_mutex);
        work(data);
        SDL_LockMutex(path_mutex);
        path_busy = false;
        SDL_CondSignal(path_done);
    }
    SDL_UnlockMutex(path_mutex);
    return 0;
}

static void path_worker_start(void) {
    if (path_thread) return;
    path_mutex = SDL_CreateMutex();
    path_wake = SDL_CreateCond(); path_done = SDL_CreateCond();
    if (!path_mutex || !path_wake || !path_done) {
        gi.error("Move: cannot create path worker synchronization: %s", SDL_GetError());
        abort();
    }
    path_thread = SDL_CreateThread(path_worker_main, "pathfinding", NULL);
    if (!path_thread) { gi.error("Move: cannot create path worker: %s", SDL_GetError()); abort(); }
}

void G_SetPathWorkerEnabled(bool enabled) {
    G_WaitPathJob();
    path_worker_enabled = enabled && SDL_GetCPUCount() > 1;
}

void G_RunPathJob(void (*work)(void *), void *data) {
    if (!path_worker_enabled) { work(data); return; }
    path_worker_start();
    SDL_LockMutex(path_mutex);
    if (path_busy) { gi.error("Move: path worker already owns a frontier"); abort(); }
    path_worker_callback = work; path_worker_data = data; path_busy = true;
    SDL_CondSignal(path_wake); SDL_UnlockMutex(path_mutex);
}

void G_WaitPathJob(void) {
    if (!path_thread) return;
    SDL_LockMutex(path_mutex);
    while (path_busy) SDL_CondWait(path_done, path_mutex);
    SDL_UnlockMutex(path_mutex);
}

void G_ShutdownPathWorker(void) {
    if (!path_thread) return;
    G_WaitPathJob();
    SDL_LockMutex(path_mutex); path_stopping = true;
    SDL_CondSignal(path_wake); SDL_UnlockMutex(path_mutex);
    SDL_WaitThread(path_thread, NULL);
    SDL_DestroyCond(path_done); SDL_DestroyCond(path_wake); SDL_DestroyMutex(path_mutex);
    path_thread = NULL; path_mutex = NULL; path_wake = path_done = NULL;
    path_stopping = false;
}

#ifdef BZ_TESTS
#include "shared/test.h"
typedef struct { SDL_threadID thread; uint32_t value; } pathWorkerTest_t;
static void path_worker_test(void *data) {
    pathWorkerTest_t *result = data;
    result->thread = SDL_ThreadID();
    result->value = result->value * 1664525u + 1013904223u;
}
TEST(wc3_pathfinding, worker_and_inline_jobs_produce_identical_results) {
    pathWorkerTest_t inline_result = {0, 123}, worker_result = {0, 123};
    G_SetPathWorkerEnabled(false);
    G_RunPathJob(path_worker_test, &inline_result); G_WaitPathJob();
    T_EQ(inline_result.thread, SDL_ThreadID());
    G_SetPathWorkerEnabled(true);
    G_RunPathJob(path_worker_test, &worker_result); G_WaitPathJob();
    T_EQ(worker_result.value, inline_result.value);
    if (SDL_GetCPUCount() > 1) T_ASSERT(worker_result.thread != SDL_ThreadID());
    else T_EQ(worker_result.thread, SDL_ThreadID());
    G_ShutdownPathWorker(); G_SetPathWorkerEnabled(false);
}
#endif
