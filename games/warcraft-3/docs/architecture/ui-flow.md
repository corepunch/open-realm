# UI Flow

This document traces the current client-side UI path: input, menu commands, frame rendering, and unit-data queries.

## Overview

```text
Client input
  -> UI mouse/key event
  -> active uiScreen_t
  -> frame tree update
  -> UI_DrawFrame
  -> renderer API
  -> OpenGL renderer or stdout renderer
```

All FDF parsing, layout solving, screen transitions, and frame rendering happen client-side. The server sends data for game-dependent UI, but it does not author UI frame trees.

## Startup Flow

```text
common/main.c
  -> Com_Init
      -> share/default.cfg
      -> share/config.cfg
      -> share/autoexec.cfg
      -> command-line cvars
  -> CL_Init
      -> select renderer from r_module
      -> re.Init
      -> UI_GetAPI
      -> ui.Init
      -> UI_MenuCommandLocal(ui_start_command)
```

Important cvars:

| cvar | Purpose |
|------|---------|
| `r_module` | `renderer` for OpenGL, `stdout` for text output |
| `ui_start_command` | Initial command, usually `menu_main` |
| `com_frame_limit` | Exit after N frames |

## Loading Flow

Loading follows the Quake-style client state split:

1. `CL_BeginLoadingMap()` sets `cls.state = ca_loading` and seeds the loading text/progress.
2. `SCR_DrawScreenField()` draws the loading plaque only while `cls.state == ca_loading`.
3. `CL_PrepRefresh()` loads configstring-backed assets and registers the map.
4. Only after all required assets are ready does `CL_PrepRefresh()` send `begin` and promote the client to `ca_active`.
5. Snapshot parsing may update `playerstate`, but it must not flip `cls.state` by itself.

That separation matters because `ca_active` means "gameplay can render now", not "we already received a player snapshot." If the loading plaque disappears before the world is ready, the client should keep `ca_loading` until the precache gate completes.

## Menu Navigation Flow

1. SDL input is translated by the client input layer.
2. `UI_MouseEventLocal` or `UI_KeyEventLocal` updates UI state.
3. The current `uiScreen_t` receives the event.
4. Button frames inspect mouse containment and event state in `games/warcraft-3/ui/ui_render.c`.
5. If a clicked frame has `OnClick`, `UI_MenuCommandLocal` executes the command.
6. Menu commands call direct screen/action handlers.

Example menu command:

```text
menu_game
```

The screen switch is local to the client. No network traffic is required for menu transitions.

## Draw Flow

Each client frame calls:

```c
ui.Refresh(msec);
CL_Input();
CL_ReadPackets();
CL_SendCommand();
CL_PrepRefresh();
SCR_UpdateScreen();
```

`SCR_UpdateScreen` calls the renderer and UI:

1. `re.BeginFrame`
2. `re.RenderFrame`
3. `ui.DrawFrame`
4. console/debug overlay
5. `re.EndFrame`

`ui.DrawFrame` dispatches to the active screen. For `menu_main`, `games/warcraft-3/ui/screens/main_menu.c` draws the `MainMenu3d` portrait background, the logo sprite, and the main menu frame tree.

## Stdout Renderer Flow

For automated or text-first diagnostics, use:

```bash
make run-ui-text
```

This expands to a one-frame run with:

```bash
-r_module=stdout -ui_start_command=menu_main -com_frame_limit=1
```

The stdout renderer receives the same UI draw calls as the OpenGL renderer but prints them:

```text
draw_portrait model="UI\\Glues\\MainMenu\\MainMenu3d\\MainMenu3d.mdl" anim="Stand" viewport={...}
draw_sprite model="UI\\Glues\\MainMenu\\WarCraftIIILogo\\WarCraftIIILogo.mdl" anim="Stand" x=0.130000 y=0.080000
draw_image name="UI\\Widgets\\Glues\\GlueScreen-Button1-Border.blp" screen={...} uv={...}
draw_text font="Fonts\\FRIZQT__.TTF" rect={...} text="|CffffffffS|Ringle Player"
```

Use this output to verify screen composition, layout rects, UVs, text translation, color codes, and button state art before taking screenshots.

## Unit Selection and Command Card Flow

Unit UI data still comes from the server because it depends on game rules and selected entities.

```text
client selection
  -> CL_RequestUnitUI
  -> clc_request_unit_ui
  -> server/sv_unit_ui.c
  -> games/warcraft-3/game/g_unit_ui.c
  -> svc_unit_ui
  -> client/cl_unit_ui.c
  -> ui.UpdateUnitUI
  -> games/warcraft-3/ui/screens/console_ui.c
```

### Client Request

```c
void CL_RequestUnitUI(DWORD num_selected, DWORD *entity_nums) {
    MSG_WriteByte(&cls.netchan.message, clc_request_unit_ui);
    MSG_WriteByte(&cls.netchan.message, (BYTE)num_selected);
    for (DWORD i = 0; i < num_selected; i++) {
        MSG_WriteShort(&cls.netchan.message, (SHORT)entity_nums[i]);
    }
}
```

### Server Query

```c
gameCommandButton_t buttons[12];
BYTE num_buttons = ge->GetCommandButtons(ent, buttons, 12);
```

The server serializes command buttons, inventory, and build queue data into `svc_unit_ui`.

### Client Cache

```c
void ConsoleUI_UpdateUnitUI(DWORD num_units, uiUnitData_t *units) {
    cached_unit_count = num_units;
    memcpy(cached_units, units, sizeof(uiUnitData_t) * num_units);
}
```

The HUD screen renders from this cache on later frames.

## Key Decisions

- UI rendering is client-side for instant menu interaction.
- The server remains authoritative for game data.
- FDF assets are parsed by the UI library, not by the game DLL.
- Runtime modules communicate through Quake-style function tables.
- `r_module=stdout` makes draw-call output scriptable for UI debugging.

## See Also

- [UI System Architecture](./ui.md)
- [UI Quick Reference](ui-quick-reference.md)
- [Runtime Modules and Cvars](../../../../architecture/runtime.md)
- [FDF File Format](../file-formats/fdf.md)
