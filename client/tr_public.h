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
#include "../common/weather.h"

KNOWN_AS(modelInfo_s, modelInfo_t);

#define MODELINFO_MAX_TEXTURES 256
#define MAX_RENDER_DECALS 32
#define MAX_RENDER_SPLAT_RECTS 1024

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
    font_t const *font;
    cstring_t text;
    rect_t rect;
    color32_t color;
    float textWidth;
    float lineHeight;
    uint8_t flags;
    uiFontJustificationH_t halign;
    uiFontJustificationV_t valign;
    texture_t const * *icons;
    rect_t clip;
} drawText_t;

/* Image drawing parameters */
typedef struct drawImage_s {
    texture_t const *texture;
    SHADERTYPE shader;
    BLEND_MODE alphamode;
    rect_t screen;
    rect_t uv;
    color32_t color;
    float angle;
    float uActiveGlow;
    float uRadialShade; /* generic clockwise remaining-fraction shade, 0 disables it */
    uint8_t flags;
    rect_t clip;
} drawImage_t;

/* Backdrop drawing parameters (9-slice border + tiled background) */
typedef struct drawBackdrop_s {
    rect_t screen;
    struct { texture_t const *texture; color32_t color; } bg, edge;
    struct { int16_t flags; float size; } corner;
    struct { float right, top, bottom, left; } insets;
    uint8_t flags;
} drawBackdrop_t;

/* Standard pointer typedefs */




/* Decoded full-screen cinematic frame. The renderer owns the persistent upload texture. */
typedef struct drawCinematicFrame_s {
    uint32_t width;
    uint32_t height;
    void const *pixels;
    rect_t screen;
} drawCinematicFrame_t;

#include "common/stb_slk.h"

typedef struct {
    // Quake 3-style file API: renderer is archive-agnostic
    int (*FS_ReadFile)(cstring_t name, void **buf);  // Returns file size, allocates buf
    void (*FS_FreeFile)(void *buf);
    // mmap-backed read for loose files; free with FS_MunmapFile (falls back to heap for MPQ/Windows)
    void *(*FS_MmapFile)(cstring_t name, uint32_t *out_size);
    void (*FS_MunmapFile)(void *ptr);
    bool (*FileExtract)(cstring_t toExtract, cstring_t extracted);
    
    handle_t (*MemAlloc)(long size);
    void (*MemFree)(handle_t);
    uint32_t (*LoadSlk)(cstring_t filename, slkField_t const *schema, void **dest, uint32_t row_stride);
    cstring_t (*CvarString)(cstring_t name, cstring_t fallback);
    void (*PlaySoundAt)(cstring_t path, vector3_t const *origin, float volume);
    void (*error)(cstring_t fmt, ...);
} refImport_t;

typedef struct {
    vector3_t target;
    vector3_t angles;
} viewLight_t;

typedef struct {
    vector3_t origin;
    vector3_t eye;       /* derived rendered eye; camerastate[0] is refreshed from the final orbit view */
    vector3_t viewangles;
    float distance;
    float fov;      /* vertical field of view in degrees */
    float znear;
    float zfar;
} viewCamera_t;

typedef struct {
    vector3_t origin;
    model_t const *model;
    struct { model_t const *model; orientation_t angles; } attachment; /* local pose after the parent socket */
    texture_t const *skin;
    texture_t const *splat;
    cstring_t name;                      /* server-authored world label (NULL = none) */
    uint32_t number;
    uint32_t owner;                     /* authoritative entity owner/player slot when the game assigns one */
    uint32_t team;
#ifdef WOW
    uint32_t display_id;
    uint32_t appearance;
    uint32_t equipment;
    model_t const *overhead_model;
    vector3_t rotation;   /* Authored placement Euler degrees; game adapter decodes to yaw/pitch/roll. */
#endif
    uint32_t frame;
    uint32_t oldframe;
    uint32_t flags;
    uint8_t health;        /* compressed 0..255 snapshot health ratio */
    uint16_t effect_flags;
    model_t const *effect_model;
    float angle;        /* Canonical actor heading in radians, independent of height anchoring. */
    float scale;
    float radius;
    float splatsize;
    float ground_offset; /* current altitude above an authored ground/support surface */
#ifndef USE_SHADOWMAPS
    texture_t const *shadow;
    rect_t shadow_rect;
#endif
    color32_t tint;  /* optional per-instance model RGBA */
    bool tint_valid; /* distinguishes explicit alpha 0 from an unset tint */
    color32_t indicator; /* optional transient entity ground-ring RGBA; alpha 0 = none */
} renderEntity_t;

typedef struct {
    vector2_t origin;
    texture_t const *texture;
    color32_t color;
    float radius;
} renderDecal_t;

/* Terrain-conforming solid-colour rectangles. The renderer batches these with
 * its built-in white texture, so callers can submit many placement/pathing
 * cells without allocating textures or issuing one draw per cell. */
typedef struct {
    vector2_t mins;
    vector2_t maxs;
    color32_t color;
} renderSplatRect_t;

typedef struct {
    viewCamera_t camerastate[2];
    vector3_t target; /* Rendered camera focus shared by projection, drag-panning, and shadows. */
    rect_t viewport;
    rect_t scissor;
    uint32_t time;
    uint32_t deltaTime;
    float lerpfrac;
    uint32_t num_entities;
    renderEntity_t *entities;
    uint32_t num_decals;
    renderDecal_t *decals;
    uint32_t num_splat_rects;
    renderSplatRect_t *splat_rects;
    uint32_t num_weather_effects;
    wc3WeatherEffect_t const *weather_effects;
    uint32_t num_lightning_effects;
    lightningEffect_t const *lightning_effects;
    matrix4_t viewProjectionMatrix;
    matrix4_t lightMatrix;
    matrix4_t textureMatrix;
    model_t const *terrainLightModel; /* optional sampling source; game renderer evaluates into terrainLight */
    model_t const *entityLightModel;  /* optional sampling source; game renderer evaluates into entityLight */
    model_t const *skyModel;          /* optional camera-relative unlit world model */
    float environmentPhase;     /* normalized 0..1 clock used to sample environment light models */
    environLight_t terrainLight;  /* evaluated world/terrain light; valid=0 keeps the renderer fallback */
    environLight_t entityLight;   /* evaluated entity light; valid=0 reuses terrainLight or the fallback */
    uint32_t player;
    uint16_t game_variant;      /* opaque game-owned local presentation variant */
    uint32_t rdflags;
    uint32_t hover_entity;     /* entity under mouse cursor (0 = none) */
    uint32_t fow_width, fow_height, fow_generation;
    uint8_t const *fow_data;
    terrainMask_t terrain_mask;
    frustum3_t frustum;
    /* Generic linear scene-distance fog. Game renderers decide which world
     * surfaces consume it; producers that carry richer style/density state
     * reduce that state to this start/end/color contract until the renderer
     * exposes additional equations. */
    bool fogEnable;
    float fogStart;
    float fogEnd;
    vector3_t fogColor;
} viewDef_t;

struct modelInfo_s {
    uint32_t textureCount;
    cstring_t texturePaths[MODELINFO_MAX_TEXTURES];
    rect_t textureUVRect;
    bool hasTextureUVRect;
};

typedef struct {
    model_t const *model;
    cstring_t anim;
    float x, y;
    void const *id, *scope; /* Stable UI owner and layout identities; separate instances sharing one model. */
} drawSprite_t;

typedef struct {
    void (*Init)(uint32_t width, uint32_t height);
    void (*Shutdown)(void);
    void (*RegisterMap)(cstring_t mapFileName);
    void (*SetAssetScope)(cstring_t scope);
    void (*RenderFrame)(viewDef_t const *viewdef);
    texture_t * (*LoadTexture)(cstring_t fileName);
    /* NULL releases the renderer-owned cinematic texture. */
    void (*DrawCinematicFrame)(drawCinematicFrame_t const *frame);
    model_t * (*LoadModel)(cstring_t filename);
    font_t * (*LoadFont)(cstring_t filename, uint32_t size);
    size2_t (*GetWindowSize)(void);
    rect_t (*GetUISceneRect)(void);
    /* The client canvas owns the scene (docs/architecture/ui-canvas.md); the renderer only projects it. */
    void (*SetUIScene)(rect_t const *scene);
    uint32_t (*GetDrawCalls)(void);
    void (*SetWindowSize)(uint32_t width, uint32_t height);
    void (*WindowChanged)(void);
    size2_t (*GetTextureSize)(texture_t const *texture);
    void (*ReleaseTexture)(texture_t *texture);
    void (*ReleaseModel)(model_t *model);
    void (*BeginFrame)(void);
    void (*EndFrame)(void);
    void (*Screenshot)(void);
    void (*DrawChar)(int x, int y, int c);
    void (*DrawString)(int x, int y, cstring_t text);
    void (*DrawCharScaled)(float x, float y, int c, float scale);
    void (*DrawFill)(rect_t const *rect, color32_t color);
    void (*DrawSelectionRect)(rect_t const *rect, color32_t color);
    void (*DrawPic)(texture_t const *texture, float x, float y);
    void (*DrawImage)(texture_t const *texture, rect_t const *screen, rect_t const *uv, color32_t color);
    void (*DrawImageEx)(drawImage_t const *drawImage);
    void (*DrawBackdrop)(drawBackdrop_t const *drawBackdrop);
    void (*DrawMinimap)(rect_t const *screen, cstring_t map);
    void (*DrawLoadingIndicator)(rect_t const *rect, uint32_t time, color32_t color);
    void (*DrawSprite)(drawSprite_t const *sprite);
    bool (*DrawCursor)(float x, float y, color32_t tint);
    bool (*SetEntityAnimFrame)(model_t const *model, cstring_t anim, renderEntity_t *entity);
    void (*DrawText)(drawText_t const *drawText);
    vector2_t (*GetTextSize)(drawText_t const *drawText);
    bool (*GetModelInfo)(model_t *model, modelInfo_t *info);
    bool (*GetEntityOverheadPosition)(renderEntity_t const *entity, vector3_t *out);
    bool (*GetEntityAttachmentPosition)(renderEntity_t const *entity, cstring_t prefix, vector3_t *out);

    void (*DrawBoundingBox)(box3_t const *box, matrix4_t const *modelMatrix, matrix4_t const *vpMatrix, color32_t color);
    float (*GetHeightAtPoint)(float x, float y);
    float (*GetCameraHeightAtPoint)(float x, float y);
    bool (*CameraUsesTerrainHeight)(void);
    bool (*TraceEntity)(viewDef_t const *viewdef, float x, float y, uint32_t *number);
    bool (*TraceLocation)(viewDef_t const *viewdef, float x, float y, vector3_t *point);
    bool (*TraceCameraPlane)(viewDef_t const *viewdef, float x, float y, vector3_t *point);
    bool (*TraceMinimap)(float x, float y, vector2_t *outWorld);
    bool (*WorldToMinimap)(vector2_t const *world, vector2_t *outScreen);
    uint32_t (*EntitiesInRect)(viewDef_t const *viewdef, rect_t const *rect, uint32_t max, uint32_t *array);

} refExport_t;




refExport_t R_GetAPI(refImport_t imp);

#endif
