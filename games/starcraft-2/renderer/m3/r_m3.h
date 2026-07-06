#ifndef __r_m3_h__
#define __r_m3_h__

#include "common/shared.h"

#define M3_ENTRIES(TYPE, NAME)\
DWORD NAME##Num; \
m3##TYPE##_t *NAME;

#define M3_FOR_EACH(TYPE, VAR, LIST) \
for (m3##TYPE##_t const *VAR = LIST; VAR && VAR < LIST + LIST##Num; VAR++)

typedef USHORT m3Face_t;
typedef char m3Char_t;
typedef float m3Float32_t;
typedef MATRIX4 m3Matrix4_t;
typedef LONG m3Int32_t;
typedef DWORD m3Uint32_t;
typedef SHORT m3Int16_t;
typedef USHORT m3Uint16_t;
typedef VECTOR2 m3Vector2_t;
typedef VECTOR3 m3Vector3_t;
typedef VECTOR4 m3Vector4_t;
typedef COLOR32 m3Pixel_t;

typedef struct {
    USHORT interpolationType;
    USHORT animFlags;
    DWORD animId;
} m3AnimRef_t;

#define M3_DECL_ANIMREF(name, type) \
typedef struct { \
    USHORT interpolationType; \
    USHORT animFlags; \
    DWORD animId; \
    type initValue; \
    type nullValue; \
    DWORD unknown; \
} m3##name##AnimRef_t;

M3_DECL_ANIMREF(Pixel, COLOR32);
M3_DECL_ANIMREF(Uint16, USHORT);
M3_DECL_ANIMREF(Uint32, DWORD);
M3_DECL_ANIMREF(Float32, float);
M3_DECL_ANIMREF(Vector2, VECTOR2);
M3_DECL_ANIMREF(Vector3, VECTOR3);
M3_DECL_ANIMREF(Vector4, VECTOR4);

typedef struct {
    VECTOR3 min;
    VECTOR3 max;
    float radius;
} BoundingSphere;

typedef struct {
    DWORD shape;
    USHORT bone;
    USHORT unknown0;
    MATRIX4 matrix;
    DWORD unknown[6];
    VECTOR3 size;
} BoundingShape;

typedef struct {
    DWORD nEntries;
    DWORD ref;
    DWORD flags;
} Reference;

struct ReferenceEntry {
    char id[4];
    DWORD offset;
    DWORD nEntries;
    DWORD version;
};

struct MD33
{
    char id[4];
    DWORD ofsRefs;
    DWORD nRefs;
    Reference MODL;
};

typedef struct {
    Reference keys;
    DWORD flags;
    DWORD biggestKey;
    Reference values;
} m3SequenceData_t;

typedef struct {
    LONG unknown0; // Keybone?
    M3_ENTRIES(Char, name);
    DWORD flags;
    SHORT parent;
    SHORT unknown1;
    m3Vector3AnimRef_t position;
    m3Vector4AnimRef_t rotation;
    m3Vector3AnimRef_t scale;
    m3Uint32AnimRef_t visibility;
} m3Bone_t;

typedef struct m3Vertex_s {
    VECTOR3 pos;
    BYTE boneWeight[4];
    BYTE boneIndex[4];
    BYTE normal[4];
    COLOR32 color;
    SHORT uv[4][2];
    BYTE tangent[4];
} m3Vertex_t;

struct MATM
{
    DWORD d1;
    DWORD d2; // Index into MAT-table?
};

struct MAT
{
    Reference name;
    int ukn1[8];
    float x, y;  //always 1.0f
    Reference layers[13];
    int ukn2[15];
};

struct LAYR
{
    int unk;
    Reference name;
    float unk2[85];
};

struct DIV {
    Reference faces; // U16
    Reference regions; // REGN - Region
    Reference BAT;
    Reference MSEC;
};

typedef struct {
    DWORD unknown0;
    DWORD unknown1;
    DWORD firstVertexIndex;
    DWORD verticesCount;
    DWORD firstTriangleIndex;
    DWORD triangleIndicesCount;
    USHORT bonesCount;
    USHORT firstBoneLookupIndex;
    USHORT boneLookupIndicesCount;
    USHORT unknown2;
    BYTE boneWeightPairsCount;
    BYTE unknown3;
    USHORT rootBoneIndex;
    DWORD unknown4;
    DWORD unknown5[2];
} m3Region_t;

struct CAM
{
    /*0x00*/ LONG d1;
    /*0x04*/ Reference name;
    /*0x0C*/ USHORT flags1;
    /*0x0E*/ USHORT flags2;
};

struct EVNT
{
    /*0x00*/ Reference name;
    /*0x08*/ SHORT unk1[4];
    /*0x10*/ float matrix[4][4];
    /*0x50*/ LONG unk2[4];
};

struct ATT
{
    /*0x00*/ LONG unk;
    /*0x04*/ Reference name;
    /*0x0C*/ LONG bone;
};

struct PHSH
{
    float m[4][4];
    float f1;
    float f2;
    Reference refs[5];
    float f3;
};

typedef struct {
    DWORD unknown[2];
    M3_ENTRIES(Char, name);
    DWORD interval[2];
    float movementSpeed;
    DWORD flags;
    DWORD frequency;
    LONG unk[3];
    LONG unk2;
    BoundingSphere boundingSphere;
    LONG d5[3];
} m3Sequence_t;

typedef struct {
    M3_ENTRIES(Char, name);
    USHORT runsConcurrent;
    USHORT priority;
    USHORT stsIndex;
    USHORT stsIndexCopy;
    M3_ENTRIES(Uint32, animIds);
    M3_ENTRIES(Uint32, animRefs);
    DWORD d3;
    Reference sd[13];
} m3SequenceTimeline_t;

typedef struct {
    M3_ENTRIES(Uint32, animIds);
    LONG unk[4];
} m3SequenceValidator_t;

typedef struct {
    M3_ENTRIES(Char, name);
    M3_ENTRIES(Uint32, stcID);
} m3SequenceGetter_t;

struct BNDS
{
    /*0x00*/ VECTOR3 extents1[2];
    /*0x18*/ float radius1;
    /*0x1C*/ VECTOR3 extents2[2];
    /*0x34*/ float radius2;
};


typedef struct {
    DWORD materialType;
    DWORD materialIndex;
} m3MaterialReference_t;

typedef struct {
    DWORD materialReferenceIndex;
    m3Float32AnimRef_t alphaFactor;
} m3CompositeMaterialSection_t;

typedef struct {
    M3_ENTRIES(Char, name);
    DWORD unknown;
    M3_ENTRIES(CompositeMaterialSection, sections);
} m3CompositeMaterial_t;

typedef struct {
    DWORD unknown0;
    USHORT regionIndex;
    DWORD unknown1;
    USHORT materialReferenceIndex;
    USHORT unknown2;
} m3Batch_t;

typedef struct {
    M3_ENTRIES(Face, faces);
    M3_ENTRIES(Region, regions);
    M3_ENTRIES(Batch, batches);
    Reference MSEC;
    DWORD indicesBuffer;
} m3Divisions_t;

typedef struct {
    DWORD unknown0;
    M3_ENTRIES(Char, imagePath);
    m3PixelAnimRef_t color;
    DWORD flags;
    DWORD uvSource1;
    DWORD colorChannelSetting;
    m3Float32AnimRef_t brightMult;
    m3Float32AnimRef_t midtoneOffset;
    DWORD unknown1;
    struct {
        float Amp;
        float Freq;
    } noise;
    DWORD rttChannel;
    struct {
        DWORD FrameRate;
        DWORD StartFrame;
        DWORD EndFrame;
        DWORD Mode;
        DWORD SyncTiming;
        m3Uint32AnimRef_t Play;
        m3Uint32AnimRef_t Restart;
    } video;
    struct {
        DWORD Rows;
        DWORD Columns;
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
    DWORD unknown4;
    struct {
        DWORD Type;
        float Exponent;
        float Min;
        float MaxOffset;
        float unknown5;
    } fresnel;
    struct {
        BYTE unknown6[8];
        float InvertedMaskX;
        float InvertedMaskY;
        float InvertedMaskZ;
        float RotationYaw;
        float RotationPitch;
        DWORD unknown7;
    } fresnel2;
    LPCTEXTURE texture;
} m3Layer_t;

typedef struct {
    M3_ENTRIES(Char, name);
    DWORD additionalFlags;
    DWORD flags;
    DWORD blendMode;
    LONG priority;
    DWORD usedRTTChannels;
    float specularity;
    float depthBlendFalloff;
    DWORD cutoutThreshold;
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
    DWORD unknown8;
    DWORD layerBlendType;
    DWORD emisBlendType;
    DWORD emisMode;
    DWORD specType;
    m3Float32AnimRef_t unknown9;
    m3Uint32AnimRef_t unknown10;
    BYTE unknown11[12];
} m3Material_t;

typedef struct m3Model_s {
    struct MD33* head;
    struct ReferenceEntry* refs;
    struct render_buffer *renbuf;
    HANDLE buffer;
    DWORD size;
    DWORD type;
    
    
    M3_ENTRIES(Char, modelName);
    DWORD flags;
    M3_ENTRIES(Sequence, sequences);
    M3_ENTRIES(SequenceTimeline, stc);
    M3_ENTRIES(SequenceGetter, stg);
    float unknown0[4];
    M3_ENTRIES(SequenceValidator, sts);
    M3_ENTRIES(Bone, bones);
    DWORD numberOfBonesToCheckForSkin;
    DWORD vertexFlags;
    M3_ENTRIES(Vertex, vertices);
    M3_ENTRIES(Divisions, divisions);
    M3_ENTRIES(Uint16, boneLookup);
    BoundingSphere boundings;
    DWORD unknown4[16];
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
    DWORD unknown27;
    Reference unknown28;
} m3Model_t;

m3Model_t *R_LoadModelM3(void *buffer, DWORD size);

#endif // M3HEADER_H_
