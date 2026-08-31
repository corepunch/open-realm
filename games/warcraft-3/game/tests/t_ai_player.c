#ifdef BZ_TESTS

#include "../g_local.h"
#include "jass/jass.h"
#include "shared/test.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
BOOL run_test_jass(LPCSTR src);

TEST(wc3_ai_player, binds_player_and_pauses_sleeping_script) {
    LPPLAYER player = &game.clients[2].ps;

    T_ASSERT(G_PlayerAIStart(player, "test_player.ai", PLAYER_AI_CAMPAIGN));
    T_NOT_NULL(level.player_ai[2].vm);
    G_PlayerAIRunFrame();
    T_NOT_NULL(level.player_ai[2].vm);

    G_PlayerAIPause(2, true);
    level.time = 20000;
    G_PlayerAIRunFrame();
    T_NOT_NULL(level.player_ai[2].vm);

    G_PlayerAIPause(2, false);
    G_PlayerAIRunFrame();
    T_NULL(level.player_ai[2].vm);
}

TEST(wc3_ai_player, roots_are_independent_and_stop_individually) {
    T_ASSERT(G_PlayerAIStart(&game.clients[1].ps, "test_idle.ai", PLAYER_AI_CAMPAIGN));
    T_ASSERT(G_PlayerAIStart(&game.clients[2].ps, "test_idle.ai", PLAYER_AI_MELEE));
    G_PlayerAIRunFrame();
    T_NOT_NULL(level.player_ai[1].vm);
    T_NOT_NULL(level.player_ai[2].vm);
    T_EQ(level.player_ai[1].mode, PLAYER_AI_CAMPAIGN);
    T_EQ(level.player_ai[2].mode, PLAYER_AI_MELEE);
    level.time = 20000;
    G_PlayerAIRunFrame();
    T_NOT_NULL(level.player_ai[1].vm);
    T_NOT_NULL(level.player_ai[2].vm);

    G_PlayerAIStop(1);
    T_NULL(level.player_ai[1].vm);
    T_NOT_NULL(level.player_ai[2].vm);
    G_PlayerAIShutdown();
    T_NULL(level.player_ai[2].vm);
}

TEST(wc3_ai_player, replacement_and_missing_script_are_bounded) {
    LPPLAYER player = &game.clients[0].ps;

    T_ASSERT(G_PlayerAIStart(player, "test_idle.ai", PLAYER_AI_CAMPAIGN));
    T_ASSERT(G_PlayerAIStart(player, "Scripts\\test_idle.ai", PLAYER_AI_MELEE));
    T_NOT_NULL(level.player_ai[0].vm);
    T_EQ(level.player_ai[0].mode, PLAYER_AI_MELEE);
    T_ASSERT(!G_PlayerAIStart(player, "missing.ai", PLAYER_AI_CAMPAIGN));
    T_NULL(level.player_ai[0].vm);
    T_ASSERT(!G_PlayerAIStart(player, "test_no_main.ai", PLAYER_AI_CAMPAIGN));
    T_NULL(level.player_ai[0].vm);
    T_ASSERT(!G_PlayerAIStart(player, "test_bad_init.ai", PLAYER_AI_CAMPAIGN));
    T_NULL(level.player_ai[0].vm);
}

TEST(wc3_ai_player, deferred_stop_removes_only_requested_player) {
    T_ASSERT(G_PlayerAIStart(&game.clients[1].ps, "test_idle.ai", PLAYER_AI_CAMPAIGN));
    T_ASSERT(G_PlayerAIStart(&game.clients[2].ps, "test_idle.ai", PLAYER_AI_CAMPAIGN));
    G_PlayerAIRequestStop(1);
    G_PlayerAIRunFrame();
    T_NULL(level.player_ai[1].vm);
    T_NOT_NULL(level.player_ai[2].vm);
}

TEST(wc3_ai_player, replacement_requested_inside_ai_is_deferred) {
    T_ASSERT(G_PlayerAIStart(&game.clients[1].ps, "test_replace.ai", PLAYER_AI_CAMPAIGN));
    G_PlayerAIRunFrame();
    T_NOT_NULL(level.player_ai[1].vm);
    T_EQ(level.player_ai[1].mode, PLAYER_AI_CAMPAIGN);
    T_STREQ(level.player_ai[1].script, "Scripts\\test_idle.ai");
    G_PlayerAIRunFrame();
    T_NOT_NULL(level.player_ai[1].vm);
}

TEST(wc3_ai_player, query_natives_read_authoritative_player_state) {
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

    T_ASSERT(G_PlayerAIStart(&game.clients[2].ps, "test_queries.ai", PLAYER_AI_CAMPAIGN));
    G_PlayerAIRunFrame();
    T_NOT_NULL(level.player_ai[2].vm);
    if (level.player_ai[2].vm) T_ASSERT(!jass_rterror_pending(level.player_ai[2].vm));
}

TEST(wc3_ai_player, unit_alive_rejects_null_dead_and_removed_handles) {
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    unit->health.value = 100;
    T_ASSERT(G_AIUnitAlive(unit));
    unit->health.value = 0;
    T_ASSERT(!G_AIUnitAlive(unit));
    unit->health.value = 100; unit->svflags |= SVF_DEADMONSTER;
    T_ASSERT(!G_AIUnitAlive(unit));
    unit->svflags &= ~SVF_DEADMONSTER; unit->inuse = false;
    T_ASSERT(!G_AIUnitAlive(unit));
    T_ASSERT(!G_AIUnitAlive(NULL));
}

#endif /* BZ_TESTS */
