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

vertex_t *R_AddQuad(vertex_t *buffer, rect_t const *screen, rect_t const *uv, color32_t color, float z) {
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

vertex_t *R_AddStrip(vertex_t *buffer, rect_t const *screen, color32_t color) {
    struct vshort const data[] = {
        { screen->x, screen->y, 0, 0, 0, color },
        { screen->x+screen->w, screen->y, 0, 0, 0, color },
        { screen->x+screen->w, screen->y+screen->h, 0, 0, 0, color },
        { screen->x, screen->y+screen->h, 0, 0, 0, color },
        { screen->x, screen->y, 0, 0, 0, color },
    };
    WRITE_VERTICES(buffer, data);
}

vertex_t *R_AddWireBox(vertex_t *buffer, box3_t const *box, color32_t color) {
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

buffer_t *R_MakeVertexArrayObject(vertex_t const *vertices, uint32_t size) {
    buffer_t *buf = ri.MemAlloc(sizeof(buffer_t));

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
        R_Call(glBufferData, GL_ARRAY_BUFFER, size * sizeof(vertex_t), vertices, GL_STATIC_DRAW);
    }

    return buf;
}

buffer_t *R_MakeIndexedVertexArrayObject(vertex_t const *vertices, uint32_t num_vertices, uint32_t const *indices, uint32_t num_indices) {
    buffer_t *buf = R_MakeVertexArrayObject(vertices, num_vertices);

    R_Call(glBindVertexArray, buf->vao);
    R_Call(glGenBuffers, 1, &buf->ibo);
    R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, buf->ibo);
    R_Call(glBufferData, GL_ELEMENT_ARRAY_BUFFER, num_indices * sizeof(*indices), indices, GL_STATIC_DRAW);
    return buf;
}

/* Static instance transforms are immutable until their ADT window is replaced. */
static GLuint r_instanced_vao = 0;

bool R_MakeInstanceBuffer(instanceBuffer_t *buffer, matrix4_t const *matrices, uint32_t count) {
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
bool R_UpdateInstanceBuffer(instanceBuffer_t *buffer, matrix4_t const *matrices, uint32_t count) {
    uint32_t capacity;

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

void R_ReleaseInstanceBuffer(instanceBuffer_t *buffer) {
    if (!buffer) return;
    if (buffer->vbo) R_Call(glDeleteBuffers, 1, &buffer->vbo);
    memset(buffer, 0, sizeof(*buffer));
}

/* Both array and indexed draws share one transient VAO for their persistent instance stream. */
static bool R_BindInstancedBuffer(buffer_t const *buffer, instanceBuffer_t const *instances) {
    if (!buffer || !instances || !instances->vbo || !instances->count) return false;
    if (!r_instanced_vao) R_Call(glGenVertexArrays, 1, &r_instanced_vao);
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

    R_Call(glBindBuffer, GL_ARRAY_BUFFER, instances->vbo);
    FOR_LOOP(i, 4) {
        R_Call(glEnableVertexAttribArray, attrib_instance + i);
        R_Call(glVertexAttribPointer, attrib_instance + i, 4, GL_FLOAT, GL_FALSE,
            sizeof(matrix4_t), (void *)(i * 4 * sizeof(float)));
        R_Call(glVertexAttribDivisor, attrib_instance + i, 1);
    }
    return true;
}

static void R_ResetInstancedBuffer(void) { FOR_LOOP(i, 4) R_Call(glVertexAttribDivisor, attrib_instance + i, 0); }

/* Draw one subrange from a packed array buffer without manufacturing sequential indices. */
void R_DrawBufferRange(buffer_t const *buffer, drawRange_t const *draw) {
    if (!buffer || !draw || !draw->count) return;
    R_Call(glBindVertexArray, buffer->vao);
    R_StatsDraw(GL_TRIANGLES, draw->count, 1);
    R_Call(glDrawArrays, GL_TRIANGLES, draw->first, draw->count);
}

/* Rebind one model batch and its persistent instance stream to the shared VAO. */
void R_DrawBufferInstanced(buffer_t const *buffer, uint32_t num_vertices, instanceBuffer_t const *instances) {
    if (!num_vertices || !R_BindInstancedBuffer(buffer, instances)) return;
    R_StatsDraw(GL_TRIANGLES, num_vertices, instances->count);
    R_Call(glDrawArraysInstanced, GL_TRIANGLES, 0, num_vertices, instances->count);
    R_ResetInstancedBuffer();
}

/* Packed batch-expanded M2 geometry shares one VBO, so submit its vertex subrange without a redundant identity EBO. */
void R_DrawBufferRangeInstanced(buffer_t const *buffer, drawRange_t const *draw, instanceBuffer_t const *instances) {
    if (!draw || !draw->count || !R_BindInstancedBuffer(buffer, instances)) return;
    R_StatsDraw(GL_TRIANGLES, draw->count, instances->count);
    R_Call(glDrawArraysInstanced, GL_TRIANGLES, draw->first, draw->count, instances->count);
    R_ResetInstancedBuffer();
}

/* M2 sections share model geometry while each visible group keeps its authored index range. */
void R_DrawIndexedBuffer16Instanced(buffer_t const *buffer, drawElements_t const *draw, instanceBuffer_t const *instances) {
    if (!draw || !draw->count || !R_BindInstancedBuffer(buffer, instances)) return;
    R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, buffer->ibo);
    R_StatsDraw(GL_TRIANGLES, draw->count, instances->count);
    R_Call(glDrawElementsInstanced, GL_TRIANGLES, draw->count, GL_UNSIGNED_SHORT,
        (void *)(uintptr_t)draw->offset, instances->count);
    R_ResetInstancedBuffer();
}

void R_DrawIndexedBuffer32Instanced(buffer_t const *buffer, drawElements_t const *draw, instanceBuffer_t const *instances) {
    if (!draw || !draw->count || !R_BindInstancedBuffer(buffer, instances)) return;
    R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, buffer->ibo);
    R_StatsDraw(GL_TRIANGLES, draw->count, instances->count);
    R_Call(glDrawElementsInstanced, GL_TRIANGLES, draw->count, GL_UNSIGNED_INT,
        (void *)(uintptr_t)draw->offset, instances->count);
    R_ResetInstancedBuffer();
}

/* Free the lazily-created shared VAO; owners release their immutable instance VBOs. */
void R_ShutdownDrawBufferInstanced(void) {
    if (r_instanced_vao) {
        R_Call(glDeleteVertexArrays, 1, &r_instanced_vao);
        r_instanced_vao = 0;
    }
}

void R_ReleaseVertexArrayObject(buffer_t *buffer) {
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
