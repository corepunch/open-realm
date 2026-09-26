#ifndef __r_mdx_h__
#define __r_mdx_h__

#include "renderer/r_local.h"
#include "renderer/r_shader.h"
#include "renderer/r_trail.h"

#define MODEL_ATTACHMENT_PATH_LENGTH 0x100
#define MDX_TEXTURE_PATH_LENGTH 260
#define MDX_TEXTURE_RECORD_SIZE (sizeof(uint32_t) + MDX_TEXTURE_PATH_LENGTH + sizeof(uint32_t))
#define MDX_MAX_NODES 1024
#define MDX_MATRIX_PALETTE BZ_BONE_PALETTE_MAX

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
    BZ_MDX_PARTICLE_HEAD,
    BZ_MDX_PARTICLE_TAIL,
    BZ_MDX_PARTICLE_BOTH
};

enum { BZ_MDX_VERTEX_BUFFER, BZ_MDX_INDEX_BUFFER, BZ_MDX_BUFFER_COUNT };

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
    box3_t box;
} mdxBounds_t;

typedef struct mdxVertexSkin_s {
    uint8_t skin[4];
    uint8_t boneWeight[4];
} mdxVertexSkin_t;

typedef uint32_t replaceableID_t;

#define TEXREPL_NONE 0
#define TEXREPL_TEAMCOLOR 1
#define TEXREPL_TEAMGLOW 2

typedef struct mdxSequence_s {
    mdxObjectName_t name;
    uint32_t interval[2];
    float movespeed;     // movement speed of the entity while playing this animation
    uint32_t flags;      // &1: non looping
    float rarity;
    int syncpoint;
    mdxBounds_t bounds;
} mdxSequence_t;

typedef struct mdxInfo_s {
    mdxObjectName_t name;
    mdxFileName_t animationFile;
    mdxBounds_t bounds;
    uint32_t blendTime;
} mdxInfo_t;

typedef struct {
    int time;
    char data[];
} mdxKeyFrame_t;

typedef struct {
    uint32_t keyframeCount;
    MODELKEYTRACKDATATYPE datatype;
    MODELKEYTRACKTYPE linetype;
    uint32_t globalSeqId;        // GLBS index or 0xFFFFFFFF if none
    mdxKeyFrame_t values[];
} mdxKeyTrack_t;

typedef struct mdxGeosetAnim_s {
    float staticAlpha;        // 0 is transparent, 1 is opaque
    uint32_t flags;           // &2: color
    vec3_t staticColor;
    uint32_t geosetId;        // GEOS index or 0xFFFFFFFF if none
    mdxKeyTrack_t *alphas; // float
    mdxKeyTrack_t *colors; // vec3
    struct mdxGeosetAnim_s *next;
} mdxGeosetAnim_t;

typedef struct mdxNode_s {
    mdxObjectName_t name;
    uint32_t node_id; // globally unique id, used as the index in the hierarchy. index into PIVT
    uint32_t parent_id; // parent MDLGENOBJECT's objectId or 0xFFFFFFFF if none
    uint32_t flags;
    mdxKeyTrack_t *translation; // vec3
    mdxKeyTrack_t *rotation; // quat
    mdxKeyTrack_t *scale; // vec3
} mdxNode_t;

typedef struct mdxBone_s {
    mdxNode_t node;
    uint32_t geoset_id;
    uint32_t geoset_animation_id;
    struct mdxBone_s *next;
} mdxBone_t;

typedef struct mdxHelper_s {
    mdxNode_t node;
    struct mdxHelper_s *next;
} mdxHelper_t;

typedef struct mdxAttachment_s {
    mdxNode_t node;
    char path[MODEL_ATTACHMENT_PATH_LENGTH];
    uint32_t attachmentID;
    mdxKeyTrack_t *Visibility;
    struct mdxAttachment_s *next;
} mdxAttachment_t;

typedef struct mdxAttachmentPosition_s {
    cstring_t name;
    cstring_t path;
    vec3_t origin;
    mat4_t transform;
} mdxAttachmentPosition_t;

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
    vec3_t Color;
    float Intensity;
    vec3_t AmbColor;
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
    vec3_t vertex[2];
    float radius;
    struct mdxCollisionShape_s *next;
} mdxCollisionShape_t;

typedef struct mdxGlobalSequence_s {
    uint32_t value;
} mdxGlobalSequence_t;

typedef struct mdxEvent_s {
    mdxNode_t node;
    uint32_t num_keys;
    uint32_t globalSeqId;
    uint32_t *keys;
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
    uint32_t textureId;        // TEXS index or 0xFFFFFFFF for none
    uint32_t transformId;      // TXAN index or 0xFFFFFFFF for none
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
    vec3_t pivot;
    float fieldOfView;      // default is 0.9500215
    float farClip;          // default is 27.7777786
    float nearClip;         // default is 0.222222224
    vec3_t targetPivot;
    mdxKeyTrack_t *translation; // vec3
    mdxKeyTrack_t *roll; // float
    mdxKeyTrack_t *targetTranslation; // vec3
    struct mdxCamera_s *next;
} mdxCamera_t;

typedef struct {
    uint32_t start, end, repeat;
} mdxParticleAnimation_t;

enum {
    MDX_PRE2_FILTER_BLEND = 0,
    MDX_PRE2_FILTER_ADDITIVE,
    MDX_PRE2_FILTER_MODULATE,
    MDX_PRE2_FILTER_MODULATE_2X,
    MDX_PRE2_FILTER_ALPHAKEY,
    MDX_PRE2_FILTER_COUNT,
};

static inline BLEND_MODE MDLX_ParticleBlendMode(uint32_t filter_mode) {
    switch (filter_mode) {
        case MDX_PRE2_FILTER_BLEND:       return BLEND_MODE_BLEND;
        case MDX_PRE2_FILTER_ADDITIVE:    return BLEND_MODE_ADD;
        case MDX_PRE2_FILTER_MODULATE:    return BLEND_MODE_MODULATE;
        case MDX_PRE2_FILTER_MODULATE_2X: return BLEND_MODE_MODULATE_2X;
        case MDX_PRE2_FILTER_ALPHAKEY:    return BLEND_MODE_ALPHAKEY;
        default:                          return BLEND_MODE_BLEND; /* loader normalizes malformed values */
    }
}

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
    uint32_t FilterMode;
    uint32_t Rows;
    uint32_t Columns;
    uint32_t FrameFlags; /* PRE2 enum: 0 head, 1 tail, 2 both. */
    float TailLength;
    float Time;
    float SegmentColor[9];
    uint8_t Alpha[3];
    float ParticleScaling[3];
    mdxParticleAnimation_t LifeSpanUVAnim;
    mdxParticleAnimation_t DecayUVAnim;
    mdxParticleAnimation_t TailUVAnim;
    mdxParticleAnimation_t TailDecayUVAnim;
    uint32_t TextureID;
    uint32_t Squirt;
    uint32_t PriorityPlane;
    uint32_t ReplaceableId;

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

    /* Frame-persistent state — zeroed at load time, survives across frames */
    float accumulator;          /* emission rate accumulator for R_EmitParticles */
} mdxParticleEmitter_t;

#define BZ_MDX_RIBBON_INSTANCES 32 // concurrent instances of one ribboned model; missiles share one MDX

typedef struct mdxRibbonEmitter_s {
    mdxNode_t node;
    float heightAbove, heightBelow, alpha;
    vec3_t color;
    float lifespan;
    uint32_t textureSlot, emissionRate, rows, columns, materialId;
    float gravity;
    struct {
        mdxKeyTrack_t *Visibility;
        mdxKeyTrack_t *HeightAbove;
        mdxKeyTrack_t *HeightBelow;
        mdxKeyTrack_t *Alpha;
        mdxKeyTrack_t *Color;
        mdxKeyTrack_t *TextureSlot;
    } keytracks;
    struct mdxRibbonEmitter_s *next;
} mdxRibbonEmitter_t;

typedef struct mdxRibbonInstance_s {
    uint32_t number, stamp, team;
    trail_t *trails; /* engine trails, one per emitter; game owns the per-model store */
    uint32_t ntrails;
    struct mdxRibbonInstance_s *next;
} mdxRibbonInstance_t;

/* Entity-less trail copy: the entity stopped being drawn (freed, culled, out
 * of PVS) while edges were still live. Advances with no new emission and draws
 * through the same material path until empty, so missile trails fade over
 * their lifespan instead of popping with the edict. */
typedef struct mdxDetachedRibbon_s {
    struct mdxModel_s *model;
    uint32_t number; /* origin entity; dropped if that entity draws again */
    uint32_t emitter, materialId, columns, rows, slot, team;
    float lifespan, gravity;
    trail_t trail;
    struct mdxDetachedRibbon_s *next;
} mdxDetachedRibbon_t;

typedef struct mdxGeoset_s {
    vec3_t *vertices;
    vec3_t *normals;
    vec2_t *texcoord;
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
    uint32_t vertexArrayBuffer;
    uint32_t indexofs; // bytes into the model-owned index buffer; indices remain geoset-local
    struct mdxGeoset_s *next;
} mdxGeoset_t;

typedef struct mdxSprite_s {
    void const *id, *scope;
    uint32_t time;
    mdxParticleEmitter_t *emitters;
    particleScene_t particles;
    struct mdxSprite_s *next;
} mdxSprite_t;

typedef struct mdxModel_s {
    uint32_t buffers[BZ_MDX_BUFFER_COUNT];
    uint32_t version;
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
    mdxRibbonEmitter_t *ribbons;
    mdxRibbonInstance_t *ribbon_states;
    uint32_t ribbon_tick; /* last frame the orphan sweep ran for this model */
    mdxSprite_t *sprites;
    mdxAttachment_t *attachments;
    mdxLight_t *lights;
    mdxNode_t *nodes[MDX_MAX_NODES];
    mdxNode_t *node_list[MDX_MAX_NODES]; /* compact list of present nodes; avoids scanning 1024 slots per frame */
    int num_nodes;
    vec3_t *pivots;
    int num_textures;
    int num_sequences;
    int num_globalSequences;
    int num_pivots;
} mdxModel_t;

typedef struct {
    modelProg_t *shader;
} mdlx_state_t;

extern mdlx_state_t mdlx;
extern mat4_t node_matrices[MDX_MAX_NODES];

mdxSequence_t const *R_FindSequenceAtTime(mdxModel_t const *model, uint32_t time);
bool MDLX_EventKeyCrossed(mdxModel_t const *model, mdxEvent_t const *event, uint32_t key, uint32_t previous_frame, uint32_t current_frame, uint32_t previous_time, uint32_t current_time);
void MDLX_GetModelKeytrackValue(mdxModel_t const *model, mdxKeyTrack_t const *keytrack, uint32_t time, handle_t output);
void MDLX_GetAnimatedColorTrackValue(mdxModel_t const *model, mdxKeyTrack_t const *keytrack, uint32_t time, vec3_t *output);
void MDLX_GetGeosetAnimationStaticColor(mdxGeosetAnim_t const *geosetAnim, vec3_t *output);
void MDLX_BindBoneMatrices(mdxModel_t const *model, mat4_t const *model_matrix, uint32_t frame1, uint32_t frame0);
bool MDLX_EvaluateLight(mdxModel_t const *model, mdxLight_t const *light,
                        mat4_t const *model_matrix, uint32_t frame, bool use_visibility,
                        rModelLight_t *output);
bool MDLX_SampleFirstLight(model_t const *model, float ratio, rModelLight_t *output);
mdxSequence_t const *MDLX_FindSequenceByName(mdxModel_t const *model, cstring_t name);
uint32_t MDLX_CollectAttachmentPositions(mdxModel_t const *model, mat4_t const *model_matrix,
                                      uint32_t frame, uint32_t oldframe, cstring_t prefix,
                                      mdxAttachmentPosition_t *positions, uint32_t max_positions);

mdxModel_t *R_LoadModelMDLX(void *buffer, uint32_t size);
void MDLX_Release(mdxModel_t *model);
void MDX_BuildBuffers(mdxModel_t *model);
void MDX_PackModelGeometry(mdxModel_t *model, vertex_t *vertices, uint16_t *indices);
void MDLX_Init(void);
void MDLX_Shutdown(void);
void MDX_RenderModel(renderEntity_t const *entity, mdxModel_t const *model, mat4_t const *model_matrix);
bool MDLX_TraceModel(renderEntity_t const *ent, line3_t const *line, vec3_t *intersection);
bool MDLX_TraceWalkableSurface(renderEntity_t const *ent, line3_t const *line, vec3_t *intersection);
bool MDLX_ExtractCamera(mdxModel_t const *model, uint32_t frame, float aspect, mat4_t *output, mat4_t *light);
bool MDLX_SetEntityAnimationFrame(model_t const *model, cstring_t anim, renderEntity_t *entity);
void MDLX_DrawSpriteInstance(drawSprite_t const *sprite, color32_t tint);
void MDLX_ReleaseSprites(mdxModel_t *model);
void MDLX_DrawSprite(model_t const *model, cstring_t anim, float x, float y);
void MDLX_DrawSpriteTinted(model_t const *model, cstring_t anim, float x, float y, color32_t tint);

texture_t const *MDLX_GetTexture(mdxModel_t const *, uint32_t, uint32_t, uint32_t, texture_t const *);
void MDLX_RenderParticleEmitters(renderEntity_t const *, mdxModel_t const *, mat4_t const *);
void MDLX_RenderRibbonEmitters(renderEntity_t const *, mdxModel_t const *, mat4_t const *);
void MDLX_DrawRibbonVerts(mdxModel_t const *model, vertex_t *verts, uint32_t nverts,
                          mdxMaterial_t const *material, uint32_t team);
mdxMaterial_t *MDLX_MaterialAt(mdxModel_t const *model, uint32_t id);
uint32_t MDLX_EmitRibbonVertices(mdxModel_t *model, renderEntity_t const *entity, mat4_t const *model_matrix,
                               mdxRibbonEmitter_t *ribbon, vertex_t *out, uint32_t max);
void MDLX_TickDetachedRibbons(void); /* once per frame from R_RenderModel: fade entity-less trails */
void MDLX_ForgetRibbonModel(mdxModel_t *model); /* model release: drop registry entry and its orphans */
uint32_t MDLX_DetachedRibbonCount(void); /* headless lifecycle diagnostic; no material or GL state */
bool MDLX_SetLayerBlend(mdxMaterialLayer_t const *layer, uint32_t layerID);
void MDLX_ApplyLayerFlags(mdxMaterialLayer_t const *layer);

#endif
