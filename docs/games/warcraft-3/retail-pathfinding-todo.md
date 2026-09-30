# Retail pathfinding: executable research backlog

Target: Warcraft III **1.27.1.7085**. [Behavior ledger][ledger] owns the
contracts and evidence limits; this file owns the work queue. Complete the
research before declaring the full faithful OpenRealm replacement. Independently
verified slices can be integrated earlier; the [scalar/turn integration](retail-pathfinding-engine.md)
has exact C/live evidence without closing the whole-replacement READY gates.

## Progress

**45 done / 217 tasks; 172 remaining.** Counts describe this backlog,
not a percentage of retail fidelity or an estimate of remaining effort.
The rewrite splits the old 81 acceptance items into independently closable
leaves. Parent IDs remain for traceability; only numbered leaves are checkboxes.
Completed evidence now sits next to its specific remaining extension.

| Area | Done | Remaining |
| --- | ---: | ---: |
| BASE — Baseline and reproducibility | 10 | 10 |
| MAP — Map construction and lifetime | 3 | 16 |
| FOOT — Footprints and query policy | 1 | 8 |
| FINE — Fine search | 1 | 8 |
| ACC — Adaptive search | 1 | 10 |
| NUM — Numbers and random state | 5 | 6 |
| ROUTE — Route progression and yielding | 0 | 10 |
| TARGET — Pursuit and arrival policy | 1 | 9 |
| SCHED — Scheduling and owner updates | 2 | 9 |
| MOVE — Stepping and callbacks | 0 | 11 |
| ORDER — Orders and reclamation | 5 | 16 |
| GROUP — Shared movement groups | 10 | 7 |
| FORM — Formation and regrouping | 2 | 10 |
| SEP — Repulsion and spatial records | 2 | 10 |
| GATE — Way Gates | 1 | 10 |
| E2E — Combined scenarios and handoff | 1 | 18 |
| READY — Start the faithful replacement | 0 | 4 |

Update these counts when checking, adding or splitting a task. Report progress
as **IDs closed + artifact + next runnable ID**, not additional raw test counts.
The denominator changes only when a new task is explicitly added or split.
NUM-02.4 was added explicitly to own the previously unassigned committed-facing
exclusion of NUM-02.2; it is closed independently of whole-trajectory NUM-02.3.
GROUP-04.1's callback removal/reuse and refreshed survivor requirement is split
into GROUP-04.1/03/04: callback pruning, generation-safe reuse, and the refreshed
survivor journey. GROUP-04.4 is now explicitly split further into completed-member
survivor travel (04.4), actual refresh/new destinations and reuse composition (04.5),
and full engine group storage/phase integration (04.6). All leaves are required
to satisfy the original combined acceptance item. GROUP-04.7 explicitly adds the
owned-path factory/accounting prerequisite discovered during04.5; it does not
replace the remaining survivor refresh/reuse acceptance. GROUP-04.8 explicitly
splits04.5's complete released/reused-member survivor journey from its actual
refresh/new-destination producer requirement; both leaves remain required.

## Work next

Start with **GROUP-04.5**. GROUP-04.2/04 now cover original callback completion,
mixed-speed survivor arrival on open/wall routes, stable reserved offsets and
all-invalid empty teardown retaining individual mover/path identities.04.8
adds completed-member destruction/reallocation and full survivor cleanup. Move
integrates active cohort speed with independent identity, wrap/reuse and
save/load tests. Removal alone does not refresh the original fixed-goal layout;
04.5 must compose the actual refresh producer and new destinations. Full retail
member storage, flags, shared overrides, queued membership and decision/commit
phases remain04.6. Ghidra persists254 names,14 layouts,90 fields and29 x86
prototypes. The strict corpus has118 declared outcomes, including preserved
native differences and rejected archives. Supplied existing Unit/Move backing
and class notification traversal still retain BASE-03.1.

| Order | Task | Starts from | Finish artifact |
| --- | --- | --- | --- |
| 1 | GROUP-04.5 | GROUP-04.1/03/04 | Actual refresh/new survivor destinations after mutation and reclaimed mover reuse |
| 2 | NUM-01.2 | Verified scalar/heading slices | Remaining trig/conversion ABI, constants and public input-domain inventory |
| 3 | MAP-03.4 | Existing widget admission | Footprint refresh and complete escape-order arrival/failure |

MAP-03.4 (widget escape to arrival/failure) also has existing fixtures. Use
the per-area dependencies and finish one bounded task before starting another.

## What counts as done

A leaf is one experiment, one finite case matrix, or one reviewable artifact.
Its sentence names the fixture/input and observable result required for closure.
Section-level tools and evidence links are the starting point, not extra tasks.
References to a parent ID mean its listed prerequisite leaves must be complete.
Later sections extend BASE-06.5 when they require a complete movement scenario;
isolated helper, inventory and format tasks do not wait for that baseline.

For each closure, add the exact command/fixture, expected versus observed result,
report path, evidence level (S/O/C/L), and remaining exclusions to the linked
evidence document. Mark the leaf `[x]` and update the ledger and counts in the
same change. A proved-unreachable branch may close with its producer proof.
Do not close from an unexplained mismatch, arbitrary tolerance, incomplete call
or a passing prefix of the requested lifecycle.

If a leaf uncovers several independent problems, split it into explicit new
IDs before continuing. Keep the completed part closed; do not silently enlarge
its acceptance criteria. Record blocked tasks with a concrete prerequisite ID.
Do not reopen verified work merely because a broader sibling remains open.
No task may weaken the fidelity gate by hiding a reachable behavior gap.

## Evidence and execution

Checked items carry forward the linked evidence's scope, not a new claim that
all historical experiments were rerun. The latest motion/order compositions
were verified at `b90aa33d`; reports under the [documented report root][ledger]
are `coverage-audit-b90aa33d/motion-oracle.json` (**M**) and
`coverage-audit-b90aa33d/order-tasks-oracle.json` (**O**). Binary/CRT hashes and
[reproduction commands](retail-pathfinding.md#reproduction) remain mandatory.
Other checked mechanisms cite their existing evidence sections and corpora.

Tool suffixes below mean `tools/ghidra/verify_wc3_pathing_<suffix>.py`.
Captures use `tools/frida/trace_wc3_pathfinding.py`,
`control_wc3_pathfinding.py` and `analyze_pathfinding_trace.py`. Extend an
existing fixture where possible; name a new artifact by task ID. An offline
assertion is sufficient unless that task specifically needs a live witness.

When workers are explicitly assigned, give each exact task IDs, owned files and
unique report paths. Only one worker edits a given oracle at a time. Serialize
Ghidra mutations and live retail controls. Integrate one passing result before
assigning its dependent task; worker availability does not change dependencies.

## BASE — Baseline and reproducibility

Evidence: [movement][M] and [live experiments][L]. Tools/artifacts: order_tasks, motion; Frida controller/analyzer.

### BASE-01 — Movement entry points

- [ ] **BASE-01.1** Trace one player point order from UI/network admission to 680320; record actual command fields, flags and caller ABI.
- [ ] **BASE-01.2** Trace one JASS point order and one AI point order to their movement entry; publish whether they share the player path.
- [ ] **BASE-01.3** Trace one target order and one ability approach; record the differences in range, target identity and routing flags.
- [ ] **BASE-01.4** List forced-position, teleport and pathing-bypass entry points with callers; assign a separate follow-up ID to each uncovered path.

### BASE-02 — Supported input inventory

- [ ] **BASE-02.1** Build a movement-type table for ground, air, water and amphibious units: authored producer, lane, masks and support surface.
- [ ] **BASE-02.2** Build an object-category table for units, buildings, destructibles and targets: tags, ownership and eligibility at each query consumer.
- [ ] **BASE-02.3** Record valid coordinate, radius and map-size domains from public producers; attach rejection or propagation evidence for boundary inputs.

### BASE-03 — Coverage inventory

- [ ] **BASE-03.1** Export a reachable function/branch inventory from the entry points identified in BASE-01; link each known branch to its evidence or task ID.
- [ ] **BASE-03.2** Publish a field read/write inventory for map, path, mover, group and order state, including virtual callbacks and globals.
- [ ] **BASE-03.3** Assign every current oracle exclusion to one remaining task; list any unassigned exclusion as a new task before finishing this audit.

### BASE-04 — Shared scenario format

- [x] **BASE-04.1** Define a versioned scenario manifest with build/data hashes, map, entities, handles, clock, seed, commands and expected termination; encode the existing FIFO case. Evidence: [frozen producer baseline](retail-pathfinding-movement.md#producer-built-frozen-baseline), version1 `retail-owner-baseline-1.27.json`.
- [x] **BASE-04.2** Define normalized snapshots for cells, route indices, budgets, membership, motion and events; encode one tick from each existing motion/order report. Same evidence; frozen complete order states and `retail-motion-snapshot-1.27.json`; missing historical observations stay null.

### BASE-05 — Corpus runner

- [x] **BASE-05.1** Inventory existing oracles/captures in one manifest with command, inputs, report, expected status and evidence level; include intentional adaptive mismatches. Evidence: [corpus inventory](retail-pathfinding-corpus.md#inventory-and-acceptance), version1 `retail-pathfinding-corpus-1.27.json`; all32 oracle scripts,12 variants,61 archives and six stronger live-input replays. Historical source/map provenance limits remain explicit.
- [x] **BASE-05.2** Add a runner that executes that manifest and fails on a missing report, truncated capture, hash mismatch or unexpected exit/result. Same evidence; `run_wc3_pathfinding_corpus.py` requires fresh outputs and explicit report contracts. Asset-free rejection regressions cover changed/stale/missing inputs and reports; known reference differences cannot become passes.
- [x] **BASE-05.3** Run the manifest from a fresh output directory; record per-case status and reproducible commands without relying on stale reports. Evidence: [fresh checkpoint](retail-pathfinding-corpus.md#fresh-checkpoint), `base-05.3-fresh-provenance-corpus-20260929/corpus-results.json`: all111 declared outcomes reproduced, including two native adaptive differences, two counterfactual controls and seven rejected archives. Original-code, C and archived-live scope remain separate; no new live capture or whole-replacement claim.

### BASE-06 — One frozen ordinary-move baseline

- [x] **BASE-06.1** Initial unit admission: 34 original 680320 admissions pass predicted task-chain and queue assertions without mid-call provisioning. Evidence: [initial admission][admission], report O `complete_initial_admissions=34`; controlled point commands and seeded pools only.
- [x] **BASE-06.2** One admitted point order uses original owner updates through arrival/release; route, raw trajectory, arrival tick and reclamation equal the direct-group control. Evidence: [joined owner baseline](retail-pathfinding-movement.md#initial-admission-through-owner-updates), report `base-06.3-owner-fifo.json`, `complete_owner_admissions=1`. Seeded map/mover storage remains BASE-06.4.
- [x] **BASE-06.3** Four queued-successor cases run only owner updates through both natural arrivals and final idle; both admissions, FIFO targets and all queue/group/path/payload ownership checked. Same evidence/report, `complete_owner_fifo_cases=4`; successor first tick follows owner cadence instead of explicit same-time group calls.
- [x] **BASE-06.4** After BASE-06.3, construct that fixture's map, mover, group and order through identified original producers; enumerate any remaining seeded storage/class-cache boundary and give it a task ID. Same evidence; original no-file loader/mover construction and setters, report `base-06.4-producer-baseline.json`; allocations/caches/terrain/clock boundaries assigned explicitly.
- [x] **BASE-06.5** After BASE-06.4 and BASE-04, freeze the baseline manifest and expected intermediate states; repeat twice and assert identical normalized output and initial/final idle invariants. Same evidence; report `base-06.5-frozen-baseline.json`, two scenarios × two runs,42 snapshots and exact output digests. No full-world or RNG claim.

## MAP — Map construction and lifetime

Evidence: [search/map evidence][S]. Tools/artifacts: map_construction, load_masks, widget_masks, maps.

### MAP-01 — Map coordinates

- [x] **MAP-01.1** Terrain-origin producer and 25 no-file map loads are covered. Evidence: [map construction][map-load]; file-backed loading and non-dyadic inputs are excluded.
- [ ] **MAP-01.2** Sweep negative origins and non-power-of-two dimensions through coordinate conversion; assert fine/proximity/adaptive padding and clipping.
- [ ] **MAP-01.3** Test each map corner at below/equal/above boundary coordinates, including non-dyadic values; record exact accepted cells and conversions.

### MAP-02 — Initial loading

- [ ] **MAP-02.1** Load one file-backed WPM/map through deserialization and map creation; compare decoded masks, fine cells and hierarchy against the no-file fixture.
- [ ] **MAP-02.2** Add one cliff, one water boundary and one bridge fixture; assert each supported movement lane's initial cells and support-height source.
- [ ] **MAP-02.3** Load two overlapping authored pathing textures in both creation orders; assert object/fine/hierarchy state after loading.

### MAP-03 — Invalidation producers

- [x] **MAP-03.1** Terrain edit/rebuild/reversal corpus passes 11,664 edits, 216 compositions and 30 clipped updates. Evidence: [terrain edits][map-edits]; other invalidation producers remain separate tasks.
- [x] **MAP-03.2** Widget rasterization covers 144 overlapping sequences and 96 paired reapply/remove lifecycles. Evidence: [widget lifecycle][widgets]; file loading, growth and full gameplay travel remain excluded.
- [ ] **MAP-03.3** List spawn, movement, size/pathing changes, construction and removal producers with affected grids and update timing; assign uncovered producers separate IDs.
- [ ] **MAP-03.4** Continue one widget-produced escape order from accepted admission to arrival/failure; assert footprint refresh and route state throughout.
- [ ] **MAP-03.5** Exercise a resource depletion/removal lifecycle; assert footprint and hierarchy changes before the next request.
- [ ] **MAP-03.6** Exercise destructible destruction and cache invalidation through the final free; assert the next route no longer sees the dead blocker.

### MAP-04 — Temporary exclusions

- [ ] **MAP-04.1** Nest self and target exclusions over two overlapping objects and terrain; restore in reverse order and compare every affected cell/count.
- [ ] **MAP-04.2** Enumerate early/failure/reentrant exits from exclusion scopes; add one restoration assertion per reachable exit and one edit-during-request case.

### MAP-05 — Map/spatial capacity

- [ ] **MAP-05.1** Cross one map/spatial allocation boundary, then free and reuse the storage; assert record identity, links and cell contents.
- [ ] **MAP-05.2** Force a generation/stamp wrap at its original mutation point; compare the first post-wrap query with a clean equivalent map.
- [ ] **MAP-05.3** Trigger reachable allocation failure and metadata/dead-record cleanup thresholds; assert failure result and no surviving partial links.

### MAP-06 — Map lifetime

- [ ] **MAP-06.1** Destroy and reload a map with a live mover; record which routes, grids and handles are cleared or rebuilt and assert first subsequent movement.
- [ ] **MAP-06.2** Save/load during an active route; determine retained versus rebuilt path state and compare resumed movement with the uninterrupted control.

## FOOT — Footprints and query policy

Evidence: [footprint evidence][S]. Tools/artifacts: footprints, cells, blockers, grid.

### FOOT-01 — Radius production

- [x] **FOOT-01.1** Authored collision conversion and the four fine-class thresholds are recovered and covered by the footprint oracle. Evidence: [footprints][footprints]; runtime producer coverage is not complete.
- [ ] **FOOT-01.2** Change collision radius through a runtime producer; test below/equal/above each class boundary and assert geometry/class changes.
- [ ] **FOOT-01.3** Trace group maximum and target-radius producers; assert updates after the largest member/target changes size or disappears.

### FOOT-02 — Passage matrix

- [ ] **FOOT-02.1** Run cardinal corridors at width below/equal/above footprint diameter, across four classes, lanes and sub-cell offsets; compare accepted cells and route.
- [ ] **FOOT-02.2** Extend that matrix to diagonal corners, touching footprints and map edges; retain one minimized counterexample per distinct mismatch.

### FOOT-03 — Object query eligibility

- [ ] **FOOT-03.1** Build a tag/mask/flag/count truth table for fine search, hierarchy, segment checks and endpoint validation using BASE-02 object categories.
- [ ] **FOOT-03.2** Put two different eligible categories in one cell, then remove each in turn; assert query results and remaining reference counts at all four consumers.

### FOOT-04 — Start and goal policy

- [ ] **FOOT-04.1** Test start inside self, target and unrelated blocker; assert the public caller's clamping, exclusion and first accepted route point.
- [ ] **FOOT-04.2** Test blocked/outside/overlapping goals and target removal; assert perimeter choice, rejection or fallback with original result codes.

## FINE — Fine search

Evidence: [fine-search evidence][S]. Tools/artifacts: queue, search, grid.

### FINE-01 — Full searches with objects

- [x] **FINE-01.1** 288 complete static searches plus repeat/stamp reuse pass with exact route/state expectations. Evidence: [static fine searches][fine-static]; mixed dynamic objects remain excluded.
- [ ] **FINE-01.2** Add stationary and moving object chains to a full search for each lane/class; compare queue, parents, termination and route.
- [ ] **FINE-01.3** Add self/suppressed objects and target-exit cases to the same matrix; assert eligibility changes rather than only reachability.

### FINE-02 — Search termination

- [ ] **FINE-02.1** Compose equal-cost ties, reopenings and stale heap entries in one full request; compare pop order, generations and charged work.
- [ ] **FINE-02.2** Force budget exhaustion and nearest-node fallback around the final pop boundary; assert result, chosen node and reconstructed partial path.

### FINE-03 — Fine storage lifetime

- [ ] **FINE-03.1** Cross node and heap growth/capacity boundaries; verify original failure codes and free-list recovery on the next request.
- [ ] **FINE-03.2** Run sequential searches through 16-bit stamp wrap and reuse; compare post-wrap route and node state with a clean control.

### FINE-04 — Public fine results

- [ ] **FINE-04.1** For each footprint class, run same-cell, blocked-start and blocked-goal requests through public setup and result consumption; assert caller-visible outcomes.
- [ ] **FINE-04.2** Run disconnected-goal, insufficient-budget and special-object completion through those same callers; assert partial/failure handling and cleanup.

## ACC — Adaptive search

Evidence: [adaptive evidence][S]. Tools/artifacts: adaptive, routes; reduced fixture under tools/ghidra/fixtures/.

### ACC-01 — Adaptive expansion

- [ ] **ACC-01.1** Enumerate side/corner and level-transition branches from the adaptive expander; record exact input preconditions for each branch.
- [ ] **ACC-01.2** Build one witness per enumerated branch across lanes/classes and special-marker cells; assert promotion/subdivision and neighbor ordering.

### ACC-02 — Classification reachability

- [ ] **ACC-02.1** Map classification/flag combinations used by adaptive fixtures back to map producers; classify each as reachable, rejected or unresolved.
- [ ] **ACC-02.2** For each unresolved combination, provide a producer-built witness or a documented rejection proof; retain separate IDs if further work is discovered.

### ACC-03 — Size-2 east-boundary veto

- [x] **ACC-03.1** The synthetic size-2 east-boundary veto is reduced and causally isolated. Evidence: [adaptive veto][adaptive-veto]; gameplay reachability remains unproven.
- [ ] **ACC-03.2** Construct the reduced veto using real map/request producers, or prove its classification cannot be produced within scope.
- [ ] **ACC-03.3** If reachable, run that mover through fallback/retry to arrival or failure; preserve the resulting retail route/outcome as a regression.

### ACC-04 — Adaptive costs

- [ ] **ACC-04.1** Compare heuristic, total cost and nearest-node selection with ordinary edges under ties and budget exhaustion; explain each shortest-path difference.
- [ ] **ACC-04.2** Repeat with an active special edge; assert edge cost, parent chain, tie ordering and partial result without assuming optimality.

### ACC-05 — Adaptive storage lifetime

- [ ] **ACC-05.1** Cross adaptive node/heap/index capacity and growth boundaries; assert failure/partial state and its public consumer result.
- [ ] **ACC-05.2** Reuse storage across stamp wrap and lane/class changes; assert no stale node, route or flag survives into the next request.

## NUM — Numbers and random state

Evidence: [numeric evidence][P] and [motion][M]. Tools/artifacts: numeric, speed, range, motion, separation.

### NUM-01 — Arithmetic inventory

- [x] **NUM-01.1** 200,330 exact scalar calls, 2,130 normalizations and 864 bounds prefixes are recorded. Evidence: [scalar arithmetic][numeric]; trig and general trajectories remain open.
- [ ] **NUM-01.2** Inventory the remaining trig/conversion helpers with operand ABI, constant initialization and public input domains; link the already verified scalar helpers.
- [x] **NUM-01.3** Independent integer formula regenerates all 1,025 embedded reciprocal entries exactly; Ghidra references identify a static table consumed by0711e0, with no runtime producer. Same generator also reproduces all 1,025 sine entries. Evidence: [generated tables](retail-pathfinding-engine.md#generated-tables-and-exact-trigonometry), report `scalar-trig-engine-exact.json`; historical build-time source is unavailable.

### NUM-02 — Branch-sensitive arithmetic

- [x] **NUM-02.1** Sine/cosine and acos compare exact output words across cardinal/oblique inputs and adjacent lookup thresholds; independently generated consumed tables match retail. Signed-zero, cancellation and opposite-heading signs are retained. Evidence: [exact vector headings](retail-pathfinding-engine.md#exact-vector-headings), reports `scalar-trig-engine-exact.json`, `acos-engine-exact.json`, `heading-chain-engine-exact.json`; 1,287 composed heading errors plus 183 live errors. Remaining helper inventory belongs to NUM-01.2, whole trajectory to NUM-02.3.
- [x] **NUM-02.2** Original 80 velocity commits and 2,384 position integrations now compare exact C output words; fresh live turn capture compares all 192 velocity/position commits, including stopping. Evidence: [exact velocity integration](retail-pathfinding-engine.md#velocity-and-position-integration), reports `velocity-integration-engine-exact.json` and `runtime/velocity-turn-exact.json`. Extension: `world-velocity-engine-exact.json` and `retail-world-velocity-1.27.json` freeze1,040 complete original/world-adapted commits and3,344 integrations, including adjacent tiny-speed guards. Move converts velocity inputs to fine-grid units before its cutoff. Committed facing is closed separately by NUM-02.4; whole-engine cadence remains excluded.
- [ ] **NUM-02.3** Extend that exact case to a fixed long oblique trajectory at small/large valid values; compare every committed position and cell crossing.
- [x] **NUM-02.4** Verify committed facing from resulting velocity, its tiny-speed guard/equality and stopped-heading remainder normalization; compare original/C/live words. Evidence: [committed facing](retail-pathfinding-engine.md#committed-facing-and-remainder-arithmetic), reports `fraction-modulo-engine-exact.json`, `facing-chain-engine-exact.json`, `facing-stock-turn-fresh-exact.json`;810 heading guards,44 angle boundaries,140 full commits and184 fresh live commits. Accepted-step engine regressions cover oblique facing and fine-grid scale; original full owner cadence/steering state remains open.

### NUM-03 — Exceptional numeric inputs

- [ ] **NUM-03.1** Drive negative and out-of-range coordinates/radii through public producers; record rejection, sanitization or propagated bit pattern.
- [ ] **NUM-03.2** Do the same for nonfinite values; document producer unreachability where demonstrated instead of treating synthetic helper calls as gameplay evidence.

### NUM-04 — Random state

- [ ] **NUM-04.1** Trace seed ownership, initialization and draws for overlap and retry consumers; publish a draw-order contract with wrap behavior.
- [ ] **NUM-04.2** Interleave two entities' overlap/retry events under a fixed seed; repeat and assert identical draws, state and resulting movement.

## ROUTE — Route progression and yielding

Evidence: [route evidence][R]. Tools/artifacts: routes, refill, segment, yield, transition.

### ROUTE-01 — Reconstruction

- [ ] **ROUTE-01.1** Extend fine/coarse endpoint reconstruction to oblique directions and every class; assert exact coordinates, rounding and route order.
- [ ] **ROUTE-01.2** Exercise empty/partial buffers, invalid starts and one growth/index limit; assert return code and next public advance state.

### ROUTE-02 — Segment checks

- [ ] **ROUTE-02.1** Sweep segment direction and length across footprint classes, with endpoints touching corners; assert sampled cells and endpoint inclusion.
- [ ] **ROUTE-02.2** Hit blocker candidate capacity with ordered objects, then change one obstruction between samples; assert cap/order and the resulting waypoint choice.

### ROUTE-03 — Dynamic route composition

- [ ] **ROUTE-03.1** After BASE-06.5, insert a blocker during adaptive-to-fine travel; assert refill indices, yield result, timestamps and charged work.
- [ ] **ROUTE-03.2** Remove that blocker while waiting; assert retry/replan timing and eventual arrival or can't-path event through the full owner tick.

### ROUTE-04 — Route mode combinations

- [ ] **ROUTE-04.1** Create a truth table for cached/exhausted/disabled routes and alternate index initialization; cover every reachable combination through public advance.
- [ ] **ROUTE-04.2** Exercise queued paths, forced arrival and target-perimeter exit against that table; assert destination, event and retained route state.

### ROUTE-05 — Yielding lifecycle

- [ ] **ROUTE-05.1** Trace blocker identity and group-bit-8 producers; assert delay duration after blocker removal and replacement by a reused handle.
- [ ] **ROUTE-05.2** Run a two-mover asymmetric yield and a three-mover yield cycle; compare countdown, release order and eventual progress/failure.

## TARGET — Pursuit and arrival policy

Evidence: [target evidence][R] and [range][M]. Tools/artifacts: target, refresh, replan, range.

### TARGET-01 — Arrival inputs

- [x] **TARGET-01.1** Point-task range has 4,957 exact cases; object range has 948 calls and six invalid-handle probes. Evidence: [range predicates][ranges]; this does not close their gameplay producers.
- [ ] **TARGET-01.2** Trace point/target order range, heading, force and stop parameters from actual commands; test equality and adjacent boundary values.
- [ ] **TARGET-01.3** Trace one ability-specific approach producer and contrast its range/stop contract with those commands.

### TARGET-02 — Target mutations

- [ ] **TARGET-02.1** Move a target continuously by sub-cell steps and change speed; assert cached destination, refresh cadence and admission/replan timing per tick.
- [ ] **TARGET-02.2** Teleport or resize a target mid-route; assert the next accepted destination and updated range/footprint.
- [ ] **TARGET-02.3** Kill/remove and then reuse the target handle; assert cancellation/revalidation without adopting the replacement entity.

### TARGET-03 — Visibility policies

- [ ] **TARGET-03.1** Map visibility policy flags/global producers to fog, invisibility and validation results 0xa9/0xaa; publish the reachable branch table.
- [ ] **TARGET-03.2** Run loss and reacquisition for each listed policy; assert retained pursuit or cancellation and resulting order/route state.

### TARGET-04 — Delayed refresh

- [ ] **TARGET-04.1** Trace refresh-threshold and Captain AI extra-delay producers; assert actual simulation ticks to the next request.
- [ ] **TARGET-04.2** Run a long-count retry with multiple members and a range change; assert per-member retry/completion/failure events.

## SCHED — Scheduling and owner updates

Evidence: [movement evidence][M] and [routes][R]. Tools/artifacts: scheduler, motion, order_tasks.

### SCHED-01 — Clock domains

- [ ] **SCHED-01.1** Trace both clock selectors and configured spans to simulation time; assert pause, scaling and ordinary advancement against a fixed event timeline.
- [ ] **SCHED-01.2** Cross clock rollover and a reachable backward-time transition; assert request deadlines, integration and admission behavior.

### SCHED-02 — Owner pass ordering

- [x] **SCHED-02.1** Singleton wall trajectory: 44 complete owner updates, all 64 scheduler buckets, visual settling and unlink pass. Evidence: [singleton owner][owner], report M `move_owner_arrival_cases=1`; shared/separation lists empty.
- [x] **SCHED-02.2** Active singleton plus eligible repulsor: 43 separation updates and four accepted attempts pass. Evidence: [separation pair][pair], M `move_owner_active_separation_cases=1`; controlled profile, bounded numeric tolerance.
- [ ] **SCHED-02.3** Populate two groups and the shared-cap/radius lists in one owner tick; assert scheduler/publication/group/movement/separation order and same-tick visibility.
- [ ] **SCHED-02.4** Mutate membership or remove a mover from one callback during that tick; assert subsequent iteration order and ownership.

### SCHED-03 — Admission queues

- [ ] **SCHED-03.1** Trace class/priority producers, including non-unit class 15; record which runtime object can enqueue into each policy bucket.
- [ ] **SCHED-03.2** Reclassify/requeue and delete a request during queue traversal; assert head/tail/count and next admitted request.
- [ ] **SCHED-03.3** Cross the scheduler work-counter wrap; compare charged work and admission to an equivalent clean-counter run.

### SCHED-04 — Contention

- [ ] **SCHED-04.1** Run two owners/classes competing for a fixed exhausted budget; record exact admission order, work and waiting duration.
- [ ] **SCHED-04.2** Run repeated exhaustion with multiple groups; establish fairness/starvation behavior from queue state over a fixed-length trace.

## MOVE — Stepping and callbacks

Evidence: [movement evidence][M]. Tools/artifacts: motion, speed, numeric.

### MOVE-01 — Movement parameters

- [ ] **MOVE-01.1** Trace authored speed, acceleration and turn/movement-angle data into a new mover; assert converted values and clamp order.
- [ ] **MOVE-01.2** Apply then remove a temporary speed/turn modifier during travel; assert committed velocity and restoration.
- [ ] **MOVE-01.3** Record group/request writes to those parameters and test each reachable overwrite order against the authored defaults.

### MOVE-02 — Stepping

- [ ] **MOVE-02.1** After NUM-02, compare a long oblique trajectory with speed and heading changes at fixed ticks; assert old-velocity integration and exact positions.
- [ ] **MOVE-02.2** Exercise stationary turn, Stop and restart at a cell boundary; assert facing, zero velocity and occupancy before/after each event.

### MOVE-03 — Spatial and presentation callbacks

- [ ] **MOVE-03.1** Cross a region boundary with nonzero elapsed time; assert enter/exit callback order relative to occupancy and support-height publication.
- [ ] **MOVE-03.2** Teleport or remove the mover from a region callback; assert no stale post-callback position/occupancy commit.
- [ ] **MOVE-03.3** Travel over one bridge/water support transition with nonzero UI limits; assert support source, height and clamped presentation transform.

### MOVE-04 — Movement bypasses

- [ ] **MOVE-04.1** Disable then enable pathing during travel; assert route/occupancy invalidation and the first resumed step.
- [ ] **MOVE-04.2** Pause/resume and force-displace a mover; assert clock, velocity and route retention or reset.
- [ ] **MOVE-04.3** Teleport and switch movement mode via producers inventoried in BASE-01; assert grids/lanes and next request. Split additional producer paths into new IDs.

## ORDER — Orders and reclamation

Evidence: [order evidence][M]. Tools/artifacts: order_tasks, arrival, lifetime.

### ORDER-01 — Arrival and failure dispatch

- [x] **ORDER-01.1** 24 generated point-order chains arrive and reclaim queues/pools; 288 internal tasks complete. Evidence: [queued arrival][arrival], O `queued_order_arrival_cases=24`; open fine grid and explicit group ticks.
- [ ] **ORDER-01.2** Enumerate remaining arrival/can't-path early exits and unit-state gates; add one full-dispatch witness per branch, including unit+280 bit40.
- [ ] **ORDER-01.3** Run one blocked-goal recovery chain through retries and final failure/next-order dispatch; assert unwind and cleanup rather than only notification.

### ORDER-02 — User/internal queues

- [x] **ORDER-02.1** Four two-order FIFO cases pass identity, successor timing, callback and final recovery assertions. Evidence: [FIFO][fifo], O `fifo_two_order_cases=4`; controlled command inputs.
- [ ] **ORDER-02.2** Map user Shift-queue versus internal task ownership and remaining queue control bits to producer/caller contracts.
- [ ] **ORDER-02.3** Exercise empty queue, rejected successor and canceled pending order through those controls; assert head/tail/count, dispatch result and release.

### ORDER-03 — Callback mutation

- [ ] **ORDER-03.1** Insert/remove a subscription while dispatching to multiple subscribers; assert delivery order, iterator and reference counts.
- [ ] **ORDER-03.2** Destroy an order or unit from a subscriber, then perform nested dispatch; assert depth/unwind and payload lifetime with no stale callback.

### ORDER-04 — Reference reclamation

- [x] **ORDER-04.1** Eight last-reference release cycles and six factory reuses pass. Evidence: [payload reclamation][reclamation]; preallocated pools and supplied registration, no populated relations/negative domain.
- [ ] **ORDER-04.2** Construct and release an object with populated relations/children through the real factory; assert child/reference cleanup and free-list recovery.
- [ ] **ORDER-04.3** Exercise bridge guard failure, stale identity and both handle domains; assert rejection and reference balance.
- [ ] **ORDER-04.4** Grow an empty factory/pool through its allocator boundary; assert first construction and final payload/wrapper release.

### ORDER-05 — Deferred requests

- [ ] **ORDER-05.1** Populate the deferred heap with different/equal deadlines; assert pop/tie order, cancellation and wrapper reuse.
- [ ] **ORDER-05.2** Schedule a repeating request and a callback that schedules/cancels another; assert invocation order and final heap/refcount state.
- [ ] **ORDER-05.3** Restore or switch the request clock with pending deadlines; assert which callbacks fire and when without rebasing deadlines by assumption.

### ORDER-06 — Cancellation and interruption

- [x] **ORDER-06.1** Three mode-1 replacements at tick3 admit only the replacement and recover all three orders. Evidence: [replacement][interrupt], O `replacement_arrival_cases=3`; other phases excluded.
- [x] **ORDER-06.2** Three mode-0 interrupts reach the temporary destination, resume the original and run its successor. Evidence: [interrupt/resume][interrupt], O `prepend_complete_cases=3`; explicit group scheduling.
- [ ] **ORDER-06.3** Run Stop/replacement while waiting, searching and turning; assert surviving queue, active flags and reclaimed allocations at each phase.
- [ ] **ORDER-06.4** Run interruption during group completion and deferred release; assert no duplicate arrival/release and correct resumed order.
- [ ] **ORDER-06.5** Kill/remove the mover during travel and one pending phase; assert scheduler unlink and all order/group/path lifetimes.
- [ ] **ORDER-06.6** Exercise one relevant ability transition during movement; assert command preservation/cancellation and routing inverse. Inventory additional distinct transitions as new tasks.

## GROUP — Shared movement groups

Evidence: [group evidence][M]. Tools/artifacts: motion.

### GROUP-01 — Group producers

- [ ] **GROUP-01.1** Issue a multi-selection player order, independent JASS orders and an AI order; record group identity sharing, creation limits and producer flags.
- [ ] **GROUP-01.2** Trigger join/leave/merge/split through those producers; assert membership and route ownership after each transition.

### GROUP-02 — Fresh group movement

- [x] **GROUP-02.1** 144 complete cached-route group ticks and 156 membership prepasses are covered. Evidence: [cached group ticks][cached-groups]; fresh wall-pair search is covered separately by GROUP-02.3; additional failed-route policy remains open.
- [x] **GROUP-02.2** Two originally admitted units join one original request; fresh shared/member routes run through owner updates and natural tick7 arrival. Evidence: [fresh shared pair](retail-pathfinding-movement.md#fresh-shared-pair-through-owner-arrival), frozen `retail-shared-pair-1.27.json`, report `group-02.2-frozen-shared-pair.json`; both decisions precede both commits, all intermediate words repeat exactly, all orders/groups/paths reclaim. Public selected/JASS/AI callers remain GROUP-01.1.
- [x] **GROUP-02.3** Original three-cell terrain wall and Footman mask-producing prefix give opposing owned routes under one shared group, tick19/25 arrivals and complete recovery. Evidence: [wall pair and reversal](retail-pathfinding-movement.md#shared-pair-with-terrain-obstruction-and-reversal), report `group-02.3-wall-ground-frozen.json`, four frozen pair fixtures;46 owner commits match C, maskless/open/reversal controls retain all raw differences. Failed routes, runtime edits and full class notification remain separate exclusions.

### GROUP-03 — Shared parameters

- [x] **GROUP-03.1** Shared-cap ownership/publication and group radius lifecycles have original-code coverage. Evidence: [shared parameters][shared-groups]; remaining flag/AI producers and target-speed adjustment are not closed.
- [ ] **GROUP-03.2** Trace group bit800 and speed-cap exemption producers; exercise target-speed adjustment with both exempt and capped members.
- [ ] **GROUP-03.3** Grow the shared auxiliary pool, change the largest member radius, then remove it; assert publication and allocation recovery.
- [ ] **GROUP-03.4** Run Captain AI attach/detach during movement; assert its shared-cap and delay ownership/inverse.

### GROUP-04 — Membership mutation

- [x] **GROUP-04.1** Cancel/detach a member during movement callbacks; verify reverse callback iteration and subsequent identity/ownership re-resolution, swap-removal order and every surviving row word. Preserve the original producers and distinguish controlled callback-boundary requests from a complete gameplay callback graph. Evidence: [callback-timed mutations](retail-pathfinding-movement.md#callback-timed-membership-mutation), frozen `retail-callback-mutations-1.27.json`,68 cases and two fresh strict corpus repeats. Handle reuse, full gameplay notification graph and survivor refresh/arrival remain open.
- [x] **GROUP-04.2** Complete the last member and run an all-invalid prepass; assert empty-group teardown, owner unlink and retained mover-owned state. Evidence: [callback completion and empty teardown](retail-pathfinding-movement.md#callback-completion-surviving-cohort-and-empty-teardown), open/wall frozen completion fixtures. The last invalid row is pruned, actual group virtual10 runs at count0, group/path pools return and owner unlinks; individual mover/path identities remain owned.
- [x] **GROUP-04.3** After GROUP-04.1, reclaim and reuse a member's handle through original producers during a callback; prove generation rejection, later iteration and registry/pool accounting. A pre-invalidated slot does not satisfy callback-timed reuse. Evidence: [callback-timed handle reuse](retail-pathfinding-movement.md#callback-timed-handle-reclamation-and-reuse), frozen open/wall reuse fixtures, four trigger/victim combinations each repeated twice, actual destructor/factory/activation, same-address/slot new generation and exact survivor row. Both registry aliases are supplied from the original creation contract; the omitted-alias counterfactual retains stale spatial slots and certifies no fidelity. Full gameplay RemoveUnit callback graph and survivor arrival remain open.
- [x] **GROUP-04.4** Complete a member at an original callback boundary, then compose the surviving mixed-speed cohort through natural arrival and cleanup; verify repeat raw trajectories and integrate active membership/speed into Move. Evidence: [completion/survivor journeys](retail-pathfinding-movement.md#callback-completion-surviving-cohort-and-empty-teardown), two frozen fixtures, four trigger/victim cases each repeated twice and178 exact C commits; [engine cohort correction](retail-pathfinding-engine.md#active-move-cohort-speed) reproduces six stale-cap failures before fixing them and covers identity wrap, edict reuse and save/load. Fixed-goal survivor offsets/destinations stay unchanged; actual refresh/reuse composition and full engine phases are explicitly split below.
- [ ] **GROUP-04.5** After GROUP-04.1/03/04, compose actual formation refresh and new survivor destinations after callback mutation, using the original mover reclamation/reuse composition in04.8. Run original owner decisions/commits through survivor natural arrival and cleanup, preserving removed/reused Unit/Move/order ownership and repeat raw trajectories. Directly calling a layout helper does not prove the actual refresh producer; removal alone does not trigger it in the verified fixed-goal journeys.
- [ ] **GROUP-04.6** Replace the engine cohort scan/static queued cap with Move-owned persistent retail group/member storage, generation/ownership checks, eligibility flags/shared override, queued activation/detachment and separate all-member decisions then commits. Integrate verified refresh/new destinations, teardown and save/load; compare intermediate group state and complete trajectories under the original clock contract.
- [x] **GROUP-04.7** Allocate baseline individual paths through original14ec50/150d50 instead of direct registration. Replay frozen singleton, pair and all callback-reuse cases; assert owner958 live/allocation counts and recycled-header links during mover release/reallocation and final group release, without changing frozen raw motion expectations. Evidence: [owned-path factory accounting](retail-pathfinding-movement.md#owned-path-factory-accounting), unchanged frozen expectations and116/116 strict corpus outcomes. Native heap allocation/failure and the full Unit construction graph remain BASE-03.1/MAP-05.3.
- [x] **GROUP-04.8** Complete a member at the original region callback, destroy/reallocate its mover through original producers, then run the retained survivor through natural arrival and final cleanup. Cover both victim roles and callback positions on open/wall maps with repeated exact trajectories, old-generation rejection, path/mover/spatial accounting and Unit/Move/order ownership; Evidence: [completed-member reuse](retail-pathfinding-movement.md#completed-member-reuse-through-survivor-arrival), two frozen four-case matrices,178 exact production velocity commits and118/118 strict corpus outcomes. Retain04.5 for actual formation refresh/new destinations and BASE-03.1 for the gameplay RemoveUnit graph/replacement actor binding.

## FORM — Formation and regrouping

Evidence: [formation evidence][M]. Tools/artifacts: motion, refill; Frida capture.

### FORM-01 — Formation producers

- [x] **FORM-01.1** The authored formation-rank setter and rank bits are mapped and tested. Evidence: [formation rank][formation-rank]; this does not close live group creation or other policy flags.
- [ ] **FORM-01.2** Load mixed authored ranks into a newly created group; assert each member's runtime rank and selected layout bucket.
- [ ] **FORM-01.3** Trace spacing bit20 and remaining formation-policy flags to callers; publish one producer-built witness per reachable value.

### FORM-02 — Layout geometry

- [x] **FORM-02.1** 144 complete layouts and 48 refresh cases are recorded. Evidence: [formation layout][formation-layout]; mixed moving radii and untested size domains remain excluded.
- [ ] **FORM-02.2** Run mixed-radius/oblique layouts with equal sort keys; assert assignments, row dimensions, centering and rotation.
- [ ] **FORM-02.3** Test moving members and sizes at/beyond the twelve-member table boundary; establish the original caller precondition or exact supported behavior.

### FORM-03 — Layout-to-motion chain

- [ ] **FORM-03.1** Compose refresh, adaptive destination query, held-member classification, all decisions and commit in one unblocked group tick; assert intermediate offsets and speeds.
- [ ] **FORM-03.2** Repeat with one blocked offset and with cached versus fresh routes; assert held/released members and fallback destination.

### FORM-04 — Regroup triggers

- [ ] **FORM-04.1** Change target, membership and member size at fixed ticks; assert which change rebuilds layout/routes and its timeout in simulation time.
- [ ] **FORM-04.2** Cause route failure and a warp-marker transition; assert regroup trigger, cached-state invalidation and next layout.

### FORM-05 — Retail formation witnesses

- [ ] **FORM-05.1** Capture one mixed-unit selection order and independently issued controls with the same map/seed; compare group IDs, assignments, caps and trajectories.
- [ ] **FORM-05.2** Send that selection through a narrow passage and regroup; compare offsets/rebuild timing with the composed fixture.

## SEP — Repulsion and spatial records

Evidence: [separation evidence][P]. Tools/artifacts: separation, spatial, motion.

### SEP-01 — Repulsion producers

- [x] **SEP-01.1** Authored Footman-disabled/Gryphon-enabled controls distinguish path blocking from opt-in repulsion. Evidence: [repulsion controls][repulsion]; mixed policy combinations remain open.
- [ ] **SEP-01.2** Trace nonzero config selectors and category/rank/mask overrides from authored/runtime producers; publish eligible/disabled cases for each.
- [ ] **SEP-01.3** Exercise the resulting policy table across supported movement types and owners; assert candidate eligibility before displacement.

### SEP-02 — Separation composition

- [x] **SEP-02.1** Post-arrival pair: 16 ticks, 14 attempts, ten accepted and four blocked. Evidence: [pair][pair], M `move_owner_separation_cases`; controlled profile and bounded numeric tolerance.
- [ ] **SEP-02.2** Record and independently compare every neighbor contribution for a three-object query through accumulation, clamp/cooldown and application.
- [ ] **SEP-02.3** After NUM-04, include an exact-overlap pair in that query; assert PRNG draws, endpoint result and actual occupancy changes across subsequent ticks.

### SEP-03 — Spatial records

- [ ] **SEP-03.1** Insert/remove movers in two orders; assert cell chain order, metadata/dead records and cleanup threshold/sampling cadence.
- [ ] **SEP-03.2** Cross query stamp wrap/repair and a fresh block allocation; assert candidate order and block reclamation after removal.
- [ ] **SEP-03.3** Force spatial allocation failure/growth during an update; assert no partial membership and the original movement outcome.

### SEP-04 — Retail separation witnesses

- [ ] **SEP-04.1** Replay one live exact-overlap case with recorded seed and neighbors; match contributions, cooldown and trajectory.
- [ ] **SEP-04.2** Capture a mixed-owner/radius/rank crowd with a blocked endpoint; explain displacement differences against the composed model.
- [ ] **SEP-04.3** Run disabled-repulse ground controls beside enabled cases; assert retry/Stop outcomes without classifying path blocking as repulsion.

## GATE — Way Gates

Evidence: [gate evidence][L] and [routes][R]. Tools/artifacts: transition, adaptive; Frida capture.

### GATE-01 — Gate eligibility and exit

- [ ] **GATE-01.1** Test activation/approach threshold equality and adjacent values with eligible/ineligible movers; assert route consumer decisions.
- [ ] **GATE-01.2** Block the exit and test outside-map/unreachable destinations; assert placement rejection, fallback or failure and retained route state.

### GATE-02 — Gate mutation

- [x] **GATE-02.1** Live outside approach, cached retarget and disable-to-walking witnesses are recorded. Evidence: [gate mutation][gate-mutation]; fresh retarget/destroy/impassable cases remain open.
- [ ] **GATE-02.2** Destroy a gate during approach; assert stale-record handling and subsequent walking/failure.
- [ ] **GATE-02.3** Retarget then issue a fresh order; compare cached versus fresh destination use through final arrival.
- [ ] **GATE-02.4** Disable the only edge across impassable terrain; assert retries/failure rather than assuming walking succeeds.

### GATE-03 — Multiple gates

- [ ] **GATE-03.1** Construct two overlapping sources in both orders; assert marker overwrite, cleanup and hierarchy propagation.
- [ ] **GATE-03.2** Run chained gates with active/inactive combinations and equal-cost alternatives; assert route choice and consumer event order.

### GATE-04 — Gate ID lifetime

- [ ] **GATE-04.1** Allocate through IDs1..255 and one further request; assert zero/exhaustion behavior and pool state.
- [ ] **GATE-04.2** Free/reuse an ID referenced by an existing route; assert revalidation and destination selection.
- [ ] **GATE-04.3** Traverse a gate with a group, then fail/skip it; assert regrouping and ordinary fine-route continuation.

## E2E — Combined scenarios and handoff

Evidence: [all contracts][ledger]. Tools/artifacts: corpus manifest/runner and normalized comparison artifacts from BASE.

### E2E-01 — Cross-feature baseline variants

- [ ] **E2E-01.1** After BASE-06.5, freeze static-detour and disconnected-goal variants; assert route, partial/failure events and final ownership.
- [ ] **E2E-01.2** Add dynamic blocker and pursuit variants to that manifest; reuse ROUTE-03/TARGET-02 evidence and compare intermediate state.
- [ ] **E2E-01.3** Add contention, cancellation and next-order variants; reuse SCHED-04/ORDER-06 evidence and assert event/queue order.
- [ ] **E2E-01.4** Add formation/crowd and gate variants; link FORM-05/SEP-04/GATE evidence and freeze expected cross-feature outputs.

### E2E-02 — Observer controls

- [x] **E2E-02.1** Open-ground no-attach control matches 304 markers twice; blocked-goal305, replacement311, fog610 and building310 match once. Evidence: [observer controls][observer]; crowd/mode repeats remain open.
- [ ] **E2E-02.2** Repeat blocked-goal, replacement, fog and building controls from the recorded manifests; assert matching completion markers and normalized outcomes.
- [ ] **E2E-02.3** Run equivalent minimal-hook/no-hook crowd and additional movement-mode controls; compare timing and complete trajectories, retaining mismatches.

### E2E-03 — Deterministic generated cases

- [ ] **E2E-03.1** Repeat each frozen scenario with the same seed twice; fail on any unexplained normalized state/event difference.
- [ ] **E2E-03.2** Generate a fixed-seed boundary corpus over supported lanes/radii/goals; minimize each mismatch and commit its input plus retail explanation.

### E2E-04 — Long-run composition

- [ ] **E2E-04.1** Compose verified stamp/counter/handle/gate-ID wrap and reuse cases into repeated movement; assert no stale ownership or changed route policy.
- [ ] **E2E-04.2** Compose reload/save-load, callback removal and pool pressure cases with active orders; assert final idle state and uninterrupted-control differences.

### E2E-05 — Unknowns audit

- [ ] **E2E-05.1** Join the BASE-03 inventory to reports and task IDs; emit a concrete list of remaining flags, prefixes, stubs, tolerances and excluded branches.
- [ ] **E2E-05.2** Resolve each listed row with evidence or documented unreachability; create bounded child tasks for unresolved rows instead of one open-ended investigation.

### E2E-06 — Implementation specification

- [ ] **E2E-06.1** Freeze structures, units, coordinate/lane/footprint and numeric/PRNG contracts with links to runnable evidence.
- [ ] **E2E-06.2** Freeze state machines, result codes, update/event order, ownership and invalidation contracts with limits/failure behavior and evidence links.

### E2E-07 — OpenRealm integration design

- [ ] **E2E-07.1** Map current Move-owned routing/steering, server clock/order dispatch and world/collision entry points to replacement interfaces; name files and call sites.
- [ ] **E2E-07.2** Specify group ownership, serialization/rebuild and cleanup boundaries; review against Quake2-style module/function-table contracts without implementing behavior.

### E2E-08 — Differential adapter design

- [ ] **E2E-08.1** Specify identical scenario inputs and normalized identity/state/event outputs for retail and OpenRealm adapters; encode one existing baseline report.
- [ ] **E2E-08.2** Define exact versus presentation-only tolerance rules and failure diagnostics; validate the comparator against deliberate mutations of that encoded report.

## READY — Start the faithful replacement

Evidence: [scope and contracts][ledger]. Tools/artifacts: frozen corpus, coverage inventory and integration design.

### READY-01 — Evidence coverage gate

- [ ] **READY-01.1** Run the coverage inventory audit: every in-scope branch/exclusion has evidence or a proved-unreachable disposition; zero unassigned behavior gaps.

### READY-02 — Behavior gate

- [ ] **READY-02.1** Run all frozen success/failure and cross-feature scenarios; zero unexplained differences, with retail quirks and numerical thresholds retained as regressions.

### READY-03 — Reproduction gate

- [ ] **READY-03.1** Run the corpus from documented inputs in a fresh output directory; build/hash/seed/observer controls and completion markers all pass.

### READY-04 — Implementation handoff gate

- [ ] **READY-04.1** Review the frozen specification and OpenRealm interface design against the baseline; record no remaining decisions that require guessing, then authorize starting the replacement against this corpus.

## Previous milestone IDs

The prior eight checked slices remain checked under these leaf IDs. The original
81 parent IDs remain above with their requirements distributed among children.

| Previous slice | Current leaf |
| --- | --- |
| BASE-06a / ORDER-02a | BASE-06.1 |
| ORDER-01a | ORDER-01.1 |
| ORDER-02b | ORDER-02.1 |
| ORDER-06a / ORDER-06b | ORDER-06.1 / ORDER-06.2 |
| SCHED-02a | SCHED-02.1 |
| SCHED-02b / GROUP-02a | SCHED-02.2 |
| SEP-02a | SEP-02.1 |

[ledger]: retail-pathfinding.md
[S]: retail-pathfinding-search.md
[R]: retail-pathfinding-routes.md
[M]: retail-pathfinding-movement.md
[P]: retail-pathfinding-separation.md
[L]: retail-pathfinding-experiments.md
[admission]: retail-pathfinding-movement.md#complete-initial-unit-admission
[arrival]: retail-pathfinding-movement.md#generated-tasks-through-queued-user-order-arrival
[fifo]: retail-pathfinding-movement.md#two-user-order-fifo-composition
[interrupt]: retail-pathfinding-movement.md#active-replacement-versus-interruptprepend
[owner]: retail-pathfinding-movement.md#complete-singleton-owner-updates-and-visual-settling
[pair]: retail-pathfinding-movement.md#owner-updates-with-a-controlled-separation-pair
[map-load]: retail-pathfinding-search.md#terrain-origin-producer-and-map-factory-composition
[map-edits]: retail-pathfinding-search.md#terrain-edit-and-explicit-rebuild-composition
[widgets]: retail-pathfinding-search.md#widget-rasterization-and-overlapping-occupancy
[footprints]: retail-pathfinding-search.md#footprints-and-dynamic-occupancy
[fine-static]: retail-pathfinding-search.md#complete-static-fine-grid-searches-and-stamp-reuse
[adaptive-veto]: retail-pathfinding-search.md#exact-size-2-east-boundary-veto
[numeric]: retail-pathfinding-separation.md#exact-scalar-arithmetic-and-occupied-cell-boundaries
[ranges]: retail-pathfinding-movement.md#exact-point-task-range-predicate
[reclamation]: retail-pathfinding-movement.md#last-reference-payload-release-and-factory-reuse
[cached-groups]: retail-pathfinding-movement.md#full-cached-route-group-tick-and-membership-prepass
[shared-groups]: retail-pathfinding-movement.md#shared-group-parameters-ownership-and-publication
[formation-rank]: retail-pathfinding-movement.md#authored-formation-rank-producer
[formation-layout]: retail-pathfinding-movement.md#complete-formation-layout-composition
[repulsion]: retail-pathfinding-separation.md#authored-repulsion-fields-and-paired-crowd-experiments
[gate-mutation]: retail-pathfinding-experiments.md#outside-entry-approach-and-live-gate-changes
[observer]: retail-pathfinding-experiments.md#controls-without-an-attached-observer
