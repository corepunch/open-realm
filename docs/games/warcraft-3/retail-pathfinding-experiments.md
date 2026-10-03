# Retail pathfinding evidence: Retail experiments and Way Gates

[Contract, current ledger and reproduction](retail-pathfinding.md).
Retail **1.27.1.7085** only; addresses and report paths use the conventions there.

## Runtime witnesses and reproduction

`tools/frida/trace_wc3_pathfinding.py` and `wc3_pathfinding.js` provide bounded,
build-checked search/request/map observers. Counters include unsampled calls;
node histograms and detailed search records are capped. They do not write
pathfinding memory or call gameplay functions. `analyze_pathfinding_trace.py`
rejects incomplete captures, inconsistent layouts and observer errors.

The untouched Human02Interlude ran on an isolated Wine prefix/display/server
(port 27046). Existing analysis sessions on 27043/27045 were left running.
The 180-second control (`runtime/interlude-control-180.jsonl`) recorded:

- 18 fine searches across four sampled paths, budget 700. Five negative results
  stopped at 701 pops: budget exhaustion, not proven disconnection.
- 11 accelerated searches across five sampled paths, budgets 400/5000, no
  negative results. Summed node levels: 35 at level 0, 56 at level 1, 35 at level 2.
- 19 accelerated requests, showing that a request need not execute a search.
- 22 hierarchy update entries, including mode-1/mode-0 pairs around requests.

The first 60-second attempt only captured loading/map creation. Earlier passive
8-second attaches likewise observed no searches. Those are setup witnesses,
not evidence that retail does not route. The loading screen requires a key;
all capture conclusions must include script progress and successful completion.

`make_wc3_pathfinding_map.py` creates new copies of the interlude and replaces
only `war3map.j`; terrain, pathing and map-info members were byte-compared after
repacking. It disables the original unit creation and campaign triggers, adds
one Footman, and records a fixed 30-simulation-second route. The four scenarios
are open, preexisting wall, insertion at tick 20, and removal at tick 50. Every
scenario issues one move at tick 10 and samples positions every 0.1 seconds.
The barrier is four 32-unit cells across the same corridor.

The open control completed at `(-1936, -157.065)` for requested goal
`(-1936, -144)`, with constant X, two accelerated searches and one fine search.
It stopped with current order zero. Target-arrival predicates are covered in
[route evidence](retail-pathfinding-routes.md#target-arrival-range-and-heading);
point-order parameter producers remain incomplete.

The first four controlled captures all contain 300 position samples, accepted
orders, matching terrain native calls and completion markers:

| Scenario | Accelerated searches / pops each | Fine search pops | Maximum X | Observed result |
| --- | --- | --- | --- | --- |
| Open | 2 / 4, 4 | 27 | -1936.000 | Straight route |
| Wall before initialization ends | 2 / 10, 10 | 144, 36, 30 | -1844.502 | Detour; hierarchy uses level-zero nodes |
| Insert at tick 20 | 2 / 4, 4 | 27, 43, 30 | -1844.873 | Original coarse route, later fine repair and detour |
| Remove at tick 50 | 2 / 10, 10 | 144, 36, 30 | -1844.502 | Matches wall run; edit occurred after passing barrier |

All four recorded four hierarchy updates total: two full initialization updates
and a temporary exclusion/restore pair at order time. No later update or
accelerated search followed the timed insertion. Thus **this capture** demonstrates
fine-route repair without an observed coarse rebuild. It does not establish all
terrain-edit invalidation behavior or the contents of untouched coarse cells.
Late removal is only a control. The early-removal, fresh-order and watched-cell
experiments below distinguish route caching from stale hierarchy state.

The requested movement speed was 100, but sampled straight-line travel was
approximately 150 world units per timer second. Do not use requested speed as
measured speed or infer pathing tick frequency from it; speed clamping and
simulation timer cadence need separate verification.

On this 1.27 installation, a copied map installed under `Maps/` and passed as a
relative Windows path loaded successfully; an absolute `Z:` map argument
returned to the menu. Use the validated relative form. The source ROC map is
in lowercase `war3.mpq` on this installation, not the 1.29 archive layout.

```sh
python3 tools/frida/make_wc3_pathfinding_map.py \
  --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --scenario open --output /tmp/PathProbe-open.w3m
# Install the new copy under the retail Maps directory without replacing originals.
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python \
  tools/frida/trace_wc3_pathfinding.py \
  --data /run/media/lofcz/ssd_external/Games/w3 \
  --map 'Maps\PathingRE-Open.w3m' --seconds 130 \
  --continue-at 80 --x11-display :96 --output /tmp/path-open.jsonl
python3 tools/frida/analyze_pathfinding_trace.py /tmp/path-open.jsonl \
  --scenario open --require fine --require acc --require hierarchy
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/verify_wc3_pathing_cells.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --report /tmp/path-parent-oracle.json
python3 tests/test_frida_pathfinding_trace.py
```

The controller terminates only its own spawned process; `--pid` attaches without
terminating that process. Loading-key automation is restricted to owned spawns
and requires an explicit isolated X display. Preserve the metadata, final
counters, markers and scenario/map hashes alongside any trajectory comparison.

## Capture interruption record

`runtime/remove20.jsonl` ended with a destroyed observer before movement;
`runtime/remove20-retry.jsonl` ended when the loading-key command could no longer
open the isolated X display. Neither is trajectory evidence. The latter run
had reached the loading screen and script initialization. The isolated X server
was confirmed terminal before restarting; existing unrelated Wine sessions
were preserved. The subsequent attempt uses display `:94` with `Xvfb -noreset`
and output `runtime/remove20-display94.jsonl`. That attempt reached tick 300 with current order zero. It observed two
accelerated searches (10 pops each), one fine search (144 pops), and no search
after removal at tick 20. Both accelerated buffers contained six points and
the fine buffer 27, in reverse order; fine consumption started at index 25.
The unit retained its detour after the barrier opened, reaching
`(-1933.346, -155.312)`. The fresh-order variant below distinguishes route caching from stale hierarchy classifications. Final observer counters match these counts, and the scenario/route analyzer
passed with all 300 samples and three valid route dumps. Summary artifact:
`runtime/remove20-summary.json`.

## Fresh order after terrain removal

`remove_reorder` extends the early-removal experiment: removal at tick 20,
accepted Stop then accepted Move at tick 21, same requested goal. The complete
capture is `runtime/remove-reorder20-retry.jsonl`; its scenario and route checks
passed (`remove-reorder20-summary.json`). The first attempt was interrupted
while loading and contains no completion evidence.

Before removal both coarse routes had six points and ten pops; the fine route
had 27 points and 144 pops. After the fresh order, two new coarse searches used
nine pops and produced six-point detours with the **same five destination-side
points**, changing only the start. A new fine search used 22 pops and produced
a 22-point route through the open area; its consumer index immediately became
zero. The unit arrived at `(-1935.408, -156.737)` with current order zero.

There were six hierarchy updates total: two initialization updates and two
request-local exclusion/restore pairs. No update of the barrier area followed
the edit. These observations distinguish cached route reuse from the result of
a genuinely new coarse search. They support a stale coarse classification while
the fine search sees the changed terrain. The direct cell witness below establishes which classifications persist.
The complete set of producers that refresh them remains unresolved.

The temporary Wine prefix disappeared during the interrupted environment.
The successful retry used a separate persistent copy at
`/home/lofcz/.local/share/open-realm/wine-pathfinding-re`, display `:94`, and
Frida port 27046. Existing unrelated prefixes were not changed.

## Direct terrain/hierarchy divergence witness

The observer accepts `--watch-cell X Y`, in fine-grid coordinates. At each
non-sample scenario marker it snapshots that cell and parents at
`(X >> (level+1), Y >> (level+1))`. Bounds, complete levels, stored words and
lane decoding are checked by the analyzer. Reads do not alter target memory.
For this map, world `(-1936,-560)` lies in fine cell `(163,78)`.

`runtime/remove-reorder20-cells.jsonl` repeats the accepted Stop/Move test with
`--watch-cell 163 78`. The watched fine word is `0x49001b76` before the initial
wall, becomes `0x4b001b76` when blocked, and returns to `0x49001b76` on removal.
Thus the edit changes `0x02000000`, retaining its low occupancy-list bits.

The level 0/1/2 classification words begin at `0x04000000` before the wall and
become `0x84000000` after the subsequent initialization rebuild. Their selected
walk lane changes from clear (00) to mixed (10). After removal, and through
Stop/Move acceptance, all three retain `0x84000000` even though the fine word
has returned to its original value. Level 3 stays `0xa4000000`, already mixed
because of the surrounding map. This directly demonstrates divergence between
the edited fine cell and its hierarchy in this native/script sequence.

The tick-300 completion snapshot retains those same words. The capture and
all 11 cell snapshots passed analysis; the summary is
`runtime/remove-reorder20-cells-summary.json`. Thus the mismatch persists
through the complete 30-second scripted observation. Other producers may refresh the region;
this is not a claim that the hierarchy can never be rebuilt after terrain edits.

## Gate experiment fixture

`gate` and `gate_off` create the retail `nwgt` unit at `(-1936,-800)`, assign
exit `(-1936,-240)`, and differ only in activation. The Footman starts at
`(-1936,-976)`, inside the authored 400×400 entry rectangle; this isolates
ordinary-Move traversal before testing approach from outside. Both use the
same Move order and destination as the corridor controls. Gate state is checked
with `WaygateIsActive` and recorded, while Frida observes `6f168f00` entry/return.
A true activation marker alone does not prove traversal. `--require gate`
requires a successful observed traversal; failed/incomplete calls are counted
separately from active-state markers.

## Active gate runtime result

`runtime/gate.jsonl` completed all 300 ticks and passed `--scenario gate
--require acc --require gate`. It observed two accelerated searches (two pops,
eight nodes each), no fine searches, and one successful `6f168f00` traversal.
Both reverse routes were exactly:

```text
(81.75, 45.75), (-128000.0078125, 1), (81.75, 32.75)
```

The middle entry is the sentinel and gate ID 1. At the gate centre watch cell,
base metadata contains ID 1 at byte +6 and parent classifications are mixed,
matching the recovered source-stamping mechanism. An ordinary `move` order
therefore demonstrably consumes a Way Gate special edge in this build.

The unit was at start `(-1936,-976)` at tick 10, at requested Move goal
`(-1936,-144)` at tick 11, and idle there at tick 12. The configured gate exit
was `(-1936,-240)`, 96 units short of the goal. Do not report that capture as an
exact landing at the configured exit. The reconstructed coarse route places the
exact requested endpoint beside the sentinel. The longer exit-to-goal and
outside-entry tests below record the traversal argument to distinguish the
requested landing from subsequent movement within the sample interval.
`PathAcc_Reconstruct` unconditionally replaces its first and last point with
exact goal/start after emitting the parent chain and gate sentinel. That is a
concrete mechanism which can place the Move goal beside a gate edge. The
observer records the incoming XY argument to `6f168f00`.
Copies `PathingRE-gate-tail.w3m` (exit Y=-432) and
`PathingRE-gate-approach.w3m` (gate centre Y=-640) isolate the longer tail and
outside-entry cases. Their builder manifests retain both configured Y values.

Assembly resolves the traversal target selection: `6f165d55..6f165d79` reads
`coarseBuffer[currentIndex-2]`, scales it by the base accelerator map's scale
through `6f04b7f0`, and passes that fine-grid point to `6f168f00`. It checks the
warp record's active bit but does not reload its destination XY at this step.
Thus the landing request comes from the reconstructed route, which may already
contain the exact Move goal in place of the reached node. The probe's
`gate-traversal.destination` is in fine-grid coordinates, not world coordinates.
This also motivates a retarget-mid-route test: activation is rechecked, but a
changed exit might require route regeneration to affect traversal.

The inactive fixture `runtime/gate-off.jsonl` also completed and passed
validation. It produced two ten-pop coarse searches with 34 nodes and ten-point
routes without sentinels, followed by one 27-pop fine search. There were zero
traversal calls. The unit walked to `(-1936,-157.065)` and stopped, matching the
open-ground baseline endpoint. The inactive gate still stamps its entry cells,
so the coarse search visits more level-zero nodes than the no-gate baseline;
disabling the edge does not erase the source marker. Summary artifact:
`runtime/gate-off-summary.json`.

## Longer gate exit-to-goal test

`runtime/gate-tail.jsonl` uses the same source but exit `(-1936,-432)`, leaving
288 world units to the requested goal. Both coarse routes contain four entries:
exact goal `(81.75,45.75)`, landing waypoint `(81.75,41.75)`, sentinel/ID 1, and
exact start `(81.75,32.75)`. The traversal observer received fine-grid
`(163.5,83.5)`, corresponding to world `(-1936,-400)` with this map origin.
It returned success, then the mover requested a nine-point fine route from
that landing point to the goal. Thus the longer route keeps a separate landing
waypoint and resumes ordinary fine routing; the landing request is 32 world
units beyond the configured exit because it uses the reconstructed grid point.
The completed capture passed gate/fine/accelerator and scenario validation
(`runtime/gate-tail-summary.json`). At tick 11 the mover was already at
Y=-392.500, consistent with movement after the requested landing; by tick 30
it was idle at Y=-157.013 and remained there through tick 300.

## Outside-entry approach and live gate changes

`runtime/gate-approach.jsonl` places the gate centre at Y=-640 and retains
configured exit Y=-240. The initial mover is 136 units below the authored
entry rectangle. Both accelerated searches take three pops and produce:

```
goal (81.75,45.75), sentinel/ID 1, approach (81.75,34.75), start (81.75,32.75)
```

A five-pop fine search routes to `(163.5,69.5)` (world Y=-848). The unit walks
normally through tick 16 (Y=-889.005), then `6f168f00` requests fine-grid
`(163.5,91.5)`, the exact Move goal. Tick 17 reports Y=-144, and tick 18 is idle.
The completed trace passes gate/fine/accelerator validation. This establishes
outside-entry approach but does not yet establish the exact gate activation
distance: cell rasterization and waypoint acceptance must both be accounted for.

The matched `gate_retarget` and `gate_disable` fixtures change state at tick 15,
after route construction and before the baseline's traversal. The analyzer
requires a cached special-edge route before these edit markers and rejects
captures with traversal before the edit completes.

`runtime/gate-retarget.jsonl` completes all 300 ticks and passes those timing
checks. The script calls `WaygateSetDestination` with Y=-432 at tick 15, while
the mover is at Y=-904.004. It still traverses between ticks 16 and 17 using
fine-grid `(163.5,91.5)`, reaches the original Move goal, and becomes idle at
tick 18. Search counts, route points, and sampled positions match the baseline;
there is no new search after retargeting. This agrees with the consumer's use
of cached route XY rather than a fresh read of the warp record's destination.
It does not establish what a newly issued order would do after the edit.

`runtime/gate-disable.jsonl` also completes and passes timing/scenario checks.
At tick 15 the script deactivates the gate and verifies `WaygateIsActive` is
false. No traversal call occurs. The two original three-pop accelerated
searches are retained; after approaching the entry waypoint, the mover performs
one additional 24-pop fine search to `(163,91)`. It continues walking through
tick 17 (Y=-874.006), becomes idle at tick 65 at Y=-157.065, and remains there.
This matches `6f165d10` skipping the two special-edge entries when the record
is inactive, then normal fine routing toward the retained goal. No coarse
regeneration or hierarchy update is observed after deactivation. These tests
use a walkable direct route; skipping an edge over impassable terrain remains
untested.

## Special edges are Way Gate records

The accelerator's `+0x30` container has RTTI
`NTempest::CDynTable<NIpse::CPaWarp>`, allocated by `6f14f280` for 256 twelve-byte
records. Data is at accelerator `+0x3c`. Each record contains a flags word and
integer destination XY; bit 0 is active. Zero is reserved as no special edge.

The identity is established by executable producer chains, not the name alone:

| JASS entry | Ability / map bridge | Accelerator mutation |
| --- | --- | --- |
| `WaygateActivate`, native `6f2193e0` | `6f43b840` → `6f04e210` → `6f15cd40` | `6f164b70` toggles flags bit 0 |
| `WaygateSetDestination`, native `6f219500` | `6f4319c0` → `6f04e550` → `6f15cd50` → `6f164ba0` | `6f164bf0` writes integer XY |

Native registration at `6f20a0d7/6f20a0eb` pairs those function pointers with
strings `6fa99274/6fa9928c`. The ability cleanup chain `6f3f53b0` → `6f43b8b0`
reaches the same deactivation mechanism; its vtable is `CAbilityWarp` at
`6fad20f8`. Map bridges reject IDs outside 1..255. `6f04e210` also maintains
an active-record count at map owner `+0x54`.

`PathAcc_ExpandBaseNode` reads the source cell's nonzero edge ID, checks the
record's active bit, and offers an edge to the recorded destination when it
differs from the current cell. `6f1653a0` resolves that destination cell and
`PathAcc_RelaxSpecialEdge` adds cost 1. Reconstruction stores a sentinel and edge
ID in the reverse route. Consumer `6f165d10` rechecks active state through
`6f165200`, resolves the destination, calls `6f168f00`, and removes two route
entries after successful handling (or skips them when the record is inactive). `6f168f00` seeks an acceptable destination
through `6f16ec00` before applying the position change.

Source setup `6f41c270` allocates an ID through `6f04e510`, storing it at
ability `+0x6c`. The allocator scans availability bytes beginning at ID 1,
marks the first free byte, and returns zero on exhaustion. Source dimensions
come from authored DataA/DataB into ability `+0x74/+0x7c`; each is multiplied by 0.5 (`6fcd53f4`, calls at `6f41c2cf/6f41c315`)
before constructing the centre-plus/minus-extents rectangle. In this ROC
archive, `Awrp` has `Data11=400`, `Data12=400`, and `nwgt` lists `Awrp,Avul`
in UnitAbilities. This is a 400×400 entry rectangle, not a 400-unit half-width.

The rectangle bridge is `6f04e360` → `6f15c000` → `6f15bf60` → `6f15c030`.
The latter writes the ID byte at base accelerator cell `+6`; `6f15bf60` then
rebuilds three parent levels. Cleanup `6f43b8b0` passes zero (confirmed by
`XOR ECX,ECX` at `6f43b955`) to erase the source rectangle, then calls
`6f04e4c0` to free the allocation byte and deactivate the record. This connects
the reducer's special-marker rule directly to preserving gate entry regions.
Overlapping source rectangles and zero-ID exhaustion behavior still need tests.

Controlled ordinary Move traversal and an inactive control are documented above.
Destroyed gates, overlapping sources, and unreachable exits still need runtime
tests; the observed Footman case does not establish every mover's eligibility.

## Cinematic experiments

Reuse [retail-camera-tracing.md](retail-camera-tracing.md): extract an untouched
control, replace only `war3map.j` in a copied map, preserve the proven MPQ
wrapper/member handling, and use `PreloadGen*` for bounded file-backed JASS
records. The original workflow used **1.29.2**; the untouched control and isolated mover
also run on **1.27.1.7085** with the tools above.

First audit the chosen scene's orders, `SetUnitPathing`, pause, speed changes,
teleports and per-tick `SetUnitX/Y` calls. Script-driven coordinate assignment
cannot demonstrate path-search behavior. Keep a baseline of the original
scene, then a controlled variant with one issued move order and no competing
cinematic orders during the measurement interval. Track start, requested goal,
current order, position, facing, alive/paused state, and explicit obstacle edits.
Flush on completion, skip, and a bounded timeout; do not log every render frame.

| Experiment | Hold fixed / vary | Evidence sought |
| --- | --- | --- |
| Open ground versus long wall | Same mover, endpoints and order count; add one barrier | Which search runs; node levels, pop counts, path-buffer output |
| Translate the same obstacle course | Preserve geometry; shift relative to hierarchy cell boundaries | Alignment-dependent level promotion/splits; trajectory alone cannot prove hierarchy |
| Narrow corridor sweep | One mover at a time; vary authored collision size and corridor width independently | Fine footprint case, accelerated size class, rejected edges; distinguish discrete classes from continuous clearance |
| Static versus live blocker | Match occupied geometry; separately use terrain/pathing blocker, building, idle unit, moving unit | Which map changes, fine object-list queries, local replans and separation |
| Insert/remove blocker mid-route | One timed edit between identical runs | Local hierarchy update versus full rebuild; stale waypoint invalidation |
| One unit versus a formation | Same goal, then nearby distinct goals; separate selection group from individual orders | Per-mover searches, shared structures, formation endpoint changes, admission limits |
| Disconnected destination | Barrier versus merely very long detour; repeat with different work pressure | Exhaustion versus budget stop; partial route and retry behavior |
| Pathing enabled/disabled | Same scripted scene and units | Identify the bypass; disabled-pathing success says nothing about normal search quality |

The retail runtime is needed here because OpenRealm tests cannot observe the
proprietary search graph or its scheduling. Once behavior is measured, encode
the relevant scenario in our pathfinding/movement regression harness before
changing production routing. This investigation changes no routing behavior.

## Live point-task acceptance and arrival

`runtime/task-open.jsonl` (130s owned spawn, copied `Maps\PathingRE-Open.w3m`,
display`:94`, loading key80s) completes all300 samples. Analyzer passes
`--scenario open --require task --require fine --require acc --require hierarchy`;
`task-open-summary.json` records zero violations. `task-open-provenance.json`
records binary/map/controller/observer SHA256 values and options.

Read-only `--task-events` hooks observe one accepted **`d016b` CTaskPoint**,
vtable RVA`b78ec0`, task identity`[1271,1275]`, successor`[1270,1274]`,
goal`(-1936,-144)`, range0. User-order identity`[1261,1268]` is distinct.
Acceptance retains the task head, sets ability-active bit4 and clears unit
dispatch bit1. Arrival subsequently leaves **both queues empty**, clears
ability-active bit and leaves dispatch bit1 set. Final sampled position is
`(-1936,-157.065)`, user order0. This live ordinary-order path complements the
emulated next-`d016c` acceptance fixture; it does not prove queued-order,
retry/cannot-path or crowded behavior.

`analyze_pathfinding_trace.py --require task` requires observed acceptance;
33 trace regression tests include rejecting an accepted task with dispatch bit
still set. Records are capped by the observer sample limit and final counters
are retained; absence beyond that cap cannot establish non-execution.

## Live Stop/Move replacement and task cleanup

`runtime/task-reorder.jsonl`: copied `PathingRE-RemoveReorder20.w3m`, 130s,
loading key80s, display`:94`, `--task-events`. All300 samples and scenario
markers pass; required task/fine/accelerated/hierarchy witnesses pass with zero
violations (`task-reorder-summary.json`). Exact tools and hashes are preserved
in `task-reorder-tools/` and `task-reorder-provenance.json`.

Observed **31 task prepends, six cleanup calls, two accepted d016b tasks, one
arrival**. Both accepted task heads differ from their associated user-order
heads. One cleanup starts with active bit4, clears it, and **retains the task
head**; subsequent processing replaces the queues and accepts the second task.
Final arrival empties both queues. Position ends`(-1935.408,-156.737)`, order0.
No cannot-path/recovery call was observed; final counters are below every task
sample cap. Thus this is replacement/cleanup evidence, not failure recovery or
Shift FIFO evidence. The analyzer checks prepend links and acceptance dispatch
bits; its 34 regression tests pass.

## Live blocked-goal recovery task sequence

`runtime/task-blocked-goal.jsonl`, copied `PathingRE-BlockedGoal.w3m`, completes
300 samples and passes scenario/task/retry-exhaustion/fine/accelerated checks
with zero violations. Summary and exact tool snapshots/hashes use the same
`task-blocked-goal` prefix. Final task counters:17 prepends, three cleanups,
one accepted point task, **one cannot-path, one recovery and one arrival**;
all below their sampling caps. One retry exhaustion/forced-arrival witness is
also present; that does not imply the cannot-path handler was bypassed.

Final three prepends are `d0162 [1272,1285] → d0144 [1273,1286] →
d0144 [1274,1287]`, each linking to the previous head. Thus the queue before
arrival is reverse insertion order, matching static`5fb190` fallback.
Arrival begins with head`[1274,1287]`, user order`[1261,1268]`, active bit4;
returns with both queues empty and active bit cleared. Final position
`(-1924.026,-238.746)`, order0, remains away from the blocked requested goal.
This proves the observed fallback, not all target-specific recovery branches
or the semantics of every inserted task. The 35 analyzer regression tests pass,
including task-record counts versus bounded final counters.

## Controls without an attached observer

`tools/frida/control_wc3_pathfinding.py` uses Frida only for owned
spawn/resume/kill: no attach, injected script or interceptor. The copied map
writes its normal `PreloadGenEnd` output into `CustomMapData/pathtrace-*.txt`.
The controller preserves/removes old output before launch, requires ordered
samples1–300 plus terminal completion, and compares exact marker strings.
Binary/map/reference/controller hashes and the controller source accompany
raw output and comparison. Positions are JASS `R2S` (three decimals), not
internal float bits; simulation tick labels are compared, wall-clock timing
and unobserved state are not.

First result: `runtime/control-open-noattach/` matches **all304 markers** in
`task-open.jsonl`, including300 position/order samples and final arrival.
Reference map hash `c0b35ff5e4f859cb672373fc13b893103013ca58265c5c2113e8e1763ca9c0aa`
was independently checked after this initial run; subsequent controller
versions require the reference provenance hash before spawning. The blocked-goal control `runtime/control-blocked-goal-noattach/` also matches
all305 markers in `task-blocked-goal.jsonl`, including its short-of-goal stop.
The replacement control `runtime/control-reorder-noattach/` matches all311
markers in `task-reorder.jsonl`. A second open run
`control-open-noattach-repeat/` again matches all304 markers. Blocked/replacement
currently have one control each; these observations are not general proof that
instrumentation is harmless.
Three asset-free control-parser tests reject missing, duplicated, reordered
samples and absent completion; the35 observer-analyzer tests also pass.

```sh
/home/lofcz/.local/share/uv/tools/frida-tools/bin/python tools/frida/control_wc3_pathfinding.py \
  --data /run/media/lofcz/ssd_external/Games/w3 \
  --map 'Maps\PathingRE-Open.w3m' --scenario open \
  --reference /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/task-open.jsonl \
  --output /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/control-open-repeat
```

Defaults:130s, loading key80s, isolated display`:94`; output directory must be
new. Do not run two captures on the same display concurrently.


## Pursuit with fog loss and reacquisition: current observer

`runtime/task-follow-fog-reacquire.jsonl` uses the current read-only observer,
`--task-events --samples 2000`,130s, loading key80s and display`:94`. Map
`PathingRE-FollowFogReacquire.w3m` SHA256
`61f3c10f8a61fc4c702f7f51a2579eba702b3298c61ed00b36e00acc08673a6a`;
exact tools and provenance use the same capture prefix.300 mover and300 target
samples complete. Analyzer `--scenario follow_fog_reacquire --require fine
--require acc --require refresh --require arrival` passes with zero violations.

Visibility blocks after tick60 (group countdown7, missed0), then reacquires
after tick76 (countdown0, missed53). No hidden-destination publication occurs;
one narrowed-arrival witness is present. Task counters:21 prepends, two
cleanups, one arrival; no point-task handler acceptance, cannot-path or recovery
was observed. This is a target-order path, not another point-task test. Final
target`(-1936,112)` is visible. Initial requests for `target-loss` and
`target-perimeter` witnesses failed and are retained in
`task-follow-fog-reacquire-unmet-requirements.json`: this run does not establish
those distinct cleanup/perimeter branches.


`runtime/control-follow-fog-reacquire-noattach/` then repeats the identical
hashed map without attach/script/hooks and matches **all610 marker strings**:
300 mover samples,300 target samples and10 lifecycle/visibility markers.
Controller/reference/map/raw-output hashes and source snapshot are retained.
This extends observer-effect controls to target visibility changes and
reacquisition at JASS sampling precision; internal state and wall-clock timing
remain outside the comparison.


## Live building footprint creation and destruction

Scenario19 `widget_lifecycle` requests creation of one Human Farm`hhou` at`(-1936,-560)`
after the start marker, issues the ordinary Footman move at tick10 and calls
`RemoveUnit` on the Farm at tick50. Only the copied map's`war3map.j` changes.
`--widget-events` enables read-only entry/return hooks on
`6501a0/650c00/6514d0/6544f0`, armed at this scenario's start marker; each method
records widget/class, collection before/after and refresh gate. Counters/caps
are checked; `--require widget` needs creation and later destruction of the
same widget/collection with the latter clearing its pointer.38 analyzer tests
include identity/order mismatches, missing method records and scenario markers.

`runtime/widget-lifecycle.jsonl` completes300 samples, one accepted point task
and arrival; task/fine/adaptive/hierarchy/widget requirements pass with zero
violations. Actual CUnit vtable`b77eb0`: one`6501a0` return changes collection
0→nonzero with gate0 during initialization; tick50 `650c00` returns with the
same collection cleared under gate1, followed by two empty-collection destroy
returns. No`6514d0/6544f0` return was observed in this scene. This crosses the
emulator's Storm-free boundary in the running game; it does not measure heap
accounting, decode semantics or every destroyed region's state. Exact source,
map and tool hashes accompany the capture; `analysis_used` records the final
38-test analyzer separately from the initial launch snapshot.


The first watched repeat (`widget-lifecycle-cells.jsonl`, cell163,78) passed
all scenario/method checks but saw no footprint change at the **requested**
location. Destruction's actual rebuild rectangle was YX`[92,154,97,159]`.
Do not infer missing invalidation from this wrongly located probe.

`widget-placement.jsonl` adds two `PATHWIDGET` JASS position records and watches
cell156,94 in a new copied map. Actual Farm position at creation and removal is
**`(-2176,-64)`**, proving relocation from the request. All300 primary samples,
widget/task/fine/adaptive/hierarchy checks pass. Coarse high bytes (levels0..3):

| Marker | High bytes |
| --- | --- |
| Before creation; after gate0 creation | `04 04 a4 a4` |
| After initialization rebuild / before removal | `54 a4 a4 a4` |
| Immediately after gate1 removal | `04 04 a4 a4` |

Thus this building removal restores sampled hierarchy classes immediately,
unlike the prior direct terrain-native edit. Fine word changes
`41ffffff → 41001ba7 → 41001bfc` show lazy list mutations; those low indices
alone are not an independent occupancy oracle. Exact comparisons are retained
in `widget-placement-cell-comparison.json`. Placement-search rules, resource
decoding and allocator accounting remain separate gaps.


`runtime/control-widget-placement-noattach/` repeats the final map without
attach/script/hooks and matches **all310 markers**, including both actual Farm
positions. Thus the observed relocation and sampled movement also reproduce
without the observer. Map/reference/controller/raw-output provenance is retained.
