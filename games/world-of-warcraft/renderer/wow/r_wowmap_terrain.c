#include "r_wowmap.h"

color32_t Wow_Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (color32_t){ b, g, r, a };
}

vertex_t Wow_Vertex(float x, float y, float z, float u, float v, color32_t color) {
    vertex_t vertex;
    memset(&vertex, 0, sizeof(vertex));
    vertex.position = (vector3_t){ x, y, z };
    vertex.texcoord = (vector2_t){ u, v };
    vertex.normal = (vector3_t){ 0.0f, 0.0f, 1.0f };
    vertex.color = color;
    return vertex;
}

void Wow_AddBoundsPoint(box3_t * bounds, vector3_t const * p) {
    bounds->min.x = MIN(bounds->min.x, p->x);
    bounds->min.y = MIN(bounds->min.y, p->y);
    bounds->min.z = MIN(bounds->min.z, p->z);
    bounds->max.x = MAX(bounds->max.x, p->x);
    bounds->max.y = MAX(bounds->max.y, p->y);
    bounds->max.z = MAX(bounds->max.z, p->z);
}

box3_t Wow_EmptyBounds(void) {
    return (box3_t){
        .min = { FLT_MAX, FLT_MAX, FLT_MAX },
        .max = { -FLT_MAX, -FLT_MAX, -FLT_MAX },
    };
}

vector2_t Wow_McvtCoords(int index) {
    int row = index / 17;
    int col = index % 17;
    vector2_t coords;

    if (col < 9) {
        coords.x = col * WOW_ADT_UNIT_SIZE;
        coords.y = row * WOW_ADT_UNIT_SIZE;
    } else {
        coords.x = (col - 8.5f) * WOW_ADT_UNIT_SIZE;
        coords.y = (row + 0.5f) * WOW_ADT_UNIT_SIZE;
    }
    return coords;
}

vector3_t Wow_McvtPoint(wowVec3_t pos, float const *heights, int index) {
    vector2_t coords = Wow_McvtCoords(index);

    vector3_t base = { pos.x, pos.y, pos.z }, offset = Wow_TerrainOffset(coords.y, coords.x, heights[index]);
    return Vector3_add(&base, &offset);
}

vector3_t Wow_TerrainFaceNormal(vector3_t const * a, vector3_t const * b, vector3_t const * c) {
    vector3_t ab = Vector3_sub(b, a);
    vector3_t ac = Vector3_sub(c, a);
    vector3_t normal = Vector3_cross(&ab, &ac);

    if (Vector3_lengthsq(&normal) <= 0.000001f) {
        return (vector3_t){ 0.0f, 0.0f, 1.0f };
    }

    Vector3_normalize(&normal);
    if (normal.z < 0.0f) {
        normal = Vector3_scale(&normal, -1.0f);
    }
    return normal;
}

static vector3_t Wow_DecodeTerrainNormal(uint8_t const *normals, int index) {
    int base = index * 3;
    signed char nx;
    signed char ny;
    signed char nz;
    vector3_t normal;

    if (!normals) {
        return (vector3_t){ 0.0f, 0.0f, 1.0f };
    }

    nx = (signed char)normals[base + 0];
    ny = (signed char)normals[base + 1];
    nz = (signed char)normals[base + 2];

    normal = Wow_TerrainNormal((vector3_t){ nx / 127.0f, ny / 127.0f, nz / 127.0f });

    if (Vector3_lengthsq(&normal) <= 0.000001f) {
        return (vector3_t){ 0.0f, 0.0f, 1.0f };
    }
    Vector3_normalize(&normal);
    if (normal.z < 0.0f) {
        normal = Vector3_scale(&normal, -1.0f);
    }
    return normal;
}

void Wow_AccumulateTerrainCellNormals(vector3_t normals[WOW_MCVT_COUNT],
                                             wowVec3_t pos,
                                             float const *heights,
                                             int x,
                                             int y) {
    static uint8_t const tri[] = { 9, 0, 17, 9, 1, 0, 9, 18, 1, 9, 17, 18 };
    int base = y * 17 + x;

    for (uint32_t i = 0; i < sizeof(tri) / sizeof(tri[0]); i += 3) {
        int i0 = base + tri[i + 0];
        int i1 = base + tri[i + 1];
        int i2 = base + tri[i + 2];
        vector3_t p0 = Wow_McvtPoint(pos, heights, i0);
        vector3_t p1 = Wow_McvtPoint(pos, heights, i1);
        vector3_t p2 = Wow_McvtPoint(pos, heights, i2);
        vector3_t normal = Wow_TerrainFaceNormal(&p0, &p1, &p2);

        normals[i0] = Vector3_add(&normals[i0], &normal);
        normals[i1] = Vector3_add(&normals[i1], &normal);
        normals[i2] = Vector3_add(&normals[i2], &normal);
    }
}

void Wow_NormalizeTerrainNormals(vector3_t normals[WOW_MCVT_COUNT]) {
    FOR_LOOP(i, WOW_MCVT_COUNT) {
        if (Vector3_lengthsq(&normals[i]) <= 0.000001f) {
            normals[i] = (vector3_t){ 0.0f, 0.0f, 1.0f };
            continue;
        }
        Vector3_normalize(&normals[i]);
    }
}

void Wow_PushTerrainVertex(vertex_t *vertices,
                                  uint32_t * index,
                                  wowVec3_t pos,
                                  float const *heights,
                                  vector3_t const * normal,
                                  int height_index,
                                  color32_t color) {
    vector3_t p = Wow_McvtPoint(pos, heights, height_index);
    vector2_t coords = Wow_McvtCoords(height_index);
    float u = coords.x / WOW_ADT_UNIT_SIZE;
    float v = coords.y / WOW_ADT_UNIT_SIZE;
    vertex_t vertex = Wow_Vertex(p.x, p.y, p.z, u, v, color);
    vertex.normal = *normal;
    vertices[(*index)++] = vertex;
}

bool Wow_IsHole(uint16_t holes, int x, int y) {
    static int holetab_h[4] = { 0x1111, 0x2222, 0x4444, 0x8888 };
    static int holetab_v[4] = { 0x000f, 0x00f0, 0x0f00, 0xf000 };
    x >>= 1;
    y >>= 1;
    return (holes & holetab_h[x] & holetab_v[y]) != 0;
}

void Wow_AddTerrainCell(vertex_t *vertices,
                               uint32_t * index,
                               wowVec3_t pos,
                               float const *heights,
                               vector3_t const normals[WOW_MCVT_COUNT],
                               int x,
                               int y,
                               color32_t const *mccv) {
    static uint8_t const tri[] = { 9, 0, 17, 9, 1, 0, 9, 18, 1, 9, 17, 18 };
    int base = y * 17 + x;
    FOR_LOOP(i, sizeof(tri) / sizeof(tri[0])) {
        int height_index = base + tri[i];
        Wow_PushTerrainVertex(vertices, index, pos, heights, &normals[height_index], height_index, mccv[height_index]);
    }
}

bool Wow_BarycentricHeight(float px,
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

bool Wow_HeightInCell(float const *heights, int row, int col, float fx, float fy, float *height) {
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

bool Wow_TerrainHeightAtPoint(float sx, float sy, float *height) {
    wowAdtChunk_t const *chunk;
    int ix, iy;

    if (!height || !wow_world.has_atlas_origin) return false;
    ix = (int)floorf((wow_world.atlas_world_y - sy) / WOW_ADT_CHUNK_SIZE);
    iy = (int)floorf((wow_world.atlas_world_x - sx) / WOW_ADT_CHUNK_SIZE);
    if (ix < 0 || ix >= WOW_HEIGHT_ATLAS_CHUNKS || iy < 0 || iy >= WOW_HEIGHT_ATLAS_CHUNKS) return false;
    chunk = wow_world.height_chunks[iy][ix];
    if (chunk && chunk->has_heights) {
        float row = (chunk->position.x - sx) / WOW_ADT_UNIT_SIZE;
        float col = (chunk->position.y - sy) / WOW_ADT_UNIT_SIZE;
        int cell_row = (int)floorf(MIN(row, 7.9999f));
        int cell_col = (int)floorf(MIN(col, 7.9999f));
        float cell_height;
        if (cell_row >= 0 && cell_row < 8 && cell_col >= 0 && cell_col < 8 &&
            Wow_HeightInCell(chunk->heights, cell_row, cell_col, row - cell_row, col - cell_col, &cell_height)) {
            *height = chunk->position.z + cell_height;
            return true;
        }
    }
    return false;
}

void Wow_AddAdtChunk(wowVec3_t pos,
                            uint32_t alpha_index_x,
                            uint32_t alpha_index_y,
                            uint16_t holes,
                            uint64_t no_effect_mask,
                            uint8_t const alpha[4][WOW_ALPHA_TEXELS],
                            wowLayer_t const *layers,
                            uint32_t layer_count,
                            char **textures,
                            uint32_t num_textures,
                            float const *heights,
                            uint8_t const *normals,
                            color32_t const *mccv,
                            uint8_t const *mcsh) {
    enum { MAX_VERTICES = 8 * 8 * 12 };
    color32_t mccv_fallback[WOW_MCVT_COUNT];
    /* Vanilla (1.x) ADTs have no MCCV, so fall back to white and let the shader's
       dynamic sun (ambient + diffuse·N·L) light the surface. MCCV only exists in
       WotLK+; TODO: convert its on-disk BGRA bytes to RGBA (like Wow_Color does
       for MOCV) when WotLK terrain data is supported. */
    if (!mccv) {
        color32_t white = Wow_Color(255, 255, 255, 255);
        FOR_LOOP(i, WOW_MCVT_COUNT) mccv_fallback[i] = white;
        mccv = mccv_fallback;
    }
    uint32_t slot_texture_ids[4] = { 0, 0, 0, 0 };
    uint32_t unique_layer_count = Wow_BuildUniqueTextureSlots(layers, layer_count, slot_texture_ids);
    uint32_t effective_layers = MAX(1, MIN(unique_layer_count ? unique_layer_count : layer_count, 4));
    vector3_t derived_normals[WOW_MCVT_COUNT];
    vertex_t *vertices;
    uint32_t num_vertices = 0;
    wowAdtChunk_t *chunk;

    if (!heights) {
        return;
    }
    vertices = ri.MemAlloc(sizeof(vertex_t) * MAX_VERTICES);
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
    chunk->bounds = (box3_t){
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
    /* The atlas indices are the authoritative resident-window MCNK address; queries must not scan every chunk. */
    if (alpha_index_x < WOW_HEIGHT_ATLAS_CHUNKS && alpha_index_y < WOW_HEIGHT_ATLAS_CHUNKS)
        wow_world.height_chunks[alpha_index_y][alpha_index_x] = chunk;
    /* Upload absolute world Z = pos.z + MCVT relative height into GPU atlas */
    Wow_UploadHeightAtlasChunk(alpha_index_x, alpha_index_y, pos.z, chunk->heights);
    /* Any resident chunk defines tile (0,0); waiting for that tile breaks sparse maps and world edges. */
    if (!wow_world.has_atlas_origin) {
        wow_world.atlas_world_x = pos.x + alpha_index_y * WOW_ADT_CHUNK_SIZE;
        wow_world.atlas_world_y = pos.y + alpha_index_x * WOW_ADT_CHUNK_SIZE;
        wow_world.has_atlas_origin = true;
    }
    FOR_LOOP(layer_index, 4) {
        uint32_t texture_id = 0;
        if (layer_index < unique_layer_count) {
            texture_id = slot_texture_ids[layer_index];
        } else if (unique_layer_count > 0) {
            texture_id = slot_texture_ids[0];
        }
        if (texture_id < num_textures && textures[texture_id]) {
            chunk->textures[layer_index] = Wow_LoadTexture(textures[texture_id], true);
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
