/*
 * menu.h — menu library public interface.
 *
 * Defines the import/export function tables for the menu library following
 * the same pattern as the renderer (client/tr_public.h) and game DLL
 * (server/game.h).
 *
 * The client fills menuImport_t with callbacks for file I/O, memory allocation,
 * and command execution, then calls M_GetAPI() to receive the menuExport_t
 * function table.
 *
 * The menu library loads FDF files, builds frame hierarchies, manages menu
 * navigation, and handles input events. It owns its string table (loaded from
 * war3skins.txt) for localization. Commands are executed via Cmd_ExecuteText;
 * the engine's command dispatcher handles routing (local vs server).
 */
#ifndef menu_h
#define menu_h

#include "client/tr_public.h"

/* Unit UI data structures (Phase 8) */
#define MAX_COMMAND_BUTTONS 12
#define MAX_INVENTORY_SLOTS 16 // must match WOW_UI_INVENTORY_SLOTS in wow_ui_shared.h
#define MAX_BUILD_QUEUE_ITEMS 7
#define MAX_LAYOUT_LAYERS 16

#ifndef MENU_MOUSE_EVENT_DEFINED
#define MENU_MOUSE_EVENT_DEFINED
typedef enum {
    MENU_MOUSE_MOVE,
    MENU_MOUSE_DOWN,
    MENU_MOUSE_UP,
    MENU_MOUSE_SCROLL,
} menuMouseEvent_t;
#endif

/* Pack/unpack signed 16-bit dx/dy into the generic int32_t param (WinAPI MAKELPARAM style). */
#define MENU_MOUSE_PARAM(dx, dy)  ((int32_t)(((uint16_t)(int16_t)(dx)) | ((uint32_t)((uint16_t)(int16_t)(dy)) << 16)))
#define MENU_MOUSE_PARAM_X(p)     ((int16_t)(((uint32_t)(p)) & 0xFFFF))
#define MENU_MOUSE_PARAM_Y(p)     ((int16_t)((((uint32_t)(p)) >> 16) & 0xFFFF))

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
} menuCommandButton_t;

typedef struct {
    char art[256];        /* Item icon path */
    char tooltip[256];    /* Tooltip text */
    char ubertip[512];    /* Extended tooltip */
    BYTE slot;            /* Inventory slot index (0-5) */
} menuInventoryItem_t;

typedef struct {
    char art[256];        /* Queue item icon path */
    WORD entity;          /* Entity number of building unit */
} menuQueueItem_t;

typedef struct {
    char address[64];
    char hostname[80];
    char mapname[80];
    DWORD players;
    DWORD maxPlayers;
    DWORD speed;
    DWORD slots;
} menuLanGame_t;

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
    menuCommandButton_t buttons[MAX_COMMAND_BUTTONS];
    BYTE num_inventory;                           /* Number of inventory items */
    menuInventoryItem_t inventory[MAX_INVENTORY_SLOTS];
    BYTE num_queue;                               /* Number of build queue items */
    menuQueueItem_t queue[MAX_BUILD_QUEUE_ITEMS];
} menuUnitData_t;

/* Callbacks provided by the client to the menu library.
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
    LPCSTR (*GetConfigString)(DWORD index);
    void (*LAN_RefreshServers)(void);
    DWORD (*LAN_NumServers)(void);
    BOOL (*LAN_Server)(DWORD index, menuLanGame_t *out);
    void (*LAN_ConnectServer)(DWORD index);
   
    /* Renderer access for frame drawing */
    LPRENDERER (*GetRenderer)(void);
    
    /* Output */
    void (*Printf)(LPCSTR fmt, ...);

    /* Sound */
    void (*PlaySound)(DWORD kit_id);
    void (*PlaySoundByName)(LPCSTR name);
} menuImport_t;

typedef void (*menuGameCommand_t)(LPCSTR command, void const *data, DWORD size);

/* Function table exported by the menu library to the client. */
typedef struct {
    /* Initialization and shutdown */
    void (*Init)(void);
    void (*Shutdown)(void);
    
    /* Main loop integration — called at draw time with current client time */
    void (*Refresh)(DWORD time);
    
    /* Input event handling */
    void (*KeyEvent)(int key, BOOL down, DWORD time);
    void (*TextInput)(LPCSTR text);
    BOOL (*MouseEvent)(menuMouseEvent_t event, int x, int y, int32_t param);
    
    /* Unit UI data updates (Phase 8: HUD migration) */
    void (*UpdateUnitUI)(DWORD num_units, menuUnitData_t *units);
    void (*UpdatePlayerState)(LPCPLAYER state);
    void (*UpdateLobbySetup)(lobbyState_t const *state);
    menuGameCommand_t GameCommand;

    /* Resolve a Warcraft-specific symbolic image key for the local player. */
    LPCSTR (*ResolveImagePath)(LPCSTR key);

    /* Legacy named XML windows — show/hide a menu.dll-owned window by ID. */
    void (*ShowWindow)(const char *window_id, int show);
} menuExport_t;

/* Entry point called by the client to get the UI function table.
 * The client must fill the menuImport_t struct before calling this. */
menuExport_t M_GetAPI(menuImport_t menuimport);

#endif
