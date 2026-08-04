#include "r_wowmap.h"

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

    *vertex = Wow_Vertex(x,
                         y,
                         z + WOW_SPLAT_Z_BIAS,
                         (x - mins->x) / width,
                         1.0f - (y - mins->y) / height,
                         color);
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
    MATRIX4 model_matrix;
    float width;
    float height;
    int cols;
    int rows;
    DWORD max_vertices;
    DWORD num_vertices = 0;
    VERTEX *vertices;
    float max_height_delta;

    if (!mins || !maxs || !texture || !shader) {
        return;
    }

    width = maxs->x - mins->x;
    height = maxs->y - mins->y;
    if (width <= 0.0f || height <= 0.0f || !wow_world.chunks) {
        return;
    }

    cols = MAX(1, MIN(WOW_SPLAT_MAX_SUBDIVISIONS, (int)ceilf(width / (WOW_ADT_UNIT_SIZE * 0.5f))));
    rows = MAX(1, MIN(WOW_SPLAT_MAX_SUBDIVISIONS, (int)ceilf(height / (WOW_ADT_UNIT_SIZE * 0.5f))));
    max_vertices = (DWORD)(cols * rows * 6);
    vertices = ri.MemAlloc(sizeof(*vertices) * max_vertices);
    if (!vertices) {
        return;
    }

    max_height_delta = MAX(WOW_SPLAT_MAX_HEIGHT_DELTA, MIN(width, height) * 0.75f);
    for (int y = 0; y < rows; y++) {
        float y0 = LerpNumber(mins->y, maxs->y, (float)y / (float)rows);
        float y1 = LerpNumber(mins->y, maxs->y, (float)(y + 1) / (float)rows);
        for (int x = 0; x < cols; x++) {
            float x0 = LerpNumber(mins->x, maxs->x, (float)x / (float)cols);
            float x1 = LerpNumber(mins->x, maxs->x, (float)(x + 1) / (float)cols);
            VERTEX v00;
            VERTEX v10;
            VERTEX v11;
            VERTEX v01;

            if (!Wow_MakeSplatVertex(x0, y0, mins, width, height, color, &v00) ||
                !Wow_MakeSplatVertex(x1, y0, mins, width, height, color, &v10) ||
                !Wow_MakeSplatVertex(x1, y1, mins, width, height, color, &v11) ||
                !Wow_MakeSplatVertex(x0, y1, mins, width, height, color, &v01)) {
                continue;
            }

            Wow_AddSplatTriangle(vertices, &num_vertices, v00, v10, v11, max_height_delta);
            Wow_AddSplatTriangle(vertices, &num_vertices, v00, v11, v01, max_height_delta);
        }
    }

    if (!num_vertices) {
        ri.MemFree(vertices);
        return;
    }

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
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(*vertices) * num_vertices, vertices, GL_STREAM_DRAW);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, num_vertices);
    R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
    R_Call(glDepthMask, GL_TRUE);
    ri.MemFree(vertices);
}

void R_RenderFlatRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, FLOAT z, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color) {
    MATRIX4 model_matrix;
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
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
    R_Call(glDrawArrays, GL_TRIANGLES, 0, 6);
    R_Call(glDisable, GL_POLYGON_OFFSET_FILL);
    R_Call(glDepthMask, GL_TRUE);
}

void R_RenderSplat(LPCVECTOR2 position, float radius, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color) {
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
