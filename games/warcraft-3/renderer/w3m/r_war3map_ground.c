#include "r_war3map.h"

#define MAX_MAP_LAYERS 16
#define WATER(INDEX) \
MakeColor(color[INDEX], LerpNumber(color[INDEX], 1, 0.25f), LerpNumber(color[INDEX], 1, 0.5f), 1)

LPCTEXTURE g_groundTextures[MAX_MAP_LAYERS] = { NULL };

#define GROUND_VERTEX_BUFFER_CAPACITY 4096

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

VECTOR3 R_GetVertexNormal(LPCWAR3MAP map, DWORD x, DWORD y) {
    bool useLevel = false;
    VECTOR3 const currentPoint = R_GetVertexPosition(map, x, y, useLevel);
    VECTOR3 const leftPoint = (x > 0) ? R_GetVertexPosition(map, x - 1, y, useLevel) : currentPoint;
    VECTOR3 const rightPoint = (x < map->width - 1) ? R_GetVertexPosition(map, x + 1, y, useLevel) : currentPoint;
    VECTOR3 const topPoint = (y > 0) ? R_GetVertexPosition(map, x, y - 1, useLevel) : currentPoint;
    VECTOR3 const bottomPoint = (y < map->height - 1) ? R_GetVertexPosition(map, x, y + 1, useLevel) : currentPoint;
    VECTOR3 const diffX = Vector3_sub(&rightPoint, &leftPoint);
    VECTOR3 const diffY = Vector3_sub(&bottomPoint, &topPoint);
    VECTOR3 normal = Vector3_cross(&diffX, &diffY);
    Vector3_normalize(&normal);
    return normal;
}

DWORD IsMidRamp(LPCWAR3MAPVERTEX mv) {
    return
        (mv[0].cliffVariation && mv[0].ramp) +
        (mv[1].cliffVariation && mv[1].ramp) +
        (mv[2].cliffVariation && mv[2].ramp) +
        (mv[3].cliffVariation && mv[3].ramp);
}


static void R_MakeTile(LPCWAR3MAP map, DWORD x, DWORD y, DWORD ground, LPCTEXTURE texture) {
    struct War3MapVertex tile[4];
    GetTileVertices(x, y, map, tile);
    int _tile = GetTile(tile, ground);
    int _ramps = GetTileRamps(tile);

    if (_tile == 0)
        return;

    if (IsTileCliff(tile) && _ramps < 4)
        return;
    
    if (_ramps == 2 && IsMidRamp(tile) == 1)
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
    int ground;
    int ramps;

    GetTileVertices(x, y, map, tile);
    ground = GetTile(tile, 0);
    ramps = GetTileRamps(tile);

    if (ground == 0) {
        return false;
    }
    if (IsTileCliff(tile) && ramps < 4) {
        return false;
    }
    if (ramps == 2 && IsMidRamp(tile) == 1) {
        return false;
    }
    return true;
}

static void R_MakeSplatTile(LPCWAR3MAP map,
                            DWORD x,
                            DWORD y,
                            LPCVECTOR2 mins,
                            FLOAT width,
                            FLOAT height,
                            COLOR32 color) {
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
    VERTEX const geom[] = {
        { .position = p[0], .texcoord = uv[0], .normal = normal, .color = color },
        { .position = p[1], .texcoord = uv[1], .normal = normal, .color = color },
        { .position = p[2], .texcoord = uv[2], .normal = normal, .color = color },
        { .position = p[0], .texcoord = uv[0], .normal = normal, .color = color },
        { .position = p[2], .texcoord = uv[2], .normal = normal, .color = color },
        { .position = p[3], .texcoord = uv[3], .normal = normal, .color = color },
    };

    memcpy(ground_current_vertex, geom, sizeof(geom));
    ground_current_vertex += sizeof(geom) / sizeof(VERTEX);
}

static void R_FlushSplatBatch(void) {
    DWORD num_vertices = (DWORD)(ground_current_vertex - ground_vertex_buffer);
    if (num_vertices == 0) {
        return;
    }

    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(VERTEX) * num_vertices, ground_vertex_buffer, GL_STREAM_DRAW);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
    ground_current_vertex = ground_vertex_buffer;
}

LPMAPLAYER R_BuildMapSegmentLayer(LPCWAR3MAP map, DWORD sx, DWORD sy, DWORD layer) {
    LPMAPLAYER mapLayer = ri.MemAlloc(sizeof(MAPLAYER));
    PATHSTR zBuffer;
    if (g_groundTextures[layer] == NULL) {
        char groundID[5] = { 0 };
        memcpy(groundID, &map->grounds[layer], 4);
        LPCSTR dir = ri.FindSheetCell(tr.sheet[SHEET_TERRAIN], groundID, "dir");
        LPCSTR file = ri.FindSheetCell(tr.sheet[SHEET_TERRAIN], groundID, "file");
        if (file && dir) {
            sprintf(zBuffer, "%s\\%s.blp", dir, file);
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

void R_RenderRectSplat(LPCVECTOR2 mins,
                       LPCVECTOR2 maxs,
                       LPCTEXTURE texture,
                       LPCSHADER shader,
                       COLOR32 color)
{
    MATRIX4 mModelMatrix;
    int x_start, x_end;
    int y_start, y_end;

    Matrix4_identity(&mModelMatrix);
    
    FLOAT const width = maxs->x - mins->x;
    FLOAT const height = maxs->y - mins->y;
    if (!tr.world || !texture || width <= 0 || height <= 0) {
        return;
    }

    x_start = MAX(0, (int)floor((mins->x - tr.world->center.x) / TILE_SIZE));
    y_start = MAX(0, (int)floor((mins->y - tr.world->center.y) / TILE_SIZE));
    x_end = MIN((int)tr.world->width - 1, (int)ceil((maxs->x - tr.world->center.x) / TILE_SIZE));
    y_end = MIN((int)tr.world->height - 1, (int)ceil((maxs->y - tr.world->center.y) / TILE_SIZE));

    if (x_start >= x_end || y_start >= y_end) {
        return;
    }

    R_BindTexture(texture, 0);
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uModelMatrix, 1, GL_FALSE, mModelMatrix.v);

    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);

    ground_current_vertex = ground_vertex_buffer;
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
    R_FlushSplatBatch();
    R_Call(glDepthMask, GL_TRUE);
}

void R_RenderFlatRectSplat(LPCVECTOR2 mins,
                           LPCVECTOR2 maxs,
                           FLOAT z,
                           LPCTEXTURE texture,
                           LPCSHADER shader,
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
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uModelMatrix, 1, GL_FALSE, model_matrix.v);

    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, sizeof(vertices) / sizeof(vertices[0]));
    R_Call(glDepthMask, GL_TRUE);
}

void R_RenderSplat(LPCVECTOR2 position,
                   FLOAT radius,
                   LPCTEXTURE texture,
                   LPCSHADER shader,
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

bool R_TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output) {
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
