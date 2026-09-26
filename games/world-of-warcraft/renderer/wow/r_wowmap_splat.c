#include "r_wowmap.h"

typedef struct {
    texture_t const * texture;
    splat_shader_t *shader;
    uint32_t num_vertices;
    vertex_t vertices[WOW_SPLAT_BATCH_VERTICES];
} wowSplatbatch_t;

static wowSplatbatch_t wow_splat_batches[WOW_SPLAT_BATCHES];

/* Stream one material batch in a single upload/draw pair. */
static void Wow_DrawSplatVertices(texture_t const * texture, splat_shader_t *shader,
                                  vertex_t const * vertices, uint32_t num_vertices) {
    matrix4_t model_matrix;

    if (!texture || !shader || !vertices || !num_vertices) return;
    Matrix4_identity(&model_matrix);
    R_BindTexture(texture, 0);

    shader->state.viewProjection = tr.viewDef.viewProjectionMatrix;
    shader->state.model = model_matrix;
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glEnable, GL_POLYGON_OFFSET_FILL);
    R_Call(glPolygonOffset, -1.0f, -1.0f);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    /* Re-specifying the whole stream buffer lets the driver orphan busy storage. */
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(*vertices) * num_vertices, vertices, GL_STREAM_DRAW);
    R_StatsDraw(GL_TRIANGLES, num_vertices, 1);
    R_ApplyShader(shader);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
    R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
    R_Call(glDepthMask, GL_TRUE);
}

void Wow_FlushSplats(void) {
    FOR_LOOP(i, WOW_SPLAT_BATCHES) {
        wowSplatbatch_t *batch = &wow_splat_batches[i];
        if (!batch->num_vertices) continue;
        Wow_DrawSplatVertices(batch->texture, batch->shader, batch->vertices, batch->num_vertices);
        memset(batch, 0, sizeof(*batch));
    }
}

/* Group splats by material; common blob shadows and selection rings become one draw each. */
static void Wow_QueueSplatVertices(texture_t const * texture, splat_shader_t *shader,
                                   vertex_t const * vertices, uint32_t num_vertices) {
    wowSplatbatch_t *empty = NULL;

    if (!texture || !shader || !vertices || !num_vertices) return;
    if (num_vertices > WOW_SPLAT_BATCH_VERTICES) {
        Wow_FlushSplats();
        Wow_DrawSplatVertices(texture, shader, vertices, num_vertices);
        return;
    }
    FOR_LOOP(i, WOW_SPLAT_BATCHES) {
        wowSplatbatch_t *batch = &wow_splat_batches[i];
        if (!batch->num_vertices) {
            if (!empty) empty = batch;
            continue;
        }
        if (batch->texture != texture || batch->shader != shader) continue;
        if (batch->num_vertices + num_vertices > WOW_SPLAT_BATCH_VERTICES) {
            Wow_DrawSplatVertices(batch->texture, batch->shader, batch->vertices, batch->num_vertices);
            batch->num_vertices = 0;
        }
        empty = batch;
        break;
    }
    if (!empty) {
        Wow_FlushSplats();
        empty = &wow_splat_batches[0];
    }
    empty->texture = texture;
    empty->shader = shader;
    memcpy(empty->vertices + empty->num_vertices, vertices, sizeof(*vertices) * num_vertices);
    empty->num_vertices += num_vertices;
}

bool Wow_MakeSplatVertex(float x,
                                float y,
                                vector2_t const * mins,
                                float width,
                                float height,
                                color32_t color,
                                vertex_t * vertex) {
    float z;

    if (!vertex || !Wow_TerrainHeightAtPoint(x, y, &z)) {
        return false;
    }

    *vertex = Wow_Vertex(x, y, z + WOW_SPLAT_Z_BIAS, (x - mins->x) / width, 1.0f - (y - mins->y) / height, color);
    return true;
}

void Wow_AddSplatTriangle(vertex_t * vertices,
                                 uint32_t * count,
                                 vertex_t a,
                                 vertex_t b,
                                 vertex_t c,
                                 float max_height_delta) {
    float min_z = MIN(a.position.z, MIN(b.position.z, c.position.z));
    float max_z = MAX(a.position.z, MAX(b.position.z, c.position.z));
    vector3_t normal;

    if (max_z - min_z > max_height_delta) {
        return;
    }

    normal = Wow_TerrainFaceNormal(&a.position, &b.position, &c.position);
    a.normal = normal;
    b.normal = normal;
    c.normal = normal;
    vertices[(*count)++] = a;
    vertices[(*count)++] = b;
    vertices[(*count)++] = c;
}

void Wow_DrawTerrainShadows(void) {
}

void R_RenderRectSplat(vector2_t const * mins, vector2_t const * maxs, texture_t const * texture, splat_shader_t *shader, color32_t color) {
    float width;
    float height;
    int cols;
    int rows;
    uint32_t max_vertices;
    uint32_t num_vertices = 0;
    vertex_t stack_vertices[WOW_SPLAT_MIN_SUBDIVISIONS * WOW_SPLAT_MIN_SUBDIVISIONS * 6];
    vertex_t samples[(WOW_SPLAT_MAX_SUBDIVISIONS + 1) * (WOW_SPLAT_MAX_SUBDIVISIONS + 1)];
    uint8_t valid[(WOW_SPLAT_MAX_SUBDIVISIONS + 1) * (WOW_SPLAT_MAX_SUBDIVISIONS + 1)];
    vertex_t *vertices;
    bool vertices_allocated = false;
    float max_height_delta;
    static bool warned_missing_sample;

    if (!mins || !maxs || !texture || !shader) {
        return;
    }

    width = maxs->x - mins->x;
    height = maxs->y - mins->y;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    if (!wow_world.chunks) {
        static bool warned_no_terrain;
        if (!warned_no_terrain) {
            fprintf(stderr, "WoW splat: no terrain samples; drawing flat at z=0\n");
            warned_no_terrain = true;
        }
        R_RenderFlatRectSplat(mins, maxs, 0.0f, texture, shader, color);
        return;
    }

    /* Small selection rings need enough fitted triangles to follow an ADT slope instead of cutting through it. */
    cols = MAX(WOW_SPLAT_MIN_SUBDIVISIONS, MIN(WOW_SPLAT_MAX_SUBDIVISIONS, (int)ceilf(width / (WOW_ADT_UNIT_SIZE * 0.5f))));
    rows = MAX(WOW_SPLAT_MIN_SUBDIVISIONS, MIN(WOW_SPLAT_MAX_SUBDIVISIONS, (int)ceilf(height / (WOW_ADT_UNIT_SIZE * 0.5f))));
    max_vertices = (uint32_t)(cols * rows * 6);
    vertices = max_vertices <= sizeof(stack_vertices) / sizeof(stack_vertices[0])
        ? stack_vertices : ri.MemAlloc(sizeof(*vertices) * max_vertices);
    if (!vertices) return;
    vertices_allocated = vertices != stack_vertices;

    /* TODO: move height projection to the vertex shader once terrain heights live in a GPU atlas. */
    max_height_delta = MAX(WOW_SPLAT_MAX_HEIGHT_DELTA, MIN(width, height) * 0.75f);
    float fallback_z = 0.0f;
    bool have_fallback = false;
    for (int y = 0; y <= rows; y++) {
        float sy = LerpNumber(mins->y, maxs->y, (float)y / (float)rows);
        for (int x = 0; x <= cols; x++) {
            float sx = LerpNumber(mins->x, maxs->x, (float)x / (float)cols);
            uint32_t index = (uint32_t)(y * (cols + 1) + x);
            valid[index] = Wow_MakeSplatVertex(sx, sy, mins, width, height, color, &samples[index]);
            if (valid[index]) {
                fallback_z = samples[index].position.z; /* already includes WOW_SPLAT_Z_BIAS */
                have_fallback = true;
            }
        }
    }
    for (int y = 0; y < rows; y++) {
        float y0 = LerpNumber(mins->y, maxs->y, (float)y / (float)rows);
        float y1 = LerpNumber(mins->y, maxs->y, (float)(y + 1) / (float)rows);
        for (int x = 0; x < cols; x++) {
            float x0 = LerpNumber(mins->x, maxs->x, (float)x / (float)cols);
            float x1 = LerpNumber(mins->x, maxs->x, (float)(x + 1) / (float)cols);
            uint32_t i00 = (uint32_t)(y * (cols + 1) + x);
            uint32_t i10 = i00 + 1;
            uint32_t i01 = i00 + (uint32_t)(cols + 1);
            uint32_t i11 = i01 + 1;
            vertex_t v00;
            vertex_t v10;
            vertex_t v11;
            vertex_t v01;

            if (valid[i00] && valid[i10] && valid[i11] && valid[i01]) {
                v00 = samples[i00]; v10 = samples[i10]; v11 = samples[i11]; v01 = samples[i01];
                Wow_AddSplatTriangle(vertices, &num_vertices, v00, v10, v11, max_height_delta);
                Wow_AddSplatTriangle(vertices, &num_vertices, v00, v11, v01, max_height_delta);
            } else {
                float cell_z;

                /* A missing edge sample must not erase a whole small splat; sample the cell center, then fall
                 * back to the nearest lattice height instead of flattening the whole ring to z=0. */
                if (!Wow_TerrainHeightAtPoint((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, &cell_z)) {
                    if (!have_fallback) {
                        if (!warned_missing_sample) {
                            fprintf(stderr, "WoW splat: no terrain sample available; drawing flat at z=0\n");
                            warned_missing_sample = true;
                        }
                        cell_z = 0.0f;
                    } else {
                        if (!warned_missing_sample) {
                            fprintf(stderr, "WoW splat: terrain cell height missing; reusing lattice height\n");
                            warned_missing_sample = true;
                        }
                        cell_z = fallback_z - WOW_SPLAT_Z_BIAS;
                    }
                }
                cell_z += WOW_SPLAT_Z_BIAS;
                v00 = Wow_Vertex(x0, y0, cell_z, (x0 - mins->x) / width, 1.0f - (y0 - mins->y) / height, color);
                v10 = Wow_Vertex(x1, y0, cell_z, (x1 - mins->x) / width, 1.0f - (y0 - mins->y) / height, color);
                v11 = Wow_Vertex(x1, y1, cell_z, (x1 - mins->x) / width, 1.0f - (y1 - mins->y) / height, color);
                v01 = Wow_Vertex(x0, y1, cell_z, (x0 - mins->x) / width, 1.0f - (y1 - mins->y) / height, color);
                Wow_AddSplatTriangle(vertices, &num_vertices, v00, v10, v11, max_height_delta);
                Wow_AddSplatTriangle(vertices, &num_vertices, v00, v11, v01, max_height_delta);
            }
        }
    }

    if (!num_vertices) {
        if (vertices_allocated) ri.MemFree(vertices);
        return;
    }

    Wow_QueueSplatVertices(texture, shader, vertices, num_vertices);
    if (vertices_allocated) ri.MemFree(vertices);
}

void R_RenderFlatRectSplat(vector2_t const * mins, vector2_t const * maxs, float z,
                           texture_t const * texture, splat_shader_t *shader, color32_t color) {
    float width;
    float height;
    vertex_t vertices[6];

    if (!mins || !maxs || !texture || !shader) {
        return;
    }

    width = maxs->x - mins->x;
    height = maxs->y - mins->y;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    vertices[0] = Wow_Vertex(mins->x, mins->y, z, 0.0f, 1.0f, color);
    vertices[1] = Wow_Vertex(maxs->x, mins->y, z, 1.0f, 1.0f, color);
    vertices[2] = Wow_Vertex(maxs->x, maxs->y, z, 1.0f, 0.0f, color);
    vertices[3] = Wow_Vertex(mins->x, mins->y, z, 0.0f, 1.0f, color);
    vertices[4] = Wow_Vertex(maxs->x, maxs->y, z, 1.0f, 0.0f, color);
    vertices[5] = Wow_Vertex(mins->x, maxs->y, z, 0.0f, 0.0f, color);

    Wow_QueueSplatVertices(texture, shader, vertices, 6);
}

void R_RenderSplat(vector2_t const * position, float radius, texture_t const * texture, splat_shader_t *shader, color32_t color) {
    if (!position || radius <= 0.0f) return;
    vector2_t mins = { .x = position->x - radius, .y = position->y - radius };
    vector2_t maxs = { .x = position->x + radius, .y = position->y + radius };
    R_RenderRectSplat(&mins, &maxs, texture, shader, color);
}

/* WoW shadows are drawn by R_RenderShadow (returns true) and splats already
 * batch through Wow_QueueSplatVertices, so the shared batch API stays immediate. */
static splat_shader_t *wow_batch_shader;
void R_BeginSplatBatch(splat_shader_t *shader) { wow_batch_shader = shader; }
void R_AddRectSplat(vector2_t const * mins, vector2_t const * maxs, texture_t const * texture, color32_t color) {
    R_RenderRectSplat(mins, maxs, texture, wow_batch_shader, color);
}
void R_EndSplatBatch(void) { }

vector2_t GetWar3MapSize(war3map_t const * war3Map) {
    (void)war3Map;
    return (vector2_t){ 0.0f, 0.0f };
}

float GetAccurateHeightAtPoint(float sx, float sy) {
    float height;
    if (Wow_TerrainHeightAtPoint(sx, sy, &height)) {
        return height;
    }
    return 0.0f;
}

float Wow_GetHeightAtPoint(float x, float y) {
    return GetAccurateHeightAtPoint(x, y);
}

bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, vector3_t * output) {
    line3_t const line = R_LineForScreenPoint(viewdef, x, y);
    float const dz = line.b.z - line.a.z;
    float t;

    if (fabsf(dz) < 0.0001f || !output) {
        return false;
    }

    t = -line.a.z / dz;
    output->x = line.a.x + (line.b.x - line.a.x) * t;
    output->y = line.a.y + (line.b.y - line.a.y) * t;
    output->z = 0.0f;
    return true;
}
