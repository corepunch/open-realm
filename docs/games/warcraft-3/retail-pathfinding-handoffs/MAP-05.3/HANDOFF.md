<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-05.3/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **MAP-05.3**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/MAP-05.3/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-MAP-05.3.json` -> [`MAP-05.3-expected.json`](../../../../../tools/ghidra/fixtures/research/MAP-05.3-expected.json) (uncompressed sha256 `e9e5bc03f5c21a65211f566d878a82802d1265ec0d4db8db75fe7e8aa3508b88`, 9906 bytes)

# MAP-05.3 handoff — reachable allocation failure and metadata/dead-record cleanup thresholds

**Status.** Instruction-verified: no map/spatial allocation in 1.27.1 has a reachable soft-failure result. Every site goes through
Storm `SMemAlloc`/`SMemReAlloc` (flags 8) which terminate the process on failure (`SErrDisplayError(8,…)` + `ExitProcess(1)`),
and no caller checks for NULL; the only explicit refusal (`SpatialLinks_AppendRecords` with growth 0) has no retail trigger. So
**no partial links can survive a reachable failure** — the process ends. Original-code verified: a census of all 28 Storm
allocations reached while building registry/owner/spatial map/objects/links, and a **labelled** NULL sweep showing each
map/spatial site faults on first use; metadata (fine-search) and dead/removal records are reclaimed by one maintenance deadline
with no count threshold (2,100 records → 36 live). Labelled: 24-bit link-index exhaustion (index `0xffffff` = empty sentinel).
Not established: any live allocation failure (not reachable without intervention; not attempted live, as required).

## Allocation sites (oracle census, frozen in `expected-MAP-05.3.json` sha256 `e9e5bc03f5c21a65211f566d878a82802d1265ec0d4db8db75fe7e8aa3508b88`)

| # | Bytes, flags | Caller (return VA) | Owner of the storage | Labelled NULL result |
|---|---|---|---|---|
| 0–3, 5, 7, 9, …, 21 | 256/528/512, 8 | 6f1c4f6f (6f1c4f40 table init) | registry/owner tables (PathRegistry_Construct, PathOwner_Construct) | latent: scenario completes, NULL stored unchecked |
| 4 | 256, 8 | 6f1c4f6f | owner clock heap table | NULL-page fault in heap insert 6f04fa40 (EIP 6f04fad8) during SpatialMap_Init |
| 6, 8, …, 20 | 32, 8 | 6f157f83 (PathOwner_Construct) | owner sub-objects | latent |
| 22 | 16,384, 8 | 6f1c517a (DynamicTable_ResizeBacking) | registry 8-byte slot table (copy 6f15cb50 from 6f15b960) | fault 6f15cb7b |
| 23 | 1,024 (16×16×4), 8 | 6f1c517a | spatial map cell table | fault 6f14ddb6 (6f14dd40 fill) |
| 24 | 36 (9 words), 8 | 6f1c517a | dirty bitmap | fault 6f14dc76 (6f14dc00) |
| 25 | 2,308 (64×36+4), 0 | 6f06a366 (ObjectPool_AllocateElement) | maintenance-request block | fault 6f06a37b |
| 26 | 4,612 (64×72+4), 0 | 6f06a366 | spatial object block | fault 6f06a37b (SpatialMap_CreateObject) |
| 27 | 1,048,576, 8 | 6f1c517a | link table first block | fault 6f14e9db (record copy) |
| growth | 2,097,152, 8 | 6f1c5170 (SMemReAlloc) | link table growth | fault 6f14e9d5 (SEP-03.3) |

Storm (instruction-verified, Storm.dll `36339f69…eb72`): 401 `SMemAlloc` 1502b6d0 RET10, 405 `SMemReAlloc` 1502c760 RET14, 403
`SMemFree` 1502bce0 RET10; failure paths 1502b29d/1502b35f/1502b37f call `1501c6a0` then `ExitProcess(1)`; flags & 8 zero-fills.
Game thunks 6f07c6d2/6f07c6d8/6f07c678 → IAT a7c7ec/a7c7e8/a7c884 = ordinals 401/405/403.

## Cleanup thresholds (instruction + oracle + live)

| Item | Rule | Evidence |
|---|---|---|
| metadata records (kind 2, PathFine_AddCellSearchLink 6f14d890 from PathFine_FindOrCreateNode) | always dirty their cell; reclaimed by the next fine-map maintenance; no count threshold | oracle: 2,000 metadata + removal/dead records over 1,024 dirty cells → one callback → 0 metadata, 0 dead/removal left, live 36 = memberships, free 2,064 + live 36 = high-water 2,100; live: ≤1,359 links between two fine deadlines, 44,889 in 70 s, all reclaimed |
| dead/removal records | reclaimed at the next deadline of that map (or full sweep on save/release); objects returned when their last record goes | SEP-03.1 oracle + live |
| stamp | reset `>0x7fffffff` → 0 at compaction start | SEP-03.1/03.2 |
| sampled cleanup 6f14e180 (cap 256) | unreferenced (dead code) | static scan |
| record/link counts | **no threshold**: storage grows by `0x20000` links and never shrinks while the map lives | SEP-03.2 |
| 24-bit link index (labelled) | high-water placed at `0xfffffe`: the 2nd insertion gets index `0xffffff` and its cell reads as empty; the 3rd stores head `000000` (aliases index 0); query then dereferences the aliased payload (fault 6f170b92) | reachability requires 16,777,215 links high-water in one map (128 MiB); maximum observed 8,525; **not shown reachable** |

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_MAP-05.3_allocation_failure.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/map053.json --expected $R/MAP-05.3/expected-MAP-05.3.json
```

## Provenance
game.dll `d51e5680…d8236`; Storm `36339f69…eb72`; harness `68d17ddf…db18`; oracle `ecd9e752…97df`.

Reproducibility: re-run in compare mode (`--expected`, same harness/oracle hashes) after all other work: exit 0, `expected-MAP-05.3.json` unchanged.

## Captures / observer controls
No new captures (metadata/compaction live numbers from SEP-03.1 captures; controls equal).

## Exclusions
Fine node cap refusal (FINE-03.1, done); adaptive map storage (ACC-05.1, done); owner/registry allocations outside the map family
(BASE-03.1). Proposed (text only): **MAP-05.4** decide whether the engine must bound or detect the 24-bit spatial link index
(16,777,215) — retail silently corrupts at that point.

## Mismatches preserved
* GROUP-04.7/corpus text: "Native heap allocation/failure … remain BASE-03.1/MAP-05.3" — for map/spatial storage the answer is
  "failure terminates the process; no recoverable result exists".
* The labelled NULL sweep differs from retail by construction (Storm never returns NULL); kept only to prove the absence of checks.

## Proposed integration (not applied)
Pool/link rows in `mapping-rows-SEP-03.1.txt`. `proposed-docs-MAP-05.3.md`.

## Suggested failing engine regressions
1. After 2,000 fine-search metadata links plus removals in one 0.1 s window, the next maintenance leaves only live memberships and
   free+live = high-water; no count-triggered early cleanup occurs.
2. Allocation failure policy: injected allocator failure in spatial/map storage aborts (fatal) instead of returning a partial
   membership; assert no surviving partial links in any engine state reachable afterwards (the process/test terminates).
