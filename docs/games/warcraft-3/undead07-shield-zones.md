# Undead07 Shield Zones

## Contract

`Undead07.j` filters shield-zone entrants by owner and Undead race/type, adds accepted units to a JASS group, then applies life loss and vertex tint from periodic callbacks. Leaving the region removes a unit from that group. Shield shutdown is registered on each Archmage's life reaching zero; the script then changes the shield doodads to their death animation.

The map reads race from the authored `UnitData.race` column. `GetUnitRace` returns a `race` enum handle using the Warcraft race value, and `IsUnitType(..., UNIT_TYPE_UNDEAD)` uses the same authored race field. Region entry and leave registrations retain their region and trigger in `level.events`; movement crossing a boundary publishes the corresponding event.

## Confirmed Failure And Fix

The captured `openwarcraft3.log` showed three missing native paths:

- `GetUnitRace` returned a null handle and `IsUnitType` did not recognize `UNIT_TYPE_UNDEAD`, so the shield entry filter rejected units.
- `TriggerRegisterUnitStateEvent` returned null, so the first two Archmage life-limit shutdown triggers were never installed.
- `TriggerRegisterLeaveRegion` returned null, so units could remain in the periodic shield group after leaving.

The existing `EVENT_GAME_STATE_LIMIT` event carries the registered unit as its subject and retains the limit operator/value. `G_SetHealth` publishes the matching response only when life crosses into the registered condition, allowing the standard trigger dispatch to run the map action. `G_TouchTriggers` already detects region exits; implementing the missing registration makes that existing path reachable.

## Verification

`games/warcraft-3/game/tests/t_api.c` covers the authored Undead type filter, unit life threshold crossing through `G_SetHealth`, and leave-region registration/dispatch through a JASS action. Run the focused suite with:

```sh
make openwarcraft3-tests test-assets
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_api.*' +com_frame_limit 10
```

The tests use synthetic `UnitData` and headless JASS execution, so they do not require retail archives or a map launch.
