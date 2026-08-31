#ifdef BZ_TESTS

#include "../g_local.h"
#include "jass/jass.h"
#include "../skills/s_skills.h"
#include "shared/test.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
BOOL run_test_jass(LPCSTR src);

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

    done->s.player = building->s.player = training->s.player = dead->s.player = 2;
    other->s.player = 1;
    done->svflags |= SVF_MONSTER; building->svflags |= SVF_MONSTER; training->svflags |= SVF_MONSTER;
    dead->svflags |= SVF_MONSTER | SVF_DEADMONSTER; other->svflags |= SVF_MONSTER;
    building->construction.active = true;
    training->training = true;
    G_SetPlayerTechResearched(&game.clients[2], MAKEFOURCC('R','h','m','e'), 2);

    T_ASSERT(G_BotStart(&game.clients[2].ps, "test_queries.ai", BOT_CAMPAIGN));
    G_BotRunFrame();
    T_NOT_NULL(level.bots[2].vm);
    if (level.bots[2].vm) T_ASSERT(!jass_rterror_pending(level.bots[2].vm));
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

#endif /* BZ_TESTS */
