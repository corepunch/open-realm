#ifndef WOW_COORDS_H
#define WOW_COORDS_H

#include "common/shared.h"

#define WOW_ADT_SIZE 533.333313f
#define WOW_ADT_TILES 64

typedef struct {
    VECTOR3 pos, rot; /* Raw MODF placement coordinates and degrees, Y-up. */
    WORD scale;      /* MODF fixed point: 1024 = unity, zero = unspecified/unity. */
} WOWPLACEMENT;
typedef WOWPLACEMENT *LPWOWPLACEMENT;
typedef WOWPLACEMENT const *LPCWOWPLACEMENT;

/* Native M2/WMO model space is already +X-forward, +Z-up. */
static MATRIX4 const wow_model_basis = { .v = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1} };

static VECTOR3 Wow_ObjectPosition(FLOAT x, FLOAT y, FLOAT z) {
    return (VECTOR3){ WOW_ADT_TILES * 0.5f * WOW_ADT_SIZE - z, WOW_ADT_TILES * 0.5f * WOW_ADT_SIZE - x, y };
}

/* MDDF stores degrees in placement axes; these are not named actor Euler axes. */
static orientation_t Wow_DoodadOrientation(VECTOR3 raw) {
    return (orientation_t){ .yaw = DEG2RAD(raw.y), .pitch = -DEG2RAD(raw.x), .roll = DEG2RAD(raw.z) };
}

static VECTOR3 Wow_TerrainOffset(FLOAT row, FLOAT col, FLOAT height) { return (VECTOR3){ -row, -col, height }; }
static VECTOR3 Wow_TerrainNormal(VECTOR3 local) { return (VECTOR3){ -local.y, -local.x, local.z }; }
static int Wow_TileIndex(FLOAT coord) { return (int)floorf(32.0f - coord / WOW_ADT_SIZE); }

/* WMO/M2 vertices stay in native Z-up model space. Convert only their ADT placement, once,
 * identically for collision and rendering. B*Ry(y-270)*Rz(-x)*Rx(z-90) = Rz(y+180)*Ry(x)*Rx(z). */
static void Wow_PlacementMatrix(LPCWOWPLACEMENT def, LPMATRIX4 matrix) {
    VECTOR3 pos = Wow_ObjectPosition(def->pos.x, def->pos.y, def->pos.z);
    FLOAT scale = def->scale ? def->scale / 1024.0f : 1.0f;
    orientation_t angles = Wow_DoodadOrientation(def->rot);
    angles.yaw += (FLOAT)M_PI;
    QUATERNION rotation = Quaternion_fromOrientation(&angles);
    Matrix4_from_rotation_translation_scale_origin(matrix, &rotation, &pos,
        &MAKE(VECTOR3, scale, scale, scale), &MAKE(VECTOR3, 0, 0, 0));
}

/* WoW uses downward pitch from the horizon and heading from +X. The orbit view uses tilt
 * from -Z and inverse heading from +Y; copying native angles made the camera overhead/sideways. */
static VECTOR3 Wow_EulerFromCamera(FLOAT pitch, FLOAT yaw) {
    return (VECTOR3){ pitch - 90.0f, 0.0f, 90.0f - yaw };
}

/* Native {downward pitch, heading, roll}; keep movement heading out of view-matrix coordinates. */
static VECTOR3 Wow_CameraFromEuler(LPCVECTOR3 euler) {
    return (VECTOR3){ euler->x + 90.0f, 90.0f - euler->z, euler->y };
}

/* Native camera angles are {downward pitch, heading, roll} in degrees, Z-up. */
static VECTOR3 Wow_ViewForward(LPCVECTOR3 angles) {
    FLOAT yaw = (FLOAT)DEG2RAD(angles->y), pitch = (FLOAT)DEG2RAD(angles->x);
    return (VECTOR3){ cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch) };
}

#endif
