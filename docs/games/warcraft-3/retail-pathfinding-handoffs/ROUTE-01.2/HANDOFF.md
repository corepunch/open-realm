<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-01.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ROUTE-01.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-01.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ROUTE-01.2.json` -> [`ROUTE-01.2-expected.json`](../../../../../tools/ghidra/fixtures/research/ROUTE-01.2-expected.json) (uncompressed sha256 `73f80ad9c9ac1d4a0fd7622875f0274e1d26900d1b87a7c2b9bc10f279d2c2b5`, 320608 bytes)

# ROUTE-01.2 research handoff — empty/partial buffers, invalid starts, route-table growth

**Status.** Original-code emulation of the public advance `165ae0` (unchanged retail x86, real CLrPath constructor
tables, host Storm storage only) freezes **264 scenario rows** (open/wall maps × four classes) and **12 growth
cases** with exact return codes and the complete next state. Instruction-verified: index validity is an unsigned
compare (fine `167cec CMP/JB`, coarse `165be3 JB`), the complete inventory of index/buffer writers (only −1 or
in-range values are produced), the growth-0x80 tables, and that both build routines treat setup result 0
(out-of-map source) like "search". **Not established:** public reachability of an out-of-map fine/coarse source
(no live run; proposed new ID), coarse table growth past 128 points (same append code, not exercised),
allocator failure (unreachable: `1c5130` always returns 1; Storm OOM is outside game code).

## Functions

| VA | Role | ABI (assembly) | Evidence |
| --- | --- | --- | --- |
| 6f165ae0 | Path_Advance | thiscall ECX path; source*, destination/output*, mover; RET 0xc; EAX 0/1/2 or 0x100000 | oracle, asm |
| 6f165b60 | Path_AdvanceAcceleratedRoute | thiscall; index test `cmp [path+70],idx; jb` unsigned (165be3); disabled adaptive → erase+append one point, index 0 (165c43); enabled → 166c30, failure returns 2 (165c08) | asm, oracle |
| 6f167ce0 | Path_GetFineWaypoint | thiscall ECX path; source*, output*; RET 8; `cmp idx,[edi+50]; jb` (167cec) → cached copy, else 167d70(mode0)+166e90; EAX 0 if refill denied | asm, oracle |
| 6f168870 | Path_InitRouteIndex | thiscall ECX path, stack mode; RET 4; idx=count−1, fine with count−1≠0 → count−2 | asm |
| 6f1485f0 | **DynamicTable_AppendPoints** (renamed) | thiscall ECX table; stack4 point* (NULL=reserve), stack8 n; RET 8; EAX 0 only if overflow with growth 0 | asm, oracle |
| 6f14a760 | **DynamicTable_ErasePoints** (renamed) | thiscall ECX table; first, n; RET 8 | asm |
| 6f14aba0 | **DynamicTable_FillPoints** (renamed) | thiscall ECX table; first, point*, end; RET 0xc | asm |
| 6f165a20 | **PathRouteTable_Construct** (renamed) | eh-vector element ctor: capacity 0, growth 0x20; 1657c0/166060 overwrite growth (+48/+68) with 0x80 | asm |
| 6f164a20 | **PathAcc_StartSearch** (renamed) | fastcall ECX adaptive; node[+c4].h=isqrt((24dx)²+(24dy)²); enqueue +c4; tail-jmp 163f50; EAX node/−1 | asm |
| 6f148100 / 6f162cb0 | PathFine/PathAcc_BuildRoute | `cmp eax,1; jne search` (14813b / 162ceb): setup 0 is not distinguished from 2 | asm, oracle |
| 6f14ad50 / 6f164c30 | Fine/Acc SetupSearch | return 0 when floor(source) ∉ map (unsigned); fine has no footprint validity test; acc returns 1 when FindCellNode(source) = −1 | asm, oracle |
| 6f1c5130 | DynamicTable_ResizeBacking | always returns 1 → append failure unreachable for growth≠0 | asm |

## Fields

| Struct+off | Meaning | Width | Write sites | Read sites | Evidence |
| --- | --- | --- | --- | --- | --- |
| path+48 / +68 | fine / coarse table growth = 0x80 | u32 | 1657c0, 166060 (only) | 1485f0 | asm, oracle |
| path+4c / +6c | capacity (0 → 128 → 256 …; never shrinks) | u32 | 1485f0 | 1485f0 | growth cases |
| path+50 / +70 | count | u32 | 1485f0, 14a760 | 167cec, 165be3, 168870 | |
| path+74 | fine index | i32 | 1657c0/166060/168740/165f8a/167100 (−1); 166fec (0); 168889 (count−1/−2); 165e77 (167bf0 result, in range) | 167cec (unsigned) | writer inventory (objdump scan of 6f165000–6f16f000 + 168740 pointer write) |
| path+78 | coarse index | i32 | ctor/activate/reset (−1); 165c43/167227 (0); 168889; 165d8c (−2, immediately overwritten by 165d9d); 165db0/165e30 (selector) | 165be3 | same |
| path+7c / +80 | fine / coarse request time (budget denial clears, interval denial retains) | u32 | 16892d (interval rebase), 166f40, 166cbf, 166fd6, 166dbb | 168910 | oracle |
| fine+90 / acc+c4 | source node — **not reset** by 14a980/164230 | i32 | setup only | 14aa10 / 164a20 | oracle `stale_outside` |

Erase callers on path tables are exactly 148100, 162cb0, 168740, 165b60, 167120, each followed by an index write
(xrefs to 14a760). Other `[reg+74]/[reg+78]` writes in 6f165000–6f16f000 (1698ca/cd PathGroup_Activate,
16aadd, 16afcd, 16be32, 16dae6 via MoveRequest_ActivateCandidateCohort, unreferenced 16d800) target group/request
objects by their decompiled receivers (decompile classification, not a full type proof). Hence **index ≥ count other than −1 is unreachable through retail writers**; such rows below are
consumer evidence only.

## Behaviour (frozen) — `expected-ROUTE-01.2.json`

sha256 `73f80ad9c9ac1d4a0fd7622875f0274e1d26900d1b87a7c2b9bc10f279d2c2b5` (payload `9ebd39bb…5970`). Source
(30.125,33.875), goal (51.25,42.75); acc units = fine×0.5. Return codes are identical across all four classes and both maps unless noted; per-class counts/words are in the JSON.

| Scenario (first advance) | Return | Next state | Following advance |
| --- | --- | --- | --- |
| Empty, adaptive off, admitted | 0 | coarse 1 point (25.625,21.375) idx0 cap128; fine count 22 (open) idx0 cap128, flags 02000000 | 0, unchanged (cached) |
| Empty, adaptive on, admitted | 0 | coarse 3 pts idx1; fine refilled toward coarse point; flags 02200000 | 0, cached |
| Empty, coarse **budget-denied** | **2** | both tables untouched (cap 0, idx −1); coarse FIFO waiting 1; output = destination | 0 after next work window (FIFO head admitted) |
| Empty, fine budget-denied | **2** | fine cap 0 idx −1, fine time cleared, flags 02000000 set, FIFO waiting 1; coarse already built | 0 |
| Empty, fine interval-denied (time=counter) | **2** | fine time retained 100, flags 0 | 0 at counter 140 |
| Fine idx ∈ {count, count+7, 7fffffff, 80000000, ffffffff}, denied | **2** | buffer retained, idx unchanged, time cleared, FIFO 1, output = destination | — |
| same, admitted | 0 | full refill (wall: idx 0x14=count−2), output (31.5,33.5) | — |
| Fine idx = count−1 (exact source point) | 0 | 167bf0 selects farthest visible (wall: idx 0xa), output (41.5,33.5) | — |
| Coarse idx ∈ {count, ffffffff}, denied | **2** | 166c30 first unlinks: flag 02000000 **cleared**; fine buffer kept, output = destination | — |
| Blocked fine source cell (wall x=40) | 0 | ordinary search from blocked cell (no source validity test); coarse (enabled): FindCellNode −1 → **one exact-goal coarse point**, result 1 | cached |
| Out-of-map fine source (−3.5,10.25)/(70.25,10.25) | **1** | setup 0 → search from **stale** fine+90 → stale partial chain; then 167290 retry init (retry 6/7), fine reset (count 0, idx −1), flags 12000000 (+20000000 if coarse mismatched) | 1 again (retry consumed) |
| Out-of-map fine source (10.25,−0.5) | 0 | stale-chain route kept (22–24 pts), flags 12000000 | cached |
| Stale-prior control (direct 166e90, two different priors) | 1/1 | different 22- vs 19-point routes, both ending in exact source (−3.5,10.25): **output depends on previous request's node memory** | — |
| Growth: straight fine route length L (256-cell map, default budget 700) | 1 | L≤127: count L+1, cap 128 (Storm alloc 0x400); L≥128: count L+1, **cap 256** (alloc 0x400 then realloc 0x800); words exact | shorter refill: count 11, cap stays 256, no Storm call |

Coarse out-of-map depends on the padded hierarchy: fine x=70.25 → acc 35.125 is **inside** the 41-wide base, so it
searches normally (11/17 points); negative x is outside and searches from stale +c4.

## Reproducer

```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ROUTE-01.2
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_route01_2_buffers.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report $R/oracle-report-run1.json --expected $R/expected-ROUTE-01.2.json
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_route01_2_buffers.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report $R/oracle-report-run2.json --expected $R/expected-ROUTE-01.2.json
```

## Provenance

game.dll d51e5680…d8236 (emulated; w3-research copy identical). Harness `route01_1_2_harness.py` 478c7604…;
`verify_route01_2_buffers.py` c74cd74f…; worktree HEAD e7035267. Reports run1/run2 sha `5e40382e…` (byte-identical).
Unicorn 2.1.4, Python 3.12.14, objdump 2.47 (`asm-excerpts-ROUTE-01.2.txt`). Preconditions as ROUTE-01.1 plus
registry/mover/group records for 165ae0 context install (refill-oracle convention) and scheduler buckets
(fine 0x6fd53ae4: 700|1<<16, limit 1100; coarse 0x6fd53ac8: 400|2<<16, limit 900). "Next owner work window" is
modelled by writing bucket work 0 only (FIFO links kept), not by the owner clock.

## Captures

| Capture | Status |
| --- | --- |
| oracle-report-run1/run2 + expected | complete, identical |
| Development run with `set_buckets()` after denial (zeroed FIFO while path stayed linked) | **rejected harness artifact**, replaced by work-only reset; kept here as a note |
| Development runs before stale-prior control/default growth budget (2000) | superseded, not evidence |

## Observer controls

No live run. Emulation hooks are read-only (147dc0 chain capture in growth cases). Run2 reproduces run1 byte-for-byte.

## Explicit exclusions

- Public reachability of out-of-map sources (SetUnitX/bounds clamping): **proposed new ID ROUTE-01.4** (text only).
- Coarse growth past 128 points, node-identity cap 32768: MAP-05.1 / existing storage oracle.
- Owner clock / bucket work reset cadence: NUM-02.3 / SCHED.
- Retry-count semantics after source-only failures: ROUTE-05/retry corpus (consumed, not re-certified).
- Gate (−2 index) branch of 165d10: GATE.

## Mismatches preserved

- Ledger routes doc: "Invalid starting-map coordinates and the wrapper's caller preconditions remain to be recovered" —
  now: setup 0 falls through to a stale-node search (not a rejection).
- Engine (code reading, not run): `wc3_fine_build_route` returns **0 points** for an out-of-map start
  (`wc3_pathing_route.h:108-110`); retail never produces a zero-count fine route from 148100 (it reconstructs stale
  nodes). `wc3_acc_search` returns −2 (one exact-goal point) for an out-of-map start; retail searches from stale +c4.
  Whether to mirror stale-memory behaviour is an owner decision; reachability first (ROUTE-01.4).
- Existing cached-route corpus (refill oracle) states (0,−1),(1,1),(2,2) are consistent with these rows.

## Proposed integration (not applied)

`mapping-rows-ROUTE-01.2.txt`, `proposed-docs-ROUTE-01.2.md`; Ghidra writes applied via gw.py (`ghidra-writes.jsonl`, 12 rows).

## Suggested failing engine regressions

1. Empty path, fine bucket over budget: public advance returns 2; fine capacity 0, index −1, fine request time 0,
   flag 02000000 set, path queued; next window admits the FIFO head and returns 0 with the frozen 22-point route.
2. Empty path, coarse denied with flag 02000000 set: returns 2 and clears 02000000 (unlink precedes admission).
3. Fine index 0x80000000 / count / count+7 with denial: returns 2, buffer and index unchanged, output = destination;
   with admission: identical to the −1 refill words.
4. Fine route of 129 points: capacity 256 after the request, 128 at 128 points; a following 11-point refill keeps 256.
5. Blocked coarse source (FindCellNode −1): single exact-goal coarse point, result 1, index 0.
6. (Owner decision, after ROUTE-01.4) out-of-map source must not yield a zero-count fine buffer followed by a
   `data[-1]` read in the waypoint getter; assert the retail public outcome (return 1, retry 6/7, cleared buffer)
   for the frozen prior.

## Payoff141 integration

Initialized outside-source search history and retained fine/adaptive/group route
capacities are now implemented in Move. The unchanged264-scenario/12-growth
oracle reaches452 fine and152 adaptive builds; all result/work/count/route
words match the production kernels at O0/O2. The previous kernels differ on98
builds. Actual fine adapter stale-prior and growth-capacity regressions reproduce
6 and24 failures before the fixes. Save123 rebuilds derived capacities.

The earlier code-reading claim of zero engine points for the two stale-prior
controls was inaccurate: the measured baseline returned a source-only route.
Two completing public SetUnitX/Y Frida repeats and an observer-free control now
prove all three outside coordinates reachable. A separate negativeX Move reaches
setup0 but its subsequent run stops progressing; it is not a completed movement
capture. See [the engine ledger](../../retail-pathfinding-engine.md#retained-search-history-and-route-capacity-payoff141).

ROUTE-01.2 remains open for full actual-engine public advance state/return and
post-load invalid-start history. The proposed01.4 is unnecessary and has not
been added. The original handoff above remains as dated research provenance.

## Payoff142 consumer correction and integration

The original harness omitted scalar6fd54194 initialization. Enabled public
advance rows therefore consumed routes with a zero distance threshold; this
is a supplied BSS control, not initialized gameplay. Original0040d0 writes ten
via Math_FromIntegerTruncated. The new264-row initialized fixture executes it
and repeats exactly; the earlier frozen fixture remains unchanged for provenance.

Production Move now retains disabled-adaptive one-point coarse caches and does
not rebuild a valid fine leg merely because its coarse index needed a refill.
The208 ordinary scenarios compare represented state through actual steering,
including256 advances and48 saved continuations. Invalid-start consumer outcomes,
packed flags without an engine counterpart, and post-load search history are
still not closed. See [the engine ledger](../../retail-pathfinding-engine.md#independent-coarse-admission-and-fine-cache-payoff142).
