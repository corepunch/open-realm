#include "server.h"
#include <arpa/inet.h>

static BOOL SV_EnsureServerPort(void) {
    NET_ConfigSource(NS_SERVER, true);
    if (!NET_IsConfigured(NS_SERVER)) {
        fprintf(stderr, "SV_EnsureServerPort: failed to bind UDP server port\n");
        return false;
    }
    return true;
}

void SV_CreateBaseline(void) {
    sv.baselines = MemAlloc(sizeof(entityState_t) * ge->max_edicts);
    memset(sv.baselines, 0, sizeof(entityState_t) * ge->max_edicts);
    FOR_LOOP(entnum, ge->num_edicts) {
        edict_t *svent = EDICT_NUM(entnum);
        sv.baselines[entnum] = svent->s;
        svent->s.number = entnum;
    }
}

static void SV_InitMulticast(void) {
    if (sv.multicast.maxsize == 0) {
        SZ_Init(&sv.multicast, sv.multicast_buf, MAX_MSGLEN);
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
    DWORD playernum;
    DWORD lobby_slot;
    char userinfo[256];
    UINAME name;
} savedLobbyClient_t;

static DWORD SV_SaveLobbyClients(savedLobbyClient_t *saved, DWORD max_saved) {
    DWORD count = 0;

    if (sv.state != ss_lobby || !saved || max_saved == 0) {
        return 0;
    }
    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT cl = &svs.clients[i];

        if (cl->state != cs_connected && cl->state != cs_spawned) {
            continue;
        }
        if (count >= max_saved) {
            break;
        }
        saved[count].addr = cl->netchan.remote_address;
        saved[count].playernum = cl->playernum;
        saved[count].lobby_slot = cl->lobby_slot;
        snprintf(saved[count].userinfo, sizeof(saved[count].userinfo), "%s", cl->userinfo);
        snprintf(saved[count].name, sizeof(saved[count].name), "%s", cl->name);
        count++;
    }
    return count;
}

static void SV_RestoreLobbyClients(savedLobbyClient_t const *saved, DWORD count) {
    if (!saved || count == 0) {
        SV_ClientConnect();
        return;
    }
    FOR_LOOP(i, count) {
        LPCLIENT cl;

        if (svs.num_clients >= MAX_CLIENTS ||
            svs.num_clients >= ge->max_clients) {
            break;
        }
        cl = &svs.clients[svs.num_clients++];
        memset(cl, 0, sizeof(*cl));
        cl->state = cs_connected;
        cl->lastframe = (DWORD)-1;
        cl->netchan.remote_address = saved[i].addr;
        cl->playernum = saved[i].playernum;
        cl->lobby_slot = saved[i].lobby_slot;
        snprintf(cl->userinfo, sizeof(cl->userinfo), "%s", saved[i].userinfo);
        snprintf(cl->name, sizeof(cl->name), "%s", saved[i].name);
        SZ_Init(&cl->netchan.message, cl->netchan.message_buf, MAX_MSGLEN);
        Netchan_OutOfBandPrint(NS_SERVER, saved[i].addr, "client_connect");
    }
}

void SV_ClientConnect(void) {
    // Reuse slot 0 if it already holds a loopback client (e.g. repeated SV_Map
    // calls without a full SV_Shutdown in between).
    if (svs.num_clients > 0 &&
        svs.clients[0].netchan.remote_address.type == NA_LOOPBACK) {
        netadr_t adr = { NA_LOOPBACK };
        svs.clients[0].lastframe = (DWORD)-1;
        SV_InitMulticast();
        SV_LobbyAssignClient(0, true);
        Netchan_OutOfBandPrint(NS_SERVER, adr, "client_connect");
        SV_LobbyBroadcastSetup();
        return;
    }
    if (svs.num_clients >= MAX_CLIENTS ||
        svs.num_clients >= ge->max_clients) {
        fprintf(stderr, "SV_ClientConnect: server full\n");
        return;
    }
    LPCLIENT cl = &svs.clients[svs.num_clients];
    svs.num_clients++;
    memset(cl, 0, sizeof(*cl));
    cl->state = cs_connected;
    cl->lastframe = (DWORD)-1;
    SV_LobbyClientInit(cl, NULL);
    SV_InitMulticast();
    // Local client uses the in-process loopback path
    memset(&cl->netchan.remote_address, 0, sizeof(cl->netchan.remote_address));
    cl->netchan.remote_address.type = NA_LOOPBACK;
    SZ_Init(&cl->netchan.message, cl->netchan.message_buf, MAX_MSGLEN);
    netadr_t adr = { NA_LOOPBACK };
    fprintf(stderr, "SV_ClientConnect: connected local client over loopback\n");
    SV_LobbyAssignClient(0, true);
    Netchan_OutOfBandPrint(NS_SERVER, adr, "client_connect");
    SV_LobbyBroadcastSetup();
}

/* Find the client slot whose netchan address matches from.  For loopback
 * addresses, slot 0 (the local client) is always returned. */
LPCLIENT SV_FindClientByAddr(const netadr_t *from) {
    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT cl = &svs.clients[i];
        if (from->type == NA_LOOPBACK &&
            cl->netchan.remote_address.type == NA_LOOPBACK)
            return cl;
        if (from->type == NA_IP &&
            cl->netchan.remote_address.type == NA_IP &&
            memcmp(cl->netchan.remote_address.ip, from->ip, 4) == 0 &&
            cl->netchan.remote_address.port == from->port)
            return cl;
    }
    return NULL;
}

/* Register a new remote client that sent the first connection packet. */
void SV_DirectConnect(const netadr_t *from, LPCSTR userinfo) {
    // Ignore if address is already registered
    if (!from || SV_FindClientByAddr(from))
        return;
    if (svs.num_clients >= MAX_CLIENTS ||
        svs.num_clients >= ge->max_clients) {
        fprintf(stderr, "SV_DirectConnect: server full\n");
        return;
    }
    LPCLIENT cl = &svs.clients[svs.num_clients];
    DWORD clientnum = svs.num_clients;
    svs.num_clients++;
    memset(cl, 0, sizeof(*cl));
    cl->state = cs_connected;
    cl->lastframe = (DWORD)-1;
    SV_LobbyClientInit(cl, userinfo);
    SV_InitMulticast();
    cl->netchan.remote_address = *from;
    SZ_Init(&cl->netchan.message, cl->netchan.message_buf, MAX_MSGLEN);
    if (sv.state == ss_lobby && !SV_LobbyAssignClient(clientnum, false)) {
        fprintf(stderr, "SV_DirectConnect: no open lobby slot for %s\n", NET_AdrToString(from));
        memset(cl, 0, sizeof(*cl));
        svs.num_clients--;
        return;
    }
    Netchan_OutOfBandPrint(NS_SERVER, *from, "client_connect");
    SV_LobbyBroadcastSetup();
}

void SV_Map(LPCSTR mapFilename) {
    savedLobbyClient_t lobby_clients[MAX_CLIENTS];
    DWORD num_lobby_clients;
    BOOL had_lobby;

    fprintf(stderr, "Server initialization (loopback/local map).\n");
    had_lobby = sv.state == ss_lobby && svs.lobby.active;
    num_lobby_clients = SV_SaveLobbyClients(lobby_clients, MAX_CLIENTS);
    SV_ClearLobbyClients();
    SV_InitGame();
    memset(&sv, 0, sizeof(struct server));
    sv.state = ss_loading;
    strlcpy(sv.configstrings[CS_WORLD], mapFilename, sizeof(sv.configstrings[CS_WORLD]));
    SZ_Init(&sv.multicast, sv.multicast_buf, MAX_MSGLEN);
    if (!ge->LoadMap(mapFilename)) {
        fprintf(stderr, "SV_Map: map load failed\n");
        sv.state = ss_dead;
        return;
    }
    if (!had_lobby) {
        memset(&svs.lobby, 0, sizeof(svs.lobby));
    }
    SV_CreateBaseline();
//    SV_LoadModels(); // model animation data is loaded lazily by game modules now
    sv.state = ss_game;
    // Keep lobby clients connected through the immediate game start.
    SV_RestoreLobbyClients(lobby_clients, num_lobby_clients);
    fprintf(stderr, "Server initialized.\n\n");
}

void SV_StartLobby(LPCSTR mapFilename) {
    if (!mapFilename || !mapFilename[0]) {
        return;
    }
    if (sv.state == ss_lobby && !strcmp(sv.configstrings[CS_WORLD], mapFilename)) {
        return;
    }
    fprintf(stderr, "SV_StartLobby: opening LAN server port for %s\n", mapFilename);
    if (!SV_EnsureServerPort()) {
        return;
    }
    if (!svs.initialized) {
        SV_InitGame();
        if (!svs.initialized) {
            return;
        }
    }
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

    svs.initialized = true;
    svs.num_client_entities = UPDATE_BACKUP * ge->max_clients * MAX_PACKET_ENTITIES;
    svs.client_entities = MemAlloc(sizeof(entityState_t) * svs.num_client_entities);
    
    FOR_LOOP(i, ge->max_clients) {
        edict_t *ent = EDICT_NUM(i);
        ent->s.number = i;
//        svs.clients[i].edict = ent;
    }
}

void SV_Shutdown(void) {
    if (!svs.initialized) {
        return;
    }
    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT client = &svs.clients[i];
        if (client->state == cs_free) {
            continue;
        }
        MSG_WriteByte(&client->netchan.message, svc_disconnect);
        Netchan_Transmit(NS_SERVER, &client->netchan);
    }
    SAFE_DELETE(sv.baselines, MemFree);
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
    SV_LobbyAddCommands();
#endif
}
