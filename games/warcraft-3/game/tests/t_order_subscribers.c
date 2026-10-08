#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"
uint32_t G_TestSubscriberVisits(bool);

bool run_test_jass(cstring_t);
void setup_test_world(void);

#define SUBSCRIBER_GLOBALS \
    "globals\nunit u = null\ntrigger a = null\ntrigger b = null\ntrigger c = null\n" \
    "trigger d = null\ntrigger p = null\ninteger phase = 0\ninteger trace = 0\nboolean nested = false\nboolean inserted = false\nendglobals\n"
#define SUBSCRIBER_REGISTER \
    "function reg takes code handler returns trigger\n" \
    "local trigger t = CreateTrigger()\n" \
    "call TriggerRegisterUnitEvent(t, u, EVENT_UNIT_ISSUED_POINT_ORDER)\n" \
    "call TriggerAddAction(t, handler)\nreturn t\nendfunction\n"
#define SUBSCRIBER_BEGIN \
    "function main takes nothing returns nothing\n" \
    "set u = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"

static bool subscribers_run(cstring_t script) {
    reset_entities(); setup_test_world();
    g_edicts[0].client = game.clients;
    return run_test_jass(script);
}

TEST(wc3_order_subscribers, callbacks_are_synchronous_player_before_unit) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS
        "function first takes nothing returns nothing\nset trace = trace*10+1\nendfunction\n"
        "function second takes nothing returns nothing\nset trace = trace*10+2\nendfunction\n"
        SUBSCRIBER_REGISTER SUBSCRIBER_BEGIN
        "set b = reg(function second)\nset a = CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(a, Player(0), EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER, null)\n"
        "call TriggerAddAction(a, function first)\n"
        "call BJassAssert(IssuePointOrder(u, \"move\", 512.0, 64.0), \"point order accepted\")\n"
        "call BJassAssert(trace == 12, \"player and unit callbacks complete before native return\")\n"
        "call BJassAssert(GetTriggerEvalCount(a) == 1 and GetTriggerExecCount(a) == 1, \"player counts\")\n"
        "call BJassAssert(GetTriggerEvalCount(b) == 1 and GetTriggerExecCount(b) == 1, \"unit counts\")\n"
        "endfunction\n"));
    T_EQ(level.events.write, level.events.read);
}

TEST(wc3_order_subscribers, insertion_during_dispatch_waits_for_next_pass) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS
        "function third takes nothing returns nothing\nset trace = trace*10+3\nendfunction\n"
        SUBSCRIBER_REGISTER
        "function first takes nothing returns nothing\nset trace = trace*10+1\n"
        "if not inserted then\nset inserted = true\nset c = reg(function third)\nendif\nendfunction\n"
        "function second takes nothing returns nothing\nset trace = trace*10+2\nendfunction\n"
        SUBSCRIBER_BEGIN
        "set a = reg(function first)\nset b = reg(function second)\n"
        "call IssuePointOrder(u, \"move\", 512.0, 64.0)\n"
        "call BJassAssert(trace == 12, \"new subscriber is beyond outer sentinel\")\n"
        "set trace = 0\ncall IssuePointOrder(u, \"move\", 640.0, 64.0)\n"
        "call BJassAssert(trace == 123, \"next pass includes appended subscriber\")\nendfunction\n"));
}

TEST(wc3_order_subscribers, destroyed_later_trigger_is_suppressed_in_same_pass) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS
        "function first takes nothing returns nothing\nset trace = trace*10+1\ncall DestroyTrigger(b)\nendfunction\n"
        "function second takes nothing returns nothing\nset trace = trace*10+2\nendfunction\n"
        "function third takes nothing returns nothing\nset trace = trace*10+3\nendfunction\n"
        SUBSCRIBER_REGISTER SUBSCRIBER_BEGIN
        "set a = reg(function first)\nset b = reg(function second)\nset c = reg(function third)\n"
        "call IssuePointOrder(u, \"move\", 512.0, 64.0)\n"
        "call BJassAssert(trace == 13, \"destroy suppresses a later subscriber immediately\")\n"
        "call BJassAssert(GetTriggerEvalCount(b) == 0 and GetTriggerExecCount(b) == 0, \"suppressed trigger is never evaluated\")\n"
        "set trace = 0\ncall IssuePointOrder(u, \"move\", 640.0, 64.0)\n"
        "call BJassAssert(trace == 13, \"destroyed subscriber stays suppressed\")\nendfunction\n"));
}

TEST(wc3_order_subscribers, nested_pass_sees_insertions_and_keeps_outer_payload) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS
        "function third takes nothing returns nothing\nset trace = trace*10+3\n"
        "call BJassAssert(GetOrderPointX() == 640.0, \"new subscriber sees nested payload\")\nendfunction\n"
        SUBSCRIBER_REGISTER
        "function first takes nothing returns nothing\nset trace = trace*10+1\n"
        "if not nested then\nset nested = true\nset c = reg(function third)\n"
        "call IssuePointOrder(u, \"move\", 640.0, 64.0)\n"
        "call BJassAssert(GetOrderPointX() == 512.0, \"outer payload survives nested replacement\")\nendif\nendfunction\n"
        "function second takes nothing returns nothing\nset trace = trace*10+2\nendfunction\n"
        SUBSCRIBER_BEGIN
        "set a = reg(function first)\nset b = reg(function second)\n"
        "call IssuePointOrder(u, \"move\", 512.0, 64.0)\n"
        "call BJassAssert(trace == 11232, \"nested tail includes insertion; outer tail excludes it\")\nendfunction\n"));
}
TEST(wc3_order_subscribers, destroyed_self_reads_zero_counts_and_action_continues) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS
        "function first takes nothing returns nothing\ncall DestroyTrigger(a)\n"
        "call BJassAssert(GetTriggerEvalCount(a) == 0 and GetTriggerExecCount(a) == 0, \"destroyed self counts resolve to zero\")\n"
        "set trace = 1\nendfunction\n" SUBSCRIBER_REGISTER SUBSCRIBER_BEGIN
        "set a = reg(function first)\ncall IssuePointOrder(u, \"move\", 512.0, 64.0)\n"
        "call BJassAssert(trace == 1, \"destroy does not abort current action\")\nendfunction\n"));
}

TEST(wc3_order_subscribers, disabled_trigger_is_evaluated_without_execution) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS
        "function first takes nothing returns nothing\nset trace = trace+1\nendfunction\n"
        SUBSCRIBER_REGISTER SUBSCRIBER_BEGIN
        "set a = reg(function first)\ncall DisableTrigger(a)\ncall IssuePointOrder(u, \"move\", 512.0, 64.0)\n"
        "call BJassAssert(trace == 0 and GetTriggerEvalCount(a) == 1 and GetTriggerExecCount(a) == 0, \"disabled eval without action\")\n"
        "call EnableTrigger(a)\ncall IssuePointOrder(u, \"move\", 640.0, 64.0)\n"
        "call BJassAssert(trace == 1 and GetTriggerEvalCount(a) == 2 and GetTriggerExecCount(a) == 1, \"enabled counts continue\")\nendfunction\n"));
}

TEST(wc3_order_subscribers, family_presence_is_frozen_before_player_dispatch) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS
        "function second takes nothing returns nothing\nset trace = trace*10+2\nendfunction\n"
        SUBSCRIBER_REGISTER
        "function first takes nothing returns nothing\nset trace = trace*10+1\n"
        "if not inserted then\nset inserted = true\nset b = reg(function second)\nendif\nendfunction\n"
        SUBSCRIBER_BEGIN "set a = CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(a, Player(0), EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER, null)\n"
        "call TriggerAddAction(a, function first)\ncall IssuePointOrder(u, \"move\", 512.0, 64.0)\n"
        "call BJassAssert(trace == 1, \"first unit subscriber cannot change frozen presence\")\n"
        "set trace = 0\ncall IssuePointOrder(u, \"move\", 640.0, 64.0)\n"
        "call BJassAssert(trace == 12, \"next producer sees unit family\")\nendfunction\n"));
}

static void subscribers_drain(void) {
    level.scheduled_frame=true;G_RunTimers();
}

static edict_t *subscribers_actor(void) {
    FILTER_EDICTS(unit, unit->class_id == MAKEFOURCC('h','f','o','o')) return unit;
    return NULL;
}

static bool subscribers_persistent_setup(void) {
    return subscribers_run(SUBSCRIBER_GLOBALS
        "function first takes nothing returns nothing\nset trace = trace*10+1\nendfunction\n"
        "function second takes nothing returns nothing\nset trace = trace*10+2\nendfunction\n"
        "function third takes nothing returns nothing\nset trace = trace*10+3\nendfunction\n"
        "function fourth takes nothing returns nothing\nset trace = trace*10+4\nendfunction\n"
        SUBSCRIBER_REGISTER
        "function destroy takes nothing returns nothing\ncall DestroyTrigger(b)\ncall DestroyTrigger(b)\nendfunction\n"
        "function append takes nothing returns nothing\nset b = reg(function fourth)\nendfunction\n"
        "function check takes nothing returns nothing\n"
        "call BJassAssert(trace == 134, \"reused lowest slot is delivered at tail\")\nendfunction\n"
        SUBSCRIBER_BEGIN "set a = reg(function first)\nset b = reg(function second)\nset c = reg(function third)\nendfunction\n");
}

TEST(wc3_order_subscribers, deferred_cleanup_reuses_slot_in_registration_order) {
    T_ASSERT(subscribers_persistent_setup());edict_t *unit=subscribers_actor();T_NOT_NULL(unit);if(!unit)return;
    jass_callbyname(level.vm,"destroy",false);
    trigger_t *trigger=level.triggers+1;event_t *event=level.events.handlers+1;
    T_ASSERT(trigger->destroyed);T_ASSERT(trigger->release_pending);T_ASSERT(event->inuse);
    wc3Clock_t deadline;uint32_t serial;
    T_ASSERT(G_NextTriggerRelease(&deadline,&serial));T_EQ(serial,trigger->release_sequence);
    T_EQ(wc3_float_bits(deadline.time),wc3_float_bits(G_ClockMinimumDelay()));
    subscribers_drain();T_ASSERT(!trigger->release_pending);T_ASSERT(!event->inuse);
    T_EQ(event->handle_generation,1);T_ASSERT(trigger->actions==NULL);
    T_ASSERT(!G_NextTriggerRelease(&deadline,&serial));
    jass_callbyname(level.vm,"append",false);T_ASSERT(event->inuse);
    T_EQ(event->trigger,level.triggers+3);
    T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){512,64}));
    jass_callbyname(level.vm,"check",false);T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_order_subscribers, saved_rank_and_pending_release_rebuild_exactly) {
    T_ASSERT(subscribers_persistent_setup());jass_callbyname(level.vm,"destroy",false);
    uint64_t rank=level.events.registration_sequence;uint32_t serial=level.triggers[1].release_sequence;
    wc3Clock_t deadline=level.triggers[1].release_deadline;
    char const *save="/tmp/wc3-subscriber-pending.bin";
    T_ASSERT(WriteGame(save));subscribers_drain();
    T_ASSERT(ReadGame(save));remove(save);
    T_EQ(level.events.registration_sequence,rank);T_EQ(level.triggers[1].release_sequence,serial);
    T_EQ(wc3_float_bits(level.triggers[1].release_deadline.time),wc3_float_bits(deadline.time));
    T_ASSERT(level.triggers[1].release_pending);subscribers_drain();
    jass_callbyname(level.vm,"append",false);
    edict_t *unit=subscribers_actor();T_NOT_NULL(unit);if(!unit)return;
    T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){512,64}));
    jass_callbyname(level.vm,"check",false);T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(WriteGame(save));T_ASSERT(ReadGame(save));remove(save);
    unit=subscribers_actor();T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){640,64}));
    T_EQ(level.triggers[3].evaluations,2);T_EQ(level.triggers[3].executions,2);
}

TEST(wc3_order_subscribers, unrelated_registrations_do_not_force_global_scan) {
    T_ASSERT(subscribers_persistent_setup());edict_t *unit=subscribers_actor();T_NOT_NULL(unit);if(!unit)return;
    T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){512,64}));
    FOR_LOOP(i,900) {
        event_t *event=G_MakeEvent((EVENTTYPE)(EVENT_UNIT_ISSUED_POINT_ORDER+1+i));
        T_NOT_NULL(event);if(!event)return;
        G_SetEventSubject(event,unit);G_SetEventTrigger(event,level.triggers);
    }
    G_TestSubscriberVisits(true);
    T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){640,64}));
    T_EQ(G_TestSubscriberVisits(false),3);
}
static bool subscribers_retail_scene(bool reverse) {
    char script[8192];
    snprintf(script,sizeof(script),SUBSCRIBER_GLOBALS
        "function fifth takes nothing returns nothing\nset trace = trace*10+5\nendfunction\n"
        SUBSCRIBER_REGISTER
        "function first takes nothing returns nothing\nset trace = trace*10+1\n"
        "if phase == 2 then\nset c = reg(function fifth)\ncall DestroyTrigger(d)\nendif\nendfunction\n"
        "function second takes nothing returns nothing\nset trace = trace*10+2\n"
        "if phase == 2 then\ncall DestroyTrigger(b)\n"
        "call BJassAssert(GetTriggerEvalCount(b) == 0 and GetTriggerExecCount(b) == 0, \"destroyed counts zero during action\")\nendif\nendfunction\n"
        "function third takes nothing returns nothing\nset trace = trace*10+3\nendfunction\n"
        "function fourth takes nothing returns nothing\nset trace = trace*10+4\n"
        "if phase == 2 then\ncall DestroyTrigger(a)\nendif\nendfunction\n"
        "function player takes nothing returns nothing\nset trace = trace*10+9\nendfunction\n"
        "function phase3 takes nothing returns nothing\nset trace = 0\nset phase = 3\n"
        "call IssuePointOrder(u, \"move\", 768.0, 64.0)\n"
        "call BJassAssert(trace == %d, \"retail phase3 trace after release\")\n"
        "call BJassAssert(GetTriggerEvalCount(p) == 3, \"player delivered in every phase\")\nendfunction\n"
        SUBSCRIBER_BEGIN "%s\nset p = CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(p, Player(0), EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER, null)\n"
        "call TriggerAddAction(p, function player)\nset phase = 1\n"
        "call IssuePointOrder(u, \"move\", 512.0, 64.0)\n"
        "call BJassAssert(trace == %d, \"retail phase1 registration order\")\n"
        "set trace = 0\nset phase = 2\ncall IssuePointOrder(u, \"move\", 640.0, 64.0)\n"
        "call BJassAssert(trace == %d, \"retail phase2 insert/destroy order\")\nendfunction\n",
        reverse ? 943 : 945,
        reverse ? "set c = reg(function fourth)\nset d = reg(function third)\nset b = reg(function second)\nset a = reg(function first)" :
                  "set a = reg(function first)\nset b = reg(function second)\nset d = reg(function third)\nset c = reg(function fourth)",
        reverse ? 94321 : 91234,reverse ? 9432 : 9124);
    return subscribers_run(script);
}

TEST(wc3_order_subscribers, retail_forward_insert_destroy_scene) {
    T_ASSERT(subscribers_retail_scene(false));subscribers_drain();
    jass_callbyname(level.vm,"phase3",false);T_ASSERT(!jass_rterror_pending(level.vm));
}
TEST(wc3_order_subscribers, retail_reverse_insert_destroy_scene) {
    T_ASSERT(subscribers_retail_scene(true));subscribers_drain();
    jass_callbyname(level.vm,"phase3",false);T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_order_subscribers, retired_response_cannot_target_reused_registration) {
    T_ASSERT(subscribers_persistent_setup());
    event_t *old=G_MakeEvent(EVENT_GAME_VARIABLE_LIMIT);T_NOT_NULL(old);if(!old)return;
    G_SetEventTrigger(old,level.triggers+1);
    G_PublishEventResponse(NULL,EVENT_GAME_VARIABLE_LIMIT,old);
    G_TriggerRequestDestroy(level.triggers+1,NULL);subscribers_drain();
    T_NOT_NULL(G_MakeEvent(EVENT_GAME_VARIABLE_LIMIT));
    event_t *replacement=G_MakeEvent(EVENT_GAME_VARIABLE_LIMIT);T_EQ(replacement,old);
    trigger_t *trigger=G_AllocJassTrigger();G_SetEventTrigger(replacement,trigger);
    G_RunEvents();jass_runevents(level.vm);
    T_EQ(trigger->evaluations,0);T_EQ(trigger->executions,0);
}

TEST(wc3_order_subscribers, registrations_of_other_events_during_dispatch_keep_order) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS SUBSCRIBER_REGISTER
        "function first takes nothing returns nothing\nlocal integer i = 54\n"
        "set trace = trace*10+1\nif not inserted then\nset inserted = true\nloop\nexitwhen i > 74\n"
        "set c = CreateTrigger()\ncall TriggerRegisterUnitEvent(c, u, ConvertUnitEvent(i))\nset i = i+1\nendloop\nendif\nendfunction\n"
        "function second takes nothing returns nothing\nset trace = trace*10+2\nendfunction\n"
        SUBSCRIBER_BEGIN "set a = reg(function first)\nset b = reg(function second)\n"
        "call IssuePointOrder(u, \"move\", 512.0, 64.0)\n"
        "call BJassAssert(trace == 12, \"other-event registrations do not disturb current iterator\")\n"
        "set trace = 0\ncall IssuePointOrder(u, \"move\", 640.0, 64.0)\n"
        "call BJassAssert(trace == 12 and GetTriggerExecCount(b) == 2, \"delivery order persists after registration growth\")\nendfunction\n"));
}

TEST(wc3_order_subscribers, pending_release_rebase_retains_request_tie_order) {
    T_ASSERT(subscribers_persistent_setup());
    level.pathing_clock.time=299.999969482421875f;level.timer_clock_valid=false;
    G_TriggerRequestDestroy(level.triggers+1,NULL);G_TriggerRequestDestroy(level.triggers,NULL);
    G_TriggerRequestDestroy(level.triggers+2,NULL);
    float original=level.triggers[1].release_deadline.time;
    uint32_t first=level.triggers[1].release_sequence;
    G_RebaseTriggerReleases(300.0f);
    T_EQ(wc3_float_bits(level.triggers[1].release_deadline.time),wc3_float_bits(wc3_sub(original,300.0f)));
    T_EQ(level.triggers[1].release_deadline.epoch,1);
    wc3Clock_t due;uint32_t serial;
    FOR_LOOP(i,3) {
        T_ASSERT(G_NextTriggerRelease(&due,&serial));T_EQ(serial,first+i);
        G_FireTriggerRelease();
        T_ASSERT(!level.triggers[i==0 ? 1 : i==1 ? 0 : 2].release_pending);
    }
    T_ASSERT(!G_NextTriggerRelease(&due,&serial));
}

TEST(wc3_order_subscribers, timer_callback_cleanup_uses_fired_clock_not_drain_limit) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS
        "function expire takes nothing returns nothing\ncall DestroyTrigger(a)\nendfunction\n"
        SUBSCRIBER_BEGIN "set a = CreateTrigger()\n"
        "call TriggerRegisterUnitEvent(a, u, EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call TimerStart(CreateTimer(), 0.002, false, function expire)\nendfunction\n"));
    float fired=level.timers[0].scalar_deadline.time;
    subscribers_drain();
    T_ASSERT(level.triggers[0].destroyed);T_ASSERT(!level.triggers[0].release_pending);
    T_EQ(wc3_float_bits(level.triggers[0].release_deadline.time),wc3_float_bits(wc3_add(fired,G_ClockMinimumDelay())));
    T_ASSERT(!level.events.handlers[0].inuse);
}

TEST(wc3_order_subscribers, physical_cleanup_releases_owned_variable_name) {
    T_ASSERT(subscribers_run(SUBSCRIBER_GLOBALS SUBSCRIBER_BEGIN
        "set a = CreateTrigger()\ncall TriggerRegisterVariableEvent(a, \"trace\", EQUAL, 100.0)\nendfunction\n"));
    event_t *event=level.events.handlers;T_NOT_NULL(event->variable);
    T_STREQ(event->variable,"trace");
    G_TriggerRequestDestroy(level.triggers,NULL);subscribers_drain();
    T_ASSERT(!event->inuse);T_ASSERT(event->variable==NULL);
}

#endif
