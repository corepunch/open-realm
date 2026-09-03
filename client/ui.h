/*
 * ui.h — UI library public interface.
 *
 * Defines the import/export function tables for the UI library following
 * the same pattern as the renderer (client/tr_public.h) and game DLL
 * (server/game.h).
 *
 * The client fills uiImport_t with callbacks for file I/O, memory allocation,
 * and command execution, then calls UI_GetAPI() to receive the uiExport_t
 * function table.
 *
 * The UI library loads FDF files, builds frame hierarchies, manages menu
 * navigation, and handles input events. It owns its string table (loaded from
 * war3skins.txt) for localization. Commands are executed via Cmd_ExecuteText;
 * the engine's command dispatcher handles routing (local vs server).
 */
#ifndef ui_h
#define ui_h

#include "client/tr_public.h"

/* Unit UI data structures (Phase 8) */
#define MAX_COMMAND_BUTTONS 12
#define MAX_INVENTORY_SLOTS 16 // must match WOW_UI_INVENTORY_SLOTS in wow_ui_shared.h
#define MAX_BUILD_QUEUE_ITEMS 7
#define MAX_LAYOUT_LAYERS 16

#ifndef UI_MOUSE_EVENT_DEFINED
#define UI_MOUSE_EVENT_DEFINED
typedef enum {
    UI_MOUSE_MOVE,
    UI_MOUSE_DOWN,
    UI_MOUSE_UP,
    UI_MOUSE_SCROLL,
} uiMouseEvent_t;
#endif

/* Pack/unpack signed 16-bit dx/dy into the generic int32_t param (WinAPI MAKELPARAM style). */
#define UI_MOUSE_PARAM(dx, dy)  ((int32_t)(((uint16_t)(int16_t)(dx)) | ((uint32_t)((uint16_t)(int16_t)(dy)) << 16)))
#define UI_MOUSE_PARAM_X(p)     ((int16_t)(((uint32_t)(p)) & 0xFFFF))
#define UI_MOUSE_PARAM_Y(p)     ((int16_t)((((uint32_t)(p)) >> 16) & 0xFFFF))

typedef struct {
    char art[256];        /* Button icon path */
    char tooltip[256];    /* Tooltip text */
    char ubertip[512];    /* Extended tooltip */
    char command[256];    /* Command to execute on click */
    char hotkey;          /* Keyboard hotkey */
    BYTE x;               /* Warcraft command grid column */
    BYTE y;               /* Warcraft command grid row */
    BYTE research;        /* Uses research command */
    BYTE active;          /* Current selected entity is using this ability */
} uiCommandButton_t;

typedef struct {
    char art[256];        /* Item icon path */
    char tooltip[256];    /* Tooltip text */
    char ubertip[512];    /* Extended tooltip */
    BYTE slot;            /* Inventory slot index (0-5) */
} uiInventoryItem_t;

typedef struct {
    char art[256];        /* Queue item icon path */
    WORD entity;          /* Entity number of building unit */
} uiQueueItem_t;

typedef struct {
    char address[64];
    char hostname[80];
    char mapname[80];
    DWORD players;
    DWORD maxPlayers;
    DWORD speed;
    DWORD slots;
} uiLanGame_t;

typedef struct {
    WORD entity_num;                              /* Entity number */
    DWORD class_id;
    DWORD model;
    char name[128];
    char class_text[128];
    char icon_art[256];
    BYTE is_building;
    BYTE is_hero;
    BYTE is_constructing;
    BYTE health;
    BYTE mana;
    BYTE ability;
    WORD level;
    SHORT damage_min;
    SHORT damage_max;
    SHORT armor;
    SHORT food_used;
    SHORT food_made;
    SHORT gold_cost;
    SHORT lumber_cost;
    SHORT hero_strength;
    SHORT hero_agility;
    SHORT hero_intelligence;
    BYTE num_buttons;                             /* Number of command buttons */
    uiCommandButton_t buttons[MAX_COMMAND_BUTTONS];
    BYTE num_inventory;                           /* Number of inventory items */
    uiInventoryItem_t inventory[MAX_INVENTORY_SLOTS];
    BYTE num_queue;                               /* Number of build queue items */
    uiQueueItem_t queue[MAX_BUILD_QUEUE_ITEMS];
} uiUnitData_t;

/* Callbacks provided by the client to the UI library.
 * The UI imports file I/O, memory allocation, and command forwarding. */
typedef struct {
    /* File system operations (archive-agnostic, Quake 3 pattern) */
    int (*FS_ReadFile)(LPCSTR fileName, void **buf);  /* Returns file size, allocates buf */
    void (*FS_FreeFile)(void *buf);
    int (*FS_GetFileList)(LPCSTR path, LPCSTR extension, char *listbuf, int bufsize);
    void (*FS_WriteFile)(LPCSTR path, const void *data, int size); /* Write to local disk */
    
    /* Memory allocation */
    HANDLE (*MemAlloc)(long size);
    void (*MemFree)(HANDLE);
    
    /* Asset indexing (for textures, models, fonts) */
    int (*ImageIndex)(LPCSTR imageName);
    int (*ModelIndex)(LPCSTR modelName);     /* register model by name, return cl.models index */
    int (*FontIndex)(LPCSTR fontName, DWORD fontSize);
    
    /* Command execution (following Quake 3 pattern)
     * UI executes console commands; engine dispatcher handles routing */
    void (*Cmd_AddCommand)(LPCSTR name, void (*function)(void));
    void (*Cmd_ExecuteText)(LPCSTR text);
    void (*ServerCommand)(LPCSTR text);
    LPCSTR (*Cvar_String)(LPCSTR name, LPCSTR fallback);
    void (*Cvar_Set)(LPCSTR name, LPCSTR value);
    void (*LAN_RefreshServers)(void);
    DWORD (*LAN_NumServers)(void);
    BOOL (*LAN_Server)(DWORD index, uiLanGame_t *out);
    void (*LAN_ConnectServer)(DWORD index);
   
    /* Game state access (for in-game HUD) */
    DWORD (*GetTime)(void);                    /* Current client clock in milliseconds */
    LPCPLAYER (*GetPlayerState)(void);          /* Access to cl.playerstate */
    DWORD (*GetNumEntities)(void);              /* cl.num_entities */
    LPCENTITYSTATE (*GetEntity)(DWORD idx);     /* &cl.ents[idx].current */
    LPCMODEL (*GetModel)(DWORD idx);            /* cl.models[idx] */
    LPCMODEL (*GetPortrait)(DWORD idx);         /* cl.portraits[idx] */
    LPCTEXTURE (*GetTexture)(DWORD idx);        /* cl.pics[idx] */
    LPCTEXTURE *(*GetTextures)(void);           /* cl.pics, for inline text icons */
    LPCFONT (*GetFont)(DWORD idx);              /* cl.fonts[idx] */
    
    /* Renderer access for frame drawing */
    LPRENDERER (*GetRenderer)(void);
    
    /* Output */
    void (*Printf)(LPCSTR fmt, ...);

    /* Sound */
    void (*PlaySound)(DWORD kit_id);
    void (*PlaySoundByName)(LPCSTR name);
} uiImport_t;

/* Model positions are normalized viewport anchors; widening preserves authored vertical scale. */
static inline void UI_ModelMatrix(LPCUIMODEL model, FLOAT aspect, LPMATRIX4 out) {
    MATRIX4 proj, view, local;
    VECTOR3 dir = Vector3_sub(&model->target, &model->eye);
    FLOAT model_aspect = (model->aspect > 0.0f) ? model->aspect : 1.0f;
    FLOAT half_y = tanf(model->fov * (FLOAT)M_PI / 360.0f) / model_aspect;
    FLOAT half_x = half_y * aspect;
    Matrix4_lookAt(&view, &model->eye, &dir, &(VECTOR3){ 0, 0, 1 });
    if (model->projection == UI_MODEL_ORTHOGRAPHIC)
        Matrix4_ortho(&proj, -half_x, half_x, -half_y, half_y, model->znear, model->zfar);
    else
        Matrix4_perspective(&proj, model->fov, aspect, model->znear, model->zfar);
    Matrix4_identity(&local);
    Matrix4_translate(&local, &(VECTOR3){ model->pos.x * half_x, model->pos.z, model->pos.y * half_y });
    Matrix4_scale(&local, &model->scale);
    Matrix4_multiply(&view, &local, out);
    Matrix4_multiply(&proj, out, &local);
    *out = local;
}

typedef void (*uiGameCommand_t)(LPCSTR command, void const *data, DWORD size);

/* Gameplay-key callbacks may consume a key and optionally request that the
 * generic RTS client recentre its camera on the returned world position. */
#define UI_GAMEKEY_HANDLED         (1u << 0)
#define UI_GAMEKEY_CAMERA_POSITION (1u << 1)
typedef DWORD (*uiGameplayKeyEvent_t)(int key, DWORD modifiers, BOOL repeat, LPVECTOR2 camera_position);

/* Function table exported by the UI library to the client. */
typedef struct {
    /* Initialization and shutdown */
    void (*Init)(void);
    void (*Shutdown)(void);
    
    /* Main loop integration — called at draw time with current client time */
    void (*Refresh)(DWORD time);
    
    /* Input event handling */
    void (*KeyEvent)(int key, BOOL down, DWORD time);
    void (*TextInput)(LPCSTR text);
    BOOL (*MouseEvent)(uiMouseEvent_t event, int x, int y, int32_t param);
    
    /* Unit UI data updates (Phase 8: HUD migration) */
    void (*UpdateUnitUI)(DWORD num_units, uiUnitData_t *units);
    void (*UpdateLobbySetup)(lobbyState_t const *state);
    uiGameCommand_t GameCommand;
    uiGameplayKeyEvent_t GameplayKeyEvent;
    void (*ClearGameState)(void);
    void (*DrawGameOverlay)(void);

    /* Legacy named XML windows — show/hide a ui.dll-owned window by ID. */
    void (*ShowWindow)(const char *window_id, int show);
} uiExport_t;

/* Entry point called by the client to get the UI function table.
 * The client must fill the uiImport_t struct before calling this. */
uiExport_t UI_GetAPI(uiImport_t uiimport);

#endif
