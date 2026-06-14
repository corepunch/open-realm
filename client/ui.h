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
#define MAX_INVENTORY_SLOTS 6
#define MAX_BUILD_QUEUE_ITEMS 7
#define MAX_LAYOUT_LAYERS 16

typedef enum {
    UI_CLIENT_MOUSE_NONE,
    UI_CLIENT_MOUSE_LEFT_DOWN,
    UI_CLIENT_MOUSE_LEFT_UP,
    UI_CLIENT_MOUSE_LEFT_DRAGGED,
    UI_CLIENT_MOUSE_RIGHT_DOWN,
    UI_CLIENT_MOUSE_RIGHT_UP,
    UI_CLIENT_MOUSE_RIGHT_DRAGGED,
} uiClientMouseEvent_t;

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
    BOOL (*ReadMapInfo)(LPCSTR mapName, LPMAPINFO info);
    BOOL (*FindMapPreviewTexture)(LPCSTR mapName, LPSTR out, DWORD out_size);
    void (*FreeMapInfo)(LPMAPINFO info);
    void (*DefaultMapName)(LPCSTR path, LPSTR out, DWORD out_size);
    void (*ResolveMapInfoString)(LPCMAPINFO info, LPCSTR text, LPSTR out, DWORD out_size);
    BOOL (*MapNameMatchesFile)(LPCSTR name, LPCSTR path);
    LPCSTR (*MapTilesetName)(BYTE tileset);
    LPCSTR (*MapSizeName)(DWORD width, DWORD height);
    void (*SanitizeMapListField)(LPSTR text);
    void (*SanitizeMapInfoText)(LPSTR text);
    
    /* Memory allocation */
    HANDLE (*MemAlloc)(long size);
    void (*MemFree)(HANDLE);
    
    /* Asset indexing (for textures, models, fonts) */
    int (*ModelIndex)(LPCSTR modelName);
    int (*ImageIndex)(LPCSTR imageName);
    int (*FontIndex)(LPCSTR fontName, DWORD fontSize);
    sheetRow_t *(*ReadSheet)(LPCSTR sheetFilename);
    sheetRow_t *(*ReadConfig)(LPCSTR configFilename);
    LPCSTR (*FindSheetCell)(sheetRow_t *sheet, LPCSTR row, LPCSTR column);
    
    /* Command execution (following Quake 3 pattern)
     * UI executes console commands; engine dispatcher handles routing */
    void (*Cmd_AddCommand)(LPCSTR name, void (*function)(void));
    void (*Cmd_ExecuteText)(LPCSTR text);
    void (*ServerCommand)(LPCSTR text);
    LPCSTR (*Cvar_String)(LPCSTR name, LPCSTR fallback);
    void (*Cvar_Set)(LPCSTR name, LPCSTR value);
    void (*LANRefreshServers)(void);
    DWORD (*LANNumServers)(void);
    BOOL (*LANServer)(DWORD index, uiLanGame_t *out);
    void (*LANConnectServer)(DWORD index);
    LPCSTR (*GetLoadingMap)(void);
    LPCSTR (*GetLoadingStatus)(void);
    FLOAT (*GetLoadingProgress)(void);
    
    /* Game state access (for in-game HUD) */
    LPCPLAYER (*GetPlayerState)(void);          /* Access to cl.playerstate */
    DWORD (*GetNumEntities)(void);              /* cl.num_entities */
    LPCENTITYSTATE (*GetEntity)(DWORD idx);     /* &cl.ents[idx].current */
    LPCMODEL (*GetModel)(DWORD idx);            /* cl.models[idx] */
    LPCMODEL (*GetPortrait)(DWORD idx);         /* cl.portraits[idx] */
    LPCTEXTURE (*GetTexture)(DWORD idx);        /* cl.pics[idx] */
    LPCTEXTURE *(*GetTextures)(void);           /* cl.pics, for inline text icons */
    LPCFONT (*GetFont)(DWORD idx);              /* cl.fonts[idx] */
    DWORD (*GetClientTime)(void);               /* cl.time */
    VECTOR2 (*GetMouseFdf)(void);               /* current mouse in Warcraft UI coords */
    DWORD (*GetMouseButton)(void);
    uiClientMouseEvent_t (*GetMouseEvent)(void);
    LPCUIFRAME (*LayoutClear)(HANDLE data);
    DWORD (*LayoutNumFrames)(void);
    LPUIFRAME (*LayoutFrame)(DWORD number);
    LPCRECT (*LayoutRect)(LPCUIFRAME frame);
    LPCSTR (*LayoutStringValue)(LPCUIFRAME frame);
    drawText_t (*LayoutDrawText)(LPCUIFRAME frame,
                                 FLOAT avl_width,
                                 LPCSTR text,
                                 uiLabel_t const *label);
    
    /* Unit UI data requests (for command card, inventory, build queue) */
    void (*RequestUnitUI)(DWORD num_selected, DWORD *entity_nums);
    
    /* Renderer access for frame drawing */
    LPRENDERER (*GetRenderer)(void);
    
    /* Error reporting */
    void (*Error)(LPCSTR fmt, ...);
    void (*Printf)(LPCSTR fmt, ...);
} uiImport_t;

/* Function table exported by the UI library to the client. */
typedef struct {
    /* Initialization and shutdown */
    void (*Init)(void);
    void (*Shutdown)(void);
    
    /* Main loop integration */
    void (*Refresh)(DWORD msec);
    void (*DrawFrame)(void);
    
    /* Input event handling */
    void (*KeyEvent)(int key, BOOL down, DWORD time);
    void (*TextInput)(LPCSTR text);
    void (*MouseEvent)(int x, int y, int button, BOOL down);
    
    /* Unit UI data updates (Phase 8: HUD migration) */
    void (*UpdateUnitUI)(DWORD num_units, uiUnitData_t *units);
    void (*UpdateLobbySetup)(lobbyState_t const *state);

    /* Server-authored layout layers decoded by the generic client. */
    void (*SetLayoutLayer)(DWORD layer, HANDLE data);
    void (*ClearLayoutLayer)(DWORD layer);
    BOOL (*HitTestLayout)(int x, int y);
} uiExport_t;

/* Entry point called by the client to get the UI function table.
 * The client must fill the uiImport_t struct before calling this. */
uiExport_t UI_GetAPI(uiImport_t uiimport);

#endif
