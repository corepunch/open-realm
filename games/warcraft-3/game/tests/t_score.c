#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
void unit_die(edict_t *self, edict_t *attacker);
bool run_test_jass(cstring_t src);

TEST(wc3_score, death_credit_separates_units_heroes_and_structures) {
    gameClient_t *killer_client = &game.clients[0];
    edict_t *killer;
    edict_t *unit;
    edict_t *hero;
    edict_t *building;

    setup_test_world();
    killer = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 96.0f, 0.0f);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 128.0f, 0.0f);
    killer->s.player = 0;
    unit->s.player = hero->s.player = building->s.player = 1;

    unit_die(unit, killer);
    unit_die(hero, killer);
    unit_die(building, killer);

    T_EQ(killer_client->score.values[WC3_PLAYER_SCORE_UNITS_KILLED], 1);
    T_EQ(killer_client->score.values[WC3_PLAYER_SCORE_HEROES_KILLED], 1);
    T_EQ(killer_client->score.values[WC3_PLAYER_SCORE_STRUCT_RAZED], 1);
}

TEST(wc3_score, get_player_score_exposes_tracked_raw_values) {
    gameClient_t *client = &game.clients[0];

    setup_test_world();
    client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED] = 9;
    client->score.values[WC3_PLAYER_SCORE_GOLD_LOST_UPKEEP] = 17;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(GetPlayerScore(Player(0), PLAYER_SCORE_UNITS_TRAINED) == 9, \"units trained score must round trip\")\n"
        "  call BJassAssert(GetPlayerScore(Player(0), PLAYER_SCORE_GOLD_LOST_UPKEEP) == 17, \"upkeep score must round trip\")\n"
        "endfunction\n"));
}

TEST(wc3_score, gathered_player_state_updates_score_source) {
    setup_test_world();

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetPlayerState(Player(0), PLAYER_STATE_GOLD_GATHERED, 321)\n"
        "  call SetPlayerState(Player(0), PLAYER_STATE_LUMBER_GATHERED, 654)\n"
        "  call BJassAssert(GetPlayerScore(Player(0), PLAYER_SCORE_GOLD_MINED_TOTAL) == 321, \"gold gathered must feed score\")\n"
        "  call BJassAssert(GetPlayerScore(Player(0), PLAYER_SCORE_LUMBER_TOTAL) == 654, \"lumber gathered must feed score\")\n"
        "endfunction\n"));
}

TEST(wc3_score, guessed_aggregates_are_bounded_and_repeatable) {
    gameClient_t *client = &game.clients[0];
    int32_t value = -1;

    setup_test_world();
    memset(&client->score, 0, sizeof(client->score));
    memset(client->tech, 0, sizeof(client->tech));
    client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED] = 3;
    client->score.values[WC3_PLAYER_SCORE_UNITS_KILLED] = 2;
    client->score.values[WC3_PLAYER_SCORE_STRUCT_BUILT] = 1;
    client->score.values[WC3_PLAYER_SCORE_STRUCT_RAZED] = 1;
    client->score.values[WC3_PLAYER_SCORE_HEROES_KILLED] = 2;
    client->score.values[WC3_PLAYER_SCORE_ITEMS_GAINED] = 3;
    client->score.values[WC3_PLAYER_SCORE_GOLD_MINED_TOTAL] = 300;
    client->score.values[WC3_PLAYER_SCORE_LUMBER_TOTAL] = 150;
    client->tech[0].id = MAKEFOURCC('R','h','m','e');
    client->tech[0].researched = 1;
    client->tech[1].id = MAKEFOURCC('R','h','a','c');

    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_TECH_PERCENT, &value));
    T_EQ(value, 50);
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_UNIT_TOTAL, &value));
    T_EQ(value, 90);
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_HERO_TOTAL, &value));
    T_EQ(value, 230);
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_RESOURCE_TOTAL, &value));
    T_EQ(value, 60);
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_TOTAL, &value));
    T_EQ(value, 880);
    client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED] = UINT32_MAX;
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_TOTAL, &value));
    T_EQ(value, INT32_MAX);
}

TEST(wc3_score, guessed_aggregate_scores_are_queryable_from_jass) {
    setup_test_world();
    memset(&game.clients[0].score, 0, sizeof(game.clients[0].score));
    game.clients[0].score.values[WC3_PLAYER_SCORE_UNITS_TRAINED] = 2;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(GetPlayerScore(Player(0), PLAYER_SCORE_UNIT_TOTAL) == 20, \"guessed unit total\")\n"
        "endfunction\n"));
}


#endif
