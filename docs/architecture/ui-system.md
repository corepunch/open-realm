# UI System Architecture

OpenWarcraft3 uses a client-side UI library with a clear split between **menu/glue screens** (drawn by the game's UI library) and the **in-game HUD** (server-authored layout sent via `svc_layout`). This follows the Quake 3 pattern where the UI is a separate library with its own import/export function table.

## Module Boundary

`menuImport_t` provides filesystem, renderer, sound, and glue command services. `menuExport_t` exposes only
`Init`, `Shutdown`, `Refresh`, `KeyEvent`, `TextInput`, `MouseEvent`, and `UpdateLobbySetup`.
There is no player/selected-unit state or gameplay resource-resolution API.

The game authors HUD and in-game dialogs through `svc_layout` and `svc_window`. The client binds replicated state,
renders frames, and handles focus/input. WC3 resolves skin keys through `UI_ThemeImagePath` using `ui_current_client`
before publishing concrete `CS_IMAGES` paths. WoW's action bar, inventory, and welcome window already use these
regular protocols; it no longer emits redundant `svc_unit_ui` packets.

## Exclusive Presentation

`CL_MenuActive()` permits glue presentation only outside an active or loading world. It gates screen drawing,
keyboard/text/mouse input, lobby updates, and engine-wrapped menu commands. `key_dest` selects focus within the
permitted presentation mode; setting it to `key_menu` cannot activate glue over a world.

`CL_BeginLoadingMap()` transfers input to gameplay and marks the loading state. `SCR_DrawScreenField()` draws the
loading layout while loading, world plus game-authored layouts while active, and glue while disconnected or in a
pre-game lobby. Main-menu mouse events are consumed before gameplay layout handlers. Disconnect restores the
normal menu command path. In-game pause/options dialogs remain game-authored windows.

The library stays loaded, but `CL_BeginLoadingMap` calls its Shutdown lifecycle once to release glue resources.
Disconnect/return-to-menu calls Init again; repeated map loads do not double-shutdown. An explicit unloaded/ready/
suspended state also keeps dedicated clients out of this lifecycle. The engine command table retains callback
pointers to the loaded library. Actual DLL unloading would additionally require command unregistration and reload
support; it is unnecessary for exclusive execution.

## History and Regression

`240e1a02` (2026-05-20, “Migrate to client-side UI architecture (Phase 8)”) introduced `UpdateUnitUI` in `ui/ui.h`.
It survived the move/rename to `client/menu.h`. `70d45169` (2026-09-04, “remove game state imports from menu ABI”)
introduced `UpdatePlayerState`, replacing pull access with pushed gameplay state rather than removing the dependency.
`78e61d5b3` introduced menu-based HUD image resolution. These predate the PR 420 null-check change.

The old authoring documentation endorsed ConsoleUI inside the menu, contradicting AGENTS.md. The boundary rule
also had an exception clause. Both are corrected: gameplay UI has no menu-library exception.

The screen regression sets `ca_active` together with `key_menu`: previously it called menu.Refresh; now it draws
only the game path. The inverse verifies disconnected glue drawing. `CL_ParseUnitUI` consumes the legacy wire
shape with a diagnostic to preserve packet alignment, never populating a menu. Legacy named `svc_ui_window`
messages are diagnosed; current windows use `svc_window`.

Run `make test`, `python3 tools/menu_boundary_audit.py`, and `python3 tools/engine_boundary_audit.py`.
UDP tests require local socket access. No interactive game launch is required for these dispatch contracts.

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
    void (*event_handler)(LPFRAMEDEF, menuMouseEvent_t, FLOAT, FLOAT, int32_t);
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
| `LAYER_TIMERDIALOG` | Warcraft III stock timer-dialog mission countdown |
| `LAYER_LEADERBOARD` | Warcraft III stock leaderboard/counting HUD |

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

Ownership is phase-dependent: normal gameplay/cinematic values arrive from authoritative player state, while `CL_BeginLoadingMap()` locally forces `CLIENT_UI_LOADING` during a session transition and the first usable frame restores `CLIENT_UI_GAME`. The client reads the value to decide what to draw — game HUD, loading presentation, or cinematic overlay.

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
menu.MouseEvent(x, y, button, down);
```

`UI_MouseEventLocal()`:
1. In an initialized runtime, returns immediately when there is no current standalone screen
2. Converts pixel coords to FDF space
3. Hit tests all interactive frames back-to-front
4. Updates frame flags (`UIFLAG_HOVERED`, `UIFLAG_PRESSED`)
5. Dispatches to per-frame `event_handler` (e.g., `UI_ButtonEventHandler`)
6. Handles globals: editbox focus loss, slider drag, popup close on outside click

The no-screen guard matters because `UI_SetScreen(NULL)` stops drawing the menu but does not clear `menu_render.c`'s last layout cache. Without the ownership check, an invisible stale glue button can consume a gameplay click before `CL_WindowMouseEvent()` and `SCR_LayoutMouseEvent()` see it.

Button clicks call `UI_QueueCommand(frame->OnClick)` to append console text and a newline. The client command buffer later invokes the registered callback; WC3 has no separate menu command dispatcher. See [WC3 console command ownership](../games/warcraft-3/architecture/ui-flow.md#console-command-ownership).

Game-mode mouse behavior lives in per-game `cl_input_<game>.c` files. Never create a separate mouse state struct or poll mouse state during draw.

## Loading Screen

`ge->PrepareMap` authors this presentation before `ge->LoadMap`. The server retains its compressed frame tree
and complete text in `sv.loading`. It sends loading model/image/font configstrings first, then explicit
`svc_loading_screen` chunks with total length, offset, and chunk length. Complete assembly decodes the existing
layout schema, registers its media, and repaints. The final `svc_mirror "precache"` after the full configstring
and baseline pages permits world registration through `cl.precache_ready`.
See [WC3 loading lifecycle](../games/warcraft-3/loading-and-assets.md) for size limits and late connections.

The client draws this server-authored layout while `playerState_t.client_ui_state == CLIENT_UI_LOADING`. It:

1. Serializes the authoritative `Loading.fdf` frame tree before `begin`
2. Includes map-authored title, subtitle, description, and background model
3. Keeps WC3 MDX art as `FT_SPRITE` with `UI_STAT_LOADING_PROGRESS`; texture bars use `FT_LOADING_BAR`.
   Both consume client-local registration progress without guessing resource type from a numeric index.

`CL_PrepRefresh()` advances that value at real registration phase boundaries. The Quake-style plaque remains frozen for ordinary
frame submission, but `SCR_UpdateLoadingPlaque()` explicitly repaints after a progress change. The value is local client state, not
a player-state/network field. The loading layout stays visible until the first usable server frame sets
`client_ui_state = CLIENT_UI_GAME`, promotes the client to `ca_active`, and ends the plaque.

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

The client UI canvas (`client/cl_canvas.c`, [ui-canvas.md](ui-canvas.md)) owns
how the authored scene maps onto the window. SC2 and WoW expand the scene with
the window aspect. WC3 stretches the authored 0.8 × 0.6 scene when the mounted
archives author no widescreen console chrome (classic retail behaviour) and
widens it with the authored 4:3 HUD root centered when `ConsoleUI.fdf` authors
the `ConsoleTexture05`/`06` extension tiles (retail 1.30+ behaviour). The
renderer projects the scene the client pushes, pointer mapping and the layout
root read the same canvas, and WC3 glue frames and sprite layers anchor to the
renderer scene; none of them derive geometry from the window on their own.
World projection still uses the physical viewport's aspect ratio; projected
labels map into the same UI canvas as pointer input. Pre-rendered movies use
the physical window aspect for letterboxing, then map that rectangle into UI
units; using the virtual canvas aspect would stretch the video.

`UIFLAG_EXTEND_WIDESCREEN_X` frames (cinematic borders, shortcut and timer
anchors) reach the full scene regardless of the root; under the stretched
policy that is the authored 0.8 width.

## Key Files

| File | Role |
|------|------|
| `client/menu.h` | UI module boundary: `menuImport_t` / `menuExport_t` |
| `client/cl_scrn.c` | `SCR_DrawScreenField` — dispatch between menus and in-game HUD |
| `client/cl_scrn.c` | `SCR_DrawLayout` — server-authored layout rendering |
| `client/cl_canvas.c` | UI canvas: scene/root resolution, settle-gated `ui_canvas` class commits |
| `client/cl_input.c` | Mouse state, input sampling |
| `common/shared.h` | `CLIENTUISTATE` enum, `UILAYOUTLAYER` enum, `playerState_t` |
| `games/*/common/ui_constants.h` | Per-game scene dimensions and `UI_PIXEL_ASPECT` |
| `games/warcraft-3/menu/menu_main.c` | UI library entry point, menu command dispatch |
| `games/warcraft-3/menu/menu_screen.h` | `uiScreen_t` struct and screen declarations |
| `games/warcraft-3/common/stb_fdf.h` | FDF parser, `FRAMEDEF` struct, `frames[]` registry |
| `games/warcraft-3/menu/menu_render.c` | Layout solver, frame drawing dispatch |
| `games/warcraft-3/menu/screens/` | Per-screen controllers (`main_menu.c`, `single_player.c`, etc.) |

## See Also

- [Server-Authored UI Payloads](ui-payloads.md) — compact type-specific wire schemas, byte limits, and diagnostics
- [Client Architecture](client.md) — client main loop and scene rendering
- [Runtime Modules and Cvars](runtime.md) — cvar system and config loading
- [Warcraft III UI System](../games/warcraft-3/architecture/ui.md) — WC3-specific UI detail
- `docs/ui-authoring.md` — FDF conventions and game-authored HUD (source tree only)

The SDL regression queues mouse, keyboard, and text events through `CL_Input`. Native SDL2 supports the complete
sequence. The local sdl2-compat installation lost synthetic text during SDL3 conversion, crashing inside
Event3to2/SDL_PushEvent; it was replaced with native SDL2 rather than retaining a test workaround.

The in-engine test command now copies its glob before `Test_Run`: tests execute console commands, which overwrite
`Cmd_Argv` storage. Passing that storage directly previously changed the filter mid-run and silently skipped later
client lifecycle/command tests. JUnit reports must include the `client_session` cases as well as game/input tests.
