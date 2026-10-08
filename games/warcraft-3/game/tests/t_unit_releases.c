#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"

bool run_test_jass(cstring_t);
void setup_test_world(void);

static bool releases_setup(void) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    return run_test_jass(
        "globals\nunit array u\ninteger trace=0\nendglobals\n"
        "function retired takes nothing returns nothing\n"
        "if GetIssuedOrderId()==OrderId(\"undefend\") then\n"
        "set trace=trace*10+GetUnitUserData(GetTriggerUnit())\nendif\nendfunction\n"
        "function main takes nothing returns nothing\nlocal integer i=1\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerAddAction(t,function retired)\nloop\nexitwhen i>3\n"
        "set u[i]=CreateUnit(Player(0),'hfoo',64.0*i,64.0,0.0)\n"
        "call UnitAddAbility(u[i],'Adef')\ncall SetUnitUserData(u[i],i)\nset i=i+1\nendloop\n"
        "call RemoveUnit(u[3])\ncall RemoveUnit(u[1])\ncall RemoveUnit(u[2])\nendfunction\n"
        "function check_order takes nothing returns nothing\n"
        "call BJassAssert(trace==331122,\"equal-deadline releases retain submission order\")\nendfunction\n");
}

TEST(wc3_unit_releases, equal_deadlines_release_in_submission_order) {
    T_ASSERT(releases_setup());
    level.pathing_clock.time=wc3_add(level.pathing_clock.time,G_ClockMinimumDelay());
    G_RunDeferredFrees();jass_callbyname(level.vm,"check_order",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_unit_releases, removal_stays_live_until_its_minimum_delay) {
    T_ASSERT(releases_setup());uint32_t live=0;
    G_RunDeferredFrees();
    FILTER_EDICTS(unit,unit->class_id==MAKEFOURCC('h','f','o','o')) {
        live++;T_ASSERT(G_IsDeferredFree(unit));
    }
    T_EQ(live,3);
}

TEST(wc3_unit_releases, primary_timer_drain_owns_unit_release) {
    T_ASSERT(releases_setup());level.scheduled_frame=true;G_RunTimers();
    uint32_t live=0;
    FILTER_EDICTS(unit,unit->class_id==MAKEFOURCC('h','f','o','o'))live++;
    T_EQ(live,0);jass_callbyname(level.vm,"check_order",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_unit_releases, real_frame_drains_releases_before_snapshot) {
    T_ASSERT(releases_setup());
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    level.time+=FRAMETIME;globals.RunFrame();
    uint32_t live=0;
    FILTER_EDICTS(unit,unit->class_id==MAKEFOURCC('h','f','o','o'))live++;
    T_EQ(live,0);jass_callbyname(level.vm,"check_order",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_unit_releases, cancellation_preserves_heap_and_does_not_release_reused_slot) {
    T_ASSERT(releases_setup());edict_t *victim=NULL;
    FILTER_EDICTS(unit,unit->class_id==MAKEFOURCC('h','f','o','o') && unit->user_data==1)victim=unit;
    T_NOT_NULL(victim);if(!victim)return;
    uint32_t const slot=victim->s.number;
    G_FreeEdict(victim);T_ASSERT(!G_IsDeferredFree(victim));
    level.time+=2000;
    edict_t *replacement=G_Spawn();T_EQ(replacement->s.number,slot);
    T_ASSERT(!G_IsDeferredFree(replacement));
    level.scheduled_frame=true;G_RunTimers();T_ASSERT(replacement->inuse);
    wc3Clock_t due;uint32_t sequence;T_ASSERT(!G_NextUnitRelease(&due,&sequence));
}

TEST(wc3_unit_releases, different_deadlines_and_unsigned_serial_wrap_choose_the_next_receiver) {
    reset_entities();setup_test_world();
    wc3Clock_t clocks[]={{.time=1,.span=300},{.time=0.5f,.span=300},{.time=1,.span=300}};
    edict_t *units[3];level.timer_sequence=UINT32_MAX-1;
    FOR_LOOP(i,3) {units[i]=G_Spawn();G_DeferFreeEdictAt(units[i],clocks+i);}
    wc3Clock_t due;uint32_t serial;
    T_ASSERT(G_NextUnitRelease(&due,&serial));T_EQ(serial,0);
    T_EQ(wc3_float_bits(due.time),wc3_float_bits(wc3_add(0.5f,G_ClockMinimumDelay())));
    G_FireUnitRelease();T_ASSERT(!units[1]->inuse);T_ASSERT(units[0]->inuse && units[2]->inuse);
    T_ASSERT(G_NextUnitRelease(&due,&serial));T_EQ(serial,1);
    G_FireUnitRelease();T_ASSERT(!units[2]->inuse);T_ASSERT(units[0]->inuse);
    T_ASSERT(G_NextUnitRelease(&due,&serial));T_EQ(serial,UINT32_MAX);
    G_FireUnitRelease();T_ASSERT(!units[0]->inuse);T_ASSERT(!G_NextUnitRelease(&due,&serial));
}

TEST(wc3_unit_releases, duplicate_request_reset_and_epoch_rebase_preserve_membership) {
    reset_entities();setup_test_world();edict_t *unit=G_Spawn();
    wc3Clock_t clock={.time=300,.span=300};G_DeferFreeEdictAt(unit,&clock);
    uint32_t const sequence=level.timer_sequence;G_DeferFreeEdictAt(unit,&clock);
    T_EQ(level.timer_sequence,sequence);T_ASSERT(G_IsDeferredFree(unit));
    G_RebaseUnitReleases(clock.span);
    wc3Clock_t due;uint32_t serial;T_ASSERT(G_NextUnitRelease(&due,&serial));T_EQ(due.epoch,1);
    T_EQ(wc3_float_bits(due.time),wc3_float_bits(wc3_sub(wc3_add(300,G_ClockMinimumDelay()),300)));
    G_ResetDeferredFrees();T_ASSERT(!G_IsDeferredFree(unit));T_ASSERT(!G_NextUnitRelease(&due,&serial));
    G_DeferFreeEdictAt(unit,&clock);T_ASSERT(G_IsDeferredFree(unit));G_TestFinishDeferredFrees();
    T_ASSERT(!unit->inuse && !G_IsDeferredFree(unit));
}
static bool chained_releases_setup(void) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    return run_test_jass(
        "globals\nunit first\nunit second\ntimer follow\nboolean chained=false\nendglobals\n"
        "function retired takes nothing returns nothing\n"
        "if GetIssuedOrderId()==OrderId(\"undefend\") and GetTriggerUnit()==first and not chained then\n"
        "set chained=true\ncall RemoveUnit(second)\n"
        "call TimerStart(follow,0.0,false,null)\nendif\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "set follow=CreateTimer()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerAddAction(t,function retired)\n"
        "set first=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        "call UnitAddAbility(first,'Adef')\n"
        "set second=CreateUnit(Player(0),'hfoo',192.0,64.0,0.0)\n"
        "call RemoveUnit(first)\nendfunction\n");
}

static void chained_releases_check(void) {
    uint32_t live=0;
    FILTER_EDICTS(unit,unit->class_id==MAKEFOURCC('h','f','o','o'))live++;
    T_EQ(live,0);T_EQ(level.num_timers,1);
    T_EQ(wc3_float_bits(level.timers[0].scalar_deadline.time),
        wc3_float_bits(wc3_add(wc3_add(1,G_ClockMinimumDelay()),G_ClockMinimumDelay())));
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_unit_releases, callback_chains_join_the_same_primary_advance) {
    T_ASSERT(chained_releases_setup());level.scheduled_frame=true;G_RunTimers();
    chained_releases_check();
}

TEST(wc3_unit_releases, post_entity_release_borrows_popped_deadline_for_nested_work) {
    T_ASSERT(chained_releases_setup());level.pathing_clock.time=1.25f;
    G_RunDeferredFrees();chained_releases_check();
    T_ASSERT(!level.timer_clock_valid);
}

typedef struct {edict_t *unit;float deadline;uint32_t sequence;} release_expected_t;
static int release_expected_compare(void const *a,void const *b) {
    release_expected_t const *x=a,*y=b;
    if(x->deadline!=y->deadline)return x->deadline<y->deadline ? -1 : 1;
    return x->sequence==y->sequence ? 0 : x->sequence<y->sequence ? -1 : 1;
}

TEST(wc3_unit_releases, large_mixed_heap_retains_keys_after_arbitrary_cancellations) {
    reset_entities();setup_test_world();release_expected_t expected[1024];uint32_t count=0;
    level.timer_sequence=UINT32_MAX-512;
    FOR_LOOP(i,1024) {
        edict_t *unit=G_Spawn();wc3Clock_t clock={.time=((i*37)%127)/128.0f,.span=300};
        G_DeferFreeEdictAt(unit,&clock);
        expected[count++]=(release_expected_t){unit,wc3_add(clock.time,G_ClockMinimumDelay()),level.timer_sequence};
    }
    /* Remove internal nodes in allocation order, exercising both sift directions. */
    FOR_LOOP(i,1024)if(i%7==0)G_FreeEdict(expected[i].unit);
    uint32_t retained=0;
    FOR_LOOP(i,count)if(expected[i].unit->inuse)expected[retained++]=expected[i];
    qsort(expected,retained,sizeof(*expected),release_expected_compare);
    FOR_LOOP(i,retained) {
        wc3Clock_t due;uint32_t serial;T_ASSERT(G_NextUnitRelease(&due,&serial));
        T_EQ(wc3_float_bits(due.time),wc3_float_bits(expected[i].deadline));T_EQ(serial,expected[i].sequence);
        T_ASSERT(G_IsDeferredFree(expected[i].unit));G_FireUnitRelease();T_ASSERT(!expected[i].unit->inuse);
    }
    wc3Clock_t due;uint32_t serial;T_ASSERT(!G_NextUnitRelease(&due,&serial));
}

static bool releases_timer_setup(bool timer_first) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};char script[1024];
    snprintf(script,sizeof(script),
        "globals\nunit u\ninteger seen=0\nendglobals\n"
        "function sample takes nothing returns nothing\nset seen=GetUnitTypeId(u)\nendfunction\n"
        "function main takes nothing returns nothing\nlocal timer t=CreateTimer()\n"
        "set u=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n%s\n%s\nendfunction\n"
        "function check takes nothing returns nothing\n"
        "call BJassAssert(seen==%s,\"unit and timer releases share deadline serial order\")\nendfunction\n",
        timer_first ? "call TimerStart(t,0.0,false,function sample)" : "call RemoveUnit(u)",
        timer_first ? "call RemoveUnit(u)" : "call TimerStart(t,0.0,false,function sample)",
        timer_first ? "'hfoo'" : "0");
    return run_test_jass(script);
}

TEST(wc3_unit_releases, tied_timer_registered_before_removal_observes_live_identity) {
    T_ASSERT(releases_timer_setup(true));level.scheduled_frame=true;G_RunTimers();
    jass_callbyname(level.vm,"check",false);T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_unit_releases, tied_timer_registered_after_removal_observes_released_identity) {
    T_ASSERT(releases_timer_setup(false));level.scheduled_frame=true;G_RunTimers();
    jass_callbyname(level.vm,"check",false);T_ASSERT(!jass_rterror_pending(level.vm));
}

#endif
