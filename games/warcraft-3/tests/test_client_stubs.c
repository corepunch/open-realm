/*
 * test_client_stubs.c — Global client state and stubs for standalone net tests.
 *
 * Provides the client_state, client_static, refExport_t, menuExport_t, and
 * mouseEvent_t globals that are normally defined in cl_main.c and referenced
 * by client/cl_parse.c, common/net.c, and common/msg.c.  Not a test harness
 * — these are the real global symbols the code expects.
 */
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../client/client.h"

struct client_state cl;
struct client_static cls;
refExport_t re;
menuExport_t menu;
mouseEvent_t mouse;
DWORD test_fow_upload_calls;
DWORD test_cursor_draw_calls;
COLOR32 test_cursor_tint;
char test_forwarded_command[128];
static PATHSTR test_existing_file;
static BOX2 test_world_bounds;

typedef struct { char name[64]; char value[128]; } mockCvar_t;
static mockCvar_t mock_cvars[32];
#define MOCK_CVAR_COUNT (sizeof(mock_cvars) / sizeof(mock_cvars[0]))

void test_client_stubs_clear_cvars(void) { memset(mock_cvars, 0, sizeof(mock_cvars)); }
void test_client_stubs_set_existing_file(LPCSTR path) {
    snprintf(test_existing_file, sizeof(test_existing_file), "%s", path ? path : "");
}

bool FS_FileExists(LPCSTR fileName) {
    return fileName && test_existing_file[0] && !strcasecmp(fileName, test_existing_file);
}

void test_client_stubs_set_world_bounds(BOX2 bounds) { test_world_bounds = bounds; }
BOX2 CM_GetWorldBounds(void) { return test_world_bounds; }

void test_client_stubs_set_cvar(LPCSTR name, LPCSTR value) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (!mock_cvars[i].name[0] || !strcmp(mock_cvars[i].name, name)) {
            snprintf(mock_cvars[i].name, sizeof(mock_cvars[i].name), "%s", name ? name : "");
            snprintf(mock_cvars[i].value, sizeof(mock_cvars[i].value), "%s", value ? value : "");
            return;
        }
    }
}

static size2_t mock_GetWindowSize(void) { return MAKE(size2_t, 1024, 768); }
static void mock_DrawLoadingIndicator(LPCRECT rect, DWORD time, COLOR32 color) { (void)rect; (void)time; (void)color; }
static void mock_DrawFill(LPCRECT rect, COLOR32 color) { (void)rect; (void)color; }
static bool mock_DrawCursor(float x, float y, COLOR32 tint) {
    (void)x; (void)y;
    test_cursor_draw_calls++;
    test_cursor_tint = tint;
    return true;
}
static void mock_SetFogOfWarData(DWORD width, DWORD height, BYTE const *data) {
    (void)width; (void)height; (void)data; test_fow_upload_calls++;
}

void V_RenderView(void) {}
void CON_DrawConsole(void) {}
BOOL CL_GameplayInputReady(void) { return false; }

int Cvar_Integer(LPCSTR name, int fallback) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (mock_cvars[i].name[0] && !strcmp(mock_cvars[i].name, name))
            return atoi(mock_cvars[i].value);
    }
    return fallback;
}

LPCSTR Cvar_String(LPCSTR name, LPCSTR fallback) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (mock_cvars[i].name[0] && !strcmp(mock_cvars[i].name, name))
            return mock_cvars[i].value;
    }
    return fallback;
}

cvar_t *Cvar_Set(LPCSTR name, LPCSTR value) {
    test_client_stubs_set_cvar(name, value);
    return NULL;
}

void CL_ParseTEnt(LPSIZEBUF msg) { (void)msg; }
void CL_BeginLoadingMap(LPCSTR mapName) { (void)mapName; cl.playerstate.client_ui_state = CLIENT_UI_LOADING; cls.state = ca_connected; cl.num_active = 0; }
void CL_SetGameplayInput(void) { cls.key_dest = key_game; }
LPCSTR CL_ResolveImagePath(LPCSTR imageName) { return imageName; }
void CL_ReloadImageResources(void) {}
void CL_Disconnect(LPCSTR reason, BOOL notify) { (void)reason; (void)notify; cls.state = ca_disconnected; }
void CL_EntityEvent(entityState_t const *ent) { (void)ent; }
void S_RegisterSound(LPCSTR path) { (void)path; }
void S_PlaySoundFile(LPCSTR path) { (void)path; }
void S_PlaySoundPacket(LPCSTR path, LPCVECTOR3 origin, BOOL positioned, int channel, FLOAT volume, FLOAT attenuation,
                       FLOAT timeofs) {
    (void)path; (void)origin; (void)positioned; (void)channel; (void)volume; (void)attenuation; (void)timeofs;
}
void Cbuf_AddText(LPCSTR text) { (void)text; }
void Cmd_ForwardToServer(LPCSTR text) {
    snprintf(test_forwarded_command, sizeof(test_forwarded_command), "%s", text ? text : "");
}
unsigned int SDL_GetTicks(void) { return 0; }
int SDL_ShowCursor(int toggle) { (void)toggle; return 1; }
void Com_Error(errorCode_t code, LPCSTR fmt, ...) { (void)code; (void)fmt; }

void test_client_stubs_init(void) {
    memset(&cl, 0, sizeof(cl));
    memset(&cls, 0, sizeof(cls));
    memset(&re, 0, sizeof(re));
    memset(&menu, 0, sizeof(menu));
    memset(&mouse, 0, sizeof(mouse));
    test_fow_upload_calls = 0;
    test_cursor_draw_calls = 0;
    test_cursor_tint = COLOR32_WHITE;
    test_forwarded_command[0] = '\0';
    test_existing_file[0] = '\0';
    test_world_bounds = (BOX2){ 0 };
    re.GetWindowSize = mock_GetWindowSize;
    re.DrawLoadingIndicator = mock_DrawLoadingIndicator;
    re.DrawFill = mock_DrawFill;
    re.DrawCursor = mock_DrawCursor;
    re.SetFogOfWarData = mock_SetFogOfWarData;
}

HANDLE MemAlloc(long size) {
    void *p = malloc((size_t)size);
    if (p) memset(p, 0, (size_t)size);
    return p;
}
void MemFree(HANDLE p) { free(p); }
