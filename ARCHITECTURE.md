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

## UI Frame Separation (uiFrame_t vs UI Library Frames)

Three separate frame structs exist because they solve different problems at different boundaries:

| Struct | Location | Purpose |
|--------|----------|---------|
| `uiFrame_t` | `common/shared.h` | Network wire protocol — server-authored in-game HUD |
| `FRAMEDEF` (`uiFrameDef_s`) | `warcraft-3/ui/ui_local.h` | WC3 UI runtime — FDF-parsed frames for menus and overlays |
| `uiWowXmlElem_t` | `world-of-warcraft/ui/ui_xml.c` | WoW UI runtime — XML-parsed frames for glue/menu screens |

### `uiFrame_t` is a wire protocol struct

`uiFrame_t` lives in `common/shared.h` so both server and client can serialize it. It is flat (uses `DWORD number` indices instead of pointers), pointer-free, and designed for delta compression (`MSG_WriteDeltaUIFrame` / `MSG_ReadDeltaUIFrame` in `common/msg.c`). Both games use it for the same purpose: the server builds frame trees for in-game HUD elements (action bars, minimap icons, unit frames) and sends them via `svc_layout` payloads to the client, which renders them from `client/cl_unit_layout.c`.

Because it crosses the network, `uiFrame_t` is intentionally sparse — one color, one texture slot, one `onclick` string, simple MIN/MID/MAX axis positioning, and a generic `FRAMETYPE` tag.

### Game UI DLLs are runtime rendering engines

The UI modules (`warcraft-3/ui/`, `world-of-warcraft/ui/`) handle menus, glue screens, and overlay UI — interactive surfaces that are never sent over the wire. These are library-internal runtime representations parsed from game data files (FDF or XML). They need:

- **Event/script systems** — WoW has 12 Lua script hooks per frame (OnLoad, OnShow, OnEnter, OnLeave, OnMouseWheel, OnUpdateModel, etc.). WC3 has per-type `event_handler` and `draw` function pointers. `uiFrame_t` has only one `onclick` string.
- **Complex anchoring** — WoW XML uses named anchor points (TOPLEFT, CENTER, BOTTOMRIGHT, etc.) with dual anchors that compute width/height between two referenced frames. WC3 uses `UIFRAMEPOINT` enums plus relative frame pointers. `uiFrame_t` uses a fixed MIN/MID/MAX axis system.
- **Rich backdrop** — bg/edge textures, tile size, edge size, background insets, backdrop color, border color. WC3 adds corner flags, blend modes, mirrored tiling, and dialog backdrops. `uiFrame_t` has none of this.
- **Multi-state textures** — WoW has NormalTexture, PushedTexture, HighlightTexture, DisabledTexture with separate texcoords per state. WC3 binds Normal/Pushed/Disabled texture names. `uiFrame_t` has one texture slot.
- **Font configuration** — Font size, justification, shadow color/offset, highlight and disabled colors, word wrap, measured text height. `uiFrame_t` has only a `text` string.
- **Widget-specific data** — Slider (min/max/step/thumb), ListBox, Menu, Popup, EditBox, TextArea, CheckBox, ScrollFrame, BuildQueue, Multiselect. `uiFrame_t` has none of this.
- **Model/animation state** — Model handles, sequence time, fog settings for character portrait rendering. Not in `uiFrame_t`.
- **Runtime interaction state** — Pressed, hovered, checked, disabled, focus, scroll offset, drag state. `uiFrame_t` is stateless (state lives in the network snapshot, not persisted on the frame).

### Why not unify them

These are not interchangeable — `uiFrame_t` is sparse by design because it fits in UDP packets; the DLL structs are rich because they drive interactive UI. Merging them would either bloat the wire format (wasting bandwidth for every in-game HUD frame) or starve the UI runtime (forcing workarounds for missing fields). The split follows the same pattern as `entityState_t` (wire protocol) vs game-side `edict_t` (runtime): narrow and serializable across the network boundary, full and pointer-heavy inside the module that owns it.
