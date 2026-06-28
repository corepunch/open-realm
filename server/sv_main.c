/*
 * sv_main.c — Main server loop and frame processing.
 *
 * The server advances the game in fixed-size time steps (FRAMETIME).
 * Every frame it reads pending client commands, calls the game library to
 * run one simulation step, and then sends the resulting entity/player state
 * to every connected client.
 *
 * Entry point called from the platform main loop: SV_Frame().
 */
#include "server.h"

//#define PRINT_ANIMATIONS

struct game_export *ge;
struct server sv;
struct server_static svs;

void SV_WriteConfigString(LPSIZEBUF msg, DWORD i) {
    MSG_WriteByte(msg, svc_configstring);
    MSG_WriteShort(msg, i);
    if (i == CS_STATUSBAR) {
        MSG_Write(msg, sv.configstrings[i], sizeof(*sv.configstrings));
    } else {
        MSG_WriteString(msg, ge->GetThemeValue(sv.configstrings[i]));
    }
    sv.syncstrings[i] = true;
}

static void SV_SendClientDatagram(LPCLIENT client) {
    SV_BuildClientFrame(client);
    SV_WriteFrameToClient(client);
}

/* Flush any un-synced config strings to all clients, then send a per-frame
 * datagram to every spawned client containing the current entity snapshot. */
static void SV_SendClientMessages(void) {
    FOR_LOOP(i, MAX_CONFIGSTRINGS) {
        if (*sv.configstrings[i] && !sv.syncstrings[i]) {
            SV_WriteConfigString(&sv.multicast, i);
            SV_Multicast(&(VECTOR3){0,0,0}, MULTICAST_ALL_R);
        }
    }
    FOR_LOOP(i, svs.num_clients) {
        LPCLIENT client = &svs.clients[i];
        if (client->state == cs_spawned) {
            SV_SendClientDatagram(client);
        }
    }
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
    strncpy(sv.configstrings[start+i], name, sizeof(*sv.configstrings) - 1);
    sv.configstrings[start+i][sizeof(*sv.configstrings) - 1] = '\0';
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

    strcpy(model_filename, name);
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
        ext = model_filename + strlen(model_filename);
        strcpy(ext, ".mdx");
    } else if (!strcasecmp(ext, ".mdl")) {
        strcpy(ext, ".mdx");
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
    sprintf(fontspec, "%s,%d", name, fontSize);
    return SV_FindIndex(fontspec, CS_FONTS, MAX_FONTSTYLES, true);
}

/* Advance the simulation by one frame: increment the frame counter and game
 * clock, then invoke the game library's RunFrame callback. */
void SV_RunGameFrame(void) {
    sv.framenum++;
    sv.time += FRAMETIME;
    ge->RunFrame();
}

/* Main server tick called from the platform event loop with the elapsed
 * milliseconds since the last call.  Returns immediately if it is not yet
 * time for a new game frame so the caller can do other work. */
void SV_Frame(DWORD msec) {
    svs.realtime += msec;
    SV_ReadPackets();
    
    if (svs.realtime < sv.time) {
        return;
    }

    if (sv.state == ss_lobby) {
        return;
    }

    SV_RunGameFrame();

    SV_SendClientMessages();
}
