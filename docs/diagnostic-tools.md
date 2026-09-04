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
- Always use this tool when investigating a DBC layout mismatch or verifying field indices in `menu_dbc.c`.
- Do not hardcode field indices in C code without first confirming them with `dbctool info` and `dbctool dump`.
- Before hardcoding any race/class/faction/item/display ID or name in C or Lua: confirm the real value from the authoritative DBC.
- Prefer `info` first, then `str` for named fields, then `dump` when you need the full picture.
- Pipe `dump` output through `grep`, `awk`, or `cut` for quick filtering.
- Use `create`/`set`/`setstr`/`save` to generate minimal DBC fixtures for tests that exercise DBC-dependent code paths.
- For race spawn/map investigations, start with `docs/games/world-of-warcraft/spawn-and-teleport.md`; it records the server-table/Map.dbc ownership split and bounded launch commands.
- For first-login camera investigations, start with `docs/games/world-of-warcraft/cinematics.md`; it records the verified row/field indices and camera-only M2 command.
- For floating ADT objects, read `docs/games/world-of-warcraft/terrain-and-world-rendering.md` before changing game-object code; renderer MDDFs and interactive game entities are separate consumers.

## MDX Animation Reference (WarsmashModEngine)

The `data/WarsmashModEngine/` directory contains a Java port of the mdx-m3-viewer used as reference for MDX animation behaviour. Key differences from the C implementation:

- **Keyframe wrapping** (`SdSequence.getValue` in `AnimatedObject.java`): when the animation frame exceeds the last keyframe within the sequence interval, the game interpolates from the last keyframe's value back toward the first keyframe's value — it does NOT clamp to the last pose.
- **Per-sequence keyframe filtering**: Warsmash builds a separate filtered keyframe list for each sequence at load time (`SdSequence` constructor), selecting keyframes with `start <= frame <= end` (inclusive). Our code filters at evaluation time with exclusive upper bound.

When investigating animation crop/truncation bugs, the relevant source files are:
- `data/WarsmashModEngine/.../mdx/AnimatedObject.java`
- `data/WarsmashModEngine/.../mdx/SdSequence.java`
- `data/WarsmashModEngine/.../mdx/Sd.java`
- `data/WarsmashModEngine/.../mdx/MdxComplexInstance.java` (updateAnimations method)

## UI Diagnostics

- Use `mdxtool --info` first when a UI model itself may be missing or malformed.
- For startup-menu diagnostics, invoke a concrete menu command directly with `+`. Do not add router-style paths, a generic `ui` console command, or startup cvars for menu routing. Register concrete commands such as `menu_credits` or `menu_options`. Examples:
	- `build/bin/openwarcraft3 -data data/Warcraft\ III +menu_main +com_frame_limit 1`
	- `build/bin/openwarcraft3 -data data/Warcraft\ III +menu_single_player_campaign +screenshot 5 +com_frame_limit 10``

## AzerothCore SQL Extraction (extract_quest_data.py)

- Use `data/WoWee/tools/extract_quest_data.py` to generate WoW server data from AzerothCore SQL dumps.
- Source: `data/azerothcore-wotlk/data/sql/base/db_world/` (quest_template, creature, etc.)
- Output: `build/generated/g_quests.c` and `quest_spawns.csv`

```sh
python3 data/WoWee/tools/extract_quest_data.py                        # regenerate from existing IDs
python3 data/WoWee/tools/extract_quest_data.py --quest-ids 5,6,7,8,9  # specific quests
python3 data/WoWee/tools/extract_quest_data.py --dry-run              # preview only
```

Agent guidance:
- Use this tool when adding new quests or refreshing quest data from upstream SQL.
- Item-collection quests (`RequiredItemId` in SQL) need manual mapping to kill objectives.
- The tool resolves creature entries → display IDs via `creature_template_model` for kill objectives.
- After regenerating, run `make test-wow-game` to verify data integrity.

## Time Profiler (macOS)

For Linux WC3 entity-rendering profiles and CPU-sample interpretation, see
[the August 2026 ARM profile review](#linux-wc3-arm-profile-review-2026-08-26).

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

## Linux WC3 ARM Profile Review (2026-08-26)

Source: [open-realm perf report, address mappings, and interpretation](https://gist.github.com/sookyboo/632574119cc8e03abb7da133d5c039f0).
Reviewed against local revision `011be57b`. The user reports 21 FPS; the gist does not identify the exact build revision,
map/camera, build flags, GL startup information, or wall-clock/GPU timings. It contains about 7K `cycles:ppp` samples,
zero reported lost samples, and additional audio/driver threads. Percentages below are sampled CPU-cycle shares,
not frame milliseconds or GPU execution time. Inclusive parent and child percentages must not be added.

| Profile path | Sample share | Interpretation |
|---|---:|---|
| Main view `R_DrawEntities` | 46.54% | Includes models and their shadows; all call sites total 47.10% |
| MDX geoset branch | 18.87% | Geometry/material submission, including driver work |
| MDX bone-binding branch | 16.31% | Node evaluation, clearing/scanning, palette construction and GL upload |
| `R_RenderShadow` | 8.33% | Separate from the two MDX branches |
| `CL_AddEntities` | 7.11% self | Address `0x15a14`; full client-slot traversal in current source |
| `CL_ParseFrame` | 4.33% self | Address `0xc7e0`; another full client-slot traversal per snapshot |
| `G_RunEntities` | 10.18% inclusive | Simulation; not the largest aggregate target |

### Source-confirmed work and limits

- `client/cl_view.c:CL_AddEntities` scans all 16,384 client slots each rendered frame, even when few are occupied.
  `client/cl_parse.c:CL_ParseFrame` scans the same capacity and copies active states each snapshot. Together their
  self samples are 11.44%. Occupancy counters and instruction-level attribution are needed before assigning all
  this cost to empty-slot scanning. `git blame` traces the render-side capacity loop to `d68a52eda` (2023).
- `games/warcraft-3/renderer/mdx/r_mdx_anim.c:MDLX_BindBoneMatrices` clears 1,024 matrices (64 KiB), scans
  1,024 node slots twice, constructs 128 palette entries and uploads `tr.bone_count` matrices per rendered model.
  The fixed-capacity work is attributed by blame to `2bfbd4e03`. Only about 5.11% of the report is under the
  `R_CalculateNodeMatrix` call site; do not advertise the entire 16.31% as removable by local-pose caching.
- Local transforms also depend on interpolation fraction and global-sequence time (`tr.viewDef.time`, or
  `SDL_GetTicks()` when zero). `(model, frame, oldframe)` alone is not a sufficient persistent cache key.
  Billboard global transforms depend on the model transform. Measure reuse before introducing a cache.
- `MDLX_BindGeosetMatrixPalette` uploads a geoset-specific palette after model bone binding. Count uploads and
  inspect all consumers before deciding whether the earlier upload can be removed.
- `games/warcraft-3/renderer/w3m/r_war3map_ground.c:R_RenderRectSplat` batches tiles within one splat, then flushes
  at the end of each call. `R_RenderModel` interleaves each entity's splats and model drawing. Existing batching
  is not batching across entities; changing pass order must preserve blending/depth behavior. Blame attributes
  the current tile batch to `4af98697c`.
- `r_mdx_merge_opaque` from the gist interpretation is absent from this checkout and the available history search
  for `games/warcraft-3/renderer`. It may be fork-specific; setting it here does not enable a merging path.
- The raw driver addresses resolve only to `libGL.so.1`, not named GL functions. The interpretation identifies
  gl4es, but the report alone does not prove its version or separate state overhead from submission/stalls.

### Next measurements

1. Obtain the tested revision, release/debug flags, renderer startup log and exact scene. Compare a clean
   `BUILD=release GL_BACKEND=gles3 MSAA=0` Linux build with the existing backend; see
   [build profiles](build-and-renderer-platforms.md#build-profiles). A local Mac run cannot validate ARM/gl4es speed.
2. On the same scene/camera, collect `r_stats 1` with `r_unit_shadows 1` and `0`, restoring `1` afterward.
   Example bounded ROC launch (repeat with `-tft`; substitute the reported map when known):

   ```sh
   build/bin/openwarcraft3 -data 'data/Warcraft III' +map 'Maps\Campaign\Orc01.w3m' +set r_stats 1 +set r_unit_shadows 0 +com_frame_limit 300
   ```

3. Add targeted temporary logs for occupied client slots, submitted/visible models, occupied MDX nodes,
   actual matrix upload counts, and unique poses including time/interpolation. Capture a bounded run before
   changing traversal, caching, or draw submission. Verify ROC and TFT and run `make test` for implementation changes.

21 FPS is 47.62 ms/frame; 30 FPS requires 33.33 ms/frame, a 30% frame-time reduction. The 8.33% shadow sample share
cannot establish the wall-time benefit of disabling shadows. Even under a simplified proportional CPU-only model,
removing that entire share predicts only about 22.9 FPS, not 30. Prioritize MDX submission/bone work and the two
client traversals, then reprofile; the report does not justify a promised FPS improvement.
