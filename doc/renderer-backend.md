# Renderer Frontend/Backend Split

## Problem

GL state changes (`glEnable`, `glDisable`, `glBlendFunc`, `glDepthMask`, `glDepthFunc`, `glColorMask`, `glCullFace`, `glPolygonOffset`, `glBlendEquation`, etc.) are scattered across every draw function in the renderer. Every 2D draw, every terrain pass, every model render, and every fog-of-war composite sets its own GL state inline via `R_Call(gl...)` before each draw call.

### Current call inventory

| State | Unique call sites |
|-------|-------------------|
| `glEnable(GL_CULL_FACE)` | 6 (engine) + 4 (game) |
| `glDisable(GL_CULL_FACE)` | 8 (engine) + 5 (game) |
| `glEnable(GL_DEPTH_TEST)` | 3 (engine) + 5 (game) |
| `glDisable(GL_DEPTH_TEST)` | 4 (engine) |
| `glDepthMask` | 3 (engine) + 10 (game) |
| `glDepthFunc` | 3 (engine) + 3 (game) |
| `glBlendFunc` | 10 (engine) + 12 (game) |
| `glColorMask` | 3 (engine) + 1 (game) |
| `glEnable(GL_BLEND)` | 6 (engine) + 8 (game) |
| `glPolygonOffset` | 0 (engine) + 4 (game) |
| `glBlendEquation` | 2 (engine) |
| `glEnable(GL_SCISSOR_TEST)` | 2 (engine) |
| **Total** | **~85 scattered GL state calls** |

Consequences:
- Same state is set repeatedly across unrelated draw functions (e.g. `glDisable(GL_CULL_FACE)` + `glEnable(GL_BLEND)` + `glBlendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` appears in `R_DrawChar`, `R_DrawFill`, `R_DrawImageBatch`, `R_DrawWireRect`, `R_DrawBoundingBox`, `R_DrawMinimapCameraRect`).
- No way to know what state a function expects vs. what state it leaves behind.
- State changes between consecutive identical surfaces are never skipped.
- Game-specific renderers (WC3, SC2, WoW) duplicate the same boilerplate.

## Reference: Doom 3 Frontend/Backend Split

Doom 3 (id Tech 4) solves this with a clean two-phase architecture:

1. **Frontend** (`R_*`): walks the scene, culls, sorts surfaces by material sort key, builds a linked-list command buffer. Never touches GL.
2. **Backend** (`RB_*`): walks the command buffer, executes draw commands. Owns all GL state.
3. **State caching**: a `uint64` bitmask packs blend/depth/stencil/color-mask state. `GL_State(bits)` XORs against cached state and only issues GL calls for changed bit groups.
4. **Sorting**: surfaces are sorted by material, so consecutive opaque geometry shares identical state bits — the XOR is zero, no GL calls issued.

## Proposed Design

### Phase 1: Backend State Cache (`r_backend.h` / `r_backend.c`)

Introduce a `backEndState_t` struct that owns all GL state. Never issue raw `glEnable`/`glDisable`/`glBlendFunc`/etc. directly — always go through state-change helpers that compare against cached state.

```
backEndState_t:
    uint32  glStateBits;      // packed blend/depth/color-mask state
    GLenum  faceCulling;      // cached cull face mode (GL_BACK / GL_FRONT / GL_NONE)
    GLenum  depthFunc;        // cached depth function
    DWORD   polygonOffsetScale, polygonOffsetBias;
    DWORD   blendEquation;    // GL_FUNC_ADD / GL_MAX
    DWORD   activeTextureUnit;
    DWORD   currentShader;
    DWORD   currentVAO;
    DWORD   currentFBO;
    RECT    currentScissor;
```

State-change helpers:

```c
void RB_State(uint32 bits);       // packed blend+depth+mask, delta-checked
void RB_Cull(GLenum mode);        // GL_BACK / GL_FRONT / GL_NONE
void RB_PolygonOffset(float scale, float bias);
void RB_BlendEquation(GLenum eq);
void RB_Scissor(LPCRECT r);
void RB_BindShader(DWORD progid);
void RB_BindVAO(DWORD vao);
void RB_BindFBO(DWORD fbo);
void RB_SetViewport(LPCRECT r);
```

Each helper compares against the cached value and only issues the GL call on delta.

### Phase 2: Migrate existing draw functions

Replace all direct GL state calls in these files with `RB_*` helpers:

**Engine renderer (move to `r_backend.c`):**
- `renderer/r_main.c` — `R_SetupGL`, `R_BeginFrame`, `R_EndFrame`, `R_SetupViewport`, `R_SetupScissor`, `R_RevertSettings`
- `renderer/r_draw.c` — `R_DrawChar`, `R_DrawFill`, `R_DrawImageBatch`, `R_DrawWireRect`, `R_DrawBoundingBox`, `R_DrawMinimapCameraRect`, `R_SetBlending`
- `renderer/r_fogofwar.c` — multi-pass FoW composite
- `renderer/r_particles.c` — particle blend setup
- `renderer/r_texture.c` — texture parameter setup

**Game renderers (use RB_* from their r_game.c):**
- `games/warcraft-3/renderer/w3m/r_war3map.c` — terrain depth/blend passes
- `games/warcraft-3/renderer/w3m/r_terrain_layers.c` — layer blend
- `games/wow/renderer/wow/r_wowmap.c` — WoW terrain
- `games/wow/renderer/wow/r_wowmap_splat.c` — splat with polygon offset
- `games/wow/renderer/wow/r_wowmap_grass.c` — grass blend/cull
- `games/wow/renderer/m2/r_m2.c` — M2 model depth/blend
- `games/sc2/renderer/sc2/r_sc2map.c` — SC2 terrain
- `games/sc2/renderer/m3/r_m3_load.c` — M3 material blend matrix
- `games/sc2/renderer/r_game.c` — SC2 game draw

### Phase 3: Sort draw surfaces (optional, higher impact)

After the state cache is in place and all GL calls go through `RB_*`, add surface sorting by material state:

1. Assign each surface a sort key from its shader + blend mode + cull mode.
2. In the backend pass, sort `drawSurfs[]` by this key before issuing draw calls.
3. Consecutive surfaces with the same sort key produce zero `RB_State()` deltas.

This is a separate step because the current renderer doesn't maintain a `drawSurf[]` array — entities and terrain are drawn immediately. The sort step can be deferred to a later phase once the state cache is proven.

## Key Constraints

- The `R_Call(gl...)` macro is kept for now — it wraps with error checking under `DIAG_OUTPUT`. `RB_*` helpers will use `R_Call` internally.
- The stdout renderer (`r_stdout.c`) does not need GL state — it already prints draw calls. It stays as-is.
- The `refExport_t` API boundary is unchanged — this is internal renderer cleanup.
- Game-specific renderers access `RB_*` through `r_backend.h` (included via `r_local.h`).

## Files To Create / Modify

| File | Action |
|------|--------|
| `renderer/r_backend.h` | **New** — `backEndState_t` struct, `RB_*` function prototypes |
| `renderer/r_backend.c` | **New** — `RB_*` implementations, `RB_ResetState()` for frame start |
| `renderer/r_local.h` | Add `#include "r_backend.h"` |
| `renderer/r_main.c` | Replace GL state calls in `R_SetupGL`, `R_BeginFrame`, `R_EndFrame`, `R_SetupViewport`, `R_SetupScissor`, `R_RevertSettings` |
| `renderer/r_draw.c` | Replace GL state calls in all `R_Draw*` functions |
| `renderer/r_fogofwar.c` | Replace GL state calls in `R_RenderFogOfWar` |
| `renderer/r_particles.c` | Replace GL state calls in `R_DrawParticles` |
| `renderer/r_ents.c` | Replace GL state calls in `R_DrawEntities`, `R_RenderModel` |
| `games/warcraft-3/renderer/w3m/r_war3map.c` | Replace GL state calls |
| `games/warcraft-3/renderer/w3m/r_terrain_layers.c` | Replace GL state calls |
| `games/wow/renderer/wow/r_wowmap.c` | Replace GL state calls |
| `games/wow/renderer/wow/r_wowmap_splat.c` | Replace GL state calls |
| `games/wow/renderer/wow/r_wowmap_grass.c` | Replace GL state calls |
| `games/wow/renderer/m2/r_m2.c` | Replace GL state calls |
| `games/sc2/renderer/sc2/r_sc2map.c` | Replace GL state calls |
| `games/sc2/renderer/m3/r_m3_load.c` | Replace GL state calls |
| `games/sc2/renderer/r_game.c` | Replace GL state calls |

## Verification

1. `make clean && make` — builds without warnings
2. `make run-ui-text UI_CMD=menu_main` — stdout renderer unaffected
3. Visual regression: launch each game (WC3, SC2, WoW), load a map, verify rendering matches pre-change
4. Grep for stray GL state calls: `rg 'gl(Enable|Disable|BlendFunc|DepthFunc|DepthMask|ColorMask|CullFace|PolygonOffset|BlendEquation)\b' renderer/ games/*/renderer/` — should only appear inside `r_backend.c`
