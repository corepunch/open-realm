<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-04.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-04.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-04.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-04.1.json` -> [`SEP-04.1-expected.json`](../../../../../tools/ghidra/fixtures/research/SEP-04.1-expected.json) (uncompressed sha256 `436190f8d21bec3f9c0e4e9c82bab3fa734feebfc49e667515d8fcd794b0fd6a`, 159228 bytes)

# SEP-04.1 handoff — complete live exact-overlap replay (seed, neighbour order, cooldown, trajectory)

**Status.** Live-verified with original-code replay and an identical repeat: cluster O9 of RS-SEP-02.2-triad (two row-0 hS00
units of owner 0 created at the same fine point (55.0, 55.0), a cell corner) — all 268 visits (61 bodies, 207 cooldown visits),
both random-direction draws, 30 accepted and 1 rejected retained-vector applications, the deadzone→cooldown7 cadence and the
final rest positions are recorded and every word reproduces with original pair/tail slices. The draw words are part of the single
shared path-owner stream (1,476 draws in the scene, each matched in order). Contrast O1 (same pair at a cell centre) never
separates (SEP-02.3). Not established: production of the initial owner words from map start (NUM-04.5); this handoff uses the
recorded words.

## Functions / fields
As SEP-02.2/02.3 (Separate_Update 6f1702f0, ApplyPendingDisplacement 6f16ffa0, ValidateEndpoint 6f16ee80, PathRandom_UnitDirection
6f1d19e0, PathRandom_Next 6f1b7130; sep +0x18/+0x1c vector, +0x20 low16 cooldown; mover +0x78/+0x7c position, +0x98 occupancy rect).

## Seed and neighbour order
* Owner words at map start of this map family `2002874931/738765888` → 8 draws at 6f1e9e25 (`PlayerSetup_ResolveRaces`) →
  `4273436052/209508436` before the first separation visit (identical across both runs and equal to the Payoff101 asserted state).
* Visit 1278 processes the owner list newest-first, so unit 46 (O9, created last in the scene) draws **first**:
  `4273436052,209508436 → 3022241195,141606968`. Unit 45 draws in visit 1279 from `230701424,2820413544` (after the other clusters'
  draws in the same pass) → `3727752651,2752512076`. A replay must therefore include every same-category overlap in the owner pass order.
* Each unit has exactly one candidate (the other); after the first application no further draws occur (distance ≥ 0.001).

## Trajectory (frozen: `expected-SEP-04.1.json`, sha256 `436190f8d21bec3f9c0e4e9c82bab3fa734feebfc49e667515d8fcd794b0fd6a`)

| Visit / unit | Position (words) | Retained vec | Apply (endpoint, valid, occ rect) | Contribution | Tail (vec, word) |
|---|---|---|---|---|---|
| 1278 / 46 | 425c0000,425c0000 | 0,0 | skipped (zero) | random, len `3f7ffff6` → `bd3f78ef,3e80de4a` | `bd0607dc,3e346a6a`, 0 |
| 1279 / 45 | 425c0000,425c0000 | 0,0 | skipped | random → `be42296d,3e302083` | `be07e9ce,3df693ee`, 0 |
| 1280 / 46 | 425c0000,425c0000 | bd0607dc,3e346a6a | `425bde7e,425cb46a` **1**, [55,55,56,56]→[55,54,56,55] | d `3e377ff2` | `bd904082,3ec22c31` |
| 1281 / 45 | 425c0000,425c0000 | be07e9ce,3df693ee | `425b7816,425c7b49` **0** (cell now held by 46) | d `3e377ff2` | `bd39dff3,be2fc080` |
| 1282 / 46 | 425bde7e,425cb46a | bd904082,3ec22c31 | `425b965d,425e38c2` 1 | d `3f109f5f` | `bdb75d74,3ef6d162` |
| … | 30 accepted applications in total, 1 rejected | | | | |
| 1309 / 45, 1310 / 46 | (54.98046,52.89040), (54.47778,58.26271) | | | contributions ≈0 | **[0,0], 0x0007** |
| 1311–1324 | cooldown 7→0 for both, no position change | | | | |
| 1325+ | body every 8th own visit: contribution 0 (d=5.42 > r=5) → cooldown 7 again | | | | |

Final rest positions: unit 45 `425bec7e,4253831f` (54.98095, 52.87805), occ `[52,54,53,55]`; unit 46 `4259e78f,42691955`
(54.47613, 58.27474), occ `[58,54,59,55]`; separation 5.42 fine (retained vectors applied after the last contribution overshoot r=5).

## Reproducer
Same map/runs as SEP-02.2; sequence extraction:
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
python3 tools/frida/research/sep_research_expected.py --replay $R/SEP-02.2/replay-triad-observe-1.json --repeat $R/SEP-02.2/replay-triad-observe-2.json --units 45 46 --label 'O9 row0 exact pair at corner (55,55)' --out /tmp/o9.json
```
(`expected-SEP-04.1.json` is composed from `$R/SEP-02.2/overlap-sequences.json`, sha256 `8eba164002e487173953809609c2dc744c9d67b2d6482acce20243a91eac6784`, plus the seed block of `$R/SEP-02.3/draw-stream-triad-observe-1.json`.)

## Provenance / captures / controls
Map `RS-SEP-02.2-triad.w3m` `80bfb0e1…b4a9`; captures triad-observe-1 `a14176dc…7c27`, triad-observe-2 `2529a899…` (sequence equal),
triad-control-1 (JASS markers equal, incl. both units' 0.1 s positions). Observer v1/v2, controller, replay script as SEP-02.2.

## Exclusions
Seed production (NUM-04.5); engine scheduling of other clusters' draws in the same pass (covered by the expected stream in SEP-02.3).

## Mismatches preserved
Label error fixed before freezing: an intermediate draft labelled O9 as "(10,10)"; the roster puts it at fine (55,55). No data changed.

## Proposed integration
No Ghidra rows (`mapping-rows-SEP-04.1.txt`); `proposed-docs-SEP-04.1.md`.

## Suggested failing engine regressions
1. Create two row-0 repulsors of one owner at world (1760,1760) on a flat 64×64 fine map with owner words `4273436052/209508436`
   and no other overlapping repulsors: assert the first draw words, the 1280/1281 accept/reject pair, 30 accepted applications,
   deadzone at body 16 of unit 45 (visit 1309) and body 17 of unit 46 (visit 1310), cooldown cadence and the final position words above.
2. Same at world (304,304) (cell centre): no application is ever accepted.
