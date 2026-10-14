# Reproducing retail Warcraft III score measurements

This procedure records how the UndeadX07c retail score observations were
captured and how the unit-cost data was compared with those observations. It
uses disposable local map copies. Never edit the campaign map inside a retail
MPQ or overwrite the original signed map.

The checked-in documentation describes the procedure; the actual extracted
maps, screenshots, score traces and test variants live under the ignored
`build/retail-score-test/` directory in the investigation workspace. Those
files are evidence artifacts, not inputs that a clean checkout can assume are
present. Keep each run's map, trace, screenshot, game version and observed
difficulty together.

## What was measured

The reference is TFT retail 1.29.2 on `Maps\\FrozenThrone\\Campaign\\UndeadX07c.w3x`.
The controlled cost runs used Normal difficulty. They captured both the retail
`PLAYER_SCORE_UNIT_TOTAL` native through passive JASS logging and the final
Units page. For the unit-cost tests, the important sample is
`intro_cleanup_plus_8s`, after the scripted opening removals and eight seconds
of gameplay. Always check the trace's `difficulty=` field; the `-loadfile`
launch used in this investigation defaulted to Normal even when Hard had been
selected elsewhere.

The score trace uses `GetPlayerScore(player, PLAYER_SCORE_UNIT_TOTAL)`, not a
reimplementation of the retail formula. It also records current food, the
native food peak, Units Produced and a diagnostic Hero Total. The final score
screen is still needed for fields that the script cannot query, such as the
displayed Heroes Used count. The native `PLAYER_SCORE_HERO_TOTAL` did not match
the visible Heroes Score in this map and must not be treated as that displayed
column.

## Prerequisites and safety

- Use the intended TFT retail installation and record its exact version.
- `build/bin/mpqtool` reads and extracts archive members. It is already present
  in the investigation workspace; if absent, follow `docs/diagnostic-tools.md`
  to build it before beginning.
- `smpq` from StormLib repacks the disposable map copy. The tested workflow used
  `smpq` 1.6 / StormLib 9.30.
- Wine can launch the retail executable. Xvfb can provide a headless display,
  but **Xvfb does not provide interaction by itself**. Use a real desktop or
  send input to the X server with a tool such as `xdotool`; observe screenshots
  to confirm every menu transition.
- A local test map whose `war3map.j` is changed is unsigned. Load it as a local
  map with `-loadfile`; do not replace or select the signed campaign member in
  its original archive.

The game writes `PreloadGen` output under the active Wine user's Documents
folder, usually
`$WINEPREFIX/drive_c/users/<user>/Documents/Warcraft III/CustomMapData/`.
Use a dedicated Wine prefix for repeatable runs, and locate the output instead
of assuming the account name or prefix path:

```sh
find "${WINEPREFIX:-$HOME/.wine}/drive_c/users" \
  -path '*/Documents/Warcraft III/CustomMapData/scoretrace-normal.txt' -print
```

## Extract an untouched control

Start from the campaign member in `War3x.mpq`, not a previously repacked
variant. Keep the original and all work products separate:

```sh
export OPENREALM_ROOT="$(pwd)"
export TRACE="$OPENREALM_ROOT/build/retail-score-test"
mkdir -p "$TRACE/original" "$TRACE/work"

build/bin/mpqtool -mpq "$OPENREALM_ROOT/data/Warcraft III/War3x.mpq" \
  cat 'Maps/FrozenThrone/Campaign/UndeadX07c.w3x' \
  > "$TRACE/original/UndeadX07c.w3x"
build/bin/mpqtool -mpq "$TRACE/original/UndeadX07c.w3x" ls \
  > "$TRACE/original/members.txt"
build/bin/mpqtool -mpq "$TRACE/original/UndeadX07c.w3x" cat war3map.j \
  > "$TRACE/original/war3map.j"
build/bin/mpqtool -mpq "$TRACE/original/UndeadX07c.w3x" cat war3map.w3i \
  > "$TRACE/original/war3map.w3i"
build/bin/mpqtool -mpq "$TRACE/original/UndeadX07c.w3x" cat war3map.w3u \
  > "$TRACE/original/war3map.w3u"
```

The campaign map is itself an MPQ nested inside `War3x.mpq`; `mpqtool cat`
extracts that member, and the second `mpqtool` invocation opens the extracted
map as an archive. `War3xLocal.mpq` also contained this map in the investigated
install. Its listed members were compared with the `War3x.mpq` copy and were
byte-identical. If using a different install, perform that comparison again;
do not assume which archive wins based only on the map name.

Open the untouched extracted map in retail before instrumenting or repacking
it. If the control does not load, stop: that is an installation, launch or
archive issue, not a JASS instrumentation failure.

## Instrument the map passively

The trace must observe the retail counters without changing unit creation,
removal, ownership or score state. In the prepared `normal-trace` map, the
instrumentation is in `build/retail-score-test/variants/normal-trace/war3map.j`.
For a fresh copy of the original script, recreate these pieces:

1. Add `ScoreTraceLog`, `ScoreTraceDifficulty`, `ScoreTraceSamplePlayer` and
   `ScoreTraceSample`. `ScoreTraceLog` sends each line to both `BJDebugMsg` and
   `Preload`; the line includes the actual `GetGameDifficulty()` value.
2. In `main`, call `PreloadGenClear()` and `PreloadGenStart()` before the map
   initialization triggers. After `RunInitializationTriggers()`, sample
   `map_init` and start a one-shot eight-second timer for `map_plus_8s`.
3. In the intro skip/cleanup action, sample `intro_cleanup`, then start a
   separate one-shot eight-second timer. Its callback samples
   `intro_cleanup_plus_8s` and calls
   `PreloadGenEnd("scoretrace-normal.txt")`. Calling `PreloadGenEnd` is
   essential: without it the buffered trace file is not finalized.
4. To attribute totals to individual units, wrap the Player 1 initialization
   `CreateUnit` calls with a helper that creates the same unit and immediately
   logs its owner, type rawcode, current food, food peak, Units Produced and
   Unit Total. The prepared trace logs all 50 creations.
5. For removal experiments, log the selected unit and native counters just
   before and after each existing random removal. Do not choose a second random
   unit for diagnostics: that would change the random stream and the experiment.

The current prepared script also samples Arthas and Anubarak cache/owner state.
Those hero logs are useful for the separate Heroes Used investigation; they are
not required to measure unit costs. Preserve the campaign's initialization and
trigger ordering when adding instrumentation.

## Repack safely and verify the map

For a script-only trace map, copy the extracted control and replace only the
archived `war3map.j` member. The staged basename must exactly match the member
name; passing a differently named file adds a new member instead of replacing
the old script.

```sh
variant="$TRACE/work/UndeadX07c-retail-score-trace.w3x"
stage="$TRACE/work/stage-trace"
mkdir -p "$stage"
cp "$TRACE/original/UndeadX07c.w3x" "$variant"
cp "$TRACE/work/war3map.j" "$stage/war3map.j"
(cd "$stage" && smpq -a -f "$variant" war3map.j)
smpq -i "$variant"
build/bin/mpqtool -mpq "$variant" ls > "$TRACE/work/trace-members.txt"
build/bin/mpqtool -mpq "$variant" cat war3map.j > "$TRACE/work/check-war3map.j"
cmp "$TRACE/work/war3map.j" "$TRACE/work/check-war3map.j"
```

Confirm the member count matches the original and that only the intended member
differs. For object-cost variants, repeat this procedure with a fresh copy of
the same instrumented base and replace only `war3map.w3u`; if a variant also
changes the test unit type in JASS, document and verify both intended member
changes. Run `smpq -i` and extract the changed member back out before launching.
Never reuse a prior variant as the next variant's base: paired controls must
differ only in the cost field under study.

The `smpq -a -f` replacement preserves the map wrapper. Do not full-repack a
map unless necessary; previous attempts using different wrapper/member
handling produced maps that were not reliable retail inputs. The longer,
validated end-to-end wrapper-preserving procedure is documented in
[`retail-camera-tracing.md`](retail-camera-tracing.md#exact-extraction-and-repacking).

## Run retail and collect evidence

Use the actual retail executable path. For the investigated 1.29.2 build, the
direct local-map launch form is:

```sh
wine "$WAR3_EXE" -window -graphicsapi OpenGL2 \
  -loadfile "$(winepath -w "$variant")"
```

Do not add `-launch` to this tested `-loadfile` flow. Use a display with input;
under Xvfb, start a persistent X server and send keyboard/mouse events to that
same `DISPLAY`. A process that starts successfully but receives no input will
not reach intro cleanup or the score page. Keep the retail process and display
alive until the score trace has been written and the score page has been
captured.

Run sequence:

1. Load the **instrumented local map**.
2. Skip the intro cinematic using the map's normal skip action.
3. Wait at least eight seconds after skip/intro cleanup. Confirm the trace has
   `label=intro_cleanup_plus_8s` and the expected `difficulty=` value.
4. Open **End Game → Quit Mission** to reach the retail score screen.
5. Capture the Overview and Units pages (and Heroes when investigating heroes)
   as screenshots. Capture a raw X11 image too if later image conversion or
   pixel review is needed.
6. Copy `scoretrace-normal.txt` from Wine's `CustomMapData` directory into the
   variant's evidence directory. Keep the exact map beside the trace and
   screenshot.

For example, under a persistent X display, identify the game window and
capture the screen after activating the appropriate page:

```sh
DISPLAY=:91 xdotool search --onlyvisible --name 'Warcraft III'
DISPLAY=:91 xwd -root -silent -out "$TRACE/work/score-screen-units.xwd"
```

The title match can vary by Wine/retail build, and `xwd -root` captures the
whole virtual desktop. Inspect the capture to verify that the game is actually
on the score screen and the intended page is selected. Do not infer a value
from a low-resolution or partially clipped screenshot when the native trace
contains it.

One simple way to make that display available to a second terminal is:

```sh
Xvfb :91 -screen 0 1280x960x24 > "$TRACE/work/xvfb.log" 2>&1 &
XVFB_PID=$!
export DISPLAY=:91
wine "$WAR3_EXE" -window -graphicsapi OpenGL2 \
  -loadfile "$(winepath -w "$variant")"
```

Use an unused display number and stop the server after the run. In another
terminal, set `DISPLAY=:91` for `xdotool` and `xwd`. A screenshot is also a
useful way to confirm the intro was skipped, the right page opened and input
reached the game; do not rely on sleeps alone to establish those transitions.
For example, send the skip input and open the game menu, then inspect the
screen before choosing the next menu item:

```sh
DISPLAY=:91 xdotool key Escape
DISPLAY=:91 xdotool key F10
DISPLAY=:91 xwd -root -silent -out "$TRACE/work/menu-check.xwd"
```

Use the visible menu and the target build's input behavior to choose Quit
Mission; do not reuse guessed screen coordinates from a different resolution.

## Read and compare the trace

`PreloadGen` output is JASS source, not plain log text. Filter the embedded
payload lines, retaining `Preload(...)` context only as needed:

```sh
rg 'SCORETRACE' "$TRACE/work/scoretrace-normal.txt"
```

Use `intro_cleanup_plus_8s` for apples-to-apples comparison with the prepared
cost experiments. Verify player IDs and map state too. A valid Normal sample
has `difficulty=NORMAL`; player 1 is Faceless Ones in this map. The trace's
`unitTotal` should agree with the Units page. If the difficulty, sample label,
Units Produced count, or expected player identity differs, discard that run
instead of averaging it with the control.

When reading the cost experiments, compare every changed-cost run with its
matching explicit same-cost control. Explicitly writing a cost equal to the
root `Units\UnitBalance.slk` value changed the baseline in some retail cases;
the increase from the matched control is the valid marginal comparison. In
particular, the `nubk +10` screenshot was initially misread as 12,495; the
captured trace says **12,945**. Prefer the trace over visual digit estimates.

The object-editor field is **Gold Cost** (`ugol` in `war3map.w3u`); lumber is
`ulum`. Each cost variant starts from the same instrumented Normal map. The
gold series used these exact explicit values:

| Edited object | Instance count | Same-cost control | Changed values |
|---|---:|---:|---|
| `nubw` original unit | 5 | 255 | 265, 305, 355, 455 |
| `nubk` original unit | 12 | 150 | 160, 200, 250, 350 |
| `n002` custom unit based on `nubk` | 15 | 150 | 250 |
| `hfoo` Footman control | 1 substituted instance | 135 | 235 |
| custom `nw01` based on `nubw` | 1 substituted instance | 255 | 355 |

These are explicit same-cost-to-root controls, not no-op edits relative to the
retail `Custom_V1` row. Their absolute totals can shift when the field is first
written. Calculate the marginal result only as `changed total - paired control
total`. For type substitutions and the one-instance test, keep the paired maps
identical except for the intended JASS rawcode/object edit. After each edit,
extract the output `war3map.w3u` back out and check the target unit/field before
starting retail.

The captured baseline and cost-series evidence is summarized in
[`build/retail-score-test/priority-experiments/README.md`](../../../build/retail-score-test/priority-experiments/README.md).
That file also lists the specific trace/screenshot artifact for each variant.

## Reproduce the UnitBalance comparison

Extract both balance variants from the actual archive rather than relying on
spreadsheet values copied into notes:

```sh
build/bin/mpqtool -mpq 'data/Warcraft III/War3x.mpq' \
  cat 'Units/UnitBalance.slk' > "$TRACE/work/UnitBalance-root.slk"
build/bin/mpqtool -mpq 'data/Warcraft III/War3x.mpq' \
  cat 'Custom_V1/Units/UnitBalance.slk' > "$TRACE/work/UnitBalance-Custom_V1.slk"
```

In the SLK header, X8 is `goldcost`, X9 is `lumbercost`, and X13 is `fused`.
This small read-only parser prints those columns by rawcode:

```sh
python3 - "$TRACE/work/UnitBalance-root.slk" \
  "$TRACE/work/UnitBalance-Custom_V1.slk" <<'PY'
import re
import sys

wanted = {'nubw', 'nubk', 'nfot', 'nfod', 'nubr', 'nfor'}
for path in sys.argv[1:]:
    cells = {}
    y = None
    for line in open(path, encoding='latin1'):
        match = re.match(r'C;X(\d+)(?:;Y(\d+))?;K(.*)', line.strip())
        if not match:
            continue
        x, new_y, value = match.groups()
        if new_y is not None:
            y = int(new_y)
        if y is not None:
            cells.setdefault(y, {})[int(x)] = value
    print(path)
    for row in cells.values():
        rawcode = row.get(1, '').strip('"')
        if rawcode in wanted:
            print(rawcode, 'gold=', row.get(8), 'lumber=', row.get(9),
                  'food=', row.get(13))
PY
```

For this investigation, `Custom_V1` values were `nubw=205+30`, `nubk=120+10`,
`nfot=385+120`, and `nfod=595+200`. Those gold values plus lumber match the
retail per-unit contributions exactly. The root table in `War3.mpq` and
`War3x.mpq` was byte-identical in the inspected install. The installed data
directory had no `War3Patch.mpq`; record that absence rather than assuming a
patch archive was inspected.

To inspect the map's data-set marker without relying on an editor display, run
this parser on the extracted `war3map.w3i`:

```sh
python3 - "$TRACE/original/war3map.w3i" <<'PY'
import struct
import sys

data = open(sys.argv[1], 'rb').read()
offset = 0

def u32():
    global offset
    value = struct.unpack_from('<I', data, offset)[0]
    offset += 4
    return value

def cstring():
    global offset
    end = data.index(b'\0', offset)
    value = data[offset:end].decode('latin1')
    offset = end + 1
    return value

version = u32()
u32()  # number of saves
u32()  # editor version
if version >= 28:
    offset += 16  # game version major/minor/patch/build
for _ in range(4):
    cstring()  # map name, author, description, recommended players
offset += 48  # camera bounds and four complements
offset += 8   # playable width and height
flags = u32()
offset += 1   # main ground type
u32()         # campaign background
if version >= 25:
    cstring()  # loading screen model
for _ in range(3):
    cstring()  # loading text, title and subtitle
game_data_set = u32() if version >= 25 else 0
print(f'format={version} flags=0x{flags:08X} '
      f'melee={bool(flags & 0x4)} gameDataSet=0x{game_data_set:08X}')
PY
```

The inspected map prints `format=25 flags=0x0000DE61 melee=False
gameDataSet=0xFFFFFFFF`. That last value must be interpreted as an unset/default
marker for this map: the map is not marked melee, the retail score values match
`Custom_V1`, and the OpenRealm log shows the incorrect `Melee_V1` lookup. The
code path to audit is `G_MapGameDataSet()` in
`games/warcraft-3/game/g_metadata.c`, followed by
`G_ReadGameDataFile()` in `games/warcraft-3/game/g_main.c`. The latter tries
the chosen versioned prefix and then root; the regular filesystem lookup checks
the map-priority archive before the mounted game archives. Thus a logged
`Melee_V1` miss is evidence of the selected prefix, not proof that the MPQ
search order is wrong.

The map W3I parser reads the `gameDataSet` field for format 25 and later. In
this map the field is `0xFFFFFFFF`, and `flags=0x0000DE61` does not set the
`melee_map` bit (`0x4`). The latest OpenRealm log requested
`Melee_V1\\Units\\UnitBalance.slk`, logged that it was missing, then fell back
to root data. When repeating an OpenRealm comparison, preserve the complete
startup log and search it with:

```sh
rg -n 'Added archive|overlay missing .*UnitBalance' openwarcraft3.log
```

That logged requested path is the decisive check for data-set selection; the
presence of `Custom_V1` in the archive alone does not prove the map loader used
it.

To retain a matching OpenRealm startup log (when comparing the same map state),
launch the TFT map with result diagnostics enabled and capture both output
streams:

```sh
cd "$OPENREALM_ROOT"
build/bin/openwarcraft3 -data "$OPENREALM_ROOT/data/Warcraft III" -tft \
  +map 'Maps\\FrozenThrone\\Campaign\\UndeadX07c.w3x' \
  +set wc3_game_result_debug 1 2>&1 | tee "$TRACE/openrealm.log"
rg -n 'Added archive|overlay missing .*UnitBalance' "$TRACE/openrealm.log"
```

Check that the startup includes the TFT archive family and then inspect which
prefix was attempted. If it says `Melee_V1`, the typed-sheet loader falls back
to root when that sheet is absent; do not attribute that result to `War3Patch`
or general MPQ load order until the requested prefix is correct.

## Common failure modes

- **The run silently used Normal.** Direct `-loadfile` did not honor the
  intended campaign difficulty in the observed run. Trust the instrumented
  `GetGameDifficulty()` value, not the selection made before launching.
- **No trace file appears.** Confirm the skip callback ran, waited the full
  eight seconds and called `PreloadGenEnd`; then search the active Wine
  prefix's `CustomMapData` directory.
- **Retail loads the original campaign instead of the test map.** Use the
  absolute path converted by `winepath -w`, and verify the map title or
  instrumentation messages. Do not select the signed campaign entry.
- **The modified script did not replace the old one.** Stage the file with the
  exact archived name `war3map.j`, then extract it from the output map and
  `cmp` it with the staged copy.
- **A screenshot appears to disagree with the trace.** Check the right player,
  page and visible digits; use the native `unitTotal` and preserve the image as
  supporting evidence rather than overriding the trace.
- **Runs disagree in their random removals.** Log each selected rawcode and
  region. Do not add diagnostic random calls or compare runs with different
  initial conditions as if they were paired.
- **A balance value looks wrong.** Record archive path and data-set prefix.
  `Melee_V1` can be missing a sheet and silently resolve to a root table after
  the warning; the retail and root tables are not interchangeable here.

## Evidence limits

The controlled cost experiments establish retail's measured marginal score
response for the tested rawcodes and map state. Matching those totals to
`Custom_V1` establishes the best-supported data source for this discrepancy;
it does not reverse-engineer every retail score rule. The procedure should be
repeated with the target edition, exact map, intended difficulty and matched
controls before generalizing to another campaign or Warcraft III version.
