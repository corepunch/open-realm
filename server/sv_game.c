#include <stdarg.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "server.h"

/* UI layout byte tracking (legacy - now handled client-side) */
uint32_t layoutBytesWritten = 0;

void PF_Write(pfWriteType_t type, void const *value) {
    switch (type) {
        case PF_BYTE:
            MSG_WriteByte(&sv.multicast, (int)*(int32_t const *)value);
            break;
        case PF_SHORT:
            MSG_WriteShort(&sv.multicast, (int)*(int32_t const *)value);
            break;
        case PF_LONG:
            MSG_WriteLong(&sv.multicast, (int)*(int32_t const *)value);
            break;
        case PF_FLOAT:
            MSG_WriteFloat(&sv.multicast, *(float const *)value);
            break;
        case PF_STRING:
            MSG_WriteString(&sv.multicast, value ? (cstring_t)value : "");
            break;
        case PF_POSITION:
            MSG_WritePos(&sv.multicast, (vector3_t const *)value);
            break;
        case PF_DIRECTION:
            MSG_WriteDir(&sv.multicast, (vector3_t const *)value);
            break;
        case PF_ANGLE:
            MSG_WriteAngle(&sv.multicast, *(float const *)value);
            break;
        case PF_ENTITY: {
            entityState_t empty;
            memset(&empty, 0, sizeof(entityState_t));
            MSG_WriteDeltaEntity(&sv.multicast, &empty, (entityState_t const *)value, true);
            break;
        }
        case PF_UIFRAME: {
            uiFrame_t const *frame = (uiFrame_t const *)value;
            uint32_t before = sv.multicast.cursize;
            uiFrame_t empty;
            memset(&empty, 0, sizeof(uiFrame_t));
            empty.tex.coord[1] = 0xff;
            empty.tex.coord[3] = 0xff;
            MSG_WriteDeltaUIFrame(&sv.multicast, &empty, frame, true);
            MSG_WriteByte(&sv.multicast, frame->buffer.size);
            MSG_Write(&sv.multicast, frame->buffer.data, frame->buffer.size);
            if (sv.multicast.cursize >= before) {
                extern uint32_t layoutBytesWritten;
                layoutBytesWritten += sv.multicast.cursize - before;
            }
            break;
        }
        case PF_UIWINDOWFRAME: {
            uiFrame_t const *frame = (uiFrame_t const *)value;
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

void PF_Confignstring(uint32_t index, cstring_t value, uint32_t len) {
    SV_SetConfigString(index, value, len);
}

void PF_Configstring(uint32_t index, cstring_t value) {
    if (!value) {
        value = "";
    }

    PF_Confignstring(index, value, (uint32_t)(strlen(value) + 1));
}

cstring_t PF_GetConfigstring(uint32_t index) {
    if (index >= MAX_CONFIGSTRINGS)
        return "";
    return sv.configstrings[index];
}

uint32_t SV_GetTime(void) {
    return sv.time;
}

void SV_SetGameTime(uint32_t time) {
    sv.time = time;
}

void PF_Multicast(vector3_t const *origin, multicast_t to) {
    SV_Multicast(origin, to);
}

static void PF_StartSound(edict_t *ent, int channel, int sound_index, float volume, float attenuation, float timeofs) {
    SV_StartSound(NULL, ent, channel, sound_index, volume, attenuation, timeofs);
}

static void PF_PositionedSound(vector3_t const *origin, edict_t *ent, int channel, int sound_index, float volume,
                               float attenuation, float timeofs) {
    SV_StartSound(origin, ent, channel, sound_index, volume, attenuation, timeofs);
}

void PF_error(cstring_t fmt, ...) {
    char msg[1024];
    va_list argptr;
    va_start(argptr,fmt);
    vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);
    fprintf(stderr, "Game Error: %s\n", msg);
}




void PF_Sleep(uint32_t msec) {
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
    import.SoundIndexAlias = SV_SoundIndexAlias;
    import.Sound = PF_StartSound;
    import.SoundPolicy = SV_StartSoundPolicy;
    import.PositionedSound = PF_PositionedSound;
    import.MinimapPing = SV_MinimapPing;
    import.FontIndex = SV_FontIndex;
    import.GetTime = SV_GetTime;
    import.SetGameTime = SV_SetGameTime;
    import.SetPaused = SV_SetPaused;
    import.ReadFile = FS_ReadFile;
    import.ReadFileAll = FS_ReadFileAll;
    import.SetPriorityArchive = FS_SetPriorityArchive;
    import.error = PF_error;
    import.LinkEntity = SV_LinkEntity;
    import.UnlinkEntity = SV_UnlinkEntity;
    import.BoxEdicts = SV_AreaEdicts;
    import.MenuAction = MenuAction;
    import.QueueMovie = CL_QueueMovie;
    import.ClearWorld = SV_ClearWorld;
    import.LoadingFrame = CL_LoadingFrame;
    import.configstring = PF_Configstring;
    import.confignstring = PF_Confignstring;
    import.GetConfigstring = PF_GetConfigstring;
    import.Write = PF_Write;
    import.ApplyLobbySettings = SV_ApplyLobbySettings;
    import.CvarString = Cvar_String;
    import.UserPath = FS_UserPath;
    import.SavePath = FS_SavePath;
    import.ListSaves = FS_ListSaves;
    import.DeleteSave = FS_DeleteSave;

    ge = GetGameAPI(&import);
    ge->Init();
}

#ifdef WOW
/* Character creation data is server-owned; initialize the game module before asking it for the selected spawn map. */
uint32_t SV_PlayerCreateMap(void) {
    if (!ge)
        SV_InitGameProgs();
    return ge && ge->PlayerCreateMap ? ge->PlayerCreateMap() : ~0u;
}
#endif
