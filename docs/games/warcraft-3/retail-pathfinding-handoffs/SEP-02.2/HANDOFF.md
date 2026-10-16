<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-02.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-02.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-02.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-02.2.json` -> [`SEP-02.2-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/SEP-02.2-expected.json.gz) (uncompressed sha256 `e237fe7b55654d0f037630394d3732157d632f1aa2a41c085ddb01ac954aaeca`, 1935418 bytes)

# SEP-02.2 handoff — three-object separation composition

**Status.** Live-verified with original-code replay: nine live three-object clusters (RS-SEP-02.2-triad, phase T), 3,000 live
update bodies in the whole capture (4,321 neighbour contributions, 3,000 tails, 2,601 cooldown visits) reproduce **exactly**
(all words incl. path-owner PRNG) when the original pair slice is run per neighbour on the live inputs captured at
`6f1703e0` and compared with the live state at `6f170518`, followed by the original tail. Two observed runs are identical in every
pointer-normalized word and visit counter; the observer-free JASS control is identical marker-for-marker. Composition order,
accumulation order, clamp/deadzone, cooldown and next-visit application/occupancy are recorded per visit. Not established:
the engine of `6f16ee80` itself under live occupancy is observed (result + endpoint), not emulated.

## Functions (ABI, assembly-verified; asm in `../SEP-01.2/static/asm-6f1702f0.txt`, `asm-6f16ffa0.txt`)

| VA | Role | ABI / instruction anchors | Evidence |
|---|---|---|---|
| 6f1702f0 Separate_Update | one visit | ECX=sep, [esp+4]=owner scratch query, RET 4. 1702f9 cooldown (171320); 17030b speed c0>0 → clear + 16ebd0; 17033e selector=byte[+22]&15, row=6fd54398+20·sel; **170354 apply retained (16ffa0)**; 170359 1/radius; 1703a3 query setup 170960 (push rank, cat, row, mover); 1703ab collect 16f570; loop 1703c0..17051f over `query+0x1c` entries in stored order; 170525 tail | asm; replay |
| 6f16ffa0 Separate_ApplyPendingDisplacement | apply retained vector | ECX=sep, plain RET. Skips only when x²+y² == 0 (ucomiss/lahf/`jnp`); endpoint = predicted position (161040/05bdd0) + vec; 16ee80 (ECX=sep,[esp+4]=&endpoint) → 05c820(ECX=mover, &endpoint, 1). Never clears the vector | asm; live `apply` rows |
| 6f16ee80 Separate_ValidateEndpoint | endpoint check | live return address 0x6f17002e; result 0/1 | live |
| 6f16f570 / 6f16e830 | candidate list / filter | see SEP-01.2/01.3 | live |

Probe points used (instruction-level, read-only): `6f1703e0` (EDI=index, EBX=sep, [ebp-0x68..-0x64]=source, [ebp-0x60..-0x5c]=candidate positions, both already time-resolved) and `6f170518` (EBX+0x18 accumulated vector, [ebp-4]=distance or random-direction length).

## Fields
| Field | Meaning | Writes | Reads |
|---|---|---|---|
| sep+0x18/+0x1c | retained vector (floats) | 1704f7/170507/170515 per pair; tail 170573/170581 (zero) or 15fc70 | 16ffa0, tail |
| sep+0x20 low16 | cooldown | 16ebd0 (=7), 171320 (dec) | 171320 |
| mover+0x78/+0x7c | stored fine position | 05c820 → 1603d0 | live |
| mover+0x98 → spatial obj +0x1c..+0x28 | fine occupancy rect (minY,minX,maxY,maxX), exclusive max | 160590 | live `occ` |

## Behaviour (frozen: `expected-SEP-02.2.json`, sha256 `e237fe7b55654d0f037630394d3732157d632f1aa2a41c085ddb01ac954aaeca`)

Visit order: the owner list is newest-first and alternates parity — visit counter 1028 updates unit 1; 1029 updates 2 then 0.
Neighbour order inside one fine cell is **newest-created first** (unit 1 sees [2,0]; unit 0 sees [2,1]; unit 2 sees [1,0]).
A unit updated earlier in the same owner pass is seen at its **new** position by later units (visit 1031: unit 2 uses unit 1's
post-1030 position, unit 0's pre-application position).

T1 (three row-0 units at fine (9.5,9.5), (9.8,9.6), (9.3,9.85)), first visits (hex float words):

| visit/unit | neighbour (order) | vec before → after | tail (vec, word) | next visit apply |
|---|---|---|---|---|
| 1028 / 1 | 2: d `3f0f1be0` | 0,0 → `3e9081fc,be1081fc` | | |
| | 0: d `3ea1e928` | → `3f1d7f85,bcf6cafc` | `3edc7f56,bcacc14b` w=0 (scaled: 0.616·0.7) | 1030: endpoint `4123b0c6,41194338` valid, occ (9,9)→(10,9) cells `[9,10,10,11]` |
| 1029 / 2 | 1 then 0 | → `bee664bf,3ede8d9e` | `bea14688,3e9bc98a` | 1031 valid, occ `[10,8,11,9]` |
| 1029 / 0 | 2 then 1 | → `be293496,becf20ee` | `bdece336,be90fd72` | 1031 valid, occ unchanged |

Cluster outcomes (both runs): T1 84 bodies/268 cooldown visits, 45 applications, 39 deadzone→cooldown7; T2 (row 4) 16 capped
tails; T3 (rows 0/2/4 inside one cell) and T8 (radius 1.0 + two 0.25) **349/352 endpoints rejected, never moves** (stuck in
"scaled/capped" with no cooldown); T4 rank-1 unit never moves; T5 disabled unit is never a candidate; T6 owner-1 unit isolated;
T7 collinear 5 rejects; T9 inert selector-5 unit pushes its neighbours (118 capped tails) without moving.

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
python3 tools/frida/research/sep_research_map.py --data /run/media/lofcz/ssd_external/Games/w3-research --variant triad --name RS-SEP-02.2-triad --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool --keep $R/SEP-02.2/maps
tools/frida/research/sep_research_runs.sh observe:triad:RS-SEP-02.2-triad:55:SEP-02.2/captures/triad-observe-N:--pair-probes,--retry-events control:triad:RS-SEP-02.2-triad:55:SEP-02.2/captures/triad-control-1:
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_SEP-02.2_replay.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --capture $R/SEP-02.2/captures/triad-observe-1 --map-json $R/SEP-02.2/maps/RS-SEP-02.2-triad.json --report $R/SEP-02.2/replay-triad-observe-1.json
python3 tools/frida/research/sep_research_expected.py --replay $R/SEP-02.2/replay-triad-observe-1.json --repeat $R/SEP-02.2/replay-triad-observe-2.json --units 0 1 2 --label 'T1 …' … --out $R/SEP-02.2/expected-SEP-02.2.json   # full label list in this file's sources
```

## Provenance
Map `RS-SEP-02.2-triad.w3m` `80bfb0e15af66af880a5063baa788ef8715ea9708446578f438063b1f8f3b4a9` (`maps/RS-SEP-02.2-triad.json`); observer v1 `7778a95e…`
(run 1) / v2 `7eee4c82…` (run 2); controller `595ea808…`; replay script `0b7e5f2e…`; oracle `ab7d52fb…`; expected tool `1c3291ae…`.
Seed: owner `4273436052/209508436` at first visit (8 startup draws by 6f1e9e25 from `2002874931/738765888`); T clusters consume no draws.

## Captures
| Capture | Status | sha256 (capture / preload) |
|---|---|---|
| triad-observe-1 | complete, 3543 markers | a14176dc… / 04aa5eb3… |
| triad-observe-2 | complete; replay report byte-size identical, all 18 cluster sequences equal | 2529a899… / da7e1435… |
| triad-control-1 (no attach) | complete; 3543/3543 markers identical | – / 64ed8a8f… |
Replay reports: -1 `4aa8c8af…cb40`, -2 (same size). `repeat-compare-triad.json`, `control-compare-triad.json`.

## Exclusions
Exact overlap → SEP-02.3/SEP-04.1 (same capture, phase O); spatial chain lifecycle, stamps, block reuse → SEP-03; endpoint mask by
movement type → BASE-02.1; moving sources (c0>0) → engine Payoff101.

## Mismatches preserved
None in arithmetic (0 mismatches). Ledger statement "accumulates XY values … from nearby records" is complete only with the two
ordering facts above (newest-first in cell; earlier same-pass updates visible).

## Proposed integration
`mapping-rows-SEP-02.2.txt`, `proposed-docs-SEP-02.2.md`.

## Suggested failing engine regressions
1. T1 geometry, three row-0 units created in order 0,1,2 inside one fine cell: assert visit 1028/1029 neighbour orders [2,0], [1,0], [2,1]
   and the exact vector/position words in `expected-SEP-02.2.json` for the first 20 bodies, then cooldown7 cadence.
2. Same-pass visibility: unit updated first in a pass is seen at its applied position by the next unit in that pass.
3. T3/T8: three units in one cell with caps 0.2, or radius 1.0 + 0.25 pair: retained vectors re-validated and rejected every
   visit; positions never change; no cooldown.
