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
    char address[64];
    char hostname[80];
    char mapname[80];
    uint32_t players;
    uint32_t maxPlayers;
    uint32_t speed;
    uint32_t slots;
} menuLanGame_t;

/* Callbacks provided by the client to the menu library.
 * The UI imports file I/O, memory allocation, and command forwarding. */
typedef struct {
    /* File system operations (archive-agnostic, Quake 3 pattern) */
    int (*FS_ReadFile)(cstring_t fileName, void **buf);  /* Returns file size, allocates buf */
    void (*FS_FreeFile)(void *buf);
    int (*FS_GetFileList)(cstring_t path, cstring_t extension, char *listbuf, int bufsize);
    void (*FS_WriteFile)(cstring_t path, const void *data, int size); /* Write to local disk */
    void (*UserPath)(cstring_t rel, string_t out, uint32_t out_size); /* Resolve writable per-user game data */
    
    /* Memory allocation */
    handle_t (*MemAlloc)(long size);
    void (*MemFree)(handle_t);
    
    /* Asset indexing (for textures, models, fonts) */
    int (*ImageIndex)(cstring_t imageName);
    int (*ModelIndex)(cstring_t modelName);     /* register model by name, return cl.models index */
    int (*FontIndex)(cstring_t fontName, uint32_t fontSize);
    
    /* Command execution (following Quake 3 pattern)
     * UI executes console commands; engine dispatcher handles routing */
    void (*Cmd_AddCommand)(cstring_t name, void (*function)(void));
    int (*Cmd_Argc)(void);
    cstring_t (*Cmd_Argv)(int arg);
    cstring_t (*Cmd_ArgsFrom)(int arg);
    void (*Cmd_ExecuteText)(cstring_t text);
    void (*ServerCommand)(cstring_t text);
    cstring_t (*Cvar_String)(cstring_t name, cstring_t fallback);
    void (*Cvar_Set)(cstring_t name, cstring_t value);
    cstring_t (*GetConfigString)(uint32_t index);
    void (*LAN_RefreshServers)(void);
    uint32_t (*LAN_NumServers)(void);
    bool (*LAN_Server)(uint32_t index, menuLanGame_t *out);
    void (*LAN_ConnectServer)(uint32_t index);
   
    /* Renderer access for frame drawing */
    refExport_t * (*GetRenderer)(void);
    
    /* Output */
    void (*Printf)(cstring_t fmt, ...);

    /* Sound */
    void (*PlaySound)(uint32_t kit_id);
    void (*PlaySoundByName)(cstring_t name);
    void (*PlayMusic)(cstring_t playlist);
    void (*StopMusic)(void);

    /* Full-screen pre-rendered movie playback. Returns false when unsupported or unavailable. */
    bool (*PlayMovie)(cstring_t path);
} menuImport_t;

/* Function table exported by the menu library to the client. */
typedef struct {
    /* Initialization and shutdown */
    void (*Init)(void);
    void (*Shutdown)(void);
    
    /* Main loop integration — called at draw time with current client time */
    void (*Refresh)(uint32_t time);
    
    /* Input event handling */
    void (*KeyEvent)(int key, bool down, uint32_t time);
    void (*TextInput)(cstring_t text);
    bool (*MouseEvent)(menuMouseEvent_t event, int x, int y, int32_t param);
    
    void (*UpdateLobbySetup)(lobbyState_t const *state);
} menuExport_t;

/* Entry point called by the client to get the UI function table.
 * The client must fill the menuImport_t struct before calling this. */
menuExport_t M_GetAPI(menuImport_t mi);

#endif
