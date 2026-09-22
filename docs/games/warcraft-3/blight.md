# Warcraft III Blight

## Scope

OpenRealm owns gameplay Blight as mutable Warcraft game state rather than as an ability-local decal or a shared-engine policy. The WPM `0x20` bit seeds a dedicated 32-unit-cell plane in `games/warcraft-3/game/g_blight.c` when the map starts. The shared routing pathmap remains game-agnostic and immutable with respect to runtime Blight; WC3 placement overlays the game-owned current Blight bit when evaluating authored pathing cells. Static obstacle rebakes therefore cannot erase JASS or `Abli` mutations.

The authoritative consumers are:

- `G_IsPointBlighted()` for simulation queries;
- building `requirePlace=blighted` / `preventPlace=blighted` footprint checks;
- `UnitBalance.uhrt=blight` natural hit-point regeneration;
- the JASS `SetBlight`, `SetBlightRect`, `SetBlightPoint`, `SetBlightLoc`, and `IsPointBlighted` natives;
- passive `Abli` Blight Growth.

Blight is global terrain state. The JASS setter natives validate their `player` handle for API compatibility, but the stored field is not per-player; `IsPointBlighted` likewise has no player argument.

## Rasterization and pathing ownership

WC3/Warsmash Blight is authored on 128-world-unit terrain corners, with each changed corner projecting to the surrounding 4x4 32-unit pathing cells. OpenRealm follows that shape for point/radius/rect operations. `G_BlightInit()` samples the initial shared pathing flags into `level.blight.cells`. Runtime add/remove operations then mutate only that WC3-owned field, so scripts can both create Blight and clear authored map Blight.

The shared `pathmap.terrain`, `pathmap.original`, and `pathmap.data` stay untouched by runtime Blight. WC3 building placement samples shared pathing, then replaces only the Blight predicate with `G_IsPointBlighted()` before applying `preventPlace` / `requirePlace`. Blight does not block movement, so changing it cannot invalidate shared movement heatmaps.

## Blight Growth (`Abli`)

`Abli` is passive and has no command-card cursor. `CAbilityBlightGrowth` resolves the concrete authored alias and uses:

- `DataA`: non-zero creates Blight; zero removes Blight;
- `DataB`: radius added each expansion step;
- `Area`: maximum radius;
- `Duration`: seconds between expansion steps.

The unit stores the concrete alias, current radius, and next expansion deadline in `edict.blight_growth`. The state is initialized on unit bind/ability add, cleared on ability removal/unit removal, and uses the restored simulation clock. Per-player `SetPlayerAbilityAvailable` suppression stops expansion without deleting existing Blight; when availability returns, an overdue step may happen immediately, matching Warsmash's disabled-tick behavior.

The implementation intentionally does not erase Blight when the source dies or disappears. `Abli` paints world state; removal is an explicit Blight operation, not reference-counted aura teardown.

Growth is suspended while `construction.active` is true. If authored `Area` data is reduced while a source is alive, its stored progress is clamped for future growth; previously painted world cells are not removed.

## Placement and regeneration

Building placement already evaluates every used authored pathing-texture cell. Missing the `blighted` required bit now returns `PLACE_REQUIRES_BLIGHT`, and construction feedback uses the Warcraft `Offblight` key with a fallback message. Arrival-time build revalidation uses the same evaluator, so Blight removed while a worker travels can invalidate the order before payment/spawn.

Natural HP regeneration with `uhrt=blight` queries the current Blight field at the unit position every frame. Aura/Rejuvenation contributions remain separate and can still heal when the unit's natural Blight regeneration is inactive.

## Save/load

Save format 32 writes the final game-owned Blight plane after the level field stream and restores it before entity records. Each unit's `blight_growth` scalar state is part of the raw versioned `edict_t` record. This preserves both script mutations and the expansion progress/deadline of `Abli` sources. The saved byte count must exactly match the Blight grid initialized for the freshly loaded map.

## Client presentation

The server publishes the authoritative Blight plane through the existing generic terrain-mask section of the Warcraft III per-frame game datagram. A client receives the initial map plane after `G_ClientBegin`; later point/radius/rect changes dirty affected rows first, then a per-client background sweep resends the next band every `BLIGHT_SWEEP_INTERVAL` frames with at most `BLIGHT_SWEEP_BYTES` of payload, wrapping at the last row, so a dropped unreliable datagram still converges. Each chunk carries the grid origin, dimensions, 32-unit cell size, and a contiguously packed row range in the shared `MSG_EncodeRLE`/`MSG_DecodeRLE` codec with FOW (`[init][runs...]`, plus a raw bitpack escape with init byte `2`), so a large initial plane can span frames without widening `entityState_t` or starving the existing weather/tint payload. Dirty and sweep bands start from the full contiguous/remaining run and shrink to fit, so coherent Blight covers far more rows per band than the old bitpack density; when RLE overflows (alternating masks cost ~1 byte per bit), the bitpack escape bounds one row at `1 + (W+7)/8` bytes and dirty/sweep progress never stalls. The client validates the payload before resizing its mask and rejects grids over `TERRAIN_MASK_MAX_CELLS` cells.

The target model renders Blight as terrain texture selection rather than as a square decal, preserving irregular transitions: the map tileset resolves through `UI\\WorldEditData.txt` to the tileset-specific `TerrainArt\\Blight\\*_Blight.blp` atlas; each 128-unit terrain corner selects Blight, cliff-associated, or normal ground texture; four effective corners drive normal atlas transition selection. Constraints: gameplay Blight stays a 32-unit authoritative state, visual composition stays renderer-owned with no second pathing representation, and missing textures are reported rather than silently replaced.

- `CL_ParseFrame()` assembles chunks into a persistent generic client `terrainMask_t` and exposes it through `viewDef_t.terrain_mask` with one assignment; incoming bits are compared against existing cells and bump generation only on change; WC3 contributes its Blight interpretation through `CL_GameModifyBuildPathing()`;
- the WC3 terrain renderer recomputes all 128-unit corners and active flags from the full synchronized 32-unit plane on generation change, counts emitted tiles and allocates exactly six vertices each, builds a dedicated Blight terrain layer with the normal terrain shader, and draws it after normal ground layers but before cliffs; solid tiles use the atlas's opaque variation and partial tiles use its authored alpha edge masks via `SetTileUV()`. Authored W3E Blight remains visible without falling back to normal ground;
- the placement preview lets WC3 overlay the synchronized Blight bit on each footprint sample before applying required/prevented flags; the server remains authoritative when the order arrives;
- destructables carry a persistent one-way Blight presentation state, initialized from their footprint, updated by added Blight, and set by successful Undead lumber hits. The server presents it through the existing image delta and generic vertex-colour snapshot channels, using the authored texture stem plus `Blight` when available;
- remaining: retail verification of the buildable-ground raster edge cases where Warsmash consults ground-texture metadata rather than only WPM pathing.

## Verification

Regression tests cover WPM-seeded Blight, runtime add/remove, survival across `CM_BakeStaticObstacles()`, Blight-mask snapshot restore, runtime building placement, Blight-only regeneration, all five JASS natives, authored non-stock `Abli` expansion/availability from both TFT `DataA1`/`DataB1` and ROC `Data11`/`Data12` columns, save/load of world, source, and destructable state, destructable one-way presentation, and preservation of the existing image/tint presentation channels.

After building, run at least:

The test runner treats a trailing `*` as a prefix matcher, so use the suite-specific prefixes below rather than a substring glob.

```sh
make test-wc3-engine WC3_PATTERN='wc3_building.blight_*'
make test-wc3-engine WC3_PATTERN='wc3_combat.blight_*'
make test-wc3-engine WC3_PATTERN='wc3_api.blight_*'
make test-wc3-engine WC3_PATTERN='wc3_save.blight_*'
make test-wc3-engine WC3_PATTERN='wc3_pathfinding.blight_*'
make test
```

For one runtime trace of the server-to-renderer path, build/run with `WC3_DEBUG_BLIGHT=1`. The log should show a `growth tick` line, a server `datagram` row range with its sweep flag, and a renderer `cache generation` line.

Manual campaign/custom-map verification should also exercise `SetBlight*`/`IsPointBlighted`, an Undead building crossing a Blight boundary, a damaged `uhrt=blight` unit walking on/off Blight, and stock `Abli` expansion timing.

## Cliff Boundaries

The renderer's cached corner mask rejects `R_CliffOwnsCorner` before sampling network or authored Blight. Any adjacent cell omitted
by `R_TileHasGround` owns that corner, including the low neighbour of a two-cell ramp. This preserves the cliff type's ground border
instead of painting Blight across its lip. Reapply this restriction on every mask generation so later spread cannot repaint it.
The gameplay Blight plane, save data and network mask retain their existing meaning; this is terrain presentation eligibility.
`renderer_terrain.blight_preserves_cliff_corners` covers spread and a subsequent generation. See
[map renderer cliff ownership](architecture/map-renderer.md#shared-normal-welding-and-undead04).
