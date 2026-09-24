# SC2 selection and control

## Ownership and input

The single local client retains `ps.number == 1`, matching the current Galaxy
`PlayerGroupPlayer` session contract. Do not infer its identity from placed army
size: TRaynor01's script assigns `gv_p1_USER = 1`, while player 2 owns the largest
placed army. `UnitCargoCreate` asks the game for the transport's owner and creates
cargo for that owner. `UnitGetOwner` uses the same game callback.

Shared `client/cl_input.c` handles click/rectangle selection and right-click orders
exactly as WC3. `SC2_CustomizeEntity` marks scenery and foreign units unselectable.
`select` replaces membership (including empty requests), validates ownership, and
returns `svc_set_selection`. The existing snapshot path publishes `RF_SELECTED`;
no new network fields or HUD layouts are involved. `smartpoint` moves the selected
owned mobile units, and `smart` uses a target entity's position. Selection must
never suppress the next point order: the shared input already separates the two.

## Shared WC3 routing

`server/sv_routing.c` is the original WC3 router, included by both games' `g_world.c`
inside their server world. It is excluded from the executable's unity source list
so it cannot create a competing client-side routing state. `server/routing.h`
contains `ROUTEPATH` and the default per-tick work budget.

`CM_AccelerateRoute` is extracted from WC3's persistent accelerated-turn helper;
WC3 and SC2 both use it. `CM_SlideRoute` likewise contains WC3's existing
15-degree left/right heading search, called by both games when the next step is
blocked. Without this local steering, SC2 stalled permanently at the radius-expanded
wall corner even though the shared field supplied a valid route. The WC3 movement member retains the original field order
and types within `movement.path`. Direct paths, bounded A*, collision-radius
checks, incremental flow fields, and unreachable-goal adjustment all use the
existing WC3 `CM_*` implementation. No second pathfinding algorithm is introduced.
SC2 services pending fields each tick and does not invalidate all fields per order
or substitute straight-line motion when a route is pending. Flying units retain
the existing direct flight behavior. Walk/Stand frames use server model sequence
intervals instead of assigning absolute server time as an animation frame.

SC2's pre-existing terrain adapter still initializes an open terrain grid and
bakes static entity obstacles. Decoding SC2 navigation/pathing layers, richer
formation assignment, full WC3-style local unit avoidance, combat, alliances,
selection of foreign units, and shift order queues remain separate work.

## Model feedback

M3 unit forward is -Y. The shared renderer applies SC2's mandatory model basis so `angle` remains a gameplay
heading from +X. Placed objects decode their native mesh rotation through `SC2_PlacementHeading`, preserving
bridges and scenery; Galaxy `UnitCreate` and `UnitSetFacing` both decode degrees to radians. See
[coordinates and model transforms](../../../AXIS.md#sc2-authored-placement-and-facing).

`R_TraceModel` inverse-transforms the cursor segment into M3 model space and tests
the authored bounds, returning world-space hit distance. These same bounds serve
visibility culling. SC2 loads the shared procedural ring and samples terrain Z
at its vertices; the old empty asset hook and Z=0 splats could not show selection
on the elevated campaign terrain. The shared renderer draws both selected and
hover rings from the existing flags.

## Snapshot precision (September 2026)

The cell-stepping report was also a wire-format defect, independent of pathfinding.
`common/msg.c` serialized non-WoW entity origins as three integer shorts and
selection radii as an integer short. Runtime logs showed the server publishing
radius `0.375` while the renderer received `0`, producing a zero-area ring despite
a valid texture. Moving positions such as `(56.272789, 49.727211)` likewise lost
their fractional components. Galaxy flight and player orders share this snapshot
path, so the dropship cutscene suffered the same stepping.

All games now serialize entity origins with `NFT_VECTOR3_FLOAT` and radius with
`NFT_FLOAT`, matching the existing exact collision-radius contract. The common
client still interpolates previous/current snapshots in `CL_AddEntity`; no
SC2-only interpolation path or alternate coordinate representation is needed.
`entityState_t` is unchanged. Protocol version 5 records the wire change: origins
cost six additional bytes when sent for WC3/SC2, and radii two additional bytes
when sent. Unchanged fields still consume no delta payload. Rebuild both ends.

Do not fix this by enlarging the circle, snapping the simulation, or giving SC2
another router. Inspect server values and decoded client values independently.

## Verification

- `make test-sc2-engine`: selection replacement/clearing, owner filtering, immediate
  point orders, arrival, rotated/scaled M3 picking, a collision-sized wall detour
  through the shared accelerator/field/slide, fractional Galaxy flight snapshots, cardinal/detour model facing, authored placement, and camera preservation.
- `make test-galaxy`: cargo inherits a non-default transport owner; invalid cargo
  creation does not spawn units.
- `make test`: includes fractional/large-radius snapshot round trips, WC3
  movement/pathfinding and save/load coverage after the
  shared router move.

For a paced campaign run (frame limits count main-loop iterations, not server ticks):

```sh
build/bin/opensc2 -data data/StarCraft2 +com_maxfps 60 +vid_hidden 1 \
  +map Maps/Campaign/TRaynor01.SC2Map +screenshot 1800 +com_frame_limit 2100
```

Startup logs identify Galaxy-created entity numbers. After the drop, `cmd select
<ids...>` and `cmd smartpoint <x> <y>` exercise the same authoritative commands as
mouse input. `select 0` clears the rings. Use the shared group recall twice to
center the camera on selected units.
