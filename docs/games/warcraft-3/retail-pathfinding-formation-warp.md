# Retained formation warp markers — Payoff165

FORM-04.2 combines the admission-denial integration from Payoff164 with the
retained warp-marker producer and recovery-counter fix implemented here.
Full mixed-selection motion and passage composition remain FORM-05.1/05.2.

## Retail contract and engine owners

`Path_RequestAcceleratedRoute` (`6f166c30`) replaces path flag `01000000`
from the accelerator's reconstructed marker count at `+a0`. An interval or
admission denial returns before that replacement. The engine records
`moveFineRoute_t.warp_markers` immediately after each successful member or
physical-group coarse search. This is a retained route fact, rather than a
test of the current index or a repeated scan of the point buffer.

`PathGroup_StepMember` (`6f16a790`, common tail `16a9bc..16a9db`) mirrors
that flag into member `80000` after ordinary and held decisions. The existing
`169b00` classification guard consumes it on the following owner visit,
seeding cooldown66 before the normal tail decrement. Move's common decision
wrapper replaces the bit on every exit, including wait and arrival. A held
replacement can retain a previous route's marker; a successful marker-free
coarse replacement clears it. Fresh entity storage starts clear.

`Path_CheckAcceleratedWaypoint` (`6f165f10`) retains both point counts and
the route flags on successful portal consumption. The captured portal
transitions reset adaptive index to0 and invalidate fine index to−1. They do
not rebuild the physical group, offsets or formation layout. The engine's
existing portal consumer preserves this behavior and now also preserves the
saved classification state.

`PathGroup_RequestRoute` (`6f16ce10`) calls `1697a0(0,1)` after every
admitted replacement: retain members, reset age/completion counters, advance
the coarse selector and refresh layout. The engine previously retained its
completion counter on recovery. `G_UnitMoveGroupDestinationStatus` reports an
actual rebuild to Move, independently of endpoint equality. A cached lookup
does not reset counters or reconstruct the layout. The original three-argument
query remains available for consumers that do not need this observation.

All added work is constant time per search/decision. There is no new entity
allocation, point-buffer scan in gameplay, worker-dependent decision or wire
field. Save134 serializes the marker for both member and physical-group route
owners; Save133 and older formats are rejected under the existing policy.

## Frozen evidence

[`retail-warp-markers165-1.27.json`](../../../tools/ghidra/fixtures/retail-warp-markers165-1.27.json)
pins three complete archived Frida observations and one observer-free public
control from FORM-05.1/05.2 map d. Their first physical gate journey repeats
the normalized route, member-marker lifetimes, layouts and warp transitions:

- Four portal consumers per observation preserve counts, flags and destination
  words, with fine/adaptive index invalidation only: twelve transitions total.
- The first owner has one layout and no later member reset or regroup advance.
- Member marker intervals are u4 visits2..116, u5 3..93, u2 3..176 and u3 3..188.
- Classification cooldown snapshots65 occur at visits3,69,135. The flag remains
  present in180 captured post-warp member commits across the three repeats.

The UI inputs have a one-visit phase difference in the second journey. Complete
motion words and control trajectories are deliberately not claimed identical.
These are freshly verified archived captures, not new live runs. The control
proves a complete uninstrumented public timeline; its differing UI phase does
not establish bit-identical motion.

Payoff164's separately pinned two crowd repeats verify4166 denied visits each
with one stop and no regroup/layout/advance, plus723 successful recovery
episodes with retained members and fresh layout. Those checks remain runnable
through `verify_target164_live.py` and its existing corpus entry.

```sh
python3 tools/ghidra/research/verify_warp165_live.py \
  --expected tools/ghidra/fixtures/retail-warp-markers165-1.27.json \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27/research \
  --output /tmp/warp165.json
```

Complete native bodies, five saved Ghidra comment readbacks and acceptance logs
are under `/GitHub/wc3-analysis/runtime/payoff165/`. Task-tagged mapping rows
are committed in `MapPathfinding.java`. Existing retail structures already
describe the path/member flags; no speculative layout is added.

## Production regressions

`wc3_movement.warp165_group_marker_classification_and_cold_save` constructs
three mixed-rank actors and a real authored Way Gate on a divided map, issues
a public group Move and advances normal server frames. It checks marker
publication, the next-visit classification cooldown, cold save/load, the held
early exit, all three actual crossings and clearing after gate deactivation
and a fresh marker-free order. The original engine failed74 of94 assertions
before marker implementation; the expanded regression also verifies retained counts, invalidated indices
and unchanged group point/offsets at actual crossings.

`wc3_movement.warp165_multi_member_denial_retains_layout_until_recovery`
uses an actual three-member public group under explicitly controlled coarse
contention. Every denied member integrates its previous velocity once and
stops; offsets, flags, cooldown and completion state remain retained. Cold
load restores the pending request. Recovery rebuilds the layout and resets
the counters before ordinary completion processing. Its first run exposed
the retained completion counter; the corrected regression passes35 assertions.
Controlled budgets are not presented as a new public retail crowd capture.

These production checks establish the owner transitions in FORM-04.2. They
do not substitute for FORM-05's remaining complete retail mixed-group
trajectories. Eight Python evidence guards reject marker loss, changed route
counts/destinations, missing index invalidation, unowned/incomplete portals
and unexpected formation reconstruction.
