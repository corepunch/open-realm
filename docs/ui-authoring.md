# UI Screen Authoring

## FDF-Driven Layout

- In client/UI code, never define or hardcode UI elements, layout coordinates, textures, frame names, or control structures that can be read from FDF. Parse and reuse the actual FDF frames/templates, then bind dynamic data into those frames.
- The only exception is selected game code under `games/<game>/game/`, where there is no FDF parser. Server-authored gameplay HUD payloads may generate simple proxy frames there when needed.

## Screen Controller Conventions

- In `games/warcraft-3/ui/screens/*.c`, prefer `UI_FRAME(...)` and `UI_CHILD_FRAME(...)` for readability and FDF-name coupling.
- Use `UI_FindChildFrame(...)` when it is clearly shorter or cleaner than introducing temporary macro-bound locals.
- Avoid excessive pointer null-check noise in screen controllers. Prefer one scene-level readiness gate (early return) over repeated per-widget checks.
- If a required root frame is missing, fail fast for that screen and skip further scene setup/update work.
- Keep frame names data-driven by FDF; avoid hardcoded lookup strings when macro-based lookup can use the frame identifier directly.

## ConsoleUI Screen Controller (In-Game HUD)

- `ui/screens/console_ui.c` is the client-side replacement for the server-authored `g_ui_stubs.c` HUD.
- Loads Blizzard's ConsoleUI.fdf, ResourceBar.fdf, UpperButtonBar.fdf, InfoPanelUnitDetail.fdf, InfoPanelBuildingDetail.fdf, InfoPanelItemDetail.fdf, and SimpleInfoPanel.fdf from MPQ at runtime via `UI_EnsureFDF()`.
- Binds player state (gold, lumber, food) via `uiimport.GetPlayerState()`.
- Receives unit selection/command data via `update_unit_ui` callback from `svc_unit_ui` messages.
- Draw path: `UI_DrawFrames()` renders FDF FRAMEDEF trees. This is separate from the server-authored UIFRAME/`SCR_DrawLayout()` path used for FDF-based skill/build menus.
- Wire into game mode via `UI_EnterGameMode()` in `ui_main.c`, which calls `consoleUIScreen.load()` and `consoleUIScreen.init()`. The `UI_RefreshLocal()` and `UI_UpdateUnitUILocal()` functions route to the screen during game mode.

## stb_fdf.h Pattern

- `stb_fdf.h` is the shared declarations-only header for FDF types (`FRAMEDEF`, enums, bind macros) and API declarations (`UI_ParseFDF`, `UI_DrawFrames`, etc.).
- Parser implementation stays in `ui_fdf.c` (has `uiimport` dependency for MPQ asset loading). `stb_fdf.h` provides shared types + declarations so both modules see identical structs without circular includes.
- Generated binding headers in `generated/` map FDF field names to struct member offsets via macros like `bind_<fieldname>`. Use `fdfbindgen` tool to regenerate from MPQ source FDF files.
