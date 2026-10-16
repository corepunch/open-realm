#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../../jass/jstate.h"
#include "fixtures/retail_startup_seed_171.h"

extern void reset_entities(void);
extern void setup_test_world(void);
extern void CM_SetupTestWorldBounds(box2_t const *);
extern bool run_test_jass(char const *);

static __typeof__(gi.CvarString) startup_cvar;
static unsigned startup_scene, startup_stamps, startup_boot_visits;
static bool startup_explicit_seed;
static uint32_t startup_stamp(void) {
    startup_stamps++;
    return retail_startup171[startup_scene].setup_seed;
}
static cstring_t startup_setting(cstring_t name,cstring_t fallback) {
    if (!strcmp(name,"wc3_lock_random_seed")) return retail_startup171[startup_scene].locked ? fallback : "0";
    if (startup_explicit_seed && !strcmp(name,"wc3_random_seed")) return "82172056";
    return startup_cvar(name,fallback);
}
/* Inspect a loader yield after owner construction; no simulation is replaced. */
static void startup_loading(void) {
    if (level.vm && !level.scriptsConfigured) {
        T_EQ(level.pathing_random.sum,1768977253u);
        T_EQ(level.pathing_random.index,2822785048u);
        FOR_LOOP(i,BZ_WC3_RANDOM_STREAMS) {
            T_EQ(level.purpose_random[i].sum,0u);T_EQ(level.purpose_random[i].index,0u);
        }
        startup_boot_visits++;
    }
}
static void startup_check_queries(retailStartup171_t const *s) {
    /* Read the public native's stored result without reparsing a decimal
     * expectation through the retail JASS literal conversion algorithm. */
    cstring_t names[]={"seedInt","seedInt2","seedReal"};
    uint32_t expected[]={(uint32_t)s->int1,(uint32_t)s->int2,s->real};
    FOR_LOOP(i,3) {
        jassdict_t *value=level.vm->globals;
        while(value && strcmp(value->key,names[i]))value=value->next;
        T_NOT_NULL(value);
        if(value) {uint32_t word;memcpy(&word,value->value.value,4);T_EQ(word,expected[i]);}
    }
    T_ASSERT(!jass_rterror_pending(level.vm));
}
static edict_t *startup_mover(void) {
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o'))return ent;
    return NULL;
}
TEST(wc3_map_random, actual_load_seeds_before_races_main_and_first_owner) {
    reset_entities();setup_test_world();world.map=NULL;CM_SetupTestWorldBounds(NULL);
    startup_cvar=gi.CvarString;
    __typeof__(gi.Milliseconds) milliseconds=gi.Milliseconds;
    __typeof__(gi.LoadingFrame) loading=gi.LoadingFrame;
    gi.Milliseconds=startup_stamp;gi.CvarString=startup_setting;gi.LoadingFrame=startup_loading;
    startup_stamps=startup_boot_visits=0;
    FOR_LOOP(i,5) {
        startup_scene=i;
        T_ASSERT(globals.LoadMap(i==1 ? "Maps/Test/PathingSeedFixed.w3m" :
            i==2 ? "Maps/Test/PathingSeedTriad.w3m" : "Maps/Test/PathingSeedMixed.w3m"));
        retailStartup171_t const *s=retail_startup171+i;
        T_EQ(startup_stamps,i+1);T_ASSERT(startup_boot_visits>0);
        T_EQ(level.setup.random_seed,s->setup_seed);
        T_EQ((level.setup.map_flags&0x8000u)!=0,s->locked);
        T_EQ(level.pathing_random.sum,s->owner[0]);T_EQ(level.pathing_random.index,s->owner[1]);
        FOR_LOOP(p,12) T_EQ(game.clients[p].ps.race,s->races[p]);
        startup_check_queries(s);
        edict_t *unit=startup_mover();T_NOT_NULL(unit);if(!unit)break;
        T_EQ(unit->current_order_id,G_OrderId("move"));
        float y=unit->s.origin2.y;
        level.time+=10;globals.RunFrame();
        T_EQ(level.pathing_random.sum,s->owner[0]);T_EQ(level.pathing_random.index,s->owner[1]);
        FOR_LOOP(t,20) {level.time+=10;globals.RunFrame();}
        T_ASSERT(unit->s.origin2.y>y);T_EQ(startup_stamps,i+1);
        /* Save an actual live map; load must retain setup identity and current
         * generator position, without stamping or restarting main(). */
        cstring_t path=Test_TempPath("openrealm-startup171.bin");
        uint32_t sum=level.pathing_random.sum,index=level.pathing_random.index,flags=level.setup.map_flags;
        T_ASSERT(WriteGame(path));level.setup.random_seed=0;level.setup.map_flags=0;
        T_ASSERT(ReadGame(path));remove(path);
        T_EQ(level.setup.random_seed,s->setup_seed);T_EQ(level.setup.map_flags,flags);
        T_EQ(level.pathing_random.sum,sum);T_EQ(level.pathing_random.index,index);
        startup_check_queries(s);T_EQ(startup_stamps,i+1);
    }
    G_ReleaseLevel();CM_W3ClearMapData();
    gi.CvarString=startup_cvar;gi.Milliseconds=milliseconds;gi.LoadingFrame=loading;
    setup_test_world();reset_entities();
}
TEST(wc3_map_random, explicit_setup_seed_bypasses_host_clock_and_authored_lock_survives) {
    reset_entities();setup_test_world();world.map=NULL;CM_SetupTestWorldBounds(NULL);
    startup_scene=3;startup_stamps=0;startup_explicit_seed=true;startup_cvar=gi.CvarString;
    __typeof__(gi.Milliseconds) milliseconds=gi.Milliseconds;
    gi.Milliseconds=startup_stamp;gi.CvarString=startup_setting;
    T_ASSERT(globals.LoadMap("Maps/Test/PathingSeedMixed.w3m"));
    T_EQ(startup_stamps,0);T_EQ(level.setup.random_seed,82172056u);
    T_EQ(level.pathing_random.sum,retail_startup171[3].owner[0]);
    T_EQ(level.pathing_random.index,retail_startup171[3].owner[1]);
    startup_check_queries(retail_startup171+3);
    /* This existing fixture enables MAP_LOCK_RANDOM_SEED in config itself.
     * The unlocked local preference must not erase an authored map flag. */
    T_ASSERT(globals.LoadMap("Maps/Test/PathingReload.w3m"));
    T_ASSERT(level.setup.map_flags&0x8000u);T_EQ(startup_stamps,0);
    G_ReleaseLevel();CM_W3ClearMapData();
    gi.CvarString=startup_cvar;gi.Milliseconds=milliseconds;startup_explicit_seed=false;
    setup_test_world();reset_entities();
}
TEST(wc3_map_random, preference_replaces_all_bits_except_retail_flag40) {
    game.clients[0].jass.race_pref=0xffffffefu;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "call SetPlayerRacePreference(Player(0),ConvertRacePref(2))\n"
        "call BJassAssert(not IsPlayerRacePrefSet(Player(0),RACE_PREF_RANDOM),\"random removed\")\n"
        "call BJassAssert(IsPlayerRacePrefSet(Player(0),RACE_PREF_ORC),\"orc installed\")\nendfunction\n"));
    T_EQ(game.clients[0].jass.race_pref,0x42u);
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "call SetPlayerRacePreference(Player(0),ConvertRacePref(128))\nendfunction\n"));
    T_EQ(game.clients[0].jass.race_pref,0xc0u);
    game.clients[0].jass.race_pref=0;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "call SetPlayerRacePreference(Player(0),ConvertRacePref(65))\nendfunction\n"));
    T_EQ(game.clients[0].jass.race_pref,1u);
}
TEST(wc3_map_random, race_resolution_uses_map_player_order_after_local_slot_remap) {
    uint32_t const numbers[4]={3,0,1,2};
    FOR_LOOP(i,12)game.clients[i].jass.race_pref=1;
    FOR_LOOP(i,4)game.clients[i].ps.number=numbers[i];
    G_GetPlayerClientByNumber(0)->jass.race_pref=32;
    G_GetPlayerClientByNumber(3)->jass.race_pref=32;
    level.setup.map_flags=0x8000u;G_InitMapRandom();
    /* The first two captured race words are742823453 and3110162172.
     * Resolve by player number even when the local client swaps reserved slots. */
    T_EQ(G_GetPlayerByNumber(0)->race,1u);
    T_EQ(G_GetPlayerByNumber(3)->race,3u);
    T_EQ(level.pathing_random.sum,3110162172u);
    T_EQ(level.pathing_random.index,616856584u);
    FOR_LOOP(i,game.max_clients)game.clients[i].ps.number=i;
}
#endif
