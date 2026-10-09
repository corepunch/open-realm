# Cross-feature route baselines

Payoff203 closes E2E-01.1 by extending the frozen BASE-06.5 contract with three
engine-integrated variants. Existing original numerical fixtures are unchanged.
The new manifest joins their provenance, route outputs and production regressions;
it does not substitute engine output for original expectations.

| Variant | Original source | Fresh constructed routes / points | Motion commits / saved suffix commits |
|---|---|---:|---:|
| Static wall detour | `retail-primary-owner-route-1.27.json` | 1 /9 | 34 /22 |
| Blocked point destination | `retail-blocked-goal-1.27.json` | 6 /43 | 207 /46 |
| Disconnected crossing | `retail-gate-group-wall-live-1.27.json` | 30 /178 | 635 /6025 |

The detour retains its explicitly supplied original adaptive-disabled profile and
fresh clock-zero callback. It is a complete controlled original owner journey,
not evidence for retail public spawn/order admission. The other two variants
use complete repeated public Frida captures. The full-height wall disconnects
the destination after the only Way Gate is disabled; the first active crossing
is its paired reachability control. The blocked point has a separate five-by-five
terrain obstacle at the requested click.

`wc3_e2e.*` drives the existing production JASS/order/owner-frame/save journeys
twice per edition. It adds exact construction assertions to their complete
motion, retry, forced-arrival and final-order checks. The game-private
`G_TestMoveRouteTrace` observes fine and adaptive builders under `BZ_TESTS` only;
its borrowed points are consumed synchronously. It introduces no production
allocation, saved state, branch policy or scheduling change.

The route fixture is generated directly from original returns. All 37 counts,
budgets, complete/partial results and 230 coordinate pairs are checked in encounter
order. Charged pop counts are checked for all 36 searches whose live fixture
records them. The controlled detour fixture does not record its pop count, so
that property is explicitly unasserted. The disconnected capture has 129 route
returns but only 30 actual searches: 99 denied admissions construct no route and
are kept separate from failed searches. This gate checks constructed routes;
existing queue tests own denial behavior.

The blocked-point variant also observes both actual retry calls, comparing
source/goal words, before/after retry count, result 1 then 4 and member count.
The owner stream must remain unchanged across each call. This is not a claim
that the entire original and engine RNG streams match. Final order/force/wait,
route counts/indices and physical-group cleanup retain the stronger production
regressions added by Payoff197. The disconnected variant preserves its complete
native retry table, including saved continuations and final order zero for both
members.

The first engine run failed 40 route-cardinality assertions because the new
acceptance gate had no construction observer. Connecting the actual builders
made all literal route checks pass; no path policy was changed. The retry gate
then failed eight assertions before its existing production retry observer was
connected. These are failing-first coverage gates, not newly discovered movement
bugs. Python negative controls reject damaged pins, missing/reordered variants, wrong
cardinality and failed/empty/duplicate engine summaries. The combined runner also
checks JUnit case identities and assertion totals. No valid retail fixture was rewritten to obtain green tests.

The fresh combined verifier executes BASE-06.5's original producer baseline,
the original controlled detour twice, the complete blocked-goal Frida repeats
and the complete disconnected Frida repeats. It then runs the three actual game
regressions in Classic and TFT. It rejects stale output and records command,
report, binary and game-library hashes. This adds no new live capture claim.
Ghidra notes for `15aa80`, `166e90` and `171060` are saved and mirrored in
`MapPathfinding.java`; the retained readback has no pending program changes.

```sh
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 make -j8 build/lib/libgame.so build/lib/libgame-wc3-test.so build/bin/openwarcraft3-tests
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 /GitHub/wc3-analysis/verify-venv/bin/python tools/ghidra/run_wc3_pathfinding_corpus.py --binary /run/media/lofcz/ssd_external/Games/w3/game.dll --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 --only oracle-e2e-baseline203 --output /tmp/wc3-e2e203-fresh
python3 -m unittest tests.test_wc3_pathing_e2e tests.test_wc3_pathfinding_corpus
```

Final validation: each edition passes three combined tests /147,846 assertions
and 31 directly affected pathfinding tests /3,423,013 assertions. All 41 Python
checks and the fresh strict corpus entry pass. The exact staged corpus contains
440 entries and 1,162 verified file pins. The full repository suite was not
repeated at this chunk.

The new manifest is `tools/ghidra/fixtures/retail-e2e-baseline203-1.27.json`;
`research/e2e203_expected.py` verifies the literal C route/retry header against
its unchanged original sources. Logs and both failing runs are retained in
`/GitHub/wc3-analysis/runtime/payoff203/`.

This completes the requested static/disconnected baseline variants. Dynamic
pursuit, contention/cancellation, crowd/gate combinations, whole-world state,
all-scenario determinism and original save/load remain their existing E2E,
BASE, MAP and gameplay tasks. Saved suffix motion/retry is compared here;
constructed-route stream comparison covers fresh runs only. No performance or
full-retail completion claim follows from this bounded acceptance gate.
