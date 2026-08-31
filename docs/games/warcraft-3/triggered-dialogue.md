# Warcraft III Triggered Dialogue

## Contract

Normal Warcraft III transmissions are presentation state, not cinematic mode.
`SetCinematicScene` may run while `PLAYER.client_ui_state == CLIENT_UI_GAME` and
must not itself move the camera, alter fog, pause simulation, hide the HUD, or
disable user control. Full cinematic policy remains owned by `ShowInterface`,
`EnableUserControl`, camera natives, and the map script.

The server owns transmission/message state in `games/warcraft-3/game/client_s`:

- `ps.cinematic_portrait` — model index used by the talking-head portrait.
- `PLAYERTEXT_SPEAKER` / `PLAYERTEXT_DIALOGUE` — resolved map strings.
- `cinematic_voice_end_time` — end of `Portrait Talk`; the scene may remain.
- `cinematic_end_time` — end of the complete transmission scene.
- `message` — one ordinary `DisplayText*` message, including position and
  expiry time.

`G_SetPlayerText` preserves an actual empty string. Do not normalize cinematic
clear state to a single space: `UI_WriteCinematicLayer` and the gameplay
transmission path use empty strings to decide whether a scene exists.

## Presentation Flow

`SetCinematicScene`

```text
resolve TRIGSTR speaker/text
  -> resolve portrait unit type to model
  -> store scene and voice expiry independently
  -> UI_WriteDialoguePresentation(player entity)
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

The current HUD intentionally retains one ordinary message per player because
`LAYER_MESSAGE` is a single server-authored layer. A new ordinary message
replaces the previous ordinary message state. `ClearTextMessages` clears that
state for the current local-player JASS context; when no local-player context
exists it clears all player clients, matching a global invocation in the
server-side multi-client VM.

During a gameplay transmission, `LAYER_MESSAGE` belongs to the transmission.
An ordinary message started underneath it retains its own expiry time and is
shown after the transmission only if it has not expired.

## Local Audio

The JASS VM evaluates `GetLocalPlayer()` branches once per represented player
by setting `currentplayer`. `StartSound` must preserve that context:

```text
currentplayer != NULL -> send "snd" only to that player's entity
currentplayer == NULL -> broadcast, preserving global StartSound calls
```

Do not make transmission audio a synchronized world entity merely to target a
force; Blizzard.j already gates transmission presentation/audio with local
player membership.

## Known Gaps

The following transmission features remain separate work because the current
engine has no established representation for them:

- `PingMinimap` / `PingMinimapEx` are still stubs. A correct implementation
  should reuse the renderer's existing world-to-minimap conversion rather than
  duplicate map-coordinate math in JASS/UI code.
- `UnitAddIndicator` / `AddIndicator` are still stubs. A speaking-unit marker
  needs per-client lifetime/color state and must not reveal a fogged unit.
- `SetCinematicScene` player color is not yet applied to the portrait. The MDX
  renderer currently has a fixed `MAX_TEAMS` texture table, while common.j
  exposes more player colors; do not silently wrap unsupported colors.
- Ordinary text uses one active-message slot rather than Warcraft's full
  multi-message stack.
- `StopSound` / dialogue-specific sound replacement remain unimplemented.

These gaps do not justify coupling dialogue to camera, fog, simulation pause,
or UI mode.

## Verification

Relevant in-engine tests are under `games/warcraft-3/game/tests/t_api.c`:

- `wc3_api.display_text_tracks_lifetime_and_clear`
- `wc3_api.display_text_uses_automatic_duration`
- `wc3_api.transmission_keeps_gameplay_ui_and_separates_voice_lifetime`
- `wc3_api.gameplay_transmission_preserves_underlying_timed_message_state`

Run when validating changes:

```sh
make test-wc3-engine WC3_PATTERN='wc3_api.*'
```

For campaign behavior, verify a bounded Human02 run with a transmission that
occurs outside cinematic mode. Confirm the normal command UI remains usable,
the selected portrait is restored after speech, the camera does not move from
the transmission itself, and fog state is unchanged.
