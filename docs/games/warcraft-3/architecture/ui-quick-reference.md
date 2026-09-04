# UI System Quick Reference

This is the short version of the current client-side UI architecture.

## System Overview

```text
Mouse/keyboard
  -> client input
  -> games/warcraft-3/menu/menu_main.c
  -> games/warcraft-3/menu/screens/*.c
  -> games/warcraft-3/menu/menu_render.c
  -> renderer API
```

The UI library parses Warcraft III FDF files client-side, owns the frame tree, and switches screens through menu commands. The server provides game data only, such as command buttons and inventory items.

## Startup

`CL_Init` creates the renderer and UI function tables:

1. Bind the OpenGL renderer through `R_GetAPI`.
2. Initialise the renderer.
3. Initialise `UI_GetAPI`.
4. Load theme and FDF assets.
5. Execute `ui_start_command`, default `menu_main`.

Common commands:

| Command | Purpose |
|---------|---------|
| `menu_main` | Main menu |
| `menu_game` | Single-player menu |
| `menu_lan_refresh` | LAN game list |
| `menu_startserver` | Create LAN game |

## Unit Selection Flow

```text
client/cl_input.c
  CL_RequestUnitUI
  -> clc_request_unit_ui
  -> server/sv_unit_ui.c
  -> games/warcraft-3/game/g_unit_ui.c
  -> svc_unit_ui
  -> client/cl_unit_ui.c
  -> ui.UpdateUnitUI
  -> games/warcraft-3/menu/screens/console_ui.c
```

The client caches returned unit data and renders it on subsequent UI frames.

## Core Files

| File | Purpose |
|------|---------|
| `client/ui.h` | Shared UI module API declaration |
| `games/warcraft-3/menu/menu_main.c` | UI entry point, lifecycle, startup command, screen selection |
| `games/warcraft-3/menu/menu_fdf.c` | FDF parsing and frame registry |
| `games/warcraft-3/menu/menu_render.c` | Layout solving and frame rendering |
| `games/warcraft-3/menu/menu_theme.c` | Warcraft UI theme resources |
| `games/warcraft-3/menu/screens/main_menu.c` | Main menu screen |
| `games/warcraft-3/menu/screens/console_ui.c` | In-game HUD screen |
| `client/cl_main.c` | Renderer/UI init and client frame loop |
| `client/cl_unit_ui.c` | `svc_unit_ui` parser |
| `server/sv_unit_ui.c` | Unit UI data request handler |
| `games/warcraft-3/game/g_unit_ui.c` | Game-side unit UI data provider |

## Runtime Cvars

| cvar | Purpose |
|------|---------|
| `ui_module` | UI module name |
| `g_module` | Game module name |
| `ui_start_command` | Initial UI command |
| `com_frame_limit` | Exit after N frames |

See [Runtime Modules and Cvars](../../../architecture/runtime.md) for the full config reference.

## Debug Checklist

1. Use `mdxtool --info` to verify model assets and sequence names.
2. Use `screenshot` and `+com_frame_limit` to capture a bounded UI frame.
3. Check layout, anchors, tiling, translated strings, and color codes on the captured frame.

## See Also

- [UI System Architecture](./ui.md)
- [Runtime Modules and Cvars](../../../architecture/runtime.md)
- [UI Flow](ui-flow.md)
- [FDF File Format](../file-formats/fdf.md)
