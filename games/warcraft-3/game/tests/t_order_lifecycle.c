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
    cstring_t file = "/tmp/wc3-sparse-order-ring.bin";
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
    T_EQ(unit->order_queue.entries, storage);
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
    T_NOT_NULL(unit->order_queue.entries);
    G_FreeEdict(unit);
    T_NULL(unit->order_queue.entries);
    remove(file);
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

TEST(wc3_order_lifecycle, animationless_melee_kill_preserves_resumed_follow) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(20, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    S_AttackProfileWrite(unit, 0)->damagePoint = (float)FRAMETIME / 1000.0f;
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

#endif
