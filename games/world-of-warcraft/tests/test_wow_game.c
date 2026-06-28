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

static testModel_t test_models[32];
static testModel_t test_images[32];
static DWORD test_num_models;
static DWORD test_num_images;
static DWORD test_clear_world_calls;
static DWORD test_apply_lobby_calls;
static BYTE test_multicast_buf[MAX_MSGLEN];
static BYTE test_last_unicast_buf[MAX_MSGLEN];
static DWORD test_multicast_size;
static DWORD test_last_unicast_size;
static DWORD test_unicast_calls;
static char test_last_error[512];

static animation_t test_animations[] = {
    { .name = "Stand",        .interval = { 0, 1000 } },
    { .name = "Walk",         .interval = { 0, 1000 } },
    { .name = "Run",          .interval = { 0, 1000 } },
    { .name = "Ready1H",      .interval = { 0, 1000 } },
    { .name = "ReadyUnarmed", .interval = { 0, 1000 } },
    { .name = "Attack1H",     .interval = { 0, 1000 } },
    { .name = "Pain",         .interval = { 0,  450 } },
    { .name = "Death",        .interval = { 0, 1200 } },
};

static void put32(LPBYTE out, DWORD value) {
    out[0] = (BYTE)(value & 0xff);
    out[1] = (BYTE)((value >> 8) & 0xff);
    out[2] = (BYTE)((value >> 16) & 0xff);
    out[3] = (BYTE)((value >> 24) & 0xff);
}

static void putfloat(LPBYTE out, FLOAT value) {
    memcpy(out, &value, sizeof(value));
}

static void putfield(LPBYTE record, DWORD field, DWORD value) {
    put32(record + field * sizeof(DWORD), value);
}

static void putfield_float(LPBYTE record, DWORD field, FLOAT value) {
    putfloat(record + field * sizeof(DWORD), value);
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
    DWORD offset = *cursor;
    DWORD len = (DWORD)strlen(value) + 1;

    memcpy(strings + offset, value, len);
    *cursor += len;
    return offset;
}

static HANDLE make_map_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(1, 5, 64, &size);
    LPBYTE record = data + 20;
    LPBYTE strings = record + 5 * sizeof(DWORD);
    DWORD cursor = 1;
    DWORD map_name = add_string(strings, &cursor, "Azeroth");
    DWORD title = add_string(strings, &cursor, "Elwynn Test");

    putfield(record, 0, 1);
    putfield(record, 1, map_name);
    putfield(record, 3, title);
    putfield(record, 4, 42);
    *size_out = size;
    return data;
}

static HANDLE make_loading_screens_dbc(LPDWORD size_out) {
    DWORD size;
    LPBYTE data = alloc_dbc(1, 3, 96, &size);
    LPBYTE record = data + 20;
    LPBYTE strings = record + 3 * sizeof(DWORD);
    DWORD cursor = 1;
    DWORD texture = add_string(strings, &cursor, "Interface\\Glues\\LoadingScreens\\LoadScreenTest.blp");

    putfield(record, 0, 42);
    putfield(record, 2, texture);
    *size_out = size;
    return data;
}

static HANDLE make_world_safe_locs_dbc(LPDWORD size_out) {
    static struct {
        DWORD id;
        LPCSTR name;
        FLOAT x, y, z;
    } const safe_locs[] = {
        { 100, "Northshire", 123.25f, -456.5f, 78.0f },
        { 101, "Deathknell, Tirisfal", 1880.7385f, 1624.7355f, 94.4343f },
        { 102, "Coldridge Valley", -6240.32f, 331.033f, 382.758f },
        { 103, "Valley of Trials", -600.0f, -4200.0f, 38.0f },
    };
    DWORD size;
    LPBYTE data = alloc_dbc(sizeof(safe_locs) / sizeof(safe_locs[0]), 6, 128, &size);
    LPBYTE records = data + 20;
    LPBYTE strings = records + sizeof(safe_locs) / sizeof(safe_locs[0]) * 6 * sizeof(DWORD);
    DWORD cursor = 1;

    FOR_LOOP(i, sizeof(safe_locs) / sizeof(safe_locs[0])) {
        LPBYTE record = records + i * 6 * sizeof(DWORD);
        DWORD safe_name = add_string(strings, &cursor, safe_locs[i].name);

        putfield(record, 0, safe_locs[i].id);
        putfield(record, 1, 1);
        putfield_float(record, 2, safe_locs[i].x);
        putfield_float(record, 3, safe_locs[i].y);
        putfield_float(record, 4, safe_locs[i].z);
        putfield(record, 5, safe_name);
    }
    *size_out = size;
    return data;
}

static HANDLE make_creature_display_info_dbc(LPDWORD size_out) {
    DWORD displays[] = { 161, 193, 163, 188 };
    DWORD size;
    LPBYTE data = alloc_dbc(4, 5, 1, &size);
    LPBYTE records = data + 20;

    FOR_LOOP(i, 4) {
        LPBYTE record = records + i * 5 * sizeof(DWORD);

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
    LPBYTE records = data + 20;
    LPBYTE strings = records + 4 * 15 * sizeof(DWORD);
    DWORD cursor = 1;

    FOR_LOOP(i, 4) {
        LPBYTE record = records + i * 15 * sizeof(DWORD);
        char model_name[64];
        DWORD model_offset;

        snprintf(model_name, sizeof(model_name), "Creature\\Test\\Creature%u.m2", (unsigned)i);
        model_offset = add_string(strings, &cursor, model_name);
        putfield(record, 0, 700 + i);
        putfield(record, 2, model_offset);
        putfield_float(record, 4, 1.0f);
        putfield_float(record, 14, 3.0f);
    }
    *size_out = size;
    return data;
}

static BOOL path_eq(LPCSTR a, LPCSTR b) {
    while (*a && *b) {
        char ca = *a == '/' ? '\\' : *a;
        char cb = *b == '/' ? '\\' : *b;

        if (tolower((unsigned char)ca) != tolower((unsigned char)cb)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static HANDLE test_read_file(LPCSTR filename, LPDWORD size) {
    if (path_eq(filename, "DBFilesClient\\Map.dbc")) {
        return make_map_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\LoadingScreens.dbc")) {
        return make_loading_screens_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\WorldSafeLocs.dbc")) {
        return make_world_safe_locs_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\CreatureDisplayInfo.dbc")) {
        return make_creature_display_info_dbc(size);
    }
    if (path_eq(filename, "DBFilesClient\\CreatureModelData.dbc")) {
        return make_creature_model_data_dbc(size);
    }
    if (size) {
        *size = 0;
    }
    return NULL;
}

static HANDLE test_mem_alloc(long size) {
    return calloc(1, (size_t)size);
}

static void test_mem_free(HANDLE mem) {
    free(mem);
}

static int test_model_index(LPCSTR model_name) {
    FOR_LOOP(i, test_num_models) {
        if (!strcasecmp(test_models[i].name, model_name)) {
            return test_models[i].index;
        }
    }
    ASSERT(test_num_models < sizeof(test_models) / sizeof(test_models[0]));
    strncpy(test_models[test_num_models].name, model_name, sizeof(test_models[0].name) - 1);
    test_models[test_num_models].index = (int)test_num_models + 1;
    test_num_models++;
    return (int)test_num_models;
}

static int test_image_index(LPCSTR image_name) {
    FOR_LOOP(i, test_num_images) {
        if (!strcasecmp(test_images[i].name, image_name)) {
            return test_images[i].index;
        }
    }
    ASSERT(test_num_images < sizeof(test_images) / sizeof(test_images[0]));
    strncpy(test_images[test_num_images].name, image_name, sizeof(test_images[0].name) - 1);
    test_images[test_num_images].index = (int)test_num_images + 1;
    test_num_images++;
    return (int)test_num_images;
}

static void test_clear_world(void) {
    test_clear_world_calls++;
}

static void test_apply_lobby_settings(LPMAPINFO info) {
    test_apply_lobby_calls++;
    ASSERT_NOT_NULL(info);
}

static void test_configstring(DWORD index, LPCSTR string) { (void)index; (void)string; }

static LPCSTR test_get_configstring(DWORD index) { (void)index; return ""; }

static LPCSTR test_cvar_string(LPCSTR name, LPCSTR fallback) { (void)name; return fallback; }

static void test_error(LPCSTR fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vsnprintf(test_last_error, sizeof(test_last_error), fmt, args);
    va_end(args);
}

static void test_write_data(void const *data, DWORD size) {
    if (!data || test_multicast_size + size > sizeof(test_multicast_buf)) {
        return;
    }
    memcpy(test_multicast_buf + test_multicast_size, data, size);
    test_multicast_size += size;
}

static void test_write(pfWriteType_t type, void const *value) {
    BYTE b;
    SHORT s;
    LPCSTR text;

    switch (type) {
        case PF_BYTE:
            b = (BYTE)*(LONG const *)value;
            test_write_data(&b, sizeof(b));
            break;
        case PF_SHORT:
            s = (SHORT)*(LONG const *)value;
            test_write_data(&s, sizeof(s));
            break;
        case PF_STRING:
            text = value ? (LPCSTR)value : "";
            test_write_data(text, (DWORD)strlen(text) + 1);
            break;
        default:
            break;
    }
}

static void test_unicast(LPEDICT ent) {
    (void)ent;
    test_unicast_calls++;
    test_last_unicast_size = test_multicast_size;
    memcpy(test_last_unicast_buf, test_multicast_buf, test_last_unicast_size);
    test_multicast_size = 0;
}

static struct game_import test_import(void) {
    struct game_import import;

    memset(&import, 0, sizeof(import));
    import.MemAlloc = test_mem_alloc;
    import.MemFree = test_mem_free;
    import.ModelIndex = test_model_index;
    import.ImageIndex = test_image_index;
    import.ReadFile = test_read_file;
    import.ClearWorld = test_clear_world;
    import.ApplyLobbySettings = test_apply_lobby_settings;
    import.Write = test_write;
    import.unicast = test_unicast;
    import.error = test_error;
    import.configstring = test_configstring;
    import.GetConfigstring = test_get_configstring;
    import.CvarString = test_cvar_string;
    return import;
}

static LPEDICT first_creature(void) {
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        wowEntityLocal_t *local = Wow_EntityLocal(&wow_edicts[i]);

        if (wow_edicts[i].inuse && local && local->kind == WOW_ENTITY_CREATURE) {
            return &wow_edicts[i];
        }
    }
    return NULL;
}

int G_RegisterModel(LPCSTR filename) {
    return gi.ModelIndex(filename);
}

LPCANIMATION G_GetAnimation(DWORD modelindex, LPCSTR animname) {
    (void)modelindex;
    FOR_LOOP(i, sizeof(test_animations) / sizeof(test_animations[0])) {
        if (!strcasecmp(test_animations[i].name, animname)) {
            return &test_animations[i];
        }
    }
    return NULL;
}

void G_FreeModels(void) {
}

void PF_TextRemoveComments(LPSTR buffer) {
    (void)buffer;
}

static void reset_test_state(void) {
    memset(test_models, 0, sizeof(test_models));
    memset(test_images, 0, sizeof(test_images));
    test_num_models = 0;
    test_num_images = 0;
    test_clear_world_calls = 0;
    test_apply_lobby_calls = 0;
    memset(test_multicast_buf, 0, sizeof(test_multicast_buf));
    test_multicast_size = 0;
    memset(test_last_unicast_buf, 0, sizeof(test_last_unicast_buf));
    test_last_unicast_size = 0;
    test_unicast_calls = 0;
    memset(test_last_error, 0, sizeof(test_last_error));
}

static void assert_player_ui_payload(void) {
    DWORD cursor = 0;
    int num_buttons;
    int num_inventory;

    ASSERT(test_last_unicast_size > 0);
    ASSERT_EQ_INT(test_last_unicast_buf[cursor++], svc_unit_ui);
    ASSERT_EQ_INT(test_last_unicast_buf[cursor++], 1);
    ASSERT_EQ_INT((SHORT)(test_last_unicast_buf[cursor] | (test_last_unicast_buf[cursor + 1] << 8)), 0);
    cursor += 2;
    num_buttons = test_last_unicast_buf[cursor++];
    ASSERT_EQ_INT(num_buttons, WOW_UI_ACTION_SLOTS);
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "Interface\\Icons\\Ability_Warrior_Cleave.blp");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "Attack");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "1");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "wow_action 0");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_EQ_INT(test_last_unicast_buf[cursor++], '1');
    for (int i = 1; i < num_buttons; i++) {
        FOR_LOOP(j, 4) {
            cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
        }
        cursor++;
    }
    num_inventory = test_last_unicast_buf[cursor++];
    ASSERT_EQ_INT(num_inventory, WOW_UI_INVENTORY_SLOTS);
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "Interface\\Icons\\INV_Misc_Bag_08.blp");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "Backpack");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_STR_EQ((LPCSTR)test_last_unicast_buf + cursor, "1");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    ASSERT_EQ_INT(test_last_unicast_buf[cursor++], 0);
}

static struct game_export *init_game(void) {
    struct game_import import = test_import();
    struct game_export *game;

    reset_test_state();
    game = GetGameAPI(&import);
    ASSERT_NOT_NULL(game);
    ASSERT_NOT_NULL(game->Init);
    ASSERT_NOT_NULL(game->LoadMap);
    game->Init();
    return game;
}

static void assert_player_spawned_at_safe_loc(LPEDICT player) {
    LPCMAPINFO info = CM_GetMapInfo();
    BOOL matched = false;

    ASSERT_NOT_NULL(info);
    ASSERT_STR_EQ(info->players[0].playerName, "Northshire");
    ASSERT_STR_EQ(info->players[1].playerName, "Deathknell, Tirisfal");
    ASSERT_EQ_INT((int)info->players[0].playerType, kPlayerTypeHuman);
    ASSERT_EQ_INT((int)info->players[1].playerType, kPlayerTypeNone);

    FOR_LOOP(i, MAX_PLAYERS) {
        LPCMAPPLAYER spawn = &info->players[i];

        if (!spawn->used)
            continue;
        if (fabsf(player->s.origin.x - spawn->startingPosition.x) > 0.001f ||
            fabsf(player->s.origin.y - spawn->startingPosition.y) > 0.001f)
            continue;
        ASSERT_EQ_INT((int)player->client->ps.start_location, (int)i);
        ASSERT_EQ_FLOAT(player->s.origin.z,
                        CM_GetHeightAtPoint(spawn->startingPosition.x, spawn->startingPosition.y),
                        0.001f);
        matched = true;
    }
    ASSERT(matched);
}

void UI_WriteWowHud(LPEDICT ent) { (void)ent; }

static void test_wow_load_map_initializes_player_state(void) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowEntityLocal_t *local;

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    local = Wow_EntityLocal(player);

    ASSERT_EQ_INT((int)test_apply_lobby_calls, 1);
    ASSERT_EQ_INT((int)test_clear_world_calls, 1);
    ASSERT(player->inuse);
    ASSERT_NOT_NULL(player->client);
    ASSERT_NOT_NULL(local);
    ASSERT_EQ_INT((int)local->kind, WOW_ENTITY_PLAYER);
    ASSERT_EQ_INT((int)local->health, 100);
    assert_player_spawned_at_safe_loc(player);
    ASSERT_EQ_FLOAT(player->client->ps.origin.x, player->s.origin.x, 0.001f);
    ASSERT_EQ_FLOAT(player->client->ps.origin.y, player->s.origin.y, 0.001f);
    /* After LoadMap, the player is in loading state — ClientBegin transitions to GAME. */
    ASSERT_EQ_INT((int)player->client->ps.client_ui_state, CLIENT_UI_LOADING);
    ASSERT_STR_EQ(player->client->ps.name, "Thrall");
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_HEALTH], 100);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_HEALTH_MAX], 100);
    ASSERT_EQ_INT((int)player->client->ps.stats[WOW_STAT_POWER], 42);
    ASSERT_EQ_INT((int)test_num_images, 0);
    ASSERT_EQ_INT((int)test_unicast_calls, 0);
    ASSERT_NOT_NULL(game->ClientBegin);
    game->ClientBegin(player);
    ASSERT(test_unicast_calls > 0);
    assert_player_ui_payload();
    ASSERT_STR_EQ(player->client->ps.texts[PLAYERTEXT_MAP_TITLE], "Elwynn Test");
    ASSERT_STR_EQ(player->client->ps.texts[PLAYERTEXT_MAP_PREVIEW],
                  "Interface\\Glues\\LoadingScreens\\LoadScreenTest.blp");
    ASSERT(player->s.model > 0);
    ASSERT(player->s.model2 > 0);
    ASSERT_EQ_FLOAT(player->s.angle, 0.0f, 0.001f);
    ASSERT_EQ_INT((int)player->s.appearance,
                  (int)Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0));
    ASSERT_EQ_INT((int)player->s.equipment,
                  (int)Wow_PackEquipment(1, 1, 1, 1));

    if (game->Shutdown) {
        game->Shutdown();
    }
}

static void test_wow_load_map_spawns_and_runs_creature_state(void) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *creature_local;
    wowEntityLocal_t *player_local;
    VECTOR2 before;
    LPCSTR attack_argv[] = { "attack", "1" };

    ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    ASSERT_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    player_local = Wow_EntityLocal(player);
    before = creature->s.origin2;

    ASSERT_EQ_INT((int)creature->s.number, 1);
    ASSERT_EQ_INT((int)creature_local->kind, WOW_ENTITY_CREATURE);
    ASSERT_EQ_INT((int)creature_local->display_id, 161);
    ASSERT_EQ_INT((int)creature_local->health, 3);
    ASSERT((creature->svflags & SVF_MONSTER) != 0);
    ASSERT((creature->s.flags & EF_GROUND_ANCHOR) != 0);
    ASSERT_EQ_INT((int)creature->s.player, 2);
    ASSERT_EQ_FLOAT(creature->s.scale, 1.0f, 0.001f);
    ASSERT_EQ_FLOAT(creature->s.radius, 1.5f, 0.001f);
    ASSERT_NOT_NULL(creature_local->animation);
    ASSERT_STR_EQ(creature_local->animation->name, "Walk");

    game->RunFrame();
    ASSERT(fabsf(creature->s.origin2.x - before.x) > 0.001f ||
           fabsf(creature->s.origin2.y - before.y) > 0.001f);

    game->ClientCommand(player, 2, attack_argv);
    ASSERT_EQ_INT((int)(player_local->enemy ? player_local->enemy->s.number : 0), 1);
    ASSERT(player_local->attack_damage_time > 0);
    ASSERT(player_local->attack_backswing_time > 0);
    ASSERT_NOT_NULL(player_local->animation);
    ASSERT_STR_EQ(player_local->animation->name, "Attack1H");

    if (game->Shutdown) {
        game->Shutdown();
    }
}

int main(void) {
    RUN_TEST(test_wow_load_map_initializes_player_state);
    RUN_TEST(test_wow_load_map_spawns_and_runs_creature_state);
    TEST_RESULTS();
}
