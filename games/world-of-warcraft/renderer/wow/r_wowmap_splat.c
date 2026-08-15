#include "r_wowmap.h"

typedef struct {
    LPCTEXTURE texture;
    LPCSHADER shader;
    DWORD num_vertices;
    VERTEX vertices[WOW_SPLAT_BATCH_VERTICES];
} WOWSPLATBATCH;

static WOWSPLATBATCH wow_splat_batches[WOW_SPLAT_BATCHES];

/* Stream one material batch in a single upload/draw pair. */
static void Wow_DrawSplatVertices(LPCTEXTURE texture, LPCSHADER shader,
                                  LPCVERTEX vertices, DWORD num_vertices) {
    MATRIX4 model_matrix;

    if (!texture || !shader || !vertices || !num_vertices) return;
    Matrix4_identity(&model_matrix);
    R_BindTexture(texture, 0);
    R_Call(glUseProgram, shader->progid);
    R_Call(glUniformMatrix4fv, shader->uViewProjectionMatrix, 1, GL_FALSE, tr.viewDef.viewProjectionMatrix.v);
    R_Call(glUniformMatrix4fv, shader->uModelMatrix, 1, GL_FALSE, model_matrix.v);
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    R_Call(glDepthMask, GL_FALSE);
    R_Call(glEnable, GL_POLYGON_OFFSET_FILL);
    R_Call(glPolygonOffset, -1.0f, -1.0f);
    R_Call(glBindVertexArray, tr.buffer[RBUF_TEMP1]->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, tr.buffer[RBUF_TEMP1]->vbo);
    /* Re-specifying the whole stream buffer lets the driver orphan busy storage. */
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(*vertices) * num_vertices, vertices, GL_STREAM_DRAW);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
    R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
    R_Call(glDepthMask, GL_TRUE);
}

void Wow_FlushSplats(void) {
    FOR_LOOP(i, WOW_SPLAT_BATCHES) {
        WOWSPLATBATCH *batch = &wow_splat_batches[i];
        if (!batch->num_vertices) continue;
        Wow_DrawSplatVertices(batch->texture, batch->shader, batch->vertices, batch->num_vertices);
        memset(batch, 0, sizeof(*batch));
    }
}

/* Group splats by material; common blob shadows and selection rings become one draw each. */
static void Wow_QueueSplatVertices(LPCTEXTURE texture, LPCSHADER shader,
                                   LPCVERTEX vertices, DWORD num_vertices) {
    WOWSPLATBATCH *empty = NULL;

    if (!texture || !shader || !vertices || !num_vertices) return;
    if (num_vertices > WOW_SPLAT_BATCH_VERTICES) {
        Wow_FlushSplats();
        Wow_DrawSplatVertices(texture, shader, vertices, num_vertices);
        return;
    }
    FOR_LOOP(i, WOW_SPLAT_BATCHES) {
        WOWSPLATBATCH *batch = &wow_splat_batches[i];
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

BOOL Wow_MakeSplatVertex(float x,
                                float y,
                                LPCVECTOR2 mins,
                                float width,
                                float height,
                                COLOR32 color,
                                LPVERTEX vertex) {
    float z;

    if (!vertex || !Wow_TerrainHeightAtPoint(x, y, &z)) {
        return false;
    }

    *vertex = Wow_Vertex(x, y, z + WOW_SPLAT_Z_BIAS, (x - mins->x) / width, 1.0f - (y - mins->y) / height, color);
    return true;
}

void Wow_AddSplatTriangle(LPVERTEX vertices,
                                 LPDWORD count,
                                 VERTEX a,
                                 VERTEX b,
                                 VERTEX c,
                                 float max_height_delta) {
    float min_z = MIN(a.position.z, MIN(b.position.z, c.position.z));
    float max_z = MAX(a.position.z, MAX(b.position.z, c.position.z));
    VECTOR3 normal;

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

void R_DrawTerrainShadows(void) {
}

void R_RenderRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color) {
    float width;
    float height;
    int cols;
    int rows;
    DWORD max_vertices;
    DWORD num_vertices = 0;
    VERTEX stack_vertices[WOW_SPLAT_MIN_SUBDIVISIONS * WOW_SPLAT_MIN_SUBDIVISIONS * 6];
    VERTEX samples[(WOW_SPLAT_MAX_SUBDIVISIONS + 1) * (WOW_SPLAT_MAX_SUBDIVISIONS + 1)];
    BYTE valid[(WOW_SPLAT_MAX_SUBDIVISIONS + 1) * (WOW_SPLAT_MAX_SUBDIVISIONS + 1)];
    VERTEX *vertices;
    BOOL vertices_allocated = false;
    float max_height_delta;
    static BOOL warned_missing_sample;

    if (!mins || !maxs || !texture || !shader) {
        return;
    }

    width = maxs->x - mins->x;
    height = maxs->y - mins->y;
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    if (!wow_world.chunks) {
        static BOOL warned_no_terrain;
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
    max_vertices = (DWORD)(cols * rows * 6);
    vertices = max_vertices <= sizeof(stack_vertices) / sizeof(stack_vertices[0])
        ? stack_vertices : ri.MemAlloc(sizeof(*vertices) * max_vertices);
    if (!vertices) return;
    vertices_allocated = vertices != stack_vertices;

    /* TODO: move height projection to the vertex shader once terrain heights live in a GPU atlas. */
    max_height_delta = MAX(WOW_SPLAT_MAX_HEIGHT_DELTA, MIN(width, height) * 0.75f);
    for (int y = 0; y <= rows; y++) {
        float sy = LerpNumber(mins->y, maxs->y, (float)y / (float)rows);
        for (int x = 0; x <= cols; x++) {
            float sx = LerpNumber(mins->x, maxs->x, (float)x / (float)cols);
            DWORD index = (DWORD)(y * (cols + 1) + x);
            valid[index] = Wow_MakeSplatVertex(sx, sy, mins, width, height, color, &samples[index]);
        }
    }
    for (int y = 0; y < rows; y++) {
        float y0 = LerpNumber(mins->y, maxs->y, (float)y / (float)rows);
        float y1 = LerpNumber(mins->y, maxs->y, (float)(y + 1) / (float)rows);
        for (int x = 0; x < cols; x++) {
            float x0 = LerpNumber(mins->x, maxs->x, (float)x / (float)cols);
            float x1 = LerpNumber(mins->x, maxs->x, (float)(x + 1) / (float)cols);
            DWORD i00 = (DWORD)(y * (cols + 1) + x);
            DWORD i10 = i00 + 1;
            DWORD i01 = i00 + (DWORD)(cols + 1);
            DWORD i11 = i01 + 1;
            VERTEX v00;
            VERTEX v10;
            VERTEX v11;
            VERTEX v01;

            if (valid[i00] && valid[i10] && valid[i11] && valid[i01]) {
                v00 = samples[i00]; v10 = samples[i10]; v11 = samples[i11]; v01 = samples[i01];
                Wow_AddSplatTriangle(vertices, &num_vertices, v00, v10, v11, max_height_delta);
                Wow_AddSplatTriangle(vertices, &num_vertices, v00, v11, v01, max_height_delta);
            } else {
                float center_z;

                /* A missing edge sample must not erase a whole small splat; sample its center and keep it visible. */
                if (!Wow_TerrainHeightAtPoint((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, &center_z)) {
                    if (!warned_missing_sample) {
                        fprintf(stderr, "WoW splat: terrain center sample missing; drawing flat at z=0\n");
                        warned_missing_sample = true;
                    }
                    R_RenderFlatRectSplat(mins, maxs, 0.0f, texture, shader, color);
                    if (vertices_allocated) ri.MemFree(vertices);
                    return;
                }
                if (!warned_missing_sample) {
                    fprintf(stderr, "WoW splat: terrain edge sample missing; using center sample\n");
                    warned_missing_sample = true;
                }
                v00 = Wow_Vertex(x0, y0, center_z + WOW_SPLAT_Z_BIAS, (x0 - mins->x) / width, 1.0f - (y0 - mins->y) / height, color);
                v10 = Wow_Vertex(x1, y0, center_z + WOW_SPLAT_Z_BIAS, (x1 - mins->x) / width, 1.0f - (y0 - mins->y) / height, color);
                v11 = Wow_Vertex(x1, y1, center_z + WOW_SPLAT_Z_BIAS, (x1 - mins->x) / width, 1.0f - (y1 - mins->y) / height, color);
                v01 = Wow_Vertex(x0, y1, center_z + WOW_SPLAT_Z_BIAS, (x0 - mins->x) / width, 1.0f - (y1 - mins->y) / height, color);
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

void R_RenderFlatRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, FLOAT z,
                           LPCTEXTURE texture, LPCSHADER shader, COLOR32 color) {
    float width;
    float height;
    VERTEX vertices[6];

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

void R_RenderSplat(LPCVECTOR2 position, float radius, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color) {
    if (!position || radius <= 0.0f) return;
    VECTOR2 mins = { .x = position->x - radius, .y = position->y - radius };
    VECTOR2 maxs = { .x = position->x + radius, .y = position->y + radius };
    R_RenderRectSplat(&mins, &maxs, texture, shader, color);
}

VECTOR2 GetWar3MapSize(LPCWAR3MAP war3Map) {
    (void)war3Map;
    return (VECTOR2){ 0.0f, 0.0f };
}

float GetAccurateHeightAtPoint(float sx, float sy) {
    float height;
    if (Wow_TerrainHeightAtPoint(sx, sy, &height)) {
        return height;
    }
    return 0.0f;
}

FLOAT R_GetHeightAtPoint(FLOAT x, FLOAT y) {
    return GetAccurateHeightAtPoint(x, y);
}

bool R_TraceLocation(viewDef_t const *viewdef, FLOAT x, FLOAT y, LPVECTOR3 output) {
    LINE3 const line = R_LineForScreenPoint(viewdef, x, y);
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
