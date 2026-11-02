#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"
#include "fixtures/retail_patrol265_public.h"
#include "fixtures/retail_patrol265_stock.h"
#include "fixtures/retail_patrol265_map.h"
#include "games/warcraft-3/common/war3map_vertices.h"

bool run_test_jass(cstring_t);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(cstring_t);
void free_slk_rows(slkTestData_t *);
void CM_SetupTestPathmap(unsigned,unsigned,uint8_t const *);
void CM_SetupTestWorldBounds(box2_t const *);
extern void (*test_preload_marker)(cstring_t);

typedef struct {
    bool active,complete;
    unsigned scene,tick,public_count,public_mismatches,poll_count;
    patrolNativePoll265_t polls[10000];
} patrolPublic265Run_t;
static patrolPublic265Run_t patrol_public265;

static unsigned PatrolPublic265Role(edict_t const *u) {
    return u && u->user_data>=1 && u->user_data<=2 ? (unsigned)u->user_data-1 : UINT32_MAX;
}
static void PatrolPublic265Record(patrolNativePoll265_t row) {
    if(!patrol_public265.active)return;
    unsigned n=patrol_public265.poll_count++;
    if(n<10000)patrol_public265.polls[n]=row;
    fprintf(stderr,"PATROLPUBLIC265POLL %u %u %u",row.scene,row.phase,row.role);
    FOR_LOOP(i,row.phase==0?10:6)fprintf(stderr," %u",row.words[i]);
    fprintf(stderr,"\n");
}
static void PatrolPublic265Arrival(moveGroup_t const *group,moveGroupMember_t const *member,wc3Arrival_t const *a) {
    (void)group;
    PatrolPublic265Record((patrolNativePoll265_t){patrol_public265.scene,0,PatrolPublic265Role(member->unit),{
        wc3_float_bits(level.pathing_clock.time),wc3_float_bits(a->source[0]),wc3_float_bits(a->source[1]),
        wc3_float_bits(a->target[0]),wc3_float_bits(a->target[1]),wc3_float_bits(a->heading),
        wc3_float_bits(a->range),wc3_float_bits(a->error),member->in_range,member->arrived}});
}
static void PatrolPublic265Committed(moveGroup_t const *group,moveGroupMember_t const *member) {
    (void)group;edict_t const *u=member->unit;
    PatrolPublic265Record((patrolNativePoll265_t){patrol_public265.scene,2,PatrolPublic265Role(u),{
        wc3_float_bits(level.pathing_clock.time),wc3_float_bits(u->movement.fine_pose.x),wc3_float_bits(u->movement.fine_pose.y),
        wc3_float_bits(wc3_mul(u->movement.velocity.x,1.0f/32)),wc3_float_bits(wc3_mul(u->movement.velocity.y,1.0f/32)),wc3_float_bits(u->s.angle)}});
}
static void PatrolPublic265Marker(cstring_t text) {
    if(strncmp(text,"PTR ",4) || patrol_public265.complete)return;
    unsigned tick,scene;
    if(sscanf(text,"PTR tick=%u scene=%u",&tick,&scene)!=2)return;
    patrol_public265.tick=tick;patrol_public265.scene=scene;
    if(strstr(text," label=start"))patrol_public265.active=true;
    char normalized[256];snprintf(normalized,sizeof(normalized),"%s",text);
    /* Explicit typed FourCC encoding conversion only; all real values retain
     * the literal public R2S output with its native three-decimal formatting. */
    char *p=strstr(normalized," type=");
    if(p) {
        p+=6;char *end;uint32_t v=(uint32_t)strtoul(p,&end,10);
        uint32_t native=((v&255u)<<24)|((v&0xff00u)<<8)|((v>>8)&0xff00u)|(v>>24);
        char value[32];snprintf(value,sizeof(value),"%u",native);size_t n=strlen(value);
        memmove(p+n,end,strlen(end)+1);memcpy(p,value,n);
    }
    unsigned i=patrol_public265.public_count++;
    unsigned count=sizeof(patrol_native_public265)/sizeof(patrol_native_public265[0]);
    bool equal=i<count && !strcmp(normalized,patrol_native_public265[i]);
    if(!equal) {
        if(!patrol_public265.public_mismatches)fprintf(stderr,"PATROLPUBLIC265FIRST marker=%u\n actual %s\n native %s\n",i,normalized,i<count?patrol_native_public265[i]:"<extra>");
        patrol_public265.public_mismatches++;
    }
    if(strstr(text," label=complete")){patrol_public265.complete=true;patrol_public265.active=false;}
}
static void PatrolPublic265Check(void) {
    T_EQ(patrol_public265.public_count,1084);T_EQ(patrol_public265.public_mismatches,0);
    unsigned count=sizeof(patrol_native_polls265)/sizeof(patrol_native_polls265[0]);
    T_EQ(count,1632);T_EQ(patrol_public265.poll_count,count);
    unsigned differences=0;
    FOR_LOOP(i,MIN(MIN(patrol_public265.poll_count,10000),count)) {
        patrolNativePoll265_t const *a=&patrol_public265.polls[i],*b=&patrol_native_polls265[i];
        unsigned words=b->phase==0?10:6;
        bool equal=a->scene==b->scene && a->phase==b->phase && a->role==b->role && !memcmp(a->words,b->words,words*sizeof(uint32_t));
        if(!equal && !differences) {
            fprintf(stderr,"PATROLPUBLIC265FIRST poll=%u actual=%u/%u/%u native=%u/%u/%u\n",i,a->scene,a->phase,a->role,b->scene,b->phase,b->role);
            FOR_LOOP(k,words)fprintf(stderr,"PATROLPUBLIC265WORD %u %08x %08x\n",k,a->words[k],b->words[k]);
        }
        if(!equal)differences++;
    }
    fprintf(stderr,"PATROLPUBLIC265COUNTS public=%u publicdiff=%u polls=%u polldiff=%u\n",patrol_public265.public_count,patrol_public265.public_mismatches,patrol_public265.poll_count,differences);
    T_EQ(differences,0);
}
static void PatrolPublic265Step(void){level.time+=5;globals.RunFrame();}
static void PatrolPublic265Run(bool saved) {
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    reset_entities();setup_test_world();memset(&patrol_public265,0,sizeof(patrol_public265));
    cstring_t names[]={"UnitData","UnitBalance","UnitWeapons","UnitUI","UnitAbilities"};
    cstring_t values[]={patrol265_UnitData,patrol265_UnitBalance,patrol265_UnitWeapons,patrol265_UnitUI,patrol265_UnitAbilities};
    slkTestData_t *rows[5],*old[5];
    FOR_LOOP(i,5){rows[i]=parse_slk_string(values[i]);old[i]=G_SetSLKRows(names[i],rows[i]);}
    mapInfo_t const *previous=level.mapinfo;mapInfo_t info=*previous;int32_t zero=0;
    unitModification_t mod={.modID=MAKEFOURCC('u','m','v','s'),.type=mod_int,.data=&zero};
    unitData_t alias={.originalUnitID=MAKEFOURCC('h','f','o','o'),.newUnitID=MAKEFOURCC('P','z','0','0'),.numbeOfModifications=1,.modifications=&mod};
    info.num_userCreatedUnits=1;info.userCreatedUnits=&alias;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    /* The captured map retains its full-height x20..23 wall; dimensions
     * alone do not establish a flat pathmap. Decode its actual terrain too. */
    CM_SetupTestPathmap(64,64,patrol_native_wpm265);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    war3map_t terrain=*world.map;war3mapVertex_t vertices[289];
    terrain.width=terrain.height=17;terrain.vertices=vertices;world.map=&terrain;
    FOR_LOOP(i,289) {
        uint16_t const *v=patrol_native_terrain265[i];
        uint8_t bytes[]={v[0]&255,v[0]>>8,v[1]&255,v[1]>>8,v[2],v[3],v[4]};
        W3E_DecodeVertex(bytes,vertices+i);
    }
    float min=game.constants.minUnitSpeed,max=game.constants.maxUnitSpeed,follow=game.constants.followRange;
    game.constants.minUnitSpeed=150;game.constants.maxUnitSpeed=400;game.constants.followRange=300;
    level.waypoints=(typeof(level.waypoints)){0};level.pathing_clock=(wc3Clock_t){0,0,300};
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    /*279 observes this exact map's stored setup seed and all45 purpose states. */
    level.setup.random_seed=0x77617233u;
    G_InitMapRandom();
    T_EQ(level.purpose_random[2].sum,3292812501u);T_EQ(level.purpose_random[2].index,1419794628u);
    T_EQ(level.purpose_random[9].sum,2863058991u);T_EQ(level.purpose_random[9].index,2362214472u);
    move_test_group_arrival=PatrolPublic265Arrival;move_test_group_committed=PatrolPublic265Committed;test_preload_marker=PatrolPublic265Marker;
    static char script[sizeof(patrol_native_probe265)+128];
    snprintf(script,sizeof(script),"%sfunction main takes nothing returns nothing\n call PathProbeInit()\nendfunction\n",patrol_native_probe265);
    T_ASSERT(run_test_jass(script));G_FinishMovePathingInitialization();level.started=level.scriptsConfigured=level.scriptsStarted=true;
    unsigned steps=0,saved_steps=0;patrolPublic265Run_t *checkpoint=NULL;
    cstring_t path=Test_TempPath("patrol-public265-saved.bin");
    while(!patrol_public265.complete && steps<10200) {
        PatrolPublic265Step();steps++;
        if(saved && !checkpoint && patrol_public265.tick>=220) {
            checkpoint=malloc(sizeof(*checkpoint));T_NOT_NULL(checkpoint);
            if(checkpoint){*checkpoint=patrol_public265;saved_steps=steps;T_ASSERT(WriteGame(path));}
        }
    }
    T_ASSERT(patrol_public265.complete);T_ASSERT(!jass_rterror_pending(level.vm));PatrolPublic265Check();
    if(checkpoint) {
        unsigned end=steps;patrolPublic265Run_t *completed=malloc(sizeof(*completed));T_NOT_NULL(completed);if(completed)*completed=patrol_public265;
        S_ClearMoveGroups();bool loaded=ReadGame(path);T_ASSERT(loaded);
        if(loaded) {
            patrol_public265=*checkpoint;
            for(steps=saved_steps;steps<end;steps++)PatrolPublic265Step();
            T_ASSERT(patrol_public265.complete);T_ASSERT(!jass_rterror_pending(level.vm));PatrolPublic265Check();
            if(completed){T_EQ(patrol_public265.poll_count,completed->poll_count);T_EQ(patrol_public265.public_count,completed->public_count);T_EQ(patrol_public265.public_mismatches,completed->public_mismatches);
                if(patrol_public265.poll_count==completed->poll_count && completed->poll_count<=10000)T_ASSERT(!memcmp(patrol_public265.polls,completed->polls,completed->poll_count*sizeof(completed->polls[0])));}
        }
        free(completed);free(checkpoint);remove(path);
    }
    move_test_group_arrival=NULL;move_test_group_committed=NULL;test_preload_marker=NULL;
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    level.started=false;reset_entities();G_SetMapUnitOverrides(NULL);level.mapinfo=previous;
    FOR_LOOP(i,5){G_SetSLKRows(names[i],old[i]);free_slk_rows(rows[i]);}
    game.constants.minUnitSpeed=min;game.constants.maxUnitSpeed=max;game.constants.followRange=follow;setup_test_world();
}
TEST(wc3_patrol_public, complete_native265_public_and_polls){PatrolPublic265Run(false);}
TEST(wc3_patrol_public, path_cleared_saved_native265_suffix){PatrolPublic265Run(true);}
#endif
#ifdef BZ_TESTS
static unsigned patrol_guard_polls;
static void PatrolGuardArrival(moveGroup_t const *group,moveGroupMember_t const *member,wc3Arrival_t const *a) {
    if(member->unit->movement.guard_state!=GUARD_RETURNING)return;
    patrol_guard_polls++;
    T_EQ(wc3_float_bits(a->range),wc3_float_bits(50.0f/32));
    /* The fine route may publish an intermediate waypoint. The retained
     * destination, unlike that next steering point, is the captured home. */
    T_EQ(group->goal.x,16.0f);T_EQ(group->goal.y,16.0f);
}
TEST(wc3_patrol_public, idle_combat_private_home_return_survives_cold_save) {
    reset_entities();setup_test_world();
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    uint32_t seed=level.setup.random_seed;level.setup.random_seed=0x77617233u;G_InitMapRandom();
    cstring_t names[]={"UnitData","UnitBalance","UnitWeapons","UnitUI","UnitAbilities"};
    cstring_t values[]={patrol265_UnitData,patrol265_UnitBalance,patrol265_UnitWeapons,patrol265_UnitUI,patrol265_UnitAbilities};
    slkTestData_t *rows[5],*old[5];
    FOR_LOOP(i,5){rows[i]=parse_slk_string(values[i]);old[i]=G_SetSLKRows(names[i],rows[i]);}
    T_ASSERT(run_test_jass(
        "globals\nunit homeSource=null\nunit homeEnemy=null\ninteger homeOrders=0\nendglobals\n"
        "function onPoint takes nothing returns nothing\nset homeOrders=homeOrders+1\nendfunction\n"
        "function kill takes nothing returns nothing\ncall KillUnit(homeEnemy)\nendfunction\n"
        "function check takes nothing returns nothing\ncall BJassAssert(homeOrders==0,\"return has no public point order\")\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "set homeSource=CreateUnit(Player(0),'hfoo',512.0,512.0,0.0)\n"
        "set homeEnemy=CreateUnit(Player(1),'hfoo',900.0,512.0,180.0)\n"
        "call PauseUnit(homeEnemy,true)\n"
        "call TriggerRegisterUnitEvent(t,homeSource,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call TriggerAddAction(t,function onPoint)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(u,u->inuse && u->s.player==0 && u->class_id==MAKEFOURCC('h','f','o','o')){unit=u;break;}
    T_NOT_NULL(unit);level.started=level.scriptsConfigured=level.scriptsStarted=true;
    if(unit) {
        unsigned index=unit-g_edicts;
        T_EQ(unit->movement.guard_state,GUARD_NONE);
        FOR_LOOP(i,1000){PatrolPublic265Step();if(unit->movement.guard_state==GUARD_COMBAT && unit->s.origin2.x>612)break;}
        T_EQ(unit->movement.guard_state,GUARD_COMBAT);T_ASSERT(unit->s.origin2.x>612);
        T_EQ(unit->movement.guard_position.x,512);T_EQ(unit->movement.guard_position.y,512);
        jass_callbyname(level.vm,"kill",false);T_ASSERT(!jass_rterror_pending(level.vm));
        FOR_LOOP(i,400){if(unit->movement.guard_state==GUARD_RETURNING)break;PatrolPublic265Step();}
        T_EQ(unit->movement.guard_state,GUARD_RETURNING);T_EQ(unit->current_order_id,0);
        T_ASSERT(unit->movement.group_id!=0);
        cstring_t save=Test_TempPath("patrol-idle-home.bin");T_ASSERT(WriteGame(save));
        FOR_LOOP(replay,2) {
            if(replay){S_ClearMoveGroups();T_ASSERT(ReadGame(save));unit=g_edicts+index;}
            patrol_guard_polls=0;move_test_group_arrival=PatrolGuardArrival;
            FOR_LOOP(i,1000){PatrolPublic265Step();if(unit->movement.guard_state==GUARD_IDLE)break;}
            move_test_group_arrival=NULL;
            T_ASSERT(patrol_guard_polls>0);T_EQ(unit->movement.guard_state,GUARD_IDLE);
            T_EQ(unit->current_order_id,0);T_ASSERT(unit->currentmove && unit->currentmove->think==ai_stand);
            T_ASSERT(Vector2_distance(&unit->s.origin2,&unit->movement.guard_position)<=50);
            jass_callbyname(level.vm,"check",false);T_ASSERT(!jass_rterror_pending(level.vm));
        }
        remove(save);
    }
    move_test_group_arrival=NULL;level.started=false;reset_entities();setup_test_world();
    level.setup.random_seed=seed;
    FOR_LOOP(i,5){G_SetSLKRows(names[i],old[i]);free_slk_rows(rows[i]);}
}
#endif
