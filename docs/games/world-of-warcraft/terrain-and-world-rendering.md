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

`games/world-of-warcraft/common/world_wow.c` keeps a 16-ADT LRU height cache for collision/spawn queries. It loads `MCVT` height samples from `MCNK` chunks and resolves point height by splitting a local cell around the center sample into triangles, then using barycentric interpolation.

`CM_WowFloorHeight` extends that terrain result with WMO floors. The ADT cache retains lightweight `MWMO`/`MWID`/`MODF` instances; shared WMO collision geometry is loaded lazily only when a floor ray enters an instance bound. Root `MOHD` supplies the group count, and each group supplies `MOVT`/`MOVI` plus its authored collision BSP in `MOBN`/`MOBR`. `MOBR` is authoritative for which `MOVI` triangles belong to collision; do not discard those references using render-oriented `MOPY` flags. A bounded Northshire run measured 2,123 triangle tests for 300 floor calls (7.08 per query) instead of scanning all 9,453 Abbey triangles. If a WMO genuinely lacks `MOBN`/`MOBR`, the loader logs it, derives collision candidates from `MOPY`, and builds a 32x32 local-XZ index. Game movement owns the query; the client camera follows the replicated player entity Z and must not run the collision query again.

This fixes buildings such as `World/WMO/Azeroth/Buildings/NSAbbey/NSAbbey.wmo`, whose floor sits above the outdoor ADT. It is a floor trace, not yet full capsule collision against WMO walls.

## Doodads And WMOs

ADT object references are renderer-owned today:

- `MDDF` entries produce doodad instances backed by M2 models.
- `MODF` entries produce WMO instances.
- Doodads are bucketed for draw-distance culling.
- Static M2s without keyed transform tracks are grouped by model and submitted through reusable instance VBOs.
- WMO triangles are coalesced by texture within each group. A second model-wide material layout is used when at least half the model's groups are visible; sparse views retain group culling.
- Missing doodad/WMO models are counted and can be represented by debug marker geometry when debug flags are enabled.

Game entities are not spawned for every ADT doodad. `games/world-of-warcraft/game/g_wow.c` logs that static ADT doodads are renderer-owned and not synchronized as entities.

### WMO lighting

Classic WMO surface lighting is group-authored. Group `MOCV` stores BGRA baked vertex colors and `MONR` stores matching normals. Interior groups (`MOGP.flags & 0x2000`) use the baked color without outdoor directional lighting; exterior groups (`flags & 0x08`) retain world lighting and may use vertex color for authored accents. Keep indoor and outdoor geometry in separate material batches even when they share a texture, otherwise model-wide batching loses the lighting mode. Missing `MOCV` uses neutral `127,127,127`; it is not replaced with guessed ambient data.

Northshire's `NSAbbey.wmo` confirms the contract in the shipped Classic data: 14 groups and 42 `MOLT` lights; its interior groups have matching `MOVT`/`MONR`/`MOCV` counts, while an exterior group has `MONR` but no `MOCV`. `MOLT` is not surface lighting: Classic format research identifies `MOCV` as the only lighting for interior WMO geometry and describes the root lights as inputs for M2 doodads and characters. Preserve `MOLT` for the future WMO-contained doodad lighting path; do not add its lights to WMO wall shading and double-light the baked result.

The correct shader formula in `r_wowmap_shader.c`:

```glsl
// WMO path: v_color.rgb is MOCV fixup-corrected (pre-baked lighting / 2)
// 2× multiplication cancels the /2 in Wow_FixMocvAlpha
color.rgb = color.rgb * 2.0 * v_color.rgb + (uWmoAmbient + uWmoLightAdd) * (1.0 - extBlend);
```

Where `extBlend = v_color.a` (1.0 for exterior batches, 0.0 for interior). Key invariants:

- `Wow_FixMocvAlpha` (CPU, `r_wowmap_objects.c`) divides raw BGRA values by 2. The shader 2× cancels only that division — it must not wrap the ambient/MOLT terms.
- `uWmoAmbient` (`MOHD amb_color / 255`) and `uWmoLightAdd` (`Wow_ComputeMoltContribution`) are additive after the MOCV term, never inside the 2×.
- `v_lighting` (N·L directional) is terrain-only. WMO geometry has baked normals; applying it doubles outdoor sun on pre-lit surfaces.
- Ambient and MOLT apply only to interior batches (`1.0 - extBlend`): exterior MOCV already contains the outdoor sun bake.
- **Overbright bug (old formula):** `color.rgb *= 2.0 * (ambient + lightAdd + MOCV + lighting * extBlend)`. For a neutral wall (`MOCV=0.5`, `lighting=0.75`): `2×(0.5+0.75) = 2.5×` — dramatically overbright.

The current opaque same-texture coalescing does not yet implement the full `MOMT` blend-mode and MOGP batch A/B/C ordering contract. Add material blend classification before enabling WMO transparency; keep transparent batches ordered rather than folding them into the opaque model-wide buffers.

WMO group bounds are rejected when their transformed bounding sphere lies wholly
beyond the fully opaque fog distance. This matters more than the projection plane:
without the CPU rejection, OpenGL still submits every material batch and lets clipping
happen after the expensive Metal/OpenGL state work. Large buildings crossing the fog
boundary remain visible because the test subtracts their sphere radius.

### WMO visibility direction

Large buildings, city blocks, caves, and most dungeon shells are WMO geometry. A WMO is divided into groups (typically rooms or exterior sections), while `MOPT`/`MOPR` describe portals and group relationships. Treating a dungeon as one ordinary mesh throws away the format's principal visibility accelerator.

Many instance maps use the WDT global-WMO form: `MPHD` marks the map accordingly and WDT-level `MWMO`/`MODF` chunks place the shell instead of per-tile ADTs doing so. `Wow_LoadWdtTiles` currently consumes only `MPHD` and `MAIN`, so global-WMO placement must be added before those dungeon shells can share the normal WMO rendering and collision paths. Do not invent terrain tiles for these maps.

The current renderer already performs two useful reductions: per-group frustum/fog culling for sparse views and model-wide material batches for dense views. The next meaningful upgrade is conservative portal traversal when the camera is confidently inside an interior group:

1. Parse group flags and root `MOPT`/`MOPR` data.
2. Locate the camera's interior group.
3. Traverse only portals facing/intersecting the current clipped frustum.
4. Fall back to normal group frustum culling whenever containment or portal data is ambiguous; exterior groups remain visible.

This is preferable to hardware occlusion queries on the macOS OpenGL-to-Metal path, where query readback can serialize the CPU and GPU. It also attacks the real cost visible in profiles: avoided group/material submissions and texture/pipeline changes. Instancing helps repeated WMO placements, but Northshire's expensive buildings are mostly unique instances, so it is secondary to portal visibility and material sorting. Indexed group buffers can later remove the current expanded triangle-list vertex duplication without changing visibility.

[WoWee's WMO renderer](https://github.com/Kelsidavis/wowee/blob/main/src/rendering/wmo_renderer.cpp) is the closest portal implementation reference: it uses conservative interior-only traversal, seeds from camera and character groups, never portal-culls exterior groups, and falls back to frustum culling when containment is ambiguous. Its collision path does **not** consume `MOBN`/`MOBR`: `bspNodes` is declared but never populated, while floor queries use all `MOVI` triangles in per-group 2D grids after instance/group bounds rejection. Keep WoWee's conservative portal rules, but retain our cheaper authored BSP collision. The [classic 1.12.1 client reverse engineering](https://github.com/samwhosung/wow-1121-client-internals/blob/main/docs/models.md) identifies separate inside/outside portal visibility and portal-flood routines plus the WMO segment-intersection wrapper. [TrinityCore's world-object position update](https://github.com/TrinityCore/TrinityCore/blob/master/src/server/game/Entities/Object/Object.cpp) likewise records a distinct static floor and current WMO from its full terrain/collision query.

Bounded verification:

```sh
make run-wow ARGS="+set wow_playerinfo '\race\Human\sex\Male\class\1\appearance\0' +map playercreate +com_frame_limit 400"
```

Expected startup evidence near Northshire includes `NSAbbey.wmo` with 9,453 collision triangles. Do not log per-floor-query data in production.

### Quake II world-model analogy

A WMO is not a game-module model in the Quake II sense. Quake II's server calls `CM_LoadMap` before `game->SpawnEntities`; `qcommon/cmodel.c` retains the BSP collision nodes, brushes, visibility, areas, area portals, and entity string. The game DLL receives the entity string and queries collision through imports. The renderer separately loads the same BSP into render-only surfaces. It does not give parsed BSP geometry to the game DLL.

Keep the equivalent ownership split here:

- `games/world-of-warcraft/common/world_wow.c` owns terrain and WMO collision/containment data required by authoritative simulation. `Wow_LoadMap` already initiates this path through `CM_LoadMap` before spawning entities.
- `games/world-of-warcraft/game/` owns semantic state: creatures, doors, triggers, transports, quests, and any authored WMO-contained gameplay objects. It queries `CM_*`; it must not parse `MOVT`, `MOBN`, or `MOBR` or retain GPU materials.
- `games/world-of-warcraft/renderer/wow/` independently owns WMO vertex buffers, materials, baked surface lighting, portal visibility, and render registration lifetime.

The useful analogy is one streamed world plus many reusable inline brush models, not thousands of independent Quake levels. An ADT supplies WMO instances (`MODF` transforms and bounds); a WMO root supplies shared groups; each group is comparable to a BSP area/room; `MOPT`/`MOPR` are comparable to area-portal connectivity; `MOBN`/`MOBR` are the collision acceleration structure. A global-WMO dungeon is the special case closest to one Quake level.

Quake II can load the whole BSP eagerly because a map is one bounded file. Azeroth cannot eagerly retain every ADT and WMO. Preserve the 16-ADT working set and shared path-keyed WMO collision cache, but move asset discovery out of the hot trace when streaming work is added: expose a `CM_WowPrepareArea(origin, radius)`-style collision-world operation, call it from authoritative game movement/spawn lifecycle, and incrementally load nearby ADT instance tables and referenced WMO collision models. `CM_WowFloorHeight` should then be a query over prepared state, with synchronous loading retained only until that streaming lifecycle exists. Do not expose raw WMO structs across the game/common boundary.

Naming should follow the existing engine convention: public collision-world operations use `CM_*`; private format loaders use `CMod_*`-style names if `world_wow.c` is split into a dedicated WMO collision loader; renderer registration remains `R_*`/`Wow_LoadWmoModel`. Do not reuse `R_LoadModel` or game `G_RegisterModel` for collision WMOs—their lifetimes and data are different.

### WMO Draw Performance

Root cause of 3fps regression: `Wow_QueueWmoDoodads` (`r_wowmap_objects.c`) called every frame for all WMO instances. Inside: `Wow_LoadDoodadModel` performs an O(n) `strcasecmp` linked-list scan; a second O(n) scan finds the `wowDoodadModel_t *group`. With 43 WMOs × ~100 doodad defs = 4300 lookups/frame at O(200), the profiler confirmed 860K `strcasecmp_l` calls/frame (78% of frame time).

Fixes applied:

1. **`def_groups` cache** (`wowWmoModel_t`): `wowDoodadModel_t **def_groups` populated on first `Wow_QueueWmoDoodads` call per model. Subsequent frames use O(1) pointer lookup; freed in `Wow_FreeWmoModels`.
2. **Move-to-front** in `Wow_LoadDoodadModel` (`r_wowmap_objects.c`): after a cache hit, the entry moves to the front of `wow_world.doodad_models`. Grass generates 465K calls with 14 unique models — after the first batch, each model hits at position 0.
3. **WMO precompute** (`r_wowmap.c`, `Wow_DrawTerrainAndWmos`): `Matrix3_normal`, `Wow_WmoContainsPoint`, `Wow_ComputeMoltContribution`, and group visibility count computed once per WMO, cached in a `wmo_cache[]` stack array (max 256 entries). Invisible WMOs (vis=0) excluded before GPU work; WMOs with no transparent batches skip pass 1.
4. **Constant uniforms hoisted**: `uUseWeightedBlend=0` and `uAlphaOrigin=(0,0)` set once before the WMO loop, not per-WMO per pass.

Measured results (M1 Pro, Eastern Kingdoms, Northshire): 3fps → 50fps after `def_groups` cache; → 60fps after WMO precompute + skip; 97fps with `r_wmos 0`; 120fps (vsync) with `r_wmos 0` + `r_grass 0`.

Remaining cost: 43 visible WMOs × 2 passes ≈ 6ms/frame; grass height atlas (465K instanced blades) ≈ 2ms/frame; together they exceed the 8.33ms 120Hz boundary → 60fps vsync. Path to 120fps: UBO-based per-WMO matrices (eliminates per-WMO `glUniform*` per pass) or portal-based group visibility (reduces groups drawn per frame).

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

## Sun Direction And Light Color

The 1.5 archive has no lighting DBCs **and no `.lit` files** (pre-1.9 zones stored
lighting in per-zone `.lit`, moved to `Light*.dbc` in 1.9), so there is no authored
sun path or light color to read. The client therefore synthesizes both:

- **Direction** — `Wow_SunDirection` (`r_wowmap_draw.c`) derives the sun from
  time-of-day (`Wow_DayFraction` = `tr.viewDef.time / WOW_DAY_LENGTH_MS`). It is
  WoWee's synthesized `directionalDir` (`data/WoWee/src/rendering/lighting_manager.cpp`
  `sampleLightParams`), negated and Y-up→Z-up swapped to engine axes. `day_frac`
  0=midnight, 0.25=dawn(sun in -X), 0.5=noon, 0.75=dusk(sun in +X). The direction is
  "toward the sun" (what the model/terrain shaders expect for `N·L`).
- **Color** — `WOW_LIGHT_AMBIENT_*` / `WOW_LIGHT_DIFFUSE_*` in
  `games/world-of-warcraft/common/ui_constants.h`, WoWee's documented no-DBC fallback
  tint (cool ambient, warm diffuse). Diffuse is halved from WoWee's `(1.0,0.95,0.85)`
  so `ambient + diffuse` stays ≤ 1.0 in the engine's non-HDR lighting.

Consumers: terrain (`r_wowmap_shader.c` vertex shader computes
`v_lighting = ambient + diffuse·N·L`), grass (`uSunDir.z` elevation), and M2 models
(`r_m2.c` `uLightDir`/`uLightColor`/`uLightAmbient`). For the authoritative chain that
*is* missing here, see WoWee: `Light.dbc` → `LightParams.dbc` →
`LightIntBand.dbc`/`LightFloatBand.dbc` (time-of-day color/fog bands, half-minutes
0–2879).

Baked terrain data in this 1.5 archive: MCNK has **no `MCCV`** (vertex color is
WotLK+; the parser reads it but the fallback is white, and its BGRA bytes still need
the RGBA swap `Wow_Color` performs for MOCV), but **does** carry `MCSH` (256 per ADT),
the 64×64×8-bit baked sun shadow map. Vanilla terrain lighting is
`texture × (ambient + diffuse·N·L) × MCSH`; the MCSH multiply is not yet applied.


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
