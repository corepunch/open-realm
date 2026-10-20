<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-01.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ROUTE-01.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-01.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ROUTE-01.1.json` -> [`ROUTE-01.1-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/ROUTE-01.1-expected.json.gz) (uncompressed sha256 `993ae13e086119b20e3208f6492ac798556ec9ef1da25fff475e725ffd0d637b`, 6291507 bytes)

# ROUTE-01.1 research handoff — fine/coarse endpoint reconstruction, oblique directions, every class

**Status.** Original-code emulation (Unicorn, unchanged retail x86, real producers) establishes exact fine and coarse
reconstruction words for **2,664 fine requests** (`166e90→148100→147dc0`) and **10,656 coarse requests**
(`166c30→162cb0→162a30`): four footprint classes, four coarse lanes, 36 regular source→goal vectors (32 oblique + 4 short/cardinal/same-cell), plus nine enclosed-goal vectors, three goal fractions, two source fractions, four 64-cell maps (open, wall-with-gap,
lane-mixed, enclosed-goal). Every output word equals an independent float32 model fed from read-only entry
observers, and equals production C (`wc3_fine_build_route`, `wc3_acc_route` via `tools/ghidra/wc3_pathing_engine_probe.c`)
at **O0 and O2: 5,328 fine + 21,312 coarse comparisons, 0 differences**. ABIs are assembly-verified. No live run
(not needed: reconstruction has no runtime-only producer). **Not established:** public Move consumption of coarse
points for classes 2/3 beyond existing ROUTE-03.x evidence, warp sentinels from real gates (GATE), negative/nonzero
world origins (NUM-02.7), size input 2 (size 4) producers other than 166c30/059ca0.

## Functions

| VA | Role | ABI (assembly) | Evidence |
| --- | --- | --- | --- |
| 6f166c30 | Path_RequestAcceleratedRoute | thiscall ECX path; [ebp+8] source* (acc units), [ebp+c] goal*, [ebp+10] warp→162cb0 last arg; RET 0xc; EAX 0/1. Class from path+b4 via SSE `comiss` against 0.5 (6fcd53f8), 1.0 (BSS 6fd3c748), 1.5 (6fcd5420) → 0..3; `shr eax,1` → size input 0/1 (166d1e). Endpoint test `ucomiss` goal vs data[0] (166dfd/166e0b); mismatch → adjusted(+24/+28) = data[0] × map0+64 via 06f9c0, flag 20000000 | asm-excerpts, oracle |
| 6f162cb0 | PathAcc_BuildRoute | thiscall ECX adaptive; stack lane, route table, source*, goal*, budget, size input, warp; RET 0x1c; EAX 1 complete/bypass, 0 partial/failed. `cmp eax,1/jne` at 162ceb: only setup==1 bypasses | asm 162cb0..162d90 |
| 6f164c30 | PathAcc_SetupSearch | (existing row) stored size +90 = 1<<size input | asm + oracle (`sizes_seen`) |
| 6f162a30 | PathAcc_Reconstruct | thiscall ECX adaptive; stack node, route, source*, goal*; RET 0x10. Software float only (070d80 int→float, 06fbb0 add); offset = 0.25 + (size==2 ? 1.0 : 0.5); level byte +22 decrement only when size==2; tag +23 appends (BSS 6fd53a74, float(tag)) after the node and increments +a0; then data[count-1]=source, data[0]=goal unconditionally | asm 162a30..162b76, oracle |
| 6f162c40 | PathAcc_NodeCentre | (x+0.5, y+0.5) — partial goal is the raw nearest node centre, **not** +0.75/+1.25 | oracle (348 partial rows) |
| 6f148100 | PathFine_BuildRoute | thiscall ECX fine; stack route, source*, goal*, mask*, budget, radius*, target; RET 0x1c | asm |
| 6f147dc0 | PathFine_Reconstruct | thiscall ECX fine; stack node, route, source*, goal*; RET 0x10; centres via 070d80+06fbb0 (+0.5, 6fcd53f4); floor compare via 070c80/070120 | asm, oracle |
| 6f162a10 | **PathAcc_AppendRoutePoint** (renamed) | stdcall(route, point*), RET 8, ECX ignored | asm |
| 6f147da0 | **PathFine_AppendRoutePoint** (renamed) | stdcall(route, point*), RET 8 | asm |

## Fields

| Struct+off | Meaning | Width | Write | Read | Evidence |
| --- | --- | --- | --- | --- | --- |
| adaptive+90 | stored size | u32 1/2 | 164c30 | 162a30 (162a45) | oracle: classes 0/1→1, 2/3→2 in all 10,656 rows |
| adaptive node+22 | level | u8 | node creation | 162a93 | levels 0..3 observed; 6,604 size-2 decrements |
| adaptive node+23 | incoming special-edge tag | u8 | relax special | 162b0e | always 0 here (no gates) |
| path+54..+70 | coarse table (data+60, growth+68=0x80, cap+6c, count+70) | | 1657c0 | | harness reads |
| path+78 | coarse index = count-1 after 166c30 (168870 mode1) | i32 | 168889 | | all rows |
| path+24/+28, flags 20000000 | adjusted endpoint / coarse mismatch | | 166e64/166e6f | | all mismatch rows |

## Behaviour (frozen)

`expected-ROUTE-01.1.json` (sha256 `993ae13e086119b20e3208f6492ac798556ec9ef1da25fff475e725ffd0d637b`, payload
sha256 in report `5f2915f2…9805`): per request raw source/goal words, result, count/capacity, index, obstruction,
mismatch, every route word; coarse rows add lane, stored size, work, nodes, parent chain (x,y,level,tag), adjusted.

| Rule (all instruction + oracle verified) | Count |
| --- | --- |
| Fine: destination-first `(x+.5,y+.5)`, last := exact source; first := exact goal iff floor(first)==floor(goal) | 2,592 reconstructions |
| Fine same-cell setup bypass: one exact-goal point, result 1, no reconstruct | 72 |
| Fine partial: goal input = nearest node centre (+.5), flag 10000000 | 96 |
| Coarse: `(x+o,y+o)`, o=.75 (size1) / 1.25 (size2); size2 & level>0: coordinate equal to last of its 2^level square −1; last := source; first := goal (unconditional) | 8,088 |
| Coarse setup bypass (same cell 864; source node == goal node through one promoted region 1,704): one exact-goal point, index 0, flag 20000000 clear | 2,568 |
| Coarse partial: first = nearest node centre (+.5), index count-1, adjusted = first×2, flag 20000000 | 348 |
| Route order: source last (index count-1), goal/centre first; no smoothing/collinear removal | all |

## Reproducer (repository root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-01.1
for o in O0 O2; do cc -std=c11 -Wall -Wextra -Werror -$o -fPIC -shared -I . tools/ghidra/wc3_pathing_engine_probe.c -o $R/engine-probe-$o.so; done
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_route01_1_reconstruction.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report $R/oracle-report-run1.json \
  --expected $R/expected-ROUTE-01.1.json --engine-library $R/engine-probe-O0.so --engine-library $R/engine-probe-O2.so
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_route01_1_reconstruction.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report $R/oracle-report-run2.json --expected $R/expected-ROUTE-01.1.json
```
(Second run compares against the frozen file; ~2 min with engines, ~35 s without.)

## Provenance

| Item | sha256 |
| --- | --- |
| game.dll (emulated; also w3-research copy) | d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236 |
| tools/ghidra/research/route01_1_2_harness.py | 478c76045a84580e0c021b23b20c8d790922b1256edacf55e5cffb1264d021f7 |
| tools/ghidra/research/verify_route01_1_reconstruction.py | 409a6364dec4499dbf3f003c4a6e0c0c3064a37f8b3bfb12b4196fb018848554 |
| tools/ghidra/wc3_pathing_engine_probe.c (unchanged, HEAD e7035267) | 89b7b86c8b79a9f3e510a04f45d52a4e021cf3d38700ac81c482cd217b8283a2 |
| common/wc3_pathing_adaptive.h / _route.h / _fine.h | 797068f7… / 4f1c2b74… / cae08a51… |
| engine-probe-O0.so / O2.so (gcc 16.2.1) | a1da13ca… / 0714fc31… |
| oracle-report-run1.json / run2.json | e44e1f9e… / af4a422a… (identical payload sha `5f2915f2…`) |

Unicorn 2.1.4, Python 3.12.14, objdump 2.47. Supplied preconditions (harness docstring): Storm 401/403/405 host
storage only, four BSS constants, padded hierarchy headers (41/20/10/5), preallocated adaptive node/heap storage,
bucket words. Terrain via original 04d870→054000; hierarchy via original 15d360.

## Captures

| Capture | Status |
| --- | --- |
| oracle-report-run1.json (+engine O0/O2) | complete, passed |
| oracle-report-run2.json (frozen compare) | complete, passed, same payload |
| /tmp development runs (612/2,448 then 2,664/10,656 rows before vectors/source fractions were extended) | superseded, not evidence; no failures occurred |

## Observer controls

Observers are Unicorn `UC_HOOK_CODE` read-only entry hooks at 147dc0/162a30 (read registers/memory only). Control:
run2 executes without the production-C comparison and reproduces the identical payload hash; the frozen file is the
comparison. No live observer used.

## Explicit exclusions

- Warp sentinel records from real gates: GATE-* (existing 4,608 special-edge controls cover the reconstruction rule).
- Nonzero/negative world origin and world→fine conversion: NUM-02.7 / FOOT-04.
- Public Move consumption of coarse routes/classes 2–3 through owner cadence: ROUTE-03.1/03.2, SCHED.
- Coarse size input 2 (stored size 4) from `1627e0` callers (059590/16ba0a/16e3d0): not a path-route producer; the old
  9,216-case synthetic corpus's size-4 rows are synthetic only (proposed note, no new ID).
- Buffer growth/invalid starts: ROUTE-01.2 (separate handoff).

## Mismatches preserved

- Ledger `retail-pathfinding-routes.md` says "Accelerated reconstruction normally emits (x+0.75,y+0.75) … size class
  +0x90 == 2 → 1.25" and the old oracle sweeps sizes 1/2/4: real producers emit only sizes 1/2; size 4 is not produced
  by 166c30 (size input = class>>1) nor 059ca0 (pushes 0). Not a contradiction, but the size-4 rows are not producer evidence.
- Ledger is silent on the partial coarse first point; it is the nearest node **+0.5 centre** (162c40), not a +0.75/+1.25
  reconstructed point and without the size-2 decrement. Engine already does this (`wc3_route_center`); record it in the ledger.

## Proposed integration (not applied)

`mapping-rows-ROUTE-01.1.txt`, `proposed-docs-ROUTE-01.1.md`. Ghidra writes already applied via gw.py
(`ghidra-writes.jsonl`, 5 rows): renames 162a10/147da0, plate-comment append 162a30.

## Suggested failing engine regressions

1. Load `expected-ROUTE-01.1.json`; for each coarse row feed the recorded hierarchy-producing map (or the recorded chain)
   to the production adaptive route and assert `count`, every word, index = count-1, flag/adjusted. Already passes in the
   probe; the regression should be at the **Move adapter** level: class 2/3 coarse route consumed by Move must keep
   the 1.25 offset and the level decrement (rows with `stored_size==2` and level>0).
2. Partial coarse row (e.g. map `enclosed`, cls 0, lane 0, vector [17,15]): assert first point == nearest node centre +0.5
   and adjusted == first×2, flag 20000000 set, coarse index count-1.
3. Coarse bypass through one promoted region (`goal_node_equal`): assert exactly one point equal to the exact goal words,
   index 0, mismatch clear, zero search work.

## Integration (Payoff140)

ROUTE-01.1 is closed in the engine tracker. See [the engine ledger](../../retail-pathfinding-engine.md#oblique-reconstruction-and-direct-owned-consumption-payoff140). The reproducer now accepts the repository gzip fixture and authenticates the literal200-row Move adapter subset plus saved Ghidra annotations. All four classes/lanes preserve native words; retained route consumers no longer copy their entire chains to scratch storage. The independent buffer/public advance task remains ROUTE-01.2. Historical source hashes above identify the research capture version, not the subsequently extended verifier.
