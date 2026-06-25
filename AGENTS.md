# Agent Instructions

This is the single source of truth for all AI agent and coding assistant instructions.
Read this file before making any changes. Load the reference docs below only when your task touches that area.

## Reference Docs

This codebase is inspired by **Quake 2**. The developer working on this project is deeply familiar with the architecture and source code of Quake 2 and wants to build a **real-time strategy (RTS) game** following the same style, conventions, and design philosophy found in Quake 2.

- `ARCHITECTURE.md` — module layout, engine/game boundary, struct/API discipline, network contracts, mouse input. Read when adding or changing engine modules, structs, function tables, or cross-module interfaces.
- `CONTRIBUTING.md` — test fixture rules, MPQ asset conventions, build and linking rules. Read when writing tests, adding fixtures, or modifying the build.
- `docs/coding-style.md` — C coding conventions, formatting rules, struct design, parsing patterns. Read before writing or reviewing any C code.
- `docs/diagnostic-tools.md` — mpqtool, mdxtool, UI text renderer, time profiler. Read when investigating asset, rendering, or performance bugs.
- `docs/sound-architecture.md` — entity sound event system, WC3 sound tables. Read when touching unit sounds or audio playback.
- `docs/ui-authoring.md` — FDF/screen authoring conventions, UI layout rules. Read when writing or modifying UI screens or FDF-driven frames.
- `docs/wow-character.md` — WoW character appearance, DBC fields, M2 skin sections, component textures, WoW client/tool references. Read when touching WoW character rendering or equipment.
- `docs/wc3-data-model.md` — SLK/metadata field codes, base-vs-computed column traps, combat damage model, hero stat system, pathfinding rules, JASS event semantics, MiscGame.txt constants. Read when touching unit stats, combat, abilities, heroes, pathfinding, or any code that reads from the metadata tables.


## Network State Contracts

- Do not casually add fields to `entityState_t`. It is a network snapshot/delta contract, so every new field increases protocol surface, bandwidth, baseline/delta behavior, save/load assumptions, and renderer/client coupling. Adding a field must be extremely well justified and should only happen after considering narrower alternatives such as existing state fields, configstrings, typed UI payloads, game-side state, or explicit commands.

## Engine Struct/API Discipline

- Follow Quake/id-style engine discipline: keep core structs and module APIs small, stable, and data-oriented. Do not add fields to engine structs or function tables unless the existing contract truly cannot express the required state.
- Before adding new engine/game machinery, first check how Quake 2 and Quake 3 handled the closest similar problem. Prefer their established channels and lifecycles: configstrings/media registration, snapshots and player/entity state, usercmds, cvars, console commands, game/client/ref import-export tables, renderer-owned caches, and default/null media.
- If a Quake-style analogue exists, follow it instead of inventing a new subsystem, side cache, struct field, API parameter, or global. If the project must differ because RTS/Warcraft data genuinely requires it, keep the change narrow and explain the specific mismatch.
- Before adding a field, prove why existing channels are insufficient. Prefer existing state such as `entityState_t.image`, renderer-side `renderEntity_t.skin`, configstrings, cvars, command strings, function-table parameters already in use, or data-driven metadata.
- Avoid adding parallel fields that duplicate derived state. If a value can be resolved from an existing ID, configstring, asset record, or cache entry, keep the derived value local to the subsystem that owns the cache.
- Do not widen network or renderer contracts for a single asset bug. In particular, avoid adding one-off fields to `entityState_t`, `playerState_t`, `renderEntity_t`, `uiFrameDef_t`, or import/export APIs unless there is a general engine requirement.
- Do not fix data-driven asset problems with hardcoded asset IDs, terrain/tree enums, campaign-specific literals, or special-case paths in engine code. Inspect the source asset format and object metadata first, then implement the generic processing path.
- Keep on-disk/file-format structs conceptually separate from runtime structs. If a runtime struct has extra fields, do not use `sizeof(runtime_struct)` as the serialized record size; parse/write the file format explicitly.
- When a UI or renderer bug is caused by cache timing, prefer fixing the cache invalidation/resolution layer over storing duplicate path/name fields on every frame or draw object.
- Do not add workaround side tables beside authoritative engine state. If Quake 2/3 would keep using configstrings, registration handles, renderer caches, null/default assets, cvars, or commands, do the same. For example, do not add global `failed_*` arrays to remember asset load failures next to configstrings; fix the registration/cache/renderer fallback path that owns asset loading.
- Before adding any "remember this failed" cache, check the analogous Quake 2/3 path first. If id-tech solved it through registration lifecycle, cache ownership, default media, or clearing state on map/ref changes, follow that pattern instead of creating a new client/game side workaround.
- Treat API and struct growth as a last resort. If a change adds fields, the review explanation should say why a smaller Quake-style solution using existing state was not enough.
- Treat increases to shared engine constants and packet/entity budgets as a last resort. Large RTS maps and doodad/tree counts are expected data, especially in Warcraft III; do not raise caps to paper over visibility, culling, lifetime, sorting, or synchronization bugs until the real bottleneck has been proven and narrower data-driven or lifecycle fixes have been exhausted.
- When replacing a single existing line or macro call with a larger custom block, keep the original line commented out immediately above the replacement and add a short comment explaining why the expansion is necessary, such as a file-format mismatch, bug fix, or new feature behavior.
- Do not hardcode values that are likely to exist in source game data, map files, catalog XML/DBC/SLK/FDF/etc., asset metadata, or other inspectable formats. Inspect the data first and parse the authoritative field. If a temporary literal is genuinely unavoidable, mark it with a `BZ_HARDCODED_DATA_FALLBACK` comment that names the expected source file/field and the reason it is not parsed yet.

## Build And Linking

- Never add `DYLIB_LOOKUP := -Wl,-undefined,dynamic_lookup` or otherwise rely on `-Wl,-undefined,dynamic_lookup` in this repository.
- If a target has unresolved symbols, fix the dependency graph or shared implementation instead of weakening the linker contract.

## Domain

- This is a **real-time strategy game** (RTS), so game logic should account for unit management, pathfinding, resource gathering, building construction, and large numbers of entities — adapted from the Quake 2 entity/server model where applicable.

## UI Module Boundary

- Keep `ui.dll` focused on loading screens and menu/glue UI.
- Draw in-game HUD/ConsoleUI through server-authored `svc_layout` payloads in the generic client path (`client/cl_layout.c`, `client/cl_unit_layout.c`). Do not move gameplay HUD drawing, portraits, minimap, or layout decoding back into `games/<game>/ui`.
- Do not add UI import callbacks for mouse polling, loading state polling, layout decoding, or Warcraft III map-info helpers. Use pushed `MouseEvent`/`LayoutMouseEvent`, `DrawLoadingScreen(map, status, progress)`, client-owned layout functions, and direct `CM_*` map-info calls inside the UI module.

## Documentation Discipline

- When implementing or changing a feature, always add or adjust agent-friendly documentation in this file (AGENTS.md) if the change introduces a new workflow, tool, convention, or subsystem that an agent would need to know about.
- If the feature has a dedicated workflow section (e.g. "MPQ Inspection", "DBC Inspection"), update that section in the corresponding docs file rather than creating a new one.
- If the feature is a new subsystem or introduces a new pattern, add a brief section describing the architecture, the key files involved, and how to inspect/debug/test the feature.
- Keep documentation concise and actionable — prefer command examples and file paths over prose.

## GitHub Issues

- Before creating a GitHub issue, check available labels with `gh label list`.
- When creating issues, assign appropriate labels (e.g. `enhancement`, `warcraft-3`, `world-of-warcraft`, `renderer`, `ui`).
- Do not prefix issue titles with game names (e.g. `wow:`, `wc3:`). Use labels instead.
- If a needed label doesn't exist, create it first with `gh label create`.
- Keep issue titles at most 80 characters.
- Keep issues scoped to one game/title when possible; use game-specific labels (`warcraft-3`, `world-of-warcraft`).
- Use `renderer` for rendering subsystem issues, `ui` for user interface issues.
