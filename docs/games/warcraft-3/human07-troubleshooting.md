# Human07 Mission Troubleshooting

## Scope

This document records the Human07 (`Maps\\Campaign\\Human07.w3m`) lifecycle findings: the
mission-end check, `SuicidePlayer`, runtime-created buildings, the Town Hall Birth
regression, and the evidence used to distinguish the failures. It separates confirmed
engine fixes from observations that still require a complete in-game replay.

The map script is authoritative for the mission objective. Engine behavior must make
the script's queries and lifecycle operations observable; it must not replace the
script with a Human07-specific completion rule.

## Authoritative data and runtime path

The map was extracted from the local Warcraft III archive with:

```text
get +map Maps\\Campaign\\Human07.w3m
```

The relevant generated `war3map.j` code is `CheckGreenBuildings`. It counts the
qualifying Player 6 structures and explicitly excludes the rawcodes `uzg1` and
`uzig`. The optional Crypt handle `gg_unit_usep_0049` is removed by the map script on
Easy and Normal. Those are authored exceptions, not a general rule that all neutral
or unfinished structures should be ignored.

The runtime path for a JASS-created building is:

```text
war3map.j CreateUnit
  -> unit_create
  -> SP_SpawnAtLocationNoBirth
  -> SP_CallSpawn
  -> SP_SpawnUnit
  -> collision initialization
  -> gi.LinkEntity
  -> server broad-phase bounds / BoxEdicts
```

The no-birth path is specific to immediate JASS `CreateUnit`. Construction, training,
and callers that own a Birth or construction lifecycle continue to use
`SP_SpawnAtLocation()`.

## Problems and solutions

| Problem | Confirmed cause | Solution |
| --- | --- | --- |
| Human07 did not finish after the visible enemy force was destroyed | `RemoveUnit` freed the edict while JASS trigger/group/entity iteration was still using the handle. The map's objective query could therefore observe stale or invalid state. | Hide the unit immediately, remove it from JASS groups and command state, then defer `G_FreeEdict` until the entity iteration has completed. `G_RunDeferredFrees()` runs after collision solving. |
| The campaign script could not launch its authored assault | OpenRealm did not expose the `SuicidePlayer` AI native used by the campaign AI path. | Implement and register `G_BotSuicidePlayer`: validate the AI player, target player, captain, and `check_full`; issue attack orders toward the target player's authored start location; launch live captain members and update captain state. |
| A runtime-created building could be walked through | `SP_SpawnAtLocationInternal()` linked the entity before `SP_CallSpawn()`. `SP_SpawnUnit()` populated building collision during spawn, but `SV_LinkEntity()` had already calculated zero-sized broad-phase bounds. `BoxEdicts()` consequently could not return the building as a blocker. | Call `SP_CallSpawn()` first and link afterward. This preserves the authored collision in the server spatial index. |
| The Town Hall could remain on the Birth model or retain a birth delay | `unit_create()` used the presentation-aware Birth path and then immediately requested Stand. Stand changed the animation but did not clear `edict.wait`, which still contained the authored build time. | Use `SP_SpawnAtLocationNoBirth()` in `unit_create()`, request Stand once, apply facing, and activate food. Immediate JASS creation now starts ready. |
| The deferred replacement Town Hall could render as an unselectable construction-shaped model | A replacement entity could receive an entity delta without an explicit scale. The client baseline and interpolated render state then treated the omitted scale as zero, collapsing the model and its picking shape. | Seed client baselines with scale `1.0` and clamp interpolated non-positive scales to `1.0` in `V_AddClientEntity()`. This preserves the normal model and selection behavior when deferred-release entities are replaced. |

The map-placed spawn path already called `SP_CallSpawn()` before linking. The
link-order defect therefore affected dynamically created units, not ordinary
map-placed entities.

## Implementation locations

- [`g_spawn.c`](../../../games/warcraft-3/game/g_spawn.c) owns the post-spawn link
  ordering and the Birth/no-Birth spawn entry points.
- [`m_unit.c`](../../../games/warcraft-3/game/m_unit.c) owns the JASS `CreateUnit`
  immediate-ready path.
- [`g_utils.c`](../../../games/warcraft-3/game/g_utils.c) owns deferred edict
  retirement; [`g_main.c`](../../../games/warcraft-3/game/g_main.c) drains it after
  the frame's entity iteration and collision solve.
- [`api_group.h`](../../../games/warcraft-3/game/api/api_group.h) keeps deferred
  removals out of JASS unit enumerations while their edicts remain live for the
  rest of the frame.
- [`g_bot.c`](../../../games/warcraft-3/game/g_bot.c), [`api_ai.h`](../../../games/warcraft-3/game/api/api_ai.h),
  and [`api_module.c`](../../../games/warcraft-3/game/api/api_module.c) own the
  `SuicidePlayer` native implementation and registration.
- [`cl_parse.c`](../../../client/cl_parse.c) seeds entity baseline scale and
  [`cl_view.c`](../../../client/cl_view.c) protects interpolated render scale so
  deferred replacement entities retain their normal presentation and picking.

## Confirmed fixes

The following evidence is reproducible in the current tree:

- `wc3_api.removeunit_hides_before_deferred_edict_release`,
  `wc3_api.createunit_does_not_reuse_deferred_dead_unit`, and
  `wc3_api.killunit_processes_deferred_unit_handle` cover deferred unit removal and
  stale-handle behavior.
- `wc3_api.createunit_starts_ready_without_birth_delay` passes in both Classic and
  TFT data modes. It verifies Stand and `wait == 0` for an immediate `CreateUnit`.
- `wc3_api.createunit_links_building_collision_bounds` passes in both Classic and
  TFT data modes. It verifies collision-sized linked bounds and that `BoxEdicts()`
  returns the runtime-created building.
- `wc3_api.human04_intro_cancel_preserves_unit_lifecycle_until_frame_end` reproduces
  the Human04 cancellation order: a temporary `ugol` is killed, the worker/building
  handles are removed, deferred state is checked before the drain, and a replacement
  building is selected afterward.
- `wc3_jass_map.human04_cancel_replaces_townhall_after_difficulty_removal` follows
  the extracted Human04 `war3map.j` order: Normal difficulty removes
  `gg_unit_usep_0087`, `EVENT_PLAYER_END_CINEMATIC` dispatches the cancellation
  trigger, and that trigger removes the old building before creating a replacement
  `htow`. It counts buildings from inside the cancellation callback and verifies the
  replacement remains selectable while both removed handles await deferred release.
  The client baseline/render-scale fix is required for the same replacement to remain
  visible and selectable in the live renderer; it is not controlled by a runtime cvar.
- `wc3_jass_map.human07_normal_removal_is_absent_from_green_building_count` follows
  Human07's Normal-difficulty `usep` removal and its `CheckGreenBuildings` query;
  it verifies the Crypt handle remains deferred but is absent from the count.
- The focused API tests passed 4/4 for the no-birth lifecycle and 11/11 for the
  building-link contract in each data mode.
- `make test-wc3-engine` passed `25784/25784` assertions in `1235` tests in both
  Classic and TFT modes.
- `make -B openwarcraft3` completed successfully and `git diff --check` reported no
  whitespace errors.

The lifecycle regression tests are in
[`games/warcraft-3/game/tests/t_api.c`](../../../games/warcraft-3/game/tests/t_api.c)
and
[`games/warcraft-3/game/tests/t_jass_map.c`](../../../games/warcraft-3/game/tests/t_jass_map.c);
the AI behavior tests are in
[`games/warcraft-3/game/tests/t_bot.c`](../../../games/warcraft-3/game/tests/t_bot.c).

## Confirmed regressions

These were real behavior regressions introduced by intermediate fixes and are now
covered or corrected:

1. Replacing `CreateUnit`'s Birth path with an immediate Stand request without also
   clearing the birth wait caused a Town Hall to appear stuck at Birth. The corrected
   no-birth path and `createunit_starts_ready_without_birth_delay` test address it.
2. Linking a spawned building before its class spawn initializer caused the building
   to be visually present but absent from broad-phase collision. The corrected link
   order and `createunit_links_building_collision_bounds` test address it.

## Warsmash comparison and remaining gap

The Warsmash sources in `screenshots/WarsmashModEngine` distinguish pure simulation
creation from presentation/construction creation:

- JASS `CreateUnit` reaches an immediate-ready creation path.
- A simple summoned unit may use Birth followed by a queued Stand.
- A building construction path explicitly enters constructing state and owns the
  Birth presentation.

OpenRealm now matches the important `CreateUnit` and building-construction ownership
boundary. The summon path still deserves a separate parity test because
`s_summon.c` uses the presentation-aware spawn path and then requests Stand directly;
that is a potential lifecycle difference, but no confirmed regression from it has
been recorded.

## Verification commands

Run focused tests independently when diagnosing either lifecycle issue:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.createunit_starts_ready_without_birth_delay'
make test-wc3-engine WC3_PATTERN='wc3_api.createunit_links_building_collision_bounds'
make test-wc3-engine WC3_PATTERN='wc3_api.human04_intro_cancel_preserves_unit_lifecycle_until_frame_end'
make test-wc3-engine WC3_PATTERN='wc3_jass_map.human04_cancel_replaces_townhall_after_difficulty_removal'
make test-wc3-engine WC3_PATTERN='wc3_jass_map.human07_normal_removal_is_absent_from_green_building_count'
make test-wc3-engine
```

The automated tests prove state, handle, and spatial-index contracts. A complete
Human07 replay remains useful for validating the final camera, trigger, and campaign
transition presentation because those visual effects are outside the focused harness.
