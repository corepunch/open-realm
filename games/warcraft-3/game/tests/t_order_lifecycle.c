#ifdef BZ_TESTS
#include "shared/test.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
void reset_entities(void);
bool run_test_jass(cstring_t src);
void order_attack(edict_t *self, edict_t *target);
void T_Damage(edict_t *target, edict_t *attacker, int damage);
void G_RunEntities(void);
void monster_think(edict_t *self);
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
static edict_t *review_order_unit_at(float x, float y, uint32_t owner) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, y);
    ((mapInfo_t *)level.mapinfo)->players[owner].playerType = kPlayerTypeHuman;
    ent->s.player = owner;
    ent->svflags |= SVF_MONSTER;
    ent->movetype = MOVETYPE_STEP;
    ent->stand = unit_stand;
    ent->die = unit_die;
    ent->unitinfo.MoveSpeed = 300;
    ent->unit_state->attack1.type = ATK_NORMAL;
    ent->unit_state->attack1.range = 30;
    ent->unit_state->attack1.cooldown = 1;
    ent->unit_state->attack1.damageBase = 10;
    ent->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND | WC3_TARGET_FLAG_STRUCTURE;
    ent->targtype = TARG_GROUND;
    ent->runtime.acquisition_range = 600;
    unit_stand(ent);
    gi.LinkEntity(ent);
    return ent;
}

static edict_t *review_order_unit(float x, uint32_t owner) { return review_order_unit_at(x, 0, owner); }

/* Travel owns scratch only while the order can advance; persistent guard/targets live separately. */
TEST(wc3_order_lifecycle, optional_movement_stop_pause_arrival_and_reuse) {
    reset_entities();
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    unit->think = monster_think;
    T_NULL(unit->movement);
    T_NOT_NULL(unit->orders);
    T_ASSERT(!G_UnitIsMoving(unit));
    vec2_t const point = { 300, 0 };
    T_ASSERT(unit_issueorder(unit, "move", &point));
    T_NOT_NULL(unit->movement);
    T_ASSERT(G_UnitIsMoving(unit));
    movement_t *first = unit->movement;
    vec2_t const origin = unit->s.origin2;
    G_SetUnitFlag(unit, UNIT_FLAG_PAUSED, true);
    level.time += FRAMETIME;
    G_RunEntities();
    T_ASSERT(unit->movement == first);
    T_FEQ(unit->s.origin2.x, origin.x, 0.001f);
    G_SetUnitFlag(unit, UNIT_FLAG_PAUSED, false);
    T_ASSERT(unit_issueimmediateorder(unit, "stop"));
    T_NULL(unit->movement);
    T_EQ(unit->orders->guard_state, GUARD_IDLE);
    T_ASSERT(unit_issueorder(unit, "move", &point));
    T_NOT_NULL(unit->movement);
    T_EQ(unit->movement->flow_generation, 0);
    /* Arrive through the real scheduler with a point inside the arrival tolerance. */
    unit->s.origin2 = point;
    unit->s.origin.x = point.x;
    level.time += FRAMETIME;
    G_RunEntities();
    T_NULL(unit->movement);
    T_ASSERT(unit->orders != NULL);
    T_ASSERT(unit_issueorder(unit, "move", &origin));
    T_NOT_NULL(unit->movement);
    unit_die(unit, NULL);
    T_NULL(unit->movement);
    G_FreeEdict(unit);
    T_NULL(unit->orders);
    T_NULL(unit->route);
    T_NULL(unit->wander);
}

TEST(wc3_order_lifecycle, stationary_follow_retains_intent_without_travel_scratch) {
    reset_entities();
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(40, 0);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    T_NOT_NULL(unit->movement);
    unit->currentmove->think(unit);
    T_NULL(unit->movement);
    T_ASSERT(unit->orders->follow_target == ally);
    ally->s.origin2.x = 600;
    unit->currentmove->think(unit);
    T_NOT_NULL(unit->movement);
    T_ASSERT(unit->orders->follow_target == ally);
    order_stop(unit);
    T_NULL(unit->movement);
}

TEST(wc3_order_lifecycle, hold_position_does_not_chase_acquired_enemy) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    T_ASSERT(S_HoldPosition(unit));
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_ASSERT(unit->orders->holding_position);
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
    T_ASSERT(unit->orders->holding_position);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, delayed_kill_preserves_new_move_order) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    vec2_t point = {600, 0};
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    edict_t *missile = G_Spawn();
    missile->owner = unit;
    missile->goalentity = enemy;
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
    T_ASSERT(unit->orders->follow_target == ally);
    G_SetHealth(enemy, 0);
    T_ASSERT(!unit_issuetargetorder(unit, "attack", enemy));
    T_ASSERT(unit->orders->follow_target == ally);
    T_ASSERT(unit->goalentity == ally);
    G_SetHealth(enemy, enemy->health.max_value);
    T_ASSERT(unit_issuetargetorder(unit, "attack", enemy));
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_NULL(unit->orders->follow_target);
}

TEST(wc3_order_lifecycle, queued_attack_preserves_follow_until_follow_completes) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(300, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    T_ASSERT(G_IssueUnitTargetOrder(unit, "attack", enemy, true, 0));
    T_ASSERT(unit->orders->follow_target == ally);
    T_ASSERT(unit->goalentity == ally);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    G_SetHealth(ally, 0);
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_NULL(unit->orders->follow_target);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_Damage(enemy, unit, (int)enemy->health.value);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, auto_attack_resumes_patrol_but_smart_attack_replaces_it) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *first = review_order_unit(300, 1);
    edict_t *second = review_order_unit(400, 1);
    order_patrol(unit, Waypoint_add(&MAKE(vec2_t, .x = 600)));
    edict_t *patrol = unit->orders->patrol_target;
    order_attack(unit, first);
    T_Damage(first, unit, (int)first->health.value);
    T_ASSERT(unit->goalentity == patrol);
    T_ASSERT(unit->currentmove->proc == CAbilityPatrol);
    T_ASSERT(unit_issuetargetorder(unit, "smart", second));
    T_ASSERT(unit->goalentity == second);
    T_ASSERT(unit->currentmove->proc == CAbilityAttack);
    T_Damage(second, unit, (int)second->health.value);
    T_NULL(unit->orders->patrol_a);
    T_NULL(unit->orders->patrol_b);
    T_NULL(unit->orders->patrol_target);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_order_lifecycle, animationless_melee_kill_preserves_resumed_follow) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *ally = review_order_unit(500, 0);
    edict_t *enemy = review_order_unit(20, 1);
    T_ASSERT(unit_issuetargetorder(unit, "move", ally));
    unit->unit_state->attack1.damagePoint = (float)FRAMETIME / 1000.0f;
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
    T_ASSERT(!unit->orders->holding_position);
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
    T_ASSERT(!unit->orders->holding_position);
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
    T_ASSERT(unit->orders->holding_position);

    unit->currentmove->think(unit);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_ASSERT(!unit->orders->holding_position);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, hold_position_interrupts_active_channel) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0);
    G_AddChannel(unit);
    G_GetChannel(unit)->code = MAKEFOURCC('A','x','x','x');

    T_ASSERT(S_HoldPosition(unit));
    T_ASSERT(!G_GetChannel(unit) || G_GetChannel(unit)->code == 0);
    T_ASSERT(unit->orders->holding_position);
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
        unit->goalentity = NULL;
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

    T_ASSERT(unit->orders->holding_position);
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
    T_ASSERT(unit->orders->guard_state == GUARD_IDLE);
    T_FEQ(unit->orders->guard_position.x, 0, 0.001f);
    T_FEQ(unit->orders->guard_position.y, 0, 0.001f);

    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->goalentity == enemy);
    T_ASSERT(unit->orders->guard_state == GUARD_COMBAT);

    unit->s.origin2.x = unit->s.origin.x = 180;
    gi.LinkEntity(unit);
    T_Damage(enemy, unit, (int)enemy->health.value);

    T_ASSERT(unit->orders->guard_state == GUARD_RETURNING);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
    T_NOT_NULL(unit->goalentity);
    T_FEQ(unit->goalentity->s.origin2.x, 0, 0.001f);
    T_FEQ(unit->goalentity->s.origin2.y, 0, 0.001f);
}

TEST(wc3_order_lifecycle, internal_stop_cleanup_does_not_create_guard_position) {
    setup_test_world();
    edict_t *unit = review_order_unit(96, 0);

    order_stop_cleanup(unit);

    T_ASSERT(unit->orders->guard_state == GUARD_NONE);
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
    T_ASSERT(unit->orders->guard_state == GUARD_RETURNING);

    unit->s.origin2 = unit->orders->guard_position;
    unit->s.origin.x = unit->s.origin2.x;
    unit->s.origin.y = unit->s.origin2.y;
    gi.LinkEntity(unit);
    unit->currentmove->think(unit);

    T_ASSERT(unit->orders->guard_state == GUARD_IDLE);
    T_ASSERT(unit->currentmove->think == ai_stand);
}

TEST(wc3_order_lifecycle, explicit_move_clears_old_stop_guard) {
    setup_test_world();
    edict_t *unit = review_order_unit(40, 0);
    vec2_t point = {500, 0};

    order_stop(unit);
    T_ASSERT(unit->orders->guard_state == GUARD_IDLE);
    T_FEQ(unit->orders->guard_position.x, 40, 0.001f);

    T_ASSERT(unit_issueorder(unit, "move", &point));
    T_ASSERT(unit->orders->guard_state == GUARD_NONE);
}

TEST(wc3_order_lifecycle, second_stop_refreshes_guard_position) {
    setup_test_world();
    edict_t *unit = review_order_unit(20, 0);

    order_stop(unit);
    T_FEQ(unit->orders->guard_position.x, 20, 0.001f);

    unit->s.origin2.x = unit->s.origin.x = 240;
    gi.LinkEntity(unit);
    order_stop(unit);

    T_ASSERT(unit->orders->guard_state == GUARD_IDLE);
    T_FEQ(unit->orders->guard_position.x, 240, 0.001f);
}

TEST(wc3_order_lifecycle, queued_player_order_outranks_guard_return) {
    setup_test_world();
    edict_t *unit = review_order_unit(0, 0), *enemy = review_order_unit(300, 1);
    vec2_t point = {700, 0};

    order_stop(unit);
    level.time = 300 - (uint32_t)(unit - g_edicts) % 300;
    unit->currentmove->think(unit);
    T_ASSERT(unit->orders->guard_state == GUARD_COMBAT);
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &point, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(unit), 1);

    unit->s.origin2.x = unit->s.origin.x = 160;
    gi.LinkEntity(unit);
    T_Damage(enemy, unit, (int)enemy->health.value);

    T_ASSERT(unit->orders->guard_state == GUARD_NONE);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_ASSERT(unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_order_lifecycle, neutral_creep_guard_keeps_spawn_anchor_independent_of_stop) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    T_ASSERT(creep->orders->creep_guard_enabled);
    T_FEQ(creep->orders->creep_guard_origin.x, 128, 0.01f);
    creep->s.origin2.x = 256;
    G_SetUnitGuardPosition(creep);
    T_FEQ(creep->orders->creep_guard_origin.x, 128, 0.01f);
    T_FEQ(creep->orders->guard_position.x, 256, 0.01f);
}

TEST(wc3_order_lifecycle, explicit_order_drops_only_creep_auto_combat) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(creep->orders->creep_guard_phase == CREEP_GUARD_COMBAT);
    G_CreepGuardExplicitOrder(creep);
    T_ASSERT(creep->orders->creep_guard_phase != CREEP_GUARD_COMBAT);
    T_ASSERT(creep->orders->creep_guard_enabled);
    T_FEQ(creep->orders->creep_guard_origin.x, 128, 0.01f);
}

TEST(wc3_order_lifecycle, internal_order_notification_does_not_cancel_creep_guard) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_CreepGuardAutoCombat(creep);
    S_UnitAbilityOrderAccepted(creep, "attack");
    T_ASSERT(creep->orders->creep_guard_phase == CREEP_GUARD_COMBAT);
}

TEST(wc3_order_lifecycle, creep_guard_native_disable_keeps_anchor_and_explicit_order) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_CreepGuardAutoCombat(creep);
    G_CreepGuardSetEnabled(creep, false);
    T_ASSERT(!creep->orders->creep_guard_enabled);
    T_ASSERT(creep->orders->creep_guard_phase != CREEP_GUARD_COMBAT);
    T_ASSERT(creep->orders->creep_guard_phase != CREEP_GUARD_RETURNING);
    T_FEQ(creep->orders->creep_guard_origin.x, 128, 0.01f);
    creep->s.origin2.x = 350;
    G_CreepGuardSetEnabled(creep, true);
    T_ASSERT(creep->orders->creep_guard_enabled);
    T_ASSERT(creep->orders->creep_guard_phase != CREEP_GUARD_COMBAT);
    T_FEQ(creep->orders->creep_guard_origin.x, 128, 0.01f);
}

/* Disabling an active autonomous return must stop the Move, but it must not
 * assign a new player Stop guard position or move the original creep anchor. */
TEST(wc3_order_lifecycle, disable_creep_guard_cancels_active_return_move) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_SetUnitGuardPosition(creep);
    creep->s.origin2.x = creep->s.origin.x = 900;
    gi.LinkEntity(creep);
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(G_CreepGuardCombatEnd(creep));
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);

    G_CreepGuardSetEnabled(creep, false);
    T_ASSERT(!creep->orders->creep_guard_enabled);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep->currentmove && creep->currentmove->proc != CAbilityMove);
    T_FEQ(creep->orders->creep_guard_origin.x, 128, 0.001f);
    T_FEQ(creep->orders->guard_position.x, 128, 0.001f);
    level.time += 5000;
    G_CreepGuardTick(creep);
    T_ASSERT(creep->currentmove && creep->currentmove->proc != CAbilityMove);
}

/* Disabling future automatic guarding must not interrupt a script-owned Move. */
TEST(wc3_order_lifecycle, disable_creep_guard_preserves_explicit_move) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    vec2_t const destination = { 400, 0 };
    G_CreepGuardInit(creep);
    T_ASSERT(G_IssueUnitPointOrder(creep, "move", &destination, false, creep->s.player, 0.0f));
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    edict_t *goal = creep->goalentity;
    G_CreepGuardSetEnabled(creep, false);
    T_ASSERT(!creep->orders->creep_guard_enabled);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    T_ASSERT(creep->goalentity == goal);
}

TEST(wc3_order_lifecycle, creep_guard_damage_refreshes_only_eligible_unit) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    level.time = 2000;
    G_CreepGuardDamaged(creep);
    T_EQ(creep->orders->creep_guard_last_hit_ms, 2000u);
    G_CreepGuardSetEnabled(creep, false);
    level.time = 3000;
    G_CreepGuardDamaged(creep);
    T_EQ(creep->orders->creep_guard_last_hit_ms, 2000u);
}

TEST(wc3_order_lifecycle, creep_guard_return_retries_stop_after_bounded_failures) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    creep->s.origin2.x = 900;
    creep->orders->creep_guard_phase = CREEP_GUARD_RETURNING;
    /* Simulate interrupted return at stand; do not enter a busy retry loop. */
    level.time = 1000;
    G_CreepGuardTick(creep);
    T_ASSERT(creep->orders->creep_guard_phase == CREEP_GUARD_RETURNING);
    T_EQ(creep->orders->creep_guard_return_retries, 0u);
    T_EQ(creep->orders->creep_guard_retry_at_ms, 2000u);
    G_CreepGuardExplicitOrder(creep);
    T_ASSERT(creep->orders->creep_guard_phase != CREEP_GUARD_RETURNING);
    T_EQ(creep->orders->creep_guard_return_retries, 0u);
    T_EQ(creep->orders->creep_guard_retry_at_ms, 0u);
}

TEST(wc3_order_lifecycle, creep_guard_arrival_clears_retry_state) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    creep->orders->creep_guard_phase = CREEP_GUARD_RETURNING;
    creep->orders->creep_guard_return_retries = 2;
    creep->orders->creep_guard_retry_at_ms = 1000;
    G_CreepGuardTick(creep);
    T_ASSERT(creep->orders->creep_guard_phase != CREEP_GUARD_RETURNING);
    T_EQ(creep->orders->creep_guard_return_retries, 0u);
    T_EQ(creep->orders->creep_guard_retry_at_ms, 0u);
}

/* Exercise the production post-mitigation damage path: a lethal blow must
 * alert a surviving idle camp member before the victim is torn down. */
TEST(wc3_order_lifecycle, lethal_creep_hit_alerts_surviving_camp_member) {
    setup_test_world();
    edict_t *victim = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    edict_t *ally = review_order_unit(192, PLAYER_NEUTRAL_AGGRESSIVE);
    edict_t *attacker = review_order_unit(240, 0);
    G_CreepGuardInit(victim);
    G_CreepGuardInit(ally);
    T_ASSERT(ally->orders->creep_guard_phase != CREEP_GUARD_COMBAT);
    T_Damage(victim, attacker, (int)victim->health.value + 100);
    T_ASSERT(M_IsDead(victim));
    T_ASSERT(ally->orders->creep_guard_phase == CREEP_GUARD_COMBAT);
    T_ASSERT(ally->goalentity == attacker);
    T_ASSERT(ally->currentmove && ally->currentmove->proc == CAbilityAttack);
}

/* Drive the actual tick through each deadline with an immobilized creep so
 * Move never becomes active. This catches unbounded, every-frame retries. */
TEST(wc3_order_lifecycle, creep_guard_retry_exhaustion_uses_simulation_time) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    creep->s.origin2.x = 900;
    creep->aiflags |= AI_IMMOBILE;
    creep->orders->creep_guard_phase = CREEP_GUARD_RETURNING;
    level.time = 1000;
    G_CreepGuardTick(creep);
    T_EQ(creep->orders->creep_guard_retry_at_ms, 2000u);
    for (uint32_t attempt = 1; attempt <= 3; attempt++) {
        level.time = 2000 + (attempt - 1) * 1000;
        G_CreepGuardTick(creep);
        T_EQ(creep->orders->creep_guard_return_retries, 0u);
        T_ASSERT(creep->orders->creep_guard_phase == CREEP_GUARD_RETURNING);
    }
    /* Once movement is permitted, actual retries are counted. Test the
     * scheduler callback instead of calling the helper for this transition. */
    creep->aiflags &= ~AI_IMMOBILE;
    for (uint32_t attempt = 1; attempt <= 3; attempt++) {
        level.time = 5000 + (attempt - 1) * 1000;
        creep->orders->creep_guard_retry_at_ms = level.time;
        unit_stand(creep); /* emulate a movement failure after each attempt */
        creep->think = monster_think;
        G_RunEntities();
        T_EQ(creep->orders->creep_guard_return_retries, attempt);
        T_ASSERT(creep->orders->creep_guard_phase == CREEP_GUARD_RETURNING);
    }
    level.time = 8000;
    unit_stand(creep);
    creep->think = monster_think;
    G_RunEntities();
    T_ASSERT(creep->orders->creep_guard_phase != CREEP_GUARD_RETURNING);
    T_EQ(creep->orders->creep_guard_return_retries, 3u);
    T_FEQ(creep->orders->creep_guard_origin.x, 128, 0.001f);
}

/* Explicit immediate orders must cancel return ownership without changing its
 * independently stored original anchor. */
TEST(wc3_order_lifecycle, creep_guard_stop_and_hold_cancel_return) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    creep->s.origin2.x = 900;
    creep->orders->creep_guard_phase = CREEP_GUARD_RETURNING;
    creep->orders->creep_guard_retry_at_ms = 2000;
    T_ASSERT(unit_issueimmediateorder(creep, "stop"));
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_EQ(creep->orders->creep_guard_retry_at_ms, 0u);
    T_FEQ(creep->orders->creep_guard_origin.x, 128, 0.01f);
    creep->orders->creep_guard_phase = CREEP_GUARD_RETURNING;
    T_ASSERT(unit_issueimmediateorder(creep, "holdposition"));
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
}

TEST(wc3_order_lifecycle, creep_guard_explicit_point_move_cancels_return) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    vec2_t destination = { 220, 0 };
    G_CreepGuardInit(creep);
    creep->orders->creep_guard_phase = CREEP_GUARD_RETURNING;
    T_ASSERT(G_IssueUnitPointOrder(creep, "move", &destination, false, creep->s.player, 0.0f));
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
}

/* Ownership transfer aborts an AI return without recording a new Stop
 * guard anchor at the creep's displaced position. */
TEST(wc3_order_lifecycle, creep_guard_owner_transfer_preserves_stop_anchor) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    G_SetUnitGuardPosition(creep);
    creep->s.origin2.x = creep->s.origin.x = 900;
    gi.LinkEntity(creep);
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(G_CreepGuardCombatEnd(creep));
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);

    G_SetUnitPlayer(creep, 0);
    T_ASSERT(!creep->orders->creep_guard_enabled);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep->currentmove && creep->currentmove->proc != CAbilityMove);
    T_FEQ(creep->orders->guard_position.x, 128, 0.001f);
}

TEST(wc3_order_lifecycle, creep_guard_owner_transfer_reconciles_policy) {
    setup_test_world();
    edict_t *creep = review_order_unit(128, PLAYER_NEUTRAL_AGGRESSIVE);
    G_CreepGuardInit(creep);
    creep->orders->creep_guard_phase = CREEP_GUARD_RETURNING;
    G_SetUnitPlayer(creep, 0);
    T_ASSERT(!creep->orders->creep_guard_enabled);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    creep->s.origin2.x = 320;
    G_SetUnitPlayer(creep, PLAYER_NEUTRAL_AGGRESSIVE);
    T_ASSERT(creep->orders->creep_guard_enabled);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_FEQ(creep->orders->creep_guard_origin.x, 320, 0.01f);
}

/* Creep guard scheduler fixtures: a map script so globals.RunFrame pumps the
 * real server frame, and creeps the scheduler drives itself. */
static void creep_guard_setup_frame_world(void) {
    reset_entities();
    setup_test_world();
    level.time = 1000;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
}

static void creep_guard_run_frames(uint32_t frames) {
    bool const started = level.started, scripts = level.scriptsStarted;
    level.started = level.scriptsStarted = true;
    FOR_LOOP(i, frames) { level.time += FRAMETIME; globals.RunFrame(); }
    level.started = started; level.scriptsStarted = scripts;
}

/* Anchor the creep at (anchor_x, 0), then teleport it to (x, y) so the leash sees distance. */
static edict_t *scheduled_creep(float anchor_x, float x, float y) {
    edict_t *creep = review_order_unit(anchor_x, PLAYER_NEUTRAL_AGGRESSIVE);
    creep->think = monster_think;
    creep->collision = 16.0f;
    creep->s.model = 1; /* IS_HOLLOW() ignores model-less edicts for collision */
    G_CreepGuardInit(creep);
    creep->s.origin2 = (vec2_t){ x, y };
    creep->s.origin.x = x;
    creep->s.origin.y = y;
    gi.LinkEntity(creep);
    return creep;
}

static bool creep_guard_attacking(edict_t const *creep) {
    return creep->currentmove && creep->currentmove->proc == CAbilityAttack;
}

static float creep_guard_home_distance(edict_t const *creep) {
    return Vector2_distance(&creep->s.origin2, &creep->orders->creep_guard_origin);
}

/* Two creeps walk home to the same anchor. B snaps onto it first; A arrives
 * within the Move's own tolerance but cannot snap onto the occupied cell. An
 * enemy stands in acquisition range of that spot: a returning creep must not
 * acquire it, and the near-anchor stop must count as home. */
TEST(wc3_order_lifecycle, returning_creep_ignores_enemy_and_settles_beside_occupied_anchor) {
    creep_guard_setup_frame_world();
    edict_t *b = scheduled_creep(128, 400, 0);
    edict_t *a = scheduled_creep(128, 900, 0);
    edict_t *enemy = review_order_unit_at(160, 400, 0);
    int frame;
    b->unit_state->attack1.cooldown = 0; /* B has no weapon, so it stays parked on the anchor */
    G_CreepGuardAutoCombat(b);
    T_ASSERT(G_CreepGuardCombatEnd(b));
    G_CreepGuardAutoCombat(a);
    T_ASSERT(G_CreepGuardCombatEnd(a));
    T_EQ(a->orders->creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(a->currentmove && a->currentmove->proc == CAbilityMove);
    for (frame = 0; frame < 400 && a->orders->creep_guard_phase == CREEP_GUARD_RETURNING; frame++) {
        creep_guard_run_frames(1);
        if (a->orders->creep_guard_phase == CREEP_GUARD_RETURNING) T_ASSERT(!creep_guard_attacking(a));
    }
    T_EQ(a->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_EQ(b->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_FEQ(b->s.origin2.x, 128, 0.001f);
    T_ASSERT(creep_guard_home_distance(a) > 4.0f);
    T_ASSERT(creep_guard_home_distance(a) <= move_settle_distance(a));
    T_ASSERT(!creep_guard_attacking(a));
    T_EQ(a->orders->creep_guard_return_retries, 0u);
    T_ASSERT(enemy->inuse);
}

/* An Ensnared returning creep stands in acquisition range of an enemy. Retail
 * returning creeps ignore enemies; the root only delays the return, and once it
 * expires the creep walks home and goes idle with no attack order. */
TEST(wc3_order_lifecycle, rooted_returning_creep_ignores_enemy_then_walks_home) {
    creep_guard_setup_frame_world();
    edict_t *creep = scheduled_creep(-800, 100, 0);
    edict_t *enemy = review_order_unit_at(100, 300, 0);
    int frame;
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(G_CreepGuardCombatEnd(creep));
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(unit_addtimedstatus(creep, "BEer", 1, 2.0f));
    for (frame = 0; frame < 10; frame++) {
        creep_guard_run_frames(1);
        T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_RETURNING);
        T_ASSERT(!creep_guard_attacking(creep));
    }
    T_ASSERT(creep->currentmove && creep->currentmove->think == ai_stand);
    T_FEQ(creep->s.origin2.x, 100, 0.001f);
    for (frame = 0; frame < 400 && creep->orders->creep_guard_phase == CREEP_GUARD_RETURNING; frame++) {
        creep_guard_run_frames(1);
        if (creep->orders->creep_guard_phase == CREEP_GUARD_RETURNING) T_ASSERT(!creep_guard_attacking(creep));
    }
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep_guard_home_distance(creep) <= move_settle_distance(creep));
    T_ASSERT(!creep_guard_attacking(creep));
    T_ASSERT(enemy->inuse);
}

/* A live Attack reaching a returning creep (Taunt, bot AI) is leashed combat,
 * never a failed return Move whose exhausted budget strands an unleashed fight. */
TEST(wc3_order_lifecycle, creep_guard_live_attack_while_returning_is_leashed) {
    setup_test_world();
    edict_t *creep = review_order_unit(-800, PLAYER_NEUTRAL_AGGRESSIVE);
    edict_t *enemy = review_order_unit(330, 0);
    G_CreepGuardInit(creep);
    creep->s.origin2.x = creep->s.origin.x = 300;
    gi.LinkEntity(creep);
    creep->orders->creep_guard_phase = CREEP_GUARD_RETURNING;
    creep->orders->creep_guard_return_retries = 3;
    order_attack(creep, enemy);
    T_ASSERT(creep_guard_attacking(creep));
    level.time = 1000;
    G_CreepGuardTick(creep);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    T_EQ(creep->orders->creep_guard_return_retries, 0u);
}

/* Hard leash through the real scheduler: idle acquisition, then the tick turns
 * the live Attack beyond MaxGuardDistance into a return Move that walks home. */
TEST(wc3_order_lifecycle, creep_guard_hard_leash_returns_home_through_scheduler) {
    creep_guard_setup_frame_world();
    edict_t *creep = scheduled_creep(-800, 300, 0);
    edict_t *enemy = review_order_unit(330, 0);
    int frame;
    for (frame = 0; frame < 4 && creep->orders->creep_guard_phase != CREEP_GUARD_COMBAT; frame++)
        creep_guard_run_frames(1);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_COMBAT);
    T_ASSERT(creep_guard_attacking(creep) && creep->goalentity == enemy);
    creep_guard_run_frames(1);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    for (frame = 0; frame < 400 && creep->orders->creep_guard_phase == CREEP_GUARD_RETURNING; frame++)
        creep_guard_run_frames(1);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep_guard_home_distance(creep) <= move_settle_distance(creep));
    T_ASSERT(!creep_guard_attacking(creep));
}

/* Soft leash through the real scheduler: between GuardDistance and
 * MaxGuardDistance the creep keeps fighting until GuardReturnTime passes
 * without a hit, then walks home. */
TEST(wc3_order_lifecycle, creep_guard_soft_leash_times_out_through_scheduler) {
    creep_guard_setup_frame_world();
    edict_t *creep = scheduled_creep(-800, -100, 0);
    edict_t *enemy = review_order_unit(-70, 0);
    uint32_t combat_ms;
    int frame;
    for (frame = 0; frame < 4 && creep->orders->creep_guard_phase != CREEP_GUARD_COMBAT; frame++)
        creep_guard_run_frames(1);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_COMBAT);
    T_ASSERT(creep_guard_attacking(creep) && creep->goalentity == enemy);
    combat_ms = level.time;
    for (frame = 0; frame < 80 && creep->orders->creep_guard_phase == CREEP_GUARD_COMBAT; frame++)
        creep_guard_run_frames(1);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_RETURNING);
    T_ASSERT(level.time - combat_ms >= 5000u);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    for (frame = 0; frame < 400 && creep->orders->creep_guard_phase == CREEP_GUARD_RETURNING; frame++)
        creep_guard_run_frames(1);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_ASSERT(creep_guard_home_distance(creep) <= move_settle_distance(creep));
}

/* A Shift-queued order only takes ownership once it starts. Until then the
 * auto-acquired Attack stays leashed, and the leash hands control to the
 * queued order instead of walking home. */
TEST(wc3_order_lifecycle, creep_guard_queued_order_defers_reset_and_takes_over_on_leash) {
    setup_test_world();
    edict_t *creep = review_order_unit(-800, PLAYER_NEUTRAL_AGGRESSIVE);
    edict_t *enemy = review_order_unit(330, 0);
    vec2_t const point = { 500, 0 };
    G_CreepGuardInit(creep);
    creep->s.origin2.x = creep->s.origin.x = 300;
    gi.LinkEntity(creep);
    order_attack(creep, enemy);
    G_CreepGuardAutoCombat(creep);
    T_ASSERT(G_IssueUnitPointOrder(creep, "move", &point, true, creep->s.player, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(creep), 1u);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_COMBAT);
    T_ASSERT(creep_guard_attacking(creep));
    level.time = 1000;
    G_CreepGuardTick(creep);
    T_EQ(creep->orders->creep_guard_phase, CREEP_GUARD_IDLE);
    T_EQ(G_UnitQueuedOrderCount(creep), 0u);
    T_ASSERT(creep->currentmove && creep->currentmove->proc == CAbilityMove);
    T_ASSERT(creep->goalentity && creep->goalentity->s.origin2.x == point.x);
}

#endif
