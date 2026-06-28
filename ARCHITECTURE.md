# Architecture

## Module Structure

- Structure the project similarly to Quake 2: separate modules for rendering, game logic, input, sound, networking, etc.
- Game state should be managed in a straightforward, imperative style consistent with Quake 2's `g_*.c` / `cl_*.c` / `r_*.c` file layout.
- The engine and game code may be separated (similar to Quake 2's `game.dll` / `ref_gl` split) to allow modular replacement of subsystems.
- Runtime modules communicate through function tables (`R_GetAPI`, `UI_GetAPI`, game imports/exports). Prefer this boundary over direct cross-module dependencies.
- Use cvars for runtime choices: `r_module`, `ui_module`, `g_module`, and `com_frame_limit`.

## Engine/Game Boundary (Strict)

- Engine modules (`renderer/`, `client/`, `common/`, `server/` core paths) must remain game-agnostic.
- Never hardcode game-specific asset names, animation names, frame names, map/script conventions, or franchise-specific literals in engine code.
- Examples of forbidden engine literals: specific sequence names like `"MainMenu Stand"`, specific UI roots, or title-specific asset assumptions.
- If behavior needs title/game knowledge, put that policy in the selected game source tree under `games/<game>/` and pass generic parameters into engine APIs.
- Game-owned sources live under `games/warcraft-3/`, `games/world-of-warcraft/`, and `games/starcraft-2/`, with `game/`, `renderer/`, and, where present, `ui/` subdirectories. Warcraft III also owns `jass/`, `sheet/`, and `tests/` under `games/warcraft-3/`.
- The renderer library is a compound module: engine renderer sources in `renderer/` are compiled together with the selected game's `games/<game>/renderer/` sources. Engine renderer code calls the `R_Game*` API declared in `renderer/r_game.h`; it must not switch on MDX/M2/M3 model formats or include game renderer internals directly.
- Prefer generic fallbacks in engine code (caller-provided names, first available sequence, data-driven metadata) rather than title-specific heuristics.

## Network State Contracts

- Do not casually add fields to `entityState_t`. It is a network snapshot/delta contract — every new field increases protocol surface, bandwidth, baseline/delta behavior, save/load assumptions, and renderer/client coupling. Adding a field must be extremely well justified and should only happen after considering narrower alternatives: existing state fields, configstrings, typed UI payloads, game-side state, or explicit commands.

## Engine Struct/API Discipline

- Follow Quake/id-style engine discipline: keep core structs and module APIs small, stable, and data-oriented. Do not add fields to engine structs or function tables unless the existing contract truly cannot express the required state.
- Before adding new engine/game machinery, first check how Quake 2 and Quake 3 handled the closest similar problem. Prefer their established channels and lifecycles: configstrings/media registration, snapshots and player/entity state, usercmds, cvars, console commands, game/client/ref import-export tables, renderer-owned caches, and default/null media.
- If a Quake-style analogue exists, follow it instead of inventing a new subsystem, side cache, struct field, API parameter, or global. If the project must differ because RTS/Warcraft data genuinely requires it, keep the change narrow and explain the specific mismatch.
- Before adding a field, prove why existing channels are insufficient. Prefer existing state such as `entityState_t.image`, renderer-side `renderEntity_t.skin`, configstrings, cvars, command strings, or function-table parameters already in use.
- Avoid adding parallel fields that duplicate derived state. If a value can be resolved from an existing ID, configstring, asset record, or cache entry, keep the derived value local to the subsystem that owns the cache.
- Do not widen network or renderer contracts for a single asset bug. In particular, avoid adding one-off fields to `entityState_t`, `playerState_t`, `renderEntity_t`, `uiFrameDef_t`, or import/export APIs unless there is a general engine requirement.
- Do not fix data-driven asset problems with hardcoded asset IDs, terrain/tree enums, campaign-specific literals, or special-case paths in engine code. Inspect the source asset format and object metadata first, then implement the generic processing path.
- Keep on-disk/file-format structs conceptually separate from runtime structs. If a runtime struct has extra fields, do not use `sizeof(runtime_struct)` as the serialized record size; parse/write the file format explicitly.
- When a UI or renderer bug is caused by cache timing, prefer fixing the cache invalidation/resolution layer over storing duplicate path/name fields on every frame or draw object.
- Do not add workaround side tables beside authoritative engine state. Fix the registration/cache/renderer fallback path that owns asset loading — do not add global `failed_*` arrays beside configstrings.
- Before adding any "remember this failed" cache, check the analogous Quake 2/3 path first. If id-tech solved it through registration lifecycle, cache ownership, default media, or clearing state on map/ref changes, follow that pattern.
- Treat API and struct growth as a last resort. If a change adds fields, the review explanation should say why a smaller Quake-style solution was not enough.
- Treat increases to shared engine constants and packet/entity budgets as a last resort. Do not raise caps to paper over visibility, culling, lifetime, sorting, or synchronization bugs until the real bottleneck has been proven and narrower fixes exhausted.
- When replacing a single existing line or macro call with a larger custom block, keep the original line commented out immediately above the replacement with a short comment explaining why.
- Do not hardcode values that are likely to exist in source game data, map files, catalog XML/DBC/SLK/FDF/etc., or asset metadata. Inspect the data first. If a temporary literal is genuinely unavoidable, mark it with a `BZ_HARDCODED_DATA_FALLBACK` comment naming the expected source file/field and reason it is not parsed yet.

## UI Rendering

The in-game HUD is drawn entirely client-side by `ui/screens/console_ui.c`, which loads FDF files from MPQ archives at runtime and renders through `UI_DrawFrames()`. Game state (player stats, unit selection) flows through `uiimport.GetPlayerState()` and the `update_unit_ui` callback.

Menus and glue screens use the same FDF rendering path (`UI_DrawFrames()`) via screen controllers under `ui/screens/`.

The server no longer sends UIFRAME trees or `svc_layout` messages. Game logic functions that previously generated frames (e.g. `UI_AddCancelButton`, `UI_ShowQuests`) are now empty stubs in `g_ui_stub.c`.
