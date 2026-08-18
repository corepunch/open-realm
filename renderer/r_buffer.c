#include "r_local.h"

struct vshort {
    float x, y, z;
    float u, v;
    color32_t color;
};

#define WRITE_VERTICES(buffer, data) \
    for (int i = 0; i < sizeof(data) / sizeof(*data); i++) { \
        buffer[i].position.x = data[i].x; \
        buffer[i].position.y = data[i].y; \
        buffer[i].position.z = data[i].z; \
        buffer[i].texcoord.x = data[i].u; \
        buffer[i].texcoord.y = data[i].v; \
        buffer[i].color = data[i].color; \
    } \
    return buffer + sizeof(data) / sizeof(*data);

VERTEX *R_AddQuad(VERTEX *buffer, LPCRECT screen, LPCRECT uv, COLOR32 color, float z) {
    struct vshort const data[] = {
        { screen->x, screen->y, z, uv->x, uv->y, color },
        { screen->x+screen->w, screen->y, z, uv->x+uv->w, uv->y, color },
        { screen->x+screen->w, screen->y+screen->h, z, uv->x+uv->w, uv->y+uv->h, color },
        { screen->x, screen->y, z, uv->x, uv->y, color },
        { screen->x+screen->w, screen->y+screen->h, z, uv->x+uv->w, uv->y+uv->h, color },
        { screen->x, screen->y+screen->h, z, uv->x, uv->y+uv->h, color },
    };
    WRITE_VERTICES(buffer, data);
}

VERTEX *R_AddStrip(VERTEX *buffer, LPCRECT screen, COLOR32 color) {
    struct vshort const data[] = {
        { screen->x, screen->y, 0, 0, 0, color },
        { screen->x+screen->w, screen->y, 0, 0, 0, color },
        { screen->x+screen->w, screen->y+screen->h, 0, 0, 0, color },
        { screen->x, screen->y+screen->h, 0, 0, 0, color },
        { screen->x, screen->y, 0, 0, 0, color },
    };
    WRITE_VERTICES(buffer, data);
}

VERTEX *R_AddWireBox(VERTEX *buffer, LPCBOX3 box, COLOR32 color) {
    struct vshort const data[] = {
        { box->min.x, box->min.y, box->min.z, 0, 0, color },
        { box->max.x, box->min.y, box->min.z, 0, 0, color },
        { box->max.x, box->min.y, box->max.z, 0, 0, color },
        { box->min.x, box->min.y, box->max.z, 0, 0, color },
        { box->min.x, box->min.y, box->min.z, 0, 0, color },
        { box->min.x, box->max.y, box->min.z, 0, 0, color },
        { box->max.x, box->max.y, box->min.z, 0, 0, color },
        { box->max.x, box->max.y, box->max.z, 0, 0, color },
        { box->min.x, box->max.y, box->max.z, 0, 0, color },
        { box->min.x, box->max.y, box->min.z, 0, 0, color },
        { box->min.x, box->min.y, box->min.z, 0, 0, color },
        { box->min.x, box->max.y, box->min.z, 0, 0, color },
        { box->max.x, box->min.y, box->min.z, 0, 0, color },
        { box->max.x, box->max.y, box->min.z, 0, 0, color },
        { box->max.x, box->min.y, box->max.z, 0, 0, color },
        { box->max.x, box->max.y, box->max.z, 0, 0, color },
        { box->min.x, box->min.y, box->max.z, 0, 0, color },
        { box->min.x, box->max.y, box->max.z, 0, 0, color },
    };
    WRITE_VERTICES(buffer, data);
}

LPBUFFER R_MakeVertexArrayObject(LPCVERTEX vertices, DWORD size) {
    LPBUFFER buf = ri.MemAlloc(sizeof(BUFFER));

    memset(buf, 0, sizeof(*buf));
    R_Call(glGenVertexArrays, 1, &buf->vao);
    R_Call(glBindVertexArray, buf->vao);
   
    R_Call(glGenBuffers, 1, &buf->vbo);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buf->vbo);

    R_Call(glEnableVertexAttribArray, attrib_position);
    R_Call(glEnableVertexAttribArray, attrib_color);
    R_Call(glEnableVertexAttribArray, attrib_texcoord);
    R_Call(glEnableVertexAttribArray, attrib_skin1);
    R_Call(glEnableVertexAttribArray, attrib_boneWeight1);
    R_Call(glEnableVertexAttribArray, attrib_normal);

    R_Call(glVertexAttribPointer, attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), FOFS(vertex, color));
    R_Call(glVertexAttribPointer, attrib_position, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), FOFS(vertex, position));
    R_Call(glVertexAttribPointer, attrib_texcoord, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), FOFS(vertex, texcoord));
    R_Call(glVertexAttribPointer, attrib_skin1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(struct vertex), FOFS(vertex, skin[0]));
    R_Call(glVertexAttribPointer, attrib_boneWeight1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), FOFS(vertex, boneWeight[0]));
    R_Call(glVertexAttribPointer, attrib_normal, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), FOFS(vertex, normal));


    if (vertices) {
        R_Call(glBufferData, GL_ARRAY_BUFFER, size * sizeof(VERTEX), vertices, GL_STATIC_DRAW);
    }

    return buf;
}

LPBUFFER R_MakeIndexedVertexArrayObject(LPCVERTEX vertices, DWORD num_vertices, DWORD const *indices, DWORD num_indices) {
    LPBUFFER buf = R_MakeVertexArrayObject(vertices, num_vertices);

    R_Call(glBindVertexArray, buf->vao);
    R_Call(glGenBuffers, 1, &buf->ibo);
    R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, buf->ibo);
    R_Call(glBufferData, GL_ELEMENT_ARRAY_BUFFER, num_indices * sizeof(*indices), indices, GL_STATIC_DRAW);
    return buf;
}

/* Static instance transforms are immutable until their ADT window is replaced. */
static GLuint r_instanced_vao = 0;

BOOL R_MakeInstanceBuffer(LPINSTANCEBUFFER buffer, LPCMATRIX4 matrices, DWORD count) {
    if (!buffer || !matrices || !count) return false;
    memset(buffer, 0, sizeof(*buffer));
    R_Call(glGenBuffers, 1, &buffer->vbo);
    if (!buffer->vbo) return false;
    buffer->count = count; buffer->capacity = count;
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    R_Call(glBufferData, GL_ARRAY_BUFFER, R_InstanceBufferBytes(count), matrices, GL_STATIC_DRAW);
    return true;
}

/* Visible static doodads regroup by model each frame; retain their VBO allocation across frames. */
BOOL R_UpdateInstanceBuffer(LPINSTANCEBUFFER buffer, LPCMATRIX4 matrices, DWORD count) {
    DWORD capacity;

    if (!buffer || !matrices || !count) return false;
    if (!buffer->vbo) R_Call(glGenBuffers, 1, &buffer->vbo);
    if (!buffer->vbo) return false;
    capacity = R_InstanceBufferCapacity(buffer->capacity, count);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    if (capacity != buffer->capacity) {
        R_Call(glBufferData, GL_ARRAY_BUFFER, R_InstanceBufferBytes(capacity), NULL, GL_STREAM_DRAW);
        buffer->capacity = capacity;
    }
    R_Call(glBufferSubData, GL_ARRAY_BUFFER, 0, R_InstanceBufferBytes(count), matrices);
    buffer->count = count;
    return true;
}

void R_ReleaseInstanceBuffer(LPINSTANCEBUFFER buffer) {
    if (!buffer) return;
    if (buffer->vbo) R_Call(glDeleteBuffers, 1, &buffer->vbo);
    memset(buffer, 0, sizeof(*buffer));
}

/* Rebind one model batch and its persistent instance stream to the shared VAO. */
void R_DrawBufferInstanced(LPCBUFFER buffer, DWORD num_vertices, LPCINSTANCEBUFFER instances) {
    if (!buffer || !num_vertices || !instances || !instances->vbo || !instances->count) return;
    if (!r_instanced_vao) {
        R_Call(glGenVertexArrays, 1, &r_instanced_vao);
    }

    R_Call(glBindVertexArray, r_instanced_vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, buffer->vbo);
    R_Call(glEnableVertexAttribArray, attrib_position);
    R_Call(glEnableVertexAttribArray, attrib_color);
    R_Call(glEnableVertexAttribArray, attrib_texcoord);
    R_Call(glEnableVertexAttribArray, attrib_skin1);
    R_Call(glEnableVertexAttribArray, attrib_boneWeight1);
    R_Call(glEnableVertexAttribArray, attrib_normal);
    R_Call(glVertexAttribPointer, attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), FOFS(vertex, color));
    R_Call(glVertexAttribPointer, attrib_position, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), FOFS(vertex, position));
    R_Call(glVertexAttribPointer, attrib_texcoord, 2, GL_FLOAT, GL_FALSE, sizeof(struct vertex), FOFS(vertex, texcoord));
    R_Call(glVertexAttribPointer, attrib_skin1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(struct vertex), FOFS(vertex, skin[0]));
    R_Call(glVertexAttribPointer, attrib_boneWeight1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(struct vertex), FOFS(vertex, boneWeight[0]));
    R_Call(glVertexAttribPointer, attrib_normal, 3, GL_FLOAT, GL_FALSE, sizeof(struct vertex), FOFS(vertex, normal));

    /* Instance matrices were uploaded once at ADT-window construction, not once per frame. */
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, instances->vbo);
    for (int i = 0; i < 4; i++) {
        R_Call(glEnableVertexAttribArray, attrib_instance0 + i);
        R_Call(glVertexAttribPointer, attrib_instance0 + i, 4, GL_FLOAT, GL_FALSE, sizeof(MATRIX4), (void *)(i * 4 * sizeof(float)));
        R_Call(glVertexAttribDivisor, attrib_instance0 + i, 1);
    }

    R_StatsDraw(GL_TRIANGLES, num_vertices, instances->count);
    R_Call(glDrawArraysInstanced, GL_TRIANGLES, 0, num_vertices, instances->count);

    for (int i = 0; i < 4; i++) {
        R_Call(glVertexAttribDivisor, attrib_instance0 + i, 0);
    }
}

/* Free the lazily-created shared VAO; owners release their immutable instance VBOs. */
void R_ShutdownDrawBufferInstanced(void) {
    if (r_instanced_vao) {
        R_Call(glDeleteVertexArrays, 1, &r_instanced_vao);
        r_instanced_vao = 0;
    }
}

void R_ReleaseVertexArrayObject(LPBUFFER buffer) {
    if (!buffer) {
        return;
    }
    if (buffer->ibo)
        R_Call(glDeleteBuffers, 1, &buffer->ibo);
    R_Call(glDeleteBuffers, 1, &buffer->vbo);
    R_Call(glDeleteVertexArrays, 1, &buffer->vao);
    buffer->ibo = 0;
    buffer->vbo = 0;
    buffer->vao = 0;
    ri.MemFree(buffer);
}
