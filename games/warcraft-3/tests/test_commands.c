/*
 * test_commands.c — Quake-style command and map resolver coverage.
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "test.h"
#include "common/video_modes.h"

static PATHSTR last_loading_map;
static PATHSTR last_sv_map;
static PATHSTR last_load_name;
static PATHSTR last_load_map;
static PATHSTR last_connect_host;
static char last_forwarded[1024];
static bool command_tests_initialized;
static bool late_command_called;
static bool save_map_readable;
static bool load_game_ok;
static unsigned short last_connect_port;

extern BOOL cl_screenshot_pending;
extern DWORD cl_screenshot_delay;
void CL_Screenshot_f(void);
BOOL CL_ScreenshotReady(void);

void Key_Init(void) {
}

void Key_WriteBindings(FILE *file) {
    (void)file;
}

void Cmd_ForwardToServer(LPCSTR text) {
    snprintf(last_forwarded, sizeof(last_forwarded), "%s", text ? text : "");
}

void CL_SetGameplayBindings(void) {
}

void CL_Connect(LPCSTR host, unsigned short port) {
    snprintf(last_connect_host, sizeof(last_connect_host), "%s", host ? host : "");
    last_connect_port = port;
}

void CL_BeginLoadingMap(LPCSTR mapName) {
    snprintf(last_loading_map, sizeof(last_loading_map), "%s", mapName ? mapName : "");
}

void CL_Shutdown(void) {
}

void SV_Map(LPCSTR pFilename) {
    snprintf(last_sv_map, sizeof(last_sv_map), "%s", pFilename ? pFilename : "");
}

BOOL SV_GetSaveMap(LPCSTR name, LPSTR map, DWORD map_size) {
    (void)name;
    if (!save_map_readable) return false;
    strlcpy(map, "Maps\\Campaign\\Human02.w3m", map_size);
    return true;
}

BOOL SV_LoadGame(LPCSTR name, LPCSTR map) {
    snprintf(last_load_name, sizeof(last_load_name), "%s", name ? name : "");
    snprintf(last_load_map, sizeof(last_load_map), "%s", map ? map : "");
    return load_game_ok;
}

void SV_Shutdown(void) {
}

void Sys_Quit(void) {
}

void PF_Sleep(DWORD msec) {
    (void)msec;
}

static void reset_map_handoff(void) {
    last_loading_map[0] = '\0';
    last_sv_map[0] = '\0';
    last_load_name[0] = '\0';
    last_load_map[0] = '\0';
    last_connect_host[0] = '\0';
    last_forwarded[0] = '\0';
    late_command_called = false;
    save_map_readable = load_game_ok = true;
    last_connect_port = 0;
}

static void Test_LateCommand_f(void) {
    late_command_called = true;
}

static void setup_command_tests(void) {
    if (command_tests_initialized) {
        return;
    }

    LPCSTR argv[] = { "test_commands", "-config", "" };

    Com_Init(3, argv);
    T_ASSERT(FS_AddArchive("build/tests/tests.mpq") != NULL);
    reset_map_handoff();
    command_tests_initialized = true;
}

TEST(commands, command_registration) {
    setup_command_tests();

    T_ASSERT(Cmd_Exists("cmdlist"));
    T_ASSERT(Cmd_Exists("load"));
    T_ASSERT(Cmd_Exists("map"));
    T_ASSERT(Cmd_Exists("maps"));
    T_ASSERT(Cmd_Exists("dir"));
    T_ASSERT(Cmd_Exists("path"));
}

TEST(commands, save_path_adds_one_sav_extension) {
    PATHSTR path;

    setup_command_tests();
    FS_SetHomeDirectory("/tmp/openwarcraft3-save-path-test");
    FS_SavePath("quick", path, sizeof(path));
    T_STREQ(path, "/tmp/openwarcraft3-save-path-test/saves/quick.sav");
    FS_SavePath("manual.SAV", path, sizeof(path));
    T_STREQ(path, "/tmp/openwarcraft3-save-path-test/saves/manual.SAV");
}

TEST(commands, config_path_uses_home_game_directory) {
    PATHSTR path;

    setup_command_tests();
    FS_SetHomeDirectory("/tmp/openwarcraft3-config-path-test");
    FS_ConfigPath("config.cfg", path, sizeof(path));
    T_STREQ(path, "/tmp/openwarcraft3-config-path-test/config.cfg");
}

TEST(commands, config_loader_reports_missing_files) {
    setup_command_tests();

    T_ASSERT(!Cvar_LoadConfig("/tmp/openwarcraft3-config-path-test/missing.cfg"));
    T_ASSERT(Cvar_LoadConfig("games/world-of-warcraft/share/config.cfg"));
    Cbuf_Execute();
}

TEST(commands, share_authored_ui_assets_are_readable) {
    DWORD size = 0;
    HANDLE data;

    setup_command_tests();
    data = FS_ReadFile("UI\\FrameDef\\OpenWarcraft3\\CampaignList.fdf", &size);
    T_NOT_NULL(data); T_ASSERT(size > 0);
    if (data) FS_FreeFile(data);
}

TEST(commands, command_and_cvar_completion) {
    char out[128];

    setup_command_tests();

    T_EQ(Cmd_CompleteCommand("cmdli", out, sizeof(out), false), 1);
    T_STREQ(out, "cmdlist");
    T_EQ(Cmd_CompleteCommand("ma", out, sizeof(out), false), 2);
    T_STREQ(out, "map");
    T_EQ(Cvar_CompleteVariable("scr_show", out, sizeof(out), false), 1);
    T_STREQ(out, "scr_showfps");
    T_ASSERT(Cvar_String("scr_showfps", NULL) != NULL);
}

TEST(commands, registered_renderer_cvar_accepts_bare_assignment) {
    setup_command_tests();
    Cvar_Set("r_stats", "0");
    Cmd_ExecuteString("r_stats 1");
    T_STREQ(Cvar_String("r_stats", NULL), "1");
}

TEST(commands, cursor_defaults_to_native_sdl) {
    setup_command_tests();
    T_STREQ(Cvar_String("r_cursor", NULL), "0");
}

TEST(commands, cursor_allows_game_authored_override) {
    LPCSTR argv[] = { "test_commands", "+r_cursor", "1" };

    setup_command_tests();
    COM_InitArgv(3, argv);
    Cbuf_AddEarlyCommands(true);
    T_STREQ(Cvar_String("r_cursor", NULL), "1");
}

TEST(commands, data_command_line_sets_data_cvar) {
    LPCSTR argv[] = { "test_commands", "-data", "tests/data dir" };

    setup_command_tests();
    Cvar_ApplyCommandLine(3, argv);

    T_STREQ(Cvar_String("data", NULL), "tests/data dir");
}

TEST(commands, tft_command_line_enables_expansion_archives) {
    LPCSTR argv[] = { "test_commands", "-tft" };

    setup_command_tests();
    Cvar_Set("fs_expansion", "0");
    Cvar_ApplyCommandLine(2, argv);

    T_STREQ(Cvar_String("fs_expansion", NULL), "1");
}

TEST(commands, roc_command_line_disables_expansion_archives) {
    LPCSTR argv[] = { "test_commands", "-roc" };

    setup_command_tests();
    Cvar_Set("fs_expansion", "1");
    Cvar_ApplyCommandLine(2, argv);

    T_STREQ(Cvar_String("fs_expansion", NULL), "0");
}

TEST(commands, roc_uses_base_ai_scripts_without_dropping_localized_data) {
    setup_command_tests();
    Cvar_Set("fs_expansion", "0");

    T_ASSERT(!FS_ArchiveFileVisible("data/War3Local.mpq", "Scripts\\human.ai"));
    T_ASSERT(!FS_ArchiveFileVisible("data/war3local.MPQ", "Scripts\\campaign.ai"));
    T_ASSERT(FS_ArchiveFileVisible("data/War3Local.mpq", "Scripts\\HumanMelee.pld"));
    T_ASSERT(FS_ArchiveFileVisible("data/War3Local.mpq", "UI\\WorldEditStrings.txt"));
    T_ASSERT(FS_ArchiveFileVisible("data/War3.mpq", "Scripts\\human.ai"));

    Cvar_Set("fs_expansion", "1");
    T_ASSERT(FS_ArchiveFileVisible("data/War3Local.mpq", "Scripts\\human.ai"));
}

TEST(commands, dash_cvars_are_not_command_line_cvars) {
    LPCSTR argv[] = { "test_commands", "-scr_showfps=0" };

    setup_command_tests();
    Cvar_Set("scr_showfps", "1");
    Cvar_ApplyCommandLine(2, argv);

    T_STREQ(Cvar_String("scr_showfps", NULL), "1");
}

TEST(commands, display_modes_require_explicit_flag) {
    LPCSTR args[] = { "test_commands", "-vid_modes" };
    LPCSTR other[] = { "test_commands", "-vid_modes_extra" };

    setup_command_tests();
    T_STREQ(Cvar_String("vid_modes", NULL), "0");
    Cvar_ApplyCommandLine(2, other);
    T_STREQ(Cvar_String("vid_modes", NULL), "0");
    Cvar_ApplyCommandLine(2, args);
    T_STREQ(Cvar_String("vid_modes", NULL), "1");
    Cvar_Set("vid_modes", "0");
}

TEST(commands, fast_forward_requires_explicit_flag) {
    LPCSTR args[] = { "test_commands", "-com_fast_forward" };
    LPCSTR other[] = { "test_commands", "-com_fast_forward_extra" };

    setup_command_tests();
    T_STREQ(Cvar_String("com_fast_forward", NULL), "0");
    Cvar_ApplyCommandLine(2, other);
    T_STREQ(Cvar_String("com_fast_forward", NULL), "0");
    Cvar_ApplyCommandLine(2, args);
    T_STREQ(Cvar_String("com_fast_forward", NULL), "1");
    Cvar_Set("com_fast_forward", "0");
}

TEST(commands, plus_cvars_apply_immediately) {
    LPCSTR argv[] = { "test_commands", "+game_port", "28010", "+scr_showfps", "0" };

    setup_command_tests();
    Cvar_Set("game_port", PORT_SERVER_STRING);
    Cvar_Set("scr_showfps", "1");
    COM_InitArgv(5, argv);
    Cbuf_AddEarlyCommands(true);

    T_STREQ(Cvar_String("game_port", NULL), "28010");
    T_STREQ(Cvar_String("scr_showfps", NULL), "0");
    Cvar_Set("scr_showfps", "1");
}

TEST(commands, obsolete_r_module_is_ignored) {
    setup_command_tests();
    T_NULL(Cvar_Set("r_module", "stdout"));
    T_NULL(Cvar_String("r_module", NULL));
}

TEST(commands, plus_map_is_early_launch_selector) {
    LPCSTR argv[] = { "test_commands", "+map", "Human02" };

    setup_command_tests();
    Cvar_Set("map", "");
    reset_map_handoff();
    COM_InitArgv(3, argv);
    Cbuf_AddEarlyCommands(true);
    Cbuf_AddLateCommands();
    Cbuf_Execute();

    T_STREQ(Cvar_String("map", NULL), "Human02");
    T_STREQ(last_loading_map, "");
    T_STREQ(last_sv_map, "");
}

TEST(commands, remaining_plus_commands_run_late) {
    LPCSTR argv[] = { "test_commands", "+test_late_command" };

    setup_command_tests();
    if (!Cmd_Exists("test_late_command")) {
        Cmd_AddCommand("test_late_command", Test_LateCommand_f);
    }
    late_command_called = false;
    COM_InitArgv(2, argv);
    Cbuf_AddEarlyCommands(true);
    Cbuf_AddLateCommands();
    Cbuf_Execute();

    T_ASSERT(late_command_called);
}

TEST(commands, screenshot_optional_delay_counts_rendered_frames) {
    setup_command_tests();
    if (!Cmd_Exists("screenshot")) Cmd_AddCommand("screenshot", CL_Screenshot_f);
    cl_screenshot_pending = false; cl_screenshot_delay = 0;
    Cmd_ExecuteString("screenshot 3");
    T_ASSERT(cl_screenshot_pending); T_EQ(cl_screenshot_delay, 3);
    T_ASSERT(!CL_ScreenshotReady()); T_ASSERT(!CL_ScreenshotReady()); T_ASSERT(CL_ScreenshotReady());
    T_ASSERT(!cl_screenshot_pending); T_EQ(cl_screenshot_delay, 0);
    Cmd_ExecuteString("screenshot"); T_ASSERT(CL_ScreenshotReady());
    Cmd_ExecuteString("screenshot invalid"); T_ASSERT(!cl_screenshot_pending);
}

typedef struct {
    DWORD count;
    bool human02;
    bool orc01;
    bool twin_w3m;
    bool twin_w3x;
} mapListState_t;

static void count_fixture_map(LPCSTR path, void *userData) {
    mapListState_t *state = userData;

    state->count++;
    if (!strcmp(path, "Maps\\Campaign\\Human02.w3m")) {
        state->human02 = true;
    } else if (!strcmp(path, "Maps\\Campaign\\Orc01.w3m")) {
        state->orc01 = true;
    } else if (!strcmp(path, "Maps\\Melee\\TwinRivers.w3m")) {
        state->twin_w3m = true;
    } else if (!strcmp(path, "Maps\\FrozenThrone\\TwinRivers.w3x")) {
        state->twin_w3x = true;
    }
}

TEST(commands, fixture_maps_are_listed_from_mpq) {
    mapListState_t state = { 0 };

    setup_command_tests();

    T_EQ(FS_ListMaps(count_fixture_map, &state), 4);
    T_EQ(state.count, 4);
    T_ASSERT(state.human02);
    T_ASSERT(state.orc01);
    T_ASSERT(state.twin_w3m);
    T_ASSERT(state.twin_w3x);
}

TEST(commands, short_map_name_resolves_from_fixture_mpq) {
    PATHSTR path;

    setup_command_tests();

    T_EQ(FS_ResolveMapPath("Human02", path, sizeof(path)), FS_MAP_RESOLVE_OK);
    T_STREQ(path, "Maps\\Campaign\\Human02.w3m");
    T_EQ(FS_ResolveMapPath("orc01", path, sizeof(path)), FS_MAP_RESOLVE_OK);
    T_STREQ(path, "Maps\\Campaign\\Orc01.w3m");
}

TEST(commands, explicit_map_path_still_resolves) {
    PATHSTR path;

    setup_command_tests();

    T_EQ(FS_ResolveMapPath("Maps/Campaign/Human02.w3m", path, sizeof(path)), FS_MAP_RESOLVE_OK);
    T_STREQ(path, "Maps\\Campaign\\Human02.w3m");
}

TEST(commands, ambiguous_short_map_name_is_rejected) {
    PATHSTR path;

    setup_command_tests();

    T_EQ(FS_ResolveMapPath("TwinRivers", path, sizeof(path)), FS_MAP_RESOLVE_AMBIGUOUS);
}

TEST(commands, map_command_uses_resolver) {
    setup_command_tests();
    reset_map_handoff();

    Cmd_ExecuteString("map Human02");

    T_STREQ(last_loading_map, "Maps\\Campaign\\Human02.w3m");
    T_STREQ(last_sv_map, "Maps\\Campaign\\Human02.w3m");
}

TEST(commands, map_command_rejects_ambiguous_short_name) {
    setup_command_tests();
    reset_map_handoff();

    Cmd_ExecuteString("map TwinRivers");

    T_STREQ(last_loading_map, "");
    T_STREQ(last_sv_map, "");
}

TEST(commands, load_command_reloads_saved_map_without_connect) {
    setup_command_tests();
    reset_map_handoff();
    Cvar_Set("dedicated", "0"); Cvar_Set("game_port", "28015");

    Cmd_ExecuteString("load quick");

    T_STREQ(last_loading_map, "Maps\\Campaign\\Human02.w3m");
    T_STREQ(last_load_name, "quick");
    T_STREQ(last_load_map, "Maps\\Campaign\\Human02.w3m");
    /* Q2 load reconnects with "new" on the existing slot, not CL_Connect. */
    T_STREQ(last_connect_host, "");
}

TEST(commands, load_command_stops_when_save_map_is_unreadable) {
    setup_command_tests();
    reset_map_handoff();
    save_map_readable = false;

    Cmd_ExecuteString("load broken");

    T_STREQ(last_loading_map, "");
    T_STREQ(last_load_name, "");
    T_STREQ(last_connect_host, "");
}
TEST(video_modes, invalid_index_uses_safe_default) {
    T_EQ(video_mode_get(-1)->width, (DWORD)640); T_EQ(video_mode_get(99)->height, (DWORD)480);
    T_EQ(video_mode_get(2)->width, (DWORD)1024); T_EQ(video_mode_get(2)->height, (DWORD)768);
}

TEST(video_modes, steam_deck_mode_is_appended_without_renumbering_existing_modes) {
    DWORD steam_deck_mode = video_mode_count() - 1;

    T_EQ(video_mode_count(), (DWORD)14);
    T_EQ(video_mode_get(5)->width, (DWORD)1280);
    T_EQ(video_mode_get(5)->height, (DWORD)960);
    T_EQ(video_mode_get((int)steam_deck_mode)->width, (DWORD)1280);
    T_EQ(video_mode_get((int)steam_deck_mode)->height, (DWORD)800);
}

TEST(video_modes, wc3_defaults_to_native_fullscreen_with_vid_mode_fallback) {
    setup_command_tests();
    Cvar_Set("vid_native", "0");
    Cvar_Set("vid_fullscreen", "0");
    Cvar_Set("vid_mode", "0");

    Cvar_LoadConfig("games/warcraft-3/share/config.cfg");
    Cbuf_Execute();

    T_STREQ(Cvar_String("vid_native", NULL), "1");
    T_STREQ(Cvar_String("vid_fullscreen", NULL), "1");
    T_STREQ(Cvar_String("vid_mode", NULL), "2");
}

TEST(video_modes, wow_defaults_allow_explicit_override) {
    LPCSTR args[] = { "test_commands", "+set", "vid_mode", "0" };

    setup_command_tests();
    Cvar_Set("vid_mode", "0");
    Cvar_LoadConfig("games/world-of-warcraft/share/config.cfg");
    Cbuf_Execute();
    T_STREQ(Cvar_String("vid_mode", NULL), "2");
    COM_InitArgv(4, args);
    Cbuf_AddEarlyCommands(true);
    T_STREQ(Cvar_String("vid_mode", NULL), "0");
}
