#include "test.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "game/g_wow_local.h"
#include "client/ui.h"


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
static char test_last_game_command[64];
static BYTE test_last_game_payload[MAX_MSGLEN];
static DWORD test_last_game_payload_size;
static char test_last_error[512];
static char test_playerinfo[MAX_PATHLEN];

typedef struct {
    BYTE layer;
    FRAMETYPE type;
    char text[512];
    char onclick[128];
} testUiFrame_t;

static testUiFrame_t test_ui_frames[256];
static DWORD test_ui_frame_count;
static BOOL test_expect_layout_layer;
static BYTE test_layout_layer;
static BOOL test_layout_seen[MAX_LAYOUT_LAYERS];

/* ---- configstring stubs (game_import.configstring / GetConfigstring) ---- */
#define TEST_CONFIGSTRINGS 128
static char test_configstrings[TEST_CONFIGSTRINGS][512];

static void test_configstring(DWORD index, LPCSTR string) {
    if (index < TEST_CONFIGSTRINGS) {
        strncpy(test_configstrings[index], string ? string : "", sizeof(test_configstrings[index]) - 1);
        test_configstrings[index][sizeof(test_configstrings[index]) - 1] = '\0';
    }
}

static LPCSTR test_get_configstring(DWORD index) {
    if (index < TEST_CONFIGSTRINGS) {
        return test_configstrings[index];
    }
    return "";
}

/* ---- cvar stub ---- */
static LPCSTR test_cvar_string(LPCSTR name, LPCSTR fallback) {
	if (!strcmp(name, WOW_CVAR_PLAYERINFO) && test_playerinfo[0])
		return test_playerinfo;
	return fallback ? fallback : "";
}

static animation_t test_animations[] = {
    { .name = "Stand",        .interval = { 0, 1000 } },
    { .name = "Walk",         .interval = { 0, 1000 } },
    { .name = "Run",          .interval = { 0, 1000 } },
    { .name = "Ready1H",      .interval = { 0, 1000 } },
    { .name = "ReadyUnarmed", .interval = { 0, 1000 } },
    { .name = "Attack1H",     .interval = { 0, 1000 } },
    { .name = "ReadySpellDirected", .interval = { 0, 1000 } },
    { .name = "SpellCastDirected",  .interval = { 0, 1000 } },
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
    T_ASSERT(test_num_models < sizeof(test_models) / sizeof(test_models[0]));
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
    T_ASSERT(test_num_images < sizeof(test_images) / sizeof(test_images[0]));
    strncpy(test_images[test_num_images].name, image_name, sizeof(test_images[0].name) - 1);
    test_images[test_num_images].index = (int)test_num_images + 1;
    test_num_images++;
    return (int)test_num_images;
}

static int test_font_index(LPCSTR font_name, DWORD font_size) {
    (void)font_name;
    return (int)font_size;
}

static void test_clear_world(void) {
    test_clear_world_calls++;
}

static void test_apply_lobby_settings(LPMAPINFO info) {
    test_apply_lobby_calls++;
    T_NOT_NULL(info);
}

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
            if (b == svc_layout) test_expect_layout_layer = true;
            else if (test_expect_layout_layer) {
                test_layout_layer = b;
                test_expect_layout_layer = false;
                if (b < MAX_LAYOUT_LAYERS) test_layout_seen[b] = true;
            }
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
        case PF_UIFRAME: {
            LPCUIFRAME frame = (LPCUIFRAME)value;
            testUiFrame_t *capture;
            if (!frame || test_ui_frame_count >= sizeof(test_ui_frames) / sizeof(test_ui_frames[0])) break;
            capture = &test_ui_frames[test_ui_frame_count++];
            capture->layer = test_layout_layer;
            capture->type = frame->flags.type;
            snprintf(capture->text, sizeof(capture->text), "%s", frame->text ? frame->text : "");
            snprintf(capture->onclick, sizeof(capture->onclick), "%s",
                     frame->onclick ? frame->onclick : "");
            break;
        }
        default:
            break;
    }
}

static void test_unicast(LPEDICT ent) {
    (void)ent;
    test_unicast_calls++;
    /* Keep the gameplay payload available: server-authored layout packets are separate messages. */
    if (test_multicast_size && test_multicast_buf[0] == svc_unit_ui) {
        test_last_unicast_size = test_multicast_size;
        memcpy(test_last_unicast_buf, test_multicast_buf, test_last_unicast_size);
    }
    test_multicast_size = 0;
}

static void test_game_command(LPEDICT ent, LPCSTR command, void const *data, DWORD size) {
    (void)ent;
    snprintf(test_last_game_command, sizeof(test_last_game_command), "%s", command ? command : "");
    test_last_game_payload_size = MIN(size, (DWORD)sizeof(test_last_game_payload));
    if (test_last_game_payload_size) memcpy(test_last_game_payload, data, test_last_game_payload_size);
}

static struct game_import test_import(void) {
    struct game_import import;

    memset(&import, 0, sizeof(import));
    import.MemAlloc = test_mem_alloc;
    import.MemFree = test_mem_free;
    import.ModelIndex = test_model_index;
    import.ImageIndex = test_image_index;
    import.FontIndex = test_font_index;
    import.ReadFile = test_read_file;
    import.ClearWorld = test_clear_world;
    import.ApplyLobbySettings = test_apply_lobby_settings;
    import.configstring = test_configstring;
    import.GetConfigstring = test_get_configstring;
    import.CvarString = test_cvar_string;
    import.Write = test_write;
    import.unicast = test_unicast;
    import.GameCommand = test_game_command;
    import.error = test_error;
    return import;
}

static LPEDICT first_creature(void) {
    for (DWORD i = WOW_MAX_CLIENTS; i < (DWORD)globals.num_edicts; i++) {
        if (wow_edicts[i].inuse && wow_edicts[i].think == Wow_RunCreatureFrame) {
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

FLOAT G_GetAttachmentZ(DWORD modelindex, int aid) {
    (void)modelindex;
    (void)aid;
    return 0.0f;
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
    test_last_game_command[0] = '\0';
    memset(test_last_game_payload, 0, sizeof(test_last_game_payload));
    test_last_game_payload_size = 0;
    memset(test_last_error, 0, sizeof(test_last_error));
    memset(test_configstrings, 0, sizeof(test_configstrings));
    test_playerinfo[0] = '\0';
    memset(test_ui_frames, 0, sizeof(test_ui_frames));
    test_ui_frame_count = 0;
    test_expect_layout_layer = false;
    test_layout_layer = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
}

static void assert_player_ui_payload(void) {
    DWORD cursor = 0;
    int num_buttons;
    int num_inventory;

    T_ASSERT(test_last_unicast_size > 0);
    T_EQ(test_last_unicast_buf[cursor++], svc_unit_ui);
    T_EQ(test_last_unicast_buf[cursor++], 1);
    T_EQ((SHORT)(test_last_unicast_buf[cursor] | (test_last_unicast_buf[cursor + 1] << 8)), 0);
    cursor += 2;
    num_buttons = test_last_unicast_buf[cursor++];
    T_EQ(num_buttons, WOW_UI_ACTION_SLOTS);
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "Interface\\Icons\\Ability_Warrior_Cleave.blp");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "Attack");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "1");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "wow_action 0");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    T_EQ(test_last_unicast_buf[cursor++], '1');
    for (int i = 1; i < num_buttons; i++) {
        FOR_LOOP(j, 4) {
            cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
        }
        if (i == 9) T_EQ(test_last_unicast_buf[cursor], '0');
        cursor++;
    }
    num_inventory = test_last_unicast_buf[cursor++];
    T_EQ(num_inventory, WOW_UI_INVENTORY_SLOTS);
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "Interface\\Icons\\INV_Misc_Bag_08.blp");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "Backpack");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "1");
    cursor += (DWORD)strlen((LPCSTR)test_last_unicast_buf + cursor) + 1;
    T_EQ(test_last_unicast_buf[cursor++], 0);
}

static struct game_export *init_game(void) {
    struct game_import import = test_import();
    struct game_export *game;

    reset_test_state();
    game = GetGameAPI(&import);
    T_NOT_NULL(game);
    T_NOT_NULL(game->Init);
    T_NOT_NULL(game->LoadMap);
    game->Init();
    return game;
}

/* Verify the player spawned at a valid spawn-table position. */
static void assert_player_spawned(LPEDICT player) {
    T_EQ((int)(player->client->ps.start_location != -1), 1);
    T_ASSERT(player->s.origin.x != 0.0f || player->s.origin.y != 0.0f);
}

TEST(wow_game, starter_weapon_damage_comes_from_serverdata) {
    LPCWOWWEAPON weapon = Wow_WeaponByEntry(WOW_START_WEAPON_ENTRY);
    DWORD damage;

    T_NOT_NULL(weapon);
    T_STREQ(weapon->name, "Worn Axe");
    T_FEQ(weapon->damage_min, 1.0f, 0.001f);
    T_FEQ(weapon->damage_max, 3.0f, 0.001f);
    T_EQ((int)weapon->delay, 2000);
    damage = Wow_RollWeaponDamage(WOW_START_WEAPON_ENTRY);
    T_ASSERT(damage >= 1 && damage <= 3);
}

TEST(wow_game, quest_serverdata_contains_givers_and_objective_locations) {
    LPCWOWQUESTGIVER giver = Wow_QuestGiver(2);
    LPCWOWQUESTOBJECTIVE objective = Wow_QuestObjective(1);

    T_EQ((int)Wow_QuestGiverCount(), 741);
    T_EQ((int)giver->quest_id, 7);
    T_EQ((int)giver->creature_entry, 197);
    T_EQ((int)giver->display_id, 1859);
    T_FEQ(giver->position.x, -8902.59f, 0.01f);
    T_FEQ(giver->position.y, -162.606f, 0.01f);
    T_EQ((int)Wow_QuestObjectiveCount(), 2558);
    T_EQ((int)objective->quest_id, 2);
    T_FEQ(objective->position.x, 2148.0f, 0.01f);
    T_FEQ(objective->position.y, -2816.0f, 0.01f);
    T_STREQ(Wow_QuestDetail(7)->title, "Kobold Camp Cleanup");
    T_ASSERT(Wow_QuestDetail(0xFFFFFFFF) == NULL);
}

TEST(wow_game, creature_serverdata_preserves_templates_and_all_models) {
    LPCWOWCREATURE marshal = Wow_CreatureByEntry(197);
    LPCWOWCREATURE deputy = Wow_CreatureByEntry(823);
    LPCWOWCREATURE defias = Wow_CreatureByEntry(824);
    LPCWOWCREATURE sparse = Wow_CreatureByEntry(34166);

    T_EQ((int)Wow_CreatureCount(), 29947);
    T_NOT_NULL(marshal); T_STREQ(marshal->name, "Marshal McBride");
    T_EQ((int)marshal->gossip_menu_id, 4048); T_EQ((int)marshal->npc_flags, 3);
    T_EQ((int)marshal->models[0].display_id, 1859);
    T_FEQ(marshal->models[0].display_scale, 1.0f, 0.001f);
    T_EQ((int)marshal->models[0].verified_build, 12340);
    T_NOT_NULL(deputy); T_EQ((int)deputy->models[0].display_id, 2072);
    T_NOT_NULL(defias); T_EQ((int)defias->model_count, 2);
    T_EQ((int)defias->models[0].display_id, 2441);
    T_EQ((int)defias->models[1].display_id, 556);
    T_NOT_NULL(sparse); T_EQ((int)sparse->models[0].display_id, 0);
    T_EQ((int)sparse->models[1].display_id, 25501);
    T_ASSERT(Wow_CreatureByEntry(0xffffffffu) == NULL);
}

TEST(wow_game, quest_hud_is_server_authored_on_quest_layer) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR open_command[] = { "quest", "7" };
    LPCSTR close_command[] = { "quest_close" };
    BOOL found_quest_button = false;
    BOOL found_quest_title = false;
    BOOL found_accept = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer == LAYER_CONSOLE && !strcmp(frame->onclick, "quest")) found_quest_button = true;
    }
    T_ASSERT(found_quest_button);

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->ClientCommand(player, 2, open_command);
    T_ASSERT(test_layout_seen[LAYER_QUESTDIALOG]);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer != LAYER_QUESTDIALOG) continue;
        if (!strcmp(frame->text, "Kobold Camp Cleanup")) found_quest_title = true;
        if (!strncmp(frame->onclick, "quest_accept 7", 14)) found_accept = true;
    }
    T_ASSERT(found_quest_title);
    T_ASSERT(found_accept);

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->ClientCommand(player, 1, close_command);
    T_ASSERT(test_layout_seen[LAYER_QUESTDIALOG]);
    FOR_LOOP(i, test_ui_frame_count)
        T_ASSERT(test_ui_frames[i].layer != LAYER_QUESTDIALOG);
}

TEST(wow_game, quest_detail_has_full_text_and_rewards) {
    LPCWOWQUESTDETAIL detail = Wow_QuestDetail(7);

    T_NOT_NULL(detail);
    T_STREQ(detail->title, "Kobold Camp Cleanup");
    T_ASSERT(detail->description && strlen(detail->description) > 10);
    T_ASSERT(detail->objectives_text && strlen(detail->objectives_text) > 5);
    T_ASSERT(detail->reward_text && strlen(detail->reward_text) > 5);
    T_EQ((int)detail->reward_xp, 850);
    T_EQ((int)detail->reward_gold, 25);
    T_EQ((int)detail->min_level, 1);
    T_EQ((int)detail->prev_quest, 783);
    T_EQ((int)detail->reward_items[0], 0);
}

TEST(wow_game, quest_accept_adds_to_quest_log) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept_command[] = { "quest_accept", "788" };
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;
    T_EQ((int)wc->quest_count, 0);

    game->ClientCommand(player, 2, accept_command);
    T_EQ((int)wc->quest_count, 1);
    T_EQ((int)wc->quests[0].quest_id, 788);
    T_EQ((int)wc->quests[0].status, WOW_QUEST_ACCEPTED);
}

TEST(wow_game, quest_prerequisite_blocks_accept) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept13[] = { "quest_accept", "13" };
    LPCSTR accept12[] = { "quest_accept", "12" };
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, accept13);
    T_EQ((int)wc->quest_count, 0);

    game->ClientCommand(player, 2, accept12);
    T_EQ((int)wc->quest_count, 1);
    T_EQ((int)wc->quests[0].quest_id, 12);

    game->ClientCommand(player, 2, accept13);
    T_EQ((int)wc->quest_count, 2);
    T_EQ((int)wc->quests[1].quest_id, 13);
}

TEST(wow_game, quest_complete_delivers_rewards) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept_command[] = { "quest_accept", "788" };
    LPCSTR complete_command[] = { "quest_complete", "788" };
    LPPLAYER ps;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    ps = &player->client->ps;
    ps->stats[WOW_STAT_XP] = 0;
    ps->stats[WOW_STAT_COPPER] = 0;

    game->ClientCommand(player, 2, accept_command);
    T_EQ((int)ps->stats[WOW_STAT_XP], 0);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);

    game->ClientCommand(player, 2, complete_command);
    T_EQ((int)ps->stats[WOW_STAT_XP], 1020);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);
}

TEST(wow_game, quest_completion_delivers_client_inbox_snapshot) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept_command[] = { "quest_accept", "788" };
    LPCSTR complete_command[] = { "quest_complete", "788" };
    BYTE const *payload;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    test_last_game_command[0] = '\0';
    test_last_game_payload_size = 0;
    game->ClientCommand(player, 2, accept_command);
    game->ClientCommand(player, 2, complete_command);
    T_STREQ(test_last_game_command, "wow_inbox");
    T_ASSERT(test_last_game_payload_size >= 2 + 4 + 1 + 1 + 4 + WOW_UI_MESSAGE_TITLE + WOW_UI_MESSAGE_BODY);
    payload = test_last_game_payload;
    T_EQ(payload[0], 1);
    T_EQ(payload[1], 1);
    T_EQ((int)(payload[2] | (payload[3] << 8) | (payload[4] << 16) | (payload[5] << 24)), 788);
    T_EQ(payload[6], WOW_UI_MESSAGE_QUEST_REWARD);
    T_EQ(payload[7], WOW_UI_MESSAGE_UNREAD);
    T_STREQ((LPCSTR)payload + 12, "Quest complete");
}

TEST(wow_game, message_read_requires_owned_id_and_clears_unread) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept_command[] = { "quest_accept", "788" };
    LPCSTR complete_command[] = { "quest_complete", "788" };
    LPCSTR read_command[] = { "message_read", "788" };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    game->ClientCommand(player, 2, accept_command);
    game->ClientCommand(player, 2, complete_command);
    game->ClientCommand(player, 2, read_command);
    T_STREQ(test_last_game_command, "wow_inbox");
    T_EQ(test_last_game_payload[1], 1);
    T_EQ(test_last_game_payload[7], 0);
}

TEST(wow_game, quest_turn_in_flow_accept_complete_reward) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept788[] = { "quest_accept", "788" };
    LPCSTR open788[] = { "quest", "788" };
    LPPLAYER ps;
    BOOL found_complete = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    ps = &player->client->ps;
    ps->stats[WOW_STAT_XP] = 0;
    ps->stats[WOW_STAT_COPPER] = 0;

    game->ClientCommand(player, 2, accept788);
    game->ClientCommand(player, 2, open788);
    /* Dialog shows accepted quest — no accept button, but close button exists */
    FOR_LOOP(i, test_ui_frame_count) {
        if (test_ui_frames[i].layer == LAYER_QUESTDIALOG &&
            !strcmp(test_ui_frames[i].onclick, "quest_close")) found_complete = true;
    }
    T_ASSERT(found_complete);

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "788"});
    T_EQ((int)ps->stats[WOW_STAT_XP], 1020);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);
}

TEST(wow_game, quest_log_shows_active_and_complete_quests) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept788[] = { "quest_accept", "788" };
    LPCSTR questlog_cmd[] = { "questlog" };
    BOOL found_header = false, found_title = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    game->ClientCommand(player, 2, accept788);

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->ClientCommand(player, 1, questlog_cmd);
    T_ASSERT(test_layout_seen[LAYER_QUESTDIALOG]);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer != LAYER_QUESTDIALOG) continue;
        if (!strcmp(frame->text, "Quest Log")) found_header = true;
        if (!strncmp(frame->text, "Cutting Teeth", 13)) found_title = true;
    }
    T_ASSERT(found_header);
    T_ASSERT(found_title);

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->ClientCommand(player, 1, (LPCSTR[]){"quest_close"});
    T_ASSERT(test_layout_seen[LAYER_QUESTDIALOG]);
    FOR_LOOP(i, test_ui_frame_count)
        T_ASSERT(test_ui_frames[i].layer != LAYER_QUESTDIALOG);
}

TEST(wow_game, quest_kill_progress_increments_and_auto_completes) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept788[] = { "quest_accept", "788" };
    wowClient_t *wc;
    wowQuestState_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, accept788);
    state = Wow_FindQuestState(wc, 788);
    T_NOT_NULL(state);
    T_EQ((int)state->status, WOW_QUEST_ACCEPTED);
    T_EQ((int)state->kill_progress[0], 0);

    FOR_LOOP(i, 4) Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)state->kill_progress[0], 4);
    T_EQ((int)state->status, WOW_QUEST_ACCEPTED);

    FOR_LOOP(i, 4) Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)state->kill_progress[0], 8);
    T_EQ((int)state->status, WOW_QUEST_COMPLETE);
}

TEST(wow_game, quest_kill_credit_only_on_accepted_quest) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->quest_count, 0);

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "788"});
    T_EQ((int)wc->quests[0].kill_progress[0], 0);
    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->quests[0].kill_progress[0], 0);
}

TEST(wow_game, quest_kill_credit_wrong_creature_no_progress) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept788[] = { "quest_accept", "788" };
    wowClient_t *wc;
    wowQuestState_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, accept788);
    state = Wow_FindQuestState(wc, 788);
    T_NOT_NULL(state);
    T_EQ((int)state->kill_progress[0], 0);

    Wow_QuestAwardKillCredit(player, 999);
    T_EQ((int)state->kill_progress[0], 0);

    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)state->kill_progress[0], 1);
}

TEST(wow_game, wow_load_map_initializes_player_state) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowEntityLocal_t *local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    local = Wow_EntityLocal(player);

    T_EQ((int)test_apply_lobby_calls, 1);
    T_EQ((int)test_clear_world_calls, 1);
    T_ASSERT(player->inuse);
    T_NOT_NULL(player->client);
    T_NOT_NULL(local);
    T_NULL(player->think); /* the player is driven by client input, not a think fn */
    T_EQ((int)local->health, 100);
    T_EQ((int)local->selected_action_slot, 255);
    assert_player_spawned(player);
    T_FEQ(player->client->ps.origin.x, player->s.origin.x, 0.001f);
    T_FEQ(player->client->ps.origin.y, player->s.origin.y, 0.001f);
    T_EQ((int)player->client->ps.client_ui_state, CLIENT_UI_LOADING);
    T_STREQ(player->client->ps.name, "Thrall");
    T_EQ((int)player->client->ps.stats[WOW_STAT_HEALTH], 100);
    T_EQ((int)player->client->ps.stats[WOW_STAT_HEALTH_MAX], 100);
    T_EQ((int)player->client->ps.stats[WOW_STAT_POWER], 100);
    T_EQ((int)player->client->ps.stats[WOW_STAT_SELECTED_ACTION], 255);
    T_EQ((int)test_num_images, 0);
    T_EQ((int)test_unicast_calls, 0);
    T_NOT_NULL(game->ClientBegin);
    game->ClientBegin(player);
    T_EQ((int)player->client->ps.client_ui_state, CLIENT_UI_GAME);
    T_ASSERT(test_unicast_calls > 0);
    assert_player_ui_payload();
    T_STREQ(player->client->ps.texts[PLAYERTEXT_MAP_TITLE], "Elwynn Test");
    T_STREQ(player->client->ps.texts[PLAYERTEXT_MAP_PREVIEW],
                  "Interface\\Glues\\LoadingScreens\\LoadScreenTest.blp");
    T_ASSERT(player->s.model > 0);
    T_ASSERT(player->s.model2 > 0);
    T_FEQ(player->s.angle, 0.0f, 0.001f);
    T_EQ((int)player->s.appearance,
                  (int)Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0));
    T_EQ((int)player->s.equipment,
                  (int)Wow_PackEquipment(1, 1, 1, 1));

    if (game->Shutdown) {
        game->Shutdown();
    }
}

/* Setting wow_playerinfo cvar to a different race places the player in that
   race's starting zone (Human → Northshire, index 0). */
TEST(wow_game, wow_load_map_spawns_at_race_zone_via_cvar) {
    struct game_export *game;

    game = init_game();
    snprintf(test_playerinfo, sizeof(test_playerinfo),
             "\\race\\Human\\sex\\Female\\class\\%u\\appearance\\0", (unsigned)WOW_CLASS_WARRIOR);
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    assert_player_spawned(&wow_edicts[0]);
    T_STREQ(wow_edicts[0].client->ps.name, "Thrall");
    T_EQ((int)wow_edicts[0].client->ps.start_location, 0);
    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, wow_load_map_spawns_and_runs_creature_state) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPEDICT creature;
    wowEntityLocal_t *creature_local;
    wowEntityLocal_t *player_local;
    VECTOR2 before;
    LPCSTR attack_argv[] = { "attack", "1" };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    T_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    player_local = Wow_EntityLocal(player);
    before = creature->s.origin2;

    T_EQ((int)creature->s.number, 1);
    T_ASSERT(creature->think == Wow_RunCreatureFrame);
    T_EQ((int)creature_local->display_id, 161);
    T_EQ((int)creature_local->health, 3);
    T_ASSERT((creature->svflags & SVF_MONSTER) != 0);
    T_ASSERT((creature->s.flags & EF_GROUND_ANCHOR) != 0);
    T_EQ((int)creature->s.player, 2);
    T_EQ((int)creature->s.class_id, 161);
    T_FEQ(creature->s.scale, 1.0f, 0.001f);
    T_FEQ(creature->s.radius, 1.5f, 0.001f);
    T_NOT_NULL(creature_local->animation);
    T_STREQ(creature_local->animation->name, "Walk");

    game->RunFrame();
    T_ASSERT(fabsf(creature->s.origin2.x - before.x) > 0.001f ||
           fabsf(creature->s.origin2.y - before.y) > 0.001f);

    game->ClientCommand(player, 2, attack_argv);
    T_EQ((int)(player_local->enemy ? player_local->enemy->s.number : 0), 1);
    T_EQ((int)player->client->ps.selected_entity, 1);

    /* Run frames until the player chases into melee range and starts swinging. */
    for (int i = 0; i < 300; i++) {
        game->RunFrame();
        if (player_local->attack_damage_time > 0) break;
    }
    T_ASSERT(player_local->attack_damage_time > 0);
    T_ASSERT(player_local->attack_backswing_time > 0);
    T_NOT_NULL(player_local->animation);
    T_STREQ(player_local->animation->name, "Attack1H");

    if (game->Shutdown) {
        game->Shutdown();
    }
}

/* Target selection is state-only; combat begins only from an explicit attack or action-bar command. */
TEST(wow_game, selecting_target_does_not_start_combat_or_chase) {
    struct game_export *game = init_game();
    LPEDICT player, creature;
    wowEntityLocal_t *local;
    VECTOR2 before;
    LPCSTR select_argv[] = { "select", "1" };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    local = Wow_EntityLocal(player);
    before = player->s.origin2;

    game->ClientCommand(player, 2, select_argv);
    game->RunFrame();

    T_EQ((int)player->client->ps.selected_entity, (int)creature->s.number);
    T_NULL(local->enemy);
    T_EQ((int)local->attack_time, 0);
    T_EQ((int)local->attack_damage_time, 0);
    T_EQ((int)local->attack_backswing_time, 0);
    T_FEQ(player->s.origin2.x, before.x, 0.001f);
    T_FEQ(player->s.origin2.y, before.y, 0.001f);
    if (game->Shutdown) game->Shutdown();
}

/* A cast must replace an active melee swing, hold the ready pose, then launch with the release pose. */
TEST(wow_game, wow_fireball_cast_interrupts_melee_and_launches) {
    struct game_export *game = init_game();
    LPEDICT player, creature, projectile = NULL;
    wowEntityLocal_t *local;
    LPCSTR action_argv[] = { "wow_action", "4" };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    local = Wow_EntityLocal(player);
    T_NOT_NULL(creature);
    creature->s.origin = (VECTOR3){ player->s.origin.x + 10.0f, player->s.origin.y, player->s.origin.z };
    creature->s.origin2 = (VECTOR2){ creature->s.origin.x, creature->s.origin.y };
    player->client->ps.selected_entity = creature->s.number;
    local->enemy = creature;
    local->attack_time = local->attack_damage_time = 500;
    local->attack_backswing_time = 500;

    game->ClientCommand(player, 2, action_argv);
    T_ASSERT(local->cast_spell != 0);
    T_EQ((int)local->attack_time, 0);
    T_STREQ(local->animation->name, "ReadySpellDirected");
    T_EQ((int)local->gcd_time, 1500);

    for (int i = 0; i < 15; i++) game->RunFrame();
    T_EQ((int)local->cast_spell, (int)SPELL_NONE);
    T_EQ((int)local->mana, 90);
    T_EQ((int)local->gcd_time, 0);
    T_ASSERT(local->cast_release_time > 0);
    T_NULL(local->enemy); /* Fireball is a one-shot ranged cast, not a melee engage. */
    T_STREQ(local->animation->name, "SpellCastDirected");
    FOR_LOOP(i, (DWORD)globals.num_edicts) {
        if (wow_edicts[i].inuse && wow_edicts[i].think == Wow_RunProjectile) {
            projectile = &wow_edicts[i];
            break;
        }
    }
    T_NOT_NULL(projectile);
    T_EQ((int)player->client->ps.stats[WOW_STAT_CAST_MAX], 0);
    if (game->Shutdown) game->Shutdown();
}

/* Moving after cast start interrupts without spending mana or creating a projectile. */
TEST(wow_game, wow_fireball_movement_cancels) {
    struct game_export *game = init_game();
    LPEDICT player, creature;
    wowEntityLocal_t *local;
    LPCSTR action_argv[] = { "wow_action", "4" };
    LPCSTR move_argv[] = { "move", "1", "0", "328", "8.5" };
    LPCSTR stop_argv[] = { "move", "0", "0", "328", "8.5" };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    local = Wow_EntityLocal(player);
    T_NOT_NULL(creature);
    creature->s.origin = (VECTOR3){ player->s.origin.x + 10.0f, player->s.origin.y, player->s.origin.z };
    creature->s.origin2 = (VECTOR2){ creature->s.origin.x, creature->s.origin.y };
    player->client->ps.selected_entity = creature->s.number;
    game->ClientCommand(player, 5, move_argv);
    game->ClientCommand(player, 2, action_argv);
    T_EQ((int)local->cast_spell, (int)SPELL_NONE);
    T_EQ((int)local->gcd_time, 0);
    game->ClientCommand(player, 5, stop_argv);
    game->ClientCommand(player, 2, action_argv);
    game->ClientCommand(player, 5, move_argv);
    game->RunFrame();

    T_EQ((int)local->cast_spell, (int)SPELL_NONE);
    T_EQ((int)local->mana, 100);
    T_EQ((int)player->client->ps.stats[WOW_STAT_CAST_MAX], 0);
    FOR_LOOP(i, (DWORD)globals.num_edicts) {
        T_ASSERT(!wow_edicts[i].inuse || wow_edicts[i].think != Wow_RunProjectile);
    }
    if (game->Shutdown) game->Shutdown();
}

/* Quest log rejects quests when full (WOW_MAX_QUEST_LOG slots). */
TEST(wow_game, quest_log_full_rejects_new_quests) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    DWORD available_quests[] = { 1, 8, 16, 47, 60, 62, 73, 83, 85, 106, 108, 117, 137, 176, 179, 182, 183 };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    FOR_LOOP(i, WOW_MAX_QUEST_LOG) {
        char id_buf[16];
        LPCSTR accept_args[2];
        snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)available_quests[i]);
        accept_args[0] = "quest_accept";
        accept_args[1] = id_buf;
        game->ClientCommand(player, 2, accept_args);
    }
    T_EQ((int)wc->quest_count, WOW_MAX_QUEST_LOG);

    /* Next accept should fail */
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "184"});
    T_EQ((int)wc->quest_count, WOW_MAX_QUEST_LOG);
    if (game->Shutdown) game->Shutdown();
}

/* Accepting the same quest twice is a no-op (idempotent). */
TEST(wow_game, quest_accept_same_quest_twice_is_idempotent) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    T_EQ((int)wc->quest_count, 1);
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    T_EQ((int)wc->quest_count, 1);
    if (game->Shutdown) game->Shutdown();
}

/* Kill credit does not exceed the required count. */
TEST(wow_game, quest_kill_credit_does_not_overflow) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    wowQuestState_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    state = Wow_FindQuestState(wc, 788);
    T_NOT_NULL(state);

    /* Kill 20 when only 8 required */
    FOR_LOOP(i, 20) Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)state->kill_progress[0], 8);
    T_EQ((int)state->status, WOW_QUEST_COMPLETE);
    if (game->Shutdown) game->Shutdown();
}

/* Accepting a quest with an invalid ID is harmless. */
TEST(wow_game, quest_accept_invalid_id_no_crash) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "99999"});
    T_EQ((int)wc->quest_count, 0);
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "0"});
    T_EQ((int)wc->quest_count, 0);
    if (game->Shutdown) game->Shutdown();
}

/* quest_complete on an unstarted quest does nothing. */
TEST(wow_game, quest_complete_without_accept_no_reward) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPPLAYER ps;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    ps = &player->client->ps;
    ps->stats[WOW_STAT_XP] = 0;
    ps->stats[WOW_STAT_COPPER] = 0;

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "788"});
    T_EQ((int)ps->stats[WOW_STAT_XP], 0);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);
    if (game->Shutdown) game->Shutdown();
}

/* Questlog toggle: first call opens, second call closes. */
TEST(wow_game, quest_log_toggle_open_close) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    BOOL found_header;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;
    T_EQ((int)wc->questlog_open, 0);

    game->ClientCommand(player, 1, (LPCSTR[]){"questlog"});
    T_EQ((int)wc->questlog_open, 1);

    test_ui_frame_count = 0;
    game->ClientCommand(player, 1, (LPCSTR[]){"questlog"});
    T_EQ((int)wc->questlog_open, 0);
    /* After closing, no quest log frames are emitted on LAYER_QUESTDIALOG */
    found_header = false;
    FOR_LOOP(i, test_ui_frame_count) {
        if (test_ui_frames[i].layer == LAYER_QUESTDIALOG &&
            !strcmp(test_ui_frames[i].text, "Quest Log"))
            found_header = true;
    }
    T_ASSERT(!found_header);
    if (game->Shutdown) game->Shutdown();
}

/* Quest dialog shows "Complete Quest" button only when status == WOW_QUEST_COMPLETE. */
TEST(wow_game, quest_dialog_shows_complete_button_only_when_done) {
    struct game_export *game = init_game();
    LPEDICT player;
    BOOL found_complete;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);

    /* Accept quest, then open dialog — should NOT show "Complete Quest" */
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (LPCSTR[]){"quest", "788"});
    found_complete = false;
    FOR_LOOP(i, test_ui_frame_count) {
        if (test_ui_frames[i].layer == LAYER_QUESTDIALOG &&
            !strcmp(test_ui_frames[i].text, "Complete Quest"))
            found_complete = true;
    }
    T_ASSERT(!found_complete);

    /* Complete kill objectives, reopen dialog — should show "Complete Quest" */
    FOR_LOOP(i, 8) Wow_QuestAwardKillCredit(player, 503);
    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (LPCSTR[]){"quest", "788"});
    found_complete = false;
    FOR_LOOP(i, test_ui_frame_count) {
        if (test_ui_frames[i].layer == LAYER_QUESTDIALOG &&
            !strcmp(test_ui_frames[i].text, "Complete Quest"))
            found_complete = true;
    }
    T_ASSERT(found_complete);
    if (game->Shutdown) game->Shutdown();
}

/* Interacting with a quest NPC opens the quest dialog via "quest" command with selected entity. */
TEST(wow_game, quest_open_via_selected_npc_entity) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    wowEntityLocal_t *npc_local;
    LPEDICT npc;
    BOOL found_title = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    /* Use first creature as a quest NPC by setting its quest_id field */
    npc = first_creature();
    T_NOT_NULL(npc);
    npc_local = Wow_EntityLocal(npc);
    npc_local->quest_id = 33;

    /* Select the NPC and issue bare "quest" command (no ID argument) */
    player->client->ps.selected_entity = npc->s.number;
    test_ui_frame_count = 0;
    game->ClientCommand(player, 1, (LPCSTR[]){"quest"});
    T_ASSERT(wc->quest_open);
    T_EQ((int)wc->quest_id, 33);

    /* Verify dialog has the quest title */
    FOR_LOOP(i, test_ui_frame_count) {
        if (test_ui_frames[i].layer == LAYER_QUESTDIALOG &&
            !strcmp(test_ui_frames[i].text, "Wolves Across the Border"))
            found_title = true;
    }
    T_ASSERT(found_title);
    if (game->Shutdown) game->Shutdown();
}

/* Quest chain: completing quest 12 unlocks 13, completing 13 unlocks 14. */
TEST(wow_game, quest_chain_sequential_unlock) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    LPPLAYER ps;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;
    ps = &player->client->ps;
    ps->stats[WOW_STAT_XP] = 0;
    ps->stats[WOW_STAT_COPPER] = 0;

    /* Quest 14 requires 13, which requires 12 */
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "14"});
    T_EQ((int)wc->quest_count, 0);

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "12"});
    T_EQ((int)wc->quest_count, 1);
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "12"});
    T_ASSERT(ps->stats[WOW_STAT_XP] > 0);

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "13"});
    T_EQ((int)wc->quest_count, 2);
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "13"});

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "14"});
    T_EQ((int)wc->quest_count, 3);
    T_EQ((int)wc->quests[2].quest_id, 14);
    T_EQ((int)wc->quests[2].status, WOW_QUEST_ACCEPTED);
    if (game->Shutdown) game->Shutdown();
}

/* Kill credit from combat: killing a creature via the combat system awards quest credit. */
TEST(wow_game, quest_kill_credit_from_combat_death) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    wowQuestState_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    /* Accept quest 788 which needs display_id 503 kills */
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    state = Wow_FindQuestState(wc, 788);
    T_NOT_NULL(state);
    T_EQ((int)state->kill_progress[0], 0);

    /* Simulate creature death (the AI death handler calls QuestAwardKillCredit) */
    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)state->kill_progress[0], 1);
    if (game->Shutdown) game->Shutdown();
}

/* Completing a quest that has already been rewarded does nothing. */
TEST(wow_game, quest_complete_already_rewarded_no_double_reward) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPPLAYER ps;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    ps = &player->client->ps;
    ps->stats[WOW_STAT_XP] = 0;
    ps->stats[WOW_STAT_COPPER] = 0;

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "788"});
    T_EQ((int)ps->stats[WOW_STAT_XP], 1020);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);

    /* Try completing again — should not award double rewards */
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "788"});
    T_EQ((int)ps->stats[WOW_STAT_XP], 1020);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);
    if (game->Shutdown) game->Shutdown();
}

/* Quest dialog progress text shows kill counts. */
TEST(wow_game, quest_dialog_shows_kill_progress_text) {
    struct game_export *game = init_game();
    LPEDICT player;
    BOOL found_progress = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    FOR_LOOP(i, 5) Wow_QuestAwardKillCredit(player, 503);

    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (LPCSTR[]){"quest", "788"});

    /* Find a text area frame that contains "5/8" progress indicator */
    FOR_LOOP(i, test_ui_frame_count) {
        if (test_ui_frames[i].layer == LAYER_QUESTDIALOG &&
            test_ui_frames[i].type == FT_TEXTAREA &&
            strstr(test_ui_frames[i].text, "5/8"))
            found_progress = true;
    }
    T_ASSERT(found_progress);
    if (game->Shutdown) game->Shutdown();
}

/* Strafe and backpedal preserve facing but select directional locomotion animations. */
TEST(wow_game, wow_directional_movement_animations) {
    struct game_export *game = init_game();
    LPEDICT player = &wow_edicts[0];
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    LPCANIMATION back_animation;
    LPCSTR left[] = { "move", "4", "0", "328", "8.5" };
    LPCSTR right[] = { "move", "8", "0", "328", "8.5" };
    LPCSTR back[] = { "move", "2", "0", "328", "8.5" };
    FLOAT facing;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    back_animation = G_GetAnimation(player->s.model, "WalkBackwards");
    facing = player->s.angle;
    game->ClientCommand(player, 5, left);
    game->RunFrame();
    T_STREQ(local->animation->name, "Run");
    T_FEQ(player->s.angle, facing, 0.001f);
    game->ClientCommand(player, 5, right);
    game->RunFrame();
    T_STREQ(local->animation->name, "Run");
    T_FEQ(player->s.angle, facing, 0.001f);
    game->ClientCommand(player, 5, back);
    game->RunFrame();
    T_STREQ(local->animation->name, back_animation ? "WalkBackwards" : "Run");
    T_FEQ(player->s.angle, facing, 0.001f);
    if (game->Shutdown) game->Shutdown();
}
