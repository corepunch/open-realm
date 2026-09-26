#ifndef server_h
#define server_h

#include "../common/common.h"
#include "game.h"

#include "../common/mpq.h"

#define EDICT_NUM(n) ((edict_t *)((string_t)ge->edicts + ge->edict_size*(n)))
#define NUM_FOR_EDICT(e) (uint32_t)(((string_t)(e)-(string_t)ge->edicts) / ge->edict_size)

#define BZ_SIGNON_SIZE 1400 // bytes; fits a 1500-byte LAN MTU with UDP/IP headers; bounds remote startup batches

/* Loopback accepts engine-sized messages; UDP startup must fit an individual datagram. */
static inline uint32_t SV_SignonLimit(struct netchan const *chan) { return chan->remote_address.type == NA_LOOPBACK ? chan->message.maxsize : MIN(chan->message.maxsize, BZ_SIGNON_SIZE); }

KNOWN_AS(client_frame, CLIENTFRAME);
KNOWN_AS(client, CLIENT);

typedef enum {
    ss_dead, // no map loaded
    ss_lobby, // LAN pregame lobby
    ss_loading, // spawning level edicts
    ss_game, // actively running
    ss_cinematic,
    ss_demo,
    ss_pic
} serverState_t;

typedef enum {
    cs_free,        // can be reused for a new connection
    cs_zombie,      // client has been disconnected, but don't reuse
                    // connection for a couple seconds
    cs_connected,   // has been assigned to a client_t, but not in game yet
    cs_spawned      // client is fully in game
} clientState_t;

struct client_s {
    PLAYER ps; // communicated by server to clients
    int ping;
    // the game dll can add anything it wants after
    // this point in the structure
};

struct edict_s {
    entityState_t s;
    LPGAMECLIENT client;
    pathTex_t *pathtex;
    float collision;
    BOX2 bounds;
    uint32_t svflags;
    uint32_t selected;
    uint32_t areanum;
    LINK area;
    bool inuse;
    BOX2 areabounds;
};

struct client_frame {
    PLAYER ps;
    uint32_t num_entities;
    uint32_t first_entity;        // into the circular sv_packet_entities[]
};

struct client {
    struct client_frame frames[UPDATE_BACKUP];
    struct netchan netchan;
    clientState_t state;
    edict_t *edict; // EDICT_NUM(clientnum+1)
    uint32_t lastframe;
    uint32_t playernum;
    uint32_t lobby_slot;
    char userinfo[256];
    UINAME name;
};

extern struct server_static {
    struct client clients[MAX_CLIENTS];
    lobbyState_t lobby;
    LPENTITYSTATE client_entities;
    bool initialized;
    uint32_t num_clients;
    uint32_t num_client_entities;
    uint32_t next_client_entities;
    uint32_t realtime;
} svs;

typedef enum {
    SHAPETYPE_BOX,
    SHAPETYPE_PLANE,
    SHAPETYPE_SPHERE,
    SHAPETYPE_CYLINDER,
} MODELCOLLISIONSHAPETYPE;

extern struct server {
    serverState_t state;
    PATHSTR name;
    PATHSTR configstrings[MAX_CONFIGSTRINGS];
    PATHSTR sound_aliases[MAX_SOUNDS];
    bool syncstrings[MAX_CONFIGSTRINGS];
    uint32_t framenum;
    uint32_t time;
    bool paused;
    uint32_t next_frame_msec; /* real-time deadline for the next simulation frame */
    uint32_t pause_msec; /* wall-clock accumulator used only for paused keepalive snapshots */
    uint32_t keepalive; /* real-time deadline for connected clients without gameplay snapshots */
    LPENTITYSTATE baselines;
    sizeBuf_t loading; /* retained compressed loading layout for initial and late connections */
    uint16_t loading_end[3]; /* first gameplay index in each loading media pool: models, images, fonts */
    sizeBuf_t multicast;
    uint8_t multicast_buf[MAX_MSGLEN];
} sv;

/* Match Quake II's "never get more than one tic behind" scheduler policy.
 * A long blocking client/map-loading hitch is wall-clock time, not simulation
 * work that should later be replayed at render-loop speed. Preserve normal
 * sub-tick lateness, but rebase a deadline that is more than one fixed step
 * overdue before running the next game frame. */
static inline uint32_t SV_ClampSimulationDeadline(uint32_t realtime, uint32_t deadline) {
    if (realtime >= deadline && realtime - deadline > FRAMETIME) {
        return realtime;
    }
    return deadline;
}

extern struct game_export *ge;

// sv_init.c
void SV_StartLobby(cstring_t mapFilename);
void SV_Map(cstring_t pFilename);
bool SV_LoadGame(cstring_t name, cstring_t map);
bool SV_GetSaveMap(cstring_t name, string_t map, uint32_t map_size);
#ifdef WOW
uint32_t SV_PlayerCreateMap(void);
#endif
void SV_ClientConnect(void);
void SV_InitGame(void);
bool SV_BuildLoadingScreen(void);
void SV_SendLoadingScreen(LPCLIENT cl);
LPCLIENT SV_FindClientByAddr(const netadr_t *from);
void SV_DirectConnect(const netadr_t *from, cstring_t userinfo);
void SV_ConnectionlessPacket(const netadr_t *from, LPSIZEBUF msg);
void SV_LobbySetConfig(uint32_t speed, uint32_t slots, cstring_t map_name);
void SV_LobbySetSlot(uint32_t slot, lobbySlot_t const *config);
void SV_LobbyInit(cstring_t mapFilename);
void SV_LobbyClientInit(LPCLIENT cl, cstring_t userinfo);
bool SV_LobbyAssignClient(uint32_t clientnum, bool host);
void SV_ApplyLobbySettings(LPMAPINFO info);
void SV_LobbyBroadcastSetup(void);
void SV_LobbyWriteSetup(LPCLIENT cl);
void SV_LobbyAddCommands(void);
void SV_BuildClientFrame(LPCLIENT client);
void SV_WriteFrameToClient(LPCLIENT client);
void SV_SetPaused(bool paused);
void SV_ParseClientMessage(LPSIZEBUF msg, LPCLIENT client);
int SV_ModelIndex(cstring_t name);
int SV_SoundIndex(cstring_t name);
int SV_SoundIndexAlias(cstring_t name, cstring_t alias);
LPCLIENT SV_ClientForEntityRecipient(LPEDICT ent);
LPCLIENT SV_ClientForEdictRecipient(LPEDICT ent);
void PF_Unicast(LPEDICT ent);
void SV_StartSoundPolicy(LPCVECTOR3 origin, LPEDICT ent, int channel, int sound_index, float volume,
                         float attenuation, float timeofs, soundPolicy_t const *policy);
void SV_StartSound(LPCVECTOR3 origin, LPEDICT ent, int channel, int sound_index, float volume, float attenuation,
                   float timeofs);
void SV_MinimapPing(LPEDICT ent, LPCVECTOR2 position, float duration, COLOR32 color, uint32_t flags);
int SV_ImageIndex(cstring_t name);
int SV_FontIndex(cstring_t name, uint32_t fontSize);
//void SV_LoadModels(void); // model animation data is loaded lazily by game modules now

// sv_game.c
void SV_WritePayload(LPSIZEBUF msg, uint8_t opcode, sizeBuf_t const *payload);
void SV_Multicast(LPCVECTOR3 origin, multicast_t to);
void SV_InitGameProgs(void);

// sv_main.c
uint32_t SV_ConfigStringWireSize(uint32_t index);
void SV_WriteConfigString(LPSIZEBUF msg, uint32_t i);
void SV_QueuePendingConfigStrings(void);
void SV_SetConfigString(uint32_t index, cstring_t value, uint32_t len);

// sv_user.c
void SV_ExecuteUserCommand(LPSIZEBUF msg, LPCLIENT client);
void SV_LobbyBroadcastChat(cstring_t sender, cstring_t text);
void SV_LobbyBroadcastChatFrom(uint32_t sender_client, cstring_t sender, cstring_t text);

/* Unit UI data requests (Phase 8) */
void SV_HandleUnitUIRequest(LPCLIENT client, LPSIZEBUF msg);

// sv_world.c
void SV_LinkEntity(LPEDICT ent);
void SV_UnlinkEntity(LPEDICT ent);
uint32_t SV_AreaEdicts(LPCBOX2 area, LPEDICT *list, uint32_t maxcount, bool (*pred)(LPCEDICT));
void SV_ClearWorld(void);

#endif
