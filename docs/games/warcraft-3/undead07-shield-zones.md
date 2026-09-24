# Undead07 Shield Zones

## Contract

`Undead07.j` filters shield-zone entrants by owner and Undead race/type, adds accepted units to a JASS group, then applies life loss and vertex tint from periodic callbacks. Leaving the region removes a unit from that group. Shield shutdown is registered on each Archmage's life reaching zero; the script then changes the shield doodads to their death animation.

The map reads race from the authored `UnitData.race` column. `GetUnitRace` maps authored race strings to the JASS values: Human 1, Orc 2, Undead 3, Night Elf 4, Demon 5, Other 7, Creeps 8, Commoner 9, Critters 10, and Naga 11. Value 6 is unused; an unknown race returns the null race value 0. These assignments were checked against a Warcraft III test-map enumeration and game.dll analysis ([race-value report](https://www.hiveworkshop.com/threads/map-script-independent-jass-driven-ai-experiment.370776/)); Warsmash's `CUnitRace` enum identifies authored race names but its ordinal values are a separate internal representation.

Region data occupies a fixed `level.regions` slot. JASS handles encode the slot and a generation, so `RemoveRegion` can reuse that storage while old handles remain inert and distinct. It also retires region event registrations and cancels their unread queued events; event handles carry a generation so a reused handler slot cannot alias an old event handle. Save/load persists live and retired slot generations, preserving `GetHandleId` identity while stale JASS references restore as null. Rectangle changes made after registration remain visible to crossings. The registry and event reference are saved by slot ID. Crossing evaluates the optional filter with `GetFilterUnit()` bound to the crossing unit before publishing the event. Trigger actions receive the registered region through `GetTriggeringRegion()` and the crossing unit through `GetEnteringUnit()` or `GetLeavingUnit()`.

## Confirmed Failure And Fix

The captured `openwarcraft3.log` showed three missing native paths:

- `GetUnitRace` returned a null handle and `IsUnitType` did not recognize `UNIT_TYPE_UNDEAD`, so the shield entry filter rejected units.
- `TriggerRegisterUnitStateEvent` returned null, so the first two Archmage life-limit shutdown triggers were never installed.
- `TriggerRegisterLeaveRegion` returned null, so units could remain in the periodic shield group after leaving.

The existing `EVENT_GAME_STATE_LIMIT` event carries the registered unit as its subject and retains the limit operator/value. `G_SetHealth` publishes the matching response only when life crosses into the registered condition, allowing the standard trigger dispatch to run the map action. Runtime health changes, including Avatar's temporary health delta and expiry clamp, pass through `G_SetHealth`. Unit state event registration currently supports `UNIT_STATE_LIFE`; unsupported states and limit operators are reported and return a null event.

Region event filters are JASS function references in the saved event registration. Save/load persists them by function name, matching trigger actions and conditions. After a filter returns, crossing dispatch revalidates the event generation and region before queueing its response; a filter that removes its region and registers another event cannot send the old crossing to a recycled slot. Save format version 40 added region registry slots and event region references; version 41 persisted region and region-event handle generations; version 42 added unit-bound registration incarnations; versions 43 and 44 added queued-event subject and source incarnations. The current format is version 44; older versions are rejected before decoding. See [WC3 Save/Load](save-load.md) for the full version history.

## Verification

`games/warcraft-3/game/tests/t_api.c` covers JASS `GetUnitRace` and `IsUnitType` for authored Undead data, the authored race-to-JASS enum table, unit life threshold crossing, filtered enter/leave events driven by real unit movement through `G_TouchTriggers`, stale-handle isolation and distinct `GetHandleId` values after `RemoveRegion`, and repeated create/remove cycles beyond the old lifetime cap. The enter test registers an empty region, adds its rectangle afterward, and checks `GetTriggeringRegion()` in the action. `games/warcraft-3/game/tests/t_avatar.c` covers threshold events crossed by Avatar's health increase and expiry. `games/warcraft-3/game/tests/t_game.c` covers save/load of a registered region, its rectangles, filter function, and `GetHandleId` identity. `g_save.c` verifies version-39 save files are rejected. Run the focused suites with:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.*'
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_avatar.*' +com_frame_limit 10
build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'wc3_save.round_trip_region_event_filter_function' +com_frame_limit 10
```

The tests use synthetic `UnitData` and headless JASS execution, so they do not require retail archives or a map launch.
