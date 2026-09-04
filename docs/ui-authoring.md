# UI Screen Authoring

For campaign loading and asset diagnostics, see [WC3 loading and assets](games/warcraft-3/loading-and-assets.md).

## FDF-Driven Layout

- In client/UI code, never define or hardcode UI elements, layout coordinates, textures, frame names, or control structures that can be read from FDF. Parse and reuse the actual FDF frames/templates, then bind dynamic data into those frames.
- WC3 game code has access to the FDF parser and uses it for frames that exist in War3.mpq (e.g. building detail, resource bar, quest dialog). Native runtime controls with no MPQ frame geometry, such as the portrait, command-card buttons, minimap content, and tooltips, use compact proxy frames serialized via `UI_WriteProxyFrame`.
- Keep proxy-frame buffers as compact wire schemas, not copies of runtime structs. See [Server-Authored UI Payloads](architecture/ui-payloads.md).

### No project-owned FDF

There are no project-owned FDF files under `share/UI/FrameDef/`. Every FDF frame used by OpenWarcraft comes from the War3.mpq
archives. The authoritative source for any frame's geometry is always the MPQ FDF, never C code and never a project overlay.

When you need a frame that exists in the MPQ, load it with `UI_EnsureFDF` and generate a binding header with `fdfbindgen`
(see AGENTS.md §WC3 UI Tooling). Examples: `InfoPanelBuildingDetail.fdf` for the building-detail HUD sub-panel; `QuestDialog.fdf`
for the quest window; `MapListBox.fdf` for a list-box control reused by campaign and multiplayer screens.

For runtime controls that have no FDF geometry in War3.mpq — portraits, command buttons, minimap content, tooltip payloads — construct a
small proxy frame in C. Warsmash ships a project-owned `UI\FrameDef\SmashUI\UnitPortrait.fdf` to describe its portrait geometry;
that file is not a retail MPQ source and must not be introduced as an OpenRealm data dependency. OpenRealm's portrait remains a runtime
proxy, with its coordinates kept in parity with the reference runtime layout.

### Passive FDF tooltips

`uiFrame_t.tooltip` is a generic hover contract, not a command-button-only field. Persistent HUD hit testing treats a frame with non-empty tooltip text as hoverable even when `onclick` is empty; this is required for passive FDF labels such as the WC3 resource bar. Do not add fake click commands merely to make a frame hoverable. `FT_TOOLTIPTEXT` remains the shared presentation proxy. When multiple visible HUD layers serialize that presentation frame, the client draws it only in the layer/window that owns the current hovered source.

For the WC3 resource bar, `ResourceBarGoldText`, `ResourceBarLumberText`, `ResourceBarSupplyText`, and `ResourceBarUpkeepText` are the stable named anchors. The retail resource icon textures are unnamed, so bind passive resource help to the named FDF value frames rather than creating project-owned icon geometry. Gold/Lumber/Food resource `Tip` headings use the generic `{value}` marker; `SCR_GetTooltipText()` expands it from the hovered frame's current `Stat` display value, preventing a server-authored layout packet from freezing an older amount while replicated playerstate continues to change. The descriptive `Ubertip` remains driven by Warcraft `GlobalStrings.fdf`. Upkeep prefers split `RESOURCE_UBERTIP_UPKEEP_INFO[_WOOD]` rows, preserves a base body that already contains tier ranges, and generates a gameplay-data-backed fallback legend if neither representation supplies one. See [WC3 food and upkeep](games/warcraft-3/food-and-upkeep.md).

### TextLength semantics

WC3 FDF `TextLength` is layout geometry, not merely a character limit. When a text frame has no explicit `Width`, its implicit width
is `TextLength` multiplied by the active font's space-glyph advance. `uiFrame_t.textLength` therefore belongs to the retained UI wire
contract and must survive `MSG_WriteDeltaUIFrame` / `MSG_ReadDeltaUIFrame`; the client resolves the final width with its registered
font metrics. Falling back to the current string's natural width breaks right-aligned resource fields because their right anchor stays
correct while the left edge moves with the number of digits.

Single-line FDF text with no authored `Width`/`Height` uses the declared FDF font size as its layout line-box height. The renderer's
glyph bounds are drawing metrics and may be shorter; using them as layout height causes chained labels to drift vertically. Conversely,
when an FDF frame has an authored `Width` or `Height`, that dimension remains authoritative even if both opposing anchors are present.
On the Y axis WC3 preserves the bottom anchor and lets an authored-height frame extend upward; only an auto-sized axis stretches between
its opposing anchors. This is required by `SimpleInfoPanel.fdf`, whose `0.03125` damage/armor frames are deliberately taller than the
`0.030125` runtime wrappers they are attached to.

Runtime WC3 command-card geometry follows the 4x3 retail/Warsmash grid: x starts at `0.6175` with a `0.0434` column stride, y starts
at `0.4660` with a `0.0440` row stride, and buttons are `0.039 x 0.039`. These are runtime control coordinates rather than FDF-owned
layout.

## Screen Controller Conventions

- In `games/warcraft-3/menu/screens/*.c`, prefer `UI_FRAME(...)` and `UI_CHILD_FRAME(...)` for readability and FDF-name coupling.
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
- Wire into game mode via `UI_EnterGameMode()` in `menu_main.c`, which calls `consoleUIScreen.load()` and `consoleUIScreen.init()`. The `UI_RefreshLocal()` and `UI_UpdateUnitUILocal()` functions route to the screen during game mode.

## stb_fdf.h Pattern

- `stb_fdf.h` is the shared declarations-only header for FDF types (`FRAMEDEF`, enums, bind macros) and API declarations (`UI_ParseFDF`, `UI_DrawFrames`, etc.).
- Parser implementation stays in `menu_fdf.c` (has `uiimport` dependency for MPQ asset loading). `stb_fdf.h` provides shared types + declarations so both modules see identical structs without circular includes.
- Generated binding headers in `generated/` map FDF field names to struct member offsets via macros like `bind_<fieldname>`. Use `fdfbindgen` tool to regenerate from MPQ source FDF files.

## DDX-Style Schema Tables Across Layout Engines

All three game layout engines follow the single-header DDX-style schema table architecture:

| Game | Header | Schema Tables | Role |
|---|---|---|---|
| **Warcraft III** | `games/warcraft-3/common/stb_fdf.h` | `items[]`, `classes[]` (`FDF_F` macros) | Parses `.fdf` frame templates into `FRAMEDEF frames[]` |
| **StarCraft II** | `games/starcraft-2/common/stb_sc2layout.h` | `sc2_frame_attrs[]`, `sc2_frame_fields[]`, `sc2_child_tags[]` | Parses `.SC2Layout` XML into `sc2Frame_t` and flattened `sc2BaseFrame_t` |
| **World of Warcraft** | `games/world-of-warcraft/menu/stb_wowxml.h` | `uiwow_node_types[]`, `uiwow_script_tags[]`, `uiwow_button_part_tags[]`, `uiwow_shared_attrs[]`, `uiwow_point_factors[]` | Parses FrameXML (`.xml`) into `uiWowXmlElem_t wow_xml.elems[]` |

Each parser defines the format grammar as data (table of names, offsets, types, flags/callbacks) and dispatches in one generic loop without manual `if`/`else` ladders.
