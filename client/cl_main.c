/*
 * cl_main.c — Main client loop and initialization.
 *
 * The client is responsible for three things:
 *   1. Capturing user input and forwarding commands to the server.
 *   2. Receiving game state snapshots from the server and applying them.
 *   3. Preparing and rendering the scene each frame.
 *
 * CL_Frame() is the entry point called from the platform event loop.
 * CL_Init() sets up the renderer and input bindings at startup.
 */
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include "client.h"
#include "cl_control_groups.h"
#include "cl_input_local.h"
#include "../common/video_modes.h"
#include "tr_public.h"
#include "ui_layout.h"
#include "sound/s_local.h"
#include "common/server_api.h"
#ifdef BZ_TESTS
#include "shared/test.h"
#endif

refExport_t re;
menuExport_t menu;

struct client_static cls;
struct client_state cl;

#define CL_TIMEOUT_MSEC 10000
#define CL_LOADING_PUMP_MSEC 16 // milliseconds; one 60 Hz platform pump; bounds checkpoint overhead during loading

typedef struct { cstring_t name; xcommand_t call; } clMenuCommand_t;
static clMenuCommand_t cl_menu_commands[128];
typedef enum { CL_MENU_UNLOADED, CL_MENU_READY, CL_MENU_SUSPENDED } clMenuLife_t;
static clMenuLife_t cl_menu_life;

static uint32_t cl_last_packet_time = 0;
static uint32_t cl_realtime = 0;

typedef enum {
    CL_MENU_ACTION_NONE,
    CL_MENU_ACTION_MAP,
    CL_MENU_ACTION_MENU,
    CL_MENU_ACTION_LOAD,
    CL_MENU_ACTION_QUIT,
} clMenuActionType_t;

typedef struct {
    clMenuActionType_t type;
    PATHSTR arg;
} clPendingMenuAction_t;

static clPendingMenuAction_t cl_pending_menu_action;
static clPendingMenuAction_t cl_movie_deferred_action;
static PATHSTR cl_pending_movie;

void Cmd_ForwardToServer(cstring_t text) {
    if (cls.state <= ca_connected || *text == '-' || *text == '+') {
        fprintf(stderr, "Unknown command \"%s\"\n", text);
        CON_printf("Unknown command \"%s\"", text);
        return;
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", text);
}

cstring_t CL_GetConfigString(uint32_t index) {
    return cl.configstrings[index];
}

void CL_ClientCommand(cstring_t cmd) {
    memset(cls.netchan.message.data, 0, cls.netchan.message.maxsize);
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", cmd);
}

void CL_ClearState(void) {
    S_StopAllSounds();
    S_ClearSoundEvents();
    CL_MusicReset();
    CL_ClearTEnts ();
    CL_WindowClear();
    CL_ClearMinimap();

    SAFE_DELETE(cl.loading.data, MemFree);
    SAFE_DELETE(cl.fow.visible, MemFree);
    SAFE_DELETE(cl.fow.explored, MemFree);
    SAFE_DELETE(cl.fow.texture, MemFree);
    SAFE_DELETE(cl.terrain_mask.cells, MemFree);
    SAFE_DELETE(cl.minimap_model, re.ReleaseModel);
    SAFE_DELETE(cl.moveConfirmation, re.ReleaseModel);
    FOR_LOOP(model, MAX_MODELS) {
        SAFE_DELETE(cl.models[model], re.ReleaseModel);
        SAFE_DELETE(cl.portraits[model], re.ReleaseModel);
    }
    FOR_LOOP(image, MAX_IMAGES) {
        if (cl.pics[image]) {
            re.ReleaseTexture((texture_t *)cl.pics[image]);
            cl.pics[image] = NULL;
        }
    }
    FOR_LOOP(image, MAX_DYNAMIC_IMAGES) {
        SAFE_DELETE(cl.dynamicPics[image], re.ReleaseTexture);
    }
    SCR_ClearLayoutResources();

    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
        SCR_ClearLayoutLayer(layer);
    }

    /* Release per-map model ownership before advancing the renderer's
     * registration sequence, and clear any map-archive asset scope so
     * menus cannot inherit the previous level's imported overrides. */
    re.RegisterMap(NULL);

    memset(&cl, 0, sizeof(struct client_state));
    CL_ControlGroupsReset();

    SZ_Clear (&cls.netchan.message);
}

/* Forward declarations for UI callbacks */
static void CL_UIServerCommand(cstring_t text);
static void CL_LANRefreshServers(void);
static uint32_t CL_LANNumServers(void);
static bool CL_LANServer(uint32_t index, menuLanGame_t *out);
static void CL_LANConnectServer(uint32_t index);
static refExport_t *CL_UIGetRenderer(void);

static void CL_SuspendMenu(void) {
    if (cl_menu_life != CL_MENU_READY) return;
    menu.Shutdown();
    cl_menu_life = CL_MENU_SUSPENDED;
}

static void CL_ResumeMenu(void) {
    if (cl_menu_life != CL_MENU_SUSPENDED) return;
    menu.Init();
    cl_menu_life = CL_MENU_READY;
}

static void CL_RunMenuCommand(void) {
    if (!CL_MenuActive()) {
        fprintf(stderr, "Menu command %s rejected while a world owns presentation\n", Cmd_Argv(0));
        return;
    }
    FOR_LOOP(i, sizeof(cl_menu_commands) / sizeof(cl_menu_commands[0])) {
        if (cl_menu_commands[i].name && !strcmp(cl_menu_commands[i].name, Cmd_Argv(0))) {
            cl_menu_commands[i].call();
            return;
        }
    }
}

static void CL_AddMenuCommand(cstring_t name, xcommand_t call) {
    FOR_LOOP(i, sizeof(cl_menu_commands) / sizeof(cl_menu_commands[0])) {
        clMenuCommand_t *cmd = &cl_menu_commands[i];
        if (cmd->name && strcmp(cmd->name, name)) continue;
        if (!cmd->name) Cmd_AddCommand(name, CL_RunMenuCommand);
        *cmd = (clMenuCommand_t){ .name = name, .call = call };
        return;
    }
    Com_Error(ERR_FATAL, "Too many main-menu commands");
}

static void CL_MenuCommand(cstring_t command) {
    if (!command || !*command) {
        return;
    }
    Cbuf_AddText(command);
    Cbuf_AddText("\n");
}

static void CL_DisconnectInternal(cstring_t reason, bool notify, bool queue_menu) {
    Cbuf_ClearDefer();
    if (cls.state == ca_disconnected) {
        return;
    }

    fprintf(stderr, "CL_Disconnect: %s\n", reason && *reason ? reason : "disconnected");

    if (cls.state >= ca_connected) {
        SZ_Clear(&cls.netchan.message);
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        MSG_WriteString(&cls.netchan.message, "disconnect");
        Netchan_Transmit(NS_CLIENT, &cls.netchan);
    }

    CL_ClearState();
    cls.state = ca_disconnected;
    cl_last_packet_time = 0;
    CL_SetMenuBindings();

    if (!queue_menu) {
        return;
    }
    Cvar_Set("map", "");
    CL_ResumeMenu();
    if (notify) {
        CL_MenuCommand("menu_disconnected");
    } else {
        CL_MenuCommand("menu_main");
    }
}

void CL_Disconnect(cstring_t reason, bool notify) {
    CL_DisconnectInternal(reason, notify, true);
}

/* UI library FS_ReadFile wrapper — tries engine filesystem first,
 * then falls back to a raw CWD fopen so share/ files written by
 * CL_UI_WriteFile (which writes relative to CWD) are readable. */
static int CL_UI_ReadFile(cstring_t fileName, void **buf) {
    int size = FS_ReadFileQ3(fileName, buf);
    if (size > 0 || !buf) return size;
    FILE *f = fopen(fileName, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return -1; }
    *buf = MemAlloc((size_t)len + 1);
    if (!*buf) { fclose(f); return -1; }
    fread(*buf, 1, (size_t)len, f);
    ((char *)*buf)[len] = '\0';
    fclose(f);
    return (int)len;
}

/* Write a local file by path (relative to CWD, same as share/ configs). */
static void CL_UI_WriteFile(cstring_t path, void const *data, int size) {
    FILE *f;
    if (!path || !data || size <= 0) return;
    f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, (size_t)size, f);
    fclose(f);
}

static bool CL_UI_HasExtension(cstring_t name, cstring_t extension) {
    cstring_t dot;

    if (!extension || !*extension) {
        return true;
    }
    dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, extension);
}

static int CL_UI_CompareFileNames(void const *a, void const *b) {
    return strcasecmp((cstring_t)a, (cstring_t)b);
}

static bool CL_UI_ListHasFile(PATHSTR *files, int count, cstring_t name) {
    for (int i = 0; i < count; i++) {
        if (!strcasecmp(files[i], name)) {
            return true;
        }
    }
    return false;
}

typedef struct {
    cstring_t extension;
    char *listbuf;
    int bufsize;
    int used;
    int count;
} clUiFileList_t;

static void CL_UI_AddMapFile(cstring_t path, void *userData) {
    clUiFileList_t *list = userData;
    int len;

    if (!list || !CL_UI_HasExtension(path, list->extension)) {
        return;
    }
    len = (int)strlen(path) + 1;
    if (list->used + len < list->bufsize) {
        memcpy(list->listbuf + list->used, path, len);
        list->used += len;
    }
    list->count++;
}

static int CL_UI_GetFileList(cstring_t path, cstring_t extension, char *listbuf, int bufsize) {
    enum { MAX_UI_FILELIST = 1024 };
    PATHSTR files[MAX_UI_FILELIST];
    char mask[MAX_PATHLEN * 2];
    sfileFindData_t find;
    handle_t handle;
    int count = 0;
    int used = 0;

    if (!path || !*path || !listbuf || bufsize <= 0) {
        return 0;
    }
    listbuf[0] = '\0';
    if (!strcasecmp(path, "Maps")) {
        clUiFileList_t list = { extension, listbuf, bufsize, 0, 0 };

        FS_ListMaps(CL_UI_AddMapFile, &list);
        if (list.used < list.bufsize) {
            list.listbuf[list.used] = '\0';
        }
        return list.count;
    }
    snprintf(mask, sizeof(mask), "%s\\*", path);

    handle = FS_FindFirstFile(mask, &find);
    while (handle && count < MAX_UI_FILELIST) {
        if (CL_UI_HasExtension(find.cFileName, extension) &&
            !CL_UI_ListHasFile(files, count, find.cFileName)) {
            strlcpy(files[count], find.cFileName, sizeof(files[count]));
            for (char *p = files[count]; *p; p++) {
                if (*p == '/') {
                    *p = '\\';
                }
            }
            count++;
        }
        if (!FS_FindNextFile(handle, &find)) {
            break;
        }
    }
    if (handle) {
        FS_FindClose(handle);
    }

    qsort(files, count, sizeof(files[0]), CL_UI_CompareFileNames);
    for (int i = 0; i < count; i++) {
        int len = (int)strlen(files[i]) + 1;
        if (used + len >= bufsize) {
            break;
        }
        memcpy(listbuf + used, files[i], len);
        used += len;
    }
    if (used < bufsize) {
        listbuf[used] = '\0';
    }
    return count;
}

static void CL_UIServerCommand(cstring_t text) {
    if (!text || !*text || *text == '-' || *text == '+') {
        return;
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", text);
}

/* Renderer access callback for UI rendering */
static refExport_t *CL_UIGetRenderer(void) {
    return &re;
}

#define CL_MAX_LAN_SERVERS 64

static menuLanGame_t cl_lan_servers[CL_MAX_LAN_SERVERS];
static uint32_t cl_num_lan_servers;

static void CL_InfoValue(cstring_t info, cstring_t key, string_t out, uint32_t out_size) {
    char needle[64];
    cstring_t cursor;
    cstring_t value;
    cstring_t end;
    size_t len;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!info || !key || !*key) {
        return;
    }

    snprintf(needle, sizeof(needle), "\\%s\\", key);
    cursor = strstr(info, needle);
    if (!cursor) {
        return;
    }
    value = cursor + strlen(needle);
    end = strchr(value, '\\');
    len = end ? (size_t)(end - value) : strlen(value);
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, value, len);
    out[len] = '\0';
}

static void CL_LANRefreshServers(void) {
    netadr_t adr;
    unsigned short port = (unsigned short)Cvar_Integer("game_port", PORT_SERVER);
    bool const open_client_socket = !NET_IsConfigured(NS_CLIENT);

    memset(cl_lan_servers, 0, sizeof(cl_lan_servers));
    cl_num_lan_servers = 0;
    if (open_client_socket) {
        fprintf(stderr, "CL_LANRefreshServers: opening client UDP socket for LAN queries\n");
    }
    NET_ConfigSource(NS_CLIENT, true);
    if (!NET_IsConfigured(NS_CLIENT)) {
        fprintf(stderr, "CL_LANRefreshServers: client UDP socket is closed, cannot query LAN servers\n");
        return;
    }

    memset(&adr, 0, sizeof(adr));
    adr.type = NA_BROADCAST;
    adr.port = htons(port);
    Netchan_OutOfBandPrint(NS_CLIENT, adr, "info");
}

static uint32_t CL_LANNumServers(void) {
    return cl_num_lan_servers;
}

static bool CL_LANServer(uint32_t index, menuLanGame_t *out) {
    if (!out || index >= cl_num_lan_servers) {
        return false;
    }
    *out = cl_lan_servers[index];
    return true;
}

static void CL_LANConnectServer(uint32_t index) {
    menuLanGame_t *game;
    unsigned short port = (unsigned short)Cvar_Integer("game_port", PORT_SERVER);

    if (index >= cl_num_lan_servers) {
        return;
    }
    game = &cl_lan_servers[index];
    Cvar_Set("connect", game->address);
    CL_Connect(game->address, port);
}

static void CL_AddLANServer(netadr_t const *from, cstring_t info) {
    menuLanGame_t *game;
    char value[128];
    cstring_t address;

    if (!from || !info) {
        return;
    }
    address = NET_AdrToString(from);
    FOR_LOOP(i, cl_num_lan_servers) {
        if (!strcmp(cl_lan_servers[i].address, address)) {
            game = &cl_lan_servers[i];
            goto update;
        }
    }
    if (cl_num_lan_servers >= CL_MAX_LAN_SERVERS) {
        fprintf(stderr, "CL_AddLANServer: ignoring %s, LAN server list is full\n", address);
        return;
    }
    game = &cl_lan_servers[cl_num_lan_servers++];
    memset(game, 0, sizeof(*game));
    snprintf(game->address, sizeof(game->address), "%s", address);

update:
    CL_InfoValue(info, "hostname", game->hostname, sizeof(game->hostname));
    CL_InfoValue(info, "mapname", game->mapname, sizeof(game->mapname));
    CL_InfoValue(info, "players", value, sizeof(value));
    game->players = (uint32_t)atoi(value);
    CL_InfoValue(info, "maxplayers", value, sizeof(value));
    game->maxPlayers = (uint32_t)atoi(value);
    CL_InfoValue(info, "speed", value, sizeof(value));
    game->speed = (uint32_t)atoi(value);
    CL_InfoValue(info, "slots", value, sizeof(value));
    game->slots = (uint32_t)atoi(value);
    if (!game->hostname[0]) {
        snprintf(game->hostname, sizeof(game->hostname), "%s", "OpenWarcraft3");
    }
}

static void CL_UICvarSet(cstring_t name, cstring_t value) {
    Cvar_Set(name, value);
}

void CL_SetLoadingProgress(float progress) {
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    if (progress <= cl.loading_progress) return;
    cl.loading_progress = progress;
    SCR_UpdateLoadingPlaque();
}

void CL_BeginLoadingMap(cstring_t mapName) {
    /* Release glue-owned caches before the renderer registers a different world. */
    CL_SuspendMenu();
    /* Per-map input conveniences must never retain entity numbers into the
     * next world, where those numbers may refer to unrelated entities. */
    CL_ResetInput();
    CL_ControlGroupsReset();
    /* Publish the resolved map before freezing the plaque; menu launches have no startup map cvar. */
    Cvar_Set("map", mapName);
    /* Same-map load keeps CS_WORLD unchanged; forget the previous world's begin
     * so PrepRefresh sends it again and CL_ParseFrame can end the plaque. */
    CL_RestartRefresh();
    cl.loading_progress = 0.0f;
    cl.precache_ready = false;
    cl.playerstate.client_ui_state = CLIENT_UI_LOADING;
    cls.state = ca_connected;
    CL_SetGameplayInput();
    SCR_BeginLoadingPlaque();
    /* New map baselines repopulate the compact active-entity list; drop any
     * stale entries from the previous map before they arrive. */
    cl.num_active = 0;
}

int CL_ModelIndex(cstring_t modelName) {
    /* Find or register model */
    for (uint32_t i = 1; i < MAX_MODELS; i++) {
        if (cl.models[i] == NULL) {
            cl.models[i] = re.LoadModel(modelName);
            return (int)i;
        }
        /* Check if already loaded - compare configstring */
        if (*cl.configstrings[CS_MODELS + i] && !strcmp(cl.configstrings[CS_MODELS + i], modelName)) {
            return (int)i;
        }
    }
    return 0;
}

int CL_ImageIndex(cstring_t imageName) {
    /* Find or register image */
    for (uint32_t i = 1; i < MAX_IMAGES; i++) {
        if (cl.pics[i] == NULL) {
            cl.pics[i] = re.LoadTexture(imageName);
            return (int)i;
        }
        /* Check if already loaded */
        if (*cl.configstrings[CS_IMAGES + i] && !strcmp(cl.configstrings[CS_IMAGES + i], imageName)) {
            return (int)i;
        }
    }
    return 0;
}

int CL_FontIndex(cstring_t fontName, uint32_t fontSize) {
    /* Create font spec string */
    char fontspec[256];
    snprintf(fontspec, sizeof(fontspec), "%s,%u", fontName, fontSize);
    
    /* Find or register font */
    for (uint32_t i = 1; i < MAX_FONTSTYLES; i++) {
        if (cl.fonts[i] == NULL) {
            cl.fonts[i] = re.LoadFont(fontName, fontSize);
            return (int)i;
        }
        /* Check if already loaded */
        if (*cl.configstrings[CS_FONTS + i] && !strcmp(cl.configstrings[CS_FONTS + i], fontspec)) {
            return (int)i;
        }
    }
    return 0;
}

void CL_UIMenuCommand(cstring_t command) {
    CON_printf("CL_UIMenuCommand: %s\\n", command);
    CL_MenuCommand(command);
}

static videoMode_t CL_VideoMode(void) { return *video_mode_get(Cvar_Integer("vid_mode", BZ_VIDEO_MODE_DEFAULT)); }

static void CL_VideoApply_f(void) {
    videoMode_t mode = CL_VideoMode();

    if (re.SetWindowSize) {
        re.SetWindowSize(mode.width, mode.height);
    }
    /* Mode changes resize the window without waiting for the SDL event to be polled. */
    CL_CanvasWindowChanged();
}

static cstring_t CL_RebuildMenuTarget(cstring_t target) {
    return target && *target ? target : "menu_main";
}

static void CL_RebuildMenu(cstring_t target) {
    Cvar_Set("map", "");
    CL_SuspendMenu();
    re.RegisterMap(NULL);
    /* An edition switch changes which archives are visible, and with them the authored widescreen chrome. */
    CL_CanvasResolvePolicy();
    CL_ResumeMenu();

    /* M_Init deliberately installs no disconnected glue screen.  Every
     * rebuild therefore has to select its destination explicitly, including
     * menu_main.  Skipping the main-menu command leaves a valid but empty UI
     * after menu_restart (for example when switching RoC/TFT editions). */
    CL_MenuCommand(CL_RebuildMenuTarget(target));
}

static void CL_MenuRestart_f(void) {
    if (cls.state != ca_disconnected) {
        return;
    }
    CL_RebuildMenu("menu_main");
}

static void CL_Quit_f(void) {
    CL_Shutdown();
    Com_Quit();
}

/* Game modules request session-boundary actions through gi.MenuAction while
 * their simulation/JASS call stack may still be active.  Never tear down or
 * replace the current world inline from that callback.  Copy the request and
 * execute it from the following client frame, after SV_Frame has returned. */
void MenuAction(cstring_t action, cstring_t arg) {
    clPendingMenuAction_t pending = { 0 };

    if (!action || !*action) return;
    if (!strcmp(action, "map")) {
        if (!arg || !*arg) return;
        if (!Com_ResolveMapArgument(arg, pending.arg, sizeof(pending.arg))) return;
        pending.type = CL_MENU_ACTION_MAP;
    } else if (!strcmp(action, "menu")) {
        pending.type = CL_MENU_ACTION_MENU;
        if (arg && *arg) strlcpy(pending.arg, arg, sizeof(pending.arg));
    } else if (!strcmp(action, "load")) {
        if (!arg || !*arg) return;
        pending.type = CL_MENU_ACTION_LOAD;
        strlcpy(pending.arg, arg, sizeof(pending.arg));
    } else if (!strcmp(action, "quit")) {
        pending.type = CL_MENU_ACTION_QUIT;
    } else {
        return;
    }

    if (cl_pending_menu_action.type != CL_MENU_ACTION_NONE) {
        fprintf(stderr, "MenuAction: replacing pending session action %u with %u\n",
                (unsigned)cl_pending_menu_action.type, (unsigned)pending.type);
    }
    cl_pending_menu_action = pending;
}

void CL_QueueMovie(cstring_t path) {
    if (!path || !*path) return;
    if (cl_pending_movie[0]) {
        fprintf(stderr, "CL_QueueMovie: replacing pending movie %s with %s\n", cl_pending_movie, path);
    }
    snprintf(cl_pending_movie, sizeof(cl_pending_movie), "%s", path);
}

static void CL_ProcessPendingMenuAction(void) {
    clPendingMenuAction_t pending;

    if (CL_MovieActive()) return;

    if (cl_movie_deferred_action.type != CL_MENU_ACTION_NONE) {
        pending = cl_movie_deferred_action;
        memset(&cl_movie_deferred_action, 0, sizeof(cl_movie_deferred_action));
        if (SV_IsActive()) SV_SetPaused(false);
        goto execute;
    }
    if (cl_pending_menu_action.type == CL_MENU_ACTION_NONE) return;

    /* Clear first: the transition may initialize a new game module which can
     * itself publish a later session action without being overwritten here. */
    pending = cl_pending_menu_action;
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));

    /* PlayCinematic is a session-boundary interposer: preserve the requested
     * map/menu transition, freeze the outgoing simulation, then execute the
     * transition only after the movie ends or is skipped. */
    if (cl_pending_movie[0]) {
        PATHSTR movie;
        snprintf(movie, sizeof(movie), "%s", cl_pending_movie);
        cl_pending_movie[0] = '\0';
        cl_movie_deferred_action = pending;
        if (SV_IsActive()) SV_SetPaused(true);
        if (CL_PlayMovie(movie)) return;
        if (SV_IsActive()) SV_SetPaused(false);
        pending = cl_movie_deferred_action;
        memset(&cl_movie_deferred_action, 0, sizeof(cl_movie_deferred_action));
    }

execute:
    switch (pending.type) {
    case CL_MENU_ACTION_MAP:
        CL_SetGameplayBindings();
        CL_BeginLoadingMap(pending.arg);
        SV_Map(pending.arg);
        if (SV_IsActive()) CL_SetLoadingProgress(0.05f);
        break;
    case CL_MENU_ACTION_MENU:
        /* Returning from a world is a session boundary, not an in-place menu
         * navigation.  Shut the client/server world down first, then rebuild
         * the menu after clearing the map asset scope so FDF/model state is
         * valid again even after a runtime menu restart. */
        CL_DisconnectInternal("Game ended.", false, false);
        SV_Shutdown();
        CL_RebuildMenu(pending.arg[0] ? pending.arg : "menu_main");
        break;
    case CL_MENU_ACTION_LOAD: {
        PATHSTR map;
        if (!SV_GetSaveMap(pending.arg, map, sizeof(map))) break;
        CL_SetGameplayBindings();
        CL_BeginLoadingMap(map);
        SV_LoadGame(pending.arg, map);
        break;
    }
    case CL_MENU_ACTION_QUIT:
        CL_Quit_f();
        break;
    case CL_MENU_ACTION_NONE:
        break;
    }
}

#ifdef BZ_TESTS
TEST(client_loading, progress_is_clamped_and_monotonic) {
    float saved = cl.loading_progress;
    uint32_t saved_disable_screen = cls.disable_screen;

    cls.disable_screen = 0;
    cl.loading_progress = 0.0f;
    CL_SetLoadingProgress(0.25f);
    T_FEQ(cl.loading_progress, 0.25f, 0.0001f);
    CL_SetLoadingProgress(0.10f);
    T_FEQ(cl.loading_progress, 0.25f, 0.0001f);
    CL_SetLoadingProgress(2.0f);
    T_FEQ(cl.loading_progress, 1.0f, 0.0001f);

    cl.loading_progress = saved;
    cls.disable_screen = saved_disable_screen;
}

static uint32_t cl_test_menu_calls;
static void CL_TestMenuCommand(void) { cl_test_menu_calls++; }
TEST(client_session, menu_commands_cannot_enter_loading_or_active_world) {
    connstate_t state = cls.state;
    uint32_t ui = cl.playerstate.client_ui_state;
    CL_AddMenuCommand("test_menu_boundary", CL_TestMenuCommand); cl_test_menu_calls = 0;
    cls.state = ca_active; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    Cmd_ExecuteString("test_menu_boundary"); T_EQ(cl_test_menu_calls, 0);
    cls.state = ca_connected; cl.playerstate.client_ui_state = CLIENT_UI_LOADING;
    Cmd_ExecuteString("test_menu_boundary"); T_EQ(cl_test_menu_calls, 0);
    cls.state = ca_disconnected; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    Cmd_ExecuteString("test_menu_boundary"); T_EQ(cl_test_menu_calls, 1);
    Cmd_RemoveCommand("test_menu_boundary");
    FOR_LOOP(i, sizeof(cl_menu_commands) / sizeof(cl_menu_commands[0]))
        if (cl_menu_commands[i].name && !strcmp(cl_menu_commands[i].name, "test_menu_boundary"))
            cl_menu_commands[i] = (clMenuCommand_t){0};
    cls.state = state; cl.playerstate.client_ui_state = ui;
}

TEST(client_session, menu_action_map_is_deferred_until_client_frame) {
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));

    MenuAction("map", "Maps\\Campaign\\Human02.w3m");

    T_EQ(cl_pending_menu_action.type, CL_MENU_ACTION_MAP);
    T_STREQ(cl_pending_menu_action.arg, "Maps\\Campaign\\Human02.w3m");

    /* Do not call CL_ProcessPendingMenuAction here: the regression contract is
     * specifically that MenuAction itself cannot enter SV_Map re-entrantly. */
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
}

TEST(client_session, menu_action_named_load_is_deferred_until_client_frame) {
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));

    MenuAction("load", "chapter-01");

    T_EQ(cl_pending_menu_action.type, CL_MENU_ACTION_LOAD);
    T_STREQ(cl_pending_menu_action.arg, "chapter-01");

    /* Loading rebuilds the server/map, so MenuAction must only capture the
     * selected save while the gameplay-window callback is still active. */
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
}

TEST(client_session, menu_action_menu_is_deferred_until_client_frame) {
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));

    MenuAction("menu", "menu_main");

    T_EQ(cl_pending_menu_action.type, CL_MENU_ACTION_MENU);
    T_STREQ(cl_pending_menu_action.arg, "menu_main");

    /* Client-owned leave buttons use this path so they cannot rebuild FDF/menu
     * state while the gameplay window input callback is still on the stack. */
    memset(&cl_pending_menu_action, 0, sizeof(cl_pending_menu_action));
}

static uint32_t cl_test_menu_shutdown_count;
static uint32_t cl_test_menu_init_count;
static uint32_t cl_test_register_map_count;
static bool cl_test_register_map_was_null;

static void CL_TestMenuShutdown(void) {
    cl_test_menu_shutdown_count++;
}

static void CL_TestMenuInit(void) {
    cl_test_menu_init_count++;
}

static void CL_TestRegisterMap(cstring_t map) {
    cl_test_register_map_count++;
    cl_test_register_map_was_null = map == NULL;
}

/* Dedicated test runs have no renderer; the menu rebuild re-resolves the canvas, which pushes its scene. */
static void CL_TestSetUIScene(rect_t const *scene) { (void)scene; }

TEST(client_session, menu_resources_suspend_once_and_resume_once) {
    clMenuLife_t old_life = cl_menu_life;
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    PATHSTR old_map;
    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    snprintf(old_map, sizeof(old_map), "%s", Cvar_String("map", ""));
    cls.state = ca_disconnected; cls.disable_screen = 1;
    menuExport_t old_menu = menu;
    menu.Shutdown = CL_TestMenuShutdown; menu.Init = CL_TestMenuInit;
    cl_test_menu_shutdown_count = cl_test_menu_init_count = 0;
    cl_menu_life = CL_MENU_UNLOADED;
    CL_SuspendMenu(); CL_ResumeMenu();
    T_EQ(cl_test_menu_shutdown_count, 0); T_EQ(cl_test_menu_init_count, 0);
    cl_menu_life = CL_MENU_READY;
    CL_BeginLoadingMap("test.map"); CL_BeginLoadingMap("test.map");
    T_EQ(cl_test_menu_shutdown_count, 1); T_EQ(cl_menu_life, CL_MENU_SUSPENDED);
    CL_ResumeMenu(); CL_ResumeMenu();
    T_EQ(cl_test_menu_init_count, 1); T_EQ(cl_menu_life, CL_MENU_READY);
    menu = old_menu; cl_menu_life = old_life;
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls; Cvar_Set("map", old_map);
}

TEST(client_session, menu_rebuild_defaults_to_main_menu_target) {
    T_STREQ(CL_RebuildMenuTarget(NULL), "menu_main");
    T_STREQ(CL_RebuildMenuTarget(""), "menu_main");
    T_STREQ(CL_RebuildMenuTarget("menu_main"), "menu_main");
    T_STREQ(CL_RebuildMenuTarget("menu_single_player_campaign"),
            "menu_single_player_campaign");
}

TEST(client_session, menu_rebuild_clears_world_scope_before_returning_to_menu) {
    clMenuLife_t old_life = cl_menu_life;
    cl_menu_life = CL_MENU_READY;
    void (*old_shutdown)(void) = menu.Shutdown;
    void (*old_init)(void) = menu.Init;
    void (*old_register_map)(cstring_t) = re.RegisterMap;
    void (*old_set_scene)(rect_t const *) = re.SetUIScene;

    cl_test_menu_shutdown_count = 0;
    cl_test_menu_init_count = 0;
    cl_test_register_map_count = 0;
    cl_test_register_map_was_null = false;
    menu.Shutdown = CL_TestMenuShutdown;
    menu.Init = CL_TestMenuInit;
    re.RegisterMap = CL_TestRegisterMap;
    re.SetUIScene = CL_TestSetUIScene;
    Cvar_Set("map", "maps/test.map");

    CL_RebuildMenu("menu_main");

    T_STREQ(Cvar_String("map", NULL), "");
    T_EQ(cl_test_menu_shutdown_count, 1);
    T_EQ(cl_test_register_map_count, 1);
    T_ASSERT(cl_test_register_map_was_null);
    T_EQ(cl_test_menu_init_count, 1);

    cl_menu_life = old_life;
    menu.Shutdown = old_shutdown;
    menu.Init = old_init;
    re.RegisterMap = old_register_map;
    re.SetUIScene = old_set_scene;
}

#endif


static void CL_RendererPlaySoundAt(cstring_t path, vector3_t const *origin, float volume) {
    if (!origin) return;
    S_PlaySoundPacket(path, origin, true, CHAN_AUTO, volume, 1.0f, 0.0f);
}

void CL_Init(void) {
    videoMode_t mode;

    CON_printf("OpenWarcraft3 v0.1");
    fprintf(stderr, "Console initialized.\n");

    re = R_GetAPI((refImport_t) {
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
        .FS_ReadFile = FS_ReadFileQ3,
        .FS_FreeFile = FS_FreeFile,
        .FS_MmapFile = FS_MmapFile,
        .FS_MunmapFile = FS_MunmapFile,
        .FileExtract = FS_ExtractFile,
        .LoadSlk = Stb_SlkLoad,
        .CvarString = Cvar_String,
        .PlaySoundAt = CL_RendererPlaySoundAt,
        .error = CON_printf,
    });

    Cmd_AddCommand("vid_apply", CL_VideoApply_f);
    
    mode = CL_VideoMode();
    re.Init(mode.width, mode.height);
    CL_CanvasInit();
    
    S_Init();
    CL_MusicInit();
    CL_MovieInit();

    /* Initialize UI library */
    menu = M_GetAPI((menuImport_t) {
        .FS_ReadFile = CL_UI_ReadFile,
        .FS_FreeFile = FS_FreeFile,
        .FS_GetFileList = CL_UI_GetFileList,
        .FS_WriteFile = CL_UI_WriteFile,
        .UserPath = FS_UserPath,
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
        .ImageIndex = CL_ImageIndex,
        .ModelIndex = CL_ModelIndex,
        .FontIndex = CL_FontIndex,
        .Cmd_AddCommand = CL_AddMenuCommand,
        .Cmd_Argc = Cmd_Argc,
        .Cmd_Argv = Cmd_Argv,
        .Cmd_ArgsFrom = Cmd_ArgsFrom,
        .Cmd_ExecuteText = Cbuf_AddText,
        .ServerCommand = CL_UIServerCommand,
        .Cvar_String = Cvar_String,
        .Cvar_Set = CL_UICvarSet,
        .GetConfigString = CL_GetConfigString,
        .LAN_RefreshServers = CL_LANRefreshServers,
        .LAN_NumServers = CL_LANNumServers,
        .LAN_Server = CL_LANServer,
        .LAN_ConnectServer = CL_LANConnectServer,
        .GetRenderer = CL_UIGetRenderer,
        .Printf = CON_printf,
        .PlaySound = S_PlaySound,
        .PlaySoundByName = S_PlaySoundByName,
        .PlayMusic = CL_MusicPlayMenu,
        .StopMusic = CL_MusicStopMenu,
        .PlayMovie = CL_PlayMovie,
    });
    
    cl_menu_life = CL_MENU_SUSPENDED;
    CL_ResumeMenu();

    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    
    CL_ClearState();

    Cmd_AddCommand("quit", CL_Quit_f);
    Cmd_AddCommand("screenshot", CL_Screenshot_f);
    Cmd_AddCommand("menu_restart", CL_MenuRestart_f);

    CON_Init();
    CL_InitInput();

    CL_SetMenuBindings();
    cls.state = ca_disconnected;
    scr_initialized = true;
    CL_MenuCommand(Cvar_String("cl_start_menu", "menu_main"));
}

void CL_ConnectionlessPacket(netadr_t const *from, sizeBuf_t *msg) {
    char payload[1024] = { 0 };
    char command[256] = { 0 };
    char *info;
    uint32_t length;
    int protocol = 0;

    if (msg->cursize <= 4) {
        return;
    }
    length = msg->cursize - 4;
    if (length >= sizeof(payload)) {
        length = sizeof(payload) - 1;
    }
    memcpy(payload, msg->data + 4, length);
    sscanf(payload, "%255s %d", command, &protocol);
    if (!strcmp(command, "info")) {
        info = strchr(payload, '\n');
        CL_AddLANServer(from, info ? info + 1 : "");
        return;
    }
    if (strcmp(command, "client_connect")) {
        return;
    }
    if (protocol != BZ_PROTOCOL_VERSION) {
        fprintf(stderr, "CL_ConnectionlessPacket: server protocol %d does not match %d\n", protocol, BZ_PROTOCOL_VERSION);
        return;
    }

    /* Q2 resets the client at the new connection boundary. The server sends
     * the complete configstring/baseline stream again, so old media indices
     * must not be retained or rebound after a same-map save/load. */
    CL_ClearState();
    CL_RestartRefresh();
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteString(&cls.netchan.message, "new");
    if (cls.state < ca_connected) {
        cls.state = ca_connected;
    }
}

#ifdef BZ_TESTS
TEST(client_session, connection_reply_requires_matching_protocol) {
    struct client_state *old_cl = MemAlloc(sizeof(cl));
    struct client_static old_cls = cls;
    void (*old_register_map)(cstring_t) = re.RegisterMap;
    netadr_t loopback = { .type = NA_LOOPBACK };
    cstring_t replies[] = { "client_connect", "client_connect 8",
        "client_connect " BZ_XSTR(BZ_PROTOCOL_VERSION) };
    memcpy(old_cl, &cl, sizeof(cl)); memset(&cl, 0, sizeof(cl));
    re.RegisterMap = CL_TestRegisterMap;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    FOR_LOOP(i, 3) {
        uint8_t bytes[128]; sizeBuf_t msg = { .data = bytes, .maxsize = sizeof(bytes) };
        cls.state = ca_connecting; SZ_Clear(&cls.netchan.message);
        MSG_WriteLong(&msg, -1); MSG_WriteString(&msg, replies[i]);
        CL_ConnectionlessPacket(&loopback, &msg);
        T_EQ(cls.state, i == 2 ? ca_connected : ca_connecting);
        if (i < 2) T_EQ(cls.netchan.message.cursize, 0);
        else {
            cls.netchan.message.readcount = 0;
            T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
            T_STREQ(MSG_ReadString2(&cls.netchan.message), "new");
        }
    }
    re.RegisterMap = old_register_map;
    memcpy(&cl, old_cl, sizeof(cl)); MemFree(old_cl); cls = old_cls;
}
#endif

static void CL_ReadPacketMessage(netadr_t const *from, sizeBuf_t *msg, int length) {
    cl_last_packet_time = cl_realtime;
    if (length >= 4) {
        int hdr;

        memcpy(&hdr, msg->data, sizeof(hdr));
        if (hdr == -1) {
            CL_ConnectionlessPacket(from, msg);
            return;
        }
    }
    CL_ParseServerMessage(msg);
}

/* Read all available server packets from the network buffer and dispatch each
 * message type to the appropriate CL_Parse* handler in cl_parse.c. */
void CL_ReadPackets(void) {
    static uint8_t net_message_buffer[MAX_MSGLEN];
    static sizeBuf_t net_message = {
        .data = net_message_buffer,
        .maxsize = MAX_MSGLEN,
        .cursize = 0,
        .readcount = 0,
    };
    netadr_t from;
    int r;

    while ((r = NET_GetLoopPacket(NS_CLIENT, &from, &net_message)) != 0) {
        CL_ReadPacketMessage(&from, &net_message, r);
    }
    while ((r = NET_GetPacket(NS_CLIENT, &from, &net_message)) != 0) {
        CL_ReadPacketMessage(&from, &net_message, r);
    }
}

/* A synchronous listen-server load may pump presentation packets and native
 * window events, but never commands or game frames.  SDL_PumpEvents services
 * the platform window manager without consuming queued gameplay input. */
void CL_LoadingFrame(void) {
    static uint32_t last_pump;
    uint32_t now;

    if (!scr_initialized || Cvar_Integer("dedicated", 0)) return;
    now = SDL_GetTicks();
    if (!last_pump || now - last_pump >= CL_LOADING_PUMP_MSEC) {
        SDL_PumpEvents();
        last_pump = now;
    }
    CL_ReadPackets();
}

void CL_SendCmd(void) {
    if (cls.state == ca_disconnected || cls.state == ca_connecting) {
        return;
    }
    Netchan_Transmit(NS_CLIENT, &cls.netchan);
}

static void CL_CheckTimeout(void) {
    if (cls.state < ca_connected || cl_last_packet_time == 0) {
        return;
    }
    if (cl_realtime - cl_last_packet_time <= CL_TIMEOUT_MSEC) {
        return;
    }
    CL_Disconnect("Connection to host timed out.", true);
}

/* Set up the netchan to point at a remote server and send an initial
 * connection request.  The server will respond with an out-of-band
 * "client_connect" packet which triggers CL_ConnectionlessPacket(). */
static void CL_SanitizeUserinfoValue(cstring_t in, string_t out, uint32_t out_size) {
    uint32_t write = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    if (!in) {
        return;
    }
    for (; *in && write + 1 < out_size; in++) {
        unsigned char c = (unsigned char)*in;

        if (c == '\\' || c == '\n' || c == '\r') {
            c = ' ';
        }
        if (c < 32) {
            continue;
        }
        out[write++] = (char)c;
    }
    out[write] = '\0';
}

void CL_Connect(cstring_t host, unsigned short port) {
    netadr_t adr;
    UINAME name;

    if (!NET_StringToAdr(host, port, &adr)) {
        fprintf(stderr, "CL_Connect: bad server address \"%s\"\n", host);
        return;
    }
    // Loopback (localhost) needs no UDP socket — packets go through the
    // in-memory ring buffer.  Only open the socket for remote addresses.
    if (adr.type != NA_LOOPBACK) {
        NET_ConfigSource(NS_CLIENT, true);
        if (!NET_IsConfigured(NS_CLIENT)) {
            fprintf(stderr, "CL_Connect: client UDP socket is closed, cannot connect\n");
            return;
        }
    }
    cls.netchan.remote_address = adr;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    Cbuf_CopyToDefer();
    cls.state = ca_connecting;
    // Send an out-of-band "connect" request; the server will register this
    // client slot and reply with "client_connect".
    CL_SanitizeUserinfoValue(Cvar_String("name", "Player"), name, sizeof(name));
    Netchan_OutOfBandPrint(NS_CLIENT, adr, "connect %d\n\\name\\%s", BZ_PROTOCOL_VERSION, name[0] ? name : "Player");
    if (adr.type == NA_LOOPBACK)
        fprintf(stderr, "CL_Connect: connecting to local server via loopback\n");
    else
        fprintf(stderr, "CL_Connect: connecting to %d.%d.%d.%d:%u\n",
                adr.ip[0], adr.ip[1], adr.ip[2], adr.ip[3], ntohs(adr.port));
}

void CL_Shutdown(void) {
    CL_SuspendMenu();
    cl_menu_life = CL_MENU_UNLOADED;
    FOR_LOOP(modelIndex, MAX_MODELS) {
        SAFE_DELETE(cl.models[modelIndex], re.ReleaseModel);
        SAFE_DELETE(cl.portraits[modelIndex], re.ReleaseModel);
    }
    SAFE_DELETE(cl.minimap_model, re.ReleaseModel);
    FOR_LOOP(imageIndex, MAX_DYNAMIC_IMAGES) {
        SAFE_DELETE(cl.dynamicPics[imageIndex], re.ReleaseTexture);
    }
    CL_MovieShutdown();
    CL_MusicShutdown();
    V_Shutdown();
    re.Shutdown();
    S_Shutdown();
}

void CL_SendCommand(void) {
    CL_SendCmd();

    Cbuf_Execute();
}

/* Main client tick called from the platform event loop.
 * Advances the client clock, applies incoming server state, samples input,
 * sends commands, and renders the current frame. */
static void CL_SendSoundEvents(void) {
    soundEvent_t event;
    if (cls.state == ca_connected) return; /* retain loading-time events until active */
    while (cls.state != ca_active || cls.netchan.message.cursize + 64 < cls.netchan.message.maxsize) {
        if (!S_PollSoundEvent(&event)) break;
        if (cls.state != ca_active) continue;
        MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
        SZ_Printf(&cls.netchan.message, "sound_event %u %u %u", event.user, event.request, event.event);
    }
}

void CL_Frame(uint32_t msec) {
    cl_realtime += msec;
    cl.time += msec;

    CL_ProcessPendingMenuAction();
    CL_Input();
    CL_CanvasFrame(cl_realtime);
    CL_MovieUpdate();
    CL_ReadPackets();
    CL_MusicUpdate();
    CL_SendSoundEvents();
    CL_CheckTimeout();
    CL_SendCommand();
    if (cls.state == ca_connected && !cl.refresh_prepped) {
        CL_PrepRefresh();
    } else if (cls.state == ca_active) {
        CL_PrepRefresh();
    }
    SCR_UpdateScreen(msec);
}

#if defined(BZ_TESTS) && defined(BZ_CLIENT_WORLD)
#include "shared/test.h"
TEST(client_sound, playback_receipts_are_reliable_ordered_and_wait_for_buffer_space) {
    sState_t *saved = malloc(sizeof(s));
    sizeBuf_t old_message = cls.netchan.message;
    int old_state = cls.state;
    uint8_t bytes[256]; int16_t out[2];
    sfxcache_t sample = { .length = 1, .loopstart = -1, .data = {1000} };
    soundPolicy_t policy = { .user = 24, .request = 731, .max_channel = 3, .max_total = 24, .max_duplicates = 4 };
    T_NOT_NULL(saved); if (!saved) return;
    *saved = s; memset(&s, 0, sizeof(s)); S_ClearSoundEvents();
    s.initialized = true; s.num_sfx = 1;
    strcpy(s.known_sfx[0].path, "receipt-test.wav"); s.known_sfx[0].cache = &sample;
    SZ_Init(&cls.netchan.message, bytes, 1); cls.state = ca_active;
    T_ASSERT(S_PlaySoundPolicy("receipt-test.wav", NULL, false, 0, 1, 0, 0, &policy));
    S_TestMix(out, 1);
    CL_SendSoundEvents(); T_EQ(cls.netchan.message.cursize, 0); /* full buffer retains events */
    SZ_Init(&cls.netchan.message, bytes, sizeof(bytes));
    cls.state = ca_connected; CL_SendSoundEvents(); T_EQ(cls.netchan.message.cursize, 0);
    cls.state = ca_active; CL_SendSoundEvents();
    FOR_LOOP(i, 3) {
        char expected[64]; snprintf(expected, sizeof(expected), "sound_event 24 731 %u", i + 1);
        T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
        char actual[64]; MSG_ReadString(&cls.netchan.message, actual);
        T_STREQ(actual, expected);
    }
    T_EQ(cls.netchan.message.readcount, cls.netchan.message.cursize);
    S_ClearSoundEvents(); s = *saved; free(saved); cls.netchan.message = old_message; cls.state = old_state;
}
#endif
