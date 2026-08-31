#ifdef BZ_TESTS

#include "../g_local.h"
#include "jass/jass.h"
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

#endif /* BZ_TESTS */
