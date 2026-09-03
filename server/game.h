#ifndef game_h
#define game_h

#include "../common/shared.h"
#include "../common/mpq.h"

#define SVF_NOCLIENT 0x00000001    // don't send entity to clients, even if it has effects
#define SVF_DEADMONSTER 0x00000002    // treat as CONTENTS_DEADMONSTER for collision
#define SVF_MONSTER 0x00000004    // treat as CONTENTS_MONSTER for collision
#define SVF_STATIC_SCENERY 0x00000008 // snapshot visibility; client fog shades map doodads/destructibles independently of unit sight
#define SVF_OWNER_ONLY 0x00000010 // snapshot visibility; send only to the client selected by entityState_t.player

KNOWN_AS(client_s, GAMECLIENT);
KNOWN_AS(edict_s, EDICT);
KNOWN_AS(link_s, LINK);

typedef struct edict_s edict_t;

struct link_s {
    LPLINK prev, next;
};

typedef enum {
    PF_BYTE,
    PF_SHORT,
    PF_LONG,
    PF_FLOAT,
    PF_STRING,
    PF_POSITION,
    PF_DIRECTION,
    PF_ANGLE,
    PF_ENTITY,
    PF_UIFRAME,
    PF_UIWINDOWFRAME,
    PF_DATA,
} pfWriteType_t;

typedef struct {
    void const *data;
    DWORD size;
} pfWriteData_t;

struct game_import {
    HANDLE (*MemAlloc)(long size);
    void (*MemFree)(HANDLE);
    int (*ModelIndex)(LPCSTR modelName);
    int (*SoundIndex)(LPCSTR soundName);
    void (*Sound)(LPEDICT ent, int channel, int sound_index, FLOAT volume, FLOAT attenuation, FLOAT timeofs);
    void (*PositionedSound)(LPCVECTOR3 origin, LPEDICT ent, int channel, int sound_index, FLOAT volume,
                            FLOAT attenuation, FLOAT timeofs);
    void (*MinimapPing)(LPEDICT ent, LPCVECTOR2 position, FLOAT duration, COLOR32 color, DWORD flags);
    int (*ImageIndex)(LPCSTR imageName);
    int (*FontIndex)(LPCSTR fontName, DWORD fontSize);
    void (*LinkEntity)(LPEDICT ent);
    void (*UnlinkEntity)(LPEDICT ent);
    DWORD (*BoxEdicts)(LPCBOX2 area, LPEDICT *list, DWORD maxcount, BOOL (*pred)(LPCEDICT));
    void (*MenuAction)(LPCSTR action, LPCSTR arg);
    void (*ClearWorld)(void);
    HANDLE (*ReadFile)(LPCSTR filename, LPDWORD size);
    /* Calls callback for every archive copy of filename, lowest priority first.
     * Useful for merging layered data files (e.g. GameData/Assets.txt). */
    void (*ReadFileAll)(LPCSTR filename, void (*callback)(HANDLE buf, DWORD size, void *ud), void *ud);
    DWORD (*GetTime)(void);
    /* Freeze only authoritative simulation advancement. The server keeps
     * packet processing and client transport alive while paused. */
    void (*SetPaused)(BOOL paused);
    void (*multicast)(LPCVECTOR3 origin, multicast_t to);
    void (*unicast)(edict_t *ent);
    void (*Write)(pfWriteType_t type, void const *value);
    void (*GameCommand)(edict_t *ent, LPCSTR command, void const *data, DWORD size);

    void (*configstring)(DWORD index, LPCSTR string);
    void (*confignstring)(DWORD index, LPCSTR string, DWORD len);
    LPCSTR (*GetConfigstring)(DWORD index);
    void (*error)(LPCSTR fmt, ...);
    void (*ApplyLobbySettings)(LPMAPINFO info);

    /* Cvar access — allows the game library to read command-line/config values
     * without linking directly against common.  Returns fallback if not set. */
    LPCSTR (*CvarString)(LPCSTR name, LPCSTR fallback);

    /* Resolve writable per-game config/state without linking game modules against engine common. */
    void (*UserPath)(LPCSTR rel, LPSTR out, DWORD out_size);
    /* Resolve save files under the platform's per-user data directory. */
    void (*SavePath)(LPCSTR rel, LPSTR out, DWORD out_size);
};

struct client;

/* Unit UI query result (Phase 8) */
typedef struct {
    char art[256];
    char tooltip[256];
    char ubertip[512];
    char command[256];
    char hotkey;
    BYTE x;
    BYTE y;
    BYTE research;
    BYTE active;
    BYTE disabled;
    DWORD number; /* optional command-button numeric overlay; 0 hides it */
    FLOAT cooldown; /* fraction of the ability's cooldown still remaining (0=ready, 1=just used) */
    FLOAT manacost; /* mana cost to cast this ability at its current level (0 if not a spell) */
    char alternate[256]; /* optional secondary command, normally activated by right click */
    BYTE alternate_active; /* presentation state for the secondary command */
} gameCommandButton_t;

typedef struct {
    char art[256];
    char tooltip[256];
    char ubertip[512];
    BYTE slot;
    DWORD charges;
} gameInventoryItem_t;

typedef struct {
    char art[256];
    DWORD starttime;
    DWORD endtime;
} gameQueueItem_t;

struct game_export {
    void (*Init)(void);
    void (*Shutdown)(void);
    void (*RunFrame)(void);
    LPCSTR (*GetThemeValue)(LPCSTR filename);
    void (*ClientCommand)(LPEDICT ent, DWORD argc, LPCSTR argv[]);
    void (*ClientSetCameraPosition)(LPEDICT ent, LPCVECTOR2 position);
    void (*ClientBegin)(LPEDICT ent);
    BOOL (*CanSeeEntity)(DWORD player, LPCEDICT ent);
    void (*CustomizeEntity)(DWORD player, LPCEDICT ent, LPENTITYSTATE state);
    DWORD (*PlayerCreateMap)(void);
    bool (*LoadMap)(LPCSTR mapFilename);
    BOOL (*SaveGame)(LPCSTR filename);
    BOOL (*LoadGame)(LPCSTR filename);
    BOOL (*GetSaveMap)(LPCSTR filename, LPSTR map, DWORD map_size);
    BOX2 (*GetWorldBounds)(void);
    
    edict_t *edicts;
    int num_edicts;
    int max_edicts;
    int max_clients;
    int edict_size;
};

struct game_export *GetGameAPI(struct game_import *game_import);

#endif
