# Warcraft III Terrain Deformation

## Scope

This document describes the generic terrain deformation path used by the Warcraft III renderer. It does not claim that OpenRealm's deformation equations reproduce Blizzard's native equations. Earthquake now sends provisional random deformation pulses to make the presentation testable; their parameters are guesses and are not verified retail behavior.

The native names and signatures below are verified in the repository's retail-shaped `games/warcraft-3/game/common.txt`, which contains the World Editor API declarations:

- `TerrainDeformCrater(x, y, radius, depth, duration, permanent)`
- `TerrainDeformRipple(x, y, radius, depth, duration, count, spaceWaves, timeWaves, radiusStartPct, limitNeg)`
- `TerrainDeformWave(x, y, dirX, dirY, distance, speed, radius, depth, trailTime, count)`
- `TerrainDeformRandom(x, y, radius, minDelta, maxDelta, duration, updateInterval)`
- `TerrainDeformStop(deformation, duration)`
- `TerrainDeformStopAll()`

Those declarations establish the interface, not the exact stock height functions, waveform, persistence, or overlap behavior.

## Runtime Ownership

A JASS call is simulation-owned. The native sends a typed, one-shot `svc_temp_entity` command containing an ID and numeric descriptor. The renderer owns a fixed 32-entry transient registry and one map-sized height-offset array. The command does not become entity snapshot data or renderer state in a save. `terraindeformation` handles store the transient ID in memory and serialize as null, so neither the effect nor its renderer ID survives save/load.

The descriptor is `terrainDeform_t` in `common/shared.h`. `type` selects one of four named variants in an anonymous union (`crater`, `ripple`, `wave`, `random`), each starting with `origin`; the JASS natives and Earthquake fill the named fields and the renderer reads them. The same storage is also visible as `data[TERRAIN_DEFORM_FLOATS]`, which is the only view the game's wire writer and the client use: eight floats in order, so the client carries every variant without knowing any of them. A static assert keeps the widest variant (`wave`) exactly that size. Add a parameter by naming it in its variant, never by agreeing on a `data[]` index.

The WC3 renderer samples its map vertices from the sum of active descriptors, updates only the union of previously and currently affected grid bounds at a 33 ms minimum interval, and rebuilds dirty terrain segments before drawing. Ground, terrain-conforming splats, and exact terrain height queries consume the deformed heights. The camera adds a bilinearly sampled deformation offset to its separately blurred base height; that offset does not receive the camera blur. Renderer arrays and active descriptors are released/reset on map changes.

Each deformation type currently uses a documented OpenRealm approximation:

- Crater: radial smooth depression using the absolute authored depth, with a time envelope unless permanent.
- Ripple: radial sinusoidal rings bounded by radius, with an optional nonnegative clamp.
- Wave: directional sinusoidal strip bounded by distance and width.
- Random: stable tile-seeded samples refreshed at the authored interval.

`count` is transported and retained but does not currently select repeated waves. The exact relationship of `duration`, `trailTime`, `count`, `spaceWaves`, `timeWaves`, and the native's stop duration to retail behavior remains unknown. Do not tune these formulas as retail-compatible without executable or controlled retail evidence.

## Earthquake Test Pulse

On each active Earthquake damage tick, after the authored `Oeq1` delay, the game sends one `TerrainDeformRandom`-shaped renderer event at the fixed cast point. Its radius comes from the ability's authored `Area`; the current stock row supplies `250`. Provisional visual-check values are `minDelta=-48`, `maxDelta=48`, `duration=1000 ms`, and `updateInterval=200 ms`. Since the existing channel tick is one second, pulses recur once per tick. Each one expires locally, so channel interruption adds no saved or persistent deformation state and leaves at most the current pulse's remaining lifetime.

The larger amplitude is intended to make the random height field easier to see during an in-game check. Neither the use of `TerrainDeformRandom` nor the amplitude, interval, duration, and relation to `Area` have been verified against retail Earthquake. The integration deliberately does not read or assign semantics to `Oeq4`.

## Height Field And Limits

Deformations are added together as vertical offsets over the static W3E height field. No permanent modification is written back to the map. This is renderer-owned presentation; server-side pathing and `CM_GetHeightAtPoint` remain unchanged. Maps whose vertex grids do not form complete existing renderer segments, or whose deformation buffers cannot be allocated, report one bounded warning and do not accept deformations.

Ground stays in one whole-map vertex buffer per ground texture (one draw call per layer, `R_BuildGroundLayerGlobal`). The buffer is baked segment by segment and records each segment's first vertex, so a dirty segment re-bakes only its own slice with `R_UpdateGroundSegment` → `R_UpdateVertexArrayObject` (`glBufferSubData`). This works because a tile's vertex count depends on ground type and cliff flags, never on height; a count mismatch is logged and that slice is left untouched. Do not move ground back into per-segment layers: that multiplies ground draw calls by the visible segment count for every frame, deformation or not. The affected segments' water and cliff layers are still rebuilt as separate per-segment buffers. All dirty segments share one cliff bake, and the cliff pieces of their clean neighbours join it as weld-only context (built, welded, then discarded), so border normals match the load-time whole-map weld instead of leaving a lighting seam after the deformation ends. Any rebuild also invalidates the Blight layer, which is baked on terrain heights and would otherwise float above or sink under the deformed ground. Cliff vertices that join to the ground sample the deformed height field; cliff art and tile masks are unchanged. Water surfaces retain their authored heights. The system does not alter gameplay collision or pathing. Headless regression coverage checks deformation height sampling and expiry, that an absent layer cannot clear already assembled segment layers, that a ground update overwrites exactly one segment slice (`renderer_terrain.ground_batch_rebakes_one_segment_slice_in_place`), and that a rebuild forces a Blight rebake (`renderer_terrain.deformation_rebuild_rebakes_blight_layer`). Segment rebuilds can be expensive when a large deformation covers much of the map; the fixed pool, bounded update interval, affected-bounds evaluation, and dirty-segment rebuilds limit unnecessary work, but profiling on the RG40XX-H-class target remains outstanding.

## Evidence Status

- **Verified from declarations:** the six native names and argument shapes listed above.
- **Verified from code:** map renderer terrain is a separate client map copy; the height offset and transient descriptor state stay renderer-owned; map-change cleanup clears both.
- **Implemented but not runtime-verified:** OpenRealm's four generic profile approximations, overlapping additive composition, stop fade, and the provisional AOeq pulse wiring described above.
- **Still unknown:** Blizzard's exact native math, whether persistent crater state survives save/load, retail overlap/combination semantics, gameplay height/pathing consumers, and whether Earthquake uses one of these natives or a separate hard-coded engine path.
- **Explicitly not implemented:** Retail-parity AOeq deformation, inferred `Oeq4` behavior, modification of server collision/pathing, and permanent source-map mutation.

The renderer regression uses a synthetic terrain grid and does not require proprietary retail assets. Runtime visual behavior has not been manually verified in-game.
