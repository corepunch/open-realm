#include "server.h"
#include "common/online.h"
#include <zlib.h>

static const struct { uint32_t base, count; } loading_pools[] = {
    { CS_MODELS, MAX_MODELS },
    { CS_IMAGES, MAX_IMAGES },
    { CS_FONTS, MAX_FONTSTYLES },
};

static bool SV_EnsureServerPort(void) {
    NET_ConfigSource(NS_SERVER, true);
    if (!NET_IsConfigured(NS_SERVER)) {
        fprintf(stderr, "SV_EnsureServerPort: failed to bind UDP server port\n");
        return false;
    }
    return true;
}

/* Publish server invariants that clients need before interpreting the game snapshot. */
static void SV_SetMapConfigStrings(void) {
    char maxclients[16], checksum[16];

    snprintf(maxclients, sizeof(maxclients), "%d", ge->max_clients);
    SV_SetConfigString(CS_MAXCLIENTS, maxclients, (uint32_t)(strlen(maxclients) + 1));
    snprintf(checksum, sizeof(checksum), "%u", (unsigned)CM_GetMapChecksum());
    SV_SetConfigString(CS_MAPCHECKSUM, checksum, (uint32_t)(strlen(checksum) + 1));
}

#ifndef TOOL_COMMON_NO_MPQ
static bool SV_SavePath(cstring_t name, PATHSTR path) {
    if (!name || !name[0] || strchr(name, '/') || strchr(name, '\\')) {
        fprintf(stderr, "save: invalid save name\n");
        return false;
    }
    FS_SavePath(name, path, sizeof(PATHSTR));
    return true;
}

bool SV_GetSaveMap(cstring_t name, string_t map, uint32_t map_size) {
    PATHSTR path;
    if (!SV_SavePath(name, path)) return false;
    if (!ge) SV_InitGameProgs();
    if (!ge || !ge->GetSaveMap || !ge->GetSaveMap(path, map, map_size)) {
        fprintf(stderr, "load: save %s has no readable map identity\n", name ? name : "");
        return false;
    }
    return true;
}

static void SV_SaveGame_f(void) {
    PATHSTR path;

    if (Cmd_Argc() != 2) { fprintf(stderr, "usage: save <name>\n"); return; }
    if (sv.state != ss_game || !SV_SavePath(Cmd_Argv(1), path)) return;
    if (!ge || !ge->SaveGame || !ge->SaveGame(path)) fprintf(stderr, "save: failed to write %s\n", path);
}

bool SV_LoadGame(cstring_t name, cstring_t map) {
    PATHSTR path;
    if (!name || !map || !*map || !SV_SavePath(name, path)) return false;
    /* Q2 SpawnEntities then SV_CheckForSavegame: ClearWorld + ReadLevel before
     * reconnect/begin. JASS main() must run during SV_Map before ReadGame. */
    SV_Map(map);
    if (sv.state != ss_game) return false;
    if (!ge || !ge->LoadGame || !ge->LoadGame(path)) {
        fprintf(stderr, "load: failed to read %s; restoring map baseline\n", path);
        SV_Map(map);
        return false;
    }
    return true;
}
#endif

void SV_CreateBaseline(void) {
    sv.baselines = MemAlloc(sizeof(entityState_t) * ge->max_edicts);
    memset(sv.baselines, 0, sizeof(entityState_t) * ge->max_edicts);
    FOR_LOOP(entnum, ge->num_edicts) {
        edict_t *svent = EDICT_NUM(entnum);
        /* Entities hidden from signon are not sent in baseline pages. Keep
         * their server baseline empty too, so a later snapshot add is encoded
         * against the empty baseline the client actually has. */
        if (!(svent->svflags & SVF_NOCLIENT)) {
            sv.baselines[entnum] = svent->s;
        }
        svent->s.number = entnum;
    }
}

static void SV_InitMulticast(void) {
    if (sv.multicast.maxsize == 0) {
        SZ_Init(&sv.multicast, sv.multicast_buf, MAX_MSGLEN);
    }
}

/* Remove released tail slots so num_clients remains the high-water bound for active and zombie slots. */
static void SV_TrimClientSlots(void) {
    while (svs.num_clients && svs.clients[svs.num_clients - 1].state == cs_free)
        svs.num_clients--;
}

/* Reclaim disconnected slots after Quake 2's short stale-packet grace period. */
void SV_ReapZombieClients(void) {
    FOR_LOOP(i, svs.num_clients) {
        client_t *cl = &svs.clients[i];
        if (cl->state >= cs_connected && Online_ConnectionLost(NS_SERVER, &cl->netchan.remote_address)) {
            fprintf(stderr, "SV_ReapZombieClients: Internet peer %u departed\n", i);
            SV_DropClient(cl);
        }
        if (cl->state != cs_zombie || svs.realtime - cl->drop_time < BZ_CLIENT_ZOMBIE_MSEC)
            continue;
        memset(cl, 0, sizeof(*cl));
    }
    SV_TrimClientSlots();
}

/* Reuse free holes before extending the client array's high-water bound. */
static client_t *SV_AllocClientSlot(uint32_t *clientnum) {
    uint32_t limit = MIN(ge->max_clients, MAX_CLIENTS);
    SV_ReapZombieClients();
    FOR_LOOP(i, svs.num_clients) {
        if (svs.clients[i].state == cs_free) {
            *clientnum = i;
            return &svs.clients[i];
        }
    }
    if (svs.num_clients >= limit)
        return NULL;
    *clientnum = svs.num_clients++;
    return &svs.clients[*clientnum];
}

/* Quake 2 SV_DropClient: tell the game a spawned player left, tell the peer, then keep a remote address
 * as a zombie briefly so stale datagrams are rejected. Loopback has no stale datagrams, and a lingering
 * zombie would push the next local connect off slot 0, so that slot is released at once. */
void SV_DropClient(client_t *cl) {
    uint32_t clientnum;
    if (!cl || cl < svs.clients || cl >= svs.clients + MAX_CLIENTS ||
        cl->state == cs_free || cl->state == cs_zombie)
        return;
    clientnum = (uint32_t)(cl - svs.clients); /* after the range check: the subtraction is undefined for a foreign pointer */
    if (cl->state == cs_spawned && cl->edict) ge->ClientDisconnect(cl->edict);
    MSG_WriteByte(&cl->netchan.message, svc_disconnect);
    Netchan_Transmit(NS_SERVER, &cl->netchan);
    cl->state = cs_zombie;
    cl->drop_time = svs.realtime;
    cl->edict = NULL;
    SV_LobbyRemoveClient(clientnum);
    if (cl->netchan.remote_address.type == NA_LOOPBACK) {
        memset(cl, 0, sizeof(*cl));
        SV_TrimClientSlots();
    }
}

static void SV_ClearLobbyClients(void) {
    FOR_LOOP(i, MAX_CLIENTS) {
        memset(&svs.clients[i], 0, sizeof(svs.clients[i]));
    }
    svs.num_clients = 0;
}

typedef struct {
    netadr_t addr;
    uint32_t clientnum;
    uint32_t playernum;
    uint32_t lobby_slot;
    char userinfo[256];
    UINAME name;
} savedLobbyClient_t;

static uint32_t SV_SaveLobbyClients(savedLobbyClient_t *saved, uint32_t max_saved) {
    uint32_t count = 0;

    if (sv.state != ss_lobby || !saved || max_saved == 0) {
        return 0;
    }
    FOR_LOOP(i, svs.num_clients) {
        client_t *cl = &svs.clients[i];

        if (cl->state != cs_connected && cl->state != cs_spawned) {
            continue;
        }
        if (count >= max_saved) {
            break;
        }
        saved[count].addr = cl->netchan.remote_address;
        saved[count].clientnum = i;
        saved[count].playernum = cl->playernum;
        saved[count].lobby_slot = cl->lobby_slot;
        snprintf(saved[count].userinfo, sizeof(saved[count].userinfo), "%s", cl->userinfo);
        snprintf(saved[count].name, sizeof(saved[count].name), "%s", cl->name);
        count++;
    }
    return count;
}

static void SV_RestoreLobbyClients(savedLobbyClient_t const *saved, uint32_t count) {
    if (!saved || count == 0) {
        SV_ClientConnect();
        return;
    }
    uint32_t client_limit = MIN(ge->max_clients, MAX_CLIENTS);
    FOR_LOOP(i, count) {
        client_t *cl;
        uint32_t clientnum = saved[i].clientnum;

        if (clientnum >= client_limit) continue;
        cl = &svs.clients[clientnum];
        memset(cl, 0, sizeof(*cl));
        cl->state = cs_connected;
        cl->lastframe = (uint32_t)-1;
        cl->netchan.remote_address = saved[i].addr;
        cl->playernum = saved[i].playernum;
        cl->lobby_slot = saved[i].lobby_slot;
        snprintf(cl->userinfo, sizeof(cl->userinfo), "%s", saved[i].userinfo);
        snprintf(cl->name, sizeof(cl->name), "%s", saved[i].name);
        SZ_Init(&cl->netchan.message, cl->netchan.message_buf, MAX_MSGLEN);
        Netchan_OutOfBandPrint(NS_SERVER, saved[i].addr, "client_connect %d", BZ_PROTOCOL_VERSION);
        svs.num_clients = MAX(svs.num_clients, clientnum + 1);
    }
}

void SV_ClientConnect(void) {
    uint32_t clientnum;
    client_t *cl;

    SV_ReapZombieClients();
    // Reuse slot 0 if it already holds a loopback client (e.g. repeated SV_Map
    // calls without a full SV_Shutdown in between).
    if (svs.num_clients > 0 &&
        (svs.clients[0].state == cs_connected || svs.clients[0].state == cs_spawned) &&
        svs.clients[0].netchan.remote_address.type == NA_LOOPBACK) {
        netadr_t adr = { NA_LOOPBACK };
        svs.clients[0].lastframe = (uint32_t)-1;
        SV_InitMulticast();
        SV_LobbyAssignClient(0, true);
        Netchan_OutOfBandPrint(NS_SERVER, adr, "client_connect %d", BZ_PROTOCOL_VERSION);
        SV_LobbyBroadcastSetup();
        return;
    }
    cl = SV_AllocClientSlot(&clientnum);
    if (!cl) {
        fprintf(stderr, "SV_ClientConnect: server full\n");
        return;
    }
    memset(cl, 0, sizeof(*cl));
    cl->state = cs_connected;
    cl->lastframe = (uint32_t)-1;
    SV_LobbyClientInit(cl, NULL);
    SV_InitMulticast();
    // Local client uses the in-process loopback path
    memset(&cl->netchan.remote_address, 0, sizeof(cl->netchan.remote_address));
    cl->netchan.remote_address.type = NA_LOOPBACK;
    SZ_Init(&cl->netchan.message, cl->netchan.message_buf, MAX_MSGLEN);
    netadr_t adr = { NA_LOOPBACK };
    fprintf(stderr, "SV_ClientConnect: connected local client over loopback\n");
    SV_LobbyAssignClient(clientnum, true);
    Netchan_OutOfBandPrint(NS_SERVER, adr, "client_connect %d", BZ_PROTOCOL_VERSION);
    SV_LobbyBroadcastSetup();
}

/* Find the client slot whose netchan address matches from.  For loopback
 * addresses, slot 0 (the local client) is always returned. */
client_t *SV_FindClientByAddr(netadr_t const *from) {
    SV_ReapZombieClients();
    FOR_LOOP(i, svs.num_clients) {
        client_t *cl = &svs.clients[i];
        if (cl->state == cs_free) continue;
        if (NET_CompareAdr(from, &cl->netchan.remote_address)) return cl;
    }
    return NULL;
}

/* Register a new remote client that sent the first connection packet. */
void SV_DirectConnect(netadr_t const *from, cstring_t userinfo) {
    client_t *existing;
    client_t *cl;
    uint32_t clientnum;

    if (!from) return;
    SV_ReapZombieClients();
    /* A repeated request means the first reply was lost or a local map restart
     * pre-created this address. Re-send the idempotent handshake response so
     * the client cannot remain on the loading plaque waiting for `new`. */
    if ((existing = SV_FindClientByAddr(from))) {
        if (existing->state == cs_zombie) return;
        /* client_connect starts a new session on both ends: the client restarts its netchan when it reads the reply,
         * so the slot's must restart too, or each side drops the other's packets as stale. */
        Netchan_Reset(&existing->netchan);
        SV_ResetDeltaBase(existing);
        Netchan_OutOfBandPrint(NS_SERVER, existing->netchan.remote_address, "client_connect %d", BZ_PROTOCOL_VERSION);
        return;
    }
    if ((Cvar_Integer("online_mode", 0) && from->type != NA_EOS && from->type != NA_LOOPBACK) ||
        (from->type == NA_EOS && sv.state != ss_lobby)) {
        fprintf(stderr, "SV_DirectConnect: Internet games admit lobby members before match start\n");
        return;
    }
    cl = SV_AllocClientSlot(&clientnum);
    if (!cl) {
        fprintf(stderr, "SV_DirectConnect: server full\n");
        return;
    }
    memset(cl, 0, sizeof(*cl));
    cl->state = cs_connected;
    cl->lastframe = (uint32_t)-1;
    SV_LobbyClientInit(cl, userinfo);
    SV_InitMulticast();
    cl->netchan.remote_address = *from;
    SZ_Init(&cl->netchan.message, cl->netchan.message_buf, MAX_MSGLEN);
    if (sv.state == ss_lobby && !SV_LobbyAssignClient(clientnum, false)) {
        fprintf(stderr, "SV_DirectConnect: no open lobby slot for %s\n", NET_AdrToString(from));
        memset(cl, 0, sizeof(*cl));
        SV_TrimClientSlots();
        return;
    }
    Netchan_OutOfBandPrint(NS_SERVER, *from, "client_connect %d", BZ_PROTOCOL_VERSION);
    SV_LobbyBroadcastSetup();
}

/* Retain the authored layout for late joiners without a configstring-slot budget or lossy text rewrite. */
bool SV_BuildLoadingScreen(void) {
    if (sv.multicast.overflowed || sv.multicast.cursize < 8 ||
        sv.multicast.data[0] != svc_layout || sv.multicast.data[1] != LAYER_LOADING) {
        fprintf(stderr, "SV_BuildLoadingScreen: missing loading layout\n");
        return false;
    }
    SAFE_DELETE(sv.loading.data, MemFree);
    uLongf size = compressBound(sv.multicast.cursize - 1);
    SZ_Init(&sv.loading, MemAlloc(size), size);
    int err = compress2(sv.loading.data, &size, sv.multicast.data + 1, sv.multicast.cursize - 1, Z_BEST_COMPRESSION);
    if (err != Z_OK || size > MAX_MSGLEN) {
        fprintf(stderr, "SV_BuildLoadingScreen: layout exceeds message limit or compression failed (%d)\n", err);
        SAFE_DELETE(sv.loading.data, MemFree);
        sv.loading.cursize = 0;
        return false;
    }
    sv.loading.cursize = size;
    /* Retain only resource boundaries; later clients use the same authoritative configstring table. */
    FOR_LOOP(i, sizeof(loading_pools) / sizeof(*loading_pools)) {
        sv.loading_end[i] = 1;
        while (sv.loading_end[i] < loading_pools[i].count && *sv.configstrings[loading_pools[i].base + sv.loading_end[i]])
            sv.loading_end[i]++;
    }
    SZ_Clear(&sv.multicast);
    return true;
}

/* Dependencies precede the screen; explicit chunks keep each UDP signon packet within its MTU budget. */
void SV_SendLoadingScreen(client_t *cl) {
    if (!sv.loading.cursize) {
        Com_Error(ERR_DROP, "Missing initial loading presentation");
        return;
    }
    if (cl->netchan.message.cursize) Netchan_Transmit(NS_SERVER, &cl->netchan);
    SV_WriteConfigString(&cl->netchan.message, CS_WORLD);
    SV_WriteConfigString(&cl->netchan.message, CS_ASSET_SCOPE);
    SV_WriteConfigString(&cl->netchan.message, CS_MAXCLIENTS);
    FOR_LOOP(i, sizeof(loading_pools) / sizeof(*loading_pools))
        for (uint32_t j = 1; j < sv.loading_end[i]; j++) {
            uint32_t index = loading_pools[i].base + j;
            if (cl->netchan.message.cursize + SV_ConfigStringWireSize(index) > SV_SignonLimit(&cl->netchan))
                Netchan_Transmit(NS_SERVER, &cl->netchan);
            SV_WriteConfigString(&cl->netchan.message, index);
        }
    for (uint32_t pos = 0; pos < sv.loading.cursize;) {
        /* The loopback reader requires packets strictly smaller than MAX_MSGLEN. */
        uint32_t limit = MIN(SV_SignonLimit(&cl->netchan), MAX_MSGLEN - 1);
        if (cl->netchan.message.cursize + BZ_LOADING_HEADER_SIZE >= limit)
            Netchan_Transmit(NS_SERVER, &cl->netchan);
        uint32_t size = MIN(sv.loading.cursize - pos, limit - cl->netchan.message.cursize - BZ_LOADING_HEADER_SIZE);
        MSG_WriteByte(&cl->netchan.message, svc_loading_screen);
        MSG_WriteLong(&cl->netchan.message, sv.loading.cursize);
        MSG_WriteLong(&cl->netchan.message, pos);
        MSG_WriteLong(&cl->netchan.message, size);
        MSG_Write(&cl->netchan.message, sv.loading.data + pos, size);
        pos += size;
        Netchan_Transmit(NS_SERVER, &cl->netchan);
    }
}

void SV_Map(cstring_t mapFilename) {
    savedLobbyClient_t lobby_clients[MAX_CLIENTS];
    uint32_t num_lobby_clients;
    bool had_lobby;

    fprintf(stderr, "Server initialization (loopback/local map).\n");
    had_lobby = sv.state == ss_lobby && svs.lobby.active;
    num_lobby_clients = SV_SaveLobbyClients(lobby_clients, MAX_CLIENTS);
    SV_ClearLobbyClients();
    SV_InitGame();
    SAFE_DELETE(sv.loading.data, MemFree);
    SAFE_DELETE(sv.baselines, MemFree);
    memset(&sv, 0, sizeof(struct server));
    Online_CloseAdmission();
    sv.state = ss_loading;
    strlcpy(sv.configstrings[CS_WORLD], mapFilename, sizeof(sv.configstrings[CS_WORLD]));
    SZ_Init(&sv.multicast, sv.multicast_buf, MAX_MSGLEN);
    SV_SetConfigString(CS_MAXCLIENTS, "", 1);
    SV_RestoreLobbyClients(lobby_clients, num_lobby_clients);
    /* Loading resources must be indexed and presented before synchronous world loading, not after it. */
    if (!ge->PrepareMap(mapFilename)) {
        fprintf(stderr, "SV_Map: loading presentation failed for %s\n", mapFilename);
        SV_Shutdown();
        CL_LoadingFrame();
        return;
    }
    if (!SV_BuildLoadingScreen()) { SV_Shutdown(); CL_LoadingFrame(); return; }
    FOR_LOOP(i, svs.num_clients) {
        client_t *cl = &svs.clients[i];
        /* num_clients spans reusable holes after disconnects; send only to live peers. */
        if (cl->state == cs_connected || cl->state == cs_spawned) SV_SendLoadingScreen(cl);
    }
    CL_LoadingFrame();
    if (!ge->LoadMap(mapFilename)) {
        fprintf(stderr, "SV_Map: map load failed\n");
        SV_Shutdown();
        CL_LoadingFrame();
        return;
    }
    SV_SetMapConfigStrings();
    if (!had_lobby) {
        memset(&svs.lobby, 0, sizeof(svs.lobby));
    }
    SV_CreateBaseline();
//    SV_LoadModels(); // model animation data is loaded lazily by game modules now
    sv.next_frame_msec = svs.realtime;
    sv.state = ss_game;
    /* Q2 defers at the map transition: loading-screen callbacks can repeat and must not replace this tail. */
    if (!Cvar_Integer("dedicated", 0)) Cbuf_CopyToDefer();
    // Clients retain the loading-media indices established before LoadMap.
    fprintf(stderr, "Server initialized.\n\n");
}

void SV_StartLobby(cstring_t mapFilename) {
    if (!mapFilename || !mapFilename[0]) {
        return;
    }
    if (sv.state == ss_lobby && !strcmp(sv.configstrings[CS_WORLD], mapFilename)) {
        return;
    }
    fprintf(stderr, "SV_StartLobby: preparing lobby for %s\n", mapFilename);
    if (!Cvar_Integer("online_mode", 0) && !SV_EnsureServerPort()) {
        return;
    }
    if (!svs.initialized) {
        SV_InitGame();
        if (!svs.initialized) {
            return;
        }
    }
    SAFE_DELETE(sv.loading.data, MemFree);
    SAFE_DELETE(sv.baselines, MemFree);
    SV_ClearLobbyClients();
    memset(&sv, 0, sizeof(struct server));
    sv.state = ss_lobby;
    snprintf(sv.configstrings[CS_WORLD], sizeof(sv.configstrings[CS_WORLD]), "%s", mapFilename);
    SV_LobbyInit(mapFilename);
    SV_InitMulticast();
    SV_ClientConnect();
    fprintf(stderr, "Lobby initialized for %s\n", mapFilename);
}

#ifndef TOOL_COMMON_NO_MPQ
static void SV_StartLobby_f(void) {
    if (Cmd_Argc() < 2) {
        fprintf(stderr, "usage: lobby_start <map>\n");
        return;
    }
    SV_StartLobby(Cmd_ArgsFrom(1));
}

#endif

void SV_InitGame(void) {
    if (!ge) {
        SV_InitGameProgs();
    }

    if (svs.initialized) {
        return;
    }

    if (!ge->edicts) {
        if (!ge->Init) {
            fprintf(stderr, "SV_InitGame: missing ge->Init callback\n");
            return;
        }
        ge->Init();
    }

    /* Q2 never reads a dead server's queue, so a local client's farewell can outlive SV_Shutdown.
     * Q2 consumes it before the next handshake allocates a slot; our local client is admitted
     * directly into slot 0, so drop the dead session's loopback datagrams at the boundary. */
    NET_ClearLoopPackets(NS_SERVER);
    svs.initialized = true;
    svs.num_client_entities = ge->max_clients * MAX_PACKET_ENTITIES * UPDATE_BACKUP;
    svs.client_entities = MemAlloc(sizeof(entityState_t) * svs.num_client_entities);
    
    FOR_LOOP(i, ge->max_clients) {
        edict_t *ent = EDICT_NUM(i);
        ent->s.number = i;
//        svs.clients[i].edict = ent;
    }
}

void SV_Shutdown(void) {
    Cbuf_ClearDefer();
    if (!svs.initialized) {
        return;
    }
    FOR_LOOP(i, svs.num_clients) {
        client_t *client = &svs.clients[i];
        if (client->state == cs_free || client->state == cs_zombie) {
            continue;
        }
        MSG_WriteByte(&client->netchan.message, svc_disconnect);
        Netchan_Transmit(NS_SERVER, &client->netchan);
    }
    if (Online_IsHost()) Online_Leave();
    SAFE_DELETE(sv.loading.data, MemFree);
    SAFE_DELETE(sv.baselines, MemFree);
    sv.state = ss_dead;
    SAFE_DELETE(svs.client_entities, MemFree);
    svs.num_clients = 0;
    memset(&svs.lobby, 0, sizeof(svs.lobby));
    svs.initialized = false;
    if (ge && ge->Shutdown) {
        ge->Shutdown();
    }
}

void SV_Init(void) {
    memset(&svs, 0, sizeof(struct server_static));
    memset(&sv, 0, sizeof(struct server));

#ifndef TOOL_COMMON_NO_MPQ
    Cmd_AddCommand("lobby_start", SV_StartLobby_f);
    Cmd_AddCommand("save", SV_SaveGame_f);
    SV_LobbyAddCommands();
#endif
}
