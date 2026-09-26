#ifndef game_h
#define game_h

#include "../common/shared.h"
#include "../common/mpq.h"

#define SVF_NOCLIENT 0x00000001    // don't send entity to clients, even if it has effects
#define SVF_DEADMONSTER 0x00000002    // treat as CONTENTS_DEADMONSTER for collision
#define SVF_MONSTER 0x00000004    // treat as CONTENTS_MONSTER for collision
#define SVF_STATIC_SCENERY 0x00000008 // snapshot visibility; client fog shades map doodads/destructibles independently of unit sight
#define SVF_OWNER_ONLY 0x00000010 // snapshot visibility; send only to the client selected by entityState_t.player

KNOWN_AS(client_s, gameClient_t);
KNOWN_AS(edict_s, edict_t);
KNOWN_AS(link_s, link_t);

typedef struct edict_s edict_t;

struct link_s {
    link_t * prev, *next;
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
    uint32_t size;
} pfWriteData_t;

struct game_import {
    handle_t (*MemAlloc)(long size);
    void (*MemFree)(handle_t);
    int (*ModelIndex)(cstring_t modelName);
    int (*SoundIndex)(cstring_t soundName);
    int (*SoundIndexAlias)(cstring_t soundName, cstring_t alias);
    void (*Sound)(edict_t * ent, int channel, int sound_index, float volume, float attenuation, float timeofs);
    void (*PositionedSound)(vector3_t const * origin, edict_t * ent, int channel, int sound_index, float volume,
                            float attenuation, float timeofs);
    void (*SoundPolicy)(vector3_t const * origin, edict_t * ent, int channel, int sound_index, float volume,
                         float attenuation, float timeofs, soundPolicy_t const *policy);
    void (*MinimapPing)(edict_t * ent, vector2_t const * position, float duration, color32_t color, uint32_t flags);
    int (*ImageIndex)(cstring_t imageName);
    int (*FontIndex)(cstring_t fontName, uint32_t fontSize);
    void (*LinkEntity)(edict_t * ent);
    void (*UnlinkEntity)(edict_t * ent);
    uint32_t (*BoxEdicts)(box2_t const * area, edict_t * *list, uint32_t maxcount, bool (*pred)(edict_t const *));
    void (*MenuAction)(cstring_t action, cstring_t arg);
    /* Queue a client-side movie to interpose the next deferred session action. */
    void (*QueueMovie)(cstring_t path);
    void (*ClearWorld)(void);
    /* Keep the native window responsive during synchronous map loading without
     * advancing commands, client simulation, or server simulation. */
    void (*LoadingFrame)(void);
    handle_t (*ReadFile)(cstring_t filename, uint32_t * size);
    /* Calls callback for every archive copy of filename, lowest priority first.
     * Useful for merging layered data files (e.g. GameData/Assets.txt). */
    void (*ReadFileAll)(cstring_t filename, void (*callback)(handle_t buf, uint32_t size, void *ud), void *ud);
    /* Mount/clear the highest-priority FS archive (current map MPQ). NULL clears. */
    void (*SetPriorityArchive)(handle_t archive);
    uint32_t (*GetTime)(void);
    /* Rewind/advance the simulation clock only. sv.framenum indexes the snapshot delta
     * ring and is process state, so a loaded game must not move it. */
    void (*SetGameTime)(uint32_t time);
    /* Freeze only authoritative simulation advancement. The server keeps
     * packet processing and client transport alive while paused. */
    void (*SetPaused)(bool paused);
    void (*multicast)(vector3_t const * origin, multicast_t to);
    void (*unicast)(edict_t *ent);
    void (*Write)(pfWriteType_t type, void const *value);

    void (*configstring)(uint32_t index, cstring_t string);
    void (*confignstring)(uint32_t index, cstring_t string, uint32_t len);
    cstring_t (*GetConfigstring)(uint32_t index);
    void (*error)(cstring_t fmt, ...);
    void (*ApplyLobbySettings)(mapInfo_t * info);

    /* Cvar access — allows the game library to read command-line/config values
     * without linking directly against common.  Returns fallback if not set. */
    cstring_t (*CvarString)(cstring_t name, cstring_t fallback);

    /* Resolve writable per-game config/state without linking game modules against engine common. */
    void (*UserPath)(cstring_t rel, string_t out, uint32_t out_size);
    /* Resolve save files under the platform's per-user data directory. */
    void (*SavePath)(cstring_t rel, string_t out, uint32_t out_size);
    /* Enumerate save basenames as a double-NUL-terminated list. */
    uint32_t (*ListSaves)(string_t out, uint32_t out_size);
    /* Delete one save basename from the writable save directory. */
    bool (*DeleteSave)(cstring_t rel);
};

struct client;

/* Unit UI query result (Phase 8) */
typedef struct {
    char art[256];
    char tooltip[256];
    char ubertip[512];
    char command[256];
    char hotkey;
    uint8_t x;
    uint8_t y;
    uint8_t research;
    uint8_t building_upgrade; /* unit-type morph command; uses target unit data/costs */
    uint32_t level; /* authored research level used for owner-specific tooltip costs */
    uint8_t active;
    uint8_t disabled;
    uint32_t number; /* optional command-button numeric overlay; 0 hides it */
    float cooldown; /* fraction of the ability's cooldown still remaining (0=ready, 1=just used) */
    float manacost; /* mana cost to cast this ability at its current level (0 if not a spell) */
    char alternate[256]; /* optional secondary command, normally activated by right click */
    uint8_t alternate_active; /* presentation state for the secondary command */
    uint32_t cooldown_start_time; /* authoritative server milliseconds; zero when ready */
    uint32_t cooldown_end_time;   /* authoritative server milliseconds; zero when ready */
} gameCommandButton_t;

typedef struct {
    char art[256];
    char tooltip[256];
    char ubertip[512];
    uint8_t slot;
    uint32_t charges;
} gameInventoryItem_t;

typedef struct {
    char art[256];
    uint32_t starttime;
    uint32_t endtime;
} gameQueueItem_t;

struct game_export {
    void (*Init)(void);
    void (*Shutdown)(void);
    void (*RunFrame)(void);
    cstring_t (*GetThemeValue)(cstring_t filename);
    void (*ClientCommand)(edict_t * ent, uint32_t argc, cstring_t argv[]);
    void (*ClientInput)(edict_t * ent, inputCmd_t const * cmd);
    /* Read destination metadata and write the loading layout into the multicast buffer, before LoadMap. */
    bool (*PrepareMap)(cstring_t mapFilename);
    void (*ClientBegin)(edict_t * ent);
    bool (*CanSeeEntity)(uint32_t player, edict_t const * ent);
    /* Cheap predicate, called for each visible candidate to preserve it under saturation. */
    bool (*IsSnapshotPriorityEntity)(uint32_t player, edict_t const * ent);
    void (*CustomizeEntity)(uint32_t player, edict_t const * ent, entityState_t * state);
    uint32_t (*WriteClientDatagram)(edict_t * ent, uint8_t * data, uint32_t size);
    uint32_t (*PlayerCreateMap)(void);
    bool (*LoadMap)(cstring_t mapFilename);
    bool (*SaveGame)(cstring_t filename);
    bool (*LoadGame)(cstring_t filename);
    bool (*GetSaveMap)(cstring_t filename, string_t map, uint32_t map_size);
    box2_t (*GetWorldBounds)(void);
    bool (*PathingEntityIsIgnored)(edict_t const * ent);
    
    edict_t *edicts;
    int num_edicts;
    int max_edicts;
    int max_clients;
    int edict_size;
};

struct game_export *GetGameAPI(struct game_import *game_import);

/* Invisible controllers use the same movement axes as actors, with a game-owned focus speed. */
static inline vector2_t input_move_focus(inputCmd_t const * cmd, player_t const * ps, float speed) {
    uint32_t bits = cmd->move.buttons;
    vector3_t dir = { !!(bits & BZ_MOVE_FORWARD) - !!(bits & BZ_MOVE_BACK),
        !!(bits & BZ_MOVE_LEFT) - !!(bits & BZ_MOVE_RIGHT), 0 };
    dir = Vector3_rotateAroundAxis(&dir, &(vector3_t){0, 0, 1}, DEG2RAD(ps->viewangles.z));
    vector3_t pos = Vector3_mad(&ps->vieworigin, speed * cmd->move.msec / 1000.0f, &dir);
    return (vector2_t){ pos.x, pos.y };
}

#endif
