# Map Renderer Architecture

The Warcraft III map renderer (`games/warcraft-3/renderer/w3m/`) is responsible for turning a parsed Warcraft III terrain file (`war3map.w3e`) into textured 3-D geometry on screen. It is compiled into the compound renderer library beside the generic engine renderer sources in `renderer/`; the server and client never touch the geometry directly.

## High-Level Pipeline

Each frame the renderer executes the following passes in order:

```
R_RenderFogOfWar   → update fog-of-war render targets
R_RenderShadowMap  → depth-only pass into RT_DEPTHMAP
R_RenderView       → colour pass: ground + cliffs, then entities,
                     then water (alpha) + particles
```

`R_DrawWorld` is called in both the shadow-map and the colour passes. It iterates every `MAPSEGMENT` and draws its `GROUND` and `CLIFF` layers. `R_DrawAlphaSurfaces` is called only in the colour pass and draws only the `WATER` layers, after depth-writing has been turned off.

## Data Loading

`R_RegisterMap` is the entry point called by the client when a map filename arrives via `svc_serverdata`:

1. `ri.FileExtract` copies the map MPQ from the main archive to a temp path (`TMP_MAP`).
2. `SFileOpenArchive` opens the extracted MPQ.
3. `FileReadWar3Map` reads `war3map.w3e` and populates a `WAR3MAP` struct:
   - Header, version, tileset character (`map->tileset`).
   - Ground tile ID list (`map->grounds`, length `map->num_grounds`) and cliff ID list (`map->cliffs`, length `map->num_cliffs`).
   - Map dimensions in vertices: `map->width` × `map->height`.
   - World-space centre offset `map->center` (loaded directly from the file).
   - Flat vertex array `map->vertices` — `map->width × map->height` entries, each `MAP_VERTEX_SIZE` bytes.
4. `R_FileReadShadowMap` reads `war3map.shd` and uploads it as an inverted greyscale texture to `tr.texture[TEX_SHADOWMAP]`.
5. `R_LoadMapSegments` partitions the vertex grid into segments. Cliff triangles remain in a map-wide CPU bake until `R_FinishCliffs` welds seams and uploads the per-segment/material ranges. Ground layers are built afterward.

## Segment System

The map is divided into axis-aligned *segments* of `SEGMENT_SIZE × SEGMENT_SIZE` tiles (8 × 8 by default, defined in `games/warcraft-3/common/mapinfo.h`). Every segment is a `MAPSEGMENT` node in the singly-linked list `g_mapSegments`.

```
Map grid (width-1) × (height-1) tiles
└── [(width-1)/8] × [(height-1)/8] segments
    └── each segment: MAPLAYER linked list
        ├── WATER layer       (built first, drawn last)
        ├── CLIFF layer(s)    (one per cliff ID in map->cliffs)
        └── GROUND layer(s)   (one per ground ID, highest index first)
```

Each segment stores a `BOX3 bbox` used for frustum culling in `R_DrawSegment`. If the bounding box is outside the current view frustum the entire segment — all layers — is skipped.

### Layer Render Order Within a Segment

`R_DrawSegment` receives a bitmask that selects which layer types to draw. When the first layer in the linked list is being drawn, blending is **disabled** (opaque base pass). All subsequent layers within the same segment draw with `GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA` blending so that higher ground textures alpha-blend over lower ones at tile boundaries.

Because `MAPLAYERTYPE_WATER` is masked out of `R_DrawWorld` and handled separately in `R_DrawAlphaSurfaces`, water tiles are always composited on top of everything else after depth-writing is disabled (`glDepthMask(GL_FALSE)`).

## Ground Layers (`r_war3map_ground.c`)

### Tile Selection

Each ground layer corresponds to one entry in `map->grounds[]`. The per-vertex `ground` field (0-based index into `map->grounds`) determines which texture is painted on each tile corner.

`GetTile(mv, layer)` returns a 4-bit index (0–15) describing the blend shape at the boundary between layer `layer` and the layer below it. For the bottom-most layer (layer 0) every tile is assigned index 15 (fully covered).

```c
// games/warcraft-3/renderer/w3m/r_war3map_utils.c
DWORD GetTile(LPCWAR3MAPVERTEX mv, DWORD ground) {
    if (ground == 0) return 15;
    return (mv[0].ground >= ground ? 4 : 0) +
           (mv[1].ground >= ground ? 8 : 0) +
           (mv[2].ground >= ground ? 1 : 0) +
           (mv[3].ground >= ground ? 2 : 0);
}
```

The 4-bit index selects one of 16 blend shapes laid out in a 4×4 sub-tile atlas inside the ground texture (each sub-tile is 64×64 texels).

### UV Mapping and Seam Bleeding

`SetTileUV` maps each tile quad to the correct atlas cell. To suppress texture bleeding at sub-tile borders the UV coordinates are nudged 5% inward toward the cell centre:

```c
vertices[i].texcoord.x = LerpNumber(vertices[i].texcoord.x,
                                    u * ((tile%4)+0.5) + ux, 0.05);
```

> **Quirk — tile 15 and double-wide textures**: when `tile == 15` and the texture is wider than it is tall (a "variation" sheet), the tile index is replaced with `mv->groundVariation` and the U coordinate is shifted by `ux = 0.5` to address the right half of the texture. The left half contains the normal set of 16 tiles and the right half contains variation tiles.

### Cliff and Ramp Tiles Are Skipped

`R_MakeTile` refuses to emit geometry for a tile if:

- `IsTileCliff(tile) && GetTileRamps(tile) < 4` — at least one vertex has a different `level` from the others, and fewer than all four vertices are ramp vertices. Cliff-face geometry is handled by the cliff layer instead.
- `GetTileRamps(tile) == 2` with exactly one `ramp && cliffVariation` corner — exactly two ramp flags are set, but only one vertex is in the *mid-ramp* position (see the ramp section below). This avoids a triangle of ground leaking through the middle of a ramp.

### Water Depth Tinting

Ground tiles that sit under water are tinted darker the deeper they are. `GetTileDepth(waterlevel, height)` returns a value in [0.05, 1] that is stored in the vertex `color` channel and multiplied with the sampled texture colour in the fragment shader. The colour channels are additionally brightened toward white proportionally to depth to simulate water scattering:

```c
#define WATER(INDEX) MakeColor(color[INDEX],
                               LerpNumber(color[INDEX], 1, 0.25f),
                               LerpNumber(color[INDEX], 1, 0.5f), 1)
```

## Cliff Layers (`r_war3map_cliffs.c`)

Cliffs are not tessellated from the heightmap. Instead, each cliff tile loads a pre-built MDX model from `Doodads\Terrain\<dir>\<dir><cfg>0.mdx` and copies its geoset vertices into the segment's GPU buffer.

### Cliff Configuration String

For each tile the four corner vertices are examined. Their relative height differences (0, 1, or 2 levels above the tile base) and ramp flags are encoded into a four-character string `cliffcfg`:

| Vertex relative level | Non-ramp | Ramp |
|-----------------------|----------|------|
| 0 | `A` | `L` |
| 1 | `B` | `H` |
| 2 | `C` | `X` |

Corner order is `[NW, NE, SE, SW]` (`r_cliff_corners[] = {1,0,2,3}`), while `GetTileVertices` stores
`[NE, NW, SE, SW]`. The resulting string like `"AABB"` selects the correct model variant.

`R_CliffTexture` selects the first authored cliff index in **SW, NW, NE, SE** order, independently of the model order.
W3E index 15 means no explicit cliff texture on that corner. When all four are 15, the cell selects cliff slot **1**. Checking only SW loses mixed-index faces; discarding all-15 cells loses the implicit second-set faces.

The per-map model cache keys the full asset path, not just four configuration letters. `Cliffs` and `CityCliffs`
(likewise `CliffTrans` and `CityCliffTrans`) contain different geometry with identical configuration suffixes.
The former four-letter key, introduced in May 2023, returned city models for dirt requests in Human02Interlude.
This was confirmed by logging requested directory versus the cached model's directory; it was not the courtyard border cause.

### Ground Override

Before baking ground layers, the renderer overrides the `ground` index around the **entire cliff model footprint** with
the cliff type's `groundTile` (or authored `upperTile`) from `CliffTypes.slk`. Ordinary cliffs cover one cell/four vertices;
ramps cover two cells/six vertices, including the low-side neighbour. Merely painting the first cell leaves the ground
texture border one tile short even when the cliff mesh itself covers the correct area.

### Ramp Placement

Native MDX positions and normals are rotated -90 degrees about Z: `(x,y,z) -> (y,-x,z)`, preserving the authored UVs.
The base translation is `(x*TILE_SIZE, y*TILE_SIZE)`. `R_CliffRampOffset` extends the two-cell model into the
low-side neighbour intentionally omitted by the ground baker. Native Y-long models are world east–west ramps;
native X-long models are world north–south ramps. Both rotated long axes span `[0,256]`.
East-high ramps shift X by -128; west-high ramps do not shift. North-high ramps shift Y by -128; south-high do not shift.

Do not replace this rotation with cyclically shifted filename letters. The assets are not exact rotated copies:
in the inspected archive `CityCliffTransHBAL0` has 31 vertices, while `CityCliffTransBALH0` has 32, with different
interior heights and UV details. The earlier SW-first filename plus X translation repaired coverage but chose the wrong mesh.

### Local Game.dll evidence

The user-provided `data/Warcraft3demo/Game.dll` (SHA-256
`286823c37a1083e91f07d040e46a9df7af4c4952e01fcbba460589bd4e297654`, preferred image base `0x6f000000`)
provides these version-specific disassembly anchors:

| Address | Observed contract |
|---------|-------------------|
| `0x6f1283e0` | Reads corners `(x,y+1), (x+1,y+1), (x+1,y), (x,y)`: NW, NE, SE, SW. |
| `0x6f128770` | Ramp filename construction and model lookup. |
| `0x6f4f9e28` | Sixteen 24-byte ramp records: two four-letter masks, origin X/Y, low-neighbour X/Y. |
| `0x6f128b90` | Loads -90 degrees (`0x6f4f9e20`) before the position/normal transform at `0x6f11dfb0`. |
| `0x6f11e200` | Heightmap deformation after rotation; adjusts Z, not an additional XY tile offset. |
| `0x6f0ea900` | Geometry cache hashes and compares the full filename. |

These are inspection addresses for this demo DLL, not universal offsets or a claim of matching every retail version.
For example, `radare2 -q -e scr.color=0 -c 'pD 512 @ 0x6f1283e0; q' data/Warcraft3demo/Game.dll` inspects corner order.
The local Warsmash reference (`environment/Terrain.java`, `realTileTexture`) independently explains the ground-border rule:
a vertex adjacent to either the high or low ramp cell receives that cliff's ground tile.

### Human02Interlude terrain-hole regression

`Maps/Campaign/Human02Interlude.w3m` is a standalone cinematic map and can be launched directly; completing Human02 with
the `win` cheat reaches the same map. The inspected local ROC data has 97x65 vertices, tileset X, 9 ground textures,
2 cliff textures, tile size 128, and world offset `(-7168,-3072)`. Coordinates below are zero-based SW cell coordinates.

The first investigation's targeted logs at ground rejection and cliff-model baking identified two geometry omissions:

- 73 of 2,284 cliff cells had SW cliff index 15 and were rejected by every cliff layer despite valid indices at other corners.
  Examples: `(10,29)` and `(65,35)` use cliff 1; `(75,25)` and `(77,9)` use cliff 0.
- The screenshot's courtyard holes remained after fixing that selection. E/W ramp models at `(36,33)` HAAL,
  `(36,34)` AHLA, and `(36,36)/(36,37)/(36,41)/(36,42)` HBAL/BHLA were baked across columns 35–36 instead of 36–37.
  The ground baker correctly skipped low-side cells `(37,33)`, `(37,34)`, `(37,36)`, `(37,37)`, `(37,41)`, `(37,42)`;
  the misplaced meshes never covered them. The foreground pair starts at world `(-2432,1536)` and `(-2432,1664)`;
  the background pair starts at `(-2432,2176)` and `(-2432,2304)`.

Those configuration names describe the **old SW-first implementation**. The incorrect X rule dates to `62e559a76`
(2023), not the recent ramp-classification change. Drawing the skipped ground does not repair the ramp holes.
The first correction fixed mesh coverage, but the user's retail comparison exposed a remaining one-tile purple-border error.

Follow-up logs and DLL inspection identified the native corner/rotation contract above and the missing ground override:

| High cell | Native city ramp | Low cell | Far-end ground vertices omitted by the old override |
|-----------|------------------|----------|----------------------------------------------------|
| `(36,36)` | `BALH0` | `(37,36)` | `(38,36)`, `(38,37)` |
| `(36,37)` | `HLAB0` | `(37,37)` | `(38,37)`, `(38,38)` |
| `(36,41)` | `BALH0` | `(37,41)` | `(38,41)`, `(38,42)` |
| `(36,42)` | `HLAB0` | `(37,42)` | `(38,42)`, `(38,43)` |

For `(36,36)`, the native rotated model is translated by `(36,36)` tiles and covers `[36,38] x [36,37]`.
Its cliff type `CXsq` selects `CityCliffTrans`, `Cliff1`, and ground `Xsqd` (index 4). Logs showed `(38,36)` and `(38,37)`
still carrying ground index 0, while the first four corners carried 4. Covering all six vertices moves the painted edge
to the ramp's actual low end, beneath Arthas, and likewise repairs the background edge.
The base and locale archives' W3E data were identical; the discrepancy was not different map input.

Windowed, bounded visual check (the built-in screenshot captures only the game drawable):

```sh
make -j8 build test-renderer-model test-renderer-shadows
build/bin/openwarcraft3 -data 'data/Warcraft III' +set vid_fullscreen 0 +set vid_native 0 +set vid_mode 4 +set skip_cutscene 1 +com_frame_limit 230 +map 'Maps/Campaign/Human02Interlude.w3m' +screenshot 180
```

Inspect the generated `screenshots/shot*.jpg` at the courtyard close-up: the foreground and background ramp edges must be
continuous, without black/sky-coloured gaps. Cinematic timing can vary; adjust the capture frame if necessary.
`renderer_terrain.cliff_texture_skips_non_cliff_corners` covers corner priority/sentinel handling, and
`renderer_terrain.ramp_footprints_cover_the_low_neighbour` checks sampled ramp levels and all four directions.
`renderer_terrain.cliff_baker_preserves_native_axes_uvs_and_ground_coverage` runs the actual baker on a synthetic grid,
checking model names, transformed positions/normals, untouched UVs, all six ramp ground vertices, and ordinary four-corner cliffs.
`renderer_terrain.cliff_cache_distinguishes_model_directories` exercises actual cache lookup/reuse and cleanup.
These tests require no retail MPQ data. Camera behavior is separate; see [cinematics](../cinematics.md).

Windowed follow-up captures before/after the ground override show both purple edges reaching their low-side endpoints;
rotating the native geometry alone did not fix that border. The cleaned build was captured again after the cache fix.

### Height Snapping

Every vertex of the copied cliff model has its Z adjusted to match the actual heightmap:

```c
fz = vertex.z + baselevel * TILE_SIZE + GetAccurateHeightAtPoint(fx, fy) - HEIGHT_COR;
```

This grounds cliff models to terrain depressions or rises that occur within a tile. `R_CliffGroundJoin` then finds edges bordering
emitted ground through `R_TileHasGround` and snaps vertices within half a tier to that exact ground edge. This includes ramp-adjusted
heights from `R_GetVertexPosition`; the decorative MDX lip alone is not the ground surface. Upward-facing join normals use the terrain
normal while vertical faces retain their authored direction.

### Shared Normal Welding and Undead04

Issue [#477](https://github.com/corepunch/open-realm/issues/477) exposed a second cliff-index case after the mixed-corner fix in
`5ae558b7`. The local ROC `Undead04.w3m` has 671 cliff cells with all four indices 15, plus 606 with explicit cliff 0. Its W3E cliff
list is `CLgr,CVdi`: the implicit cells need `CVdi`. Returning 15 skipped every material layer, leaving black holes beside the river
and waygates. Warsmash's `Terrain` corner selection also resolves the all-15 case to 1. The northeast waygate is at world
`(4032,-1856)`; a fog-disabled capture of the unchanged terrain confirms those faces are now present.

`renderer/r_cliff.h` shares the dynamic triangle bake and normal welder with SC2. The welder keys quantized XYZ, combines only
similarly-facing normals from distinct placements, and leaves stacked, opposing and zero normals alone. Expanded triangles must
count each placement/authored normal once: counting repeated indices biases opposite sides of a seam differently. WC3 defers uploads
until all segments and cliff materials have been baked, so seams across segment/material boundaries participate in the same pass.
Native MDX axes, ramp footprints and UVs remain WC3-owned; SC2's M3 placement/catalog rules remain SC2-owned.

Blight preserves corners owned by any omitted cliff/transition cell, including low ramp neighbours. Ground, splats, snapping and
Blight share `R_TileHasGround`; see [Blight presentation](../blight.md) and [SC2 ramps and welding](../../starcraft-2/terrain-and-world-rendering.md).

Regression coverage in `test_renderer_model` includes `undead04_implicit_cliff_and_ground_join`,
`blight_preserves_cliff_corners`, and `cliff_welding_ignores_triangle_multiplicity_and_stacked_faces`. Run `make test-renderer-model`
and `make test`. A bounded retail-data load is:

```sh
build/bin/openwarcraft3 -data "data/Warcraft III" +vid_hidden 1 +map Maps/Campaign/Undead04.w3m +com_fast_forward 1 +com_frame_limit 120
```

For camera commands with negative coordinates, use a config passed through `+exec` (for example `camera move 4032 -1856`);
the startup argument parser treats a separate negative argument as a command-line option. Fog and the opening cinematic must be
disabled for a visual inspection of that location; automated geometry tests do not need either runtime change.

## Water Layer (`r_war3map_water.c`)

Water tiles are flat quads at the `waterlevel` height of each vertex (per-vertex because the water surface can vary across a tile). Only tiles where at least one corner has the `water` flag set — and no corner has the `mapedge` flag — are emitted.

Water uses a fixed `ReplaceableTextures\Water\Water12.blp` texture shared across the whole map. UVs are generated by `WATER_SCALE`:

```c
#define WATER_SCALE(x,y) (((x%3)+y)/3.0)
```

> **Quirk — WATER_SCALE tiling**: the `x%3` term creates a 3-tile staggered repeat that prevents an obvious grid pattern while keeping UV coordinates cheap to compute. The tile coordinates passed in are the raw grid integers so the texture tiles every 3 units in X and every 1 unit in Y.

Water opacity is computed per-vertex. A tile that is at most 50 units below the water surface gets a linearly-ramped alpha (0–0.5). Tiles above the water surface have alpha 0 (the quad still exists but is invisible).

## Shadow Map Pass

`R_RenderShadowMap` uses the same `SHADER_DEFAULT` program but passes `tr.viewDef.lightMatrix` as the view-projection matrix. OpenGL depth-writes go into `RT_DEPTHMAP` (a 1024×1024 depth texture). The colour pass reads this texture to compute `get_shadow()` in the fragment shader.

The static baked shadow texture (`war3map.shd`) is bound to texture unit 1 during `R_DrawWorld`. It stores pre-computed shadow data from the Warcraft III World Editor at 4× heightmap resolution ((`width`-1)×4 × (`height`-1)×4 texels).

> **Gotcha — two shadow inputs**: the fragment shader currently reads both the runtime depth map (`uShadowmap`) and the static baked shadow map (`TEX_SHADOWMAP`) via the same `uShadowmap` uniform slot. The baked shadow map is bound at load time; the runtime depth map is bound in `R_RenderView` via `glBindTexture(GL_TEXTURE2D, tr.rt[RT_DEPTHMAP]->texture)`. Whichever binding wins at draw time determines the shadow look.

## Fog of War (`r_fogofwar.c`)

The fog of war uses three off-screen render targets, all sized at (`width`-1)×4 × (`height`-1)×4 texels (same resolution as the baked shadow map):

Server snapshot visibility and client fog shading have separate ownership. Mobile units require current server-side vision;
buildings and map doodads/destructibles enter snapshots after their cell is explored and remain while the client fog mask
shrouds them. Doodads/destructibles carry `SVF_STATIC_SCENERY` so the server can use explored rather than current vision
without per-frame metadata lookups. Never send static scenery unconditionally: ROC Human02 has 2,299 such entities, which
saturates the 1,024-entity snapshot and exposes unexplored waterfalls through black fog.

| Target | Contents |
|--------|----------|
| `FOW_RT_IMMEDIATE` | Sight revealed this frame by all player-1 units |
| `FOW_RT_HISTORY` | Accumulated maximum visibility (ever-seen areas) |
| `FOW_RT_RESULT` | 50 % history + 50 % immediate — the texture bound to `uFogOfWar` |

Each entity with `radius >= 10` casts a circle of sight. A custom ray-cast shader (`vs_shadow` / `fs_shadow`) subtracts visibility blocked by other entities' silhouettes from the circle.

`R_GetFogOfWarTexture` prefers the server-authored `fow_resources.network` texture. The older render-target raycast is used
only when no network texture exists; white is reserved for `RDF_NOFOG`, `RDF_NOWORLDMODEL`, or unavailable fog resources.

World particles sample the same texture matrix and unit-2 fog mask as model geosets. This matters for
`Doodads\\Terrain\\CliffDoodad\\Waterfall\\Waterfall.mdx`: the asset has two `PRE2` emitters and no geosets, so applying
FOW only in the model shader leaves the complete waterfall visible through otherwise black fog. UI/model scenes receive
the white FOW texture through `RDF_NOWORLDMODEL`, preserving their particle presentation without a parallel shader path.

> **Gotcha — only team 1**: the fog loop filters `ent->team != 1`, meaning only the first player's units reveal the map. Multi-player fog of war is not yet implemented.

## Splat Rendering (`R_RenderSplat`)

`R_RenderSplat` draws a world-space decal (selection circles, ground effects) by re-using the static vertex buffer used for ground tiles. It:

1. Converts the splat's world-space AABB to heightmap grid coordinates.
2. Re-runs `R_MakeTile` over those tiles into the shared `aVertexBuffer` (with `texture = NULL` to skip UV assignment).
3. Computes UV per vertex from the splat's position and radius so the decal texture projects onto the terrain surface regardless of terrain slope.

> **Gotcha — shared static buffer**: `aVertexBuffer` in `r_war3map_ground.c` is a file-scope array also used during segment building. `R_RenderSplat` must not be called while segment building is in progress (it is safe at runtime because segments are built once at load time).

`R_RenderRectSplat` is the immediate (single-decal) entry point. For many same-shader decals — unit shadows — `R_DrawEntityShadows` uses the batched `R_BeginSplatBatch` / `R_AddRectSplat` / `R_EndSplatBatch` API to collapse hundreds of splats into one vertex-buffer upload + draw per distinct texture. See [performance](../performance.md).

## Height and Normal Queries

`GetAccurateHeightAtPoint(sx, sy)` performs bilinear interpolation across the four heightmap vertices surrounding the world-space point `(sx, sy)`. This is used by cliff snapping and by `R_GetAPI().GetHeightAtPoint` which the server calls to place units on the ground.

`R_TraceLocation` casts a screen-space ray through the terrain by testing every pair of triangles in the full heightmap (`O(width × height)` triangle intersection tests). This is only called on mouse input events so the cost is acceptable for the current map sizes.

> **Gotcha — coordinate spaces**: the heightmap grid runs from `(0, 0)` to `(width-1, height-1)` in *tile space*, while world space has its origin at `map->center`. The conversion is `world.x = center.x + tile.x * TILE_SIZE`. `TILE_SIZE` is 128 world units.

## Texture Loading

### Undead04 cliff material verification

The locally inspected ROC Undead04 W3E has tileset `L`, `custom=1`, ground palette
`Vdrt,Vdrr,Vcbp,Lgrs,Lgrd,Ybtl,Yrtl`, and cliff palette `CLgr,CVdi`. The 606 cliff cells with an explicit
index 0 select `CLgr`; the 671 all-15 cliff cells select implicit slot 1, `CVdi`. For example, cell `(93,54)`
near the northeast waygate has NW/NE/SE/SW levels `[4,4,6,6]`, four ground indices 3, and four cliff indices 15.
`R_MakeCliff` replaces the adjoining ground with the selected cliff row's `groundTile` before ground baking.

| Corner selection | Authored cliff slot | ROC/TFT SLK row | Loaded wall image on tileset L | Adjoining ground |
| --- | --- | --- | --- | --- |
| Explicit 0 | 0 | `CLgr` | `ReplaceableTextures/Cliff/Cliff1.blp` | `Lgrs` (ground index 3) |
| All four 15 | 1 | `CVdi` | `ReplaceableTextures/Cliff/Cliff0.blp` | `Vdrt` (ground index 0) |

Both archive versions give these rows the same `texFile` and `groundTile` values. The locally installed archives
have no `L_Cliff0.blp` or `L_Cliff1.blp`, so the existing texture lookup resolves the base images. `Cliff0` contains
the layered rock face and brown rim visible in the supplied photos. The local nested `L.mpq` contains splat
overrides, not cliff images. Its listfile needs PKWARE decompression; inspect it with StormLib if `mpqtool` reports
unsupported compression mask 0x08. `blp2jpg` does not support these JPEG-compressed BLP1 images; the renderer's
decoder handles their shared JPEG header plus mip payload.

Sorting the stored cliff palette into ground-palette order would swap both materials, making the river grass
instead of dirt. The all-15 fix in `639ce3a4` is the relevant correction; the
[W3E reference](../file-docs/w3e.md) distinguishes editor behavior from runtime.

`renderer_terrain.undead04_cliff_material_and_ground_use_authored_slots` drives `R_BuildMapSegmentCliffs` and
`R_FinishCliffs` with both corner cases and the native palette/SLK values. It checks that only the expected layer
is emitted, that its actual texture request matches the table above, and that only cliff-footprint ground corners
change. Asset lookup and GPU upload are mocked; selection, geometry baking and ground replacement are production code.
This complements the existing implicit-cliff geometry/join test and locks the authored-slot material contract.
It does not exercise W3E file loading or real image decoding.

A framebuffer check on `2b673bb1` at the northeast waygate, camera `(4032,-1856)`, showed the layered rock and
brown cliff rim matching the supplied reference photo. No additional texture mismatch was reproduced there.
The diagnostic copy retained the native `war3map.w3e` unchanged and disabled `InitCustomTriggers` and
`RunInitializationTriggers` so the opening cinematic could not override the camera. A JASS helper disabled fog,
set noon, suspended time of day, and called `ResetToGameCamera(0)` before `camera move 4032 -1856` and
`screenshot 30`. The run was bounded with `+com_frame_limit 150`; mission behavior was not under test.

During this local capture, map registration exceeded `CL_TIMEOUT_MSEC` and disconnected the client before
the scene could render. A one-shot conditional LLDB breakpoint at `CL_CheckTimeout` reset
`cl_last_packet_time = cl_realtime` only when the timeout condition became true, then continued. This was a
capture-only workaround; no timeout or renderer production code was changed. A normal-launch loading timeout
is a separate issue from the material selection verified here.

Ground and cliff textures are located by looking up the tile or cliff ID string (four-character FourCC, e.g. `"Ldrt"`) in the `TerrainArt\Terrain.slk` or `TerrainArt\CliffTypes.slk` spreadsheets respectively.

For cliffs, the code first tries the tileset-specific variant `<texDir>\<tileset>_<texFile>.blp` and falls back to the generic `<texDir>\<texFile>.blp` if the first file does not exist.

Ground textures are cached in `g_groundTextures[]`, indexed by ground layer. `_W3M_ClearMap` resets the ground
and cliff choices before the next map is registered; texture storage remains owned by the renderer cache.

## Key Constants

| Constant | Value | Defined in | Meaning |
|----------|-------|-----------|---------|
| `TILE_SIZE` | 128 | `games/warcraft-3/common/mapinfo.h` | World units per heightmap tile |
| `SEGMENT_SIZE` | 8 | `games/warcraft-3/common/mapinfo.h` | Tiles per segment side |
| `SHADOW_TEXSIZE` | 1024 | `r_local.h` | Shadow-map texture resolution |
| `DECODE_HEIGHT(v)` | `(v - 0x2000) / 4` | `games/warcraft-3/common/terrain.h` | Raw W3E heightmap int → world units |
| `HEIGHT_COR` | `TILE_SIZE * 2` | `games/warcraft-3/common/terrain.h` | Cliff-level baseline correction |
| `WATER_HEIGHT_COR` | `80` | `games/warcraft-3/common/terrain.h` | Water-level baseline correction |
| `MAX_MAP_LAYERS` | 16 | `r_war3map_ground.c` | Max ground texture slots |

## Key Files

| File | Purpose |
|------|---------|
| `games/warcraft-3/renderer/w3m/r_war3map.c` | Map loading, segment building, `R_DrawWorld`, `R_DrawAlphaSurfaces` |
| `games/warcraft-3/renderer/w3m/r_war3map.h` | `MAPSEGMENT` / `MAPLAYER` structs, public declarations |
| `games/warcraft-3/renderer/w3m/r_war3map_ground.c` | Ground and ramp tile geometry, splat rendering, height/normal queries |
| `games/warcraft-3/renderer/w3m/r_war3map_cliffs.c` | Cliff model loading, vertex baking, height snapping |
| `games/warcraft-3/renderer/w3m/r_war3map_water.c` | Water tile geometry and opacity |
| `games/warcraft-3/renderer/w3m/r_war3map_utils.c` | Shared helpers: `GetTile`, `SetTileUV`, `GetTileDepth`, vertex accessors |
| `renderer/r_fogofwar.c` | Fog-of-war render targets and ray-cast sight shader |
| `renderer/r_main.c` | `R_RenderShadowMap`, `R_RenderView`, `R_RenderFrame` |
| `renderer/r_shader.c` | GLSL sources for `SHADER_DEFAULT`, `SHADER_UI`, `SHADER_SPLAT` |
| `games/warcraft-3/common/mapinfo.h` | `TILE_SIZE`, `SEGMENT_SIZE`, `WAR3MAP` struct layout |
