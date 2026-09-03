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
BOOL run_test_jass_error(LPCSTR src, LPCSTR expected);

/* =========================================================================
 * Helper: scan the event queue for a given type.
 * ========================================================================= */

static BOOL event_in_queue(EVENTTYPE type) {
    for (DWORD i = level.events.read; i < level.events.write; i++)
        if (level.events.queue[i % MAX_EVENT_QUEUE].type == type) return true;
    return false;
}

static char victory_menu_action[32];
static char victory_menu_arg[256];

static void capture_victory_menu_action(LPCSTR action, LPCSTR arg) {
    strlcpy(victory_menu_action, action ? action : "", sizeof(victory_menu_action));
    strlcpy(victory_menu_arg, arg ? arg : "", sizeof(victory_menu_arg));
}

static void victory_noop_write(pfWriteType_t type, void const *value) {
    (void)type;
    (void)value;
}

static void victory_noop_unicast(LPEDICT ent) {
    (void)ent;
}

/* =========================================================================
 * Shared JASS/list lexer regressions
 * ========================================================================= */

TEST(wc3_jass_map, parse_segment_quoted_single_value_stops_at_end) {
    PARSER parser = {
        .buffer = "\"Learn Holy Light - [Level %d]\"",
        .delimiters = ""
    };
    LPCSTR value = parse_segment(&parser);

    T_NOT_NULL(value);
    T_STREQ(value, "Learn Holy Light - [Level %d]");
    T_NULL(parse_segment(&parser));
}

TEST(wc3_jass_map, parse_segment_quoted_list_advances_to_next_value) {
    PARSER parser = {
        .buffer = "\"Level 1\",\"Level 2\"",
        .delimiters = ""
    };
    char first[32];
    LPCSTR value = parse_segment(&parser);

    T_NOT_NULL(value);
    strlcpy(first, value, sizeof(first));
    value = parse_segment(&parser);
    T_STREQ(first, "Level 1");
    T_NOT_NULL(value);
    T_STREQ(value, "Level 2");
    T_NULL(parse_segment(&parser));
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
    T_ASSERT(run_test_jass_error(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(false, \"intentional failure\")\n"
        "endfunction\n"
    , "assertion failed: intentional failure"));
}

TEST(wc3_jass_map, bjassassert_error_message_must_match) {
    T_ASSERT(!run_test_jass_error(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(false, \"actual failure\")\n"
        "endfunction\n"
    , "assertion failed: different failure"));
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
        "  call SetDefaultDifficulty(MAP_DIFFICULTY_EASY)\n"
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
        "  call BJassAssert(GetDefaultDifficulty() == MAP_DIFFICULTY_EASY, \"default difficulty\")\n"
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

TEST(wc3_jass_map, player_technology_roundtrip_uses_declared_types) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetPlayerTechMaxAllowed(Player(0), 123456, 3)\n"
        "  call SetPlayerTechResearched(Player(0), 123456, 1)\n"
        "  call AddPlayerTechResearched(Player(0), 123456, 1)\n"
        "  call BJassAssert(GetPlayerTechMaxAllowed(Player(0), 123456) == 3, \"tech maximum\")\n"
        "  call BJassAssert(GetPlayerTechResearched(Player(0), 123456, true), \"tech researched boolean\")\n"
        "  call BJassAssert(GetPlayerTechCount(Player(0), 123456, true) == 2, \"tech level count\")\n"
        "  call SetPlayerTechMaxAllowed(Player(0), 123456, -1)\n"
        "  call BJassAssert(GetPlayerTechMaxAllowed(Player(0), 123456) == -1, \"unlimited tech maximum\")\n"
        "  call SetPlayerTechResearched(Player(0), 123456, 0)\n"
        "  call BJassAssert(not GetPlayerTechResearched(Player(0), 123456, false), \"tech researched cleared\")\n"
        "endfunction\n"
    ));
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

TEST(wc3_jass_map, quest_flags_default_state) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  call BJassAssert(not IsQuestCompleted(q),  \"completed should default false\")\n"
        "  call BJassAssert(not IsQuestFailed(q),     \"failed should default false\")\n"
        "  call BJassAssert(not IsQuestDiscovered(q), \"discovered should default false\")\n"
        "  call BJassAssert(not IsQuestRequired(q),   \"required should default false\")\n"
        "  call BJassAssert(IsQuestEnabled(q),        \"enabled should default true\")\n"
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
        "  call BJassAssert(GetPlayerState(Player(0), PLAYER_STATE_GAME_RESULT) == 1, \"defeat result\")\n"
        "  call BJassAssert(GetPlayerSlotState(Player(0)) == PLAYER_SLOT_STATE_LEFT, \"defeated player left\")\n"
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
        "  call BJassAssert(GetPlayerState(Player(0), PLAYER_STATE_GAME_RESULT) == 0, \"victory result\")\n"
        "  call BJassAssert(GetPlayerSlotState(Player(0)) == PLAYER_SLOT_STATE_LEFT, \"victorious player left\")\n"
        "endfunction\n"
    );
    T_ASSERT(ok);
    T_ASSERT( event_in_queue(EVENT_PLAYER_VICTORY));
    T_ASSERT(!event_in_queue(EVENT_PLAYER_DEFEAT));
}

TEST(wc3_jass_map, paused_victory_drains_result_event_and_releases_fallback) {
    T_ASSERT(run_test_jass(
        "globals\n"
        "  boolean victoryFired = false\n"
        "endglobals\n"
        "function onEndCinematic takes nothing returns nothing\n"
        "  call RemovePlayer(Player(0), PLAYER_GAME_RESULT_VICTORY)\n"
        "  call PauseGame(true)\n"
        "endfunction\n"
        "function onVictory takes nothing returns nothing\n"
        "  set victoryFired = true\n"
        "endfunction\n"
        "function verifyVictory takes nothing returns nothing\n"
        "  call BJassAssert(victoryFired, \"victory event stranded behind pause\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger endTrigger = CreateTrigger()\n"
        "  local trigger victoryTrigger = CreateTrigger()\n"
        "  call TriggerRegisterPlayerEvent(endTrigger, Player(0), EVENT_PLAYER_END_CINEMATIC)\n"
        "  call TriggerAddAction(endTrigger, function onEndCinematic)\n"
        "  call TriggerRegisterPlayerEvent(victoryTrigger, Player(0), EVENT_PLAYER_VICTORY)\n"
        "  call TriggerAddAction(victoryTrigger, function onVictory)\n"
        "endfunction\n"
    ));

    game.clients[0].connected = true;
    game.clients[0].ps.client_ui_state = CLIENT_UI_CINEMATIC;
    G_PublishEvent(&g_edicts[0], EVENT_PLAYER_END_CINEMATIC);

    /* This is the normal frame ordering: the first event pass schedules a
     * JASS action, and that action publishes VICTORY too late for the pass. */
    G_RunEvents();
    jass_runevents(level.vm);
    T_ASSERT(level.script_paused);
    T_EQ(game.clients[0].jass.pending_game_result, 1);
    T_ASSERT(level.events.read < game.clients[0].jass.pending_game_result_event);

    G_DrainPausedResultEvents();
    T_EQ(level.events.read, level.events.write);
    jass_callbyname(level.vm, "verifyVictory", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));

    /* Stock CustomVictoryDialogBJ pauses while the map can still be in
     * cinematic UI state. The fallback must not wait for another sim frame. */
    UI_FlushPendingGameResults();
    T_EQ(game.clients[0].jass.pending_game_result, 0);
}


TEST(wc3_jass_map, victory_continue_runs_blizzard_continuation_while_paused) {
    void (*old_menu_action)(LPCSTR, LPCSTR) = gi.MenuAction;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    LPCSTR command[] = { "hidegameresult" };

    T_ASSERT(run_test_jass(
        "function CustomVictoryOkBJ takes nothing returns nothing\n"
        "  call PauseGame(false)\n"
        "  call ChangeLevel(\"Maps\\Campaign\\Human03.w3m\", false)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "endfunction\n"
    ));

    victory_menu_action[0] = '\0';
    victory_menu_arg[0] = '\0';
    gi.MenuAction = capture_victory_menu_action;
    gi.Write = victory_noop_write;
    gi.unicast = victory_noop_unicast;
    level.script_paused = true;
    game.clients[0].ps.stats[PLAYERSTATE_GAME_RESULT] = 0;

    G_ClientCommand(&g_edicts[0], 1, command);

    T_ASSERT(!level.script_paused);
    T_STREQ(victory_menu_action, "map");
    T_STREQ(victory_menu_arg, "Maps\\Campaign\\Human03.w3m");

    gi.MenuAction = old_menu_action;
    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_jass_map, neutral_remove_records_result_without_victory_or_defeat_event) {
    BOOL ok = run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call RemovePlayer(Player(0), PLAYER_GAME_RESULT_NEUTRAL)\n"
        "  call BJassAssert(GetPlayerState(Player(0), PLAYER_STATE_GAME_RESULT) == 3, \"neutral result\")\n"
        "  call BJassAssert(GetPlayerSlotState(Player(0)) == PLAYER_SLOT_STATE_LEFT, \"neutral player left\")\n"
        "endfunction\n"
    );
    T_ASSERT(ok);
    T_ASSERT(!event_in_queue(EVENT_PLAYER_VICTORY));
    T_ASSERT(!event_in_queue(EVENT_PLAYER_DEFEAT));
}

#endif /* BZ_TESTS */
