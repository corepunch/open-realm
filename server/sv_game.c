#include <stdarg.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "server.h"

/* UI layout byte tracking (legacy - now handled client-side) */
DWORD layoutBytesWritten = 0;

/* Dedicated servers can inspect the exact svc_layout payload before it enters the netchan. */
static void SV_DebugLayoutMessage(sizeBuf_t const *source) {
    sizeBuf_t msg = *source;
    DWORD frames = 0, textured = 0, stats = 0;

    msg.readcount = 0;
    if (MSG_ReadByte(&msg) != svc_layout) return;
    DWORD layer = MSG_ReadByte(&msg);
    while (msg.readcount + sizeof(DWORD) + sizeof(WORD) <= msg.cursize) {
        UIFRAME frame = { 0 };
        DWORD bits, number = MSG_ReadEntityBits(&msg, &bits);
        if (!number && !bits) break;
        MSG_ReadDeltaUIFrame(&msg, &frame, number, bits);
        if (msg.readcount >= msg.cursize) break;
        DWORD payload = (BYTE)MSG_ReadByte(&msg);
        if (payload > msg.cursize - msg.readcount) break;
        msg.readcount += payload;
        frames++;
        textured += frame.tex.index != 0;
        stats += frame.stat != 0;
    }
    fprintf(stderr, "SV layout: layer=%u bytes=%u frames=%u textured=%u stats=%u\n",
            (unsigned)layer, (unsigned)source->cursize, (unsigned)frames,
            (unsigned)textured, (unsigned)stats);
}

void PF_Write(pfWriteType_t type, void const *value) {
    switch (type) {
        case PF_BYTE:
            MSG_WriteByte(&sv.multicast, (int)*(LONG const *)value);
            break;
        case PF_SHORT:
            MSG_WriteShort(&sv.multicast, (int)*(LONG const *)value);
            break;
        case PF_LONG:
            MSG_WriteLong(&sv.multicast, (int)*(LONG const *)value);
            break;
        case PF_FLOAT:
            MSG_WriteFloat(&sv.multicast, *(FLOAT const *)value);
            break;
        case PF_STRING:
            MSG_WriteString(&sv.multicast, value ? (LPCSTR)value : "");
            break;
        case PF_POSITION:
            MSG_WritePos(&sv.multicast, (LPCVECTOR3)value);
            break;
        case PF_DIRECTION:
            MSG_WriteDir(&sv.multicast, (LPCVECTOR3)value);
            break;
        case PF_ANGLE:
            MSG_WriteAngle(&sv.multicast, *(FLOAT const *)value);
            break;
        case PF_ENTITY: {
            entityState_t empty;
            memset(&empty, 0, sizeof(entityState_t));
            MSG_WriteDeltaEntity(&sv.multicast, &empty, (LPCENTITYSTATE)value, true);
            break;
        }
        case PF_UIFRAME: {
            LPCUIFRAME frame = (LPCUIFRAME)value;
            DWORD before = sv.multicast.cursize;
            uiFrame_t empty;
            memset(&empty, 0, sizeof(uiFrame_t));
            empty.tex.coord[1] = 0xff;
            empty.tex.coord[3] = 0xff;
            MSG_WriteDeltaUIFrame(&sv.multicast, &empty, frame, true);
            MSG_WriteByte(&sv.multicast, frame->buffer.size);
            MSG_Write(&sv.multicast, frame->buffer.data, frame->buffer.size);
            if (sv.multicast.cursize >= before) {
                extern DWORD layoutBytesWritten;
                layoutBytesWritten += sv.multicast.cursize - before;
            }
            break;
        }
        case PF_UIWINDOWFRAME: {
            LPCUIFRAME frame = (LPCUIFRAME)value;
            uiFrame_t empty = { 0 };
            empty.tex.coord[1] = empty.tex.coord[3] = 0xff;
            MSG_WriteDeltaUIWindowFrame(&sv.multicast, &empty, frame, true);
            MSG_WriteByte(&sv.multicast, frame->buffer.size);
            MSG_Write(&sv.multicast, frame->buffer.data, frame->buffer.size);
            break;
        }
        case PF_DATA: {
            pfWriteData_t const *data = value;
            if (data && data->data && data->size) {
                MSG_Write(&sv.multicast, data->data, data->size);
            }
            break;
        }
    }
}

void PF_Confignstring(DWORD index, LPCSTR value, DWORD len) {
    SV_SetConfigString(index, value, len);
}

void PF_Configstring(DWORD index, LPCSTR value) {
    if (!value) {
        value = "";
    }

    PF_Confignstring(index, value, (DWORD)(strlen(value) + 1));
}

LPCSTR PF_GetConfigstring(DWORD index) {
    if (index >= MAX_CONFIGSTRINGS)
        return "";
    return sv.configstrings[index];
}

DWORD SV_GetTime(void) {
    return sv.time;
}

void SV_SetGameTime(DWORD time) {
    sv.time = time;
}

void PF_Multicast(LPCVECTOR3 origin, multicast_t to) {
    SV_Multicast(origin, to);
}

static void PF_StartSound(LPEDICT ent, int channel, int sound_index, FLOAT volume, FLOAT attenuation, FLOAT timeofs) {
    SV_StartSound(NULL, ent, channel, sound_index, volume, attenuation, timeofs);
}

static void PF_PositionedSound(LPCVECTOR3 origin, LPEDICT ent, int channel, int sound_index, FLOAT volume,
                               FLOAT attenuation, FLOAT timeofs) {
    SV_StartSound(origin, ent, channel, sound_index, volume, attenuation, timeofs);
}

void PF_Unicast(edict_t *ent) {
    if (Cvar_Integer("sv_debug_layout", 0)) SV_DebugLayoutMessage(&sv.multicast);
    if (!ent) {
        SZ_Clear(&sv.multicast);
        return;
    }
    DWORD p = NUM_FOR_EDICT(ent);
    LPCLIENT client = NULL;
    if (p >= 1 && p <= ge->max_clients && p <= svs.num_clients) {
        client = svs.clients + (p - 1);
    } else if (svs.num_clients == 1) {
        client = svs.clients;
    } else {
        SZ_Clear(&sv.multicast);
        return;
    }
    SZ_Write(&client->netchan.message, sv.multicast.data, sv.multicast.cursize);
    SZ_Clear(&sv.multicast);
    Netchan_Transmit(NS_SERVER, &client->netchan);
}

/* Send a bounded typed payload through the same reliable per-client channel as
 * layout and lobby game commands, without exposing netchan state to game DLLs. */
void SV_GameCommand(edict_t *ent, LPCSTR command, void const *data, DWORD size) {
    BYTE payload_buf[MAX_MSGLEN];
    sizeBuf_t payload;
    DWORD p;
    LPCLIENT client;

    if (!ent || !command || (!data && size) || size > sizeof(payload_buf)) {
        fprintf(stderr, "SV_GameCommand: rejected command=%s size=%u\n",
                command ? command : "(null)", (unsigned)size);
        return;
    }
    p = NUM_FOR_EDICT(ent);
    client = p >= 1 && p <= ge->max_clients && p <= svs.num_clients
        ? svs.clients + (p - 1) : svs.num_clients == 1 ? svs.clients : NULL;
    if (!client) {
        fprintf(stderr, "SV_GameCommand: no client for command=%s\n", command);
        return;
    }
    SZ_Init(&payload, payload_buf, sizeof(payload_buf));
    if (size) SZ_Write(&payload, data, size);
    SV_WriteGameCommand(&client->netchan.message, command, &payload);
    Netchan_Transmit(NS_SERVER, &client->netchan);
}

void PF_error(LPCSTR fmt, ...) {
    char msg[1024];
    va_list argptr;
    va_start(argptr,fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    fprintf(stderr, "Game Error: %s\n", msg);
}




void PF_Sleep(DWORD msec) {
    usleep(msec * 1000);
}

void SV_InitGameProgs(void) {
    struct game_import import = { 0 };
    
    import.multicast = PF_Multicast;
    import.unicast = PF_Unicast;
    import.GameCommand = SV_GameCommand;
        
    import.MemAlloc = MemAlloc;
    import.MemFree = MemFree;
    import.ModelIndex = SV_ModelIndex;
    import.ImageIndex = SV_ImageIndex;
    import.SoundIndex = SV_SoundIndex;
    import.Sound = PF_StartSound;
    import.PositionedSound = PF_PositionedSound;
    import.MinimapPing = SV_MinimapPing;
    import.FontIndex = SV_FontIndex;
    import.GetTime = SV_GetTime;
    import.SetGameTime = SV_SetGameTime;
    import.SetPaused = SV_SetPaused;
    import.ReadFile = FS_ReadFile;
    import.ReadFileAll = FS_ReadFileAll;
    import.error = PF_error;
    import.LinkEntity = SV_LinkEntity;
    import.UnlinkEntity = SV_UnlinkEntity;
    import.BoxEdicts = SV_AreaEdicts;
    import.MenuAction = MenuAction;
    import.ClearWorld = SV_ClearWorld;
    import.configstring = PF_Configstring;
    import.confignstring = PF_Confignstring;
    import.GetConfigstring = PF_GetConfigstring;
    import.Write = PF_Write;
    import.ApplyLobbySettings = SV_ApplyLobbySettings;
    import.CvarString = Cvar_String;
    import.UserPath = FS_UserPath;
    import.SavePath = FS_SavePath;

    ge = GetGameAPI(&import);
    ge->Init();
}

#ifdef WOW
/* Character creation data is server-owned; initialize the game module before asking it for the selected spawn map. */
DWORD SV_PlayerCreateMap(void) {
    if (!ge)
        SV_InitGameProgs();
    return ge && ge->PlayerCreateMap ? ge->PlayerCreateMap() : ~0u;
}
#endif
