/* Opt-in live check of the production adapter, included by online_eos.c.
 * Not a registered TEST: make test must never contact the live deployment. */
#include <SDL2/SDL.h>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#define ONLINE_LIVE_TIMEOUT 180000 // milliseconds; bounds service/indexing/crash checks including native SDK calls

static uint32_t online_live_start, online_live_tick;
static char online_live_status[256];

/* SDK entry points can block in native credential storage before any tick. */
static Uint32 SDLCALL Online_LiveWatchdog(Uint32 interval, void *context) {
    (void)interval; (void)context;
    fprintf(stderr, "EOS acceptance FAIL: 180-second process deadline exceeded (including native SDK calls).\n");
    fflush(stderr); _Exit(1);
}

/* Give native HTTP and the production adapter the same advancing wall clock. */
static bool Online_LivePump(void) {
#ifdef __APPLE__
    /* Dedicated SDL has no video event pump, but EOS native HTTP needs one. */
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, false);
#else
    SDL_Delay(5);
#endif
    uint32_t now = SDL_GetTicks();
    Online_Frame(now - online_live_tick); online_live_tick = now;
    if (strcmp(online_live_status, Online_Status())) {
        snprintf(online_live_status, sizeof(online_live_status), "%s", Online_Status());
        fprintf(stderr, "EOS acceptance %u ms: %s\n", now - online_live_start, online_live_status);
    }
    return now - online_live_start < ONLINE_LIVE_TIMEOUT;
}

static bool Online_LiveIdle(void) {
    return !online.operation_pending && !online.updating && !online.leave_pending;
}

/* Submission alone does not prove that leave/destroy reached the service. */
static bool Online_LiveDeparture(void) {
    Online_Leave();
    while (!Online_LiveIdle()) if (!Online_LivePump()) return false;
    return !online.lobby[0] && !online.num_peers && !online.departure_failed;
}

/* The first public update, not CreateLobby, grants host admission readiness. */
static bool Online_LivePublished(void) {
    while (!Online_HostReady()) {
        if (!Online_LivePump() || !Online_Ready() || !Online_IsHost()) return false;
    }
    return true;
}

/* Verify the same privacy transition used when the engine starts a match. */
static bool Online_LiveAdmission(void) {
    Online_CloseAdmission();
    while (online.host_dirty || online.updating) if (!Online_LivePump()) return false;
    EOS_HLobbyDetails details = Online_Details();
    if (!details) return false;
    EOS_LobbyDetails_Info *info = NULL;
    EOS_LobbyDetails_CopyInfoOptions copy = { .ApiVersion = EOS_LOBBYDETAILS_COPYINFO_API_LATEST };
    bool valid = Online_Result("verify admission", EOS_LobbyDetails_CopyInfo(details, &copy, &info));
    if (valid) {
        valid = info->PermissionLevel == EOS_LPL_INVITEONLY && !Online_HostReady();
        EOS_LobbyDetails_Info_Release(info);
    }
    EOS_LobbyDetails_Release(details);
    return valid;
}

/* Public indexing is asynchronous; failed searches cannot establish absence. */
static bool Online_LiveFind(cstring_t room, bool present) {
    for (;;) {
        while (!Online_LiveIdle()) if (!Online_LivePump()) return false;
        Online_Search();
        while (online.operation_pending) if (!Online_LivePump()) return false;
        /* A failed request cannot prove directory cleanup. */
        char expected[64];
        snprintf(expected, sizeof(expected), "%u Internet games found.", online.num_games);
        if (strcmp(Online_Status(), expected)) return false;
        bool found = false;
        for (uint32_t i = 0; i < online.num_games; i++)
            if (!strcmp(online.games[i].game.hostname, room)) found = true;
        if (found == present) return true;
        uint32_t wait = SDL_GetTicks();
        while (SDL_GetTicks() - wait < 5000) if (!Online_LivePump()) return false;
    }
}

/* Exercise real peer admission, channel routing, queues and full-size reassembly. */
static bool Online_LiveExchange(bool host, bool relay, netadr_t *remote, uint8_t *payload, uint8_t *received) {
    sizeBuf_t message = { .data = received, .maxsize = MAX_MSGLEN };
    NETSOURCE source = host ? NS_SERVER : NS_CLIENT;
    if (!host) NET_SendPacket(source, MAX_MSGLEN, payload, *remote);
    int size = 0;
    while (!size) {
        if (!Online_LivePump() || !online.lobby[0]) return false;
        size = NET_GetPacket(source, remote, &message);
    }
    if (size != MAX_MSGLEN || memcmp(payload, received, MAX_MSGLEN)) return false;
    EOS_ProductUserId user = EOS_ProductUserId_FromString(remote->peer);
    onlinePeer_t *peer = Online_Peer(user, source);
    if (!peer || (peer->network_type != EOS_NCT_DirectConnection && peer->network_type != EOS_NCT_RelayedConnection) ||
        (relay && peer->network_type != EOS_NCT_RelayedConnection)) return false;
    if (host) NET_SendPacket(source, MAX_MSGLEN, payload, *remote);
    fprintf(stderr, "EOS acceptance: verified %d-byte %s message over %s\n", MAX_MSGLEN,
        host ? "client-to-server" : "server-to-client",
        peer->network_type == EOS_NCT_RelayedConnection ? "relay" : "direct");
    return true;
}

static bool Online_LiveLogin(void) {
    if (!Online_Begin()) return false;
    while (!Online_Ready()) {
        if (!Online_LivePump() || (!online.ready && !online.authenticating)) return false;
    }
    return true;
}

/* Pair installations without replacing the adapter or registering a networked TEST. */
static bool Online_LiveRun(cstring_t role, cstring_t room, cstring_t map) {
    bool solo = !strcmp(role, "solo"), host = solo || !strcmp(role, "host") || !strcmp(role, "crash-host");
    bool relay = Cvar_Integer("online_force_relay", 0) != 0;
    if (!Online_LiveLogin()) return false;
    EOS_ProductUserId identity = online.user;
    EOS_ERelayControl policy;
    EOS_P2P_GetRelayControlOptions control = { .ApiVersion = EOS_P2P_GETRELAYCONTROL_API_LATEST };
    if (!Online_Result("read relay policy", EOS_P2P_GetRelayControl(online.p2p, &control, &policy)) ||
        policy != (relay ? EOS_RC_ForceRelays : EOS_RC_AllowRelays)) return false;

    if (host) {
        Online_Host(map, room, 1, 2, 1);
        if (!Online_LivePublished()) return false;
        if (solo) {
            if (!Online_LiveFind(room, true) || !Online_LiveAdmission() ||
                !Online_LiveDeparture() || !Online_LiveLogin()) return false;
            if (online.user != identity) return false;
            Online_Host(map, room, 1, 2, 2);
            return Online_LivePublished() && Online_LiveDeparture() && Online_LiveLogin() && Online_LiveFind(room, false);
        }
        /* The guest proves public discovery. Once it joins this two-member
         * room is full, so the browser correctly excludes it from searches. */
    } else {
        if (!Online_LiveFind(room, true)) return false;
        uint32_t index;
        for (index = 0; index < online.num_games; index++)
            if (!strcmp(online.games[index].game.hostname, room)) break;
        if (index == online.num_games) return false;
        netadr_t owner;
        if (!NET_StringToAdr(online.games[index].game.address, 0, &owner)) return false;
        if (EOS_ProductUserId_FromString(owner.peer) == online.user) {
            fprintf(stderr, "EOS acceptance requires a second guest identity on another installation/OS user.\n");
            return false;
        }
        Online_Join(index);
        while (online.operation_pending) if (!Online_LivePump()) return false;
        if (!online.lobby[0]) return false;
    }

    uint8_t *payload = malloc(MAX_MSGLEN), *received = malloc(MAX_MSGLEN);
    if (!payload || !received) { free(payload); free(received); return false; }
    for (uint32_t i = 0; i < MAX_MSGLEN; i++) payload[i] = (uint8_t)(i * 73);
    netadr_t remote = {0};
    bool valid = host || Online_TakeConnection(&remote);
    if (valid) valid = Online_LiveExchange(host, relay, &remote, payload, received);
    free(payload); free(received);
    if (!valid) return false;
    if (host) {
        uint8_t bytes[32]; sizeBuf_t ack = { .data = bytes, .maxsize = sizeof(bytes) };
        int size = 0;
        while (!size) if (!Online_LivePump()) return false; else size = NET_GetPacket(NS_SERVER, &remote, &ack);
        if (size != 4 || memcmp(bytes, "done", 4)) return false;
        if (!strcmp(role, "crash-host")) {
            fprintf(stderr, "EOS acceptance: exiting host without SDK teardown (simulate process crash).\n");
            fflush(stderr); _Exit(0);
        }
        return Online_LiveAdmission() && Online_LiveDeparture();
    }
    NET_SendPacket(NS_CLIENT, 4, "done", remote);
    uint32_t ended = SDL_GetTicks();
    while (online.lobby[0]) if (!Online_LivePump()) return false;
    fprintf(stderr, "EOS acceptance: host loss detected after %u ms\n", SDL_GetTicks() - ended);
    if (!Online_LiveDeparture() || !Online_LiveLogin() || !Online_LiveFind(room, false)) return false;
    fprintf(stderr, "EOS acceptance: public room gone after %u ms\n", SDL_GetTicks() - ended);
    return true;
}

/* This explicit diagnostic owns its process and always returns a bounded result. */
void Online_Acceptance_f(void) {
    cstring_t role = Cmd_Argv(1), room = Cmd_Argv(2);
    char map[sizeof(online.hosted.mapname)];
    cstring_t map_path = Cvar_String("online_acceptance_map", "Maps/Campaign/Human02.w3m");
    if (Cmd_Argc() != 3 || !room[0] || strlen(room) >= sizeof(online.hosted.hostname) ||
        strlen(map_path) >= sizeof(map) ||
        (strcmp(role, "solo") && strcmp(role, "host") && strcmp(role, "guest") && strcmp(role, "crash-host"))) {
        fprintf(stderr, "Usage: +online_acceptance <solo|host|guest|crash-host> <unique-room-name>\n");
        exit(1);
    }
    snprintf(map, sizeof(map), "%s", map_path);
    if (SDL_InitSubSystem(SDL_INIT_TIMER) != 0 || !SDL_AddTimer(ONLINE_LIVE_TIMEOUT, Online_LiveWatchdog, NULL)) {
        fprintf(stderr, "EOS acceptance cannot start deadline timer: %s\n", SDL_GetError());
        exit(1);
    }
    online_live_start = online_live_tick = SDL_GetTicks();
    online_live_status[0] = 0;
    bool valid = Online_LiveRun(role, room, map);
    /* Complete asynchronous departure before releasing SDK handles. */
    if (!Online_LiveDeparture()) valid = false;
    Online_Shutdown();
    fprintf(stderr, "EOS acceptance %s: %s (%u ms)\n", role, valid ? "PASS" : "FAIL", SDL_GetTicks() - online_live_start);
    exit(valid ? 0 : 1);
}
