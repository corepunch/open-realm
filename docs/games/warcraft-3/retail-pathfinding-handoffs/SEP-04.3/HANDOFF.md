<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-04.3/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-04.3**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-04.3/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-04.3.json` -> [`SEP-04.3-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/SEP-04.3-expected.json.gz) (uncompressed sha256 `a0e0751360e39fd89d67c19e2fc1384cef45d813171cd25b322fe0bdd6d48807`, 832402 bytes)

# SEP-04.3 handoff — disabled-repulsion ground controls beside enabled cases

**Status.** Live-verified (2 observed runs with identical timelines, legs, separation sequences and stops; 1 observer-free JASS
control identical marker-for-marker) on RS-SEP-04.3-ground: two otherwise identical 4-unit foot groups (radius 0.5 fine,
owner 0) — `hSC1` repulse=1 and `hSD1` repulse=0 — each ordered at tick 22 to an unreachable point inside its own closed ring and
at tick 202 through a 2-cell wall gap to a reachable point. Retry/Stop outcomes are attributed per unit (run 2; run 1's observer
lacked the `current` field), separation activity is classified per visit and replayed exactly (482 bodies, 1,182 pairs, 49 moving
visits, 805 cooldown visits — both runs). Path blocking is separated from repulsion by construction of the evidence: separation
bodies occur only while the unit's requested speed `c0==0`; the disabled group has no separation object at all.

## Functions (assembly + live)
| VA | Role | ABI / observation |
|---|---|---|
| 6f1702f0 c0 gate (17030b..17033b) | moving source | `c0 > 0` (comiss vs 0) → vector := 0, `Separate_SetIdleCooldown` (7), RET 4 — 49 live "moving" visits, all exact |
| 6f16e830 | candidate filter | moving candidates rejected (`c0 != 0`) — observed in crowd (SEP-04.2) |
| 6f1689d0 / 6f167290 | retry init / retry result | ECX=path; mover in progress read from `[6fd53a8c]` (live: equals the unit's mover pointer) |
| 6f1d62b0 PathRandom_DrawMantissaRange | retry-count draw | 7 live owner draws, all at retry-init with count 7/8 (call site 6f1d62bb); count-2 inits draw nothing |
| 6f171340 | mover stop | live callers per unit: `MoverBridge_StopWithRecovery` 05ca8b ×10–11, `MoveBridge_StartPoint` 05bb54 ×2, `PathGroup_FinishMember` 16d53f ×2 |

## Behaviour (frozen: `expected-SEP-04.3.json`, sha256 `a0e0751360e39fd89d67c19e2fc1384cef45d813171cd25b322fe0bdd6d48807`)

| Unit | Repulse | Leg 1 (ring, unreachable): stop tick / dist (world) / drift after stop | Leg 2 (gap): stop tick / dist / drift | Separation visits body/moving/cooldown, applications | Retry results (run 2) | Owner draws (tick) |
|---|---|---|---|---|---|---|
| 0 | on | 76 / 177.1 / **41.0** | 338 / 8.2 / 7.6 | 88/15/231, 67 | 0→1:1 ×8, 1→1:4, 0→6/0→7, 6→5 | 204, 256, 260 |
| 1 | on | 66 / 175.5 / **70.7** | 278 / 79.8 / 129.9 | 134/12/188, 114 | 0→1:1 ×3, 1→1:4 ×2 | – |
| 2 | on | 62 / 159.9 / **39.7** | 272 / 11.7 / 168.9 | 140/11/183, 118 | 0→1:1 ×3, 1→1:4 | – |
| 3 | on | 32 / 172.2 / **40.4** | 316 / 32.3 / 101.5 | 120/11/203, 97 | 0→1:1 ×5, 1→1:4 ×2 | – |
| 4 | off | 114 / 183.8 / **0.0** | 322 / 64.5 / 0.0 | none | 0→1:1 ×7, 1→1:4 ×2, 0→6, 0→7 | 204, 248 |
| 5 | off | 114 / 174.9 / **0.0** | 338 / 65.2 / 0.0 | none | 0→1:1 ×12, 1→1:4 ×3, 0→6, 0→7 | 226, 228 |
| 6 | off | 96 / 159.6 / **0.0** | 286 / 11.4 / 0.0 | none | 0→1:1 ×4, 1→1:4 | – |
| 7 | off | 38 / 173.9 / **0.0** | 292 / 78.5 / 0.0 | none | 0→1:1 ×2, 1→1:4 ×2 | – |

Classification rules supported by the data: (1) leg 1: every unit of both groups gets `167290` result 4 (count stays 1) 2–4 ticks
before its JASS stop, then `PathGroup_FinishMember` + `MoverBridge_StopWithRecovery` at that tick — path blocking/retry exhaustion,
not repulsion; leg 2: units 0, 2 (enabled) and 6 (disabled) finish without a result-4 retry at 8–12 world (ordinary
arrival), units 1, 3, 4, 5, 7 again through exhaustion;
(2) after Stop, only enabled units drift (40–170 world) and every drift step is an accepted separation application; disabled units
stay exactly at their stop position until the next order; (3) while moving, enabled units' visits clear the vector (no body,
0 bodies with c0≠0); (4) the enabled group was also pushed apart before its first order (ticks 4–24, 1.25-fine spacing < r=5),
whereas the disabled group's only pre-move change is the path start relocation at the order tick; (5) retry-count draws (7/8)
come from the same shared path-owner stream for both groups (interleaved: tick 204 unit 4 then unit 0).

## Reproducer
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
python3 tools/frida/research/sep_research_map.py --data /run/media/lofcz/ssd_external/Games/w3-research --variant ground --name RS-SEP-04.3-ground --tool /run/media/lofcz/ssd_external/GitHub/open-realm/build/bin/mpqtool --keep $R/SEP-04.3/maps
tools/frida/research/sep_research_runs.sh observe:ground:RS-SEP-04.3-ground:60:SEP-04.3/captures/ground-observe-N:--pair-probes,--retry-events control:ground:RS-SEP-04.3-ground:60:SEP-04.3/captures/ground-control-1:
/GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/research/verify_SEP-02.2_replay.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --capture $R/SEP-04.3/captures/ground-observe-2 --map-json $R/SEP-04.3/maps/RS-SEP-04.3-ground.json --report $R/SEP-04.3/replay-ground-observe-2.json
python3 tools/frida/research/SEP-04.3_ground_controls.py --capture $R/SEP-04.3/captures/ground-observe-2 --map-json $R/SEP-04.3/maps/RS-SEP-04.3-ground.json --out $R/SEP-04.3/ground-controls-observe-2.json
```

## Provenance
Map `RS-SEP-04.3-ground.w3m` `e70072c9e288232e2433b7566f4132701b151a11ab2363bf919dffeae6544946` (two rings x18..26 at y8..16 and
y44..52, wall x36/37 with gaps y14–15 and y50–51; `maps/RS-SEP-04.3-ground.json`); observer v1 `7778a95e…` (run 1), v2 `7eee4c82…`
(run 2); analyzer `SEP-04.3_ground_controls.py` `085b36b5…`; replay script as SEP-02.2. Seed `4273436052/209508436` at the first
visit; first retry draw `4273436052,209508436 → 3022241195,141606968`.

## Captures
| Capture | Status | capture / preload sha256 |
|---|---|---|
| ground-observe-1 (obs v1) | complete, 1616 markers; retry rows not attributable (no `current`) | f7a621e7… / 8af1a726… |
| ground-observe-2 (obs v2) | complete; legs, separation sequences, stops equal to -1 | 7d46a2b1… / 71516358… |
| ground-control-1 (no attach) | complete; 1616/1616 markers identical | – / d46438bb… |

## Observer controls
`control-compare-ground.json`, `repeat-compare-ground.json`; separation sequences repeat-equal (`ground-sequences.json`).

## Exclusions
Route/retry internals (ROUTE/MOVE TODO scopes; witnessed only); the meaning of retry init count 2 vs 7/8 (Payoff101 / NUM-04.1–02);
group (shared) orders — this scene uses individual Move orders.

## Mismatches preserved
Run 1 used observer v1 (no retry attribution): kept, labelled. None in arithmetic.

## Proposed integration
No Ghidra rows (`mapping-rows-SEP-04.3.txt`); `proposed-docs-SEP-04.3.md`.

## Suggested failing engine regressions
1. Repulse-0 ground group: zero separation state, zero displacement between Stop and the next order, retry-exhaustion Stop.
2. Repulse-1 ground group in the same scene: identical Stop mechanism; post-Stop drift equals the sum of accepted separation
   applications; no separation body while moving (vector cleared, cooldown 7).
3. Retry-count draws (7/8) of both groups share the path-owner stream with overlap draws; tick-204 order unit 4 then unit 0.
