#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../../client/client.h"

struct client_state cl;
struct client_static cls;
refExport_t re;
uiExport_t ui;
uiImport_t uiimport;
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
    (void)mapName;
    cl.playerstate.client_ui_state = CLIENT_UI_LOADING;
    cls.state = ca_connected;
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

unsigned int SDL_GetTicks(void) {
    return 0;
}

void Com_Error(errorCode_t code, LPCSTR fmt, ...) {
    (void)code;
    (void)fmt;
}

void SCR_EndLoadingPlaque(void) {
    cls.disable_screen = 0;
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
