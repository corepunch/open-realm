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
#define MAX_LAYOUT_LAYERS 16
#define MAX_CONTROL_GROUPS 10 // groups; numbered 0-9; stored in cl.groups

typedef struct {
    entityState_t baseline;
    entityState_t current;
    entityState_t prev;
    DWORD serverframe;
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
    DWORD button;
    int wheel;
    VECTOR2 origin;
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
    BOOL refresh_prepped;
    LPMODEL models[MAX_MODELS];
    LPMODEL portraits[MAX_MODELS];
    LPMODEL minimap_model;
    LPCTEXTURE pics[MAX_IMAGES];
    LPTEXTURE dynamicPics[MAX_DYNAMIC_IMAGES];
    char dynamicPicNames[MAX_DYNAMIC_IMAGES][512];
    DWORD dynamicPicCursor;
    LPCFONT fonts[MAX_FONTSTYLES];
    PATHSTR configstrings[MAX_CONFIGSTRINGS];
    centity_t ents[MAX_CLIENT_ENTITIES];
    HANDLE layout[MAX_LAYOUT_LAYERS];
    viewDef_t viewDef;
    struct frame frame;
    VECTOR2 startingPosition;
    PLAYER playerstate;
    struct {
        BOOL active;
        VECTOR2 origin;
    } camera_prediction;
    struct {
        DWORD width;
        DWORD height;
        BYTE *visible;
        BYTE *explored;
        BYTE *texture;
    } fow;
    LPENTITYSTATE cursorEntity;
    struct {
        DWORD image;
        FLOAT radius;
    } cursor_splat;
    DWORD hover_entity;     /* entity number under mouse cursor (0 = none) */
    LPMODEL moveConfirmation;
    DWORD num_entities;
    /* Compact list of entity numbers whose current state carries a live model.
     * CL_ParseFrame and CL_AddEntities iterate this instead of scanning all
     * MAX_CLIENT_ENTITIES slots every frame. */
    DWORD active_entities[MAX_CLIENT_ENTITIES];
    DWORD num_active;
    DWORD time;
    struct {
        RECT rect;
        bool in_progress;
        DWORD entity_nums[MAX_SELECTED_ENTITIES];  /* Currently selected entity numbers */
        DWORD num_selected;                         /* Number of currently selected entities */
    } selection;
    struct {
        DWORD entity_nums[MAX_SELECTED_ENTITIES];
        DWORD num_selected;
    } groups[MAX_CONTROL_GROUPS];
    DWORD group_last;    /* last recalled group, MAX_CONTROL_GROUPS if none */
    DWORD group_last_ms;
};

struct client_static {
    struct netchan netchan;
    keydest_t key_dest;
    connstate_t state;
    DWORD disable_screen;       /* loading plaque timestamp; freeze screen while nonzero */
    int disable_servercount;    /* servercount when plaque was raised */
};

// cl_main.c
void CL_Connect(LPCSTR host, unsigned short port);
void CL_Disconnect(LPCSTR reason, BOOL notify);
void CL_SetMenuBindings(void);
void CL_SetGameplayInput(void);
void CL_SetGameplayBindings(void);
void CL_BeginLoadingMap(LPCSTR mapName);
void CL_RequestUnitUI(DWORD num_selected, DWORD *entity_nums);
VECTOR2 CL_ClampCameraPosition(VECTOR2 position);

void V_RenderView(void);
void V_Shutdown(void);
void CL_PrepRefresh(void);
void CL_RegisterConfigString(DWORD index);
void CL_UpdateConfigString(DWORD index, LPCSTR olds);
void CL_RestartRefresh(void);
// cl_parse.c
void CL_ParseServerMessage(LPSIZEBUF msg);
void CL_AddActiveEntity(DWORD index);
void CL_RemoveActiveEntity(DWORD index);

// cl_window.c
void CL_WindowOpen(uiWindowDef_t const *def, HANDLE layout);
void CL_WindowClose(DWORD id);
void CL_WindowClear(void);
void CL_WindowDraw(void);
BOOL CL_WindowMouseEvent(menuMouseEvent_t event, int x, int y, int32_t param);
BOOL CL_WindowKeyEvent(int key);
BOOL CL_WindowModalActive(void);

void CON_DrawConsole(void);
void CON_printf(LPCSTR fmt, ...);
void CON_Init(void);
void CON_ToggleConsole(void);
void CON_TextInput(LPCSTR text);
void CON_KeyEvent(int key, bool down);

// cl_view.c
//void Matrix4_fromViewAngles(LPCVECTOR3 target, LPCVECTOR3 angles, float distance, LPMATRIX4 output);
//void Matrix4_getLightMatrix(LPCVECTOR3 sunangles, LPCVECTOR3 target, float scale, LPMATRIX4 output);
void Matrix4_getCameraMatrix(LPMATRIX4 output);
/* Paused views retain the last scene and render time. Zero delta is required
 * because model renderers emit effects while submitting cached entities. */
static inline BOOL V_AdvanceSceneTime(viewDef_t *view, DWORD now, LPDWORD last, BOOL paused) {
    DWORD elapsed = *last ? now - *last : 0;
    *last = now;
    if (paused) { view->deltaTime = 0; return false; }
    view->time = view->time ? view->time + elapsed : now;
    view->deltaTime = elapsed;
    return true;
}
void V_AddEntity(renderEntity_t *ent);
BOOL V_FindEntity(DWORD number, renderEntity_t *out);
void V_AddDecal(renderDecal_t *decal);

// cl_scrn.c
LPCUIFRAME SCR_Clear(HANDLE data);
LPCUIFRAME SCR_ClearWindow(HANDLE data);
DWORD SCR_NumFrames(void);
LPUIFRAME SCR_Frame(DWORD number);
LPCRECT SCR_LayoutRect(LPCUIFRAME frame);
void CL_LayoutDrawMinimap(LPCUIFRAME frame, LPCRECT screen);
void CL_ClearMinimap(void);
void CL_ParseMinimapPing(LPSIZEBUF msg);
void CL_UpdateMinimapModel(void);
#ifdef BZ_TESTS
DWORD CL_MinimapPingCount(void);
DWORD CL_MinimapRecentCount(void);
#endif
LPCENTITYSTATE SCR_LayoutContextEntity(void);
BOOL SCR_LayoutContextValue(DWORD stat, LPFLOAT value);
BOOL SCR_LayoutWorldHoverRoot(LPRECT root);
FLOAT SCR_UICanvasWidth(void);
VECTOR2 SCR_ScreenToUI(int x, int y);
BOOL SCR_ProjectWorldPoint(LPCVECTOR3 point, LPVECTOR2 screen);
VECTOR2 SCR_GetAxisBounds(LPCRECT rect, bool is_x_axis);
FLOAT SCR_NormalizeAnchorOffset(uiFramePoint_t const *p, bool is_x_axis);
VECTOR2 SCR_SolveAxisPosition(LPCUIFRAME frame,
                              uiFramePoints_t const points,
                              FLOAT width,
                              bool is_x_axis,
                              bool assigned_size);
LPCSTR SCR_GetStringValue(LPCUIFRAME frame);
LPCSTR SCR_GetTooltipText(LPCUIFRAME frame);
drawText_t SCR_GetDrawText(LPCUIFRAME frame,
                         FLOAT avl_width,
                         LPCSTR text,
                         uiLabel_t const *label);
void SCR_UpdateScreen(DWORD msec);
void SCR_BeginLoadingPlaque(void);
void SCR_EndLoadingPlaque(void);
void SCR_ClearLayoutResources(void);

// cl_screenshot.c
extern BOOL cl_screenshot_pending;
extern DWORD cl_screenshot_delay;
void CL_Screenshot_f(void);
BOOL CL_ScreenshotReady(void);

// cl_input.c
void CL_Input(void);
void CL_InitInput(void);

// cl_tent.c
void CL_ParseTEnt(LPSIZEBUF msg);
void CL_AddTEnts(void);
void CL_DrawTEnts(void);
void CL_ClearTEnts(void);

// cl_main.c - UI integration
int CL_ModelIndex(LPCSTR modelName);
int CL_ImageIndex(LPCSTR imageName);
LPCSTR CL_ResolveImagePath(LPCSTR imageName);
int CL_FontIndex(LPCSTR fontName, DWORD fontSize);
void CL_UIMenuCommand(LPCSTR command);

/* Entity one-shot sound/effect events (cl_fx.c) */
void CL_EntityEvent(entityState_t const *ent);

/* Unit UI data parsing (Phase 8) */
void CL_ParseUnitUI(LPSIZEBUF msg);

extern struct client_state cl;
extern struct client_static cls;
extern refExport_t re;
extern menuExport_t menu;
extern mouseEvent_t mouse;
extern BOOL scr_initialized;

#endif
