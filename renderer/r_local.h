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
#elif __linux__
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
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
#define MAX_SKIN_BONES 8
#define NUM_SELECTION_CIRCLES 3
#define NUM_RECT_VERTICES 6
#define SYSFONT_COLS 16
#define SYSFONT_ROWS 16
#define SYSFONT_DRAW_WIDTH 8
#define SYSFONT_DRAW_HEIGHT 8

#include "../common/common.h"
#include "../client/tr_public.h"

extern refImport_t ri;


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

struct shader_program {
    DWORD progid;
    DWORD uViewProjectionMatrix;
    DWORD uModelMatrix;
    DWORD uLightMatrix;
    DWORD uNormalMatrix;
    DWORD uTextureMatrix;
    DWORD uTexture;
#if defined(USE_SHADOWMAPS) || defined(DEBUG_PATHFINDING)
    DWORD uShadowmap;
#endif
    DWORD uFogOfWar;
    DWORD uBones;
    DWORD uUseDiscard;
    DWORD uUnshaded;
    DWORD uLayerAlpha;
    DWORD uGeosetColor;
    DWORD uUvTrans;
    DWORD uUvRot;
    DWORD uUvScale;
    DWORD uMdxLightCount;
    DWORD uMdxLights;
    DWORD uMdxFallbackLighting;
    DWORD uMdxLightFill;
    DWORD uEyePosition;
    DWORD uActiveGlow;
    DWORD uFogEnable;
    DWORD uFogColor;
    DWORD uFogParams;
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
    LPTEXTURE minimap;
    RECT minimapRect;   /* last UI-space rect the minimap was drawn at */
    BOOL hasMinimap;
};

void R_RegisterMap(LPCSTR mapFileName);
int R_RegisterTextureFile(LPCSTR textureFileName);
LPTEXTURE R_LoadTexture(LPCSTR textureFileName);
void R_ReleaseTexture(LPTEXTURE texture);
void R_DrawWorld(void);
void R_DrawDecals(void);
void R_DrawAlphaSurfaces(void);
void R_RenderFrame(viewDef_t const *viewDef);
LPTEXTURE R_AllocateTexture(DWORD width, DWORD height);
LPTEXTURE R_MakeSysFontTexture(void);
LPTEXTURE R_MakeLoadingIndicatorTexture(void);
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
void R_DrawPortrait(LPCMODEL model, LPCRECT viewport, LPCSTR anim);
bool R_SetEntityAnimFrame(LPCMODEL model, LPCSTR anim, renderEntity_t *entity);
void R_DrawSprite(LPCMODEL model, LPCSTR anim, float x, float y);
void R_RenderSplat(LPCVECTOR2 position, float radius, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color);
void R_DrawHealthBars(void);
void R_RenderRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color);
void R_RenderFlatRectSplat(LPCVECTOR2 mins, LPCVECTOR2 maxs, FLOAT z, LPCTEXTURE texture, LPCSHADER shader, COLOR32 color);

// r_shader.c
LPSHADER R_InitShader(LPCSTR vs_default, LPCSTR fs_default);
void R_ReleaseShader(LPSHADER shader);

// r_main.c
#ifdef USE_SHADOWMAPS
void R_RenderShadowMap(void);
#endif
void R_RenderView(void);

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

size2_t R_GetWindowSize(void);

// r_buffer.c
VERTEX *R_AddQuad(VERTEX *buffer, LPCRECT screen, LPCRECT uv, COLOR32 color, float z);
VERTEX *R_AddStrip(VERTEX *buffer, LPCRECT screen, COLOR32 color);
VERTEX *R_AddWireBox(VERTEX *buffer, LPCBOX3 box, COLOR32 color);
LPBUFFER R_MakeVertexArrayObject(LPCVERTEX vertices, DWORD size);
LPBUFFER R_MakeIndexedVertexArrayObject(LPCVERTEX vertices, DWORD num_vertices, DWORD const *indices, DWORD num_indices);
void R_DrawBuffer(LPCBUFFER buffer, DWORD num_vertices);
void R_DrawIndexedBuffer(LPCBUFFER buffer, DWORD num_indices);

// r_draw.c
void R_DrawChar(int x, int y, int c);
void R_DrawFill(LPCRECT rect, COLOR32 color);
void R_DrawImage(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color);
void R_DrawImageEx(LPCDRAWIMAGE drawImage);
void R_DrawImageBatch(LPCTEXTURE texture, SHADERTYPE shaderType, BLEND_MODE alphamode, FLOAT uActiveGlow, BOOL hasClip, LPCRECT clip, LPCVERTEX vertices, DWORD num_vertices, BOOL repeat);
void R_DrawMinimap(LPCRECT screen);
bool R_TraceMinimap(float x, float y, LPVECTOR2 outWorld);
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

// r_image.c
LPRENDERTARGET R_AllocateRenderTexture(GLsizei width, GLsizei height, GLenum format, GLenum type, GLenum attachment);
void R_ReleaseRenderTexture(LPRENDERTARGET rt);

// r_fogofwar.c
void R_InitFogOfWar(DWORD width, DWORD height);
void R_ShutdownFogOfWar(void);
void R_RenderFogOfWar(void);
DWORD R_GetFogOfWarTexture(void);
void R_SetFogOfWarData(DWORD width, DWORD height, BYTE const *data);

// r_particles.c
void R_InitParticles(void);
void R_ShutdownParticles(void);
void R_DrawParticles(void);
cparticle_t *R_SpawnParticle(void);

extern struct render_globals tr;

#endif
