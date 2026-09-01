# WC3 Pathfinding And Harvest Reachability

## Contract

WC3 movement keeps target selection, static routing, and interaction behavior separate:

```text
order / behavior -> target + interaction range -> routing -> collision-aware step
```

`games/warcraft-3/game/g_ai.c` owns per-tick steering and local block-and-slide. `common/routing.c` owns static pathmap line tests, connectivity queries, and cached flow fields. Harvest target selection remains in `skills/s_harvest_lumber.c`; the router never changes a tree target by itself.

Ground Move, Patrol, and Attack-move location orders are collision-size aware from destination selection through line tests, flow generation, and move-time validation. Generic interaction routing deliberately keeps the point-route contract: attack, gold-mine entry, resource return, repair, and similar behaviors own their interaction ranges and may need to continue toward a blocked target after the point flow reaches the footprint edge. Live units remain outside static flow fields and are handled by the precise swept-circle checks in `move_is_valid`.

Attack range against a building is measured from the attacker's collision edge to the building's authored no-walk footprint when `pathtex` is available. `skills/s_attack.c` therefore uses `CM_DistanceToPathingFootprint()` for building targets instead of requiring the attacker to enter weapon range of the blocked building centre. This is especially important for explicit force-fire on owned/friendly large buildings: centre-distance range checks make a melee unit orbit the footprint forever even though it is already beside a valid attack surface. Non-building targets retain the existing centre-distance attack check.

Lumber's unreachable-interior-tree detection is the narrower exception: `unit_changeangle_for_radius()` uses the Peasant's collision radius so Harvest can identify when the best legal approach to a blocked tree has genuinely been exhausted outside `HARVEST_RANGE`. Do not use that route-end signal for building interactions unless the route request also carries the behavior's interaction range.

Plain right-click movement is different from an interaction order: `move_selectlocation()` already assigns a collision-safe final waypoint, so `ai_move_walk()` may safely route that order with the mover's real collision radius. A distant temporary block does not cancel a plain move order. The unit keeps the order and retries local movement while the static route remains reachable; the existing near-goal settle rule is retained for an occupied final slot. If a completed collision-sized flow field reports the mover's component unreachable, the move may stop.

## Flow-Field Lifecycle

Game routing no longer uses the old lifetime quota of two synchronous whole-map flow-field bakes. That quota avoided repeated handheld stalls, but after it was spent a later uncached move order received generation 0 forever and generic steering fell back toward the raw target. A reachable order behind trees/buildings could therefore stop even though the static router could have found a route.

`CM_RequestHeatmapForRadius()` is now the game-facing cache-miss path. A cache hit returns its generation immediately; a miss starts one resumable reverse shortest-path job and returns 0 until the job completes. `G_RunFrame()` advances that job after entity simulation through `CM_ProcessPathJobs()`. The default relaxation budget is 4096 queue pops per frame and is runtime-tunable with:

```sh
+set wc3_path_work_budget 4096
```

The value is clamped to 256-65536. This keeps the expensive SPFA relaxation work bounded without permanently denying later destinations. Only one miss is built at a time; requests for other destinations naturally retry after the active job completes. Static-pathing invalidation cancels the in-progress job along with cached generations. A cache miss can therefore produce a short, intentional start pause when a real detour is required. Raising `wc3_path_work_budget` (for example to 8192) reduces that latency at the cost of more routing work per simulation frame; the default remains 4096 for handheld safety.

While a resumable request is pending, `unit_changeangle*()` leaves both `movement.flow_generation == 0` and `movement.flow_direct == false`. `unit_moveindirection()` treats that pair as "no heading resolved this tick" and does not commit a step. This shared guard is important: a caller must never turn a pending route into movement along the unit's stale facing.

`CM_BuildHeatmapForRadius()` remains the synchronous API for tests/tools that explicitly require a completed field. Production movement goes through `M_RefreshHeatmap()` -> `CM_RequestHeatmapForRadius()`.

`CM_BuildHeatmapForRadius()` and the resumable request path both key cached fields by adjusted target cell and mover collision radius. The flood and flow query use the same radius-expanded static pathability predicate as move-time terrain checks. The zero-radius `CM_BuildHeatmap()` wrapper remains for callers that intentionally route a point.

Flow vectors only descend to a strictly lower heatmap price for both collision-sized and radius-zero point fields. The adjusted goal cell therefore has a zero vector instead of pointing back out to a higher-cost neighbour. This matters for shared interaction routes such as Gold Mines and resource drop-offs: an outward point-flow at the adjusted cell makes every worker sharing that field orbit the same wrong location. Cached prices retain `INT_MAX` for cells that the completed field cannot reach. `CM_FlowReachedGoal(generation, x, y)` identifies the adjusted goal cell, while `CM_FlowCanReach(generation, x, y)` distinguishes a disconnected cell from a zero produced by interpolation near the goal.

Generic interactions still own their final range/contact semantics. When a radius-zero field reaches its adjusted legal cell beside a blocked entity target, `unit_changeangle()` records `flow_goal_reached` and resumes steering toward the real entity centre. Most behaviors still complete at their own footprint/range boundary. Gold Mine entry and gold return additionally treat that reached point-flow goal as a valid queue/deposit handoff, because the mover's collision validation can forbid the final step toward the blocked centre. Gold also reuses Move's near-goal settle detector for the crowded case where another worker leaves it a fraction outside the strict one-step footprint threshold. Location orders may instead stop at their collision-safe route endpoint.

Blocked building interactions should not make every worker use that centre-directed fallback as the normal approach. Gold Mine entry and gold/lumber return first look for a collision-sized, directly reachable cell beside the target's authored pathing footprint. The search band is routing slack only: mover radius + one simulation step + one path-cell diagonal. It accounts for radius-expanded pathmap cell centres near an irregular/square footprint; it does **not** enlarge mine entry or resource-deposit range. Once the edge staging point is within one movement step, behavior-owned footprint/contact checks remain authoritative and ordinary interaction steering resumes. `CM_PathCellWorldSize()` exposes the routing grid scale for this staging calculation; it is not a gameplay range constant.

The footprint edge is re-selected from the mover's current position rather than cached for the whole order. Live-unit collision can push packed workers onto different sides of the same building, so a previously good staging point can become a stale lane that sends the worker back through the crowd. `CM_FindApproachPointToFootprintForRadius()` keeps this adaptive policy inexpensive by marking the exact pathmap cells within range of the authored blocked pixels in a reusable scratch mask, then evaluating each candidate once. Do not replace that mask with a simple footprint bounding box: sparse and irregular pathing textures require distance to the actual blocked pixels.

## Retail Move Destination Behavior

Two defects explained the Human01 fence report and units getting stuck behind trees or towers:

1. `CM_LineIsWalkableForRadius()` used ordinary Bresenham stepping. A 45-degree step from one cell to the next checked only those two cells, so the direct shortcut accepted an `ox/xo` arrangement even though both cardinal side cells were blocked. The shortcut now requires both side cells to be legal, matching heatmap expansion and `compute_flow_at()`.
2. Location steering requested a point-sized route while `move_is_valid()` rejected positions using the unit collision radius. The field could therefore direct a unit through a gap that its body could not occupy. Move, Patrol, and Attack-move now use `self->collision` for the direct line and field; interaction behaviors retain radius zero.

When a clicked destination is in another static connected component, the destination-rooted field reports the mover cell unreachable. `CM_ClosestReachablePointForRadius(from, target, radius)` floods the mover component with the same radius and diagonal rules, then chooses its legal cell nearest the click. The location order retargets its private waypoint once and follows a normal field to that point. This avoids both failure modes of the old behavior: freezing at the order origin and sliding forever along the blocking wall.

Ordinary destination fields remain incremental and frame-budgeted. The mover-component flood is synchronous only after a completed destination field proves the click unreachable, so this exceptional recovery does not add input-time work to reachable orders.

The current router does not need wholesale replacement. Its cached integration fields, collision expansion, direct-line shortcut, and local dynamic avoidance are suitable for WC3 movement once they share one traversability contract. Remaining large-scale work is performance-oriented: incremental field construction and richer dynamic crowd routing, not a different static shortest-path algorithm.

### Retail Game.dll path audit

The ROC demo `data/Warcraft3demo/Game.dll` (build 4486, SHA-256
`286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654`) retains RTTI for `CAbilityMove`,
`NIpse::CLrPathingSys`, `NIpse::CLrPathingAcc`, and `NIpse::CLrPath`. `CAbilityMove` installs its vtable at
`Game.dll+0x102898`. The low-level path constructor at `+0x458040` creates persistent route state, while `+0x466aa0`
allocates and stores one of those path objects on a mover. Route setup at `+0x4661d0` compares destination and mode
state, submits an adjusted coordinate through `+0x458670`, and branches on several path-result states returned by
`+0x458930`. Group movement at `+0x46a130` and `+0x46a330` derives per-mover coordinates and path flags before updating
each path object. Retail routing is therefore not a point-only line test followed by movement that independently
rejects the unit footprint.

This supports the direction of commit `4bad783d`: using the mover's collision size consistently and resolving an
unreachable click to a legal endpoint are closer to retail's per-mover, adjusted-endpoint architecture than routing a
point toward an impossible destination. The binary does not establish that OpenRealm's nearest-cell flood, SPFA
integration field, cache policy, or exact `ox/xo` diagonal test matches Blizzard's algorithm. Treat the corner rule as
a necessary consistency fix and the closest-reachable policy as behaviorally retail-like, not instruction-equivalent.

## Retail Lumber Fallback

Retail Warcraft III continues lumber gathering when the explicitly clicked tree is alive but buried inside an unreachable group of trees.

OpenRealm keeps the clicked tree authoritative while routing can still approach it. If the worker reaches the collision-sized flow field's adjusted goal but remains outside `HARVEST_RANGE`, or the active field has no route from the worker's component, Harvest treats that approach as failed and selects a replacement tree. The replacement search excludes the failed tree and prefers the nearest live tree with a directly reachable legal harvest approach; a tree already within `HARVEST_RANGE` is immediately eligible.

This fallback is gameplay behavior in `s_harvest_lumber.c`, not a pathfinder rule. Normal movement, attack, patrol, item pickup, and spells do not acquire a different target when routing fails.

Multiple Peasants may legally harvest the same tree. Harvest now keeps the closest direct legal chop point instead of pre-assigning angular lanes. Dynamic separation is handled by the resource-worker local avoidance policy: a worker blocked by same-direction resource traffic queues briefly, while crossing or persistently pinned traffic uses deterministic right-first bounded passing. Static flow fields remain shared and occupancy-free, and the final tree approach no longer scans all edicts to allocate or reserve lanes. See [worker-crowd-routing.md](worker-crowd-routing.md) for the Human02 simulation and policy contract.

## Confirmed August 2026 Regression

A Human02 handheld trace for a Peasant ordered to an interior tree showed the worker reaching the forest edge and oscillating indefinitely at roughly 352-358 world units from the target. Every tick reported `blocked_frames=0` and, after instrumentation was extended, `flow=0 flow_goal=0`.

Two routing defects originally combined to prevent Harvest from ever receiving a usable failure/exhaustion state:

1. The lumber direct-line gate used `CM_LineIsWalkable()` as a zero-radius point test while the actual movement step used `CM_PointIsPathableForRadius(..., self->collision)`. A line could therefore be declared clear even when the Peasant could not physically fit along it. The fix remains scoped to behavior contracts that can safely use a collision-sized route; gold-mine/building interactions complete at their own contact/range boundary rather than at a flow field's adjusted goal.
2. The legacy generic heatmap build cap could permanently deny later route fields. The current implementation removes that lifetime quota entirely and uses resumable game routing instead, so lumber and ordinary movement do not depend on which route misses happened earlier in the match.

A third defect made a reached flow goal unstable: `compute_flow_at()` blended reachable neighbours including equal/higher-cost cells, so an adjusted goal beside asymmetric blocked geometry could point outward. All fields now follow only lower prices; radius-zero interaction routing then hands the final approach back to the behavior at the adjusted route end.

Do not reintroduce a distance-only timeout around Harvest to hide these routing failures. Fix and expose the routing state first, then let Harvest decide whether to retarget.

## Diagnostics

Runtime Harvest logging is off by default:

```sh
+set wc3_harvest_path_debug 1
```

prints Harvest transitions and fallback reasons. Level 2 adds per-approach route state and reports when a requested tree field becomes ready:

```sh
+set wc3_harvest_path_debug 2
```

Lumber routing uses the `WC3_HARVEST_PATH` prefix. Gold-mine entry uses `WC3_GOLD_PATH`, and gold return/deposit uses `WC3_GOLD_RETURN`. Generic resumable routing does not emit per-build debug lines. A healthy interior-tree fallback should progress through a nonzero flow generation and then one of:

```text
fallback ... reason=route_goal_out_of_range
fallback ... reason=route_unreachable
fallback ... reason=movement_blocked
```

followed by `start` and `reached` for the replacement tree.

## Verification

Focused tests live in `games/warcraft-3/game/tests/t_pathfinding.c` and `t_movement.c`. They cover:

- cache separation by collision radius;
- resumable cache misses serialize without losing a later destination;
- collision-radius-aware line walkability;
- direct-line and flow rejection of diagonal `ox/xo` corner cuts;
- rejection of a corridor too narrow for the mover;
- collision-sized Move, Patrol, and Attack-move route selection;
- collision-sized Move routing around a long wall;
- exact reachable clicks and closest reachable points across a disconnected wall;
- collision-radius expansion of the closest reachable boundary;
- end-to-end settling at the nearest reachable point for a disconnected click;
- a zero outward vector at the flow goal;
- end-to-end lumber retarget from a buried clicked tree to a reachable edge tree;
- gold-mine entry through an authored blocking mine footprint;
- gold return/deposit at an authored Town Hall footprint corner;
- lumber return to a Town Hall through an authored blocking building footprint;
- a distant temporarily blocked plain move keeps its order alive while near-goal jitter still settles.

Run when validating locally:

```sh
make test-wc3-engine WC3_PATTERN='wc3_pathfinding.*'
make test-wc3-engine WC3_PATTERN='wc3_movement.plain_move_*'
make test-wc3-engine WC3_PATTERN='wc3_movement.blocked_move_*'
make test-wc3-engine WC3_PATTERN='wc3_movement.*'
make test-wc3-engine WC3_PATTERN='wc3_movement.lumber_*'
```

### Interaction-owned route endpoints

Generic radius-0 point fields are still used for mine entry, resource return, attack, and other behaviors whose real target centre may be blocked. Their flow vectors nevertheless strictly descend to the adjusted legal route endpoint. Once that endpoint is reached, `unit_changeangle()` exposes `flow_goal_reached` and steers toward the real entity target. The owning behavior decides what that means: attack/range behaviors continue using their range test, while Gold Mine entry/return may hand off immediately at the route goal or after Move's bounded near-goal settle detector proves a crowded worker has stopped making progress at the interaction edge. This keeps routing monotonic without turning a distant blocked route into a successful interaction.
