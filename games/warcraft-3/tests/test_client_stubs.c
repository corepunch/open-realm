#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../../client/client.h"

struct client_state cl;
struct client_static cls;
refExport_t re;
uiExport_t ui;
mouseEvent_t mouse;

typedef struct {
    char name[64];
    char value[128];
} mockCvar_t;

static mockCvar_t mock_cvars[32];

#define MOCK_CVAR_COUNT (sizeof(mock_cvars) / sizeof(mock_cvars[0]))

void test_client_stubs_clear_cvars(void) {
    memset(mock_cvars, 0, sizeof(mock_cvars));
}

void test_client_stubs_set_cvar(LPCSTR name, LPCSTR value) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (!mock_cvars[i].name[0] || !strcmp(mock_cvars[i].name, name)) {
            snprintf(mock_cvars[i].name, sizeof(mock_cvars[i].name), "%s", name ? name : "");
            snprintf(mock_cvars[i].value, sizeof(mock_cvars[i].value), "%s", value ? value : "");
            return;
        }
    }
}

static size2_t mock_GetWindowSize(void) {
    return MAKE(size2_t, 1024, 768);
}

static void mock_DrawLoadingIndicator(LPCRECT rect, DWORD time, COLOR32 color) {
    (void)rect;
    (void)time;
    (void)color;
}

void V_RenderView(void) {
}

void CON_DrawConsole(void) {
}

int Cvar_Integer(LPCSTR name, int fallback) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (mock_cvars[i].name[0] && !strcmp(mock_cvars[i].name, name)) {
            return atoi(mock_cvars[i].value);
        }
    }
    return fallback;
}

LPCSTR Cvar_String(LPCSTR name, LPCSTR fallback) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (mock_cvars[i].name[0] && !strcmp(mock_cvars[i].name, name)) {
            return mock_cvars[i].value;
        }
    }
    return fallback;
}

void CL_ParseTEnt(LPSIZEBUF msg) {
    (void)msg;
}

void CL_BeginLoadingMap(LPCSTR mapName) {
    snprintf(cl.loading_map, sizeof(cl.loading_map), "%s", mapName ? mapName : "");
    cl.loading_status[0] = '\0';
    cl.loading_progress = 0.0f;
    cl.playerstate.client_ui_state = CLIENT_UI_LOADING;
    cls.state = ca_loading;
}

void CL_SetGameplayInput(void) {
    cls.key_dest = key_game;
}

void CL_Disconnect(LPCSTR reason, BOOL notify) {
    (void)reason;
    (void)notify;
    cls.state = ca_disconnected;
}

void CL_EntityEvent(entityState_t const *ent) {
    (void)ent;
}

void Cbuf_AddText(LPCSTR text) {
    (void)text;
}

void Cmd_ForwardToServer(LPCSTR text) {
    (void)text;
}

HANDLE FS_ReadFile(LPCSTR filename, LPDWORD size) {
    (void)filename;
    if (size) {
        *size = 0;
    }
    return NULL;
}

BOMStatus PF_TextRemoveBom(LPSTR buffer) {
    (void)buffer;
    return NO_BOM;
}

bool CM_LoadMapFormat(LPCSTR mapFilename) {
    (void)mapFilename;
    return false;
}

BOOL CM_ReadMapInfo(LPCSTR mapFilename, LPMAPINFO info) {
    (void)mapFilename;
    if (info) {
        memset(info, 0, sizeof(*info));
    }
    return false;
}

BOOL CM_FindMapPreviewTexture(LPCSTR mapFilename, LPSTR out, DWORD out_size) {
    (void)mapFilename;
    if (out && out_size) {
        out[0] = '\0';
    }
    return false;
}

void CM_FreeMapInfo(LPMAPINFO info) {
    (void)info;
}

void CM_DefaultMapName(LPCSTR path, LPSTR out, DWORD out_size) {
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s", path ? path : "");
}

void CM_ResolveMapInfoString(LPCMAPINFO info, LPCSTR text, LPSTR out, DWORD out_size) {
    (void)info;
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s", text ? text : "");
}

BOOL CM_MapNameMatchesFile(LPCSTR name, LPCSTR path) {
    (void)name;
    (void)path;
    return false;
}

LPCSTR CM_TilesetName(BYTE tileset) {
    (void)tileset;
    return NULL;
}

LPCSTR CM_MapSizeName(DWORD width, DWORD height) {
    (void)width;
    (void)height;
    return "Unknown";
}

void CM_SanitizeMapListField(LPSTR text) {
    (void)text;
}

void CM_SanitizeMapInfoText(LPSTR text) {
    (void)text;
}

void test_client_stubs_init(void) {
    memset(&cl, 0, sizeof(cl));
    memset(&cls, 0, sizeof(cls));
    memset(&re, 0, sizeof(re));
    memset(&ui, 0, sizeof(ui));
    memset(&mouse, 0, sizeof(mouse));
    re.GetWindowSize = mock_GetWindowSize;
    re.DrawLoadingIndicator = mock_DrawLoadingIndicator;
}
