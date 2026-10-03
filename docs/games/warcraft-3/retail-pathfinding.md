# Retail Warcraft III pathfinding

## Objective and status

Recover the complete retail behavior, verify each mechanism against original
code, compose those tests into full movement scenarios, then replace
OpenRealm's pathfinding with an implementation that passes the same corpus.
Algorithm labels are descriptive; behavioral parity is the completion criterion.

**Current state:** substantial mechanism coverage; incomplete full-lifecycle
composition. **576 game functions annotated. Incremental scalar, velocity, stock-turn, idle-unit routing, nearest partial routes and point Move arrival implemented;
ordinary public twelve-member movement matches1,953 commits and2,634 saved suffix commits;
the full pathfinder replacement remains open.** See the
[engine integration evidence](retail-pathfinding-engine.md) for exact C/live
comparisons and remaining velocity/clock/trajectory gaps.
The [executable backlog](retail-pathfinding-todo.md#progress) records completed
mechanisms and remaining work as independently closable tasks. Its progress
counts are maintained there; neither task nor assertion counts measure retail
fidelity. Large predicate sweeps do not substitute for order-to-arrival or
crowd trajectory tests.

| Detailed evidence | Contents |
| --- | --- |
| [Search](retail-pathfinding-search.md) | Fine/adaptive searches, heap, heuristic, footprint queries, map construction/invalidation |
| [Routes](retail-pathfinding-routes.md) | Reconstruction, scheduling, target refresh/visibility, arrival, retries, yielding, refill and transitions |
| [Movement](retail-pathfinding-movement.md) | Clocks, speed/heading, velocity, groups, formations, regrouping, completion events and order release |
| [Separation](retail-pathfinding-separation.md) | Authored repulsion, candidate order/filtering, displacement, occupancy links and reclamation |
| [Experiments](retail-pathfinding-experiments.md) | Copied-map workflow, controls, terrain divergence, Way Gates and bounded capture commands |
| [Engine integration](retail-pathfinding-engine.md) | Exact scalar/velocity C replay, authored and scripted windows, repeat captures and remaining numerical gaps |
| [Corpus](retail-pathfinding-corpus.md) | Versioned oracle/archive inventory, known adaptive differences, strict fresh-report runner and provenance limits |

The ledger below is the current status authority. Evidence files preserve
addresses, numerical contracts, fixture limits and artifacts; a local test's
exclusion is not necessarily an investigation-wide gap. Update both when new
composition closes a gap.

## Binary and evidence conventions

- Target: `game.dll` **1.27.1.7085**, preferred image base `6f000000`.
- SHA256: `d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
- Frida address: `module.base + (VA - 0x6f000000)`. Other builds need new offsets.
- `tools/ghidra/MapPathfinding.java`: hash-guarded descriptive names/comments;
  `MapPathfindingTypes.java` also persists45 partial layouts,287 verified
  fields and258 instruction-established prototypes with explicit register/stack
  storage. Its schema is
  [`retail-pathfinding-types-1.27.json`](../../../tools/ghidra/fixtures/retail-pathfinding-types-1.27.json).
  Unassigned bytes remain undefined; `Prefix` lengths are verified extents,
  not claims of complete class size. Scalar words use a uint32 typedef so
  decompilation cannot imply host float arithmetic. The hash/base guards run
  before mutation; incompatible existing layouts/names are preserved by refusal.
  Applying and rerunning the script, then saving `game.dll`, produced the
  layout/prototype readback `num-01.16-ghidra-types.json` under the report root.
  Pass the absolute schema path and optional metadata report path as script args.
  576 function names/comments applied and saved. Latest captain readbacks:
  `runtime/captain-home-ghidra-readback-saved-261002.json` and
  `runtime/captain-home-ghidra-types-saved-261002.json`. Prior group-radius readbacks:
  `runtime/group-radius-ghidra-readback-saved-261002.json` and
  `runtime/group-radius-types-final-261002.json`. Prior moving-radius readbacks:
  `runtime/moving-radius-ghidra-readback-saved-261002.json` and
  `runtime/moving-radius-types-final-261002.json`. Prior target resize readbacks:
  `runtime/follow-target-resize-ghidra-readback-saved-261002.json` and
  `runtime/follow-target-resize-types-final-261002.json`. Prior target teleport readbacks:
  `runtime/follow-target-teleport-ghidra-readback-saved-261002.json` and
  `runtime/follow-target-teleport-types-final-261002.json`. Prior death/removal readbacks:
  `runtime/follow-target-reuse-ghidra-readback-saved-261002.json` and
  `runtime/follow-target-reuse-types-final-261002.json`. Prior target Follow readbacks:
  `runtime/follow-velocity-ghidra-readback-saved-261002.json` and
  `runtime/follow-velocity-types-final-261002.json`. Prior independent-active Shift readbacks:
  `runtime/selected-independent-shift-ghidra-readback-saved-261002.json` and
  `runtime/selected-independent-shift-types-final-261002.json`. Prior mixed active/idle Shift readbacks:
  `runtime/selected-mixed-shift-ghidra-readback-saved-261002.json` and
  `runtime/selected-mixed-shift-types-final-261002.json`. Prior two-pending Shift readbacks:
  `runtime/selected-double-queued-ghidra-readback-saved-261002.json` and
  `runtime/selected-double-queued-types-final-saved-261002.json`. Prior idle Shift readbacks:
  `runtime/selected-idle-shift-ghidra-readback-saved-261002.json` and
  `runtime/selected-idle-shift-types-final-261002.json`. Prior queued-input readbacks:
  `runtime/selected-queued-ghidra-readback-final-saved-261002.json` and
  `runtime/selected-queued-types-final-261002.json`. Prior selected-input readbacks:
  `runtime/selected-point-ghidra-readback-261002.json` and `runtime/selected-point-types-261002.json`. Prior twelve-member readbacks:
  `runtime/public-twelve-ghidra-readback-261002.json` and `runtime/public-twelve-interval-types-261002.json`. Prior public pair readbacks:
  `runtime/public-pair-ghidra-readback-261001.json` and `runtime/public-pair-ghidra-types-261001.json`. Prior group admission readbacks:
  `runtime/group-point-ghidra-readback-261001.json` and `runtime/group-point-ghidra-types-261001.json`. Prior spawn type readback:
  `runtime/spawn-admission-types-261001.json`. Prior retry readbacks:
  `runtime/peer-retry-ghidra-readback-final-261001.json` and
  `runtime/peer-retry-types-final-261001.json`. Names are recovered roles, not original debug symbols;
  RTTI names are original. No inferred prototypes installed.
- `MapPathfindingCRT.java` separately saves the exact sibling CRT byte/locale map:
  three partial types, eight function roles, seven global labels and the verified
  ECX/stack context constructor ABI. The analyzed program is `/CRT/msvcr120.dll`
  in the same project; the mapper verifies its executable SHA before mutation.
  Readback: `num-01.13-ghidra-crt-types.json`.
- Local Ghidra project: `/GitHub/wc3-analysis/projects/WC3Audio.gpr`, program
  `game.dll`, backend `http://127.0.0.1:8089`.
- Reports/decompilation/captures: `/GitHub/wc3-analysis/reports/pathfinding-1.27/`.
  `runtime/...` artifact paths in evidence files are relative to this directory.
  Exports use `<VA>-decompile_function.json`, `-disassemble_function.json` and
  `-get_xrefs_to.json`. Decompiled retail bodies stay outside the repository.
- Ghidra loses register-carried/soft-float operands. Check assembly before
  assigning ABI, units or formulas. Oracle reports distinguish complete calls,
  bounded prefixes, stubs, preallocated storage and numerical tolerances.
- Unicorn: flush translated blocks after adding observation hooks or reusing
  an end-address boundary crossed by a resumed call; assert hook counts/end PC.

Setup: [audio RE tools](audio-retail-analysis.md). Older reference:
[RoC demo audit](pathfinding.md#retail-gamedll-path-audit).

## Recovered system contract

| Layer | Recovered behavior |
| --- | --- |
| Maps | Owner `6fd53a48`: proximity `+234` (scale 8), fine `+238` (1), adaptive `+23c..248` (2/4/8/16), fine/adaptive systems `+24c/+250`. Padded dimensions; four two-bit traversal lanes. |
| Fine search | Eight neighbors, cardinal/diagonal costs 15/21; piecewise heuristic; cheaper closed nodes reopen. Heap records carry generations; stale entries consume work. Budget 700 can stop at pop 701. |
| Adaptive search | Dense multiresolution maps; aligned squares promote/subdivide by classification. Search produces a reverse coarse route. Synthetic size-2 east-boundary veto is reduced and causally isolated; live consequence unresolved. |
| Footprints | Authored collision scalar → world/grid conversion `/32` → mover `+90`, path `+b4`; four fine classes at 0.5/1/1.5. Queries include terrain and eligible occupancy objects; selected objects are temporarily excluded/restored. |
| Persistent routes | `CLrPath` owns fine/adaptive buffers `+34/+54`, indices `+74/+78`, work/admission state, retries and yielding delay. Request, search, partial result and arrival are distinct outcomes. |
| Scheduling | Per-owner/class queues and policy buckets gate work by accumulated pops and update counters. Cached waypoints can bypass search; failed admission is distinct from an unreachable goal. |
| Movement | Separate group/member routes. Group tick decides all members before committing all speeds/headings. Commit integrates old velocity first; shared speed eligibility, heading and occupancy affect the next step. |
| Formation | Authored `UnitData.slk:formation` → mover rank bits 12–15; rank buckets, ordered rows, centering/rotation and offset destination queries. Regrouping refreshes layouts/routes. |
| Repulsion | Authored opt-in via `UnitBalance.slk:repulse`; proximity queries feed ordered pair displacement, endpoint validation and later position application. Footman blocking does not imply repulsion; Gryphon captures exercise it. |
| Completion | Group member completion detaches/stops, emits arrival or blocked events, and defers row removal. Movement subscribers remap events to order handlers; queue pop schedules deferred wrapper release. |
| Terrain edits | `SetTerrainPathable` mutates fine flags. A copied-map edit + fresh order leaves watched coarse classifications stale through 30 simulation seconds; other invalidation producers remain unresolved. |
| Way Gates | IDs 1..255 stamp adaptive source cells; active records provide cost-1 edges. Routes encode sentinel + ID; consumption rechecks active state and validates placement. |

This is fine/adaptive-grid A* with footprint/object queries. No scalar clearance
field or cached cluster-portal graph has been recovered; neither label is a
requirement for the implementation. Preserve measured quirks, including coarse
staleness and software-float boundary behavior, until their full contracts are known.

## Completion ledger

Evidence levels: **S** static/assembly mapping; **O** isolated original-code
oracle; **C** composed original-code calls; **L** controlled live retail witness.
Levels apply only to the stated fixtures. No row below claims complete parity.
Counts overlap; do not sum them into a coverage percentage.

| ID / area | Verified coverage | Remaining acceptance work |
| --- | --- | --- |
| MAP — maps/invalidation | **S/O/L:** dimensions/scales, reducers (20,736 cases), temporary rectangle exclusion/restore; direct fine/coarse divergence; 11,664 edits +216 rebuild/reversal compositions, 30 clipped updates; terrain-origin producer and 25 complete no-file loads; maintenance/release prefixes; decoded masks, 144 overlapping widget sequences and96 paired full widget lifecycles | File-backed loader/deserialization, reload/destruction, allocation growth, non-dyadic bounds; all terrain/object lifecycle invalidation producers; mixed/overlapping exclusions and exceptional restoration |
| FINE — fine search | **S/O/C:** costs/heuristic, heap ties/reopening/generations/budgets; 288 complete static searches + reuse;384 full mixed ground/flight/float/amph object chains, metadata repeats and C matches; wrapper partial/same-cell/zero-budget results;3,840 exact fine reconstruction words and engine fractional endpoints; engine idle-object/partial routing and seven stock profiles | Full authored profile parsing/support surfaces; initial endpoint/footprint policies; stamp wrap, node/free-list/capacity paths; invalid-coordinate preconditions |
| ACC — adaptive search | **S/O/C:** promotion/subdivision, lanes, size classes, full requests, reduced size-2 veto; enabled public-advance search corpus | Remaining side/corner branches; live size-2 consequence; invalid classifications, special-edge search combinations and heuristic effects |
| FOOT — footprints | **S/O/C:** producer conversion, exhaustive masks, terrain/occupancy queries, target perimeter, self/target exclusion in refill | Full world-radius/geometric contract; size × corridor/alignment sweep; mixed category/lifetime interactions |
| SCHED — admission/scheduling | **S/O/C/L:** bucket/FIFO/cadence, owner row and transitions, budgets/timestamps, live handoffs; full singleton owner tick and active singleton plus controlled repulsor | Populated shared-cap/multiple-group owner tick; mutation/reentrancy; fairness under crowd load; non-unit class-15 producers |
| ROUTE — reconstruction/refill | **S/O/C:** fine/coarse endpoints, partials, size-2 adjustment, sampling/skipping; 288 object refills; 288 enabled hierarchy advances; 288 full ordinary fine/coarse transitions | Dynamic segment collectors and public admission; dynamic yielding + fresh refill + multi-tick progression; allocation growth; invalid-start contracts |
| TARGET — target state/retry | **S/O/C/L:** target radius/heading tests, destination reset/replan, refresh counter, follow, invisibility cancellation, fog loss/reacquisition, perimeter acceptance and retry exhaustion | Point-order arrival parameter producers; sub-cell edits; other visibility-loss policies; complete moving-target ticks; gameplay meanings of remaining policy flags |
| NUM — arithmetic | **S/O/C/L:** 200,330 exact scalar calls including divide/sqrt; independently reconstructed acos tables and exact vector-heading chain; 2,130 normalizations; 864 bounds prefixes; compiled decimal/integer producer wrapping and public default-CRT byte grammar, with514 repeated raw S2R/Move calls | Remaining conversions and arithmetic consumers; producer reachability of extreme raw inputs; full mutation/trajectory parity |
| MOVE — kinematics/clocks | **S/O/C:** 13,824 speed/heading cases; producers, normalization; 2,304 position integrations; exact C/live velocity, vector-heading and committed-facing kernels; authored turn/window chain; velocity commits with real occupancy updates | General movement parity outside verified scalar/turn/heading/facing kernels; clock domains/cadence/rollover; moving mixed-object and region-crossing trajectories |
| GROUP — group state | **S/O/C/L (target witnesses):** shared-cap ownership/publication, max footprint, stops/pools; 576 member decisions; 144 route/commit compositions; 144 full cached-route ticks; 156 membership prepasses;68 callback-boundary mutations/controls and exact survivor rows; original callback-timed handle reclamation/reuse on open and wall pairs; mixed-speed completion/survivor arrival, stable reserved slots and empty teardown retaining mover-owned paths; active engine cohort speed correction; 36 new-group fine-search ticks; fine/adaptive singleton wall trajectories and one active singleton plus repulsor | Shared-group adaptive/obstacle/crowd fresh ticks; target-speed adjustment; populated owner tick; create/join/leave/destroy during movement; live Captain AI delay; stale handles under mutation |
| FORM — formations/regroup | **S/O/C:** authored rank setter; interval classifier; rank/row layout; 144 full layouts, 48 refreshes, 384 offset-destination cases, 648 regroup/reset/refresh compositions | Mixed-radius/moving/oblique complete layouts; refresh → destination → decision → commit chain; live selection formations and rebuild cadence; remaining flag producers |
| ORDER — completion/lifetime | **S/O/C/L:** 1,536 completion + next-prepass cases; real CUnit bridge; 12 subscriber prefixes; 144 full arrival calls including next-task rejection/acceptance and group/path preparation plus36 fresh fine-search ticks and78 elapsed travel/arrival/reclamation trajectories including fine/adaptive/full-owner wall detours and one active singleton plus repulsor; 288 full ordinary-point order producers/984 direct point-task producers/24 produced-chain queued-order arrivals and four two-order FIFO cases, with34 complete initial unit admissions; live d016b acceptance/arrival and blocked-goal recovery; 432 generic queue pops/releases; 216 retained-ref notifications; 8 zero-ref reclaim cycles/6 factory reuses; S plus engine regression proof of immutable issued-order event ID/point/target snapshots through reentry and save/load, separately from live current-order state; registered2039d0 at42 frozen owner states plus18 invalid-backing controls and engine point-Move active-head lifecycle/save/reuse; repeated public Follow/Hold/Patrol/death query witness and engine Move-owned Follow/save/reuse plus completed-Hold behavior regression and synchronous healthy Follow RemoveUnit/queued handoff and public/UI Patrol reversal/queued/save/reuse | UI/network producers feeding the verified unit-admission-to-arrival composition; broader obstacle/adaptive/crowd travel; alternate can't-path branches; callback reentrancy/user-order progression; remaining current-order owners (ORDER-01.10..14/16..18); cancellation variants; live registration, populated targets/relations/subscriptions; negative-domain/repeating requests and heap ordering |
| SEP — separation/spatial | **S/O/C/L:** filters/cooldown, candidate order/stamps, arithmetic slices, overlap PRNG, endpoint validation, bounds/lazy links/reclamation; Footman/Gryphon controls; 16 post-arrival pair ticks (10 accepted/4 blocked attempts) and 43 separation updates within an active-singleton owner trajectory | Exact per-neighbor live replay; full numeric parity; config/category/rank producers; mixed owners/blocked repulsion; allocation failure/growth, stamp repair, full scheduling |
| GATE — special edges | **S/O/C/L:** native/source/ID chains, sentinel handling, active/inactive traversal, outside approach, cached retarget and disable→walking; ordinary transition oracle | Destroy during approach; fresh retarget order; disabled edge over impassable terrain; overlapping/multiple gates; ID exhaustion/reuse; unreachable exits and mover eligibility |
| E2E — reproducibility/parity | **O/C/L:** hash guards, independent references for selected routines, bounded manifests/captures/analyzers; producer-built point/FIFO baseline with42 frozen snapshots and two identical repeats; BASE-05 versioned corpus and154 declared outcomes, retaining native differences, counterfactual controls and rejected archives | Broader unified intermediate-state/trajectory corpus; historical observer/map provenance gaps; full order-to-arrival/failure compositions; eventual OpenRealm differential runner |

## Next work and completion criteria

[Executable research backlog](retail-pathfinding-todo.md#work-next): numbered
tasks, acceptance checks, completed evidence and the READY gate. Start with
**GROUP-04.6** mixed active/idle and unrelated-current-order queued producers,
then live crowd/routing compositions. Two pending shared requests, physical
generation-safe visits and creation-time formation origin are now integrated. Numeric inventory remains
required alongside concrete engine consumers; the backlog names dependencies
and finish artifacts. The areas below describe the sequence, not additional tasks.

1. **ORDER + GROUP + MOVE:** join the verified initial-admission/FIFO chain
   (now joined with actual owner updates and map/mover producers, with a
   [frozen baseline](retail-pathfinding-movement.md#producer-built-frozen-baseline))
   to populated shared groups; the controlled separation pair already
   passes and is not an untouched prerequisite.
2. **MAP + FOOT + FINE + ACC:** complete construction/invalidation and dynamic
   blocker contracts; exercise bounds, sizes, categories, wrap and capacity paths.
3. **ROUTE + TARGET + SCHED:** add obstacle insertion/removal, moving targets,
   yielding, retries, cancellation and multi-tick contention to that lifecycle.
4. **FORM + SEP + GATE:** compose selection formations, mixed crowds and special
   edges; validate numerical/ordering behavior in controlled retail captures.
5. **E2E:** consolidate the reusable corpus; then implement the replacement and
   compare OpenRealm with retail using the same inputs and state observations.

Close a ledger item only when its producer, consumer, state transitions and
failure/lifetime paths are mapped and covered by appropriate O/C/L evidence.
Record intentional fixture exclusions and numeric tolerances. A prefix test
stops at its boundary; static mapping does not upgrade it to a full call.
“No hooks/stubs” is scoped per corpus, not a blanket property of every oracle.

## Special edges are Way Gate records

See [gate producer/consumer contract](retail-pathfinding-experiments.md#special-edges-are-way-gate-records)
and [live gate experiments](retail-pathfinding-experiments.md#gate-experiment-fixture).
Related OpenRealm feature status: [Way Gates](way-gates.md).

## Cinematic experiments

Use copied Human02Interlude maps; replace only `war3map.j`, retain an untouched
control, and remove competing scene orders/coordinate writes during measurement.
Record accepted order, goal, position/facing, path buffers, obstacles and bounded
completion. Realtime rendering alone does not prove that a scene uses pathfinding.

[Scenario matrix and workflow](retail-pathfinding-experiments.md#cinematic-experiments),
[bounded launch/analyze commands](retail-pathfinding-experiments.md#runtime-witnesses-and-reproduction),
[original camera/cutscene workflow](retail-camera-tracing.md).

## Reproduction

Run from the repository root. Local installation paths are examples; the binary
hash is mandatory. Existing report counts above are evidence records, not a claim
that this documentation edit reran the experiments.

```sh
pathing_python=/GitHub/wc3-analysis/verify-venv/bin/python
pathing_binary=/run/media/lofcz/ssd_external/Games/w3/game.dll
pathing_reports=/GitHub/wc3-analysis/reports/pathfinding-1.27
"$pathing_python" tools/ghidra/verify_wc3_pathing_motion.py \
  --binary "$pathing_binary" --report "$pathing_reports/motion-oracle.json"
```

The motion oracle also requires sibling `msvcr120.dll`, SHA256
`86e39b5995af0e042fcdaa85fe2aefd7c9ddc7ad65e6327bd5e7058bc3ab615f`.
It maps/relocates original CRT transform math and binds its actual exports;
this is not an independent model of retail software trigonometry.

Standard invocations use `tools/ghidra/verify_wc3_pathing_<suffix>.py` with
`--binary` and `--report` as above; substitute the report/extra arguments below.
Special adaptive fixtures/interventions retain their full commands in
[search evidence](retail-pathfinding-search.md#size-2-traversal-and-a-reduced-hierarchy-witness):
some intentionally return exit 1 to reproduce a retail/reference difference.
The [corpus manifest and runner](retail-pathfinding-corpus.md) collect all32
oracle scripts, selected exact/frozen variants and archived captures with
explicit expected outcomes; a known difference remains a difference.

| Script suffix | Report | Extra arguments |
| --- | --- | --- |
| `queue` | `fine-queue-loop-oracle.json` | — |
| `search` | `fine-heuristic-oracle.json` | — |
| `adaptive` | `adaptive-class1-oracle.json` | — |
| `footprints` | `footprint-occupancy-oracle.json` | `--exhaustive` |
| `grid` | `fine-grid-request-oracle.json` | — |
| `routes` | `route-reconstruction-oracle.json` | — |
| `scheduler` | `scheduler-oracle.json` | — |
| `target` | `target-oracle.json` | — |
| `retry` | `retry-oracle.json` | — |
| `segment` | `segment-oracle.json` | — |
| `blockers` | `blocker-collector-oracle.json` | — |
| `refill` | `refill-oracle.json` | — |
| `yield` | `yield-oracle.json` | — |
| `motion` | `motion-oracle.json` | — |
| `order_tasks` | `order-tasks-oracle.json` | — |
| `lifetime` | `lifetime-oracle.json` | — |
| `maps` | `maps-oracle.json` | — |
| `map_construction` | `map-construction-oracle.json` | — |
| `load_masks` | `load-masks-oracle.json` | — |
| `widget_masks` | `widget-masks-oracle.json` | — |
| `numeric` | `numeric-oracle.json` | — |
| `range` | `range-oracle.json` | — |
| `speed` | `speed-oracle.json` | — |
| `spatial` | `spatial-oracle.json` | — |
| `separation` | `separation-oracle.json` | — |
| `waypoint` | `waypoint-acceptance-oracle.json` | — |
| `transition` | `transition-oracle.json` | — |

Other oracle scripts (`arrival`, `cells`, `refresh`, `replan`, `reset`) expose
fixture options through `--help`; consult their reports and subsystem evidence
before changing a run's scope. Trace-analyzer regression:
`python3 tests/test_frida_pathfinding_trace.py`.

```sh
curl --max-time 30 -fsS \
  'http://127.0.0.1:8089/decompile_function?address=6f1625f0&program=game.dll'
curl --max-time 30 -fsS \
  'http://127.0.0.1:8089/disassemble_function?address=6f164020&program=game.dll'
```

Terminology only: [HPA*](https://webdocs.cs.ualberta.ca/~mmueller/ps/2004/hpastar.pdf),
[HAA*](https://pathfinding.ai/pdf/harabor-botea-cig08.pdf).
These are not evidence of Blizzard's implementation.

### Fresh shared-pair checkpoint

GROUP-02.2 now has a producer-built exact two-unit owner fixture: original point
admission, original shared request membership, fresh owned/shared routes, both
decisions before either commit, natural tick7 arrival and complete cleanup.
See [fresh shared pair](retail-pathfinding-movement.md#fresh-shared-pair-through-owner-arrival)
for the command, canonical hash and explicit supplied boundaries. Public player/
JASS/AI producer sharing and runtime membership mutation remain open; the following checkpoint covers terrain obstruction.

### Shared wall-pair checkpoint

GROUP-02.3 now freezes a terrain-wall owner scenario with stock Footman mask
getter/publication evidence from Frida: opposing member routes, tick19/25
arrivals, membership2→1→0, complete cleanup and46 exact production-C commits.
[Wall pair and controls](retail-pathfinding-movement.md#shared-pair-with-terrain-obstruction-and-reversal)
retain maskless negative and open/reversal states, including retained rebuild
metadata. Other movement profiles and the full ability-notification class graph
remain BASE-02.1/BASE-03.1.


### Engine payoff32: peer-assignment retry

Complete original retry initialization/consumption now matches production C
for336/2016 cases, plus two complete97-call live repeats with exact native
source/adjusted-goal, registered group count and owner random state. Move ports
peer20 fine-leg invalidation, retained coarse-route refill, final-goal heading
and stopped velocity; public JASS/scheduler/save71 continuation repeats1440 state
words. See [engine transaction and admission limit](retail-pathfinding-engine.md#payoff32-blocked-fine-leg-retry-and-retained-coarse-plan).
Other nonempty vectors still require original source-footprint admission/recovery;
this does not close full terrain/idle retries, group producers or crowd cycles.

Public singleton oblique movement now composes the real timer/clock producer
with a separate group plan and member route. Three lifetimes match689 original
commits, including two intermediate stop/refill handoffs and260 saved
continuation commits. [Payoff36](retail-pathfinding-engine.md#public-oblique-move-retains-the-singleton-group-destination)
keeps supplied scenery geometry distinct from unresolved scene-loading and
hierarchy-invalidation producers; shared physical crowds remain required.


Ordinary fine work and pending request FIFOs now follow all16 unit-player rows.
Ownership cancellation occurs before player publication and removes the old row
before reclassification; Save76 preserves each row and request class. The public
owner-change motion/Stop journey repeats exactly and has a normal-frame engine
regression. See [payoff40](retail-pathfinding-engine.md#ordinary-fine-search-belongs-to-the-unit-player).
Three accelerated policy pools and non-unit class15 producers remain open.


Ordinary player-selected ground Move now creates the same retained physical
owner as public point groups. Two actual native Win32 click lifetimes each match
all228 engine clock/position/velocity/facing commits and saved continuations.
Their absolute input timings are explicit; their relative motion repeats exactly.
The observer stays read-only and the helper uses the public Windows input API.
See [payoff41](retail-pathfinding-engine.md#selected-ground-move-uses-the-shared-physical-owner).
The first Shift behind an active selected ground cohort now retains a new common
request and rebuilds physical membership at staggered arrival; two original
journeys match1020 absolute motion commits and684 saved suffix commits. See
[payoff42](retail-pathfinding-engine.md#selected-shift-move-retains-request-ownership-through-staggered-arrival).
Idle selected ground Shift now also starts the shared physical owner immediately;
two native journeys match456 engine commits and304 saved suffix commits. See
[payoff43](retail-pathfinding-engine.md#selected-idle-shift-starts-the-shared-physical-owner-immediately).
Two pending selected ground Shift moves retain the latest submitted request
through both FIFO activations. Creation-ordered owner generations and the
formation origin survive slot reuse; normal frames match1110 exact native commits
and864 saved suffix commits. See [payoff44](retail-pathfinding-engine.md#two-pending-shift-moves-retain-submission-history-and-physical-generations).
Mixed active/idle selected ground Shift now preserves active heads and starts
idle peers immediately. Four native journeys match1510 engine commits and1020
saved suffix commits, including both later cohort rejection and joining. See
[payoff45](retail-pathfinding-engine.md#mixed-active-and-idle-shift-share-one-submitted-request).
Independent active singleton point owners now also accept the common pending
request: two native repeats match1022 engine commits and844 saved suffix commits.
See [payoff46](retail-pathfinding-engine.md#independent-active-owners-accept-the-same-pending-ground-request).
Other current-order families, air/mixed candidates, enabled formation options
and AI sharing remain open.


Payoff47 brings the recovered target range, countdown, destination bucket and
approach-to-persistent-Follow lifecycle into the Move ability. Two complete
public moving-target/speed-change captures repeat1015 absolute commits; ordinary
engine frames and Save80 continuations match1015 plus2595 suffix commits. Five
new descriptive roles are saved in Ghidra; layouts and explicit prototypes
remain unchanged. TARGET-02.4 closes this bounded ground producer; wider target
mutation and delayed readiness producers remain required. See
[the engine evidence](retail-pathfinding-engine.md#smart-follow-tracks-a-moving-target-through-a-speed-change).

Payoff48 closes TARGET-02.3 by integrating synchronous ground Follow cancellation
on public target death/removal. Actual native pool/public-handle reuse retains a
fresh canonical generation; the engine remains idle until explicit new Smart.
Both normal-frame journeys match948 original commits each plus3492 total saved
suffix commits. [Engine evidence](retail-pathfinding-engine.md#follow-cancels-synchronously-before-target-pool-reuse)
and strict capture checks retain the scope; queued/combat parents remain open.

Payoff49 integrates the premature legacy point-route settling fix exposed by
public moving-target axis teleport. Four native repeated setter journeys match
4091 normal engine commits and7872 saved suffix commits. The test-only commit
observer preserves same-clock movement before later timer writes.
[Teleport evidence](retail-pathfinding-engine.md#follow-tracks-public-target-teleports-without-premature-point-settling)
closes TARGET-02.5; collision resizing remains02.2.

Payoff50 closes TARGET-02.2 with public Chaos collision resizing while ground
Follow is active. The same target/mover identity changes radius31 to63 or7;
active range is retained, and a new nearby approach uses half predicted edge
distance before restoring authored persistent FollowRange. Five repeated native
journeys match5075 ordinary engine commits and9725 saved suffix commits. Chaos
is implemented in its owning ability with authored UnitID and requirement gates.
[Resize evidence](retail-pathfinding-engine.md#follow-retains-active-range-and-admits-resized-targets-with-half-edge-approaches)
retains failed setup controls and the boundary to moving-unit footprint work.


Moving Chaos now has an independently repeated ground point-Move contract across
all nine radius boundaries.15fef0 writes mover90 and immediately refreshes fine
and proximity geometry;670950 retains the public unit while rebinding type,
stats and preserved task head.4d8dc0/4d8d40 schedule the0.01 Chaos commit from
enabled/research delivery;4d8b40 owns subscription refresh and4c8aa0 cancels the
two pending timers.053630 rearms periodic requests from the temporary due timer
clock: request4 deadline,8 period,c clock,10 flags and18 receiver. The owner period
is3cf5c290, public0.10 period3dcccccd. The engine now retains both scalar deadline
cursors and deferred Move handoff through Save81. See [payoff51](retail-pathfinding-engine.md#moving-radius-changes-retain-point-motion-and-scalar-owner-deadlines).
This closes bounded runtime footprint publication and positive periodic producer
integration; general timer domains, shared maximum-radius mutation, passages and
other task/locomotion families remain open.


Payoff52 separates unbound group live maximum from retained path footprint after
public largest-peer growth/shrink/removal. The complete three journeys match831
engine motion commits,730 owner states and2207 Save81 suffix commits per
variant. The generic owner begin phase publishes counter and player-row budget
before standalone movement callbacks. Ghidra now retains path+b4 and the complete
168be0 setter ABI. Bound shared7c and wider target/task families remain open.
See [the engine/capture evidence](retail-pathfinding-engine.md#local-group-maximum-and-retained-route-footprint-have-separate-lifetimes).

Payoff53 completes the ordinary public blocked-terrain point Move lifetime.
Preserving the clicked waypoint, submitting the selected fine goal unchanged,
and carrying retry/forced-arrival state through the angular gate makes207
normal engine commits exact against two retail captures. Four Save82
continuations match46 suffix commits. Ghidra's existing route/retry/step/cleanup
roles carry the observed1261..1264 owner sequence; saved readback verifies all
546 roles and the existing44 partial layouts/247 ABIs. See [engine payoff53](retail-pathfinding-engine.md#blocked-point-goals-retain-the-click-through-retry-and-forced-arrival).
Outside/overlap and other task/mask/class domains remain open.


Payoff54 completes ordinary ground outside point-Move clipping. Public tasks
retain the raw click; original routing uses world bounds inset by cell-size
times four. Repeated public edge neighbors,108 original prefix/C cases and
all191 outside-west engine commits agree, including47 saved suffix commits.
Seven additional saved roles connect four public wrappers, dispatch and point
construction; four explicit stack ABIs raise the persisted total to251. See
[engine payoff54](retail-pathfinding-engine.md#outside-point-goals-clip-routing-while-retaining-the-public-click).
Other placement, target, class and mask domains remain open.


## Captain AI home and membership producers

The engine now preserves authored home, roster and current goal over
InitAssault, reads the native formation flag for CaptainIsFull, and sends
new recruits toward an authored home through Move. Six repeated native
captures retain the full178-commit recruit journey and establish sticky
shortfall/InitAssault retry flags. Only the first33 ordinary engine commits
and48 saved admission suffix commits are exact; the native membership/shared
task replacement at2s remains open. See [captain engine evidence](retail-pathfinding-engine.md#captain-home-recruitment-and-formation-retries-reach-move).

Native9c7b10 ORs flag6c bit1,9b8a20 returns that bit,9c3550 clears it on a
shortfall, and9d9020 delivers a membership-range enter callback through
9d4aa0. That callback updates c4 and rebuilds private point/shared requests
through9d4600/9d44d0/9d16c0/9d27c0. It is not evidence for an arbitrary
one-second engine retry timer. Virtual captain pathing category2 and radius0
must remain distinct from ordinary unit profiles. Default homes come from
an AI town object, and CaptainGoHome retains moving virtual actors and their
follower tasks. Both producers and their whole engine lifetimes remain open.

### Stationary captain membership timer and virtual occupancy (payoff56)

The two/farther source controls recover `063560` world/fine radius publication,
`15f210` retained range scans, `15eac0` strict predicted circle admission and
`9d9020` private task handoff. The captain's four listeners retain actual roster
range producers and creation-phase periods; exact timer deadlines execute after
a due owner. A zero-radius category2 virtual actor still owns a single fine cell
and remains a blocker after the follower switches to a point task. Original
blocker capture identifies that same canonical owner rather than inferring it
from the stop position. Ghidra now saves590 descriptive roles,48 partial
layouts/307 fields,260 explicit ABIs and50 globals. Complete production movement
and save/lifetime payoff is recorded in [engine integration](retail-pathfinding-engine.md#stationary-captain-range-callback-and-zero-radius-occupancy).
Larger rosters, moving actors, default town-home producers and general bot
restoration remain separate tasks; this stationary motion contract is bounded.


Payoff57 [ports the stationary two-recruit captain journey](retail-pathfinding-engine.md#stationary-captain-pair-private-followers-to-shared-arrival):
369 literal physical commits, repeated full native phase hash, actual850-world
membership circle, two callbacks before one shared batch, and152 shared7c/pathb4
footprint publications. Native AI pool admission is newest first and captain
roster attachment prepends. The virtual actor is excluded as a blocker but stays
a fine-search target region; restoring that distinction fixes the first divergent
heading at1170ms. Save84 retains roster/entry state; Stop synchronously detaches
physical owners. Ghidra591 roles preserve these findings. Larger/mixed cohorts,
forced pair retries, owned-list reuse and moving/default-home captains remain open.


### Mixed captain pair and blocked authored home

[Payoff58 engine integration](retail-pathfinding-engine.md#mixed-captain-pairs-and-blocked-home-retries)
retains complete mixed369 and blocked524 physical commits, each repeated in two
owned captures. A mixed roster publishes shared32 then live31 with cached32.
Blocked-home placement admits the category2 actor beside the obstruction;
166c30/15d360 temporarily clear rounded source/target rectangles for coarse
admission. Correcting these producers lets existing fine/retry/arrival kernels
reproduce both natural retry budgets, final singleton retry and failure cleanup.
Larger batches, moving captains, pool reuse and general dynamic coarse publication
remain distinct open requirements.
