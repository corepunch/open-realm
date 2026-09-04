#ifdef BZ_TESTS

#include "../g_local.h"
#include "jass/jass.h"
#include "../skills/s_skills.h"
#include "shared/test.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
BOOL run_test_jass(LPCSTR src);
void reset_entities(void);
void setup_test_pathmap(DWORD width, DWORD height, BYTE const *cells);
void setup_test_world(void);

static UnitAbilities_t const bot_harvester_abilities = { .abilList = "Ahar" };
static UnitAbilities_t const bot_hall_abilities = { .abilList = "Argl" };
static UnitAbilities_t const bot_mine_abilities = { .abilList = "Abgm" };

TEST(wc3_bot, display_text_formats_only_authoritative_integer_templates) {
    LONG values[] = {12, -3, 7};
    char text[64], small[8];

    BotDisplayFormat(text, sizeof(text), "values %d %d %d %% %x\\n", values, sizeof(values) / sizeof(*values));
    T_STREQ(text, "values 12 -3 7 % %x\n");
    BotDisplayFormat(text, sizeof(text), "missing %d", values, 0);
    T_STREQ(text, "missing %d");
    BotDisplayFormat(small, sizeof(small), "123456789", values, sizeof(values) / sizeof(*values));
    T_STREQ(small, "1234567");
}

static LPEDICT make_bot_harvest_unit(DWORD class_id, FLOAT x, FLOAT y, DWORD player, UnitAbilities_t const *abilities) {
    LPEDICT unit = alloc_test_unit(class_id, x, y);
    unit->s.player = player; unit->data.UnitAbilities = abilities;
    unit->health.value = unit->health.max_value = 1000; unit->stand = unit_stand;
    return unit;
}

TEST(wc3_bot, binds_player_and_pauses_sleeping_script) {
    LPPLAYER player = &game.clients[2].ps;

    T_ASSERT(G_BotStart(player, "test_player.ai", BOT_CAMPAIGN));
    T_NOT_NULL(level.bots[2].vm);
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);

    G_BotPause(2, true);
    level.time = 20000;
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);

    G_BotPause(2, false);
    G_BotRunFrame();
    T_NULL(level.bots[2].vm);
}

TEST(wc3_bot, roots_are_independent_and_stop_individually) {
    T_ASSERT(G_BotStart(&game.clients[1].ps, "test_idle.ai", BOT_CAMPAIGN));
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_idle.ai", BOT_MELEE));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[1].vm);
    T_NOT_NULL(level.bots[2].vm);
    T_EQ(level.bots[1].mode, BOT_CAMPAIGN);
    T_EQ(level.bots[2].mode, BOT_MELEE);
    level.time = 20000;
    G_BotRunFrame();
    T_NOT_NULL(level.bots[1].vm);
    T_NOT_NULL(level.bots[2].vm);

    G_BotStop(1);
    T_NULL(level.bots[1].vm);
    T_NOT_NULL(level.bots[2].vm);
    G_BotShutdown();
    T_NULL(level.bots[2].vm);
}

TEST(wc3_bot, replacement_and_missing_script_are_bounded) {
    LPPLAYER player = &game.clients[0].ps;

    T_ASSERT(G_BotStart(player, "test_idle.ai", BOT_CAMPAIGN));
    T_ASSERT(G_BotStart(player, "Scripts\\test_idle.ai", BOT_MELEE));
    T_NOT_NULL(level.bots[0].vm);
    T_EQ(level.bots[0].mode, BOT_MELEE);
    T_ASSERT(!G_BotStart(player, "missing.ai", BOT_CAMPAIGN));
    T_NULL(level.bots[0].vm);
    T_ASSERT(!G_BotStart(player, "test_no_main.ai", BOT_CAMPAIGN));
    T_NULL(level.bots[0].vm);
    T_ASSERT(!G_BotStart(player, "test_bad_init.ai", BOT_CAMPAIGN));
    T_NULL(level.bots[0].vm);
}

TEST(wc3_bot, deferred_stop_removes_only_requested_player) {
    T_ASSERT(G_BotStart(&game.clients[1].ps, "test_idle.ai", BOT_CAMPAIGN));
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_idle.ai", BOT_CAMPAIGN));
    G_BotRequestStop(1);
    G_BotRunFrame();
    T_NULL(level.bots[1].vm);
    T_NOT_NULL(level.bots[2].vm);
}

TEST(wc3_bot, replacement_requested_inside_ai_is_deferred) {
    T_ASSERT(G_BotStart(&game.clients[1].ps, "test_replace.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[1].vm);
    T_EQ(level.bots[1].mode, BOT_CAMPAIGN);
    T_STREQ(level.bots[1].script, "Scripts\\test_idle.ai");
    G_BotRunFrame();
    T_NOT_NULL(level.bots[1].vm);
}

TEST(wc3_bot, query_natives_read_authoritative_player_state) {
    LPEDICT done = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    LPEDICT training = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    LPEDICT dead = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    LPEDICT other = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    LPEDICT hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 128, 2, &bot_hall_abilities);
    LPEDICT mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 128, MAX_PLAYERS, &bot_mine_abilities);
    LPEDICT builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 128);

    done->s.player = building->s.player = training->s.player = dead->s.player = 2;
    other->s.player = 1;
    done->svflags |= SVF_MONSTER; building->svflags |= SVF_MONSTER; training->svflags |= SVF_MONSTER;
    dead->svflags |= SVF_MONSTER | SVF_DEADMONSTER; other->svflags |= SVF_MONSTER;
    building->construction.active = true;
    training->training = true;
    builder->s.player = 2; builder->health.value = 100; builder->build_project = MAKEFOURCC('h','b','a','r');
    mine->resources = 1000;
    G_SetPlayerTechResearched(&game.clients[2], MAKEFOURCC('R','h','m','e'), 2);

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_queries.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);
    if (level.bots[2].vm) T_ASSERT(!jass_rterror_pending(level.bots[2].vm));
    T_EQ(G_BotTown(&game.clients[2].ps, 0), hall); T_EQ(G_BotTownMine(&game.clients[2].ps, 0), mine);
}

TEST(wc3_bot, mines_belong_to_the_nearest_owned_town) {
    LPPLAYER player = &game.clients[2].ps;
    reset_entities();
    LPEDICT hall0 = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    LPEDICT hall1 = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 1000, 0, 2, &bot_hall_abilities);
    LPEDICT mine0 = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 100, 0, MAX_PLAYERS, &bot_mine_abilities);
    LPEDICT mine1 = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 900, 0, MAX_PLAYERS, &bot_mine_abilities);
    mine0->resources = 1000; mine1->resources = 2000;

    T_EQ(G_BotTown(player, 0), hall0); T_EQ(G_BotTown(player, 1), hall1);
    T_EQ(G_BotTownMine(player, 0), mine0); T_EQ(G_BotTownMine(player, 1), mine1);
    T_EQ(G_BotTownWithMine(player), 0); T_EQ(G_BotMinesOwned(player), 2); T_EQ(G_BotGoldOwned(player), 3000);
    mine0->resources = 0;
    T_NULL(G_BotTownMine(player, 0)); T_EQ(G_BotTownWithMine(player), 1); T_EQ(G_BotMinesOwned(player), 1);
    T_EQ(G_BotGoldOwned(player), 2000);
}

TEST(wc3_bot, produce_queues_trainable_units_and_rejects_unknown_types) {
    LPPLAYER player = &game.clients[2].ps;
    LPEDICT producer;
    UnitProfile_t profile = { .trains = "hfoo" };
    reset_entities();
    producer = make_bot_harvest_unit(MAKEFOURCC('h','b','a','r'), 0, 0, 2, NULL);
    producer->data.UnitProfile = &profile;
    player->stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    player->stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    T_ASSERT(G_BotProduce(player, 2, MAKEFOURCC('h','f','o','o'), -1));
    T_NOT_NULL(producer->build); T_EQ(producer->build->class_id, MAKEFOURCC('h','f','o','o'));
    T_NOT_NULL(producer->build->build); T_EQ(producer->build->build->class_id, MAKEFOURCC('h','f','o','o'));
    T_ASSERT(!G_BotProduce(player, 1, MAKEFOURCC('u','n','k','n'), -1));
}

TEST(wc3_bot, build_site_requires_direct_static_route) {
    BYTE cells[100] = {0};
    edict_t worker = { .collision = 0.0f, .s.origin2 = { 1.0f, 5.0f } };
    VECTOR2 same_side = { 4.0f, 5.0f }, across_wall = { 8.0f, 5.0f };
    FOR_LOOP(y, 10) cells[5 + y * 10] = 2;
    setup_test_pathmap(10, 10, cells);
    T_ASSERT(G_BotBuildSiteReachable(&worker, &same_side));
    T_ASSERT(!G_BotBuildSiteReachable(&worker, &across_wall));
    T_ASSERT(!G_BotBuildSiteReachable(NULL, &same_side));
    setup_test_world();
}

TEST(wc3_bot, unit_alive_rejects_null_dead_and_removed_handles) {
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    unit->health.value = 100;
    T_ASSERT(G_BotUnitAlive(unit));
    unit->health.value = 0;
    T_ASSERT(!G_BotUnitAlive(unit));
    unit->health.value = 100; unit->svflags |= SVF_DEADMONSTER;
    T_ASSERT(!G_BotUnitAlive(unit));
    unit->svflags &= ~SVF_DEADMONSTER; unit->inuse = false;
    T_ASSERT(!G_BotUnitAlive(unit));
    T_ASSERT(!G_BotUnitAlive(NULL));
}

TEST(wc3_bot, campaign_settings_persist_for_authoritative_consumers) {
    bot_t *ai;
    DWORD enabled = BOT_TARGET_HEROES | BOT_HEROES_FLEE | BOT_IGNORE_INJURED |
        BOT_UNITS_FLEE | BOT_SLOW_CHOPPING | BOT_SMART_ARTILLERY | BOT_NEW_HEROES |
        BOT_DEFEND_PLAYER;

    T_ASSERT(G_BotStart(&game.clients[3].ps, "test_ai_settings.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    ai = level.bots + 3;
    T_NOT_NULL(ai->vm);
    T_EQ(ai->mode, BOT_CAMPAIGN);
    T_NOT_NULL(ai->hero_levels);
    T_STREQ(jass_functionname(ai->hero_levels), "hero_levels");
    T_EQ(ai->replacement_count, 3);
    T_EQ(ai->flags, enabled);
    T_ASSERT(!(ai->flags & BOT_PEONS_REPAIR));
    T_ASSERT(!(ai->flags & BOT_WATCH_MEGA));
    T_ASSERT(!(ai->flags & BOT_HEROES_TAKE_ITEM));
    T_ASSERT(!(ai->flags & BOT_GROUPS_FLEE));
    T_ASSERT(!(ai->flags & BOT_CAPTAIN_CHANGES));
    T_ASSERT(!(ai->flags & BOT_GROUP_TIMED_LIFE));
    T_ASSERT(!(ai->flags & BOT_RANDOM_PATHS));
    T_ASSERT(!(ai->flags & BOT_HEROES_BUY_ITEMS));
}

TEST(wc3_bot, melee_settings_cover_inverse_flags_and_clamp_replacements) {
    bot_t *ai;
    DWORD enabled = BOT_PEONS_REPAIR | BOT_WATCH_MEGA | BOT_HEROES_TAKE_ITEM |
        BOT_GROUPS_FLEE | BOT_CAPTAIN_CHANGES | BOT_GROUP_TIMED_LIFE |
        BOT_RANDOM_PATHS | BOT_HEROES_BUY_ITEMS;

    T_ASSERT(G_BotStart(&game.clients[4].ps, "test_ai_settings_inverse.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    ai = level.bots + 4;
    T_NOT_NULL(ai->vm);
    T_EQ(ai->mode, BOT_MELEE);
    T_EQ(ai->replacement_count, 0);
    T_EQ(ai->flags, enabled);
    T_ASSERT(!(ai->flags & BOT_TARGET_HEROES));
    T_ASSERT(!(ai->flags & BOT_HEROES_FLEE));
    T_ASSERT(!(ai->flags & BOT_IGNORE_INJURED));
    T_ASSERT(!(ai->flags & BOT_UNITS_FLEE));
    T_ASSERT(!(ai->flags & BOT_SLOW_CHOPPING));
    T_ASSERT(!(ai->flags & BOT_SMART_ARTILLERY));
    T_ASSERT(!(ai->flags & BOT_NEW_HEROES));
    T_ASSERT(!(ai->flags & BOT_DEFEND_PLAYER));
}

TEST(wc3_bot, stop_gathering_stops_only_owned_harvesters_and_releases_mines) {
    static umove_t lumber_move = { "attack", NULL, NULL, &a_harvest };
    static umove_t gold_move = { "attack", NULL, NULL, &a_goldmine };
    static umove_t attack_move = { "attack", NULL, NULL, &a_attack };
    LPEDICT lumber = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT gold = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32, 0);
    LPEDICT other = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 0);
    LPEDICT fighter = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    LPEDICT mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 128, 0);

    lumber->s.player = gold->s.player = fighter->s.player = 2; other->s.player = 1;
    lumber->stand = gold->stand = other->stand = fighter->stand = unit_stand;
    lumber->currentmove = &lumber_move; gold->currentmove = &gold_move;
    other->currentmove = &lumber_move; fighter->currentmove = &attack_move;
    gold->goldmine.mine = mine; gold->goldmine.mine_spawn_time = mine->spawn_time;
    gold->invulnerable = true; gold->s.renderfx |= RF_HIDDEN; mine->peonsinside = 1;
    lumber->harvested_lumber = 7; gold->harvested_gold = 5;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_stop_gathering.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NULL(lumber->currentmove->ability);
    T_NULL(gold->currentmove->ability);
    T_EQ(lumber->harvested_lumber, 7);
    T_EQ(gold->harvested_gold, 5);
    T_NULL(gold->goldmine.mine);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(!(gold->s.renderfx & RF_HIDDEN));
    T_ASSERT(!gold->invulnerable);
    T_EQ(other->currentmove->ability, &a_harvest);
    T_EQ(fighter->currentmove->ability, &a_attack);
}

TEST(wc3_bot, harvest_gold_assigns_nearest_owned_workers_up_to_quota) {
    bot_t *bot = level.bots + 2;
    reset_entities();
    LPEDICT hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    LPEDICT trainee = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 8, 0, 2, &bot_harvester_abilities);
    LPEDICT builder = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 16, 0, 2, &bot_harvester_abilities);
    LPEDICT near = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 32, 0, 2, &bot_harvester_abilities);
    LPEDICT far = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 96, 0, 2, &bot_harvester_abilities);
    LPEDICT other = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 16, 0, 1, &bot_harvester_abilities);
    LPEDICT mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 0, MAX_PLAYERS, &bot_mine_abilities);
    mine->resources = 1000; trainee->training = true; trainee->s.renderfx |= RF_HIDDEN;
    builder->build_project = MAKEFOURCC('h','b','a','r');

    G_BotClearHarvest(&game.clients[2].ps);
    G_BotHarvest(&game.clients[2].ps, 0, 1, true);
    T_EQ(ARRAY_COUNT(bot->harvesters), 1); T_EQ(bot->harvesters[0], near);
    T_EQ(near->goalentity, mine); T_EQ(near->currentmove->ability, &a_goldmine);
    T_NULL(trainee->currentmove); T_NULL(builder->currentmove); T_NULL(far->currentmove);
    T_NULL(other->currentmove); T_EQ(hall->s.player, 2);
}

TEST(wc3_bot, harvest_pass_reserves_workers_across_gold_and_wood_then_clears) {
    static umove_t gold_move = { "attack", NULL, NULL, &a_goldmine };
    bot_t *bot = level.bots + 2;
    reset_entities();
    make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    LPEDICT first = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 32, 0, 2, &bot_harvester_abilities);
    LPEDICT second = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 64, 0, 2, &bot_harvester_abilities);
    LPEDICT mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 0, MAX_PLAYERS, &bot_mine_abilities);
    LPEDICT tree = make_bot_harvest_unit(MAKEFOURCC('L','T','l','t'), 0, 256, MAX_PLAYERS, NULL);
    mine->resources = 1000; tree->targtype = TARG_TREE;
    first->currentmove = &gold_move; first->goalentity = mine;

    G_BotClearHarvest(&game.clients[2].ps);
    G_BotHarvest(&game.clients[2].ps, 0, 1, true);
    G_BotHarvest(&game.clients[2].ps, 0, 1, false);
    T_EQ(ARRAY_COUNT(bot->harvesters), 2); T_EQ(bot->harvesters[0], first); T_EQ(bot->harvesters[1], second);
    T_EQ(first->currentmove, &gold_move); T_EQ(first->goalentity, mine); T_EQ(second->goalentity, tree);
    G_BotHarvest(&game.clients[2].ps, 1, 2, true);
    T_EQ(ARRAY_COUNT(bot->harvesters), 2);
    G_BotClearHarvest(&game.clients[2].ps);
    T_EQ(ARRAY_COUNT(bot->harvesters), 0); T_NULL(bot->harvesters);
}

TEST(wc3_bot, harvest_returns_carried_resources_before_collecting) {
    bot_t *bot = level.bots + 2;
    reset_entities();
    LPEDICT hall = make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    LPEDICT worker = make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 64, 0, 2, &bot_harvester_abilities);
    LPEDICT mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 0, MAX_PLAYERS, &bot_mine_abilities);
    mine->resources = 1000; worker->harvested_lumber = 5;

    G_BotClearHarvest(&game.clients[2].ps);
    G_BotHarvest(&game.clients[2].ps, 0, 1, true);
    T_EQ(ARRAY_COUNT(bot->harvesters), 1); T_EQ(worker->goalentity, hall);
    T_EQ(worker->harvested_lumber, 5); T_EQ(worker->currentmove->ability, &a_harvest);
}

TEST(wc3_bot, harvest_natives_execute_through_player_bot_vm) {
    bot_t *bot = level.bots + 2;
    reset_entities();
    make_bot_harvest_unit(MAKEFOURCC('h','t','o','w'), 0, 0, 2, &bot_hall_abilities);
    make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 32, 0, 2, &bot_harvester_abilities);
    make_bot_harvest_unit(MAKEFOURCC('h','p','e','a'), 64, 0, 2, &bot_harvester_abilities);
    LPEDICT mine = make_bot_harvest_unit(MAKEFOURCC('n','g','o','l'), 256, 0, MAX_PLAYERS, &bot_mine_abilities);
    LPEDICT tree = make_bot_harvest_unit(MAKEFOURCC('L','T','l','t'), 0, 256, MAX_PLAYERS, NULL);
    mine->resources = 1000; tree->targtype = TARG_TREE;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_harvest.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(bot->vm); T_ASSERT(!jass_rterror_pending(bot->vm));
    T_EQ(ARRAY_COUNT(bot->harvesters), 2);
    T_EQ(bot->harvesters[0]->goalentity, mine); T_EQ(bot->harvesters[1]->goalentity, tree);
}

TEST(wc3_bot, create_captains_resets_both_bot_owned_captains) {
    bot_t *bot = level.bots + 2;
    bot->captains[BOT_CAPTAIN_ATTACK].state = BOT_CAPTAIN_ACTIVE;
    bot->captains[BOT_CAPTAIN_ATTACK].desired = 6;
    bot->captains[BOT_CAPTAIN_ATTACK].home.x = 128;
    bot->captains[BOT_CAPTAIN_DEFENSE].state = BOT_CAPTAIN_RETREATING;
    bot->captains[BOT_CAPTAIN_DEFENSE].desired = 3;
    bot->captains[BOT_CAPTAIN_DEFENSE].goal.y = 256;

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_create_captains.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    FOR_LOOP(i, BOT_CAPTAIN_COUNT) {
        T_EQ(bot->captains[i].state, BOT_CAPTAIN_IDLE);
        T_EQ(bot->captains[i].desired, 0);
        T_EQ(ARRAY_COUNT(bot->captains[i].units), 0);
        T_NULL(bot->captains[i].units);
        T_FEQ(bot->captains[i].home.x, 0, 0.001f);
        T_FEQ(bot->captains[i].home.y, 0, 0.001f);
        T_FEQ(bot->captains[i].goal.x, 0, 0.001f);
        T_FEQ(bot->captains[i].goal.y, 0, 0.001f);
    }
}

TEST(wc3_bot, ignored_units_counts_only_live_owned_captain_members) {
    bot_t *bot = level.bots + 2;
    LPEDICT attack = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT defense = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    LPEDICT dead = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    LPEDICT removed = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    LPEDICT wrong_type = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 128, 0);
    LPEDICT wrong_owner = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 160, 0);

    attack->s.player = defense->s.player = dead->s.player = removed->s.player = wrong_type->s.player = 2;
    wrong_owner->s.player = 1;
    attack->health.value = defense->health.value = removed->health.value = wrong_type->health.value = 100;
    wrong_owner->health.value = 100; dead->health.value = 0; removed->inuse = false;
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_ignored_units.ai", BOT_CAMPAIGN));
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(3 * sizeof(LPEDICT));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 3;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = attack;
    bot->captains[BOT_CAPTAIN_ATTACK].units[1] = dead;
    bot->captains[BOT_CAPTAIN_ATTACK].units[2] = wrong_type;
    bot->captains[BOT_CAPTAIN_DEFENSE].units = gi.MemAlloc(3 * sizeof(LPEDICT));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units) = 3;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[0] = defense;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[1] = removed;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[2] = wrong_owner;

    T_EQ(G_BotIgnoredUnits(&game.clients[2].ps, MAKEFOURCC('h','f','o','o')), 2);
    T_EQ(G_BotIgnoredUnits(&game.clients[2].ps, MAKEFOURCC('h','r','i','f')), 1);
    T_EQ(G_BotIgnoredUnits(NULL, MAKEFOURCC('h','f','o','o')), 0);
    G_BotRunFrame();
    T_NOT_NULL(bot->vm);
    T_ASSERT(!jass_rterror_pending(bot->vm));
}

TEST(wc3_bot, captain_in_combat_selects_roster_and_clears_stale_targets) {
    bot_t *bot = level.bots + 2;
    LPEDICT attack = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT defense = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    LPEDICT enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
    attack->health.value = defense->health.value = enemy->health.value = 100;
    attack->s.player = defense->s.player = 2; enemy->s.player = 1;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(sizeof(LPEDICT));
    bot->captains[BOT_CAPTAIN_DEFENSE].units = gi.MemAlloc(sizeof(LPEDICT));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 1;
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units) = 1;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = attack;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[0] = defense;
    attack->combatentity = enemy;

    T_ASSERT(G_BotCaptainInCombat(&game.clients[2].ps, true));
    T_ASSERT(!G_BotCaptainInCombat(&game.clients[2].ps, false));
    enemy->inuse = false;
    T_ASSERT(!G_BotCaptainInCombat(&game.clients[2].ps, true)); T_NULL(attack->combatentity);
}

TEST(wc3_bot, add_defenders_fills_idempotently_from_completed_owned_units) {
    bot_t *bot = level.bots + 2;
    DWORD type = MAKEFOURCC('h','f','o','o');
    LPEDICT first = alloc_test_unit(type, 0, 0), second = alloc_test_unit(type, 32, 0);
    LPEDICT training = alloc_test_unit(type, 64, 0), other = alloc_test_unit(type, 96, 0);
    first->s.player = second->s.player = training->s.player = 2; other->s.player = 1;
    first->health.value = second->health.value = training->health.value = other->health.value = 100;
    training->training = true;

    T_ASSERT(G_BotAddDefenders(&game.clients[2].ps, 2, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 2);
    T_EQ(bot->captains[BOT_CAPTAIN_DEFENSE].units[0], first);
    T_EQ(bot->captains[BOT_CAPTAIN_DEFENSE].units[1], second);
    T_ASSERT(G_BotAddDefenders(&game.clients[2].ps, 2, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 2);
    T_ASSERT(!G_BotAddDefenders(&game.clients[2].ps, 3, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 2);
    T_ASSERT(G_BotAddDefenders(&game.clients[2].ps, 0, type));
}

TEST(wc3_bot, assault_init_resets_attack_only_and_fill_tracks_desired_roster) {
    bot_t *bot = level.bots + 2;
    DWORD type = MAKEFOURCC('h','f','o','o');
    LPEDICT first = make_bot_harvest_unit(type, 0, 0, 2, NULL);
    LPEDICT second = make_bot_harvest_unit(type, 32, 0, 2, NULL);
    LPEDICT building = make_bot_harvest_unit(type, 64, 0, 2, NULL);
    LPEDICT enemy = make_bot_harvest_unit(type, 96, 0, 1, NULL);
    building->construction.active = true;

    G_BotCreateCaptains(&game.clients[2].ps);
    T_ASSERT(G_BotAddDefenders(&game.clients[2].ps, 1, type));
    G_BotInitAssault(&game.clients[2].ps);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].state, BOT_CAPTAIN_FORMING);
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units), 1);
    T_ASSERT(!G_BotAddAssault(&game.clients[2].ps, 2, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 1);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].units[0], second);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].desired, 2);
    T_ASSERT(!G_BotAddAssault(&game.clients[2].ps, 2, type));
    T_EQ(ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units), 1);
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].desired, 4);
    T_ASSERT(first != second && building != enemy);
}

TEST(wc3_bot, captain_size_empty_and_full_count_only_live_assault_members) {
    bot_t *bot = level.bots + 2;
    DWORD type = MAKEFOURCC('h','f','o','o');
    LPEDICT first = make_bot_harvest_unit(type, 0, 0, 2, NULL);
    LPEDICT second = make_bot_harvest_unit(type, 32, 0, 2, NULL);

    G_BotCreateCaptains(&game.clients[2].ps);
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 0);
    T_ASSERT(G_BotCaptainIsFull(&game.clients[2].ps));
    G_BotInitAssault(&game.clients[2].ps);
    T_ASSERT(G_BotAddAssault(&game.clients[2].ps, 2, type));
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 2);
    T_ASSERT(G_BotCaptainIsFull(&game.clients[2].ps));
    first->svflags |= SVF_DEADMONSTER;
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 1);
    T_ASSERT(!G_BotCaptainIsFull(&game.clients[2].ps));
    second->inuse = false;
    T_EQ(G_BotCaptainGroupSize(&game.clients[2].ps), 0);
    T_ASSERT(!G_BotCaptainIsFull(&game.clients[2].ps));
    T_EQ(bot->captains[BOT_CAPTAIN_ATTACK].desired, 2);
}

TEST(wc3_bot, captain_readiness_uses_lower_hero_and_unit_aggregate) {
    static UnitBalance_t hero_balance = { .strength = 1 };
    static UnitBalance_t unit_balance = {0};
    bot_t *bot = level.bots + 2;
    LPEDICT hero = make_bot_harvest_unit(MAKEFOURCC('H','p','a','l'), 0, 0, 2, NULL);
    LPEDICT first = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 32, 0, 2, NULL);
    LPEDICT second = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 64, 0, 2, NULL);
    hero->data.UnitBalance = &hero_balance; first->data.UnitBalance = second->data.UnitBalance = &unit_balance;
    hero->health.value = 333; hero->health.max_value = 1000;
    first->health.value = 100; first->health.max_value = 100;
    second->health.value = 50; second->health.max_value = 100;
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(3 * sizeof(LPEDICT));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 3;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = hero;
    bot->captains[BOT_CAPTAIN_ATTACK].units[1] = first;
    bot->captains[BOT_CAPTAIN_ATTACK].units[2] = second;

    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, false), 33);
    hero->health.value = 1000;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, false), 75);
    first->svflags |= SVF_DEADMONSTER; second->inuse = false;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, false), 100);
}

TEST(wc3_bot, captain_readiness_treats_empty_and_zero_mana_categories_as_full) {
    static UnitBalance_t unit_balance = {0};
    bot_t *bot = level.bots + 2;
    LPEDICT unit = make_bot_harvest_unit(MAKEFOURCC('h','f','o','o'), 0, 0, 2, NULL);
    unit->data.UnitBalance = &unit_balance;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, false), 100);
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, true), 100);
    bot->captains[BOT_CAPTAIN_ATTACK].units = gi.MemAlloc(sizeof(LPEDICT));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_ATTACK].units) = 1;
    bot->captains[BOT_CAPTAIN_ATTACK].units[0] = unit;
    unit->mana.value = unit->mana.max_value = 0;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, true), 100);
    unit->mana.value = 2; unit->mana.max_value = 3;
    T_EQ(G_BotCaptainReadiness(&game.clients[2].ps, true), 66);
    T_EQ(G_BotCaptainReadiness(NULL, true), 100);
}

TEST(wc3_bot, guard_posts_fill_typed_units_without_stealing_captain_members) {
    bot_t *bot = level.bots + 2;
    DWORD type = MAKEFOURCC('h','f','o','o');
    LPEDICT captain = alloc_test_unit(type, 0, 0), first = alloc_test_unit(type, 32, 0);
    LPEDICT second = alloc_test_unit(type, 64, 0), other = alloc_test_unit(type, 96, 0);
    captain->s.player = first->s.player = second->s.player = 2; other->s.player = 1;
    captain->health.value = first->health.value = second->health.value = other->health.value = 100;
    bot->captains[BOT_CAPTAIN_DEFENSE].units = gi.MemAlloc(sizeof(LPEDICT));
    ARRAY_COUNT(bot->captains[BOT_CAPTAIN_DEFENSE].units) = 1;
    bot->captains[BOT_CAPTAIN_DEFENSE].units[0] = captain;

    G_BotAddGuardPost(&game.clients[2].ps, type, 100, 200);
    G_BotAddGuardPost(&game.clients[2].ps, type, 300, 400);
    G_BotFillGuardPosts(&game.clients[2].ps);
    T_EQ(ARRAY_COUNT(bot->guards), 2);
    T_EQ(bot->guards[0].unit, first); T_EQ(bot->guards[1].unit, second);
    T_FEQ(bot->guards[0].origin.x, 100, 0.001f); T_FEQ(bot->guards[1].origin.y, 400, 0.001f);
    T_EQ(G_BotIgnoredUnits(&game.clients[2].ps, type), 3);
}

TEST(wc3_bot, guard_posts_replace_dead_members_and_leave_missing_types_empty) {
    bot_t *bot = level.bots + 2;
    DWORD type = MAKEFOURCC('h','f','o','o');
    LPEDICT dead = alloc_test_unit(type, 0, 0), replacement = alloc_test_unit(type, 32, 0);
    dead->s.player = replacement->s.player = 2; dead->health.value = replacement->health.value = 100;
    G_BotAddGuardPost(&game.clients[2].ps, type, 100, 200);
    G_BotAddGuardPost(&game.clients[2].ps, MAKEFOURCC('h','r','i','f'), 300, 400);
    G_BotFillGuardPosts(&game.clients[2].ps);
    T_EQ(bot->guards[0].unit, dead); T_NULL(bot->guards[1].unit);
    dead->health.value = 0;
    G_BotFillGuardPosts(&game.clients[2].ps);
    T_EQ(bot->guards[0].unit, replacement); T_NULL(bot->guards[1].unit);
}

TEST(wc3_bot, return_guard_posts_moves_idle_units_but_preserves_combat) {
    bot_t *bot = level.bots + 2;
    DWORD type = MAKEFOURCC('h','f','o','o');
    LPEDICT idle = alloc_test_unit(type, 0, 0), fighting = alloc_test_unit(type, 0, 32);
    LPEDICT enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
    idle->s.player = fighting->s.player = 2; enemy->s.player = 1;
    idle->health.value = fighting->health.value = enemy->health.value = 100;
    idle->stand = fighting->stand = unit_stand; fighting->combatentity = enemy;
    G_BotAddGuardPost(&game.clients[2].ps, type, 256, 0);
    G_BotAddGuardPost(&game.clients[2].ps, type, 256, 32);
    G_BotFillGuardPosts(&game.clients[2].ps);
    G_BotReturnGuardPosts(&game.clients[2].ps);
    T_EQ(bot->guards[0].unit, idle); T_EQ(idle->currentmove->ability, &a_move);
    T_FEQ(idle->goalentity->s.origin2.x, 256, 0.001f);
    T_EQ(bot->guards[1].unit, fighting); T_NULL(fighting->currentmove);
    G_BotReturnGuardPosts(&game.clients[3].ps);
    T_EQ(ARRAY_COUNT(level.bots[3].guards), 0);
}

TEST(wc3_bot, command_stack_is_player_owned_and_consumed_by_ai_natives) {
    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_bot_commands.ai", BOT_CAMPAIGN));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "call CommandAI(Player(2),10,20)\ncall CommandAI(Player(2),30,40)\n"
        "call CommandAI(Player(1),50,60)\nendfunction"));
    T_EQ(G_BotCommandsWaiting(&game.clients[2].ps), 2);
    T_EQ(G_BotCommandsWaiting(&game.clients[1].ps), 1);
    T_EQ(G_BotLastCommand(&game.clients[1].ps), 50);
    T_EQ(G_BotLastData(&game.clients[1].ps), 60);

    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);
    T_ASSERT(!jass_rterror_pending(level.bots[2].vm));
    T_EQ(G_BotCommandsWaiting(&game.clients[2].ps), 0);
    T_EQ(G_BotCommandsWaiting(&game.clients[1].ps), 1);
}

#endif /* BZ_TESTS */
