#include "r_mdx.h"
#include "renderer/r_local.h"

#define LPCSTR const char *
#define FOR_EACH_LIST(type, property, list) \
for (type *property = list, *next = list ? (list)->next : NULL; \
property; \
property = next, next = next ? next->next : NULL)
#define SAFE_DELETE(x, func) if (x) { func(x); (x) = NULL; }
#define SFileReadArray2(buffer, object, variable, elemsize) \
MSG_Read(buffer, &object->num_##variable, 4); \
if (object->num_##variable > 0) {object->variable = ri.MemAlloc(object->num_##variable * elemsize); \
MSG_Read(buffer, object->variable, object->num_##variable * elemsize); }

#define MODEL_READ_LIST(BLOCK, TYPE, TYPES) \
while (!FileIsAtEndOfBlock(BLOCK)) { \
    sizeBuf_t inner = FileReadBlock(BLOCK); \
    mdx##TYPE##_t *p_##TYPE = ri.MemAlloc(sizeof(mdx##TYPE##_t)); \
    Read##TYPE(&inner, p_##TYPE); \
    PUSH_BACK(mdx##TYPE##_t, p_##TYPE, model->TYPES); \
    BLOCK->readcount += inner.readcount; \
}

#define MODEL_READ_ARRAY(BLOCK, TYPE, TYPES) \
model->TYPES = ri.MemAlloc(BLOCK->cursize); \
model->num_##TYPES = BLOCK->cursize / sizeof(mdx##TYPE##_t); \
MSG_Read(BLOCK, model->TYPES, BLOCK->cursize);

enum {
    ID_VRTX = MAKEFOURCC('V','R','T','X'),
    ID_NRMS = MAKEFOURCC('N','R','M','S'),
    ID_UVBS = MAKEFOURCC('U','V','B','S'),
    ID_PTYP = MAKEFOURCC('P','T','Y','P'),
    ID_PCNT = MAKEFOURCC('P','C','N','T'),
    ID_PVTX = MAKEFOURCC('P','V','T','X'),
    ID_GNDX = MAKEFOURCC('G','N','D','X'),
    ID_MTGC = MAKEFOURCC('M','T','G','C'),
    ID_UVAS = MAKEFOURCC('U','V','A','S'),
    ID_MATS = MAKEFOURCC('M','A','T','S'),
    ID_LAYS = MAKEFOURCC('L','A','Y','S'),
    ID_KGAO = MAKEFOURCC('K','G','A','O'),
    ID_KGAC = MAKEFOURCC('K','G','A','C'),
    ID_KGTR = MAKEFOURCC('K','G','T','R'),
    ID_KGRT = MAKEFOURCC('K','G','R','T'),
    ID_KGSC = MAKEFOURCC('K','G','S','C'),
    ID_KEVT = MAKEFOURCC('K','E','V','T'),
    ID_KCTR = MAKEFOURCC('K','C','T','R'),
    ID_KTTR = MAKEFOURCC('K','T','T','R'),
    ID_KCRL = MAKEFOURCC('K','C','R','L'),
    ID_KMTE = MAKEFOURCC('K','M','T','E'),
    ID_KMTA = MAKEFOURCC('K','M','T','A'),
    ID_KMTF = MAKEFOURCC('K','M','T','F'),
    ID_KTAT = MAKEFOURCC('K','T','A','T'),
    ID_KTAR = MAKEFOURCC('K','T','A','R'),
    ID_KTAS = MAKEFOURCC('K','T','A','S'),
    ID_KP2V = MAKEFOURCC('K','P','2','V'),
    ID_KP2E = MAKEFOURCC('K','P','2','E'),
    ID_KP2W = MAKEFOURCC('K','P','2','W'),
    ID_KP2N = MAKEFOURCC('K','P','2','N'),
    ID_KP2S = MAKEFOURCC('K','P','2','S'),
    ID_KP2L = MAKEFOURCC('K','P','2','L'),
    ID_KP2G = MAKEFOURCC('K','P','2','G'),
    ID_KP2R = MAKEFOURCC('K','P','2','R'),
    ID_KATV = MAKEFOURCC('K','A','T','V'),
    ID_KLAV = MAKEFOURCC('K','L','A','V'),
    ID_KLAC = MAKEFOURCC('K','L','A','C'),
    ID_KLAI = MAKEFOURCC('K','L','A','I'),
    ID_KLBC = MAKEFOURCC('K','L','B','C'),
    ID_KLBI = MAKEFOURCC('K','L','B','I'),
    ID_KLAS = MAKEFOURCC('K','L','A','S'),
    ID_KLAE = MAKEFOURCC('K','L','A','E'),
};

typedef struct {
    DWORD header;
    DWORD size;
    DWORD start;
} tFileBlock_t;

DWORD GetModelKeyTrackDataTypeSize(MODELKEYTRACKDATATYPE dataType) {
    switch (dataType) {
        case TDATA_INT1: return 4;
        case TDATA_FLOAT1: return 4;
        case TDATA_FLOAT3: return 12;
        case TDATA_FLOAT4: return 16;
        default: return 0;
    }
}

DWORD GetModelKeyTrackTypeSize(MODELKEYTRACKTYPE keyTrackType) {
    switch (keyTrackType) {
        case TRACK_NO_INTERP: return 1;
        case TRACK_LINEAR: return 1;
        case TRACK_HERMITE: return 3;
        case TRACK_BEZIER: return 3;
        default: return 0;
    }
}

DWORD GetModelKeyFrameSize(MODELKEYTRACKDATATYPE dataType,
                           MODELKEYTRACKTYPE keyTrackType)
{
    return 4 + GetModelKeyTrackDataTypeSize(dataType) * GetModelKeyTrackTypeSize(keyTrackType);
}

DWORD R_ModelFindBiggestGroup(mdxGeoset_t const *geoset) {
    if (!geoset || !geoset->matrixGroupSizes || geoset->num_matrixGroupSizes <= 0) {
        return 0;
    }
    DWORD biggest = 0;
    FOR_LOOP(i, geoset->num_matrixGroupSizes) {
        biggest = MAX(geoset->matrixGroupSizes[i], biggest);
    }
    return biggest;
}

static BYTE R_AddGeosetMatrixPaletteEntry(mdxGeoset_t *geoset, int matrix_id, DWORD *overflow_count, int *overflow_first) {
    if (matrix_id < 0) {
        matrix_id = 0;
    }
    FOR_LOOP(i, geoset->num_matrixPalette) {
        if (geoset->matrixPalette[i] == matrix_id) {
            return (BYTE)i;
        }
    }
    if (geoset->num_matrixPalette >= MDX_MATRIX_PALETTE) {
        if (!*overflow_count)
            *overflow_first = matrix_id;
        (*overflow_count)++;
        return 0;
    }
    geoset->matrixPalette[geoset->num_matrixPalette] = matrix_id;
    return (BYTE)geoset->num_matrixPalette++;
}

void R_SetupGeosetVertexBuffer(mdxGeoset_t *geoset) {
    typedef BYTE matrixGroup_t[MAX_SKIN_BONES];
    mdxVertexSkin_t *vertices = ri.MemAlloc(sizeof(mdxVertexSkin_t) * geoset->num_vertices);
    DWORD matrixGroupCount = geoset->num_matrixGroupSizes > 0 && geoset->matrixGroupSizes && geoset->matrices
        ? (DWORD)geoset->num_matrixGroupSizes
        : 1;
    matrixGroup_t *matrixGroups = ri.MemAlloc(sizeof(matrixGroup_t) * matrixGroupCount);
    DWORD indexOffset = 0;
    DWORD paletteOverflowCount = 0;
    int paletteOverflowFirst = 0;
    geoset->matrixPalette = ri.MemAlloc(sizeof(*geoset->matrixPalette) * MDX_MATRIX_PALETTE);
    geoset->num_matrixPalette = 0;

    FOR_LOOP(matrixGroupIndex, matrixGroupCount) {
        memset(&matrixGroups[matrixGroupIndex], 0x00, sizeof(matrixGroup_t));
        if (matrixGroupCount == 1 && (!geoset->matrixGroupSizes || !geoset->matrices || geoset->num_matrixGroupSizes <= 0)) {
            matrixGroups[matrixGroupIndex][0] = R_AddGeosetMatrixPaletteEntry(geoset, 0, &paletteOverflowCount, &paletteOverflowFirst);
            continue;
        }
        DWORD sourceGroupSize = geoset->matrixGroupSizes[matrixGroupIndex];
        DWORD groupSize = MIN(sourceGroupSize, MAX_SKIN_BONES);
        if (indexOffset >= (DWORD)geoset->num_matrices) {
            groupSize = 0;
        } else if (indexOffset + groupSize > (DWORD)geoset->num_matrices) {
            groupSize = (DWORD)geoset->num_matrices - indexOffset;
        }
        FOR_LOOP(matrixIndex, groupSize) {
            int matrix_id = geoset->matrices[indexOffset + matrixIndex];
            matrixGroups[matrixGroupIndex][matrixIndex] = R_AddGeosetMatrixPaletteEntry(
                geoset, matrix_id, &paletteOverflowCount, &paletteOverflowFirst);
        }
        /* Top-four filtering consumes four matrices but the next group still
           starts after the complete file-shaped group. */
        indexOffset += sourceGroupSize;
    }
    if (paletteOverflowCount) {
        fprintf(stderr,
                "MDX geoset uses more than %d unique matrices; %u matrix refs fell back to palette slot 0 (first node %d)\n",
                MDX_MATRIX_PALETTE,
                paletteOverflowCount,
                paletteOverflowFirst);
    }

    FOR_LOOP(vertex, geoset->num_vertices) {
        DWORD matrixGroupIndex = 0;
        DWORD matrixGroupSize = 1;
        BYTE leftover = 0xff;
        BYTE leftoversize = 1;
        if (geoset->vertexGroups && geoset->matrixGroupSizes && geoset->num_matrixGroupSizes > 0) {
            matrixGroupIndex = (BYTE)geoset->vertexGroups[vertex];
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
        BYTE *matrixGroup = matrixGroups[matrixGroupIndex];
        /* Distribute equal weights across all bones in the group, then keep
           the top 4 by weight (descending) and renormalize to sum=255. */
        BYTE rawSkin[MAX_SKIN_BONES] = {0};
        BYTE rawWeight[MAX_SKIN_BONES] = {0};
        memcpy(rawSkin, matrixGroup, MAX_SKIN_BONES);
        if (matrixGroupCount == 1 && matrixGroup[0] == 0) {
            rawWeight[0] = 255;
        } else {
            FOR_LOOP(matrixIndex, matrixGroupSize) {
                BYTE value = (float)leftover / (float)leftoversize;
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
        DWORD wsum = 0;
        FOR_LOOP(j, 4) wsum += vertices[vertex].boneWeight[j];
        if (wsum && wsum != 255) {
            DWORD acc = 0;
            FOR_LOOP(j, 4) {
                DWORD w = vertices[vertex].boneWeight[j] * 255 / wsum;
                vertices[vertex].boneWeight[j] = (BYTE)w;
                acc += w;
            }
            /* Fix rounding remainder in the highest-weight slot. */
            if (acc < 255) vertices[vertex].boneWeight[0] += (BYTE)(255 - acc);
        }
    }
    R_Call(glGenVertexArrays, 1, &geoset->vertexArrayBuffer);
    R_Call(glBindVertexArray, geoset->vertexArrayBuffer);
    R_Call(glGenBuffers, MAX_MDLX_BUFFERS, geoset->buffer);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, geoset->buffer[1]);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(VECTOR3) * geoset->num_vertices, geoset->vertices, GL_STATIC_DRAW);
    R_Call(glVertexAttribPointer, attrib_position, 3, GL_FLOAT, GL_FALSE, 0, 0);
    R_Call(glEnableVertexAttribArray, attrib_position);

    R_Call(glBindBuffer, GL_ARRAY_BUFFER, geoset->buffer[2]);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(VECTOR2) * geoset->num_texcoord, geoset->texcoord, GL_STATIC_DRAW);
    R_Call(glVertexAttribPointer, attrib_texcoord, 2, GL_FLOAT, GL_FALSE, 0, 0);
    R_Call(glEnableVertexAttribArray, attrib_texcoord);

    R_Call(glBindBuffer, GL_ARRAY_BUFFER, geoset->buffer[3]);
    R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(VECTOR3) * geoset->num_normals, geoset->normals, GL_STATIC_DRAW);
    R_Call(glVertexAttribPointer, attrib_normal, 3, GL_FLOAT, GL_FALSE, 0, 0);
    R_Call(glEnableVertexAttribArray, attrib_normal);

    R_Call(glBindBuffer, GL_ARRAY_BUFFER, geoset->buffer[4]);

    R_Call(glEnableVertexAttribArray, attrib_skin1);
    R_Call(glEnableVertexAttribArray, attrib_boneWeight1);

    R_Call(glVertexAttribPointer, attrib_skin1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(mdxVertexSkin_t), FOFS(mdxVertexSkin_s, skin[0]));
    R_Call(glVertexAttribPointer, attrib_boneWeight1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(mdxVertexSkin_t), FOFS(mdxVertexSkin_s, boneWeight[0]));

    R_Call(glBufferData, GL_ARRAY_BUFFER, geoset->num_vertices * sizeof(mdxVertexSkin_t), vertices, GL_STATIC_DRAW);

    /* MDX models have no per-vertex colour data.  The unified model shader
     * multiplies the sampled texture by v_color (i_color attribute), so we
     * supply a white (255,255,255,255) buffer to make it a no-op. */
    {
        typedef BYTE color4_t[4];
        color4_t *whiteColors = ri.MemAlloc(sizeof(color4_t) * geoset->num_vertices);
        FOR_LOOP(i, geoset->num_vertices) {
            whiteColors[i][0] = 255;
            whiteColors[i][1] = 255;
            whiteColors[i][2] = 255;
            whiteColors[i][3] = 255;
        }
        R_Call(glBindBuffer, GL_ARRAY_BUFFER, geoset->buffer[5]);
        R_Call(glBufferData, GL_ARRAY_BUFFER, sizeof(color4_t) * geoset->num_vertices, whiteColors, GL_STATIC_DRAW);
        R_Call(glEnableVertexAttribArray, attrib_color);
        R_Call(glVertexAttribPointer, attrib_color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0);
        ri.MemFree(whiteColors);
    }

    R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, geoset->buffer[0]);
    R_Call(glBufferData, GL_ELEMENT_ARRAY_BUFFER, sizeof(USHORT) * geoset->num_triangles, geoset->triangles, GL_STATIC_DRAW);
            
    ri.MemFree(vertices);
    ri.MemFree(matrixGroups);
}

void R_ReleaseModelNode(mdxNode_t *node) {
    SAFE_DELETE(node->translation, ri.MemFree);
    SAFE_DELETE(node->rotation, ri.MemFree);
    SAFE_DELETE(node->scale, ri.MemFree);
}

typedef enum {
    BLOCKREAD_OK,
    BLOCKREAD_ERROR,
} blockReadCode_t;

typedef blockReadCode_t (*blockReaderFunc_t)(LPSIZEBUF sb, void *model);

typedef struct {
    LPCSTR block_id;
    blockReaderFunc_t read;
} blockReader_t;

blockReadCode_t MSG_ReadBlock(LPSIZEBUF buffer, blockReader_t const *readers, void *data) {
    DWORD blockHeader;
    while (MSG_Read(buffer, &blockHeader, 4)) {
        sizeBuf_t block;
        memset(&block, 0, sizeof(sizeBuf_t));
        MSG_Read(buffer, &block.cursize, 4);
        block.data = buffer->data + buffer->readcount;
        for (blockReader_t const *br = readers; br->read; br++) {
            if (*(int const *)br->block_id != blockHeader)
                continue;
            if (br->read(&block, data) != BLOCKREAD_OK)
                return BLOCKREAD_ERROR;
            buffer->readcount += block.cursize;
            goto next_block;
        }
        buffer->readcount += block.cursize;;
        PrintTag(blockHeader);
    next_block:
        continue;
    }
    return BLOCKREAD_OK;
}

int MSG_Read(LPSIZEBUF buffer, void *dest, DWORD bytes) {
    if (buffer->readcount + bytes > buffer->cursize)
        return 0;
    memcpy(dest, (char *)buffer->data + buffer->readcount, bytes);
    buffer->readcount += bytes;
    return bytes;
}

int MSG_ReadLong(LPSIZEBUF buffer) {
    DWORD value = 0;
    MSG_Read(buffer, &value, 4);
    return value;
}

int MSG_ReadByte(LPSIZEBUF buffer) {
    DWORD value = 0;
    MSG_Read(buffer, &value, 1);
    return value;
}

sizeBuf_t FileReadBlock(LPSIZEBUF buffer) {
    sizeBuf_t buf;
    buf.data = buffer->data + buffer->readcount;
    buf.readcount = 0;
    buf.cursize = 4;
    buf.cursize = MSG_ReadLong(&buf);
    return buf;
}

int FileIsAtEndOfBlock(LPSIZEBUF sb) {
    return sb->readcount >= sb->cursize;
}

void ReadGeosetMatrices(LPSIZEBUF buffer, mdxGeoset_t *geoset) {
    SFileReadArray2(buffer, geoset, matrices, sizeof(int));
    MSG_Read(buffer, &geoset->materialID, sizeof(int));
    MSG_Read(buffer, &geoset->group, sizeof(int));
    MSG_Read(buffer, &geoset->selectable, sizeof(int));
    MSG_Read(buffer, &geoset->default_bounds, sizeof(mdxBounds_t));
    SFileReadArray2(buffer, geoset, bounds, sizeof(mdxBounds_t));
}

void ReadGeoset(LPSIZEBUF buffer, mdxGeoset_t *geoset) {
    DWORD header;
    while (MSG_Read(buffer, &header, 4)) {
        switch (header) {
            case ID_VRTX: SFileReadArray2(buffer, geoset, vertices, sizeof(VECTOR3)); break;
            case ID_NRMS: SFileReadArray2(buffer, geoset, normals, sizeof(VECTOR3)); break;
            case ID_UVBS: SFileReadArray2(buffer, geoset, texcoord, sizeof(VECTOR2)); break;
            case ID_PTYP: SFileReadArray2(buffer, geoset, primitiveTypes, sizeof(int)); break;
            case ID_PCNT: SFileReadArray2(buffer, geoset, primitiveCounts, sizeof(int)); break;
            case ID_PVTX: SFileReadArray2(buffer, geoset, triangles, sizeof(short)); break;
            case ID_GNDX: SFileReadArray2(buffer, geoset, vertexGroups, sizeof(char)); break;
            case ID_MTGC: SFileReadArray2(buffer, geoset, matrixGroupSizes, sizeof(int)); break;
            case ID_UVAS: MSG_Read(buffer, &geoset->num_texcoordChannels, sizeof(int)); break;
            case ID_MATS: ReadGeosetMatrices(buffer, geoset); break;
            default:
                PrintTag(header);
                break;
        }
    };
    R_SetupGeosetVertexBuffer(geoset);
}

void ReadKeyTrack(LPSIZEBUF buffer, MODELKEYTRACKDATATYPE dataType, mdxKeyTrack_t **output) {
    DWORD keyframeCount = MSG_ReadLong(buffer);
    MODELKEYTRACKTYPE keyTrackType = MSG_ReadLong(buffer);
    DWORD globalSeqId = MSG_ReadLong(buffer);
    DWORD const dataSize = GetModelKeyFrameSize(dataType, keyTrackType) * keyframeCount;
    *output = ri.MemAlloc(sizeof(mdxKeyTrack_t) + dataSize);
    (*output)->keyframeCount = keyframeCount;
    (*output)->datatype = dataType;
    (*output)->linetype = keyTrackType;
    (*output)->globalSeqId = globalSeqId;
    MSG_Read(buffer, (*output)->values, dataSize);
}

void ReadMaterialLayer(LPSIZEBUF buffer, mdxMaterialLayer_t *layer) {
    DWORD blockHeader;
    MSG_Read(buffer, &layer->blendMode, 4);
    MSG_Read(buffer, &layer->flags, 4);
    MSG_Read(buffer, &layer->textureId, 4);
    MSG_Read(buffer, &layer->transformId, 4);
    MSG_Read(buffer, &layer->coordId, 4);
    MSG_Read(buffer, &layer->staticAlpha, 4);
    while (MSG_Read(buffer, &blockHeader, 4)) {
        switch (blockHeader) {
            case ID_KMTA: ReadKeyTrack(buffer, TDATA_FLOAT1, &layer->alpha); break;
            case ID_KMTF: ReadKeyTrack(buffer, TDATA_INT1, &layer->flipbook); break;
            default:
                PrintTag(blockHeader);
                break;
        }
    }
}

void ReadMaterialLayers(LPSIZEBUF buffer, mdxMaterial_t *material) {
    if (!(material->num_layers = MSG_ReadLong(buffer)))
        return;
    material->layers = ri.MemAlloc(sizeof(mdxMaterialLayer_t) * material->num_layers);
    FOR_LOOP(layerID, material->num_layers) {
        sizeBuf_t layer = FileReadBlock(buffer);
        ReadMaterialLayer(&layer, &material->layers[layerID]);
        buffer->readcount += layer.readcount;
    }
}

void ReadMaterial(LPSIZEBUF buffer, mdxMaterial_t *material) {
    DWORD blockHeader;
    material->priority = MSG_ReadLong(buffer);
    material->flags = MSG_ReadLong(buffer);
    while (MSG_Read(buffer, &blockHeader, 4)) {
        switch (blockHeader) {
            case ID_LAYS: ReadMaterialLayers(buffer, material); break;
            case ID_KMTE: ReadKeyTrack(buffer, TDATA_FLOAT1, &material->emission); break;
            case ID_KMTA: ReadKeyTrack(buffer, TDATA_FLOAT1, &material->alpha); break;
            case ID_KMTF: ReadKeyTrack(buffer, TDATA_INT1, &material->flipbook); break;
            default:
                PrintTag(blockHeader);
                return;
        }
    };
}

void ReadTextureAnim(LPSIZEBUF buffer, mdxTextureAnim_t *textureAnim) {
    DWORD blockHeader;
    while (MSG_Read(buffer, &blockHeader, 4)) {
        switch (blockHeader) {
            case ID_KTAT: ReadKeyTrack(buffer, TDATA_FLOAT3, &textureAnim->translation); break;
            case ID_KTAR: ReadKeyTrack(buffer, TDATA_FLOAT4, &textureAnim->rotation); break;
            case ID_KTAS: ReadKeyTrack(buffer, TDATA_FLOAT3, &textureAnim->scale); break;
            default:
                PrintTag(blockHeader);
                break;
        }
    }
}

void ReadNode(LPSIZEBUF buffer, mdxNode_t *node, DWORD blockSize) {
    DWORD blockEnd = buffer->readcount + blockSize;
    MSG_Read(buffer, &node->name, sizeof(mdxObjectName_t));
    node->node_id = MSG_ReadLong(buffer);
    node->parent_id = MSG_ReadLong(buffer);
    node->flags = MSG_ReadLong(buffer);
    
    while (buffer->readcount < blockEnd) {
        DWORD blockHeader;
        MSG_Read(buffer, &blockHeader, 4);
        switch (blockHeader) {
            case ID_KGTR: ReadKeyTrack(buffer, TDATA_FLOAT3, &node->translation); break;
            case ID_KGRT: ReadKeyTrack(buffer, TDATA_FLOAT4, &node->rotation); break;
            case ID_KGSC: ReadKeyTrack(buffer, TDATA_FLOAT3, &node->scale); break;
            default:
                PrintTag(blockHeader);
                break;
        }
    }
}

void MSG_ReadOverflow(LPSIZEBUF buffer, void *dest, DWORD bytes) {
    buffer->cursize += bytes;
    MSG_Read(buffer, dest, bytes);
}

void ReadBone(LPSIZEBUF buffer, mdxBone_t *bone) {
    ReadNode(buffer, &bone->node, buffer->cursize - buffer->readcount);
    MSG_ReadOverflow(buffer, &bone->geoset_id, sizeof(DWORD));
    MSG_ReadOverflow(buffer, &bone->geoset_animation_id, sizeof(DWORD));
}

void ReadHelper(LPSIZEBUF buffer, mdxHelper_t *helper) {
    ReadNode(buffer, &helper->node, buffer->cursize - buffer->readcount);
}

void ReadCollisionShape(LPSIZEBUF buffer, mdxCollisionShape_t *cs) {
    ReadNode(buffer, &cs->node, buffer->cursize - buffer->readcount);
    MSG_ReadOverflow(buffer, &cs->type, sizeof(DWORD));
    MSG_ReadOverflow(buffer, &cs->vertex[0], sizeof(VECTOR3));
    if (cs->type != SHAPETYPE_SPHERE) {
        MSG_ReadOverflow(buffer, &cs->vertex[1], sizeof(VECTOR3));
    }
    if ((cs->type == SHAPETYPE_SPHERE) || (cs->type == SHAPETYPE_CYLINDER)) {
        MSG_ReadOverflow(buffer, &cs->radius, sizeof(float));
    }
}

#define MSG_READ(BUFFER, VAR) \
MSG_Read(BUFFER, &VAR, sizeof(VAR));

void ReadParticleEmitter(LPSIZEBUF buffer, mdxParticleEmitter_t *pe) {
    DWORD emitterSize = MSG_ReadLong(buffer), header;
    ReadNode(buffer, &pe->node, emitterSize - sizeof(emitterSize));
    MSG_READ(buffer, pe->Speed);
    MSG_READ(buffer, pe->Variation);
    MSG_READ(buffer, pe->Latitude);
    MSG_READ(buffer, pe->Gravity);
    MSG_READ(buffer, pe->LifeSpan);
    MSG_READ(buffer, pe->EmissionRate);
    MSG_READ(buffer, pe->Length);
    MSG_READ(buffer, pe->Width);
    MSG_READ(buffer, pe->FilterMode);
    MSG_READ(buffer, pe->Rows);
    MSG_READ(buffer, pe->Columns);
    MSG_READ(buffer, pe->FrameFlags);
    MSG_READ(buffer, pe->TailLength);
    MSG_READ(buffer, pe->Time);
    MSG_READ(buffer, pe->SegmentColor);
    MSG_READ(buffer, pe->Alpha);
    MSG_READ(buffer, pe->ParticleScaling);
    MSG_READ(buffer, pe->LifeSpanUVAnim);
    MSG_READ(buffer, pe->DecayUVAnim);
    MSG_READ(buffer, pe->TailUVAnim);
    MSG_READ(buffer, pe->TailDecayUVAnim);
    MSG_READ(buffer, pe->TextureID);
    MSG_READ(buffer, pe->Squirt);
    MSG_READ(buffer, pe->PriorityPlane);
    MSG_READ(buffer, pe->ReplaceableId);
    while (MSG_Read(buffer, &header, 4)) {
        switch (header) {
            case ID_KP2V: ReadKeyTrack(buffer, TDATA_FLOAT1, &pe->keytracks.Visibility); break;
            case ID_KP2E: ReadKeyTrack(buffer, TDATA_FLOAT1, &pe->keytracks.EmissionRate); break;
            case ID_KP2W: ReadKeyTrack(buffer, TDATA_FLOAT1, &pe->keytracks.Width); break;
            case ID_KP2N: ReadKeyTrack(buffer, TDATA_FLOAT1, &pe->keytracks.Length); break;
            case ID_KP2S: ReadKeyTrack(buffer, TDATA_FLOAT1, &pe->keytracks.Speed); break;
            case ID_KP2L: ReadKeyTrack(buffer, TDATA_FLOAT1, &pe->keytracks.Latitude); break;
            case ID_KP2G: ReadKeyTrack(buffer, TDATA_FLOAT1, &pe->keytracks.Gravity); break;
            case ID_KP2R: ReadKeyTrack(buffer, TDATA_FLOAT1, &pe->keytracks.Variation); break;
            default:
                PrintTag(header);
                break;
        }
    }
}

void ReadCamera(LPSIZEBUF buffer, mdxCamera_t *camera) {
    DWORD blockHeader;
    MSG_Read(buffer, &camera->name, sizeof(mdxObjectName_t));
    MSG_Read(buffer, &camera->pivot, sizeof(VECTOR3));
    MSG_Read(buffer, &camera->fieldOfView, sizeof(float));
    MSG_Read(buffer, &camera->farClip, sizeof(float));
    MSG_Read(buffer, &camera->nearClip, sizeof(float));
    MSG_Read(buffer, &camera->targetPivot, sizeof(VECTOR3));
    while (MSG_Read(buffer, &blockHeader, 4)) {
        switch (blockHeader) {
            case ID_KCTR: ReadKeyTrack(buffer, TDATA_FLOAT3, &camera->translation); break;
            case ID_KTTR: ReadKeyTrack(buffer, TDATA_FLOAT3, &camera->targetTranslation); break;
            case ID_KCRL: ReadKeyTrack(buffer, TDATA_FLOAT1, &camera->roll); break;
            default:
                break;
        }
    }
}

void ReadEvent(LPSIZEBUF buffer, mdxEvent_t *event) {
    ReadNode(buffer, &event->node, buffer->cursize - buffer->readcount);
    DWORD blockHeader;
    MSG_ReadOverflow(buffer, &blockHeader, 4);
    if (blockHeader == ID_KEVT) {
        MSG_ReadOverflow(buffer, &event->num_keys, sizeof(DWORD));
        MSG_ReadOverflow(buffer, &event->globalSeqId, sizeof(DWORD));
        event->keys = ri.MemAlloc(event->num_keys * sizeof(DWORD));
        MSG_ReadOverflow(buffer, event->keys, event->num_keys * sizeof(DWORD));
    } else {
        PrintTag(blockHeader);
    }
}

void ReadAttachment(LPSIZEBUF buffer, mdxAttachment_t *attachment) {
    DWORD attachmentSize = MSG_ReadLong(buffer), header;
    ReadNode(buffer, &attachment->node, attachmentSize - sizeof(attachmentSize));
    MSG_Read(buffer, attachment->path, MODEL_ATTACHMENT_PATH_LENGTH);
    MSG_ReadLong(buffer);
    attachment->attachmentID = MSG_ReadLong(buffer);
    while (MSG_Read(buffer, &header, 4)) {
        switch (header) {
            case ID_KATV:
                ReadKeyTrack(buffer, TDATA_FLOAT1, &attachment->Visibility);
                break;
            default:
                PrintTag(header);
                break;
        }
    }
}

void ReadLight(LPSIZEBUF buffer, mdxLight_t *light) {
    DWORD lightSize = MSG_ReadLong(buffer), header;
    ReadNode(buffer, &light->node, lightSize - sizeof(lightSize));
    MSG_READ(buffer, light->type);
    MSG_READ(buffer, light->AttenuationStart);
    MSG_READ(buffer, light->AttenuationEnd);
    MSG_READ(buffer, light->Color);
    MSG_READ(buffer, light->Intensity);
    MSG_READ(buffer, light->AmbColor);
    MSG_READ(buffer, light->AmbIntensity);
    while (MSG_Read(buffer, &header, 4)) {
        switch (header) {
            case ID_KLAV:
                ReadKeyTrack(buffer, TDATA_FLOAT1, &light->keytracks.Visibility);
                break;
            case ID_KLAC: ReadKeyTrack(buffer, TDATA_FLOAT3, &light->keytracks.Color); break;
            case ID_KLAI: ReadKeyTrack(buffer, TDATA_FLOAT1, &light->keytracks.Intensity); break;
            case ID_KLBC: ReadKeyTrack(buffer, TDATA_FLOAT3, &light->keytracks.AmbColor); break;
            case ID_KLBI:
                ReadKeyTrack(buffer, TDATA_FLOAT1, &light->keytracks.AmbIntensity);
                break;
            case ID_KLAS: ReadKeyTrack(buffer, TDATA_FLOAT1, &light->keytracks.AttenuationStart); break;
            case ID_KLAE: ReadKeyTrack(buffer, TDATA_FLOAT1, &light->keytracks.AttenuationEnd); break;
            default:
                PrintTag(header);
                break;
        }
    }

}

void ReadGeosetAnim(LPSIZEBUF buffer, mdxGeosetAnim_t *geosetAnim) {
    DWORD blockHeader;
    MSG_Read(buffer, geosetAnim, 24);
    while (MSG_Read(buffer, &blockHeader, 4)) {
        switch (blockHeader) {
            case ID_KGAO: ReadKeyTrack(buffer, TDATA_FLOAT1, &geosetAnim->alphas); break;
            case ID_KGAC: ReadKeyTrack(buffer, TDATA_FLOAT3, &geosetAnim->colors); break;
            default:
                PrintTag(blockHeader);
                break;
        }
    };
}

mdxNode_t *MDLX_GetModelNodeWithObjectID(mdxModel_t *model, DWORD objectID) {
    if (objectID == -1) {
        return NULL;
    }
    FOR_EACH_LIST(mdxBone_t, bone, model->bones) {
        if (bone->node.node_id == objectID) {
            return &bone->node;
        }
    }
    FOR_EACH_LIST(mdxHelper_t, helper, model->helpers) {
        if (helper->node.node_id == objectID) {
            return &helper->node;
        }
    }
    return NULL;
}

blockReadCode_t MDLX_ReadMODL(LPSIZEBUF sb, mdxModel_t *model) {
    MSG_Read(sb, &model->info, sizeof(mdxInfo_t));
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadVERS(LPSIZEBUF sb, mdxModel_t *model) {
    if ((model->version = MSG_ReadLong(sb)) != 800) {
        fprintf(stderr, "Usupported MDLX version %d\n", model->version);
        return BLOCKREAD_ERROR;
    } else {
        return BLOCKREAD_OK;
    }
}

blockReadCode_t MDLX_ReadEVTS(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, Event, events);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadGEOS(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, Geoset, geosets);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadMTLS(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, Material, materials);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadTXAN(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, TextureAnim, textureAnims);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadBONE(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, Bone, bones);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadGEOA(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, GeosetAnim, geosetAnims);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadHELP(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, Helper, helpers);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadCLID(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, CollisionShape, collisionShapes);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadCAMS(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, Camera, cameras);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadSEQS(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_ARRAY(sb, Sequence, sequences);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadGLBS(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_ARRAY(sb, GlobalSequence, globalSequences);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadPIVT(LPSIZEBUF sb, mdxModel_t *model) {
    typedef VECTOR3 mdxVec3_t;
    MODEL_READ_ARRAY(sb, Vec3, pivots);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadTEXS(LPSIZEBUF sb, mdxModel_t *model) {
    // MODEL_READ_ARRAY(sb, Texture, textures);
    // TEXS records are fixed 268-byte file records; mdxTexture_t also carries runtime texid state.
    model->num_textures = sb->cursize / MDX_TEXTURE_RECORD_SIZE;
    if (model->num_textures <= 0) {
        return BLOCKREAD_OK;
    }

    model->textures = ri.MemAlloc(sizeof(mdxTexture_t) * model->num_textures);
    FOR_LOOP(i, model->num_textures) {
        mdxTexture_t *texture = &model->textures[i];
        if (!MSG_Read(sb, &texture->replaceableID, sizeof(DWORD)) ||
            !MSG_Read(sb, texture->path, MDX_TEXTURE_PATH_LENGTH) ||
            !MSG_Read(sb, &texture->nWrapping, sizeof(DWORD))) {
            return BLOCKREAD_ERROR;
        }
        texture->path[MDX_TEXTURE_PATH_LENGTH - 1] = '\0';
        texture->texid = -1;
    }
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadPRE2(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, ParticleEmitter, emitters);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadATCH(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, Attachment, attachments);
    return BLOCKREAD_OK;
}

blockReadCode_t MDLX_ReadLITE(LPSIZEBUF sb, mdxModel_t *model) {
    MODEL_READ_LIST(sb, Light, lights);
    return BLOCKREAD_OK;
}

blockReader_t R_MDLX[] = {
    { "VERS", (blockReaderFunc_t)MDLX_ReadVERS },
    { "MODL", (blockReaderFunc_t)MDLX_ReadMODL },
    { "EVTS", (blockReaderFunc_t)MDLX_ReadEVTS },
    { "GEOS", (blockReaderFunc_t)MDLX_ReadGEOS },
    { "MTLS", (blockReaderFunc_t)MDLX_ReadMTLS },
    { "TXAN", (blockReaderFunc_t)MDLX_ReadTXAN },
    { "BONE", (blockReaderFunc_t)MDLX_ReadBONE },
    { "GEOA", (blockReaderFunc_t)MDLX_ReadGEOA },
    { "HELP", (blockReaderFunc_t)MDLX_ReadHELP },
    { "CAMS", (blockReaderFunc_t)MDLX_ReadCAMS },
    { "SEQS", (blockReaderFunc_t)MDLX_ReadSEQS },
    { "GLBS", (blockReaderFunc_t)MDLX_ReadGLBS },
    { "PIVT", (blockReaderFunc_t)MDLX_ReadPIVT },
    { "TEXS", (blockReaderFunc_t)MDLX_ReadTEXS },
    { "CLID", (blockReaderFunc_t)MDLX_ReadCLID },
    { "PRE2", (blockReaderFunc_t)MDLX_ReadPRE2 },
    { "ATCH", (blockReaderFunc_t)MDLX_ReadATCH },
    { "LITE", (blockReaderFunc_t)MDLX_ReadLITE },
    { NULL },
};

mdxBounds_t MDX_CalculateBounds(mdxModel_t const *model) {
    mdxBounds_t b = { 0 };
    FOR_EACH_LIST(mdxGeoset_t, geoset, model->geosets) {
        FOR_LOOP(i, geoset->num_vertices) {
            VECTOR3 const *vertex = geoset->vertices+i;
            b.box.min.x = MIN(vertex->x, b.box.min.x);
            b.box.min.y = MIN(vertex->y, b.box.min.y);
            b.box.min.z = MIN(vertex->z, b.box.min.z);
            b.box.max.x = MAX(vertex->x, b.box.max.x);
            b.box.max.y = MAX(vertex->y, b.box.max.y);
            b.box.max.z = MAX(vertex->z, b.box.max.z);
        }
    }
    b.radius = MAX(b.radius, b.box.max.x - b.box.min.x);
    b.radius = MAX(b.radius, b.box.max.y - b.box.min.y);
    b.radius = MAX(b.radius, b.box.max.z - b.box.min.z);
    return b;
}

mdxModel_t *R_LoadModelMDLX(void *data, DWORD size) {
    mdxModel_t *model = ri.MemAlloc(sizeof(mdxModel_t));
    sizeBuf_t buffer = { .data = data, .cursize = size, .readcount = 4 };
    if (MSG_ReadBlock(&buffer, R_MDLX, model) != BLOCKREAD_OK) {
        MDLX_Release(model);
        return NULL;
    }
    FOR_EACH_LIST(mdxBone_t, bone, model->bones) {
        if (bone->node.node_id < MDX_MAX_NODES) {
            model->nodes[bone->node.node_id] = &bone->node;
        }
    }
    FOR_EACH_LIST(mdxHelper_t, helper, model->helpers) {
        if (helper->node.node_id < MDX_MAX_NODES) {
            model->nodes[helper->node.node_id] = &helper->node;
        }
    }
    FOR_EACH_LIST(mdxCollisionShape_t, shape, model->collisionShapes) {
        if (shape->node.node_id < MDX_MAX_NODES) {
            model->nodes[shape->node.node_id] = &shape->node;
        }
    }
    FOR_EACH_LIST(mdxParticleEmitter_t, emitter, model->emitters) {
        if (emitter->node.node_id < MDX_MAX_NODES) {
            model->nodes[emitter->node.node_id] = &emitter->node;
        }
    }
    FOR_EACH_LIST(mdxAttachment_t, attachment, model->attachments) {
        if (attachment->node.node_id < MDX_MAX_NODES) {
            model->nodes[attachment->node.node_id] = &attachment->node;
        }
    }
    FOR_EACH_LIST(mdxLight_t, light, model->lights) {
        if (light->node.node_id < MDX_MAX_NODES) {
            model->nodes[light->node.node_id] = &light->node;
        }
    }
    FOR_LOOP(i, model->num_textures) {
        mdxTexture_t *tex = model->textures+i;
        if (!tex->path[0]) {
            tex->texid = -1;
            continue;
        }
        tex->texid = R_RegisterTextureFile(tex->path);
        LPCTEXTURE loaded = R_FindTextureByID(tex->texid);
        R_SetTextureWrap(loaded, tex->nWrapping & 0x1, tex->nWrapping & 0x2);
    }
    FOR_EACH_LIST(mdxGeosetAnim_t, geosetAnim, model->geosetAnims) {
        mdxGeoset_t *geoset = model->geosets;
        for (DWORD geosetID = geosetAnim->geosetId;
             geoset && geosetID > 0;
             geosetID--)
        {
            geoset = geoset->next;
        }
        if (geoset) {
            geoset->geosetAnim = geosetAnim;
        }
    }
    model->bounds = MDX_CalculateBounds(model);
    return model;
}

void MDLX_ReleaseModelNode(mdxNode_t *node) {
    SAFE_DELETE(node->translation, ri.MemFree);
    SAFE_DELETE(node->rotation, ri.MemFree);
    SAFE_DELETE(node->scale, ri.MemFree);
}

void MDLX_ReleaseModelGeoset(mdxGeoset_t *geoset) {
    R_Call(glDeleteVertexArrays, 1, &geoset->vertexArrayBuffer);
    R_Call(glDeleteBuffers, MAX_MDLX_BUFFERS, geoset->buffer);

    SAFE_DELETE(geoset->next, MDLX_ReleaseModelGeoset);
    SAFE_DELETE(geoset->vertices, ri.MemFree);
    SAFE_DELETE(geoset->normals, ri.MemFree);
    SAFE_DELETE(geoset->texcoord, ri.MemFree);
    SAFE_DELETE(geoset->matrices, ri.MemFree);
    SAFE_DELETE(geoset->matrixPalette, ri.MemFree);
    SAFE_DELETE(geoset->primitiveTypes, ri.MemFree);
    SAFE_DELETE(geoset->primitiveCounts, ri.MemFree);
    SAFE_DELETE(geoset->triangles, ri.MemFree);
    SAFE_DELETE(geoset->vertexGroups, ri.MemFree);
    SAFE_DELETE(geoset->matrixGroupSizes, ri.MemFree);
    SAFE_DELETE(geoset->bounds, ri.MemFree);
    SAFE_DELETE(geoset, ri.MemFree);
}

void MDLX_ReleaseModelMaterial(mdxMaterial_t *material) {
    SAFE_DELETE(material->next, MDLX_ReleaseModelMaterial);
    if (material->layers) {
        FOR_LOOP(i, material->num_layers) {
            SAFE_DELETE(material->layers[i].alpha, ri.MemFree);
            SAFE_DELETE(material->layers[i].flipbook, ri.MemFree);
        }
        ri.MemFree(material->layers);
    }
    SAFE_DELETE(material->emission, ri.MemFree);
    SAFE_DELETE(material->alpha, ri.MemFree);
    SAFE_DELETE(material->flipbook, ri.MemFree);
    SAFE_DELETE(material, ri.MemFree);
}

void MDLX_ReleaseModelTextureAnim(mdxTextureAnim_t *textureAnim) {
    SAFE_DELETE(textureAnim->next, MDLX_ReleaseModelTextureAnim);
    SAFE_DELETE(textureAnim->translation, ri.MemFree);
    SAFE_DELETE(textureAnim->rotation, ri.MemFree);
    SAFE_DELETE(textureAnim->scale, ri.MemFree);
    SAFE_DELETE(textureAnim, ri.MemFree);
}

void MDLX_ReleaseModelBone(mdxBone_t *bone) {
    MDLX_ReleaseModelNode(&bone->node);
    SAFE_DELETE(bone->next, MDLX_ReleaseModelBone);
    SAFE_DELETE(bone, ri.MemFree);
}

void MDLX_ReleaseModelGeosetAnim(mdxGeosetAnim_t *geosetAnim) {
    SAFE_DELETE(geosetAnim->next, MDLX_ReleaseModelGeosetAnim);
    SAFE_DELETE(geosetAnim->alphas, ri.MemFree);
    SAFE_DELETE(geosetAnim->colors, ri.MemFree);
    SAFE_DELETE(geosetAnim, ri.MemFree);
}

void MDLX_ReleaseModelHelper(mdxHelper_t *helper) {
    MDLX_ReleaseModelNode(&helper->node);
    SAFE_DELETE(helper->next, MDLX_ReleaseModelHelper);
    SAFE_DELETE(helper, ri.MemFree);
}

void MDLX_ReleaseModelLight(mdxLight_t *light) {
    MDLX_ReleaseModelNode(&light->node);
    SAFE_DELETE(light->next, MDLX_ReleaseModelLight);
    SAFE_DELETE(light->keytracks.Visibility, ri.MemFree);
    SAFE_DELETE(light->keytracks.Color, ri.MemFree);
    SAFE_DELETE(light->keytracks.Intensity, ri.MemFree);
    SAFE_DELETE(light->keytracks.AmbColor, ri.MemFree);
    SAFE_DELETE(light->keytracks.AmbIntensity, ri.MemFree);
    SAFE_DELETE(light->keytracks.AttenuationStart, ri.MemFree);
    SAFE_DELETE(light->keytracks.AttenuationEnd, ri.MemFree);
    SAFE_DELETE(light, ri.MemFree);
}

void MDLX_Release(mdxModel_t *model) {
    SAFE_DELETE(model->geosets, MDLX_ReleaseModelGeoset);
    SAFE_DELETE(model->materials, MDLX_ReleaseModelMaterial);
    SAFE_DELETE(model->textureAnims, MDLX_ReleaseModelTextureAnim);
    SAFE_DELETE(model->bones, MDLX_ReleaseModelBone);
    SAFE_DELETE(model->geosetAnims, MDLX_ReleaseModelGeosetAnim);
    SAFE_DELETE(model->helpers, MDLX_ReleaseModelHelper);
    SAFE_DELETE(model->lights, MDLX_ReleaseModelLight);
    SAFE_DELETE(model->textures, ri.MemFree);
    SAFE_DELETE(model->sequences, ri.MemFree);
    SAFE_DELETE(model->globalSequences, ri.MemFree);
    SAFE_DELETE(model->pivots, ri.MemFree);
}
