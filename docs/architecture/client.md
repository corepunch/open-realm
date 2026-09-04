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

`CL_Input` (in `cl_input.c`) reads the current SDL2 keyboard and mouse state and fills a `usercmd_t` struct:

```c
typedef struct {
    DWORD  msec;
    DWORD  buttons;   // bitmask of pressed buttons
    SHORT  forwardmove;
    SHORT  sidemove;
    float  angles[3]; // camera Euler angles
} usercmd_t;
```

Camera rotation is applied to the stored `cl.viewangles` every frame. Mouse button events are translated to `BUTTON_*` bitmask bits. Keyboard events produce `CL_KeyEvent` calls which feed the console and keyboard bindings.

### 3. CL_SendCommand

Serialises the current `usercmd_t` as a `clc_move` message and writes it to the loopback send buffer for the server to read on its next `SV_ReadPackets` call.

For higher-level actions (right-click, ability use, unit selection), the input code sends dedicated `clc_*` messages that the selected game's command dispatcher handles. In the Warcraft III build this is `games/warcraft-3/game/g_commands.c`.

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

`CL_ParsePlayerInfo` copies `vieworigin`, `viewangles`, `distance`, `fov`, `znear`, and `zfar` onto `viewDef.camerastate[]`. Clip planes are required camera samples, same as `fov`: every game must author them on `playerState` through `player_set_lens` so FOV and clip cannot be written apart. Zero is a real value, not a keep-previous sentinel. Gameplay defaults live in `CL_GameDefaultCamera` (`WC3_CAMERA_DEFAULT_*`, `WOW_CAMERA_FOV` plus `WOW_WORLD_*_CLIP`, SC2 map camera); `CL_InputModeSetGameplay` does not invent clip. `Matrix4_getCameraMatrix` converts both snapshots with `Quaternion_fromEuler`, slerps, and builds the orbit view with `Matrix4_fromViewQuat`. Games that previously packed a non-Euler value into a component (SC2 camera height on `z`) must put a real Euler on the snapshot; height belongs in `vieworigin.z`.

WoW still replaces look-at Z from the local player entity (`WOW_CAMERA_EYE_HEIGHT`); that is not an orientation sample.

## Entity Interpolation

The client keeps two snapshots per entity: `prev` and `current`. `CL_PrepRefresh` blends between them using a fraction derived from `cl.time` and the server frame interval, producing smooth motion even when the client render rate exceeds the server tick rate.

## Console and HUD

`cl_console.c` maintains an in-game console that can be toggled with the tilde key. `cl_scrn.c` coordinates frame presentation and the UI draw pass.

Server-authored layout controls normally dispatch their `onclick` command on left-button release. A command whose first character is
`+` follows the Quake held-command convention instead: `cl_scrn.c` dispatches it on mouse-down, captures the formatted command, and
sends the same command with a leading `-` on mouse-up even if the pointer has left the frame. This is generic input transport; game
modules may use it for press-and-hold controls without adding game-specific branches to `client/`.

Configstrings are also live server state, not connect-time-only metadata. A game-side `gi.configstring()` call reaches
`PF_Confignstring()`, which delegates storage to the generic server-owned `SV_SetConfigString()`. That helper clears
`sv.syncstrings[index]`; `SV_FindIndex()` uses the same path when it creates model/sound/image entries. At the start of a later
`SV_SendClientMessages()` pass, each unsynced entry is sent reliably with `svc_configstring` before normal spawned-client datagrams.
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

Server-authored text frames may also bind to live snapshot values instead of forcing a complete layout resend whenever a number changes.
`playerState.stats[16]` is the generic environment/day-phase clock (`UI_PLAYERSTAT_ENV_PHASE`).
`playerState.stats[18..21]` are reserved generic selection-UI slots (current/max health and current/max mana), serialized as the two
existing packed `NFT_LONG` stat pairs. `UI_STAT_SELECTION_HEALTH_TEXT` and `UI_STAT_SELECTION_MANA_TEXT` make `SCR_GetStringValue()`
format those values at draw time. Game modules may populate these reserved slots for a sole-selected entity; client layout code must keep
the binding generic rather than looking up game-specific entity types or rules.

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
