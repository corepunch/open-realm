#ifndef server_h
#define server_h

#include "../common/common.h"
#include "game.h"

#include "../common/mpq.h"

#define EDICT_NUM(n) ((edict_t *)((LPSTR)ge->edicts + ge->edict_size*(n)))
#define NUM_FOR_EDICT(e) (DWORD)(((LPSTR)(e)-(LPSTR)ge->edicts) / ge->edict_size)

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
    FLOAT collision;
    BOX2 bounds;
    DWORD svflags;
    DWORD selected;
    DWORD areanum;
    LINK area;
    BOOL inuse;
    BOX2 areabounds;
};

struct client_frame {
    PLAYER ps;
    DWORD num_entities;
    DWORD first_entity;        // into the circular sv_packet_entities[]
};

struct client {
    struct client_frame frames[UPDATE_BACKUP];
    struct netchan netchan;
    clientState_t state;
    edict_t *edict; // EDICT_NUM(clientnum+1)
    DWORD lastframe;
    DWORD playernum;
    DWORD lobby_slot;
    char userinfo[256];
    UINAME name;
};

extern struct server_static {
    struct client clients[MAX_CLIENTS];
    lobbyState_t lobby;
    LPENTITYSTATE client_entities;
    bool initialized;
    DWORD num_clients;
    DWORD num_client_entities;
    DWORD next_client_entities;
    DWORD realtime;
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
    BOOL syncstrings[MAX_CONFIGSTRINGS];
    DWORD framenum;
    DWORD time;
    BOOL paused;
    DWORD next_frame_msec; /* real-time deadline for the next simulation frame */
    DWORD pause_msec; /* wall-clock accumulator used only for paused keepalive snapshots */
    LPENTITYSTATE baselines;
    sizeBuf_t multicast;
    BYTE multicast_buf[MAX_MSGLEN];
} sv;

extern struct game_export *ge;

// sv_init.c
void SV_StartLobby(LPCSTR mapFilename);
void SV_Map(LPCSTR pFilename);
BOOL SV_LoadGame(LPCSTR name, LPCSTR map);
BOOL SV_GetSaveMap(LPCSTR name, LPSTR map, DWORD map_size);
#ifdef WOW
DWORD SV_PlayerCreateMap(void);
#endif
void SV_ClientConnect(void);
void SV_InitGame(void);
LPCLIENT SV_FindClientByAddr(const netadr_t *from);
void SV_DirectConnect(const netadr_t *from, LPCSTR userinfo);
void SV_ConnectionlessPacket(const netadr_t *from, LPSIZEBUF msg);
void SV_LobbySetConfig(DWORD speed, DWORD slots, LPCSTR map_name);
void SV_LobbySetSlot(DWORD slot, lobbySlot_t const *config);
void SV_LobbyInit(LPCSTR mapFilename);
void SV_LobbyClientInit(LPCLIENT cl, LPCSTR userinfo);
BOOL SV_LobbyAssignClient(DWORD clientnum, BOOL host);
void SV_ApplyLobbySettings(LPMAPINFO info);
void SV_LobbyBroadcastSetup(void);
void SV_LobbyWriteSetup(LPCLIENT cl);
void SV_LobbyAddCommands(void);
void SV_BuildClientFrame(LPCLIENT client);
void SV_WriteFrameToClient(LPCLIENT client);
void SV_SetPaused(BOOL paused);
void SV_ParseClientMessage(LPSIZEBUF msg, LPCLIENT client);
int SV_ModelIndex(LPCSTR name);
int SV_SoundIndex(LPCSTR name);
LPCLIENT SV_ClientForEntityRecipient(LPEDICT ent);
void SV_StartSound(LPCVECTOR3 origin, LPEDICT ent, int channel, int sound_index, FLOAT volume, FLOAT attenuation,
                   FLOAT timeofs);
void SV_MinimapPing(LPEDICT ent, LPCVECTOR2 position, FLOAT duration, COLOR32 color, DWORD flags);
int SV_ImageIndex(LPCSTR name);
int SV_FontIndex(LPCSTR name, DWORD fontSize);
//void SV_LoadModels(void); // model animation data is loaded lazily by game modules now

// sv_game.c
void SV_WriteGameCommand(LPSIZEBUF msg, LPCSTR command, sizeBuf_t const *payload);
void SV_GameCommand(edict_t *ent, LPCSTR command, void const *data, DWORD size);
void SV_Multicast(LPCVECTOR3 origin, multicast_t to);
void SV_InitGameProgs(void);

// sv_main.c
void SV_WriteConfigString(LPSIZEBUF msg, DWORD i);
void SV_SetConfigString(DWORD index, LPCSTR value, DWORD len);

// sv_user.c
void SV_ExecuteUserCommand(LPSIZEBUF msg, LPCLIENT client);
void SV_LobbyBroadcastChat(LPCSTR sender, LPCSTR text);
void SV_LobbyBroadcastChatFrom(DWORD sender_client, LPCSTR sender, LPCSTR text);

/* Unit UI data requests (Phase 8) */
void SV_HandleUnitUIRequest(LPCLIENT client, LPSIZEBUF msg);

// sv_world.c
void SV_LinkEntity(LPEDICT ent);
void SV_UnlinkEntity(LPEDICT ent);
DWORD SV_AreaEdicts(LPCBOX2 area, LPEDICT *list, DWORD maxcount, BOOL (*pred)(LPCEDICT));
void SV_ClearWorld(void);

#endif
