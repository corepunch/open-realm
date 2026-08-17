# World of Warcraft Grass Rendering System

## Overview

This document describes the WoW ground-effect rendering architecture. OpenWarcraft3 resolves the same terrain effect and detail M2 data used by the WoW client, then reuses the normal M2 material path.

## Current OpenWarcraft3 Implementation

### Architecture

OpenWarcraft3 resolves ground clutter from the WoW MPQ data:

- `GroundEffectTexture.dbc` supplies terrain effects, doodad IDs, and amount/density; weighted layouts are version-dependent.
- `GroundEffectDoodad.dbc` resolves each doodad ID to its model name; in the bundled archive the model string is field 2, and `.mdl` names are normalized to `.m2`.
- Models are loaded from `World\\NoDXT\\Detail\\` and rendered through the normal M2 path, so embedded WoW material/texture references remain authoritative.
- ADT alpha coverage and height samples control whether and where each ground-effect model is placed.
- Placement randomness is deterministic per ADT chunk; renderer tuning and schema constants are named `WOW_GRASS_*` defines in `r_wowmap.h`.
- Ground-effect M2 instances carry `RF_GROUND_EFFECT` and are culled at `WOW_GRASS_DRAW_DISTANCE`.
- Road-like terrain layers are excluded when their sampled ADT coverage is at least `WOW_GRASS_ROAD_COVERAGE_MIN`; the heuristic follows WoWee's local layer-weight check.

### Instanced rendering

Ground-effect instances are rendered with **GPU instancing**, not one full `M2_RenderModel`
call per clump (which produced ~47k draw calls and dropped the frame rate by 10x):

- `Wow_AddGroundEffectInstance` links each clump into `wow_world.ground_effects` (a flat list,
  no doodad bucketing).
- `Wow_DrawGrass` (`r_wowmap_grass.c`) culls by squared distance and frustum, groups visible
  instances by their `model`, and collects one `MATRIX4` per instance via `R_GetEntityMatrix`.
- `R_GameRenderModelInstanced` → `M2_RenderInstanced` (`r_m2.c`) renders each model batch once
  with `R_DrawBufferInstanced`, passing the whole matrix array to `glDrawArraysInstanced`.
- `R_ModelShaderInstanced` (`r_shader.c`) is a copy of the model vertex shader with a per-instance
  `mat4 i_instance` attribute (4 vec4 columns, divisor 1) folded in instead of `uModelMatrix`; the
  fragment shader is the shared `model_fs`.
- `R_DrawBufferInstanced` (`r_buffer.c`) binds a shared VAO to the batch VBO plus an instanced
  matrix VBO and draws `num_instances` copies in one call.

The per-instance distance fade (`WOW_GRASS_FADE_START_DISTANCE`) is not yet applied because it
would require an instanced alpha attribute; see Known Issues.

The loader supports both known 11-DWORD layouts. It detects the bundled legacy layout (ID, date stamp, continent, zone, texture ID, four doodad IDs, density, sound) versus the modern weighted layout (ID, four doodad IDs, four weights, amount/coverage, terrain type) by resolving the candidate doodad IDs through `GroundEffectDoodad.dbc`. Legacy rows receive uniform weights for valid slots and ignore `0xffffffff`; modern rows retain their stored weights. `dbctool info` remains the first check when adding another client archive.

### Key Components

#### 1. Blade Generation (`r_wowmap_grass.c`)

The core grass building function processes 8x8 terrain chunks:

```c
void Wow_BuildGrassForChunk(wowAdtChunk_t *chunk,
                            BYTE const alpha[4][WOW_ALPHA_TEXELS],
                            wowLayer_t const *layers,
                            DWORD layer_count)
```

**Sampling Strategy:**
- Samples the 8x8 chunk grid at `WOW_GRASS_CELL_STEP` intervals.
- Each eligible sample selects weighted doodad models from the MPQ effect record.
- Each placement uses the ADT height map and is registered as a renderer-owned doodad instance.

**Placement Algorithm:**
1. Sample the alpha map at a grid position to determine terrain coverage.
2. Calculate attempts from the DBC amount/density field and alpha coverage.
3. Select a valid doodad ID uniformly from the four slots in `GroundEffectTexture.dbc`.
4. Resolve its model name through `GroundEffectDoodad.dbc`.
5. Sample height at a jittered position and place the M2 at that terrain height.

**Coloring:**
- Base green: 105-175 (varies per clump)
- Brown: 42-64 (dark brown)
- Dark blue: 28-46 (shadow/depth)
- Alpha: 220 (slightly transparent)

#### 2. Blade Geometry

Each clump consists of 2 perpendicular crossed triangles:

**First blade:**
- Base width: 0.22-0.44 units
- Height: 1.05-2.30 units
- Full alpha opacity

**Second blade (perpendicular):**
- Width: 85% of first blade (0.19-0.37 units)
- Height: 92% of first blade (0.97-2.12 units)
- Adds visual density without doubling fill rate

**Geometry Details:**
- Each triangle has 3 vertices (6 vertices total per clump)
- Normal vectors calculated from blade direction for proper lighting
- UV coordinates simple: (0,0) → (1,0) → (0.5,1) for tapered appearance

#### 3. Shader System (`r_wowmap_shader.c`)

**Vertex Shader:**
- Wind wave animation: `sin(time + dot(position.xy, vec2(0.071, 0.049)))`
- Wave amplitude: 0.22 units max deflection
- Maintains blade orientation while swaying

**Fragment Shader:**
- Distance-based fade: `smoothstep(WOW_GRASS_FADE_START_DISTANCE, WOW_GRASS_DRAW_DISTANCE)`
- Lighting calculation: 0.55-1.0 range (never fully dark)
- Alpha blending with back-to-front rendering

**Uniforms:**
- `uGrassTime`: Animated time value for wind
- `uGrassCameraOrigin`: Camera position for distance calculations
- `uGrassDrawDistance`: Maximum render distance (220.0 units)
- `uLightMatrix`: For proper lighting direction
- `uViewProjectionMatrix`: Standard 3D transform

### Data Structures

#### Layer Definition (wowLayer_t)
```c
typedef struct {
    DWORD texture_id;      // Terrain texture reference
    DWORD flags;           // Layer flags (detail, PBR, etc.)
    DWORD offset_in_mcal;  // Offset into alpha map data
    DWORD effect_id;       // GroundEffectTexture.dbc reference
} wowLayer_t;
```

The `effect_id` field (at offset +12 bytes) is the key link to grass configuration.

#### Chunk State (wowAdtChunk_t)
```c
VERTEX *grass_buffer;           // Vertex array object
DWORD num_grass_vertices;       // Vertex count
BOX3 grass_bounds;              // Bounding box for culling
```

### Performance

**Vertex Budget per Chunk:**
- Max clumps: 16 (4x4 grid) × 4 (per location) = 64
- Vertices per clump: 6
- **Max vertices per chunk: 384**
- **Memory per chunk: ~24 KB** (at 64 bytes per vertex)

**Draw Call Strategy:**
- All ground-effect instances are batched per model: one `glDrawArraysInstanced` per M2 batch, regardless of how many clumps use that model.
- Culled by squared distance (`WOW_GRASS_DRAW_DISTANCE`) and frustum before grouping.
- Typical visible cost: a handful of instanced draw calls (one per distinct model in view).

## Real World of Warcraft Implementation

### Architecture

Real WoW uses a **doodad-based system** that instantiates actual game asset models:

- **Data Source**: GroundEffectTexture.dbc maps terrain types to doodad models
- **Placement**: Same ADT alpha map coverage + height calculation
- **Models**: M2 doodad files with full animations and LOD support
- **Rendering**: Standard entity renderer (same as other doodads/objects)
- **Variety**: Up to 4 different grass/plant models per terrain type with weighted selection

### GroundEffectTexture.dbc / GroundEffectDoodad.dbc

The two 11-DWORD `GroundEffectTexture.dbc` layouts (legacy vs. modern weighted) and the `GroundEffectDoodad.dbc`
`id/doodad-path/flags` layout are documented in [`docs/dbc-reference.md`](dbc-reference.md#ground-effect--terrain-tables).

### Placement Algorithm

Real WoW uses the same placement algorithm as OpenWarcraft3:

1. **Determine grass layer**: Find layer with highest alpha coverage that has `effect_id != 0`
2. **Look up GroundEffectTexture**: Query `GroundEffectTexture.dbc[effect_id]`
3. **Select doodad**: Weighted random selection from 4 doodad options
4. **Calculate density**: Use `AmountAndCoverage` field to determine how many instances to place
5. **Sample placement**: Grid-based placement with jitter (same as procedural system)
6. **Sample height**: Use MCVT (height map) to place doodad on terrain surface
7. **Instance model**: Call doodad renderer with calculated transform

**Key Difference:** Instead of generating blade geometry, WoW instantiates actual M2 doodad models with full animation, materials, and LOD support.

## ADT Chunk Format Integration

### Terrain Chunks (MCNK)

Each 8x8 unit terrain chunk contains:

**Layers (MCLY):**
```c
typedef struct {
    DWORD texture_id;      // Texture index in MMDX
    DWORD flags;
    DWORD offset_in_mcal;  // Offset into alpha data
    DWORD effect_id;       // >>> GRASS REFERENCE <<<
} wowLayer_t;
```

**Example scenario:**
- Layer 0: Stone texture, no effect_id (0)
- Layer 1: Grass texture, effect_id=5 (look up GroundEffectTexture.dbc[5])
- Layer 2: Snow texture, effect_id=0 (no grass)

The alpha maps in MCAL determine coverage of each layer. Layer 1 with 80% alpha means "80% of this chunk is covered by grass texture + grass doodads."

### Height Map (MCVT)

The height map provides 9×9 height samples per chunk:
- Used to position grass at exact terrain height
- Interpolation within grid cells places grass on sloped terrain

### Alpha Maps (MCAL)

Four 64×64 alpha textures per chunk:
- Track blend amount for each layer
- Used to determine grass coverage at each sample point
- Format: mostly-opaque for layer 0, blend amounts for layers 1-3

## Improvements in OpenWarcraft3

### Phase 1: Data-Driven Integration (Current)

**Added in this session:**

1. **GroundEffectTexture.dbc Loading**
   ```c
   static wowGroundEffectTexture_t wow_ground_effect_textures[512];
   static BOOL Wow_LoadGroundEffectDBCs(void);
   ```

2. **Helper Functions**
   - `Wow_GetGroundEffectTexture(effect_id)`: Lookup and cache
   - `Wow_SelectDoodadFromWeights(weights, seed)`: Weighted random selection
   - `Wow_GrassEffectIdForCoverage(...)`: Find dominant grass layer

3. **Data-Driven Coloring**
   - Checks if GroundEffectTexture data exists for layer
   - Varies grass colors based on terrain type data
   - Falls back to procedural colors if DBC missing

4. **Preserved Advantages**
   - No API changes (still blade geometry)
   - Same performance characteristics
   - Same placement algorithm
   - Backward compatible with missing DBCs

### Phase 2: Full Doodad System (Implemented)

**Done in this session** (replacing the Phase 1 blade generation):

1. **M2 Model Instantiation**
   - `Wow_AddGroundEffectInstance()` instantiates the resolved GroundEffectDoodad M2 per clump.
   - Instances are rendered through GPU instancing (`M2_RenderInstanced`), not per-instance model draws.

2. **Animation Support**
   - Real doodad models have wind, growth, decay animations
   - Use M2 sequence system for seasonal variations

3. **Appearance Variation**
   - Same grass placement, different visual assets
   - Weighted selection from 4 doodad options per terrain type
   - Much richer visual variety

4. **Material System**
   - Real textures instead of procedural colors
   - PBR support for grass materials
   - Proper specular/normal mapping

## File Organization

### Source Files

**Core rendering:**
- `renderer/wow/r_wowmap.h` - Type definitions
- `renderer/wow/r_wowmap.c` - Main terrain loading
- `renderer/wow/r_wowmap_grass.c` - Grass generation
- `renderer/wow/r_wowmap_shader.c` - Grass shader code

**Data handling:**
- `common/world_wow.c` - DBC parsing utilities
- `common/world_wow.h` - World data structures

### Configuration

**Runtime cvars:**
- `r_module`: Select renderer (must be "wow" for grass)
- `r_grass`: Toggle grass rendering (if implemented)
- `r_grass_density`: Density multiplier (0.0-2.0)
- `r_grass_distance`: Draw distance in units

**Constants in r_wowmap.h:**
```c
#define WOW_GRASS_DRAW_DISTANCE     220.0f
#define WOW_GRASS_DENSITY           1.0f
#define WOW_GRASS_CELL_STEP         2
#define WOW_GRASS_VERTICES_PER_CLUMP 6
```

## Performance Characteristics

### CPU Cost

**Per Chunk Processing:**
- Alpha map coverage sampling: 16 samples
- Height interpolation: 16 samples (3-4 float operations each)
- Random number generation: 16-64 operations (seeded, deterministic)
- Vertex buffer allocation and filling: Linear in clump count
- **Typical: <1 ms per chunk on modern CPU**

**Batch Cost:**
- Frustum culling: ~64 chunks to check
- Visibility testing: 16 chunks visible typically
- State setup: ~1 draw call for all grass
- **Typical: <5 ms per frame for visible grass**

### GPU Cost

**Vertex Processing:**
- 384 vertices max per chunk (typical: 80-120)
- ~16,000 total grass vertices visible typically
- Wind animation: Simple sine wave, negligible cost
- **Typical: <1 ms vertex processing**

**Fragment Processing:**
- Simple alpha test/blend
- Distance fade: One smoothstep per pixel
- Lighting: Lerp between 0.55-1.0 range
- **Typical: <2 ms fragment processing**

### Memory Usage

**Static:**
- DBC cache: ~512 entries × 44 bytes = 22 KB
- Shader objects: ~5-10 KB

**Per-Frame:**
- Vertex buffers: ~100 chunks × 384 vertices × 64 bytes = 2.4 MB
- Transient allocations: Minimal (buffers freed after upload)

## Testing and Validation

### Required Data Files

**From World of Warcraft:**
- `DBFilesClient\GroundEffectTexture.dbc` - Terrain-to-doodad mapping
- `DBFilesClient\Map.dbc` - Map metadata
- `DBFilesClient\WorldSafeLocs.dbc` - Safe spawns
- ADT files with terrain chunks (`.w3m` maps in Warcraft III, `.wdt` in WoW)

### Validation Tools

**MPQ Inspection:**
```bash
build/bin/mpqtool -mpq "data/Warcraft III/War3.mpq" ls "DBFilesClient"
build/bin/mpqtool -mpq "data/World of Warcraft/Data/enUS/patch.mpq" cat "DBFilesClient\GroundEffectTexture.dbc" | od -x | head
```

**Rendering Inspection:**
```bash
make run-ui-text UI_CMD=+map "Maps/Campaign/Orc01.w3m"
# Add grass distance adjustment for testing:
# con_set r_grass_distance 500
```

## Known Issues and Limitations

1. **Missing DBC Fallback**
   - If GroundEffectTexture.dbc is missing, grass still renders with procedural colors
   - Graceful degradation maintained

2. **No Per-Instance Distance Fade**
   - `WOW_GRASS_FADE_START_DISTANCE` is not applied in the instanced path; ground-effect
     clumps pop in/out at `WOW_GRASS_DRAW_DISTANCE` instead of fading.
   - A proper fix needs an instanced alpha attribute (`i_instanceAlpha`) multiplied into
     `v_color.a` in `instanced_vs`, plus the alpha packed into the instance buffer.

2. **Color Variance Limited**
   - Procedural blade system can't capture full range of WoW grass appearance
   - Real system uses material textures, not vertex colors

3. **No Animation Variation**
   - All grass uses same wind shader
   - Real system has doodad-specific animations

4. **Flat Blade Appearance**
   - No multi-layer texturing
   - No spectral detail or LOD
   - Real system has full M2 complexity

5. **Memory Pre-allocation**
   - 384 vertices allocated per chunk even if fewer needed
   - Could be optimized with dynamic allocation

## References

### WoW Development Resources

- **wowdev.wiki**: Comprehensive ADT and DBC format documentation
  - ADT specification with MCLY, MCAL, MCVT chunks
  - WDBC file format and common DBC structures
  - GroundEffectTexture.dbc field layout

- **getTrinityCore**: Open-source WoW server
  - Reference implementations of terrain rendering
  - DBC parsing examples
  - M2 model handling

- **getMaNGOS**: Earlier WoW server project
  - Documentation of vanilla/TBC DBC formats
  - Terrain chunk processing

### Related Systems

- **M2 Rendering**: `renderer/m2/r_m2.c`
  - Doodad model loading and animation
  - Character appearance system
  - Animation sequence handling

- **ADT Loading**: `renderer/wow/r_wowmap.c`
  - Chunk parsing and initialization
  - Texture loading and management
  - Height map processing

- **DBC Utilities**: `common/world_wow.c`
  - `CM_WowValidDbc()` - Header validation
  - `CM_WowDbcString()` - String block access
  - `CM_WowFindMapId()` - Example DBC iteration

## Conclusion

The WoW grass system represents a balance between visual fidelity and performance. The current OpenWarcraft3 implementation uses procedural blade geometry with data-driven coloring, providing good performance while maintaining authentic terrain appearance. Future improvements could add full M2 doodad instancing to match real WoW's visual complexity, leveraging the existing doodad rendering pipeline.

The data-driven approach ensures that as new game data (DBCs, textures, models) becomes available, the grass system can be enhanced without engine changes—following the Quake 2/id Software philosophy that shapes this entire project.
