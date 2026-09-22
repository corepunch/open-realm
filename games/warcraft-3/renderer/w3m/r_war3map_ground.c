#include "r_war3map.h"

#define MAX_MAP_LAYERS 16
#define WATER(INDEX) \
MakeColor(color[INDEX], LerpNumber(color[INDEX], 1, 0.25f), LerpNumber(color[INDEX], 1, 0.5f), 1)

LPCTEXTURE g_groundTextures[MAX_MAP_LAYERS] = { NULL };

void R_ResetGroundTextures(void) {
    memset(g_groundTextures, 0, sizeof(g_groundTextures));
}

#define GROUND_VERTEX_BUFFER_CAPACITY (SEGMENT_SIZE * SEGMENT_SIZE * 6)

static VERTEX ground_vertex_buffer[GROUND_VERTEX_BUFFER_CAPACITY];
static LPVERTEX ground_current_vertex = NULL;

VECTOR3 R_GetVertexPosition(LPCWAR3MAP map, DWORD x, DWORD y, BOOL useLevel) {
    LPCWAR3MAPVERTEX vert = GetWar3MapVertex(map, x, y);
    FLOAT level = useLevel ? vert->level * TILE_SIZE - HEIGHT_COR : 0;
    if (useLevel && vert->ramp && vert->cliffVariation) {
        level += 0.5 * TILE_SIZE;
    }
    FLOAT z = DECODE_HEIGHT(vert->accurate_height) + level;
    return (VECTOR3) {
        .x = map->center.x + x * TILE_SIZE,
        .y = map->center.y + y * TILE_SIZE,
        .z = z,
    };
}

static FLOAT r_war3_normal_height(LPCVOID data, DWORD x, DWORD y) {
    return R_GetVertexPosition(data, x, y, false).z;
}

VECTOR3 R_GetVertexNormal(LPCWAR3MAP map, DWORD x, DWORD y) {
    TERRAINNORMALS grid = { map, r_war3_normal_height, map->width, map->height, TILE_SIZE };

    return R_TerrainGridNormal(&grid, x, y);
}

static void R_MakeTile(LPCWAR3MAP map, DWORD x, DWORD y, DWORD ground, LPCTEXTURE texture) {
    struct War3MapVertex tile[4];
    GetTileVertices(x, y, map, tile);
    int _tile = GetTile(tile, ground);
    if (!_tile || !R_TileHasGround(tile))
        return;
    
    VECTOR3 const p[] = {
        R_GetVertexPosition(map, x, y, true),
        R_GetVertexPosition(map, x + 1, y, true),
        R_GetVertexPosition(map, x + 1, y + 1, true),
        R_GetVertexPosition(map, x, y + 1, true),
    };

    VECTOR3 const n[] = {
        R_GetVertexNormal(map, x, y),
        R_GetVertexNormal(map, x + 1, y),
        R_GetVertexNormal(map, x + 1, y + 1),
        R_GetVertexNormal(map, x, y + 1),
    };

    FLOAT const waterlevel[] = {
        GetWar3MapVertexWaterLevel(&tile[3]),
        GetWar3MapVertexWaterLevel(&tile[2]),
        GetWar3MapVertexWaterLevel(&tile[0]),
        GetWar3MapVertexWaterLevel(&tile[1]),
    };

    FLOAT const color[] = {
        GetTileDepth(waterlevel[0], p[0].z),
        GetTileDepth(waterlevel[1], p[1].z),
        GetTileDepth(waterlevel[2], p[2].z),
        GetTileDepth(waterlevel[3], p[3].z),
    };

    struct vertex geom[] = {
        { .position = p[0], .texcoord = {0, 0}, .normal = n[0], .color = WATER(0), },
        { .position = p[1], .texcoord = {1, 0}, .normal = n[1], .color = WATER(1), },
        { .position = p[2], .texcoord = {1, 1}, .normal = n[2], .color = WATER(2), },
        { .position = p[0], .texcoord = {0, 0}, .normal = n[0], .color = WATER(0), },
        { .position = p[2], .texcoord = {1, 1}, .normal = n[2], .color = WATER(2), },
        { .position = p[3], .texcoord = {0, 1}, .normal = n[3], .color = WATER(3), },
    };
    
    if (texture) {
        SetTileUV(GetWar3MapVertex(map, x, y), _tile, geom, texture);
    }

    memcpy(ground_current_vertex, geom, sizeof(geom));
    ground_current_vertex += sizeof(geom) / sizeof(VERTEX);
}

static BOOL R_TileAcceptsSplat(LPCWAR3MAP map, DWORD x, DWORD y) {
    struct War3MapVertex tile[4];

    GetTileVertices(x, y, map, tile);
    return GetTile(tile, 0) && R_TileHasGround(tile);
}

static void R_BuildSplatQuad(LPCWAR3MAP map, DWORD x, DWORD y, LPCVECTOR2 mins, FLOAT width, FLOAT height, COLOR32 color, LPVERTEX geom) {
    VECTOR3 const p[] = {
        R_GetVertexPosition(map, x, y, true),
        R_GetVertexPosition(map, x + 1, y, true),
        R_GetVertexPosition(map, x + 1, y + 1, true),
        R_GetVertexPosition(map, x, y + 1, true),
    };
    VECTOR2 const uv[] = {
        { (p[0].x - mins->x) / width, 1 - (p[0].y - mins->y) / height },
        { (p[1].x - mins->x) / width, 1 - (p[1].y - mins->y) / height },
        { (p[2].x - mins->x) / width, 1 - (p[2].y - mins->y) / height },
        { (p[3].x - mins->x) / width, 1 - (p[3].y - mins->y) / height },
    };
    VECTOR3 const normal = { 0, 0, 1 };
    VERTEX const quad[] = {
        { .position = p[0], .texcoord = uv[0], .normal = normal, .color = color },
        { .position = p[1], .texcoord = uv[1], .normal = normal, .color = color },
        { .position = p[2], .texcoord = uv[2], .normal = normal, .color = color },
        { .position = p[0], .texcoord = uv[0], .normal = normal, .color = color },
        { .position = p[2], .texcoord = uv[2], .normal = normal, .color = color },
        { .position = p[3], .texcoord = uv[3], .normal = normal, .color = color },
    };
    memcpy(geom, quad, sizeof(quad));
}

static void R_MakeSplatTile(LPCWAR3MAP map, DWORD x, DWORD y, LPCVECTOR2 mins, FLOAT width, FLOAT height, COLOR32 color) {
    VERTEX geom[6];
    R_BuildSplatQuad(map, x, y, mins, width, height, color, geom);
    memcpy(ground_current_vertex, geom, sizeof(geom));
    ground_current_vertex += sizeof(geom) / sizeof(VERTEX);
}

static void R_MakeBlightTile(LPCWAR3MAP map, DWORD x, DWORD y, LPCVECTOR2 mins, FLOAT width, FLOAT height, DWORD blight_tile) {
    VERTEX geom[6];
    R_BuildSplatQuad(map, x, y, mins, width, height, COLOR32_WHITE, geom);
    /* Mixed tiles use the atlas's left-hand alpha mask selected by the
     * four-corner bitmask.  Tile 15 is special in SetTileUV: it selects
     * the right-hand opaque variation, which is correct only when every
     * corner is Blighted. */
    SetTileUV(GetWar3MapVertex(map, x, y), blight_tile, geom, R_BlightTexture());
    memcpy(ground_current_vertex, geom, sizeof(geom));
    ground_current_vertex += sizeof(geom) / sizeof(VERTEX);
}

static void R_FlushSplatBatch(void) {
    DWORD num_vertices = (DWORD)(ground_current_vertex - ground_vertex_buffer);
    if (num_vertices == 0) {
        return;
    }

    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(VERTEX) * num_vertices, ground_vertex_buffer, GL_STREAM_DRAW);
    R_StatsDraw(GL_TRIANGLES, num_vertices, 1);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
    ground_current_vertex = ground_vertex_buffer;
}

/* Bind the splat shader/VAO/VBO and upload the per-view uniforms once.  All
 * splats in a batch share this state; only the texture and per-tile geometry
 * vary, so re-issuing it per splat was pure overhead. */
static LPCTEXTURE g_splat_texture;
static splat_shader_t *g_splat_shader;

static void R_SetupSplatState(LPCTEXTURE texture, splat_shader_t *shader) {
    MATRIX4 mModelMatrix;

    Matrix4_identity(&mModelMatrix);
    g_splat_texture = texture;
    g_splat_shader = shader;
    R_BindTexture(texture, 0);
    R_SetTextureWrap(texture, false, false); /* splats are decals, so repeating the source texture smears the edges */

    shader->state.viewProjection = tr.viewDef.viewProjectionMatrix;
    shader->state.model = mModelMatrix;
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    ground_current_vertex = ground_vertex_buffer;
    R_ApplyShader(shader);
}

/* Emit terrain-conforming tiles for one splat rect into the shared buffer,
 * flushing to the GPU only when the buffer fills. */
static void R_GenerateSplatTiles(LPCVECTOR2 mins, LPCVECTOR2 maxs, COLOR32 color) {
    int x_start, x_end;
    int y_start, y_end;

    FLOAT const width = maxs->x - mins->x;
    FLOAT const height = maxs->y - mins->y;
    if (width <= 0 || height <= 0) {
        return;
    }

    x_start = MAX(0, (int)floor((mins->x - tr.world->center.x) / TILE_SIZE));
    y_start = MAX(0, (int)floor((mins->y - tr.world->center.y) / TILE_SIZE));
    x_end = MIN((int)tr.world->width - 1, (int)ceil((maxs->x - tr.world->center.x) / TILE_SIZE));
    y_end = MIN((int)tr.world->height - 1, (int)ceil((maxs->y - tr.world->center.y) / TILE_SIZE));

    if (x_start >= x_end || y_start >= y_end) {
        return;
    }

    for (int x = x_start; x < x_end; x++) {
        for (int y = y_start; y < y_end; y++) {
            if (!R_TileAcceptsSplat(tr.world, (DWORD)x, (DWORD)y)) {
                continue;
            }
            if ((ground_current_vertex - ground_vertex_buffer) + 6 > GROUND_VERTEX_BUFFER_CAPACITY) {
                R_FlushSplatBatch();
            }
            R_MakeSplatTile(tr.world, (DWORD)x, (DWORD)y, mins, width, height, color);
        }
    }
}

void R_BeginSplatBatch(splat_shader_t *shader) {
    g_splat_shader = shader;
    g_splat_texture = NULL;
    ground_current_vertex = ground_vertex_buffer;
}

void R_AddRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, LPCTEXTURE texture, COLOR32 color) {
    if (!tr.world || !texture) {
        return;
    }
    if (texture != g_splat_texture) {
        R_FlushSplatBatch();
        R_SetupSplatState(texture, g_splat_shader);
    }
    R_GenerateSplatTiles(mins, maxs, color);
}

void R_EndSplatBatch(void) {
    R_FlushSplatBatch();
    R_Call(glDepthMask, GL_TRUE);
}

typedef struct {
    BYTE *active;
    BYTE *corners;
    DWORD width, height;
    DWORD generation;
    VECTOR2 origin;
    FLOAT cell_size;
} blightTileCache_t;

static blightTileCache_t blight_tiles;
static MAPLAYER blight_layer;
static BOOL blight_layer_valid;
static DWORD blight_layer_generation = ~0u;

static BOOL R_BlightTileCacheUpdate(viewDef_t const *view);

static void R_ResetBlightLayer(void) {
    if (blight_layer.buffer) R_ReleaseVertexArrayObject((LPBUFFER)blight_layer.buffer);
    memset(&blight_layer, 0, sizeof(blight_layer));
    blight_layer_valid = false;
    blight_layer_generation = ~0u;
}

void R_ResetBlightCache(void) {
    R_ResetBlightLayer();
    SAFE_DELETE(blight_tiles.active, ri.MemFree);
    SAFE_DELETE(blight_tiles.corners, ri.MemFree);
    memset(&blight_tiles, 0, sizeof(blight_tiles));
}

static DWORD R_BlightEmittedTiles(void) {
    DWORD count = 0, stride = blight_tiles.width + 1;
    FOR_LOOP(ty, blight_tiles.height) FOR_LOOP(tx, blight_tiles.width) {
        DWORD const blight_tile = TerrainMask_TileMask(blight_tiles.corners, stride, tx, ty);
        VECTOR2 mins = { blight_tiles.origin.x + tx * TILE_SIZE, blight_tiles.origin.y + ty * TILE_SIZE };
        int const map_x = (int)floorf((mins.x - tr.world->center.x) / TILE_SIZE);
        int const map_y = (int)floorf((mins.y - tr.world->center.y) / TILE_SIZE);
        if (!blight_tiles.active[tx + ty * blight_tiles.width] || !blight_tile ||
            map_x < 0 || map_y < 0 || map_x >= (int)tr.world->width - 1 ||
            map_y >= (int)tr.world->height - 1 ||
            !R_TileAcceptsSplat(tr.world, (DWORD)map_x, (DWORD)map_y)) continue;
        count++;
    }
    return count;
}

void R_UpdateBlightLayer(void) {
    LPVERTEX vertices;
    DWORD count = 0, tiles;
    DWORD stride = blight_tiles.width + 1;

    if (!R_BlightTileCacheUpdate(&tr.viewDef) || !R_BlightTexture()) {
        R_ResetBlightLayer();
        return;
    }
    if (blight_layer_valid && blight_layer.texture == R_BlightTexture() &&
        blight_layer.num_vertices && blight_layer_generation == tr.viewDef.terrain_mask.generation)
        return;
    tiles = R_BlightEmittedTiles();
    if (!tiles) { R_ResetBlightLayer(); return; }
    vertices = ri.MemAlloc(sizeof(*vertices) * tiles * 6);
    if (!vertices) {
        fprintf(stderr, "R_UpdateBlightLayer: failed to allocate %u-tile Blight layer\n", (unsigned)tiles);
        R_ResetBlightLayer();
        return;
    }
    ground_current_vertex = vertices;
    FOR_LOOP(ty, blight_tiles.height) FOR_LOOP(tx, blight_tiles.width) {
        DWORD const blight_tile = TerrainMask_TileMask(blight_tiles.corners, stride, tx, ty);
        VECTOR2 mins = { blight_tiles.origin.x + tx * TILE_SIZE, blight_tiles.origin.y + ty * TILE_SIZE };
        int const map_x = (int)floorf((mins.x - tr.world->center.x) / TILE_SIZE);
        int const map_y = (int)floorf((mins.y - tr.world->center.y) / TILE_SIZE);
        if (!blight_tiles.active[tx + ty * blight_tiles.width] || !blight_tile ||
            map_x < 0 || map_y < 0 || map_x >= (int)tr.world->width - 1 ||
            map_y >= (int)tr.world->height - 1 ||
            !R_TileAcceptsSplat(tr.world, (DWORD)map_x, (DWORD)map_y)) continue;
        R_MakeBlightTile(tr.world, (DWORD)map_x, (DWORD)map_y, &mins, TILE_SIZE, TILE_SIZE, blight_tile);
    }
    count = (DWORD)(ground_current_vertex - vertices);
    R_ResetBlightLayer();
    if (count) {
        blight_layer.texture = R_BlightTexture();
        blight_layer.type = MAPLAYERTYPE_GROUND;
        blight_layer.num_vertices = count;
        blight_layer.buffer = R_MakeVertexArrayObject(vertices, count);
        blight_layer_valid = blight_layer.buffer != NULL;
        blight_layer_generation = tr.viewDef.terrain_mask.generation;
    }
    ri.MemFree(vertices);
    ground_current_vertex = NULL;
}

void R_DrawBlightLayer(void) {
    if (!blight_layer_valid || !blight_layer.buffer) return;
    R_BindTexture(blight_layer.texture, 0);
    R_ApplyShader(&tr.shader_default);
    R_DrawBuffer(blight_layer.buffer, blight_layer.num_vertices);
}

static BOOL R_BlightTileCacheUpdate(viewDef_t const *view) {
    DWORD cells_per_tile;
    DWORD old_width = blight_tiles.width, old_height = blight_tiles.height;
    DWORD new_width, new_height;

    if (!view || !view->terrain_mask.cells || !view->terrain_mask.width ||
        !view->terrain_mask.height || view->terrain_mask.cell_size <= 0.0f)
        return false;
    cells_per_tile = (DWORD)floorf(TILE_SIZE / view->terrain_mask.cell_size + 0.5f);
    if (!cells_per_tile || fabsf(cells_per_tile * view->terrain_mask.cell_size - TILE_SIZE) > 0.01f)
        return false;
    new_width = (view->terrain_mask.width + cells_per_tile - 1) / cells_per_tile;
    new_height = (view->terrain_mask.height + cells_per_tile - 1) / cells_per_tile;
    if (!blight_tiles.active || old_width != new_width || old_height != new_height ||
        blight_tiles.origin.x != view->terrain_mask.origin.x ||
        blight_tiles.origin.y != view->terrain_mask.origin.y ||
        blight_tiles.cell_size != view->terrain_mask.cell_size) {
        blight_tiles.width = new_width;
        blight_tiles.height = new_height;
        SAFE_DELETE(blight_tiles.active, ri.MemFree);
        SAFE_DELETE(blight_tiles.corners, ri.MemFree);
        blight_tiles.active = ri.MemAlloc(blight_tiles.width * blight_tiles.height);
        blight_tiles.corners = ri.MemAlloc((blight_tiles.width + 1) * (blight_tiles.height + 1));
        if (!blight_tiles.active || !blight_tiles.corners) {
            fprintf(stderr, "R_UpdateBlightLayer: failed to allocate %ux%u tile cache\n",
                    (unsigned)blight_tiles.width, (unsigned)blight_tiles.height);
            SAFE_DELETE(blight_tiles.active, ri.MemFree);
            SAFE_DELETE(blight_tiles.corners, ri.MemFree);
            blight_tiles.width = blight_tiles.height = 0;
            return false;
        }
        memset(blight_tiles.active, 0, blight_tiles.width * blight_tiles.height);
        memset(blight_tiles.corners, 0, (blight_tiles.width + 1) * (blight_tiles.height + 1));
        blight_tiles.origin = view->terrain_mask.origin;
        blight_tiles.cell_size = view->terrain_mask.cell_size;
        blight_tiles.generation = ~0u;
    }
    if (blight_tiles.generation == view->terrain_mask.generation) return true;
    BLIGHT_LOG("cache generation=%u tiles=%ux%u\n",
            (unsigned)view->terrain_mask.generation,
            (unsigned)blight_tiles.width, (unsigned)blight_tiles.height);
    FOR_LOOP(cy, blight_tiles.height + 1) FOR_LOOP(cx, blight_tiles.width + 1) {
        int x = (int)lroundf((blight_tiles.origin.x - tr.world->center.x) / TILE_SIZE) + cx;
        int y = (int)lroundf((blight_tiles.origin.y - tr.world->center.y) / TILE_SIZE) + cy;
        /* Blight changes eligible ground corners, never the terrain type owned by a cliff mesh. */
        blight_tiles.corners[cx + cy * (blight_tiles.width + 1)] = !R_CliffOwnsCorner(tr.world, x, y) &&
            TerrainMask_CornerValue(view->terrain_mask.cells, view->terrain_mask.width,
                view->terrain_mask.height, cells_per_tile, cx, cy);
    }
    FOR_LOOP(ty, blight_tiles.height) FOR_LOOP(tx, blight_tiles.width) {
        BYTE const *corners = &blight_tiles.corners[tx + ty * (blight_tiles.width + 1)];
        blight_tiles.active[tx + ty * blight_tiles.width] =
            corners[0] || corners[1] || corners[blight_tiles.width + 1] ||
            corners[blight_tiles.width + 2];
    }
    blight_tiles.generation = view->terrain_mask.generation;
    return true;
}

void R_RenderRectSplat(LPCVECTOR2 mins,
                       LPCVECTOR2 maxs,
                       LPCTEXTURE texture,
                       splat_shader_t *shader,
                       COLOR32 color)
{
    if (!tr.world || !texture) {
        return;
    }
    R_SetupSplatState(texture, shader);
    R_GenerateSplatTiles(mins, maxs, color);
    R_FlushSplatBatch();
    R_Call(glDepthMask, GL_TRUE);
}

LPMAPLAYER R_BuildMapSegmentLayer(LPCWAR3MAP map, DWORD sx, DWORD sy, DWORD layer) {
    LPMAPLAYER mapLayer = ri.MemAlloc(sizeof(MAPLAYER));
    PATHSTR zBuffer;
    if (g_groundTextures[layer] == NULL) {
        w3TerrainArt_t const *terrain = R_TerrainArt(map->grounds[layer]);
        if (terrain->file && terrain->dir) {
            snprintf(zBuffer, sizeof(zBuffer), "%s\\%s.blp", terrain->dir, terrain->file);
            g_groundTextures[layer] = R_LoadTexture(zBuffer);
        } else {
            return NULL;
        }
    }
    mapLayer->texture = g_groundTextures[layer];
    mapLayer->type = MAPLAYERTYPE_GROUND;
    ground_current_vertex = ground_vertex_buffer;
    for (DWORD x = sx * SEGMENT_SIZE; x < (sx + 1) * SEGMENT_SIZE; x++) {
        for (DWORD y = sy * SEGMENT_SIZE; y < (sy + 1) * SEGMENT_SIZE; y++) {
            R_MakeTile(map, x, y, layer, mapLayer->texture);
        }
    }
    mapLayer->num_vertices = (DWORD)(ground_current_vertex - ground_vertex_buffer);
    mapLayer->buffer = R_MakeVertexArrayObject(ground_vertex_buffer, mapLayer->num_vertices);
    return mapLayer;
}

LPMAPLAYER R_BuildGroundLayerGlobal(LPCWAR3MAP map, DWORD layer) {
    LPMAPLAYER mapLayer;
    PATHSTR zBuffer;

    if (g_groundTextures[layer] == NULL) {
        w3TerrainArt_t const *terrain = R_TerrainArt(map->grounds[layer]);
        if (terrain->file && terrain->dir) {
            sprintf(zBuffer, "%s\\%s.blp", terrain->dir, terrain->file);
            g_groundTextures[layer] = R_LoadTexture(zBuffer);
        } else {
            return NULL;
        }
    }

    /* Construction scratch must not remain resident (or leak when the next map is larger). */
    LPVERTEX whole_map_buffer = ri.MemAlloc(sizeof(VERTEX) * (map->width - 1) * (map->height - 1) * 6);

    mapLayer = ri.MemAlloc(sizeof(MAPLAYER));
    mapLayer->texture = g_groundTextures[layer];
    mapLayer->type = MAPLAYERTYPE_GROUND;
    ground_current_vertex = whole_map_buffer;
    for (DWORD x = 0; x < map->width - 1; x++) {
        for (DWORD y = 0; y < map->height - 1; y++) {
            R_MakeTile(map, x, y, layer, mapLayer->texture);
        }
    }
    mapLayer->num_vertices = (DWORD)(ground_current_vertex - whole_map_buffer);
    if (mapLayer->num_vertices)
        mapLayer->buffer = R_MakeVertexArrayObject(whole_map_buffer, mapLayer->num_vertices);
    ri.MemFree(whole_map_buffer);
    ground_current_vertex = NULL;
    if (!mapLayer->num_vertices) {
        ri.MemFree(mapLayer);
        return NULL;
    }
    return mapLayer;
}

void R_RenderFlatRectSplat(LPCVECTOR2 mins,
                           LPCVECTOR2 maxs,
                           FLOAT z,
                           LPCTEXTURE texture,
                           splat_shader_t *shader,
                           COLOR32 color)
{
    MATRIX4 model_matrix;
    FLOAT const width = maxs->x - mins->x;
    FLOAT const height = maxs->y - mins->y;
    if (!texture || width <= 0 || height <= 0) {
        return;
    }

    VERTEX vertices[6] = {
        { .position = { mins->x, mins->y, z }, .texcoord = { 0, 1 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { maxs->x, mins->y, z }, .texcoord = { 1, 1 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { maxs->x, maxs->y, z }, .texcoord = { 1, 0 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { mins->x, mins->y, z }, .texcoord = { 0, 1 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { maxs->x, maxs->y, z }, .texcoord = { 1, 0 }, .normal = { 0, 0, 1 }, .color = color },
        { .position = { mins->x, maxs->y, z }, .texcoord = { 0, 0 }, .normal = { 0, 0, 1 }, .color = color },
    };

    Matrix4_identity(&model_matrix);

    R_BindTexture(texture, 0);
    R_SetTextureWrap(texture, false, false); /* this pass uses a 0..1 quad, so clamp the border instead of tiling */

    shader->state.viewProjection = tr.viewDef.viewProjectionMatrix;
    shader->state.model = model_matrix;

    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    R_StatsDraw(GL_TRIANGLES, sizeof(vertices) / sizeof(vertices[0]), 1);
    R_ApplyShader(shader);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, sizeof(vertices) / sizeof(vertices[0]));
    R_Call(glDepthMask, GL_TRUE);
}

void R_RenderSplat(LPCVECTOR2 position,
                   FLOAT radius,
                   LPCTEXTURE texture,
                   splat_shader_t *shader,
                   COLOR32 color)
{
    VECTOR2 mins = {
        .x = position->x - radius,
        .y = position->y - radius,
    };
    VECTOR2 maxs = {
        .x = position->x + radius,
        .y = position->y + radius,
    };

    R_RenderRectSplat(&mins, &maxs, texture, shader, color);
}

VECTOR3 CM_PointIntoHeightmap(LPCVECTOR3 point) {
    if (!point || !tr.world) {
        return (VECTOR3){0};
    }
    return (VECTOR3) {
        .x = (point->x - tr.world->center.x) / TILE_SIZE,
        .y = (point->y - tr.world->center.y) / TILE_SIZE,
        .z = point->z
    };
}

FLOAT R_GetHeightMapValue(int x, int y) {
    return GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x, y));
}

VECTOR3 R_PointFromHeightmap(LPCVECTOR3 point) {
    return (VECTOR3) {
        .x = point->x * TILE_SIZE + tr.world->center.x,
        .y = point->y * TILE_SIZE + tr.world->center.y,
        .z = point->z
    };
}

static BOOL R_ClipTraceToHeightmap(LPCLINE3 line, FLOAT max_x, FLOAT max_y, LPFLOAT t0, LPFLOAT t1) {
    FLOAT const bounds_min[2] = { 0.0f, 0.0f };
    FLOAT const bounds_max[2] = { max_x, max_y };
    FLOAT const start[2] = { line->a.x, line->a.y };
    FLOAT const finish[2] = { line->b.x, line->b.y };

    *t0 = 0.0f;
    *t1 = 1.0f;
    FOR_LOOP(axis, 2) {
        FLOAT const dir = finish[axis] - start[axis];
        FLOAT near_t;
        FLOAT far_t;

        if (fabsf(dir) < EPSILON) {
            if (start[axis] < bounds_min[axis] || start[axis] > bounds_max[axis]) {
                return false;
            }
            continue;
        }

        near_t = (bounds_min[axis] - start[axis]) / dir;
        far_t = (bounds_max[axis] - start[axis]) / dir;
        if (near_t > far_t) {
            FLOAT const swap = near_t;
            near_t = far_t;
            far_t = swap;
        }
        *t0 = MAX(*t0, near_t);
        *t1 = MIN(*t1, far_t);
        if (*t0 > *t1) {
            return false;
        }
    }
    return true;
}

static BOOL R_TraceHeightmapTile(int x, int y, LPCLINE3 line, LPVECTOR3 output) {
    TRIANGLE3 const tri1 = {
        { x, y, R_GetHeightMapValue(x, y) },
        { x+1, y, R_GetHeightMapValue(x+1, y) },
        { x+1, y+1, R_GetHeightMapValue(x+1, y+1) },
    };
    TRIANGLE3 const tri2 = {
        { x+1, y+1, R_GetHeightMapValue(x+1, y+1) },
        { x, y+1, R_GetHeightMapValue(x, y+1) },
        { x, y, R_GetHeightMapValue(x, y) },
    };

    if (Line3_intersect_triangle(line, &tri1, output)) {
        return true;
    }
    return Line3_intersect_triangle(line, &tri2, output);
}

bool _W3M_TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output) {
    if (!viewdef || !output || !tr.world) {
        return false;
    }
    LINE3 const gline = R_LineForScreenPoint(viewdef, x, y);
    LINE3 line = {
        .a = CM_PointIntoHeightmap(&gline.a),
        .b = CM_PointIntoHeightmap(&gline.b),
    };
    int const tiles_x = (int)tr.world->width - 1;
    int const tiles_y = (int)tr.world->height - 1;
    FLOAT t0;
    FLOAT t1;
    FLOAT dir_x;
    FLOAT dir_y;
    int tile_x;
    int tile_y;
    int step_x;
    int step_y;
    FLOAT t_max_x;
    FLOAT t_max_y;
    FLOAT t_delta_x;
    FLOAT t_delta_y;

    if (tiles_x <= 0 || tiles_y <= 0 ||
        !R_ClipTraceToHeightmap(&line, (FLOAT)tiles_x, (FLOAT)tiles_y, &t0, &t1)) {
        return false;
    }

    dir_x = line.b.x - line.a.x;
    dir_y = line.b.y - line.a.y;
    tile_x = (int)floorf(line.a.x + dir_x * t0);
    tile_y = (int)floorf(line.a.y + dir_y * t0);
    tile_x = MAX(0, MIN(tiles_x - 1, tile_x));
    tile_y = MAX(0, MIN(tiles_y - 1, tile_y));

    if (fabsf(dir_x) < EPSILON) {
        step_x = 0;
        t_max_x = 1.0e30f;
        t_delta_x = 1.0e30f;
    } else {
        step_x = dir_x > 0.0f ? 1 : -1;
        t_max_x = (((FLOAT)tile_x + (step_x > 0 ? 1.0f : 0.0f)) - line.a.x) / dir_x;
        t_delta_x = fabsf(1.0f / dir_x);
    }

    if (fabsf(dir_y) < EPSILON) {
        step_y = 0;
        t_max_y = 1.0e30f;
        t_delta_y = 1.0e30f;
    } else {
        step_y = dir_y > 0.0f ? 1 : -1;
        t_max_y = (((FLOAT)tile_y + (step_y > 0 ? 1.0f : 0.0f)) - line.a.y) / dir_y;
        t_delta_y = fabsf(1.0f / dir_y);
    }

    while (tile_x >= 0 && tile_y >= 0 && tile_x < tiles_x && tile_y < tiles_y) {
        if (R_TraceHeightmapTile(tile_x, tile_y, &line, output)) {
            *output = R_PointFromHeightmap(output);
            return true;
        }
        if (t_max_x < t_max_y) {
            if (t_max_x > t1) {
                break;
            }
            tile_x += step_x;
            t_max_x += t_delta_x;
        } else {
            if (t_max_y > t1) {
                break;
            }
            tile_y += step_y;
            t_max_y += t_delta_y;
        }
    }
    return false;
}
