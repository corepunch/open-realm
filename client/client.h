#ifndef client_h
#define client_h

#include "common/common.h"
#include "tr_public.h"
#include "keys.h"
#include "client/menu.h"
#include "ui_layout.h"

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#include "common/ui_constants.h"
#define MAX_CLIENT_ENTITIES MAX_GAME_ENTITIES
#define MAX_CONSOLE_MESSAGES 256
#define MAX_CONSOLE_MESSAGE_LEN 1024
#define VIEW_SHADOW_SIZE 1500
#define MAX_CONFIRMATION_OBJECTS 16
#define MAX_CONTROL_GROUPS 10 // groups; numbered 0-9; stored in cl.groups

typedef struct {
    entityState_t baseline;
    entityState_t current;
    entityState_t prev;
    uint32_t serverframe;
    color32_t tint;
    bool tint_valid;
    bool selected;
} centity_t;

typedef enum {
    UI_EVENT_NONE,
    UI_LEFT_MOUSE_DOWN,
    UI_LEFT_MOUSE_UP,
    UI_LEFT_MOUSE_DRAGGED,
    UI_RIGHT_MOUSE_DOWN,
    UI_RIGHT_MOUSE_UP,
    UI_RIGHT_MOUSE_DRAGGED,
    NUM_MENU_MOUSE_EVENTS
} mouseEventType_t;

typedef struct {
    mouseEventType_t event;
    uint32_t button;
    int wheel;
    vec2_t origin;
} mouseEvent_t;

typedef enum {
    key_game,
    key_console,
    key_message,
    key_menu,
} keydest_t;

typedef enum {
    ca_disconnected,
    ca_connecting,
    ca_connected,
    ca_active,
} connstate_t;

struct frame {
    int serverframe;
    int servertime;
    int oldclientframe;
};

struct client_state {
    bool refresh_prepped;
    sizeBuf_t loading;        /* compressed loading-screen chunks; released after decode or disconnect */
    float loading_progress;   /* client-owned normalized loading progress [0,1] */
    bool precache_ready;       /* complete media table received after the loading-only batch */
    model_t *models[MAX_MODELS];
    model_t *portraits[MAX_MODELS];
    model_t *minimap_model;
    texture_t const *pics[MAX_IMAGES];
    texture_t *dynamicPics[MAX_DYNAMIC_IMAGES];
    char dynamicPicNames[MAX_DYNAMIC_IMAGES][512];
    uint32_t dynamicPicCursor;
    font_t const *fonts[MAX_FONTSTYLES];
    PATHSTR configstrings[MAX_CONFIGSTRINGS];
    centity_t ents[MAX_CLIENT_ENTITIES];
    handle_t layout[MAX_LAYOUT_LAYERS];
    viewDef_t viewDef;
    wc3WeatherEffect_t weather_effects[MAX_WEATHER_EFFECTS];
    uint32_t num_weather_effects;
    lightningEffect_t lightning_effects[MAX_LIGHTNING_EFFECTS];
    uint32_t num_lightning_effects;
    struct frame frame;
    vec2_t startingPosition;
    player_t playerstate;
    struct {
        bool active;
        vec2_t origin;
        bool view;
        vec3_t angles;
        float distance;
        uint32_t focus_ms, view_ms;
    } camera_prediction;
    struct {
        uint32_t width;
        uint32_t height;
        uint8_t *visible;
        uint8_t *explored;
        uint8_t *texture;
        uint32_t generation;
    } fow;
    terrainMask_t terrain_mask;
    entityState_t *cursorEntity;
    struct {
        uint32_t image;
        float radius;
    } cursor_splat;
    uint32_t hover_entity;     /* entity number under mouse cursor (0 = none) */
    model_t *moveConfirmation;
    uint32_t num_entities;
    /* Compact list of entity numbers whose current state carries a live model.
     * CL_ParseFrame and CL_AddEntities iterate this instead of scanning all
     * MAX_CLIENT_ENTITIES slots every frame. */
    uint32_t active_entities[MAX_CLIENT_ENTITIES];
    uint32_t num_active;
    uint32_t time;
    struct {
        rect_t rect;
        bool in_progress;
        uint32_t entity_nums[MAX_SELECTED_ENTITIES];  /* Currently selected entity numbers */
        uint32_t num_selected;                         /* Number of currently selected entities */
    } selection;
    struct {
        uint32_t entity_nums[MAX_SELECTED_ENTITIES];
        uint32_t num_selected;
    } groups[MAX_CONTROL_GROUPS];
    uint32_t group_last;    /* last recalled group, MAX_CONTROL_GROUPS if none */
    uint32_t group_last_ms;
};

struct client_static {
    struct netchan netchan;
    keydest_t key_dest;
    connstate_t state;
    uint32_t disable_screen;       /* loading plaque timestamp; freeze screen while nonzero */
    int disable_servercount;    /* servercount when plaque was raised */
};

// cl_main.c
void CL_Connect(cstring_t host, unsigned short port);
void CL_Disconnect(cstring_t reason, bool notify);
void CL_SetMenuBindings(void);
void CL_SetGameplayInput(void);
void CL_SetGameplayBindings(void);
void CL_BeginLoadingMap(cstring_t mapName);
void CL_PrepLoading(void);
void CL_SetLoadingProgress(float progress);

/* Long-form client music presentation (client/cl_music.c). */
void CL_MusicInit(void);
void CL_MusicReset(void);
void CL_MusicShutdown(void);
void CL_MusicPlayMenu(cstring_t playlist);
void CL_MusicStopMenu(void);
void CL_MusicUpdate(void);
void CL_MusicSetMap(cstring_t playlist, bool random, int32_t index, uint32_t session_id);
void CL_MusicClearMap(void);
void CL_MusicPlay(cstring_t playlist, bool random, int32_t index, int32_t start_ms, int32_t fade_ms,
                  uint32_t played_mask, uint32_t session_id);
void CL_MusicStop(bool fade_out);
void CL_MusicResume(void);
void CL_MusicPlayThematic(cstring_t playlist, int32_t index, int32_t start_ms, uint32_t session_id);
void CL_MusicEndThematic(void);
void CL_MusicSetVolume(int32_t volume);
void CL_MusicSetPosition(int32_t millisecs);
void CL_MusicSetThematicVolume(int32_t volume);
void CL_MusicSetThematicPosition(int32_t millisecs);
void CL_MusicSuspend(void);
void CL_MusicResumeFromSuspend(void);

/* Optional full-screen movie playback (client/cl_movie.c). */
void CL_MovieInit(void);
void CL_Movie_f(void);
void CL_QueueMovie(cstring_t path);
bool CL_PlayMovie(cstring_t path);
bool CL_MovieActive(void);
void CL_MovieUpdate(void);
void CL_MovieDraw(void);
bool CL_MovieKeyEvent(keyCode_t key, bool down);
void CL_MovieShutdown(void);
vec2_t CL_ClampCameraPosition(vec2_t position);
void CL_PredictCameraPosition(vec2_t position);
static inline float cl_normalize_entity_scale(float scale) { return scale > 0.0f ? scale : 1.0f; }

void V_RenderView(void);
void V_Shutdown(void);
void CL_PrepRefresh(void);
void CL_RegisterConfigString(uint32_t index);
void CL_UpdateConfigString(uint32_t index, cstring_t olds);
void CL_RestartRefresh(void);
// cl_parse.c
void CL_ParseServerMessage(sizeBuf_t *msg);
void CL_AddActiveEntity(uint32_t index);
void CL_RemoveActiveEntity(uint32_t index);

// cl_canvas.c
void CL_CanvasInit(void);
void CL_CanvasResolvePolicy(void);
void CL_CanvasWindowChanged(void);
void CL_CanvasFrame(uint32_t now);
void CL_CanvasWriteChrome(void);
uiCanvas_t const *CL_Canvas(void);
UICANVASCLASS CL_CanvasSettledChrome(void);

// cl_window.c
void CL_WindowOpen(uiWindowDef_t const *def, handle_t layout);
bool CL_WindowMouseOver(int x, int y);
void CL_WindowClose(uint32_t id);
void CL_WindowClear(void);
void CL_WindowDraw(void);
bool CL_WindowMouseEvent(menuMouseEvent_t event, int x, int y, int32_t param);
bool CL_WindowKeyEvent(int key);
bool CL_WindowTextInput(cstring_t text);
bool CL_WindowTextInputActive(void);
cstring_t CL_WindowEditTextValue(uint32_t text_frame);
bool CL_WindowEditCursor(uint32_t text_frame, uint32_t *cursor);
void CL_SetTransientTextInput(bool enabled);
bool CL_WindowModalActive(void);

void CON_DrawConsole(void);
void CON_printf(cstring_t fmt, ...);
void CON_Init(void);
void CON_ToggleConsole(void);
void CON_TextInput(cstring_t text);
void CON_KeyEvent(int key, bool down);

// cl_view.c
//void Matrix4_fromViewAngles(vec3_t const *target, vec3_t const *angles, float distance, mat4_t *output);
//void Matrix4_getLightMatrix(vec3_t const *sunangles, vec3_t const *target, float scale, mat4_t *output);
void Matrix4_getCameraMatrix(mat4_t *output);
/* Paused views retain the last scene and render time. Zero delta is required
 * because model renderers emit effects while submitting cached entities. */
static inline bool V_AdvanceSceneTime(viewDef_t *view, uint32_t now, uint32_t *last, bool paused) {
    uint32_t elapsed;

    /* Map/session time restarts from zero. A persistent renderer clock must
     * treat that as a new epoch instead of unsigned-wrap advancing effects by
     * roughly 2^32 milliseconds in one frame. */
    if (*last && now < *last) {
        *last = now;
        view->time = now;
        view->deltaTime = 0;
        return !paused;
    }

    elapsed = *last ? now - *last : 0;
    *last = now;
    if (paused) { view->deltaTime = 0; return false; }
    view->time = view->time ? view->time + elapsed : now;
    view->deltaTime = elapsed;
    return true;
}
void V_AddEntity(renderEntity_t *ent);
bool V_FindEntity(uint32_t number, renderEntity_t *out);
void V_AddDecal(renderDecal_t *decal);

// cl_scrn.c
uiFrame_t const *SCR_Clear(handle_t data);
uiFrame_t const *SCR_ClearLayer(handle_t data, uint32_t layer);
uiFrame_t const *SCR_ClearWindow(handle_t data);
uint32_t SCR_NumFrames(void);
uiFrame_t *SCR_Frame(uint32_t number);
rect_t const *SCR_LayoutRect(uiFrame_t const *frame);
void CL_LayoutDrawMinimap(uiFrame_t const *frame, rect_t const *screen);
void CL_ClearMinimap(void);
void CL_ParseMinimapPing(sizeBuf_t *msg);
void CL_UpdateMinimapModel(void);
#ifdef BZ_TESTS
uint32_t CL_MinimapPingCount(void);
uint32_t CL_MinimapRecentCount(void);
#endif
/* World-hover targeting and UI_STAT_CONTEXT_* name/vital bindings share this
 * snapshot gate so a stale hover cannot keep a name after death or flag loss.
 * Invulnerable units may publish a name with neither bar flag. */
static inline bool CL_EntityAllowsWorldHover(entityState_t const *state) {
    return state && state->model &&
           state->stats[ENT_HEALTH] > 0 &&
           !(state->flags & EF_NOT_SELECTABLE) &&
           (state->name || (state->flags & (EF_HOVER_HEALTH | EF_HOVER_MANA)));
}

entityState_t const *SCR_LayoutContextEntity(void);
bool SCR_LayoutEntityContextActive(void);
bool SCR_LayoutContextValue(uint32_t stat, float *value);
bool SCR_LayoutContextFrameVisible(uiFrame_t const *frame);
bool SCR_LayoutWorldHoverRoot(rect_t *root);
float SCR_UICanvasWidth(void);
vec2_t SCR_ScreenToUI(int x, int y);
bool SCR_ProjectWorldPoint(vec3_t const *point, vec2_t *screen);
vec2_t SCR_GetAxisBounds(rect_t const *rect, bool is_x_axis);
float SCR_NormalizeAnchorOffset(uiFramePoint_t const *p, bool is_x_axis);
vec2_t SCR_SolveAxisPosition(uiFrame_t const *frame,
                              uiFramePoints_t const points,
                              float width,
                              bool is_x_axis,
                              bool assigned_size);
cstring_t SCR_GetStringValue(uiFrame_t const *frame);
cstring_t SCR_GetTooltipText(uiFrame_t const *frame);
drawText_t SCR_GetDrawText(uiFrame_t const *frame,
                         float avl_width,
                         cstring_t text,
                         uiLabel_t const *label);
void SCR_UpdateScreen(uint32_t msec);
void SCR_BeginLoadingPlaque(void);
void SCR_UpdateLoadingPlaque(void);
void SCR_EndLoadingPlaque(void);
void SCR_ClearLayoutResources(void);

// cl_screenshot.c
extern bool cl_screenshot_pending;
extern uint32_t cl_screenshot_delay;
void CL_Screenshot_f(void);
bool CL_ScreenshotReady(void);

// cl_input.c
void CL_Input(void);
void CL_InitInput(void);

// cl_tent.c
void CL_ParseTEnt(sizeBuf_t *msg);
void CL_AddTEnts(void);
void CL_ApplyIndicator(renderEntity_t *ent);
void CL_DrawTEnts(void);
void CL_ClearTEnts(void);

// cl_main.c - UI integration
int CL_ModelIndex(cstring_t modelName);
int CL_ImageIndex(cstring_t imageName);
int CL_FontIndex(cstring_t fontName, uint32_t fontSize);
void CL_UIMenuCommand(cstring_t command);

/* Entity one-shot sound/effect events (cl_fx.c) */
void CL_EntityEvent(entityState_t const *ent);

/* Unit UI data parsing (Phase 8) */
void CL_ParseUnitUI(sizeBuf_t *msg);

extern struct client_state cl;
extern struct client_static cls;
extern refExport_t re;
extern menuExport_t menu;
extern mouseEvent_t mouse;
extern bool scr_initialized;

/* Loading and active worlds own presentation independently of keyboard focus. */
static inline bool CL_MenuActive(void) { return cls.state != ca_active && cl.playerstate.client_ui_state != CLIENT_UI_LOADING; }

#endif
