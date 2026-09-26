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

extern jassModule_t jass_funcs[];

void test_sound_event(edict_t * ent, uint32_t request, uint32_t event) {
    edict_t * player = g_edicts;
    player->client = game.clients;
    player->client->connected = true;
    player->client->ps.number = ent->s.player;
    char user[16], token[16], kind[16];
    snprintf(user, sizeof(user), "%u", ent->s.number);
    snprintf(token, sizeof(token), "%u", request);
    snprintf(kind, sizeof(kind), "%u", event);
    cstring_t args[] = {"sound_event", user, token, kind};
    G_ClientCommand(player, 4, args);
}

edict_t * alloc_test_unit(uint32_t class_id, float x, float y) {
    static UnitWeapons_t const test_weapons = { .attacksEnabled = 3 };
    edict_t * ent = G_Spawn();
    ent->class_id = class_id;
    G_BindEntityData(ent);
    /* The fixture archive has no UnitWeapons.slk. Tests that construct attacks
     * by hand start with both authored weapon slots enabled unless they attach
     * a specific row for a disabled-slot case. */
    ent->data.UnitWeapons = &test_weapons;
    ent->s.origin2 = (vector2_t){x, y};
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
    uint32_t cap = globals.max_edicts;
    G_ResetDeferredFrees();
    G_ResetHeroPassiveCaches();
    G_ResetSelectionSoundState();
    G_ResetSoundPresentationState();
    /* Wipe only the live cap, then restore MAX_ENTITIES. Pocket Factory's alloc-failure
     * test shrinks max_edicts to num_edicts+1 (26 in the full suite); walking 16000
     * edicts first would G_FreeActorSkills stale high slots. */
    if (cap > MAX_ENTITIES) cap = MAX_ENTITIES;
    FOR_LOOP(i, cap) G_FreeActorSkills(g_edicts + i);
    memset(g_edicts, 0, sizeof(edict_t) * cap);
    globals.max_edicts = MAX_ENTITIES;
    globals.num_edicts = game.max_clients;
    globals.edicts = g_edicts;
    FOR_LOOP(i, game.max_clients) g_edicts[i].s.number = i;
    gi.ClearWorld();
}

/* CM_SetupTestPathmap is in routing.c, only compiled for test builds. */
void CM_SetupTestPathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void CM_SetupTestWorldBounds(box2_t const * bounds);

/*
 * Minimal test world: an all-walkable pathmap covering coords up to 2048×2048
 * (64×64 cells at TILE_SIZE=32), and a valid MAPINFO so unit-data lookups,
 * area queries, and fog-of-war code don't crash on NULL pointers.
 */
#define TEST_PATHMAP_CELLS 64
static uint8_t test_pathmap_cells[TEST_PATHMAP_CELLS * TEST_PATHMAP_CELLS];
static mapInfo_t test_mapinfo;
static war3map_t test_worldmap;
static war3mapVertex_t test_vertices[(TEST_PATHMAP_CELLS + 1) * (TEST_PATHMAP_CELLS + 1)];

static uint32_t test_get_time(void) { return level.time; }
static void test_set_paused(bool paused) { (void)paused; }

/* Pathmap tests need an explicit world-space transform; production maps normally provide it via war3map.w3e. */
void setup_test_pathmap(uint32_t width, uint32_t height, uint8_t const *cells) {
    CM_SetupTestPathmap(width, height, cells);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {0, 0}, .max = {(float)width, (float)height}));
}

void setup_test_world(void) {
    G_ClearGroundSurfaces();
	memset(&test_mapinfo, 0, sizeof(test_mapinfo));
	level.mapinfo = &test_mapinfo;
	G_InitPlayerAlliances(level.mapinfo);

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
	CM_SetupTestWorldBounds(&MAKE(box2_t,
		.min = {-TEST_PATHMAP_CELLS * 16.0f, -TEST_PATHMAP_CELLS * 16.0f},
		.max = { TEST_PATHMAP_CELLS * 16.0f,  TEST_PATHMAP_CELLS * 16.0f}));
	G_BlightInit();

	/* Rebuild the area-node tree so spatial queries don't chase dangling entity
	 * links left over from previous tests. */
	gi.ClearWorld();

}

/* Every in-engine WC3 test starts from the state contract the old standalone harness provided. */
static void reset_test_state(void) {
    G_ResetDeferredFrees();
    UI_TestResetInfoPanelIconCache();
    G_ResetSelectionSoundState();
    G_ResetSoundPresentationState();
    G_BotShutdown();
    if (level.vm) { jass_close(level.vm); level.vm = NULL; }
    G_FowShutdown();
    G_BlightShutdown();
    globals.max_edicts = MAX_ENTITIES;
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
    G_ClearJassGroupRegistry();
    G_ClearRegionRegistry();
    G_ClearHashtableRegistry();
    memset(&level, 0, sizeof(level));
    FOR_LOOP(i, MAX_PLAYERS) level.player_leaderboards[i] = -1;
    strlcpy(level.map_path, "Maps\\Campaign\\SaveTest.w3m", sizeof(level.map_path));
    memset(&test_mapinfo, 0, sizeof(test_mapinfo));
    level.mapinfo = &test_mapinfo;
    G_InitPlayerAlliances(level.mapinfo);
    gi.GetTime = test_get_time;
    gi.SetPaused = test_set_paused;
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {0, 0}, .max = {512, 384}));
    gi.ClearWorld();
}

static void ignore_jass_error(cstring_t message) { (void)message; }

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
static bool run_test_jass_impl(cstring_t src, cstring_t expected) {
    /* jass_dobuffer mutates the string in-place; duplicate to avoid clobbering read-only literals. */
    uint32_t len = strlen(src);
    string_t buf = gi.MemAlloc(len + 1);
    memcpy(buf, src, len + 1);

    if (level.vm) { jass_close(level.vm); level.vm = NULL; }

    jass_sethost(&MAKE(jassHost_t,
        .MemAlloc         = gi.MemAlloc,
        .MemFree          = gi.MemFree,
        .GetTime          = gi.GetTime,
        .ReadFile         = gi.ReadFile,
        .natives          = jass_funcs,
        .GetPlayerByNumber = G_GetPlayerByNumber,
        .TimerCoroutineValid = G_TimerCoroutineValid,
        .RuntimeError     = ignore_jass_error,
        .SaveHandle       = G_SaveJassHandle,
        .LoadHandle       = G_LoadJassHandle,
        .VariableChanged  = G_JassVariableChanged,
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

bool run_test_jass(cstring_t src) { return run_test_jass_impl(src, NULL); }
bool run_test_jass_error(cstring_t src, cstring_t expected) { return run_test_jass_impl(src, expected); }

__attribute__((constructor)) static void register_test_reset(void) { Test_SetBeforeEach(reset_test_state); }


#endif /* BZ_TESTS */
