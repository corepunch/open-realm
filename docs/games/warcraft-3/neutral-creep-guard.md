# Neutral Hostile guard and camp AI

The Neutral Hostile guard system uses an anchor established at unit creation and
keeps this separate from the player Stop/Hold Position guard anchor.
It covers automatic acquisition, bounded pursuit, return-to-anchor movement,
lethal-hit camp assistance, wake-up and construction-start provocation.

## Guard lifecycle

`creep_guard_enabled` controls whether automatic guard policy is available.
`creep_guard_phase` has exactly one of `CREEP_GUARD_IDLE`,
`CREEP_GUARD_COMBAT`, or `CREEP_GUARD_RETURNING`.
The phase belongs to the server simulation and is persisted in the save.

`[Misc] GuardDistance` (600 fallback), `MaxGuardDistance` (1000 fallback),
and `GuardReturnTime` (5 seconds fallback) control leash policy.
Positive damage refreshes the no-hit timer. Outside the hard range, or outside
soft range after the timeout, an automatically fighting creep attempts a Move
back to its original anchor. Stopped moves retry on a simulation-time one-second
backoff, no more than three actual attempts. Root, stun, pause, Cyclone and
other immobilizing conditions delay attempts without consuming that budget.
No teleportation occurs. If the target is lost and automatic combat becomes
idle, the unit can enter return policy even without an attack-end callback.

The generic `A_ORDER_ACCEPTED` ability notification does not cancel guard state;
it is also used by internal abilities. Explicit accepted target/point orders
cancel automated ownership. Queued and scripted orders retain precedence.
Do not use the ability notification as proof of external player/script intent.

## Camp assistance and target eligibility

A post-mitigation positive hit, including a lethal hit, calls for help while the
victim still exists. Eligible idle nearby Neutral Hostile creeps within
`[Misc] CreepCallForHelp` (600 fallback) of both anchors and current positions
can attack the assailant. One fan-out per hit prevents recursive chains.
Naturally sleeping eligible units are woken through the existing sleep API.

Autonomous idle creeps ignore flying scouts and finished structures. A newly
started construction can provoke eligible nearby creeps using
`[Misc] BuildingPlacementNotifyRadius` (600 fallback). Level-7+ creeps use
an approximate wounded-unit/Hero preference, with distance tie-breaks.
This scoring is not yet proven retail-equivalent.

## JASS and computer AI

`SetUnitCreepGuard` controls the Neutral Hostile guard policy.
`RemoveGuardPosition`, `RecycleGuardPosition`, and `RemoveAllGuardPositions`
operate on the existing computer-AI guard-post roster, not the creep leash.
Automatic preplaced-unit registration and replacement parity are incomplete.

## Persistence and testing

Save version **87** replaces the two mutually exclusive guard-phase booleans
with a serialized lifecycle enum. Older saves are rejected in accordance with
repository policy. Existing tests cover lethal assistance, retry bookkeeping,
anchor independence, enabled/disabled policy, and state round-trips.
Added integration tests exercise a real return `CAbilityMove`/waypoint through
`WriteGame`/`ReadGame`, and drive retry recovery through `G_RunEntities`.

Known gaps: complete JASS VM dispatch tests for guard natives, inverse tests
for sleeping/construction/ownership changes, retail camp membership and neutral
building sale aggression, missed/absorbed hit leash refresh, exact high-level
target scoring, and multiplayer order replay. Passing unit tests will not by
itself establish retail parity. Build the affected WC3 target and run `make test`
before committing, per `CONTRIBUTING.md`.
