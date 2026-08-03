#include "server.h"

#include "common/net_platform.h"
#include <stdlib.h>

static DWORD SV_LanPlayerCount(void) {
    DWORD count = 0;
    FOR_LOOP(i, svs.num_clients) {
        if (svs.clients[i].state == cs_connected || svs.clients[i].state == cs_spawned) {
            count++;
        }
    }
    return count;
}

static void SV_LanSanitizeValue(LPCSTR in, LPSTR out, size_t out_size) {
    size_t write = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }

    for (; *in && write + 1 < out_size; in++) {
        char c = *in;
        if (c == '\\') {
            c = '/';
        } else if (c == '\n' || c == '\r') {
            c = ' ';
        }
        out[write++] = c;
    }
    out[write] = '\0';
}

static void SV_LanInfo(const netadr_t *from) {
    char mapname[80];
    char hostname[80];
    char speed[16];
    char slots[16];

    if ((sv.state != ss_lobby && sv.state != ss_game) || !from) {
        if (from) {
            fprintf(stderr,
                    "SV_LanInfo: ignoring info query from %s while server state=%d\n",
                    NET_AdrToString(from),
                    sv.state);
        }
        return;
    }
    if (svs.lobby.active) {
        SV_LanSanitizeValue(svs.lobby.map_path, mapname, sizeof(mapname));
        SV_LanSanitizeValue(svs.lobby.map_name, hostname, sizeof(hostname));
        snprintf(speed, sizeof(speed), "%u", (unsigned)svs.lobby.game_speed);
        snprintf(slots, sizeof(slots), "%u", (unsigned)svs.lobby.slot_count);
    } else {
        SV_LanSanitizeValue(sv.configstrings[CS_WORLD], mapname, sizeof(mapname));
        SV_LanSanitizeValue(Cvar_String("sv_hostname", "OpenWarcraft3"), hostname, sizeof(hostname));
        SV_LanSanitizeValue(Cvar_String("sv_game_speed", "2"), speed, sizeof(speed));
        snprintf(slots, sizeof(slots), "%u", (unsigned)(ge ? ge->max_clients : MAX_CLIENTS));
    }
    Netchan_OutOfBandPrint(NS_SERVER,
                           *from,
                           "info\n\\hostname\\%s\\mapname\\%s\\players\\%u\\maxplayers\\%u\\speed\\%s\\slots\\%s",
                           hostname[0] ? hostname : "OpenWarcraft3",
                           mapname,
                           (unsigned)SV_LanPlayerCount(),
                           (unsigned)(ge ? ge->max_clients : MAX_CLIENTS),
                           speed,
                           slots);
}

void SV_ConnectionlessPacket(const netadr_t *from, LPSIZEBUF msg) {
    char payload[256];
    char command[32] = { 0 };
    char *status;
    DWORD length;

    if (!msg || msg->cursize <= 4) {
        return;
    }

    length = msg->cursize - 4;
    if (length >= sizeof(payload)) {
        length = sizeof(payload) - 1;
    }
    memcpy(payload, msg->data + 4, length);
    payload[length] = '\0';

    status = strchr(payload, '\n');
    if (status) {
        *status++ = '\0';
    }
    sscanf(payload, "%31s", command);
    if (!strcmp(command, "connect")) {
        fprintf(stderr,
                "SV_ConnectionlessPacket: connect from %s\n",
                from ? NET_AdrToString(from) : "unknown");
        SV_DirectConnect(from, status);
    } else if (!strcmp(command, "info")) {
        SV_LanInfo(from);
    }
}
