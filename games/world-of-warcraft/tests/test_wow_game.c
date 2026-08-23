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
    DWORD image_index;
    FLOAT x, y, w, h;
    COLOR32 color;
    RESOURCE font;
    BYTE uv[4];
    COLOR32 fontcolor;
    RESOURCE scroll_image[3];
    BYTE scroll_uv[4];
    DWORD payload_size;
} testUiFrame_t;

static testUiFrame_t test_ui_frames[256];
static DWORD test_ui_frame_count;
static BOOL test_expect_layout_layer;
static BYTE test_layout_layer;
static BOOL test_layout_seen[MAX_LAYOUT_LAYERS];

/* ---- configstring stubs (game_import.configstring / GetConfigstring) ---- */
#define TEST_CONFIGSTRINGS MAX_CONFIGSTRINGS
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

    *(DWORD *)data = ID_WDBC;
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
    DWORD displays[] = { 161, 193, 163, 188, 2072 }; /* 2072 = Deputy Willem (quest giver) */
    DWORD size;
    LPBYTE data = alloc_dbc(5, 5, 1, &size);
    LPBYTE records = data + 20;

    FOR_LOOP(i, 5) {
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
    LPBYTE data = alloc_dbc(5, 15, 160, &size);
    LPBYTE records = data + 20;
    LPBYTE strings = records + 5 * 15 * sizeof(DWORD);
    DWORD cursor = 1;

    FOR_LOOP(i, 5) {
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
            snprintf(capture->onclick, sizeof(capture->onclick), "%s", frame->onclick ? frame->onclick : "");
            capture->image_index = frame->tex.index;
            capture->x = frame->points.x[FPP_MIN].offset / UI_FRAMEPOINT_SCALE;
            capture->y = -frame->points.y[FPP_MIN].offset / UI_FRAMEPOINT_SCALE;
            capture->w = frame->size.width; capture->h = frame->size.height;
            capture->color = frame->color;
            if (frame->buffer.data && frame->flags.type == FT_STRING)
                capture->font = ((uiLabel_t const *)frame->buffer.data)->font;
            if (frame->buffer.data && frame->flags.type == FT_TEXTAREA)
                capture->font = ((uiTextArea_t const *)frame->buffer.data)->font;
            if (frame->buffer.data && frame->flags.type == FT_SIMPLEBUTTON) {
                uiSimpleButton_t const *button = frame->buffer.data;
                memcpy(capture->uv, button->normal.texcoord, sizeof(capture->uv));
                capture->fontcolor = button->normal.fontcolor;
            }
            if (frame->buffer.data && frame->flags.type == FT_SCROLLBAR) {
                uiScrollBarImage_t const *scroll = frame->buffer.data;
                FOR_LOOP(i, 3) capture->scroll_image[i] = scroll->image[i];
                memcpy(capture->scroll_uv, scroll->texcoord, sizeof(capture->scroll_uv));
                capture->payload_size = frame->buffer.size;
            }
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
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "Heroic Strike");
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
    T_STREQ((LPCSTR)test_last_unicast_buf + cursor, "Worn Knapsack");
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

    T_EQ((int)Wow_QuestGiverCount(), 1787);
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

TEST(wow_game, quest_givers_receive_creature_frame_for_idle_animation) {
    struct game_export *game = init_game();
    VECTOR2 origin = { -8947.64f, -132.319f }; /* Deputy Willem (entry 823, display 2072) */
    BOOL found = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame(); /* reset spawn budget */

    Wow_SpawnQuestLocations(&origin);

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT e = &wow_edicts[i];
        if (!e->inuse || e->s.class_id != 2072) continue;
        wowEntityLocal_t *local = Wow_EntityLocal(e);
        found = true;
        T_ASSERT(e->think == Wow_RunCreatureFrame);
        T_ASSERT(e->idle == Wow_AIIdle);
        T_ASSERT(e->svflags & SVF_MONSTER);
        T_NOT_NULL(local->animation);
        T_ASSERT(local->quest_available_model != 0);
        T_ASSERT(e->s.image >= CS_GENERAL);
        T_STREQ(test_configstrings[e->s.image], "Deputy Willem");
        if (local->animation) T_STREQ(local->animation->name, "Stand");
        break;
    }
    T_ASSERT(found);
    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, quest_marker_transitions_on_acceptance) {
    struct game_export *game = init_game();
    VECTOR2 origin = { -8947.64f, -132.319f };
    LPEDICT giver = NULL;
    entityState_t state;
    DWORD avail_model, active_idx;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();
    Wow_SpawnQuestLocations(&origin);
    FOR_LOOP(i, globals.num_edicts) {
        if (wow_edicts[i].inuse && wow_edicts[i].s.class_id == 2072) { giver = &wow_edicts[i]; break; }
    }
    T_NOT_NULL(giver);
    if (giver) {
        avail_model = (DWORD)test_model_index("Interface\\Buttons\\TalkToMe.m2");
        active_idx = (DWORD)test_image_index("Interface\\GossipFrame\\ActiveQuestIcon.blp");
        /* Before acceptance: the authoritative yellow "!" M2, not the GossipFrame BLP. */
        state = giver->s;
        game->CustomizeEntity(0, giver, &state);
        T_EQ((int)state.image, 0);
        T_EQ((int)state.overhead_sprite, 0);
        T_EQ((int)state.model2, (int)avail_model);
        T_ASSERT(state.renderfx & RF_ATTACH_OVERHEAD);
        /* Selection reveals the name, which makes the renderer stack the marker above attachment 18. */
        wow_clients[0].client.ps.selected_entity = giver->s.number;
        state = giver->s;
        game->CustomizeEntity(0, giver, &state);
        T_EQ((int)state.image, (int)giver->s.image);
        wow_clients[0].client.ps.selected_entity = 0;
        /* After acceptance: grey "?" (ActiveQuestIcon, no tint). */
        game->ClientCommand(&wow_edicts[0], 2, (LPCSTR[]){ "quest_accept", "783" });
        state = giver->s;
        game->CustomizeEntity(0, giver, &state);
        T_EQ((int)(state.overhead_sprite & 0x7fff), (int)active_idx);
        T_EQ((int)state.model2, 0);
        T_ASSERT(!(state.renderfx & RF_ATTACH_OVERHEAD));
        T_ASSERT(!(state.overhead_sprite & WOW_QUEST_SPRITE_TINT_FLAG));
    }
    if (game->Shutdown) game->Shutdown();
}

static LPCSTR test_image_name(DWORD index) {
    return index >= 1 && index <= test_num_images ? test_images[index - 1].name : NULL;
}

/* One physical Deputy Willem owns all matching queststarter rows; a fresh
 * Human resolves those rows to quest 783 and uses the client FrameXML metrics. */
TEST(wow_game, deputy_willem_opens_classic_first_human_quest_frame) {
    struct game_export *game = init_game();
    LPEDICT player, deputy = NULL;
    DWORD deputy_count = 0;
    BOOL found_npc = false, found_title = false, found_body = false;
    BOOL found_portrait = false, found_close = false, found_decline = false, found_scroll = false;
    char entnum[16];

    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\%u\\appearance\\0", (unsigned)WOW_CLASS_PALADIN);
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    game->RunFrame();
    Wow_SpawnQuestLocations(&(VECTOR2){ -8947.64f, -132.319f });
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &wow_edicts[i];
        if (!ent->inuse || ent->s.class_id != 2072) continue;
        deputy = ent; deputy_count++;
    }
    T_EQ((int)deputy_count, 1);
    T_NOT_NULL(deputy);
    if (!deputy) { if (game->Shutdown) game->Shutdown(); return; }

    test_ui_frame_count = 0;
    snprintf(entnum, sizeof(entnum), "%u", (unsigned)deputy->s.number);
    game->ClientCommand(player, 2, (LPCSTR[]){ "interact", entnum });
    T_EQ((int)((wowClient_t *)player->client)->quest_id, 783);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        LPCSTR image;
        if (frame->layer != LAYER_QUESTDIALOG) continue;
        if (!strcmp(frame->text, "Deputy Willem")) {
            found_npc = true;
            T_FEQ(frame->y, 127.0f / 768.0f, 0.0001f);
            T_FEQ(frame->h, 14.0f / 768.0f, 0.0001f);
        }
        if (!strcmp(frame->text, "A Threat Within")) {
            found_title = true;
            T_EQ((int)frame->font, 18);
            T_FEQ(frame->x, 28.0f / 1024.0f, 0.0001f);
            T_FEQ(frame->y, 195.0f / 768.0f, 0.0001f);
            T_EQ((int)frame->color.r, 0); T_EQ((int)frame->color.g, 0); T_EQ((int)frame->color.b, 0);
        }
        if (frame->type == FT_TEXTAREA && strstr(frame->text, "young paladin")) {
            found_body = true;
            T_EQ((int)frame->font, 13);
            T_FEQ(frame->x, 28.0f / 1024.0f, 0.0001f);
            T_EQ((int)frame->color.r, 0); T_EQ((int)frame->color.g, 0); T_EQ((int)frame->color.b, 0);
        }
        if (frame->type == FT_PORTRAIT && frame->image_index == deputy->s.model) found_portrait = true;
        if (!strcmp(frame->onclick, "quest_close") && frame->w == 32.0f / 1024.0f) found_close = true;
        if (!strcmp(frame->text, "Decline") && !strcmp(frame->onclick, "quest_close")) found_decline = true;
        if (!strcmp(frame->text, "Accept")) {
            T_EQ((int)frame->uv[0], 0); T_EQ((int)frame->uv[1], 159);
            T_EQ((int)frame->uv[2], 0); T_EQ((int)frame->uv[3], 175);
            T_EQ((int)frame->fontcolor.r, 255); T_EQ((int)frame->fontcolor.g, 209);
            T_EQ((int)frame->fontcolor.b, 0); T_EQ((int)frame->fontcolor.a, 255);
        }
        if (!strcmp(frame->text, "Decline")) {
            T_EQ((int)frame->fontcolor.r, 255); T_EQ((int)frame->fontcolor.g, 209);
            T_EQ((int)frame->fontcolor.b, 0); T_EQ((int)frame->fontcolor.a, 255);
        }
        if (frame->type == FT_SCROLLBAR) {
            found_scroll = true;
            T_FEQ(frame->x, 329.0f / 1024.0f, 0.0001f); T_FEQ(frame->y, 185.0f / 768.0f, 0.0001f);
            T_FEQ(frame->w, 16.0f / 1024.0f, 0.0001f); T_FEQ(frame->h, 334.0f / 768.0f, 0.0001f);
            T_FEQ(frame->w * UI_PIXEL_ASPECT, 16.0f / 768.0f, 0.0001f);
            T_EQ(frame->payload_size, sizeof(uiScrollBarImage_t)); T_EQ(frame->payload_size, 10);
            FOR_LOOP(j, 3) T_ASSERT(frame->scroll_image[j] > 0);
            T_EQ((int)frame->scroll_uv[0], 63); T_EQ((int)frame->scroll_uv[1], 191);
            T_EQ((int)frame->scroll_uv[2], 63); T_EQ((int)frame->scroll_uv[3], 191);
        }
        image = frame->type == FT_TEXTURE ? test_image_name(frame->image_index) : NULL;
        if (image && !strcmp(image, "Interface\\Buttons\\UI-Panel-MinimizeButton-Up.blp")) found_close = true;
    }
    T_ASSERT(found_npc); T_ASSERT(found_title); T_ASSERT(found_body);
    T_ASSERT(found_portrait); T_ASSERT(found_close); T_ASSERT(found_decline); T_ASSERT(found_scroll);
    if (game->Shutdown) game->Shutdown();
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

TEST(wow_game, hud_draws_race_portrait_on_console_layer) {
    struct game_export *game = init_game();
    LPEDICT player;
    BOOL found_portrait = false, found_minimap = false, found_level = false;
    DWORD status_bars = 0;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        LPCSTR name;
        if (frame->layer != LAYER_CONSOLE) continue;
        if (frame->type == FT_STRING && !strcmp(frame->text, "Lvl 1")) found_level = true;
        if (frame->type == FT_MINIMAP) {
            found_minimap = true;
            T_FEQ(frame->x, 867.0f/1024.0f, 0.0001f); T_FEQ(frame->y, 22.0f/768.0f, 0.0001f);
            T_FEQ(frame->w, 140.0f/1024.0f, 0.0001f); T_FEQ(frame->h, 140.0f/768.0f, 0.0001f);
            continue;
        }
        if (frame->type != FT_TEXTURE) continue;
        name = test_image_name(frame->image_index);
        if (name && !strcmp(name, "Interface\\CharacterFrame\\TemporaryPortrait-Male-Orc.blp")) found_portrait = true;
        if (name && !strcmp(name, "Interface\\TargetingFrame\\UI-StatusBar.blp")) {
            status_bars++;
            T_FEQ(frame->x, 87.0f/1024.0f, 0.0001f);
        }
    }
    T_ASSERT(found_portrait); T_ASSERT(found_minimap); T_ASSERT(found_level); T_EQ((int)status_bars, 2);
}

TEST(wow_game, quest_detail_has_full_text_and_rewards) {
    LPCWOWQUESTDETAIL detail = Wow_QuestDetail(7);
    LPCWOWQUESTDETAIL threat = Wow_QuestDetail(783);

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
    T_NOT_NULL(threat);
    T_ASSERT(strstr(threat->description, "young $c"));
    T_ASSERT(strstr(threat->description, "speak with my superior, Marshal McBride"));
    T_ASSERT(!strstr(threat->description, "..."));
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
    T_EQ((int)wc->client.ps.quest_count, 0);

    game->ClientCommand(player, 2, accept_command);
    T_EQ((int)wc->client.ps.quest_count, 1);
    T_EQ((int)wc->client.ps.quest_log[0].quest_id, 788);
    T_EQ((int)wc->client.ps.quest_log[0].status, SV_QUEST_ACTIVE);
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
    T_EQ((int)wc->client.ps.quest_count, 0);

    game->ClientCommand(player, 2, accept12);
    T_EQ((int)wc->client.ps.quest_count, 1);
    T_EQ((int)wc->client.ps.quest_log[0].quest_id, 12);

    game->ClientCommand(player, 2, accept13);
    T_EQ((int)wc->client.ps.quest_count, 1);
    game->ClientCommand(player, 2, (LPCSTR[]){ "quest_complete", "12" });
    game->ClientCommand(player, 2, accept13);
    T_EQ((int)wc->client.ps.quest_count, 2);
    T_EQ((int)wc->client.ps.quest_log[1].quest_id, 13);
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
    svQuestEntry_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, accept788);
    state = SV_QuestFind(wc->client.ps.quest_log, wc->client.ps.quest_count, 788);
    T_NOT_NULL(state);
    T_EQ((int)state->status, SV_QUEST_ACTIVE);
    T_EQ((int)wc->kill_progress[0][0], 0);

    FOR_LOOP(i, 4) Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->kill_progress[0][0], 4);
    T_EQ((int)state->status, SV_QUEST_ACTIVE);

    FOR_LOOP(i, 4) Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->kill_progress[0][0], 8);
    T_EQ((int)state->status, SV_QUEST_COMPLETE);
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
    T_EQ((int)wc->client.ps.quest_count, 0);

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "788"});
    T_EQ((int)wc->kill_progress[0][0], 0);
    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->kill_progress[0][0], 0);
}

TEST(wow_game, quest_kill_credit_wrong_creature_no_progress) {
    struct game_export *game = init_game();
    LPEDICT player;
    LPCSTR accept788[] = { "quest_accept", "788" };
    wowClient_t *wc;
    svQuestEntry_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, accept788);
    state = SV_QuestFind(wc->client.ps.quest_log, wc->client.ps.quest_count, 788);
    T_NOT_NULL(state);
    T_EQ((int)wc->kill_progress[0][0], 0);

    Wow_QuestAwardKillCredit(player, 999);
    T_EQ((int)wc->kill_progress[0][0], 0);

    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->kill_progress[0][0], 1);
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
    T_STREQ(player->client->ps.texts[PLAYERTEXT_MAP_PREVIEW], "Interface\\Glues\\LoadingScreens\\LoadScreenTest.blp");
    T_ASSERT(player->s.model > 0);
    T_ASSERT(player->s.model2 > 0);
    T_FEQ(player->s.angle, 0.0f, 0.001f);
    T_EQ((int)player->s.appearance, (int)Wow_PackAppearance(0, 0, 0, 0, 0, WOW_CLASS_WARRIOR, 0));
    T_EQ((int)player->s.equipment, (int)Wow_PackEquipment(1, 1, 1, 1));

    if (game->Shutdown) {
        game->Shutdown();
    }
}

/* A race/map mismatch falls back to any available spawn on the target map so that
 * dev workflows (+map N +warp X with an off-map character) work without rejection.
 * The spawn position is some map-valid location, not the character's racial home. */
TEST(wow_game, wow_load_map_falls_back_on_mismatched_playercreate_map) {
    struct game_export *game;

    game = init_game();
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Female\\class\\%u\\appearance\\0", (unsigned)WOW_CLASS_WARRIOR);
    /* test DBC has map_id=1 (Kalimdor); Human Warrior home is map=0 — mismatch */
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt")); /* succeeds via fallback */
    T_ASSERT(wow_edicts[0].inuse);                             /* player was spawned */
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
    T_ASSERT(fabsf(creature->s.origin2.x - before.x) > 0.001f || fabsf(creature->s.origin2.y - before.y) > 0.001f);

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

/* Quest log rejects quests when full (SV_MAX_QUEST_LOG slots). */
TEST(wow_game, quest_log_full_rejects_new_quests) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    DWORD available_quests[] = { 1, 8, 16, 47, 60, 62, 73, 83, 85, 106, 108, 117, 137, 176, 179, 182, 183 };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    FOR_LOOP(i, SV_MAX_QUEST_LOG) {
        char id_buf[16];
        LPCSTR accept_args[2];
        snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)available_quests[i]);
        accept_args[0] = "quest_accept";
        accept_args[1] = id_buf;
        game->ClientCommand(player, 2, accept_args);
    }
    T_EQ((int)wc->client.ps.quest_count, SV_MAX_QUEST_LOG);

    /* Next accept should fail */
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "184"});
    T_EQ((int)wc->client.ps.quest_count, SV_MAX_QUEST_LOG);
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
    T_EQ((int)wc->client.ps.quest_count, 1);
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    T_EQ((int)wc->client.ps.quest_count, 1);
    if (game->Shutdown) game->Shutdown();
}

/* Kill credit does not exceed the required count. */
TEST(wow_game, quest_kill_credit_does_not_overflow) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    svQuestEntry_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    state = SV_QuestFind(wc->client.ps.quest_log, wc->client.ps.quest_count, 788);
    T_NOT_NULL(state);

    /* Kill 20 when only 8 required */
    FOR_LOOP(i, 20) Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->kill_progress[0][0], 8);
    T_EQ((int)state->status, SV_QUEST_COMPLETE);
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
    T_EQ((int)wc->client.ps.quest_count, 0);
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "0"});
    T_EQ((int)wc->client.ps.quest_count, 0);
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

/* Quest dialog shows "Complete Quest" button only when status == SV_QUEST_COMPLETE. */
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

/* "interact N" on a quest NPC opens the quest dialog with title and Accept button. */
TEST(wow_game, quest_open_via_interact_command) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    wowEntityLocal_t *npc_local;
    LPEDICT npc;
    BOOL found_title = false;
    BOOL found_accept = false;
    char cmd_arg[16];

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    npc = first_creature();
    T_NOT_NULL(npc);
    npc_local = Wow_EntityLocal(npc);
    npc_local->quest_id = 33;

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    snprintf(cmd_arg, sizeof(cmd_arg), "%u", (unsigned)npc->s.number);
    game->ClientCommand(player, 2, (LPCSTR[]){"interact", cmd_arg});

    T_ASSERT(wc->quest_open);
    T_EQ((int)wc->quest_id, 33);
    T_ASSERT(test_layout_seen[LAYER_QUESTDIALOG]);

    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer != LAYER_QUESTDIALOG) continue;
        if (!strcmp(frame->text, "Wolves Across the Border")) found_title = true;
        if (!strncmp(frame->onclick, "quest_accept 33", 15)) found_accept = true;
    }
    T_ASSERT(found_title);
    T_ASSERT(found_accept);
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
    T_EQ((int)wc->client.ps.quest_count, 0);

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "12"});
    T_EQ((int)wc->client.ps.quest_count, 1);
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "12"});
    T_ASSERT(ps->stats[WOW_STAT_XP] > 0);

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "13"});
    T_EQ((int)wc->client.ps.quest_count, 2);
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_complete", "13"});

    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "14"});
    T_EQ((int)wc->client.ps.quest_count, 3);
    T_EQ((int)wc->client.ps.quest_log[2].quest_id, 14);
    T_EQ((int)wc->client.ps.quest_log[2].status, SV_QUEST_ACTIVE);
    if (game->Shutdown) game->Shutdown();
}

/* Kill credit from combat: killing a creature via the combat system awards quest credit. */
TEST(wow_game, quest_kill_credit_from_combat_death) {
    struct game_export *game = init_game();
    LPEDICT player;
    wowClient_t *wc;
    svQuestEntry_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    /* Accept quest 788 which needs display_id 503 kills */
    game->ClientCommand(player, 2, (LPCSTR[]){"quest_accept", "788"});
    state = SV_QuestFind(wc->client.ps.quest_log, wc->client.ps.quest_count, 788);
    T_NOT_NULL(state);
    T_EQ((int)wc->kill_progress[0][0], 0);

    /* Simulate creature death (the AI death handler calls QuestAwardKillCredit) */
    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->kill_progress[0][0], 1);
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

TEST(wow_game, playercreate_map_comes_from_spawn_table) {
    struct game_export *game = init_game();

    T_EQ(Wow_PlayerCreateMap("Human", WOW_CLASS_WARRIOR), 0u);
    T_EQ(Wow_PlayerCreateMap("Orc", WOW_CLASS_WARRIOR), 1u);
    T_EQ(Wow_PlayerCreateMap("NightElf", 11), 1u);
    T_EQ(Wow_PlayerCreateMap("Unknown", WOW_CLASS_WARRIOR), ~0u);
    T_NOT_NULL(game->PlayerCreateMap);
    T_EQ(game->PlayerCreateMap(), ~0u);
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Orc\\sex\\Male\\class\\1\\appearance\\0");
    T_EQ(game->PlayerCreateMap(), 1u);
    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, game_object_uses_authored_mddf_transform) {
    WOWDOODADDEF def = {
        .position = { 17598.289f, 90.646f, 14467.403f },
        .rotation = { 0.0f, 138.5f, 0.0f },
        .scale = 1863,
    };
    entityState_t state = { 0 };

    WowGo_SetDoodadTransform(&def, &state);
    T_FEQ(state.origin.x, 32.0f * WOW_ADT_SIZE - def.position[2], 0.001f);
    T_FEQ(state.origin.y, 32.0f * WOW_ADT_SIZE - def.position[0], 0.001f);
    T_FEQ(state.origin.z, def.position[1], 0.001f);
    T_FEQ(state.rotation.x, def.rotation[0], 0.001f);
    T_FEQ(state.rotation.y, def.rotation[1], 0.001f);
    T_FEQ(state.rotation.z, def.rotation[2], 0.001f);
    T_FEQ(state.scale, def.scale / 1024.0f, 0.001f);
    T_EQ(state.flags & EF_GROUND_ANCHOR, 0);
}

/* -------------------------------------------------------------------------
 * Loot system tests
 * -------------------------------------------------------------------------*/

/* Wow_RollLoot on a wolf entity always yields copper within the [10,40] table range. */
TEST(wow_game, loot_roll_wolf_copper_in_range) {
    struct game_export *game = init_game();
    LPEDICT creature;
    wowEntityLocal_t *local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(&wow_edicts[0]);
    creature = first_creature();
    T_NOT_NULL(creature);
    local = Wow_EntityLocal(creature);
    T_EQ((int)local->display_id, WOW_CREATURE_DISPLAY_WOLF);

    Wow_RollLoot(creature);
    T_ASSERT(local->loot_copper >= 10 && local->loot_copper <= 40);
    if (game->Shutdown) game->Shutdown();
}

/* loot command near a corpse snapshots items and auto-takes copper into player wallet. */
TEST(wow_game, loot_command_auto_takes_copper) {
    struct game_export *game = init_game();
    LPEDICT player = &wow_edicts[0], creature;
    wowClient_t *wc;
    wowEntityLocal_t *creature_local, *player_local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    creature = first_creature();
    T_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    player_local   = Wow_EntityLocal(player);
    wc             = (wowClient_t *)player->client;

    /* Kill the creature and bypass the death animation for test speed. */
    Wow_AIDie(creature, player);
    creature->think = Wow_RunCorpseFrame;
    /* Override loot to a deterministic copper value. */
    creature_local->loot_copper = 30;
    creature->s.origin2 = player->s.origin2;

    player_local->copper = 100;
    game->ClientCommand(player, 1, (LPCSTR[]){"loot"});

    T_EQ((int)player_local->copper, 130);      /* copper auto-looted on open */
    T_EQ((int)creature_local->loot_copper, 0); /* drained from corpse */
    T_EQ((int)wc->loot_target, (int)creature->s.number);
    if (game->Shutdown) game->Shutdown();
}

/* loot_take <slot> moves the item from the corpse snapshot into inventory. */
TEST(wow_game, loot_take_moves_item_to_inventory) {
    struct game_export *game = init_game();
    LPEDICT player = &wow_edicts[0], creature;
    wowClient_t *wc;
    wowEntityLocal_t *creature_local;
    DWORD inv_slot;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    creature = first_creature();
    T_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    wc             = (wowClient_t *)player->client;

    Wow_AIDie(creature, player);
    creature->think = Wow_RunCorpseFrame;
    /* One deterministic item, no copper, no other drops. */
    memset(creature_local->loot_items, 0, sizeof(creature_local->loot_items));
    creature_local->loot_count = 1;
    creature_local->loot_copper = 0;
    snprintf(creature_local->loot_items[0].icon, sizeof(creature_local->loot_items[0].icon),
             "%s", "Interface\\Icons\\INV_Misc_Food_52.blp");
    snprintf(creature_local->loot_items[0].name, sizeof(creature_local->loot_items[0].name),
             "%s", "Stringy Wolf Meat");
    creature_local->loot_items[0].count = 1;
    creature->s.origin2 = player->s.origin2;

    game->ClientCommand(player, 1, (LPCSTR[]){"loot"});
    T_STREQ(wc->loot_snap[0].name, "Stringy Wolf Meat");

    /* Record first empty inventory slot before take. */
    inv_slot = WOW_UI_INVENTORY_SLOTS;
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS)
        if (!wc->inventory[i].icon[0]) { inv_slot = i; break; }
    T_ASSERT(inv_slot < WOW_UI_INVENTORY_SLOTS);

    game->ClientCommand(player, 2, (LPCSTR[]){"loot_take", "0"});
    T_STREQ(wc->inventory[inv_slot].name, "Stringy Wolf Meat");
    T_EQ((int)wc->loot_snap[0].icon[0], 0); /* snapshot slot cleared */
    T_EQ((int)wc->loot_target, 0);           /* auto-closed: last item taken */
    if (game->Shutdown) game->Shutdown();
}

/* loot_close dismisses the loot window without taking any items. */
TEST(wow_game, loot_close_clears_window) {
    struct game_export *game = init_game();
    LPEDICT player = &wow_edicts[0], creature;
    wowClient_t *wc;
    wowEntityLocal_t *creature_local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    creature = first_creature();
    T_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    wc             = (wowClient_t *)player->client;

    Wow_AIDie(creature, player);
    creature->think = Wow_RunCorpseFrame;
    creature_local->loot_copper = 10;
    creature->s.origin2 = player->s.origin2;

    game->ClientCommand(player, 1, (LPCSTR[]){"loot"});
    T_ASSERT(wc->loot_target != 0);

    game->ClientCommand(player, 1, (LPCSTR[]){"loot_close"});
    T_EQ((int)wc->loot_target, 0);
    if (game->Shutdown) game->Shutdown();
}

/* -------------------------------------------------------------------------
 * Backpack window tests
 * -------------------------------------------------------------------------*/

/* backpack command toggles backpack_open each call. */
TEST(wow_game, backpack_toggles_open_closed) {
    struct game_export *game = init_game();
    LPEDICT player = &wow_edicts[0];
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    T_EQ((int)wc->backpack_open, 0);
    game->ClientCommand(player, 1, (LPCSTR[]){"backpack"});
    T_EQ((int)wc->backpack_open, 1);
    game->ClientCommand(player, 1, (LPCSTR[]){"backpack"});
    T_EQ((int)wc->backpack_open, 0);
    if (game->Shutdown) game->Shutdown();
}

/* -------------------------------------------------------------------------
 * Damage flash overlay tests
 * -------------------------------------------------------------------------*/

/* Damage dealt to the player sets the incoming flash timer. */
TEST(wow_game, damage_flash_incoming_set_on_player_hit) {
    struct game_export *game = init_game();
    LPEDICT player = &wow_edicts[0], creature;
    wowClient_t *wc;
    wowEntityLocal_t *player_local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    creature = first_creature();
    T_NOT_NULL(creature);
    wc = (wowClient_t *)player->client;
    player_local = Wow_EntityLocal(player);
    player_local->dead = false;
    player_local->health = 100;
    player_local->godmode = false;
    wc->incoming_dmg_timer = 0;
    wc->outgoing_dmg_timer = 0;

    Wow_ApplyDamage(player, creature, 5);
    T_EQ((int)wc->incoming_damage, 5);
    T_EQ((int)wc->incoming_dmg_timer, 1500);
    T_EQ((int)wc->outgoing_dmg_timer, 0); /* creature is not a client */
    if (game->Shutdown) game->Shutdown();
}

/* Damage dealt by the player to an enemy sets the outgoing flash timer. */
TEST(wow_game, damage_flash_outgoing_set_on_enemy_hit) {
    struct game_export *game = init_game();
    LPEDICT player = &wow_edicts[0], creature;
    wowClient_t *wc;
    wowEntityLocal_t *creature_local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    creature = first_creature();
    T_NOT_NULL(creature);
    wc = (wowClient_t *)player->client;
    creature_local = Wow_EntityLocal(creature);
    creature_local->dead = false;
    creature_local->health = 100;
    creature_local->godmode = false;
    wc->incoming_dmg_timer = 0;
    wc->outgoing_dmg_timer = 0;

    Wow_ApplyDamage(creature, player, 2);
    T_EQ((int)wc->outgoing_damage, 2);
    T_EQ((int)wc->outgoing_dmg_timer, 1500);
    T_EQ((int)wc->incoming_dmg_timer, 0); /* creature is not a client */
    if (game->Shutdown) game->Shutdown();
}
