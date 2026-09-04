# Renderer Frontend/Backend Split

Tracking: [#89](https://github.com/corepunch/open-realm/issues/89) (implement),
[#93](https://github.com/corepunch/open-realm/issues/93) (research). Packed shader
contracts: [model-shader.md](architecture/model-shader.md). Platforms:
[build-and-renderer-platforms.md](build-and-renderer-platforms.md).

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

The shared renderer requests the compile-time `MSAA=0/2/4/8` sample count before creating the SDL
window; the bandwidth-safe default is zero. After context creation it logs both SDL's returned attributes and OpenGL's
`GL_SAMPLE_BUFFERS`/`GL_SAMPLES`; the latter determine `tr.msaa_samples`. Context creation
is retried without MSAA only when the requested multisample visual is unavailable, and
that downgrade is logged.

MDX, M2, M3, alpha-key particles, and WoW grass share one material contract:

- an `MSAA=0` fragment shader discards below the authoritative cutoff; an MSAA
  build remaps the edge with `smoothstep`/`fwidth`;
- with a multisampled target, `R_SetAlphaKeyState(true)` disables blending, enables
  `GL_SAMPLE_ALPHA_TO_COVERAGE`, and retains depth writes;
- if an MSAA build cannot obtain a multisampled context, it logs the downgrade and uses
  ordinary alpha blending with depth writes disabled; an `MSAA=0` build uses hard discard
  and depth writes;
- every non-alpha-key material path and frame start disables alpha-to-coverage so the
  state cannot leak into true blended, additive, UI, or terrain passes.

Alpha-to-coverage is for cutout coverage, not general order-independent transparency.
True blended and additive material modes keep their existing blend/depth contracts.
See [Build And Renderer Platforms](build-and-renderer-platforms.md) for build commands and the GLES3 contract.

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

## Target API shape

Do **not** emulate OpenGL 1.x (`glEnable(GL_LIGHTING)`, fog modes, texture-env, uber-shader permutations).
Call-site `glUniform*` is already gone (`shader_desc_t` + typed `state` + `R_ApplyShader`). The remaining work
follows Sebastian Aaltonen's thin-pipeline / root-struct design, mapped onto the current OpenGL 3.1 / GLES3
backend. Issues: [#89](https://github.com/corepunch/open-realm/issues/89),
[#93](https://github.com/corepunch/open-realm/issues/93). Source:
[SebAaltonen, 2026-09-03](https://x.com/SebAaltonen/status/2095562458467287266).

| Steal | Meaning here |
|-------|----------------|
| Root struct + one push | One `alignas(16)` C struct shared with the shader: transforms, material, texture indices. Fill a stack/bump copy and upload once (UBO / persistent-mapped buffer today; push constants later). |
| Thin pipeline objects | Graphics pipeline = shaders + color/depth formats + a small raster blob (blend, depth write/test, cull, write mask). One short create call. |
| Blend/depth as small objects | Attached at pipeline create. On GL they can also change cheaply; the backend diffs them with a Doom 3-style bitmask. Not a giant opaque PSO. |
| Texture/sampler heap | 32-bit indices in the root struct. No per-draw `glBindTexture` storm. GLES without bindless needs an explicit, logged strategy. |
| 64-bit GPU pointers | Not required on GL/GLES. Do not block #89 on `vkCmdPushDataEXT` or buffer device address. |

Lighting, fog, and grass stay packed data in the root struct (`MODELLIGHTING`, `uGrassParams`, model fog flags).
See [model-shader.md](architecture/model-shader.md). They are not public `lightingEnabled` / `fogMode` bits that pick shader permutations.

Typical draw shape: bind pipeline if changed, push root once, draw.

## Backend state cache (Phase 1 of #89)

Doom 3's `R_*` / `RB_*` split and `GL_State(uint64)` XOR remain the **GL implementation** of cheap blend/depth/cull
packets. Frontend never issues `glEnable` / `glBlendFunc` / `glDepthMask`. Backend owns those calls.

```
backEndState_t:
    uint32  glStateBits;      // packed blend/depth/color-mask
    GLenum  faceCulling;
    GLenum  depthFunc;
    DWORD   polygonOffsetScale, polygonOffsetBias;
    DWORD   blendEquation;
    DWORD   activeTextureUnit;
    DWORD   currentPipeline;
    DWORD   currentVAO;
    DWORD   currentFBO;
    RECT    currentScissor;
```

```c
void RB_State(uint32 bits);
void RB_Cull(GLenum mode);
void RB_PolygonOffset(float scale, float bias);
void RB_BlendEquation(GLenum eq);
void RB_Scissor(LPCRECT r);
void RB_BindVAO(DWORD vao);
void RB_BindFBO(DWORD fbo);
void RB_SetViewport(LPCRECT r);
```

Each helper compares against the cached value and only issues the GL call on delta. Later phases in #89
add thin pipeline objects, the root-struct push, and the texture heap. Surface sorting (by pipeline +
blend/depth bits) waits until draws are no longer immediate.

## Key constraints

- `R_Call(gl...)` stays for diagnostics; `RB_*` uses it internally.
- `refExport_t` is unchanged. Pipeline/root types live in the renderer (`r_backend.h` via `r_local.h`).
- Callers never reference `progid` or uniform locations. `shader_desc_t` remains the GLSL grammar until
  the root-struct push replaces per-field `glUniform*`.
- One representation per shader concept: no zero-count modes or parallel fallback uniforms.
- macOS GL 4.1, desktop GL 3.1, and GLES3 Mali-G31 each need an explicit transport. Missing extensions
  are logged; do not silently demote to a bind loop and call it bindless.

## Files for the state-cache step

| File | Action |
|------|--------|
| `renderer/r_backend.h` | **New** — `backEndState_t`, pipeline/root types, `RB_*` |
| `renderer/r_backend.c` | **New** — cache, later pipeline bind + root push |
| `renderer/r_local.h` | `#include "r_backend.h"` |
| `renderer/r_main.c`, `r_draw.c`, `r_fogofwar.c`, `r_particles.c`, `r_ents.c` | `RB_*` instead of raw GL state |
| WC3 / WoW / SC2 game renderers | Same; listed in #89 |

## Verification

1. `make clean && make` — no warnings.
2. Visual: WC3 ROC and TFT, SC2, WoW; load a map. `+screenshot N +com_frame_limit`.
3. `rg 'gl(Enable|Disable|BlendFunc|DepthFunc|DepthMask|ColorMask|CullFace|PolygonOffset|BlendEquation)\b' renderer/ games/*/renderer/` — only inside `r_backend.c`.
4. `r_stats 1` on a known scene so draw count does not regress silently.
