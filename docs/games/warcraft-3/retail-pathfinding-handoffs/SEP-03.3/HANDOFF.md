<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-03.3/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-03.3**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-03.3/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-03.3.json` -> [`SEP-03.3-expected.json`](../../../../../tools/ghidra/fixtures/research/SEP-03.3-expected.json) (uncompressed sha256 `890ecea517eae5618616874f38d7227f4d46977c13a5fbd60b3b06cecd653185`, 7226 bytes)

# SEP-03.3 handoff — spatial growth/failure during an update: partial membership and movement outcome

**Status.** Instruction-verified: spatial link growth cannot fail softly in retail. Storm `SMemAlloc`/`SMemReAlloc` (flags 8)
never return NULL — on failure they call `SErrDisplayError(8 = ERROR_NOT_ENOUGH_MEMORY, …)` and `ExitProcess(1)`; the game
callers store the result without a NULL check; the only non-allocator refusal (`growth increment == 0`) is unreachable because
the sole initializer stores `0x20000` (live-confirmed for both maps). Original-code verified: growth in the middle of one
`6f14e770` update keeps complete membership and identical chains/queries versus a pre-grown control. Live: growth to the first
1 MiB block happens inside the first mover position commit of each map and movement continues normally (both orders, controls
equal). **Labelled interventions** show what a soft failure would leave (never reachable). Not shown: a live growth beyond
131,072 links (high-water 8,525 in these scenes; oracle covers it).

## Functions (assembly)

| VA | Role | ABI / evidence |
|---|---|---|
| 6f14d9e0 | SpatialMap_PrependCellRecord | RET10; free list empty → `index = +88`, record built, `6f14dde0(&rec,1)` at 14da55, **EAX ignored**, then +B0++/obj+3C++ and cell head := index (14da87..9c) |
| 6f14dde0 | SpatialLinks_AppendRecords | RET8, EAX 1/0; refusal only when `count+n > capacity && growth(+14)==0` (14de05..30); growth path never returns 0 (6f1c5130 returns 1) |
| 6f1c5130 | DynamicTable_ResizeBacking | RET8, EAX=1 always (1c51c4); SMemReAlloc(ptr,bytes,file,0x174,8) at 1c516b or SMemAlloc(bytes,file,0x174,8) at 1c5175; result stored to +4 and data +C without check |
| 6f14c090 | SpatialLinks_Construct | only writer of table +14 for spatial maps: `{capacity 0, growth 0x20000}` from SpatialMap_Construct 6f14c280; static scan 6f14b000–6f15ffff finds no other store to map+80 |
| Storm 401 `SMemAlloc` (Storm.dll `1502b6d0`, RET10) | heap alloc 1502b250: size > `0x7fffffff` or allocator NULL → `1501c6a0(8, file, line, 0, 0, 1)` then `ExitProcess(1)` ([15041194]) at 1502b2a4/1502b366/1502b386; zero-fill when flags & 8 |
| Storm 405 `SMemReAlloc` (`1502c760`, RET14) | NULL/invalid ptr → SMemAlloc; else 1502b400: flags & `0x10` absent (game passes 8) → move via 1502b250 (same fatal path) |

## Behaviour (frozen `expected-SEP-03.3.json` sha256 `890ecea517eae5618616874f38d7227f4d46977c13a5fbd60b3b06cecd653185`)

| Case | Class | Result |
|---|---|---|
| A,B take 13 links; C's update `(19,19,25,25)` needs 36 with 5 free | reachable (original code, host storage) | one SMemReAlloc 1 MiB→2 MiB (caller 6f1c5170) inside the update; count 131,067→131,103, capacity 131,072→262,144; C in all 36 cells, refs 36; query `[C,B,A]` |
| same updates on a pre-grown map (free-list reuse, no Storm call) | control | identical chains in rows 19–24 and query `[C,B,A]` |
| Storm returns NULL at the growth (labelled) | intervention | wrapper pointer and data become `0`; the record copy 6f14e9b0 faults (EIP `6f14e9d5`, write near `0x100000`) — process crash, no partial-but-consistent state |
| growth increment forced 0 at count 131,068 (labelled) | intervention | 6 insertions: 4 stored, last 2 refused; records +6 but high-water stops at 131,072; cells 1300/1301 both point at **index 131,072 (beyond capacity)**, their previous heads are lost, object refs 6, saved rect already updated — partial, corrupt membership |

Movement outcome. 6f14e770 returns void and its callers (Mover_UpdateProximityBounds 6f160579, Mover_UpdateFineOccupancyBounds
6f1605aa/6f1606d2) have no failure branch, so the reachable outcomes are: (a) growth succeeds and movement is identical to the
no-growth control, or (b) the process terminates (Storm fatal error). Live: the first link allocation (1 MiB, both maps) occurs in
the first mover commit (backtrace `14de26←1c5130←14da5a←14d9c2←14e8a8←16057e/1606d7←160481/16048a←15f7be`) and the unit continues
its scripted move; JASS markers equal the observer-free control (SEP-03.1 captures).

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_SEP-03.3_spatial_growth_failure.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/sep033.json --expected $R/SEP-03.3/expected-SEP-03.3.json
objdump -d -M intel --start-address=0x1502b250 --stop-address=0x1502b390 /run/media/lofcz/ssd_external/Games/w3-research/Storm.dll
objdump -p /run/media/lofcz/ssd_external/Games/w3-research/game.dll | grep -A70 'DLL Name: Storm.dll'   # IAT a7c7ec=401, a7c7e8=405, a7c884=403
```

## Provenance
game.dll `d51e5680…d8236`, Storm.dll `36339f69727f0f3dcb4431a9567b84f56007c331cfa3b183862af43e8314eb72` (research and original
install identical); harness `68d17ddf…db18`; oracle `8d5bfeeb…6ee5`. Live evidence: SEP-03.1 captures.

Reproducibility: re-run in compare mode (`--expected`, same harness/oracle hashes) after all other work: exit 0, `expected-SEP-03.3.json` unchanged.

## Captures / observer controls
No new captures; SEP-03.1 `spatial_ab/ba-observe-1` (link-reserve events, capacity `0x20000`, growth field `131072` in every
maintenance record for both maps) with equal controls.

## Exclusions
Map-wide allocation census and other allocation sites → MAP-05.3; 24-bit index exhaustion (non-OOM) → MAP-05.3; fine node cap
32,768 → FINE-03.1 (done).

## Mismatches preserved
* Ledger (`-separation.md`): "Allocation growth/failure remains unverified" → growth verified (oracle+live first block); failure is
  fatal by construction.
* Ledger corpus statement "native heap allocation/failure … remain BASE-03.1/MAP-05.3" — the spatial part is answered here; the
  ignored `6f14dde0` result is a latent defect only reachable with a zero growth increment (no retail writer).

## Proposed integration (not applied)
Rows for 6f14dde0/6f14c090 are in `mapping-rows-SEP-03.1.txt`. `proposed-docs-SEP-03.3.md`.

## Suggested failing engine regressions
1. Grow spatial storage in the middle of one rectangle update (fill to capacity−5, then a 36-cell insertion): membership/order equal
   to an unconstrained run.
2. Engine policy for allocation failure: treat as fatal (abort) rather than returning partial membership; assert no API reports
   "spatial insert failed" with a partially updated object.
