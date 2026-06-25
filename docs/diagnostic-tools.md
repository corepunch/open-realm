# Diagnostic Tools

## MPQ Inspection (mpqtool)

- When investigating Warcraft III assets, prefer using the local CLI utility `build/bin/mpqtool` instead of guessing file paths.
- Use `ls` mode to browse archive structure incrementally:
	- `build/bin/mpqtool -mpq <path-to-mpq> ls`
	- `build/bin/mpqtool -mpq <path-to-mpq> ls <subdir>`
- Use `cat` mode to dump file contents to stdout:
	- `build/bin/mpqtool -mpq <path-to-mpq> cat <archive-file>`
	- Example with redirect: `build/bin/mpqtool -mpq <path-to-mpq> cat Scripts/war3map.j > /tmp/war3map.j`
- Normalize slashes as needed; both `\` and `/` are accepted.
- Never define in C code any UI/layout/asset values that can be read from MPQ data files such as XML, Lua, FDF, DBC, SLK, or similar. Build or extend systems that load and use the authoritative MPQ data instead of embedding fallback literals in engine code.
- Default to this tool whenever you need to discover MPQ contents, inspect text assets, or extract raw file bytes for analysis.

## MDX Inspection (mdxtool)

- Use `build/bin/mdxtool` to validate MDX assets and detect data problems before debugging render code.
- CLI synopsis:
	- `build/bin/mdxtool -mpq <path-to-mpq> -model <archive-model-path> [--anim <sequence>] [--use-model-camera] [--front-ortho] [--info] [--dump-all] [--once]`
- Viewer mode (opens window):
	- `build/bin/mdxtool -mpq <path-to-mpq> -model <archive-model-path>`
- Front-ortho viewer (flat UI layers):
	- `build/bin/mdxtool -mpq <path-to-mpq> -model <archive-model-path> --front-ortho`
- Info mode (no window, stdout only):
	- `build/bin/mdxtool -mpq <path-to-mpq> -model <archive-model-path> --info`
- Example: `build/bin/mdxtool -mpq data/Warcraft\ III/War3.mpq -model UI\Glues\MainMenu\WarCraftIIILogo\WarCraftIIILogo.mdx`
- Front-ortho example: `build/bin/mdxtool -mpq data/Warcraft\ III/War3.mpq -model UI\Glues\SpriteLayers\TopRightPanel.mdx --front-ortho`

Common flags:
- `--anim <sequence>`: render or inspect a specific sequence by name.
- `--use-model-camera`: prefer the first embedded MDX camera when present.
- `--front-ortho`: use a front-facing orthographic preview camera for flat UI models.
- `--info`: print model metadata and chunk counts without opening a window.
- `--dump-all`: print loaded model details including nodes, bones, geosets, materials, and cameras.
- `--once`: render one frame and exit; useful for scripted diagnostics.

When to use `--info`:
- Confirm the model exists and loads from MPQ path.
- Check whether a model has cameras (`CAMS`), sequences (`SEQS`), textures (`TEXS`), pivots (`PIVT`).
- Check optional systems that often explain missing visuals: lights (`LITE`), particle emitters (`PRE2`), attachments (`ATCH`), helpers (`HELP`), bones (`BONE`), collision shapes (`CLID`), geosets (`GEOS`), geoset anims (`GEOA`).

Agent guidance:
- Prefer `--info` first for existence, chunk counts, and camera availability.
- Use `--dump-all --once` when chunk summaries are not enough.
- Use `--front-ortho` for glue sprites, panel layers, logos, and other flat UI-facing models.
- Use `--use-model-camera` only when the model actually contains a useful embedded camera.

Expected output style:
- `mdxtool --info: model=<path> size=<bytes>`
- one line per relevant chunk with counts, e.g. `SEQS: count=...`, `CAMS: count=...`, `LITE: count=...`.

Use this output in bug reports/diagnostics so rendering issues can be triaged from data facts (camera/lights/particles/sequence availability) without requiring screenshots.

## UI Text Renderer

- Use `make run-ui-text` to inspect client-side UI rendering without opening a window.
- Default command: `make run-ui-text UI_CMD=menu_main`
- Equivalent explicit command: `build/bin/openwarcraft3 -data data/Warcraft\ III +r_module stdout +com_frame_limit 1 +menu_main`
- `+r_module stdout` selects the text renderer. `+com_frame_limit 1` exits after one frame.

Expected output includes: `load_texture`, `load_model`, `load_font`, `draw_portrait`, `draw_sprite`, `draw_image`, `draw_text`, `draw_sys_text`.

Agent guidance:
- Prefer the stdout renderer first for UI layout, FDF translation, button state, backdrop tiling, UV, color, and menu-command bugs.
- Use `mdxtool --info` first when a UI model itself may be missing or malformed.
- `fdftool` is no longer the primary UI inspection path; Phase 8 moved UI rendering into the client-side UI library.
- For startup-menu diagnostics, invoke a concrete menu command directly with `+`. Do not add router-style paths, a generic `ui` console command, or startup cvars for menu routing. Register concrete commands such as `menu_credits` or `menu_options`. Examples:
	- `build/bin/openwarcraft3 -data data/Warcraft\ III +menu_main`
	- `make run-ui-text UI_CMD=menu_single_player_campaign`

## Time Profiler (macOS)

- For runtime CPU profiling on macOS, prefer Instruments `xctrace` with the local `xctraceprof` parser.
- Record a run:
	- `/Applications/Xcode.app/Contents/Developer/usr/bin/xctrace record --template "Time Profiler" --time-limit 20s --output /private/tmp/openwarcraft3-orc01.trace --launch -- /Users/igor/Developer/openwarcraft3/build/bin/openwarcraft3 -data "/Users/igor/Developer/openwarcraft3/data/Warcraft III" +map "Maps\\Campaign\\Orc01.w3m"`
- Export to XML:
	- `/Applications/Xcode.app/Contents/Developer/usr/bin/xctrace export --input /private/tmp/openwarcraft3-orc01.trace --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' > /private/tmp/openwarcraft3-orc01-timeprof.xml`
- Summarize:
	- `build/bin/xctraceprof --window 8:18 --top 25 /private/tmp/openwarcraft3-orc01-timeprof.xml`
	- `build/bin/xctraceprof --window 8:18 --focus R_RenderFogOfWar --top 25 /private/tmp/openwarcraft3-orc01-timeprof.xml`
	- `build/bin/xctraceprof --window 8:18 --focus SV_Frame --top 20 /private/tmp/openwarcraft3-orc01-timeprof.xml`
- Use `R_RenderFogOfWar` for renderer-owned fog, `CL_ParseFogOfWar`/`R_SetFogOfWarData` for client texture upload, `SV_Frame`/`G_FowUpdate` for server/game tick work.
