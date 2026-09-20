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
    DWORD players;
    DWORD maxPlayers;
    DWORD speed;
    DWORD slots;
} menuLanGame_t;

/* Callbacks provided by the client to the menu library.
 * The UI imports file I/O, memory allocation, and command forwarding. */
typedef struct {
    /* File system operations (archive-agnostic, Quake 3 pattern) */
    int (*FS_ReadFile)(LPCSTR fileName, void **buf);  /* Returns file size, allocates buf */
    void (*FS_FreeFile)(void *buf);
    int (*FS_GetFileList)(LPCSTR path, LPCSTR extension, char *listbuf, int bufsize);
    void (*FS_WriteFile)(LPCSTR path, const void *data, int size); /* Write to local disk */
    void (*UserPath)(LPCSTR rel, LPSTR out, DWORD out_size); /* Resolve writable per-user game data */
    
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
    int (*Cmd_Argc)(void);
    LPCSTR (*Cmd_Argv)(int arg);
    LPCSTR (*Cmd_ArgsFrom)(int arg);
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
    void (*PlayMusic)(LPCSTR playlist);
    void (*StopMusic)(void);

    /* Full-screen pre-rendered movie playback. Returns false when unsupported or unavailable. */
    BOOL (*PlayMovie)(LPCSTR path);
} menuImport_t;

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
    
    void (*UpdateLobbySetup)(lobbyState_t const *state);
} menuExport_t;

/* Entry point called by the client to get the UI function table.
 * The client must fill the menuImport_t struct before calling this. */
menuExport_t M_GetAPI(menuImport_t mi);

#endif
