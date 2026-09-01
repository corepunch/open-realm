#ifdef BZ_TESTS
/* Persistent Hero/idle-worker shortcut state tests. */
#include "test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);

TEST(wc3_shortcuts, peasant_plain_stand_is_idle_but_busy_move_is_not) {
    umove_t busy = { "walk", NULL, NULL, NULL };
    LPEDICT worker;

    reset_entities();
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;
    worker->stand = unit_stand;
    unit_stand(worker);

    T_ASSERT(G_ActorHasSkill(worker, "Ahar"));
    T_ASSERT(G_UnitIsIdleWorker(worker));
    unit_setmove(worker, &busy);
    T_ASSERT(!G_UnitIsIdleWorker(worker));
    unit_stand(worker);
    T_ASSERT(G_UnitIsIdleWorker(worker));
}

TEST(wc3_shortcuts, hold_position_worker_is_not_idle) {
    LPEDICT worker;

    reset_entities();
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;
    worker->stand = unit_stand;
    worker->movement.holding_position = true;
    unit_stand(worker);

    T_ASSERT(!G_UnitIsIdleWorker(worker));
}

TEST(wc3_shortcuts, controlled_unit_invalidation_marks_player_dirty) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;
    client->shortcuts.dirty = false;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;

    G_InvalidateUnitShortcutsForUnit(worker);
    T_ASSERT(client->shortcuts.dirty);
}
#endif
