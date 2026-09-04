#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "test.h"
#include "../menu/menu_local.h"
#include "../menu/menu_dialog.h"
#include "../menu/menu_screen.h"
#include "../common/minimap.h"
#include "../../../common/mpq.h"

static const char *captured_image_path;
static const char *captured_model_path;
static char captured_command[128];
static char captured_cvar_name[64];
static char captured_cvar_value[64];
static char captured_printf[512];
static DWORD captured_draw_calls;
static DWORD captured_dim_draws;
static DWORD captured_dim_draw_index;
static DWORD captured_text_draws;
static DWORD captured_stand_sprites;
static DWORD captured_realm_panel_sprites;
static uintptr_t fake_texture_id;
static LPTEXTURE hover_texture;
static DWORD captured_hover_draws;
static RECT captured_text_rects[8];
static VECTOR2 fake_text_size;
static HANDLE test_mpq_archive;
static BOOL hide_expansion_campaign_file;
static BOOL test_fs_expansion;
static LPCSTR test_campaign_mission_visibility;
static int test_campaign_played_mission = -1;
static VECTOR2 test_mouse_pos;
static LPCPLAYER test_player;
static LPCSTR test_map = "";
static DWORD map_reads, texture_releases;
static int fake_image_index(LPCSTR name) {
    captured_model_path = name;
    return (name && *name) ? 456 : 0;
}

static void parse_fdf(const char *name, const char *src) {
    char *buf = strdup(src);
    T_NOT_NULL(buf);
    if (!buf) {
        return;
    }
    UI_ParseFDF_Buffer(name, buf);
    free(buf);
}

static int require_not_null(const void *ptr) {
    T_NOT_NULL(ptr);
    return ptr != NULL;
}

TEST(menu_fdf, minimap_content_rect_preserves_rectangular_map_aspect) {
    RECT frame = { 10.0f, 20.0f, 100.0f, 100.0f };
    VECTOR2 wide = { 200.0f, 100.0f };
    VECTOR2 tall = { 100.0f, 200.0f };
    VECTOR2 invalid = { 0.0f, 100.0f };
    RECT content;

    content = WC3_MinimapContentRect(&frame, &wide);
    T_FEQ(content.x, 10.0f, 0.001f);
    T_FEQ(content.y, 45.0f, 0.001f);
    T_FEQ(content.w, 100.0f, 0.001f);
    T_FEQ(content.h, 50.0f, 0.001f);

    content = WC3_MinimapContentRect(&frame, &tall);
    T_FEQ(content.x, 35.0f, 0.001f);
    T_FEQ(content.y, 20.0f, 0.001f);
    T_FEQ(content.w, 50.0f, 0.001f);
    T_FEQ(content.h, 100.0f, 0.001f);

    content = WC3_MinimapContentRect(&frame, &invalid);
    T_FEQ(content.x, frame.x, 0.001f);
    T_FEQ(content.y, frame.y, 0.001f);
    T_FEQ(content.w, frame.w, 0.001f);
    T_FEQ(content.h, frame.h, 0.001f);
}

static int test_fs_read_file(LPCSTR file_name, void **buf) {
    HANDLE file;
    DWORD size;
    DWORD read;
    void *data;

    if (!buf) {
        return -1;
    }
    *buf = NULL;
    if (strstr(file_name, ".w3m")) map_reads++;

    if (!test_mpq_archive &&
        !SFileOpenArchive("build/tests/tests.mpq", 0, 0, &test_mpq_archive)) {
        return -1;
    }

    if (hide_expansion_campaign_file &&
        file_name &&
        !strcasecmp(file_name, "UI\\CampaignStrings_exp.txt")) {
        return -1;
    }

    if (!SFileOpenFileEx(test_mpq_archive, file_name, SFILE_OPEN_FROM_MPQ, &file)) {
        return -1;
    }

    size = SFileGetFileSize(file, NULL);
    data = malloc((size_t)size + 1);
    if (!data) {
        SFileCloseFile(file);
        return -1;
    }
    if (!SFileReadFile(file, data, size, &read, NULL) || read != size) {
        free(data);
        SFileCloseFile(file);
        return -1;
    }
    SFileCloseFile(file);
    ((char *)data)[size] = '\0';
    *buf = data;
    return (int)size;
}

static int test_missing_fs_read(LPCSTR file_name, void **buf) {
    (void)file_name;
    if (buf) *buf = NULL;
    return -1;
}

static void test_fs_free_file(void *buf) {
    free(buf);
}

static int test_image_index(LPCSTR name) {
    return (name && *name) ? 123 : 0;
}

/* Commented out — currently unused by any test stub. */
/* static int test_model_index(LPCSTR name) {
    return (name && *name) ? 456 : 0;
} */

static LPTEXTURE test_load_texture(LPCSTR name) {
    LPTEXTURE texture = (LPTEXTURE)(uintptr_t)(++fake_texture_id);

    captured_image_path = name;
    if (name && strstr(name, "Hover.blp")) {
        hover_texture = texture;
    }
    return texture;
}

static LPMODEL test_load_model(LPCSTR name) {
    captured_model_path = name;
    return (LPMODEL)1;
}

static LPFONT test_load_font(LPCSTR name, DWORD size) {
    (void)name;
    (void)size;
    return (LPFONT)1;
}

static VECTOR2 test_get_text_size(LPCDRAWTEXT draw_text) {
    (void)draw_text;
    return fake_text_size;
}

/* Commented out — currently unused by any test stub. */
/* static VECTOR2 test_ui_get_mouse_fdf(void) {
    return test_mouse_pos;
} */

static void test_draw_text(LPCDRAWTEXT draw_text) {
    if (captured_text_draws < sizeof(captured_text_rects) / sizeof(captured_text_rects[0]) &&
        draw_text) {
        captured_text_rects[captured_text_draws] = draw_text->rect;
    }
    captured_text_draws++;
}

static void test_draw_image_ex(LPCDRAWIMAGE draw_image) {
    captured_draw_calls++;
    if (draw_image && draw_image->texture == hover_texture) {
        captured_hover_draws++;
    }
    if (draw_image &&
        draw_image->color.r == 255 &&
        draw_image->color.g == 255 &&
        draw_image->color.b == 255 &&
        draw_image->color.a == 128) {
        captured_dim_draws++;
        captured_dim_draw_index = captured_draw_calls;
    }
}

static void test_draw_sprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    (void)model;
    (void)x;
    (void)y;
    if (anim && !strcmp(anim, "Stand"))
        captured_stand_sprites++;
    if (anim && !strcmp(anim, "RealmSelection Stand"))
        captured_realm_panel_sprites++;
}

static void test_draw_backdrop(LPCDRAWBACKDROP draw_backdrop) {
    (void)draw_backdrop;
    captured_draw_calls++;
}

static size2_t test_get_window_size(void) {
    return MAKE(size2_t, 1000, 750);
}

static void test_release_texture(LPTEXTURE texture) { (void)texture; texture_releases++; }

static LPRENDERER test_get_renderer(void) {
    static refExport_t renderer = {
        .LoadTexture = test_load_texture,
        .ReleaseTexture = test_release_texture,
        .LoadModel = test_load_model,
        .LoadFont = test_load_font,
        .GetWindowSize = test_get_window_size,
        .DrawImageEx = test_draw_image_ex,
        .DrawBackdrop = test_draw_backdrop,
        .DrawText = test_draw_text,
        .DrawSprite = test_draw_sprite,
        .GetTextSize = test_get_text_size,
    };
    return &renderer;
}

static int test_font_index(LPCSTR name, DWORD size) {
    (void)name;
    (void)size;
    return 1;
}

static HANDLE test_ui_mem_alloc(long size) {
    void *ptr = calloc(1u, (size_t)size);
    return ptr;
}

static void test_ui_mem_free(HANDLE ptr) {
    free(ptr);
}

static void test_ui_printf(LPCSTR fmt, ...) {
    va_list argptr;

    va_start(argptr, fmt);
    vsnprintf(captured_printf, sizeof(captured_printf), fmt ? fmt : "", argptr);
    va_end(argptr);
}

static void test_cmd_execute_text(LPCSTR text) {
    snprintf(captured_command, sizeof(captured_command), "%s", text ? text : "");
}

static LPCSTR test_cvar_string(LPCSTR name, LPCSTR fallback) {
    if (!strcmp(name, "map")) return test_map;
    if (name && !strcmp(name, "fs_expansion")) {
        return test_fs_expansion ? "1" : "0";
    }
    if (name && !strcmp(name, "game_port")) {
        return "27910";
    }
    if (name && !strcmp(name, "wc3_campaign_mission_visibility") && test_campaign_mission_visibility) {
        return test_campaign_mission_visibility;
    }
    if (name && !strncmp(name, "wc3_campaign_played_human_", 26)) {
        int mission = atoi(name + 26);
        return mission == test_campaign_played_mission ? "1" : "0";
    }
    return fallback;
}

static void test_cvar_set(LPCSTR name, LPCSTR value) {
    snprintf(captured_cvar_name, sizeof(captured_cvar_name), "%s", name ? name : "");
    snprintf(captured_cvar_value, sizeof(captured_cvar_value), "%s", value ? value : "");
}

static LPCPLAYER test_get_player_state(void) {
    return test_player;
}

static void load_ui_files(LPCSTR const *file_names, size_t count) {
    uiImport_t saved = uiimport;

    UI_ClearTemplates();
    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = test_fs_read_file;
    uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.GetPlayerState = test_get_player_state;
    uiimport.ImageIndex = test_image_index;
    uiimport.FontIndex = test_font_index;
    uiimport.Printf = test_ui_printf;
    for (size_t i = 0; i < count; i++) {
        UI_ParseFDF(file_names[i]);
    }
    uiimport = saved;
}

static void reset_ui_state(void) {
    UI_ClearTemplates();
    UI_ClearTextures();
    captured_image_path = NULL;
    captured_model_path = NULL;
    captured_command[0] = '\0';
    captured_printf[0] = '\0';
    captured_draw_calls = 0;
    captured_dim_draws = 0;
    captured_dim_draw_index = 0;
    captured_text_draws = 0;
    captured_stand_sprites = 0;
    captured_realm_panel_sprites = 0;
    fake_texture_id = 0;
    texture_releases = map_reads = 0;
    test_player = NULL; test_map = "";
    hover_texture = NULL;
    captured_hover_draws = 0;
    memset(captured_text_rects, 0, sizeof(captured_text_rects));
    fake_text_size = MAKE(VECTOR2, 0.050f, 0.016f);
    test_mouse_pos = MAKE(VECTOR2, 0, 0);
    UI_ClearEditFocus();
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.ImageIndex = fake_image_index;
    uiimport.FontIndex = test_font_index;
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Printf = test_ui_printf;
    uiimport.GetPlayerState = test_get_player_state;
    M_SetActive(true);
}

TEST(menu_fdf, parse_single_frame_definition) {
    LPFRAMEDEF root;

    reset_ui_state();
    parse_fdf("single.fdf",
              "Frame \"FRAME\" \"Root\" { Width 0.5, Height 0.25, }");

    root = UI_FindFrame("Root");
    if (!require_not_null(root)) return;
    T_EQ(root->Type, FT_FRAME);
    T_FEQ(root->Width, 0.5f, 0.01f);
    T_FEQ(root->Height, 0.25f, 0.01f);
    T_NULL(root->Parent);
}

TEST(menu_fdf, compact_pool_preserves_capacity_and_reports_exhaustion) {
    LPFRAMEDEF last = NULL;
    reset_ui_state();
    FOR_LOOP(i, MAX_UI_CLASSES - 1) last = UI_Spawn(FT_FRAME, NULL);
    T_NOT_NULL(last); T_ASSERT(last == &frames[MAX_UI_CLASSES - 1]);
    T_NULL(UI_Spawn(FT_FRAME, NULL));
    T_ASSERT(sizeof(FRAMEPOINT) <= sizeof(void *) + 8);
}

TEST(menu_fdf, parse_nested_parent_child_relationship) {
    LPFRAMEDEF root;
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("nested.fdf",
              "Frame \"FRAME\" \"Root\" { "
              "Frame \"TEXT\" \"Child\" { Text \"Hello\", }"
              " }");

    root = UI_FindFrame("Root");
    child = UI_FindFrame("Child");
    if (!require_not_null(root)) return;
    if (!require_not_null(child)) return;
    T_EQ(child->Type, FT_TEXT);
    T_ASSERT(child->Parent == root);
    T_STREQ(child->Text, "Hello");
}

TEST(menu_fdf, inherits_copies_compatible_type_fields) {
    LPFRAMEDEF base;
    LPFRAMEDEF derived;

    reset_ui_state();
    parse_fdf("inherits_ok.fdf",
              "Frame \"FRAME\" \"Base\" { Width 0.33, Height 0.44, }"
              "Frame \"FRAME\" \"Derived\" INHERITS \"Base\" { }"
    );

    base = UI_FindFrame("Base");
    derived = UI_FindFrame("Derived");

    if (!require_not_null(base)) return;
    if (!require_not_null(derived)) return;
    T_FEQ(derived->Width, base->Width, 0.01f);
    T_FEQ(derived->Height, base->Height, 0.01f);
    T_EQ(derived->Type, FT_FRAME);
}

TEST(menu_fdf, inherits_rejects_incompatible_type) {
    LPFRAMEDEF base_text;
    LPFRAMEDEF derived_frame;

    reset_ui_state();
    parse_fdf("inherits_bad.fdf",
              "Frame \"TEXT\" \"BaseText\" { Width 0.77, }"
              "Frame \"FRAME\" \"DerivedFrame\" INHERITS \"BaseText\" { Height 0.25, }"
    );

    base_text = UI_FindFrame("BaseText");
    derived_frame = UI_FindFrame("DerivedFrame");

    if (!require_not_null(base_text)) return;
    if (!require_not_null(derived_frame)) return;
    T_FEQ(base_text->Width, 0.77f, 0.01f);
    T_FEQ(derived_frame->Height, 0.25f, 0.01f);
    T_FEQ(derived_frame->Width, 0.0f, 0.01f);
}

TEST(menu_fdf, setpoint_top_left_sets_top_y_anchor) {
    LPFRAMEDEF root;
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_tl.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint TOPLEFT, \"Root\", TOPLEFT, 0.01, -0.02,"
              " }"
              "}");

    root = UI_FindFrame("Root");
    child = UI_FindFrame("Child");
    if (!require_not_null(root)) return;
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MIN].used, 1);
    T_EQ(child->Points.x[FPP_MIN].targetPos, FPP_MIN);
    T_FEQ(child->Points.x[FPP_MIN].offset, 0.01f, 0.01f);
    T_ASSERT(child->Points.x[FPP_MIN].relativeTo == root);

    T_EQ(child->Points.y[FPP_MIN].used, 1);
    T_EQ(child->Points.y[FPP_MIN].targetPos, FPP_MIN);
    T_FEQ(child->Points.y[FPP_MIN].offset, -0.02f, 0.01f);
    T_ASSERT(child->Points.y[FPP_MIN].relativeTo == root);
}

TEST(menu_fdf, setallpoints_sets_min_and_max) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setall.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" { SetAllPoints, }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MIN].used, 1);
    T_EQ(child->Points.x[FPP_MAX].used, 1);
    T_EQ(child->Points.y[FPP_MIN].used, 1);
    T_EQ(child->Points.y[FPP_MAX].used, 1);
}

TEST(menu_fdf, anchor_translates_to_setpoint_state) {
    LPFRAMEDEF frame;

    reset_ui_state();
    parse_fdf("anchor.fdf",
              "Frame \"FRAME\" \"Root\" { Anchor BOTTOMRIGHT, -0.03, 0.04, }");

    frame = UI_FindFrame("Root");
    if (!require_not_null(frame)) return;
    T_EQ(frame->Points.x[FPP_MAX].used, 1);
    T_EQ(frame->Points.y[FPP_MAX].used, 1);
    T_FEQ(frame->Points.x[FPP_MAX].offset, -0.03f, 0.01f);
    T_FEQ(frame->Points.y[FPP_MAX].offset, 0.04f, 0.01f);
}

TEST(menu_fdf, backdrop_flags_and_insets_are_parsed) {
    LPFRAMEDEF frame;

    reset_ui_state();
    parse_fdf("backdrop_flags.fdf",
              "Frame \"BACKDROP\" \"BD\" {"
              " BackdropTileBackground,"
              " BackdropBlendAll,"
              " BackdropBackgroundInsets 0.1 0.2 0.3 0.4,"
              "}"
    );

    frame = UI_FindFrame("BD");
    if (!require_not_null(frame)) return;
    T_EQ(frame->Type, FT_BACKDROP);
    T_EQ(frame->Backdrop.TileBackground, 1);
    T_EQ(frame->Backdrop.BlendAll, 1);
    T_FEQ(frame->Backdrop.BackgroundInsets[0], 0.1f, 0.01f);
    T_FEQ(frame->Backdrop.BackgroundInsets[1], 0.2f, 0.01f);
    T_FEQ(frame->Backdrop.BackgroundInsets[2], 0.3f, 0.01f);
    T_FEQ(frame->Backdrop.BackgroundInsets[3], 0.4f, 0.01f);
}

TEST(menu_fdf, vector_parser_accepts_f_suffixes) {
    LPFRAMEDEF frame;

    reset_ui_state();
    parse_fdf("vector_f_suffix.fdf",
              "Frame \"TEXTBUTTON\" \"Button\" {"
              " ButtonPushedTextOffset -0.002f -0.003f,"
              "}");

    frame = UI_FindFrame("Button");
    if (!require_not_null(frame)) return;
    T_FEQ(frame->Button.PushedTextOffset.x, -0.002f, 0.01f);
    T_FEQ(frame->Button.PushedTextOffset.y, -0.003f, 0.01f);
}

TEST(menu_fdf, chat_display_tokens_map_to_text_area) {
    reset_ui_state();
    parse_fdf("chat_display.fdf", "Frame \"CHATDISPLAY\" \"Chat\" { ChatDisplayLineHeight 0.012, ChatDisplayBorderSize 0.034, }");
    LPFRAMEDEF frame = UI_FindFrame("Chat");
    if (!require_not_null(frame)) return;
    T_FEQ(frame->TextArea.LineHeight, 0.012f, 0.01f);
    T_FEQ(frame->TextArea.Inset, 0.034f, 0.01f);
}

TEST(menu_fdf, comments_are_ignored_inside_frame_bodies) {
    LPFRAMEDEF frame;

    reset_ui_state();
    parse_fdf("comments_in_body.fdf",
              "// leading file comment\n"
              "/* leading block comment */\n"
              "Frame \"BACKDROP\" \"CommentedFrame\" {\n"
              "    // Disabled source asset line from shipped FDF files\n"
              "    // Anchor TOPLEFT, 0.259375, -0.003125,\n"
              "    Anchor /* corner */ TOPLEFT, /* x */ 0.25, /* y */ -0.125,\n"
              "    Width 0.5, // inline value comment\n"
              "    Height /* block before value */ 0.25,\n"
              "}\n");

    frame = UI_FindFrame("CommentedFrame");
    if (!require_not_null(frame)) return;
    T_FEQ(frame->Points.x[FPP_MIN].offset, 0.25f, 0.01f);
    T_FEQ(frame->Points.y[FPP_MIN].offset, -0.125f, 0.01f);
    T_FEQ(frame->Width, 0.5f, 0.01f);
    T_FEQ(frame->Height, 0.25f, 0.01f);
}

TEST(menu_fdf, comments_are_ignored_between_setpoint_arguments) {
    LPFRAMEDEF root;
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("comments_in_args.fdf",
              "Frame \"FRAME\" \"Root\" {\n"
              "    Width 0.8,\n"
              "    Frame \"FRAME\" \"Child\" {\n"
              "        SetPoint TOPLEFT /* after first arg */,\n"
              "                 // relative frame on the next line\n"
              "                 \"Root\",\n"
              "                 /* target point */ TOPLEFT,\n"
              "                 0.125 /* x before comma */,\n"
              "                 // y offset can be separated by a source comment\n"
              "                 -0.25,\n"
              "    }\n"
              "}\n");

    root = UI_FindFrame("Root");
    child = UI_FindFrame("Child");
    if (!require_not_null(root)) return;
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MIN].used, 1);
    T_EQ(child->Points.x[FPP_MIN].targetPos, FPP_MIN);
    T_FEQ(child->Points.x[FPP_MIN].offset, 0.125f, 0.01f);
    T_ASSERT(child->Points.x[FPP_MIN].relativeTo == root);

    T_EQ(child->Points.y[FPP_MIN].used, 1);
    T_EQ(child->Points.y[FPP_MIN].targetPos, FPP_MIN);
    T_FEQ(child->Points.y[FPP_MIN].offset, -0.25f, 0.01f);
    T_ASSERT(child->Points.y[FPP_MIN].relativeTo == root);
}

TEST(menu_fdf, comment_markers_inside_quoted_strings_are_preserved) {
    LPFRAMEDEF text;
    LPFRAMEDEF more_text;

    reset_ui_state();
    parse_fdf("quoted_comment_markers.fdf",
              "StringList {\n"
              "    QUOTED_TEXT \"literal // text and /* block marker */ text\",\n"
              "    MORE_TEXT \"more /* marker */ and // marker text\",\n"
              "}\n"
              "Frame \"TEXT\" \"QuotedText\" {\n"
              "    Text \"QUOTED_TEXT\", // real parser comment\n"
              "}\n"
              "Frame \"TEXT\" \"MoreText\" {\n"
              "    Text \"MORE_TEXT\", /* real block comment */\n"
              "}\n");

    text = UI_FindFrame("QuotedText");
    more_text = UI_FindFrame("MoreText");
    if (!require_not_null(text)) return;
    if (!require_not_null(more_text)) return;
    T_STREQ(text->Text, "literal // text and /* block marker */ text");
    T_STREQ(more_text->Text, "more /* marker */ and // marker text");
}

TEST(menu_fdf, global_stringlist_keys_resolve_and_clear_with_templates) {
    reset_ui_state();
    parse_fdf("global_strings.fdf",
              "StringList {\n"
              "    COLON_DAMAGE \"Damage:\"\n"
              "    COLON_FOOD_PROVIDED \"Food Provided:\"\n"
              "    COLON_STRENGTH \"Strength:\"\n"
              "    COLON_GOLD \"Gold:\"\n"
              "    INFOPANEL_LEVEL_CLASS \"Level %u %s\"\n"
              "    INVENTORY \"Inventory\"\n"
              "}\n");

    T_STREQ(UI_GetString("COLON_DAMAGE"), "Damage:");
    T_STREQ(UI_GetString("COLON_FOOD_PROVIDED"), "Food Provided:");
    T_STREQ(UI_GetString("COLON_STRENGTH"), "Strength:");
    T_STREQ(UI_GetString("COLON_GOLD"), "Gold:");
    T_STREQ(UI_GetString("INFOPANEL_LEVEL_CLASS"), "Level %u %s");
    T_STREQ(UI_GetString("INVENTORY"), "Inventory");

    UI_ClearTemplates();
    T_STREQ(UI_GetString("COLON_DAMAGE"), "COLON_DAMAGE");
}

TEST(menu_fdf, split_infopanel_stringlists_resolve_before_frame_parse) {
    LPFRAMEDEF damage;
    LPFRAMEDEF strength;

    reset_ui_state();
    parse_fdf("GlobalStrings.fdf",
              "StringList {\n"
              "    COLON_ARMOR \"Armor:\"\n"
              "}\n");
    parse_fdf("InfoPanelStrings.fdf",
              "StringList {\n"
              "    COLON_DAMAGE \"Damage:\"\n"
              "    COLON_STRENGTH \"Strength:\"\n"
              "}\n");
    parse_fdf("SimpleInfoPanel.fdf",
              "String \"DamageLabel\" { Text \"COLON_DAMAGE\", }\n"
              "String \"StrengthLabel\" { Text \"COLON_STRENGTH\", }\n");

    damage = UI_FindFrame("DamageLabel");
    strength = UI_FindFrame("StrengthLabel");
    if (!require_not_null(damage)) return;
    if (!require_not_null(strength)) return;

    T_STREQ(UI_GetString("COLON_ARMOR"), "Armor:");
    T_STREQ(UI_GetString("COLON_DAMAGE"), "Damage:");
    T_STREQ(damage->Text, "Damage:");
    T_STREQ(strength->Text, "Strength:");
}

TEST(menu_fdf, shipped_style_disabled_properties_do_not_escape_comments) {
    LPFRAMEDEF root;
    LPFRAMEDEF icon;
    LPFRAMEDEF text;

    reset_ui_state();
    parse_fdf("resourcebar_comments.fdf",
              "/* ResourceBar-style source comments around disabled art. */\n"
              "Frame \"FRAME\" \"ResourceRoot\" {\n"
              "    Texture \"ResourceBarGoldIcon\" {\n"
              "        // Anchor TOPLEFT, 0.259375, -0.003125,\n"
              "        // File \"UpkeepIcon\",\n"
              "        Anchor TOPLEFT, 0.010, -0.020,\n"
              "        File \"UI\\\\Feedback\\\\Resources\\\\ResourceGold.blp\",\n"
              "    }\n"
              "    String \"ResourceBarUpkeepText\" {\n"
              "        // SetPoint TOPLEFT, \"ResourceBarGoldIcon\", TOPRIGHT, 0.004, 0.000,\n"
              "        SetPoint LEFT, \"ResourceBarGoldIcon\", RIGHT, 0.030, 0.000,\n"
              "        Text \"No Upkeep\",\n"
              "    }\n"
              "    // The parser must resume with real children after comment-only lines.\n"
              "    Frame \"TEXT\" \"AfterCommentText\" {\n"
              "        Text \"AFTER_COMMENT\",\n"
              "    }\n"
              "}\n");

    root = UI_FindFrame("ResourceRoot");
    icon = UI_FindFrame("ResourceBarGoldIcon");
    text = UI_FindFrame("ResourceBarUpkeepText");
    if (!require_not_null(root)) return;
    if (!require_not_null(icon)) return;
    if (!require_not_null(text)) return;

    T_ASSERT(icon->Parent == root);
    T_ASSERT(text->Parent == root);
    T_EQ(icon->Type, FT_TEXTURE);
    T_EQ(text->Type, FT_STRING);
    T_EQ(icon->Points.x[FPP_MIN].used, 1);
    T_FEQ(icon->Points.x[FPP_MIN].offset, 0.010f, 0.01f);
    T_EQ(text->Points.x[FPP_MIN].used, 1);
    T_FEQ(text->Points.x[FPP_MIN].offset, 0.030f, 0.01f);
    T_ASSERT(text->Points.x[FPP_MIN].relativeTo == icon);
    T_STREQ(text->Text, "No Upkeep");
    T_NOT_NULL(UI_FindFrame("AfterCommentText"));
}

TEST(menu_fdf, backdrop_background_adds_blp_extension) {
    LPFRAMEDEF frame;

    reset_ui_state();
    parse_fdf("backdrop_bg_path.fdf",
              "Frame \"BACKDROP\" \"BD\" {"
              " BackdropBackground \"TestUI/Textures/checker_8x8\","
              "}");

    frame = UI_FindFrame("BD");
    if (!require_not_null(frame)) return;
    T_EQ(frame->Backdrop.Background, 1);
    T_NULL(captured_image_path);
    T_NOT_NULL(UI_GetTexture(frame->Backdrop.Background));
    T_ASSERT(UI_GetTexture(frame->Backdrop.Background) == UI_GetTexture(frame->Backdrop.Background));
    T_EQ(fake_texture_id, 1);
    T_NOT_NULL(captured_image_path);
    T_STREQ(captured_image_path, "TestUI/Textures/checker_8x8.blp");
}

TEST(menu_fdf, background_art_uses_model_index) {
    LPFRAMEDEF sprite;

    reset_ui_state();
    parse_fdf("sprite_path.fdf",
              "Frame \"SPRITE\" \"SpriteA\" {"
              " BackgroundArt \"TestUI/Models/quad_sprite.mdx\","
              "}");

    sprite = UI_FindFrame("SpriteA");
    if (!require_not_null(sprite)) return;
    T_EQ(sprite->Type, FT_SPRITE);
    T_EQ(sprite->Portrait.model, 1);
    T_NOT_NULL(captured_model_path);
    T_STREQ(captured_model_path, "TestUI/Models/quad_sprite.mdx");
}

TEST(menu_fdf, collect_frame_tree_preorder_matches_writer_traversal) {
    LPCFRAMEDEF out[8];
    DWORD count;
    LPFRAMEDEF root;
    LPFRAMEDEF child_a;
    LPFRAMEDEF child_b;
    LPFRAMEDEF grand;

    reset_ui_state();
    parse_fdf("collect_tree.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"A\" {"
              "  Frame \"TEXT\" \"A1\" { Text \"x\", }"
              " }"
              " Frame \"FRAME\" \"B\" { }"
              "}");

    root = UI_FindFrame("Root");
    child_a = UI_FindFrame("A");
    child_b = UI_FindFrame("B");
    grand = UI_FindFrame("A1");
    if (!require_not_null(root)) return;
    if (!require_not_null(child_a)) return;
    if (!require_not_null(child_b)) return;
    if (!require_not_null(grand)) return;

    memset(out, 0, sizeof(out));
    count = UI_CollectFrameTree(root, out, 8);

    T_EQ((int)count, 4);
    T_ASSERT(out[0] == root);
    T_ASSERT(out[1] == child_a);
    T_ASSERT(out[2] == grand);
    T_ASSERT(out[3] == child_b);
}

TEST(menu_fdf, collect_frame_tree_skips_hidden_children) {
    LPCFRAMEDEF out[4];
    DWORD count;
    LPFRAMEDEF root;
    LPFRAMEDEF visible;
    LPFRAMEDEF hidden;

    reset_ui_state();
    parse_fdf("collect_hidden.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Visible\" { }"
              " Frame \"FRAME\" \"Hidden\" { }"
              "}");

    root = UI_FindFrame("Root");
    visible = UI_FindFrame("Visible");
    hidden = UI_FindFrame("Hidden");
    if (!require_not_null(root)) return;
    if (!require_not_null(visible)) return;
    if (!require_not_null(hidden)) return;

    UI_SetHidden(hidden, true);

    memset(out, 0, sizeof(out));
    count = UI_CollectFrameTree(root, out, 4);

    T_EQ((int)count, 2);
    T_ASSERT(out[0] == root);
    T_ASSERT(out[1] == visible);
}

TEST(menu_fdf, collect_frame_tree_skips_button_control_art) {
    LPCFRAMEDEF out[4];
    DWORD count;
    LPFRAMEDEF button;
    LPFRAMEDEF text;

    reset_ui_state();
    parse_fdf("collect_button_art.fdf",
              "Frame \"GLUETEXTBUTTON\" \"Button\" {"
              " ControlBackdrop \"ButtonBackdrop\","
              " ControlPushedBackdrop \"ButtonPushedBackdrop\","
              " ControlDisabledBackdrop \"ButtonDisabledBackdrop\","
              " ControlMouseOverHighlight \"ButtonHighlight\","
              " Frame \"BACKDROP\" \"ButtonBackdrop\" { }"
              " Frame \"BACKDROP\" \"ButtonPushedBackdrop\" { }"
              " Frame \"BACKDROP\" \"ButtonDisabledBackdrop\" { }"
              " Frame \"HIGHLIGHT\" \"ButtonHighlight\" { }"
              " Frame \"TEXT\" \"ButtonText\" { Text \"x\", }"
              "}");

    button = UI_FindFrame("Button");
    text = UI_FindFrame("ButtonText");
    if (!require_not_null(button)) return;
    if (!require_not_null(text)) return;

    memset(out, 0, sizeof(out));
    count = UI_CollectFrameTree(button, out, 4);

    T_EQ((int)count, 2);
    T_ASSERT(out[0] == button);
    T_ASSERT(out[1] == text);
}

TEST(menu_fdf, collect_frame_tree_skips_editbox_text_frame) {
    LPCFRAMEDEF out[4];
    DWORD count;
    LPFRAMEDEF editbox;

    reset_ui_state();
    parse_fdf("collect_editbox_text.fdf",
              "Frame \"EDITBOX\" \"Edit\" {"
              " EditTextFrame \"EditText\","
              " Frame \"TEXT\" \"EditText\" { Text \"x\", }"
              "}");

    editbox = UI_FindFrame("Edit");
    if (!require_not_null(editbox)) return;

    memset(out, 0, sizeof(out));
    count = UI_CollectFrameTree(editbox, out, 4);

    T_EQ((int)count, 1);
    T_ASSERT(out[0] == editbox);
}

TEST(menu_fdf, collect_frame_tree_returns_total_when_truncated) {
    LPCFRAMEDEF out[2];
    DWORD count;
    LPFRAMEDEF root;

    reset_ui_state();
    parse_fdf("collect_truncated.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"A\" { }"
              " Frame \"FRAME\" \"B\" { }"
              " Frame \"FRAME\" \"C\" { }"
              "}");

    root = UI_FindFrame("Root");
    if (!require_not_null(root)) return;

    memset(out, 0, sizeof(out));
    count = UI_CollectFrameTree(root, out, 2);

    T_EQ((int)count, 4);
    T_ASSERT(out[0] == root);
    T_NOT_NULL(out[1]);
}

TEST(menu_fdf, find_child_frame_descends_recursively) {
    LPFRAMEDEF root;
    LPFRAMEDEF found;

    reset_ui_state();
    parse_fdf("find_child.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"L1\" {"
              "  Frame \"FRAME\" \"L2\" { }"
              " }"
              "}");

    root = UI_FindFrame("Root");
    if (!require_not_null(root)) return;

    found = UI_FindChildFrame(root, "L2");
    if (!require_not_null(found)) return;
    T_STREQ(found->Name, "L2");
}

TEST(menu_fdf, programmatic_setpoint_maps_to_points) {
    FRAMEDEF root;
    FRAMEDEF child;

    reset_ui_state();

    UI_InitFrame(&root, FT_FRAME);
    strcpy(root.Name, "Root");

    UI_InitFrame(&child, FT_FRAME);
    strcpy(child.Name, "Child");

    UI_SetPoint(&child, FRAMEPOINT_CENTER, &root, FRAMEPOINT_TOPLEFT, 0.11f, -0.22f);

    T_EQ(child.Points.x[FPP_MID].used, 1);
    T_EQ(child.Points.y[FPP_MID].used, 1);
    T_EQ(child.Points.x[FPP_MID].targetPos, FPP_MIN);
    T_EQ(child.Points.y[FPP_MID].targetPos, FPP_MIN);
    T_FEQ(child.Points.x[FPP_MID].offset, 0.11f, 0.01f);
    T_FEQ(child.Points.y[FPP_MID].offset, -0.22f, 0.01f);
    T_ASSERT(child.Points.x[FPP_MID].relativeTo == &root);
    T_ASSERT(child.Points.y[FPP_MID].relativeTo == &root);
}

TEST(menu_fdf, programmatic_setallpoints_sets_both_axes) {
    FRAMEDEF frame;

    reset_ui_state();
    UI_InitFrame(&frame, FT_FRAME);
    UI_SetAllPoints(&frame);

    T_EQ(frame.Points.x[FPP_MIN].used, 1);
    T_EQ(frame.Points.x[FPP_MAX].used, 1);
    T_EQ(frame.Points.y[FPP_MIN].used, 1);
    T_EQ(frame.Points.y[FPP_MAX].used, 1);
}

/* --- SetPoint: coverage for each FRAMEPOINT position --- */

TEST(menu_fdf, setpoint_top_maps_mid_x_max_y) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_top.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint TOP, \"Root\", TOP, 0.03, -0.07,"
              " }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MID].used, 1);
    T_EQ(child->Points.x[FPP_MID].targetPos, FPP_MID);
    T_FEQ(child->Points.x[FPP_MID].offset, 0.03f, 0.01f);

    T_EQ(child->Points.y[FPP_MIN].used, 1);
    T_EQ(child->Points.y[FPP_MIN].targetPos, FPP_MIN);
    T_FEQ(child->Points.y[FPP_MIN].offset, -0.07f, 0.01f);
}

TEST(menu_fdf, setpoint_topright_maps_max_x_max_y) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_topright.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint TOPRIGHT, \"Root\", TOPRIGHT, 0.05, -0.10,"
              " }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MAX].used, 1);
    T_EQ(child->Points.x[FPP_MAX].targetPos, FPP_MAX);
    T_FEQ(child->Points.x[FPP_MAX].offset, 0.05f, 0.01f);

    T_EQ(child->Points.y[FPP_MIN].used, 1);
    T_EQ(child->Points.y[FPP_MIN].targetPos, FPP_MIN);
    T_FEQ(child->Points.y[FPP_MIN].offset, -0.10f, 0.01f);
}

TEST(menu_fdf, setpoint_left_maps_min_x_mid_y) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_left.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint LEFT, \"Root\", LEFT, 0.02, 0.04,"
              " }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MIN].used, 1);
    T_EQ(child->Points.x[FPP_MIN].targetPos, FPP_MIN);
    T_FEQ(child->Points.x[FPP_MIN].offset, 0.02f, 0.01f);

    T_EQ(child->Points.y[FPP_MID].used, 1);
    T_EQ(child->Points.y[FPP_MID].targetPos, FPP_MID);
    T_FEQ(child->Points.y[FPP_MID].offset, 0.04f, 0.01f);
}

TEST(menu_fdf, setpoint_center_maps_mid_x_mid_y) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_center.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint CENTER, \"Root\", CENTER, 0.0, 0.0,"
              " }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MID].used, 1);
    T_EQ(child->Points.x[FPP_MID].targetPos, FPP_MID);
    T_FEQ(child->Points.x[FPP_MID].offset, 0.0f, 0.01f);

    T_EQ(child->Points.y[FPP_MID].used, 1);
    T_EQ(child->Points.y[FPP_MID].targetPos, FPP_MID);
    T_FEQ(child->Points.y[FPP_MID].offset, 0.0f, 0.01f);
}

TEST(menu_fdf, setpoint_right_maps_max_x_mid_y) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_right.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint RIGHT, \"Root\", RIGHT, -0.06, 0.0,"
              " }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MAX].used, 1);
    T_EQ(child->Points.x[FPP_MAX].targetPos, FPP_MAX);
    T_FEQ(child->Points.x[FPP_MAX].offset, -0.06f, 0.01f);

    T_EQ(child->Points.y[FPP_MID].used, 1);
    T_EQ(child->Points.y[FPP_MID].targetPos, FPP_MID);
    T_FEQ(child->Points.y[FPP_MID].offset, 0.0f, 0.01f);
}

TEST(menu_fdf, setpoint_bottomleft_maps_min_x_min_y) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_bottomleft.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint BOTTOMLEFT, \"Root\", BOTTOMLEFT, 0.01, 0.02,"
              " }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MIN].used, 1);
    T_EQ(child->Points.x[FPP_MIN].targetPos, FPP_MIN);
    T_FEQ(child->Points.x[FPP_MIN].offset, 0.01f, 0.01f);

    T_EQ(child->Points.y[FPP_MAX].used, 1);
    T_EQ(child->Points.y[FPP_MAX].targetPos, FPP_MAX);
    T_FEQ(child->Points.y[FPP_MAX].offset, 0.02f, 0.01f);
}

TEST(menu_fdf, setpoint_bottom_maps_mid_x_min_y) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_bottom.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint BOTTOM, \"Root\", BOTTOM, 0.0, 0.08,"
              " }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MID].used, 1);
    T_EQ(child->Points.x[FPP_MID].targetPos, FPP_MID);
    T_FEQ(child->Points.x[FPP_MID].offset, 0.0f, 0.01f);

    T_EQ(child->Points.y[FPP_MAX].used, 1);
    T_EQ(child->Points.y[FPP_MAX].targetPos, FPP_MAX);
    T_FEQ(child->Points.y[FPP_MAX].offset, 0.08f, 0.01f);
}

TEST(menu_fdf, setpoint_bottomright_maps_max_x_min_y) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setpoint_bottomright.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" {"
              "  SetPoint BOTTOMRIGHT, \"Root\", BOTTOMRIGHT, -0.04, 0.05,"
              " }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    T_EQ(child->Points.x[FPP_MAX].used, 1);
    T_EQ(child->Points.x[FPP_MAX].targetPos, FPP_MAX);
    T_FEQ(child->Points.x[FPP_MAX].offset, -0.04f, 0.01f);

    T_EQ(child->Points.y[FPP_MAX].used, 1);
    T_EQ(child->Points.y[FPP_MAX].targetPos, FPP_MAX);
    T_FEQ(child->Points.y[FPP_MAX].offset, 0.05f, 0.01f);
}

/* --- SetPoint: center-mutex behavior --- */

TEST(menu_fdf, setpoint_edge_overrides_and_clears_center) {
    FRAMEDEF frame;

    reset_ui_state();
    UI_InitFrame(&frame, FT_FRAME);

    /* Set CENTER first — both mid slots occupied */
    UI_SetPoint(&frame, FRAMEPOINT_CENTER, NULL, FRAMEPOINT_CENTER, 0.5f, 0.5f);
    T_EQ(frame.Points.x[FPP_MID].used, 1);
    T_EQ(frame.Points.y[FPP_MID].used, 1);

    /* Set TOPLEFT — should clear x-mid and y-mid, then fill x-min and y-min */
    UI_SetPoint(&frame, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    T_EQ(frame.Points.x[FPP_MID].used, 0);
    T_EQ(frame.Points.x[FPP_MIN].used, 1);
    T_EQ(frame.Points.y[FPP_MID].used, 0);
    T_EQ(frame.Points.y[FPP_MIN].used, 1);
}

TEST(menu_fdf, setpoint_center_ignored_when_edges_set) {
    FRAMEDEF frame;

    reset_ui_state();
    UI_InitFrame(&frame, FT_FRAME);

    /* Anchor x and y edges via TOPLEFT + BOTTOMRIGHT */
    UI_SetPoint(&frame, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    UI_SetPoint(&frame, FRAMEPOINT_BOTTOMRIGHT, NULL, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);

    /* Attempt to set CENTER — should be ignored on both axes */
    UI_SetPoint(&frame, FRAMEPOINT_CENTER, NULL, FRAMEPOINT_CENTER, 0.5f, 0.5f);

    T_EQ(frame.Points.x[FPP_MID].used, 0);
    T_EQ(frame.Points.y[FPP_MID].used, 0);
    /* Edges must still be intact */
    T_EQ(frame.Points.x[FPP_MIN].used, 1);
    T_EQ(frame.Points.x[FPP_MAX].used, 1);
    T_EQ(frame.Points.y[FPP_MIN].used, 1);
    T_EQ(frame.Points.y[FPP_MAX].used, 1);
}

/* --- SetAllPoints: detailed field verification --- */

TEST(menu_fdf, setallpoints_zero_offsets_and_target_positions) {
    FRAMEDEF frame;

    reset_ui_state();
    UI_InitFrame(&frame, FT_FRAME);
    UI_SetAllPoints(&frame);

    /* TOPLEFT anchor: x[MIN] left->left, y[MIN] top->top */
    T_FEQ(frame.Points.x[FPP_MIN].offset, 0.0f, 0.01f);
    T_EQ(frame.Points.x[FPP_MIN].targetPos, FPP_MIN);
    T_NULL(frame.Points.x[FPP_MIN].relativeTo);

    T_FEQ(frame.Points.y[FPP_MIN].offset, 0.0f, 0.01f);
    T_EQ(frame.Points.y[FPP_MIN].targetPos, FPP_MIN);
    T_NULL(frame.Points.y[FPP_MIN].relativeTo);

    /* BOTTOMRIGHT anchor: x[MAX] right->right, y[MAX] bottom->bottom */
    T_FEQ(frame.Points.x[FPP_MAX].offset, 0.0f, 0.01f);
    T_EQ(frame.Points.x[FPP_MAX].targetPos, FPP_MAX);
    T_NULL(frame.Points.x[FPP_MAX].relativeTo);

    T_FEQ(frame.Points.y[FPP_MAX].offset, 0.0f, 0.01f);
    T_EQ(frame.Points.y[FPP_MAX].targetPos, FPP_MAX);
    T_NULL(frame.Points.y[FPP_MAX].relativeTo);

    /* Center slot must remain unused */
    T_EQ(frame.Points.x[FPP_MID].used, 0);
    T_EQ(frame.Points.y[FPP_MID].used, 0);
}

TEST(menu_fdf, setallpoints_with_relative_frame_propagates_to_both_anchors) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setallpoints_rel.fdf",
              "Frame \"FRAME\" \"Anchor\" { Width 0.8, Height 0.6, }"
              "Frame \"FRAME\" \"Child\" { SetAllPoints, }");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    /* SetAllPoints via FDF uses NULL relativeTo (resolved at layout time) */
    T_EQ(child->Points.x[FPP_MIN].used, 1);
    T_EQ(child->Points.x[FPP_MAX].used, 1);
    T_EQ(child->Points.y[FPP_MIN].used, 1);
    T_EQ(child->Points.y[FPP_MAX].used, 1);
    T_FEQ(child->Points.x[FPP_MIN].offset, 0.0f, 0.01f);
    T_FEQ(child->Points.x[FPP_MAX].offset, 0.0f, 0.01f);
    T_FEQ(child->Points.y[FPP_MIN].offset, 0.0f, 0.01f);
    T_FEQ(child->Points.y[FPP_MAX].offset, 0.0f, 0.01f);
}

TEST(menu_fdf, text_uses_key_when_no_stringlist_entry_exists) {
    LPFRAMEDEF text;

    reset_ui_state();
    parse_fdf("text_key_passthrough.fdf",
              "Frame \"TEXT\" \"TextA\" {"
              " Text \"TRIGSTR_999\","
              "}");

    text = UI_FindFrame("TextA");
    if (!require_not_null(text)) return;
    T_STREQ(text->Text, "TRIGSTR_999");
}

TEST(menu_fdf, long_stringlist_text_uses_dynamic_storage) {
    LPFRAMEDEF text;

    reset_ui_state();
    parse_fdf("long_text.fdf",
              "StringList { LONG_TEXT "
              "\"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789\","
              "}"
              "Frame \"TEXT\" \"TextA\" {"
              " Text \"LONG_TEXT\","
              "}");

    text = UI_FindFrame("TextA");
    if (!require_not_null(text)) return;
    T_EQ(strlen(text->Text), 124);
    T_ASSERT(text->Text != text->TextStorage);
    T_NULL(text->Tip);
    T_NULL(text->Ubertip);
}

TEST(menu_fdf, duplicate_name_prefers_first_template) {
    LPFRAMEDEF found;

    reset_ui_state();
    parse_fdf("dup_name.fdf",
              "Frame \"FRAME\" \"Dup\" { Width 0.10, }"
              "Frame \"FRAME\" \"Dup\" { Width 0.20, }");

    found = UI_FindFrame("Dup");
    if (!require_not_null(found)) return;
    T_FEQ(found->Width, 0.10f, 0.01f);
}

TEST(menu_fdf, unknown_token_does_not_crash_existing_definitions) {
    LPFRAMEDEF good;

    reset_ui_state();
    parse_fdf("unknown_token.fdf",
              "Frame \"FRAME\" \"Good\" { Width 0.5, }"
              "Frame \"FRAME\" \"Bad\" { UnknownThing 1, }");

    good = UI_FindFrame("Good");
    if (!require_not_null(good)) return;
    T_FEQ(good->Width, 0.5f, 0.01f);
}

TEST(menu_fdf, single_line_text_auto_height_uses_fdf_font_size) {
    LPFRAMEDEF root;

    reset_ui_state();
    fake_text_size = MAKE(VECTOR2, 0.050f, 0.016f);
    parse_fdf("text-height.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Width 0.8, Height 0.6,"
              " Frame \"TEXT\" \"AutoLabel\" {"
              "  SetPoint TOPLEFT, \"Root\", TOPLEFT, 0.1, -0.1,"
              "  FrameFont \"MasterFont\", 0.013, \"\","
              "  Text \"COLON_RESOLUTION\","
              " }"
              " Frame \"TEXT\" \"WrappedInfo\" {"
              "  Width 0.2,"
              "  SetPoint TOPLEFT, \"AutoLabel\", BOTTOMLEFT, 0.0, -0.03,"
              "  FrameFont \"MasterFont\", 0.013, \"\","
              "  Text \"GAMEPORT_INFO\","
              " }"
              "}");

    root = UI_FindFrame("Root");
    if (!require_not_null(root)) return;

    UI_DrawFrame(root);

    T_EQ(captured_text_draws, 2);
    T_FEQ(captured_text_rects[0].h, 0.013f, 0.0001f);
    T_FEQ(captured_text_rects[1].h, 0.016f, 0.0001f);
}

TEST(menu_fdf, gameplay_ignores_stale_glue_hit_test_cache) {
    uiImport_t saved = uiimport;
    LPFRAMEDEF root;
    LPFRAMEDEF button;

    reset_ui_state();
    uiimport.FS_ReadFile = test_missing_fs_read;
    uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.Cvar_String = test_cvar_string;
    uiimport.Cmd_ExecuteText = test_cmd_execute_text;

    /* A startup map enters the initialized runtime with no standalone screen. */
    test_map = "Maps\\Campaign\\Human02.w3m";
    M_Init();

    parse_fdf("gameplay_input_ownership.fdf",
              "Frame \"FRAME\" \"InputOwnershipRoot\" {"
              " Width 0.8, Height 0.6,"
              " Frame \"GLUETEXTBUTTON\" \"StaleMenuButton\" {"
              "  Width 0.2, Height 0.1,"
              "  SetPoint TOPLEFT, \"InputOwnershipRoot\", TOPLEFT, 0.1, -0.1,"
              " }"
              "}");

    root = UI_FindFrame("InputOwnershipRoot");
    button = UI_FindFrame("StaleMenuButton");
    if (!require_not_null(root) || !require_not_null(button)) {
        M_Shutdown();
        test_map = "";
        uiimport = saved;
        return;
    }
    UI_SetOnClick(button, "test_stale_menu_click");

    /* Simulate the retained rectangle from the last glue draw. The cache is
     * still hit-testable internally, but gameplay must not dispatch through it. */
    UI_DrawFrame(root);
    T_ASSERT(UI_HitTest(0.15f, 0.15f) != NULL);
    captured_command[0] = '\0';
    T_ASSERT(!M_MouseEvent(UI_MOUSE_UP, 188, 144, 1));
    T_STREQ(captured_command, "");

    M_Shutdown();
    test_map = "";
    uiimport = saved;
}

TEST(menu_fdf, glue_checkbox_toggles_and_draws_check_highlight) {
    LPFRAMEDEF root;
    LPFRAMEDEF checkbox;

    reset_ui_state();
    parse_fdf("checkbox.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Width 0.8, Height 0.6,"
              " Frame \"GLUECHECKBOX\" \"OptionCheck\" {"
              "  Width 0.024, Height 0.024,"
              "  SetPoint TOPLEFT, \"Root\", TOPLEFT, 0.1, -0.1,"
              "  ControlBackdrop \"OptionBackdrop\","
              "  Frame \"BACKDROP\" \"OptionBackdrop\" {"
              "   BackdropBackground \"Textures\\\\Black32.blp\","
              "  }"
              "  CheckBoxCheckHighlight \"OptionCheckMark\","
              "  Frame \"HIGHLIGHT\" \"OptionCheckMark\" {"
              "   HighlightType \"FILETEXTURE\","
              "   HighlightAlphaFile \"Textures\\\\White32.blp\","
              "   HighlightAlphaMode \"BLEND\","
              "  }"
              " }"
              "}");

    root = UI_FindFrame("Root");
    checkbox = UI_FindFrame("OptionCheck");
    if (!require_not_null(root)) return;
    if (!require_not_null(checkbox)) return;

    captured_draw_calls = 0;
    UI_DrawFrame(root);
    T_ASSERT(!checkbox->CheckBox.Checked);
    T_EQ(captured_draw_calls, 1);

    test_mouse_pos.x = 130;
    test_mouse_pos.y = 130;
    M_MouseEvent(UI_MOUSE_UP, 130, 130, 1);
    captured_draw_calls = 0;
    UI_DrawFrame(root);
    T_ASSERT(checkbox->CheckBox.Checked);
    T_EQ(captured_draw_calls, 2);

    M_MouseEvent(UI_MOUSE_UP, 130, 130, 1);
    captured_draw_calls = 0;
    UI_DrawFrame(root);
    T_ASSERT(!checkbox->CheckBox.Checked);
    T_EQ(captured_draw_calls, 1);
}

TEST(menu_fdf, control_map_list_receives_mouse_clicks) {
    LPFRAMEDEF root;
    LPFRAMEDEF list;
    uiMapListState_t state = {0};

    reset_ui_state();
    uiimport.Cmd_ExecuteText = test_cmd_execute_text;
    parse_fdf("control_map_list.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Width 0.8, Height 0.6,"
              " Frame \"CONTROL\" \"MapListBox\" {"
              "  Width 0.3, Height 0.1,"
              "  SetPoint TOPLEFT, \"Root\", TOPLEFT, 0.1, -0.1,"
              " }"
              "}");

    root = UI_FindFrame("Root");
    list = UI_FindFrame("MapListBox");
    if (!require_not_null(root)) return;
    if (!require_not_null(list)) return;

    state.count = 2;
    snprintf(state.items[0].name, sizeof(state.items[0].name), "Campaign One");
    snprintf(state.items[1].name, sizeof(state.items[1].name), "Campaign Two");
    UI_BindMapList(list, &state, NULL, 4, "test_map_list_select %u");

    T_EQ(list->Type, FT_CONTROL);
    T_NOT_NULL(list->event_handler);

    /* Populate the layout cache, then click in the first 0.019-high row. */
    UI_DrawFrame(root);
    captured_command[0] = '\0';
    M_MouseEvent(UI_MOUSE_UP, 188, 144, 1);
    T_STREQ(captured_command, "test_map_list_select 0");
}

TEST(menu_fdf, popup_menu_hover_sets_flag_on_middle_row) {
    LPFRAMEDEF root;
    LPFRAMEDEF popup;

    reset_ui_state();
    parse_fdf("popup_hover.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Width 0.8, Height 0.6,"
              " Frame \"POPUPMENU\" \"GameMenu\" {"
              "  Width 0.18, Height 0.025,"
              /* FDF top-anchor Y offsets are positive upward, so -0.1 places the popup on-screen at y=0.1. */
              "  SetPoint TOPLEFT, \"Root\", TOPLEFT, 0.1, -0.1,"
              " }"
              "}");

    root = UI_FindFrame("Root");
    popup = UI_FindFrame("GameMenu");
    if (!require_not_null(root)) return;
    if (!require_not_null(popup)) return;

    /* Draw once to populate layout rects */
    UI_DrawFrame(root);

    /* The popup is at (0.1, 0.1) with size (0.18, 0.025) in FDF coords.
     * Window is 1000x750, scene is (0,0,0.8,0.6).
     * pixel = fdf * (1000/0.8, 750/0.6). */
    FLOAT mid_x = 0.1f + 0.18f * 0.5f;  /* 0.19 */
    FLOAT mid_y = 0.1f + 0.025f * 0.5f; /* 0.1125 */
    int px = (int)(mid_x / 0.8f * 1000.0f);
    int py = (int)(mid_y / 0.6f * 750.0f);

    M_MouseEvent(UI_MOUSE_MOVE, px, py, 0);

    T_ASSERT(popup->ui_flags & UIFLAG_HOVERED);
}

TEST(menu_fdf, button1_dropdown_backdrop_gets_hover_highlight) {
    LPFRAMEDEF root;

    reset_ui_state();
    parse_fdf("dropdown_hover.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Width 0.8, Height 0.6,"
              " Frame \"HIGHLIGHT\" \"StandardBorderedButtonMouseOverHighlightTemplate\" {"
              "  HighlightType \"FILETEXTURE\","
              "  HighlightAlphaFile \"Textures\\\\Hover.blp\","
              "  HighlightAlphaMode \"ADD\","
              " }"
              " Frame \"POPUPMENU\" \"NameMenu\" {"
              "  Width 0.178125, Height 0.025,"
              "  SetPoint TOPLEFT, \"Root\", TOPLEFT, 0.1, -0.1,"
              "  ControlBackdrop \"PlayerSlotPopupMenuBackdrop\","
              "  Frame \"BACKDROP\" \"PlayerSlotPopupMenuBackdrop\" {"
              "   BackdropBackground \"UI\\\\Widgets\\\\Glues\\\\GlueScreen-Button1-BackdropBackground.blp\","
              "   BackdropEdgeFile \"UI\\\\Widgets\\\\Glues\\\\GlueScreen-Button1-BorderedBackdropBorder.blp\","
              "  }"
              " }"
              "}");

    root = UI_FindFrame("Root");
    if (!require_not_null(root)) return;
    T_NULL(hover_texture);

    test_mouse_pos.x = 130;
    test_mouse_pos.y = 130;
    UI_DrawFrame(root);
    M_MouseEvent(UI_MOUSE_MOVE, 130, 130, 0);
    captured_hover_draws = 0;
    UI_DrawFrame(root);

    T_NOT_NULL(hover_texture);
    T_EQ(captured_hover_draws, 1);
}

TEST(menu_fdf, backdrop_edge_without_corner_size_logs_error) {
    LPFRAMEDEF root;

    reset_ui_state();
    parse_fdf("bad_backdrop.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Width 0.8, Height 0.6,"
              " Frame \"BACKDROP\" \"BrokenPanel\" {"
              "  Width 0.3, Height 0.2,"
              "  SetPoint CENTER, \"Root\", CENTER, 0, 0,"
              "  BackdropBackground \"Textures\\\\Black32.blp\","
              "  BackdropEdgeFile \"Textures\\\\White32.blp\","
              "  BackdropCornerFlags \"UL|UR|BL|BR|T|L|B|R\","
              " }"
              "}");

    root = UI_FindFrame("Root");
    if (!require_not_null(root)) return;
    UI_DrawFrame(root);
    T_ASSERT(strstr(captured_printf, "BackdropCornerSize is zero") != NULL);
}

TEST(menu_fdf, editbox_without_text_frame_click_focus_accepts_text_input) {
    LPFRAMEDEF root;
    LPFRAMEDEF editbox;

    reset_ui_state();
    parse_fdf("editbox_input.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Width 0.8, Height 0.6,"
              " Frame \"EDITBOX\" \"ChatEditBox\" {"
              "  Width 0.3, Height 0.04,"
              "  SetPoint TOPLEFT, \"Root\", TOPLEFT, 0.1, -0.1,"
              " }"
              "}");

    root = UI_FindFrame("Root");
    editbox = UI_FindFrame("ChatEditBox");
    if (!require_not_null(root)) return;
    if (!require_not_null(editbox)) return;

    test_mouse_pos.x = 130;
    test_mouse_pos.y = 130;
    UI_DrawFrame(root);
    M_MouseEvent(UI_MOUSE_DOWN, 130, 130, 1);
    captured_draw_calls = 0;
    UI_DrawFrame(root);

    T_ASSERT(UI_EditHasFocus(editbox));
    M_TextInput("hello");
    T_STREQ(UI_EditValue(editbox), "hello");
    T_ASSERT(!M_EditKey(13));
}

TEST(menu_fdf, options_game_port_enter_applies_and_blurs) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\OptionsMenu.fdf",
    };
    LPFRAMEDEF root;
    LPFRAMEDEF editbox;
    uiImport_t saved = uiimport;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));

    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.Printf = test_ui_printf;
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Cmd_ExecuteText = test_cmd_execute_text;
    uiimport.Cvar_String = test_cvar_string;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.GetPlayerState = test_get_player_state;
    captured_command[0] = '\0';

    T_ASSERT(optionsMenuScreen.load());
    optionsMenuScreen.init();

    root = UI_FindFrame("OptionsMenu");
    editbox = UI_FindFrame("GamePortEditBox");
    if (!require_not_null(root)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(editbox)) {
        uiimport = saved;
        return;
    }

    UI_SetEditValue(editbox, "27911");
    test_mouse_pos.x = 130;
    test_mouse_pos.y = 130;
    /* Event hit testing consumes the layout cache built by the preceding draw. */
    UI_DrawFrame(root);
    M_MouseEvent(UI_MOUSE_DOWN, 130, 130, 1);
    T_ASSERT(UI_EditHasFocus(editbox));

    optionsMenuScreen.key_event(13, true);

    T_STREQ(captured_command, "seta game_port 27911\n");
    T_ASSERT(!UI_EditHasFocus(editbox));

    OptionsMenu_Apply();
    T_STREQ(captured_command, "vid_apply\nwriteconfig\n");

    uiimport = saved;
}

TEST(menu_fdf, esc_menu_confirm_quit_panel_is_available) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\UI\\EscMenuMainPanel.fdf",
    };
    LPFRAMEDEF panel;
    LPFRAMEDEF quit_button;
    LPFRAMEDEF cancel_button;
    LPFRAMEDEF message;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));

    panel = UI_FindFrame("ConfirmQuitPanel");
    quit_button = UI_FindFrame("ConfirmQuitQuitButton");
    cancel_button = UI_FindFrame("ConfirmQuitCancelButton");
    message = UI_FindFrame("ConfirmQuitMessageText");

    if (!require_not_null(panel)) return;
    if (!require_not_null(quit_button)) return;
    if (!require_not_null(cancel_button)) return;
    if (!require_not_null(message)) return;

    T_STREQ(quit_button->Text, "ConfirmQuitQuitButtonText");
    T_STREQ(cancel_button->Text, "ConfirmQuitCancelButtonText");
    T_STREQ(message->Text, "Are you sure you want to exit?");
}

TEST(menu_fdf, dialog_war3_supports_configurable_button_modes) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\DialogWar3.fdf",
    };
    LPFRAMEDEF root;
    uiDialogWar3_t dialog;
    uiDialogWar3Init_t init = {
        .modal_name = "TestDialogModal",
        .template_name = "DialogWar3",
    };
    uiDialogWar3Config_t config = {
        .message = "CONFIRM_EXIT_MESSAGE",
        .icon = UI_DIALOG_WAR3_ICON_ERROR,
        .buttons = UI_DIALOG_WAR3_BUTTONS_OK,
        .ok_command = "menu_main",
    };

    load_ui_files(files, sizeof(files) / sizeof(files[0]));
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Printf = test_ui_printf;

    root = UI_Spawn(FT_FRAME, NULL);
    if (!require_not_null(root)) return;
    snprintf(root->Name, sizeof(root->Name), "%s", "TestDialogRoot");
    UI_SetSize(root, UI_BASE_WIDTH, UI_BASE_HEIGHT);

    T_ASSERT(UI_DialogWar3Init(&dialog, root, &init));
    T_ASSERT(!UI_DialogWar3Visible(&dialog));

    UI_DialogWar3Show(&dialog, &config);
    T_ASSERT(UI_DialogWar3Visible(&dialog));
    T_ASSERT(dialog.modal->Parent == NULL);
    T_STREQ(dialog.frames.DialogWar3->DialogBackdropName, "DialogBackdrop");
    T_NOT_NULL(UI_FindChildFrame(dialog.frames.DialogWar3, dialog.frames.DialogWar3->DialogBackdropName));
    T_STREQ(dialog.frames.DialogText->Text, "Are you sure you want to exit?");
    T_ASSERT(dialog.frames.DialogIcon->Backdrop.Background != 0);
    T_ASSERT(!dialog.frames.DialogButtonOKBackdrop->hidden);
    T_ASSERT(dialog.frames.DialogButtonNoBackdrop->hidden);
    T_ASSERT(dialog.frames.DialogButtonYesBackdrop->hidden);
    T_STREQ(dialog.frames.DialogButtonOK->OnClick, "menu_main");

    UI_DialogWar3Hide(&dialog);
    T_ASSERT(!UI_DialogWar3Visible(&dialog));
}

static LPCSTR const authored_dialog_files[] = {
    "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
    /* DialogWar3.fdf must be pre-loaded: UI_DialogWar3EnsureTemplate("BattleNetDialogTemplate")
     * calls UI_EnsureFDF for it; without a prior load the deferred UI_EnsureFDF call hits a null
     * FS_ReadFile (load_ui_files restores uiimport before UI_DialogWar3Init runs). */
    "UI\\FrameDef\\Glue\\DialogWar3.fdf",
    "UI\\FrameDef\\Glue\\BattleNetTemplates.fdf",
    "UI\\FrameDef\\UI\\ScriptDialog.fdf",
};

TEST(menu_fdf, dialog_supports_battlenet_template) {
    LPFRAMEDEF root;
    uiDialogWar3_t dialog;
    uiDialogWar3Init_t init = {
        .modal_name = "TestBattleNetDialogModal",
        .template_name = "BattleNetDialogTemplate",
    };
    uiDialogWar3Config_t config = {
        .message = "OpenWarcraft3\nA larger dialog template.",
        .buttons = UI_DIALOG_WAR3_BUTTONS_OK,
        .ok_command = "menu_main",
    };

    load_ui_files(authored_dialog_files, sizeof(authored_dialog_files) / sizeof(authored_dialog_files[0]));
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Printf = test_ui_printf;

    root = UI_Spawn(FT_FRAME, NULL);
    if (!require_not_null(root)) return;
    UI_SetSize(root, UI_BASE_WIDTH, UI_BASE_HEIGHT);

    T_ASSERT(UI_DialogWar3Init(&dialog, root, &init));
    T_ASSERT(dialog.frame->Width > 0.5f);
    T_ASSERT(dialog.frame->Height > 0.3f);
    T_ASSERT(dialog.icon == NULL);
    T_NOT_NULL(dialog.text);
    T_NOT_NULL(dialog.ok_button);
    T_NOT_NULL(dialog.frame->DialogBackdrop);
    T_ASSERT(dialog.ok_backdrop == dialog.ok_button);
    T_ASSERT(dialog.ok_button->Parent == dialog.frame);
    T_FEQ(dialog.text->Width, 0.48f, 0.001f);
    T_FEQ(dialog.text->Height, 0.25f, 0.001f);
    T_FEQ(dialog.ok_button->Width, 0.18f, 0.001f);
    T_FEQ(dialog.ok_button->Height, 0.031f, 0.001f);
    T_ASSERT(dialog.text->Points.x[FPP_MIN].relativeTo == dialog.frame);
    T_ASSERT(dialog.ok_button->Points.y[FPP_MAX].relativeTo == dialog.frame);

    UI_DialogWar3Show(&dialog, &config);
    T_ASSERT(UI_DialogWar3Visible(&dialog));
    T_STREQ(dialog.text->Text, "OpenWarcraft3\nA larger dialog template.");
    T_STREQ(dialog.ok_button->OnClick, "menu_main");
}

TEST(menu_fdf, dialog_supports_standard_authored_template) {
    LPFRAMEDEF root;
    uiDialogWar3_t dialog;
    uiDialogWar3Init_t init = {
        .modal_name = "TestStandardDialogModal",
        .template_name = "StandardDialogTemplate",
    };

    load_ui_files(authored_dialog_files, sizeof(authored_dialog_files) / sizeof(authored_dialog_files[0]));
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Printf = test_ui_printf;
    root = UI_Spawn(FT_FRAME, NULL);
    if (!require_not_null(root)) return;
    UI_SetSize(root, UI_BASE_WIDTH, UI_BASE_HEIGHT);

    T_ASSERT(UI_DialogWar3Init(&dialog, root, &init));
    T_FEQ(dialog.frame->Width, 0.60f, 0.001f);
    T_FEQ(dialog.text->Width, 0.48f, 0.001f);
    T_FEQ(dialog.text->Height, 0.25f, 0.001f);
    T_FEQ(dialog.ok_button->Width, 0.159f, 0.001f);
    T_ASSERT(dialog.text->Points.x[FPP_MIN].relativeTo == dialog.frame);
    T_ASSERT(dialog.ok_button->Points.y[FPP_MAX].relativeTo == dialog.frame);
}

TEST(menu_fdf, dialog_preserves_script_text_and_authors_button) {
    LPFRAMEDEF root;
    uiDialogWar3_t dialog;
    uiDialogWar3Init_t init = {
        .modal_name = "TestScriptDialogModal",
        .template_name = "ScriptDialog",
    };

    load_ui_files(authored_dialog_files, sizeof(authored_dialog_files) / sizeof(authored_dialog_files[0]));
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Printf = test_ui_printf;
    root = UI_Spawn(FT_FRAME, NULL);
    if (!require_not_null(root)) return;
    UI_SetSize(root, UI_BASE_WIDTH, UI_BASE_HEIGHT);

    T_ASSERT(UI_DialogWar3Init(&dialog, root, &init));
    T_FEQ(dialog.frame->Width, 0.288f, 0.001f);
    T_FEQ(dialog.frame->Height, 0.112f, 0.001f);
    T_STREQ(dialog.text->Name, "ScriptDialogText");
    T_FEQ(dialog.ok_button->Width, 0.159f, 0.001f);
    T_FEQ(dialog.ok_button->Height, 0.031f, 0.001f);
    T_ASSERT(dialog.ok_button->Points.y[FPP_MAX].relativeTo == dialog.frame);
}

TEST(menu_fdf, main_menu_quit_dialog_commands_quit) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\UI\\EscMenuTemplates.fdf",
        "UI\\FrameDef\\UI\\EscMenuMainPanel.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\MainMenu.fdf",
        "UI\\FrameDef\\Glue\\DialogWar3.fdf",
    };
    LPFRAMEDEF global_exit_button;
    LPFRAMEDEF exit_button;
    LPFRAMEDEF logo;
    LPFRAMEDEF modal;
    LPFRAMEDEF dialog;
    LPFRAMEDEF message;
    LPFRAMEDEF icon;
    LPFRAMEDEF ok_backdrop;
    LPFRAMEDEF no_backdrop;
    LPFRAMEDEF yes_backdrop;
    LPFRAMEDEF no_button;
    LPFRAMEDEF yes_button;
    uiImport_t saved = uiimport;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));

    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.Printf = test_ui_printf;
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Cmd_ExecuteText = test_cmd_execute_text;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.GetPlayerState = test_get_player_state;
    captured_command[0] = '\0';

    T_ASSERT(mainMenuScreen.load());
    mainMenuScreen.init();

    global_exit_button = UI_FindFrame("ExitButton");
    exit_button = UI_FindChildFrame(UI_FindFrame("MainMenuFrame"), "ExitButton");
    if (!require_not_null(exit_button)) {
        uiimport = saved;
        return;
    }
    T_ASSERT(global_exit_button != exit_button);
    T_ASSERT(!exit_button->hidden);
    T_STREQ(exit_button->OnClick, "menu_quit");
    logo = UI_FindChildFrame(UI_FindFrame("MainMenuFrame"), "WarCraftIIILogo");
    if (!require_not_null(logo)) { uiimport = saved; return; }
    T_FEQ(logo->Points.x[FPP_MIN].offset, 0.13f, 0.001f);
    T_FEQ(logo->Points.y[FPP_MIN].offset, -0.08f, 0.001f);

    modal = UI_FindFrame("MainMenuQuitModal");
    if (!require_not_null(modal)) {
        uiimport = saved;
        return;
    }
    T_EQ(modal->Type, FT_DIALOG);
    T_ASSERT(modal->Parent == NULL);
    T_ASSERT(modal->hidden);
    T_ASSERT(UI_FindChildFrame(modal, "MainMenuQuitModalCover") == NULL);

    dialog = UI_FindChildFrame(modal, "DialogWar3");
    if (!require_not_null(dialog)) {
        uiimport = saved;
        return;
    }
    T_EQ(dialog->Type, FT_DIALOG);
    T_ASSERT(dialog->hidden);

    MainMenu_ShowQuitConfirm();
    T_ASSERT(!modal->hidden);
    T_ASSERT(!dialog->hidden);

    message = UI_FindChildFrame(dialog, "DialogText");
    icon = UI_FindChildFrame(dialog, "DialogIcon");
    ok_backdrop = UI_FindChildFrame(dialog, "DialogButtonOKBackdrop");
    no_backdrop = UI_FindChildFrame(dialog, "DialogButtonNoBackdrop");
    yes_backdrop = UI_FindChildFrame(dialog, "DialogButtonYesBackdrop");
    no_button = UI_FindChildFrame(dialog, "DialogButtonNo");
    yes_button = UI_FindChildFrame(dialog, "DialogButtonYes");

    if (!require_not_null(message)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(icon)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(ok_backdrop)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(no_backdrop)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(yes_backdrop)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(no_button)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(yes_button)) {
        uiimport = saved;
        return;
    }

    T_STREQ(message->Text, "Do you want to Quit?");
    T_ASSERT(icon->Backdrop.Background != 0);
    T_ASSERT(ok_backdrop->hidden);
    T_ASSERT(!no_backdrop->hidden);
    T_ASSERT(!yes_backdrop->hidden);
    T_STREQ(no_button->OnClick, "menu_main");
    T_STREQ(yes_button->OnClick, "quit");

    UI_MenuCommandLocal(no_button->OnClick);
    T_ASSERT(modal->hidden);
    T_ASSERT(dialog->hidden);

    UI_MenuCommandLocal(yes_button->OnClick);
    T_STREQ(captured_command, "quit");

    uiimport = saved;
}

TEST(menu_fdf, main_menu_realm_select_uses_realm_panel_anim) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\UI\\EscMenuTemplates.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\DialogWar3.fdf",
        "UI\\FrameDef\\Glue\\MainMenu.fdf",
    };
    uiImport_t saved = uiimport;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));
    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.Printf = test_ui_printf;
    uiimport.GetRenderer = test_get_renderer;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.GetPlayerState = test_get_player_state;

    T_ASSERT(mainMenuScreen.load());
    mainMenuScreen.init();
    MainMenu_ShowRealmSelect();
    captured_stand_sprites = 0;
    captured_realm_panel_sprites = 0;
    mainMenuScreen.draw();
    T_EQ(captured_stand_sprites, 0);
    T_EQ(captured_realm_panel_sprites, 2);
    uiimport = saved;
}

static void test_single_player_campaign_profile(BOOL tft) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\SinglePlayerMenu.fdf",
        "UI\\FrameDef\\Glue\\CampaignMenu.fdf",
        "UI\\FrameDef\\Glue\\MapListBox.fdf",
    };
    uiImport_t saved = uiimport;
    LPFRAMEDEF root;
    LPFRAMEDEF campaign_button;
    LPFRAMEDEF skirmish_button;
    LPFRAMEDEF cancel_button;
    LPFRAMEDEF back_button;
    LPFRAMEDEF campaign_select_frame;
    LPFRAMEDEF mission_select_frame;
    LPFRAMEDEF mission_name;
    LPFRAMEDEF mission_name_header;
    LPFRAMEDEF mission_list_box;
    LPFRAMEDEF difficulty_title;
    LPFRAMEDEF campaign_list_box;
    LPFRAMEDEF human_button;

    hide_expansion_campaign_file = false;
    test_fs_expansion = tft;
    test_campaign_mission_visibility = NULL;
    test_campaign_played_mission = -1;
    load_ui_files(files, sizeof(files) / sizeof(files[0]));

    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.Printf = test_ui_printf;
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Cmd_ExecuteText = test_cmd_execute_text;
    uiimport.Cvar_String = test_cvar_string;
    uiimport.Cvar_Set = test_cvar_set;
    uiimport.FS_ReadFile = test_fs_read_file;
    uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.GetPlayerState = test_get_player_state;

    if (!singlePlayerMenuScreen.load()) {
        T_ASSERT(false);
        uiimport = saved;
        return;
    }
    singlePlayerMenuScreen.init();

    root = UI_FindFrame("SinglePlayerMenu");
    campaign_button = UI_FindFrame("CampaignButton");
    skirmish_button = UI_FindFrame("SkirmishButton");
    cancel_button = UI_FindFrame("CancelButton");
    back_button = UI_FindFrame("BackButton");

    if (!require_not_null(root)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(campaign_button)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(skirmish_button)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(cancel_button)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(back_button)) { uiimport = saved; return; }
    T_ASSERT(!root->hidden);
    T_STREQ(campaign_button->OnClick, "menu_single_player_campaign");
    T_STREQ(skirmish_button->OnClick, "menu_single_player_skirmish");
    T_STREQ(cancel_button->OnClick, "menu_main");
    T_STREQ(back_button->OnClick, "menu_single_player_campaign_back");

    SinglePlayerMenu_ShowCampaign();
    campaign_select_frame = UI_FindFrame("CampaignSelectFrame");
    human_button = UI_FindFrame("HumanButton");
    campaign_list_box = campaign_select_frame
        ? UI_FindChildFrame(campaign_select_frame, "MapListBox")
        : NULL;

    if (!require_not_null(human_button) || !require_not_null(campaign_list_box)) {
        uiimport = saved;
        return;
    }
    T_ASSERT(human_button->hidden);
    T_ASSERT(!campaign_list_box->hidden);
    T_FEQ(campaign_list_box->Width, 0.34f, 0.001f);
    T_FEQ(campaign_list_box->Height, 0.11f, 0.001f);
    T_FEQ(campaign_list_box->Points.x[FPP_MIN].offset, -0.14f, 0.001f);
    T_FEQ(campaign_list_box->Points.y[FPP_MAX].offset, 0.04f, 0.001f);
    T_ASSERT(campaign_list_box->Points.x[FPP_MIN].relativeTo == back_button);
    T_ASSERT(campaign_list_box->Points.y[FPP_MAX].relativeTo == back_button);
    T_ASSERT(campaign_list_box->MapListControl.State != NULL);
    T_NOT_NULL(campaign_list_box->event_handler);
    T_EQ((int)campaign_list_box->MapListControl.State->count, 4);
    T_STREQ(campaign_list_box->MapListControl.State->items[0].name,
                  tft
                      ? "Sentinels Campaign: Terror of the Tides"
                      : "Human Campaign: The Scourge of Lordaeron");
    T_STREQ(campaign_list_box->MapListControl.State->items[1].path,
                  tft ? "Human" : "Undead");
    T_STREQ(campaign_list_box->MapListControl.SelectCommand,
                  "menu_single_player_campaign_select %u");

    captured_command[0] = '\0';
    SinglePlayerMenu_LaunchCampaignIndex(tft ? 1 : 0);
    T_STREQ(captured_command, "");

    mission_select_frame = UI_FindFrame("MissionSelectFrame");
    mission_name = UI_FindFrame("MissionName");
    mission_name_header = UI_FindFrame("MissionNameHeader");
    mission_list_box = mission_select_frame
        ? UI_FindChildFrame(mission_select_frame, "MapListBox")
        : NULL;
    if (!require_not_null(mission_select_frame) ||
        !require_not_null(mission_name) ||
        !require_not_null(mission_name_header) ||
        !require_not_null(mission_list_box)) {
        uiimport = saved;
        return;
    }
    T_ASSERT(campaign_select_frame->hidden);
    T_ASSERT(!mission_select_frame->hidden);
    T_ASSERT(!mission_list_box->hidden);
    T_STREQ(mission_name->Text,
            tft ? "Curse of the Blood Elves" : "The Scourge of Lordaeron");
    T_STREQ(mission_name_header->Text,
            tft ? "Alliance Campaign" : "Human Campaign");
    T_EQ((int)mission_list_box->MapListControl.State->count, 3);
    T_STREQ(mission_list_box->MapListControl.State->items[0].name,
            tft ? "Chapter One: Misconceptions" : "The Defense of Strahnbrad");
    T_STREQ(mission_list_box->MapListControl.State->items[1].name,
            tft ? "Chapter Two: A Dark Covenant" : "Blackrock & Roll");
    T_STREQ(mission_list_box->MapListControl.SelectCommand,
            "menu_single_player_mission_select %u");
    T_NOT_NULL(mission_list_box->event_handler);

    UI_MenuCommandLocal(back_button->OnClick);
    T_ASSERT(!campaign_select_frame->hidden);
    T_ASSERT(mission_select_frame->hidden);

    captured_cvar_name[0] = '\0';
    captured_cvar_value[0] = '\0';
    SinglePlayerMenu_SetDifficulty(2);
    T_STREQ(captured_cvar_name, "wc3_campaign_difficulty");
    T_STREQ(captured_cvar_value, "2");
    difficulty_title = UI_FindFrame("CampaignPopupMenuTitleTextTemplate");
    if (difficulty_title) {
        T_STREQ(difficulty_title->Text, UI_GetString("HARD"));
    }

    test_campaign_mission_visibility = "played";
    test_campaign_played_mission = 1;
    SinglePlayerMenu_LaunchCampaignIndex(tft ? 1 : 0);
    T_EQ((int)mission_list_box->MapListControl.State->count, 1);
    T_EQ((int)mission_list_box->MapListControl.State->items[0].flags, 1);
    T_STREQ(mission_list_box->MapListControl.State->items[0].name,
            tft ? "Chapter Two: A Dark Covenant" : "Blackrock & Roll");

    captured_command[0] = '\0';
    captured_cvar_name[0] = '\0';
    captured_cvar_value[0] = '\0';
    SinglePlayerMenu_LaunchMissionIndex(0);
    T_STREQ(captured_cvar_name, "wc3_campaign_played_human_1");
    T_STREQ(captured_cvar_value, "1");
    T_STREQ(captured_command,
                  tft
                      ? "map \"Maps\\FrozenThrone\\Campaign\\HumanX02.w3x\""
                      : "map \"Maps\\Campaign\\Human02.w3m\"");

    hide_expansion_campaign_file = false;
    test_fs_expansion = false;
    test_campaign_mission_visibility = NULL;
    test_campaign_played_mission = -1;
    uiimport = saved;
}

TEST(menu_fdf, single_player_screen_loads_roc_campaigns) {
    test_single_player_campaign_profile(false);
}

TEST(menu_fdf, single_player_screen_loads_tft_campaigns) {
    test_single_player_campaign_profile(true);
}

static const char *utf16le_src_ascii;
static int utf16le_fs_read(LPCSTR file_name, void **buf) {
    const char *src = utf16le_src_ascii;
    size_t len = strlen(src);
    /* Encode as UTF-16 LE: BOM (FF FE) then each ASCII char as two bytes. */
    size_t out_size = 2 + len * 2;
    unsigned char *data = malloc(out_size + 2);
    if (!data) return -1;
    data[0] = 0xFF; data[1] = 0xFE;
    for (size_t i = 0; i < len; i++) {
        data[2 + i * 2]     = (unsigned char)src[i];
        data[2 + i * 2 + 1] = 0x00;
    }
    data[out_size]     = 0;
    data[out_size + 1] = 0;
    *buf = data;
    (void)file_name;
    return (int)out_size;
}

static void utf16le_fs_free(void *buf) { free(buf); }

TEST(menu_fdf, utf16le_fdf_is_parsed_correctly) {
    uiImport_t saved = uiimport;
    LPFRAMEDEF frame;

    reset_ui_state();
    UI_ClearTemplates();

    utf16le_src_ascii =
        "/* UTF-16 LE block comment at file start */\n"
        "// UTF-16 LE line comment\n"
        "Frame \"BACKDROP\" \"UTF16Frame\" {\n"
        "    Width 0.75,\n"
        "    Height 0.50,\n"
        "}\n";

    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = utf16le_fs_read;
    uiimport.FS_FreeFile = utf16le_fs_free;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.ImageIndex = fake_image_index;
    uiimport.FontIndex = test_font_index;
    uiimport.Printf = test_ui_printf;

    UI_ParseFDF("utf16le_test.fdf");
    uiimport = saved;

    frame = UI_FindFrame("UTF16Frame");
    if (!require_not_null(frame)) return;
    T_EQ(frame->Type, FT_BACKDROP);
    T_FEQ(frame->Width, 0.75f, 0.01f);
    T_FEQ(frame->Height, 0.50f, 0.01f);
}

/* Parsing a template must not fetch art that an instantiated frame overrides. */
TEST(menu_fdf, unused_template_texture_stays_unloaded) {
    reset_ui_state();
    parse_fdf("template_art.fdf",
              "Frame \"BACKDROP\" \"Template\" { BackdropBackground \"Unused.blp\", }"
              "Frame \"BACKDROP\" \"Panel\" INHERITS \"Template\" { BackdropBackground \"Used.blp\", }");
    LPFRAMEDEF panel = UI_FindFrame("Panel");
    if (!require_not_null(panel)) return;
    T_EQ(fake_texture_id, 0);
    T_NOT_NULL(UI_GetTexture(panel->Backdrop.Background));
    T_STREQ(captured_image_path, "Used.blp");
    T_EQ(fake_texture_id, 1);
    T_NOT_NULL(UI_GetTexture(UI_FindFrame("Template")->Backdrop.Background));
    T_STREQ(captured_image_path, "Unused.blp"); /* A real consumer still reaches the renderer's diagnostic path. */
    T_EQ(fake_texture_id, 2);
    T_NULL(UI_GetTexture(0)); T_NULL(UI_GetTexture(1024)); T_NULL(UI_GetTexture(50));
}

static int test_theme_read(LPCSTR name, void **buf) {
    LPCSTR text = "[Default]\nBackground=Default.blp\n[Human]\nBackground=Human.blp\n";
    (void)name; *buf = strdup(text); return (int)strlen(text);
}

static int test_versioned_theme_read(LPCSTR name, void **buf) {
    LPCSTR text =
        "[Default]\n"
        "GlueSpriteLayerBackground_V0=RocBackground.mdl\n"
        "GlueSpriteLayerBackground_V1=TftBackground.mdl\n"
        "GlueSpriteLayerTopLeft_V0=RocTopLeft.mdl\n"
        "GlueSpriteLayerTopLeft_V1=TftTopLeft.mdl\n"
        "MainMenuLogo=SharedLogo.mdl\n"
        "MainMenuLogo_V0=RocLogo.mdl\n"
        "MainMenuLogo_V1=TftLogo.mdl\n"
        "OnlyTft_V1=TftOnly.mdl\n";
    (void)name; *buf = strdup(text); return (int)strlen(text);
}

TEST(menu_fdf, versioned_theme_keys_follow_expansion_edition) {
    uiImport_t saved = uiimport;

    reset_ui_state();
    uiimport.FS_ReadFile = test_versioned_theme_read;
    uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.Cvar_String = test_cvar_string;
    UI_LoadTheme("UI\\war3skins.txt");

    test_fs_expansion = false;
    T_STREQ(Theme_String("GlueSpriteLayerBackground", "Default"), "RocBackground.mdl");
    T_STREQ(Theme_String("GlueSpriteLayerTopLeft", "Default"), "RocTopLeft.mdl");

    test_fs_expansion = true;
    T_STREQ(Theme_String("GlueSpriteLayerBackground", "Default"), "TftBackground.mdl");
    T_STREQ(Theme_String("GlueSpriteLayerTopLeft", "Default"), "TftTopLeft.mdl");

    test_fs_expansion = false;
    UI_ClearTheme();
    uiimport = saved;
}

TEST(menu_fdf, unversioned_theme_key_precedes_edition_variant) {
    uiImport_t saved = uiimport;

    reset_ui_state();
    uiimport.FS_ReadFile = test_versioned_theme_read;
    uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.Cvar_String = test_cvar_string;
    UI_LoadTheme("UI\\war3skins.txt");

    test_fs_expansion = false;
    T_STREQ(Theme_String("MainMenuLogo", "Default"), "SharedLogo.mdl");
    test_fs_expansion = true;
    T_STREQ(Theme_String("MainMenuLogo", "Default"), "SharedLogo.mdl");

    test_fs_expansion = false;
    T_STREQ(Theme_String("OnlyTft", "Default"), "OnlyTft");

    UI_ClearTheme();
    uiimport = saved;
}

TEST(menu_fdf, deferred_texture_cache_tracks_theme_changes) {
    uiImport_t saved = uiimport;
    PLAYER player = { .race = kPlayerRaceHuman };
    reset_ui_state();
    uiimport.FS_ReadFile = test_theme_read; uiimport.FS_FreeFile = test_fs_free_file;
    UI_LoadTheme("UI\\war3skins.txt");
    DWORD index = UI_LoadTexture("Background", true);
    T_EQ(UI_LoadTexture("Background", true), index);
    T_EQ(fake_texture_id, 0);
    test_player = &player;
    T_NOT_NULL(UI_GetTexture(index));
    T_STREQ(captured_image_path, "Human.blp");
    T_EQ(texture_releases, 0); T_EQ(fake_texture_id, 1);
    T_NOT_NULL(UI_GetTexture(index)); T_EQ(fake_texture_id, 1);
    test_player = NULL;
    T_NOT_NULL(UI_GetTexture(index));
    T_STREQ(captured_image_path, "Default.blp");
    T_EQ(texture_releases, 1); T_EQ(fake_texture_id, 2);
    T_NOT_NULL(UI_GetTexture(index)); T_EQ(fake_texture_id, 2);
    UI_ClearTheme(); uiimport = saved;
}

/* A menu launch starts without a startup map cvar; subsequent destinations must invalidate the metadata cache. */
TEST(menu_fdf, loading_screen_uses_current_destination_and_caches_per_map) {
    uiImport_t saved = uiimport;
    PLAYER player = { .client_ui_state = CLIENT_UI_LOADING };
    reset_ui_state();
    uiimport.FS_ReadFile = test_fs_read_file; uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.Cvar_String = test_cvar_string; uiimport.Cmd_ExecuteText = test_cmd_execute_text;
    M_Init();
    test_player = &player;
    M_Refresh(0);
    T_EQ(map_reads, 0);
    test_map = "Maps\\Campaign\\Human02.w3m";
    M_Refresh(1);
    LPFRAMEDEF title = UI_FindFrame("LoadingTitleText");
    if (require_not_null(title)) {
        T_STREQ(title->Text, "Human02");
        T_ASSERT(UI_FindFrame("LoadingBackground")->Portrait.model != 0);
        T_EQ(map_reads, 1);
        M_Refresh(2); T_EQ(map_reads, 1);
        test_map = "Maps\\Campaign\\Orc01.w3m";
        M_Refresh(3); T_STREQ(title->Text, "Orc01"); T_EQ(map_reads, 2);
        M_Refresh(4); T_EQ(map_reads, 2);
        M_Init();
        M_Refresh(5); T_EQ(map_reads, 3);
    }
    test_player = NULL; test_map = "";
    M_Shutdown(); uiimport = saved;
}

TEST(menu_fdf, loading_rows_support_roc_and_tft_schema) {
    static const struct { LPCSTR row, model; DWORD seq; BOOL valid; } cases[] = {
        { "WESTRING_LOADINGSCREEN_HUMAN01,0,UI\\Glues\\Loading\\Backgrounds\\Campaigns\\LordaeronBackground.mdl",
          "UI\\Glues\\Loading\\Backgrounds\\Campaigns\\LordaeronBackground.mdl", 0, true },
        { "1,WESTRING_LOADINGSCREEN_HUMANX01,6,UI\\Glues\\Loading\\Backgrounds\\Campaigns\\LordaeronExpansionBackground.mdl",
          "UI\\Glues\\Loading\\Backgrounds\\Campaigns\\LordaeronExpansionBackground.mdl", 6, true },
        { "0,WESTRING_LOADINGSCREEN_HUMAN02,1,UI\\Glues\\Loading\\Backgrounds\\Campaigns\\LordaeronBackground.mdl",
          "UI\\Glues\\Loading\\Backgrounds\\Campaigns\\LordaeronBackground.mdl", 1, true },
        { NULL, "", 0, false }, { "", "", 0, false },
        { "WESTRING_LOADINGSCREEN_HUMAN01", "", 0, false },
        { "1,WESTRING_LOADINGSCREEN_HUMANX01,6,", "", 6, false },
    };
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        PATHSTR model; DWORD seq;
        T_EQ(UI_ParseLoadingRow(cases[i].row, &seq, model), cases[i].valid);
        if (cases[i].valid) { T_STREQ(model, cases[i].model); T_EQ(seq, cases[i].seq); }
    }
}

TEST(menu_fdf, exported_image_resolver_uses_local_player_skin) {
    uiImport_t saved = uiimport;
    PLAYER player = { .race = kPlayerRaceHuman };

    reset_ui_state();
    uiimport.FS_ReadFile = test_theme_read; uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.GetPlayerState = test_get_player_state;
    test_player = &player;
    UI_LoadTheme("UI\\war3skins.txt");
    T_STREQ(UI_ResolveImagePathLocal("Background"), "Human.blp");
    T_STREQ(UI_ResolveImagePathLocal("UI\\Textures\\fixed.blp"), "UI\\Textures\\fixed.blp");
    test_player = NULL;
    UI_ClearTheme(); uiimport = saved;
}
