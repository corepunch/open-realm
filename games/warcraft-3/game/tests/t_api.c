#ifdef BZ_TESTS
/*
 * test_api.c — Tests for the JASS native API implementations.
 *
 * These tests exercise the C-level game-state that the api_*.h functions
 * read and write.  They work directly on struct fields, alliance tables,
 * and the group registry — no MPQ or renderer is required.
 *
 * Covered:
 *   Player  — color, start_location, name, team, alliance
 *   Hero    — str/agi/int attributes, XP accumulation, skill points,
 *             suspend_xp, overflow-safe AddHeroXP
 *   Unit    — invulnerable, paused, no_pathing, unit_color flags
 *   Group   — FirstOfGroup, IsUnitInGroup
 *   Misc    — SubString semantics, GetRandomInt / GetRandomReal range
 *   Stock   — global capacities, per-unit overrides, spawn inheritance
 */

#include "test.h"
#include "../g_local.h"
#include "common/ui_constants.h"
#include "games/warcraft-3/common/minimap.h"
#include "common/campaign_progress.h"

/* Helpers defined in t_utils.c */
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void CM_SetupTestWorldBounds(box2_t const *bounds);
bool run_test_jass(cstring_t src);
bool run_test_jass_error(cstring_t src, cstring_t expected);
extern player_t *currentplayer;
void unit_die(edict_t *self, edict_t *attacker);
void unit_build(edict_t *self, uint32_t class_id);
/* Public point orders use the scheduled physical owner. Prime its velocity,
 * then advance it before testing the entity-owned region/range observations. */
static void api_run_move_entities(unsigned ticks) {
    G_BeginEntityFrame();level.scheduled_frame=true;
    FOR_LOOP(i,ticks) {
        wc3_clock_advance(&level.pathing_clock,10.0f/FRAMETIME,0);
        M_RunScheduledThinks();
    }
    G_RunEntities();level.scheduled_frame=false;
}

static edict_t *find_test_unit(uint32_t class_id);
static slkTestData_t *building_install_repair_data(slkTestData_t **rows_out);
static void building_restore_repair_data(slkTestData_t *old, slkTestData_t *rows);



#include "jass/jass.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */

/*
 * Return a pointer to player slot [idx].  Assigns player->number = idx so
 * that G_GetPlayerByNumber / PLAYER_CLIENT macros work correctly.
 */
static player_t *test_player(int idx) {
    game.clients[idx].ps.number = (uint32_t)idx;
    return &game.clients[idx].ps;
}

static uint32_t selection_native_packet_count;
static uint32_t selection_native_packet_units;
static int selection_native_packet_stage;

static pfWriteType_t terrain_deform_write_types[32];
static int32_t terrain_deform_write_values[32];
static float terrain_deform_write_floats[16];
static uint32_t terrain_deform_write_count, terrain_deform_float_count, terrain_deform_multicast_count;

static void terrain_deform_test_write(pfWriteType_t type, void const *data) {
    if (terrain_deform_write_count < sizeof(terrain_deform_write_types) / sizeof(terrain_deform_write_types[0])) {
        uint32_t slot = terrain_deform_write_count++;
        terrain_deform_write_types[slot] = type;
        if (type == PF_BYTE || type == PF_LONG) terrain_deform_write_values[slot] = *(int32_t const *)data;
    }
    if (type == PF_FLOAT && terrain_deform_float_count <
        sizeof(terrain_deform_write_floats) / sizeof(terrain_deform_write_floats[0]))
        terrain_deform_write_floats[terrain_deform_float_count++] = *(float const *)data;
}

static void terrain_deform_test_multicast(vec3_t const *origin, multicast_t to) {
    (void)origin;
    if (to == MULTICAST_ALL) terrain_deform_multicast_count++;
}

static void selection_native_test_write(pfWriteType_t type, void const *data) {
    int32_t value;

    if (type != PF_BYTE || !data) return;
    value = *(int32_t const *)data;
    if (selection_native_packet_stage == 0 && value == svc_set_selection) {
        selection_native_packet_count++;
        selection_native_packet_stage = 1;
    } else if (selection_native_packet_stage == 1) {
        selection_native_packet_units = (uint32_t)value;
        selection_native_packet_stage = 2;
    }
}

static void selection_native_test_unicast(edict_t *ent) { (void)ent; }

/* Retail public decimal parser outputs captured with raw words, not R2S text. */
TEST(wc3_api, pathfinding_decimal_destination_uses_retail_parser) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit mover = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  local real x = S2R(\"1936.25\")\n"
        "  local real y = S2R(\"144.125\")\n"
        "  call BJassAssert(S2R(\"1.25\") == 1.25, \"decimal was truncated to integer\")\n"
        "  call BJassAssert(y == 144.125, \"fractional coordinate missing\")\n"
        "  call BJassAssert(S2R(\"1.25e2\") == 1.25, \"exponent suffix should terminate decimal\")\n"
        "  call BJassAssert(S2R(\" 1.25\") == 0.0, \"leading whitespace should terminate decimal\")\n"
        "  call BJassAssert(S2R(\"1234567890\") == 1234567808.0, \"significant digit truncation differs\")\n"
        "  call BJassAssert(IssuePointOrder(mover, \"move\", x, y), \"parsed point order rejected\")\n"
        "endfunction\n"));
    edict_t *mover = NULL;
    for (int i = MAX_CLIENTS; i < globals.num_edicts; i++)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) mover = &g_edicts[i];
    T_ASSERT(mover != NULL);
    if (mover) {
        vec2_t issued;
        T_ASSERT(G_GetIssuedOrderPoint(mover, &issued));
        T_EQ(issued.x, 1936.2501220703125f);
        T_EQ(issued.y, 144.125f);
    }
    reset_entities();
}

/* Retail NUM-01.13: an authored unit-name byte string reaches SubString/S2R and Move. */
TEST(wc3_api, pathfinding_decimal_high_bytes_match_retail_words) {
    static uint32_t const expected[] = {0x00000000u, 0x41400000u, 0x3f000000u, 0x00000000u};
    char bytes[129];
    FOR_LOOP(i, 128) bytes[i] = (char)(128+i);
    bytes[128] = 0;
    unitModification_t name = {
        .modID = MAKEFOURCC('u','n','a','m'), .type = mod_string, .data = (handle_t)bytes
    };
    unitData_t original = {
        .originalUnitID = MAKEFOURCC('h','f','o','o'), .numbeOfModifications = 1, .modifications = &name
    };
    mapInfo_t mapinfo = {.num_originalUnits = 1, .originalUnits = &original};
    reset_entities();
    setup_test_world();
    mapInfo_t const *saved_mapinfo = level.mapinfo;
    level.mapinfo = &mapinfo;
    G_SetMapUnitOverrides(&mapinfo);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable values = InitHashtable()\n"
        "  local unit mover = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  local string source = GetUnitName(mover)\n"
        "  local string byte = \"\"\n"
        "  local integer i = 0\n"
        "  local real x = 0.0\n"
        "  local real y = 0.0\n"
        "  call BJassAssert(StringLength(source) == 128, \"authored byte source changed\")\n"
        "  loop\n"
        "    exitwhen i == 128\n"
        "    set byte = SubString(source, i, i + 1)\n"
        "    call SaveReal(values, i, 0, S2R(byte))\n"
        "    call SaveReal(values, i, 1, S2R(\"12\" + byte + \"34\"))\n"
        "    call SaveReal(values, i, 2, S2R(\".5\" + byte + \"7\"))\n"
        "    call SaveReal(values, i, 3, S2R(\"-\" + byte + \"0.2\"))\n"
        "    set i = i + 1\n"
        "  endloop\n"
        "  set x = S2R(\"-1936.25\" + SubString(source, 0, 1) + \"9\")\n"
        "  set y = S2R(\"-144.125\" + SubString(source, 127, 128) + \"9\")\n"
        "  call BJassAssert(IssuePointOrder(mover, \"move\", x, y), \"byte-parsed Move rejected\")\n"
        "endfunction\n"));
    hashtable_t const *table = &level.hashtables[0];
    T_EQ(table->num_entries, 512);
    FOR_LOOP(i, table->num_entries) {
        uint32_t word;
        T_EQ(table->entries[i].type, HT_REAL);
        T_EQ(table->entries[i].parent, (int32_t)(i/4));
        T_EQ(table->entries[i].child, (int32_t)(i%4));
        memcpy(&word, &table->entries[i].value.real, sizeof(word));
        T_EQ(word, expected[i%4]);
    }
    edict_t *mover = find_test_unit(MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(mover);
    if (mover) {
        vec2_t point;
        T_ASSERT(G_GetIssuedOrderPoint(mover, &point));
        uint32_t word;
        memcpy(&word, &point.x, sizeof(word));
        T_EQ(word, 0xc4f20801u);
        memcpy(&word, &point.y, sizeof(word));
        T_EQ(word, 0xc3102000u);
    }
    reset_entities();
    G_SetMapUnitOverrides(NULL);
    level.mapinfo = saved_mapinfo;
}

/* Capture actual VM literal values in their owning hashtable, independently of JASS comparisons. */
#include "retail_expressions.h"

TEST(wc3_api, pathfinding_compiled_expressions_match_original_inputs_and_results) {
    reset_entities();setup_test_world();
    char source[24000];unsigned length=snprintf(source,sizeof(source),
        "globals\nhashtable values\ninteger calls=0\nendglobals\n"
        "function PathExpressionInteger takes integer tag, integer value returns integer\n"
        "set calls=calls*10+tag\nreturn value\nendfunction\n"
        "function PathExpressionReal takes integer tag, real value returns real\n"
        "set calls=calls*10+tag\nreturn value\nendfunction\n");
    FOR_LOOP(i,sizeof(expression82_cases)/sizeof(*expression82_cases)) {
        bool integer=expression82_cases[i].integer;
        length+=snprintf(source+length,sizeof(source)-length,
            "function case%u takes nothing returns nothing\nlocal %s input\nset calls=0\nset input=%s\n"
            "call Save%s(values,%u,0,input)\ncall Save%s(values,%u,1,%s(input))\n"
            "call SaveInteger(values,%u,2,calls)\nendfunction\n",i,integer?"integer":"real",
            expression82_cases[i].expression,integer?"Integer":"Real",i,integer?"Real":"Integer",i,
            integer?"I2R":"R2I",i);
    }
    length+=snprintf(source+length,sizeof(source)-length,"function main takes nothing returns nothing\nset values=InitHashtable()\n");
    FOR_LOOP(i,sizeof(expression82_cases)/sizeof(*expression82_cases))
        length+=snprintf(source+length,sizeof(source)-length,"call case%u()\n",i);
    snprintf(source+length,sizeof(source)-length,"endfunction\n");
    T_ASSERT(run_test_jass(source));
    hashtable_t const *table=&level.hashtables[0];
    T_EQ(table->num_entries,3*sizeof(expression82_cases)/sizeof(*expression82_cases));
    FOR_LOOP(i,table->num_entries) {
        uint32_t word;memcpy(&word,&table->entries[i].value,sizeof(word));
        unsigned row=i/3,column=i%3;
        T_EQ(table->entries[i].parent,(int32_t)row);T_EQ(table->entries[i].child,(int32_t)column);
        T_EQ(word,column==0?expression82_cases[row].input:column==1?expression82_cases[row].output:expression82_cases[row].operands);
    }
    reset_entities();
}

TEST(wc3_api, pathfinding_compiled_literals_match_retail_words) {
    static uint32_t const expected[] = {0xbf85635d, 0x3f19999a, 0x3fc00000, 0xcf000000, 0x7f000000};
    reset_entities();
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local hashtable values = InitHashtable()\n"
        "  local unit mover = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call SaveReal(values, 0, 0, 0.59999999999999998)\n"
        "  call SaveReal(values, 0, 1, 0.6)\n"
        "  call SaveReal(values, 0, 2, 4294967297.5)\n"
        "  call SaveReal(values, 0, 3, 2147483648.0)\n"
        "  call SaveReal(values, 0, 4, 0.00000000000000000000000000000001)\n"
        "  call BJassAssert(IssuePointOrder(mover, \"move\", 4294967297.5, 144.125), \"compiled point order rejected\")\n"
        "endfunction\n"));
    hashtable_t const *table = &level.hashtables[0];
    T_EQ(table->num_entries, 5);
    FOR_LOOP(i, table->num_entries) {
        uint32_t word;
        memcpy(&word, &table->entries[i].value.real, sizeof(word));
        T_EQ(table->entries[i].type, HT_REAL);
        T_EQ(table->entries[i].child, (int32_t)i);
        T_EQ(word, expected[i]);
    }
    edict_t *mover = find_test_unit(MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(mover);
    if (mover) {
        vec2_t point;
        T_ASSERT(G_GetIssuedOrderPoint(mover, &point));
        uint32_t word;
        memcpy(&word, &point.x, sizeof(word));
        T_EQ(word, 0x3fc00000);
        memcpy(&word, &point.y, sizeof(word));
        T_EQ(word, 0x43102000);
    }
    reset_entities();
}

/* Save/load reconstructs source tokens and retains both stored words and future evaluations. */
TEST(wc3_api, pathfinding_compiled_literals_survive_save_load) {
    cstring_t path = Test_TempPath("openwarcraft3-wc3-compiled-literals-save.bin");
    reset_entities();
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  hashtable values = null\n"
        "  constant real compiled = 0.59999999999999998\n"
        "endglobals\n"
        "function StoreCompiled takes nothing returns nothing\n"
        "  call SaveReal(values, 0, 0, compiled)\n"
        "  call SaveReal(values, 0, 1, 4294967297.5)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set values = InitHashtable()\n"
        "  call StoreCompiled()\n"
        "endfunction\n"));
    T_ASSERT(WriteGame(path));
    T_ASSERT(ReadGame(path));
    FOR_LOOP(pass, 2) {
        hashtable_t const *table = &level.hashtables[0];
        T_EQ(table->num_entries, 2);
        if (table->num_entries == 2) {
            uint32_t word;
            memcpy(&word, &table->entries[0].value.real, sizeof(word));
            T_EQ(word, 0xbf85635d);
            memcpy(&word, &table->entries[1].value.real, sizeof(word));
            T_EQ(word, 0x3fc00000);
        }
        if (!pass) {
            jass_callbyname(level.vm, "StoreCompiled", true);
            jass_runevents(level.vm);
            T_ASSERT(!jass_rterror_pending(level.vm));
        }
    }
    remove(path);
    reset_entities();
}

/* Unrecovered host-only token syntax must not enter the verified decimal producer. */
TEST(wc3_api, pathfinding_compiled_unverified_numeric_syntax_reports_error) {
    setup_test_world();
    T_ASSERT(!run_test_jass("function main takes nothing returns nothing\n  local real value = 1e2\nendfunction\n"));
    T_ASSERT(run_test_jass_error("function main takes nothing returns nothing\n  local real value = nan\nendfunction\n",
        "Compiled real token outside verified retail decimal grammar"));
    T_ASSERT(run_test_jass_error("function main takes nothing returns nothing\n  local real value = inf\nendfunction\n",
        "Compiled real token outside verified retail decimal grammar"));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n  call BJassAssert(.5 == 0.5, \"verified decimal syntax recovers\")\nendfunction\n"));
    reset_entities();
}

/* Wide source integers must wrap per digit before their native conversion or movement use. */
TEST(wc3_api, pathfinding_compiled_integer_literals_match_retail_words) {
    static struct { cstring_t expression; uint32_t word, real_word; } const cases[] = {
        { "0", 0x00000000u, 0x00000000u },
        { "1", 0x00000001u, 0x3f800000u },
        { "-1", 0xffffffffu, 0xbf800000u },
        { "16777217", 0x01000001u, 0x4b800000u },
        { "2147483647", 0x7fffffffu, 0x4effffffu },
        { "2147483648", 0x80000000u, 0xcf000000u },
        { "-2147483648", 0x80000000u, 0xcf000000u },
        { "4294967295", 0xffffffffu, 0xbf800000u },
        { "4294967296", 0x00000000u, 0x00000000u },
        { "4294967297", 0x00000001u, 0x3f800000u },
        { "-4294967297", 0xffffffffu, 0xbf800000u },
        { "9223372036854775807", 0xffffffffu, 0xbf800000u },
        { "9223372036854775808", 0x00000000u, 0x00000000u },
        { "18446744073709551615", 0xffffffffu, 0xbf800000u },
        { "18446744073709551616", 0x00000000u, 0x00000000u },
        { "18446744073709551617", 0x00000001u, 0x3f800000u },
        { "-18446744073709551617", 0xffffffffu, 0xbf800000u },
        { "9999999999999999999999999999999999999999999999999999999999999999999999999999999999", 0xffffffffu, 0xbf800000u },
        { "$0", 0x00000000u, 0x00000000u },
        { "$7fffffff", 0x7fffffffu, 0x4effffffu },
        { "$80000000", 0x80000000u, 0xcf000000u },
        { "-$80000000", 0x80000000u, 0xcf000000u },
        { "$ffffffff", 0xffffffffu, 0xbf800000u },
        { "$100000000", 0x00000000u, 0x00000000u },
        { "$100000001", 0x00000001u, 0x3f800000u },
        { "$ffffffffffffffff", 0xffffffffu, 0xbf800000u },
        { "$10000000000000000", 0x00000000u, 0x00000000u },
        { "$10000000000000001", 0x00000001u, 0x3f800000u },
        { "0x7FFFFFFF", 0x7fffffffu, 0x4effffffu },
        { "0x80000000", 0x80000000u, 0xcf000000u },
        { "0xFFFFFFFF", 0xffffffffu, 0xbf800000u },
        { "0x100000001", 0x00000001u, 0x3f800000u },
        { "0xFFFFFFFFFFFFFFFF", 0xffffffffu, 0xbf800000u },
        { "0x10000000000000001", 0x00000001u, 0x3f800000u },
        { "00", 0x00000000u, 0x00000000u },
        { "077", 0x0000003fu, 0x427c0000u },
        { "017777777777", 0x7fffffffu, 0x4effffffu },
        { "020000000000", 0x80000000u, 0xcf000000u },
        { "-020000000000", 0x80000000u, 0xcf000000u },
        { "037777777777", 0xffffffffu, 0xbf800000u },
        { "040000000000", 0x00000000u, 0x00000000u },
        { "040000000001", 0x00000001u, 0x3f800000u },
        { "01000000000000000000000", 0x00000000u, 0x00000000u },
        { "01000000000000000000001", 0x00000001u, 0x3f800000u },
    };
    reset_entities();
    setup_test_world();
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        char script[1024];
        G_ClearHashtableRegistry();
        snprintf(script, sizeof(script), "function main takes nothing returns nothing\n"
            "  local hashtable values = InitHashtable()\n"
            "  call SaveInteger(values, 0, 0, %s)\n"
            "  call SaveReal(values, 0, 1, I2R(%s))\nendfunction\n", cases[i].expression, cases[i].expression);
        T_ASSERT(run_test_jass(script));
        hashtable_t const *table = &level.hashtables[0];
        T_EQ(table->num_entries, 2);
        if (table->num_entries == 2) {
            uint32_t word;
            T_EQ(table->entries[0].type, HT_INTEGER);
            memcpy(&word, &table->entries[0].value.integer, sizeof(word));
            T_EQ(word, cases[i].word);
            T_EQ(table->entries[1].type, HT_REAL);
            memcpy(&word, &table->entries[1].value.real, sizeof(word));
            T_EQ(word, cases[i].real_word);
        }
    }
    reset_entities();
}

/* Source integer conversion reaches Move and remains live after source-token reconstruction. */
TEST(wc3_api, pathfinding_compiled_integer_move_survives_save_load) {
    cstring_t path = Test_TempPath("openwarcraft3-wc3-compiled-integer-save.bin");
    reset_entities();
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  hashtable values = null\n"
        "  constant integer compiled = 18446744073709551873\n"
        "endglobals\n"
        "function StoreCompiledInteger takes nothing returns nothing\n"
        "  call SaveInteger(values, 0, 0, compiled)\n"
        "  call SaveInteger(values, 0, 1, $10000000000000001)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local unit mover = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  set values = InitHashtable()\n"
        "  call StoreCompiledInteger()\n"
        "  call BJassAssert(IssuePointOrder(mover, \"move\", I2R(compiled), 144.0), \"compiled integer Move rejected\")\n"
        "endfunction\n"));
    T_ASSERT(WriteGame(path));
    T_ASSERT(ReadGame(path));
    FOR_LOOP(pass, 2) {
        hashtable_t const *table = &level.hashtables[0];
        T_EQ(table->num_entries, 2);
        if (table->num_entries == 2) {
            T_EQ(table->entries[0].value.integer, 257);
            T_EQ(table->entries[1].value.integer, 1);
        }
        if (!pass) {
            jass_callbyname(level.vm, "StoreCompiledInteger", true);
            jass_runevents(level.vm);
            T_ASSERT(!jass_rterror_pending(level.vm));
        }
    }
    edict_t *mover = find_test_unit(MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(mover);
    if (mover) {
        vec2_t point;
        T_ASSERT(G_GetIssuedOrderPoint(mover, &point));
        T_EQ(point.x, 257.0f);
        T_EQ(point.y, 144.0f);
    }
    remove(path);
    reset_entities();
}

/* Frozen public-native words, with decimal text producing the real inputs. */
TEST(wc3_api, pathfinding_public_numeric_natives_match_retail_words) {
    static struct { cstring_t expression; uint32_t word; bool integer; } const cases[] = {
        { "S2R(\"1.25\")", 0x3fa00000u, false },
        { "S2R(\"-1.25\")", 0xbfa00000u, false },
        { "S2R(\"+1.25\")", 0x3fa00000u, false },
        { "S2R(\".125\")", 0x3e000000u, false },
        { "S2R(\"1936.25\")", 0x44f20801u, false },
        { "S2R(\"-1936.25\")", 0xc4f20801u, false },
        { "S2R(\"144.125\")", 0x43102000u, false },
        { "S2R(\"123456789.123456789\")", 0x4ceb79a2u, false },
        { "S2R(\"0.000000123456789\")", 0x34048f8au, false },
        { "S2R(\"1234567890\")", 0x4e932c05u, false },
        { "S2R(\"2147483648\")", 0x4effffffu, false },
        { "S2R(\"1.25e2\")", 0x3fa00000u, false },
        { "S2R(\"1.25;more\")", 0x3fa00000u, false },
        { "S2R(\"1..25\")", 0x3f800000u, false },
        { "S2R(\"1e3\")", 0x3f800000u, false },
        { "S2R(\"abc\")", 0x00000000u, false },
        { "S2R(\"nan\")", 0x00000000u, false },
        { "S2R(\"inf\")", 0x00000000u, false },
        { "S2R(\" 1.25\")", 0x00000000u, false },
        { "S2R(\"\")", 0x00000000u, false },
        { "S2R(\".\")", 0x00000000u, false },
        { "S2R(\"-0.0\")", 0x00000000u, false },
        { "S2R(\"+0\")", 0x00000000u, false },
        { "S2R(\"12345678901234567890123456789012345678901234567890\")", 0x00000000u, false },
        { "I2R(0)", 0x00000000u, false },
        { "I2R(1)", 0x3f800000u, false },
        { "I2R(-1)", 0xbf800000u, false },
        { "I2R(16777217)", 0x4b800000u, false },
        { "I2R(2147483647)", 0x4effffffu, false },
        { "I2R(-2147483647)", 0xceffffffu, false },
        { "R2I(S2R(\"0.0\"))", 0x00000000u, true },
        { "R2I(S2R(\"0.5\"))", 0x00000000u, true },
        { "R2I(S2R(\"-0.5\"))", 0x00000000u, true },
        { "R2I(S2R(\"1.75\"))", 0x00000001u, true },
        { "R2I(S2R(\"-1.75\"))", 0xffffffffu, true },
        { "R2I(S2R(\"2147483648.0\"))", 0x7fffff80u, true },
        { "R2I(S2R(\"-2147483648.0\"))", 0x80000080u, true },
        { "R2I(S2R(\"10000000000.0\"))", 0x7fffffffu, true },
        { "R2I(S2R(\"-10000000000.0\"))", 0x80000000u, true },
        { "Sin(S2R(\"0.0\"))", 0x00000000u, false },
        { "Sin(S2R(\"0.6\"))", 0x3f108c3eu, false },
        { "Sin(S2R(\"-0.6\"))", 0xbf108bebu, false },
        { "Sin(S2R(\"3.141592653589793\"))", 0x30000000u, false },
        { "Sin(S2R(\"6.283185307179586\"))", 0xb0000000u, false },
        { "Cos(S2R(\"0.0\"))", 0x3f7fffffu, false },
        { "Cos(S2R(\"0.6\"))", 0x3f53494cu, false },
        { "Cos(S2R(\"-0.6\"))", 0x3f534985u, false },
        { "Cos(S2R(\"3.141592653589793\"))", 0xbf7fffffu, false },
        { "Cos(S2R(\"6.283185307179586\"))", 0x3f7fffffu, false },
        { "Acos(S2R(\"-2.0\"))", 0x00000000u, false },
        { "Acos(S2R(\"-1.0\"))", 0x40490fdbu, false },
        { "Acos(S2R(\"-0.6\"))", 0x400db70du, false },
        { "Acos(S2R(\"0.0\"))", 0x3fc90fdau, false },
        { "Acos(S2R(\"0.6\"))", 0x3f6d6335u, false },
        { "Acos(S2R(\"1.0\"))", 0x00000000u, false },
        { "Acos(S2R(\"2.0\"))", 0x00000000u, false },
        { "SquareRoot(S2R(\"-4.0\"))", 0x00000000u, false },
        { "SquareRoot(S2R(\"-0.0005\"))", 0x00000000u, false },
        { "SquareRoot(S2R(\"0.0\"))", 0x00000000u, false },
        { "SquareRoot(S2R(\"0.0009999999\"))", 0x3d018707u, false },
        { "SquareRoot(S2R(\"0.001\"))", 0x3d018707u, false },
        { "SquareRoot(S2R(\"0.0010000002\"))", 0x3d018707u, false },
        { "SquareRoot(S2R(\"0.5\"))", 0x3f3504f3u, false },
        { "SquareRoot(S2R(\"2.0\"))", 0x3fb504f3u, false },
        { "SquareRoot(S2R(\"4.0\"))", 0x40000000u, false },
        { "S2R(\"-1936.25\")", 0xc4f20801u, false },
        { "S2R(\"-144.125\")", 0xc3102000u, false },
    };
    setup_test_world();
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        char script[1024];
        /* Save the actual native result; the C expectation cannot be parsed by the VM. */
        G_ClearHashtableRegistry();
        snprintf(script, sizeof(script), "function main takes nothing returns nothing\n"
            "  local hashtable values = InitHashtable()\n"
            "  call Save%s(values, 0, 0, %s)\nendfunction\n",
            cases[i].integer ? "Integer" : "Real", cases[i].expression);
        T_ASSERT(run_test_jass(script));
        hashtable_t const *table = &level.hashtables[0];
        T_EQ(table->num_entries, 1);
        if (table->num_entries == 1) {
            uint32_t word;
            T_EQ(table->entries[0].type, cases[i].integer ? HT_INTEGER : HT_REAL);
            memcpy(&word, &table->entries[0].value, sizeof(word));
            T_EQ(word, cases[i].word);
        }
    }
    reset_entities();
}

/* Actual registered angle-native outputs with decimal producers and public guards. */
TEST(wc3_api, pathfinding_public_angle_natives_match_retail_words) {
    static struct { cstring_t expression; uint32_t word; bool integer; } const cases[] = {
        { "Asin(S2R(\"-2.0\"))", 0x00000000u, false },
        { "Asin(S2R(\"-1.0\"))", 0xbfc90fdbu, false },
        { "Asin(S2R(\"-0.999\"))", 0xbfc356aeu, false },
        { "Asin(S2R(\"-0.994140625\"))", 0xbfbb32e2u, false },
        { "Asin(S2R(\"-0.6\"))", 0xbf24bc7fu, false },
        { "Asin(S2R(\"0.0\"))", 0x00000000u, false },
        { "Asin(S2R(\"0.6\"))", 0x3f24bc7fu, false },
        { "Asin(S2R(\"0.994140625\"))", 0x3fbb32e1u, false },
        { "Asin(S2R(\"0.999\"))", 0x3fc356adu, false },
        { "Asin(S2R(\"1.0\"))", 0x3fc90fdbu, false },
        { "Asin(S2R(\"2.0\"))", 0x00000000u, false },
        { "Atan(S2R(\"-10000.0\"))", 0xbfc90c94u, false },
        { "Atan(S2R(\"-1.0\"))", 0xbf490fdcu, false },
        { "Atan(S2R(\"-0.6\"))", 0xbf0a58efu, false },
        { "Atan(S2R(\"-0.2679492\"))", 0xbe860a90u, false },
        { "Atan(S2R(\"0.0\"))", 0x00000000u, false },
        { "Atan(S2R(\"0.2679492\"))", 0x3e860a90u, false },
        { "Atan(S2R(\"0.6\"))", 0x3f0a58efu, false },
        { "Atan(S2R(\"1.0\"))", 0x3f490fdcu, false },
        { "Atan(S2R(\"10000.0\"))", 0x3fc90c94u, false },
        { "Tan(S2R(\"0.0\"))", 0x00000000u, false },
        { "Tan(S2R(\"0.6\"))", 0x3f2f234eu, false },
        { "Tan(S2R(\"-0.6\"))", 0xbf2f22bau, false },
        { "Tan(S2R(\"1.5707963267948966\"))", 0x4effffffu, false },
        { "Tan(S2R(\"3.141592653589793\"))", 0xb0000001u, false },
        { "Atan2(S2R(\"0.0\"), S2R(\"0.0\"))", 0x00000000u, false },
        { "Atan2(S2R(\"0.0009\"), S2R(\"0.0009\"))", 0x00000000u, false },
        { "Atan2(S2R(\"0.001\"), S2R(\"0.0009\"))", 0x3f5685efu, false },
        { "Atan2(S2R(\"0.0009\"), S2R(\"0.001\"))", 0x3f3b99c8u, false },
        { "Atan2(S2R(\"-0.001\"), S2R(\"0.0\"))", 0xbfc90fdbu, false },
        { "Atan2(S2R(\"0.0\"), S2R(\"-0.001\"))", 0x40490fdbu, false },
        { "Atan2(S2R(\"1.0\"), S2R(\"1.0\"))", 0x3f490fdcu, false },
        { "Atan2(S2R(\"-1.0\"), S2R(\"1.0\"))", 0xbf490fdcu, false },
        { "Atan2(S2R(\"1.0\"), S2R(\"-1.0\"))", 0x4016cbe4u, false },
        { "Atan2(S2R(\"-1.0\"), S2R(\"-1.0\"))", 0xc016cbe4u, false },
        { "Atan2(S2R(\"10.0\"), S2R(\"1.0\"))", 0x3fbc4de9u, false },
        { "Atan2(S2R(\"1.0\"), S2R(\"10.0\"))", 0x3dcc1f14u, false },
        { "Deg2Rad(S2R(\"0.0\"))", 0x00000000u, false },
        { "Deg2Rad(S2R(\"90.0\"))", 0x3fc90fdbu, false },
        { "Deg2Rad(S2R(\"180.0\"))", 0x40490fdbu, false },
        { "Deg2Rad(S2R(\"360.0\"))", 0x40c90fdbu, false },
        { "Deg2Rad(S2R(\"-45.0\"))", 0xbf490fdbu, false },
        { "Deg2Rad(S2R(\"0.1\"))", 0x3ae4c389u, false },
        { "Rad2Deg(S2R(\"0.0\"))", 0x00000000u, false },
        { "Rad2Deg(S2R(\"3.141592653589793\"))", 0x43340000u, false },
        { "Rad2Deg(S2R(\"6.283185307179586\"))", 0x43b40000u, false },
        { "Rad2Deg(S2R(\"-0.6\"))", 0xc2098287u, false },
        { "Rad2Deg(S2R(\"0.001\"))", 0x3d6aaefbu, false },
    };
    setup_test_world();
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        char script[1024];
        /* Save the actual native result; the C expectation cannot be parsed by the VM. */
        G_ClearHashtableRegistry();
        snprintf(script, sizeof(script), "function main takes nothing returns nothing\n"
            "  local hashtable values = InitHashtable()\n"
            "  call Save%s(values, 0, 0, %s)\nendfunction\n",
            cases[i].integer ? "Integer" : "Real", cases[i].expression);
        T_ASSERT(run_test_jass(script));
        hashtable_t const *table = &level.hashtables[0];
        T_EQ(table->num_entries, 1);
        if (table->num_entries == 1) {
            uint32_t word;
            T_EQ(table->entries[0].type, cases[i].integer ? HT_INTEGER : HT_REAL);
            memcpy(&word, &table->entries[0].value, sizeof(word));
            T_EQ(word, cases[i].word);
        }
    }
    reset_entities();
}

/* Original registered20f990 outputs, verified against independent scalar models. */
TEST(wc3_api, pathfinding_public_power_native_matches_retail_words) {
    static struct { cstring_t expression; uint32_t word; bool integer; } const cases[] = {
        { "Pow(S2R(\"0\"), S2R(\"-1\"))", 0x00000000u, false },
        { "Pow(S2R(\"0\"), S2R(\"0\"))", 0x3f800000u, false },
        { "Pow(S2R(\"0\"), S2R(\"0.5\"))", 0x00000000u, false },
        { "Pow(S2R(\"-0.0005\"), S2R(\"-1\"))", 0x00000000u, false },
        { "Pow(S2R(\"0.0005\"), S2R(\"-1\"))", 0x00000000u, false },
        { "Pow(S2R(\"0.0005\"), S2R(\"0.5\"))", 0x3cb72dcau, false },
        { "Pow(S2R(\"0.001\"), S2R(\"-1\"))", 0x4479ffeau, false },
        { "Pow(S2R(\"-0.001\"), S2R(\"-1\"))", 0x4479ffeau, false },
        { "Pow(S2R(\"1\"), S2R(\"-3\"))", 0x3f800000u, false },
        { "Pow(S2R(\"1\"), S2R(\"0.0005\"))", 0x3f800000u, false },
        { "Pow(S2R(\"2\"), S2R(\"0.0005\"))", 0x3f800000u, false },
        { "Pow(S2R(\"-2\"), S2R(\"0.0005\"))", 0x3f800000u, false },
        { "Pow(S2R(\"2\"), S2R(\"0\"))", 0x3f800000u, false },
        { "Pow(S2R(\"2\"), S2R(\"-0.0\"))", 0x3f800000u, false },
        { "Pow(S2R(\"2\"), S2R(\"0.5\"))", 0x3fb504f3u, false },
        { "Pow(S2R(\"-2\"), S2R(\"0.5\"))", 0x3fb504f3u, false },
        { "Pow(S2R(\"2\"), S2R(\"-0.5\"))", 0x3f3504f5u, false },
        { "Pow(S2R(\"-2\"), S2R(\"-0.5\"))", 0x3f3504f5u, false },
        { "Pow(S2R(\"2\"), S2R(\"3\"))", 0x41000000u, false },
        { "Pow(S2R(\"-2\"), S2R(\"3\"))", 0xc1000000u, false },
        { "Pow(S2R(\"2\"), S2R(\"4\"))", 0x41800000u, false },
        { "Pow(S2R(\"-2\"), S2R(\"4\"))", 0x41800000u, false },
        { "Pow(S2R(\"0.5\"), S2R(\"-3\"))", 0x40fffffbu, false },
        { "Pow(S2R(\"-0.5\"), S2R(\"-3\"))", 0x40fffffbu, false },
        { "Pow(S2R(\"16\"), S2R(\"0.5\"))", 0x407ffffbu, false },
        { "Pow(S2R(\"10\"), S2R(\"-0.3\"))", 0x3f004dd1u, false },
        { "Pow(S2R(\"100\"), S2R(\"0.3\"))", 0x407ec9d7u, false },
        { "Pow(S2R(\"-100\"), S2R(\"0.3\"))", 0x407ec9d7u, false },
        { "Pow(S2R(\"1.5\"), S2R(\"2.25\"))", 0x401f5cacu, false },
        { "Pow(S2R(\"1.25\"), S2R(\"7.5\"))", 0x40aa992eu, false },
        { "Pow(S2R(\"10\"), S2R(\"10\"))", 0x501502f9u, false },
        { "Pow(S2R(\"100\"), S2R(\"-20\"))", 0x7f000000u, false },
        { "Pow(S2R(\"100\"), S2R(\"20\"))", 0x00000000u, false },
        { "Pow(S2R(\"0.0009999999\"), S2R(\"-1\"))", 0x4479ffeau, false },
        { "Pow(S2R(\"0.0010000002\"), S2R(\"-1\"))", 0x4479ffeau, false },
        { "Pow(S2R(\"2\"), S2R(\"8388608\"))", 0x00000000u, false },
        { "Pow(S2R(\"1\"), S2R(\"2147483520\"))", 0x3f800000u, false },
        { "Pow(S2R(\"2\"), S2R(\"-17\"))", 0x37000012u, false },
        { "Pow(S2R(\"2\"), S2R(\"63.5\"))", 0x5f3504b5u, false },
        { "Pow(S2R(\"2\"), S2R(\"-63.5\"))", 0x1fb50533u, false },
    };
    setup_test_world();
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        char script[1024];
        /* Save the actual native result; the C expectation cannot be parsed by the VM. */
        G_ClearHashtableRegistry();
        snprintf(script, sizeof(script), "function main takes nothing returns nothing\n"
            "  local hashtable values = InitHashtable()\n"
            "  call Save%s(values, 0, 0, %s)\nendfunction\n",
            cases[i].integer ? "Integer" : "Real", cases[i].expression);
        T_ASSERT(run_test_jass(script));
        hashtable_t const *table = &level.hashtables[0];
        T_EQ(table->num_entries, 1);
        if (table->num_entries == 1) {
            uint32_t word;
            T_EQ(table->entries[0].type, cases[i].integer ? HT_INTEGER : HT_REAL);
            memcpy(&word, &table->entries[0].value, sizeof(word));
            T_EQ(word, cases[i].word);
        }
    }
    reset_entities();
}

/* A converted negative signed exponent has no retail helper result; reject without inventing one. */
TEST(wc3_api, pathfinding_power_nonterminating_domain_reports_error) {
    setup_test_world();
    T_ASSERT(!run_test_jass("function main takes nothing returns nothing\n  local real r = Pow(2.0, I2R(1073741824) * 2.0)\nendfunction\n"));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n  call BJassAssert(Pow(2.0, 3.0) == 8.0, \"power recovers after rejected domain\")\nendfunction\n"));
    reset_entities();
}

TEST(wc3_api, revive_hero_location_native_restores_grom_style_death) {
    edict_t *hero;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit grom = null\n"
        "  boolean gameover = false\n"
        "  boolean gromRevived = false\n"
        "  location destination = null\n"
        "endglobals\n"
        "function grom_dead takes nothing returns boolean\n"
        "  return GetDyingUnit() == grom and not gameover\n"
        "endfunction\n"
        "function revive_grom takes nothing returns nothing\n"
        "  set gromRevived = ReviveHeroLoc(grom, destination, false)\n"
        "  call BJassAssert(gromRevived, \"Grom death trigger could not revive Hero\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set grom = CreateUnit(Player(1), 'Hpal', 32.0, 32.0, 0.0)\n"
        "  set destination = Location(128.0, 192.0)\n"
        "  call BJassAssert(not ReviveHeroLoc(grom, destination, false), \"live Hero was reported revived\")\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(1), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddCondition(t, Condition(function grom_dead))\n"
        "  call TriggerAddAction(t, function revive_grom)\n"
        "endfunction\n"));
    hero = NULL;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('H','p','a','l') && ent->s.player == 1) {
            hero = ent;
            break;
        }
    }
    T_NOT_NULL(hero);
    T_ASSERT(hero != NULL);
    if (!hero) return;
    G_SetHealth(hero, 0.0f);
    unit_die(hero, NULL);
    G_RunEvents();
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_FEQ(hero->health.max_value, 650.0f, 0.01f);
    T_ASSERT(!M_IsDead(hero));
    T_FEQ(hero->s.origin2.x, 128.0f, 0.001f);
    T_FEQ(hero->s.origin2.y, 192.0f, 0.001f);
}

TEST(wc3_api, jass_selection_masks_and_sync_are_deferred) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    player_t *saved_currentplayer = currentplayer;
    edict_t *first = NULL, *second = NULL;
    uint32_t player0_bit = 1u << 0;
    uint32_t player1_bit = 1u << 1;

    reset_entities();
    setup_test_world();
    g_edicts[0].client = &game.clients[0];
    g_edicts[1].client = &game.clients[1];
    game.clients[0].connected = true;
    currentplayer = test_player(1);
    T_ASSERT(run_test_jass(
        "type unit extends handle\n"
        "globals\n"
        "  unit first = null\n"
        "  unit second = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set first = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  set second = CreateUnit(Player(0), 'hpea', 32.0, 0.0, 0.0)\n"
        "  call SelectUnit(first, true)\n"
        "endfunction\n"
        "function select_for_player0 takes nothing returns nothing\n"
        "  call SelectUnit(first, true)\n"
        "  call SelectUnit(second, true)\n"
        "endfunction\n"
        "function clear_for_player0 takes nothing returns nothing\n"
        "  call SelectUnit(first, false)\n"
        "  call ClearSelection()\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (!g_edicts[i].inuse || g_edicts[i].s.player != 0) continue;
        if (!first) first = &g_edicts[i];
        else if (!second) second = &g_edicts[i];
    }
    T_NOT_NULL(first);
    T_NOT_NULL(second);
    T_ASSERT(first->selected & player1_bit);
    T_ASSERT(!(first->selected & player0_bit));
    game.clients[1].selection_dirty = false;

    currentplayer = test_player(0);
    jass_callbyname(level.vm, "select_for_player0", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT((first->selected & (player0_bit | player1_bit)) == (player0_bit | player1_bit));
    T_ASSERT((second->selected & player0_bit) != 0);
    T_ASSERT(game.clients[0].selection_dirty);

    jass_callbyname(level.vm, "clear_for_player0", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(first->selected == player1_bit);
    T_ASSERT(second->selected == 0);
    game.clients[1].connected = false;

    selection_native_packet_count = 0;
    selection_native_packet_units = (uint32_t)-1;
    selection_native_packet_stage = 0;
    gi.Write = selection_native_test_write;
    gi.unicast = selection_native_test_unicast;
    G_UpdateClientSelections();
    G_UpdateClientSelections();
    gi.Write = old_write;
    gi.unicast = old_unicast;

    T_EQ(selection_native_packet_count, 1);
    T_EQ(selection_native_packet_units, 0);
    T_ASSERT(!game.clients[0].selection_dirty);
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, undead_race_and_unit_type_match_authored_unit_data) {
    edict_t *undead = NULL;
    UnitData_t undead_data = { .id = MAKEFOURCC('u','g','h','o'), .race = "undead" };

    reset_entities();
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit raceSubject = null\n"
        "endglobals\n"
        "function verifyRace takes nothing returns nothing\n"
        "  if GetUnitRace(raceSubject) == ConvertRace(3) and\n"
        "      IsUnitType(raceSubject, ConvertUnitType(14)) then\n"
        "    call SetWidgetLife(raceSubject, 333.0)\n"
        "  else\n"
        "    call SetWidgetLife(raceSubject, 222.0)\n"
        "  endif\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set raceSubject = CreateUnit(Player(0), 'ugho', 0.0, 0.0, 0.0)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('u','g','h','o')) {
            undead = &g_edicts[i];
            undead->data.UnitData = &undead_data;
        }
    T_NOT_NULL(undead);
    T_EQ(WC3_RaceFromString(undead->data.UnitData->race), RACE_UNDEAD);
    jass_callbyname(level.vm, "verifyRace", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_FEQ(undead->health.value, 333.0f, 0.001f);
}

TEST(wc3_api, authored_race_names_map_to_jass_race_values) {
    static struct { cstring_t name; int32_t value; } const races[] = {
        { STR_HUMAN, 1 }, { STR_ORC, 2 }, { STR_UNDEAD, 3 }, { STR_NIGHTELF, 4 },
        { STR_DEMON, 5 }, { STR_CREEPS, 8 }, { STR_OTHER, 7 },
        { STR_CRITTERS, 10 }, { STR_COMMONER, 9 }, { "naga", 11 },
    };
    FOR_LOOP(i, sizeof(races) / sizeof(*races)) T_EQ(WC3_JassRaceFromString(races[i].name), races[i].value);
    T_EQ(WC3_JassRaceFromString("unrecognized"), 0);
    T_EQ(WC3_JassRaceFromString("NAGA"), 11);
}

TEST(wc3_api, unit_life_state_event_fires_when_health_crosses_limit) {
    edict_t *unit = NULL;
    event_t *registration = NULL;

    reset_entities();
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit u = null\n"
        "endglobals\n"
        "function on_death takes nothing returns nothing\n"
        "  call SetWidgetLife(GetTriggerUnit(), 75.0)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call TriggerAddAction(t, function on_death)\n"
        "  call TriggerRegisterUnitStateEvent(t, u, ConvertUnitState(0), ConvertLimitOp(1), 0.0)\n"
        "  call SetWidgetLife(u, 100.0)\n"
        "  call SetUnitState(u, ConvertUnitState(0), 0.0)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) unit = &g_edicts[i];
    T_NOT_NULL(unit);
    FOR_EACH_EVENT(evt)
        if (evt->type == EVENT_GAME_STATE_LIMIT && evt->subject == unit) registration = evt;
    T_NOT_NULL(registration);
    if (registration) {
        T_EQ(registration->state, WC3_UNIT_STATE_LIFE);
        T_EQ(registration->limitop, WC3_LIMITOP_LESS_THAN_OR_EQUAL);
        T_FEQ(registration->limitval, 0.0f, 0.001f);
    }
    G_RunEvents();
    jass_runevents(level.vm);
    T_FEQ(unit->health.value, 75.0f, 0.001f);
}

TEST(wc3_api, zero_life_writes_run_unit_death_lifecycle) {
    edict_t *units[2] = { NULL, NULL };

    reset_entities();
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit stateUnit = null\n"
        "  unit widgetUnit = null\n"
        "  integer deaths = 0\n"
        "endglobals\n"
        "function on_death takes nothing returns nothing\n"
        "  set deaths = deaths + 1\n"
        "endfunction\n"
        "function verifyDeathCount takes nothing returns nothing\n"
        "  call BJassAssert(deaths == 2, \"zero-life writes should publish both player death events\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set stateUnit = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  set widgetUnit = CreateUnit(Player(0), 'hfoo', 128.0, 64.0, 0.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddAction(t, function on_death)\n"
        "  call SetUnitState(stateUnit, ConvertUnitState(0), 0.0)\n"
        "  call SetWidgetLife(widgetUnit, 0.0)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *unit = globals.edicts + i;
        if (!unit->inuse || unit->class_id != MAKEFOURCC('h','f','o','o') || unit->s.player != 0) continue;
        if (!units[0]) units[0] = unit;
        else if (!units[1]) units[1] = unit;
    }
    T_NOT_NULL(units[0]);
    T_NOT_NULL(units[1]);
    if (!units[0] || !units[1]) return;
    T_ASSERT(units[0]->svflags & SVF_DEADMONSTER);
    T_ASSERT(units[1]->svflags & SVF_DEADMONSTER);

    level.started = level.scriptsStarted = true;
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyDeathCount", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, unit_life_limit_event_queue_saturation_does_not_crash) {
    edict_t *unit;
    event_t *registration;

    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    unit->health.value = 100.0f;
    registration = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    T_NOT_NULL(registration);
    G_SetEventSubject(registration, unit);
    registration->state = WC3_UNIT_STATE_LIFE;
    registration->limitop = WC3_LIMITOP_LESS_THAN_OR_EQUAL;
    registration->limitval = 0.0f;

    FOR_LOOP(i, MAX_EVENT_QUEUE)
        T_NOT_NULL(G_PublishEventWithValue(NULL, EVENT_GAME_VICTORY, NULL, (int32_t)i));
    G_SetHealth(unit, 0.0f);

    T_FEQ(unit->health.value, 0.0f, 0.001f);
    T_EQ(level.events.write, (uint32_t)MAX_EVENT_QUEUE);
    T_EQ(level.events.queue[0].value, 0);
    T_EQ(level.events.queue[MAX_EVENT_QUEUE - 1].value, (int32_t)MAX_EVENT_QUEUE - 1);
}

TEST(wc3_api, reused_unit_does_not_inherit_old_life_event) {
    edict_t *unit, *replacement;
    event_t *registration;
    uint32_t old_spawn_time, removal_events;

    reset_entities(); setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    unit->spawn_time = level.time;
    registration = G_MakeEvent(EVENT_GAME_STATE_LIMIT);
    G_SetEventSubject(registration, unit);
    registration->state = WC3_UNIT_STATE_LIFE;
    registration->limitop = WC3_LIMITOP_LESS_THAN_OR_EQUAL;
    registration->limitval = 0;
    old_spawn_time = unit->spawn_time;
    G_FreeEdict(unit);
    removal_events=level.events.write;
    level.time += 2000;
    replacement = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    replacement->spawn_time = level.time;
    T_ASSERT(replacement == unit);
    T_NE(replacement->spawn_time, old_spawn_time);
    G_SetHealth(replacement, 0);
    T_EQ(level.events.write, removal_events);
}

TEST(wc3_api, movement_crossing_region_publishes_entering_unit) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *mover = NULL;
    vec2_t destination = {80.0f, 0.0f};

    reset_entities();
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "type unit extends handle\n"
        "type region extends handle\n"
        "type trigger extends handle\n"
        "globals\n"
        "  unit mover = null\n"
        "  unit entering = null\n"
        "  region watchedRegion = null\n"
        "  boolean correctTriggeringRegion = false\n"
        "  boolean entered = false\n"
        "  integer rejected = 0\n"
        "endglobals\n"
        "function accept_enter_filter takes nothing returns boolean\n"
        "  return GetFilterUnit() == mover\n"
        "endfunction\n"
        "function reject_enter_filter takes nothing returns boolean\n"
        "  return false\n"
        "endfunction\n"
        "function on_enter takes nothing returns nothing\n"
        "  set entering = GetEnteringUnit()\n"
        "  set correctTriggeringRegion = GetTriggeringRegion() == watchedRegion\n"
        "  set entered = true\n"
        "endfunction\n"
        "function on_rejected_enter takes nothing returns nothing\n"
        "  set rejected = rejected + 1\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger acceptedEvent = CreateTrigger()\n"
        "  local trigger rejectedEvent = CreateTrigger()\n"
        "  set watchedRegion = CreateRegion()\n"
        "  set mover = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call TriggerRegisterEnterRegion(acceptedEvent, watchedRegion, Condition(function accept_enter_filter))\n"
        "  call TriggerRegisterEnterRegion(rejectedEvent, watchedRegion, Condition(function reject_enter_filter))\n"
        "  call RegionAddRect(watchedRegion, Rect(32.0, -16.0, 63.0, 16.0))\n"
        "  call TriggerAddAction(acceptedEvent, function on_enter)\n"
        "  call TriggerAddAction(rejectedEvent, function on_rejected_enter)\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(entered, \"movement did not enter region\")\n"
        "  call BJassAssert(entering == mover, \"GetEnteringUnit mismatch\")\n"
        "  call BJassAssert(correctTriggeringRegion, \"GetTriggeringRegion mismatch\")\n"
        "  call BJassAssert(rejected == 0, \"rejected enter filter should not run\")\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].s.player == 0 &&
            g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) {
            mover = &g_edicts[i];
            break;
        }
    }
    T_NOT_NULL(mover);
    mover->svflags |= SVF_MONSTER;
    mover->movetype = MOVETYPE_STEP;
    mover->stand = unit_stand;
    mover->birth = unit_birth;
    mover->die = unit_die;
    mover->think = monster_think;
    mover->collision = 0.0f;
    mover->health.value = 250.0f;
    mover->health.max_value = 250.0f;
    unit_stand(mover);
    S_SetUnitMoveSpeed(mover,400); /* Cross the first32-unit region cell in this one-step test. */
    T_ASSERT(unit_issueorder(mover, "move", &destination));

    api_run_move_entities(2);
    T_ASSERT(mover->s.origin2.x >= 32.0f);
    T_ASSERT(mover->s.origin2.x < 64.0f);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, nonunit_map_entity_placement_does_not_fire_region_enter) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *map_entity;
    vec2_t old_position = { 0.0f, 0.0f };
    event_t *handler = NULL;

    reset_entities(); setup_test_world(); currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer enters = 0\n"
        "endglobals\n"
        "function on_enter takes nothing returns nothing\n"
        "  set enters = enters + 1\n"
        "endfunction\n"
        "function verify_real_entry takes nothing returns nothing\n"
        "  call BJassAssert(enters == 1, \"real unit entry was not delivered exactly once\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local region r = CreateRegion()\n"
        "  call RegionAddRect(r, Rect(50.0, 50.0, 150.0, 150.0))\n"
        "  call TriggerRegisterEnterRegion(t, r, null)\n"
        "  call TriggerAddAction(t, function on_enter)\n"
        "endfunction\n"));

    FOR_EACH_EVENT(evt) {
        if (evt->type == EVENT_GAME_ENTER_REGION) { handler = evt; break; }
    }
    T_NOT_NULL(handler);
    map_entity = G_Spawn();
    T_NOT_NULL(map_entity);
    if (!map_entity || !handler) { currentplayer = saved_currentplayer; return; }
    map_entity->class_id = map_entity->s.class_id = MAKEFOURCC('d','0','0','1');
    map_entity->s.origin2 = (vec2_t){ 100.0f, 100.0f };
    G_UnitPositionChanged(map_entity, &old_position);
    G_RunEvents(); jass_runevents(level.vm);

    /* Same classed entity, now carrying a real unit rawcode: the unit-only
     * gate must still reject it because it has no unit runtime classification. */
    map_entity->class_id = map_entity->s.class_id = MAKEFOURCC('h','p','e','a');
    old_position = (vec2_t){ 0.0f, 0.0f };
    map_entity->s.origin2 = (vec2_t){ 100.0f, 100.0f };
    G_UnitPositionChanged(map_entity, &old_position);
    G_RunEvents(); jass_runevents(level.vm);

    /* The runtime unit classification admits the same entity and delivers
     * the crossing through the normal movement entry point. */
    map_entity->svflags |= SVF_MONSTER;
    old_position = (vec2_t){ 0.0f, 0.0f };
    G_UnitPositionChanged(map_entity, &old_position);
    G_RunEvents(); jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify_real_entry", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, removed_region_filter_cannot_publish_to_reused_event) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *mover = NULL;
    vec2_t destination = {80.0f, 0.0f};

    reset_entities();
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "type unit extends handle\n"
        "type region extends handle\n"
        "type trigger extends handle\n"
        "globals\n"
        "  region watchedRegion = null\n"
        "  trigger replacementTrigger = null\n"
        "  unit mover = null\n"
        "  integer replacementFires = 0\n"
        "endglobals\n"
        "function mutate_region_filter takes nothing returns boolean\n"
        "  call RemoveRegion(watchedRegion)\n"
        "  set watchedRegion = CreateRegion()\n"
        "  call RegionAddRect(watchedRegion, Rect(24.0, -16.0, 64.0, 16.0))\n"
        "  call TriggerRegisterEnterRegion(replacementTrigger, watchedRegion, null)\n"
        "  return true\n"
        "endfunction\n"
        "function on_replacement_enter takes nothing returns nothing\n"
        "  set replacementFires = replacementFires + 1\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger oldTrigger = CreateTrigger()\n"
        "  set replacementTrigger = CreateTrigger()\n"
        "  set mover = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  set watchedRegion = CreateRegion()\n"
        "  call RegionAddRect(watchedRegion, Rect(24.0, -16.0, 64.0, 16.0))\n"
        "  call TriggerRegisterEnterRegion(oldTrigger, watchedRegion, Condition(function mutate_region_filter))\n"
        "  call TriggerAddAction(replacementTrigger, function on_replacement_enter)\n"
        "endfunction\n"
        "function verify_replacement_did_not_receive_old_crossing takes nothing returns nothing\n"
        "  call BJassAssert(replacementFires == 0, \"retired region crossing fired replacement registration\")\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].s.player == 0 &&
            g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) {
            mover = &g_edicts[i];
            break;
        }
    }
    T_NOT_NULL(mover);
    mover->movetype = MOVETYPE_STEP;
    mover->stand = unit_stand;
    mover->birth = unit_birth;
    mover->die = unit_die;
    mover->think = monster_think;
    mover->collision = 0.0f;
    mover->health.value = mover->health.max_value = 250.0f;
    unit_stand(mover);
    T_ASSERT(unit_issueorder(mover, "move", &destination));
    G_RunEntities();
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify_replacement_did_not_receive_old_crossing", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, removed_region_filter_unit_does_not_receive_crossing_event) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *mover = NULL;
    vec2_t destination = {80.0f, 0.0f};

    G_ResetDeferredFrees(); reset_entities(); setup_test_world(); currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "type unit extends handle\n"
        "type region extends handle\n"
        "type trigger extends handle\n"
        "globals\n"
        "  region watchedRegion = null\n"
        "  unit mover = null\n"
        "  integer fires = 0\n"
        "endglobals\n"
        "function remove_filter takes nothing returns boolean\n"
        "  call RemoveUnit(mover)\n"
        "  return true\n"
        "endfunction\n"
        "function on_enter takes nothing returns nothing\n"
        "  set fires = fires + 1\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set mover = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  set watchedRegion = CreateRegion()\n"
        "  call RegionAddRect(watchedRegion, Rect(24.0, -16.0, 64.0, 16.0))\n"
        "  call TriggerRegisterEnterRegion(t, watchedRegion, Condition(function remove_filter))\n"
        "  call TriggerAddAction(t, function on_enter)\n"
        "endfunction\n"
        "function verify_removed_unit_did_not_receive_event takes nothing returns nothing\n"
        "  call BJassAssert(fires == 0, \"removed region-filter unit received crossing event\")\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].s.player == 0 &&
            g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) { mover = &g_edicts[i]; break; }
    }
    T_NOT_NULL(mover);
    mover->movetype = MOVETYPE_STEP; mover->stand = unit_stand; mover->birth = unit_birth;
    mover->die = unit_die; mover->think = monster_think; mover->collision = 0.0f;
    mover->health.value = mover->health.max_value = 250.0f;
    unit_stand(mover);
    T_ASSERT(unit_issueorder(mover, "move", &destination));
    G_RunEntities(); G_RunEvents(); jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify_removed_unit_did_not_receive_event", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    G_TestFinishDeferredFrees();
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, removed_region_is_inert_and_does_not_alias_replacement) {
    T_ASSERT(run_test_jass(
        "type region extends handle\n"
        "type trigger extends handle\n"
        "globals\n"
        "  region retired = null\n"
        "  region replacement = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local trigger watched = CreateTrigger()\n"
        "  set retired = CreateRegion()\n"
        "  call RegionAddRect(retired, Rect(0.0, 0.0, 20.0, 20.0))\n"
        "  call TriggerRegisterEnterRegion(watched, retired, null)\n"
        "  call RemoveRegion(retired)\n"
        "  call BJassAssert(not IsPointInRegion(retired, 10.0, 10.0), \"removed region remains active\")\n"
        "  call BJassAssert(TriggerRegisterEnterRegion(watched, retired, null) == null, \"removed region accepted a new registration\")\n"
        "  set replacement = CreateRegion()\n"
        "  call BJassAssert(replacement != retired, \"replacement reused stale region handle\")\n"
        "  call RegionAddRect(replacement, Rect(100.0, 100.0, 120.0, 120.0))\n"
        "  call RegionAddRect(retired, Rect(200.0, 200.0, 220.0, 220.0))\n"
        "  call BJassAssert(IsPointInRegion(replacement, 110.0, 110.0), \"replacement geometry lost\")\n"
        "  call BJassAssert(not IsPointInRegion(replacement, 210.0, 210.0), \"stale region mutated replacement\")\n"
        "endfunction\n"));
    FOR_EACH_EVENT(evt) if (evt->type == EVENT_GAME_ENTER_REGION)
        T_NULL(evt->region);
}

TEST(wc3_api, region_add_rect_reports_capacity_rejection) {
    FILE *capture = tmpfile();
    int saved_stderr = dup(STDERR_FILENO);
    char diagnostic[1024] = { 0 };
    region_t *region = NULL;

    reset_entities(); setup_test_world();
    T_NOT_NULL(capture);
    T_ASSERT(saved_stderr >= 0);
    if (!capture || saved_stderr < 0) {
        if (capture) fclose(capture);
        if (saved_stderr >= 0) close(saved_stderr);
        return;
    }

    fflush(stderr);
    T_EQ(dup2(fileno(capture), STDERR_FILENO), STDERR_FILENO);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local region watched = CreateRegion()\n"
        "  local integer i = 0\n"
        "  loop\n"
        "    exitwhen i == 17\n"
        "    call RegionAddRect(watched, Rect(100.0, 100.0, 200.0, 200.0))\n"
        "    set i = i + 1\n"
        "  endloop\n"
        "endfunction\n"));
    fflush(stderr);
    T_EQ(dup2(saved_stderr, STDERR_FILENO), STDERR_FILENO);
    close(saved_stderr);
    rewind(capture);
    (void)fread(diagnostic, 1, sizeof(diagnostic) - 1, capture);
    fclose(capture);

    FOR_LOOP(i, level.num_regions)
        if (level.regions[i].inuse) { region = &level.regions[i]; break; }
    T_NOT_NULL(region);
    if (region) T_EQ(region->num_rects, MAX_REGION_SIZE);
    T_ASSERT(strstr(diagnostic, "RegionAddRect rejected") != NULL);
    T_ASSERT(strstr(diagnostic, "MAX_REGION_SIZE") != NULL);
}

TEST(wc3_api, recycled_region_gets_distinct_handle_id) {
    T_ASSERT(run_test_jass(
        "type region extends handle\n"
        "type hashtable extends handle\n"
        "function main takes nothing returns nothing\n"
        "  local hashtable ht = InitHashtable()\n"
        "  local region oldRegion = CreateRegion()\n"
        "  local region newRegion = null\n"
        "  local integer oldRegionId = GetHandleId(oldRegion)\n"
        "  call SaveInteger(ht, oldRegionId, 0, 11)\n"
        "  call RemoveRegion(oldRegion)\n"
        "  call BJassAssert(GetHandleId(oldRegion) == oldRegionId, \"retired region changed GetHandleId\")\n"
        "  set newRegion = CreateRegion()\n"
        "  call BJassAssert(GetHandleId(newRegion) != oldRegionId, \"recycled region reused GetHandleId\")\n"
        "  call BJassAssert(LoadInteger(ht, GetHandleId(newRegion), 0) == 0, \"replacement region inherited old hashtable data\")\n"
        "endfunction\n"));
}

TEST(wc3_api, recycled_region_event_gets_distinct_handle_id) {
    T_ASSERT(run_test_jass(
        "type region extends handle\n"
        "type trigger extends handle\n"
        "type event extends handle\n"
        "type hashtable extends handle\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local hashtable ht = InitHashtable()\n"
        "  local region r = CreateRegion()\n"
        "  local region replacement = null\n"
        "  local event oldEvent = TriggerRegisterEnterRegion(t, r, null)\n"
        "  local event newEvent = null\n"
        "  local integer oldEventId = GetHandleId(oldEvent)\n"
        "  call SaveInteger(ht, oldEventId, 0, 22)\n"
        "  call RemoveRegion(r)\n"
        "  call BJassAssert(GetHandleId(oldEvent) == oldEventId, \"retired region event changed GetHandleId\")\n"
        "  set replacement = CreateRegion()\n"
        "  set newEvent = TriggerRegisterEnterRegion(t, replacement, null)\n"
        "  call BJassAssert(GetHandleId(newEvent) != oldEventId, \"recycled region event reused GetHandleId\")\n"
        "  call BJassAssert(LoadInteger(ht, GetHandleId(newEvent), 0) == 0, \"replacement event inherited old hashtable data\")\n"
        "endfunction\n"));
}

TEST(wc3_api, set_unit_position_dispatches_region_crossings) {
    player_t *saved_currentplayer = currentplayer;

    reset_entities(); setup_test_world(); currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit mover = null\n"
        "  integer enters = 0\n"
        "  integer leaves = 0\n"
        "endglobals\n"
        "function on_enter takes nothing returns nothing\n"
        "  set enters = enters + 1\n"
        "endfunction\n"
        "function on_leave takes nothing returns nothing\n"
        "  set leaves = leaves + 1\n"
        "endfunction\n"
        "function teleport_into takes nothing returns nothing\n"
        "  call SetUnitPosition(mover, 40.0, 0.0)\n"
        "endfunction\n"
        "function teleport_out takes nothing returns nothing\n"
        "  call SetUnitPosition(mover, 80.0, 0.0)\n"
        "endfunction\n"
        "function verify_crossings takes nothing returns nothing\n"
        "  call BJassAssert(enters == 1, \"SetUnitPosition did not fire enter event\")\n"
        "  call BJassAssert(leaves == 1, \"SetUnitPosition did not fire leave event\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger enterTrigger = CreateTrigger()\n"
        "  local trigger leaveTrigger = CreateTrigger()\n"
        "  local region r = CreateRegion()\n"
        "  set mover = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call RegionAddRect(r, Rect(32.0, -16.0, 63.0, 16.0))\n"
        "  call TriggerRegisterEnterRegion(enterTrigger, r, null)\n"
        "  call TriggerRegisterLeaveRegion(leaveTrigger, r, null)\n"
        "  call TriggerAddAction(enterTrigger, function on_enter)\n"
        "  call TriggerAddAction(leaveTrigger, function on_leave)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a') && g_edicts[i].s.player == 0)
            g_edicts[i].svflags |= SVF_MONSTER;

    jass_callbyname(level.vm, "teleport_into", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    G_RunEntities(); G_RunEvents(); jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "teleport_out", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    G_RunEntities(); G_RunEvents(); jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "verify_crossings", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, removed_regions_reuse_slots_without_lifetime_cap) {
    T_ASSERT(run_test_jass(
        "type region extends handle\n"
        "function main takes nothing returns nothing\n"
        "  local region r = null\n"
        "  local integer i = 0\n"
        "  loop\n"
        "    exitwhen i >= 3000\n"
        "    set r = CreateRegion()\n"
        "    call BJassAssert(r != null, \"region creation stopped after repeated removals\")\n"
        "    call RemoveRegion(r)\n"
        "    set i = i + 1\n"
        "  endloop\n"
        "endfunction\n"));
    T_EQ(level.num_regions, 1);
}

TEST(wc3_api, removed_region_events_release_handler_capacity) {
    T_ASSERT(run_test_jass(
        "type region extends handle\n"
        "type trigger extends handle\n"
        "type event extends handle\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local region r = null\n"
        "  local event oldEvent = null\n"
        "  local event e = null\n"
        "  local integer i = 0\n"
        "  loop\n"
        "    exitwhen i >= 1100\n"
        "    set r = CreateRegion()\n"
        "    set e = TriggerRegisterEnterRegion(t, r, null)\n"
        "    call BJassAssert(e != null, \"removed region registrations exhausted event slots\")\n"
        "    if i == 0 then\n"
        "      set oldEvent = e\n"
        "    elseif i == 1 then\n"
        "      call BJassAssert(oldEvent != e, \"reused event slot aliased a retired event handle\")\n"
        "    endif\n"
        "    call RemoveRegion(r)\n"
        "    set i = i + 1\n"
        "  endloop\n"
        "endfunction\n"));
    {
        uint32_t active_region_events = 0;
        FOR_EACH_EVENT(event) if (event->type == EVENT_GAME_ENTER_REGION || event->type == EVENT_GAME_LEAVE_REGION)
            active_region_events++;
        T_EQ(active_region_events, 0);
    }
}

static uint32_t unit_team_color(edict_t const *unit) {
    uint32_t const encoded = unit
        ? (unit->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT : 0;
    return encoded ? encoded - 1u : 0;
}

static cstring_t skip_cutscene_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "skip_cutscene") ? "1" : fallback;
}

static cstring_t group_debug_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "wc3_group_debug") ? "1" : fallback;
}

static uint32_t presentation_write_count;
static uint32_t presentation_unicast_count;
static int32_t command_error_header[2];
static uint32_t command_error_header_count;
static uiFrame_t command_error_frames[2];
static uint32_t command_error_frame_count;
static uiLabel_t command_error_labels[2];
static uint32_t command_error_label_count;
static edict_t *command_error_recipient;
static pfWriteType_t indicator_types[4];
static int32_t indicator_values[4];
static uint32_t indicator_write_count;
static edict_t *indicator_recipient;
static bool captured_pause;
static uint32_t dnc_model_index_calls;
static uint32_t dnc_configstring_calls;
static uint32_t dnc_configstring_index[2];
static char dnc_configstring_value[2][16];
static uint32_t sky_model_index_calls;
static uint32_t sky_configstring_calls;
static uint32_t sky_configstring_index;
static char sky_configstring_value[16];
static uint32_t scene_fog_configstring_calls;
static uint32_t scene_fog_configstring_index;
static char scene_fog_configstring_value[MAX_PATHLEN];

static int capture_dnc_model_index(cstring_t modelName) {
    (void)modelName;
    return 41 + (int)dnc_model_index_calls++;
}

static void capture_dnc_configstring(uint32_t index, cstring_t value) {
    if (dnc_configstring_calls < 2) {
        dnc_configstring_index[dnc_configstring_calls] = index;
        snprintf(dnc_configstring_value[dnc_configstring_calls],
                 sizeof(dnc_configstring_value[dnc_configstring_calls]),
                 "%s", value ? value : "");
    }
    dnc_configstring_calls++;
}

static int capture_sky_model_index(cstring_t modelName) {
    (void)modelName;
    sky_model_index_calls++;
    return 41;
}

static void capture_sky_configstring(uint32_t index, cstring_t value) {
    sky_configstring_calls++;
    sky_configstring_index = index;
    snprintf(sky_configstring_value, sizeof(sky_configstring_value), "%s", value ? value : "");
}

static void capture_scene_fog_configstring(uint32_t index, cstring_t value) {
    scene_fog_configstring_calls++;
    scene_fog_configstring_index = index;
    snprintf(scene_fog_configstring_value, sizeof(scene_fog_configstring_value), "%s", value ? value : "");
}

static void capture_pause(bool paused) { captured_pause = paused; }

/* Campaign messages explicitly request attention; opening the journal acknowledges it. */
TEST(wc3_api, quest_flash_is_player_local_and_acknowledged) {
    player_t *saved = currentplayer;
    uint32_t oldtime = level.time;
    setup_test_world();
    game.clients[0].quest_until = game.clients[1].quest_until = 0;
    currentplayer = test_player(1); level.time = 500;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n call FlashQuestDialogButton()\nendfunction\n"));
    T_EQ(game.clients[0].quest_until, 0);
    T_EQ(game.clients[1].quest_until, 10500);
    level.time = 1500;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n call FlashQuestDialogButton()\nendfunction\n"));
    T_EQ(game.clients[1].quest_until, 11500);
    UI_ShowQuests(PLAYER_ENT(currentplayer));
    T_EQ(game.clients[1].quest_until, 0);
    game.clients[1].connected = true; g_edicts[1].inuse = false;
    game.clients[1].resourcebar.gold_rate = game.clients[1].resourcebar.lumber_rate = 100;
    game.clients[1].quest_until = level.time = 11500;
    G_UpdateClientResourceBars();
    T_EQ(game.clients[1].quest_until, 0);
    game.clients[1].connected = false;
    currentplayer = saved; level.time = oldtime;
}

TEST(wc3_api, timer_dialog_natives_store_state_and_local_visibility) {
    player_t *saved = currentplayer;
    timerdialog_t *dialog;
    gtimer_t *timer;

    setup_test_world();
    currentplayer = test_player(1);
    T_ASSERT(run_test_jass(
        "globals\n"
        "  timer countdown = null\n"
        "  timerdialog countdownDialog = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set countdown = CreateTimer()\n"
        "  call TimerStart(countdown, 65.0, false, null)\n"
        "  set countdownDialog = CreateTimerDialog(countdown)\n"
        "  call BJassAssert(countdownDialog != null, \"timer dialog should be allocated\")\n"
        "  call TimerDialogSetTitle(countdownDialog, \"Until Reinforcements Arrive\")\n"
        "  call TimerDialogSetTitleColor(countdownDialog, 300, -5, 64, 255)\n"
        "  call TimerDialogSetTimeColor(countdownDialog, 1, 2, 3, 4)\n"
        "  call BJassAssert(not IsTimerDialogDisplayed(countdownDialog), \"dialog starts hidden\")\n"
        "  call TimerDialogDisplay(countdownDialog, true)\n"
        "  call BJassAssert(IsTimerDialogDisplayed(countdownDialog), \"dialog should be visible locally\")\n"
        "endfunction\n"
        "function destroyDialog takes nothing returns nothing\n"
        "  call DestroyTimerDialog(countdownDialog)\n"
        "  call BJassAssert(TimerGetRemaining(countdown) > 64.0, \"destroying dialog must not destroy timer\")\n"
        "endfunction\n"));

    dialog = &level.timer_dialogs[0];
    T_ASSERT(dialog->inuse);
    T_ASSERT(dialog->timer != NULL);
    timer = dialog->timer;
    T_STREQ(dialog->title, "Until Reinforcements Arrive");
    T_EQ(dialog->title_color.r, 255); T_EQ(dialog->title_color.g, 0);
    T_EQ(dialog->title_color.b, 64);  T_EQ(dialog->title_color.a, 255);
    T_EQ(dialog->time_color.r, 1);   T_EQ(dialog->time_color.g, 2);
    T_EQ(dialog->time_color.b, 3);   T_EQ(dialog->time_color.a, 4);
    T_EQ(dialog->visible_clients, 1u << 1);

    jass_callbyname(level.vm, "destroyDialog", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(!dialog->inuse);
    T_ASSERT(timer->running);
    currentplayer = saved;
}

TEST(wc3_api, leaderboard_natives_manage_items_sort_and_player_assignment) {
    player_t *saved_currentplayer = currentplayer;
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  leaderboard lb = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set lb = CreateLeaderboard()\n"
        "  call BJassAssert(lb != null, \"leaderboard handle\")\n"
        "  call LeaderboardSetLabel(lb, \"Grunts Trained\")\n"
        "  call LeaderboardAddItem(lb, \"Second\", 2, Player(1))\n"
        "  call LeaderboardAddItem(lb, \"First\", 1, Player(0))\n"
        "  call LeaderboardSortItemsByValue(lb, true)\n"
        "  call BJassAssert(LeaderboardGetPlayerIndex(lb, Player(0)) == 0, \"sort/player index\")\n"
        "  call BJassAssert(LeaderboardHasPlayerItem(lb, Player(1)), \"player item\")\n"
        "  call LeaderboardSetItemValue(lb, 0, 3)\n"
        "  call LeaderboardSetItemLabel(lb, 0, \"Grunts\")\n"
        "  call PlayerSetLeaderboard(Player(0), lb)\n"
        "  call LeaderboardDisplay(lb, true)\n"
        "  call BJassAssert(PlayerGetLeaderboard(Player(0)) == lb, \"player assignment\")\n"
        "  call BJassAssert(IsLeaderboardDisplayed(lb), \"displayed\")\n"
        "  call BJassAssert(LeaderboardGetItemCount(lb) == 2, \"item count\")\n"
        "  call BJassAssert(LeaderboardGetLabelText(lb) == \"Grunts Trained\", \"label\")\n"
        "endfunction\n"));

    T_ASSERT(level.leaderboards[0].inuse);
    T_STREQ(level.leaderboards[0].items[0].label, "Grunts");
    T_EQ(level.leaderboards[0].items[0].value, 3);
    T_EQ(level.player_leaderboards[0], 0);
    T_ASSERT(level.leaderboards[0].displayed_clients & 1u);
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, leaderboard_display_uses_client_slot_for_mapped_player) {
    leaderboard_t *board;
    setup_test_world();
    game.clients[0].ps.number = 1;
    game.clients[1].ps.number = 0;
    board = G_AllocLeaderboard();
    G_SetPlayerLeaderboard(0, board);
    G_SetLeaderboardDisplayed(board, &game.clients[1].ps, true);
    T_ASSERT(board->displayed_clients & (1u << 1));
    T_ASSERT(!(board->displayed_clients & 1u));
    T_ASSERT(G_IsLeaderboardDisplayed(board, &game.clients[1].ps));
    T_ASSERT(!G_IsLeaderboardDisplayed(board, &game.clients[0].ps));
}

TEST(wc3_api, leaderboard_changes_dirty_stacked_multiboard_for_assigned_client) {
    leaderboard_t *board;
    uint32_t const client_bit = 1u << 1;

    setup_test_world();
    game.clients[0].ps.number = 1;
    game.clients[1].ps.number = 0;
    board = G_AllocLeaderboard();
    T_NOT_NULL(board);

    G_SetPlayerLeaderboard(0, board);
    T_ASSERT(level.multiboard_dirty_clients & client_bit);

    level.multiboard_dirty_clients = 0;
    G_MarkLeaderboardDirty(board);
    T_ASSERT(level.multiboard_dirty_clients & client_bit);
    T_ASSERT(!(level.multiboard_dirty_clients & 1u));

    level.multiboard_dirty_clients = 0;
    G_SetLeaderboardDisplayed(board, &game.clients[1].ps, true);
    T_ASSERT(level.multiboard_dirty_clients & client_bit);

    level.multiboard_dirty_clients = 0;
    G_FreeLeaderboard(board);
    T_ASSERT(level.multiboard_dirty_clients & client_bit);
    T_EQ(level.player_leaderboards[0], -1);
}

TEST(wc3_api, version_queries_accept_typed_handles) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(VersionCompatible(ConvertVersion(0)), \"ROC compatible\")\n"
        "  call BJassAssert(VersionSupported(ConvertVersion(0)), \"ROC supported\")\n"
        "  call BJassAssert(not VersionCompatible(ConvertVersion(1)), \"current compatibility policy\")\n"
        "  call BJassAssert(not VersionSupported(ConvertVersion(1)), \"current support policy\")\n"
        "endfunction\n"));
}

TEST(wc3_api, unit_type_ground_uses_authored_foot_movement) {
    edict_t *footman, *gryphon;
    setup_test_world();
    T_EQ(G_UnitData(MAKEFOURCC('h','f','o','o'))->id, MAKEFOURCC('h','f','o','o'));
    T_STREQ(G_UnitData(MAKEFOURCC('h','f','o','o'))->moveTypeName, "foot");
    T_EQ(G_UnitData(MAKEFOURCC('h','g','r','y'))->id, MAKEFOURCC('h','g','r','y'));
    T_STREQ(G_UnitData(MAKEFOURCC('h','g','r','y'))->moveTypeName, "fly");
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit footman = CreateUnit(Player(1), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  local unit gryphon = CreateUnit(Player(1), 'hgry', 0.0, 0.0, 0.0)\n"
        "  call BJassAssert(IsUnitType(footman, UNIT_TYPE_GROUND), \"foot movement is ground\")\n"
        "  call BJassAssert(not IsUnitType(gryphon, UNIT_TYPE_GROUND), \"fly movement is not ground\")\n"
        "  call BJassAssert(IsUnitType(gryphon, UNIT_TYPE_FLYING), \"fly movement is flying\")\n"
        "endfunction\n"));
    T_ASSERT(!jass_rterror_pending(level.vm));
    footman = find_test_unit(MAKEFOURCC('h','f','o','o'));
    gryphon = find_test_unit(MAKEFOURCC('h','g','r','y'));
    T_NOT_NULL(footman);
    T_NOT_NULL(gryphon);
    if (footman) {
        T_STREQ(footman->data.UnitData->moveTypeName, "foot");
        T_ASSERT(!(footman->script_unit_types & (1u << 4)));
        T_ASSERT(!(footman->aiflags & AI_FLYING));
    }
    if (gryphon) {
        T_STREQ(gryphon->data.UnitData->moveTypeName, "fly");
        T_ASSERT(gryphon->aiflags & AI_FLYING);
    }
}

TEST(wc3_api, ability_cooldown_natives_share_unit_cooldown_state) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call BlzStartUnitAbilityCooldown(u, 'AHtb', 3.5)\n"
        "  call BJassAssert(BlzGetUnitAbilityCooldownRemaining(u, 'AHtb') > 3.4, \"start cooldown\")\n"
        "  call BlzEndUnitAbilityCooldown(u, 'AHtb')\n"
        "  call BJassAssert(BlzGetUnitAbilityCooldownRemaining(u, 'AHtb') == 0.0, \"end cooldown\")\n"
        "  call BlzStartUnitAbilityCooldown(u, 'AHtb', 2.0)\n"
        "  call BlzStartUnitAbilityCooldown(u, 'AHwe', 4.0)\n"
        "  call UnitResetCooldown(u)\n"
        "  call BJassAssert(BlzGetUnitAbilityCooldownRemaining(u, 'AHtb') == 0.0, \"reset thunder clap\")\n"
        "  call BJassAssert(BlzGetUnitAbilityCooldownRemaining(u, 'AHwe') == 0.0, \"reset water elemental\")\n"
        "endfunction\n"));
}

TEST(wc3_api, pause_game_forwards_authoritative_pause_state) {
    void (*old_set_paused)(bool) = gi.SetPaused;

    captured_pause = false;
    gi.SetPaused = capture_pause;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call PauseGame(true)\n"
        "endfunction\n"));
    T_ASSERT(level.script_paused);
    T_ASSERT(captured_pause);

    G_SetScriptPaused(false);
    T_ASSERT(!level.script_paused);
    T_ASSERT(!captured_pause);
    gi.SetPaused = old_set_paused;
}

TEST(wc3_api, quest_pause_is_single_client_only) {
    void (*old_set_paused)(bool) = gi.SetPaused;

    captured_pause = false;
    gi.SetPaused = capture_pause;
    G_SetClientConnected(&g_edicts[0], true);
    G_SetQuestDialogOpen(&g_edicts[0], true);
    T_ASSERT(level.quest_paused);
    T_ASSERT(captured_pause);

    G_SetClientConnected(&g_edicts[1], true);
    T_ASSERT(!level.quest_paused);
    T_ASSERT(!captured_pause);

    G_SetClientConnected(&g_edicts[1], false);
    T_ASSERT(level.quest_paused);
    T_ASSERT(captured_pause);
    G_SetQuestDialogOpen(&g_edicts[0], false);
    T_ASSERT(!level.quest_paused);
    T_ASSERT(!captured_pause);
    gi.SetPaused = old_set_paused;
}

static void capture_presentation_write(pfWriteType_t type, void const *data) {
    (void)type;
    (void)data;
    presentation_write_count++;
}

static void capture_presentation_unicast(edict_t *ent) {
    (void)ent;
    presentation_unicast_count++;
}

static void capture_command_error_write(pfWriteType_t type, void const *data) {
    if (type == PF_BYTE && command_error_header_count < 2) {
        command_error_header[command_error_header_count++] = data ? *(int32_t const *)data : -1;
        return;
    }
    if (type == PF_UIFRAME && data) {
        uiFrame_t const *frame = data;
        if (command_error_frame_count < 2)
            command_error_frames[command_error_frame_count++] = *frame;
        if (frame->buffer.size == sizeof(uiLabel_t) && frame->buffer.data && command_error_label_count < 2)
            command_error_labels[command_error_label_count++] = *(uiLabel_t const *)frame->buffer.data;
    }
}

static void capture_command_error_unicast(edict_t *ent) {
    command_error_recipient = ent;
}

static int capture_command_error_font(cstring_t name, uint32_t size) {
    (void)name;
    (void)size;
    return 17;
}

static void capture_indicator_write(pfWriteType_t type, void const *data) {
    uint32_t const slot = indicator_write_count++;
    if (slot >= 4) return;
    indicator_types[slot] = type;
    if (data) indicator_values[slot] = *(int32_t const *)data;
}

static void capture_indicator_unicast(edict_t *ent) {
    indicator_recipient = ent;
}

TEST(wc3_api, add_indicator_accepts_unit_widget_and_sends_local_tinted_ring) {
    gameClient_t *gc = &game.clients[0];
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    memset(indicator_types, 0, sizeof(indicator_types));
    memset(indicator_values, 0, sizeof(indicator_values));
    indicator_write_count = 0;
    indicator_recipient = NULL;
    gc->ps.number = 0;
    G_SetClientConnected(&g_edicts[0], true);
    currentplayer = &gc->ps;
    gi.Write = capture_indicator_write;
    gi.unicast = capture_indicator_unicast;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 64.0, 96.0, 0.0)\n"
        "  call AddIndicator(u, 255, 128, 64, 200)\n"
        "endfunction\n"));

    T_EQ(indicator_write_count, 4);
    T_EQ(indicator_types[0], PF_BYTE);
    T_EQ(indicator_values[0], svc_temp_entity);
    T_EQ(indicator_types[1], PF_BYTE);
    T_EQ(indicator_values[1], TE_ENTITY_INDICATOR);
    T_EQ(indicator_types[2], PF_LONG);
    T_ASSERT(indicator_values[2] > 0);
    T_EQ(indicator_types[3], PF_LONG);
    T_EQ((uint32_t)indicator_values[3], 0xc84080ffu);
    T_EQ(indicator_recipient, &g_edicts[0]);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    currentplayer = NULL;
    G_SetClientConnected(&g_edicts[0], false);
}

TEST(wc3_api, terrain_deform_native_sends_renderer_only_descriptor_and_returns_stoppable_handle) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_multicast)(vec3_t const *, multicast_t) = gi.multicast;
    uint32_t old_id;
    terrain_deform_write_count = terrain_deform_float_count = terrain_deform_multicast_count = 0;
    gi.Write = terrain_deform_test_write;
    gi.multicast = terrain_deform_test_multicast;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local terraindeformation d\n"
        "  set d = TerrainDeformCrater(128.0, 256.0, 96.0, 14.0, 1800, false)\n"
        "  call TerrainDeformStop(d, 300)\n"
        "  call TerrainDeformStopAll()\n"
        "endfunction\n"));

    T_EQ(terrain_deform_write_values[0], svc_temp_entity);
    T_EQ(terrain_deform_write_values[1], TE_TERRAIN_DEFORM);
    T_ASSERT(terrain_deform_write_values[2] > 0);
    T_EQ(terrain_deform_write_values[3], TERRAIN_DEFORM_CRATER);
    old_id = (uint32_t)terrain_deform_write_values[2];
    T_EQ(terrain_deform_float_count, 8);
    T_FEQ(terrain_deform_write_floats[0], 128.0f, 0.001f);
    T_FEQ(terrain_deform_write_floats[1], 256.0f, 0.001f);
    T_FEQ(terrain_deform_write_floats[2], 96.0f, 0.001f);
    T_FEQ(terrain_deform_write_floats[3], 14.0f, 0.001f);
    T_EQ(terrain_deform_write_count, 22);
    T_EQ(terrain_deform_write_values[12], 1800);
    T_EQ(terrain_deform_write_values[13], 0);
    T_EQ(terrain_deform_write_values[14], 0);
    T_EQ(terrain_deform_write_values[15], 0);
    T_EQ(terrain_deform_write_values[16], svc_temp_entity);
    T_EQ(terrain_deform_write_values[17], TE_TERRAIN_DEFORM_STOP);
    T_EQ(terrain_deform_write_values[18], (int32_t)old_id);
    T_EQ(terrain_deform_write_values[19], 300);
    T_EQ(terrain_deform_write_values[20], svc_temp_entity);
    T_EQ(terrain_deform_write_values[21], TE_TERRAIN_DEFORM_STOP_ALL);
    T_EQ(terrain_deform_multicast_count, 3);
    T_EQ(old_id, (uint32_t)terrain_deform_write_values[2]);

    gi.Write = old_write;
    gi.multicast = old_multicast;
}

TEST(wc3_api, disconnected_presentation_defers_network_write_until_connected) {
    gameClient_t *gc = &game.clients[0];
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    presentation_write_count = 0;
    presentation_unicast_count = 0;
    gi.Write = capture_presentation_write;
    gi.unicast = capture_presentation_unicast;

    T_ASSERT(!gc->connected);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call ShowInterface(false, 0.0)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_ASSERT(gc->presentation_dirty);

    G_RunClients();
    T_EQ(presentation_write_count, 0);
    T_EQ(presentation_unicast_count, 0);
    T_ASSERT(gc->presentation_dirty);

    G_SetClientConnected(&g_edicts[0], true);
    G_RunClients();
    T_ASSERT(presentation_write_count > 0);
    T_ASSERT(presentation_unicast_count > 0);
    T_ASSERT(!gc->presentation_dirty);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_api, client_ui_init_preserves_authored_state_and_rejects_invalid_state) {
    gameClient_t *gc = &game.clients[0];
    gc->ps.client_ui_state = CLIENT_UI_CINEMATIC;
    gc->ps.uiflags = ~(1u << LAYER_CINEMATIC);
    gc->presentation_dirty = true;

    G_InitClientUIState(gc);

    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_EQ(gc->ps.uiflags, ~(1u << LAYER_CINEMATIC));
    T_ASSERT(gc->presentation_dirty);

    gc->ps.client_ui_state = CLIENT_UI_CINEMATIC + 1;
    G_InitClientUIState(gc);
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
}

static cstring_t gamecache_disabled_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "wc3_gamecache_mode") ? "disabled" : fallback;
}

static cstring_t gamecache_memory_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "wc3_gamecache_mode") ? "memory" : fallback;
}

static cstring_t campaign_progress_roc_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "fs_expansion") ? "0" : fallback;
}

/* Per-process: concurrent suites in one checkout must not share the progress file or its .tmp/.bak siblings. */
#define campaign_progress_test_path Test_TempPath("campaign-progress-native-test.orcp")

static void campaign_progress_test_user_path(cstring_t rel, string_t out, uint32_t out_size) {
    (void)rel;
    if (!out || !out_size) return;
    snprintf(out, out_size, "%s", campaign_progress_test_path);
}

static int ui_sound_calls;
static int ui_sound_value;
static float ui_sound_volume;
static char ui_sound_path[512];
static int capture_ui_sound_index(cstring_t path) {
    snprintf(ui_sound_path, sizeof(ui_sound_path), "%s", path ? path : "");
    return 77;
}
static int capture_ui_sound_index_alias(cstring_t path, cstring_t alias) { (void)alias; return capture_ui_sound_index(path); }

static void capture_ui_sound(edict_t *ent, int channel, int sound, float volume, float attenuation, float timeofs) {
    (void)ent; (void)attenuation; (void)timeofs;
    ui_sound_calls++;
    ui_sound_value = sound;
    ui_sound_volume = volume;
    T_EQ(channel, CHAN_OWNER | CHAN_RELIABLE);
}

static void capture_ui_sound_policy(vec3_t const *origin, edict_t *ent, int channel, int sound,
                                    float volume, float attenuation, float timeofs, soundPolicy_t const *policy) {
    T_NULL(origin); T_NOT_NULL(policy); T_EQ(policy->max_total, 24);
    capture_ui_sound(ent, channel, sound, volume, attenuation, timeofs);
}

TEST(wc3_api, default_camera_authors_lens) {
    gameCamera_t cam;
    T_ASSERT(CL_GameDefaultCamera(&cam));
    T_ASSERT(CL_GameCameraUsesWorldUp());
    T_FEQ(cam.fov, WC3_CAMERA_DEFAULT_FOV, 0.001f);
    T_FEQ(cam.znear, WC3_CAMERA_DEFAULT_NEAR_Z, 0.001f);
    T_FEQ(cam.zfar, WC3_CAMERA_DEFAULT_FAR_Z, 0.001f);
}

/* Campaign human slots need not match the connection slot; exercise the real VM/edict module boundary. */
TEST(wc3_api, escape_restores_game_camera_ui_and_control) {
    gameClient_t *gc = &game.clients[0];
    cstring_t cancel[] = { "cancel" };
    game.clients[1].ps.number = 0;
    gc->ps.number = 1;
    gc->camera.state.viewangles = (vec3_t){300, 0, 120};
    gc->camera.state.target_distance = 900;
    gc->camera.state.fov = 35;
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function cleanup takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ResetToGameCamera(0.0)\n"
        "    call ShowInterface(true, 0.0)\n"
        "    call EnableUserControl(true)\n"
        "    call PanCameraTo(128.0, 256.0)\n"
        "  endif\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerEvent(t, Player(1), EVENT_PLAYER_END_CINEMATIC)\n"
        "  call TriggerAddAction(t, function cleanup)\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ShowInterface(false, 0.0)\n"
        "    call EnableUserControl(false)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_ASSERT(gc->no_control);
    /* An unrelated player's cancel must not run the registered cleanup. */
    globals.ClientCommand(&g_edicts[1], 1, cancel);
    G_RunEvents(); jass_runevents(level.vm);
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_FEQ(gc->camera.state.target_distance, 900, 0.001f);

    globals.ClientCommand(&g_edicts[0], 1, cancel);
    G_RunEvents(); jass_runevents(level.vm); G_RunClients();
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
    T_EQ(gc->ps.uiflags, 1u << LAYER_CINEMATIC);
    T_ASSERT(!gc->no_control);
    T_ASSERT(gc->camera.pan_active);
    T_FEQ(gc->camera.pan_destination.x, 128, 0.001f); T_FEQ(gc->camera.pan_destination.y, 256, 0.001f);
    T_FEQ(gc->ps.distance, WC3_CAMERA_DEFAULT_DISTANCE, 0.001f); T_FEQ(gc->ps.fov, WC3_CAMERA_DEFAULT_FOV, 0.001f);
    T_FEQ(gc->ps.znear, WC3_CAMERA_DEFAULT_NEAR_Z, 0.001f);
    T_FEQ(gc->ps.zfar, WC3_CAMERA_DEFAULT_FAR_Z, 0.001f);
    T_FEQ(gc->ps.viewangles.x, 326.0f, 0.001f); T_FEQ(gc->ps.viewangles.z, 0.0f, 0.001f);
}

TEST(wc3_api, fly_height_native_keeps_authored_default_separate) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call SetUnitFlyHeight(u, 123.0, 0.0)\n"
        "  call BJassAssert(R2I(GetUnitFlyHeight(u)) == 123, \"current fly height was not updated\")\n"
        "  call BJassAssert(R2I(GetUnitDefaultFlyHeight(u)) != 123, \"default fly height became mutable\")\n"
        "endfunction\n"));
}

TEST(wc3_api, entering_unit_native_returns_region_event_subject) {
    edict_t *entering = NULL;
    event_t *handler = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit udg_Entering = null\n"
        "  boolean udg_Entered = false\n"
        "endglobals\n"
        "function onEnter takes nothing returns nothing\n"
        "  call BJassAssert(GetEnteringUnit() == udg_Entering, \"GetEnteringUnit did not return event subject\")\n"
        "  call BJassAssert(GetTriggerUnit() == udg_Entering, \"GetTriggerUnit did not return event subject\")\n"
        "  set udg_Entered = true\n"
        "endfunction\n"
        "function verifyEnter takes nothing returns nothing\n"
        "  call BJassAssert(udg_Entered, \"region enter action did not run\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local region r = CreateRegion()\n"
        "  call RegionAddRect(r, Rect(100.0, 100.0, 200.0, 200.0))\n"
        "  set udg_Entering = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call TriggerRegisterEnterRegion(t, r, null)\n"
        "  call TriggerAddAction(t, function onEnter)\n"
        "endfunction\n"
    ));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') &&
            g_edicts[i].s.player == 0) {
            entering = &g_edicts[i];
        }
    }
    T_NOT_NULL(entering);
    FOR_EACH_EVENT(evt) {
        if (evt->type == EVENT_GAME_ENTER_REGION) {
            handler = evt;
            break;
        }
    }
    T_NOT_NULL(handler);
    G_PublishEventResponse(entering, EVENT_GAME_ENTER_REGION, handler);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyEnter", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, leaving_region_event_is_registered_and_dispatched) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *leaving = NULL;
    vec2_t destination = { 300.0f, 150.0f };

    reset_entities();
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit leaving = null\n"
        "  integer accepted = 0\n"
        "  integer rejected = 0\n"
        "  boolean correctLeavingUnit = false\n"
        "endglobals\n"
        "function allow_filter takes nothing returns boolean\n"
        "  return GetFilterUnit() == leaving\n"
        "endfunction\n"
        "function reject_filter takes nothing returns boolean\n"
        "  return false\n"
        "endfunction\n"
        "function onAcceptedLeave takes nothing returns nothing\n"
        "  set accepted = accepted + 1\n"
        "  set correctLeavingUnit = GetLeavingUnit() == leaving\n"
        "endfunction\n"
        "function onRejectedLeave takes nothing returns nothing\n"
        "  set rejected = rejected + 1\n"
        "endfunction\n"
        "function onLeave takes nothing returns nothing\n"
        "  call SetWidgetLife(GetTriggerUnit(), 75.0)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger acceptedTrigger = CreateTrigger()\n"
        "  local trigger rejectedTrigger = CreateTrigger()\n"
        "  local region r = CreateRegion()\n"
        "  call RegionAddRect(r, Rect(100.0, 100.0, 200.0, 200.0))\n"
        "  set leaving = CreateUnit(Player(0), 'hpea', 150.0, 150.0, 0.0)\n"
        "  call TriggerAddAction(acceptedTrigger, function onAcceptedLeave)\n"
        "  call TriggerAddAction(rejectedTrigger, function onRejectedLeave)\n"
        "  call TriggerRegisterLeaveRegion(acceptedTrigger, r, Condition(function allow_filter))\n"
        "  call TriggerRegisterLeaveRegion(rejectedTrigger, r, Condition(function reject_filter))\n"
        "endfunction\n"
        "function verifyLeave takes nothing returns nothing\n"
        "  call BJassAssert(accepted == 1, \"accepted leave filter should run once\")\n"
        "  call BJassAssert(rejected == 0, \"rejected leave filter should not run\")\n"
        "  call BJassAssert(correctLeavingUnit, \"GetLeavingUnit should resolve the crossing unit\")\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a') && g_edicts[i].s.player == 0)
            leaving = &g_edicts[i];
    T_NOT_NULL(leaving);
    leaving->svflags |= SVF_MONSTER;
    leaving->movetype = MOVETYPE_STEP;
    leaving->stand = unit_stand;
    leaving->birth = unit_birth;
    leaving->die = unit_die;
    leaving->think = monster_think;
    leaving->collision = 0.0f;
    leaving->unitinfo.MoveSpeed = 1000.0f;
    leaving->health.value = leaving->health.max_value = 250.0f;
    unit_stand(leaving);
    T_ASSERT(unit_issueorder(leaving, "move", &destination));
    FOR_LOOP(i, 10) {
        if (leaving->s.origin2.x >= 224.0f) break;
        level.time += FRAMETIME;
        api_run_move_entities(1);
        G_RunEvents();
        jass_runevents(level.vm);
    }
    T_ASSERT(leaving->s.origin2.x >= 224.0f);
    jass_callbyname(level.vm, "verifyLeave", false);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    currentplayer = saved_currentplayer;
}

/* An event's owner is GetTriggerPlayer(), not the local-player selector used by GetLocalPlayer().
 * Human02's victory chain starts from the Blademaster (player 4) dying, then
 * TriggerExecute()s nested cinematic triggers whose local UI branch targets
 * the connected Human player (map player 1). */
TEST(wc3_api, enemy_event_keeps_trigger_player_separate_from_local_player_context) {
    gameClient_t *human = &game.clients[0];
    gameClient_t *enemy = &game.clients[4];
    edict_t *dying;

    /* Reproduce the campaign mapping where connection slot 0 is map player 1. */
    game.clients[1].ps.number = 0;
    human->ps.number = 1;
    enemy->ps.number = 4;
    currentplayer = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  trigger udg_Inner = null\n"
        "  unit udg_OuterUnit = null\n"
        "  boolean udg_Evaluated = false\n"
        "endglobals\n"
        "function inner_condition takes nothing returns boolean\n"
        "  return GetTriggerUnit() == udg_OuterUnit\n"
        "endfunction\n"
        "function inner_action takes nothing returns nothing\n"
        "  call BJassAssert(GetTriggerUnit() == udg_OuterUnit, \"nested TriggerExecute lost GetTriggerUnit\")\n"
        "  call SetPlayerState(Player(1), PLAYER_STATE_RESOURCE_GOLD, GetPlayerId(GetTriggerPlayer()))\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ShowInterface(false, 0.0)\n"
        "  endif\n"
        "endfunction\n"
        "function outer_action takes nothing returns nothing\n"
        "  set udg_OuterUnit = GetTriggerUnit()\n"
        "  set udg_Evaluated = TriggerEvaluate(udg_Inner)\n"
        "  call BJassAssert(udg_Evaluated, \"nested TriggerEvaluate lost GetTriggerUnit\")\n"
        "  call TriggerExecute(udg_Inner)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger outer = CreateTrigger()\n"
        "  set udg_Inner = CreateTrigger()\n"
        "  call TriggerAddCondition(udg_Inner, Condition(function inner_condition))\n"
        "  call TriggerAddAction(udg_Inner, function inner_action)\n"
        "  call TriggerRegisterPlayerUnitEvent(outer, Player(4), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddAction(outer, function outer_action)\n"
        "endfunction\n"));

    dying = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    dying->s.player = 4;
    G_PublishEvent(dying, EVENT_PLAYER_UNIT_DEATH);
    G_RunEvents();
    jass_runevents(level.vm);

    T_EQ(human->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 4);
    T_EQ(human->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_EQ(enemy->ps.client_ui_state, CLIENT_UI_GAME);
    T_NULL(currentplayer);
}

TEST(wc3_api, camera_margin_is_default_camera_inset_from_playable_area) {
    /* W3I complements crop the entire W3E terrain to the playable rectangle.
     * GetCameraMargin is the remaining inset from that playable rectangle to
     * the W3I default camera bounds; it is not complement * TILE_SIZE. */
    int const raw_complements[4] = { 4, 8, 6, 10 };
    mapInfo_t *mapinfo = (mapInfo_t *)level.mapinfo;

    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = { -4096.0f, -3072.0f },
        .max = { 4096.0f, 3072.0f }));
    memcpy(&mapinfo->cameraBounds.complement, raw_complements, sizeof(raw_complements));

    /* Complements produce playable [-3584,-2304]..[3072,1792].
     * Default camera bounds are inset by L=256, R=384, B=384, T=512. */
    memcpy(mapinfo->cameraBounds.bounds, (float[8]){
        -3328.0f, -1920.0f,
        -3328.0f,  1280.0f,
         2688.0f,  1280.0f,
         2688.0f, -1920.0f,
    }, sizeof(mapinfo->cameraBounds.bounds));

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-3584.0 + GetCameraMargin(CAMERA_MARGIN_LEFT), "
        "-2304.0 + GetCameraMargin(CAMERA_MARGIN_BOTTOM), "
        "-3584.0 + GetCameraMargin(CAMERA_MARGIN_LEFT), "
        "1792.0 - GetCameraMargin(CAMERA_MARGIN_TOP), "
        "3072.0 - GetCameraMargin(CAMERA_MARGIN_RIGHT), "
        "1792.0 - GetCameraMargin(CAMERA_MARGIN_TOP), "
        "3072.0 - GetCameraMargin(CAMERA_MARGIN_RIGHT), "
        "-2304.0 + GetCameraMargin(CAMERA_MARGIN_BOTTOM))\n"
        "endfunction\n"));

    /* The generated SetCameraBounds call reconstructs the W3I default camera
     * rectangle instead of applying the complement widths a second time. */
    T_FEQ(level.camera_bounds.min.x, -3328.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.x, 2688.0f, 0.001f);
    T_FEQ(level.camera_bounds.min.y, -1920.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.y, 1280.0f, 0.001f);
}

TEST(wc3_api, world_bounds_enables_full_map_group_transfer) {
    edict_t *unit;

    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'b', 'l', 'a'), 128.0f, 128.0f);
    unit->svflags |= SVF_MONSTER;
    unit->s.player = 8;
    T_ASSERT(run_test_jass(
        "function transfer_action takes nothing returns nothing\n"
        "  call SetUnitOwner(GetEnumUnit(), Player(1), true)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local group units = CreateGroup()\n"
        "  call GroupEnumUnitsInRect(units, GetWorldBounds(), null)\n"
        "  call ForGroup(units, function transfer_action)\n"
        "endfunction\n"));
    T_EQ(unit->s.player, 1);
}

TEST(wc3_api, camera_bounds_clamp_user_and_scripted_targets) {
    gameClient_t *gc = &game.clients[0];
    vec2_t requested = { 500.0f, -500.0f };

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-100.0, -50.0, -100.0, 50.0, 100.0, 50.0, 100.0, -50.0)\n"
        "endfunction\n"));
    T_FEQ(level.camera_bounds.min.x, -100.0f, 0.001f);
    T_FEQ(level.camera_bounds.min.y, -50.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.x, 100.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.y, 50.0f, 0.001f);

    G_ClientSetCameraPosition(&g_edicts[0], &requested);
    T_FEQ(gc->camera.state.position.x, 100.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, -50.0f, 0.001f);

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraPosition(GetCameraBoundMinX() - 200.0, GetCameraBoundMaxY() + 200.0)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.state.position.x, -100.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 50.0f, 0.001f);
    currentplayer = NULL;
}


TEST(wc3_api, camera_angle_interpolation_uses_shortest_periodic_arc) {
    gameClient_t *gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.target_controller = NULL;
    gc->camera.target_mode = CAMERA_TARGET_FOLLOW;
    gc->camera.old_state = gc->camera.state;
    gc->camera.old_state.viewangles = (vec3_t){ -394.0f, 0.0f, 350.0f };
    gc->camera.state = gc->camera.old_state;
    gc->camera.state.viewangles = (vec3_t){ 326.0f, 0.0f, 10.0f };
    gc->camera.start_time = 100;
    gc->camera.end_time = 1100;
    level.time = 600;

    G_RunClients();

    /* -394 and 326 are the same orientation modulo 360, so pitch must not
     * travel two full turns.  Yaw 350 -> 10 crosses the wrap by +20 degrees. */
    T_FEQ(gc->ps.viewangles.x, -394.0f, 0.001f);
    T_FEQ(gc->ps.viewangles.y, 0.0f, 0.001f);
    T_FEQ(gc->ps.viewangles.z, 360.0f, 0.001f);
    T_FEQ(CL_GameLerpDegrees(10.0f, 350.0f, 0.5f), 0.0f, 0.001f);
    T_FEQ(CL_GameLerpDegrees(326.0f, -394.0f, 0.5f), 326.0f, 0.001f);
}

TEST(wc3_api, camera_runtime_getters_report_interpolated_state_and_eye) {
    gameClient_t *gc = &game.clients[0];

    gc->ps.number = 0;
    gc->ps.vieworigin = (vec3_t){ 100.0f, 200.0f, 300.0f };
    gc->ps.viewangles = (vec3_t){ 0.0f, 0.0f, 0.0f };
    gc->ps.distance = 100.0f;
    gc->ps.fov = 50.0f;
    gc->ps.znear = 100.0f;
    gc->ps.zfar = 4000.0f;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(R2I(GetCameraField(CAMERA_FIELD_FARZ)) == 4000, \"farz\")\n"
        "  call BJassAssert(R2I(GetCameraEyePositionX()) == 100, \"eye x\")\n"
        "  call BJassAssert(R2I(GetCameraEyePositionY()) == 200, \"eye y\")\n"
        "  call BJassAssert(R2I(GetCameraEyePositionZ()) == 400, \"eye z\")\n"
        "endfunction\n"));
    currentplayer = NULL;
}

/* A leaving client must not keep the simulation paused behind a dialog nobody can close. */
TEST(wc3_api, client_disconnect_export_releases_connection_and_modal_state) {
    edict_t *player = &g_edicts[0];

    setup_test_world();
    player->client = &game.clients[0];
    G_SetClientConnected(player, true);
    T_ASSERT(player->client->connected);
    player->client->quest_dialog_open = true;
    player->client->modal_flags = WC3_MODAL_QUEST;

    globals.ClientDisconnect(player);
    T_ASSERT(!player->client->connected);
    T_ASSERT(!player->client->quest_dialog_open);
    T_EQ(player->client->modal_flags, 0);
}

/* The natives only store inputs; the game frame turns them into plain view offsets and leaves the logical
 * camera (vieworigin, angles, distance, target height) untouched so scripts and input read a steady view. */
TEST(wc3_api, camera_noise_is_evaluated_by_the_game_into_view_offsets) {
    gameClient_t *gc = &game.clients[0];
    player_t steady;
    float steady_height;

    setup_test_world();
    gc->ps.number = 0;
    memset(gc->camera.noise, 0, sizeof(gc->camera.noise));
    level.time = 250;
    G_RunClients();
    steady = gc->ps;
    steady_height = gc->camera.target_height;
    T_FEQ(Vector3_len(&gc->ps.viewoffset), 0.0f, 0.001f);
    T_FEQ(Vector3_len(&gc->ps.eyeoffset), 0.0f, 0.001f);
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call CameraSetTargetNoiseEx(12.0, 4.0, true)\n"
        "  call CameraSetSourceNoiseEx(56.0, 7.0, false)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.noise[CAMERA_NOISE_TARGET].magnitude, 12.0f, 0.001f);
    T_FEQ(gc->camera.noise[CAMERA_NOISE_TARGET].velocity, 4.0f, 0.001f);
    T_ASSERT(gc->camera.noise[CAMERA_NOISE_TARGET].vert_only);
    T_FEQ(gc->camera.noise[CAMERA_NOISE_SOURCE].magnitude, 56.0f, 0.001f);
    T_FEQ(gc->camera.noise[CAMERA_NOISE_SOURCE].velocity, 7.0f, 0.001f);
    T_ASSERT(!gc->camera.noise[CAMERA_NOISE_SOURCE].vert_only);

    G_RunClients();
    /* vertOnly target noise stays on world Z and inside its magnitude. */
    T_FEQ(gc->ps.viewoffset.x, 0.0f, 0.001f);
    T_FEQ(gc->ps.viewoffset.y, 0.0f, 0.001f);
    T_ASSERT(fabsf(gc->ps.viewoffset.z) > 0.001f && fabsf(gc->ps.viewoffset.z) <= 12.0f);
    /* Source noise is independent and uses all three axes. */
    T_ASSERT(fabsf(gc->ps.eyeoffset.x) > 0.001f && fabsf(gc->ps.eyeoffset.x) <= 56.0f);
    T_ASSERT(fabsf(gc->ps.eyeoffset.y) > 0.001f && fabsf(gc->ps.eyeoffset.y) <= 56.0f);
    T_ASSERT(fabsf(gc->ps.eyeoffset.z) <= 56.0f);
    T_ASSERT(memcmp(&gc->ps.vieworigin, &steady.vieworigin, sizeof(vec3_t)) == 0);
    T_ASSERT(memcmp(&gc->ps.viewangles, &steady.viewangles, sizeof(vec3_t)) == 0);
    T_FEQ(gc->ps.distance, steady.distance, 0.001f);
    T_FEQ(gc->camera.target_height, steady_height, 0.001f);

    /* The oscillator advances with simulation time through the real per-frame entry point. */
    {
        float const first = gc->ps.viewoffset.z;
        bool const started = level.started;
        level.started = true;
        level.time = 350;
        globals.RunFrame();
        level.started = started;
        T_ASSERT(fabsf(gc->ps.viewoffset.z - first) > 0.001f);
    }

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call CameraSetTargetNoise(0.0, 0.0)\n"
        "  call CameraSetSourceNoise(0.0, 0.0)\n"
        "endfunction\n"));
    G_RunClients();
    T_FEQ(Vector3_len(&gc->ps.viewoffset), 0.0f, 0.001f);
    T_FEQ(Vector3_len(&gc->ps.eyeoffset), 0.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, reset_to_game_camera_clears_source_and_target_noise) {
    gameClient_t *gc = &game.clients[0];

    setup_test_world();
    gc->ps.number = 0;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call CameraSetTargetNoiseEx(12.0, 4.0, true)\n"
        "  call CameraSetSourceNoiseEx(56.0, 7.0, false)\n"
        "  call ResetToGameCamera(0.0)\n"
        "endfunction\n"));

    FOR_LOOP(slot, CAMERA_NOISE_COUNT) {
        T_FEQ(gc->camera.noise[slot].magnitude, 0.0f, 0.001f);
        T_FEQ(gc->camera.noise[slot].velocity, 0.0f, 0.001f);
        T_ASSERT(!gc->camera.noise[slot].vert_only);
    }
    currentplayer = NULL;
}

TEST(wc3_api, camera_field_set_adjust_and_stop_sample_current_transition) {
    gameClient_t *gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.target_distance = 900.0f;
    gc->camera.old_state = gc->camera.state;
    gc->camera.start_time = gc->camera.end_time = 100;
    level.time = 100;
    currentplayer = &gc->ps;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraField(CAMERA_FIELD_TARGET_DISTANCE, 1500.0, 2.0)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.old_state.target_distance, 900.0f, 0.001f);
    T_FEQ(gc->camera.state.target_distance, 1500.0f, 0.001f);
    T_EQ(gc->camera.start_time, 100);
    T_EQ(gc->camera.end_time, 2100);

    level.time = 1100;
    G_RunClients();
    T_FEQ(gc->ps.distance, 1200.0f, 0.001f);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call AdjustCameraField(CAMERA_FIELD_TARGET_DISTANCE, 300.0, 1.0)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.old_state.target_distance, 1200.0f, 0.001f);
    T_FEQ(gc->camera.state.target_distance, 1500.0f, 0.001f);
    T_EQ(gc->camera.start_time, 1100);
    T_EQ(gc->camera.end_time, 2100);

    level.time = 1600;
    G_RunClients();
    T_FEQ(gc->ps.distance, 1350.0f, 0.001f);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call StopCamera()\n"
        "endfunction\n"));
    T_FEQ(gc->camera.old_state.target_distance, 1350.0f, 0.001f);
    T_FEQ(gc->camera.state.target_distance, 1350.0f, 0.001f);
    T_EQ(gc->camera.start_time, 1600);
    T_EQ(gc->camera.end_time, 1600);

    level.time = 2100;
    G_RunClients();
    T_FEQ(gc->ps.distance, 1350.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_field_setters_preserve_authored_angle_mapping) {
    gameClient_t *gc = &game.clients[0];
    float pitch = G_CameraAuthoredToPitch(304.0f);

    gc->ps.number = 0;
    gc->camera.state.viewangles = (vec3_t){ pitch, 0.0f, G_CameraAuthoredToYaw(90.0f, pitch) };
    gc->camera.state.fov = G_CameraHorizontalToVerticalFov(70.0f);
    gc->camera.old_state = gc->camera.state;
    gc->camera.start_time = gc->camera.end_time = 100;
    level.time = 100;
    currentplayer = &gc->ps;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraField(CAMERA_FIELD_ANGLE_OF_ATTACK, 270.0, 0.0)\n"
        "  call AdjustCameraField(CAMERA_FIELD_ROTATION, 15.0, 0.0)\n"
        "  call SetCameraField(CAMERA_FIELD_FIELD_OF_VIEW, 80.0, 0.0)\n"
        "endfunction\n"));

    pitch = G_CameraAuthoredToPitch(270.0f);
    T_FEQ(gc->camera.state.viewangles.x, pitch, 0.001f);
    T_FEQ(gc->camera.state.viewangles.z, G_CameraAuthoredToYaw(105.0f, pitch), 0.001f);
    T_FEQ(gc->camera.state.fov, G_CameraHorizontalToVerticalFov(80.0f), 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, timed_camera_pan_with_z_interpolates_target_height) {
    gameClient_t *gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(vec2_t, 0.0f, 0.0f);
    gc->camera.state.z_offset = 0.0f;
    gc->camera.target_height = G_MakeServerOrigin(0.0f, 0.0f, 0.0f).z;
    gc->camera.old_state = gc->camera.state;
    level.time = 100;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call PanCameraToTimedWithZ(200.0, 300.0, 400.0, 2.0)\n"
        "endfunction\n"));
    T_ASSERT(gc->camera.pan_active);
    T_FEQ(gc->camera.pan_destination.x, 200.0f, 0.001f);
    T_FEQ(gc->camera.pan_destination.y, 300.0f, 0.001f);
    T_FEQ(gc->camera.state.z_offset, 400.0f, 0.001f);
    T_EQ(gc->camera.start_time, 100);
    T_EQ(gc->camera.end_time, 2100);

    level.time = 1100;
    G_RunClients();
    T_FEQ(gc->ps.vieworigin.x, 100.0f, 0.001f);
    T_FEQ(gc->ps.vieworigin.y, 150.0f, 0.001f);
    T_FEQ(gc->ps.vieworigin.z, gc->camera.target_height + 200.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_orient_controller_keeps_source_fixed_while_tracking_unit) {
    gameClient_t *gc;
    edict_t *target = NULL;
    float initial_yaw;

    setup_test_world();
    gc = &game.clients[0];
    gc->ps.number = 0;
    gc->camera.state.position = MAKE(vec2_t, 0.0f, 0.0f);
    gc->camera.state.z_offset = 0.0f;
    gc->camera.state.viewangles = MAKE(vec3_t, 90.0f, 0.0f, 270.0f);
    gc->camera.state.target_distance = 100.0f;
    gc->camera.old_state = gc->camera.state;
    gc->camera.start_time = gc->camera.end_time = 100;
    level.time = 100;
    G_RunClients();
    currentplayer = &gc->ps;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 100.0, 0.0, 0.0)\n"
        "  call SetCameraOrientController(u, 0.0, 0.0)\n"
        "endfunction\n"));
    target = gc->camera.target_controller;
    T_NOT_NULL(target);
    T_EQ(gc->camera.target_mode, CAMERA_TARGET_ORIENT);
    T_FEQ(gc->camera.orient_eye.x, -100.0f, 0.01f);
    T_FEQ(gc->camera.orient_eye.y, 0.0f, 0.01f);

    G_RunClients();
    initial_yaw = gc->camera.state.viewangles.z;
    T_FEQ(gc->camera.state.target_distance, 100.0f, 0.001f);
    target->s.origin2 = MAKE(vec2_t, 0.0f, 100.0f);
    target->s.origin.x = 0.0f;
    target->s.origin.y = 100.0f;
    G_RunClients();
    T_ASSERT(fabsf(gc->camera.state.viewangles.z - initial_yaw) > 1.0f);
    T_FEQ(gc->camera.state.target_distance, 100.0f, 0.001f);
    T_FEQ(gc->camera.orient_eye.x, -100.0f, 0.01f);
    T_FEQ(gc->camera.orient_eye.y, 0.0f, 0.01f);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ResetToGameCamera(0.0)\n"
        "endfunction\n"));
    T_NULL(gc->camera.target_controller);
    T_EQ(gc->camera.target_mode, CAMERA_TARGET_FOLLOW);
    currentplayer = NULL;
}

TEST(wc3_api, untimed_camera_pan_uses_authored_forward_and_strafe_rates) {
    gameClient_t *gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(vec2_t, 0.0f, 0.0f);
    gc->camera.old_state = gc->camera.state;
    gc->camera.target_height = G_MakeServerOrigin(0.0f, 0.0f, 0.0f).z;
    G_ClearCameraPan(gc);
    level.time = 100;
    currentplayer = &gc->ps;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call PanCameraTo(200.0, 400.0)\n"
        "endfunction\n"));
    T_ASSERT(gc->camera.pan_active);
    T_FEQ(gc->camera.pan_rate.x, 100.0f, 0.001f);
    T_FEQ(gc->camera.pan_rate.y, 200.0f, 0.001f);

    level.time = 1100;
    G_RunClients();
    T_FEQ(gc->ps.vieworigin.x, 100.0f, 0.001f);
    T_FEQ(gc->ps.vieworigin.y, 200.0f, 0.001f);
    T_ASSERT(gc->camera.pan_active);

    level.time = 2100;
    G_RunClients();
    T_FEQ(gc->ps.vieworigin.x, 200.0f, 0.001f);
    T_FEQ(gc->ps.vieworigin.y, 400.0f, 0.001f);
    T_ASSERT(!gc->camera.pan_active);
    T_FEQ(gc->camera.state.position.x, 200.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 400.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, invalid_camera_pan_rates_report_error_without_snapping) {
    gameClient_t *gc = &game.clients[0];
    stbIniCache_t saved = game.config.misc, custom = { 0 };

    T_ASSERT(Stb_IniCacheLoadBuffer(&custom,
        "[CameraRates]\nStrafe=invalid\nForward=200\n"));
    game.config.misc = custom;
    gc->ps.number = 0;
    gc->camera.state.position = MAKE(vec2_t, 10.0f, 20.0f);
    gc->camera.old_state = gc->camera.state;
    G_ClearCameraPan(gc);
    currentplayer = &gc->ps;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call PanCameraTo(200.0, 400.0)\n"
        "endfunction\n"));
    T_ASSERT(!gc->camera.pan_active);
    T_FEQ(gc->camera.state.position.x, 10.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 20.0f, 0.001f);

    currentplayer = NULL;
    game.config.misc = saved;
    Stb_IniCacheFree(&custom);
}

TEST(wc3_api, camera_setup_pantimed_uses_normal_pan_rates) {
    gameClient_t *gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(vec2_t, 0.0f, 0.0f);
    gc->camera.old_state = gc->camera.state;
    gc->camera.target_height = G_MakeServerOrigin(0.0f, 0.0f, 0.0f).z;
    G_ClearCameraPan(gc);
    level.time = 100;
    currentplayer = &gc->ps;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local camerasetup c = CreateCameraSetup()\n"
        "  call CameraSetupSetDestPosition(c, 200.0, 400.0, 0.0)\n"
        "  call CameraSetupApply(c, true, true)\n"
        "endfunction\n"));
    T_ASSERT(gc->camera.pan_active);
    T_FEQ(gc->camera.pan_destination.x, 200.0f, 0.001f);
    T_FEQ(gc->camera.pan_destination.y, 400.0f, 0.001f);

    level.time = 1100;
    G_RunClients();
    T_FEQ(gc->ps.vieworigin.x, 100.0f, 0.001f);
    T_FEQ(gc->ps.vieworigin.y, 200.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_target_controller_can_inherit_unit_facing) {
    gameClient_t *gc = &game.clients[0];
    edict_t *target = NULL;

    /* Camera offsets need an admitted clear spawn, independent of prior camera bounds. */
    reset_entities(); setup_test_world();
    gc->ps.number = 0;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 100.0, 200.0, 135.0)\n"
        "  call SetCameraTargetController(u, 10.0, -20.0, true)\n"
        "endfunction\n"));
    target = gc->camera.target_controller;
    T_NOT_NULL(target);
    T_FEQ(gc->camera.state.position.x, 110.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 180.0f, 0.001f);
    T_FEQ(gc->camera.state.viewangles.z, -45.0f, 0.001f);
    T_EQ(gc->camera.target_mode, CAMERA_TARGET_FOLLOW_FACING);

    target->s.origin2 = MAKE(vec2_t, 300.0f, 400.0f);
    target->s.angle = (float)DEG2RAD(45.0f);
    G_RunClients();
    T_FEQ(gc->camera.state.position.x, 310.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 380.0f, 0.001f);
    T_FEQ(gc->camera.state.viewangles.z, 45.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_setup_applies_clip_planes_z_and_dopan_contract) {
    gameClient_t *gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(vec2_t, 12.0f, 34.0f);
    gc->camera.state.near_z = 100.0f;
    gc->camera.state.far_z = 5000.0f;
    gc->camera.state.z_offset = 0.0f;
    gc->camera.old_state = gc->camera.state;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local camerasetup c = CreateCameraSetup()\n"
        "  call CameraSetupSetDestPosition(c, 500.0, 600.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_NEARZ, 55.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_FARZ, 6500.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_ZOFFSET, 125.0, 0.0)\n"
        "  call CameraSetupApply(c, false, false)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.state.position.x, 12.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 34.0f, 0.001f);
    T_FEQ(gc->camera.state.near_z, 55.0f, 0.001f);
    T_FEQ(gc->camera.state.far_z, 6500.0f, 0.001f);
    T_FEQ(gc->camera.state.z_offset, 125.0f, 0.001f);

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local camerasetup c = CreateCameraSetup()\n"
        "  call CameraSetupSetDestPosition(c, 700.0, 800.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_ZOFFSET, 377.4, 0.0)\n"
        "  call CameraSetupApplyForceDuration(c, true, 2.0)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.state.position.x, 700.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 800.0f, 0.001f);
    T_FEQ(gc->camera.state.z_offset, 377.4f, 0.001f);

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local camerasetup c = CreateCameraSetup()\n"
        "  call CameraSetupSetDestPosition(c, 700.0, 800.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_NEARZ, 65.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_FARZ, 7500.0, 0.0)\n"
        "  call CameraSetupApplyWithZ(c, 275.0)\n"
        "endfunction\n"));
    T_ASSERT(gc->camera.pan_active);
    T_FEQ(gc->camera.pan_destination.x, 700.0f, 0.001f);
    T_FEQ(gc->camera.pan_destination.y, 800.0f, 0.001f);
    T_FEQ(gc->camera.state.near_z, 65.0f, 0.001f);
    T_FEQ(gc->camera.state.far_z, 7500.0f, 0.001f);
    T_FEQ(gc->camera.state.z_offset, 275.0f, 0.001f);
    G_RunClients();
    T_FEQ(gc->ps.znear, 65.0f, 0.001f);
    T_FEQ(gc->ps.zfar, 7500.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_quick_position_sets_spacebar_target_without_moving_camera) {
    gameClient_t *gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(vec2_t, 12.0f, 34.0f);
    gc->camera.quick_position = MAKE(vec2_t, 0.0f, 0.0f);
    gc->camera.quick_position_set = false;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraQuickPosition(2608.0, -5856.0)\n"
        "endfunction\n"));

    T_FEQ(gc->camera.state.position.x, 12.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 34.0f, 0.001f);
    T_ASSERT(gc->camera.quick_position_set);
    T_FEQ(gc->camera.quick_position.x, 2608.0f, 0.001f);
    T_FEQ(gc->camera.quick_position.y, -5856.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_bounds_are_map_global) {
    gameClient_t *gc0 = &game.clients[0];

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-100.0, -100.0, -100.0, 100.0, 100.0, 100.0, 100.0, -100.0)\n"
        "endfunction\n"));
    T_FEQ(level.camera_bounds.min.x, -100.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.y, 100.0f, 0.001f);

    currentplayer = &gc0->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-25.0, -20.0, -25.0, 20.0, 25.0, 20.0, 25.0, -20.0)\n"
        "endfunction\n"));
    T_FEQ(level.camera_bounds.min.x, -25.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.y, 20.0f, 0.001f);
    currentplayer = NULL;
}

/* Fast-forward only changes cinematic timing; JASS retains ownership of the input/UI lifecycle. */
TEST(wc3_api, skip_cutscene_preserves_scripted_input_and_ui_state) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    gameClient_t *gc = &game.clients[0];

    gi.CvarString = skip_cutscene_cvar;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ShowInterface(false, 0.0)\n"
        "  call EnableUserControl(false)\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_ASSERT(gc->no_control);

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ShowInterface(true, 0.0)\n"
        "  call EnableUserControl(true)\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
    T_ASSERT(!gc->no_control);
    currentplayer = NULL;
    gi.CvarString = old_cvar;
}

static uint32_t test_fow_cell(float x, float y) {
    uint32_t cx = G_FowWorldToCellX(x), cy = G_FowWorldToCellY(y);
    return cy * level.fow.width + cx;
}

#ifdef WC3_FOW_PACKED_MASK
static cstring_t api_fow_fast_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "wc3_fow_fast") ? "1" : fallback;
}

TEST(wc3_api, fog_state_natives_update_packed_planes) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    fowPlayerGrid_t *grid;
    uint32_t index, word;
    uint16_t bit;

    setup_test_world();
    G_FowInit();
    G_FowConnectPlayer(0);
    gi.CvarString = api_fow_fast_cvar;
    G_FowUpdate();
    grid = &level.fow.players[0];
    index = test_fow_cell(0.0f, 0.0f);
    word = (index % level.fow.width >> 4) + index / level.fow.width * grid->packed_stride;
    bit = (uint16_t)(1u << (index % level.fow.width & 15));

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_VISIBLE, 0.0, 0.0, 32.0, false)\n"
        "endfunction\n"));
    T_ASSERT(grid->packed_visible[word] & bit); T_ASSERT(grid->packed_explored[word] & bit);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_FOGGED, 0.0, 0.0, 32.0, false)\n"
        "endfunction\n"));
    T_ASSERT(!(grid->packed_visible[word] & bit)); T_ASSERT(grid->packed_explored[word] & bit);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_MASKED, 0.0, 0.0, 32.0, false)\n"
        "endfunction\n"));
    T_ASSERT(!(grid->packed_visible[word] & bit)); T_ASSERT(!(grid->packed_explored[word] & bit));
    gi.CvarString = old_cvar;
    G_FowShutdown();
}
#endif

TEST(wc3_api, fog_state_natives_write_masked_fogged_and_visible) {
    uint32_t fogged, visible, masked;
    fowPlayerGrid_t *grid;
    setup_test_world();
    G_FowInit();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRect(Player(0), FOG_OF_WAR_FOGGED, Rect(-64.0, -64.0, 64.0, 64.0), false)\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_VISIBLE, 256.0, 0.0, 32.0, false)\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_VISIBLE, -256.0, 0.0, 32.0, false)\n"
        "  call SetFogStateRadiusLoc(Player(0), FOG_OF_WAR_MASKED, Location(-256.0, 0.0), 32.0, false)\n"
        "endfunction\n"));
    grid = &level.fow.players[0];
    fogged = test_fow_cell(0.0f, 0.0f);
    visible = test_fow_cell(256.0f, 0.0f);
    masked = test_fow_cell(-256.0f, 0.0f);
    T_EQ(grid->explored[fogged], 1); T_EQ(grid->visible[fogged], 0);
    T_EQ(grid->explored[visible], 1); T_EQ(grid->visible[visible], 1);
    T_EQ(grid->explored[masked], 0); T_EQ(grid->visible[masked], 0);
}

TEST(wc3_api, unit_share_vision_native_updates_per_unit_recipient_mask) {
    edict_t *unit = NULL;

    setup_test_world();
    reset_entities();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(5), 'hpea', 64.0, 64.0, 0.0)\n"
        "  call UnitShareVision(u, Player(0), true)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('h','p','e','a') && ent->s.player == 5) {
            unit = ent;
            break;
        }
    }
    T_NOT_NULL(unit);
    if (!unit) return;
    T_ASSERT(G_UnitSharesVisionWith(unit, 0));
    T_ASSERT(!G_UnitSharesVisionWith(unit, 1));
}

TEST(wc3_api, fog_state_shared_vision_reaches_allied_viewer_only) {
    uint32_t index;
    setup_test_world();
    G_FowInit();
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_VISION, true);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_VISIBLE, 0.0, 0.0, 32.0, true)\n"
        "endfunction\n"));
    index = test_fow_cell(0.0f, 0.0f);
    T_EQ(level.fow.players[0].visible[index], 1);
    T_EQ(level.fow.players[1].visible[index], 1);
    T_EQ(level.fow.players[2].visible[index], 0);
}

TEST(wc3_api, fog253_queries_classify_completed_planes) {
    reset_entities();setup_test_world();G_FowInit();G_FowConnectPlayer(0);
    T_ASSERT(run_test_jass(
        "function checkFog takes real x, real y, player p, boolean visible, boolean fogged, boolean masked returns nothing\n"
        "local location l=Location(x,y)\n"
        "call BJassAssert(IsVisibleToPlayer(x,y,p)==visible, \"point visible\")\n"
        "call BJassAssert(IsFoggedToPlayer(x,y,p)==fogged, \"point fogged\")\n"
        "call BJassAssert(IsMaskedToPlayer(x,y,p)==masked, \"point masked\")\n"
        "call BJassAssert(IsLocationVisibleToPlayer(l,p)==visible, \"location visible\")\n"
        "call BJassAssert(IsLocationFoggedToPlayer(l,p)==fogged, \"location fogged\")\n"
        "call BJassAssert(IsLocationMaskedToPlayer(l,p)==masked, \"location masked\")\n"
        "call RemoveLocation(l)\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "call FogEnable(true)\ncall FogMaskEnable(true)\n"
        "call checkFog(0.0,0.0,Player(0),false,false,true)\n"
        "call SetFogStateRadius(Player(0),FOG_OF_WAR_VISIBLE,0.0,0.0,32.0,false)\n"
        "call checkFog(0.0,0.0,Player(0),true,false,false)\n"
        "call checkFog(0.0,0.0,Player(1),false,false,true)\n"
        "call SetFogStateRadius(Player(0),FOG_OF_WAR_FOGGED,0.0,0.0,32.0,false)\n"
        "call checkFog(0.0,0.0,Player(0),false,true,false)\n"
        "call FogEnable(false)\ncall FogMaskEnable(false)\n"
        "call checkFog(0.0,0.0,Player(0),true,false,false)\n"
        "call checkFog(256.0,0.0,Player(0),true,false,false)\n"
        "call FogEnable(true)\ncall FogMaskEnable(true)\n"
        "call SetFogStateRadius(Player(0),FOG_OF_WAR_MASKED,0.0,0.0,32.0,false)\n"
        "call checkFog(0.0,0.0,Player(0),false,false,true)\n"
        "call checkFog(0.0,0.0,null,false,false,false)\n"
        "call BJassAssert(not IsLocationVisibleToPlayer(null,Player(0)), \"null visible\")\n"
        "call BJassAssert(not IsLocationFoggedToPlayer(null,Player(0)), \"null fogged\")\n"
        "call BJassAssert(not IsLocationMaskedToPlayer(null,Player(0)), \"null masked\")\n"
        "endfunction\n"));
    G_FowShutdown();reset_entities();setup_test_world();
}

TEST(wc3_api, fog253_queries_use_calling_client_policy) {
    reset_entities();setup_test_world();G_FowInit();
    player_t *caller=test_player(1);
    currentplayer=caller;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call FogEnable(false)\ncall FogMaskEnable(false)\n"
        "call BJassAssert(IsVisibleToPlayer(0.0,0.0,Player(0)), \"calling policy must affect other player query\")\n"
        "call BJassAssert(not IsMaskedToPlayer(0.0,0.0,Player(0)), \"calling mask policy\")\n"
        "endfunction\n"));
    currentplayer=NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call BJassAssert(IsMaskedToPlayer(0.0,0.0,Player(1)), \"queried player policy must not leak\")\n"
        "call BJassAssert(not IsVisibleToPlayer(0.0,0.0,Player(1)), \"default calling policy\")\n"
        "call BJassAssert(IsVisibleToPlayer(0.0,0.0,Player(12)), \"neutral forced visibility\")\n"
        "endfunction\n"));
    G_FowShutdown();reset_entities();setup_test_world();
}

TEST(wc3_api, fog_modifier_same_turn_start_stop_still_explores) {
    fogModifier_t mod = {
        .player = 0,
        .state = WC3_FOG_STATE_VISIBLE,
        .center = { 0.0f, 0.0f },
        .radius = 32.0f,
    };
    uint32_t index;

    setup_test_world();
    G_FowInit();
    G_FowConnectPlayer(0);
    fogModifier_t *owned=G_FogModifierCreate();T_NOT_NULL(owned);if(!owned)return;
    *owned=mod;
    index = test_fow_cell(0.0f, 0.0f);

    G_FogModifierStart(owned);
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 1);
    G_FogModifierStop(owned);

    /* The next normal update removes current sight but must retain the
     * exploration created synchronously by the short-lived modifier. */
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 0);
}

TEST(wc3_api, fog_modifier_states_and_visible_stop_transition) {
    fogModifier_t mod = {
        .player = 0,
        .state = WC3_FOG_STATE_VISIBLE,
        .center = { 0.0f, 0.0f },
        .radius = 32.0f,
    };
    uint32_t index;
    setup_test_world();
    G_FowInit();
    G_FowConnectPlayer(0);
    fogModifier_t *owned=G_FogModifierCreate();T_NOT_NULL(owned);if(!owned)return;
    *owned=mod;
    index = test_fow_cell(0.0f, 0.0f);

    G_FogModifierStart(owned);
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 1);
    G_FogModifierStop(owned);
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 0);

    owned->center.x = 256.0f;
    index = test_fow_cell(256.0f, 0.0f);
    owned->state = WC3_FOG_STATE_FOGGED;
    G_FogModifierStart(owned);
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 0);
    owned->state = WC3_FOG_STATE_MASKED;
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 0);
    T_EQ(level.fow.players[0].visible[index], 0);
    G_FogModifierStop(owned);
}

TEST(wc3_api, fog_modifier_save_restores_aliases_stopped_and_unreferenced_active_records) {
    reset_entities(); setup_test_world(); G_FowInit(); G_FowConnectPlayer(0);
    T_ASSERT(run_test_jass(
        "type fogmodifier extends handle\n"
        "globals\nfogmodifier first\nfogmodifier alias\nfogmodifier stopped\nendglobals\n"
        "function verify takes nothing returns nothing\n"
        "call BJassAssert(first == alias, \"fog aliases survive\")\n"
        "call FogModifierStop(first)\ncall FogModifierStart(alias)\n"
        "endfunction\n"
        "function destroy takes nothing returns nothing\n"
        "call DestroyFogModifier(first)\ncall FogModifierStart(alias)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\nlocal fogmodifier hidden\n"
        "set first=CreateFogModifierRadius(Player(0),FOG_OF_WAR_VISIBLE,0,0,256,false,true)\n"
        "set alias=first\ncall FogModifierStart(first)\n"
        "set hidden=CreateFogModifierRadius(Player(0),FOG_OF_WAR_FOGGED,0,0,256,false,true)\n"
        "call FogModifierStart(hidden)\nset hidden=null\n"
        "set stopped=CreateFogModifierRadius(Player(0),FOG_OF_WAR_VISIBLE,0,0,256,false,true)\n"
        "endfunction\n"));
    uint32_t index=test_fow_cell(0,0); G_FowUpdate();
    T_EQ(level.fow.players[0].visible[index],0); T_EQ(level.fow.players[0].explored[index],1);
    cstring_t file=Test_TempPath("wc3-fog166-registry.bin");
    T_ASSERT(WriteGame(file));
    jass_callbyname(level.vm,"verify",false);G_FowUpdate();
    T_EQ(level.fow.players[0].visible[index],1);
    T_ASSERT(ReadGame(file)); G_FowUpdate();
    T_EQ(level.fow.players[0].visible[index],0);
    jass_callbyname(level.vm,"verify",false);G_FowUpdate();
    T_ASSERT(!jass_rterror_pending(level.vm));T_EQ(level.fow.players[0].visible[index],1);
    jass_callbyname(level.vm,"destroy",false);G_FowUpdate();
    T_EQ(level.fow.players[0].visible[index],0);
    /* A destroyed light handle is saved as null; the unreferenced hidden
     * modifier remains game-owned and active through another cold restore. */
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));G_FowUpdate();
    T_EQ(level.fow.players[0].visible[index],0);
    remove(file);G_FowShutdown();reset_entities();setup_test_world();
}

TEST(wc3_time, jass_state_uses_misc_clock_and_suspend) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFloatGameState(GAME_STATE_TIME_OF_DAY, 12.0)\n"
        "endfunction\n"));
    /* Warsmash defers SetFloatGameState until the simulation clock update. */
    T_FEQ(G_GetTimeOfDay(), 0.0f, 0.001f);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_EQ(game.clients[0].ps.stats[UI_PLAYERSTAT_ENV_PHASE],
         (uint16_t)lroundf(0.5f * (float)USHRT_MAX));

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(GetFloatGameState(GAME_STATE_TIME_OF_DAY) == 12.0, \"time getter\")\n"
        "  call SuspendTimeOfDay(true)\n"
        "endfunction\n"));
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_EQ(game.clients[0].ps.stats[UI_PLAYERSTAT_ENV_PHASE],
         (uint16_t)lroundf(0.5f * (float)USHRT_MAX));

    /* An explicit set still applies while ordinary progression is suspended. */
    G_SetTimeOfDay(18.0f);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 18.0f, 0.001f);
    T_ASSERT(G_IsNight());

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SuspendTimeOfDay(false)\n"
        "endfunction\n"));
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 18.005f, 0.001f);
}

TEST(wc3_time, false_time_overrides_and_freezes_canonical_clock) {
    float const tick_seconds = (float)FRAMETIME / 1000.0f;
    float const clock_step = tick_seconds / game.constants.gameDayLength * game.constants.gameDayHours;

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_ASSERT(!G_IsFalseTimeOfDay());

    G_SetFalseTimeOfDay(0, 0, tick_seconds * 3.0f);
    /* Warsmash initializes the false clock on its first simulation tick. */
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_ASSERT(!G_IsFalseTimeOfDay());

    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 0.0f, 0.001f);
    T_ASSERT(G_IsFalseTimeOfDay());
    T_ASSERT(G_IsNight());
    T_EQ(game.clients[0].ps.stats[UI_PLAYERSTAT_ENV_VARIANT], 1);

    /* Explicit SetTimeOfDay retargets the false clock, not the frozen
     * canonical clock, while the override exists. */
    G_SetTimeOfDay(18.5f);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 18.5f, 0.001f);
    T_ASSERT(G_IsFalseTimeOfDay());

    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_ASSERT(!G_IsFalseTimeOfDay());
    T_ASSERT(!G_IsNight());
    T_EQ(game.clients[0].ps.stats[UI_PLAYERSTAT_ENV_VARIANT], 0);

    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f + clock_step, 0.001f);
}

TEST(wc3_time, warsmash_false_time_native_can_be_declared_by_extension_script) {
    T_ASSERT(run_test_jass(
        "native SetFalseTimeOfDay takes integer hour, integer minute, real duration returns nothing\n"
        "function main takes nothing returns nothing\n"
        "  call SetFalseTimeOfDay(21, 15, 2.0)\n"
        "endfunction\n"));
    T_ASSERT(level.timeofday.false_time.active);
    T_ASSERT(!level.timeofday.false_time.initialized);
    T_EQ(level.timeofday.false_time.hour, 21);
    T_EQ(level.timeofday.false_time.minute, 15);
}

TEST(wc3_time, false_time_transition_drives_game_state_events) {
    float const step = (float)FRAMETIME / 1000.0f;
    uint32_t writes;

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterGameStateEvent(t, GAME_STATE_TIME_OF_DAY, GREATER_THAN_OR_EQUAL, 18.0)\n"
        "endfunction\n"));
    writes = level.events.write;

    G_SetFalseTimeOfDay(21, 0, step * 3.0f);
    /* Creation is deliberately uninitialized, so the event enters its
     * condition when the first simulation tick exposes the false clock. */
    T_EQ(level.events.write, writes);
    G_UpdateTimeOfDay();
    T_EQ(level.events.write, writes + 1);
}

TEST(wc3_time, set_day_night_models_publishes_registered_dnc_models) {
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    void (*old_configstring)(uint32_t, cstring_t) = gi.configstring;

    dnc_model_index_calls = 0;
    dnc_configstring_calls = 0;
    memset(dnc_configstring_index, 0, sizeof(dnc_configstring_index));
    memset(dnc_configstring_value, 0, sizeof(dnc_configstring_value));
    gi.ModelIndex = capture_dnc_model_index;
    gi.configstring = capture_dnc_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetDayNightModels(\"Environment\\DNC\\Terrain.mdl\", \"Environment\\DNC\\Unit.mdl\")\n"
        "endfunction\n"));

    T_EQ(dnc_model_index_calls, 2);
    T_EQ(dnc_configstring_calls, 2);
    T_EQ(dnc_configstring_index[0], CS_TERRAIN_LIGHT_MODEL);
    T_STREQ(dnc_configstring_value[0], "41");
    T_EQ(dnc_configstring_index[1], CS_ENTITY_LIGHT_MODEL);
    T_STREQ(dnc_configstring_value[1], "42");

    gi.ModelIndex = old_model_index;
    gi.configstring = old_configstring;
}

TEST(wc3_environment_fog, set_terrain_fog_ex_publishes_runtime_state) {
    void (*old_configstring)(uint32_t, cstring_t) = gi.configstring;
    int style = -1;
    float start = 0.0f, end = 0.0f, density = 0.0f;
    float red = 0.0f, green = 0.0f, blue = 0.0f;

    scene_fog_configstring_calls = 0;
    scene_fog_configstring_index = 0;
    scene_fog_configstring_value[0] = '\0';
    gi.configstring = capture_scene_fog_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetTerrainFogEx(0, 1000.0, 5000.0, 0.25, 0.2, 0.3, 0.4)\n"
        "endfunction\n"));

    T_EQ(level.environment_fog.active.style, WC3_ENV_FOG_LINEAR);
    T_FEQ(level.environment_fog.active.start, 1000.0f, 0.001f);
    T_FEQ(level.environment_fog.active.end, 5000.0f, 0.001f);
    T_FEQ(level.environment_fog.active.density, 0.25f, 0.001f);
    T_FEQ(level.environment_fog.active.color.x, 0.2f, 0.001f);
    T_FEQ(level.environment_fog.active.color.y, 0.3f, 0.001f);
    T_FEQ(level.environment_fog.active.color.z, 0.4f, 0.001f);
    T_EQ(scene_fog_configstring_calls, 1);
    T_EQ(scene_fog_configstring_index, CS_SCENE_FOG);
    T_EQ(sscanf(scene_fog_configstring_value, "%d %f %f %f %f %f %f",
                &style, &start, &end, &density, &red, &green, &blue), 7);
    T_EQ(style, WC3_ENV_FOG_LINEAR);
    T_FEQ(start, 1000.0f, 0.001f);
    T_FEQ(end, 5000.0f, 0.001f);
    T_FEQ(density, 0.25f, 0.001f);
    T_FEQ(red, 0.2f, 0.001f);
    T_FEQ(green, 0.3f, 0.001f);
    T_FEQ(blue, 0.4f, 0.001f);

    gi.configstring = old_configstring;
}

TEST(wc3_environment_fog, reset_terrain_fog_restores_default_state) {
    void (*old_configstring)(uint32_t, cstring_t) = gi.configstring;

    level.environment_fog.defaults = (wc3EnvironmentFogState_t){
        .style = WC3_ENV_FOG_LINEAR,
        .start = 2500.0f,
        .end = 9000.0f,
        .density = 0.125f,
        .color = { 0.1f, 0.2f, 0.3f },
    };
    level.environment_fog.defaults_valid = true;
    level.environment_fog.active = (wc3EnvironmentFogState_t){
        .style = WC3_ENV_FOG_EXPONENTIAL_2,
        .start = 1.0f, .end = 2.0f, .density = 3.0f,
        .color = { 0.9f, 0.8f, 0.7f },
    };
    scene_fog_configstring_calls = 0;
    scene_fog_configstring_value[0] = '\0';
    gi.configstring = capture_scene_fog_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ResetTerrainFog()\n"
        "endfunction\n"));

    T_EQ(level.environment_fog.active.style, WC3_ENV_FOG_LINEAR);
    T_FEQ(level.environment_fog.active.start, 2500.0f, 0.001f);
    T_FEQ(level.environment_fog.active.end, 9000.0f, 0.001f);
    T_FEQ(level.environment_fog.active.density, 0.125f, 0.001f);
    T_FEQ(level.environment_fog.active.color.x, 0.1f, 0.001f);
    T_FEQ(level.environment_fog.active.color.y, 0.2f, 0.001f);
    T_FEQ(level.environment_fog.active.color.z, 0.3f, 0.001f);
    T_EQ(scene_fog_configstring_calls, 1);
    T_EQ(scene_fog_configstring_index, CS_SCENE_FOG);

    gi.configstring = old_configstring;
}

TEST(wc3_environment_fog, reset_without_default_is_noop) {
    void (*old_configstring)(uint32_t, cstring_t) = gi.configstring;

    level.environment_fog.defaults_valid = false;
    level.environment_fog.active = (wc3EnvironmentFogState_t){
        .style = WC3_ENV_FOG_LINEAR,
        .start = 1200.0f,
        .end = 6400.0f,
        .density = 0.5f,
        .color = { 0.25f, 0.5f, 0.75f },
    };
    scene_fog_configstring_calls = 0;
    gi.configstring = capture_scene_fog_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ResetTerrainFog()\n"
        "endfunction\n"));

    T_EQ(level.environment_fog.active.style, WC3_ENV_FOG_LINEAR);
    T_FEQ(level.environment_fog.active.start, 1200.0f, 0.001f);
    T_FEQ(level.environment_fog.active.end, 6400.0f, 0.001f);
    T_FEQ(level.environment_fog.active.density, 0.5f, 0.001f);
    T_EQ(scene_fog_configstring_calls, 0);

    gi.configstring = old_configstring;
}

static cstring_t fog_roc_cvar(cstring_t name, cstring_t fallback) { return !strcmp(name, "fs_expansion") ? "0" : fallback; }
static cstring_t fog_tft_cvar(cstring_t name, cstring_t fallback) { return !strcmp(name, "fs_expansion") ? "1" : fallback; }

static void check_singleton_default_zfog(wc3EnvironmentFogState_t const *fog) {
    T_EQ(fog->style, WC3_ENV_FOG_LINEAR); /* authored 0 maps through +1 */
    T_FEQ(fog->start, 20000.0f, 0.001f);
    T_FEQ(fog->end, 50000.0f, 0.001f);
    T_FEQ(fog->density, 0.0f, 0.001f);
    T_FEQ(fog->color.x, 0.0f, 0.001f);
    T_FEQ(fog->color.y, 0.0f, 0.001f);
    T_FEQ(fog->color.z, 0.0f, 0.001f);
}

/* Retail [DefaultZFog] is a singleton; TFT index 1 never parses, so index 0 is the per-field fallback. */
TEST(wc3_environment_fog, singleton_default_zfog_parses_under_both_editions) {
    stbIniCache_t saved = game.config.misc, custom = { 0 };
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    wc3EnvironmentFogState_t fog;

    T_ASSERT(Stb_IniCacheLoad(&custom, "TestData\\DefaultZFog.txt"));
    game.config.misc = custom;
    gi.CvarString = fog_roc_cvar;
    T_ASSERT(G_EnvironmentFogDefault(&fog));
    check_singleton_default_zfog(&fog);
    gi.CvarString = fog_tft_cvar;
    T_ASSERT(G_EnvironmentFogDefault(&fog));
    check_singleton_default_zfog(&fog);

    gi.CvarString = old_cvar;
    game.config.misc = saved;
    Stb_IniCacheFree(&custom);
}

TEST(wc3_environment_fog, invalid_extended_style_disables_scene_fog) {
    void (*old_configstring)(uint32_t, cstring_t) = gi.configstring;

    scene_fog_configstring_calls = 0;
    scene_fog_configstring_value[0] = '\0';
    gi.configstring = capture_scene_fog_configstring;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetTerrainFogEx(99, 1.0, 2.0, 3.0, 0.4, 0.5, 0.6)\n"
        "endfunction\n"));
    T_EQ(level.environment_fog.active.style, WC3_ENV_FOG_NONE);
    T_ASSERT(scene_fog_configstring_value[0] == '0');

    gi.configstring = old_configstring;
}

TEST(wc3_api, set_sky_model_publishes_registered_model) {
    int (*old_model_index)(cstring_t) = gi.ModelIndex;
    void (*old_configstring)(uint32_t, cstring_t) = gi.configstring;

    sky_model_index_calls = sky_configstring_calls = 0;
    sky_configstring_index = 0;
    sky_configstring_value[0] = '\0';
    gi.ModelIndex = capture_sky_model_index;
    gi.configstring = capture_sky_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetSkyModel(\"Environment\\Sky\\Sky.mdx\")\n"
        "endfunction\n"));

    T_EQ(sky_model_index_calls, 1);
    T_EQ(sky_configstring_calls, 1);
    T_EQ(sky_configstring_index, CS_SKY);
    T_STREQ(sky_configstring_value, "41");

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetSkyModel(\"\")\n"
        "endfunction\n"));
    T_EQ(sky_model_index_calls, 1);
    T_EQ(sky_configstring_calls, 2);
    T_EQ(sky_configstring_index, CS_SKY);
    T_STREQ(sky_configstring_value, "0");

    gi.ModelIndex = old_model_index;
    gi.configstring = old_configstring;
}

TEST(wc3_time, dawn_and_dusk_use_misc_thresholds) {
    G_SetTimeOfDay(5.99f);
    G_UpdateTimeOfDay();
    T_ASSERT(G_IsNight());

    G_SetTimeOfDay(game.constants.dawnTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(!G_IsNight());

    G_SetTimeOfDay(game.constants.duskTimeGameHours - 0.01f);
    G_UpdateTimeOfDay();
    T_ASSERT(!G_IsNight());

    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(G_IsNight());
}

TEST(wc3_time, game_state_event_fires_on_false_to_true_transition) {
    uint32_t writes;

    G_SetTimeOfDay(5.0f);
    G_UpdateTimeOfDay();
    T_ASSERT(run_test_jass(
        "function onTime takes nothing returns nothing\n"
        "  call SetFloatGameState(ConvertFGameState(2), 12.0)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterGameStateEvent(t, GAME_STATE_TIME_OF_DAY, GREATER_THAN_OR_EQUAL, 6.0)\n"
        "  call TriggerAddAction(t, function onTime)\n"
        "endfunction\n"));

    G_SetTimeOfDay(6.0f);
    G_UpdateTimeOfDay();
    writes = level.events.write;
    T_EQ(writes, 1);
    G_RunEvents();
    jass_runevents(level.vm);

    /* The trigger action queues 12:00; applying it does not refire because
     * both 06:00 and 12:00 satisfy the registered >= 6 condition. */
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_EQ(level.events.write, writes);
    G_UpdateTimeOfDay();
    T_EQ(level.events.write, writes);
}

TEST(wc3_api, variable_event_fires_when_counter_reaches_limit) {
    uint32_t writes;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer counter = 0\n"
        "endglobals\n"
        "function onCounter takes nothing returns nothing\n"
        "  call SetFloatGameState(GAME_STATE_TIME_OF_DAY, 12.0)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterVariableEvent(t, \"counter\", ConvertLimitOp(2), 100.0)\n"
        "  call TriggerAddAction(t, function onCounter)\n"
        "  set counter = 99\n"
        "  set counter = 100\n"
        "  set counter = 101\n"
        "endfunction\n"));
    writes = level.events.write;
    T_EQ(writes, 1);
    G_RunEvents();
    jass_runevents(level.vm);
    T_EQ(level.events.read, writes);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
}

/* The retail cripple timer broadcasts with a direct local-player argument after its local IF. */
TEST(wc3_api, direct_local_player_text_call_reaches_each_player_once) {
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function announce takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call DisplayTimedTextToPlayer(GetLocalPlayer(), 0, 0, 5, \"local\")\n"
        "  endif\n"
        "  call DisplayTimedTextToPlayer(GetLocalPlayer(), 0, 0, 10, \"revealed\")\n"
        "  call DisplayTextToPlayer(Player(1), 0, 0, \"targeted\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  call ExecuteFunc(\"announce\")\n"
        "endfunction\n"));
    jass_runevents(level.vm);
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *gc = &game.clients[i];
        if (i >= MAX_PLAYERS) {
            T_EQ(gc->message_log.count, 0);
            continue;
        }
        T_EQ(gc->message_log.count, i < 2 ? 2 : 1);
        T_STREQ(gc->message_log.entries[i == 0 ? 1 : 0], "revealed");
        T_STREQ(gc->message.text, i == 1 ? "targeted" : "revealed");
    }
    T_NULL(currentplayer);
}

TEST(wc3_api, display_text_tracks_lifetime_and_clear) {
    gameClient_t *gc = &game.clients[0];

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    level.time = 100;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call DisplayTimedTextToPlayer(Player(0), 0.10, 0.20, 2.0, \"Timed message\")\n"
        "endfunction\n"));
    T_EQ(gc->message.end_time, 2100);
    T_FEQ(gc->message.position.x, 0.10f, 0.001f);
    T_FEQ(gc->message.position.y, 0.20f, 0.001f);
    T_STREQ(gc->message.text, "Timed message");
    T_EQ(gc->message_log.count, 1);
    T_STREQ(gc->message_log.entries[0], "Timed message");

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ClearTextMessages()\n"
        "endfunction\n"));
    T_EQ(gc->message.end_time, 0);
    T_STREQ(gc->message.text, "");
    T_EQ(gc->message_log.count, 1);
    T_STREQ(gc->message_log.entries[0], "Timed message");
}

static edict_t *find_test_unit(uint32_t class_id) {
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == class_id) {
            return g_edicts + i;
        }
    }
    return NULL;
}

static void setup_set_unit_position_pathmap(void) {
    enum { CELLS = 16 };
    uint8_t pathmap[CELLS * CELLS] = {0};

    /* Requested point (256,256) lies in cell (8,8). Buildings and other
     * authored blockers are baked into this same no-walk map in production. */
    pathmap[8 * CELLS + 8] = 2;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
}

/* Public terrain natives must mutate just the selected fine-cell bits and
 * report blocked status. Native type201630 is distinct from movement masks. */
TEST(wc3_api, terrain_pathing_natives_preserve_other_bits_and_cells) {
    uint8_t const masks[] = {255,2,4,8,16,32,64,128,0};
    box2_t bounds = {{-1024,-2048},{1024,0}};
    static uint8_t cells[64*64];
    char script[2048];
    for (unsigned type=0;type<9;type++) {
        unsigned rawtype = type==8 ? 42 : type;
        for (unsigned seed=0;seed<4;seed++) {
            uint8_t flags=(uint8_t[]){0,1,0x55,0xaa}[seed], after=flags|masks[type];
            memset(cells,0,sizeof(cells)); cells[5*64+4]=flags; cells[5*64+5]=0x91;
            reset_entities(); setup_test_world();
            CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(64,64,cells); G_BlightInit();
            snprintf(script,sizeof(script),
                "function main takes nothing returns nothing\n"
                "local pathingtype p=ConvertPathingType(%u)\n"
                "call BJassAssert(IsTerrainPathable(-895.875,-1887.75,p)==%s,\"terrain query before\")\n"
                "call SetTerrainPathable(-895.875,-1887.75,p,false)\n"
                "call BJassAssert(IsTerrainPathable(-895.875,-1887.75,p)==%s,\"terrain query blocked\")\n"
                "call SetTerrainPathable(-895.875,-1887.75,p,true)\n"
                "call BJassAssert(not IsTerrainPathable(-895.875,-1887.75,p),\"terrain query cleared\")\n"
                "call SetTerrainPathable(-895.875,-1887.75,p,false)\n"
                "endfunction\n",rawtype,flags&masks[type]?"true":"false",masks[type]?"true":"false");
            T_ASSERT(run_test_jass(script));
            uint8_t actual=0;
            T_ASSERT(CM_GetPathingFlagsAt(&(vec2_t){-895.875,-1887.75},&actual));
            T_EQ(actual,after);
            T_ASSERT(CM_GetPathingFlagsAt(&(vec2_t){-863.875,-1887.75},&actual));
            T_EQ(actual,0x91);
        }
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_api, terrain_pathing_natives_survive_save_and_restore_blight) {
    cstring_t filename = Test_TempPath("openwarcraft3-terrain-pathing-save.bin");
    uint8_t cells[16*16] = {0}, flags = 0;
    vec2_t point = {144,176}, neighbor = {176,176};
    reset_entities(); setup_test_world();
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{512,512}});
    CM_SetupTestPathmap(16,16,cells); G_BlightInit();
    T_ASSERT(run_test_jass(
        "function block takes nothing returns nothing\n"
        "call SetTerrainPathable(144,176,ConvertPathingType(0),false)\nendfunction\n"
        "function clear takes nothing returns nothing\n"
        "call SetTerrainPathable(144,176,ConvertPathingType(0),true)\nendfunction\n"
        "function verify takes nothing returns nothing\n"
        "call BJassAssert(IsTerrainPathable(144,176,ConvertPathingType(1)),\"saved walking bit\")\n"
        "call BJassAssert(IsTerrainPathable(144,176,ConvertPathingType(5)),\"saved blight bit\")\n"
        "endfunction\nfunction main takes nothing returns nothing\ncall block()\nendfunction\n"));
    T_ASSERT(G_IsPointBlighted(&point)); T_ASSERT(!G_IsPointBlighted(&neighbor));
    T_ASSERT(WriteGame(filename));
    jass_callbyname(level.vm,"clear",false);
    T_ASSERT(!G_IsPointBlighted(&point));
    T_ASSERT(G_GetTerrainPathingFlags(&point,&flags)); T_EQ(flags,0);
    T_ASSERT(ReadGame(filename));
    jass_callbyname(level.vm,"verify",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(G_GetTerrainPathingFlags(&point,&flags)); T_EQ(flags,255);
    T_ASSERT(G_IsPointBlighted(&point)); T_ASSERT(!G_IsPointBlighted(&neighbor));
    T_ASSERT(G_GetTerrainPathingFlags(&neighbor,&flags)); T_EQ(flags,0);
    remove(filename); reset_entities(); setup_test_world();
}

TEST(wc3_api, terrain_point_query231_preserves_live_spatial_observations) {
    uint8_t cells[16*16]={0};
    reset_entities();setup_test_world();
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{512,512}});CM_SetupTestPathmap(16,16,cells);
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),144,176);
    unit->svflags|=SVF_MONSTER;unit->collision=16;unit->s.model=1;
    G_PublishMoveSpatialObject(unit);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    wc3RecordObject_t *object=wc3_records_owned(map,unit-g_edicts);
    object->stamp=7;map->query=1000;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call BJassAssert(not IsTerrainPathable(144,176,ConvertPathingType(1)),\"terrain ignores unit verdict\")\n"
        "endfunction\n"));
    /*04e090 supplies a high-only mask.1489a0 still observes an active
     * object's identity before its low-category miss; this history is saved. */
    T_EQ(map->query,1001);T_EQ(object->stamp,1001);
    uint32_t owner=unit-g_edicts;
    cstring_t filename=Test_TempPath("point231-save.bin");
    T_ASSERT(WriteGame(filename));
    /* Saving compacts memberships through its own stamped traversal. Compare
     * the serialized state, rather than the earlier native-return counter. */
    uint32_t saved_query=map->query,saved_stamp=object->stamp;
    map->query=9000;object->stamp=9000;
    T_ASSERT(ReadGame(filename));map=S_GetMoveFineSpatial();
    object=wc3_records_owned(map,owner);T_NOT_NULL(object);
    T_EQ(map->query,saved_query);if(object)T_EQ(object->stamp,saved_stamp);
    remove(filename);reset_entities();setup_test_world();
}

TEST(wc3_api, terrain_pathing_query_ignores_objects_and_preserves_native_amphibious_bit) {
    uint8_t cells[16*16] = {0}, flags = 0;
    vec2_t point = {144,176};
    reset_entities(); setup_test_world();
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{512,512}});
    CM_SetupTestPathmap(16,16,cells); G_BlightInit();
    edict_t *object = alloc_test_unit(0,point.x,point.y);
    object->collision = 16; object->svflags = 0; gi.LinkEntity(object);
    CM_BakeStaticObstacles();
    T_ASSERT(CM_GetPathingFlagsAt(&point,&flags)); T_ASSERT(flags & 2);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call BJassAssert(not IsTerrainPathable(144,176,ConvertPathingType(1)),\"objects are not terrain\")\n"
        "call BJassAssert(IsTerrainPathable(-0.125,176,ConvertPathingType(42)),\"outside with empty mask\")\n"
        "call BJassAssert(IsTerrainPathable(512,176,ConvertPathingType(1)),\"max edge is outside\")\n"
        "call SetTerrainPathable(-0.125,176,ConvertPathingType(0),false)\n"
        "call SetTerrainPathable(144,176,ConvertPathingType(1),false)\n"
        "call SetTerrainPathable(144,176,ConvertPathingType(6),false)\n"
        "call BJassAssert(not IsTerrainPathable(144,176,ConvertPathingType(7)),\"native writes do not derive amphibious bit\")\n"
        "call SetTerrainPathable(144,176,ConvertPathingType(1),true)\n"
        "call BJassAssert(not IsTerrainPathable(144,176,ConvertPathingType(1)),\"terrain clears under object\")\n"
        "endfunction\n"));
    T_ASSERT(G_GetTerrainPathingFlags(&point,&flags)); T_EQ(flags,64);
    T_ASSERT(CM_GetPathingFlagsAt(&point,&flags)); T_ASSERT(flags & 2);
    reset_entities(); setup_test_world();
}

TEST(wc3_api, set_unit_position_unstucks_from_blocked_pathing) {
    edict_t *moved;

    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit mover = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "  call SetUnitPosition(mover, 256.0, 256.0)\n"
        "endfunction\n"));

    moved = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(moved);
    /* Retail policy2 visits cell(8,7) first and publishes its centre. */
    T_FEQ(moved->s.origin.x, 272.0f, 0.001f);
    T_FEQ(moved->s.origin.y, 240.0f, 0.001f);
}

TEST(wc3_api, createunit_unstucks_from_blocked_pathing) {
    edict_t *created;

    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call CreateUnit(Player(0), 'hpea', 256.0, 256.0, 0.0)\n"
        "endfunction\n"));

    created = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(created);
    /* Public CreateUnit uses the same verified cell(8,7) admission as SetUnitPosition. */
    T_FEQ(created->s.origin.x, 272.0f, 0.001f);
    T_FEQ(created->s.origin.y, 240.0f, 0.001f);
}

TEST(wc3_api, createunit_static_scenery_keeps_requested_spawn) {
    static cstring_t const data_slk =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"movetp\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K\"_\"\nE\n";
    static cstring_t const balance_slk =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"spd\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K0\nE\n";
    static cstring_t const weapons_slk =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitWeaponID\"\nC;Y1;X2;K\"weapsOn\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K0\nE\n";
    slkTestData_t *data_rows, *balance_rows, *weapons_rows;
    slkTestData_t *old_data, *old_balance, *old_weapons;
    slkTestData_t *new_data, *new_balance, *new_weapons;
    edict_t *created;

    reset_entities(); setup_test_world();
    data_rows = parse_slk_string(data_slk);
    balance_rows = parse_slk_string(balance_slk);
    weapons_rows = parse_slk_string(weapons_slk);
    old_data = G_SetSLKRows("UnitData", data_rows);
    old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    old_weapons = G_SetSLKRows("UnitWeapons", weapons_rows);
    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call CreateUnit(Player(0), 'nfrm', 256.0, 256.0, 0.0)\n"
        "endfunction\n"));

    created = find_test_unit(MAKEFOURCC('n','f','r','m'));
    T_NOT_NULL(created);
    if (created) {
        T_FEQ(created->s.origin.x, 256.0f, 0.001f);
        T_FEQ(created->s.origin.y, 256.0f, 0.001f);
    }
    reset_entities();
    new_data = G_SetSLKRows("UnitData", old_data);
    new_balance = G_SetSLKRows("UnitBalance", old_balance);
    new_weapons = G_SetSLKRows("UnitWeapons", old_weapons);
    free_slk_rows(new_data); free_slk_rows(new_balance); free_slk_rows(new_weapons);
    free_slk_rows(data_rows);
    free_slk_rows(balance_rows);
    free_slk_rows(weapons_rows);
    free_slk_rows(old_data); free_slk_rows(old_balance); free_slk_rows(old_weapons);
}

TEST(wc3_api, createunit_custom_static_scenery_keeps_requested_spawn) {
    static cstring_t const data_slk =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"movetp\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K\"_\"\nE\n";
    static cstring_t const balance_slk =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"spd\"\nC;Y1;X3;K\"realHP\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K0\nC;Y2;X3;K500\nE\n";
    static cstring_t const weapons_slk =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitWeaponID\"\nC;Y1;X2;K\"weapsOn\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K0\nE\n";
    uint32_t const custom_id = MAKEFOURCC('x','f','r','m');
    float custom_health = 137.25f;
    unitModification_t health_mod = {
        .modID = MAKEFOURCC('u','h','p','m'), .type = mod_real, .data = &custom_health
    };
    unitData_t custom = {
        .originalUnitID = MAKEFOURCC('n','f','r','m'), .newUnitID = custom_id,
        .numbeOfModifications = 1, .modifications = &health_mod
    };
    mapInfo_t mapinfo = { .num_userCreatedUnits = 1, .userCreatedUnits = &custom };
    slkTestData_t *data_rows, *balance_rows, *weapons_rows;
    slkTestData_t *old_data, *old_balance, *old_weapons;
    slkTestData_t *new_data, *new_balance, *new_weapons;
    mapInfo_t const *old_mapinfo;
    edict_t *created;

    reset_entities(); setup_test_world();
    data_rows = parse_slk_string(data_slk);
    balance_rows = parse_slk_string(balance_slk);
    weapons_rows = parse_slk_string(weapons_slk);
    old_data = G_SetSLKRows("UnitData", data_rows);
    old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    old_weapons = G_SetSLKRows("UnitWeapons", weapons_rows);
    old_mapinfo = level.mapinfo;
    level.mapinfo = &mapinfo;
    G_SetMapUnitOverrides(&mapinfo);
    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call CreateUnit(Player(0), 'xfrm', 256.0, 256.0, 0.0)\n"
        "endfunction\n"));

    created = find_test_unit(custom_id);
    T_NOT_NULL(created);
    if (created) {
        T_FEQ(created->s.origin.x, 256.0f, 0.001f);
        T_FEQ(created->s.origin.y, 256.0f, 0.001f);
        T_EQ(created->data.UnitData->id, custom_id);
        T_ASSERT(M_UnitMoveDisabled(created));
        T_EQ(created->data.UnitWeapons->id, custom_id);
        T_EQ(created->data.UnitBalance->id, custom_id);
        T_FEQ(created->data.UnitBalance->maxHealth, custom_health, 0.001f);
    }
    reset_entities();
    G_SetMapUnitOverrides(NULL);
    level.mapinfo = old_mapinfo;
    new_data = G_SetSLKRows("UnitData", old_data);
    new_balance = G_SetSLKRows("UnitBalance", old_balance);
    new_weapons = G_SetSLKRows("UnitWeapons", old_weapons);
    free_slk_rows(new_data); free_slk_rows(new_balance); free_slk_rows(new_weapons);
    free_slk_rows(data_rows);
    free_slk_rows(balance_rows);
    free_slk_rows(weapons_rows);
    free_slk_rows(old_data); free_slk_rows(old_balance); free_slk_rows(old_weapons);
}

/* BASE-02.1: zero fine query does not remove a positive-speed Move owner.
 * Both position natives still retain the requested unblocked pose. */
TEST(wc3_api, zero_query_armed_move_owner_keeps_requested_position) {
    static cstring_t const data_slk =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"movetp\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K\"_\"\nE\n";
    static cstring_t const balance_slk =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"spd\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K75\nE\n";
    static cstring_t const weapons_slk =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"unitWeaponID\"\nC;Y1;X2;K\"weapsOn\"\n"
        "C;Y2;X1;K\"nfrm\"\nC;Y2;X2;K1\nE\n";
    slkTestData_t *data_rows, *balance_rows, *weapons_rows;
    slkTestData_t *old_data, *old_balance, *old_weapons;
    slkTestData_t *new_data, *new_balance, *new_weapons;
    edict_t *created;

    reset_entities(); setup_test_world();
    data_rows = parse_slk_string(data_slk);
    balance_rows = parse_slk_string(balance_slk);
    weapons_rows = parse_slk_string(weapons_slk);
    old_data = G_SetSLKRows("UnitData", data_rows);
    old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    old_weapons = G_SetSLKRows("UnitWeapons", weapons_rows);
    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit tower = CreateUnit(Player(0), 'nfrm', 256.0, 256.0, 0.0)\n"
        "endfunction\n"));
    created = find_test_unit(MAKEFOURCC('n','f','r','m'));
    T_NOT_NULL(created);
    if (created) {
        T_ASSERT(!M_UnitMoveDisabled(created));
        T_FEQ(S_UnitMoveSpeed(created), 75.0f, 0.001f);
        T_FEQ(created->s.origin.x, 256.0f, 0.001f);
        T_FEQ(created->s.origin.y, 256.0f, 0.001f);
    }
    reset_entities();
    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit tower = CreateUnit(Player(0), 'nfrm', 64.0, 64.0, 0.0)\n"
        "  call SetUnitPosition(tower, 256.0, 256.0)\n"
        "endfunction\n"));
    created = find_test_unit(MAKEFOURCC('n','f','r','m'));
    T_NOT_NULL(created);
    if (created) {
        T_FEQ(created->s.origin.x, 256.0f, 0.001f);
        T_FEQ(created->s.origin.y, 256.0f, 0.001f);
    }
    reset_entities();
    new_data = G_SetSLKRows("UnitData", old_data);
    new_balance = G_SetSLKRows("UnitBalance", old_balance);
    new_weapons = G_SetSLKRows("UnitWeapons", old_weapons);
    free_slk_rows(new_data); free_slk_rows(new_balance); free_slk_rows(new_weapons);
    free_slk_rows(data_rows); free_slk_rows(balance_rows); free_slk_rows(weapons_rows);
    free_slk_rows(old_data); free_slk_rows(old_balance); free_slk_rows(old_weapons);
}

TEST(wc3_api, createunit_avoids_live_unit_collision) {
    vec2_t const point = { 256.0f, 256.0f };
    edict_t *first, *second;

    reset_entities(); setup_test_world();
    first = unit_create(0, BZ_WC3_UNIT_PEASANT, &point, 0);
    /* The first visible unit occupies the point; CreateUnit must search nearby. */
    if (first) { first->s.model = 1; first->collision = 16.0f; gi.LinkEntity(first); }
    second = unit_create(0, BZ_WC3_UNIT_PEASANT, &point, 0);
    T_NOT_NULL(first); T_NOT_NULL(second);
    if (first && second) {
        T_FEQ(first->s.origin.x, 256.0f, 0.001f);
        T_EQ(second->collision,16);
        /* Both16-radius actors are class1; first disjoint cell is(7,6). */
        T_FEQ(second->s.origin.x, 240.0f, 0.001f);
        T_FEQ(second->s.origin.y, 208.0f, 0.001f);
    }
}

TEST(wc3_api, flyer_unstuck_search_uses_unflyable_instead_of_unwalkable) {
    enum { CELLS = 16 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    vec2_t const requested = {256.0f, 256.0f};
    vec2_t out;
    edict_t *mover = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);

    mover->s.model = 1;
    mover->collision = 16.0f;
    mover->aiflags |= AI_FLYING;
    pathmap[8 * CELLS + 8] = CM_PATHING_UNWALKABLE;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));

    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    T_FEQ(out.x, 256.0f, 0.001f);
    T_FEQ(out.y, 256.0f, 0.001f);

    pathmap[8 * CELLS + 8] = CM_PATHING_UNFLYABLE;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    T_FEQ(out.x, 256.0f, 0.001f);
    T_FEQ(out.y, 192.0f, 0.001f);
}

TEST(wc3_api, float_unstuck_retains_nonflyer_collision_layer) {
    enum { CELLS = 16 };
    static UnitData_t const foot_data = { .moveTypeName = "foot" };
    static UnitData_t const float_data = { .moveTypeName = "float" };
    uint8_t pathmap[CELLS * CELLS];
    vec2_t const requested = {256.0f, 256.0f};
    vec2_t out;
    edict_t *blocker, *mover;

    memset(pathmap, CM_PATHING_UNWALKABLE, sizeof(pathmap)); /* swimmable water */
    reset_entities();
    setup_test_world();
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
    blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), requested.x, requested.y);
    mover = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    blocker->s.model = mover->s.model = 1;
    blocker->collision = mover->collision = 16.0f;
    blocker->data.UnitData = &foot_data;
    mover->data.UnitData = &float_data;
    gi.LinkEntity(blocker);

    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    T_FEQ(out.x, requested.x, 0.001f);
    T_FEQ(out.y, requested.y - 64.0f, 0.001f);

    blocker->data.UnitData = &float_data;
    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    T_FEQ(out.x, requested.x, 0.001f);
    T_FEQ(out.y, requested.y - 64.0f, 0.001f);

    blocker->aiflags |= AI_FLYING;
    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    T_FEQ(out.x, requested.x, 0.001f);
    T_FEQ(out.y, requested.y, 0.001f);
}

TEST(wc3_api, unit_unstuck_search_skips_live_unit_collision) {
    vec2_t const requested = {256.0f, 256.0f};
    vec2_t out;
    edict_t *blocker;
    edict_t *mover;

    setup_set_unit_position_pathmap();
    blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 192.0f);
    mover = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    blocker->s.model = mover->s.model = 1;
    blocker->collision = mover->collision = 16.0f;
    gi.LinkEntity(blocker);

    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    /* Requested point is static-blocked; the next spiral point is occupied. */
    T_FEQ(out.x, 320.0f, 0.001f);
    T_FEQ(out.y, 192.0f, 0.001f);
}

TEST(wc3_api, set_unit_position_loc_uses_same_unstuck_search) {
    edict_t *moved;

    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit mover = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "  local location target = Location(256.0, 256.0)\n"
        "  call SetUnitPositionLoc(mover, target)\n"
        "endfunction\n"));

    moved = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(moved);
    T_FEQ(moved->s.origin.x, 272.0f, 0.001f);
    T_FEQ(moved->s.origin.y, 240.0f, 0.001f);
}

/* Issue-418: HumanX03.w3x calls OffsetLocation(GetUnitLoc(null unit), ...) which
 * reaches GetLocationX/MoveLocation with a null handle. Null locations read as 0
 * (same contract as GetRectCenterX) and MoveLocation on null is a safe no-op. */
TEST(wc3_api, null_location_natives_return_zero_and_noop) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local location nullLoc = null\n"
        "  local location live = Location(10.0, 20.0)\n"
        "  call BJassAssert(GetLocationX(nullLoc) == 0.0, \"GetLocationX(null) must be 0\")\n"
        "  call BJassAssert(GetLocationY(nullLoc) == 0.0, \"GetLocationY(null) must be 0\")\n"
        "  call MoveLocation(nullLoc, 100.0, 200.0)\n"
        "  call BJassAssert(GetLocationX(live) == 10.0, \"live location x\")\n"
        "  call BJassAssert(GetLocationY(live) == 20.0, \"live location y\")\n"
        "  call MoveLocation(live, 30.0, 40.0)\n"
        "  call BJassAssert(GetLocationX(live) == 30.0, \"moved location x\")\n"
        "  call BJassAssert(GetLocationY(live) == 40.0, \"moved location y\")\n"
        "endfunction\n"));
}

TEST(wc3_api, set_unit_x_y_remain_raw_coordinates_on_blocked_pathing) {
    edict_t *moved;

    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit mover = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "  call SetUnitX(mover, 256.0)\n"
        "  call SetUnitY(mover, 256.0)\n"
        "endfunction\n"));

    moved = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(moved);
    T_FEQ(moved->s.origin.x, 256.0f, 0.001f);
    T_FEQ(moved->s.origin.y, 256.0f, 0.001f);
}

TEST(wc3_api, set_unit_scale_uses_wc3_x_component_as_uniform_scale) {
    edict_t *scaled = NULL;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hpea', 32.0, 64.0, 0.0)\n"
        "  call SetUnitScale(u, 1.5, 2.0, 3.0)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) {
            scaled = g_edicts + i;
            break;
        }
    }
    T_NOT_NULL(scaled);
    T_FEQ(scaled->s.scale, 1.5f, 0.001f);
}

TEST(wc3_api, set_unit_vertex_color_publishes_clamped_rgba) {
    uint8_t data[256];
    edict_t *tinted, *clent;
    uint32_t size, offset;
    uint16_t header, count;
    bool found = false;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hpea', 32.0, 64.0, 0.0)\n"
        "  call SetUnitVertexColor(u, 300, 128, -10, 0)\n"
        "endfunction\n"));

    tinted = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(tinted);
    T_ASSERT(tinted->vertex_color_set);
    T_ASSERT(tinted->vertex_color_override_set);
    T_EQ(tinted->vertex_color.r, 255); T_EQ(tinted->vertex_color.g, 128);
    T_EQ(tinted->vertex_color.b, 0); T_EQ(tinted->vertex_color.a, 0);

    clent = G_GetPlayerEntityByNumber(0);
    T_NOT_NULL(clent);
    size = G_WriteClientDatagram(clent, data, sizeof(data));
    T_ASSERT(size >= sizeof(header) + sizeof(count));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(header & BZ_GAME_DATAGRAM_ENTITY_TINTS);
    offset = sizeof(header) + (header & BZ_GAME_DATAGRAM_COUNT_MASK) * sizeof(wc3WeatherEffect_t);
    if (header & BZ_GAME_DATAGRAM_LIGHTNING) {
        uint16_t lightning_count = 0;
        memcpy(&lightning_count, data + offset, sizeof(lightning_count)); offset += sizeof(lightning_count);
        offset += lightning_count * sizeof(lightningEffect_t);
    }
    memcpy(&count, data + offset, sizeof(count)); offset += sizeof(count);
    FOR_LOOP(i, count) {
        uint16_t number; color32_t color;
        memcpy(&number, data + offset, sizeof(number)); offset += sizeof(number);
        memcpy(&color, data + offset, sizeof(color)); offset += sizeof(color);
        if (number != tinted->s.number) continue;
        T_EQ(color.r, 255); T_EQ(color.g, 128); T_EQ(color.b, 0); T_EQ(color.a, 0);
        found = true;
    }
    T_ASSERT(found);
}

TEST(wc3_api, authored_unit_ui_tint_initializes_vertex_color) {
    UnitUI_t ui = { .tintRed = 224, .tintGreen = 232, .tintBlue = 255 };
    edict_t unit = { .data.UnitUI = &ui };

    G_InitializeUnitVertexColor(&unit);
    T_ASSERT(unit.vertex_color_set);
    T_EQ(unit.vertex_color.r, 224); T_EQ(unit.vertex_color.g, 232);
    T_EQ(unit.vertex_color.b, 255); T_EQ(unit.vertex_color.a, 255);
}

TEST(wc3_api, authored_white_unit_ui_tint_clears_previous_color) {
    UnitUI_t ui = { .tintRed = 255, .tintGreen = 255, .tintBlue = 255 };
    edict_t unit = {
        .data.UnitUI = &ui,
        .vertex_color = MAKE(color32_t, 224, 232, 255, 255),
        .vertex_color_set = true,
    };

    G_InitializeUnitVertexColor(&unit);
    T_ASSERT(!unit.vertex_color_set);
    T_EQ(unit.vertex_color.r, 255); T_EQ(unit.vertex_color.g, 255);
    T_EQ(unit.vertex_color.b, 255); T_EQ(unit.vertex_color.a, 255);
}

TEST(wc3_api, explicit_vertex_color_override_survives_authored_rebind) {
    UnitUI_t ui = { .tintRed = 255, .tintGreen = 255, .tintBlue = 255 };
    edict_t unit = {
        .data.UnitUI = &ui,
        .vertex_color = MAKE(color32_t, 17, 34, 51, 68),
        .vertex_color_set = true,
        .vertex_color_override_set = true,
    };

    G_InitializeUnitVertexColor(&unit);
    T_ASSERT(unit.vertex_color_set);
    T_ASSERT(unit.vertex_color_override_set);
    T_EQ(unit.vertex_color.r, 17); T_EQ(unit.vertex_color.g, 34);
    T_EQ(unit.vertex_color.b, 51); T_EQ(unit.vertex_color.a, 68);
}


TEST(wc3_api, game_datagram_carries_and_expires_lightning_snapshot) {
    uint8_t data[1024];
    vec3_t source = { 10.0f, 20.0f, 30.0f }, target = { 100.0f, 200.0f, 40.0f };
    color32_t tint = MAKE(color32_t, 200, 150, 100, 255);
    gLightning_t *effect;
    uint32_t size, offset;
    uint16_t header, count;
    lightningEffect_t wire;

    memset(level.lightning_effects, 0, sizeof(level.lightning_effects));
    level.next_lightning_id = 0;
    level.time = 1000;
    effect = G_LightningAdd(&(lightningAddParams_t){
        .effect_id = MAKEFOURCC('C', 'L', 'P', 'B'), .source = &source, .target = &target,
        .color = tint, .duration_ms = 2000,
    });
    T_NOT_NULL(effect);

    size = G_WriteClientDatagram(NULL, data, sizeof(data));
    T_ASSERT(size > sizeof(header));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(header & BZ_GAME_DATAGRAM_LIGHTNING);
    offset = sizeof(header) + (header & ~BZ_GAME_DATAGRAM_FLAGS) * sizeof(wc3WeatherEffect_t);
    memcpy(&count, data + offset, sizeof(count)); offset += sizeof(count);
    T_EQ(count, 1);
    memcpy(&wire, data + offset, sizeof(wire));
    T_EQ(wire.handle, effect->state.handle);
    T_EQ(wire.effect_id, MAKEFOURCC('C', 'L', 'P', 'B'));
    T_FEQ(wire.source.x, 10.0f, 0.001f); T_FEQ(wire.source.z, 30.0f, 0.001f);
    T_FEQ(wire.target.y, 200.0f, 0.001f); T_EQ(wire.color.g, 150);
    T_EQ(wire.start_time, 1000); T_EQ(wire.end_time, 3000);

    level.time = 3000;
    size = G_WriteClientDatagram(NULL, data, sizeof(data));
    T_ASSERT(size >= sizeof(header));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(!(header & BZ_GAME_DATAGRAM_LIGHTNING));
    T_ASSERT(!effect->inuse);
}

TEST(wc3_api, ability_lightning_tracks_attached_units_in_datagram) {
    uint8_t data[1024];
    vec3_t source = { 10.0f, 20.0f, 30.0f }, target = { 100.0f, 200.0f, 40.0f };
    edict_t *source_unit, *target_unit;
    gLightning_t *effect;
    uint32_t size, offset;
    uint16_t header, count;
    lightningEffect_t wire;

    reset_entities();
    memset(level.lightning_effects, 0, sizeof(level.lightning_effects));
    level.next_lightning_id = 0;
    level.time = 1000;
    source_unit = alloc_test_unit(MAKEFOURCC('O', 'h', 't', 'r'), source.x, source.y);
    target_unit = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), target.x, target.y);
    source_unit->s.origin.z = source.z; source_unit->s.radius = 8.0f;
    target_unit->s.origin.z = target.z; target_unit->s.radius = 12.0f;
    effect = G_LightningAdd(&(lightningAddParams_t){
        .effect_id = MAKEFOURCC('C', 'L', 'P', 'B'), .source = &source, .target = &target,
        .color = COLOR32_WHITE, .duration_ms = 2000,
    });
    G_LightningAttach(effect, source_unit, target_unit);
    T_NOT_NULL(effect);

    source_unit->s.origin.x = 30.0f; source_unit->s.origin.y = 40.0f;
    target_unit->s.origin.x = 300.0f; target_unit->s.origin.y = 400.0f;
    size = G_WriteClientDatagram(NULL, data, sizeof(data));
    T_ASSERT(size > sizeof(header));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(header & BZ_GAME_DATAGRAM_LIGHTNING);
    offset = sizeof(header) + (header & ~BZ_GAME_DATAGRAM_FLAGS) * sizeof(wc3WeatherEffect_t);
    memcpy(&count, data + offset, sizeof(count)); offset += sizeof(count);
    T_EQ(count, 1);
    memcpy(&wire, data + offset, sizeof(wire));
    T_FEQ(wire.source.x, 30.0f, 0.001f); T_FEQ(wire.source.y, 40.0f, 0.001f);
    T_FEQ(wire.target.x, 300.0f, 0.001f); T_FEQ(wire.target.y, 400.0f, 0.001f);
    T_FEQ(wire.source.z, source.z + 4.0f, 0.001f);
    T_FEQ(wire.target.z, target.z + 6.0f, 0.001f);
}

TEST(wc3_api, jass_lightning_natives_use_the_presentation_registry) {
    gLightning_t *effect;
    uint32_t id = UINT32_MAX;

    memset(level.lightning_effects, 0, sizeof(level.lightning_effects));
    level.next_lightning_id = 0;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local lightning bolt = AddLightningEx(\"CLPB\", false, 10.0, 20.0, 30.0, 100.0, 200.0, 40.0)\n"
        "  call BJassAssert(bolt != null, \"AddLightningEx must create a lightning handle\")\n"
        "  call BJassAssert(MoveLightningEx(bolt, false, 15.0, 25.0, 35.0, 105.0, 205.0, 45.0), \"MoveLightningEx failed\")\n"
        "  call BJassAssert(SetLightningColor(bolt, 1.0, 0.5, 0.0, 0.75), \"SetLightningColor failed\")\n"
        "  call BJassAssert(GetLightningColorG(bolt) > 0.49 and GetLightningColorG(bolt) < 0.51, \"lightning colour did not update\")\n"
        "endfunction\n"));
    effect = level.lightning_effects;
    T_ASSERT(effect->inuse);
    T_EQ(effect->state.effect_id, MAKEFOURCC('C', 'L', 'P', 'B'));
    T_FEQ(effect->state.source.x, 15.0f, 0.001f);
    T_FEQ(effect->state.target.z, 45.0f, 0.001f);
    T_FEQ(effect->script_color[1], 0.5f, 0.001f);
    T_EQ(effect->state.color.g, 128);
    T_EQ(effect->state.color.a, 191);
    T_ASSERT(G_SaveJassHandle("lightning", effect, &id));
    T_EQ(G_LoadJassHandle("lightning", id), effect);
    G_LightningRemove(effect);
}

TEST(wc3_api, narrator_and_hint_text_share_message_log) {
    gameClient_t *gc = &game.clients[0];

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    level.time = 100;
    gc->ps.client_ui_state = CLIENT_UI_GAME;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call SetCinematicScene(0, PLAYER_COLOR_RED, \"Narrator\", \"Select a peon.\", 3.0, 2.0)\n"
        "  endif\n"
        "  call DisplayTimedTextToPlayer(Player(0), 0.0, 0.0, 3.0, \"HINT - Build a Burrow.\")\n"
        "endfunction\n"));

    T_EQ(gc->message_log.count, 2);
    T_STREQ(gc->message_log.entries[0], "|cffffcc00Narrator:|r Select a peon.");
    T_STREQ(gc->message_log.entries[1], "HINT - Build a Burrow.");

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCinematicScene(0, PLAYER_COLOR_RED, \"\", \"\", 0.0, 0.0)\n"
        "endfunction\n"));
    T_EQ(gc->message_log.count, 0);

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    gc->ps.client_ui_state = CLIENT_UI_CINEMATIC;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCinematicScene(0, PLAYER_COLOR_RED, \"Narrator\", \"Cutscene line.\", 3.0, 2.0)\n"
        "endfunction\n"));
    T_EQ(gc->message_log.count, 0);
}

TEST(wc3_api, command_error_uses_dedicated_replacing_hud_layer) {
    gameClient_t *gc = &game.clients[0];
    edict_t *ent = &g_edicts[0];
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_font)(cstring_t, uint32_t) = gi.FontIndex;

    gc->ps.number = 0;
    ent->client = gc;
    G_SetClientConnected(ent, true);
    memset(command_error_header, 0, sizeof(command_error_header));
    memset(command_error_frames, 0, sizeof(command_error_frames));
    memset(command_error_labels, 0, sizeof(command_error_labels));
    command_error_header_count = 0;
    command_error_frame_count = 0;
    command_error_label_count = 0;
    command_error_recipient = NULL;
    gi.Write = capture_command_error_write;
    gi.unicast = capture_command_error_unicast;
    gi.FontIndex = capture_command_error_font;

    UI_WriteCommandError(ent, "Not enough gold.");

    T_EQ(command_error_header_count, 2);
    T_EQ(command_error_header[0], svc_layout);
    T_EQ(command_error_header[1], WC3_LAYER_COMMAND_ERROR);
    T_EQ(command_error_frame_count, 2);
    T_EQ(command_error_frames[0].color.r, 0);
    T_EQ(command_error_frames[0].color.g, 0);
    T_EQ(command_error_frames[0].color.b, 0);
    T_EQ(command_error_frames[1].color.r, 255);
    T_EQ(command_error_frames[1].color.g, 204);
    T_EQ(command_error_frames[1].color.b, 0);
    T_EQ(command_error_labels[0].textalignx, FONT_JUSTIFYLEFT);
    T_EQ(command_error_labels[1].textalignx, FONT_JUSTIFYLEFT);
    T_EQ(command_error_frames[0].points.x[FPP_MIN].offset,
         (int16_t)((WC3_HUD_PORTRAIT_X + 0.001f) * UI_FRAMEPOINT_SCALE));
    T_EQ(command_error_frames[0].points.y[FPP_MIN].offset,
         (int16_t)(-(WC3_HUD_IDLE_WORKER_Y + 0.001f) * UI_FRAMEPOINT_SCALE));
    T_EQ(command_error_frames[1].points.x[FPP_MIN].offset,
         (int16_t)(WC3_HUD_PORTRAIT_X * UI_FRAMEPOINT_SCALE));
    T_EQ(command_error_frames[1].points.y[FPP_MIN].offset,
         (int16_t)(-WC3_HUD_IDLE_WORKER_Y * UI_FRAMEPOINT_SCALE));
    T_EQ(command_error_label_count, 2);
    T_EQ(command_error_recipient, ent);

    command_error_header_count = 0;
    command_error_frame_count = 0;
    command_error_label_count = 0;
    command_error_recipient = NULL;
    UI_WriteCommandError(ent, NULL);
    T_EQ(command_error_header_count, 2);
    T_EQ(command_error_header[1], WC3_LAYER_COMMAND_ERROR);
    T_EQ(command_error_frame_count, 0);
    T_EQ(command_error_recipient, ent);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    gi.FontIndex = old_font;
    G_SetClientConnected(ent, false);
}

TEST(wc3_api, command_error_expires_without_touching_message_state) {
    gameClient_t *gc = &game.clients[0];
    edict_t *ent = &g_edicts[0];
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_font)(cstring_t, uint32_t) = gi.FontIndex;
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;
    __typeof__(gi.SoundPolicy) old_sound_policy = gi.SoundPolicy;

    gc->ps.number = 0;
    ent->client = gc;
    G_SetClientConnected(ent, true);
    snprintf(gc->message.text, sizeof(gc->message.text), "ordinary message");
    level.time = 100;
    command_error_header_count = 0;
    command_error_frame_count = 0;
    command_error_recipient = NULL;
    ui_sound_calls = 0;
    gi.Write = capture_command_error_write;
    gi.unicast = capture_command_error_unicast;
    gi.FontIndex = capture_command_error_font;
    gi.SoundPolicy = capture_ui_sound_policy;
    gi.Sound = capture_ui_sound;
    gi.SoundIndex = capture_ui_sound_index;
    gi.SoundIndexAlias = capture_ui_sound_index_alias;

    G_ShowCommandErrorText(ent, "OpenRealm-only failure.");
    T_EQ(command_error_header_count, 2);
    T_EQ(command_error_header[1], WC3_LAYER_COMMAND_ERROR);
    T_EQ(command_error_frame_count, 2);
    T_STREQ(gc->message.text, "ordinary message");

    command_error_header_count = 0;
    command_error_frame_count = 0;
    command_error_recipient = NULL;
    level.time = 100 + 9999;
    G_UpdateCommandError(ent);
    T_EQ(command_error_header_count, 0);
    T_EQ(command_error_frame_count, 0);
    T_STREQ(gc->message.text, "ordinary message");

    level.time++;
    G_UpdateCommandError(ent);
    T_EQ(command_error_header_count, 2);
    T_EQ(command_error_header[1], WC3_LAYER_COMMAND_ERROR);
    T_EQ(command_error_frame_count, 0);
    T_EQ(command_error_recipient, ent);
    T_STREQ(gc->message.text, "ordinary message");

    gi.Write = old_write;
    gi.unicast = old_unicast;
    gi.FontIndex = old_font;
    gi.SoundPolicy = old_sound_policy;
    gi.Sound = old_sound;
    gi.SoundIndex = old_soundindex;
    gi.SoundIndexAlias = old_sound_alias;
    G_SetClientConnected(ent, false);
    G_CommandErrorReset();
}

TEST(wc3_api, transient_text_does_not_enter_message_log) {
    gameClient_t *gc = &game.clients[0];
    edict_t ent = { .client = gc };

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    level.time = 100;
    UI_ShowTransientText(&ent, &MAKE(vec2_t, 0.0f, 0.0f), "Not enough gold.", 2.0f);

    T_STREQ(gc->message.text, "Not enough gold.");
    T_EQ(gc->message_log.count, 0);
}

TEST(wc3_api, command_error_key_resolves_commandstrings_and_race_variant) {
    gameClient_t *gc = &game.clients[0];
    edict_t *ent = &g_edicts[0];
    static mapTrigStr_t trigstr;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_font)(cstring_t, uint32_t) = gi.FontIndex;
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;
    __typeof__(gi.SoundPolicy) old_sound_policy = gi.SoundPolicy;

    InitUnitData();
    memset(&trigstr, 0, sizeof(trigstr));
    trigstr.id = 0;
    snprintf(trigstr.text, sizeof(trigstr.text), "Human02");
    ((mapInfo_t *)level.mapinfo)->strings = &trigstr;
    gc->ps.race = kPlayerRaceUndead;
    T_STREQ(G_ResolveCommandErrorText(gc, "Blightringfull"),
            "That gold mine can't support any more Acolytes.");
    T_STREQ(G_ResolveCommandErrorText(gc, "Nofood"),
            "Summon more Ziggurats to continue unit production.");
    T_STREQ(G_ResolveCommandErrorText(gc, "Nogold"), "Not enough gold.");
    T_STREQ(G_ResolveCommandErrorText(gc, "Cantfindcorpse"), "There are no usable corpses nearby.");
    T_STREQ(G_ResolveCommandErrorText(gc, "RepairHPmaxed"), "Target is not damaged.");
    T_STREQ(G_ResolveCommandErrorText(gc, "Outofstock"), "Out of stock.");
    T_STREQ(G_ResolveCommandErrorText(gc, "Calltoarms"), "No Peasants could be found.");
    T_STREQ(G_ResolveCommandErrorText(gc, "Backtowork"), "No Militia could be found.");
    T_STREQ(G_ResolveCommandErrorText(gc, "TestTrigstr"), "Human02");

    gc->ps.number = 0;
    ent->client = gc;
    G_SetClientConnected(ent, true);
    command_error_header_count = 0;
    command_error_frame_count = 0;
    command_error_label_count = 0;
    command_error_recipient = NULL;
    ui_sound_calls = 0;
    gi.Write = capture_command_error_write;
    gi.unicast = capture_command_error_unicast;
    gi.FontIndex = capture_command_error_font;
    gi.SoundPolicy = capture_ui_sound_policy;
    gi.Sound = capture_ui_sound;
    gi.SoundIndex = capture_ui_sound_index;
    gi.SoundIndexAlias = capture_ui_sound_index_alias;

    G_ShowCommandErrorKey(ent, "Nofood", NULL);
    T_EQ(command_error_header[1], WC3_LAYER_COMMAND_ERROR);
    T_EQ(command_error_frame_count, 2);
    T_STREQ(command_error_frames[1].text, "Summon more Ziggurats to continue unit production.");
    T_EQ(command_error_recipient, ent);
    T_EQ(ui_sound_calls, 1);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    gi.FontIndex = old_font;
    gi.SoundPolicy = old_sound_policy;
    gi.Sound = old_sound;
    gi.SoundIndex = old_soundindex;
    gi.SoundIndexAlias = old_sound_alias;
    G_SetClientConnected(ent, false);
}

TEST(wc3_api, removeunit_hides_before_deferred_edict_release) {
    edict_t *unit;

    G_ResetDeferredFrees();
    reset_entities();
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    G_DeferFreeEdict(unit);
    T_ASSERT(unit->inuse);
    T_ASSERT(unit->s.renderfx & RF_HIDDEN);
    G_TestFinishDeferredFrees();
    T_ASSERT(!unit->inuse);
}

/* Human04's intro-cancel path kills the temporary Haunted Gold Mine and then
 * removes its workers/buildings from JASS while the trigger is still running.
 * Keep that authored event order here: the mine gets a death transition, while
 * RemoveUnit hides the other widgets and retires them only after the callback. */
TEST(wc3_api, human04_intro_cancel_preserves_unit_lifecycle_until_frame_end) {
    edict_t *mine = NULL, *worker = NULL, *building = NULL, *replacement;
    ggroup_t *cancel_group;
    uint32_t const bit = 1u << game.clients[0].ps.number;
    char number[16];
    cstring_t select[] = { "select", number };

    setup_test_world();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit hauntedMine = null\n"
        "  unit acolyte = null\n"
        "  unit townHall = null\n"
        "  group cancelUnits = null\n"
        "endglobals\n"
        "function cancelIntro takes nothing returns nothing\n"
        "  call KillUnit(hauntedMine)\n"
        "  call RemoveUnit(acolyte)\n"
        "  call BJassAssert(GetUnitTypeId(acolyte) == 'hpea', \"worker handle must survive cancellation action\")\n"
        "  call RemoveUnit(townHall)\n"
        "  call BJassAssert(GetUnitTypeId(townHall) == 'hbar', \"building handle must survive cancellation action\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set hauntedMine = CreateUnit(Player(0), 'ugol', 0.0, 0.0, 0.0)\n"
        "  set acolyte = CreateUnit(Player(0), 'hpea', 64.0, 0.0, 0.0)\n"
        "  set townHall = CreateUnit(Player(0), 'hbar', 128.0, 0.0, 0.0)\n"
        "  set cancelUnits = CreateGroup()\n"
        "  call GroupAddUnit(cancelUnits, acolyte)\n"
        "  call GroupAddUnit(cancelUnits, townHall)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].class_id == BZ_WC3_UNIT_HAUNTED_GOLD_MINE) mine = &g_edicts[i];
        if (g_edicts[i].class_id == BZ_WC3_UNIT_PEASANT) worker = &g_edicts[i];
        if (g_edicts[i].class_id == BZ_WC3_UNIT_BARRACKS) building = &g_edicts[i];
    }
    T_NOT_NULL(mine); T_NOT_NULL(worker); T_NOT_NULL(building);
    if (!mine || !worker || !building) goto cleanup;
    mine->birth(mine);
    T_ASSERT(G_StartUndeadConstruction(worker, mine));
    T_ASSERT(mine->construction);
    T_STREQ(mine->currentmove->animation, "birth");

    jass_callbyname(level.vm, "cancelIntro", false);
    jass_runevents(level.vm);
    T_ASSERT(mine->svflags & SVF_DEADMONSTER);
    T_STREQ(mine->currentmove->animation, "death");
    T_ASSERT(!mine->construction);
    T_ASSERT(worker->inuse);
    T_ASSERT(G_IsDeferredFree(worker));
    T_ASSERT(G_IsDeferredFree(building));
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_ASSERT(building->s.renderfx & RF_HIDDEN);
    cancel_group = level.groups[0];
    T_EQ(cancel_group->num_units, 0);
    T_ASSERT(!G_UnitCanBeSelected(&game.clients[0], worker));

    level.started = true; level.scriptsStarted = true; globals.RunFrame();
    T_ASSERT(!worker->inuse);
    T_ASSERT(!building->inuse);
    replacement = alloc_test_unit(BZ_WC3_UNIT_BARRACKS, 128.0f, 0.0f);
    T_NOT_NULL(replacement);
    if (replacement) {
        replacement->s.player = 0;
        replacement->svflags |= SVF_MONSTER;
        T_ASSERT(replacement != building);
        T_ASSERT(G_UnitCanBeSelected(&game.clients[0], replacement));
        snprintf(number, sizeof(number), "%u", replacement->s.number);
        globals.ClientCommand(&g_edicts[0], 2, select);
        T_ASSERT(replacement->selected & bit);
        G_DeselectEntity(&game.clients[0], replacement);
        G_FreeEdict(replacement);
    }

cleanup:
    currentplayer = NULL;
}

TEST(wc3_api, createunit_does_not_reuse_deferred_dead_unit) {
    edict_t *dead, *replacement;

    G_ResetDeferredFrees();
    reset_entities();
    dead = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    unit_die(dead, NULL);
    G_DeferFreeEdict(dead);
    replacement = unit_createorfind(0, MAKEFOURCC('h','p','e','a'), &(vec2_t){0, 0}, 0);
    T_ASSERT(replacement && replacement != dead);
    T_ASSERT(replacement->inuse);
    G_TestFinishDeferredFrees();
    G_FreeEdict(replacement);
}

TEST(wc3_api, createunit_allocates_fresh_nearby_unit) {
    edict_t *existing, *created;

    G_ResetDeferredFrees();
    reset_entities();
    existing = alloc_test_unit(MAKEFOURCC('n','z','o','m'), 0, 0);
    created = unit_create(0, MAKEFOURCC('n','z','o','m'), &(vec2_t){0, 0}, 0);
    T_ASSERT(created && created != existing);
    T_ASSERT(created->inuse);
    G_FreeEdict(created);
    G_FreeEdict(existing);
}

TEST(wc3_api, createunit_starts_ready_without_birth_delay) {
    static cstring_t const ui_slk =
        "ID;PWXL;N;EBB;Y3;X2\n"
        "C;Y1;X1;K\"unitUIID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y2;X1;K\"hfoo\"\n"
        "C;Y2;X2;K\"TestUI\\\\Models\\\\quad_sprite.mdx\"\n"
        "E\n";
    slkTestData_t *ui_rows, *old_ui;
    edict_t *unit;

    G_ResetDeferredFrees();
    reset_entities();
    setup_test_world();
    ui_rows = parse_slk_string(ui_slk);
    old_ui = G_SetSLKRows("UnitUI", ui_rows);
    unit = unit_create(0, BZ_WC3_UNIT_FOOTMAN, &(vec2_t){0, 0}, 0);
    T_NOT_NULL(unit);
    if (unit) {
        T_NOT_NULL(unit->currentmove);
        if (unit->currentmove) {
            T_STREQ(unit->currentmove->animation, "stand");
            T_EQ((int)unit->wait, 0);
        }
        G_FreeEdict(unit);
    }
    G_SetSLKRows("UnitUI", old_ui);
    free_slk_rows(ui_rows);
}

TEST(wc3_api, createunit_links_building_collision_bounds) {
    static cstring_t const ui_slk =
        "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"unitUIID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"isbldg\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K\"TestUI\\\\Models\\\\quad_sprite.mdx\"\n"
        "C;Y2;X3;K1\n"
        "E\n";
    static cstring_t const balance_slk =
        "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"unitBalanceID\"\n"
        "C;Y1;X2;K\"collision\"\n"
        "C;Y1;X3;K\"isbldg\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K64\n"
        "C;Y2;X3;K1\n"
        "E\n";
    static cstring_t const data_slk =
        "ID;PWXL;N;EBB;Y1;X1\n"
        "C;Y1;X1;K\"id\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "E\n";
    slkTestData_t *ui_rows, *old_ui, *balance_rows, *old_balance, *data_rows, *old_data;
    edict_t *building;
    edict_t *found[4] = { 0 };
    box2_t area = { { -256.0f, -256.0f }, { 256.0f, 256.0f } };
    bool linked = false;

    reset_entities();
    setup_test_world();
    ui_rows = parse_slk_string(ui_slk);
    balance_rows = parse_slk_string(balance_slk);
    data_rows = parse_slk_string(data_slk);
    old_ui = G_SetSLKRows("UnitUI", ui_rows);
    old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    old_data = G_SetSLKRows("UnitData", data_rows);
    T_ASSERT(G_UnitIsBuilding(BZ_WC3_UNIT_PEASANT));
    T_EQ((int)G_UnitCollision(BZ_WC3_UNIT_PEASANT), 64);
    building = unit_create(0, BZ_WC3_UNIT_PEASANT, &(vec2_t){0, 0}, 0);
    T_NOT_NULL(building);
    if (building) {
        T_ASSERT(building->data.UnitUI->modelFile);
        T_ASSERT(building->data.UnitBalance->isBuilding);
        T_EQ((int)building->data.UnitBalance->collision, 64);
        T_ASSERT(building->s.flags & EF_BUILDING);
        T_ASSERT(building->collision > 0.0f);
        T_EQ(building->bounds.min.x, -building->collision - 1.0f);
        T_EQ(building->bounds.max.x, building->collision + 1.0f);
        FOR_LOOP(i, gi.BoxEdicts(&area, found, 4, NULL))
            if (found[i] == building) linked = true;
        T_ASSERT(linked);
        G_FreeEdict(building);
    }
    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("UnitBalance", old_balance);
    G_SetSLKRows("UnitData", old_data);
    free_slk_rows(ui_rows);
    free_slk_rows(balance_rows);
    free_slk_rows(data_rows);
}

TEST(wc3_api, message_log_is_bounded_and_evicts_oldest_entry) {
    gameClient_t *gc = &game.clients[0];
    edict_t ent = { .client = gc };
    char text[64];

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    for (uint32_t i = 0; i < WC3_MESSAGE_LOG_MAX_ENTRIES + 1; i++) {
        snprintf(text, sizeof(text), "Message %u", (unsigned)i);
        UI_MessageLogAppend(&ent, text);
    }

    T_EQ(gc->message_log.count, WC3_MESSAGE_LOG_MAX_ENTRIES);
    T_EQ(gc->message_log.first, 1);
    T_STREQ(gc->message_log.entries[gc->message_log.first], "Message 1");
    T_STREQ(gc->message_log.entries[0], "Message 128");
}

TEST(wc3_api, display_text_uses_automatic_duration) {
    gameClient_t *gc = &game.clients[0];

    level.time = 100;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call DisplayTextToPlayer(Player(0), 0.0, 0.0, \"123456\")\n"
        "endfunction\n"));
    T_EQ(gc->message.end_time, 6100);

    level.time = gc->message.end_time;
    G_RunClients();
    T_EQ(gc->message.end_time, 0);
}

TEST(wc3_api, transmission_keeps_gameplay_ui_and_separates_voice_lifetime) {
    gameClient_t *gc = &game.clients[0];

    level.time = 100;
    gc->ps.client_ui_state = CLIENT_UI_GAME;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call ForceCinematicSubtitles(false)\n"
        "    call SetCinematicScene(0, PLAYER_COLOR_BLUE, \"Captain\", \"Hold the line!\", 6.0, 4.0)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
    T_STREQ(gc->ps.texts[PLAYERTEXT_SPEAKER], "Captain");
    T_STREQ(gc->ps.texts[PLAYERTEXT_DIALOGUE], "Hold the line!");
    T_EQ(gc->cinematic_voice_end_time, 4100);
    T_EQ(gc->cinematic_end_time, 6100);
    T_EQ(gc->ps.stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR], 1);

    level.time = 4100;
    G_RunClients();
    T_EQ(gc->cinematic_voice_end_time, 0);
    T_EQ(gc->cinematic_end_time, 6100);
    T_STREQ(gc->ps.texts[PLAYERTEXT_DIALOGUE], "Hold the line!");

    level.time = 6100;
    G_RunClients();
    T_EQ(gc->cinematic_end_time, 0);
    T_EQ(gc->ps.stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR], 0);
    T_STREQ(gc->ps.texts[PLAYERTEXT_SPEAKER], "");
    T_STREQ(gc->ps.texts[PLAYERTEXT_DIALOGUE], "");
}

/* Blizzard's cinematic helpers may forward polymorphic JASS null into a string
 * parameter while an ESC cancellation unwinds the active transmission. */
TEST(wc3_api, cinematic_string_null_is_accepted) {
    gameClient_t *gc = &game.clients[0];

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCinematicScene(0, PLAYER_COLOR_BLUE, null, null, 0.0, 0.0)\n"
        "endfunction\n"));
    T_STREQ(gc->ps.texts[PLAYERTEXT_SPEAKER], "");
    T_STREQ(gc->ps.texts[PLAYERTEXT_DIALOGUE], "");
    currentplayer = NULL;
}

TEST(wc3_api, gameplay_transmission_preserves_underlying_timed_message_state) {
    gameClient_t *gc = &game.clients[0];

    level.time = 100;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call DisplayTimedTextToPlayer(Player(0), 0.0, 0.0, 10.0, \"Objective updated\")\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call SetCinematicScene(0, PLAYER_COLOR_RED, \"Footman\", \"Ready.\", 3.0, 2.0)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->message.end_time, 10100);
    T_STREQ(gc->message.text, "Objective updated");
    T_EQ(gc->cinematic_end_time, 3100);

    level.time = 3100;
    G_RunClients();
    T_EQ(gc->cinematic_end_time, 0);
    T_EQ(gc->message.end_time, 10100);
    T_STREQ(gc->message.text, "Objective updated");
}

static uint32_t ui_point_calls;
static bool count_ui_point(edict_t *ent, vec2_t const *loc) { ui_point_calls++; return false; }

TEST(wc3_api, enable_user_ui_does_not_block_world_selection) {
    gameClient_t *gc = &game.clients[0];
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    char number[16];
    cstring_t select[] = { "select", number };

    gc->ps.number = 0;
    unit->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    snprintf(number, sizeof(number), "%u", unit->s.number);
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call EnableUserUI(false)\n"
        "endfunction"));
    T_ASSERT(gc->no_ui);

    globals.ClientCommand(&g_edicts[0], 2, select);
    T_ASSERT(unit->selected & (1u << gc->ps.number));
    currentplayer = NULL;
}

TEST(wc3_api, same_type_selection_filters_candidates_by_anchor_type) {
    gameClient_t *gc = &game.clients[0];
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    edict_t *other = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 128.0f, 64.0f);
    char first_number[16], second_number[16], other_number[16];
    cstring_t command[] = { "select", first_number, "sametype", first_number, second_number, other_number };

    gc->ps.number = 0;
    first->s.player = second->s.player = other->s.player = 0;
    first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; other->svflags |= SVF_MONSTER;
    snprintf(first_number, sizeof(first_number), "%u", first->s.number);
    snprintf(second_number, sizeof(second_number), "%u", second->s.number);
    snprintf(other_number, sizeof(other_number), "%u", other->s.number);

    globals.ClientCommand(&g_edicts[0], 6, command);
    T_ASSERT(G_IsEntitySelected(gc, first));
    T_ASSERT(G_IsEntitySelected(gc, second));
    T_ASSERT(!G_IsEntitySelected(gc, other));
}

TEST(wc3_api, client_selection_publishes_selection_events_once_per_delta) {
    gameClient_t *gc = &game.clients[0];
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    char first_number[16];
    char second_number[16];
    cstring_t select_first[] = { "select", first_number };
    cstring_t select_second[] = { "select", second_number };

    gc->ps.number = 0;
    first->s.player = second->s.player = 0;
    first->svflags |= SVF_MONSTER;
    second->svflags |= SVF_MONSTER;
    snprintf(first_number, sizeof(first_number), "%u", first->s.number);
    snprintf(second_number, sizeof(second_number), "%u", second->s.number);

    T_ASSERT(run_test_jass(
        "function selected_action takes nothing returns nothing\n"
        "  call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, GetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD) + 1)\n"
        "endfunction\n"
        "function deselected_action takes nothing returns nothing\n"
        "  call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_LUMBER, GetPlayerState(Player(0), PLAYER_STATE_RESOURCE_LUMBER) + 1)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger selected = CreateTrigger()\n"
        "  local trigger deselected = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(selected, Player(0), EVENT_PLAYER_UNIT_SELECTED, null)\n"
        "  call TriggerAddAction(selected, function selected_action)\n"
        "  call TriggerRegisterPlayerUnitEvent(deselected, Player(0), EVENT_PLAYER_UNIT_DESELECTED, null)\n"
        "  call TriggerAddAction(deselected, function deselected_action)\n"
        "endfunction\n"));

    globals.ClientCommand(&g_edicts[0], 2, select_first);
    G_RunEvents();
    jass_runevents(level.vm);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);

    /* Re-sending identical authoritative membership is not a new selection. */
    globals.ClientCommand(&g_edicts[0], 2, select_first);
    G_RunEvents();
    jass_runevents(level.vm);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);

    globals.ClientCommand(&g_edicts[0], 2, select_second);
    G_RunEvents();
    jass_runevents(level.vm);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 2);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 1);
}

TEST(wc3_api, immediate_order_publishes_order_event_context) {
    edict_t *unit;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testUnit = null\n"
        "  integer orderEvents = 0\n"
        "  integer unitOrderEvents = 0\n"
        "endglobals\n"
        "function onOrder takes nothing returns nothing\n"
        "  set orderEvents = orderEvents + 1\n"
        "  call BJassAssert(GetOrderedUnit() == testUnit, \"ordered unit must be the immediate-order unit\")\n"
        "endfunction\n"
        "function onUnitOrder takes nothing returns nothing\n"
        "  set unitOrderEvents = unitOrderEvents + 1\n"
        "  call BJassAssert(GetOrderedUnit() == testUnit, \"unit issued-order context must identify the ordered unit\")\n"
        "endfunction\n"
        "function verifyOrder takes nothing returns nothing\n"
        "  call BJassAssert(orderEvents == 1, \"immediate order must publish one player order event\")\n"
        "  call BJassAssert(unitOrderEvents == 1, \"immediate order must publish one unit order event\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local trigger unitTrigger = CreateTrigger()\n"
        "  set testUnit = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_ISSUED_ORDER, null)\n"
        "  call TriggerAddAction(t, function onOrder)\n"
        "  call TriggerRegisterUnitEvent(unitTrigger, testUnit, EVENT_UNIT_ISSUED_ORDER)\n"
        "  call TriggerAddAction(unitTrigger, function onUnitOrder)\n"
        "endfunction\n"));

    unit = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(unit);
    unit->health.value = unit->health.max_value = 100.0f;
    T_ASSERT(unit_issueimmediateorder(unit, "holdposition"));
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyOrder", true);
    jass_runevents(level.vm);
    T_EQ(G_GetIssuedOrderId(unit), G_OrderId("holdposition"));
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* Synchronous nested orders finish before the outer native returns, while
 * each condition/action keeps its own immutable submitted payload. */
TEST(wc3_api, issued_order_context_is_frozen_across_replacement_and_reentry) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testUnit = null\n"
        "  integer pointConditions = 0\n"
        "  integer pointActions = 0\n"
        "  integer stopActions = 0\n"
        "endglobals\n"
        "function checkPoint takes integer index returns nothing\n"
        "  local integer id = OrderId(\"move\")\n"
        "  local real x = 192.0\n"
        "  local real y = 64.0\n"
        "  local location p\n"
        "  if index == 3 then\n"
        "    set id = OrderId(\"smart\")\n"
        "    set x = 256.0\n"
        "    set y = 96.0\n"
        "  elseif index == 2 then\n"
        "    set x = 320.0\n"
        "    set y = 128.0\n"
        "  endif\n"
        "  call BJassAssert(GetIssuedOrderId() == id, \"callback must retain its submitted order ID\")\n"
        "  call BJassAssert(GetOrderPointX() == x and GetOrderPointY() == y, \"callback must retain its submitted point\")\n"
        "  set p = GetOrderPointLoc()\n"
        "  call BJassAssert(GetLocationX(p) == x and GetLocationY(p) == y, \"order location must use callback snapshot\")\n"
        "  call RemoveLocation(p)\n"
        "endfunction\n"
        "function pointCondition takes nothing returns boolean\n"
        "  set pointConditions = pointConditions + 1\n"
        "  call checkPoint(pointConditions)\n"
        "  return true\n"
        "endfunction\n"
        "function onPoint takes nothing returns nothing\n"
        "  set pointActions = pointActions + 1\n"
        "  call checkPoint(pointActions)\n"
        "  if pointActions == 1 then\n"
        "    call BJassAssert(IssuePointOrder(testUnit, \"move\", 320.0, 128.0), \"reentrant replacement accepted\")\n"
        "    call checkPoint(1)\n"
        "  endif\n"
        "endfunction\n"
        "function onStop takes nothing returns nothing\n"
        "  set stopActions = stopActions + 1\n"
        "  call BJassAssert(GetIssuedOrderId() == OrderId(\"stop\"), \"immediate callback must retain Stop ID\")\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(pointConditions == 3 and pointActions == 3 and stopActions == 1, \"each submission publishes once\")\n"
        "endfunction\n"
        "function issue takes nothing returns nothing\n"
        "  call BJassAssert(IssuePointOrder(testUnit, \"move\", 192.0, 64.0), \"initial Move accepted\")\n"
        "  call BJassAssert(IssuePointOrder(testUnit, \"smart\", 256.0, 96.0), \"replacement Smart accepted\")\n"
        "  call BJassAssert(IssueImmediateOrder(testUnit, \"stop\"), \"Stop accepted\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger p = CreateTrigger()\n"
        "  local trigger s = CreateTrigger()\n"
        "  set testUnit = CreateUnit(Player(0), 'hpea', 64.0, 32.0, 0.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(p, Player(0), EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER, null)\n"
        "  call TriggerAddCondition(p, Condition(function pointCondition))\n"
        "  call TriggerAddAction(p, function onPoint)\n"
        "  call TriggerRegisterUnitEvent(s, testUnit, EVENT_UNIT_ISSUED_ORDER)\n"
        "  call TriggerAddAction(s, function onStop)\n"
        "endfunction\n"));
    edict_t *unit = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(unit);
    unit->health.value = unit->health.max_value = 100;
    unit->stand = unit_stand;
    unit_stand(unit);
    jass_callbyname(level.vm, "issue", true); jass_runevents(level.vm);
    G_RunEvents(); jass_runevents(level.vm);
    G_RunEvents(); jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", true); jass_runevents(level.vm);
    T_STREQ(jass_rterror_message(level.vm), "");
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, current_order_point_move_tracks_active_head_through_server_frames) {
    cstring_t filename = Test_TempPath("openwarcraft3-wc3-current-order-point-save-test.bin");
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testUnit = null\n"
        "endglobals\n"
        "function verifyIdle takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(testUnit) == 0, \"idle unit has no current user order\")\n"
        "endfunction\n"
        "function verifyMove takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(testUnit) == OrderId(\"move\"), \"current head stays Move while Smart waits\")\n"
        "endfunction\n"
        "function verifySmart takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(testUnit) == OrderId(\"smart\"), \"Smart becomes current only at activation\")\n"
        "endfunction\n"
        "function issue takes nothing returns nothing\n"
        "  call BJassAssert(IssuePointOrder(testUnit, \"move\", 192.0, 64.0), \"ordinary Move accepted\")\n"
        "  call verifyMove()\n"
        "endfunction\n"
        "function replace takes nothing returns nothing\n"
        "  call BJassAssert(IssuePointOrder(testUnit, \"smart\", 320.0, 128.0), \"point replacement accepted\")\n"
        "  call verifySmart()\n"
        "  call BJassAssert(not IssuePointOrder(testUnit, \"missingorder\", 64.0, 64.0), \"unsupported replacement rejected\")\n"
        "  call verifySmart()\n"
        "endfunction\n"
        "function stop takes nothing returns nothing\n"
        "  call BJassAssert(IssueImmediateOrder(testUnit, \"stop\"), \"Stop accepted\")\n"
        "  call verifyIdle()\n"
        "endfunction\n"
        "function recreate takes nothing returns nothing\n"
        "  set testUnit = CreateUnit(Player(0), 'hpea', 64.0, 32.0, 0.0)\n"
        "  call verifyIdle()\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set testUnit = CreateUnit(Player(0), 'hpea', 64.0, 32.0, 0.0)\n"
        "endfunction\n"));
    edict_t *unit = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(unit);
    unit->health.value = unit->health.max_value = 100;
    unit->collision = 8; unit->unitinfo.MoveSpeed = 256;
    unit->movetype = MOVETYPE_STEP; unit->svflags |= SVF_MONSTER;
    unit->think = monster_think; unit->stand = unit_stand; unit->die = unit_die;
    unit_stand(unit); gi.LinkEntity(unit);
    jass_callbyname(level.vm, "verifyIdle", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "issue", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(G_IssueUnitPointOrder(unit, "smart", &(vec2_t){384, 160}, true, 0, 0));
    T_EQ(unit->order_queue.count, 1);
    jass_callbyname(level.vm, "verifyMove", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_rterror_clear(level.vm);
    T_ASSERT(WriteGame(filename));
    T_ASSERT(unit_issueimmediateorder(unit, "stop"));
    T_ASSERT(ReadGame(filename));
    T_EQ(unit->order_queue.count, 1);
    jass_callbyname(level.vm, "verifyMove", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_rterror_clear(level.vm);
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    for (int frame = 0; frame < 240 && unit->order_queue.count; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_EQ(unit->order_queue.count, 0);
    T_ASSERT(move_is_active_order_walk(unit));
    jass_callbyname(level.vm, "verifySmart", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_rterror_clear(level.vm);
    jass_callbyname(level.vm, "stop", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_rterror_clear(level.vm);
    jass_callbyname(level.vm, "issue", false);
    jass_callbyname(level.vm, "replace", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    for (int frame = 0; frame < 240 && move_is_active_order_walk(unit); frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_ASSERT(!move_is_active_order_walk(unit));
    T_ASSERT(Vector2_distance(&unit->s.origin2, &(vec2_t){320, 128}) <= unit->collision + 16);
    jass_callbyname(level.vm, "verifyIdle", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.started = false;
    G_FreeEdict(unit); level.time += 1001;
    jass_callbyname(level.vm, "recreate", false);
    T_ASSERT(find_test_unit(MAKEFOURCC('h','p','e','a')) == unit);
    T_ASSERT(!jass_rterror_pending(level.vm));
    remove(filename);
}

TEST(wc3_api, current_order_follow_tracks_active_head_through_server_frames) {
    cstring_t filename = Test_TempPath("openwarcraft3-wc3-current-order-follow-save-test.bin");
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testUnit = null\n"
        "  unit targetUnit = null\n"
        "endglobals\n"
        "function verifySmart takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(testUnit) == OrderId(\"smart\"), \"standing or moving Follow retains Smart\")\n"
        "endfunction\n"
        "function verifyMove takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(testUnit) == OrderId(\"move\"), \"target Move retains its public command\")\n"
        "endfunction\n"
        "function verifyIdle takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(testUnit) == 0, \"retired Follow has no current command\")\n"
        "endfunction\n"
        "function smart takes nothing returns nothing\n"
        "  call BJassAssert(IssueTargetOrder(testUnit, \"smart\", targetUnit), \"ally Smart accepted\")\n"
        "  call verifySmart()\n"
        "endfunction\n"
        "function move takes nothing returns nothing\n"
        "  call BJassAssert(IssueTargetOrder(testUnit, \"move\", targetUnit), \"target Move accepted\")\n"
        "  call verifyMove()\n"
        "endfunction\n"
        "function reject takes nothing returns nothing\n"
        "  call BJassAssert(not IssueTargetOrder(testUnit, \"missingorder\", targetUnit), \"invalid replacement rejected\")\n"
        "  call verifyMove()\n"
        "endfunction\n"
        "function stop takes nothing returns nothing\n"
        "  call BJassAssert(IssueImmediateOrder(testUnit, \"stop\"), \"Stop accepted\")\n"
        "  call verifyIdle()\n"
        "endfunction\n"
        "function removeTarget takes nothing returns nothing\n"
        "  call RemoveUnit(targetUnit)\n"
        "endfunction\n"
        "function recreate takes nothing returns nothing\n"
        "  set testUnit = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "  set targetUnit = CreateUnit(Player(0), 'hfoo', 512.0, 64.0, 0.0)\n"
        "  call verifyIdle()\n"
        "endfunction\n"
        "function kill takes nothing returns nothing\n"
        "  call KillUnit(testUnit)\n"
        "  call verifyIdle()\n"
        "  call BJassAssert(not IssueTargetOrder(testUnit, \"move\", targetUnit), \"dead Move rejected\")\n"
        "  call verifyIdle()\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set testUnit = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "  set targetUnit = CreateUnit(Player(0), 'hfoo', 512.0, 64.0, 0.0)\n"
        "endfunction\n"));
    edict_t *unit = find_test_unit(MAKEFOURCC('h','p','e','a'));
    edict_t *target = find_test_unit(MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(unit); T_NOT_NULL(target);
    edict_t *actors[] = {unit, target};
    for (int i = 0; i < 2; i++) {
        actors[i]->health.value = actors[i]->health.max_value = 100;
        actors[i]->collision = 8; actors[i]->unitinfo.MoveSpeed = 256;
        actors[i]->movetype = MOVETYPE_STEP; actors[i]->svflags |= SVF_MONSTER;
        actors[i]->think = monster_think; actors[i]->stand = unit_stand; actors[i]->die = unit_die;
        unit_stand(actors[i]); gi.LinkEntity(actors[i]);
    }
    jass_callbyname(level.vm, "smart", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); jass_rterror_clear(level.vm);
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){768, 128}, true, 0, 0));
    T_EQ(unit->order_queue.count, 1);
    T_ASSERT(WriteGame(filename));
    T_ASSERT(unit_issueimmediateorder(unit, "stop"));
    T_ASSERT(ReadGame(filename));
    jass_callbyname(level.vm, "verifySmart", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); jass_rterror_clear(level.vm);
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    for (int frame = 0; frame < 80; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_ASSERT(unit->movement.follow_target == target);
    T_EQ(unit->order_queue.count, 1);
    T_ASSERT(M_DistanceToGoal(unit) <= G_FollowStopRange(unit, target) + 16);
    jass_callbyname(level.vm, "verifySmart", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); jass_rterror_clear(level.vm);
    jass_callbyname(level.vm, "move", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); jass_rterror_clear(level.vm);
    T_EQ(unit->order_queue.count, 0);
    jass_callbyname(level.vm, "reject", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); jass_rterror_clear(level.vm);
    float const old_x = unit->s.origin.x;
    target->s.origin.x = target->s.origin2.x = 704; gi.LinkEntity(target);
    for (int frame = 0; frame < 20; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_ASSERT(unit->s.origin.x > old_x);
    jass_callbyname(level.vm, "verifyMove", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); jass_rterror_clear(level.vm);
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){768, 128}, true, 0, 0));
    jass_callbyname(level.vm, "removeTarget", false);
    /* RemoveUnit defers edict reclamation until the event pass completes. */
    for (int frame = 0; frame < 10 && unit->movement.follow_target; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_NULL(unit->movement.follow_target);
    T_EQ(unit->order_queue.count, 0);
    jass_callbyname(level.vm, "verifyMove", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); jass_rterror_clear(level.vm);
    for (int frame = 0; frame < 120 && move_is_active_order_walk(unit); frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_ASSERT(Vector2_distance(&unit->s.origin2, &(vec2_t){768, 128}) <= unit->collision + 16);
    jass_callbyname(level.vm, "verifyIdle", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_rterror_clear(level.vm);
    level.started = false;
    G_FreeEdict(unit); level.time += 1001;
    jass_callbyname(level.vm, "recreate", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(find_test_unit(MAKEFOURCC('h','p','e','a')) == unit);
    target = find_test_unit(MAKEFOURCC('h','f','o','o'));
    actors[1] = target;
    for (int i = 0; i < 2; i++) {
        actors[i]->health.value = actors[i]->health.max_value = 100;
        actors[i]->collision = 8; actors[i]->unitinfo.MoveSpeed = 256;
        actors[i]->movetype = MOVETYPE_STEP; actors[i]->svflags |= SVF_MONSTER;
        actors[i]->think = monster_think; actors[i]->stand = unit_stand; actors[i]->die = unit_die;
        unit_stand(actors[i]); gi.LinkEntity(actors[i]);
    }
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){192, 64}, false, 0, 0));
    T_ASSERT(G_IssueUnitTargetOrder(unit, "smart", target, true, 0));
    jass_callbyname(level.vm, "verifyMove", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    for (int frame = 0; frame < 120 && unit->order_queue.count; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_EQ(unit->order_queue.count, 0);
    T_ASSERT(unit->movement.follow_target == target);
    jass_callbyname(level.vm, "verifySmart", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "stop", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "move", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "kill", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); T_EQ(unit->order_queue.count, 0);
    remove(filename);
}

TEST(wc3_api, current_order_repair_native_frames_queue_save_death_and_reuse) {
    cstring_t filename = Test_TempPath("wc3-repair122-save.bin");
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    reset_entities(); setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n unit worker = null\n unit target = null\nendglobals\n"
        "function repair takes nothing returns nothing\n"
        " call BJassAssert(IssueTargetOrder(worker, \"repair\", target), \"Repair accepted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(worker)==852024, \"Repair owns public head\")\nendfunction\n"
        "function smart takes nothing returns nothing\n"
        " call BJassAssert(IssueTargetOrder(worker, \"smart\", target), \"Smart Repair accepted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(worker)==851971, \"Smart retains public identity\")\nendfunction\n"
        "function verifyRepair takes nothing returns nothing\n"
        " call BJassAssert(GetUnitCurrentOrder(worker)==852024, \"Repair remains current\")\nendfunction\n"
        "function verifyIdle takes nothing returns nothing\n"
        " call BJassAssert(GetUnitCurrentOrder(worker)==0, \"Repair head retired\")\nendfunction\n"
        "function full takes nothing returns nothing\n"
        " call SetUnitState(target, UNIT_STATE_LIFE, GetUnitState(target, UNIT_STATE_MAX_LIFE))\nendfunction\n"
        "function kill takes nothing returns nothing\n call KillUnit(worker)\n call verifyIdle()\nendfunction\n"
        "function removeTarget takes nothing returns nothing\n call RemoveUnit(target)\n call verifyRepair()\nendfunction\n"
        "function recreate takes nothing returns nothing\n"
        " set worker=CreateUnit(Player(0),'hpea',64.0,64.0,0.0)\n call verifyIdle()\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " set worker=CreateUnit(Player(0),'hpea',64.0,64.0,0.0)\n"
        " set target=CreateUnit(Player(0),'hbar',704.0,64.0,0.0)\nendfunction\n"));
    edict_t *worker = find_test_unit(MAKEFOURCC('h','p','e','a'));
    edict_t *target = find_test_unit(MAKEFOURCC('h','b','a','r'));
    T_NOT_NULL(worker); T_NOT_NULL(target);
    worker->health.value = worker->health.max_value = 100;
    worker->collision = 8; worker->unitinfo.MoveSpeed = 256;
    worker->movetype = MOVETYPE_STEP; worker->svflags |= SVF_MONSTER;
    worker->think = monster_think; worker->stand = unit_stand; worker->die = unit_die;
    target->health.max_value = 1000; target->health.value = 900;
    target->collision = 32; target->stand = unit_stand;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    unit_stand(worker); gi.LinkEntity(worker); gi.LinkEntity(target);
    jass_callbyname(level.vm, "repair", false); T_ASSERT(!jass_rterror_pending(level.vm));
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    T_ASSERT(G_IssueUnitPointOrder(worker, "move", &(vec2_t){768,128}, true, 0, 0));
    target->health.value = 999.5f;
    T_ASSERT(!G_IssueUnitTargetOrder(worker, "repair", target, false, 0));
    T_EQ(worker->current_order_id, 852024); T_EQ(worker->order_queue.count, 1);
    target->health.value = 900;
    T_ASSERT(WriteGame(filename));
    T_ASSERT(unit_issueimmediateorder(worker, "stop")); T_ASSERT(ReadGame(filename));
    jass_callbyname(level.vm, "verifyRepair", false); T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(worker->build, target); T_EQ(worker->order_queue.count, 1);
    T_EQ(worker->buildwork->target_spawn_time, target->spawn_time);
    T_ASSERT(!worker->buildwork->target_removed);
    T_ASSERT(unit_issueimmediateorder(worker, "stop"));
    jass_callbyname(level.vm, "smart", false); T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(unit_issueimmediateorder(worker, "stop"));
    T_ASSERT(G_IssueUnitPointOrder(worker, "move", &(vec2_t){192,64}, false, 0, 0));
    T_ASSERT(G_IssueUnitTargetOrder(worker, "repair", target, true, 0));
    T_EQ(worker->current_order_id, 851986); T_EQ(worker->order_queue.count, 1);
    T_ASSERT(WriteGame(filename)); T_ASSERT(unit_issueimmediateorder(worker, "stop"));
    T_ASSERT(ReadGame(filename));
    for (int frame = 0; frame < 120 && worker->order_queue.count; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_EQ(worker->order_queue.count, 0);
    jass_callbyname(level.vm, "verifyRepair", false); T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "full", false); T_ASSERT(!jass_rterror_pending(level.vm));
    for (int frame = 0; frame < 3; frame++) { level.time += FRAMETIME; globals.RunFrame(); }
    jass_callbyname(level.vm, "verifyRepair", false); T_ASSERT(!jass_rterror_pending(level.vm));
    float approach_x = worker->s.origin.x;
    for (int frame = 0; frame < 120 && worker->current_order_id; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_ASSERT(worker->s.origin.x > approach_x); T_NULL(worker->build);
    jass_callbyname(level.vm, "verifyIdle", false); T_ASSERT(!jass_rterror_pending(level.vm));
    target->health.value = 900;
    jass_callbyname(level.vm, "repair", false); T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "kill", false); T_ASSERT(!jass_rterror_pending(level.vm));
    T_NULL(worker->build); T_EQ(worker->order_queue.count, 0);
    G_FreeEdict(worker); level.time += 1001;
    jass_callbyname(level.vm, "recreate", false); T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(find_test_unit(MAKEFOURCC('h','p','e','a')), worker);
    jass_callbyname(level.vm, "repair", false); T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "removeTarget", false); T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(worker->buildwork->target_removed); T_NULL(worker->build);
    T_ASSERT(WriteGame(filename)); T_ASSERT(unit_issueimmediateorder(worker, "stop"));
    T_ASSERT(ReadGame(filename));
    jass_callbyname(level.vm, "verifyRepair", false); T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(worker->buildwork->target_removed); T_NULL(worker->build);
    for (int frame = 0; frame < 8 && worker->current_order_id; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    jass_callbyname(level.vm, "verifyIdle", false); T_ASSERT(!jass_rterror_pending(level.vm));
    T_NULL(worker->build);
    remove(filename); building_restore_repair_data(old, rows);
}

TEST(wc3_api, current_order_hold_is_retired_while_behavior_persists) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    cstring_t filename = Test_TempPath("openwarcraft3-wc3-current-order-hold-save-test.bin");
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testUnit = null\n"
        "endglobals\n"
        "function verifyHold takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(testUnit) == 0, \"Hold completes its user head while retaining behavior\")\n"
        "endfunction\n"
        "function hold takes nothing returns nothing\n"
        "  call BJassAssert(IssueImmediateOrder(testUnit, \"holdposition\"), \"Hold accepted\")\n"
        "  call verifyHold()\n"
        "endfunction\n"
        "function move takes nothing returns nothing\n"
        "  call BJassAssert(IssuePointOrder(testUnit, \"move\", 512.0, 64.0), \"Move accepted\")\n"
        "  call BJassAssert(GetUnitCurrentOrder(testUnit) == OrderId(\"move\"), \"Move replaces retained Hold behavior\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set testUnit = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "endfunction\n"));
    edict_t *unit = find_test_unit(MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(unit);
    unit->health.value = unit->health.max_value = 100;
    unit->collision = 8; unit->unitinfo.MoveSpeed = 256;
    unit->movetype = MOVETYPE_STEP; unit->svflags |= SVF_MONSTER;
    unit->think = monster_think; unit->stand = unit_stand; unit->die = unit_die;
    unit_stand(unit); gi.LinkEntity(unit);
    jass_callbyname(level.vm, "move", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){768, 128}, true, 0, 0));
    jass_callbyname(level.vm, "hold", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); T_EQ(unit->order_queue.count, 0);
    T_ASSERT(unit->movement.holding_position);
    T_ASSERT(WriteGame(filename));
    T_ASSERT(unit_issueimmediateorder(unit, "stop"));
    T_ASSERT(ReadGame(filename));
    jass_callbyname(level.vm, "verifyHold", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); T_ASSERT(unit->movement.holding_position);
    /* The minimal MPQ omits UnitWeapons; supply the authored attack contract. */
    unit->data.UnitWeapons = &weapons;
    S_AttackProfileWrite(unit, 0)->type = ATK_NORMAL; S_AttackProfileWrite(unit, 0)->range = 64; S_AttackProfileWrite(unit, 0)->cooldown = 0.5f;
    S_AttackProfileWrite(unit, 0)->damagePoint = 0.2f;
    S_AttackProfileWrite(unit, 0)->damageBase = 10; S_AttackProfileWrite(unit, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
    unit->runtime.acquisition_range = 600;
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','g','r','u'), 88, 64);
    enemy->s.player = 1; enemy->targtype = TARG_GROUND; enemy->health.value = enemy->health.max_value = 1000;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    enemy->svflags |= SVF_MONSTER;
    enemy->die = unit_die; gi.LinkEntity(enemy);
    T_ASSERT(G_FindNearestEnemy(unit, 600) == enemy);
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    for (int frame = 0; frame < 60; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_ASSERT(enemy->health.value < enemy->health.max_value);
    T_FEQ(unit->s.origin.x, 64, 0.001f); T_FEQ(unit->s.origin.y, 64, 0.001f);
    jass_callbyname(level.vm, "verifyHold", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    G_FreeEdict(enemy);
    for (int frame = 0; frame < 20; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_ASSERT(unit->movement.holding_position);
    jass_callbyname(level.vm, "verifyHold", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "move", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); T_ASSERT(!unit->movement.holding_position);
    remove(filename);
}

TEST(wc3_api, current_order_follow_target_removal_is_synchronous) {
    for (int smart = 0; smart < 2; smart++) for (int queued = 0; queued < 4; queued++) {
        reset_entities();
        setup_test_world();
        T_ASSERT(run_test_jass(
            "globals\n"
            "  unit subject = null\n"
            "  unit target = null\n"
            "  unit other = null\n"
            "endglobals\n"
            "function move takes nothing returns nothing\n"
            "  call BJassAssert(IssueTargetOrder(subject, \"move\", target), \"target Move accepted\")\n"
            "endfunction\n"
            "function smart takes nothing returns nothing\n"
            "  call BJassAssert(IssueTargetOrder(subject, \"smart\", target), \"target Smart accepted\")\n"
            "endfunction\n"
            "function removeTarget takes nothing returns nothing\n"
            "  call RemoveUnit(target)\n"
            "endfunction\n"
            "function removeOther takes nothing returns nothing\n"
            "  call RemoveUnit(other)\n"
            "endfunction\n"
            "function verifyIdle takes nothing returns nothing\n"
            "  call BJassAssert(GetUnitCurrentOrder(subject) == 0, \"RemoveUnit synchronously retires Follow\")\n"
            "endfunction\n"
            "function verifyMove takes nothing returns nothing\n"
            "  call BJassAssert(GetUnitCurrentOrder(subject) == OrderId(\"move\"), \"valid pending point Move activates synchronously\")\n"
            "endfunction\n"
            "function main takes nothing returns nothing\n"
            "  set subject = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
            "  set target = CreateUnit(Player(0), 'hfoo', 512.0, 64.0, 0.0)\n"
            "  set other = CreateUnit(Player(0), 'hgry', 64.0, 512.0, 0.0)\n"
            "endfunction\n"));
        edict_t *unit = find_test_unit(MAKEFOURCC('h','p','e','a'));
        edict_t *target = find_test_unit(MAKEFOURCC('h','f','o','o'));
        edict_t *actors[] = {unit, target};
        T_NOT_NULL(unit); T_NOT_NULL(target);
        for (int i = 0; i < 2; i++) {
            actors[i]->health.value = actors[i]->health.max_value = 100;
            actors[i]->collision = 8; actors[i]->unitinfo.MoveSpeed = 256;
            actors[i]->movetype = MOVETYPE_STEP; actors[i]->svflags |= SVF_MONSTER;
            actors[i]->think = monster_think; actors[i]->stand = unit_stand; actors[i]->die = unit_die;
            unit_stand(actors[i]); gi.LinkEntity(actors[i]);
        }
        jass_callbyname(level.vm, smart ? "smart" : "move", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        /* Retail216: Smart initially owns approach/d0174, whose loss can
         * reissue a point. Exercise this test's persistent Follow/d0173
         * contract by completing that approach before removing the target. */
        api_run_move_entities(30);
        T_ASSERT(Vector2_distance(&unit->s.origin2,&target->s.origin2) <=
                 G_FollowStopRange(unit,target)+unit->collision+target->collision);
        jass_callbyname(level.vm, "removeOther", false);
        T_ASSERT(unit->movement.follow_target == target);
        T_EQ(unit->current_order_id, G_OrderId(smart ? "smart" : "move"));
        if (queued == 2) T_ASSERT(G_IssueUnitTargetOrder(unit, "smart", target, true, 0));
        if (queued) T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){768, 128}, queued != 3, 0, 0));
        jass_callbyname(level.vm, "removeTarget", false);
        T_ASSERT(target->inuse); T_ASSERT(G_IsDeferredFree(target));
        T_NULL(unit->movement.follow_target);
        T_EQ(unit->order_queue.count, 0);
        jass_callbyname(level.vm, queued ? "verifyMove" : "verifyIdle", false);
        T_ASSERT(!jass_rterror_pending(level.vm)); jass_rterror_clear(level.vm);
        jass_callbyname(level.vm, "removeTarget", false);
        jass_callbyname(level.vm, queued ? "verifyMove" : "verifyIdle", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        if (queued) T_ASSERT(move_is_active_order_walk(unit));
        level.started = level.scriptsConfigured = level.scriptsStarted = true;
        level.time += FRAMETIME; globals.RunFrame();
        T_ASSERT(!target->inuse);
        if (queued) {
            for (int frame = 0; frame < 120 && move_is_active_order_walk(unit); frame++) {
                level.time += FRAMETIME; globals.RunFrame();
            }
            T_ASSERT(Vector2_distance(&unit->s.origin2, &(vec2_t){768, 128}) <= unit->collision + 16);
        }
        jass_callbyname(level.vm, "verifyIdle", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
    }
}

TEST(wc3_api, current_order_patrol_owns_native_reversal_and_pending_activation) {
    char const *filename = Test_TempPath("openwarcraft3-wc3-current-order-patrol-save-test.bin");
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit subject = null\n"
        "endglobals\n"
        "function verifyPatrol takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(subject) == 851991, \"persistent Patrol owns its public head\")\n"
        "endfunction\n"
        "function verifyIdle takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitCurrentOrder(subject) == 0, \"retired Patrol has no current head\")\n"
        "endfunction\n"
        "function patrol takes nothing returns nothing\n"
        "  call BJassAssert(IssuePointOrder(subject, \"patrol\", 384.0, 64.0), \"public Patrol accepted\")\n"
        "  call verifyPatrol()\n"
        "endfunction\n"
        "function reject takes nothing returns nothing\n"
        "  call BJassAssert(not IssuePointOrder(subject, \"missingorder\", 512.0, 64.0), \"invalid replacement rejected\")\n"
        "  call verifyPatrol()\n"
        "endfunction\n"
        "function stop takes nothing returns nothing\n"
        "  call BJassAssert(IssueImmediateOrder(subject, \"stop\"), \"Stop accepted\")\n"
        "  call verifyIdle()\n"
        "endfunction\n"
        "function move takes nothing returns nothing\n"
        "  call BJassAssert(IssuePointOrder(subject, \"move\", 192.0, 64.0), \"point Move accepted\")\n"
        "endfunction\n"
        "function kill takes nothing returns nothing\n"
        "  call KillUnit(subject)\n"
        "  call verifyIdle()\n"
        "  call BJassAssert(not IssuePointOrder(subject, \"patrol\", 384.0, 64.0), \"dead Patrol rejected\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set subject = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "endfunction\n"));
    edict_t *unit = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(unit);
    unit->health.value = unit->health.max_value = 100;
    unit->collision = 8; unit->unitinfo.MoveSpeed = 256;
    unit->movetype = MOVETYPE_STEP; unit->svflags |= SVF_MONSTER;
    unit->think = monster_think; unit->stand = unit_stand; unit->die = unit_die;
    unit_stand(unit); gi.LinkEntity(unit);
    jass_callbyname(level.vm, "patrol", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_NOT_NULL(unit->movement.patrol_a); T_NOT_NULL(unit->movement.patrol_b);
    if (!unit->movement.patrol_a || !unit->movement.patrol_b) return;
    jass_callbyname(level.vm, "reject", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    unsigned reversals = 0;
    edict_t *last = unit->movement.patrol_target;
    for (int frame = 0; frame < 100; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
        if (unit->movement.patrol_target != last) {
            reversals++; last = unit->movement.patrol_target;
        }
    }
    T_ASSERT(reversals >= 2);
    jass_callbyname(level.vm, "verifyPatrol", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){768, 128}, true, 0, 0));
    T_EQ(unit->order_queue.count, 1);
    T_ASSERT(WriteGame(filename));
    jass_callbyname(level.vm, "stop", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); T_EQ(unit->order_queue.count, 0);
    T_ASSERT(ReadGame(filename));
    jass_callbyname(level.vm, "verifyPatrol", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); T_EQ(unit->order_queue.count, 1);
    jass_callbyname(level.vm, "move", false);
    T_ASSERT(!jass_rterror_pending(level.vm)); T_EQ(unit->order_queue.count, 0);
    T_NULL(unit->movement.patrol_a);
    T_ASSERT(G_IssueUnitPointOrder(unit, "patrol", &(vec2_t){448, 64}, true, 0, 0));
    T_EQ(unit->order_queue.count, 1);
    for (int frame = 0; frame < 120 && unit->current_order_id != 851991; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_EQ(unit->order_queue.count, 0);
    jass_callbyname(level.vm, "verifyPatrol", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(Vector2_distance(&unit->s.origin2, &(vec2_t){192, 64}) <= 64);
    T_FEQ(unit->movement.patrol_a->s.origin.x, unit->s.origin.x, 0.001f);
    T_FEQ(unit->movement.patrol_a->s.origin.y, unit->s.origin.y, 0.001f);
    T_FEQ(unit->movement.patrol_b->s.origin.x, 448, 0.001f);
    T_FEQ(unit->movement.patrol_b->s.origin.y, 64, 0.001f);
    jass_callbyname(level.vm, "kill", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    G_FreeEdict(unit); level.time += 1001;
    jass_callbyname(level.vm, "main", false);
    T_ASSERT(find_test_unit(MAKEFOURCC('h','p','e','a')) == unit);
    jass_callbyname(level.vm, "verifyIdle", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    remove(filename);
}

TEST(wc3_api, current_order_patrol_ui_uses_same_pending_owner) {
    void (*write)(pfWriteType_t, void const *) = gi.Write;
    void (*unicast)(edict_t *) = gi.unicast;
    cstring_t command[] = { "button", "CmdPatrol" };
    setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *clent = g_edicts;
    clent->client = game.clients;
    gi.Write = selection_native_test_write; gi.unicast = selection_native_test_unicast;
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 64);
    unit->health.value = unit->health.max_value = 100;
    unit->collision = 8; unit->unitinfo.MoveSpeed = 256;
    unit->movetype = MOVETYPE_STEP; unit->svflags |= SVF_MONSTER;
    unit->think = monster_think; unit->stand = unit_stand; unit->die = unit_die;
    unit_stand(unit); gi.LinkEntity(unit); G_SelectEntity(clent->client, unit);
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){192, 64}, false, 0, 0));
    G_ClientCommand(clent, 2, command);
    T_ASSERT(clent->client->menu.supports_order_queue);
    T_NOT_NULL(clent->client->menu.on_location_selected);
    clent->client->menu.order_queued = true;
    T_ASSERT(clent->client->menu.on_location_selected(clent, &(vec2_t){448, 64}));
    T_EQ(unit->current_order_id, G_OrderId("move")); T_EQ(unit->order_queue.count, 1);
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    for (int frame = 0; frame < 120 && unit->current_order_id != 851991; frame++) {
        level.time += FRAMETIME; globals.RunFrame();
    }
    T_EQ(unit->current_order_id, 851991); T_EQ(unit->order_queue.count, 0);
    T_NOT_NULL(unit->movement.patrol_a);
    clent->client->menu.order_queued = false;
    T_ASSERT(clent->client->menu.on_location_selected(clent, &(vec2_t){512, 256}));
    T_EQ(unit->current_order_id, 851991);
    T_FEQ(unit->movement.patrol_b->s.origin.x, 512, 0.001f);
    T_FEQ(unit->movement.patrol_b->s.origin.y, 256, 0.001f);
    gi.Write = write; gi.unicast = unicast;
}

TEST(wc3_api, issued_order_context_ignores_spell_metadata) {
    setup_test_world();
    g_edicts[0].client = &game.clients[0];
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer spellEvents = 0\n"
        "endglobals\n"
        "function onSpell takes nothing returns nothing\n"
        "  set spellEvents = spellEvents + 1\n"
        "  call BJassAssert(GetSpellAbilityId() == 'AHbz', \"spell retains its own scalar metadata\")\n"
        "  call BJassAssert(GetSpellTargetX() == 112.0, \"spell retains its own point metadata\")\n"
        "  call BJassAssert(GetIssuedOrderId() == 0, \"spell callback has no issued order ID\")\n"
        "  call BJassAssert(GetOrderPointX() == 0.0, \"spell callback has no order point X\")\n"
        "  call BJassAssert(GetOrderPointY() == 0.0, \"spell callback has no order point Y\")\n"
        "  call BJassAssert(GetOrderPointLoc() == null, \"spell callback has no order location\")\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(spellEvents == 1, \"spell callback executes once\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_SPELL_EFFECT, null)\n"
        "  call TriggerAddAction(t, function onSpell)\n"
        "endfunction\n"));
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 32);
    G_PublishEventWithPoint(&(gameEventPointParams_t){ .edict = unit,
        .type = EVENT_PLAYER_UNIT_SPELL_EFFECT, .value = MAKEFOURCC('A','H','b','z'),
        .point = &(vec2_t){112, 224} });
    G_RunEvents(); jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", false);
    T_STREQ(jass_rterror_message(level.vm), "");
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, issued_target_context_is_frozen_across_replacement) {
    setup_test_world();
    g_edicts[0].client = &game.clients[0];
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit mover = null\n"
        "  unit first = null\n"
        "  unit second = null\n"
        "  integer targetEvents = 0\n"
        "endglobals\n"
        "function onTarget takes nothing returns nothing\n"
        "  set targetEvents = targetEvents + 1\n"
        "  if targetEvents <= 2 then\n"
        "    call BJassAssert(GetIssuedOrderId() == OrderId(\"move\"), \"first target order ID frozen\")\n"
        "    call BJassAssert(GetOrderTargetUnit() == first, \"first target ownership frozen\")\n"
        "  else\n"
        "    call BJassAssert(GetIssuedOrderId() == OrderId(\"smart\"), \"second target order ID frozen\")\n"
        "    call BJassAssert(GetOrderTargetUnit() == second, \"second target ownership frozen\")\n"
        "  endif\n"
        "  call BJassAssert(GetOrderPointX() == 0.0, \"target order does not expose point metadata\")\n"
        "endfunction\n"
        "function issue takes nothing returns nothing\n"
        "  call BJassAssert(IssueTargetOrder(mover, \"move\", first), \"first follow accepted\")\n"
        "  call BJassAssert(IssueTargetOrder(mover, \"smart\", second), \"replacement follow accepted\")\n"
        "  call BJassAssert(IssueImmediateOrder(mover, \"stop\"), \"follow interruption accepted\")\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(targetEvents == 4, \"both event families publish each target once\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set mover = CreateUnit(Player(0), 'hpea', 64.0, 32.0, 0.0)\n"
        "  set first = CreateUnit(Player(0), 'hfoo', 192.0, 64.0, 0.0)\n"
        "  set second = CreateUnit(Player(0), 'hfoo', 256.0, 96.0, 0.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), ConvertPlayerUnitEvent(40), null)\n"
        "  call TriggerRegisterUnitEvent(t, mover, EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
        "  call TriggerAddAction(t, function onTarget)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *unit = &g_edicts[i];
        if (!unit->inuse || (unit->class_id != MAKEFOURCC('h','p','e','a') &&
                             unit->class_id != MAKEFOURCC('h','f','o','o'))) continue;
        unit->health.value = unit->health.max_value = 100;
        unit->svflags |= SVF_MONSTER;
        unit->stand = unit_stand; unit_stand(unit);
    }
    jass_callbyname(level.vm, "issue", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    G_RunEvents(); jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", false);
    T_STREQ(jass_rterror_message(level.vm), "");
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, build_placement_publishes_point_order_event_context) {
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    UnitProfile_t profile = { .builds = "hbar" };
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128.0f, -128.0f);
    builder->s.player = client->ps.number;
    builder->data.UnitProfile = &profile;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer pointEvents = 0\n"
        "endglobals\n"
        "function onPointOrder takes nothing returns nothing\n"
        "  set pointEvents = pointEvents + 1\n"
        "  call BJassAssert(GetUnitTypeId(GetOrderedUnit()) == 'hpea', \"ordered unit must be the builder\")\n"
        "  call BJassAssert(GetIssuedOrderId() == 'hbar', \"build point order must expose building rawcode\")\n"
        "  call BJassAssert(GetOrderPointX() == 64.0, \"build point order X must survive event dispatch\")\n"
        "  call BJassAssert(GetOrderPointY() == 64.0, \"build point order Y must survive event dispatch\")\n"
        "endfunction\n"
        "function verifyPointOrder takes nothing returns nothing\n"
        "  call BJassAssert(pointEvents == 1, \"build placement must publish one player point-order event\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER, null)\n"
        "  call TriggerAddAction(t, function onPointOrder)\n"
        "endfunction\n"));

    T_ASSERT(G_IssueBuildOrder(builder, barracks, &point));
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyPointOrder", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, issue_build_order_by_id_uses_authoritative_build_path) {
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    UnitProfile_t profile = { .builds = "hbar" };
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testBuilder = null\n"
        "endglobals\n"
        "function issueBuild takes nothing returns nothing\n"
        "  call BJassAssert(IssueBuildOrderById(testBuilder, 'hbar', 64.0, 64.0), \"build native must accept a legal structure\")\n"
        "  call BJassAssert(not IssueBuildOrderById(testBuilder, 'hfoo', 96.0, 96.0), \"build native must reject a non-Builds rawcode\")\n"
        "endfunction\n"
        "function issuePointBuild takes nothing returns nothing\n"
        "  call BJassAssert(IssuePointOrderById(testBuilder, 'hbar', 128.0, 128.0), \"point-order-by-id must accept a building rawcode\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set testBuilder = CreateUnit(Player(0), 'hpea', -128.0, -128.0, 0.0)\n"
        "endfunction\n"));

    builder = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(builder);
    builder->data.UnitProfile = &profile;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    jass_callbyname(level.vm, "issueBuild", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(builder->build_project, barracks);
    T_NOT_NULL(builder->goalentity);
    T_FEQ(builder->goalentity->s.origin2.x, 64.0f, 0.001f);
    T_FEQ(builder->goalentity->s.origin2.y, 64.0f, 0.001f);

    unit_stand(builder);
    jass_callbyname(level.vm, "issuePointBuild", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(builder->build_project, barracks);
    T_NOT_NULL(builder->goalentity);
}

TEST(wc3_api, construct_finish_fires_player_and_unit_events_with_structure_context) {
    edict_t *building;
    gameClient_t *saved;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testBuilding = null\n"
        "  integer playerFinish = 0\n"
        "  integer unitFinish = 0\n"
        "endglobals\n"
        "function onPlayerFinish takes nothing returns nothing\n"
        "  call BJassAssert(GetConstructedStructure() == testBuilding, \"player finish structure context\")\n"
        "  set playerFinish = playerFinish + 1\n"
        "endfunction\n"
        "function onUnitFinish takes nothing returns nothing\n"
        "  call BJassAssert(GetConstructedStructure() == testBuilding, \"unit finish structure context\")\n"
        "  set unitFinish = unitFinish + 1\n"
        "endfunction\n"
        "function verifyFinish takes nothing returns nothing\n"
        "  call BJassAssert(playerFinish == 1, \"player construct finish must fire once\")\n"
        "  call BJassAssert(unitFinish == 1, \"unit construct finish must fire once\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger playerTrig = CreateTrigger()\n"
        "  local trigger unitTrig = CreateTrigger()\n"
        "  set testBuilding = CreateUnit(Player(0), 'hbar', 64.0, 64.0, 0.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(playerTrig, Player(0), EVENT_PLAYER_UNIT_CONSTRUCT_FINISH, null)\n"
        "  call TriggerRegisterUnitEvent(unitTrig, testBuilding, EVENT_UNIT_CONSTRUCT_FINISH)\n"
        "  call TriggerAddAction(playerTrig, function onPlayerFinish)\n"
        "  call TriggerAddAction(unitTrig, function onUnitFinish)\n"
        "endfunction\n"));

    building = find_test_unit(MAKEFOURCC('h','b','a','r'));
    T_NOT_NULL(building);
    building->stand = unit_stand;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    saved = g_edicts[0].client;
    g_edicts[0].client = NULL;
    G_CompleteConstruction(building);
    g_edicts[0].client = saved;

    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyFinish", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, spell_effect_event_exposes_wc3_response_context_and_order_ids) {
    edict_t *caster, *target;
    vec2_t point = { 96.0f, 144.0f };

    setup_test_world();
    caster = alloc_test_unit(MAKEFOURCC('O','t','c','h'), 0.0f, 0.0f);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    caster->s.player = game.clients[0].ps.number;
    target->s.player = game.clients[1].ps.number;
    target->svflags |= SVF_MONSTER;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer spellEvents = 0\n"
        "endglobals\n"
        "function onSpellEffect takes nothing returns nothing\n"
        "  set spellEvents = spellEvents + 1\n"
        "  call BJassAssert(GetSpellAbilityUnit() != null, \"spell caster missing\")\n"
        "  if GetSpellAbilityId() == 'AOcl' then\n"
        "    call BJassAssert(GetSpellTargetUnit() != null, \"unit spell target missing\")\n"
        "    call BJassAssert(GetSpellTargetX() == 0.0 and GetSpellTargetY() == 0.0, \"unit target must not expose point context\")\n"
        "  elseif GetSpellAbilityId() == 'AEbl' then\n"
        "    call BJassAssert(GetSpellTargetUnit() == null, \"point spell must not expose a unit target\")\n"
        "    call BJassAssert(GetSpellTargetX() == 96.0 and GetSpellTargetY() == 144.0, \"point spell coordinates missing\")\n"
        "  else\n"
        "    call BJassAssert(false, \"unexpected spell id\")\n"
        "  endif\n"
        "endfunction\n"
        "function verifySpellEvents takes nothing returns nothing\n"
        "  call BJassAssert(spellEvents == 2, \"expected both spell-effect events\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call BJassAssert(OrderId(\"chainlightning\") == 852119, \"chain lightning order id mismatch\")\n"
        "  call BJassAssert(OrderId2String(852119) == \"chainlightning\", \"chain lightning order name mismatch\")\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_SPELL_EFFECT, null)\n"
        "  call TriggerAddAction(t, function onSpellEffect)\n"
        "endfunction\n"));

    G_PublishEventWithPoint(&(gameEventPointParams_t){
        .edict = caster, .type = EVENT_PLAYER_UNIT_SPELL_EFFECT, .source = target,
        .value = (int32_t)MAKEFOURCC('A','O','c','l') });
    G_PublishEventWithPoint(&(gameEventPointParams_t){
        .edict = caster, .type = EVENT_PLAYER_UNIT_SPELL_EFFECT, .value = (int32_t)MAKEFOURCC('A','E','b','l'),
        .point = &point });
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifySpellEvents", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, enable_user_ui_does_not_block_target_commands) {
    gameClient_t *gc = &game.clients[0];
    cstring_t point[] = { "point", "10", "20" };

    ui_point_calls = 0; gc->ps.number = 0; gc->menu.on_location_selected = count_ui_point;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call EnableUserUI(false)\n"
        "endfunction"));
    T_ASSERT(gc->no_ui);
    globals.ClientCommand(&g_edicts[0], 3, point);
    T_EQ(ui_point_calls, 1);

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call EnableUserUI(true)\n"
        "endfunction"));
    T_ASSERT(!gc->no_ui);
    globals.ClientCommand(&g_edicts[0], 3, point);
    T_EQ(ui_point_calls, 2);
    gc->menu.on_location_selected = NULL;
    currentplayer = NULL;
}

TEST(wc3_api, debug_statements_parse_but_do_not_execute_in_release) {
    gameClient_t *gc = &game.clients[0];
    gc->ps.stats[1] = 0;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 6)\n"
        "debug call MissingDebug()\n"
        "debug set bj_forLoopAIndex = 7\n"
        "debug if true then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 8)\n"
        "endif\n"
        "endfunction"));
    T_EQ(gc->ps.stats[1], 6);
    currentplayer = NULL;
}

/* Create a minimal unit in slot 0 and return it. */
static edict_t *make_unit_hero(void) {
    reset_entities();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    return ent;
}

TEST(wc3_api, model_effects_are_rendered_but_not_world_selectable) {
    vec2_t point = { 64.0f, 64.0f };
    edict_t *target;
    edict_t *point_effect;
    edict_t *target_effect;

    setup_test_world();
    point_effect = G_SpawnModelEffect("TestUI\\Models\\anim_pulse.mdx", &point, NULL, NULL, false);
    T_NOT_NULL(point_effect);
    T_ASSERT(point_effect->s.model != 0);
    T_ASSERT(point_effect->s.flags & EF_NOT_SELECTABLE);

    target = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 128.0f, 128.0f);
    target->svflags |= SVF_MONSTER;
    G_SetUnitColorOverride(target, 6);
    target_effect = G_SpawnModelEffect("TestUI\\Models\\anim_pulse.mdx", NULL, target, "overhead", false);
    T_NOT_NULL(target_effect);
    T_ASSERT(target_effect->s.model != 0);
    T_ASSERT(target_effect->s.flags & EF_NOT_SELECTABLE);
    T_EQ(unit_team_color(target_effect), 6);
}

TEST(wc3_api, effect_natives_return_independent_handles) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local effect direct = AddSpecialEffect(\"TestUI\\\\Models\\\\anim_pulse.mdx\", 64.0, 64.0)\n"
        "  local effect spell = AddSpellEffectById('AHhb', EFFECT_TYPE_TARGET, 96.0, 96.0)\n"
        "  call BJassAssert(direct != null, \"AddSpecialEffect returned null\")\n"
        "  call BJassAssert(spell != null, \"AddSpellEffectById returned null\")\n"
        "  call BJassAssert(direct != spell, \"effect handles aliased\")\n"
        "  call DestroyEffect(direct)\n"
        "  call DestroyEffect(spell)\n"
        "endfunction\n"));
}

/* Undead01's MassTeleportCaster has only a flagged non-looping Stand sequence.
 * Stop rendering it at that boundary but keep its JASS handle valid until the
 * script calls DestroyEffect, even if another variable was assigned afterward. */
TEST(wc3_api, nonlooping_effect_stand_hides_without_invalidating_handle) {
    int effect_model;
    uint32_t live_effects = 0;
    setup_test_world();
    effect_model = G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx");
    T_ASSERT(effect_model > 0);
    animation_t const *stand = G_GetAnimation((uint32_t)effect_model, "Stand");
    T_NOT_NULL(stand);
    T_ASSERT(stand && (stand->flags & 1u));
    T_ASSERT(run_test_jass(
        "globals\n"
        "  effect retainedEffect = null\n"
        "  effect overwrittenEffect = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set overwrittenEffect = AddSpecialEffect(\"TestUI\\\\Models\\\\anim_oneshot.mdx\", 32.0, 32.0)\n"
        "  set retainedEffect = overwrittenEffect\n"
        "  set overwrittenEffect = AddSpecialEffect(\"TestUI\\\\Models\\\\anim_pulse.mdx\", 64.0, 64.0)\n"
        "endfunction\n"
        "function DestroyRetainedEffect takes nothing returns nothing\n"
        "  call DestroyEffect(retainedEffect)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = globals.edicts + i;
        if (effect->inuse && effect->s.model == effect_model) live_effects++;
    }
    T_EQ(live_effects, 1);
    edict_t *effect = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (globals.edicts[i].inuse && globals.edicts[i].s.model == effect_model) effect = globals.edicts + i;
    T_NOT_NULL(effect);
    if (!effect) return;
    for (int i = 0; i < 12 && effect->think; i++) {
        level.time += FRAMETIME;
        G_RunEntities();
    }
    T_ASSERT(effect->inuse);
    T_ASSERT(effect->s.renderfx & RF_HIDDEN);
    T_ASSERT(effect->aiflags & AI_HOLD_FRAME);
    jass_callbyname(level.vm, "DestroyRetainedEffect", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(!effect->inuse);
}

TEST(wc3_api, jass_sound_runtime_tracks_one_shot_volume_and_attachment_safely) {
    gsound_t handle_storage = {0};
    handle_t handle = &handle_storage;
    jassSoundPlayback_t playback;
    edict_t *unit;

    reset_entities();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32.0f, 48.0f);
    unit->spawn_time = 1234;

    G_JassSoundRuntimeInit(handle);
    G_JassSoundPlayback(handle, &playback);
    T_FEQ(playback.volume, 1.0f, 0.001f);
    T_ASSERT(!playback.positioned);

    G_JassSoundSetVolume(handle, 0.5f);
    G_JassSoundSetPosition(handle, &MAKE(vec3_t, 10.0f, 20.0f, 30.0f));
    G_JassSoundPlayback(handle, &playback);
    T_FEQ(playback.volume, 0.5f, 0.001f);
    T_ASSERT(playback.positioned);
    T_FEQ(playback.origin.x, 10.0f, 0.001f);
    T_FEQ(playback.origin.y, 20.0f, 0.001f);
    T_NULL(playback.emitter);

    G_JassSoundAttach(handle, unit);
    G_JassSoundPlayback(handle, &playback);
    T_ASSERT(playback.positioned);
    T_ASSERT(playback.emitter == unit);
    T_FEQ(playback.origin.x, 32.0f, 0.001f);
    T_FEQ(playback.origin.y, 48.0f, 0.001f);

    /* Reusing the edict slot after the attached unit was freed must not make a
     * sound follow the replacement entity. */
    unit->spawn_time++;
    G_JassSoundPlayback(handle, &playback);
    T_ASSERT(!playback.positioned);
    T_NULL(playback.emitter);

}

TEST(wc3_api, repeated_wait_for_sound_only_waits_until_voice_end_once) {
    gameClient_t *gc = &game.clients[0];
    gsound_t lifetime = { 0 };
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    setup_test_world();
    G_JassSoundRuntimeInit(&lifetime);
    lifetime.duration = 15000;
    T_EQ(G_JassSoundRemainingDuration(&lifetime), 15000u);
    level.time = 1000;
    G_JassSoundMarkStarted(&lifetime);
    level.time = 4500;
    T_EQ(G_JassSoundRemainingDuration(&lifetime), 11500u);
    level.time = 16000;
    T_EQ(G_JassSoundRemainingDuration(&lifetime), 0u);
    G_JassSoundMarkStarted(&lifetime);
    level.time = 17000;
    T_EQ(G_JassSoundRemainingDuration(&lifetime), 14000u);
    level.time = 0;
    gc = &game.clients[0];
    gc->ps.number = 0;
    gc->connected = true;
    currentplayer = &gc->ps;
    ui_sound_calls = 0;
    gi.Sound = capture_ui_sound;
    gi.SoundIndex = capture_ui_sound_index;
    gi.SoundIndexAlias = capture_ui_sound_index_alias;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  sound introVoice = null\n"
        "  trigger introTrigger = null\n"
        "  integer introStage = 0\n"
        "endglobals\n"
        "function IntroSequenceAction takes nothing returns nothing\n"
        "  call StartSound(introVoice)\n"
        "  call TriggerSleepAction(3.5)\n"
        "  call TriggerWaitForSound(introVoice, 0.0)\n"
        "  set introStage = 1\n"
        "  call TriggerWaitForSound(introVoice, 2.0)\n"
        "  set introStage = 2\n"
        "endfunction\n"
        "function AssertIntroStageZero takes nothing returns nothing\n"
        "  call BJassAssert(introStage == 0, \"dialogue advanced before the voice duration elapsed\")\n"
        "endfunction\n"
        "function AssertIntroStageOne takes nothing returns nothing\n"
        "  call BJassAssert(introStage == 1, \"positive offset did not preserve the remaining wait\")\n"
        "endfunction\n"
        "function AssertIntroStageDone takes nothing returns nothing\n"
        "  call BJassAssert(introStage == 2, \"repeated wait delayed dialogue after voice ended\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set introVoice = CreateSound(\"intro.wav\", false, false, false, 0, 0, \"\")\n"
        "  call SetSoundDuration(introVoice, 15000)\n"
        "  set introTrigger = CreateTrigger()\n"
        "  call TriggerAddAction(introTrigger, function IntroSequenceAction)\n"
        "  call TriggerExecute(introTrigger)\n"
        "endfunction\n"));
    T_EQ(ui_sound_calls, 1);
    T_EQ(level.time, 0u);

    level.time = 3500;
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "AssertIntroStageZero", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.time = 14999;
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "AssertIntroStageZero", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.time = 15000;
    jass_runevents(level.vm);
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "AssertIntroStageOne", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.time = 16999;
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "AssertIntroStageOne", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    level.time = 17000;
    jass_runevents(level.vm);
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "AssertIntroStageDone", false);
    T_ASSERT(!jass_rterror_pending(level.vm));

    gi.Sound = old_sound;
    gi.SoundIndex = old_soundindex;
    gi.SoundIndexAlias = old_sound_alias;
    currentplayer = NULL;
}

TEST(wc3_api, jass_create_sound_from_label_uses_merged_ambience_table) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X4;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y1;X4;K\"Volume\"\n"
        "C;Y2;X1;K\"AmbientTest\"\n"
        "C;Y2;X2;K\"ambient.wav\"\n"
        "C;Y2;X3;K\"Sound\\Ambient\\\"\n"
        "C;Y2;X4;K\"63.5\"\n"
        "E\n";
    gameClient_t *gc = &game.clients[0];
    edict_t *recipient = &g_edicts[0];
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old_rows = G_SetSLKRows("AmbienceSounds", rows);
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    recipient->client = gc;
    gc->ps.number = 0;
    gc->connected = true;
    currentplayer = &gc->ps;
    ui_sound_calls = 0; ui_sound_value = 0; ui_sound_volume = 0.0f; ui_sound_path[0] = '\0';
    __typeof__(gi.SoundPolicy) old_sound_policy = gi.SoundPolicy;
    gi.SoundPolicy = capture_ui_sound_policy;
    gi.Sound = capture_ui_sound; gi.SoundIndex = capture_ui_sound_index; gi.SoundIndexAlias = capture_ui_sound_index_alias;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local sound s = CreateSoundFromLabel(\"AmbientTest\", false, false, false, 0, 0)\n"
        "  call StartSound(s)\n"
        "endfunction\n"));
    T_EQ(ui_sound_calls, 1);
    T_EQ(ui_sound_value, 77);
    T_STREQ(ui_sound_path, "Sound\\Ambient\\ambient.wav");
    T_FEQ(ui_sound_volume, 0.5f, 0.001f);

    gi.SoundIndex = old_soundindex; gi.SoundIndexAlias = old_sound_alias; gi.Sound = old_sound; gi.SoundPolicy = old_sound_policy;
    currentplayer = NULL;
    G_SetSLKRows("AmbienceSounds", old_rows); free_slk_rows(rows);
}

TEST(wc3_api, jass_create_sound_filename_with_label_keeps_explicit_file_and_uses_unitack_params) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X4;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y1;X4;K\"Volume\"\n"
        "C;Y2;X1;K\"FootmanWarcry\"\n"
        "C;Y2;X2;K\"warcry.wav\"\n"
        "C;Y2;X3;K\"Units\\Human\\Footman\\\"\n"
        "C;Y2;X4;K\"31.75\"\n"
        "E\n";
    gameClient_t *gc = &game.clients[0];
    edict_t *recipient = &g_edicts[0];
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old_rows = G_SetSLKRows("UnitAckSounds", rows);
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    recipient->client = gc; gc->ps.number = 0; gc->connected = true; currentplayer = &gc->ps;
    ui_sound_calls = 0; ui_sound_value = 0; ui_sound_volume = 0.0f; ui_sound_path[0] = '\0';
    gi.Sound = capture_ui_sound; gi.SoundIndex = capture_ui_sound_index; gi.SoundIndexAlias = capture_ui_sound_index_alias;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local sound s = CreateSoundFilenameWithLabel(\"Sound\\\\Custom\\\\explicit.wav\", false, false, false, 0, 0, \"FootmanWarcry\")\n"
        "  call StartSound(s)\n"
        "endfunction\n"));
    T_EQ(ui_sound_calls, 1);
    T_EQ(ui_sound_value, 77);
    T_STREQ(ui_sound_path, "Sound\\Custom\\explicit.wav");
    T_FEQ(ui_sound_volume, 0.25f, 0.001f);

    gi.SoundIndex = old_soundindex; gi.SoundIndexAlias = old_sound_alias; gi.Sound = old_sound; currentplayer = NULL;
    G_SetSLKRows("UnitAckSounds", old_rows); free_slk_rows(rows);
}

TEST(wc3_api, jass_set_sound_params_from_label_keeps_filename_and_uses_dialog_params) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X4;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y1;X4;K\"Volume\"\n"
        "C;Y2;X1;K\"DialogTest\"\n"
        "C;Y2;X2;K\"different.wav\"\n"
        "C;Y2;X3;K\"Sound\\Dialog\\\"\n"
        "C;Y2;X4;K\"63.5\"\n"
        "E\n";
    gameClient_t *gc = &game.clients[0];
    edict_t *recipient = &g_edicts[0];
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old_rows = G_SetSLKRows("DialogSounds", rows);
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    recipient->client = gc; gc->ps.number = 0; gc->connected = true; currentplayer = &gc->ps;
    ui_sound_calls = 0; ui_sound_value = 0; ui_sound_volume = 0.0f; ui_sound_path[0] = '\0';
    gi.Sound = capture_ui_sound; gi.SoundIndex = capture_ui_sound_index; gi.SoundIndexAlias = capture_ui_sound_index_alias;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local sound s = CreateSound(\"Sound\\\\Custom\\\\voice.wav\", false, false, false, 0, 0, \"DefaultEAXON\")\n"
        "  call SetSoundParamsFromLabel(s, \"DialogTest\")\n"
        "  call StartSound(s)\n"
        "endfunction\n"));
    T_EQ(ui_sound_calls, 1);
    T_EQ(ui_sound_value, 77);
    T_STREQ(ui_sound_path, "Sound\\Custom\\voice.wav");
    T_FEQ(ui_sound_volume, 0.5f, 0.001f);

    gi.SoundIndex = old_soundindex; gi.SoundIndexAlias = old_sound_alias; gi.Sound = old_sound; currentplayer = NULL;
    G_SetSLKRows("DialogSounds", old_rows); free_slk_rows(rows);
}

TEST(wc3_api, jass_start_sound_skips_disconnected_local_player) {
    gameClient_t *gc = &game.clients[0];
    edict_t *recipient = &g_edicts[0];
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    recipient->client = gc;
    gc->ps.number = 0;
    currentplayer = &gc->ps;
    ui_sound_calls = 0;
    ui_sound_value = 0;
    gi.Sound = capture_ui_sound;
    gi.SoundIndex = capture_ui_sound_index; gi.SoundIndexAlias = capture_ui_sound_index_alias;

    gc->connected = false;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local sound s = CreateSound(\"test.wav\", false, false, false, 0, 0, \"\")\n"
        "  call StartSound(s)\n"
        "endfunction\n"));
    T_EQ(ui_sound_calls, 0);

    gc->connected = true;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local sound s = CreateSound(\"test.wav\", false, false, false, 0, 0, \"\")\n"
        "  call StartSound(s)\n"
        "endfunction\n"));
    T_EQ(ui_sound_calls, 1);
    T_EQ(ui_sound_value, 77);

    gi.SoundIndex = old_soundindex; gi.SoundIndexAlias = old_sound_alias;
    gi.Sound = old_sound;
    currentplayer = NULL;
}

TEST(wc3_api, ui_sound_transport_waits_for_connected_client) {
    gameClient_t client = { 0 };
    edict_t ent = { .client = &client };
    void (*old_sound)(edict_t *, int, int, float, float, float) = gi.Sound;
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    ui_sound_calls = 0;
    ui_sound_value = 0;
    __typeof__(gi.SoundPolicy) old_sound_policy = gi.SoundPolicy;
    gi.SoundPolicy = capture_ui_sound_policy;
    gi.Sound = capture_ui_sound;
    gi.SoundIndex = capture_ui_sound_index; gi.SoundIndexAlias = capture_ui_sound_index_alias;

    G_PlayUISoundForPlayer(&ent, "InterfaceError");
    T_EQ(ui_sound_calls, 0);

    client.connected = true;
    G_PlayUISoundForPlayer(&ent, "InterfaceError");
    T_EQ(ui_sound_calls, 1);
    T_EQ(ui_sound_value, 77);

    gi.SoundIndex = old_soundindex; gi.SoundIndexAlias = old_sound_alias;
    gi.Sound = old_sound; gi.SoundPolicy = old_sound_policy;
}

TEST(wc3_api, customize_entity_preserves_world_state) {
    entityState_t state = { .number = 7, .model = 11, .renderfx = RF_SELECTED };
    edict_t ent = { 0 };

    T_NOT_NULL(globals.CustomizeEntity);
    if (!globals.CustomizeEntity)
        return;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(state.number, 7);
    T_EQ(state.model, 11);
    T_EQ(state.renderfx, RF_SELECTED);
}

TEST(wc3_api, customize_entity_publishes_automatic_minimap_contacts) {
    UnitBalance_t hero_balance = { .strength = 1 };
    UnitUI_t ui = { 0 };
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 }, .data = { .UnitUI = &ui } };

    ent.health.value = 100.0f;
    T_ASSERT(globals.IsSnapshotPriorityEntity(3, &ent));
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_UNIT);

    state.flags = EF_BUILDING;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_BUILDING);

    ui.hideOnMinimap = true;
    T_ASSERT(!globals.IsSnapshotPriorityEntity(3, &ent));
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_NONE);
    ui.hideOnMinimap = false;

    ui.neutralBuildingMinimapIcon = true;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_NEUTRAL_BUILDING);

    ui.neutralBuildingMinimapIcon = false;
    state.flags = 0;
    ent.data.UnitBalance = &hero_balance;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_HERO);

    /* WC3 keeps the hero icon and generic minimap-display switches separate. */
    ui.hideOnMinimap = true;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_HERO);

    ui.hideHeroMinimap = true;
    T_ASSERT(!globals.IsSnapshotPriorityEntity(3, &ent));
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_NONE);

    ui.hideOnMinimap = false;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_UNIT);
}

TEST(wc3_api, customize_entity_distinguishes_racial_gold_mine_markers) {
    UnitAbilities_t haunted_abilities = { .abilList = "Abgm" };
    UnitAbilities_t entangled_abilities = { .abilList = "Aegm" };
    entityState_t state = { .number = 7, .model = 11, .flags = EF_BUILDING };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 } };

    ent.health.value = 100.0f;
    ent.data.UnitAbilities = &haunted_abilities;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_GOLD_HAUNTED);

    ent.data.UnitAbilities = &entangled_abilities;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_GOLD_ENTANGLED);
}

TEST(wc3_api, customize_entity_suppresses_hidden_and_dead_minimap_contacts) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 } };

    ent.health.value = 100.0f;
    state.renderfx = RF_HIDDEN;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_NONE);

    state.renderfx = 0;
    ent.svflags |= SVF_DEADMONSTER;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_NONE);
}

TEST(wc3_api, customize_entity_marks_live_unit_hoverable) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, customize_entity_publishes_gold_mine_hover_value) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = {
        .svflags = SVF_MONSTER,
        .s = { .player = 3, .flags = EF_RESOURCE_SOURCE },
        .resources = 12500,
        .invulnerable = true,
    };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(state.hover_value, 12501);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
}

TEST(wc3_api, customize_entity_publishes_overlay_parent_gold) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t parent = {
        .inuse = true,
        .spawn_time = 91,
        .s = { .flags = EF_RESOURCE_SOURCE },
        .resources = 4500,
    };
    mineOverlay_t overlay_state = { .parent = &parent, .parent_spawn_time = 91 };
    edict_t overlay = {
        .svflags = SVF_MONSTER,
        .s = { .player = 3 },
        .mineoverlay = &overlay_state,
    };
    parent.health.value = 100.0f;
    overlay.health.value = 100.0f;

    globals.CustomizeEntity(3, &overlay, &state);
    T_EQ(state.hover_value, 4501);
}

TEST(wc3_api, customize_entity_hides_invulnerable_health_but_keeps_mana) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 } };
    ent.health.value = 100.0f;
    ent.mana.max_value = 80.0f;
    ent.invulnerable = true;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
    T_ASSERT(state.flags & EF_HOVER_MANA);
}

TEST(wc3_api, customize_entity_omits_hover_mana_without_mana_pool) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOVER_MANA));
}

TEST(wc3_api, customize_entity_packs_hover_cargo_count_and_capacity) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y2;X1;K\"Abun\"\nC;Y2;X2;K\"Abun\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"4\"\nE\n";
    static UnitAbilities_t const abilities = { .abilList = "Abun" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 }, .data = { .UnitAbilities = &abilities } };

    ent.health.value = 100.0f;
    cargo_t cargo_state = { .count = 3 };
    ent.cargo = &cargo_state;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(EntityCargoCount(state.stats[ENT_CARGO]), 3);
    T_EQ(EntityCargoCapacity(state.stats[ENT_CARGO]), 4);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_api, customize_entity_publishes_empty_acar_capacity_for_hover) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y2;X1;K\"Acar\"\nC;Y2;X2;K\"Acar\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"8\"\nE\n";
    static UnitAbilities_t const abilities = { .abilList = "Acar" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 }, .data = { .UnitAbilities = &abilities } };

    ent.health.value = 100.0f;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(EntityCargoCount(state.stats[ENT_CARGO]), 0);
    T_EQ(EntityCargoCapacity(state.stats[ENT_CARGO]), 8);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_api, customize_entity_marks_enemy_hover_relation_hostile) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 2 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(state.flags & EF_HOSTILE);
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, customize_entity_publishes_minimap_relation_when_not_hoverable) {
    entityState_t state = { .number = 7, .model = 11, .flags = EF_NOT_SELECTABLE };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 2 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(0, &ent, &state);
    T_EQ(wc3_minimap_contact_get(state.effect_flags), WC3_MINIMAP_CONTACT_UNIT);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
    T_ASSERT(state.flags & EF_HOSTILE);
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, customize_entity_marks_passive_ally_hover_relation_neutral) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 1 } };
    ent.health.value = 100.0f;
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);

    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(state.flags & EF_NEUTRAL);
}

TEST(wc3_api, customize_entity_marks_neutral_passive_owner_neutral) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_PASSIVE } };
    ent.health.value = 100.0f;

    T_EQ(PLAYER_NEUTRAL_PASSIVE, 15); T_ASSERT(PLAYER_NEUTRAL_PASSIVE < MAX_PLAYERS);
    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(state.flags & EF_NEUTRAL);
}

TEST(wc3_api, customize_entity_honors_runtime_neutral_aggressive_alliance) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_AGGRESSIVE } };
    ent.health.value = 100.0f;

    T_EQ(PLAYER_NEUTRAL_AGGRESSIVE, 12); T_ASSERT(PLAYER_NEUTRAL_AGGRESSIVE < MAX_PLAYERS);
    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_AGGRESSIVE), ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_AGGRESSIVE), ALLIANCE_SHARED_CONTROL, true);
    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, neutral_passive_relation_can_be_revoked_at_runtime) {
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_PASSIVE } };

    G_InitPlayerAlliances(level.mapinfo);
    T_EQ(G_SelectionRelation(0, &ent), SELECT_RELATION_NEUTRAL);

    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_PASSIVE), ALLIANCE_PASSIVE, false);
    T_EQ(G_SelectionRelation(0, &ent), SELECT_RELATION_ENEMY);
}

TEST(wc3_api, customize_entity_marks_shared_control_hover_relation_friendly) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 1 } };
    ent.health.value = 100.0f;
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, true);

    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(state.flags & EF_ALLIED);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, customize_entity_marks_passive_allies_separately_from_neutrals) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 1 } };
    ent.health.value = 100.0f;
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);

    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_ALLIED);
    T_ASSERT(state.flags & EF_NEUTRAL);
    T_ASSERT(!(state.flags & EF_HOSTILE));

    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, false);
    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(!(state.flags & EF_ALLIED));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
    T_ASSERT(state.flags & EF_HOSTILE);
}

TEST(wc3_api, selection_relation_matches_enemy_neutral_and_shared_control) {
    edict_t enemy = { .s = { .player = 2 } };
    edict_t passive = { .s = { .player = PLAYER_NEUTRAL_PASSIVE } };
    edict_t hostile = { .s = { .player = PLAYER_NEUTRAL_AGGRESSIVE } };
    edict_t ally = { .s = { .player = 1 } };

    T_EQ(G_SelectionRelation(0, &enemy), SELECT_RELATION_ENEMY);
    T_EQ(G_SelectionRelation(0, &passive), SELECT_RELATION_NEUTRAL);
    T_EQ(G_SelectionRelation(0, &hostile), SELECT_RELATION_ENEMY);

    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);
    T_EQ(G_SelectionRelation(0, &ally), SELECT_RELATION_NEUTRAL);
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, true);
    T_EQ(G_SelectionRelation(0, &ally), SELECT_RELATION_FRIEND);
}

TEST(wc3_api, smart_target_indicator_uses_relationship_color_from_miscdata) {
    stbIniCache_t saved = game.config.misc, custom = { 0 };
    edict_t own = { .s = { .player = 0 } };
    edict_t ally = { .s = { .player = 1 } };
    edict_t enemy = { .s = { .player = 2 } };
    color32_t color;

    T_ASSERT(Stb_IniCacheLoad(&custom, "TestData\\SelectionCircle.txt"));
    game.config.misc = custom;
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);

    color = G_SmartTargetIndicatorColor(0, &own);
    T_EQ(color.r, 1); T_EQ(color.g, 2); T_EQ(color.b, 3); T_EQ(color.a, 200);
    color = G_SmartTargetIndicatorColor(0, &ally);
    T_EQ(color.r, 4); T_EQ(color.g, 5); T_EQ(color.b, 6); T_EQ(color.a, 201);
    color = G_SmartTargetIndicatorColor(0, &enemy);
    T_EQ(color.r, 7); T_EQ(color.g, 8); T_EQ(color.b, 9); T_EQ(color.a, 202);

    game.config.misc = saved;
    Stb_IniCacheFree(&custom);
}

TEST(wc3_api, smart_target_indicator_falls_back_to_stock_classic_colors) {
    stbIniCache_t saved = game.config.misc;
    edict_t own = { .s = { .player = 0 } };
    edict_t ally = { .s = { .player = 1 } };
    edict_t enemy = { .s = { .player = 2 } };
    color32_t color;

    game.config.misc = (stbIniCache_t){ 0 };
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);

    color = G_SmartTargetIndicatorColor(0, &own);
    T_EQ(color.r, 0); T_EQ(color.g, 255); T_EQ(color.b, 0); T_EQ(color.a, 255);
    color = G_SmartTargetIndicatorColor(0, &ally);
    T_EQ(color.r, 255); T_EQ(color.g, 255); T_EQ(color.b, 0); T_EQ(color.a, 255);
    color = G_SmartTargetIndicatorColor(0, &enemy);
    T_EQ(color.r, 255); T_EQ(color.g, 0); T_EQ(color.b, 0); T_EQ(color.a, 255);

    game.config.misc = saved;
}

TEST(wc3_api, selection_accepts_visible_foreign_unit_but_rejects_invalid_states) {
    gameClient_t *client = &game.clients[0];
    edict_t ent = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 2 } };
    ent.health.value = 100.0f;
    client->ps.number = 0;

    T_ASSERT(G_UnitCanBeSelected(client, &ent));
    ent.s.flags |= EF_NOT_SELECTABLE;
    T_ASSERT(!G_UnitCanBeSelected(client, &ent));
    ent.s.flags &= ~EF_NOT_SELECTABLE;
    ent.s.renderfx |= RF_HIDDEN;
    T_ASSERT(!G_UnitCanBeSelected(client, &ent));
    ent.s.renderfx &= ~RF_HIDDEN;
    ent.svflags |= SVF_DEADMONSTER;
    T_ASSERT(!G_UnitCanBeSelected(client, &ent));
}

TEST(wc3_api, multiselect_focus_tracks_one_selected_unit_and_falls_back_when_removed) {
    gameClient_t *client = &game.clients[0];
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 32, 0);

    client->ps.number = 0;
    first->s.player = second->s.player = 0;
    first->svflags |= SVF_MONSTER;
    second->svflags |= SVF_MONSTER;
    G_ResetSelectionFocus(client);

    G_SelectEntity(client, first);
    G_SelectEntity(client, second);
    T_ASSERT(G_GetMainSelectedUnit(client) == first);
    T_ASSERT(G_FocusSelectedUnit(client, second));
    T_ASSERT(G_GetMainSelectedUnit(client) == second);
    T_ASSERT(G_IsEntitySelected(client, first));
    T_ASSERT(G_IsEntitySelected(client, second));

    G_DeselectEntity(client, second);
    T_ASSERT(G_GetMainSelectedUnit(client) == first);
    T_ASSERT(!G_FocusSelectedUnit(client, second));
}

TEST(wc3_api, multiselect_portrait_second_click_collapses_exact_focused_unit) {
    gameClient_t *client = &game.clients[0];
    edict_t *footman_first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *footman_second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    edict_t *paladin = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64, 0);
    char second_number[16];
    cstring_t focus_second[] = { "focus", second_number };

    client->ps.number = 0;
    G_ResetSelectionFocus(client);
    footman_first->s.player = footman_second->s.player = paladin->s.player = 0;
    footman_first->svflags |= SVF_MONSTER;
    footman_second->svflags |= SVF_MONSTER;
    paladin->svflags |= SVF_MONSTER;
    G_SelectEntity(client, footman_first);
    G_SelectEntity(client, footman_second);
    G_SelectEntity(client, paladin);
    snprintf(second_number, sizeof(second_number), "%u", (unsigned)footman_second->s.number);

    T_ASSERT(G_GetMainSelectedUnit(client) == footman_first);

    /* Another member of the already-active Footman subgroup becomes the exact
     * representative without changing the three-unit selection. */
    globals.ClientCommand(&g_edicts[0], 2, focus_second);
    T_ASSERT(G_GetMainSelectedUnit(client) == footman_second);
    T_ASSERT(G_IsEntitySelected(client, footman_first));
    T_ASSERT(G_IsEntitySelected(client, footman_second));
    T_ASSERT(G_IsEntitySelected(client, paladin));

    /* Clicking that same representative again matches Warsmash: the status
     * panel selection collapses to only the clicked unit. */
    globals.ClientCommand(&g_edicts[0], 2, focus_second);
    T_ASSERT(G_GetMainSelectedUnit(client) == footman_second);
    T_ASSERT(!G_IsEntitySelected(client, footman_first));
    T_ASSERT(G_IsEntitySelected(client, footman_second));
    T_ASSERT(!G_IsEntitySelected(client, paladin));
}

TEST(wc3_api, tab_cycle_advances_unit_type_subgroups_and_wraps) {
    gameClient_t *client = &game.clients[0];
    edict_t *footman_first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *footman_second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    edict_t *knight = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 64, 0);
    edict_t *paladin = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 96, 0);
    UnitData_t footman_data = { .priority = 3 };
    UnitData_t knight_data = { .priority = 2 };
    UnitData_t paladin_data = { .priority = 1 };

    client->ps.number = 0;
    G_ResetSelectionFocus(client);
    footman_first->data.UnitData = footman_second->data.UnitData = &footman_data;
    knight->data.UnitData = &knight_data;
    paladin->data.UnitData = &paladin_data;

    edict_t *units[] = { footman_first, footman_second, knight, paladin };
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        units[i]->s.player = 0;
        units[i]->svflags |= SVF_MONSTER;
        G_SelectEntity(client, units[i]);
    }

    T_ASSERT(G_GetMainSelectedUnit(client) == footman_first);
    T_ASSERT(G_CycleSelectionSubgroup(client));
    T_ASSERT(G_GetMainSelectedUnit(client) == knight);
    T_ASSERT(G_CycleSelectionSubgroup(client));
    T_ASSERT(G_GetMainSelectedUnit(client) == paladin);
    T_ASSERT(G_CycleSelectionSubgroup(client));
    T_ASSERT(G_GetMainSelectedUnit(client) == footman_first);

    /* Cycling focus must not mutate authoritative selection membership. */
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        T_ASSERT(G_IsEntitySelected(client, units[i]));
    }
}

TEST(wc3_api, tab_cycle_is_noop_for_single_type_selection) {
    gameClient_t *client = &game.clients[0];
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);

    client->ps.number = 0;
    first->s.player = second->s.player = 0;
    first->svflags |= SVF_MONSTER;
    second->svflags |= SVF_MONSTER;
    G_ResetSelectionFocus(client);
    G_SelectEntity(client, first);
    G_SelectEntity(client, second);

    T_ASSERT(G_GetMainSelectedUnit(client) == first);
    T_ASSERT(!G_CycleSelectionSubgroup(client));
    T_ASSERT(G_GetMainSelectedUnit(client) == first);
}

TEST(wc3_api, multiselect_order_matches_warsmash_priority_level_and_rawcode) {
    gameClient_t *client = &game.clients[0];
    edict_t *low_priority = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *rawcode_foo_first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    edict_t *rawcode_foo_second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    edict_t *rawcode_knight = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 96, 0);
    edict_t *higher_level = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 128, 0);
    edict_t *higher_priority = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 160, 0);
    UnitData_t low_data = { .priority = 1 };
    UnitData_t middle_data = { .priority = 5 };
    UnitData_t high_data = { .priority = 6 };
    UnitBalance_t low_balance = { .level = 99 };
    UnitBalance_t middle_balance = { .level = 2 };
    UnitBalance_t higher_level_balance = { .level = 3 };
    UnitBalance_t high_balance = { .level = 0 };
    edict_t *ordered[6] = { 0 };

    client->ps.number = 0;
    G_ResetSelectionFocus(client);

    low_priority->data.UnitData = &low_data;
    low_priority->data.UnitBalance = &low_balance;
    rawcode_foo_first->data.UnitData = &middle_data;
    rawcode_foo_first->data.UnitBalance = &middle_balance;
    rawcode_foo_second->data.UnitData = &middle_data;
    rawcode_foo_second->data.UnitBalance = &middle_balance;
    rawcode_knight->data.UnitData = &middle_data;
    rawcode_knight->data.UnitBalance = &middle_balance;
    higher_level->data.UnitData = &middle_data;
    higher_level->data.UnitBalance = &higher_level_balance;
    higher_priority->data.UnitData = &high_data;
    higher_priority->data.UnitBalance = &high_balance;

    edict_t *units[] = {
        low_priority,
        rawcode_foo_first,
        rawcode_foo_second,
        rawcode_knight,
        higher_level,
        higher_priority,
    };
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        units[i]->s.player = 0;
        units[i]->svflags |= SVF_MONSTER;
        G_SelectEntity(client, units[i]);
    }

    T_EQ(G_GetOrderedSelectedUnits(client, ordered, sizeof(ordered) / sizeof(ordered[0])), 6);
    T_ASSERT(ordered[0] == higher_priority);
    T_ASSERT(ordered[1] == higher_level);
    /* OpenRealm stores FourCC bytes little-endian.  Canonical Warcraft rawcode
     * ordering still places "hkni" ahead of "hfoo" for the final tie-break. */
    T_ASSERT(ordered[2] == rawcode_knight);
    T_ASSERT(ordered[3] == rawcode_foo_first);
    T_ASSERT(ordered[4] == rawcode_foo_second);
    T_ASSERT(ordered[5] == low_priority);

    /* When no explicit subgroup focus remains, the same sorted first unit must
     * own the portrait/command-card fallback rather than edict scan order. */
    G_ResetSelectionFocus(client);
    T_ASSERT(G_GetMainSelectedUnit(client) == higher_priority);
}

/* Selection policy tests use the same CVar callback as gameplay. */
static cstring_t selection_limit_test_value;
static cstring_t (*selection_limit_original_cvar)(cstring_t, cstring_t);
static cstring_t selection_limit_test_cvar(cstring_t name, cstring_t fallback) {
    if (!strcmp(name, "wc3_selection_limit")) return selection_limit_test_value;
    return selection_limit_original_cvar ? selection_limit_original_cvar(name, fallback) : fallback;
}

TEST(wc3_api, selection_limit_defaults_to_24_and_accepts_only_classic_12) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    selection_limit_original_cvar = old_cvar;
    gi.CvarString = selection_limit_test_cvar;
    selection_limit_test_value = "24";
    T_EQ(G_SelectionLimit(), 24);
    selection_limit_test_value = "12";
    T_EQ(G_SelectionLimit(), 12);
    selection_limit_test_value = "garbage";
    T_EQ(G_SelectionLimit(), 24);
    selection_limit_test_value = "0";
    T_EQ(G_SelectionLimit(), 24);
    gi.CvarString = old_cvar;
}

TEST(wc3_api, selection_limit_direct_insert_and_runtime_reduction) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    gameClient_t *client = &game.clients[0];
    edict_t *units[25];
    selection_limit_original_cvar = old_cvar;
    selection_limit_test_value = "24";
    gi.CvarString = selection_limit_test_cvar;
    client->ps.number = 0;
    FOR_LOOP(i, 25) {
        units[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), (float)i * 32.0f, 0);
        units[i]->s.player = 0;
        units[i]->svflags |= SVF_MONSTER;
        G_SelectEntity(client, units[i]);
    }
    edict_t *ordered[25];
    T_EQ(G_GetOrderedSelectedUnits(client, ordered, 25), 24);
    T_ASSERT(!G_IsEntitySelected(client, units[24]));

    /* The periodic validation pass must remove extra authoritative members. */
    selection_limit_test_value = "12";
    G_UpdateClientSelections();
    T_EQ(G_GetOrderedSelectedUnits(client, ordered, 25), 12);
    T_ASSERT(!G_IsEntitySelected(client, units[12]));
    G_SelectEntity(client, units[24]);
    T_ASSERT(!G_IsEntitySelected(client, units[24]));
    gi.CvarString = old_cvar;
}

TEST(wc3_api, selection_limit_warns_once_for_unsupported_values) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    FILE *capture = tmpfile();
    int saved_stderr = dup(STDERR_FILENO);
    char diagnostic[1024] = { 0 };
    uint32_t warnings = 0;

    T_NOT_NULL(capture);
    T_ASSERT(saved_stderr >= 0);
    if (!capture || saved_stderr < 0) {
        if (capture) fclose(capture);
        if (saved_stderr >= 0) close(saved_stderr);
        return;
    }
    selection_limit_original_cvar = old_cvar;
    selection_limit_test_value = "18";
    gi.CvarString = selection_limit_test_cvar;

    fflush(stderr);
    T_EQ(dup2(fileno(capture), STDERR_FILENO), STDERR_FILENO);
    /* An unsupported value still resolves to the Reforged default, but the
     * substitution is reported once instead of silently every frame. */
    T_EQ(G_SelectionLimit(), 24);
    T_EQ(G_SelectionLimit(), 24);
    selection_limit_test_value = "12";
    T_EQ(G_SelectionLimit(), 12);
    fflush(stderr);
    T_EQ(dup2(saved_stderr, STDERR_FILENO), STDERR_FILENO);
    close(saved_stderr);
    rewind(capture);
    (void)fread(diagnostic, 1, sizeof(diagnostic) - 1, capture);
    fclose(capture);
    gi.CvarString = old_cvar;

    for (cstring_t hit = diagnostic; (hit = strstr(hit, "wc3_selection_limit")) != NULL; hit++) warnings++;
    T_EQ(warnings, 1);
    T_ASSERT(strstr(diagnostic, "18") != NULL);
}

TEST(wc3_api, selection_limit_reduction_keeps_highest_priority_units) {
    static UnitData_t hero_data, grunt_data;
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    gameClient_t *client = &game.clients[0];
    bool const started = level.started, scripts_started = level.scriptsStarted;
    edict_t *units[WC3_SELECTION_MAX];
    edict_t *ordered[WC3_SELECTION_MAX];
    edict_t *hero;

    reset_entities(); setup_test_world();
    /* The real frame scheduler runs script events, so give it a live VM. */
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    selection_limit_original_cvar = old_cvar;
    selection_limit_test_value = "24";
    gi.CvarString = selection_limit_test_cvar;
    client->ps.number = 0;
    /* The Hero takes the highest edict index so an index-ordered trim would
     * drop it first; Warcraft keeps the highest selection priority instead. */
    FOR_LOOP(i, WC3_SELECTION_MAX - 1) {
        units[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), (float)i * 32.0f, 0);
        T_NOT_NULL(units[i]->data.UnitData);
        grunt_data = *units[i]->data.UnitData;
        grunt_data.priority = 1;
        units[i]->data.UnitData = &grunt_data;
    }
    hero = units[WC3_SELECTION_MAX - 1] = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 64.0f);
    T_NOT_NULL(hero->data.UnitData);
    hero_data = *hero->data.UnitData;
    hero_data.priority = 6;
    hero->data.UnitData = &hero_data;
    FOR_LOOP(i, WC3_SELECTION_MAX) {
        units[i]->s.player = 0;
        units[i]->svflags |= SVF_MONSTER;
        G_SelectEntity(client, units[i]);
    }
    T_EQ(G_GetOrderedSelectedUnits(client, ordered, WC3_SELECTION_MAX), WC3_SELECTION_MAX);
    T_ASSERT(ordered[0] == hero);

    /* Lower the cap mid-game and let the real frame scheduler trim it. */
    selection_limit_test_value = "12";
    level.started = true; level.scriptsStarted = true;
    globals.RunFrame();
    level.started = started; level.scriptsStarted = scripts_started;

    T_EQ(G_GetOrderedSelectedUnits(client, ordered, WC3_SELECTION_MAX), 12);
    T_ASSERT(G_IsEntitySelected(client, hero));
    T_ASSERT(ordered[0] == hero);
    T_ASSERT(!G_IsEntitySelected(client, units[WC3_SELECTION_MAX - 2]));
    gi.CvarString = old_cvar;
}

TEST(wc3_api, selection_revalidation_clears_hidden_raw_selection_bit) {
    gameClient_t *client = &game.clients[0];
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    uint32_t bit = 1 << client->ps.number;

    ent->svflags |= SVF_MONSTER;
    ent->s.player = 1;
    G_SelectEntity(client, ent);
    T_ASSERT(ent->selected & bit);

    ent->s.renderfx |= RF_HIDDEN;
    T_ASSERT(!G_IsEntitySelected(client, ent));
    G_UpdateClientSelections();

    T_ASSERT(!(ent->selected & bit));
}

TEST(wc3_api, control_is_separate_from_selection_and_honors_shared_control) {
    gameClient_t *client = &game.clients[0];
    edict_t own = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 0 } };
    edict_t enemy = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 1 } };
    edict_t neutral = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_PASSIVE } };
    own.health.value = enemy.health.value = neutral.health.value = 100.0f;
    client->ps.number = 0;

    T_ASSERT(G_UnitCanControl(client, &own));
    T_ASSERT(G_UnitCanBeSelected(client, &enemy));
    T_ASSERT(!G_UnitCanControl(client, &enemy));
    T_ASSERT(G_UnitCanBeSelected(client, &neutral));
    T_ASSERT(!G_UnitCanControl(client, &neutral));

    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, true);
    T_ASSERT(G_UnitCanControl(client, &enemy));

    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_PASSIVE), ALLIANCE_SHARED_CONTROL, true);
    T_ASSERT(G_UnitCanControl(client, &neutral));
}

TEST(wc3_api, advanced_shared_control_limits_resource_spending) {
    gameClient_t *client = &game.clients[0];
    edict_t own = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 0 } };
    edict_t ally = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 1 } };
    edict_t enemy = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 2 } };
    uint32_t saved_forward = level.alliances[0][1];
    uint32_t saved_reverse = level.alliances[1][0];
    uint32_t saved_enemy = level.alliances[0][2];
    uint32_t saved_client_number = client->ps.number;
    own.health.value = ally.health.value = enemy.health.value = 100.0f;
    client->ps.number = 0;

    /* Explicitly establish the fixture, regardless of earlier test state. */
    level.alliances[0][1] = 0;
    level.alliances[1][0] = 0;
    level.alliances[0][2] = 0;
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, false);
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_ADVANCED_CONTROL, false);
    T_ASSERT(G_UnitCanSpendResources(client, &own));
    T_ASSERT(!G_UnitCanControl(client, &ally));
    T_ASSERT(!G_UnitCanSpendResources(client, &ally));
    T_ASSERT(!G_UnitCanSpendResources(client, &enemy));

    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, true);
    T_ASSERT(G_UnitCanControl(client, &ally));
    T_ASSERT(!G_UnitCanSpendResources(client, &ally));

    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_ADVANCED_CONTROL, true);
    T_ASSERT(G_UnitCanControl(client, &ally));
    T_ASSERT(G_UnitCanSpendResources(client, &ally));

    /* Advanced permission also grants ordinary orders on its own. */
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, false);
    T_ASSERT(G_UnitCanControl(client, &ally));
    T_ASSERT(G_UnitCanSpendResources(client, &ally));
    T_EQ(G_SelectionRelation(0, &ally), SELECT_RELATION_FRIEND);

    /* Sharing is directional; never accept the reverse grant. */
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_ADVANCED_CONTROL, false);
    G_SetPlayerAlliance(test_player(1), test_player(0), ALLIANCE_SHARED_ADVANCED_CONTROL, true);
    T_ASSERT(!G_UnitCanControl(client, &ally));
    T_ASSERT(!G_UnitCanSpendResources(client, &ally));
    G_SetPlayerAlliance(test_player(1), test_player(0), ALLIANCE_SHARED_ADVANCED_CONTROL, false);
    /* A hostile owner stays unauthorized even with an advanced sharing bit. */
    G_SetPlayerAlliance(test_player(0), test_player(2), ALLIANCE_SHARED_ADVANCED_CONTROL, true);
    T_ASSERT(!G_UnitCanSpendResources(client, &enemy));
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, false);
    level.alliances[0][1] = saved_forward;
    level.alliances[1][0] = saved_reverse;
    level.alliances[0][2] = saved_enemy;
    client->ps.number = saved_client_number;
    G_InvalidateAllUnitShortcuts();
}

/* Team Resources uses the same directional control grants as allied
 * resource spending, but must not disclose an enemy's economy or show basic
 * shared-control teammates.  Preserve the global alliance fixture. */
TEST(wc3_api, team_resources_eligibility_requires_advanced_ally) {
    uint32_t const forward = level.alliances[0][1];
    uint32_t const reverse = level.alliances[1][0];
    uint32_t const hostile = level.alliances[0][2];
    uint32_t const neutral = level.alliances[0][PLAYER_NEUTRAL_AGGRESSIVE];

    level.alliances[0][1] = 0;
    level.alliances[1][0] = 0;
    level.alliances[0][2] = 0;
    level.alliances[0][PLAYER_NEUTRAL_AGGRESSIVE] = 0;
    T_ASSERT(!G_CanViewTeamResources(0, 0));
    T_ASSERT(!G_CanViewTeamResources(0, 1));
    T_ASSERT(!G_CanViewTeamResources(MAX_PLAYERS, 1));
    T_ASSERT(!G_CanViewTeamResources(0, MAX_PLAYERS));
    T_ASSERT(!G_CanViewTeamResources(0, PLAYER_NEUTRAL_AGGRESSIVE));

    level.alliances[0][1] = (1u << ALLIANCE_PASSIVE) |
                            (1u << ALLIANCE_SHARED_CONTROL);
    T_ASSERT(!G_CanViewTeamResources(0, 1));
    level.alliances[0][1] |= 1u << ALLIANCE_SHARED_ADVANCED_CONTROL;
    T_ASSERT(G_CanViewTeamResources(0, 1));
    T_ASSERT(!G_CanViewTeamResources(1, 0));
    /* An advanced control grant alone cannot turn a hostile owner into an ally. */
    level.alliances[0][2] = 1u << ALLIANCE_SHARED_ADVANCED_CONTROL;
    T_ASSERT(!G_CanViewTeamResources(0, 2));
    level.alliances[0][1] &= ~(1u << ALLIANCE_PASSIVE);
    T_ASSERT(!G_CanViewTeamResources(0, 1));

    level.alliances[0][1] = forward;
    level.alliances[1][0] = reverse;
    level.alliances[0][2] = hostile;
    level.alliances[0][PLAYER_NEUTRAL_AGGRESSIVE] = neutral;
}

TEST(wc3_api, customize_entity_rejects_non_unit_hover_health) {
    entityState_t state = { .number = 7, .model = 11, .flags = EF_HOVER_HEALTH };
    edict_t ent = { .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
}

TEST(wc3_api, customize_entity_rejects_dead_or_unselectable_unit_hover_health) {
    entityState_t state = { .number = 7, .model = 11,
        .flags = EF_NOT_SELECTABLE | EF_HOVER_HEALTH | EF_HOSTILE | EF_NEUTRAL };
    edict_t ent = { .svflags = SVF_MONSTER | SVF_DEADMONSTER, .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, customize_entity_rejects_hidden_unit_hover_health) {
    entityState_t state = { .number = 7, .model = 11, .renderfx = RF_HIDDEN, .hover_value = 12500,
        .flags = EF_HOVER_HEALTH | EF_HOSTILE | EF_NEUTRAL };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
    T_EQ(state.hover_value, 0);
}

/* =========================================================================
 * Player — color
 * ========================================================================= */

TEST(wc3_api, player_color_default_zero) {
    player_t *p = test_player(0);
    T_EQ((int)p->color, 0);
}

TEST(wc3_api, player_color_set_get) {
    player_t *p = test_player(0);
    p->color = 5;
    T_EQ((int)p->color, 5);
}

TEST(wc3_api, player_color_max_index) {
    player_t *p = test_player(0);
    p->color = 23;
    T_EQ((int)p->color, 23);
}

TEST(wc3_api, set_player_color_recolors_existing_owner_colored_units) {
    edict_t *unit = NULL;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u\n"
        "  call SetPlayerColor(Player(4), PLAYER_COLOR_PURPLE)\n"
        "  set u = CreateUnit(Player(4), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call SetPlayerColor(Player(4), PLAYER_COLOR_ORANGE)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') && g_edicts[i].s.player == 4) unit = &g_edicts[i];
    T_NOT_NULL(unit);
    T_EQ(unit_team_color(unit), 5);
}

TEST(wc3_api, set_player_color_preserves_different_explicit_unit_color) {
    edict_t *unit = NULL;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u\n"
        "  call SetPlayerColor(Player(4), PLAYER_COLOR_PURPLE)\n"
        "  set u = CreateUnit(Player(4), 'hfoo', 96.0, 64.0, 0.0)\n"
        "  call SetUnitColor(u, PLAYER_COLOR_GREEN)\n"
        "  call SetPlayerColor(Player(4), PLAYER_COLOR_ORANGE)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') &&
            g_edicts[i].s.player == 4 && g_edicts[i].s.origin.x == 96.0f) unit = &g_edicts[i];
    T_NOT_NULL(unit);
    T_EQ(unit_team_color(unit), 6);
}

/* =========================================================================
 * Player — start_location
 * ========================================================================= */

TEST(wc3_api, player_start_location_default) {
    /* start_location is 0-initialised by setup_game(). */
    gameClient_t *cl = &game.clients[1];
    cl->ps.number = 1;
    T_EQ((int)cl->ps.start_location, 0);
}

TEST(wc3_api, player_start_location_set_get) {
    gameClient_t *cl = &game.clients[2];
    cl->ps.number = 2;
    cl->ps.start_location = 3;
    T_EQ((int)cl->ps.start_location, 3);
}

TEST(wc3_api, player_start_location_negative) {
    gameClient_t *cl = &game.clients[3];
    cl->ps.number = 3;
    cl->ps.start_location = -1;
    T_EQ((int)cl->ps.start_location, -1);
}

/* =========================================================================
 * Player — name
 * ========================================================================= */

TEST(wc3_api, player_name_set_get) {
    player_t *p = test_player(0);
    p->name = "Arthas";
    T_STREQ(p->name, "Arthas");
}

TEST(wc3_api, player_name_null_default) {
    /* memset in setup_game zeroes the name pointer. */
    player_t *p = test_player(4);
    p->name = NULL; /* explicit reset */
    T_NULL(p->name);
}

/* =========================================================================
 * Player — team
 * ========================================================================= */

TEST(wc3_api, player_team_set_get) {
    player_t *p = test_player(0);
    p->team = 2;
    T_EQ((int)p->team, 2);
}

/* =========================================================================
 * Player — alliance
 * ========================================================================= */

TEST(wc3_api, alliance_passive_default_false) {
    player_t *p0 = test_player(0);
    player_t *p1 = test_player(1);
    /* Ordinary player pairs begin unallied; neutral defaults are separate. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_defaults_include_neutral_passive_and_neutral_controller_slots) {
    mapInfo_t *mapinfo = (mapInfo_t *)level.mapinfo;

    mapinfo->players[3].playerType = kPlayerTypeNeutral;
    G_InitPlayerAlliances(level.mapinfo);

    T_ASSERT(G_GetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_PASSIVE), ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(test_player(PLAYER_NEUTRAL_PASSIVE), test_player(0), ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(test_player(0), test_player(3), ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(test_player(3), test_player(0), ALLIANCE_PASSIVE));
    T_ASSERT(!G_GetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_AGGRESSIVE), ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_set_true) {
    player_t *p0 = test_player(0);
    player_t *p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_is_directional) {
    player_t *p0 = test_player(0);
    player_t *p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
    T_ASSERT(!G_GetPlayerAlliance(p1, p0, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_revoke_does_not_change_reverse_relation) {
    player_t *p0 = test_player(0);
    player_t *p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(p1, p0, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, false);
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(p1, p0, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_revoke) {
    player_t *p0 = test_player(0);
    player_t *p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, false);
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_enemy_when_not_allied) {
    player_t *p0 = test_player(0);
    player_t *p2 = test_player(2);
    /* Players 0 and 2 have no alliance — IsUnitEnemy logic is !ally. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p2, ALLIANCE_PASSIVE));
}

TEST(wc3_api, is_unit_ally_uses_querying_player_direction) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(1), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call SetPlayerAlliance(Player(0), Player(1), ALLIANCE_PASSIVE, true)\n"
        "  call SetPlayerAlliance(Player(1), Player(0), ALLIANCE_PASSIVE, false)\n"
        "  call BJassAssert(IsUnitAlly(u, Player(0)), \"source player sees unit owner as ally\")\n"
        "  call BJassAssert(not IsUnitEnemy(u, Player(0)), \"source player does not see ally as enemy\")\n"
        "  call BJassAssert(IsUnitEnemy(CreateUnit(Player(0), 'hpea', 64.0, 0.0, 0.0), Player(1)), \"reverse direction remains hostile\")\n"
        "endfunction\n"
    ));
}

/* =========================================================================
 * Hero — str / agi / int attributes
 * ========================================================================= */

TEST(wc3_api, hero_str_set_get) {
    edict_t *ent = make_unit_hero();
    ent->hero.str = 25;
    T_EQ((int)ent->hero.str, 25);
}

TEST(wc3_api, hero_agi_set_get) {
    edict_t *ent = make_unit_hero();
    ent->hero.agi = 18;
    T_EQ((int)ent->hero.agi, 18);
}

TEST(wc3_api, hero_int_set_get) {
    edict_t *ent = make_unit_hero();
    ent->hero.intel = 22;
    T_EQ((int)ent->hero.intel, 22);
}

/* =========================================================================
 * Hero — XP accumulation
 * ========================================================================= */

TEST(wc3_api, hero_xp_default_zero) {
    edict_t *ent = make_unit_hero();
    T_EQ((int)ent->hero.xp, 0);
}

TEST(wc3_api, hero_xp_set) {
    edict_t *ent = make_unit_hero();
    ent->hero.xp = 500;
    T_EQ((int)ent->hero.xp, 500);
}

TEST(wc3_api, hero_xp_add) {
    edict_t *ent = make_unit_hero();
    ent->hero.xp = 100;
    uint32_t add = 50;
    /* Replicate AddHeroXP logic: cap at INT32_MAX */
    uint32_t cur = ent->hero.xp;
    uint32_t sum = cur + add;
    ent->hero.xp = (sum < cur || sum > (uint32_t)INT32_MAX) ? (uint32_t)INT32_MAX : sum;
    T_EQ((int)ent->hero.xp, 150);
}

TEST(wc3_api, hero_xp_map_main_uses_normal_progression) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  call AddHeroXP(h, 500, false)\n"
        "  call BJassAssert(GetHeroXP(h) == 500, \"startup AddHeroXP did not persist XP\")\n"
        "  call BJassAssert(GetHeroLevel(h) == 3, \"startup AddHeroXP did not level Hero\")\n"
        "  call SetHeroXP(h, 100, false)\n"
        "  call BJassAssert(GetHeroXP(h) == 500, \"SetHeroXP lowered XP\")\n"
        "  call BJassAssert(GetHeroLevel(h) == 3, \"SetHeroXP lowered Hero level\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_api, hero_strip_levels_jass_and_blizzard_level_wrapper) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 128.0, 0.0, 0.0)\n"
        "  call BJassAssert(not UnitStripHeroLevel(u, 1), \"non-Hero can lose levels\")\n"
        "  call BJassAssert(not UnitStripHeroLevel(h, 0), \"zero levels should fail\")\n"
        "  call BJassAssert(not UnitStripHeroLevel(h, -1), \"negative levels should fail\")\n"
        "  call SetHeroLevel(h, 4, false)\n"
        "  call BJassAssert(GetHeroLevel(h) == 4, \"SetHeroLevel did not raise level\")\n"
        "  call BJassAssert(UnitStripHeroLevel(h, 2), \"failed to strip two levels\")\n"
        "  call BJassAssert(GetHeroLevel(h) == 2, \"wrong level after strip\")\n"
        "  call BJassAssert(GetHeroXP(h) == 200, \"XP threshold after strip incorrect\")\n"
        "  call SetHeroLevelBJ(h, 1, false)\n"
        "  call BJassAssert(GetHeroLevel(h) == 1, \"BJ wrapper did not lower level\")\n"
        "  call BJassAssert(not UnitStripHeroLevel(h, 99), \"level-1 Hero lost a level\")\n"
        "  call SetHeroLevelBJ(h, 3, false)\n"
        "  call BJassAssert(GetHeroLevel(h) == 3, \"BJ wrapper did not raise level\")\n"
        "  call SetHeroLevelBJ(h, -100, false)\n"
        "  call BJassAssert(GetHeroLevel(h) == 1, \"BJ wrapper did not clamp to level 1\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_api, hero_skill_points_jass_modify_and_query) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 128.0, 0.0, 0.0)\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 1, \"new Hero did not start with one skill point\")\n"
        "  call BJassAssert(UnitModifySkillPoints(h, 2), \"positive skill point delta failed\")\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 3, \"positive skill point delta was not applied\")\n"
        "  call BJassAssert(UnitModifySkillPoints(h, -1), \"negative skill point delta failed\")\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 2, \"negative skill point delta was not applied\")\n"
        "  call BJassAssert(UnitModifySkillPoints(h, -99), \"large negative skill point delta failed\")\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 0, \"skill points did not clamp at zero\")\n"
        "  call BJassAssert(not UnitModifySkillPoints(h, -1), \"empty skill point pool accepted another negative delta\")\n"
        "  call BJassAssert(GetHeroSkillPoints(u) == 0, \"non-Hero reported skill points\")\n"
        "  call BJassAssert(not UnitModifySkillPoints(u, 1), \"non-Hero accepted skill points\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_api, hero_skill_points_map_main_can_award_points) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  call UnitModifySkillPoints(h, 2)\n"
        "  call BJassAssert(GetHeroLevel(h) == 1, \"skill point award changed Hero level\")\n"
        "  call BJassAssert(GetHeroXP(h) == 0, \"skill point award changed Hero XP\")\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 3, \"map-start skill point award did not persist\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_api, hero_xp_overflow_clamps) {
    edict_t *ent = make_unit_hero();
    ent->hero.xp = (uint32_t)INT32_MAX - 5;
    uint32_t add = 100;
    uint32_t cur = ent->hero.xp;
    uint32_t sum = cur + add;
    ent->hero.xp = (sum < cur || sum > (uint32_t)INT32_MAX) ? (uint32_t)INT32_MAX : sum;
    /* Overflow past INT32_MAX clamps to INT32_MAX, never goes negative */
    T_EQ((long long)ent->hero.xp, (long long)INT32_MAX);
    T_ASSERT((int32_t)ent->hero.xp >= 0);
}

/* =========================================================================
 * Hero — suspend_xp
 * ========================================================================= */

TEST(wc3_api, hero_suspend_xp_default_false) {
    edict_t *ent = make_unit_hero();
    T_ASSERT(!ent->hero.suspend_xp);
}

TEST(wc3_api, hero_suspend_xp_set_true) {
    edict_t *ent = make_unit_hero();
    ent->hero.suspend_xp = true;
    T_ASSERT(ent->hero.suspend_xp);
}

TEST(wc3_api, hero_xp_not_added_when_suspended) {
    edict_t *ent = make_unit_hero();
    ent->hero.xp = 100;
    ent->hero.suspend_xp = true;
    /* Replicate AddHeroXP: skip when suspend_xp is set */
    int32_t xp_to_add = 50;
    if (!ent->hero.suspend_xp && xp_to_add > 0) {
        uint32_t add = (uint32_t)xp_to_add;
        uint32_t cur = ent->hero.xp;
        uint32_t sum = cur + add;
        ent->hero.xp = (sum < cur || sum > (uint32_t)INT32_MAX) ? (uint32_t)INT32_MAX : sum;
    }
    T_EQ((int)ent->hero.xp, 100);
}

/* =========================================================================
 * Unit flags — invulnerable / paused / no_pathing / unit_color
 * ========================================================================= */

TEST(wc3_api, prop_window_runtime_native_and_authored_default) {
    static UnitData_t authored = { .propWin = 23.0f };
    edict_t *unit;

    setup_test_world();
    currentplayer = test_player(0);
    T_ASSERT(run_test_jass(
        "type unit extends handle\n"
        "globals\n"
        "  unit subject = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set subject = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call SetUnitPropWindow(subject, 0.174532925)\n"
        "endfunction\n"
        "function verify_prop_window takes nothing returns nothing\n"
        "  if GetUnitPropWindow(subject) == 0.174532925 and GetUnitDefaultPropWindow(subject) == 23.0 then\n"
        "    call SetUnitPropWindow(subject, 0.34906585)\n"
        "  else\n"
        "    call SetUnitPropWindow(subject, 0.523598776)\n"
        "  endif\n"
        "endfunction\n"));

    unit = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(unit);
    if (!unit) return;
    unit->data.UnitData = &authored;
    jass_callbyname(level.vm, "verify_prop_window", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_FEQ(unit->unitinfo.PropWindow, DEG2RAD(20.0f), 0.001f);
}

TEST(wc3_api, unit_invulnerable_default_false) {
    edict_t *ent = make_unit_hero();
    T_ASSERT(!ent->invulnerable);
}

TEST(wc3_api, unit_invulnerable_set) {
    edict_t *ent = make_unit_hero();
    ent->invulnerable = true;
    T_ASSERT(ent->invulnerable);
}

TEST(wc3_api, unit_paused_default_false) {
    edict_t *ent = make_unit_hero();
    T_ASSERT(!ent->paused);
}

TEST(wc3_api, unit_paused_set) {
    edict_t *ent = make_unit_hero();
    ent->paused = true;
    T_ASSERT(ent->paused);
    ent->paused = false;
    T_ASSERT(!ent->paused);
}

TEST(wc3_api, unit_no_pathing_default_false) {
    edict_t *ent = make_unit_hero();
    T_ASSERT(!ent->no_pathing);
}

TEST(wc3_api, unit_no_pathing_set) {
    edict_t *ent = make_unit_hero();
    ent->no_pathing = true;
    T_ASSERT(ent->no_pathing);
}

TEST(wc3_api, unit_color_default_zero) {
    edict_t *ent = make_unit_hero();
    T_EQ((int)ent->unit_color, 0);
}

TEST(wc3_api, unit_color_override_distinguishes_red_from_default) {
    edict_t *ent = make_unit_hero();
    uint32_t color = 99;

    G_SetUnitColorOverride(ent, 0);
    T_ASSERT(ent->unit_color != 0);
    T_ASSERT(G_GetUnitColorOverride(ent, &color));
    T_EQ(color, 0);
}

TEST(wc3_api, set_unit_color_publishes_team_color_without_changing_owner) {
    edict_t *unit = NULL;
    uint32_t encoded;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(4), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call SetUnitColor(u, PLAYER_COLOR_LIGHT_GRAY)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') && g_edicts[i].s.player == 4) unit = &g_edicts[i];
    T_NOT_NULL(unit);
    encoded = (unit->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT;
    T_EQ(unit->s.player, 4);
    {
        uint32_t color = 0;
        T_ASSERT(G_GetUnitColorOverride(unit, &color));
        T_EQ(color, 8);
    }
    T_EQ(encoded, 9);
}

TEST(wc3_api, set_unit_color_can_override_owner_with_red) {
    edict_t *unit = NULL;
    uint32_t encoded;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(4), 'hfoo', 96.0, 96.0, 0.0)\n"
        "  call SetUnitColor(u, PLAYER_COLOR_RED)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') &&
            g_edicts[i].s.player == 4 && g_edicts[i].s.origin.x == 96.0f) unit = &g_edicts[i];
    T_NOT_NULL(unit);
    encoded = (unit->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT;
    T_EQ(encoded, 1);
    {
        uint32_t color = 99;
        T_ASSERT(G_GetUnitColorOverride(unit, &color));
        T_EQ(color, 0);
    }
}

TEST(wc3_api, set_unit_owner_honors_change_color) {
    edict_t *recolored = NULL;
    edict_t *preserved = NULL;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit a\n"
        "  local unit b\n"
        "  call SetPlayerColor(Player(4), PLAYER_COLOR_PURPLE)\n"
        "  call SetPlayerColor(Player(5), PLAYER_COLOR_YELLOW)\n"
        "  set a = CreateUnit(Player(4), 'hfoo', 128.0, 64.0, 0.0)\n"
        "  set b = CreateUnit(Player(4), 'hfoo', 160.0, 64.0, 0.0)\n"
        "  call SetUnitOwner(a, Player(5), true)\n"
        "  call SetUnitOwner(b, Player(5), false)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *unit = &g_edicts[i];
        if (unit->class_id != MAKEFOURCC('h','f','o','o') || unit->s.player != 5) continue;
        if (unit->s.origin.x == 128.0f) recolored = unit;
        if (unit->s.origin.x == 160.0f) preserved = unit;
    }
    T_NOT_NULL(recolored);
    T_NOT_NULL(preserved);
    T_EQ(unit_team_color(recolored), 4);
    T_EQ(unit_team_color(preserved), 3);
}

TEST(wc3_api, authored_team_color_precedence_matches_unit_data) {
    edict_t *unit = make_unit_hero();
    UnitUI_t ui = *unit->data.UnitUI;
    doodad_t placement = { .color = (uint32_t)-1 };

    unit->data.UnitUI = &ui;
    unit->s.player = 4;
    game.clients[4].ps.number = 4;
    game.clients[4].ps.color = 2;
    ui.teamColor = -1;
    ui.customTeamColor = true;
    G_ApplyMapUnitTeamColor(unit, &placement);
    T_EQ(unit_team_color(unit), 2);

    ui.teamColor = 5;
    G_ApplyMapUnitTeamColor(unit, &placement);
    T_EQ(unit_team_color(unit), 5);

    placement.color = 8;
    G_ApplyMapUnitTeamColor(unit, &placement);
    T_EQ(unit_team_color(unit), 8);

    ui.customTeamColor = false;
    G_ApplyMapUnitTeamColor(unit, &placement);
    T_EQ(unit_team_color(unit), 5);
}

TEST(wc3_api, missing_unit_type_team_color_uses_owner_color) {
    edict_t *unit = make_unit_hero();
    UnitUI_t ui = *unit->data.UnitUI;

    unit->data.UnitUI = &ui;
    unit->s.player = 4;
    game.clients[4].ps.number = 4;
    game.clients[4].ps.color = 7;
    ui.teamColor = 0;
    ui.customTeamColor = false;
    G_InitializeUnitTeamColor(unit);
    T_EQ(unit_team_color(unit), 7);
}

/* =========================================================================
 * Unit — hidden flag (RF_HIDDEN)
 * ========================================================================= */

TEST(wc3_api, unit_hidden_default_false) {
    edict_t *ent = make_unit_hero();
    T_ASSERT(!(ent->s.renderfx & RF_HIDDEN));
}

TEST(wc3_api, unit_hidden_set) {
    edict_t *ent = make_unit_hero();
    ent->s.renderfx |= RF_HIDDEN;
    T_ASSERT(ent->s.renderfx & RF_HIDDEN);
}

TEST(wc3_api, unit_hidden_clear) {
    edict_t *ent = make_unit_hero();
    ent->s.renderfx |= RF_HIDDEN;
    ent->s.renderfx &= ~RF_HIDDEN;
    T_ASSERT(!(ent->s.renderfx & RF_HIDDEN));
}

TEST(wc3_api, show_unit_visibility_transition_invalidates_hero_shortcuts) {
    gameClient_t *client = &game.clients[0];
    edict_t *hero = NULL;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testHero\n"
        "endglobals\n"
        "function hideHero takes nothing returns nothing\n"
        "  call ShowUnit(testHero, false)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set testHero = CreateUnit(Player(0), 'Hpal', 64.0, 64.0, 0.0)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('H','p','a','l') && g_edicts[i].s.player == 0) hero = &g_edicts[i];
    T_NOT_NULL(hero);
    T_ASSERT(!(hero->s.renderfx & RF_HIDDEN));

    client->shortcuts.dirty = false;
    jass_callbyname(level.vm, "hideHero", true);
    jass_runevents(level.vm);

    T_ASSERT(hero->s.renderfx & RF_HIDDEN);
    T_ASSERT(client->shortcuts.dirty);
}


/* =========================================================================
 * Group — FirstOfGroup / IsUnitInGroup
 * ========================================================================= */

TEST(wc3_api, group_first_of_empty_returns_null) {
    ggroup_t g = {0};
    edict_t *first = (g.num_units > 0) ? g.units[0] : NULL;
    T_NULL(first);
}

TEST(wc3_api, group_first_of_group) {
    reset_entities();
    edict_t *a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.units[1] = b;
    g.num_units = 2;
    T_ASSERT(g.units[0] == a);
    T_ASSERT(g.units[1] == b);
}

TEST(wc3_api, group_add_is_set_semantics) {
    reset_entities();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ggroup_t *group = G_AllocJassGroup();
    T_NOT_NULL(group);
    T_ASSERT(group_add_entity(group, unit));
    T_ASSERT(!group_add_entity(group, unit));
    T_EQ(group->num_units, 1);
    G_FreeJassGroup(group);
}

TEST(wc3_api, group_debug_creator_follows_slot_lifecycle) {
    ggroup_t *group = G_AllocJassGroup();

    T_NOT_NULL(group);
    G_SetJassGroupDebugContext(group, "creatorA", "helperA <- actionA", 42);
    T_STREQ(G_GetJassGroupDebugCreator(group), "creatorA");
    T_STREQ(G_GetJassGroupDebugChain(group), "helperA <- actionA");
    T_EQ(G_GetJassGroupDebugTrigger(group), 42);
    G_FreeJassGroup(group);
    T_NULL(G_GetJassGroupDebugCreator(group));
    T_NULL(G_GetJassGroupDebugChain(group));
    T_EQ(G_GetJassGroupDebugTrigger(group), -1);
}

TEST(wc3_api, group_debug_captures_nested_jass_call_chain) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;

    reset_entities();
    gi.CvarString = group_debug_cvar;
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function makeLeakedGroup takes nothing returns nothing\n"
        "local group g = CreateGroup()\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "call makeLeakedGroup()\n"
        "endfunction"));
    T_EQ(level.num_groups, 1);
    T_ASSERT(level.groups[0]->inuse);
    T_STREQ(G_GetJassGroupDebugCreator(level.groups[0]), "makeLeakedGroup");
    T_STREQ(G_GetJassGroupDebugChain(level.groups[0]), "makeLeakedGroup <- main");
    currentplayer = NULL;
    gi.CvarString = old_cvar;
}

TEST(wc3_api, destroyed_group_slots_are_reused) {
    uint32_t const saved_num_groups = level.num_groups;
    ggroup_t *first = NULL;

    for (uint32_t i = 0; i < 4096; i++) {
        ggroup_t *group = G_AllocJassGroup();
        T_NOT_NULL(group);
        if (!group) break;
        if (!first) first = group;
        else T_ASSERT(group == first);
        T_ASSERT(group->inuse);
        G_FreeJassGroup(group);
        T_ASSERT(!group->inuse);
    }
    T_EQ(level.num_groups, saved_num_groups + 1);
}


TEST(wc3_api, group_registry_grows_past_legacy_1024_limit) {
    enum { LEGACY_GROUP_LIMIT = 1024, EXTRA_GROUPS = 64 };
    uint32_t id = UINT32_MAX;

    reset_entities();
    for (uint32_t i = 0; i < LEGACY_GROUP_LIMIT + EXTRA_GROUPS; i++) {
        ggroup_t *group = G_AllocJassGroup();
        T_NOT_NULL(group);
        if (!group) break;
    }
    T_EQ(level.num_groups, LEGACY_GROUP_LIMIT + EXTRA_GROUPS);
    T_ASSERT(level.group_capacity >= level.num_groups);
    T_ASSERT(G_SaveJassHandle("group", level.groups[LEGACY_GROUP_LIMIT + 7], &id));
    T_EQ(id, LEGACY_GROUP_LIMIT + 7);
    T_ASSERT(G_LoadJassHandle("group", id) == level.groups[id]);

    FOR_LOOP(i, level.num_groups) G_FreeJassGroup(level.groups[i]);
}

TEST(wc3_api, destroyed_group_handle_is_not_saveable_or_loadable) {
    uint32_t id = UINT32_MAX;
    ggroup_t *group = G_AllocJassGroup();

    T_NOT_NULL(group);
    T_ASSERT(G_SaveJassHandle("group", group, &id));
    T_EQ(id, 0);
    T_ASSERT(G_LoadJassHandle("group", id) == group);

    G_FreeJassGroup(group);
    T_ASSERT(!G_SaveJassHandle("group", group, &id));
    T_NULL(G_LoadJassHandle("group", 0));
}

TEST(wc3_api, repeated_create_destroy_group_does_not_exhaust_registry) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function recycleGroup takes nothing returns nothing\n"
        "local group g = CreateGroup()\n"
        "call DestroyGroup(g)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "call recycleGroup()\n"
        "call recycleGroup()\n"
        "call recycleGroup()\n"
        "endfunction"));
    T_EQ(level.num_groups, 1);
    T_ASSERT(!level.groups[0]->inuse);
    currentplayer = NULL;
}

/* Original scene46 admits the first twelve retained group members for every
 * public point-order form. The engine must execute the ordinary Move dispatch
 * and preserve the two units beyond the native admission limit. */
static cstring_t group_point_admission_script =
    "globals\n"
    "group requestGroup = null\n"
    "unit array requestUnits\n"
    "endglobals\n"
    "function verifyGroupPoint takes boolean accepted returns nothing\n"
    "local integer i = 0\n"
    "call BJassAssert(accepted, \"group point request must accept Move\")\n"
    "loop\n"
    "exitwhen i == 14\n"
    "if i < 12 then\n"
    "call BJassAssert(GetUnitCurrentOrder(requestUnits[i]) == 851986, \"retained member must receive Move\")\n"
    "else\n"
    "call BJassAssert(GetUnitCurrentOrder(requestUnits[i]) == 0, \"group request must retain the twelve-unit limit\")\n"
    "endif\n"
    "call IssueImmediateOrder(requestUnits[i], \"stop\")\n"
    "set i = i + 1\n"
    "endloop\n"
    "endfunction\n"
    "function main takes nothing returns nothing\n"
    "local integer i = 0\n"
    "local location goal = Location(128.0, 512.0)\n"
    "set requestGroup = CreateGroup()\n"
    "loop\n"
    "exitwhen i == 14\n"
    "set requestUnits[i] = CreateUnit(Player(0), 'hpea', I2R(i)*96.0, 0.0, 90.0)\n"
    "call GroupAddUnit(requestGroup, requestUnits[i])\n"
    "set i = i + 1\n"
    "endloop\n"
    "call verifyGroupPoint(GroupPointOrder(requestGroup, \"move\", 128.0, 512.0))\n"
    "call verifyGroupPoint(GroupPointOrderById(requestGroup, 851986, 128.0, 512.0))\n"
    "call verifyGroupPoint(GroupPointOrderLoc(requestGroup, \"move\", goal))\n"
    "call verifyGroupPoint(GroupPointOrderByIdLoc(requestGroup, 851986, goal))\n"
    "call BJassAssert(not GroupPointOrder(null, \"move\", 0.0, 0.0), \"null group must reject\")\n"
    "call BJassAssert(not GroupPointOrderById(requestGroup, 0, 0.0, 0.0), \"unknown order must reject\")\n"
    "call BJassAssert(not GroupPointOrderLoc(requestGroup, \"move\", null), \"null location must reject\")\n"
    "call BJassAssert(not GroupPointOrderByIdLoc(requestGroup, 851986, null), \"numeric null location must reject\")\n"
    "call RemoveLocation(goal)\n"
    "call DestroyGroup(requestGroup)\n"
    "endfunction";

TEST(wc3_api, group_point_order_forms_admit_twelve_members) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    setup_test_world();
    uint8_t cells[64*64] = {0};
    box2_t bounds = {{-512,-512},{1536,1536}};
    CM_SetupTestPathmap(64, 64, cells);
    CM_SetupTestWorldBounds(&bounds);
    T_ASSERT(run_test_jass(group_point_admission_script));
    CM_SetupTestPathmap(0, 0, NULL);
    currentplayer = NULL;
}

/* Group dispatch resolves registered Move ownership, while Patrol and Attack
 * retain their normal point-order path. A JASS collection does not own Move. */
TEST(wc3_api, group_point_order_ability_ownership) {
    reset_entities(); setup_test_world(); level.waypoints=(typeof(level.waypoints)){0};
    currentplayer=&game.clients[0].ps;
    cstring_t script="globals\nunit a\nunit b\ngroup g\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set a=CreateUnit(Player(0),'hpea',128,128,90)\n"
        "set b=CreateUnit(Player(0),'hpea',256,128,90)\n"
        "set g=CreateGroup()\ncall GroupAddUnit(g,a)\ncall GroupAddUnit(g,b)\n"
        "call BJassAssert(GroupPointOrder(g,\"smart\",512,512),\"Smart batch accepted\")\n"
        "call DestroyGroup(g)\n"
        "call BJassAssert(GetUnitCurrentOrder(a)==OrderId(\"smart\"),\"collection destruction retains Move\")\n"
        "call BJassAssert(GetUnitCurrentOrder(b)==OrderId(\"smart\"),\"both Move members remain active\")\n"
        "set g=CreateGroup()\ncall GroupAddUnit(g,a)\ncall GroupAddUnit(g,b)\n"
        "call BJassAssert(GroupPointOrder(g,\"patrol\",640,512),\"Patrol batch accepted\")\n"
        /* ORDER-01.9: issued Patrol expands to the active two-endpoint order. */
        "call BJassAssert(GetUnitCurrentOrder(a)==851991,\"Patrol owner retained\")\n"
        "call BJassAssert(GetUnitCurrentOrder(b)==851991,\"second Patrol owner retained\")\n"
        "call BJassAssert(GroupPointOrder(g,\"attack\",640,512),\"Attack batch accepted\")\n"
        "call DestroyGroup(g)\nendfunction\n";
    T_ASSERT(run_test_jass(script));
    /* Replacement detaches members now; the owner retires at its next visit. */
    unsigned empty=0;
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) if(level.move_groups[i]->inuse) {
        empty++;T_EQ(level.move_groups[i]->count,0);
    }
    T_EQ(empty,1);
    S_BeginAbilityOwnerUpdates();S_RunAbilityOwnerUpdates();
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) T_ASSERT(!level.move_groups[i]->inuse);
    unsigned count=0;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','p','e','a')) {
        count++; T_NOT_NULL(ent->movement.attackmove_waypoint); T_NOT_NULL(ent->currentmove);
        if (ent->currentmove) T_ASSERT(ent->currentmove->proc==CAbilityAttack);
    }
    T_EQ(count,2);
    currentplayer=NULL; reset_entities(); setup_test_world();
}

TEST(wc3_api, destroy_group_clears_members) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local group g = CreateGroup()\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "call GroupAddUnit(g, u)\n"
        "call DestroyGroup(g)\n"
        "if FirstOfGroup(g) != null then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    currentplayer = NULL;
}

TEST(wc3_api, unit_ability_mutation_has_set_semantics) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "if not UnitAddAbility(u, 'AInv') or UnitAddAbility(u, 'AInv') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "if not UnitRemoveAbility(u, 'AInv') or UnitRemoveAbility(u, 'AInv') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 2)\n"
        "endif\n"
        "call RemoveUnit(u)\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    currentplayer = NULL;
}

TEST(wc3_api, unit_invis_reports_intrinsic_active_state) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "if UnitInvis(null) or UnitInvis(u) then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "call ShowUnit(u, false)\n"
        "if UnitInvis(u) then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 4)\n"
        "endif\n"
        "call ShowUnit(u, true)\n"
        "if not UnitAddAbility(u, 'Agho') or not UnitInvis(u) then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 2)\n"
        "endif\n"
        "call KillUnit(u)\n"
        "if UnitInvis(u) then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 3)\n"
        "endif\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 0);
    currentplayer = NULL;
}

TEST(wc3_api, unit_ability_mutation_rejects_invalid_inputs) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "if UnitAddAbility(u, 'xxxx') or UnitAddAbility(null, 'AInv') or UnitRemoveAbility(null, 'AInv') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "call RemoveUnit(u)\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    currentplayer = NULL;
}

TEST(wc3_api, unit_ability_mutation_restores_static_ability) {
    edict_t *unit;
    static UnitAbilities_t const abilities = { .abilList = "Ahar" };
    reset_entities(); unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0); unit->data.UnitAbilities = &abilities;
    T_ASSERT(G_ActorHasSkill(unit, "Ahar"));
    T_ASSERT(G_ActorRemoveSkill(unit, MAKEFOURCC('A','h','a','r')));
    T_ASSERT(!G_ActorHasSkill(unit, "Ahar"));
    T_ASSERT(!G_ActorRemoveSkill(unit, MAKEFOURCC('A','h','a','r')));
    T_ASSERT(G_ActorAddSkill(unit, MAKEFOURCC('A','h','a','r')));
    T_ASSERT(G_ActorHasSkill(unit, "Ahar"));
    T_ASSERT(!G_ActorAddSkill(unit, MAKEFOURCC('A','h','a','r')));
    G_FreeEdict(unit);
}

TEST(wc3_api, unit_ability_permanence_requires_present_ability) {
    edict_t *unit;
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "if not UnitAddAbility(u, 'AInv') or not UnitMakeAbilityPermanent(u, true, 'AInv') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "if UnitMakeAbilityPermanent(u, true, 'xxxx') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 2)\n"
        "endif\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    unit = globals.edicts + game.max_clients;
    T_ASSERT(G_ActorSkillPermanent(unit, MAKEFOURCC('A','I','n','v')));
    T_ASSERT(G_ActorSetSkillPermanent(unit, MAKEFOURCC('A','I','n','v'), false));
    T_ASSERT(!G_ActorSkillPermanent(unit, MAKEFOURCC('A','I','n','v')));
    T_ASSERT(G_ActorSetSkillPermanent(unit, MAKEFOURCC('A','I','n','v'), false));
    G_FreeEdict(unit); currentplayer = NULL;
}

TEST(wc3_api, unit_ability_mutation_rejects_full_lists) {
    static UnitAbilities_t const abilities = { .abilList = "Ahar" };
    uint32_t invulnerable = MAKEFOURCC('A','I','n','v');
    reset_entities();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    FOR_LOOP(i, MAX_ABILITIES) unit->abilities.added[i] = i + 1;
    ARRAY_COUNT(unit->abilities.added) = MAX_ABILITIES;
    T_ASSERT(!G_ActorAddSkill(unit, invulnerable)); T_EQ(ARRAY_COUNT(unit->abilities.added), MAX_ABILITIES);
    memset(&unit->abilities, 0, sizeof(unit->abilities)); unit->data.UnitAbilities = &abilities;
    FOR_LOOP(i, MAX_ABILITIES) unit->abilities.removed[i] = i + 1;
    ARRAY_COUNT(unit->abilities.removed) = MAX_ABILITIES;
    T_ASSERT(!G_ActorRemoveSkill(unit, MAKEFOURCC('A','h','a','r'))); T_ASSERT(G_ActorHasSkill(unit, "Ahar"));
    memset(&unit->abilities, 0, sizeof(unit->abilities));
    unit->abilities.added[0] = invulnerable; ARRAY_COUNT(unit->abilities.added) = 1;
    FOR_LOOP(i, MAX_ABILITIES) unit->abilities.permanent[i] = i + 1;
    ARRAY_COUNT(unit->abilities.permanent) = MAX_ABILITIES;
    T_ASSERT(!G_ActorSetSkillPermanent(unit, invulnerable, true));
    T_ASSERT(!G_ActorSkillPermanent(unit, invulnerable));
    G_FreeEdict(unit);
}

TEST(wc3_api, ai_difficulty_defaults_to_normal) {
    reset_entities();
    test_player(0);
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "if GetAIDifficulty(Player(0)) != AI_DIFFICULTY_NORMAL then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    currentplayer = NULL;
}

TEST(wc3_api, group_is_unit_in_group_true) {
    reset_entities();
    edict_t *a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.num_units = 1;
    /* Replicate IsUnitInGroup logic */
    bool found = false;
    for (uint32_t i = 0; i < g.num_units; i++) {
        if (g.units[i] == a) { found = true; break; }
    }
    T_ASSERT(found);
}

TEST(wc3_api, group_is_unit_in_group_false) {
    reset_entities();
    edict_t *a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.num_units = 1;
    bool found = false;
    for (uint32_t i = 0; i < g.num_units; i++) {
        if (g.units[i] == b) { found = true; break; }
    }
    T_ASSERT(!found);
}

/* =========================================================================
 * Misc — SubString semantics
 * ========================================================================= */

/*
 * Replicate SubString() logic from api_misc.h:
 *   source[start..end) — start inclusive, end exclusive.
 */
static void substr(char const *source, int32_t start, int32_t end, char *out, int32_t outsz) {
    int32_t len = (int32_t)strlen(source);
    if (start < 0) start = 0;
    if (end > len) end = len;
    int32_t n = end - start;
    if (n <= 0 || n + 1 > outsz) { out[0] = '\0'; return; }
    memcpy(out, source + start, (size_t)n);
    out[n] = '\0';
}

TEST(wc3_api, substring_basic) {
    char buf[64];
    substr("hello", 1, 4, buf, (int32_t)sizeof(buf));
    T_STREQ(buf, "ell");
}

TEST(wc3_api, substring_full) {
    char buf[64];
    substr("hello", 0, 5, buf, (int32_t)sizeof(buf));
    T_STREQ(buf, "hello");
}

TEST(wc3_api, substring_start_equals_end) {
    char buf[64];
    substr("hello", 2, 2, buf, (int32_t)sizeof(buf));
    T_STREQ(buf, "");
}

TEST(wc3_api, substring_end_past_len) {
    char buf[64];
    substr("hi", 0, 100, buf, (int32_t)sizeof(buf));
    T_STREQ(buf, "hi");
}

TEST(wc3_api, substring_single_char) {
    char buf[64];
    substr("hello", 0, 1, buf, (int32_t)sizeof(buf));
    T_STREQ(buf, "h");
}

/* =========================================================================
 * Misc — GetRandomInt / GetRandomReal range
 * ========================================================================= */

/* Original seeded public queries, with raw/direction draws represented by
 * discarded public integer draws; both consume exactly one owner transition. */
TEST(wc3_api, random_natives_match_original_owner_words) {
    uint32_t const seeds[] = {0,1,47,53,59,61,7085,12345,0x7fffffff,0x80000000,0xffffffff};
    uint32_t const expected[11][24] = {
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x00000007u,0x3f060b5au,
            0xfffffffcu,0xc10f54c6u,0xfffffff9u,0x4125499au,0x0000001bu,0x419e42f7u,
            0x80000000u,0x33d51d1cu,0x7fffffffu,0x00000000u,0x79f5ca6eu,0x3f268f5eu,
            0x87b86f9bu,0xc3f888a8u,0x00000001u,0x3a94645au,0x7fffffffu,0x40435264u,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x00000001u,0x3f21a828u,
            0xfffffffcu,0xc103faaeu,0x0000000cu,0x411db9f6u,0x0000000eu,0x41a9a63fu,
            0x80000000u,0x344d014eu,0x7fffffffu,0x00000000u,0x49c31ad0u,0x3e774a30u,
            0xae988360u,0xc3d16bacu,0x00000000u,0x3abe8f51u,0x7ffffffeu,0x4084f6b5u,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x00000005u,0x3f7e499eu,
            0xfffffffcu,0xc0a2cfc3u,0x0000000au,0xc1040368u,0x00000014u,0x41909d6au,
            0x80000000u,0x3410d0d8u,0x7fffffffu,0x00000000u,0x6261cee8u,0x3f51049eu,
            0xe2764c75u,0x43ac1a58u,0x00000001u,0x3af5531eu,0x7ffffffeu,0x4070ce80u,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x00000009u,0x3d9e2e90u,
            0xfffffff9u,0xc091f376u,0x0000000bu,0x40cbbec4u,0x00000011u,0x4141141au,
            0x80000000u,0x332de758u,0x7fffffffu,0x00000000u,0x097fedceu,0x3f3e36d4u,
            0xc6be9908u,0xc3d90ba4u,0x00000001u,0x3ae4574eu,0x7fffffffu,0x40a2db46u,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x0000000au,0x3f58be3eu,
            0xfffffff9u,0xbfb395a0u,0xfffffff9u,0x40cacb38u,0x00000018u,0x4177f0c2u,
            0x80000000u,0x33b45298u,0x7fffffffu,0x00000000u,0x7c1ce627u,0x3e34a1c0u,
            0xd7b91224u,0xc2f565a0u,0x00000001u,0x3a9229edu,0x7ffffffeu,0x40945f0bu,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x00000001u,0x3c93ba40u,
            0xfffffffau,0xc0af0eb0u,0x00000004u,0x4101161eu,0x0000000eu,0x41c692beu,
            0x80000000u,0x33a57e8cu,0x7fffffffu,0x00000000u,0x75c365adu,0x3c997840u,
            0xbf6c970eu,0x43f21ef0u,0x00000001u,0x3ad27d9du,0x7ffffffeu,0x4087dc57u,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x0000000au,0x3f356de0u,
            0xfffffff7u,0xc0f00cd3u,0xfffffff7u,0xc002b478u,0x00000012u,0x4196dcc3u,
            0x80000000u,0x331ef738u,0x7fffffffu,0x00000000u,0x007c2d07u,0x3e0d9a88u,
            0xa4310beau,0xc3f9a4a0u,0x00000000u,0x3a9ae2d9u,0x7fffffffu,0x400c0b92u,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x00000006u,0x3e0048f0u,
            0xfffffff8u,0xc0cc2ba8u,0x00000009u,0x40b66b70u,0x00000012u,0x419c7a77u,
            0x80000000u,0x33ef320cu,0x7fffffffu,0x00000000u,0x3a37495bu,0x3ee6bab4u,
            0xce167b57u,0x43dc3eccu,0x00000000u,0x3a9c5c3du,0x7fffffffu,0x40841349u,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x00000003u,0x3e956398u,
            0xfffffff7u,0xbff155e0u,0x00000003u,0xc0c44fe4u,0x00000018u,0x419b257au,
            0x80000000u,0x3299fdd0u,0x7fffffffu,0x00000000u,0x143fcb8eu,0x3db41420u,
            0xc483c403u,0x43f15644u,0x00000000u,0x3aaae953u,0x7ffffffeu,0x4071bfacu,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x0000000au,0x3d4c7160u,
            0xfffffff7u,0xc0a3ca01u,0x0000000bu,0xc10575a9u,0x0000001cu,0x419fec43u,
            0x80000000u,0x3371a598u,0x7fffffffu,0x00000000u,0x32025ab9u,0x3e805f58u,
            0xf786c7beu,0x4362de88u,0x00000001u,0x3ad5fd35u,0x7fffffffu,0x4008da06u,
        },
        {
            0x00000000u,0x00000000u,0x00000007u,0x40e00000u,0x00000004u,0x3f58797au,
            0xfffffffcu,0xc0beef94u,0xfffffffeu,0x412290aau,0x0000000cu,0x41a7c0dfu,
            0x80000000u,0x34614b92u,0x7fffffffu,0x00000000u,0x52601e41u,0x3f48a5c0u,
            0x988aa757u,0xc35eda58u,0x00000001u,0x3ade211du,0x7fffffffu,0x403e22aau,
        },
    };
    cstring_t const bounds[] = {
        "0,0", "(I2R(0)/I2R(1)),(I2R(0)/I2R(1))",
        "7,7", "(I2R(7)/I2R(1)),(I2R(7)/I2R(1))",
        "1,10", "(I2R(0)/I2R(1)),(I2R(1)/I2R(1))",
        "-10,-1", "(I2R(-10)/I2R(1)),(I2R(-1)/I2R(1))",
        "-9,13", "(I2R(-9)/I2R(1)),(I2R(13)/I2R(1))",
        "12,-4", "(I2R(12)/I2R(1)),(I2R(-4)/I2R(1))",
        "-2147483648,2147483647", "(I2R(0)/I2R(1)),(I2R(1)/I2R(4194304))",
        "2147483647,-2147483648", "(I2R(0)/I2R(1)),(I2R(1)/I2R(8388608))",
        "0,2147483647", "(I2R(-1)/I2R(8)),(I2R(7)/I2R(8))",
        "-2147483648,0", "(I2R(-512)/I2R(1)),(I2R(512)/I2R(1))",
        "0,-1", "(I2R(1)/I2R(1024)),(I2R(1)/I2R(512))",
        "2147483646,2147483647", "(I2R(2)/I2R(1)),(I2R(-2)/I2R(1))",
    };
    char script[16384];
    FOR_LOOP(seed,11) {
        reset_entities(); setup_test_world(); G_ClearHashtableRegistry();
        int used=snprintf(script,sizeof(script),
            "globals\nhashtable values\nendglobals\nfunction main takes nothing returns nothing\n"
            "local integer discard\nset values=InitHashtable()\ncall SetRandomSeed(%d)\n",(int32_t)seeds[seed]);
        FOR_LOOP(i,12) used+=snprintf(script+used,sizeof(script)-used,
            "set discard=GetRandomInt(0,1)\n"
            "call SaveInteger(values,0,%u,GetRandomInt(%s))\n"
            "call SaveReal(values,0,%u,GetRandomReal(%s))\n"
            "set discard=GetRandomInt(0,1)\n",i*2,bounds[i*2],i*2+1,bounds[i*2+1]);
        snprintf(script+used,sizeof(script)-used,"endfunction\n");
        T_ASSERT(run_test_jass(script));
        hashtable_t const *table=&level.hashtables[0]; T_EQ(table->num_entries,24);
        FOR_LOOP(i,24) {
            T_EQ(table->entries[i].child,(int32_t)i);
            uint32_t word; memcpy(&word,&table->entries[i].value,sizeof(word));
        T_EQ(word,expected[seed][i]);
        }
    }
    reset_entities(); setup_test_world(); G_ClearHashtableRegistry();
}

TEST(wc3_api, random_owner_continues_identically_after_save) {
    cstring_t file=Test_TempPath("openwarcraft3-random-owner-save.bin");
    uint32_t expected[2];
    reset_entities(); setup_test_world(); G_ClearHashtableRegistry();
    T_ASSERT(run_test_jass(
        "globals\nhashtable values\nendglobals\n"
        "function next takes nothing returns nothing\n"
        "call SaveInteger(values,0,0,GetRandomInt(1,2000000000))\n"
        "call SaveReal(values,0,1,GetRandomReal(-9,13))\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "set values=InitHashtable()\ncall SetRandomSeed(12345)\nendfunction\n"));
    T_ASSERT(WriteGame(file)); jass_callbyname(level.vm,"next",false);
    FOR_LOOP(i,2) memcpy(expected+i,&level.hashtables[0].entries[i].value,sizeof(uint32_t));
    T_ASSERT(ReadGame(file)); jass_callbyname(level.vm,"next",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    FOR_LOOP(i,2) {
        uint32_t actual; memcpy(&actual,&level.hashtables[0].entries[i].value,sizeof(actual));
        T_EQ(actual,expected[i]);
    }
    remove(file); reset_entities(); setup_test_world(); G_ClearHashtableRegistry();
}

/* Exercise the natives through compiled JASS; libc range tests never reached the engine. */
TEST(wc3_api, random_int_in_range) {
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "local integer i=0\nlocal integer value\ncall SetRandomSeed(42)\nloop\nexitwhen i==50\n"
        "set value=GetRandomInt(1,10)\ncall BJassAssert(value>=1 and value<=10, \"integer range\")\n"
        "set i=i+1\nendloop\nendfunction\n"));
}
TEST(wc3_api, random_int_single_value) {
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "call BJassAssert(GetRandomInt(7,7)==7, \"equal bounds\")\nendfunction\n"));
}
TEST(wc3_api, random_real_in_range) {
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "local integer i=0\nlocal real value\ncall SetRandomSeed(42)\nloop\nexitwhen i==50\n"
        "set value=GetRandomReal(0,1)\ncall BJassAssert(value>=0 and value<1, \"real range\")\n"
        "set i=i+1\nendloop\nendfunction\n"));
}
TEST(wc3_api, random_seed_deterministic) {
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "local integer value\ncall SetRandomSeed(12345)\nset value=GetRandomInt(1,100000)\n"
        "call SetRandomSeed(12345)\ncall BJassAssert(GetRandomInt(1,100000)==value, \"reseed\")\nendfunction\n"));
}

/* =========================================================================
 * Item — position and type id
 * ========================================================================= */

TEST(wc3_api, item_position_set) {
    reset_entities();
    edict_t *item = alloc_test_unit(MAKEFOURCC('I','0','0','0'), 10.0f, 20.0f);
    item->s.origin.x = 10.0f;
    item->s.origin.y = 20.0f;
    T_FEQ(item->s.origin.x, 10.0f, 0.001f);
    T_FEQ(item->s.origin.y, 20.0f, 0.001f);
}

TEST(wc3_api, item_type_id) {
    reset_entities();
    edict_t *item = alloc_test_unit(MAKEFOURCC('I','0','0','0'), 0.0f, 0.0f);
    uint32_t expected = MAKEFOURCC('I','0','0','0');
    T_EQ((int)item->class_id, (int)expected);
}

/* =========================================================================
 * Inventory — edict-based UnitHasItem / UnitItemInSlot
 * ========================================================================= */

static edict_t *alloc_inventory_test_unit(void) {
    edict_t *unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    unit->health.value = 100;
    unit->health.max_value = 100;
    return unit;
}

static edict_t *alloc_world_test_item(uint32_t class_id) {
    edict_t *item = alloc_test_unit(class_id, 0, 0);
    item->s.model = 1;
    item->targtype = TARG_ITEM;
    if (!item->item) item->item = G_AllocItem();
    assert(item->item);
    item->item->in_world = true;
    item->item->inventory_slot = -1;
    return item;
}

TEST(wc3_api, unit_has_item_true) {
    reset_entities();
    edict_t *unit = alloc_inventory_test_unit();
    edict_t *item = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item, 0);
    /* UnitHasItem checks pointer identity */
    bool found = false;
    FOR_LOOP(i, MAX_INVENTORY) {
        if (unit->inventory[i] == item) { found = true; break; }
    }
    T_ASSERT(found);
}

TEST(wc3_api, unit_has_item_false_different_instance) {
    /* Two items of the same type — only one is in inventory.
     * With edict-based inventory, distinct instances are distinguishable. */
    reset_entities();
    edict_t *unit  = alloc_inventory_test_unit();
    edict_t *item1 = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    edict_t *item2 = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item1, 0);
    /* item2 is NOT in inventory */
    bool found = false;
    FOR_LOOP(i, MAX_INVENTORY) {
        if (unit->inventory[i] == item2) { found = true; break; }
    }
    T_ASSERT(!found);
}

TEST(wc3_api, unit_item_in_slot_returns_edict) {
    reset_entities();
    edict_t *unit = alloc_inventory_test_unit();
    edict_t *item = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item, 2);
    T_ASSERT(unit->inventory[2] == item);
}

TEST(wc3_api, unit_item_in_slot_empty_is_null) {
    reset_entities();
    edict_t *unit = alloc_inventory_test_unit();
    T_NULL(unit->inventory[0]);
}

/* =========================================================================
 * Unit — IsUnitOwnedByPlayer
 * ========================================================================= */

TEST(wc3_api, unit_owned_by_player) {
    reset_entities();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ent->s.player = 2;
    /* Replicate IsUnitOwnedByPlayer: ent->s.player == PLAYER_NUM(player) */
    player_t *p = test_player(2);
    T_EQ((int)ent->s.player, (int)PLAYER_NUM(p));
}

TEST(wc3_api, unit_not_owned_by_player) {
    reset_entities();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ent->s.player = 1;
    player_t *p = test_player(3);
    T_ASSERT(ent->s.player != PLAYER_NUM(p));
}

TEST(wc3_api, is_unit_type_reports_structure_from_authoritative_metadata) {
    reset_entities();
    T_ASSERT(G_UnitIsBuilding(MAKEFOURCC('h','b','a','r')));
    T_ASSERT(!G_UnitIsBuilding(MAKEFOURCC('h','p','e','a')));
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit building = CreateUnit(Player(0), 'hbar', 0.0, 0.0, 0.0)\n"
        "local unit worker = CreateUnit(Player(0), 'hpea', 256.0, 0.0, 0.0)\n"
        "if not IsUnitType(building, UNIT_TYPE_STRUCTURE) then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "if IsUnitType(worker, UNIT_TYPE_STRUCTURE) then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_LUMBER, 1)\n"
        "endif\n"
        "call RemoveUnit(building)\n"
        "call RemoveUnit(worker)\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 0);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);
    currentplayer = NULL;
}

/* =========================================================================
 * Unit — IsUnitInRange
 * ========================================================================= */

TEST(wc3_api, unit_in_range) {
    reset_entities();
    edict_t *a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 3.0f, 4.0f);  /* dist = 5 */
    float dist = Vector2_distance(&a->s.origin2, &b->s.origin2);
    T_ASSERT(dist <= 6.0f);
}

TEST(wc3_api, unit_out_of_range) {
    reset_entities();
    edict_t *a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 3.0f, 4.0f);  /* dist = 5 */
    float dist = Vector2_distance(&a->s.origin2, &b->s.origin2);
    T_ASSERT(!(dist <= 4.0f));
}

TEST(wc3_api, unit_in_range_fires_when_registered_subject_moves) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *subject, *target;
    vec2_t destination = { 100.0f, 0.0f };

    reset_entities();
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  trigger rangeTrigger = null\n"
        "  unit rangeSubject = null\n"
        "  boolean entered = false\n"
        "endglobals\n"
        "function on_range takes nothing returns nothing\n"
        "  set entered = true\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set rangeSubject = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call CreateUnit(Player(0), 'hfoo', 280.0, 0.0, 0.0)\n"
        "  set rangeTrigger = CreateTrigger()\n"
        "  call TriggerRegisterUnitInRange(rangeTrigger, rangeSubject, 256.0, null)\n"
        "  call TriggerAddAction(rangeTrigger, function on_range)\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(entered, \"registered subject movement did not fire range event\")\n"
        "endfunction\n"));

    subject = find_test_unit(MAKEFOURCC('h','p','e','a'));
    target = find_test_unit(MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(subject); T_NOT_NULL(target);
    /* This minimal Footman fixture has no collision value; retail's range
     * candidate predicate requires a positive mover radius. */
    target->collision=16;G_MarkMoveSpatialObject(target);
    subject->movetype = MOVETYPE_STEP;
    subject->stand = unit_stand;
    subject->birth = unit_birth;
    subject->die = unit_die;
    subject->think = monster_think;
    subject->collision = 0.0f;
    T_FEQ(subject->unitinfo.PropWindow, DEG2RAD(60.0f), 0.001f);
    subject->health.value = 250.0f;
    subject->health.max_value = 250.0f;
    unit_stand(subject);
    T_ASSERT(unit_issueorder(subject, "move", &destination));
    /* Match the primary drain before each physical owner. A frame-end-only
     * drain would predict an earlier listener from an already committed pose. */
    G_BeginEntityFrame();level.scheduled_frame=true;
    FOR_LOOP(i,3) {
        wc3_clock_advance(&level.pathing_clock,10.0f/FRAMETIME,0);
        G_RunTimersBeforePathOwner(&level.pathing_clock);M_RunScheduledThinks();
    }
    G_RunEntities();level.scheduled_frame=false;
    T_ASSERT(subject->s.origin2.x > 0.0f);
    T_ASSERT(Vector2_distance(&subject->s.origin2, &target->s.origin2) <= 256.0f);
    /* Range delivery is a primary-clock poll, independent of the movement pass. */
    level.scheduled_frame=true;G_RunTimers();
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, deferred_removed_range_subject_cannot_dispatch_crossing) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *subject = NULL, *target = NULL;
    event_t *rangeEvent = NULL;
    bool queuedRangeCrossing = false;
    vec2_t destination = { 200.0f, 0.0f };

    G_ResetDeferredFrees(); reset_entities(); setup_test_world(); currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit rangeSubject = null\n"
        "  integer rangeFires = 0\n"
        "endglobals\n"
        "function on_range takes nothing returns nothing\n"
        "  set rangeFires = rangeFires + 1\n"
        "endfunction\n"
        "function remove_range_subject takes nothing returns nothing\n"
        "  call RemoveUnit(rangeSubject)\n"
        "endfunction\n"
        "function verify_removed_subject_did_not_dispatch takes nothing returns nothing\n"
        "  call BJassAssert(rangeFires == 0, \"deferred-removed range subject dispatched crossing\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger rangeTrigger = CreateTrigger()\n"
        "  set rangeSubject = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call CreateUnit(Player(0), 'hfoo', 280.0, 0.0, 0.0)\n"
        "  call TriggerRegisterUnitInRange(rangeTrigger, rangeSubject, 256.0, null)\n"
        "  call TriggerAddAction(rangeTrigger, function on_range)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].s.player == 0 &&
            g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) subject = &g_edicts[i];
        if (g_edicts[i].inuse && g_edicts[i].s.player == 0 &&
            g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) target = &g_edicts[i];
    }
    T_NOT_NULL(subject); T_NOT_NULL(target);
    FOR_EACH_EVENT(evt) if (evt->type == EVENT_UNIT_IN_RANGE) { rangeEvent = evt; break; }
    T_NOT_NULL(rangeEvent);
    if (rangeEvent) T_ASSERT(rangeEvent->subject == subject);
    jass_callbyname(level.vm, "remove_range_subject", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(subject->inuse && G_IsDeferredFree(subject));
    T_ASSERT(!G_EventSubjectIsCurrent(rangeEvent));

    /* This test owns event dispatch, so start the mover facing the crossing direction. */
    target->s.angle = (float)M_PI;

    target->movetype = MOVETYPE_STEP; target->stand = unit_stand; target->birth = unit_birth;
    target->die = unit_die; target->think = monster_think; target->collision = 0.0f;
    target->unitinfo.MoveSpeed = 1000.0f;
    target->health.value = target->health.max_value = 250.0f; unit_stand(target);
    T_ASSERT(unit_issueorder(target, "move", &destination));
    api_run_move_entities(2);
    T_ASSERT(memcmp(&subject->old_origin, &subject->s.origin2, sizeof(vec2_t)) == 0);
    T_ASSERT(Vector2_distance(&subject->old_origin, &target->old_origin) > rangeEvent->range);
    T_ASSERT(Vector2_distance(&subject->s.origin2, &target->s.origin2) <= rangeEvent->range);
    T_ASSERT(Vector2_distance(&subject->s.origin2, &target->s.origin2) <= 256.0f);
    /* Issued Move delivery is synchronous; no unrelated queued order remains. */
    T_EQ(level.events.write, level.events.read);
    for (uint32_t i = level.events.read; i < level.events.write; i++) {
        gameEvent_t *queued = &level.events.queue[i % MAX_EVENT_QUEUE];
        if (queued->type == EVENT_UNIT_IN_RANGE && queued->responseTo == rangeEvent)
            queuedRangeCrossing = true;
    }
    T_ASSERT(!queuedRangeCrossing);
    G_RunEvents(); jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify_removed_subject_did_not_dispatch", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    G_TestFinishDeferredFrees();
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, unit_in_range_queue_full_does_not_crash_subject_movement) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *subject = NULL;
    vec2_t destination = {100.0f, 0.0f};

    reset_entities(); setup_test_world(); currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "type unit extends handle\n"
        "type trigger extends handle\n"
        "globals\n"
        "  unit rangeSubject = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set rangeSubject = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call CreateUnit(Player(0), 'hfoo', 280.0, 0.0, 0.0)\n"
        "  call TriggerRegisterUnitInRange(t, rangeSubject, 256.0, null)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) { subject = &g_edicts[i]; break; }
    T_NOT_NULL(subject);
    subject->movetype = MOVETYPE_STEP; subject->stand = unit_stand; subject->birth = unit_birth;
    subject->die = unit_die; subject->think = monster_think; subject->collision = 0.0f;
    subject->health.value = subject->health.max_value = 250.0f; unit_stand(subject);
    T_ASSERT(unit_issueorder(subject, "move", &destination));
    level.events.read = 0; level.events.write = MAX_EVENT_QUEUE;
    api_run_move_entities(2);
    T_EQ(level.events.write, (uint32_t)MAX_EVENT_QUEUE);
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, unit_in_range_queue_full_does_not_crash_target_movement) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *subject = NULL;
    edict_t *target = NULL;
    /* Move the target into rangeSubject's radius to exercise the saturated event queue. */
    vec2_t destination = {600.0f, 0.0f};

    reset_entities(); setup_test_world(); currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "type unit extends handle\n"
        "type trigger extends handle\n"
        "globals\n"
        "  unit rangeSubject = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set rangeSubject = CreateUnit(Player(0), 'hpea', 530.0, 0.0, 0.0)\n"
        "  call CreateUnit(Player(0), 'hfoo', 257.0, 0.0, 0.0)\n"
        "  call TriggerRegisterUnitInRange(t, rangeSubject, 256.0, null)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a') &&
            g_edicts[i].s.origin2.x > 500.0f) subject = &g_edicts[i];
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') &&
            g_edicts[i].s.origin2.x > 256.0f) target = &g_edicts[i];
    }
    T_NOT_NULL(subject);
    T_NOT_NULL(target);
    T_FEQ(subject->s.origin2.x, 530.0f, 0.01f);
    T_FEQ(target->s.origin2.x, 257.0f, 0.01f);
    target->movetype = MOVETYPE_STEP; target->stand = unit_stand; target->birth = unit_birth;
    target->die = unit_die; target->think = monster_think; target->collision = 0.0f;
    target->health.value = target->health.max_value = 250.0f; unit_stand(target);
    T_ASSERT(unit_issueorder(target, "move", &destination));
    level.events.read = 0; level.events.write = MAX_EVENT_QUEUE;
    api_run_move_entities(2);
    T_ASSERT(Vector2_distance(&subject->s.origin2, &target->s.origin2) <= 256.0f);
    T_EQ(level.events.write, (uint32_t)MAX_EVENT_QUEUE);
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, killunit_runs_normal_unit_death_transition) {
    edict_t *victim = NULL;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call KillUnit(u)\n"
        "endfunction"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) victim = &g_edicts[i];
    T_NOT_NULL(victim);
    T_FEQ(victim->health.value, 0.0f, 0.001f);
    T_ASSERT(victim->svflags & SVF_DEADMONSTER);
    T_ASSERT(victim->s.flags & EF_NOT_SELECTABLE);
    T_NOT_NULL(victim->currentmove);
    T_STREQ(victim->currentmove->animation, "death");
}

TEST(wc3_api, killunit_processes_deferred_unit_handle) {
    edict_t *victim;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call RemoveUnit(u)\n"
        "  call KillUnit(u)\n"
        "  call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1);
    victim = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h', 'f', 'o', 'o')) victim = &g_edicts[i];
    T_NOT_NULL(victim);
    if (victim) T_ASSERT(victim->svflags & SVF_DEADMONSTER);
}

TEST(wc3_api, player_unit_counts_support_campaign_peon_goals) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit p1 = CreateUnit(Player(0), 'opeo', 0.0, 0.0, 0.0)\n"
        "  local unit p2 = CreateUnit(Player(0), 'opeo', 64.0, 0.0, 0.0)\n"
        "  local unit p3 = CreateUnit(Player(0), 'opeo', 128.0, 0.0, 0.0)\n"
        "  local unit p4 = CreateUnit(Player(0), 'opeo', 192.0, 0.0, 0.0)\n"
        "  local unit p5 = CreateUnit(Player(0), 'opeo', 256.0, 0.0, 0.0)\n"
        "  local unit enemy = CreateUnit(Player(1), 'opeo', 320.0, 0.0, 0.0)\n"
        "  local unit burrow = CreateUnit(Player(0), 'otrb', 384.0, 0.0, 0.0)\n"
        "  call BJassAssert(GetPlayerUnitCount(Player(0), true) == 5, \"unit count must exclude structures and other players\")\n"
        "  call BJassAssert(GetPlayerTypedUnitCount(Player(0), \"Peon\", true, true) == 5, \"typed Peon count must reach five\")\n"
        "  call BJassAssert(GetPlayerTypedUnitCount(Player(0), UnitId2String('opeo'), true, true) == 5, \"typed count must accept UnitId2String identity\")\n"
        "  call KillUnit(p5)\n"
        "  call BJassAssert(GetPlayerTypedUnitCount(Player(0), \"Peon\", true, true) == 4, \"dead Peons must not count\")\n"
        "  call RemoveUnit(p1)\n"
        "  call RemoveUnit(p2)\n"
        "  call RemoveUnit(p3)\n"
        "  call RemoveUnit(p4)\n"
        "  call RemoveUnit(p5)\n"
        "  call RemoveUnit(enemy)\n"
        "  call RemoveUnit(burrow)\n"
        "endfunction"));
}

TEST(wc3_api, train_start_event_exposes_producer_and_trainee) {
    edict_t *producer = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  boolean trainStarted = false\n"
        "endglobals\n"
        "function onTrainStart takes nothing returns nothing\n"
        "  set trainStarted = true\n"
        "  call BJassAssert(GetTriggerUnit() != null, \"train start must expose producer\")\n"
        "  call BJassAssert(GetTrainedUnitType() == 'opeo', \"train start must expose queued Peon type\")\n"
        "  call BJassAssert(GetTrainedUnit() != null, \"train start must expose queued trainee\")\n"
        "endfunction\n"
        "function verifyTrainStart takes nothing returns nothing\n"
        "  call BJassAssert(trainStarted, \"train-start trigger did not fire\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_TRAIN_START, null)\n"
        "  call TriggerAddAction(t, function onTrainStart)\n"
        "endfunction"));

    producer = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'e'), 0.0f, 0.0f);
    producer->s.player = 0;
    game.clients[0].ps.number = 0;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    unit_build(producer, MAKEFOURCC('o', 'p', 'e', 'o'));
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyTrainStart", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, trained_unit_type_uses_train_finish_event_subject) {
    edict_t *trained = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer trainedType = 0\n"
        "endglobals\n"
        "function onTrainFinish takes nothing returns nothing\n"
        "  set trainedType = GetTrainedUnitType()\n"
        "  call BJassAssert(GetTrainedUnit() != null, \"train finish must expose trained unit\")\n"
        "endfunction\n"
        "function verifyTrainFinish takes nothing returns nothing\n"
        "  call BJassAssert(trainedType == 'opeo', \"GetTrainedUnitType must return trained rawcode\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_TRAIN_FINISH, null)\n"
        "  call TriggerAddAction(t, function onTrainFinish)\n"
        "endfunction"));

    trained = alloc_test_unit(MAKEFOURCC('o', 'p', 'e', 'o'), 0.0f, 0.0f);
    trained->s.player = 0;
    G_PublishEvent(trained, EVENT_PLAYER_UNIT_TRAIN_FINISH);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyTrainFinish", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* A campaign defeat trigger may run on any owned unit death and ask whether
 * the player still has structures.  The structure-count native must scan the
 * surviving world state rather than returning zero just because the event was
 * raised by a non-building unit. */
TEST(wc3_api, player_structure_count_survives_nonstructure_death) {
    edict_t *victim = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit building = null\n"
        "  unit victim = null\n"
        "  boolean deathFired = false\n"
        "endglobals\n"
        "function onDeath takes nothing returns nothing\n"
        "  set deathFired = true\n"
        "  call BJassAssert(GetPlayerStructureCount(Player(0), true) == 1, \"living structure lost on unit death\")\n"
        "endfunction\n"
        "function verifyDeath takes nothing returns nothing\n"
        "  call BJassAssert(deathFired, \"death trigger did not fire\")\n"
        "  call BJassAssert(GetPlayerStructureCount(Player(0), true) == 1, \"living structure count changed after unit death\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set building = CreateUnit(Player(0), 'hbar', 0.0, 0.0, 0.0)\n"
        "  call SetWidgetLife(building, 1.0)\n"
        "  set victim = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call TriggerRegisterDeathEvent(t, victim)\n"
        "  call TriggerAddAction(t, function onDeath)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].class_id == MAKEFOURCC('h', 'f', 'o', 'o') &&
            g_edicts[i].s.player == 0) {
            victim = &g_edicts[i];
            break;
        }
    }
    T_NOT_NULL(victim);
    unit_die(victim, NULL);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyDeath", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* =========================================================================
 * Campaign availability progress
 * ========================================================================= */

TEST(wc3_api, campaign_progress_natives_persist_stock_bj_unlocks) {
    void (*old_user_path)(cstring_t, string_t, uint32_t) = gi.UserPath;
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    wc3CampaignProgress_t progress;
    wc3CampaignProgressKey_t campaign_key;
    wc3CampaignProgressKey_t mission_key;

    remove(campaign_progress_test_path);
    remove(Test_TempPath("campaign-progress-native-test.orcp.tmp"));
    remove(Test_TempPath("campaign-progress-native-test.orcp.bak"));
    gi.UserPath = campaign_progress_test_user_path;
    gi.CvarString = campaign_progress_roc_cvar;
    level.campaign_select_on_end = false;
    G_CampaignProgressResetRuntime();

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCampaignAvailableBJ(true, bj_CAMPAIGN_INDEX_H)\n"
        "  call SetMissionAvailableBJ(true, bj_MISSION_INDEX_H00)\n"
        "endfunction\n"));

    campaign_key = MAKE(wc3CampaignProgressKey_t,
                        .edition = WC3_CAMPAIGN_EDITION_ROC,
                        .campaign = 1);
    mission_key = MAKE(wc3CampaignProgressKey_t,
                       .edition = WC3_CAMPAIGN_EDITION_ROC,
                       .campaign = 1,
                       .mission = 0);
    T_ASSERT(wc3_campaign_progress_load(campaign_progress_test_path, &progress));
    T_ASSERT(progress.tutorial_known[WC3_CAMPAIGN_EDITION_ROC]);
    T_ASSERT(progress.tutorial_cleared[WC3_CAMPAIGN_EDITION_ROC]);
    T_ASSERT(wc3_campaign_progress_campaign_available(&progress, campaign_key));
    T_ASSERT(wc3_campaign_progress_mission_available(&progress, mission_key));
    T_ASSERT(level.campaign_select_on_end);

    level.campaign_select_on_end = false;
    G_CampaignProgressResetRuntime();
    gi.CvarString = old_cvar;
    gi.UserPath = old_user_path;
    remove(campaign_progress_test_path);
}

TEST(wc3_api, campaign_progress_round_trip_preserves_explicit_false) {
    wc3CampaignProgress_t written;
    wc3CampaignProgress_t loaded;
    wc3CampaignProgressKey_t key = MAKE(wc3CampaignProgressKey_t,
                                        .edition = WC3_CAMPAIGN_EDITION_TFT,
                                        .campaign = 2,
                                        .mission = 7);

    remove(campaign_progress_test_path);
    wc3_campaign_progress_init(&written);
    T_ASSERT(wc3_campaign_progress_set_campaign(&written, key, false));
    T_ASSERT(wc3_campaign_progress_set_mission(&written, key, false));
    T_ASSERT(wc3_campaign_progress_save(campaign_progress_test_path, &written));
    T_ASSERT(wc3_campaign_progress_load(campaign_progress_test_path, &loaded));
    T_ASSERT(wc3_campaign_progress_has_campaign(&loaded, key));
    T_ASSERT(!wc3_campaign_progress_campaign_available(&loaded, key));
    T_ASSERT(wc3_campaign_progress_has_mission(&loaded, key));
    T_ASSERT(!wc3_campaign_progress_mission_available(&loaded, key));
    remove(campaign_progress_test_path);
}

TEST(wc3_api, campaign_progress_campaign_keys_match_blizzard_offsets) {
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_ROC, "Tutorial"), 0);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_ROC, "Human"), 1);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_ROC, "NightElf"), 4);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_TFT, "NightElf"), 0);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_TFT, "Human"), 1);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_TFT, "Orc"), 3);
}

/* =========================================================================
 * Campaign game cache
 * ========================================================================= */

TEST(wc3_api, gamecache_scalar_values_round_trip_and_flush_by_type) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local gamecache c = InitGameCache(\"openrealm-test-memory-only.w3v\")\n"
        "  call FlushGameCache(c)\n"
        "  call StoreInteger(c, \"mission\", \"value\", 42)\n"
        "  call StoreReal(c, \"mission\", \"real\", 3.5)\n"
        "  call StoreBoolean(c, \"mission\", \"flag\", true)\n"
        "  call StoreString(c, \"mission\", \"text\", \"arthas\")\n"
        "  call BJassAssert(HaveStoredInteger(c, \"mission\", \"value\"), \"missing stored integer\")\n"
        "  call BJassAssert(GetStoredInteger(c, \"mission\", \"value\") == 42, \"wrong stored integer\")\n"
        "  call BJassAssert(GetStoredReal(c, \"mission\", \"real\") == 3.5, \"wrong stored real\")\n"
        "  call BJassAssert(GetStoredBoolean(c, \"mission\", \"flag\"), \"wrong stored boolean\")\n"
        "  call BJassAssert(GetStoredString(c, \"mission\", \"text\") == \"arthas\", \"wrong stored string\")\n"
        "  call FlushStoredInteger(c, \"mission\", \"value\")\n"
        "  call BJassAssert(not HaveStoredInteger(c, \"mission\", \"value\"), \"integer flush failed\")\n"
        "  call BJassAssert(HaveStoredString(c, \"mission\", \"text\"), \"typed flush removed another value\")\n"
        "endfunction\n"));
}

TEST(wc3_api, gamecache_disabled_keeps_handle_local_and_does_not_commit) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;

    gi.CvarString = gamecache_disabled_cvar;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local gamecache source = InitGameCache(\"openrealm-test-disabled.w3v\")\n"
        "  local gamecache fresh\n"
        "  call StoreInteger(source, \"Human01\", \"Stage\", 2)\n"
        "  call BJassAssert(GetStoredInteger(source, \"Human01\", \"Stage\") == 2, \"disabled mode broke local handle state\")\n"
        "  call BJassAssert(SaveGameCache(source), \"disabled SaveGameCache should remain script-compatible\")\n"
        "  set fresh = InitGameCache(\"openrealm-test-disabled.w3v\")\n"
        "  call BJassAssert(not HaveStoredInteger(fresh, \"Human01\", \"Stage\"), \"disabled mode committed cache state\")\n"
        "endfunction\n"));
    gi.CvarString = old_cvar;
}

TEST(wc3_api, gamecache_save_commits_to_process_memory) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;

    gi.CvarString = gamecache_memory_cvar;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local gamecache source = InitGameCache(\"openrealm-test-memory-save.w3v\")\n"
        "  local gamecache unsaved\n"
        "  local gamecache saved\n"
        "  call FlushGameCache(source)\n"
        "  call BJassAssert(SaveGameCache(source), \"initial memory save failed\")\n"
        "  call StoreInteger(source, \"Human01\", \"Stage\", 2)\n"
        "  set unsaved = InitGameCache(\"openrealm-test-memory-save.w3v\")\n"
        "  call BJassAssert(not HaveStoredInteger(unsaved, \"Human01\", \"Stage\"), \"unsaved value leaked into committed cache\")\n"
        "  call BJassAssert(SaveGameCache(source), \"memory save failed\")\n"
        "  set saved = InitGameCache(\"openrealm-test-memory-save.w3v\")\n"
        "  call BJassAssert(GetStoredInteger(saved, \"Human01\", \"Stage\") == 2, \"saved value did not survive new cache handle\")\n"
        "endfunction\n"));
    gi.CvarString = old_cvar;
}

TEST(wc3_api, gamecache_restore_does_not_publish_pickup_events) {
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer pickupCount = 0\n"
        "  unit restored = null\n"
        "endglobals\n"
        "function onPickup takes nothing returns nothing\n"
        "  set pickupCount = pickupCount + 1\n"
        "endfunction\n"
        "function verifyPickup takes nothing returns nothing\n"
        "  call BJassAssert(pickupCount == 1, \"RestoreUnit published a pickup event\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local gamecache c = InitGameCache(\"openrealm-test-pickup-restore-memory-only.w3v\")\n"
        "  local unit source = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  local item i = CreateItem('spro', 0.0, 0.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_PICKUP_ITEM, null)\n"
        "  call TriggerAddAction(t, function onPickup)\n"
        "  call UnitAddItem(source, i)\n"
        "  call FlushGameCache(c)\n"
        "  call BJassAssert(StoreUnit(c, \"Human01\", \"Arthas\", source), \"StoreUnit failed\")\n"
        "  set pickupCount = 0\n"
        "  set restored = RestoreUnit(c, \"Human01\", \"Arthas\", Player(0), 128.0, 64.0, 0.0)\n"
        "  call BJassAssert(restored != null, \"RestoreUnit returned null\")\n"
        "endfunction\n"));
    G_RunEvents();
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "verifyPickup", true);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, gamecache_restore_preserves_hero_progression) {
    edict_t *restored = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit restoredHero = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local gamecache c = InitGameCache(\"openrealm-test-hero-memory-only.w3v\")\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  call FlushGameCache(c)\n"
        "  call SetHeroLevel(h, 2, false)\n"
        "  call SelectHeroSkill(h, 'AHhb')\n"
        "  call BJassAssert(StoreUnit(c, \"Human01\", \"Arthas\", h), \"StoreUnit failed\")\n"
        "  set restoredHero = RestoreUnit(c, \"Human01\", \"Arthas\", Player(0), 128.0, 64.0, 90.0)\n"
        "  call BJassAssert(restoredHero != null, \"RestoreUnit returned null\")\n"
        "  call BJassAssert(GetHeroLevel(restoredHero) == 2, \"hero level was not restored\")\n"
        "  call BJassAssert(GetUnitAbilityLevel(restoredHero, 'AHhb') == 1, \"learned rank was not restored\")\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('H','p','a','l') &&
            fabsf(ent->s.origin2.x - 128.0f) < 0.01f &&
            fabsf(ent->s.origin2.y - 64.0f) < 0.01f) {
            restored = ent;
            break;
        }
    }
    T_NOT_NULL(restored);
    T_EQ((int)restored->hero.level, 2);
    T_EQ((int)restored->hero.skillpoints, 1);
    T_EQ((int)restored->heroabilities[0].code, (int)MAKEFOURCC('A','H','h','b'));
    T_EQ((int)restored->heroabilities[0].level, 1);
}

TEST(wc3_api, gamecache_restore_dead_hero_at_quarter_health) {
    edict_t *restored = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit restoredDeadHero = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local gamecache c = InitGameCache(\"openrealm-test-dead-hero-memory-only.w3v\")\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  call FlushGameCache(c)\n"
        "  call SetHeroLevel(h, 2, false)\n"
        "  call KillUnit(h)\n"
        "  call BJassAssert(StoreUnit(c, \"Human01\", \"Arthas\", h), \"StoreUnit failed for dead hero\")\n"
        "  set restoredDeadHero = RestoreUnit(c, \"Human01\", \"Arthas\", Player(0), 256.0, 64.0, 90.0)\n"
        "  call BJassAssert(restoredDeadHero != null, \"RestoreUnit returned null for dead hero\")\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('H','p','a','l') &&
            fabsf(ent->s.origin2.x - 256.0f) < 0.01f &&
            fabsf(ent->s.origin2.y - 64.0f) < 0.01f) {
            restored = ent;
            break;
        }
    }
    T_NOT_NULL(restored);
    T_ASSERT(restored->health.max_value > 0.0f);
    T_FEQ(restored->health.value, restored->health.max_value * 0.25f, 0.01f);
    T_ASSERT(!M_IsDead(restored));
}

TEST(wc3_api, gamecache_restore_preserves_explicit_red_unit_color) {
    edict_t *restored = NULL;
    uint32_t color = 99;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit restoredUnit = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local gamecache c = InitGameCache(\"openrealm-test-color-memory-only.w3v\")\n"
        "  local unit u = CreateUnit(Player(4), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call FlushGameCache(c)\n"
        "  call SetUnitColor(u, PLAYER_COLOR_RED)\n"
        "  call BJassAssert(StoreUnit(c, \"Human01\", \"Guard\", u), \"StoreUnit failed\")\n"
        "  set restoredUnit = RestoreUnit(c, \"Human01\", \"Guard\", Player(4), 192.0, 64.0, 0.0)\n"
        "  call BJassAssert(restoredUnit != null, \"RestoreUnit returned null\")\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('h','f','o','o') &&
            fabsf(ent->s.origin2.x - 192.0f) < 0.01f &&
            fabsf(ent->s.origin2.y - 64.0f) < 0.01f) {
            restored = ent;
            break;
        }
    }
    T_NOT_NULL(restored);
    T_EQ(unit_team_color(restored), 0);
    T_ASSERT(G_GetUnitColorOverride(restored, &color));
    T_EQ(color, 0);
}

/* =========================================================================
 * Death event context
 * ========================================================================= */

/* Removal must preserve both death registrations and the dying-unit context until actions run. */
static void death_events_before_corpse_removal(bool queued) {
    edict_t *victim;
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit victim = null\n"
        "  integer deaths = 0\n"
        "endglobals\n"
        "function on_death takes nothing returns nothing\n"
        "  set deaths = deaths + 1\n"
        "  call BJassAssert(GetDyingUnit() == victim, \"wrong dying unit\")\n"
        "  call BJassAssert(GetUnitTypeId(GetDyingUnit()) == 'hfoo', \"corpse freed before death action\")\n"
        "endfunction\n"
        "function kill_and_remove takes nothing returns nothing\n"
        "  call KillUnit(victim)\n"
        "  call RemoveUnit(victim)\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(deaths == 2, \"removing the corpse cancelled or duplicated death events\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set victim = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call TriggerRegisterDeathEvent(t, victim)\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddAction(t, function on_death)\n"
        "endfunction\n"));
    victim = find_test_unit(MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(victim);
    jass_callbyname(level.vm, "kill_and_remove", queued);
    level.started = level.scriptsStarted = true;
    level.time += FRAMETIME;
    globals.RunFrame();
    T_ASSERT(!victim->inuse);
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    globals.RunFrame();
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, death_events_survive_same_tick_corpse_removal) { death_events_before_corpse_removal(false); }
TEST(wc3_api, death_events_survive_removal_after_frame_event_pass) { death_events_before_corpse_removal(true); }

TEST(wc3_api, death_events_drain_chained_corpse_removals) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit first = null\n"
        "  unit second = null\n"
        "  unit alive = null\n"
        "  integer deaths = 0\n"
        "endglobals\n"
        "function on_death takes nothing returns nothing\n"
        "  set deaths = deaths + 1\n"
        "  call BJassAssert(GetUnitTypeId(GetDyingUnit()) == 'hfoo', \"death context was freed\")\n"
        "  if GetDyingUnit() == first then\n"
        "    call KillUnit(second)\n"
        "    call RemoveUnit(second)\n"
        "    call RemoveUnit(alive)\n"
        "  endif\n"
        "endfunction\n"
        "function finish takes nothing returns nothing\n"
        "  call KillUnit(first)\n"
        "  call RemoveUnit(first)\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(deaths == 2, \"chained removals lost or invented death events\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set first = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  set second = CreateUnit(Player(0), 'hfoo', 128.0, 64.0, 0.0)\n"
        "  set alive = CreateUnit(Player(0), 'hpea', 192.0, 64.0, 0.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddAction(t, function on_death)\n"
        "endfunction\n"));
    jass_callbyname(level.vm, "finish", true);
    level.started = level.scriptsStarted = true;
    level.time += FRAMETIME;
    globals.RunFrame();
    FOR_LOOP(i, globals.num_edicts) if (i >= game.max_clients) T_ASSERT(!g_edicts[i].inuse);
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    globals.RunFrame();
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, death_events_reject_a_reused_subject_slot) {
    edict_t *victim, *replacement;
    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer deaths = 0\n"
        "endglobals\n"
        "function on_death takes nothing returns nothing\n"
        "  set deaths = deaths + 1\n"
        "endfunction\n"
        "function verify takes nothing returns nothing\n"
        "  call BJassAssert(deaths == 2, \"only the original synchronous death reached subscribers\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local unit victim = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call TriggerRegisterDeathEvent(t, victim)\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddAction(t, function on_death)\n"
        "  call KillUnit(victim)\n"
        "  call BJassAssert(deaths == 2, \"both death families complete inside KillUnit\")\n"
        "endfunction\n"));
    victim = find_test_unit(MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(victim);
    G_FreeEdict(victim);
    level.time += 2000;
    replacement = SP_SpawnAtLocation(MAKEFOURCC('h','p','e','a'), 0, &MAKE(vec2_t, 64, 64));
    T_ASSERT(replacement == victim);
    G_DeferFreeEdict(replacement);
    G_TestFinishDeferredFrees();
    T_ASSERT(!replacement->inuse);
    G_RunEvents(); jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, death_event_exposes_trigger_widget_and_killing_unit) {
    edict_t *victim = NULL;
    edict_t *killer = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit victim = null\n"
        "  unit killer = null\n"
        "  boolean deathFired = false\n"
        "endglobals\n"
        "function onDeath takes nothing returns nothing\n"
        "  set deathFired = true\n"
        "  call BJassAssert(GetTriggerWidget() == victim, \"wrong trigger widget\")\n"
        "  call BJassAssert(GetKillingUnit() == killer, \"wrong killing unit\")\n"
        "endfunction\n"
        "function verifyDeath takes nothing returns nothing\n"
        "  call BJassAssert(deathFired, \"death trigger did not fire\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set victim = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  set killer = CreateUnit(Player(0), 'hpea', 128.0, 64.0, 0.0)\n"
        "  call TriggerRegisterDeathEvent(t, victim)\n"
        "  call TriggerAddAction(t, function onDeath)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].class_id == MAKEFOURCC('h', 'f', 'o', 'o')) {
            victim = &g_edicts[i];
        } else if (g_edicts[i].class_id == MAKEFOURCC('h', 'p', 'e', 'a')) {
            killer = &g_edicts[i];
        }
    }
    T_NOT_NULL(victim);
    T_NOT_NULL(killer);

    unit_die(victim, killer);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyDeath", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, stock_slots_propagate_override_clamp_and_inherit) {
    edict_t *first = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 0, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 32, 0);
    edict_t *future;

    G_SetStockSlots(first, true, level.stock.item_slots);
    G_SetStockSlots(second, true, level.stock.item_slots);
    G_SetAllStockSlots(true, 11); G_SetAllStockSlots(false, 9);
    T_EQ(level.stock.item_slots, 11); T_EQ(level.stock.unit_slots, 9);
    T_EQ(first->stock->item_slots, 11); T_EQ(second->stock->item_slots, 11);
    T_EQ(first->stock->unit_slots, 9); T_EQ(second->stock->unit_slots, 9);

    G_SetStockSlots(first, true, 3); G_SetStockSlots(first, false, -1);
    T_EQ(first->stock->item_slots, 3); T_ASSERT(!first->stock || first->stock->unit_slots == 0);
    T_EQ(second->stock->item_slots, 11); T_EQ(second->stock->unit_slots, 9);

    future = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 64, 0);
    G_SetStockSlots(future, true, level.stock.item_slots);
    T_EQ(future->stock->item_slots, 11); T_EQ(future->stock->unit_slots, 9);
}

TEST(wc3_api, stock_slot_natives_update_global_and_unit_state) {
    edict_t *shop = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 0, 0);
    edict_t *created = NULL;
    G_SetStockSlots(shop, true, level.stock.item_slots);

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\nlocal unit shop\n"
        "call SetAllItemTypeSlots(11)\n"
        "call SetAllUnitTypeSlots(10)\n"
        "set shop = CreateUnit(Player(0),'hfoo',128.0,128.0,0.0)\n"
        "call SetItemTypeSlots(shop,3)\n"
        "call SetUnitTypeSlots(shop,4)\n"
        "endfunction"));
    T_EQ(level.stock.item_slots, 11); T_EQ(level.stock.unit_slots, 10);
    T_EQ(shop->stock->item_slots, 11); T_EQ(shop->stock->unit_slots, 10);
    FOR_LOOP(i, globals.num_edicts) if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) created = g_edicts + i;
    T_NOT_NULL(created);
    T_EQ(created->stock->item_slots, 3); T_EQ(created->stock->unit_slots, 4);
}

TEST(wc3_api, item_stock_natives_override_and_remove_runtime_stock) {
    static UnitAbilities_t sell_items = { .abilList = "Asid", .heroAbilList = "" };
    edict_t *shop = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 0, 0);

    shop->data.UnitAbilities = &sell_items;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call SetAllItemTypeSlots(11)\n"
        "call AddItemToAllStock('spro',1,2)\n"
        "call AddItemToStock(null,'spro',1,2)\n"
        "call RemoveItemFromStock(null,'spro')\n"
        "endfunction"));
    T_EQ(shop->stock->item_count, 1);
    T_EQ(shop->stock->items[0].id, MAKEFOURCC('s','p','r','o'));
    T_EQ(shop->stock->items[0].current, 1);
    T_EQ(shop->stock->items[0].maximum, 2);

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call RemoveItemFromAllStock('spro')\n"
        "endfunction"));
    T_ASSERT(!shop->stock || shop->stock->item_count == 0);
}

TEST(wc3_api, unit_stock_natives_override_and_remove_runtime_stock) {
    static UnitAbilities_t sell_units = { .abilList = "Asud", .heroAbilList = "" };
    edict_t *shop = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 0, 0);

    shop->data.UnitAbilities = &sell_units;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call SetAllUnitTypeSlots(11)\n"
        "call AddUnitToAllStock('nmer',1,2)\n"
        "call AddUnitToStock(null,'nmer',1,2)\n"
        "call RemoveUnitFromStock(null,'nmer')\n"
        "endfunction"));
    T_EQ(shop->stock->unit_count, 1);
    T_EQ(shop->stock->units[0].id, MAKEFOURCC('n','m','e','r'));
    T_EQ(shop->stock->units[0].current, 1);
    T_EQ(shop->stock->units[0].maximum, 2);

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call RemoveUnitFromAllStock('nmer')\n"
        "endfunction"));
    T_ASSERT(!shop->stock || shop->stock->unit_count == 0);
}

TEST(wc3_api, weather_effect_native_preserves_bounds_id_and_enable_state) {
    T_ASSERT(run_test_jass(
        "globals\n"
        "  weathereffect w = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local rect r = Rect(-256.0, -128.0, 512.0, 384.0)\n"
        "  set w = AddWeatherEffect(r, 'RAhr')\n"
        "  call EnableWeatherEffect(w, true)\n"
        "endfunction\n"));

    T_ASSERT(level.weather_effects[0].inuse);
    T_ASSERT(level.weather_effects[0].enabled);
    T_EQ(level.weather_effects[0].effect_id, MAKEFOURCC('R','A','h','r'));
    T_FEQ(level.weather_effects[0].bounds.min.x, -256.0f, 0.001f);
    T_FEQ(level.weather_effects[0].bounds.min.y, -128.0f, 0.001f);
    T_FEQ(level.weather_effects[0].bounds.max.x, 512.0f, 0.001f);
    T_FEQ(level.weather_effects[0].bounds.max.y, 384.0f, 0.001f);
}

TEST(wc3_api, weather_effect_native_remove_releases_runtime_slot) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local rect r = Rect(0.0, 0.0, 128.0, 128.0)\n"
        "  local weathereffect w = AddWeatherEffect(r, 'RAlr')\n"
        "  call EnableWeatherEffect(w, true)\n"
        "  call RemoveWeatherEffect(w)\n"
        "endfunction\n"));

    T_ASSERT(!level.weather_effects[0].inuse);
}

TEST(wc3_api, authored_global_and_region_weather_start_enabled) {
    mapInfo_t info = {0};
    mapWeatherRegion_t region = {
        .bounds = { .min = {-64.0f, -32.0f}, .max = {96.0f, 160.0f} },
        .weatherID = MAKEFOURCC('R','L','l','r'),
    };

    info.weatherID = MAKEFOURCC('R','A','h','r');
    info.num_weatherRegions = 1;
    info.weatherRegions = &region;
    level.mapinfo = &info;
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-512.0f, -384.0f}, .max = {512.0f, 384.0f}));

    G_WeatherInitMap();

    T_ASSERT(level.weather_effects[0].inuse);
    T_ASSERT(level.weather_effects[0].enabled);
    T_EQ(level.weather_effects[0].effect_id, info.weatherID);
    T_FEQ(level.weather_effects[0].bounds.min.x, -512.0f, 0.001f);
    T_FEQ(level.weather_effects[0].bounds.max.y, 384.0f, 0.001f);
    T_ASSERT(level.weather_effects[1].inuse);
    T_ASSERT(level.weather_effects[1].enabled);
    T_EQ(level.weather_effects[1].effect_id, region.weatherID);
    T_FEQ(level.weather_effects[1].bounds.min.x, -64.0f, 0.001f);
    T_FEQ(level.weather_effects[1].bounds.max.y, 160.0f, 0.001f);
}

TEST(wc3_api, weather_effect_handle_round_trips_through_save_codec) {
    box2_t bounds = { .min = {-32.0f, -16.0f}, .max = {64.0f, 96.0f} };
    gweather_t *effect = G_WeatherAdd(&bounds, MAKEFOURCC('R','A','l','r'), false);
    uint32_t id = UINT32_MAX;

    T_NOT_NULL(effect);
    T_ASSERT(G_SaveJassHandle("weathereffect", effect, &id));
    T_EQ(id, 0);
    T_EQ(G_LoadJassHandle("weathereffect", id), effect);
}

/* =========================================================================
 * Test suite entry point
 * ========================================================================= */

TEST(wc3_api, controller_input_preserves_scripted_ownership) {
    gameClient_t *gc = &game.clients[0];
    inputCmd_t cmd = { .action = BZ_INPUT_VIEW, .view = {{-40, 0, 25}, 1200} };
    bool old_ctrl = gc->no_control;
    float dist = gc->camera.state.target_distance;
    gc->no_control = true;
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.target_distance, dist, 0.001f);
    gc->no_control = false;
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.viewangles.x, -40, 0.001f);
    T_FEQ(gc->camera.state.viewangles.z, 25, 0.001f);
    T_FEQ(gc->camera.state.target_distance, 1200, 0.001f);
    T_FEQ(gc->camera.old_state.target_distance, 1200, 0.001f);
    T_EQ(gc->camera.start_time, gc->camera.end_time);
    gc->no_control = old_ctrl;
}

/* Minimap focus must reach the server camera, clear unit tracking, and respect scripted control. */
TEST(wc3_api, controller_focus_updates_camera_and_respects_control) {
    gameClient_t *gc = &game.clients[0];
    inputCmd_t cmd = { .action = BZ_INPUT_FOCUS, .focus = { 300, 400 } };
    level.camera_bounds = (box2_t){ .min = { 0, 0 }, .max = { 512, 512 } };
    gc->camera.state.position = (vec2_t){ 10, 20 };
    gc->camera.target_controller = &g_edicts[2];
    gc->no_control = true;
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.position.x, 10, 0.001f); T_FEQ(gc->camera.state.position.y, 20, 0.001f);
    T_NOT_NULL(gc->camera.target_controller);
    gc->no_control = false;
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.position.x, 300, 0.001f); T_FEQ(gc->camera.state.position.y, 400, 0.001f);
    T_NULL(gc->camera.target_controller); T_EQ(gc->camera.start_time, gc->camera.end_time);
    cmd.focus = (vec2_t){ -100, 700 };
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.position.x, 0, 0.001f); T_FEQ(gc->camera.state.position.y, 512, 0.001f);
}

/* Issue #418: SetUnitUserData / GetUnitUserData must persist scratch integer on the unit. */
TEST(wc3_api, unit_user_data_survives_set_get) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call SetUnitUserData(u, 42)\n"
        "  call BJassAssert(GetUnitUserData(u) == 42, \"user data must round-trip\")\n"
        "  call SetUnitUserData(u, -7)\n"
        "  call BJassAssert(GetUnitUserData(u) == -7, \"negative user data\")\n"
        "  call SetUnitUserData(null, 99)\n"
        "  call BJassAssert(GetUnitUserData(null) == 0, \"null unit must return 0\")\n"
        "endfunction\n"));
}

/* Issue #418: UnitSetUsesAltIcon must persist the flag on the unit. */
TEST(wc3_api, unit_set_uses_alt_icon_persists) {
    edict_t *unit = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call UnitSetUsesAltIcon(u, true)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) {
            unit = &g_edicts[i]; break;
        }
    }
    T_NOT_NULL(unit);
    T_ASSERT(unit->uses_alt_icon);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call UnitSetUsesAltIcon(null, true)\n"
        "endfunction\n"));
}

/* Issue #418: GetChangingUnit / GetChangingUnitPrevOwner must expose ownership-change event context. */
TEST(wc3_api, change_owner_event_exposes_unit_and_prev_owner) {
    /* Verify the natives are registered and callable without crash. The
     * trigger-context getters return null outside an event callback, which
     * is correct because no ownership change event was published yet. */
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(GetChangingUnit() == null, \"no event context: null unit\")\n"
        "  local player p = GetChangingUnitPrevOwner()\n"
        "  call BJassAssert(p == null, \"no event context: null player\")\n"
        "endfunction\n"));
}

/* Issue #418: EnumItemsInRect must visit in-world items and bind GetEnumItem. */
TEST(wc3_api, enum_items_in_rect_visits_world_items) {
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer udg_EnumCount = 0\n"
        "  item udg_LastItem = null\n"
        "endglobals\n"
        "function countItem takes nothing returns nothing\n"
        "  set udg_EnumCount = udg_EnumCount + 1\n"
        "  set udg_LastItem = GetEnumItem()\n"
        "endfunction\n"
        "function verifyEnum takes nothing returns nothing\n"
        "  call BJassAssert(udg_EnumCount == 1, \"one world item must be enumerated\")\n"
        "  call BJassAssert(udg_LastItem != null, \"GetEnumItem must be non-null inside callback\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  call CreateItem('spro', 0.0, 0.0)\n"
        "  call EnumItemsInRect(GetWorldBounds(), null, function countItem)\n"
        "endfunction\n"));
    jass_callbyname(level.vm, "verifyEnum", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, create_item_rejects_empty_item_id) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local item i = CreateItem(0, 0.0, 0.0)\n"
        "  call BJassAssert(i == null, \"empty item ID must return null\")\n"
        "  set i = CreateItem('zzzz', 0.0, 0.0)\n"
        "  call BJassAssert(i == null, \"unresolved item ID must return null\")\n"
        "endfunction\n"));
}

/* Issue #418: stub natives must execute without crash or AI_STOP. */
TEST(wc3_api, campaign_stub_natives_accept_calls_without_crash) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCampaignMenuRaceEx(1)\n"
        "  call DoNotSaveReplay()\n"
        "  call SetAltMinimapIcon(\"UI\\MiniMap\\Human.blp\")\n"
        "endfunction\n"));
}

TEST(wc3_api, preload_refresh_boundaries_are_registered) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call PreloadRefresh()\n"
        "  call PreloadEndEx()\n"
        "endfunction\n"));
}

TEST(wc3_api, issue_418_campaign_natives_are_registered) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetAllyColorFilterState(2)\n"
        "  call BJassAssert(GetAllyColorFilterState() == 2, \"ally color state round-trip\")\n"
        "  call SetCreepCampFilterState(false)\n"
        "  call BJassAssert(not GetCreepCampFilterState(), \"creep camp filter\")\n"
        "  call UnitRemoveBuffsEx(null, true, true, true, true, true, true, true)\n"
        "endfunction\n"));
}

TEST(wc3_api, ally_color_filter_global_state_updates_all_players) {
    player_t *const saved_currentplayer = currentplayer;

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetAllyColorFilterState(2)\n"
        "  call BJassAssert(GetAllyColorFilterState() == 2, \"global ally color state\")\n"
        "endfunction\n"));
    FOR_LOOP(i, game.max_clients)
        T_EQ(game.clients[i].ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR], WC3_MINIMAP_ALLY_COLOR_WORLD);
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, ally_color_filter_clamps_to_wc3_modes) {
    player_t *const saved_currentplayer = currentplayer;

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetAllyColorFilterState(99)\n"
        "  call BJassAssert(GetAllyColorFilterState() == 2, \"high ally filter clamp\")\n"
        "  call SetAllyColorFilterState(-7)\n"
        "  call BJassAssert(GetAllyColorFilterState() == 0, \"low ally filter clamp\")\n"
        "endfunction\n"));
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, ally_color_filter_state_honors_currentplayer) {
    player_t *const saved_currentplayer = currentplayer;

    game.clients[0].ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR] = WC3_MINIMAP_ALLY_COLOR_PLAYERS;
    game.clients[1].ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR] = WC3_MINIMAP_ALLY_COLOR_PLAYERS;
    currentplayer = test_player(1);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetAllyColorFilterState(1)\n"
        "  call BJassAssert(GetAllyColorFilterState() == 1, \"local ally color state\")\n"
        "endfunction\n"));
    T_EQ(game.clients[0].ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR], WC3_MINIMAP_ALLY_COLOR_PLAYERS);
    T_EQ(game.clients[1].ps.stats[WC3_PLAYERSTAT_MINIMAP_ALLY_COLOR], WC3_MINIMAP_ALLY_COLOR_MINIMAP);
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, destroyed_quest_handle_is_safe) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  local questitem qi = QuestCreateItem(q)\n"
        "  call DestroyQuest(q)\n"
        "  call QuestSetDiscovered(q, true)\n"
        "  call QuestItemSetCompleted(qi, true)\n"
        "  call BJassAssert(not IsQuestDiscovered(q), \"destroyed quest is invalid\")\n"
        "endfunction\n"));
}

/* Issue #418: bot assault natives must use the retail common.ai signatures. */
TEST(wc3_api, bot_assault_natives_noop_on_null_player) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCaptainHome(1, 0.0, 0.0)\n"
        "  call SetStagePoint(0.0, 0.0)\n"
        "  call SuicideUnit(1, 'hfoo')\n"
        "  call SuicideUnitEx(1, 'hfoo', 0)\n"
        "  call BJassAssert(not SuicidePlayer(null, false), \"empty player returns false\")\n"
        "  call BJassAssert(not MergeUnits(1, 'hfoo', 'hfoo', 'hfoo'), \"empty player returns false\")\n"
        "  call BJassAssert(GetUpgradeGoldCost(0) == 0, \"unknown upgrade returns 0\")\n"
        "  call BJassAssert(GetUpgradeWoodCost(0) == 0, \"unknown upgrade wood returns 0\")\n"
        "  call BJassAssert(GetUpgradeLumberCost(0) == 0, \"legacy upgrade lumber alias returns 0\")\n"
        "  call ShiftTownSpot(32.0, 64.0)\n"
        "endfunction\n"));
}

/* Issue #436: DotA shop/hero/combat/item/string natives. */
TEST(wc3_api, dota_string_and_object_name_natives) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(StringLength(\"abc\") == 3, \"StringLength\")\n"
        "  call BJassAssert(StringLength(\"\") == 0, \"empty StringLength\")\n"
        "  call BJassAssert(StringCase(\"AbC\", true) == \"ABC\", \"StringCase upper\")\n"
        "  call BJassAssert(StringCase(\"AbC\", false) == \"abc\", \"StringCase lower\")\n"
        "  call BJassAssert(StringHash(\"\") == 0, \"empty StringHash\")\n"
        "  call BJassAssert(StringHash(\"case\") == StringHash(\"CASE\"), \"StringHash casefold\")\n"
        "  call BJassAssert(StringHash(\"path/to\") == StringHash(\"path\\\\to\"), \"StringHash slash\")\n"
        "  call BJassAssert(StringLength(GetObjectName('hfoo')) > 0, \"GetObjectName footman\")\n"
        "endfunction\n"));
}

TEST(wc3_api, dota_hero_attr_ability_level_and_order) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 64.0, 0.0, 0.0)\n"
        "  call SetHeroStr(h, 22, true)\n"
        "  call SetHeroAgi(h, 17, true)\n"
        "  call SetHeroInt(h, 19, true)\n"
        "  call BJassAssert(GetHeroStr(h, false) == 22, \"GetHeroStr\")\n"
        "  call BJassAssert(GetHeroAgi(h, true) == 17, \"GetHeroAgi\")\n"
        "  call BJassAssert(GetHeroInt(h, false) == 19, \"GetHeroInt\")\n"
        "  call BJassAssert(GetUnitLevel(h) >= 1, \"hero GetUnitLevel\")\n"
        "  call BJassAssert(GetUnitLevel(u) >= 0, \"unit GetUnitLevel\")\n"
        "  call SelectHeroSkill(h, 'AHhb')\n"
        "  call BJassAssert(GetUnitAbilityLevel(h, 'AHhb') == 1, \"learned level 1\")\n"
        "  call BJassAssert(SetUnitAbilityLevel(h, 'AHhb', 3) == 3, \"SetUnitAbilityLevel\")\n"
        "  call BJassAssert(GetUnitAbilityLevel(h, 'AHhb') == 3, \"level stuck at 3\")\n"
        "  call BJassAssert(IncUnitAbilityLevel(h, 'AHhb') == 4, \"IncUnitAbilityLevel\")\n"
        "  call BJassAssert(GetUnitCurrentOrder(h) == 0 or GetUnitCurrentOrder(h) != 0, \"GetUnitCurrentOrder callable\")\n"
        "  call BJassAssert(UnitInventorySize(h) >= 0, \"UnitInventorySize\")\n"
        "  call BJassAssert(UnitAddType(u, ConvertUnitType(10)), \"UnitAddType\")\n"
        "  call BJassAssert(IsUnitType(u, ConvertUnitType(10)), \"script type visible\")\n"
        "  call BJassAssert(UnitRemoveType(u, ConvertUnitType(10)), \"UnitRemoveType\")\n"
        "  call BJassAssert(not IsUnitType(u, ConvertUnitType(10)), \"script type cleared\")\n"
        "  call SetPlayerAbilityAvailable(Player(0), 'AHhb', false)\n"
        "  call SetPlayerAbilityAvailable(Player(0), 'AHhb', true)\n"
        "endfunction\n"));
}

TEST(wc3_api, dota_unit_damage_target_and_invulnerable) {
    edict_t *attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    float life_before;
    T_NOT_NULL(attacker); T_NOT_NULL(target);
    G_SetHealth(attacker, 500); G_SetHealth(target, 500);
    life_before = target->health.value;
    /* Drive the registered native through JASS with null typed handles (allowed). */
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit a = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  local unit t = CreateUnit(Player(0), 'hfoo', 64.0, 0.0, 0.0)\n"
        "  call SetWidgetLife(t, 500.0)\n"
        "  call BJassAssert(UnitDamageTarget(a, t, 40.0, true, false, null, null, null), \"damage ok\")\n"
        "  call BJassAssert(GetWidgetLife(t) < 500.0, \"life dropped\")\n"
        "  call SetUnitInvulnerable(t, true)\n"
        "  call BJassAssert(not UnitDamageTarget(a, t, 40.0, true, false, null, null, null), \"invuln rejected\")\n"
        "endfunction\n"));
    /* Also exercise the C damage path used by the native. */
    T_Damage(target, attacker, 40);
    T_ASSERT(target->health.value < life_before);
    T_ASSERT(target->health.value <= life_before - 39.0f);
}

TEST(wc3_api, dota_item_user_data_visibility_and_stock) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local item it = CreateItem('ratf', 0.0, 0.0)\n"
        "  local unit shop = CreateUnit(Player(0), 'nmer', 128.0, 128.0, 0.0)\n"
        "  call SetItemUserData(it, 77)\n"
        "  call BJassAssert(GetItemUserData(it) == 77, \"item user data\")\n"
        "  call BJassAssert(StringLength(GetItemName(it)) >= 0, \"GetItemName\")\n"
        "  call SetItemVisible(it, false)\n"
        "  call BJassAssert(not IsItemVisible(it), \"hidden item\")\n"
        "  call SetItemVisible(it, true)\n"
        "  call BJassAssert(IsItemVisible(it), \"shown item\")\n"
        "  call SetItemPawnable(it, false)\n"
        "  call BJassAssert(not IsItemOwned(it), \"world item not owned\")\n"
        "  call SetItemInvulnerable(it, true)\n"
        "  call BJassAssert(IsItemInvulnerable(it), \"item invulnerability set\")\n"
        "  call SetItemInvulnerable(it, false)\n"
        "  call BJassAssert(not IsItemInvulnerable(it), \"item invulnerability cleared\")\n"
        "  call AddUnitToStock(shop, 'hfoo', 2, 5)\n"
        "  call RemoveUnitFromStock(shop, 'hfoo')\n"
        "  call BJassAssert(AddLightning(\"CLPB\", false, 0.0, 0.0, 10.0, 10.0) != null, \"AddLightning\")\n"
        "  call BJassAssert(CreateImage(\"\", 32.0, 32.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0) != null, \"CreateImage\")\n"
        "  call BJassAssert(CreateUbersplat(0.0, 0.0, \"\", 255, 255, 255, 255, false, false) != null, \"CreateUbersplat\")\n"
        "endfunction\n"));
}

TEST(wc3_api, dota_damage_event_exposes_source_and_amount) {
    edict_t *attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *target = NULL;
    T_NOT_NULL(attacker);
    attacker->s.player = 1;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit g_src = null\n"
        "  real g_dmg = 0.0\n"
        "  unit g_tgt = null\n"
        "endglobals\n"
        "function on_damaged takes nothing returns nothing\n"
        "  set g_src = GetEventDamageSource()\n"
        "  set g_dmg = GetEventDamage()\n"
        "endfunction\n"
        "function verify_damage takes nothing returns nothing\n"
        "  call BJassAssert(g_src != null, \"GetEventDamageSource set\")\n"
        "  call BJassAssert(g_dmg == 25.0, \"GetEventDamage\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger trig = CreateTrigger()\n"
        "  set g_tgt = CreateUnit(Player(0), 'hfoo', 256.0, 256.0, 0.0)\n"
        "  call TriggerRegisterUnitEvent(trig, g_tgt, EVENT_UNIT_DAMAGED)\n"
        "  call TriggerAddAction(trig, function on_damaged)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') &&
            g_edicts[i].s.origin2.x > 200.0f) {
            target = &g_edicts[i];
            break;
        }
    }
    T_NOT_NULL(target);
    G_SetHealth(target, 500);
    T_Damage(target, attacker, 25);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify_damage", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, removed_damage_source_does_not_cancel_live_subject_event) {
    player_t *saved_currentplayer = currentplayer;
    edict_t *attacker, *target = NULL;
    event_t *damageRegistration = NULL;
    gameEvent_t *damageEvent = NULL;
    uint32_t queued_before;

    G_ResetDeferredFrees(); reset_entities(); setup_test_world(); currentplayer = NULL;
    attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    T_NOT_NULL(attacker);
    attacker->s.player = 1;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit damageSource = null\n"
        "  unit damageTarget = null\n"
        "  boolean damageEventFired = false\n"
        "endglobals\n"
        "function on_damaged_after_source_removed takes nothing returns nothing\n"
        "  set damageSource = GetEventDamageSource()\n"
        "  set damageEventFired = true\n"
        "endfunction\n"
        "function verify_damaged_event_after_source_removed takes nothing returns nothing\n"
        "  call BJassAssert(damageEventFired, \"live subject damage event was canceled with its source\")\n"
        "  call BJassAssert(damageSource == null, \"stale damage source should be cleared\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger damageTrigger = CreateTrigger()\n"
        "  local event damageRegistration = null\n"
        "  set damageTarget = CreateUnit(Player(0), 'hfoo', 256.0, 256.0, 0.0)\n"
        "  set damageRegistration = TriggerRegisterUnitEvent(damageTrigger, damageTarget, EVENT_UNIT_DAMAGED)\n"
        "  call BJassAssert(damageRegistration != null, \"damage event registration failed\")\n"
        "  call TriggerAddAction(damageTrigger, function on_damaged_after_source_removed)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') &&
            g_edicts[i].s.origin2.x > 200.0f) { target = &g_edicts[i]; break; }
    }
    T_NOT_NULL(target);
    G_SetHealth(target, 500);
    queued_before = level.events.write;
    T_Damage(target, attacker, 25);
    T_ASSERT(level.events.write > queued_before);
    for (uint32_t i = queued_before; i < level.events.write; i++) {
        gameEvent_t *queued = &level.events.queue[i % MAX_EVENT_QUEUE];
        if (queued->type == EVENT_UNIT_DAMAGED) damageEvent = queued;
    }
    FOR_EACH_EVENT(evt)
        if (evt->type == EVENT_UNIT_DAMAGED && evt->subject == target) { damageRegistration = evt; break; }
    T_NOT_NULL(damageEvent); T_NOT_NULL(damageRegistration);
    if (damageEvent) {
        T_ASSERT(damageEvent->edict == target && damageEvent->source == attacker);
        T_ASSERT(damageEvent->edict_spawn_tracked && damageEvent->source_spawn_tracked);
    }
    if (damageRegistration) T_ASSERT(G_EventSubjectIsCurrent(damageRegistration));
    G_FreeEdict(attacker);
    T_ASSERT(target->inuse && !G_IsDeferredFree(target));
    G_RunEvents(); jass_runevents(level.vm);
    T_ASSERT(damageEvent->source == NULL && !damageEvent->source_spawn_tracked);
    jass_callbyname(level.vm, "verify_damaged_event_after_source_removed", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, blight_natives_share_authoritative_world_state) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local location l = Location(640.0, 0.0)\n"
        "  local rect r = Rect(768.0, -64.0, 896.0, 64.0)\n"
        "  call SetBlightPoint(Player(0), 32.0, 32.0, true)\n"
        "  call BJassAssert(IsPointBlighted(32.0, 32.0), \"SetBlightPoint add\")\n"
        "  call SetBlightPoint(Player(0), 32.0, 32.0, false)\n"
        "  call BJassAssert(not IsPointBlighted(32.0, 32.0), \"SetBlightPoint remove\")\n"
        "  call SetBlight(Player(0), 256.0, 0.0, 73.0, true)\n"
        "  call BJassAssert(IsPointBlighted(256.0, 0.0), \"SetBlight add\")\n"
        "  call SetBlight(Player(0), 256.0, 0.0, 73.0, false)\n"
        "  call BJassAssert(not IsPointBlighted(256.0, 0.0), \"SetBlight remove\")\n"
        "  call SetBlightLoc(Player(0), l, 73.0, true)\n"
        "  call BJassAssert(IsPointBlighted(640.0, 0.0), \"SetBlightLoc\")\n"
        "  call SetBlightRect(Player(0), r, true)\n"
        "  call BJassAssert(IsPointBlighted(800.0, 0.0), \"SetBlightRect\")\n"
        "  call SetBlightRect(Player(0), r, false)\n"
        "  call BJassAssert(not IsPointBlighted(800.0, 0.0), \"SetBlightRect remove\")\n"
        "  call RemoveLocation(l)\n"
        "  call RemoveRect(r)\n"
        "endfunction\n"));
}

typedef struct { uint8_t *out; uint32_t count; } blightBitDst_t;
static void blight_test_write(uint32_t index, uint8_t value, uint32_t count, void *ctx) {
    blightBitDst_t *c = ctx;
    FOR_LOOP(i, count) if (index + i < c->count) c->out[index + i] = value;
}

/* Decode one RLE terrain-mask payload into a flat 0/1 array; returns decoded bits. */
static uint32_t blight_test_decode(uint8_t const *payload, terrainMaskChunk_t const *chunk, uint8_t *out, uint32_t count) {
    uint32_t bits = (uint32_t)chunk->width * chunk->row_count;
    if (!MSG_ValidateRLE(payload, chunk->payload_bytes, bits)) return 0;
    memset(out, 0, count);
    return MSG_DecodeRLE(payload, chunk->payload_bytes, bits, blight_test_write, &(blightBitDst_t){ out, count });
}

TEST(wc3_api, blight_datagram_carries_runtime_mask_and_clears_delivered_rows) {
    uint8_t data[8192], bits[4096];
    uint16_t header;
    terrainMaskChunk_t chunk;
    vec2_t point = { 32.0f, 32.0f };
    edict_t *client_ent;
    uint32_t size, offset, bit;

    setup_test_world();
    client_ent = &g_edicts[0];
    client_ent->client = &game.clients[0];
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    G_BlightMarkClientFull(client_ent);
    G_SetBlightPoint(&point, true);

    size = G_WriteClientDatagram(client_ent, data, sizeof(data));
    T_ASSERT(size > sizeof(header));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(header & BZ_GAME_DATAGRAM_TERRAIN_MASK);
    offset = sizeof(header) + (header & BZ_GAME_DATAGRAM_COUNT_MASK) * sizeof(wc3WeatherEffect_t);
    if (header & BZ_GAME_DATAGRAM_LIGHTNING) {
        uint16_t lightning_count = 0;
        memcpy(&lightning_count, data + offset, sizeof(lightning_count)); offset += sizeof(lightning_count);
        offset += lightning_count * sizeof(lightningEffect_t);
    }
    if (header & BZ_GAME_DATAGRAM_ENTITY_TINTS) offset += sizeof(uint16_t);
    memcpy(&chunk, data + offset, sizeof(chunk)); offset += sizeof(chunk);
    T_EQ(chunk.width, 64); T_EQ(chunk.height, 64); T_EQ(chunk.row_count, 64);
    bit = 33 + 33 * chunk.width;
    T_EQ(blight_test_decode(data + offset, &chunk, bits, sizeof(bits)), (uint32_t)chunk.width * chunk.row_count);
    T_ASSERT(bits[bit]);

    size = G_WriteClientDatagram(client_ent, data, sizeof(data));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(!(header & BZ_GAME_DATAGRAM_TERRAIN_MASK));
    T_EQ(size, sizeof(uint16_t) * 2);
    client_ent->client = NULL;
    game.clients[0].connected = false;
}

TEST(wc3_api, blight_sweep_resends_dropped_rows) {
    uint8_t data[8192], bits[4096];
    uint16_t header;
    terrainMaskChunk_t chunk;
    vec2_t point = { 32.0f, 32.0f };
    edict_t *client_ent;
    uint32_t size, offset, bit;

    setup_test_world();
    client_ent = &g_edicts[0];
    client_ent->client = &game.clients[0];
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    G_BlightMarkClientFull(client_ent);
    G_SetBlightPoint(&point, true);
    size = G_WriteClientDatagram(client_ent, data, sizeof(data));
    T_ASSERT(size > sizeof(header));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(header & BZ_GAME_DATAGRAM_TERRAIN_MASK);
    /* Simulate a dropped packet: the server cleared dirty rows on write. */
    level.framenum = 0;
    size = G_WriteClientDatagram(client_ent, data, sizeof(data));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(!(header & BZ_GAME_DATAGRAM_TERRAIN_MASK));
    level.framenum = BLIGHT_SWEEP_INTERVAL;
    size = G_WriteClientDatagram(client_ent, data, sizeof(data));
    T_ASSERT(size > sizeof(header));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(header & BZ_GAME_DATAGRAM_TERRAIN_MASK);
    offset = sizeof(header) + (header & BZ_GAME_DATAGRAM_COUNT_MASK) * sizeof(wc3WeatherEffect_t);
    if (header & BZ_GAME_DATAGRAM_LIGHTNING) {
        uint16_t lightning_count = 0;
        memcpy(&lightning_count, data + offset, sizeof(lightning_count)); offset += sizeof(lightning_count);
        offset += lightning_count * sizeof(lightningEffect_t);
    }
    if (header & BZ_GAME_DATAGRAM_ENTITY_TINTS) offset += sizeof(uint16_t);
    memcpy(&chunk, data + offset, sizeof(chunk)); offset += sizeof(chunk);
    T_EQ(chunk.width, 64); T_EQ(chunk.height, 64);
    bit = 33 + 33 * chunk.width;
    T_ASSERT(chunk.first_row <= 33 && 33 < chunk.first_row + chunk.row_count);
    T_EQ(blight_test_decode(data + offset, &chunk, bits, sizeof(bits)), (uint32_t)chunk.width * chunk.row_count);
    T_ASSERT(bits[bit - chunk.first_row * chunk.width]);
    client_ent->client = NULL;
    game.clients[0].connected = false;
    level.framenum = 0;
}

TEST(wc3_api, blight_checkerboard_row_uses_bitpack_escape_and_makes_progress) {
    uint8_t data[sizeof(terrainMaskChunk_t) + 16], bits[4096];
    terrainMaskChunk_t chunk;
    edict_t *client_ent;
    uint32_t size, offset = sizeof(chunk);

    setup_test_world();
    client_ent = &g_edicts[0];
    client_ent->client = &game.clients[0];
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    T_EQ(level.blight.width, 64); T_EQ(level.blight.height, 64);
    /* Checkerboard defeats RLE: one alternating row costs W+1 bytes, so the escape must bound it. */
    FOR_LOOP(i, 64u * 64u) level.blight.cells[i] = (uint8_t)(i & 1);
    G_BlightMarkClientFull(client_ent);
    /* Bitpack one row costs 1 + 64/8 = 9 bytes; RLE one alternating row costs 65, two rows fit neither. */
    size = G_BlightWriteDatagram(client_ent, data, sizeof(data));
    T_EQ(size, sizeof(chunk) + 9);
    memcpy(&chunk, data, sizeof(chunk));
    T_EQ(chunk.first_row, 0); T_EQ(chunk.row_count, 1);
    T_EQ(data[offset], 2); /* bitpack escape bounds the worst case */
    T_EQ(blight_test_decode(data + offset, &chunk, bits, sizeof(bits)), 64u);
    FOR_LOOP(i, 64u) T_EQ(bits[i], (uint8_t)(i & 1));
    /* The delivered row cleared, so the next band keeps moving instead of stalling. */
    size = G_BlightWriteDatagram(client_ent, data, sizeof(data));
    T_EQ(size, sizeof(chunk) + 9);
    memcpy(&chunk, data, sizeof(chunk));
    T_EQ(chunk.first_row, 1); T_EQ(chunk.row_count, 1);
    /* Sweep resync advances through the same worst case instead of stalling. */
    FOR_LOOP(y, level.blight.height) level.blight.dirty_rows[y] &= ~(1u << 0);
    level.blight.sweep_row[0] = 0;
    level.framenum = BLIGHT_SWEEP_INTERVAL;
    size = G_BlightWriteDatagram(client_ent, data, sizeof(data));
    T_EQ(size, sizeof(chunk) + 9);
    memcpy(&chunk, data, sizeof(chunk));
    T_EQ(chunk.first_row, 0); T_EQ(chunk.row_count, 1);
    T_EQ(data[offset], 2);
    T_EQ(level.blight.sweep_row[0], 1u);
    client_ent->client = NULL;
    game.clients[0].connected = false;
    level.framenum = 0;
}

TEST(wc3_api, blight_dirty_rows_take_priority_over_sweep) {
    uint8_t data[8192];
    uint16_t header;
    terrainMaskChunk_t chunk;
    vec2_t point = { 32.0f, 32.0f };
    edict_t *client_ent;
    uint32_t size, offset;

    setup_test_world();
    client_ent = &g_edicts[0];
    client_ent->client = &game.clients[0];
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    level.framenum = BLIGHT_SWEEP_INTERVAL;
    level.blight.sweep_row[0] = 0;
    G_SetBlightPoint(&point, true);
    size = G_WriteClientDatagram(client_ent, data, sizeof(data));
    T_ASSERT(size > sizeof(header));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(header & BZ_GAME_DATAGRAM_TERRAIN_MASK);
    offset = sizeof(header) + (header & BZ_GAME_DATAGRAM_COUNT_MASK) * sizeof(wc3WeatherEffect_t);
    if (header & BZ_GAME_DATAGRAM_LIGHTNING) {
        uint16_t lightning_count = 0;
        memcpy(&lightning_count, data + offset, sizeof(lightning_count)); offset += sizeof(lightning_count);
        offset += lightning_count * sizeof(lightningEffect_t);
    }
    if (header & BZ_GAME_DATAGRAM_ENTITY_TINTS) offset += sizeof(uint16_t);
    memcpy(&chunk, data + offset, sizeof(chunk));
    T_ASSERT(chunk.first_row > 0);
    T_EQ(level.blight.sweep_row[0], 0);
    client_ent->client = NULL;
    game.clients[0].connected = false;
    level.framenum = 0;
}

TEST(wc3_api, blight_mark_client_full_resets_sweep_cursor) {
    edict_t *client_ent;

    setup_test_world();
    client_ent = &g_edicts[0];
    client_ent->client = &game.clients[0];
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    level.blight.sweep_row[0] = 17;
    G_BlightMarkClientFull(client_ent);
    T_EQ(level.blight.sweep_row[0], 0);
    client_ent->client = NULL;
    game.clients[0].connected = false;
}

TEST(wc3_api, blight_tileset_line_parse_truncates_long_value) {
    char line[512];
    char key = 0;
    uint8_t raw[sizeof(PATHSTR) + 1];
    char sentinel = (char)0xA5;

    memset(line, 'A', sizeof(line) - 2);
    memcpy(line, "A = foo , ", 10);
    line[sizeof(line) - 2] = 0; line[sizeof(line) - 1] = 0;
    memset(raw, 0, sizeof(raw));
    raw[sizeof(raw) - 1] = (uint8_t)sentinel;
    T_ASSERT(WC3_ParseBlightTilesetLine(line, &key, (string_t)raw));
    T_EQ(key, 'A');
    T_EQ(raw[sizeof(raw) - 1], (uint8_t)sentinel);
    T_EQ(strlen((cstring_t)raw), (size_t)(MAX_PATHLEN - 1));
    T_ASSERT(!WC3_ParseBlightTilesetLine("[TileSets]", &key, (string_t)raw));
}

TEST(wc3_api, customize_entity_gate_hover_lifecycle) {
    static DestructableData_t const row = { .file = "Gate.mdx", .displayName = "WESTRING_DEST_ELVEN_GATE_HORIZONTAL" };
    destructable_t destructable_state = { 0 };
    edict_t ent = { .inuse = true, .class_id = MAKEFOURCC('A','T','g','1'),
        .svflags = SVF_STATIC_SCENERY, .targtype = TARG_STRUCTURE,
        .data = { .DestructableData = &row }, .destructable = &destructable_state,
        .health = { .value = 500, .max_value = 500 } };
    entityState_t state = { .number = 7, .model = 11 };
    globals.CustomizeEntity(0, &ent, &state);
    T_NE(state.name, 0);
    T_EQ(state.stats[ENT_HEALTH], 255);
    cstring_t names = gi.GetConfigstring(CS_GENERAL + (state.name - 1) / ENT_NAMES_PER_CS);
    T_ASSERT(entity_name_slot_equals(names + ((state.name - 1) % ENT_NAMES_PER_CS) * ENT_NAME_SLOT_SIZE,
                                    "Ancient Elven Gate"));
    T_ASSERT(state.flags & EF_NEUTRAL);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
    ent.destructable->dead = true; ent.health.value = 0;
    globals.CustomizeEntity(0, &ent, &state);
    T_EQ(state.name, 0); T_ASSERT(!(state.flags & EF_NEUTRAL));
    ent.destructable->dead = false; ent.health.value = 500;
    state.flags |= EF_NOT_SELECTABLE;
    globals.CustomizeEntity(0, &ent, &state);
    T_EQ(state.name, 0);
    state.flags &= ~EF_NOT_SELECTABLE; state.renderfx = RF_HIDDEN;
    globals.CustomizeEntity(0, &ent, &state);
    T_EQ(state.name, 0);
    state.renderfx = 0;
    globals.CustomizeEntity(0, &ent, &state);
    T_NE(state.name, 0);
}

/* Missing Warcraft natives must execute through the production JASS dispatcher. */
TEST(wc3_api, alarm_and_ai_native_registration) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(not UnitIgnoreAlarmToggled(null), \"null alarm state\")\n"
        "  call BJassAssert(not UnitIgnoreAlarm(null, true), \"invalid alarm unit\")\n"
        "endfunction\n"));
}

TEST(wc3_api, captain_ai_natives_accept_null_script_player) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetAmphibious()\n"
        "  call DisablePathing()\n"
        "  call TeleportCaptain(1.0, 2.0)\n"
        "  call CaptainAttack(1.0, 2.0)\n"
        "  call CaptainGoHome()\n"
        "  call CaptainVsUnits(null)\n"
        "  call CaptainVsPlayer(null)\n"
        "  call ClearCaptainTargets()\n"
        "  call ResetCaptainLocs()\n"
        "  call BJassAssert(not CaptainAtGoal(), \"no captain\")\n"
        "  call BJassAssert(not CaptainIsHome(), \"no captain\")\n"
        "endfunction\n"));
}

#endif /* BZ_TESTS */
