# UI System Architecture

Menus and loading screens run in the selected UI library (`games/warcraft-3/ui/`). The in-game HUD/ConsoleUI is server-authored through compact `svc_layout` payloads and drawn by the generic client layout renderer in `client/cl_layout.c` and `client/cl_unit_layout.c`. The shared client-facing UI API is declared in `client/ui.h`.

## Migration from Server-Side UI (Phase 1-7)

Previously, the UI module also drew pieces of the in-game ConsoleUI. Gameplay HUD ownership has moved back to the Quake-style server-authored layout path: the game writes title-specific proxy frames, the client decodes and draws them, and `ui.dll` stays focused on glue screens and loading.

## Quick Navigation

**Looking for a specific topic?**

- **Complete end-to-end flow** (client input → command/data update → rendering): See [UI Flow](ui-flow.md)
- **Runtime cvars and stdout renderer**: See [Runtime Modules and Cvars](../../../../architecture/runtime.md)
- **How to add a new UI element**: See [Adding a New UI Element](#adding-a-new-ui-element) below
- **FDF file syntax**: See [FDF File Format](../file-formats/fdf.md)

## Concepts

| Term | Meaning |
|------|---------|
| `frameDef_t` | A template parsed from an FDF file and stored in the registry |
| `FRAMEDEF` | The alias used by the C API for a frame definition being constructed |
| `uiScreen_t` | A screen controller with init, refresh, draw, and input callbacks |
| `ui_start_command` | Cvar selecting the first UI command, e.g. `menu_main` |

## Initialisation

`UI_Init` (`games/warcraft-3/ui/ui_main.c`) is called once from `CL_Init` when the client starts:

1. Loads UI library via `UI_GetAPI(uiImport_t)` function table.
2. Client provides import functions: memory allocation, file I/O, MPQ access.
3. UI library loads Warcraft III `.fdf` files from MPQ via `UI_ParseFDF` (`games/warcraft-3/ui/ui_fdf.c`).
4. UI executes `ui_start_command`, defaulting to `menu_main`.
5. Screen controller manages frame lifecycle, drawing, and input routing.

## Frame Definition Files (FDF)

FDF files declare frame templates hierarchically. `UI_ParseFDF_Buffer` (`games/warcraft-3/ui/ui_fdf.c`) accepts a writable C string, tokenises it, and registers `frameDef_t` entries in a global lookup table. Frames can inherit from a base template with `InheritsParts`.

See [FDF File Format](../file-formats/fdf.md) for the full syntax reference.

## Server-Authored Gameplay UI

Gameplay HUD state is sent with `svc_layout`. Warcraft III creates title-specific proxy frames in `games/warcraft-3/game/g_ui_stubs.c`; the client stores each layer in `cl.layout[layer]`, decodes frame geometry in `client/cl_layout.c`, and draws the result in `client/cl_unit_layout.c`.

## Frame Tree Layout

The frame tree is a depth-first hierarchy managed client-side by the UI library. Each frame stores:

- **Anchor point** — which of the nine anchor points (`TOPLEFT` … `BOTTOMRIGHT`) of this frame is attached to which point of which parent frame, and at what X/Y offset.
- **Size** — explicit width and height if set.
- **Texture** — texture name resolved via UI library's asset loader.
- **Text** — optional UTF-8 string for labels.
- **Stat** — optional `PLAYERSTATE_*` tag that makes the text field track a live player stat (gold, lumber, supply, etc.) and update automatically every frame.
- **Type-specific data** — backdrop edge insets, button up/down/hover states, label font index.
- **Children** — singly-linked list of child frames.

The UI library maintains the frame tree and recalculates layout when frames are added, removed, or resized.

## Rendering

The Warcraft III UI library handles menus, lobby/setup screens, loading screens, and cinematic glue panels through FDF frames. Active gameplay uses `svc_layout` instead:

1. Game code sends layer payloads such as `LAYER_CONSOLE`, `LAYER_PORTRAIT`, `LAYER_COMMANDBAR`, `LAYER_INFOPANEL`, and `LAYER_INVENTORY`.
2. `CL_ParseLayout` stores each raw layer payload and wires it to the client layout renderer.
3. `SCR_DrawLayout` in `client/cl_unit_layout.c` draws the HUD, portrait, command card, inventory, tooltips, and other server-authored frames.
4. Menu input goes to `ui.MouseEvent`; gameplay HUD input goes directly to `SCR_LayoutMouseEvent` in the client-owned layout handler.

## Menu Commands

UI screens are selected by explicit menu commands. This keeps navigation in the same Quake-style command stream as buttons, console input, and startup configuration.

Examples:

| Command | Purpose |
|---------|---------|
| `menu_main` | Main menu |
| `menu_game` | Single-player menu |
| `menu_lan_refresh` | Refresh LAN map list |
| `menu_startserver` | LAN create-game screen |

The startup command is configurable:

```bash
build/bin/openwarcraft3 -data=data/Warcraft\ III -ui_start_command=menu_main
```

For isolated UI diagnostics:

```bash
make run-ui-text
```

That command uses `r_module=stdout` and `com_frame_limit=1` to print one frame of draw calls and exit. Menu-only diagnostics do not open UDP sockets.

## Dynamic Updates

Gameplay HUD updates happen through server-authored layout layers:

- **Selection changes** — game code sends updated `LAYER_PORTRAIT`, `LAYER_INFOPANEL`, `LAYER_INVENTORY`, and `LAYER_COMMANDBAR` payloads.
- **Command card** — `games/warcraft-3/game/g_ui_stubs.c` writes `FT_COMMANDBUTTON` frames with click commands and icon indices.
- **Resource display** — player state values arrive in snapshots; text frames can refer to player stats through `uiFrame_t.stat`.
- **Mouse/clicks** — `client/cl_input.c` pushes events to `SCR_LayoutMouseEvent`, which hit-tests the decoded frame layer and forwards click commands to the server.

Rendering still happens client-side, but the gameplay HUD shape and contents are authored by the game through `svc_layout`.

## Stdout Renderer Diagnostics

The stdout renderer is the preferred first-pass diagnostic for UI rendering. It implements the same renderer API as the OpenGL renderer but writes draw calls to stdout:

- `load_texture`, `load_model`, `load_font`
- `draw_portrait`, `draw_sprite`
- `draw_image` with texture name, screen rect, UV rect, color, blend mode, and rotation
- `draw_text` with font, rect, measured size, color, and translated text
- `draw_sys_text` for console overlay text

Use it to check screen composition, frame positions, backdrop tiling, missing assets, hover/pressed state changes, translated strings, and Warcraft color codes without taking screenshots.

## UI Test Asset Policy

UI test fixtures and assets are repository-owned. Do not copy Warcraft III assets into tests.

- Author source fixtures in `games/warcraft-3/tests/resources-src/`.
- Generate deterministic BLP/MDX assets into `build/tests/resources/`.
- Pack and validate `build/tests/tests.mpq` via `make test-assets`.

The normal test pipeline enforces this by running `test-assets` before `make test`.

For UI-impacting changes, use `make test-ui` as the required gate. It runs:

- FDF parser/frame-graph suites
- UI layout conformance suites
- End-to-end client UI rendering suites
- Tool-backed oracle suites (`mdxtool --info`)

Note: `fdftool` was removed in Phase 8 as it depended on deleted server-side UI code. Use `make run-ui-text` for UI draw-call inspection and `mdxtool --info` for model data inspection.

## Adding a New UI Element

For menu/loading screens, add FDF-backed frames in `games/warcraft-3/ui/screens/*.c`.

For gameplay HUD elements, add proxy frames in `games/warcraft-3/game/g_ui_stubs.c` and send them on an appropriate layout layer:

```c
uiFrame_t frame;
memset(&frame, 0, sizeof(frame));
frame.flags.type = FT_TEXTURE;
frame.tex.index = gi.ImageIndex("SomeTexture");
UI_SetFrameRect(&frame, x, y, w, h);
UI_WriteProxyFrame(&frame, NULL, 0);
```

The client layout renderer supports the frame types listed in `client/cl_unit_layout.c`. Add generic draw behavior there only when the frame type is not Warcraft-specific.

## Key Files

### Client-Side UI (Phase 8+)

| File | Purpose |
|------|---------|
| `games/warcraft-3/game/g_ui_stubs.c` | Author Warcraft III gameplay HUD proxy frames |
| `client/cl_parse.c` | `CL_ParseLayout` receives `svc_layout` payloads |
| `client/cl_layout.c` | Decode server-authored frame geometry |
| `client/cl_unit_layout.c` | Draw gameplay HUD layers and handle layout input |
| `client/ui.h` | Shared UI module API declaration |
| `games/warcraft-3/ui/ui_main.c` | `UI_GetAPI`, menus, loading, cinematic glue panels |
| `games/warcraft-3/ui/ui_fdf.c` | FDF parser and programmatic frame API |
| `games/warcraft-3/ui/ui_render.c` | Menu/loading FDF frame rendering dispatch |
| `games/warcraft-3/ui/screens/main_menu.c` | Main menu, single player menu, etc. |
| `client/cl_input.c` | Mouse input, menu/layout event routing |
| `renderer/r_font.c` | Bitmap font rasteriser |
| `common/common.h` | `clc_request_unit_ui`, `svc_unit_ui` opcodes |

### Server-Side Data Providers

| File | Purpose |
|------|---------|
| `games/warcraft-3/game/g_unit_ui.c` | Converts selected entities into command card, inventory, and build queue data |
| `server/sv_unit_ui.c` | Marshals unit UI data into `svc_unit_ui` messages |
| `client/cl_unit_ui.c` | Receives `svc_unit_ui` and forwards decoded data to the UI library |

## Quake 3 UI Lineage

OpenWarcraft3's UI architecture descends from two Quake 3 Arena UI systems. Understanding the lineage clarifies design decisions.

### Q3 Classic (`q3_ui/`) — Pure C, Hardcoded Layout

The original Q3 UI is entirely C. Every menu screen is a separate `.c` file with a static struct and hardcoded menuitem arrays:

```c
// q3_ui/ui_menu.c — each screen is a struct + static items
typedef struct {
    menuframework_s menu;
    menutext_s      singleplayer;
    menutext_s      multiplayer;
    // ...
} mainmenu_t;

static mainmenu_t s_main;

static menuitem_s s_main_items[] = {
    { (void*)&s_main.singleplayer, MTYPE_TEXT, ... },
    // ...
};

void Main_MenuEvent(void *ptr, int event) {
    if (event != QM_ACTIVATED) return;
    switch (((menucommon_s*)ptr)->id) {
        case ID_SINGLEPLAYER: UI_SPLevelMenu(); break;
        // ...
    }
}
```

Key characteristics:
- **Layout is compile-time**: menu positions, item types, and screen hierarchy are C structs
- **Navigation is a stack**: `UI_PushMenu()` / `UI_PopMenu()` — no external routing
- **No external layout files**: everything lives in source code
- **Callbacks are function pointers**: `menuitem_s.callback` fires on activation
- **Each screen is ~200-400 lines of C**: struct definition, init, draw, key handler

### Q3 Team Arena (`ui/`) — C Backend + Script-Defined Layout

Team Arena rewrote the UI to separate layout from logic using `.menu` script files:

```c
// ui/ui_shared.h — layout is data-driven
typedef struct {
    Window window;
    int type;
    const char *text;
    const char *action;      // script command on click
    const char *mouseEnter;  // script on hover
    const char *onFocus;     // script on focus
    const char *cvar;        // bound cvar
    // ...
} itemDef_t;

typedef struct {
    Window window;
    itemDef_t *items[MAX_MENUITEMS];
    const char *onOpen;   // script when menu opens
    const char *onClose;  // script when menu closes
    // ...
} menuDef_t;
```

Menu files (`ui/menu.def`) define frames, items, and behavior in a script language:
```
menuDef {
    name        "main"
    rect        0 0 640 480
    onOpen      "playSound sound/misc/menu1.wav"
    itemDef {
        type    BUTTON
        text    "Multiplayer"
        action  "openServer; playSound sound/misc/menu2.wav"
    }
}
```

Key characteristics:
- **Layout is external**: `.menu` files parsed at runtime via `PC_LoadSource` / `Asset_Parse`
- **Behavior is scripted**: `action`, `onOpen`, `onClose`, `mouseEnter` are script strings
- **C handles rendering + parsing**: `Display_DoPaint`, `Item_RunScript`, `Asset_Parse`
- **Single `uiInfo_t` global**: all UI state in one struct (server lists, player info, etc.)
- **Script commands are C-dispatched**: `commandDef_t` table maps `"openServer"` → `UI_OpenServer_f()`

### WoW UI — C Backend + FDF Layout + Lua Scripts

The WoW UI extends the Team Arena pattern with Lua:

| Aspect | Q3 Team Arena | WoW UI |
|--------|--------------|--------|
| Layout format | `.menu` scripts | `.fdf` files (FrameXML) |
| Behavior scripts | Inline script strings | Lua scripts + XML `OnClick`/`OnMouseWheel` |
| Script runtime | `Item_RunScript` (simple tokenizer) | Full Lua VM (`lua_pcall`) |
| C-side dispatch | `commandDef_t` table | `uiScreen_t` function pointers + Lua FFI |
| State binding | `cvar` fields on items | Lua table state + `UIWow_XMLGetAttr` |

Both share the same architecture: C owns rendering and event dispatch, external files own layout and behavior. The WoW UI just uses a more powerful scripting language.

### WC3 UI — Pure C with Runtime-Parsed FDF

The WC3 UI is the odd one out. It parses FDF files at runtime (like Team Arena parses `.menu` files), but all behavior is C — no script layer:

| Aspect | Q3 Classic | WC3 UI | Q3 Team Arena | WoW UI |
|--------|-----------|--------|--------------|--------|
| Layout source | C structs | FDF files | `.menu` scripts | FDF files |
| Behavior source | C callbacks | C functions | Script strings | Lua scripts |
| Runtime parsing | No | Yes | Yes | Yes |
| Script VM | None | None | Simple tokenizer | Lua |
| Screen model | `menuframework_s` stack | `uiScreen_t` controllers | Global `menuDef_t` array | `uiScreen_t` + Lua |
| Control impl | `ui_qmenu.c` widgets | Static `.h` functions | `ui_shared.c` generic | `ui_xml.c` + Lua |

The WC3 UI sits between Q3 Classic (pure C, hardcoded) and Q3 Team Arena (C + scripts). It has the runtime layout parsing of Team Arena but keeps all logic in C like Q3 Classic. This makes it simpler to debug (no script VM) but harder to modify (every new control type needs C code).

The WoW UI completes the evolution by adding Lua, giving designers control over behavior without recompiling.
