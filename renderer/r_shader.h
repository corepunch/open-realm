#ifndef r_shader_h
#define r_shader_h

#include "renderer/r_local.h"
#include "renderer/shader_desc.h"

/* Built-in renderer programs (descriptors live in r_shader.c). */
extern const shader_desc_t sd_unlit;        /* UI / unlit sprite */
extern const shader_desc_t sd_minimap;      /* circular alpha mask */
extern const shader_desc_t sd_splat;        /* crop edges to [0,1] */
extern const shader_desc_t sd_shadow_splat; /* black silhouette with texture alpha */
extern const shader_desc_t sd_commandbutton;/* edge glow + generic clockwise radial shade */
extern const shader_desc_t sd_minimap_fog;  /* fog-of-war overlay with y-flip */
extern const shader_desc_t sd_default;      /* ground/world per-vertex lighting */
extern const shader_desc_t sd_model;        /* shared skinned model (MDX/M2/M3) */

/* Shared comparison/PCF contract; callers supply the existing shadow sampler and light-space position. */
#define BZ_SHADOW_GLSL \
    "float shadow_visibility(sampler2D depths, vec4 lightpos) {\n" \
    "    vec3 p = lightpos.xyz / lightpos.w * 0.5 + 0.5;\n" \
    "    if (any(lessThan(p, vec3(0.0))) || any(greaterThan(p, vec3(1.0)))) return 1.0;\n" \
    "    vec2 texel = 1.0 / vec2(textureSize(depths, 0));\n" \
    "    float lit = 0.0;\n" \
    "    for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++)\n" \
    "        lit += step(p.z - 0.0001, texture(depths, p.xy + vec2(x, y) * texel).r);\n" \
    "    return lit / 9.0;\n" \
    "}\n"

typedef enum {
    R_MODEL_LIGHT_OMNI,
    R_MODEL_LIGHT_DIRECT,
    R_MODEL_LIGHT_AMBIENT,
} RMODELLIGHTTYPE;

typedef struct rmodellight_s {
    vec3_t pos, dir, color, ambient;
    float atten_start, intensity, ambient_intensity;
    RMODELLIGHTTYPE type;
} rModelLight_t;



typedef struct modelLighting_s {
    rModelLight_t lights[BZ_MODEL_LIGHT_MAX];
    vec3_t ambient;
    uint32_t count;
} modelLighting_t;



typedef struct modelGrass_s {
    vec2_t camera, fade, height;
    vec3_t wind;
    vec4_t phase;
    float time;
    bool enabled;
} modelGrass_t;



/* Translate semantic fixed-pipeline-style state into the private shader mat4 schema. */
static inline void R_PackModelLighting(mat4_t *out, modelLighting_t const *in) {
    FOR_LOOP(i, in->count) {
        rModelLight_t const *light = &in->lights[i];
        out[i] = (mat4_t){ .v = {
            light->pos.x, light->pos.y, light->pos.z, (float)light->type,
            -light->dir.x, -light->dir.y, -light->dir.z, light->atten_start,
            light->color.x, light->color.y, light->color.z, light->intensity,
            light->ambient.x, light->ambient.y, light->ambient.z, light->ambient_intensity,
        }};
    }
    float ambient[3] = { in->ambient.x, in->ambient.y, in->ambient.z };
    FOR_LOOP(i, 3) out[0].v[12 + i] = out[0].v[12 + i] * out[0].v[15] + ambient[i];
    out[0].v[15] = 1.0f;
}

/* Instanced grass uses four packed vec4 columns so one upload owns the complete effect state. */
static inline void R_PackModelGrass(mat4_t *out, modelGrass_t const *in) {
    *out = (mat4_t){ .v = {
        in->camera.x, in->camera.y, in->fade.x, in->fade.y,
        in->time, in->wind.x, in->wind.y, in->wind.z,
        in->phase.x, in->phase.y, in->phase.z, in->phase.w,
        in->height.x, in->height.y, in->enabled ? 1.0f : 0.0f, 0.0f,
    }};
}

static inline environLight_t R_EnvironLightFromModel(rModelLight_t const *in) {
    if (!in) return (environLight_t){0};
    return (environLight_t){
        .dir = in->dir, .color = in->color, .ambient = in->ambient,
        .intensity = in->intensity, .ambient_intensity = in->ambient_intensity,
        .type = (uint32_t)in->type, .valid = true,
    };
}

static inline bool R_LightingFromEnviron(environLight_t const *in, modelLighting_t *out) {
    if (!out) return false;
    *out = (modelLighting_t){0};
    if (!in || !in->valid) return false;
    out->count = 1;
    out->lights[0] = (rModelLight_t){
        .dir = in->dir, .color = in->color, .ambient = in->ambient,
        .intensity = in->intensity, .ambient_intensity = in->ambient_intensity,
        .type = (RMODELLIGHTTYPE)in->type,
    };
    return true;
}

void R_SetDefaultLighting(defaultProg_t *shader, modelLighting_t const *lighting);
void R_SetModelLighting(modelProg_t *shader, modelLighting_t const *lighting);
void R_SetModelGrass(modelProg_t *shader, modelGrass_t const *grass);

#endif
