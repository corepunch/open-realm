# Critter wandering (`Awan`)

`Awan` owns autonomous decisions. The unit simulator drives its innate `A_IDLE`
callback; the existing Move ability owns movement, collision and pathfinding.
A simulation-local PRNG is persisted per unit with the next movement deadline
and a private, stable destination edict reference. No client-side decisions or debug logs.

## Behaviour

- Eligible idle units with `Awan` periodically attempt a short Move from their
  current location. No spawn-centred leash is imposed.
- Wander suppresses idle automatic combat acquisition and retaliation, not
  enemy selection of a critter as a target.
- Nonlethal damage attempts to move away from the attacker, unless any explicit active order
  (moving, attacking, casting, harvesting, building or patrolling) is active. A new Wander movement can be interrupted.
- Unit pause, stun, immobilization, construction, training, Hold Position and
  queued orders prevent starting new autonomous moves.
- Each unit owns a private, stable destination edict while wandering is active.
  The shared waypoint ring cannot recycle this edict. Move owns pathfinding.
- An external Move replaces the internal goal even when its movement procedure
  remains unchanged. Accepted non-Move orders also clear Wander ownership.
- Disabling `Awan` cancels its currently owned Move, but never cancels an
  unrelated explicit order.
- The scheduling PRNG and private destination reference are part of save version 66.

## Retail uncertainty

Movement delay 8–10 seconds, radius 64–256 world units, and retreat distances
192/96/48/24 units are **provisional**. They are not verified retail constants.
Destination selection is position-relative. Stop/Hold ordering, proximity-only
fleeing, special-case critter behaviour and JASS order-event compatibility require
retail tests. This implementation deliberately does not synthesize JASS issued
orders for autonomous Move.

## Test coverage and remaining gaps

The headless tests cover innate registration, separate seeded schedules, scheduler
pause/resume, accepted-order ownership clearing, disabled-ability cancellation, external Move preservation,
recycled shared-waypoint generation rejection, private-waypoint non-recycling,
serializer field and active-goal round trips.
New tests exercise two naturally scheduled Move/arrival cycles on the shared
synthetic pathmap, custom Wander alias enable/disable, and a retry deadline
following terminal blockage. Compilation and execution remain outstanding.
Further coverage is needed for dynamically obstructed routes, blocked retreats,
and exhaustive queued-order restoration after loading.

## Stable destination lifetime

The shared 256-entry waypoint ring may overwrite coordinates of an active
Move target, not merely its generation. Each unit that actually generates a
Wander/flee order therefore allocates one private invisible destination edict,
reuses it only when the previous autonomous Move is finished, and frees it on
ability disable or unit removal. The private pointer is serialized as F_EDICT;
no client AI or alternate pathfinding is introduced. Authored Wander aliases
are detected via `S_ResolveAbilityAlias` instead of only matching `Awan`.

## Disable and death cleanup

The private destination is ability-owned. Disabling Wander, death, or unit
removal must stop an active Move that still targets this private entity,
detach `goalentity`, clear Wander ownership, and release the entity even
when an earlier accepted-order callback already retired the owner token.
External orders with independent targets remain untouched. The revised
regression tests exercise stale ownership, external Move preservation,
combat damage dispatch, and continuation/cleanup following a save/load.
Natural arrival and blocked recovery now have test assertions, but their
execution and retail timing still require local runtime verification.

## Follow-up source review (6 October 2026)

- Fleeing checks `G_UnitHasActiveOrder()` rather than only `unit_is_walking()`,
  so damage does not override an active attack/cast/build/harvest/patrol order.
- Ability-disable tests must use an actual `Awan` private waypoint, not a
  shared `Waypoint_add()` goal mislabeled as Wander-owned.
- The shared synthetic pathmap is all-walkable. Scheduler tests now demand
  two naturally initiated movements and arrivals, without injecting damage.
  A dynamically blocked route remains to be tested with a dedicated fixture.
- The post-load test advances `monster_think` through arrival; the test must
  still be executed before successful continuation can be claimed.

## Blocked Move recovery

Move invokes `S_WanderRecoverBlockedMove` before entering its terminal Hold
state. It checks the currently targeted private waypoint rather than the
`wander_goal` token, because move/accepted-order callbacks may have already
retired that token. A blocked autonomous Move returns to idle and retains its
private waypoint for the next decision; external Moves continue to use normal
Hold semantics. Unit tests cover stale tokens, external orders and repeated
damage retargeting. The new synthetic arrival tests and post-load test
remain unexecuted; blocked routing on a dynamic obstacle fixture is still
missing.

## Regression fixtures added in the follow-up

The synthetic all-walkable map is used to advance `monster_think()` through a
real private Move arrival and to check that the next Wander decision is
scheduled. A second test saves during the active Move, reloads, and advances
through arrival rather than ending the order manually. A blocked-route test
sets the production movement progress watermark to a terminal blocked state
and advances the actual Move think callback; it asserts that Wander does not
enter the terminal Hold pose. These fixtures need compilation and execution;
the presence of source assertions does not establish that they pass.

Precise retail random intervals, native order-event behaviour, executing
the new custom derived ability fixture, and large-population entity capacity
remain separate validation work. No runtime performance or retail-fidelity claim is made here.

## Arrival and failure retry timing

A completed autonomous Move starts a fresh wandering delay at the Move arrival
callback, rather than retaining the deadline chosen before travel. A terminal
blocked Move similarly schedules its next attempt *before* returning to idle,
so a failed journey cannot immediately retry when its old deadline expired.
These timings are provisional retail approximations.

The regression suite includes autonomous idle → Move → actual arrival → idle →
second Move → arrival using the shared test pathmap, plus an expired-deadline
blocked Move check and an authored `Awan` alias activation/disable check.
The blocked-route test injects the terminal blockage watermark into the
production Move think callback; a full dynamic-obstacle pathfinding fixture
and runtime population benchmark remain outstanding.
