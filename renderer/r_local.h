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
 * windows.h, whose DWORD/RECT names collide with the engine's public types. */
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
#define BZ_BONE_UNIFORM_RESERVE 64 // vec4 uniforms; lighting/view/grass budget; excluded before sizing uBones
#define NUM_SELECTION_CIRCLES 3
#define NUM_RECT_VERTICES 6
#define SYSFONT_COLS 16
#define SYSFONT_ROWS 16
#define SYSFONT_DRAW_WIDTH 8
#define SYSFONT_DRAW_HEIGHT 8

#include "../common/common.h"
#include "../client/tr_public.h"
#include "r_alpha.h"

extern refImport_t ri;

static inline BOOL R_CvarEnabled(LPCSTR name, LPCSTR fallback) { return !ri.CvarString || atoi(ri.CvarString(name, fallback)); }
static uint64_t R_PrimitiveTriangles(GLenum mode, DWORD count, DWORD instances) {
    return mode == GL_TRIANGLES ? (uint64_t)(count / 3) * instances : 0;
}


KNOWN_AS(shader_program, SHADER);
KNOWN_AS(render_buffer, BUFFER);
KNOWN_AS(render_target, RENDERTARGET);
KNOWN_AS(vertex, VERTEX);

typedef struct vertex {
    VECTOR3 position;
    VECTOR2 texcoord;
    VECTOR3 normal;
    COLOR32 color;
    BYTE skin[MAX_SKIN_BONES];
    BYTE boneWeight[MAX_SKIN_BONES];
} vertex_t;

struct texture {
    DWORD texid;
    DWORD width;
    DWORD height;
    LPTEXTURE next;
};

struct render_buffer {
    DWORD vao;
    DWORD vbo;
    DWORD ibo;
};

typedef struct INSTANCEBUFFER {
    DWORD vbo;
    DWORD count;
    DWORD capacity;
} INSTANCEBUFFER;
typedef struct INSTANCEBUFFER *LPINSTANCEBUFFER;
typedef const struct INSTANCEBUFFER *LPCINSTANCEBUFFER;

static size_t R_InstanceBufferBytes(DWORD count) { return (size_t)count * sizeof(MATRIX4); }
static DWORD R_InstanceBufferCapacity(DWORD capacity, DWORD count) {
    if (capacity >= count) return capacity;
    for (capacity = capacity ? capacity : 16; capacity < count; capacity *= 2) {}
    return capacity;
}
static DWORD R_BonePaletteSize(DWORD vectors) {
    return MAX(1, MIN(BZ_BONE_PALETTE_MAX, vectors > BZ_BONE_UNIFORM_RESERVE ?
                     (vectors - BZ_BONE_UNIFORM_RESERVE) / 4 : 1));
}
#define STATUS_BAR_GAP_RATIO 0.1875f // bar heights; preserves the old 0.0015/0.008 scene-height gap
/* Mana keeps the original lower slot while health moves one row above it. */
static inline VECTOR2 R_StatusBarOffsets(FLOAT height, BOOL mana) {
    return MAKE(VECTOR2, -(mana ? 2.0f + STATUS_BAR_GAP_RATIO : 1.0f) * height, 0.0f);
}
static void R_SwapRedBlue(BYTE *pixels, DWORD count, DWORD stride) {
    FOR_LOOP(i, count) {
        BYTE tmp = pixels[i * stride]; pixels[i * stride] = pixels[i * stride + 2]; pixels[i * stride + 2] = tmp;
    }
}

struct shader_program {
    DWORD progid;
    DWORD uViewProjectionMatrix;
    DWORD uModelMatrix;
    DWORD uLightMatrix;
    DWORD uNormalMatrix;
    DWORD uTextureMatrix;
    DWORD uTexture;
#ifdef USE_SHADOWMAPS
    DWORD uShadowmap;
#endif
    DWORD uFogOfWar;
    DWORD uBones;
    DWORD uAlphaKey;
    DWORD uAlphaCutoff;
    DWORD uUnshaded;
    DWORD uLayerAlpha;
    DWORD uGeosetColor;
    DWORD uUvMatrix;
    DWORD uLightCount;
    DWORD uLights;
    DWORD uGrassParams;
    DWORD uEyePosition;
    DWORD uActiveGlow;
    DWORD uFogEnable;
    DWORD uFogColor;
    DWORD uFogParams;
    DWORD uFirstBoneLookupIndex;
};

struct render_target {
    DWORD buffer;
    DWORD texture;
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

enum {
    SHEET_TERRAIN,
    SHEET_CLIFF,
    SHEET_COUNT,
};

struct render_globals {
    viewDef_t viewDef;
    LPCWAR3MAP world;
    LPTEXTURE texture[TEX_COUNT];
    LPSHADER shader[SHADER_COUNT];
    LPBUFFER buffer[RBUF_COUNT];
    LPMODEL model[MODEL_COUNT];
    LPRENDERTARGET rt[RT_COUNT];
    sheetRow_t *sheet[SHEET_COUNT];
    size2_t drawableSize;
    int msaa_samples;
    DWORD bone_count;
    LPTEXTURE minimap;
    RECT minimapRect;   /* last UI-space rect the minimap was drawn at */
    BOOL hasMinimap;
};

void R_RegisterMap(LPCSTR mapFileName);
int R_RegisterTextureFile(LPCSTR textureFileName);
LPTEXTURE R_LoadTexture(LPCSTR textureFileName);
LPTEXTURE R_FindLoadedTexture(LPCSTR name);
void R_CacheLoadedTexture(LPCSTR name, LPTEXTURE texture);
void R_ReleaseTexture(LPTEXTURE texture);
void R_ShutdownTextureCache(void);
void R_DrawWorld(void);
void R_DrawDecals(void);
void R_DrawAlphaSurfaces(void);
void R_RenderFrame(viewDef_t const *viewDef);
LPTEXTURE R_AllocateTexture(DWORD width, DWORD height);
LPTEXTURE R_MakeSysFontTexture(void);
LPTEXTURE R_MakeLoadingIndicatorTexture(void);
LPTEXTURE R_MakeSelectionCircleTexture(void);
BOOL R_IsTexturePCX(HANDLE data, DWORD filesize);
LPTEXTURE R_LoadTexturePCX(HANDLE data, DWORD filesize);
void R_LoadTextureMipLevel(LPCTEXTURE pTexture, DWORD level, LPCCOLOR32 pPixels, DWORD width, DWORD height);
void R_BindTexture(LPCTEXTURE texture, DWORD unit);
void R_SetTextureWrap(LPCTEXTURE texture, bool wrapS, bool wrapT);
void R_RenderModel(renderEntity_t const *edict);
void R_DrawTerrainShadows(void);
bool MDLX_TraceModel(renderEntity_t const *edict, LPCLINE3 line);
void R_ReleaseVertexArrayObject(LPBUFFER buffer);
LPCTEXTURE R_FindTextureByID(DWORD textureID);
void R_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y);
bool R_SetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity);
void R_RenderSplat(LPCVECTOR2 position, float radius, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color);
void R_DrawHealthBars(void);
void R_DrawBackdrop(LPCDRAWBACKDROP drawBackdrop);
void R_RenderRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color);
void R_RenderFlatRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, FLOAT z, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color);

// r_shader.c
LPSHADER R_InitShader(LPCSTR vs_default, LPCSTR fs_default);
void R_ReleaseShader(LPSHADER shader);
LPSHADER R_ModelShader(void);
LPSHADER R_ModelShaderInstanced(void);
void R_ShutdownModelShader(void);

// r_main.c
#ifdef USE_SHADOWMAPS
void R_RenderShadowMap(void);
#endif
void R_RenderView(void);
void R_SetupViewport(LPCRECT r);
void R_SetupScissor(LPCRECT r);
void R_RevertSettings(void);
void R_SetAlphaKeyState(BOOL enabled);
void R_StatsDraw(GLenum mode, DWORD count, DWORD instances);
DWORD R_GetFrameDrawCalls(void);

// r_ents.c
bool R_TraceEntity(viewDef_t const *viewdef, float x, float y, LPDWORD number);
bool R_TraceLocation(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point);
void R_GetEntityMatrix(renderEntity_t const *entity, LPMATRIX4 matrix);
LINE3 R_LineForScreenPoint(viewDef_t const *viewdef, float x, float y);
DWORD R_EntitiesInRect(viewDef_t const *viewdef, LPCRECT rect, DWORD max, LPDWORD array);
void R_DrawEntities(void);
FLOAT R_GetHeightAtPoint(FLOAT x, FLOAT y);

// r_model.c
LPMODEL R_LoadModel(LPCSTR modelFilename);
void R_ReleaseModel(LPMODEL model);
void R_RegisterMapAssets(LPCSTR mapFileName);
void R_ShutdownModels(void);

size2_t R_GetWindowSize(void);
void R_SetWindowSize(DWORD width, DWORD height);
size2_t R_GetTextureSize(LPCTEXTURE texture);

// r_buffer.c
VERTEX *R_AddQuad(VERTEX *buffer, LPCRECT screen, LPCRECT uv, COLOR32 color, float z);
VERTEX *R_AddStrip(VERTEX *buffer, LPCRECT screen, COLOR32 color);
VERTEX *R_AddWireBox(VERTEX *buffer, LPCBOX3 box, COLOR32 color);
LPBUFFER R_MakeVertexArrayObject(LPCVERTEX vertices, DWORD size);
LPBUFFER R_MakeIndexedVertexArrayObject(LPCVERTEX vertices, DWORD num_vertices, DWORD const *indices, DWORD num_indices);
void R_DrawBuffer(LPCBUFFER buffer, DWORD num_vertices);
void R_DrawBufferCopies(LPCBUFFER buffer, DWORD num_vertices, DWORD num_instances);
void R_DrawIndexedBuffer(LPCBUFFER buffer, DWORD num_indices);
BOOL R_MakeInstanceBuffer(LPINSTANCEBUFFER buffer, LPCMATRIX4 matrices, DWORD count);
BOOL R_UpdateInstanceBuffer(LPINSTANCEBUFFER buffer, LPCMATRIX4 matrices, DWORD count);
void R_ReleaseInstanceBuffer(LPINSTANCEBUFFER buffer);
void R_DrawBufferInstanced(LPCBUFFER buffer, DWORD num_vertices, LPCINSTANCEBUFFER instances);
void R_ShutdownDrawBufferInstanced(void);

// r_draw.c
void R_DrawChar(int x, int y, int c);
void R_DrawFill(LPCRECT rect, COLOR32 color);
void R_DrawImage(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color);
void R_DrawImageEx(LPCDRAWIMAGE drawImage);
void R_DrawImageBatch(LPCTEXTURE texture, SHADERTYPE shaderType, BLEND_MODE alphamode, FLOAT uActiveGlow, BOOL hasClip, LPCRECT clip, LPCVERTEX vertices, DWORD num_vertices, BOOL repeat);
void R_DrawMinimap(LPCRECT screen);
bool R_TraceMinimap(float x, float y, LPVECTOR2 outWorld);
void R_DrawMinimapCameraRect(LPCRECT screen);
void R_DrawLoadingIndicator(LPCRECT rect, DWORD time, COLOR32 color);
void R_DrawPic(LPCTEXTURE texture, float x, float y);
void R_DrawSelectionRect(LPCRECT rect, COLOR32 color);
void R_DrawBoundingBox(LPCBOX3 box, LPCMATRIX4 modelMatrix, LPCMATRIX4 vpMatrix, COLOR32 color);
void R_DrawWireRect(LPCRECT rect, COLOR32 color);
bool R_GetModelInfo(LPMODEL model, LPMODELINFO info);
RECT R_UISceneRect(void);

// r_font.c
LPFONT R_LoadFont(LPCSTR filename, DWORD size);
void R_ShutdownFonts(void);
VECTOR2 R_GetTextSize(LPCDRAWTEXT drawText);
void R_DrawText(LPCDRAWTEXT drawText);
void R_DrawString(int x, int y, LPCSTR text);
/* One thousandth of a pixel in normalized UI space is exact enough for glyph-fit decisions. */
static BOOL R_TextFitsWidth(FLOAT remaining) { return remaining >= -0.000001f; }

// r_image.c
LPRENDERTARGET R_AllocateRenderTexture(GLsizei width, GLsizei height, GLenum format, GLenum type, GLenum attachment);
void R_ReleaseRenderTexture(LPRENDERTARGET rt);

// r_fogofwar.c
void R_InitFogOfWar(DWORD width, DWORD height);
void R_ShutdownFogOfWar(void);
void R_RenderFogOfWar(void);
DWORD R_GetFogOfWarTexture(void);
DWORD R_GetMinimapFogOfWarTexture(void);
void R_SetFogOfWarData(DWORD width, DWORD height, BYTE const *data);

// r_particles.c
void R_InitParticles(void);
void R_ShutdownParticles(void);
void R_DrawParticles(void);
cparticle_t *R_SpawnParticle(void);
void R_DrawBillboardSprite(LPCTEXTURE texture, LPCVECTOR3 origin, float size, COLOR32 color);

extern struct render_globals tr;

#endif
