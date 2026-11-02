# Selected formation passage (Payoff169)

FORM-05.2 now has a complete production-engine journey, rather than only isolated
layout and regroup comparisons. Actual JASS creation and the selected Move
producer traverse the delivered WPM wall gap. All340 owner visits and1,825
member pre-commit records match the original; a cold save after visit80 preserves
260 further visits and1,345 member commits, including the second layout after
regroup visit92. Both Classic and TFT pass114,761 assertions. Public final
positions also match the original script's three-decimal output, with all six
public heads completed.

## Creation publishes the initial formation state

Original `16de50` zeroes a vector, calls `169ef0` to accumulate predicted member
positions in their existing order, multiplies the sums by the software reciprocal
of count, then calls `16d8e0` to publish heading and fresh bit`10000`. It subsequently
replaces the formation point with the representative member's predicted pose.
The heading mean and route-source point are distinct. For the mixed six-member
witness, the mean is fine(10,31), the source point is(10,32), and the initial
heading is`3cd03cbe` toward raw goal`42480006/4200000d`.

OpenRealm previously retained heading0 and flags0 until route admission. The new
regression first failed on the initial fresh flag (`0` versus`10000`). Move now
performs the ordered mean/heading publication during creation and retains the
representative route origin. This is a general cohort operation, with no map,
rawcode or coordinate exceptions and no allocation. Existing movement behavior
and save format137 remain unchanged; the existing saved heading/flags retain
this state.

## Complete observed boundaries

The engine test uses read-only observers before the owner body, before each
member commit, after route admission and at regroup status calculation. It
compares member ordering, offsets, destinations, requested/committed speeds,
cap selection, flags, pre-commit position/velocity/facing/clock, route indices
and regroup status/arrived/near/completion. All decisions precede all commits.

The first layout follows route admission; regroup advances after visit92 and
rebuilds at the final point. Final route admission is visit93. Save continuation
crosses these boundaries without injecting routes, offsets or member decisions.
The first pre-owner age is normalized to the engine's initialization convention;
all subsequent captured ages are compared. This does not claim the wider initial
failed-admission age contract.

The fixture supplies the observed primary owner clocks and original scene WPM
geometry. Owner clocks are recovered from the following pre-owner mover clock;
the first is independently confirmed by the exact layout-entry observer. All
successive intervals match six native5ms increments. The final supplied owner
clock is inferred by that verified progression and is explicitly not an observed
post-commit word. Commit captures contain **pre-commit** pose and the resulting
speed argument, not post-commit poses. Full public clock production and arbitrary
MPQ scene construction retain their existing NUM/BASE/MAP tasks.

## Reproduction and provenance

The frozen fixture is `tools/ghidra/fixtures/retail-formation-passage169-1.27.json`;
the generated engine table is
`games/warcraft-3/game/tests/fixtures/retail_formation_passage_169.h`.
Its strict verifier regenerates the table directly from the complete original
FORM-05.2 passage capture and rejects any differing word. Ten archived captures
include eight observed runs and two observer-free controls. Their exact hashes,
metadata, completion and event counts are checked. Across those observations,
2,491 regroup calls over21 physical lifetimes have no timing violations;23
captured layouts equal current production C at both-O0 and-O2. This includes
six repeated singleton layouts in addition to the handoff's original17 cases.
Repeat differences remain in the raw captures.

```sh
python3 tools/ghidra/research/verify_formation169_passage.py \
  --expected tools/ghidra/fixtures/retail-formation-passage169-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --header games/warcraft-3/game/tests/fixtures/retail_formation_passage_169.h \
  --output /tmp/formation169-evidence.json
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_movement.formation169*'
build/bin/openwarcraft3-tests -data build/tests -tft +dedicated 1 \
  +test 'wc3_movement.formation169*'
```

Retail binary1.27.1.7085 SHA256:
`d51e5680243fc90e19c9d6074f7fac433c466d3cf5f46e2364291725574d8236`.
Original reports remain under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/FORM-05.1/` and
`FORM-05.2/`; fresh implementation/build/RED/green/verifier/Ghidra evidence is
under`/GitHub/wc3-analysis/runtime/payoff169/`. No new live retail run is claimed.
Ghidra saves the recovered `169ef0` name and typed ECX/stack4/RET4 ABI, plus
creation, layout and regroup comments; mapper and type schema reproduce them.

Validation: production/test builds,403 Classic movement tests(5,788,684
assertions before the final-position assertions were added), final passage
Classic/TFT and14 selected-order TFT tests(82,279 assertions). This is focused
iteration validation, not a full repository checkpoint.

Payoff170 closes FORM-05.1 with complete engine replay of the independent-order
controls and remaining selected repeats. Wider policy flags, nontrivial layout
prediction and selections above six retain their owning tasks.

Follow-up: [selection and independent ownership](retail-pathfinding-formation-selection.md)
compares both selected click phases with complete independent public singleton journeys.
