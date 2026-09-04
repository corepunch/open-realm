/*
 * t_utils.c — Shared helpers for in-engine WC3 tests.
 *
 * Compiled once into the game module alongside the t_*.c files.
 * Provides alloc_test_unit(), reset_entities(), setup_test_world(),
 * and run_test_jass() for JASS integration tests.
 */
#ifdef BZ_TESTS

#include "../g_local.h"
#include "jass/jass.h"

extern JASSMODULE jass_funcs[];

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y) {
    LPEDICT ent = G_Spawn();
    ent->class_id = class_id;
    G_BindEntityData(ent);
    ent->s.origin2 = (VECTOR2){x, y};
    ent->s.origin.x = x;
    ent->s.origin.y = y;
    ent->s.origin.z = 0;
    ent->bounds.min.x = x - 16;
    ent->bounds.min.y = y - 16;
    ent->bounds.max.x = x + 16;
    ent->bounds.max.y = y + 16;
    /* Match SP_SpawnUnit's liveness contract for real unit rows.  Order and
     * selection code uses health <= 0 as the authoritative dead predicate, so
     * a generic test unit must not silently start life as a corpse. */
    ent->health.max_value = MAX(ent->data.UnitBalance->maxHealth, 1.0f);
    ent->health.value = ent->health.max_value;
    return ent;
}

void reset_entities(void) {
    G_JassSoundRuntimeReset();
    FOR_LOOP(i, globals.max_edicts) G_FreeActorSkills(g_edicts + i);
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = game.max_clients;
    globals.edicts = g_edicts;
    FOR_LOOP(i, game.max_clients) g_edicts[i].s.number = i;
    gi.ClearWorld();
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
static void test_set_paused(BOOL paused) { (void)paused; }

/* Pathmap tests need an explicit world-space transform; production maps normally provide it via war3map.w3e. */
void setup_test_pathmap(DWORD width, DWORD height, BYTE const *cells) {
    CM_SetupTestPathmap(width, height, cells);
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {0, 0}, .max = {(FLOAT)width, (FLOAT)height}));
}

void setup_test_world(void) {
    G_ClearGroundSurfaces();
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
	gi.ClearWorld();

}

/* Every in-engine WC3 test starts from the state contract the old standalone harness provided. */
static void reset_test_state(void) {
    UI_TestResetInfoPanelIconCache();
    G_JassSoundRuntimeReset();
    G_BotShutdown();
    if (level.vm) { jass_close(level.vm); }
    G_FowShutdown();
    memset(g_edicts, 0, sizeof(edict_t) * globals.max_edicts);
    globals.num_edicts = game.max_clients;
    globals.edicts = g_edicts;
    /* Restore player-slot client pointers so G_GetPlayerEntityByNumber works. */
    FOR_LOOP(i, game.max_clients) g_edicts[i].s.number = i;
    memset(game.clients, 0, game.max_clients * sizeof(*game.clients));
    game.constants.dawnTimeGameHours = 6.0f;
    game.constants.duskTimeGameHours = 18.0f;
    game.constants.gameDayHours = 24.0f;
    game.constants.gameDayLength = 480.0f;
    game.constants.foodCeiling = 100;
    game.constants.upkeepUsageCount = 2;
    game.constants.upkeepGoldTaxCount = 3;
    game.constants.upkeepLumberTaxCount = 3;
    game.constants.upkeepUsage[0] = 50.0f;
    game.constants.upkeepUsage[1] = 80.0f;
    game.constants.upkeepGoldTax[0] = 0.0f;
    game.constants.upkeepGoldTax[1] = 0.30f;
    game.constants.upkeepGoldTax[2] = 0.60f;
    game.constants.upkeepLumberTax[0] = 0.0f;
    game.constants.upkeepLumberTax[1] = 0.0f;
    game.constants.upkeepLumberTax[2] = 0.0f;
    FOR_LOOP(i, game.max_clients) {
        game.clients[i].ps.number = i;
        game.clients[i].ps.stats[PLAYERSTATE_FOOD_CAP_CEILING] = 100;
        game.clients[i].ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 100;
        game.clients[i].ps.stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = 100;
        g_edicts[i].client = &game.clients[i];
    }
    memset(&level, 0, sizeof(level));
    strlcpy(level.map_path, "Maps\\Campaign\\SaveTest.w3m", sizeof(level.map_path));
    memset(&test_mapinfo, 0, sizeof(test_mapinfo));
    level.mapinfo = &test_mapinfo;
    gi.GetTime = test_get_time;
    gi.SetPaused = test_set_paused;
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {0, 0}, .max = {512, 384}));
    gi.ClearWorld();
}

static void ignore_jass_error(LPCSTR message) { (void)message; }

/*
 * run_test_jass — load a synthetic JASS map script and run its main().
 *
 * Initializes a fresh JASS VM, loads Scripts\common.j and Scripts\Blizzard.j
 * from the test fixture MPQ, evaluates the given source, calls main(), and
 * pumps all coroutines.  The VM is stored in level.vm so callers can inspect
 * C-level game state (level.quests, level.events) after the call returns.
 * The VM is closed automatically by reset_test_state() before the next test.
 *
 * Returns true if no JASS runtime error occurred. The test host captures VM
 * errors so callers can distinguish expected failures from test failures.
 */
static BOOL run_test_jass_impl(LPCSTR src, LPCSTR expected) {
    /* jass_dobuffer mutates the string in-place; duplicate to avoid clobbering read-only literals. */
    DWORD len = strlen(src);
    LPSTR buf = gi.MemAlloc(len + 1);
    memcpy(buf, src, len + 1);

    if (level.vm) { jass_close(level.vm); level.vm = NULL; }

    jass_sethost(&MAKE(JASSHOST,
        .MemAlloc         = gi.MemAlloc,
        .MemFree          = gi.MemFree,
        .GetTime          = gi.GetTime,
        .ReadFile         = gi.ReadFile,
        .natives          = jass_funcs,
        .GetPlayerByNumber = G_GetPlayerByNumber,
        .RuntimeError     = ignore_jass_error,
        .SaveHandle       = G_SaveJassHandle,
        .LoadHandle       = G_LoadJassHandle,
    ));
    level.vm = jass_newstate();

    jass_dofile(level.vm, "Scripts\\common.j");
    jass_dofile(level.vm, "Scripts\\Blizzard.j");
    jass_dobuffer(level.vm, buf);
    gi.MemFree(buf);

    jass_callbyname(level.vm, "main", true);
    jass_runevents(level.vm);

    if (expected)
        return jass_rterror_pending(level.vm) && !strcmp(jass_rterror_message(level.vm), expected);
    if (!jass_rterror_pending(level.vm)) return true;
    fprintf(stderr, "JASS test error: %s\n", jass_rterror_message(level.vm));
    return false;
}

BOOL run_test_jass(LPCSTR src) { return run_test_jass_impl(src, NULL); }
BOOL run_test_jass_error(LPCSTR src, LPCSTR expected) { return run_test_jass_impl(src, expected); }

__attribute__((constructor)) static void register_test_reset(void) { Test_SetBeforeEach(reset_test_state); }


#endif /* BZ_TESTS */
