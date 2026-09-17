# JASS Native Coverage

This document tracks the Warcraft III JASS host callbacks registered by
`games/warcraft-3/game/api/api_module.c`. The bundled `game/common.txt` is the
signature authority. HiveWorkshop references are useful for observable editor
and map-script behavior, but do not replace the native declarations.

Cooldown control now registers `UnitResetCooldown`, `BlzStartUnitAbilityCooldown`,
`BlzEndUnitAbilityCooldown`, and `BlzGetUnitAbilityCooldownRemaining`. They use the
same normalized per-unit cooldown records as normal casts. `BlzGetAbilityCooldown`,
`BlzSetUnitAbilityCooldown`, and `BlzGetUnitAbilityCooldown` remain unimplemented
because OpenRealm does not yet have per-unit mutable ability-field overrides. See
[Ability Cooldowns](ability-cooldowns.md).

`TimerStart(timer, timeout, periodic, null)` is legal and starts or resets the
timer without an expiration callback. `TimerStart` must detect that null before
calling strict `jass_checkcode()`; non-null handlers still require the exact
`code` type. Blizzard's Human02 startup path uses this during cutscene
fast-forward or ESC skip. HiveWorkshop's timer reset examples confirm the same
native behavior, commonly followed by `PauseTimer` when resetting getter state.

## Baseline

The registry currently contains 917 callbacks. The last conservative source
audit snapshot now classifies 502 as implemented and 334 as clear placeholders
across 836 callbacks (60.0% overall at that snapshot). Newer registrations,
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
| `api_leaderboard.h` | 27 | 27 | 0 |
| `api_math.h` | 26 | 18 | 8 |
| `api_group.h` | 25 | 18 | 7 |
| `api_quest.h` | 24 | 22 | 2 |
| `api_destructable.h` | 22 | 17 | 5 |
| `api_item.h` | 16 | 10 | 6 |
| `api_effect.h` | 13 | 13 | 0 |
| `api_cinefilter.h` | 10 | 10 | 0 |
| `api_test.h` | 1 | 1 | 0 |

The codebase already exceeds 50% of all registered callbacks. "Populate 50% of
the original placeholder baseline" is a different target: 180 of the original
360 placeholders, yielding 656 implemented callbacks (78.5% overall). Recount whenever callbacks are added
to the registry or a placeholder begins consuming authoritative state.

Issue #418 campaign-audit JASS native families now implemented: `SetCaptainHome`, `SetStagePoint`, `SuicideUnit`, `SuicideUnitEx`, `SuicidePlayer`, `MergeUnits`, `GetUpgradeGoldCost`, `GetUpgradeLumberCost` (all in `api_ai.h`), `EnumItemsInRect` / `GetEnumItem` (items in rect, mirrors `EnumDestructablesInRect`; boolexpr filter TODO), `GetChangingUnit` / `GetChangingUnitPrevOwner` (ownership-change event context, eventValue = prev_owner+1), `SetUnitUserData` / `GetUnitUserData` / `UnitSetUsesAltIcon` (unit state fields). Stubs added: `SetCampaignMenuRaceEx`, `SetAltMinimapIcon`, `DoNotSaveReplay`. Bot AI signatures for `SetCaptainHome` and `SuicideUnit*` take the player as explicit arg 1 (not from context); scripts call `GetAiPlayer()` to supply it. `EnumItemsInRect` arg 3 is the actionFunc; arg 2 (boolexpr) is skipped (TODO). `currentenumitem` is defined alongside `G_IsItem` in `g_items.c`.

`AddIndicator` and `UnitAddIndicator` are implemented as local transient presentation rather than widget state.
The game sends the widget entity number plus clamped RGBA to each applicable client, which reuses the ordinary
terrain-conforming selection circle for two flashes without changing authoritative selection membership. This also
covers Blizzard.j's `UnitAddIndicatorBJ`, whose wrapper calls the generic `AddIndicator` native with a unit widget.

`SetUnitColor` now publishes a per-unit replaceable team-color override without changing unit ownership. The override is packed into unused `effect_flags` presentation bits as `playercolor + 1`, so `PLAYER_COLOR_RED` remains distinct from the zero/default owner-color state. This mirrors Warsmash's `RenderUnit.setPlayerColor` behavior and allows campaign scripts such as Prologue01 to give completed Circle of Power checkpoints their authored neutral-passive look.

`SetUnitVertexColor` now retains a persistent clamped RGBA override in WC3 game state and publishes visible overrides through the existing per-frame game datagram rather than widening `entityState_t`. The client keeps an explicit-validity tint cache so alpha `0` is distinct from the renderer's historical "unset tint" sentinel, then the MDX renderer reuses its existing per-instance geoset-colour multiply and translucent-layer path. `SetWaterBaseColor`, general JASS text tags, authored `UnitUI.slk` RGB defaults, and invisibility-alpha parity remain separate gaps. See [Unit Vertex Color](unit-vertex-color.md).

`SetTerrainFogEx` and `ResetTerrainFog` now own persistent WC3 environmental distance-fog state and publish it through the generic `CS_SCENE_FOG` presentation contract. `SetTerrainFogEx` retains style/start/end/density/RGB, while `ResetTerrainFog` restores the merged `[DefaultZFog]` RoC/TFT row. The renderer currently uses Warsmash-compatible linear start/end blending for every enabled style; legacy `SetTerrainFog`, true exponential equations, and particle fog remain separate gaps. See [Environmental Terrain Fog](environmental-fog.md).

Coverage is not conformance. `SetUnitAnimation` now resolves the unit type's Required Animation Names and
`AddUnitAnimationProperties` mutates the same per-unit tag set before reselecting the logical animation; see
[Required Animation Names](unit-animation-properties.md). `SetUnitAnimationByIndex`, rarity selection, and queued
animation semantics remain separate gaps. `SetDoodadAnimation` and `SetDoodadAnimationRect` now select matching
map-scenery edicts by rawcode and area, play authored MDX sequences, honor `nearestOnly`, support numbered random
variants, and implement the retail `show`/`hide` special names. Doodad `soundon`/`soundoff` remains a separate audio
presentation gap, so those natives are still partial rather than full conformance; see
[Scripted Doodad Animation](doodad-animation.md). Several callbacks consume state but still violate
their JASS contract and therefore need a `partial` status in any future generated
ledger. The effect module now has functional independent handles for
`AddSpecialEffect*`, `AddSpellEffect*`, and `DestroyEffect`. The three weather
effect callbacks now consume their arguments and own stable map-lifetime handles,
so they are no longer placeholders; weather presentation remains partial where
`Weather.slk` ambient sound/fog/UV details are not yet implemented. Target effects currently implement only
the `overhead` attachment specially, and `LIGHTNING` spell effects remain
unsupported, so those paths are partial rather than proof of full retail
conformance. See [Ability, Buff, And Item Presentation Effects](ability-and-item-effects.md) and [Weather](weather.md).

Natural creep sleep now consumes the authored `UnitData.canSleep` flag and implements `UnitAddSleep`, `UnitCanSleep`,
`UnitIsSleeping`, and `UnitWakeUp` against a natural-sleep state distinct from the Dreadlord `AUsl`/`BUsL` status.
`UnitCanSleepPerm` recognizes `Sleep Always` (`Asla`), while `UnitAddSleepPerm` remains a placeholder until Asla's
ability-owned `Sleep Once` / player-slot semantics are modeled. See [Neutral Creep Sleep](creep-sleep.md).
`KillUnit` must use the normal unit-death transition rather than only writing life to zero. The transition selects the
model's `Death` sequence, publishes unit/player death events, clears orders and selection, updates pathing/FOW state,
and starts the corpse/decay lifecycle. Bypassing `unit_die()` therefore leaves unit death events, presentation,
and gameplay cleanup inconsistent with the native contract.

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

`EndGame`, `ChangeLevel`, `RestartGame`, and `DisplayLoadDialog` cross the existing `gi.MenuAction` session boundary.
`EndGame` returns the local client to the frontend, `ChangeLevel` loads the requested map, `RestartGame` reloads the
current `map` cvar, and `DisplayLoadDialog` enters the frontend load-game screen. `ForceCampaignSelectScreen` is different:
stock `SetCampaignAvailableBJ` calls it while an ending cinematic can still be running, so the native only latches campaign
selection as the destination for the eventual `EndGame`. This is required by `Prologue02`, whose `NextLevelPrep` unlocks
the Human campaign before a long final cinematic and calls `CustomVictoryBJ` only after that cinematic finishes.
`SetTutorialCleared`, `SetCampaignAvailable`, and `SetMissionAvailable` now persist their profile availability state to
`campaign-progress.orcp`, so those Prologue02 unlocks are durable before the eventual frontend transition. See
[campaign-progress.md](campaign-progress.md). The `doScoreScreen` parameter is consumed but score-screen presentation
is not implemented yet.

`PlayCinematic` now queues `Movies\<name>.mpq` through `gi.QueueMovie`. When the script subsequently requests a map/menu
session action, the client pauses the outgoing simulation, plays the pre-rendered movie through the optional FFmpeg
backend, then resumes that deferred action after EOF or Escape. Builds without `FFMPEG=1` leave the native harmless and
continue the session transition. `SetOpCinematicAvailable` and `SetEdCinematicAvailable` are still incomplete; campaign
movie unlock persistence and camera-button rows remain follow-up work. See [pre-rendered-movies.md](pre-rendered-movies.md).

`GetDefaultDifficulty` / `SetDefaultDifficulty` now own a per-level default distinct from mutable
`GetGameDifficulty()` state. Campaign map startup seeds both values from `wc3_campaign_difficulty`; scripts may then
change current and default difficulty independently.

Still incomplete: the generic `Dialog*` / dialog-button event natives, `DialogAddQuitButton`, actual score-screen
presentation, Reduce Difficulty/observer-on-death result policy, and result-dialog ownership of single-player modal
pausing. The existing `PauseGame` / modal path should be reused for that work rather than adding a second pause model.

## Campaign Game Cache

Campaign cache natives use a `gameCache_t` JASS handle plus committed storage.
`Store*` always mutates only the handle; persistence happens at the explicit
`SaveGameCache` call. `wc3_gamecache_mode` defaults to `disk`, which commits to
both process memory and the OpenRealm sidecar for cross-process campaign
carry-over. `memory` keeps committed state process-local, while `disabled` makes
new cache handles start empty and makes `SaveGameCache` a successful non-persisting
operation for cache-miss testing. Scalar typed values support
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

## Code Callback Representation

Every declared function used as a JASS `code` value retains its `LPCJASSFUNC` declaration, including native functions.
`jass_pushfunction()` must not replace a native declaration with the separately allocated `jasstype_cfunction` wrapper used for
raw host calls. A script argument typed as `code` copies its value by pointer; copying that wrapper and later treating it as a
`JASSFUNC` makes the wrapper's first word (the C function address) appear to be `func->args`. On AArch64 this produced the issue
#410 crash in `jass_call_impl()` while iterating the apparent argument list. Dispatch resolves `func->nativefunc` only when the
retained declaration is invoked.

`jass_syntax.native_code_survives_script_argument` reproduces the complete contract: it forwards a declared native callback through
a script `code` parameter, stores it in a host callback, and invokes it later. Run it through `make test-galaxy`.

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
- Research queue lifecycle now publishes `EVENT_PLAYER_UNIT_RESEARCH_START/CANCEL/FINISH` and the corresponding unit-scoped events. `GetResearchingUnit()` resolves to the producer building and `GetResearched()` resolves to the exact upgrade rawcode. The rawcode is carried as scalar event context rather than inferred from the producer's mutable queue, because cancel/finish may free the queue item before deferred JASS callbacks execute. Prologue02's War Mill upgrade tutorial (`Trig_U2_ClickUpgrade_Done`) relies on the player-unit research-start event.
- Unit-type structure upgrading (`UnitProfile.Upgrade` / `uupt`, for example Town Hall -> Keep) now publishes `EVENT_PLAYER_UNIT_UPGRADE_START/CANCEL/FINISH` and the corresponding unit-scoped events on the same building edict. Completion performs the in-place type rebind before the finish event is queued, so the lifecycle preserves the original unit handle/edict identity instead of substituting a newly spawned structure. This lifecycle is separate from `UpgradeData.slk` research events. See [building-construction.md](building-construction.md#in-place-structure-upgrades-uupt).
- `GetPlayerStructureCount` scans live owned unit edicts classified by `G_UnitIsBuilding`. `includeIncomplete=false` excludes `construction.active` structures; dead/zero-life structures never count. Campaign defeat conditions may query this native from an ordinary unit-death trigger, so the result is derived from all surviving structures rather than from the triggering unit.
- `GetPlayerUnitCount` scans live owned non-structure units. Dead/zero-life entities never count, and `includeIncomplete=false` excludes entities still carrying the shared construction-active state.
- `GetPlayerTypedUnitCount` scans live owned units and structures by Warcraft's legacy string identity. It accepts both the four-character value returned by `UnitId2String` and the authored unit profile name (for example `Peon`), and honors `includeIncomplete` through the same construction-active state. `includeUpgrades` is accepted but upgrade-equivalence expansion remains unimplemented because equivalence groups are not represented yet. This native is used by campaign/tutorial goals that re-count an owned unit type after training finishes.
- `EVENT_PLAYER_UNIT_TRAIN_START` and `EVENT_UNIT_TRAIN_START` are published when an accepted trainee enters a producer queue. The producer remains `GetTriggerUnit()`, while the hidden queued trainee is carried as callback source so `GetTrainedUnitType` and `GetTrainedUnit` expose the trainee for both train-start and train-finish callbacks.
- `GetTrainedUnitType` returns the class/rawcode of the current trained unit, using the event source for train-start callbacks and the event subject for train-finish callbacks rather than returning the previous zero stub.
- Accepted point orders publish `EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER` and `EVENT_UNIT_ISSUED_POINT_ORDER`. `GetOrderedUnit()` identifies the ordered unit, `GetIssuedOrderId()` exposes the accepted order ID, and `GetOrderPointX/Y/Loc()` expose the event point. Construction placement uses the building rawcode as its issued order ID and publishes at placement acceptance, which is required by Prologue02's Burrow-to-lumber tutorial handoff. See [issued-target-order-events.md](issued-target-order-events.md).
- `OrderId` / `OrderId2String` now use canonical Warcraft numeric IDs for the covered movement and implemented stock spell orders, with the previous FourCC conversion retained only as an unknown/custom fallback. `IssueImmediateOrderById`, `IssuePointOrderById`, `IssuePointOrderByIdLoc`, and `IssueTargetOrderById` route through the same string-order paths.
- Polymorph participates in that table as `polymorph = 852074`; owned `Aply` casts use the shared unit-target spell path. `IsUnitType(whichUnit, UNIT_TYPE_POLYMORPHED)` reflects the live reversible Polymorph state rather than a sheep rawcode. See [polymorph.md](polymorph.md).
- `IssueBuildOrderById` routes directly through `G_IssueBuildOrder()`, so JASS construction uses the same worker `Builds`, technology/resource, placement/pathing, waypoint, and issued-point-order validation as player/AI construction. Building rawcodes passed to `IssuePointOrderById` or `IssuePointOrderByIdLoc` are special-cased into that same build path before generic order-id translation; canonical movement/spell ids continue through the order table. `CreateUnit` remains an immediate spawn rather than construction, and string-form `IssueBuildOrder` remains incomplete.
- Construction completion publishes both `EVENT_PLAYER_UNIT_CONSTRUCT_FINISH` and `EVENT_UNIT_CONSTRUCT_FINISH`; `GetConstructedStructure()` resolves the completed structure in either callback family. `GetConstructingStructure()` and `GetCancelledStructure()` continue to expose the corresponding implemented start/cancel event structure. See [building-construction.md](building-construction.md).
- Stock spell orders owned by the caster route through the unified spell pipeline. Successful casts publish `EVENT_PLAYER_UNIT_SPELL_EFFECT` and `EVENT_UNIT_SPELL_EFFECT` after mana/cooldown commit and before the gameplay callback. `GetSpellAbilityUnit`, `GetSpellAbilityId`, `GetSpellTargetUnit`, `GetSpellTargetX/Y/Loc`, `GetSpellTargetItem`, and `GetSpellTargetDestructable` expose that response context. `GetSpellAbility` and the CHANNEL/CAST/FINISH/ENDCAST lifecycle remain unsupported rather than being synthesized.

## Time Of Day Game State

`GAME_STATE_TIME_OF_DAY` is the sole `fgamestate` declared by the bundled `common.txt`. `SetFloatGameState` and
`GetFloatGameState` now round-trip through the server-owned Warcraft daily cycle, and `SuspendTimeOfDay` freezes only ordinary clock
progression. Explicit sets are deferred until the next time-of-day update and still apply while suspended.

`TriggerRegisterGameStateEvent` is **partial**: registrations for `GAME_STATE_TIME_OF_DAY` store the `limitop` and threshold and fire
once on a false-to-true transition; integer game-state limit events are not implemented yet. The server-authored
`TimeOfDayIndicator` now binds to the replicated normalized day phase and the WC3 MDX renderer scrubs its selected sequence with an
explicit `@ratio`. `SetDayNightModels` now registers the map-authored terrain/unit DNC models, republishes their model indices to
the client, and the WC3 renderer samples sequence 0 at the same normalized phase to use each model's first light as the corresponding
base world light. `AIct` (`itemchangetimeofday`) now follows Warsmash's false-time behavior: authored DataA/DataB/Dur install a
temporary effective clock, the canonical cycle freezes underneath it, and the HUD selects the clock model's alternate sequence while
the override is initialized. The Warsmash-extension host native `SetFalseTimeOfDay` is also registered, but is intentionally absent
from the bundled retail `common.j`; extension scripts must declare it themselves. `SetTimeOfDayScale` and `GetTimeOfDayScale` remain
placeholders because the inspected Warsmash source does not provide a behavior to mirror. See [time-of-day.md](time-of-day.md) for
ownership, false-time lifecycle, `Misc` fields, gameplay consumers, HUD synchronization, DNC rendering, and the remaining
visual-lighting gaps.

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

This is distinct from `PauseUnit`, `PauseCompAI`, and timer pause state. `PauseTimer` / `ResumeTimer` operate on the existing deterministic countdown timer and do not alter the global scheduler pause source.

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

Timer dialogs are now implemented for the ordinary campaign-countdown path. `CreateTimerDialog` allocates a stable fixed-slot handle associated with an existing timer; `DestroyTimerDialog` removes only presentation state and leaves that timer running. `TimerDialogSetTitle`, title/time RGBA setters, `TimerDialogDisplay`, and `IsTimerDialogDisplayed` drive the stock `TimerDialog.fdf` HUD through a dedicated layout layer. Values render as zero-padded `MM:SS` and refresh only when the visible whole second or dialog presentation state changes. Local visibility follows the existing `currentplayer` convention. Timer-dialog handles and presentation state survive save/load through stable slot IDs. See [Timer Dialogs And Mission Countdowns](timer-dialogs.md).

`TimerDialogSetSpeed` is still intentionally a no-op pending verified retail display-rate semantics, and `TimerDialogSetRealTimeRemaining` is not registered yet. The renderer currently displays the lowest-slot visible dialog rather than guessing at retail multi-dialog stacking.

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
the same candidate/point/level/max-rank validation as the in-game skill menu.
`GetHeroSkillPoints` reads the unspent Hero point pool and
`UnitModifySkillPoints` applies a signed delta without changing XP/level; the
shared mutation clamps subtraction at zero and refreshes the owner's command
state. `SetHeroXP`, `AddHeroXP`, and `SetHeroLevel` share the raise-only Hero XP
transition; every crossed level publishes both `EVENT_PLAYER_HERO_LEVEL` and
`EVENT_UNIT_HERO_LEVEL`, and `GetLevelingUnit()` resolves to that Hero.
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

`CreateSound`, `SetSoundVolume`, `SetSoundPosition`, `AttachSoundToUnit`, and `StartSound` currently cover the one-shot subset used by campaign cinematics. Position/attachment is sampled when playback begins and transported through generic `svc_sound`; moving attached emitters, `StopSound`/fade lifecycle, playing/loading queries, pitch/cone/distance controls, and volume groups remain incomplete. `killWhenDone` eventually needs to release only after playback completion, while an immediate destroyed/stopped handle cannot remain queryable as playing.

Background music now uses a separate long-form path: `SetMapMusic`, `ClearMapMusic`, `PlayMusic`, `PlayMusicEx`, `StopMusic`, `ResumeMusic`, `PlayThematicMusic`, `PlayThematicMusicEx`, `EndThematicMusic`, the music/thematic volume setters, and both position setters serialize through `svc_music`; `client/cl_music.c` optionally decodes with `FFMPEG=1` into an independent music PCM stream. `war3skins.txt` and `Music.slk` are resolved in the Warcraft game module, including per-race and RoC/TFT skin fallback. Startup map music is retained before `ClientBegin` and synchronized when the player becomes presentation-safe. `GetSoundFileDuration`, `StopMusic(true)` fade timing, `war3mapSkin.txt`, exact retail random/index semantics, and save-game decoder position remain gaps. See [music.md](music.md). The ordinary one-shot mixer remains WAV-only, so compressed campaign dialogue is still a separate decoder/transport gap.

Summon event context uses the summoner as the trigger unit and the created unit as the event source: `GetSummoningUnit()` reads the former and `GetSummonedUnit()` the latter. Mirror Image (`AOmi`) uses the common no-target spell path, creates the data-defined number of timed copies, marks them as illusions for `IsUnitIllusion`, and publishes both player-unit and unit summon events. Damage multipliers, dispel interaction, image shuffle, and full illusion visual semantics remain separate parity work.

## Leaderboards

Leaderboards are now fixed-slot, save-stable server-authored UI models with copied labels, integer values, optional player ownership, styles, colors, stable sorting, and per-player assignment. All 27 registered leaderboard natives mutate/query this state. `PlayerSetLeaderboard` selects one board for a player; `LeaderboardDisplay` controls that board's per-client presentation mask.

The HUD loads stock `LeaderBoard.fdf` chrome and emits runtime text rows through dedicated `LAYER_LEADERBOARD`. A value update therefore refreshes only the assigned clients' leaderboard layer. Labels resolve through the existing map-string path. Save format 24 persists board/item state, assignment indexes, and JASS handle identity. See [Leaderboards And Counted Objective HUDs](leaderboards.md).

Current presentation intentionally defers icon rendering and exact retail row/player-color packing; those flags are retained in model state rather than guessed visually.

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

## Version handle crash in computer-player startup

`common.j` declares `VersionCompatible` and `VersionSupported` with a `version` parameter. `ConvertVersion`
allocates that typed handle; it is not an integer argument. Retail TFT `common.ai` calls
`VersionCompatible(VERSION_FROZEN_THRONE)` while selecting heroes. The old integer check asserted from
`G_BotRunFrame`; a bounded local lobby reproduced a valid version handle containing 1 immediately before the
assertion. Both natives now consume `jass_checkhandle(..., "version")`.

`wc3_api.version_queries_accept_typed_handles` exercises both converted versions through both native callbacks.
This correction preserves the existing RoC-only version-reporting policy (`VersionGet` returns 0); expansion-aware
version reporting remains a separate conformance gap.

After the handle correction, the bounded Twisted Meadows lobby reached gameplay. Its Orc AI then stopped with
the existing `MeleeDifficulty` unimplemented-native diagnostic; that separate API gap does not abort the process.

### Shared Galaxy Error Boundaries

The shared VM now also unwinds unknown (undeclared) calls through its protected boundary, validates native scalar/string arguments
with runtime errors instead of process assertions, and protects global-initializer evaluation. Unique missing function names remain
queryable until VM close via `jass_missingcount`/`jass_missingname`. See [Galaxy protected calls and coverage](../starcraft-2/galaxy-scripting.md#protected-calls-and-missing-native-inventory)
for the SC2 reproduction, inventory workflow, and shared VM regressions. This does not change WC3 quest state ownership or native IDs.
