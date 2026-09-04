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
Yaw still uses the authored StartGame value (Mar Sara `179.9584`);
do not add a yaw offset to hide an axis mismatch. Flipping the light's X component would make lighting and shadow projection disagree
and would hide a remaining camera-axis issue.

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

Camera rendering and `HeightMap="Air"` movers use the same stateless 5x5 box filter over a 16-world-unit heightmap footprint through
`SC2_MapCameraHeightAtPoint` and `SC2_MapAirHeightAtPoint`. This spatial filter makes a narrow canyon contribute little to height while
broad terrain tiers still affect it. Do not replace it with a temporal filter: retaining the previous frame's height makes the camera
or air unit rubber-band toward every local depression. Ground units, commands, roads, and collision continue to use exact
`SC2_MapHeightAtPoint` queries.

SC2 drag-panning intersects the cursor ray with the horizontal plane at `viewCamera_t.origin.z`, the current terrain-relative camera target height.
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
texture markings follow bends instead of stretching independently between controls. Road vertices retain the authored height when
it is above terrain, otherwise they use terrain height plus `SC2_HARD_TILE_Z_BIAS` (`0.05` world units) to avoid z-fighting without
visibly floating.

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

Only vertices from different placements with the same quantized height and normals in the same hemisphere may contribute to one another. Same-placement vertices preserve authored hard edges. Seed each average with the source normal, compute outputs separately from inputs, and leave zero normals unchanged so `Vector3_normalize` never receives a zero sum.

Validation:

```sh
make test-sc2
build/bin/opensc2 -data data/StarCraft2 +map Maps/Campaign/TRaynor01.SC2Map +screenshot 5 +com_frame_limit 10
```

## Cliff Texture Batches

Each inspected `TRaynor01` cliff M3 contains one division, one region, one batch, and one standard material. Natural and made cliff sets nevertheless use different authoritative textures:

- `CliffNatural0_*`: `Assets/Textures/marsara_cliff0_diffuse.dds`
- `CliffMade0_*`: `Assets/Textures/MarSara_Cliff1_Diffuse.dds`

The old map baker combined every cliff into one buffer and retained only the first diffuse texture, causing made cliffs to sample the natural atlas. The map now builds one `MAPLAYERTYPE_CLIFF` layer per distinct loaded texture and draws every layer in both the terrain-depth and material-overlay passes.

Do not restore a map-wide diffuse choice. Future support for multi-batch cliff M3s must follow each `m3Batch_t` through `regionIndex` and `materialReferenceIndex`, including composite/terrain materials and source vertex alpha.

## Streaming Texture Ownership

Persistent and streamed loads share the global renderer texture cache. Lifetime state belongs to the cache entry with `owns_texture`, including when a lookup arrives through an alias. A normal load permanently pins that owner; a streamed load may refresh its generation only while it remains unpinned. Reclamation deletes only stale streamed owners.

A bounded WoW run confirmed the former defect by logging persistent dungeon and terrain textures being reclassified as streamed. Focused coverage lives in `tests/test_renderer_model.c` and includes both load orders, current/stale generations, aliases, and GL deletion counts.
