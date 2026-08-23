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
static char last_forwarded[1024];
static bool command_tests_initialized;
static bool late_command_called;

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

void CL_BeginLoadingMap(LPCSTR mapName) {
    snprintf(last_loading_map, sizeof(last_loading_map), "%s", mapName ? mapName : "");
}

void CL_Shutdown(void) {
}

void SV_Map(LPCSTR pFilename) {
    snprintf(last_sv_map, sizeof(last_sv_map), "%s", pFilename ? pFilename : "");
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
    last_forwarded[0] = '\0';
    late_command_called = false;
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
    T_ASSERT(Cmd_Exists("map"));
    T_ASSERT(Cmd_Exists("maps"));
    T_ASSERT(Cmd_Exists("dir"));
    T_ASSERT(Cmd_Exists("path"));
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

TEST(commands, dash_cvars_are_not_command_line_cvars) {
    LPCSTR argv[] = { "test_commands", "-r_module=stdout" };

    setup_command_tests();
    Cvar_Set("r_module", "renderer");
    Cvar_ApplyCommandLine(2, argv);

    T_STREQ(Cvar_String("r_module", NULL), "renderer");
}

TEST(commands, plus_cvars_apply_immediately) {
    LPCSTR argv[] = { "test_commands", "+game_port", "28010", "+r_module", "stdout" };

    setup_command_tests();
    Cvar_Set("game_port", PORT_SERVER_STRING);
    Cvar_Set("r_module", "renderer");
    COM_InitArgv(5, argv);
    Cbuf_AddEarlyCommands(true);

    T_STREQ(Cvar_String("game_port", NULL), "28010");
    T_STREQ(Cvar_String("r_module", NULL), "stdout");
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
TEST(video_modes, invalid_index_uses_safe_default) {
    T_EQ(video_mode_get(-1)->width, (DWORD)640); T_EQ(video_mode_get(99)->height, (DWORD)480);
    T_EQ(video_mode_get(2)->width, (DWORD)1024); T_EQ(video_mode_get(2)->height, (DWORD)768);
}
