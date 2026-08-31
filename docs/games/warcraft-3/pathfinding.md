# WC3 Pathfinding And Harvest Reachability

## Contract

WC3 movement keeps target selection, static routing, and interaction behavior separate:

```text
order / behavior -> target + interaction range -> routing -> collision-aware step
```

`games/warcraft-3/game/g_ai.c` owns per-tick steering and local block-and-slide. `common/routing.c` owns static pathmap line tests and cached flow fields. Harvest target selection remains in `skills/s_harvest_lumber.c`; the router never changes a tree target by itself.

Move-time occupancy is collision-size aware, but generic interaction routing deliberately keeps the existing point-route contract. Attack, gold-mine entry, resource return, repair, and similar behaviors own their interaction ranges and may need to continue toward a blocked target after the point flow reaches the footprint edge. Live units remain outside the static flow field and are handled by the precise swept-circle checks in `move_is_valid`.

Attack range against a building is measured from the attacker's collision edge to the building's authored no-walk footprint when `pathtex` is available. `skills/s_attack.c` therefore uses `CM_DistanceToPathingFootprint()` for building targets instead of requiring the attacker to enter weapon range of the blocked building centre. This is especially important for explicit force-fire on owned/friendly large buildings: centre-distance range checks make a melee unit orbit the footprint forever even though it is already beside a valid attack surface. Non-building targets retain the existing centre-distance attack check.

Lumber's unreachable-interior-tree detection is the narrower exception: `unit_changeangle_for_radius()` uses the Peasant's collision radius so Harvest can identify when the best legal approach to a blocked tree has genuinely been exhausted outside `HARVEST_RANGE`. Do not use that route-end signal for building interactions unless the route request also carries the behavior's interaction range.

## Flow-Field Lifecycle

Generic point routing keeps main's existing hard cap on synchronous whole-map flow-field bakes. An earlier version of this compatibility patch reset that cap from `G_RunFrame()`, which made each new obstructed right-click destination eligible for another full flood and produced a visible input-time stall on the handheld target. Do not globally refill that synchronous budget until generic routing is made incremental or otherwise cheap enough for the frame budget.

The collision-sized lumber route is intentionally separate. `M_RefreshHeatmap(goal, mover_radius)` bypasses the generic point-build cap only when `mover_radius > 0`; that path is used by the fixed tree target and cached on the tree after its first build. This keeps retail unreachable-tree recovery independent of whether generic movement already spent its two legacy bakes, without re-enabling repeated whole-map bakes for ordinary move orders.

`CM_BuildHeatmapForRadius()` keys cached fields by both adjusted target cell and mover collision radius. The flood and flow bake use the same radius-expanded static pathability predicate as move-time terrain checks. The zero-radius `CM_BuildHeatmap()` wrapper remains for callers that intentionally route a point.

Flow vectors only descend to a strictly lower heatmap price. The adjusted goal cell therefore has a zero vector instead of pointing back out to a higher-cost neighbour. Cached fields also retain a one-byte reachability mask: `CM_FlowReachedGoal(generation, x, y)` identifies the intentional zero at the adjusted goal, while `CM_FlowCanReach(generation, x, y)` distinguishes a disconnected cell from a zero produced by interpolation near the goal.

## Retail Lumber Fallback

Retail Warcraft III continues lumber gathering when the explicitly clicked tree is alive but buried inside an unreachable group of trees.

OpenRealm keeps the clicked tree authoritative while routing can still approach it. If the worker reaches the collision-sized flow field's adjusted goal but remains outside `HARVEST_RANGE`, or the active field has no route from the worker's component, Harvest treats that approach as failed and selects a replacement tree. The replacement search excludes the failed tree and prefers the nearest live tree with a directly reachable legal harvest approach; a tree already within `HARVEST_RANGE` is immediately eligible.

This fallback is gameplay behavior in `s_harvest_lumber.c`, not a pathfinder rule. Normal movement, attack, patrol, item pickup, and spells do not acquire a different target when routing fails.

## Confirmed August 2026 Regression

A Human02 handheld trace for a Peasant ordered to an interior tree showed the worker reaching the forest edge and oscillating indefinitely at roughly 352-358 world units from the target. Every tick reported `blocked_frames=0` and, after instrumentation was extended, `flow=0 flow_goal=0`.

Two routing defects combined to prevent Harvest from ever receiving a usable failure/exhaustion state:

1. The lumber direct-line gate used `CM_LineIsWalkable()` as a zero-radius point test while the actual movement step used `CM_PointIsPathableForRadius(..., self->collision)`. A line could therefore be declared clear even when the Peasant could not physically fit along it. The fix is scoped to the lumber approach; collision-expanding every generic entity route caused gold-mine/building regressions because those behaviors complete at their own contact/range boundary rather than at the flow field's adjusted goal.
2. Once the legacy generic heatmap build cap had been consumed, a later lumber target could receive generation 0. The lumber-specific collision-sized refresh now bypasses that generic cap and caches the resulting fixed-tree field. A global per-frame reset was tested and rejected because it caused visible right-click movement stalls from repeated synchronous whole-map bakes.

A third defect made a reached flow goal unstable: `compute_flow_at()` blended all reachable neighbours, including higher-cost cells, so a zero-cost goal cell could point outward. The corrected collision-sized field only follows lower prices.

Do not reintroduce a distance-only timeout around Harvest to hide these routing failures. Fix and expose the routing state first, then let Harvest decide whether to retarget.

## Diagnostics

Runtime logging is off by default:

```sh
+set wc3_harvest_path_debug 1
```

prints Harvest transitions and fallback reasons. Level 2 adds per-approach route state and flow-field build decisions:

```sh
+set wc3_harvest_path_debug 2
```

Lumber routing uses the `WC3_HARVEST_PATH` prefix. Gold-mine entry uses `WC3_GOLD_PATH`, gold return/deposit uses `WC3_GOLD_RETURN`, and the temporary `WC3_ROUTE_PATH` line records each generic synchronous flow-field build. Because the legacy generic build cap is retained, `WC3_ROUTE_PATH` cannot become a per-frame log flood. A healthy interior-tree fallback should progress through a nonzero flow generation and then one of:

```text
fallback ... reason=route_goal_out_of_range
fallback ... reason=route_unreachable
fallback ... reason=movement_blocked
```

followed by `start` and `reached` for the replacement tree.

## Verification

Focused tests live in `games/warcraft-3/game/tests/t_pathfinding.c` and `t_movement.c`. They cover:

- cache separation by collision radius;
- collision-radius-aware line walkability;
- rejection of a corridor too narrow for the mover;
- a zero outward vector at the flow goal;
- end-to-end lumber retarget from a buried clicked tree to a reachable edge tree;
- gold-mine entry through an authored blocking mine footprint;
- gold return/deposit at an authored Town Hall footprint corner;
- lumber return to a Town Hall through an authored blocking building footprint.

Run when validating locally:

```sh
make test-wc3-engine WC3_PATTERN='wc3_pathfinding.*'
make test-wc3-engine WC3_PATTERN='wc3_movement.lumber_*'
```

### Interaction-owned route endpoints

Generic radius-0 point flow fields retain blended steering around blocked goal cells because mine entry, resource return, repair, attack, and other behaviors own their interaction boundary. Strictly descending vectors are used only for collision-sized routes (`radius > 0`) where the lumber behavior needs an explicit route-end signal.
