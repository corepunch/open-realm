<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-03.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-03.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-03.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-03.2.json` -> [`SEP-03.2-expected.json`](../../../../../tools/ghidra/fixtures/research/SEP-03.2-expected.json) (uncompressed sha256 `0b1a6583042f9b7819811d839a0527ca064544c2ba256670c850b3d5b81e4f39`, 5769 bytes)

# SEP-03.2 handoff — spatial query-stamp wrap/repair, fresh block allocation, candidate order and reclamation

**Status.** Instruction-verified stamp producers/consumers and the only repair (counter reset); original-code verified
(Unicorn, real constructors) increments per operation, the repair boundary, link-table first allocation and growth, object-pool
block allocation and LIFO reclamation, and candidate order across growth/reclamation. Live-verified: lazy 1 MiB link allocation
on the first movement record of each map, object-pool blocks of 64 (5 at load, 6th at the 40-unit burst), LIFO reuse of
reclaimed objects (20/20), measured stamp rates. **Labelled** (fixture counter write replacing ≥2^31 increments): the stale-stamp
hazards after the repair. Not established: any live occurrence of the repair (needs ≥0x80000000 increments; ~7 h at the
measured 40-mover fine-map rate).

## Functions
As SEP-03.1 for writers/readers/compactors. Additional (ABIs from assembly):

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f14cdf0 | SpatialMap_CollectRegionObjects | thiscall ECX map, [4] `&{x,y}`, [8] output table; RET8 | non-empty cell: stamp+1 (query) and +2 (cell) (14ce44..4c); collects only +40 `0x10000000` objects (14ce7f) |
| 6f14dde0 | SpatialLinks_AppendRecords | thiscall ECX table (map+6C), [4] src, [8] n; RET8; EAX 1/0 | n==0→1; count+n>capacity: growth(+14)==0 → **0 without storing** (14de08..30); else 6f1c5130(bytes=(cap+max(inc,need))*8, 1) and copy; caller 6f14d9e0 ignores EAX |
| 6f1c5130 | DynamicTable_ResizeBacking | thiscall ECX, [4] bytes, [8] keep flag; RET8; EAX always 1 | SMemReAlloc(405)/SMemAlloc(401) with flags 8 (zero-fill), result stored without NULL check (1c5170/1c517a) |
| 6f14cf20 | SpatialMap_CreateObject | thiscall ECX map, [4] payload (mover), [8] descriptor; RET8; EAX object | 6f14beb0 → pool owner+5D8; rect `(-1,-1,-1,-1)`, +2C map, +30 payload, +34..40 = 0 |
| 6f14c880 | SpatialPool_AllocateObject | thiscall ECX pool, [4]/[8] link words; RET8; EAX object (element+4) | recycled list +14 first (LIFO), else 6f06a320 |
| 6f06a320 | ObjectPool_AllocateElement | thiscall ECX pool, [4] zero flag, [8]/[C] tags; RET0C | raw free list empty → SMemAlloc(size*count+4) (06a361), elements threaded ascending |
| 6f14d750 | SpatialObject_ReturnToPool | thiscall ECX object; RET0 | header object-4 pushed on +14, live +18-- |

## Stamp rules (instruction + oracle; frozen `expected-SEP-03.2.json` sha256 `0b1a6583042f9b7819811d839a0527ca064544c2ba256670c850b3d5b81e4f39`)

| Operation | Counter change | Object stamp written |
|---|---|---|
| separation query 6f170c00, clipped rect non-empty | +1 (query stamp) + 1 per non-empty cell; e.g. 0→8 for 7 cells, 8→10 for 1, 10→11 for an empty in-map rect | kind 1 → query stamp (accepted or rejected); other kinds → cell stamp |
| query with empty/fully clipped rect | 0 (`(8,8,9,9)`, `(-4,-4,-1,-1)`, `(2,2,2,5)`) | none |
| collector 6f14cdf0 | +2 on a non-empty cell, 0 on empty | flagged objects only |
| dirty / full compaction | reset to 0 first if `>0x7fffffff`; then +1 per non-empty processed cell | live kept/removed objects → cell stamp |

Skip tests: object stamp == query stamp, == current cell stamp, or == `0xffffffff` (dead). **The repair only resets the map counter;
object stamps are never cleared**, so after a reset the counter revisits old stamp values.

| Case | Result | Class |
|---|---|---|
| control (no counter jump): R stamped `15`, later queries | R always returned | reachable |
| R stamped `15`; counter jumped to `80000000`; dirty compaction repairs to 0 (+1 → `1`); ordinary queries advance the counter to `14`; the next full-map query gets stamp `15` = R's stale stamp | that query returns `[P,Q]` (R skipped, counter `18` after); the following query `[P,Q,R]` | **labelled** |
| R's only record in a dirty cell; counter `80000000` → repair → cell stamp `1` == R's stamp | compaction unlinks R's live insertion (cell 54 empty, R refs 0, R rect unchanged → stays invisible until its rectangle changes); query `[S]` vs control `[R,S]` | **labelled** |
| counter `fffffffe`, query stamp `ffffffff` | live object gets stamp `ffffffff` (dead sentinel); next compaction unlinks its records and returns it to the pool while its mover still points at it | **labelled**; unreachable while maps compact every 0.1 s (needs 2^31 increments between two repairs) |

Live rates (SEP-03.1 captures, per clock unit): fine map ≈85.5k–86.7k with 40 moving Footmen, ≈100 with 2; proximity ≈180–613
with 40. First repair: fine map ≈2^31/86k ≈ 25,000 clock units (≈7 h) under that load; proximity much later.

## Fresh blocks, candidate order, reclamation

| Item | Oracle | Live |
|---|---|---|
| Link table: ctor growth `0x20000`, capacity 0, data `-1` | first record → SMemAlloc **1,048,576** bytes flags 8 (caller 6f1c517a) | proximity and fine: 1 MiB at the first mover position commit (backtrace 14de26←1c5130←14da5a←14d9c2←14e8a8←16057e/1606d7←160481/16048a←15f7be) |
| Growth | at update 257 (count 131,328) SMemReAlloc 1 MiB→**2 MiB** (caller 6f1c5170); capacity 131,072→262,144; vector never shrinks | not reached (max high-water 8,525 fine, 157 proximity) |
| Candidate order across growth | chains (as kind/payload) and full query `[A,B,C]` equal to a pre-grown free-list control; digest `8699ebcb…2fe` | — |
| Object pool owner+5D8 | element 0x48, 64/block; block = SMemAlloc **4,612** (caller 6f06a366); object = block+8, stride 72, ascending; 65th object opens block 2 | blocks at load: raw 1,65,129,193,257; burst at tick 60 opens raw 321 |
| Reclamation | retire O1,O4,O2 → returned **during the next compaction** in cell order O4,O2,O1 → recycled `[O1,O2,O4]`; query `[O5,O3,O0]` keeps relative order; new objects pop O1,O2,O4 then a raw element; newcomers enter chain heads `[N3,N2,N1,N0,O5,O3,O0]` | 40 objects recycled at tick 90's deadline; the 20 allocations at tick 100 pop them exactly LIFO (both orders) |

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_SEP-03.2_spatial_stamps_blocks.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/sep032.json --expected $R/SEP-03.2/expected-SEP-03.2.json
python3 tools/frida/research/sep03_map06_analyze.py spatial $R/SEP-03.1/captures/spatial_ba-observe-1.jsonl   # link_reserve, pool_blocks, pool_reuse, cadence.*stamp*
```

## Provenance
game.dll `d51e5680…d8236`; harness `68d17ddf…db18`; oracle `d256510b…13ba`; live evidence = SEP-03.1 captures (hashes there).

Reproducibility: re-run in compare mode (`--expected`, same harness/oracle hashes) after all other work: exit 0, `expected-SEP-03.2.json` unchanged.

## Captures
None new; uses `SEP-03.1/captures/spatial_ab-observe-1` and `spatial_ba-observe-1` (complete, controls equal).

## Observer controls
As SEP-03.1 (181/181 markers equal in both orders).

## Exclusions
Allocation failure → SEP-03.3/MAP-05.3; map release/reuse → MAP-05.1; collector order on save → MAP-06.2.

## Mismatches preserved
* Ledger (`-separation.md`): "General wrap/stale-stamp recovery is not yet validated" / "counter reset or stamp repair elsewhere
  has not been recovered" → the only repair is the compaction-time counter reset; there is no object-stamp repair (three labelled
  consequences above).
* First oracle draft of the stale-stamp case used jump `7ffffff0` (no repair; loop ended by its guard) — corrected before freezing.

## Proposed integration (not applied)
No new Ghidra rows beyond SEP-03.1 (pool/link names are listed there). `proposed-docs-SEP-03.2.md`.
Proposed new ID (text): **SEP-03.5** decide the engine's stamp representation (native counter + reset, or epoch-safe), and add a
regression that runs ≥2^31 simulated increments (or an equivalent epoch jump) proving no candidate/membership loss in the engine.

## Suggested failing engine regressions
1. Stamp arithmetic: per query `+1+nonempty`, per collector `+2`, per compaction `+nonempty`, reset only when `>0x7fffffff` and
   only at compaction start; assert counter words after the six queries above (`0,8,10,11,11,11,11`).
2. Link storage: first record allocates 131,072 slots; the 131,073rd outstanding record grows to 262,144 without changing any
   chain order or query result; storage never shrinks while the map lives.
3. Object reuse: retire three movers, run one maintenance deadline, create four: the first three reuse the retired objects in
   reverse destruction order and each newcomer is first in its cells.
