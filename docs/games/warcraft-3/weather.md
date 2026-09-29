# Warcraft III Weather

## Contract

Warcraft III weather is a map/JASS-owned presentation effect.  The authoritative
visual definition is `TerrainArt\Weather.slk`; a weather rawcode such as `RAhr`
selects one SLK row.  It is not an MDX model entity and it does not change
pathfinding, fog-of-war simulation, combat, or unit state.

OpenRealm has three sources of weather instances:

1. `war3map.w3i` global `weatherID` -> one enabled effect covering
   `CM_GetWorldBounds()`;
2. `war3map.w3r` v5 region weather IDs -> one enabled effect per authored
   weather region;
3. JASS `AddWeatherEffect(rect, id)` -> one disabled handle which becomes visible
   after `EnableWeatherEffect(handle, true)` and is destroyed by
   `RemoveWeatherEffect(handle)`.

The explicit enable step matches normal Warcraft GUI/JASS usage: creating a
weather effect and turning it on are separate operations.

## Data Flow

```text
W3I global weather / W3R region weather / JASS weather native
                         |
                         v
games/warcraft-3/game/g_weather.c
  stable level weather registry
                         |
                         | generic reliable GameCommand payload
                         v
client/cl_parse.c -> refExport_t.GameCommand
                         |
                         v
games/warcraft-3/renderer/r_weather.c
                         |
                         +-> TerrainArt\\Weather.slk typed row
                         +-> map-scoped texDir\\texFile.blp texture
                         +-> shared cparticle_t pool
                         v
                    alpha pass
```

The shared client does not interpret `wc3_weather`; it forwards opaque game
commands through the selected renderer callback.  This follows the game-boundary
rule in `docs/architecture/server-selected-effects.md`.

## W3R Weather Fields

`games/warcraft-3/common/world_w3.c` reads the version-5 `war3map.w3r` layout.
Only bounds and non-zero weather rawcodes are retained because the region name,
ambient-sound string, editor ID, and colour are not needed by the weather
renderer.

A v5 disk record is:

```text
float left, bottom, right, top
cstring name
int region_id
fourcc weather_id
cstring ambient_sound
byte blue, green, red, alpha/reserved
```

The parser validates version 5 and bounds the allocation from the remaining
file size before reading map-controlled counts.  Format reference:
ChiefOfGxBxL/WC3MapSpecification `Regions/5.md`.

## Weather.slk

The renderer owns a typed `Weather.slk` schema because these rows are
presentation data, not gameplay unit metadata.  It first tries the map-scoped
`TerrainArt\Weather.slk`, then falls back to the base game file.  This preserves
map-import overrides without adding WC3 paths to shared renderer code.

`texFile` is the Warcraft texture basename rather than a filename with an extension; the renderer appends `.blp` after combining it with `texDir`.

The schema includes:

- `effectID`, `texDir`, `texFile`;
- `alphaMode`, `useFog`;
- `height`, `angx`, `angy`;
- `emrate`, `lifespan`, `particles`;
- `veloc`, `accel`, `var`;
- `texr`, `texc`;
- `head`, `tail`, `taillen`;
- `lati`, `long`, `midTime`;
- start/mid/end RGB, alpha, and scale;
- head/tail UV stage fields;
- `AmbientSound`, `version`.

Representative shipped rain (`RAhr`) uses `rainTail`, a negative velocity, a
short lifespan, and `tail=1`; do not hard-code those values.  Community extracts
of Blizzard's `Weather.slk` document the column names and shipped values; the
map's SLK remains authoritative at runtime.

## Particle Rendering

`r_weather.c` emits only inside the intersection of the weather rectangle and a
camera-local world window.  Large map-wide weather therefore does not allocate
particles across the entire map.  Emission accrues the authored `emrate` across
frame time, then applies the provisional 20 Hz interpretation described below.
The authored `particles` count caps live logical particles per weather effect.

Spawn height is terrain height plus the authored `height`.  `angx`/`angy` rotate
the base vertical direction, `veloc` supplies signed speed, `var` applies the
same symmetric per-particle speed variation used by WC3 ParticleEmitter2, and
`lati` spreads each particle inside a cone around that authored direction.
`accel` changes speed along the selected particle direction.  Weather uses
renderer-local xorshift state rather than gameplay RNG.

The shared particle representation has an optional generic world-space `tail`
vector.  A zero vector keeps the camera-facing billboard path.  A non-zero
vector renders a camera-facing quad from `position - tail` to `position`; WC3
weather sets that vector from `velocity * taillen`.  `head=1`, `tail=1` now
creates both primitives from the same spawned weather particle state instead of
discarding the authored head.

Start/mid/end RGB/alpha and scale reuse the shared particle interpolation.
Weather also supplies separate head/tail `hUV*` / `tUV*` atlas-frame curves,
interpolated across the same authored `midTime`; ordinary shared particles keep
the existing full-lifetime atlas animation unless they opt into that curve.

OpenRealm currently multiplies weather texture and particle color and uses
ordinary source-alpha blending; it parses `alphaMode` but does not map its
values. A retail trace of Prologue01's `RLhr` (`alphaMode=0`) confirms those
parts of the path and also reveals a retail alpha-test cutoff that OpenRealm
does not currently reproduce. See [Retail Rain Alpha](#retail-rain-alpha) for
the evidence, parity gap, and reproducible probe. Behavior for other
`alphaMode` values remains unverified.

## Retail Rain Alpha

### Confirmed Prologue01 path

The evidence chain starts in the retail map. `Prologue01.w3m` is in
`War3Local.mpq`; its `war3map.j` creates and enables `RLhr` over
`Starting_Area_Rain` near map initialization. The `War3.mpq`
`TerrainArt\\Weather.slk` row authors `alphaMode=0`, white RGB, and
`alphaStart=alphaMid=alphaEnd=150`. That row names
`ReplaceableTextures\\Weather\\rainTail.blp`, a 32x16 BLP1 image whose source
header declares an 8-bit alpha channel.

A read-only Frida trace of the retail OpenGL draw path observed the rain batch
with these values:

| State at the draw | Observed value | Meaning |
|---|---:|---|
| Vertex color | `(255, 255, 255, 150)` | White particle color, authored alpha 150/255 (about 0.588) |
| Texture environment | `GL_MODULATE` (`8448`) | Texture RGBA modulates the vertex RGBA |
| Texture | 32x16, `GL_TEXTURE_ALPHA_SIZE=4` | The bound rain-sized texture reports 4-bit internal alpha precision |
| Alpha test | enabled, `GL_GEQUAL` (`518`), ref `0.0156863` | Fragment alpha must be at least 4/255 to pass |
| Blend | enabled, `GL_SRC_ALPHA` (`770`), `GL_ONE_MINUS_SRC_ALPHA` (`771`) | Standard source-alpha compositing |
| Primitive batch | `GL_TRIANGLES`, 468 or 474 indices | Repeated weather draw observed during the cinematic |

For this mode-0 rain path, the effective fragment alpha is the modulated texture
alpha times the particle vertex alpha: `A = A_texture * (150/255)`. The alpha
test discards fragments where `A < 4/255`. Passing fragments use ordinary
source-alpha compositing: `C_out = C_src * A + C_dst * (1-A)`. The white vertex
RGB leaves the sampled texture RGB unchanged under modulation. The 4-bit
precision is the OpenGL texture object's reported internal alpha size; the BLP
source itself declares 8-bit alpha. The trace records the bound texture object
and its dimensions, not its asset path. Attribution to `rainTail` comes from
the controlled Prologue01 scene (`RLhr` is the active opening weather), the
row's asset path and dimensions, and the matching authored vertex alpha
observed in the draw; the probe does not independently recover a texture
filename.

This establishes the runtime behavior for Prologue01 `RLhr` (`alphaMode=0`). It
does not establish what `alphaMode=1` or other weather rows do. OpenRealm's
weather path already multiplies texture and vertex color and uses the same
source-alpha blend factors, but it does not apply the observed `4/255` alpha
test. In the implementation, [`R_WeatherSpawn`](../../../games/warcraft-3/renderer/r_weather.c)
selects `BLEND_MODE_BLEND`; the shared [particle shader](../../../renderer/r_particles.c)
discards only for its separate alpha-key mode. The missing `4/255` cutoff is a
confirmed parity gap. The effect of nonzero `alphaMode` values remains
unverified.

### Reproduce the trace

The controller refuses to attach unless the `--exe` file has SHA-256
`3f2ed0120d80578bf07e4423296dade1adfb959d59a2d20a7584224559570eed`; it then
attaches to the `Warcraft III.exe` process on the Frida endpoint. Run the game
from that same file. The setup needs the matching Frida 17.19.0 client/server
pair, Wine, and Xvfb. The trace used Frida's x86_64 Wine server with an ia32
game process. To recreate the client/server install, download
`frida-server-17.19.0-windows-x86_64.exe.xz` from the
[official 17.19.0 release](https://github.com/frida/frida/releases/tag/17.19.0),
then run:

```sh
FRIDA_TOOLS="$HOME/.local/share/open-realm/tools"
python3 -m venv "$FRIDA_TOOLS/frida-venv"
"$FRIDA_TOOLS/frida-venv/bin/python" -m pip install 'frida==17.19.0'
mkdir -p "$FRIDA_TOOLS/frida"
xz -dc frida-server-17.19.0-windows-x86_64.exe.xz > "$FRIDA_TOOLS/frida/server.exe"
```

Extract the original map without rebuilding or modifying its MPQ:

```sh
WC3DATA="$PWD/data/warcraft-3"
build/bin/mpqtool -mpq "$WC3DATA/War3Local.mpq" cat Maps/Campaign/Prologue01.w3m > /tmp/Prologue01.w3m
build/bin/mpqtool -mpq /tmp/Prologue01.w3m cat war3map.j | rg -n -C2 'Starting_Area_Rain|AddWeatherEffect'
sha256sum "$WC3DATA/Warcraft III.exe"
build/bin/mpqtool -mpq "$WC3DATA/War3.mpq" cat TerrainArt/Weather.slk | rg -n -A45 'Y1;K"effectID"|Y13;K"RLhr"'
build/bin/mpqtool -mpq "$WC3DATA/War3.mpq" imginfo ReplaceableTextures/Weather/rainTail.blp
```

The map script identifies the active opening effect; the SLK row supplies its
authored color/alpha and texture path; `imginfo` confirms the source BLP
dimensions and alpha bits. This separates authored source data from the
runtime GL state observed by the probe.

Use the matching Frida 17.19.0 client/server pair. The server path below is the
local tool installation used for this trace. If display `:97` is not already
running, start Xvfb in its own terminal and leave it running:

```sh
Xvfb :97 -screen 0 1280x720x24 -nolisten tcp
```

```sh
export WINEPREFIX="$HOME/.local/share/open-realm/wine-wc3"
export DISPLAY=:97
DISPLAY=:97 WAYLAND_DISPLAY= WINEDEBUG=-all \
  wine "$HOME/.local/share/open-realm/tools/frida/server.exe" --listen=127.0.0.1:27043
```

In another terminal on that display, launch the extracted map:

```sh
export WINEPREFIX="$HOME/.local/share/open-realm/wine-wc3"
export DISPLAY=:97
WC3DATA="$PWD/data/warcraft-3"
DISPLAY=:97 WAYLAND_DISPLAY= WINEDEBUG=-all wine "$WC3DATA/Warcraft III.exe" -window -graphicsapi OpenGL2 \
  -loadfile "$(winepath -w /tmp/Prologue01.w3m)"
```

Dismiss the chapter screen (send Escape to the focused game with
`xdotool key --clearmodifiers Escape` if needed) and wait until the opening
rain is visible. Attach for a bounded 10-second capture; the game remains
running after the probe detaches:

```sh
WC3DATA="$PWD/data/warcraft-3"
"$HOME/.local/share/open-realm/tools/frida-venv/bin/python" \
  tools/frida/trace_wc3_weather_alpha.py --exe "$WC3DATA/Warcraft III.exe" \
  --seconds 10 --output /tmp/wc3-weather-alpha.jsonl
```

The probe hooks `opengl32.dll`'s `glColorPointer` and `glDrawElements`, then
queries the current texture dimensions/alpha size, texture environment, alpha
test, and blend state at matching 32x16 rain-texture draws. A `rain-draw` JSONL
row should report `colorArray.sample` containing `[255,255,255,150]`,
`textureEnvMode=8448`, `blendSrc=770`, `blendDst=771`,
`alphaTestFunc=518`, and `alphaTestRef` near `0.0156863`. The script is
read-only and does not call game functions or change OpenGL state.

The GL enum values are recorded numerically so the output is machine-readable:
`8448=GL_MODULATE`, `770=GL_SRC_ALPHA`, `771=GL_ONE_MINUS_SRC_ALPHA`, and
`518=GL_GEQUAL`.

## Undead01 Rain Fidelity: Best Guess

Undead01's JASS in `Maps/Campaign/Undead01.w3m` (inside `War3Local.mpq`)
enables `RLlr` (Lordaeron light rain). The base `War3.mpq` row has `emrate=40`,
`lifespan=1.1`, `particles=880`, `alphaMode=0`, `alphaStart/Mid/End=150`,
`head=0`, and `tail=1`. This is separate from the map's `SetSkyModel` choice.

These commands inspect the installed source data without relying on an
extracted working-tree file:

```sh
build/bin/mpqtool -mpq "data/Warcraft III/War3Local.mpq" cat Maps/Campaign/Undead01.w3m > /tmp/Undead01.w3m
build/bin/mpqtool -mpq /tmp/Undead01.w3m cat war3map.j | rg -n "RLlr|SetSkyModel"
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" cat TerrainArt/Weather.slk | rg -n "RLlr"
```

Before this change, OpenRealm treated `emrate` as particles per second. At
steady state, `40 * 1.1` predicts about 44 live rain particles before any pool
limit. The row's `particles=880` is exactly twenty times that estimate. The
same 20x relationship appears in shipped rain rows such as `RAhr`
(`100 * 0.9 * 20 = 1800`).

**Best-guess density hypothesis, not confirmed retail behavior:** Warcraft may
apply `emrate` once per 50 ms weather update (20 Hz), making `RLlr` emit about
800 particles per second and reach its authored 880-particle cap. The renderer
now applies that multiplier and cap, with frame-time accumulation. Community
Weather.slk guidance describes `emrate` as particles per second and
`particles` as a maximum, so the field relationship alone does not prove the
20 Hz interpretation ([The Helper weather guide](https://world-editor-tutorials.thehelper.net/cat_usersubmit.php?view=112038)).

`RLlr` also authors `alphaMode=0` and alpha 150. The live alpha trace above used
Prologue01's `RLhr`; the `RLlr` row has not had a separate runtime capture.

The shipped `RLlr` row has `head=0,tail=1`; the renderer draws its tail
primitive. Rows with both flags clear have no authored primitive and are
skipped with a diagnostic instead of being silently converted to a head.

**Implementation status:** OpenRealm currently emits at the 20 Hz equivalent
rate and enforces the authored live-particle cap per weather effect. The rate
multiplier remains a best guess pending a same-map, same-camera, same-duration
comparison with retail. Keep that density question separate from the confirmed
mode-0 alpha path above; `alphaMode` values other than zero remain unverified.

## Lifecycle And Networking

`level.weather_effects[]` owns stable weather handles for the map lifetime.
JASS gets light handles to those slots; renderer objects are not JASS objects.
Each `svc_frame` carries the complete registered weather set in its game-owned
datagram. The client caches that set in `client_state` and passes it through
`viewDef`; the renderer reconciles its particle runtime while rendering, so
packet loss and reconnects converge without a renderer command API.

Map-authored W3I/W3R weather starts enabled.  JASS-created weather starts
disabled until `EnableWeatherEffect(..., true)`.

The fixed weather registry and `next_weather_id` are part of WC3 save format
version 10.  `weathereffect` JASS values snapshot as stable registry-slot
indexes, and load/reconnect replays the restored authoritative set to the
renderer.  See [Save/Load](save-load.md).

Disabling/removing stops new emission.  Already-spawned cosmetic particles are
allowed to expire by their authored lifespan rather than being converted into
networked state.

## Deliberate Gaps

The initial renderer intentionally does **not** guess behavior that was not
confirmed strongly enough:

- `useFog` is parsed but is not mapped to a new weather-specific fog policy;
  the existing shared particle FOW behavior remains unchanged;
- `long` is parsed but not yet used because its exact legacy emitter-extent
  transform is not verified;
- `AmbientSound` is parsed but weather ambient audio is not yet wired through
  `AmbienceSounds.slk`;
- the camera-local emission window is a bounded renderer performance policy;
  the exact retail camera footprint and weather-rectangle edge behavior are not
  claimed yet;
- `angx`/`angy` currently use the conventional vertical-vector X/Y rotation;
  exact retail sign/order quirks have not been claimed;
- overlapping weather effects are independently composited; retail edge/fade
  quirks have not been claimed;
- only `war3map.w3r` version 5 is accepted by the new region-weather parser.

These are compatibility follow-ups, not reasons to put guesses into the current
renderer.

## Verification

In-engine tests in `games/warcraft-3/game/tests/t_api.c` cover:

- JASS add/enable preserving rawcode and rectangle;
- JASS remove releasing the runtime slot;
- W3I global and W3R-derived region weather starting enabled;
- weather light-handle save-codec identity and a full save/load round trip of
  rawcode, rectangle, enable state, handle ID, and JASS global identity.

For visual verification, use a map with shipped heavy/light rain and test:

1. global map weather;
2. a rectangular weather region while panning across its edge;
3. JASS create -> enable -> disable -> re-enable -> remove;
4. a map-imported `TerrainArt\Weather.slk`/rain texture override;
5. different frame rates to confirm emission speed/density remains stable;
6. a custom `Weather.slk` row with non-zero `var`/`lati` and distinct head/tail
   UV stages, including `head=1,tail=1`, to confirm the two primitives share
   motion while selecting their independent atlas curves.

The renderer-level `test-mdx-ui` suite directly exercises weather emission,
alpha, tail geometry, and rejection of a row with neither primitive flag.

Do not add weather debug logging to do this; use the authored-map cases above
as the acceptance checks.

## See Also

- [SLK Spreadsheet Format](../../../games/warcraft-3/file-formats/slk.md)
- [JASS Native Coverage](../../../games/warcraft-3/jass-native-coverage.md)
- [Server-Selected Presentation Effects](../../architecture/server-selected-effects.md)
- [Ability, Buff, And Item Presentation Effects](ability-and-item-effects.md)
- [Save/Load](save-load.md)
