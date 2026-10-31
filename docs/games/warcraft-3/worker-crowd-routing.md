# WC3 Resource Worker Crowd Routing

## Contract

Harvest movement uses the Warsmash-style collision split directly:

- Gold Mine approach keeps terrain, tree, and building pathing but ignores live-unit collision.
- Gold/lumber return keeps terrain, tree, and building pathing but ignores live-unit collision.
- Tree approach keeps live-unit collision and uses the resource-worker local
  avoidance policy: same-stream workers queue briefly, then use a deterministic
  bounded pass if the worker ahead remains stopped at the tree.
- Resource-building routes use the worker collision radius for static pathing. A newly constructed Farm therefore invalidates/rebuilds the static route and the bounded mover-owned A* accelerator can detour around it without reintroducing worker-vs-worker blocking.
- Return Resources targets the innermost collision-safe ring around the Town Hall/Lumber Mill footprint, choosing the worker's current side. Reaching that exact rasterized endpoint is a valid deposit handoff when the path grid leaves it just outside the continuous footprint+step threshold.

`games/warcraft-3/game/skills/s_move.c` owns both sides of that movement split:
`MOVE_AVOID_STATIC_ONLY` / `unit_changeangle_interaction_ignore_units` for
Mine/drop-off legs, and `MOVE_AVOID_RESOURCE_WORKER` for collision-preserving
tree approach. `skills/s_goldmine.c` owns Mine/Gold-return behavior;
`skills/s_harvest_lumber.c` owns tree and lumber-return behavior. Target choice,
Mine capacity, resource accounting, and replacement-tree selection remain
outside the router.

The queue/pass-right implementation described below is used only where live-unit
collision remains part of the resource contract: the approach to a tree. Gold
Mine approach and both resource-return legs intentionally ignore live units and
therefore do not select `MOVE_AVOID_RESOURCE_WORKER`.

## Near-Side Chop Targets

Tree approach does not follow a field rooted at the tree centre. The centre is
inside the authored footprint, so the field's adjusted goal is one grid-selected
corner shared by every worker in a group: the first worker chops there and the
rest queue at the contact boundary, which used to look like a group marching in
place next to the tree. `harvest_near_side_approach()` instead selects each
worker's own nearest legal point inside the authored chop range — the point on
the worker's own bearing — and steers there with the worker crowd policy. Each
worker's bearing already separates the targets; proactive per-tree caps spread a
group over neighbours, and the crowd queue/pass policy handles same-side
workers. The group spreads around the accessible edge and each worker stops on
its own `HARVEST_RANGE` check.

Distribution is planned **up front** rather than discovered reactively.
`harvest_start()` caps the number of live workers targeting one tree at
`HARVEST_TREE_WORKER_CAP` (2): the third and later workers of a group are sent
straight to `find_adjacent_harvest_tree()`, which keeps the nearest few live
trees and scores them by distance plus a penalty per worker already targeting
them. The group therefore spreads over adjacent trees as soon as the order is
issued, without any wait. This is a plan, not a reachability proof — the strict
direct-approach check still runs later in `find_reachable_replacement_tree()`,
the fallback that takes over when routing reports the chosen tree unreachable.

If no legal near-side point is visible, or the live-unit crowd blocks the step,
`ai_walktree()` holds the **stand** pose for that tick instead of presenting the
walk cycle in place; the queue/pass policy keeps retrying and the walk pose
returns on the first accepted step.

A worker that still cannot make meaningful progress for 20 consecutive ticks
(less than a quarter of one movement step, and only while it is actually beside
its tree) is reassigned again as a backstop for late arrivals and trees that
filled up after the order. Regressions:
`wc3_lumber_crowd.group_ordered_to_one_tree_reaches_chop_range`,
`wc3_lumber_crowd.crowded_tree_large_footprint_waits_without_marching`, and
`wc3_lumber_crowd.crowded_tree_spreads_group_to_adjacent_trees`.

## Why Queue Instead Of Alternating Lanes

The August 31 Human02 logs showed three starting Peasants converging on the Gold Mine at `(-4736,-3840)` and returning to the Town Hall at `(-3776,-4032)`.  The logs report a 19-unit worker step and the existing tests use the observed 16-unit Peasant collision radius.  Mine footprint samples match a 256x256 authored blocking footprint.  Town Hall samples recover a 384x384 footprint: for example, a worker 233.6 units horizontally and 198.2 vertically from the Town Hall centre reports a 42.1-unit footprint distance, which matches `hypot(233.6 - 192, 198.2 - 192)`.

Two earlier fixes were individually reasonable but interacted badly under crowding:

- assigning same-tree workers deterministic angular slots made workers deviate even when a simple queue would clear naturally;
- repeatedly choosing nearest free left/right local slides could make adjacent workers reverse their choice from one think to the next, producing visible dancing.

Caching a lane for the entire resource leg was also rejected: live collision can push a Peasant onto another side of the building, making the old edge point stale.  The current rule therefore keeps **adaptive direct targets** but makes the dynamic collision response deterministic.

## Python Simulation

`tools/wc3_peasant_crowd_sim.py` is a deterministic 2-D model built from the Human02 positions and movement constants above.  It does not attempt to emulate the full Warcraft III router.  It isolates the part under investigation: Peasant-sized swept-circle movement around other Peasants while travelling between the logged Gold Mine/Town Hall corridor.

The simulation compares:

- `direct_wait`: never sidestep another Peasant;
- `alternating_slide`: immediately search alternating +/- angular deflections, approximating the old local slide behavior;
- `queue_pass_right`: queue same-stream traffic, use deterministic right-first passing for crossing/pinned traffic, and bound lateral excursion.

Run the standard scenarios with:

```sh
python3 tools/wc3_peasant_crowd_sim.py --stress-seeds 100
```

Run the larger randomized check used before implementing the C policy with:

```sh
python3 tools/wc3_peasant_crowd_sim.py --stress-seeds 1000
```

The script writes `openrealm_peasant_simulation_results.csv`.  If matplotlib is installed it also writes `openrealm_peasant_simulation.png`; plotting is optional and is not required for the simulation itself.

### Logged 30-Peasant Return Scenario

Thirty Peasants return toward the Human02 Town Hall, seeded with exact logged return positions including `(-4575.3,-3865.7)`, `(-4575.2,-3934.1)`, and `(-4576.0,-3901.2)`.  Remaining workers are packed at non-overlapping 36-unit spacing behind the same corridor.  Short deterministic pauses force workers to interrupt one another's paths.

| Policy | Completed | Finish ticks | Mean path / direct | P95 lateral deviation | Max deviation |
|---|---:|---:|---:|---:|---:|
| direct wait | 30/30 | 44 | 1.000x | 0.0 | 0.0 |
| alternating slide | 30/30 | 37 | 1.073x | 76.5 | 96.5 |
| queue/pass-right | 30/30 | 42 | 1.011x | 35.5 | 36.7 |

`direct_wait` is straight but is not sufficient for two-way traffic.  `alternating_slide` completes quickly but adds unnecessary lateral travel.  `queue_pass_right` keeps the logged resource stream close to the direct route while retaining an escape.

### 30-Peasant Counterflow Scenario

Fifteen workers head toward the Town Hall while fifteen head toward the mine through the same corridor.

| Policy | Completed | Finish ticks | Worst stall |
|---|---:|---:|---:|
| direct wait | 25/30 | 400 limit | 398 ticks |
| alternating slide | 30/30 | 25 | 0 ticks |
| queue/pass-right | 30/30 | 26 | 1 tick |

This is why the implementation cannot simply make Peasants wait forever behind every blocker.

### Randomized Stress

The final `queue_pass_right` simulation was run for 1,000 deterministic random seeds with 30 Peasants per seed, mixed destinations, and temporary interruptions:

- 1,000 / 1,000 scenarios completed;
- 30,000 / 30,000 journeys completed;
- worst observed stall: 10 ticks;
- mean path length: 1.033x the direct trip;
- mean maximum lateral deviation: 16.6 world units;
- worst maximum deviation: 94.2 world units, inside the 96-unit emergency bound for a 16-unit Peasant;
- mean completion: 32.3 ticks;
- pure-Python runtime for all 1,000 scenarios: about 10.5 seconds on the development runner.

The Python model deliberately uses an O(N^2) neighbour scan because N=30 is tiny.  The engine implementation does **not** add an all-edict crowd scan: `move_is_valid()` continues to use the existing `BoxEdicts` swept-segment broad phase and evaluates only nearby collision candidates.  Removing same-tree angular slot assignment also removes the prior full-edict scans from the final lumber approach.

## Verification

Focused C regressions cover the policy primitives:

- `wc3_collision.resource_worker_queues_then_passes_right` checks four same-stream hold decisions followed by a bounded right-side escape;
- `wc3_collision.resource_worker_passes_opposing_traffic_immediately` checks counterflow does not enter the queue delay;
- `wc3_movement.lumber_same_tree_workers_preserve_direct_order` checks two moving workers keep the same direct tree target/order without being assigned artificial angular lanes;
- `wc3_movement.lumber_same_tree_worker_routes_around_chopper` checks a following worker queues briefly, then routes around a Peasant already chopping the shared tree without disabling live-unit collision;
- `wc3_movement.worker_resource_gold_approach_ignores_live_units` checks resource movement can cross another Peasant's collision circle on a Mine leg;
- `wc3_movement.worker_resource_tree_approach_keeps_live_unit_collision` checks tree harvesting retains ordinary unit collision for a destructable/tree target;
- `wc3_movement.worker_resource_lumber_return_ignores_live_units` checks Return Resources uses the no-live-unit-collision policy;
- `wc3_movement.worker_resource_static_detour_uses_worker_radius` puts a Farm-sized static block across the mine lane and checks the bounded detour is built with the Peasant collision radius;
- `wc3_lumber_crowd.group_ordered_to_one_tree_reaches_chop_range` orders six Peasants to one tree and checks most reach chop range while no worker is left presenting the Walk animation outside it;
- `wc3_lumber_crowd.crowded_tree_large_footprint_waits_without_marching` checks a group that cannot all fit around a large footprint queues in the stand pose instead of marching in place;
- `wc3_pathfinding.invalidation_does_not_recycle_heatmap_generation` checks a static/cache rebuild cannot alias a stale route generation to a new field;
- existing gold footprint tests continue to check mine entry, return, and deposit boundaries.

The simulation is evidence for choosing the local policy; the C tests remain the executable engine contract.  Human02 gameplay is the end-to-end validation because static pathing, authored footprints, flow-field fallback, mine occupancy, return/deposit state, and renderer timing are intentionally outside the Python model.
