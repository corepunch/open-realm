# UI Screen Authoring

## FDF-Driven Layout

- In client/UI code, never define or hardcode UI elements, layout coordinates, textures, frame names, or control structures that can be read from FDF. Parse and reuse the actual FDF frames/templates, then bind dynamic data into those frames.
- WC3 game code uses the same FDF parser as the UI module. Server-authored gameplay HUDs load frame trees, inject runtime data,
  and serialize them; they do not reconstruct those trees as proxy frames.
- Keep proxy-frame buffers as compact wire schemas, not copies of runtime structs. See [Server-Authored UI Payloads](architecture/ui-payloads.md).

### Project-owned FDF

Blizzard FDF remains authoritative for shipped frames. When OpenWarcraft introduces a genuinely new control, author its template
under `share/UI/FrameDef/OpenWarcraft3/`; `FS_Init` mounts `share/` as a loose asset root and the screen must require it through
`UI_EnsureFDF()`. C may clone the template and bind data, visibility, and commands, but must not replace its size or anchors.

`CampaignList.fdf` is the reference: ROC/TFT `MapListBox.fdf` supplies the reusable control shell, while the project template owns
the campaign consumer's size and anchor to `BackButton`. Both ROC and TFT load the same project template.

`DialogTemplates.fdf` is the composition reference. It inherits Blizzard's `StandardDialogTemplate`,
`BattleNetDialogTemplate`, and `ScriptDialog` skins, then authors the message and OK-button children that those reusable roots do
not position for a consumer. `ui_dialog.c` maps the public template names to those final roots, clones them, binds text/commands,
and recenters the dialog instance; it must not manufacture children or overwrite their size, justification, or relative anchors.
Cloning copies `DialogBackdropName` but not the resolved child pointer, so the controller must rebind `DialogBackdrop` by name.

`MessageOverlay.fdf` is the server-authored HUD reference: FDF owns the text-area schema and default anchor; the game module copies
the parsed frame and injects per-player JASS text/position immediately before `svc_layout` serialization.

`Hud.fdf` demonstrates dynamic types: `BuildQueue*`, `Multiselect*`, and `Grid*` properties author payload relationships and repeated
control stride. `UI_CloneGridItem()` expands a template without putting origin, size, spacing, or column counts back into C.

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
