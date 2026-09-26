#ifndef __common_h__
#define __common_h__

#include <stddef.h>

#include "shared.h"
#include "net.h"
#include "mpq.h"
#include "mapinfo.h"
#include "terrain_mask.h"

#define MAP_VERTEX_FILE_SIZE 7
#define MAX_SHEET_LINE 1024
#define MAX_COMMAND_ENTITIES 64
#define CMDARG_LEN 64
#define MAX_CMDARGS 64
#define UPDATE_BACKUP 16
#define UPDATE_MASK (UPDATE_BACKUP-1)
#define U_REMOVE 31
#define FOW_CELLS_PER_TILE_SIDE 2
#define FOW_CELL_SIZE (TILE_SIZE / FOW_CELLS_PER_TILE_SIDE)
#define FOW_CHUNK_TARGET_BYTES 8192
#define MAX_GAME_DATAGRAM_SIZE 8192 // bytes; bounds game-owned per-frame payloads appended after entity state

enum {
    FOW_MSG_FULL = 1 << 0,
    FOW_MSG_VISIBLE_PLANE = 1 << 1,
    FOW_MSG_EXPLORED_PLANE = 1 << 2,
    FOW_MSG_RLE = 1 << 3,
};

#define SFileReadArray(file, object, variable, elemsize, alloc) \
SFileReadFile(file, &object->num_##variable, 4, NULL, NULL); \
if (object->num_##variable > 0) {object->variable = alloc(object->num_##variable * elemsize); \
SFileReadFile(file, object->variable, object->num_##variable * elemsize, NULL, NULL); }

typedef enum {
    ERR_FATAL,        // exit the entire game with a popup window
    ERR_DROP,         // print to console and disconnect from game
    ERR_QUIT,         // not an error, just a normal exit
} errorCode_t;

// server to client
enum svc_ops {
    svc_bad,
// these ops are known to the game dll
//    svc_muzzleflash,
//    svc_muzzleflash2,
    svc_temp_entity,
    svc_layout,
    svc_playerinfo,
    svc_cursor,
    svc_cursor_splat,

// the rest are private to the client and server
//    svc_nop,
//    svc_disconnect,
//    svc_reconnect,
    svc_sound,                    // [byte flags] [short sound] [optional volume/attenuation/offset/entity/position]
    svc_music,                    // [byte musicCommand_t] [command-specific reliable presentation payload]
    svc_minimap_ping,             // [vec2 position] [float seconds] [rgba] [byte flags]
//    svc_print,                    // [byte] id [string] null terminated string
//    svc_stufftext,                // [string] stuffed into client's console buffer, should be \n terminated
//    svc_serverdata,                // [long] protocol ...
    svc_configstring,            // [short] [string]
    svc_spawnbaseline,
//    svc_centerprint,            // [string] to put in center of the screen
//    svc_download,                // [short] size [size bytes]
//    svc_playerinfo,                // variable
    svc_packetentities,            // [...]
//    svc_deltapacketentities,    // [...]
    svc_frame,
    svc_mirror,
    svc_lobby_setup,            // [string map] [string name] [byte speed] [byte slots] [byte local_slot] [long revision] [slots]
    svc_fogofwar,
    
// Unit UI data (Phase 8: HUD migration)
    svc_unit_ui,                 // [byte num_units] for each unit: [short entity] [byte num_buttons] [buttons] [byte num_inventory] [inventory] [byte num_queue] [queue]
    svc_window,                  // [byte open] [long id] [long class, long flags, frames, long text size, text]
    svc_ui_window,               // [string window_id] [byte show] legacy menu-module-owned named XML window toggle
    svc_disconnect,               // server is closing or dropped this client
    svc_lobby_chat,              // [byte own] [string text]
    svc_set_selection,           // [byte count] [count * long entity]
    svc_console_print,           // [string text] server/game command feedback for the local console
    svc_nop,                     // transport keepalive; no payload or presentation change
    svc_loading_screen,          // [long total] [long offset] [long size] [size bytes of zlib loading layout]
};

// client to server
enum clc_ops {
    clc_bad,
//    clc_nop,
    clc_camera_position,
//    clc_userinfo,            // [[userinfo string]
    clc_stringcmd,           // [string] message

    clc_request_unit_ui = 3, // [byte num_selected] [num_selected * short entity_nums]
    clc_input = 4,           // typed focus/view/movement input; see MSG_ReadInput
};

typedef enum t_attrib_id {
    attrib_position,
    attrib_color,
    attrib_texcoord,
    attrib_normal,
    attrib_skin1,
    //attrib_skin2,        /* removed: unified shader uses top-4 bones only */
    attrib_boneWeight1,
    //attrib_boneWeight2,  /* removed: unified shader uses top-4 bones only */
    attrib_particleAxis,
    attrib_particleSize,
    attrib_particleTail,
    attrib_instance,
    attrib_count = attrib_instance + 4, /* mat4 attributes reserve four consecutive locations */
} t_attrib_id;

struct texture;
struct font;
struct m2Model_s;

typedef void (*xcommand_t)(void);
typedef void (*cmdListFunc_t)(cstring_t name, void *userData);
typedef void (*fsMapListFunc_t)(cstring_t path, void *userData);

typedef enum {
    FS_MAP_RESOLVE_OK,
    FS_MAP_RESOLVE_NOT_FOUND,
    FS_MAP_RESOLVE_AMBIGUOUS,
} fsMapResolve_t;

typedef struct cvar_s {
    struct cvar_s *next;
    cstring_t name;
    string_t string;
    float value;
    int integer;
    uint32_t flags;
    bool modified;
    cstring_t description; /* shown on tab-complete; set via Cvar_Describe */
} cvar_t;

enum {
    FLAG(CVAR_ARCHIVE, 0),
};

typedef struct model {
    unsigned int modeltype;
    struct mdxModel_s *mdx;
    struct m3Model_s *m3;
    struct m2Model_s *m2;
} model_t;

KNOWN_AS(model, model_t);
KNOWN_AS(texture, texture_t);
KNOWN_AS(font, font_t);
KNOWN_AS(War3MapVertex, war3mapVertex_t);
KNOWN_AS(war3map, war3map_t);
KNOWN_AS(TerrainInfo, terrainInfo_t);
KNOWN_AS(CliffInfo, cliffInfo_t);

#include "cmodel.h"

/* Per-game identity, defined by each game.mk (warcraft-3, world-of-warcraft,
 * starcraft-2). Used to scope share/<game>/ defaults and writable user data. */
#ifndef BZ_GAME
#define BZ_GAME "openwarcraft3"
#endif

// common.c
void Com_Init(int argc, cstring_t *argv);
void Com_Error(errorCode_t code, cstring_t fmt, ...);
void LoadMap(cstring_t pFilename);
bool Com_ResolveMapArgument(cstring_t arg, string_t out, uint32_t out_size);

void FS_Init(void);
void FS_SetShareDirectory(cstring_t dir);
void FS_SetHomeDirectory(cstring_t dir);
cstring_t FS_BasePath(void);
cstring_t FS_HomePath(void);
void FS_UserPath(cstring_t rel, string_t out, uint32_t out_size);
void FS_ConfigPath(cstring_t rel, string_t out, uint32_t out_size);
void FS_SavePath(cstring_t rel, string_t out, uint32_t out_size);
uint32_t FS_ListSaves(string_t out, uint32_t out_size);
bool FS_DeleteSave(cstring_t rel);
void FS_Shutdown(void);
BOMStatus PF_TextRemoveBom(string_t buffer);

void Com_Quit(void);
void Sys_Quit(void);

handle_t FS_AddArchive(cstring_t filename);
bool FS_AddDataDirectory(cstring_t dirname);
bool FS_ArchiveFileVisible(cstring_t archive, cstring_t filename);
/* Highest-priority open archive for FS_OpenFile/FS_ReadFile (e.g. current map MPQ). NULL clears. */
void FS_SetPriorityArchive(handle_t archive);
handle_t FS_GetPriorityArchive(void);
handle_t FS_OpenFile(cstring_t fileName);
void FS_CloseFile(handle_t file);
handle_t FS_ReadLooseFile(cstring_t filename, uint32_t *size, uint32_t extraBytes);
bool FS_ExtractFile(cstring_t toExtract, cstring_t extracted);
bool FS_FileExists(cstring_t fileName);
bool FS_ResolveLoosePath(cstring_t fileName, string_t out, uint32_t out_size);
handle_t FS_ReadFile(cstring_t filename, uint32_t *size);
void FS_ReadFileAll(cstring_t filename, void (*callback)(handle_t buf, uint32_t size, void *ud), void *ud);

// Quake 3-style file API (returns file size, allocates buffer)
int FS_ReadFileQ3(cstring_t filename, void **buf);
void FS_FreeFile(void *buf);
// mmap-backed read for loose files (PROT_READ, MAP_PRIVATE); free with FS_MunmapFile
void *FS_MmapFile(cstring_t filename, uint32_t *out_size);
void  FS_MunmapFile(void *ptr);
handle_t FS_FindFirstFile(cstring_t mask, sfileFindData_t *findData);
bool FS_FindNextFile(handle_t find, sfileFindData_t *findData);
bool FS_FindClose(handle_t find);
uint32_t FS_ListMaps(fsMapListFunc_t func, void *userData);
fsMapResolve_t FS_ResolveMapPath(cstring_t name, string_t out, uint32_t out_size);

typedef struct {
    handle_t (*ReadFile)(cstring_t filename, uint32_t *size);
    void (*FreeFile)(handle_t file);
    handle_t (*MemAlloc)(long size);
    void (*MemFree)(handle_t mem);
} sheetHost_t;

void FS_SetSheetHost(sheetHost_t const *host);

void CL_Init(void);
void CL_Frame(uint32_t msec);
void CL_Shutdown(void);

/* Sound (sound/s_sound.c) */
bool S_Init(void);
void S_Shutdown(void);
void S_PlaySound(uint32_t kit_id);
void S_PlaySoundByName(cstring_t name);
void S_StopAllSounds(void);
void S_BeginRegistration(void);
void S_EndRegistration(void);
void CL_Connect(cstring_t host, unsigned short port);
void CL_SetMenuBindings(void);
void CL_SetGameplayBindings(void);
void CL_BeginLoadingMap(cstring_t mapName);
void CL_LoadingFrame(void);

void SV_Init(void);
void SV_Frame(uint32_t msec);
void SV_Shutdown(void);
void SV_StartLobby(cstring_t pFilename);
void SV_Map(cstring_t pFilename);
bool SV_GetSaveMap(cstring_t name, string_t map, uint32_t map_size);
bool SV_LoadGame(cstring_t name, cstring_t map);
#ifdef WOW
uint32_t SV_PlayerCreateMap(void);
#endif
void SV_LobbyBroadcastChat(cstring_t sender, cstring_t text);
void SV_LobbyBroadcastChatFrom(uint32_t sender_client, cstring_t sender, cstring_t text);
void MenuAction(cstring_t action, cstring_t arg);

handle_t MemAlloc(long size);
void MemFree(handle_t mem);

void Sys_MkDir(cstring_t directory);

struct edict_s;
uint32_t CM_BuildHeatmap(struct edict_s *goalentity);
uint32_t CM_BuildHeatmapForRadius(struct edict_s *goalentity, float radius);
uint32_t CM_RequestHeatmapForRadius(struct edict_s *goalentity, float radius);
uint32_t CM_RequestHeatmapForRadiusFlags(struct edict_s *goalentity, float radius, uint8_t blocked_flags);
void  CM_ProcessPathJobs(uint32_t work_budget);
bool  CM_FindPathWaypoint(pathAccelParams_t const *params, vec2_t *out);
bool  CM_ActivateCachedFlow(uint32_t generation);
bool  CM_ActivateCachedFlowForFlags(uint32_t generation, uint8_t blocked_flags);
bool  CM_FlowReachedGoal(uint32_t generation, float x, float y);
bool  CM_FlowCanReach(uint32_t generation, float x, float y);
vec2_t get_flow_direction(uint32_t heatmapindex, float fnx, float fny);
void CM_BakeStaticObstacles(void);
void CM_InvalidatePathCache(void);
void CM_SetupPathMap(uint32_t width, uint32_t height, uint8_t const *cells);
bool CM_IsMapLoaded(cstring_t mapFilename);
bool CM_ClosestPathablePoint(vec2_t const *location, vec2_t *out);
bool CM_ClosestPathablePointForRadius(vec2_t const *location, float radius, vec2_t *out);
bool CM_ClosestPathablePointForRadiusFlags(vec2_t const *location, float radius, uint8_t blocked_flags,
                                           vec2_t *out);
bool CM_ClosestReachablePointForRadius(vec2_t const *from, vec2_t const *target, float radius, vec2_t *out);
bool CM_ClosestReachablePointForRadiusFlags(vec2_t const *from, vec2_t const *target, float radius,
                                            uint8_t blocked_flags, vec2_t *out);
bool CM_FindDirectApproachPointForRadius(vec2_t const *from, vec2_t const *target, float range, float radius, vec2_t *out);
float CM_PathCellWorldSize(void);
bool CM_FindApproachPointToFootprintForRadius(struct edict_s const *target, vec2_t const *from, float range, float radius, vec2_t *out);
bool CM_FindInnerApproachPointToFootprintForRadius(struct edict_s const *target, vec2_t const *from, float range, float radius, vec2_t *out);
float CM_GetHeightAtPoint(float sx, float sy);
float CM_GetWaterHeightAtPoint(float sx, float sy);
bool CM_TerrainPointIsWalkable(vec2_t const *location);
bool CM_TerrainPointIsSwimmable(vec2_t const *location);
float CM_GetCameraHeightOffset(void);
box2_t CM_GetWorldBounds(void);

struct world_state {
    war3map_t *map;
    mapInfo_t info;
    struct Doodad *doodads;
};

typedef struct {
    vec3_t target;
    float distance, pitch, yaw, fov, znear, zfar, height_offset;
} gameCamera_t;

/* Games must author fov/znear/zfar together; the client copies all three like distance. */
static inline void player_set_lens(player_t *ps, gameCamera_t const *cam) {
    ps->fov = cam->fov;
    ps->znear = cam->znear;
    ps->zfar = cam->zfar;
}

bool CL_GameDefaultCamera(gameCamera_t *camera);
bool CL_GameCameraUsesWorldUp(void);
float CL_GameLerpDegrees(float a, float b, float fraction);
cstring_t CL_GameOrderQueueReleaseCommand(void);
bool CL_GameBuildCursorBlocked(vec3_t const *origin);
void CL_GameModifyBuildPathing(vec2_t const *point, uint8_t *flags);
typedef struct {
    uint32_t anchor;
    uint32_t const *visible;
    uint32_t visible_count, limit;
    string_t command;
    uint32_t command_size;
} gameSameTypeSelection_t;
bool CL_GameBuildSameTypeSelection(gameSameTypeSelection_t *selection);
/* Resolved from the mounted archives once per session and after an edition switch; see common/ui_canvas.h. */
UICANVASPOLICY CL_GameCanvasPolicy(void);

extern struct world_state world;

/* Implemented by the selected game's common/world_*.c. */
bool     CM_LoadMapFormat(cstring_t mapFilename, cmLoadYield_t yield);
vec2_t  CM_GetNormalizedMapPosition(float x, float y);
vec2_t  CM_GetDenormalizedMapPosition(float x, float y);

// games/warcraft-3/sheet/parser.c
string_t ParserGetTokenEx(parser_t *p, bool sameLine);
string_t ParserGetToken(parser_t *p);
string_t FS_ReadFileIntoString(cstring_t fileName);
void FS_FreeFileString(string_t buffer);
void ParserError(parser_t *p);

// cmd.c
void Cbuf_Init(void);
void Cbuf_AddText(cstring_t text);
void Cbuf_Execute(void);
void Cbuf_CopyToDefer(void);
void Cbuf_InsertFromDefer(void);
void Cbuf_ClearDefer(void);
void Cbuf_AddEarlyCommands(bool clear);
bool Cbuf_AddLateCommands(void);
int Cmd_Argc(void);
cstring_t Cmd_Argv(int arg);
cstring_t Cmd_ArgsFrom(int arg);
void Cmd_AddCommand(cstring_t cmd_name, xcommand_t function);
void Cmd_RemoveCommand(cstring_t cmd_name);
bool Cmd_Exists(cstring_t cmd_name);
void Cmd_ExecuteString(cstring_t text);
void Cmd_ForwardToServer(cstring_t text);
void Cmd_ForEachCommand(cmdListFunc_t func, void *userData);
int Cmd_CompleteCommand(cstring_t partial, string_t out, uint32_t out_size, bool print);

// common.c command-line args
void COM_InitArgv(int argc, cstring_t *argv);
int COM_Argc(void);
cstring_t COM_Argv(int arg);
void COM_ClearArgv(int arg);

// cvar.c
void Cvar_Init(void);
void Cvar_EndConfig(void);
cvar_t *Cvar_Get(cstring_t name, cstring_t value, uint32_t flags);
cvar_t *Cvar_GetD(cstring_t name, cstring_t value, uint32_t flags, cstring_t description);
cvar_t *Cvar_Set(cstring_t name, cstring_t value);
cvar_t *Cvar_SetValue(cstring_t name, float value);
cstring_t Cvar_String(cstring_t name, cstring_t fallback);
int Cvar_Integer(cstring_t name, int fallback);
float Cvar_Value(cstring_t name, float fallback);
bool Cvar_LoadConfig(cstring_t filename);
void Cvar_WriteConfig(cstring_t filename);
void Cvar_ApplyConfigCommandLine(int argc, cstring_t *argv);
void Cvar_ApplyCommandLine(int argc, cstring_t *argv);
bool Cvar_ApplyBooleanCommandLineFlag(cstring_t name);
bool Cvar_Command(void);
void Cvar_ForEachVariable(cmdListFunc_t func, void *userData);
int Cvar_CompleteVariable(cstring_t partial, string_t out, uint32_t out_size, bool print);
void Cvar_Describe(cstring_t name, cstring_t description);

#endif
