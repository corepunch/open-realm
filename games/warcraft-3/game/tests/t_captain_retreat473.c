/* GROUP-03.4.7.3 retreat473: a moving six-Footman CaptainAttack roster loses four
 * members far from home. Retail reevaluates home on each removal, sets retreat at
 * two members, reissues the survivors to the authored home point, stops the
 * virtual actor inside range500 and clears retreat on its goal event while the
 * survivors continue. The identical public JASS/AI scripts run here; expected
 * words are retail getter returns and Captain snapshots, never supplied poses. */
#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"
#include "fixtures/retail_retreat473.h"
#include "fixtures/retail_retreat473b.h"
void reset_entities(void);
void setup_test_world(void);
bool run_test_jass(cstring_t);
extern void (*test_preload_marker)(cstring_t);
extern void (*test_unit_position_query)(edict_t *,unsigned,float);

typedef struct {
    cstring_t script;
    uint32_t const (*public)[5];
    uint32_t const (*captain)[17];
    unsigned public_count,captain_count,survivors;
} r473Scene_t;
#define R473_SCENE(script,prefix,survivors) {script,prefix##_public,prefix##_captain,\
    sizeof(prefix##_public)/sizeof(*prefix##_public),sizeof(prefix##_captain)/sizeof(*prefix##_captain),survivors}
static r473Scene_t const r473_scenes[]={R473_SCENE("Scripts\\retreat473.j",retreat473,2),
    R473_SCENE("Scripts\\retreat473b.j",retreat473b,1)};
static r473Scene_t const *r473_scene;
#define R473_PUBLIC (r473_scene->public_count)
#define R473_CAPTAIN (r473_scene->captain_count)
static uint32_t r473_query[2];
static unsigned r473_public_row,r473_captain_row,r473_bad;
static unsigned r473_marker_tick;

static void r473_coordinate(edict_t *unit,unsigned axis,float value) {
    (void)unit;
    if(axis<2)r473_query[axis]=wc3_float_bits(value);
}
static void r473_report(char const *table,unsigned row,unsigned tick,unsigned field,uint32_t expected,uint32_t actual) {
    fprintf(stderr,"retreat473 %s row%u tick%u field%u expected%08x actual%08x\n",table,row,tick,field,expected,actual);
    r473_bad++;
}
/* Attack Captain native snapshot words at label markers. Native6c bit0 is the
 * InitAssault formation flag, kept as captain->full. Native b8/c8 are not
 * engine-owned counters and are not compared; bc/c0/c4 are. */
static void r473_captain(unsigned tick) {
    botCaptain_t const *captain=level.bots[0].captains+BOT_CAPTAIN_ATTACK;
    if(!captain->home_actor)return;
    T_ASSERT(r473_captain_row<R473_CAPTAIN);
    if(r473_captain_row>=R473_CAPTAIN){r473_bad++;return;}
    uint32_t const *expected=r473_scene->captain[r473_captain_row];
    edict_t const *actor=captain->home_actor;
    uint32_t actual[17]={tick,level.pathing_counter,(uint32_t)captain->state,captain->policy_flags|(captain->full ? 1u : 0u),expected[4],
        ARRAY_COUNT(captain->units),(uint32_t)captain->strength_count,captain->entered_members,expected[8],expected[9],
        wc3_float_bits(captain->goal.x),wc3_float_bits(captain->goal.y),wc3_float_bits(captain->request_range),
        wc3_float_bits(actor->movement.fine_pose.x),wc3_float_bits(actor->movement.fine_pose.y),
        /* Engine velocity is world-scaled; retail mover80 is fine (exact /32). */
        wc3_float_bits(wc3_mul(actor->movement.velocity.x,1.0f/32)),wc3_float_bits(wc3_mul(actor->movement.velocity.y,1.0f/32))};
    FOR_LOOP(k,17) {
        T_EQ(actual[k],expected[k]);
        if(actual[k]!=expected[k])r473_report("captain",r473_captain_row,tick,k,expected[k],actual[k]);
    }
    r473_captain_row++;
}
static void r473_marker(cstring_t text) {
    unsigned tick,index,order;
    if(sscanf(text,"R473 tick=%u label=unit u=%u order=%u",&tick,&index,&order)==3) {
        uint32_t row[]={index,tick,order,r473_query[0],r473_query[1]};
        memset(r473_query,0,sizeof(r473_query));
        if(r473_bad)return;
        T_ASSERT(r473_public_row<R473_PUBLIC);
        if(r473_public_row>=R473_PUBLIC){r473_bad++;return;}
        FOR_LOOP(k,5) {
            uint32_t expected=r473_scene->public[r473_public_row][k];T_EQ(row[k],expected);
            if(row[k]!=expected)r473_report("public",r473_public_row,tick,k,expected,row[k]);
        }
        r473_public_row++;
        return;
    }
    if(sscanf(text,"R473 tick=%u label=",&tick)!=1)return;
    r473_marker_tick=tick;
    if(!r473_bad)r473_captain(tick);
}
static void r473_write(pfWriteType_t type,void const *value) {(void)type;(void)value;}
static void r473_unicast(edict_t *unit) {(void)unit;}

/* saved: cold engine save/load at the given public tick, then continue comparing. */
static void r473_run(unsigned scene,unsigned save_tick) {
    r473_scene=r473_scenes+scene;
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    G_BotShutdown();reset_entities();setup_test_world();
    void (*old_write)(pfWriteType_t,void const *)=gi.Write;void (*old_unicast)(edict_t *)=gi.unicast;
    gi.Write=r473_write;gi.unicast=r473_unicast;
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    uint8_t cells[64*64]={0};CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    world.map->width=world.map->height=17;world.map->center=(vec2_t){0,0};
    FOR_LOOP(i,17*17)((war3mapVertex_t *)world.map->vertices)[i]=(war3mapVertex_t){.accurate_height=8192,.waterlevel=8550,.level=2};
    mapInfo_t const *old=level.mapinfo;mapInfo_t info=*old;
    info.players[0].startingPosition=(vec2_t){272,304};game.clients[0].ps.start_location=0;
    /* Stock retail Footman fields absent from the minimal test archive. */
    float radius=31,speed=270,range=90,acquire=500;uint32_t weapons=1;
    unitModification_t mods[]={
        {.modID=MAKEFOURCC('u','c','o','l'),.type=mod_unreal,.data=&radius},
        {.modID=MAKEFOURCC('u','m','v','s'),.type=mod_real,.data=&speed},
        {.modID=MAKEFOURCC('u','a','c','q'),.type=mod_unreal,.data=&acquire},
        {.modID=MAKEFOURCC('u','a','1','r'),.type=mod_unreal,.data=&range},
        {.modID=MAKEFOURCC('u','a','e','n'),.type=mod_int,.data=&weapons}};
    unitData_t original={.originalUnitID=MAKEFOURCC('h','f','o','o'),.numbeOfModifications=5,.modifications=mods};
    info.num_originalUnits=1;info.originalUnits=&original;info.num_userCreatedUnits=0;info.userCreatedUnits=NULL;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_counter=1024;
    G_FowInit();G_FowConnectPlayer(0);G_FowUpdate();game.clients[0].ps.rdflags|=RDF_NOFOG;
    uint32_t length=0;char *probe=gi.ReadFile(r473_scene->script,&length);T_NOT_NULL(probe);
    if(probe) {
        char *script=malloc(length+100);snprintf(script,length+100,"%s\nfunction main takes nothing returns nothing\ncall PathProbeInit()\nendfunction\n",probe);
        T_ASSERT(run_test_jass(script));free(script);gi.MemFree(probe);
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        r473_public_row=r473_captain_row=r473_bad=r473_marker_tick=0;memset(r473_query,0,sizeof(r473_query));
        test_preload_marker=r473_marker;test_unit_position_query=r473_coordinate;
        bool restored=false;
        while(level.time<34000 && !r473_bad && r473_public_row<R473_PUBLIC) {
            level.time+=5;globals.RunFrame();
            if(save_tick && !restored && r473_marker_tick==save_tick) {
                cstring_t file=Test_TempPath("wc3-retreat473.bin");T_ASSERT(WriteGame(file));
                test_preload_marker=NULL;test_unit_position_query=NULL;
                G_BotStop(0);T_ASSERT(ReadGame(file));remove(file);restored=true;
                T_NULL(level.bots[0].vm);
                test_preload_marker=r473_marker;test_unit_position_query=r473_coordinate;
            }
        }
        fprintf(stderr,"retreat473 scene%u save%u public%u/%u captain%u/%u bad%u time%u\n",scene,save_tick,r473_public_row,(unsigned)R473_PUBLIC,
            r473_captain_row,(unsigned)R473_CAPTAIN,r473_bad,level.time);
        T_EQ(r473_public_row,R473_PUBLIC);T_EQ(r473_captain_row,R473_CAPTAIN);T_EQ(r473_bad,0);
        if(save_tick)T_ASSERT(restored);
        if(!r473_bad) {
            player_t *player=&game.clients[0].ps;
            T_ASSERT(!G_BotCaptainRetreating(player));T_EQ(G_BotCaptainGroupSize(player),r473_scene->survivors);
        }
    }
    test_preload_marker=NULL;test_unit_position_query=NULL;
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    G_BotShutdown();G_FowShutdown();reset_entities();G_SetMapUnitOverrides(NULL);
    level.mapinfo=old;gi.Write=old_write;gi.unicast=old_unicast;
}

TEST(wc3_captain_retreat473, moving_roster_removal_retreat_and_home_arrival) {r473_run(0,0);}
/* Mid-retreat: the bit is set, the actor is still travelling home at speed500. */
TEST(wc3_captain_retreat473, saved_mid_retreat_continuation) {r473_run(0,138);}
/* Mid-attack: the moving roster is still on its CaptainAttack request. */
TEST(wc3_captain_retreat473, saved_mid_attack_continuation) {r473_run(0,120);}
/* Retail UI save points. Observed: sl-explore-2 paused after tick142 (bit2 set,
 * actor at speed500), sl-explore-1 after184 (members walking), sl-observe-3 after
 * 224. Observer-free: sl-control-2 after139 (bit2 set), sl-control-1 after241.
 * Every retail load resumed identically to its uninterrupted first pass. */
TEST(wc3_captain_retreat473, saved_at_retail_ui_save_tick139) {r473_run(0,139);}
TEST(wc3_captain_retreat473, saved_at_retail_ui_save_tick142) {r473_run(0,142);}
TEST(wc3_captain_retreat473, saved_at_retail_ui_save_tick184) {r473_run(0,184);}
TEST(wc3_captain_retreat473, saved_at_retail_ui_save_tick224) {r473_run(0,224);}
TEST(wc3_captain_retreat473, saved_at_retail_ui_save_tick241) {r473_run(0,241);}
/* Second removal during retreat: 9d08e0 requests home again; the survivor's
 * d0012 null-target head is retained (assembly8901..890f) while the actor
 * request is republished. */
TEST(wc3_captain_retreat473, removal_during_retreat_retains_member_head) {r473_run(1,0);}
TEST(wc3_captain_retreat473, saved_removal_during_retreat) {r473_run(1,140);}
#endif
