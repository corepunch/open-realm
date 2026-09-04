# Warcraft III Triggered Dialogue

## Contract

Normal Warcraft III transmissions are presentation state, not cinematic mode.
`SetCinematicScene` may run while `PLAYER.client_ui_state == CLIENT_UI_GAME` and
must not itself move the camera, alter fog, pause simulation, hide the HUD, or
disable user control. Full cinematic policy remains owned by `ShowInterface`,
`EnableUserControl`, camera natives, and the map script.

The server owns transmission/message state in `games/warcraft-3/game/client_s`:

- `ps.cinematic_portrait` — model index used by the talking-head portrait (`BYTE`, packed with team/color/race).
- `ps.stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR]` — requested player-color index for the portrait render entity.
- `PLAYERTEXT_SPEAKER` / `PLAYERTEXT_DIALOGUE` — resolved map strings.
- `cinematic_voice_end_time` — end of `Portrait Talk`; the scene may remain.
- `cinematic_end_time` — end of the complete transmission scene.
- `message` — one ordinary transient `DisplayText*` message, including
  position and expiry time.
- `message_log` — bounded historical `DisplayText*` entries used by the
  single-player Message Log; its lifetime is independent from `message`.

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
`ClearTextMessages` clears only the active message and leaves that historical
log intact.

Command errors are deliberately different: `G_ShowCommandErrorText` uses
`UI_ShowTransientText`, which shares the transient presentation path without
recording the text in Message Log history.

During a gameplay transmission, `LAYER_MESSAGE` belongs to the transmission.
An ordinary message started underneath it retains its own expiry time and is
shown after the transmission only if it has not expired.

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
- `UnitAddIndicator` / `AddIndicator` are still stubs. A speaking-unit marker
  needs per-client lifetime/color state and must not reveal a fogged unit.
- `SetCinematicScene` now applies player colors supported by the renderer to both gameplay and full-cinematic portraits. The wire frame carries the color through `uiFrame_t.stat`, and `SCR_LayoutDrawPortrait` assigns it to the portrait render entity. The renderer still has 16 replaceable team-color textures; extended common.j colors are deliberately normalized to color slot 0 instead of wrapping through the renderer mask.
- Ordinary on-screen text uses one active-message slot rather than Warcraft's
  full multi-message stack. Historical `DisplayText*` entries are retained
  separately by the Message Log.
- `StopSound` / dialogue-specific sound replacement remain unimplemented.

These gaps do not justify coupling dialogue to camera, fog, simulation pause,
or UI mode.

## Verification

Relevant in-engine tests are under `games/warcraft-3/game/tests/t_api.c`:

- `wc3_api.disconnected_presentation_defers_network_write_until_connected`
- `wc3_api.display_text_tracks_lifetime_and_clear`
- `wc3_api.transient_command_style_text_does_not_enter_message_log`
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
