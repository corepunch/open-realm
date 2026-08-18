# GPU Terrain Height Atlas and Static Grass Batches

Status: **authoritative static M2 batches active; camera-grid prototype disabled**. ADT
load resolves GroundEffectTexture/GroundEffectDoodad records, loads each MPQ-authored M2
geometry/material, and compiles immutable transforms into one batch per resident model.
Temporary placement entities are then freed. Per frame, `Wow_DrawGrass` only submits the
resident model batches; the transform matrices live in `GL_STATIC_DRAW` instance VBOs,
while wind, distance fade, alpha remapping, and alpha-to-coverage are GPU work. Rebuilding
and uploading happen only when the ADT window changes.

The exact `R32F` height atlas and RGBA8 grass-control atlas remain available for the
future camera-grid path. That prototype is deliberately disabled: its 12-vertex generic
cross used hard-coded dimensions and constant green output, while its control texture
stored no GroundEffectDoodad model/material identity. Enabling it therefore replaced
the authoritative wider textured clumps with narrow untextured rectangles. Do not
enable it until real M2 geometry and material selection are encoded and visually match.

Bounded verification command (Human start is required so `playercreate` selects map 0):

```sh
make run-wow ARGS="+set wow_playerinfo '\\race\\Human\\sex\\Male\\class\\1\\appearance\\0' +map playercreate +com_frame_limit 100"
```

Add `+set r_stats 1` for averaged renderer draw/triangle/instance counts. Runtime isolation
toggles are documented in [renderer-backend.md](../../renderer-backend.md); the primary
ones here are `r_grass`, `r_doodads`, `r_wmos`, `r_terrain`, and `r_minimap`.

The corrected 2026-08-18 checkpoint loaded 2,304 MCNKs and compiled 465,524 authored
placements into 14 persistent M2 model batches. It obtained a verified 4x MSAA
framebuffer, loaded the authored materials without missing-texture reports, and exited
at the frame limit.

## Decision

Keep the authoritative `GroundEffectDoodad` M2 geometry, materials, and placement
matrices. Group placements once per resident ADT window, upload one immutable instance
VBO per M2 model, and submit one instanced draw per M2 material batch. Use
`GL_SAMPLE_ALPHA_TO_COVERAGE` for alpha-key edges and distance fade without sorting.

The verified Classic detail M2s contain a nominal 3,333 ms sequence but no keyed bone
tracks. Their movement therefore comes from root-anchored vertex-shader wind, phased by
world position and scaled by each M2's authored geometry height. Do not add per-instance
CPU animation.

The instanced shader receives an identity bone palette once when it is created. The
grass draw path does not evaluate M2 animation or rebuild/upload a 128-matrix palette per
model; only the shader time uniform changes each frame.

Retain the height/control atlas camera-grid experiment as a future option, but do not
enable it until the control data preserves the exact doodad model/material identity.
Do **not** start with compute shaders, GPU indirect culling, or geometry clipmaps; the
GL 3.1 baseline does not require them yet.

## Evidence and Current Bottleneck

The original Time Profiler capture attributes 6.65 s to `Wow_DrawGrass`; 1.76 s is
`Wow_EntityInView`, while only 170 ms is `R_GameRenderModelInstanced`. The draw itself is
not the main problem. `Wow_DrawGrass` (`renderer/wow/r_wowmap_grass.c`) currently:

1. walks the flat `wow_world.ground_effects` list twice;
2. performs distance and sphere/frustum tests twice for every clump;
3. searches the model group array linearly twice;
4. rebuilds and copies a `MATRIX4` for every visible clump every frame;
5. uploads those matrices again for instanced drawing.

The current path removes that work: it groups and builds matrices once per ADT window,
uploads them to immutable instance VBOs, and releases the CPU arrays. There are no
per-instance visibility tests, matrix constructions, allocations, copies, or uploads
during a frame.

A second 2026-08-18 bounded diagnostic explained the reported 60 fps capture. SDL's swap
interval was already zero, so this was not a vsync cap. The old shared dynamic instance
VBO called `glBufferData` for every group every frame: 465,524 matrices × 64 bytes =
29,793,536 bytes copied per frame, directly matching `_platform_memmove` as the largest
sample. Persistent per-group VBOs remove that traffic. The same run inspected all 14
resident M2s: each had one bone and zero keyed translation/rotation/scale tracks, which
is why recalculating M2 bones produced no visible animation.

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

### Camera-following static mesh: the GPU-native grass path

The Angry Bots rain technique—an immutable VBO of slots, tiled around the camera and
animated entirely in a vertex shader—eliminates every per-frame CPU cost and is the
right primary pattern for grass. Adapted for WoW:

1. Allocate one blade/M2 geometry buffer and submit it with `glDraw*Instanced`; do not
   copy identical geometry into every slot.
2. In the vertex shader, map `gl_InstanceID` to an odd camera-centered grid. Hash the
   resulting integer world-cell coordinate for stable jitter, yaw, scale, density, and
   wind phase:
   ```glsl
   ivec2 offset    = instance_grid_offset(gl_InstanceID);
   vec2 worldCell  = floor(uCameraXZ / SLOT_SPACING) + vec2(offset);
   vec2 worldXZ    = (worldCell + stable_jitter(worldCell)) * SLOT_SPACING;
   vec3 worldPos   = vec3(worldXZ, 0.0);
   worldPos.y      = HeightAtlas_SampleDiamond(uHeightAtlas, worldPos.xz);
   ```
   Moving by one cell remaps overlapping instances to the same integer world cells, so
   their generated positions do not slide or pop. The grid half-extent must exceed draw
   distance plus one spacing/jitter margin.
3. The height atlas (`R32F`, 17×9 tiles, exact MCVT samples) provides Z without any CPU
   lookup per blade. The vertex shader calls the same diamond interpolation used for
   terrain vertices.
4. Wind and per-blade phase variation are entirely in the vertex shader.
5. Frustum culling is not needed: the mesh is always centered on the camera by
   construction. A single AABB covering the full tile extent is sufficient for GPU
   occlusion; no per-blade or per-clump test runs on the CPU.

**How WoW placement data fits in.** The MCNK cell-placement information (predicted layer
map, suppression mask, density) must still be read from the authoritative DBC/ADT source.
Encode it into a grass-control texture: one channel for suppression, one for density
weight, one for layer/effect index. The vertex shader reads this texture at each blade's
world XZ and moves suppressed blades to a degenerate position (all three verts at the
same point) so the rasterizer skips them entirely at zero fragment cost. This is cheaper
than branching on early-out because it avoids shader divergence.

**Multiple M2 models.** This is a hard correctness requirement, not optional polish.
The disabled prototype lost textures because the control atlas carried no model/material
identity. Bake each weighted M2 variant as a separate model index in the
slot data. The vertex shader selects the correct base vertex range or the fragment shader
selects the correct texture from a small array. Keep the number of distinct M2 variants
per patch small (GroundEffectTexture.dbc typically has 2–4 weighted models); each variant
can be its own sub-mesh within the VBO to keep materials simple.

**Bone animation.** The locally inspected Classic Elwynn grass M2s each have one bone.
Bake the rest-pose transform from that bone at load time and apply it statically; the
vertex shader then adds wind offset on top. If a genuine multi-bone animated M2 appears,
handle it as an instanced draw (Phase 3 path) rather than the static grass mesh.

### Limitations of the camera-following static mesh

| Constraint | Implication |
|---|---|
| Grid remap | Hash placement from integer world-cell coordinates. Hashing immutable slot IDs would slide the whole field whenever the camera crosses a cell. |
| Grid density | Every instance is evaluated every frame; suppressed blades cost vertex work but no fragment work. Budget grid spacing against dense and sparse areas. |
| Exact WoW cell boundaries | Camera-snapping produces a regular grid, not MCNK-aligned cells. Suppression via texture lookup reproduces WoW placement without exact cell boundaries in the mesh topology. |
| Height atlas coverage | The blade must land inside the currently loaded height atlas. Blades near the atlas edge that fall outside should be suppressed in the vertex shader; add an atlas-bounds check to the tile origin logic. |
| Bone animation (multi-bone M2s) | Pure static mesh cannot carry per-bone animation. Fall back to instanced draws (Phase 3 path) for any M2 with more than one effective bone. |
| View-distance ring | Grid half-extent must cover draw radius plus camera-within-cell and jitter margins. The current 181x181 grid at 2.5 m spacing covers the 220 m radius. |

## Alpha, Sorting, and Fade

Grass does not inherently require sorting. The answer depends on each M2 batch's
authoritative blend mode:

| Material | Blend | Depth write | Sorting |
|---|---:|---:|---|
| opaque | off | on | none |
| alpha-key/cutout | off; alpha-to-coverage | on | none |
| true alpha blend | `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | off | back-to-front or OIT |
| additive | additive | normally off | usually order-independent |

For the verified Classic Elwynn grass/flower assets, use alpha-key cutout and depth
writes. This lets the depth buffer resolve overlap, so per-blade sorting is unnecessary.
Khronos' transparency guidance explicitly distinguishes cutout foliage from true
translucency: discard transparent texels and retain depth testing/writes. NVIDIA's
vegetation chapters likewise identify sorting alpha-blended foliage as expensive and
describe alpha-to-coverage as an edge-quality alternative under MSAA.

**Alpha-to-coverage (ATOC).** With MSAA enabled, `GL_SAMPLE_ALPHA_TO_COVERAGE` converts
each fragment's alpha value into a coverage bitmask applied at subpixel resolution. At
4× MSAA, alpha=0.75 fills three of four samples; alpha=0.25 fills one of four. This gives
sub-pixel coverage with depth writes enabled and no per-blade sorting, eliminating the
harsh texel-edge aliasing of an alpha test. This is not general order-independent
transparency: differently coloured overlapping coverage can remain order-sensitive.
GPU Gems 3 chapter 4 (SpeedTree) documents this for
foliage edges and wind animation; Ben Golus's "Anti-Aliased Alpha Test: The Esoteric Alpha
To Coverage" (Medium, ~2018) is the most complete developer-level explanation and covers
alpha scaling, mip bias, and depth-write safety. The DX9-era ATI whitepaper (ATI_ATOC.pdf)
and NVIDIA's ATOC vendor extension (`MAKEFOURCC('A','T','O','C',0)`) are the original API
references; the ATI document is offline but archived. In Unity ShaderLab the same feature
is spelled `AlphaToMask On`. This is almost certainly the "alpha-to-fill" technique the
user recalled: the "fills N of 4 samples" framing for MSAA is exactly how every
practitioner describes it informally.

This same technique applies to trees. Any alpha-cutout M2 asset rendered under MSAA
benefits from enabling ATOC: it removes hard pixel-grid edges on leaves and branches
without sorting and without disabling depth writes.

Implement material handling per M2 batch; remove the blanket ground-effect blend
override in `M2_RenderInstanced`. For distance fade:

- with MSAA, enable `GL_SAMPLE_ALPHA_TO_COVERAGE` and keep depth writes; modulate
  alpha in the fragment shader for fade, not blending;
- without MSAA, the discard-free renderer uses a logged alpha-blended fallback with
  depth writes disabled; expect weaker overlap quality than the MSAA path;
- do not convert every alpha-key asset to smooth alpha blending merely to fade it;
- if a real `BLEND_MODE_BLEND` ground-effect asset is found, sort **patches/batches**
  back-to-front first. Only add weighted blended OIT if visible artifacts and profiling
  justify it.

Keep grass shaders small, draw terrain/opaque geometry first, render roughly
front-to-back by patch, use
good alpha mipmaps, and measure overdraw. Do not add a depth prepass automatically; for
tiny grass triangles it may cost more vertex/alpha work than it saves.

## Shader Work

### Grass vertex shader (camera-following static mesh path)

```glsl
// Uniforms: uCameraXZ (vec2), uSlotSpacing (float), uHeightAtlas (sampler2D),
//           uGrassControl (sampler2D), uTime (float), uGrassDist (float)
// Per-vertex: aModelPos (vec3), aUV (vec2)
// Per-slot: gl_InstanceID maps to a centered integer grid coordinate.

ivec2 offset  = instance_grid_offset(gl_InstanceID);
vec2 cell     = floor(uCameraXZ / uSlotSpacing) + vec2(offset);
vec2 worldXZ  = (cell + stable_jitter(cell)) * uSlotSpacing;
float seed    = stable_hash(cell);

// Suppress blades outside atlas bounds or in no-effect cells
vec4  ctrl      = texture(uGrassControl, worldXZ * uControlScale + 0.5);
float suppress  = step(0.5, ctrl.r);                  // 1 = suppressed
float distFade  = 1.0 - smoothstep(uGrassDist * 0.8, uGrassDist, length(worldXZ - uCameraXZ));

// Degenerate suppressed blades at a single point (no rasterization cost)
float keep      = (1.0 - suppress) * step(0.001, distFade);

float worldY    = HeightAtlas_SampleDiamond(uHeightAtlas, worldXZ);

// Rotate model vertex by yaw, then scale, then place in world
float cy = cos(seed * 6.28318), sy = sin(seed * 6.28318);
vec3  rot = vec3(cy * aModelPos.x - sy * aModelPos.z,
                 aModelPos.y,
                 sy * aModelPos.x + cy * aModelPos.z) * stable_scale(cell);

// Wind: sine-wave offset scaled by blade height and a per-slot phase
float windPhase  = uTime * 1.3 + seed * 6.28318;
float windBend   = sin(windPhase) * 0.07 * rot.y;     // only upper verts bend
rot.x           += windBend;

vec3 worldPos = vec3(worldXZ.x + rot.x, worldY + rot.y, worldXZ.y + rot.z) * keep;
gl_Position   = uViewProj * vec4(worldPos, 1.0);

vUV       = aUV;
vAlpha    = distFade * keep;
```

The fragment shader remaps M2 texture alpha around the material cutoff without
`discard`. With MSAA, enable `GL_SAMPLE_ALPHA_TO_COVERAGE` and use `vAlpha` to modulate
coverage for distance fade rather than enabling blending. Without MSAA, the shared
renderer logs and uses its alpha-blended/depth-write-off fallback.

`HeightAtlas_SampleDiamond` must be a shared GLSL snippet (include or generated string)
used by terrain, grass, and splat shaders. Duplicate coordinate math will drift.

### Terrain vertex shader

Needs: exact height/normal fetch, chunk/world transform, terrain UV, alpha-atlas
coordinate, and cell-hole handling. Same `HeightAtlas_SampleDiamond` snippet as grass.

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

### Phase 3: eliminate grass entities — camera-following static mesh

- Introduce one shared blade/M2 geometry buffer. Generate the camera-centered world-cell
  grid from `gl_InstanceID`; do not duplicate the geometry or upload instance data.
- Build a grass-control texture covering the loaded ADT window: one channel for
  suppression (no-effect mask), one for density weight, one for layer/effect index.
  Update this texture when an MCNK loads or unloads; do not update it every frame.
- Implement stable integer-world-cell placement, heightmap Z, suppression
  degenerate-collapse, wind, and ATOC distance fade.
- A single draw call covers the entire grass tile with no per-frame CPU culling.
  Remove `wow_world.ground_effects`, `wow_grass_scratch`, `RF_GROUND_EFFECT` matrix
  logic, and `Wow_AddGroundEffectInstance` only after visual and counter parity.
- If multiple M2 model types are needed in one tile, partition the VBO into
  per-model sub-meshes, each drawn with its own material in one `glDrawArrays` call
  per visible model type. Keep the call count small; 2–4 models per effect type is typical.
- Fall back to per-patch instanced draw (original Phase 3 design) for any M2 with
  more than one effective animation bone.

### Phase 4: fix materials and fade

- Honor opaque, alpha-key, blended, and additive M2 batch modes independently.
- Verify alpha-key assets use remapped alpha coverage + depth writes and require no
  per-blade sorting.
- Keep the shared discard-free alpha-key contract: ATOC under MSAA and a logged blended
  fallback otherwise.
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
- A bounded OpenWoW world run completes and `make test` is green. If a change also
  modifies shared or another game's renderer path, build and run those affected targets.

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
make run-wow ARGS="+set wow_playerinfo '\race\Human\sex\Male\class\1\appearance\0' +map playercreate +com_frame_limit 300"
make test
```

Use the `xctrace` / `xctraceprof` workflow in
[`docs/diagnostic-tools.md`](../../diagnostic-tools.md) and focus on
`Wow_DrawGrass`, `Wow_DrawTerrainAndWmos`, and the static M2 submission function. Add GPU
timer queries around terrain and grass separately when diagnosing a CPU/GPU handoff.

## Research References

### Grass geometry and CPU elimination

- [GPU Gems: Rendering Countless Blades of Waving Grass](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-7-rendering-countless-blades-waving-grass)
  — Kurt Pelzer (Piranha Bytes), 2004. Three-quad star clusters, wind via vertex shader
  trig, alpha-blended sorting. Starting point for clustered grass geometry.
- [GPU Gems 2: Toward Photorealism in Virtual Botany](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-1-toward-photorealism-virtual-botany)
  — David Whatley (Simutronics), 2005. Deterministic procedural placement, screen-door
  alpha test, and why sorted alpha foliage is costly.
- **"Procedural Grass in Ghost of Tsushima"** — François Malenfant (Sucker Punch), GDC
  2020, session 1026991 on the GDC Vault. The canonical reference for camera-snapping
  infinite tiling grass: a static per-tile VBO, vertex-shader tileOrigin snap, and
  heightmap displacement — exactly the pattern described in the camera-following section
  above. Not directly fetchable in all regions; search GDC Vault for the title.
- **Unity AngryBots demo (2013)** — rain particles as a static immutable mesh animated
  entirely in a vertex shader. The "generate the mesh once, loop it around the camera"
  pattern originates here for game-engine practitioners. No formal publication; widely
  referenced in Unity forum threads and the Unity sample project.

### Alpha-to-coverage

- [GPU Gems 3: Next-Generation SpeedTree Rendering](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-4-next-generation-speedtree-rendering)
  — Kharlamov, Cantlay, Stepanenko (NVIDIA), 2008. First major book treatment of ATOC
  for foliage edges; explains the subpixel-coverage mechanism and wind scintillation fix.
- **"Anti-Aliased Alpha Test: The Esoteric Alpha To Coverage"** — Ben Golus (Unity
  Technologies), Medium, ~2018. URL:
  `bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f`.
  The best developer-level explanation of how ATOC fills N-of-4 MSAA samples, alpha
  scaling, mip bias, and why depth writes stay enabled. The informal "alpha to fill"
  description recalled by the user refers to this "fills N of 4 coverage samples"
  mechanism; it is not a published term but a universal practitioner paraphrase.
- **ATI ATOC whitepaper** (`ATI_ATOC.pdf`, ~2005–2008). The original AMD vendor
  extension documentation enabling A2C in DX9 via `MAKEFOURCC('A','2','M','1')`. The
  AMD developer page is offline; the document exists on web.archive.org. NVIDIA's
  equivalent used `MAKEFOURCC('A','T','O','C',0)`. In Unity ShaderLab the same
  feature is spelled `AlphaToMask On`; in OpenGL it is `GL_SAMPLE_ALPHA_TO_COVERAGE`.

### Terrain height and sorting

- [Khronos: Transparency Sorting](https://wikis.khronos.org/opengl/Transparency_Sorting)
  explains why cutouts use discard + depth writes without translucency sorting.
- [Khronos: Early Fragment Test](https://wikis.khronos.org/opengl/Early_Depth_Test)
  records the performance caveat that fragment `discard` can inhibit early depth on
  some hardware.
- [GPU Gems 2: Terrain Rendering Using GPU-Based Geometry Clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
  is the reference for a later large-distance terrain LOD, not a prerequisite here.

### WoW data

- [wowdev/noggit3 ground-effects investigation](https://github.com/wowdev/noggit3/issues/87)
  documents the MCNK 8x8 two-bit layer map and 64-bit suppression map. Treat this as
  reverse-engineered evidence and validate orientation/version against local assets.

## Known Risks

- MCNK ground-effect map meaning/orientation may vary by client version; tests and local
  visual validation are mandatory.
- Some ground-effect M2s may use real blending or more complex animation/materials than
  the verified Elwynn set. Dispatch from material data rather than hardcoding alpha-key.
- **World-cell stability.** Placement seeds must come from the integer world cell, not
  `gl_InstanceID`; otherwise the entire field slides when the camera crosses a cell.
- **Degenerate-collapse overhead.** A suppressed blade is collapsed to a degenerate
  point in the vertex shader, not removed from the VBO. In very sparse areas the vertex
  shader still runs for every suppressed slot. If vertex throughput becomes the limit,
  consider a two-level VBO: a coarse suppression-aware CPU pass that compacts into a
  smaller active-slot VBO once per ADT load event (not per frame).
- **Grass-control texture boundaries.** Blades near the atlas edge may fall outside the
  loaded height or control texture. Add an explicit out-of-bounds guard in the vertex
  shader; do not rely on GL clamp-to-edge producing a valid height or suppression value.
- The CPU height copy remains required by collision/gameplay and by validation. The GPU
  atlas is a rendering representation, not a new source of truth.
- **Multi-bone M2 fallback.** The static mesh path cannot animate multi-bone M2s.
  Keep the per-patch instanced draw path compiling and tested so it remains a viable
  fallback when such assets are encountered.
