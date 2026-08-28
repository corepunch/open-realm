#include "renderer/r_local.h"
#include "renderer/r_shader.h"
#include "games/starcraft-2/common/sc2_map.h"
#include "r_m3.h"

#define M3_MAX_NODES 128

#define M3_FOR_EACH(TYPE, VAR, LIST) \
for (m3##TYPE##_t const *VAR = LIST; VAR && VAR < LIST + LIST##Num; VAR++)

#define M3_READ(BUFFER, VAR, VERSION) \
if ((BUFFER)->ent.version > VERSION || VERSION == 0) M3_Read(BUFFER, &VAR, sizeof(VAR));

#define READ_REFERENCE(TARGET, REF, TYPE) { \
    m3Reader_t reader##__LINE__ = M3_MakeSizeBuf(currentmodel, REF); \
    TARGET##Num = reader##__LINE__.valid ? MIN(REF.nEntries, reader##__LINE__.length / sizeof(m3##TYPE##_t)) : 0; \
    TARGET = TARGET##Num ? ri.MemAlloc(sizeof(m3##TYPE##_t) * (TARGET##Num + 1)) : NULL; \
    if (TARGET) memset(TARGET, 0, sizeof(m3##TYPE##_t) * (TARGET##Num + 1)); \
    FOR_LOOP(n, TARGET##Num) { \
        M3_Read##TYPE(currentmodel, &reader##__LINE__, &TARGET[n]); \
    } \
}

#define M3_REFR(BUFFER, TARGET, TYPE, VERSION) \
if ((BUFFER)->ent.version > VERSION) { \
    Reference ref; \
    M3_Read(BUFFER, &ref, sizeof(ref)); \
    READ_REFERENCE(TARGET, ref, TYPE); \
}

#define M3_READER(TYPE) \
void M3_Read##TYPE(m3Model_t *model, m3Reader_t *sb, m3##TYPE##_t *data)

#define M3_SEQUENCE_DATA(TYPE) \
M3_READER(TYPE##SequenceData) { \
    M3_REFR(sb, data->keys, Uint32, 0); \
    M3_READ(sb, data->flags, 0); \
    M3_READ(sb, data->biggestKey, 0); \
    M3_REFR(sb, data->values, TYPE, 0); \
}

static MATRIX4 bonemats[BZ_BONE_PALETTE_MAX];
static MATRIX4 tmp[M3_MAX_NODES];

m3Model_t *currentmodel;

static struct {
    MODELPROG * shader;
    DWORD uDiffuseMap;
    DWORD indexofs;
} m3 = { 0 };

typedef struct {
    struct ReferenceEntry ent;
    DWORD readcount;
    DWORD length;
    BOOL valid;
    void *data;
} m3Reader_t;

void
R_EvalKeyframeValue(void const *left,
                    void const *right,
                    float t,
                    MODELKEYTRACKDATATYPE datatype,
                    MODELKEYTRACKTYPE linetype,
                    HANDLE out);

/* M3 diffuse uses the same shadow-casting authored key as SC2 terrain. */
static void M3_SetLighting(MODELPROG * shader, renderEntity_t const *entity) {
    sc2Map_t const *map = SC2_MapCurrent();
    sc2MapLighting_t const *src = map ? &map->lighting : NULL;
    /* Layout-camera chrome is a separate scene: the map's colorized ambient made the HUD nearly black. */
    bool const lit = src && src->enabled && !(tr.viewDef.rdflags & RDF_NOWORLDMODEL);
    bool const portrait = entity && (entity->flags & RF_PORTRAIT_LIGHTING);
    /* Portrait/HUD chrome: bright neutral ambient so chrome is visible regardless of map lighting.
       Mirrors WC3's RF_PORTRAIT_LIGHTING path: 0.58 ambient (no directional) / 0.22 (with lights). */
    VECTOR3 ambient = lit ? sc2_light_ambient(src)
                          : (portrait ? (VECTOR3){ 0.58f, 0.58f, 0.58f }
                                      : sc2_light_ambient(NULL));
    MODELLIGHTING state = { .ambient = ambient, .count = SC2_DIFFUSE_LIGHTS };
    /* Fill/back are not extra unshadowed Lambert suns; they otherwise illuminate key-backfacing surfaces. */
    FOR_LOOP(i, SC2_DIFFUSE_LIGHTS) {
        sc2DirectionalLight_t const *light = lit ? &src->directional[i] : NULL;
        bool const enabled = light && light->enabled;
        if (enabled) {
            state.lights[i] = (RMODELLIGHT){
                .dir       = Vector3_unm(&light->direction),
                .color     = light->color,
                .intensity = light->color_multiplier,
                .type      = R_MODEL_LIGHT_DIRECT,
            };
        } else if (!lit && i == 0) {
            state.lights[i] = (RMODELLIGHT){
                .dir       = { 0.577f, 0.577f, 0.577f },
                .color     = { 1.0f, 0.90f, 0.80f },
                /* Portrait chrome: key at 0.62 matching WC3's portrait directional intensity. */
                .intensity = portrait ? 0.62f : 1.0f,
                .type      = R_MODEL_LIGHT_DIRECT,
            };
            if (portrait) state.ambient = (VECTOR3){ 0.22f, 0.22f, 0.22f };
        } else {
            state.lights[i] = (RMODELLIGHT){
                .dir  = { 0.0f, 0.0f, 1.0f },
                .type = R_MODEL_LIGHT_DIRECT,
            };
        }
    }
    R_SetModelLighting(shader, &state);
}

void M3_Read(m3Reader_t *buffer, void *dest, DWORD bytes) {
    if (!dest || bytes == 0) return;
    if (!buffer || !buffer->valid || !buffer->data ||
        buffer->readcount > buffer->length ||
        bytes > buffer->length - buffer->readcount) {
        memset(dest, 0, bytes);
        if (buffer) buffer->valid = false;
        return;
    }
    memcpy(dest, (LPBYTE)buffer->data + buffer->readcount, bytes);
    buffer->readcount += bytes;
}

m3Reader_t M3_MakeSizeBuf(m3Model_t const *model, Reference ref) {
    if (!model || !model->buffer || !model->refs || !model->head || !ref.nEntries ||
        ref.ref >= model->head->nRefs ||
        model->refs[ref.ref].offset >= model->size) {
        return (m3Reader_t){ 0 };
    }
    return (m3Reader_t) {
        .data = (LPBYTE)model->buffer + model->refs[ref.ref].offset,
        .readcount = 0,
        .length = model->size - model->refs[ref.ref].offset,
        .valid = true,
        .ent = model->refs[ref.ref],
    };
}

enum {
    kMaterialStandard = 1,
    kMaterialDisplacement,
    kMaterialComposite,
    kMaterialTerrain,
    kMaterialVolume,
    kMaterialUnknown1,
    kMaterialCreep,
    kMaterialVolumeNoise,
    kMaterialSplatTerrainBake,
    kMaterialUnknown2,
    kMaterialLensFlare,
};

M3_READER(Int16) { M3_Read(sb, data, sizeof(m3Int16_t)); }
M3_READER(Uint16) { M3_Read(sb, data, sizeof(m3Uint16_t)); }
M3_READER(Int32) { M3_Read(sb, data, sizeof(m3Int32_t)); }
M3_READER(Uint32) { M3_Read(sb, data, sizeof(m3Uint32_t)); }
M3_READER(Float32) { M3_Read(sb, data, sizeof(m3Float32_t)); }
M3_READER(Vector2) { M3_Read(sb, data, sizeof(m3Vector2_t)); }
M3_READER(Vector3) { M3_Read(sb, data, sizeof(m3Vector3_t)); }
M3_READER(Vector4) { M3_Read(sb, data, sizeof(m3Vector4_t)); }
M3_READER(Matrix4) { M3_Read(sb, data, sizeof(m3Matrix4_t)); }
M3_READER(Face) { M3_Read(sb, data, sizeof(m3Face_t)); }
M3_READER(Pixel) { M3_Read(sb, data, sizeof(m3Pixel_t)); }

M3_READER(Char) {
    M3_Read(sb, data, sizeof(m3Char_t));
    if (*data == '/')
        *data = '\\';
}

static DWORD M3_VertexUVCount(DWORD flags) {
    DWORD count;

    if (flags & 0x100000) return 4;
    if (flags & 0x80000) count = 3;
    else if (flags & 0x40000) count = 2;
    else count = 1;

    if (flags & 0x40000000)
        count++;
    return MIN(count, 4);
}

static DWORD M3_VertexDiskSize(DWORD flags) {
    return 28 + M3_VertexUVCount(flags) * sizeof(SHORT) * 2 + ((flags & 0x200) ? sizeof(COLOR32) : 0);
}

M3_READER(Vertex) {
    DWORD uv_count = M3_VertexUVCount(model->vertexFlags);
    M3_READ(sb, data->pos, 0);
    M3_READ(sb, data->boneWeight, 0);
    M3_READ(sb, data->boneIndex, 0);
    M3_READ(sb, data->normal, 0);
    data->color = COLOR32_WHITE;
    if (model->vertexFlags & 0x200)
        M3_READ(sb, data->color, 0);
    FOR_LOOP(i, uv_count)
        M3_READ(sb, data->uv[i], 0);
    M3_READ(sb, data->tangent, 0);
}

static void M3_ReadVertexReference(m3Model_t *model, m3Reader_t *sb) {
    Reference ref;
    m3Reader_t reader;
    DWORD stride = M3_VertexDiskSize(model->vertexFlags);
    DWORD count;

    M3_Read(sb, &ref, sizeof(ref));
    reader = M3_MakeSizeBuf(model, ref);
    count = reader.valid && stride ? MIN(ref.nEntries / stride, reader.length / stride) : 0;
    model->verticesNum = count;
    model->vertices = count ? ri.MemAlloc(sizeof(m3Vertex_t) * (count + 1)) : NULL;
    if (model->vertices) memset(model->vertices, 0, sizeof(m3Vertex_t) * (count + 1));
    FOR_LOOP(n, count)
        M3_ReadVertex(model, &reader, &model->vertices[n]);
}

M3_READER(MaterialReference) {
    M3_READ(sb, data->materialType, 0);
    M3_READ(sb, data->materialIndex, 0);
}

M3_READER(CompositeMaterialSection) {
    M3_READ(sb, data->materialReferenceIndex, 0);
    M3_READ(sb, data->alphaFactor, 0);
}

M3_READER(CompositeMaterial) {
    M3_REFR(sb, data->name, Char, 0);
    M3_READ(sb, data->unknown, 0);
    M3_REFR(sb, data->sections, CompositeMaterialSection, 0);
}

M3_READER(Layer) {
    M3_READ(sb, data->unknown0, 0);
    M3_REFR(sb, data->imagePath, Char, 0);
    M3_READ(sb, data->color, 0);
    M3_READ(sb, data->flags, 0);
    M3_READ(sb, data->uvSource1, 0);
    M3_READ(sb, data->colorChannelSetting, 0);
    M3_READ(sb, data->brightMult, 0);
    M3_READ(sb, data->midtoneOffset, 0);
    M3_READ(sb, data->unknown1, 0);
    M3_READ(sb, data->noise, 23);
    M3_READ(sb, data->rttChannel, 0);
    M3_READ(sb, data->video, 0);
    M3_READ(sb, data->flipBook, 0);
    M3_READ(sb, data->uv, 0);
    M3_READ(sb, data->brightness, 0);
    M3_READ(sb, data->triPlanarOffset, 23);
    M3_READ(sb, data->triPlanarScale, 23);
    M3_READ(sb, data->unknown4, 0);
    M3_READ(sb, data->fresnel, 0);
    M3_READ(sb, data->fresnel2, 24);
    
    if (data->imagePath && *data->imagePath) {
        data->texture = R_LoadTexture(data->imagePath);
    }
}

M3_READER(Material) {
    M3_REFR(sb, data->name, Char, 0);
    M3_READ(sb, data->additionalFlags, 0);
    M3_READ(sb, data->flags, 0);
    M3_READ(sb, data->blendMode, 0);
    M3_READ(sb, data->priority, 0);
    M3_READ(sb, data->usedRTTChannels, 0);
    M3_READ(sb, data->specularity, 0);
    M3_READ(sb, data->depthBlendFalloff, 0);
    M3_READ(sb, data->cutoutThreshold, 0);
    M3_READ(sb, data->specMult, 0);
    M3_READ(sb, data->emisMult, 0);
    M3_REFR(sb, data->diffuseLayer, Layer, 0);
    M3_REFR(sb, data->decalLayer, Layer, 0);
    M3_REFR(sb, data->specularLayer, Layer, 0);
    M3_REFR(sb, data->glossLayer, Layer, 15);
    M3_REFR(sb, data->emissiveLayer, Layer, 0);
    M3_REFR(sb, data->emissive2Layer, Layer, 0);
    M3_REFR(sb, data->evioLayer, Layer, 0);
    M3_REFR(sb, data->evioMaskLayer, Layer, 0);
    M3_REFR(sb, data->alphaMaskLayer, Layer, 0);
    M3_REFR(sb, data->alphaMask2Layer, Layer, 0);
    M3_REFR(sb, data->normalLayer, Layer, 0);
    M3_REFR(sb, data->heightLayer, Layer, 0);
    M3_REFR(sb, data->lightMapLayer, Layer, 0);
    M3_REFR(sb, data->ambientOcclusionLayer, Layer, 0);
    M3_READ(sb, data->unknown4, 18);
    M3_READ(sb, data->unknown8, 0);
    M3_READ(sb, data->layerBlendType, 0);
    M3_READ(sb, data->emisBlendType, 0);
    M3_READ(sb, data->emisMode, 0);
    M3_READ(sb, data->specType, 0);
    M3_READ(sb, data->unknown9, 0);
    M3_READ(sb, data->unknown10, 0);
    M3_READ(sb, data->unknown11, 18);
}

M3_READER(Region) {
    M3_READ(sb, data->unknown0, 0);
    M3_READ(sb, data->unknown1, 0);
    M3_READ(sb, data->firstVertexIndex, 0);
    M3_READ(sb, data->verticesCount, 0);
    M3_READ(sb, data->firstTriangleIndex, 0);
    M3_READ(sb, data->triangleIndicesCount, 0);
    M3_READ(sb, data->bonesCount, 0);
    M3_READ(sb, data->firstBoneLookupIndex, 0);
    M3_READ(sb, data->boneLookupIndicesCount, 0);
    M3_READ(sb, data->unknown2, 0);
    M3_READ(sb, data->boneWeightPairsCount, 0);
    M3_READ(sb, data->unknown3, 0);
    M3_READ(sb, data->rootBoneIndex, 0);
    M3_READ(sb, data->unknown4, 3);
    M3_READ(sb, data->unknown5, 4);
}

M3_READER(Batch) {
    M3_READ(sb, data->unknown0, 0);
    M3_READ(sb, data->regionIndex, 0);
    M3_READ(sb, data->unknown1, 0);
    M3_READ(sb, data->materialReferenceIndex, 0);
    M3_READ(sb, data->unknown2, 0);
}

M3_READER(Divisions) {
    M3_REFR(sb, data->faces, Face, 0);
    M3_REFR(sb, data->regions, Region, 0);
    M3_REFR(sb, data->batches, Batch, 0);
    M3_READ(sb, data->MSEC, 0);
}

M3_READER(Sequence) {
    M3_READ(sb, data->unknown, 0);
    M3_REFR(sb, data->name, Char, 0);
    M3_READ(sb, data->interval, 0);
    M3_READ(sb, data->movementSpeed, 0);
    M3_READ(sb, data->flags, 0);
    M3_READ(sb, data->frequency, 0);
    M3_READ(sb, data->unk, 0);
    if (sb->ent.version < 2)
        M3_READ(sb, data->unk2, 0);
    M3_READ(sb, data->boundingSphere, 0);
    M3_READ(sb, data->d5, 0);
//    printf("  %s\n", data->name);
}

M3_READER(SequenceTimeline) {
    M3_REFR(sb, data->name, Char, 0);
    M3_READ(sb, data->runsConcurrent, 0);
    M3_READ(sb, data->priority, 0);
    M3_READ(sb, data->stsIndex, 0);
    M3_READ(sb, data->stsIndexCopy, 0);
    M3_REFR(sb, data->animIds, Uint32, 0);
    M3_REFR(sb, data->animRefs, Uint32, 0);
    M3_READ(sb, data->d3, 0);
    FOR_LOOP(i, 13) {
        M3_READ(sb, data->sd[i], 0);
    }
}

M3_READER(SequenceValidator) {
    M3_REFR(sb, data->animIds, Uint32, 0);
    M3_READ(sb, data->unk, 0);
}

M3_READER(SequenceGetter) {
    M3_REFR(sb, data->name, Char, 0);
    M3_REFR(sb, data->stcID, Uint32, 0);
}

M3_READER(Bone) {
    M3_READ(sb, data->unknown0, 0);
    M3_REFR(sb, data->name, Char, 0);
    M3_READ(sb, data->flags, 0);
    M3_READ(sb, data->parent, 0);
    M3_READ(sb, data->unknown1, 0);
    M3_READ(sb, data->position, 0);
    M3_READ(sb, data->rotation, 0);
    M3_READ(sb, data->scale, 0);
    M3_READ(sb, data->visibility, 0);
}

/* Converts M3 vertex data (SHORT UV ÷ 2048, ubyte normal ×2−1) to the unified
   float layout expected by the shared model shader. Uploads a VERTEX array so
   M3 uses the same VAO layout as MDX/M2. */
void M3_MakeBuffer(m3Model_t *model) {
    VERTEX *verts = model->verticesNum ? ri.MemAlloc(model->verticesNum * sizeof(VERTEX)) : NULL;
    DWORD elems = 0;

    FOR_LOOP(i, model->verticesNum) {
        m3Vertex_t const *src = &model->vertices[i];
        VERTEX *dst = &verts[i];
        dst->position = src->pos;
        dst->texcoord = (VECTOR2){ src->uv[0][0] / 2048.0f, src->uv[0][1] / 2048.0f };
        dst->normal = (VECTOR3){
            src->normal[0] / 127.5f - 1.0f,
            src->normal[1] / 127.5f - 1.0f,
            src->normal[2] / 127.5f - 1.0f,
        };
        dst->color = src->color;
        memcpy(dst->skin, src->boneIndex, 4);
        memcpy(dst->boneWeight, src->boneWeight, 4);
    }

    M3_FOR_EACH(Divisions, div, model->divisions) elems += div->facesNum;
    USHORT *indices = elems ? ri.MemAlloc(elems * sizeof(*indices)) : NULL;
    m3_pack_division_faces(model->divisions, model->divisionsNum, indices);
    model->renbuf = R_MakeVertexArrayObject(verts, model->verticesNum);
    R_Call(glBindVertexArray, model->renbuf->vao);
    R_Call(glGenBuffers, 1, &model->renbuf->ibo);
    R_Call(glBindBuffer, GL_ELEMENT_ARRAY_BUFFER, model->renbuf->ibo);
    R_Call(glBufferData, GL_ELEMENT_ARRAY_BUFFER, elems * sizeof(*indices), indices, GL_STATIC_DRAW);
    ri.MemFree(indices);
    ri.MemFree(verts);
}

void M3_InitMODL(m3Model_t *model, m3Reader_t sb) {
    M3_REFR(&sb, model->modelName, Char, 0);
    M3_READ(&sb, model->flags, 0);
    M3_REFR(&sb, model->sequences, Sequence, 0);
    M3_REFR(&sb, model->stc, SequenceTimeline, 0);
    M3_REFR(&sb, model->stg, SequenceGetter, 0);
    M3_READ(&sb, model->unknown0, 0);
    M3_REFR(&sb, model->sts, SequenceValidator, 0);
    M3_REFR(&sb, model->bones, Bone, 0);
    M3_READ(&sb, model->numberOfBonesToCheckForSkin, 0);
    M3_READ(&sb, model->vertexFlags, 0);
    M3_ReadVertexReference(model, &sb);
    M3_REFR(&sb, model->divisions, Divisions, 0);
    M3_REFR(&sb, model->boneLookup, Uint16, 0);
    M3_READ(&sb, model->boundings, 0);
    M3_READ(&sb, model->unknown4, 0);
    M3_READ(&sb, model->attachmentPoints, 0);
    M3_READ(&sb, model->attachmentPointAddons, 0);
    M3_READ(&sb, model->ligts, 0);
    M3_READ(&sb, model->shbxData, 0);
    M3_READ(&sb, model->cameras, 0);
    M3_READ(&sb, model->unknown21, 0);
    // Materials
    M3_REFR(&sb, model->materialReferences, MaterialReference, 0);
    M3_REFR(&sb, model->materialStandard, Material, 0);
    M3_READ(&sb, model->materialDisplacement, 0);
    M3_REFR(&sb, model->materialComposite, CompositeMaterial, 0);
    M3_READ(&sb, model->materialTerrain, 0);
    M3_READ(&sb, model->materialVolume, 0);
    M3_READ(&sb, model->materialUnknown1, 0);
    M3_READ(&sb, model->materialCreep, 0);
    M3_READ(&sb, model->materialVolumeNoise, 24);
    M3_READ(&sb, model->materialSplatTerrainBake, 25);
    M3_READ(&sb, model->materialUnknown2, 27);
    M3_READ(&sb, model->materialLensFlare, 28);
    // Particles
    M3_READ(&sb, model->particleEmitters, 0);
    M3_READ(&sb, model->particleEmitterCopies, 0);
    M3_READ(&sb, model->ribbonEmitters, 0);
    M3_READ(&sb, model->projections, 0);
    M3_READ(&sb, model->forces, 0);
    M3_READ(&sb, model->warps, 0);
    M3_READ(&sb, model->unknown22, 0);
    M3_READ(&sb, model->rigidBodies, 0);
    M3_READ(&sb, model->unknown23, 0);
    M3_READ(&sb, model->physicsJoints, 0);
    M3_READ(&sb, model->clothBehavior, 27)
    M3_READ(&sb, model->unknown24, 0);
    M3_READ(&sb, model->ikjtData, 0);
    M3_READ(&sb, model->unknown25, 0);
    M3_READ(&sb, model->unknown26, 24);
    M3_READ(&sb, model->partsOfTurrentBehaviors, 0);
    M3_READ(&sb, model->turrentBehaviors, 0);
    M3_REFR(&sb, model->absoluteInverseBoneRestPositions, Matrix4, 0);
    M3_READ(&sb, model->tightHitTest, 0);
    M3_READ(&sb, model->fuzzyHitTestObjects, 0);
    M3_READ(&sb, model->attachmentVolumes, 0);
    M3_READ(&sb, model->attachmentVolumesAddon0, 0);
    M3_READ(&sb, model->attachmentVolumesAddon1, 0);
    M3_READ(&sb, model->billboardBehaviors, 0);
    M3_READ(&sb, model->tmdData, 0);
    M3_READ(&sb, model->unknown27, 0);
    M3_READ(&sb, model->unknown28, 0);

    M3_MakeBuffer(model);
}

m3Uint32_t M3_FindAnimRef(m3SequenceTimeline_t const *timeline, m3Uint32_t animID) {
    if (!timeline)
        return 0;
    M3_FOR_EACH(Uint32, it, timeline->animIds) {
        if (animID == *it) {
            return timeline->animRefs[it - timeline->animIds];
        }
    }
    return 0;
}

DWORD M3_FindKeyAtTime(m3Uint32_t const *keys, DWORD numkeys, DWORD time, float *t) {
    if (numkeys == 0 || *keys > time)
        return 0;
    FOR_LOOP(b, numkeys) {
        if (keys[b] > time) {
            DWORD a = b - 1;
            *t = (float)(time - keys[a]) / (float)(keys[b] - keys[a]);
            return b;
        }
    }
    *t = 1;
    return numkeys - 1;
}

#define M3_GET_POINTER(MODEL, REF, TYPE) ((m3##TYPE##_t const *)M3_MakeSizeBuf(MODEL, REF).data)

#define M3_GET_ANIM_VALUE(ANIMREF, DATATYPE) \
M3_Get##ANIMREF##AnimValue(m3Model_t const *model, \
                           m3SequenceTimeline_t const *timeline, \
                           m3##ANIMREF##AnimRef_t const *animref, \
                           DWORD time) { \
    if (!model || !timeline || !animref) return animref ? animref->initValue : (m3##ANIMREF##_t){ 0 }; \
    m3Uint32_t const anim = M3_FindAnimRef(timeline, animref->animId); \
    if (anim == 0) return animref->initValue; \
    DWORD const sdref = anim >> 16; \
    DWORD const sdindex = anim & 0xffff; \
    if (sdref >= 13 || sdindex >= timeline->sd[sdref].nEntries) return animref->initValue; \
    m3SequenceData_t const *sdbase = M3_GET_POINTER(model, timeline->sd[sdref], SequenceData); \
    if (!sdbase) return animref->initValue; \
    m3SequenceData_t const *sd = sdbase + sdindex; \
    m3##ANIMREF##_t output = animref->initValue; \
    m3Float32_t t = 0.f; \
    m3Uint32_t const *keys = M3_GET_POINTER(model, sd->keys, Uint32); \
    if (!keys) return animref->initValue; \
    DWORD key = M3_FindKeyAtTime(keys, sd->keys.nEntries, time, &t); \
    if (key > 0) { \
        m3##ANIMREF##_t const *values = M3_GET_POINTER(model, sd->values, ANIMREF); \
        if (!values || key >= sd->values.nEntries) return animref->initValue; \
        R_EvalKeyframeValue(values+key-1, values+key, t, DATATYPE, TRACK_LINEAR, &output); \
        return output; \
    } else { \
        return animref->initValue; \
    } \
}

DWORD   M3_GET_ANIM_VALUE(Uint32,  TDATA_INT1);
float   M3_GET_ANIM_VALUE(Float32, TDATA_FLOAT1);
VECTOR3 M3_GET_ANIM_VALUE(Vector3, TDATA_FLOAT3);
VECTOR4 M3_GET_ANIM_VALUE(Vector4, TDATA_FLOAT4);

static BOOL M3_MaterialIsBlended(m3Material_t const *material) {
    return material && material->blendMode >= BLEND_MODE_BLEND;
}

static BOOL M3_MaterialHasAlphaMask(m3Material_t const *material) {
    return material &&
        material->alphaMaskLayer &&
        material->alphaMaskLayer->texture;
}

static FLOAT M3_MaterialAlphaCutoff(m3Material_t const *material) {
    if (!material) {
        return 1.0f;
    }
    if (material->cutoutThreshold > 0) {
        return (FLOAT)material->cutoutThreshold / 255.0f;
    }
    if (!M3_MaterialIsBlended(material) && M3_MaterialHasAlphaMask(material)) {
        return 0.5f;
    }
    return -1.0f;
}

static COLOR32 M3_LayerColor(m3Layer_t const *layer) {
    COLOR32 color;

    if (!layer)
        return COLOR32_WHITE;
    color = layer->color.initValue;
    if (!color.r && !color.g && !color.b && !color.a)
        return COLOR32_WHITE;
    return color;
}

static BOOL M3_SetMaterialBlendMode(m3Material_t const *material) {
    R_SetAlphaKeyState(false);
#ifdef USE_SHADOWMAPS
    switch (tr.render_phase == RENDER_PHASE_LIGHTS ? (int)(material ? material->blendMode : BLEND_MODE_NONE) : -1) {
        case BLEND_MODE_BLEND:
        case BLEND_MODE_ADD:
        case BLEND_MODE_ADDALPHA:
        case BLEND_MODE_MODULATE:
        case BLEND_MODE_MODULATE_2X:
            return false;
    }
#endif
    switch (material ? material->blendMode : BLEND_MODE_NONE) {
        case BLEND_MODE_NONE:
        case BLEND_MODE_ALPHAKEY:
            R_Call(glDisable, GL_BLEND);
            R_Call(glBlendFunc, GL_ONE, GL_ZERO);
            R_Call(glDepthMask, GL_TRUE);
            break;
        case BLEND_MODE_BLEND:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_ADD:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_ONE, GL_ONE);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_ADDALPHA:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_SRC_ALPHA, GL_ONE);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_MODULATE:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_DST_COLOR, GL_ZERO);
            R_Call(glDepthMask, GL_FALSE);
            break;
        case BLEND_MODE_MODULATE_2X:
            R_Call(glEnable, GL_BLEND);
            R_Call(glBlendFunc, GL_DST_COLOR, GL_SRC_COLOR);
            R_Call(glDepthMask, GL_FALSE);
            break;
        default:
            R_Call(glDisable, GL_BLEND);
            R_Call(glBlendFunc, GL_ONE, GL_ZERO);
            R_Call(glDepthMask, GL_TRUE);
            break;
    }
    return true;
}

m3Model_t *R_LoadModelM3(void *data, DWORD size) {
    m3Model_t *model = ri.MemAlloc(sizeof(m3Model_t));
    if (!model)
        return NULL;
    memset(model, 0, sizeof(*model));
    if (!data || size < sizeof(struct MD33)) {
        return model;
    }
    model->buffer = malloc(size);
    if (!model->buffer) {
        return model;
    }
    model->size = size;
    memcpy(model->buffer, data, size);
    model->head = model->buffer;
    if (*(DWORD const *)model->head->id != ID_43DM ||
        model->head->ofsRefs >= model->size ||
        model->head->nRefs > (model->size - model->head->ofsRefs) / sizeof(struct ReferenceEntry) ||
        model->head->MODL.ref >= model->head->nRefs) {
        fprintf(stderr, "R_LoadModelM3: invalid header\n");
        return model;
    }
    model->refs = (struct ReferenceEntry *)((LPBYTE)model->buffer + model->head->ofsRefs);
    model->type = model->refs[model->head->MODL.ref].version;
    currentmodel = model;
    M3_InitMODL(model, M3_MakeSizeBuf(model, model->head->MODL));
    return model;
}

/* Additive emissive/glow layer, drawn inline right after the material's diffuse
   layer.  Mirrors Quake 3's glow stage (GLS_SRCBLEND_ONE|GLS_DSTBLEND_ONE): the
   diffuse stage already wrote depth, so this only reads it — no separate scene
   pass and no second bone-palette rebuild. */
static void M3_DrawEmissiveLayer(m3Region_t const *region, m3Material_t const *material, FLOAT alpha) {
    m3Layer_t const *emissive = material->emissiveLayer;
    if (!emissive || !emissive->texture)
        return;
    COLOR32 ec = M3_LayerColor(emissive);
    BOOL prev_unshaded = m3.shader->state.unshaded;
    R_Call(glEnable, GL_BLEND);
    R_Call(glBlendFunc, GL_ONE, GL_ONE);
    R_Call(glDepthMask, GL_FALSE);
    m3.shader->state.unshaded = 1;
    m3.shader->state.alphaKey = 0;
    m3.shader->state.geosetColor = (VECTOR4){ ec.r / 255.0f, ec.g / 255.0f, ec.b / 255.0f, ec.a / 255.0f * alpha };
    m3.shader->state.firstBoneLookupIndex = (FLOAT)region->firstBoneLookupIndex;
    R_Call(glActiveTexture, GL_TEXTURE0);
    R_Call(glBindTexture, GL_TEXTURE_2D, emissive->texture->texid);
#ifndef __linux__
    R_StatsDraw(GL_TRIANGLES, region->triangleIndicesCount, 1);
    R_ApplyShader(m3.shader);
    R_Call(glDrawElementsBaseVertex, GL_TRIANGLES, region->triangleIndicesCount, GL_UNSIGNED_SHORT,
           (HANDLE)(uintptr_t)(m3.indexofs + sizeof(USHORT) * region->firstTriangleIndex), region->firstVertexIndex);
#endif
    m3.shader->state.unshaded = prev_unshaded;
}

static void M3_DrawRegionMaterial(m3Region_t const *region, m3Material_t const *material, FLOAT alpha) {
    LPCTEXTURE diffuse = material->diffuseLayer && material->diffuseLayer->texture ? material->diffuseLayer->texture : tr.texture[TEX_WHITE];
    COLOR32 diffuse_color = M3_LayerColor(material->diffuseLayer);
#ifndef __linux__
    DWORD const num_indices = region->triangleIndicesCount;
    DWORD const first_vertex = region->firstVertexIndex;
    HANDLE const indices = (HANDLE)(uintptr_t)(m3.indexofs + sizeof(USHORT) * region->firstTriangleIndex);
#endif

    if (!M3_SetMaterialBlendMode(material)) {
        return;
    }
    m3.shader->state.geosetColor = (VECTOR4){ diffuse_color.r / 255.0f, diffuse_color.g / 255.0f, diffuse_color.b / 255.0f, diffuse_color.a / 255.0f * alpha };
    {
        FLOAT cutoff = M3_MaterialAlphaCutoff(material);
        BOOL alpha_key = cutoff >= 0.0f;
        m3.shader->state.alphaKey = alpha_key;
        m3.shader->state.alphaCutoff = cutoff >= 0.0f ? cutoff : 0.5f;
        if (alpha_key && !M3_MaterialIsBlended(material)) R_SetAlphaKeyState(true);
    }
    m3.shader->state.firstBoneLookupIndex = (FLOAT)region->firstBoneLookupIndex;
    R_Call(glActiveTexture, GL_TEXTURE0);
    R_Call(glBindTexture, GL_TEXTURE_2D, diffuse->texid);
    M3_FOR_EACH(Layer, layer, material->diffuseLayer) {
        if (!layer->texture)
            continue;
#ifndef __linux__
        R_StatsDraw(GL_TRIANGLES, num_indices, 1);
        R_ApplyShader(m3.shader);
        R_Call(glDrawElementsBaseVertex, GL_TRIANGLES, num_indices, GL_UNSIGNED_SHORT, indices, first_vertex);
#endif
    }
    /* Shadow-map pass only fills depth; glow belongs to the lit color pass. */
    if (tr.render_phase != RENDER_PHASE_LIGHTS)
        M3_DrawEmissiveLayer(region, material, alpha);
}

static void M3_DrawRegionMaterialReference(m3Model_t const *model,
                                           m3Region_t const *region,
                                           m3MaterialReference_t const *mref,
                                           FLOAT alpha,
                                           BOOL blendedPass,
                                           DWORD depth) {
    if (!model || !region || !mref || depth > 4)
        return;
    switch (mref->materialType) {
        case kMaterialStandard:
            if (mref->materialIndex < model->materialStandardNum) {
                m3Material_t const *material = model->materialStandard+mref->materialIndex;
                if (M3_MaterialIsBlended(material) == blendedPass)
                    M3_DrawRegionMaterial(region, material, alpha);
            }
            break;
        case kMaterialComposite:
            if (mref->materialIndex >= model->materialCompositeNum)
                break;
            m3CompositeMaterial_t const *composite = model->materialComposite+mref->materialIndex;
            M3_FOR_EACH(CompositeMaterialSection, section, composite->sections) {
                if (section->materialReferenceIndex >= model->materialReferencesNum)
                    continue;
                M3_DrawRegionMaterialReference(model,
                                               region,
                                               model->materialReferences+section->materialReferenceIndex,
                                               alpha * section->alphaFactor.initValue,
                                               blendedPass,
                                               depth + 1);
            }
            break;
    }
}

void M3_DrawDivisions(m3Model_t const *model, m3Divisions_t const *divisions, BOOL blendedPass) {
    if (!model || !model->renbuf || !model->renbuf->ibo || !divisions)
        return;
    m3.indexofs = divisions->indexofs;
    M3_FOR_EACH(Batch, batch, divisions->batches) {
        if (batch->regionIndex >= divisions->regionsNum ||
            batch->materialReferenceIndex >= model->materialReferencesNum)
            continue;
        M3_DrawRegionMaterialReference(model,
                                       divisions->regions+batch->regionIndex,
                                       model->materialReferences+batch->materialReferenceIndex,
                                       1.0f,
                                       blendedPass,
                                       0);
    }
}

void M3_MakeBoneMatrix(LPCVECTOR3 p, LPCVECTOR4 r, LPCVECTOR3 s, LPCMATRIX4 par, LPMATRIX4 m) {
    MATRIX4 matrix;
    Matrix4_identity(&matrix);
    Matrix4_translate(&matrix, p);
    Matrix4_rotate4(&matrix, r);
    Matrix4_scale(&matrix, s);
    Matrix4_multiply(par, &matrix, m);
}

//m3Sequence_t const *
//M3_FindSequenceByName(m3Model_t const *model,
//                      LPCSTR name)
//{
//    M3_FOR_EACH(Sequence, seq, model->sequences) {
//        if (!strcmp(seq->name, name)) {
//            return seq;
//        }
//    }
//    return NULL;
//}

m3SequenceTimeline_t const *
M3_FindSequenceTimeline(m3Model_t const *model,
                        m3Sequence_t const *seq)
{
    M3_FOR_EACH(SequenceTimeline, stc, model->stc) {
        if (stc->stsIndex == seq - model->sequences) {
            return stc;
        }
    }
    return NULL;
}

m3SequenceTimeline_t const *
M3_FindAnimationAtTime(m3Model_t const *model,
                       DWORD time,
                       DWORD *localtime)
{
    M3_FOR_EACH(Sequence, seq, model->sequences) {
        if (time < seq->interval[1]) {
            *localtime = time;
            return M3_FindSequenceTimeline(model, seq);
        } else {
            time -= seq->interval[1];
        }
    }
    return NULL;
}

void M3_RenderModel(renderEntity_t const *entity, m3Model_t const *model, LPCMATRIX4 transform) {
    MATRIX4 identity;
    if (!entity || !model || !model->renbuf || !model->bones || !model->absoluteInverseBoneRestPositions)
        return;
    Matrix4_identity(&identity);
    
    struct {
        m3SequenceTimeline_t const *stc;
        DWORD time;
    } a, b;
    
    a.stc = M3_FindAnimationAtTime(model, entity->oldframe, &a.time);
    b.stc = M3_FindAnimationAtTime(model, entity->frame, &b.time);

    M3_FOR_EACH(Bone, bone, model->bones) {
        LPCMATRIX4 parent = bone->parent >= 0 && bone->parent < (SHORT)model->bonesNum ? tmp+bone->parent : &identity;
        VECTOR3 a_p = M3_GetVector3AnimValue(model, a.stc, &bone->position, a.time);
        VECTOR4 a_r = M3_GetVector4AnimValue(model, a.stc, &bone->rotation, a.time);
        VECTOR3 a_s = M3_GetVector3AnimValue(model, a.stc, &bone->scale, a.time);
        VECTOR3 b_p = M3_GetVector3AnimValue(model, b.stc, &bone->position, b.time);
        VECTOR4 b_r = M3_GetVector4AnimValue(model, b.stc, &bone->rotation, b.time);
        VECTOR3 b_s = M3_GetVector3AnimValue(model, b.stc, &bone->scale, b.time);
        VECTOR3 p = Vector3_lerp(&a_p, &b_p, tr.viewDef.lerpfrac);
        QUATERNION r = Quaternion_slerp((LPCQUATERNION)&a_r, (LPCQUATERNION)&b_r, tr.viewDef.lerpfrac);
        VECTOR3 s = Vector3_lerp(&a_s, &b_s, tr.viewDef.lerpfrac);
//        float v = M3_GetUint32AnimValue(model, a.stc, &bone->visibility, a.time);
        M3_MakeBoneMatrix(&p, (LPCVECTOR4)&r, &s, parent, tmp+(bone-model->bones));
    }

    /* Build a full 128-entry bone palette indexed by boneLookup[i].
       Vertex boneIndex values are boneLookup-relative so we pre-multiply
       the inverse rest pose here, making every vertex index an absolute
       palette slot. This removes the need for uFirstBoneLookupIndex. */
    memset(bonemats, 0, sizeof(bonemats));
    FOR_LOOP(j, BZ_BONE_PALETTE_MAX) {
        MATRIX4 ident; Matrix4_identity(&ident); bonemats[j] = ident;
    }
    M3_FOR_EACH(Uint16, boneLookup, model->boneLookup) {
        m3Uint16_t boneIndex = *boneLookup;
        DWORD paletteIndex = (DWORD)(boneLookup - model->boneLookup);
        if (boneIndex >= model->bonesNum || boneIndex >= model->absoluteInverseBoneRestPositionsNum ||
            paletteIndex >= BZ_BONE_PALETTE_MAX) {
            continue;
        }
        Matrix4_multiply(tmp + boneIndex, model->absoluteInverseBoneRestPositions + boneIndex,
                         bonemats + paletteIndex);
    }

    MATRIX4 mScaledMatrix;
    MATRIX3 mNormalMatrix;

    memcpy(&mScaledMatrix, transform, sizeof(MATRIX4));
    // SC2 placed-object rotations are already in the entity matrix; the old global M3 +90 made bridges and doodads quarter-turn too far.
//    Matrix4_rotate(&mScaledMatrix, &(VECTOR3){0,0,90/*tr.viewDef.time*0.05*/}, ROTATE_ZYX);
    // SC2 entity scale is already applied by R_GetEntityMatrix; the old 100x loader scale put the camera inside units.
//    Matrix4_scale(&mScaledMatrix, &(VECTOR3){100,100,100});
    Matrix3_normal(&mNormalMatrix, &mScaledMatrix);

    R_Call(glDisable, GL_BLEND);
    R_Call(glEnable, GL_DEPTH_TEST);
    R_Call(glDepthMask, GL_TRUE);

#ifdef USE_SHADOWMAPS
    if (tr.render_phase == RENDER_PHASE_LIGHTS) {
        m3.shader->state.viewProjection = tr.viewDef.lightMatrix;
    } else {
        m3.shader->state.viewProjection = tr.viewDef.viewProjectionMatrix;
    }
#else
    m3.shader->state.viewProjection = tr.viewDef.viewProjectionMatrix;
#endif
    m3.shader->state.lightMatrix = tr.viewDef.lightMatrix;
    m3.shader->state.textureMatrix = tr.viewDef.textureMatrix;
    m3.shader->state.model = mScaledMatrix;
    m3.shader->state.normalMatrix = mNormalMatrix;
    memcpy(&m3.shader->state.bones, bonemats->v, (MIN(model->boneLookupNum, BZ_BONE_PALETTE_MAX)) * sizeof(MATRIX4));
    m3.shader->state.boneCount = MAX(1, MIN(model->boneLookupNum, BZ_BONE_PALETTE_MAX));
    M3_SetLighting(m3.shader, entity);
    /* The unified model shader requires identity defaults for uniforms that
       M3 does not animate (texture UV transform, layer alpha, geoset colour). */
    m3.shader->state.geosetColor = (VECTOR4){ 1.0f, 1.0f, 1.0f, 1.0f };
    m3.shader->state.layerAlpha = 1.0f;
    { GLfloat m[9] = { 1,0,0, 0,1,0, 0,0,1 }; memcpy(&m3.shader->state.uvMatrix, m, (1) * sizeof(MATRIX3)); }
    m3.shader->state.alphaKey = 0;
    m3.shader->state.alphaCutoff = 0.5f;
    /* Portrait/HUD chrome renders at full texture brightness; lighting is cosmetic there. */
    m3.shader->state.unshaded = entity && (entity->flags & RF_PORTRAIT_LIGHTING) ? 1 : 0;
    m3.shader->state.fogEnable = 0;
    m3.shader->state.firstBoneLookupIndex = 0.0f;
    R_Call(glBindVertexArray, model->renbuf->vao);
    R_Call(glBindBuffer, GL_ARRAY_BUFFER, model->renbuf->vbo);
    
    R_BindTexture(tr.texture[TEX_WHITE], 0);
    /* Terrain owns units 0..4; restore the model's shadow binding and avoid depth-target feedback. */
    R_Call(glActiveTexture, GL_TEXTURE1);
    R_Call(glBindTexture, GL_TEXTURE_2D, tr.render_phase == RENDER_PHASE_LIGHTS || (tr.viewDef.rdflags & RDF_NOWORLDMODEL) ? tr.texture[TEX_WHITE]->texid : tr.rt[RT_DEPTHMAP]->texture);
    
    R_Call(glDisable, GL_CULL_FACE);
    
    M3_FOR_EACH(Divisions, div, model->divisions) {
        M3_DrawDivisions(model, div, false);
    }
    M3_FOR_EACH(Divisions, div, model->divisions) {
        M3_DrawDivisions(model, div, true);
    }
    
    R_Call(glActiveTexture, GL_TEXTURE0);
    R_SetAlphaKeyState(false);
    R_Call(glDepthMask, GL_TRUE);
    R_Call(glEnable, GL_BLEND);
}

/* Draw generated geometry through an M3 model's first authored material. */
void M3_RenderBuffer(renderEntity_t const *entity, m3Model_t const *model, LPCBUFFER buffer, DWORD vertices, DWORD indices) {
    m3Model_t view;
    m3Divisions_t div = {0};
    m3Region_t region = {.verticesCount=vertices,.triangleIndicesCount=indices,.bonesCount=1,.boneLookupIndicesCount=1};
    m3Batch_t batch = {0};
    MATRIX4 identity;

    if (!model || !buffer || !indices || !model->divisions || !model->divisionsNum ||
        !model->divisions[0].batches || !model->divisions[0].batchesNum) return;
    view = *model; batch.materialReferenceIndex = model->divisions[0].batches[0].materialReferenceIndex;
    div.regions = &region; div.regionsNum = 1;
    div.batches = &batch; div.batchesNum = 1;
    view.renbuf = (LPBUFFER)buffer; view.divisions = &div; view.divisionsNum = 1;
    Matrix4_identity(&identity); M3_RenderModel(entity, &view, &identity);
}

void M3_Init(void) {
    m3.shader = R_ModelShader();
    /* uTexture (unit 0) is wired up by R_InitShader; diffuse texture binds to unit 0. */
}

void M3_Shutdown(void) {
    /* m3.shader is the shared model shader, released by the renderer. */
}
