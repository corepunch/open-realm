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
#include "client.h"
#include "tr_public.h"
#include <arpa/inet.h>

refExport_t re;
uiExport_t ui;

struct client_static cls;
struct client_state cl;

#define CL_TIMEOUT_MSEC 10000

static DWORD cl_last_packet_time = 0;
static DWORD cl_realtime = 0;

void Cmd_ForwardToServer(LPCSTR text) {
    if (cls.state <= ca_connected || *text == '-' || *text == '+') {
        fprintf(stderr, "Unknown command \"%s\"\n", text);
        CON_printf("Unknown command \"%s\"", text);
        return;
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", text);
}

LPCSTR CL_GetConfigString(DWORD index) {
    return cl.configstrings[index];
}

LPCENTITYSTATE CL_GetEntityByIndex(DWORD number) {
    if (number < cl.num_entities) {
        return &cl.ents[number].current;
    } else {
        return NULL;
    }
}

LPCTEXTURE CL_GetTextureByIndex(DWORD index) {
    if (index < MAX_IMAGES) {
        return cl.pics[index];
    }
    return NULL;
}

static LPCTEXTURE *CL_UIGetTextures(void) {
    return cl.pics;
}

static LPCFONT CL_UIGetFont(DWORD index) {
    if (index < MAX_FONTSTYLES) {
        return cl.fonts[index];
    }
    return NULL;
}

void CL_ClientCommand(LPCSTR cmd) {
    memset(cls.netchan.message.data, 0, cls.netchan.message.maxsize);
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", cmd);
}

void CL_ClearState(void) {
    CL_ClearTEnts ();

    SAFE_DELETE(cl.fow.visible, MemFree);
    SAFE_DELETE(cl.fow.explored, MemFree);
    SAFE_DELETE(cl.fow.texture, MemFree);

    if (ui.ClearLayoutLayer) {
        FOR_LOOP(layer, MAX_LAYOUT_LAYERS) {
            ui.ClearLayoutLayer(layer);
        }
    }

    memset(&cl, 0, sizeof(struct client_state));

    SZ_Clear (&cls.netchan.message);
}

/* Forward declarations for UI callbacks */
static LPCPLAYER CL_UIGetPlayerState(void);
static DWORD CL_UIGetNumEntities(void);
static LPCENTITYSTATE CL_UIGetEntity(DWORD idx);
static void CL_UIServerCommand(LPCSTR text);
static void CL_UIRequestUnitUI(DWORD num_selected, DWORD *entity_nums);
static void CL_LANRefreshServers(void);
static DWORD CL_LANNumServers(void);
static BOOL CL_LANServer(DWORD index, uiLanGame_t *out);
static void CL_LANConnectServer(DWORD index);
static LPCSTR CL_UIGetLoadingMap(void);
static LPCMODEL CL_UIGetModel(DWORD idx);
static LPCMODEL CL_UIGetPortrait(DWORD idx);
static LPRENDERER CL_UIGetRenderer(void);
static DWORD CL_UIGetClientTime(void);
static VECTOR2 CL_UIGetMouseFdf(void);
static DWORD CL_UIGetMouseButton(void);
static uiClientMouseEvent_t CL_UIGetMouseEvent(void);

static void CL_MenuCommand(LPCSTR command) {
    if (!command || !*command) {
        return;
    }
    Cbuf_AddText(command);
    Cbuf_AddText("\n");
}

void CL_Disconnect(LPCSTR reason, BOOL notify) {
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

    if (notify) {
        CL_MenuCommand("menu_disconnected");
    } else {
        CL_MenuCommand("menu_main");
    }
}

static refExport_t CL_GetRendererAPI(refImport_t imp) {
    LPCSTR module = Cvar_String("r_module", "renderer");

    if (module && (!strcmp(module, "stdout") || !strcmp(module, "text"))) {
        return R_StdoutGetAPI(imp);
    }
    if (module && *module && strcmp(module, "renderer")) {
        fprintf(stderr, "Unknown renderer module \"%s\", using renderer\n", module);
    }
    return R_GetAPI(imp);
}

/* UI library FS_ReadFile wrapper that converts to Quake 3 pattern */
static int CL_UI_ReadFile(LPCSTR fileName, void **buf) {
    return FS_ReadFileQ3(fileName, buf);
}

static BOOL CL_UI_HasExtension(LPCSTR name, LPCSTR extension) {
    LPCSTR dot;

    if (!extension || !*extension) {
        return true;
    }
    dot = strrchr(name, '.');
    return dot && !strcasecmp(dot, extension);
}

static int CL_UI_CompareFileNames(const void *a, const void *b) {
    return strcasecmp((LPCSTR)a, (LPCSTR)b);
}

static BOOL CL_UI_ListHasFile(PATHSTR *files, int count, LPCSTR name) {
    for (int i = 0; i < count; i++) {
        if (!strcasecmp(files[i], name)) {
            return true;
        }
    }
    return false;
}

typedef struct {
    LPCSTR extension;
    char *listbuf;
    int bufsize;
    int used;
    int count;
} clUiFileList_t;

static void CL_UI_AddMapFile(LPCSTR path, void *userData) {
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

static int CL_UI_GetFileList(LPCSTR path, LPCSTR extension, char *listbuf, int bufsize) {
    enum { MAX_UI_FILELIST = 1024 };
    PATHSTR files[MAX_UI_FILELIST];
    char mask[MAX_PATHLEN * 2];
    SFILE_FIND_DATA find;
    HANDLE handle;
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
            snprintf(files[count], sizeof(files[count]), "%s", find.cFileName);
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

/* Game state access callbacks for UI library */
static LPCPLAYER CL_UIGetPlayerState(void) {
    return &cl.playerstate;
}

static DWORD CL_UIGetNumEntities(void) {
    return cl.num_entities;
}

static LPCENTITYSTATE CL_UIGetEntity(DWORD idx) {
    if (idx >= MAX_CLIENT_ENTITIES) {
        return NULL;
    }
    return &cl.ents[idx].current;
}

static void CL_UIServerCommand(LPCSTR text) {
    if (!text || !*text || *text == '-' || *text == '+') {
        return;
    }
    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    SZ_Printf(&cls.netchan.message, "%s", text);
}

static LPCMODEL CL_UIGetModel(DWORD idx) {
    if (idx >= MAX_MODELS) {
        return NULL;
    }
    return cl.models[idx];
}

static LPCMODEL CL_UIGetPortrait(DWORD idx) {
    if (idx >= MAX_MODELS) {
        return NULL;
    }
    return cl.portraits[idx];
}

/* Renderer access callback for UI rendering */
static LPRENDERER CL_UIGetRenderer(void) {
    return &re;
}

static DWORD CL_UIGetClientTime(void) {
    return cl.time;
}

static VECTOR2 CL_UIGetMouseFdf(void) {
    return SCR_MouseToFdf();
}

static DWORD CL_UIGetMouseButton(void) {
    return mouse.button;
}

static uiClientMouseEvent_t CL_UIGetMouseEvent(void) {
    switch (mouse.event) {
        case UI_LEFT_MOUSE_DOWN: return UI_CLIENT_MOUSE_LEFT_DOWN;
        case UI_LEFT_MOUSE_UP: return UI_CLIENT_MOUSE_LEFT_UP;
        case UI_LEFT_MOUSE_DRAGGED: return UI_CLIENT_MOUSE_LEFT_DRAGGED;
        case UI_RIGHT_MOUSE_DOWN: return UI_CLIENT_MOUSE_RIGHT_DOWN;
        case UI_RIGHT_MOUSE_UP: return UI_CLIENT_MOUSE_RIGHT_UP;
        case UI_RIGHT_MOUSE_DRAGGED: return UI_CLIENT_MOUSE_RIGHT_DRAGGED;
        default: return UI_CLIENT_MOUSE_NONE;
    }
}

/* Request unit UI data (command card, inventory, build queue) */
static void CL_UIRequestUnitUI(DWORD num_selected, DWORD *entity_nums) {
    (void)num_selected;
    (void)entity_nums;
    if (ui.UpdateUnitUI) {
        ui.UpdateUnitUI(0, NULL);
    }
}

#define CL_MAX_LAN_SERVERS 64

static uiLanGame_t cl_lan_servers[CL_MAX_LAN_SERVERS];
static DWORD cl_num_lan_servers;

static void CL_InfoValue(LPCSTR info, LPCSTR key, LPSTR out, DWORD out_size) {
    char needle[64];
    LPCSTR cursor;
    LPCSTR value;
    LPCSTR end;
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
    BOOL const open_client_socket = !NET_IsConfigured(NS_CLIENT);

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

static DWORD CL_LANNumServers(void) {
    return cl_num_lan_servers;
}

static BOOL CL_LANServer(DWORD index, uiLanGame_t *out) {
    if (!out || index >= cl_num_lan_servers) {
        return false;
    }
    *out = cl_lan_servers[index];
    return true;
}

static void CL_LANConnectServer(DWORD index) {
    uiLanGame_t *game;
    unsigned short port = (unsigned short)Cvar_Integer("game_port", PORT_SERVER);

    if (index >= cl_num_lan_servers) {
        return;
    }
    game = &cl_lan_servers[index];
    Cvar_Set("connect", game->address);
    CL_Connect(game->address, port);
}

static void CL_AddLANServer(const netadr_t *from, LPCSTR info) {
    uiLanGame_t *game;
    char value[128];
    LPCSTR address;

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
    game->players = (DWORD)atoi(value);
    CL_InfoValue(info, "maxplayers", value, sizeof(value));
    game->maxPlayers = (DWORD)atoi(value);
    CL_InfoValue(info, "speed", value, sizeof(value));
    game->speed = (DWORD)atoi(value);
    CL_InfoValue(info, "slots", value, sizeof(value));
    game->slots = (DWORD)atoi(value);
    if (!game->hostname[0]) {
        snprintf(game->hostname, sizeof(game->hostname), "%s", "OpenWarcraft3");
    }
}

static LPCSTR CL_UIGetLoadingMap(void) {
    return cl.loading_map;
}

static void CL_UICvarSet(LPCSTR name, LPCSTR value) {
    Cvar_Set(name, value);
}

static LPCSTR CL_UIGetLoadingStatus(void) {
    return cl.loading_status;
}

static FLOAT CL_UIGetLoadingProgress(void) {
    return cl.loading_progress;
}

void CL_BeginLoadingMap(LPCSTR mapName) {
    snprintf(cl.loading_map, sizeof(cl.loading_map), "%s", mapName ? mapName : "");
    cl.loading_status[0] = '\0';
    cl.loading_progress = 0.0f;
    cl.playerstate.client_ui_state = CLIENT_UI_LOADING;
    cls.state = ca_loading;
    CL_MenuCommand("menu_ingame");
}

void CL_LoadingUpdate(LPCSTR status, FLOAT progress) {
    if (status && *status) {
        snprintf(cl.loading_status, sizeof(cl.loading_status), "%s", status);
    }
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }
    cl.loading_progress = progress;
}

/* Public wrapper for UI library and input system (Phase 8.6) */
void CL_RequestUnitUI(DWORD num_selected, DWORD *entity_nums) {
    CL_UIRequestUnitUI(num_selected, entity_nums);
}

int CL_ModelIndex(LPCSTR modelName) {
    /* Find or register model */
    for (DWORD i = 1; i < MAX_MODELS; i++) {
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

int CL_ImageIndex(LPCSTR imageName) {
    /* Find or register image */
    for (DWORD i = 1; i < MAX_IMAGES; i++) {
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

int CL_FontIndex(LPCSTR fontName, DWORD fontSize) {
    /* Create font spec string */
    char fontspec[256];
    snprintf(fontspec, sizeof(fontspec), "%s,%u", fontName, fontSize);
    
    /* Find or register font */
    for (DWORD i = 1; i < MAX_FONTSTYLES; i++) {
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

void CL_UIMenuCommand(LPCSTR command) {
    CON_printf("CL_UIMenuCommand: %s\\n", command);
    CL_MenuCommand(command);
}

typedef struct {
    DWORD width;
    DWORD height;
} clVideoMode_t;

static clVideoMode_t const cl_video_modes[] = {
    { 640, 480 },
    { 800, 600 },
    { 1024, 768 },
    { 1152, 864 },
    { 1280, 720 },
    { 1280, 960 },
    { 1280, 1024 },
    { 1366, 768 },
    { 1600, 900 },
    { 1600, 1200 },
    { 1920, 1080 },
    { 1920, 1200 },
    { 2560, 1440 },
};

static clVideoMode_t CL_VideoMode(void) {
    int mode = Cvar_Integer("vid_mode", 2);

    if (mode < 0 || mode >= (int)(sizeof(cl_video_modes) / sizeof(cl_video_modes[0]))) {
        mode = 2;
    }
    return cl_video_modes[mode];
}

void CL_Init(void) {
    clVideoMode_t mode;

    CON_printf("OpenWarcraft3 v0.1");
    fprintf(stderr, "Console initialized.\n");

    re = CL_GetRendererAPI((refImport_t) {
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
        .FS_ReadFile = FS_ReadFileQ3,
        .FS_FreeFile = FS_FreeFile,
        .FileExtract = FS_ExtractFile,
        .ReadSheet = FS_ParseSLK,
        .FindSheetCell = FS_FindSheetCell,
        .CvarString = Cvar_String,
        .error = CON_printf,
    });
    
    mode = CL_VideoMode();
    re.Init(mode.width, mode.height);
    
    /* Initialize UI library */
    ui = UI_GetAPI((uiImport_t) {
        .FS_ReadFile = CL_UI_ReadFile,
        .FS_FreeFile = FS_FreeFile,
        .FS_GetFileList = CL_UI_GetFileList,
        .ReadMapInfo = CM_ReadMapInfo,
        .FindMapPreviewTexture = CM_FindMapPreviewTexture,
        .FreeMapInfo = CM_FreeMapInfo,
        .DefaultMapName = CM_DefaultMapName,
        .ResolveMapInfoString = CM_ResolveMapInfoString,
        .MapNameMatchesFile = CM_MapNameMatchesFile,
        .MapTilesetName = CM_TilesetName,
        .MapSizeName = CM_MapSizeName,
        .SanitizeMapListField = CM_SanitizeMapListField,
        .SanitizeMapInfoText = CM_SanitizeMapInfoText,
        .MemAlloc = MemAlloc,
        .MemFree = MemFree,
        .ModelIndex = CL_ModelIndex,
        .ImageIndex = CL_ImageIndex,
        .FontIndex = CL_FontIndex,
        .ReadSheet = FS_ParseSLK,
        .ReadConfig = FS_ParseINI,
        .FindSheetCell = FS_FindSheetCell,
        .Cmd_AddCommand = Cmd_AddCommand,
        .Cmd_ExecuteText = Cbuf_AddText,
        .ServerCommand = CL_UIServerCommand,
        .Cvar_String = Cvar_String,
        .Cvar_Set = CL_UICvarSet,
        .LANRefreshServers = CL_LANRefreshServers,
        .LANNumServers = CL_LANNumServers,
        .LANServer = CL_LANServer,
        .LANConnectServer = CL_LANConnectServer,
        .GetLoadingMap = CL_UIGetLoadingMap,
        .GetLoadingStatus = CL_UIGetLoadingStatus,
        .GetLoadingProgress = CL_UIGetLoadingProgress,
        .GetPlayerState = CL_UIGetPlayerState,
        .GetNumEntities = CL_UIGetNumEntities,
        .GetEntity = CL_UIGetEntity,
        .GetModel = CL_UIGetModel,
        .GetPortrait = CL_UIGetPortrait,
        .GetTexture = CL_GetTextureByIndex,
        .GetTextures = CL_UIGetTextures,
        .GetFont = CL_UIGetFont,
        .GetClientTime = CL_UIGetClientTime,
        .GetMouseFdf = CL_UIGetMouseFdf,
        .GetMouseButton = CL_UIGetMouseButton,
        .GetMouseEvent = CL_UIGetMouseEvent,
        .LayoutClear = SCR_Clear,
        .LayoutNumFrames = SCR_NumFrames,
        .LayoutFrame = SCR_Frame,
        .LayoutRect = SCR_LayoutRect,
        .LayoutStringValue = SCR_GetStringValue,
        .LayoutDrawText = SCR_GetDrawText,
        .RequestUnitUI = CL_UIRequestUnitUI,
        .GetRenderer = CL_UIGetRenderer,
        .Error = CON_printf,
        .Printf = CON_printf,
    });
    
    if (ui.Init) {
        ui.Init();
    }

    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    
    CL_ClearState();

    Cmd_AddCommand("quit", Com_Quit);

    CON_Init();
    CL_InitInput();

    CL_SetMenuBindings();
    cls.state = ca_disconnected;
    CL_MenuCommand("menu_login");
}

void CL_ConnectionlessPacket(const netadr_t *from, LPSIZEBUF msg) {
    char payload[1024] = { 0 };
    char command[256] = { 0 };
    char *info;
    DWORD length;

    if (msg->cursize <= 4) {
        return;
    }
    length = msg->cursize - 4;
    if (length >= sizeof(payload)) {
        length = sizeof(payload) - 1;
    }
    memcpy(payload, msg->data + 4, length);
    sscanf(payload, "%255s", command);
    if (!strcmp(command, "info")) {
        info = strchr(payload, '\n');
        CL_AddLANServer(from, info ? info + 1 : "");
        return;
    }
    if (strcmp(command, "client_connect")) {
        return;
    }

    MSG_WriteByte(&cls.netchan.message, clc_stringcmd);
    MSG_WriteString(&cls.netchan.message, "new");
    cls.state = ca_connected;
}

static void CL_ReadPacketMessage(const netadr_t *from, LPSIZEBUF msg, int length) {
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
    static BYTE net_message_buffer[MAX_MSGLEN];
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
static void CL_SanitizeUserinfoValue(LPCSTR in, LPSTR out, DWORD out_size) {
    DWORD write = 0;

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

void CL_Connect(LPCSTR host, unsigned short port) {
    netadr_t adr;
    UINAME name;

    fprintf(stderr, "CL_Connect: opening client UDP socket for %s\n", host);
    NET_ConfigSource(NS_CLIENT, true);
    if (!NET_IsConfigured(NS_CLIENT)) {
        fprintf(stderr, "CL_Connect: client UDP socket is closed, cannot connect\n");
        return;
    }
    if (!NET_StringToAdr(host, port, &adr)) {
        fprintf(stderr, "CL_Connect: bad server address \"%s\"\n", host);
        return;
    }
    cls.netchan.remote_address = adr;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    cls.state = ca_connecting;
    // Send an out-of-band "connect" request; the server will register this
    // client slot and reply with "client_connect".
    CL_SanitizeUserinfoValue(Cvar_String("name", "Player"), name, sizeof(name));
    Netchan_OutOfBandPrint(NS_CLIENT, adr, "connect\n\\name\\%s", name[0] ? name : "Player");
    fprintf(stderr, "CL_Connect: connecting to %d.%d.%d.%d:%u\n",
            adr.ip[0], adr.ip[1], adr.ip[2], adr.ip[3], ntohs(adr.port));
}

void CL_Shutdown(void) {
    if (ui.Shutdown) {
        ui.Shutdown();
    }
    FOR_LOOP(modelIndex, MAX_MODELS) {
        SAFE_DELETE(cl.models[modelIndex], re.ReleaseModel);
        SAFE_DELETE(cl.portraits[modelIndex], re.ReleaseModel);
    }
    FOR_LOOP(imageIndex, MAX_DYNAMIC_IMAGES) {
        SAFE_DELETE(cl.dynamicPics[imageIndex], re.ReleaseTexture);
    }
    V_Shutdown();
    re.Shutdown();
}

void CL_SendCommand(void) {
    CL_SendCmd();

    Cbuf_Execute();
}

/* Main client tick called from the platform event loop.
 * Advances the client clock, applies incoming server state, samples input,
 * sends commands, and renders the current frame. */
void CL_Frame(DWORD msec) {
    cl_realtime += msec;
    cl.time += msec;

    /* Update UI library */
    if (ui.Refresh) {
        ui.Refresh(msec);
    }

    CL_Input();
    CL_ReadPackets();
    CL_CheckTimeout();
    CL_SendCommand();
    CL_PrepRefresh();
    SCR_UpdateScreen(msec);
}
