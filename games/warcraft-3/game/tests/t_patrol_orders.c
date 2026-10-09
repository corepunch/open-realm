#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"

bool run_test_jass(cstring_t);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *);
void free_slk_rows(slkTestData_t *);

static edict_t *patrol_orders_setup(void) {
    reset_entities();setup_test_world();
    T_ASSERT(run_test_jass("globals\n unit actor=null\nendglobals\n"
        "function short takes nothing returns nothing\n"
        " call BJassAssert(IssuePointOrder(actor,\"patrol\",124.0,64.0),\"Short Patrol accepted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==0,\"Short Patrol retires synchronously\")\nendfunction\n"
        "function near takes nothing returns nothing\n"
        " call BJassAssert(IssuePointOrder(actor,\"patrol\",163.99,64.0),\"Near Patrol accepted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==0,\"Near Patrol retires synchronously\")\nendfunction\n"
        "function exact takes nothing returns nothing\n"
        " call BJassAssert(IssuePointOrder(actor,\"patrol\",164.0,64.0),\"Exact Patrol accepted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==851991,\"Exact Patrol expands\")\nendfunction\n"
        "function kill takes nothing returns nothing\n call KillUnit(actor)\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==0,\"Death retires Patrol\")\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " set actor=CreateUnit(Player(0),'hpea',64.0,64.0,0.0)\nendfunction\n"));
    edict_t *actor=NULL;FILTER_EDICTS(unit,unit->class_id==MAKEFOURCC('h','p','e','a')) actor=unit;
    T_NOT_NULL(actor);if(!actor)return NULL;
    actor->health.value=actor->health.max_value=420;actor->collision=8;
    actor->unitinfo.MoveSpeed=256;actor->unitinfo.AcquireRange=0;
    actor->movetype=MOVETYPE_STEP;actor->svflags|=SVF_MONSTER;
    actor->think=monster_think;actor->stand=unit_stand;actor->die=unit_die;
    unit_stand(actor);gi.LinkEntity(actor);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    return actor;
}

static void patrol_orders_frames(unsigned count) {
    FOR_LOOP(i,count){level.time+=FRAMETIME;globals.RunFrame();}
}

static void patrol_orders_until(edict_t *actor,uint32_t head,unsigned limit) {
    FOR_LOOP(i,limit){if(actor->current_order_id==head)return;patrol_orders_frames(1);}
    T_EQ(actor->current_order_id,head);
}

TEST(wc3_patrol_orders, public_expansion_uses_retail_minimum_distance) {
    edict_t *actor=patrol_orders_setup();if(!actor)return;
    jass_callbyname(level.vm,"short",false);T_ASSERT(!jass_rterror_pending(level.vm));jass_rterror_clear(level.vm);
    patrol_orders_frames(8);T_EQ(actor->current_order_id,0);T_FEQ(actor->s.origin2.x,64,0);
    T_ASSERT(unit_issueimmediateorder(actor,"stop"));
    actor->s.origin.x=actor->s.origin2.x=64;gi.LinkEntity(actor);
    jass_callbyname(level.vm,"near",false);T_ASSERT(!jass_rterror_pending(level.vm));jass_rterror_clear(level.vm);
    T_ASSERT(unit_issueimmediateorder(actor,"stop"));
    actor->s.origin.x=actor->s.origin2.x=64;gi.LinkEntity(actor);
    jass_callbyname(level.vm,"exact",false);T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(actor->current_order_id,WC3_ORDER_ID_PATROL_TWO_POINTS);
}

TEST(wc3_patrol_orders, queued_move_runs_at_leg_end_then_return_leg_resumes) {
    edict_t *actor=patrol_orders_setup();if(!actor)return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){384,64},false,0,0));
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){600,64},true,0,0));
    patrol_orders_until(actor,G_OrderId("move"),80);
    T_EQ(actor->order_queue.count,1);
    patrol_orders_until(actor,WC3_ORDER_ID_PATROL_TWO_POINTS,100);
    T_EQ(actor->order_queue.count,0);T_NOT_NULL(actor->movement.patrol_target);
    if(actor->movement.patrol_target)T_FEQ(actor->movement.patrol_target->s.origin2.x,64,0);
    patrol_orders_frames(100);T_EQ(actor->current_order_id,WC3_ORDER_ID_PATROL_TWO_POINTS);
}

TEST(wc3_patrol_orders, queued_patrol_rotates_behind_non_patrol_then_routes_alternate) {
    edict_t *actor=patrol_orders_setup();if(!actor)return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){384,64},false,0,0));
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){256,320},true,0,0));
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){600,64},true,0,0));
    patrol_orders_until(actor,G_OrderId("move"),80);T_EQ(actor->order_queue.count,2);
    patrol_orders_until(actor,WC3_ORDER_ID_PATROL_TWO_POINTS,100);
    T_NOT_NULL(actor->movement.patrol_target);
    if(actor->movement.patrol_target)T_FEQ(actor->movement.patrol_target->s.origin2.x,64,0);
    bool second=false;
    FOR_LOOP(i,160){
        patrol_orders_frames(1);
        if(actor->movement.patrol_target && actor->movement.patrol_target->s.origin2.y==320){second=true;break;}
    }
    T_ASSERT(second);T_EQ(actor->order_queue.count,1);
}

TEST(wc3_patrol_orders, full_user_fifo_keeps_its_return_continuation) {
    edict_t *actor=patrol_orders_setup();if(!actor)return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){384,64},false,0,0));
    FOR_LOOP(i,MAX_UNIT_ORDER_QUEUE)T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){600,64},true,0,0));
    T_ASSERT(!G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},true,0,0));
    patrol_orders_until(actor,G_OrderId("move"),80);
    T_EQ(actor->order_queue.count,MAX_UNIT_ORDER_QUEUE);
    patrol_orders_until(actor,WC3_ORDER_ID_PATROL_TWO_POINTS,120);
    T_NOT_NULL(actor->movement.patrol_target);
    if(actor->movement.patrol_target)T_FEQ(actor->movement.patrol_target->s.origin2.x,64,0);
}
TEST(wc3_patrol_orders, saved_rotated_pair_retains_activation_origin_and_fifo_order) {
    edict_t *actor=patrol_orders_setup();if(!actor)return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){384,64},false,0,0));
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){256,320},true,0,0));
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){600,64},true,0,0));
    patrol_orders_until(actor,G_OrderId("move"),80);
    T_EQ(actor->order_queue.count,2);
    unitOrderQueue_t *queue=&actor->order_queue;
    if(queue->count!=2)return;
    unitOrder_t first=queue->entries[queue->head],second=queue->entries[(queue->head+1)%UNIT_ORDER_STORAGE_CAPACITY];
    T_FEQ(first.point.x,64,0);T_FEQ(first.continuation.x,384,0);
    T_FEQ(second.point.x,256,0);T_FEQ(second.point.y,320,0);
    T_FEQ(second.continuation.x,actor->s.origin2.x,0);
    T_FEQ(second.continuation.y,actor->s.origin2.y,0);
    cstring_t save=Test_TempPath("wc3-patrol-rotation176.bin");
    T_ASSERT(WriteGame(save));T_ASSERT(unit_issueimmediateorder(actor,"stop"));
    T_EQ(queue->count,0);T_ASSERT(ReadGame(save));remove(save);
    T_EQ(queue->count,2);
    T_ASSERT(!memcmp(&queue->entries[queue->head],&first,sizeof(first)));
    T_ASSERT(!memcmp(&queue->entries[(queue->head+1)%UNIT_ORDER_STORAGE_CAPACITY],&second,sizeof(second)));
    patrol_orders_until(actor,WC3_ORDER_ID_PATROL_TWO_POINTS,100);
    T_FEQ(actor->movement.patrol_target->s.origin2.x,64,0);
    bool rotated=false;
    FOR_LOOP(i,160){
        patrol_orders_frames(1);
        if(actor->movement.patrol_target && actor->movement.patrol_target->s.origin2.y==320){rotated=true;break;}
    }
    T_ASSERT(rotated);
    if(rotated){
        T_FEQ(actor->movement.patrol_a->s.origin2.x,second.continuation.x,0);
        T_FEQ(actor->movement.patrol_a->s.origin2.y,second.continuation.y,0);
    }
    jass_callbyname(level.vm,"kill",false);T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(queue->count,0);T_EQ(actor->current_order_id,0);
}

TEST(wc3_patrol_orders, combat_resumes_same_leg_before_starting_queued_move) {
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;E\nB;Y2;X2;D0\n"
        "C;X1;Y1;K\"unitWeaponID\"\nC;X2;K\"weapsOn\"\n"
        "C;X1;Y2;K\"hpea\"\nC;X2;K1\nE\n");
    slkTestData_t *old=G_SetSLKRows("UnitWeapons",rows);
    edict_t *actor=patrol_orders_setup();
    if(!actor){G_SetSLKRows("UnitWeapons",old);free_slk_rows(rows);return;}
    actor->runtime.acquisition_range=160;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType=kPlayerTypeHuman;
    actor->targtype=TARG_GROUND;
    unitAttack_t *profile=S_AttackProfileWrite(actor,0);
    profile->type=ATK_NORMAL;profile->targetsAllowed=WC3_TARGET_FLAG_GROUND;
    profile->range=90;profile->damagePoint=.1f;profile->cooldown=1.3f;
    profile->damageBase=1;profile->backswingPoint=.47f;
    edict_t *enemy=unit_create(1,MAKEFOURCC('h','f','o','o'),&(vec2_t){320,64},0);
    T_NOT_NULL(enemy);if(!enemy){G_SetSLKRows("UnitWeapons",old);free_slk_rows(rows);return;}
    enemy->health.value=enemy->health.max_value=5000;enemy->targtype=TARG_GROUND;
    enemy->paused=true;enemy->collision=8;enemy->svflags|=SVF_MONSTER;gi.LinkEntity(enemy);
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){768,64},false,0,0));
    edict_t *endpoint=actor->movement.patrol_target;
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){896,64},true,0,0));
    FOR_LOOP(i,100){if(enemy->health.value<5000)break;patrol_orders_frames(1);}
    T_ASSERT(enemy->health.value<5000);
    T_EQ(actor->current_order_id,WC3_ORDER_ID_PATROL_TWO_POINTS);
    T_EQ(actor->order_queue.count,1);
    cstring_t save=Test_TempPath("wc3-patrol-combat176.bin");
    T_ASSERT(WriteGame(save));T_ASSERT(ReadGame(save));remove(save);
    T_EQ(actor->movement.patrol_target,endpoint);T_EQ(actor->goalentity,enemy);
    G_DeferFreeEdict(enemy);T_EQ(actor->current_order_id,WC3_ORDER_ID_PATROL_TWO_POINTS);
    patrol_orders_frames(6);
    T_EQ(actor->current_order_id,WC3_ORDER_ID_PATROL_TWO_POINTS);
    T_EQ(actor->movement.patrol_target,endpoint);T_EQ(actor->goalentity,endpoint);
    T_EQ(actor->order_queue.count,1);
    patrol_orders_until(actor,G_OrderId("move"),140);
    T_ASSERT(actor->s.origin2.x>700);T_EQ(actor->order_queue.count,1);
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){600,64},false,0,0));
    T_EQ(actor->order_queue.count,0);T_NULL(actor->movement.patrol_a);
    patrol_orders_frames(2);T_EQ(actor->current_order_id,G_OrderId("move"));
    G_SetSLKRows("UnitWeapons",old);free_slk_rows(rows);
}

TEST(wc3_patrol_orders, blocked_leg_appends_return_behind_pending_commands) {
    edict_t *actor=patrol_orders_setup();if(!actor)return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){384,64},false,0,0));
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){600,64},true,0,0));
    /* Exercise the real generic blocked decision at its retained watermark. */
    actor->movement.last_distance=1;actor->movement.last_origin=actor->s.origin2;
    actor->movement.blocked_frames=MOVE_BLOCKED_FRAMES;
    actor->currentmove->think(actor);
    T_EQ(actor->current_order_id,G_OrderId("move"));T_EQ(actor->order_queue.count,1);
    if(actor->order_queue.count){
        unitOrder_t const *ret=&actor->order_queue.entries[actor->order_queue.head];
        T_FEQ(ret->point.x,64,0);T_FEQ(ret->continuation.x,384,0);
    }
}
TEST(wc3_patrol_orders, owner_payload_kind_does_not_reserve_a_move_group_identity) {
    edict_t *actor=patrol_orders_setup();if(!actor)return;
    unitOrder_t continuation={.order="patrol",.owner_context=PATROL_QUEUE_TWO_POINTS};
    T_ASSERT(G_AppendUnitOrder(actor,&continuation));
    level.next_move_group_id=0;move_group_id_bound=0;move_group_id_bound_valid=false;
    T_EQ(move_allocate_group_id(),1);
}
TEST(wc3_patrol_orders, unqueued_reversals_reuse_the_retained_leg_without_fifo_allocation) {
    edict_t *actor=patrol_orders_setup();if(!actor)return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"patrol",&(vec2_t){384,64},false,0,0));
    T_NULL(actor->order_queue.entries);
    patrol_orders_frames(100);
    T_EQ(actor->current_order_id,WC3_ORDER_ID_PATROL_TWO_POINTS);
    T_EQ(actor->order_queue.count,0);T_NULL(actor->order_queue.entries);
}
#endif
