#include "test.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "game/g_wow_local.h"
#include "common/stb_dbc.h"


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
    *(DWORD *)data = ID_WDBC;
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
static void test_clear_world(void) {}
static void test_apply_lobby_settings(LPMAPINFO info) { (void)info; }
static int test_model_index(LPCSTR name) {
    FOR_LOOP(i, test_num_models)
        if (!strcasecmp(test_models[i].name, name)) return test_models[i].index;
    T_ASSERT(test_num_models < sizeof(test_models) / sizeof(test_models[0]));
    strncpy(test_models[test_num_models].name, name, sizeof(test_models[0].name) - 1);
    test_models[test_num_models].index = (int)test_num_models + 1;
    test_num_models++;
    return (int)test_num_models;
}
static int test_image_index(LPCSTR name) {
    FOR_LOOP(i, test_num_images)
        if (!strcasecmp(test_images[i].name, name)) return test_images[i].index;
    T_ASSERT(test_num_images < sizeof(test_images) / sizeof(test_images[0]));
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
void UI_WriteWowHover(LPEDICT ent) { (void)ent; }
void UI_WriteWelcomeWindow(LPEDICT ent) { (void)ent; }
void UI_HideWindow(LPEDICT ent, LPCSTR window_id) { (void)ent; (void)window_id; }

static struct game_import test_import(void) {
    struct game_import import;
    memset(&import, 0, sizeof(import));
    import.MemAlloc = test_mem_alloc;
    import.MemFree = test_mem_free;
    import.ModelIndex = test_model_index;
    import.ImageIndex = test_image_index;
    import.ReadFile = test_read_file;
    /* Map-load tests provide required server callbacks; the old fixture crashed at a null call. */
    import.ClearWorld = test_clear_world;
    import.ApplyLobbySettings = test_apply_lobby_settings;
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
    T_NOT_NULL(game);
    T_NOT_NULL(game->Init);
    T_NOT_NULL(game->LoadMap);
    game->Init();
    return game;
}

/* Entities are identified by their game-local think function pointer (Quake2
 * style); there is no kind tag. */
static LPEDICT first_with_think(void (*think)(LPEDICT)) {
    for (DWORD i = MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        if (wow_edicts[i].inuse && Wow_EntityLocal(&wow_edicts[i])->think == think) return &wow_edicts[i];
    }
    return NULL;
}

static DWORD count_with_think(void (*think)(LPEDICT)) {
    DWORD count = 0;
    for (DWORD i = MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        if (wow_edicts[i].inuse && Wow_EntityLocal(&wow_edicts[i])->think == think) count++;
    }
    return count;
}

/* ===================================================================
 * Corpse tests
 * =================================================================== */

TEST(wow_entities, dying_creature_becomes_corpse) {
    struct game_export *game = init_game();
    DWORD num_edicts;
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame(); /* reset spawn budget */

    LPEDICT creature = first_with_think(Wow_RunCreatureFrame);
    T_NOT_NULL(creature);
    wowEntityLocal_t *cl = Wow_EntityLocal(creature);
    DWORD model = creature->s.model;
    VECTOR3 origin = creature->s.origin;
    num_edicts = (DWORD)globals.num_edicts;

    Wow_AIDie(creature, &wow_edicts[0]);
    T_ASSERT(cl->dead);
    T_EQ((int)globals.num_edicts, (int)num_edicts);
    while (cl->death_time > 0) Wow_AIAdvanceLockedFrame(creature);
    T_ASSERT(cl->think == Wow_RunCorpseFrame);
    T_EQ((int)cl->corpse_owner, (int)creature->s.number);
    T_ASSERT(cl->corpse_timer > 0);
    T_EQ((int)creature->s.model, (int)model);
    T_FEQ(creature->s.origin.x, origin.x, 0.001f);
    T_FEQ(creature->s.origin.y, origin.y, 0.001f);
    T_EQ((int)creature->s.flags & EF_GROUND_ANCHOR, (int)EF_GROUND_ANCHOR);
    T_EQ((int)creature->svflags & SVF_MONSTER, 0);
    T_ASSERT(creature->svflags & SVF_DEADMONSTER);

    if (game->Shutdown) game->Shutdown();
}

TEST(wow_entities, corpse_decays_over_time) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    LPEDICT creature = first_with_think(Wow_RunCreatureFrame);
    T_NOT_NULL(creature);
    Wow_AIDie(creature, &wow_edicts[0]);
    while (Wow_EntityLocal(creature)->death_time > 0) Wow_AIAdvanceLockedFrame(creature);

    wowEntityLocal_t *cl = Wow_EntityLocal(creature);
    T_ASSERT(cl->think == Wow_RunCorpseFrame);
    DWORD initial = cl->corpse_timer;

    for (int i = 0; i < 10; i++) game->RunFrame();
    T_ASSERT(cl->corpse_timer < initial);
    T_ASSERT(creature->inuse);

    if (game->Shutdown) game->Shutdown();
}

TEST(wow_entities, corpse_removed_after_timer_expires) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    LPEDICT creature = first_with_think(Wow_RunCreatureFrame);
    T_NOT_NULL(creature);
    Wow_AIDie(creature, &wow_edicts[0]);
    while (Wow_EntityLocal(creature)->death_time > 0) Wow_AIAdvanceLockedFrame(creature);

    T_ASSERT(Wow_EntityLocal(creature)->think == Wow_RunCorpseFrame);
    Wow_EntityLocal(creature)->corpse_timer = FRAMETIME;

    game->RunFrame();
    T_ASSERT(!creature->inuse);

    if (game->Shutdown) game->Shutdown();
}

TEST(wow_entities, walking_creature_reanchors_to_terrain) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    LPEDICT creature = first_with_think(Wow_RunCreatureFrame);
    T_NOT_NULL(creature);
    wowEntityLocal_t *cl = Wow_EntityLocal(creature);
    T_ASSERT(cl->patrol_radius > 0.0f && cl->walk_speed > 0.0f);

    /* Simulate walking onto a different elevation: corrupt Z, then a walk step
     * must re-anchor it to the terrain (Quake2 M_CheckGround behaviour). */
    creature->s.origin.z = 99999.0f;
    Wow_AIMove(creature);

    T_NE(creature->s.origin.z, 99999.0f);
    T_FEQ(creature->s.origin.z, Wow_TerrainHeight(creature->s.origin.x, creature->s.origin.y), 0.001f);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * DynamicObject tests
 * =================================================================== */

TEST(wow_entities, dynamic_object_spawn_and_properties) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    VECTOR2 origin = { 100.0f, 200.0f };
    LPEDICT dobj = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, 5000);
    T_NOT_NULL(dobj);

    wowEntityLocal_t *dl = Wow_EntityLocal(dobj);
    T_ASSERT(dl->think == Wow_RunDynamicObjectFrame);
    T_EQ((int)dl->dyn_spell_id, (int)WOW_SPELL_FIREBOLT);
    T_EQ((int)dl->dyn_duration, 5000);
    T_EQ((int)dl->dyn_radius, 2);
    T_FEQ(dobj->s.origin2.x, 100.0f, 0.001f);
    T_FEQ(dobj->s.origin2.y, 200.0f, 0.001f);
    T_FEQ(dobj->s.radius, 2.0f, 0.001f);
    T_EQ((int)dobj->s.flags & EF_GROUND_ANCHOR, (int)EF_GROUND_ANCHOR);

    DWORD initial = dl->dyn_duration;
    for (int i = 0; i < 10; i++) game->RunFrame();
    T_ASSERT(dl->dyn_duration < initial);

    if (game->Shutdown) game->Shutdown();
}

TEST(wow_entities, dynamic_object_despawns_after_duration) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    VECTOR2 origin = { 0, 0 };
    LPEDICT dobj = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, FRAMETIME);
    T_NOT_NULL(dobj);

    game->RunFrame();
    T_ASSERT(!dobj->inuse);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * Creature info cache tests
 * =================================================================== */

TEST(wow_entities, creature_info_cache_known_displays) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));

    T_STREQ(Wow_CachedCreatureName(161), "Wolf");
    T_STREQ(Wow_CachedCreatureName(193), "Boar");
    T_STREQ(Wow_CachedCreatureName(163), "Kobold");
    T_STREQ(Wow_CachedCreatureName(188), "Murloc");
    T_STREQ(Wow_CachedCreatureName(99999), "Unknown");

    T_EQ((int)Wow_CachedCreatureType(161), 1);
    T_EQ((int)Wow_CachedCreatureType(163), 7);
    T_EQ((int)Wow_CachedCreatureFamily(161), 1);
    T_EQ((int)Wow_CachedCreatureFamily(163), 0);
    T_EQ((int)Wow_CachedCreatureRank(161), 0);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * Spawn budget tests
 * =================================================================== */

TEST(wow_entities, spawn_budget_resets_per_frame) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    VECTOR2 origin = { 0, 0 };
    LPEDICT d1 = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, 1000);
    T_NOT_NULL(d1);
    game->RunFrame();

    LPEDICT d2 = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, 1000);
    T_NOT_NULL(d2);
    T_ASSERT(d1 != d2);

    if (game->Shutdown) game->Shutdown();
}

TEST(wow_entities, entity_counts_after_load) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));

    DWORD creatures = count_with_think(Wow_RunCreatureFrame);
    T_ASSERT(creatures > 0);
    T_EQ((int)count_with_think(Wow_RunCorpseFrame), 0);
    T_EQ((int)count_with_think(Wow_RunProjectile), 0);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * Spawn budget overflow test
 * =================================================================== */

TEST(wow_entities, edict_limit_reached_returns_null) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();

    DWORD num = (DWORD)globals.num_edicts;
    /* Fill remaining edicts with corpses (up to WOW_MAX_EDICTS) */
    while (num < WOW_MAX_EDICTS) {
        LPEDICT e = &wow_edicts[num++];
        memset(e, 0, sizeof(*e));
        memset(&wow_entity_locals[num - 1], 0, sizeof(wow_entity_locals[0]));
        e->inuse = true;
        e->s.number = num - 1;
        Wow_EntityLocal(e)->think = Wow_RunCorpseFrame;
        globals.num_edicts = (int)num;
    }

    VECTOR2 origin = { 0, 0 };
    LPEDICT should_fail = Wow_SpawnDynamicObject(WOW_SPELL_FIREBOLT, &origin, 1000);
    T_NULL(should_fail);

    if (game->Shutdown) game->Shutdown();
}

/* ===================================================================
 * stb_dbc.h — shared WDBC reader primitives
 * =================================================================== */

TEST(wow_entities, stb_dbc_parses_header_and_fields) {
    DWORD size;
    LPBYTE data = alloc_dbc(2, 4, 128, &size);
    LPBYTE r0 = data + 20, r1 = r0 + 4 * sizeof(DWORD);
    LPBYTE strings = r1 + 4 * sizeof(DWORD);
    DWORD cursor = 1;
    stbDbc_t h;
    BYTE const *found;

    putfield(r0, 0, 7);
    putfield(r0, 1, add_string(strings, &cursor, "Azeroth"));
    putfield(r0, 2, 99);
    putfield(r1, 0, 8);
    putfield(r1, 1, add_string(strings, &cursor, "Kalimdor"));

    /* Header validation fills records/fields/record_size/string_size. */
    T_ASSERT(Stb_DbcValid(data, size, &h));
    T_EQ((int)h.records, 2);
    T_EQ((int)h.fields, 4);
    T_EQ((int)h.record_size, 4 * (int)sizeof(DWORD));
    T_EQ((int)h.string_size, 128);

    /* Block pointers. */
    T_ASSERT(Stb_DbcRecords(data) == data + 20);
    T_ASSERT(Stb_DbcStrings(data, &h) == data + 20 + 2 * 4 * sizeof(DWORD));

    /* Field access + bounds check. */
    T_EQ((int)Stb_DbcField(&h, r0, 2), 99);
    T_EQ((int)Stb_DbcField(&h, r0, 4), 0);       /* field >= fields -> 0 */
    T_EQ((int)Stb_DbcField(&h, r0, 3), 0);       /* within fields, zero value */

    /* String access: valid, offset 0 (null), out-of-range. */
    T_STREQ(Stb_DbcString(Stb_DbcStrings(data, &h), h.string_size, Stb_DbcField(&h, r0, 1)), "Azeroth");
    T_NULL(Stb_DbcString(Stb_DbcStrings(data, &h), h.string_size, 0));
    T_NULL(Stb_DbcString(Stb_DbcStrings(data, &h), h.string_size, h.string_size));

    /* ID lookup by field 0. */
    found = Stb_DbcFindID(data, &h, 8);
    T_ASSERT(found == r1);
    T_EQ((int)Stb_DbcField(&h, found, 1), (int)Stb_DbcField(&h, r1, 1));
    T_NULL(Stb_DbcFindID(data, &h, 42));

    free(data);

    /* Malformed envelope: block overflow, too-small record size, bad magic. */
    LPBYTE bad = malloc(64);
    *(DWORD *)bad = ID_WDBC;
    put32(bad + 4, 10); put32(bad + 8, 1); put32(bad + 12, 4); put32(bad + 16, 0);
    T_ASSERT(!Stb_DbcValid(bad, 32, &h)); /* 20 + 10*4 > 32 */
    put32(bad + 12, 0);
    T_ASSERT(!Stb_DbcValid(bad, 32, &h)); /* record_size < sizeof(DWORD) */
    memcpy(bad, "XXXX", 4);
    T_ASSERT(!Stb_DbcValid(bad, 32, &h)); /* bad magic */
    free(bad);

    /* Loose rule: logical field_count may exceed record_size/4 (classic CharStartOutfit),
       but Stb_DbcField still bounds-checks against the physical record. */
    LPBYTE wide = calloc(1, 20 + 8);
    *(DWORD *)wide = ID_WDBC;
    put32(wide + 4, 1); put32(wide + 8, 40); put32(wide + 12, 8); put32(wide + 16, 0);
    T_ASSERT(Stb_DbcValid(wide, 20 + 8, &h));
    T_EQ((int)h.fields, 40);
    T_EQ((int)h.record_size, 8);
    put32(wide + 20, 123); put32(wide + 24, 456);
    T_EQ((int)Stb_DbcField(&h, wide + 20, 0), 123);
    T_EQ((int)Stb_DbcField(&h, wide + 20, 1), 456);
    T_EQ((int)Stb_DbcField(&h, wide + 20, 2), 0);   /* 2*4+4 > record_size */
    T_EQ((int)Stb_DbcField(&h, wide + 20, 39), 0);  /* field >= fields */
    free(wide);
}
