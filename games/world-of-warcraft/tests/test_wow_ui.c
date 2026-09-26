#include "test.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "client/menu.h"
#include "common/mpq.h"
#include "common/wow_ui_shared.h"
#include "menu/menu_local.h"

#ifndef TEST_WOW_MPQ
#define TEST_WOW_MPQ "build/tests/test-wow.mpq"
#endif

struct texture {
    uint32_t texid;
    uint32_t width;
    uint32_t height;
    char name[256];
};

struct font {
    uint32_t size;
    char name[256];
};


static handle_t test_archive;
static PLAYER test_ps;
static refExport_t test_renderer;
static LPCTEXTURE test_textures[MAX_IMAGES];
static uint32_t next_texture_id;
static uint32_t loaded_textures;
static uint32_t missing_textures;
static uint32_t forbidden_texture_loads;
static uint32_t draw_panel_count;
static uint32_t draw_inventory_count;
static uint32_t draw_fill_count;
static uint32_t draw_text_count;
static uint32_t draw_cursor_count;
static uint32_t draw_minimap_count;
static uint32_t draw_tip_alert_count;
static char last_draw_text[256];
static char last_server_command[256];
static char last_cmd_execute_text[256];
static uint32_t last_panel_width;
static uint32_t last_panel_height;
static uint32_t last_inventory_width;
static uint32_t last_inventory_height;
static char test_show_tips[8];

static bool test_path_is_wow_default(cstring_t name) {
    return name &&
        (strstr(name, "Interface\\TargetingFrame\\UI-PlayerFrame.blp") ||
         strstr(name, "Interface\\MainMenuBar\\UI-MainMenuBar.blp") ||
         strstr(name, "Interface\\Glues\\LoadingBar\\"));
}

static uint32_t test_read32(uint8_t const *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int test_fs_read_file(cstring_t fileName, void **buf) {
    handle_t file;
    uint32_t size;
    uint32_t read = 0;

    if (!buf) {
        return -1;
    }
    *buf = NULL;
    if (!test_archive || !SFileOpenFileEx(test_archive, fileName, SFILE_OPEN_FROM_MPQ, &file)) {
        return -1;
    }
    size = SFileGetFileSize(file, NULL);
    *buf = calloc(1, (size_t)size + 1);
    if (!*buf || !SFileReadFile(file, *buf, size, &read, NULL) || read != size) {
        free(*buf);
        *buf = NULL;
        SFileCloseFile(file);
        return -1;
    }
    SFileCloseFile(file);
    return (int)size;
}

static void test_fs_free_file(void *buf) {
    free(buf);
}

static handle_t test_mem_alloc(long size) {
    return calloc(1, (size_t)size);
}

static void test_mem_free(handle_t mem) {
    free(mem);
}

static void test_printf(cstring_t fmt, ...) {
    (void)fmt;
}

static void test_read_texture_size(cstring_t name, LPTEXTURE texture) {
    void *buf = NULL;
    int size = test_fs_read_file(name, &buf);

    texture->width = 0;
    texture->height = 0;
    if (size >= 20 && buf && test_read32(buf) == ID_BLP2) {
        texture->width = test_read32((uint8_t const *)buf + 12);
        texture->height = test_read32((uint8_t const *)buf + 16);
    } else {
        missing_textures++;
    }
    test_fs_free_file(buf);
}

static LPTEXTURE test_load_texture(cstring_t name) {
    LPTEXTURE texture = calloc(1, sizeof(*texture));

    T_NOT_NULL(texture);
    if (!texture) {
        return NULL;
    }
    texture->texid = ++next_texture_id;
    snprintf(texture->name, sizeof(texture->name), "%s", name ? name : "");
    test_read_texture_size(name, texture);
    loaded_textures++;
    if (test_path_is_wow_default(name)) {
        forbidden_texture_loads++;
    }
    return texture;
}

static LPFONT test_load_font(cstring_t name, uint32_t size) {
    LPFONT font = calloc(1, sizeof(*font));

    T_NOT_NULL(font);
    if (!font) {
        return NULL;
    }
    font->size = size;
    snprintf(font->name, sizeof(font->name), "%s", name ? name : "");
    return font;
}

static void test_release_texture(LPTEXTURE texture) {
    free(texture);
}

static size2_t test_get_texture_size(LPCTEXTURE texture) {
    size2_t s = {0, 0};
    if (texture) { s.width = texture->width; s.height = texture->height; }
    return s;
}

static void test_draw_image(LPCTEXTURE texture, rect_t const * screen, rect_t const * uv, COLOR32 color) {
    (void)uv;
    (void)color;
    if (!texture) {
        return;
    }
    if (!strcmp(texture->name, "Interface\\Test\\LuaPanel.blp")) {
        draw_panel_count++;
        last_panel_width = texture->width;
        last_panel_height = texture->height;
    } else if (!strcmp(texture->name, "Interface\\Test\\Inventory.blp")) {
        draw_inventory_count++;
        last_inventory_width = texture->width;
        last_inventory_height = texture->height;
    } else if (!strcmp(texture->name, "Interface\\TutorialFrame\\TutorialFrameAlert") ||
               !strcmp(texture->name, "Interface\\TutorialFrame\\TutorialFrameAlert.blp")) {
        draw_tip_alert_count++;
    }
}

static void test_draw_image_ex(LPCDRAWIMAGE image) {
    if (image) {
        test_draw_image(image->texture, &image->screen, &image->uv, image->color);
    }
}

static void test_draw_fill(rect_t const * rect, COLOR32 color) {
    (void)rect;
    (void)color;
    draw_fill_count++;
}

static void test_draw_minimap(rect_t const * rect, cstring_t map) {
    (void)map;
    (void)rect;
    draw_minimap_count++;
}

static VECTOR2 test_get_text_size(LPCDRAWTEXT drawText) {
    float w = drawText && drawText->text ? (float)strlen(drawText->text) * 0.01f : 0.0f;
    float h = drawText && drawText->font ? drawText->font->size / 1000.0f : 0.012f;
    return MAKE(VECTOR2, w, h);
}

static void test_draw_text(LPCDRAWTEXT drawText) {
    draw_text_count++;
    snprintf(last_draw_text, sizeof(last_draw_text), "%s", drawText && drawText->text ? drawText->text : "");
    if (drawText && drawText->text && !strcmp(drawText->text, "|"))
        draw_cursor_count++;
}

static LPCTEXTURE test_get_texture(uint32_t index) {
    return index < MAX_IMAGES ? test_textures[index] : NULL;
}

static int test_image_index(cstring_t imageName) {
    FOR_LOOP(i, MAX_IMAGES) {
        LPCTEXTURE texture = test_textures[i];

        if (texture && !strcmp(texture->name, imageName)) {
            return (int)i;
        }
    }
    for (uint32_t i = 1; i < MAX_IMAGES; i++) {
        if (!test_textures[i]) {
            test_textures[i] = test_load_texture(imageName);
            return (int)i;
        }
    }
    return 0;
}

static LPCPLAYER test_get_player_state(void) {
    return &test_ps;
}



static LPRENDERER test_get_renderer(void) {
    return &test_renderer;
}

static void test_server_command(cstring_t text) {
    snprintf(last_server_command, sizeof(last_server_command), "%s", text ? text : "");
}

static void test_cmd_execute_text(cstring_t text) {
    snprintf(last_cmd_execute_text, sizeof(last_cmd_execute_text), "%s", text ? text : "");
}

static cstring_t test_cvar_string(cstring_t name, cstring_t fallback) {
    if (!strcmp(name, BZ_WOW_CVAR_SHOW_TIPS)) return test_show_tips;
    return fallback;
}

static void test_cvar_set(cstring_t name, cstring_t value) {
    if (!strcmp(name, BZ_WOW_CVAR_SHOW_TIPS)) snprintf(test_show_tips, sizeof(test_show_tips), "%s", value);
}

static void reset_test_state(void) {
    memset(&test_ps, 0, sizeof(test_ps));
    memset(test_textures, 0, sizeof(test_textures));
    memset(&test_renderer, 0, sizeof(test_renderer));
    memset(last_draw_text, 0, sizeof(last_draw_text));
    memset(last_server_command, 0, sizeof(last_server_command));
    memset(last_cmd_execute_text, 0, sizeof(last_cmd_execute_text));
    next_texture_id = 0;
    loaded_textures = 0;
    missing_textures = 0;
    forbidden_texture_loads = 0;
    draw_panel_count = 0;
    draw_inventory_count = 0;
    draw_fill_count = 0;
    draw_text_count = 0;
    draw_cursor_count = 0;
    draw_minimap_count = 0;
    draw_tip_alert_count = 0;
    last_panel_width = 0;
    last_panel_height = 0;
    last_inventory_width = 0;
    last_inventory_height = 0;
    snprintf(test_show_tips, sizeof(test_show_tips), "1");

    test_renderer.LoadTexture = test_load_texture;
    test_renderer.LoadFont = test_load_font;
    test_renderer.ReleaseTexture = test_release_texture;
    test_renderer.GetTextureSize = test_get_texture_size;
    test_renderer.DrawImage = test_draw_image;
    test_renderer.DrawImageEx = test_draw_image_ex;
    test_renderer.DrawFill = test_draw_fill;
    test_renderer.DrawMinimap = test_draw_minimap;
    test_renderer.DrawText = test_draw_text;
    test_renderer.GetTextSize = test_get_text_size;

    test_ps.client_ui_state = CLIENT_UI_GAME;
    test_ps.name = "LuaTester";
    test_ps.stats[WOW_STAT_HEALTH] = 77;
    test_ps.stats[WOW_STAT_HEALTH_MAX] = 100;
    test_ps.stats[WOW_STAT_POWER] = 55;
    test_ps.stats[WOW_STAT_POWER_MAX] = 80;
    test_ps.stats[WOW_STAT_LEVEL] = 9;
    test_ps.stats[WOW_STAT_SELECTED_ACTION] = 255;
}

static menuExport_t init_ui(void) {
    menuExport_t menu;

    menu = M_GetAPI((menuImport_t) { .FS_ReadFile = test_fs_read_file, .FS_FreeFile = test_fs_free_file, .MemAlloc = test_mem_alloc, .MemFree = test_mem_free, .Cmd_ExecuteText = test_cmd_execute_text, .ImageIndex = test_image_index, .ServerCommand = test_server_command, .Cvar_String = test_cvar_string, .Cvar_Set = test_cvar_set, .GetRenderer = test_get_renderer, .Printf = test_printf, });

    T_NOT_NULL(menu.Init);
    T_NOT_NULL(menu.Refresh);
    T_NOT_NULL(menu.Shutdown);
    menu.Init();
    return menu;
}

extern bool UIWow_RunLuaString(cstring_t name, cstring_t script);

TEST(wow_ui, wow_lua_ui_draws_from_generated_mpq) {
    menuExport_t menu;

    reset_test_state();
    T_ASSERT(SFileOpenArchive(TEST_WOW_MPQ, 0, 0, &test_archive));

    menu = init_ui();
    menu.Refresh(33);

    T_EQ((int)forbidden_texture_loads, 0);
    T_EQ((int)missing_textures, 0);
    T_EQ((int)draw_panel_count, 1);
    T_EQ((int)draw_inventory_count, 1);
    T_EQ((int)draw_fill_count, 1);
    T_EQ((int)draw_text_count, 1);
    T_EQ((int)draw_minimap_count, 0);
    T_EQ((int)last_panel_width, 16);
    T_EQ((int)last_panel_height, 8);
    T_EQ((int)last_inventory_width, 8);
    T_EQ((int)last_inventory_height, 8);
    T_STREQ(last_draw_text, "Login:33");
    T_STREQ(last_server_command, "");

    menu.Shutdown();
    FOR_LOOP(i, MAX_IMAGES) {
        if (test_textures[i]) {
            test_release_texture((LPTEXTURE)test_textures[i]);
            test_textures[i] = NULL;
        }
    }
    SFileCloseArchive(test_archive);
    test_archive = NULL;
}

TEST(wow_ui, enter_world_delegates_map_selection_to_server_playercreateinfo) {
    menuExport_t menu;

    reset_test_state();
    T_ASSERT(SFileOpenArchive(TEST_WOW_MPQ, 0, 0, &test_archive));
    menu = init_ui();
    T_ASSERT(UIWow_RunLuaString("enter_world_test", "EnterWorld()"));
    T_STREQ(last_cmd_execute_text, "map playercreate");
    menu.Shutdown();
    SFileCloseArchive(test_archive);
    test_archive = NULL;
}
