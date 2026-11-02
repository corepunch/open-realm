<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-03.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-03.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-03.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-03.1.json` -> [`SEP-03.1-expected.json`](../../../../../tools/ghidra/fixtures/research/SEP-03.1-expected.json) (uncompressed sha256 `fdc2e65af02d8bbf08c00b7bbf9ee727a80d19acb8e7a922d48a1bba8eb05161`, 241682 bytes)

# SEP-03.1 handoff — spatial insertion/removal order, metadata/dead records, cleanup thresholds and cadence

**Status.** Instruction-verified (assembly of every writer/reader/compactor/scheduler below), original-code verified
(Unicorn: real registry/owner/map constructors, 8 insertion×removal×mode cases, 3 lifetime cases, 10 threshold cases,
12 maintenance deadlines; an independent model predicts every chain, count, dirty bit and destruction point) and
live-verified (two insertion/removal orders on `RS-SEP-03.1-spatial_ab/ba`, each with an observer-free control whose 181 JASS
markers are identical; 697/698 maintenance callbacks per map). Not established: the writer of the high byte of object `+3C`
(flags `0x10`/`0x80` observed live, masked out by all count logic); engine behaviour (owner).

## Functions (ABIs from assembly)

| VA | Role (Ghidra name) | ABI | Evidence |
|---|---|---|---|
| 6f14e770 | SpatialObject_UpdateRectangle | thiscall ECX object, [esp+4] rect `(y0,x0,y1,x1)`; RET4 | 14e793..7ad unchanged→exit; 14e82a/85e strips via 6f1d4ae0 processed **last→first** (14e840/873); removal strips (kind 0) before insertion strips (kind `0x1000000`); disjoint 14e88d..8a3; rect copy 14e8ab |
| 6f14d960 | SpatialMap_EmitRectangleRecords | thiscall ECX map, [4] rect, [8] object, [C] kind word; RET0C | clip to +54..60 (14d96a..998); outer Y, inner X; 6f14d9e0(x,y,obj,kind) |
| 6f14d9e0 | SpatialMap_PrependCellRecord | thiscall ECX map, [4] x, [8] y, [C] object, [10] kind word (0 / `0x1000000`); RET10 | kind 0 sets dirty bit +98 (14d9f8..a16); pop free head +AC (14da5f..85) else append via 6f14dde0 at index +88 (14da35..55, **result ignored**); +B0++ 14da87, obj+3C++ 14da8d; head low24 = new index, high byte kept 14da90..9c |
| 6f14d890 | PathFine_AddCellSearchLink | thiscall ECX fine map, [4] x, [8] y, [C] lo16, [10] hi16; RET10 | always sets dirty bit (14d8af..bf); kind `0x2000000` record, payload = words; +B0++ (no object count) |
| 6f14dae0 | SpatialObject_Retire | thiscall ECX object; RET0 | if !(+40 & `0x10000000`) emit kind-0 records over +1C (14daf6); registry release 6f1cbfb0 (14db0c); +38 = -1 (14daff), +14/+18 = -1; 6f14dab0 (14db23) |
| 6f14dab0 | SpatialObject_ReleaseIfUnreferenced (named by another agent) | [esp+4] object (ECX unused); RET4; EAX 1 live / 0 dead | +38 != -1 → 1; else if (+3C & 0xffffff)==0 → vtable+10(0) → 6f1c5490 → vtable+4 6f14d750 (pool) |
| 6f14e050 | SpatialMap_CompactCell | thiscall ECX map, [4] cell; RET4 | empty → no stamp change; else +B4++ (14e07d) = cell stamp; keep kind 1 with obj+38 ∉ {-1, cell stamp} (14e0bb..d2); else unlink, push slot LIFO on +AC (14e10b..125), +B0-- , obj+3C-- (14e12f); 6f14dab0 for every object record; live → obj+38 = cell stamp (14e144) |
| 6f14df20 | SpatialMap_CompactDirtyCells | thiscall ECX map; RET0 | stamp >`0x7fffffff` → 0 (14df29..35) **before** scan; ascending set bits; clears each processed word (14df9b) |
| 6f14dfc0 | SpatialMap_CompactAllCells | thiscall ECX map; RET0 | same reset (14dfc5..d1); every cell (+38); bitmap untouched |
| 6f14e180 | SpatialMap_CompactSampledCells | thiscall ECX map, [4] request (cap 256); RET4 | **unreferenced**: no xref, no absolute pointer, no E8/E9 rel32 to it in the whole .text |
| 6f14e000 | SpatialMap_CompactDirtyWord | thiscall ECX map, [4] word; RET4 | unreferenced (same scan); orphan `call 6f14df20; ret 4` at 6f054410 also unreferenced |
| 6f0543c0 | SpatialMap_RunMaintenanceRequest | thiscall ECX request; RET0 | cancelled (+10 & `0x10000`) → release request; else 6f14df20(+18 map) (0543d0); repeating (+10 & 1) → 6f053780 |
| 6f0523d0 | SimClock_DrainUnitTickRequests | thiscall ECX clock = owner+164; RET0 | while heap count +20 >1 and time +40 ≥ top deadline (COMISS 0523ef): pop, clear `0x20000`, time := deadline, dispatch; caller SimClock_AdvanceUnitTicks 6f054230 (0542aa/0542be) |
| 6f053780 | SimClock_RearmUnitTickRequest | thiscall ECX clock, [4] request; RET4 | deadline = time(+40) + period(+8) by Math_Add 6f06fbb0; flags `&~0x10000 | 0x20000`; heap insert 6f04fa40 (key deadline, tie sequence +14) |
| 6f14e3f0 | SimClock_ScheduleUnitTickRequest | thiscall ECX clock, [4] map, [8] &type(=4), [C] period word, [10] &deadline, [14] sequence; RET14; EAX request | request pool +38 or 0x24-byte block element; +10 = `0x20000` |
| 6f14c990 | SpatialMap_Init (vtable+C) | thiscall ECX map, [4] descriptor; RET4 | period = max([6fd53a4c], [6fcd539c]=1e-4) (14c9ef..0f); deadline = owner+1A4 + period; sequence = ++owner+1B4; +B8 = request, `|= 1` |
| 6f003c40 | SpatialMap_InitMaintenancePeriod | static initializer; RET0 | [6fd53a4c] = Math_Divide(1.0, 10.0) → word `3dccccce` (oracle) |
| 6f170c00 / 6f170b30 | separation rectangle/cell readers | as SEP ledger (RET8 / RET0C) | query stamp +1 when the clipped rect is non-empty (170c4a), +1 per non-empty cell (170b58) |
| 6f15ee60 | Mover_RetireSpatialObjects | thiscall ECX mover, [4] fwd; RET4 | Retire +94 (prox) then +98 (fine) |

## Fields

| Struct+off | Meaning | Write sites | Read sites |
|---|---|---|---|
| map+28 | cell words: high byte terrain/flags, low24 head (`0xffffff` empty) | 6f14d9e0, 6f14d890, 6f14d2e0, 6f14e050, 6f14dd40 fill | 6f170b30, 6f14cdf0, 6f14e050 |
| map+6C..88 | link table (8-byte `{kind<<24|next, payload}`): +78 data, +80 growth `0x20000`, +84 capacity, +88 high-water | 6f14c090, 6f14dde0, 6f1c5130 | writers, compaction, readers |
| map+98/+A8 | dirty bitmap data / word count | 6f14d9e0 (kind 0), 6f14d890; cleared 6f14df20 | 6f14df20 |
| map+AC | free-list head (LIFO) | 6f14c280/6f14c990 (`0xffffff`), writers pop, 6f14e050 push | writers |
| map+B0 | outstanding record count | ctor 0; ++ writers; -- 6f14e050 (not reset by init/release, MAP-05.1) | — |
| map+B4 | stamp counter | ctor/init 0; ++ readers/collector/compaction; reset 6f14df20/dfc0/e180 | same |
| map+B8 | maintenance request | 6f14c990, 6f14d1d0; 0 by 6f14cac0 | 6f14cac0 |
| obj+1C..28 | rect `(y0,x0,y1,x1)`; initial `(-1,-1,-1,-1)` from 6fa90b10 | 6f14cf20, 6f14e770 | 6f14dae0, save |
| obj+38 | stamp; `-1` = dead | 6f14dae0, 6f14e050, readers | readers, 6f14dab0 |
| obj+3C | low24 outstanding records (high byte flags, writer open) | ++ writers, -- 6f14e050 | 6f14dab0 |
| obj+40 | `0x10000000` region-style: no removal on retire | region code | 6f14dae0, 6f14cdf0, 6f14d000 |
| request (0x24) | +4 deadline, +8 period, +C clock, +10 flags (`0x20000` queued, `1` repeat, `0x10000` cancelled), +14 sequence, +18 map, +1C 4 | 6f14e3f0, 6f053780, 6f0523d0, 6f14cac0 | 6f0543c0, heap |

## Behaviour (frozen: `expected-SEP-03.1.json`, sha256 `fdc2e65af02d8bbf08c00b7bbf9ee727a80d19acb8e7a922d48a1bba8eb05161`)

| # | Rule | Oracle | Live |
|---|---|---|---|
| 1 | Every membership entry prepends; a shared cell lists newest first. Insert PQ → query `[Q,P,R]`, QP → `[P,Q,R]` (shared first cell 18) | 8 cases | AB: prox cell 428 `[B(52),A(50)]`; BA: `[A(51),B(50)]` at tick 6 |
| 2 | Overlap-preserving moves keep retained-cell records (`[Q,P,R]` unchanged); leaving and re-entering a cell re-prepends (P first) | 8 cases | — |
| 3 | Removal (move away or Retire) prepends kind-0 records and dirties those cells; Retire marks +38=-1; never-inserted objects are destroyed inside Retire | yes | A/B: retire then recycle |
| 4 | Fine-search metadata (kind 2) always dirties; no object count | 2 records | ≤1359 links between two fine deadlines, 44,889 in 69.7 s |
| 5 | Compaction keeps first effective live insertion per object in order, unlinks removal/older/dead/metadata, pushes slots LIFO; a dead object returns to the pool (LIFO) when its last record is unlinked | free lists e.g. PQ/PQ `12,44,11,43,10,42,9,25…` vs PQ/QP `12,38,11,37,10,36,9,25…`; destruction by ascending cell (P@28, Q@36) in all orders | removed at tick 50/52 → recycled inside the next deadline's compaction (clock `409ffff7`/`40a6665d`) in both orders; 20/20 reuse allocations pop the recycled list LIFO |
| 6 | Region-style objects (+40 `0x10000000`) emit no removal records; their records survive dirty compaction and are reclaimed only by a full sweep | records 4 linger until 6f14dfc0 | ChangeLevel teardown: 158 region objects retired (6f063b8b) are recycled only during SpatialMap_Release's full compaction (MAP-06.1) |
| 7 | Cadence: one repeating request per spatial map (proximity +234, fine +238), unit-tick clock, period `3dccccce`; deadlines `3dccccce,3e4cccce,3e99999a,3ecccccd,3f000000,3f199999,…` (soft-float accumulation, 10th `3f7ffffd`); proximity fires before fine at equal deadlines (sequence tie) | 12 rounds, early ulp never fires | 697/698 callbacks per map, every gap 0.1, order proximity→fine at all 697/698 deadlines, first words identical |
| 8 | Thresholds: stamp `>0x7fffffff` → 0 before scanning (`7fffffff` kept); no record/dirty-count threshold; full sweep only from PathMaps_Save 6f15c750 and SpatialMap_Release 6f14cac0 | starts 7ffffffe/7fffffff/80000000/fffffffe/ffffffff × 2 drivers | no reset in 70 s (stamp rates below) |

Live stamp rates (per clock unit ≈ game second): proximity 698–1131 overall, 47.5 with two movers, 179–613 with 40 movers;
fine 9.4k–10.1k overall, 100 with two movers, **85.5k–86.7k with 40 movers** (repair after ~7 h of that load; see SEP-03.2).

## Reproducer (repository root)
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_SEP-03.1_spatial_lifecycle.py \
  --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --report /tmp/sep031.json --expected $R/SEP-03.1/expected-SEP-03.1.json
for s in spatial_ab spatial_ba; do python3 tools/frida/research/sep03_map06_make_map.py \
  --base /GitHub/wc3-analysis/reports/pathfinding-1.27/runtime/Human02Interlude-original.w3m \
  --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool --scenario $s --output /tmp/RS-$s.w3m
  cp -n /tmp/RS-$s.w3m /run/media/lofcz/ssd_external/Games/w3-research/Maps/RS-SEP-03.1-$s.w3m; done
tools/frida/research/sep03_map06_runs.sh $R/SEP-03.1/captures \
  'spatial_ab-observe-N:observe:RS-SEP-03.1-spatial_ab:150:--preload-names rs-spatial_ab.txt' \
  'spatial_ab-control-N:control:RS-SEP-03.1-spatial_ab:150:--preload-names rs-spatial_ab.txt'   # same for spatial_ba
python3 tools/frida/research/sep03_map06_analyze.py spatial $R/SEP-03.1/captures/spatial_ab-observe-1.jsonl
python3 tools/frida/research/sep03_map06_analyze.py markers $R/SEP-03.1/captures/spatial_ab-observe-1-rs-spatial_ab.txt $R/SEP-03.1/captures/spatial_ab-control-1-rs-spatial_ab.txt
```
`sep03_map06_runs.sh` wraps each run in `flock …/_env/live.lock` with `--data …/w3-research --remote 127.0.0.1:27048 --x11-display :97`.

## Provenance
game.dll `d51e5680…d8236`; Storm.dll `36339f69…eb72` (research copy = original). Base map `Human02Interlude-original.w3m`
`199683be…2104`. Maps: spatial_ab `278f4c71…6251`, spatial_ba `ceba5557…5765` (probe `c84fc4e8…` at build time; builder imports the
unchanged `make_wc3_pathfinding_map.py` `c336f39e…206a`). Scripts: harness `68d17ddf…db18`, oracle `970d3e27…90c0`, controller
`c8e9eb9d…` (captures) / `3e3854a0…` then `2999b547…6e24` (current; later edits add save cleanup, screenshots, UI actions and an all-movers flag only), observer v1 `81646c39…` (ab-observe-1),
v2 `84749a06…` (ba-observe-1, forgets names of recycled objects), v3 `42f4100a…` (adds compaction-begin events), analyzer `816adb39…`.
No seed is involved (no random draws in these paths; maintenance ordering is by deadline/sequence).

Reproducibility: re-run in compare mode (`--expected`, same harness/oracle hashes) after all other work: exit 0, `expected-SEP-03.1.json` unchanged.

## Captures
| Capture | sha256 | Status |
|---|---|---|
| spatial_ab-observe-1.jsonl | `f2723db5…c307` | complete, 3308 events, 181 markers, 0 errors; names of recycled objects become stale after tick 52 (observer v1) — use events ≤ tick 52 for names |
| spatial_ba-observe-1.jsonl | `f09a959b…1904` | complete, 3223 events, 0 errors |
| spatial_ab-control-1.jsonl (+preload `99ddf5a0…`) | `a71c1261…` | control, complete |
| spatial_ba-control-1.jsonl (+preload `bca1a2df…`) | `d5df648a…` | control, complete |

## Observer controls
Spatial_ab and spatial_ba: observer vs observer-free JASS marker lists equal (181/181 each, exact R2S strings including both units'
positions every 0.1 s). Hooks are entry/exit reads only.

## Exclusions
Stamp wrap/repair hazards and fresh blocks → SEP-03.2; growth/failure → SEP-03.3; map-level release/reuse → MAP-05.1;
save/load chain reconstruction → MAP-06.2; neighbour arithmetic → SEP-02/04 (other agent). Proposed new ID (text only):
**SEP-03.4** identify the writer and meaning of spatial object `+3C` high-byte flags (`0x10000000` set at proximity insertion,
`0x80000000` at retirement) and assert they never reach the low-24 count.

## Mismatches preserved
* Ledger (`-separation.md`, "Spatial record reclamation"): "6f14e180 samples work and caps its request at 256; its sampling
  algorithm and actual runtime scheduling remain open." → 6f14e180 (and 6f14e000, orphan 6f054410) are unreferenced in 1.27.1;
  runtime cleanup is only the per-map dirty compaction every 0.1 s plus full sweeps on save/release.
* Ledger "Do not infer that either verified complete driver runs every simulation tick" — confirmed: dirty driver runs per 0.1 clock
  unit per map; the full driver never runs periodically.
* Observer v1 kept names of recycled objects (spatial_ab-observe-1 shows `B.fine`/`A.fine` labels on later burst objects); fixed in v2;
  capture kept unchanged.

## Proposed integration (not applied)
`mapping-rows-SEP-03.1.txt` (27 rows incl. label 6fd53a4c; already written to Ghidra, see `ghidra-writes.jsonl`),
`proposed-docs-SEP-03.1.md`.

## Suggested failing engine regressions
1. Two movers entering one proximity cell in order A,B: cell enumeration `[B,A]`; reverse order gives `[A,B]`; an overlap-preserving
   move keeps order; leave+re-enter puts the mover first.
2. Remove A then B (and B then A) at tick t: both remain invisible to queries immediately; storage of both is reclaimed at the first
   maintenance deadline ≥ t, never earlier; reused slots/objects are popped LIFO.
3. Maintenance runs every 0.1 game-clock units per map (proximity before fine at equal deadlines), compacts only cells dirtied by
   removals or fine-search metadata; run 12 deadlines and assert words `3dccccce…3fa66663`.
4. Region-style membership (no removal records on retire) survives dirty compaction and is reclaimed only by save/map release.
