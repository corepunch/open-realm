# WC3 Resource Worker Crowd Routing

## Contract

Gold and lumber workers use the ordinary static router and the ordinary swept-circle move validator.  Their only special rule is the **local dynamic avoidance policy** used after routing has chosen the direction for the current tick.

The resource-worker policy is deliberately small:

1. Aim at the direct interaction route: the closest legal authored-footprint edge for Gold Mines and resource drop-offs, or the closest legal chop point for a tree.
2. Try the exact direct step every think.
3. If another moving unit blocks that step and its goal is broadly in the same direction, treat it as a queue.  Hold the direct line for four blocked decisions instead of overtaking it.
4. Opposing/crossing traffic may pass immediately.  A same-stream worker that remains pinned after four blocked decisions gets the same escape.
5. Search deterministic right-hand deflections first at 15, 30, 45, 60, 75, and 90 degrees.  Search the opposite side only if the right side has no legal step.
6. Normal passing stays within five mover collision radii of the local direct corridor.  After eight blocked decisions the emergency bound expands to six radii.
7. The next think tries the direct route again.  No passing lane or building-edge point is cached.

`games/warcraft-3/game/g_ai.c` owns this policy through `MOVE_AVOID_RESOURCE_WORKER`.  Gold uses it from `s_goldmine.c`; lumber uses it for both tree approach and resource return from `s_harvest_lumber.c`.  Generic Move, Patrol, Attack, Build, Repair, and combat steering retain `MOVE_AVOID_GENERIC`.

The local corridor is reset when the direct route succeeds or when a static flow-field turn changes the desired heading by more than 30 degrees.  That keeps the lateral bound local to the current route segment rather than incorrectly constraining a legitimate corner around static geometry.

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
- `wc3_movement.lumber_same_tree_workers_queue_on_direct_approach` checks two workers keep the same direct tree approach and the rear worker queues instead of receiving a pre-assigned angular lane;
- existing gold footprint tests continue to check adaptive edge selection, mine entry, return, and deposit boundaries.

The simulation is evidence for choosing the local policy; the C tests remain the executable engine contract.  Human02 gameplay is the end-to-end validation because static pathing, authored footprints, flow-field fallback, mine occupancy, return/deposit state, and renderer timing are intentionally outside the Python model.
