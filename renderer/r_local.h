#ifndef r_local_h
#define r_local_h

#include <SDL2/SDL.h>
#include "../common/mpq.h"

// TODO: M1 doesn't link without these includes

#if __APPLE__
#include <TargetConditionals.h>
#if !TARGET_OS_IPHONE && !TARGET_IPHONE_SIMULATOR
#include <OpenGL/gl3.h>
#else
#include <OpenGLES/ES3/gl.h>
#endif
#elif __linux__ && defined(BZ_GL_ES3)
#include <GLES3/gl3.h>
#elif __linux__ || defined(__OpenBSD__)
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#elif defined(_WIN32)
/* Windows' OpenGL 1.1 import library does not expose the modern functions
 * used by the renderer.  Epoxy provides runtime dispatch without pulling in
 * windows.h, whose uint32_t/rect_t names collide with the engine's public types. */
#include <epoxy/gl.h>
#endif

#ifdef DIAG_OUTPUT
#define GetError()\
{\
    for (GLenum Error = glGetError(); (GL_NO_ERROR != Error); Error = glGetError())\
    {\
        switch (Error)\
        {\
            case GL_INVALID_ENUM:      printf("\n%s\n\n", "GL_INVALID_ENUM"    ); assert(0); break;\
            case GL_INVALID_VALUE:     printf("\n%s\n\n", "GL_INVALID_VALUE"   ); assert(0); break;\
            case GL_INVALID_OPERATION: printf("\n%s\n\n", "GL_INVALID_OPERATION"); assert(0); break;\
            case GL_OUT_OF_MEMORY:     printf("\n%s\n\n", "GL_OUT_OF_MEMORY"   ); assert(0); break;\
            default:                                                                              break;\
        }\
    }\
}
#else
#define GetError() do { } while (0)
#endif

#define R_Call(func, ...) func(__VA_ARGS__); GetError();
#ifdef USE_SHADOWMAPS
#define SHADOW_TEXSIZE 1024
#define SHADOW_SCALE 1500
#endif
#define MAX_TEAMS 16
#define TEAM_MASK (MAX_TEAMS - 1)
#define PORTRAIT_SHADOW_SIZE 50
#define MAX_SKIN_BONES 4
#define BZ_BONE_PALETTE_MAX 128 // matrices; shared MDX/M2/M3 shader contract; bounds CPU-side palette storage
#define NUM_SELECTION_CIRCLES 3
#define NUM_RECT_VERTICES 6
#define SYSFONT_COLS 16
#define SYSFONT_ROWS 16
#define SYSFONT_DRAW_WIDTH 8
#define SYSFONT_DRAW_HEIGHT 8

typedef enum render_phase_e {
    RENDER_PHASE_SOLID = 0,
    RENDER_PHASE_LIGHTS,
    RENDER_PHASE_ALPHA,
} render_phase_t;

#include "../common/common.h"
#include "../client/tr_public.h"
#include "r_alpha.h"

extern refImport_t ri;

static inline bool R_CvarEnabled(cstring_t name, cstring_t fallback) { return !ri.CvarString || atoi(ri.CvarString(name, fallback)); }
static inline uint64_t R_PrimitiveTriangles(GLenum mode, uint32_t count, uint32_t instances) {
    return mode == GL_TRIANGLES ? (uint64_t)(count / 3) * instances : 0;
}


KNOWN_AS(render_buffer, buffer_t);
KNOWN_AS(render_target, rendertarget_t);
KNOWN_AS(vertex, vertex_t);

typedef struct vertex {
    vec3_t position;
    vec2_t texcoord;
    vec3_t normal;
    color32_t color;
    uint8_t skin[MAX_SKIN_BONES];
    uint8_t boneWeight[MAX_SKIN_BONES];
} vertex_t;

struct texture {
    uint32_t texid;
    uint32_t width;
    uint32_t height;
    texture_t *next;
};

struct render_buffer {
    uint32_t vao;
    uint32_t vbo;
    uint32_t ibo;
};

typedef struct drawElements_s { uint32_t count, offset; } drawElements_t;

typedef struct drawRange_s { uint32_t first, count; } drawRange_t;


typedef struct instanceBuffer_s {
    uint32_t vbo;
    uint32_t count;
    uint32_t capacity;
} instanceBuffer_t;



static inline size_t R_InstanceBufferBytes(uint32_t count) { return (size_t)count * sizeof(mat4_t); }
static inline uint32_t R_InstanceBufferCapacity(uint32_t capacity, uint32_t count) {
    if (capacity >= count) return capacity;
    for (capacity = capacity ? capacity : 16; capacity < count; capacity *= 2) {}
    return capacity;
}
static inline void R_SwapRedBlue(uint8_t *pixels, uint32_t count, uint32_t stride) {
    FOR_LOOP(i, count) {
        uint8_t tmp = pixels[i * stride]; pixels[i * stride] = pixels[i * stride + 2]; pixels[i * stride + 2] = tmp;
    }
}

#include "shader_desc.h"
#define BZ_MODEL_LIGHT_MAX 8 // lights; shared model shader array capacity; bounds one lighting-state upload

/* Typed shader values are separate from the program-owned GL locations. */

/* Simple sprite/UI shaders: position+texcoord+color vertex, unlit fragment. */
typedef struct spritestate_s {
    mat4_t viewProjection;
    mat4_t model;
    int texture;
    float activeGlow;
    float radialShade;
    bool fogEnable;
    vec3_t fogColor;
    vec2_t fogParams;
} spriteState_t;


typedef struct spriteprog_s {
    shaderProg_t prog;
    spriteState_t state;
} spriteProg_t;



/* Ground/world shader with per-vertex lighting + texture/fog-of-war matrices.
   progid/viewProjection/model share the SPRITEPROG prefix layout so the
   splat path can address either type through splat_shader_t. */
typedef struct defaultState_s {
    mat4_t viewProjection;
    mat4_t model;
    mat4_t textureMatrix;
    mat4_t lightMatrix;
    mat3_t normalMatrix;
    int lightCount;
    mat4_t lights[BZ_MODEL_LIGHT_MAX];
    int texture;
    int shadowmap;
    int fogOfWar;
    bool fogEnable;
    vec3_t fogColor;
    vec2_t fogParams;
} defaultState_t;


typedef struct defaultProg_s {
    shaderProg_t prog;
    defaultState_t state;
} defaultProg_t;



/* Minimal common view for the splat/decals path: the three uniforms it uploads. */
typedef struct {
    shaderProg_t prog;
    struct { mat4_t viewProjection, model; } state;
} splat_shader_t;

/* SPRITEPROG and DEFAULTPROG share a progid/viewProjection/model prefix,
   so the splat path can address either through splat_shader_t. */
#define R_SPLAT_SHADER(P) ((splat_shader_t *)(P))

/* Shared skinned-model shader (MDX/M2/M3): bone palette + 8 packed lights. */
typedef struct modelState_s {
    mat4_t bones[BZ_BONE_PALETTE_MAX];
    uint32_t boneCount;
    mat4_t viewProjection;
    mat4_t lightMatrix;
    mat4_t textureMatrix;
    int lightCount;
    float firstBoneLookupIndex;
    mat4_t lights[BZ_MODEL_LIGHT_MAX];
    mat4_t grassParams;
    mat4_t model;
    mat3_t normalMatrix;
    int texture;
    int shadowmap;
    int fogOfWar;
    float layerAlpha;
    vec4_t geosetColor;
    mat3_t uvMatrix;
    bool alphaKey;
    float alphaCutoff;
    bool unshaded;
    bool fogEnable;
    vec3_t fogColor;
    vec2_t fogParams;
} modelState_t;


typedef struct modelProg_s {
    shaderProg_t prog;
    modelState_t state;
} modelProg_t;



struct render_target {
    uint32_t buffer;
    uint32_t texture;
};

typedef enum {
    TRACK_NO_INTERP = 0x0,
    TRACK_LINEAR = 0x1,
    TRACK_HERMITE = 0x2,
    TRACK_BEZIER = 0x3,
    NUM_TRACK_TYPES = 0x4,
} MODELKEYTRACKTYPE;

typedef enum {
    TDATA_INT1,
    TDATA_FLOAT1,
    TDATA_FLOAT3,
    TDATA_FLOAT4,
} MODELKEYTRACKDATATYPE;

enum {
#ifdef USE_SHADOWMAPS
    TEX_SHADOWMAP,
#endif
    TEX_WATER,
    TEX_FONT,
    TEX_WHITE,
    TEX_BLACK,
    TEX_PLACEHOLDER,
    TEX_BLOB_SHADOW,
    TEX_LOADING_INDICATOR,
    TEX_TERRAIN_SHADOW,
    TEX_TEAM_GLOW,
    TEX_TEAM_COLOR = TEX_TEAM_GLOW + MAX_TEAMS,
    TEX_SELECTION_CIRCLE = TEX_TEAM_COLOR + MAX_TEAMS,
    TEX_COUNT = TEX_SELECTION_CIRCLE + NUM_SELECTION_CIRCLES,
};

enum {
#ifdef USE_SHADOWMAPS
    RT_DEPTHMAP,
#endif
    RT_COUNT,
};

enum {
    RBUF_TEMP1,
    RBUF_COUNT
};

enum {
    MODEL_SELECTION,
    MODEL_COUNT,
};

struct render_globals {
    viewDef_t viewDef;
    render_phase_t render_phase;    /* current whole-scene pass (solid / shadow-map / alpha); read by game renderers */
    war3map_t const *world;
    texture_t *texture[TEX_COUNT];
    spriteProg_t  shader_ui;
    spriteProg_t  shader_splat;
    spriteProg_t  shader_shadowSplat;
    spriteProg_t  shader_commandButton;
    spriteProg_t  shader_minimap;
    spriteProg_t  shader_minimapFog;
    spriteProg_t  shader_unlit;
    defaultProg_t shader_default;
    buffer_t *buffer[RBUF_COUNT];
    model_t *model[MODEL_COUNT];
    rendertarget_t *rt[RT_COUNT];
    size2_t drawableSize;
    rect_t uiScene;       /* client-resolved UI scene (re.SetUIScene); R_UISceneRect projects it onto the drawable */
    int msaa_samples;
    texture_t *minimap;
    rect_t minimapRect;   /* UI-space world-content rect used for minimap projection */
    bool hasMinimap;
    texture_t *cinematic;
    GLuint cinematic_pbo;
    uint32_t cinematic_pbo_size;
    bool cinematic_pbo_warned;
    bool cinematic_pbo_disabled;
};

void R_RegisterMap(cstring_t mapFileName);
int R_RegisterTextureFile(cstring_t textureFileName);
texture_t *R_LoadTexture(cstring_t textureFileName);
texture_t *R_LoadTextureStreamed(cstring_t textureFileName);
void R_AdvanceTextureGeneration(void);
void R_ReclaimStreamedTextures(uint32_t keep_recent);
int R_ReadTextureFile(cstring_t name, string_t path, void **buffer);
texture_t *R_FindLoadedTexture(cstring_t name);
void R_CacheLoadedTexture(cstring_t name, texture_t *texture);
void R_ReleaseTexture(texture_t *texture);
void R_ShutdownTextureCache(void);
void R_DrawWorld(void);
void R_DrawSky(void);
void R_DrawDecals(void);
void R_DrawAlphaSurfaces(void);
void R_RenderFrame(viewDef_t const *viewDef);
texture_t *R_AllocateTexture(uint32_t width, uint32_t height);
void R_DrawCinematicFrame(drawCinematicFrame_t const *frame);
texture_t *R_MakeSysFontTexture(void);
texture_t *R_MakeLoadingIndicatorTexture(void);
texture_t *R_MakeSelectionCircleTexture(void);
bool R_IsTexturePCX(handle_t data, uint32_t filesize);
texture_t *R_LoadTexturePCX(handle_t data, uint32_t filesize);
#define BZ_GL_BGRA 0x80e1 // GL enum; shared desktop/EXT/APPLE token absent from core GLES headers; BGRA byte uploads
typedef enum { PIXEL_RGBA, PIXEL_BGRA } PIXELFORMAT;
typedef struct {
    void const *pixels;
    uint32_t width, height, level;
    PIXELFORMAT format;
} texMip_t;


void R_InitTextureFormats(void);
void R_LoadTextureMipLevel(texture_t const *texture, texMip_t const *mip);
void R_BindTexture(texture_t const *texture, uint32_t unit);
void R_SetTextureWrap(texture_t const *texture, bool wrapS, bool wrapT);
void R_DrawEntity(renderEntity_t const *edict, bool shad);
void R_DrawSplatRects(void);
void R_DrawTerrainShadows(void);
bool MDLX_TraceModel(renderEntity_t const *edict, line3_t const *line, vec3_t *intersection);
void R_ReleaseVertexArrayObject(buffer_t *buffer);
texture_t const *R_FindTextureByID(uint32_t textureID);
void R_DrawSprite(drawSprite_t const *sprite);
bool R_SetEntityAnimFrame(model_t const *model, cstring_t anim, renderEntity_t *entity);
void R_RenderSplat(vec2_t const *position, float radius, texture_t const *texture, splat_shader_t *shader, color32_t color);
void R_DrawBackdrop(drawBackdrop_t const *drawBackdrop);
void R_RenderRectSplat(vec2_t const *mins, vec2_t const *maxs, texture_t const *texture, splat_shader_t *shader, color32_t color);
void R_RenderFlatRectSplat(vec2_t const *mins, vec2_t const *maxs, float z, texture_t const *texture, splat_shader_t *shader, color32_t color);
/* Batched splat rendering: accumulate many ground decals (unit shadows) into one
 * vertex-buffer upload + draw per contiguous texture run (plus capacity flushes),
 * instead of one upload + draw per splat. */
void R_BeginSplatBatch(splat_shader_t *shader);
void R_AddRectSplat(vec2_t const *mins, vec2_t const *maxs, texture_t const *texture, color32_t color);
void R_EndSplatBatch(void);

// r_shader.c
modelProg_t *R_ModelShader(void);
modelProg_t *R_ModelShaderInstanced(void);
void R_ShutdownModelShader(void);
void R_LoadBuiltinShaders(void);
void R_ShutdownBuiltinShaders(void);
spriteProg_t *R_SpriteShader(SHADERTYPE type);

// r_main.c
void R_BindDefaultFramebuffer(void);
#ifdef USE_SHADOWMAPS
void R_RenderShadowMap(void);
#endif
void R_RenderView(void);
void R_SetupGL(bool drawLight);
void R_SetupViewport(rect_t const *r);
void R_SetupScissor(rect_t const *r);
void R_RevertSettings(void);
void R_SetAlphaKeyState(bool enabled);
void R_StatsDraw(GLenum mode, uint32_t count, uint32_t instances);
uint32_t R_GetFrameDrawCalls(void);

// r_ents.c
bool R_TraceEntity(viewDef_t const *viewdef, float x, float y, uint32_t *number);
bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, vec3_t *point);
bool R_TraceCameraPlane(viewDef_t const *viewdef, float x, float y, vec3_t *point);
void R_GetEntityMatrix(renderEntity_t const *entity, mat4_t *matrix);
void R_GetAttachmentMatrix(renderEntity_t const *entity, mat4_t const *socket, mat4_t *matrix);
line3_t R_LineForScreenPoint(viewDef_t const *viewdef, float x, float y);
uint32_t R_EntitiesInRect(viewDef_t const *viewdef, rect_t const *rect, uint32_t max, uint32_t *array);
void R_DrawEntities(void);
float R_GetHeightAtPoint(float x, float y);

// r_model.c
/* Canonical game renderer hooks are declared here so unity-ordered game sources can call them directly. */
model_t *R_LoadModel(cstring_t modelFilename);
void R_ReleaseModel(model_t *model);
model_t *R_LoadRegisteredModel(cstring_t modelFilename);
void R_ReleaseRegisteredModel(model_t *model);
void R_RegisterMapAssets(cstring_t mapFileName);
bool R_MapAssetCandidate(cstring_t asset, string_t candidate, uint32_t candidate_size);
void R_SetMapAssetScope(cstring_t scope);
void R_ShutdownModels(void);

size2_t R_GetWindowSize(void);
void R_SetWindowSize(uint32_t width, uint32_t height);
size2_t R_GetTextureSize(texture_t const *texture);

// r_buffer.c
vertex_t *R_AddQuad(vertex_t *buffer, rect_t const *screen, rect_t const *uv, color32_t color, float z);
vertex_t *R_AddStrip(vertex_t *buffer, rect_t const *screen, color32_t color);
vertex_t *R_AddWireBox(vertex_t *buffer, box3_t const *box, color32_t color);
buffer_t *R_MakeVertexArrayObject(vertex_t const *vertices, uint32_t size);
buffer_t *R_MakeIndexedVertexArrayObject(vertex_t const *vertices, uint32_t num_vertices, uint32_t const *indices, uint32_t num_indices);
void R_DrawBuffer(buffer_t const *buffer, uint32_t num_vertices);
void R_DrawBufferRange(buffer_t const *buffer, drawRange_t const *draw);
void R_DrawIndexedBuffer16(buffer_t const *buffer, drawElements_t const *draw);
void R_DrawIndexedBuffer32(buffer_t const *buffer, drawElements_t const *draw);
void R_DrawBufferCopies(buffer_t const *buffer, uint32_t num_vertices, uint32_t num_instances);
void R_DrawIndexedBuffer(buffer_t const *buffer, uint32_t num_indices);
bool R_MakeInstanceBuffer(instanceBuffer_t *buffer, mat4_t const *matrices, uint32_t count);
bool R_UpdateInstanceBuffer(instanceBuffer_t *buffer, mat4_t const *matrices, uint32_t count);
void R_ReleaseInstanceBuffer(instanceBuffer_t *buffer);
void R_DrawBufferInstanced(buffer_t const *buffer, uint32_t num_vertices, instanceBuffer_t const *instances);
void R_DrawBufferRangeInstanced(buffer_t const *buffer, drawRange_t const *draw, instanceBuffer_t const *instances);
void R_DrawIndexedBuffer16Instanced(buffer_t const *buffer, drawElements_t const *draw, instanceBuffer_t const *instances);
void R_DrawIndexedBuffer32Instanced(buffer_t const *buffer, drawElements_t const *draw, instanceBuffer_t const *instances);
void R_ShutdownDrawBufferInstanced(void);

// r_draw.c
void R_DrawChar(int x, int y, int c);
void R_DrawCharScaled(float x, float y, int c, float scale);
void R_DrawFill(rect_t const *rect, color32_t color);
void R_DrawImage(texture_t const *texture, rect_t const *screen, rect_t const *uv, color32_t color);
void R_DrawImageEx(drawImage_t const *drawImage);
void R_DrawImageBatch(texture_t const *texture, SHADERTYPE shaderType, BLEND_MODE alphamode, float uActiveGlow, float uRadialShade, bool hasClip, rect_t const *clip, vertex_t const *vertices, uint32_t num_vertices, bool repeat);
void R_DrawMinimapScene(rect_t const *screen, cstring_t map);
bool R_TraceMinimap(float x, float y, vec2_t *outWorld);
bool R_WorldToMinimap(vec2_t const *world, vec2_t *outScreen);
void R_DrawMinimapCameraRect(rect_t const *screen);
void R_DrawMinimapBorder(rect_t const *screen, color32_t color);
void R_DrawLoadingIndicator(rect_t const *rect, uint32_t time, color32_t color);
void R_DrawPic(texture_t const *texture, float x, float y);
void R_DrawSelectionRect(rect_t const *rect, color32_t color);
void R_DrawBoundingBox(box3_t const *box, mat4_t const *modelMatrix, mat4_t const *vpMatrix, color32_t color);
void R_DrawWireRect(rect_t const *rect, color32_t color);
bool R_GetModelInfo(model_t *model, modelInfo_t *info);
bool R_GetEntityOverheadPosition(renderEntity_t const *entity, vec3_t *out);
bool R_GetEntityAttachmentPosition(renderEntity_t const *entity, cstring_t prefix, vec3_t *out);
rect_t R_UISceneRect(void);
void R_SetUIScene(rect_t const *scene);

// r_font.c
font_t *R_LoadFont(cstring_t filename, uint32_t size);
void R_ShutdownFonts(void);
vec2_t R_GetTextSize(drawText_t const *drawText);
void R_DrawText(drawText_t const *drawText);
void R_DrawString(int x, int y, cstring_t text);
/* One thousandth of a pixel in normalized UI space is exact enough for glyph-fit decisions. */
static inline bool R_TextFitsWidth(float remaining) { return remaining >= -0.000001f; }

// r_image.c
rendertarget_t *R_AllocateRenderTexture(GLsizei width, GLsizei height, GLenum format, GLenum type, GLenum attachment);
void R_ReleaseRenderTexture(rendertarget_t *rt);

// r_fogofwar.c
void R_InitFogOfWar(uint32_t width, uint32_t height);
void R_ShutdownFogOfWar(void);
void R_RenderFogOfWar(void);
void R_UpdateFogOfWarData(void);
uint32_t R_GetFogOfWarTexture(void);
uint32_t R_GetMinimapFogOfWarTexture(void);

// r_particles.c
typedef struct {
    cparticle_t *active;
    uint32_t generation;
} particleScene_t;

cparticle_t *R_BeginParticleScene(particleScene_t *scene);
void R_EndParticleScene(particleScene_t *scene, cparticle_t *previous);
void R_ClearParticleScene(particleScene_t *scene);
void R_InitParticles(void);
void R_ShutdownParticles(void);
void R_DrawParticles(void);
cparticle_t *R_SpawnParticle(void);
void R_DrawBillboardSprite(texture_t const *texture, vec3_t const *origin, float size, color32_t color);
typedef struct {
    texture_t const *texture;
    vec3_t const *points;
    uint32_t point_count;
    float width;
    float texcoord_scale;
    float texcoord_phase;
    color32_t color;
    BLEND_MODE blend_mode;
    bool depth_test;
} ribbonDraw_t;
void R_DrawRibbon(ribbonDraw_t const *draw);

extern struct render_globals tr;

#endif
