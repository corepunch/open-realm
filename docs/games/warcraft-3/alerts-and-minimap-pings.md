# Alerts, Recent Transmissions, And Minimap Pings

## Contract

Warcraft III location-bearing notifications are local presentation layered over authoritative simulation events. OpenRealm keeps four concerns separate:

```text
simulation event / JASS native
    -> game-owned reliable GameCommand payload
    -> WC3 UI alert state
       -> temporary minimap indicator
       -> eight-entry recent-alert history
    -> gameplay Space key asks the UI for a camera target
    -> generic RTS client applies the normal camera-position path
```

The server does not move the camera when an alert is emitted, and a ping does not change fog or selection. Recent-alert state is client-local and is not added to `playerState_t`/`entityState_t`.

The game/UI wire payload is `wc3MinimapPing_t` in `games/warcraft-3/common/alerts.h`. It is deliberately game-local; `client/cl_parse.c` treats `svc_game_command` data as opaque and forwards it through `ui.GameCommand`.

## Producers

`G_SendMinimapPing()` in `game/g_sound.c` resolves `MinimapIndicator` from the recipient's active `war3skins.txt` section, falling back to `UI\\Minimap\\Minimap-Ping.mdl`, then sends `minimap_ping` to only that player's entity.

The following high-confidence gameplay completions also call `G_SendOwnerMinimapAlert()` and therefore enter recent-alert history:

| Event | Stored location |
| --- | --- |
| construction completes | completed building `s.origin2` |
| unit training completes | completed unit `s.origin2` |
| research completes | researching producer `s.origin2` |

The completion path currently uses a one-second alert-ping lifetime. That is an implementation default, not a measured retail constant; do not treat it as proven Warcraft timing.

Under-attack/town/allied alert production is not implemented yet. Its throttling/coalescing policy is not established in the current codebase and should be measured before adding it rather than emitting one alert per damage event. Hero-revive and building-morph completion alerts are likewise separate work.

## JASS Minimap Pings

`PingMinimap(x, y, duration)` now sends `minimap_ping` through the same renderer path but does **not** add an automatic recent-alert entry. In a `GetLocalPlayer()` context OpenRealm targets that represented player; with no local-player context it sends the presentation to all connected game clients, matching a native invoked on every retail client.

`PingMinimapEx(x, y, duration, red, green, blue, extraEffects)` is registered and transports the clamped RGB values and `extraEffects` flag in `wc3MinimapPing_t`. The current generic sprite export has no tint/extra-effects parameter, so the WC3 overlay presently draws the configured authored model without applying those extended visual parameters. Keep the payload fields: they preserve the JASS contract for a later renderer extension without another wire-format redesign.

## Minimap Projection And Drawing

`refExport_t.WorldToMinimap` is the renderer-side inverse companion of `TraceMinimap`. `R_WorldToMinimap()` calls the same `R_MinimapPointForWorld()` helper used by normal minimap drawing, so pings and clicks share one world/minimap transform instead of duplicating map-coordinate math in JASS or UI code.

`games/warcraft-3/ui/ui_alerts.c` stores up to 16 simultaneously active visual pings. Sixteen is an OpenRealm implementation cap, not a retail Warcraft constant. When all slots are occupied, the oldest active visual ping is replaced.

Ping lifetime uses `UI_GetTime()` backed by the client clock exposed through `uiImport_t.GetTime`, not the menu/glue `ui.Refresh()` timestamp. During normal `ca_active` gameplay `ui.Refresh()` is not called every frame, so using only `ui_state.time` would freeze the elapsed-time calculation and leave minimap indicators active indefinitely.

The WC3 UI draws pings through `ui.DrawGameOverlay` after the server-authored HUD. This is an intentional in-game `ui.dll` exception: `svc_layout` cannot express a transient authored MDX whose screen point is derived from client-local world-to-minimap projection. Keep this exception isolated to the alert overlay; do not move WC3 asset names or alert semantics into the shared HUD/client.

`MDLX_DrawSpriteTinted()` temporarily replaces `tr.viewDef`. Because minimap pings are drawn after the world, it must restore the previous `tr.viewDef` after its sprite pass; otherwise a post-world sprite can corrupt renderer state expected by subsequent HUD/overlay work.

## Recent Alert History And Space

The client-local WC3 UI keeps the latest `WC3_RECENT_ALERT_COUNT == 8` remembered alert positions. New entries are inserted newest-first and evict the oldest when full.

Space behaviour is:

```text
new alert A, then B, then C
Space -> C
Space -> B
Space -> A
Space -> C  (wrap)
```

If a new alert arrives during traversal, the cursor resets so the next Space goes to that newest alert. SDL key-repeat is consumed without advancing the cursor, so holding Space does not race through the history.

The game-specific UI handles Space through the generic `ui.GameplayKeyEvent` hook. It returns `UI_GAMEKEY_CAMERA_POSITION` with a world coordinate; `client/cl_input_w3.c` applies that coordinate through its existing `CL_SetCameraPosition()` path, preserving local prediction, camera-bounds clamping, and the normal `clc_camera_position` server update. The UI does not select the source unit and does not retain a live entity pointer; stored X/Y remains valid if the source later dies or is removed.

## `SetCameraQuickPosition`

`SetCameraQuickPosition` remains non-moving server-side state in `game/api/api_camera.h`. It records `client->camera.quick_position` and does not move the current camera.

If the automatic eight-entry alert history is empty, the WC3 UI sends the game-owned `quickcamera` client command. `CMD_QuickCamera` reads the authoritative server-side quick position and applies it through `G_ClientSetCameraPosition()`. Automatic alert history takes priority while it contains entries. This precedence is a conservative OpenRealm policy; the exact retail interaction between explicit `SetCameraQuickPosition` calls and the built-in transmission history has not been established and should be compatibility-tested before changing it.

`CL_ClearState()` calls the generic `ui.ClearGameState` callback. WC3 uses it to clear active pings, history, and traversal state across disconnect/map-state resets; scripted quick-position state remains server-owned.

## Engine Boundary

Shared additions are intentionally content-neutral:

- `ui.GameplayKeyEvent` returns generic handled/camera-target flags.
- `ui.ClearGameState` clears game-owned transient client state.
- `refExport_t.WorldToMinimap` projects a world point through the renderer's current minimap.

Warcraft-specific payloads, command names, history limits, asset fallbacks, and alert semantics remain under `games/warcraft-3/`.

## Known Gaps

- under-attack, town-under-attack and allied alert producers/throttling;
- hero-revive and building-morph completion alert policy;
- `PingMinimapEx` RGB tint and `extraEffects` visual treatment;
- verified retail timing/animation choice and ping-relative MDX animation phase for automatic completion pings;
- exact retail precedence between automatic transmission history and `SetCameraQuickPosition`.

Do not fill these gaps by guessing constants or emitting damage alerts every hit.

## Verification

Automated UI coverage in `games/warcraft-3/tests/test_ui_fdf.c` checks the eight-entry eviction/cycle order, reset-on-new-alert behaviour, that plain scripted pings do not enter recent-alert history, and scripted quick-position fallback. `games/warcraft-3/game/tests/t_game.c` covers the owner-targeted `minimap_ping` game-command payload and the disconnected-client inverse path. The task that introduced this document intentionally did not compile or run tests at the requester's direction.

Recommended runtime checks when testing manually:

1. Complete a building off-screen, confirm the minimap indicator, then press Space and verify the camera centres on the building without changing selection.
2. Complete more than eight location-bearing notifications and verify Space walks newest to oldest, retaining only the latest eight.
3. Complete a trained unit and verify the remembered point is the spawned unit location.
4. Complete research and verify the remembered point is the researching building.
5. Call `PingMinimap` with an arbitrary map point/duration and verify the indicator aligns with normal minimap entities and expires independently of recent-alert history.
6. Call `SetCameraQuickPosition` with no automatic alert history and verify assignment does not move the camera, then Space recalls it.
7. Verify a ping in fog does not reveal terrain or units.

See also: [Triggered Dialogue](triggered-dialogue.md), [Cinematics And Camera](cinematics.md), [Sounds](sounds.md), and [Server-Selected Presentation Effects](../../architecture/server-selected-effects.md).
