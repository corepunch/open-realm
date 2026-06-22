# Shadow Maps Re-enable

## Goal

Re-enable runtime shadow maps for WC3, WoW, and SC2 by default, delete the old blob-shadow network fields, and keep
shadow matrix selection clean. The renderer should not grow game-specific branches like "if WoW character create" or
"if SC2 overhead". Each frame should already carry the selected shadow matrix before the render pass starts.

## Current State

- Runtime shadow maps still exist, but are hidden behind `USE_SHADOWMAPS`.
- The shadow-map disable happened in `81c2f246` / `daed0de8` ("Added #ifdef to ignore shadowmaps"). It wrapped the
  existing path rather than removing it.
- Earlier history adjusted the light projection in `dc9c3491` and `f1a81f90`.
- The current unit shadow path is a blob/splat path:
  - `entityState_t.shadow`
  - `entityState_t.shadow_rect`
  - `ShadowPackRect` / `ShadowUnpackRect`
  - `renderEntity_t.shadow`, `shadow_x`, `shadow_y`, `shadow_w`, `shadow_h`
  - WC3 game code loads `ReplaceableTextures\Shadows\*.blp`
  - WoW renderer supplies a fallback blob shadow when the entity has no explicit shadow texture
- `games/warcraft-3/docs/architecture/map-renderer.md` already notes a conflict: the runtime depth shadow map and
  the baked `war3map.shd` texture both use the same `uShadowmap` binding name/path.

## Design

### 1. Treat `viewDef.lightMatrix` as the selected shadow matrix

Do not add shadow matrix policy to `entityState_t` or game snapshots. The selected matrix is view-level render state:

- Overhead gameplay views fill `viewDef.lightMatrix` from the gameplay camera target.
- Loading/menu previews fill `viewDef.lightMatrix` from the preview target.
- Entity-camera views, including WoW CharacterCreate and WC3 glue portraits, let `R_GameExtractEntityCamera(...)`
  fill both `viewProjectionMatrix` and `lightMatrix`.

This keeps the distinction where it belongs:

| View kind | Owner | Matrix source |
|---|---|---|
| WC3/WoW/SC2 overhead gameplay | client view code | camera lerp target + normal world shadow scale |
| menu/loading preview | client view code | preview target + preview scale |
| WoW CharacterCreate / model camera | selected game renderer | model bounds, M2/MDX camera, preview radius |

The renderer then just consumes `viewDef.lightMatrix`. It does not decide why that matrix was selected.

### 2. Replace `is_rendering_lights` with a render pass enum

Add one renderer-owned enum, for example:

```c
typedef enum {
    R_PASS_COLOR,
    R_PASS_SHADOW,
} renderPass_t;
```

Store the active pass in `tr.pass`. Add tiny helpers in the renderer:

```c
static LPCMATRIX4 R_ViewProjectionForPass(void) {
    return tr.pass == R_PASS_SHADOW ? &tr.viewDef.lightMatrix : &tr.viewDef.viewProjectionMatrix;
}

static BOOL R_IsShadowPass(void) { return tr.pass == R_PASS_SHADOW; }
```

All MDX/M2/M3/default shader setup should use `R_ViewProjectionForPass()` instead of open-coding:

```c
#ifdef USE_SHADOWMAPS
extern bool is_rendering_lights;
if (is_rendering_lights) ...
#endif
```

This leaves one enum dispatch point instead of many conditional blocks.

### 3. Make the shadow pass a normal renderer feature

Remove `USE_SHADOWMAPS` as a compile-time feature flag. Keep a cvar only for runtime control:

- `r_shadows 1` or reuse/rename `r_unit_shadows` with default `1`
- The default path allocates `RT_DEPTHMAP`, renders it each 3D frame, and binds it for the color pass.

`R_RenderFrame` should do:

1. Build frustum from `viewProjectionMatrix`.
2. Render fog of war as today.
3. If shadows are enabled and the view has shadow casters, run `R_RenderShadowMap`.
4. Run color pass.

`R_RenderShadowMap` should centrally skip world drawing for `RDF_NOWORLDMODEL`, but still draw entities. This is what
enables WoW CharacterCreate and WC3 model-camera previews to receive shadow maps without special branches in model code.

### 4. Delete blob-shadow state from snapshots

Remove the blob shadow fields from the network and client render structs:

- `entityState_t.shadow`
- `entityState_t.shadow_rect`
- `ShadowPackRect`
- `ShadowUnpackRect`
- `common/msg.c` entries for those fields
- `renderEntity_t.shadow`
- `renderEntity_t.shadow_x/y/w/h`

Then remove the producer/consumer paths:

- WC3 `G_LoadShadowTexture`, `M_SetUnitShadow`, `M_SetBuildingShadow`, and item shadow assignment.
- `client/cl_view.c` copy/unpack of shadow fields.
- `renderer/r_ents.c` `R_RenderShadow`.
- `R_GameRenderShadow` from `renderer/r_game.h` and every game implementation.
- WoW blob fallback in `games/world-of-warcraft/renderer/r_game.c`.

Keep `RF_NO_SHADOW` as the generic "does not cast runtime shadow" render flag. UI overlays, attached overhead models,
particles, and unlit preview helpers can continue using it.

### 5. Blend baked WC3 terrain shadows into the depth shadowmap

Separate the runtime depth map from the baked terrain shadow at the uniform level:

- `uShadowmap` = runtime depth map only (rendered each frame from the sun orthographic).
- `uTerrainShadow` = baked `war3map.shd` texture (WC3 only; WoW/SC2 bind a white/empty texture).

Bind `uTerrainShadow` on texture unit 3. In the terrain and model fragment shaders, combine both:

```glsl
uniform sampler2D uTerrainShadow;

float get_shadow() {
    float depth = texture(uShadowmap, vec2(v_shadow.x + 1.0, v_shadow.y + 1.0) * 0.5).r;
    float depth_shadow = depth < (v_shadow.z + 0.99) * 0.5 ? 0.0 : 1.0;
    float terrain_shadow = texture(uTerrainShadow, v_texcoord2).r;
    return min(depth_shadow, 1.0 - terrain_shadow);
}
```

This gives maximum fidelity: pre-baked shadows (building footprints, tree canopies from the map editor)叠加 on top of
dynamic unit/building shadows. For WoW and SC2, `uTerrainShadow` is bound to `TEX_WHITE` so `terrain_shadow` is always
0 and the result degrades to the plain depth shadow.

Remove `R_GameDrawTerrainShadows()` and the full-map terrain shadow splat rendering from the main pass — the terrain
fragment shader now samples `uTerrainShadow` directly.

### 6. Shadow caster policy

The renderer should use generic policy:

- Color pass draws normal entities, splats, selection circles, decals, alpha surfaces, particles.
- Shadow pass draws only solid shadow casters:
  - skip `RF_NO_SHADOW`
  - skip hidden entities
  - skip overlay/splat/selection/decal/particle draw paths
  - draw world geometry only when `!(rdflags & RDF_NOWORLDMODEL)`
  - draw alpha surfaces only if a specific format later proves it should cast shadows

No game-specific caster decisions should live in the engine renderer. If a game-specific model should not cast a
shadow, set `RF_NO_SHADOW` at the source that creates the render entity.

## Implementation Plan

### Phase 1: Centralize the render pass

Files:

- `renderer/r_local.h`
- `renderer/r_main.c`
- `renderer/r_ents.c`
- `games/warcraft-3/renderer/mdx/r_mdx_geoset.c`
- `games/world-of-warcraft/renderer/m2/r_m2.c`
- `games/starcraft-2/renderer/m3/r_m3_load.c`

Steps:

1. Add `renderPass_t` and `tr.pass`.
2. Add `R_ViewProjectionForPass()` / `R_IsShadowPass()` helpers.
3. Replace `is_rendering_lights` with `tr.pass`.
4. Remove per-model `extern bool is_rendering_lights`.
5. Keep all pass branching inside renderer helpers or top-level pass functions.

### Phase 2: Turn shadow maps on by default

Files:

- `renderer/r_local.h`
- `renderer/r_main.c`
- `renderer/r_shader.c`
- `games/warcraft-3/renderer/mdx/r_mdx_render.c`
- `games/warcraft-3/renderer/mdx/r_mdx_geoset.c`
- `games/world-of-warcraft/renderer/m2/r_m2.c`
- `games/starcraft-2/renderer/m3/r_m3_load.c`

Steps:

1. Remove `USE_SHADOWMAPS` compile guards around runtime shadow-map code.
2. Allocate `RT_DEPTHMAP` unconditionally.
3. Register and bind `uShadowmap` unconditionally in shaders that sample it.
4. Keep `r_shadows` / `r_unit_shadows` as a runtime cvar gate, default `1`.
5. In color shaders, sample runtime shadows only when the pass produced a depth map for the frame.

### Phase 3: Remove blob shadow fields

Files:

- `common/shared.h`
- `common/msg.c`
- `client/tr_public.h`
- `client/cl_view.c`
- `renderer/r_ents.c`
- `renderer/r_game.h`
- `renderer/r_shader.c` — remove `fs_shadow_splat` and `SHADER_SHADOWSPLAT`
- `renderer/r_local.h` — remove `TEX_BLOB_SHADOW`
- `renderer/r_main.c` — remove `R_MakeBlobShadowTexture()`
- `games/warcraft-3/game/g_monster.c`
- `games/warcraft-3/game/g_items.c`
- `games/warcraft-3/game/g_spawn.c`
- `games/warcraft-3/game/g_unitdata.h`
- `games/warcraft-3/game/g_metadata.h` — remove shadow SLK mappings (`ushu`, `ushx/y/w/h`, `ushb`, `bshd`)
- `games/world-of-warcraft/renderer/r_game.c`
- `games/starcraft-2/renderer/r_game.c`
- `games/warcraft-3/renderer/r_game.c`
- `games/warcraft-3/ui/screens/options_menu.c` — remove shadow quality popup (controlled `r_unit_shadows`)
- `common/cvar.c` — remove `r_unit_shadows` cvar registration

Steps:

1. Remove snapshot fields and delta serialization.
2. Remove client copy/unpack into render entities.
3. Remove blob/splat shadow rendering code and game hooks.
4. Remove WC3 metadata helpers that only feed blob shadows, unless another system still reads them.
5. Remove `SHADER_SHADOWSPLAT` shader, `TEX_BLOB_SHADOW` texture, `R_MakeBlobShadowTexture()`.
6. Remove `r_unit_shadows` cvar and options menu entry.
7. Keep `RF_NO_SHADOW` and existing UI/attachment uses.

### Phase 4: Fix matrix ownership and naming

Files:

- `client/cl_view.c`
- `client/tr_public.h`
- `games/world-of-warcraft/renderer/r_game.c`
- `games/warcraft-3/renderer/mdx/r_mdx_render.c`

Steps:

1. Rename helper functions for clarity:
   - `Matrix4_getLightMatrix` -> `Matrix4_getWorldShadowMatrix`
   - `Matrix4_getPreviewLightMatrix` -> `Matrix4_getPreviewShadowMatrix`
   - game-owned entity camera helpers stay game-owned.
2. Leave `viewDef.lightMatrix` in the public API for now to avoid wider churn, or rename it only in a dedicated cleanup
   commit after shadow maps are working.
3. Ensure `R_GameExtractEntityCamera` always writes a valid `lightMatrix` when it returns true.
4. Remove the current `RDF_USE_ENTITY_CAMERA` shadow-map skip in `R_RenderFrame`; the selected matrix already handles
   that view mode.

### Phase 5: Blend baked WC3 terrain shadows into depth shadowmap

Files:

- `games/warcraft-3/renderer/w3m/r_war3map.c`
- `games/warcraft-3/renderer/w3m/r_war3map_ground.c`
- `renderer/r_shader.c`
- `renderer/r_local.h`

Steps:

1. Keep `war3map.shd` loading (`R_FileReadShadowMap`), keep `TEX_TERRAIN_SHADOW`.
2. Add `uTerrainShadow` uniform to `struct shader_program` and register it (texture unit 3).
3. Bind `TEX_TERRAIN_SHADOW` (or `TEX_WHITE` for WoW/SC2) to unit 3 in `R_RenderFrame`.
4. Update terrain fragment shader (`fs_default`): sample `uTerrainShadow` via `v_texcoord2` and combine with `get_shadow()` using `min(depth_shadow, 1.0 - terrain_shadow)`.
5. Update model fragment shaders (MDX/M2/M3): add `uTerrainShadow` uniform and same combining logic so units standing on baked-shadow terrain also receive the darkening.
6. Remove `R_GameDrawTerrainShadows()` call from `R_RenderShadowMap()` and the full-map splat rendering from the main pass.
7. WoW/SC2: bind `TEX_WHITE` to unit 3 so `terrain_shadow` is always 0.

### Phase 6: Verification

Minimum commands:

```bash
make build-run-wow
make build-run-sc2
make build-run-wc3
make test-headless
```

Runtime checks:

- WC3 gameplay: units/buildings cast runtime depth shadows, no `ReplaceableTextures\Shadows\*.blp` blob quads.
- WC3 terrain: pre-baked `war3map.shd` shadows叠加 with dynamic shadows in fragment shader.
- WC3 glue/model-camera scenes: model shadow uses the portrait/model-camera matrix, not the overhead matrix.
- WoW gameplay: M2 characters cast onto terrain with no flat blob fallback.
- WoW CharacterCreate: character preview receives a tight preview shadow matrix.
- SC2 gameplay: M3 units cast runtime shadows with the overhead matrix.
- Disable cvar: shadow-map pass is skipped and all games still render normally.

## Non-goals

- Do not add `entityState_t` fields for shadow type, shadow matrix, or shadow texture.
- Do not add game-specific asset names or mode checks to engine renderer code.
- Do not raise network/entity budgets for shadows.
- Do not hardcode WC3/WoW/SC2 matrix literals in `renderer/`; game-specific preview rules belong in the client view setup
  or the selected game renderer's entity-camera extraction.
