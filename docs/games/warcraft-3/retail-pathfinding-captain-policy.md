# Captain home, retreat and point policies (Payoff159)

## Recovered contract and implementation

The public non-combat home policy is event-driven. `G_BotSetCaptainHome` and actual
member removal invoke the recovered `9d08e0` policy. An occupied home change
updates the authored home, preserving the current actor and retained point request.
`InitAssault` retains state; it does not turn an active attack Captain into state1.
An empty existing actor must remain in place until reevaluation: moving it first
makes the near-home test suppress the required point request.

With a Town and a Captain outside its home predicate, flee-disabled or state1
Captains return only when their roster is empty. With flee enabled, the cleared
strength-ready bit or `strength_count < 3 && member_count < 7` requests home.
The retreat flag is native `6c&2`, independent of the state enum. Near-home GoHome
clears it only when the range-enter count is at least half the roster. Empty
rosters and state1 place the actor through ordinary Move admission before the
retained home request at range500.

`CaptainAttack` is now registered for player-bound AI. It sets state2, clears the
retreat flag and publishes a point request at range200. The virtual actor's goal
event returns home when it reaches the retained point predicate; an event outside
that predicate republishes range200 while retaining the original request range.
The actual ordinary physical-group completion path delivers this callback.

`S_CaptainPointMove` keeps virtual actor and physical roster publication together.
Its captured speeds are 500 (`43fa0000`) during retreat, 9999 (`461c3c00`) for an
empty roster, and the slowest member on the covered ordinary roster paths.
The existing scalar near-home predicate remains `distance_squared <= 2*r*r+5000`
for range at least100; a host Euclidean500 test is not an equivalent boundary test.

The repeating `d01c1` request is registered at actor creation with period1 second.
`G_RunCaptainTimers` retains its absolute deadline independently of private AI
script execution. Its idle-member reissue uses `S_ReissueCaptainUnit`, preserving
range-listener membership, entered/outer flags and deadlines. Temporary enrollment
is a different transition and must not be used to implement periodic reissue.
The old frame-polled health/power ratio and persistence timer are removed.

## Counter identity and persistence

`CaptainAI_UpdateRosterCountsAndRanges` at `6f9d0650` changes the signed `c0`
counter on roster additions/removals. Campaign mode counts every member.
Assembly at `9d06d3` passes **unit+b8** to `057fd0`. The actual registered
`GetUnitState` native (`204050`, registration `209277`, name string `a97fb0`)
calls `687270`; its MANA case2 passes precisely **unit+b8** to that helper.
The melee rule therefore uses current mana zero or a non-illusion Hero at the
mutation point. It does not sum life or recompute the counter on every update.
The engine now stores `strength_count` and applies these deltas at its roster
writers. A public melee mutation capture is still needed for the composed lifetime.

Save133 persists the policy bits, signed strength counter, retained point/range
and periodic deadline alongside the logical roster. Cold engine load restores
these without retaining the private VM. Logical encounter order and physical
member indices remain separate. Earlier layouts are rejected.

## Evidence and automated verification

The [research handoff](retail-pathfinding-handoffs/GROUP-03.4.7.3/HANDOFF.md)
contains four twice-observed public policy scenes and their controls. Its decoded
policy verifier reproduces24 reevaluations and16 goal-event GoHome calls.
That verifier's distant-scene geometric approximation does not replace the
original scalar boundary predicate used by the engine.

Additional readonly observations in `runtime/captain-policy159/` under the
[report root](retail-pathfinding.md#binary-and-evidence-conventions) freeze12
speed publications and128 periodic updates (64 per Captain) in two repeats.
All373 public markers match the existing observer-free control with the identical
packed map hash. The sole complete Preload-file difference is PreloadEnd's wall
clock; the verifier normalizes only that non-simulation field, retaining every
public marker and every frozen speed/update word. Metadata pins observer,
controller, map, DLL and capture hashes. Six verifier tests cover acceptance and rejection of changed
speed/deadline/policy data, missing updates and an instrumented control.

```sh
python3 tools/ghidra/run_wc3_pathfinding_corpus.py \
  --archive /GitHub/wc3-analysis/reports/pathfinding-1.27 \
  --output /tmp/captain-policy159-fresh \
  --only live-captain-policy-speed-and-period
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_bot.*'
build/bin/openwarcraft3-tests -data build/tests -tft +dedicated 1 +test 'wc3_bot.*'
```

The production regressions first failed eight assertions for empty GoHome,
removal-driven retreat and the missing native. A further empty home-change test
failed three assertions before the placement/publication ordering fix. Tests now
exercise count-versus-life decisions, speed words, native admission, goal-event
return, retreat cancellation and cold saved policy/deadline restoration.
The existing23 public Captain movement tests still pass2,606,567 assertions in
Classic, including captured GoHome and saved continuations.

Ghidra names/comments are saved and mirrored in `MapPathfinding.java`: creation
period and state arguments, distinct authored/retained points, runtime speed
constants, mana operand provenance, periodic update and near-home inverse.

## Batch checkpoint

The production game and test targets build. The full repository checkpoint
passes with the isolated native SDL2 runtime: Classic and TFT each pass2,877
tests and11,022,431 assertions; all833 pathfinding Python checks pass. The exact
staged snapshot also passes all833 checks and the fresh strict policy corpus.
The initial host run stopped in the documented SDL2-to-SDL3 text-event
conversion crash; GDB showed no test failures before that external-library fault.
The engine was not changed to bypass the input regression.

## Remaining scope

GROUP-03.4.7.3 stays open. No whole-trajectory parity claim is made for the new
retreat/Attack scenes. Combat target identities and engagement/strength branches,
retail mid-Captain save/load, public melee mana mutations, siege special range
counts, non-home missing-member speed factors/eligibility, default Town home
creation, larger rosters and cross-clock/callback scheduler compositions still
need their evidence and engine integration. The existing handoff proposes extra
IDs for some gaps; this integration keeps them under the existing open leaf.

See [temporary enrollment](retail-pathfinding-captain-enrollment.md) for the
separate enrollment-before-buff and duplicate/withdrawal transitions.
