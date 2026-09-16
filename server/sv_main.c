/*
 * sv_main.c — Main server loop and frame processing.
 *
 * The server advances the game in fixed-size time steps (FRAMETIME).
 * Every frame it reads pending client commands. When simulation is running it
 * advances one fixed game step and sends the resulting state; while paused it
 * keeps transport alive with frozen-state snapshots instead.
 *
 * Entry point called from the platform main loop: SV_Frame().
 */
#include "server.h"

//#define PRINT_ANIMATIONS

struct game_export *ge;
struct server sv;
struct server_static svs;

#define BZ_KEEPALIVE_MSEC 1000 // milliseconds; Quake 2's interval; keeps clients alive while waiting to spawn

BOOL SV_IsActive(void) {
    return svs.initialized && (sv.state == ss_lobby || sv.state == ss_game);
}

/* Store one server-owned configstring and force reliable client resynchronization. */
void SV_SetConfigString(DWORD index, LPCSTR value, DWORD len) {
    if (index >= MAX_CONFIGSTRINGS) {
        fprintf(stderr, "configstring: bad index %u\n", index);
        return;
    }
    if (!value) {
        value = "";
        len = 1;
    }
    DWORD max = sizeof(sv.configstrings[index]) - 1;
    if (len > max) len = max;
    memset(sv.configstrings[index], 0, sizeof(sv.configstrings[index]));
    memcpy(sv.configstrings[index], value, len);
    /* Q2 publishes loading-time values through signon, not a second bulk live update that overflows UDP. */
    sv.syncstrings[index] = sv.state == ss_loading;
}

/* Batch bounds must account for fixed binary slots as well as theme-decorated strings. */
DWORD SV_ConfigStringWireSize(DWORD index) {
    if (index == CS_STATUSBAR) {
        return 1 + 2 + sizeof(*sv.configstrings);
    }
    return 1 + 2 + (DWORD)strlen(ge->GetThemeValue(sv.configstrings[index])) + 1;
}

static void SV_AppendConfigString(LPSIZEBUF msg, DWORD i) {
    MSG_WriteByte(msg, svc_configstring);
    MSG_WriteShort(msg, i);
    if (i == CS_STATUSBAR) {
        MSG_Write(msg, sv.configstrings[i], sizeof(*sv.configstrings));
    } else {
        MSG_WriteString(msg, ge->GetThemeValue(sv.configstrings[i]));
    }
}

static BOOL SV_QueueConfigString(LPCLIENT client, DWORD i) {
    DWORD size = SV_ConfigStringWireSize(i);
    DWORD limit = SV_SignonLimit(&client->netchan);

    if (size + 32 > limit) {
        fprintf(stderr, "SV_QueueConfigString: configstring %u exceeds message limit\n", (unsigned)i);
        return false;
    }
    if (client->netchan.message.cursize && client->netchan.message.cursize + size + 32 > limit)
        Netchan_Transmit(NS_SERVER, &client->netchan);
    SV_AppendConfigString(&client->netchan.message, i);
    return true;
}

void SV_WriteConfigString(LPSIZEBUF msg, DWORD i) {
    SV_AppendConfigString(msg, i);
    sv.syncstrings[i] = true;
}

/* Queue live configstrings before any later payload that may reference them.
 * syncstrings[] is global because the same update is appended to every current
 * client; clients that connect later receive the complete signon table. */
void SV_QueuePendingConfigStrings(void) {
    for (DWORD i = 0; sv.state == ss_game && i < MAX_CONFIGSTRINGS; i++) {
        if (!*sv.configstrings[i] || sv.syncstrings[i]) continue;
        BOOL queued = true;
        FOR_LOOP(client_index, svs.num_clients) {
            LPCLIENT client = &svs.clients[client_index];
            if (client->state == cs_free || client->state == cs_zombie) continue;
            if (!SV_QueueConfigString(client, i)) queued = false;
        }
        if (queued) sv.syncstrings[i] = true;
    }
}

static void SV_SendClientDatagram(LPCLIENT client) {
    SV_BuildClientFrame(client);
    SV_WriteFrameToClient(client);
}

/* Flush any un-synced config strings to all clients, then send a per-frame
 * datagram to every spawned client containing the current entity snapshot. */
static void SV_SendClientMessages(void) {
    SV_QueuePendingConfigStrings();
    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT client = &svs.clients[i];
        if (client->state == cs_spawned && sv.state == ss_game) {
            SV_SendClientDatagram(client);
        } else if (client->state == cs_connected || client->state == cs_spawned) {
            /* Q2 sends pending messages or a one-second keepalive while a client has no gameplay frames. */
            if (client->netchan.message.cursize || svs.realtime >= sv.keepalive) {
                if (!client->netchan.message.cursize) MSG_WriteByte(&client->netchan.message, svc_nop);
                Netchan_Transmit(NS_SERVER, &client->netchan);
            }
        }
    }
    if (svs.realtime >= sv.keepalive) sv.keepalive = svs.realtime + BZ_KEEPALIVE_MSEC;
}

static void SV_ProcessPacket(netadr_t *from, LPSIZEBUF net_message, int r) {
    if (r >= 4) {
        int hdr;
        memcpy(&hdr, net_message->data, sizeof(hdr));
        if (hdr == -1) {
            SV_ConnectionlessPacket(from, net_message);
            return;
        }
    }
    LPCLIENT client = SV_FindClientByAddr(from);
    if (client) {
        SV_ParseClientMessage(net_message, client);
    }
}

/* Read and dispatch all pending client messages from the network buffers. */
static void SV_ReadPackets(void) {
    static BYTE net_message_buffer[MAX_MSGLEN];
    static sizeBuf_t net_message = {
        .data = net_message_buffer,
        .maxsize = MAX_MSGLEN,
        .cursize = 0,
        .readcount = 0,
    };
    netadr_t from;
    int r;
    while ((r = NET_GetLoopPacket(NS_SERVER, &from, &net_message)) != 0) {
        SV_ProcessPacket(&from, &net_message, r);
    }
    if (sv.state == ss_dead) {
        while ((r = NET_GetPacket(NS_SERVER, &from, &net_message)) != 0) {
            SV_ProcessPacket(&from, &net_message, r);
        }
    } else {
        while ((r = NET_GetPacket(NS_SERVER, &from, &net_message)) != 0) {
            SV_ProcessPacket(&from, &net_message, r);
        }
    }
}

static int SV_FindIndex(LPCSTR name, int start, int max, bool create) {
    if (!name || !name[0])
        return 0;
    int i;
    for (i=1 ; i<max && sv.configstrings[start+i][0] ; i++)
        if (!strcmp(sv.configstrings[start+i], name))
            return i;
    if (!create)
        return 0;
    if (i >= max) {
        fprintf(stderr,
                "SV_FindIndex: pool full start=%d max=%d name=%s\n",
                start,
                max,
                name);
        return 0;
    }
    SV_SetConfigString(start + i, name, (DWORD)(strlen(name) + 1));
    return i;
}

int SV_ModelIndex(LPCSTR name) {
//    if (!strcmp(name, "units\\human\\Peasant\\Peasant.mdx")) {
//        name = "Assets\\Units\\Terran\\MarineTychus\\MarineTychus.m3";
//    }
    PATHSTR model_filename = { 0 };
    LPCSTR base;
    LPCSTR slash;
    LPSTR ext;

    strlcpy(model_filename, name, sizeof(model_filename));
    base = model_filename;
    slash = strrchr(base, '\\');
    if (slash) {
        base = slash + 1;
    }
    slash = strrchr(base, '/');
    if (slash) {
        base = slash + 1;
    }
    ext = strrchr((LPSTR)base, '.');
    if (!ext) {
        size_t len = strlen(model_filename);
        if (len + 5 <= sizeof(model_filename)) {
            memcpy(model_filename + len, ".mdx", 5);
        }
    } else if (!strcasecmp(ext, ".mdl")) {
        memcpy(ext, ".mdx", 5);
    }
    int modelindex = SV_FindIndex(model_filename, CS_MODELS, MAX_MODELS, true);
    return modelindex;
}

int SV_SoundIndex(LPCSTR name) {
    return SV_FindIndex(name, CS_SOUNDS, MAX_SOUNDS, true);
}

int SV_ImageIndex(LPCSTR name) {
    return SV_FindIndex(name, CS_IMAGES, MAX_IMAGES, true);
}

int SV_FontIndex(LPCSTR name, DWORD fontSize) {
    PATHSTR fontspec;
    snprintf(fontspec, sizeof(fontspec), "%s,%d", name, fontSize);
    return SV_FindIndex(fontspec, CS_FONTS, MAX_FONTSTYLES, true);
}

/* Advance the simulation by one frame: increment the frame counter and game
 * clock, then invoke the game library's RunFrame callback. */
void SV_RunGameFrame(void) {
    sv.framenum++;
    sv.time += FRAMETIME;
    ge->RunFrame();
}

/* Pause is a scheduler property, not a stopped server. Client commands must
 * still be readable so a modal can close/unpause, and spawned clients still
 * need traffic so their normal connection timeout does not fire. */
void SV_SetPaused(BOOL paused) {
    paused = !!paused;
    Cvar_Set("paused", paused ? "1" : "0");
    if (sv.paused == paused) {
        return;
    }
    sv.paused = paused;
    sv.pause_msec = 0;

    /* Wall-clock time accumulated while paused must never become simulation
     * catch-up work. Keep realtime monotonic and rebase only the simulation
     * scheduler deadline. */
    if (!paused) {
        sv.next_frame_msec = svs.realtime;
    }
}

/* Main server tick called from the platform event loop with the elapsed
 * milliseconds since the last call.  Network input remains live while the
 * authoritative simulation is paused. */
void SV_Frame(DWORD msec) {
    svs.realtime += msec;
    SV_ReadPackets();

    if (sv.state == ss_lobby) {
        /* An unchanged lobby previously sent nothing and timed out even its own loopback host. */
        SV_SendClientMessages();
        return;
    }

    if (sv.paused) {
        sv.pause_msec += msec;
        if (sv.pause_msec >= FRAMETIME) {
            sv.pause_msec %= FRAMETIME;
            SV_SendClientMessages();
        }
        return;
    }

    if (svs.realtime < sv.next_frame_msec) {
        return;
    }

    sv.next_frame_msec = SV_ClampSimulationDeadline(svs.realtime, sv.next_frame_msec);
    SV_RunGameFrame();
    sv.next_frame_msec += FRAMETIME;
    SV_SendClientMessages();
}
