# Retail Warcraft III camera tracing

For movement experiments using this copied-map workflow, see
[retail pathfinding](retail-pathfinding.md#cinematic-experiments). Its Ghidra and
Frida addresses target 1.27.1.7085, not the 1.29.2 camera reference used here.

This documents the repeatable retail-reference workflow used for the
`Human02Interlude.w3m` opening cinematic. The reference was Warcraft III ROC
1.29.2 running under Wine. The original campaign map was never modified.

## Workflow

1. Extract a copy of the map into `build/retail-camera-trace/`.
2. Edit only the copied `war3map.j`.
3. Add passive JASS snapshots around important cinematic events.
4. Preserve the extracted MPQ and replace only its `war3map.j` member.
5. Launch that copied map directly in retail.
6. Copy `CAMTRACE` messages from the game and parse them with
   `tools/retail_camera_trace.py`.

The generated working files are described in
`build/retail-camera-trace/README.md`. They are build artifacts, not source
assets.

## Exact extraction and repacking

Build the archive tool first, then use a workspace-local root variable rather
than hard-coding an installation path:

```sh
make mpqtool
export OPENREALM_ROOT=/path/to/open-realm
trace="$OPENREALM_ROOT/build/retail-camera-trace"
mkdir -p "$trace/Human02Interlude-original" "$trace/Human02Interlude-instrumented"
build/bin/mpqtool -mpq "$OPENREALM_ROOT/data/Warcraft III/War3Local.mpq" \
  cat 'Maps/Campaign/Human02Interlude.w3m' > "$trace/Human02Interlude-original/Human02Interlude.w3m"
build/bin/mpqtool -mpq "$trace/Human02Interlude-original/Human02Interlude.w3m" \
  cat 'war3map.j' > "$trace/Human02Interlude-original/war3map.j"
cp "$trace/Human02Interlude-original/Human02Interlude.w3m" \
   "$trace/Human02Interlude-instrumented/Human02Interlude.w3m"
cp "$trace/Human02Interlude-original/war3map.j" \
   "$trace/Human02Interlude-instrumented/war3map.j"
```

Campaign maps in this installation are in `War3Local.mpq`, not `War3.mpq`.
Extract the untouched control first and verify it in retail before editing or
repacking anything:

```sh
control="$trace/retail-original/Human02Interlude.w3m"
mkdir -p "$(dirname "$control")"
build/bin/mpqtool -mpq "$OPENREALM_ROOT/data/Warcraft III/War3Local.mpq" \
  cat 'Maps/Campaign/Human02Interlude.w3m' > "$control"
build/bin/mpqtool -mpq "$control" ls
```

For ROC 1.29, use `-loadfile` without `-launch`; this build otherwise returned
to the main menu. Use `-window -graphicsapi OpenGL2` under Wine when the
default renderer produces a black screen:

```sh
wine "$WAR3_EXE" -window -graphicsapi OpenGL2 \
  -loadfile "Z:$control"
```

Keep the Linux path and Wine path on one physical shell line. If the untouched
extracted control does not load, stop debugging JASS or repacking; the
installation, Wine launch, or map path is the problem. The successful control
run differed from the earlier `Human02Interlude-original` build artifact,
proving that artifact was not the untouched retail source.

The confirmed working method preserves the original MPQ layout and replaces
only `war3map.j` with the same archived name:

```sh
map="$trace/Human02Interlude-instrumented/Human02Interlude-smpq.w3m"
output="$trace/Human02Interlude-instrumented/Human02Interlude-smpq-event-replaced.w3m"
script="$trace/Human02Interlude-debug/event-war3map.j"
stage="$(mktemp -d)"
cp "$map" "$output"
cp "$script" "$stage/war3map.j"
(cd "$stage" && smpq -a -f "$output" war3map.j)
smpq -i "$output"
```

The staged basename is important. Passing a file named `event-war3map.j`
directly adds a new archive member instead of replacing `war3map.j`, producing
19 files. The successful replacement archive has 18 files and one
`war3map.j`. Keep the original `Human02Interlude-smpq.w3m` as a known-good
control and never modify it in place.

An end-to-end rebuild also works when it starts from the exact map extracted
from the retail `War3Local.mpq`. Use a fresh staging directory, extract every
map member, install the instrumented script under the exact `war3map.j` name,
create an MPQ version 1 payload, and restore the original wrapper and trailer:

```sh
work="$(mktemp -d)"
original="$work/Human02Interlude-original.w3m"
files="$work/map-files"
payload="$work/map-payload.mpq"
output="$trace/Human02Interlude-end-to-end-event.w3m"
instrumented_script="$trace/Human02Interlude-instrumented/war3map.j"
mkdir -p "$files"
build/bin/mpqtool -mpq "$OPENREALM_ROOT/data/Warcraft III/War3Local.mpq" \
  cat 'Maps/Campaign/Human02Interlude.w3m' > "$original"
(cd "$files" && smpq -n -x "$original")
cp "$instrumented_script" "$files/war3map.j"
(cd "$files" && smpq -c -M 1 "$payload" *)
dd if="$original" of="$work/map-header.bin" bs=512 count=1 status=none
tail -c 260 "$original" > "$work/map-footer.bin"
cat "$work/map-header.bin" "$payload" "$work/map-footer.bin" > "$output"
smpq -i "$output"
```

This exact procedure produced a retail-loadable 18-file archive from the
original campaign data. The resulting map retained MPQ version 1, sector
compression, the original wrapper/trailer, and the instrumented `war3map.j`.
The earlier full-repack procedure remains useful for comparison, but variants
made from derived payloads or with different file/member handling were not
reliable:

```sh
command -v smpq                  # provided by StormLib; required for repacking
map="$trace/Human02Interlude-instrumented/Human02Interlude.w3m"
files="$trace/Human02Interlude-instrumented/map-files"
mkdir -p "$trace/Human02Interlude-instrumented/repack"
(cd "$files" && smpq -c -M 1 \
  "$trace/Human02Interlude-instrumented/repack/map-payload.mpq" *)
dd if="$map" of="$trace/Human02Interlude-instrumented/repack/map-header.bin" \
  bs=512 count=1 status=none
tail -c 260 "$map" > "$trace/Human02Interlude-instrumented/repack/map-footer.bin"
cat "$trace/Human02Interlude-instrumented/repack/map-header.bin" \
    "$trace/Human02Interlude-instrumented/repack/map-payload.mpq" \
    "$trace/Human02Interlude-instrumented/repack/map-footer.bin" \
  > "$trace/Human02Interlude-instrumented/Human02Interlude-CAMTRACE-legacy.w3m"
build/bin/mpqtool -mpq \
  "$trace/Human02Interlude-instrumented/Human02Interlude-CAMTRACE-legacy.w3m" ls
```

For this map the legacy wrapper is 512 bytes and the footer is 260 bytes. The
repacker used here is the apt-installed `/usr/bin/smpq` 1.6 using StormLib
9.30. Keep the MPQ-extracted control map and its wrapper as comparison inputs;
do not repack the campaign archive in place.

The JASS edits belong at these stable generated-script locations:

- add the trace globals and helper functions after the existing `globals` block;
- call `CameraTraceStart()` at the beginning of
  `Trig_CinematicStart_Actions`;
- call snapshots immediately before and after the existing
  `CameraSetupApplyForPlayer` calls in `Trig_Scene1_Actions`;
- call `CameraTraceStop()` in both the skip action and the normal end of Scene 1.

The generated `war3map-camera-trace.patch` records the exact Human02 edit and
can be applied with `patch -p0` when the generated build directory is present.
If it is absent, use the locations above and the helper contract below; do not
edit the source campaign archive.

## JASS instrumentation

The minimal helper body is:

```jass
globals
    timer udg_CameraTraceTimer = null
    integer udg_CameraTraceSample = 0
    real udg_CameraTraceTime = 0.0
    boolean udg_CameraTraceEnabled = false
endglobals

function CameraTraceSnapshot takes string label returns nothing
    if not udg_CameraTraceEnabled then
        return
    endif
    set udg_CameraTraceSample = udg_CameraTraceSample + 1
    set udg_CameraTraceTime = TimerGetElapsed(udg_CameraTraceTimer)
    call BJDebugMsg("CAMTRACE n=" + I2S(udg_CameraTraceSample) + " t=" + R2S(udg_CameraTraceTime) + " label=" + label +
        " tx=" + R2S(GetCameraTargetPositionX()) + " ty=" + R2S(GetCameraTargetPositionY()) + " tz=" + R2S(GetCameraTargetPositionZ()) +
        " ex=" + R2S(GetCameraEyePositionX()) + " ey=" + R2S(GetCameraEyePositionY()) + " ez=" + R2S(GetCameraEyePositionZ()) +
        " dist=" + R2S(GetCameraField(CAMERA_FIELD_TARGET_DISTANCE)) + " aoa=" + R2S(GetCameraField(CAMERA_FIELD_ANGLE_OF_ATTACK)) +
        " rot=" + R2S(GetCameraField(CAMERA_FIELD_ROTATION)) + " fov=" + R2S(GetCameraField(CAMERA_FIELD_FIELD_OF_VIEW)) +
        " roll=" + R2S(GetCameraField(CAMERA_FIELD_ROLL)) + " zoff=" + R2S(GetCameraField(CAMERA_FIELD_ZOFFSET)) +
        " farz=" + R2S(GetCameraField(CAMERA_FIELD_FARZ)))
endfunction

function CameraTraceStart takes nothing returns nothing
    set udg_CameraTraceSample = 0
    set udg_CameraTraceTime = 0.0
    set udg_CameraTraceEnabled = true
    set udg_CameraTraceTimer = CreateTimer()
    call TimerStart(udg_CameraTraceTimer, 3600.00, false, null)
    call CameraTraceSnapshot("start")
endfunction

function CameraTraceStop takes nothing returns nothing
    if not udg_CameraTraceEnabled then
        return
    endif
    call CameraTraceSnapshot("stop")
    set udg_CameraTraceEnabled = false
    call PauseTimer(udg_CameraTraceTimer)
endfunction
```

The timer is used only as an elapsed-time clock; it does not invoke a periodic
callback. Every snapshot increments the sample number and records the elapsed
time at the named event.

The copied script added three helpers:

- `CameraTraceSnapshot(label)` prints one machine-readable line containing the
  target XYZ, eye XYZ, distance, angle of attack, rotation, FOV, roll, Z offset,
  and far Z.
- `CameraTraceStart()` creates an elapsed-time clock and samples the local camera.
- `CameraTraceStop()` records the final state and pauses the clock.

The trace records the cinematic start, the state before and after `DummyStart`,
the states immediately before and after the `TowerHigh` and `TowerLow`
applications, and the Scene 1 end/skip paths. It does not change camera calls,
waits, transmissions, units, terrain, fog, game speed, or trigger order.

`BJDebugMsg` was the first reliable output channel. The retail build also
validated the `PreloadGen*` file-generation natives. The file-backed trace uses
the following additional calls:

```jass
call PreloadGenClear()
call PreloadGenStart()
call Preload(msg)
call PreloadGenEnd("camtrace.txt")
```

`PreloadGenStart()` is called when tracing starts, each snapshot passes its
completed `CAMTRACE` string to `Preload(msg)`, and `PreloadGenEnd()` is called
when tracing stops. Retail writes the result below the Wine Documents tree at
`Documents/Warcraft III/CustomMapData/`. The filename is the argument passed
to `PreloadGenEnd`: the full probe uses `camtrace.txt`, while the transition
probe map uses `camtrace-transition.txt`. The exact Linux path depends on the
Wine prefix. Locate either output with:

```sh
find "$HOME/.wine/drive_c/users" -type f \
  \( -name 'camtrace.txt' -o -name 'camtrace-transition.txt' \) -print
```

The output is a generated JASS preload script, not raw text. Warcraft inserts
its own asset preload calls and the map path between the camera samples, so
consumers must retain only lines containing `CAMTRACE`. The validated output
contained the start sample, `DummyStart`, `TowerLow`, `TowerHigh`, and final
stop snapshots with the same values shown by `BJDebugMsg`. The file is written
when the cinematic reaches the stop path, and a later run overwrites the same
filename.

The values are raw retail getter values: angular fields and FOV appeared as
radians in the trace, while distance and positions remained world units.

## Retail fade/filter investigation

The opening scene's first two seconds are intentionally hidden by the map's
cinematic filter. The script first applies `DummyStart` and `TowerLow`, waits
for approximately two seconds, then applies `TowerHigh` and begins the
ten-second `TowerHigh` to `TowerLow` transition. A one-second
`CinematicFilterGenericBJ` call then reveals the tower view. A missing tower
during the initial black interval is therefore not evidence of a camera or
model-rendering failure.

The retail 1.29.2 executable contains these cine-filter native names:

```text
SetCineFilterTexture
SetCineFilterBlendMode
SetCineFilterTexMapFlags
SetCineFilterStartUV
SetCineFilterEndUV
SetCineFilterStartColor
SetCineFilterEndColor
SetCineFilterDuration
DisplayCineFilter
IsCineFilterDisplayed
```

Static inspection also finds RTTI for `CCinematicFilter`, `CFadeTimer`, and
`CSimpleFadeTimer`. Blizzard symbols/PDB data are not present in the
installation, so these are the names and internal class identities visible
from the binary; native implementation addresses still require a debugger
breakpoint or disassembly mapping.

For the tested ROC 1.29.2 `Warcraft III.exe` (image base `0x00400000`), the
JASS native registration table resolves to these wrapper entry points. These
addresses are specific to this executable build and must not be copied to a
different patch:

| Native | Virtual address | RVA |
| --- | ---: | ---: |
| `SetCineFilterTexture` | `0x004A7800` | `0x000A7800` |
| `SetCineFilterBlendMode` | `0x004A7460` | `0x000A7460` |
| `SetCineFilterTexMapFlags` | `0x004A77B0` | `0x000A77B0` |
| `SetCineFilterStartUV` | `0x004A7740` | `0x000A7740` |
| `SetCineFilterEndUV` | `0x004A75D0` | `0x000A75D0` |
| `SetCineFilterStartColor` | `0x004A7640` | `0x000A7640` |
| `SetCineFilterEndColor` | `0x004A74D0` | `0x000A74D0` |
| `SetCineFilterDuration` | `0x004A74A0` | `0x000A74A0` |
| `DisplayCineFilter` | `0x0048F9B0` | `0x0008F9B0` |
| `IsCineFilterDisplayed` | `0x00498E70` | `0x00098E70` |

The registration block is at `0x004A05AF` (with a duplicate native-table
initializer at `0x00CBB860`). The wrapper disassembly shows that
`SetCineFilterDuration` stores the supplied duration in the filter state,
`DisplayCineFilter` updates the filter's displayed-state field, and
`IsCineFilterDisplayed` reads that field. `SetCineFilterStartColor` and
`SetCineFilterEndColor` pack the four byte color arguments before passing them
to the filter state. The exact timer update and screen-compositing methods are
inside the stripped `CCinematicFilter`/`CFadeTimer` implementation and still
need runtime breakpoints or further call-graph analysis.

The corresponding OpenRealm path is `api_cinefilter.h` to `G_Cinefade()` to
`playerState.cinefade` to `SCR_DrawLayout()`. Compare the overlay alpha at
`t=0`, `t=2.0`, and `t=2.1` through `t=3.1`; the camera may already be
positioned beneath a fully opaque filter while the scene is black.

### Runtime breakpoint result

The first live breakpoint run under Wine reached the retail filter natives and
printed this startup sequence before the campaign map began:

```text
RETAIL_FILTER start-color a=0,0,0,255
RETAIL_FILTER end-color a=0,0,0,255
RETAIL_FILTER duration=0.000000
RETAIL_FILTER display=1
```

This confirms that the black opening can be established independently of a
camera operation. It does not yet identify the compositor's per-frame alpha
update: that requires the map to remain running under the debugger and a
breakpoint/watchpoint on the internal `CFadeTimer` state. The debugger must be
launched with a stable X11 display and the same MPQ-packed map copy that is
known to load without returning to the menu. If Wine loses its display or the
map exits during debugger startup, `winedbg` can leave a GDB prompt with no
inferior; that run has no runtime evidence and should be discarded.

For native debugging, install the 32-bit Wine runtime. This installation uses
Wine's WoW64 mode; the 32-bit runtime is required, but this Wine build rejects
`WINEARCH=win32`, so use the existing/default WoW64 prefix rather than creating
a legacy 32-bit prefix:

```sh
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install wine32:i386 libwine:i386
wineboot -u
```

Verify the package installation with `dpkg-query -W wine32:i386
libwine:i386`. A test such as `WINEARCH=win32 wineboot -u` is expected to
fail with this WoW64 build and does not indicate that the required 32-bit
runtime is missing.

The available `winedbg` can launch the 32-bit executable through its GDB
proxy. This is sufficient for breakpoints after native registration targets
are identified, but the lack of Blizzard symbols means initial breakpoints use
raw addresses. Do not modify the retail executable or the original campaign
archive during this investigation.

## Retail launch

For the installed 1.29 executable, this was the working form:

```sh
export OPENREALM_ROOT=/path/to/open-realm
map="$OPENREALM_ROOT/build/retail-camera-trace/Human02Interlude-instrumented/Human02Interlude-CAMTRACE-legacy.w3m"
wine "$OPENREALM_ROOT/data/Warcraft III/Warcraft III.exe" \
  -window -graphicsapi OpenGL2 \
  -loadfile "$(winepath -w "$map")"
```

`Z:` is Wine's mapping of the Linux filesystem. Verify the Linux source first:

```sh
map="$OPENREALM_ROOT/build/retail-camera-trace/Human02Interlude-instrumented/Human02Interlude-CAMTRACE-legacy.w3m"
test -f "$map" && echo "Linux path exists" || echo "Missing"
winepath -w "$map"
```

Keep the Wine path on one shell line. A newline embedded in a quoted path was
passed through to `winepath` during the investigation and produced an invalid
Windows path. Shell parentheses also caused `wine cmd /c` syntax errors; a
simple `test -f` plus `winepath -w` check was less error-prone.

## Packing lessons

The 1.29 client was sensitive to the map package shape. The confirmed working
instrumented package was made by copying a working MPQ archive and replacing
only `war3map.j` in place. It retained MPQ version 1, 4096-byte sector
compression, 18 files, the original 512-byte Warcraft map wrapper, and the
original 260-byte trailer. The modified JASS then ran and produced camera
logs, proving that the script itself was valid and that this replacement
workflow was accepted by retail.

An untouched map extracted from `War3Local.mpq` also loaded successfully. The
exact end-to-end `smpq` rebuild above loaded successfully as well. Several
other independently rebuilt MPQs built from derived payloads loaded the
loading screen and then crashed or returned to the menu. A derived copy going
to the menu therefore does not prove that the retail installation or launch
command is wrong. Preserve the known-good archive and use either the exact
end-to-end procedure or the script-only replacement method.

Do not overwrite the campaign map in `data/Warcraft III`. Pack into a separate
file with a distinct name and preserve the original extracted files for binary
comparison. A repacked uninstrumented map is a useful control: if it crashes,
the packer/package is the problem, not JASS instrumentation.

## What did not work

- Launching an invalid or unsupported package without the compatible legacy
  wrapper led to the menu, an empty error dialog, or an access-violation report.
- Full MPQ reconstruction from a derived or incorrectly staged payload could
  produce maps that 1.29 rejected or crashed after the loading screen. The
  exact rebuild from a fresh extraction of `War3Local.mpq` is confirmed to
  work.
- Passing a renamed script directly to `smpq -a -f` added a second JASS member
  instead of replacing `war3map.j`; stage the file under the exact archive
  basename first.
- The original retail executable sometimes showed a black screen under Wine;
  `-window -graphicsapi OpenGL2` made the client usable for this test.
- Wine's NTLM warnings (`ntlm_auth`/winbind) were startup noise, not the cause
  of the map crash.
- `BJDebugMsg` alone did not create a trace file. It only displayed messages
  in the game UI, and the long camera line wrapped visually. `PreloadGen*` is
  the validated retail file-output route.
- Starting OpenRealm with a temporary data directory containing only selected
  archives did not load the map reliably. The complete Warcraft data layout and
  the expected map path are safer for OpenRealm runs.

## Reducing output noise

The default trace is event-only, so it avoids filling the debug message area
with repeated identical states. Use one of these modes depending on the
question being answered:

| Mode | Sampling | Use |
| --- | ---: | --- |
| Transition | snapshots immediately before, during, and after a camera call | destination/start-state bugs |
| Event-only | named events | settled camera values and trigger ordering |
| Fine | 0.05 s timer | short interpolation or angle-wrap investigations |

For a transition investigation, temporarily add a timer callback to the copied
script; do not change the cinematic's authored waits or camera durations. For
event-only tracing, keep `CameraTraceSnapshot()` and call it only at named
points:

```jass
call CameraTraceSnapshot("before-tower-high")
call CameraSetupApply(towerHigh, true, false)
call CameraTraceSnapshot("after-tower-high")
```

The sampler should remain optional and local-player-only. Do not add permanent
per-frame `BJDebugMsg` or `Preload` calls to production engine code. The
validated file-output route is intended for the copied retail instrumentation
map only.

## Parsing

For the on-screen output, save copied messages as `retail-camtrace.txt` and
run:

```sh
python3 tools/retail_camera_trace.py retail-camtrace.txt -o retail-camtrace.csv
```

The parser retains the raw fields and adds `dx`, `dy`, `dz`, horizontal
distance, and Euclidean eye-to-target distance. These derived values are
diagnostics only; they do not replace the retail camera fields.

For a generated preload file, first extract only the camera lines from the
wrapper, then use the same parser. Replace the input filename with whichever
file the map generated:

```sh
grep 'call Preload( "CAMTRACE ' camtrace-transition.txt \
  | sed -E 's/^.*Preload\( "([^"]+)" \).*$/\1/' \
  > retail-camtrace.txt
python3 tools/retail_camera_trace.py retail-camtrace.txt -o retail-camtrace.csv
```

## OpenRealm engine-side trace

OpenRealm can emit the same event-only format without modifying or repacking
the map. Enable the opt-in cvar before loading the original map:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' \
  +set vid_hidden 1 +set camera_trace 1 \
  +map 'Maps/Campaign/Human02Interlude.w3m' +com_frame_limit 300 \
  > openrealm-camtrace.log 2>&1
```

The trace is emitted by the `CameraSetupApply*` camera natives after they
assign runtime state. It is disabled by default. Mode `1` is event-only;
mode `2` additionally samples the realized camera at up to 20 Hz, subject to
the simulation frame cadence, which is useful for comparing interpolation.
Both modes report the same logical fields and reconstructed eye/target geometry
as the JASS getters. The engine trace does not depend on `BJDebugMsg` or the
instrumented retail map.

Use mode `2` for a bounded transition capture:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' \
  +set vid_hidden 1 +set camera_trace 2 \
  +map 'Maps/Campaign/Human02Interlude.w3m' +com_frame_limit 500 \
  > openrealm-camtrace-fine.log 2>&1
```

Mode `2` is intentionally opt-in because it writes one diagnostic line per
sample. Return to mode `1` for compact event-only traces.

## Full-cinematic event trace

The generated full-event probe records the entire Human02 cinematic without a
periodic timer. It snapshots before and after each of the ten camera setup
applications, around dialogue transmissions, at the Scene 1/Scene 2 boundary,
and at the final victory path. The map preserves the known-good 18-file MPQ
layout and writes `camtrace-full.txt` under the Wine Documents
`Warcraft III/CustomMapData` directory.

Launch the prepared probe with:

```sh
map="$PWD/build/retail-camera-trace/Human02Interlude-instrumented/Human02Interlude-CAMTRACE-full-events.w3m"
wine "$PWD/data/Warcraft III/Warcraft III.exe" \
  -window -graphicsapi OpenGL2 \
  -loadfile "$(winepath -w "$map")"
```

Extract the machine-readable lines after the cinematic reaches victory:

```sh
trace_file="$(find "$HOME/.wine/drive_c/users" -type f -name 'camtrace-full.txt' -print -quit)"
grep 'call Preload( "CAMTRACE ' "$trace_file" \
  | sed -E 's/^.*Preload\( "([^"]+)" \).*$/\1/' > retail-full-camtrace.txt
```

Run OpenRealm long enough to finish Human02, then stop it before the automatic
Human03 load if necessary:

```sh
build/bin/openwarcraft3 -data 'data/Warcraft III' \
  +set vid_hidden 1 +set camera_trace 1 \
  +map 'Maps/Campaign/Human02Interlude.w3m' +com_frame_limit 5000 \
  > openrealm-full-camtrace.log 2>&1
```

The comparator aligns retail `cinematic-camera-01-after` through
`cinematic-camera-10-after` with OpenRealm's ten camera-setup events:

```sh
python3 tools/compare_wc3_camera_trace.py \
  retail-full-camtrace.txt openrealm-full-camtrace.log
```

The OpenRealm log may contain the first camera events from the next campaign
map after Human02 victory. Ignore rows after the Human02 tenth camera event;
the comparator uses the first aligned camera sequence.

## Verified reference observations

For the Human02 opening shot, retail kept target X/Y at approximately
`(-4909.3, 2474.5)` and distance at `3348.6`. Target Z and Z offset changed
together, while `targetZ - zOffset` stayed approximately `454.106`. The first
and final low shot reported approximately:

```text
targetZ=831.507 eye=(-1656.662,1729.546,551.303)
aoa=0.084 rotation=2.916 fov=1.571 zoff=377.400 farz=4000.000
```

Near the tower, target Z, angle of attack, FOV, and Z offset interpolated while
distance remained fixed. These are retail measurements from this map/version,
not universal assumptions about every Warcraft camera setup.

## See also

- [WC3 Cinematic / Cutscene System](cinematics.md)
- [Client camera architecture](../../architecture/client.md)
- [Camera viewport and cinematic state](cinematics.md)
