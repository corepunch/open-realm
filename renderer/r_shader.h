#ifndef r_shader_h
#define r_shader_h

#include "renderer/r_local.h"

#define BZ_MODEL_LIGHT_MAX 8 // lights; shared model shader array capacity; bounds one lighting-state upload

typedef enum {
    R_MODEL_LIGHT_OMNI,
    R_MODEL_LIGHT_DIRECT,
    R_MODEL_LIGHT_AMBIENT,
} RMODELLIGHTTYPE;

typedef struct RMODELLIGHT {
    VECTOR3 pos, dir, color, ambient;
    FLOAT atten_start, intensity, ambient_intensity;
    RMODELLIGHTTYPE type;
} RMODELLIGHT;
typedef struct RMODELLIGHT *LPRMODELLIGHT;
typedef const struct RMODELLIGHT *LPCRMODELLIGHT;

typedef struct MODELLIGHTING {
    RMODELLIGHT lights[BZ_MODEL_LIGHT_MAX];
    VECTOR3 ambient;
    DWORD count;
} MODELLIGHTING;
typedef struct MODELLIGHTING *LPMODELLIGHTING;
typedef const struct MODELLIGHTING *LPCMODELLIGHTING;

typedef struct MODELGRASS {
    VECTOR2 camera, fade, height;
    VECTOR3 wind;
    VECTOR4 phase;
    FLOAT time;
    BOOL enabled;
} MODELGRASS;
typedef struct MODELGRASS *LPMODELGRASS;
typedef const struct MODELGRASS *LPCMODELGRASS;

/* Translate semantic fixed-pipeline-style state into the private shader mat4 schema. */
static inline void R_PackModelLighting(LPMATRIX4 out, LPCMODELLIGHTING in) {
    FOR_LOOP(i, in->count) {
        LPCRMODELLIGHT light = &in->lights[i];
        out[i] = (MATRIX4){ .v = {
            light->pos.x, light->pos.y, light->pos.z, (FLOAT)light->type,
            -light->dir.x, -light->dir.y, -light->dir.z, light->atten_start,
            light->color.x, light->color.y, light->color.z, light->intensity,
            light->ambient.x, light->ambient.y, light->ambient.z, light->ambient_intensity,
        }};
    }
    FLOAT ambient[3] = { in->ambient.x, in->ambient.y, in->ambient.z };
    FOR_LOOP(i, 3) out[0].v[12 + i] = out[0].v[12 + i] * out[0].v[15] + ambient[i];
    out[0].v[15] = 1.0f;
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

void R_SetModelLighting(LPCSHADER shader, LPCMODELLIGHTING lighting);
void R_SetModelGrass(LPCSHADER shader, LPCMODELGRASS grass);

#endif
