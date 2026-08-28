# JASS Native Coverage

This document tracks the Warcraft III JASS host callbacks registered by
`games/warcraft-3/game/api/api_module.c`. The bundled `game/common.txt` is the
signature authority. HiveWorkshop references are useful for observable editor
and map-script behavior, but do not replace the native declarations.

## Baseline

The registry currently contains 836 callbacks. A conservative source audit
classifies 476 as implemented and 360 as clear placeholders, for 56.9% overall
coverage. This count treats a callback as a placeholder only when it ignores its
arguments and unconditionally returns no value, zero, false, or a null handle.
A working `returns nothing` callback also returns zero at the C ABI boundary, so
raw `return 0` counts are not meaningful.

| Module | Registered | Implemented | Placeholder |
| --- | ---: | ---: | ---: |
| `api_misc.h` | 317 | 190 | 127 |
| `api_unit.h` | 144 | 82 | 62 |
| `api_player.h` | 86 | 37 | 49 |
| `api_trigger.h` | 48 | 26 | 22 |
| `api_camera.h` | 42 | 34 | 8 |
| `api_sound.h` | 35 | 7 | 28 |
| `api_leaderboard.h` | 27 | 2 | 25 |
| `api_math.h` | 26 | 18 | 8 |
| `api_group.h` | 25 | 18 | 7 |
| `api_quest.h` | 24 | 22 | 2 |
| `api_destructable.h` | 22 | 17 | 5 |
| `api_item.h` | 16 | 10 | 6 |
| `api_effect.h` | 13 | 2 | 11 |
| `api_cinefilter.h` | 10 | 10 | 0 |
| `api_test.h` | 1 | 1 | 0 |

The codebase already exceeds 50% of all registered callbacks. "Populate 50% of
the placeholders" is a different target: 180 of the 360 placeholders, yielding
656 implemented callbacks (78.5% overall). Recount whenever callbacks are added
to the registry or a placeholder begins consuming authoritative state.

Coverage is not conformance. Several callbacks consume state but still violate
their JASS contract and therefore need a `partial` status in any future generated
ledger. Known examples include:

- `GroupAddUnit` and `GroupRemoveUnit` are declared to return `boolean`, but the
  current callbacks push no return value.
- Several group enumeration callbacks collect matching edicts but ignore their
  `boolexpr` filter. Counted variants must apply the filter before decrementing
  the accepted-unit limit.
- `ForceEnumPlayers` and its variants do not yet evaluate filters.
- `GetPlayerSlotState` derives only empty/playing from W3I and cannot report the
  runtime `PLAYER_SLOT_STATE_LEFT` transition.
- `ForcePlayerStartLocation` changes the index but does not reserve it against
  random placement.

Use four statuses when maintaining the ledger: `implemented`, `partial`,
`placeholder`, and `unregistered`. A callback becomes implemented only when it
consumes every argument, returns the declared JASS type, owns handle lifetime,
and has a behavior-level test.

## State Ownership

Use three distinct lifetimes. Do not widen the networked `PLAYER` struct merely
because a JASS type is named `playerstate`.

| Lifetime | Owner | Examples |
| --- | --- | --- |
| Map setup | `level` or a mutable map-setup snapshot | game type, map flags, placement, speed, difficulty, density, start-location priorities |
| Player setup/runtime | `game.clients[]` and server-only WC3 client fields | controller, race preferences, tax rates, handicap, tech state |
| Client-visible runtime | `PLAYER ps` | resources, food, team, race, color, camera/UI state |

`war3map.w3i` remains authoritative initial data. The JASS `config()` function
reconstructs and may override map/player setup before `main()` starts. Setup
callbacks therefore need mutable per-level state initialized from `MAPINFO`;
casting away `level.mapinfo` constness is not the long-term ownership model.

## Map Configuration Contract

The following callbacks form one coherent setup subsystem:

- `SetMapName` and `SetMapDescription` replace map metadata for the current
  level. Their strings must outlive the JASS stack and be released at shutdown.
- `SetTeams` and `SetPlayers` set configured counts, clamped to map capacity.
  `GetTeams` and `GetPlayers` return those configured counts, not hardcoded
  engine maxima.
- `DefineStartLocation` and `DefineStartLocationLoc` set indexed map positions.
  The existing implementation writes `MAPINFO.players[].startingPosition`.
- `SetStartLocPrioCount` sizes the valid priority slots for one start location.
  `SetStartLocPrio` stores both the other start-location index and the
  `startlocprio` enum. The two getters return those exact values.
- `SetGameTypeSupported` toggles one bit independently. The selected game type
  is separate state returned by `GetGameTypeSelected`.
- `SetMapFlag` toggles one map flag independently; `IsMapFlagSet` tests it.
- Placement, speed, difficulty, and resource/creature density are enum-valued
  setup fields with direct setter/getter round trips.

`ForcePlayerStartLocation` is not merely an alias for
`SetPlayerStartLocation`: it also reserves that location so random placement
cannot assign it to another player.

The bundled declarations also expose three setup natives not currently present
in the registry: `SetEnemyStartLocPrioCount`, `SetEnemyStartLocPrio`, and
`SetPlayerName`. Add them when their owning setup state is implemented.

## Player Configuration Contract

- `SetPlayerRacePreference` updates a bitmask. `RACE_PREF_*` values can be
  combined; `IsPlayerRacePrefSet` tests membership rather than equality.
- `SetPlayerRaceSelectable` and `GetPlayerSelectable` are a boolean round trip.
- `SetPlayerController` and `GetPlayerController` store the configured
  `mapcontrol` value. Lobby choices may override the initial W3I controller.
- `SetPlayerTaxRate` stores a directional source-player to other-player rate,
  keyed by resource `playerstate`; `GetPlayerTaxRate` reads the same cell.
- `SetPlayerOnScoreScreen` controls score-screen inclusion only. It is not a
  generic connected/playing flag.
- `SetPlayerState` and `GetPlayerState` use `ps.stats[]` for the declared WC3
  player-state indices. Values needed only by server simulation should not be
  added to the network contract automatically.
- `GetPlayerSlotState` distinguishes empty, playing, and left. Deriving only
  empty/playing from W3I cannot represent a player that left at runtime.
- `SetPlayerHandicap` and `SetPlayerHandicapXP` are percentages represented by
  real values; their getters must return the stored values, not normalized
  fractions unless runtime evidence demonstrates that contract.
- Tech maximums, researched levels, and ability availability require keyed
  per-player tables. They should not be packed into `ps.stats[]`.

## Trigger Context Contract

Event response natives such as `GetTriggerPlayer`, `GetTriggerUnit`,
`GetAttacker`, and `GetEventPlayerState` read the active JASS execution context.
They are not global "last event" values. Registration callbacks create an event
subscription containing the trigger, subject/filter, event kind, and any limit
condition. Dispatch must install context before evaluating conditions/actions
and restore the previous context afterward so nested trigger execution works.

Trigger ownership is split deliberately:

- `TRIGGER` owns enabled/wait-on-sleep state plus condition and action lists.
- An event registration owns the trigger link, subject, event kind, filter, and
  limit condition. Destroying a trigger must detach or invalidate registrations.
- Evaluation and execution counts belong to `TRIGGER`; reset clears both counts
  and transient execution state without deleting registered actions.
- `TriggerSleepAction` yields the current JASS coroutine. `TriggerWaitOnSleeps`
  controls whether `TriggerExecuteWait` waits for yielded actions; it is not a
  global scheduler switch.
- Timer, game-state, player-state, and unit-state events need edge-aware limit
  checks. Polling a condition true every frame must not repeatedly fire unless
  Warcraft's event contract for that event says it should.

## Collections And Regions

Groups contain unit handles; forces contain player handles. Enumeration uses a
temporary JASS context so `GetFilterUnit` or `GetFilterPlayer` resolves to the
candidate being tested. `ForGroup`/`ForForce` similarly bind the enum handle for
the callback and restore the prior context afterward for nested enumeration.

Every `Group*OrderById` callback must resolve the numeric order through the same
order table used by the string variant, then call the existing unit order path.
The boolean result reports whether the group order was accepted according to
the native contract; it is not an unconditional success value.

A `REGION` is the union of its cells and rectangles. Add/clear operations mutate
that set, while `IsPointInRegion`, `IsLocationInRegion`, and `IsUnitInRegion`
query it. Enter/leave events require per-unit previous membership so crossing an
edge fires once; testing only current containment cannot distinguish entry from
remaining inside.

## Timers

A timer handle needs timeout, accumulated elapsed time, start time, periodic and
paused flags, and a handler. Timer time uses deterministic server game time, not
wall-clock time. Pausing freezes elapsed time; resuming preserves it; restarting
replaces the previous schedule. During expiry, `GetExpiredTimer` resolves from
the active JASS context and nested callbacks restore the previous value.

One scheduler should drive both timer handlers and timer-expire trigger events.
Periodic timers reschedule from their intended expiry to avoid frame-time drift.
Destroying a timer cancels pending work and invalidates event references without
leaving a scheduler pointer to freed JASS handle storage.

## Units, Items, And Destructables

Unit, item, destructable, and base `widget` handles resolve to edicts. Do not add
parallel object stores for JASS. Native families should dispatch through the
same spawn, order, movement, damage, inventory, and death paths used by gameplay
so trigger publication and snapshots remain consistent.

- Query callbacks read authoritative edict fields or SLK metadata. “Default”
  getters read immutable type data, while non-default getters read runtime state.
- Position setters must use the owning movement/linking function, not only write
  `s.origin`, so collision, pathing, visibility, and snapshots agree.
- Kill and remove are distinct: kill runs death behavior and events; remove
  releases the entity without fabricating a death.
- Item ownership is the inventory holder or explicit owning player defined by
  the item contract, not merely `edict.s.player` unless that field is kept in
  sync by every inventory transition.
- Item charges are mutable item-instance state. `GetItemCharges` and
  `SetItemCharges` read/write the carried or world item's edict state; setting
  charges on a carried item refreshes the selected-unit inventory layer.
- `widget` life operations share the damage/life representation across units,
  items, and destructables and clamp against the runtime maximum.

## Sound And Music

`sound` handles are game-owned descriptions containing asset identity, playback
parameters, attachment/position, and lifecycle state. Label constructors resolve
the WC3 sound-data tables; they must not treat the label itself as a filename.
The server emits sound commands/state through the existing sound architecture;
JASS callbacks must not call a client mixer directly.

`StartSound`, `StopSound`, attachment, 3D position, and playing/loading queries
must describe one coherent handle state machine. `killWhenDone` releases only
after playback completion, while an immediate destroyed/stopped handle cannot
remain queryable as playing. Music and thematic music are separate channels:
ending thematic music resumes or restores map music according to the stored
music state rather than starting an unrelated track.

## Leaderboards

A leaderboard is server-authored UI state: label, visibility, style, ordered
items, colors, and per-player assignment. Store model state in the game module
and serialize it through the existing layout/UI message path. Do not put
leaderboard geometry in C and do not make the client authoritative for values.

Each item owns a copied label, value, optional player handle, style bits, and
colors. Sort callbacks reorder items stably by the requested key and direction;
player lookup returns the current post-sort index. `PlayerSetLeaderboard` is a
per-player association, so displaying one board need not expose it to every
client.

## Implementation Order

1. Map and player configuration: establishes ownership and setter/getter tests.
2. Groups, forces, regions, and trigger context: reusable collection/event
   machinery required by generated map scripts.
3. Unit and item queries/mutation: use edicts and existing order/combat paths.
4. Timers and sounds: require scheduler and sound-handle lifecycles.
5. Presentation-only effects, leaderboards, and remaining camera callbacks.

For each family, add VM-level round-trip tests in `game/tests/t_jass_map.c` and
state-level invariants in `game/tests/t_api.c`. Test both the positive and
inverse path for flags, masks, filters, and cache/state transitions. Warcraft III
changes must be verified against ROC and TFT archives.

## HiveWorkshop References

These references were verified during the baseline audit. They supplement the
bundled declarations with observable map/editor behavior:

- [Start-location count and map setup](https://www.hiveworkshop.com/threads/function-wanted-gettotalstartlocations.187750/post-1822897)
- [Player capacity remains map/W3I-version dependent](https://www.hiveworkshop.com/threads/success-hybrid-12-24-player-map-backwards-compatible-1-24-1-28-5-1-31.339722/)
- [JASS-driven player/controller setup](https://www.hiveworkshop.com/threads/map-script-independent-jass-driven-ai-experiment.370776/post-3713000)
- [Fixed player settings and color behavior](https://www.hiveworkshop.com/threads/preparing-menu-color-bug.312129/)
- [Start-location priority behavior](https://www.hiveworkshop.com/threads/start-location.273991/post-2770678)
