# SC2 Terrain And World Rendering

## Directional Light Convention

`CLight/DirectionalLight/Direction` is the world-space direction in which the light rays travel. Mar Sara's live catalog value is
`(0.724693,-0.124265,-0.677775)`. `sc2_shadow_matrix` consumes that vector directly because cast shadows travel with the rays;
Lambert shading consumes its negation because `dot(normal, light)` expects a surface-to-light vector. Do not make both consumers use
the same sign.

A bounded TerranTest run confirmed that the archive value reaches `SC2_MapCurrent()` unchanged and that diffuse receives
`(-0.724693,0.124265,0.677775)`. If the light appears horizontally reversed relative to SC2, investigate the view basis first.
`SC2_MapDefaultCamera` currently passes SC2's authored yaw (Mar Sara uses `179.9584`) directly to the Quake-style
`Quaternion_fromEuler(..., ROTATE_ZYX)` path; no code proves that SC2 and the engine define yaw zero, handedness, and positive rotation
identically. Flipping the light's X component would make lighting and shadow projection disagree and would hide the camera-axis issue.

Diagnostic workflow:

```sh
make opensc2
build/bin/opensc2 -data data/StarCraft2 +vid_hidden 1 +map Maps/TerranTest.SC2Components +com_frame_limit 10
```

Add a temporary one-shot log after `SC2_MapLoad` in `R_SC2RegisterMap`; do not log from a draw path. Compare the authored ray, its
Lambert negation, and the camera right vector in screen space, then remove the log.

## Hard-Tile Roads

Roads use `CTile` records in `GameData/TileData.xml`; for example,
`MarSaraTile` resolves to `Assets/HardTiles/MarSaraRoad/MarSaraRoad.m3`. Maps place hard tiles through the binary `t3HardTile` file
(`HRDT`, observed version 102), whose repeated records contain a world surface center, surface normal, two endpoint offsets,
half-width, depth, flags, and a
null-terminated tile ID. `SC2_MapLoad` validates the whole pointer-walk before allocating `hard_tiles`, resolves each ID through the
layered `CTile` catalogs, and logs unresolved IDs. `r_sc2_build_hard_tiles` retains one model reference per placement and
`r_sc2_draw_hard_tiles` submits the authored transform through the normal M3 material and shadow path.

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
