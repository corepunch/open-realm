# Terrain And World Rendering

## WDT And ADT

The WoW renderer starts from a WDT path and tracks a 64x64 tile grid. Each present tile resolves to an ADT file:

```text
World/Maps/<MapName>/<MapName>_<tile_x>_<tile_y>.adt
```

Important local constants:

| Constant | Value | Meaning |
| --- | --- | --- |
| `WOW_WDT_TILES` | `64` | WDT tile grid size per axis. |
| `WOW_ADT_SIZE` | `533.333313f` | World units per ADT tile. |
| `WOW_ADT_CHUNK_SIZE` | `WOW_ADT_SIZE / 16` | World units per ADT chunk. |
| `WOW_ADT_UNIT_SIZE` | `WOW_ADT_CHUNK_SIZE / 8` | Fine height-grid unit. |
| `WOW_MCVT_COUNT` | `9 * 9 + 8 * 8` | Height samples per ADT chunk. |

The current renderer loads a small ADT window around the active area and keeps an alpha atlas for terrain splat masks.

## ADT Chunk Tags

The code compares chunk tags in reversed byte order. Important ADT tags currently handled:

| Tag In Code | Normal Tag | Purpose |
| --- | --- | --- |
| `XETM` | `MTEX` | Texture filename block. |
| `XDMM` | `MMDX` | Doodad model filename block. |
| `DIMM` | `MMID` | Doodad model filename offsets. |
| `FDDM` | `MDDF` | Doodad placement definitions. |
| `OMWM` | `MWMO` | WMO filename block. |
| `DIWM` | `MWID` | WMO filename offsets. |
| `FDOM` | `MODF` | WMO placement definitions. |
| `KNCM` | `MCNK` | Terrain chunk. |
| `TVCM` | `MCVT` | Terrain heights. |
| `RNCM` | `MCNR` | Terrain normals. |
| `YLCM` | `MCLY` | Texture layers. |
| `LACM` | `MCAL` | Alpha maps. |

## Terrain Layers

Each ADT chunk can carry up to four texture layers. The renderer stores:

- up to four texture handles,
- a per-chunk alpha atlas coordinate,
- decoded alpha maps,
- chunk position,
- `WOW_MCVT_COUNT` heights,
- optional normals,
- bounds for culling.

The splat path has a small Z bias, polygon offset, and height-delta guard to keep decal geometry close to terrain without exploding across sharp height changes. If the camera's ADT window has not produced terrain samples yet, it emits a flat quad instead of dropping the draw.

### Dynamic Splat Batching

WoW splats use top-down rectangular projection, matching the terrain-decal model used by modern large-world renderers. Terrain-conforming splats sample one shared `(cols + 1) x (rows + 1)` height lattice and reuse those vertices between adjacent cells; the old cell-local loop queried four corners per cell. Small splats use at least a 4x4 fitted grid (96 triangle vertices and 25 shared height samples), so selection rings follow local ADT deformation instead of intersecting it as a single flat quad.

Both paths queue vertices into fixed material batches keyed by texture and shader. `R_GameDrawAlphaSurfaces()` flushes each occupied batch with one `GL_STREAM_DRAW` buffer re-specification and one draw call. Re-specifying the complete streaming buffer permits driver-side buffer orphaning and avoids overwriting storage still consumed by the GPU, so extra terrain-fit triangles do not create extra draw calls.

Creature selection depends on the replicated `entityState.radius`. WoW model collision radii may be `0.5`, so this field uses the existing two-byte `NFT_PACKED_FLOAT` encoding; `NFT_ROUND` truncated such radii to zero and caused the renderer to reject the resulting zero-area circle.

WoWee instead builds a flat 48-segment procedural disc, floor-snaps its center periodically, raises it by `0.17`, and disables depth testing. That avoids terrain clipping cheaply, but permits the ring to show through intervening geometry. OpenWarcraft keeps depth testing and fits the batched mesh to terrain; polygon offset plus the small world-space bias handle coplanar depth precision.

References:

- [Khronos: Buffer Object Streaming](https://wikis.khronos.org/opengl/Buffer_Object_Streaming)
- [Khronos: Basics of Polygon Offset](https://wikis.khronos.org/opengl/Basics_Of_Polygon_Offset)
- [GameDev StackExchange: fitted-mesh terrain decals](https://gamedev.stackexchange.com/questions/32095/decal-implementation)
- [Activision: Large Scale Terrain Rendering, decal rendering](https://advances.realtimerendering.com/s2023/Etienne%28ATVI%29-Large%20Scale%20Terrain%20Rendering%20with%20notes%20%28Advances%202023%29.pdf)

## Grass

The WoW renderer builds lightweight grass geometry while loading each ADT chunk. Placement is derived from ADT texture layer data:

- `MCLY.effect_id` marks terrain layers that should emit ground clutter.
- The decoded 64x64 `MCAL` alpha maps decide where those layers are visible.
- Chunk height samples place each grass clump on the terrain surface.

Each generated clump is two crossed, tapered blade triangles in a chunk-local VAO. Rendering uses a small WoW-owned shader with camera-distance fade and cheap vertex wind, and culls whole chunk grass buffers before drawing. The first-pass tuning constants are:

| Constant | Value | Meaning |
| --- | --- | --- |
| `WOW_GRASS_DENSITY` | `1.0f` | Scales generated clumps per eligible layer sample. |
| `WOW_GRASS_DRAW_DISTANCE` | `220.0f` | Camera-space draw/fade distance for grass chunks. |

This is intentionally a first-pass ground-effect renderer. Exact client-style `GroundEffectTexture.dbc` and `GroundEffectDoodad.dbc` model selection can replace the placeholder blade geometry without changing the ADT placement path.

## Height Queries

`games/world-of-warcraft/common/world_wow.c` keeps a one-ADT height cache for collision/spawn queries. It loads `MCVT` height samples from `MCNK` chunks and resolves point height by splitting a local cell around the center sample into triangles, then using barycentric interpolation.

This is intentionally narrow: it is enough to ground actors on loaded terrain while the renderer and game scaffolding evolve.

## Doodads And WMOs

ADT object references are renderer-owned today:

- `MDDF` entries produce doodad instances backed by M2 models.
- `MODF` entries produce WMO instances.
- Doodads are bucketed for draw-distance culling.
- Missing doodad/WMO models are counted and can be represented by debug marker geometry when debug flags are enabled.

Game entities are not spawned for every ADT doodad. `games/world-of-warcraft/game/g_wow.c` logs that static ADT doodads are renderer-owned and not synchronized as entities.

## Current Limits

- Terrain rendering is the core focus.
- WMO, doodad, lighting, grass, particles, water, and animation fidelity are incomplete.
- The draw window and asset compatibility are tuned around local classic-era data.
- Production support for arbitrary WoW client versions is not present.
