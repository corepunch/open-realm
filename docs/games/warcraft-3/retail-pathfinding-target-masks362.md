# Target-refresh producer and mask composition

Payoffs361/362 complete the existing TARGET-02.1 producer/composition audit.
The audit combines the complete natural contention regression in
[payoff361](retail-pathfinding-crowd361.md), literal Move and stepped Smart in
[payoff356](retail-pathfinding-target-audit356.md), and a complete amphibious
Smart journey. The amphibious expectations retain all617 original owner visits,
including cached destinations, refresh countdown, request timestamps, route
counts/indices, retry state and exact fine pose. A saved suffix and public Stop
check continuation and teardown.

The original arena publishes dry pathing bytes40 and a CA wall. Copying only the
wall's ground bit02 creates a different amphibious arena: query80 walks through
that synthetic wall. The first362 regression exposed this fixture error at the
second owner visit. The failed log and source identity remain diagnostic evidence;
the fixture now uses the exact original pathing bytes. A second fidelity check
found the copied Footman radius31 also differed from the actual amphibious
Myrmidon radius: native word3fc00000 is1.5 fine, or48 world units. The corrected
fixture retains target radius31 and asserts the original follower radius and
captured reach413d8000 on every approach/persistent visit. Direct extraction of
retail `War3x.mpq` UnitData/UnitBalance also confirms speed270, turnRate0.5 and
propWin60; the copied base's turnRate0.6 explains a further initial-heading
mismatch. The regression now supplies the complete observed/authored profile and
asserts its turn value. These were fixture failures and did not justify changing
movement behavior.

The static producer inventory closes the audit by following the actual unit
request and its consumers, rather than assuming a common path from order names:

| Boundary | Original evidence | Production coverage |
|---|---|---|
| Unit target requests | `5fc640` has two direct callers, Move target dispatcher `5ff8b0` and Attack chase `49a240` | Public Smart/Move/Follow, Attack, unit/point spell approaches and structure targets use the physical target owner. |
| Lower bridge | `05a5c0` adds two Captain and two missile callers | Captain and missile owners remain in their existing GROUP-03.4.7.3 and E2E-06.1 leaves. Their presence does not create another unit-order branch in destination readiness. |
| Current query ownership | Unit virtual158 is `685ef0` → `05ac30`, resolving the current owned query path; absent path returns0 | `no_pathing` supplies0; authored/current flight and ground profiles remain distinct. Profile tests exercise foot, horse, hover, float, amphibious and flight masks through public Move validation. |
| Captured target region | `5fc640` chooses category10 exactly when low query byte AND12 equals10, otherwise category02; `063d60` selects the first matching region | structure279 covers both equivalence classes, collection order, moving live identity, retirement, no recapture and restored ownership. Structure319 verifies runtime footprint publication through public target motion and cold saves. |
| No-warp capture | `5fc640` queries the target's Adro ability class at request construction | Public approach/persistent handoff tests cover authored aliases, later ability removal, re-admission and save restoration. |
| Group sampling and refresh | `16cd30`, `16ce10`, `169680` | delayed164, refresh274 and full36-follower361 preserve discarded premature samples, denial, delayed admission, old-route renewal and saved cadence. |
| Changed-cell predicate | `167e40` has no order-class, profile, target-type or visibility branch | The original28,880-case gate oracle and production journeys share coordinate buckets and age10 for both retained timestamps. Search masks/geometry produce those timestamps upstream. |
| Visibility and target loss | Fixed callback `23a760`, validation and retained subscription owners | target251/252/256 cover the original policy table, hidden cached destinations, reacquisition, cancellation, source pause, detection/shared vision and cold saves. |
| Target-family geometry | The same request captures range, region and predicted fine position before admission | Exact air/ground/mixed186/187, Attack306, spell185, structure278/279, flight280, target246 and cohort319 regressions preserve the respective production owners. |
| Retained cohorts and request republication | Canonical readiness, shared parameters and cached target region retain separate ownership | shared189/scheduler190 cover shared storage and visit order; capscope273 covers target aliases, pending readiness, nested rejection/replacement and cold saves. Target319 retains target identity, range and cached region through Slow isolation/republication and saved continuations. |

The query selector has exactly two equivalence classes for all256 low-byte
inputs. The gate and timestamp writers do not branch on those classes. This
establishes target-refresh composition across the identified producers; it does
not claim that every possible map geometry has an independently captured complete
trajectory. Remaining route-search, footprint, Captain and missile contracts
retain their existing algorithm/owner leaves.

Fresh read-only retail observation and observer-free control for the amphibious
scene are already part of the complete nine-scene356 archive. All5,017 actual
owner rows and2,085 public markers match the original observation; the new617-row
engine fixture is taken directly from scene6, without decimal reconstruction or
new expected values. Ghidra adds two saved audit notes and exports512 instruction
guards across eight complete functions; saved readback is identical. The verifier
combines the existing inventories for1,434 unique guarded original instructions.

The external archive is
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/TARGET-02.1/payoff362/`.

The final strict verifier passes109 tests and2,026,927 assertions in each of
Classic and TFT, including complete movement-profile parsing/occupancy and
positive-speed zero-query movement. It also executes the original28,880-case
destination gate oracle and guards1,434 original instructions. Seven profile and
evidence rejection tests pass; crowd361 has ten additional integrity checks.
Existing engine behavior matches the correctly reproduced inputs, so no new
engine behavior was invented for this closure.

```sh
python3 tools/ghidra/verify_wc3_pathing_mask362.py \
  --binary /path/to/retail/game.dll --report /tmp/mask362-fresh.json
```
