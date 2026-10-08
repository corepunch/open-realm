#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"
#include "../../common/wc3_pathing_speed.h"
#include "retail_attack_swing174.h"
#include "retail_attack_ground175.h"

bool run_test_jass(cstring_t source);
void setup_test_world(void);
void reset_entities(void);
void order_attack(edict_t *self, edict_t *target);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

static edict_t *attack_orders_setup(void) {
    static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
    reset_entities(); setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n unit actor=null\n unit enemy=null\nendglobals\n"
        "function attack takes nothing returns nothing\n"
        " call BJassAssert(IssueTargetOrder(actor,\"attack\",enemy),\"Attack admitted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==OrderId(\"attack\"),\"Attack head\")\nendfunction\n"
        "function once takes nothing returns nothing\n"
        " call BJassAssert(IssueTargetOrder(actor,\"attackonce\",enemy),\"Attack Once admitted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==OrderId(\"attackonce\"),\"Attack Once head\")\nendfunction\n"
        "function ground takes nothing returns nothing\n"
        " call BJassAssert(IssuePointOrder(actor,\"attackground\",600.0,64.0),\"Ground admitted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==OrderId(\"attackground\"),\"Ground head\")\nendfunction\n"
        "function point takes nothing returns nothing\n"
        " call BJassAssert(IssuePointOrder(actor,\"attack\",384.0,64.0),\"Attack Move admitted\")\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==OrderId(\"attack\"),\"Attack Move head\")\nendfunction\n"
        "function selfAttack takes nothing returns nothing\n"
        " call BJassAssert(IssueTargetOrder(actor,\"attack\",actor),\"Attack self forwarded\")\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==OrderId(\"attack\"),\"Forwarded head\")\nendfunction\n"
        "function verifyAttack takes nothing returns nothing\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==OrderId(\"attack\"),\"Retained Attack head\")\nendfunction\n"
        "function kill takes nothing returns nothing\n call KillUnit(actor)\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==0,\"Death retires head synchronously\")\nendfunction\n"
        "function recreate takes nothing returns nothing\n"
        " set actor=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        " call BJassAssert(GetUnitCurrentOrder(actor)==0,\"Reused slot has no head\")\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " set actor=CreateUnit(Player(0),'hfoo',64.0,64.0,0.0)\n"
        " set enemy=CreateUnit(Player(1),'hgry',384.0,64.0,0.0)\nendfunction\n"));
    edict_t *actor = NULL;
    FILTER_EDICTS(unit, unit->class_id == MAKEFOURCC('h','f','o','o')) actor = unit;
    T_NOT_NULL(actor);
    if (!actor) return NULL;
    actor->health.value = actor->health.max_value = 420;
    actor->data.UnitWeapons = &weapons;
    actor->targtype = TARG_GROUND;
    actor->unitinfo.MoveSpeed = 256; actor->unitinfo.AcquireRange = 0;
    actor->collision = 8; actor->svflags |= SVF_MONSTER;
    actor->movetype = MOVETYPE_STEP; actor->think = monster_think;
    actor->stand = unit_stand;
    S_AttackProfileWrite(actor, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(actor, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
    S_AttackProfileWrite(actor, 0)->range = 90;
    S_AttackProfileWrite(actor, 0)->cooldown = 1.3f;
    S_AttackProfileWrite(actor, 0)->damagePoint = 0.2f;
    S_AttackProfileWrite(actor, 0)->damageBase = 1;
    unit_stand(actor); gi.LinkEntity(actor);
    FILTER_EDICTS(unit, unit->class_id == MAKEFOURCC('h','g','r','y')) {
        unit->health.value = unit->health.max_value = 5000;
        unit->targtype = TARG_AIR; unit->paused = true;
        unit->collision = 8; gi.LinkEntity(unit);
    }
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    return actor;
}

static edict_t *attack_orders_enemy(void) {
    FILTER_EDICTS(unit, unit->class_id == MAKEFOURCC('h','g','r','y')) return unit;
    return NULL;
}

static void attack_orders_call(cstring_t name) {
    jass_callbyname(level.vm, name, false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

static void attack_orders_frames(unsigned count) {
    FOR_LOOP(i, count) { level.time += FRAMETIME; globals.RunFrame(); }
}

TEST(wc3_attack_orders, target_head_save_replacement_and_automatic_combat) {
    /* Save rebinds authored rows. Install a real weapons row rather than
     * expecting a stack/static test-only row pointer to survive load. */
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;E\nB;Y2;X2;D0\n"
        "C;X1;Y1;K\"unitWeaponID\"\nC;X2;K\"weapsOn\"\n"
        "C;X1;Y2;K\"hfoo\"\nC;X2;K1\nE\n");
    slkTestData_t *old=G_SetSLKRows("UnitWeapons",rows);
    edict_t *actor = attack_orders_setup(), *enemy = attack_orders_enemy();
    if (!actor || !enemy) { G_SetSLKRows("UnitWeapons",old); free_slk_rows(rows); return; }
    S_AttackProfileWrite(actor,0)->targetsAllowed |= WC3_TARGET_FLAG_AIR;
    enemy->s.origin2.x=128; enemy->s.origin.x=128; gi.LinkEntity(enemy);
    attack_orders_call("attack");
    T_EQ(actor->goalentity, enemy); T_EQ(actor->current_order_id, G_OrderId("attack"));
    FOR_LOOP(i,160) {
        if (enemy->health.value<5000) break;
        attack_orders_frames(1); T_EQ(actor->current_order_id,G_OrderId("attack"));
    }
    T_ASSERT(enemy->health.value<5000);
    T_ASSERT(actor->attack_cooldown_active);
    cstring_t save = "/tmp/wc3-attack-orders173.bin";
    T_ASSERT(WriteGame(save)); T_ASSERT(unit_issueimmediateorder(actor,"stop"));
    T_ASSERT(ReadGame(save)); attack_orders_call("verifyAttack");
    T_EQ(actor->goalentity, enemy); remove(save);
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){192,128},false,0,0));
    T_EQ(actor->current_order_id,G_OrderId("move"));
    T_ASSERT(unit_issueimmediateorder(actor,"stop")); T_EQ(actor->current_order_id,0);
    order_attack(actor,enemy); T_EQ(actor->goalentity,enemy); T_EQ(actor->current_order_id,0);
    attack_orders_frames(3); T_EQ(actor->current_order_id,0);
    attack_orders_call("attack"); attack_orders_call("kill");
    T_EQ(actor->current_order_id,0); T_EQ(actor->order_queue.count,0);
    G_FreeEdict(actor); level.time+=1001; attack_orders_call("recreate");
    T_ASSERT(actor->inuse); T_EQ(actor->current_order_id,0);
    G_SetSLKRows("UnitWeapons",old); free_slk_rows(rows);
}

TEST(wc3_attack_orders, point_head_survives_acquisition_and_resumes_to_arrival) {
    edict_t *actor = attack_orders_setup(), *enemy = attack_orders_enemy();
    if (!actor || !enemy) return;
    attack_orders_call("point");
    edict_t *waypoint = actor->movement.attackmove_waypoint;
    T_NOT_NULL(waypoint); T_EQ(actor->current_order_id,G_OrderId("attack"));
    enemy->targtype=TARG_GROUND;
    order_attack(actor,enemy); T_EQ(actor->current_order_id,G_OrderId("attack"));
    T_EQ(actor->movement.attackmove_waypoint,waypoint);
    G_FreeEdict(enemy);
    FOR_LOOP(i,160) { if (!actor->current_order_id) break; attack_orders_frames(1); }
    T_EQ(actor->current_order_id,0); T_NULL(actor->movement.attackmove_waypoint);
    T_ASSERT(actor->s.origin.x>320);
}

TEST(wc3_attack_orders, invalid_target_admission_snapshots_point) {
    edict_t *actor = attack_orders_setup(), *enemy = attack_orders_enemy();
    if (!actor || !enemy) return;
    /* Air, invulnerable ground and a corpse all use the same public conversion. */
    FOR_LOOP(kind,3) {
        T_ASSERT(unit_issueimmediateorder(actor,"stop"));
        enemy->targtype=kind ? TARG_GROUND : TARG_AIR;
        enemy->invulnerable=kind==1; enemy->health.value=kind==2 ? 0 : 5000;
        attack_orders_call("attack");
        T_NOT_NULL(actor->movement.attackmove_waypoint);
        T_ASSERT(actor->goalentity!=enemy);
        if (actor->movement.attackmove_waypoint) {
            T_FEQ(actor->goalentity->s.origin2.x,384,0);
            T_FEQ(actor->goalentity->s.origin2.y,64,0);
        }
    }
    T_ASSERT(unit_issueimmediateorder(actor,"stop"));
    attack_orders_call("selfAttack"); attack_orders_frames(2);
    T_EQ(actor->current_order_id,0);
}

TEST(wc3_attack_orders, queued_conversion_keeps_issue_position_across_save) {
    edict_t *actor=attack_orders_setup(), *enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){192,64},false,0,0));
    T_ASSERT(G_IssueUnitTargetOrder(actor,"attack",enemy,true,0));
    T_EQ(actor->current_order_id,G_OrderId("move")); T_EQ(actor->order_queue.count,1);
    unitOrder_t const *queued=&actor->order_queue.entries[actor->order_queue.head];
    T_EQ(queued->target_type,UNIT_ORDER_TARGET_POINT); T_FEQ(queued->point.x,384,0);
    enemy->s.origin2.x=768; enemy->s.origin.x=768; gi.LinkEntity(enemy);
    cstring_t save="/tmp/wc3-attack-queue173.bin";
    T_ASSERT(WriteGame(save)); T_ASSERT(ReadGame(save)); remove(save);
    FOR_LOOP(i,160) { if (!actor->order_queue.count) break; attack_orders_frames(1); }
    T_EQ(actor->order_queue.count,0); attack_orders_call("verifyAttack");
    T_NOT_NULL(actor->movement.attackmove_waypoint);
    if (actor->movement.attackmove_waypoint) T_FEQ(actor->movement.attackmove_waypoint->s.origin2.x,384,0);
    FOR_LOOP(i,160) { if (!actor->current_order_id) break; attack_orders_frames(1); }
    T_EQ(actor->current_order_id,0); T_ASSERT(actor->s.origin.x<450);
}

TEST(wc3_attack_orders, queued_move_starts_after_attack_move_arrival) {
    edict_t *actor=attack_orders_setup(); if (!actor) return;
    attack_orders_call("point");
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){512,128},true,0,0));
    T_EQ(actor->current_order_id,G_OrderId("attack"));
    FOR_LOOP(i,160) { if (!actor->order_queue.count) break; attack_orders_frames(1); }
    T_EQ(actor->order_queue.count,0); T_EQ(actor->current_order_id,G_OrderId("move"));
    FOR_LOOP(i,160) { if (!actor->current_order_id) break; attack_orders_frames(1); }
    T_EQ(actor->current_order_id,0); T_ASSERT(actor->s.origin.x>450);
}

TEST(wc3_attack_orders, artillery_ground_publishes_its_own_head) {
    edict_t *actor=attack_orders_setup(); if (!actor) return;
    S_AttackProfileWrite(actor,0)->weapon=WPN_ARTILLERY;
    S_AttackProfileWrite(actor,0)->type=ATK_SIEGE;
    T_ASSERT(G_IssueUnitPointOrder(actor,"attackground",&(vec2_t){192,64},false,0,0));
    T_EQ(actor->current_order_id,G_OrderId("attackground"));
    attack_orders_frames(3); T_EQ(actor->current_order_id,G_OrderId("attackground"));
    T_ASSERT(unit_issueimmediateorder(actor,"stop")); T_EQ(actor->current_order_id,0);
}

/* Drive an actual committed hit: no model animation supplies completion. */
static edict_t *attack_orders_hit(float backswing) {
    edict_t *actor=attack_orders_setup(),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return NULL;
    S_AttackProfileWrite(actor,0)->targetsAllowed |= WC3_TARGET_FLAG_AIR;
    S_AttackProfileWrite(actor,0)->backswingPoint=backswing;
    enemy->s.origin2.x=128; enemy->s.origin.x=128; gi.LinkEntity(enemy);
    attack_orders_call("attack");
    FOR_LOOP(i,160) {
        if (enemy->health.value<5000) break;
        attack_orders_frames(1);
    }
    T_ASSERT(enemy->health.value<5000);
    return actor;
}

TEST(wc3_attack_orders, target_loss_waits_for_swing_before_queued_move) {
    edict_t *actor=attack_orders_hit(0.47f),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){384,128},true,0,0));
    vec2_t position=actor->s.origin2;
    G_DeferFreeEdict(enemy);
    T_NULL(actor->goalentity);
    T_EQ(actor->current_order_id,G_OrderId("attack")); T_EQ(actor->order_queue.count,1);
    attack_orders_frames(3);
    T_EQ(actor->current_order_id,G_OrderId("attack")); T_EQ(actor->order_queue.count,1);
    T_FEQ(actor->s.origin2.x,position.x,0); T_FEQ(actor->s.origin2.y,position.y,0);
    attack_orders_frames(3);
    T_EQ(actor->current_order_id,G_OrderId("move")); T_EQ(actor->order_queue.count,0);
    /* Swing completion precedes readiness for the next weapon attack. */
    T_ASSERT((int32_t)(actor->attack_cooldown_end_time-G_Time())>0);
}

TEST(wc3_attack_orders, target_loss_replacement_is_not_retired_by_old_swing) {
    edict_t *actor=attack_orders_hit(0.47f),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    G_DeferFreeEdict(enemy); T_EQ(actor->current_order_id,G_OrderId("attack"));
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},false,0,0));
    attack_orders_frames(7);
    T_EQ(actor->current_order_id,G_OrderId("move")); T_ASSERT(actor->s.origin2.x>64);
    T_ASSERT(unit_issueimmediateorder(actor,"stop")); T_EQ(actor->current_order_id,0);
}

TEST(wc3_attack_orders, detached_swing_wait_survives_save_and_slot_reuse) {
    edict_t *actor=attack_orders_hit(0.67f),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){384,128},true,0,0));
    G_DeferFreeEdict(enemy); G_TestFinishDeferredFrees();
    T_EQ(actor->current_order_id,G_OrderId("attack")); T_NULL(actor->goalentity);
    abilityPrimaryTimer_t timer=actor->attack_swing;
    T_ASSERT(timer.active);
    cstring_t save="/tmp/wc3-attack-swing174.bin";
    T_ASSERT(WriteGame(save)); T_ASSERT(ReadGame(save)); remove(save);
    T_ASSERT(actor->attack_swing.active); T_EQ(actor->attack_swing.sequence,timer.sequence);
    T_FEQ(actor->attack_swing.deadline.time,timer.deadline.time,0);
    T_EQ(actor->attack_swing.deadline.epoch,timer.deadline.epoch);
    T_EQ(actor->current_order_id,G_OrderId("attack")); T_EQ(actor->order_queue.count,1);
    attack_orders_frames(4); T_EQ(actor->current_order_id,G_OrderId("attack"));
    attack_orders_frames(4); T_EQ(actor->current_order_id,G_OrderId("move"));
    G_FreeEdict(actor); level.time+=1001; attack_orders_call("recreate");
    attack_orders_frames(8); T_EQ(actor->current_order_id,0);
    T_ASSERT(!actor->attack_swing.active);
}

TEST(wc3_attack_orders, original_swing_delay_words_and_cooldown_clamps) {
    FOR_LOOP(i,sizeof(retail_attack_swing)/sizeof(*retail_attack_swing)) {
        float remaining=wc3_float(retail_attack_swing[i].remaining);
        float delay=wc3_attack_swing_delay(wc3_float(retail_attack_swing[i].backswing),
                                          wc3_float(retail_attack_swing[i].divisor),&remaining);
        T_EQ(wc3_float_bits(delay),retail_attack_swing[i].delay);
        T_EQ(wc3_float_bits(remaining),retail_attack_swing[i].cooldown);
    }
}

TEST(wc3_attack_orders, lethal_hit_waits_and_death_cancels_completion) {
    edict_t *actor=attack_orders_hit(0.47f),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    enemy->health.value=1;
    T_Damage(enemy,actor,2);
    T_ASSERT(M_IsDead(enemy)); T_NULL(actor->goalentity);
    T_EQ(actor->current_order_id,G_OrderId("attack"));
    attack_orders_frames(3); T_EQ(actor->current_order_id,G_OrderId("attack"));
    attack_orders_call("kill"); T_EQ(actor->current_order_id,0);
    attack_orders_frames(3); T_EQ(actor->current_order_id,0); T_NULL(actor->combatentity);
}

TEST(wc3_attack_orders, loss_before_damage_point_has_no_swing_wait) {
    edict_t *actor=attack_orders_setup(),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    S_AttackProfileWrite(actor,0)->targetsAllowed |= WC3_TARGET_FLAG_AIR;
    attack_orders_call("attack");
    T_ASSERT(!actor->attack_swing.active);
    G_DeferFreeEdict(enemy); T_NULL(actor->goalentity); T_EQ(actor->current_order_id,0);
}

static edict_t *attack_orders_once(void) {
    edict_t *actor=attack_orders_setup(),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return NULL;
    S_AttackProfileWrite(actor,0)->targetsAllowed |= WC3_TARGET_FLAG_AIR;
    S_AttackProfileWrite(actor,0)->backswingPoint=.47f;
    enemy->s.origin.x=enemy->s.origin2.x=128; gi.LinkEntity(enemy);
    attack_orders_call("once");
    FOR_LOOP(i,160) {
        if (enemy->health.value<5000) break;
        attack_orders_frames(1);
    }
    T_ASSERT(enemy->health.value<5000);
    return actor;
}

TEST(wc3_attack_orders, once_retires_after_one_swing_then_automatic_attack_has_no_head) {
    edict_t *actor=attack_orders_once(),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    float health=enemy->health.value;
    T_EQ(actor->current_order_id,G_OrderId("attackonce"));
    attack_orders_frames(3); T_EQ(actor->current_order_id,G_OrderId("attackonce"));
    T_FEQ(enemy->health.value,health,0);
    attack_orders_frames(3); T_EQ(actor->current_order_id,0); T_NULL(actor->goalentity);
    T_FEQ(enemy->health.value,health,0);
    T_ASSERT(actor->attack_cooldown_active);
    order_attack(actor,enemy); T_EQ(actor->current_order_id,0);
    attack_orders_frames(20); T_ASSERT(enemy->health.value<health); T_EQ(actor->current_order_id,0);
}

TEST(wc3_attack_orders, once_queue_and_saved_swing_keep_their_head) {
    edict_t *actor=attack_orders_once(),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    float health=enemy->health.value;
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},true,0,0));
    T_EQ(actor->current_order_id,G_OrderId("attackonce")); T_EQ(actor->order_queue.count,1);
    cstring_t save="/tmp/wc3-attack-once175.bin";
    T_ASSERT(WriteGame(save)); T_ASSERT(ReadGame(save)); remove(save);
    attack_orders_frames(3); T_EQ(actor->current_order_id,G_OrderId("attackonce"));
    attack_orders_frames(3); T_EQ(actor->current_order_id,G_OrderId("move"));
    T_EQ(actor->order_queue.count,0); T_FEQ(enemy->health.value,health,0);
}

TEST(wc3_attack_orders, once_replacement_and_target_removal_do_not_repeat_damage) {
    edict_t *actor=attack_orders_once(),*enemy=attack_orders_enemy();
    if (!actor || !enemy) return;
    float health=enemy->health.value;
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},false,0,0));
    attack_orders_frames(8); T_EQ(actor->current_order_id,G_OrderId("move"));
    T_FEQ(enemy->health.value,health,0);
    actor=attack_orders_once();enemy=attack_orders_enemy();if (!actor || !enemy) return;
    G_DeferFreeEdict(enemy); T_NULL(actor->goalentity); T_EQ(actor->current_order_id,G_OrderId("attackonce"));
    attack_orders_frames(3); T_EQ(actor->current_order_id,G_OrderId("attackonce"));
    attack_orders_frames(3); T_EQ(actor->current_order_id,0);
}

TEST(wc3_attack_orders, non_artillery_ground_approaches_authored_range_and_holds_fifo) {
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;E\nB;Y2;X2;D0\n"
        "C;X1;Y1;K\"unitWeaponID\"\nC;X2;K\"weapsOn\"\n"
        "C;X1;Y2;K\"hfoo\"\nC;X2;K1\nE\n");
    slkTestData_t *old=G_SetSLKRows("UnitWeapons",rows);
    edict_t *actor=attack_orders_setup();
    if (!actor) { G_SetSLKRows("UnitWeapons",old); free_slk_rows(rows); return; }
    S_AttackProfileWrite(actor,0)->weapon=WPN_NORMAL;
    S_AttackProfileWrite(actor,0)->range=137;
    attack_orders_call("ground");
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},true,0,0));
    attack_orders_frames(32);
    vec2_t position=actor->s.origin2;
    T_ASSERT(Vector2_distance(&position,&(vec2_t){600,64})<=137);
    T_ASSERT(position.x>400); T_EQ(actor->current_order_id,G_OrderId("attackground"));
    T_EQ(actor->order_queue.count,1);
    attack_orders_frames(32);
    T_FEQ(actor->s.origin2.x,position.x,0); T_FEQ(actor->s.origin2.y,position.y,0);
    T_EQ(actor->current_order_id,G_OrderId("attackground")); T_EQ(actor->order_queue.count,1);
    FILTER_EDICTS(unit,unit->owner==actor && unit->movetype==MOVETYPE_FLYMISSILE) T_ASSERT(false);
    cstring_t save="/tmp/wc3-attack-ground175.bin";
    T_ASSERT(WriteGame(save)); T_ASSERT(ReadGame(save)); remove(save);
    T_EQ(actor->current_order_id,G_OrderId("attackground")); T_EQ(actor->order_queue.count,1);
    attack_orders_frames(4); T_EQ(actor->current_order_id,G_OrderId("attackground"));
    T_FEQ(actor->s.origin2.x,position.x,0);
    T_ASSERT(unit_issueimmediateorder(actor,"stop")); T_EQ(actor->current_order_id,0); T_EQ(actor->order_queue.count,0);
    G_SetSLKRows("UnitWeapons",old); free_slk_rows(rows);
}
TEST(wc3_attack_orders, ground_slot_matches_complete_original_selector) {
    edict_t *actor=attack_orders_setup(); if (!actor) return;
    UnitWeapons_t weapons={0};actor->data.UnitWeapons=&weapons;
    actor->abilstatus=(heroabilitystatus_t *)G_AllocUnitStatus();
    unitStatusStorage_t *status=(unitStatusStorage_t *)actor->abilstatus;
    unsigned index=0;
    FOR_LOOP(enabled,4) FOR_LOOP(counters,8) FOR_LOOP(first,9) FOR_LOOP(second,9) FOR_LOOP(special,2) {
        weapons.attacksEnabled=enabled;
        FOR_LOOP(i,3) status->attack_prevention[i]=(counters>>i)&1;
        FOR_LOOP(slot,2) {
            unitAttack_t *profile=S_AttackProfileWrite(actor,slot);
            profile->weapon=slot ? second : first;
            profile->targetsAllowed=special ? WC3_TARGET_FLAG_TREE : WC3_TARGET_FLAG_AIR;
        }
        unsigned selected=retail_attack_ground_slots[index++];
        T_EQ(attack_ground_slot(actor),selected);
        T_EQ(attack_ground_profile(actor),S_AttackProfileRead(actor,selected));
    }
    T_EQ(index,sizeof(retail_attack_ground_slots));
}

TEST(wc3_attack_orders, ground_uses_second_weapon_geometry_damage_and_delivery) {
    edict_t *actor=attack_orders_setup(); if (!actor) return;
    UnitWeapons_t weapons={.attacksEnabled=3,.attack2={.areaTargets=WC3_TARGET_FLAG_TREE}};
    actor->data.UnitWeapons=&weapons;
    S_AttackProfileWrite(actor,0)->weapon=WPN_NORMAL;
    S_AttackProfileWrite(actor,0)->range=10;
    unitAttack_t *profile=S_AttackProfileWrite(actor,1);
    *profile=*S_AttackProfileRead(actor,0);
    profile->type=ATK_SIEGE;profile->weapon=WPN_ARTILLERY;profile->range=600;
    profile->damageBase=73;profile->damagePoint=.1f;profile->projectile.speed=900;
    attack_orders_call("ground");
    actor->currentmove->think(actor);
    T_STREQ(actor->currentmove->animation,"attack range");
    actor->wait=.01f;actor->currentmove->think(actor);
    edict_t *missile=NULL;
    FILTER_EDICTS(unit,unit->owner==actor && unit->movetype==MOVETYPE_FLYMISSILE) missile=unit;
    T_NOT_NULL(missile);
    if (missile) {
        T_EQ(missile->damage,73);T_FEQ(missile->velocity,.9f,0);
        T_ASSERT(missile->aiflags&AI_PROJECTILE_FIXED_TARGET);
        T_NOT_NULL(missile->artillery);
        if (missile->artillery) {
            T_EQ(missile->artillery->attack_type,ATK_SIEGE);
            T_EQ(missile->artillery->area_targets,WC3_TARGET_FLAG_TREE);
        }
        T_FEQ(missile->channel->origin.x,600,0);
    }
    T_EQ(actor->current_order_id,G_OrderId("attackground"));
}

TEST(wc3_attack_orders, once_retires_at_projectile_launch_recovery_before_impact) {
    edict_t *actor=attack_orders_setup(),*enemy=attack_orders_enemy();if (!actor || !enemy) return;
    unitAttack_t *profile=S_AttackProfileWrite(actor,0);
    profile->targetsAllowed|=WC3_TARGET_FLAG_AIR;profile->weapon=WPN_MISSILE;
    profile->range=512;profile->backswingPoint=.47f;profile->projectile.speed=1;
    attack_orders_call("once");
    edict_t *missile=NULL;
    FOR_LOOP(i,80) {
        attack_orders_frames(1);
        FILTER_EDICTS(unit,unit->owner==actor && unit->movetype==MOVETYPE_FLYMISSILE) missile=unit;
        if (missile) break;
    }
    T_NOT_NULL(missile);T_FEQ(enemy->health.value,5000,0);
    T_EQ(actor->current_order_id,G_OrderId("attackonce"));
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},true,0,0));
    attack_orders_frames(3);T_EQ(actor->current_order_id,G_OrderId("attackonce"));
    attack_orders_frames(3);T_EQ(actor->current_order_id,G_OrderId("move"));
    T_FEQ(enemy->health.value,5000,0);
    unsigned count=0;FILTER_EDICTS(unit,unit->owner==actor && unit->movetype==MOVETYPE_FLYMISSILE) count++;
    T_EQ(count,1);
}

TEST(wc3_attack_orders, once_death_and_rejection_do_not_publish_or_reuse_the_head) {
    edict_t *actor=attack_orders_setup(),*enemy=attack_orders_enemy();if (!actor || !enemy) return;
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},false,0,0));
    T_ASSERT(!G_IssueUnitTargetOrder(actor,"attackonce",enemy,false,0));
    T_EQ(actor->current_order_id,G_OrderId("move"));
    actor=attack_orders_once();if (!actor) return;
    attack_orders_call("kill");T_EQ(actor->current_order_id,0);
    G_FreeEdict(actor);level.time+=1001;attack_orders_call("recreate");
    attack_orders_frames(8);T_EQ(actor->current_order_id,0);T_ASSERT(!actor->attack_swing.active);
}

TEST(wc3_attack_orders, ground_pause_replacement_and_death_preserve_order_ownership) {
    edict_t *actor=attack_orders_setup();if (!actor) return;
    S_AttackProfileWrite(actor,0)->weapon=WPN_NORMAL;
    attack_orders_call("ground");actor->paused=true;
    attack_orders_frames(8);T_FEQ(actor->s.origin2.x,64,0);
    T_EQ(actor->current_order_id,G_OrderId("attackground"));
    actor->paused=false;attack_orders_frames(8);T_ASSERT(actor->s.origin2.x>64);
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},false,0,0));
    attack_orders_frames(4);T_EQ(actor->current_order_id,G_OrderId("move"));
    attack_orders_call("ground");attack_orders_call("kill");T_EQ(actor->current_order_id,0);
}
TEST(wc3_attack_orders, once_saved_approach_commits_one_hit_then_starts_queued_move) {
    slkTestData_t *rows=parse_slk_string("ID;PWXL;N;E\nB;Y2;X2;D0\n"
        "C;X1;Y1;K\"unitWeaponID\"\nC;X2;K\"weapsOn\"\n"
        "C;X1;Y2;K\"hfoo\"\nC;X2;K1\nE\n");
    slkTestData_t *old=G_SetSLKRows("UnitWeapons",rows);
    edict_t *actor=attack_orders_setup(),*enemy=attack_orders_enemy();
    if (!actor || !enemy) { G_SetSLKRows("UnitWeapons",old);free_slk_rows(rows);return; }
    S_AttackProfileWrite(actor,0)->targetsAllowed|=WC3_TARGET_FLAG_AIR;
    S_AttackProfileWrite(actor,0)->backswingPoint=.47f;
    attack_orders_call("once");
    T_ASSERT(G_IssueUnitPointOrder(actor,"move",&(vec2_t){768,64},true,0,0));
    attack_orders_frames(4);T_FEQ(enemy->health.value,5000,0);
    cstring_t save="/tmp/wc3-attack-once-approach175.bin";
    T_ASSERT(WriteGame(save));T_ASSERT(ReadGame(save));remove(save);
    T_EQ(actor->current_order_id,G_OrderId("attackonce"));
    FOR_LOOP(i,160) {
        if (actor->current_order_id==G_OrderId("move")) break;
        attack_orders_frames(1);
    }
    T_EQ(actor->current_order_id,G_OrderId("move"));T_EQ(actor->order_queue.count,0);
    T_FEQ(enemy->health.value,4999,0);
    attack_orders_frames(8);T_FEQ(enemy->health.value,4999,0);
    G_SetSLKRows("UnitWeapons",old);free_slk_rows(rows);
}
#endif
