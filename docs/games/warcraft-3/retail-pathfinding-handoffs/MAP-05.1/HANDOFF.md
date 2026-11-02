<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-05.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **MAP-05.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-05.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-MAP-05.1.json` -> [`MAP-05.1-expected.json`](../../../../../tools/ghidra/fixtures/research/MAP-05.1-expected.json) (uncompressed sha256 `8d73801a22f62fe9f29c663734a03413dc6ffd810ff36c07b677627d0d2e54b9`, 8545 bytes)

# MAP-05.1 handoff — map/spatial allocation boundary, release and reuse (identities, links, cell contents)

**Status.** Original-code verified (Unicorn; owner, registry, spatial-map pool factory, object pool and timers all built by
original constructors; Storm imports = host storage) and instruction-verified: crossing the 131,072-link boundary, reclaiming
and reusing link slots and objects with exact identities, crossing the 64-object pool block boundary, and releasing a whole map
and re-creating it from the map pool. Live-verified: lazy first link block per map, pool blocks and LIFO object reuse
(SEP-03.1 captures), and whole-owner teardown/rebuild on ChangeLevel/RestartGame (MAP-06.1 captures). Not shown live: link
growth beyond the first block (scene high-water 8,525).

## Functions
See SEP-03.1 (writers, compaction, pools) plus:

| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f14efe0 | SpatialMapPool_CreateMap | ECX out, EDX descriptor, [4] init flag; RET4 | 6f151580 pop owner+598 recycled list (LIFO) or raw element + 6f14c280; vtable+C 6f14c990 |
| 6f14cac0 | SpatialMap_Release (vtable 6fa90a44+10) | thiscall ECX map, [4] unused; RET4 | 6f14dfc0 full compaction; request +10 `|= 0x10000`, map+B8 = 0; 6f14e590 erase all links, 6f14def0 free (count 0); dirty table erase/free (6f14e470/6f14de90); 6f14ca60 (cells freed, unregister, pool return via vtable+4) |
| 6f14ca60 | spatial map base release | thiscall ECX map, [4]; RET4 | cell table erase (6f14e530) and free (6f14dec0), +30/+40/+3C = 0, PathRegistry_UnregisterAndRelease |
| 6f15b670 | PathMaps_Release | thiscall ECX owner; RET0 | vtable+10(0) for +24C, +250, +234, +238, +23C..248, each slot cleared |

## Behaviour (frozen `expected-MAP-05.1.json` sha256 `8d73801a22f62fe9f29c663734a03413dc6ffd810ff36c07b677627d0d2e54b9`)

| Step (64×64 map from the pool factory) | Exact result |
|---|---|
| fill to high-water 131,054; A `(20,20,22,22)`, B `(20,20,23,23)` (13 links) | no Storm call |
| C `(19,19,25,25)` (36 insertions, 5 free) | SMemReAlloc 1 MiB→2 MiB (caller 6f1c5170); C records at indices 131,067…131,102 in emission order (cell 1235→131067 …); capacity 262,144 |
| retire C → 36 removal records 131,103…131,138; dirty compaction | 550 dirty cells, 130,868 slots reclaimed; free list equals the independent prediction (reverse reclamation order + previous list); every C insertion/removal slot is in it; C object recycled (pool head) |
| D = next object → **same memory as C**; D `(19,19,25,25)` | 36 slots popped from the free-list head in emission order (cell 1235→130818, 1236→130819, 1237→130822 …); 0 Storm calls; high-water stays 131,139 |
| cell contents after reuse | cell 1300 `[D@130831, B@131058, A@131054]`, 1302 `[D@130835, B@131060]`, 1560 `[D@130887]`; query `(18,18,26,26)` → `[D,B,A]` |
| object pool: create until two new blocks | blocks of **4,612** bytes (caller 6f06a366) at the 59th and 123rd new object (raw allocated 65, 129); object = block+8; retire three new objects (no records → destroyed in Retire): recycled `[122,121,120]`; next four creations reuse `122,121,120`, then a raw element |
| release map (vtable+10) with A,B,D still live | Storm frees **2,097,152** (links), **516** (dirty), **16,384** (cells) via 6f1c5197; request flags `00030001`, map+B8 0; map pool recycled `[map]`, live 0; **map+B0 stays 307 and +AC stays 130,890** (not reset); A/B/D +3C unchanged (4/9/36) |
| create a 32×32 map from the pool | **same identity** `40016008`; fresh state: link data `-1`, capacity 0, count 0, free head `0xffffff`, stamp 0, cells 1,024, new request (`4002302c`, not the old one); **records +B0 still 307** |
| drain clock past both deadlines | old request: cancelled, released without callback (flags `00010001`); one compaction callback for the new map |
| stale object A (live across the release) retired | 4 removal records written **into the reused map** (cells 660,661,692,693), A refs 8→4 after compaction, A never destroyed (its pre-release records were erased without decrementing) |

Live counterparts:
* First link block (1 MiB) per map at the first mover commit; object-pool blocks: 5 at load (raw 1,65,129,193,257), 6th at the
  40-unit burst (raw 321); 20/20 reuse allocations LIFO (SEP-03.1 captures).
* ChangeLevel and 8 RestartGame cycles: every mover (`Mover_RetireSpatialObjects` callers 6f16eb83 for removed units, 6f1466b8 for
  the teardown loop) and every region object (6f063b8b) is retired **before** `PathMaps_Release`; SpatialMap_Release's full
  compaction then reclaims all records (proximity 100→0, fine 7,076→0) and recycles all 258–260 objects; the owner is destroyed
  and a new owner/pools/maps are constructed (new map addresses; heap reuse sometimes repeats an address, e.g. `0xa5800a0` in
  RestartGame cycles 4/7/8). So the stale-object hazard above does not occur in these flows.

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_MAP-05.1_spatial_storage_reuse.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/map051.json --expected $R/MAP-05.1/expected-MAP-05.1.json
python3 tools/frida/research/sep03_map06_analyze.py lifetime $R/MAP-06.1/captures/changelevel-observe-1.jsonl
```

## Provenance
game.dll `d51e5680…d8236`; harness `68d17ddf…db18`; oracle `d0298b34…c943`; live: SEP-03.1 and MAP-06.1 capture hashes there.

Reproducibility: re-run in compare mode (`--expected`, same harness/oracle hashes) after all other work: exit 0, `expected-MAP-05.1.json` unchanged.

## Captures / observer controls
No new captures (see SEP-03.1: controls equal; MAP-06.1).

## Exclusions
Allocation failure (MAP-05.3, SEP-03.3); fine node storage growth (FINE-03.1 done); adaptive maps (separate pool owner+5B8, not
exercised); save/load reconstruction (MAP-06.2).

## Mismatches preserved
* Construction ledger: "full map release/reload unexecuted" → executed here (oracle) and live (MAP-06.1).
* New: map+B0/+AC survive release and pooled reuse (only the constructor zeroes +B0; init resets +AC). Harmless in observed flows
  because live release finds no outstanding records (+B0 = 0 after the release compaction).

## Proposed integration (not applied)
Rows in `mapping-rows-SEP-03.1.txt` (pool/map names). `proposed-docs-MAP-05.1.md`.

## Suggested failing engine regressions
1. Spatial storage: cross a capacity boundary inside one update, retire the crossing object, run maintenance, insert a new object
   with the same rectangle: it occupies the reclaimed slots in LIFO order, storage does not grow, queries list it first.
2. Map teardown: retire all movers/regions, release the map; assert zero outstanding records before storage is freed and that a
   re-created map starts empty (no inherited links, free list, dirty bits or maintenance callbacks).
