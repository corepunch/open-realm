# Selected and independent Move ownership (Payoff170)

Building on [selected passage](retail-pathfinding-formation-passage.md),
FORM-05.1 compares the actual UI selected Move producer with six independent
JASS point orders in the same mixed-unit scene. The engine now executes both
through physical movement groups: one six-member cohort for selection, six
singleton cohorts for independent orders. Independent admission previously set
an `individual` flag and ran a separate entity walker, skipping the common
route, decision, commit and regroup pipeline. Removing that flag makes the
public producer follow the recovered owner, without adding map or unit exceptions.

## Retail ownership and classification

Original `05bb90` receives the independent point order through the unit's
Move bridge, clamps and converts its requested position, prepares the request,
and reaches `16bdb0` activation through `16bd83`. UI selection enters through
`89ccfc/89cd34`; both producers allocate at `16b7b0` and tick at `16c150`.
Singleton membership does not select another movement implementation.

Original `16c250` branches at `16c281` for count1 and at `16c290` for group
flag`200`. Those branches step members directly, bypassing multi-member
classification and its temporary spatial exclusion. After multi-member steps,
`16c325` tests row flag`300000`; `16c34c` marks positive requested speed with
row flag`100000`. `StepMember` does not own this publication. The engine now
marks eligible rows in that classification postpass, so moving singleton rows
retain flags0 exactly as the independent captures do.

Advisory steering before the first layout uses the requested destination,
rather than an unpublished zeroed member destination. This applies to route
queries, held-goal headings and fine retry restoration. The owner publishes its
layout before executing member decisions. No new saved fields are needed;
format137 retains the existing physical groups and routes.

## Complete engine regression

The failing-first public regression creates all six mixed-rank units before
issuing orders, preserving creation identity and newest-first owner traversal.
It drives the real selected producer twice, retaining each captured click phase,
and the independent JASS producer once. Read-only observers compare every owner,
member pre-commit, route and regroup boundary with the frozen original words.
No observer supplies a route, steering decision, cap or formation layout.

| Scene | Physical groups | Owner visits | Member commits | Saved suffix visits | Saved suffix commits |
|---|---:|---:|---:|---:|---:|
| First selected click | 1 | 340 | 1825 | 280 | 1465 |
| Second selected click | 1 | 340 | 1825 | 280 | 1465 |
| Independent orders | 6 | 1719 | 1719 | 1359 | 1359 |
| Total | 8 | 2399 | 5369 | 1919 | 4289 |

Each scene cold-loads a save after60 owner frames, then compares its complete
remaining native stream and final script positions. The first failing run
retained the two passing selected streams but rejected all six `individual`
flags and observed no independent physical-owner commits. The first owner
fix exposed the erroneous singleton `100000` publication; moving that write
into the classification postpass resolves both differences.

Earlier small tests that directly called an entity walker now drive the
production owner for physical point groups. Their controlled intervals remain
explicit and advance the primary clock with epoch handling. Coarse route checks
read the group-owned route; fine and adaptive leg checks remain member-owned.
The isolated constant-heading pose consumer uses internal waypoint admission,
while complete public journeys continue to cover public admission and clocks.
Hero save/load and audit drivers also advance the scheduled owner, retaining
their mid-walk, resumed movement and pause/cinematic assertions. Frozen
numerical expectations are retained.

## Evidence and reproduction

[`verify_formation170_selection.py`](../../../tools/ghidra/research/verify_formation170_selection.py)
checks all ten provenance-pinned captures using the preceding passage verifier,
then reconstructs the complete three-scene C fixture byte-for-byte. It also
requires the two independent repeats to extract identical streams. The manifest
is [`retail-formation-selection170-1.27.json`](../../../tools/ghidra/fixtures/retail-formation-selection170-1.27.json).
The raw archives remain under
`/GitHub/wc3-analysis/reports/pathfinding-1.27/research/FORM-05.1/`.

```sh
python3 tools/ghidra/research/verify_formation170_selection.py \
  --expected tools/ghidra/fixtures/retail-formation-selection170-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --header games/warcraft-3/game/tests/fixtures/retail_formation_selection_170.h \
  --output /tmp/formation170.json
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 \
  +test 'wc3_movement.formation170_public_selection_and_independent_orders_match_retail'
```

Saved Ghidra comments cover `05bb90`, `16bdb0`, `16c150` and `16c250`;
`MapPathfinding.java` retains those annotations. The recovered `16c250`
thiscall has group in ECX, spatial target at stack+4 and RET4, recorded in
the reproducible type manifest and confirmed after saving the program.

The fixture supplies original owner clocks and frozen WPM geometry. Final clocks
follow the verified six-5ms progression; captured commits describe pre-commit
state, not unobserved post-commit words. Both selected click phases remain
separate. These are existing complete Frida captures, not a new live run.
Selections above six, AI producer policy and membership mutation remain separate
tasks. This comparison closes the producer/assignment/cap/trajectory leaf and
does not claim to close every movement fidelity gap.

Validation: production and test builds; all 404 movement tests in both Classic
and TFT (6,195,439 assertions each);127 unit and 188 combat tests per mode;
194 save tests per mode (27,614 assertions each);850 pathfinding Python tests;
and the fresh strict selection/independent corpus report.
This is the eleventh implementation commit after the Payoff159 full repository
checkpoint; it does not claim another `make test` checkpoint or a frame-budget gain.
