# Retail pathfinding: evidence required before reimplementation

[Contract and coverage ledger](retail-pathfinding.md). Target: retail
**1.27.1.7085**, binary hash and tools as specified there. This checklist tracks
remaining evidence, not implementation tasks or a percentage-complete estimate.
Existing verified mechanisms are prerequisites, not work to repeat.

## Closure rule

Every checkbox requires: producer/consumer and state contract; reproducible
fixture/command; expected versus observed result; artifact; evidence level
(S/O/C/L from the ledger); remaining exclusions. Append those references when
checking it off and update the corresponding ledger/evidence section.

A task is closed by verified behavior or a documented proof that the branch
cannot occur within scope. A guessed meaning, arbitrary tolerance, passing
prefix, unexplained mismatch or lack of observed calls does not close it.
Retain retail quirks; optimal paths are not the reference when retail differs.

“100% faithful” is the target contract, not a conclusion obtainable from a test
count. Readiness requires no known behavior-affecting unknowns within the
inventoried scope, plus branch and scenario coverage below. New discoveries
extend this list; they must not silently become implementation assumptions.

## Execution order

| Stage | Work | Exit artifact |
| --- | --- | --- |
| 1 | BASE; ORDER/MOVE/GROUP paths needed for one ordinary ground move | Reproducible order → fresh search → movement → completion → next order baseline |
| 2 | MAP, FOOT, FINE, ACC, NUM | Complete spatial/search contracts and boundary corpus |
| 3 | Remaining ROUTE, TARGET, SCHED, MOVE, ORDER | Dynamic obstacles, moving targets, contention and interruption compositions |
| 4 | Remaining GROUP, FORM, SEP, GATE | Formation, crowd and special-edge lifecycle compositions |
| 5 | E2E and READY | Frozen evidence package and OpenRealm integration specification |

Dependencies guide sequencing, not separate investigations: add each recovered
feature to the baseline and assert intermediate state before extending it.
NUM supports every geometry layer; map lifecycle supports every dynamic case.

## Parallel execution protocol

Use one coordinator and up to three workers. Parallelize independent contracts;
keep dependent full-lifecycle composition with the coordinator.

| Role | Responsibility |
| --- | --- |
| Coordinator | Own BASE-06 and the critical dependency chain; review findings, compose fixtures, integrate documentation and close checklist items |
| Lifecycle worker | A bounded order/callback/reclamation contract feeding ORDER; avoid concurrent edits to the shared motion oracle |
| Map worker | MAP/FOOT contract with a dedicated map fixture/oracle |
| Numeric worker | NUM helper/branch contract with an independent reference and boundary oracle |

Each assignment specifies task IDs, exact question, owned files/report paths,
existing evidence, allowed experiments and a verifiable stop condition. Deliver
entrypoints/ABI, fixture, assertions, results/artifacts and unresolved branches.
Return a concrete blocker/dependency when found; do not spend a whole batch
expanding into another worker's task.

Workers use separate new oracle files or explicitly disjoint existing files;
unique report paths prevent overwrites. The coordinator owns the main ledger,
checklist and shared Ghidra annotations. Serialize Ghidra mutations and live
retail process/map manipulation; independent offline original-code runs can
proceed concurrently. No concurrent patching of the same fixture.

Review and integrate after each bounded result, then assign the next unlocked
task. Run changed oracles and affected compositions; rerun broader suites only
when shared behavior or dependencies changed. Share precise findings/artifact
paths instead of entire transcripts. Keep unsupported interpretations open.

### Current composition frontier

| Contract | Next evidence | Diagnostic owner/file |
| --- | --- | --- |
| ORDER/MOVE/GROUP | Populate full owner separation/shared-group updates; extend verified fine/adaptive wall trajectories | One worker owns `verify_wc3_pathing_motion.py` |
| MAP | Travel/failure after uninterrupted widget-produced escape admission; resource/cache/destructable lifecycle | Map worker: `verify_wc3_pathing_widget_masks.py` |
| ORDER | Extend controlled command inputs toward UI/network producers; preserve verified initial admission/FIFO/replacement/interrupt journeys | Numeric worker: `verify_wc3_pathing_order_tasks.py` |
| E2E/ORDER | Extend no-attach controls to crowds; preserve live building placement/invalidation witnesses | Coordinator: Frida controller/analyzer and docs |

Names are under `tools/ghidra/` unless stated. These are next dependencies,
not assertions that every lane is running; validate live agent/session state.

### Verified partial closures

| Tasks advanced | Evidence / boundary |
| --- | --- |
| ORDER-01, MOVE-03 | [36 full arrival dispatches](retail-pathfinding-movement.md#complete-arrival-dispatch-and-support-refresh): handler/unwind, subscriptions, timer, stop and support height. [108 authentic active-task arrival/pop cases](retail-pathfinding-movement.md#active-arrival-and-authentic-task-removal) cover empty-queue return, next-task rejection, and acceptance retaining the new task with group/path preparation. [4,957 exact point-range cases](retail-pathfinding-movement.md#exact-point-task-range-predicate) cover the rejection predicate; [948 object-range calls](retail-pathfinding-movement.md#exact-object-range-predicate) add two-radius/prediction/clamp behavior and six invalid-handle fault probes. [Live Stop/Move replacement](retail-pathfinding-experiments.md#live-stopmove-replacement-and-task-cleanup) verifies separate cleanup/head mutation. 36 accepted-task fresh fine-search ticks now execute; [Live blocked-goal recovery](retail-pathfinding-experiments.md#live-blocked-goal-recovery-task-sequence) verifies the fallback. [77 elapsed trajectories](retail-pathfinding-movement.md#composed-elapsed-travel-arrival-and-reclamation) now reach arrival and reclamation, including two exact-repeat fine wall detours and one adaptive-to-fine and one full singleton-owner composition. [288 user-order producers/984 point-task producers/24 produced-chain queued-order arrivals](retail-pathfinding-movement.md#original-user-order-to-task-production) now pass. Four two-order FIFO cases also pass. Three active-replacement and three interrupt/resume journeys also pass. [34 full initial unit admissions](retail-pathfinding-movement.md#complete-initial-unit-admission) now pass without mid-call provisioning. UI/network producers, bridge/water/clamped presentation, populated targets and alternate recovery branches remain open. |
| ORDER-04 | [8 last-reference release cycles, 6 factory reuses](retail-pathfinding-movement.md#last-reference-payload-release-and-factory-reuse). Preallocated storage/supplied registration; relations/children/negative domain remain open. |
| MAP-01/02/03 | [11,664 edits, 216 rebuild/reversal compositions, 30 clipped updates](retail-pathfinding-search.md#terrain-edit-and-explicit-rebuild-composition). [Origin producer and 25 complete no-file loader calls](retail-pathfinding-search.md#terrain-origin-producer-and-map-factory-composition) now covered; [150 maintenance callbacks and 25 release prefixes](retail-pathfinding-search.md#constructed-map-maintenance-and-partial-release) verify continued hierarchy staleness and the external-free boundary. [Decoded WPM/image mask loops](retail-pathfinding-search.md#decoded-wpm-and-image-mask-consumers) cover mask translation; [144 overlapping widget raster sequences](retail-pathfinding-search.md#widget-rasterization-and-overlapping-occupancy) plus96 full widget removals,96 paired reapply/remove lifecycles,320 nonempty callback/rejection cases and two destruction prefixes to Storm403 cover lazy records, snapping and the real refresh gate; file-backed loader/reload, allocation growth, non-dyadic inputs and other invalidation producers remain open. |
| NUM-01/02 | [200,330 exact helper calls, 2,130 normalizations, 864 bounds prefixes](retail-pathfinding-separation.md#exact-scalar-arithmetic-and-occupied-cell-boundaries). Trig, reciprocal table derivation, producer domains and full trajectory composition remain open. |

No whole checkbox below closes from these slices. Next coordinator dependency:
UI/network order production, populated separation and crowd/shared-group
variants of the verified order → search → travel → arrival chain.

## BASE — scope and evidence infrastructure

- [ ] **BASE-01** Inventory every movement entry point/caller: player/JASS/AI point and target orders, queued/replaced orders, ability-driven approach, forced position changes and pathing bypasses. Classify shared versus distinct routing paths; record any additional reachable modes discovered.
- [ ] **BASE-02** Inventory movement types, masks/lanes, object categories and valid input domains from data and producers. Include ground/air/water/amphibious/other authored modes where present; establish supported terrain/support-surface interactions rather than assuming them.
- [ ] **BASE-03** Build a reachable-function/branch and field read/write inventory from entry points through cleanup. Include virtual callbacks, globals, flags, constructors, allocators and caller preconditions; associate every current oracle exclusion with a task or existing coverage.
- [ ] **BASE-04** Define a shared fixture/trace schema: build/data/map hashes, ordered entities/handles, initial state, simulation times, PRNG state, commands/edits, expected events and termination. Capture map cells, routes, indices, budgets, flags, membership and motion at stable boundaries.
- [ ] **BASE-05** Add one corpus manifest/runner for existing oracles and retail captures. Distinguish expected retail/reference differences from regressions; record full calls versus prefixes/stubs, seed, scope and exact/tolerant assertions. Fail on truncated capture or missing completion.
- [ ] **BASE-06** Freeze one complete ordinary ground-move baseline, including construction, fresh group/member searches, spatial updates, arrival callback, release and next order. Compare intermediate state; establish empty/idle before and after invariants.

## MAP — construction, mutation and invalidation

Evidence: [search](retail-pathfinding-search.md#map-construction-and-invalidation), [terrain experiments](retail-pathfinding-experiments.md#direct-terrainhierarchy-divergence-witness).

- [ ] **MAP-01** Recover exact world/fine/proximity/adaptive origins, padding, dimensions, coordinate conversions, clipping and edge-cell semantics; test negative coordinates, non-power-of-two maps, corners and boundary-adjacent values.
- [ ] **MAP-02** Complete initial terrain/pathing/object load order, mask population and hierarchy construction, including relevant cliffs, water, bridges and authored pathing textures identified by BASE-02.
- [ ] **MAP-03** Enumerate every terrain/object invalidation producer: spawn, movement, size/pathing change, construction, destruction/removal and map-script edits. Verify affected cells/levels and update timing; distinguish intended staleness from missed producers.
- [ ] **MAP-04** Compose overlapping self/target exclusions, coincident terrain, multiple objects and edits during requests; verify exact restoration on each reachable early/failure/reentrant exit.
- [ ] **MAP-05** Recover map/spatial storage growth, free/reuse, stamps and generation reset/wrap. Test capacity boundaries, dead/metadata records, reclamation scheduling and reachable allocation failures.
- [ ] **MAP-06** Trace map teardown/reload and persistence boundaries. Establish whether save/load preserves or rebuilds path/map/handle state; test the reachable movement consequences.

## FOOT — footprint and collision contract

Evidence: [footprint queries](retail-pathfinding-search.md#footprints-and-dynamic-occupancy).

- [ ] **FOOT-01** Complete collision data → runtime scalar → footprint class/geometry for every producer, including runtime changes, group maximum and target radius. Sweep immediately below/equal/above each class boundary.
- [ ] **FOOT-02** Test corridor width × radius × sub-cell alignment × approach direction × movement lane; include diagonals, corners, touching footprints and map edges. Match accepted cells and routes, not just reachability.
- [ ] **FOOT-03** Resolve object tags, mask bits, flags, counters and category eligibility across fine search, hierarchy, segment checks and endpoint validation; verify mixed chains and lifecycle transitions.
- [ ] **FOOT-04** Complete start/goal occupancy policy: inside self/target/other object, blocked/outside-map points, overlap and target removal. Establish clamping, perimeter selection and invalid-input preconditions at public callers.

## FINE — fine search

Evidence: [fine search](retail-pathfinding-search.md).

- [ ] **FINE-01** Extend complete searches to mixed dynamic-object lists and all masks/classes, including moving/stationary blockers, special-target exits and suppressed/self objects; compare node/queue/parent state and exact route.
- [ ] **FINE-02** Close remaining relaxation/termination branches under ties, reopenings, stale heap entries, budget limits and nearest-node fallback; cover interactions in full searches, beyond isolated slices.
- [ ] **FINE-03** Exercise node/heap storage growth, cap/exhaustion, free-list reuse and 16-bit stamp wrap across sequential searches; distinguish normal failure from caller-invalid inputs.
- [ ] **FINE-04** Compose search setup and result handling for same-cell, blocked start/goal, disconnected goal, insufficient budget and special-object completion across all footprint classes.

## ACC — adaptive search

Evidence: [adaptive search and reduced veto](retail-pathfinding-search.md#complete-adaptive-request-oracle).

- [ ] **ACC-01** Enumerate/test every side/corner expansion, level transition and size-dependent predicate across lanes, boundaries and special-marker cells; verify promotion and subdivision ordering.
- [ ] **ACC-02** Establish producer reachability of every classification/flag combination. Test reachable combinations; document rejection/preconditions for malformed synthetic states.
- [ ] **ACC-03** Carry the reduced size-2 east-boundary veto into a realizable map/request and complete mover scenario; measure fine fallback, retries and outcome, or prove why the synthetic state is unreachable.
- [ ] **ACC-04** Verify heuristic/cost/nearest-node behavior with ordinary and special edges, ties and budgets. Explain discrepancies with a shortest-path reference without changing retail behavior.
- [ ] **ACC-05** Close adaptive node/heap/index/stamp/capacity lifetime paths, including repeated requests and lane/size changes; compose resulting partial/failure states with the path consumer.

## NUM — numerical and random-state parity

Evidence: [motion](retail-pathfinding-movement.md), [spatial rounding witness](retail-pathfinding-separation.md#position-application-and-spatial-bounds).

- [ ] **NUM-01** Inventory relevant soft-float/integer arithmetic, conversion, floor, normalization, trig and square-root helpers; recover operand ABI, constants/init provenance and reachable ranges.
- [ ] **NUM-02** Replace approximate mathematical references with bit-faithful contracts where bits affect branching, cells, ordering or accumulated motion. Cover cancellation, signed zero, exact/adjacent thresholds, oblique vectors and large/small valid values.
- [ ] **NUM-03** Resolve negative/nonfinite/out-of-range inputs at producer boundaries: rejection, sanitization or actual propagation. Do not extrapolate from ordinary positive-input corpora.
- [ ] **NUM-04** Recover PRNG initialization, ownership, draw order and wrap for retries, overlap and other discovered consumers; compose interleaved entities and deterministic repeat runs.

## ROUTE — reconstruction, refinement and progression

Evidence: [routes](retail-pathfinding-routes.md).

- [ ] **ROUTE-01** Complete reconstruction/endpoint contracts for all classes/levels, oblique paths, partial/empty buffers and invalid starts; cover exact rounding and buffer growth/index limits.
- [ ] **ROUTE-02** Extend segment sampling, normalization, skipping and blocker collection to every footprint class and direction; verify endpoint inclusion, touching corners, candidate caps/order and obstructions changing between samples.
- [ ] **ROUTE-03** Compose fresh adaptive search → fine refill → dynamic obstruction/yield → retry/replan → motion across multiple ticks; preserve exact return codes, indices, destinations, timestamps and charged work.
- [ ] **ROUTE-04** Exercise exhausted/cached/disabled route combinations and alternate index-initialization modes, queued paths, forced arrival and target-perimeter exits through public callers.
- [ ] **ROUTE-05** Map blocker identity/cooldown lifecycle, asymmetric yielding and group-bit-8 producers; test chains/cycles of yielding, blocker removal/replacement and actual delay duration in simulation updates.

## TARGET — pursuit, arrival and visibility

Evidence: [target state](retail-pathfinding-routes.md#destination-changes-and-replan-gating).

- [ ] **TARGET-01** Trace all arrival-range/heading producers, especially point orders versus target orders and ability-specific approach. Verify boundary equality, force flags and stop parameters through gameplay callers.
- [ ] **TARGET-02** Compose continuous/sub-cell target edits, speed changes, teleports, target growth, death/removal and handle reuse; verify refresh cadence, cached destination and replan/admission interaction.
- [ ] **TARGET-03** Close every visibility-loss policy and its flag/global producers, including retained pursuit, cancellation, reacquisition and validation results `0xa9/0xaa`; distinguish fog, invisibility and target invalidity.
- [ ] **TARGET-04** Verify refresh threshold/extra-delay producers, including Captain AI, and live long-count/multi-member retry policies; compose range changes and retries through completion/failure events.

## SCHED — clocks, work admission and owner tick

Evidence: [admission](retail-pathfinding-routes.md), [movement clocks](retail-pathfinding-movement.md#stored-velocity-integration-and-movement-clocks).

- [ ] **SCHED-01** Recover both clock-domain selectors, configured spans, advancement, pause/time scaling, rollover/backward-time semantics and simulation seconds per pathing update; distinguish render, JASS timer and simulation cadence.
- [ ] **SCHED-02** Execute a populated complete owner tick, recovering order of scheduler, shared-cap publication, group/radius passes, movement, separation and callbacks; verify iteration order and same-tick visibility of changes.
- [ ] **SCHED-03** Close every queue/class/priority producer, including non-unit class 15; test enqueue/unlink/reclassify/requeue/deletion during iteration and counter wrap.
- [ ] **SCHED-04** Measure contention across owners/classes/groups and repeated budget exhaustion; explain fairness/starvation and retry timing with exact queue/work state, not elapsed-time guesses.

## MOVE — velocity, stepping and spatial callbacks

Evidence: [movement](retail-pathfinding-movement.md#speed-and-heading-update).

- [ ] **MOVE-01** Complete authored/runtime speed, acceleration/increment, turn-cap and movement-angle producers, including temporary modifiers and all group/request writes; establish valid ranges and clamp order.
- [ ] **MOVE-02** Compose general-input turning, speed commit and old-velocity integration over long trajectories; test stationary turns, speed/heading changes, stopping and boundary crossings with NUM parity.
- [ ] **MOVE-03** Execute nonzero-elapsed region entry/exit callbacks, support-height/position handoff and mixed-object occupancy updates; verify ordering and reentrant movement/teleport effects.
- [ ] **MOVE-04** Audit pathing disable/enable, pause/resume, forced displacement, teleport and movement-mode changes for bypass/invalidation semantics; add only paths shown reachable by BASE.

## ORDER — dispatch, completion and lifetime

Evidence: [subscriptions](retail-pathfinding-movement.md#movement-subscriptions-and-internal-event-remapping), [queue/release](retail-pathfinding-movement.md#arrival-cleanup-and-internal-order-queue).

- [ ] **ORDER-01** Extend subscriber prefixes through real arrival/can't-path handlers and dispatcher unwind; cover all early exits, retry/recovery branches and unit-state gates, including queue cleanup bit `unit+280 & 40`.
- [ ] **ORDER-02** Recover internal queue versus user Shift-queue ownership; execute next-order dispatch (`6f67df00`), control bits and completion/failure semantics for empty, replaced, canceled and multi-order queues.
- [ ] **ORDER-03** Exercise subscription insertion/removal and order/unit destruction during callbacks, nested dispatch and multiple subscribers; verify payload remapping, iterator/depth/refcount invariants and event order.
- [ ] **ORDER-04** Finish zero-reference payload reclamation through class factory/allocator; test populated relations/children, live wrapper construction, bridge guard failures, stale handles and both identity domains.
- [ ] **ORDER-05** Complete deferred-request allocation, nonempty heap ordering/ties, cancellation, repeating requests, callbacks scheduling/canceling other requests and wrapper reuse; verify deadline time versus restored clock.
- [ ] **ORDER-06** Compose Stop, replacement, interruption, death/removal and relevant ability transitions during every routing phase, including waiting, searching, turning, group completion and deferred release.

## GROUP — shared routes and membership

Evidence: [group decisions](retail-pathfinding-movement.md#group-decision-and-speed-commit).

- [ ] **GROUP-01** Recover group creation/join/leave/merge/split/destruction producers and limits; establish which player/JASS/AI orders create shared groups versus independent movers.
- [ ] **GROUP-02** Extend full cached ticks to fresh group/member searches, route failure, completion and populated owner scheduling; verify separate/shared route use and decision-before-commit invariants.
- [ ] **GROUP-03** Close target-speed adjustment (`group bit 800`), shared-cap exemptions and flag producers, maximum footprint, auxiliary publication/pool allocation and Captain AI lifecycle.
- [ ] **GROUP-04** Test membership mutation during callbacks/movement, stale identities, last-member completion, empty-group teardown and all-invalid prepasses; verify swap-removal effects on subsequent iteration/layout.

## FORM — formation layout and regrouping

Evidence: [formation layout](retail-pathfinding-movement.md#complete-formation-layout-composition).

- [ ] **FORM-01** Compose authored SLK rank/flags through live mover/group creation; identify group spacing flag `20` and other formation policy producers, without inferring meaning from consumers.
- [ ] **FORM-02** Extend complete layouts to mixed radii, moving/oblique members, equal sort keys and all reachable sizes/ranks; establish behavior/preconditions beyond the observed twelve-member table domain.
- [ ] **FORM-03** Compose layout refresh → adaptive offset query → interval classification/held member → all-member decisions → speed commit, including blockers and cached/fresh routes.
- [ ] **FORM-04** Verify every rebuild/regroup trigger and timeout in real simulation time; include moving targets, membership/size changes, failed routes and warp markers.
- [ ] **FORM-05** Capture actual selection-issued mixed-unit formations, independently issued controls, narrow passages and regrouping; match assignments, offsets, cap publication and trajectories.

## SEP — repulsion and spatial maintenance

Evidence: [separation](retail-pathfinding-separation.md).

- [ ] **SEP-01** Complete authored config, category/rank/mask/flag producers, including nonzero selectors and overrides; verify enabled/disabled behavior across applicable movement types and owners.
- [ ] **SEP-02** Execute the entire query → ordered accumulation → clamp/cooldown → later validation/application chain with actual occupancy updates and NUM/PRNG state; compare every neighbor contribution.
- [ ] **SEP-03** Close link insertion/removal order from unit lifecycle, metadata/dead-object lifetimes, stamp repair/wrap, cleanup sampling threshold/schedule, fresh allocation/growth and block reclamation.
- [ ] **SEP-04** Match controlled live exact-overlap, crowded, mixed-owner/radius/rank and blocked-displacement cases; distinguish ground path blocking/retry from authored repulsion and explain all trajectory differences.

## GATE — special-edge lifecycle

Evidence: [Way Gates](retail-pathfinding-experiments.md#special-edges-are-way-gate-records).

- [ ] **GATE-01** Complete activation/approach thresholds and numeric boundaries, mover eligibility, destination placement/rejection and blocked/unreachable exits through the real consumer.
- [ ] **GATE-02** Test disable/destroy/retarget during approach, fresh order after retarget, and disabled edge over otherwise impassable terrain; verify cached-record versus current-state behavior.
- [ ] **GATE-03** Compose multiple/chained/overlapping sources, active/inactive combinations and route ties; recover source overwrite/cleanup and hierarchy propagation order.
- [ ] **GATE-04** Test IDs 1..255 exhaustion, zero ID, deallocation/reuse and stale route references; compose group/regroup transitions and ordinary fine walking around failed/skipped edges.

## E2E — final evidence package and integration boundary

- [ ] **E2E-01** Build layered scenarios from BASE-06: static detour/disconnection → size/lane changes → dynamic blockers → pursuit → contention → cancellation/next order → formations/crowds → gates. Include cross-feature interactions selected from shared mutable state and branch dependencies.
- [ ] **E2E-02** Repeat controlled captures with equivalent minimal/no Frida hooks; compare script-level trajectory/order outcomes and timing to detect observer effects. Retain input manifests and successful completion markers. [Open-ground no-attach control](retail-pathfinding-experiments.md#controls-without-an-attached-observer) matches304 markers twice; blocked-goal305, replacement311, fog-reacquisition610 and building-lifecycle310 match once each. Crowds, other modes and further repeats remain open.
- [ ] **E2E-03** Run deterministic replay and seeded generated/boundary cases across supported modes; reduce every mismatch. Preserve counterexamples as regressions with an explanation of retail behavior.
- [ ] **E2E-04** Close long-run lifecycle cases: generation/stamp/counter wrap, handle/ID reuse, map reload/save-load, removal during callbacks and queue/pool pressure. Reachable behavior must match even when visually rare.
- [ ] **E2E-05** Audit remaining unknowns, unnamed behavior-affecting flags, prefixes, stubs, tolerances and excluded branches against BASE-03; require coverage or documented unreachability for each.
- [ ] **E2E-06** Freeze the implementation-neutral specification: structures/units, numeric rules, state machines, update/event ordering, map invalidation, ownership, PRNG, limits, result codes and failure behavior. Link each contract to runnable evidence.
- [ ] **E2E-07** Specify OpenRealm integration seams using its existing architecture: Move-owned routing/steering, server simulation clock/order dispatch, collision/world state, group ownership, save/load and cleanup. Map current entry points and replacement boundaries without implementing replacement behavior yet.
- [ ] **E2E-08** Define the differential adapter contract for original-code/reference fixtures and eventual OpenRealm implementation: identical scenario inputs, normalized identity mapping, exact state/event comparisons and trajectory checks. Keep expected results independent of replacement code.

## READY — gate to start the faithful replacement

- [ ] **READY-01** Every checklist item has closure evidence; all ledger rows have no unexplained behavior-affecting gaps within the inventoried scope.
- [ ] **READY-02** Full order-to-arrival/failure and cross-feature scenarios pass; known retail quirks and numerical boundaries are regression fixtures, not exceptions hidden by tolerances.
- [ ] **READY-03** The corpus reproduces from documented inputs/tools with build guards, deterministic seeds, capture controls and no unexplained differences.
- [ ] **READY-04** The specification and OpenRealm integration boundary are reviewable and sufficient to implement without guessing. Then begin replacement, adding each layer against the same corpus; claiming achieved parity still requires the resulting implementation to pass it.
