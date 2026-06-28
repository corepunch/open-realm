#ifndef __r_mdx_h__
#define __r_mdx_h__

#include "renderer/r_local.h"

#define MODEL_ATTACHMENT_PATH_LENGTH 0x100
#define MDX_TEXTURE_PATH_LENGTH 260
#define MDX_TEXTURE_RECORD_SIZE (sizeof(DWORD) + MDX_TEXTURE_PATH_LENGTH + sizeof(DWORD))
#define MDX_MAX_NODES 1024
#define MDX_MATRIX_PALETTE 128

typedef char mdxObjectName_t[80];
typedef char mdxFileName_t[260];

//typedef enum {
//  TEXOP_LOAD = 0x0,
//  TEXOP_TRANSPARENT = 0x1,
//  TEXOP_BLEND = 0x2,
//  TEXOP_ADD = 0x3,
//  TEXOP_ADD_ALPHA = 0x4,
//  TEXOP_MODULATE = 0x5,
//  TEXOP_MODULATE2X = 0x6,
//  NUMTEXOPS = 0x7,
//} mdxTexOp_t;

typedef enum {
  MODEL_GEO_UNSHADED = 0x1,
  MODEL_GEO_SPHERE_ENV_MAP = 0x2,  // unused until v1500
  MODEL_GEO_WRAPWIDTH = 0x4,       // unused until v1500
  MODEL_GEO_WRAPHEIGHT = 0x8,      // unused until v1500
  MODEL_GEO_TWOSIDED = 0x10,
  MODEL_GEO_UNFOGGED = 0x20,
  MODEL_GEO_NO_DEPTH_TEST = 0x40,
  MODEL_GEO_NO_DEPTH_SET = 0x80,
  MODEL_GEO_NO_FALLBACK = 0x100,   // added in v1500. seen in ElwynnTallWaterfall01.mdx, FelwoodTallWaterfall01.mdx and LavaFallsBlackRock*.mdx
} mdxGeoFlags_t;

enum {
    MODEL_EMITTER_HEAD = 1,
    MODEL_EMITTER_TAIL = 2
};

#define MAX_MDLX_BUFFERS 8

#define MDLXNODE_Helper 0
#define MDLXNODE_DontInheritTranslation 1
#define MDLXNODE_DontInheritRotation 2
#define MDLXNODE_DontInheritScaling 4
#define MDLXNODE_Billboarded 8
#define MDLXNODE_BillboardedLockX 16
#define MDLXNODE_BillboardedLockY 32
#define MDLXNODE_BillboardedLockZ 64
#define MDLXNODE_CameraAnchored 128
#define MDLXNODE_Bone 256
#define MDLXNODE_Light 512
#define MDLXNODE_EventObject 1024
#define MDLXNODE_Attachment 2048
#define MDLXNODE_ParticleEmitter 4096
#define MDLXNODE_CollisionShape 8192
#define MDLXNODE_RibbonEmitter 16384
#define MDLXNODE_Unshaded_EmitterUsesMdl 32768
#define MDLXNODE_SortPrimitivesFarZ_EmitterUsesTga 65536
#define MDLXNODE_LineEmitter 131072
#define MDLXNODE_Unfogged 262144
#define MDLXNODE_ModelSpace 524288
#define MDLXNODE_XYQuad 1048576

typedef enum {
    SHAPETYPE_BOX,
    SHAPETYPE_PLANE,
    SHAPETYPE_SPHERE,
    SHAPETYPE_CYLINDER,
} MODELCOLLISIONSHAPETYPE;

typedef struct mdxBounds_s {
    float radius;
    BOX3 box;
} mdxBounds_t;

typedef struct mdxVertexSkin_s {
    BYTE skin[4];
    BYTE boneWeight[4];
} mdxVertexSkin_t;

typedef DWORD replaceableID_t;

#define TEXREPL_NONE 0
#define TEXREPL_TEAMCOLOR 1
#define TEXREPL_TEAMGLOW 2

typedef struct mdxSequence_s {
    mdxObjectName_t name;
    DWORD interval[2];
    float movespeed;     // movement speed of the entity while playing this animation
    DWORD flags;      // &1: non looping
    float rarity;
    int syncpoint;
    mdxBounds_t bounds;
} mdxSequence_t;

typedef struct mdxInfo_s {
    mdxObjectName_t name;
    mdxFileName_t animationFile;
    mdxBounds_t bounds;
    DWORD blendTime;
} mdxInfo_t;

typedef struct {
    int time;
    char data[];
} mdxKeyFrame_t;

typedef struct {
    DWORD keyframeCount;
    MODELKEYTRACKDATATYPE datatype;
    MODELKEYTRACKTYPE linetype;
    DWORD globalSeqId;        // GLBS index or 0xFFFFFFFF if none
    mdxKeyFrame_t values[];
} mdxKeyTrack_t;

typedef struct mdxGeosetAnim_s {
    float staticAlpha;        // 0 is transparent, 1 is opaque
    DWORD flags;           // &2: color
    VECTOR3 staticColor;
    DWORD geosetId;        // GEOS index or 0xFFFFFFFF if none
    mdxKeyTrack_t *alphas; // float
    mdxKeyTrack_t *colors; // vec3
    struct mdxGeosetAnim_s *next;
} mdxGeosetAnim_t;

typedef struct mdxNode_s {
    mdxObjectName_t name;
    DWORD node_id; // globally unique id, used as the index in the hierarchy. index into PIVT
    DWORD parent_id; // parent MDLGENOBJECT's objectId or 0xFFFFFFFF if none
    DWORD flags;
    mdxKeyTrack_t *translation; // vec3
    mdxKeyTrack_t *rotation; // quat
    mdxKeyTrack_t *scale; // vec3
} mdxNode_t;

typedef struct mdxBone_s {
    mdxNode_t node;
    DWORD geoset_id;
    DWORD geoset_animation_id;
    struct mdxBone_s *next;
} mdxBone_t;

typedef struct mdxHelper_s {
    mdxNode_t node;
    struct mdxHelper_s *next;
} mdxHelper_t;

typedef struct mdxAttachment_s {
    mdxNode_t node;
    char path[MODEL_ATTACHMENT_PATH_LENGTH];
    DWORD attachmentID;
    mdxKeyTrack_t *Visibility;
    struct mdxAttachment_s *next;
} mdxAttachment_t;

typedef enum {
    MODELLIGHTTYPE_OMNI = 0x0,
    MODELLIGHTTYPE_DIRECT = 0x1,
    MODELLIGHTTYPE_AMBIENT = 0x2,
} MODELLIGHTTYPE;

typedef struct mdxLight_s {
    mdxNode_t node;
    MODELLIGHTTYPE type;
    float AttenuationStart;
    float AttenuationEnd;
    VECTOR3 Color;
    float Intensity;
    VECTOR3 AmbColor;
    float AmbIntensity;
    struct {
        mdxKeyTrack_t *Visibility;
        mdxKeyTrack_t *Color;
        mdxKeyTrack_t *Intensity;
        mdxKeyTrack_t *AmbColor;
        mdxKeyTrack_t *AmbIntensity;
        mdxKeyTrack_t *AttenuationStart;
        mdxKeyTrack_t *AttenuationEnd;
    } keytracks;
    struct mdxLight_s *next;
} mdxLight_t;

typedef struct mdxCollisionShape_s {
    mdxNode_t node;
    MODELCOLLISIONSHAPETYPE type;
    VECTOR3 vertex[2];
    float radius;
    struct mdxCollisionShape_s *next;
} mdxCollisionShape_t;

typedef struct mdxGlobalSequence_s {
    DWORD value;
} mdxGlobalSequence_t;

typedef struct mdxEvent_s {
    mdxNode_t node;
    DWORD num_keys;
    DWORD globalSeqId;
    DWORD *keys;
    struct mdxEvent_s *next;
} mdxEvent_t;

typedef struct mdxTexture_s {
    replaceableID_t replaceableID;
    char path[MDX_TEXTURE_PATH_LENGTH];
    int nWrapping; //(1:WrapWidth; 2:WrapHeight; 3:Both)
    int texid;
} mdxTexture_t;

typedef struct mdxMaterialLayer_s {
    BLEND_MODE blendMode;
    mdxGeoFlags_t flags;
    DWORD textureId;        // TEXS index or 0xFFFFFFFF for none
    DWORD transformId;      // TXAN index or 0xFFFFFFFF for none
    int coordId;           // UAVS index or -1 for none, defines vertex buffer format coordId == -1 ? GxVBF_PN : GxVBF_PNT0
    float staticAlpha;
    mdxKeyTrack_t *alpha; // float
    mdxKeyTrack_t *flipbook; // int
} mdxMaterialLayer_t;

typedef struct mdxMaterial_s {
    int priority;
    int flags;
    int num_layers;
    mdxMaterialLayer_t *layers;
    mdxKeyTrack_t *emission; // float
    mdxKeyTrack_t *alpha; // float
    mdxKeyTrack_t *flipbook; // int
    struct mdxMaterial_s *next;
} mdxMaterial_t;

typedef struct mdxTextureAnim_s {
    mdxKeyTrack_t *translation; // vec3
    mdxKeyTrack_t *rotation; // quat
    mdxKeyTrack_t *scale; // vec3
    struct mdxTextureAnim_s *next;
} mdxTextureAnim_t;

typedef struct mdxCamera_s {
    mdxObjectName_t name;
    VECTOR3 pivot;
    float fieldOfView;      // default is 0.9500215
    float farClip;          // default is 27.7777786
    float nearClip;         // default is 0.222222224
    VECTOR3 targetPivot;
    mdxKeyTrack_t *translation; // vec3
    mdxKeyTrack_t *roll; // float
    mdxKeyTrack_t *targetTranslation; // vec3
    struct mdxCamera_s *next;
} mdxCamera_t;

typedef struct {
    DWORD start, end, repeat;
} mdxParticleAnimation_t;

typedef struct mdxParticleEmitter_s {
    mdxNode_t node;
    float Speed;
    float Variation;
    float Latitude;
    float Gravity;
    float LifeSpan;
    float EmissionRate;
    float Length;
    float Width;
    DWORD FilterMode;
    DWORD Rows;
    DWORD Columns;
    DWORD FrameFlags; // float???
    float TailLength;
    float Time;
    float SegmentColor[9];
    BYTE Alpha[3];
    float ParticleScaling[3];
    mdxParticleAnimation_t LifeSpanUVAnim;
    mdxParticleAnimation_t DecayUVAnim;
    mdxParticleAnimation_t TailUVAnim;
    mdxParticleAnimation_t TailDecayUVAnim;
    DWORD TextureID;
    DWORD Squirt;
    DWORD PriorityPlane;
    DWORD ReplaceableId;

    struct {
        mdxKeyTrack_t *Visibility;
        mdxKeyTrack_t *EmissionRate;
        mdxKeyTrack_t *Width;
        mdxKeyTrack_t *Length;
        mdxKeyTrack_t *Speed;
        mdxKeyTrack_t *Latitude;
        mdxKeyTrack_t *Gravity;
        mdxKeyTrack_t *Variation;
    } keytracks;
    
    struct mdxParticleEmitter_s *next;
} mdxParticleEmitter_t;

typedef struct mdxGeoset_s {
    VECTOR3 *vertices;
    VECTOR3 *normals;
    VECTOR2 *texcoord;
    mdxBounds_t *bounds;
    mdxBounds_t default_bounds;
    mdxGeosetAnim_t *geosetAnim;
//    mdxVertexSkin_t *skinning;
    int *matrices;
    int *matrixPalette;
    int *primitiveTypes;
    int *primitiveCounts;
    short *triangles;
    char *vertexGroups;
    int *matrixGroupSizes;
    int materialID;
    int group;
    int selectable;// (0:none;4:Unselectable)
    int num_vertices;
    int num_normals;
    int num_texcoord;
    int num_matrices;
    int num_matrixPalette;
    int num_primitiveTypes;
    int num_primitiveCounts;
    int num_triangles;
    int num_vertexGroups;
    int num_matrixGroupSizes;
    int num_bounds;
    int num_texcoordChannels;    
    DWORD vertexArrayBuffer;
    DWORD buffer[MAX_MDLX_BUFFERS];
    struct mdxGeoset_s *next;
} mdxGeoset_t;

typedef struct mdxModel_s {
    DWORD version;
    mdxInfo_t info;
    mdxBounds_t bounds;
    mdxGeoset_t *geosets;
    mdxTexture_t *textures;
    mdxSequence_t *sequences;
    mdxEvent_t *events;
    mdxMaterial_t *materials;
    mdxTextureAnim_t *textureAnims;
    mdxBone_t *bones;
    mdxGeosetAnim_t *geosetAnims;
    mdxCollisionShape_t *collisionShapes;
    mdxHelper_t *helpers;
    mdxCamera_t *cameras;
    mdxGlobalSequence_t *globalSequences;
    mdxParticleEmitter_t *emitters;
    mdxAttachment_t *attachments;
    mdxLight_t *lights;
    mdxNode_t *nodes[MDX_MAX_NODES];
    VECTOR3 *pivots;
    int num_textures;
    int num_sequences;
    int num_globalSequences;
    int num_pivots;
} mdxModel_t;

typedef struct {
    LPSHADER shader;
} mdlx_state_t;

extern mdlx_state_t mdlx;
extern MATRIX4 node_matrices[MDX_MAX_NODES];

mdxSequence_t const *R_FindSequenceAtTime(mdxModel_t const *model, DWORD time);
void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *keytrack, DWORD time, HANDLE output);
mdxSequence_t const *MDLX_FindSequenceByName(mdxModel_t const *model, LPCSTR name);

mdxModel_t *R_LoadModelMDLX(void *buffer, DWORD size);
void MDLX_Release(mdxModel_t *model);
void MDLX_Init(void);
void MDLX_Shutdown(void);
void MDX_RenderModel(renderEntity_t const *entity, mdxModel_t const *model, LPCMATRIX4 model_matrix);
bool MDLX_TraceModel(renderEntity_t const *ent, LPCLINE3 line);
void MDLX_DrawPortrait(LPCMODEL model, LPCRECT viewport, LPCSTR anim);
void MDLX_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y);

#endif
