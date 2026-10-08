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

/* Loading must reconstruct logical requests; retaining this process's side heap
 * accidentally masks missing serialization on an ordinary warm round trip. */
TEST(wc3_unit_releases, cold_save_restores_absolute_deadlines_serials_and_callback_order) {
    T_ASSERT(releases_setup());wc3Clock_t saved,loaded;uint32_t serial,next;
    T_ASSERT(G_NextUnitRelease(&saved,&serial));uint32_t sequence=level.timer_sequence;
    cstring_t file="/tmp/wc3-request-release181.bin";T_ASSERT(WriteGame(file));
    G_ResetDeferredFrees();T_ASSERT(!G_NextUnitRelease(&loaded,&next));
    level.pathing_clock=(wc3Clock_t){.time=7,.epoch=2,.span=300};level.timer_sequence=900;
    T_ASSERT(ReadGame(file));T_ASSERT(G_NextUnitRelease(&loaded,&next));
    T_EQ(wc3_float_bits(loaded.time),wc3_float_bits(saved.time));T_EQ(loaded.epoch,saved.epoch);
    T_EQ(wc3_float_bits(loaded.span),wc3_float_bits(saved.span));T_EQ(next,serial);
    T_EQ(level.pathing_clock.time,1);T_EQ(level.pathing_clock.epoch,0u);T_EQ(level.timer_sequence,sequence);
    level.scheduled_frame=true;G_RunTimers();jass_callbyname(level.vm,"check_order",false);
    T_ASSERT(!jass_rterror_pending(level.vm));T_ASSERT(!G_NextUnitRelease(&loaded,&next));remove(file);
}

TEST(wc3_unit_releases, load_discards_process_requests_absent_from_snapshot) {
    reset_entities();setup_test_world();level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    edict_t *unit=G_Spawn();uint32_t slot=unit->s.number;
    cstring_t file="/tmp/wc3-request-empty181.bin";T_ASSERT(WriteGame(file));
    G_DeferFreeEdict(unit);T_ASSERT(G_IsDeferredFree(unit));T_ASSERT(ReadGame(file));
    T_ASSERT(g_edicts[slot].inuse);T_ASSERT(!G_IsDeferredFree(g_edicts+slot));
    level.scheduled_frame=true;G_RunTimers();T_ASSERT(g_edicts[slot].inuse);remove(file);
}

TEST(wc3_unit_releases, cold_restore_after_wrap_keeps_rebased_release_and_borrowed_clock) {
    reset_entities();setup_test_world();level.pathing_clock=(wc3Clock_t){.time=300,.span=300};
    edict_t *unit=G_Spawn();uint32_t slot=unit->s.number;G_DeferFreeEdict(unit);
    G_RebaseUnitReleases(300);level.pathing_clock=(wc3Clock_t){.epoch=1,.span=300};
    level.timer_clock=level.timer_source_clock=level.pathing_clock;level.timer_clock_valid=true;
    wc3Clock_t before,after;uint32_t serial,next;T_ASSERT(G_NextUnitRelease(&before,&serial));
    cstring_t file="/tmp/wc3-request-epoch181.bin";T_ASSERT(WriteGame(file));G_ResetDeferredFrees();
    T_ASSERT(ReadGame(file));T_ASSERT(G_NextUnitRelease(&after,&next));
    T_EQ(wc3_float_bits(after.time),wc3_float_bits(before.time));T_EQ(after.epoch,1u);T_EQ(next,serial);
    G_RunDeferredFrees();T_ASSERT(g_edicts[slot].inuse);
    level.scheduled_frame=true;G_RunTimers();T_ASSERT(!g_edicts[slot].inuse);remove(file);
}

/* A real timer callback creates a release during the old-epoch span drain.
 * The new request participates in the rebase and the remainder drain. */
TEST(wc3_unit_releases, span_callback_release_rebases_once_before_owner_and_remainder_drains) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    level.pathing_clock=(wc3Clock_t){.time=wc3_float(0x4395fffe),.span=300};
    T_ASSERT(run_test_jass(
        "globals\nunit u\ntimer child\nendglobals\n"
        "function span_action takes nothing returns nothing\n"
        "call RemoveUnit(u)\ncall TimerStart(child,0.125,false,null)\nendfunction\n"
        "function main takes nothing returns nothing\nlocal timer t=CreateTimer()\n"
        "set child=CreateTimer()\nset u=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        "call TimerStart(t,0.0,false,function span_action)\nendfunction\n"));
    level.timers[0].scalar_deadline.time=300;G_RebuildTimerQueue();
    wc3Clock_t target=level.pathing_clock;wc3_clock_advance(&target,wc3_float(0x3ba3d70a),0);
    level.scheduled_frame=true;G_RunTimersBeforePathOwner(&target);G_RunTimers();
    T_EQ(level.num_timers,2u);T_EQ(level.timers[1].scalar_deadline.epoch,1u);
    T_EQ(wc3_float_bits(level.timers[1].scalar_deadline.time),0x3e000000u);
    T_ASSERT(!find_test_unit(MAKEFOURCC('h','f','o','o')));T_ASSERT(!jass_rterror_pending(level.vm));
}
static abilityProc_t release_flush_parent;
static uint32_t release_flush_trace;
static wc3Clock_t release_flush_clock;
static intptr_t release_flush_proc(edict_t *unit,abilityMsg_t msg,abilityCall_t const *call) {
    if(msg==A_UNIT_REMOVE) {
        release_flush_trace=release_flush_trace*10+unit->user_data;release_flush_clock=G_TimerQueryClock(NULL);
    }
    return release_flush_parent(unit,msg,call);
}

/* Observe the outgoing owner independently of saved JASS globals, which the
 * load legitimately overwrites. The live fixture witnesses this teardown. */
TEST(wc3_unit_releases, load_flushes_only_old_owner_requests_within_point_two_seconds) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "local unit u\nlocal integer i=1\nloop\nexitwhen i>3\n"
        "set u=CreateUnit(Player(0),'hfoo',64.0*i,64.0,0.0)\n"
        "call UnitAddAbility(u,'Adef')\ncall SetUnitUserData(u,i)\nset i=i+1\nendloop\nendfunction\n"));
    cstring_t file="/tmp/wc3-request-flush181.bin";T_ASSERT(WriteGame(file));
    ability_t const *defend=FindAbilityByClassname("Adef");T_NOT_NULL(defend);if(!defend)return;
    release_flush_parent=defend->proc;release_flush_trace=0;
    S_ReplaceAbilityProcedure(defend,release_flush_proc);
    FILTER_EDICTS(unit,unit->class_id==MAKEFOURCC('h','f','o','o')) {
        wc3Clock_t clock=level.pathing_clock;clock.time=unit->user_data==1 ? 1 : unit->user_data==2 ? 1.1f : 1.3f;
        G_DeferFreeEdictAt(unit,&clock);
    }
    T_ASSERT(ReadGame(file));S_ReplaceAbilityProcedure(defend,release_flush_parent);
    T_EQ(release_flush_trace,12u);T_EQ(wc3_float_bits(release_flush_clock.time),wc3_float_bits(wc3_add(1.1f,G_ClockMinimumDelay())));
    wc3Clock_t due;uint32_t serial;T_ASSERT(!G_NextUnitRelease(&due,&serial));
    uint32_t active=0;FILTER_EDICTS(unit,unit->class_id==MAKEFOURCC('h','f','o','o')) {active++;T_ASSERT(!G_IsDeferredFree(unit));}
    T_EQ(active,3u);remove(file);
}
TEST(wc3_unit_releases, moving_clock_backwards_leaves_absolute_requests_pending) {
    T_ASSERT(releases_setup());level.pathing_clock.time=.5f;level.scheduled_frame=true;G_RunTimers();
    wc3Clock_t due;uint32_t serial;T_ASSERT(G_NextUnitRelease(&due,&serial));
    T_EQ(wc3_float_bits(due.time),wc3_float_bits(wc3_add(1,G_ClockMinimumDelay())));
    level.pathing_clock.time=1.5f;G_RunTimers();jass_callbyname(level.vm,"check_order",false);
    T_ASSERT(!jass_rterror_pending(level.vm));T_ASSERT(!G_NextUnitRelease(&due,&serial));
}

/* These are malformed logical records, not corrupt-checksum fixtures; the
 * owning reader must reject stale identities, duplicates and invalid clocks. */
TEST(wc3_unit_releases, logical_reader_rejects_invalid_payload_and_drops_partial_heap) {
    reset_entities();setup_test_world();edict_t *unit=G_Spawn();
    FOR_LOOP(mode,8) {
        FILE *file=tmpfile();T_NOT_NULL(file);if(!file)break;
        uint32_t count=mode==0 ? MAX_ENTITIES+1 : mode==7 ? 2 : 1;
        uint32_t fields[]={unit->s.number,unit->spawn_time,17};wc3Clock_t clock={.time=1,.span=300};
        if(mode==1)fields[0]=globals.num_edicts;
        if(mode==2)unit->inuse=false;
        if(mode==3)fields[1]++;
        if(mode==4)clock.time=NAN;
        if(mode==5)clock.span=0;
        if(mode==6)clock.span=INFINITY;
        T_EQ(fwrite(&count,sizeof(count),1,file),1u);
        FOR_LOOP(i,mode==7 ? 2 : 1) {
            T_EQ(fwrite(fields,sizeof(fields),1,file),1u);T_EQ(fwrite(&clock,sizeof(clock),1,file),1u);
        }
        rewind(file);T_ASSERT(!G_ReadUnitReleases(file));wc3Clock_t due;uint32_t serial;
        T_ASSERT(!G_NextUnitRelease(&due,&serial));unit->inuse=true;fclose(file);
    }
}
#endif
