#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"

bool run_test_jass(cstring_t);
void setup_test_world(void);

#define REENTRY_GLOBALS \
    "globals\nunit u=null\ntrigger p=null\ntrigger a=null\ntrigger b=null\n" \
    "integer trace=0\nboolean nested=false\nendglobals\n"
#define REENTRY_REGISTER \
    "function reg takes code action, unitevent ev returns trigger\n" \
    "local trigger t=CreateTrigger()\ncall TriggerRegisterUnitEvent(t,u,ev)\n" \
    "call TriggerAddAction(t,action)\nreturn t\nendfunction\n"
#define REENTRY_BEGIN \
    "function main takes nothing returns nothing\n" \
    "set u=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"

static bool reentry_run(cstring_t script) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    return run_test_jass(script);
}

TEST(wc3_order_reentry, stop_is_current_during_nested_notification_then_retires) {
    T_ASSERT(reentry_run(REENTRY_GLOBALS
        "function immediate takes nothing returns nothing\nset trace=trace*10+2\n"
        "call BJassAssert(GetIssuedOrderId()==OrderId(\"stop\"),\"nested Stop payload\")\n"
        "call BJassAssert(GetUnitCurrentOrder(u)==OrderId(\"stop\"),\"Stop owns transient head\")\nendfunction\n"
        "function first takes nothing returns nothing\nset trace=trace*10+1\n"
        "call IssueImmediateOrder(u,\"stop\")\nset trace=trace*10+3\n"
        "call BJassAssert(GetUnitCurrentOrder(u)==0,\"Stop completed before returning to outer action\")\n"
        "call BJassAssert(GetIssuedOrderId()==OrderId(\"move\") and GetOrderPointX()==512.0,\"outer point survives Stop\")\nendfunction\n"
        "function second takes nothing returns nothing\nset trace=trace*10+4\n"
        "call BJassAssert(GetUnitCurrentOrder(u)==0 and GetOrderPointX()==512.0,\"later subscriber retains outer payload\")\nendfunction\n"
        REENTRY_REGISTER REENTRY_BEGIN
        "set p=reg(function immediate,EVENT_UNIT_ISSUED_ORDER)\n"
        "set a=reg(function first,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "set b=reg(function second,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call IssuePointOrder(u,\"move\",512.0,64.0)\n"
        "call BJassAssert(trace==1234,\"nested Stop completes inside first callback\")\nendfunction\n"));
}

TEST(wc3_order_reentry, stop_completion_preserves_replacement_from_its_callback) {
    T_ASSERT(reentry_run(REENTRY_GLOBALS
        "function immediate takes nothing returns nothing\n"
        "call BJassAssert(GetUnitCurrentOrder(u)==OrderId(\"stop\"),\"Stop head before nested replacement\")\n"
        "call IssuePointOrder(u,\"move\",640.0,64.0)\nendfunction\n"
        REENTRY_REGISTER REENTRY_BEGIN
        "set p=reg(function immediate,EVENT_UNIT_ISSUED_ORDER)\ncall IssueImmediateOrder(u,\"stop\")\n"
        "call BJassAssert(GetUnitCurrentOrder(u)==OrderId(\"move\"),\"old Stop cannot complete new Move\")\nendfunction\n"));
}

TEST(wc3_order_reentry, player_stop_keeps_outer_payload_for_both_families) {
    T_ASSERT(reentry_run(REENTRY_GLOBALS
        "function immediate takes nothing returns nothing\nset trace=trace*10+2\n"
        "call BJassAssert(GetUnitCurrentOrder(u)==OrderId(\"stop\"),\"player immediate sees Stop\")\nendfunction\n"
        "function first takes nothing returns nothing\nset trace=trace*10+1\n"
        "call IssueImmediateOrder(u,\"stop\")\nset trace=trace*10+3\nendfunction\n"
        "function second takes nothing returns nothing\nset trace=trace*10+4\n"
        "call BJassAssert(GetIssuedOrderId()==OrderId(\"move\") and GetOrderPointX()==512.0 and GetUnitCurrentOrder(u)==0,\"later player sees retained Move packet\")\nendfunction\n"
        "function unitpoint takes nothing returns nothing\nset trace=trace*10+5\n"
        "call BJassAssert(GetIssuedOrderId()==OrderId(\"move\") and GetOrderPointX()==512.0 and GetUnitCurrentOrder(u)==0,\"unit family shares retained Move packet\")\nendfunction\n"
        REENTRY_REGISTER REENTRY_BEGIN
        "set p=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(p,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerAddAction(p,function immediate)\n"
        "set a=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(a,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        "call TriggerAddAction(a,function first)\n"
        "set b=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(b,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        "call TriggerAddAction(b,function second)\ncall reg(function unitpoint,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call IssuePointOrder(u,\"move\",512.0,64.0)\n"
        "call BJassAssert(trace==12345,\"player reentry unwinds before later player and unit\")\nendfunction\n"));
}

TEST(wc3_order_reentry, three_levels_restore_each_point_and_destroy_later_callback) {
    T_ASSERT(reentry_run(REENTRY_GLOBALS
        "function first takes nothing returns nothing\nlocal real x=GetOrderPointX()\nset trace=trace*10+1\n"
        "if x<768.0 then\ncall IssuePointOrder(u,\"move\",x+128.0,64.0)\n"
        "else\ncall DestroyTrigger(b)\nendif\n"
        "call BJassAssert(GetOrderPointX()==x,\"each nested packet survives unwind\")\n"
        "set trace=trace*10+2\nendfunction\n"
        "function second takes nothing returns nothing\nset trace=trace*10+3\nendfunction\n"
        REENTRY_REGISTER REENTRY_BEGIN
        "set a=reg(function first,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "set b=reg(function second,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call IssuePointOrder(u,\"move\",512.0,64.0)\n"
        "call BJassAssert(trace==111222 and GetTriggerExecCount(a)==3 and GetTriggerExecCount(b)==0,\"nested destruction suppresses every outer pending callback\")\nendfunction\n"));
}

TEST(wc3_order_reentry, kill_delivers_player_then_unit_death_inside_point_callback) {
    T_ASSERT(reentry_run(REENTRY_GLOBALS
        "function playerdeath takes nothing returns nothing\nset trace=trace*10+2\n"
        "call BJassAssert(GetTriggerUnit()==u and GetWidgetLife(u)==0.0,\"player death sees corpse\")\n"
        "call KillUnit(u)\nendfunction\n"
        "function death takes nothing returns nothing\nset trace=trace*10+3\n"
        "call BJassAssert(GetTriggerUnit()==u and GetWidgetLife(u)==0.0,\"unit death sees corpse\")\nendfunction\n"
        "function first takes nothing returns nothing\nset trace=trace*10+1\ncall KillUnit(u)\n"
        "set trace=trace*10+4\n"
        "call BJassAssert(GetIssuedOrderId()==OrderId(\"move\") and GetOrderPointX()==512.0,\"outer point restored after death\")\nendfunction\n"
        "function second takes nothing returns nothing\nset trace=trace*10+5\n"
        "call BJassAssert(GetWidgetLife(u)==0.0,\"later outer callback still delivered to corpse\")\nendfunction\n"
        REENTRY_REGISTER REENTRY_BEGIN
        "set p=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(p,Player(0),EVENT_PLAYER_UNIT_DEATH,null)\n"
        "call TriggerAddAction(p,function playerdeath)\ncall reg(function death,EVENT_UNIT_DEATH)\n"
        "set a=reg(function first,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "set b=reg(function second,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call IssuePointOrder(u,\"move\",512.0,64.0)\n"
        "call BJassAssert(trace==12345,\"death is synchronous, nested and ordered\")\nendfunction\n"));
}

TEST(wc3_order_reentry, death_samples_unit_subscribers_after_player_delivery) {
    T_ASSERT(reentry_run(REENTRY_GLOBALS REENTRY_REGISTER
        "function death takes nothing returns nothing\nset trace=trace*10+2\nendfunction\n"
        "function playerdeath takes nothing returns nothing\nset trace=trace*10+1\n"
        "set a=reg(function death,EVENT_UNIT_DEATH)\nendfunction\n"
        REENTRY_BEGIN
        "set p=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(p,Player(0),EVENT_PLAYER_UNIT_DEATH,null)\n"
        "call TriggerAddAction(p,function playerdeath)\ncall KillUnit(u)\n"
        "call BJassAssert(trace==12,\"death producer samples unit presence after player callback\")\nendfunction\n"));
}

TEST(wc3_order_reentry, removal_keeps_running_dispatch_and_suspends_new_point_order) {
    T_ASSERT(reentry_run(REENTRY_GLOBALS
        "function first takes nothing returns nothing\nset trace=trace*10+1\n"
        "if nested then\nreturn\nendif\nset nested=true\ncall RemoveUnit(u)\n"
        "call BJassAssert(GetUnitTypeId(u)=='hfoo' and GetWidgetLife(u)==420.0,\"removal retains live handle until drain\")\n"
        "call BJassAssert(GetUnitCurrentOrder(u)==0,\"removal cancels old head\")\n"
        "call BJassAssert(IssuePointOrder(u,\"move\",640.0,64.0),\"suspended native admission still returns true\")\n"
        "call BJassAssert(GetUnitCurrentOrder(u)==OrderId(\"move\"),\"new suspended user head is retained\")\nendfunction\n"
        "function second takes nothing returns nothing\nset trace=trace*10+2\n"
        "call BJassAssert(GetWidgetLife(u)==420.0 and GetOrderPointX()==512.0,\"outer subscriber survives pending removal\")\nendfunction\n"
        "function check_removed takes nothing returns nothing\n"
        "call BJassAssert(GetUnitTypeId(u)==0 and GetWidgetLife(u)==0.0 and GetUnitCurrentOrder(u)==0,\"handle dead after deferred release\")\nendfunction\n"
        REENTRY_REGISTER REENTRY_BEGIN
        "set a=reg(function first,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "set b=reg(function second,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call IssuePointOrder(u,\"move\",512.0,64.0)\n"
        "call BJassAssert(trace==12 and GetTriggerExecCount(a)==1 and GetTriggerExecCount(b)==1,\"suspended order emits no notification\")\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(e,e->class_id==MAKEFOURCC('h','f','o','o'))unit=e;
    T_NOT_NULL(unit);if(!unit)return;
    T_ASSERT(G_IsDeferredFree(unit));T_EQ(unit->currentmove,NULL);
    T_EQ(G_UnitQueuedOrderCount(unit),1);
    T_ASSERT(!G_UnitStartNextQueuedOrder(unit));T_EQ(G_UnitQueuedOrderCount(unit),1);
    G_RunDeferredFrees();T_ASSERT(!unit->inuse);T_EQ(unit->class_id,0);
    jass_callbyname(level.vm,"check_removed",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_order_reentry, remove_from_player_keeps_unit_packet_then_defend_retires_and_detaches) {
    T_ASSERT(reentry_run(REENTRY_GLOBALS
        "function immediate takes nothing returns nothing\nset trace=trace*10+4\n"
        "call BJassAssert(GetTriggerUnit()==u and GetIssuedOrderId()==OrderId(\"undefend\") and GetWidgetLife(u)==0.0,\"Defend forced off owns a corpse notification\")\nendfunction\n"
        "function first takes nothing returns nothing\nset trace=trace*10+1\ncall RemoveUnit(u)\nendfunction\n"
        "function second takes nothing returns nothing\nset trace=trace*10+2\n"
        "call BJassAssert(GetWidgetLife(u)==420.0 and GetOrderPointX()==512.0 and GetUnitCurrentOrder(u)==0,\"later player retains removed unit packet\")\nendfunction\n"
        "function unitpoint takes nothing returns nothing\nset trace=trace*10+3\n"
        "call BJassAssert(GetWidgetLife(u)==420.0 and GetOrderPointX()==512.0 and GetUnitCurrentOrder(u)==0,\"unit family still delivers\")\nendfunction\n"
        "function check_release takes nothing returns nothing\n"
        "call BJassAssert(trace==12344,\"retirement and detach each notify off, after point families\")\nendfunction\n"
        REENTRY_REGISTER REENTRY_BEGIN
        "call UnitAddAbility(u,'Adef')\n"
        "set p=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(p,Player(0),EVENT_PLAYER_UNIT_ISSUED_ORDER,null)\n"
        "call TriggerAddAction(p,function immediate)\n"
        "set a=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(a,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        "call TriggerAddAction(a,function first)\n"
        "set b=CreateTrigger()\ncall TriggerRegisterPlayerUnitEvent(b,Player(0),EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER,null)\n"
        "call TriggerAddAction(b,function second)\ncall reg(function unitpoint,EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call IssuePointOrder(u,\"move\",512.0,64.0)\n"
        "call BJassAssert(trace==123,\"removal defers ability detach until dispatch completes\")\nendfunction\n"));
    G_RunDeferredFrees();jass_callbyname(level.vm,"check_release",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}
#endif
