#include "test_framework.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "client/ui.h"
#include "common/mpq.h"
#include "common/wow_ui_shared.h"

#ifndef TEST_WOW_MPQ
#define TEST_WOW_MPQ "build/tests/test-wow.mpq"
#endif

struct texture {
    DWORD texid;
    DWORD width;
    DWORD height;
    char name[256];
};

struct font {
    DWORD size;
    char name[256];
};

int _tests_run = 0;
int _tests_failed = 0;

static HANDLE test_archive;
static PLAYER test_ps;
static refExport_t test_renderer;
static LPCTEXTURE test_textures[MAX_IMAGES];
static DWORD test_time;
static DWORD next_texture_id;
static DWORD loaded_textures;
static DWORD missing_textures;
static DWORD forbidden_texture_loads;
static DWORD draw_panel_count;
static DWORD draw_inventory_count;
static DWORD draw_fill_count;
static DWORD draw_text_count;
static DWORD draw_minimap_count;
static char last_draw_text[256];
static char last_server_command[256];
static char last_cmd_execute_text[256];
static DWORD last_panel_width;
static DWORD last_panel_height;
static DWORD last_inventory_width;
static DWORD last_inventory_height;
static DWORD glue_backdrop_edge_draws;
static DWORD glue_account_label_centered;
static FLOAT glue_button_uv_width;
static FLOAT glue_button_y;
static FLOAT glue_login_text_y;
static COLOR32 glue_login_text_color;

static BOOL test_path_is_wow_default(LPCSTR name) {
    return name &&
        (strstr(name, "Interface\\TargetingFrame\\UI-PlayerFrame.blp") ||
         strstr(name, "Interface\\MainMenuBar\\UI-MainMenuBar.blp") ||
         strstr(name, "Interface\\Glues\\LoadingBar\\"));
}

static DWORD test_read32(BYTE const *p) {
    return ((DWORD)p[0]) | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
}

static int test_fs_read_file(LPCSTR fileName, void **buf) {
    HANDLE file;
    DWORD size;
    DWORD read = 0;

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

static HANDLE test_mem_alloc(long size) {
    return calloc(1, (size_t)size);
}

static void test_mem_free(HANDLE mem) {
    free(mem);
}

static void test_printf(LPCSTR fmt, ...) {
    (void)fmt;
}

static void test_read_texture_size(LPCSTR name, LPTEXTURE texture) {
    void *buf = NULL;
    int size = test_fs_read_file(name, &buf);

    texture->width = 0;
    texture->height = 0;
    if (size >= 20 && buf && test_read32(buf) == ID_BLP2) {
        texture->width = test_read32((BYTE const *)buf + 12);
        texture->height = test_read32((BYTE const *)buf + 16);
    } else {
        missing_textures++;
    }
    test_fs_free_file(buf);
}

static LPTEXTURE test_load_texture(LPCSTR name) {
    LPTEXTURE texture = calloc(1, sizeof(*texture));

    ASSERT_NOT_NULL(texture);
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

static LPFONT test_load_font(LPCSTR name, DWORD size) {
    LPFONT font = calloc(1, sizeof(*font));

    ASSERT_NOT_NULL(font);
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

static void test_draw_image(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color) {
    (void)uv;
    (void)color;
    if (!texture) {
        return;
    }
    if (!strcmp(texture->name, "Interface\\Test\\LuaPanel.blp")) {
        draw_panel_count++;
        last_panel_width = texture->width;
        last_panel_height = texture->height;
        if (screen && uv && screen->w > 0.16f && screen->h > 0.055f) {
            glue_button_uv_width = uv->w;
            glue_button_y = screen->y;
        }
    } else if (!strcmp(texture->name, "Interface\\Test\\Inventory.blp")) {
        draw_inventory_count++;
        last_inventory_width = texture->width;
        last_inventory_height = texture->height;
        if (screen && screen->w < 0.03f) {
            glue_backdrop_edge_draws++;
        }
    }
}

static void test_draw_image_ex(LPCDRAWIMAGE image) {
    if (image) {
        test_draw_image(image->texture, &image->screen, &image->uv, image->color);
    }
}

static void test_draw_fill(LPCRECT rect, COLOR32 color) {
    (void)rect;
    (void)color;
    draw_fill_count++;
}

static void test_draw_minimap(LPCRECT rect) {
    (void)rect;
    draw_minimap_count++;
}

static void test_draw_text(LPCDRAWTEXT drawText) {
    draw_text_count++;
    snprintf(last_draw_text, sizeof(last_draw_text), "%s", drawText && drawText->text ? drawText->text : "");
    if (drawText && drawText->text && !strcmp(drawText->text, "Account Name") &&
        drawText->halign == FONT_JUSTIFYCENTER) {
        glue_account_label_centered++;
    }
    if (drawText && drawText->text && !strcmp(drawText->text, "LOGIN")) {
        glue_login_text_y = drawText->rect.y;
        glue_login_text_color = drawText->color;
    }
}

static LPCTEXTURE test_get_texture(DWORD index) {
    return index < MAX_IMAGES ? test_textures[index] : NULL;
}

static int test_image_index(LPCSTR imageName) {
    FOR_LOOP(i, MAX_IMAGES) {
        LPCTEXTURE texture = test_textures[i];

        if (texture && !strcmp(texture->name, imageName)) {
            return (int)i;
        }
    }
    for (DWORD i = 1; i < MAX_IMAGES; i++) {
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

static DWORD test_get_client_time(void) {
    return test_time;
}

static LPRENDERER test_get_renderer(void) {
    return &test_renderer;
}

static void test_server_command(LPCSTR text) {
    snprintf(last_server_command, sizeof(last_server_command), "%s", text ? text : "");
}

static void test_cmd_execute_text(LPCSTR text) {
    snprintf(last_cmd_execute_text, sizeof(last_cmd_execute_text), "%s", text ? text : "");
}

static void reset_test_state(void) {
    memset(&test_ps, 0, sizeof(test_ps));
    memset(test_textures, 0, sizeof(test_textures));
    memset(&test_renderer, 0, sizeof(test_renderer));
    memset(last_draw_text, 0, sizeof(last_draw_text));
    memset(last_server_command, 0, sizeof(last_server_command));
    memset(last_cmd_execute_text, 0, sizeof(last_cmd_execute_text));
    test_time = 1000;
    next_texture_id = 0;
    loaded_textures = 0;
    missing_textures = 0;
    forbidden_texture_loads = 0;
    draw_panel_count = 0;
    draw_inventory_count = 0;
    draw_fill_count = 0;
    draw_text_count = 0;
    draw_minimap_count = 0;
    last_panel_width = 0;
    last_panel_height = 0;
    last_inventory_width = 0;
    last_inventory_height = 0;
    glue_backdrop_edge_draws = 0;
    glue_account_label_centered = 0;
    glue_button_uv_width = 0.0f;
    glue_button_y = 0.0f;
    glue_login_text_y = 0.0f;
    glue_login_text_color = COLOR32_BLACK;

    test_renderer.LoadTexture = test_load_texture;
    test_renderer.LoadFont = test_load_font;
    test_renderer.ReleaseTexture = test_release_texture;
    test_renderer.DrawImage = test_draw_image;
    test_renderer.DrawImageEx = test_draw_image_ex;
    test_renderer.DrawFill = test_draw_fill;
    test_renderer.DrawMinimap = test_draw_minimap;
    test_renderer.DrawText = test_draw_text;

    test_ps.client_ui_state = CLIENT_UI_GAME;
    test_ps.name = "LuaTester";
    test_ps.stats[WOW_STAT_HEALTH] = 77;
    test_ps.stats[WOW_STAT_HEALTH_MAX] = 100;
    test_ps.stats[WOW_STAT_POWER] = 55;
    test_ps.stats[WOW_STAT_POWER_MAX] = 80;
    test_ps.stats[WOW_STAT_LEVEL] = 9;
}

static uiExport_t init_ui(void) {
    uiExport_t ui;

    ui = UI_GetAPI((uiImport_t) {
        .FS_ReadFile = test_fs_read_file,
        .FS_FreeFile = test_fs_free_file,
        .MemAlloc = test_mem_alloc,
        .MemFree = test_mem_free,
        .Cmd_ExecuteText = test_cmd_execute_text,
        .ImageIndex = test_image_index,
        .ServerCommand = test_server_command,
        .GetPlayerState = test_get_player_state,
        .GetTexture = test_get_texture,
        .GetClientTime = test_get_client_time,
        .GetRenderer = test_get_renderer,
        .Printf = test_printf,
    });
    ASSERT_NOT_NULL(ui.Init);
    ASSERT_NOT_NULL(ui.Refresh);
    ASSERT_NOT_NULL(ui.DrawFrame);
    ASSERT_NOT_NULL(ui.Shutdown);
    ui.Init();
    return ui;
}

extern BOOL UIWow_XMLLoadGlueFromToc(LPCSTR toc_path);
extern void UIWow_XMLDraw(void);
extern BOOL UIWow_XMLMouseEvent(int x, int y, int button, BOOL down);
extern BOOL UIWow_XMLTextInput(LPCSTR text);

static void test_wow_glue_xml_login_button_routes_next_screen(void) {
    uiExport_t ui;

    reset_test_state();
    ASSERT(SFileOpenArchive(TEST_WOW_MPQ, 0, 0, &test_archive));

    ui = init_ui();
    reset_test_state();
    ASSERT(UIWow_XMLLoadGlueFromToc("Interface\\GlueXML\\GlueXML.toc"));
    UIWow_XMLDraw();

    ASSERT(draw_panel_count > 0);
    ASSERT(draw_inventory_count > 0);
    ASSERT(glue_backdrop_edge_draws >= 2);
    ASSERT(glue_account_label_centered > 0);
    ASSERT(glue_button_uv_width > 0.5f && glue_button_uv_width < 0.7f);
    ASSERT(glue_login_text_y < glue_button_y);
    ASSERT_EQ_INT((int)glue_login_text_color.r, 255);
    ASSERT_EQ_INT((int)glue_login_text_color.g, 198);
    ASSERT_EQ_INT((int)glue_login_text_color.b, 0);
    ASSERT(draw_text_count > 0);
    ASSERT(UIWow_XMLMouseEvent(520, 400, 1, true));
    ASSERT(UIWow_XMLTextInput("A"));
    ASSERT(UIWow_XMLMouseEvent(520, 530, 1, true));
    ASSERT(UIWow_XMLMouseEvent(520, 530, 1, false));
    ASSERT_STR_EQ(last_cmd_execute_text, "+menu_character_select\n");

    ui.Shutdown();
    FOR_LOOP(i, MAX_IMAGES) {
        if (test_textures[i]) {
            test_release_texture((LPTEXTURE)test_textures[i]);
            test_textures[i] = NULL;
        }
    }
    SFileCloseArchive(test_archive);
    test_archive = NULL;
}

static void test_wow_lua_ui_draws_from_generated_mpq(void) {
    uiExport_t ui;
    uiUnitData_t unit;

    reset_test_state();
    ASSERT(SFileOpenArchive(TEST_WOW_MPQ, 0, 0, &test_archive));

    ui = init_ui();
    memset(&unit, 0, sizeof(unit));
    unit.num_inventory = 1;
    snprintf(unit.inventory[0].art, sizeof(unit.inventory[0].art), "%s", "Interface\\Test\\Inventory.blp");
    snprintf(unit.inventory[0].tooltip, sizeof(unit.inventory[0].tooltip), "%s", "Inventory");
    snprintf(unit.inventory[0].ubertip, sizeof(unit.inventory[0].ubertip), "%s", "1");
    unit.inventory[0].slot = 0;
    ui.UpdateUnitUI(1, &unit);
    ui.Refresh(33);
    ui.DrawFrame();

    ASSERT_EQ_INT((int)forbidden_texture_loads, 0);
    ASSERT_EQ_INT((int)missing_textures, 0);
    ASSERT_EQ_INT((int)draw_panel_count, 1);
    ASSERT_EQ_INT((int)draw_inventory_count, 1);
    ASSERT_EQ_INT((int)draw_fill_count, 1);
    ASSERT_EQ_INT((int)draw_text_count, 1);
    ASSERT_EQ_INT((int)draw_minimap_count, 1);
    ASSERT_EQ_INT((int)last_panel_width, 16);
    ASSERT_EQ_INT((int)last_panel_height, 8);
    ASSERT_EQ_INT((int)last_inventory_width, 8);
    ASSERT_EQ_INT((int)last_inventory_height, 8);
    ASSERT_STR_EQ(last_draw_text, "LuaTester:77:33");
    ASSERT_STR_EQ(last_server_command, "wow_lua_test 9 33");

    ui.Shutdown();
    FOR_LOOP(i, MAX_IMAGES) {
        if (test_textures[i]) {
            test_release_texture((LPTEXTURE)test_textures[i]);
            test_textures[i] = NULL;
        }
    }
    SFileCloseArchive(test_archive);
    test_archive = NULL;
}

int main(void) {
    RUN_TEST(test_wow_lua_ui_draws_from_generated_mpq);
    RUN_TEST(test_wow_glue_xml_login_button_routes_next_screen);
    TEST_RESULTS();
}
