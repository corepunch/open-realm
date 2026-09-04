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
    SHADER_MINIMAP,
    SHADER_MINIMAP_FOG,
    SHADER_UNLIT,
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
#include "common/stb_slk.h"

typedef struct {
    // Quake 3-style file API: renderer is archive-agnostic
    int (*FS_ReadFile)(LPCSTR name, void **buf);  // Returns file size, allocates buf
    void (*FS_FreeFile)(void *buf);
    // mmap-backed read for loose files; free with FS_MunmapFile (falls back to heap for MPQ/Windows)
    void *(*FS_MmapFile)(LPCSTR name, DWORD *out_size);
    void (*FS_MunmapFile)(void *ptr);
    bool (*FileExtract)(LPCSTR toExtract, LPCSTR extracted);
    
    HANDLE (*MemAlloc)(long size);
    void (*MemFree)(HANDLE);
    DWORD (*LoadSlk)(LPCSTR filename, slkField_t const *schema, void **dest, DWORD row_stride);
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
    VECTOR3 viewangles;
    float distance;
    float fov;      /* vertical field of view in degrees */
    float znear;
    float zfar;
} viewCamera_t;

typedef struct {
    VECTOR3 origin;
    LPCMODEL model;
    LPCTEXTURE skin;
    LPCTEXTURE splat;
    LPCSTR name;                      /* server-authored world label (NULL = none) */
    DWORD number;
    DWORD team;
#ifdef WOW
    DWORD display_id;
    DWORD appearance;
    DWORD equipment;
    LPCMODEL attached_model;
    LPCMODEL overhead_model;
    VECTOR3 rotation;   /* 3D rotation for renderer-only static objects (WoW map objects, doodads) */
#endif
    DWORD frame;
    DWORD oldframe;
    DWORD flags;
    BYTE health;        /* compressed 0..255 snapshot health ratio */
    USHORT effect_flags;
    LPCMODEL effect_model;
    float angle;        /* 1D yaw for dynamic actors (units, players); grounded Warcraft III entities use this */
    float scale;
    float radius;
    float splatsize;
#ifndef USE_SHADOWMAPS
    LPCTEXTURE shadow;
    RECT shadow_rect;
#endif
    COLOR32 tint;  /* optional per-instance model RGBA; alpha 0 = unmodified/white */
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
    LPCMODEL terrainLightModel; /* optional sampling source; game renderer evaluates into terrainLight */
    LPCMODEL entityLightModel;  /* optional sampling source; game renderer evaluates into entityLight */
    LPCMODEL skyModel;          /* optional camera-relative unlit world model */
    FLOAT environmentPhase;     /* normalized 0..1 clock used to sample environment light models */
    ENVIRONLIGHT terrainLight;  /* evaluated world/terrain light; valid=0 keeps the renderer fallback */
    ENVIRONLIGHT entityLight;   /* evaluated entity light; valid=0 reuses terrainLight or the fallback */
    DWORD player;
    DWORD rdflags;
    DWORD hover_entity;     /* entity under mouse cursor (0 = none) */
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
    DWORD (*GetDrawCalls)(void);
    void (*SetWindowSize)(DWORD width, DWORD height);
    size2_t (*GetTextureSize)(LPCTEXTURE texture);
    void (*ReleaseTexture)(LPTEXTURE texture);
    void (*ReleaseModel)(LPMODEL model);
    void (*BeginFrame)(void);
    void (*EndFrame)(void);
    void (*Screenshot)(void);
    void (*DrawChar)(int x, int y, int c);
    void (*DrawString)(int x, int y, LPCSTR text);
    void (*DrawFill)(LPCRECT rect, COLOR32 color);
    void (*DrawSelectionRect)(LPCRECT rect, COLOR32 color);
    void (*DrawPic)(LPCTEXTURE texture, float x, float y);
    void (*DrawImage)(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color);
    void (*DrawImageEx)(LPCDRAWIMAGE drawImage);
    void (*DrawBackdrop)(LPCDRAWBACKDROP drawBackdrop);
    void (*DrawMinimap)(LPCRECT screen);
    void (*DrawLoadingIndicator)(LPCRECT rect, DWORD time, COLOR32 color);
    void (*DrawSprite)(LPCMODEL model, LPCSTR anim, float x, float y);
    bool (*DrawCursor)(float x, float y, COLOR32 tint);
    bool (*SetEntityAnimFrame)(LPCMODEL model, LPCSTR anim, renderEntity_t *entity);
    void (*DrawText)(LPCDRAWTEXT drawText);
    VECTOR2 (*GetTextSize)(LPCDRAWTEXT drawText);
    bool (*GetModelInfo)(LPMODEL model, LPMODELINFO info);
    bool (*GetEntityOverheadPosition)(renderEntity_t const *entity, LPVECTOR3 out);
    bool (*GetEntityAttachmentPosition)(renderEntity_t const *entity, LPCSTR prefix, LPVECTOR3 out);

    void (*DrawBoundingBox)(LPCBOX3 box, LPCMATRIX4 modelMatrix, LPCMATRIX4 vpMatrix, COLOR32 color);
    FLOAT (*GetHeightAtPoint)(float x, float y);
    bool (*TraceEntity)(viewDef_t const *viewdef, float x, float y, LPDWORD number);
    bool (*TraceLocation)(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point);
    bool (*TraceCameraPlane)(viewDef_t const *viewdef, float x, float y, LPVECTOR3 point);
    bool (*TraceMinimap)(float x, float y, LPVECTOR2 outWorld);
    bool (*WorldToMinimap)(LPCVECTOR2 world, LPVECTOR2 outScreen);
    DWORD (*EntitiesInRect)(viewDef_t const *viewdef, LPCRECT rect, DWORD max, LPDWORD array);

} refExport_t;

typedef refExport_t *LPRENDERER;
typedef refExport_t const *LPCRENDERER;

refExport_t R_GetAPI(refImport_t imp);

#endif
