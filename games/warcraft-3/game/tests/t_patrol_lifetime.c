#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"
#include "fixtures/retail_patrol_hfoo_profile.h"
#include "fixtures/retail_patrol_hfoo_weapons.h"
#include "fixtures/retail_patrol_lifetime_polls.h"
#include "fixtures/retail_patrol_startup272_acquisition.h"

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
    unsigned tick,count;
    unsigned actor[7];uint32_t spawn[7];
    patrolLifetimeNativeRow_t rows[16000];
    bool timers_active;
    unsigned timer_count,attachments;
    unsigned timer_actor[64];uint32_t timer_spawn[64];
    patrol272Acquisition_t timers[2048];
} patrolLifetimeRun_t;
static patrolLifetimeRun_t patrol_lifetime_run;

static void PatrolLifetimeAcquisition(edict_t const *unit,bool attachment,float duration) {
    if(!patrol_lifetime_run.timers_active)return;
    unsigned role=UINT32_MAX,index=(unsigned)(unit-g_edicts);
    if(attachment) {
        role=patrol_lifetime_run.attachments++;
        if(role<64){patrol_lifetime_run.timer_actor[role]=index;patrol_lifetime_run.timer_spawn[role]=unit->spawn_time;}
    } else {
        FOR_LOOP(i,MIN(patrol_lifetime_run.attachments,64))
            if(patrol_lifetime_run.timer_actor[i]==index && patrol_lifetime_run.timer_spawn[i]==unit->spawn_time)role=i;
    }
    /* Native observer records the primary domain, not the timer domain whose
     * popped deadline0522e0 exposes during this callback. Keep both semantics:
     * scheduling still uses G_TimerQueryClock; this sampled field is primary. */
    patrol272Acquisition_t row={attachment,role,wc3_float_bits(level.pathing_clock.time),
        wc3_float_bits(duration),unit->attack_scan_initial_pending};
    unsigned i=patrol_lifetime_run.timer_count++;
    if(i<2048)patrol_lifetime_run.timers[i]=row;
    fprintf(stderr,"PATROLLIFETIMER %u %u %u %u %u\n",row.phase,row.actor,row.clock,row.duration,row.initial_pending);
}

static unsigned PatrolLifetimeRole(edict_t const *unit) {
    FOR_LOOP(i,7) if(patrol_lifetime_run.actor[i]==(unsigned)(unit-g_edicts) &&
        patrol_lifetime_run.spawn[i]==unit->spawn_time)return i;
    return UINT32_MAX;
}
static void PatrolLifetimeRecord(patrolLifetimeNativeRow_t row) {
    if(!patrol_lifetime_run.active)return;
    unsigned i=patrol_lifetime_run.count++;
    if(i<16000)patrol_lifetime_run.rows[i]=row;
    fprintf(stderr,"PATROLLIFEPOLL %u %u",row.phase,row.role);
    FOR_LOOP(k,row.phase==0?10:6)fprintf(stderr," %u",row.words[k]);
    fprintf(stderr,"\n");
}
static void PatrolLifetimeArrival(moveGroup_t const *group,moveGroupMember_t const *member,wc3Arrival_t const *a) {
    (void)group;
    PatrolLifetimeRecord((patrolLifetimeNativeRow_t){0,PatrolLifetimeRole(member->unit),{
        wc3_float_bits(level.pathing_clock.time),wc3_float_bits(a->source[0]),wc3_float_bits(a->source[1]),
        wc3_float_bits(a->target[0]),wc3_float_bits(a->target[1]),wc3_float_bits(a->heading),
        wc3_float_bits(a->range),wc3_float_bits(a->error),member->in_range,member->arrived}});
}
static void PatrolLifetimeCommitted(moveGroup_t const *group,moveGroupMember_t const *member) {
    (void)group;edict_t const *u=member->unit;
    PatrolLifetimeRecord((patrolLifetimeNativeRow_t){2,PatrolLifetimeRole(u),{
        wc3_float_bits(level.pathing_clock.time),wc3_float_bits(u->movement.fine_pose.x),wc3_float_bits(u->movement.fine_pose.y),
        wc3_float_bits(wc3_mul(u->movement.velocity.x,1.0f/32)),wc3_float_bits(wc3_mul(u->movement.velocity.y,1.0f/32)),
        wc3_float_bits(u->s.angle)}});
}
static void PatrolLifetimeMarker(cstring_t text) {
    unsigned row,tick,role;char label[32];
    if(sscanf(text,"PATHMETA patrol_lifetime row=%u tick=%u case=%u label=%31s",&row,&tick,&role,label)==4) {
        patrol_lifetime_run.tick=tick;
        if(role<7 && !strcmp(label,"initial")) {
            FILTER_EDICTS(u,u->inuse && u->class_id==MAKEFOURCC('h','f','o','o') &&
                u->s.player==0 && u->s.origin2.x==272 && u->s.origin2.y==256+256*role) {
                patrol_lifetime_run.actor[role]=(unsigned)(u-g_edicts);
                patrol_lifetime_run.spawn[role]=u->spawn_time;
            }
            T_ASSERT(patrol_lifetime_run.actor[role]!=0);
        }
    }
    if(!strcmp(text,"PATHTRACE tick=0 label=start_patrol_lifetime"))patrol_lifetime_run.active=true;
    if(!strcmp(text,"PATHMETA complete")) {
        patrol_lifetime_run.complete=true;patrol_lifetime_run.active=false;patrol_lifetime_run.timers_active=false;
    }
}
static void PatrolLifetimeCheck(void) {
    unsigned count=sizeof(patrol_lifetime_native)/sizeof(patrol_lifetime_native[0]);
    T_EQ(count,7124);T_EQ(patrol_lifetime_run.count,count);
    unsigned first=UINT32_MAX,mismatched=0;
    FOR_LOOP(i,MIN(MIN(patrol_lifetime_run.count,16000),count)) {
        patrolLifetimeNativeRow_t const *a=&patrol_lifetime_run.rows[i],*b=&patrol_lifetime_native[i];
        unsigned n=b->phase==0?10:6;
        bool same=a->phase==b->phase && a->role==b->role && !memcmp(a->words,b->words,n*sizeof(uint32_t));
        if(!same)mismatched++;
        if(!same && first==UINT32_MAX) {
            first=i;fprintf(stderr,"PATROLLIFEFIRST row=%u actual=%u/%u expected=%u/%u\n",i,a->phase,a->role,b->phase,b->role);
            FOR_LOOP(k,n)fprintf(stderr,"PATROLLIFEWORD field=%u actual=%08x expected=%08x\n",k,a->words[k],b->words[k]);
        }
    }
    /* Scan every row; avoid thousands of shifted assertion cascades. */
    fprintf(stderr,"PATROLLIFECOUNTS actual=%u expected=%u mismatched=%u\n",patrol_lifetime_run.count,count,mismatched);
    T_EQ(mismatched,0);
    unsigned timer_count=sizeof(patrol272_acquisition)/sizeof(patrol272_acquisition[0]);
    T_EQ(timer_count,356u);T_EQ(patrol_lifetime_run.timer_count,timer_count);
    unsigned timer_mismatches=0;
    FOR_LOOP(i,MIN(MIN(patrol_lifetime_run.timer_count,2048),timer_count)) {
        patrol272Acquisition_t const *a=patrol_lifetime_run.timers+i,*b=patrol272_acquisition+i;
        if(memcmp(a,b,sizeof(*a))) {
            if(!timer_mismatches)fprintf(stderr,"PATROLLIFETIMERFIRST row=%u actual=%u/%u/%08x/%08x/%u expected=%u/%u/%08x/%08x/%u\n",
                i,a->phase,a->actor,a->clock,a->duration,a->initial_pending,b->phase,b->actor,b->clock,b->duration,b->initial_pending);
            timer_mismatches++;
        }
    }
    T_EQ(timer_mismatches,0u);
}
static void PatrolLifetimeStep(void) {level.time+=5;globals.RunFrame();}
static void PatrolLifetimeRun(bool replay) {
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    reset_entities();setup_test_world();memset(&patrol_lifetime_run,0,sizeof(patrol_lifetime_run));
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    cstring_t names[]={"UnitData","UnitBalance","UnitWeapons"};
    cstring_t texts[]={tft?patrol_tft_UnitData:patrol_roc_UnitData,tft?patrol_tft_UnitBalance:patrol_roc_UnitBalance,
        tft?patrol_tft_UnitWeapons:patrol_roc_UnitWeapons};
    slkTestData_t *rows[3],*old[3];
    FOR_LOOP(i,3){rows[i]=parse_slk_string(texts[i]);old[i]=G_SetSLKRows(names[i],rows[i]);}
    uint8_t cells[64*64]={0};CM_SetupTestPathmap(64,64,cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    level.waypoints=(typeof(level.waypoints)){0};level.pathing_clock=(wc3Clock_t){0,0,300};
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    /* Controlled124c measured693710 seed and stream2 before the first
     * attachment; its complete7124 polls also equal the older124b repeats.
     * Initialize through the production map path, never per-unit seeds. */
    uint32_t old_flags=level.setup.map_flags,old_seed=level.setup.random_seed;
    level.setup.map_flags=0;level.setup.random_seed=patrol272_seed;
    G_InitMapRandom();
    T_EQ(level.purpose_random[2].sum,patrol272_stream2_initial[0]);
    T_EQ(level.purpose_random[2].index,patrol272_stream2_initial[1]);
    move_test_group_arrival=PatrolLifetimeArrival;move_test_group_committed=PatrolLifetimeCommitted;
    test_preload_marker=PatrolLifetimeMarker;
    patrol_lifetime_run.timers_active=true;attack_test_acquire=PatrolLifetimeAcquisition;
    static char script[sizeof(patrol_lifetime_probe)+128];
    snprintf(script,sizeof(script),"%sfunction main takes nothing returns nothing\n call PathProbeInit()\nendfunction\n",patrol_lifetime_probe);
    T_ASSERT(run_test_jass(script));G_FinishMovePathingInitialization();
    static uint32_t const first_deadlines[]={1028668480u,1050413242u,1050920322u,
        1034729336u,1047457436u,1055191020u,1041991308u};
    FOR_LOOP(i,7) {
        edict_t const *unit=g_edicts+patrol_lifetime_run.actor[i];
        T_ASSERT(unit->attack_scan.active);
        T_EQ(wc3_float_bits(unit->attack_scan.deadline.time),first_deadlines[i]);
    }
    /* The probe also creates and kills an eighth Footman before starting
     * its timer. That attachment consumes its draw despite immediate death. */
    T_EQ(level.purpose_random[2].sum,3695680528u);
    T_EQ(level.purpose_random[2].index,876643544u);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    unsigned steps=0,saved_steps=0;patrolLifetimeRun_t *checkpoint=NULL;
    cstring_t save=Test_TempPath("patrol-lifetime-full-diagnostic.bin");
    while(!patrol_lifetime_run.complete && steps<5200) {
        PatrolLifetimeStep();steps++;
        if(replay && !checkpoint && patrol_lifetime_run.tick>=11) {
            /* Tick10 creates the actual damage subscriptions. Save after
             * their registry slots exist; do not roll the warm VM back before
             * registrations retained by the load contract. */
            checkpoint=malloc(sizeof(*checkpoint));T_NOT_NULL(checkpoint);
            if(checkpoint){*checkpoint=patrol_lifetime_run;saved_steps=steps;T_ASSERT(WriteGame(save));}
        }
    }
    T_ASSERT(patrol_lifetime_run.complete);T_ASSERT(!jass_rterror_pending(level.vm));PatrolLifetimeCheck();
    if(checkpoint) {
        unsigned final_steps=steps;
        patrolLifetimeRun_t *completed=malloc(sizeof(*completed));T_NOT_NULL(completed);
        if(completed)*completed=patrol_lifetime_run;
        S_ClearMoveGroups();bool loaded=ReadGame(save);T_ASSERT(loaded);
        if(loaded) {
            patrol_lifetime_run=*checkpoint;
            for(steps=saved_steps;steps<final_steps;steps++)PatrolLifetimeStep();
            T_ASSERT(patrol_lifetime_run.complete);T_ASSERT(!jass_rterror_pending(level.vm));PatrolLifetimeCheck();
            if(completed) {
                T_EQ(patrol_lifetime_run.count,completed->count);
                T_ASSERT(patrol_lifetime_run.count<=16000);
                if(patrol_lifetime_run.count==completed->count && completed->count<=16000)
                    T_ASSERT(!memcmp(patrol_lifetime_run.rows,completed->rows,completed->count*sizeof(completed->rows[0])));
                T_EQ(patrol_lifetime_run.timer_count,completed->timer_count);
                T_ASSERT(patrol_lifetime_run.timer_count<=2048);
                if(patrol_lifetime_run.timer_count==completed->timer_count && completed->timer_count<=2048)
                    T_ASSERT(!memcmp(patrol_lifetime_run.timers,completed->timers,completed->timer_count*sizeof(completed->timers[0])));
            }
        }
        free(completed);free(checkpoint);remove(save);
    }
    move_test_group_arrival=NULL;move_test_group_committed=NULL;test_preload_marker=NULL;attack_test_acquire=NULL;
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    level.started=false;reset_entities();setup_test_world();
    level.setup.map_flags=old_flags;level.setup.random_seed=old_seed;
    FOR_LOOP(i,3){G_SetSLKRows(names[i],old[i]);free_slk_rows(rows[i]);}
}
TEST(wc3_patrol_lifetime, complete_original_interval_diagnostic) {PatrolLifetimeRun(false);}
TEST(wc3_patrol_lifetime, path_cleared_saved_full_interval_diagnostic) {PatrolLifetimeRun(true);}
TEST(wc3_patrol_lifetime, public_acquisition_range_updates_live_query_and_save) {
    reset_entities();setup_test_world();
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    uint32_t old_flags=level.setup.map_flags,old_seed=level.setup.random_seed;
    level.setup.map_flags=0;level.setup.random_seed=0x77617233u;G_InitMapRandom();
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    cstring_t names[]={"UnitData","UnitBalance","UnitWeapons"};
    cstring_t texts[]={tft?patrol_tft_UnitData:patrol_roc_UnitData,
        tft?patrol_tft_UnitBalance:patrol_roc_UnitBalance,
        tft?patrol_tft_UnitWeapons:patrol_roc_UnitWeapons};
    slkTestData_t *rows[3],*old[3];
    FOR_LOOP(i,3){rows[i]=parse_slk_string(texts[i]);old[i]=G_SetSLKRows(names[i],rows[i]);}
    T_ASSERT(run_test_jass(
        "globals\nunit rangeUnit=null\nendglobals\n"
        "function zero takes nothing returns nothing\ncall SetUnitAcquireRange(rangeUnit,0.0)\nendfunction\n"
        "function threshold takes nothing returns nothing\n"
        "call SetUnitAcquireRange(rangeUnit,50.0)\ncall check()\n"
        "call SetUnitAcquireRange(rangeUnit,I2R(13107199)/262144.0)\ncall check()\n"
        "call SetUnitAcquireRange(rangeUnit,I2R(13107201)/262144.0)\n"
        "call BJassAssert(GetUnitAcquireRange(rangeUnit)==I2R(13107201)/262144.0,\"adjacent accepted acquisition range\")\nendfunction\n"
        "function restoreRange takes nothing returns nothing\ncall SetUnitAcquireRange(rangeUnit,137.25)\nendfunction\n"
        "function pause takes nothing returns nothing\ncall PauseUnit(rangeUnit,true)\nendfunction\n"
        "function resume takes nothing returns nothing\ncall PauseUnit(rangeUnit,false)\nendfunction\n"
        "function kill takes nothing returns nothing\ncall KillUnit(rangeUnit)\nendfunction\n"
        "function patrol takes nothing returns nothing\n"
        "call BJassAssert(IssuePointOrder(rangeUnit,\"patrol\",1024.0,512.0),\"Patrol activation\")\nendfunction\n"
        "function check takes nothing returns nothing\n"
        "call BJassAssert(GetUnitAcquireRange(rangeUnit)==137.25,\"restored public acquisition range\")\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "set rangeUnit=CreateUnit(Player(0),'hfoo',512.0,512.0,0.0)\n"
        "call BJassAssert(GetUnitAcquireRange(rangeUnit)==500.0,\"authored live acquisition range\")\n"
        "call SetUnitAcquireRange(rangeUnit,137.25)\ncall check()\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(u,u->inuse && u->class_id==MAKEFOURCC('h','f','o','o')){unit=u;break;}
    T_NOT_NULL(unit);
    if(unit) {
        uint32_t index=(uint32_t)(unit-g_edicts);
        T_EQ(G_AcquisitionRange(unit),137.25f);
        jass_callbyname(level.vm,"threshold",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(wc3_float_bits(G_AcquisitionRange(unit)),0x42480001u);
        jass_callbyname(level.vm,"restoreRange",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(unit->attack_scan_initial_pending);
        T_EQ(wc3_float_bits(unit->attack_scan.deadline.time),1028668480u);
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        cstring_t save=Test_TempPath("patrol-live-acquisition-range.bin");
        T_ASSERT(WriteGame(save));
        cstring_t paused_save=Test_TempPath("patrol-pending-paused-scan.bin");
        jass_callbyname(level.vm,"pause",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(unit->attack_scan.active);T_ASSERT(unit->attack_scan_initial_pending);
        T_ASSERT(!unit->attack_scan_enabled);T_ASSERT(WriteGame(paused_save));
        FOR_LOOP(replay,2) {
            if(replay){S_ClearMoveGroups();T_ASSERT(ReadGame(paused_save));unit=g_edicts+index;}
            T_ASSERT(!unit->attack_scan_enabled);
            FOR_LOOP(i,12){PatrolLifetimeStep();}
            T_ASSERT(!unit->attack_scan_initial_pending);T_ASSERT(!unit->attack_scan.active);
            wc3Clock_t now=G_TimerQueryClock(NULL);
            jass_callbyname(level.vm,"resume",false);T_ASSERT(!jass_rterror_pending(level.vm));
            T_ASSERT(unit->attack_scan_enabled);T_ASSERT(unit->attack_scan.active);
            T_EQ(wc3_float_bits(unit->attack_scan.deadline.time),wc3_float_bits(wc3_add(now.time,.01f)));
        }
        remove(paused_save);
        S_ClearMoveGroups();T_ASSERT(ReadGame(save));unit=g_edicts+index;
        cstring_t dead_save=Test_TempPath("patrol-pending-dead-scan.bin");
        jass_callbyname(level.vm,"kill",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(M_IsDead(unit));T_ASSERT(unit->attack_scan.active);T_ASSERT(unit->attack_scan_initial_pending);
        T_ASSERT(WriteGame(dead_save));
        FOR_LOOP(replay,2) {
            if(replay){S_ClearMoveGroups();T_ASSERT(ReadGame(dead_save));unit=g_edicts+index;}
            T_ASSERT(M_IsDead(unit));T_ASSERT(unit->attack_scan.active);T_ASSERT(unit->attack_scan_initial_pending);
            FOR_LOOP(i,12){PatrolLifetimeStep();}
            T_ASSERT(!unit->attack_scan_initial_pending);T_ASSERT(!unit->attack_scan.active);
        }
        remove(dead_save);
        S_ClearMoveGroups();T_ASSERT(ReadGame(save));unit=g_edicts+index;
        FOR_LOOP(i,12){PatrolLifetimeStep();}
        T_ASSERT(!unit->attack_scan_initial_pending);
        jass_callbyname(level.vm,"zero",false);
        T_ASSERT(!jass_rterror_pending(level.vm));T_EQ(G_AcquisitionRange(unit),137.25f);
        jass_callbyname(level.vm,"check",false);T_ASSERT(!jass_rterror_pending(level.vm));
        S_ClearMoveGroups();bool loaded=ReadGame(save);T_ASSERT(loaded);
        if(loaded) {
            unit=g_edicts+index;
            T_EQ(G_AcquisitionRange(unit),137.25f);
            T_ASSERT(unit->attack_scan_initial_pending);
            jass_callbyname(level.vm,"check",false);T_ASSERT(!jass_rterror_pending(level.vm));
            FOR_LOOP(i,12){PatrolLifetimeStep();}
            T_ASSERT(!unit->attack_scan_initial_pending);
            T_ASSERT(WriteGame(save));
            FOR_LOOP(replay,2) {
                if(replay){S_ClearMoveGroups();T_ASSERT(ReadGame(save));unit=g_edicts+index;}
                T_ASSERT(!unit->attack_scan_initial_pending);
                wc3Clock_t now=G_TimerQueryClock(NULL);
                jass_callbyname(level.vm,"patrol",false);T_ASSERT(!jass_rterror_pending(level.vm));
                T_EQ(wc3_float_bits(unit->attack_scan.deadline.time),wc3_float_bits(wc3_add(now.time,.01f)));
            }
        }
        remove(save);
    }
    level.started=false;reset_entities();setup_test_world();
    level.setup.map_flags=old_flags;level.setup.random_seed=old_seed;
    FOR_LOOP(i,3){G_SetSLKRows(names[i],old[i]);free_slk_rows(rows[i]);}
}
/* Engine ownership regressions: the full native oracle above establishes
 * timing; these mutations verify inverse/save safety without new retail claims. */
static unsigned patrol_reaction_callback_mode;
static void PatrolReactionTargetLost(edict_t *unit) {
    (void)unit;
    attack_test_target_lost=NULL;
    jass_callbyname(level.vm,patrol_reaction_callback_mode==5 ? "stop" : "move",false);
}
TEST(wc3_patrol_lifetime, pending_reaction_save_and_owner_inverse) {
    stbIniCache_t custom_misc={0};
    void *old_misc=game.config.misc.source;float old_reaction=game.constants.reactionDelay;
    T_ASSERT(Stb_IniCacheLoadBuffer(&custom_misc,"[Misc]\nReactionDelay=0.75\n"));
    game.config.misc.source=custom_misc.source;
    InitMiscValueDefault("ReactionDelay",&game.constants.reactionDelay,.25f);
    T_EQ(game.constants.reactionDelay,.75f);
    reset_entities();setup_test_world();
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    uint32_t old_flags=level.setup.map_flags,old_seed=level.setup.random_seed;
    level.setup.map_flags=0;level.setup.random_seed=0x77617233u;G_InitMapRandom();
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    cstring_t names[]={"UnitData","UnitBalance","UnitWeapons"};
    cstring_t texts[]={tft?patrol_tft_UnitData:patrol_roc_UnitData,
        tft?patrol_tft_UnitBalance:patrol_roc_UnitBalance,
        tft?patrol_tft_UnitWeapons:patrol_roc_UnitWeapons};
    slkTestData_t *rows[3],*old[3];
    FOR_LOOP(i,3){rows[i]=parse_slk_string(texts[i]);old[i]=G_SetSLKRows(names[i],rows[i]);}
    T_ASSERT(run_test_jass(
        "globals\nunit reactionSource=null\nunit reactionTarget=null\nendglobals\n"
        "function detach takes nothing returns nothing\ncall BJassAssert(UnitRemoveAbility(reactionSource,'Aatk'),\"detach Attack\")\nendfunction\n"
        "function stop takes nothing returns nothing\ncall IssueImmediateOrder(reactionSource,\"stop\")\nendfunction\n"
        "function move takes nothing returns nothing\ncall IssuePointOrder(reactionSource,\"move\",512.0,1024.0)\nendfunction\n"
        "function kill takes nothing returns nothing\ncall KillUnit(reactionTarget)\nendfunction\n"
        "function remove takes nothing returns nothing\ncall RemoveUnit(reactionTarget)\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "set reactionSource=CreateUnit(Player(0),'hfoo',512.0,512.0,0.0)\n"
        "set reactionTarget=CreateUnit(Player(1),'hfoo',640.0,512.0,180.0)\n"
        "call PauseUnit(reactionTarget,true)\n"
        "call SetUnitAcquireRange(reactionSource,137.25)\n"
        "call IssuePointOrder(reactionSource,\"patrol\",1024.0,512.0)\nendfunction\n"));
    edict_t *unit=NULL,*target=NULL;
    FILTER_EDICTS(u,u->inuse && u->class_id==MAKEFOURCC('h','f','o','o')) {
        if(u->s.player==0)unit=u;else if(u->s.player==1)target=u;
    }
    T_NOT_NULL(unit);T_NOT_NULL(target);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    if(unit && target) {
        unsigned index=unit-g_edicts,target_index=target-g_edicts;
        /* True detach cancels even the initial request; Pause/death retain it. */
        T_ASSERT(unit->attack_scan_initial_pending);T_ASSERT(unit->attack_scan.active);
        cstring_t initial=Test_TempPath("patrol-initial-detach.bin");
        T_ASSERT(WriteGame(initial));
        jass_callbyname(level.vm,"detach",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(!unit->attack_scan.active);T_ASSERT(!unit->attack_scan_enabled);
        FOR_LOOP(i,100){PatrolLifetimeStep();}
        T_ASSERT(!unit->attack_scan.active);T_ASSERT(!unit->attack_reaction.active);
        S_ClearMoveGroups();T_ASSERT(ReadGame(initial));remove(initial);
        unit=g_edicts+index;target=g_edicts+target_index;
        FOR_LOOP(i,200) {if(unit->attack_reaction.active)break;PatrolLifetimeStep();}
        T_ASSERT(unit->attack_reaction.active);
        T_EQ(unit->current_order_id,WC3_ORDER_ID_PATROL_TWO_POINTS);
        T_ASSERT(unit->currentmove==&attack_move_reaction);
        T_ASSERT(unit->attack_target==target);
        cstring_t save=Test_TempPath("patrol-reaction-owner.bin");
        abilityPrimaryTimer_t timer=unit->attack_reaction;
        T_EQ(wc3_float_bits(timer.deadline.time),wc3_float_bits(wc3_add(
            wc3_float(1028668480u),wc3_mul(wc3_float(1056568932u),.75f))));
        T_ASSERT(WriteGame(save));
        FOR_LOOP(mode,8) {
            S_ClearMoveGroups();bool loaded=ReadGame(save);T_ASSERT(loaded);if(!loaded)break;
            unit=g_edicts+index;target=g_edicts+target_index;
            T_ASSERT(unit->attack_reaction.active);
            T_EQ(wc3_float_bits(unit->attack_reaction.deadline.time),wc3_float_bits(timer.deadline.time));
            T_ASSERT(unit->attack_target==target);
            T_ASSERT(unit->currentmove==&attack_move_reaction);
            if(mode) {
                cstring_t calls[]={"","stop","move","kill","remove","kill","kill","detach"};
                if(mode==5 || mode==6){patrol_reaction_callback_mode=mode;attack_test_target_lost=PatrolReactionTargetLost;}
                jass_callbyname(level.vm,calls[mode],false);T_ASSERT(!jass_rterror_pending(level.vm));
                T_ASSERT(attack_test_target_lost==NULL);
                T_ASSERT(!unit->attack_reaction.active);
                if(mode==1) {
                    T_ASSERT(unit->attack_target==NULL);
                    /* Stop may later acquire again. Remove that independent
                     * opportunity while checking the cancelled old deadline. */
                    jass_callbyname(level.vm,"kill",false);
                }
            }
            FOR_LOOP(i,100){PatrolLifetimeStep();}
            T_ASSERT(!unit->attack_reaction.active);
            if(mode!=7)T_ASSERT(unit->currentmove!=&attack_move_reaction);
            if(mode==7) {
                T_ASSERT(!G_ActorHasAbilityCode(unit,MAKEFOURCC('A','a','t','k')));
                T_ASSERT(!unit->attack_scan.active);T_ASSERT(!unit->attack_damage.active);
                T_EQ(target->health.value,target->health.max_value);
            }
            if(mode==1)T_ASSERT(unit->attack_target==NULL);
            if(mode==2 || mode==6)T_EQ(unit->current_order_id,G_OrderId("move"));
            if(mode>=3 && mode!=7)T_ASSERT(unit->attack_target!=target);
        }
        remove(save);
    }
    attack_test_target_lost=NULL;
    game.config.misc.source=old_misc;game.constants.reactionDelay=old_reaction;
    Stb_IniCacheFree(&custom_misc);
    level.started=false;reset_entities();setup_test_world();
    level.setup.map_flags=old_flags;level.setup.random_seed=old_seed;
    FOR_LOOP(i,3){G_SetSLKRows(names[i],old[i]);free_slk_rows(rows[i]);}
}
static unsigned patrol_damage_commits;
static wc3Clock_t patrol_damage_clock;
static void PatrolDamageCommit(edict_t const *unit) {
    (void)unit;patrol_damage_commits++;patrol_damage_clock=G_TimerQueryClock(NULL);
}
TEST(wc3_patrol_lifetime, weapon_damage_clock_survives_cold_save_and_frame_cadence) {
    reset_entities();setup_test_world();
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    uint32_t old_flags=level.setup.map_flags,old_seed=level.setup.random_seed;
    level.setup.map_flags=0;level.setup.random_seed=0x77617233u;G_InitMapRandom();
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    cstring_t names[]={"UnitData","UnitBalance","UnitWeapons"};
    cstring_t texts[]={tft?patrol_tft_UnitData:patrol_roc_UnitData,
        tft?patrol_tft_UnitBalance:patrol_roc_UnitBalance,
        tft?patrol_tft_UnitWeapons:patrol_roc_UnitWeapons};
    slkTestData_t *rows[3],*old[3];
    FOR_LOOP(i,3){rows[i]=parse_slk_string(texts[i]);old[i]=G_SetSLKRows(names[i],rows[i]);}
    T_ASSERT(run_test_jass(
        "globals\nunit damageSource=null\nunit damageTarget=null\nendglobals\n"
        "function onDamage takes nothing returns nothing\ncall IssueImmediateOrder(damageSource,\"stop\")\ncall SetUnitOwner(damageTarget,Player(0),false)\nendfunction\n"
        "function kill takes nothing returns nothing\ncall KillUnit(damageTarget)\nendfunction\n"
        "function move takes nothing returns nothing\ncall IssuePointOrder(damageSource,\"move\",512.0,1024.0)\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger tr=CreateTrigger()\n"
        "set damageSource=CreateUnit(Player(0),'hfoo',512.0,512.0,0.0)\n"
        "set damageTarget=CreateUnit(Player(1),'hfoo',640.0,512.0,180.0)\n"
        "call PauseUnit(damageTarget,true)\n"
        "call TriggerRegisterUnitEvent(tr,damageTarget,ConvertUnitEvent(52))\n"
        "call TriggerAddAction(tr,function onDamage)\n"
        "call BJassAssert(IssueTargetOrder(damageSource,\"attack\",damageTarget),\"public explicit Attack\")\nendfunction\n"));
    edict_t *unit=NULL,*target=NULL;
    FILTER_EDICTS(u,u->inuse && u->class_id==MAKEFOURCC('h','f','o','o')) {
        if(u->s.player==0)unit=u;else if(u->s.player==1)target=u;
    }
    T_NOT_NULL(unit);T_NOT_NULL(target);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    if(unit && target) {
        unsigned index=unit-g_edicts,target_index=target-g_edicts;
        FOR_LOOP(i,200){if(unit->attack_damage.active)break;PatrolLifetimeStep();}
        T_ASSERT(unit->attack_damage.active);T_ASSERT(!unit->attack_reaction.active);
        wc3Clock_t deadline=unit->attack_damage.deadline;
        float health=target->health.value;
        cstring_t save=Test_TempPath("patrol-damage-point.bin");T_ASSERT(WriteGame(save));
        FOR_LOOP(mode,4) {
            S_ClearMoveGroups();bool loaded=ReadGame(save);T_ASSERT(loaded);if(!loaded)break;
            unit=g_edicts+index;target=g_edicts+target_index;
            T_ASSERT(unit->attack_damage.active);
            T_EQ(wc3_float_bits(unit->attack_damage.deadline.time),wc3_float_bits(deadline.time));
            patrol_damage_commits=0;attack_test_damage=PatrolDamageCommit;
            if(mode>=2) {
                jass_callbyname(level.vm,mode==2?"kill":"move",false);
                T_ASSERT(!jass_rterror_pending(level.vm));T_ASSERT(!unit->attack_damage.active);
            }
            unsigned step=mode==1?100:5;
            FOR_LOOP(i,1000/step){level.time+=step;globals.RunFrame();}
            attack_test_damage=NULL;
            if(mode<2) {
                T_EQ(patrol_damage_commits,1u);
                T_EQ(wc3_float_bits(patrol_damage_clock.time),wc3_float_bits(deadline.time));
                T_EQ(patrol_damage_clock.epoch,deadline.epoch);
                T_ASSERT(target->health.value<health);
            } else T_EQ(patrol_damage_commits,0u);
            T_ASSERT(!unit->attack_damage.active);
            if(mode==3)T_EQ(unit->current_order_id,G_OrderId("move"));
        }
        remove(save);
    }
    attack_test_damage=NULL;
    level.started=false;reset_entities();setup_test_world();
    level.setup.map_flags=old_flags;level.setup.random_seed=old_seed;
    FOR_LOOP(i,3){G_SetSLKRows(names[i],old[i]);free_slk_rows(rows[i]);}
}
/* Nonstock authored artillery without an animation: suspension must not
 * strand the retained ground order merely because no model endfunc fires. */
TEST(wc3_patrol_lifetime, ground_damage_owner_resumes_after_public_pause) {
    reset_entities();setup_test_world();
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    bool tft=atoi(gi.CvarString("fs_expansion","0"))!=0;
    cstring_t names[]={"UnitData","UnitBalance","UnitWeapons","UnitUI"};
    cstring_t texts[]={tft?patrol_tft_UnitData:patrol_roc_UnitData,
        tft?patrol_tft_UnitBalance:patrol_roc_UnitBalance,
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitWeapID\"\nC;X2;Y1;K\"weapsOn\"\n"
        "C;X3;Y1;K\"weapTp1\"\nC;X4;Y1;K\"atkType1\"\nC;X5;Y1;K\"targs1\"\n"
        "C;X6;Y1;K\"rangeN1\"\nC;X7;Y1;K\"dmgpt1\"\nC;X8;Y1;K\"cool1\"\n"
        "C;X9;Y1;K\"dmgplus1\"\nC;X10;Y1;K\"dice1\"\nC;X11;Y1;K\"sides1\"\n"
        "C;X1;Y2;K\"hfoo\"\nC;X2;Y2;K1\nC;X3;Y2;K\"artillery\"\n"
        "C;X4;Y2;K\"siege\"\nC;X5;Y2;K\"ground\"\nC;X6;Y2;K400\n"
        "C;X7;Y2;K0.4\nC;X8;Y2;K1.0\nC;X9;Y2;K10\nC;X10;Y2;K1\nC;X11;Y2;K1\nE\n",
        "ID;PWXL;N;E\nC;X1;Y1;K\"unitUIID\"\nC;X2;Y1;K\"file\"\n"
        "C;X1;Y2;K\"hfoo\"\nC;X2;Y2;K\"\"\nE\n"};
    slkTestData_t *rows[4],*old[4];
    FOR_LOOP(i,4){rows[i]=parse_slk_string(texts[i]);old[i]=G_SetSLKRows(names[i],rows[i]);}
    T_ASSERT(run_test_jass(
        "globals\nunit groundSource=null\nendglobals\n"
        "function pause takes nothing returns nothing\ncall PauseUnit(groundSource,true)\nendfunction\n"
        "function resume takes nothing returns nothing\ncall PauseUnit(groundSource,false)\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "set groundSource=CreateUnit(Player(0),'hfoo',512.0,512.0,0.0)\n"
        "call BJassAssert(IssuePointOrder(groundSource,\"attackground\",768.0,512.0),\"public artillery order\")\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(u,u->inuse && u->class_id==MAKEFOURCC('h','f','o','o')){unit=u;break;}
    T_NOT_NULL(unit);level.started=level.scriptsConfigured=level.scriptsStarted=true;
    if(unit) {
        T_EQ(attack_ground_profile(unit)->weapon,WPN_ARTILLERY);
        FOR_LOOP(i,200){if(unit->attack_damage.active)break;PatrolLifetimeStep();}
        T_ASSERT(unit->attack_damage.active);T_ASSERT(!attack_animation_can_finish(unit));
        jass_callbyname(level.vm,"pause",false);T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(unit->paused);T_ASSERT(!unit->attack_damage.active);
        FOR_LOOP(i,120){PatrolLifetimeStep();}
        T_ASSERT(!unit->attack_damage.active);
        jass_callbyname(level.vm,"resume",false);T_ASSERT(!jass_rterror_pending(level.vm));
        bool resumed=false;
        FOR_LOOP(i,400){PatrolLifetimeStep();if(unit->attack_damage.active){resumed=true;break;}}
        T_ASSERT(resumed);T_EQ(unit->current_order_id,G_OrderId("attackground"));
    }
    level.started=false;reset_entities();setup_test_world();
    FOR_LOOP(i,4){G_SetSLKRows(names[i],old[i]);free_slk_rows(rows[i]);}
}
#endif
