# Plan: feature/wow-menu — Bare Minimum WoW Menu

## Goal

Create branch `feature/wow-menu` with a single commit that ports the WoW menu system
(login, character select, character create) from `feature/wow-sound-and-characters-clean`,
while keeping HUD and loading screen as they are on main. No sound system.

## Strategy

Copy the **final state** of required files from the clean branch and adapt them.
Cherry-picking 147 individual commits is impractical due to interdependencies.
A single-commit port is cleaner and what the user requested.

## Key Design Decisions

1. **No sound** — `PlaySound`/`PlaySoundByName` left NULL in `uiImport_t`. WoW Lua
   code is already NULL-safe (checks `uiimport.PlaySound` before calling).

2. **Mouse exclusively through `UI_MouseEvent`** — In menu mode, SDL events route to
   `ui.MouseEvent` only. No `mouse.origin`/`mouse.button` storage for menu purposes.
   The engine-side `mouseEvent_t mouse` struct is only used for WC3 gameplay.

3. **Default to login** — `CL_Init()` ends with `CL_SetMenuBindings()` + `CL_MenuCommand("menu_login")`.

4. **`uiImport_t` is a superset** — Keep all existing WC3 fields AND add new WoW fields
   (`FS_WriteFile`, `GetTime`, `PlaySound`, `PlaySoundByName`). This avoids breaking
   WC3 UI which still references the old fields.

5. **`MenuCommand` removed from `uiExport_t`** — Both WC3 and WoW register menu commands
   via `Cmd_AddCommand`. `CL_MenuCommand` uses `Cbuf_AddText` directly. WC3's internal
   `UI_MenuCommandLocal` is still called from within WC3 UI code.

6. **Layout wiring in `cl_main.c`** — `DrawOverlays`, `LayoutMouseEvent`, `SetLayoutLayer`,
   `ClearLayoutLayer`, `HitTestLayout` wired directly in `CL_Init()`, not through `uiExport_t`.

---

## Files to Create

| # | File | Lines | Purpose |
|---|------|-------|---------|
| 1 | `client/ui_layout.h` | 30 | Layout function declarations (from clean) |
| 2 | `client/ui_text_input.h` | 216 | Shared text-input engine (from clean) |
| 3 | `client/cl_unit_layout.c` | ~560 | Server-authored layout moved to client (renamed from WC3 `ui_layout.c`) |
| 4 | `games/world-of-warcraft/ui/ui_main.c` | 580 | WoW UI entry point, lifecycle, menu commands (from clean) |
| 5 | `games/world-of-warcraft/ui/ui_local.h` | 124 | Internal types (from clean) |
| 6 | `games/world-of-warcraft/ui/ui_lua.c` | 1021 | Lua VM + all bindings (from clean) |
| 7 | `games/world-of-warcraft/ui/ui_xml.c` | 2252 | WoW XML runtime (from clean) |
| 8 | `games/world-of-warcraft/ui/ui_dbc.c` | 875 | Character-creation DBC loader (from clean) |
| 9 | `games/world-of-warcraft/ui/ui_dbc.h` | 38 | DBC public API (from clean) |
| 10 | `games/world-of-warcraft/ui/ui_loading.c` | 49 | Loading screen background (from clean) |
| 11 | `games/world-of-warcraft/tests/resources-src/.../OW3Glue.lua` | 23 | Test fixture |
| 12 | `games/world-of-warcraft/tests/resources-src/.../GameHUD.lua` | 49 | Test fixture |
| 13 | `games/world-of-warcraft/tests/resources-src/.../GlueXML.toc` | 0 | Empty TOC |

## Files to Modify

### `client/ui.h`
- Replace `uiClientMouseEvent_t` with `uiMouseEvent_t` enum: `UI_MOUSE_MOVE`, `UI_MOUSE_DOWN`, `UI_MOUSE_UP`, `UI_MOUSE_SCROLL`
- Add `UI_MOUSE_PARAM(dx,dy)` / `UI_MOUSE_PARAM_X(p)` / `UI_MOUSE_PARAM_Y(p)` macros
- **Keep** all existing `uiImport_t` fields (WC3 needs them)
- **Add** to `uiImport_t`: `FS_WriteFile`, `GetTime`, `PlaySound`, `PlaySoundByName`
- **Remove** from `uiExport_t`: `MenuCommand`
- **Add** to `uiExport_t`: `DrawLoadingScreen(LPCSTR map, LPCSTR status, FLOAT progress)`, `LayoutMouseEvent(uiMouseEvent_t, int x, int y, int32_t param)`, `DrawOverlays(void)`
- Keep `SetLayoutLayer`, `ClearLayoutLayer`, `HitTestLayout` in `uiExport_t`

### `client/cl_input.c`
- Add second `switch` block for UI mouse forwarding:
  - `SDL_MOUSEBUTTONDOWN` → if `key_menu`: `ui.MouseEvent(UI_MOUSE_DOWN, x, y, button)`, `ui.LayoutMouseEvent(UI_MOUSE_DOWN, x, y, button)`
  - `SDL_MOUSEBUTTONUP` → similar with `UI_MOUSE_UP`
  - `SDL_MOUSEMOTION` → if `key_menu`: `ui.MouseEvent(UI_MOUSE_MOVE, x, y, 0)`, `ui.LayoutMouseEvent(UI_MOUSE_MOVE, x, y, 0)`. If `key_game`: `CL_InputModeMouseMotion`
  - `SDL_MOUSEWHEEL` → `ui.MouseEvent(UI_MOUSE_SCROLL, x, y, UI_MOUSE_PARAM(...))`, `ui.LayoutMouseEvent(...)`
  - `SDL_TEXTINPUT` → if `key_menu`: `ui.TextInput(text)`
  - `SDL_KEYDOWN/UP` → if `key_menu`: `ui.KeyEvent(key, down, time)`
- First switch block keeps existing `mouse.origin`/`Key_Event` logic for WC3 gameplay

### `client/cl_main.c`
- Remove `CL_MenuCommand` wrapper — replace with direct `Cbuf_AddText` calls
- Simplify `uiImport_t`: remove `ReadMapInfo`, `FindMapPreviewTexture`, `FreeMapInfo`, `DefaultMapName`, `ResolveMapInfoString`, `MapNameMatchesFile`, `MapTilesetName`, `MapSizeName`, `SanitizeMapListField`, `SanitizeMapInfoText`, `ModelIndex`, `GetClientTime`, `GetMouseFdf`, `GetMouseButton`, `GetMouseEvent`, `LayoutClear`, `LayoutNumFrames`, `LayoutFrame`, `LayoutRect`, `LayoutStringValue`, `LayoutDrawText`, `RequestUnitUI`, `Error`
- **Add** to `uiImport_t`: `FS_WriteFile`, `GetTime`, `PlaySound`, `PlaySoundByName`
- Wire in `CL_Init()`: `ui.DrawOverlays = SCR_DrawLayout`, `ui.LayoutMouseEvent = SCR_LayoutMouseEvent`, `ui.SetLayoutLayer = SCR_SetLayoutLayer`, `ui.ClearLayoutLayer = SCR_ClearLayoutLayer`, `ui.HitTestLayout = SCR_LayoutHitTest`
- At end of `CL_Init()`: `CL_SetMenuBindings()`, `cls.state = ca_disconnected`, `CL_MenuCommand("menu_login")`
- Add `CL_UI_WriteFile` helper (simple `fopen`/`fwrite`/`fclose`)
- Remove `#include "ui_layout.h"` (it's now a separate header)

### `client/cl_scrn.c`
- Add loading screen path: `if (cls.state == ca_loading && ui.DrawLoadingScreen) { ui.DrawLoadingScreen(...); }`
- Add `ui.DrawOverlays` call in game mode

### `common/main.c`
- Defer `SV_Init()`: `if (!menu_mode) { SV_Init(); }` before `CL_Init()`

### `Makefile`
- WoW UI lib: change source discovery to pick up new files (`ui_main.c`, `ui_lua.c`, `ui_xml.c`, `ui_dbc.c`, `ui_loading.c`)
- Add `WOW_UI_CFLAGS` with `$(LUA_CFLAGS)` and `$(WOW_XML_CFLAGS)`
- Add `$(LUA_LIBS)` and `$(WOW_XML_LIBS)` to WoW UI link
- WC3 UI lib: update to use `client/cl_unit_layout.c` instead of `games/warcraft-3/ui/ui_layout.c`
- WoW test: update source list to compile new UI files + `common/mpq.c`

### `games/warcraft-3/game.mk`
- Update WC3 UI lib source list (remove `ui_layout.c`, it moved to `client/`)
- Update test target references

### `games/warcraft-3/tests/test_client_stubs.c`
- Add `CL_EntityEvent` and `Cbuf_AddText` stubs

### `games/world-of-warcraft/tests/test_wow_ui.c`
- Update to match clean branch version

## Files to Delete

| File | Reason |
|------|--------|
| `games/world-of-warcraft/ui/ui_wow.c` | Replaced by `ui_main.c` + `ui_lua.c` + `ui_xml.c` + `ui_dbc.c` + `ui_loading.c` |
| `games/warcraft-3/ui/ui_layout.c` | Moved to `client/cl_unit_layout.c` |

## Execution Order

1. `git checkout -b feature/wow-menu main`
2. Copy all new files from `feature/wow-sound-and-characters-clean`
3. Modify existing files per changes above
4. Delete replaced files
5. Build and verify compilation
6. Run tests
7. Single commit: `"WoW: bare minimum menu system (login, character select, character create)"`

## Mouse Flow Diagram

```
SDL event
  └→ cl_input.c CL_Input()
       ├─ First switch: mouse.origin + Key_Event (WC3 gameplay)
       └─ Second switch (key_menu):
            ├→ ui.MouseEvent(UI_MOUSE_DOWN/UP/MOVE/SCROLL, x, y, param)
            └→ ui.LayoutMouseEvent(UI_MOUSE_DOWN/UP/MOVE/SCROLL, x, y, param)

UI library (WoW):
  ui_main.c UIWow_MouseEvent()
    ├→ UIWow_XMLMouseEvent() — XML frame hit testing
    └→ UIWow_LuaMouseMove() / UIWow_LuaMouseClick()
```

## Risks

1. **libxml2 required** — `ui_xml.c` needs libxml2. Makefile already has pkg-config detection.
2. **Lua required** — `ui_lua.c` needs lua5.4 or lua. Makefile already has pkg-config detection.
3. **GlueXML assets required** — Without actual MPQ data containing GlueXML Lua/XML files,
   the menu won't render. This is expected — the MPQ data is required.
4. **WC3 UI breakage from `uiImport_t` changes** — Mitigated by keeping all old fields
   and only adding new ones. WC3 UI code is unchanged.
