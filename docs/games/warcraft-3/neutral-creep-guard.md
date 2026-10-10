# Neutral Hostile creep guard — stages A–E and audit fixes

The integrated implementation introduces per-unit Neutral Hostile guard anchors distinct from the
player Stop guard state. Spawn/creation XY becomes the creep's anchor, and
ordinary automatic acquisition or damage retaliation can own a guard chase.
Explicit accepted orders take precedence and discard automatic chase ownership;
they do not overwrite the anchor. This stage does not implement camp assistance,
creep guard JASS natives, or building-use aggression.

During automatic combat the unit checks XY distance from its original anchor
every simulation tick. `Misc` keys `GuardDistance` (default 600),
`MaxGuardDistance` (default 1000), and `GuardReturnTime` (default 5 seconds)
control the soft radius, hard radius and no-hit timeout. Values are resolved
through the existing map-override-aware `game.config.misc`. Successful damage
refreshes the no-hit clock; attack attempts that deal no damage are not yet
tracked, which is a documented compatibility gap.

Once beyond the hard radius, or outside the soft radius without a qualifying
hit for the configured timeout, automatic combat disengages and starts an
ordinary point Move toward the anchor. The existing movement system handles
pathfinding and arrival. The return phase suppresses acquisition because a
Move is active. If a return Move ends before arrival, the guard remains in return policy
and retries at most three actual movement attempts, with a one-second
simulation-time backoff. Temporary immobility postpones attempts without
consuming the failure budget. Script orders and queued
orders remain higher-priority than automated return.

The guard anchor, timestamps and state flags are included in the game save
field table, preserving guard ownership across save/load. Disabling/resetting
guard behaviour through the JASS natives is reserved for Stage D.

## Follow-up and validation

- Check 600 / 1000 / 5 defaults against mounted ROC/TFT assets and retail.
- Confirm whether missed and fully absorbed attacks refresh the guard timer.
- Confirm guard behaviour after an explicit script order ends; this stage does
  not automatically recapture script-controlled creeps into combat.
- Stage B will handle coordinated assistance and natural-sleep wake propagation.
- Run `make test` and relevant neutral camp map fixtures before merging. This
  patch was prepared without a local build or test run.

## Stage D: script guard controls

- `SetUnitCreepGuard(unit, bool)` is registered and controls Stage A's neutral-hostile automatic creep-leash policy. Disabling it clears automatic-combat/return bookkeeping without issuing another order or modifying Stop guard state. Re-enabling retains the unit's original anchor. Non-neutral-hostile units and buildings do not become creep guards.
- `RemoveGuardPosition(unit)`, `RecycleGuardPosition(unit)` and `RemoveAllGuardPositions(player)` now operate on the separate computer-AI guard-post roster. Removing discards the assigned post; recycling vacates it for existing AI replacement processing; removing all clears that player's AI guard-post roster. Units not assigned to an active post are ignored. They do **not** disable Neutral Hostile creep leashes.
- **Compatibility limit:** Warcraft editor wording describes AI preplaced-unit guard positions and replacement semantics. OpenRealm's existing bot roster only contains `G_BotAddGuardPost` positions; it does not yet represent all automatically registered preplaced-unit posts, Hero/peon exclusions or full retail guard recycling. The three roster natives therefore implement the supported subset, not complete retail semantics. Some historical reports also indicate `SetUnitCreepGuard` is ineffective for Neutral Hostile owners in retail; its OpenRealm behaviour is an explicit interoperability choice pending in-game comparison.
- Explicit trigger orders continue to outrank automatic camp orders via `G_CreepGuardExplicitOrder`.

## Stage E: persistence, lifecycle and determinism

- Guard fields introduced in Stage A are persisted through `g_save.c`'s
  `edictMovement_s` field table. Stage E advances the save envelope to version
  85 and adds version 84 to the explicit rejection cases: older save layouts
  must not be silently treated as compatible.
- Reject inactive/dead creep guard auto-combat and damage updates. A dying or
  freed unit must not restart an automatic chase while effects resolve.
- Clamp `GuardReturnTime` before converting seconds into unsigned milliseconds.
  Custom maps may supply extreme positive values; normal 5-second and map
  overrides remain unchanged. Timers continue to use `level.time` rather than
  wall-clock time, preserving deterministic game-frame scheduling.
- Added targeted tests for disabling/re-enabling creep guarding without
  relocating its anchor and for disabled creeps ignoring damage clock updates.
- Existing return behaviour deliberately gives up on blocked or completed
  Move rather than retrying forever. Further return pathing/reacquisition and
  full saved-in-flight combat tests require simulation fixtures.

**Still requiring integration coverage:** save/load during return with a live
waypoint, repeated interrupted returns, multiple simultaneous camps, paused or
stunned guards, owner conversions, custom map Misc values, multiplayer order
replay, neutral-building-use provocation and retail target score parity. Stage E
provides hardening and focused tests, not proof of full retail equivalence.

## Post-audit regression coverage (save format 86)

The guard audit fix moves camp assistance before lethal-hit death processing,
adds bounded return retries, and persists retry counters/deadlines (save 86).
The following focused tests use the game-module entry points rather than a
copy of guard code:

- `wc3_order_lifecycle.lethal_creep_hit_alerts_surviving_camp_member` drives
  `T_Damage` with a lethal hit and verifies a nearby idle camp ally acquires
  the attacker. This reproduces the pre-fix early-return bug.
- `wc3_order_lifecycle.creep_guard_retry_exhaustion_uses_simulation_time`
  drives `G_CreepGuardTick` across all retry deadlines with a movement-disabled
  guard and verifies that it stops after three attempts without teleporting.
  The existing order cancellation and arrival tests cover state clearing.
- `wc3_save.creep_guard_return_retry_state_round_trip` uses `WriteGame` and
  `ReadGame` to verify the guard anchor, returning flag, retry count/deadline,
  and hit/outside timestamps survive persistence.

These fixtures cover the retry-state save path, but a real in-flight
creep-return `CAbilityMove`/waypoint round-trip and blocked-path movement
callback lifecycle still require dedicated integration coverage. The existing
`wc3_save.live_guard_return_move_resumes_after_round_trip` covers the distinct
player Stop-guard implementation, not creep guard movement.

For changed executable code, run the affected build and `make test` before
merging, in accordance with `CONTRIBUTING.md`. No local compilation or test
execution was performed while preparing this follow-up patch.

## Merge verification

Run the affected game target and `make test` after applying the complete
patch chain. The retry regression drives `monster_think`, while full live
waypoint save/load and native-dispatch tests remain to be added before
claiming end-to-end coverage. Stage D AI guard-post natives cover a subset
of retail guard-position behaviour.
