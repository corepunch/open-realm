<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-02.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ROUTE-02.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-02.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ROUTE-02.2.json` -> [`ROUTE-02.2-expected.json`](../../../../../tools/ghidra/fixtures/research/ROUTE-02.2-expected.json) (uncompressed sha256 `1433827c28e1e59c74c5cb6863aad9b09f7d78ee72bdb0738715cc96abd611f6`, 52830 bytes)

# ROUTE-02.2 handoff — blocker candidate cap/order and obstruction change between samples

**Status.** Established at **original-code emulation** level (Unicorn, complete original producers, no stubs) plus
**instruction-verified** ABIs/static closure: next-step candidate order, the 32-token cap and its truncation,
resolver consequences of truncation, waypoint selection under object occupancy, and that occupancy cannot change
inside one selector/collector call (so the "change between samples" is a change between successive selector
calls). **Not established:** live retail reachability of the cap in a real crowd (no live capture made; proposed
ROUTE-02.5), runtime lazy-chain insertion order (FINE-01.6), engine behaviour (owner). Nothing closed.

## Functions

| VA | Role | ABI (assembly) | Evidence |
| --- | --- | --- | --- |
| 6f166140 | Path_CollectAndResolveNextStepBlockers | thiscall ECX path; [esp+4] source*, [esp+8] destination*; RET 8; AL = (table count==0). SEH frame 6f9e7508 | asm 6f166140..1662f1; 72+14+24 complete calls |
| 6f149560/1497c0/149b00/149e60 | class 0..3 step collectors | thiscall ECX fine; (point int[2]*, code, table*) RET 0xc; jump tables | asm; hook order == model in 72 calls |
| 6f148e30 | **PathFine_CollectRowBlockers** (new) | thiscall ECX fine; (x0,y,table*,count) RET 0x10 | asm 148e30..148e68 |
| 6f148cc0 | **PathFine_CollectColumnBlockers** (new) | thiscall ECX fine; (x,y0,table*,count) RET 0x10 | asm 148cc0..148cf8 |
| 6f148c30 | **PathFine_CollectRectBlockers** (new) | thiscall ECX fine; (x0,y0,table*,w,h) RET 0x14; row-major | asm 148c30..148c6b |
| 6f148ad0 | PathFine_CollectCellBlockers | thiscall ECX fine; (x,y,table*) RET 0xc | asm rets 148bb7/148bd8 |
| 6f1480d0 | PathFine_AppendBlockerPayload | fastcall ECX table, EDX payload; RET; gate `cmp [ecx+1c],20; jae` | asm 1480d0..1480ff |
| 6f148550 | **DynamicTable_AppendRepeatedDword** (new) | thiscall ECX table; (value*,count) RET 8; EAX bool | asm 148550..1485d7 |
| 6f168590 | **DynamicTable_RemoveRange** (new) | thiscall ECX table; (start,count) RET 8; EAX bool; memmove import 6fa7c508 | asm 168590..1685e9 |
| 6f167940 | **PathFine_StepDirectionCode** (new) | fastcall ECX prev int[2]*, EDX next int[2]*; RET; EAX code (x- 8, x+ 2, y- 1, y+ 4) | asm; call sites 16624e, 168e23 |
| 6f147340 | table constructor (not renamed) | thiscall; init {capacity,growth}; sets +14 growth,+18 capacity,+1c 0 | asm 147340..147396; fine+ac call 147778 with {0x20,0} |
| 6f168360 | Path_ResolveMovingBlockers | thiscall ECX path; (table*) RET 4; `comiss cand2,self2; jae` → self waits | asm 16841c..16849e; 14 compositions |
| 6f167bf0 | Path_SelectVisibleFineWaypoint | thiscall ECX path; (source*) RET 4; EAX index; passes ECX=path to 168d30 (ignored) | asm 167bf0..167cd1 |
| 6f165e60 | Path_CommitVisibleFineWaypoint | thiscall ECX path; (source*,out*) RET 8; writes path+74, out | asm 165e60..165e89 |
| 6f168d30 | Path_TestSampledSegment | ECX unused; (start*,dir*,float len) RET 0xc | asm (ecx overwritten at 168d3a) |

Bold = renamed in Ghidra by this task (ghidra-writes.jsonl, mirrored in mapping-rows). ESP after every top-level
emulated call is asserted equal to the RET n value.

## Fields

| Struct+off | Meaning | Width | Write sites | Read sites | Evidence |
| --- | --- | --- | --- | --- | --- |
| fine+ac table +0c/+14/+18/+1c | data / growth / capacity / count | u32 | 147340 (cap 0x20, growth 0); 148550 (+1c,+18); 168590 (+1c) | 1480d0 (+1c<0x20), 148ad0 terrain branch, 168360 | asm 147769-147778, oracle |
| fine+a4 | query mask | u32 | 16626e, 167c3f (from path+9c; not restored) | 1489a0, 148ad0 | asm, write audit |
| fine+a8 / +cc | search target / target-seen latch | ptr/u32 | 14ae9b / 148a5a | 1489a0 | Part F |
| fine+d0 | observed-obstruction latch | u32 | 148aaa/148abc (also during selection), reset 14aeab | 166fc1 | write audit, asm |
| fine+d4 | endpoint mode | u32 | 14aea1 (=0 at search setup); 14a08c/14a21a/16ef3a/17014c set 1 and restore | 1489a0 | asm; Part C d4 variants |
| grid+b4 | per-cell stamp counter | u32 | 148a03, 148b2e | same | write audit; Part A counter |
| object+38 | stamp (ffffffff dead) | u32 | 148a47, 148b7f | same | Part A stamps |
| object+40 | flags/counter (&8fffffff suppress; 60000000 exempt only in 1489a0 with d4=0) | u32 | 166265/1662dd, 167c2a/167cbd (self only) | 1489a0, 148ad0 | Parts A/C |
| path+a8/+ac, +94 | blocker identity (slot,gen), delay | u32 | 168070, 168379/168386 | 1680f0 | Part B |

## Frozen results (`expected-ROUTE-02.2.json`, sha256 `1433827c28e1e59c74c5cb6863aad9b09f7d78ee72bdb0738715cc96abd611f6`)

32×32 fine map, mask `02000001`, source (16.5,16.5) (code 0: (16.25,16.25)→(16.5,16.5)), destination source+3·step.

**A. Strip order + cap (72 calls).** Visited cells per class/code equal the model in `strip_model()` (e.g. class 2
code 3 (x+,y−): (15..18,14) then (18,15..17); class 3 code 0: 4×4 row-major from (14,14)). Tokens = cell order ×
chain order, per-cell dedup only; specials in first cell: kind-2, inactive, dead, mask-mismatch, counter 1, bit31
skipped; flag 20000000 and 10000000 **collected**; non-mover/null payload → null; within-cell duplicate → one token;
terrain/out-of-range cell → one null, chain unread. All 36 "over" cases stop at exactly 32 tokens; class 3 code 0
"under" also hits 32. Stale vector (count 5) is cleared first.

**B. Resolver over capped order (14 compositions; self speed 1, different group, same class unless noted).**

| Case | Self blocker / delay | Peer outcomes |
| --- | --- | --- |
| fast(3) peer at token 0/15/31 | that peer / 4 | peer untouched |
| fast peer at chain pos 32 or 39 (dropped) | none (ffffffff) / 0 | untouched |
| slow(0.5)@5, fast@20 | fast / 4 | slow: blocker=self(47,1047), delay 20 |
| slow@20, fast@5 | fast / 4 | slow untouched (scan ended) |
| slow@5, fast@32 | none / 0 | slow assigned 20 |
| slow@31, fast@32 | none / 0 | slow@31 assigned 20 |
| 40 slow peers | none / 0 | positions 0..31 assigned 20; 32..39 untouched |
| preblocked fast@3, fast@9 | @9 / 4 | @3 keeps its own blocker (46), delay 0 |
| slow peer, class 2 vs self 0 | that peer / 4 | — |
| slow peer, same group (flags bit8 clear) | that peer / 4 | — |

All return AL=0, count 32.

**C. Selection (288 calls) and change sequences.** Route points (20.25−2n, 16.75), n=0..4, index 4, source
(12.25,16.75). One object at cell X: static/bit28 → index 3 (X=15), 2 (X=17), 1 (X=19, classes 0/1) or 2 (classes
2/3); 20000000/40000000 → 0 with d4=0, same as static with d4=1; self, counter, bit31, dead, inactive, mask
mismatch, kind 2 → 0. Sequence between successive calls (all classes): clear **0** → insert static (17,16) **2** →
flag 20000000 **0** → same with d4=1 **2** → flag cleared **2** → unlinked **0** → terrain (13,16) **3** (classes 0/1)
/ **0** (classes 2/3: first-sample strip miss, ROUTE-02.1).

**D. Write audit** (complete calls, non-stack writes): 167bf0 → fine+a4, fine+d0, FS:0 SEH, grid+b4, object+38,
self+38, self+40 (restored). 165e60 adds path+74 and output. 166140 → table entries/count, fine+a4, grid+b4,
object+38, self+38/+40, peer/self path +94/+a8/+ac. No cell, link, object +30/+34 or foreign +40 writes.
Static closure (`callgraph-ROUTE-02.2.json`): 48 functions, no indirect calls other than Storm 401/403/405 thunks
reachable only from 1c5130 (resize; unreachable at cap 32/growth 0) and the memmove import.

**E. Selection vs collection (16).** Mover (speed 3) at (13,16): class 0/1 static → select 3, collected, self waits
(4); flag 20000000 → select **0** yet collected, self waits. At (14,16): class 2/3 collected (entering column), class
0/1 not. **F. Target identity (8).** path+a4 = fine+a8 = object at (17,16): selection 2 and fine+cc=1 (all
classes); at (14,16) classes 2/3 collect the target and the requester waits on it (blocker 10/1010, delay 4).

## Reproducer (repository root)

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-02.2
D=/run/media/lofcz/ssd_external/Games/w3/game.dll
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_route02_2_blockers.py --binary $D --report $R/oracle-report-run1.json --expected /tmp/e1.json
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_route02_2_blockers.py --binary $D --report $R/oracle-report-control-no-observers.json --expected /tmp/c.json --no-observers
python3 tools/ghidra/research/route02_2_callgraph.py --binary $D --report $R/callgraph-ROUTE-02.2.json
sha256sum /tmp/e1.json   # 1433827c...611f6
objdump -d -M intel --start-address=0x6f040000 --stop-address=0x6f180000 $D | grep -E "(inc|dec) +DWORD PTR \[e..\+0x40\]$"   # +40 scopes; none in 165ae0/165c60/16fbd0
```

No live run, no map, no lock taken.

## Provenance

game.dll `d51e5680…d8236`; Unicorn 2.1.4; GNU objdump 2.47. Scripts: `verify_route02_2_blockers.py`
`f87ed0409949b5fc6c560feda4296423e2d280357b880fa2c30c584064ede8ea`, `route02_2_callgraph.py`
`85c0d714780d30bb626222ae6e658512c9e1905c2f04b38f827e84abad3347dc`. Outputs: expected `1433827c…611f6`;
oracle-report-run1/2 `b8a3e954…6fbf0` (identical); control-no-observers `e75207a3…32fb5`; callgraph `3f981dae…d60b`.
Synthetic inputs fully defined in the script (no seed).

## Captures

| Capture | Status |
| --- | --- |
| oracle runs 1, 2 (final script) | pass, byte-identical expected |
| earlier runs with pre-control script `bad406e9…` | pass, same expected sha (superseded, not kept separately) |
| callgraph v1 (objdump continuation lines mis-parsed → "undecoded" entries, missing 168360 callees) | **invalid**, fixed parser; v1 /tmp output not evidence |
| live retail | none |

## Observer controls

Code hook (148ad0 entry, stack read) and write hook are read-only. `--no-observers` run (no hooks, no Part D)
produces an identical expected JSON except the absent write audit. Repeat count 2 + control.

## Explicit exclusions

- Live crowd reachability of the 32 cap and real chain order → proposed **ROUTE-02.5**; runtime link chronology FINE-01.6.
- Producer/meaning of `+40` flags 20000000/40000000 and fine+d4 scopes → FOOT-04 / FINE-01.2 / FINE-01.3.
- Resolver group-bit8 producer and full cycles → ROUTE-05.1/05.2 (decision itself verified in ROUTE-05.3).
- Multi-tick retry/refill after a nonempty vector → ROUTE-03.1/03.2; self/target exclusion scopes → MAP-04.1/04.2.
- Class 2/3 first-sample strip miss → already ROUTE-02.1.

## Mismatches preserved

- **Candidate engine mismatch (not executed):** `g_world.c` `move_collect_blocker_cell` and `move_occupancy_cell`
  skip `ent==query->target`; retail suppresses only self (166265/167c2a) and Part F shows the target is collected
  and blocks selection.
- Ledger (routes) lists "allocator growth" as open for the collector: the fine blocker table cannot grow
  (capacity 32, growth 0, gate count<32) — refinement, not a contradiction.
- Engine Ghidra note on 166140 "Edict order is not retail overlapping cell-link insertion order" stays (FINE-01.6).
- My first Part E read (class 2/3 object at (13,16) "not seen") is correct retail behaviour (inside source
  footprint; neither entering strip nor first sample covers it); (14,16) variant added, both kept.

## Proposed integration

`mapping-rows-ROUTE-02.2.txt`, `types-ROUTE-02.2.json`, `proposed-docs-ROUTE-02.2.md` (ROUTE-02.5 text only).

## Suggested failing engine regressions

1. 40 moving slower peers (different group, same class) linked in one entering cell in a known chain order, self
   speed 1 → exactly the first 32 get wait_blocker=self, delay 20; the last 8 untouched; requester no wait.
2. Same cell, stationary fillers, faster peer at chain position 31 → requester waits (delay 4); at 32 → no wait.
3. slow@5/fast@20 vs slow@20/fast@5 → the table in B (order decides whether the slow peer is assigned).
4. Class 0 diagonal step with one unit spanning 3 strip cells → 3 tokens.
5. Order target unit in a class-2 entering column → collected; requester waits on it (Part F). Target on the
   selection segment → index 2 not 0.
6. Peer with retail moving flag on the segment ahead → selection keeps index 0, collector still yields (Part E).
7. Successive reached-waypoint selections on the frozen route with the C-sequence edits → 0,2,0,2,2,0.
