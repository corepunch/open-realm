#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"
#include "jass/jass.h"

bool run_test_jass(cstring_t);
void setup_test_world(void);

static bool range_setup_at(bool inside,bool mutate,float time) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    level.pathing_clock=(wc3Clock_t){.time=time,.span=300};
    char script[4096];snprintf(script,sizeof(script),
        "globals\nunit center\nunit entrant\ntrigger first\ntrigger peer\ntrigger child\n"
        "integer trace=0\ntimer follow\nendglobals\n"
        "function child_action takes nothing returns nothing\nset trace=trace*10+3\nendfunction\n"
        "function peer_action takes nothing returns nothing\nset trace=trace*10+2\nendfunction\n"
        "function first_action takes nothing returns nothing\nset trace=trace*10+1\n%s\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "set follow=CreateTimer()\nset first=CreateTrigger()\nset peer=CreateTrigger()\n"
        "set center=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        "set entrant=CreateUnit(Player(0),'hpea',%s,64.0,0.0)\n"
        "call TriggerRegisterUnitInRange(first,center,150.0,null)\n"
        "call TriggerAddAction(first,function first_action)\n"
        "call TriggerRegisterUnitInRange(peer,center,150.0,null)\n"
        "call TriggerAddAction(peer,function peer_action)\nendfunction\n"
        "function enter takes nothing returns nothing\ncall SetUnitPosition(entrant,128.0,64.0)\nendfunction\n"
        "function leave takes nothing returns nothing\ncall SetUnitPosition(entrant,512.0,64.0)\nendfunction\n"
        "function replace_entrant takes nothing returns nothing\n"
        "set entrant=CreateUnit(Player(0),'hpea',128.0,64.0,0.0)\nendfunction\n"
        "function check_reentry takes nothing returns nothing\n"
        "call BJassAssert(trace==1212,\"left occupant was not admitted again\")\nendfunction\n"
        "function destroy_all takes nothing returns nothing\ncall DestroyTrigger(first)\ncall DestroyTrigger(peer)\nendfunction\n"
        "function check_empty takes nothing returns nothing\n"
        "call BJassAssert(trace==0,\"range event fired before listener deadline\")\nendfunction\n"
        "function check_pair takes nothing returns nothing\n"
        "call BJassAssert(trace==12,\"listeners do not retain registration tie order\")\nendfunction\n"
        "function check_first takes nothing returns nothing\n"
        "call BJassAssert(trace==1,\"destroyed tied peer dispatched its action\")\nendfunction\n"
        "function check_child takes nothing returns nothing\n"
        "call BJassAssert(trace==13,\"callback-created listener lost the creator deadline phase\")\nendfunction\n",
        mutate ? "call DestroyTrigger(peer)\nset child=CreateTrigger()\n"
            "call TriggerRegisterUnitInRange(child,center,150.0,null)\n"
            "call TriggerAddAction(child,function child_action)\ncall TimerStart(follow,0.0,false,null)" : "",
        inside ? "128.0" : "512.0");
    return run_test_jass(script);
}

static bool range_setup(bool inside,bool mutate) {return range_setup_at(inside,mutate,1);}

/* The real primary drain looks one quantum ahead; these supplied frame clocks
 * put its target before or after the original registration deadline. */
static void range_advance(float frame_clock) {
    level.pathing_clock.time=frame_clock;level.scheduled_frame=true;G_RunTimers();
}
static void range_check(cstring_t name) {
    jass_callbyname(level.vm,name,false);T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_range_listeners, position_changes_wait_for_registration_phase) {
    T_ASSERT(range_setup(false,false));jass_callbyname(level.vm,"enter",false);
    G_RunEvents();jass_runevents(level.vm);range_check("check_empty");
    range_advance(1.11f);range_check("check_empty");
    range_advance(1.125f);range_check("check_pair");
}

TEST(wc3_range_listeners, first_poll_detects_existing_occupants_without_movement) {
    T_ASSERT(range_setup(true,false));range_advance(1.125f);range_check("check_pair");
    range_advance(1.5f);range_check("check_pair");
}

TEST(wc3_range_listeners, callback_destroy_and_registration_share_the_popped_deadline) {
    T_ASSERT(range_setup(true,true));range_advance(1.125f);range_check("check_first");
    T_EQ(level.num_timers,1);
    T_EQ(wc3_float_bits(level.timers[0].scalar_deadline.time),wc3_float_bits(wc3_add(1.125f,G_ClockMinimumDelay())));
    range_advance(1.25f);range_check("check_child");
    range_advance(1.5f);range_check("check_child");
}

TEST(wc3_range_listeners, late_drain_catches_up_without_changing_repeating_serials) {
    T_ASSERT(range_setup(true,false));wc3Clock_t due;uint32_t serial;
    T_ASSERT(G_NextRangeRequest(&due,&serial));uint32_t first=serial;
    float next=due.time;
    while(next<=wc3_add(2.0f,wc3_float(0x3ba3d70a)))next=wc3_add(next,.125f);
    range_advance(2);range_check("check_pair");
    T_ASSERT(G_NextRangeRequest(&due,&serial));T_EQ(serial,first);
    T_EQ(wc3_float_bits(due.time),wc3_float_bits(next));
    jass_callbyname(level.vm,"leave",false);range_advance(2.125f);
    jass_callbyname(level.vm,"enter",false);range_advance(2.25f);range_check("check_reentry");
}

TEST(wc3_range_listeners, moving_prediction_uses_listener_deadline_not_frame_target) {
    T_ASSERT(range_setup(false,false));edict_t *unit=find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(unit);if(!unit)return;
    /* At popped1.125 inside; at the frame target1.145 outside. */
    unit->s.origin2=(vec2_t){128,64};G_MarkMoveSpatialObject(unit);
    unit->movement.clock_valid=true;unit->movement.pose_clock=(wc3Clock_t){.time=1,.span=300};
    unit->movement.velocity=(vec2_t){768,0};
    range_advance(1.14f);range_check("check_pair");
    T_EQ(unit->s.origin2.x,128);T_EQ(unit->movement.pose_clock.time,1);
}

TEST(wc3_range_listeners, cold_save_restores_phase_serials_and_retained_occupants) {
    T_ASSERT(range_setup(true,false));range_advance(1.125f);range_check("check_pair");
    wc3Clock_t before,after;uint32_t serial,loaded;
    T_ASSERT(G_NextRangeRequest(&before,&serial));
    cstring_t file=Test_TempPath("wc3-range-listener180.bin");
    T_ASSERT(WriteGame(file));G_ResetRangeListeners();T_ASSERT(!G_NextRangeRequest(&after,&loaded));
    T_ASSERT(ReadGame(file));T_ASSERT(G_NextRangeRequest(&after,&loaded));
    T_EQ(wc3_float_bits(before.time),wc3_float_bits(after.time));T_EQ(serial,loaded);
    range_advance(1.5f);range_check("check_pair");
    jass_callbyname(level.vm,"leave",false);range_advance(1.625f);
    jass_callbyname(level.vm,"enter",false);range_advance(1.75f);range_check("check_reentry");
    remove(file);
}

TEST(wc3_range_listeners, release_chain_keeps_poll_alive_until_final_listener_release) {
    T_ASSERT(range_setup(true,false));jass_callbyname(level.vm,"destroy_all",false);
    wc3Clock_t due;uint32_t serial;
    T_ASSERT(G_NextTriggerRelease(&due,&serial));
    level.timer_source_clock=level.pathing_clock;level.timer_clock_valid=true;level.timer_clock=due;
    G_FireTriggerRelease();
    wc3Clock_t bridge;T_ASSERT(G_NextRangeRequest(&bridge,&serial));
    T_EQ(wc3_float_bits(bridge.time),wc3_float_bits(wc3_add(due.time,G_ClockMinimumDelay())));
    level.timer_clock=bridge;G_FireRangeRequest();
    wc3Clock_t listener;T_ASSERT(G_NextRangeRequest(&listener,&serial));
    T_EQ(wc3_float_bits(listener.time),wc3_float_bits(wc3_add(bridge.time,G_ClockMinimumDelay())));
    level.timer_clock=listener;G_FireRangeRequest();
    /* Peer still owns its ordinary timer until its own chain is drained. */
    T_ASSERT(G_NextRangeRequest(&due,&serial));T_EQ(wc3_float_bits(due.time),wc3_float_bits(wc3_add(1,.125f)));
    range_advance(1.125f);T_ASSERT(!G_NextRangeRequest(&due,&serial));range_check("check_empty");
}

TEST(wc3_range_listeners, authored_radius_uses_candidate_collision_and_strict_boundary) {
    T_ASSERT(range_setup(false,false));edict_t *unit=find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(unit);if(!unit)return;
    unit->collision=16;unit->s.origin2=(vec2_t){230,64};G_MarkMoveSpatialObject(unit);
    range_advance(1.125f);range_check("check_empty");
    /* Radius150 + candidate16; source collision contributes nothing. */
    unit->s.origin2.x=229;G_MarkMoveSpatialObject(unit);
    range_advance(1.25f);range_check("check_pair");
}

TEST(wc3_range_listeners, entrant_identity_reuse_does_not_inherit_old_membership) {
    T_ASSERT(range_setup(true,false));range_advance(1.125f);range_check("check_pair");
    edict_t *unit=find_test_unit(MAKEFOURCC('h','p','e','a'));T_NOT_NULL(unit);if(!unit)return;
    uint32_t spawn=unit->spawn_time;
    G_FreeEdict(unit);level.time+=1001;
    jass_callbyname(level.vm,"replace_entrant",false);
    T_ASSERT(unit->inuse);T_NE(unit->spawn_time,spawn);
    range_advance(1.25f);range_check("check_reentry");
}

TEST(wc3_range_listeners, destroyed_self_cannot_repeat_actions_or_leak_requests) {
    reset_entities();setup_test_world();level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(run_test_jass(
        "globals\ntrigger t\ninteger fires=0\nunit a\nunit b\nendglobals\n"
        "function on_enter takes nothing returns nothing\nset fires=fires+1\ncall DestroyTrigger(t)\nendfunction\n"
        "function main takes nothing returns nothing\nset t=CreateTrigger()\n"
        "set a=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        "set b=CreateUnit(Player(0),'hpea',128.0,64.0,0.0)\n"
        "call TriggerRegisterUnitInRange(t,a,150.0,null)\ncall TriggerAddAction(t,function on_enter)\nendfunction\n"
        "function verify takes nothing returns nothing\ncall BJassAssert(fires==1,\"destroyed self ran again\")\nendfunction\n"));
    range_advance(1.5f);range_check("verify");wc3Clock_t due;uint32_t serial;
    T_ASSERT(!G_NextRangeRequest(&due,&serial));T_ASSERT(!G_NextTriggerRelease(&due,&serial));
}

TEST(wc3_range_listeners, reset_drops_pending_range_work) {
    T_ASSERT(range_setup(true,false));jass_callbyname(level.vm,"destroy_all",false);
    reset_entities();wc3Clock_t due;uint32_t serial;
    T_ASSERT(!G_NextRangeRequest(&due,&serial));
}

TEST(wc3_range_listeners, candidate_cell_order_and_reverse_enter_order_are_preserved) {
    reset_entities();setup_test_world();level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(run_test_jass(
        "globals\nunit a\nunit x\nunit y\ninteger trace=0\nendglobals\n"
        "function enter takes nothing returns nothing\n"
        "if GetTriggerUnit()==x then\nset trace=trace*10+1\nelse\nset trace=trace*10+2\nendif\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "set a=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        "set y=CreateUnit(Player(0),'hpea',64.0,320.0,0.0)\n"
        "set x=CreateUnit(Player(0),'hpea',400.0,64.0,0.0)\n"
        "call TriggerRegisterUnitInRange(t,a,450.0,null)\ncall TriggerAddAction(t,function enter)\nendfunction\n"
        "function verify takes nothing returns nothing\n"
        "call BJassAssert(trace==21,\"range cell order changed or notifications were not reversed\")\nendfunction\n"));
    range_advance(1.125f);range_check("verify");
}

TEST(wc3_range_listeners, dense_4096_occupants_are_retained_once_and_reconciled_after_removal) {
    reset_entities();setup_test_world();level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(run_test_jass(
        "globals\ninteger fires=0\nendglobals\n"
        "function enter takes nothing returns nothing\nset fires=fires+1\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "local unit a=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        "call TriggerRegisterUnitInRange(t,a,150.0,null)\ncall TriggerAddAction(t,function enter)\nendfunction\n"
        "function verify takes nothing returns nothing\ncall BJassAssert(fires==4096,\"dense range lost or repeated entrants\")\nendfunction\n"));
    edict_t *units[4096];
    FOR_LOOP(i,4096) {
        units[i]=alloc_test_unit(MAKEFOURCC('h','p','e','a'),128,64);
        units[i]->s.model=1;units[i]->svflags|=SVF_MONSTER;units[i]->collision=16;
        G_MarkMoveSpatialObject(units[i]);
    }
    range_advance(1.125f);range_check("verify");range_advance(1.25f);range_check("verify");
    for(uint32_t i=0;i<4096;i+=2)G_FreeEdict(units[i]);
    range_advance(1.5f);range_check("verify");
    cstring_t file=Test_TempPath("wc3-dense-range180.bin");T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
    range_advance(1.625f);range_check("verify");remove(file);
}

TEST(wc3_range_listeners, filtered_occupant_is_retained_until_it_leaves_and_reenters) {
    reset_entities();setup_test_world();level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(run_test_jass(
        "globals\nunit a\nunit b\nboolean accept=false\ninteger fires=0\ninteger filters=0\nendglobals\n"
        "function eligible takes nothing returns boolean\n"
        "call BJassAssert(GetFilterUnit()==b,\"range filter context lost entrant\")\n"
        "set filters=filters+1\nreturn accept\nendfunction\n"
        "function enter takes nothing returns nothing\nset fires=fires+1\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "set a=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        "set b=CreateUnit(Player(0),'hpea',128.0,64.0,0.0)\n"
        "call TriggerRegisterUnitInRange(t,a,150.0,Filter(function eligible))\n"
        "call TriggerAddAction(t,function enter)\nendfunction\n"
        "function enable takes nothing returns nothing\nset accept=true\nendfunction\n"
        "function leave takes nothing returns nothing\ncall SetUnitPosition(b,512.0,64.0)\nendfunction\n"
        "function return_inside takes nothing returns nothing\ncall SetUnitPosition(b,128.0,64.0)\nendfunction\n"
        "function rejected takes nothing returns nothing\ncall BJassAssert(fires==0 and filters==1,\"range filter ran again while retained\")\nendfunction\n"
        "function admitted takes nothing returns nothing\ncall BJassAssert(fires==1 and filters==2,\"filtered occupant did not enter after leaving\")\nendfunction\n"));
    range_advance(1.125f);range_check("rejected");jass_callbyname(level.vm,"enable",false);
    range_advance(1.25f);range_check("rejected");jass_callbyname(level.vm,"leave",false);range_advance(1.375f);
    jass_callbyname(level.vm,"return_inside",false);range_advance(1.5f);range_check("admitted");
}

TEST(wc3_range_listeners, cold_save_restores_partially_completed_release_chain) {
    T_ASSERT(range_setup(true,false));jass_callbyname(level.vm,"destroy_all",false);
    wc3Clock_t due;uint32_t serial;T_ASSERT(G_NextTriggerRelease(&due,&serial));
    level.timer_source_clock=level.pathing_clock;level.timer_clock_valid=true;level.timer_clock=due;
    G_FireTriggerRelease();wc3Clock_t before;uint32_t saved;
    T_ASSERT(G_NextRangeRequest(&before,&saved));
    cstring_t file=Test_TempPath("wc3-range-release180.bin");T_ASSERT(WriteGame(file));G_ResetRangeListeners();
    T_ASSERT(ReadGame(file));T_ASSERT(G_NextRangeRequest(&due,&serial));
    T_EQ(wc3_float_bits(due.time),wc3_float_bits(before.time));T_EQ(serial,saved);
    range_advance(1.5f);range_check("check_empty");T_ASSERT(!G_NextRangeRequest(&due,&serial));remove(file);
}

TEST(wc3_range_listeners, zero_radius_candidate_is_excluded_until_it_has_positive_collision) {
    T_ASSERT(range_setup(true,false));edict_t *unit=find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(unit);if(!unit)return;
    unit->collision=0;G_MarkMoveSpatialObject(unit);range_advance(1.125f);range_check("check_empty");
    unit->collision=16;G_MarkMoveSpatialObject(unit);range_advance(1.25f);range_check("check_pair");
}

TEST(wc3_range_listeners, full_legacy_queue_does_not_drop_primary_listener_callbacks) {
    T_ASSERT(range_setup(true,false));level.events.read=0;level.events.write=MAX_EVENT_QUEUE;
    range_advance(1.125f);range_check("check_pair");
    T_EQ(level.events.write,(uint32_t)MAX_EVENT_QUEUE);T_EQ(level.events.read,0u);
}
/* Labelled supplied-clock reproduction of the live wrap, retaining its exact
 * last pre-wrap poll and software-truncated rearm words. */
TEST(wc3_range_listeners, span_poll_rearms_before_rebase_and_keeps_live_retail_words) {
    T_ASSERT(range_setup_at(true,false,wc3_float(0x4395efff)));
    wc3Clock_t due;uint32_t serial,initial;T_ASSERT(G_NextRangeRequest(&due,&initial));
    T_EQ(wc3_float_bits(due.time),0x4395ffffu);
    range_advance(wc3_float(0x4395fffe));range_check("check_pair");
    T_ASSERT(G_NextRangeRequest(&due,&serial));T_EQ(due.epoch,1u);T_EQ(serial,initial);
    T_EQ(wc3_float_bits(due.time),0x3dfff000u);
    /* Commit the quantum as G_RunFrame does after the merged request drain. */
    wc3_clock_advance(&level.pathing_clock,wc3_float(0x3ba3d70a),0);
    T_EQ(wc3_float_bits(level.pathing_clock.time),0x3ba10000u);
    range_advance(wc3_float(0x3dfff000));range_check("check_pair");
    T_ASSERT(G_NextRangeRequest(&due,&serial));T_EQ(serial,initial);
    T_EQ(wc3_float_bits(due.time),0x3e7ff800u);T_EQ(due.epoch,1u);
}

TEST(wc3_range_listeners, cold_ui_load_keeps_absolute_retail_poll_deadline_and_serial) {
    T_ASSERT(range_setup_at(true,false,wc3_float(0x41efffff)));
    level.pathing_clock.time=wc3_float(0x41f05e1f);
    wc3Clock_t before,after;uint32_t saved,serial;T_ASSERT(G_NextRangeRequest(&before,&saved));
    T_EQ(wc3_float_bits(before.time),0x41f0ffffu);
    cstring_t file=Test_TempPath("wc3-request-range181.bin");T_ASSERT(WriteGame(file));G_ResetRangeListeners();
    level.pathing_clock=(wc3Clock_t){.time=70,.epoch=1,.span=300};
    T_ASSERT(ReadGame(file));T_ASSERT(G_NextRangeRequest(&after,&serial));
    T_EQ(wc3_float_bits(level.pathing_clock.time),0x41f05e1fu);
    T_EQ(wc3_float_bits(after.time),0x41f0ffffu);T_EQ(after.epoch,before.epoch);T_EQ(serial,saved);
    range_advance(wc3_float(0x41f08000));range_check("check_empty");
    range_advance(wc3_float(0x41f0ffff));range_check("check_pair");remove(file);
}
TEST(wc3_range_listeners, first_timer_allocated_in_callback_keeps_the_popped_clock) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(run_test_jass("globals\ntimer first\nendglobals\n"
        "function enter takes nothing returns nothing\nset first=CreateTimer()\n"
        "call TimerStart(first,0.0,false,null)\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "local unit a=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        "local unit b=CreateUnit(Player(0),'hpea',128.0,64.0,0.0)\n"
        "call TriggerRegisterUnitInRange(t,a,150.0,null)\ncall TriggerAddAction(t,function enter)\nendfunction\n"));
    range_advance(1.14f);T_EQ(level.num_timers,1u);
    T_EQ(wc3_float_bits(level.timers[0].scalar_deadline.time),wc3_float_bits(wc3_add(1.125f,G_ClockMinimumDelay())));
    T_ASSERT(!jass_rterror_pending(level.vm));
}
#endif
