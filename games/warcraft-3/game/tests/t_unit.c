/*
 * t_unit.c — In-engine unit lifecycle and order tests.
 *
 * Runs inside the real game module via +dedicated 1 +test 'wc3_unit.*'.
 * The real gi, edict pool, and unit data tables are available.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);

/* Forward declarations for functions in m_unit.c without a public header. */
void unit_stand(LPEDICT self);
void unit_birth(LPEDICT self);
void unit_die(LPEDICT self, LPEDICT attacker);
void unit_begin_decay(LPEDICT self);
void unit_decay_think(LPEDICT self);
void unit_entercombat(LPEDICT self, LPEDICT target);
void unit_leavecombat(LPEDICT self);
BOOL unit_affectingcombat(LPEDICT self);
BOOL unit_issuetargetorder(LPEDICT self, LPCSTR order, LPEDICT target);
BOOL unit_issueorder(LPEDICT self, LPCSTR order, LPCVECTOR2 point);
BOOL unit_issueimmediateorder(LPEDICT self, LPCSTR order);
BOOL unit_additem(LPEDICT edict, LPEDICT item);
BOOL unit_additemtoslot(LPEDICT edict, LPEDICT item, DWORD slot);
slkTestData_t *parse_slk_string(LPCSTR slk_text);
void free_slk_rows(slkTestData_t *rows);

/* Reset the entity pool between tests. */
static void reset_test_entities(void) {
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = 0;
    globals.edicts = g_edicts;
}

/* Create a minimal unit edict with lifecycle callbacks wired up. */
static LPEDICT make_unit(FLOAT x, FLOAT y) {
    LPEDICT ent = G_Spawn();
    ent->class_id       = MAKEFOURCC('h','p','e','a');
    G_BindEntityData(ent);
    ent->s.origin2      = (VECTOR2){x, y};
    ent->s.origin.x     = x;
    ent->s.origin.y     = y;
    ent->s.origin.z     = 0;
    ent->s.model        = 1; /* non-zero so IS_HOLLOW is false */
    ent->movetype       = MOVETYPE_STEP;
    ent->collision      = 16.0f;
    ent->stand          = unit_stand;
    ent->birth          = unit_birth;
    ent->die            = unit_die;
    ent->health.value   = G_UnitBalance(ent->class_id)->maxHealth;
    ent->health.max_value = G_UnitBalance(ent->class_id)->maxHealth;
    ent->unitinfo.MoveSpeed = G_UnitBalance(ent->class_id)->speed;
    unit_stand(ent);
    return ent;
}

static LPEDICT make_inventory_unit(FLOAT x, FLOAT y) {
    LPEDICT ent = make_unit(x, y);
    ent->class_id = MAKEFOURCC('H','p','a','l');
    G_BindEntityData(ent);
    return ent;
}

static LPEDICT make_world_item(DWORD class_id) {
    LPEDICT item = G_Spawn();
    item->class_id = class_id;
    G_BindEntityData(item);
    item->s.model = 1;
    item->item.in_world = true;
    item->item.inventory_slot = -1;
    return item;
}

static LPEDICT unit_make_harvest_tree(FLOAT x, FLOAT y) {
    LPEDICT tree = G_Spawn();
    tree->s.origin2 = (VECTOR2){x, y};
    tree->s.origin.x = x;
    tree->s.origin.y = y;
    tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = 100.0f;
    return tree;
}

static LPEDICT unit_make_harvest_goldmine(FLOAT x, FLOAT y) {
    static UnitAbilities_t const abilities = { .abilList = "Agld" };
    LPEDICT mine = G_Spawn();
    mine->s.origin2 = (VECTOR2){x, y};
    mine->s.origin.x = x;
    mine->s.origin.y = y;
    mine->data.UnitAbilities = &abilities;
    mine->resources = 12500;
    mine->health.value = mine->health.max_value = 1000.0f;
    return mine;
}

TEST(wc3_unit, shared_test_unit_starts_alive) {
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);

    T_ASSERT(ent->data.UnitBalance->maxHealth > 0.0f);
    T_FEQ(ent->health.max_value, ent->data.UnitBalance->maxHealth, 0.001f);
    T_FEQ(ent->health.value, ent->health.max_value, 0.001f);
    T_ASSERT(!M_IsDead(ent));
}

TEST(wc3_unit, selection_sound_registration_caches_all_responses) {
    static LPCSTR const slk =
        "ID;PWXL;N;E\n"
        "B;X3;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y2;X1;K\"FootmanWhat\"\n"
        "C;Y2;X2;K\"FootmanWhat1.wav,FootmanWhat2.wav,FootmanWhat3.wav,FootmanWhat4.wav\"\n"
        "C;Y2;X3;K\"Units\\Human\\Footman\\\"\n"
        "E\n";
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    slkTestData_t *sounds = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("UnitAckSounds", sounds);
    G_RegisterSelectSounds(ent, "Footman");
    T_EQ(ent->sound.num_select, 4);
    FOR_LOOP(i, ent->sound.num_select) T_ASSERT(ent->sound.select[i]);
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(sounds);
}

TEST(wc3_unit, selecting_owned_unit_queues_one_ack_sound) {
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    ent->sound.select[0] = 11;
    ent->sound.select[1] = 12;
    ent->sound.num_select = 2;
    G_QueueSelectionSound(ent);
    T_ASSERT(ent->sound.pending == 11 || ent->sound.pending == 12);
    T_EQ(ent->sound.pending != 0, 1);
}

TEST(wc3_unit, selection_without_responses_does_not_queue_ack) {
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_QueueSelectionSound(ent);
    T_EQ(ent->sound.pending, 0);
}

TEST(wc3_unit, ready_sound_queues_owner_only_sound) {
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    ent->sound.ready[0] = 21;
    ent->sound.ready[1] = 22;
    ent->sound.num_ready = 2;

    G_QueueReadySound(ent);
    T_ASSERT(ent->sound.owner_pending == 21 || ent->sound.owner_pending == 22);
    T_EQ(ent->sound.owner_pending != 0, 1);
}

/* -----------------------------------------------------------------------
 * Birth tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, birth_sets_birth_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_birth(ent);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "birth");
}

TEST(wc3_unit, birth_sets_wait_from_build_time) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_birth(ent);
    T_ASSERT(ent->wait > 0);
    T_EQ((int)ent->wait, G_UnitBalance(ent->class_id)->buildTime);
}

TEST(wc3_unit, birth_sets_no_ubersplat_flag) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_birth(ent);
    T_ASSERT(ent->s.renderfx & RF_NO_UBERSPLAT);
}

/* -----------------------------------------------------------------------
 * Stand tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, stand_sets_stand_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_stand(ent);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, stand_uses_ready_animation_in_combat) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    LPEDICT target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (VECTOR2){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;

    unit_entercombat(ent, target);
    unit_stand(ent);

    T_ASSERT(unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "stand ready");
}

TEST(wc3_unit, stop_exits_ready_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    LPEDICT target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (VECTOR2){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;

    unit_entercombat(ent, target);
    unit_stand(ent);
    T_STREQ(ent->currentmove->animation, "stand ready");

    unit_issueimmediateorder(ent, "stop");

    T_ASSERT(!unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, stand_clears_build_pointer) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    ent->build = ent;
    unit_stand(ent);
    T_NULL(ent->build);
}

TEST(wc3_unit, stand_clears_no_ubersplat_flag) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    ent->s.renderfx |= RF_NO_UBERSPLAT;
    unit_stand(ent);
    T_ASSERT(!(ent->s.renderfx & RF_NO_UBERSPLAT));
}

TEST(wc3_unit, spawned_building_is_immobile) {
    T_ASSERT(unit_spawn_aiflags(MAKEFOURCC('h','b','a','r')) & AI_IMMOBILE);
}

TEST(wc3_unit, spawned_building_carries_renderer_building_flag) {
    T_ASSERT(G_UnitIsBuilding(MAKEFOURCC('h','b','a','r')));
}

TEST(wc3_unit, spawned_mobile_unit_is_not_immobile) {
    T_ASSERT(!(unit_spawn_aiflags(MAKEFOURCC('h','p','e','a')) & AI_IMMOBILE));
}

/* -----------------------------------------------------------------------
 * Die tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, die_sets_death_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_die(ent, NULL);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, die_emits_registered_death_sound) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    ent->sound.death = 23;

    unit_die(ent, NULL);
    T_EQ(ent->sound.world_pending, 23);
    T_EQ(ent->sound.world_pending_event, EV_DEATH);
    T_EQ(ent->sound.world_pending, 23);
}

TEST(wc3_unit, die_raises_dead_monster_flag) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    T_ASSERT(!M_IsDead(ent));
    unit_die(ent, NULL);
    T_FEQ(ent->health.value, 0.0f, 0.001f);
    T_ASSERT(M_IsDead(ent));
    T_ASSERT(ent->svflags & SVF_DEADMONSTER);
}

TEST(wc3_unit, die_clears_selection_and_marks_corpse_unselectable) {
    reset_test_entities();
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT ent = make_unit(0, 0);
    client->ps.number = 0;
    ent->s.player = 0;

    G_SelectEntity(client, ent);
    T_ASSERT(G_IsEntitySelected(client, ent));

    unit_die(ent, NULL);

    T_EQ(ent->selected, 0);
    T_ASSERT(ent->s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(!G_IsEntitySelected(client, ent));

    G_SelectEntity(client, ent);
    T_ASSERT(!G_IsEntitySelected(client, ent));
}

TEST(wc3_unit, die_releases_held_frame_before_death_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    ent->aiflags |= AI_HOLD_FRAME;

    unit_die(ent, NULL);

    T_ASSERT(!(ent->aiflags & AI_HOLD_FRAME));
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, dead_unit_rejects_orders_that_would_replace_death_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    LPEDICT target = make_unit(128, 0);
    VECTOR2 point = { 64.0f, 0.0f };

    ent->health.value = 0.0f;
    unit_die(ent, target);

    T_ASSERT(!unit_issueorder(ent, "move", &point));
    T_ASSERT(!unit_issueimmediateorder(ent, "stop"));
    T_ASSERT(!unit_issuetargetorder(ent, "smart", target));
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, smart_on_passive_ally_starts_persistent_follow) {
    reset_test_entities();
    LPEDICT follower = make_unit(0, 0);
    LPEDICT leader = make_unit(256, 0);
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = 0;
    leader->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

    T_ASSERT(unit_issuetargetorder(follower, "smart", leader));
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
    T_ASSERT(follower->currentmove && follower->currentmove->ability == &a_move);
}

TEST(wc3_unit, target_move_on_unit_starts_persistent_follow) {
    reset_test_entities();
    LPEDICT follower = make_unit(0, 0);
    LPEDICT leader = make_unit(256, 0);
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = leader->s.player = 0;

    T_ASSERT(unit_issuetargetorder(follower, "move", leader));
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
    T_ASSERT(follower->currentmove && follower->currentmove->ability == &a_move);
}

TEST(wc3_unit, queued_smart_on_passive_ally_revalidates_to_follow) {
    reset_test_entities();
    setup_test_world();
    LPEDICT follower = make_unit(0, 0);
    LPEDICT leader = make_unit(256, 0);
    VECTOR2 first = { 96.0f, 0.0f };
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = 0;
    leader->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

    T_ASSERT(G_IssueUnitPointOrder(follower, "move", &first, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitTargetOrder(follower, "smart", leader, true, 0));
    T_EQ(G_UnitQueuedOrderCount(follower), 1);

    unit_stand(follower);

    T_EQ(G_UnitQueuedOrderCount(follower), 0);
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
}

TEST(wc3_unit, smart_on_neutral_aggressive_attacks_not_follows) {
    reset_test_entities();
    LPEDICT unit = make_unit(0, 0);
    LPEDICT target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    memset(level.alliances, 0, sizeof(level.alliances));

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->movement.follow_target == NULL);
    T_ASSERT(unit->currentmove && unit->currentmove->ability == &a_attack);
}

TEST(wc3_unit, smart_on_shared_vision_enemy_still_attacks) {
    reset_test_entities();
    LPEDICT unit = make_unit(0, 0);
    LPEDICT target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.alliances[0][1] |= 1 << ALLIANCE_SHARED_VISION;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->movement.follow_target == NULL);
    T_ASSERT(unit->currentmove && unit->currentmove->ability == &a_attack);
}

TEST(wc3_unit, die_publishes_death_event) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    memset(level.events.queue, 0, sizeof(level.events.queue));
    memset(level.events.handlers, 0, sizeof(level.events.handlers));

    unit_die(ent, NULL);
    BOOL found = false;
    for (int i = 0; i < MAX_EVENT_QUEUE; i++) {
        if (level.events.queue[i].type == EVENT_UNIT_DEATH) {
            found = true;
            break;
        }
    }
    T_ASSERT(found);
}

TEST(wc3_unit, hero_dissipation_marks_same_hero_revivable_and_hidden) {
    reset_test_entities();
    LPEDICT hero = make_inventory_unit(0, 0);
    hero->health.value = hero->health.max_value = 500.0f;

    unit_die(hero, NULL);
    T_ASSERT(hero->svflags & SVF_DEADMONSTER);
    T_ASSERT(!hero->revival.awaiting);

    unit_begin_decay(hero);
    /* Drive exactly one elapsed simulation step without depending on the
     * archive's configured DissipateTime in this unit test. */
    hero->wait = (FLOAT)FRAMETIME / 1000.0f;
    unit_decay_think(hero);

    T_ASSERT(hero->inuse);
    T_ASSERT(hero->revival.awaiting);
    T_ASSERT(hero->s.renderfx & RF_HIDDEN);
}

TEST(wc3_unit, scripted_revive_clears_altar_revival_state_on_same_hero) {
    reset_test_entities();
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT altar = make_unit(0, 0);
    LPEDICT hero = make_inventory_unit(0, 0);
    altar->s.player = hero->s.player = client->ps.number;
    altar->build = hero;
    hero->health.max_value = 500.0f;
    hero->mana.max_value = 300.0f;
    hero->svflags |= SVF_DEADMONSTER;
    hero->s.flags |= EF_NOT_SELECTABLE;
    hero->s.renderfx |= RF_HIDDEN;
    hero->revival.awaiting = true;
    hero->revival.reviving = true;
    hero->revival.producer = altar;
    hero->revival.player = client->ps.number;
    hero->revival.gold = 100;
    hero->revival.lumber = 50;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    G_ReviveHero(hero, 64.0f, 96.0f);

    T_NULL(altar->build);
    T_ASSERT(hero->inuse);
    T_ASSERT(!(hero->svflags & SVF_DEADMONSTER));
    T_ASSERT(!(hero->s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!(hero->s.renderfx & RF_HIDDEN));
    T_ASSERT(!hero->revival.awaiting);
    T_ASSERT(!hero->revival.reviving);
    T_NULL(hero->revival.producer);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 50);
    T_EQ((int)hero->s.origin2.x, 64);
    T_EQ((int)hero->s.origin2.y, 96);
    T_ASSERT(hero->health.value > 0.0f);
    G_SelectEntity(client, hero);
    T_ASSERT(G_IsEntitySelected(client, hero));
}

TEST(wc3_unit, removing_producer_cancels_mixed_revival_and_training_queue) {
    reset_test_entities();
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT altar = make_unit(0, 0);
    LPEDICT hero = make_inventory_unit(0, 0);
    LPEDICT trainee = make_unit(0, 0);
    LONG gold = MAX(0, trainee->data.UnitBalance->goldCost);
    LONG lumber = MAX(0, trainee->data.UnitBalance->lumberCost);

    altar->s.player = hero->s.player = trainee->s.player = client->ps.number;
    altar->build = hero;
    hero->revival.awaiting = true;
    hero->revival.reviving = true;
    hero->revival.producer = altar;
    hero->revival.queue_next = trainee;
    hero->revival.player = client->ps.number;
    hero->revival.gold = 100;
    hero->revival.lumber = 50;
    trainee->training = true;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    G_FreeEdict(altar);

    T_ASSERT(!altar->inuse);
    T_ASSERT(hero->inuse);
    T_ASSERT(!hero->revival.reviving);
    T_NULL(hero->revival.producer);
    T_ASSERT(!trainee->inuse);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100 + gold);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 50 + lumber);
}

/* -----------------------------------------------------------------------
 * Order tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, issueorder_move_creates_waypoint) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    BOOL result = unit_issueorder(ent, "move", &dest);
    T_ASSERT(result);
    T_NOT_NULL(ent->goalentity);
}

TEST(wc3_unit, issueorder_move_sets_walk_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "walk");
}

TEST(wc3_unit, shift_move_starts_immediately_when_idle_then_queues_fifo) {
    reset_test_entities();
    setup_test_world();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 a = { 96.0f, 0.0f };
    VECTOR2 b = { 192.0f, 0.0f };
    VECTOR2 c = { 288.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "walk");
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &c, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 2);

    unit_stand(ent);
    T_EQ(G_UnitQueuedOrderCount(ent), 1);
    T_FEQ(ent->goalentity->s.origin2.x, b.x, 0.01f);

    unit_stand(ent);
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_FEQ(ent->goalentity->s.origin2.x, c.x, 0.01f);
}

TEST(wc3_unit, nonqueued_move_replaces_pending_shift_orders) {
    reset_test_entities();
    setup_test_world();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 a = { 96.0f, 0.0f };
    VECTOR2 b = { 192.0f, 0.0f };
    VECTOR2 replacement = { 320.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 1);

    T_ASSERT(unit_issueorder(ent, "move", &replacement));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_FEQ(ent->goalentity->s.origin2.x, replacement.x, 0.01f);
}

TEST(wc3_unit, militia_target_order_reaches_militia_behavior) {
    static UnitAbilities_t const worker_abilities = { .abilList = "Amil" };
    static UnitAbilities_t const hall_abilities = { .abilList = "Amic" };
    LPEDICT worker;
    LPEDICT hall;

    reset_test_entities();
    setup_test_world();
    worker = make_unit(0, 0);
    hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 256, 0);
    worker->data.UnitAbilities = &worker_abilities;
    hall->data.UnitAbilities = &hall_abilities;
    worker->s.player = hall->s.player = 1;
    hall->pathtex = NULL;

    T_ASSERT(G_IssueUnitTargetOrder(worker, "militia", hall, false, worker->s.player));
    T_ASSERT(worker->militia.partner == hall);
    T_NOT_NULL(worker->currentmove);
    T_ASSERT(worker->currentmove->ability == &a_militia);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, stale_queued_entity_target_is_skipped_for_next_order) {
    reset_test_entities();
    setup_test_world();
    LPEDICT ent = make_unit(0, 0);
    LPEDICT target = make_unit(128, 0);
    VECTOR2 a = { 64.0f, 0.0f };
    VECTOR2 b = { 256.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitTargetOrder(ent, "attack", target, true, 0));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 2);

    /* Simulate recycling the target slot before the queued Attack begins. */
    target->spawn_time++;
    unit_stand(ent);

    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "walk");
    T_FEQ(ent->goalentity->s.origin2.x, b.x, 0.01f);
}

TEST(wc3_unit, stop_clears_pending_shift_orders) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 a = { 96.0f, 0.0f };
    VECTOR2 b = { 192.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 1);

    T_ASSERT(unit_issueimmediateorder(ent, "stop"));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, point_attack_order_uses_attack_move_behavior) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = { 128.0f, 0.0f };

    T_ASSERT(unit_issueorder(ent, "attack", &dest));
    T_NOT_NULL(ent->movement.attackmove_waypoint);
    T_ASSERT(ent->goalentity == ent->movement.attackmove_waypoint);
}

TEST(wc3_unit, issueorder_move_preserves_combat_state) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    LPEDICT target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (VECTOR2){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;
    VECTOR2 dest = {100.0f, 0.0f};

    unit_entercombat(ent, target);
    unit_issueorder(ent, "move", &dest);

    T_ASSERT(unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "walk");
}

TEST(wc3_unit, issueorder_unknown_returns_false) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    BOOL result = unit_issueorder(ent, "patrol", &dest);
    T_ASSERT(!result);
}

TEST(wc3_unit, issueorder_null_inputs_return_false) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};

    T_ASSERT(!unit_issueorder(NULL, "move", &dest));
    T_ASSERT(!unit_issueorder(ent, NULL, &dest));
    T_ASSERT(!unit_issueorder(ent, "move", NULL));
}

TEST(wc3_unit, issueimmediateorder_stop) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);
    T_STREQ(ent->currentmove->animation, "walk");

    BOOL result = unit_issueimmediateorder(ent, "stop");
    T_ASSERT(result);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, issueimmediateorder_holdposition_uses_hold_state) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);

    T_ASSERT(unit_issueimmediateorder(ent, "holdposition"));
    T_ASSERT(ent->movement.holding_position);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, issueimmediateorder_autoharvestlumber_uses_nearest_live_tree) {
    reset_test_entities();
    LPEDICT worker = make_unit(0, 0);
    LPEDICT far_tree = unit_make_harvest_tree(300.0f, 0.0f);
    LPEDICT dead_tree = unit_make_harvest_tree(25.0f, 0.0f);
    LPEDICT near_tree = unit_make_harvest_tree(100.0f, 0.0f);
    (void)far_tree;
    dead_tree->health.value = 0.0f;

    T_ASSERT(unit_issueimmediateorder(worker, "autoharvestlumber"));
    T_ASSERT(worker->goalentity == near_tree);
    T_ASSERT(worker->secondarygoal == near_tree);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, issueimmediateorder_autoharvestgold_uses_nearest_live_mine) {
    reset_test_entities();
    LPEDICT worker = make_unit(0, 0);
    LPEDICT far_mine = unit_make_harvest_goldmine(300.0f, 0.0f);
    LPEDICT empty_mine = unit_make_harvest_goldmine(25.0f, 0.0f);
    LPEDICT near_mine = unit_make_harvest_goldmine(100.0f, 0.0f);
    (void)far_mine;
    empty_mine->resources = 0;

    T_ASSERT(unit_issueimmediateorder(worker, "autoharvestgold"));
    T_ASSERT(worker->goalentity == near_mine);
    T_ASSERT(worker->secondarygoal == near_mine);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, issueimmediateorder_autoharvest_requires_resource_target) {
    reset_test_entities();
    LPEDICT worker = make_unit(0, 0);

    T_ASSERT(!unit_issueimmediateorder(worker, "autoharvestlumber"));
    T_ASSERT(!unit_issueimmediateorder(worker, "autoharvestgold"));
    T_STREQ(worker->currentmove->animation, "stand");
}

TEST(wc3_unit, issueimmediateorder_unknown_returns_false) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    BOOL result = unit_issueimmediateorder(ent, "patrol");
    T_ASSERT(!result);
}

TEST(wc3_unit, issueimmediateorder_null_inputs_return_false) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);

    T_ASSERT(!unit_issueimmediateorder(NULL, "stop"));
    T_ASSERT(!unit_issueimmediateorder(ent, NULL));
}

/* -----------------------------------------------------------------------
 * Inventory tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, additemtoslot_fills_empty_slot) {
    reset_test_entities();
    LPEDICT ent  = make_inventory_unit(0, 0);
    LPEDICT item = make_world_item(MAKEFOURCC('r','a','t','f'));
    BOOL ok = unit_additemtoslot(ent, item, 0);
    T_ASSERT(ok);
    T_ASSERT(ent->inventory[0] == item);
}

TEST(wc3_unit, additemtoslot_rejects_occupied_slot) {
    reset_test_entities();
    LPEDICT ent   = make_inventory_unit(0, 0);
    LPEDICT item1 = make_world_item(MAKEFOURCC('r','a','t','f'));
    LPEDICT item2 = make_world_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(ent, item1, 0);
    BOOL ok = unit_additemtoslot(ent, item2, 0);
    T_ASSERT(!ok);
}

TEST(wc3_unit, additem_fills_first_free_slot) {
    reset_test_entities();
    LPEDICT ent   = make_inventory_unit(0, 0);
    LPEDICT item1 = make_world_item(MAKEFOURCC('r','a','t','f'));
    LPEDICT item2 = make_world_item(MAKEFOURCC('r','d','e','2'));
    unit_additemtoslot(ent, item1, 0);
    BOOL ok = unit_additem(ent, item2);
    T_ASSERT(ok);
    T_ASSERT(ent->inventory[1] == item2);
}

TEST(wc3_unit, additem_fails_when_inventory_full) {
    reset_test_entities();
    LPEDICT ent = make_inventory_unit(0, 0);
    for (int i = 0; i < MAX_INVENTORY; i++) {
        LPEDICT item = make_world_item(MAKEFOURCC('r','a','t','f'));
        unit_additemtoslot(ent, item, i);
    }
    LPEDICT extra = make_world_item(MAKEFOURCC('r','d','e','2'));
    BOOL ok = unit_additem(ent, extra);
    T_ASSERT(!ok);
}

#endif /* BZ_TESTS */
