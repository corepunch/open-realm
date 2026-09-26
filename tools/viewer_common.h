#ifndef viewer_common_h
#define viewer_common_h

#include "tool_common.h"
#include <SDL2/SDL.h>
#include <math.h>

#define VIEWER_WINDOW_WIDTH 800
#define VIEWER_WINDOW_HEIGHT 600

typedef struct viewer_orbit_s {
    vector3_t target;
    float yaw_deg;
    float pitch_deg;
    float distance;
    bool dragging;
    bool reverse_drag;
} viewer_orbit_t;

static inline handle_t Viewer_AddArchive(handle_t *archives, size_t count, cstring_t filename);
static inline handle_t Viewer_OpenFile(handle_t const *archives, size_t count, cstring_t fileName);
static inline void Viewer_CloseFile(handle_t file);
static inline bool Viewer_ExtractFile(handle_t const *archives, size_t count, cstring_t toExtract, cstring_t extracted);
static inline bool Viewer_FileExists(handle_t const *archives, size_t count, cstring_t fileName);
static inline void Viewer_CloseArchives(handle_t *archives, size_t count);

static inline handle_t Viewer_MemAlloc(long size);
static inline void Viewer_MemFree(handle_t mem);

static inline bool Viewer_OrbitHandleEvent(viewer_orbit_t *orbit, SDL_Event const *event);
static inline void Viewer_OrbitInit(viewer_orbit_t *orbit, vector3_t target, float distance, float yaw_deg, float pitch_deg);
static inline void Viewer_OrbitBuildCamera(viewer_orbit_t const *orbit, float aspect, float fov, float znear, float zfar, matrix4_t * output);
static inline void Viewer_OrbitBuildLight(viewer_orbit_t const *orbit, vector3_t const * sunangles, float scale, matrix4_t * output);

static vector3_t Viewer_OrbitEye(viewer_orbit_t const *orbit) {
    float const yaw = orbit->yaw_deg * (float)M_PI / 180.0f;
    float const pitch = orbit->pitch_deg * (float)M_PI / 180.0f;
    float const cp = cosf(pitch);
    float const sp = sinf(pitch);
    float const cy = cosf(yaw);
    float const sy = sinf(yaw);
    return (vector3_t) {
        orbit->target.x + orbit->distance * cp * cy,
        orbit->target.y + orbit->distance * cp * sy,
        orbit->target.z + orbit->distance * sp,
    };
}

static inline void Viewer_OrbitClamp(viewer_orbit_t *orbit) {
    orbit->pitch_deg = MAX(-89.0f, MIN(89.0f, orbit->pitch_deg));
    orbit->distance = MAX(40.0f, MIN(20000.0f, orbit->distance));
}

static inline bool Viewer_OrbitHandleEvent(viewer_orbit_t *orbit, SDL_Event const *event) {
    switch (event->type) {
        case SDL_MOUSEBUTTONDOWN:
            if (event->button.button == SDL_BUTTON_LEFT) {
                orbit->dragging = true;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (event->button.button == SDL_BUTTON_LEFT) {
                orbit->dragging = false;
            }
            break;
        case SDL_MOUSEMOTION:
            if (orbit->dragging) {
                float const direction = orbit->reverse_drag ? -1.0f : 1.0f;
                orbit->yaw_deg += (float)event->motion.xrel * 0.25f * direction;
                orbit->pitch_deg -= (float)event->motion.yrel * 0.25f * direction;
                Viewer_OrbitClamp(orbit);
                return true;
            }
            break;
        case SDL_MOUSEWHEEL:
            if (event->wheel.y != 0) {
                float const factor = powf(0.85f, (float)(-event->wheel.y));
                orbit->distance *= factor;
                Viewer_OrbitClamp(orbit);
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

static inline void Viewer_OrbitInit(viewer_orbit_t *orbit, vector3_t target, float distance, float yaw_deg, float pitch_deg) {
    *orbit = (viewer_orbit_t) {
        .target = target,
        .yaw_deg = yaw_deg,
        .pitch_deg = pitch_deg,
        .distance = distance,
        .dragging = false,
        .reverse_drag = false,
    };
    Viewer_OrbitClamp(orbit);
}

static inline void Viewer_OrbitBuildCamera(viewer_orbit_t const *orbit, float aspect, float fov, float znear, float zfar, matrix4_t * output) {
    matrix4_t proj, view;
    vector3_t eye = Viewer_OrbitEye(orbit);
    vector3_t dir = Vector3_sub(&orbit->target, &eye);
    Matrix4_perspective(&proj, fov, aspect, znear, zfar);
    Matrix4_lookAt(&view, &eye, &dir, &(vector3_t){ 0, 0, 1 });
    Matrix4_multiply(&proj, &view, output);
}

static inline void Viewer_OrbitBuildLight(viewer_orbit_t const *orbit, vector3_t const * sunangles, float scale, matrix4_t * output) {
    matrix4_t proj, view;
    vector3_t eye = Viewer_OrbitEye(orbit);
    vector3_t dir = Vector3_sub(&orbit->target, &eye);
    Matrix4_ortho(&proj, -scale, scale, -scale, scale, -1000.0f, 3000.0f);
    Matrix4_lookAt(&view, &eye, &dir, &(vector3_t){ 0, 0, 1 });
    Matrix4_multiply(&proj, &view, output);
    (void)sunangles;
}

static inline handle_t Viewer_AddArchive(handle_t *archives, size_t count, cstring_t filename) {
    return Tool_AddArchive(archives, count, filename);
}

static inline handle_t Viewer_OpenFile(handle_t const *archives, size_t count, cstring_t fileName) {
    return Tool_OpenFile(archives, count, fileName);
}

static inline void Viewer_CloseFile(handle_t file) {
    Tool_CloseFile(file);
}

static inline bool Viewer_ExtractFile(handle_t const *archives, size_t count, cstring_t toExtract, cstring_t extracted) {
    return Tool_ExtractFile(archives, count, toExtract, extracted);
}

static inline bool Viewer_FileExists(handle_t const *archives, size_t count, cstring_t fileName) {
    return Tool_FileExists(archives, count, fileName);
}

static inline void Viewer_CloseArchives(handle_t *archives, size_t count) {
    Tool_CloseArchives(archives, count);
}

static inline handle_t Viewer_MemAlloc(long size) {
    return Tool_MemAlloc(size);
}

static inline void Viewer_MemFree(handle_t mem) {
    Tool_MemFree(mem);
}

#endif
