# OpenWarcraft Agent Guide

## Project Context

This codebase is inspired by **Quake 2** (id Software). The developer is deeply familiar with Quake 2's architecture and source code. **Quake 2 is the primary reference** for all lifecycle, state communication, UI control, movement, and entity patterns. Use **Quake 3** as a secondary reference for features Q2 lacks, such as client-side UI libraries or renderer module separation.

## Further Reading

| Topic | File |
|-------|------|
| Architecture, engine boundaries, struct/API discipline, network contracts | [ARCHITECTURE.md](ARCHITECTURE.md) |
| Test discipline, build & linking rules, MPQ fixture rules | [CONTRIBUTING.md](CONTRIBUTING.md) |
| Diagnostic tools (mpqtool, dbctool, mdxtool, text renderer, profiler) | [docs/diagnostic-tools.md](docs/diagnostic-tools.md) |
| UI screen authoring, FDF conventions, ConsoleUI, stb_fdf.h | [docs/ui-authoring.md](docs/ui-authoring.md) |
| WoW character display, DBC/skin-section/component-texture rules | [docs/wow-character.md](docs/wow-character.md) |
| Entity sound architecture | [doc/architecture/sound.md](doc/architecture/sound.md) |
| WC3 data model (SLK, unit stats, combat) | [docs/wc3-data-model.md](docs/wc3-data-model.md) |
| SC2 HUD layout pipeline (sc2BaseFrame_t → uiFrame_t, layer IDs, stat bindings) | [games/starcraft-2/docs/hud-layout-pipeline.md](games/starcraft-2/docs/hud-layout-pipeline.md) |

## Coding Style

- Follow the C coding style used in the Quake 2 source code (id Software style).
- Use the same patterns for module organization, data structures, and naming conventions as in Quake 2.
- Prefer simple, flat, and data-oriented design over complex object-oriented abstractions.
- Keep the code readable, compact, and close to the metal — minimize unnecessary indirection.
- **No hacks. No silent fallbacks. No hiding errors.** Every implementation must have a solid reasoning. If a shortcut is taken, mark it with `/* HACK: */` or `/* TODO: */` and explain *why* the proper fix is not yet possible. Never demote, drop, or silently skip unresolved data — instead, log it with `fprintf(stderr, ...)` so future debugging reveals the gap. Demotion (e.g. `FT_TEXTURE → FT_FRAME` for unresolved resources) hides the real problem; the resource should be found and resolved, not discarded.
- **Never guess at a bug fix.** Before writing any fix, add targeted `fprintf(stderr, ...)` logs at the exact code paths in question, run the binary with `+com_frame_limit N` to capture a bounded log, read the output, and confirm the root cause from evidence. Only then write the fix and remove the logs. A fix written without log evidence is a guess and will be reverted. If the existing CLI tools (`mpqtool`, `dbctool`, `mdxtool`) cannot answer the question, extend them or add a new tool rather than guessing. The tools in `build/bin/` exist precisely because guessing at asset/data problems wastes time.
- **Use `git blame` when investigating history.** When a value, macro, or code path seems wrong, use `git blame` or `git log -p -S <pattern>` to find when and why it was introduced. The commit message and diff often explain the original intent, distinguishing a deliberate trade-off from an accidental value or copy-paste left-over.
- **Write as little code as possible.** Prefer smart tricks and reuse of existing code over writing new functions. When Quake code style leads to verbose vertical expansion, override it with denser, shorter forms: pack related statements on one line, use ternary/comma for conditional side effects, omit braces for single-statement bodies, and collapse trivial helpers into one-liners.
- For trusted binary game data, prefer memory-mapped/file-shaped structs with trailing arrays wherever possible. Read the blob, allocate/copy it as one block if ownership is needed, and point consumers at that struct instead of decoding, cropping, or post-processing into parallel runtime arrays.
- Prefer table-driven parsing for keyed/text formats such as XML, FDF, catalogs, and similar game data. Define a small schema table first (`{ name, offsetof(struct, field), type }`), then run one generic parser over that table.
- Prefer format-driven parsing when the data has a fixed syntax. Use `sscanf(text, "%f,%f,%f", ...)` instead of hand-writing character walkers, separator loops, and ad hoc token logic.
- Do not bury schema in long manual `if`/`else` or `switch` ladders when a compact table can describe the same work.
- Do not use several booleans to represent mutually exclusive state. Define and pass an enum, then dispatch from that enum.
- Put pure, reusable local helpers in a small nearby utils header (e.g. `sc2_utils.h`) as `static` functions. Keep subsystem-owned helpers that touch globals or runtime state in the `.c` file that owns that state.
- Follow a strict DRY rule: do not duplicate logic or repeat the same data literal in multiple places.
- Keep runtime structs concise. Group related fields; use anonymous structs for repeated shapes; prefer `DWORD flags` over many standalone `BOOL` fields.
- Test flag membership with implicit bool conversion: `flags & FLAG` not `(flags & FLAG) != 0`.
- Use `snake_case` for functions and variables, `ALL_CAPS` for constants and macros, matching Quake 2 conventions.
- Use the `BZ_` prefix for project-private compile-time macros, generated binding helpers, environment toggles, and namespaced constants.
- When fixing warnings for short, future-facing hooks (one-line static moves, extern declarations, placeholder assignments), prefer commenting them out over deleting them. Add a short comment explaining the warning and when the line should come back.
- For WoW UI code (`games/world-of-warcraft/ui/`), do not fail silently. Emit a clear `UIWow:` log when a required script, handler, renderer resource, or fallback path is missing.
- When a function has more than 3 parameters, group them into a dedicated input struct (`draw<Thing>_t` or `<thing>Params_t`).

## General Formatting

- Minimize vertical space. Prefer fewer, denser lines over many short ones.
- Keep C source lines at or under 120 characters.
- Single-statement functions go on one line: `int f(void) { return 0; }`
- Omit braces for single-statement `if`/`else`/`while` bodies.
- Keep control-flow keywords at the start of their own line. Do not write chained forms like `...; if (...)` on the same physical line.
- Add a short comment before each non-trivial function describing why it exists — the constraint or contract that isn't obvious from the signature alone.
- For any fallback, workaround, or partial implementation, prepend `/* HACK: */` or `/* TODO: */` and explain why.
- When providing a bug fix, add an inline comment at the fix site explaining why the fix is correct and what the original behaviour was.

## WinAPI-style Typedefs for Structs

- Struct names are ALL CAPS, short, and descriptive (e.g., `PORTRAITFOG`). No `_t` suffix.
- Use WinAPI-style `LP`/`LPC` typedefs for struct pointer types (`LPCPORTRAITDEF`, `LPRECT`).
- `LP` = long pointer (non-const), `LPC` = long pointer to const.
- Define both alongside the struct using separate `typedef` lines so `LPC` is `const struct *`.

## What to Avoid

- **Do not patch loaded UI layout in code.** SC2Layout and FDF files define the correct layout. When a panel's anchors seem wrong (e.g. off-screen positioning from a cross-panel reference), fix the anchor resolution in the layout system so it handles the case correctly — don't override anchors per-panel in game code. The layout data is authoritative; code must apply it faithfully.
- Do not introduce helper variables just to name an intermediate result if the expression is already readable inline.
- Do not add blank lines between short, related statements.
- Do not split a declaration and its first assignment onto separate lines.
- Do not add null-pointer or function-pointer guards before calling cross-module API functions (`ui.*`, `re.*`, `s.*`, etc.). These are guaranteed to be set at init time.

## Tool Failures

- **If a tool fails repeatedly, stop and notify the user.** When a tool fails more than 2-3 times with the same error, do not keep retrying blindly. Inform the user what is failing and why, propose a workaround, and ask whether to continue or wait for help.

## Test Discipline

- Verify every Warcraft III data/UI change in both ROC (default archives) and TFT (`-tft`). TFT archives override ROC
  paths and may replace whole FDF/string/data files rather than extending them, so confirm both variants explicitly.

- **Every structural change must include or update tests.** When you add a function, change a behavior path, fix a bug, or modify a struct/API contract, check whether existing tests cover the change.
- **New code paths need new tests.** If you add an `if` branch, a new function, a new field, or a new cache/state machine, write a test for the new path and its inverse.
- **Cache/state-machine changes double-test.** Test both cache hit and cache miss paths, and verify performance counters where tracked.
- **Run `make test` before committing.** The WC3 test binary (`test_openwarcraft3`) includes all unit tests.
- **Compile and run tests before finishing any work.** Run `make run-sc2` to verify the build compiles, then `make test` to confirm all tests pass. Never mark work complete without a green test run.
- **Auto-quit the app with `+com_frame_limit N`.** When running the binary for verification, pass `+com_frame_limit 100` (or similar) so the process exits after N frames without manual intervention. Example: `make run-sc2 ARGS="+com_frame_limit 100"`
- **`git blame` before changing existing struct/API fields.** Understand why a field exists and what trade-offs were made before changing it.
- **Do not disable a failing test.** Fix the code or fix the test — do not comment it out, add `SKIP`, or reduce its coverage.

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full engine/game boundary, module structure, network state contracts, engine struct/API discipline, and UI rendering overview.

Key principles inline:
- Runtime modules communicate through function tables (`R_GetAPI`, `UI_GetAPI`, game imports/exports).
- The server controls what the client draws via state bits in `playerState_t`. The client just reads them.
- Never hardcode game-specific asset names, animation names, or franchise-specific literals in engine code.
- Never use `#ifdef SC2`/`#ifdef WOW` to vary constants in shared engine code. Per-game constants live in `games/*/common/ui_constants.h` and resolve via the per-game `-I` include path. Each game defines its native coordinate space (`UI_BASE_WIDTH`, `UI_BASE_HEIGHT`, `UI_FRAMEPOINT_SCALE`) — no conversion functions between game-native coords and "engine coords". The engine operates in whatever coordinate space the game header declares.

## Server-Authoring Pattern (Quake 2 STAT_LAYOUTS)

- `uiflags` bitmask hides layout layers: each bit corresponds to a `UILAYOUTLAYER` value. Server sets bits to hide layers, client checks `(1 << layer) & uiflags`.
- `client_ui_state` enum (`CLIENT_UI_GAME`, `CLIENT_UI_LOADING`, `CLIENT_UI_CINEMATIC`) controls broad client modes.
- Never hardcode game-mode-specific skip logic in the client. The server sets the appropriate flags; the client respects them.

## Mouse Input Architecture

- Mouse state is owned by the client: the `mouse` global (`mouseEvent_t` in `client/cl_input.c`) is the single source of truth.
- The UI library receives mouse events via `ui.MouseEvent(x, y, button, down)` — push-based, called during `SDL_PollEvent` in `CL_Input()`.
- Game-mode-specific mouse behavior lives in per-game `cl_input_<game>.c` files via the `CL_InputMode*` functions.
- Never create a separate mouse state struct in game UI code. Never poll mouse event state during draw.

## UI Module Boundary

- Keep `ui.dll` focused on loading screens, menu/glue UI, and client-side in-game HUD screens.
- Do not add UI import callbacks for mouse polling, loading state polling, layout decoding, or map-info helpers. Use pushed events, `DrawLoadingScreen(map, status, progress)`, client-owned layout functions, and direct `CM_*` calls inside the UI module.
- Loading-screen ownership stays with `ca_loading`. The client may only enter `ca_active` from `CL_PrepRefresh()` after all required assets are registered.

See [docs/ui-authoring.md](docs/ui-authoring.md) for FDF conventions, screen controller patterns, ConsoleUI, and stb_fdf.h.

## Missing Asset Placeholders

Follow Quake 2's pattern. Never fail silently, never crash, never log per-frame.

- **Registration always returns a valid handle.** `R_LoadTexture` returns `tr.texture[TEX_PLACEHOLDER]` on file-not-found. `R_LoadModel` returns an empty zeroed `model_t`. Callers never get NULL.
- **Log once per unique asset.** Use a static `last_missing` pointer to suppress repeated warnings for the same filename.
- **Cache the result.** Higher-level caches store the placeholder handle just like a successful load.
- **Do not add per-frame or per-draw warnings.**
- **Do not add local null-check guards for textures returned by `R_LoadTexture`.** The function guarantees a valid pointer.

## Command Conventions

- The `+` prefix (e.g. `+map`, `+menu_main`) is for **command-line arguments only**. It tells `Cbuf_AddLateCommands` to strip the `+` and queue the command for startup execution.
- In code, use the bare command name when calling `Cbuf_AddText` or `uiimport.Cmd_ExecuteText`: `"map ..."` not `"+map ..."`.

## Domain

- This is a **real-time strategy game** (RTS), so game logic should account for unit management, pathfinding, resource gathering, building construction, and large numbers of entities — adapted from the Quake 2/3 entity/server model where applicable.

## Documentation Discipline

- When implementing or changing a feature, add or adjust agent-friendly documentation if the change introduces a new workflow, tool, convention, or subsystem.
- Update the relevant dedicated file in `docs/` or `doc/architecture/` rather than adding large blocks here.
- Keep AGENTS.md as a concise index and rule set. Detailed workflows and reference material belong in dedicated files.
- Keep documentation concise and actionable — prefer command examples and file paths over prose.
- **Populate docs as you go.** Any fact that required research — a lookup in GitHub issues, a `gh issue view`, an API signature dug up from source, a format quirk discovered during parsing — belongs in the relevant `docs/` file immediately after you discover it. Do not leave findings only in conversation context. If no doc file exists for the subsystem yet, create one. Future agents (and future you) must be able to answer the same question from docs without repeating the research.

## GitHub Issues

- Before creating a GitHub issue, check available labels with `gh label list`.
- When creating issues, assign appropriate labels (e.g. `enhancement`, `warcraft-3`, `world-of-warcraft`, `renderer`, `ui`).
- If a needed label doesn't exist, create it first with `gh label create`.
- Keep issue titles at most 80 characters.
- Keep issues scoped to one game/title when possible.
