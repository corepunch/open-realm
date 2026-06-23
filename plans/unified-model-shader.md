# Unified Model Shader

## Goal

Replace three separate model shaders (MDX, M2, M3) with one shared shader. The GPU skinning
loop is identical in all three formats. Differences in UV encoding, normal encoding, bone
count, palette indexing, and lighting can be resolved by normalizing data at load time.

## Current State

| Format | VS/FS location | Bones | UV | Normal | Lighting |
|--------|---------------|-------|----|--------|----------|
| MDX (WC3) | `r_mdx_render.c:15` / `:101` | 8 (two attr sets) | float | float | 8 per-model lights + fallback |
| M2 (WoW) | `r_m2.c:406` / `:457` | 4 | float | float | 1 directional |
| M3 (SC2) | `r_m3_load.c:41` / `:99` | 4 + lookup offset | SHORT ÷ 2048 | ubyte ×2−1 | 3 directional + ambient |

All three share `R_InitShader`, global attribute locations 0–7, `uBones`, `uViewProjectionMatrix`,
`uModelMatrix`, `uLightMatrix`, `uNormalMatrix`, `uTextureMatrix`, `uTexture`, `uShadowmap`,
`uFogOfWar`. The GPU skinning loop is the same everywhere:

```glsl
for (int i = 0; i < 4; ++i)
    position += uBones[int(bones[i])] * pos4 * weight[i];
```

## Unified Vertex Format (40 bytes)

```c
typedef struct {
    VECTOR3 position;    // float — all formats native
    VECTOR2 texcoord;    // float — M3 converts SHORT→float on load
    VECTOR3 normal;      // float — M3 decodes ubyte→float on load
    COLOR32 color;       // ubyte — MDX sets white, M2/M3 already ubyte
    BYTE skin[4];        // top 4 bone indices sorted by weight
    BYTE boneWeight[4];  // top 4 weights renormalized to sum to 255
} vertex_t;
```

Replaces the current 48-byte `vertex_t` (`skin[8]+boneWeight[8]`). All formats use the
same interleaved VAO layout:

| Attrib | Location | Type | Notes |
|--------|----------|------|-------|
| `i_position` | 0 | vec3 float | |
| `i_color` | 1 | vec4 ubyte normalized | |
| `i_texcoord` | 2 | vec2 float | |
| `i_normal` | 3 | vec3 float | |
| `i_skin1` | 4 | vec4 ubyte | |
| `i_boneWeight1` | 6 | vec4 ubyte normalized | |

`i_skin2` / `i_boneWeight2` (locations 5, 7) are removed.

## Per-Format Changes

### M3 data conversion on load

In `M3_MakeBuffer`, convert M3-specific encodings before uploading:

- **UV**: `SHORT` where 2048 = 1.0 → float: `uv / 2048.0f`
- **Normal**: `ubyte [0,255]` → float `[-1,1]`: `n / 127.5f - 1.0f`

VAO changes: `attrib_texcoord` from `GL_SHORT` to `GL_FLOAT`, `attrib_normal` from
`GL_UNSIGNED_BYTE` (normalized) to `GL_FLOAT`.

### MDX top-4 bone weight filtering

Sort (weight, index) pairs descending, take top 4, renormalize weights to sum to 255.
Shrink `mdxVertexSkin_t` from 16 to 8 bytes. Remove `attrib_skin2`/`attrib_boneWeight2`
from MDX VAO.

### Bone palette: always 128 matrices

| Format | Current | Change |
|--------|---------|--------|
| MDX | CPU palette remap → 128 matrices | No change |
| M2 | CPU bone_lookup → 64 matrices | Extend to 128 (zero-fill unused) |
| M3 | CPU lookup + `uFirstBoneLookupIndex` offset | Pre-build full 128-entry palette on CPU, drop `uFirstBoneLookupIndex` |

M3 pre-multiply by `absoluteInverseBoneRestPositions` happens during palette build,
making vertex bone indices absolute palette entries.

### Lighting: single directional + ambient

```glsl
uniform vec3 uLightDir;      // single directional
uniform vec3 uLightColor;    // directional color × intensity
uniform vec3 uLightAmbient;  // ambient color
```

```glsl
vec3 vertex_lighting(vec3 normal) {
    vec3 n = normalize(normal);
    return uLightAmbient + uLightColor * max(dot(n, normalize(uLightDir)), 0.0);
}
```

| Format | uLightDir | uLightColor | uLightAmbient |
|--------|-----------|-------------|---------------|
| MDX | Fallback dir from `uLightMatrix` row 2 | `uMdxFallbackLighting.y` | `uMdxFallbackLighting.x` |
| M2 | `tr.viewDef.lightDir` | computed from directional | 0.0 |
| M3 | `directional[0].direction` | `color × multiplier` | `ambient_color` |

MDX per-model omni lights (up to 8) are WC3-specific. Not in unified base shader.

## Unified Vertex Shader

```glsl
#version 140
in vec3 i_position;
in vec4 i_color;
in vec2 i_texcoord;
in vec3 i_normal;
in vec4 i_skin1;
in vec4 i_boneWeight1;

out vec4 v_color;
out vec4 v_shadow;
out vec2 v_texcoord;
out vec2 v_texcoord2;
out vec3 v_lighting;

uniform mat4 uBones[128];
uniform mat4 uViewProjectionMatrix;
uniform mat4 uModelMatrix;
uniform mat4 uLightMatrix;
uniform mat3 uNormalMatrix;
uniform mat4 uTextureMatrix;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uLightAmbient;

vec3 vertex_lighting(vec3 normal) {
    vec3 n = normalize(normal);
    return uLightAmbient + uLightColor * max(dot(n, normalize(uLightDir)), 0.0);
}

void main() {
    vec4 pos4 = vec4(i_position, 1.0);
    vec4 norm4 = vec4(i_normal, 0.0);
    vec4 position = vec4(0.0);
    vec4 normal = vec4(0.0);
    for (int i = 0; i < 4; ++i) {
        position += uBones[int(i_skin1[i])] * pos4 * i_boneWeight1[i];
        normal += uBones[int(i_skin1[i])] * norm4 * i_boneWeight1[i];
    }
    position.w = 1.0;
    v_color = i_color;
    v_texcoord = i_texcoord;
    v_texcoord2 = (uTextureMatrix * uModelMatrix * position).xy;
    vec3 worldNormal = normalize(uNormalMatrix * normal.xyz);
    v_lighting = vertex_lighting(worldNormal);
    v_shadow = uLightMatrix * uModelMatrix * position;
    gl_Position = uViewProjectionMatrix * uModelMatrix * position;
}
```

## Unified Fragment Shader

```glsl
#version 140
in vec2 v_texcoord;
in vec2 v_texcoord2;
in vec4 v_shadow;
in vec3 v_lighting;
in vec4 v_color;
out vec4 o_color;

uniform sampler2D uTexture;
uniform sampler2D uShadowmap;
uniform sampler2D uFogOfWar;
uniform float uLayerAlpha;
uniform vec4 uGeosetColor;
uniform vec2 uUvTrans;
uniform vec2 uUvRot;
uniform vec2 uUvScale;
uniform bool uUseDiscard;
uniform bool uUnshaded;

float get_fogofwar() {
    return texture(uFogOfWar, v_texcoord2).r;
}

vec2 quat_transform(vec2 q, vec2 v) {
    float c = q.y * q.y - q.x * q.x;
    float s = 2.0 * q.x * q.y;
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

void main() {
    vec2 uv = v_texcoord;
    uv += uUvTrans;
    uv = quat_transform(uUvRot, uv - 0.5) + 0.5;
    uv = uUvScale * (uv - 0.5) + 0.5;
    vec4 col = texture(uTexture, uv);
    col *= uGeosetColor;
    col *= uLayerAlpha;
    col *= v_color;
    if (!uUnshaded) {
        col.rgb *= get_fogofwar() * v_lighting;
    }
    o_color = col;
    if (o_color.a < 0.5 && uUseDiscard) discard;
}
```

Each format sets only the uniforms it needs. Unused ones stay at defaults:
`uGeosetColor=(1,1,1,1)`, `uLayerAlpha=1.0`, `uUvTrans=(0,0)`, `uUvRot=(0,1)`,
`uUvScale=(1,1)`, `uUseDiscard=false`, `uUnshaded=false`.

## Implementation Phases

### Phase 1: M3 data conversion on load

Files: `r_m3.h`, `r_m3_load.c`

1. Convert SHORT→float UV and ubyte→float normal in `M3_MakeBuffer`.
2. Change VAO pointers to `GL_FLOAT` for texcoord and normal.
3. Remove `/ 2048.0` and `* 2.0 - 1.0` from M3 vertex shader.

### Phase 2: MDX top-4 bone weight filtering

Files: `r_mdx.h`, `r_mdx_load.c`, `r_mdx_render.c`

1. Sort weights descending, keep top 4, renormalize to 255.
2. Shrink `mdxVertexSkin_t` to 8 bytes.
3. Remove `attrib_skin2`/`attrib_boneWeight2` from VAO and shader.

### Phase 3: Unified vertex struct

Files: `r_local.h`, `r_shader.c`, `common.h`, `r_m2.c`

1. Redefine `vertex_t` to 40 bytes (`skin[4]`, `boneWeight[4]`).
2. Update `R_MakeVertexArrayObject` for new layout.
3. Update `M2_MakeVertex` to copy 4 indices/weights.
4. Remove `attrib_skin2`/`attrib_boneWeight2` from enum and `R_InitShader`.

### Phase 4: M3 bone palette

Files: `r_m3_load.c`

1. Pre-build full 128-entry palette on CPU (with `absoluteInverseBoneRestPositions`).
2. Upload 128 matrices unconditionally.
3. Remove `uFirstBoneLookupIndex` and `uBoneWeightPairsCount`.

### Phase 5: Unified lighting

Files: `r_mdx_render.c`, `r_mdx_geoset.c`, `r_m2.c`, `r_m3_load.c`, `r_shader.c`

1. Add `uLightDir`, `uLightColor`, `uLightAmbient` to `shader_program`.
2. Remove `uMdxLights`, `uMdxLightCount`, `uMdxFallbackLighting`, `uMdxLightFill`.
3. Each format sets the 3 new uniforms from its own light data.

### Phase 6: Write unified shader

Files: `r_shader.c` (or new `r_model.c`), remove per-format shader strings

1. Write `model_vs` / `model_fs` as defined above.
2. Add `R_ModelShader()` returning the shared compiled shader.
3. Each format calls `R_ModelShader()` instead of its local singleton.

### Phase 7: Cleanup

1. Remove `mdx_vs`/`mdx_fs`, `m2_vs`/`m2_fs`, `m3_vs`/`m3_fs`.
2. Remove `MDX_Shader()`, `M2_Shader()`, `m3.shader`.
3. Remove `mdxVertexSkin_t` old 16-byte struct.
4. Remove `attrib_skin2`/`attrib_boneWeight2` from enum and all references.

## Non-goals

- Do not unify terrain/particle/sprite shaders.
- Do not add specular/normal-map/alpha-mask now; M3 extras are partially implemented
  and can be added as optional uniforms later.
- Do not add MDX per-model omni lights to unified shader.
- Do not change per-format bone matrix computation; only change palette upload size.
