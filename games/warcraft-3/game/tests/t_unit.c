/*
 * t_unit.c — In-engine unit lifecycle and order tests.
 *
 * Runs inside the real game module via +dedicated 1 +test 'wc3_unit.*'.
 * The real gi, edict pool, and unit data tables are available.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);

/* Forward declarations for functions in m_unit.c without a public header. */
void unit_stand(LPEDICT self);
void unit_birth(LPEDICT self);
void unit_die(LPEDICT self, LPEDICT attacker);
void unit_entercombat(LPEDICT self, LPEDICT target);
void unit_leavecombat(LPEDICT self);
BOOL unit_affectingcombat(LPEDICT self);
BOOL unit_issueorder(LPEDICT self, LPCSTR order, LPCVECTOR2 point);
BOOL unit_issueimmediateorder(LPEDICT self, LPCSTR order);
BOOL unit_additem(LPEDICT edict, LPEDICT item);
BOOL unit_additemtoslot(LPEDICT edict, LPEDICT item, DWORD slot);

/* Reset the entity pool between tests. */
static void reset_test_entities(void) {
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = 0;
    globals.edicts = g_edicts;
}

/* Create a minimal unit edict with lifecycle callbacks wired up. */
static LPEDICT make_unit(FLOAT x, FLOAT y) {
    LPEDICT ent = G_Spawn();
    ent->class_id       = MAKEFOURCC('h','p','e','a');
    ent->s.origin2      = (VECTOR2){x, y};
    ent->s.origin.x     = x;
    ent->s.origin.y     = y;
    ent->s.origin.z     = 0;
    ent->s.model        = 1; /* non-zero so IS_HOLLOW is false */
    ent->movetype       = MOVETYPE_STEP;
    ent->collision      = 16.0f;
    ent->stand          = unit_stand;
    ent->birth          = unit_birth;
    ent->die            = unit_die;
    ent->health.value   = UNIT_HP(ent->class_id);
    ent->health.max_value = UNIT_HP(ent->class_id);
    ent->unitinfo.MoveSpeed = UNIT_SPEED(ent->class_id);
    unit_stand(ent);
    return ent;
}

/* -----------------------------------------------------------------------
 * Birth tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, birth_sets_birth_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_birth(ent);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "birth");
}

TEST(wc3_unit, birth_sets_wait_from_build_time) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_birth(ent);
    T_ASSERT(ent->wait > 0);
    T_EQ((int)ent->wait, UNIT_BUILD_TIME(ent->class_id));
}

TEST(wc3_unit, birth_sets_no_ubersplat_flag) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_birth(ent);
    T_ASSERT(ent->s.renderfx & RF_NO_UBERSPLAT);
}

/* -----------------------------------------------------------------------
 * Stand tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, stand_sets_stand_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_stand(ent);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, stand_uses_ready_animation_in_combat) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    LPEDICT target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    target->s.origin2 = (VECTOR2){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;

    unit_entercombat(ent, target);
    unit_stand(ent);

    T_ASSERT(unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "stand ready");
}

TEST(wc3_unit, stop_exits_ready_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    LPEDICT target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    target->s.origin2 = (VECTOR2){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;

    unit_entercombat(ent, target);
    unit_stand(ent);
    T_STREQ(ent->currentmove->animation, "stand ready");

    unit_issueimmediateorder(ent, "stop");

    T_ASSERT(!unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, stand_clears_build_pointer) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    ent->build = ent;
    unit_stand(ent);
    T_NULL(ent->build);
}

TEST(wc3_unit, stand_clears_no_ubersplat_flag) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    ent->s.renderfx |= RF_NO_UBERSPLAT;
    unit_stand(ent);
    T_ASSERT(!(ent->s.renderfx & RF_NO_UBERSPLAT));
}

/* -----------------------------------------------------------------------
 * Die tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, die_sets_death_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_die(ent, NULL);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, die_raises_dead_monster_flag) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    unit_die(ent, NULL);
    T_ASSERT(ent->svflags & SVF_DEADMONSTER);
}

TEST(wc3_unit, die_publishes_death_event) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    memset(level.events.queue, 0, sizeof(level.events.queue));
    level.events.handlers = NULL;

    unit_die(ent, NULL);
    BOOL found = false;
    for (int i = 0; i < MAX_EVENT_QUEUE; i++) {
        if (level.events.queue[i].type == EVENT_UNIT_DEATH) {
            found = true;
            break;
        }
    }
    T_ASSERT(found);
}

/* -----------------------------------------------------------------------
 * Order tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, issueorder_move_creates_waypoint) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    BOOL result = unit_issueorder(ent, "move", &dest);
    T_ASSERT(result);
    T_NOT_NULL(ent->goalentity);
}

TEST(wc3_unit, issueorder_move_sets_walk_animation) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "walk");
}

TEST(wc3_unit, issueorder_move_preserves_combat_state) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    LPEDICT target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    target->s.origin2 = (VECTOR2){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;
    VECTOR2 dest = {100.0f, 0.0f};

    unit_entercombat(ent, target);
    unit_issueorder(ent, "move", &dest);

    T_ASSERT(unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "walk");
}

TEST(wc3_unit, issueorder_unknown_returns_false) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    BOOL result = unit_issueorder(ent, "patrol", &dest);
    T_ASSERT(!result);
}

TEST(wc3_unit, issueorder_null_inputs_return_false) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};

    T_ASSERT(!unit_issueorder(NULL, "move", &dest));
    T_ASSERT(!unit_issueorder(ent, NULL, &dest));
    T_ASSERT(!unit_issueorder(ent, "move", NULL));
}

TEST(wc3_unit, issueimmediateorder_stop) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    VECTOR2 dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);
    T_STREQ(ent->currentmove->animation, "walk");

    BOOL result = unit_issueimmediateorder(ent, "stop");
    T_ASSERT(result);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, issueimmediateorder_unknown_returns_false) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    BOOL result = unit_issueimmediateorder(ent, "patrol");
    T_ASSERT(!result);
}

TEST(wc3_unit, issueimmediateorder_null_inputs_return_false) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);

    T_ASSERT(!unit_issueimmediateorder(NULL, "stop"));
    T_ASSERT(!unit_issueimmediateorder(ent, NULL));
}

/* -----------------------------------------------------------------------
 * Inventory tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, additemtoslot_fills_empty_slot) {
    reset_test_entities();
    LPEDICT ent  = make_unit(0, 0);
    LPEDICT item = G_Spawn();
    item->class_id = MAKEFOURCC('r','a','t','f');
    item->inuse = true;
    BOOL ok = unit_additemtoslot(ent, item, 0);
    T_ASSERT(ok);
    T_ASSERT(ent->inventory[0] == item);
}

TEST(wc3_unit, additemtoslot_rejects_occupied_slot) {
    reset_test_entities();
    LPEDICT ent   = make_unit(0, 0);
    LPEDICT item1 = G_Spawn();
    item1->class_id = MAKEFOURCC('r','a','t','f');
    item1->inuse = true;
    LPEDICT item2 = G_Spawn();
    item2->class_id = MAKEFOURCC('r','a','t','f');
    item2->inuse = true;
    unit_additemtoslot(ent, item1, 0);
    BOOL ok = unit_additemtoslot(ent, item2, 0);
    T_ASSERT(!ok);
}

TEST(wc3_unit, additem_fills_first_free_slot) {
    reset_test_entities();
    LPEDICT ent   = make_unit(0, 0);
    LPEDICT item1 = G_Spawn();
    item1->class_id = MAKEFOURCC('r','a','t','f');
    item1->inuse = true;
    LPEDICT item2 = G_Spawn();
    item2->class_id = MAKEFOURCC('r','d','e','2');
    item2->inuse = true;
    unit_additemtoslot(ent, item1, 0);
    BOOL ok = unit_additem(ent, item2);
    T_ASSERT(ok);
    T_ASSERT(ent->inventory[1] == item2);
}

TEST(wc3_unit, additem_fails_when_inventory_full) {
    reset_test_entities();
    LPEDICT ent = make_unit(0, 0);
    for (int i = 0; i < MAX_INVENTORY; i++) {
        LPEDICT item = G_Spawn();
        item->class_id = MAKEFOURCC('r','a','t','f');
        item->inuse = true;
        unit_additemtoslot(ent, item, i);
    }
    LPEDICT extra = G_Spawn();
    extra->class_id = MAKEFOURCC('r','d','e','2');
    extra->inuse = true;
    BOOL ok = unit_additem(ent, extra);
    T_ASSERT(!ok);
}

#endif /* BZ_TESTS */
