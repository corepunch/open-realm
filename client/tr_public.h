#ifndef tr_public_h
#define tr_public_h

/*
 * Public renderer API.
 *
 * This follows Quake 3's tr_public.h shape: callers see the renderer
 * import/export tables and the data structs those tables exchange. The
 * concrete R_* helpers stay in renderer/r_local.h unless they are module
 * entry points.
 */

#include "../common/common.h"

KNOWN_AS(modelInfo_s, MODELINFO);

#define MODELINFO_MAX_TEXTURES 256
#define MAX_RENDER_DECALS 32

/* Shader types for different rendering paths */
typedef enum {
    SHADER_DEFAULT,
    SHADER_UI,
    SHADER_SPLAT,
    SHADER_SHADOWSPLAT,
    SHADER_COMMANDBUTTON,
    SHADER_MINIMAP_FOG,
    SHADER_COUNT,
} SHADERTYPE;

/* Shared flags for draw structs */
enum {
    DRAW_CLIP      = 1 << 0,
    DRAW_WORD_WRAP = 1 << 1,
    DRAW_TILE      = 1 << 2,
    DRAW_MIRRORED  = 1 << 3,
    DRAW_EDGE_2X2  = 1 << 4, /* edge texture uses WoW 2×2 quadrant UV layout */
};

/* Text drawing parameters */
typedef struct drawText_s {
    LPCFONT font;
    LPCSTR text;
    RECT rect;
    COLOR32 color;
    FLOAT textWidth;
    FLOAT lineHeight;
    BYTE flags;
    uiFontJustificationH_t halign;
    uiFontJustificationV_t valign;
    LPCTEXTURE *icons;
    RECT clip;
} drawText_t;

/* Image drawing parameters */
typedef struct drawImage_s {
    LPCTEXTURE texture;
    SHADERTYPE shader;
    BLEND_MODE alphamode;
    RECT screen;
    RECT uv;
    COLOR32 color;
    FLOAT angle;
    FLOAT uActiveGlow;
    BYTE flags;
    RECT clip;
} drawImage_t;

/* Backdrop drawing parameters (9-slice border + tiled background) */
typedef struct drawBackdrop_s {
    RECT screen;
    struct { LPCTEXTURE texture; COLOR32 color; } bg, edge;
    struct { SHORT flags; FLOAT size; } corner;
    struct { FLOAT right, top, bottom, left; } insets;
    BYTE flags;
} drawBackdrop_t;

/* Standard pointer typedefs */
typedef drawText_t const *LPCDRAWTEXT;
typedef drawImage_t const *LPCDRAWIMAGE;
typedef drawBackdrop_t const *LPCDRAWBACKDROP;

typedef struct {
    // Quake 3-style file API: renderer is archive-agnostic
    int (*FS_ReadFile)(LPCSTR name, void **buf);  // Returns file size, allocates buf
    void (*FS_FreeFile)(void *buf);
    bool (*FileExtract)(LPCSTR toExtract, LPCSTR extracted);
    
    HANDLE (*MemAlloc)(long size);
    void (*MemFree)(HANDLE);
    sheetRow_t *(*ReadSheet)(LPCSTR sheetFilename);
    LPCSTR (*FindSheetCell)(sheetRow_t *sheet, LPCSTR row, LPCSTR column);
    LPCSTR (*CvarString)(LPCSTR name, LPCSTR fallback);
    void (*error)(LPCSTR fmt, ...);
} refImport_t;

typedef struct {
    VECTOR3 target;
    VECTOR3 angles;
} viewLight_t;

typedef struct {
    VECTOR3 origin;
    VECTOR3 eye;
    QUATERNION viewquat;
    VECTOR3 viewangles;
    float distance;
    float fov;      /* vertical field of view in degrees */
    float znear;
    float zfar;
} viewCamera_t;

typedef struct {
    VECTOR3 origin;
    LPCMODEL model;
    LPCMODEL attached_model;
    LPCTEXTURE skin;
    LPCTEXTURE splat;
    LPCTEXTURE shadow;
    DWORD number;
    DWORD team;
#ifdef WOW
    DWORD appearance;
    DWORD equipment;
#endif
    DWORD frame;
    DWORD oldframe;
    DWORD flags;
    float angle;        /* 1D yaw for dynamic actors (units, players); grounded Warcraft III entities use this */
    VECTOR3 rotation;   /* 3D rotation for renderer-only static objects (WoW map objects, doodads) */
    float scale;
    float radius;
    float splatsize;
    RECT shadow_rect;
    BYTE health;   /* current HP fraction, 0-255 (0 = dead/no bar) */
    BYTE mana;     /* current mana fraction, 0-255 (0 = no mana bar) */
} renderEntity_t;

typedef struct {
    VECTOR2 origin;
    LPCTEXTURE texture;
    COLOR32 color;
    float radius;
} renderDecal_t;

typedef struct {
    viewCamera_t camerastate[2];
    RECT viewport;
    RECT scissor;
    DWORD time;
    DWORD deltaTime;
    float lerpfrac;
    DWORD num_entities;
    renderEntity_t *entities;
    DWORD num_decals;
    renderDecal_t *decals;
    MATRIX4 viewProjectionMatrix;
    MATRIX4 lightMatrix;
    MATRIX4 textureMatrix;
    DWORD player;
    DWORD rdflags;
    FRUSTUM3 frustum;
    /* Linear scene fog (matches the original's fixed-function glFog* setup:
     * GL_FOG_MODE=LINEAR, GL_FOG_START/END, GL_FOG_COLOR).  Used by the glue
     * menu scene for its atmospheric haze. */
    BOOL fogEnable;
    float fogStart;
    float fogEnd;
    VECTOR3 fogColor;
} viewDef_t;

struct modelInfo_s {
    DWORD textureCount;
    LPCSTR texturePaths[MODELINFO_MAX_TEXTURES];
    RECT textureUVRect;
    BOOL hasTextureUVRect;
};

typedef struct {
    void (*Init)(DWORD width, DWORD height);
    void (*Shutdown)(void);
    void (*RegisterMap)(LPCSTR mapFileName);
    void (*RenderFrame)(viewDef_t const *viewdef);
    void (*SetFogOfWarData)(DWORD width, DWORD height, BYTE const *data);
    LPTEXTURE (*LoadTexture)(LPCSTR fileName);
    LPMODEL (*LoadModel)(LPCSTR filename);
    LPFONT (*LoadFont)(LPCSTR filename, DWORD size);
    size2_t (*GetWindowSize)(void);
    size2_t (*GetTextureSize)(LPCTEXTURE texture);
    void (*ReleaseTexture)(LPTEXTURE texture);
    void (*ReleaseModel)(LPMODEL model);
    void (*BeginFrame)(void);
    void (*EndFrame)(void);
    void (*DrawChar)(int x, int y, int c);
    void (*DrawFill)(LPCRECT rect, COLOR32 color);
    void (*DrawSelectionRect)(LPCRECT rect, COLOR32 color);
    void (*DrawPic)(LPCTEXTURE texture, float x, float y);
    void (*DrawImage)(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color);
    void (*DrawImageEx)(LPCDRAWIMAGE drawImage);
    void (*DrawBackdrop)(LPCDRAWBACKDROP drawBackdrop);
    void (*DrawMinimap)(LPCRECT screen);
    void (*DrawLoadingIndicator)(LPCRECT rect, DWORD time, COLOR32 color);
    void (*DrawSprite)(LPCMODEL model, LPCSTR anim, float x, float y);
    bool (*SetEntityAnimFrame)(LPCMODEL model, LPCSTR anim, renderEntity_t *entity);
    void (*DrawText)(LPCDRAWTEXT drawText);
    VECTOR2 (*GetTextSize)(LPCDRAWTEXT drawText);
    bool (*GetModelInfo)(LPMODEL model, LPMODELINFO info);

    void (*DrawBoundingBox)(LPCBOX3 box, LPCMATRIX4 modelMatrix, LPCMATRIX4 vpMatrix, COLOR32 color);
    FLOAT (*GetHeightAtPoint)(float x, float y);
    bool (*TraceEntity)(viewDef_t const *viewdef, float x, float y, LPDWORD number);
    bool (*TraceLocation)(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point);
    bool (*TraceMinimap)(float x, float y, LPVECTOR2 outWorld);
    DWORD (*EntitiesInRect)(viewDef_t const *viewdef, LPCRECT rect, DWORD max, LPDWORD array);

#ifdef DEBUG_PATHFINDING
    void (*SetPathTexture)(LPCCOLOR32 debugTexture);
#endif
} refExport_t;

typedef refExport_t *LPRENDERER;
typedef refExport_t const *LPCRENDERER;

refExport_t R_GetAPI(refImport_t imp);
refExport_t R_StdoutGetAPI(refImport_t imp);

#endif
