# UI System Quick Reference

This is the short version of the current client-side UI architecture.

## System Overview

```text
Mouse/keyboard
  -> client input
  -> games/warcraft-3/ui/ui_main.c
  -> games/warcraft-3/ui/screens/*.c
  -> games/warcraft-3/ui/ui_render.c
  -> renderer API
```

The UI library parses Warcraft III FDF files client-side, owns the frame tree, and switches screens through menu commands. The server provides game data only, such as command buttons and inventory items.

## Startup

`CL_Init` creates the renderer and UI function tables:

1. Select renderer from `r_module`.
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

## Renderer Diagnostics

Use the stdout renderer before taking screenshots:

```bash
make run-ui-text
```

Equivalent:

```bash
build/bin/openwarcraft3 \
  -data=data/Warcraft\ III \
  -r_module=stdout \
  -ui_start_command=menu_main \
  -com_frame_limit=1
```

The output includes:

- loaded textures, models, and fonts
- `draw_portrait` and `draw_sprite`
- `draw_image` with screen rect, UV rect, blend mode, color, and texture name
- `draw_text` with text, font, rect, measured size, and color
- `draw_sys_text` console overlay lines

This is useful for checking layout, anchors, backdrop pieces, button state art, translated strings, color codes, and screen composition without a window.

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
  -> games/warcraft-3/ui/screens/console_ui.c
```

The client caches returned unit data and renders it on subsequent UI frames.

## Core Files

| File | Purpose |
|------|---------|
| `client/ui.h` | Shared UI module API declaration |
| `games/warcraft-3/ui/ui_main.c` | UI entry point, lifecycle, startup command, screen selection |
| `games/warcraft-3/ui/ui_fdf.c` | FDF parsing and frame registry |
| `games/warcraft-3/ui/ui_render.c` | Layout solving and frame rendering |
| `games/warcraft-3/ui/ui_theme.c` | Warcraft UI theme resources |
| `games/warcraft-3/ui/screens/main_menu.c` | Main menu screen |
| `games/warcraft-3/ui/screens/console_ui.c` | In-game HUD screen |
| `client/cl_main.c` | Renderer/UI init and client frame loop |
| `client/cl_unit_ui.c` | `svc_unit_ui` parser |
| `server/sv_unit_ui.c` | Unit UI data request handler |
| `games/warcraft-3/game/g_unit_ui.c` | Game-side unit UI data provider |
| `renderer/r_stdout.c` | Text renderer backend |

## Runtime Cvars

| cvar | Purpose |
|------|---------|
| `r_module` | `renderer` for OpenGL, `stdout` for text output |
| `ui_module` | UI module name |
| `g_module` | Game module name |
| `ui_start_command` | Initial UI command |
| `com_frame_limit` | Exit after N frames |

See [Runtime Modules and Cvars](../../../architecture/runtime.md) for the full config reference.

## Debug Checklist

1. Use `mdxtool --info` to verify model assets and sequence names.
2. Use `make run-ui-text` to inspect draw calls.
3. Check `draw_image screen={...}` for layout and anchors.
4. Check `draw_image uv={...}` for tiling and atlas issues.
5. Check `draw_text text="..."` for translated strings and Warcraft color codes.
6. Use screenshots only after the draw-call transcript looks sane.

## See Also

- [UI System Architecture](./ui.md)
- [Runtime Modules and Cvars](../../../architecture/runtime.md)
- [UI Flow](ui-flow.md)
- [FDF File Format](../file-formats/fdf.md)
