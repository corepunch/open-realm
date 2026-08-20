#include "r_wowmap.h"

COLOR32 Wow_Color(BYTE r, BYTE g, BYTE b, BYTE a) {
    return (COLOR32){ b, g, r, a };
}

VERTEX Wow_Vertex(float x, float y, float z, float u, float v, COLOR32 color) {
    VERTEX vertex;
    memset(&vertex, 0, sizeof(vertex));
    vertex.position = (VECTOR3){ x, y, z };
    vertex.texcoord = (VECTOR2){ u, v };
    vertex.normal = (VECTOR3){ 0.0f, 0.0f, 1.0f };
    vertex.color = color;
    return vertex;
}

void Wow_AddBoundsPoint(LPBOX3 bounds, LPCVECTOR3 p) {
    bounds->min.x = MIN(bounds->min.x, p->x);
    bounds->min.y = MIN(bounds->min.y, p->y);
    bounds->min.z = MIN(bounds->min.z, p->z);
    bounds->max.x = MAX(bounds->max.x, p->x);
    bounds->max.y = MAX(bounds->max.y, p->y);
    bounds->max.z = MAX(bounds->max.z, p->z);
}

BOX3 Wow_EmptyBounds(void) {
    return (BOX3){
        .min = { FLT_MAX, FLT_MAX, FLT_MAX },
        .max = { -FLT_MAX, -FLT_MAX, -FLT_MAX },
    };
}

VECTOR3 Wow_WorldPoint(float x, float y, float z) {
    return (VECTOR3){
        x,
        y,
        z,
    };
}

VECTOR2 Wow_McvtCoords(int index) {
    int row = index / 17;
    int col = index % 17;
    VECTOR2 coords;

    if (col < 9) {
        coords.x = col * WOW_ADT_UNIT_SIZE;
        coords.y = row * WOW_ADT_UNIT_SIZE;
    } else {
        coords.x = (col - 8.5f) * WOW_ADT_UNIT_SIZE;
        coords.y = (row + 0.5f) * WOW_ADT_UNIT_SIZE;
    }
    return coords;
}

VECTOR3 Wow_McvtPoint(wowVec3_t pos, float const *heights, int index) {
    VECTOR2 coords = Wow_McvtCoords(index);

    return Wow_WorldPoint( pos.x - coords.y, pos.y - coords.x, pos.z + heights[index]);
}

VECTOR3 Wow_TerrainFaceNormal(LPCVECTOR3 a, LPCVECTOR3 b, LPCVECTOR3 c) {
    VECTOR3 ab = Vector3_sub(b, a);
    VECTOR3 ac = Vector3_sub(c, a);
    VECTOR3 normal = Vector3_cross(&ab, &ac);

    if (Vector3_lengthsq(&normal) <= 0.000001f) {
        return (VECTOR3){ 0.0f, 0.0f, 1.0f };
    }

    Vector3_normalize(&normal);
    if (normal.z < 0.0f) {
        normal = Vector3_scale(&normal, -1.0f);
    }
    return normal;
}

static VECTOR3 Wow_DecodeTerrainNormal(BYTE const *normals, int index) {
    int base = index * 3;
    signed char nx;
    signed char ny;
    signed char nz;
    VECTOR3 normal;

    if (!normals) {
        return (VECTOR3){ 0.0f, 0.0f, 1.0f };
    }

    nx = (signed char)normals[base + 0];
    ny = (signed char)normals[base + 1];
    nz = (signed char)normals[base + 2];

    /* ADT local axes map into world axes as x=-y_local, y=-x_local, z=z_local. */
    normal = (VECTOR3){
        -(float)ny / 127.0f,
        -(float)nx / 127.0f,
        (float)nz / 127.0f,
    };

    if (Vector3_lengthsq(&normal) <= 0.000001f) {
        return (VECTOR3){ 0.0f, 0.0f, 1.0f };
    }
    Vector3_normalize(&normal);
    if (normal.z < 0.0f) {
        normal = Vector3_scale(&normal, -1.0f);
    }
    return normal;
}

void Wow_AccumulateTerrainCellNormals(VECTOR3 normals[WOW_MCVT_COUNT],
                                             wowVec3_t pos,
                                             float const *heights,
                                             int x,
                                             int y) {
    static BYTE const tri[] = { 9, 0, 17, 9, 1, 0, 9, 18, 1, 9, 17, 18 };
    int base = y * 17 + x;

    for (DWORD i = 0; i < sizeof(tri) / sizeof(tri[0]); i += 3) {
        int i0 = base + tri[i + 0];
        int i1 = base + tri[i + 1];
        int i2 = base + tri[i + 2];
        VECTOR3 p0 = Wow_McvtPoint(pos, heights, i0);
        VECTOR3 p1 = Wow_McvtPoint(pos, heights, i1);
        VECTOR3 p2 = Wow_McvtPoint(pos, heights, i2);
        VECTOR3 normal = Wow_TerrainFaceNormal(&p0, &p1, &p2);

        normals[i0] = Vector3_add(&normals[i0], &normal);
        normals[i1] = Vector3_add(&normals[i1], &normal);
        normals[i2] = Vector3_add(&normals[i2], &normal);
    }
}

void Wow_NormalizeTerrainNormals(VECTOR3 normals[WOW_MCVT_COUNT]) {
    FOR_LOOP(i, WOW_MCVT_COUNT) {
        if (Vector3_lengthsq(&normals[i]) <= 0.000001f) {
            normals[i] = (VECTOR3){ 0.0f, 0.0f, 1.0f };
            continue;
        }
        Vector3_normalize(&normals[i]);
    }
}

void Wow_PushTerrainVertex(VERTEX *vertices,
                                  LPDWORD index,
                                  wowVec3_t pos,
                                  float const *heights,
                                  LPCVECTOR3 normal,
                                  int height_index,
                                  COLOR32 color) {
    VECTOR3 p = Wow_McvtPoint(pos, heights, height_index);
    VECTOR2 coords = Wow_McvtCoords(height_index);
    float u = coords.x / WOW_ADT_UNIT_SIZE;
    float v = coords.y / WOW_ADT_UNIT_SIZE;
    VERTEX vertex = Wow_Vertex(p.x, p.y, p.z, u, v, color);
    vertex.normal = *normal;
    vertices[(*index)++] = vertex;
}

BOOL Wow_IsHole(WORD holes, int x, int y) {
    static int holetab_h[4] = { 0x1111, 0x2222, 0x4444, 0x8888 };
    static int holetab_v[4] = { 0x000f, 0x00f0, 0x0f00, 0xf000 };
    x >>= 1;
    y >>= 1;
    return (holes & holetab_h[x] & holetab_v[y]) != 0;
}

void Wow_AddTerrainCell(VERTEX *vertices,
                               LPDWORD index,
                               wowVec3_t pos,
                               float const *heights,
                               VECTOR3 const normals[WOW_MCVT_COUNT],
                               int x,
                               int y,
                               COLOR32 const *mccv) {
    static BYTE const tri[] = { 9, 0, 17, 9, 1, 0, 9, 18, 1, 9, 17, 18 };
    int base = y * 17 + x;
    FOR_LOOP(i, sizeof(tri) / sizeof(tri[0])) {
        int height_index = base + tri[i];
        Wow_PushTerrainVertex(vertices, index, pos, heights, &normals[height_index], height_index, mccv[height_index]);
    }
}

BOOL Wow_BarycentricHeight(float px,
                                  float py,
                                  float ax,
                                  float ay,
                                  float ah,
                                  float bx,
                                  float by,
                                  float bh,
                                  float cx,
                                  float cy,
                                  float ch,
                                  float *height) {
    float den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    float wa;
    float wb;
    float wc;

    if (fabsf(den) < 0.000001f || !height) {
        return false;
    }
    wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den;
    wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den;
    wc = 1.0f - wa - wb;
    if (wa < -0.0001f || wb < -0.0001f || wc < -0.0001f) {
        return false;
    }

    *height = wa * ah + wb * bh + wc * ch;
    return true;
}

BOOL Wow_HeightInCell(float const *heights, int row, int col, float fx, float fy, float *height) {
    int base = row * 17 + col;
    float h_tl = heights[base];
    float h_tr = heights[base + 1];
    float h_bl = heights[base + 17];
    float h_br = heights[base + 18];
    float h_c = heights[base + 9];

    return Wow_BarycentricHeight(fx, fy, 0.5f, 0.5f, h_c, 0.0f, 0.0f, h_tl, 1.0f, 0.0f, h_bl, height) ||
           Wow_BarycentricHeight(fx, fy, 0.5f, 0.5f, h_c, 0.0f, 1.0f, h_tr, 0.0f, 0.0f, h_tl, height) ||
           Wow_BarycentricHeight(fx, fy, 0.5f, 0.5f, h_c, 1.0f, 1.0f, h_br, 0.0f, 1.0f, h_tr, height) ||
           Wow_BarycentricHeight(fx, fy, 0.5f, 0.5f, h_c, 1.0f, 0.0f, h_bl, 1.0f, 1.0f, h_br, height);
}

BOOL Wow_TerrainHeightAtPoint(float sx, float sy, float *height) {
    wowAdtChunk_t const *chunk;

    if (!height) {
        return false;
    }

    for (chunk = wow_world.chunks; chunk; chunk = chunk->next) {
        float local_row;
        float local_col;
        int cell_row;
        int cell_col;
        float cell_height;

        if (!chunk->has_heights ||
            sx > chunk->position.x + 0.001f ||
            sx < chunk->position.x - 8.0f * WOW_ADT_UNIT_SIZE - 0.001f ||
            sy > chunk->position.y + 0.001f ||
            sy < chunk->position.y - 8.0f * WOW_ADT_UNIT_SIZE - 0.001f) {
            continue;
        }

        local_row = (chunk->position.x - sx) / WOW_ADT_UNIT_SIZE;
        local_col = (chunk->position.y - sy) / WOW_ADT_UNIT_SIZE;
        cell_row = (int)floorf(MIN(local_row, 7.9999f));
        cell_col = (int)floorf(MIN(local_col, 7.9999f));
        if (cell_row < 0 || cell_row >= 8 || cell_col < 0 || cell_col >= 8) {
            continue;
        }
        if (Wow_HeightInCell(chunk->heights, cell_row, cell_col, local_row - cell_row, local_col - cell_col, &cell_height)) {
            *height = chunk->position.z + cell_height;
            return true;
        }
    }

    return false;
}

void Wow_AddAdtChunk(wowVec3_t pos,
                            DWORD alpha_index_x,
                            DWORD alpha_index_y,
                            WORD holes,
                            uint64_t no_effect_mask,
                            BYTE const alpha[4][WOW_ALPHA_TEXELS],
                            wowLayer_t const *layers,
                            DWORD layer_count,
                            char **textures,
                            DWORD num_textures,
                            float const *heights,
                            BYTE const *normals,
                            COLOR32 const *mccv,
                            BYTE const *mcsh) {
    enum { MAX_VERTICES = 8 * 8 * 12 };
    COLOR32 mccv_fallback[WOW_MCVT_COUNT];
    /* Vanilla (1.x) ADTs have no MCCV, so fall back to white and let the shader's
       dynamic sun (ambient + diffuse·N·L) light the surface. MCCV only exists in
       WotLK+; TODO: convert its on-disk BGRA bytes to RGBA (like Wow_Color does
       for MOCV) when WotLK terrain data is supported. */
    if (!mccv) {
        COLOR32 white = Wow_Color(255, 255, 255, 255);
        FOR_LOOP(i, WOW_MCVT_COUNT) mccv_fallback[i] = white;
        mccv = mccv_fallback;
    }
    DWORD slot_texture_ids[4] = { 0, 0, 0, 0 };
    DWORD unique_layer_count = Wow_BuildUniqueTextureSlots(layers, layer_count, slot_texture_ids);
    DWORD effective_layers = MAX(1, MIN(unique_layer_count ? unique_layer_count : layer_count, 4));
    VECTOR3 derived_normals[WOW_MCVT_COUNT];
    VERTEX *vertices;
    DWORD num_vertices = 0;
    wowAdtChunk_t *chunk;

    if (!heights) {
        return;
    }
    vertices = ri.MemAlloc(sizeof(VERTEX) * MAX_VERTICES);
    if (!vertices) {
        return;
    }

    if (normals) {
        FOR_LOOP(i, WOW_MCVT_COUNT) {
            derived_normals[i] = Wow_DecodeTerrainNormal(normals, i);
        }
    } else {
        memset(derived_normals, 0, sizeof(derived_normals));
        FOR_LOOP(y, 8) {
            FOR_LOOP(x, 8) {
                if (WOW_IGNORE_TERRAIN_HOLES || !Wow_IsHole(holes, x, y)) {
                    Wow_AccumulateTerrainCellNormals(derived_normals, pos, heights, x, y);
                }
            }
        }
        Wow_NormalizeTerrainNormals(derived_normals);
    }

    FOR_LOOP(y, 8) {
        FOR_LOOP(x, 8) {
            if (WOW_IGNORE_TERRAIN_HOLES || !Wow_IsHole(holes, x, y)) {
                Wow_AddTerrainCell(vertices, &num_vertices, pos, heights, derived_normals, x, y, mccv);
            }
        }
    }

    if (!num_vertices) {
        ri.MemFree(vertices);
        return;
    }

    chunk = ri.MemAlloc(sizeof(*chunk));
    memset(chunk, 0, sizeof(*chunk));
    chunk->bounds = (BOX3){
        .min = { FLT_MAX, FLT_MAX, FLT_MAX },
        .max = { -FLT_MAX, -FLT_MAX, -FLT_MAX },
    };
    FOR_LOOP(i, num_vertices) {
        Wow_AddBoundsPoint(&chunk->bounds, &vertices[i].position);
    }
    chunk->buffer = R_MakeVertexArrayObject(vertices, num_vertices);
    chunk->num_vertices = num_vertices;
    chunk->layer_count = effective_layers;
    chunk->position = pos;
    memcpy(chunk->heights, heights, sizeof(chunk->heights));
    chunk->has_heights = true;
    if (mcsh) {
        memcpy(chunk->mcsh, mcsh, sizeof(chunk->mcsh));
        chunk->has_mcsh = true;
    }
    chunk->alpha_texture = wow_world.alpha_atlas_texture ? wow_world.alpha_atlas_texture : Wow_CreateAlphaTexture(alpha);
    chunk->alpha_index_x = alpha_index_x;
    chunk->alpha_index_y = alpha_index_y;
    /* Upload absolute world Z = pos.z + MCVT relative height into GPU atlas */
    Wow_UploadHeightAtlasChunk(alpha_index_x, alpha_index_y, pos.z, chunk->heights);
    /* Any resident chunk defines tile (0,0); waiting for that tile breaks sparse maps and world edges. */
    if (!wow_world.has_atlas_origin) {
        wow_world.atlas_world_x = pos.x + alpha_index_y * WOW_ADT_CHUNK_SIZE;
        wow_world.atlas_world_y = pos.y + alpha_index_x * WOW_ADT_CHUNK_SIZE;
        wow_world.has_atlas_origin = true;
    }
    FOR_LOOP(layer_index, 4) {
        DWORD texture_id = 0;
        if (layer_index < unique_layer_count) {
            texture_id = slot_texture_ids[layer_index];
        } else if (unique_layer_count > 0) {
            texture_id = slot_texture_ids[0];
        }
        if (texture_id < num_textures && textures[texture_id]) {
            chunk->textures[layer_index] = Wow_LoadTexture(textures[texture_id]);
        } else {
            chunk->textures[layer_index] = layer_index > 0 ? chunk->textures[0] : tr.texture[TEX_WHITE];
        }
    }
    Wow_BuildGrassForChunk(chunk, alpha, layers, layer_count, textures, num_textures, no_effect_mask);
    ADD_TO_LIST(chunk, wow_world.chunks);
    wow_world.num_chunks++;
    wow_world.layer_histogram[MIN(effective_layers, 4)]++;
    ri.MemFree(vertices);
}
