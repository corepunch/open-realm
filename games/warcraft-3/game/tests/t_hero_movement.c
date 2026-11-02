#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"

void setup_test_world(void);
void reset_entities(void);
bool run_test_jass(cstring_t);
slkTestData_t *parse_slk_string(cstring_t);
void free_slk_rows(slkTestData_t *);

static char const hero225_script[] =
    "globals\n unit h=null\n unit n=null\n item i=null\nendglobals\n"
    "function main takes nothing returns nothing\n"
    " set h=CreateUnit(Player(0),'Hpal',128,128,0)\n"
    " set n=CreateUnit(Player(0),'hfoo',128,512,0)\nendfunction\n"
    "function level3 takes nothing returns nothing\n call SetHeroLevel(h,3,false)\nendfunction\n"
    "function agi77 takes nothing returns nothing\n call SetHeroAgi(h,77,true)\nendfunction\n"
    "function item takes nothing returns nothing\n set i=UnitAddItemById(h,'belv')\nendfunction\n"
    "function speed takes nothing returns nothing\n call SetUnitMoveSpeed(h,200)\nendfunction\n"
    "function agi51 takes nothing returns nothing\n call SetHeroAgi(h,51,false)\nendfunction\n"
    "function drop takes nothing returns nothing\n call UnitRemoveItem(h,i)\n call RemoveItem(i)\nendfunction\n"
    "function level4 takes nothing returns nothing\n call SetHeroLevel(h,4,false)\nendfunction\n"
    "function strip takes nothing returns nothing\n call UnitStripHeroLevel(h,2)\nendfunction\n"
    "function move takes nothing returns nothing\n call IssuePointOrder(h,\"move\",600,128)\nendfunction\n"
    "function stop takes nothing returns nothing\n call IssueImmediateOrder(h,\"stop\")\nendfunction\n"
    "function cache takes nothing returns nothing\n"
    " local gamecache c=InitGameCache(\"move225-memory-only.w3v\")\n"
    " local unit restored=null\n"
    " call FlushGameCache(c)\n"
    " call BJassAssert(StoreUnit(c,\"probe\",\"hero\",h),\"store Hero\")\n"
    " set restored=RestoreUnit(c,\"probe\",\"hero\",Player(0),160,192,0)\n"
    " call BJassAssert(GetHeroAgi(restored,true)==83,\"cache retains total agility\")\n"
    " call BJassAssert(GetHeroAgi(restored,false)==77,\"cache retains base agility\")\n"
    " call BJassAssert(GetUnitDefaultMoveSpeed(restored)==373.75,\"cache rebuilds speed contribution once\")\n"
    " call RemoveUnit(restored)\nendfunction\n"
    "function base_item takes nothing returns nothing\n"
    " call BJassAssert(GetHeroAgi(h,false)==77,\"retail base agility excludes item\")\n"
    " call BJassAssert(GetHeroAgi(h,true)==83,\"retail total agility includes item\")\nendfunction\n"
    "function base_set takes nothing returns nothing\n"
    " call BJassAssert(GetHeroAgi(h,false)==51,\"retail setter changes base agility\")\n"
    " call BJassAssert(GetHeroAgi(h,true)==57,\"retail setter retains item agility\")\nendfunction\n";

static edict_t *hero225_find(uint32_t code) {
    FILTER_EDICTS(unit,unit->inuse && unit->class_id==code) return unit;
    return NULL;
}

static void hero225_call(cstring_t function) {
    jass_callbyname(level.vm,function,false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* Frozen native M225 public stages: default speed, effective speed, total AGI.
 * Both runtime repeats and the observer-free control agree for each map. */
TEST(wc3_hero_movement, move225_agility_delta_survives_speed_override_and_level_changes) {
    char const abilities[] =
        "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\nC;Y2;X4;K\"1\"\n"
        "C;Y3;X1;K\"AIa6\"\nC;Y3;X2;K\"AIab\"\nC;Y3;X3;K\"6\"\nE\n";
    char const items[] =
        "ID;PWXL;N;E\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"droppable\"\nC;Y1;X4;K\"file\"\nC;Y2;X1;K\"belv\"\n"
        "C;Y2;X2;K\"AIa6\"\nC;Y2;X3;K\"true\"\nC;Y2;X4;K\"TestUI/Models/anim_pulse.mdx\"\nE\n";
    cstring_t const functions[]={NULL,"level3","agi77","item","speed","agi51","drop","level4","strip"};
    /* Raw return words frozen from native custom and fractional repeats.
     * Fractional default and mutable base deliberately differ. */
    uint32_t const defaults[][9]={
        {0x438f2000,0x43910000,0x43b72000,0x43bae000,0x43bae000,0x43aaa000,0x43a6e000,0x43a78000,0x43a5a000},
        {0x438d2ccc,0x438e9999,0x43ab9333,0x43ae6ccc,0x43ae6ccc,0x43a21333,0x439f3999,0x439fb333,0x439e4666}};
    uint32_t const speeds[][9]={
        {0x438f2000,0x43910000,0x43b72000,0x43bae000,0x43480000,0x43278000,0x43200000,0x43214000,0x431d8000},
        {0x438d2ccc,0x438e9998,0x43ab9331,0x43ae6cca,0x43480000,0x432f4ccd,0x43299999,0x432a8ccc,0x4327b331}};
    uint32_t const agility[]={13,16,77,83,83,57,51,52,49};
    float const saved_coefficient=game.constants.agiMoveBonus;
    slkTestData_t *a=parse_slk_string(abilities),*a_old=G_SetSLKRows("AbilityData",a);
    slkTestData_t *i=parse_slk_string(items),*i_old=G_SetSLKRows("ItemData",i);
    FOR_LOOP(profile,2) {
        reset_entities();setup_test_world();game.constants.agiMoveBonus=profile ? wc3_decimal("0.95") : 1.25f;
        T_ASSERT(run_test_jass(hero225_script));
        edict_t *hero=hero225_find(MAKEFOURCC('H','p','a','l'));
        edict_t *nonhero=hero225_find(MAKEFOURCC('h','f','o','o'));
        T_NOT_NULL(hero);T_NOT_NULL(nonhero);
        if(hero && nonhero) FOR_LOOP(n,9) {
            if(functions[n])hero225_call(functions[n]);
            if(n==3) {
                hero225_call("base_item");
                if (!profile) hero225_call("cache");
                uint32_t const number=hero->s.number;
                cstring_t file=Test_TempPath("hero-move225-item.bin");
                T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));hero=g_edicts+number;
                T_EQ(hero->hero_item_agility,6);hero225_call("base_item");remove(file);
            }
            if(n==5)hero225_call("base_set");
            fprintf(stderr,"M225 engine profile=%u stage=%u agi=%u item=%d "
                "default=%08x current=%08x cached=%08x\n",(unsigned)profile,(unsigned)n,
                hero->hero.agi,hero->hero_item_agility,wc3_float_bits(S_UnitDefaultMoveSpeed(hero)),
                wc3_float_bits(S_UnitMoveSpeed(hero)),wc3_float_bits(hero->hero_move_bonus));
            T_EQ(wc3_float_bits(S_UnitDefaultMoveSpeed(hero)),defaults[profile][n]);
            T_EQ(wc3_float_bits(S_UnitMoveSpeed(hero)),speeds[profile][n]);
            T_EQ(hero->hero.agi,agility[n]);
            T_EQ(S_UnitDefaultMoveSpeed(nonhero),270);
        }
    }
    game.constants.agiMoveBonus=saved_coefficient;
    G_SetSLKRows("ItemData",i_old);free_slk_rows(i);
    G_SetSLKRows("AbilityData",a_old);free_slk_rows(a);
}

TEST(wc3_hero_movement, move225_default_query_and_active_motion_keep_distinct_speeds_after_save) {
    float const saved=game.constants.agiMoveBonus;
    cstring_t file=Test_TempPath("hero-move225.bin");
    setup_test_world();game.constants.agiMoveBonus=1.25f;
    T_ASSERT(run_test_jass(hero225_script));
    edict_t *hero=hero225_find(MAKEFOURCC('H','p','a','l'));
    T_NOT_NULL(hero);
    if(hero) {
        hero225_call("level3");hero225_call("agi77");hero225_call("speed");
        hero225_call("agi51");hero225_call("move");
        uint32_t const number=hero->s.number;
        T_EQ(S_UnitDefaultMoveSpeed(hero),333.75f);
        T_EQ(S_UnitMoveSpeed(hero),167.5f);
        T_EQ(hero->current_order_id,G_OrderId("move"));
        T_ASSERT(WriteGame(file));
        float const start=hero->s.origin2.x;
        G_BeginEntityFrame();level.scheduled_frame=true;
        FOR_LOOP(n,60) {
            wc3_clock_advance(&level.pathing_clock,10.0f/FRAMETIME,0);
            M_RunScheduledThinks();
        }
        G_RunEntities();level.scheduled_frame=false;
        T_ASSERT(hero->s.origin2.x>start);
        T_ASSERT(ReadGame(file));hero=g_edicts+number;
        T_EQ(S_UnitDefaultMoveSpeed(hero),333.75f);
        T_EQ(S_UnitMoveSpeed(hero),167.5f);
        hero225_call("level4");
        T_EQ(S_UnitDefaultMoveSpeed(hero),335);
        T_EQ(S_UnitMoveSpeed(hero),168.75f);
        T_EQ(hero->current_order_id,G_OrderId("move"));
        hero225_call("stop");T_EQ(hero->current_order_id,0);
        T_EQ(S_UnitMoveSpeed(hero),168.75f);
    }
    remove(file);game.constants.agiMoveBonus=saved;
}

TEST(wc3_hero_movement, move225_stock_zero_coefficient_preserves_speed) {
    float const saved=game.constants.agiMoveBonus;
    setup_test_world();game.constants.agiMoveBonus=0;
    T_ASSERT(run_test_jass(hero225_script));
    edict_t *hero=hero225_find(MAKEFOURCC('H','p','a','l'));
    T_NOT_NULL(hero);
    if(hero) {
        hero225_call("level3");hero225_call("agi77");hero225_call("speed");hero225_call("agi51");
        T_EQ(S_UnitDefaultMoveSpeed(hero),270);T_EQ(S_UnitMoveSpeed(hero),200);
    }
    game.constants.agiMoveBonus=saved;
}
#endif
