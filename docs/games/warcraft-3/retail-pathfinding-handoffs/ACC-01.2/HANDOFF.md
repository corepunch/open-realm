<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ACC-01.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ACC-01.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ACC-01.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ACC-01.2.json` -> [`ACC-01.2-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/ACC-01.2-expected.json.gz) (uncompressed sha256 `fbc64c90871e4934c98ec1c7b4fec03b9647db058f45b06c96f23262b0304966`, 2741205 bytes)

# ACC-01.2 handoff — producer-built witnesses for every reachable adaptive branch

**Status.** Established at *original-code (Unicorn) producer* level: 3,288 complete original 162cb0
requests over maps built only by original producers (terrain setter 04d870→054000 with a lane-specific
flag, full 15d360, Way Gate source bridge 04e360, record bridges 04e210/04e550) witness **all 463
reachable Jcc outcomes** of the ACC-01.1 inventory in **all four lanes** (one harness-only exception
below), and **all 276 level-transition / clamp targets** (16 relax call sites x parent/child level x
stored size, special edge, size-2 clamp per level/axis). No observed outcome is one ACC-01.1 classified
unreachable (supporting all 115 proofs empirically). Ten outcomes absent from the accepted corpora get full
frozen node tables. 21 outcomes are reached **only** with markers present (18 special-edge, 2 low-edge
boundary predicates, 1 harness first-request). Not claimed: live retail, mover-level outcomes, ushort alias
regime (ACC-05.1).

## Functions

Same set and ABIs as ACC-01.1 (`../ACC-01.1/HANDOFF.md`); producers as ACC-02.2. Passive hooks: block
coverage (6f1625f0..6f165600), relax entries 6f164020/6f165220 (`[ESP]` ret, child, parent), lookup entry
6f1625f0 and RETs 6f162670/6f1626f9/6f162705, coarse entry 6f1644d0 (node, level, x, y).

## Witness matrix (frozen in `expected-ACC-01.2.json`)

| Family (scenarios) | Producer input | Requests per lane | Purpose |
| --- | --- | --- | --- |
| std (84) | ordinary 84-map geometry (seed 12717085) as 2x2 fine blocks | 4 (sizes 1/2 x budgets 100000/8) | baseline branches, budget exits |
| fine-random (16) | single fine cells, seeds 70000+ | 4 | base class2 (mixed base) cells |
| edge (10) | sources/goals on rows/columns 0..1, random blocks near edges, strip x=1 | 24 | low-edge lookups/predicates |
| edge/marker-low, marker-corner | markers (9,1),(1,9) / (9,9), records inactive | 8 | R-marker 1635dd, 164e78; SE corner level3→0 size 2 |
| setup/wall | same x/y, same cell, same node, blocked start/goal, goal outside | 28 | S1–S5 |
| gate (9) | open/blocked/same-x/self/inactive/chain/wide/near/walled exits | 12 (sizes x budgets 100000/40/8 x warp 0/1) | special edge branches |
| found (12) | `tools/ghidra/fixtures/research/ACC-01.2-witness-maps.json` (bounded randomized search, replayed deterministically) | 2–4 | special reopen/improve, ordinary head reopen, 9 rare transitions |

Each `rows[*]` holds lane, source, goal, size, budget, warp, result, work, node count, warp count, route
words and a sha256 of all node words. `jcc_witnesses` gives the first witness per lane and the families;
`event_witnesses` the same for semantic events; `transitions` lists the 276 targets (all `observed`).

### Branches missing from the accepted corpora (full node tables in `details`)

| Outcome | Meaning | Witness (lane 0 shown; all lanes in JSON) | Result / work / nodes / warps |
| --- | --- | --- | --- |
| 164d3a fall | same x, different y | setup/wall#0 (4.25,4.75)→(4.25,27.75) size1 | 1 / 4 / 8 / 0; route (4.25,27.75),(4.75,16.75),(4.75,8.75),(4.25,4.75) |
| 164200 taken | reopen closed-list head | special: found/gate-trial198#1; ordinary: found/reopen-trial35#0 (31.25,30.75)→(26.25,1.75), warp 0 | 1/9/23/1; ordinary 1/223/248/0 |
| 1644b8 fall | record x equal, y differs | gate/dest-same-x#1 (source 6,6 → exit 6,22) | 1 / 6 / 21 / 1 |
| 16528f fall, 165297 fall | special relax improves a **closed** destination (reopen) | found/gate-trial198#1 (source 6,4 → exit 11,9, goal 30.25,24.75) | 1 / 9 / 23 / 1 |
| 165297 taken | special relax improves an **open** destination | found/gate-trial95#1 (source 4,11 → exit 5,12) | 1 / 7 / 17 / 1 |
| 1652d8 taken | fresh special destination not strictly nearer | found/gate-trial24#1 (exit 24,20, goal 0.25,11.75) | 0 / 495 / 399 / 0 |
| 1653b6 taken | special destination blocked | gate/dest-blocked#1 (exit on blocked 20,20) | 1 / 21 / 31 / 0 |
| 1635dd taken | size-2 East boundary at y=0 (marker-mixed level-1 block) | edge/marker-low#4 (1.5,1.5)→goal, size 2 | 1 / 4 / 22 / 0 |
| 164e78 taken | size-2 South boundary at x=0 | first witness found/events-trial7#1 (gate present); designed witness edge/marker-low#4 | 0/135/153/0; 1/4/22/0 |

Route words, node tables (x, y, level, g, h, parent, state, byte20, byte23) are in `details`.

### Level-transition matrix (all observed)

Base sites (N,E,S,W,NE,SE,SW,NW) parent 0 → child 0..3 for both sizes; side walkers and coarse corners
parent 1..3 → child 0..3 (child < parent only through subdivision/descent, child ≥ parent only through
promotion of the initial lookup); special edge parent 0 → child 0..3; size-2 representative clamp at levels
1..3 for x only, y only, both, none. Rare ones came from the replayed found maps (events-trial1..364) and
the designed SE-corner marker case.

## Reproducer

```sh
P=/GitHub/wc3-analysis/verify-venv/bin/python; B=/run/media/lofcz/ssd_external/Games/w3/game.dll; R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
$P tools/ghidra/research/verify_acc01_2_witnesses.py --binary $B --witness-maps tools/ghidra/fixtures/research/ACC-01.2-witness-maps.json \
   --inventory $R/ACC-01.1/expected-ACC-01.1.json --out /tmp/acc012.json --fixture $R/ACC-01.2/expected-ACC-01.2.json
# optional: regenerate the found inputs (bounded searches; not needed to replay)
$P tools/ghidra/research/acc01_2_find_witnesses.py --binary $B --mode gate --out /tmp/found-gate.json
$P tools/ghidra/research/acc01_2_find_witnesses.py --binary $B --mode reopen --out /tmp/found-reopen.json
$P tools/ghidra/research/acc01_2_find_witnesses.py --binary $B --mode events --known $R/ACC-01.2/witnesses-run1.json --out /tmp/found-events.json
```

## Provenance

game.dll `d51e5680...d8236`. `expected-ACC-01.2.json` (= witnesses-run3.json) sha256
`fbc64c90871e4934c98ec1c7b4fec03b9647db058f45b06c96f23262b0304966`. Scripts: `verify_acc01_2_witnesses.py`
`5ee6d92d...9d1841`, `acc01_2_find_witnesses.py` `58f11355...6636f1`, harness `c2915cca...df7929c`;
fixture `ACC-01.2-witness-maps.json` `34eb2e24e251b2ccd6dfda7c0ea62191ee56078c1e6f6241433f43429d1400a2`.
Seeds: 12717085 (std), 70000+i (fine-random), 80000+i (edge), 1234/4321/12 (found searches). Supplied:
empty 64x64 fine storage, padded 41/20/10/5 headers, node/heap/route storage.

## Captures

| Capture | Status |
| --- | --- |
| witnesses-run1.json | superseded (before marker-low/corner, found maps), kept; used as `--known` for the events search |
| witnesses-run2.json | superseded (no SE-corner case, no transition summary), kept |
| witnesses-run3.json = expected | complete |
| witnesses-repeat.json | complete, byte-identical to expected (exit 0) |

## Observer controls

All hooks are passive; 168 overlapping requests equal ACC-04.1's differently-hooked runs word for word.

## Exclusions

`162cc3 taken` (route buffer count 0 on entry) is witnessed only in lane 0 because only the very first
request of the process starts with an empty route buffer: a harness artifact, reachable for any fresh path
buffer (ROUTE-01.2 owns buffer lifetime). Live retail captures: not required (no producer is live-only);
mover/fine refinement of these routes: ACC-03.3/ROUTE. Ushort alias: ACC-05.1.

## Mismatches preserved

None between runs (run1→run3 only added scenarios). The first-witness detail for 164e78 is a found map
with a gate rather than the designed marker case; both are recorded.

## Proposed integration

`mapping-rows-ACC-01.2.txt` (no new rows beyond ACC-01.1's), `proposed-docs-ACC-01.2.md`.

## Suggested failing engine regressions

For each `details` entry, build the same producer map in the engine (fine flags per lane; gate sources as
1x1 base rectangles; records active/destination as listed) and assert result, work, node count, warp count,
every route word and the ten node fields. Add the four-lane variants from `rows` (identical node digests
across lanes for lane-specific flags). Priority: head reopen (ordinary and special), closed-destination
special reopen, marker-only low-edge boundaries, setup same-x.

## Full sha256 (computed at handoff time)

```
c2915cca134d3bd04385a5412e69dee7d462d2ca8eb985a1ae9acba15df7929c  tools/ghidra/research/acc_research_harness.py
12524db067a6b2a4a86996634f9de1071ba80b4018dc239cd52cd2adc7d23038  tools/ghidra/research/acc01_coverage_wrapper.py
57b607770185779d7d5e04914556088e4a72de58ae6204edf158f15f63217b36  tools/ghidra/research/verify_acc01_1_branches.py
5ee6d92d51bc1cc2819de720517a1b58e87ac6aa4e463095889eda8ab03d1841  tools/ghidra/research/verify_acc01_2_witnesses.py
58f113555a38c604e1b35655a88fe9102171e92f1ed95c59bb3a339d0f6636f1  tools/ghidra/research/acc01_2_find_witnesses.py
1c35ca43254e0b7fd23cdf5e6fdadd32b0c111b3b2bc28517f617f9917309d69  tools/ghidra/research/verify_acc02_2_markers.py
9d6d63c68419cf37dbf5111516ff9268dfe7c35ab09cc3f07d816fbd874c0c55  tools/ghidra/research/acc02_2_dynamic_writers.py
233a51d8472e579d9a1d0f99d43220fb494fbb90ee23b95f78c0c50644d94465  tools/ghidra/research/acc02_2_cell_writer_scan.py
38b65304f9a47ff7a23241043dfbaa740268b99293ae584372c7660a09f8e8b5  tools/ghidra/research/verify_acc04_1_costs.py
34eb2e24e251b2ccd6dfda7c0ea62191ee56078c1e6f6241433f43429d1400a2  tools/ghidra/fixtures/research/ACC-01.2-witness-maps.json
fbc64c90871e4934c98ec1c7b4fec03b9647db058f45b06c96f23262b0304966  expected-ACC-01.2.json
fbc64c90871e4934c98ec1c7b4fec03b9647db058f45b06c96f23262b0304966  witnesses-repeat.json
d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236  game.dll
```
