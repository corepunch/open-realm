# UI System Architecture

OpenWarcraft3 uses a client-side UI library with a clear split between **menu/glue screens** (drawn by the game's UI library) and the **in-game HUD** (server-authored layout sent via `svc_layout`). This follows the Quake 3 pattern where the UI is a separate library with its own import/export function table.

## Module Boundary

The UI library communicates with the client through two vtables:

**`uiImport_t`** (`client/ui.h`) — services the client provides to the UI library:
`.FS_ReadFile`, `.MemAlloc`, `.Cmd_ExecuteText`, `.GetPlayerState`, renderer/sound access, and font/texture indexing.

**`uiExport_t`** — functions the UI library exposes to the client:
`.Init`, `.Shutdown`, `.Refresh`, `.KeyEvent`, `.TextInput`, `.MouseEvent`, `.UpdateUnitUI`, `.UpdateLobbySetup`, plus the optional `.GameCommand` hook. Generic gameplay systems such as minimap input and transient markers stay in `client/`, not in the menu UI module.

The client creates both at startup in `CL_Init`:

```c
re = CL_GetRendererAPI(...);
ui = UI_GetAPI((uiImport_t) {
    .FS_ReadFile = CL_UI_ReadFile,
    .Cmd_ExecuteText = Cbuf_AddText,
    .GetPlayerState = CL_UIGetPlayerState,
});
ui.Init();  // loads FDF files, initializes screens
```

## Screen Dispatch: Menus vs HUD

`SCR_DrawScreenField()` in `client/cl_scrn.c` is the central dispatch point. It routes to different rendering paths based on `cls.state`:

```c
void SCR_DrawScreenField(DWORD msec) {
    re.BeginFrame();
    switch (cls.state) {
    case ca_disconnected:     ui.Refresh(cl.time); break;   // menu/glue UI
    case ca_connecting:      ui.Refresh(cl.time); break;   // menu/glue UI
    case ca_connected:       ui.Refresh(cl.time); break;   // menu/glue UI
    case ca_active:
        V_RenderView();          // 3D world
        SCR_DrawLayout();        // server-authored in-game HUD
        if (cls.key_dest == key_menu)
            ui.Refresh(cl.time); // ESC menu overlay
        break;
    }
    CON_DrawConsole();
    re.EndFrame();
}
```

**Key rule**: `ui.Refresh()` draws menu/glue screens. When in `ca_active` (gameplay), `SCR_DrawLayout()` draws the in-game HUD via a completely separate path. The UI library's screens only appear when the console key (`key_menu`) is toggled (ESC menu overlay).

Inside `ui.Refresh()` → `UI_RefreshLocal()`, presentation ownership has two independent signals:

1. **Loading** (`playerState_t.client_ui_state == CLIENT_UI_LOADING`): Draws the loading screen first. This remains authoritative even if `menu_ingame` has already been queued through the command buffer.
2. **Standalone menu/glue screen** (`UI_GetCurrentScreen() != NULL`): Calls the current `uiScreen_t->draw()` — main menu, single player, options, LAN lobby, etc.
3. **No standalone screen** (`UI_GetCurrentScreen() == NULL`): The UI module draws no glue screen. During gameplay the in-game HUD is handled by `SCR_DrawLayout()`.

`ui_current_screen` is the WC3 UI module's ownership token; do not add a parallel `game_mode` boolean. `UI_SetScreen(NULL)` is the handoff from standalone glue/menu presentation to loading/gameplay presentation.

## Screen Controllers

Each menu screen is a `uiScreen_t` struct (`games/warcraft-3/menu/menu_screen.h`):

```c
typedef struct uiScreen_s {
    LPCSTR name;
    BOOL (*load)(void);                                      // load FDF, bind frames
    void (*init)(void);                                      // post-load setup
    void (*shutdown)(void);                                  // cleanup
    void (*refresh)(int msec);                               // per-frame update
    void (*draw)(void);                                      // render frames
    void (*key_event)(int key, BOOL down);                   // keyboard input
    void (*update_unit_ui)(DWORD num_units, uiUnitData_t *); // HUD data (game mode only)
} uiScreen_t;
```

Example — main menu controller:

```c
uiScreen_t mainMenuScreen = {
    .name = "main",
    .load = MainMenu_LoadScreen,       // parses FDF, binds named frames
    .init = MainMenu_Init,             // wires click handlers, loads 3D models
    .shutdown = MainMenu_Shutdown,
    .refresh = MainMenu_Refresh,
    .draw = MainMenu_Draw,             // renders 3D background + frame tree
    .key_event = MainMenu_KeyEvent,
};
```

Screen switching via `UI_SetScreen()` calls `screen->shutdown()` on the old screen, then `screen->load()`, `screen->init()` on the new one.

## Menu Commands

Navigation is command-driven — buttons in FDF files have `OnClick = "menu_game"`, and the button event handler calls the central menu command dispatcher. The registered commands (24 total) include:

| Command | Effect |
|---------|--------|
| `menu_main` | Main menu |
| `menu_game` | Single-player menu |
| `menu_lan_refresh` | Refresh LAN game list |
| `menu_startserver` | Create LAN game |
| `menu_ingame` | Release the standalone menu screen with `UI_SetScreen(NULL)` |

The startup command is hardcoded: `menu_main` for Warcraft III, `menu_login` for World of Warcraft.

## FDF Layout System

FDF (Frame Definition File) is the layout format inherited from Warcraft III. The parser lives in `stb_fdf.h` (shared types and declarative dispatch) and `menu_fdf.c` (host-side I/O for MPQ loading).

### Frame Types

`FRAMEDEF` is a master struct with typed sub-structs for each frame type:

```c
struct uiFrameDef_s {
    FRAMETYPE  Type;          // FT_BUTTON, FT_TEXTURE, FT_BACKDROP, FT_DIALOG, ...
    UINAME     Name, TextStorage, OnClick;
    LPCSTR     Text, Tip;
    FLOAT      Width, Height;
    COLOR32    Color;
    BOOL       inuse, hidden, disabled;
    // Type-specific data:
    struct { ... } Points;    // anchor points (TOPLEFT→BOTTOMRIGHT base frame)
    struct { ... } Texture;   // Image, Image2, TexCoord
    struct { ... } Backdrop;  // Background, CornerFlags, EdgeFile
    struct { ... } Font;      // Name, Size, Color, Justification
    struct { ... } Button;    // Normal/Pushed/Disabled textures
    struct { ... } Slider;    // Layout, MinValue, MaxValue, StepSize
    // ... more sub-structs
    DWORD ui_flags;           // UIFLAG_PRESSED, UIFLAG_HOVERED, UIFLAG_VISIBLE
    void (*event_handler)(LPFRAMEDEF, uiMouseEvent_t, FLOAT, FLOAT, int32_t);
    void (*draw)(LPCFRAMEDEF, LPCRECT);
};
```

A static global array `frames[MAX_UI_CLASSES]` (4096 entries) holds all live frames.

### FDF Parsing

The parser is table-driven — FDF class tags map to handlers, and properties map to struct field offsets with type-specific parsers:

```c
static fdf_parse_class_t classes[] = {
    { "Frame",       Frame },
    { "Texture",     Texture },
    { "String",      String },
    { "Layer",       Layer },
    { "IncludeFile", IncludeFile },
};
```

Classes spawn child frames and recurse. Properties like `Width`, `Height`, `SetPoint`, `Text`, `Font`, `File`, `BackdropBackground` are resolved by a dispatch table that writes to `&frame-><offset>`.

### Frame Loading Order

At init, FDF files are loaded from MPQ in dependency order:

```
GlobalStrings.fdf → EscMenuTemplates.fdf → EscMenuMainPanel.fdf →
StandardTemplates.fdf → MainMenu.fdf → SinglePlayerMenu.fdf →
CampaignMenu.fdf → DialogWar3.fdf → MapListBox.fdf → MapInfoPane.fdf →
StandardTemplates.fdf + BattleNetTemplates.fdf + ScriptDialog.fdf →
LocalMultiplayerJoin.fdf → LocalMultiplayerCreate.fdf → TeamSetup.fdf →
PlayerSlot.fdf → GameChatroom.fdf → Loading.fdf
```

### Generated Bindings

FDF frame references are resolved into typed C structs by the `fdfbindgen` code generator. For `MainMenu.fdf`:

```c
typedef struct MainMenu_s {
    LPFRAMEDEF MainMenuFrame;
    LPFRAMEDEF WarCraftIIILogo;
    LPFRAMEDEF SinglePlayerButton;
    LPFRAMEDEF MultiPlayerButton;
    // ... all named frames from the FDF file
} MainMenu_t;
```

`MainMenu_Bind()` calls `UI_FindFrame("MainMenuFrame")` and `UI_FindChildFrame()` to resolve every named frame reference into a pointer.

### Frame Drawing

`UI_DrawFrames()` is called by screen controllers. It renders in three passes per segment:

1. **Pass 1**: Model-based sprites (`FT_SPRITE`)
2. **Pass 2**: Controls (textures, text, buttons, backdrops, sliders, etc.)
3. **Pass 3**: Hover highlights

`UI_DrawFrameOne()` dispatches by frame type:
`FT_DIALOG` → `UI_DrawBackdropWithColor`, `FT_TEXTURE` → `UI_DrawTexture`, `FT_TEXT/STRING` → `UI_DrawText`, `FT_BUTTON` → `UI_ButtonDraw`, `FT_MODEL` → `UI_DrawPortrait`.

## In-Game HUD: Server-Authored Layout

When `cls.state == ca_active`, the in-game HUD follows the Quake 2 `STAT_LAYOUTS` pattern. The **server** sends layout frames via `svc_layout` messages once per client connect. The client's `SCR_DrawLayout()` renders them every frame with no game-specific knowledge.

### Layer System

Frames are grouped into `UILAYOUTLAYER` layers:

| Layer | Content |
|-------|---------|
| `LAYER_BACKGROUND` | Console chrome, minimap panel |
| `LAYER_COMMANDBAR` | Command card buttons |
| `LAYER_INFOPANEL` | Selected unit info/portrait |
| `LAYER_INVENTORY` | Inventory slots |
| `LAYER_CONSOLE` | Menu bar, resource display |
| `LAYER_PORTRAIT` | 3D unit portrait |
| `LAYER_CINEMATIC` | Cinematic overlays |
| `LAYER_MESSAGE` | Chat message area |
| `LAYER_QUESTDIALOG` | Quest/objective display |
| `LAYER_UNIT_SHORTCUTS` | Warcraft III persistent Hero and idle-worker controls |

The server controls visibility via `playerState_t.uiflags` — a bitmask where each bit corresponds to a `UILAYOUTLAYER` value. The client's `SCR_DrawLayout()` skips layers whose bit is set:

```c
if ((1 << layer) & cl.playerstate.uiflags) continue; // server hides this layer
```

### Client UI State

`playerState_t.client_ui_state` controls broad client modes:

```c
typedef enum {
    CLIENT_UI_GAME,       // gameplay HUD active
    CLIENT_UI_LOADING,    // loading screen active
    CLIENT_UI_CINEMATIC,  // cinematic mode
} CLIENTUISTATE;
```

Set by the server. The client reads it to decide what to draw — game HUD, loading progress, or cinematic overlay.

### Frame Rendering

The `drawers[]` table maps `FRAMETYPE` to draw functions:

```c
FT_TEXTURE  → SCR_LayoutDrawTexture
FT_BACKDROP → SCR_LayoutDrawBackdrop
FT_COMMANDBUTTON → SCR_LayoutDrawCommandButton
FT_MINIMAP  → SCR_LayoutDrawMinimap
FT_PORTRAIT → SCR_LayoutDrawPortrait
FT_STRING   → SCR_LayoutDrawString
```

This is the generic renderer — it handles all three games (WC3, SC2, WoW) the same way. Game-specific layout is built by the game module on the server side and transmitted as `uiFrame_t` arrays.

## Mouse Input Architecture

Mouse state is owned by the client (`mouseEvent_t` in `client/cl_input.c`). The UI library receives mouse events via push-based dispatch:

```c
// During SDL_PollEvent in CL_Input:
ui.MouseEvent(x, y, button, down);
```

`UI_MouseEventLocal()`:
1. In an initialized runtime, returns immediately when there is no current standalone screen
2. Converts pixel coords to FDF space
3. Hit tests all interactive frames back-to-front
4. Updates frame flags (`UIFLAG_HOVERED`, `UIFLAG_PRESSED`)
5. Dispatches to per-frame `event_handler` (e.g., `UI_ButtonEventHandler`)
6. Handles globals: editbox focus loss, slider drag, popup close on outside click

The no-screen guard matters because `UI_SetScreen(NULL)` stops drawing the menu but does not clear `menu_render.c`'s last layout cache. Without the ownership check, an invisible stale glue button can consume a gameplay click before `CL_WindowMouseEvent()` and `SCR_LayoutMouseEvent()` see it.

Button clicks execute `UI_MenuCommandLocal(frame->OnClick)`, which routes through the registered menu command table.

Game-mode mouse behavior lives in per-game `cl_input_<game>.c` files. Never create a separate mouse state struct or poll mouse state during draw.

## Loading Screen

The loading screen is owned by the UI library and drawn when `playerState_t.client_ui_state == CLIENT_UI_LOADING`, before standalone-screen dispatch. It:

1. Loads `Loading.fdf` frames
2. Reads map info from `.w3m`/`.w3x` (title, subtitle, custom loading screen model)
3. Binds frames: `LoadingBackground` (3D portrait), `LoadingBar` (progress), `LoadingTitleText`, `LoadingSubtitleText`, `LoadingText`
4. Updates progress from `cl.connectionProgress` each frame

The loading screen stays visible until the server sets `client_ui_state = CLIENT_UI_GAME` and the client reaches `ca_active`.

## WC3 vs SC2 vs WoW UI

| Aspect | Warcraft III | StarCraft II | World of Warcraft |
|--------|-------------|-------------|-------------------|
| Menu UI library | `games/warcraft-3/menu/` | Default UI (no SC2-specific) | `games/world-of-warcraft/menu/` |
| Layout format | FDF files (MPQ) | `.SC2Layout` XML | FDF files (wow-specific) |
| In-game HUD | Server-authored `svc_layout` | Server-authored `svc_layout` | Server-authored `svc_layout` |
| Screen controllers | `uiScreen_t` in `screens/` | Fallback only | N/A (loading screen only) |
| Startup screen | `menu_main` | `menu_main` (default) | `menu_login` |

## Authored Pixel Aspect

`UI_BASE_WIDTH` and `UI_BASE_HEIGHT` define the renderer's coordinate scene,
not necessarily the source layout's pixel aspect. `UI_PIXEL_ASPECT` converts an
authored horizontal pixel span to the equivalent vertical UI span:

```c
UI_PIXEL_ASPECT = UI_MIN_ASPECT * UI_BASE_HEIGHT / UI_BASE_WIDTH
```

It is `1` for WC3's 0.8x0.6 scene and SC2's 1600x1200 scene, but `4/3` for
WoW's normalized 1x1 scene. Font glyph Y offsets, heights, line advance, inline
icons, and inferred square control heights must apply this factor. X advances
and widths must not. Applying one normalization divisor to both axes made WoW
glyphs and inferred square scrollbar sprites exactly 25% too short vertically.

Frames with authoritative width and height already converted independently
(for example WoW `PW(16)` and `PH(16)`) must not apply the factor a second time.

## Key Files

| File | Role |
|------|------|
| `client/ui.h` | UI module boundary: `uiImport_t` / `uiExport_t` |
| `client/cl_scrn.c` | `SCR_DrawScreenField` — dispatch between menus and in-game HUD |
| `client/cl_scrn.c` | `SCR_DrawLayout` — server-authored layout rendering |
| `client/cl_input.c` | Mouse state, input sampling |
| `common/shared.h` | `CLIENTUISTATE` enum, `UILAYOUTLAYER` enum, `playerState_t` |
| `games/*/common/ui_constants.h` | Per-game scene dimensions and `UI_PIXEL_ASPECT` |
| `games/warcraft-3/menu/menu_main.c` | UI library entry point, menu command dispatch |
| `games/warcraft-3/menu/menu_screen.h` | `uiScreen_t` struct and screen declarations |
| `games/warcraft-3/common/stb_fdf.h` | FDF parser, `FRAMEDEF` struct, `frames[]` registry |
| `games/warcraft-3/menu/menu_render.c` | Layout solver, frame drawing dispatch |
| `games/warcraft-3/menu/screens/` | Per-screen controllers (`main_menu.c`, `console_ui.c`, etc.) |

## See Also

- [Server-Authored UI Payloads](ui-payloads.md) — compact type-specific wire schemas, byte limits, and diagnostics
- [Client Architecture](client.md) — client main loop and scene rendering
- [Runtime Modules and Cvars](runtime.md) — cvar system and config loading
- [Warcraft III UI System](../games/warcraft-3/architecture/ui.md) — WC3-specific UI detail
- `docs/ui-authoring.md` — FDF conventions and ConsoleUI controller (source tree only)
