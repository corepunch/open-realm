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
    globals.num_edicts = game.max_clients;
    globals.edicts = g_edicts;
    FOR_LOOP(i, game.max_clients) g_edicts[i].s.number = i;
    if (gi.ClearWorld) gi.ClearWorld();
}

/* CM_SetupTestPathmap is in routing.c, only compiled for test builds. */
void CM_SetupTestPathmap(DWORD width, DWORD height, BYTE const *cells);
void CM_SetupTestWorldBounds(LPCBOX2 bounds);

/*
 * Minimal test world: an all-walkable pathmap covering coords up to 2048×2048
 * (64×64 cells at TILE_SIZE=32), and a valid MAPINFO so unit-data lookups,
 * area queries, and fog-of-war code don't crash on NULL pointers.
 */
#define TEST_PATHMAP_CELLS 64
static BYTE test_pathmap_cells[TEST_PATHMAP_CELLS * TEST_PATHMAP_CELLS];
static MAPINFO test_mapinfo;
static WAR3MAP test_worldmap;
static WAR3MAPVERTEX test_vertices[(TEST_PATHMAP_CELLS + 1) * (TEST_PATHMAP_CELLS + 1)];

static DWORD test_get_time(void) { return level.time; }

/* Pathmap tests need an explicit world-space transform; production maps normally provide it via war3map.w3e. */
void setup_test_pathmap(DWORD width, DWORD height, BYTE const *cells) {
    CM_SetupTestPathmap(width, height, cells);
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {0, 0}, .max = {(FLOAT)width, (FLOAT)height}));
}

void setup_test_world(void) {
	memset(&test_mapinfo, 0, sizeof(test_mapinfo));
	level.mapinfo = &test_mapinfo;

	memset(&test_worldmap, 0, sizeof(test_worldmap));
	test_worldmap.width = TEST_PATHMAP_CELLS;
	test_worldmap.height = TEST_PATHMAP_CELLS;
	memset(test_vertices, 0, sizeof(test_vertices));
	for (int i = 0; i < (int)(sizeof(test_vertices) / sizeof(test_vertices[0])); i++)
		test_vertices[i].accurate_height = 0x2000;
	test_worldmap.vertices = test_vertices;
	world.map = &test_worldmap;

	memset(test_pathmap_cells, 0, sizeof(test_pathmap_cells));
	CM_SetupTestPathmap(TEST_PATHMAP_CELLS, TEST_PATHMAP_CELLS, test_pathmap_cells);
	CM_SetupTestWorldBounds(&MAKE(BOX2,
		.min = {-TEST_PATHMAP_CELLS * 16.0f, -TEST_PATHMAP_CELLS * 16.0f},
		.max = { TEST_PATHMAP_CELLS * 16.0f,  TEST_PATHMAP_CELLS * 16.0f}));

	/* Rebuild the area-node tree so spatial queries don't chase dangling entity
	 * links left over from previous tests. */
	if (gi.ClearWorld) gi.ClearWorld();

}

/* Every in-engine WC3 test starts from the state contract the old standalone harness provided. */
static void reset_test_state(void) {
    G_FowShutdown();
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = game.max_clients;
    globals.edicts = g_edicts;
    FOR_LOOP(i, game.max_clients) g_edicts[i].s.number = i;
    memset(game.clients, 0, game.max_clients * sizeof(*game.clients));
    FOR_LOOP(i, game.max_clients) game.clients[i].ps.number = i;
    memset(&level, 0, sizeof(level));
    memset(&test_mapinfo, 0, sizeof(test_mapinfo));
    level.mapinfo = &test_mapinfo;
    gi.GetTime = test_get_time;
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {0, 0}, .max = {512, 384}));
    if (gi.ClearWorld) gi.ClearWorld();
}

__attribute__((constructor)) static void register_test_reset(void) { Test_SetBeforeEach(reset_test_state); }


#endif /* BZ_TESTS */
