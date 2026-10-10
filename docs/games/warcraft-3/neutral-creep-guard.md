# Neutral Hostile creep guard — Stage A

Stage A introduces per-unit Neutral Hostile guard anchors distinct from the
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
Move is active. If the Move ends early or becomes blocked, the unit re-enters
idle rather than starting an infinite path retry loop. Script orders and queued
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
