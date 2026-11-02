<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/ACC-04.1/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **ACC-04.1**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/ACC-04.1/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-ACC-04.1.json` -> [`ACC-04.1-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/ACC-04.1-expected.json.gz) (uncompressed sha256 `01c0f2acdc12cf68554bd9c83aa66dbed7fcb409b35bb6e5ff72d55938bb411a`, 572790 bytes)

# ACC-04.1 handoff — ordinary adaptive costs under ties and budget exhaustion

**Status.** Established at *instruction-verified + original-code (Unicorn)* level over producer-built maps
(original terrain setters + 15d360; complete 162cb0 requests, warp off, lane 0, sizes 1/2):
(1) the integer distance 1d58e0 equals floor(sqrt) on all 135,981 reachable inputs; (2) every one of the
188 goal-reaching requests returns **exactly the Dijkstra optimum of the graph its own relaxations
explored** (same representatives) — no inadmissibility/early-termination loss was observed; (3) route
differences against an 8-neighbour base-grid optimum are explained by the adaptive graph itself
(order-dependent representatives, any-angle coarse edges, side/corner gating), not by A* mechanics; (4)
ties are pervasive and resolved by "first strictly better relaxation wins" plus heap order; (5) the partial
endpoint is the discovery-time strict-minimum squared distance representative. Not claimed: optimality of
the final world trajectory, admissibility proofs beyond the tested domain, special edges (ACC-04.2 done).

## Functions / ABIs (assembly)

| VA | Role | ABI | Evidence |
| --- | --- | --- | --- |
| 6f1d58e0 | PathAcc_IntegerSquareRoot | ECX n, EAX result, plain RET; Newton with seeds (hex) n<=ff: ((n*aaaaaaab)>>35)+1, n<=ffff: ((n*51eb851f)>>38)+15, else (((n-hi)>>1)+hi)>>e + 1bc with hi=(n*39acc69d)>>32; loop until trunc((s-n/s)/2)==0 | asm, part1 (model + floor) |
| 6f164020 | PathAcc_RelaxNode | thiscall ECX adaptive, stack child,parent RET 8; cost=isqrt((24\|dx\|)²+(24\|dy\|)²) of representatives (6f164044..06c), h same to goal cell bc/c0 (6f164079..0a8) | asm 6f164020..139 |
| 6f164107 | nearest test | fresh child only: `d²=(x-gx)²+(y-gy)²` unscaled, `JNC` keeps old on equal (strict <) | asm |
| 6f163f50 | PathAcc_Search | fastcall ECX; `w=[9c]++; if w>=[98] → -1` before pop; heap empty check first | asm 6f163f70..85 |
| 6f162780 | PathAcc_EnqueueNode | key g+h, generation++ | asm |
| 6f1483f0/6f148240 | shared heap insert/pop | tie policy as in `retail-pathfinding-search.md` (no secondary key) | existing oracle |
| 6f162c40 | PathAcc_NodeCentre | representative integer XY + 0.5 (not square centre) | asm/decompile |
| 6f163f93 | (hook point) | after 148240: `[EBP-c]` key, `[EBP-8]` index, `[EBP-4]` generation | asm |

## Behaviour / frozen results (`expected-ACC-04.1.json`)

| Measure | Value |
| --- | --- |
| isqrt samples / differs from floor(sqrt) | 135,981 / **0**; cardinal edge 24, diagonal 33 |
| requests (84 maps + transposes, 2 sizes) / goal reached | 336 / 188 |
| retail g(goal) == Dijkstra over own relaxations | **188 / 188** |
| vs base-grid optimum (8-neighbour, corner-safe, 24/33 steps, 2x2 for size 2) | equal 9, **above 135, below 44**, max ratio 1.0672 (random_1 size1: 858 vs 804) |
| requests with adjacent equal-key pops / with equal-cost relaxation rejections | 324 / 274 of 336 |
| map vs transposed map (168 pairs) | same result in all; same cost 164; mirrored route 80 (14 of 94 reached pairs not mirrored); 4 cost differences: random_13 880/891, random_20 845/849, random_45 894/895, random_49 905/920 |
| budget sweep (157 cases, 4 maps x 2 sizes) | partial nearest == strict-min d² over discovered nodes (first discovery on ties) in all; differs from min-f node 134/148, from min-h node 4/148 |

Explanations (each observed difference class):

1. **Above the grid optimum (135)**: a coarse node's representative is the coordinate of the lookup that
   created it (often a square corner or a size-2 clamped corner), so routes zig through representatives
   such as `[8,8,0]→[8,9,0]→[8,10,1]` (random_1). The optimum over the same explored graph equals retail g,
   so the excess is graph geometry, not search error.
2. **Below the grid optimum (44)**: edges between representatives are straight Euclidean segments across
   clear coarse squares (any-angle), cheaper than 8-direction steps (gap_2 size2: 789 vs 804).
3. **Transposition differences (4 cost, 14 shape)**: side order N,E,S,W, first-half-before-second walker
   recursion and heap ties are not symmetric under x↔y; the first expansion creates different
   representatives, after which the two graphs differ (random_49: `[6,4,1]` vs `[4,6,1]` then diverging
   chains; 2–9 route nodes of the cheaper orientation do not exist in the other's route).
4. **Ties**: a later relaxation with equal g is rejected (`cand >= g`), so among equal-cost parents the
   first discovered wins; equal keys pop in heap order (no h/index tie-break).
5. **Budget exhaustion**: work = budget+1 when the budget stops the loop; heap exhaustion reports the pop
   count. Budget 0 → work 1, nodes 2 (source+goal created at setup), exact source point, result 0. The
   partial target is the nearest *discovered* representative + 0.5; the goal node created at setup is not
   discovered until relaxed. min-h differs from nearest only when the integer distance collapses distinct
   d² (horizontal_15: d² 170 vs 169 both h 312, nearest picks 169).

## Reproducer

```sh
P=/GitHub/wc3-analysis/verify-venv/bin/python; B=/run/media/lofcz/ssd_external/Games/w3/game.dll; R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
$P tools/ghidra/research/verify_acc04_1_costs.py --binary $B --out /tmp/acc041.json --fixture $R/ACC-04.1/expected-ACC-04.1.json
```

## Provenance

game.dll `d51e5680...d8236`; `expected-ACC-04.1.json` sha256
`01c0f2acdc12cf68554bd9c83aa66dbed7fcb409b35bb6e5ff72d55938bb411a`; script `verify_acc04_1_costs.py`
`38b65304...e08b5`, imports `acc_research_harness.py` `c2915cca...` and `verify_acc01_2_witnesses.py`
(map geometry = the 84-map ordinary corpus, seed 12717085). Supplied: empty 64x64 fine storage, padded
41/20/10/5 headers.

## Captures

| Capture | Status |
| --- | --- |
| costs-run1.json | superseded, kept: min-f/min-h statistics wrongly included the undiscovered setup goal node |
| costs-run2.json = expected | complete |
| costs-repeat.json | complete, equal (exit 0) |

## Observer controls

Pop/relax hooks are passive reads. All 168 non-transposed requests have result/work/node count/route words
identical to the same requests in ACC-01.2 (`expected-ACC-01.2.json`, different hook set: coverage and
events). Repeat equals frozen.

## Exclusions

Special edges (ACC-04.2, done). Fine-search costs (separate). Mover-level/world trajectory outcomes
(ACC-03.3, ROUTE). Requests beyond 65,535 nodes (ACC-05.1).

## Mismatches preserved

- costs-run1 min-f/min-h counts (superseded) are kept for transparency.
- The base-grid reference is not a lower bound for retail costs (any-angle edges) — do not use it as an
  optimality oracle in engine tests.

## Proposed integration

`mapping-rows-ACC-04.1.txt` (comment 1d58e0 applied via gw.py), `proposed-docs-ACC-04.1.md`.

## Suggested failing engine regressions

1. Integer distance: assert engine isqrt == floor(sqrt) for (24a)²+(24b)², a,b ≤ 520 (or the original
   Newton transcription).
2. For the four asymmetric transposition pairs assert both orientation costs (880/891, 845/849, 894/895,
   905/920) and route node lists (`part3_ties.asymmetric_cost_pairs`).
3. Budget sweep rows (`part4_budget.rows`): result, work, nearest index, first route point for each budget.
4. Equal-cost relaxation: the first relaxation with the minimal g keeps its parent (no replacement on equal).

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
01c0f2acdc12cf68554bd9c83aa66dbed7fcb409b35bb6e5ff72d55938bb411a  expected-ACC-04.1.json
01c0f2acdc12cf68554bd9c83aa66dbed7fcb409b35bb6e5ff72d55938bb411a  costs-repeat.json
d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236  game.dll
```
