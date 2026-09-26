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
#include <math.h>

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
static char const *gal_search_dirs[] = {
    "data/TRaynor01-galaxy/",
    "",
    NULL,
};

static void *gal_read_file(char const *path, unsigned int *out_size) {
    for (char const **dir = gal_search_dirs; *dir; dir++) {
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
static unsigned int gal_stub(jass_t *j)    { return jass_pushnull(j); }
static unsigned int gal_void(jass_t *j)    { (void)j; return 0; }
static unsigned int gal_true(jass_t *j) { return jass_pushboolean(j, 1); }
static unsigned int gal_false_ret(jass_t *j) { return jass_pushboolean(j, 0); }
static unsigned int gal_zero(jass_t *j)    { return jass_pushinteger(j, 0); }
static float gal_sound_length(cstring_t id, int asset) {
    return !strcmp(id, "IntroLine") && asset == 2 ? 2.5f : 0.0f;
}
static uint32_t gal_actor_destroyed, gal_actor_last;
static void gal_actor_destroy(unsigned id) { gal_actor_destroyed++; gal_actor_last = id; }
static bool gal_unit_moving;
static int32_t gal_move_count;
static float gal_move_x;
static jassFunc_t const *gal_saved_code;
static uint32_t gal_code_calls;
static void *gal_unit_create(cstring_t type, int player, float x, float y, float angle) {
    (void)type; (void)player; (void)x; (void)y; (void)angle;
    return (void *)(uintptr_t)1;
}
static float gal_created_angle, gal_facing_angle;
static void *gal_facing_create(cstring_t type, int player, float x, float y, float angle) {
    gal_created_angle = angle;
    return gal_unit_create(type, player, x, y, angle);
}
static void gal_set_facing(void *ent, float x, float y, float angle) {
    (void)ent; (void)x; (void)y; gal_facing_angle = angle;
}
static int gal_owners[8], gal_units;
static void *gal_owned_create(cstring_t type, int player, float x, float y, float angle) {
    (void)type; (void)x; (void)y; (void)angle;
    gal_owners[gal_units] = player;
    return &gal_owners[gal_units++];
}
static int gal_unit_owner(void *ent) { return *(int *)ent; }
static void gal_unit_move(void *ent, float x, float y) {
    (void)ent; (void)y; gal_move_count++; gal_move_x = x; gal_unit_moving = true;
}
static bool gal_is_moving(void *ent) { (void)ent; return gal_unit_moving; }

static uint32_t gal_save_code(jass_t *j) { gal_saved_code = jass_checkcode(j, 1); return 0; }
static uint32_t gal_call_saved(jass_t *j) { jass_pushfunction(j, gal_saved_code); return jass_call(j, 0); }
static uint32_t gal_code_callback(jass_t *j) { (void)j; gal_code_calls++; return 0; }

static unsigned int gal_TestFail(jass_t *j) {
    cstring_t msg = jass_checkstring(j, 1);
    jass_rterror(j, msg ? msg : "TestFail");
    return 0;
}

static jassModule_t gal_test_natives[] = {
    { "TestFail", gal_TestFail },
    { "NoValue", gal_void },
    { "SaveCode", gal_save_code },
    { "CallSaved", gal_call_saved },
    { "NativeCallback", gal_code_callback },
    /* Stubs for natives used during InitGlobals / InitTriggers init paths */
    { "TriggerCreate",               gal_stub },
    { "TriggerAddEventMapInit",      gal_stub },
    { "TriggerAddEventTimeElapsed",  gal_stub },
    { "TriggerAddEventTimePeriodic", gal_stub },
    { "TriggerAddEventTimer",        gal_stub },
    { "TriggerAddEventDialogControl", gal_stub },
    { "TriggerAddEventUnitProperty", gal_stub },
    { "TriggerEnable",               gal_stub },
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
    { "PlayerGroupEmpty",            gal_stub },
    { "Color",                       gal_stub },
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

static jassModule_t gal_assert_natives[] = {
    { "TestFail", gal_TestFail },
    { "NoValue", gal_void },
    { NULL, NULL },
};

/* =========================================================================
 * Test helpers
 * ========================================================================= */

typedef struct {
    jass_t *j;
    char   errmsg[256];
} gal_state_t;

static gal_state_t gal_new(void) {
    jass_sethost(&MAKE(jassHost_t,
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
static int gal_run_mode(gal_state_t *s, char const *src, JASSMODE mode) {
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

static int gal_run(gal_state_t *s, char const *src) { return gal_run_mode(s, src, JASS_MODE_GALAXY); }

/* Parse-only: load but don't call main(). */
static int gal_parse_mode(gal_state_t *s, char const *src, JASSMODE mode) {
    unsigned int len = (unsigned int)strlen(src);
    char *buf = malloc(len + 1);
    memcpy(buf, src, len + 1);
    bool ok = jass_dobuffer_ex(s->j, buf, mode);
    free(buf);
    if (!ok || jass_rterror_pending(s->j)) {
        snprintf(s->errmsg, sizeof(s->errmsg), "%s", jass_rterror_message(s->j));
        jass_rterror_clear(s->j);
        return 0;
    }
    return 1;
}

static int gal_parse(gal_state_t *s, char const *src) { return gal_parse_mode(s, src, JASS_MODE_GALAXY); }

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

TEST(jass_syntax, unresolved_native_reports_runtime_error) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse_mode(&s,
        "native MissingAIAction takes nothing returns nothing\n"
        "function main takes nothing returns nothing\ncall MissingAIAction()\nendfunction\n", JASS_MODE_JASS));
    jass_callbyname(s.j, "main", true);
    jass_runevents(s.j);
    T_ASSERT(jass_rterror_pending(s.j));
    T_ASSERT(!strcmp(jass_rterror_message(s.j), "unimplemented native: MissingAIAction"));
    gal_destroy(&s);
}

TEST(jass_syntax, unresolved_native_stops_synchronous_call) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse_mode(&s,
        "native MissingAIAction takes nothing returns nothing\n"
        "native TestFail takes string msg returns nothing\n"
        "globals\ninteger value=0\nendglobals\n"
        "function main takes nothing returns nothing\ncall MissingAIAction()\nset value=1\nendfunction\n"
        "function verify takes nothing returns nothing\n"
        "if value!=0 then\ncall TestFail(\"continued after native error\")\nendif\nendfunction\n", JASS_MODE_JASS));
    jass_callbyname(s.j, "main", false);
    T_ASSERT(jass_rterror_pending(s.j));
    jass_rterror_clear(s.j);
    jass_callbyname(s.j, "verify", false);
    T_ASSERT(!jass_rterror_pending(s.j));
    gal_destroy(&s);
}

TEST(jass_syntax, native_code_survives_script_argument) {
    gal_state_t s = gal_new();
    gal_saved_code = NULL;
    gal_code_calls = 0;
    T_ASSERT(gal_run_mode(&s,
        "native SaveCode takes code callback returns nothing\n"
        "native CallSaved takes nothing returns nothing\n"
        "native NativeCallback takes nothing returns nothing\n"
        "function forward takes code callback returns nothing\n"
        "call SaveCode(callback)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "call forward(function NativeCallback)\n"
        "call CallSaved()\n"
        "endfunction\n", JASS_MODE_JASS));
    T_EQ(gal_code_calls, 1);
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

/* CampaignLib declares local constants before ordinary locals in coroutine-dispatched helpers. */
TEST(galaxy, vm_const_locals) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string msg);\n"
        "void main() {\n"
        "    const int lv_type = 2; int index = lv_type + 1;\n"
        "    const bool enabled = true; const string name = \"path\";\n"
        "    if (index != 3 || !enabled || name != \"path\") { TestFail(\"const local lost\"); }\n"
        "}"));
    FOR_LOOP(i, 2) {
        jass_callbyname(s.j, "main", i != 0);
        jass_runevents(s.j);
        T_ASSERT(!jass_rterror_pending(s.j));
    }
    gal_destroy(&s);
}

/* The real native takes one integer, including when nested in campaign text concatenation. */
TEST(galaxy, vm_format_number) {
    gal_state_t s = gal_new();
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg); native text FormatNumber(int number);\n"
        "text credits(int value) { return \"Credits: \" + FormatNumber(value); }\n"
        "void main() {\n"
        "    if (credits(1234567) != \"Credits: 1,234,567\") { TestFail(\"nested format\"); }\n"
        "    if (FormatNumber(-1234) != \"-1,234\") { TestFail(\"negative format\"); }\n"
        "    if (FormatNumber(0) != \"0\" || FormatNumber(999) != \"999\") { TestFail(\"small format\"); }\n"
        "}"));
    gal_destroy(&s);
}

/* Match the authored animation call, finite replacement limits, and both case modes. */
TEST(galaxy, vm_replace_word) {
    gal_state_t s = gal_new();
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "native string StringReplaceWord(string s, string word, string replace, int maxCount, bool caseSens);\n"
        "void main() {\n"
        "    if (StringReplaceWord(\"Stand Work Start\", \" \", \",\", 0, true) != \"Stand,Work,Start\") { TestFail(\"animation\"); }\n"
        "    if (StringReplaceWord(\"aAa\", \"a\", \"xx\", 1, false) != \"xxAa\") { TestFail(\"limit\"); }\n"
        "    if (StringReplaceWord(\"aAa\", \"a\", \"\", -1, true) != \"A\") { TestFail(\"case\"); }\n"
        "    if (StringReplaceWord(\"aAa\", \"a\", \"xx\", -1, false) != \"xxxxxx\") { TestFail(\"all\"); }\n"
        "    string src = \"a\"; int i = 0;\n"
        "    while (i < 11) { src = src + src; i = i + 1; }\n"
        "    if (StringReplaceWord(src, \"a\", \"aa\", 0, true) != src + src) { TestFail(\"truncation\"); }\n"
        "}"));
    gal_destroy(&s);
}

/* CinematicFade's color uses fixed percentages, while Color supplies opaque alpha. */
TEST(galaxy, vm_color_percentages) {
    gal_state_t s = gal_new();
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
    T_ASSERT(gal_parse(&s,
        "native color Color(fixed r, fixed g, fixed b);\n"
        "native color ColorWithAlpha(fixed r, fixed g, fixed b, fixed a);\n"
        "color opaque() { return Color(100.0, 50.2, 0.0); }\n"
        "color alpha() { return ColorWithAlpha(0.0, 100.0, 50.2, 50.2); }\n"
        "color clear() { return ColorWithAlpha(0.0, 0.0, 0.0, 0.0); }"));
    cstring_t names[] = {
        "opaque",
        "alpha",
        "clear"
    };
    uint32_t values[] = { 0xffff8000u, 0x8000ff80u, 0 };
    FOR_LOOP(i, 3) {
        jass_callbyname(s.j, names[i], false);
        T_ASSERT(!jass_rterror_pending(s.j));
        T_EQ((uint32_t)jass_checkinteger(s.j, -1), values[i]);
        jass_pop(s.j, 1);
    }
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

/* A failed callback must not execute dependent statements or prevent other callbacks from running. */
TEST(galaxy, vm_unknown_calls_are_protected) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string msg); native void MissingNative();"
        "int value = 0;"
        "void nested() { value = UnknownExpression() + 1; value = 99; }"
        "void failing() { nested(); value = 99; }"
        "void declared() { MissingNative(); value = 99; }"
        "void good() { value = value + 1; }"
        "void verify() { if (value != 1) { TestFail(\"error escaped callback boundary\"); } }"));
    jass_callbyname(s.j, "failing", false);
    T_ASSERT(jass_rterror_pending(s.j));
    T_EQ(jass_missingcount(s.j), 1);
    T_STREQ(jass_missingname(s.j, 0), "UnknownExpression");
    jass_rterror_clear(s.j);
    jass_callbyname(s.j, "declared", true);
    jass_callbyname(s.j, "failing", true);
    jass_callbyname(s.j, "good", true);
    jass_runevents(s.j);
    T_ASSERT(jass_rterror_pending(s.j));
    T_EQ(jass_missingcount(s.j), 2);
    T_NULL(jass_missingname(s.j, 2));
    jass_rterror_clear(s.j);
    jass_callbyname(s.j, "verify", false);
    T_ASSERT(!jass_rterror_pending(s.j));
    gal_destroy(&s);
}

TEST(galaxy, vm_unknown_global_initializer_is_protected) {
    gal_state_t s = gal_new();
    T_ASSERT(!gal_parse(&s, "int value = MissingInitializer();"));
    T_EQ(jass_missingcount(s.j), 1);
    T_ASSERT(gal_run(&s, "void main() {}"));
    gal_destroy(&s);
}

TEST(galaxy, vm_objective_lifecycle) {
    gal_state_t s = gal_new();
    galaxy_reset();
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free, .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);"
        "void main() {"
        "int hq = ObjectiveCreate(\"Destroy HQ\", \"Primary mission\", 1, true);"
        "int boards = ObjectiveCreate3(\"Holoboards\", \"Optional mission\", 1, true, false);"
        "if (hq == 0 || boards == hq || ObjectiveLastCreated() != boards) { TestFail(\"objective identity\"); }"
        "if (!ObjectiveGetPrimary(hq) || ObjectiveGetPrimary(boards)) { TestFail(\"objective primary\"); }"
        "ObjectiveSetName(boards, \"Holoboards (\" + IntToText(1) + \"/6)\"); ObjectiveSetState(hq, 2);"
        "if (ObjectiveGetState(hq) != 2 || ObjectiveGetState(boards) != 1) { TestFail(\"objective state\"); }"
        "if (ObjectiveGetName(boards) != \"Holoboards (1/6)\") { TestFail(\"objective name\"); }"
        "if (ObjectiveGetDescription(hq) != \"Primary mission\") { TestFail(\"objective description\"); }"
        "ObjectiveDestroy(hq); if (ObjectiveGetState(hq) != -1) { TestFail(\"destroyed objective\"); }"
        "}"));
    galaxy_reset();
    T_ASSERT(gal_run(&s, "void main() { if (ObjectiveLastCreated() != 0) { TestFail(\"objective reset\"); } }"));
    gal_destroy(&s);
}

/* Cargo must retain the real transport owner rather than defaulting to neutral. */
TEST(galaxy, cargo_inherits_transport_owner) {
    gal_state_t s = gal_new();
    galaxy_reset(); gal_units = 0;
    sc2_galaxy_on_unit_create = gal_owned_create;
    sc2_galaxy_unit_owner = gal_unit_owner;
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free, .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg); void main() {"
        "UnitCreate(1, \"Dropship\", 0, 4, Point(0.0, 0.0), 0.0);"
        "unit ship = UnitLastCreated(); UnitCargoCreate(ship, \"Marine\", 2);"
        "if (UnitGetOwner(UnitCargoLastCreated()) != 4) { TestFail(\"cargo owner\"); }"
        "if (UnitGetOwner(ship) != 4) { TestFail(\"transport owner\"); }"
        "UnitCargoCreate(null, \"Marine\", 1);"
        "}"));
    T_EQ(gal_units, 3); T_EQ(gal_owners[1], 4); T_EQ(gal_owners[2], 4);
    sc2_galaxy_on_unit_create = NULL; sc2_galaxy_unit_owner = NULL;
    galaxy_reset(); gal_destroy(&s);
}

TEST(galaxy, vm_actor_scope_first) {
    gal_state_t s = gal_new();
    galaxy_reset();
    gal_actor_destroyed = gal_actor_last = 0;
    sc2_galaxy_on_actor_destroy = gal_actor_destroy;
    sc2_galaxy_on_unit_create = gal_unit_create;
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free, .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);"
        "native actorscope ActorScopeFromUnit(unit u);"
        "native actor ActorCreate(actorscope scope, string name, string c1, string c2, string c3);"
        "native actor ActorFrom(string name);"
        "native actorscope ActorScopeFrom(string name); native void ActorScopeKill(actorscope scope);"
        "void main() {"
        "UnitCreate(1, \"Raynor\", 0, 1, Point(0.0, 0.0), 0.0);"
        "actorscope scope = ActorScopeFromUnit(UnitLastCreated());"
        "if (scope == null) { TestFail(\"live actor scope missing\"); }"
        "actor site = ActorCreate(scope, \"SiteHosted\", \"Origin\", \"\", \"\");"
        "if (site == null || ActorFrom(\"::LastCreated\") != site) { TestFail(\"site identity\"); }"
        "actor icon = ActorCreate(scope, \"TalkIcon\", \"\", \"\", \"\");"
        "if (icon == site || ActorFrom(\"::LastCreated\") != icon) { TestFail(\"icon identity\"); }"
        "ActorScopeKill(ActorScopeFrom(\"::LastCreated\"));"
        "if (ActorFrom(\"::LastCreated\") != null) { TestFail(\"destroyed actor identity\"); }"
        "ActorScopeKill(ActorScopeFrom(\"::LastCreated\"));"
        "}"));
    T_EQ(gal_actor_destroyed, 1); T_EQ(gal_actor_last, 2);
    sc2_galaxy_on_actor_destroy = NULL;
    sc2_galaxy_on_unit_create = NULL;
    galaxy_reset();
    gal_destroy(&s);
}

TEST(galaxy, vm_bad_native_argument_is_protected) {
    gal_state_t s = gal_new();
    T_ASSERT(gal_parse(&s, "native void TestFail(string msg); void main() { TestFail(42); } void good() {}"));
    jass_callbyname(s.j, "main", true);
    jass_runevents(s.j);
    T_ASSERT(jass_rterror_pending(s.j));
    T_STREQ(jass_rterror_message(s.j), "invalid native argument: expected jasstype_string");
    jass_rterror_clear(s.j);
    jass_callbyname(s.j, "good", false);
    T_ASSERT(!jass_rterror_pending(s.j));
    gal_destroy(&s);
}

TEST(galaxy, vm_string_word) {
    gal_state_t s = gal_new();
    jass_sethost(&MAKE(jassHost_t,
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
    jass_sethost(&MAKE(jassHost_t,
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
    jass_sethost(&MAKE(jassHost_t,
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
    jass_sethost(&MAKE(jassHost_t,
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
    jass_sethost(&MAKE(jassHost_t,
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
        "void failing() { MissingFunction(); }\n"
        "void main() {\n"
        "    trigger value = TriggerCreate(\"child\");\n"
        "    TriggerExecute(value, true, true);\n"
        "    if (gv_called != 2) { TestFail(\"wait-done trigger resumed parent before child\"); }\n"
        "}"));
    jass_callbyname(s.j, "failing", false);
    T_ASSERT(jass_rterror_pending(s.j));
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
    jass_sethost(&MAKE(jassHost_t,
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

static void *gal_include_read_file(char const *path, unsigned int *out_size) {
    if (!strcmp(path, "sub.galaxy")) {
        *out_size = (unsigned int)strlen(gal_sub_content);
        char *buf = malloc(*out_size + 1);
        memcpy(buf, gal_sub_content, *out_size + 1);
        return buf;
    }
    return NULL;
}

TEST(galaxy, include_loads_sub_file) {
    jass_sethost(&MAKE(jassHost_t,
        .MemAlloc       = gal_alloc,
        .MemFree        = gal_free,
        .ReadFile       = gal_include_read_file,
        .galaxy_natives = gal_test_natives,
    ));
    jass_t *j = jass_newstate();

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
    jass_sethost(&MAKE(jassHost_t,
        .MemAlloc       = gal_alloc,
        .MemFree        = gal_free,
        .ReadFile       = gal_include_read_file,
        .galaxy_natives = gal_test_natives,
    ));
    jass_t *j = jass_newstate();

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
    char const *path = "/tmp/openwarcraft3_test_autodetect.galaxy";
    FILE *f = fopen(path, "wb");
    if (!f) return;  /* skip if /tmp not writable */
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    jass_sethost(&MAKE(jassHost_t,
        .MemAlloc       = gal_alloc,
        .MemFree        = gal_free,
        .ReadFile       = gal_read_file,
        .galaxy_natives = gal_test_natives,
    ));
    jass_t *j = jass_newstate();
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

static int gal_file_exists(char const *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int gal_load_errors = 0;

static void gal_load(jass_t *j, char const *path) {
    jass_rterror_clear(j);
    bool ok = jass_dofile_ex(j, path, JASS_MODE_GALAXY);
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
    /* Load MapScript.galaxy only: it resolves its includes (NativeLib, LibertyLib, CampaignLib)
     * via the include-once guard, exactly like galaxy_open().  Loading TriggerLibs separately
     * would cause MapScript's include directives to re-parse them a second time. */
    gal_load(s.j, "data/TRaynor01-galaxy/MapScript.galaxy");
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

TEST(galaxy, create_and_set_facing_share_radian_host_contract) {
    gal_state_t s = gal_new();
    galaxy_reset();
    gal_created_angle = gal_facing_angle = 0;
    sc2_galaxy_on_unit_create = gal_facing_create;
    sc2_galaxy_unit_set_position = gal_set_facing;
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free, .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
    T_ASSERT(gal_run(&s,
        "void main() { UnitCreate(1, \"Marine\", 0, 1, Point(3.0, 5.0), 90.0);"
        "UnitSetFacing(UnitLastCreated(), 90.0, 0.0); }"));
    T_FEQ(gal_created_angle, (float)M_PI / 2, 0.0001f);
    T_FEQ(gal_facing_angle, gal_created_angle, 0.0001f);
    sc2_galaxy_on_unit_create = NULL; sc2_galaxy_unit_set_position = NULL;
    galaxy_reset(); gal_destroy(&s);
}


typedef struct { sc2UnitState_t state; float x, y, facing; int owner; } gal_ent_t;
static gal_ent_t gal_ents[8];
static int gal_ent_n;
static bool gal_moving;
static void gal_ent_reset(void) { memset(gal_ents, 0, sizeof(gal_ents)); gal_ent_n = 0; gal_moving = false; }
static void *gal_ent_create(cstring_t type, int player, float x, float y, float angle) {
    gal_ent_t *e;
    (void)type;
    if (gal_ent_n >= 8) return NULL;
    e = &gal_ents[gal_ent_n++];
    memset(e, 0, sizeof(*e));
    e->state.vitals[0].value = e->state.vitals[0].max_value = 100;
    e->x = x; e->y = y; e->facing = angle; e->owner = player;
    return e;
}
static sc2UnitState_t *gal_ent_state(void *ent) { return &((gal_ent_t *)ent)->state; }
static bool gal_ent_loc(void *ent, float *x, float *y, float *z, float *facing) {
    gal_ent_t *e = ent; *x = e->x; *y = e->y; *z = 0; *facing = e->facing; return true;
}
static void gal_ent_setpos(void *ent, float x, float y, float facing) {
    gal_ent_t *e = ent;
    if (isfinite(x)) { e->x = x; e->y = y; }
    if (isfinite(facing)) e->facing = facing;
}
static int gal_ent_owner(void *ent) { return ((gal_ent_t *)ent)->owner; }
static bool gal_ent_alive(void *ent) { return ((gal_ent_t *)ent)->state.vitals[0].value > 0; }
static void gal_ent_changed(void *ent) { (void)ent; }
static void gal_ent_remove(void *ent) { (void)ent; }
static void gal_ent_ordermove(void *ent, float x, float y) { (void)ent; (void)x; (void)y; gal_moving = true; }
static bool gal_ent_moving(void *ent) { (void)ent; return gal_moving; }
static void gal_ent_bind(void) {
    sc2_galaxy_on_unit_create = gal_ent_create; sc2_galaxy_unit_state = gal_ent_state;
    sc2_galaxy_unit_location = gal_ent_loc; sc2_galaxy_unit_set_position = gal_ent_setpos;
    sc2_galaxy_unit_owner = gal_ent_owner; sc2_galaxy_unit_is_alive = gal_ent_alive;
    sc2_galaxy_unit_changed = gal_ent_changed; sc2_galaxy_unit_remove = gal_ent_remove;
    sc2_galaxy_unit_move = gal_ent_ordermove; sc2_galaxy_unit_is_moving = gal_ent_moving;
}
static void gal_ent_unbind(void) {
    sc2_galaxy_on_unit_create = NULL; sc2_galaxy_unit_state = NULL; sc2_galaxy_unit_location = NULL;
    sc2_galaxy_unit_set_position = NULL; sc2_galaxy_unit_owner = NULL; sc2_galaxy_unit_is_alive = NULL;
    sc2_galaxy_unit_changed = NULL; sc2_galaxy_unit_remove = NULL;
    sc2_galaxy_unit_move = NULL; sc2_galaxy_unit_is_moving = NULL;
}
static void gal_use_natives(void) {
    jass_sethost(&MAKE(jassHost_t, .MemAlloc = gal_alloc, .MemFree = gal_free, .ReadFile = gal_read_file,
        .natives = gal_assert_natives, .galaxy_natives = galaxy_get_natives()));
}

/* A null region used to raise a script error and stop InitTriggers before the intro orders. */
TEST(galaxy, vm_event_null_region_does_not_abort) {
    gal_state_t s = gal_new();
    galaxy_reset(); gal_ent_reset(); gal_ent_bind(); gal_use_natives();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "native trigger TriggerCreate(string name);\n"
        "native void TriggerAddEventUnitRegion(trigger t, unitref u, region r, bool state);\n"
        "native void TriggerAddEventUnitRangePoint(trigger t, unitref u, point p, fixed distance, bool state);\n"
        "native point RegionGetCenter(region r);\n"
        "native void TimerPause(timer t, bool pause);\n"
        "native void TriggerAddEventUnitDied(trigger t, unitref u);\n"
        "native unit UnitCreate(int count, string type, int flags, int player, point where, fixed angle);\n"
        "native point Point(fixed x, fixed y);\n"
        "native void UnitKill(unit u);\n"
        "native unit EventUnit();\n"
        "int gv_dead = 0;\n"
        "bool on_die(bool testConds, bool runActions) {\n"
        "    if (testConds) { return false; }\n"
        "    gv_dead = gv_dead + 1; return true; }\n"
        "bool on_die_real(bool testConds, bool runActions) { gv_dead = gv_dead + 1; return true; }\n"
        "void main() {\n"
        "    TriggerAddEventUnitRegion(TriggerCreate(\"on_die\"), null, null, true);\n"
        "    TriggerAddEventUnitRangePoint(TriggerCreate(\"on_die\"), null, null, 1.0, true);\n"
        "    TriggerAddEventUnitRangePoint(TriggerCreate(\"on_die\"), null, RegionGetCenter(null), 1.0, true);\n"
        "    TimerPause(null, true);\n"
        "    unit u = UnitCreate(1, \"Marine\", 0, 1, Point(0.0, 0.0), 0.0);\n"
        "    TriggerAddEventUnitDied(TriggerCreate(\"on_die_real\"), null);\n"
        "    UnitKill(u);\n"
        "    if (gv_dead != 1 || EventUnit() != null) { TestFail(\"later death callback\"); }\n"
        "}"));
    gal_ent_unbind(); galaxy_reset(); gal_destroy(&s);
}

/* gt_UnitMovementCheck registers a null abilcmd. That null is a handle, and checkinteger used to abort InitTriggers. */
TEST(galaxy, vm_event_null_abilcmd_matches_any_order) {
    gal_state_t s = gal_new();
    galaxy_reset(); gal_ent_reset(); gal_ent_bind(); gal_use_natives();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "native trigger TriggerCreate(string name);\n"
        "native unit UnitCreate(int count, string type, int flags, int player, point where, fixed angle);\n"
        "native point Point(fixed x, fixed y);\n"
        "native abilcmd AbilityCommand(string name, int index);\n"
        "native order Order(abilcmd command);\n"
        "native order OrderTargetingPoint(abilcmd command, point target);\n"
        "native bool UnitIssueOrder(unit value, order valueOrder, int queue);\n"
        "native int ConversationDataStateGetValue(string state);\n"
        "native void PlayerModifyPropertyInt(int player, int prop, int oper, int val);\n"
        "native void TriggerAddEventUnitOrder(trigger t, unitref u, abilcmd a);\n"
        "native void TriggerAddEventUnitAbility(trigger t, unitref u, abilcmd a, int stage, bool includeShared);\n"
        "int gv_any = 0; int gv_stop = 0;\n"
        "bool on_any(bool testConds, bool runActions) { gv_any = gv_any + 1; return true; }\n"
        "bool on_stop(bool testConds, bool runActions) { gv_stop = gv_stop + 1; return true; }\n"
        "void main() {\n"
        "    unit mover = UnitCreate(1, \"Marine\", 0, 1, Point(0.0, 0.0), 0.0);\n"
        "    if (Order(null) == null) { TestFail(\"null order\"); }\n"
        "    PlayerModifyPropertyInt(1, 7, 0, ConversationDataStateGetValue(\"Credits\"));\n"
        "    TriggerAddEventUnitOrder(TriggerCreate(\"on_any\"), null, null);\n"
        "    TriggerAddEventUnitOrder(TriggerCreate(\"on_stop\"), null, AbilityCommand(\"stop\", 0));\n"
        "    TriggerAddEventUnitAbility(TriggerCreate(\"on_stop\"), null, null, -1, false);\n"
        "    UnitIssueOrder(mover, OrderTargetingPoint(AbilityCommand(\"move\", 0), Point(1.0, 0.0)), 0);\n"
        "    if (gv_any != 1 || gv_stop != 0) { TestFail(\"null abilcmd filter\"); }\n"
        "}"));
    gal_ent_unbind(); galaxy_reset(); gal_destroy(&s);
}

/* Targetable and tooltipable are writable natives.galaxy states. Rejecting them aborted map init. */
TEST(galaxy, vm_unit_state_writable_flags) {
    gal_state_t s = gal_new();
    galaxy_reset(); gal_ent_reset(); gal_ent_bind(); gal_use_natives();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "native unit UnitCreate(int count, string type, int flags, int player, point where, fixed angle);\n"
        "native point Point(fixed x, fixed y);\n"
        "native void UnitSetState(unit u, int state, bool value);\n"
        "native bool UnitTestState(unit u, int state);\n"
        "void main() {\n"
        "    unit u = UnitCreate(1, \"Marine\", 0, 1, Point(0.0, 0.0), 0.0);\n"
        "    UnitSetState(u, 18, false);\n"
        "    UnitSetState(u, 20, true);\n"
        "    if (UnitTestState(u, 18) || !UnitTestState(u, 20)) { TestFail(\"writable state\"); }\n"
        "}"));
    gal_ent_unbind(); galaxy_reset(); gal_destroy(&s);
}

/* Death, property, create, and cargo callbacks fill the response the way GetTriggerUnit reads jass context. */
TEST(galaxy, vm_event_unit_response) {
    gal_state_t s = gal_new();
    galaxy_reset(); gal_ent_reset(); gal_ent_bind(); gal_use_natives();
    T_ASSERT(gal_run(&s,
        "native void TestFail(string msg);\n"
        "native trigger TriggerCreate(string name);\n"
        "native void TriggerAddEventUnitCreated(trigger t, unitref u, string abil, string behavior);\n"
        "native void TriggerAddEventUnitDied(trigger t, unitref u);\n"
        "native void TriggerAddEventUnitRemoved(trigger t, unitref u);\n"
        "native void TriggerAddEventUnitRevive(trigger t, unitref u);\n"
        "native void TriggerAddEventUnitProperty(trigger t, unitref u, int prop);\n"
        "native void TriggerAddEventUnitCargo(trigger t, unitref u, bool state);\n"
        "native void TriggerEnable(trigger t, bool enable);\n"
        "native trigger TriggerGetCurrent();\n"
        "native int TriggerGetExecCount(trigger t);\n"
        "native unit EventUnit();\n"
        "native unit EventUnitCreatedUnit();\n"
        "native unit EventUnitCargo();\n"
        "native unit UnitCreate(int count, string type, int flags, int player, point where, fixed angle);\n"
        "native unit UnitLastCreated();\n"
        "native point Point(fixed x, fixed y);\n"
        "native unitref UnitRefFromUnit(unit u);\n"
        "native void UnitKill(unit u);\n"
        "native void UnitRevive(unit u);\n"
        "native void UnitRemove(unit u);\n"
        "native void UnitSetPropertyFixed(unit u, int prop, fixed value);\n"
        "native unit UnitCargoCreate(unit transport, string type, int count);\n"
        "native unit UnitCargoLastCreated();\n"
        "int gv_created = 0; int gv_decoy = 0; int gv_any = 0; int gv_specific = 0; int gv_bad = 0;\n"
        "int gv_prop = 0; int gv_removed = 0; int gv_revive = 0; int gv_cargo = 0; int gv_unload = 0;\n"
        "trigger gv_ta = null; trigger gv_tany = null;\n"
        "unit gv_a = null; unit gv_b = null; unit gv_c = null; unit gv_ship = null;\n"
        "bool on_create(bool testConds, bool runActions) {\n"
        "    gv_created = gv_created + 1;\n"
        "    if (EventUnit() == null || EventUnitCreatedUnit() != EventUnit()) { gv_bad = 1; }\n"
        "    return true; }\n"
        "bool on_decoy(bool testConds, bool runActions) { gv_decoy = gv_decoy + 1; return true; }\n"
        "bool on_any(bool testConds, bool runActions) {\n"
        "    gv_any = gv_any + 1; if (TriggerGetCurrent() != gv_tany) { gv_bad = 2; } return true; }\n"
        "bool on_a(bool testConds, bool runActions) {\n"
        "    gv_specific = gv_specific + 1;\n"
        "    if (TriggerGetCurrent() != gv_ta || EventUnit() != gv_a) { gv_bad = 3; }\n"
        "    UnitKill(gv_b);\n"
        "    if (EventUnit() != gv_a || TriggerGetCurrent() != gv_ta) { gv_bad = 4; }\n"
        "    return true; }\n"
        "bool on_prop(bool testConds, bool runActions) {\n"
        "    gv_prop = gv_prop + 1; if (EventUnit() != gv_a) { gv_bad = 5; } return true; }\n"
        "bool on_remove(bool testConds, bool runActions) {\n"
        "    gv_removed = gv_removed + 1; if (EventUnit() != gv_c) { gv_bad = 6; } return true; }\n"
        "bool on_revive(bool testConds, bool runActions) {\n"
        "    gv_revive = gv_revive + 1; if (EventUnit() != gv_a) { gv_bad = 7; } return true; }\n"
        "bool on_load(bool testConds, bool runActions) {\n"
        "    gv_cargo = gv_cargo + 1;\n"
        "    if (EventUnit() != gv_ship || EventUnitCargo() != UnitCargoLastCreated()) { gv_bad = 8; }\n"
        "    return true; }\n"
        "bool on_unload(bool testConds, bool runActions) { gv_unload = gv_unload + 1; return true; }\n"
        "void main() {\n"
        "    TriggerAddEventUnitCreated(TriggerCreate(\"on_create\"), null, \"\", \"\");\n"
        "    TriggerAddEventUnitCreated(TriggerCreate(\"on_decoy\"), null, \"Nope\", \"\");\n"
        "    gv_ship = UnitCreate(1, \"Dropship\", 0, 1, Point(0.0, 0.0), 0.0);\n"
        "    gv_a = UnitCreate(1, \"Marine\", 0, 1, Point(1.0, 0.0), 0.0);\n"
        "    gv_b = UnitCreate(1, \"Marine\", 0, 1, Point(2.0, 0.0), 0.0);\n"
        "    gv_c = UnitCreate(1, \"Marine\", 0, 1, Point(3.0, 0.0), 0.0);\n"
        "    if (gv_created != 4 || gv_decoy != 0) { TestFail(\"create filter\"); }\n"
        "    gv_ta = TriggerCreate(\"on_a\"); gv_tany = TriggerCreate(\"on_any\");\n"
        "    TriggerAddEventUnitDied(gv_ta, UnitRefFromUnit(gv_a));\n"
        "    TriggerAddEventUnitDied(gv_tany, null);\n"
        "    TriggerAddEventUnitProperty(TriggerCreate(\"on_prop\"), UnitRefFromUnit(gv_a), 0);\n"
        "    TriggerAddEventUnitRemoved(TriggerCreate(\"on_remove\"), UnitRefFromUnit(gv_c));\n"
        "    TriggerAddEventUnitRevive(TriggerCreate(\"on_revive\"), UnitRefFromUnit(gv_a));\n"
        "    TriggerAddEventUnitCargo(TriggerCreate(\"on_load\"), UnitRefFromUnit(gv_ship), true);\n"
        "    TriggerAddEventUnitCargo(TriggerCreate(\"on_unload\"), UnitRefFromUnit(gv_ship), false);\n"
        "    UnitSetPropertyFixed(gv_a, 0, 40.0);\n"
        "    UnitSetPropertyFixed(gv_a, 0, 40.0);\n"
        "    UnitSetPropertyFixed(gv_a, 14, 3.0);\n"
        "    if (gv_prop != 1) { TestFail(\"property filter\"); }\n"
        "    UnitKill(gv_a);\n"
        "    if (gv_specific != 1 || gv_any != 2 || gv_prop != 2 || gv_bad != 0) { TestFail(\"death response\"); }\n"
        "    if (TriggerGetExecCount(gv_ta) != 1 || TriggerGetExecCount(gv_tany) != 2) { TestFail(\"exec count\"); }\n"
        "    TriggerEnable(gv_tany, false);\n"
        "    UnitKill(gv_c);\n"
        "    if (gv_any != 2) { TestFail(\"disabled trigger fired\"); }\n"
        "    TriggerEnable(gv_tany, true);\n"
        "    UnitRevive(gv_a);\n"
        "    if (gv_revive != 1 || gv_prop != 3) { TestFail(\"revive\"); }\n"
        "    UnitRemove(gv_c);\n"
        "    if (gv_removed != 1) { TestFail(\"remove\"); }\n"
        "    UnitCargoCreate(gv_ship, \"Marine\", 1);\n"
        "    if (gv_created != 5 || gv_cargo != 1 || gv_unload != 0 || gv_bad != 0) { TestFail(\"cargo\"); }\n"
        "}"));
    gal_ent_unbind(); galaxy_reset(); gal_destroy(&s);
}

/* Position crossings and issued orders publish the same response fields WC3 stores on the event. */
TEST(galaxy, vm_event_spatial_and_order) {
    gal_state_t s = gal_new();
    galaxy_reset(); gal_ent_reset(); gal_ent_bind(); gal_use_natives();
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string msg);\n"
        "native trigger TriggerCreate(string name);\n"
        "native unit UnitCreate(int count, string type, int flags, int player, point where, fixed angle);\n"
        "native point Point(fixed x, fixed y);\n"
        "native unitref UnitRefFromUnit(unit u);\n"
        "native region RegionRect(fixed minx, fixed miny, fixed maxx, fixed maxy);\n"
        "native void TriggerAddEventUnitRegion(trigger t, unitref u, region r, bool state);\n"
        "native void TriggerAddEventUnitRange(trigger t, unitref u, unit fromUnit, fixed range, bool state);\n"
        "native void TriggerAddEventUnitRangePoint(trigger t, unitref u, point p, fixed distance, bool state);\n"
        "native void UnitSetPosition(unit u, point p);\n"
        "native region EventUnitRegion();\n"
        "native unit EventUnit();\n"
        "native unit EventUnitTarget();\n"
        "native order EventUnitOrder();\n"
        "native void TriggerAddEventUnitOrder(trigger t, unitref u, abilcmd a);\n"
        "native void TriggerAddEventUnitBecomesIdle(trigger t, unitref u, bool idle);\n"
        "native abilcmd AbilityCommand(string name, int index);\n"
        "native order OrderTargetingPoint(abilcmd command, point target);\n"
        "native bool UnitIssueOrder(unit value, order valueOrder, int queue);\n"
        "int gv_enter = 0; int gv_leave = 0; int gv_near = 0; int gv_far = 0; int gv_point = 0;\n"
        "int gv_bad = 0; int gv_orders = 0; int gv_stop = 0; int gv_idle = 0; int gv_busy = 0;\n"
        "region gv_hit = null; unit gv_tgt = null;\n"
        "bool on_enter(bool testConds, bool runActions) {\n"
        "    gv_enter = gv_enter + 1; gv_hit = EventUnitRegion(); if (EventUnit() == null) { gv_bad = 1; } return true; }\n"
        "bool on_leave(bool testConds, bool runActions) { gv_leave = gv_leave + 1; return true; }\n"
        "bool on_near(bool testConds, bool runActions) { gv_near = gv_near + 1; gv_tgt = EventUnitTarget(); return true; }\n"
        "bool on_far(bool testConds, bool runActions) { gv_far = gv_far + 1; return true; }\n"
        "bool on_point(bool testConds, bool runActions) { gv_point = gv_point + 1; return true; }\n"
        "bool on_order(bool testConds, bool runActions) {\n"
        "    gv_orders = gv_orders + 1; if (EventUnitOrder() == null || EventUnit() == null) { gv_bad = 2; } return true; }\n"
        "bool on_stop(bool testConds, bool runActions) { gv_stop = gv_stop + 1; return true; }\n"
        "bool on_idle(bool testConds, bool runActions) { gv_idle = gv_idle + 1; return true; }\n"
        "bool on_busy(bool testConds, bool runActions) { gv_busy = gv_busy + 1; return true; }\n"
        "void main() {\n"
        "    unit mover = UnitCreate(1, \"Marine\", 0, 1, Point(30.0, 30.0), 0.0);\n"
        "    unit anchor = UnitCreate(1, \"Marine\", 0, 1, Point(0.0, 0.0), 0.0);\n"
        "    region area = RegionRect(0.0, 0.0, 10.0, 10.0);\n"
        "    point origin = Point(0.0, 0.0);\n"
        "    TriggerAddEventUnitRegion(TriggerCreate(\"on_enter\"), UnitRefFromUnit(mover), area, true);\n"
        "    TriggerAddEventUnitRegion(TriggerCreate(\"on_leave\"), UnitRefFromUnit(mover), area, false);\n"
        "    TriggerAddEventUnitRange(TriggerCreate(\"on_near\"), UnitRefFromUnit(mover), anchor, 5.0, true);\n"
        "    TriggerAddEventUnitRange(TriggerCreate(\"on_far\"), UnitRefFromUnit(mover), anchor, 5.0, false);\n"
        "    TriggerAddEventUnitRangePoint(TriggerCreate(\"on_point\"), UnitRefFromUnit(mover), origin, 5.0, true);\n"
        "    TriggerAddEventUnitOrder(TriggerCreate(\"on_order\"), UnitRefFromUnit(mover), 0);\n"
        "    TriggerAddEventUnitOrder(TriggerCreate(\"on_stop\"), UnitRefFromUnit(mover), AbilityCommand(\"stop\", 0));\n"
        "    TriggerAddEventUnitBecomesIdle(TriggerCreate(\"on_idle\"), UnitRefFromUnit(mover), true);\n"
        "    TriggerAddEventUnitBecomesIdle(TriggerCreate(\"on_busy\"), UnitRefFromUnit(mover), false);\n"
        "    UnitSetPosition(mover, Point(5.0, 5.0));\n"
        "    if (gv_enter != 1 || gv_hit != area || gv_leave != 0) { TestFail(\"enter\"); }\n"
        "    UnitSetPosition(mover, Point(6.0, 5.0));\n"
        "    if (gv_enter != 1) { TestFail(\"stay inside\"); }\n"
        "    UnitSetPosition(mover, Point(4.0, 0.0));\n"
        "    if (gv_near != 1 || gv_point != 1 || gv_tgt != anchor || gv_far != 0) { TestFail(\"range enter\"); }\n"
        "    UnitSetPosition(mover, Point(40.0, 40.0));\n"
        "    if (gv_leave != 1 || gv_far != 1 || gv_near != 1 || gv_point != 1) { TestFail(\"range leave\"); }\n"
        "    UnitIssueOrder(mover, OrderTargetingPoint(AbilityCommand(\"move\", 0), Point(1.0, 0.0)), 0);\n"
        "    if (gv_orders != 1 || gv_busy != 1 || gv_idle != 0 || gv_stop != 0 || gv_bad != 0) { TestFail(\"order\"); }\n"
        "}\n"
        "void check() { if (gv_idle != 1 || gv_busy != 1 || gv_bad != 0) { TestFail(\"idle\"); } }\n"));
    jass_callbyname(s.j, "main", false);
    jass_runevents(s.j);
    if (jass_rterror_pending(s.j)) fprintf(stderr, "spatial: %s\n", jass_rterror_message(s.j));
    T_ASSERT(!jass_rterror_pending(s.j));
    galaxy_tick(s.j);
    gal_moving = false;
    galaxy_tick(s.j);
    jass_callbyname(s.j, "check", false);
    if (jass_rterror_pending(s.j)) fprintf(stderr, "idle: %s\n", jass_rterror_message(s.j));
    T_ASSERT(!jass_rterror_pending(s.j));
    gal_ent_unbind(); galaxy_reset(); gal_destroy(&s);
}

/* Time, timer, and player callbacks advance through galaxy_tick, the same scheduler entry as the server frame. */
TEST(galaxy, vm_event_time_and_player) {
    gal_state_t s = gal_new();
    int i;
    galaxy_reset(); gal_use_natives();
    T_ASSERT(gal_parse(&s,
        "native void TestFail(string msg);\n"
        "native trigger TriggerCreate(string name);\n"
        "native void TriggerAddEventTimePeriodic(trigger t, fixed interval, int timeType);\n"
        "native void TriggerAddEventTimeElapsed(trigger t, fixed time, int timeType);\n"
        "native timer TimerCreate();\n"
        "native void TimerStart(timer t, fixed duration, bool periodic, int timeType);\n"
        "native timer TimerLastStarted();\n"
        "native void TriggerAddEventTimer(trigger t, timer which);\n"
        "native timer EventTimer();\n"
        "native void AITimePause(bool pause);\n"
        "native void PlayerModifyPropertyInt(int player, int prop, int op, int value);\n"
        "native void PlayerSetAlliance(int source, int alliance, int other, bool ally);\n"
        "native void TriggerAddEventPlayerPropChange(trigger t, int player, int prop);\n"
        "native void TriggerAddEventPlayerAllianceChange(trigger t, int player);\n"
        "native int EventPlayer();\n"
        "native int EventPlayerProperty();\n"
        "native void TriggerAddEventDialogControl(trigger t, int player, int control, int eventType);\n"
        "native void TriggerAddEventChatMessage(trigger t, int player, string text, bool exact);\n"
        "native void TriggerAddEventKeyPressed(trigger t, int player, int key, bool down, int s, int c, int a);\n"
        "native void TriggerAddEventUnitDamaged(trigger t, unitref u, int damageType, int fatal, string effect);\n"
        "int gv_per = 0; int gv_once = 0; int gv_ai = 0; int gv_tm = 0; int gv_tmp = 0;\n"
        "int gv_prop = 0; int gv_any = 0; int gv_other = 0; int gv_ally = 0; int gv_bad = 0;\n"
        "timer gv_timer = null;\n"
        "bool on_per(bool testConds, bool runActions) { gv_per = gv_per + 1; return true; }\n"
        "bool on_once(bool testConds, bool runActions) { gv_once = gv_once + 1; return true; }\n"
        "bool on_ai(bool testConds, bool runActions) { gv_ai = gv_ai + 1; return true; }\n"
        "bool on_tm(bool testConds, bool runActions) {\n"
        "    gv_tm = gv_tm + 1; if (EventTimer() != gv_timer) { gv_bad = 1; } return true; }\n"
        "bool on_tmp(bool testConds, bool runActions) { gv_tmp = gv_tmp + 1; return true; }\n"
        "bool on_prop(bool testConds, bool runActions) {\n"
        "    gv_prop = gv_prop + 1;\n"
        "    if (EventPlayer() != 2 || EventPlayerProperty() != 1) { gv_bad = 2; }\n"
        "    return true; }\n"
        "bool on_any(bool testConds, bool runActions) { gv_any = gv_any + 1; return true; }\n"
        "bool on_other(bool testConds, bool runActions) { gv_other = gv_other + 1; return true; }\n"
        "bool on_ally(bool testConds, bool runActions) {\n"
        "    gv_ally = gv_ally + 1; if (EventPlayer() != 1) { gv_bad = 3; } return true; }\n"
        "bool on_dlg(bool testConds, bool runActions) { gv_bad = 4; return true; }\n"
        "bool on_chat(bool testConds, bool runActions) { gv_bad = 5; return true; }\n"
        "bool on_key(bool testConds, bool runActions) { gv_bad = 6; return true; }\n"
        "bool on_dmg(bool testConds, bool runActions) { gv_bad = 7; return true; }\n"
        "void main() {\n"
        "    timer periodic;\n"
        "    TriggerAddEventTimePeriodic(TriggerCreate(\"on_per\"), 0.2, 0);\n"
        "    TriggerAddEventTimeElapsed(TriggerCreate(\"on_once\"), 0.3, 0);\n"
        "    TriggerAddEventTimePeriodic(TriggerCreate(\"on_ai\"), 0.2, 2);\n"
        "    gv_timer = TimerCreate();\n"
        "    TimerStart(gv_timer, 0.2, false, 0);\n"
        "    if (TimerLastStarted() != gv_timer) { TestFail(\"last timer\"); }\n"
        "    TriggerAddEventTimer(TriggerCreate(\"on_tm\"), gv_timer);\n"
        "    periodic = TimerCreate();\n"
        "    TimerStart(periodic, 0.2, true, 0);\n"
        "    TriggerAddEventTimer(TriggerCreate(\"on_tmp\"), periodic);\n"
        "    TriggerAddEventPlayerPropChange(TriggerCreate(\"on_prop\"), 2, 1);\n"
        "    TriggerAddEventPlayerPropChange(TriggerCreate(\"on_any\"), -1, 1);\n"
        "    TriggerAddEventPlayerPropChange(TriggerCreate(\"on_other\"), 2, 4);\n"
        "    TriggerAddEventPlayerAllianceChange(TriggerCreate(\"on_ally\"), 1);\n"
        "    TriggerAddEventDialogControl(TriggerCreate(\"on_dlg\"), -1, -1, -1);\n"
        "    TriggerAddEventChatMessage(TriggerCreate(\"on_chat\"), -1, \"gg\", false);\n"
        "    TriggerAddEventKeyPressed(TriggerCreate(\"on_key\"), -1, 65, true, 0, 0, 0);\n"
        "    TriggerAddEventUnitDamaged(TriggerCreate(\"on_dmg\"), null, -1, 0, \"\");\n"
        "    PlayerModifyPropertyInt(2, 1, 0, 9);\n"
        "    PlayerModifyPropertyInt(2, 1, 0, 9);\n"
        "    PlayerModifyPropertyInt(3, 1, 0, 4);\n"
        "    PlayerSetAlliance(1, 0, 2, true);\n"
        "    PlayerSetAlliance(1, 0, 2, true);\n"
        "    if (gv_prop != 1 || gv_any != 2 || gv_other != 0 || gv_ally != 1 || gv_bad != 0) { TestFail(\"player\"); }\n"
        "    AITimePause(true);\n"
        "}\n"
        "void check() {\n"
        "    if (gv_bad != 0) { TestFail(\"filtered callback fired\"); }\n"
        "    if (gv_per != 2 || gv_once != 1 || gv_ai != 0 || gv_tm != 1 || gv_tmp != 2) { TestFail(\"clock\"); }\n"
        "}\n"
        "void resume() { AITimePause(false); }\n"
        "void check2() {\n"
        "    if (gv_per != 3 || gv_once != 1 || gv_ai != 1 || gv_tm != 1 || gv_tmp != 3) { TestFail(\"resume\"); }\n"
        "}\n"));
    jass_callbyname(s.j, "main", false);
    if (jass_rterror_pending(s.j)) fprintf(stderr, "player events: %s\n", jass_rterror_message(s.j));
    T_ASSERT(!jass_rterror_pending(s.j));
    for (i = 0; i < 4; i++) galaxy_tick(s.j);
    jass_callbyname(s.j, "check", false);
    if (jass_rterror_pending(s.j)) fprintf(stderr, "clock: %s\n", jass_rterror_message(s.j));
    T_ASSERT(!jass_rterror_pending(s.j));
    jass_callbyname(s.j, "resume", false);
    galaxy_tick(s.j); galaxy_tick(s.j);
    jass_callbyname(s.j, "check2", false);
    if (jass_rterror_pending(s.j)) fprintf(stderr, "resume: %s\n", jass_rterror_message(s.j));
    T_ASSERT(!jass_rterror_pending(s.j));
    galaxy_reset(); gal_destroy(&s);
}

#include "test_galaxy_foundations.h"

#endif /* BZ_TESTS */
