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

Both paths queue vertices into fixed material batches keyed by texture and shader. `R_GameDrawAlphaSurfaces()` flushes each occupied batch with one `GL_STREAM_DRAW` buffer re-specification and one draw call. Re-specifying the complete streaming buffer permits driver-side buffer orphaning and avoids overwriting storage still consumed by the GPU, so extra terrain-fit triangles do not create extra draw calls. `WOW_SPLAT_BATCHES` (8) slots and `WOW_SPLAT_BATCH_VERTICES` (4096) cap the batch table; more distinct materials than slots forces a full flush, which is fine for today's shadow + selection-ring material set — bump both if new splat materials appear.

Creature selection depends on the replicated `entityState.radius`. WoW model collision radii may be `0.5`, so the WoW build encodes this field with the two-byte `NFT_PACKED_FLOAT` under `#ifdef WOW`; `NFT_ROUND` truncated such radii to zero and caused the renderer to reject the resulting zero-area circle. WC3 keeps `NFT_ROUND` because its building/destructable selection radii exceed the packed-float ±65.5 range.

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
- Static M2s without keyed transform tracks are grouped by model and submitted through reusable instance VBOs.
- WMO triangles are coalesced by texture within each group. A second model-wide material layout is used when at least half the model's groups are visible; sparse views retain group culling.
- Missing doodad/WMO models are counted and can be represented by debug marker geometry when debug flags are enabled.

Game entities are not spawned for every ADT doodad. `games/world-of-warcraft/game/g_wow.c` logs that static ADT doodads are renderer-owned and not synchronized as entities.

WMO group bounds are rejected when their transformed bounding sphere lies wholly
beyond the fully opaque fog distance. This matters more than the projection plane:
without the CPU rejection, OpenGL still submits every material batch and lets clipping
happen after the expensive Metal/OpenGL state work. Large buildings crossing the fog
boundary remain visible because the test subtracts their sphere radius.

## Distance Fog And Hard Clip

WoW uses two distances: geometry becomes fully fog-colored first, then a slightly
farther hard plane clips it. Blizzard describes this exact separation and the later
terrain/model LOD work required to extend it in
[Engineer's Workshop: Extended Draw Distance](https://worldofwarcraft.blizzard.com/en-us/news/20139979/engineers-workshop-extended-draw-distance).

The installed 1.5 `dbc.MPQ` has no `Light.dbc`, `LightParams.dbc`,
`LightIntBand.dbc`, or `LightFloatBand.dbc`; verify that before attempting the later
DBC-driven lighting chain:

```sh
build/bin/mpqtool -mpq data/world-of-warcraft/dbc.MPQ ls DBFilesClient | rg '^Light'
```

Consequently this client uses an explicit outdoor fallback: fog starts at 500, is
opaque at 650, and the camera hard-clips at 700 world units. This is twice the
reverse-engineered classic `farclip` default of 350 while remaining inside the current
small ADT streaming design. The local reference is `data/whoa-master/src/world/CWorldParam.cpp`
(default) plus `data/whoa-master/src/world/CWorld.cpp` (183.33--791.67 classic clamp).
`r_fog`, `r_fog_start`, and `r_fog_end` allow live visual
diagnosis; `r_fog 0` intentionally exposes the hard boundary. Terrain/WMO use the WoW
world shader, while M2 entities and instanced doodads use the shared MDX/M2/M3 model
shader's existing fog uniforms.

MDDF positions are absolute map coordinates, not tile-local coordinates. Both the
renderer and the game-side interactive-object path use `CM_WowObjectPoint`:

```text
engine.x = 32 * WOW_ADT_SIZE - mddf.position.z
engine.y = 32 * WOW_ADT_SIZE - mddf.position.x
engine.z = mddf.position.y
```

The authored three-axis rotation and `scale / 1024` are preserved. Never replace
MDDF Z with a terrain-height query: elevated statues, signs, and props are authored
relative to platforms and WMO geometry. `CM_WowAdtPath` also derives each ADT path
from the currently loaded WDT rather than assuming Azeroth.

A bounded Northshire diagnostic reported zero game-side ADT game objects, so the
observed crusader statue is a renderer-owned MDDF instance. Its client-authored
record is elevated and rotated; the corrected game-side path prevents future
interactive duplicates from disagreeing with that renderer placement, but does not
rewrite the source MDDF placement.

### Fast placement verification

Do not assume that a visible doodad is a game entity. The two MDDF consumers are:

| Consumer | Purpose | Transform path |
| --- | --- | --- |
| `renderer/wow/r_wowmap_objects.c` | All visible static doodads | `Wow_ObjectPoint` -> `CM_WowObjectPoint` |
| `game/g_gameobject.c` | DBC-matched interactive entities | `WowGo_SetDoodadTransform` -> `CM_WowObjectPoint` |

Check the existing startup line before changing either path:

```text
WoW: spawned N game objects from ADT doodads (N interactive)
```

If `N=0`, changing `WowGo_SpawnDoodad` cannot affect the visible object. The
Northshire statue investigation produced `N=0` and this renderer-owned MDDF:

```text
model=world\dungeon\scarletmonastery\passivedoodads\statues\statuehmcrusader.mdx
position=(17598.289, 90.646, 14467.403) rotation=(0, 138.5, 0) scale=1863
```

Compare that raw record, `CM_WowObjectPoint`, and its supporting WMO/platform.
Do not terrain-snap its authored Z.

## Minimap

Classic WoW ships pre-baked 256x256 minimap tiles. `Textures/Minimap/md5translate.trs`
maps logical names such as `Azeroth\map32_48.blp` to hashed BLP names stored under
`Textures/Minimap/`. `R_RegisterMap` parses the map's entries once, and
`Wow_DrawMinimap` crops and rotates the at-most-four tiles intersecting the
camera-centered 320-world-unit square. Do not re-render terrain and WMOs into the
minimap: that duplicated hundreds of main-view draw submissions every frame.
Resolved tile handles are stored directly in the 64x64 map table, so steady-state
drawing performs no path formatting, MPQ lookup, or linked texture-cache search.

[WoWee's minimap](https://github.com/Kelsidavis/wowee/blob/main/src/rendering/minimap.cpp)
also reads `md5translate.trs`, but periodically builds a nine-draw 3x3 off-screen
composite and then displays one quad. The current OpenGL path is deliberately simpler:
the small 320-unit crop intersects only one to four tiles, so direct cached quads avoid
an FBO, a 768x768 composite texture, descriptor/state restoration, and refresh work.
If the minimap radius grows past one tile, reassess that trade-off.

Diagnostic lookup:

```sh
build/bin/mpqtool -data data/world-of-warcraft cat 'Textures/Minimap/md5translate.trs'
build/bin/mpqtool -data data/world-of-warcraft cat 'Textures/Minimap/ea283abc0bf9637c3fad5e840a65b38b.blp'
```

Each game owns its minimap drawing via the `R_GameDrawMinimap(LPCRECT screen)` renderer hook, dispatched from the shared `R_DrawMinimap`:

- WC3 draws the `war3mapMap` texture plus the fog-of-war overlay and camera view rect.
- SC2 draws its map minimap texture.
- WoW draws the translated Blizzard minimap tiles above.

The circular frame is the existing `Interface\Minimap\UI-Minimap-Border.blp` overlay (a ring with a transparent center), not a stencil.

## Current Limits

- Terrain rendering is the core focus.
- DBC-driven lighting, WMO portals, distant LOD, water, and some animation fidelity are incomplete.
- The draw window and asset compatibility are tuned around local classic-era data.
- Production support for arbitrary WoW client versions is not present.
