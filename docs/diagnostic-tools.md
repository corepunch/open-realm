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

## DBC Inspection (dbctool)

- Use `build/bin/dbctool` to inspect or create WoW DBC files without writing C code or running the game.
- DBCs live inside WoW MPQ archives under `DBFilesClient\`. You can also pass a raw extracted file with `-file`.
- CLI synopsis:
    - `build/bin/dbctool -mpq <archive.mpq> info <DBFilesClient\File.dbc>`
    - `build/bin/dbctool -mpq <archive.mpq> dump <DBFilesClient\File.dbc> [max-rows]`
    - `build/bin/dbctool -mpq <archive.mpq> get  <DBFilesClient\File.dbc> <row> <field>`
    - `build/bin/dbctool -mpq <archive.mpq> str  <DBFilesClient\File.dbc> <row> <field>`
    - `build/bin/dbctool -file <file.dbc>   info|dump|get|str ...`

Commands:
- `info` — print header: record count, field count, record size, string block size.
- `dump` — print all (or up to `max-rows`) records as tab-separated uint32 columns, one row per line.
- `get  r f` — print field `f` of row `r` (0-based) as a uint32. Use for ID, flags, or integer columns.
- `str  r f` — resolve field `f` of row `r` as a string-block offset and print the string. Use for name/filename columns.

Examples:
```
build/bin/dbctool -mpq data/world-of-warcraft/Data/patch.mpq info DBFilesClient\\ChrRaces.dbc
build/bin/dbctool -mpq data/world-of-warcraft/Data/patch.mpq dump DBFilesClient\\ChrClasses.dbc 10
build/bin/dbctool -mpq data/world-of-warcraft/Data/patch.mpq get  DBFilesClient\\ChrRaces.dbc 0 0
build/bin/dbctool -mpq data/world-of-warcraft/Data/patch.mpq str  DBFilesClient\\ChrRaces.dbc 0 17
build/bin/dbctool -file /tmp/ChrRaces.dbc dump
```

### Writing DBC files for tests

Use `create`, `set`, `setstr`, and `save` to build fixture DBCs for unit tests. No `-mpq` or `-file` prefix needed.

- `create <out.dbc> <fields> <record_size>` — create an empty DBC (record_size must be 4× fields).
- `set <file.dbc> <row> <field> <value>` — set a uint32 field. Row is auto-created.
- `setstr <file.dbc> <row> <field> <string>` — set a string field (value stored in string block).
- `save <file.dbc>` — write the in-memory DBC to disk and clean up.

Example:
```
build/bin/dbctool create /tmp/test.dbc 3 12
build/bin/dbctool set /tmp/test.dbc 0 0 1
build/bin/dbctool setstr /tmp/test.dbc 0 1 "Hello"
build/bin/dbctool set /tmp/test.dbc 0 2 42
build/bin/dbctool save /tmp/test.dbc
build/bin/dbctool -file /tmp/test.dbc dump
```

Agent guidance:
- Always use this tool when investigating a DBC layout mismatch or verifying field indices in `ui_dbc.c`.
- Do not hardcode field indices in C code without first confirming them with `dbctool info` and `dbctool dump`.
- Before hardcoding any race/class/faction/item/display ID or name in C or Lua: confirm the real value from the authoritative DBC.
- Prefer `info` first, then `str` for named fields, then `dump` when you need the full picture.
- Pipe `dump` output through `grep`, `awk`, or `cut` for quick filtering.
- Use `create`/`set`/`setstr`/`save` to generate minimal DBC fixtures for tests that exercise DBC-dependent code paths.

## MDX Animation Reference (WarsmashModEngine)

The `data/WarsmashModEngine/` directory contains a Java port of the mdx-m3-viewer used as reference for MDX animation behaviour. Key differences from the C implementation:

- **Keyframe wrapping** (`SdSequence.getValue` in `AnimatedObject.java`): when the animation frame exceeds the last keyframe within the sequence interval, the game interpolates from the last keyframe's value back toward the first keyframe's value — it does NOT clamp to the last pose.
- **Per-sequence keyframe filtering**: Warsmash builds a separate filtered keyframe list for each sequence at load time (`SdSequence` constructor), selecting keyframes with `start <= frame <= end` (inclusive). Our code filters at evaluation time with exclusive upper bound.

When investigating animation crop/truncation bugs, the relevant source files are:
- `data/WarsmashModEngine/.../mdx/AnimatedObject.java`
- `data/WarsmashModEngine/.../mdx/SdSequence.java`
- `data/WarsmashModEngine/.../mdx/Sd.java`
- `data/WarsmashModEngine/.../mdx/MdxComplexInstance.java` (updateAnimations method)

## UI Text Renderer

- Use `make run-ui-text` to inspect client-side UI rendering without opening a window.
- Default command: `make run-ui-text UI_CMD=menu_main`
- Equivalent explicit command: `build/bin/openwarcraft3 -data data/Warcraft\ III +r_module stdout +com_frame_limit 1 +menu_main`
- `+r_module stdout` selects the text renderer. `+com_frame_limit 1` exits after one frame.

Expected output includes: `load_texture`, `load_model`, `load_font`, `draw_portrait`, `draw_sprite`, `draw_image`, `draw_text`, `draw_sys_text`.

Agent guidance:
- Prefer the stdout renderer first for UI layout, FDF translation, button state, backdrop tiling, UV, color, and menu-command bugs.
- Use `mdxtool --info` first when a UI model itself may be missing or malformed.
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
