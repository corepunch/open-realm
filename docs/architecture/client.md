# Client Architecture

The client (`client/`) is the presentation layer of OpenWarcraft3. In the normal renderer path it owns the SDL2 window, the OpenGL context, the input system, and the connection to the server. It never runs game logic — all simulation takes place on the server.

## Startup

The client is initialised by `CL_Init` in `cl_main.c`, which:

1. Binds the OpenGL renderer through `R_GetAPI`.
2. Initialises the renderer.
3. Initialises the client-side UI library with `M_GetAPI`.
4. Registers client-side console commands.
5. Installs menu or gameplay key bindings depending on startup mode.

Startup mode is selected in `common/main.c` from the `map` and `connect` cvars:

- no `map` and no `connect` — menu mode, client-side UI only
- `map` set — listen-server mode
- `connect` set — remote-client mode

The initial menu command comes from `ui_start_command`, defaulting to `menu_main`.

## Main Loop

`CL_Frame(DWORD msec)` is called once per rendered frame by the platform layer (`common/main.c`). It performs the following steps in order:

```c
void CL_Frame(DWORD msec) {
    cl.time += msec;
    menu.Refresh(msec);   // 1. update active client-side UI screen
    CL_Input();         // 2. sample keyboard / mouse
    CL_ReadPackets();   // 3. apply incoming server messages
    CL_SendCommand();   // 4. execute commands and forward to server
    CL_PrepRefresh();   // 5. build scene for the renderer
    SCR_UpdateScreen(); // 6. draw world, UI, and console
}
```

### 1. CL_ReadPackets

Drains the loopback or UDP receive buffer and dispatches each message by its `svc_*` opcode:

| Opcode | Handler | Effect |
|--------|---------|--------|
| `svc_spawnbaseline` | `CL_ParseBaseline` | Initialise entity `s` field from delta |
| `svc_packetentities` | `CL_ReadPacketEntities` | Apply per-frame entity delta |
| `svc_configstring` | `CL_ParseConfigString` | Update model, image, and font configstrings |
| `svc_frame` | `CL_ParseFrame` | Receive server frame timing |
| `svc_unit_ui` | `CL_ParseUnitUI` | Forward selected-unit data to the UI library |
| `svc_map_list`, `svc_map_info`, `svc_game_list`, `svc_player_list` | list handlers | Feed menu/list UI screens |

### 2. CL_Input

`CL_Input` pumps SDL events through key bindings and shared pan/look/zoom/selection controls. All game builds
use this one implementation. Bindings and independent config options select behavior; there is no RTS/orbit mode.
Controller changes are typed `INPUTCMD` operations: focus XY, orbit Euler/distance, or movement bits/milliseconds.
Game commands such as selection and abilities continue through `clc_stringcmd`.

SDL window events remain client-owned. Events that can change the OpenGL drawable (`MOVED`, `RESIZED`, `SIZE_CHANGED`, and `DISPLAY_CHANGED` on SDL versions that provide it) call the mandatory renderer `WindowChanged` export. The renderer only marks drawable state dirty at that point; the next `R_BeginFrame` re-queries the drawable once after event pumping has completed. This preserves the client/renderer boundary and avoids steady-state drawable polling.

### 3. CL_SendCommand

Input writes `clc_input` to the netchannel message buffer. `CL_SendCommand` flushes queued commands through
loopback or UDP; `SV_ParseClientMessage` validates input and invokes the mandatory game `ClientInput` export on
the client's assigned player edict. The game owns movement and camera constraints. See [shared input](shared-input.md).

### 4. CL_PrepRefresh

Builds the render scene:
- Sets the camera view origin and angles from `cl.viewangles`.
- Calls `CL_AddEntities` which iterates `cl.entities` and calls `V_AddEntity` for each visible entity. Entities with the `EF_NODRAW` flag are skipped.
- Client-side temporary effects (`client/cl_tent.c`) add short-lived visual-only entities such as blood splats and hit sparks.

### 5. SCR_UpdateScreen

Calls into the renderer API:

1. `R_BeginFrame` — clear colour/depth, update matrices.
2. `R_RenderFrame` — draw all entities, terrain, water, particles.
3. `ui.DrawFrame` — draw the active client-side UI screen.
4. Console overlay draws debug text.
5. `R_EndFrame` — present the frame.

## Camera samples

`playerState.viewangles` is the only view orientation on the wire: Euler degrees in `ROTATE_ZYX` order `{pitch, roll, yaw}`. Do not send a parallel quaternion — Euler→quat is lossless, quat→Euler is not.

`CL_ParsePlayerInfo` copies `vieworigin`, `viewangles`, `distance`, `fov`, `znear`, and `zfar` onto `viewDef.camerastate[]`. Clip planes are required camera samples, same as `fov`: every game must author them on `playerState` through `player_set_lens` so FOV and clip cannot be written apart. Zero is a real value, not a keep-previous sentinel. Gameplay defaults live in `CL_GameDefaultCamera` (`WC3_CAMERA_DEFAULT_*`, `WOW_CAMERA_FOV` plus `WOW_WORLD_*_CLIP`, SC2 map camera); Input controls do not invent clip. Every game supplies full focus XYZ, including actor eye height where needed. `Matrix4_getCameraMatrix` converts both snapshots with `Quaternion_fromEuler`, slerps, and builds the orbit view with `Matrix4_fromViewQuat`. Games that previously packed a non-Euler value into a component (SC2 camera height on `z`) must put a real Euler on the snapshot; height belongs in `vieworigin.z`.

Terrain-relative cameras keep exact terrain plus authored offset in each `camerastate` sample. `CL_PredictCameraPosition`
rebases Z by `height(new XY) - height(old XY)` before changing either sample's XY; the same helper handles immediate input and
prediction reapplied to an unacknowledged server snapshot. Changing XY alone used the previous terrain Z as a new authored offset,
which caused a vertical jump when the server acknowledged the move. `playerstate` remains the untouched server sample.

At render time, `Matrix4_getCameraMatrix` extracts both terrain-relative offsets and interpolates those offsets, then adds the
blurred terrain height at the interpolated XY. Using only the latest offset made scripted Z transitions jump at snapshot frequency.
The resulting `viewDef.target` accompanies the projection matrix and is used by drag-plane tracing and shadow focus. The same final
orbit view is inverted once to publish the rendered eye in `viewDef.camerastate[0].eye`; camera-relative effects such as the WC3 sky
model and particles must use that eye rather than the look-at target. `renderer/r_trace.c` owns screen rays and picking. Its drag plane
must use the rendered target, since exact snapshot terrain
can change beneath a camera held steady by the spatial filter. Absolute-height cameras keep ordinary XYZ interpolation.

Regression coverage: `net.camera_prediction_preserves_terrain_offsets` exercises repeated predictions and pending/acknowledged
packets; `client_camera.terrain_offsets_interpolate` checks the actual projection and absolute-height path;
`client_camera.rendered_eye_tracks_orbit_distance` locks the derived eye used by camera-relative rendering;
`renderer_view.pan_plane_uses_rendered_target` checks screen-ray intersections across exact-height changes and a moving rendered
plane. Run `make test` for all three, `make test-client-camera` for the focused projection check, or the in-engine command
`build/bin/openwarcraft3-tests -data build/tests +dedicated 1 +test 'client_camera.*' +com_frame_limit 100`.

WoW authors actor Z plus `WOW_CAMERA_EYE_HEIGHT` in its server focus sample; the client does not replace it.
Native camera angles convert at the game boundary; see [coordinates and camera angles](../../AXIS.md) for the
SC2 azimuth and WoW horizon/heading mappings. The historical `{pitch, roll, yaw}` shorthand describes view Euler
components, not interchangeable native gameplay angles.

## Entity Interpolation

The client keeps two snapshots per entity: `prev` and `current`. `CL_PrepRefresh` blends between them using a fraction derived from `cl.time` and the server frame interval, producing smooth motion even when the client render rate exceeds the server tick rate.

`V_RenderView` keeps a presentation-only scene clock across frames so paused views can submit cached entities with `deltaTime = 0`. Map/session server time can restart from zero while the process and renderer remain alive, so `V_AdvanceSceneTime` treats `now < last` as a new render epoch: it resets `view.time` to `now` and emits zero delta for that frame. The non-active preview path applies the same non-wrapping elapsed-time rule. Never subtract a retained `DWORD` scene timestamp from a newly reset map clock without this epoch check; unsigned wrap otherwise looks like roughly `2^32` milliseconds and can burst particles/animations/effects for one frame. The regression is covered by `net.scene_time_rewind_starts_a_new_render_epoch`.

## Console and HUD

`cl_console.c` maintains an in-game console that can be toggled with the tilde key. `cl_scrn.c` coordinates frame presentation and the UI draw pass.

Server-authored layout controls normally dispatch their `onclick` command on left-button release. A command whose first character is
`+` follows the Quake held-command convention instead: `cl_scrn.c` dispatches it on mouse-down, captures the formatted command, and
sends the same command with a leading `-` on mouse-up even if the pointer has left the frame. This is generic input transport; game
modules may use it for press-and-hold controls without adding game-specific branches to `client/`.

Configstrings are also live server state, not connect-time-only metadata. A game-side `gi.configstring()` call reaches
`PF_Confignstring()`, which delegates storage to the generic server-owned `SV_SetConfigString()`. That helper clears
`sv.syncstrings[index]`; `SV_FindIndex()` uses the same path when it creates model/sound/image entries. `SV_QueuePendingConfigStrings()`
appends each unsynced value to every current client's reliable message before the normal spawned-client datagram. `PF_Unicast()` calls
the same helper before appending an immediate game payload, because layout authoring can allocate an image/font/model index and then
unicast a frame that references it in the same game callback. This guarantees the `svc_configstring` precedes the dependent layout in
the same outbound message instead of leaving a first-use frame temporarily bound to an unregistered slot.
`CL_ParseConfigString` keeps the already-loaded model/image handle when the resent path is unchanged; begin and same-map load
used to `ReleaseModel`/`LoadModel` every slot. A changed or newly filled slot still binds immediately
so late presentation models (after `CL_PrepRefresh`) stay coherent. Do not mutate `sv.configstrings[]` directly for runtime values,
or already-connected clients may retain stale data. Keeping the mutation helper in `server/` also lets standalone server-network
tests cover the real resync implementation without linking the game-import wrapper in `sv_game.c`.

Low fixed configstrings may also carry compact references to resources already owned by a normal precache pool. For example, the
optional generic environment-light slots `CS_TERRAIN_LIGHT_MODEL` and `CS_ENTITY_LIGHT_MODEL` contain decimal `CS_MODELS` indices;
they do not duplicate model paths or create a second loader. The client resolves those references to model handles and copies the
generic `UI_PLAYERSTAT_ENV_PHASE` clock onto `viewDef_t`. It does not include a game header to do that. The active game renderer
evaluates those optional inputs into `ENVIRONLIGHT` samples (`terrainLight` / `entityLight`); draw paths consume the samples, not
the MDX handles. See [Environment Lighting](environment-lighting.md).

`CS_SCENE_FOG` is another low fixed generic presentation contract. Its payload is `style start end density r g b`; style 0 disables
scene-distance fog and positive styles enable it. `client/cl_view.c` parses the payload without game headers and reduces enabled values
to `viewDef_t`'s generic linear `fogStart` / `fogEnd` / `fogColor` fields. Richer producer-owned style/density semantics stay outside
the client until the renderer has a proven generic equation for them. Game renderers decide which world surfaces consume the view
sample; see WC3 [Environmental Terrain Fog](../games/warcraft-3/environmental-fog.md) for the first producer.

Server-authored text frames may also bind to live snapshot values instead of forcing a complete layout resend whenever a number changes.
`playerState.stats[16]` is the generic environment/day-phase clock (`UI_PLAYERSTAT_ENV_PHASE`).
`playerState.stats[18..21]` are reserved generic selection-UI slots (current/max health and current/max mana), serialized as the two
existing packed `NFT_LONG` stat pairs. `UI_STAT_SELECTION_HEALTH_TEXT` and `UI_STAT_SELECTION_MANA_TEXT` make `SCR_GetStringValue()`
format those values at draw time. Game modules may populate these reserved slots for a sole-selected entity; client layout code must keep
the binding generic rather than looking up game-specific entity types or rules.

`playerState.stats[23]` is the generic environment-presentation variant (`UI_PLAYERSTAT_ENV_VARIANT`). `FT_SPRITE` may opt into
`UIFLAG_SPRITE_STAT_SEQUENCE`; in that mode `frame.value` names the secondary `stats[]` slot whose value replaces an explicit `#N`
sequence selector before the ordinary `frame.stat` normalized `@ratio` phase is appended. This keeps variant selection generic and
snapshot-driven: the client does not need to know why a game chose sequence 0 versus sequence 1.

## Key Files

| File | Purpose |
|------|---------|
| `client/cl_main.c` | `CL_Frame`, `CL_Init`, `CL_ReadPackets` |
| `client/cl_input.c` | Input sampling, shared `+select` / `zoom` / `group` |
| `client/cl_parse.c` | Server message handlers |
| `client/cl_view.c` | Camera, `CL_PrepRefresh`, `V_AddEntity` |
| `client/cl_tent.c` | Temporary client-side effects |
| `client/cl_scrn.c` | Screen update and UI draw pass |
| `client/cl_console.c` | In-game console |
| `client/keys.c` | Key event dispatch and modifier-aware binding table (`bind SHIFT+1`) |
| `client/cl_control_groups.c` | Numbered control groups on `cl.groups`; `group` / `group add` / `group assign` |
| `common/net.c` | Loopback transport shared by client and server |

## See Also

- [Server-selected effects](server-selected-effects.md) — generic camera samples on the 32-bit player-state mask
- [WC3 cinematics](../games/warcraft-3/cinematics.md) — server Euler lerp, client quat slerp
