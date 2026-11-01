#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"
#include "fixtures/retail_patrol265_stock.h"

bool run_test_jass(cstring_t);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(cstring_t);
void free_slk_rows(slkTestData_t *);
void CM_SetupTestWorldBounds(box2_t const *);

/* Static-grounded owner tests complement the complete public265 failure
 * replay. Slot rules come from unmodified495c00's portable oracle; these
 * cases additionally exercise the real primary queue and serializer. */
typedef struct {
    slkTestData_t *rows[5],*old[5];
    edict_t *source,*targets[9];
    uint32_t seed;
} failedCacheFixture_t;
static cstring_t failed_cache_tables[]={"UnitData","UnitBalance","UnitWeapons","UnitUI","UnitAbilities"};
static void FailedCacheFinish(failedCacheFixture_t *f) {
    level.started=false;reset_entities();setup_test_world();
    level.setup.random_seed=f->seed;
    FOR_LOOP(i,5){G_SetSLKRows(failed_cache_tables[i],f->old[i]);free_slk_rows(f->rows[i]);}
}
static bool FailedCacheSetup(failedCacheFixture_t *f) {
    memset(f,0,sizeof(*f));reset_entities();setup_test_world();
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    level.pathing_clock=(wc3Clock_t){0,0,300};
    f->seed=level.setup.random_seed;level.setup.random_seed=0x77617233u;G_InitMapRandom();
    cstring_t values[]={patrol265_UnitData,patrol265_UnitBalance,patrol265_UnitWeapons,patrol265_UnitUI,patrol265_UnitAbilities};
    FOR_LOOP(i,5){f->rows[i]=parse_slk_string(values[i]);f->old[i]=G_SetSLKRows(failed_cache_tables[i],f->rows[i]);}
    bool ok=run_test_jass(
        "globals\nunit cacheSource=null\nunit array cacheTarget\nendglobals\n"
        "function pause takes nothing returns nothing\ncall PauseUnit(cacheSource,true)\nendfunction\n"
        "function detach takes nothing returns nothing\ncall BJassAssert(UnitRemoveAbility(cacheSource,'Aatk'),\"remove Attack owner\")\nendfunction\n"
        "function move takes nothing returns nothing\ncall IssuePointOrder(cacheSource,\"move\",512.0,1800.0)\nendfunction\n"
        "function owner takes nothing returns nothing\ncall SetUnitOwner(cacheTarget[0],Player(2),false)\nendfunction\n"
        "function main takes nothing returns nothing\nlocal integer i=0\n"
        "set cacheSource=CreateUnit(Player(0),'hfoo',512.0,512.0,0.0)\ncall SetUnitUserData(cacheSource,100)\n"
        "loop\nexitwhen i==9\nset cacheTarget[i]=CreateUnit(Player(1),'hfoo',1024.0,128.0+I2R(i)*180.0,180.0)\n"
        "call SetUnitUserData(cacheTarget[i],i+1)\ncall PauseUnit(cacheTarget[i],true)\nset i=i+1\nendloop\n"
        "call IssuePointOrder(cacheSource,\"move\",512.0,1800.0)\nendfunction\n");
    T_ASSERT(ok);T_ASSERT(!jass_rterror_pending(level.vm));
    FILTER_EDICTS(u,u->inuse && u->class_id==MAKEFOURCC('h','f','o','o')) {
        if(u->user_data==100)f->source=u;
        else if(u->user_data>=1 && u->user_data<=9)f->targets[u->user_data-1]=u;
    }
    T_NOT_NULL(f->source);FOR_LOOP(i,9){T_NOT_NULL(f->targets[i]);ok=ok && f->targets[i]!=NULL;}
    return ok && f->source;
}
static void FailedCacheStep(void) {
    wc3_clock_advance(&level.pathing_clock,wc3_float(0x3ba3d70a),0);
    level.time+=5;level.pathing_msec+=5;
    bool scheduled=level.scheduled_frame;level.scheduled_frame=true;G_RunTimers();level.scheduled_frame=scheduled;
}
static void FailedCacheAdvance(unsigned msec) {FOR_LOOP(i,msec/5){FailedCacheStep();}}
static unsigned FailedCacheActive(edict_t const *u) {
    unsigned n=0;FOR_LOOP(i,ATTACK_FAILED_TARGET_SLOTS)n+=u->attack_failed_targets[i].timer.active;return n;
}
TEST(wc3_attack_failed_targets, original_slot_selection_refresh_and_eviction) {
    failedCacheFixture_t f;if(!FailedCacheSetup(&f)){FailedCacheFinish(&f);return;}
    edict_t *u=f.source;
    FOR_LOOP(i,8) {
        attack_failed_target_remember(u,f.targets[i]);
        T_EQ(u->attack_failed_targets[7-i].target,f.targets[i]);
        T_ASSERT(!attack_failed_target_allowed(u,f.targets[i]));
        T_EQ(wc3_float_bits(u->attack_failed_targets[7-i].timer.deadline.time),wc3_float_bits(10));
    }
    uint64_t previous=u->attack_failed_targets[7].timer.sequence;
    attack_failed_target_remember(u,f.targets[0]);
    T_EQ(u->attack_failed_targets[7].target,f.targets[0]);
    T_ASSERT(u->attack_failed_targets[7].timer.sequence>previous);T_EQ(FailedCacheActive(u),8);
    /* All remaining times tie: native's strict comparison retains slot0. */
    attack_failed_target_remember(u,f.targets[8]);
    T_EQ(u->attack_failed_targets[0].target,f.targets[8]);
    T_ASSERT(attack_failed_target_allowed(u,f.targets[7]));
    attack_failed_targets_clear(u);T_EQ(FailedCacheActive(u),0);
    FOR_LOOP(i,8){attack_failed_target_remember(u,f.targets[i]);FailedCacheAdvance(25);}
    /* Different clocks: the first insertion in slot7 expires earliest. */
    attack_failed_target_remember(u,f.targets[8]);
    T_EQ(u->attack_failed_targets[7].target,f.targets[8]);
    T_ASSERT(attack_failed_target_allowed(u,f.targets[0]));
    FailedCacheFinish(&f);
}
TEST(wc3_attack_failed_targets, independent_expiry_and_cold_saved_heap) {
    failedCacheFixture_t f;if(!FailedCacheSetup(&f)){FailedCacheFinish(&f);return;}
    edict_t *u=f.source;unsigned index=u-g_edicts;
    unsigned target_indices[3];FOR_LOOP(i,3)target_indices[i]=f.targets[i]-g_edicts;
    attack_failed_target_remember(u,f.targets[0]);FailedCacheAdvance(5000);
    attack_failed_target_remember(u,f.targets[0]); /* Refresh to15 seconds. */
    attack_failed_target_remember(u,f.targets[1]);FailedCacheAdvance(500);
    attack_failed_target_remember(u,f.targets[2]); /* Independent15.5 deadline. */
    T_EQ(FailedCacheActive(u),3);
    abilityPrimaryTimer_t timers[3];FOR_LOOP(i,3)timers[i]=u->attack_failed_targets[7-i].timer;
    cstring_t save=Test_TempPath("attack-failed-cache.bin");T_ASSERT(WriteGame(save));
    FOR_LOOP(replay,2) {
        if(replay){S_ClearMoveGroups();T_ASSERT(ReadGame(save));u=g_edicts+index;
            FOR_LOOP(i,3)f.targets[i]=g_edicts+target_indices[i];}
        T_EQ(FailedCacheActive(u),3);T_ASSERT(!attack_failed_target_allowed(u,f.targets[0]));
        FOR_LOOP(i,3) {
            abilityPrimaryTimer_t const *timer=&u->attack_failed_targets[7-i].timer;
            T_EQ(wc3_float_bits(timer->deadline.time),wc3_float_bits(timers[i].deadline.time));
            T_EQ(timer->deadline.epoch,timers[i].deadline.epoch);T_EQ(timer->sequence,timers[i].sequence);
        }
        FailedCacheAdvance(9600); /*15.1: both refreshed entries expired. */
        T_ASSERT(attack_failed_target_allowed(u,f.targets[0]));T_ASSERT(attack_failed_target_allowed(u,f.targets[1]));
        T_ASSERT(!attack_failed_target_allowed(u,f.targets[2]));T_EQ(FailedCacheActive(u),1);
        FailedCacheAdvance(600);T_EQ(FailedCacheActive(u),0);T_ASSERT(attack_failed_target_allowed(u,f.targets[2]));
    }
    remove(save);FailedCacheFinish(&f);
}
TEST(wc3_attack_failed_targets, weak_identity_and_true_detach_inverse) {
    failedCacheFixture_t f;if(!FailedCacheSetup(&f)){FailedCacheFinish(&f);return;}
    edict_t *u=f.source,*target=f.targets[0];
    attack_failed_target_remember(u,target);
    jass_callbyname(level.vm,"owner",false);T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(!attack_failed_target_allowed(u,target));T_EQ(FailedCacheActive(u),1);
    jass_callbyname(level.vm,"move",false);T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(FailedCacheActive(u),1);
    jass_callbyname(level.vm,"pause",false);T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(FailedCacheActive(u),1);
    uint32_t birth=target->spawn_time;
    unsigned target_index=target-g_edicts;
    G_FreeEdict(target);
    /* G_Spawn requires freetime+1000 < level.time, not <=. Advance the
     * real primary queue beyond that boundary while the cache remains live. */
    FailedCacheAdvance(1005);
    T_ASSERT(!target->inuse);
    T_ASSERT(target->freetime+1000<level.time);
    edict_t *replacement=NULL;
    /* G_Spawn chooses the lowest eligible free index. Fill any earlier
     * holes through ordinary unit creation instead of assuming this target
     * is the first free entity or manufacturing its incarnation. */
    FOR_LOOP(i,target_index+1) {
        edict_t *created=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){1024,128},180);
        T_NOT_NULL(created);
        if(!created)break;
        if(created==target){replacement=created;break;}
        T_ASSERT((unsigned)(created-g_edicts)<target_index);
        if((unsigned)(created-g_edicts)>=target_index)break;
    }
    T_NOT_NULL(replacement);
    if(replacement) {
        T_ASSERT(replacement==target);T_NE(replacement->spawn_time,birth);
        T_ASSERT(attack_failed_target_allowed(u,replacement));T_EQ(FailedCacheActive(u),1);
        unsigned source_index=u-g_edicts,replacement_index=replacement-g_edicts;
        cstring_t save=Test_TempPath("attack-failed-weak-identity.bin");T_ASSERT(WriteGame(save));
        S_ClearMoveGroups();T_ASSERT(ReadGame(save));u=g_edicts+source_index;replacement=g_edicts+replacement_index;
        T_EQ(FailedCacheActive(u),1);T_ASSERT(attack_failed_target_allowed(u,replacement));
        T_EQ(u->attack_failed_targets[7].target_spawn_time,birth);remove(save);
    }
    jass_callbyname(level.vm,"detach",false);T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(FailedCacheActive(u),0);FailedCacheAdvance(11000);T_EQ(FailedCacheActive(u),0);
    FailedCacheFinish(&f);
}
#endif
