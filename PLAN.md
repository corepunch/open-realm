# PR 170 Rendering Completion Plan

Branch: `feature/perf-opt`

## Goal

Finish PR #170 by fixing the two confirmed review defects, restoring the authoritative SC2 center InfoPanel, correcting cliff material rendering, and adding authored roads and tower wires to `TRaynor01`.

Roads and cables will share a compact C adaptation of Quake III's uniform cubic B-spline evaluator from `data/Quake-III-Arena-master/code/splines/splines.cpp`. SC2 map, catalog, layout, and M3 data remain authoritative for control points, endpoints, widths, materials, attachments, and visibility.

## Phase 0: Capture Authoritative Evidence

1. Record the PR head and preserve unrelated worktree changes.
2. Run `TRaynor01` with `+screenshot 5 +com_frame_limit 10` and retain the screenshot and bounded log showing the cliff quads, missing center panel, and absent roads and wires.
3. Run the stdout renderer with `+sv_debug_layout 1` and inspect the `LAYER_INFOPANEL` frame count, textures, models, dimensions, and stat values.
4. Use `mpqtool` and `sc2map` on `Liberty.SC2Campaign/Base.SC2Maps` and embedded `TRaynor01.SC2Map` to identify the road and tower-wire records. Extend `sc2map` when current diagnostics cannot expose the source records.
5. Use `m3tool --info --dump-all` on the exact cliff and cable assets to record division, region, batch, material, vertex-alpha, attachment, and ribbon data.
6. Do not implement roads or wires until their authoritative representation is confirmed. Do not infer tower links from proximity or names.

## Phase 1: Fix Texture Lifetime Review Blocker

Status: completed. `make test-renderer-model` passes 2,675 assertions.

1. Correct cache ownership in `renderer/r_texture.c`:
   - A texture created by normal `R_LoadTexture` is permanently pinned.
   - A streamed-first texture becomes pinned when a normal consumer touches it.
   - A streamed cache hit may update generation but cannot demote a pinned entry.
   - Aliases sharing one `LPTEXTURE` propagate persistent ownership to the canonical owning entry.
2. Add focused tests in `tests/test_renderer_model.c` for persistent then streamed, streamed then persistent, streamed-only stale reclamation, current-generation retention, aliases sharing one texture, and expected GL deletion calls.
3. Run `make test-renderer-model` immediately after the first edit.

## Phase 2: Fix Cliff Normal Welding Review Blocker

Status: completed. `make test-sc2` passes 648 assertions.

1. Replace XY-only welding in `games/starcraft-2/renderer/sc2/r_sc2map.c` with a key containing quantized XYZ and placement-group identity.
2. Smooth only coincident, compatible boundary vertices from different cliff placements. Preserve authored hard edges within one placement.
3. Reject incompatible or opposing normals and guard zero-length sums before calling `Vector3_normalize`.
4. Add tests in `games/starcraft-2/tests/test_sc2_map.c` for adjacent compatible seams, equal XY at different heights, same-placement hard edges, opposing normals, zero-sum input, and finite normalized output.
5. Run `make test-sc2` immediately after the edit.

## Phase 3: Remove The Blurry Cliff Quads

Status: in progress. Runtime inspection found one division, region, batch, and standard material per loaded `TRaynor01` cliff M3. The immediate defect was map-wide collapse of distinct natural and made-cliff textures; separate texture batches are now preserved. Vertex alpha and nonstandard material handling remain to be validated on broader fixtures.

The current baker flattens every M3 region into one buffer, discards batch and material identity, applies the terrain pass to all triangles, then overlays one diffuse cliff texture. SC2 cliff caps and auxiliary planes instead rely on M3 batch materials, terrain material references, masks, and vertex alpha.

1. Iterate M3 batches rather than every raw region while baking cliffs.
2. Preserve `regionIndex`, `materialReferenceIndex`, source vertex color and alpha, and resolved standard/composite/terrain material class in draw ranges.
3. Reuse material resolution from `games/starcraft-2/renderer/m3/r_m3_load.c`; do not add a parallel parser.
4. Split rendering into explicit ranges:
   - terrain-receiving caps use terrain textures and source vertex alpha;
   - opaque cliff faces use their referenced M3 material;
   - blended, masked, decal, or auxiliary ranges use their authored state;
   - the shadow pass draws depth-writing opaque ranges only.
5. Keep the existing terrain and cliff geometry ownership in `r_sc2map.c`; do not hide rectangular triangles by shape, size, or guessed material names.
6. Add a fixture with multiple cliff batches, materials, and nontrivial vertex alpha. Assert each range is submitted exactly once in the correct pass.
7. Capture a new `TRaynor01` screenshot and confirm the blurry red/sand planes are gone while cliff caps and faces remain complete.

## Phase 4: Restore The Center Console InfoPanel

Status: console ownership complete; selection binding pending. The native InfoPanel subtree, minimap, commands, portrait, and three chrome models now share one retained `LAYER_BACKGROUND` tree. Selection refresh is blocked on parsing native map-player mapping: the server currently advertises player 0, `SC2_InitClients` assigns player 1, and `TRaynor01` controllable units are owned by player 2. Do not infer ownership from the first mobile unit.

1. Inspect `InfoPanel`, `PortraitPanel`, `ConsoleModelInfopanel`, wireframe, labels, and stat frames through `stb_sc2layout.h` and stdout-renderer output. Classify each missing frame as absent, hidden, zero-sized, unresolved, or emitted without dynamic data.
2. Add one selected-unit HUD refresh path in `games/starcraft-2/game/g_sc2.c`:
   - establish an actual initial owned selectable unit on client begin;
   - derive the primary selected unit once after each successful `select`;
   - update portrait, wireframe, name, life, shields, energy, and commands;
   - restamp and resend the unified `LAYER_BACKGROUND` console tree.
3. Bind the parsed InfoPanel subtree in the shared console tree and hide selection-dependent children when there is no selection.
4. Resolve images and models through the merged `Assets.txt` catalog. Fix archive merging when Core entries are shadowed; do not invent paths or UI geometry.
5. Keep the complete bottom console in one `hud_console.c` write so sibling parent indices refer to the same retained tree.
6. Extend `test_sc2_consoleui.c` and `test_sc2_hud_live.c` to assert a positive-size textured center InfoPanel, selected-unit portrait/wireframe and stats, no-selection behavior, selection refresh, and one `LAYER_BACKGROUND` console message.
7. Run `make test-sc2` and `make test-sc2-live`.

## Phase 5: Add Authoritative Road Splines

1. Add compact road/control-point records to `common/sc2_map.h` using project `ARRAY` conventions.
2. Parse the schema confirmed in Phase 0 in `common/sc2_map.c`, including authored order, width, material key, UV scale/offset, flags, and points. Release all arrays in map teardown and log unsupported variants.
3. Adapt Quake III `idSplineList::calcSpline` and `buildSpline` into pure C in `common/sc2_utils.h`:
   - preserve the four-point uniform cubic B-spline basis exactly;
   - expose position and tangent evaluation;
   - use named, documented sampling limits;
   - reuse project vector math.
4. Build immutable road meshes per map in `renderer/sc2/r_sc2map.c`: sample each authored centerline, derive tangent and perpendicular strip edges, conform each edge to terrain height and normals, accumulate distance-based UVs, and preserve authored widths, materials, joins, and caps.
5. Render roads as depth-tested terrain decals with alpha blending, depth writes disabled, and documented polygon offset to prevent z-fighting.
6. Add straight and curved component-folder fixtures and test parsing, spline continuity, finite vertices, UV continuity, height conformity, and cleanup.
7. Require `TRaynor01` roads to match the reference placement and scale without floating, clipping through cliffs, or blurring at normal camera distance.

## Phase 6: Add Tower Wires

1. Use the representation proven in Phase 0: parse explicit map spline/link records in `sc2_map.c`, render explicit cable doodads through the object/model path, or implement authored M3 ribbon emitters and attachments in `r_m3_load.c`.
2. Never synthesize links from nearest towers or a hardcoded tower-name table.
3. Resolve authored endpoint attachments and transforms. Use source sag controls or the documented format default when the source stores endpoints only.
4. Sample cable curves with the same Q3-derived B-spline helper.
5. Generate a stable camera-facing ribbon or authored multi-sided cable mesh with continuous distance UVs and bounded tessellation.
6. Build static cables once per map or model registration. Keep them separate from the time-based effect emitter in `renderer/r_trail.h`.
7. Test endpoint transforms, sagged midpoint, multiple spans, missing endpoints, malformed controls, UV continuity, and resource cleanup.
8. Validate from two camera angles: cables meet tower attachments, sag smoothly, remain visible against terrain, and allocate nothing per frame.

## Phase 7: Documentation And Completion

1. Update `docs/games/starcraft-2/embedded-map-files.md` with confirmed road and wire schemas, defaults, diagnostics, and unsupported variants.
2. Update `docs/games/starcraft-2/terrain-and-world-rendering.md` with cliff material ranges, Q3-derived spline sampling, roads, and cable ownership.
3. Update `docs/games/starcraft-2/hud-layout-pipeline.md` with selected-unit InfoPanel lifecycle and layer refresh rules.
4. Run focused tests after every phase, then:

   ```sh
   make -j4 openwarcraft3 openwow opensc2 install-share
   make test
   make test-sc2-live
   ```

5. Capture WC3 ROC, WC3 TFT, WoW, and SC2 with `+screenshot 5 +com_frame_limit 10`, visually inspect every image, and record peak RSS and physical footprint.
6. Compare `TRaynor01` against the supplied references. Acceptance requires no blurry rectangular cliff planes, a complete center InfoPanel, terrain-conforming roads, continuous tower wires at authored endpoints, no required-resource magenta placeholders, and no NaN, shader, or GL errors.
7. Post screenshots, commands, tests, memory results, and remaining unrelated asset gaps to PR #170.
8. Resolve review discussions `r3871819160` and `r3871825331` only after their focused regressions pass. Keep the PR open while CI or visual acceptance is incomplete.

## Constraints

- Do not widen `entityState_t` or `playerState_t`.
- Do not duplicate or patch SC2Layout geometry in C.
- Do not guess asset paths, road shapes, cable links, or material identities.
- Do not add silent fallbacks; log unresolved authoritative data once.
- Keep roads, static cables, and cliff material ranges map/renderer-owned.
- Preserve one representation per shader concept and one parser per data schema.
- Use the Quake III spline basis, not its C++ containers or editor framework.
