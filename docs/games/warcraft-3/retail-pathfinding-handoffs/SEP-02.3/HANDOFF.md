<!-- Research handoff copied from /GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-02.3/HANDOFF.md; report-root paths below remain absolute. -->
> Research handoff for **SEP-02.3**. Not a TODO closure. Report root: `/GitHub/wc3-analysis/reports/pathfinding-1.27/research/SEP-02.3/` (captures, oracle reports, maps).
> Frozen expected results in the repository:
> - `expected-SEP-02.3.json` -> [`SEP-02.3-expected.json.gz`](../../../../../tools/ghidra/fixtures/research/SEP-02.3-expected.json.gz) (uncompressed sha256 `c6a63ba77761771757bd6a94057c2fd018c46c554f8d4b80e98863af491c8e89`, 1672539 bytes)

# SEP-02.3 handoff — exact overlap in the multi-object query (draws, endpoint, occupancy)

**Status.** RNG prerequisite for replay is satisfied: separation consumes only the shared path-owner state (`PathRandom_UnitDirection`
6f1d19e0, EDX = owner `[6fd53a48]`, one `PathRandom_Next` 6f1b7130 per neighbour with distance < 0.001). Live-verified with
original-code replay: in RS-SEP-02.2-triad phase O (nine exact-overlap clusters, 2/3 units), **every** live path-owner draw after
startup (1,476) is exactly one replayed random-branch pair, in order, with identical before/after words; no other consumer drew
in this scene. Endpoint results and occupancy rectangles are recorded for every subsequent visit; two observed runs identical
(including final owner `3711717831/2080948460`), JASS control identical. **Not needed for replay:** NUM-04.5 (production of the
initial owner words) and NUM-04.6 (per-unit TLS streams, never touched by separation). **Needed for an engine to reach these words
from map start:** NUM-04.5 — this map family starts from `2002874931/738765888`, then `PlayerSetup_ResolveRaces` (call site
6f1e9e25) draws 8 times, giving `4273436052/209508436` at the first separation visit (identical to the Payoff101 asserted state).

## Functions
| VA | Role | ABI | Evidence |
|---|---|---|---|
| 6f17043c..1704a4 (in Separate_Update) | near-zero branch | `comiss [6fcd53a0]=0.001 > d` → 1d19e0(ECX=&out,EDX=owner) → length recomputed (06f9c0×2, 06fbb0, 071480) replaces d | asm `../SEP-01.2/static/asm-6f1702f0.txt` |
| 6f1d19e0 PathRandom_UnitDirection | direction | ECX=out ptr, EDX=PRNG state, plain RET, EAX=out; `(draw&0x7fffff)|0x3f800000` −1 × 2π (6fcd5464) → 071340 (cos,sin) | asm (`ctx 6f1d19e0`) |
| 6f1b7130 PathRandom_Next | owner PRNG | ECX=state (two words), EAX=result | asm; live hook filtered on ECX==owner |

## Behaviour (frozen: `expected-SEP-02.3.json`, sha256 `c6a63ba77761771757bd6a94057c2fd018c46c554f8d4b80e98863af491c8e89`)

| Cluster (phase O) | Fine position | Bodies | Draws | Endpoint accepted / rejected | Occupancy changes | Outcome |
|---|---|---|---|---|---|---|
| O1 row0 pair | (9.5,9.5) cell centre | 261 | 261 | 0 / 258 | 0 | **never separates**; one draw per body forever |
| O2 row0 pair + third at (32.3,9.6) | (32.0,9.5) x on cell boundary | 97 | 2 | 47 / 5 | 11 | separates after one draw each |
| O3 row0 triple | (54.5,9.5) centre | 402 | 804 | 0 / 399 | 0 | 2 draws per body, never moves |
| O4 sel5 + row0 | (9.5,32.0) y on boundary | 177 | 3 | 29 / 0 | 6 | row0 leaves; inert unit drew while overlapping |
| O5 row4 pair | (32,32) corner | 114 | 2 | 90 / 0 | 12 | separates |
| O6 row0 + repulse0 | (54.5,32.0) | 17 | 0 | – | 0 | disabled unit is not a candidate: no draw |
| O7 row0 + rank1 | (9.5,54.5) centre | 151 | 134 | 0 / 133 | 0 | only rank-0 unit draws; stuck |
| O8 sel5 pair | (32.0,54.5) | 268 | 268 | – (zero vector) | 0 | draws every visit, no cooldown |
| O9 row0 pair | (55,55) corner | 61 | 2 | 30 / 1 | 7 | separates (see SEP-04.1) |

Rule recovered (inference, consistent with all nine clusters): with footprint class 1×1 and per-visit caps 0.5/0.2, an exactly
overlapping pair resolves only if a capped retained vector moves the endpoint into a different, unoccupied fine cell. At a cell
centre (fractional part 0.5 in both axes) a ≤0.5 step never changes `floor`, so the pair consumes one shared draw per eligible body
indefinitely (O1/O3/O7); when either coordinate lies on a cell boundary, any step with a negative component in that axis changes
`floor` and the pair separates after one draw per unit (O2/O4/O5/O9).

First draws (owner list newest-first across clusters, visit 1278): O9 unit 46 `4273436052,209508436 → 3022241195,141606968`;
O8 unit 44 next; full ordered stream in `draw-stream-triad-observe-1.json`.

## Reproducer
Same map/runs as SEP-02.2, then:
```sh
R=/GitHub/wc3-analysis/reports/pathfinding-1.27/research
python3 tools/frida/research/SEP-02.3_draw_stream.py --capture $R/SEP-02.2/captures/triad-observe-1 --map-json $R/SEP-02.2/maps/RS-SEP-02.2-triad.json --replay $R/SEP-02.2/replay-triad-observe-1.json --out $R/SEP-02.3/draw-stream-triad-observe-1.json
```

## Provenance / captures / controls
As SEP-02.2 (triad-observe-1 `a14176dc…`, triad-observe-2 `2529a899…`, triad-control-1); draw-stream reports -1/-2 equal except
input hashes. Script `SEP-02.3_draw_stream.py` `d4da096e…a66`.

## Exclusions
Seed production (NUM-04.5), TLS/unit streams (NUM-04.6), retry draws interleaving (covered by SEP-04.3 ground and engine Payoff101),
trigonometric table derivation (NUM scope).

## Mismatches preserved
None. Note the doc statement "exact overlap contributes approximately 0.256" holds per contribution; it does not imply separation
— see O1/O3/O7.

## Proposed integration
No Ghidra rows (functions already named). `mapping-rows-SEP-02.3.txt` (empty), `proposed-docs-SEP-02.3.md`.

## Suggested failing engine regressions
1. Two row-0 units created at the same fine cell centre with owner state `4273436052/209508436` at the first visit: assert the first
   draw words above, 0 accepted endpoints and one draw per body for ≥ 250 bodies.
2. Same at a cell corner (55,55): accepted endpoints from the second body, separation to ≈5.42 fine and cooldown7 (SEP-04.1 sequence).
3. Inert selector-5 pair exactly overlapping: one draw per visit, no cooldown, no movement.
