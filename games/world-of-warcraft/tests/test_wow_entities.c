#include "test_framework.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "game/g_wow_local.h"

int _tests_run = 0;
int _tests_failed = 0;

typedef struct {
    char name[MAX_PATHLEN];
    int index;
} testModel_t;

static testModel_t test_models[64];
static testModel_t test_images[32];
static DWORD test_num_models;
static DWORD test_num_images;
static BYTE test_multicast_buf[MAX_MSGLEN];
static DWORD test_multicast_size;
static char test_last_error[512];

#define TEST_CONFIGSTRINGS 128
static char test_configstrings[TEST_CONFIGSTRINGS][512];

static void test_configstring(DWORD index, LPCSTR string) {
    if (index < TEST_CONFIGSTRINGS) {
        strncpy(test_configstrings[index], string ? string : "", sizeof(test_configstrings[index]) - 1);
        test_configstrings[index][sizeof(test_configstrings[index]) - 1] = '\0';
    }
}
static LPCSTR test_get_configstring(DWORD index) {
    return index < TEST_CONFIGSTRINGS ? test_configstrings[index] : "";
}
static LPCSTR test_cvar_string(LPCSTR name, LPCSTR fallback) {
    (void)name;
    return fallback ? fallback : "";
}

static animation_t test_animations[] = {
    { .name = "Stand",        .interval = { 0, 1000 } },
    { .name = "Walk",         .interval = { 0, 1000 } },
    { .name = "Run",          .interval = { 0, 1000 } },
    { .name = "Ready1H",      .interval = { 0, 1000 } },
    { .name = "Attack1H",     .interval = { 0, 1000 } },
    { .name = "Pain",         .interval = { 0,  450 } },
    { .name = "Death",        .interval = { 0, 1200 } },
    { .name = "Dead",         .interval = { 0, 1200 } },
};

static void put32(LPBYTE out, DWORD value) {
    out[0] = (BYTE)(value & 0xff);
    out[1] = (BYTE)((value >> 8) & 0xff);
    out[2] = (BYTE)((value >> 16) & 0xff);
    out[3] = (BYTE)((value >> 24) & 0xff);
}
static void putfield(LPBYTE record, DWORD field, DWORD value) {
    put32(record + field * sizeof(DWORD), value);
}
static void putfield_float(LPBYTE record, DWORD field, FLOAT value) {
    memcpy(record + field * sizeof(DWORD), &value, sizeof(value));
}

static HANDLE alloc_dbc(DWORD records, DWORD fields, DWORD string_size, LPDWORD size_out) {
    DWORD record_size = fields * sizeof(DWORD);
    DWORD size = 20 + records * record_size + string_size;
    LPBYTE data = calloc(1, size);
    memcpy(data, "WDBC", 4);
    put32(data + 4, records);
    put32(data + 8, fields);
    put32(data + 12, record_size);
    put32(data + 16, string_size);
    *size_out = size;
    return data;
}
static DWORD add_string(LPBYTE strings, DWORD *cursor, LPCSTR value) {
    DWORD offset = *cursor, len = (DWORD)strlen(value) + 1;
    memcpy(strings + offset, value, len);
    *cursor += len;
    return offset;
}

static BOOL path_eq(LPCSTR a, LPCSTR b) {
    while (*a && *b) {
        char ca = *a == '/' ? '\\' : *a, cb = *b == '/' ? '\\' : *b;
        if (tolower((unsigned char)ca) != tolower((unsigned char)cb)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static HANDLE make_map_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(1, 5, 64, &size);
    LPBYTE record = data + 20, strings = record + 5 * sizeof(DWORD);
    DWORD cursor = 1;
    putfield(record, 0, 1);
    putfield(record, 1, add_string(strings, &cursor, "Azeroth"));
    putfield(record, 3, add_string(strings, &cursor, "Elwynn Test"));
    putfield(record, 4, 42);
    *size_out = size;
    return data;
}
static HANDLE make_world_safe_locs_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(1, 6, 64, &size);
    LPBYTE record = data + 20, strings = record + 6 * sizeof(DWORD);
    DWORD cursor = 1;
    putfield(record, 0, 100);
    putfield(record, 1, 1);
    putfield_float(record, 2, 123.25f);
    putfield_float(record, 3, -456.5f);
    putfield_float(record, 4, 78.0f);
    putfield(record, 5, add_string(strings, &cursor, "Northshire"));
    *size_out = size;
    return data;
}
static HANDLE make_creature_display_info_dbc(LPDWORD size_out) {
    DWORD displays[] = { 161, 193, 163, 188 };
    DWORD size;
    LPBYTE data = alloc_dbc(4, 5, 1, &size);
    FOR_LOOP(i, 4) {
        LPBYTE record = data + 20 + i * 5 * sizeof(DWORD);
        putfield(record, 0, displays[i]);
        putfield(record, 1, 700 + i);
        putfield_float(record, 4, 1.0f);
    }
    *size_out = size;
    return data;
}
static HANDLE make_creature_model_data_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(4, 15, 160, &size);
    LPBYTE records = data + 20, strings = records + 4 * 15 * sizeof(DWORD);
    DWORD cursor = 1;
    FOR_LOOP(i, 4) {
        char name[64];
        snprintf(name, sizeof(name), "Creature\\Test\\Creature%u.m2", (unsigned)i);
        LPBYTE record = records + i * 15 * sizeof(DWORD);
        putfield(record, 0, 700 + i);
        putfield(record, 2, add_string(strings, &cursor, name));
        putfield_float(record, 4, 1.0f);
        putfield_float(record, 14, 3.0f);
    }
    *size_out = size;
    return data;
}

/* ---- game_import stubs ---- */
static HANDLE test_read_file(LPCSTR filename, LPDWORD size) {
    if (path_eq(filename, "DBFilesClient\\Map.dbc")) return make_map_dbc(size);
    if (path_eq(filename, "DBFilesClient\\WorldSafeLocs.dbc")) return make_world_safe_locs_dbc(size);
    if (path_eq(filename, "DBFilesClient\\CreatureDisplayInfo.dbc")) return make_creature_display_info_dbc(size);
    if (path_eq(filename, "DBFilesClient\\CreatureModelData.dbc")) return make_creature_model_data_dbc(size);
    if (size) *size = 0;
    return NULL;
}
static HANDLE test_mem_alloc(long n) { return calloc(1, (size_t)n); }
static void test_mem_free(HANDLE m) { free(m); }
static int test_model_index(LPCSTR name) {
    FOR_LOOP(i, test_num_models)
        if (!strcasecmp(test_models[i].name, name)) return test_models[i].index;
    ASSERT(test_num_models < sizeof(test_models) / sizeof(test_models[0]));
    strncpy(test_models[test_num_models].name, name, sizeof(test_models[0].name) - 1);
    test_models[test_num_models].index = (int)test_num_models + 1;
    test_num_models++;
    return (int)test_num_models;
}
static int test_image_index(LPCSTR name) {
    FOR_LOOP(i, test_num_images)
        if (!strcasecmp(test_images[i].name, name)) return test_images[i].index;
    ASSERT(test_num_images < sizeof(test_images) / sizeof(test_images[0]));
    strncpy(test_images[test_num_images].name, name, sizeof(test_images[0].name) - 1);
    test_images[test_num_images].index = (int)test_num_images + 1;
    test_num_images++;
    return (int)test_num_images;
}
static void test_error(LPCSTR fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(test_last_error, sizeof(test_last_error), fmt, args);
    va_end(args);
}
void UI_WriteWowHud(LPEDICT ent) { (void)ent; }

static struct game_import test_import(void) {
    struct game_import import;
    memset(&import, 0, sizeof(import));
    import.MemAlloc = test_mem_alloc;
    import.MemFree = test_mem_free;
    import.ModelIndex = test_model_index;
    import.ImageIndex = test_image_index;
    import.ReadFile = test_read_file;
    import.configstring = test_configstring;
    import.GetConfigstring = test_get_configstring;
    import.CvarString = test_cvar_string;
    import.error = test_error;
    return import;
}

int G_RegisterModel(LPCSTR filename) { return gi.ModelIndex(filename); }
LPCANIMATION G_GetAnimation(DWORD idx, LPCSTR name) {
    (void)idx;
    FOR_LOOP(i, sizeof(test_animations) / sizeof(test_animations[0]))
        if (!strcasecmp(test_animations[i].name, name)) return &test_animations[i];
    return NULL;
}
void G_FreeModels(void) {}
FLOAT G_GetAttachmentZ(DWORD idx, int aid) { (void)idx; (void)aid; return 0.0f; }

static void reset_test_state(void) {
    memset(test_models, 0, sizeof(test_models));
    memset(test_images, 0, sizeof(test_images));
    test_num_models = 0; test_num_images = 0;
    memset(test_multicast_buf, 0, sizeof(test_multicast_buf));
    test_multicast_size = 0;
    memset(test_last_error, 0, sizeof(test_last_error));
    memset(test_configstrings, 0, sizeof(test_configstrings));
}

static struct game_export *init_game(void) {
    struct game_import import = test_import();
    reset_test_state();
    struct game_export *game = GetGameAPI(&import);
    ASSERT_NOT_NULL(game);
    ASSERT_NOT_NULL(game->Init);
    ASSERT_NOT_NULL(game->LoadMap);
    game->Init();
    return game;
}

static LPEDICT first_of_kind(wowEntityKind_t kind) {
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        wowEntityLocal_t *l = Wow_EntityLocal(&wow_edicts[i]);
        if (wow_edicts[i].inuse && l && l->kind == kind) return &wow_edicts[i];
    }
    return NULL;
}

static DWORD count_of_kind(wowEntityKind_t kind) {
    DWORD count = 0;
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        wowEntityLocal_t *l = Wow_EntityLocal(&wow_edicts[i]);
        if (wow_edicts[i].inuse && l && l->kind == kind) count++;
    }
    return count;
}

/* ===================================================================
 * Corpse tests
 * =================================================================== */

static void test_corpse_spawned_on_creature_death(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame(); /* reset spawn budget */

    LPEDICT creature = first_of_kind(WOW_ENTITY_UNIT);
    ASSERT_NOT_NULL(creature);
    wowEntityLocal_t *cl = Wow_EntityLocal(creature);

    Wow_AIDie(creature, &wow_edicts[0]);
    ASSERT(cl->dead);

    LPEDICT corpse = first_of_kind(WOW_ENTITY_CORPSE);
    ASSERT_NOT_NULL(corpse);
    wowEntityLocal_t *col = Wow_EntityLocal(corpse);
    ASSERT_EQ_INT((int)col->kind, WOW_ENTITY_CORPSE);
    ASSERT_EQ_INT((int)col->corpse_owner, (int)creature->s.number);
    ASSERT(col->corpse_timer > 0);
    ASSERT_EQ_INT((int)corpse->s.model, (int)creature->s.model);
    ASSERT_EQ_FLOAT(corpse->s.origin.x, creature->s.origin.x, 0.001f);
    ASSERT_EQ_FLOAT(corpse->s.origin.y, creature->s.origin.y, 0.001f);
    ASSERT_EQ_INT((int)corpse->s.flags & EF_GROUND_ANCHOR, (int)EF_GROUND_ANCHOR);

    if (game->Shutdown) game->Shutdown();
}

static void test_corpse_decays_over_time(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    LPEDICT creature = first_of_kind(WOW_ENTITY_UNIT);
    ASSERT_NOT_NULL(creature);
    Wow_AIDie(creature, &wow_edicts[0]);

    LPEDICT corpse = first_of_kind(WOW_ENTITY_CORPSE);
    ASSERT_NOT_NULL(corpse);
    wowEntityLocal_t *col = Wow_EntityLocal(corpse);
    DWORD initial = col->corpse_timer;

    for (int i = 0; i < 10; i++) game->RunFrame();
    ASSERT(col->corpse_timer < initial);
    ASSERT(corpse->inuse);

    if (game->Shutdown) game->Shutdown();
}

static void test_corpse_removed_after_timer_expires(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    LPEDICT creature = first_of_kind(WOW_ENTITY_UNIT);
    ASSERT_NOT_NULL(creature);
    Wow_AIDie(creature, &wow_edicts[0]);

    LPEDICT corpse = first_of_kind(WOW_ENTITY_CORPSE);
    ASSERT_NOT_NULL(corpse);
    wowEntityLocal_t *col = Wow_EntityLocal(corpse);
    col->corpse_timer = FRAMETIME;

    game->RunFrame();
    ASSERT(!corpse->inuse);

    if (game->Shutdown) game->Shutdown();
}

static void test_corpse_not_spawned_for_null_entity(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));

    LPEDICT corpse = Wow_SpawnCorpse(NULL);
    ASSERT_NULL(corpse);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * DynamicObject tests
 * =================================================================== */

static void test_dynamic_object_spawn_and_properties(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    VECTOR2 origin = { 100.0f, 200.0f };
    LPEDICT dobj = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, 5000);
    ASSERT_NOT_NULL(dobj);

    wowEntityLocal_t *dl = Wow_EntityLocal(dobj);
    ASSERT_EQ_INT((int)dl->kind, WOW_ENTITY_DYNAMICOBJECT);
    ASSERT_EQ_INT((int)dl->dyn_spell_id, (int)WOW_SPELL_FIREBOLT);
    ASSERT_EQ_INT((int)dl->dyn_duration, 5000);
    ASSERT_EQ_INT((int)dl->dyn_radius, 2);
    ASSERT_EQ_FLOAT(dobj->s.origin2.x, 100.0f, 0.001f);
    ASSERT_EQ_FLOAT(dobj->s.origin2.y, 200.0f, 0.001f);
    ASSERT_EQ_FLOAT(dobj->s.radius, 2.0f, 0.001f);
    ASSERT_EQ_INT((int)dobj->s.flags & EF_GROUND_ANCHOR, (int)EF_GROUND_ANCHOR);

    DWORD initial = dl->dyn_duration;
    for (int i = 0; i < 10; i++) game->RunFrame();
    ASSERT(dl->dyn_duration < initial);

    if (game->Shutdown) game->Shutdown();
}

static void test_dynamic_object_despawns_after_duration(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    VECTOR2 origin = { 0, 0 };
    LPEDICT dobj = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, FRAMETIME);
    ASSERT_NOT_NULL(dobj);

    game->RunFrame();
    ASSERT(!dobj->inuse);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * Entity handler dispatch tests
 * =================================================================== */

static void test_entity_handler_table_complete(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));

    ASSERT_STR_EQ(wow_entity_handlers[WOW_ENTITY_NONE].name, "none");
    ASSERT_STR_EQ(wow_entity_handlers[WOW_ENTITY_PLAYER].name, "player");
    ASSERT_STR_EQ(wow_entity_handlers[WOW_ENTITY_UNIT].name, "unit");
    ASSERT_STR_EQ(wow_entity_handlers[WOW_ENTITY_GAMEOBJECT].name, "gameobject");
    ASSERT_STR_EQ(wow_entity_handlers[WOW_ENTITY_CORPSE].name, "corpse");
    ASSERT_STR_EQ(wow_entity_handlers[WOW_ENTITY_PROJECTILE].name, "projectile");
    ASSERT_STR_EQ(wow_entity_handlers[WOW_ENTITY_DYNAMICOBJECT].name, "dynamicobject");

    ASSERT_NOT_NULL(wow_entity_handlers[WOW_ENTITY_UNIT].think);
    ASSERT_NOT_NULL(wow_entity_handlers[WOW_ENTITY_PROJECTILE].think);
    ASSERT_NOT_NULL(wow_entity_handlers[WOW_ENTITY_CORPSE].think);
    ASSERT_NOT_NULL(wow_entity_handlers[WOW_ENTITY_DYNAMICOBJECT].think);
    ASSERT_NULL(wow_entity_handlers[WOW_ENTITY_PLAYER].think);

    if (game->Shutdown) game->Shutdown();
}

static void test_backward_compat_creature_equals_unit(void) {
    ASSERT_EQ_INT((int)WOW_ENTITY_CREATURE, (int)WOW_ENTITY_UNIT);

    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    LPEDICT creature = first_of_kind(WOW_ENTITY_CREATURE);
    ASSERT_NOT_NULL(creature);
    ASSERT_EQ_INT((int)Wow_EntityLocal(creature)->kind, (int)WOW_ENTITY_UNIT);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * Creature info cache tests
 * =================================================================== */

static void test_creature_info_cache_known_displays(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));

    ASSERT_STR_EQ(Wow_CachedCreatureName(161), "Wolf");
    ASSERT_STR_EQ(Wow_CachedCreatureName(193), "Boar");
    ASSERT_STR_EQ(Wow_CachedCreatureName(163), "Kobold");
    ASSERT_STR_EQ(Wow_CachedCreatureName(188), "Murloc");
    ASSERT_STR_EQ(Wow_CachedCreatureName(99999), "Unknown");

    ASSERT_EQ_INT((int)Wow_CachedCreatureType(161), 1);
    ASSERT_EQ_INT((int)Wow_CachedCreatureType(163), 7);
    ASSERT_EQ_INT((int)Wow_CachedCreatureFamily(161), 1);
    ASSERT_EQ_INT((int)Wow_CachedCreatureFamily(163), 0);
    ASSERT_EQ_INT((int)Wow_CachedCreatureRank(161), 0);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * Spawn budget tests
 * =================================================================== */

static void test_spawn_budget_resets_per_frame(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    VECTOR2 origin = { 0, 0 };
    LPEDICT d1 = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, 1000);
    ASSERT_NOT_NULL(d1);
    game->RunFrame();

    LPEDICT d2 = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, 1000);
    ASSERT_NOT_NULL(d2);
    ASSERT(d1 != d2);

    if (game->Shutdown) game->Shutdown();
}

static void test_entity_counts_after_load(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));

    DWORD creatures = count_of_kind(WOW_ENTITY_UNIT);
    ASSERT(creatures > 0);
    ASSERT_EQ_INT((int)count_of_kind(WOW_ENTITY_PLAYER), 0);
    ASSERT_EQ_INT((int)count_of_kind(WOW_ENTITY_CORPSE), 0);
    ASSERT_EQ_INT((int)count_of_kind(WOW_ENTITY_PROJECTILE), 0);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * Spawn budget overflow test
 * =================================================================== */

static void test_edict_limit_reached_returns_null(void) {
    struct game_export *game = init_game();
    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    DWORD num = (DWORD)globals.num_edicts;
    /* Fill remaining edicts with corpses (up to WOW_MAX_EDICTS) */
    while (num < WOW_MAX_EDICTS) {
        LPEDICT e = &wow_edicts[num++];
        memset(e, 0, sizeof(*e));
        memset(&wow_entity_locals[num - 1], 0, sizeof(wow_entity_locals[0]));
        e->inuse = true;
        e->s.number = num - 1;
        wow_entity_locals[num - 1].kind = WOW_ENTITY_CORPSE;
        globals.num_edicts = (int)num;
    }

    VECTOR2 origin = { 0, 0 };
    LPEDICT should_fail = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, 1000);
    ASSERT_NULL(should_fail);

    if (game->Shutdown) game->Shutdown();
}

int main(void) {
    RUN_TEST(test_corpse_spawned_on_creature_death);
    RUN_TEST(test_corpse_decays_over_time);
    RUN_TEST(test_corpse_removed_after_timer_expires);
    RUN_TEST(test_corpse_not_spawned_for_null_entity);
    RUN_TEST(test_dynamic_object_spawn_and_properties);
    RUN_TEST(test_dynamic_object_despawns_after_duration);
    RUN_TEST(test_entity_handler_table_complete);
    RUN_TEST(test_backward_compat_creature_equals_unit);
    RUN_TEST(test_creature_info_cache_known_displays);
    RUN_TEST(test_spawn_budget_resets_per_frame);
    RUN_TEST(test_entity_counts_after_load);
    RUN_TEST(test_edict_limit_reached_returns_null);
    TEST_RESULTS();
}
