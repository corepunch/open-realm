/*
 * t_utils.c — Shared helpers for in-engine WC3 tests.
 *
 * Compiled once into the game module alongside the t_*.c files.
 * Provides alloc_test_unit() and reset_entities() used by all test suites.
 */
#ifdef BZ_TESTS

#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y) {
    LPEDICT ent = G_Spawn();
    ent->class_id = class_id;
    ent->s.origin2 = (VECTOR2){x, y};
    ent->s.origin.x = x;
    ent->s.origin.y = y;
    ent->s.origin.z = 0;
    return ent;
}

void reset_entities(void) {
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = 0;
    globals.edicts = g_edicts;
}

#endif /* BZ_TESTS */
