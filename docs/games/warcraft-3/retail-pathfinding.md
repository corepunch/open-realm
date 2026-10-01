# Retail Warcraft III pathfinding

## Objective and status

Recover the complete retail behavior, verify each mechanism against original
code, compose those tests into full movement scenarios, then replace
OpenRealm's pathfinding with an implementation that passes the same corpus.
Algorithm labels are descriptive; behavioral parity is the completion criterion.

**Current state:** substantial mechanism coverage; incomplete full-lifecycle
composition. **362 game functions annotated. Incremental scalar, velocity, stock-turn, idle-unit routing, nearest partial routes and point Move arrival implemented;
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
  `MapPathfindingTypes.java` also persists31 partial layouts,177 verified
  fields and175 instruction-established prototypes with explicit register/stack
  storage. Its schema is
  [`retail-pathfinding-types-1.27.json`](../../../tools/ghidra/fixtures/retail-pathfinding-types-1.27.json).
  Unassigned bytes remain undefined; `Prefix` lengths are verified extents,
  not claims of complete class size. Scalar words use a uint32 typedef so
  decompilation cannot imply host float arithmetic. The hash/base guards run
  before mutation; incompatible existing layouts/names are preserved by refusal.
  Applying and rerunning the script, then saving `game.dll`, produced the
  layout/prototype readback `num-01.16-ghidra-types.json` under the report root.
  Pass the absolute schema path and optional metadata report path as script args.
  460 function names/comments applied and saved. Latest public toggle readback:
  `runtime/pathing-toggle-ghidra-readback-261001.json`. Names are recovered roles, not original debug symbols;
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
**NUM-01.2/12**; the backlog names their dependencies and
finish artifacts. The areas below describe the sequence, not additional tasks.

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
