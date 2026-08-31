#ifdef BZ_TESTS

#include "../g_local.h"
#include "jass/jass.h"
#include "shared/test.h"

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

#endif /* BZ_TESTS */
