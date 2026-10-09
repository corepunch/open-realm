<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.3/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-01.3**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-01.3/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-01.3.json` -> [`SEP-01.3-expected.json`](../../../../../tools/ghidra/fixtures/research/SEP-01.3-expected.json) (uncompressed sha256 `65b95d67b2cfa3a890f5dd4b4bc9f758a80192fb1a4a8018eb00750cde3217e0`, 56302 bytes)

# SEP-01.3 handoff — policy matrix across movement types and owners

**Status.** Live-verified (2 identical observed runs + 1 observer-free JASS control) for 36 two-unit cases on RS-SEP-01.2-policy:
movement types foot/fly/hover/amph/horse, owners 0/1/2/15, all authored field classes and five runtime producers. Candidate
eligibility (every `Separate_FilterCandidate` 6f16e830 decision, with the failing predicate) is recorded separately from
displacement arithmetic; every one of the 2,626 live update bodies (2,272 neighbour pairs, 2,626 tails, plus 6,851 cooldown
visits) reproduces exactly with original-code slices on the captured words (both runs). Not covered: float movement (map has
no water), movement-type-specific endpoint masks (BASE-02.1), >2-unit compositions (SEP-02.2), moving sources (engine Payoff101).

## Functions / fields
No new functions beyond SEP-01.2. Additional live facts (observer rows `sep-update.moverBefore`):
| Field | Meaning | Evidence |
|---|---|---|
| mover+0x94 → spatial object +0x2c | proximity map queried by 6f170960 | **one map `0x80700a0` shared by all five movement types** in the capture → candidates cross movement types |
| query +0x50 / +0x52 | source category / source rank (minimum) | live rows equal the source word bits 20–27 / 28–31 |
| mover+0xa8 | path object pointer (mask is path+0x9c, BASE-02.1) | observer column named `mask` holds this pointer — not a mask |

## Behaviour (frozen: `expected-SEP-01.3.json`, sha256 `65b95d67b2cfa3a890f5dd4b4bc9f758a80192fb1a4a8018eb00750cde3217e0`)

Eligibility of B as a candidate of source A = A and B both have a separation object ∧ cat(A)==cat(B) ∧ rank(B) ≥ rank(A) ∧
B idle (c0==0) ∧ B not flagged (+14 bit31) ∧ footprint>0. Movement type does **not** enter the predicate.

| Pair (A / B) | A→B | B→A | Separates | Notes |
|---|---|---|---|---|
| foot/foot, owner 0 | ✔ | ✔ | both | baseline |
| fly/fly, hover/hover, amph/amph, horse/horse | ✔ | ✔ | both | |
| fly/foot, hover/foot, amph/foot, horse/foot | ✔ | ✔ | both | shared proximity map |
| owner 0 / owner 1 (foot), fly 0 / fly 2, owner 15 / owner 0 | category mismatch | mismatch | neither | owner nibble only, alliance irrelevant |
| owner 15 / owner 15 | ✔ | ✔ | both | |
| enabled / repulse=0 | `+ac null` | (no visits) | neither | disabled unit has no separation object |
| group 0 / group 1 | mismatch | mismatch | neither | |
| rank 1 / rank 0; rank 2 / rank 1 | rank below minimum | ✔ | lower rank only | asymmetric |
| selector 0..4 at 6 fine | ✔ | ✔ | rows 2,3,4 only | rows 0 (d≥r) and 1 (deadzone: 0.4(1/7)²·0.7<0.01) do not move |
| radius 1.0 / radius 0.25 at 0.25 fine | ✔ | ✔ | **neither** | 200/201 endpoints rejected each: permanent retained-vector deadlock, no cooldown |

Displacement arithmetic: `replay-policy-observe-{2,3}.json` → `body 2626 / body_ok 2626, pairs_ok 2272, tails_ok 2626, cooldown_ok 6851`.

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
tools/frida/research/sep_research_runs.sh observe:policy:RS-SEP-01.2-policy:75:SEP-01.2/captures/policy-observe-N:--pair-probes
python3 tools/frida/research/SEP-01.3_policy_matrix.py --capture $R/SEP-01.2/captures/policy-observe-2 --map-json $R/SEP-01.2/maps/RS-SEP-01.2-policy.json --report $R/SEP-01.3/policy-matrix-observe-2.json
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_SEP-02.2_replay.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --capture $R/SEP-01.2/captures/policy-observe-2 --map-json $R/SEP-01.2/maps/RS-SEP-01.2-policy.json --report $R/SEP-01.3/replay-policy-observe-2.json
python3 tools/frida/research/SEP-01.2_expected.py --oracle $R/SEP-01.2/oracle/policy-oracle.json --matrix $R/SEP-01.3/policy-matrix-observe-2.json --repeat-matrix $R/SEP-01.3/policy-matrix-observe-3.json --map-json $R/SEP-01.2/maps/RS-SEP-01.2-policy.json --out $R/SEP-01.3/expected-SEP-01.3.json
```

## Provenance / captures / controls
Same captures as SEP-01.2: policy-observe-2 (`756e0a11…`), policy-observe-3 (`b594870c…`, matrix and all 36 normalized sequences equal),
policy-control-1 (JASS 6035/6035 identical); policy-observe-1 failed (controller bug, not evidence). Matrix sha256: -2 `5cbebf92…091a`,
-3 `5b76598b…d358`; replay -2 `775eecf1…ef82`, -3 `1f015579…1ffd`. Analyzer `SEP-01.3_policy_matrix.py` `ab64feeb…8708`,
replay `verify_SEP-02.2_replay.py` `0b7e5f2e…4bd2`, shared analysis `sep_research_analyze.py` `ac2ea4c6…c5a9`.

## Exclusions
Float movement (no water in the flat map) and endpoint masks per movement type → BASE-02.1; categories of non-unit objects → BASE-02.2;
moving-source/c0 clearing → already engine Payoff101; multi-neighbour → SEP-02.2.

## Mismatches preserved
None against the ledger. Note: several pairs show exactly one rejected endpoint (`rej=1`) during separation (hover/foot, amph/foot,
baseline) — consistent with the endpoint validator crossing into the partner's cell; kept in the matrix.

## Proposed integration
`mapping-rows-SEP-01.3.txt` (no new rows), `proposed-docs-SEP-01.3.md`.

## Suggested failing engine regressions
1. A fly and a foot unit of the same owner/group/rank placed 0.25 fine apart separate from each other (shared proximity index).
2. Owner 0 vs owner 1 (allied) never separate; owner 15 pair does.
3. Rank 1 + rank 0: only the rank-0 unit moves; words `10000000`/`00000000`.
4. Selector 0/1 pair at 6 fine: no movement; selectors 2/3/4: movement, JASS displacements ≈19.5/33.7/47.5 world per unit.
5. Radius 1.0 + radius 0.25 at 0.25 fine: neither moves; retained vectors stay non-zero and are re-validated every visit (no cooldown).
