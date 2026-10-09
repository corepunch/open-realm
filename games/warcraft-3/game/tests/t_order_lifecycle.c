#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
bool run_test_jass(cstring_t src);
void order_attack(edict_t *self, edict_t *target);
void T_Damage(edict_t *target, edict_t *attacker, int damage);
void SV_Physics_Toss(edict_t *ent);
void unit_build(edict_t *self, uint32_t class_id);
void attack_melee_cooldown(edict_t *self);
void ai_train_build(edict_t *self);
static slkTestData_t *building_install_repair_data(slkTestData_t **rows_out);
static void building_restore_repair_data(slkTestData_t *old, slkTestData_t *rows);

static bool lifecycle_stop_frame_seen, lifecycle_hold_frame_seen;
static bool lifecycle_move_frame_seen;
static uint32_t lifecycle_stop_frame_flags, lifecycle_hold_frame_flags, lifecycle_move_frame_flags;
static uint32_t lifecycle_stop_frame_texture, lifecycle_hold_frame_texture;

static void lifecycle_capture_command_frame(pfWriteType_t type, void const *value) {
    uiFrame_t const *frame;
    if (type != PF_UIFRAME || !value) return;
    frame = value;
    if (frame->flags.type != FT_COMMANDBUTTON) return;
    if (frame->onclick && !strcmp(frame->onclick, "button CmdStop")) {
        lifecycle_stop_frame_seen = true;
        lifecycle_stop_frame_flags = frame->flagsvalue;
        lifecycle_stop_frame_texture = frame->tex.index;
    } else if (frame->onclick && !strcmp(frame->onclick, "button CmdHoldPos")) {
        lifecycle_hold_frame_seen = true;
        lifecycle_hold_frame_flags = frame->flagsvalue;
        lifecycle_hold_frame_texture = frame->tex.index;
    } else if (frame->onclick && !strcmp(frame->onclick, "button CmdMove")) {
        lifecycle_move_frame_seen = true;
        lifecycle_move_frame_flags = frame->flagsvalue;
    }
}

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
    S_AttackProfileWrite(ent, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(ent, 0)->range = 30;
    S_AttackProfileWrite(ent, 0)->cooldown = 1;
    S_AttackProfileWrite(ent, 0)->damageBase = 10;
    S_AttackProfileWrite(ent, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND | WC3_TARGET_FLAG_STRUCTURE;
    ent->targtype = TARG_GROUND;
    ent->runtime.acquisition_range = 600;
    unit_stand(ent);
    gi.LinkEntity(ent);
    return ent;
}

TEST(wc3_order_lifecycle, unused_queue_is_sparse_and_wrapped_entries_survive_save) {
    cstring_t file = Test_TempPath("wc3-sparse-order-ring.bin");
    reset_entities(); setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    T_NULL(unit->order_queue.entries);
    G_ClearUnitOrderQueue(unit);
    T_NULL(unit->order_queue.entries);
    FOR_LOOP(i, MAX_UNIT_ORDER_QUEUE) {
        vec2_t point = { (float)i, -(float)i };
        T_ASSERT(G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE,
                                 &point, NULL, 0, 0, i));
    }
    unitOrder_t *storage = unit->order_queue.entries;
    T_NOT_NULL(storage);
    T_ASSERT(!G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0, 100));
    T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    T_EQ(unit->order_queue.head, 1); T_EQ(unit->order_queue.count, MAX_UNIT_ORDER_QUEUE - 1);
    vec2_t last = { 900, 800 };
    T_ASSERT(G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE, &last, NULL, 3, 7, 999));
    T_ASSERT(WriteGame(file));
    G_ClearUnitOrderQueue(unit);
    T_NULL(unit->order_queue.entries);
    T_EQ(unit->order_queue.count, 0);
    T_ASSERT(ReadGame(file));
    T_EQ(unit->order_queue.head, 1); T_EQ(unit->order_queue.count, MAX_UNIT_ORDER_QUEUE);
    FOR_LOOP(i, MAX_UNIT_ORDER_QUEUE) {
        unitOrder_t const *entry = unit->order_queue.entries + unit->order_queue.head;
        T_EQ(entry->order_id, i == MAX_UNIT_ORDER_QUEUE - 1 ? 999 : i + 1);
        T_STREQ(entry->order, "holdposition");
        T_FEQ(entry->point.x, i == MAX_UNIT_ORDER_QUEUE - 1 ? 900 : i + 1, 0);
        T_ASSERT(G_UnitStartNextQueuedOrder(unit));
    }
    T_EQ(unit->order_queue.head, 0); T_EQ(unit->order_queue.count, 0);
    T_NULL(unit->order_queue.entries);
    T_ASSERT(WriteGame(file));
    T_ASSERT(ReadGame(file));
    T_NULL(unit->order_queue.entries);
    G_FreeEdict(unit);
    T_NULL(unit->order_queue.entries);
    remove(file);
    reset_entities(); setup_test_world();
}

/* Cold allocation, ordered cancellation and final dispatch must return storage
 * without retaining a bucket on every unit that once received Shift orders. */
TEST(wc3_order_lifecycle, pool192_empty_queues_release_storage_and_reuse_lifo) {
    enum { COUNT = 129 };
    edict_t *units[COUNT];
    unitOrder_t *slots[COUNT];
    reset_entities(); setup_test_world();
    FOR_LOOP(i, COUNT) {
        units[i] = review_order_unit(0, 0);
        T_NULL(units[i]->order_queue.entries);
        T_ASSERT(G_QueueUnitOrder(units[i], "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0, 0));
        slots[i] = units[i]->order_queue.entries;
        T_NOT_NULL(slots[i]);
        FOR_LOOP(j, i) T_NE(slots[i], slots[j]);
    }
    FOR_LOOP(i, COUNT) {
        T_ASSERT(unit_issueimmediateorder(units[i], "stop"));
        T_NULL(units[i]->order_queue.entries);
        T_EQ(units[i]->order_queue.count, 0);
        T_EQ(units[i]->order_queue.head, 0);
    }
    FOR_LOOP(i, COUNT) {
        T_ASSERT(G_QueueUnitOrder(units[i], "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0, 0));
        T_EQ(units[i]->order_queue.entries, slots[COUNT - 1 - i]);
    }
    FOR_LOOP(i, COUNT) {
        T_ASSERT(G_UnitStartNextQueuedOrder(units[i]));
        T_NULL(units[i]->order_queue.entries);
        T_ASSERT(units[i]->movement.holding_position);
        T_ASSERT(!G_UnitStartNextQueuedOrder(units[i]));
        G_ClearUnitOrderQueue(units[i]);
        T_NULL(units[i]->order_queue.entries);
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, pool192_discarded_target_and_replacement_release_storage) {
    reset_entities(); setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *target = review_order_unit(256, 1);
    vec2_t point = {512, 0};
    T_ASSERT(G_QueueUnitOrder(unit, "attack", UNIT_ORDER_TARGET_ENTITY, NULL, target, 0, 0, 0));
    T_NOT_NULL(unit->order_queue.entries);
    target->spawn_time++;
    T_ASSERT(!G_UnitStartNextQueuedOrder(unit));
    T_NULL(unit->order_queue.entries);
    T_ASSERT(G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0, 0));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &point, false, 0, 0));
    T_NULL(unit->order_queue.entries);
    T_EQ(unit->current_order_id, G_OrderId("move"));
    reset_entities(); setup_test_world();
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
    vec2_t point = {600, 0};
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    edict_t *missile = G_Spawn();
    missile->owner = unit;
    S_SetMoveGoal(missile, &missile->goalentity, enemy);
    missile->velocity = 10000;
    missile->damage = 10000;
    T_ASSERT(unit_issueorder(unit, "move", &point));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &MAKE(vec2_t, .x = 800), true, 0, 0));
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
    /* Native207160 accepts this as a point Attack Move snapshot. */
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    T_NULL(unit->movement.follow_target);
    T_NOT_NULL(unit->movement.attackmove_waypoint);
    T_FEQ(unit->goalentity->s.origin2.x, enemy->s.origin2.x, 0);
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
    order_patrol(unit, Waypoint_add(&MAKE(vec2_t, .x = 600)));
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

TEST(wc3_order_lifecycle, patrol_head_differs_from_issued_command_through_combat) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(30, 1);
    vec2_t const endpoint = {600, 0};
    T_EQ(G_OrderId("patrol"), 851990);
    T_ASSERT(G_IssueUnitPointOrder(unit, "patrol", &endpoint, false, 0, 0));
    T_EQ(G_GetIssuedOrderId(unit), 851990);
    T_EQ(unit->current_order_id, 851991);
    edict_t *patrol = unit->movement.patrol_target;
    order_attack(unit, enemy);
    T_EQ(unit->current_order_id, 851991);
    T_ASSERT(!G_IssueUnitTargetOrder(unit, "repair", enemy, false, 0));
    T_EQ(unit->current_order_id, 851991);
    T_ASSERT(unit->currentmove->proc == CAbilityAttack);
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_EQ(unit->current_order_id, 851991);
    T_ASSERT(unit->goalentity == patrol);
    T_ASSERT(unit->currentmove->proc == CAbilityPatrol);
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &(vec2_t){768, 0}, false, 0, 0));
    T_EQ(unit->current_order_id, 851986);
    T_NULL(unit->movement.patrol_a);
}

TEST(wc3_order_lifecycle, animationless_melee_kill_preserves_resumed_follow) {
    setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(20, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    S_AttackProfileWrite(unit, 0)->damagePoint = (float)FRAMETIME / 1000.0f;
    G_SetHealth(enemy, 1);
    order_attack(unit, enemy);
    unit->currentmove->think(unit);
    unit->currentmove->think(unit);
    T_ASSERT(M_IsDead(enemy));
    /* The committed hit retains Follow through its independent swing wait;
     * no animation-end callback is required to resume it. */
    T_EQ(unit->movement.follow_target, ally);
    T_ASSERT(unit->attack_swing.active);
    T_NULL(unit->goalentity);
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    FOR_LOOP(i, 8) { level.time += FRAMETIME; globals.RunFrame(); }
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
    building->health.value = building->health.max_value - 1.0f;
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

TEST(wc3_order_lifecycle, stop_auto_acquires_and_chases_after_returning_to_idle) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    order_stop(unit);
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_ASSERT(!unit->movement.holding_position);
    T_ASSERT(unit->currentmove->proc == CAbilityAttack);
}

TEST(wc3_order_lifecycle, queued_stop_preserves_later_fifo_work) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    vec2_t first = {300, 0}, second = {600, 0};

    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &first, false, 0, 0.0f));
    T_ASSERT(G_QueueUnitOrder(unit, "stop", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0.0f, 0));
    T_ASSERT(G_QueueUnitOrder(unit, "move", UNIT_ORDER_TARGET_POINT, &second, NULL, 0, 0.0f, 0));
    T_EQ(G_UnitQueuedOrderCount(unit), 2);

    unit_stand(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    T_ASSERT(!unit->movement.holding_position);
    T_ASSERT(unit->currentmove->think == ai_stand);

    unit->currentmove->think(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, queued_hold_preserves_later_fifo_work) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    vec2_t first = {300, 0}, second = {600, 0};

    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &first, false, 0, 0.0f));
    T_ASSERT(G_QueueUnitOrder(unit, "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL, 0, 0.0f, 0));
    T_ASSERT(G_QueueUnitOrder(unit, "move", UNIT_ORDER_TARGET_POINT, &second, NULL, 0, 0.0f, 0));
    T_EQ(G_UnitQueuedOrderCount(unit), 2);

    unit_stand(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    T_ASSERT(unit->movement.holding_position);

    unit->currentmove->think(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_ASSERT(!unit->movement.holding_position);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, hold_position_interrupts_active_channel) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    if (!unit->channel) unit->channel = G_AllocChannel();
    assert(unit->channel);
    unit->channel->code = MAKEFOURCC('A','x','x','x');

    T_ASSERT(S_HoldPosition(unit));
    T_ASSERT(!unit->channel || unit->channel->code == 0);
    T_ASSERT(unit->movement.holding_position);
}

TEST(wc3_order_lifecycle, hold_position_owns_common_stand_transition) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);

    T_ASSERT(S_HoldPosition(unit));
    T_ASSERT(unit->currentmove == &holdpos_move_stand);
    unit_stand(unit);
    T_ASSERT(unit->currentmove == &holdpos_move_stand);
}

TEST(wc3_order_lifecycle, stop_and_hold_buttons_expose_engaged_state) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    gameCommandButton_t stop, hold;

    T_ASSERT(G_BuildCommandButton(unit, STR_CmdStop, false, 0, &stop));
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
    T_ASSERT(stop.queueable);
    T_ASSERT(hold.queueable);
    T_EQ(stop.engaged, 1);
    T_EQ(hold.engaged, 0);

    T_ASSERT(S_HoldPosition(unit));
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdStop, false, 0, &stop));
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
    T_EQ(stop.engaged, 0);
    T_EQ(hold.engaged, 1);

    {
        edict_t *enemy = review_order_unit(20, 1);
        level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
        unit->currentmove->think(unit);
        T_ASSERT(unit->goalentity == enemy);
        T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
        T_EQ(hold.engaged, 1);
        S_SetMoveGoal(unit, &unit->goalentity, NULL);
        unit_stand(unit);
        T_ASSERT(G_BuildCommandButton(unit, STR_CmdStop, false, 0, &stop));
        T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
        T_EQ(stop.engaged, 0);
        T_EQ(hold.engaged, 1);
    }

    order_stop(unit);
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdStop, false, 0, &stop));
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
    T_EQ(stop.engaged, 1);
    T_EQ(hold.engaged, 0);
}

TEST(wc3_order_lifecycle, hold_button_command_refresh_publishes_new_engaged_state) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent, *unit;
    gameCommandButton_t hold;
    cstring_t command[] = { "button", "CmdHoldPos" };

    setup_test_world();
    clent = &g_edicts[0];
    clent->inuse = true;
    clent->client->connected = true;
    clent->client->ps.number = 0;
    unit = review_order_unit(0, 0);
    G_SelectEntity(clent->client, unit);

    lifecycle_stop_frame_seen = lifecycle_hold_frame_seen = lifecycle_move_frame_seen = false;
    lifecycle_stop_frame_flags = lifecycle_hold_frame_flags = 0;
    lifecycle_move_frame_flags = 0;
    lifecycle_stop_frame_texture = lifecycle_hold_frame_texture = 0;
    gi.Write = lifecycle_capture_command_frame;
    G_ClientCommand(clent, 2, command);
    gi.Write = old_write;

    T_ASSERT(unit->movement.holding_position);
    T_ASSERT(G_BuildCommandButton(unit, STR_CmdHoldPos, false, 0, &hold));
    T_EQ(hold.engaged, 1);
    T_ASSERT(lifecycle_stop_frame_seen);
    T_ASSERT(lifecycle_hold_frame_seen);
    T_ASSERT(lifecycle_move_frame_seen);
    T_ASSERT(lifecycle_stop_frame_texture != 0);
    T_ASSERT(lifecycle_hold_frame_texture != 0);
    T_ASSERT(!(lifecycle_stop_frame_flags & UIFLAG_ABILITY_ENGAGED));
    T_ASSERT(lifecycle_hold_frame_flags & UIFLAG_ABILITY_ENGAGED);
    T_ASSERT(lifecycle_stop_frame_flags & UIFLAG_ORDER_QUEUEABLE);
    T_ASSERT(lifecycle_hold_frame_flags & UIFLAG_ORDER_QUEUEABLE);
    T_ASSERT(!(lifecycle_move_frame_flags & UIFLAG_ORDER_QUEUEABLE));
}
/* A right-click move and the later return to idle change Stop's glow without a command-card click. */
TEST(wc3_order_lifecycle, move_and_idle_transitions_refresh_stop_glow) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent, *unit;
    vec2_t point = {500, 0};

    setup_test_world();
    clent = &g_edicts[0];
    clent->inuse = true;
    clent->client->connected = true;
    clent->client->ps.number = 0;
    unit = review_order_unit(0, 0);
    G_SelectEntity(clent->client, unit);
    order_stop(unit);
    clent->client->commands_dirty = false;

    T_ASSERT(G_IssueUnitPointOrder(unit, "smart", &point, false, 0, 0.0f));
    T_ASSERT(clent->client->commands_dirty);
    lifecycle_stop_frame_seen = false;
    lifecycle_stop_frame_flags = 0;
    gi.Write = lifecycle_capture_command_frame;
    Get_Commands_f(clent);
    gi.Write = old_write;
    T_ASSERT(lifecycle_stop_frame_seen);
    T_ASSERT(!(lifecycle_stop_frame_flags & UIFLAG_ABILITY_ENGAGED));

    clent->client->commands_dirty = false;
    unit_stand(unit);
    T_ASSERT(clent->client->commands_dirty);
    lifecycle_stop_frame_seen = false;
    gi.Write = lifecycle_capture_command_frame;
    Get_Commands_f(clent);
    gi.Write = old_write;
    T_ASSERT(lifecycle_stop_frame_seen);
    T_ASSERT(lifecycle_stop_frame_flags & UIFLAG_ABILITY_ENGAGED);
}

TEST(wc3_order_lifecycle, stop_records_guard_position_and_returns_after_auto_combat) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);

    order_stop(unit);
    T_ASSERT(unit->movement.guard_state == GUARD_IDLE);
    T_FEQ(unit->movement.guard_position.x, 0, 0.001f);
    T_FEQ(unit->movement.guard_position.y, 0, 0.001f);

    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_ASSERT(unit->movement.guard_state == GUARD_COMBAT);

    unit->s.origin2.x = unit->s.origin.x = 180;
    gi.LinkEntity(unit);
    T_Damage(enemy, unit, (int)enemy->health.value);

    T_ASSERT(unit->movement.guard_state == GUARD_RETURNING);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
    T_NOT_NULL(unit->goalentity);
    T_FEQ(unit->goalentity->s.origin2.x, 0, 0.001f);
    T_FEQ(unit->goalentity->s.origin2.y, 0, 0.001f);
}

TEST(wc3_order_lifecycle, internal_stop_cleanup_does_not_create_guard_position) {
    setup_test_world();
    edict_t *unit = review_order_unit(96, 0);

    order_stop_cleanup(unit);

    T_ASSERT(unit->movement.guard_state == GUARD_NONE);
}

TEST(wc3_order_lifecycle, guard_return_completion_restores_stopped_idle) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);

    order_stop(unit);
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    unit->s.origin2.x = unit->s.origin.x = 120;
    gi.LinkEntity(unit);
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_ASSERT(unit->movement.guard_state == GUARD_RETURNING);

    unit->s.origin2 = unit->movement.guard_position;
    unit->s.origin.x = unit->s.origin2.x;
    unit->s.origin.y = unit->s.origin2.y;
    gi.LinkEntity(unit);
    unit->currentmove->think(unit);

    T_ASSERT(unit->movement.guard_state == GUARD_IDLE);
    T_ASSERT(unit->currentmove->think == ai_stand);
}

TEST(wc3_order_lifecycle, explicit_move_clears_old_stop_guard) {
    setup_test_world();
    edict_t *unit = review_order_unit(40, 0);
    vec2_t point = {500, 0};

    order_stop(unit);
    T_ASSERT(unit->movement.guard_state == GUARD_IDLE);
    T_FEQ(unit->movement.guard_position.x, 40, 0.001f);

    T_ASSERT(unit_issueorder(unit, "move", &point));
    T_ASSERT(unit->movement.guard_state == GUARD_NONE);
}

TEST(wc3_order_lifecycle, second_stop_refreshes_guard_position) {
    setup_test_world();
    edict_t *unit = review_order_unit(20, 0);

    order_stop(unit);
    T_FEQ(unit->movement.guard_position.x, 20, 0.001f);

    unit->s.origin2.x = unit->s.origin.x = 240;
    gi.LinkEntity(unit);
    order_stop(unit);

    T_ASSERT(unit->movement.guard_state == GUARD_IDLE);
    T_FEQ(unit->movement.guard_position.x, 240, 0.001f);
}

TEST(wc3_order_lifecycle, queued_player_order_outranks_guard_return) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    vec2_t point = {700, 0};

    order_stop(unit);
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->movement.guard_state == GUARD_COMBAT);
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &point, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(unit), 1);

    unit->s.origin2.x = unit->s.origin.x = 160;
    gi.LinkEntity(unit);
    T_Damage(enemy, unit, (int)enemy->health.value);

    T_ASSERT(unit->movement.guard_state == GUARD_NONE);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, repair_family_heads_survive_approach_and_complete_at_work) {
    static cstring_t const names[] = {"repair", "renew", "restoration"};
    static cstring_t const codes[] = {"Arep", "Aren", "Arst"};
    static uint32_t const ids[] = {852024, 852161, 852202};
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    FOR_LOOP(i, 3) {
        reset_entities(); setup_test_world();
        edict_t *worker = review_order_unit(0, 0);
        edict_t *target = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 512, 0);
        UnitAbilities_t abilities = {.abilList = codes[i]};
        worker->data.UnitAbilities = &abilities;
        target->health.value = target->health.max_value - 100;
        gi.LinkEntity(target);
        T_EQ(G_OrderId(names[i]), ids[i]);
        T_ASSERT(G_IssueUnitTargetOrder(worker, names[i], target, false, 0));
        T_EQ(worker->current_order_id, ids[i]);
        T_EQ(worker->build, target);
        target->health.value = target->health.max_value;
        /* Full health during approach is completion at contact, not target loss. */
        worker->currentmove->think(worker);
        T_EQ(worker->current_order_id, ids[i]);
        T_EQ(worker->build, target);
        worker->s.origin2 = target->s.origin2; gi.LinkEntity(worker);
        worker->currentmove->think(worker);
        worker->currentmove->think(worker);
        T_EQ(worker->current_order_id, 0); T_NULL(worker->build);
        target->health.value -= 100;
        T_ASSERT(G_IssueUnitTargetOrder(worker, "smart", target, false, 0));
        T_EQ(worker->current_order_id, 851971);
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &(vec2_t){768,0}, true, 0, 0));
        umove_t const *work = worker->currentmove;
        worker->buildwork->gold_accum = 0.25f;
        T_ASSERT(G_IssueUnitTargetOrder(worker, names[i], target, false, 0));
        T_EQ(worker->current_order_id, 851971); T_EQ(worker->currentmove, work);
        T_EQ(worker->order_queue.count, 1); T_FEQ(worker->buildwork->gold_accum, 0.25f, 0);
        /* An unknown family must not retag or replace an accepted Smart repair. */
        T_ASSERT(!G_IssueUnitTargetOrder(worker, names[(i + 1) % 3], target, false, 0));
        T_EQ(worker->current_order_id, 851971);
        T_EQ(worker->order_queue.count, 1);
        T_ASSERT(unit_issueimmediateorder(worker, "stop"));
        T_EQ(worker->current_order_id, 0); T_NULL(worker->build);
        T_ASSERT(S_OrderRepair(worker, target, 0));
        T_EQ(worker->current_order_id, 0); /* Internal construction work has no public head. */
        unit_issueimmediateorder(worker, "stop");
    }
    reset_entities(); setup_test_world();
    building_restore_repair_data(old, rows);
}

TEST(wc3_order_lifecycle, repair_autocast_orders_validate_direction_before_interrupting) {
    static cstring_t const codes[] = {"Arep", "Aren", "Arst"};
    static cstring_t const on[] = {"repairon", "renewon", "restorationon"};
    static cstring_t const off[] = {"repairoff", "renewoff", "restorationoff"};
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    FOR_LOOP(i, 3) {
        reset_entities(); setup_test_world();
        edict_t *worker = review_order_unit(0, 0);
        UnitAbilities_t abilities = {.abilList = codes[i]};
        worker->data.UnitAbilities = &abilities;
        vec2_t goal = {512, 0}, pending = {768, 0};
        T_ASSERT(!unit_issueimmediateorder(worker, off[i]));
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &goal, false, 0, 0));
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &pending, true, 0, 0));
        T_ASSERT(unit_issueimmediateorder(worker, on[i]));
        T_EQ(worker->current_order_id, 0); T_EQ(G_UnitQueuedOrderCount(worker), 0);
        T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &goal, false, 0, 0));
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &pending, true, 0, 0));
        edict_t *destination = worker->goalentity; umove_t const *move = worker->currentmove;
        T_ASSERT(!unit_issueimmediateorder(worker, on[i]));
        T_EQ(worker->current_order_id, 851986); T_EQ(worker->goalentity, destination);
        T_EQ(worker->currentmove, move); T_EQ(G_UnitQueuedOrderCount(worker), 1);
        G_SetPlayerAbilityAvailable(&game.clients[0], FS_SLKKey(codes[i]), false);
        T_ASSERT(!unit_issueimmediateorder(worker, off[i]));
        T_EQ(worker->current_order_id, 851986); T_EQ(G_UnitQueuedOrderCount(worker), 1);
        G_SetPlayerAbilityAvailable(&game.clients[0], FS_SLKKey(codes[i]), true);
        T_ASSERT(unit_issueimmediateorder(worker, off[i]));
        T_EQ(worker->current_order_id, 0); T_EQ(G_UnitQueuedOrderCount(worker), 0);
        T_ASSERT(!(worker->aiflags & AI_AUTOCAST_REPAIR));
        T_ASSERT(!unit_issueimmediateorder(worker, off[i]));
    }
    reset_entities(); setup_test_world();
    building_restore_repair_data(old, rows);
}

TEST(wc3_order_lifecycle, repair_removed_target_cannot_become_a_reused_building) {
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    reset_entities(); setup_test_world();
    edict_t *worker = review_order_unit(0, 0);
    UnitAbilities_t abilities = {.abilList = "Arep"};
    worker->data.UnitAbilities = &abilities;
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 512, 0);
    target->spawn_time = level.time;
    target->health.value -= 100;
    T_ASSERT(G_IssueUnitTargetOrder(worker, "repair", target, false, 0));
    G_FreeEdict(target);
    T_EQ(worker->current_order_id, 852024);
    level.time += 1001;
    edict_t *replacement = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    replacement->spawn_time = level.time;
    T_EQ(replacement, target); replacement->health.value -= 100;
    float health = replacement->health.value;
    worker->currentmove->think(worker);
    T_EQ(worker->current_order_id, 0); T_NULL(worker->build);
    T_FEQ(replacement->health.value, health, 0);
    T_ASSERT(G_IssueUnitTargetOrder(worker, "repair", replacement, false, 0));
    G_DeferFreeEdict(replacement);
    T_EQ(worker->current_order_id, 852024); T_NULL(worker->build);
    T_ASSERT(G_IssueUnitPointOrder(worker, "move", &(vec2_t){512,0}, false, 0, 0));
    T_EQ(worker->current_order_id, 851986); T_NOT_NULL(worker->goalentity);
    reset_entities(); setup_test_world();
    building_restore_repair_data(old, rows);
}

TEST(wc3_order_lifecycle, queued_repair_family_activates_after_move_in_server_frames) {
    static cstring_t const names[] = {"repair", "renew", "restoration"};
    static cstring_t const codes[] = {"Arep", "Aren", "Arst"};
    static uint32_t const ids[] = {852024, 852161, 852202};
    slkTestData_t *rows, *old = building_install_repair_data(&rows);
    FOR_LOOP(i, 3) {
        reset_entities(); setup_test_world();
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        edict_t *worker = review_order_unit(0, 0);
        edict_t *target = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 512, 0);
        UnitAbilities_t abilities = {.abilList = codes[i]};
        worker->data.UnitAbilities = &abilities;
        worker->think = monster_think;
        target->health.value -= 100;
        gi.LinkEntity(target);
        level.started = level.scriptsConfigured = level.scriptsStarted = true;
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
        game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
        T_ASSERT(G_IssueUnitPointOrder(worker, "move", &(vec2_t){128,0}, false, 0, 0));
        T_ASSERT(G_IssueUnitTargetOrder(worker, names[i], target, true, 0));
        T_EQ(worker->current_order_id, 851986); T_EQ(worker->order_queue.count, 1);
        for (int frame = 0; frame < 120 && worker->order_queue.count; frame++) {
            level.time += FRAMETIME; globals.RunFrame();
        }
        T_EQ(worker->order_queue.count, 0); T_EQ(worker->current_order_id, ids[i]);
        T_EQ(worker->build, target);
        target->health.value = target->health.max_value;
        for (int frame = 0; frame < 120 && worker->current_order_id; frame++) {
            level.time += FRAMETIME; globals.RunFrame();
        }
        T_EQ(worker->current_order_id, 0); T_NULL(worker->build);
        T_EQ(worker->order_queue.count, 0);
    }
    reset_entities(); setup_test_world();
    building_restore_repair_data(old, rows);
}

TEST(wc3_order_lifecycle, public_move_follow_ignores_automatic_combat) {
    reset_entities(); setup_test_world();
    edict_t *subject = review_order_unit(0, 0);
    edict_t *target = review_order_unit(512, 0);
    edict_t *enemy = review_order_unit(48, 1);
    T_ASSERT(G_IssueUnitTargetOrder(subject, "move", target, false, 0));
    level.time = 0; G_BeginAcquisitionFrame();
    G_AcquisitionEntityLinked(enemy);
    T_EQ(G_FindNearestEnemy(subject, G_AcquisitionRange(subject)), enemy);
    subject->currentmove->think(subject);
    T_EQ(subject->goalentity, target);
    T_NE(subject->goalentity, enemy);
    T_EQ(subject->current_order_id, 851986);
    T_ASSERT(subject->currentmove->proc == CAbilityMove);
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, follow_combat_target_loss_preserves_head_until_enemy_loss) {
    reset_entities(); setup_test_world();
    edict_t *subject = review_order_unit(0, 0);
    edict_t *target = review_order_unit(512, 0);
    edict_t *enemy = review_order_unit(48, 1);
    T_ASSERT(G_IssueUnitTargetOrder(subject, "smart", target, false, 0));
    T_ASSERT(G_IssueUnitPointOrder(subject, "move", &(vec2_t){1024, 0}, true, 0, 0));
    order_attack(subject, enemy);
    T_EQ(subject->current_order_id, 851971);
    G_DeferFreeEdict(target);
    T_NULL(subject->movement.follow_target);
    T_EQ(subject->goalentity, enemy);
    T_EQ(subject->current_order_id, 851971);
    T_EQ(subject->order_queue.count, 1);
    unit_die(enemy, NULL);
    T_EQ(subject->current_order_id, 851986);
    T_EQ(subject->order_queue.count, 0);
    T_NE(subject->goalentity, enemy);
    G_TestFinishDeferredFrees();
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, follow_incarnation_and_combat_removal_survive_save_before_drain) {
    cstring_t file = Test_TempPath("wc3-follow-retirement117.bin");
    /* alloc_test_unit supplies transient weapon rows; restore must resolve a
     * real authored row instead. The fixture MPQ has no UnitWeapons.slk. */
    slkTestData_t *weapons = parse_slk_string(
        "ID;PWXL;N;E\nC;Y1;X1;K\"unitWeaponID\"\nC;Y1;X2;K\"weapsOn\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;Y2;X2;K3\nE\n");
    slkTestData_t *old_weapons = G_SetSLKRows("UnitWeapons", weapons);
    reset_entities(); setup_test_world();
    edict_t *subject = review_order_unit(0, 0);
    edict_t *target = review_order_unit(512, 0);
    edict_t *enemy = review_order_unit(48, 1);
    target->spawn_time = 4242;
    game.clients[0].ps.rdflags |= RDF_NOFOG;
    T_ASSERT(G_IssueUnitTargetOrder(subject, "smart", target, false, 0));
    T_EQ(subject->movement.follow_target_spawn_time, 4242);
    T_ASSERT(WriteGame(file));
    subject->movement.follow_target_spawn_time = 1;
    T_ASSERT(ReadGame(file));
    T_EQ(subject->movement.follow_target_spawn_time, 4242);
    T_ASSERT(S_AttackCanTarget(subject, enemy));
    order_attack(subject, enemy);
    T_EQ(subject->current_order_id, 851971);
    T_EQ(subject->goalentity, enemy);
    G_DeferFreeEdict(target);
    T_ASSERT(WriteGame(file));
    T_ASSERT(ReadGame(file));
    T_EQ(subject->current_order_id, 851971);
    T_NULL(subject->movement.follow_target);
    T_ASSERT(G_IsDeferredFree(target));
    G_TestFinishDeferredFrees();
    T_ASSERT(!target->inuse);
    unit_die(enemy, NULL);
    T_EQ(subject->current_order_id, 0);
    remove(file);
    reset_entities(); setup_test_world();
    G_SetSLKRows("UnitWeapons", old_weapons);
    free_slk_rows(weapons);
}

TEST(wc3_order_lifecycle, follow_damage_callback_nested_replacement_survives_owner_exit) {
    for (int smart = 0; smart < 2; smart++) {
        char script[4096];
        reset_entities(); setup_test_world();
        snprintf(script, sizeof(script),
            "globals\nunit subject\nunit target\nunit enemy\ntrigger nested\n"
            "integer removedHead=0\ninteger replacementHead=0\nendglobals\n"
            "function child takes nothing returns nothing\n"
            "call IssuePointOrder(subject, \"move\", 1024.0, 0.0)\n"
            "call RemoveUnit(enemy)\nendfunction\n"
            "function damaged takes nothing returns nothing\n"
            "call RemoveUnit(target)\nset removedHead=GetUnitCurrentOrder(subject)\n"
            "call TriggerExecute(nested)\nset replacementHead=GetUnitCurrentOrder(subject)\nendfunction\n"
            "function verify takes nothing returns nothing\n"
            "call BJassAssert(removedHead==%d, \"Follow loss head\")\n"
            "call BJassAssert(replacementHead==851986, \"nested Move owns head\")\n"
            "call BJassAssert(GetUnitCurrentOrder(subject)==851986, \"old owner cannot complete replacement\")\n"
            "endfunction\nfunction main takes nothing returns nothing\n"
            "local trigger damage=CreateTrigger()\n"
            "set subject=CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
            "set target=CreateUnit(Player(0), 'hfoo', 512.0, 0.0, 0.0)\n"
            "set enemy=CreateUnit(Player(1), 'hfoo', 48.0, 0.0, 0.0)\n"
            "call SetUnitUserData(subject, 900)\ncall SetUnitUserData(enemy, 901)\n"
            "call IssueTargetOrder(subject, \"%s\", target)\n"
            "set nested=CreateTrigger()\ncall TriggerAddAction(nested, function child)\n"
            "call TriggerRegisterUnitEvent(damage, %s, EVENT_UNIT_DAMAGED)\n"
            "call TriggerAddAction(damage, function damaged)\nendfunction\n",
            smart ? 851971 : 0, smart ? "smart" : "move", smart ? "enemy" : "subject");
        T_ASSERT(run_test_jass(script));
        edict_t *subject = NULL, *enemy = NULL;
        FILTER_EDICTS(ent, ent->inuse) {
            if (ent->user_data == 900) subject = ent;
            if (ent->user_data == 901) enemy = ent;
        }
        T_NOT_NULL(subject); T_NOT_NULL(enemy);
        if (subject && enemy) {
            if (smart) order_attack(subject, enemy);
            T_Damage(smart ? enemy : subject, smart ? subject : enemy, 1);
            level.started = level.scriptsConfigured = level.scriptsStarted = true;
            level.time += FRAMETIME; globals.RunFrame();
            jass_callbyname(level.vm, "verify", true);
            T_ASSERT(!jass_rterror_pending(level.vm));
            T_EQ(subject->current_order_id, 851986);
            T_NULL(subject->movement.follow_target);
            T_NE(subject->goalentity, enemy);
            T_ASSERT(!enemy->inuse);
        }
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, smart_combat_enemy_death_resumes_healthy_follow_parent) {
    reset_entities(); setup_test_world();
    edict_t *subject = review_order_unit(0, 0);
    edict_t *target = review_order_unit(512, 0);
    edict_t *enemy = review_order_unit(48, 1);
    T_ASSERT(G_IssueUnitTargetOrder(subject, "smart", target, false, 0));
    order_attack(subject, enemy);
    unit_die(enemy, NULL);
    T_EQ(subject->current_order_id, 851971);
    T_EQ(subject->movement.follow_target, target);
    T_EQ(subject->goalentity, target);
    T_ASSERT(subject->currentmove->proc == CAbilityMove);
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, retired_follow_subject_reuse_does_not_inherit_old_callbacks) {
    for (int smart = 0; smart < 2; smart++) {
        reset_entities(); setup_test_world();
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        edict_t *subject = review_order_unit(0, 0);
        edict_t *target = review_order_unit(512, 0);
        subject->spawn_time = 10;
        T_ASSERT(G_IssueUnitTargetOrder(subject, smart ? "smart" : "move", target, false, 0));
        T_ASSERT(G_IssueUnitPointOrder(subject, "move", &(vec2_t){1024, 0}, true, 0, 0));
        G_DeferFreeEdict(subject); G_TestFinishDeferredFrees();
        level.time += 1001;
        edict_t *replacement = review_order_unit(128, 0);
        replacement->spawn_time = level.time;
        T_EQ(replacement, subject);
        T_NULL(replacement->movement.follow_target);
        T_EQ(replacement->movement.follow_target_spawn_time, 0);
        T_EQ(replacement->current_order_id, 0);
        T_EQ(replacement->order_queue.count, 0);
        T_ASSERT(G_IssueUnitPointOrder(replacement, "move", &(vec2_t){1024, 0}, false, 0, 0));
        G_DeferFreeEdict(target);
        T_EQ(replacement->current_order_id, 851986);
        replacement->think = monster_think;
        level.started = level.scriptsConfigured = level.scriptsStarted = true;
        FOR_LOOP(frame, 4) { level.time += FRAMETIME; globals.RunFrame(); }
        T_EQ(replacement->current_order_id, 851986);
        T_ASSERT(replacement->s.origin2.x > 128);
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_order_lifecycle, follow_direct_free_cannot_adopt_reused_target) {
    for (int smart = 0; smart < 2; smart++) {
        reset_entities(); setup_test_world();
        edict_t *subject = review_order_unit(0, 0);
        edict_t *target = review_order_unit(512, 0);
        target->spawn_time = level.time;
        T_ASSERT(G_IssueUnitTargetOrder(subject, smart ? "smart" : "move", target, false, 0));
        G_FreeEdict(target);
        level.time += 1001;
        edict_t *replacement = review_order_unit(128, 0);
        replacement->spawn_time = level.time;
        T_EQ(replacement, target);
        subject->currentmove->think(subject);
        T_NULL(subject->movement.follow_target);
        T_EQ(subject->current_order_id, 0);
        T_NE(subject->goalentity, replacement);
    }
    reset_entities(); setup_test_world();
}

#endif
