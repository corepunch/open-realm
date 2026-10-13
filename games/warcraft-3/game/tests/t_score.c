#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../hud/score_screen_table.h"

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
    T_EQ(value, 0); /* Counts alone no longer fabricate unit value. */
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_HERO_TOTAL, &value));
    T_EQ(value, 230);
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_RESOURCE_TOTAL, &value));
    T_EQ(value, 60);
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_TOTAL, &value));
    T_EQ(value, 290);
    client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED] = UINT32_MAX;
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_TOTAL, &value));
    T_EQ(value, 290); /* Counter saturation is independent of weighted totals. */
}

/* Retail Normal map initialization creates an army before RemoveUnit trims it.
 * The historical peak must survive the first gameplay audit. */
TEST(wc3_score, gameplay_food_audit_preserves_initialization_peak) {
    gameClient_t *client;
    mapPlayer_t *player;

    setup_test_world();
    client = &game.clients[1];
    player = &((mapInfo_t *)level.mapinfo)->players[1];
    player->used = true;
    client->mapplayer = player;
    client->ps.number = 1;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 127;
    client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED] = 171;

    G_PlayerScoreSetGameplayFoodBaseline();
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 127);
    T_EQ(client->score.values[WC3_PLAYER_SCORE_FOOD_MAXUSED], 171);
}

TEST(wc3_score, unit_value_uses_authored_cost_not_unit_count) {
    gameClient_t *client = &game.clients[0];
    edict_t *unit;
    int32_t total = -1;
    uint32_t cost;
    setup_test_world();
    memset(&client->score, 0, sizeof(client->score));
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    T_NOT_NULL(unit);
    T_NOT_NULL(unit->data.UnitBalance);
    unit->s.player = client->ps.number;
    cost = (uint32_t)MAX(0, unit->data.UnitBalance->goldCost) +
           (uint32_t)MAX(0, unit->data.UnitBalance->lumberCost);
    G_PlayerScoreUnitTrained(unit);
    T_EQ(client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED], 1);
    T_EQ(client->score.unit_value_produced, cost);
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_UNIT_TOTAL, &total));
    T_EQ(total, cost);
}

TEST(wc3_score, explicit_score_screen_exclusion_and_normal_player_defaults) {
    gameClient_t *client = &game.clients[0];
    mapPlayer_t *player;
    setup_test_world();
    player = &((mapInfo_t *)level.mapinfo)->players[0];
    memset(&client->jass, 0, sizeof(client->jass));
    client->ps.number = 0;
    client->mapplayer = player;
    player->used = true;
    player->playerType = kPlayerTypeHuman;
    T_ASSERT(G_ScorePlayerEligible(client));
    client->jass.on_score_screen_set = true;
    client->jass.on_score_screen = false;
    T_ASSERT(!G_ScorePlayerEligible(client));
    client->jass.on_score_screen = true;
    T_ASSERT(G_ScorePlayerEligible(client));
    client->jass.on_score_screen_set = false;
    player->playerType = kPlayerTypeNeutral;
    T_ASSERT(!G_ScorePlayerEligible(client));
    player->playerType = kPlayerTypeComputer;
    T_ASSERT(G_ScorePlayerEligible(client));
}

TEST(wc3_score, jass_false_score_screen_override_is_explicit) {
    gameClient_t *client = &game.clients[0];
    setup_test_world();
    client->ps.number = 0;
    memset(&client->jass, 0, sizeof(client->jass));
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetPlayerOnScoreScreen(Player(0), false)\n"
        "endfunction\n"));
    T_ASSERT(client->jass.on_score_screen_set);
    T_ASSERT(!client->jass.on_score_screen);
}

TEST(wc3_score, score_counts_completed_units_but_not_buildings) {
    edict_t *unit;
    edict_t *building;
    gameClient_t *client = &game.clients[0];
    setup_test_world();
    memset(&client->score, 0, sizeof(client->score));
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64.0f, 0.0f);
    unit->s.player = building->s.player = client->ps.number;
    G_PlayerScoreUnitTrained(unit);
    G_PlayerScoreUnitTrained(building);
    T_EQ(client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED], 1);
}

TEST(wc3_score, direct_runtime_creation_counts_produced_unit_once) {
    gameClient_t *client = &game.clients[0];
    edict_t *unit;
    setup_test_world();
    memset(&client->score, 0, sizeof(client->score));
    unit = unit_create(client->ps.number, BZ_WC3_UNIT_PEASANT,
                       &(vec2_t){ 128.0f, 128.0f }, 0.0f);
    T_NOT_NULL(unit);
    T_EQ(client->score.values[WC3_PLAYER_SCORE_UNITS_TRAINED], 1);
}

TEST(wc3_score, hero_experience_appears_in_hero_score) {
    edict_t *hero;
    gameClient_t *client = &game.clients[0];
    int32_t hero_total = 0;
    setup_test_world();
    memset(&client->score, 0, sizeof(client->score));
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0.0f, 0.0f);
    hero->s.player = client->ps.number;
    G_PlayerScoreUnitTrained(hero);
    hero->hero.level = 1;
    G_HeroSetXP(hero, 500);
    G_HeroSetXP(hero, 600);
    G_HeroSetXP(hero, 200); /* lowering an XP value must not add score */
    T_EQ(client->score.heroes_used, 1);
    T_EQ(client->score.experience_gained, 600);
    T_ASSERT(G_GetPlayerScoreValue(client, WC3_PLAYER_SCORE_HERO_TOTAL, &hero_total));
    T_EQ(hero_total, 6000); /* Provisional retail reference: 10 points/XP. */
}

TEST(wc3_score, scripted_level_assignments_do_not_count_as_earned_xp) {
    edict_t *hero;
    gameClient_t *client = &game.clients[0];
    setup_test_world();
    memset(&client->score, 0, sizeof(client->score));
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0.0f, 0.0f);
    hero->s.player = client->ps.number;
    hero->hero.level = 1;
    G_HeroSetXPForLevel(hero, G_HeroXPForLevel(3));
    T_EQ(client->score.experience_gained, 0);
    G_HeroSetXP(hero, hero->hero.xp + 500);
    T_EQ(client->score.experience_gained, 500);
}

TEST(wc3_score, guessed_aggregate_scores_are_queryable_from_jass) {
    setup_test_world();
    memset(&game.clients[0].score, 0, sizeof(game.clients[0].score));
    game.clients[0].score.values[WC3_PLAYER_SCORE_UNITS_TRAINED] = 2;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(GetPlayerScore(Player(0), PLAYER_SCORE_UNIT_TOTAL) == 0, \"counts alone have no weight\")\n"
        "endfunction\n"));
}


TEST(wc3_score, retail_score_table_uses_separate_columns_and_does_not_fabricate_untracked_values) {
    wc3ScorePlayerSnapshot_t player = { 0 };
    int32_t value = -1;
    bool percent = false;

    player.values[WC3_PLAYER_SCORE_UNIT_TOTAL] = 70;
    player.values[WC3_PLAYER_SCORE_HEROES_KILLED] = 3;
    player.values[WC3_PLAYER_SCORE_GOLD_MINED_TOTAL] = 240;
    player.values[WC3_PLAYER_SCORE_TECH_PERCENT] = 60;
    T_EQ(WC3_ScoreColumns(0), 4);
    T_EQ(WC3_ScoreColumns(1), 5);
    T_EQ(WC3_ScoreColumns(2), 5);
    T_EQ(WC3_ScoreColumns(3), 5);
    T_ASSERT(WC3_ScoreCellValue(&player, 0, 0, &value, &percent));
    T_EQ(value, 70);
    T_ASSERT(WC3_ScoreCellValue(&player, 2, 1, &value, &percent));
    T_EQ(value, 3);
    player.heroes_used = 3;
    player.experience_gained = 500;
    T_ASSERT(WC3_ScoreCellValue(&player, 2, 0, &value, &percent));
    T_EQ(value, 3);
    T_ASSERT(WC3_ScoreCellValue(&player, 2, 4, &value, &percent));
    T_EQ(value, 500);
    T_ASSERT(WC3_ScoreCellValue(&player, 3, 0, &value, &percent));
    T_EQ(value, 240);
    T_ASSERT(WC3_ScoreCellValue(&player, 3, 3, &value, &percent));
    T_EQ(value, 60);
    T_ASSERT(!percent); /* Retail integer, no '%' suffix. */
    T_ASSERT(WC3_ScoreCellValue(&player, 3, 2, &value, &percent));
    T_EQ(value, 0);
    T_ASSERT(WC3_ScoreCellValue(&player, 1, 4, &value, &percent));
    T_EQ(value, 0); /* Largest Army is tracked peak food used. */
}

TEST(wc3_score, score_table_scroll_clamps_to_visible_player_range) {
    T_EQ(WC3_ScoreMaxScroll(0), 0);
    T_EQ(WC3_ScoreMaxScroll(WC3_SCORE_VISIBLE_ROWS), 0);
    T_EQ(WC3_ScoreMaxScroll(WC3_SCORE_VISIBLE_ROWS + 5), 5);
    T_EQ(WC3_ScoreScrollStart(WC3_SCORE_VISIBLE_ROWS + 5, 0.0f), 0);
    T_EQ(WC3_ScoreScrollStart(WC3_SCORE_VISIBLE_ROWS + 5, 0.51f), 3);
    T_EQ(WC3_ScoreScrollStart(WC3_SCORE_VISIBLE_ROWS + 5, 1.0f), 5);
    T_EQ(WC3_ScoreScrollStart(WC3_SCORE_VISIBLE_ROWS + 5, NAN), 0);
    T_EQ(WC3_ScoreScrollStart(2, 1.0f), 0);
}

TEST(wc3_score, score_sort_is_stable_and_uses_frozen_values) {
    wc3ScoreSnapshot_t snapshot = { 0 };
    wc3ScoreView_t view = { .column = WC3_SCORE_NO_SORT };
    uint8_t order[MAX_PLAYERS] = { 0 };
    snapshot.count = 4;
    snapshot.players[0].values[WC3_PLAYER_SCORE_UNIT_TOTAL] = 30;
    snapshot.players[1].values[WC3_PLAYER_SCORE_UNIT_TOTAL] = 10;
    snapshot.players[2].values[WC3_PLAYER_SCORE_UNIT_TOTAL] = 30;
    snapshot.players[3].values[WC3_PLAYER_SCORE_UNIT_TOTAL] = 20;
    WC3_ScoreOrderedPlayers(&snapshot, 0, &view, order);
    T_EQ(order[0], 0); T_EQ(order[1], 1);
    WC3_ScoreSortColumn(&view, 0, 0);
    T_ASSERT(view.descending);
    WC3_ScoreOrderedPlayers(&snapshot, 0, &view, order);
    T_EQ(order[0], 0); T_EQ(order[1], 2); T_EQ(order[2], 3); T_EQ(order[3], 1);
    /* Selecting the active column again reverses the ordering, ties stable. */
    WC3_ScoreSortColumn(&view, 0, 0);
    T_ASSERT(!view.descending);
    WC3_ScoreOrderedPlayers(&snapshot, 0, &view, order);
    T_EQ(order[0], 1); T_EQ(order[1], 3); T_EQ(order[2], 0); T_EQ(order[3], 2);
    /* Reordering is a view: it must never rearrange the frozen source. */
    T_EQ(snapshot.players[0].values[WC3_PLAYER_SCORE_UNIT_TOTAL], 30);
    T_EQ(snapshot.players[1].values[WC3_PLAYER_SCORE_UNIT_TOTAL], 10);
}

TEST(wc3_score, score_sort_unavailable_values_last_and_groups_teams) {
    wc3ScoreSnapshot_t snapshot = { 0 };
    wc3ScoreView_t view = { .column = WC3_SCORE_NO_SORT };
    uint8_t order[MAX_PLAYERS] = { 0 };
    snapshot.count = 4;
    snapshot.players[0].number = 3;
    snapshot.players[0].team = 2;
    snapshot.players[0].color = 8;
    snapshot.players[0].values[WC3_PLAYER_SCORE_HEROES_KILLED] = 20;
    snapshot.players[1].number = 1;
    snapshot.players[1].team = 1;
    snapshot.players[1].color = 4;
    snapshot.players[1].values[WC3_PLAYER_SCORE_HEROES_KILLED] = 5;
    snapshot.players[2].number = 0;
    snapshot.players[2].team = 2;
    snapshot.players[2].color = 2;
    snapshot.players[2].values[WC3_PLAYER_SCORE_HEROES_KILLED] = 10;
    snapshot.players[3].number = 2;
    snapshot.players[3].team = 1;
    snapshot.players[3].color = 6;
    WC3_ScoreSortColumn(&view, 2, 1); /* Heroes Killed, not missing Heroes Used. */
    WC3_ScoreOrderedPlayers(&snapshot, 2, &view, order);
    T_EQ(order[0], 0); T_EQ(order[1], 2); T_EQ(order[2], 1); T_EQ(order[3], 3);
    view.teams = true; /* GUESS: team id groups before score order. */
    WC3_ScoreOrderedPlayers(&snapshot, 2, &view, order);
    T_EQ(order[0], 1); T_EQ(order[1], 3); T_EQ(order[2], 0); T_EQ(order[3], 2);
    T_EQ(WC3_ScoreTeamColor(&snapshot, &snapshot.players[0]), 2);
    T_EQ(WC3_ScoreTeamColor(&snapshot, &snapshot.players[1]), 4);
    /* Missing retail counter cannot reorder rows, even if clicked. */
    view.teams = false;
    WC3_ScoreSortColumn(&view, 2, 0);
    WC3_ScoreOrderedPlayers(&snapshot, 2, &view, order);
    T_EQ(order[0], 0); T_EQ(order[1], 1); T_EQ(order[2], 2); T_EQ(order[3], 3);
    WC3_ScoreSortColumn(&view, 1, 4); /* Invalid Units column is ignored. */
    T_EQ(view.column, 0);
}

TEST(wc3_score, scripted_level_xp_follows_current_hero_owner) {
    edict_t *kept;
    edict_t *transferred;
    setup_test_world();
    kept = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0.0f, 0.0f);
    transferred = alloc_test_unit(MAKEFOURCC('H','b','l','m'), 64.0f, 0.0f);
    kept->s.player = transferred->s.player = 0;
    kept->hero.level = transferred->hero.level = 1;
    G_HeroSetXPForLevel(kept, G_HeroXPForLevel(3));
    G_HeroSetXPForLevel(transferred, G_HeroXPForLevel(7));
    T_EQ(G_PlayerScoreScriptedXP(0), G_HeroXPForLevel(3) + G_HeroXPForLevel(7));
    transferred->s.player = 15;
    T_EQ(G_PlayerScoreScriptedXP(0), G_HeroXPForLevel(3));
    T_EQ(G_PlayerScoreScriptedXP(15), G_HeroXPForLevel(7));
    T_EQ(game.clients[0].score.experience_gained, 0);
}

#endif
