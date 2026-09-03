# Alerts, Recent Transmissions, And Minimap Pings

## Contract

Warcraft III location-bearing notifications are local presentation layered over authoritative simulation events. OpenRealm keeps four concerns separate:

```text
simulation event / JASS native
    -> game calls generic gi.MinimapPing
    -> reliable svc_minimap_ping packet
    -> client/cl_minimap.c
       -> temporary minimap indicator
       -> eight-entry recent-alert history
       -> Space recall through the normal camera-position path
```

The server does not move the camera when an alert is emitted, and a ping does not change fog or selection. Recent-alert state is client-local and is not added to `playerState_t`/`entityState_t`.

The wire contract is the generic `svc_minimap_ping` message: world position, lifetime, RGBA color, and behavior flags. The authored alert model name is the server-owned `CS_MINIMAP` configstring, following Quake's `CS_SKY` pattern; `client/cl_minimap.c` loads it through the normal configstring lifecycle. No minimap state belongs to a game UI library.

Ordinary minimap unit dots are not pings. They are derived every frame from replicated entities by each game's renderer. A ping is a transient attention event, analogous to `svc_sound`, and therefore does not widen `entityState_t` or survive save/load.

## Producers

`G_SendMinimapPing()` in `game/g_minimap.c` resolves `MinimapIndicator` from the recipient's active `war3skins.txt` section and publishes the model path through `CS_MINIMAP` before sending the generic ping packet. An empty model path leaves the client with its generic colored marker.

The following high-confidence gameplay completions also call `G_SendOwnerMinimapAlert()` and therefore enter recent-alert history:

| Event | Stored location |
| --- | --- |
| construction completes | completed building `s.origin2` |
| unit training completes | completed unit `s.origin2` |
| research completes | researching producer `s.origin2` |

The completion path currently uses a one-second alert-ping lifetime. That is an implementation default, not a measured retail constant; do not treat it as proven Warcraft timing.

Under-attack/town/allied alert production is not implemented yet. Its throttling/coalescing policy is not established in the current codebase and should be measured before adding it rather than emitting one alert per damage event. Hero-revive and building-morph completion alerts are likewise separate work.

## JASS Minimap Pings

`PingMinimap(x, y, duration)` sends the generic packet but does **not** add an automatic recent-alert entry. In a `GetLocalPlayer()` context OpenRealm targets that represented player; with no local-player context it sends the presentation to all connected game clients, matching a native invoked on every retail client.

`PingMinimapEx(x, y, duration, red, green, blue, extraEffects)` transports clamped RGB values and `MINIMAP_PING_EXTRA_EFFECTS`. The generic marker uses the color and adds an outer pulse for extra effects. An authored model draws its own materials and animation, so packet tint does not override that model.

## Minimap Projection And Drawing

`refExport_t.WorldToMinimap` is the renderer-side inverse companion of `TraceMinimap`. `R_WorldToMinimap()` calls the same `R_MinimapPointForWorld()` helper used by minimap camera/click projection, so pings and clicks share one world/minimap transform instead of duplicating map-coordinate math in JASS or UI code.

For rectangular Warcraft maps, world-space minimap content must **not** be stretched across the whole square HUD frame. Warsmash computes a centred `minimapFilledArea` using `max(worldWidth, worldHeight)`: the authored minimap texture still fills the complete frame, while fog, units, camera geometry, clicks, and alert pings use the aspect-preserving content rectangle. OpenRealm mirrors that contract through `WC3_MinimapContentRect()` and stores that rectangle in `tr.minimapRect` after WC3 draws the minimap. Without this inset, the alert's world coordinate is correct (so Spacebar recall is correct) but its visual ping is displaced relative to the authored map/fog on non-square maps.

`client/cl_minimap.c` stores up to 16 simultaneously active visual pings. Sixteen is an OpenRealm implementation cap, not a retail Warcraft constant. When all slots are occupied, the oldest active visual ping is replaced. Lifetime uses the normal advancing `cl.time` clock.

`FT_MINIMAP` invokes `CL_LayoutDrawMinimap()`, which first asks the game renderer to draw terrain, fog, entities, and camera bounds, then draws active attention markers. A nonzero model index uses the registered authored model. Model zero, or an unavailable model after a logged warning, uses the generic colored cross/pulse.

`MDLX_DrawSpriteTinted()` temporarily replaces `tr.viewDef`. Because minimap pings are drawn after the world, it must restore the previous `tr.viewDef` after its sprite pass; otherwise a post-world sprite can corrupt renderer state expected by subsequent HUD/overlay work.

## Recent Alert History And Space

The generic minimap client keeps the latest eight positions from packets carrying `MINIMAP_PING_REMEMBER`. New entries are inserted newest-first and evict the oldest when full.

Space behaviour is:

```text
new alert A, then B, then C
Space -> C
Space -> B
Space -> A
Space -> C  (wrap)
```

If a new alert arrives during traversal, the cursor resets so the next Space goes to that newest alert. SDL key-repeat is consumed without advancing the cursor, so holding Space does not race through the history.

`CL_MinimapKeyEvent()` consumes Space only while recent history exists, then calls `CL_SetCameraPosition()`, preserving local prediction, replicated camera-bounds clamping, and the normal `clc_camera_position` server update. It stores coordinates rather than entity pointers, so a source may die or be removed without invalidating history.

## `SetCameraQuickPosition`

`SetCameraQuickPosition` remains non-moving server-side state in `game/api/api_camera.h`. It records `client->camera.quick_position` and does not move the current camera.

When recent history is empty, the minimap handler leaves Space unconsumed. WC3's normal `SPACE "cmd quickcamera"` binding then invokes `CMD_QuickCamera`, which reads the authoritative server-side quick position and applies it through `G_ClientSetCameraPosition()`. Automatic alert history therefore retains priority without a game-specific branch in client code.

`CL_ClearState()` calls `CL_ClearMinimap()` directly to clear active pings, history, and dragging across disconnect/map resets; scripted quick-position state remains server-owned.

## Engine Boundary

Shared additions are intentionally content-neutral: `game_import.MinimapPing` sends `svc_minimap_ping`, `CL_ParseMinimapPing` owns transient state, and `refExport_t.WorldToMinimap` projects a world point through the renderer's current minimap. Warcraft-specific producers, skin lookup, and alert policy remain under `games/warcraft-3/`.

## Known Gaps

- under-attack, town-under-attack and allied alert producers/throttling;
- hero-revive and building-morph completion alert policy;
- verified retail timing/animation choice and ping-relative MDX animation phase for automatic completion pings;
- exact retail precedence between automatic transmission history and `SetCameraQuickPosition`.

Do not fill these gaps by guessing constants or emitting damage alerts every hit.

## Verification

`tests/test_net.c` covers valid and truncated `svc_minimap_ping` packets. `games/warcraft-3/game/tests/t_game.c` covers owner targeting, duration/color/flags, optional model registration, and the disconnected-client inverse path.

Recommended runtime checks when testing manually:

1. Complete a building off-screen, confirm the minimap indicator, then press Space and verify the camera centres on the building without changing selection.
2. Complete more than eight location-bearing notifications and verify Space walks newest to oldest, retaining only the latest eight.
3. Complete a trained unit and verify the remembered point is the spawned unit location.
4. Complete research and verify the remembered point is the researching building.
5. On a rectangular map, call `PingMinimap` at several known world points and verify the indicator aligns with the authored minimap/fog reveal inside the centred aspect-preserving content area, then verify it expires independently of recent-alert history.
6. Call `SetCameraQuickPosition` with no automatic alert history and verify assignment does not move the camera, then Space recalls it.
7. Verify a ping in fog does not reveal terrain or units.

See also: [Triggered Dialogue](triggered-dialogue.md), [Cinematics And Camera](cinematics.md), [Sounds](sounds.md), and [Server-Selected Presentation Effects](../../architecture/server-selected-effects.md).
