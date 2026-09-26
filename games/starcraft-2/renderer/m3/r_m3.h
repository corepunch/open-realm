#ifndef __r_m3_h__
#define __r_m3_h__

#include "common/shared.h"

#define M3_ENTRIES(TYPE, NAME)\
uint32_t NAME##Num; \
m3##TYPE##_t *NAME;

typedef uint16_t m3Face_t;
typedef char m3Char_t;
typedef float m3Float32_t;
typedef mat4_t m3Matrix4_t;
typedef int32_t m3Int32_t;
typedef uint32_t m3Uint32_t;
typedef int16_t m3Int16_t;
typedef uint16_t m3Uint16_t;
typedef vec2_t m3Vector2_t;
typedef vec3_t m3Vector3_t;
typedef vec4_t m3Vector4_t;
typedef color32_t m3Pixel_t;

typedef struct {
    uint16_t interpolationType;
    uint16_t animFlags;
    uint32_t animId;
} m3AnimRef_t;

#define M3_DECL_ANIMREF(name, type) \
typedef struct { \
    uint16_t interpolationType; \
    uint16_t animFlags; \
    uint32_t animId; \
    type initValue; \
    type nullValue; \
    uint32_t unknown; \
} m3##name##AnimRef_t;

M3_DECL_ANIMREF(Pixel, color32_t);
M3_DECL_ANIMREF(Uint16, uint16_t);
M3_DECL_ANIMREF(Uint32, uint32_t);
M3_DECL_ANIMREF(Float32, float);
M3_DECL_ANIMREF(Vector2, vec2_t);
M3_DECL_ANIMREF(Vector3, vec3_t);
M3_DECL_ANIMREF(Vector4, vec4_t);

typedef struct {
    vec3_t min;
    vec3_t max;
    float radius;
} BoundingSphere;

typedef struct {
    uint32_t shape;
    uint16_t bone;
    uint16_t unknown0;
    mat4_t matrix;
    uint32_t unknown[6];
    vec3_t size;
} BoundingShape;

typedef struct {
    uint32_t nEntries;
    uint32_t ref;
    uint32_t flags;
} Reference;

struct ReferenceEntry {
    char id[4];
    uint32_t offset;
    uint32_t nEntries;
    uint32_t version;
};

struct m3Header
{
    char id[4];
    uint32_t ofsRefs;
    uint32_t nRefs;
    Reference MODL;
};

typedef struct {
    Reference keys;
    uint32_t flags;
    uint32_t biggestKey;
    Reference values;
} m3SequenceData_t;

typedef struct {
    int32_t unknown0; // Keybone?
    M3_ENTRIES(Char, name);
    uint32_t flags;
    int16_t parent;
    int16_t unknown1;
    m3Vector3AnimRef_t position;
    m3Vector4AnimRef_t rotation;
    m3Vector3AnimRef_t scale;
    m3Uint32AnimRef_t visibility;
} m3Bone_t;

typedef struct m3Vertex_s {
    vec3_t pos;
    uint8_t boneWeight[4];
    uint8_t boneIndex[4];
    uint8_t normal[4];
    color32_t color;
    int16_t uv[4][2];
    uint8_t tangent[4];
} m3Vertex_t;

struct m3MaterialMap
{
    uint32_t d1;
    uint32_t d2; // Index into MAT-table?
};

struct m3Material
{
    Reference name;
    int ukn1[8];
    float x, y;  //always 1.0f
    Reference layers[13];
    int ukn2[15];
};

struct m3Layer
{
    int unk;
    Reference name;
    float unk2[85];
};

struct m3Division {
    Reference faces; // U16
    Reference regions; // REGN - Region
    Reference BAT;
    Reference MSEC;
};

typedef struct {
    uint32_t unknown0;
    uint32_t unknown1;
    uint32_t firstVertexIndex;
    uint32_t verticesCount;
    uint32_t firstTriangleIndex;
    uint32_t triangleIndicesCount;
    uint16_t bonesCount;
    uint16_t firstBoneLookupIndex;
    uint16_t boneLookupIndicesCount;
    uint16_t unknown2;
    uint8_t boneWeightPairsCount;
    uint8_t unknown3;
    uint16_t rootBoneIndex;
    uint32_t unknown4;
    uint32_t unknown5[2];
} m3Region_t;

struct m3Camera
{
    /*0x00*/ int32_t d1;
    /*0x04*/ Reference name;
    /*0x0C*/ uint16_t flags1;
    /*0x0E*/ uint16_t flags2;
};

struct m3Event
{
    /*0x00*/ Reference name;
    /*0x08*/ int16_t unk1[4];
    /*0x10*/ float matrix[4][4];
    /*0x50*/ int32_t unk2[4];
};

struct m3Attachment
{
    /*0x00*/ int32_t unk;
    /*0x04*/ Reference name;
    /*0x0C*/ int32_t bone;
};

struct m3Physics
{
    float m[4][4];
    float f1;
    float f2;
    Reference refs[5];
    float f3;
};

typedef struct {
    uint32_t unknown[2];
    M3_ENTRIES(Char, name);
    uint32_t interval[2];
    float movementSpeed;
    uint32_t flags;
    uint32_t frequency;
    int32_t unk[3];
    int32_t unk2;
    BoundingSphere boundingSphere;
    int32_t d5[3];
} m3Sequence_t;

typedef struct {
    M3_ENTRIES(Char, name);
    uint16_t runsConcurrent;
    uint16_t priority;
    uint16_t stsIndex;
    uint16_t stsIndexCopy;
    M3_ENTRIES(Uint32, animIds);
    M3_ENTRIES(Uint32, animRefs);
    uint32_t d3;
    Reference sd[13];
} m3SequenceTimeline_t;

typedef struct {
    M3_ENTRIES(Uint32, animIds);
    int32_t unk[4];
} m3SequenceValidator_t;

typedef struct {
    M3_ENTRIES(Char, name);
    M3_ENTRIES(Uint32, stcID);
} m3SequenceGetter_t;

struct m3Bounds
{
    /*0x00*/ vec3_t extents1[2];
    /*0x18*/ float radius1;
    /*0x1C*/ vec3_t extents2[2];
    /*0x34*/ float radius2;
};


typedef struct {
    uint32_t materialType;
    uint32_t materialIndex;
} m3MaterialReference_t;

typedef struct {
    uint32_t materialReferenceIndex;
    m3Float32AnimRef_t alphaFactor;
} m3CompositeMaterialSection_t;

typedef struct {
    M3_ENTRIES(Char, name);
    uint32_t unknown;
    M3_ENTRIES(CompositeMaterialSection, sections);
} m3CompositeMaterial_t;

typedef struct {
    uint32_t unknown0;
    uint16_t regionIndex;
    uint32_t unknown1;
    uint16_t materialReferenceIndex;
    uint16_t unknown2;
} m3Batch_t;

typedef struct {
    M3_ENTRIES(Face, faces);
    M3_ENTRIES(Region, regions);
    M3_ENTRIES(Batch, batches);
    Reference MSEC;
    uint32_t indexofs;
} m3Divisions_t;

/* Concatenate file-shaped division faces and retain each division's byte range in the model EBO. */
static uint32_t m3_pack_division_faces(m3Divisions_t *divisions, uint32_t count, uint16_t *indices) {
    uint32_t offset = 0;
    FOR_LOOP(i, count) {
        if (divisions[i].facesNum)
            memcpy(indices + offset, divisions[i].faces, divisions[i].facesNum * sizeof(*indices));
        divisions[i].indexofs = offset * sizeof(*indices); offset += divisions[i].facesNum;
    }
    return offset;
}

typedef struct {
    uint32_t unknown0;
    M3_ENTRIES(Char, imagePath);
    m3PixelAnimRef_t color;
    uint32_t flags;
    uint32_t uvSource1;
    uint32_t colorChannelSetting;
    m3Float32AnimRef_t brightMult;
    m3Float32AnimRef_t midtoneOffset;
    uint32_t unknown1;
    struct {
        float Amp;
        float Freq;
    } noise;
    uint32_t rttChannel;
    struct {
        uint32_t FrameRate;
        uint32_t StartFrame;
        uint32_t EndFrame;
        uint32_t Mode;
        uint32_t SyncTiming;
        m3Uint32AnimRef_t Play;
        m3Uint32AnimRef_t Restart;
    } video;
    struct {
        uint32_t Rows;
        uint32_t Columns;
        m3Uint16AnimRef_t Frame;
    } flipBook;
    struct {
        m3Vector2AnimRef_t Offset;
        m3Vector3AnimRef_t Angle;
        m3Vector2AnimRef_t Tiling;
        m3Uint32AnimRef_t unknown2;
        m3Float32AnimRef_t unknown3;
    } uv;
    m3Float32AnimRef_t brightness;
    m3Vector3AnimRef_t triPlanarOffset;
    m3Vector3AnimRef_t triPlanarScale;
    uint32_t unknown4;
    struct {
        uint32_t Type;
        float Exponent;
        float Min;
        float MaxOffset;
        float unknown5;
    } fresnel;
    struct {
        uint8_t unknown6[8];
        float InvertedMaskX;
        float InvertedMaskY;
        float InvertedMaskZ;
        float RotationYaw;
        float RotationPitch;
        uint32_t unknown7;
    } fresnel2;
    texture_t const *texture;
} m3Layer_t;

typedef struct {
    M3_ENTRIES(Char, name);
    uint32_t additionalFlags;
    uint32_t flags;
    uint32_t blendMode;
    int32_t priority;
    uint32_t usedRTTChannels;
    float specularity;
    float depthBlendFalloff;
    uint32_t cutoutThreshold;
    float specMult;
    float emisMult;
    M3_ENTRIES(Layer, diffuseLayer);
    M3_ENTRIES(Layer, decalLayer);
    M3_ENTRIES(Layer, specularLayer);
    M3_ENTRIES(Layer, glossLayer);
    M3_ENTRIES(Layer, emissiveLayer);
    M3_ENTRIES(Layer, emissive2Layer);
    M3_ENTRIES(Layer, evioLayer);
    M3_ENTRIES(Layer, evioMaskLayer);
    M3_ENTRIES(Layer, alphaMaskLayer);
    M3_ENTRIES(Layer, alphaMask2Layer);
    M3_ENTRIES(Layer, normalLayer);
    M3_ENTRIES(Layer, heightLayer);
    M3_ENTRIES(Layer, lightMapLayer);
    M3_ENTRIES(Layer, ambientOcclusionLayer);
    Reference unknown4[4];
    uint32_t unknown8;
    uint32_t layerBlendType;
    uint32_t emisBlendType;
    uint32_t emisMode;
    uint32_t specType;
    m3Float32AnimRef_t unknown9;
    m3Uint32AnimRef_t unknown10;
    uint8_t unknown11[12];
} m3Material_t;

typedef struct m3Model_s {
    struct m3Header* head;
    struct ReferenceEntry* refs;
    struct render_buffer *renbuf;
    handle_t buffer;
    uint32_t size;
    uint32_t type;
    
    
    M3_ENTRIES(Char, modelName);
    uint32_t flags;
    M3_ENTRIES(Sequence, sequences);
    M3_ENTRIES(SequenceTimeline, stc);
    M3_ENTRIES(SequenceGetter, stg);
    float unknown0[4];
    M3_ENTRIES(SequenceValidator, sts);
    M3_ENTRIES(Bone, bones);
    uint32_t numberOfBonesToCheckForSkin;
    uint32_t vertexFlags;
    M3_ENTRIES(Vertex, vertices);
    M3_ENTRIES(Divisions, divisions);
    M3_ENTRIES(Uint16, boneLookup);
    BoundingSphere boundings;
    uint32_t unknown4[16];
    Reference attachmentPoints;
    Reference attachmentPointAddons;
    Reference ligts;
    Reference shbxData;
    Reference cameras;
    Reference unknown21;
    M3_ENTRIES(MaterialReference, materialReferences);
    M3_ENTRIES(Material, materialStandard);
    Reference materialDisplacement;
    M3_ENTRIES(CompositeMaterial, materialComposite);
    Reference materialTerrain;
    Reference materialVolume;
    Reference materialUnknown1;
    Reference materialCreep;
    Reference materialVolumeNoise;
    Reference materialSplatTerrainBake;
    Reference materialUnknown2;
    Reference materialLensFlare;
    Reference particleEmitters;
    Reference particleEmitterCopies;
    Reference ribbonEmitters;
    Reference projections;
    Reference forces;
    Reference warps;
    Reference unknown22;
    Reference rigidBodies;
    Reference unknown23;
    Reference physicsJoints;
    Reference clothBehavior;
    Reference unknown24;
    Reference ikjtData;
    Reference unknown25;
    Reference unknown26;
    Reference partsOfTurrentBehaviors;
    Reference turrentBehaviors;
    M3_ENTRIES(Matrix4, absoluteInverseBoneRestPositions);
    BoundingShape tightHitTest;
    Reference fuzzyHitTestObjects;
    Reference attachmentVolumes;
    Reference attachmentVolumesAddon0;
    Reference attachmentVolumesAddon1;
    Reference billboardBehaviors;
    Reference tmdData;
    uint32_t unknown27;
    Reference unknown28;
} m3Model_t;

m3Model_t *R_LoadModelM3(void *buffer, uint32_t size);

#endif // M3HEADER_H_
