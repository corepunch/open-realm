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

## Alpha-key coverage contract

The shared renderer requests 4x MSAA by default (`r_msaa 4`) before creating the SDL
window. After context creation it logs both SDL's returned attributes and OpenGL's
`GL_SAMPLE_BUFFERS`/`GL_SAMPLES`; the latter determine `tr.msaa_samples`. Context creation
is retried without MSAA only when the requested multisample visual is unavailable, and
that downgrade is logged.

MDX, M2, M3, alpha-key particles, and WoW grass share one material contract:

- the fragment shader remaps texture alpha around the authoritative cutoff with
  `smoothstep`/`fwidth`; no renderer shader uses `discard`;
- with a multisampled target, `R_SetAlphaKeyState(true)` disables blending, enables
  `GL_SAMPLE_ALPHA_TO_COVERAGE`, and retains depth writes;
- without MSAA, it logs the lack of coverage support at renderer initialization and uses
  ordinary alpha blending with depth writes disabled, preserving visibility at reduced
  overlap quality;
- every non-alpha-key material path and frame start disables alpha-to-coverage so the
  state cannot leak into true blended, additive, UI, or terrain passes.

Alpha-to-coverage is for cutout coverage, not general order-independent transparency.
True blended and additive material modes keep their existing blend/depth contracts.

## Renderer profiling cvars

Use `r_stats 1` to print one averaged line per second:

```text
[R_STATS] fps=... draws=... vertices=... triangles=... instances=...
[WOW_STATS] terrain=drawn/considered ... wmo=instances/models groups=... draws=... textures=... model_batched=... doodads=visible/candidates ...
```

`R_STATS` counts every renderer draw submission, including UI, minimap, fog, particles,
models, terrain, and instanced amplification. `instances` is the sum of each draw's
instance count; non-instanced draws contribute one. Use the WoW pass toggles to isolate
view-dependent costs without changing asset loading or simulation:

| Cvar | Default | Draws |
|---|---:|---|
| `r_grass` | 1 | GroundEffectDoodad instanced batches |
| `r_doodads` | 1 | ADT doodad M2s and map-object debug geometry |
| `r_wmos` | 1 | WMO groups in the world |
| `r_terrain` | 1 | ADT terrain in the world |
| `r_minimap` | 1 | Blizzard minimap tiles (normally 1-4 draws) |
| `r_entities` | 1 | Snapshot entities |
| `r_particles` | 1 | Particle batches |
| `r_fogofwar` | 1 | Fog-of-war passes |
| `r_fog` | 1 | WoW distance fog (turning it off exposes the hard clip) |
| `r_fog_start` | 500 | WoW outdoor fog start in world units |
| `r_fog_end` | 650 | WoW fully opaque fog / WMO CPU-cull distance |
| `r_swapinterval` | 1 | SDL/OpenGL presentation interval (`0` uncapped request, `1` display synchronized) |

For the Human start and left-facing slowdown, launch with `+set r_stats 1`, turn left, then
toggle one pass at a time in the console, for example `set r_wmos 0`, `set r_doodads 0`,
`set r_grass 0`, and `set r_minimap 0`. Compare both FPS and draw counts; restore each
toggle before testing the next so effects do not overlap.

Renderer cvars are registered during common initialization, so the shorter Quake-style
console form is also valid: `r_grass 0`, `r_wmos 0`, and `r_stats 1`. `set` remains useful
for creating an ad-hoc cvar; renderer controls must not rely on that side effect.

### WoW Human-start checkpoint (2026-08-18)

At 2048x1536 with 4x MSAA, the forward-facing Human start submits about 1,744 draws,
6.84 million vertices, 2.28 million triangles, and 467,000 instances per frame. Its
world pass contains 133 visible terrain chunks, 505 WMO batch draws, and 276 visible
doodads. Isolating one pass at a time found:

| Disabled pass | Draws/frame | FPS | Approximate draw reduction |
|---|---:|---:|---:|
| none | 1,744 | 94-95 | - |
| live minimap | 1,378 | 94 | 367 |
| WMO | 992 | 103 | 750 |
| doodads | 1,430 | 104 | 315 |
| grass | 1,731 | 92 | negligible |

The FPS values are directional rather than additive because macOS Metal/OpenGL driver
work varies between runs. The draw deltas are the useful isolation signal. Grass is 14
persistent instanced batches and is not the draw-call bottleneck.

The original WMO pass reused the four-layer terrain shader by binding every WMO texture
to units 0-3 and white to unit 4 for every material. This matched profiler time in
`glActiveTexture`, `glBindTexture`, sampler loading, and Metal pipeline preparation.
The WMO single-texture shader branch now samples unit 0 only; the same scene improved
from roughly 82-90 FPS to 94-95 FPS without changing draw count. The remaining scalable
cost was the number of separately submitted WMO and doodad batches, plus the duplicate
live-minimap world pass.

After static-doodad instancing, authoritative minimap tiles, and hybrid WMO material
batching, the same forward Human-start view measured about 910 total draws at the 120
FPS presentation ceiling. Its WMO work fell from 486 to 207 draws: 10 of 17 visible
instances used model-wide material batches, while sparse instances retained per-group
culling. The minimap fell from roughly 367 duplicated world draws to at most four UI
quads. `r_swapinterval 0` is useful for requesting uncapped presentation, but macOS's
OpenGL-on-Metal path may still present at the display's 120 Hz ceiling.

The FPS overlay uses one batched system-font submission and displays
`FPS ##  Drawcalls ##`; its draw count is captured before the overlay itself.

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
