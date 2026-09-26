#include "r_war3map.h"

#define MAX_MAP_LAYERS 16
#define WATER(INDEX) \
MakeColor(color[INDEX], LerpNumber(color[INDEX], 1, 0.25f), LerpNumber(color[INDEX], 1, 0.5f), 1)

texture_t const *g_groundTextures[MAX_MAP_LAYERS] = { NULL };

void R_ResetGroundTextures(void) {
    memset(g_groundTextures, 0, sizeof(g_groundTextures));
}

#define GROUND_VERTEX_BUFFER_CAPACITY (SEGMENT_SIZE * SEGMENT_SIZE * 6)
#define SPLAT_TILE_MAX_VERTICES 30 // vertices; two clipped terrain triangles can each form a seven-corner polygon

static vertex_t ground_vertex_buffer[GROUND_VERTEX_BUFFER_CAPACITY];
static vertex_t *ground_current_vertex = NULL;

vec3_t R_GetVertexPosition(war3map_t const *map, uint32_t x, uint32_t y, bool useLevel) {
    war3mapVertex_t const *vert = GetWar3MapVertex(map, x, y);
    float level = useLevel ? vert->level * TILE_SIZE - HEIGHT_COR : 0;
    if (useLevel && vert->ramp && vert->cliffVariation) {
        level += 0.5 * TILE_SIZE;
    }
    float z = DECODE_HEIGHT(vert->accurate_height) + level;
    return (vec3_t) {
        .x = map->center.x + x * TILE_SIZE,
        .y = map->center.y + y * TILE_SIZE,
        .z = z,
    };
}

static float r_war3_normal_height(void const *data, uint32_t x, uint32_t y) {
    return R_GetVertexPosition(data, x, y, false).z;
}

vec3_t R_GetVertexNormal(war3map_t const *map, uint32_t x, uint32_t y) {
    terrainNormals_t grid = { map, r_war3_normal_height, map->width, map->height, TILE_SIZE };

    return R_TerrainGridNormal(&grid, x, y);
}

static void R_MakeTile(war3map_t const *map, uint32_t x, uint32_t y, uint32_t ground, texture_t const *texture) {
    struct War3MapVertex tile[4];
    GetTileVertices(x, y, map, tile);
    int _tile = GetTile(tile, ground);
    if (!_tile || !R_TileHasGround(tile))
        return;
    
    vec3_t const p[] = {
        R_GetVertexPosition(map, x, y, true),
        R_GetVertexPosition(map, x + 1, y, true),
        R_GetVertexPosition(map, x + 1, y + 1, true),
        R_GetVertexPosition(map, x, y + 1, true),
    };

    vec3_t const n[] = {
        R_GetVertexNormal(map, x, y),
        R_GetVertexNormal(map, x + 1, y),
        R_GetVertexNormal(map, x + 1, y + 1),
        R_GetVertexNormal(map, x, y + 1),
    };

    float const waterlevel[] = {
        GetWar3MapVertexWaterLevel(&tile[3]),
        GetWar3MapVertexWaterLevel(&tile[2]),
        GetWar3MapVertexWaterLevel(&tile[0]),
        GetWar3MapVertexWaterLevel(&tile[1]),
    };

    float const color[] = {
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
    ground_current_vertex += sizeof(geom) / sizeof(vertex_t);
}

static bool R_TileAcceptsSplat(war3map_t const *map, uint32_t x, uint32_t y) {
    struct War3MapVertex tile[4];

    GetTileVertices(x, y, map, tile);
    return GetTile(tile, 0) && R_TileHasGround(tile);
}

static void R_BuildSplatQuad(war3map_t const *map, uint32_t x, uint32_t y, vec2_t const *mins, float width, float height, color32_t color, vertex_t *geom) {
    vec3_t const p[] = {
        R_GetVertexPosition(map, x, y, true),
        R_GetVertexPosition(map, x + 1, y, true),
        R_GetVertexPosition(map, x + 1, y + 1, true),
        R_GetVertexPosition(map, x, y + 1, true),
    };
    vec2_t const uv[] = {
        { (p[0].x - mins->x) / width, 1 - (p[0].y - mins->y) / height },
        { (p[1].x - mins->x) / width, 1 - (p[1].y - mins->y) / height },
        { (p[2].x - mins->x) / width, 1 - (p[2].y - mins->y) / height },
        { (p[3].x - mins->x) / width, 1 - (p[3].y - mins->y) / height },
    };
    vec3_t const normal = { 0, 0, 1 };
    vertex_t const quad[] = {
        { .position = p[0], .texcoord = uv[0], .normal = normal, .color = color },
        { .position = p[1], .texcoord = uv[1], .normal = normal, .color = color },
        { .position = p[2], .texcoord = uv[2], .normal = normal, .color = color },
        { .position = p[0], .texcoord = uv[0], .normal = normal, .color = color },
        { .position = p[2], .texcoord = uv[2], .normal = normal, .color = color },
        { .position = p[3], .texcoord = uv[3], .normal = normal, .color = color },
    };
    memcpy(geom, quad, sizeof(quad));
}

/* Clip in world XY while interpolating Z on each original terrain triangle. */
struct splClip { uint32_t count, axis; float edge; bool above; };
static uint32_t R_ClipSplatPoly(vec3_t const *src, vec3_t *dst, struct splClip clip) {
    vec3_t const *prev = &src[clip.count - 1];
    float pv = clip.axis ? prev->y : prev->x;
    bool pin = clip.above ? pv >= clip.edge : pv <= clip.edge;
    uint32_t out = 0;
    FOR_LOOP(i, clip.count) {
        vec3_t const *cur = &src[i];
        float cv = clip.axis ? cur->y : cur->x;
        bool cin = clip.above ? cv >= clip.edge : cv <= clip.edge;
        if (pin != cin && pv != clip.edge && cv != clip.edge) {
            float t = (clip.edge - pv) / (cv - pv);
            vec3_t hit = clip.axis
                ? (vec3_t){ prev->x + (cur->x - prev->x) * t, clip.edge, prev->z + (cur->z - prev->z) * t }
                : (vec3_t){ clip.edge, prev->y + (cur->y - prev->y) * t, prev->z + (cur->z - prev->z) * t };
            dst[out++] = hit;
        }
        if (cin) dst[out++] = *cur;
        prev = cur; pv = cv; pin = cin;
    }
    return out;
}

static void R_MakeSplatTile(war3map_t const *map, uint32_t x, uint32_t y, vec2_t const *mins, float width, float height, color32_t color) {
    vertex_t geom[6];
    struct splClip clips[4];
    uint32_t num_clips = 0;
    R_BuildSplatQuad(map, x, y, mins, width, height, color, geom);
    if (geom[0].position.x >= mins->x && geom[1].position.x <= mins->x + width &&
        geom[0].position.y >= mins->y && geom[2].position.y <= mins->y + height) {
        memcpy(ground_current_vertex, geom, sizeof(geom));
        ground_current_vertex += 6;
        return;
    }
    /* Full tile quads stretched edge texels outside the splat when the lit shader drew building footprints. */
    if (geom[0].position.x < mins->x) clips[num_clips++] = (struct splClip){ 0, 0, mins->x, true };
    if (geom[1].position.x > mins->x + width) clips[num_clips++] = (struct splClip){ 0, 0, mins->x + width, false };
    if (geom[0].position.y < mins->y) clips[num_clips++] = (struct splClip){ 0, 1, mins->y, true };
    if (geom[2].position.y > mins->y + height) clips[num_clips++] = (struct splClip){ 0, 1, mins->y + height, false };
    FOR_LOOP(tri, 2) {
        vec3_t a[8], b[8];
        vec3_t *poly = a, *scratch = b;
        uint32_t n = 3;
        FOR_LOOP(i, 3) a[i] = geom[tri * 3 + i].position;
        FOR_LOOP(edge, num_clips) {
            struct splClip clip = clips[edge];
            vec3_t *swap;
            clip.count = n;
            n = R_ClipSplatPoly(poly, scratch, clip);
            if (!n) break;
            swap = poly; poly = scratch; scratch = swap;
        }
        for (uint32_t i = 1; i + 1 < n; i++) {
            vec3_t p[] = { poly[0], poly[i], poly[i + 1] };
            FOR_LOOP(j, 3) {
                vertex_t v = geom[0];
                v.position = p[j];
                v.texcoord = (vec2_t){ (p[j].x - mins->x) / width, 1 - (p[j].y - mins->y) / height };
                *ground_current_vertex++ = v;
            }
        }
    }
}

static void R_MakeBlightTile(war3map_t const *map, uint32_t x, uint32_t y, vec2_t const *mins, float width, float height, uint32_t blight_tile) {
    vertex_t geom[6];
    R_BuildSplatQuad(map, x, y, mins, width, height, COLOR32_WHITE, geom);
    /* Mixed tiles use the atlas's left-hand alpha mask selected by the
     * four-corner bitmask.  Tile 15 is special in SetTileUV: it selects
     * the right-hand opaque variation, which is correct only when every
     * corner is Blighted. */
    SetTileUV(GetWar3MapVertex(map, x, y), blight_tile, geom, R_BlightTexture());
    memcpy(ground_current_vertex, geom, sizeof(geom));
    ground_current_vertex += sizeof(geom) / sizeof(vertex_t);
}

static void R_FlushSplatBatch(void) {
    uint32_t num_vertices = (uint32_t)(ground_current_vertex - ground_vertex_buffer);
    if (num_vertices == 0) {
        return;
    }

    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertex_t) * num_vertices, ground_vertex_buffer, GL_STREAM_DRAW);
    R_StatsDraw(GL_TRIANGLES, num_vertices, 1);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
    ground_current_vertex = ground_vertex_buffer;
}

/* Bind the splat shader/VAO/VBO and upload the per-view uniforms once.  All
 * splats in a batch share this state; only the texture and per-tile geometry
 * vary, so re-issuing it per splat was pure overhead. */
static texture_t const *g_splat_texture;
static splat_shader_t *g_splat_shader;

static void R_SetSplatDepthBias(bool enabled) {
    if (enabled) {
        R_Call(glEnable, GL_POLYGON_OFFSET_FILL);
        R_Call(glPolygonOffset, -1.0f, -1.0f);
    } else {
        R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
        R_Call(glPolygonOffset, 0.0f, 0.0f);
    }
}

static void R_SetupSplatState(texture_t const *texture, splat_shader_t *shader) {
    mat4_t mModelMatrix;

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
    R_SetSplatDepthBias(true);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    ground_current_vertex = ground_vertex_buffer;
    R_ApplyShader(shader);
}

/* Emit terrain-conforming tiles for one splat rect into the shared buffer,
 * flushing to the GPU only when the buffer fills. */
static void R_GenerateSplatTiles(vec2_t const *mins, vec2_t const *maxs, color32_t color) {
    int x_start, x_end;
    int y_start, y_end;

    float const width = maxs->x - mins->x;
    float const height = maxs->y - mins->y;
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
            if (!R_TileAcceptsSplat(tr.world, (uint32_t)x, (uint32_t)y)) {
                continue;
            }
            if (ground_current_vertex - ground_vertex_buffer >
                GROUND_VERTEX_BUFFER_CAPACITY - SPLAT_TILE_MAX_VERTICES) {
                R_FlushSplatBatch();
            }
            R_MakeSplatTile(tr.world, (uint32_t)x, (uint32_t)y, mins, width, height, color);
        }
    }
}

void R_BeginSplatBatch(splat_shader_t *shader) {
    g_splat_shader = shader;
    g_splat_texture = NULL;
    ground_current_vertex = ground_vertex_buffer;
}

void R_AddRectSplat(vec2_t const *mins, vec2_t const *maxs, texture_t const *texture, color32_t color) {
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
    R_SetSplatDepthBias(false);
    R_Call(glDepthMask, GL_TRUE);
}

typedef struct {
    uint8_t *active;
    uint8_t *corners;
    uint32_t width, height;
    uint32_t generation;
    vec2_t origin;
    float cell_size;
} blightTileCache_t;

static blightTileCache_t blight_tiles;
static maplayer_t blight_layer;
static bool blight_layer_valid;
static uint32_t blight_layer_generation = ~0u;

static bool R_BlightTileCacheUpdate(viewDef_t const *view);

static void R_ResetBlightLayer(void) {
    if (blight_layer.buffer) R_ReleaseVertexArrayObject((buffer_t *)blight_layer.buffer);
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

static uint32_t R_BlightEmittedTiles(void) {
    uint32_t count = 0, stride = blight_tiles.width + 1;
    FOR_LOOP(ty, blight_tiles.height) FOR_LOOP(tx, blight_tiles.width) {
        uint32_t const blight_tile = TerrainMask_TileMask(blight_tiles.corners, stride, tx, ty);
        vec2_t mins = { blight_tiles.origin.x + tx * TILE_SIZE, blight_tiles.origin.y + ty * TILE_SIZE };
        int const map_x = (int)floorf((mins.x - tr.world->center.x) / TILE_SIZE);
        int const map_y = (int)floorf((mins.y - tr.world->center.y) / TILE_SIZE);
        if (!blight_tiles.active[tx + ty * blight_tiles.width] || !blight_tile ||
            map_x < 0 || map_y < 0 || map_x >= (int)tr.world->width - 1 ||
            map_y >= (int)tr.world->height - 1 ||
            !R_TileAcceptsSplat(tr.world, (uint32_t)map_x, (uint32_t)map_y)) continue;
        count++;
    }
    return count;
}

void R_UpdateBlightLayer(void) {
    vertex_t *vertices;
    uint32_t count = 0, tiles;
    uint32_t stride = blight_tiles.width + 1;

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
        uint32_t const blight_tile = TerrainMask_TileMask(blight_tiles.corners, stride, tx, ty);
        vec2_t mins = { blight_tiles.origin.x + tx * TILE_SIZE, blight_tiles.origin.y + ty * TILE_SIZE };
        int const map_x = (int)floorf((mins.x - tr.world->center.x) / TILE_SIZE);
        int const map_y = (int)floorf((mins.y - tr.world->center.y) / TILE_SIZE);
        if (!blight_tiles.active[tx + ty * blight_tiles.width] || !blight_tile ||
            map_x < 0 || map_y < 0 || map_x >= (int)tr.world->width - 1 ||
            map_y >= (int)tr.world->height - 1 ||
            !R_TileAcceptsSplat(tr.world, (uint32_t)map_x, (uint32_t)map_y)) continue;
        R_MakeBlightTile(tr.world, (uint32_t)map_x, (uint32_t)map_y, &mins, TILE_SIZE, TILE_SIZE, blight_tile);
    }
    count = (uint32_t)(ground_current_vertex - vertices);
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

static bool R_BlightTileCacheUpdate(viewDef_t const *view) {
    uint32_t cells_per_tile;
    uint32_t old_width = blight_tiles.width, old_height = blight_tiles.height;
    uint32_t new_width, new_height;

    if (!view || !view->terrain_mask.cells || !view->terrain_mask.width ||
        !view->terrain_mask.height || view->terrain_mask.cell_size <= 0.0f)
        return false;
    cells_per_tile = (uint32_t)floorf(TILE_SIZE / view->terrain_mask.cell_size + 0.5f);
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
        uint8_t const *corners = &blight_tiles.corners[tx + ty * (blight_tiles.width + 1)];
        blight_tiles.active[tx + ty * blight_tiles.width] =
            corners[0] || corners[1] || corners[blight_tiles.width + 1] ||
            corners[blight_tiles.width + 2];
    }
    blight_tiles.generation = view->terrain_mask.generation;
    return true;
}

void R_RenderRectSplat(vec2_t const *mins,
                       vec2_t const *maxs,
                       texture_t const *texture,
                       splat_shader_t *shader,
                       color32_t color)
{
    if (!tr.world || !texture) {
        return;
    }
    R_SetupSplatState(texture, shader);
    R_GenerateSplatTiles(mins, maxs, color);
    R_FlushSplatBatch();
    R_SetSplatDepthBias(false);
    R_Call(glDepthMask, GL_TRUE);
}

maplayer_t *R_BuildMapSegmentLayer(war3map_t const *map, uint32_t sx, uint32_t sy, uint32_t layer) {
    maplayer_t *mapLayer = ri.MemAlloc(sizeof(maplayer_t));
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
    for (uint32_t x = sx * SEGMENT_SIZE; x < (sx + 1) * SEGMENT_SIZE; x++) {
        for (uint32_t y = sy * SEGMENT_SIZE; y < (sy + 1) * SEGMENT_SIZE; y++) {
            R_MakeTile(map, x, y, layer, mapLayer->texture);
        }
    }
    mapLayer->num_vertices = (uint32_t)(ground_current_vertex - ground_vertex_buffer);
    mapLayer->buffer = R_MakeVertexArrayObject(ground_vertex_buffer, mapLayer->num_vertices);
    return mapLayer;
}

maplayer_t *R_BuildGroundLayerGlobal(war3map_t const *map, uint32_t layer) {
    maplayer_t *mapLayer;
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
    vertex_t *whole_map_buffer = ri.MemAlloc(sizeof(vertex_t) * (map->width - 1) * (map->height - 1) * 6);

    mapLayer = ri.MemAlloc(sizeof(maplayer_t));
    mapLayer->texture = g_groundTextures[layer];
    mapLayer->type = MAPLAYERTYPE_GROUND;
    ground_current_vertex = whole_map_buffer;
    for (uint32_t x = 0; x < map->width - 1; x++) {
        for (uint32_t y = 0; y < map->height - 1; y++) {
            R_MakeTile(map, x, y, layer, mapLayer->texture);
        }
    }
    mapLayer->num_vertices = (uint32_t)(ground_current_vertex - whole_map_buffer);
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

void R_RenderFlatRectSplat(vec2_t const *mins,
                           vec2_t const *maxs,
                           float z,
                           texture_t const *texture,
                           splat_shader_t *shader,
                           color32_t color)
{
    mat4_t model_matrix;
    float const width = maxs->x - mins->x;
    float const height = maxs->y - mins->y;
    if (!texture || width <= 0 || height <= 0) {
        return;
    }

    vertex_t vertices[6] = {
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

void R_RenderSplat(vec2_t const *position,
                   float radius,
                   texture_t const *texture,
                   splat_shader_t *shader,
                   color32_t color)
{
    vec2_t mins = {
        .x = position->x - radius,
        .y = position->y - radius,
    };
    vec2_t maxs = {
        .x = position->x + radius,
        .y = position->y + radius,
    };

    R_RenderRectSplat(&mins, &maxs, texture, shader, color);
}

vec3_t CM_PointIntoHeightmap(vec3_t const *point) {
    if (!point || !tr.world) {
        return (vec3_t){0};
    }
    return (vec3_t) {
        .x = (point->x - tr.world->center.x) / TILE_SIZE,
        .y = (point->y - tr.world->center.y) / TILE_SIZE,
        .z = point->z
    };
}

float R_GetHeightMapValue(int x, int y) {
    return GetWar3MapVertexHeight(GetWar3MapVertex(tr.world, x, y));
}

vec3_t R_PointFromHeightmap(vec3_t const *point) {
    return (vec3_t) {
        .x = point->x * TILE_SIZE + tr.world->center.x,
        .y = point->y * TILE_SIZE + tr.world->center.y,
        .z = point->z
    };
}

static bool R_ClipTraceToHeightmap(line3_t const *line, float max_x, float max_y, float *t0, float *t1) {
    float const bounds_min[2] = { 0.0f, 0.0f };
    float const bounds_max[2] = { max_x, max_y };
    float const start[2] = { line->a.x, line->a.y };
    float const finish[2] = { line->b.x, line->b.y };

    *t0 = 0.0f;
    *t1 = 1.0f;
    FOR_LOOP(axis, 2) {
        float const dir = finish[axis] - start[axis];
        float near_t;
        float far_t;

        if (fabsf(dir) < EPSILON) {
            if (start[axis] < bounds_min[axis] || start[axis] > bounds_max[axis]) {
                return false;
            }
            continue;
        }

        near_t = (bounds_min[axis] - start[axis]) / dir;
        far_t = (bounds_max[axis] - start[axis]) / dir;
        if (near_t > far_t) {
            float const swap = near_t;
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

static bool R_TraceHeightmapTile(int x, int y, line3_t const *line, vec3_t *output) {
    triangle3_t const tri1 = {
        { x, y, R_GetHeightMapValue(x, y) },
        { x+1, y, R_GetHeightMapValue(x+1, y) },
        { x+1, y+1, R_GetHeightMapValue(x+1, y+1) },
    };
    triangle3_t const tri2 = {
        { x+1, y+1, R_GetHeightMapValue(x+1, y+1) },
        { x, y+1, R_GetHeightMapValue(x, y+1) },
        { x, y, R_GetHeightMapValue(x, y) },
    };

    if (Line3_intersect_triangle(line, &tri1, output)) {
        return true;
    }
    return Line3_intersect_triangle(line, &tri2, output);
}

bool _W3M_TraceLocation(viewDef_t const *viewdef, float x, float y, vec3_t *output) {
    if (!viewdef || !output || !tr.world) {
        return false;
    }
    line3_t const gline = R_LineForScreenPoint(viewdef, x, y);
    line3_t line = {
        .a = CM_PointIntoHeightmap(&gline.a),
        .b = CM_PointIntoHeightmap(&gline.b),
    };
    int const tiles_x = (int)tr.world->width - 1;
    int const tiles_y = (int)tr.world->height - 1;
    float t0;
    float t1;
    float dir_x;
    float dir_y;
    int tile_x;
    int tile_y;
    int step_x;
    int step_y;
    float t_max_x;
    float t_max_y;
    float t_delta_x;
    float t_delta_y;

    if (tiles_x <= 0 || tiles_y <= 0 ||
        !R_ClipTraceToHeightmap(&line, (float)tiles_x, (float)tiles_y, &t0, &t1)) {
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
        t_max_x = (((float)tile_x + (step_x > 0 ? 1.0f : 0.0f)) - line.a.x) / dir_x;
        t_delta_x = fabsf(1.0f / dir_x);
    }

    if (fabsf(dir_y) < EPSILON) {
        step_y = 0;
        t_max_y = 1.0e30f;
        t_delta_y = 1.0e30f;
    } else {
        step_y = dir_y > 0.0f ? 1 : -1;
        t_max_y = (((float)tile_y + (step_y > 0 ? 1.0f : 0.0f)) - line.a.y) / dir_y;
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
