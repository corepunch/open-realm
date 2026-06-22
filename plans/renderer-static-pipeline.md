# Static Renderer Pipeline

## Goal

Move shader, uniform, texture-unit, and GL-state binding out of frontend/game draw code and into renderer-owned pipeline
objects. Calling code should describe what it wants to draw; the selected pipeline should decide which shader variant to
use and bind the matching uniforms/resources.

The target feel is similar to fixed-function OpenGL state such as `glEnable(GL_FOG)`, but without hidden public ordering
dependencies. Public draw APIs should pass complete pass/draw state packets, while the renderer caches and applies GL
state internally.

## Current State

- `renderer/r_shader.c` compiles simple shader programs and registers one large shared set of uniform locations in
  `struct shader_program`.
- Several uniforms are game/model-specific (`uMdxLights`, `uMdxFallbackLighting`, `uBones`, `uGrassTime`,
  WoW terrain samplers), but the generic shader struct keeps growing as a shared bag.
- Warcraft III MDX rendering manually binds common matrices, lighting uniforms, bone matrices, texture transforms,
  material flags, and texture units in `games/warcraft-3/renderer/mdx/`.
- WoW M2 rendering manually binds the same common matrices and per-batch resources in
  `games/world-of-warcraft/renderer/m2/`.
- WoW terrain/grass already has separate shader-owned uniform globals in `r_wowmap_shader.c`, which proves the need for
  per-pipeline state but does not provide a common renderer pattern.
- The renderer already has pass state (`tr.pass`, `R_PASS_SHADOW`, `R_ViewProjectionForPass()`), so a cleaner shader
  variant layer can build on that instead of adding more ad hoc `if shadow pass` branches.

## Design

### 1. Use explicit pass and draw packets at the public boundary

Avoid a public API shaped like:

```c
R_EnableLighting(true);
R_SetLightMatrix(...);
R_SetModelMatrix(...);
R_DrawModel(...);
```

That recreates hidden global ordering bugs. Prefer complete state objects:

```c
typedef struct _RENDERPASS {
    MATRIX4 view_projection, texture_matrix, light_matrix;
    LPCTEXTURE shadowmap, terrain_shadow, fogofwar;
    DWORD flags;
} RENDERPASS, *PRENDERPASS;
typedef RENDERPASS const *LPCRENDERPASS;

typedef struct _MODELDRAW {
    LPCMODEL model;
    MATRIX4 model_matrix;
    MATRIX3 normal_matrix;
    LPCTEXTURE texture;
    COLOR32 color;
    FLOAT alpha;
    DWORD flags;
} MODELDRAW, *PMODELDRAW;
typedef MODELDRAW const *LPCMODELDRAW;
```

Top-level flow should become:

```c
R_BeginPass(&pass);
R_DrawModel(&draw);
R_DrawTerrain(&terrain_draw);
R_EndPass();
```

The renderer may keep internal cached state, but each call receives enough data to be reasoned about independently.

### 2. Introduce renderer-owned pipeline objects

Each pipeline owns:

- shader variants
- uniform locations
- texture-unit conventions
- GL depth/blend/cull state
- common matrix/resource binding
- feature fallbacks for missing uniforms

Example shape:

```c
typedef enum {
    R_PIPELINE_MDX,
    R_PIPELINE_M2,
    R_PIPELINE_WC3_TERRAIN,
    R_PIPELINE_WOW_TERRAIN,
    R_PIPELINE_UI,
} RENDERPIPELINE;

typedef enum {
    R_SHADER_MAIN,
    R_SHADER_SHADOW,
    R_SHADER_PASS_COUNT,
} R_SHADER_PASS;

typedef struct _PIPELINE {
    LPSHADER variants[R_SHADER_PASS_COUNT];
    DWORD flags;
} PIPELINE, *PPIPELINE;
typedef PIPELINE const *LPCPIPELINE;
```

The exact enum ownership can be game-local where the pipeline is game-specific. Engine renderer code should not switch
on Warcraft III, WoW, or SC2 model formats.

### 3. Compile shader variants with small define sets

Add a helper that prepends `#define` lines before compiling GLSL:

```c
LPSHADER R_InitShaderEx(LPCSTR vs, LPCSTR fs, LPCSHADERDEFINE defines, DWORD num_defines);
```

Useful variants:

- `SHADOW_PASS` — skips color, lighting, fog, terrain shadow, most material work
- `LIGHTING` — includes light uniforms/calculation
- `FOGOFWAR` — includes fog-of-war sampling
- `SKINNING` — includes bone attributes/uniforms
- `ALPHA_TEST` — includes discard path for cutout materials

Keep the variant count modest. Split variants when they remove meaningful work or simplify a pass; do not generate every
combination until a real draw path needs it.

### 4. Treat missing uniforms as normal

Uniform locations should be `GLint`, not `DWORD`, because OpenGL uses `-1` for absent/optimized-out uniforms.

Add tiny wrappers:

```c
void R_Uniform1i(GLint loc, GLint v);
void R_Uniform1f(GLint loc, GLfloat v);
void R_UniformMatrix4(GLint loc, LPCMATRIX4 m);
```

Each wrapper no-ops when `loc < 0`. This makes it intentional that a pipeline can submit light, fog, or shadow state to
an unlit/unshadowed shader variant without special casing every call site.

### 5. Keep game policy in game renderer modules

The engine renderer should provide generic helpers:

- compile/link shaders with defines
- bind common attrib locations
- register common uniform groups
- cache GL state
- provide pass-level resources such as shadow/fog textures

The selected game renderer should own the specific pipelines:

- Warcraft III: MDX model, WC3 terrain, water, UI/sprite support
- WoW: M2 model, ADT terrain, grass, WMO, water
- SC2: M3 model, SC2 terrain, decals/effects

This preserves the current engine/game boundary and avoids engine literals such as MDX/M2/M3 animation or material
policy.

## Implementation Plan

### Phase 1: Make uniform handling safe and explicit

Files:

- `renderer/r_local.h`
- `renderer/r_shader.c`
- shader call sites in `renderer/` and `games/*/renderer/`

Steps:

1. Change uniform fields in `struct shader_program` from `DWORD` to `GLint`.
2. Add `R_Uniform*` wrappers that skip `loc < 0`.
3. Replace direct `glUniform*` calls at high-churn common call sites first.
4. Keep direct `glUniform*` only inside narrow pipeline-owned code while migrating.
5. Verify current shaders still render and missing uniforms do not produce GL errors.

### Phase 2: Add shader define compilation

Files:

- `renderer/r_shader.c`
- `renderer/r_local.h`

Steps:

1. Add a small `SHADERDEFINE` struct with `name` and optional `value`.
2. Implement `R_InitShaderEx(...)` by prepending `#version`-compatible `#define` text after the GLSL version line.
3. Keep `R_InitShader(vs, fs)` as a wrapper with no defines.
4. Include shader compile logs with the active define list so broken variants are easy to identify.
5. Do not add game-specific define names to engine enums; callers pass plain define tables.

### Phase 3: Build a first real pipeline around MDX

Files:

- `games/warcraft-3/renderer/mdx/r_mdx.h`
- `games/warcraft-3/renderer/mdx/r_mdx_render.c`
- `games/warcraft-3/renderer/mdx/r_mdx_geoset.c`

Steps:

1. Add an MDX pipeline struct beside existing `mdlx.shader`.
2. Compile at least two variants: main and shadow.
3. Move common matrix, texture, shadow/fog, and fallback-light binding into one MDX pipeline bind function.
4. Leave geoset/material binding in MDX code, but make it call compact helpers instead of open-coding global shader state.
5. Remove per-pass shader branches from geoset rendering when the selected variant already encodes that pass.

### Phase 4: Convert terrain paths

Files:

- `games/warcraft-3/renderer/w3m/r_war3map*.c`
- `games/world-of-warcraft/renderer/wow/r_wowmap_shader.c`
- `games/world-of-warcraft/renderer/wow/r_wowmap_draw.c`

Steps:

1. Give WC3 terrain a terrain pipeline with main/shadow variants.
2. Convert WoW terrain and grass globals into pipeline-owned uniform locations.
3. Make terrain draw packets carry texture layers, alpha atlas details, and pass flags.
4. Keep ADT/W3M data parsing and terrain policy game-owned.
5. Bind shared pass resources such as fog/shadow through common renderer helpers.

### Phase 5: Convert M2 and later SC2 M3

Files:

- `games/world-of-warcraft/renderer/m2/r_m2.c`
- `games/starcraft-2/renderer/m3/r_m3_load.c`

Steps:

1. Add an M2 pipeline with main/shadow variants and common matrix/bone binding helpers.
2. Keep character appearance, geoset visibility, and equipment texture policy in M2 code.
3. Add an M3 pipeline only after MDX/M2 prove the pattern.
4. Avoid adding a shared "model pipeline" that hides format-specific rules prematurely.

### Phase 6: Slim the shared shader struct

Files:

- `renderer/r_local.h`
- `renderer/r_shader.c`
- all migrated game renderer modules

Steps:

1. Remove game-specific uniform fields from the shared `shader_program`.
2. Keep only universally registered fields or move even those into named uniform groups.
3. Store pipeline-specific locations in pipeline-specific structs.
4. Audit texture-unit assumptions and centralize them per pipeline.

## Verification

- Build the default target with `make build`.
- Run a text-renderer menu frame with `make run-ui-text UI_CMD=menu_main`.
- For WC3 model paths, run a map or model-camera scene that draws animated MDX units and UI portraits.
- For WoW, run `make build-run-wow` or a targeted `openwow` command that draws terrain plus M2 actors.
- For shadow variants, compare main pass and shadow pass behavior with `r_shadows 1` and `r_shadows 0`.
- Check GL logs for missing uniform noise; missing uniforms should be silent through `R_Uniform*` wrappers.

## Non-goals

- Do not add game-specific shader policy to engine renderer code.
- Do not expand `entityState_t`, `renderEntity_t`, or renderer/game APIs just to move one uniform.
- Do not generate every possible shader-feature combination before draw paths need them.
- Do not hardcode Warcraft III, WoW, or SC2 asset/material literals in shared renderer code.
