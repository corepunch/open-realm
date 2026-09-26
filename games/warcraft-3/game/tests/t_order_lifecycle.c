#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
void order_attack(edict_t *self, edict_t *target);
void T_Damage(edict_t *target, edict_t *attacker, int damage);
void SV_Physics_Toss(edict_t *ent);
void unit_build(edict_t *self, uint32_t class_id);
void attack_melee_cooldown(edict_t *self);
void ai_train_build(edict_t *self);
static slkTestData_t *building_install_repair_data(slkTestData_t **rows_out);
static void building_restore_repair_data(slkTestData_t *old, slkTestData_t *rows);

/* Keep production order, acquisition, and death entry points active in this review fixture. */
static edict_t *review_order_unit(float x, uint32_t owner) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, 0);
    ((mapInfo_t *)level.mapinfo)->players[owner].playerType = kPlayerTypeHuman;
    ent->s.player = owner;
    ent->svflags |= SVF_MONSTER;
    ent->movetype = MOVETYPE_STEP;
    ent->stand = unit_stand;
    ent->die = unit_die;
    ent->unitinfo.MoveSpeed = 300;
    ent->attack1.type = ATK_NORMAL;
    ent->attack1.range = 30;
    ent->attack1.cooldown = 1;
    ent->attack1.damageBase = 10;
    ent->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND | WC3_TARGET_FLAG_STRUCTURE;
    ent->targtype = TARG_GROUND;
    ent->runtime.acquisition_range = 600;
    unit_stand(ent);
    gi.LinkEntity(ent);
    return ent;
}

TEST(wc3_order_lifecycle, hold_position_does_not_chase_acquired_enemy) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    T_ASSERT(S_HoldPosition(unit));
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_ASSERT(unit->movement.holding_position);
    unit->currentmove->think(unit);
    T_FEQ(unit->s.origin.x, 0, 0.001f);
}

TEST(wc3_order_lifecycle, hold_attacks_in_range_then_stays_when_enemy_leaves) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(20, 1);
    T_ASSERT(S_HoldPosition(unit));
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    unit->currentmove->think(unit);
    T_STREQ(unit->currentmove->animation, "attack");
    enemy->s.origin.x = 300;
    attack_melee_cooldown(unit);
    unit->currentmove->think(unit);
    unit->currentmove->think(unit);
    T_FEQ(unit->s.origin.x, 0, 0.001f);
    T_ASSERT(unit->movement.holding_position);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, delayed_kill_preserves_new_move_order) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    vector2_t point = {600, 0};
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    edict_t *missile = G_Spawn();
    missile->owner = unit;
    missile->goalentity = enemy;
    missile->velocity = 10000;
    missile->damage = 10000;
    T_ASSERT(unit_issueorder(unit, "move", &point));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &MAKE(vector2_t, .x = 800), true, 0, 0));
    umove_t const *move = unit->currentmove;
    /* A projectile resolves damage after its owner has already accepted Move. */
    SV_Physics_Toss(missile);
    T_ASSERT(M_IsDead(enemy));
    T_ASSERT(unit->currentmove == move);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
}

TEST(wc3_order_lifecycle, explicit_attack_replaces_persistent_follow) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(300, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    T_ASSERT(unit->movement.follow_target == ally);
    G_SetHealth(enemy, 0);
    T_ASSERT(!unit_issuetargetorder(unit, "attack", enemy));
    T_ASSERT(unit->movement.follow_target == ally);
    T_ASSERT(unit->goalentity == ally);
    G_SetHealth(enemy, enemy->health.max_value);
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_NULL(unit->movement.follow_target);
}

TEST(wc3_order_lifecycle, queued_attack_preserves_follow_until_follow_completes) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(300, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    T_ASSERT(G_IssueUnitTargetOrder(unit, "attack", enemy, true, 0));
    T_ASSERT(unit->movement.follow_target == ally);
    T_ASSERT(unit->goalentity == ally);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    G_SetHealth(ally, 0);
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_NULL(unit->movement.follow_target);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, auto_attack_resumes_patrol_but_smart_attack_replaces_it) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *first = review_order_unit(300, 1);
    edict_t *second = review_order_unit(400, 1);
    order_patrol(unit, Waypoint_add(&MAKE(vector2_t, .x = 600)));
    edict_t *patrol = unit->movement.patrol_target;
    order_attack(unit, first);
    T_Damage(first, unit, (int)first->health.value);
    T_ASSERT(unit->goalentity == patrol);
    T_ASSERT(unit->currentmove->proc == CAbilityPatrol);
    T_ASSERT(unit_issuetargetorder(unit, "smart", second));
    T_ASSERT(unit->goalentity == second);
    T_ASSERT(unit->currentmove->proc == CAbilityAttack);
    T_Damage(second, unit, (int)second->health.value);
    T_NULL(unit->movement.patrol_a);
    T_NULL(unit->movement.patrol_b);
    T_NULL(unit->movement.patrol_target);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, animationless_melee_kill_preserves_resumed_follow) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(20, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    unit->attack1.damagePoint = (float)FRAMETIME / 1000.0f;
    G_SetHealth(enemy, 1);
    order_attack(unit, enemy);
    unit->currentmove->think(unit);
    unit->currentmove->think(unit);
    T_ASSERT(M_IsDead(enemy));
    T_ASSERT(unit->goalentity == ally);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, finishing_repair_preserves_production_queue) {
    setup_test_world();
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    edict_t *worker = review_order_unit(0, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    UnitAbilities_t abilities = { .abilList = "Arep" };
    worker->data.UnitAbilities = &abilities;
    building->stand = unit_stand;
    building->svflags |= SVF_MONSTER;
    UnitBalance_t balance = *building->data.UnitBalance;
    balance.reptm = 10;
    building->data.UnitBalance = &balance;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    unit_build(building, MAKEFOURCC('h','f','o','o'));
    edict_t *queued = building->build;
    T_NOT_NULL(queued);
    UnitBalance_t trainee = *queued->data.UnitBalance;
    trainee.buildTime = 20;
    queued->data.UnitBalance = &trainee;
    queued->health.max_value = 420;
    queued->health.value = 0;
    queued->stand = unit_stand;
    building->health.value = building->health.max_value - 0.001f;
    T_ASSERT(S_OrderRepair(worker, building, 0));
    worker->currentmove->think(worker);
    building_restore_repair_data(old, rows);
    T_FEQ(building->health.value, building->health.max_value, 0.0001f);
    T_ASSERT(building->build == queued);
    T_ASSERT(building->currentmove->proc == CAbilityTrain);
    float progress = queued->health.value;
    ai_train_build(building);
    T_ASSERT(queued->health.value > progress);
}
#endif
