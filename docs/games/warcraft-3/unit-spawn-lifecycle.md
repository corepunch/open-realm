# Runtime Unit Spawn Lifecycle

## Contract

`SP_SpawnAtLocation()` is the presentation-aware runtime spawn path. It binds
unit data, initializes the edict, installs the normal unit callbacks, and calls
`birth`, which selects the birth animation and stores the authored build time in
`edict.wait`.

`SP_SpawnAtLocationNoBirth()` performs the same initialization without that
presentation lifecycle. It is appropriate when the caller owns the resulting
state, such as restoring a gold-mine overlay.

The runtime spawn path links the edict only after `SP_CallSpawn()` completes.
`SP_SpawnUnit()` derives a building's collision radius from its authored data;
linking first leaves the server broad-phase bounds at zero and lets movers walk
through the building even though its entity state reports collision.

JASS `CreateUnit` is an immediate creation operation. `unit_create()` therefore
uses the no-birth path, enters `stand` once, applies the requested facing, and
activates food. It must not call `SP_SpawnAtLocation()` followed by `stand()`:
`stand()` changes the animation but does not clear the birth wait, leaving a
ready unit with a stale build-time delay.

Public mobile creation uses the verified32-ring fine footprint admission through
Move's `S_InitUnitPosition`, including stationary neighbours and static pathing.
The initial scalar write starts from retail's `-128000.5` native-fine sentinel;
preserving cancellation is necessary for exact fractional GetUnitX/Y results.
The accepted pose survives save/load and begins ordinary scheduled Move without
losing its native bits. If no point is admitted, creation retains its handle and
logs the unresolved unit/coordinates. See [original public spawn evidence](retail-pathfinding-engine.md#public-spawn-admission-and-initial-mover-pose).

Movement-disabled units keep the requested point. Warsmash resolves `movetp`
through `PathingGrid.getMovementType()`; a value that names no movement type
becomes `MovementType.DISABLED`, which `isPathable()` accepts everywhere and
`intersectsAnythingOtherThan()` never collides. Retail `Units\UnitData.slk`
authors `_` on all 136 building and scenery rows (farms, towers, the `nfrm`
Frostmourne pedestal) and one of `foot`/`horse`/`fly`/`hover`/`float`/`amph` on
everything else. `M_UnitMoveDisabled()` in `skills/s_move.c` is that class, and
Public admission and `G_CanRepositionUnitAt()` consult it, so `CreateUnit`, `SetUnitPosition` and
`SetUnitPositionLoc` agree. A row with no `movetp` cell at all stays mobile.

Do not classify scenery from speed or weapons: an earlier heuristic (no movement
type + zero speed + no enabled attack, `CreateUnit` only) left towers nudged,
farms not, and `SetUnitPosition` inconsistent with `CreateUnit`.

The public creation search snapshots quantized live-unit occupancy once per
request and uses the verified class-sized fine rectangles. Candidate tests
reuse that snapshot; they do not rescan the full actor set for each cell.

Construction, training, and summons use `SP_SpawnAtLocation()` directly because
their owning systems may consume the birth presentation or replace it with a
construction/hidden lifecycle afterward.

## Verification

`wc3_api.createunit_static_scenery_keeps_requested_spawn` verifies that a
stock-shaped Frostmourne row keeps its requested point when pathing is blocked.
`wc3_api.createunit_custom_static_scenery_keeps_requested_spawn` verifies a
custom rawcode inherits the scenery rows and applies a non-stock max-health
override without moving.
`wc3_api.movement_disabled_armed_unit_keeps_requested_position` verifies an
armed `_` row with a non-zero speed cell through both `CreateUnit` and
`SetUnitPosition`. All live in `games/warcraft-3/game/tests/t_api.c`.

`wc3_api.createunit_starts_ready_without_birth_delay` separately verifies that
the created unit enters `stand` with `wait == 0`, and
`wc3_api.createunit_links_building_collision_bounds` covers the corresponding
server-link contract with a synthetic building row and verifies that its
collision-sized bounds are visible to `BoxEdicts()`.

Run both Warcraft III data modes with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.createunit_*'
```

See [Human07 Mission Troubleshooting](human07-troubleshooting.md) for the complete
mission investigation, deferred `RemoveUnit` semantics, `SuicidePlayer`, and the
confirmed building-link regression.
