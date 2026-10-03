/* Opt-in engine acceptance: real WC3 game, menu, client and renderer. No retail assets. */
#include "server/server.h"
#include "client/client.h"

static lobbyState_t online_live_lobby;
static void (*online_live_lobby_update)(lobbyState_t const *);
static bool online_live_chat_host, online_live_chat_guest, online_live_chat_own;

static void Online_LiveChat(cstring_t text, bool own) {
    if (!strcmp(text, "Acceptance host: host-ready") && own == online_live_chat_own) online_live_chat_host = true;
    if (!strcmp(text, "Acceptance guest: guest-ready") && own != online_live_chat_own) online_live_chat_guest = true;
}

static void Online_LiveLobbyUpdate(lobbyState_t const *state) {
    online_live_lobby = *state;
    if (online_live_lobby_update) online_live_lobby_update(state);
}

static void Online_LiveGameFrame(uint32_t msec) {
    if (online_live_server) SV_Frame(msec);
    CL_Frame(msec);
}

static void Online_LiveCommand(cstring_t command) {
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteString(&cls.netchan.message, command);
}

static bool Online_LiveGameRun(cstring_t role, cstring_t room) {
    bool local = !strcmp(role, "local"), host = strstr(role, "guest") == NULL;
    bool crash = !strcmp(role, "game-crash-host");
    Cvar_Set("dedicated", "0");
    /* Main-menu initialization intentionally leaves Internet play. Complete
     * that phase before enabling the online lobby, as the normal UI does. */
    Cvar_Set("online_mode", "0");
    Cvar_Set("vid_hidden", "1"); Cvar_Set("s_sound", "0");
    Cvar_Set("ui_skip_transitions", "1");
    Cvar_Set("cl_camera_edge_scroll", "0");
    Cvar_Set("name", host ? "Acceptance host" : "Acceptance guest");
    CL_Init();
    Cbuf_Execute();
    Cvar_Set("online_mode", local ? "0" : "1");
    online_live_lobby_update = menu.UpdateLobbySetup;
    menu.UpdateLobbySetup = Online_LiveLobbyUpdate;
    online_live_chat_own = host; cl_test_lobby_chat = Online_LiveChat;
    online_live_game = true; online_live_server = host;
    if (local) {
        SV_Map("Maps/Transport.w3m");
        CL_Connect("localhost", 0);
    } else if (host) {
        SV_StartLobby("Maps/Transport.w3m");
        SV_LobbySetConfig(2, 2, room);
        for (uint32_t i = 0; i < 2; i++) {
            lobbySlot_t slot = { .visible = true, .type = i ? LOBBY_SLOT_OPEN : LOBBY_SLOT_HUMAN,
                .map_player = i, .race = 1, .team = 0, .color = i };
            SV_LobbySetSlot(i, &slot);
        }
        CL_Connect("localhost", 0);
        while (!Online_HostReady()) if (!Online_LivePump()) return false;
    } else {
        char bucket[sizeof(online.bucket)];
        strlcpy(bucket, online.bucket, sizeof(bucket));
        strlcpy(online.bucket, "OpenRealm-incompatible-protocol", sizeof(online.bucket));
        if (!Online_LiveFind(room, false)) return false;
        strlcpy(online.bucket, bucket, sizeof(online.bucket));
        if (!Online_LiveFind(room, true)) return false;
        uint32_t index;
        for (index = 0; index < online.num_games; index++)
            if (!strcmp(online.games[index].game.hostname, room)) break;
        if (index == online.num_games) return false;
        netadr_t owner;
        if (!NET_StringToAdr(online.games[index].game.address, 0, &owner) ||
            EOS_ProductUserId_FromString(owner.peer) == online.user) return false;
        /* Exercise the production admission check against a mismatching CRC
         * before restoring the actual public map metadata and joining. */
        online.games[index].crc ^= 1;
        Online_Join(index);
        bool rejected = !online.operation_pending && !online.lobby[0] &&
            !strcmp(Online_Status(), "The installed map differs from the host's map.");
        online.games[index].crc ^= 1;
        if (!rejected) return false;
        fprintf(stderr, "EOS gameplay: incompatible discovery bucket and mismatching map CRC rejected\n");
        Online_Join(index);
    }
    if (!local) {
        while (online_live_lobby.slot_count != 2 || !online_live_lobby.slots[1].occupied) {
            if (!Online_LivePump()) return false;
        }
        if (online_live_lobby.local_slot != (host ? 0u : 1u) ||
            online_live_lobby.game_speed != 2 || !online_live_lobby.slots[0].occupied ||
            strcmp(online_live_lobby.slots[0].name, "Acceptance host") ||
            strcmp(online_live_lobby.slots[1].name, "Acceptance guest") ||
            online_live_lobby.slots[0].color != 0 || online_live_lobby.slots[1].color != 1) return false;
        for (uint32_t i = 0; i < 2; i++)
            if (online_live_lobby.slots[i].race != 1 || online_live_lobby.slots[i].team != 0 ||
                online_live_lobby.slots[i].type != LOBBY_SLOT_HUMAN) return false;
        fprintf(stderr, "EOS gameplay: received authoritative two-player lobby and distinct slots/names/colors\n");
        Online_LiveCommand(host ? "lobby_say host-ready" : "lobby_say guest-ready");
        while (!online_live_chat_host || !online_live_chat_guest) if (!Online_LivePump()) return false;
        fprintf(stderr, "EOS gameplay: both lobby chat messages decoded with correct sender ownership\n");
        if (host) {
            if (!Online_LivePublished() || !Online_LiveFind(room, false)) return false;
            fprintf(stderr, "EOS gameplay: full room excluded from public browser\n");
            /* Allow the remote setup packet to be consumed before the map transition. */
            uint32_t wait = SDL_GetTicks();
            while (SDL_GetTicks() - wait < 1000) if (!Online_LivePump()) return false;
            SV_Map("Maps/Transport.w3m");
        }
    }
    while (cls.state != ca_active) {
        if (!Online_LivePump() || (host && sv.state == ss_dead)) return false;
    }
    if (cl.playerstate.client_ui_state != CLIENT_UI_GAME || !cl.refresh_prepped ||
        strcmp(cl.configstrings[CS_WORLD], "Maps/Transport.w3m") || !CM_GetMapChecksum()) return false;
    if (!local) {
        if (host) while (svs.clients[1].state != cs_spawned) if (!Online_LivePump()) return false;
        onlinePeer_t *peer = Online_Peer(host ? EOS_ProductUserId_FromString(svs.clients[1].netchan.remote_address.peer) : online.owner,
            host ? NS_SERVER : NS_CLIENT);
        if (!peer || (peer->network_type != EOS_NCT_DirectConnection && peer->network_type != EOS_NCT_RelayedConnection) ||
            (Cvar_Integer("online_force_relay", 0) && peer->network_type != EOS_NCT_RelayedConnection)) return false;
        fprintf(stderr, "EOS gameplay: engine traffic uses %s\n", peer->network_type == EOS_NCT_RelayedConnection ? "relay" : "direct");
    }
    uint32_t first = cl.frame.serverframe, previous = first, frames = 0;
    uint32_t started = SDL_GetTicks(), last = started;
    uint32_t units[2] = {0};
    for (uint32_t i = 0; i < cl.num_active; i++) {
        entityState_t const *ent = &cl.ents[cl.active_entities[i]].current;
        if (ent->class_id == MAKEFOURCC('h', 'f', 'o', 'o') && ent->player < 2) units[ent->player] = ent->number;
    }
    if (!units[0] || !units[1]) {
        fprintf(stderr, "EOS gameplay: both authored units must reach the initial snapshot\n"); return false;
    }
    char select[64];
    snprintf(select, sizeof(select), "select %u", units[host ? 0 : 1]);
    Online_LiveCommand(select);
    Online_LiveCommand(host ? "smartpoint -768 512" : "smartpoint 768 512");
    Online_LiveCommand(host ? "camera move -384 128" : "camera move 384 128");
    while (frames < 100) {
        if (!Online_LivePump() || cls.state != ca_active) return false;
        if (cl.frame.serverframe < (int)previous) return false;
        if (cl.frame.serverframe > (int)previous) {
            frames++; previous = cl.frame.serverframe; last = SDL_GetTicks();
        }
        if (SDL_GetTicks() - last > 5000) return false;
    }
    if (fabsf(cl.playerstate.vieworigin.x - (host ? -384 : 384)) > 1 ||
        fabsf(cl.playerstate.vieworigin.y - 128) > 1) {
        fprintf(stderr, "EOS gameplay: camera round-trip failed: received %.1f %.1f, expected %d 128\n",
            cl.playerstate.vieworigin.x, cl.playerstate.vieworigin.y, host ? -384 : 384);
        return false;
    }
    for (uint32_t i = 0; i < (local ? 1u : 2u); i++) {
        entityState_t const *ent = &cl.ents[units[i]].current;
        if (fabsf(ent->origin.x - (i ? 768 : -768)) > 32 || fabsf(ent->origin.y - 512) > 32) {
            fprintf(stderr, "EOS gameplay: player %u Move command not reflected in shared snapshot: %.1f %.1f\n",
                i, ent->origin.x, ent->origin.y); return false;
        }
    }
    fprintf(stderr, "EOS gameplay: active sign-on, map checksum, camera command round-trip, %u fresh snapshots (%u -> %u) in %u ms\n",
        frames, first, previous, SDL_GetTicks() - started);
    fprintf(stderr, "EOS gameplay: player Move commands changed the shared unit snapshots\n");
    if (local) return true;
    if (host) {
        if (!strcmp(role, "game-survivor")) {
            started = SDL_GetTicks();
            while (svs.num_clients > 1 || svs.lobby.slots[1].occupied) {
                if (!Online_LivePump() || cls.state != ca_active || !Online_InLobby()) return false;
            }
            if (cl.frame.serverframe <= (int)previous || svs.lobby.slots[1].type != LOBBY_SLOT_OPEN) return false;
            fprintf(stderr, "EOS gameplay: guest crash freed server/lobby slot after %u ms; host snapshots continue\n", SDL_GetTicks() - started);
            SV_Shutdown();
            return Online_LiveDeparture();
        }
        /* The guest signals verification through the production game command;
         * observe its resulting authoritative player state before departure. */
        while (!svs.clients[1].edict || fabsf(svs.clients[1].edict->client->ps.vieworigin.x - 512) > 1) {
            if (!Online_LivePump()) return false;
        }
        if (crash) {
            fprintf(stderr, "EOS gameplay: exiting host without engine/SDK teardown\n"); fflush(stderr); _Exit(0);
        }
        SV_Shutdown();
        return Online_LiveDeparture();
    }
    if (!strcmp(role, "game-crash-guest")) {
        fprintf(stderr, "EOS gameplay: exiting guest without engine/SDK teardown\n"); fflush(stderr); _Exit(0);
    }
    Online_LiveCommand("camera move 512 128");
    started = SDL_GetTicks();
    while (cls.state != ca_disconnected) if (!Online_LivePump()) return false;
    /* Flush the queued menu restoration and prove the actual client UI recovered. */
    if (!Online_LivePump() || !CL_MenuActive()) return false;
    fprintf(stderr, "EOS gameplay: client returned to menu after host loss in %u ms\n", SDL_GetTicks() - started);
    return Online_LiveDeparture() && Online_LiveLogin() && Online_LiveFind(room, false);
}
