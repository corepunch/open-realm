#ifndef WOW_COORDS_H
#define WOW_COORDS_H

#include "common/shared.h"

#define WOW_ADT_SIZE 533.333313f
#define WOW_ADT_TILES 64

typedef struct {
    vec3_t pos, rot; /* Raw MODF placement coordinates and degrees, Y-up. */
    uint16_t scale;      /* MODF fixed point: 1024 = unity, zero = unspecified/unity. */
} wowPlacement_t;



/* Native M2/WMO model space is already +X-forward, +Z-up. */
static mat4_t const wow_model_basis = { .v = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1} };

static vec3_t Wow_ObjectPosition(float x, float y, float z) {
    return (vec3_t){ WOW_ADT_TILES * 0.5f * WOW_ADT_SIZE - z, WOW_ADT_TILES * 0.5f * WOW_ADT_SIZE - x, y };
}

/* MDDF stores degrees in placement axes; these are not named actor Euler axes. */
static orientation_t Wow_DoodadOrientation(vec3_t raw) {
    return (orientation_t){ .yaw = DEG2RAD(raw.y), .pitch = -DEG2RAD(raw.x), .roll = DEG2RAD(raw.z) };
}

static vec3_t Wow_TerrainOffset(float row, float col, float height) { return (vec3_t){ -row, -col, height }; }
static vec3_t Wow_TerrainNormal(vec3_t local) { return (vec3_t){ -local.y, -local.x, local.z }; }
static int Wow_TileIndex(float coord) { return (int)floorf(32.0f - coord / WOW_ADT_SIZE); }

/* WMO/M2 vertices stay in native Z-up model space. Convert only their ADT placement, once,
 * identically for collision and rendering. B*Ry(y-270)*Rz(-x)*Rx(z-90) = Rz(y+180)*Ry(x)*Rx(z). */
static void Wow_PlacementMatrix(wowPlacement_t const *def, mat4_t *matrix) {
    vec3_t pos = Wow_ObjectPosition(def->pos.x, def->pos.y, def->pos.z);
    float scale = def->scale ? def->scale / 1024.0f : 1.0f;
    orientation_t angles = Wow_DoodadOrientation(def->rot);
    angles.yaw += (float)M_PI;
    quaternion_t rotation = Quaternion_fromOrientation(&angles);
    Matrix4_from_rotation_translation_scale_origin(matrix, &rotation, &pos,
        &MAKE(vec3_t, scale, scale, scale), &MAKE(vec3_t, 0, 0, 0));
}

/* WoW uses downward pitch from the horizon and heading from +X. The orbit view uses tilt
 * from -Z and inverse heading from +Y; copying native angles made the camera overhead/sideways. */
static vec3_t Wow_EulerFromCamera(float pitch, float yaw) {
    return (vec3_t){ pitch - 90.0f, 0.0f, 90.0f - yaw };
}

/* Native {downward pitch, heading, roll}; keep movement heading out of view-matrix coordinates. */
static vec3_t Wow_CameraFromEuler(vec3_t const *euler) {
    return (vec3_t){ euler->x + 90.0f, 90.0f - euler->z, euler->y };
}

/* Native camera angles are {downward pitch, heading, roll} in degrees, Z-up. */
static vec3_t Wow_ViewForward(vec3_t const *angles) {
    float yaw = (float)DEG2RAD(angles->y), pitch = (float)DEG2RAD(angles->x);
    return (vec3_t){ cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch) };
}

#endif
