#include "server.h"
#include <stdlib.h>

static BOOL SV_LobbyEnsureTeams(LPMAPINFO info, DWORD num_teams) {
    mapTeam_t *teams;

    if (!info) {
        return false;
    }
    if (num_teams <= info->num_teams) {
        return true;
    }
    if (num_teams > MAX_PLAYERS) {
        num_teams = MAX_PLAYERS;
    }
    teams = MemAlloc(sizeof(*teams) * num_teams);
    if (!teams) {
        return false;
    }
    memset(teams, 0, sizeof(*teams) * num_teams);
    if (info->teams && info->num_teams > 0) {
        memcpy(teams, info->teams, sizeof(*teams) * info->num_teams);
        MemFree(info->teams);
    }
    info->teams = teams;
    info->num_teams = num_teams;
    return true;
}

static void SV_LobbyClearPlayerTeams(LPMAPINFO info, DWORD player) {
    if (!info || !info->teams || player >= MAX_PLAYERS) {
        return;
    }
    FOR_LOOP(i, info->num_teams) {
        info->teams[i].playerMasks &= ~(1u << player);
    }
}

static void SV_LobbyMovePlayerToTeam(LPMAPINFO info, DWORD player, DWORD team) {
    if (!info || player >= MAX_PLAYERS || team >= MAX_PLAYERS) {
        return;
    }
    if (!SV_LobbyEnsureTeams(info, team + 1)) {
        return;
    }
    SV_LobbyClearPlayerTeams(info, player);
    info->teams[team].playerMasks |= 1u << player;
}

static DWORD SV_LobbyMapPlayerTeam(LPCMAPINFO info, DWORD player) {
    if (!info || !info->teams || player >= MAX_PLAYERS) {
        return player;
    }
    FOR_LOOP(i, info->num_teams) {
        if (info->teams[i].playerMasks & (1u << player)) {
            return i;
        }
    }
    return player;
}

static LPCSTR SV_LobbyBaseName(LPCSTR path) {
    LPCSTR base = path;

    if (!path) {
        return "";
    }
    for (LPCSTR p = path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }
    return base;
}

static void SV_LobbySanitizeText(LPCSTR in, LPSTR out, DWORD out_size) {
    DWORD write = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    for (; *in && write + 1 < out_size; in++) {
        unsigned char c = (unsigned char)*in;

        if (c == '\\' || c == '"' || c == '\n' || c == '\r') {
            c = ' ';
        }
        if (c < 32) {
            continue;
        }
        out[write++] = (char)c;
    }
    out[write] = '\0';
}

static LPCSTR SV_UserinfoValue(LPCSTR userinfo, LPCSTR key, LPSTR out, DWORD out_size) {
    DWORD key_len;

    if (!out || out_size == 0) {
        return "";
    }
    out[0] = '\0';
    if (!userinfo || !key || !key[0]) {
        return out;
    }
    key_len = (DWORD)strlen(key);
    for (LPCSTR p = userinfo; *p;) {
        LPCSTR k;
        LPCSTR v;
        DWORD len = 0;

        if (*p == '\\') {
            p++;
        }
        k = p;
        while (*p && *p != '\\') {
            p++;
        }
        if ((DWORD)(p - k) != key_len || strncmp(k, key, key_len)) {
            if (*p == '\\') {
                p++;
            }
            while (*p && *p != '\\') {
                p++;
            }
            continue;
        }
        if (*p == '\\') {
            p++;
        }
        v = p;
        while (v[len] && v[len] != '\\' && len + 1 < out_size) {
            len++;
        }
        memcpy(out, v, len);
        out[len] = '\0';
        return out;
    }
    return out;
}

static playerType_t SV_LobbySlotPlayerType(lobbySlot_t const *slot) {
    if (!slot) {
        return kPlayerTypeNone;
    }
    if (slot->type == LOBBY_SLOT_COMPUTER) {
        return kPlayerTypeComputer;
    }
    if (slot->type == LOBBY_SLOT_HUMAN && slot->occupied) {
        return kPlayerTypeHuman;
    }
    return kPlayerTypeNone;
}

static LPCSTR SV_LobbySlotTypeName(lobbySlotType_t type) {
    switch (type) {
        case LOBBY_SLOT_OPEN: return "open";
        case LOBBY_SLOT_HUMAN: return "human";
        case LOBBY_SLOT_COMPUTER: return "computer";
        case LOBBY_SLOT_CLOSED: return "closed";
    }
    return "unknown";
}

static LPCSTR SV_PlayerTypeName(playerType_t type) {
    switch (type) {
        case kPlayerTypeNone: return "none";
        case kPlayerTypeHuman: return "human";
        case kPlayerTypeComputer: return "computer";
        case kPlayerTypeNeutral: return "neutral";
        case kPlayerTypeRescuable: return "rescuable";
    }
    return "unknown";
}

static void SV_LobbySendChat(LPCLIENT client, DWORD clientnum, DWORD sender_client, LPCSTR text) {
    BYTE payload_buf[MAX_MSGLEN];
    sizeBuf_t payload;

    if (!client || !text || !text[0]) {
        return;
    }
    if (client->state != cs_connected && client->state != cs_spawned) {
        return;
    }
    SZ_Init(&payload, payload_buf, sizeof(payload_buf));
    MSG_WriteByte(&payload, clientnum == sender_client ? 1 : 0);
    MSG_WriteString(&payload, text);
    SV_WriteGameCommand(&client->netchan.message, "lobby_chat", &payload);
    Netchan_Transmit(NS_SERVER, &client->netchan);
}

static void SV_LobbyClearClientAssignment(DWORD clientnum) {
    LPCLIENT cl;

    if (clientnum >= svs.num_clients) {
        return;
    }
    cl = &svs.clients[clientnum];
    cl->lobby_slot = MAX_PLAYERS;
    cl->playernum = MAX_PLAYERS;
}

static BOOL SV_LobbyAssignClientToSlot(DWORD clientnum, DWORD slotnum) {
    LPCLIENT cl;
    lobbySlot_t *slot;

    if (clientnum >= svs.num_clients || slotnum >= MAX_PLAYERS) {
        return false;
    }
    cl = &svs.clients[clientnum];
    slot = &svs.lobby.slots[slotnum];
    if (!slot->visible || slot->map_player >= MAX_PLAYERS) {
        return false;
    }
    slot->occupied = true;
    slot->client = clientnum;
    slot->type = LOBBY_SLOT_HUMAN;
    snprintf(slot->name, sizeof(slot->name), "%s", cl->name[0] ? cl->name : "Player");
    cl->lobby_slot = slotnum;
    cl->playernum = slot->map_player;
    return true;
}

static BOOL SV_LobbyClientAssigned(DWORD clientnum) {
    FOR_LOOP(i, svs.lobby.slot_count) {
        lobbySlot_t const *slot = &svs.lobby.slots[i];

        if (slot->visible && slot->occupied && slot->client == clientnum) {
            return true;
        }
    }
    return false;
}

void SV_LobbyClientInit(LPCLIENT cl, LPCSTR userinfo) {
    char raw_name[sizeof(cl->name)];

    if (!cl) {
        return;
    }
    cl->playernum = MAX_PLAYERS;
    cl->lobby_slot = MAX_PLAYERS;
    snprintf(cl->userinfo, sizeof(cl->userinfo), "%s", userinfo ? userinfo : "");
    SV_UserinfoValue(userinfo, "name", raw_name, sizeof(raw_name));
    if (!raw_name[0]) {
        snprintf(raw_name, sizeof(raw_name), "%s", Cvar_String("name", "Player"));
    }
    SV_LobbySanitizeText(raw_name, cl->name, sizeof(cl->name));
    if (!cl->name[0]) {
        snprintf(cl->name, sizeof(cl->name), "Player");
    }
}

void SV_LobbyInit(LPCSTR mapFilename) {
    memset(&svs.lobby, 0, sizeof(svs.lobby));
    svs.lobby.active = true;
    snprintf(svs.lobby.map_path, sizeof(svs.lobby.map_path), "%s", mapFilename ? mapFilename : "");
    snprintf(svs.lobby.map_name, sizeof(svs.lobby.map_name), "%s", SV_LobbyBaseName(mapFilename));
    svs.lobby.game_speed = (DWORD)Cvar_Integer("sv_game_speed", 2);
    svs.lobby.local_slot = MAX_PLAYERS;
}

BOOL SV_LobbyAssignClient(DWORD clientnum, BOOL host) {
    DWORD fallback = MAX_PLAYERS;

    if (!svs.lobby.active || clientnum >= svs.num_clients) {
        return true;
    }
    if (SV_LobbyClientAssigned(clientnum)) {
        return true;
    }
    FOR_LOOP(i, svs.lobby.slot_count) {
        lobbySlot_t const *slot = &svs.lobby.slots[i];

        if (!slot->visible || slot->occupied) {
            continue;
        }
        if (host && slot->type == LOBBY_SLOT_HUMAN) {
            return SV_LobbyAssignClientToSlot(clientnum, i);
        }
        if (slot->type == LOBBY_SLOT_OPEN && fallback == MAX_PLAYERS) {
            fallback = i;
        }
    }
    if (fallback != MAX_PLAYERS) {
        return SV_LobbyAssignClientToSlot(clientnum, fallback);
    }
    return false;
}

void SV_LobbyWriteSetup(LPCLIENT cl) {
    BYTE payload_buf[MAX_MSGLEN];
    sizeBuf_t payload;
    DWORD local_slot = cl ? cl->lobby_slot : MAX_PLAYERS;

    if (!cl) {
        return;
    }
    SZ_Init(&payload, payload_buf, sizeof(payload_buf));
    MSG_WriteString(&payload, svs.lobby.map_path);
    MSG_WriteString(&payload, svs.lobby.map_name);
    MSG_WriteByte(&payload, (int)svs.lobby.game_speed);
    MSG_WriteByte(&payload, (int)svs.lobby.slot_count);
    MSG_WriteByte(&payload, local_slot < MAX_PLAYERS ? (int)local_slot : 255);
    MSG_WriteLong(&payload, (int)svs.lobby.revision);
    FOR_LOOP(i, svs.lobby.slot_count) {
        lobbySlot_t const *slot = &svs.lobby.slots[i];

        MSG_WriteByte(&payload, slot->visible ? 1 : 0);
        MSG_WriteByte(&payload, slot->occupied ? 1 : 0);
        MSG_WriteByte(&payload, slot->visible && slot->client < MAX_CLIENTS ? (int)slot->client : 255);
        MSG_WriteByte(&payload, slot->visible && slot->map_player < MAX_PLAYERS ? (int)slot->map_player : 255);
        MSG_WriteByte(&payload, (int)slot->type);
        MSG_WriteByte(&payload, (int)slot->race);
        MSG_WriteByte(&payload, (int)slot->team);
        MSG_WriteByte(&payload, (int)slot->color);
        MSG_WriteString(&payload, slot->name);
    }
    SV_WriteGameCommand(&cl->netchan.message, "lobby_setup", &payload);
}

void SV_LobbyBroadcastSetup(void) {
    if (sv.state != ss_lobby || !svs.lobby.active) {
        return;
    }
    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT cl = &svs.clients[i];

        if (cl->state != cs_connected && cl->state != cs_spawned) {
            continue;
        }
        SV_LobbyWriteSetup(cl);
        Netchan_Transmit(NS_SERVER, &cl->netchan);
    }
}

void SV_LobbySetConfig(DWORD speed, DWORD slots, LPCSTR map_name) {
    if (!svs.lobby.active) {
        return;
    }
    if (slots > MAX_PLAYERS) {
        slots = MAX_PLAYERS;
    }
    svs.lobby.game_speed = speed;
    svs.lobby.slot_count = slots;
    SV_LobbySanitizeText(map_name && map_name[0] ? map_name : SV_LobbyBaseName(svs.lobby.map_path),
                         svs.lobby.map_name,
                         sizeof(svs.lobby.map_name));
    svs.lobby.revision++;
}

void SV_LobbySetSlot(DWORD slotnum, lobbySlot_t const *config) {
    lobbySlot_t old;
    lobbySlot_t *slot;

    if (!svs.lobby.active || !config || slotnum >= MAX_PLAYERS) {
        return;
    }
    if (slotnum >= svs.lobby.slot_count) {
        svs.lobby.slot_count = slotnum + 1;
    }
    slot = &svs.lobby.slots[slotnum];
    old = *slot;
    *slot = *config;
    slot->client = old.client;
    slot->occupied = old.occupied;
    if (slot->type != LOBBY_SLOT_HUMAN || !old.occupied) {
        slot->occupied = false;
        slot->client = MAX_CLIENTS;
        if (old.occupied) {
            SV_LobbyClearClientAssignment(old.client);
        }
    } else if (old.occupied && old.client < svs.num_clients) {
        LPCLIENT cl = &svs.clients[old.client];

        slot->map_player = config->map_player;
        cl->lobby_slot = slotnum;
        cl->playernum = slot->map_player;
        snprintf(slot->name, sizeof(slot->name), "%s", cl->name[0] ? cl->name : config->name);
    }
    if (slot->type == LOBBY_SLOT_HUMAN && !slot->occupied) {
        SV_LobbyAssignClient(0, true);
    }
    if (slot->type == LOBBY_SLOT_OPEN) {
        slot->occupied = false;
        slot->client = MAX_CLIENTS;
    }
    if (!slot->name[0]) {
        snprintf(slot->name, sizeof(slot->name), "%s",
                 slot->type == LOBBY_SLOT_COMPUTER ? "Computer" :
                 slot->type == LOBBY_SLOT_CLOSED ? "Closed" :
                 slot->type == LOBBY_SLOT_HUMAN ? "Player" : "Open");
    }
    svs.lobby.revision++;
}

void SV_ApplyLobbySettings(LPMAPINFO info) {
    if (!info) {
        return;
    }
    FOR_LOOP(i, MAX_PLAYERS) {
        info->players[i].color = i;
    }
    if (!svs.lobby.active || svs.lobby.slot_count == 0) {
        return;
    }
    FOR_LOOP(i, MAX_PLAYERS) {
        if (info->players[i].used &&
            info->players[i].playerType != kPlayerTypeNeutral &&
            info->players[i].playerType != kPlayerTypeRescuable) {
            info->players[i].playerType = kPlayerTypeNone;
            SV_LobbyClearPlayerTeams(info, i);
        }
    }
    FOR_LOOP(slotnum, svs.lobby.slot_count) {
        lobbySlot_t const *slot = &svs.lobby.slots[slotnum];
        mapPlayer_t *player;
        playerType_t type;

        if (!slot->visible || slot->map_player >= MAX_PLAYERS) {
            continue;
        }
        player = &info->players[slot->map_player];
        if (!player->used) {
            continue;
        }
        type = SV_LobbySlotPlayerType(slot);
        fprintf(stderr,
                "  lobby slot %u: visible=%d occupied=%d client=%u map_player=%u slot_type=%s applied_type=%s race=%u team=%u color=%u name=\"%s\"\n",
                (unsigned)slotnum,
                slot->visible ? 1 : 0,
                slot->occupied ? 1 : 0,
                (unsigned)slot->client,
                (unsigned)slot->map_player,
                SV_LobbySlotTypeName(slot->type),
                SV_PlayerTypeName(type),
                (unsigned)slot->race,
                (unsigned)slot->team,
                (unsigned)slot->color,
                slot->name);
        player->playerType = type;
        player->playerRace = slot->race;
        player->color = slot->color % MAX_CLIENTS;
        if (slot->name[0]) {
            SAFE_DELETE(player->playerName, MemFree);
            player->playerName = MemAlloc(strlen(slot->name) + 1);
            memcpy(player->playerName, slot->name, strlen(slot->name) + 1);
        }
        if (type == kPlayerTypeNone) {
            SV_LobbyClearPlayerTeams(info, slot->map_player);
        } else {
            SV_LobbyMovePlayerToTeam(info, slot->map_player, slot->team);
        }
    }
    FOR_LOOP(i, MAX_PLAYERS) {
        mapPlayer_t const *player = &info->players[i];

        if (!player->used && player->playerType == kPlayerTypeNone) {
            continue;
        }
        fprintf(stderr,
                "  map player %u: used=%d type=%s race=%u team=%u color=%u start=(%.1f %.1f) name=\"%s\"\n",
                (unsigned)i,
                player->used ? 1 : 0,
                SV_PlayerTypeName(player->playerType),
                (unsigned)player->playerRace,
                (unsigned)SV_LobbyMapPlayerTeam(info, i),
                (unsigned)player->color,
                player->startingPosition.x,
                player->startingPosition.y,
                player->playerName ? player->playerName : "");
    }
}

void SV_LobbyBroadcastChatFrom(DWORD sender_client, LPCSTR sender, LPCSTR text) {
    char clean_sender[64];
    char clean_text[256];
    char line[384];

    if (sv.state != ss_lobby || !text || !text[0]) {
        return;
    }
    SV_LobbySanitizeText(sender && sender[0] ? sender : "Player", clean_sender, sizeof(clean_sender));
    SV_LobbySanitizeText(text, clean_text, sizeof(clean_text));
    if (!clean_text[0]) {
        return;
    }
    snprintf(line, sizeof(line), "%s: %s", clean_sender[0] ? clean_sender : "Player", clean_text);
    FOR_LOOP(i, svs.num_clients) {
        SV_LobbySendChat(&svs.clients[i], i, sender_client, line);
    }
}

void SV_LobbyBroadcastChat(LPCSTR sender, LPCSTR text) {
    SV_LobbyBroadcastChatFrom(MAX_CLIENTS, sender, text);
}

#ifndef TOOL_COMMON_NO_MPQ
static void SV_LobbySay_f(void) {
    SV_LobbyBroadcastChatFrom(0, "Player", Cmd_ArgsFrom(1));
}

static void SV_LobbyConfig_f(void) {
    DWORD speed;
    DWORD slots;

    if (Cmd_Argc() < 4) {
        fprintf(stderr, "usage: lobby_config <speed> <slots> <map_name>\n");
        return;
    }
    speed = (DWORD)atoi(Cmd_Argv(1));
    slots = (DWORD)atoi(Cmd_Argv(2));
    SV_LobbySetConfig(speed, slots, Cmd_ArgsFrom(3));
    SV_LobbyBroadcastSetup();
}

static void SV_LobbySlot_f(void) {
    lobbySlot_t slot;
    DWORD slotnum;

    if (Cmd_Argc() < 9) {
        fprintf(stderr, "usage: lobby_slot <slot> <visible> <map_player> <type> <race> <team> <color> <name>\n");
        return;
    }
    memset(&slot, 0, sizeof(slot));
    slotnum = (DWORD)atoi(Cmd_Argv(1));
    slot.visible = atoi(Cmd_Argv(2)) != 0;
    slot.map_player = (DWORD)atoi(Cmd_Argv(3));
    slot.type = (lobbySlotType_t)atoi(Cmd_Argv(4));
    slot.race = (playerRace_t)atoi(Cmd_Argv(5));
    slot.team = (DWORD)atoi(Cmd_Argv(6));
    slot.color = (DWORD)atoi(Cmd_Argv(7));
    slot.client = MAX_CLIENTS;
    SV_LobbySanitizeText(Cmd_ArgsFrom(8), slot.name, sizeof(slot.name));
    SV_LobbySetSlot(slotnum, &slot);
    SV_LobbyBroadcastSetup();
}
#endif

void SV_LobbyAddCommands(void) {
#ifndef TOOL_COMMON_NO_MPQ
    Cmd_AddCommand("lobby_config", SV_LobbyConfig_f);
    Cmd_AddCommand("lobby_slot", SV_LobbySlot_f);
    Cmd_AddCommand("lobby_say", SV_LobbySay_f);
#endif
}
