<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-04.2/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-04.2**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-04.2/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-04.2-sequences.json` -> [`SEP-04.2-expected-sequences.json.gz`](../../../../../tools/ghidra/fixtures/research/SEP-04.2-expected-sequences.json.gz) (uncompressed sha256 `856d574d9fe0301091126f9339b879477bbd0aed46ecc3c49d487c75f9a1ebd1`, 1878819 bytes)
> - `expected-SEP-04.2.json` -> [`SEP-04.2-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/SEP-04.2-expected.json.gz) (uncompressed sha256 `997e6ee7e15b1f9187f2c4313ceaddb5c02cfb0a3a1a1d03f58a1340d0d403bf`, 1893346 bytes)

# SEP-04.2 handoff — mixed owner/radius/rank crowd with blocked endpoints

**Status.** Live-verified (2 identical observed runs + JASS control) on RS-SEP-04.2-crowd: ten units (owners 0/1; footprint radius
0.25/0.5/1.0 fine; ranks 0/1/2; selectors 0/2/4; one repulse-0 unit) packed 0.375 fine apart against a terrain wall (fine x 30..33,
y 20..44), idle for 120 ticks, then individually ordered (tick 122) to a point inside the wall. Every unit's displacement is
explained against the recovered model: (1) candidate set from the policy word (6f16e830 decisions), (2) arithmetic — all 1,117
live bodies / 3,228 contributions / 1,117 tails / 97 moving visits replay exactly with original code, (3) endpoint acceptance —
live 6f16ee80 results, with rejection causes **reconstructed** (inference) from observed occupancy rectangles, terrain cells and
the disabled unit's JASS position, (4) path movement = JASS displacement minus accepted separation applications. Not established:
the actual blocker object identity for each rejection (no retail call reconstructs it; would need the `--blockers` style chain walk
inside 6f16ee80, proposed SEP-04.4); four units had not finished their blocked-goal orders at the scene end (tick 302).

## Functions / fields
As SEP-01.2/02.2. Additional observed fact: endpoint validation is **category-blind** — owner-1 units' endpoints are rejected on
owner-0 and repulse-0 occupancy (unit 9: causes include units 0,2–6,8), while their candidate sets contain only owner-1 units.

## Behaviour (frozen: `expected-SEP-04.2.json`, sha256 `997e6ee7e15b1f9187f2c4313ceaddb5c02cfb0a3a1a1d03f58a1340d0d403bf`)

Idle phase (ticks 2–120): **no unit moves** (JASS displacement 0 for all ten; accepted applications 0); every eligible unit
re-validates and fails ≈99 endpoints (deadlock described in SEP-02.3). Order phase: displacement is path movement (first moving sample
already 32–96 world away from the idle position: path start relocation, also seen for the repulse-0 unit) plus separation applied
only after a unit is idle again.

| Unit | Type / radius | Owner | Word | Eligible pushers | Bodies replayed | Applications acc/rej | Main reconstructed reject cause | Stop tick / goal dist | Post-stop separation (world) | End dist | Retry results |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | hS00 0.25 | 0 | 00000000 | 2,3,4,5,6 | 149/149 | 39/103 | terrain + occ 9,2 ×99 | 242 / 98.7 | (−56.8, 60.3) | 158.8 | 0→1:1 ×6, 1→1:4 ×1 |
| 1 | hS00 0.25 | 1 | 01000000 | 7,9 | 158/158 | 10/142 | occ (7 units) ×99; terrain ×43 | 214 / 78.5 | (−0.4, −50.1) | 55.7 | 0→1:1 ×4, 1→1:4 |
| 2 | hSC1 0.5 | 0 | 00000000 | 0,3,4,5,6 | 174/174 | 4/165 | occ ×99; terrain ×66 | 192 / 65.0 | (24.0, −14.4) | 49.8 | 0→1:1 ×2, 1→1:4 |
| 3 | hSC2 1.0 | 0 | 00000000 | 0,2,4,5,6 | 121/121 | 14/99 | occ incl. repulse-0 unit 8 ×99 | still moving | (8.5, −33.8) | 463.4 | 0→1:1 ×4, 0→6/0→7 |
| 4 | hSR1 0.25 | 0 | 10000000 | 5 only (rank ≥1) | 112/112 | 1/99 | terrain + occ 9,2 | still moving | (−16.0, 0) | 126.4 | 0→1:1 ×8 |
| 5 | hSR2 0.25 | 0 | 20000000 | none | 25/25 | 0/0 | — | 226 / 98.1 | 0 | 98.1 | 0→1:1 ×6, 1→1:4 |
| 6 | hS02 0.25 | 0 | 00020000 | 0,2,3,4,5 | 129/129 | 18/104 | occ 3 ×99 | still moving | (−12.1, 78.5) | 225.7 | 0→1:1 ×11 |
| 7 | hS04 0.25 | 1 | 01040000 | 1,9 | 140/140 | 11/123 | occ 3 ×99; occ 0 ×23 | 260 / 102.5 | (−17.9, 38.2) | 145.4 | 0→1:1 ×6, 1→1:4 |
| 8 | hSD0 0.25 | 0 | none | — | 0 | 0 | — | still moving | 0 | 179.2 | 0→1:1 ×7, 0→6/0→7 ×4 |
| 9 | hSC1 0.5 | 1 | 01000000 | 1,7 | 109/109 | 0/99 | terrain + occ of owner-0 units (+8) | 264 / 129.2 | 0 | 129.2 | 0→7,0→6,0→1:1 ×7, 1→1:4 |

Differences explained: rank-2 unit 5 has no pusher → never displaced by separation; rank-1 unit 4 only by unit 5; owner-1 units
(1,7,9) only by each other; unit 9 (0.5) never escapes (all endpoints overlap 2×2 occupancy); the radius-1.0 unit 3 (3×3) is
blocked by everyone including the repulse-0 unit; units 0/2/6/7 gain 25–80 world of post-stop separation; retry `1→1:4`
(exhaustion → forced arrival) precedes each stop; `0→6/0→7` are random 6/7 retry initializations (draw-consuming, see SEP-04.3).

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
python3 tools/frida/research/sep_research_map.py --data /run/media/lofcz/ssd_external/Games/w3-research --variant crowd --name RS-SEP-04.2-crowd --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool --keep $R/SEP-04.2/maps
tools/frida/research/sep_research_runs.sh observe:crowd:RS-SEP-04.2-crowd:55:SEP-04.2/captures/crowd-observe-N:--pair-probes,--retry-events control:crowd:RS-SEP-04.2-crowd:55:SEP-04.2/captures/crowd-control-1:
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_SEP-02.2_replay.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --capture $R/SEP-04.2/captures/crowd-observe-2 --map-json $R/SEP-04.2/maps/RS-SEP-04.2-crowd.json --report $R/SEP-04.2/replay-crowd-observe-2.json
python3 tools/frida/research/SEP-04.2_crowd_explain.py --capture $R/SEP-04.2/captures/crowd-observe-2 --map-json $R/SEP-04.2/maps/RS-SEP-04.2-crowd.json --replay $R/SEP-04.2/replay-crowd-observe-2.json --report $R/SEP-04.2/explain-crowd-observe-2.json
python3 tools/frida/research/SEP-04.2_phase_split.py --capture $R/SEP-04.2/captures/crowd-observe-2 --map-json $R/SEP-04.2/maps/RS-SEP-04.2-crowd.json --out $R/SEP-04.2/phase-split-crowd-observe-2.json
```

## Provenance
Map `RS-SEP-04.2-crowd.w3m` `8bb2b4b2a2e52691a8b9d6a2224cc6ab751686b592bff4270100a265e5302ed3` (wall cells and roster in
`maps/RS-SEP-04.2-crowd.json`); observer v2 `7eee4c82…b991`; controller `595ea808…`; explain `484a205f…`; phase split
`SEP-04.2_phase_split.py` `648627285cda…`;
replay reports -1 `d4aac82d…`, -2 `bd9f196a…`; explain -1 `568ce1df…`, -2 `2998fbee…` (identical printed summaries); phase split `11e4715f…`.
Seed: owner `4273436052/209508436` at the first visit; the scene consumes draws only in retry initializations.

## Captures
| Capture | Status | capture / preload sha256 |
|---|---|---|
| crowd-observe-1 | complete, 1515 markers | b46d7374… / 589f2054… |
| crowd-observe-2 | complete; all 2,255 visits equal to -1 | 0c221cff… / 11b3b1e9… |
| crowd-control-1 (no attach) | complete; 1515/1515 markers identical | – / 28b592b1… |

## Observer controls
`control-compare-crowd.json`, `repeat-compare-crowd.json` (exact marker equality).

## Exclusions
Blocker identity inside 6f16ee80 (proposed **SEP-04.4**); retry/route mechanics for the blocked goal (MOVE/ROUTE scopes; witnessed
only); scene stops at tick 302 with four orders unfinished (no claim about their completion).

## Mismatches preserved
None in arithmetic. Reconstruction caveat: causes list every observed occupancy rectangle overlapping the endpoint footprint;
they are not the validator's first blocking object.

## Proposed integration
No Ghidra rows (`mapping-rows-SEP-04.2.txt` empty); `proposed-docs-SEP-04.2.md`.

## Suggested failing engine regressions
1. Ten-unit roster/positions from `maps/RS-SEP-04.2-crowd.json` against the x30..33 wall: no unit moves in ticks 2–120; each
   eligible unit's retained vector is non-zero and rejected every body.
2. Candidate sets per unit exactly as the "Eligible pushers" column; unit 5 (rank 2) never receives a contribution.
3. An owner-1 unit's endpoint is rejected by owner-0 occupancy (category-blind validation).
