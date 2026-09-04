#ifndef __common_h__
#define __common_h__

#include <stddef.h>

#include "shared.h"
#include "net.h"
#include "mpq.h"
#include "mapinfo.h"

#define MAP_VERTEX_FILE_SIZE 7
#define MAX_SHEET_LINE 1024
#define MAX_COMMAND_ENTITIES 64
#define HEIGHT_COR (TILE_SIZE * 2 + 5)
#define WATER_HEIGHT_COR 80
#define DECODE_HEIGHT(x) (((x) - 0x2000) / 4)
#define CMDARG_LEN 64
#define MAX_CMDARGS 64
#define UPDATE_BACKUP 16
#define UPDATE_MASK (UPDATE_BACKUP-1)
#define U_REMOVE 31
#define FOW_CELLS_PER_TILE_SIDE 2
#define FOW_CELL_SIZE (TILE_SIZE / FOW_CELLS_PER_TILE_SIDE)
#define FOW_CHUNK_TARGET_BYTES 8192

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
    svc_gamecmd,                // [string command] [short payload_size] [payload]
    svc_fogofwar,
    
// Unit UI data (Phase 8: HUD migration)
    svc_unit_ui,                 // [byte num_units] for each unit: [short entity] [byte num_buttons] [buttons] [byte num_inventory] [inventory] [byte num_queue] [queue]
    svc_window,                  // [byte open] [long id] [long class, long flags, frames, long text size, text]
    svc_ui_window,               // [string window_id] [byte show] legacy menu-module-owned named XML window toggle
    svc_disconnect               // server is closing or dropped this client
};

// client to server
enum clc_ops {
    clc_bad,
//    clc_nop,
    clc_camera_position,
//    clc_userinfo,            // [[userinfo string]
    clc_stringcmd,           // [string] message

    clc_request_unit_ui      // [byte num_selected] [num_selected * short entity_nums]
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
    attrib_instance,
    attrib_count = attrib_instance + 4, /* mat4 attributes reserve four consecutive locations */
} t_attrib_id;

struct texture;
struct font;
struct m2Model_s;

typedef void (*xcommand_t)(void);
typedef void (*cmdListFunc_t)(LPCSTR name, void *userData);
typedef void (*fsMapListFunc_t)(LPCSTR path, void *userData);

typedef enum {
    FS_MAP_RESOLVE_OK,
    FS_MAP_RESOLVE_NOT_FOUND,
    FS_MAP_RESOLVE_AMBIGUOUS,
} fsMapResolve_t;

typedef struct cvar_s {
    struct cvar_s *next;
    LPCSTR name;
    LPSTR string;
    FLOAT value;
    int integer;
    DWORD flags;
    bool modified;
    LPCSTR description; /* shown on tab-complete; set via Cvar_Describe */
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

KNOWN_AS(model, MODEL);
KNOWN_AS(texture, TEXTURE);
KNOWN_AS(font, FONT);
KNOWN_AS(War3MapVertex, WAR3MAPVERTEX);
KNOWN_AS(war3map, WAR3MAP);
KNOWN_AS(TerrainInfo, TERRAININFO);
KNOWN_AS(CliffInfo, CLIFFINFO);

#include "cmodel.h"

/* Per-game identity, defined by each game.mk (warcraft-3, world-of-warcraft,
 * starcraft-2). Used to scope share/<game>/ defaults and writable user data. */
#ifndef BZ_GAME
#define BZ_GAME "openwarcraft3"
#endif

// common.c
void Com_Init(int argc, LPCSTR *argv);
void Com_Error(errorCode_t code, LPCSTR fmt, ...);
void LoadMap(LPCSTR pFilename);
bool Com_ResolveMapArgument(LPCSTR arg, LPSTR out, DWORD out_size);

void FS_Init(void);
void FS_SetShareDirectory(LPCSTR dir);
void FS_SetHomeDirectory(LPCSTR dir);
LPCSTR FS_BasePath(void);
LPCSTR FS_HomePath(void);
void FS_UserPath(LPCSTR rel, LPSTR out, DWORD out_size);
void FS_ConfigPath(LPCSTR rel, LPSTR out, DWORD out_size);
void FS_SavePath(LPCSTR rel, LPSTR out, DWORD out_size);
void FS_Shutdown(void);
BOMStatus PF_TextRemoveBom(LPSTR buffer);

void Com_Quit(void);
void Sys_Quit(void);

HANDLE FS_AddArchive(LPCSTR filename);
BOOL FS_AddDataDirectory(LPCSTR dirname);
BOOL FS_ArchiveFileVisible(LPCSTR archive, LPCSTR filename);
HANDLE FS_OpenFile(LPCSTR fileName);
void FS_CloseFile(HANDLE file);
HANDLE FS_ReadLooseFile(LPCSTR filename, LPDWORD size, DWORD extraBytes);
bool FS_ExtractFile(LPCSTR toExtract, LPCSTR extracted);
bool FS_FileExists(LPCSTR fileName);
HANDLE FS_ReadFile(LPCSTR filename, LPDWORD size);
void FS_ReadFileAll(LPCSTR filename, void (*callback)(HANDLE buf, DWORD size, void *ud), void *ud);

// Quake 3-style file API (returns file size, allocates buffer)
int FS_ReadFileQ3(LPCSTR filename, void **buf);
void FS_FreeFile(void *buf);
// mmap-backed read for loose files (PROT_READ, MAP_PRIVATE); free with FS_MunmapFile
void *FS_MmapFile(LPCSTR filename, LPDWORD out_size);
void  FS_MunmapFile(void *ptr);
HANDLE FS_FindFirstFile(LPCSTR mask, SFILE_FIND_DATA *findData);
BOOL FS_FindNextFile(HANDLE find, SFILE_FIND_DATA *findData);
BOOL FS_FindClose(HANDLE find);
DWORD FS_ListMaps(fsMapListFunc_t func, void *userData);
fsMapResolve_t FS_ResolveMapPath(LPCSTR name, LPSTR out, DWORD out_size);

typedef struct {
    HANDLE (*ReadFile)(LPCSTR filename, LPDWORD size);
    void (*FreeFile)(HANDLE file);
    HANDLE (*MemAlloc)(long size);
    void (*MemFree)(HANDLE mem);
} SHEETHOST;

void FS_SetSheetHost(SHEETHOST const *host);

void CL_Init(void);
void CL_Frame(DWORD msec);
void CL_Shutdown(void);

/* Sound (sound/s_sound.c) */
BOOL S_Init(void);
void S_Shutdown(void);
void S_PlaySound(DWORD kit_id);
void S_PlaySoundByName(LPCSTR name);
void S_StopAllSounds(void);
void S_BeginRegistration(void);
void S_EndRegistration(void);
void CL_Connect(LPCSTR host, unsigned short port);
void CL_SetMenuBindings(void);
void CL_SetGameplayBindings(void);
void CL_BeginLoadingMap(LPCSTR mapName);

void SV_Init(void);
void SV_Frame(DWORD msec);
void SV_Shutdown(void);
void SV_StartLobby(LPCSTR pFilename);
void SV_Map(LPCSTR pFilename);
BOOL SV_GetSaveMap(LPCSTR name, LPSTR map, DWORD map_size);
BOOL SV_LoadGame(LPCSTR name, LPCSTR map);
#ifdef WOW
DWORD SV_PlayerCreateMap(void);
#endif
void SV_LobbyBroadcastChat(LPCSTR sender, LPCSTR text);
void SV_LobbyBroadcastChatFrom(DWORD sender_client, LPCSTR sender, LPCSTR text);
void MenuAction(LPCSTR action, LPCSTR arg);

HANDLE MemAlloc(long size);
void MemFree(HANDLE mem);

void Sys_MkDir(LPCSTR directory);

struct edict_s;
DWORD CM_BuildHeatmap(struct edict_s *goalentity);
DWORD CM_BuildHeatmapForRadius(struct edict_s *goalentity, FLOAT radius);
DWORD CM_RequestHeatmapForRadius(struct edict_s *goalentity, FLOAT radius);
void  CM_ProcessPathJobs(DWORD work_budget);
BOOL  CM_FindPathWaypoint(pathAccelParams_t const *params, LPVECTOR2 out);
BOOL  CM_ActivateCachedFlow(DWORD generation);
BOOL  CM_FlowReachedGoal(DWORD generation, FLOAT x, FLOAT y);
BOOL  CM_FlowCanReach(DWORD generation, FLOAT x, FLOAT y);
VECTOR2 get_flow_direction(DWORD heatmapindex, float fnx, float fny);
void CM_BakeStaticObstacles(void);
void CM_InvalidatePathCache(void);
void CM_SetupPathMap(DWORD width, DWORD height, BYTE const *cells);
BOOL CM_IsMapLoaded(LPCSTR mapFilename);
BOOL CM_ClosestPathablePoint(LPCVECTOR2 location, LPVECTOR2 out);
BOOL CM_ClosestPathablePointForRadius(LPCVECTOR2 location, FLOAT radius, LPVECTOR2 out);
BOOL CM_ClosestReachablePointForRadius(LPCVECTOR2 from, LPCVECTOR2 target, FLOAT radius, LPVECTOR2 out);
BOOL CM_FindDirectApproachPointForRadius(LPCVECTOR2 from, LPCVECTOR2 target, FLOAT range, FLOAT radius, LPVECTOR2 out);
FLOAT CM_PathCellWorldSize(void);
BOOL CM_FindApproachPointToFootprintForRadius(struct edict_s const *target, LPCVECTOR2 from, FLOAT range, FLOAT radius, LPVECTOR2 out);
BOOL CM_FindInnerApproachPointToFootprintForRadius(struct edict_s const *target, LPCVECTOR2 from, FLOAT range, FLOAT radius, LPVECTOR2 out);
FLOAT CM_GetHeightAtPoint(FLOAT sx, FLOAT sy);
FLOAT CM_GetCameraHeightOffset(void);
BOX2 CM_GetWorldBounds(void);

struct world_state {
    LPWAR3MAP map;
    MAPINFO info;
    struct Doodad *doodads;
};

typedef struct {
    VECTOR3 target;
    FLOAT distance, pitch, yaw, fov, znear, zfar, height_offset;
} gameCamera_t;

/* Games must author fov/znear/zfar together; the client copies all three like distance. */
static inline void player_set_lens(LPPLAYER ps, gameCamera_t const *cam) {
    ps->fov = (DWORD)cam->fov;
    ps->znear = cam->znear;
    ps->zfar = cam->zfar;
}

BOOL CL_GameDefaultCamera(gameCamera_t *camera);
FLOAT CL_GameCameraHeightAtPoint(FLOAT x, FLOAT y);
FLOAT CL_GameLerpDegrees(FLOAT a, FLOAT b, FLOAT fraction);

extern struct world_state world;

/* Implemented by the selected game's common/world_*.c. */
bool     CM_LoadMapFormat(LPCSTR mapFilename);
VECTOR2  CM_GetNormalizedMapPosition(FLOAT x, FLOAT y);
VECTOR2  CM_GetDenormalizedMapPosition(FLOAT x, FLOAT y);

// games/warcraft-3/sheet/parser.c
LPSTR ParserGetTokenEx(parser_t *p, bool sameLine);
LPSTR ParserGetToken(parser_t *p);
LPSTR FS_ReadFileIntoString(LPCSTR fileName);
void FS_FreeFileString(LPSTR buffer);
void ParserError(parser_t *p);

// cmd.c
void Cbuf_Init(void);
void Cbuf_AddText(LPCSTR text);
void Cbuf_Execute(void);
void Cbuf_AddEarlyCommands(bool clear);
bool Cbuf_AddLateCommands(void);
int Cmd_Argc(void);
LPCSTR Cmd_Argv(int arg);
LPCSTR Cmd_ArgsFrom(int arg);
void Cmd_AddCommand(LPCSTR cmd_name, xcommand_t function);
void Cmd_RemoveCommand(LPCSTR cmd_name);
bool Cmd_Exists(LPCSTR cmd_name);
void Cmd_ExecuteString(LPCSTR text);
void Cmd_ForwardToServer(LPCSTR text);
void Cmd_ForEachCommand(cmdListFunc_t func, void *userData);
int Cmd_CompleteCommand(LPCSTR partial, LPSTR out, DWORD out_size, bool print);

// common.c command-line args
void COM_InitArgv(int argc, LPCSTR *argv);
int COM_Argc(void);
LPCSTR COM_Argv(int arg);
void COM_ClearArgv(int arg);

// cvar.c
void Cvar_Init(void);
cvar_t *Cvar_Get(LPCSTR name, LPCSTR value, DWORD flags);
cvar_t *Cvar_GetD(LPCSTR name, LPCSTR value, DWORD flags, LPCSTR description);
cvar_t *Cvar_Set(LPCSTR name, LPCSTR value);
cvar_t *Cvar_SetValue(LPCSTR name, FLOAT value);
LPCSTR Cvar_String(LPCSTR name, LPCSTR fallback);
int Cvar_Integer(LPCSTR name, int fallback);
FLOAT Cvar_Value(LPCSTR name, FLOAT fallback);
bool Cvar_LoadConfig(LPCSTR filename);
void Cvar_WriteConfig(LPCSTR filename);
void Cvar_ApplyConfigCommandLine(int argc, LPCSTR *argv);
void Cvar_ApplyCommandLine(int argc, LPCSTR *argv);
bool Cvar_Command(void);
void Cvar_ForEachVariable(cmdListFunc_t func, void *userData);
int Cvar_CompleteVariable(LPCSTR partial, LPSTR out, DWORD out_size, bool print);
void Cvar_Describe(LPCSTR name, LPCSTR description);

#endif
