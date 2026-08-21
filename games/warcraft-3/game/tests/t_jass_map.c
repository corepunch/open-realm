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
 * Scripts load Scripts\common.j from tests/wc3-engine-data/ — no War3.mpq.
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
