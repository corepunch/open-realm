#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_interrupt209_scene.h"

extern void (*test_preload_marker)(cstring_t);

/* Original I209 accepts every command behind removal without dispatching it.
 * No physical owner, event or release deadline may be created by admission. */
TEST(wc3_interrupt, pending_removal_retains_all_order_shapes_and_saved_head) {
    reset_entities();setup_test_world();g_edicts[0].client=game.clients;
    T_ASSERT(run_test_jass(
        "globals\nunit array u\nunit target=null\ninteger events=0\nendglobals\n"
        "function issued takes nothing returns nothing\n"
        "if GetIssuedOrderId()!=OrderId(\"undefend\") then\nset events=events+1\nendif\nendfunction\n"
        "function main takes nothing returns nothing\nlocal integer i=0\nlocal trigger t=null\n"
        "set target=CreateUnit(Player(0),'hfoo',768,768,0)\n"
        "loop\nexitwhen i==4\nset u[i]=CreateUnit(Player(0),'hfoo',256,256+128*i,0)\n"
        "set t=CreateTrigger()\ncall TriggerRegisterUnitEvent(t,u[i],EVENT_UNIT_ISSUED_ORDER)\n"
        "call TriggerRegisterUnitEvent(t,u[i],EVENT_UNIT_ISSUED_POINT_ORDER)\n"
        "call TriggerRegisterUnitEvent(t,u[i],EVENT_UNIT_ISSUED_TARGET_ORDER)\n"
        "call TriggerAddAction(t,function issued)\ncall RemoveUnit(u[i])\nset i=i+1\nendloop\n"
        "call BJassAssert(IssueImmediateOrder(u[0],\"stop\"),\"pending Stop admitted\")\n"
        "call BJassAssert(IssueImmediateOrder(u[1],\"holdposition\"),\"pending Hold admitted\")\n"
        "call BJassAssert(IssueTargetOrder(u[2],\"move\",target),\"pending target admitted\")\n"
        "call BJassAssert(IssuePointOrder(u[3],\"move\",512,512),\"pending point admitted\")\n"
        "call BJassAssert(GetUnitCurrentOrder(u[0])==OrderId(\"stop\"),\"suspended Stop head\")\n"
        "call BJassAssert(GetUnitCurrentOrder(u[1])==OrderId(\"holdposition\"),\"suspended Hold head\")\n"
        "call BJassAssert(events==0,\"suspended admission sends no issued events\")\nendfunction\n"));
    edict_t *units[4]={0};unsigned count=0;
    FILTER_EDICTS(ent,G_IsDeferredFree(ent))if(count<4)units[count++]=ent;
    T_EQ(count,4);wc3Clock_t due;uint32_t serial;
    T_ASSERT(G_NextUnitRelease(&due,&serial));uint32_t sequence=level.timer_sequence;
    cstring_t const orders[]={"stop","holdposition","move","move"};
    FOR_LOOP(i,count) {
        edict_t *unit=units[i];T_EQ(unit->current_order_id,G_OrderId(orders[i]));
        T_EQ(unit->order_queue.count,1);T_NULL(unit->currentmove);T_EQ(unit->movement.group_id,0);
        T_ASSERT(!unit->movement.fine_queued);T_ASSERT(!unit->movement.fine_route.adaptive_admission.queued);
        if(unit->order_queue.count) {
            unitOrder_t *order=unit->order_queue.entries+unit->order_queue.head;
            T_EQ(order->order_id,G_OrderId(orders[i]));
            T_EQ(order->target_type,i<2?UNIT_ORDER_TARGET_NONE:i==2?UNIT_ORDER_TARGET_ENTITY:UNIT_ORDER_TARGET_POINT);
        }
        T_ASSERT(!G_UnitStartNextQueuedOrder(unit));
    }
    T_EQ(level.timer_sequence,sequence);
    cstring_t file=Test_TempPath("wc3-interrupt209.bin");T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
    wc3Clock_t loaded;uint32_t loaded_serial;T_ASSERT(G_NextUnitRelease(&loaded,&loaded_serial));
    T_EQ(wc3_float_bits(loaded.time),wc3_float_bits(due.time));T_EQ(loaded.epoch,due.epoch);T_EQ(loaded_serial,serial);
    FOR_LOOP(i,count){T_ASSERT(G_IsDeferredFree(units[i]));T_EQ(units[i]->order_queue.count,1);}
    level.pathing_clock=due;G_RunDeferredFrees();FOR_LOOP(i,count)T_ASSERT(!units[i]->inuse);
    T_ASSERT(!G_NextUnitRelease(&due,&serial));G_RunDeferredFrees();
    remove(file);reset_entities();setup_test_world();
}

static edict_t *interrupt209_units[4],*interrupt209_caster;
static uint32_t interrupt209_old,interrupt209_replaced,interrupt209_final,interrupt209_counter,interrupt209_first_visit;
static unsigned interrupt209_release_callbacks;
static float interrupt209_mana;
static void interrupt209_group(moveGroup_t const *group) {
    if(group->id==interrupt209_final && !interrupt209_first_visit)interrupt209_first_visit=level.pathing_counter;
}
static unsigned interrupt209_channels,interrupt209_accepted,interrupt209_completed;
static uint32_t interrupt209_release_sequence;
static bool interrupt209_prepared;
static void interrupt209_marker(cstring_t value) {
    if(!interrupt209_prepared && strstr(value," label=issued")) {
        /* The minimal test SLKs omit initMana and target classifications.
         * Supply those instance inputs before the public spell producer. */
        FILTER_EDICTS(ent,ent->inuse && ent->svflags&SVF_MONSTER) {
            ent->targtype=TARG_GROUND;
            if(ent->class_id==MAKEFOURCC('H','p','a','l'))ent->mana.value=200;
        }
        interrupt209_prepared=true;
    }
    if(strstr(value," label=spell-accepted")) {
        unsigned count=0;FILTER_EDICTS(ent,ent->class_id==MAKEFOURCC('h','f','o','o') && ent->inuse) {
            if(count && count<=4)interrupt209_units[count-1]=ent;
            count++;
        }
        T_EQ(count,5);
        FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('H','p','a','l'))interrupt209_caster=ent;
        T_NOT_NULL(interrupt209_caster);if(interrupt209_caster)interrupt209_old=interrupt209_caster->movement.group_id;
    }
    if(strstr(value," label=channel-before-mutation")) {
        interrupt209_channels++;interrupt209_counter=level.pathing_counter;interrupt209_mana=interrupt209_caster->mana.value;
        T_NOT_NULL(move_find_group(interrupt209_old));
    }
    if(strstr(value," label=channel-after-mutation")) {
        interrupt209_replaced=interrupt209_caster->movement.group_id;
        T_ASSERT(interrupt209_replaced && interrupt209_replaced!=interrupt209_old);
        T_EQ(interrupt209_caster->mana.value,interrupt209_mana);
        T_NOT_NULL(move_find_group(interrupt209_old));
    }
    if(strstr(value," label=release-before-mutation")) {
        interrupt209_release_callbacks++;
        T_EQ(interrupt209_caster->movement.group_id,interrupt209_replaced);
    }
    if(strstr(value," label=release-after-mutation")) {
        interrupt209_final=interrupt209_caster->movement.group_id;
        T_ASSERT(interrupt209_final && interrupt209_final!=interrupt209_replaced);
        T_EQ(interrupt209_first_visit,0);
    }
    if(strstr(value," label=remove-after "))interrupt209_release_sequence=level.timer_sequence;
    unsigned role;
    if(sscanf(value,"I209 tick=%*u label=accepted role=%u",&role)==1 && role<4) {
        edict_t *unit=interrupt209_units[role];T_NOT_NULL(unit);if(!unit)return;
        cstring_t const orders[]={"stop","holdposition","move","move"};
        T_ASSERT(G_IsDeferredFree(unit));T_EQ(unit->current_order_id,G_OrderId(orders[role]));
        T_EQ(unit->order_queue.count,1);T_NULL(unit->currentmove);T_EQ(unit->movement.group_id,0);
        T_EQ(level.timer_sequence,interrupt209_release_sequence);interrupt209_accepted++;
    }
    if(strstr(value," label=complete"))interrupt209_completed++;
}

TEST(wc3_interrupt, actual_spell_completion_replaces_owner_and_releases_removed_peers_once) {
    reset_entities();setup_test_world();FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    uint8_t cells[64*64]={0};CM_SetupTestPathmap(64,64,cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    char const *text="ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"levels\"\nC;Y1;X9;K\"reqLevel\"\nC;Y1;X10;K\"levelSkip\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend\"\n"
        "C;Y2;X4;K\"13\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"800\"\nC;Y2;X7;K\"37\"\nC;Y2;X8;K3\nC;Y2;X9;K1\nC;Y2;X10;K2\nE\n";
    slkTestData_t *rows=parse_slk_string(text),*old=G_SetSLKRows("AbilityData",rows);
    level.pathing_clock=(wc3Clock_t){0,0,300};level.pathing_counter=1024;
    level.time=level.pathing_msec=level.pathing_phase=0;level.pathing_due=false;
    memset(interrupt209_units,0,sizeof(interrupt209_units));
    interrupt209_channels=interrupt209_accepted=interrupt209_completed=interrupt209_release_callbacks=0;interrupt209_prepared=false;
    interrupt209_old=interrupt209_replaced=interrupt209_final=interrupt209_first_visit=interrupt209_counter=0;
    interrupt209_caster=NULL;move_test_group_begin=interrupt209_group;
    test_preload_marker=interrupt209_marker;T_ASSERT(run_test_jass(interrupt209_scene));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    while(level.time<14000){level.time+=5;globals.RunFrame();}
    T_EQ(interrupt209_channels,1);T_EQ(interrupt209_accepted,4);T_EQ(interrupt209_completed,1);T_EQ(interrupt209_release_callbacks,1);
    T_EQ(interrupt209_first_visit,interrupt209_counter+1);
    T_NULL(move_find_group(interrupt209_old));T_NULL(move_find_group(interrupt209_replaced));T_NULL(move_find_group(interrupt209_final));
    if(interrupt209_caster)T_ASSERT(Vector2_distance(&interrupt209_caster->s.origin2,&(vec2_t){1536,1536})<32);
    FOR_LOOP(i,4)if(interrupt209_units[i])T_ASSERT(!interrupt209_units[i]->inuse);
    jass_callbyname(level.vm,"I209Check",false);T_ASSERT(!jass_rterror_pending(level.vm));
    test_preload_marker=NULL;move_test_group_begin=NULL;level.started=false;
    G_SetSLKRows("AbilityData",old);free_slk_rows(rows);reset_entities();setup_test_world();
}
#endif
