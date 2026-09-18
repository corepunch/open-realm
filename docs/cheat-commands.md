# Cheat commands

The engine exposes `sv_cheats`, defaulting to `0`. Game commands are server-authoritative and are rejected until cheats are enabled:

```
set sv_cheats 1
give ...
```

This follows the Quake 2 model: `give` is a game/server command, while `+give` is a late command-line command that runs after the map command when both are supplied. For example:

```
openwarcraft3 +set sv_cheats 1 +map "Maps/Campaign/Human02.w3m" +give gold 5000
```

This deliberately differs from stock Quake II's policy: Q2 gates `give`/`god` in deathmatch, whereas this engine
requires `sv_cheats 1` in every mode and also gates `kill`.

## Command lifecycle

`Cmd_ExecuteString` resolves engine commands/CVars, then forwards game text as `clc_stringcmd`.
`SV_ExecuteUserCommand` passes the issuer's edict to the game module's `ClientCommand`; the game owns names,
permission, target selection, and effects. Client-side command-name lists must not mirror the game's dispatch table.

Quake II uses `Cbuf_CopyToDefer` at map loading and `Cbuf_InsertFromDefer` at `SV_Begin_f`.
OpenRealm keeps that whole-buffer pattern, but restores the tail on the first active client snapshot:
our client remains `ca_connected` through `begin` and cannot forward ordinary game commands yet.
`SV_Map` (with a local client) and a successful `CL_Connect` save the tail; disconnect, failed map loading, or replacement loading
discard it with a diagnostic. Commands retain ordering and full text within the ordinary command-buffer/parser limits.
The old eight-entry, 256-byte queue and `CL_IsDeferredCommand` whitelist are removed.

`+map` remains an early startup selector. Startup queues late commands before loading and executes them after readiness;
this is necessary because the former startup order tried forwarding cheats while still disconnected.
Loading presentation can reenter `CL_BeginLoadingMap` when `CS_WORLD` arrives; it must never replace the saved tail.
Dedicated servers continue processing operator commands without waiting for a local client.
Console commands typed outside an active connection are rejected rather than saved for an unrelated future session.
Commands entered during loading are likewise rejected unless they belong to the captured map/connect command tail.

References: [Q2 client forwarding](https://github.com/id-Software/Quake-2/blob/master/client/cl_main.c),
[Q2 command buffers](https://github.com/id-Software/Quake-2/blob/master/qcommon/cmd.c),
[Q2 begin](https://github.com/id-Software/Quake-2/blob/master/server/sv_user.c),
[Q2 cheat handlers](https://github.com/id-Software/Quake-2/blob/master/game/g_cmds.c).

## Debug console scaling

`client/console.c` scales the system-font atlas in whole multiples of its 8-by-8 glyph size:
`max(1, floor(min(window.width / 640, window.height / 480)))`. Console glyphs, spacing, margins,
and border thickness share that scale: 640x480 and 1280x720 use 1x, 1920x1080 uses 2x (16x16 glyphs),
and 2560x1440 uses 3x. These are SDL window coordinates; Retina drawable pixels may be larger.
The minimum stays 1x for windows smaller than 640x480.

`re.DrawCharScaled` is the console's renderer entry point. The shared `r_draw_string_scaled` implementation
is private to `renderer/r_draw.c`; ordinary `re.DrawChar` and `re.DrawString` retain 1x sizing.
This console presentation policy is independent of cheat execution and game FDF text.

PR #357's original console-scaling commit `ab06a6ee` used fractional scaling. A bounded run at 1920x1080
confirmed scale 2.25 and 18x18 glyphs; rounding down selects the requested 16x16 glyphs instead.
Reproduce the console scene with these late command arguments (add `-tft` for the expansion):

```
+set vid_native 0 +set vid_mode 10 +set vid_fullscreen 0 +menu_main +toggleconsole +screenshot 5 +com_frame_limit 10
```

ROC/TFT screenshots verified the rounded console. Temporary scale traces were removed after verification.
The follow-up passed `make test` both on the PR base and applied to main `583f1674`; all three games built
in the latter checkout. That main includes `0059057e`, which fixes the older PR base's SC2 routing build failure.

## Warcraft III

All `sv_cheats 1` Warcraft III cheat commands print their result to the issuing player's in-game console. This includes successful state changes as well as disabled-cheat, usage, and validation feedback. The same text is retained on stderr for terminal/debug logs; console delivery is presentation-only and is skipped for disconnected test/reserved clients.

`hero max` raises the selected friendly Hero to the active map's `Misc/MaxHeroLevel` and grants the skill points associated with that level:

```
hero max
```

`hero select` selects the first live controllable Hero owned by the issuing player through the ordinary authoritative selection path. It is useful for headless/GDB campaign diagnostics when the normal mouse selection path is unavailable:

```
hero select
```

The command requires `sv_cheats 1` and refreshes the normal selection events, portrait, command card, and client selection snapshot.

`hero dump` prints a machine-readable `HERO_SAVELOAD snapshot=dump` line for the selected Hero, or the first live Hero owned by the issuing player when nothing is selected. `hero walk [dx dy]` issues an ordinary `move` order from the selected Hero; omitted offsets walk 80 world units along +X. Both require `sv_cheats 1`. The campaign diagnostic `wc3_hero_saveload_audit` uses the same snapshot format without needing a connected client; see [Save/Load](games/warcraft-3/save-load.md#hero-walk--save--load).

The command uses the ordinary Hero XP/level-up path so attribute growth and Hero level events remain authoritative. After leveling, it ensures the Hero has at least one total skill point per Hero level minus already learned Hero ability ranks; existing extra skill points awarded by map scripts are preserved. The command requires `sv_cheats 1` and a selected controllable Hero.

The same selected-Hero command family can restore or directly set current health and mana:

```
hero health
hero health <amount>
hero mana
hero mana <amount>
```

Without an amount, `hero health` fills current health to the Hero's current maximum and `hero mana` fills current mana to its current maximum. With an amount, the command sets the current value to that non-negative integer, clamped to the current maximum; it never changes maximum health or maximum mana. Health uses the ordinary runtime health setter, while mana updates the current mana pool directly. These commands require `sv_cheats 1` and a selected controllable Hero.

For map-start resource testing, `wc3_cheat_starting_resources` is a session-only CVar. Set it before loading the map:

```
openwarcraft3 +set sv_cheats 1 +set wc3_cheat_starting_resources 1 +map "Maps/Campaign/Human02.w3m"
```

When enabled, the map load arms a one-shot +5000 gold / +5000 lumber grant for each used human-controlled slot. The grant is not tied directly to `war3map.j main()`: OpenRealm waits until that human client is connected, user control is enabled, and the ordinary gameplay UI has returned from any intro cinematic, then adds the bonus to the resource values authored by the map at that boundary. Computer and neutral players are unchanged, values clamp to the player-state storage limit, and each human client can receive the grant only once per fresh map load. Save-game restoration preserves the saved resource totals and disables any still-pending map-start grant so loading cannot award another +5000. The CVar is sampled when the map is spawned; changing it mid-map does not grant resources until the next map load/restart. Both this CVar and `sv_cheats 1` must be enabled before map load. Revoking `sv_cheats` before a pending grant applies cancels it; enabling cheats later does not rearm that map. Rejections/cancellations are logged. The default is `0`.

The playable-state boundary is necessary for campaign maps. A Human02 runtime trace showed the generated initialization setting the human slot to `0` gold / `0` lumber, followed by a later `EVENT_PLAYER_END_CINEMATIC` path that authored the real starting values as `300` gold / `50` lumber. Applying the cheat immediately after `main()` therefore produced `5000/5000` only for the campaign trigger to overwrite it with `300/50`. Waiting until the cinematic returns control makes the same map resolve to `5300/5050`. Melee maps without an intro cinematic receive the grant on their first playable server frame after initialization.

Resource commands target the issuing player and are additive, so they work even when no unit is selected:

```
give gold <amount>
give lumber <amount>
give res <amount>       # add the amount to both gold and lumber
```

Amounts are non-negative integers and clamp at the player-state resource storage limit rather than wrapping. These commands require `sv_cheats 1`; they are independent of `wc3_cheat_starting_resources`, which remains useful when a reproducible bonus should be applied automatically at map start.

Terminal result cheats also target the issuing player and do not require a selected unit:

```
win
lose
```

Both require `sv_cheats 1`. They intentionally use the same authoritative player-removal/result transition as JASS `RemovePlayer`: `win` records `PLAYER_GAME_RESULT_VICTORY` and publishes `EVENT_PLAYER_VICTORY`, while `lose` records `PLAYER_GAME_RESULT_DEFEAT` and publishes `EVENT_PLAYER_DEFEAT`. Existing map result triggers, ending cinematics, pause-aware result-event draining, and the normal campaign/result dialog continuation therefore remain in control; the cheats do not directly switch maps or force a UI screen. Repeating either command after the player has already been removed is a no-op, matching `RemovePlayer` idempotence.

Campaign quest/script debugging uses several related command families, all gated by `sv_cheats 1`. Their diagnostic/result lines are written both to stderr and to the issuing player's in-game console, so `quest list`, `trigger list`, `objective list`, and `cinematic list` are usable without watching the launch terminal:

For JASS group-lifetime diagnostics, `wc3_group_debug 1` records each live group's creator, nested JASS call path, and trigger ordinal in a non-persistent growable side table. The group registry itself grows past the old 1024-handle ceiling and prints `WC3_GROUP_DEBUG grow ...` at each pointer-table expansion; if allocation genuinely fails, the engine prints grouped `WC3_GROUP_DEBUG chain` summaries with live/allocated/freed/outstanding counts. Use these to find pathological retention without adding map-specific cleanup rules.

With `wc3_quest_debug 1`, on-screen tutorial/message presentation also emits `WC3_TUTORIAL_TEXT` lines. `DisplayTextToPlayer`, both timed text natives, and `SetCinematicScene` include the active trigger ordinal, JASS caller, target player, raw `TRIGSTR_*` token, resolved text, and timing/position fields; `EndCinematicScene` logs the matching clear. Use this to distinguish a tutorial trigger that never emits its next instruction from text that was authored correctly but failed in presentation.

For the Prologue02 Burrow-to-lumber handoff, the same cvar also emits `WC3_TUTORIAL_FLOW` lines for trigger ordinals 120-165 and startup-only `WC3_TUTORIAL_SOURCE` blocks for `Trig_W2_BurrowComplete_Q`, its generated pre/post-wait helper predicates, the Burrow work-complete/check paths, the abort path, the later War Mill tutorial trigger, and any `gg_trg_*` triggers referenced by the Burrow action body. The source dump also discovers whichever authored functions reference narrator sounds `T02Narrator031` through `T02Narrator035`, covering the lumber-harvest and War Mill construction instructions without assuming their trigger names. The flow trace records definitions, registrations, enable/disable changes, direct evaluate/execute calls, JASS sleep/wait durations, `GetTriggeringTrigger` context reads, and `IsTriggerEnabled` results. `WC3_TUTORIAL_COROUTINE` additionally reports when one of those trigger coroutines becomes eligible to resume and whether that resume yields again or finishes. Runtime traces have confirmed that `WaitForSoundBJ` resumes normally for trigger 150 and reaches its queue-removal tail; the wider range is intended to identify the missing authored lumber-stage enqueue/event rather than diagnose coroutine wakeup.

```
quest list
quest complete <index>
quest complete all

trigger list [case-sensitive-function-filter]
trigger fire <index>
trigger fire <index> selected

objective list [case-sensitive-function-filter]
objective complete <trigger-index>
objective complete <trigger-index> selected
objc <trigger-index>                  # alias for objective complete <trigger-index>
objc <trigger-index> selected         # alias preserving selected-unit context

jass <zero-argument-function-name>

cinematic list [case-sensitive-function-filter]
cinematic play <trigger-index>
cinematic play <trigger-index> selected
cinematic stop
```

`quest list` prints the allocated quests in the same 0-based in-use order used by the existing `quest <index>` journal command. `quest complete` marks the chosen quest and every allocated objective item completed; `all` applies that state change to every allocated quest. This is intentionally a **quest-state/UI cheat only**: it does not execute map-authored completion actions, does not fire a synthetic quest-completed event, and does not alter `failed`, `discovered`, `enabled`, or `required`. Campaign scripts usually advance because their own gameplay trigger runs, not because `QuestSetCompleted` changed a journal flag.

`trigger list` prints the stable `level.triggers[]` index, enabled/disabled state, and registered condition/action function names. The optional filter matches those JASS function names. `trigger fire` deliberately behaves like direct `TriggerExecute`: it executes the trigger's registered actions as coroutines without evaluating its conditions and even when the trigger is disabled. This makes it suitable for invoking campaign completion/action triggers that normal progression has not armed yet. Without `selected`, event-response unit/player context is empty. With `selected`, the current primary selected unit is supplied as the trigger unit, which also derives `GetTriggerPlayer()` from that unit's owner. Event fields that require a distinct source unit (for example `GetKillingUnit`) remain unset.

`objc <trigger-index> [selected]` is a shorthand for `objective complete <trigger-index> [selected]`; it uses the same cheat gate and TriggerExecute-style path.

`objective` is a convenience layer for the common campaign case where the map has a completion/victory trigger that performs the real progression work. `objective list` scans trigger **action** names for likely completion triggers. It accepts ordinary `Victory_*` actions and quest/objective names containing completion words such as `Complete`, `Finish`, or `Done`, while rejecting obvious `Cheat`, `Defeat`, cinematic, skip, intro/outro, and time-stop helpers. For the Prologue map observed during development this keeps `Trig_Victory_Found_Medivh_Actions` while rejecting `Trig_Victory_Cheat_Actions`, `Trig_Defeat_Thrall_Dies_Actions`, and `Trig_End_Cinematic_Actions`. `objective complete <trigger-index> [selected]` executes the chosen trigger through the same direct TriggerExecute-style path as `trigger fire`; the index is the stable trigger index printed by the list, not a separate objective ordinal. This remains heuristic discovery, so `trigger list [filter]` is the fallback for unusually named progression triggers.

`jass` starts a named map JASS function as a coroutine, so normal trigger sleeps/waits can yield and resume. It is intended for generated zero-argument functions such as `Trig_*_Actions`; calling functions that require arguments is unsupported. Unlike `trigger fire ... selected`, this command does not manufacture event-response context.

`cinematic` is a higher-level wrapper for in-map scripted cutscenes. `cinematic list` now classifies only trigger **action** names with strong cutscene markers (`cinematic`, `cutscene`, `intro`, `outro`, `ending`, `interlude`) and rejects obvious helper names containing `skip`, `time_stop`/`timestop`, or `cheat`. Generic `victory` and `defeat` markers are intentionally not cinematic evidence: they commonly describe ordinary progression/result triggers. This avoids the observed false positives `Trig_Intro_Cinematic_Skip_Actions`, `Trig_Intro_Time_Stop_Actions`, `Trig_Victory_Cheat_Actions`, `Trig_Defeat_Cheat_Actions`, `Trig_Victory_Found_Medivh_Actions`, and `Trig_Defeat_Thrall_Dies_Actions`, while retaining `Trig_Intro_Cinematic_Actions` and `Trig_End_Cinematic_Actions`. The optional filter further restricts the surviving candidates by JASS function name. Discovery is intentionally heuristic: a map may name a cinematic trigger without any of those words, so a failed candidate search must fall back to `trigger list [filter]` rather than assuming the map contains no cutscene.

`cinematic play <trigger-index> [selected]` uses the same direct TriggerExecute-style path as `trigger fire`: it runs the map-authored trigger actions even if the trigger is disabled and without evaluating its conditions. The index does not have to be one reported by `cinematic list`, which makes `trigger list` a reliable manual fallback. The optional `selected` context has the same GetTriggerUnit/GetTriggerPlayer behavior and limitations as `trigger fire selected`.

`cinematic stop` intentionally does **not** force camera/UI/control state back to gameplay. It publishes the normal `EVENT_PLAYER_END_CINEMATIC` event for the issuing player and other human map players, matching the existing Escape/cancel route. The map's authored skip trigger therefore owns its skip flag, camera reset, unit cleanup, user-control restoration, and termination of the main cinematic coroutine. If a map does not register an end-cinematic handler, the command cannot safely invent that cleanup.

These commands play **in-map JASS cinematics**, not prerendered `PlayCinematic` movie files. Movie decoding/playback remains a separate subsystem.

For campaign skipping, prefer `objective list` followed by `objective complete <trigger-index>` when it finds the relevant authored completion trigger; otherwise use `trigger list <part-of-name>` followed by `trigger fire <index>`. Both execute the map's authored actions so dialogue, spawning, trigger enable/disable changes, quest updates, and later mission state can advance together. For cutscenes, `cinematic list`/`cinematic play` provide the same execution path with narrower cutscene discovery and an authored `cinematic stop` escape route. `quest complete` remains useful when testing only the journal/UI state.

Instant build is an issuing-player cheat and requires `sv_cheats 1`:

```
instant build          # toggle
instant build on
instant build off
warpten                # Warcraft-style alias; same build toggle
```

When enabled, structures owned by that player complete on the next construction entity frame, in-place building upgrades complete on the next upgrade frame, ordinary trained units complete on the next producer tick, and active research completes on the next research tick. Construction checks the cheat in the building-owned construction state before the Human paused-strategy gate, so completion does not depend on a particular worker behavior or race construction strategy. The cheat deliberately preserves the normal command path: the worker still travels to a valid site, placement and resource/food checks still run, upgrade/research costs and requirements are still checked, the structure/unit/upgrade/research still starts through its ordinary lifecycle, and normal completion events, UI invalidation, sounds, rally orders, and exit-placement checks remain authoritative. Turning the cheat on also affects construction, building upgrades, training, and research already in progress on their next applicable frame/tick. A blocked producer exit still keeps a completed trained unit queued until a legal exit position exists. Hero revival timers are not changed by this command.

The state is per player rather than global, so enabling it for a human player does not accelerate computer opponents. Like the other runtime cheats it stays active until explicitly toggled off or the player state is replaced by a fresh map/client lifecycle.

Instant kill is also an issuing-player cheat and requires `sv_cheats 1`:

```
instant kill           # toggle
instant kill on
instant kill off
```

When enabled, any normal damage dealt by that player's units or buildings is made lethal to unit, building, and attackable destructable targets on the same damage-resolution call. This includes destructable gates as well as trees and crates when they are attackable. It preserves the ordinary death path, so destructable death transitions, death callbacks, combat cleanup, kill credit, and attack completion still run normally. Invulnerable targets remain immune. The state is per player, so other human or computer players retain normal damage unless they enable their own cheat state.

The combined command controls both instant-build and instant-kill state for the issuing player:

```
instant all             # turn both on unless both are already on; otherwise turn both off
instant all on
instant all off
```

If one of the two cheats is on and the other is off, bare `instant all` turns both on. The explicit `on`/`off` forms always set both states together.

Time-of-day phase cheats set the authoritative Warcraft clock directly and require `sv_cheats 1`:

```
day
night
```

The commands use the active map's authored `Dawn`, `Dusk`, and `DayHours` values and choose the midpoint of the requested phase. Stock Warcraft values therefore produce 12:00 for `day` and 00:00 for `night`, while maps that override their day/night thresholds still land inside their own authored phase. The write goes through `G_SetTimeOfDay()`, so HUD clock state, DNC lighting, sight/regeneration rules, and time-of-day trigger conditions consume the same authoritative change. Explicit time sets still apply while ordinary time-of-day progression is suspended.

The obsolete `wc3_cheat_timeofday_scale` registration was removed: current `G_UpdateTimeOfDay` never read it.
Use `day`/`night` for phase testing; there is no independent clock-speed cheat.

Unit-oriented commands target the first selected unit:

```
give item <rawcode>
give ability <rawcode>
give xp <amount>       # hero only
god                    # toggle selected player unit invulnerability
kill                   # kill the selected controllable unit
```

`god` and `kill` resolve the primary controllable selection (including shared-control units). Missing/enemy-only
selections are rejected. `god` changes that actor's invulnerability; `kill` clears it and invokes the actor's normal
`die` callback, preserving death events, food cleanup, corpse/revival state, and deselection. The invisible controller
is never the target. Previously both commands mutated the controller, and `kill` only changed its health.

`give item` uses the normal item spawn and pickup path, so inventory capacity and passive item effects remain authoritative. `research <rawcode>` remains available for the existing non-cheat research/debug path.

Movement/camera diagnostics use compact console command families:

```
enemiesclear [radius]
eclear [radius]          # alias for enemiesclear

camera move <x> <y>
camera selected
camera edge <0|1>
```

`enemiesclear` is cheat-gated by `sv_cheats 1`. It uses the primary selected friendly unit as the center and immediately removes nearby non-building enemy units; the default radius is 768 world units. The `eclear` alias runs the identical handler. Friendly units, buildings, units outside the radius, and the selected center unit are left unchanged.

The `camera` family is diagnostic rather than a gameplay cheat. `camera move` moves the gameplay camera to the requested world coordinates, subject to the map's camera bounds. `camera selected` focuses and follows the primary selected unit using the same persistent target controller as portrait camera focusing. `camera edge 0` disables mouse screen-edge scrolling and `camera edge 1` restores it; keyboard camera binds and drag-pan remain available. Edge scrolling is client-local, while `move` and `selected` are forwarded through the normal authoritative WC3 game-command path.

## World of Warcraft

Commands target the player. Success, usage, and disabled-cheat replies use `svc_console_print` to the issuing
client as well as stderr, matching WC3 feedback:

```
give all
give health [amount]
give mana [amount]
give gold <amount>      # copper
give xp <amount>
god                     # toggle player invulnerability
kill
```

WoW’s current prototype action bar is class-authored rather than a learned-spell container, so `give spell` and item creation by database entry are intentionally not claimed yet. They should be added only when the server has a real spell-known/item-entry model to mutate.

## Console ownership

The client owns the console/menu input destination. A `CLIENT_UI_GAME` player-state update must not overwrite `key_console` or `key_menu`; otherwise the console opens for one frame and is immediately hidden by the next server snapshot.

## Regression evidence

A bounded instrumented Human02 run showed startup rejecting `instantkill` while retaining `god`/`kill`.
The selected-Footman regression showed controller invulnerability changing while the Footman remained at 420 HP
with no death flag. The resource fixture armed its bonus with `sv_cheats=0`. The WoW engine test toggled godmode
but emitted zero reply packets. Temporary traces were removed after reproducing these paths.

Focused tests: `commands.deferred_*`, `commands.canceled_*`, `wc3_game.selected_unit_cheats_*`,
`wc3_game.unit_cheats_*`, `wc3_game.starting_resource_cheat_*`, and `wow_combat.cheat_feedback_*`.
Use `make test` for all command, network, and game suites. Runtime startup check (ROC or `-tft`):

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -roc +set sv_cheats 1 +map 'Maps/Campaign/Human02.w3m' +instantkill on +give gold 123 +god +com_frame_limit 120
```

`instantkill` and `give` should execute after loading; `god` must report a missing controllable selection if none exists.

Verified on the cleanup branch: all three game binaries built; `make test` passed 59,335 assertions across
2,871 test executions (including ROC/TFT and UDP tests). The bounded Human02 run executed `instantkill on`
and `give gold 123` after `begin`, then rejected `god`/`kill` because no controllable unit was selected.
