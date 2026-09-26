#include "r_mdx.h"

typedef struct { uint32_t count; int first; } mdxPaletteError_t;

static uint8_t R_AddGeosetMatrixPaletteEntry(mdxGeoset_t *geoset, int matrix_id, mdxPaletteError_t * overflow) {
    if (matrix_id < 0) {
        matrix_id = 0;
    }
    FOR_LOOP(i, geoset->num_matrixPalette) {
        if (geoset->matrixPalette[i] == matrix_id) {
            return (uint8_t)i;
        }
    }
    if (geoset->num_matrixPalette >= MDX_MATRIX_PALETTE) {
        if (!overflow->count)
            overflow->first = matrix_id;
        overflow->count++;
        return 0;
    }
    geoset->matrixPalette[geoset->num_matrixPalette] = matrix_id;
    return (uint8_t)geoset->num_matrixPalette++;
}

/* Keep geoset-local palette indices while packing every stream into the shared vertex format. */
static void mdx_pack_vertices(mdxGeoset_t *geoset, vertex_t * vertices) {
    typedef uint8_t matrixGroup_t[MAX_SKIN_BONES];
    uint32_t matrixGroupCount = geoset->num_matrixGroupSizes > 0 && geoset->matrixGroupSizes && geoset->matrices
        ? (uint32_t)geoset->num_matrixGroupSizes
        : 1;
    matrixGroup_t *matrixGroups = ri.MemAlloc(sizeof(matrixGroup_t) * matrixGroupCount);
    uint32_t indexOffset = 0;
    mdxPaletteError_t overflow = {0};
    geoset->matrixPalette = ri.MemAlloc(sizeof(*geoset->matrixPalette) * MDX_MATRIX_PALETTE);
    geoset->num_matrixPalette = 0;

    FOR_LOOP(matrixGroupIndex, matrixGroupCount) {
        memset(&matrixGroups[matrixGroupIndex], 0x00, sizeof(matrixGroup_t));
        if (matrixGroupCount == 1 &&
            (!geoset->matrixGroupSizes || !geoset->matrices || geoset->num_matrixGroupSizes <= 0)) {
            matrixGroups[matrixGroupIndex][0] = R_AddGeosetMatrixPaletteEntry(geoset, 0, &overflow);
            continue;
        }
        uint32_t sourceGroupSize = geoset->matrixGroupSizes[matrixGroupIndex];
        uint32_t groupSize = MIN(sourceGroupSize, MAX_SKIN_BONES);
        if (indexOffset >= (uint32_t)geoset->num_matrices) {
            groupSize = 0;
        } else if (indexOffset + groupSize > (uint32_t)geoset->num_matrices) {
            groupSize = (uint32_t)geoset->num_matrices - indexOffset;
        }
        FOR_LOOP(matrixIndex, groupSize) {
            int matrix_id = geoset->matrices[indexOffset + matrixIndex];
            matrixGroups[matrixGroupIndex][matrixIndex] = R_AddGeosetMatrixPaletteEntry(geoset, matrix_id, &overflow);
        }
        /* Top-four filtering consumes four matrices but the next group still
           starts after the complete file-shaped group. */
        indexOffset += sourceGroupSize;
    }
    if (overflow.count) {
        fprintf(stderr,
                "MDX geoset uses more than %d unique matrices; %u matrix refs fell back to palette slot 0 (first node %d)\n",
                MDX_MATRIX_PALETTE,
                overflow.count,
                overflow.first);
    }

    FOR_LOOP(vertex, geoset->num_vertices) {
        vertices[vertex].position = geoset->vertices[vertex];
        vertices[vertex].normal = geoset->normals[vertex];
        vertices[vertex].texcoord = geoset->texcoord[vertex];
        vertices[vertex].color = COLOR32_WHITE;
        uint32_t matrixGroupIndex = 0;
        uint32_t matrixGroupSize = 1;
        uint8_t leftover = 0xff;
        uint8_t leftoversize = 1;
        if (geoset->vertexGroups && geoset->matrixGroupSizes && geoset->num_matrixGroupSizes > 0) {
            matrixGroupIndex = (uint8_t)geoset->vertexGroups[vertex];
            if (matrixGroupIndex >= matrixGroupCount) {
                matrixGroupIndex = matrixGroupCount - 1;
            }
            matrixGroupSize = MAX(1, geoset->matrixGroupSizes[matrixGroupIndex]);
            if (matrixGroupSize > MAX_SKIN_BONES) {
                matrixGroupSize = MAX_SKIN_BONES;
            }
            if (matrixGroupSize > geoset->num_matrices) {
                matrixGroupSize = geoset->num_matrices;
            }
            leftoversize = matrixGroupSize;
        }
        uint8_t *matrixGroup = matrixGroups[matrixGroupIndex];
        /* Distribute equal weights across all bones in the group, then keep
           the top 4 by weight (descending) and renormalize to sum=255. */
        uint8_t rawSkin[MAX_SKIN_BONES] = {0};
        uint8_t rawWeight[MAX_SKIN_BONES] = {0};
        memcpy(rawSkin, matrixGroup, MAX_SKIN_BONES);
        if (matrixGroupCount == 1 && matrixGroup[0] == 0) {
            rawWeight[0] = 255;
        } else {
            FOR_LOOP(matrixIndex, matrixGroupSize) {
                uint8_t value = (float)leftover / (float)leftoversize;
                rawWeight[matrixIndex] = value;
                leftover = MAX(0, leftover - value);
                leftoversize = MAX(1, leftoversize - 1);
            }
        }
        /* Insertion-sort top 4 (weight desc) into the 4-slot skin/boneWeight. */
        memset(vertices[vertex].skin, 0, 4);
        memset(vertices[vertex].boneWeight, 0, 4);
        FOR_LOOP(i, MAX_SKIN_BONES) {
            if (!rawWeight[i]) continue;
            FOR_LOOP(j, 4) {
                if (rawWeight[i] > vertices[vertex].boneWeight[j]) {
                    /* Shift lower entries down, dropping slot 3. */
                    for (int k = 3; k > (int)j; k--) {
                        vertices[vertex].skin[k] = vertices[vertex].skin[k - 1];
                        vertices[vertex].boneWeight[k] = vertices[vertex].boneWeight[k - 1];
                    }
                    vertices[vertex].skin[j] = rawSkin[i];
                    vertices[vertex].boneWeight[j] = rawWeight[i];
                    break;
                }
            }
        }
        /* Renormalize top-4 weights to sum=255. */
        uint32_t wsum = 0;
        FOR_LOOP(j, 4) wsum += vertices[vertex].boneWeight[j];
        if (wsum && wsum != 255) {
            uint32_t acc = 0;
            FOR_LOOP(j, 4) {
                uint32_t w = vertices[vertex].boneWeight[j] * 255 / wsum;
                vertices[vertex].boneWeight[j] = (uint8_t)w;
                acc += w;
            }
            /* Fix rounding remainder in the highest-weight slot. */
            if (acc < 255) vertices[vertex].boneWeight[0] += (uint8_t)(255 - acc);
        }
    }
    ri.MemFree(matrixGroups);
}

/* One model owns two buffers; VAOs retain each geoset's vertex range and local 16-bit index interpretation. */
void MDX_PackModelGeometry(mdxModel_t *model, vertex_t * vertices, uint16_t *indices) {
    uint32_t base = 0, elems = 0;
    FOR_EACH_LIST(mdxGeoset_t, geo, model->geosets) {
        mdx_pack_vertices(geo, vertices + base);
        memcpy(indices + elems, geo->triangles, geo->num_triangles * sizeof(*indices));
        geo->indexofs = elems * sizeof(*indices);
        elems += geo->num_triangles; base += geo->num_vertices;
    }
}

/* Build compact model-owned GPU storage after all geoset blocks have been parsed. */
void MDX_BuildBuffers(mdxModel_t *model) {
    static const struct { GLuint attr; GLint size; GLenum type; GLboolean norm; size_t ofs; } attrs[] = {
        { attrib_position, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, position) },
        { attrib_texcoord, 2, GL_FLOAT, GL_FALSE, offsetof(vertex_t, texcoord) },
        { attrib_normal, 3, GL_FLOAT, GL_FALSE, offsetof(vertex_t, normal) },
        { attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(vertex_t, color) },
        { attrib_skin1, 4, GL_UNSIGNED_BYTE, GL_FALSE, offsetof(vertex_t, skin) },
        { attrib_boneWeight1, 4, GL_UNSIGNED_BYTE, GL_TRUE, offsetof(vertex_t, boneWeight) },
    };
    uint32_t verts = 0, elems = 0, base = 0;
    FOR_EACH_LIST(mdxGeoset_t, geo, model->geosets) {
        verts += geo->num_vertices;
        elems += geo->num_triangles;
    }
    if (!model->geosets) return;
    vertex_t * vertices = ri.MemAlloc(verts * sizeof(*vertices));
    uint16_t *indices = ri.MemAlloc(elems * sizeof(*indices));
    MDX_PackModelGeometry(model, vertices, indices);
    R_Call(glGenBuffers, BZ_MDX_BUFFER_COUNT, model->buffers);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, model->buffers[BZ_MDX_VERTEX_BUFFER]);
    R_Call(glBufferData, GL_ARRAY_BUFFER, verts * sizeof(*vertices), vertices, GL_STATIC_DRAW);
    base = 0;
    FOR_EACH_LIST(mdxGeoset_t, geo, model->geosets) {
        R_Call(glGenVertexArrays, 1, &geo->vertexArrayBuffer);
        R_Call(glBindVertexArray, geo->vertexArrayBuffer);
        R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, model->buffers[BZ_MDX_INDEX_BUFFER]);
        FOR_LOOP(i, sizeof(attrs) / sizeof(*attrs)) {
            R_Call(glEnableVertexAttribArray, attrs[i].attr);
            R_Call(glVertexAttribPointer, attrs[i].attr, attrs[i].size, attrs[i].type, attrs[i].norm,
                sizeof(vertex_t), (void *)(base * sizeof(vertex_t) + attrs[i].ofs));
        }
        base += geo->num_vertices;
    }
    /* Upload once after binding a VAO: core GL owns the element-buffer binding in the VAO. */
    R_Call(glBufferData, GL_ELEMENT_ARRAY_BUFFER, elems * sizeof(*indices), indices, GL_STATIC_DRAW);
    ri.MemFree(indices); ri.MemFree(vertices);
}
