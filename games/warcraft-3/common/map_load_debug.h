#ifndef WC3_MAP_LOAD_DEBUG_H
#define WC3_MAP_LOAD_DEBUG_H

#include <stdio.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* Renderer and game modules are separately linked; diagnostics cannot call engine common symbols. */
static inline double WC3_MapLoadMonotonicMilliseconds(void) {
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

#ifdef WC3_DEBUG_MAP_LOAD
#define WC3_MAP_LOAD_LOG(...) fprintf(stderr, "WC3_MAP_LOAD " __VA_ARGS__)
#define WC3_MAP_LOAD_NOW() WC3_MapLoadMonotonicMilliseconds()
#define WC3_GAME_MAP_LOAD_NOW() \
    ((gi.MonotonicMilliseconds) ? gi.MonotonicMilliseconds() : 0.0)
#define WC3_MAP_LOAD_REPORT(map, phase, start) \
    WC3_MAP_LOAD_LOG("map=%s phase=%s ms=%.3f\n", (map), (phase), WC3_MAP_LOAD_NOW() - (start))
#define WC3_GAME_MAP_LOAD_REPORT(map, phase, start) \
    WC3_MAP_LOAD_LOG("map=%s phase=%s ms=%.3f\n", (map), (phase), WC3_GAME_MAP_LOAD_NOW() - (start))
#else
#define WC3_MAP_LOAD_LOG(...) ((void)0)
#define WC3_MAP_LOAD_NOW() 0.0
#define WC3_GAME_MAP_LOAD_NOW() 0.0
#define WC3_MAP_LOAD_REPORT(map, phase, start) ((void)(start))
#define WC3_GAME_MAP_LOAD_REPORT(map, phase, start) ((void)(start))
#endif

#endif
