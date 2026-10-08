#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"

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
#endif
