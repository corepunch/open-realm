<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **MAP-04.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-MAP-04.2.json` -> [`MAP-04.2-expected.json`](../../../../../tools/ghidra/fixtures/research/MAP-04.2-expected.json) (uncompressed sha256 `94e1fd49e08eb9249d3ccc8f50ea57bd844d2f6fc0aa4c56c63f9f9c16fa3c18`, 36582 bytes)
> - `expected-oracle-MAP-04.2.json` -> [`MAP-04.2-expected-oracle.json`](../../../../../tools/ghidra/fixtures/research/MAP-04.2-expected-oracle.json) (uncompressed sha256 `d8c0c978c29d0bd893fe2d8882e45f6e7adfb29c8d93fa992b9c9ca356fc1438`, 6507 bytes)

# MAP-04.2 handoff — exclusion-scope exits, reentrancy and edit-during-request

**Status.** Established, not closed:
- **Instruction-level exit inventory** (recursive-descent CFG over objdump decoding, with switch tables and null guards resolved) for all 12 code sites that hold a pathing exclusion.
- **Original-code emulation** of every reachable exit of the two request scopes, plus their pre-acquire denial exits: 8 cases, each with a restoration assertion.
- **Original-code and live-retail evidence for the edit-during-request case.** A pending fine terrain edit is published by the request's own restoration, inside the rounded self/target coverage only. Oracle: 18 cases. Live: 1 cell published and 6 left pending, in 2 repeats.

Not established:
- Static closure of the stop-recovery/embedded-recovery scopes. They contain about 110 unresolved virtual calls; only live evidence covers them.
- Portal scope `16ec00` beyond static analysis (one unresolved context callback; no portals in the live maps).
- Whether any public caller can pass a stale handle to `059590`, which would fault.

**Main conclusions:**
1. **No pathing request scope can be interrupted by a map edit.**
   - The fine, coarse, visible-waypoint and next-step scopes reach no map writer and no other exclusion scope. Their closures contain no unresolved indirect call. The only imports are memmove/memset, Storm 401/403/405, and /GS-failure process termination.
   - Live, no writer ran inside any of 3×(3 fine + 9 coarse) requests or 3×442 next-step scopes.
2. **"Edit during a request" is therefore an edit between request phases.** Because restoration rebuilds from current fine cells, the request publishes pending edits under its rounded coverage early.
3. **Every reachable exit of a held scope occurs after its release.** The compiled C++ unwind restorations are unreachable, because there is no throw source.

## Exit inventory (`scope-exits-MAP-04.2.json`, tool `verify_map04_2_scope_exits.py`)

| Scope (VA) | Acquire → release | Exits while held | Other exits | Exceptional path | Inside the scope | Obligation / evidence |
| --- | --- | --- | --- | --- | --- | --- |
| Fine request 166e90 | 166f66 esi, 166f7d edi → 167041, 167048 | none (null-guard skips 16703f/167046 infeasible: esi/edi preserved) | 166f56 interval or admission denial **before** acquire | FuncInfo 6fc55c7c unwind → 058740 target then self; no throw in 52-function closure | No writers or scopes | Assert counters equal at `ret` 167061; denial changes no counter. Oracle `fine_exact`, `fine_partial` (flag 10000000), `fine_interval_denied`, `fine_admission_denied`; live 6/6 |
| Coarse request 166c30 | 166d35, 166d4c → 166dd7, 166dee | none | 166c5a interval denial, 166cc9 admission denial (+80=0), both before acquire; **two post-release exits**: 166e2a exact endpoint, 166e7e partial (sets 20000000 and adjusted +24/+28) | No SEH frame; no throw in 89-function closure | No writers. Restore **re-reads** path+a0/+a4 and obj+1c. Closure stores at +a0 belong to the adaptive search object (162b33, 16423a, 1634b4), not the path | One assertion per exit: classes equal baseline after `coarse_exact` and `coarse_partial`; no 15d360 call on denials. Live 27/27 order-correct, cleared at search; restoration equal except the pending-edit publication below |
| Visible waypoint 167bf0 | 167c2a → 167cbd | none | — | Unwind state 0 | 20 functions, none writers | Counter equal at 167cd1 |
| Next-step blockers 166140 | 166265 → 1662dd | none (switch 166287 resolved to 4 class collectors) | — | Holder in arg slot [ebp+8] | 27 functions incl. 168360 (writes other paths' waits, not maps) | Counter equal at 1662f1; live 442 scopes per capture, LIFO |
| Portal point 16ec00 | 16ec34 → 16eca0 | none | — | Unwind state 0 | Callback 16ee00 resolved; its context callback 16ee19 `call eax` unresolved | Static only; GATE owns portal execution |
| Separation endpoint 16ee80 | 16ef24 → 16ef61 | none | — | 2 states (holder, 04b520) | 12 functions | Counter equal at 16ef75 |
| Embedded recovery 170080 | 170133 → 1701d2 | none | — | Unwind holder + 04b520 | **Moves its own spatial record while excluded** (14e770/14d960/160590). Placement callback 654060 (from 69a871/9d7625) or null; ~110 unresolved virtual calls | Counter-based exclusion, so the move does not change restoration. Live 24 per capture (3 captures), LIFO, word 2 at entry and leave |
| Group publish 16bcf0 | 16bd1d (169c50+16da60) → 16bd2a (early, returns -1 when a member conflict is counted) or 16bd90 (normal) | none | 16bd0e completion gate before acquire | No SEH | 163 functions; two CRT array-iterator callbacks unresolved. **No +38/+f4 store**, so the target region re-resolved by 16da60(0) is the acquired one. Release re-resolves members: a member dead before release keeps its +1 on a retired object | One assertion each for early and normal exit. Live 6 publish scopes per capture, 12 region toggles in pairs; member values not captured (MAP-04.3) |
| Stop recovery 05ca50 | 05ca68 → 05ca8f (05bd30) | none | — | **No SEH**: a fault inside would leak the +1 | Contains 170080 (same object, depth+1); mover notifications `[eax+20]`, `[edx+54]`, `[eax+50]` unresolved | Live 24 per capture, LIFO; nested under 651590 from 69a840 (depth 3) |
| Point query 04df50 | 04dffb → 04e03f | none (guard re-reads unchanged arg [ebp+c]) | — | SEH frame, but its unwind map covers only 04b520; **the +1 is not in the unwind map** | 4 functions | Unreachable leak; counter equal at 04e05d |
| Rectangle list 059590 | loops 059733/05976e → 059808/05983f | none (per-element predicates re-evaluated on unchanged inputs) | Exits 0598ea/0599ae after both restore loops | Unresolvable unit handle: 05971e/0597f3 `xor ecx,ecx` then `mov eax,[ecx+98]` = **fault** in exclusion or restore loop | 83 functions, no writers | Restore order = exclusion order (units then widgets, each descending), rebuild-from-fine. Callers: AI/abilities |
| 0599c0 | — | — | — | — | No references anywhere | Dead code |

Call sites resolved from instructions (in `RESOLVED`/`CONTEXT_CALLBACKS` of the tool):
- 14efa6 → 169840 (vtable 6fa90d64 stored by 14fca9).
- 14ec76 → 166060 (vtable 6fa91c40 stored by 165813).
- PathFine_FindPlacement callback → 654060 or null (stop paths) and 16ee00 (portal).

## Edit during request

| Evidence | Setup | Result |
| --- | --- | --- |
| Static | Closures of all four request scopes | No writer reachable, no unresolved indirect call: an edit cannot run inside a request (single call stack) |
| Live, 3 captures | Writers observed with the active scope stack | 0 foreign writers inside request scopes. All 7 `SetTerrainPathable` calls ran with an empty scope stack |
| Oracle (`expected-oracle-MAP-04.2.json`, 18 cases) | Original 054000 sets cell flags; this does not call 15d360. Then a complete 166c30 | Edit base cell inside the rounded self/target coverage: the request publishes it (e.g. (22,19) 0xff → base (11,9) AA, level-1 (5,4) AA; 0x02 → 80). Outside: stays pending (differs from a full publication only there). (A,–) at (22,19) stays pending |
| Live RS-MAP-04.2-target-edit, 2 repeats | Tick 9: 7 `SetTerrainPathable(…, walkability, false)` beside target rect (31,30,34,33). Tick 10: smart-target order | First coarse request (caller `FUN_6f167120`+158, target-only). Fine (31,35) → base (15,17): 0 → **80** (lane-0 mixed); parents (1,7,8), (2,3,4), (3,1,2) 0 → 80. Fine (34..37,32) and (31,36..37) are outside coverage: stay 0. Every later request restores its bytes exactly. Repeats are identical |

## Fields

The exclusion fields are documented in MAP-04.1. Additional fields:

| Field | Meaning | Write sites | Read sites |
| --- | --- | --- | --- |
| request+ac..+d8 | Per-candidate slot word (-1 = unresolved) | 169cb2 (-1), 169d43 (member) | 169d31 conflict test (then 169d39 conflict++ or 169d45 valid++) |
| request+f4 | Cached target region (dropped when its +38 == -1) | 16aff0 | 16aff0 |
| path+7c / +80 | Fine / coarse request time; zeroed by admission denial | 166f40 / 166cbf | 168910 interval test |

## Reproducers (repository root)

```sh
# Static inventory (objdump 2.47, reads the DLL only)
python3 tools/ghidra/research/verify_map04_2_scope_exits.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll \
  --functions /GitHub/wc3-analysis/reports/pathfinding-1.27/research/_ghidra/functions.json --report /tmp/scope-exits.json
# Oracle exits + pending edits (also re-checks MAP-04.1)
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_map04_1_nested_exclusions.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/map04.json \
  --reference /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.1/expected-MAP-04.1.json \
  --edit-reference /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.2/expected-oracle-MAP-04.2.json
# Research map (new file only)
python3 tools/frida/research/map04_2_make_map.py --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool --scenario target_overlap \
  --output /run/media/lofcz/ssd_external/Games/w3-research/Maps/RS-MAP-04.2-target-edit.w3m
# Live (isolated env, global lock, bounded); repeat with -2 and the plain map PathingRE-TargetOverlap81-261003.w3m (--margin 1)
flock /GitHub/wc3-analysis/reports/pathfinding-1.27/research/_env/live.lock \
  /home/lofcz/.local/share/uv/tools/frida-tools/bin/python tools/frida/research/map04_2_trace_scopes.py \
  --data /run/media/lofcz/ssd_external/Games/w3-research --map 'Maps\RS-MAP-04.2-target-edit.w3m' --scenario target_overlap \
  --remote 127.0.0.1:27048 --x11-display :97 --seconds 170 --continue-at 80 --continue-after-start 5 --margin 3 \
  --output /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-04.2/captures/target-edit-observe-1
python3 tools/frida/research/verify_map04_2_scope_capture.py --capture <capture dir> \
  [--reference /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/target-overlap81-repeat.jsonl] --report <analysis.json>
python3 tools/ghidra/research/freeze_map04_2_expected.py --oracle MAP-04.2/expected-oracle-MAP-04.2.json \
  --static MAP-04.2/scope-exits-MAP-04.2.json --live captures/target-overlap-observe-2-analysis.json \
  captures/target-edit-observe-1-analysis.json captures/target-edit-observe-2-analysis.json --output expected-MAP-04.2.json
```

## Provenance

- game.dll `d51e5680…d8236` (research copy identical); war3.exe `0ea3434d…`; Storm.dll `36339f69…`.
- Frida 17.18.0; Unicorn 2.1.4; GNU objdump 2.47 (decoded text sha256 `9dece18c…2177`).
- Scripts:
  - `verify_map04_2_scope_exits.py` `75c2f2f5…1be3`
  - `verify_map04_1_nested_exclusions.py` `75160050…eefc`
  - `freeze_map04_2_expected.py` `775eb169…21f7`
  - `map04_2_scope_observer.js`: `0bd419c6…fe36` (plain-map run 2; preserved copy in capture dir) and `36bbc49e…4b83` (edit runs; copies in capture dirs)
  - `map04_2_trace_scopes.py` `1c3bf860…3f54` (edit runs); the plain run used `612ee1a2…3992`
  - `verify_map04_2_scope_capture.py` `4f2dae5d…7c0d`
  - `map04_2_make_map.py` `2ff13732…228c`
  - `map04_2_target_edit_probe.j` `58adafe4…70c6`
  - wrapped builder `make_wc3_pathfinding_map.py` `c336f39e…206a`
- Maps:
  - base `Human02Interlude-original.w3m` `199683be…2104`
  - RS-MAP-04.2-target-edit.w3m `93c10a41…ad84`
  - PathingRE-TargetOverlap81-261003.w3m `a28fcc98…c41a`
- Frozen files:
  - `expected-oracle-MAP-04.2.json` **`d8c0c978c29d0bd893fe2d8882e45f6e7adfb29c8d93fa992b9c9ca356fc1438`** (2 identical runs)
  - `scope-exits-MAP-04.2.json` `f773be04…a772` (identical repeat)
  - composite `expected-MAP-04.2.json` **`94e1fd49e08eb9249d3ccc8f50ea57bd844d2f6fc0aa4c56c63f9f9c16fa3c18`** (identical repeat)
- Seeds: none. The scenarios are scripted; JASS markers come from the observer's Preload hook.

## Captures

| Capture | Status | sha256 | Markers |
| --- | --- | --- | --- |
| target-overlap-observe-1 | **Failed** (Frida reserved `this.depth`); kept, not evidence | 9500567e… | partial |
| target-overlap-observe-2 | Complete (tick 300) | 98d5c514… | 300-tick PATHTRACE sequence equals the reference `runtime/target-overlap81-repeat.jsonl` (`e47610cd…`, wc3_pathfinding.js observer) |
| target-edit-observe-1 | Complete | 673f2e03… | 322 markers |
| target-edit-observe-2 | Complete | 32ef10eb… | 322 markers, identical to run 1 |

## Observer controls

- Oracle: hooks are read-only code observers. An observer-free pass (no hooks installed) gives identical finals (45/45; MAP-04.1).
- Live: the target-overlap probe writes no Preload file, so a JASS-file control is not possible.
  - Control used instead: a different, much heavier observer (reference capture) yields the identical 300-tick JASS-visible trajectory.
  - The two edit runs agree on every marker and every scope/class value.

## Explicit exclusions

- Group member counter values and a member dying between acquire and release: proposed **MAP-04.3**.
- Portal scope execution: GATE family.
- Placement-callback and agent-notification reentrancy inside stop recovery: proposed **MAP-04.4** (live witness of a JASS trigger or order fired from a Stop-recovery notification).
- Whether a public order can give 059590 a stale handle: proposed **MAP-04.5**.
- Allocation failure (Storm fatal) inside scopes: MAP-05.3.
- Request admission/interval timing: NUM-02.3 / SCHED.

## Mismatches preserved

1. Between the two edit repeats, the calls to 14e770 inside stop-recovery scopes differ: 26 vs 38. Totals also differ: spatial_rectangle 13010/13270, mover_fine_bounds 6505/6635, point_query 13846/13878. All JASS markers, scope/class values and counter ledgers are identical. This is probably frame-rate-dependent presentation work, but that is not established.
2. The first edit-case freeze, `superseded-expected-edit-cases-v1.json` (`6698c92e…`), lacked exit cases. It is kept and was replaced by `expected-oracle-MAP-04.2.json`.
3. The 05bd30 Ghidra plate first said +5c; the correction was appended (+98).
4. The ledger (search.md, request exclusions row) says "early/reentrant exits … MAP-04.1/02". Reentrant exits do not exist for the request scopes (static and live). They are only conceivable in stop-recovery, through unresolved notifications.

## Proposed integration

`mapping-rows-MAP-04.2.txt`, `proposed-docs-MAP-04.2.md`. Ghidra writes are already applied through gw.py; see `ghidra-writes.jsonl`.

## Suggested failing engine regressions

1. **Exit restoration, coarse.** One owned adaptive request with mover + target overlapping blocked terrain, run as an exact endpoint and again with the goal enclosed (partial, flag 20000000, adjusted endpoint). Both leave every hierarchy lane byte equal to the pre-request bytes when no edit is pending.
2. **Exit restoration, fine.** For exact, partial (10000000), interval-denied and admission-denied requests, the mover/target exclusion state afterwards equals before. Denials perform no exclusion at all, so no hierarchy write happens.
3. **Edit between phases.** Use `SetTerrainPathable` (no hierarchy publication) beside a target, then a target order:
   - Cells inside floor(min/2)..floor(max/2) of the target's fine rect are published by that request.
   - The other edited cells stay class 0 until their own publication.
   - Expected for the live geometry: base (15,17) 0→mixed in lane 0 and parents (7,8), (3,4), (1,2); (17,16), (18,16), (15,18) stay 0.
4. **No intra-request edits.** Assert in a debug build that no fine/hierarchy writer runs while a request exclusion is held. Stop recovery may move its own spatial record while excluded and must still restore its counter.
