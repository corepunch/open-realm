# JASS Native Coverage

This document tracks the Warcraft III JASS host callbacks registered by
`games/warcraft-3/game/api/api_module.c`. The bundled `game/common.txt` is the
signature authority. HiveWorkshop references are useful for observable editor
and map-script behavior, but do not replace the native declarations.

`TimerStart(timer, timeout, periodic, null)` is legal and starts or resets the
timer without an expiration callback. `TimerStart` must detect that null before
calling strict `jass_checkcode()`; non-null handlers still require the exact
`code` type. Blizzard's Human02 startup path uses this during cutscene
fast-forward or ESC skip. HiveWorkshop's timer reset examples confirm the same
native behavior, commonly followed by `PauseTimer` when resetting getter state.

## Baseline

The registry currently contains 917 callbacks. The last conservative source
audit snapshot now classifies 499 as implemented and 337 as clear placeholders
across 836 callbacks (59.7% overall at that snapshot). Newer registrations,
including `GetUnitAbilityLevel`, are not folded into the implementation split
below yet. This count treats a callback as a placeholder only when it ignores its
arguments and unconditionally returns no value, zero, false, or a null handle.
A working `returns nothing` callback also returns zero at the C ABI boundary, so
raw `return 0` counts are not meaningful.

| Module | Registered | Implemented | Placeholder |
| --- | ---: | ---: | ---: |
| `api_misc.h` | 317 | 190 | 127 |
| `api_unit.h` | 144 | 83 | 61 |
| `api_player.h` | 86 | 43 | 43 |
| `api_trigger.h` | 48 | 26 | 22 |
| `api_camera.h` | 42 | 39 | 3 |
| `api_sound.h` | 35 | 10 | 25 |
| `api_leaderboard.h` | 27 | 2 | 25 |
| `api_math.h` | 26 | 18 | 8 |
| `api_group.h` | 25 | 18 | 7 |
| `api_quest.h` | 24 | 22 | 2 |
| `api_destructable.h` | 22 | 17 | 5 |
| `api_item.h` | 16 | 10 | 6 |
| `api_effect.h` | 13 | 10 | 3 |
| `api_cinefilter.h` | 10 | 10 | 0 |
| `api_test.h` | 1 | 1 | 0 |

The codebase already exceeds 50% of all registered callbacks. "Populate 50% of
the original placeholder baseline" is a different target: 180 of the original
360 placeholders, yielding 656 implemented callbacks (78.5% overall). Recount whenever callbacks are added
to the registry or a placeholder begins consuming authoritative state.

Coverage is not conformance. Several callbacks consume state but still violate
their JASS contract and therefore need a `partial` status in any future generated
ledger. The effect module now has functional independent handles for
`AddSpecialEffect*`, `AddSpellEffect*`, and `DestroyEffect`; its three weather
effect callbacks remain placeholders. Target effects currently implement only
the `overhead` attachment specially, and `LIGHTNING` spell effects remain
unsupported, so those paths are partial rather than proof of full retail
conformance. See [Ability, Buff, And Item Presentation Effects](ability-and-item-effects.md).

Known examples include:

- `GroupAddUnit` and `GroupRemoveUnit` are declared to return `boolean`, but the
  current callbacks push no return value.
- Several group enumeration callbacks collect matching edicts but ignore their
  `boolexpr` filter. Counted variants must apply the filter before decrementing
  the accepted-unit limit.
- `ForceEnumPlayers` and its variants do not yet evaluate filters.
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
| Client-visible runtime | `PLAYER ps` | resources, food, team, race, color, camera target/bounds/Z/clip planes, UI state |

`war3map.w3i` remains authoritative initial data. The JASS `config()` function
reconstructs and may override map/player setup before `main()` starts. Setup
callbacks therefore need mutable per-level state initialized from `MAPINFO`;
casting away `level.mapinfo` constness is not the long-term ownership model.

Camera state is client-visible runtime state rather than mutable map metadata: `level.camera_bounds` is initialized from W3I and `SetCameraBounds` replaces that one map-global rectangle. Snapshot fields carry target Z offset and near/far clipping planes; `G_RunClients()` always writes `ps.znear`/`ps.zfar` from the current or lerped `CAMERASETUP`, and `CreateCameraSetup` defaults both clip planes so an Apply cannot send a zero far plane. The client copies those samples with no keep-previous fallback. The client clamps predicted targets with `CM_GetWorldBounds()`. `GetCameraMargin` is not a direct read of the W3I complement integers: it returns the geometric inset between the complement-derived playable rectangle and the W3I default camera rectangle. This distinction matters because World Editor generated `SetCameraBounds` calls use playable-edge constants plus/minus `GetCameraMargin`.

## Player Result / End-Game Lifecycle

`RemovePlayer` now consumes all four `PLAYER_GAME_RESULT_*` values, stores the result in
`PLAYER_STATE_GAME_RESULT`, marks the runtime player slot `PLAYER_SLOT_STATE_LEFT`, stops that player's bot, and
publishes `EVENT_PLAYER_VICTORY` / `EVENT_PLAYER_DEFEAT` for the two corresponding results. Repeated removal is
idempotent. `TIE` and `NEUTRAL` record the result and slot transition without inventing victory/defeat events.

The native FDF `GameResultDialog` remains a temporary compatibility fallback while the generic ScriptDialog native
family is incomplete. Result presentation is queued by `RemovePlayer` and normally flushed after JASS result events;
if the player is in `CLIENT_UI_CINEMATIC`, the fallback normally waits until cinematic UI returns to gameplay. Stock
Blizzard.j single-player result dialogs are the exception: `CustomVictoryBJ` / `CustomDefeatBJ` call `RemovePlayer`
and then `PauseGame(true)`. A result event published from that late JASS action must be drained before the pause can
stop future simulation frames, and the fallback may then display over cinematic UI because the paused scheduler cannot
advance that UI state. Result presentation now uses the same client-managed `svc_window` path as the other in-game
menus, so it draws above the still-suppressed cinematic HUD without changing `playerState_t.uiflags`. The window is
modal/unique, does not acquire a second client-owned pause, and cannot be dismissed with Escape; its buttons close the
window while forwarding the existing result commands. `GameResultBackdrop` reuses the loaded Esc-menu backdrop art
for a consistent race-skinned menu panel and extends one action row below the nominal dialog bounds so the authored
Quit button remains inside that panel. The fallback still uses `GlobalStrings.fdf`
`GAMEOVER_*` labels where available and only exposes actions the current engine can execute.

`EndGame`, `ChangeLevel`, `RestartGame`, `DisplayLoadDialog`, and `ForceCampaignSelectScreen` now cross the existing
`gi.MenuAction` session boundary. `EndGame` returns the local client to the frontend, `ChangeLevel` loads the requested
map, `RestartGame` reloads the current `map` cvar, `DisplayLoadDialog` enters the frontend load-game screen, and
`ForceCampaignSelectScreen` returns to campaign selection. The `doScoreScreen` parameter is consumed but score-screen
presentation is not implemented yet.

`GetDefaultDifficulty` / `SetDefaultDifficulty` now own a per-level default distinct from mutable
`GetGameDifficulty()` state. Campaign map startup seeds both values from `wc3_campaign_difficulty`; scripts may then
change current and default difficulty independently.

Still incomplete: the generic `Dialog*` / dialog-button event natives, `DialogAddQuitButton`, actual score-screen
presentation, Reduce Difficulty/observer-on-death result policy, and result-dialog ownership of single-player modal
pausing. The existing `PauseGame` / modal path should be reused for that work rather than adding a second pause model.

## Campaign Game Cache

Campaign carry-over uses a `gameCache_t` JASS handle plus a committed
process-level snapshot. `Store*` mutates the handle and `SaveGameCache` commits
it. `wc3_gamecache_mode` defaults to `memory`, which carries saved state between
maps in the same process without disk I/O; `disk` additionally loads/writes the
OpenRealm sidecar for cross-process persistence. Scalar typed values support
store/get/have/flush, while `StoreUnit` snapshots the unit rawcode, Hero
progression (including unspent skill points and learned ranks), health/mana,
unit colour, and inventory IDs/charges. `RestoreUnit` creates a fresh unit for
the requested player and reapplies that snapshot. See
[campaign-game-cache.md](campaign-game-cache.md) for the lifecycle and private
on-disk format.

The `SyncStored*` callbacks remain **partial**: they validate/consume their
arguments but do not implement multiplayer synchronization.
`ReloadGameCachesFromDisk` remains unregistered.

## Rally getters

`GetUnitRallyPoint`, `GetUnitRallyUnit`, and `GetUnitRallyDestructable` are registered against the producer-owned rally state documented in [rally-points.md](rally-points.md). The point getter resolves current widget coordinates; the typed widget getters return only matching unit/destructable targets. String-based `setrally` orders are implemented. Numeric order-ID parity remains partial because the engine still lacks the general Warcraft order-ID table.

## Runtime Error Reporting

`jass_rterror()` records the error on the root VM and aborts the current
coroutine. Reporting belongs to `JASSHOST.RuntimeError`; `jass_sethost()` uses
the standard `JASS runtime error:` stderr reporter when the host omits it.

In engine tests, `run_test_jass_error(source, expected)` installs a silent host
reporter and compares the recorded error with `expected`. Use it for deliberate
negative paths so expected failures do not resemble test-run failures. Ordinary
`run_test_jass()` calls print captured errors as `JASS test error:` before
returning false.

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
- Tech maximums and researched levels use the keyed `game.clients[].tech` table rather than `ps.stats[]`. `SetPlayerTechMaxAllowed`, `GetPlayerTechMaxAllowed`, `AddPlayerTechResearched`, `SetPlayerTechResearched`, `GetPlayerTechResearched`, and `GetPlayerTechCount` round-trip through that state. `-1` is preserved as the unlimited maximum sentinel; non-negative values are exact maxima. W3I technology-unavailability entries seed a maximum of zero before map scripts may override it. Build and train command availability consume the same state, and mutations invalidate the owning player's command card for deferred refresh after simulation. The table is bounded by `MAX_PLAYER_TECH_STATE` and reports exhaustion rather than silently overwriting state. The bundled `game/common.txt` declares `GetPlayerTechResearched` as `boolean`, so it reports whether the exact rawcode has a researched level above zero; `GetPlayerTechCount` returns the exact-rawcode count/level. Both currently ignore technology-equivalence expansion when `specificonly` is false because equivalence groups are not represented yet. Ability availability remains separate work.
- `GetPlayerStructureCount` scans live owned unit edicts classified by `G_UnitIsBuilding`. `includeIncomplete=false` excludes `construction.active` structures; dead/zero-life structures never count. Campaign defeat conditions may query this native from an ordinary unit-death trigger, so the result is derived from all surviving structures rather than from the triggering unit. `GetPlayerUnitCount` and `GetPlayerTypedUnitCount` remain separate coverage work.

## Time Of Day Game State

`GAME_STATE_TIME_OF_DAY` is the sole `fgamestate` declared by the bundled `common.txt`. `SetFloatGameState` and
`GetFloatGameState` now round-trip through the server-owned Warcraft daily cycle, and `SuspendTimeOfDay` freezes only ordinary clock
progression. Explicit sets are deferred until the next time-of-day update and still apply while suspended.

`TriggerRegisterGameStateEvent` is **partial**: registrations for `GAME_STATE_TIME_OF_DAY` store the `limitop` and threshold and fire
once on a false-to-true transition; integer game-state limit events are not implemented yet. The server-authored
`TimeOfDayIndicator` now binds to the replicated normalized day phase and the WC3 MDX renderer scrubs its selected sequence with an
explicit `@ratio`. `SetDayNightModels` now registers the map-authored terrain/unit DNC models, republishes their model indices to
the client, and the WC3 renderer samples sequence 0 at the same normalized phase to use each model's first light as the corresponding
base world light. `SetTimeOfDayScale` and `GetTimeOfDayScale` remain placeholders because the inspected Warsmash source does not
provide a behavior to mirror. See [time-of-day.md](time-of-day.md) for ownership, `Misc` fields, gameplay consumers, HUD
synchronization, DNC rendering, and the remaining visual-lighting gaps.

## Trigger Context Contract

Event response natives such as `GetTriggerPlayer`, `GetTriggerUnit`,
`GetAttacker`, and `GetEventPlayerState` read the active JASS execution context.
They are not global "last event" values. Registration callbacks create an event
subscription containing the trigger, subject/filter, event kind, and any limit
condition. Dispatch must install context before evaluating conditions/actions
and restore the previous context afterward so nested trigger execution works.

The event-response player and the `GetLocalPlayer()` selector are separate VM
state. `JASSCONTEXT.playerState` carries the event/filter/AI player consumed by
`GetTriggerPlayer()` and `GetFilterPlayer()`. `JASSCONTEXT.localPlayerState`
carries only the local presentation selector exposed through the VM-global
`currentplayer`. An event fired by a player-4 unit must therefore be able to
report `GetTriggerPlayer() == Player(4)` while a nested `TriggerExecute()` still
fans out `if GetLocalPlayer() == Player(1)` and applies UI/camera/audio work to
map player 1. Nested coroutines inherit both values independently. Never seed
`currentplayer` from the triggering unit owner; doing so turns local-player
branches into event-owner branches and silently suppresses campaign cinematics.

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

## Global Pause

`PauseGame(flag)` is wired through the WC3 game module to the generic server scheduler pause. The server freezes `sv.time` / simulation frames while continuing network reads and client traffic. Pause sources are combined in game code so closing a Quest modal cannot accidentally clear a script-owned `PauseGame(true)`. Quest-driven pausing is restricted to single-client sessions. See [Pause And Modal UI](pause-and-modal-ui.md).

This is distinct from `PauseUnit`, `PauseCompAI`, and timer pause state. `PauseTimer` / `ResumeTimer` remain unimplemented object-level timer natives.

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

Hero skill progression is documented separately in
[Hero Ability Progression](hero-abilities.md). `SelectHeroSkill` routes through
the same candidate/point/level/max-rank validation as the in-game skill menu,
`SetHeroLevel` routes through the XP level transition, and
`GetUnitAbilityLevel` reads the runtime learned rank. Generic runtime ability
addition/removal/level mutation remains separate work because OpenRealm does
not yet own a general per-unit dynamic ability collection.

## Sound And Music

`sound` handles are game-owned descriptions containing asset identity, playback
parameters, attachment/position, and lifecycle state. Label constructors resolve
the WC3 sound-data tables; they must not treat the label itself as a filename.
The server emits sound commands/state through the existing sound architecture;
JASS callbacks must not call a client mixer directly.

Triggered transmissions reuse this sound path but remain presentation state,
not cinematic-mode policy. `StartSound` preserves `GetLocalPlayer()` context so
force-gated Blizzard.j transmissions send speech only to the represented local
player; global calls still broadcast. See
[triggered-dialogue.md](triggered-dialogue.md) for the gameplay portrait/message
layer split, independent voice/scene lifetimes, and remaining ping/indicator gaps.

`CreateSound`, `SetSoundVolume`, `SetSoundPosition`, `AttachSoundToUnit`, and `StartSound` currently cover the one-shot subset used by campaign cinematics. Position/attachment is sampled when playback begins and transported through generic `svc_sound`; moving attached emitters, `StopSound`/fade lifecycle, playing/loading queries, pitch/cone/distance controls, volume groups, and music remain incomplete. `killWhenDone` eventually needs to release only after playback completion, while an immediate destroyed/stopped handle cannot remain queryable as playing. Music and thematic music are separate channels: ending thematic music resumes or restores map music according to the stored music state rather than starting an unrelated track. The current mixer is WAV-only, so MP3 campaign dialogue remains a separate decoder/transport gap.

Summon event context uses the summoner as the trigger unit and the created unit as the event source: `GetSummoningUnit()` reads the former and `GetSummonedUnit()` the latter. Mirror Image (`AOmi`) uses the common no-target spell path, creates the data-defined number of timed copies, marks them as illusions for `IsUnitIllusion`, and publishes both player-unit and unit summon events. Damage multipliers, dispel interaction, image shuffle, and full illusion visual semantics remain separate parity work.

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
