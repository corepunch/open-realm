# UI Screen Authoring

For campaign loading and asset diagnostics, see [WC3 loading and assets](games/warcraft-3/loading-and-assets.md).

## FDF-Driven Layout

- In client/UI code, never define or hardcode UI elements, layout coordinates, textures, frame names, or control structures that can be read from FDF. Parse and reuse the actual FDF frames/templates, then bind dynamic data into those frames.
- WC3 game code has access to the FDF parser and uses it for frames that exist in War3.mpq (e.g. building detail, resource bar, quest dialog). For native WC3 frame types that have no FDF definition in the MPQ (portrait, command button, minimap, tooltip), simple proxy frames are constructed in C with inline float values and serialized via `UI_WriteProxyFrame`.
- Keep proxy-frame buffers as compact wire schemas, not copies of runtime structs. See [Server-Authored UI Payloads](architecture/ui-payloads.md).

### No project-owned FDF

There are no project-owned FDF files under `share/UI/FrameDef/`. Every FDF frame used by OpenWarcraft comes from the War3.mpq
archives. The authoritative source for any frame's geometry is always the MPQ FDF, never C code and never a project overlay.

When you need a frame that exists in the MPQ, load it with `UI_EnsureFDF` and generate a binding header with `fdfbindgen`
(see AGENTS.md §WC3 UI Tooling). Examples: `InfoPanelBuildingDetail.fdf` for the building-detail HUD sub-panel; `QuestDialog.fdf`
for the quest window; `MapListBox.fdf` for a list-box control reused by campaign and multiplayer screens.

For frame types that have no FDF definition in War3.mpq — portraits, command buttons, minimap, tooltip — construct a static
`FRAMEDEF` in C with `UI_InitFrame`, `UI_SetSize`, and `UI_SetPoint`, using inline float literals. Do not name those positions
with `#define` constants.

## Screen Controller Conventions

- In `games/warcraft-3/ui/screens/*.c`, prefer `UI_FRAME(...)` and `UI_CHILD_FRAME(...)` for readability and FDF-name coupling.
- Use `UI_FindChildFrame(...)` when it is clearly shorter or cleaner than introducing temporary macro-bound locals.
- Avoid excessive pointer null-check noise in screen controllers. Prefer one scene-level readiness gate (early return) over repeated per-widget checks.
- If a required root frame is missing, fail fast for that screen and skip further scene setup/update work.
- Keep frame names data-driven by FDF; avoid hardcoded lookup strings when macro-based lookup can use the frame identifier directly.

## ConsoleUI Screen Controller (In-Game HUD)

- `ui/screens/console_ui.c` is the client-side replacement for the server-authored `hud/hud.c` HUD.
- Loads Blizzard's ConsoleUI.fdf, ResourceBar.fdf, UpperButtonBar.fdf, InfoPanelUnitDetail.fdf, InfoPanelBuildingDetail.fdf, InfoPanelItemDetail.fdf, and SimpleInfoPanel.fdf from MPQ at runtime via `UI_EnsureFDF()`.
- Binds player state (gold, lumber, food) via `uiimport.GetPlayerState()`.
- Receives unit selection/command data via `update_unit_ui` callback from `svc_unit_ui` messages.
- Draw path: `UI_DrawFrames()` renders FDF FRAMEDEF trees. This is the only draw path for the in-game HUD.
- Wire into game mode via `UI_EnterGameMode()` in `ui_main.c`, which calls `consoleUIScreen.load()` and `consoleUIScreen.init()`. The `UI_RefreshLocal()` and `UI_UpdateUnitUILocal()` functions route to the screen during game mode.

## stb_fdf.h Pattern

- `stb_fdf.h` is the shared declarations-only header for FDF types (`FRAMEDEF`, enums, bind macros) and API declarations (`UI_ParseFDF`, `UI_DrawFrames`, etc.).
- Parser implementation stays in `ui_fdf.c` (has `uiimport` dependency for MPQ asset loading). `stb_fdf.h` provides shared types + declarations so both modules see identical structs without circular includes.
- Generated binding headers in `generated/` map FDF field names to struct member offsets via macros like `bind_<fieldname>`. Use `fdfbindgen` tool to regenerate from MPQ source FDF files.

## DDX-Style Schema Tables Across Layout Engines

All three game layout engines follow the single-header DDX-style schema table architecture:

| Game | Header | Schema Tables | Role |
|---|---|---|---|
| **Warcraft III** | `games/warcraft-3/common/stb_fdf.h` | `items[]`, `classes[]` (`FDF_F` macros) | Parses `.fdf` frame templates into `FRAMEDEF frames[]` |
| **StarCraft II** | `games/starcraft-2/common/stb_sc2layout.h` | `sc2_frame_attrs[]`, `sc2_frame_fields[]`, `sc2_child_tags[]` | Parses `.SC2Layout` XML into `sc2Frame_t` and flattened `sc2BaseFrame_t` |
| **World of Warcraft** | `games/world-of-warcraft/ui/stb_wowxml.h` | `uiwow_node_types[]`, `uiwow_script_tags[]`, `uiwow_button_part_tags[]`, `uiwow_shared_attrs[]`, `uiwow_point_factors[]` | Parses FrameXML (`.xml`) into `uiWowXmlElem_t wow_xml.elems[]` |

Each parser defines the format grammar as data (table of names, offsets, types, flags/callbacks) and dispatches in one generic loop without manual `if`/`else` ladders.
