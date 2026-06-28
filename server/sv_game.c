#include <stdarg.h>
#include <unistd.h>

#include "server.h"

/* UI layout byte tracking (legacy - now handled client-side) */
DWORD layoutBytesWritten = 0;

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
            MSG_WriteShort(&sv.multicast, frame->buffer.size);
            MSG_Write(&sv.multicast, frame->buffer.data, frame->buffer.size);
            if (sv.multicast.cursize >= before) {
                extern DWORD layoutBytesWritten;
                layoutBytesWritten += sv.multicast.cursize - before;
            }
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
    if (index < 0 || index >= MAX_CONFIGSTRINGS) {
        fprintf(stderr, "configstring: bad index %i\n", index);
        return;
    }
    
    memset(sv.configstrings[index], 0, sizeof(sv.configstrings[index]));
    memcpy(sv.configstrings[index], value, len);
    
    //    if (sv.state != ss_loading)
    //    {    // send the update to everyone
    //        SZ_Clear (&sv.multicast);
    //        MSG_WriteChar (&sv.multicast, svc_configstring);
    //        MSG_WriteShort (&sv.multicast, index);
    //        MSG_WriteString (&sv.multicast, value);
    //
    //        SV_Multicast (vec3_origin, MULTICAST_ALL_R);
    //    }
}

void PF_Configstring(DWORD index, LPCSTR value) {
    if (!value) {
        value = "";
    }

    PF_Confignstring(index, value, (DWORD)(strlen(value) + 1));
    strcpy(sv.configstrings[index], value);
}

LPCSTR PF_GetConfigstring(DWORD index) {
    if (index >= MAX_CONFIGSTRINGS)
        return "";
    return sv.configstrings[index];
}

DWORD SV_GetTime(void) {
    return sv.time;
}

void PF_Multicast(LPCVECTOR3 origin, multicast_t to) {
    SV_Multicast(origin, to);
}

void PF_Unicast(edict_t *ent) {
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

void PF_error(LPCSTR fmt, ...) {
    char msg[1024];
    va_list argptr;
    va_start(argptr,fmt);
    vsprintf(msg, fmt, argptr);
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
        
    import.MemAlloc = MemAlloc;
    import.MemFree = MemFree;
    import.ModelIndex = SV_ModelIndex;
    import.ImageIndex = SV_ImageIndex;
    import.SoundIndex = SV_SoundIndex;
    import.FontIndex = SV_FontIndex;
    import.GetTime = SV_GetTime;
    import.ReadFile = FS_ReadFile;
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

    ge = GetGameAPI(&import);
    ge->Init();
}
