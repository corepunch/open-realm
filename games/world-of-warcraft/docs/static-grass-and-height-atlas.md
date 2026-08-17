# GPU Terrain Height Atlas and Static Grass Batches

Status: **implementation design; not yet implemented**. This replaces the per-instance
draw architecture described in [grass-rendering-system.md](grass-rendering-system.md),
while retaining WoW's ADT and DBC data as the source of truth.

## Decision

Move terrain elevation to an exact GPU height atlas and replace grass entities with
renderer-owned, immutable instance batches. Cull grass once per MCNK-sized patch, never
per clump. Keep the actual `GroundEffectDoodad` M2 geometry and materials first; a
procedural crossed-blade renderer is an optional quality mode, not the primary design.

Do **not** start with compute shaders, GPU indirect culling, a geometry clipmap, or a
single expanded VBO containing every copied M2 vertex. The renderer currently targets
OpenGL 3.1 / GLSL 1.40, and the streamed 3x3-ADT window is small enough for static
templates, static instance buffers, and coarse CPU culling. Those changes remove the
measured bottleneck without raising the renderer baseline.

## Evidence and Current Bottleneck

The reported Time Profiler capture attributes 6.65 s to `Wow_DrawGrass`; 1.76 s is
`Wow_EntityInView`, while only 170 ms is `R_GameRenderModelInstanced`. The draw itself is
not the main problem. `Wow_DrawGrass` (`renderer/wow/r_wowmap_grass.c`) currently:

1. walks the flat `wow_world.ground_effects` list twice;
2. performs distance and sphere/frustum tests twice for every clump;
3. searches the model group array linearly twice;
4. rebuilds and copies a `MATRIX4` for every visible clump every frame;
5. uploads those matrices again for instanced drawing.

The direct ground-effect matrix path in `R_GameEntityMatrix` is a useful temporary
optimization, but the remaining list traversal, duplicate culling, matrix construction,
and uploads are all avoidable. Grass is immutable for the lifetime of a loaded ADT.

The current material state is also wrong for at least the locally verified Classic
grass set. `M2_RenderInstanced` notices `BLEND_MODE_ALPHAKEY`, then unconditionally enables
alpha blending and disables depth writes for all ground effects. Inspection of
`World\NoDXT\Detail\ElwFlo01-03.m2` and `ElwGra01-05.m2` in the local `model.MPQ` found
render flags `(flags=4, blend=1)` for every model; WoW blend 1 maps to
`BLEND_MODE_ALPHAKEY` in `m2_blend_mode`.

## Authoritative Data Flow

The target path is:

```text
MCNK MCVT (145 exact relative heights) ---------> height atlas ------> terrain + grass VS
MCNK MCNR (145 authored normals) --------------> normal atlas ------> terrain VS
MCNK 8x8 predicted-layer map ------------------+
MCNK 64-bit no-effect-doodad map ---------------+--> grass patch builder
MCLY[layer].effect_id --------------------------+
GroundEffectTexture.dbc (models/weights/amount)-+
GroundEffectDoodad.dbc (model path) ------------+--> immutable batches of actual M2s
MCAL alpha maps ------------------------------------> terrain splatting
```

The parser already copies the 16-byte predicted-layer map at MCNK header offset `0x40`
into `pred_tex`, but then discards it with `(void)pred_tex`. It does not copy the adjacent
8-byte no-effect-doodad mask at `0x50`. Reverse-engineered tooling identifies the former
as eight rows of 8 two-bit MCLY indices and the latter as one suppression bit per 8x8
cell. This is stronger placement evidence than guessing a dominant effect layer from a
64x64 MCAL sample. Parse and test both maps before changing placement.

Public reverse engineering does not establish Blizzard's exact CPU/GPU batching
implementation. It does establish that the assets are real M2 ground-effect doodads and
that MCNK supplies per-cell layer/suppression data. Do not present the renderer design
below as recovered Blizzard client code.

## Exact Height Atlas

### Preserve MCVT; do not resample it

MCVT is not a conventional 17x17 height field. It is a diamond-fan mesh with 145 values:
81 outer points (9x9) and 64 cell-center points (8x8). In the loaded array, each of the
first eight logical rows occupies 17 values (9 outer followed by 8 centers), and the last
row has only 9 outer values. `Wow_McvtCoords`, `Wow_AddTerrainCell`, and
`Wow_HeightInCell` describe the exact topology.

Upload each MCNK as a **17x9 texel tile** without changing sample values:

- columns `0..8`: outer points for that row;
- columns `9..16`: center points for rows `0..7`;
- row 8, columns `9..16`: unused and initialized to zero;
- atlas tile `(cx, cy)` starts at `(cx * 17, cy * 9)`.

For the current 48x48-MCNK streamed window, a `GL_R32F` height atlas is only
`816x432`, about 1.35 MiB. Start with `R32F`: it avoids an unmeasured `R16F` precision
tradeoff and preserves the CPU float samples. Optimize the format only after measuring
the actual local-height range and an error budget. Store relative MCVT height and supply
the MCNK base Z per draw/patch; do not store absolute world Z in half precision.

Use `texelFetch`, not filtered `texture`, for terrain vertices and grass height. Grass
must reproduce `Wow_HeightInCell`: fetch the four outer corners plus the center and use
the same four triangle regions/barycentric interpolation. Bilinear filtering changes the
authored diamond surface and produces a different Z near cell centers.

### Normals and boundaries

Upload MCNR alongside MCVT, preferably as `RGB8_SNORM`, after the existing ADT-to-world
axis conversion. Use the authored normal at terrain vertices. When MCNR is absent,
derive the same accumulated normals once while loading and upload those; never derive
them per frame.

No gutter is required while all height and normal reads use `texelFetch`. If later work
uses filtered sampling, add explicit duplicated borders; otherwise filtering will bleed
between unrelated MCNK atlas tiles.

### Atlas lifecycle

Mirror the alpha-atlas coordinate system and ADT-window lifecycle:

- allocate height and normal atlases when the 3x3 ADT window is established;
- upload one 17x9 height tile and one normal tile when an MCNK loads;
- initialize unused texels deterministically;
- clear/recreate the atlases when `alpha_origin_x/y` changes, exactly as the current
  loader recreates the streamed world;
- release them in `Wow_FreeWorld` and reset shader handles in renderer shutdown.

The height atlas is much smaller than the existing 3072x3072 RGBA8 alpha atlas. Do not
pack height into the alpha atlas: the texel grids, filtering rules, formats, and update
rates are different.

## GPU Terrain Path

GPU height storage does not require geometry clipmaps yet. The current terrain range and
3x3-ADT streaming window already provide bounded spatial subdivision.

1. Create one immutable terrain template containing the existing 64 diamond fans (12
   triangle-list vertices per cell). Each vertex needs only its MCVT texel identity,
   local UV, and cell/hole identity; world XYZ and normal come from the atlases.
2. Draw each visible MCNK with its chunk origin, base Z, atlas tile, alpha-atlas tile,
   and hole mask. Preserve the current four terrain texture bindings and MCAL splat path.
3. In the vertex shader, fetch exact height/normal samples and build world position.
   The static topology must reproduce `Wow_AddTerrainCell`'s triangle order exactly.
4. Stop allocating a unique expanded terrain VBO per MCNK after visual and numeric
   parity is proven. Keep CPU `chunk->heights` while CPU collision, splats, and gameplay
   height queries still need it; GPU rendering does not imply deleting authoritative CPU
   data.
5. Restore actual hole behavior. The current `WOW_IGNORE_TERRAIN_HOLES=1` is unrelated
   technical debt. A template vertex can carry its 8x8 cell ID and the vertex shader can
   clip all three vertices for a holed cell, or the loader can retain a small per-chunk
   index/range list. Choose after measuring; do not silently ignore holes.

If terrain later becomes vertex/draw-call bound at much larger distances, geometry
clipmaps are a valid second design: nested camera-centered grids sample a mipmapped
height image while static vertex/index buffers stay fixed. GPU Gems 2 documents this
exact motivation and division of work. It is not required to solve the present grass
CPU profile, and a clipmap must preserve WoW holes, texture layers, MCNK seams, and
collision/render height parity before replacing the MCNK grid.

## Grass Representation

### Build immutable patch batches

Replace `wowDoodadInstance_t` grass entities with renderer-owned patches keyed by MCNK
or, if draw-call profiling requires it, a 2x2/4x4 MCNK block. A patch contains:

- conservative world bounds including the tallest selected ground-effect M2 and wind;
- a compact list of model batches;
- an immutable GPU instance buffer per batch;
- counts for diagnostics;
- no `renderEntity_t`, linked-list node, or per-frame matrix storage.

An instance descriptor should contain only values that vary per placement: local XY,
yaw, optional scale, and a deterministic seed/animation phase. Do not store a `mat4`.
The vertex shader fetches terrain Z, reconstructs the existing ADT-to-world basis, and
applies yaw. Keep the M2 vertex buffer shared by all placements, preserving its texture,
UVs, tiny geometry, and animation. The locally inspected `ElwGra01.m2`, for example, is
only eight vertices / twelve indices with one bone; expanding thousands of copies into
one VBO wastes memory and makes animation harder.

Build descriptors once when the MCNK is loaded:

1. decode the predicted layer for each of the 8x8 cells;
2. reject cells set in the no-effect-doodad mask;
3. resolve `MCLY[layer].effect_id`;
4. resolve `GroundEffectTexture` density/amount and weighted doodad choices;
5. create deterministic attempts from MCNK world identity + cell + attempt index;
6. store local XY/yaw/seed, grouped by resolved `LPCMODEL`;
7. upload each completed group once and cache its bounds.

Do not re-read MCAL or DBCs in `Wow_DrawGrass`. MCAL may be used only if archive-version
validation proves it participates in that client's placement rules; it must not replace
the parsed predicted-layer and suppression maps by convenience.

### Coarse visibility, never per clump

`Wow_DrawGrass` should enumerate only patch coordinates intersecting the grass-distance
circle, not scan every loaded patch or every instance. For each candidate patch:

1. squared-distance reject using patch bounds;
2. one frustum AABB/sphere test for the whole patch;
3. submit its already grouped static buffers.

At MCNK scale this replaces tens of thousands of repeated tests with roughly hundreds
of cheap patch tests. If draw calls then dominate, merge 2x2 or 4x4 patches and retest.
Larger patches reduce calls but submit more off-screen grass; select size from CPU and GPU
timers, not intuition.

With the existing GL 3.1 baseline, static instanced attributes plus ordinary instanced
draws are sufficient. A future renderer backend may compact visible patches into an
indirect buffer on the GPU, but compute + `MultiDrawIndirect` is not the first milestone.

### Why not one camera-following grass mesh?

The Angry Bots rain technique—an immutable volume of slots, wrapped around the camera
and animated in a vertex shader—is excellent for visually interchangeable particles.
Grass shares the useful ideas of immutable slots, deterministic world-space hashing,
and shader animation. WoW ground effects add constraints rain does not have:

- per-cell authoritative layer/suppression data;
- up to four weighted, actual M2 models per effect;
- model-specific materials, UVs, geometry, bounds, and animation;
- a non-bilinear diamond height surface.

Therefore use a static **descriptor mesh per streamed patch/model**, not one universal
expanded mesh. A camera-following procedural crossed-blade mode can be added later for
very distant grass, where replacing actual M2 silhouettes is an explicit LOD decision.

## Alpha, Sorting, and Fade

Grass does not inherently require sorting. The answer depends on each M2 batch's
authoritative blend mode:

| Material | Blend | Depth write | Sorting |
|---|---:|---:|---|
| opaque | off | on | none |
| alpha-key/cutout | off; shader discard | on | none |
| true alpha blend | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | off | back-to-front or OIT |
| additive | additive | normally off | usually order-independent |

For the verified Classic Elwynn grass/flower assets, use alpha-key cutout and depth
writes. This lets the depth buffer resolve overlap, so per-blade sorting is unnecessary.
Khronos' transparency guidance explicitly distinguishes cutout foliage from true
translucency: discard transparent texels and retain depth testing/writes. NVIDIA's
vegetation chapters likewise identify sorting alpha-blended foliage as expensive and
describe alpha-to-coverage as an edge-quality alternative under MSAA.

Implement material handling per M2 batch; remove the blanket ground-effect blend
override in `M2_RenderInstanced`. For distance fade:

- with MSAA, test `GL_SAMPLE_ALPHA_TO_COVERAGE` and keep depth writes;
- without MSAA, use a stable screen/world-space dither threshold before discard;
- do not convert every alpha-key asset to smooth alpha blending merely to fade it;
- if a real `BLEND_MODE_BLEND` ground-effect asset is found, sort **patches/batches**
  back-to-front first. Only add weighted blended OIT if visible artifacts and profiling
  justify it.

Cutout/discard can inhibit early depth optimizations on some GPUs, so keep grass shaders
small, draw terrain/opaque geometry first, render roughly front-to-back by patch, use
good alpha mipmaps, and measure overdraw. Do not add a depth prepass automatically; for
tiny grass triangles it may cost more vertex/alpha work than it saves.

## Shader Work

The grass vertex shader needs:

- compact instance attributes (`local_xy`, yaw/scale, seed);
- MCNK origin/base Z and height-atlas tile;
- exact diamond height lookup;
- deterministic variation and optional per-instance animation phase;
- existing M2 bone animation followed by instance transform;
- wind displacement bounded by the patch bounds;
- distance fade value for alpha-to-coverage/dither.

The fragment shader should keep each M2 batch's texture and blend semantics. Do not
replace M2 textures with the orphaned procedural `fs_wow_grass` unless implementing the
explicit procedural LOD.

The terrain vertex shader needs exact height/normal fetch, chunk/world transform,
terrain UV, alpha-atlas coordinate, and cell-hole handling. Keep height lookup in one
shared GLSL snippet or generated string used by terrain, grass, and eventually splats;
duplicated coordinate math will drift.

## Implementation Sequence

### Phase 0: establish measurements and parity scenes

- Add counters/timers for loaded patches, candidate patches, visible patches, submitted
  instances, grass draw calls, grass CPU time, and (where supported) GPU time.
- Capture a bounded baseline in a dense Elwynn field and at least one road, steep slope,
  ADT seam, WMO footprint, and sparse/flower effect.
- Record model blend modes and predicted/no-effect cell values for those locations.

### Phase 1: parse authoritative placement maps

- Add the 64-bit no-effect-doodad field to the MCNK loader and pass both maps into grass
  construction.
- Add pure tests for two-bit layer decoding, bit orientation, invalid layer handling,
  suppression, weighted selection, and deterministic seeds.
- Compare rendered cell occupancy against the existing client or a trusted viewer before
  removing the MCAL heuristic. Log unsupported archive layouts; do not fall back silently.

### Phase 2: add the exact height/normal atlas

- Allocate/upload 17x9 exact MCVT tiles and normal tiles.
- Add a CPU reference helper matching the GLSL diamond interpolation.
- Test every MCVT vertex and randomized points in all four triangles; GPU/CPU height must
  agree within the chosen float tolerance.
- Move terrain to the shared static template, then compare seams, slopes, splats, normals,
  and holes before deleting per-chunk render VBOs.

### Phase 3: eliminate grass entities and frame rebuilds

- Introduce patch/model batch structs and static compact instance buffers.
- Build once on MCNK load; free on world/window unload.
- Add a static-instance M2 draw path that does not upload matrices each frame.
- Enumerate nearby patches spatially, cull once per patch, and submit cached model groups.
- Remove `wow_world.ground_effects`, `wow_grass_scratch`, `RF_GROUND_EFFECT` matrix logic,
  and `Wow_AddGroundEffectInstance` only after visual and counter parity.

### Phase 4: fix materials and fade

- Honor opaque, alpha-key, blended, and additive M2 batch modes independently.
- Verify alpha-key assets use discard + depth writes and require no sorting.
- Implement alpha-to-coverage when MSAA is active and deterministic dither otherwise.
- Test intersecting clumps, camera rotation, fade band, mip distance, and patch seams.

### Phase 5: tune only from the new profile

- Try MCNK, 2x2, and 4x4 patch sizes; retain the best CPU/GPU balance.
- Add a procedural far LOD only if M2 vertex/fill cost, not CPU submission, becomes the
  next bottleneck.
- Consider multi-draw/indirect and geometry clipmaps only in a renderer-capability tier,
  after the GL 3.1 path is correct and measured.

## Acceptance Criteria

- `Wow_DrawGrass` performs zero per-instance visibility tests, matrices, allocations, or
  buffer uploads per frame.
- A stationary camera causes no grass buffer updates.
- CPU work scales with candidate patches and model batches, not total loaded clumps.
- Grass placement is stable across camera movement and ADT-window reloads.
- Terrain shader height matches `Wow_HeightInCell`; no seam or center-fan shape changes.
- Alpha-key grass writes depth and does not require sorting.
- No grass appears in parsed no-effect cells or terrain holes.
- Dense-field grass CPU time improves by at least an order of magnitude from the supplied
  profile; final frame and GPU times are recorded, not inferred from draw-call count.
- `make run-sc2` still compiles the shared renderer and `make test` is green.

## Diagnostic and Verification Commands

Inspect the DBC/model contract before changing a new archive version:

```sh
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\GroundEffectTexture.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\GroundEffectDoodad.dbc'
build/bin/m2tool -mpq data/world-of-warcraft/model.MPQ -mpq data/world-of-warcraft/texture.MPQ \
  -model 'World\NoDXT\Detail\ElwGra01.m2' --dump-all
```

`m2tool --dump-all` currently identifies the render-flags array but does not print its
values; extend it before making this a routine material audit. The local `ElwGra01.m2`
tuple quoted above was confirmed directly at that model's reported render-flags offset:

```sh
build/bin/mpqtool -mpq data/world-of-warcraft/model.MPQ cat \
  'World\NoDXT\Detail\ElwGra01.m2' > /private/tmp/ElwGra01.m2
xxd -g 2 -s 2688 -l 4 /private/tmp/ElwGra01.m2  # 0400 0100: flags=4, blend=1
```

Run a bounded world scene and tests:

```sh
build/bin/openwow -data data/world-of-warcraft +map World/Maps/Azeroth/Azeroth.wdt +com_frame_limit 300
make run-sc2 ARGS='+com_frame_limit 100'
make test
```

Use the `xctrace` / `xctraceprof` workflow in
[`docs/diagnostic-tools.md`](../../../docs/diagnostic-tools.md) and focus on
`Wow_DrawGrass`, `Wow_DrawTerrainAndWmos`, and the static M2 submission function. Add GPU
timer queries around terrain and grass separately when diagnosing a CPU/GPU handoff.

## Research References

- [GPU Gems: Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass)
  discusses clustered grass geometry, block-level management, depth writes, and the
  limitations of alpha-blended sorting.
- [GPU Gems 2: Toward Photorealism in Virtual Botany](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-1-toward-photorealism-virtual-botany)
  discusses deterministic procedural placement and why sorted alpha foliage is costly.
- [GPU Gems 3: Next-Generation SpeedTree Rendering](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-4-next-generation-speedtree-rendering)
  describes alpha-to-coverage for foliage edges.
- [Khronos: Transparency Sorting](https://wikis.khronos.org/opengl/Transparency_Sorting)
  explains why cutouts use discard + depth writes without translucency sorting.
- [Khronos: Early Fragment Test](https://wikis.khronos.org/opengl/Early_Depth_Test)
  records the performance caveat that fragment `discard` can inhibit early depth on
  some hardware.
- [GPU Gems 2: Terrain Rendering Using GPU-Based Geometry Clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
  is the reference for a later large-distance terrain LOD, not a prerequisite here.
- [wowdev/noggit3 ground-effects investigation](https://github.com/wowdev/noggit3/issues/87)
  documents the MCNK 8x8 two-bit layer map and 64-bit suppression map. Treat this as
  reverse-engineered evidence and validate orientation/version against local assets.

## Known Risks

- MCNK ground-effect map meaning/orientation may vary by client version; tests and local
  visual validation are mandatory.
- Some ground-effect M2s may use real blending or more complex animation/materials than
  the verified Elwynn set. Dispatch from material data rather than hardcoding alpha-key.
- Patch merging can trade a solved CPU problem for GPU overdraw. Keep coarse-culling and
  GPU timing counters visible while tuning.
- A shader-clipped absent blade still consumes vertex work. Density rejection is better
  resolved while building static descriptors than by filling a universal grid and moving
  rejected vertices off-screen.
- The CPU height copy remains required by collision/gameplay and by validation. The GPU
  atlas is a rendering representation, not a new source of truth.
