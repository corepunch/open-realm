#ifndef r_shader_utils_h
#define r_shader_utils_h

#include "renderer/r_local.h"

typedef struct DIRECTLIGHT { VECTOR3 dir, color, ambient; } DIRECTLIGHT;
typedef struct DIRECTLIGHT *LPDIRECTLIGHT;
typedef const struct DIRECTLIGHT *LPCDIRECTLIGHT;

typedef struct MODELGRASS {
    VECTOR2 camera, fade, height;
    VECTOR3 wind;
    VECTOR4 phase;
    FLOAT time;
    BOOL enabled;
} MODELGRASS;
typedef struct MODELGRASS *LPMODELGRASS;
typedef const struct MODELGRASS *LPCMODELGRASS;

/* The shader stores every source in the same mat4 schema; directional input points from the surface toward the light. */
static inline void R_PackDirectLight(LPMATRIX4 out, LPCDIRECTLIGHT in) {
    *out = (MATRIX4){ .v = {
        0, 0, 0, 1,
        -in->dir.x, -in->dir.y, -in->dir.z, 0,
        in->color.x, in->color.y, in->color.z, 1,
        in->ambient.x, in->ambient.y, in->ambient.z, 1,
    }};
}

/* Global ambient is folded into one source so model lighting needs no parallel ambient uniform. */
static inline void R_AddLightAmbient(LPMATRIX4 light, LPCVECTOR3 ambient) {
    FLOAT src[3] = { ambient->x, ambient->y, ambient->z };
    FOR_LOOP(i, 3) light->v[12 + i] = light->v[12 + i] * light->v[15] + src[i];
    light->v[15] = 1.0f;
}

/* Instanced grass uses four packed vec4 columns so one upload owns the complete effect state. */
static inline void R_PackModelGrass(LPMATRIX4 out, LPCMODELGRASS in) {
    *out = (MATRIX4){ .v = {
        in->camera.x, in->camera.y, in->fade.x, in->fade.y,
        in->time, in->wind.x, in->wind.y, in->wind.z,
        in->phase.x, in->phase.y, in->phase.z, in->phase.w,
        in->height.x, in->height.y, in->enabled ? 1.0f : 0.0f, 0.0f,
    }};
}

#endif
