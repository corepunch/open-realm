/*
 * t_utils.c — Shared helpers for in-engine WC3 tests.
 *
 * Compiled once into the game module alongside the t_*.c files.
 * Provides alloc_test_unit(), reset_entities(), and setup_test_world().
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
    ent->bounds.min.x = x - 16;
    ent->bounds.min.y = y - 16;
    ent->bounds.max.x = x + 16;
    ent->bounds.max.y = y + 16;
    return ent;
}

void reset_entities(void) {
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = 0;
    globals.edicts = g_edicts;
}

/* CM_SetupTestPathmap is in routing.c, only compiled for test builds. */
void CM_SetupTestPathmap(DWORD width, DWORD height, BYTE const *cells);

/*
 * Minimal test world: an all-walkable pathmap covering coords up to 2048×2048
 * (64×64 cells at TILE_SIZE=32), and a valid MAPINFO so unit-data lookups,
 * area queries, and fog-of-war code don't crash on NULL pointers.
 */
#define TEST_PATHMAP_CELLS 64
static BYTE test_pathmap_cells[TEST_PATHMAP_CELLS * TEST_PATHMAP_CELLS];
static MAPINFO test_mapinfo;

static BOOL test_world_ready;

void setup_test_world(void) {
    if (test_world_ready) return;

    memset(&test_mapinfo, 0, sizeof(test_mapinfo));
    level.mapinfo = &test_mapinfo;

    memset(test_pathmap_cells, 0, sizeof(test_pathmap_cells));
    CM_SetupTestPathmap(TEST_PATHMAP_CELLS, TEST_PATHMAP_CELLS, test_pathmap_cells);

    test_world_ready = true;
}

/* Run before any TEST() — other constructors depend on level.mapinfo being valid. */


#endif /* BZ_TESTS */
