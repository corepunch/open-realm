# Client Architecture

The client (`client/`) is the presentation layer of OpenWarcraft3. In the normal renderer path it owns the SDL2 window, the OpenGL context, the input system, and the connection to the server. It never runs game logic — all simulation takes place on the server.

## Startup

The client is initialised by `CL_Init` in `cl_main.c`, which:

1. Chooses a renderer API from `r_module`.
2. Initialises the selected renderer.
3. Initialises the client-side UI library with `UI_GetAPI`.
4. Registers client-side console commands.
5. Installs menu or gameplay key bindings depending on startup mode.

Startup mode is selected in `common/main.c` from the `map` and `connect` cvars:

- no `map` and no `connect` — menu mode, client-side UI only
- `map` set — listen-server mode
- `connect` set — remote-client mode

The initial menu command comes from `ui_start_command`, defaulting to `menu_main`.

Renderer choices:

- `r_module=renderer` — normal OpenGL renderer
- `r_module=stdout` or `r_module=text` — text renderer for one-frame UI diagnostics

## Main Loop

`CL_Frame(DWORD msec)` is called once per rendered frame by the platform layer (`common/main.c`). It performs the following steps in order:

```c
void CL_Frame(DWORD msec) {
    cl.time += msec;
    ui.Refresh(msec);   // 1. update active client-side UI screen
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
5. `R_EndFrame` — present the frame or flush the stdout renderer.

With `r_module=stdout`, the same calls produce text lines such as `draw_image`, `draw_text`, and `draw_portrait` instead of pixels. This is useful for scriptable UI checks:

```bash
make run-ui-text
```

## Entity Interpolation

The client keeps two snapshots per entity: `prev` and `current`. `CL_PrepRefresh` blends between them using a fraction derived from `cl.time` and the server frame interval, producing smooth motion even when the client render rate exceeds the server tick rate.

## Console and HUD

`cl_console.c` maintains an in-game console that can be toggled with the tilde key. `cl_scrn.c` coordinates frame presentation and the UI draw pass.

## Key Files

| File | Purpose |
|------|---------|
| `client/cl_main.c` | `CL_Frame`, `CL_Init`, `CL_ReadPackets` |
| `client/cl_input.c` | Input sampling, `usercmd_t` construction |
| `client/cl_parse.c` | Server message handlers |
| `client/cl_view.c` | Camera, `CL_PrepRefresh`, `V_AddEntity` |
| `client/cl_tent.c` | Temporary client-side effects |
| `client/cl_scrn.c` | Screen update and UI draw pass |
| `client/cl_console.c` | In-game console |
| `client/keys.c` | Key event dispatch and binding table |
| `common/net.c` | Loopback transport shared by client and server |
| `renderer/r_stdout.c` | Text renderer backend for draw-call diagnostics |
