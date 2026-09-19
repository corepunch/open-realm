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

## Placement and regeneration

Building placement already evaluates every used authored pathing-texture cell. Missing the `blighted` required bit now returns `PLACE_REQUIRES_BLIGHT`, and construction feedback uses the Warcraft `Offblight` key with a fallback message. Arrival-time build revalidation uses the same evaluator, so Blight removed while a worker travels can invalidate the order before payment/spawn.

Natural HP regeneration with `uhrt=blight` queries the current Blight field at the unit position every frame. Aura/Rejuvenation contributions remain separate and can still heal when the unit's natural Blight regeneration is inactive.

## Save/load

Save format 32 writes the final game-owned Blight plane after the level field stream and restores it before entity records. Each unit's `blight_growth` scalar state is part of the raw versioned `edict_t` record. This preserves both script mutations and the expansion progress/deadline of `Abli` sources. The saved byte count must exactly match the Blight grid initialized for the freshly loaded map.

## Known presentation gaps

This implementation is deliberately simulation-first. The following remain separate work:

- runtime server-to-client Blight synchronization for the placement preview;
- visible terrain Blight rendering/dirty-chunk updates;
- Blighted destructable/tree presentation, including Ghoul-triggered tree Blight;
- retail verification of the buildable-ground raster edge cases where Warsmash consults ground-texture metadata rather than only WPM pathing.

Until runtime client synchronization exists, the server is authoritative when a build click arrives, but the client's per-cell green/red preview only knows the map's initial WPM Blight.

## Verification

Regression tests cover WPM-seeded Blight, runtime add/remove, survival across `CM_BakeStaticObstacles()`, Blight-mask snapshot restore, runtime building placement, Blight-only regeneration, all five JASS natives, authored non-stock `Abli` expansion/availability from both TFT `DataA1`/`DataB1` and ROC `Data11`/`Data12` columns, and save/load of both world and per-source growth state.

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

Manual campaign/custom-map verification should also exercise `SetBlight*`/`IsPointBlighted`, an Undead building crossing a Blight boundary, a damaged `uhrt=blight` unit walking on/off Blight, and stock `Abli` expansion timing.
