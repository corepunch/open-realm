#ifndef MAP_LOAD_DEBUG_H
#define MAP_LOAD_DEBUG_H

#include <stdio.h>
#include <time.h>
#ifdef _WIN32
/* Only the kernel32 timing API is needed. <windows.h> would pull in winuser.h,
 * whose SetRect declaration and GetClassName/DrawText macros rename or collide
 * with engine symbols in the unity translation units that include this header.
 * Keep minwindef's legacy far/near macros: mmsystem.h requires them when a
 * later file in the same unity includes <windows.h>. */
#include <windef.h>
#include <winbase.h>
#endif

/* Renderer and game modules are separately linked; diagnostics cannot call engine common symbols. */
static inline double MapLoadMonotonicMilliseconds(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter) || !frequency.QuadPart)
        return 0.0;
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0.0;
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
#endif
}

#ifdef DEBUG_MAP_LOAD
#define MAP_LOAD_LOG(...) fprintf(stderr, "MAP_LOAD " __VA_ARGS__)
#define MAP_LOAD_NOW() MapLoadMonotonicMilliseconds()
#define GAME_MAP_LOAD_NOW() \
    gi.MonotonicMilliseconds()
#define MAP_LOAD_REPORT(map, phase, start) \
    MAP_LOAD_LOG("map=%s phase=%s ms=%.3f\n", (map), (phase), MAP_LOAD_NOW() - (start))
#define GAME_MAP_LOAD_REPORT(map, phase, start) \
    MAP_LOAD_LOG("map=%s phase=%s ms=%.3f\n", (map), (phase), GAME_MAP_LOAD_NOW() - (start))
#else
#define MAP_LOAD_LOG(...) do { if (0) fprintf(stderr, "MAP_LOAD " __VA_ARGS__); } while (0)
#define MAP_LOAD_NOW() 0.0
#define GAME_MAP_LOAD_NOW() 0.0
#define MAP_LOAD_REPORT(map, phase, start) ((void)(start))
#define GAME_MAP_LOAD_REPORT(map, phase, start) ((void)(start))
#endif

#endif
