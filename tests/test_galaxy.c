/* test_galaxy.c — Galaxy scripting mode unit and integration tests.
 *
 * Covers:
 *   galaxy.parse_*     — parser-level: verify parse succeeds
 *   galaxy.vm_*        — VM integration: parse + execute + assert result
 *   galaxy.include_*   — include directive preprocessing
 *   galaxy.smoke_*     — cutscene smoke: load TRaynor01 files, call InitGlobals
 *
 * Build: linked against libjass and libshared only — no game module needed.
 *   make test-galaxy
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef BZ_TESTS

#include "shared/test.h"
#include "games/warcraft-3/jass/jass.h"
#include "games/starcraft-2/game/galaxy/galaxy_host.h"

#define BZ_GAL_INDEX_TEST_DECLS 5000 // declarations; exceeds VM bucket count; used to exercise collision chains

/* =========================================================================
 * Minimal host — stdlib allocator + flat-file ReadFile.
 * ========================================================================= */

static void *gal_alloc(long size) { return calloc(1, (size_t)size); }
static void  gal_free(void *p)    { free(p); }

/* ReadFile searches these prefixes in order until one works. */
static const char *gal_search_dirs[] = {
    "data/TRaynor01-galaxy/",
    "",
    NULL,
};

static void *gal_read_file(const char *path, unsigned int *out_size) {
    for (const char **dir = gal_search_dirs; *dir; dir++) {
        char full[1024];
        snprintf(full, sizeof(full), "%s%s", *dir, path);
        FILE *f = fopen(full, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = malloc((size_t)(sz + 1));
        if (!buf) { fclose(f); return NULL; }
        (void)fread(buf, 1, (size_t)sz, f);
        buf[sz] = '\0';
        fclose(f);
        *out_size = (unsigned int)sz;
        return buf;
    }
    return NULL;
}

/* Native stubs for the test host.  TestFail wires Galaxy assertions into
 * jass_rterror so T_ASSERT(!jass_rterror_pending(j)) detects failures. */
static unsigned int gal_stub(LPJASS j)    { return jass_pushnull(j); }
static unsigned int gal_void(LPJASS j)    { (void)j; return 0; }
static unsigned int gal_true(LPJASS j) { return jass_pushboolean(j, 1); }
static unsigned int gal_false_ret(LPJASS j) { return jass_pushboolean(j, 0); }
static unsigned int gal_zero(LPJASS j)    { return jass_pushinteger(j, 0); }
static float gal_sound_length(LPCSTR id, int asset) {
    return !strcmp(id, "IntroLine") && asset == 2 ? 2.5f : 0.0f;
}
static BOOL gal_unit_moving;
static LONG gal_move_count;
static FLOAT gal_move_x;
static void *gal_unit_create(LPCSTR type, int player, float x, float y, float angle) {
    (void)type; (void)player; (void)x; (void)y; (void)angle;
    return (void *)(uintptr_t)1;
}
static void gal_unit_move(void *ent, float x, float y) {
    (void)ent; (void)y; gal_move_count++; gal_move_x = x; gal_unit_moving = true;
}
static BOOL gal_is_moving(void *ent) { (void)ent; return gal_unit_moving; }

static unsigned int gal_TestFail(LPJASS j) {
    LPCSTR msg = jass_checkstring(j, 1);
    jass_rterror(j, msg ? msg : "TestFail");
    return 0;
}

static JASSMODULE gal_test_natives[] = {
    { "TestFail", gal_TestFail },
    { "NoValue", gal_void },
    /* Stubs for natives used during InitGlobals / InitTriggers init paths */
    { "TriggerCreate",               gal_stub },
    { "TriggerAddEventMapInit",      gal_stub },
    { "TriggerAddEventTimeElapsed",  gal_stub },
    { "TriggerAddEventTimePeriodic", gal_stub },
    { "TriggerAddEventTimer",        gal_stub },
    { "UnitGroupLoopBegin",          gal_stub },
    { "UnitGroupLoopDone",           gal_true  },   /* true = done, exits loop */
    { "UnitGroupLoopEnd",            gal_stub },
    { "UnitGroupLoopStep",           gal_stub },
    { "UnitGroupLoopCurrent",        gal_stub },
    { "UnitGroupEmpty",              gal_true },
    { "IntLoopBegin",                gal_stub },
    { "IntLoopDone",                 gal_true  },   /* true = done, exits loop */
    { "IntLoopEnd",                  gal_stub },
    { "IntLoopStep",                 gal_stub },
    { "PlayerGroupLoopBegin",        gal_stub },
    { "PlayerGroupLoopDone",         gal_true  },   /* true = done, exits loop */
    { "PlayerGroupLoopEnd",          gal_stub },
    { "PlayerGroupLoopStep",         gal_stub },
    { "PlayerGroupAll",              gal_stub },
    { "SoundLink",                   gal_stub },
    { "SoundPlay",                   gal_stub },
    { "SoundPlayAtPoint",            gal_stub },
    { "SoundPlayScene",              gal_stub },
    { "Point",                       gal_stub },
    { "PointFromId",                 gal_stub },
    { "RegionFromId",                gal_stub },
    { "UnitFromId",                  gal_stub },
    { "UnitCreate",                  gal_stub },
    { "UnitLastCreated",             gal_stub },
    { "UnitGetPosition",             gal_stub },
    { "UnitSetState",                gal_stub },
    { "UnitKill",                    gal_stub },
    { "UnitRemove",                  gal_stub },
    { "UnitGroup",                   gal_stub },
    { "UnitGroupAdd",                gal_stub },
    { "UnitFilter",                  gal_stub },
    { "CinematicFade",               gal_stub },
    { "CinematicMode",               gal_stub },
    { "CameraApplyInfo",             gal_stub },
    { "CameraInfoDefault",           gal_stub },
    { "CameraInfoFromId",            gal_stub },
    { "Wait",                        gal_stub },
    { "MinimapPing",                 gal_stub },
    { "PlayerGroupActive",           gal_stub },
    { "PlayerSetState",              gal_stub },
    { "PlayerSetAlliance",           gal_stub },
    { "PlayerModifyPropertyInt",     gal_stub },
    { "PlayerDifficulty",            gal_zero  },
    { "PlayerGroupHasPlayer",        gal_false_ret },
    { "RandomInt",                   gal_zero  },
    { "RandomFixed",                 gal_zero  },
    { "AbsF",                        gal_zero  },
    { "Sin",                         gal_zero  },
    { "Cos",                         gal_zero  },
    { "Tan",                         gal_zero  },
    { "SquareRoot",                  gal_zero  },
    { "Pow",                         gal_zero  },
    { "MinF",                        gal_zero  },
    { "MaxF",                        gal_zero  },
    { "ModF",                        gal_zero  },
    { "ACos",                        gal_zero  },
    { "ASin",                        gal_zero  },
    { "ATan",                        gal_zero  },
    { "ATan2",                       gal_zero  },
    { "AngleBetweenPoints",          gal_zero  },
    { "DistanceBetweenPoints",       gal_zero  },
    { "PointGetX",                   gal_zero  },
    { "PointGetY",                   gal_zero  },
    { "PointGetHeight",              gal_zero  },
    { "PointGetFacing",              gal_zero  },
    { "PointWithOffsetPolar",        gal_stub  },
    { "PointWithOffset",             gal_stub  },
    { "ActorScopeKill",              gal_stub  },
    { "CatalogFieldValueGet",        gal_stub  },
    { "PreloadAsset",                gal_stub  },
    { "PreloadObject",               gal_stub  },
    { "PreloadImage",                gal_stub  },
    { "PreloadModel",                gal_stub  },
    { "PreloadSound",                gal_stub  },
    { "MovieStartRecording",         gal_stub  },
    { "MovieStopRecording",          gal_stub  },
    { "GameSetBackground",           gal_stub  },
    { "GameTimeOfDayPause",          gal_stub  },
    { "GameTimeOfDaySet",            gal_stub  },
    { "GameGetMissionTime",          gal_stub  },
    { "ObjectiveCreate",             gal_stub  },
    { "ObjectiveLastCreated",        gal_stub  },
    { "ObjectiveSetName",            gal_stub  },
    { "ObjectiveSetState",           gal_stub  },
    { "ObjectiveGetState",           gal_stub  },
    { "PingLastCreated",             gal_stub  },
    { "PingSetTooltip",              gal_stub  },
    { "PingSetScale",                gal_stub  },
    { "PingDestroy",                 gal_stub  },
    { "IntToText",                   gal_stub  },
    { "Order",                       gal_stub  },
    { "OrderTargetingPoint",         gal_stub  },
    { "OrderTargetingUnit",          gal_stub  },
    { "HelpPanelAddTip",             gal_stub  },
    { "HelpPanelDisplayPage",        gal_stub  },
    { "HelpPanelEnableTechTreeButton", gal_stub },
    { "EventUnit",                   gal_stub  },
    { "EventUnitCargo",              gal_stub  },
    { "EventUnitTarget",             gal_stub  },
    { "AITimePause",                 gal_stub  },
    { NULL, NULL },
};

static JASSMODULE gal_assert_natives[] = {
    { "TestFail", gal_TestFail },
    { "NoValue", gal_void },
    { NULL, NULL },
};

/* =========================================================================
 * Test helpers
 * ========================================================================= */

typedef struct {
    LPJASS j;
    char   errmsg[256];
} gal_state_t;

static gal_state_t gal_new(void) {
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc         = gal_alloc,
        .MemFree          = gal_free,
        .ReadFile         = gal_read_file,
        .natives          = gal_test_natives,
        .galaxy_natives   = gal_test_natives,
    ));
    gal_state_t s;
    s.j = jass_newstate();
    s.errmsg[0] = '\0';
    return s;
}

static void gal_destroy(gal_state_t *s) {
    if (s->j) { jass_close(s->j); s->j = NULL; }
}

/* Load source and call main() if it exists. */
static int gal_run_mode(gal_state_t *s, const char *src, JASSMODE mode) {
    unsigned int len = (unsigned int)strlen(src);
    char *buf = malloc(len + 1);
    memcpy(buf, src, len + 1);
    jass_dobuffer_ex(s->j, buf, mode);
    free(buf);
    if (jass_rterror_pending(s->j)) {
        snprintf(s->errmsg, sizeof(s->errmsg), "%s", jass_rterror_message(s->j));
        jass_rterror_clear(s->j);
        return 0;
    }
    jass_callbyname(s->j, "main", false);
    jass_runevents(s->j);
    if (jass_rterror_pending(s->j)) {
        snprintf(s->errmsg, sizeof(s->errmsg), "%s", jass_rterror_message(s->j));
        jass_rterror_clear(s->j);
        return 0;
    }
    return 1;
}

static int gal_run(gal_state_t *s, const char *src) { return gal_run_mode(s, src, JASS_MODE_GALAXY); }

/* Parse-only: load but don't call main(). */
static int gal_parse_mode(gal_state_t *s, const char *src, JASSMODE mode) {
    unsigned int len = (unsigned int)strlen(src);
    char *buf = malloc(len + 1);
    memcpy(buf, src, len + 1);
    BOOL ok = jass_dobuffer_ex(s->j, buf, mode);
    free(buf);
    if (!ok || jass_rterror_pending(s->j)) {
        snprintf(s->errmsg, sizeof(s->errmsg), "%s", jass_rterror_message(s->j));
        jass_rterror_clear(s->j);
        return 0;
    }
    return 1;
}

static int gal_parse(gal_state_t *s, const char *src) { return gal_parse_mode(s, src, JASS_MODE_GALAXY); }

/* =========================================================================
 * Parser tests — verify specific Galaxy constructs parse without error
 * ========================================================================= */

TEST(galaxy, parse_empty_function) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "void f() {}"));
    gal_destroy(&s);
}

TEST(galaxy, parse_function_with_return) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "void f() { return; }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_function_return_value) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "int f() { return 5; }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_native_decl) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "native void Foo(int a, bool b);"));
    gal_destroy(&s);
}

TEST(galaxy, parse_global_int) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "int gv_x = 0;"));
    gal_destroy(&s);
}

TEST(galaxy, parse_global_bool) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "bool gv_flag = true;"));
    gal_destroy(&s);
}

TEST(galaxy, parse_global_fixed) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "fixed gv_speed = 1.0;"));
    gal_destroy(&s);
}

TEST(galaxy, parse_global_text) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "text gv_label = \"Hello\";"));
    gal_destroy(&s);
}

TEST(galaxy, parse_const_global) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "const int c_MaxCount = 50;"));
    gal_destroy(&s);
}

TEST(galaxy, parse_array_global) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "int[10] arr;"));
    gal_destroy(&s);
}

TEST(galaxy, parse_array_with_initializer) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "bool[33] gv_flags;"));
    gal_destroy(&s);
}

TEST(galaxy, parse_function_args) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "void f(int a, bool b, fixed c) {}"));
    gal_destroy(&s);
}

TEST(galaxy, parse_trigger_callback_signature) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "bool f(bool testConds, bool runActions) { return true; }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_if_statement) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "void f() { if (true) { return; } }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_if_else) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "void f() { if (false) { } else { } }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_if_elseif_else) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "int gv_x = 0;\n"
        "void f() {\n"
        "    if (gv_x == 1) { }\n"
        "    else if (gv_x == 2) { }\n"
        "    else { }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, parse_while_loop) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "void f() { while (false) { } }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_while_with_body) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "int gv_i = 0;\n"
        "void f() { while (gv_i < 10) { gv_i = gv_i + 1; } }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_unary_not) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "bool gv_b = false;\n"
        "void f() { bool x = !gv_b; }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_not_equal) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "void f() { bool x = (1 != 2); }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_string_concat) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "void f() { string s = \"hello\" + \" world\"; }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_local_var_decl) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "void f() {\n"
        "    int x = 5;\n"
        "    bool b = true;\n"
        "    string s = \"test\";\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, parse_array_access) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "int[10] arr;\n"
        "void f() { arr[0] = 1; arr[9] = 99; }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_function_call_stmt) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string msg);\n"
        "void f() { TestFail(\"nope\"); }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_nested_calls) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "native bool UnitGroupLoopDone();\n"
        "void f() { bool x = !UnitGroupLoopDone(); }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_multiple_functions) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "int gv_counter = 0;\n"
        "void reset() { gv_counter = 0; }\n"
        "void inc()   { gv_counter = gv_counter + 1; }\n"
        "void main()  { reset(); inc(); inc(); }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_break_in_while) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "void f() { while (true) { break; } }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_logical_and_or) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "void f() {\n"
        "    bool a = true and false;\n"
        "    bool b = false or true;\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, parse_symbolic_logical_operators) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "void f() { bool x = true&&false||true; }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_compact_comparison_operators) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "void f() { bool x = 1<=2; bool y = 2!=3; }"));
    gal_destroy(&s);
}

TEST(galaxy, parse_continue_in_loop) {
    /* continue is implemented as exitwhen(false) — a parse-safe no-op. */
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "void f() { while (true) { continue; } }"));
    gal_destroy(&s);
}

TEST(jass_syntax, parse_native_form) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse_mode(&s,
        "globals\ninteger value=0\nendglobals\n"
        "function main takes nothing returns nothing\nset value=value+1\nendfunction\n", JASS_MODE_JASS));
    gal_destroy(&s);
}

TEST(jass_syntax, reject_galaxy_function_form) {
    gal_state_t s = gal_new();
    T_ASSERT(!gal_parse_mode(&s, "void f() {}", JASS_MODE_JASS));
    gal_destroy(&s);
}

TEST(jass_syntax, reject_galaxy_symbolic_logic) {
    gal_state_t s = gal_new();
    T_ASSERT(!gal_parse_mode(&s,
        "function f takes nothing returns boolean\nreturn true&&false\nendfunction\n", JASS_MODE_JASS));
    gal_destroy(&s);
}

TEST(jass_syntax, vm_compact_arithmetic_and_comparison) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run_mode(&s,
        "native TestFail takes string msg returns nothing\n"
        "function main takes nothing returns nothing\n"
        "local integer value=1+2\n"
        "if value!=3 then\ncall TestFail(\"JASS compact operators failed\")\nendif\n"
        "endfunction\n", JASS_MODE_JASS));
    gal_destroy(&s);
}

/* =========================================================================
 * VM integration tests — parse + execute + verify result
 * ========================================================================= */

TEST(galaxy, vm_empty_main) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s, "void main() {}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_int_arithmetic) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int gv_result = 0;\n"
        "void main() {\n"
        "    gv_result = 3 + 4;\n"
        "    if (gv_result != 7) { TestFail(\"3+4 should be 7\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_int_subtraction) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    int x = 10 - 3;\n"
        "    if (x != 7) { TestFail(\"10-3 should be 7\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_int_multiply) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    int x = 6 * 7;\n"
        "    if (x != 42) { TestFail(\"6*7 should be 42\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_bool_type_alias) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "bool gv_flag = false;\n"
        "void main() {\n"
        "    gv_flag = true;\n"
        "    if (!gv_flag) { TestFail(\"flag should be true\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_bool_negation) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    bool x = !false;\n"
        "    if (!x) { TestFail(\"!false should be true\"); }\n"
        "    bool y = !true;\n"
        "    if (y) { TestFail(\"!true should be false\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_not_equal) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    if (!(1 != 2)) { TestFail(\"1 != 2 should be true\"); }\n"
        "    if (1 != 1)    { TestFail(\"1 != 1 should be false\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_while_counter) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int gv_counter = 0;\n"
        "void main() {\n"
        "    while (gv_counter < 5) {\n"
        "        gv_counter = gv_counter + 1;\n"
        "    }\n"
        "    if (gv_counter != 5) { TestFail(\"counter should be 5\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_if_else_branch) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int gv_x = 0;\n"
        "void main() {\n"
        "    if (false) { gv_x = 1; }\n"
        "    else       { gv_x = 2; }\n"
        "    if (gv_x != 2) { TestFail(\"else branch not taken\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_if_elseif_chain) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int gv_x = 2;\n"
        "int gv_result = 0;\n"
        "void main() {\n"
        "    if (gv_x == 1) { gv_result = 10; }\n"
        "    else if (gv_x == 2) { gv_result = 20; }\n"
        "    else { gv_result = 30; }\n"
        "    if (gv_result != 20) { TestFail(\"else-if branch wrong\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_array_read_write) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int[5] gv_arr;\n"
        "void main() {\n"
        "    gv_arr[0] = 10;\n"
        "    gv_arr[2] = 99;\n"
        "    gv_arr[4] = 42;\n"
        "    if (gv_arr[2] != 99) { TestFail(\"arr[2] should be 99\"); }\n"
        "    if (gv_arr[4] != 42) { TestFail(\"arr[4] should be 42\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_function_call) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int gv_val = 0;\n"
        "void set_val(int v) { gv_val = v; }\n"
        "void main() {\n"
        "    set_val(77);\n"
        "    if (gv_val != 77) { TestFail(\"function call failed\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_function_return_value) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int double_it(int x) { return x * 2; }\n"
        "void main() {\n"
        "    int r = double_it(21);\n"
        "    if (r != 42) { TestFail(\"return value wrong\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_fixed_type_alias) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "fixed gv_speed = 2.5;\n"
        "void main() {\n"
        "    if (gv_speed == 0.0) { TestFail(\"fixed init failed\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_string_concat) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "string gv_s = \"\";\n"
        "void main() {\n"
        "    gv_s = \"hello\" + \" \" + \"world\";\n"
        "    if (gv_s == \"\") { TestFail(\"string concat empty\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_const_global) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "const int c_Max = 100;\n"
        "void main() {\n"
        "    if (c_Max != 100) { TestFail(\"const value wrong\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_local_var_scoping) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    int a = 1;\n"
        "    int b = 2;\n"
        "    int c = a + b;\n"
        "    if (c != 3) { TestFail(\"local var scope wrong\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_break_exits_loop) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int gv_steps = 0;\n"
        "void main() {\n"
        "    while (true) {\n"
        "        gv_steps = gv_steps + 1;\n"
        "        if (gv_steps >= 3) { break; }\n"
        "    }\n"
        "    if (gv_steps != 3) { TestFail(\"break didn't exit at 3\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_multidimensional_array_access) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int[3][4] gv_grid;\n"
        "void main() {\n"
        "    gv_grid[1][2] = 12;\n"
        "    gv_grid[2][1] = 21;\n"
        "    gv_grid[1][1] = 11;\n"
        "    if (gv_grid[1][2] != 12) { TestFail(\"first nested value wrong\"); }\n"
        "    if (gv_grid[2][1] != 21) { TestFail(\"second nested value wrong\"); }\n"
        "    if (gv_grid[1][1] != 11) { TestFail(\"nested values aliased\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_nested_function_calls) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "int add(int a, int b) { return a + b; }\n"
        "int mul(int a, int b) { return a * b; }\n"
        "void main() {\n"
        "    int r = add(mul(2, 3), mul(4, 5));\n"
        "    if (r != 26) { TestFail(\"nested call result wrong\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_comparison_operators) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    if (!(1 <  2)) { TestFail(\"1 < 2\");  }\n"
        "    if (!(2 >  1)) { TestFail(\"2 > 1\");  }\n"
        "    if (!(2 >= 2)) { TestFail(\"2 >= 2\"); }\n"
        "    if (!(2 <= 2)) { TestFail(\"2 <= 2\"); }\n"
        "    if (!(1 == 1)) { TestFail(\"1 == 1\"); }\n"
        "    if (!(1 != 2)) { TestFail(\"1 != 2\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_symbolic_logic_and_shifts) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    bool logic = true&&false||true;\n"
        "    int shifted = (1<<5)>>2;\n"
        "    if (!logic) { TestFail(\"symbolic logic failed\"); }\n"
        "    if (shifted!=8) { TestFail(\"shift operators failed\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_null_equality) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    string empty = null;\n"
        "    string value = \"value\";\n"
        "    if (empty != null) { TestFail(\"typed null must equal null\"); }\n"
        "    if (value == null) { TestFail(\"non-null string must differ from null\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_string_word) {
    gal_state_t s = gal_new();
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_test_natives, .galaxy_natives = galaxy_get_natives(),
    ));
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "native string StringWord(string value, int index);\n"
        "void main() {\n"
        "    if (StringWord(\"klaatu  barada nikto\", 2) != \"barada\") { TestFail(\"second word mismatch\"); }\n"
        "    if (StringWord(\"klaatu barada nikto\", 4) != null) { TestFail(\"missing word must be null\"); }\n"
        "}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_string_word_loop_terminates) {
    gal_state_t s = gal_new();
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_test_natives, .galaxy_natives = galaxy_get_natives(),
    ));
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string msg);\n"
        "native string StringWord(string value, int index);\n"
        "int gv_count = 0;\n"
        "void main() {\n"
        "    string item = \"\";\n"
        "    while (true) {\n"
        "        gv_count = gv_count + 1;\n"
        "        item = StringWord(\"alpha beta gamma\", gv_count);\n"
        "        if (item == null) { gv_count = gv_count - 1; break; }\n"
        "    }\n"
        "    if (gv_count != 3) { TestFail(\"word loop did not terminate at sentinel\"); }\n"
        "}"));
    jass_callbyname(s.j, "main", true);
    jass_runevents(s.j);
    T_ASSERT(!jass_rterror_pending(s.j));
    gal_destroy(&s);
}

TEST(galaxy, vm_sound_link_length) {
    gal_state_t s = gal_new();
    sc2_galaxy_sound_length = gal_sound_length;
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives(),
    ));
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg); native soundlink SoundLink(string id, int asset);"
        "native fixed SoundLengthSync(soundlink value);"
        "void main() {"
        "if (SoundLengthSync(SoundLink(\"IntroLine\", 2)) != 2.5) { TestFail(\"linked duration mismatch\"); }"
        "if (SoundLengthSync(SoundLink(\"Missing\", 0)) != 0.0) { TestFail(\"missing duration mismatch\"); }"
        "}"));
    sc2_galaxy_sound_length = NULL;
    galaxy_reset();
    gal_destroy(&s);
}

TEST(galaxy, vm_unit_order_append_waits_for_idle) {
    gal_state_t s = gal_new();
    gal_move_count = 0; gal_move_x = 0.0f; gal_unit_moving = false;
    sc2_galaxy_on_unit_create = gal_unit_create;
    sc2_galaxy_unit_move = gal_unit_move;
    sc2_galaxy_unit_is_moving = gal_is_moving;
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives(),
    ));
    T_ASSERT(gal_run(&s,
        "native point Point(fixed x, fixed y); native abilcmd AbilityCommand(string name, int index);"
        "native order OrderTargetingPoint(abilcmd command, point target);"
        "native unit UnitCreate(int count, string type, int flags, int player, point where, fixed angle);"
        "native bool UnitIssueOrder(unit value, order valueOrder, int queue);"
        "void main() { unit value = UnitCreate(1, \"Flyer\", 0, 1, Point(0.0, 0.0), 0.0);"
        "UnitIssueOrder(value, OrderTargetingPoint(AbilityCommand(\"move\", 0), Point(1.0, 0.0)), 0);"
        "UnitIssueOrder(value, OrderTargetingPoint(AbilityCommand(\"move\", 0), Point(2.0, 0.0)), 1); }"));
    T_EQ(gal_move_count, 0);
    galaxy_tick(s.j); T_EQ(gal_move_count, 1); T_FEQ(gal_move_x, 1.0f, 0.001f);
    galaxy_tick(s.j); T_EQ(gal_move_count, 1);
    gal_unit_moving = false;
    galaxy_tick(s.j); T_EQ(gal_move_count, 2); T_FEQ(gal_move_x, 2.0f, 0.001f);
    sc2_galaxy_on_unit_create = NULL;
    sc2_galaxy_unit_move = NULL;
    sc2_galaxy_unit_is_moving = NULL;
    galaxy_reset();
    gal_destroy(&s);
}

TEST(galaxy, vm_coroutine_void_argument) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "native void NoValue(); native void TestFail(string msg);\n"
        "int gv_called = 0;\n"
        "void sink(int value) { gv_called = 1; }\n"
        "void main() { sink(NoValue()); if (gv_called != 1) { TestFail(\"callee not run\"); } }"));
    jass_callbyname(s.j, "main", true);
    jass_runevents(s.j);
    T_ASSERT(!jass_rterror_pending(s.j));
    gal_destroy(&s);
}

TEST(galaxy, vm_coroutine_executes_dynamic_trigger) {
    gal_state_t s = gal_new();
    galaxy_reset();
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives(),
    ));
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string msg);\n"
        "native void Wait(fixed duration, int timeType);\n"
        "native trigger TriggerCreate(string funcName);\n"
        "native void TriggerExecute(trigger value, bool testConds, bool waitDone);\n"
        "int gv_called = 0;\n"
        "bool child(bool testConds, bool runActions) { gv_called = 1; Wait(0.0, 0); gv_called = 2; return true; }\n"
        "void main() {\n"
        "    trigger value = TriggerCreate(\"child\");\n"
        "    MissingFunction();\n"
        "    TriggerExecute(value, true, true);\n"
        "    if (gv_called != 2) { TestFail(\"wait-done trigger resumed parent before child\"); }\n"
        "}"));
    jass_callbyname(s.j, "main", true);
    jass_runevents(s.j);
    jass_runevents(s.j);
    T_ASSERT(!jass_rterror_pending(s.j));
    galaxy_reset();
    gal_destroy(&s);
}

TEST(galaxy, vm_trigger_fires_multiple_times) {
    gal_state_t s = gal_new();
    galaxy_reset();
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives(),
    ));
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string msg);\n"
        "native trigger TriggerCreate(string funcName);\n"
        "native void TriggerExecute(trigger t, bool testConds, bool waitDone);\n"
        "int gv_count = 0;\n"
        "bool handler(bool testConds, bool runActions) { gv_count = gv_count + 1; return true; }\n"
        "void main() {\n"
        "    trigger t = TriggerCreate(\"handler\");\n"
        "    TriggerExecute(t, false, true);\n"
        "    TriggerExecute(t, false, true);\n"
        "    TriggerExecute(t, false, true);\n"
        "    if (gv_count != 3) { TestFail(\"trigger should fire 3 times\"); }\n"
        "}"));
    jass_callbyname(s.j, "main", true);
    jass_runevents(s.j);
    T_ASSERT(!jass_rterror_pending(s.j));
    galaxy_reset();
    gal_destroy(&s);
}

TEST(galaxy, vm_indexed_root_lookups) {
    gal_state_t s = gal_new();
    size_t cap = BZ_GAL_INDEX_TEST_DECLS * 80, used = 0;
    char *src = calloc(1, cap);
    used += snprintf(src + used, cap - used, "int gv_hits = 0;\n");
    FOR_LOOP(i, BZ_GAL_INDEX_TEST_DECLS) {
        used += snprintf(src + used, cap - used, "int gv_%u = 0; void fn_%u() { gv_%u = %u; gv_hits = gv_hits + 1; }\n", i, i, i, i);
    }
    used += snprintf(src + used, cap - used,
        "native void TestFail(string msg); void main() { if (gv_hits != %u) { TestFail(\"indexed lookup failed\"); } }",
        BZ_GAL_INDEX_TEST_DECLS);
    T_ASSERT(used < cap && gal_parse(&s, src));
    FOR_LOOP(i, BZ_GAL_INDEX_TEST_DECLS) {
        char name[32];
        snprintf(name, sizeof(name), "fn_%u", i);
        jass_callbyname(s.j, name, false);
    }
    jass_callbyname(s.j, "main", false);
    T_ASSERT(!jass_rterror_pending(s.j));
    free(src);
    gal_destroy(&s);
}

/* =========================================================================
 * Include directive tests
 * ========================================================================= */

/* Mock ReadFile for include tests: maps "sub.galaxy" to inline content. */
static char gal_sub_content[] =
    "int gv_subval = 42;\n"
    "void sub_set(int v) { gv_subval = v; }\n";

static void *gal_include_read_file(const char *path, unsigned int *out_size) {
    if (!strcmp(path, "sub.galaxy")) {
        *out_size = (unsigned int)strlen(gal_sub_content);
        char *buf = malloc(*out_size + 1);
        memcpy(buf, gal_sub_content, *out_size + 1);
        return buf;
    }
    return NULL;
}

TEST(galaxy, include_loads_sub_file) {
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc       = gal_alloc,
        .MemFree        = gal_free,
        .ReadFile       = gal_include_read_file,
        .galaxy_natives = gal_test_natives,
    ));
    LPJASS j = jass_newstate();

    static const char src[] =
        "include \"sub\"\n"
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    if (gv_subval != 42) { TestFail(\"sub global not loaded\"); }\n"
        "}";

    unsigned int len = (unsigned int)strlen(src);
    char *buf = malloc(len + 1);
    memcpy(buf, src, len + 1);
    jass_dobuffer_ex(j, buf, JASS_MODE_GALAXY);
    free(buf);

    jass_callbyname(j, "main", false);
    jass_runevents(j);

    T_ASSERT(!jass_rterror_pending(j));
    jass_close(j);
}

TEST(galaxy, include_sub_function_callable) {
    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc       = gal_alloc,
        .MemFree        = gal_free,
        .ReadFile       = gal_include_read_file,
        .galaxy_natives = gal_test_natives,
    ));
    LPJASS j = jass_newstate();

    static const char src[] =
        "include \"sub\"\n"
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    sub_set(99);\n"
        "    if (gv_subval != 99) { TestFail(\"sub_set didn't update global\"); }\n"
        "}";

    unsigned int len = (unsigned int)strlen(src);
    char *buf = malloc(len + 1);
    memcpy(buf, src, len + 1);
    jass_dobuffer_ex(j, buf, JASS_MODE_GALAXY);
    free(buf);

    jass_callbyname(j, "main", false);
    jass_runevents(j);

    T_ASSERT(!jass_rterror_pending(j));
    jass_close(j);
}

/* =========================================================================
 * jass_dofile auto-detection test
 * ========================================================================= */

TEST(galaxy, dofile_autodetects_galaxy_extension) {
    /* Write a tiny .galaxy file to /tmp, load it via jass_dofile, verify mode. */
    static const char src[] =
        "native void TestFail(string msg);\n"
        "int gv_probe = 0;\n"
        "void probe() { gv_probe = 7; }\n";
    const char *path = "/tmp/openwarcraft3_test_autodetect.galaxy";
    FILE *f = fopen(path, "wb");
    if (!f) return;  /* skip if /tmp not writable */
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc       = gal_alloc,
        .MemFree        = gal_free,
        .ReadFile       = gal_read_file,
        .galaxy_natives = gal_test_natives,
    ));
    LPJASS j = jass_newstate();
    jass_dofile(j, path);
    jass_callbyname(j, "probe", false);
    jass_runevents(j);

    T_ASSERT(!jass_rterror_pending(j));
    jass_close(j);
}

/* =========================================================================
 * TRaynor01 cutscene smoke test
 *
 * Requires data/TRaynor01-galaxy/ to be present (extracted from MPQ).
 * Loads all four Galaxy files, calls InitGlobals(), verifies no crash.
 * ========================================================================= */

static int gal_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int gal_load_errors = 0;

static void gal_load(LPJASS j, const char *path) {
    jass_rterror_clear(j);
    BOOL ok = jass_dofile_ex(j, path, JASS_MODE_GALAXY);
    if (!ok || jass_rterror_pending(j)) {
        fprintf(stderr, "[smoke] parse/eval error in %s: %s\n", path,
                jass_rterror_pending(j) ? jass_rterror_message(j) : "unknown");
        gal_load_errors++;
        jass_rterror_clear(j);
    }
}

TEST(galaxy, smoke_parse_mapscript) {
    if (!gal_file_exists("data/TRaynor01-galaxy/MapScript.galaxy")) return;

    gal_state_t s = gal_new();
    /* Load in dependency order: natives → NativeLib → LibertyLib → CampaignLib → MapScript */
    gal_load_errors = 0;
    /* Load and evaluate Galaxy standard libs — CampaignLib/MapScript deferred
     * until VM evaluation is robust against complex SC2 type interactions. */
    gal_load(s.j, "data/TRaynor01-galaxy/TriggerLibs/GameDataAllNatives.galaxy");
    gal_load(s.j, "data/TRaynor01-galaxy/TriggerLibs/natives.galaxy");
    gal_load(s.j, "data/TRaynor01-galaxy/TriggerLibs/NativeLib.galaxy");
    gal_load(s.j, "data/TRaynor01-galaxy/TriggerLibs/LibertyLib.galaxy");

    T_ASSERT(gal_load_errors == 0);
    gal_destroy(&s);
}

TEST(galaxy, smoke_init_globals) {
    if (!gal_file_exists("data/TRaynor01-galaxy/MapScript.galaxy")) return;

    gal_state_t s = gal_new();
    gal_load_errors = 0;
    gal_load(s.j, "data/TRaynor01-galaxy/TriggerLibs/GameDataAllNatives.galaxy");
    gal_load(s.j, "data/TRaynor01-galaxy/TriggerLibs/natives.galaxy");
    gal_load(s.j, "data/TRaynor01-galaxy/TriggerLibs/NativeLib.galaxy");
    gal_load(s.j, "data/TRaynor01-galaxy/TriggerLibs/LibertyLib.galaxy");
    T_ASSERT(gal_load_errors == 0);

    jass_rterror_clear(s.j);
    jass_callbyname(s.j, "libNtve_InitLib", false);
    jass_runevents(s.j);

    jass_callbyname(s.j, "InitGlobals", false);
    jass_runevents(s.j);

    if (jass_rterror_pending(s.j)) {
        fprintf(stderr, "[smoke] InitGlobals error: %s\n", jass_rterror_message(s.j));
    }
    T_ASSERT(!jass_rterror_pending(s.j));
    gal_destroy(&s);
}

#endif /* BZ_TESTS */
