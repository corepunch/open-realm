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
particles across the entire map.  Emission uses the authored `emrate *
deltaTime`, so particle creation is frame-rate independent without inventing a
region-area multiplier.

Spawn height is terrain height plus the authored `height`.  `angx`/`angy` rotate
the base vertical direction, `veloc` supplies signed speed, and `accel` changes
speed along that direction.  Weather uses renderer-local xorshift state rather
than gameplay RNG.

The shared particle representation now has an optional generic world-space
`tail` vector.  A zero vector keeps the old camera-facing billboard path.  A
non-zero vector renders a camera-facing quad from `position - tail` to
`position`; WC3 weather sets that vector from `velocity * taillen`.  This keeps
rain streak support game-neutral and leaves existing MDX/WoW particle callers
unchanged.

Start/mid/end RGB/alpha and scale reuse the shared particle interpolation.
Texture rows/columns reuse the existing lifetime atlas path.

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

- `Weather.slk` numeric `alphaMode` is parsed but the first implementation uses
  normal alpha blending for weather particles until the complete mode mapping
  is verified;
- `useFog` is parsed but is not mapped to a new weather-specific fog policy;
  the existing shared particle FOW behavior remains unchanged;
- `particles` is parsed but not yet enforced as a per-weather-system live
  particle cap; the shared renderer pool remains the hard global bound;
- `var`, `lati`, `long`, and the explicit head/tail UV start/mid/end fields are
  parsed but not yet used because their exact legacy transforms are not verified;
- combined `head=1` + `tail=1` weather currently uses the tail primitive rather
  than drawing a second independent head primitive;
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
5. different frame rates to confirm emission speed/density remains stable.

Do not add weather debug logging to do this; use the authored-map cases above
as the acceptance checks.

## See Also

- [SLK Spreadsheet Format](file-formats/slk.md)
- [JASS Native Coverage](jass-native-coverage.md)
- [Server-Selected Presentation Effects](../../architecture/server-selected-effects.md)
- [Ability, Buff, And Item Presentation Effects](ability-and-item-effects.md)
- [Save/Load](save-load.md)
