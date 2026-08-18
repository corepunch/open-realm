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
5. `R_LoadMapSegments` partitions the vertex grid into segments and bakes all GPU buffers.

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
- `GetTileRamps(tile) == 2 && IsMidRamp(tile) == 1` — exactly two ramp flags are set, but only one vertex is in the *mid-ramp* position (see the ramp section below). This avoids a triangle of ground leaking through the middle of a ramp.

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

Corner order is `[SW, NW, SE, NE]` (stored in `remap[] = {3,1,0,2}`). The resulting string like `"AABB"` selects the correct model variant.

### Ground Override

When a cliff tile is built, the renderer overwrites the `ground` index of all four corners to match the cliff type's `groundTile` entry from `CliffTypes.slk`. This ensures the ground layer paints the right texture on the flat surface at the top of the cliff.

### Ramp Placement

For ramp tiles the model is translated by ±`TILE_SIZE` in X or Y depending on which side has the lower terrain. The axis is chosen by comparing the model's bounding-box extents: if the model is wider in X the ramp runs east–west and the X offset is adjusted; otherwise it runs north–south and Y is adjusted.

### Height Snapping

Every vertex of the copied cliff model has its Z adjusted to match the actual heightmap:

```c
fz = vertex.z + baselevel * TILE_SIZE + GetAccurateHeightAtPoint(fx, fy) - HEIGHT_COR;
```

This grounds cliff models to terrain depressions or rises that occur within a tile.

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

| Target | Contents |
|--------|----------|
| `FOW_RT_IMMEDIATE` | Sight revealed this frame by all player-1 units |
| `FOW_RT_HISTORY` | Accumulated maximum visibility (ever-seen areas) |
| `FOW_RT_RESULT` | 50 % history + 50 % immediate — the texture bound to `uFogOfWar` |

Each entity with `radius >= 10` casts a circle of sight. A custom ray-cast shader (`vs_shadow` / `fs_shadow`) subtracts visibility blocked by other entities' silhouettes from the circle.

> **Gotcha — fog is currently disabled**: `R_GetFogOfWarTexture` always returns `tr.texture[TEX_WHITE]->texid`. The full fog pipeline runs every frame but its result is never bound to `uFogOfWar`. The commented-out code in `R_GetFogOfWarTexture` shows what to uncomment to re-enable it.

> **Gotcha — only team 1**: the fog loop filters `ent->team != 1`, meaning only the first player's units reveal the map. Multi-player fog of war is not yet implemented.

## Splat Rendering (`R_RenderSplat`)

`R_RenderSplat` draws a world-space decal (selection circles, ground effects) by re-using the static vertex buffer used for ground tiles. It:

1. Converts the splat's world-space AABB to heightmap grid coordinates.
2. Re-runs `R_MakeTile` over those tiles into the shared `aVertexBuffer` (with `texture = NULL` to skip UV assignment).
3. Computes UV per vertex from the splat's position and radius so the decal texture projects onto the terrain surface regardless of terrain slope.

> **Gotcha — shared static buffer**: `aVertexBuffer` in `r_war3map_ground.c` is a file-scope array also used during segment building. `R_RenderSplat` must not be called while segment building is in progress (it is safe at runtime because segments are built once at load time).

## Height and Normal Queries

`GetAccurateHeightAtPoint(sx, sy)` performs bilinear interpolation across the four heightmap vertices surrounding the world-space point `(sx, sy)`. This is used by cliff snapping and by `R_GetAPI().GetHeightAtPoint` which the server calls to place units on the ground.

`R_TraceLocation` casts a screen-space ray through the terrain by testing every pair of triangles in the full heightmap (`O(width × height)` triangle intersection tests). This is only called on mouse input events so the cost is acceptable for the current map sizes.

> **Gotcha — coordinate spaces**: the heightmap grid runs from `(0, 0)` to `(width-1, height-1)` in *tile space*, while world space has its origin at `map->center`. The conversion is `world.x = center.x + tile.x * TILE_SIZE`. `TILE_SIZE` is 128 world units.

## Texture Loading

Ground and cliff textures are located by looking up the tile or cliff ID string (four-character FourCC, e.g. `"Ldrt"`) in the `TerrainArt\Terrain.slk` or `TerrainArt\CliffTypes.slk` spreadsheets respectively.

For cliffs, the code first tries the tileset-specific variant `<texDir>\<tileset>_<texFile>.blp` and falls back to the generic `<texDir>\<texFile>.blp` if the first file does not exist.

Ground textures are cached globally in `g_groundTextures[]`, indexed by the ground layer index. Because this is a global array, **ground textures are not freed or re-initialised between maps**. Loading a second map will reuse textures from the first map if the layer count is the same.

## Key Constants

| Constant | Value | Defined in | Meaning |
|----------|-------|-----------|---------|
| `TILE_SIZE` | 128 | `games/warcraft-3/common/mapinfo.h` | World units per heightmap tile |
| `SEGMENT_SIZE` | 8 | `games/warcraft-3/common/mapinfo.h` | Tiles per segment side |
| `SHADOW_TEXSIZE` | 1024 | `r_local.h` | Shadow-map texture resolution |
| `DECODE_HEIGHT(v)` | `v / 4.0f` | `common.h` | Raw heightmap int → world units |
| `HEIGHT_COR` | (see `common.h`) | `common.h` | Cliff-level baseline correction |
| `WATER_HEIGHT_COR` | (see `common.h`) | `common.h` | Water-level baseline correction |
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
