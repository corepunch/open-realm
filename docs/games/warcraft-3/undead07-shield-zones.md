# Undead07 Shield Zones

## Contract

`Undead07.j` filters shield-zone entrants by owner and Undead race/type, adds accepted units to a JASS group, then applies life loss and vertex tint from periodic callbacks. Leaving the region removes a unit from that group. Shield shutdown is registered on each Archmage's life reaching zero; the script then changes the shield doodads to their death animation.

The map reads race from the authored `UnitData.race` column. `GetUnitRace` maps each authored race string to its JASS `race` enum value; `IsUnitType(..., UNIT_TYPE_UNDEAD)` uses the authored race classification. Region entry and leave registrations retain their region, trigger, and optional boolexpr in `level.events`. Crossing evaluates the filter with `GetFilterUnit()` bound to the crossing unit before publishing the event. `GetEnteringUnit()` and `GetLeavingUnit()` resolve to that event subject.

## Confirmed Failure And Fix

The captured `openwarcraft3.log` showed three missing native paths:

- `GetUnitRace` returned a null handle and `IsUnitType` did not recognize `UNIT_TYPE_UNDEAD`, so the shield entry filter rejected units.
- `TriggerRegisterUnitStateEvent` returned null, so the first two Archmage life-limit shutdown triggers were never installed.
- `TriggerRegisterLeaveRegion` returned null, so units could remain in the periodic shield group after leaving.

The existing `EVENT_GAME_STATE_LIMIT` event carries the registered unit as its subject and retains the limit operator/value. `G_SetHealth` publishes the matching response only when life crosses into the registered condition, allowing the standard trigger dispatch to run the map action. Runtime health changes, including Avatar's temporary health delta and expiry clamp, pass through `G_SetHealth`. Unit state event registration currently supports `UNIT_STATE_LIFE`; unsupported states and limit operators are reported and return a null event.

Region event filters are JASS function references in the saved event registration. Save/load persists them by function name, matching trigger actions and conditions.

## Verification

`games/warcraft-3/game/tests/t_api.c` covers JASS `GetUnitRace` and `IsUnitType` for authored Undead data, the authored race-to-JASS enum table, unit life threshold crossing, and filtered enter/leave events driven by real unit movement through `G_TouchTriggers`. `games/warcraft-3/game/tests/t_avatar.c` covers threshold events crossed by Avatar's health increase and expiry. `games/warcraft-3/game/tests/t_game.c` covers save/load of a registered region filter. Run the focused suites with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.*'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_avatar.*' +com_frame_limit 10
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_save.round_trip_region_event_filter_function' +com_frame_limit 10
```

The tests use synthetic `UnitData` and headless JASS execution, so they do not require retail archives or a map launch.
