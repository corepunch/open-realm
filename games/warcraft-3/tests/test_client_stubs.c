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
uint32_t test_fow_upload_calls;
uint32_t test_cursor_draw_calls;
COLOR32 test_cursor_tint;
char test_forwarded_command[128];
char test_menu_action[32];
char test_menu_action_arg[128];
char test_console_message[MAX_CONSOLE_MESSAGE_LEN];
static PATHSTR test_existing_file;
static BOX2 test_world_bounds;
static size2_t test_window_size;
static UICANVASPOLICY test_canvas_policy = UI_CANVAS_POLICY;
static rect_t test_ui_scene;

typedef struct { char name[64]; char value[128]; } mockCvar_t;
static mockCvar_t mock_cvars[32];
#define MOCK_CVAR_COUNT (sizeof(mock_cvars) / sizeof(mock_cvars[0]))

void test_client_stubs_clear_cvars(void) { memset(mock_cvars, 0, sizeof(mock_cvars)); }
void test_client_stubs_set_existing_file(cstring_t path) {
    snprintf(test_existing_file, sizeof(test_existing_file), "%s", path ? path : "");
}

bool FS_FileExists(cstring_t fileName) {
    return fileName && test_existing_file[0] && !strcasecmp(fileName, test_existing_file);
}

void test_client_stubs_set_world_bounds(BOX2 bounds) { test_world_bounds = bounds; }
BOX2 CM_GetWorldBounds(void) { return test_world_bounds; }

void test_client_stubs_set_cvar(cstring_t name, cstring_t value) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (!mock_cvars[i].name[0] || !strcmp(mock_cvars[i].name, name)) {
            snprintf(mock_cvars[i].name, sizeof(mock_cvars[i].name), "%s", name ? name : "");
            snprintf(mock_cvars[i].value, sizeof(mock_cvars[i].value), "%s", value ? value : "");
            return;
        }
    }
}

static bool mock_CameraUsesTerrainHeight(void) { return false; }
static size2_t mock_GetWindowSize(void) { return test_window_size; }
static void mock_SetUIScene(rect_t const * scene) { test_ui_scene = *scene; }
/* The scene the client canvas last pushed to the renderer; tests compare it with CL_Canvas(). */
rect_t test_client_stubs_ui_scene(void) { return test_ui_scene; }
/* Stands in for the per-game hook in games/<game>/common/world_*.c. */
UICANVASPOLICY CL_GameCanvasPolicy(void) { return test_canvas_policy; }
cstring_t CL_GameOrderQueueReleaseCommand(void) { return NULL; }
void test_client_stubs_set_canvas_policy(UICANVASPOLICY policy) {
    test_canvas_policy = policy;
    CL_CanvasResolvePolicy();
}
static void mock_DrawLoadingIndicator(rect_t const * rect, uint32_t time, COLOR32 color) { (void)rect; (void)time; (void)color; }
static void mock_DrawFill(rect_t const * rect, COLOR32 color) { (void)rect; (void)color; }
static void mock_DrawImageEx(LPCDRAWIMAGE image) { (void)image; }
static bool mock_DrawCursor(float x, float y, COLOR32 tint) {
    (void)x; (void)y;
    test_cursor_draw_calls++;
    test_cursor_tint = tint;
    return true;
}

void V_RenderView(void) {}
void CON_DrawConsole(void) {}
void CON_printf(cstring_t fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vsnprintf(test_console_message, sizeof(test_console_message), fmt, args);
    va_end(args);
}
bool CL_GameplayInputReady(void) { return false; }
bool CL_MovieKeyEvent(keyCode_t key, bool down) { (void)key; (void)down; return false; }
bool CL_GameBuildSameTypeSelection(gameSameTypeSelection_t *selection) {
    uint32_t count = 0;
    uint32_t class_id;

    if (!selection || !selection->command || selection->command_size < 2 || !selection->visible_count)
        return false;
    class_id = cl.ents[selection->anchor].current.class_id;
    snprintf(selection->command, selection->command_size, "select %u sametype", selection->anchor);
    FOR_LOOP(i, selection->visible_count) {
        uint32_t const number = selection->visible[i];
        size_t used;
        if (!number || number == selection->anchor || number >= MAX_CLIENT_ENTITIES ||
            cl.ents[number].current.class_id != class_id || count >= MIN(selection->limit, 61)) continue;
        used = strlen(selection->command);
        if (used + 12 >= selection->command_size) break;
        snprintf(selection->command + used, selection->command_size - used, " %u", number);
        count++;
    }
    return true;
}
/* Transient-window tests exercise focus without owning a real SDL text-input session. */
void CL_SetTransientTextInput(bool enabled) { (void)enabled; }

int Cvar_Integer(cstring_t name, int fallback) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (mock_cvars[i].name[0] && !strcmp(mock_cvars[i].name, name))
            return atoi(mock_cvars[i].value);
    }
    return fallback;
}

cstring_t Cvar_String(cstring_t name, cstring_t fallback) {
    FOR_LOOP(i, MOCK_CVAR_COUNT) {
        if (mock_cvars[i].name[0] && !strcmp(mock_cvars[i].name, name))
            return mock_cvars[i].value;
    }
    return fallback;
}

cvar_t *Cvar_Set(cstring_t name, cstring_t value) {
    test_client_stubs_set_cvar(name, value);
    return NULL;
}

void CL_ParseTEnt(LPSIZEBUF msg) { (void)msg; }
void CL_BeginLoadingMap(cstring_t mapName) { (void)mapName; cl.playerstate.client_ui_state = CLIENT_UI_LOADING; cls.state = ca_connected; cl.num_active = 0; }
void CL_SetGameplayInput(void) { cls.key_dest = key_game; }
void CL_ReloadImageResources(void) {}
void CL_Disconnect(cstring_t reason, bool notify) { (void)reason; (void)notify; cls.state = ca_disconnected; }
void CL_EntityEvent(entityState_t const *ent) { (void)ent; }
void S_RegisterSound(cstring_t path) { (void)path; }
void S_PlaySoundFile(cstring_t path) { (void)path; }
void S_PlaySoundPacket(cstring_t path, LPCVECTOR3 origin, bool positioned, int channel, float volume, float attenuation,
                       float timeofs) {
    (void)path; (void)origin; (void)positioned; (void)channel; (void)volume; (void)attenuation; (void)timeofs;
}
bool S_PlaySoundPolicy(cstring_t path, LPCVECTOR3 origin, bool positioned, int channel, float volume,
                       float attenuation, float timeofs, soundPolicy_t const *policy) {
    (void)policy;
    S_PlaySoundPacket(path, origin, positioned, channel, volume, attenuation, timeofs);
    return true;
}
void Cbuf_AddText(cstring_t text) { (void)text; }
void Cbuf_ClearDefer(void) {}
void Cbuf_InsertFromDefer(void) {}
int Cmd_Argc(void) { return 0; }
cstring_t Cmd_Argv(int arg) { (void)arg; return ""; }
cstring_t Cmd_ArgsFrom(int arg) { (void)arg; return ""; }
void Cmd_AddCommand(cstring_t name, xcommand_t function) { (void)name; (void)function; }
void MenuAction(cstring_t action, cstring_t arg) {
    snprintf(test_menu_action, sizeof(test_menu_action), "%s", action ? action : "");
    snprintf(test_menu_action_arg, sizeof(test_menu_action_arg), "%s", arg ? arg : "");
}
void CL_QueueMovie(cstring_t path) { (void)path; }
bool CL_MovieActive(void) { return false; }
void CL_MovieDraw(void) {}
void CL_MusicSetMap(cstring_t playlist, bool random, int32_t index, uint32_t session_id) { (void)playlist; (void)random; (void)index; (void)session_id; }
void CL_MusicClearMap(void) {}
void CL_MusicPlay(cstring_t playlist, bool random, int32_t index, int32_t start_ms, int32_t fade_ms, uint32_t played_mask, uint32_t session_id) { (void)playlist; (void)random; (void)index; (void)start_ms; (void)fade_ms; (void)played_mask; (void)session_id; }
void CL_MusicStop(bool fade_out) { (void)fade_out; }
void CL_MusicResume(void) {}
void CL_MusicPlayThematic(cstring_t playlist, int32_t index, int32_t start_ms, uint32_t session_id) { (void)playlist; (void)index; (void)start_ms; (void)session_id; }
void CL_MusicEndThematic(void) {}
void CL_MusicSetVolume(int32_t volume) { (void)volume; }
void CL_MusicSetPosition(int32_t millisecs) { (void)millisecs; }
void CL_MusicSetThematicVolume(int32_t volume) { (void)volume; }
void CL_MusicSetThematicPosition(int32_t millisecs) { (void)millisecs; }
void Cmd_ForwardToServer(cstring_t text) {
    snprintf(test_forwarded_command, sizeof(test_forwarded_command), "%s", text ? text : "");
}
unsigned int SDL_GetTicks(void) { return 0; }
int SDL_ShowCursor(int toggle) { (void)toggle; return 1; }
void Com_Error(errorCode_t code, cstring_t fmt, ...) { (void)code; (void)fmt; }

void test_client_stubs_init(void) {
    SAFE_DELETE(cl.loading.data, MemFree);
    memset(&cl, 0, sizeof(cl));
    memset(&cls, 0, sizeof(cls));
    memset(&re, 0, sizeof(re));
    memset(&menu, 0, sizeof(menu));
    memset(&mouse, 0, sizeof(mouse));
    test_fow_upload_calls = 0;
    test_cursor_draw_calls = 0;
    test_cursor_tint = COLOR32_WHITE;
    test_forwarded_command[0] = '\0';
    test_menu_action[0] = '\0';
    test_menu_action_arg[0] = '\0';
    test_console_message[0] = '\0';
    test_existing_file[0] = '\0';
    test_world_bounds = (BOX2){ 0 };
    test_window_size = MAKE(size2_t, 1024, 768);
    test_canvas_policy = UI_CANVAS_POLICY;
    test_ui_scene = (rect_t){ 0 };
    re.GetWindowSize = mock_GetWindowSize;
    re.SetUIScene = mock_SetUIScene;
    re.CameraUsesTerrainHeight = mock_CameraUsesTerrainHeight;
    re.DrawLoadingIndicator = mock_DrawLoadingIndicator;
    re.DrawFill = mock_DrawFill;
    re.DrawImageEx = mock_DrawImageEx;
    re.DrawCursor = mock_DrawCursor;
    CL_CanvasInit();
}

/* Window changes reach the canvas the way SDL events do: resolved at once, before any hit test. */
void test_client_stubs_set_window_size(uint32_t width, uint32_t height) {
    test_window_size = MAKE(size2_t, width, height);
    CL_CanvasWindowChanged();
}

handle_t MemAlloc(long size) {
    void *p = malloc((size_t)size);
    if (p) memset(p, 0, (size_t)size);
    return p;
}
void MemFree(handle_t p) { free(p); }
