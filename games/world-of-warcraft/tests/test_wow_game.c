#include "test.h"
#include "games/world-of-warcraft/common/wow_config.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "game/g_wow_local.h"
#include "game/g_character_save.h"
#include "game/g_landmarks.h"
#include "common/wow_coords.h"
#include "client/menu.h"


typedef struct {
    char name[MAX_PATHLEN];
    int index;
} testModel_t;

static testModel_t test_models[32];
static testModel_t test_images[128];
static uint32_t test_num_models;
static uint32_t test_num_images;
static uint32_t test_clear_world_calls;
static uint32_t test_apply_lobby_calls;
static uint8_t test_multicast_buf[MAX_MSGLEN];
static uint8_t test_last_unicast_buf[MAX_MSGLEN];
static uint32_t test_multicast_size;
static uint32_t test_last_unicast_size;
static uint32_t test_unicast_calls;
static uint32_t test_welcome_window_flags;
static uint8_t test_window_write_stage;
static bool test_welcome_window_seen;
static bool test_welcome_has_dismiss_button;
static uint8_t test_selection_buf[6];
static uint32_t test_selection_size;
static char test_last_error[512];
static char test_playerinfo[MAX_PATHLEN];
static bool test_worldmap_fixture;
static bool test_landmarks_fixture;
static edict_t *test_quest_giver;
static bool test_vendor_fixture;
static bool test_worldmap_many_zones;
static bool test_subzone_terrain;
static uint32_t test_game_time;

typedef struct {
    uint8_t layer;
    FRAMETYPE type;
    uint32_t stat;
    char text[512];
    char onclick[128];
    uint32_t image_index;
    float x, y, w, h;
    color32_t color;
    RESOURCE font;
    uint8_t uv[4];
    color32_t fontcolor;
    RESOURCE scroll_image[3];
    uint8_t scroll_uv[4];
    uint32_t payload_size;
    uint32_t message_id;
    uint8_t message_flags;
    uiCommandButton_t command_button;
    uint32_t flagsvalue;
} testUiFrame_t;

static testUiFrame_t test_ui_frames[256];
static uint32_t test_ui_frame_count;
static bool test_expect_layout_layer;
static uint8_t test_layout_layer;
static bool test_layout_seen[MAX_LAYOUT_LAYERS];

/* ---- configstring stubs (game_import.configstring / GetConfigstring) ---- */
#define TEST_CONFIGSTRINGS MAX_CONFIGSTRINGS
static char test_configstrings[TEST_CONFIGSTRINGS][512];

static void test_configstring(uint32_t index, cstring_t string) {
    if (index < TEST_CONFIGSTRINGS) {
        strncpy(test_configstrings[index], string ? string : "", sizeof(test_configstrings[index]) - 1);
        test_configstrings[index][sizeof(test_configstrings[index]) - 1] = '\0';
    }
}

static cstring_t test_get_configstring(uint32_t index) {
    if (index < TEST_CONFIGSTRINGS) {
        return test_configstrings[index];
    }
    return "";
}

/* ---- cvar stub ---- */
static cstring_t test_cvar_string(cstring_t name, cstring_t fallback) {
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

static void put32(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xff);
    out[1] = (uint8_t)((value >> 8) & 0xff);
    out[2] = (uint8_t)((value >> 16) & 0xff);
    out[3] = (uint8_t)((value >> 24) & 0xff);
}

static void putfloat(uint8_t *out, float value) {
    memcpy(out, &value, sizeof(value));
}

static void putfield(uint8_t *record, uint32_t field, uint32_t value) {
    put32(record + field * sizeof(uint32_t), value);
}

static void putfield_float(uint8_t *record, uint32_t field, float value) {
    putfloat(record + field * sizeof(uint32_t), value);
}

static handle_t alloc_dbc(uint32_t records, uint32_t fields, uint32_t string_size, uint32_t *size_out) {
    uint32_t record_size = fields * sizeof(uint32_t);
    uint32_t size = 20 + records * record_size + string_size;
    uint8_t *data = calloc(1, size);

    *(uint32_t *)data = ID_WDBC;
    put32(data + 4, records);
    put32(data + 8, fields);
    put32(data + 12, record_size);
    put32(data + 16, string_size);
    *size_out = size;
    return data;
}

static uint32_t add_string(uint8_t *strings, uint32_t *cursor, cstring_t value) {
    uint32_t offset = *cursor;
    uint32_t len = (uint32_t)strlen(value) + 1;

    memcpy(strings + offset, value, len);
    *cursor += len;
    return offset;
}

static handle_t make_map_dbc(uint32_t *size_out) {
    uint32_t size;
    uint8_t *data = alloc_dbc(1, 5, 64, &size);
    uint8_t *record = data + 20;
    uint8_t *strings = record + 5 * sizeof(uint32_t);
    uint32_t cursor = 1;
    uint32_t map_name = add_string(strings, &cursor, "Azeroth");
    uint32_t title = add_string(strings, &cursor, "Elwynn Test");

    putfield(record, 0, test_vendor_fixture ? 0 : 1);
    putfield(record, 1, map_name);
    putfield(record, 3, title);
    putfield(record, 4, 42);
    *size_out = size;
    return data;
}

static handle_t make_loading_screens_dbc(uint32_t *size_out) {
    uint32_t size;
    uint8_t *data = alloc_dbc(1, 3, 96, &size);
    uint8_t *record = data + 20;
    uint8_t *strings = record + 3 * sizeof(uint32_t);
    uint32_t cursor = 1;
    uint32_t texture = add_string(strings, &cursor, "Interface\\Glues\\LoadingScreens\\LoadScreenTest.blp");

    putfield(record, 0, 42);
    putfield(record, 2, texture);
    *size_out = size;
    return data;
}

static handle_t make_world_safe_locs_dbc(uint32_t *size_out) {
    static struct {
        uint32_t id;
        cstring_t name;
        float x, y, z;
    } const safe_locs[] = {
        { 100, "Northshire", 123.25f, -456.5f, 78.0f },
        { 101, "Deathknell, Tirisfal", 1880.7385f, 1624.7355f, 94.4343f },
        { 102, "Coldridge Valley", -6240.32f, 331.033f, 382.758f },
        { 103, "Valley of Trials", -600.0f, -4200.0f, 38.0f },
    };
    uint32_t size;
    uint8_t *data = alloc_dbc(sizeof(safe_locs) / sizeof(safe_locs[0]), 6, 128, &size);
    uint8_t *records = data + 20;
    uint8_t *strings = records + sizeof(safe_locs) / sizeof(safe_locs[0]) * 6 * sizeof(uint32_t);
    uint32_t cursor = 1;

    FOR_LOOP(i, sizeof(safe_locs) / sizeof(safe_locs[0])) {
        uint8_t *record = records + i * 6 * sizeof(uint32_t);
        uint32_t safe_name = add_string(strings, &cursor, safe_locs[i].name);

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

static handle_t make_creature_display_info_dbc(uint32_t *size_out) {
    uint32_t displays[] = { 161, 193, 163, 188, 2072, 1287, 1844 }; /* 2072 = Deputy Willem (quest giver) */
    uint32_t size;
    uint8_t *data = alloc_dbc(7, 5, 1, &size);
    uint8_t *records = data + 20;

    FOR_LOOP(i, 7) {
        uint8_t *record = records + i * 5 * sizeof(uint32_t);

        putfield(record, 0, displays[i]);
        putfield(record, 1, 700 + i);
        putfield_float(record, 4, 1.0f);
    }
    *size_out = size;
    return data;
}

static handle_t make_creature_model_data_dbc(uint32_t *size_out) {
    uint32_t size;
    uint8_t *data = alloc_dbc(7, 15, 224, &size);
    uint8_t *records = data + 20;
    uint8_t *strings = records + 7 * 15 * sizeof(uint32_t);
    uint32_t cursor = 1;

    FOR_LOOP(i, 7) {
        uint8_t *record = records + i * 15 * sizeof(uint32_t);
        char model_name[64];
        uint32_t model_offset;

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

static bool path_eq(cstring_t a, cstring_t b) {
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

/* Two adjacent ADT chunks have different AreaTable IDs. Their MCVT samples
 * are all zero, so this exercises discovery independently of a height change. */
static uint8_t *test_exploration_adt(uint32_t *size) {
    enum { vertex_count = 145, payload = 128 + 8 + vertex_count * 4, stride = 8 + payload };
    uint8_t *data = calloc(2, stride);
    for (uint32_t i = 0; i < 2; ++i) {
        uint8_t *chunk = data + i * stride;
        memcpy(chunk, "KNCM", 4);
        put32(chunk + 4, payload);
        uint8_t *header = chunk + 8;
        put32(header + 4, i);
        put32(header + 0x34, 710 + i);
        putfield_float(header + 0x68, 1, -(float)i * (100.0f / 3.0f));
        memcpy(header + 128, "TVCM", 4);
        put32(header + 132, vertex_count * 4);
    }
    *size = stride * 2;
    return data;
}

static handle_t test_read_file(cstring_t filename, uint32_t *size) {
    if (test_subzone_terrain && path_eq(filename, "World/Maps/Azeroth/Azeroth_32_32.adt"))
        return test_exploration_adt(size);
    if (test_worldmap_fixture && (path_eq(filename, "Interface\\WorldMap\\TestZone\\Valley2.blp") ||
                                  path_eq(filename, "Interface\\WorldMap\\TestZone\\Valley1.blp"))) {
        uint8_t *blp = calloc(1, 20);
        memcpy(blp, "BLP2", 4);
        putfield(blp, 3, 256);
        putfield(blp, 4, 128);
        *size = 20;
        return blp;
    }
    if (test_worldmap_fixture && path_eq(filename, "DBFilesClient\\WorldMapOverlay.dbc")) {
        uint8_t *data = alloc_dbc(1, 17, 32, size);
        uint8_t *record = data + 20;
        uint32_t cursor = 1;
        putfield(record, 1, 42);
        putfield(record, 2, 710);
        putfield(record, 8, add_string(record + 68, &cursor, "Valley"));
        putfield(record, 9, 315);
        putfield(record, 10, 128);
        putfield(record, 11, 56);
        putfield(record, 12, 74);
        return data;
    }
    if (test_worldmap_fixture && path_eq(filename, "DBFilesClient\\AreaTable.dbc")) {
        uint8_t *data = alloc_dbc(1, 21, 32, size);
        uint8_t *records = data + 20;
        uint8_t *strings = records + 84;
        uint32_t cursor = 1;
        putfield(records, 0, 14);
        putfield(records, 11, add_string(strings, &cursor, "Test Zone"));
        return data;
    }
    if (test_landmarks_fixture && path_eq(filename, "DBFilesClient\\AreaPOI.dbc")) {
        uint8_t *data = alloc_dbc(23, 19, 256, size);
        uint8_t *records = data + 20, *strings = records + 23 * 76;
        uint32_t cursor = 1;
        for (uint32_t i = 0; i < 23; ++i) {
            uint8_t *record = records + i * 76;
            char name[24];
            snprintf(name, sizeof(name), "Place %u", i);
            putfield(record, 0, i + 10);
            putfield_float(record, 4, (float)i);
            putfield_float(record, 5, (float)i);
            putfield(record, 7, i == 22 ? 0 : 1);
            putfield(record, i == 0 ? 11 : 10, add_string(strings, &cursor, name));
        }
        return data;
    }
    if (test_worldmap_fixture && path_eq(filename, "DBFilesClient\\WorldMapArea.dbc")) {
        uint32_t rows = test_worldmap_many_zones ? 23 : 2;
        uint8_t *data = alloc_dbc(rows, 8, 32, size);
        uint8_t *records = data + 20;
        uint8_t *strings = records + rows * 32;
        uint32_t cursor = 1;
        uint32_t continent = add_string(strings, &cursor, "Continent");
        uint32_t zone = add_string(strings, &cursor, "TestZone");
        putfield(records, 1, 1); putfield(records, 3, continent);
        putfield_float(records, 4, 1000); putfield_float(records, 5, -1000);
        putfield_float(records, 6, 1000); putfield_float(records, 7, -1000);
        putfield(records + 32, 0, 42);
        putfield(records + 32, 1, 1); putfield(records + 32, 3, zone);
        putfield(records + 32, 2, 14);
        putfield_float(records + 32, 4, 200); putfield_float(records + 32, 5, -200);
        putfield_float(records + 32, 6, 100); putfield_float(records + 32, 7, -100);
        for (uint32_t i = 2; i < rows; i++) {
            memcpy(records + i * 32, records + 32, 32);
            putfield(records + i * 32, 2, 14 + i);
        }
        return data;
    }
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

static handle_t test_mem_alloc(long size) {
    return calloc(1, (size_t)size);
}

static void test_mem_free(handle_t mem) {
    free(mem);
}

static int test_model_index(cstring_t model_name) {
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

static int test_image_index(cstring_t image_name) {
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

static int test_font_index(cstring_t font_name, uint32_t font_size) {
    (void)font_name;
    return (int)font_size;
}

static void test_clear_world(void) {
    test_clear_world_calls++;
}
static void test_loading_frame(void) {}

static void test_apply_lobby_settings(mapInfo_t *info) {
    test_apply_lobby_calls++;
    T_NOT_NULL(info);
}

static void test_error(cstring_t fmt, ...) {
    va_list args;

    va_start(args, fmt);
    vsnprintf(test_last_error, sizeof(test_last_error), fmt, args);
    va_end(args);
}

static uint32_t test_get_time(void) {
    return test_game_time;
}

static void test_write_data(void const *data, uint32_t size) {
    if (!data || test_multicast_size + size > sizeof(test_multicast_buf)) {
        return;
    }
    memcpy(test_multicast_buf + test_multicast_size, data, size);
    test_multicast_size += size;
}

static void test_write(pfWriteType_t type, void const *value) {
    uint8_t b;
    int16_t s;
    cstring_t text;

    switch (type) {
        case PF_BYTE:
            b = (uint8_t)*(int32_t const *)value;
            if (test_window_write_stage == 1) test_window_write_stage = 2;
            else if (test_window_write_stage) test_window_write_stage = 0;
            if (!test_window_write_stage && b == svc_window) test_window_write_stage = 1;
            if (b == svc_layout) test_expect_layout_layer = true;
            else if (test_expect_layout_layer) {
                test_layout_layer = b;
                test_expect_layout_layer = false;
                if (b < MAX_LAYOUT_LAYERS) test_layout_seen[b] = true;
            }
            test_write_data(&b, sizeof(b));
            break;
        case PF_SHORT:
            s = (int16_t)*(int32_t const *)value;
            test_write_data(&s, sizeof(s));
            break;
        case PF_LONG:
            if (test_window_write_stage == 2) test_window_write_stage = 3;
            else if (test_window_write_stage == 3) test_window_write_stage = 4;
            else if (test_window_write_stage == 4) {
                test_welcome_window_flags = *(uint32_t const *)value;
                test_welcome_window_seen = true;
                test_window_write_stage = 0;
            } else if (test_window_write_stage) test_window_write_stage = 0;
            test_write_data(value, sizeof(int32_t));
            break;
        case PF_STRING:
            text = value ? (cstring_t)value : "";
            test_write_data(text, (uint32_t)strlen(text) + 1);
            break;
        case PF_UIWINDOWFRAME: {
            uiFrame_t const *frame = value;
            if (frame && frame->flags.type == FT_SIMPLEBUTTON && frame->onclick)
                test_welcome_has_dismiss_button = true;
            break;
        }
        case PF_UIFRAME: {
            uiFrame_t const *frame = (uiFrame_t const *)value;
            testUiFrame_t *capture;
            if (!frame || test_ui_frame_count >= sizeof(test_ui_frames) / sizeof(test_ui_frames[0])) break;
            capture = &test_ui_frames[test_ui_frame_count++];
            capture->layer = test_layout_layer;
            capture->type = frame->flags.type;
            capture->stat = frame->stat;
            capture->flagsvalue = frame->flagsvalue;
            capture->command_button = (uiCommandButton_t){0};
            if (frame->flags.type == FT_COMMANDBUTTON &&
                frame->buffer.size == sizeof(capture->command_button))
                memcpy(&capture->command_button, frame->buffer.data, sizeof(capture->command_button));
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
            if (frame->buffer.data && frame->flags.type == FT_MESSAGE_QUEUE) {
                uiMessageQueue_t const *message = frame->buffer.data;
                capture->payload_size = frame->buffer.size;
                capture->message_id = message->message_id;
                capture->message_flags = message->flags;
            }
            break;
        }
        default:
            break;
    }
}

static void test_unicast(edict_t *ent) {
    (void)ent;
    test_unicast_calls++;
    if (test_multicast_size && test_multicast_buf[0] == svc_set_selection) {
        test_selection_size = test_multicast_size;
        if (test_selection_size <= sizeof(test_selection_buf))
            memcpy(test_selection_buf, test_multicast_buf, test_selection_size);
    }
    /* Keep the gameplay payload available: server-authored layout packets are separate messages. */
    if (test_multicast_size && test_multicast_buf[0] == svc_unit_ui) {
        test_last_unicast_size = test_multicast_size;
        memcpy(test_last_unicast_buf, test_multicast_buf, test_last_unicast_size);
    }
    test_multicast_size = 0;
}

static void test_save_path(cstring_t rel, string_t out, uint32_t size) {
    snprintf(out, size, "%s", Test_TempPath(rel));
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
    import.GetTime = test_get_time;
    import.ClearWorld = test_clear_world;
    import.LoadingFrame = test_loading_frame;
    import.ApplyLobbySettings = test_apply_lobby_settings;
    import.configstring = test_configstring;
    import.GetConfigstring = test_get_configstring;
    import.CvarString = test_cvar_string;
    import.SavePath = test_save_path;
    import.Write = test_write;
    import.unicast = test_unicast;
    import.error = test_error;
    return import;
}

static edict_t *first_creature(void) {
    for (uint32_t i = MAX_CLIENTS; i < (uint32_t)globals.num_edicts; i++) {
        if (wow_edicts[i].inuse && Wow_EntityLocal(&wow_edicts[i])->think == Wow_RunCreatureFrame) {
            return &wow_edicts[i];
        }
    }
    return NULL;
}

static void test_finish_quest_kills(edict_t *player, uint32_t quest_id) {
    wowQuestDetail_t const *detail = Wow_QuestDetail(quest_id);
    if (!detail) return;
    FOR_LOOP(i, detail->kill_objective_count)
        FOR_LOOP(j, detail->kill_objectives[i].required_count)
            Wow_QuestAwardKillCredit(player, detail->kill_objectives[i].display_id);
}

int G_RegisterModel(cstring_t filename) {
    return gi.ModelIndex(filename);
}

animation_t const *G_GetAnimation(uint32_t modelindex, cstring_t animname) {
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

float G_GetAttachmentZ(uint32_t modelindex, int aid) {
    (void)modelindex;
    (void)aid;
    return 0.0f;
}

void PF_TextRemoveComments(string_t buffer) {
    (void)buffer;
}

static void reset_test_state(void) {
    test_worldmap_fixture = false;
    test_landmarks_fixture = false;
    test_quest_giver = NULL;
    test_vendor_fixture = false;
    test_worldmap_many_zones = false;
    test_subzone_terrain = false;
    test_game_time = 0;
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
    test_welcome_window_flags = 0;
    test_window_write_stage = 0;
    test_welcome_window_seen = false;
    test_welcome_has_dismiss_button = false;
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
    bool action = false, inventory = false;
    uint32_t attack = test_image_index("Interface\\Icons\\Ability_Warrior_Cleave.blp");
    uint32_t bag = test_image_index("Interface\\Icons\\INV_Misc_Bag_08.blp");
    T_EQ(test_last_unicast_size, 0); /* No obsolete svc_unit_ui emission. */
    T_ASSERT(test_layout_seen[LAYER_CONSOLE]);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer == LAYER_COMMANDBAR && frame->type == FT_COMMANDBUTTON)
            action |= frame->image_index == attack;
        if (frame->layer == LAYER_CONSOLE && frame->type == FT_TEXTURE)
            inventory |= frame->image_index == bag;
    }
    T_ASSERT(action); T_ASSERT(inventory);
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

/* Older quest-state tests exercise progression independently of NPC spawning.
 * Give those tests a nearby synthetic giver and use the same commands as play. */
static void test_command_with_quest_giver(struct game_export *game, edict_t *player,
                               uint32_t argc, cstring_t argv[]) {
    bool transaction = argc >= 2 && (!strcasecmp(argv[0], "quest_accept") ||
                                    !strcasecmp(argv[0], "quest_complete"));
    bool dialog = argc >= 2 && !strcasecmp(argv[0], "quest");
    if (transaction || dialog) {
        uint32_t id = (uint32_t)strtoul(argv[1], NULL, 10);
        edict_t *giver = test_quest_giver;
        if (!giver) {
            giver = Wow_Spawn();
            if (!giver) { game->RunFrame(); giver = Wow_Spawn(); }
            test_quest_giver = giver;
        }
        if (giver) {
            wowEntityLocal_t *local = Wow_EntityLocal(giver);
            local->quest_id = id;
            local->home = player->s.origin2;
            giver->s.flags |= EF_HAS_QUEST;
            giver->s.origin2 = player->s.origin2;
            ((wowClient_t *)player->client)->selected_entity = giver->s.number;
            if (transaction) game->ClientCommand(player, 2, (cstring_t[]){ "quest", argv[1] });
        }
    }
    game->ClientCommand(player, argc, argv);
}

/* Verify the player spawned at a valid spawn-table position. */
static void assert_player_spawned(edict_t *player) {
    T_EQ((int)(player->client->ps.start_location != -1), 1);
    T_ASSERT(player->s.origin.x != 0.0f || player->s.origin.y != 0.0f);
}

TEST(wow_game, starter_weapon_damage_comes_from_serverdata) {
    wowWeapon_t const *weapon = Wow_WeaponByEntry(WOW_START_WEAPON_ENTRY);
    uint32_t damage;

    T_NOT_NULL(weapon);
    T_STREQ(weapon->name, "Worn Axe");
    T_FEQ(weapon->damage_min, 1.0f, 0.001f);
    T_FEQ(weapon->damage_max, 3.0f, 0.001f);
    T_EQ((int)weapon->delay, 2000);
    damage = Wow_RollWeaponDamage(WOW_START_WEAPON_ENTRY);
    T_ASSERT(damage >= 1 && damage <= 3);
}

TEST(wow_game, quest_serverdata_contains_givers_and_objective_locations) {
    wowQuestGiver_t const *giver = Wow_QuestGiver(2);
    wowQuestObjective_t const *objective = Wow_QuestObjective(1);

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

TEST(wow_game, quest_giver_group_index_returns_only_one_physical_npc_rows) {
    vec2_t deputy = { -8947.64f, -132.319f };
    vec2_t missing = { 1.0f, 2.0f };
    uint32_t group = Wow_QuestGiverGroup(6, &deputy);
    uint32_t expected[] = { 6, 18, 783, 3903, 5261 };

    T_ASSERT(group != WOW_QUEST_GIVER_GROUP_NONE);
    T_EQ((int)Wow_QuestGiverGroupCount(group), 5);
    FOR_LOOP(i, sizeof(expected) / sizeof(*expected)) T_EQ((int)Wow_QuestGiverInGroup(group, i)->quest_id, (int)expected[i]);
    T_EQ((int)Wow_QuestGiverGroup(6, &missing), (int)WOW_QUEST_GIVER_GROUP_NONE);
    T_ASSERT(Wow_QuestGiverInGroup(group, 5) == NULL);
}

TEST(wow_game, creature_serverdata_preserves_templates_and_all_models) {
    wowCreature_t const *marshal = Wow_CreatureByEntry(197);
    wowCreature_t const *deputy = Wow_CreatureByEntry(823);
    wowCreature_t const *defias = Wow_CreatureByEntry(824);
    wowCreature_t const *sparse = Wow_CreatureByEntry(34166);

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
    vec2_t origin = { -8947.64f, -132.319f }; /* Deputy Willem (entry 823, display 2072) */
    bool found = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame(); /* reset spawn budget */

    Wow_SpawnQuestLocations(&origin);

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *e = &wow_edicts[i];
        if (!e->inuse || e->s.class_id != 2072) continue;
        wowEntityLocal_t *local = Wow_EntityLocal(e);
        found = true;
        T_ASSERT(local->think == Wow_RunCreatureFrame);
        T_ASSERT(local->idle == Wow_AIIdle);
        T_ASSERT(e->svflags & SVF_MONSTER);
        T_NOT_NULL(local->animation);
        T_ASSERT(local->quest_available_model != 0);
        T_ASSERT(e->s.name != 0);
        T_ASSERT(entity_name_slot_equals(test_configstrings[CS_GENERAL + (e->s.name - 1) / ENT_NAMES_PER_CS] + ((e->s.name - 1) % ENT_NAMES_PER_CS) * ENT_NAME_SLOT_SIZE, "Deputy Willem"));
        if (local->animation) T_STREQ(local->animation->name, "Stand");
        break;
    }
    T_ASSERT(found);
    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, quest_marker_transitions_on_acceptance) {
    struct game_export *game = init_game();
    vec2_t origin = { -8947.64f, -132.319f };
    edict_t *giver = NULL;
    entityState_t state;
    uint32_t avail_model;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();
    Wow_SpawnQuestLocations(&origin);
    FOR_LOOP(i, globals.num_edicts) {
        if (wow_edicts[i].inuse && wow_edicts[i].s.class_id == 2072) { giver = &wow_edicts[i]; break; }
    }
    T_NOT_NULL(giver);
    if (giver) {
        avail_model = (uint32_t)test_model_index("Interface\\Buttons\\TalkToMe.m2");
        /* Before acceptance: the authoritative yellow "!" M2, EF_HAS_QUEST cleared for this client. */
        state = giver->s;
        game->CustomizeEntity(0, giver, &state);
        T_EQ((int)state.name, 0);
        T_ASSERT(!(state.flags & EF_HAS_QUEST));
        T_EQ((int)state.model2, (int)avail_model);
        T_ASSERT(state.renderfx & RF_ATTACH_OVERHEAD);
        /* Selection reveals the name, which makes the renderer stack the marker above attachment 18. */
        wow_clients[0].selected_entity = giver->s.number;
        state = giver->s;
        game->CustomizeEntity(0, giver, &state);
        T_EQ((int)state.name, (int)giver->s.name);
        wow_clients[0].selected_entity = 0;
        /* After acceptance: grey "?" flag, no tint. */
        wow_edicts[0].s.origin2 = giver->s.origin2;
        char giver_id[32];
        snprintf(giver_id, sizeof(giver_id), "%u", giver->s.number);
        game->ClientCommand(&wow_edicts[0], 2, (cstring_t[]){ "interact", giver_id });
        game->ClientCommand(&wow_edicts[0], 2, (cstring_t[]){ "quest_accept", "783" });
        state = giver->s;
        game->CustomizeEntity(0, giver, &state);
        T_ASSERT(state.flags & EF_HAS_QUEST);
        T_EQ((int)state.model2, 0);
        T_ASSERT(!(state.renderfx & RF_ATTACH_OVERHEAD));
        T_ASSERT(!(state.flags & EF_QUEST_COMPLETE));
    }
    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, customize_entity_authors_live_creature_hover_vitals) {
    struct game_export *game = init_game();
    edict_t *ent = &wow_edicts[MAX_CLIENTS];
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    entityState_t state;

    ent->inuse = true; ent->svflags = SVF_MONSTER; ent->s.number = MAX_CLIENTS; ent->s.model = 1;
    local->health = 50; local->mana = 100;
    state = ent->s;
    game->CustomizeEntity(0, ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_EQ((int)state.stats[ENT_HEALTH], 127); T_EQ((int)state.stats[ENT_MANA], 255);
    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, customize_entity_clears_dead_creature_hover_vitals) {
    struct game_export *game = init_game();
    edict_t *ent = &wow_edicts[MAX_CLIENTS];
    wowEntityLocal_t *local = Wow_EntityLocal(ent);
    entityState_t state;

    ent->inuse = true; ent->svflags = SVF_MONSTER | SVF_DEADMONSTER; ent->s.number = MAX_CLIENTS; ent->s.model = 1;
    local->health = 50; local->dead = true;
    state = ent->s; state.flags |= EF_HOVER_HEALTH;
    state.stats[ENT_HEALTH] = state.stats[ENT_MANA] = 255;
    game->CustomizeEntity(0, ent, &state);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
    T_EQ((int)state.stats[ENT_HEALTH], 0); T_EQ((int)state.stats[ENT_MANA], 0);
    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, client_begin_sends_server_authored_hover_context) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    bool name = false, health = false, mana = false;

    game->ClientBegin(player);
    T_ASSERT(test_layout_seen[LAYER_WORLD_HOVER]);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer != LAYER_WORLD_HOVER) continue;
        name |= frame->type == FT_STRING && frame->stat == UI_STAT_CONTEXT_NAME;
        health |= frame->type == FT_SIMPLESTATUSBAR && frame->stat == UI_STAT_CONTEXT_HEALTH;
        mana |= frame->type == FT_SIMPLESTATUSBAR && frame->stat == UI_STAT_CONTEXT_MANA;
    }
    T_ASSERT(name); T_ASSERT(health); T_ASSERT(mana);
    if (game->Shutdown) game->Shutdown();
}

static cstring_t test_image_name(uint32_t index) {
    return index >= 1 && index <= test_num_images ? test_images[index - 1].name : NULL;
}

/* One physical Deputy Willem owns all matching queststarter rows; a fresh
 * Human resolves those rows to quest 783 and uses the client FrameXML metrics. */
TEST(wow_game, deputy_willem_opens_classic_first_human_quest_frame) {
    struct game_export *game = init_game();
    edict_t *player, *deputy = NULL;
    uint32_t deputy_count = 0;
    bool found_npc = false, found_title = false, found_body = false;
    bool found_portrait = false, found_close = false, found_decline = false, found_scroll = false;
    char entnum[16];

    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\%u\\appearance\\0", (unsigned)WOW_CLASS_PALADIN);
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    game->RunFrame();
    Wow_SpawnQuestLocations(&(vec2_t){ -8947.64f, -132.319f });
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &wow_edicts[i];
        if (!ent->inuse || ent->s.class_id != 2072) continue;
        deputy = ent; deputy_count++;
    }
    T_EQ((int)deputy_count, 1);
    T_NOT_NULL(deputy);
    if (!deputy) { if (game->Shutdown) game->Shutdown(); return; }

    player->s.origin2 = deputy->s.origin2;
    test_ui_frame_count = 0;
    snprintf(entnum, sizeof(entnum), "%u", (unsigned)deputy->s.number);
    game->ClientCommand(player, 2, (cstring_t[]){ "interact", entnum });
    T_EQ((int)((wowClient_t *)player->client)->quest_id, 783);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        cstring_t image;
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
    edict_t *player;
    cstring_t open_command[] = { "quest", "7" };
    cstring_t close_command[] = { "quest_close" };
    bool found_quest_button = false;
    bool found_quest_title = false;
    bool found_accept = false;

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
    test_command_with_quest_giver(game, player, 2, open_command);
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
    edict_t *player;
    bool found_portrait = false, found_minimap = false, found_level = false;
    uint32_t status_bars = 0;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        cstring_t name;
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
    wowQuestDetail_t const *detail = Wow_QuestDetail(7);
    wowQuestDetail_t const *threat = Wow_QuestDetail(783);

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
    edict_t *player;
    cstring_t accept_command[] = { "quest_accept", "788" };
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;
    T_EQ((int)wc->quest_count, 0);

    test_command_with_quest_giver(game, player, 2, accept_command);
    T_EQ((int)wc->quest_count, 1);
    T_EQ((int)wc->quest_log[0].quest_id, 788);
    T_EQ((int)wc->quest_log[0].status, SV_QUEST_ACTIVE);
}

TEST(wow_game, quest_prerequisite_blocks_accept) {
    struct game_export *game = init_game();
    edict_t *player;
    cstring_t accept13[] = { "quest_accept", "13" };
    cstring_t accept12[] = { "quest_accept", "12" };
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    test_command_with_quest_giver(game, player, 2, accept13);
    T_EQ((int)wc->quest_count, 0);

    test_command_with_quest_giver(game, player, 2, accept12);
    T_EQ((int)wc->quest_count, 1);
    T_EQ((int)wc->quest_log[0].quest_id, 12);

    test_command_with_quest_giver(game, player, 2, accept13);
    T_EQ((int)wc->quest_count, 1);
    test_finish_quest_kills(player, 12);
    test_command_with_quest_giver(game, player, 2, accept13);
    T_EQ((int)wc->quest_count, 1);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_complete", "12" });
    test_command_with_quest_giver(game, player, 2, accept13);
    T_EQ((int)wc->quest_count, 1);
    T_EQ((int)wc->quest_log[0].quest_id, 13);
}

TEST(wow_game, quest_complete_delivers_rewards) {
    struct game_export *game = init_game();
    edict_t *player;
    cstring_t accept_command[] = { "quest_accept", "788" };
    cstring_t complete_command[] = { "quest_complete", "788" };
    player_t *ps;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    ps = &player->client->ps;
    Wow_EntityLocal(player)->experience = 0;
    Wow_EntityLocal(player)->copper = 0;
    ps->stats[WOW_STAT_XP] = 0;
    ps->stats[WOW_STAT_COPPER] = 0;

    test_command_with_quest_giver(game, player, 2, accept_command);
    T_EQ((int)ps->stats[WOW_STAT_XP], 0);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);

    test_finish_quest_kills(player, 788);
    test_command_with_quest_giver(game, player, 2, complete_command);
    T_EQ((int)ps->stats[WOW_STAT_XP], 1020);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);
}

TEST(wow_game, quest_completion_delivers_server_message_queue) {
    struct game_export *game = init_game();
    edict_t *player;
    cstring_t accept_command[] = { "quest_accept", "788" };
    cstring_t complete_command[] = { "quest_complete", "788" };
    bool found = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    test_command_with_quest_giver(game, player, 2, accept_command);
    test_finish_quest_kills(player, 788);
    test_ui_frame_count = 0;
    test_command_with_quest_giver(game, player, 2, complete_command);
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer == LAYER_MESSAGE && frame->type == FT_MESSAGE_QUEUE && frame->message_id == 788) {
            found = true;
            T_EQ(frame->message_flags, UI_MESSAGE_UNREAD);
            T_STREQ(frame->text, "Quest complete");
            T_STREQ(frame->onclick, "message_open 788");
        }
    }
    T_ASSERT(found);
}

TEST(wow_game, message_open_and_close_are_server_owned) {
    struct game_export *game = init_game();
    edict_t *player;
    cstring_t accept_command[] = { "quest_accept", "788" };
    cstring_t complete_command[] = { "quest_complete", "788" };
    cstring_t open_command[] = { "message_open", "788" };
    cstring_t close_command[] = { "message_close" };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    test_command_with_quest_giver(game, player, 2, accept_command);
    test_finish_quest_kills(player, 788);
    test_command_with_quest_giver(game, player, 2, complete_command);
    test_command_with_quest_giver(game, player, 2, open_command);
    T_EQ((int)((wowClient_t *)player->client)->message_open_id, 788);
    T_EQ((int)((wowClient_t *)player->client)->messages[0].flags, 0);
    game->ClientCommand(player, 1, close_command);
    T_EQ((int)((wowClient_t *)player->client)->message_open_id, 0);
}

TEST(wow_game, quest_turn_in_flow_accept_complete_reward) {
    struct game_export *game = init_game();
    edict_t *player;
    cstring_t accept788[] = { "quest_accept", "788" };
    cstring_t open788[] = { "quest", "788" };
    player_t *ps;
    bool found_complete = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    ps = &player->client->ps;
    Wow_EntityLocal(player)->experience = 0;
    Wow_EntityLocal(player)->copper = 0;

    test_command_with_quest_giver(game, player, 2, accept788);
    test_command_with_quest_giver(game, player, 2, open788);
    /* Dialog shows accepted quest — no accept button, but close button exists */
    FOR_LOOP(i, test_ui_frame_count) {
        if (test_ui_frames[i].layer == LAYER_QUESTDIALOG &&
            !strcmp(test_ui_frames[i].onclick, "quest_close")) found_complete = true;
    }
    T_ASSERT(found_complete);

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    test_finish_quest_kills(player, 788);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_complete", "788"});
    T_EQ((int)ps->stats[WOW_STAT_XP], 1020);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);
}

TEST(wow_game, quest_log_shows_active_and_complete_quests) {
    struct game_export *game = init_game();
    edict_t *player;
    cstring_t accept788[] = { "quest_accept", "788" };
    cstring_t questlog_cmd[] = { "questlog" };
    bool found_header = false, found_title = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    test_command_with_quest_giver(game, player, 2, accept788);

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
    game->ClientCommand(player, 1, (cstring_t[]){"quest_close"});
    T_ASSERT(test_layout_seen[LAYER_QUESTDIALOG]);
    FOR_LOOP(i, test_ui_frame_count)
        T_ASSERT(test_ui_frames[i].layer != LAYER_QUESTDIALOG);
}

TEST(wow_game, quest_kill_progress_increments_and_auto_completes) {
    struct game_export *game = init_game();
    edict_t *player;
    cstring_t accept788[] = { "quest_accept", "788" };
    wowClient_t *wc;
    svQuestEntry_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    test_command_with_quest_giver(game, player, 2, accept788);
    state = SV_QuestFind(wc->quest_log, wc->quest_count, 788);
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
    edict_t *player;
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->quest_count, 0);

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "788"});
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_complete", "788"});
    T_EQ((int)wc->kill_progress[0][0], 0);
    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->kill_progress[0][0], 1);
}

TEST(wow_game, quest_kill_credit_wrong_creature_no_progress) {
    struct game_export *game = init_game();
    edict_t *player;
    cstring_t accept788[] = { "quest_accept", "788" };
    wowClient_t *wc;
    svQuestEntry_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    test_command_with_quest_giver(game, player, 2, accept788);
    state = SV_QuestFind(wc->quest_log, wc->quest_count, 788);
    T_NOT_NULL(state);
    T_EQ((int)wc->kill_progress[0][0], 0);

    Wow_QuestAwardKillCredit(player, 999);
    T_EQ((int)wc->kill_progress[0][0], 0);

    Wow_QuestAwardKillCredit(player, 503);
    T_EQ((int)wc->kill_progress[0][0], 1);
}

TEST(wow_game, welcome_window_does_not_block_gameplay_input) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(&wow_edicts[0]);
    T_ASSERT(test_welcome_window_seen);
    T_ASSERT(test_welcome_has_dismiss_button);
    T_ASSERT(!(test_welcome_window_flags & UI_WINDOW_MODAL));
    game->Shutdown();
}

TEST(wow_game, wow_load_map_initializes_player_state) {
    struct game_export *game = init_game();
    edict_t *player;
    wowEntityLocal_t *local;

    T_NOT_NULL(game->PrepareMap);
    T_ASSERT(game->PrepareMap("World/Maps/Azeroth/Azeroth.wdt"));
    T_EQ(test_clear_world_calls, 0);
    T_EQ(test_unicast_calls, 0);
    T_ASSERT(test_layout_seen[LAYER_LOADING]);
    T_EQ(test_num_images, 2);
    T_FEQ(test_ui_frames[0].w, 1.0f, 0.0001f);
    T_FEQ(test_ui_frames[0].h, 1.0f, 0.0001f);
    T_STREQ(test_images[1].name, "Interface\\Glues\\LoadingBar\\Loading-BarFill.blp");
    /* SV_BuildLoadingScreen packs and consumes this initial multicast before LoadMap. */
    test_multicast_size = 0;
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    local = Wow_EntityLocal(player);

    T_EQ((int)test_apply_lobby_calls, 1);
    T_EQ((int)test_clear_world_calls, 1);
    T_ASSERT(player->inuse);
    T_NOT_NULL(player->client);
    T_NOT_NULL(local);
    T_NULL(local->think); /* the player is driven by client input, not a think fn */
    T_EQ((int)local->health, 100);
    T_EQ((int)local->selected_action_slot, 255);
    assert_player_spawned(player);
    T_FEQ(player->client->ps.vieworigin.x, player->s.origin.x, 0.001f);
    T_FEQ(player->client->ps.vieworigin.y, player->s.origin.y, 0.001f);
    T_FEQ(player->client->ps.vieworigin.z, player->s.origin.z + WOW_CAMERA_EYE_HEIGHT, 0.001f);
    T_EQ((int)player->client->ps.client_ui_state, CLIENT_UI_LOADING);
    T_STREQ(player->client->ps.name, "Thrall");
    T_EQ((int)player->client->ps.stats[WOW_STAT_HEALTH], 100);
    T_EQ((int)player->client->ps.stats[WOW_STAT_HEALTH_MAX], 100);
    T_EQ((int)player->client->ps.stats[WOW_STAT_POWER], 100);
    T_EQ((int)player->client->ps.stats[WOW_STAT_SELECTED_ACTION], 255);
    T_EQ((int)test_num_images, 2);
    T_EQ((int)test_unicast_calls, 0);
    T_NOT_NULL(game->ClientBegin);
    game->ClientBegin(player);
    T_EQ((int)player->client->ps.client_ui_state, CLIENT_UI_GAME);
    T_ASSERT(test_unicast_calls > 0);
    assert_player_ui_payload();
    {
        cstring_t info = test_get_configstring(WOW_CS_MAPINFO);
        cstring_t title = Wow_InfoValueForKey(info, "title", "");
        cstring_t preview = Wow_InfoValueForKey(info, "preview", "");
        T_STREQ(title, "Elwynn Test");
        T_STREQ(preview, "Interface/Glues/LoadingScreens/LoadScreenTest.blp");
    }
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
    edict_t *player;
    edict_t *creature;
    wowEntityLocal_t *creature_local;
    wowEntityLocal_t *player_local;
    vec2_t before;
    char target_num[16];
    cstring_t attack_argv[] = { "attack", target_num };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    T_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    player_local = Wow_EntityLocal(player);
    before = creature->s.origin2;

    snprintf(target_num, sizeof(target_num), "%u", (unsigned)creature->s.number);
    T_EQ((int)creature->s.number, MAX_CLIENTS);
    T_ASSERT(creature_local->think == Wow_RunCreatureFrame);
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
    T_EQ((int)(player_local->enemy ? player_local->enemy->s.number : 0), MAX_CLIENTS);
    T_EQ((int)((wowClient_t *)player->client)->selected_entity, MAX_CLIENTS);

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

TEST(wow_game, melee_approach_obeys_the_same_terrain_step_limit_as_manual_input) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    vec3_t const *ledge = CM_WowGetSpawnPos(1);
    edict_t *target = first_creature();
    T_NOT_NULL(ledge);
    T_NOT_NULL(target);
    if (!ledge || !target) { game->Shutdown(); return; }
    float step = WOW_WALK_SPEED * FRAMETIME / 1000.0f;
    float start_x = ledge->x - step;
    player->s.origin = (vec3_t){ start_x, ledge->y, Wow_TerrainHeight(start_x, ledge->y) };
    T_ASSERT(fabsf(player->s.origin.z - ledge->z) > step);
    target->s.origin = (vec3_t){ ledge->x + 8.0f, ledge->y, ledge->z };
    Wow_EntityLocal(target)->think = NULL;
    char number[16];
    snprintf(number, sizeof(number), "%u", (unsigned)target->s.number);
    game->ClientCommand(player, 2, (cstring_t[]){ "attack", number });
    game->RunFrame();
    T_FEQ(player->s.origin.x, start_x, 0.001f);

    target->s.origin.x = start_x - 8.0f;
    game->RunFrame();
    T_FEQ(player->s.origin.x, start_x - step, 0.001f);
    T_FEQ(fabsf(player->s.angle), (float)M_PI, 0.001f);
    game->Shutdown();
}

/* Target selection is state-only; combat begins only from an explicit attack or action-bar command. */
TEST(wow_game, selecting_target_does_not_start_combat_or_chase) {
    struct game_export *game = init_game();
    edict_t *player, *creature;
    wowEntityLocal_t *local;
    vec2_t before;
    char target_num[16];
    cstring_t select_argv[] = { "select", target_num };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    snprintf(target_num, sizeof(target_num), "%u", (unsigned)creature->s.number);
    local = Wow_EntityLocal(player);
    before = player->s.origin2;

    game->ClientCommand(player, 2, select_argv);
    game->RunFrame();

    T_EQ((int)((wowClient_t *)player->client)->selected_entity, (int)creature->s.number);
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
    edict_t *player, *creature, *projectile = NULL;
    wowEntityLocal_t *local;
    cstring_t action_argv[] = { "wow_action", "4" };

    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8\\appearance\\0");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    local = Wow_EntityLocal(player);
    T_NOT_NULL(creature);
    creature->s.origin = (vec3_t){ player->s.origin.x + 10.0f, player->s.origin.y, player->s.origin.z };
    creature->s.origin2 = (vec2_t){ creature->s.origin.x, creature->s.origin.y };
    ((wowClient_t *)player->client)->selected_entity = creature->s.number;
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
    FOR_LOOP(i, (uint32_t)globals.num_edicts) {
        if (wow_edicts[i].inuse && Wow_EntityLocal(&wow_edicts[i])->think == Wow_RunProjectile) {
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
    edict_t *player, *creature;
    wowEntityLocal_t *local;
    cstring_t action_argv[] = { "wow_action", "4" };
    cstring_t move_argv[] = { "move", "1", "0", "328", "8.5" };
    cstring_t stop_argv[] = { "move", "0", "0", "328", "8.5" };

    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8\\appearance\\0");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    creature = first_creature();
    local = Wow_EntityLocal(player);
    T_NOT_NULL(creature);
    creature->s.origin = (vec3_t){ player->s.origin.x + 10.0f, player->s.origin.y, player->s.origin.z };
    creature->s.origin2 = (vec2_t){ creature->s.origin.x, creature->s.origin.y };
    ((wowClient_t *)player->client)->selected_entity = creature->s.number;
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
    FOR_LOOP(i, (uint32_t)globals.num_edicts) {
        T_ASSERT(!wow_edicts[i].inuse || Wow_EntityLocal(&wow_edicts[i])->think != Wow_RunProjectile);
    }
    if (game->Shutdown) game->Shutdown();
}

/* Quest log rejects quests when full (SV_MAX_QUEST_LOG slots). */
TEST(wow_game, quest_log_full_rejects_new_quests) {
    struct game_export *game = init_game();
    edict_t *player;
    wowClient_t *wc;
    uint32_t available_quests[] = { 1, 8, 16, 47, 60, 62, 73, 83, 85, 106, 108, 117, 137, 176, 179, 182, 183 };

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    FOR_LOOP(i, SV_MAX_QUEST_LOG) {
        char id_buf[16];
        cstring_t accept_args[2];
        snprintf(id_buf, sizeof(id_buf), "%u", (unsigned)available_quests[i]);
        accept_args[0] = "quest_accept";
        accept_args[1] = id_buf;
        test_command_with_quest_giver(game, player, 2, accept_args);
    }
    T_EQ((int)wc->quest_count, SV_MAX_QUEST_LOG);

    /* Next accept should fail */
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "184"});
    T_EQ((int)wc->quest_count, SV_MAX_QUEST_LOG);
    if (game->Shutdown) game->Shutdown();
}

/* Accepting the same quest twice is a no-op (idempotent). */
TEST(wow_game, quest_accept_same_quest_twice_is_idempotent) {
    struct game_export *game = init_game();
    edict_t *player;
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "788"});
    T_EQ((int)wc->quest_count, 1);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "788"});
    T_EQ((int)wc->quest_count, 1);
    if (game->Shutdown) game->Shutdown();
}

/* Kill credit does not exceed the required count. */
TEST(wow_game, quest_kill_credit_does_not_overflow) {
    struct game_export *game = init_game();
    edict_t *player;
    wowClient_t *wc;
    svQuestEntry_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "788"});
    state = SV_QuestFind(wc->quest_log, wc->quest_count, 788);
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
    edict_t *player;
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "99999"});
    T_EQ((int)wc->quest_count, 0);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "0"});
    T_EQ((int)wc->quest_count, 0);
    if (game->Shutdown) game->Shutdown();
}

/* quest_complete on an unstarted quest does nothing. */
TEST(wow_game, quest_complete_without_accept_no_reward) {
    struct game_export *game = init_game();
    edict_t *player;
    player_t *ps;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    ps = &player->client->ps;
    Wow_EntityLocal(player)->experience = 0;
    Wow_EntityLocal(player)->copper = 0;
    ps->stats[WOW_STAT_XP] = 0;
    ps->stats[WOW_STAT_COPPER] = 0;

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_complete", "788"});
    T_EQ((int)ps->stats[WOW_STAT_XP], 0);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);
    if (game->Shutdown) game->Shutdown();
}

/* Questlog toggle: first call opens, second call closes. */
TEST(wow_game, quest_log_toggle_open_close) {
    struct game_export *game = init_game();
    edict_t *player;
    wowClient_t *wc;
    bool found_header;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;
    T_EQ((int)wc->questlog_open, 0);

    game->ClientCommand(player, 1, (cstring_t[]){"questlog"});
    T_EQ((int)wc->questlog_open, 1);

    test_ui_frame_count = 0;
    game->ClientCommand(player, 1, (cstring_t[]){"questlog"});
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
    edict_t *player;
    bool found_complete;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);

    /* Accept quest, then open dialog — should NOT show "Complete Quest" */
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "788"});
    test_ui_frame_count = 0;
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest", "788"});
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
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest", "788"});
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
    edict_t *player;
    wowClient_t *wc;
    wowEntityLocal_t *npc_local;
    edict_t *npc;
    bool found_title = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    /* Use first creature as a quest NPC by setting its quest_id field */
    npc = first_creature();
    T_NOT_NULL(npc);
    npc_local = Wow_EntityLocal(npc);
    npc_local->quest_id = 33;
    npc->s.flags |= EF_HAS_QUEST;
    npc->s.origin2 = player->s.origin2;

    /* Select the NPC and issue bare "quest" command (no ID argument) */
    ((wowClient_t *)player->client)->selected_entity = npc->s.number;
    test_ui_frame_count = 0;
    game->ClientCommand(player, 1, (cstring_t[]){"quest"});
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
    edict_t *player;
    wowClient_t *wc;
    wowEntityLocal_t *npc_local;
    edict_t *npc;
    bool found_title = false;
    bool found_accept = false;
    char cmd_arg[16];

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    npc = first_creature();
    T_NOT_NULL(npc);
    npc_local = Wow_EntityLocal(npc);
    npc_local->quest_id = 33;
    npc->s.flags |= EF_HAS_QUEST;
    npc->s.origin2 = player->s.origin2;

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    snprintf(cmd_arg, sizeof(cmd_arg), "%u", (unsigned)npc->s.number);
    game->ClientCommand(player, 2, (cstring_t[]){"interact", cmd_arg});

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
    edict_t *player;
    wowClient_t *wc;
    player_t *ps;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;
    ps = &player->client->ps;
    ps->stats[WOW_STAT_XP] = 0;
    ps->stats[WOW_STAT_COPPER] = 0;

    /* Quest 14 requires 13, which requires 12 */
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "14"});
    T_EQ((int)wc->quest_count, 0);

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "12"});
    T_EQ((int)wc->quest_count, 1);
    test_finish_quest_kills(player, 12);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_complete", "12"});
    T_ASSERT(ps->stats[WOW_STAT_XP] > 0);

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "13"});
    T_EQ((int)wc->quest_count, 1);
    test_finish_quest_kills(player, 13);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_complete", "13"});

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "14"});
    T_EQ((int)wc->quest_count, 1);
    T_EQ((int)wc->quest_log[0].quest_id, 14);
    T_EQ((int)wc->quest_log[0].status, SV_QUEST_ACTIVE);
    if (game->Shutdown) game->Shutdown();
}

/* Kill credit from combat: killing a creature via the combat system awards quest credit. */
TEST(wow_game, quest_kill_credit_from_combat_death) {
    struct game_export *game = init_game();
    edict_t *player;
    wowClient_t *wc;
    svQuestEntry_t *state;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    /* Accept quest 788 which needs display_id 503 kills */
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "788"});
    state = SV_QuestFind(wc->quest_log, wc->quest_count, 788);
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
    edict_t *player;
    player_t *ps;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    ps = &player->client->ps;
    Wow_EntityLocal(player)->experience = 0;
    Wow_EntityLocal(player)->copper = 0;

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "788"});
    test_finish_quest_kills(player, 788);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_complete", "788"});
    T_EQ((int)ps->stats[WOW_STAT_XP], 1020);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);

    /* Try completing again — should not award double rewards */
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_complete", "788"});
    T_EQ((int)ps->stats[WOW_STAT_XP], 1020);
    T_EQ((int)ps->stats[WOW_STAT_COPPER], 0);
    if (game->Shutdown) game->Shutdown();
}

/* Quest dialog progress text shows kill counts. */
TEST(wow_game, quest_dialog_shows_kill_progress_text) {
    struct game_export *game = init_game();
    edict_t *player;
    bool found_progress = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);

    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest_accept", "788"});
    FOR_LOOP(i, 5) Wow_QuestAwardKillCredit(player, 503);

    test_ui_frame_count = 0;
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){"quest", "788"});

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

/* Strafing turns the model to face the direction of travel; backpedal preserves facing. */
TEST(wow_game, wow_directional_movement_animations) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    animation_t const *back_animation;
    cstring_t left[] = { "move", "4", "0", "328", "8.5" };
    cstring_t right[] = { "move", "8", "0", "328", "8.5" };
    cstring_t back[] = { "move", "2", "0", "328", "8.5" };
    float facing;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    back_animation = G_GetAnimation(player->s.model, "WalkBackwards");
    facing = player->s.angle;
    game->ClientCommand(player, 5, left);
    game->RunFrame();
    T_STREQ(local->animation->name, "Run");
    T_FEQ(player->s.angle, (float)(M_PI / 2.0), 0.001f);
    game->ClientCommand(player, 5, right);
    game->RunFrame();
    T_STREQ(local->animation->name, "Run");
    T_FEQ(player->s.angle, (float)(-M_PI / 2.0), 0.001f);
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
    wowDoodadDef_t def = {
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
    edict_t *creature;
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
    edict_t *player = &wow_edicts[0], *creature;
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
    creature_local->think = Wow_RunCorpseFrame;
    /* Override loot to a deterministic copper value. */
    creature_local->loot_copper = 30;
    creature->s.origin2 = player->s.origin2;

    player_local->copper = 100;
    game->ClientCommand(player, 1, (cstring_t[]){"loot"});

    T_EQ((int)player_local->copper, 130);      /* copper auto-looted on open */
    T_EQ((int)creature_local->loot_copper, 0); /* drained from corpse */
    T_EQ((int)wc->loot_target, (int)creature->s.number);
    if (game->Shutdown) game->Shutdown();
}

/* loot_take <slot> moves the item from the corpse snapshot into inventory. */
TEST(wow_game, loot_take_moves_item_to_inventory) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0], *creature;
    wowClient_t *wc;
    wowEntityLocal_t *creature_local;
    uint32_t inv_slot;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    creature = first_creature();
    T_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    wc             = (wowClient_t *)player->client;

    Wow_AIDie(creature, player);
    creature_local->think = Wow_RunCorpseFrame;
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

    game->ClientCommand(player, 1, (cstring_t[]){"loot"});
    T_STREQ(wc->loot_snap[0].name, "Stringy Wolf Meat");

    /* Record first empty inventory slot before take. */
    inv_slot = WOW_UI_INVENTORY_SLOTS;
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS)
        if (!wc->inventory[i].icon[0]) { inv_slot = i; break; }
    T_ASSERT(inv_slot < WOW_UI_INVENTORY_SLOTS);

    game->ClientCommand(player, 2, (cstring_t[]){"loot_take", "0"});
    T_STREQ(wc->inventory[inv_slot].name, "Stringy Wolf Meat");
    T_EQ((int)wc->loot_snap[0].icon[0], 0); /* snapshot slot cleared */
    T_EQ((int)wc->loot_target, 0);           /* auto-closed: last item taken */
    if (game->Shutdown) game->Shutdown();
}

/* loot_close dismisses the loot window without taking any items. */
TEST(wow_game, loot_close_clears_window) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0], *creature;
    wowClient_t *wc;
    wowEntityLocal_t *creature_local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    creature = first_creature();
    T_NOT_NULL(creature);
    creature_local = Wow_EntityLocal(creature);
    wc             = (wowClient_t *)player->client;

    Wow_AIDie(creature, player);
    creature_local->think = Wow_RunCorpseFrame;
    creature_local->loot_copper = 10;
    creature->s.origin2 = player->s.origin2;

    game->ClientCommand(player, 1, (cstring_t[]){"loot"});
    T_ASSERT(wc->loot_target != 0);

    game->ClientCommand(player, 1, (cstring_t[]){"loot_close"});
    T_EQ((int)wc->loot_target, 0);
    if (game->Shutdown) game->Shutdown();
}

/* -------------------------------------------------------------------------
 * Backpack window tests
 * -------------------------------------------------------------------------*/

/* backpack command toggles backpack_open each call. */
TEST(wow_game, backpack_toggles_open_closed) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    wowClient_t *wc;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    wc = (wowClient_t *)player->client;

    T_EQ((int)wc->backpack_open, 0);
    game->ClientCommand(player, 1, (cstring_t[]){"backpack"});
    T_EQ((int)wc->backpack_open, 1);
    game->ClientCommand(player, 1, (cstring_t[]){"backpack"});
    T_EQ((int)wc->backpack_open, 0);
    if (game->Shutdown) game->Shutdown();
}

/* Opening the backpack emits its window frames on the console HUD layer. */
TEST(wow_game, backpack_window_emits_on_console_layer) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    bool found_title = false, found_close = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);

    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->ClientCommand(player, 1, (cstring_t[]){"backpack"});
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer != LAYER_CONSOLE) continue;
        if (!strcmp(frame->text, "Backpack")) found_title = true;
        if (!strcmp(frame->text, "Close") && !strcmp(frame->onclick, "backpack")) found_close = true;
    }
    T_ASSERT(found_title);
    T_ASSERT(found_close);
    if (game->Shutdown) game->Shutdown();
}

/* -------------------------------------------------------------------------
 * Damage flash overlay tests
 * -------------------------------------------------------------------------*/

/* Damage dealt to the player sets the incoming flash timer. */
TEST(wow_game, damage_flash_incoming_set_on_player_hit) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0], *creature;
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
    edict_t *player = &wow_edicts[0], *creature;
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

/* GCD set by an instant cast prevents a second cast until it expires. */
TEST(wow_game, gcd_blocks_second_instant_cast) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    wowEntityLocal_t *local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    local = Wow_EntityLocal(player);
    T_NOT_NULL(local);
    local->health = 50;

    /* Slot 3 = Healing Touch (instant, range=0, self). */
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "3" });
    T_EQ((int)local->gcd_time, 1500);
    T_EQ((int)local->mana, 92);    /* 100 - WOW_HEALING_TOUCH_MANA_COST (8) */
    T_EQ((int)local->health, 52);

    local->health = 50;
    uint32_t mana_snapshot = local->mana;

    /* Immediate second cast blocked by GCD. */
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "3" });
    T_EQ((int)local->mana, (int)mana_snapshot);
    T_EQ((int)local->health, 50);

    /* 15 frames drain the GCD (1500ms / FRAMETIME). */
    FOR_LOOP(i, 1500 / FRAMETIME) game->RunFrame();
    T_EQ((int)local->gcd_time, 0);

    /* Third cast succeeds once GCD expires. */
    local->health = 50;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "3" });
    T_EQ((int)local->health, 52);

    if (game->Shutdown) game->Shutdown();
}

/* ps.stats[WOW_STAT_CAST_PROGRESS] reflects remaining cast time each frame. */
TEST(wow_game, cast_progress_stat_shows_countdown) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    edict_t *creature;
    wowEntityLocal_t *local;

    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8\\appearance\\0");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    creature = first_creature();
    T_NOT_NULL(creature);
    if (!creature) { if (game->Shutdown) game->Shutdown(); return; }
    creature->s.origin  = (vec3_t){ player->s.origin.x + 10.0f, player->s.origin.y, player->s.origin.z };
    creature->s.origin2 = (vec2_t){ creature->s.origin.x, creature->s.origin.y };
    ((wowClient_t *)player->client)->selected_entity = creature->s.number;
    local = Wow_EntityLocal(player);

    /* Slot 4 = Fireball (1500ms cast). */
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "4" });
    T_ASSERT(local->cast_spell != SPELL_NONE);
    T_EQ((int)local->cast_duration, 1500);
    T_EQ((int)local->cast_remaining, 1500);

    game->ClientBegin(player);
    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    /* A real server tick updates the cast countdown without re-sending the action bar. */
    game->RunFrame();
    T_EQ((int)player->client->ps.stats[WOW_STAT_CAST_PROGRESS], 1400);
    T_EQ((int)player->client->ps.stats[WOW_STAT_CAST_MAX], 1500);
    T_ASSERT(test_layout_seen[LAYER_INFOPANEL]);
    bool found_countdown = false;
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->layer != LAYER_INFOPANEL) continue;
        if (!strcmp(frame->text, "Fireball  1.4 s")) found_countdown = true;
    }
    T_ASSERT(found_countdown);
    T_ASSERT(!test_layout_seen[LAYER_COMMANDBAR]);

    FOR_LOOP(i, 13) game->RunFrame();
    test_ui_frame_count = 0;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->RunFrame();
    T_ASSERT(test_layout_seen[LAYER_INFOPANEL]);
    T_EQ(test_ui_frame_count, 0);
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->RunFrame();
    T_ASSERT(!test_layout_seen[LAYER_INFOPANEL]);

    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, action_buttons_use_the_same_class_bindings_as_commands) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    bool attack_click = false, heal_click = false, empty_click = false;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    FOR_LOOP(i, test_ui_frame_count) {
        if (!strcmp(test_ui_frames[i].onclick, "wow_action 0")) attack_click = true;
        if (!strcmp(test_ui_frames[i].onclick, "wow_action 3")) heal_click = true;
        if (!strcmp(test_ui_frames[i].onclick, "wow_action 4")) empty_click = true;
    }
    T_ASSERT(attack_click);
    T_ASSERT(heal_click);
    T_ASSERT(!empty_click);
    T_EQ(((wowClient_t *)player->client)->action_spells[4], SPELL_NONE);
    game->Shutdown();
}

TEST(wow_game, action_icons_receive_one_clock_based_cooldown_payload) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    Wow_EntityLocal(player)->health = 50;
    test_game_time = 4000;
    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "3" });
    uint32_t shaded = 0;
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (!(frame->flagsvalue & UIFLAG_RADIAL_SHADE)) continue;
        T_EQ(frame->type, FT_COMMANDBUTTON);
        T_EQ(frame->layer, LAYER_COMMANDBAR);
        T_EQ(frame->command_button.radialStartTime, 4000);
        T_EQ(frame->command_button.radialEndTime, 5500);
        shaded++;
    }
    T_EQ(shaded, 2);

    test_game_time += FRAMETIME;
    memset(test_layout_seen, 0, sizeof(test_layout_seen));
    game->RunFrame();
    T_ASSERT(!test_layout_seen[LAYER_COMMANDBAR]);
    game->Shutdown();
}

TEST(wow_game, fire_blast_cooldown_blocks_only_that_spell_and_updates_icon) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8\\appearance\\0");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    edict_t *target = first_creature();
    T_NOT_NULL(target);
    if (!target) { game->Shutdown(); return; }
    target->s.origin = (vec3_t){ player->s.origin.x + 5.0f, player->s.origin.y, player->s.origin.z };
    ((wowClient_t *)player->client)->selected_entity = target->s.number;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    test_game_time = 10000;
    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "6" });
    T_EQ(local->spell_cooldown[WOW_SPELL_FIRE_BLAST], 8000);
    T_EQ(local->mana, 60);
    bool found = false;
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (strcmp(frame->onclick, "wow_action 6")) continue;
        found = true;
        T_ASSERT(frame->flagsvalue & UIFLAG_RADIAL_SHADE);
        T_EQ(frame->command_button.radialStartTime, 10000);
        T_EQ(frame->command_button.radialEndTime, 18000);
    }
    T_ASSERT(found);
    FOR_LOOP(i, 1500 / FRAMETIME) game->RunFrame();
    uint32_t mana = local->mana;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "6" });
    T_EQ(local->mana, mana);
    T_EQ(local->spell_cooldown[WOW_SPELL_FIRE_BLAST], 6500);
    local->health = 50;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "3" });
    T_EQ(local->health, 52);
    FOR_LOOP(i, 6500 / FRAMETIME) game->RunFrame();
    T_EQ(local->spell_cooldown[WOW_SPELL_FIRE_BLAST], 0);
    game->Shutdown();
}

TEST(wow_game, fire_blast_damage_and_rejected_casts) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8\\appearance\\0");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    edict_t *target = first_creature();
    T_NOT_NULL(target);
    if (!target) { game->Shutdown(); return; }
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    wowEntityLocal_t *victim = Wow_EntityLocal(target);
    ((wowClient_t *)player->client)->selected_entity = target->s.number;
    target->s.origin2 = (vec2_t){ player->s.origin2.x + 5.0f, player->s.origin2.y };
    victim->health = 1000;
    local->mana = 39;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "6" });
    T_EQ(local->mana, 39);
    T_EQ(local->spell_cooldown[WOW_SPELL_FIRE_BLAST], 0);
    T_EQ(local->gcd_time, 0);
    local->mana = 100;
    victim->dead = true;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "6" });
    T_EQ(local->mana, 100);
    T_EQ(local->spell_cooldown[WOW_SPELL_FIRE_BLAST], 0);
    T_EQ(local->gcd_time, 0);
    victim->dead = false;
    FOR_LOOP(i, 32) {
        local->mana = 100;
        local->gcd_time = 0;
        local->spell_cooldown[WOW_SPELL_FIRE_BLAST] = 0;
        victim->health = 1000;
        game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "6" });
        T_ASSERT(victim->health >= 968 && victim->health <= 976);
        T_EQ(local->spell_cooldown[WOW_SPELL_FIRE_BLAST], 8000);
    }
    game->Shutdown();
}

TEST(wow_game, casts_reject_invalid_targets_and_cancel_when_target_moves_out_of_range) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8\\appearance\\0");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    edict_t *target = first_creature();
    T_NOT_NULL(target);
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    wowEntityLocal_t *victim = Wow_EntityLocal(target);
    ((wowClient_t *)player->client)->selected_entity = target->s.number;
    target->s.origin2 = (vec2_t){ player->s.origin2.x + 10.0f, player->s.origin2.y };
    victim->dead = true;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "4" });
    T_EQ(local->cast_spell, SPELL_NONE);
    T_EQ(local->gcd_time, 0);
    victim->dead = false;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "4" });
    T_EQ(local->cast_spell, WOW_SPELL_FIREBOLT);
    target->s.origin2.x = player->s.origin2.x + 50.0f;
    game->RunFrame();
    T_EQ(local->cast_spell, SPELL_NONE);
    T_EQ(local->mana, 100);
    game->Shutdown();
}

TEST(wow_game, healing_touch_accepts_its_actual_mana_cost_without_turning) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    local->health = 50;
    local->mana = 8;
    player->s.angle = 1.25f;
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "3" });
    T_EQ(local->health, 52);
    T_EQ(local->mana, 0);
    T_FEQ(player->s.angle, 1.25f, 0.001f);
    game->Shutdown();
}

/* Wow_MonsterStart sets RF_HOSTILE on all spawned creatures. */
TEST(wow_game, creature_spawns_with_rf_hostile_flag) {
    struct game_export *game = init_game();
    edict_t *creature;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();
    creature = first_creature();
    T_NOT_NULL(creature);
    if (creature)
        T_ASSERT(creature->s.flags & EF_HOSTILE);

    if (game->Shutdown) game->Shutdown();
}

/* After death animation expires (WOW_DEFAULT_DEATH_TIME = 1200ms), think transitions
 * to Wow_RunCorpseFrame and SVF_MONSTER is cleared. */
TEST(wow_game, creature_death_transitions_to_corpse_frame) {
    struct game_export *game = init_game();
    edict_t *creature;
    wowEntityLocal_t *local;

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->RunFrame();
    creature = first_creature();
    T_NOT_NULL(creature);
    if (!creature) { if (game->Shutdown) game->Shutdown(); return; }

    local = Wow_EntityLocal(creature);
    Wow_AIDie(creature, NULL);
    T_ASSERT(local->dead);
    T_ASSERT(creature->svflags & SVF_DEADMONSTER);
    T_ASSERT(local->think != Wow_RunCorpseFrame);

    /* WOW_DEFAULT_DEATH_TIME = 1200ms / FRAMETIME = 100ms → 12 ticks. */
    FOR_LOOP(i, 1200 / FRAMETIME) Wow_AIAdvanceLockedFrame(creature);
    T_ASSERT(local->think == Wow_RunCorpseFrame);
    T_ASSERT(!(creature->svflags & SVF_MONSTER));

    if (game->Shutdown) game->Shutdown();
}

TEST(wow_game, target_selection_reconciles_client_groups) {
    struct game_export *game = init_game();
    char number[16];
    uint32_t selected;
    cstring_t args[] = { "select", number };
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    edict_t *target = first_creature();
    snprintf(number, sizeof(number), "%u", target->s.number);
    test_selection_size = 0;
    game->ClientCommand(&wow_edicts[0], 2, args);
    T_EQ(test_selection_size, 6); T_EQ(test_selection_buf[0], svc_set_selection); T_EQ(test_selection_buf[1], 1);
    memcpy(&selected, test_selection_buf + 2, sizeof(selected));
    T_EQ(selected, target->s.number);
    snprintf(number, sizeof(number), "%u", MAX_GAME_ENTITIES);
    game->ClientCommand(&wow_edicts[0], 2, args);
    T_EQ(test_selection_size, 2); T_EQ(test_selection_buf[1], 0);
    game->Shutdown();
}

TEST(wow_game, controller_orbits_authoritative_actor_focus) {
    struct game_export *game = init_game();
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    edict_t *player = &wow_edicts[0];
    game->ClientBegin(player);
    float ground = player->s.origin.z;
    player->s.origin.z = 42;
    game->ClientInput(player, &(inputCmd_t){ .action = BZ_INPUT_VIEW, .view = {{-72, 0, 0}, 8} });
    T_FEQ(player->client->ps.vieworigin.z, 42 + WOW_CAMERA_EYE_HEIGHT, 0.001f);
    T_FEQ(player->client->ps.viewangles.x, -72, 0.001f);
    T_FEQ(player->client->ps.viewangles.z, 0, 0.001f);
    T_FEQ(player->client->ps.distance, 8, 0.001f);
    vec3_t focus = player->client->ps.vieworigin;
    game->ClientInput(player, &(inputCmd_t){ .action = BZ_INPUT_FOCUS, .focus = {999, 999} });
    T_FEQ(player->client->ps.vieworigin.x, focus.x, 0.001f);
    T_FEQ(player->client->ps.vieworigin.y, focus.y, 0.001f);
    player->s.origin.z = ground;
    game->ClientInput(player, &(inputCmd_t){ .action = BZ_INPUT_MOVE, .move = {BZ_MOVE_FORWARD, 16} });
    game->RunFrame();
    T_ASSERT(player->s.origin.y != focus.y);
    vec3_t stopped = player->s.origin;
    game->ClientInput(player, &(inputCmd_t){ .action = BZ_INPUT_MOVE });
    game->RunFrame();
    T_FEQ(player->s.origin.x, stopped.x, 0.001f);
    T_FEQ(player->s.origin.y, stopped.y, 0.001f);
    game->ClientInput(player, &(inputCmd_t){ .action = BZ_INPUT_VIEW, .view = {{-90, 0, 0}, 1000} });
    T_FEQ(player->client->ps.distance, WOW_CAMERA_MAX_DISTANCE, 0.001f);
    T_FEQ(player->client->ps.viewangles.x, 270 - WOW_CAMERA_MAX_PITCH, 0.001f);
}

TEST(wow_game, quest_turn_in_waits_for_kills_and_survives_frame_updates) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    uint32_t xp = player->client->ps.stats[WOW_STAT_XP];
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_accept", "788" });
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_complete", "788" });
    T_EQ(player->client->ps.stats[WOW_STAT_XP], xp);

    FOR_LOOP(i, 20) Wow_QuestAwardKillCredit(player, 503);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_complete", "788" });
    game->RunFrame();
    T_EQ(player->client->ps.stats[WOW_STAT_XP], xp + 1020);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_accept", "788" });
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_complete", "788" });
    T_EQ(player->client->ps.stats[WOW_STAT_XP], xp + 1020);
    game->Shutdown();
}

TEST(wow_game, quest_abandon_shifts_progress_and_allows_restart) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_accept", "788" });
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_accept", "12" });
    T_ASSERT(client->quest_tracked[0]);
    T_ASSERT(client->quest_tracked[1]);
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_track", "12" });
    T_ASSERT(!client->quest_tracked[1]);
    Wow_QuestAwardKillCredit(player, 503);
    Wow_QuestAwardKillCredit(player, 2331);
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_abandon", "788" });
    T_EQ(client->quest_count, 1);
    T_EQ(client->quest_log[0].quest_id, 12);
    T_ASSERT(!client->quest_tracked[0]);
    T_EQ(client->kill_progress[0][0], 1);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_accept", "788" });
    T_EQ(client->quest_count, 2);
    T_EQ(client->kill_progress[1][0], 0);
    T_ASSERT(client->quest_tracked[1]);
    test_finish_quest_kills(player, 12);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_complete", "12" });
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_abandon", "12" });
    T_EQ(client->quest_count, 1);
    T_ASSERT(Wow_QuestWasRewarded(client, 12));
    game->Shutdown();
}

TEST(wow_game, rewarded_quests_leave_room_in_journal_and_preserve_currency) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    wowClient_t *client = (wowClient_t *)player->client;
    uint32_t initial_copper = local->copper;
    uint32_t initial_xp = local->experience;
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_accept", "12" });
    test_finish_quest_kills(player, 12);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_complete", "12" });
    T_EQ(client->quest_count, 0);
    T_ASSERT(Wow_QuestWasRewarded(client, 12));
    T_EQ(local->copper, initial_copper + 500);
    T_EQ(local->experience, initial_xp + 850);
    game->RunFrame();
    T_EQ(player->client->ps.stats[WOW_STAT_COPPER], initial_copper + 500);
    T_EQ(player->client->ps.stats[WOW_STAT_XP], initial_xp + 850);
    test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_accept", "12" });
    T_EQ(client->quest_count, 0);
    game->Shutdown();
}

TEST(wow_game, quest_journal_paginates_and_clamps_after_abandon) {
    static uint32_t const quests[] = { 1, 8, 16, 47, 60, 62, 73, 83, 85, 106, 108, 117, 137 };
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    FOR_LOOP(i, sizeof(quests) / sizeof(quests[0])) {
        char id[16];
        snprintf(id, sizeof(id), "%u", quests[i]);
        test_command_with_quest_giver(game, player, 2, (cstring_t[]){ "quest_accept", id });
    }
    T_EQ(client->quest_count, 13);
    game->ClientCommand(player, 1, (cstring_t[]){ "questlog" });
    game->ClientCommand(player, 2, (cstring_t[]){ "questlog_page", "next" });
    T_EQ(client->questlog_offset, 12);
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_abandon", "137" });
    T_EQ(client->questlog_offset, 0);
    game->ClientCommand(player, 2, (cstring_t[]){ "questlog_page", "prev" });
    T_EQ(client->questlog_offset, 0);
    game->Shutdown();
}

TEST(wow_game, full_backpack_and_stale_loot_commands_do_not_remove_items) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    edict_t *corpse = first_creature();
    T_NOT_NULL(corpse);
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(corpse);
    Wow_AIDie(corpse, player);
    local->think = Wow_RunCorpseFrame;
    memset(local->loot_items, 0, sizeof(local->loot_items));
    local->loot_count = 1;
    local->loot_copper = 0;
    local->loot_items[0] = (wowHudIcon_t){ "food.blp", "Meat", 1 };
    corpse->s.origin2 = player->s.origin2;
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS)
        client->inventory[i] = (wowHudIcon_t){ "bag.blp", "Bag", 1 };

    game->ClientCommand(player, 1, (cstring_t[]){ "loot" });
    game->ClientCommand(player, 2, (cstring_t[]){ "loot_take", "0" });
    T_EQ(local->loot_count, 1);
    T_EQ(client->loot_target, corpse->s.number);

    client->inventory[0] = (wowHudIcon_t){0};
    corpse->s.origin2.y += 50.0f;
    game->ClientCommand(player, 2, (cstring_t[]){ "loot_take", "0" });
    T_EQ(client->inventory[0].count, 0);
    T_EQ(local->loot_count, 1);
    T_EQ(client->loot_target, 0);

    corpse->s.origin2 = player->s.origin2;
    game->ClientCommand(player, 1, (cstring_t[]){ "loot" });
    game->ClientCommand(player, 2, (cstring_t[]){ "loot_take", "no" });
    T_EQ(local->loot_count, 1);
    game->ClientCommand(player, 1, (cstring_t[]){ "loot_all" });
    T_STREQ(client->inventory[0].name, "Meat");
    T_EQ(local->loot_count, 0);
    game->Shutdown();
}

TEST(wow_game, loot_all_preserves_excess_items_and_rejects_stale_generation) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    edict_t *corpse = first_creature();
    T_NOT_NULL(corpse);
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(corpse);
    Wow_AIDie(corpse, player);
    local->think = Wow_RunCorpseFrame;
    memset(local->loot_items, 0, sizeof(local->loot_items));
    local->loot_items[0] = (wowHudIcon_t){ "food.blp", "Meat", 1 };
    local->loot_items[1] = (wowHudIcon_t){ "other.blp", "Hide", 1 };
    local->loot_count = 2;
    local->loot_copper = 0;
    corpse->s.origin2 = player->s.origin2;
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS)
        client->inventory[i] = (wowHudIcon_t){ "bag.blp", "Bag", 1 };
    client->inventory[0] = (wowHudIcon_t){0};
    game->ClientCommand(player, 1, (cstring_t[]){ "loot" });
    char stale_token[32];
    snprintf(stale_token, sizeof(stale_token), "%llu", (unsigned long long)(client->loot_generation + 1));
    game->ClientCommand(player, 2, (cstring_t[]){ "loot_all", stale_token });
    T_EQ(local->loot_count, 2);
    T_EQ(client->inventory[0].count, 0);
    bool found_take_all = false;
    FOR_LOOP(i, test_ui_frame_count)
        if (!strncmp(test_ui_frames[i].onclick, "loot_all ", 9)) found_take_all = true;
    T_ASSERT(found_take_all);
    game->ClientCommand(player, 1, (cstring_t[]){ "loot_all" });
    T_STREQ(client->inventory[0].name, "Meat");
    T_EQ(local->loot_count, 1);
    T_STREQ(local->loot_items[1].name, "Hide");
    T_EQ(client->loot_snap_count, 1);
    client->inventory[1] = (wowHudIcon_t){0};
    local->loot_generation++;
    game->ClientCommand(player, 2, (cstring_t[]){ "loot_take", "1" });
    T_EQ(client->inventory[1].count, 0);
    T_EQ(local->loot_count, 1);
    T_EQ(client->loot_target, 0);
    game->Shutdown();
}

TEST(wow_game, loot_interaction_rejects_out_of_range_and_expired_corpses) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    edict_t *corpse = first_creature();
    T_NOT_NULL(corpse);
    wowEntityLocal_t *local = Wow_EntityLocal(corpse);
    wowClient_t *client = (wowClient_t *)player->client;
    Wow_AIDie(corpse, player);
    local->think = Wow_RunCorpseFrame;
    local->loot_items[0] = (wowHudIcon_t){ "food.blp", "Meat", 1 };
    local->loot_count = 1;
    local->loot_copper = 0;
    corpse->s.origin2 = player->s.origin2;
    corpse->s.origin2.x += 11.0f;
    char number[16];
    snprintf(number, sizeof(number), "%u", corpse->s.number);
    game->ClientCommand(player, 2, (cstring_t[]){ "interact", number });
    T_EQ(client->loot_target, 0);
    corpse->s.origin2 = player->s.origin2;
    game->ClientCommand(player, 2, (cstring_t[]){ "interact", number });
    T_EQ(client->loot_target, corpse->s.number);
    corpse->inuse = false;
    game->ClientCommand(player, 1, (cstring_t[]){ "loot_all" });
    T_EQ(client->loot_target, 0);
    T_EQ(local->loot_count, 1);
    game->Shutdown();
}

TEST(wow_game, unbound_action_slots_and_malformed_commands_are_inert) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];

    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    edict_t *target = first_creature();
    T_NOT_NULL(target);
    target->s.origin2 = player->s.origin2;
    ((wowClient_t *)player->client)->selected_entity = target->s.number;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "4" });
    T_EQ(local->cast_spell, SPELL_NONE);
    T_EQ(local->gcd_time, 0);
    game->ClientCommand(player, 2, (cstring_t[]){ "wow_action", "wrong" });
    T_EQ(local->gcd_time, 0);
    T_NULL(local->enemy);
    game->Shutdown();
}

TEST(wow_game, world_map_displays_current_zone_and_live_position) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    test_worldmap_fixture = true;
    player->s.origin.x = 0;
    player->s.origin.y = 0;
    player->s.origin2 = (vec2_t){ 0, 0 };
    test_ui_frame_count = 0;
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });

    wowClient_t *wc = (wowClient_t *)player->client;
    T_ASSERT(wc->worldmap_open);
    T_STREQ(wc->worldmap_area, "TestZone");
    int map_tile = test_image_index("Interface\\WorldMap\\TestZone\\TestZone1.blp");
    int marker = test_image_index("Interface\\WorldMap\\WorldMapPlayerIcon.blp");
    bool found_tile = false, found_marker = false, found_close = false;
    FOR_LOOP(i, test_ui_frame_count) {
        testUiFrame_t const *frame = &test_ui_frames[i];
        if (frame->type == FT_TEXTURE && frame->image_index == map_tile) found_tile = true;
        if (frame->type == FT_TEXTURE && frame->image_index == marker) {
            found_marker = true;
            T_FEQ(frame->x, (128.0f + 384.0f - 10.0f) / 1024.0f, 0.002f);
            T_FEQ(frame->y, (95.0f + 256.0f - 10.0f) / 768.0f, 0.002f);
        }
        if (!strcmp(frame->onclick, "worldmap")) found_close = true;
    }
    T_ASSERT(found_tile);
    T_ASSERT(found_marker);
    T_ASSERT(found_close);
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    T_ASSERT(!wc->worldmap_open);
    game->Shutdown();
}

TEST(wow_game, world_map_zone_chooser_keeps_selected_zone) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    test_worldmap_fixture = true;
    player->s.origin = (vec3_t){500, 500, 0};
    game->ClientCommand(player, 1, (cstring_t[]){"worldmap"});
    wowClient_t *wc = (wowClient_t *)player->client;
    T_STREQ(wc->worldmap_area, "Continent");
    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "zones"});
    T_ASSERT(wc->worldmap_choosing);
    bool found = false;
    FOR_LOOP(i, test_ui_frame_count)
        if (!strcmp(test_ui_frames[i].onclick, "worldmap zone 2")) {
            found = true;
            T_STREQ(test_ui_frames[i].text, "Test Zone");
        }
    T_ASSERT(found);
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "next"});
    T_EQ(wc->worldmap_page, 0);
    test_ui_frame_count = 0;
    game->ClientCommand(player, 3, (cstring_t[]){"worldmap", "zone", "2"});
    T_ASSERT(!wc->worldmap_choosing);
    T_STREQ(wc->worldmap_area, "TestZone");
    T_STREQ(wc->worldmap_title, "Test Zone");
    bool current_button = false;
    FOR_LOOP(i, test_ui_frame_count)
        if (!strcmp(test_ui_frames[i].onclick, "worldmap current")) current_button = true;
    T_ASSERT(current_button);
    int marker = test_image_index("Interface\\WorldMap\\WorldMapPlayerIcon.blp");
    FOR_LOOP(i, test_ui_frame_count)
        T_ASSERT(test_ui_frames[i].image_index != marker);
    T_ASSERT(UI_ResolveWorldMap(player));
    T_STREQ(wc->worldmap_area, "TestZone");
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "current"});
    T_STREQ(wc->worldmap_area, "Continent");
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "zones"});
    game->ClientCommand(player, 3, (cstring_t[]){"worldmap", "zone", "1"});
    T_ASSERT(wc->worldmap_choosing);
    T_EQ(wc->worldmap_selection, 0);
    game->ClientCommand(player, 3, (cstring_t[]){"worldmap", "zone", "999"});
    T_ASSERT(wc->worldmap_choosing);
    T_STREQ(wc->worldmap_area, "Continent");
    game->Shutdown();
}

TEST(wow_game, world_map_chooser_paginates_and_stays_static) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    test_worldmap_fixture = true;
    test_worldmap_many_zones = true;
    game->ClientCommand(player, 1, (cstring_t[]){"worldmap"});
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "zones"});
    wowClient_t *wc = (wowClient_t *)player->client;
    T_ASSERT(wc->worldmap_has_next_page);
    uint32_t before = test_unicast_calls;
    FOR_LOOP(i, 1000 / FRAMETIME) game->RunFrame();
    T_EQ(test_unicast_calls, before);
    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "next"});
    T_EQ(wc->worldmap_page, 1);
    T_ASSERT(!wc->worldmap_has_next_page);
    bool last_zone = false, previous = false;
    FOR_LOOP(i, test_ui_frame_count) {
        if (!strcmp(test_ui_frames[i].onclick, "worldmap zone 23")) last_zone = true;
        if (!strcmp(test_ui_frames[i].onclick, "worldmap previous")) previous = true;
    }
    T_ASSERT(last_zone);
    T_ASSERT(previous);
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "next"});
    T_EQ(wc->worldmap_page, 1);
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "previous"});
    T_EQ(wc->worldmap_page, 0);
    T_ASSERT(wc->worldmap_has_next_page);
    game->Shutdown();
}

TEST(wow_game, world_map_handles_missing_data_and_resolves_larger_area) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *wc = (wowClient_t *)player->client;
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    T_ASSERT(wc->worldmap_open);
    T_STREQ(wc->worldmap_area, "");

    test_worldmap_fixture = true;
    player->s.origin.x = 500;
    player->s.origin.y = 500;
    T_ASSERT(UI_ResolveWorldMap(player));
    T_STREQ(wc->worldmap_area, "Continent");
    player->s.origin.x = 2000;
    T_ASSERT(!UI_ResolveWorldMap(player));
    T_STREQ(wc->worldmap_area, "");
    game->Shutdown();
}

TEST(wow_game, open_world_map_refreshes_position_and_area_from_scheduler) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    test_worldmap_fixture = true;
    player->s.origin = (vec3_t){0, 0, 0};
    player->s.origin2 = (vec2_t){0, 0};
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    wowClient_t *wc = (wowClient_t *)player->client;
    T_STREQ(wc->worldmap_area, "TestZone");

    player->s.origin.x = 500;
    player->s.origin2.x = 500;
    uint32_t before = test_unicast_calls;
    FOR_LOOP(i, 1000 / FRAMETIME) game->RunFrame();
    T_STREQ(wc->worldmap_area, "Continent");
    T_ASSERT(test_unicast_calls > before);
    player->s.origin.x = 0;
    player->s.origin2.x = 0;
    FOR_LOOP(i, 1000 / FRAMETIME) game->RunFrame();
    T_STREQ(wc->worldmap_area, "TestZone");
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    before = test_unicast_calls;
    FOR_LOOP(i, 1000 / FRAMETIME) game->RunFrame();
    T_EQ(test_unicast_calls, before);
    game->Shutdown();
}

TEST(wow_game, continent_view_stays_selected_until_recentered) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    test_worldmap_fixture = true;
    player->s.origin = (vec3_t){0, 0, 0};
    game->ClientCommand(player, 2, (cstring_t[]){ "worldmap", "continent" });
    wowClient_t *client = (wowClient_t *)player->client;
    T_ASSERT(!client->worldmap_open);
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    T_STREQ(client->worldmap_area, "TestZone");
    game->ClientCommand(player, 2, (cstring_t[]){ "worldmap", "continent" });
    T_STREQ(client->worldmap_area, "Continent");
    player->s.origin.x = 50;
    FOR_LOOP(i, 1000 / FRAMETIME) game->RunFrame();
    T_STREQ(client->worldmap_area, "Continent");
    game->ClientCommand(player, 2, (cstring_t[]){ "worldmap", "current" });
    T_STREQ(client->worldmap_area, "TestZone");
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    T_ASSERT(!client->worldmap_open);
    game->Shutdown();
}

TEST(wow_game, backpack_commands_validate_and_preserve_stacks) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    local->health = 50;
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "2"});
    T_EQ(local->health, 85);
    T_EQ(client->inventory[2].count, 1);
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "-1"});
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "999999999999999999999999"});
    T_EQ(local->health, 85);
    game->ClientCommand(player, 3, (cstring_t[]){"bag_split", "3", "6"});
    T_EQ(client->inventory[3].count, 3);
    T_EQ(client->inventory[6].count, 2);
    game->ClientCommand(player, 3, (cstring_t[]){"bag_move", "6", "3"});
    T_EQ(client->inventory[3].count, 5);
    T_EQ(client->inventory[6].count, 0);
    game->ClientCommand(player, 1, (cstring_t[]){"backpack"});
    game->ClientCommand(player, 2, (cstring_t[]){"bag_select", "3"});
    game->ClientCommand(player, 2, (cstring_t[]){"bag_select", "6"});
    T_EQ(client->inventory[3].count, 0);
    T_EQ(client->inventory[6].count, 5);
    local->dead = true;
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "6"});
    T_EQ(client->inventory[6].count, 5);
    game->Shutdown();
}

TEST(wow_game, spellbook_bind_clear_and_shared_cooldown) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8\\appearance\\0");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    local->spell_cooldown[WOW_SPELL_FIRE_BLAST] = 5000;
    game->ClientCommand(player, 1, (cstring_t[]){"spellbook"});
    T_ASSERT(client->spellbook_open);
    local->health = 50;
    game->ClientCommand(player, 2, (cstring_t[]){"spellbook_cast", "3"});
    T_EQ(local->health, 52);
    T_EQ(local->mana, 92);
    game->ClientCommand(player, 2, (cstring_t[]){"spellbook_cast", "3"});
    T_EQ(local->health, 52); /* GCD also applies to casts from the book. */
    game->ClientCommand(player, 2, (cstring_t[]){"spellbook_select", "4"});
    T_EQ(client->spellbook_selected, WOW_SPELL_FIRE_BLAST);
    game->ClientCommand(player, 3, (cstring_t[]){"spellbook_bind", "2", "4"});
    T_EQ(client->action_spells[2], WOW_SPELL_FIRE_BLAST);
    T_EQ(local->spell_cooldown[WOW_SPELL_FIRE_BLAST], 5000);
    game->ClientCommand(player, 3, (cstring_t[]){"spellbook_bind", "2", "clear"});
    T_EQ(client->action_spells[2], SPELL_NONE);
    T_EQ(local->spell_cooldown[WOW_SPELL_FIRE_BLAST], 5000);
    game->ClientCommand(player, 3, (cstring_t[]){"spellbook_bind", "999", "4"});
    game->ClientCommand(player, 3, (cstring_t[]){"spellbook_bind", "2", "999"});
    T_EQ(client->action_spells[2], SPELL_NONE);
    game->Shutdown();
}

TEST(wow_game, spellbook_class_restriction) {
    T_ASSERT(Wow_SpellKnown(WOW_CLASS_WARRIOR, WOW_SPELL_ATTACK));
    T_ASSERT(!Wow_SpellKnown(WOW_CLASS_WARRIOR, WOW_SPELL_FIRE_BLAST));
    T_ASSERT(Wow_SpellKnown(WOW_CLASS_MAGE, WOW_SPELL_FIRE_BLAST));
    T_ASSERT(!Wow_SpellKnown(WOW_CLASS_MAGE, WOW_SPELL_COUNT));
}

TEST(wow_game, merchant_transactions_validate_range_and_balance) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    edict_t *npc = Wow_Spawn();
    T_ASSERT(npc);
    wowEntityLocal_t *vendor = Wow_EntityLocal(npc);
    vendor->creature_entry = 233; /* Farmer Saldean, vendor flag in imported data */
    npc->s.origin2 = player->s.origin2;
    T_ASSERT(Wow_MerchantOpen(player, npc));
    local->copper = 19;
    T_ASSERT(!Wow_MerchantBuy(player, 0));
    local->copper = 20;
    T_ASSERT(Wow_MerchantBuy(player, 0));
    T_EQ(local->copper, 0);
    T_ASSERT(Wow_MerchantSell(player, 2));
    T_EQ(local->copper, 15);
    T_EQ(client->merchant_buyback[0].count, 3);
    T_ASSERT(Wow_MerchantBuyback(player, 0));
    T_EQ(local->copper, 0);
    npc->s.origin2.x += 100;
    T_ASSERT(!Wow_MerchantBuy(player, 0));
    game->Shutdown();
}

TEST(wow_game, inventory_stack_limits_and_atomic_insert) {
    wowClient_t client = {0};
    wowHudIcon_t potion = { .icon = "potion", .name = "Potion", .count = 4, .item_id = 118 };
    T_ASSERT(Wow_InventoryAdd(&client, &potion));
    potion.count = 7;
    T_ASSERT(Wow_InventoryAdd(&client, &potion));
    T_EQ(client.inventory[0].count, 5);
    T_EQ(client.inventory[1].count, 5);
    T_EQ(client.inventory[2].count, 1);
    for (uint32_t i = 3; i < WOW_UI_INVENTORY_SLOTS; i++) {
        client.inventory[i] = (wowHudIcon_t){ .icon = "unknown", .name = "unknown", .count = 1 };
    }
    potion.count = 5;
    T_ASSERT(!Wow_InventoryAdd(&client, &potion));
    T_EQ(client.inventory[2].count, 1);
    potion.count = 4;
    T_ASSERT(Wow_InventoryAdd(&client, &potion));
    T_EQ(client.inventory[2].count, 5);
    T_ASSERT(!Wow_InventoryRemove(&client, 0, 6));
    T_EQ(client.inventory[0].count, 5);
    T_ASSERT(Wow_InventoryRemove(&client, 0, 5));
    T_EQ(client.inventory[0].count, 0);
    wowHudIcon_t unknown = { .icon = "unknown", .name = "unknown", .count = 3 };
    T_ASSERT(Wow_InventoryAdd(&client, &unknown));
    T_EQ(client.inventory[0].count, 3);
    T_EQ(client.inventory[3].count, 1);
}

TEST(wow_game, quest_journal_requires_nearby_giver_for_transactions) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_accept", "788" });
    T_EQ(client->quest_count, 0);
    edict_t *giver = Wow_Spawn();
    T_NOT_NULL(giver);
    wowEntityLocal_t *local = Wow_EntityLocal(giver);
    local->quest_id = 788;
    giver->s.flags |= EF_HAS_QUEST;
    giver->s.origin2 = player->s.origin2;
    char id[32];
    snprintf(id, sizeof(id), "%u", giver->s.number);
    game->ClientCommand(player, 2, (cstring_t[]){ "interact", id });
    T_ASSERT(client->quest_open);
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_accept", "788" });
    T_EQ(client->quest_count, 1);
    T_ASSERT(client->quest_tracked[0]);
    /* Pins are emitted only for tracked quests and live map anchors. */
    test_worldmap_fixture = true;
    player->s.origin = (vec3_t){0, 0, 0};
    player->s.origin2 = (vec2_t){0, 0};
    giver->s.origin2 = player->s.origin2;
    edict_t *anchor = Wow_Spawn();
    T_NOT_NULL(anchor);
    Wow_EntityLocal(anchor)->go_type = WOW_QUEST_OBJECTIVE_ANCHOR;
    Wow_EntityLocal(anchor)->go_entry = 788;
    anchor->s.origin = (vec3_t){10, 10, 0};
    test_ui_frame_count = 0;
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    bool found_pin = false;
    FOR_LOOP(i, test_ui_frame_count)
        if (test_ui_frames[i].layer == LAYER_GAME_0 && test_ui_frames[i].color.r == 255 &&
            test_ui_frames[i].color.g == 215 && test_ui_frames[i].color.b == 40) found_pin = true;
    T_ASSERT(found_pin);
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_track", "788" });
    test_ui_frame_count = 0;
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    found_pin = false;
    FOR_LOOP(i, test_ui_frame_count)
        if (test_ui_frames[i].layer == LAYER_GAME_0 && test_ui_frames[i].color.r == 255 &&
            test_ui_frames[i].color.g == 215 && test_ui_frames[i].color.b == 40) found_pin = true;
    T_ASSERT(!found_pin);
    game->ClientCommand(player, 1, (cstring_t[]){ "worldmap" });
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_inspect", "788" });
    T_ASSERT(client->quest_open && client->quest_journal_detail);
    FOR_LOOP(i, 20) Wow_QuestAwardKillCredit(player, 503);
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_complete", "788" });
    T_EQ(client->quest_count, 1);
    game->ClientCommand(player, 1, (cstring_t[]){ "quest_back" });
    game->ClientCommand(player, 2, (cstring_t[]){ "interact", id });
    giver->s.origin2.x += 50;
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_complete", "788" });
    T_EQ(client->quest_count, 1);
    giver->s.origin2 = player->s.origin2;
    game->ClientCommand(player, 2, (cstring_t[]){ "interact", id });
    game->ClientCommand(player, 2, (cstring_t[]){ "quest_complete", "788" });
    T_EQ(client->quest_count, 0);
    T_ASSERT(Wow_QuestWasRewarded(client, 788));
    game->Shutdown();
}

TEST(wow_game, fireball_rank_training_and_cast) {
    struct game_export *game = init_game();
    edict_t *player, *creature, *projectile = NULL;
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8\\appearance\\0");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    game->ClientCommand(player, 2, (cstring_t[]){"spellbook_cast", "5"});
    game->ClientCommand(player, 3, (cstring_t[]){"spellbook_bind", "2", "5"});
    T_EQ(client->action_spells[2], SPELL_NONE);
    game->ClientCommand(player, 1, (cstring_t[]){"spellbook_train_fireball"});
    T_ASSERT(!client->fireball_rank2_learned);
    local->experience = 400;
    game->ClientCommand(player, 1, (cstring_t[]){"spellbook_train_fireball"});
    T_ASSERT(client->fireball_rank2_learned);
    T_EQ(client->action_spells[4], WOW_SPELL_FIREBALL_RANK2);
    creature = first_creature();
    T_NOT_NULL(creature);
    creature->s.origin = (vec3_t){player->s.origin.x + 10, player->s.origin.y, player->s.origin.z};
    creature->s.origin2 = (vec2_t){creature->s.origin.x, creature->s.origin.y};
    client->selected_entity = creature->s.number;
    game->ClientCommand(player, 2, (cstring_t[]){"spellbook_cast", "5"});
    T_EQ(local->cast_spell, WOW_SPELL_FIREBALL_RANK2);
    for (int i = 0; i < 20; ++i) game->RunFrame();
    T_EQ(local->mana, 55);
    for (uint32_t i = 1; i < globals.num_edicts; ++i) {
        wowEntityLocal_t *p = Wow_EntityLocal(&wow_edicts[i]);
        if (wow_edicts[i].inuse && p && p->projectile_caster == player->s.number && p->projectile_target == creature->s.number) projectile = &wow_edicts[i];
    }
    T_NOT_NULL(projectile);
    if (projectile) {
        uint32_t damage = Wow_EntityLocal(projectile)->projectile_damage;
        T_ASSERT(damage >= 31 && damage <= 45);
    }
    game->Shutdown();
}

TEST(wow_game, taxi_discovery_fare_and_arrival) {
    struct game_export *game = init_game();
    edict_t *player;
    wowClient_t *client;
    wowEntityLocal_t *local;
    float x, y, z;
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    client = (wowClient_t *)player->client;
    local = Wow_EntityLocal(player);
    x = player->s.origin.x; y = player->s.origin.y; z = player->s.origin.z;
    WowTaxi_Free(&wow_taxi_graph);
    wow_taxi_graph.node_count=2; wow_taxi_graph.path_count=1; wow_taxi_graph.point_count=2;
    wow_taxi_graph.nodes=calloc(2,sizeof(*wow_taxi_graph.nodes));
    wow_taxi_graph.paths=calloc(1,sizeof(*wow_taxi_graph.paths));
    wow_taxi_graph.points=calloc(2,sizeof(*wow_taxi_graph.points));
    wow_taxi_graph.nodes[0]=(wowTaxiNode_t){1,CM_WowGetMapId(),x,y,z,"Origin"};
    wow_taxi_graph.nodes[1]=(wowTaxiNode_t){2,CM_WowGetMapId(),x+20,y,z,"Destination"};
    wow_taxi_graph.paths[0]=(wowTaxiPath_t){1,1,2,10,0,2};
    wow_taxi_graph.points[0]=(wowTaxiPoint_t){1,0,CM_WowGetMapId(),x,y,z,0,0};
    wow_taxi_graph.points[1]=(wowTaxiPoint_t){1,1,CM_WowGetMapId(),x+20,y,z,0,0};
    local->copper=9;
    client->taxi_known[0] |= 1u << 2;
    game->ClientCommand(player,1,(cstring_t[]){"taxi"});
    T_ASSERT(client->taxi_known[0] & (1u << 1)); T_ASSERT(client->taxi_open);
    game->ClientCommand(player,2,(cstring_t[]){"taxi","2"});
    T_ASSERT(!client->taxi_flying); T_EQ(local->copper,9);
    local->copper=20;
    game->ClientCommand(player,2,(cstring_t[]){"taxi","2"});
    T_ASSERT(client->taxi_flying); T_EQ(local->copper,10);
    for (int i=0;i<100 && client->taxi_flying;i++) game->RunFrame();
    T_ASSERT(!client->taxi_flying); T_FEQ(player->s.origin.x,x+20,0.01f);
    T_EQ(local->copper,10);
    game->Shutdown();
}

TEST(wow_game, merchant_commands_roll_back_and_buyback_all_slots) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    edict_t *npc = Wow_Spawn();
    T_ASSERT(npc);
    wowEntityLocal_t *vendor = Wow_EntityLocal(npc);
    vendor->creature_entry = 233;
    npc->s.origin2 = player->s.origin2;
    char number[24], revision[24];
    snprintf(number, sizeof(number), "%u", npc->s.number);
    game->ClientCommand(player, 2, (cstring_t[]){ "interact", number });
    T_EQ(client->merchant_target, npc->s.number);
    local->copper = 1000;
    memset(client->inventory, 0, sizeof(client->inventory));
    for (uint32_t i = 0; i < WOW_UI_INVENTORY_SLOTS; i++) {
        client->inventory[i] = *(&Wow_MerchantItem(0)->item);
        client->inventory[i].count = Wow_InventoryStackLimit(118);
    }
    snprintf(revision, sizeof(revision), "%u", client->merchant_revision);
    game->ClientCommand(player, 3, (cstring_t[]){ "merchant_buy", "0", revision });
    T_EQ(local->copper, 1000);
    T_EQ(client->merchant_revision, (uint32_t)strtoul(revision, NULL, 10));
    local->copper = UINT32_MAX;
    char stack_count[24];
    snprintf(stack_count, sizeof(stack_count), "%u", client->inventory[0].count);
    game->ClientCommand(player, 5, (cstring_t[]){ "merchant_sell", "0", revision, "118", stack_count });
    T_EQ(client->inventory[0].count, Wow_InventoryStackLimit(118));
    local->copper = 0;
    /* Build thirteen individually identifiable buyback stacks; the oldest rotates out. */
    for (uint32_t i = 1; i <= 13; i++) {
        client->inventory[0] = Wow_MerchantItem(0)->item;
        client->inventory[0].count = i;
        T_ASSERT(Wow_MerchantSell(player, 0));
    }
    T_EQ(client->merchant_buyback[0].count, 13);
    T_EQ(client->merchant_buyback[11].count, 2);
    game->ClientCommand(player, 1, (cstring_t[]){ "merchant_view" });
    T_ASSERT(client->merchant_buyback_view);
    client->inventory[0] = Wow_MerchantItem(0)->item;
    client->inventory[0].count = Wow_InventoryStackLimit(118);
    snprintf(revision, sizeof(revision), "%u", client->merchant_revision);
    game->ClientCommand(player, 3, (cstring_t[]){ "merchant_buyback", "11", revision });
    T_EQ(client->merchant_buyback[11].count, 2); /* no inventory capacity */
    memset(&client->inventory[0], 0, sizeof(client->inventory[0]));
    game->ClientCommand(player, 3, (cstring_t[]){ "merchant_buyback", "11", revision });
    T_EQ(client->merchant_buyback[11].count, 0);
    T_EQ(client->inventory[0].count, 2);
    vendor->dead = true;
    T_ASSERT(!Wow_MerchantValid(player));
    vendor->dead = false;
    local->dead = true;
    T_ASSERT(!Wow_MerchantValid(player));
    local->dead = false;
    npc->s.origin2.x += 11;
    T_ASSERT(!Wow_MerchantValid(player));
    game->Shutdown();
}

TEST(wow_game, bank_transactions_require_live_nearby_banker_and_space) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    edict_t *banker = Wow_Spawn();
    T_ASSERT(banker);
    Wow_EntityLocal(banker)->creature_entry = 2458;
    banker->s.origin2 = player->s.origin2;
    T_ASSERT(Wow_BankOpen(player, banker));
    T_ASSERT(Wow_BankTransfer(player, true, 3, true));
    T_EQ(client->inventory[3].count, 3);
    T_EQ(client->bank[0].count, 2);
    T_ASSERT(Wow_BankTransfer(player, false, 0, false));
    T_EQ(client->inventory[3].count, 5);
    T_EQ(client->bank[0].count, 0);
    for (uint32_t i = 0; i < 16; i++)
        client->bank[i] = (wowHudIcon_t){ .icon = "unknown", .count = 1 };
    T_ASSERT(!Wow_BankTransfer(player, true, 3, false));
    T_EQ(client->inventory[3].count, 5);
    banker->s.origin2.x += 100;
    T_ASSERT(!Wow_BankTransfer(player, true, 3, false));
    T_EQ(client->inventory[3].count, 5);
    banker->s.origin2 = player->s.origin2;
    Wow_EntityLocal(banker)->dead = true;
    T_ASSERT(!Wow_BankTransfer(player, true, 3, false));
    game->Shutdown();
}

TEST(wow_game, bank_command_rejects_stale_snapshot) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    edict_t *banker = Wow_Spawn();
    T_ASSERT(banker);
    Wow_EntityLocal(banker)->creature_entry = 2458;
    banker->s.origin2 = player->s.origin2;
    T_ASSERT(Wow_BankOpen(player, banker));
    UI_WriteWowHud(player);
    char rev[16]; snprintf(rev, sizeof(rev), "%u", (unsigned)client->bank_revision);
    game->ClientCommand(player, 6, (cstring_t[]){"bank_deposit", "3", rev, "117", "4", "0"});
    T_EQ(client->inventory[3].count, 5);
    game->ClientCommand(player, 6, (cstring_t[]){"bank_deposit", "3", rev, "117", "5", "1"});
    T_EQ(client->inventory[3].count, 3);
    T_EQ(client->bank[0].count, 2);
    game->ClientCommand(player, 6, (cstring_t[]){"bank_deposit", "3", rev, "117", "3", "0"});
    T_EQ(client->inventory[3].count, 3);
    game->Shutdown();
}

TEST(wow_game, imported_vendor_spawn_is_nearby_and_interactable) {
    struct game_export *game = init_game();
    test_vendor_fixture = true;
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    for (uint32_t i = MAX_CLIENTS; i < WOW_MAX_EDICTS; i++) wow_edicts[i].inuse = false;
    globals.num_edicts = MAX_CLIENTS;
    wow_spawns_this_frame = 0;
    vec2_t origin = { -9464.87f, 93.9896f };
    Wow_SpawnVendors(&origin);
    edict_t *vendor = NULL;
    for (uint32_t i = MAX_CLIENTS; i < (uint32_t)globals.num_edicts; i++)
        if (wow_edicts[i].inuse && Wow_EntityLocal(&wow_edicts[i])->creature_entry == 54) vendor = &wow_edicts[i];
    T_NOT_NULL(vendor);
    if (vendor) {
        edict_t *player = &wow_edicts[0];
        player->s.origin2 = origin;
        char number[24];
        snprintf(number, sizeof(number), "%u", vendor->s.number);
        game->ClientCommand(player, 2, (cstring_t[]){"interact", number});
        T_EQ(((wowClient_t *)player->client)->merchant_target, vendor->s.number);
        uint32_t old_count = globals.num_edicts;
        wow_spawns_this_frame = 0;
        Wow_SpawnVendors(&origin);
        uint32_t copies = 0;
        for (uint32_t i = MAX_CLIENTS; i < (uint32_t)globals.num_edicts; i++)
            if (wow_edicts[i].inuse && Wow_EntityLocal(&wow_edicts[i])->creature_entry == 54) copies++;
        T_EQ(copies, 1);
        T_ASSERT(globals.num_edicts <= old_count + 8);
    }
    game->Shutdown();
}

TEST(wow_game, bank_and_inventory_survive_map_change_and_close_sessions) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    edict_t *banker = Wow_Spawn();
    T_ASSERT(banker);
    Wow_EntityLocal(banker)->creature_entry = 2458;
    banker->s.origin2 = player->s.origin2;
    T_ASSERT(Wow_BankOpen(player, banker));
    T_ASSERT(Wow_BankTransfer(player, true, 3, false));
    client->merchant_target = client->merchant_entry = 42;
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    T_EQ(client->inventory[3].count, 0);
    T_EQ(client->bank[0].count, 5);
    T_EQ(client->bank_target, 0);
    T_EQ(client->merchant_target, 0);
    game->Shutdown();
}

TEST(wow_game, first_aid_crafting_atomic_inventory) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    memset(client->inventory, 0, sizeof(client->inventory));
    client->inventory[0] = (wowHudIcon_t){"Interface\\Icons\\INV_Fabric_Linen_01.blp", "Linen Cloth", 1, 2589};
    client->inventory[1] = (wowHudIcon_t){"Interface\\Icons\\INV_Fabric_Linen_01.blp", "Linen Cloth", 1, 2589};
    game->ClientCommand(player, 2, (cstring_t[]){"craft", "3276"});
    T_EQ(client->inventory[0].count, 1); /* closed */
    game->ClientCommand(player, 1, (cstring_t[]){"crafting"});
    T_ASSERT(client->crafting_open);
    game->ClientCommand(player, 2, (cstring_t[]){"craft", "3276"});
    T_EQ(client->inventory[0].item_id, 2581);
    T_EQ(client->inventory[0].count, 1);
    T_EQ(client->inventory[1].item_id, 0);
    game->ClientCommand(player, 2, (cstring_t[]){"craft", "3276"});
    T_EQ(client->inventory[0].count, 1); /* insufficient cloth */
    for (uint32_t i = 1; i < WOW_UI_INVENTORY_SLOTS; ++i)
        client->inventory[i] = (wowHudIcon_t){"occupied", "Item", 1, 0};
    client->inventory[1] = (wowHudIcon_t){"cloth", "Linen Cloth", 2, 2589};
    game->ClientCommand(player, 2, (cstring_t[]){"craft", "3275"});
    T_EQ(client->inventory[1].count, 2); /* no room after consuming one */
    game->ClientCommand(player, 2, (cstring_t[]){"craft", "999999999999999999999"});
    T_EQ(client->inventory[1].count, 2);
    Wow_EntityLocal(player)->dead = true;
    game->ClientCommand(player, 2, (cstring_t[]){"craft", "3276"});
    T_EQ(client->inventory[1].count, 2);
    game->Shutdown();
}

TEST(wow_game, visiting_a_subzone_reveals_its_map_art_to_that_player) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    test_subzone_terrain = true;
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    game->ClientBegin(player);
    test_worldmap_fixture = true;
    player->s.origin = (vec3_t){0, 0, 0};
    wowClient_t *wc = (wowClient_t *)player->client;
    game->ClientCommand(player, 1, (cstring_t[]){"worldmap"});
    uint32_t tile = test_image_index("Interface\\WorldMap\\TestZone\\Valley2.blp");
    bool seen = false;
    for (uint32_t i = 0; i < test_ui_frame_count; ++i)
        if (test_ui_frames[i].image_index == tile) seen = true;
    T_ASSERT(!seen);

    game->ClientCommand(player, 1, (cstring_t[]){"worldmap"});
    T_ASSERT(!wc->worldmap_open);
    player->s.origin2 = (vec2_t){-10, -10};
    T_EQ(CM_WowAreaAtPoint(-10, -10), 710);
    T_EQ(CM_WowAreaAtPoint(-10, -40), 711);
    T_EQ(CM_WowAreaAtPoint(-90, -90), 0);
    for (uint32_t t = 0; t < 500; t += FRAMETIME) game->RunFrame();
    T_ASSERT(!Wow_ExploreArea(&wc->exploration, 710));
    game->ClientCommand(player, 1, (cstring_t[]){"worldmap"});
    test_ui_frame_count = 0;
    UI_WriteWorldMap(player);
    for (uint32_t i = 0; i < test_ui_frame_count; ++i) {
        testUiFrame_t const *piece = &test_ui_frames[i];
        if (piece->image_index != tile) continue;
        T_FEQ(piece->w, (768.0f / 1024) * 59 / 1002, 0.001f);
        T_FEQ(piece->h, (512.0f / 768) * 128 / 668, 0.001f);
        seen = true;
    }
    T_ASSERT(seen);
    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "continent"});
    for (uint32_t i = 0; i < test_ui_frame_count; ++i)
        T_NE(test_ui_frames[i].image_index, tile);
    game->Shutdown();
}

TEST(wow_game, bandage_healing_interrupt_and_cooldown) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    client->inventory[6] = (wowHudIcon_t){"bandage", "Linen Bandage", 3, 1251};
    local->health = 100;
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "6"});
    T_EQ(client->inventory[6].count, 3);
    local->health = 20;
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "6"});
    T_EQ(client->inventory[6].count, 2);
    for (int i = 0; i < 20; ++i) game->RunFrame();
    T_EQ(local->health, 42);
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "6"});
    T_EQ(client->inventory[6].count, 2);
    Wow_ApplyDamage(player, NULL, 1);
    for (int i = 0; i < 20; ++i) game->RunFrame();
    T_EQ(local->health, 41);
    T_EQ(local->bandage_ticks, 0);
    T_ASSERT(local->recently_bandaged_ms > 0);
    local->recently_bandaged_ms = 0;
    client->inventory[7] = (wowHudIcon_t){"heavy", "Heavy Linen Bandage", 1, 2581};
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "7"});
    for (int i = 0; i < 10; ++i) game->RunFrame();
    T_EQ(local->health, 60);
    game->ClientCommand(player, 5, (cstring_t[]){"move", "1", "0", "328", "8.5"});
    game->RunFrame();
    T_EQ(local->bandage_ticks, 0);
    game->Shutdown();
}

TEST(wow_game, world_map_landmarks_list_and_selection) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    test_worldmap_fixture = test_landmarks_fixture = true;
    player->s.origin = (vec3_t){0, 0, 0};
    player->s.origin2 = (vec2_t){0, 0};
    wowClient_t *client = (wowClient_t *)player->client;
    game->ClientCommand(player, 1, (cstring_t[]){"worldmap"});
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "places"});
    T_ASSERT(client->worldmap_landmarks_list);
    T_ASSERT(client->worldmap_landmarks_next);
    bool first = false, foreign = false;
    FOR_LOOP(i, test_ui_frame_count) {
        if (!strcmp(test_ui_frames[i].text, "Place 0")) first = true;
        if (!strcmp(test_ui_frames[i].text, "Place 22")) foreign = true;
    }
    T_ASSERT(first);
    T_ASSERT(!foreign);
    test_ui_frame_count = 0;
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "landmark_next"});
    T_EQ(client->worldmap_landmarks_page, 1);
    bool last = false;
    FOR_LOOP(i, test_ui_frame_count) if (!strcmp(test_ui_frames[i].text, "Place 21")) last = true;
    T_ASSERT(last);
    game->ClientCommand(player, 3, (cstring_t[]){"worldmap", "landmark", "23"});
    T_ASSERT(client->worldmap_landmarks_list); /* foreign physical map */
    test_ui_frame_count = 0;
    game->ClientCommand(player, 3, (cstring_t[]){"worldmap", "landmark", "22"});
    T_ASSERT(!client->worldmap_landmarks_list);
    T_ASSERT(client->worldmap_landmarks_visible);
    T_EQ(client->worldmap_landmarks_selected, 22);
    bool label = false;
    FOR_LOOP(i, test_ui_frame_count) if (!strcmp(test_ui_frames[i].text, "Place 21")) label = true;
    T_ASSERT(label);
    game->ClientCommand(player, 2, (cstring_t[]){"worldmap", "landmarks"});
    T_ASSERT(!client->worldmap_landmarks_visible);
    game->Shutdown();
}

TEST(wow_game, bandage_channel_stops_for_spell_and_completes) {
    struct game_export *game = init_game();
    edict_t *player = &wow_edicts[0];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = (wowClient_t *)player->client;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    client->inventory[6] = (wowHudIcon_t){"bandage", "Linen Bandage", 2, 1251};
    local->health = 10;
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "6"});
    game->ClientCommand(player, 2, (cstring_t[]){"spellbook_cast", "3"});
    T_EQ(local->bandage_ticks, 0);
    T_EQ(local->health, 12);
    local->recently_bandaged_ms = 0;
    local->gcd_time = 0;
    game->ClientCommand(player, 2, (cstring_t[]){"bag_use", "6"});
    for (int i = 0; i < 60; ++i) game->RunFrame();
    T_EQ(local->health, 78);
    T_EQ(local->bandage_ticks, 0);
    T_EQ(local->recently_bandaged_ms, 54000);
    game->Shutdown();
}

TEST(wow_game, landmark_rows_validate_bounds_and_locales) {
    struct game_export *game = init_game();
    test_landmarks_fixture = true;
    uint32_t size = 0;
    uint8_t *data = test_read_file("DBFilesClient\\AreaPOI.dbc", &size);
    wowLandmark_t landmark;
    T_EQ(Wow_LandmarkRows(data, size), 23);
    T_ASSERT(Wow_LandmarkRead(data, size, 0, 1, 200, -200, 100, -100, &landmark));
    T_STREQ(landmark.name, "Place 0");
    T_FEQ(landmark.u, 0.5f, 0.001f);
    T_FEQ(landmark.v, 0.5f, 0.001f);
    T_ASSERT(!Wow_LandmarkRead(data, size, 0, 0, 200, -200, 100, -100, &landmark));
    T_ASSERT(!Wow_LandmarkRead(data, size, 21, 1, 10, -10, 10, -10, &landmark));
    T_ASSERT(!Wow_LandmarkRead(data, size, 23, 1, 200, -200, 100, -100, &landmark));
    T_ASSERT(!Wow_LandmarkRead(data, size, 0, 1, 0, 0, 100, -100, &landmark));
    T_EQ(Wow_LandmarkRows(data, size - 1), 0);
    putfield(data, 1, UINT32_MAX);
    T_EQ(Wow_LandmarkRows(data, size), 0);
    T_ASSERT(!Wow_LandmarkRead(data, size, 0, 1, 200, -200, 100, -100, &landmark));
    free(data);
    game->Shutdown();
}

TEST(wow_game, service_npcs_refresh_on_travel_and_reject_reuse) {
    struct game_export *game = init_game();
    test_vendor_fixture = true;
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    for (uint32_t i = MAX_CLIENTS; i < WOW_MAX_EDICTS; i++) wow_edicts[i].inuse = false;
    globals.num_edicts = MAX_CLIENTS;
    wow_spawns_this_frame = 0;
    edict_t *player = &wow_edicts[0];
    player->s.origin2 = (vec2_t){ -9464.87f, 93.9896f };
    Wow_SpawnVendors(&player->s.origin2);
    edict_t *vendor = NULL;
    for (uint32_t i = MAX_CLIENTS; i < (uint32_t)globals.num_edicts; i++)
        if (wow_edicts[i].inuse && Wow_EntityLocal(&wow_edicts[i])->creature_entry == 54) vendor = &wow_edicts[i];
    T_NOT_NULL(vendor);
    if (!vendor) { game->Shutdown(); return; }
    T_ASSERT(Wow_MerchantOpen(player, vendor));
    Wow_EntityLocal(vendor)->spawn_serial++;
    T_ASSERT(!Wow_MerchantValid(player));
    T_ASSERT(Wow_MerchantOpen(player, vendor));
    edict_t *quest = Wow_Spawn();
    T_NOT_NULL(quest);
    Wow_EntityLocal(quest)->quest_id = 783;
    Wow_EntityLocal(quest)->creature_entry = 233;
    quest->s.origin2 = player->s.origin2;
    player->s.origin2 = (vec2_t){ -5343.68f, -2932.13f };
    player->s.origin.x = player->s.origin2.x;
    player->s.origin.y = player->s.origin2.y;
    for (uint32_t ms = 0; ms < 5100; ms += FRAMETIME) game->RunFrame();
    T_ASSERT(!vendor->inuse || Wow_EntityLocal(vendor)->creature_entry != 54);
    T_ASSERT(quest->inuse);
    T_EQ(((wowClient_t *)player->client)->merchant_target, 0);
    bool found = false;
    for (uint32_t i = MAX_CLIENTS; i < (uint32_t)globals.num_edicts; i++)
        if (wow_edicts[i].inuse && Wow_EntityLocal(&wow_edicts[i])->creature_entry == 167) found = true;
    T_ASSERT(found);
    game->Shutdown();
}

TEST(wow_game, character_save_roundtrip_and_corruption_is_atomic) {
    char path[512]; snprintf(path, sizeof(path), "%s", Test_TempPath("character-save.sav"));
    wowClient_t source = {0}, dest = {0};
    wowEntityLocal_t player = {0}, restored = {0};
    source.inventory[0] = (wowHudIcon_t){ .icon = "potion", .name = "Potion", .count = 3, .item_id = 118 };
    source.bank[0] = source.inventory[0];
    source.quest_count = 1; source.quest_log[0].quest_id = 42; source.quest_log[0].status = SV_QUEST_ACTIVE;
    source.kill_progress[0][0] = 3; source.quest_tracked[0] = true;
    source.rewarded_quests[8] = 4; source.taxi_known[2] = 8; source.exploration.visited[5] = 16;
    for (uint32_t i = 0; i < WOW_UI_ACTION_SLOTS; i++) source.action_spells[i] = SPELL_NONE;
    source.action_spells[2] = WOW_SPELL_ATTACK;
    source.fireball_rank2_learned = true; player.copper = 123; player.experience = 456;
    T_ASSERT(Wow_CharacterSavePath(path, &source, &player));
    T_EQ(Wow_CharacterLoadPath(path, &dest, &restored), 1);
    T_EQ(dest.inventory[0].count, 3); T_EQ(dest.bank[0].item_id, 118);
    T_EQ(dest.action_spells[0], SPELL_NONE); T_EQ(dest.action_spells[2], WOW_SPELL_ATTACK);
    T_EQ(dest.quest_log[0].quest_id, 42); T_EQ(dest.kill_progress[0][0], 3);
    T_ASSERT(dest.quest_tracked[0] && dest.fireball_rank2_learned);
    T_EQ(dest.rewarded_quests[8], 4); T_EQ(dest.taxi_known[2], 8); T_EQ(dest.exploration.visited[5], 16);
    T_EQ(restored.copper, 123); T_EQ(restored.experience, 456);
    FILE *f = fopen(path, "wb"); T_ASSERT(f); T_EQ(fwrite("broken", 1, 6, f), 6); fclose(f);
    dest.inventory[0].count = 2; restored.copper = 999;
    T_EQ(Wow_CharacterLoadPath(path, &dest, &restored), -1);
    T_EQ(dest.inventory[0].count, 2); T_EQ(restored.copper, 999);
    remove(path);
    T_EQ(Wow_CharacterLoadPath(path, &dest, &restored), 0);
}

TEST(wow_game, taxi_flightmaster_and_per_player_state) {
    struct game_export *game = init_game();
    edict_t *player, *other, *npc;
    wowClient_t *first, *second;
    wowEntityLocal_t *first_local, *second_local, *npc_local;
    float x, y, z;
    char npc_id[32];
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    player = &wow_edicts[0];
    game->ClientBegin(player);
    first = (wowClient_t *)player->client;
    first_local = Wow_EntityLocal(player);
    x = player->s.origin.x; y = player->s.origin.y; z = player->s.origin.z;
    WowTaxi_Free(&wow_taxi_graph);
    wow_taxi_graph.node_count = 2; wow_taxi_graph.path_count = 1; wow_taxi_graph.point_count = 2;
    wow_taxi_graph.nodes = calloc(2, sizeof(*wow_taxi_graph.nodes));
    wow_taxi_graph.paths = calloc(1, sizeof(*wow_taxi_graph.paths));
    wow_taxi_graph.points = calloc(2, sizeof(*wow_taxi_graph.points));
    wow_taxi_graph.nodes[0] = (wowTaxiNode_t){1, CM_WowGetMapId(), x+30, y, z, "Origin"};
    wow_taxi_graph.nodes[1] = (wowTaxiNode_t){2, CM_WowGetMapId(), x+50, y, z, "Destination"};
    wow_taxi_graph.paths[0] = (wowTaxiPath_t){1, 1, 2, 10, 0, 2};
    wow_taxi_graph.points[0] = (wowTaxiPoint_t){1, 0, CM_WowGetMapId(), x+30, y, z, 0, 0};
    wow_taxi_graph.points[1] = (wowTaxiPoint_t){1, 1, CM_WowGetMapId(), x+50, y, z, 0, 0};
    game->RunFrame();
    npc = Wow_Spawn();
    T_NOT_NULL(npc);
    if (!npc) { game->Shutdown(); return; }
    npc_local = Wow_EntityLocal(npc);
    npc_local->creature_entry = 352; /* Dungar Longdrink: flight master */
    npc_local->health = 100;
    npc->s.origin = player->s.origin;
    npc->s.origin2 = player->s.origin2;
    snprintf(npc_id, sizeof(npc_id), "%u", npc->s.number);
    game->ClientCommand(player, 2, (cstring_t[]){"interact", npc_id});
    T_ASSERT(first->taxi_open);
    T_EQ(first->taxi_source, 1);
    T_ASSERT(first->taxi_known[0] & (1u << 1));
    other = &wow_edicts[1];
    second = &wow_clients[1];
    second_local = Wow_EntityLocal(other);
    other->inuse = true;
    other->client = &second->client;
    other->s.origin = player->s.origin;
    other->s.origin2 = player->s.origin2;
    second_local->health = 100;
    T_ASSERT(!(second->taxi_known[0] & (1u << 1)));
    game->ClientCommand(other, 2, (cstring_t[]){"interact", npc_id});
    T_ASSERT(second->taxi_known[0] & (1u << 1));
    first_local->copper = 20;
    game->ClientCommand(player, 2, (cstring_t[]){"taxi", "2"});
    T_ASSERT(!first->taxi_flying);
    T_EQ(first_local->copper, 20);
    first->taxi_known[0] |= 1u << 2;
    player->s.origin.x += 11;
    player->s.origin2.x += 11;
    game->ClientCommand(player, 2, (cstring_t[]){"taxi", "2"});
    T_ASSERT(!first->taxi_flying);
    T_EQ(first_local->copper, 20);
    player->s.origin.x -= 11;
    player->s.origin2.x -= 11;
    game->ClientCommand(player, 2, (cstring_t[]){"interact", npc_id});
    game->ClientCommand(player, 2, (cstring_t[]){"taxi", "2"});
    T_ASSERT(first->taxi_flying);
    T_ASSERT(!second->taxi_flying);
    second->taxi_known[0] |= 1u << 2;
    second_local->copper = 30;
    game->ClientCommand(other, 2, (cstring_t[]){"taxi", "2"});
    T_ASSERT(second->taxi_flying);
    T_EQ(second_local->copper, 20);
    game->RunFrame();
    T_ASSERT(player->s.origin.x > x);
    T_ASSERT(other->s.origin.x > x);
    T_ASSERT(first->taxi_flying);
    T_ASSERT(second->taxi_flying);
    Wow_ApplyDamage(player, npc, 25);
    T_EQ(first_local->health, 100);
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    T_ASSERT(!first->taxi_flying);
    T_ASSERT(!second->taxi_flying);
    game->Shutdown();
    T_EQ(wow_taxi_graph.node_count, 0);
}

TEST(wow_game, character_progress_survives_logout_and_isolated_by_id) {
    struct game_export *game = init_game();
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Orc\\sex\\Male\\class\\1\\charid\\1000000000000001");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = &wow_clients[0];
    client->inventory[3].count = 2; client->bank[0] = client->inventory[3];
    Wow_EntityLocal(&wow_edicts[0])->copper = 77;
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    T_EQ(client->inventory[3].count, 2); T_EQ(Wow_EntityLocal(&wow_edicts[0])->copper, 77);
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Orc\\sex\\Male\\class\\1\\charid\\1000000000000002");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    T_EQ(client->inventory[3].count, 5); T_EQ(client->bank[0].count, 0);
    game->Shutdown();
    game = init_game();
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Orc\\sex\\Male\\class\\1\\charid\\1000000000000001");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    T_EQ(client->inventory[3].count, 2); T_EQ(client->bank[0].count, 2);
    T_EQ(Wow_EntityLocal(&wow_edicts[0])->copper, 77);
    game->Shutdown();
}

TEST(wow_game, character_action_bindings_restore_and_validate_class_and_rank) {
    char path[512]; snprintf(path, sizeof(path), "%s", Test_TempPath("character-actions.sav"));
    struct game_export *game = init_game();
    snprintf(test_playerinfo, sizeof(test_playerinfo), "\\race\\Human\\sex\\Male\\class\\8");
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    wowClient_t *client = &wow_clients[0]; wowEntityLocal_t *local = Wow_EntityLocal(&wow_edicts[0]);
    client->fireball_rank2_learned = false;
    client->action_spells[0] = SPELL_NONE;
    client->action_spells[1] = WOW_SPELL_FIREBALL_RANK2;
    client->action_spells[2] = WOW_SPELL_FROSTBOLT;
    T_ASSERT(Wow_CharacterSavePath(path, client, local));
    T_EQ(Wow_CharacterLoadPath(path, client, local), 1);
    T_EQ(client->action_spells[0], SPELL_NONE);
    T_EQ(client->action_spells[1], SPELL_NONE);
    T_EQ(client->action_spells[2], WOW_SPELL_FROSTBOLT);
    T_STREQ(client->actions[2].icon, Wow_SpellIcon(WOW_SPELL_FROSTBOLT));
    client->fireball_rank2_learned = true;
    client->action_spells[1] = WOW_SPELL_FIREBALL_RANK2;
    T_ASSERT(Wow_CharacterSavePath(path, client, local));
    T_EQ(Wow_CharacterLoadPath(path, client, local), 1);
    T_EQ(client->action_spells[1], WOW_SPELL_FIREBALL_RANK2);
    T_ASSERT(game->LoadMap("World/Maps/Azeroth/Azeroth.wdt"));
    T_EQ(client->action_spells[0], SPELL_NONE);
    T_EQ(client->action_spells[1], WOW_SPELL_FIREBALL_RANK2);
    game->Shutdown(); remove(path);
}
