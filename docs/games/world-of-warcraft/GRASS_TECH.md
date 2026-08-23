# Grass Rendering — Technical Reference

Current implementation state and math. For the design rationale and forward-looking
phases see [static-grass-and-height-atlas.md](static-grass-and-height-atlas.md); for
the original architecture walkthrough see [grass-rendering-system.md](grass-rendering-system.md).

---

## Active vs. Disabled Paths

```c
#define WOW_GRASS_CAMERA_MESH 0   // 0 = instanced-M2 path active; 1 = camera-grid prototype
```

| Path | Status | File |
|---|---|---|
| **M2 instanced** | Active | `r_wowmap_grass.c`, `r_m2.c`, `r_shader.c` (`instanced_vs`) |
| Camera-grid static mesh | Disabled | `r_wowmap_grass.c`, `r_wowmap_shader.c` (`vs_wow_grass`) |

The camera-grid path is disabled because its generic 12-vertex cross cannot carry
authoritative doodad geometry or material identity. Do not re-enable it until the
control texture encodes model/material selection. See the static-grass doc for the
complete correctness requirements.

### Original 2004 Elwynn Size Oracle

The original Northshire reference has short ground detail reaching roughly the
ankle or lower shin, unlike later references with waist-high stylized grass. The
active renderer already matches the 2004 size source: it draws the authoritative
`World\NoDXT\Detail\ElwGra*.m2` assets at scale `1.0`. Their visible heights are
mostly `0.288..0.620` world units (`ElwGra02` is the tall `1.593`-unit outlier),
against a visible `HumanMale.m2` height of `2.035` units. Therefore grass height
should not be increased from the later screenshots. Our field can look fuller
than the 2004 capture because density was deliberately increased by 1.5×; treat
size and density as separate comparisons.

---

## M2 Instanced Path

### Placement pipeline (CPU, once per ADT load)

```
MCNK → Wow_BuildGrassForChunk()
  for each of 8×8 cells (WOW_GRASS_CELL_STEP = 1):
    check no_effect_mask bit
    sample MCAL coverage at cell → coverage [0-255]
    reject if coverage < WOW_GRASS_COVERAGE_MIN (32)
    reject if road layer alpha ≥ WOW_GRASS_ROAD_COVERAGE_MIN (24)
    resolve effect_id → GroundEffectTexture.dbc record
    clumps = MIN(MAX(1, ceil(coverage/255 × min(density, 24))), 12)
    for each clump:
      jitter position within cell
      sample MCVT height
      weighted-random select from 4 doodad slots
      Wow_AddGroundEffectInstance(model_path, origin, yaw)
```

`Wow_AddGroundEffectInstance` links placements into a per-model list. The first call
to `Wow_DrawGrass` groups them, builds one `GL_STATIC_DRAW` instance VBO per M2 model,
then frees all CPU placement nodes. Subsequent frames submit the resident VBOs without
any per-instance CPU work.

### Density formula

```c
clumps = MIN(MAX(1, (int)ceilf(coverage / 255.0f * MIN(density, WOW_GRASS_DBC_DENSITY_MAX))),
             WOW_GRASS_MAX_PLACEMENTS_PER_SAMPLE);
// WOW_GRASS_DBC_DENSITY_MAX       24   cap on GroundEffectTexture.dbc density field
// WOW_GRASS_MAX_PLACEMENTS_PER_SAMPLE 12   hard ceiling on instances per cell sample
```

At full coverage and max DBC density: `ceil(1.0 × 24) = 24` → capped to 12.
At half coverage and density 8: `ceil(0.5 × 8) = 4`.

### Wind sway (instanced vertex shader, `r_shader.c`)

```glsl
// Uniforms uploaded by M2_RenderInstanced():
//   uGrassWind  = vec3(WIND_SPEED=1.7, WIND_AMPLITUDE=0.12, ROOT_FRACTION=0.15)
//   uGrassPhase = vec4(PHASE_X=0.917, PHASE_Y=1.481, DIR_X=0.86, DIR_Y=0.51)
//   uGrassTime  = elapsed seconds
//   uGrassHeight = vec2(model_bounds_z_min, model_bounds_z_max)

float grassHeight = max(uGrassHeight.y - uGrassHeight.x, 0.001);
float grassTop    = smoothstep(uGrassWind.z, 1.0,
                       clamp((position.z - uGrassHeight.x) / grassHeight, 0.0, 1.0));
float grassPhase  = dot(i_instance3.xy, uGrassPhase.xy);   // world XY position
float grassSway   = sin(uGrassTime * uGrassWind.x + grassPhase)
                    * uGrassWind.y * grassHeight * grassTop;
position.xy      += uGrassPhase.zw * grassSway;            // .zw = sway direction
```

`i_instance3.xy` is the world-space translation of the M2 instance (column 3 of the
row-major instance matrix). `grassTop` suppresses sway below `ROOT_FRACTION` of blade
height so roots stay anchored.

#### Phase decorrelation finding

The original phase constants were `PHASE_X = 0.071`, `PHASE_Y = 0.113`. Two blades
2.5 world units apart (one slot spacing) differed by only `0.071 × 2.5 ≈ 0.18 rad ≈ 10°`.
Blades within a ~25-unit radius appeared synchronized — visibly unnatural.

The fix: raise constants until adjacent blades differ by ~120°:

| Constant | Old | New | Phase diff at 2.5 u |
|---|---|---|---|
| `WOW_GRASS_WIND_PHASE_X` | 0.071 | **0.917** | ~131° |
| `WOW_GRASS_WIND_PHASE_Y` | 0.113 | **1.481** | ~212° (≈ 148° wrapped) |

The ratio `1.481 / 0.917 ≈ φ²` (where φ = golden ratio ≈ 1.618) avoids grid-aligned
periodicity — no row or column of blades shares the same phase.

The formula `dot(worldPos, vec2(A, B))` is correct for natural grass: it produces a
propagating wavefront across the field (nearby blades have correlated but not identical
phases) while globally decorrelating distant blades. Avoid replacing it with a pure
per-blade hash, which would break wave propagation.

---

## Camera-Grid Static Mesh Path (disabled)

Defined in `r_wowmap_shader.c`. Active only when `WOW_GRASS_CAMERA_MESH = 1`.

### Grid layout

```
WOW_GRASS_GRID_SIDE  181   // slots per axis; odd keeps one slot centered on camera cell
WOW_GRASS_GRID_HALF   90   // = (GRID_SIDE - 1) / 2
WOW_GRASS_SLOT_SPACING 2.5f // world units between slots
```

`gl_InstanceID` maps to a 2D camera-centered offset:
```glsl
int gx = gl_InstanceID % GRID_SIDE - GRID_HALF;  // [-90, +90]
int gy = gl_InstanceID / GRID_SIDE - GRID_HALF;
vec2 cell    = floor(uCameraXZ / uGrassSlotSpacing) + vec2(gx, gy);
vec2 worldXY = (cell + jitter * 0.72) * uGrassSlotSpacing;
```

Placement is stable because `cell` is computed from integer world-cell coordinates.
Camera movement remaps `gl_InstanceID` to the same integer cell, preventing sliding.

### Wind sway (camera-grid vertex shader, `r_wowmap_shader.c`)

```glsl
float seed  = GrassHash(cell + vec2(41.41, 17.17));  // also drives yaw rotation
float phase = GrassHash(cell + vec2(3.71,  53.9));   // independent phase hash
float top   = clamp(i_texcoord.y, 0.0, 1.0);
float wave  = sin(uGrassTime * 1.7 + phase * 6.2831853) * 0.22 * top;
pos.xy     += vec2(wave, wave * 0.35);
```

`seed` and `phase` use the same `GrassHash` function but different constant offsets so
yaw rotation and animation phase are statistically independent — blades pointing in the
same direction do not sway in sync.

#### Phase/yaw decoupling finding

Before the fix, `phase` was `seed` (same hash). Correlation caused blades with
identical yaw orientations to also sway at the same time. The fix is a second
`GrassHash` call with an orthogonal seed offset `(3.71, 53.9)`.

### GrassHash

```glsl
float GrassHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
```

Standard GPU hash noise. Returns [0, 1). Used for jitter, scale, yaw, density
suppression, and (with independent offsets) animation phase.

---

## Shared Constants Reference

All in `r_wowmap.h`. Every define has a trailing comment in the header; this table
summarises the most change-sensitive values.

| Constant | Value | What it controls |
|---|---|---|
| `WOW_GRASS_WIND_PHASE_X` | 0.917 | rad/world-unit in X for M2 sway phase |
| `WOW_GRASS_WIND_PHASE_Y` | 1.481 | rad/world-unit in Y for M2 sway phase |
| `WOW_GRASS_WIND_SPEED` | 1.7 | rad/s; sway frequency |
| `WOW_GRASS_WIND_AMPLITUDE` | 0.12 | fraction of blade height; peak sway |
| `WOW_GRASS_WIND_ROOT_FRACTION` | 0.15 | normalized height below which sway = 0 |
| `WOW_GRASS_DBC_DENSITY_MAX` | 24 | cap on DBC density field (was 16) |
| `WOW_GRASS_MAX_PLACEMENTS_PER_SAMPLE` | 12 | max M2 instances per cell (was 8) |
| `WOW_GRASS_COVERAGE_MIN` | 32 | alpha threshold to spawn grass at all |
| `WOW_GRASS_ROAD_COVERAGE_MIN` | 24 | road layer alpha that suppresses grass |
| `WOW_GRASS_DRAW_DISTANCE` | 220.0 | world units; cull distance |
| `WOW_GRASS_FADE_START_DISTANCE` | 160.0 | world units; fade begins here |

Density history: `DBC_DENSITY_MAX` and `MAX_PLACEMENTS_PER_SAMPLE` were both scaled
×1.5 from 16/8 to 24/12 to increase field density without changing the placement
formula structure.

---

## Future Tuning

Research across production grass shaders (GPU Gems, Ghost of Tsushima, several open
GitHub implementations) validated our approach and surfaced two cosmetic improvements
worth considering if the sway ever looks stiff or monotonous.

### Two-axis independent oscillation

Current static mesh path uses one sine wave driven diagonally:
```glsl
float wave = sin(uGrassTime * 1.7 + phase * 6.2831853) * 0.22 * top;
pos.xy += vec2(wave, wave * 0.35);
```

Splitting into independent X/Z terms with different time frequencies decorrelates the
two axes so blades don't all trace the same diagonal arc:
```glsl
float waveX = sin(uGrassTime * 1.3 + phase * 6.2831853) * 0.22 * top;
float waveZ = cos(uGrassTime * 0.9 + phase * 6.2831853) * 0.08 * top;
pos.x += waveX;
pos.y += waveZ;   // y is the world-horizontal Z axis in the cross mesh
```
Not yet applied — the current single-term result is acceptable; add this if the motion
looks robotic at closer range.

### Quadratic height falloff

Every surveyed shader uses `bend = uv.y²` (quadratic). Our M2 path's
`smoothstep(ROOT_FRACTION, 1.0, normalizedHeight)` is already close to this curve. The
static mesh path uses `top = clamp(i_texcoord.y, 0.0, 1.0)` — linear. Squaring it:
```glsl
float top = clamp(i_texcoord.y, 0.0, 1.0);
top = top * top;   // quadratic: base stays fully anchored, tip moves more freely
```
makes the blade look more physically rooted without changing any constants.

### Wave-front gust (advanced)

The most natural wind in surveyed shaders propagates a raised-cosine gust band across
the field at a configurable speed, with a sine-perturbed front edge so it doesn't march
in a straight line. See `static-grass-and-height-atlas.md` shader section for the full
pattern; it requires a `windDir` uniform and a `speed` constant but no texture lookup.

---

## Diagnostics

```sh
# Inspect DBC density fields for a given archive:
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\GroundEffectTexture.dbc'
build/bin/dbctool -mpq data/world-of-warcraft/dbc.MPQ info 'DBFilesClient\GroundEffectDoodad.dbc'

# Bounded world run; toggle r_grass at runtime to isolate submission cost:
make run-wow ARGS="+set wow_playerinfo '\race\Human\sex\Male\class\1\appearance\0' \
  +map playercreate +set r_stats 1 +com_frame_limit 300"
```

Runtime toggles: `r_grass`, `r_doodads`, `r_terrain`.
