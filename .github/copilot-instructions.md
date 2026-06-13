# GitHub Copilot Instructions

## Project Context

This codebase is inspired by **Quake 2**. The developer working on this project is deeply familiar with the architecture and source code of Quake 2 and wants to build a **real-time strategy (RTS) game** following the same style, conventions, and design philosophy found in Quake 2.

## Coding Style

- Follow the C coding style used in the Quake 2 source code (id Software style).
- Use the same patterns for module organization, data structures, and naming conventions as in Quake 2.
- Prefer simple, flat, and data-oriented design over complex object-oriented abstractions.
- Keep the code readable, compact, and close to the metal — minimize unnecessary indirection.
- For trusted binary game data, prefer memory-mapped/file-shaped structs with trailing arrays wherever possible. Read the blob, allocate/copy it as one block if ownership is needed, and point consumers at that struct instead of decoding, cropping, or post-processing into parallel runtime arrays.
- Prefer table-driven parsing for keyed/text formats such as XML, FDF, catalogs, and similar game data. Define a small schema table first, for example `{ name, offsetof(struct, field), type }`, then run one generic parser over that table. This is also called data-driven or array-driven parsing: the field mapping is data, and the parser is a tiny machine that applies it.
- Prefer format-driven parsing when the data has a fixed syntax. Configure the parser with the format and launch it, for example `sscanf(text, "%f,%f,%f", ...)` for SC2 comma-separated vectors, instead of hand-writing character walkers, separator loops, and ad hoc token logic.
- Do not bury schema in long manual `if`/`else` or `switch` ladders when a compact table can describe the same work. Put the mapping beside the target struct, keep the interpreter small, and let adding a field mean adding one table row instead of adding custom logic in the parser body.
- Do not use several booleans to represent mutually exclusive state. If only one mode/kind/type can be active, define and pass an enum, then dispatch from that enum. For example, use one `sc2ObjectType_t` value instead of separate `is_unit`, `is_doodad`, and `is_camera` flags.
- Put pure, reusable local helpers in a small nearby utils header, such as `sc2_utils.h`, as `static` functions. Keep subsystem-owned helpers that touch globals, allocation hosts, file handles, or runtime state in the `.c` file that owns that state.
- Write tiny parsing/utility helpers only when they remove real duplication or clarify a call site. Keep them brutally short; prefer simple standard C library calls (`strchr`, `strspn`, `strtoul`, etc.) over hand-written multi-line loops unless the format genuinely requires custom logic. Do not add ceremonial blank lines inside tiny helpers, and keep trivial statement bodies on one line when that is clearer, e.g. `if (*p == '"') quoted = !quoted;`. Avoid temporary success variables for tiny wrappers; branch directly on the call and return explicit `true`/`false` when that is shorter and clearer.
- Follow a strict Don't Repeat Yourself (DRY) rule: do not duplicate logic or repeat the same data literal in multiple places. If the same path/key/constant appears more than once (for example `Interface\\GlueXML\\AccountLogin.xml`), centralize it as one named constant or one shared loader path and reuse it.
- Use `snake_case` for functions and variables, `ALL_CAPS` for constants and macros, matching Quake 2 conventions.
- Use the `BZ_` prefix for project-private compile-time macros, generated binding helpers, environment toggles, and namespaced constants that need a project prefix.
- When fixing warnings for short, future-facing hooks such as one-line static moves, extern declarations, or placeholder assignments, prefer commenting them out over deleting them. Add a short comment explaining the warning being fixed and when the line should come back, for example that Linux `-Wall` warns while the hook is unused.
- For WoW UI code (`games/world-of-warcraft/ui/`), do not fail silently. When a required script, handler, renderer resource, or fallback path is missing, emit a clear `UIWow:` log that explains what was skipped and why. Prefer one-time warnings for per-frame paths to avoid log spam.

## General
- Minimize vertical space. Prefer fewer, denser lines over many short ones.
- Keep C source lines at or under 120 characters. Single-statement helpers may stay on one line when they fit,
  but do not chain long runs of API calls or argument-heavy expressions horizontally.
- Single-statement functions go on one line: `int f(void) { return 0; }`
- Omit braces for single-statement `if`/`else`/`while` bodies.
- Keep control-flow keywords at the start of their own line in normal code paths. Do not write chained forms like `...; if (...)` or `...; while (...)` on the same physical line.
- Add a short comment before each non-trivial function describing purpose and scope.

## Packing multiple statements
- Chain sequential, logically related statements on one line with `;`:
  `lua_pushvalue(L, 2); lua_pushvalue(L, 1);`
- Stop packing when the line would exceed 120 characters or when the calls form a list of similar operations.
  In those cases, use one operation per line or a small table/helper so the repetition reads vertically.
- Merge declarations that belong to the same logical step:
  `int key_idx = lua_absindex(L, -2), val_idx = lua_absindex(L, -1);`

## Ternary + comma operator for conditional initialization
- When an assignment depends on a condition that also has side effects, use
  the comma operator inside the ternary branch to keep it a single expression:
```c
  int nargs = lua_isnoneornil(L, 2) ? 1 : (lua_pushvalue(L, 2), lua_xmove(L, co, 1), 2);
```
  The comma operator sequences the side-effect calls; the branch evaluates to
  the final value. Use this to avoid splitting a variable's initialization from
  its declaration.

## Braces
- Omit braces when the body is a single statement or a single comma-chained expression.
- Keep braces for multi-statement `while` bodies, and anything
  that would become ambiguous without them.

## Pointers and casts
- Inline pointer-through-cast writes where the intent is clear:
  `*((struct Object **)lua_getextraspace(L)) = self;`

## What to avoid
- Do not introduce helper variables just to name an intermediate result if the
  expression is already readable inline.
- Do not add blank lines between short, related statements.
- Do not split a declaration and its first assignment onto separate lines.

## Architecture

- Structure the project similarly to Quake 2: separate modules for rendering, game logic, input, sound, networking, etc.
- Game state should be managed in a straightforward, imperative style consistent with Quake 2's `g_*.c` / `cl_*.c` / `r_*.c` file layout.
- The engine and game code may be separated (similar to Quake 2's `game.dll` / `ref_gl` split) to allow modular replacement of subsystems.
- Runtime modules communicate through function tables (`R_GetAPI`, `UI_GetAPI`, game imports/exports). Prefer this boundary over direct cross-module dependencies.
- Use cvars for runtime choices: `r_module`, `ui_module`, `g_module`, and `com_frame_limit`.

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

## Engine/Game Boundary (Strict)

- Engine modules (`renderer/`, `client/`, `common/`, `server/` core paths) must remain game-agnostic.
- Never hardcode game-specific asset names, animation names, frame names, map/script conventions, or franchise-specific literals in engine code.
- Examples of forbidden engine literals: specific sequence names like `"MainMenu Stand"`, specific UI roots, or title-specific asset assumptions.
- If behavior needs title/game knowledge, put that policy in the selected game source tree under `games/<game>/` and pass generic parameters into engine APIs.
- Game-owned sources live under `games/warcraft-3/`, `games/world-of-warcraft/`, and `games/starcraft-2/`, with `game/`, `renderer/`, and, where present, `ui/` subdirectories. Warcraft III also owns `jass/`, `sheet/`, and `tests/` under `games/warcraft-3/`. The Makefile still exposes friendly targets such as `game`, `renderer`, and `ui` for the default Warcraft III build.
- The renderer library is a compound module: engine renderer sources in `renderer/` are compiled together with the selected game's `games/<game>/renderer/` sources. Engine renderer code calls the `R_Game*` API declared in `renderer/r_game.h`; it must not switch on MDX/M2/M3 model formats or include game renderer internals directly.
- Prefer generic fallbacks in engine code (caller-provided names, first available sequence, data-driven metadata) rather than title-specific heuristics.

## Build And Linking

- Never add `DYLIB_LOOKUP := -Wl,-undefined,dynamic_lookup` or otherwise rely on `-Wl,-undefined,dynamic_lookup` in this repository.
- If a target has unresolved symbols, fix the dependency graph or shared implementation instead of weakening the linker contract.

## Domain

- This is a **real-time strategy game** (RTS), so game logic should account for unit management, pathfinding, resource gathering, building construction, and large numbers of entities — adapted from the Quake 2 entity/server model where applicable.

## MPQ Inspection Workflow

- When investigating Warcraft III assets, prefer using the local CLI utility `build/bin/mpqtool` instead of guessing file paths.
- Never define in C code any UI/layout/asset values that can be read from MPQ data files such as XML, Lua, FDF, DBC, SLK, or similar. Build or extend systems that load and use the authoritative MPQ data instead of embedding fallback literals in engine code.
- Tests must not depend on a developer's local Warcraft III data or `War3.mpq`. Add any archive fixtures under `tests/resources-src`, pack them into the generated `build/tests/tests.mpq` through `make test-assets`, and point tests at that fixture MPQ instead.
- Tests must not read from ignored local extraction folders such as `data/fdf` or `data/Warcraft III`. If a test needs FDF, map, texture, model, or other archive content, copy the minimal fixture into `tests/resources-src`, add it to `build/tests/tests.mpq`, and read it from that generated archive.
- When a test fixture intentionally replaces an actual game archive file with custom content, use the same archive path and filename as the real game file. Do not invent project-specific replacement names for files that are meant to stand in for game files; keep the name WoW/Warcraft-style and make only the contents custom.
- Use `ls` mode to browse archive structure incrementally:
	- `build/bin/mpqtool -mpq <path-to-mpq> ls`
	- `build/bin/mpqtool -mpq <path-to-mpq> ls <subdir>`
- Use `cat` mode to dump file contents to stdout so output can be piped or redirected:
	- `build/bin/mpqtool -mpq <path-to-mpq> cat <archive-file>`
	- Example with redirect: `build/bin/mpqtool -mpq <path-to-mpq> cat Scripts/war3map.j > /tmp/war3map.j`
- Normalize slashes as needed when querying paths; both `\` and `/` are accepted by the tool input.
- For agent workflows, default to this tool whenever you need to discover MPQ contents, inspect text assets, or extract raw file bytes for analysis.

## WoW Character Display Workflow

- Do not fix WoW character clothing, hair, or appearance bugs by hardcoding one model path, one race, one item texture set, or one group of M2 skin sections in engine code. Character appearance is data-driven by M2 skin section IDs plus DBC display records.
- For player/NPC character models, inspect `CharSections.dbc`, `CharHairGeosets.dbc`, `CharHairTextures.dbc`, `CharStartOutfit.dbc`, `ItemDisplayInfo.dbc`, and `HelmetGeosetVisData.dbc` before changing renderer policy. Local DBC files live under `DBFilesClient` in the WoW MPQs and can be inspected with `build/bin/mpqtool`.
- Some classic-era DBCs have a logical field count that is larger than `record_size / 4`; for example local `CharStartOutfit.dbc` reports 41 fields with 152-byte records. Parse DBC records by validating the file envelope and checking each accessed field against `record_size`, not by rejecting the whole file when `field_count * 4` exceeds `record_size`.
- `ItemDisplayInfo.dbc` carries item model names/textures, geoset groups, flags, helmet visibility, and eight character texture component slots. In the local classic-era 23-field layout, texture components start at field 14; in the documented 25-field TBC/Wrath layout, they start at field 15. The component slots map to upper arm, lower arm, hand, upper torso, lower torso, upper leg, lower leg, and foot.
- M2 skin section IDs are grouped by hundreds. Character renderers should select one variant per relevant group at draw time or through a variant cache keyed by appearance/equipment, not by throwing away sections at model-load time. Loading all batches preserves future per-entity equipment changes.
- Do not infer visible geosets from non-empty component textures. WoW keeps default character geosets such as gloves/boots/ears/sleeves/legs/robe/pelvis visible unless item geoset groups override them; item component texture stems are normally pasted into a composed character skin texture. The `whoa-master` component path documents defaults in `ComponentData.hpp` and applies them in `CCharacterComponent::GeosRenderPrep`.
- Component texture names in `ItemDisplayInfo.dbc` are stems, not full archive paths. Resolve them under `Item\TextureComponents\<slot-folder>\` and try gender-specific suffixes (`_M`, `_F`) before universal (`_U`) where the archive contains universal components.
- The whoa-master character component rectangles are documented in 512x512 atlas space. Classic body skins such as `Character\Orc\Male\OrcMaleSkin00_00.blp` may be 256x256, so scale component paste rectangles to the actual destination body texture size before compositing. Otherwise all right-half slots such as torso, pants, boots, and feet land outside the texture and silently disappear.
- The current packed WoW `equipment` bytes are local slot item indices, not raw item IDs. Treat each byte as an index into a WoW-owned 256-entry item list selected by race, gender, and slot, with index `0` meaning empty and nonzero indices resolving through DBC-backed `ItemDisplayInfo` display IDs in renderer/tool code. Keep the game state packed with `Wow_PackEquipment(...)` rather than widening entity/player state for preview gear.
- Grounded WoW actors must use the same one-dimensional yaw path as Warcraft III/OpenWarcraft3 entities: game code writes `entityState_t.angle` in radians, the client interpolates it with `LerpRotation(...)`, and grounded M2 rendering consumes `renderEntity_t.angle`. Do not put player/creature yaw back into `entityState_t.rotation` or interpolate a 3D rotation vector for dynamic actors; `rotation` is reserved for static object/model transforms that genuinely need three axes.
- Useful public references:
	- TrinityCore `ItemDisplayInfo.dbc` field layout and flags: https://trinitycore.info/files/DBC/335/itemdisplayinfo
	- WoTLK Modding Wiki `ItemDisplayInfo`: https://wotlkdev.github.io/wiki/dbc/ItemDisplayInfo
	- getMaNGOS TBC `ItemDisplayInfo` field list: https://www.getmangos.eu/wiki/referenceinfo/dbcfiles/mangosonedbc/ItemDisplayInfo-r7649/
	- `wow_dbc` parser crate notes for vanilla/TBC/Wrath DBC schemas: https://github.com/gtker/wow_dbc

## MDX Inspection Workflow (mdxtool)

- Use `build/bin/mdxtool` to validate MDX assets and detect data problems before debugging render code.
- CLI synopsis:
	- `build/bin/mdxtool -mpq <path-to-mpq> -model <archive-model-path> [--anim <sequence>] [--use-model-camera] [--front-ortho] [--info] [--dump-all] [--once]`
- Viewer mode (opens window):
	- `build/bin/mdxtool -mpq <path-to-mpq> -model <archive-model-path>`
	- Example: `build/bin/mdxtool -mpq data/Warcraft\ III/War3.mpq -model UI\Glues\MainMenu\WarCraftIIILogo\WarCraftIIILogo.mdx`
- Front-ortho viewer mode for flat UI layers:
	- `build/bin/mdxtool -mpq <path-to-mpq> -model <archive-model-path> --front-ortho`
	- Example: `build/bin/mdxtool -mpq data/Warcraft\ III/War3.mpq -model UI\Glues\SpriteLayers\TopRightPanel.mdx --front-ortho`
- Info mode (no window, stdout only):
	- `build/bin/mdxtool -mpq <path-to-mpq> -model <archive-model-path> --info`

Common flags:
- `--anim <sequence>`: render or inspect a specific sequence by name, e.g. `--anim "MainMenu Stand"`.
- `--use-model-camera`: prefer the first embedded MDX camera when present.
- `--front-ortho`: use a front-facing orthographic preview camera for flat UI models without useful embedded cameras.
- `--info`: print model metadata and chunk counts without opening a window.
- `--dump-all`: print loaded model details including nodes, bones, geosets, materials, and cameras.
- `--once`: render one frame and exit; useful for scripted diagnostics and agent workflows.

When to use `--info`:
- Confirm the model exists and loads from MPQ path.
- Check whether a model has cameras (`CAMS`) for portrait/model-camera paths.
- Check sequence counts (`SEQS`) for Birth/Stand/Death animation expectations.
- Check textures (`TEXS`) and pivots (`PIVT`) for basic model completeness.
- Check optional systems that often explain missing visuals:
	- lights (`LITE`)
	- particle emitters (`PRE2`)
	- attachments (`ATCH`)
	- helpers (`HELP`)
	- bones (`BONE`)
	- collision shapes (`CLID`)
	- geosets (`GEOS`) and geoset anims (`GEOA`)

Expected output style:
- `mdxtool --info: model=<path> size=<bytes>`
- one line per relevant chunk with counts, e.g. `SEQS: count=...`, `CAMS: count=...`, `LITE: count=...`.

Agent guidance:
- Prefer `--info` first for existence, chunk counts, and camera availability.
- Use `--dump-all --once` when chunk summaries are not enough and you need loaded geoset, material, bone, or animation-state details.
- Use `--front-ortho` for glue sprites, panel layers, logos, and other flat UI-facing models.
- Use `--use-model-camera` only when the model actually contains a useful embedded camera.

Use this output in bug reports/diagnostics so rendering issues can be triaged from data facts (camera/lights/particles/sequence availability) without requiring screenshots.

## UI Text Renderer Workflow

- Use `make run-ui-text` to inspect client-side UI rendering without opening a window or taking screenshots.
- Default command:
	- `make run-ui-text UI_CMD=menu_main`
- Equivalent explicit command:
	- `build/bin/openwarcraft3 -data data/Warcraft\ III +r_module stdout +com_frame_limit 1 +menu_main`
- `+r_module stdout` selects the text renderer.
- Menu-only diagnostics do not enter LAN browser/connect/host paths, so UDP sockets are not opened.
- `+com_frame_limit 1` exits after one frame and skips writing `share/config.cfg`.

Expected output includes:
- `load_texture`, `load_model`, `load_font`
- `draw_portrait`, `draw_sprite`
- `draw_image` with texture name, screen rect, UV rect, blend mode, color, rotation
- `draw_text` with font, rect, measured size, color, translated text
- `draw_sys_text` for console overlay output

Agent guidance:
- Prefer the stdout renderer first for UI layout, FDF translation, button state, backdrop tiling, UV, color-code, and menu-command composition bugs.
- Use `mdxtool --info` first when a UI model itself may be missing or malformed.
- `fdftool` is no longer the primary UI inspection path; Phase 8 moved UI rendering into the client-side UI library.
- For startup-menu diagnostics, invoke a concrete menu command directly with a leading `+`. UI navigation is command-based; do not add router-style paths such as `/credits` or `/options`, do not add a generic `ui` console command, and do not add startup cvars for menu routing. Register concrete menu commands such as `menu_credits` or `menu_options` instead. Examples:
	- `build/bin/openwarcraft3 -data data/Warcraft\ III +menu_main`
	- `build/bin/openwarcraft3 -data data/Warcraft\ III +menu_single_player_campaign`
	- `make run-ui-text UI_CMD=menu_single_player_campaign`

## Time Profiler Workflow

- For runtime CPU profiling on macOS, prefer Instruments `xctrace` with the local `xctraceprof` parser.
- Record a real run with Time Profiler:
	- `/Applications/Xcode.app/Contents/Developer/usr/bin/xctrace record --template "Time Profiler" --time-limit 20s --output /private/tmp/openwarcraft3-orc01.trace --launch -- /Users/igor/Developer/openwarcraft3/build/bin/openwarcraft3 -data "/Users/igor/Developer/openwarcraft3/data/Warcraft III" +map "Maps\\Campaign\\Orc01.w3m"`
- Export the time-profile table to XML:
	- `/Applications/Xcode.app/Contents/Developer/usr/bin/xctrace export --input /private/tmp/openwarcraft3-orc01.trace --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' > /private/tmp/openwarcraft3-orc01-timeprof.xml`
- Summarize the relevant window and focus symbols:
	- `build/bin/xctraceprof --window 8:18 --top 25 /private/tmp/openwarcraft3-orc01-timeprof.xml`
	- `build/bin/xctraceprof --window 8:18 --focus R_RenderFogOfWar --top 25 /private/tmp/openwarcraft3-orc01-timeprof.xml`
	- `build/bin/xctraceprof --window 8:18 --focus SV_Frame --top 20 /private/tmp/openwarcraft3-orc01-timeprof.xml`
- Use `R_RenderFogOfWar` when investigating legacy renderer-owned fog work, `CL_ParseFogOfWar` / `R_SetFogOfWarData` for client texture upload, and `SV_Frame` / `G_FowUpdate` when investigating server/game tick work such as authoritative fog, movement, and snapshots.

## UI Screen Authoring Conventions

- In client/UI code, never define or hardcode UI elements, layout coordinates, textures, frame names, or control structures that can be read from FDF. Parse and reuse the actual FDF frames/templates, then bind dynamic data into those frames.
- The only exception is selected game code under `games/<game>/game/`, where there is no FDF parser. Server-authored gameplay HUD payloads may generate simple proxy frames there when needed.
- In `games/warcraft-3/ui/screens/*.c`, prefer `UI_FRAME(...)` and `UI_CHILD_FRAME(...)` for readability and FDF-name coupling.
- Use `UI_FindChildFrame(...)` when it is clearly shorter or cleaner than introducing temporary macro-bound locals.
- Avoid excessive pointer null-check noise in screen controllers. Prefer one scene-level readiness gate (early return) over repeated per-widget checks.
- If a required root frame is missing, fail fast for that screen and skip further scene setup/update work.
- Keep frame names data-driven by FDF and avoid hardcoded lookup strings when macro-based lookup can use the frame identifier directly.
