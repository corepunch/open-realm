#ifndef client_h
#define client_h

#include "common/common.h"
#include "tr_public.h"
#include "keys.h"
#include "client/ui.h"

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#ifdef WOW
#define UI_BASE_WIDTH 1.0f
#define UI_BASE_HEIGHT 1.0f
#else
#define UI_BASE_WIDTH 0.8f
#define UI_BASE_HEIGHT 0.6f
#endif
#define UI_MIN_ASPECT (4.0f / 3.0f)
#define MAX_CLIENT_ENTITIES MAX_GAME_ENTITIES
#define MAX_CONSOLE_MESSAGES 256
#define MAX_CONSOLE_MESSAGE_LEN 1024
#define VIEW_SHADOW_SIZE 1500
#define MAX_CONFIRMATION_OBJECTS 16
#define MAX_LAYOUT_LAYERS 16

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
    NUM_UI_MOUSE_EVENTS
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
    ca_loading,
    ca_active,
} connstate_t;

struct frame {
    int serverframe;
    int servertime;
    int oldclientframe;
};

struct client_state {
    LPMODEL models[MAX_MODELS];
    LPMODEL portraits[MAX_MODELS];
    LPCTEXTURE pics[MAX_IMAGES];
    LPTEXTURE dynamicPics[MAX_DYNAMIC_IMAGES];
    char dynamicPicNames[MAX_DYNAMIC_IMAGES][512];
    DWORD dynamicPicCursor;
    LPCFONT fonts[MAX_FONTSTYLES];
    PATHSTR configstrings[MAX_CONFIGSTRINGS];
    PATHSTR loading_map;
    char loading_status[128];
    FLOAT loading_progress;
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
    LPCMODEL moveConfirmation;
    DWORD num_entities;
    DWORD time;
    struct {
        RECT rect;
        bool in_progress;
        DWORD entity_nums[MAX_SELECTED_ENTITIES];  /* Currently selected entity numbers */
        DWORD num_selected;                         /* Number of currently selected entities */
    } selection;
};

struct client_static {
    struct netchan netchan;
    keydest_t key_dest;
    connstate_t state;
};

// cl_main.c
void CL_Connect(LPCSTR host, unsigned short port);
void CL_Disconnect(LPCSTR reason, BOOL notify);
void CL_SetMenuBindings(void);
void CL_SetGameplayInput(void);
void CL_SetGameplayBindings(void);
void CL_BeginLoadingMap(LPCSTR mapName);
void CL_LoadingUpdate(LPCSTR status, FLOAT progress);
void CL_RequestUnitUI(DWORD num_selected, DWORD *entity_nums);

void V_RenderView(void);
void V_Shutdown(void);
void CL_PrepRefresh(void);
void CL_ParseServerMessage(LPSIZEBUF msg);

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
void V_AddEntity(renderEntity_t *ent);
void V_AddDecal(renderDecal_t *decal);

// cl_scrn.c
LPCUIFRAME SCR_Clear(HANDLE data);
DWORD SCR_NumFrames(void);
LPUIFRAME SCR_Frame(DWORD number);
LPCRECT SCR_LayoutRect(LPCUIFRAME frame);
VECTOR2 SCR_MouseToFdf(void);
VECTOR2 SCR_GetAxisBounds(LPCRECT rect, bool is_x_axis);
FLOAT SCR_NormalizeAnchorOffset(uiFramePoint_t const *p, bool is_x_axis);
VECTOR2 SCR_SolveAxisPosition(LPCUIFRAME frame,
                              uiFramePoints_t const points,
                              FLOAT width,
                              bool is_x_axis);
LPCSTR SCR_GetStringValue(LPCUIFRAME frame);
drawText_t SCR_GetDrawText(LPCUIFRAME frame,
                         FLOAT avl_width,
                         LPCSTR text,
                         uiLabel_t const *label);
void SCR_UpdateScreen(DWORD msec);

// cl_input.c
void CL_Input(void);
void CL_InitInput(void);

// cl_tent.c
void CL_ParseTEnt(LPSIZEBUF msg);
void CL_AddTEnts(void);
void CL_ClearTEnts(void);

// cl_main.c - UI integration
int CL_ModelIndex(LPCSTR modelName);
int CL_ImageIndex(LPCSTR imageName);
int CL_FontIndex(LPCSTR fontName, DWORD fontSize);
void CL_UIMenuCommand(LPCSTR command);

/* Unit UI data parsing (Phase 8) */
void CL_ParseUnitUI(LPSIZEBUF msg);

extern struct client_state cl;
extern struct client_static cls;
extern refExport_t re;
extern uiExport_t ui;
extern mouseEvent_t mouse;

#endif
