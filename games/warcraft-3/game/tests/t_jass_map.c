#ifdef BZ_TESTS
/*
 * t_jass_map.c — JASS integration tests for quests and win conditions.
 *
 * Quest tests are purely self-contained JASS scripts: they create quests,
 * mutate fields, and assert using BJassAssert + IsQuest* getters.  The C
 * wrapper only checks that run_test_jass() returned true (no JASS error).
 *
 * Win condition tests are hybrid: the JASS script calls RemovePlayer(), and
 * C checks level.events because there is no JASS introspection for the
 * server-side event queue.
 *
 * Scripts load generated fixtures from build/tests/tests.mpq — no War3.mpq.
 *
 * Covered:
 *   Quests   — CreateQuest, QuestSet+IsQuest+ round-trips, QuestCreateItem,
 *              QuestItemSet+IsQuestItem+, multiple quests in one script
 *   Win      — RemovePlayer(DEFEAT) and RemovePlayer(VICTORY) each publish
 *              exactly the right EVENT_PLAYER_* into level.events
 *   Sanity   — BJassAssert true passes, BJassAssert false is caught
 */

#include "test.h"
#include "../g_local.h"

BOOL run_test_jass(LPCSTR src);

/* =========================================================================
 * Helper: scan the event queue for a given type.
 * ========================================================================= */

static BOOL event_in_queue(EVENTTYPE type) {
    for (DWORD i = level.events.read; i < level.events.write; i++)
        if (level.events.queue[i % MAX_EVENT_QUEUE].type == type) return true;
    return false;
}

/* =========================================================================
 * Sanity — assertion helpers work end-to-end
 * ========================================================================= */

TEST(wc3_jass_map, bjassassert_true_passes) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(1 + 1 == 2, \"arithmetic\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, bjassassert_false_is_caught) {
    T_ASSERT(!run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(false, \"intentional failure\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, array_assignment_and_access_evaluate_expressions) {
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer array values\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local integer i = 2\n"
        "  set values[i + 1] = 40 + 2\n"
        "  call BJassAssert(values[3] == 42, \"array expression evaluation\")\n"
        "endfunction\n"
    ));
}

/* =========================================================================
 * Map/player setup — config() state round trips through native enum handles
 * ========================================================================= */

TEST(wc3_jass_map, map_configuration_roundtrip) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetTeams(3)\n"
        "  call SetPlayers(6)\n"
        "  call SetGameTypeSupported(GAME_TYPE_MELEE, true)\n"
        "  call SetGameTypeSupported(GAME_TYPE_FFA, false)\n"
        "  call SetMapFlag(MAP_FOG_MAP_EXPLORED, true)\n"
        "  call SetMapFlag(MAP_FOG_HIDE_TERRAIN, false)\n"
        "  call SetGamePlacement(MAP_PLACEMENT_FIXED)\n"
        "  call SetGameSpeed(MAP_SPEED_FAST)\n"
        "  call SetGameDifficulty(MAP_DIFFICULTY_HARD)\n"
        "  call SetResourceDensity(MAP_DENSITY_HEAVY)\n"
        "  call SetCreatureDensity(MAP_DENSITY_LIGHT)\n"
        "  call BJassAssert(GetTeams() == 3, \"team count\")\n"
        "  call BJassAssert(GetPlayers() == 6, \"player count\")\n"
        "  call BJassAssert(IsGameTypeSupported(GAME_TYPE_MELEE), \"melee supported\")\n"
        "  call BJassAssert(not IsGameTypeSupported(GAME_TYPE_FFA), \"ffa disabled\")\n"
        "  call BJassAssert(IsMapFlagSet(MAP_FOG_MAP_EXPLORED), \"explored flag\")\n"
        "  call BJassAssert(not IsMapFlagSet(MAP_FOG_HIDE_TERRAIN), \"hide terrain disabled\")\n"
        "  call BJassAssert(GetGamePlacement() == MAP_PLACEMENT_FIXED, \"placement\")\n"
        "  call BJassAssert(GetGameSpeed() == MAP_SPEED_FAST, \"speed\")\n"
        "  call BJassAssert(GetGameDifficulty() == MAP_DIFFICULTY_HARD, \"difficulty\")\n"
        "  call BJassAssert(GetResourceDensity() == MAP_DENSITY_HEAVY, \"resource density\")\n"
        "  call BJassAssert(GetCreatureDensity() == MAP_DENSITY_LIGHT, \"creature density\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, map_metadata_and_start_priority_persist) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetMapName(\"Native Test Map\")\n"
        "  call SetMapDescription(\"Native setup state\")\n"
        "  call SetStartLocPrioCount(2, 2)\n"
        "  call SetStartLocPrio(2, 0, 5, MAP_LOC_PRIO_HIGH)\n"
        "  call SetStartLocPrio(2, 1, 7, MAP_LOC_PRIO_NOT)\n"
        "  call BJassAssert(GetStartLocPrioSlot(2, 0) == 5, \"priority location\")\n"
        "  call BJassAssert(GetStartLocPrio(2, 0) == MAP_LOC_PRIO_HIGH, \"high priority\")\n"
        "  call BJassAssert(GetStartLocPrioSlot(2, 1) == 7, \"excluded location\")\n"
        "  call BJassAssert(GetStartLocPrio(2, 1) == MAP_LOC_PRIO_NOT, \"excluded priority\")\n"
        "endfunction\n"
    ));
    T_STREQ(level.setup.name, "Native Test Map");
    T_STREQ(level.setup.description, "Native setup state");
}

TEST(wc3_jass_map, player_configuration_roundtrip) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetPlayerName(Player(0), \"Jaina\")\n"
        "  call SetPlayerRacePreference(Player(0), RACE_PREF_HUMAN)\n"
        "  call SetPlayerRacePreference(Player(0), RACE_PREF_RANDOM)\n"
        "  call SetPlayerRaceSelectable(Player(0), false)\n"
        "  call SetPlayerController(Player(0), MAP_CONTROL_COMPUTER)\n"
        "  call SetPlayerTaxRate(Player(0), Player(1), PLAYER_STATE_RESOURCE_GOLD, 35)\n"
        "  call SetPlayerHandicap(Player(0), 80.0)\n"
        "  call SetPlayerHandicapXP(Player(0), 125.0)\n"
        "  call SetPlayerOnScoreScreen(Player(0), true)\n"
        "  call BJassAssert(GetPlayerName(Player(0)) == \"Jaina\", \"player name\")\n"
        "  call BJassAssert(IsPlayerRacePrefSet(Player(0), RACE_PREF_HUMAN), \"human pref\")\n"
        "  call BJassAssert(IsPlayerRacePrefSet(Player(0), RACE_PREF_RANDOM), \"random pref\")\n"
        "  call BJassAssert(not IsPlayerRacePrefSet(Player(0), RACE_PREF_ORC), \"orc absent\")\n"
        "  call BJassAssert(not GetPlayerSelectable(Player(0)), \"race locked\")\n"
        "  call BJassAssert(GetPlayerController(Player(0)) == MAP_CONTROL_COMPUTER, \"controller\")\n"
        "  call BJassAssert(GetPlayerTaxRate(Player(0), Player(1), PLAYER_STATE_RESOURCE_GOLD) == 35, \"tax\")\n"
        "  call BJassAssert(GetPlayerHandicap(Player(0)) == 80.0, \"handicap\")\n"
        "  call BJassAssert(GetPlayerHandicapXP(Player(0)) == 125.0, \"xp handicap\")\n"
        "endfunction\n"
    ));
    T_ASSERT(game.clients[0].jass.on_score_screen);
}

TEST(wc3_jass_map, region_rectangles_add_query_and_clear) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local region area = CreateRegion()\n"
        "  local rect west = Rect(0.0, 0.0, 10.0, 10.0)\n"
        "  local rect east = Rect(20.0, 20.0, 30.0, 30.0)\n"
        "  local location point = Location(25.0, 25.0)\n"
        "  call RegionAddRect(area, west)\n"
        "  call RegionAddRect(area, east)\n"
        "  call BJassAssert(IsPointInRegion(area, 5.0, 5.0), \"west point\")\n"
        "  call BJassAssert(IsLocationInRegion(area, point), \"east location\")\n"
        "  call BJassAssert(not IsPointInRegion(area, 15.0, 15.0), \"union gap\")\n"
        "  call RegionClearRect(area, west)\n"
        "  call BJassAssert(not IsPointInRegion(area, 5.0, 5.0), \"cleared west\")\n"
        "  call BJassAssert(IsLocationInRegion(area, point), \"east remains\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, force_filters_counts_callbacks_and_alliances) {
    G_SetPlayerAlliance(&game.clients[0].ps, &game.clients[1].ps, ALLIANCE_PASSIVE, true);
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer enum_count = 0\n"
        "  integer enum_sum = 0\n"
        "endglobals\n"
        "function KeepNotOne takes nothing returns boolean\n"
        "  return GetPlayerId(GetFilterPlayer()) != 1\n"
        "endfunction\n"
        "function CountPlayer takes nothing returns nothing\n"
        "  set enum_count = enum_count + 1\n"
        "  set enum_sum = enum_sum + GetPlayerId(GetEnumPlayer())\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local force picked = CreateForce()\n"
        "  local force allies = CreateForce()\n"
        "  local force enemies = CreateForce()\n"
        "  call ForceEnumPlayersCounted(picked, Condition(function KeepNotOne), 2)\n"
        "  call BJassAssert(IsPlayerInForce(Player(0), picked), \"first accepted player\")\n"
        "  call BJassAssert(not IsPlayerInForce(Player(1), picked), \"filtered player\")\n"
        "  call BJassAssert(IsPlayerInForce(Player(2), picked), \"limit counts accepted players\")\n"
        "  call ForForce(picked, function CountPlayer)\n"
        "  call BJassAssert(enum_count == 2, \"force callback count\")\n"
        "  call BJassAssert(enum_sum == 2, \"enum player context\")\n"
        "  call BJassAssert(GetEnumPlayer() == null, \"enum context restored\")\n"
        "  call ForceEnumAllies(allies, Player(0), null)\n"
        "  call BJassAssert(IsPlayerInForce(Player(1), allies), \"allied player\")\n"
        "  call ForceEnumEnemies(enemies, Player(0), Condition(function KeepNotOne))\n"
        "  call BJassAssert(not IsPlayerInForce(Player(1), enemies), \"ally excluded from enemies\")\n"
        "  call BJassAssert(IsPlayerInForce(Player(2), enemies), \"enemy included\")\n"
        "  call ForceClear(picked)\n"
        "  call BJassAssert(not IsPlayerInForce(Player(0), picked), \"force clear\")\n"
        "endfunction\n"
    ));
}

/* =========================================================================
 * Quests — pure JASS tests using IsQuest* getters and BJassAssert
 * ========================================================================= */

TEST(wc3_jass_map, quest_created_is_non_null) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  call BJassAssert(q != null, \"CreateQuest returned null\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, quest_flags_default_false) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  call BJassAssert(not IsQuestCompleted(q),  \"completed should default false\")\n"
        "  call BJassAssert(not IsQuestFailed(q),     \"failed should default false\")\n"
        "  call BJassAssert(not IsQuestDiscovered(q), \"discovered should default false\")\n"
        "  call BJassAssert(not IsQuestRequired(q),   \"required should default false\")\n"
        "  call BJassAssert(not IsQuestEnabled(q),    \"enabled should default false\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, quest_discovered_required_roundtrip) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  call QuestSetDiscovered(q, true)\n"
        "  call QuestSetRequired(q, true)\n"
        "  call BJassAssert(IsQuestDiscovered(q), \"should be discovered\")\n"
        "  call BJassAssert(IsQuestRequired(q),   \"should be required\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, quest_completed_roundtrip) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  call QuestSetCompleted(q, true)\n"
        "  call BJassAssert(IsQuestCompleted(q), \"should be completed\")\n"
        "  call BJassAssert(not IsQuestFailed(q), \"failed stays false\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, quest_failed_roundtrip) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  call QuestSetFailed(q, true)\n"
        "  call BJassAssert(IsQuestFailed(q), \"should be failed\")\n"
        "  call BJassAssert(not IsQuestCompleted(q), \"completed stays false\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, quest_enabled_roundtrip) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  call QuestSetEnabled(q, true)\n"
        "  call BJassAssert(IsQuestEnabled(q), \"should be enabled\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, quest_multiple_independent) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q1 = CreateQuest()\n"
        "  local quest q2 = CreateQuest()\n"
        "  call QuestSetCompleted(q1, true)\n"
        "  call BJassAssert(IsQuestCompleted(q1),      \"q1 should be completed\")\n"
        "  call BJassAssert(not IsQuestCompleted(q2),  \"q2 should not be completed\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, quest_item_completed_roundtrip) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  local questitem item = QuestCreateItem(q)\n"
        "  call BJassAssert(not IsQuestItemCompleted(item), \"item defaults incomplete\")\n"
        "  call QuestItemSetCompleted(item, true)\n"
        "  call BJassAssert(IsQuestItemCompleted(item), \"item should be completed\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_jass_map, quest_multiple_items_independent) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  local questitem a = QuestCreateItem(q)\n"
        "  local questitem b = QuestCreateItem(q)\n"
        "  call QuestItemSetCompleted(a, true)\n"
        "  call BJassAssert(IsQuestItemCompleted(a),      \"item a should be complete\")\n"
        "  call BJassAssert(not IsQuestItemCompleted(b),  \"item b should be incomplete\")\n"
        "endfunction\n"
    ));
}

/* =========================================================================
 * Win conditions — JASS calls RemovePlayer, C checks the event queue
 * ========================================================================= */

TEST(wc3_jass_map, defeat_publishes_event) {
    BOOL ok = run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call RemovePlayer(Player(0), PLAYER_GAME_RESULT_DEFEAT)\n"
        "endfunction\n"
    );
    T_ASSERT(ok);
    T_ASSERT( event_in_queue(EVENT_PLAYER_DEFEAT));
    T_ASSERT(!event_in_queue(EVENT_PLAYER_VICTORY));
}

TEST(wc3_jass_map, victory_publishes_event) {
    BOOL ok = run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call RemovePlayer(Player(0), PLAYER_GAME_RESULT_VICTORY)\n"
        "endfunction\n"
    );
    T_ASSERT(ok);
    T_ASSERT( event_in_queue(EVENT_PLAYER_VICTORY));
    T_ASSERT(!event_in_queue(EVENT_PLAYER_DEFEAT));
}

#endif /* BZ_TESTS */
