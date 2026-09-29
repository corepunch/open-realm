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

Weather particles use ordinary alpha blending, preserving the behavior from
before the weather fidelity changes. `Weather.slk`'s `alphaMode` is parsed but
not applied; authored start/mid/end alpha still participates in particle color
interpolation and source-alpha blending.

## Undead01 Rain Fidelity: Best Guess

Undead01's JASS enables `RLlr` (Lordaeron light rain) in
`extracted/Undead01/war3map.j`. The base `War3.mpq` row has `emrate=40`,
`lifespan=1.1`, `particles=880`, `alphaMode=0`, `alphaStart/Mid/End=150`,
`head=0`, and `tail=1`. This is separate from the map's `SetSkyModel` choice.

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

**Alpha behavior:** `RLlr` authors `alphaMode=0` and alpha 150. OpenRealm keeps
the pre-change behavior and uses ordinary alpha blending, so the authored 150
value participates in source-alpha compositing. The report that retail rain
looks slightly more opaque does not establish whether retail uses different
blend mapping, texture-alpha handling, or effective vertex alpha. Keep the
authored value unchanged until those possibilities are compared directly.

**Implementation status:** OpenRealm currently emits at the 20 Hz equivalent
rate and enforces the authored live-particle cap per weather effect. This is a
best guess pending a same-map, same-camera, same-duration comparison with
retail. Check alpha separately over light and dark backgrounds; do not infer an
alpha-mode change from particle density. `alphaMode` and the authored alpha
values remain unchanged.

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

Do not add weather debug logging to do this; use the authored-map cases above
as the acceptance checks.

## See Also

- [SLK Spreadsheet Format](file-formats/slk.md)
- [JASS Native Coverage](jass-native-coverage.md)
- [Server-Selected Presentation Effects](../../architecture/server-selected-effects.md)
- [Ability, Buff, And Item Presentation Effects](ability-and-item-effects.md)
- [Save/Load](save-load.md)
