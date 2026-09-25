# SC2 Terrain And World Rendering

## Directional Light Convention

`CLight/DirectionalLight/Direction` is the world-space direction in which the light rays travel. Mar Sara's live catalog value is
`(0.724693,-0.124265,-0.677775)`. `sc2_shadow_matrix` consumes that vector directly because cast shadows travel with the rays;
Lambert shading consumes its negation because `dot(normal, light)` expects a surface-to-light vector. Do not make both consumers use
the same sign.

A bounded TerranTest run confirmed that the archive value reaches `SC2_MapCurrent()` unchanged and that diffuse receives
`(-0.724693,0.124265,0.677775)`. If the light appears horizontally reversed relative to SC2, investigate the view basis first.
Map/Galaxy pitch is degrees down from horizontal (the old `Matrix4_getSc2CameraMatrix` lookAt). Generic orbit identity looks
down `-Z`, so `CL_GameDefaultCamera` and `SC2_ViewAngles` store `pitch - 90` on `playerState.viewangles.x`. Gameplay 56 becomes
`-34` (same tilt as WC3 Euler 326). Do not send raw map pitch 56/34.9 as Euler X — that aims nearly straight down.
`SC2_MapDefaultCamera` keeps hardcoded gameplay defaults (pitch 56, distance 34.07) unless a `StartGame*` camera exists.
TRaynor01's first camera object is a close cinematic (pitch 8.6, dist 4.95); using it as the default aims along the ground.
Native yaw retains the authored StartGame value (Mar Sara `179.9584`), but the view-matrix adapter must publish
`yaw - 180`. Copying native yaw directly put the TRaynor01 camera on the opposite side of the bridge from the retail
reference. `SC2_CameraFromEuler` reverses this conversion for manual input. See [coordinates and camera angles](../../../AXIS.md).
Do not rotate map coordinates or flip the light's X component to compensate for a camera convention.

Diagnostic workflow:

```sh
make opensc2
build/bin/opensc2 -data data/StarCraft2 +vid_hidden 1 +map Maps/TerranTest.SC2Components +com_frame_limit 10
```

Add a temporary one-shot log after `SC2_MapLoad` in `R_SC2RegisterMap`; do not log from a draw path. Compare the authored ray, its
Lambert negation, and the camera right vector in screen space, then remove the log.

## Camera Drag Plane

SC2 computes camera target Z as terrain height at the target XY plus the camera's authored `HeightOffset`. This matches Warsmash's
WC3 lifecycle: `MeleeUI` samples terrain under `cameraManager.target`, then `GameCameraManager.updateTargetZ` adds preset height and
target offset before deriving the eye from pitch and distance. Treating `HeightOffset` as absolute world Z puts the eye underground.

During renderer map registration, the client builds a separate blurred height source from the exact heightmap. It averages every grid
sample within an eight-world-unit radius (`ceil(radius / cell_size)` cells), then samples that cache bilinearly while rendering the
camera. On the unit-spaced SC2 grid this is a dense 17x17 box filter. The previous sparse 4x4 kernel sampled offsets -8, -3, +3, +8;
cliffs appeared repeatedly as those taps crossed them instead of contributing continuously across the footprint. The server Air-height
query retains its separate broad 4x4 sampling contract. This camera spatial filter makes a narrow canyon contribute little to camera
height while broad terrain tiers still affect it. Do not replace it with a temporal
filter: retaining the previous frame's height makes the camera rubber-band toward every local depression. Ground units, commands, roads,
collision, and the server camera state continue to use exact `SC2_MapHeightAtPoint` queries; the client applies the blurred terrain base
to the current camera XY while interpolating only the authored height offsets between snapshots.

A bounded TRaynor01 comparison along X=20..45, Y=28.75 at quarter-cell intervals measured maximum adjacent camera-height changes
of 0.254 before and 0.112 world units after dense filtering; maximum changes in that increment fell from 0.423 to 0.063. These are
local-map measurements, not tuning constants. `renderer_view.camera_height_dense_blur` checks a synthetic cliff's continuous ramp,
bilinear interpolation, non-unit grid spacing, clamped edges, and cache release (`make test-renderer-view`). Shared camera offset and
prediction coverage is documented in [Client camera samples](../../architecture/client.md#camera-samples).

SC2 drag-panning intersects the cursor ray with the horizontal plane at `viewDef.target.z`, the rendered terrain-relative camera target height.
It must not use `R_SC2TraceLocation`: that function intersects actual heightmap triangles for unit commands, so reusing it for camera
drag makes the pan anchor jump when the cursor crosses cliffs or other terrain tiers. `TraceCameraPlane` owns the stable screen-ray
intersection during the drag; camera rendering resamples terrain at the moved target, so the eye rises over higher ground. Smart
commands continue to use terrain `TraceLocation`.

## Hard-Tile Roads

Roads use `CTile` records in `GameData/TileData.xml`; for example,
`MarSaraTile` resolves to `Assets/HardTiles/MarSaraRoad/MarSaraRoad.m3`. Maps place hard tiles through the binary `t3HardTile` file
(`HRDT`, observed version 102), whose repeated records contain a world surface center, surface normal, two endpoint offsets,
half-width, depth, flags, and a
null-terminated tile ID. `SC2_MapLoad` validates the whole pointer-walk before allocating `hard_tiles`, resolves each ID through the
layered `CTile` catalogs, and logs unresolved IDs. `r_sc2_build_hard_tiles` joins each flagged control-point chain as a cubic Bezier
ribbon. Adjacent triangles and spans share sampled cross-sections, while longitudinal UV advances by sampled centerline distance so
texture markings follow bends instead of stretching independently between controls. The ribbon is only the XY footprint and
UV source: each triangle is clipped against both triangles of every intersecting
terrain cell, using the same `00--11` diagonal, corrected ground heights, normals, and cliff/ramp cell omissions as
`r_sc2_build_ground_layer`. Barycentric interpolation puts every clipped vertex on that ground plane; clipping just to cell
rectangles or sampling only the ribbon edges still spans folds inside non-planar cells. New edge vertices interpolate the
ribbon UVs, retaining markings through bends. Cliff cells require their actual baked M3 triangles too: skipping those cells
left triangular gaps at both TRaynor01 bridge approaches. `r_sc2_build_cliff_layer` now retains its CPU bake until roads
finish projecting and then frees it. Clipping handles either mesh winding and restricts cliff projection to the spline's
height envelope using HRDT depth (the larger endpoint depth per span; TRaynor01 uses `1.0`). Intersections crossing the
envelope are clipped, not discarded as whole triangles; canyon floors outside it receive no road. The baker counts
intersections before allocating and splits GPU buffers at
65,535 vertices to respect the M3 material path's 16-bit index contract.

`r_sc2_draw_road_layers` uses `glPolygonOffset(-2,-2)` in the color pass, ahead of the cliff overlay at `(-1,-1)`, and
restores offset state afterward. There is no geometric Z lift. The road is a coplanar material overlay and must not draw
in `RENDER_PHASE_LIGHTS`: ground/cliff geometry already supplies that caster surface. Submitting the negative decal bias
to the shadow map made roads shadow themselves, nearly blacking out the asphalt and dashed lines; resetting it afterward
also removed `R_RenderShadowMap`'s positive `(2,4)` bias before unit casters. Keep the entire road pass, including GL state
changes, out of shadow rendering. `sc2_map.road_overlay_preserves_shadow_caster_pass` captures the production draw calls
and checks both shadow-state preservation and material submission/state cleanup in the color pass.
The former `max(authored_z, terrain_z + 0.05)` path (continuous ribbons introduced in `102eb94c`) placed roads above
SC2 selection splats, which have a `0.02` world-unit lift, and left wide triangles floating across terrain folds. Raising
selection circles or merely reducing the road constant would hide the geometry mismatch. The authored spline Z still
participates in curve sampling but does not become the rendered surface height.

`make test-sc2` covers the former road/ring height inversion, the two planes of a non-planar cell, interpolated UVs,
area preservation, empty/degenerate intersections, reversed cliff winding, and partial cliff-depth intersections.
Use a fully rebuilt `make opensc2` before visual QA so the
executable and renderer agree on `renderEntity_t`; a stale executable can crash in `R_GetEntityBounds` after a shared
header change. `+screenshot 1` captures the opening cinematic camera, which may contain roads without the selected unit;
use a focused temporary copy for the bridge and ring together. Copy TRaynor01 root map files to
`build/share/starcraft-2/Maps/RoadBridge.SC2Components`, use an empty `InitGlobals`/`InitTriggers`/`InitMap` in its `MapScript.galaxy`,
set the first StartGame camera's `CameraTarget` to `(29.5,29)`, and add a player-1 Marine at `(34,24,0)` for auto-selection.
The road, heightmap, cliffs and original bridge placements remain authored map data. Capture with:

```sh
build/bin/opensc2 -data data/StarCraft2 +vid_hidden 1 +com_maxfps 60 \
  +map Maps/RoadBridge.SC2Components +screenshot 1 +com_frame_limit 80
```

The temporary scene is visual QA only; do not commit retail map files.

`MarSaraRoad_Diffuse.dds` is a 1024x1024 atlas. Its seamless road body runs horizontally through the lower half (`V=0.5..1.0`),
so generated ribbons map arc length to U and road width to that V band. Mapping width to U and length to the full V range selects the
upper end-cap tiles and dirt between atlas regions, producing disconnected slabs. The body band has a 2:1 length/width aspect ratio;
use the authored half-width when converting world distance to repeating U.

Do not replace these placements with a generic road spline from painted terrain. If a map/version stores a centerline that must be tessellated,
the relevant Quake III reference is `data/Quake-III-Arena-master/code/splines/splines.cpp`: it samples a uniform cubic B-spline with
four basis weights. `code/renderer/tr_curve.c` instead adaptively tessellates 2D quadratic Bezier patch grids and is not the default
road primitive.

A bounded `TRaynor01` run loads 48 `MarSaraTile` placements. Its live file also established that the block tile ID is variable-length;
the old public fixed-12-byte description shifts the next block count by one byte.

## Cliff Normal Welding

SC2 cliff pieces are expanded into non-indexed `VERTEX` triangles in `r_sc2map.c`. Seam smoothing must use quantized XYZ position and placement identity. An XY-only key is invalid because stacked geometry shares grid columns; a bounded `TRaynor01` diagnostic found 9,749 mixed-height XY buckets and 9,058 opposing-normal comparisons among 309,669 vertices.

The dynamic bake and welder now live in `renderer/r_cliff.h`, shared with WC3. Repeated expanded triangle corners count each placement/authored normal only once to avoid weighting one side of a seam more heavily.

The shared header helpers must be `static inline`: the standalone SC2 tests include this header for
`R_CliffWeldCompatible` without linking the renderer or defining its `ri` import table. In PR #480, plain
`static` allocation/welding helpers were emitted by GCC at `-O0`, causing undefined `ri` references in
`make test-sc2` on Linux. Apple Clang discarded the unused helpers, so macOS tests alone missed the failure.
Keep the standalone link independent of renderer globals; do not add dummy imports or weaken the linker.

Only vertices from different placements with the same quantized height and normals in the same hemisphere may contribute to one another. Same-placement vertices preserve authored hard edges. Seed each average with the source normal, compute outputs separately from inputs, and leave zero normals unchanged so `Vector3_normalize` never receives a zero sum.

Validation:

```sh
make test-sc2
build/bin/opensc2 -data data/StarCraft2 +map Maps/Campaign/TRaynor01.SC2Map +screenshot 5 +com_frame_limit 10
```

## Ramp Cliff Transitions

`t3Terrain.xml/rampList` supplies `dir`, `lo`, `hi`, `cid`, four side variants, and oriented `leftLo`, `leftHi`, `rightLo`, `rightHi`,
`base`, `mid` boxes. A box stores up/right axes, center and half extents in height-grid units. Scalar attributes use the XML schema;
the nested box string is one explicit parser production. Variant `4294967295` disables a side. The map owns/frees the parsed array.
Previously `cc.f & 2` skipped ordinary cliff models without loading the ramp sides (introduced in `24354a8c`).

Borrowing WC3's transition-footprint approach, `r_sc2_build_ramp_cliffs` selects native transition M3s for those boxes. Ordinary
corners use `A + tier`; slope corners use `P + tier` from the mid-box slope. Packed CLIF levels use six fractional bits. Model
configurations are cyclic but not all use lexicographically minimal names: the local TRaynor01 diagonal side resolves `BRQQ` to
`QBRQ` with the corresponding rotation. Preserve M3 vertices/UVs and the authored variant through the existing cliff texture batches.

TRaynor01 has a straight ramp centered at `(102,34)` with `BQRC/BCRQ` sides, and a diagonal ramp centered at `(50,90)` with
`BQQR/QBRQ` sides. The diagonal M3 footprints are **L-shaped**, missing a complete 2×2 corner block. Classify ground using each
2×2 block center strictly inside the mid box; the remaining side blocks belong to the mesh. Clipping individual triangles to the
oriented mid box incorrectly assumes a triangular model footprint and creates overlaps/holes. Mesh edges bordering emitted ground
sample its actual triangle height, not bilinear height, which differs inside non-planar cells. The low-tier M3 base averages only
integer-tier HMAP base samples; fractional CLIF samples inside the slope must not bias that base upward.

`make test-sc2` covers XML/MPQ lifecycle, straight and diagonal model configurations, block coverage, and triangle height joins.
Bounded captures can use `+com_fast_forward 1 +screenshot 10 +com_frame_limit 60`; without fast-forward a hidden scene can finish
its frame budget before the screenshot receives a world snapshot. Local verification used TRaynor01's authored terrain and M3s
in a temporary terrain-only map with a StartGame camera at `(50,90)`. Retail assets are not test fixtures or committed outputs.
See [WC3 cliff baking and ramps](../warcraft-3/architecture/map-renderer.md#shared-normal-welding-and-undead04).

## Cliff Texture Batches

Each inspected `TRaynor01` cliff M3 contains one division, one region, one batch, and one standard material. Natural and made cliff sets nevertheless use different authoritative textures:

- `CliffNatural0_*`: `Assets/Textures/marsara_cliff0_diffuse.dds`
- `CliffMade0_*`: `Assets/Textures/MarSara_Cliff1_Diffuse.dds`

The old map baker combined every cliff into one buffer and retained only the first diffuse texture, causing made cliffs to sample the natural atlas. The map now builds one `MAPLAYERTYPE_CLIFF` layer per distinct loaded texture and draws every layer in both the terrain-depth and material-overlay passes.

Do not restore a map-wide diffuse choice. Future support for multi-batch cliff M3s must follow each `m3Batch_t` through `regionIndex` and `materialReferenceIndex`, including composite/terrain materials and source vertex alpha.

## Streaming Texture Ownership

Persistent and streamed loads share the global renderer texture cache. Lifetime state belongs to the cache entry with `owns_texture`, including when a lookup arrives through an alias. A normal load permanently pins that owner; a streamed load may refresh its generation only while it remains unpinned. Reclamation deletes only stale streamed owners.

A bounded WoW run confirmed the former defect by logging persistent dungeon and terrain textures being reclassified as streamed. Focused coverage lives in `tests/test_renderer_model.c` and includes both load orders, current/stale generations, aliases, and GL deletion counts.
