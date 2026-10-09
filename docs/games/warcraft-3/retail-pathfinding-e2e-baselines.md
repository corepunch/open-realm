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
pursuit, contention/cancellation, whole-world state,
all-scenario determinism and original save/load remain their existing E2E,
BASE, MAP and gameplay tasks. Saved suffix motion/retry is compared here;
constructed-route stream comparison covers fresh runs only. No performance or
full-retail completion claim follows from this bounded acceptance gate.

## Formation, crowd and gate variants (Payoff205)

E2E-01.4 extends the same baseline through
`retail-e2e-variants205-1.27.json`. It pins the parent contract, 13 unchanged
original fixtures/C headers and six complete child evidence contracts. This
combines existing engine integrations; it adds no gameplay policy or new live
capture claim. No existing numerical expectation is regenerated.

| Category | Retail contracts | Actual game acceptance |
|---|---|---|
| Formation | FORM-05.1 selected/independent controls and FORM-05.2 passage | All owner/member/offset/cap/route/regroup words, final orders/positions and cold-save suffixes |
| Crowd | SEP-04.2 mixed owner/radius/rank/selector crowd and SEP-04.3 enabled/disabled ground controls | All ordered contributions, PRNG/pose/endpoint/occupancy/retry words, public samples and saved mid-order continuation |
| Gate | GATE open group crossing/disabled cached skip, sole-edge disconnection and all chained activation combinations | All motion/consumer/retry ordering and 16,339 saved continuation commits |

`wc3_e2e205.*` executes all eight underlying fresh/save game journeys twice
per edition. It requires every child to execute assertions and propagate any
failure. Classic and TFT each pass three combined tests /2,117,900 assertions.
Formation includes 2,739 owner visits /7,194 member commits per fresh repetition;
gate variants retain 610/635/575 complete motion commits. Mixed and ground crowd
contracts retain 2,255/1,336 visits, 3,228/1,182 contributions and 76/66 retries.
The existing child tests continue comparing their full intermediate streams;
the combined tests do not replace them with end-position checks.

The manifest requires the original completion meanings. Four mixed-crowd orders
are still blocked at the final pre-removal sample; the complete script is not
proof that those orders arrived. Formation supplied clocks/WPM, inferred final
clock and pre-commit capture limits remain inherited. The selected no-hook
click-timing exclusions are also retained. Engine saved continuations do not
prove retail save/load, whole-world state or full RNG equivalence.

The fresh combined runner validates the static/disconnected parent and executes
the two formation capture verifiers, the full original crowd replay and all
three gate capture verifiers before running the actual game categories. It
records child report/command hashes and engine binary/library/JUnit hashes.
The Python gate initially failed because the combined manifest was missing;
negative controls reject missing/reordered categories, changed literal pins,
weakened original child checks and empty/failed/duplicate game summaries. This
is a coverage gate, not a newly discovered gameplay regression.

```sh
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --only oracle-e2e-variants205 --output /tmp/wc3-e2e205-fresh
python3 -m unittest tests.test_wc3_pathing_e2e_variants tests.test_wc3_pathfinding_corpus
```

Ghidra combined-scope comments at `16c150`, `1702f0` and `165f10` are saved and
mirrored in `MapPathfinding.java`. Logs live in
`/GitHub/wc3-analysis/runtime/payoff205/`. This is implementation commit 11 of
the current 12-commit validation batch. No additional full suite or frame-budget
improvement is claimed. Contention/cancellation and all-scenario
determinism remain their open E2E tasks.

## Dynamic blocker and pursuit variants (Payoff206)

E2E-01.2 joins the completed ROUTE-03/ROUTE-05 dynamic and asymmetric-yield
journeys with TARGET-02 ground Smart pursuit and TARGET-03 fogged cached
arrival. The combined runner checks eight unchanged source pins and three
unchanged child contracts before running eight actual game journeys, twice in
Classic and TFT. Each edition passes854,608 assertions. Shared validation and
execution reuse Payoff205's runner; its existing formation/crowd/gate contract
and numerical expectations remain unchanged.

The dynamic category covers all thirteen original engine scenes: insertion
before/after refill, a slower moving peer, removal during waits/partial travel,
terrain closure/reopening, same/different-player crossings and tunnels, and
three-mover convergence. Every observed owner step retains pose, velocity,
facing, fine/adaptive buffers and indices, delays/retries, blocker identity,
search timestamps and charged work. Cold loads reproduce the unchanged suffix.
Tunnel orders retire through cannot-path; the three-mover witness is an acyclic
yield chain. Final facing without a following observation stays unasserted.

The pursuit category retains235 raw initial Smart owner states and85 saved
suffix states, plus238 raw25-word fog-follow states and28 saved suffix states.
Supplemental production tests exercise denied-route refresh/stop/queue recovery,
discarded target samples through the17-visit cadence, and hidden approach and
persistent-follow arrival. The initial approach capture ends with an active
order; its final numerical state is checked without calling it arrival.
Native crowd captures prove4166 denial visits and723 recovery episodes, not
full numerical crowd parity. Unpublished buffer destinations and unwritten
failed-request result words remain excluded.

```sh
LD_LIBRARY_PATH=/GitHub/wc3-analysis/native-sdl2 \
/GitHub/wc3-analysis/verify-venv/bin/python \
  tools/ghidra/run_wc3_pathfinding_corpus.py \
  --binary /run/media/lofcz/ssd_external/Games/w3-research2/game.dll \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --only oracle-e2e-dynamic206 --output /tmp/wc3-e2e206-fresh
python3 -m unittest tests.test_wc3_pathing_e2e_dynamic tests.test_wc3_pathing_e2e_variants
```

The negative gates reject missing categories/completion policies, changed
original source pins, weakened child comparisons and empty/failed/duplicate
game summaries. Ghidra comments at167e40 and169680 are saved and mirrored in
`MapPathfinding.java`; saved readback confirms both notes and `changed=false`.
Captures are freshly evaluated archived Frida observations, not new live runs.
Scene-specific controls/repeat limitations remain those of the child contracts.
Saved engine continuations compare with uninterrupted retail, not original
saves. Wider target families and visibility producers, whole-world/full-RNG
parity and frame-budget claims remain outside this acceptance chunk. Runtime
reports and the batch checkpoint are under
`/GitHub/wc3-analysis/runtime/payoff206/`.

This is implementation commit12/12 of the validation batch. The full
`make -j6 TEST_JOBS=6 test openwarcraft3` checkpoint passes: Classic and TFT each
run3,251 tests/15,224,495 assertions; the pathfinding-tools target passes1,011
Python checks, in addition to the14 focused E2E contract checks. Complete logs,
JUnit copies and `full-checkpoint.json` are retained in the runtime directory.
This checkpoint starts the next focused implementation batch.
