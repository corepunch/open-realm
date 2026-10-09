# Warcraft III Triggered Dialogue

## Contract

Normal Warcraft III transmissions are presentation state, not cinematic mode.
`SetCinematicScene` may run while `PLAYER.client_ui_state == CLIENT_UI_GAME` and
must not itself move the camera, alter fog, pause simulation, hide the HUD, or
disable user control. Full cinematic policy remains owned by `ShowInterface`,
`EnableUserControl`, camera natives, and the map script.

The server owns transmission/message state in `games/warcraft-3/game/client_s`:

- `ps.cinematic_portrait` — portrait-present flag (`BYTE`, packed with team/color/race).
- `ps.stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_MODEL]` — full 16-bit registered model index used by both portrait HUD paths.
- `ps.stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR]` — requested player-color index for the portrait render entity.
- `PLAYERTEXT_SPEAKER` / `PLAYERTEXT_DIALOGUE` — resolved map strings.
- `cinematic_voice_end_time` — end of `Portrait Talk`; the scene may remain.
- `cinematic_end_time` — end of the complete transmission scene.
- `message` — one ordinary transient `DisplayText*` message, including
  position and expiry time.
- `message_log` — bounded historical `DisplayText*` and transmission entries
  used by the single-player Message Log; its lifetime is independent from
  `message` and the active transmission.

`G_SetPlayerText` preserves an actual empty string. Do not normalize cinematic
clear state to a single space: `UI_WriteCinematicLayer` and the gameplay
transmission path use empty strings to decide whether a scene exists.

## Presentation Flow

`SetCinematicScene`

```text
resolve TRIGSTR speaker/text
  -> resolve portrait unit type to model
  -> store supported player color for portrait replaceable textures
  -> store scene and voice expiry independently
  -> mark client presentation_dirty
  -> G_RunClients flushes UI_WriteDialoguePresentation for connected clients
```

`UI_WriteDialoguePresentation` selects presentation from the existing UI mode:

```text
CLIENT_UI_GAME
  -> LAYER_PORTRAIT: temporary transmission portrait
  -> LAYER_MESSAGE: yellow speaker name + dialogue

CLIENT_UI_CINEMATIC
  -> LAYER_CINEMATIC: CinematicPanel portrait/speaker/dialogue
```

A gameplay transmission does not set `CLIENT_UI_CINEMATIC`. When it expires,
`G_RunClients` clears transmission state, restores the selected-unit portrait,
and restores an ordinary timed message if that message is still alive.

`ForceCinematicSubtitles` is registered and consumes its boolean argument, but
current Warsmash effectively renders gameplay transmission subtitles regardless
of that override. OpenRealm intentionally matches that behavior for now rather
than inventing an unverified user-preference policy.

Build with `WC3_DEBUG_TUTORIAL_FLOW=1` and enable `wc3_quest_debug 1` at runtime to emit `WC3_TUTORIAL_TEXT` diagnostics before message/transmission presentation state is mutated. Without the build option, these diagnostics are compiled out. Timestamps in the cinematic-scene diagnostics are elapsed simulation time in `HH:MM:SS.mmm`, not wall-clock time. Each record includes the active trigger ordinal and JASS caller plus both the raw map string token and its resolved text. Ordinary `DisplayText*` calls also record target/position/duration; `SetCinematicScene` records speaker, dialogue, portrait and scene/voice lifetimes, and `EndCinematicScene` records the clear. This is intended for campaign tutorial progression debugging and does not alter message timing or retention.

`StartSound` records the sound handle's latest simulation start time. A later
`TriggerWaitForSound` waits only for the remaining authored duration from that
start, plus its offset. Waiting again after the duration has elapsed returns
immediately; starting the same handle again begins a new duration window. A
handle that has never started retains the previous full-duration wait behavior.
This tracks the server's logical sound dispatch, not client audibility: a
missing/unresolved client asset can still leave the script waiting for the
authored duration. The regression `wc3_api.repeated_wait_for_sound_only_waits_until_voice_end_once`
covers a 3.5-second delay, repeated wait, restarted handle, and positive offset.

Prologue02 additionally traces the Burrow-completion handoff with `WC3_TUTORIAL_FLOW` for trigger ordinals 120-165 and `WC3_TUTORIAL_SOURCE` for the authored Burrow work-complete/check functions, `Trig_W2_BurrowComplete_Q`, directly referenced trigger globals, and whichever functions reference narrator sounds `T02Narrator031` through `T02Narrator035` (the lumber/War Mill teaching sequence). `WC3_TUTORIAL_COROUTINE` confirms sleep/resume boundaries for the same range. Current runtime evidence shows the nested `WaitForSoundBJ` in trigger 150 waking normally and the trigger reaching its final queue-removal call, so the remaining diagnostic focus is the earlier lumber-stage enqueue/event path. This is startup/runtime diagnostics only: it does not synthesize tutorial steps or change trigger queue, sleep, or transmission semantics.

## Network Lifecycle

Dialogue/interface JASS natives own presentation **state**, not the server
message transport. `ShowInterface`, `SetCinematicScene`, `EndCinematicScene`,
`DisplayText*`, and `ClearTextMessages` mark `client_s.presentation_dirty`.
`G_RunClients` sends the final presentation once per simulation update only
when `client->connected` is true. The dirty bit remains set while disconnected,
so a state change is deferred rather than discarded.

`UI_WriteDialoguePresentation` also enforces the same `connected` boundary.
This is a transport invariant: `svc_layout` ultimately writes through
`gi.Write`/`PF_Write`, whose server multicast buffer is not valid before
`G_ClientBegin` completes. `UI_ShowGameInterface` is the intentional immediate
initial-HUD path because `G_ClientBegin` sets `connected` first. Selection/HUD
refresh paths may also request an immediate write, but the writer will not
serialize for an unconnected reserved player edict.

Map scripts run during `G_LoadMap` before the local client completes its
connection handshake so save restoration can rebuild deterministic JASS state.
`G_ClientBegin` must preserve `client_ui_state`, `uiflags`, and
`presentation_dirty` authored during that startup; zero-initialized clients
already represent `CLIENT_UI_GAME`. Commit `697d5f51` exposed this lifecycle by
starting scripts during map load while `G_ClientBegin` still reset the UI mode,
which kept the cinematic camera but replaced the cinematic HUD with gameplay.

Commit `39193e6` (`wc3: implement triggered in-game dialogue`) introduced the
regression by calling `UI_WriteDialoguePresentation` synchronously from
`UI_ShowInterface`. In-engine JASS tests use reserved player edicts whose
clients are deliberately not connected, so that call reached `PF_Write` with
an uninitialized zero-sized multicast buffer and printed:

```text
Write buffer overflow (msg):
```

Do not fix this in `MSG_Write`, `PF_Write`, or by initializing a fake multicast
buffer for disconnected tests. The invalid operation is the premature
server-to-client UI write.

### Alternative Considered: Guard-Only

A smaller fix is to leave every JASS/native caller synchronous and add only:

```c
if (!client->connected)
    return;
```

to `UI_WriteDialoguePresentation`. This prevents the overflow, but it keeps
state mutation coupled to immediate network serialization and can emit several
complete layout updates when one trigger changes interface mode, transmission,
and message state in the same simulation step. The implemented dirty-state
path keeps the boundary explicit, coalesces those changes in `G_RunClients`,
and still retains the writer guard as a transport invariant for explicit HUD
refresh callers.

The portrait animation has two lifetimes. While
`cinematic_voice_end_time > GetTime()` the frame requests `Portrait Talk`.
After voice expiry it requests `Portrait` until `cinematic_end_time` clears the
scene. This allows Blizzard.j's transmission portrait hang time to remain
visible without forcing the talking animation for the entire hang period.

## Ordinary Text Messages

`DisplayTimedTextToPlayer` stores its JASS X/Y coordinates and explicit
lifetime. `DisplayTextToPlayer` passes the untimed sentinel to `UI_ShowText`,
which derives the current compatibility duration as:

```text
strlen(resolved text) / 6 + 5 seconds
```

The current HUD intentionally retains one **active** ordinary message per
player because `LAYER_MESSAGE` is a single server-authored layer. A new
ordinary message replaces the previous transient message state. Independently,
`UI_ShowText` appends the formatted text to the bounded `message_log` history.
`SetCinematicScene` records the same speaker/dialogue formatting used by the
gameplay transmission overlay only while the player is in gameplay UI mode, so
narrator/tutorial prompts and `DisplayText*` hints share the retained F12
history. Cutscene-mode dialogue and blank cinematic clear calls are not
recorded. `ClearTextMessages` clears only the active ordinary message and
leaves that historical log intact.

Command errors are deliberately different: established Warcraft failures are
resolved from their `CommandStrings` key and written to the dedicated
`WC3_LAYER_COMMAND_ERROR` slot. That slot is independent of `LAYER_MESSAGE`, is not
recorded in Message Log history, and is replaced by the next command failure.
OpenRealm-specific failures without a confirmed Warcraft key use the same
dedicated presentation through `G_ShowCommandErrorText`.

During a gameplay transmission, `LAYER_MESSAGE` belongs to the transmission.
An ordinary message started underneath it retains its own expiry time and is
shown after the transmission only if it has not expired.

## Direct Local-Player Text Calls

`Blizzard.j`'s `MeleeCrippledPlayerTimeout` broadcasts the reveal announcement with
`DisplayTimedTextToPlayer(GetLocalPlayer(), ...)` after leaving its local-player `if` block.
On Bandit Ridge this reached `DisplayTimedTextToPlayer` with both `toPlayer` and `currentplayer`
null and crashed in `PLAYER_ENT`. The VM previously expanded only local-player conditions.
`eval_CALL` now evaluates direct local-player call statements once per Warcraft player slot when
there is no current selector, restoring the unscoped selector afterward. Calls inside an existing
local-player scope remain targeted; explicit `Player(n)` arguments are not broadcast.
This belongs in VM evaluation, rather than interpreting every null player argument as a broadcast
in the text native or substituting the host player.

Reproduce the original timer path with:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' -tft +dedicated 1 +set com_fast_forward 1 +map 'Maps/FrozenThrone/(2)BanditRidge.w3x' +com_frame_limit 2000
```

The same map also runs with ROC archives by omitting `-tft`. A connected host can omit
`+dedicated 1` and use `-vid_hidden 1`. Before the fix the timer caused SIGSEGV; afterward both
modes complete the bounded run. The subsequent `CripplePlayer` native is still unimplemented:
its runtime error is a separate missing reveal feature, not another null-player crash.
`wc3_api.direct_local_player_text_call_reaches_each_player_once` covers coroutine dispatch,
targeted local scope, one announcement per supported player slot, and an explicit target.

## Local Audio

The JASS VM evaluates `GetLocalPlayer()` branches once per represented player
by setting `currentplayer`. `StartSound` must preserve that context:

```text
currentplayer != NULL -> send a reliable owner-only `svc_sound` packet to that player's entity
currentplayer == NULL -> broadcast, preserving global StartSound calls
```

Do not make transmission audio a synchronized world entity merely to target a
force; Blizzard.j already gates transmission presentation/audio with local
player membership.

## Known Gaps

The following transmission limitations remain:

- `PingMinimap` / `PingMinimapEx` now use the shared renderer world-to-minimap
  projection and the WC3 `MinimapIndicator` overlay. `PingMinimapEx` currently
  transports RGB/`extraEffects` but the overlay does not yet apply those extended
  visual parameters. See [alerts-and-minimap-pings.md](alerts-and-minimap-pings.md).
- `AddIndicator` and `UnitAddIndicator` now emit a temporary selection-circle-style indicator. The supplied
  RGBA tint flashes twice using a fixed one-second client lifetime, follows the widget while it is rendered, and
  does not change authoritative selection membership. Blizzard.j's `UnitAddIndicatorBJ` calls `AddIndicator`, so
  both native entry points share this path. Hidden/fogged widgets do not gain visibility merely from an indicator.
- `SetCinematicScene` now applies player colors supported by the renderer to both gameplay and full-cinematic portraits. The wire frame carries the color through `uiFrame_t.stat`, and `SCR_LayoutDrawPortrait` assigns it to the portrait render entity. The renderer still has 16 replaceable team-color textures; extended common.j colors are deliberately normalized to color slot 0 instead of wrapping through the renderer mask.
- Ordinary on-screen text uses one active-message slot rather than Warcraft's
  full multi-message stack. Historical `DisplayText*` entries are retained
  separately by the Message Log.
- `StopSound` / dialogue-specific sound replacement remain unimplemented.

These gaps do not justify coupling dialogue to camera, fog, simulation pause,
or UI mode.

### Nested scripted waits

A wait issued by a native called from a scripted helper such as `WaitForSoundBJ` must suspend the **same** trigger coroutine as a direct `TriggerSleepAction`. The helper frame must remain on that coroutine until the wake time, resume first, and only then return to its caller. Campaign queue helpers rely on this ordering: a narration action can call `WaitForSoundBJ`, inspect its trigger state after the wait, and finally remove itself from Blizzard's queued-trigger list. Treating the scripted helper as a detached/synchronous call, or losing its frame across the yield, can leave the queue permanently blocked even though the dialogue was displayed. In-engine coverage includes a synthetic nested-script sleep regression in `t_game.c`.

## Verification

Relevant in-engine tests are under `games/warcraft-3/game/tests/t_api.c`:

- `wc3_api.disconnected_presentation_defers_network_write_until_connected`
- `wc3_api.display_text_tracks_lifetime_and_clear`
- `wc3_api.transient_text_does_not_enter_message_log`
- `wc3_api.command_error_uses_dedicated_replacing_hud_layer`
- `wc3_api.command_error_key_resolves_commandstrings_and_race_variant`
- `wc3_api.message_log_is_bounded_and_evicts_oldest_entry`
- `wc3_api.display_text_uses_automatic_duration`
- `wc3_api.transmission_keeps_gameplay_ui_and_separates_voice_lifetime` (also covers portrait player-color state and expiry cleanup)
- `wc3_api.gameplay_transmission_preserves_underlying_timed_message_state`

Run when validating changes:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.*'
make test-wc3-engine 2>&1 | tee /tmp/wc3-tests.log
! grep -F "Write buffer overflow (msg):" /tmp/wc3-tests.log
```

For campaign behavior, verify a bounded Human02 run with a transmission that
occurs outside cinematic mode. Confirm the normal command UI remains usable,
the selected portrait is restored after speech, the camera does not move from
the transmission itself, and fog state is unchanged.

## See Also

- [Quest And Message Log UI](quest-and-message-log-ui.md)
- [Server-Authored UI Payloads](../../architecture/ui-payloads.md)
- [JASS Native Coverage](jass-native-coverage.md)

See also: [Alerts And Minimap Pings](alerts-and-minimap-pings.md).

See [portrait animation and destructable hover parity](portrait-and-hover-parity.md)
for the retail comparison and renderer/snapshot regressions.
