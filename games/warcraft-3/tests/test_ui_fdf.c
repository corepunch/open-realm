#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "test_framework.h"
#include "../ui/ui_local.h"
#include "../ui/ui_dialog.h"
#include "../ui/ui_screen.h"
#include "../../../common/mpq.h"

static void setup_game(void) {}
static void teardown_game(void) {}

static const char *captured_image_path;
static const char *captured_model_path;
static char captured_command[128];
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
static VECTOR2 test_mouse_pos;

static int fake_image_index(LPCSTR name) {
    captured_image_path = name;
    return (name && *name) ? 123 : 0;
}

static int fake_model_index(LPCSTR name) {
    captured_model_path = name;
    return (name && *name) ? 456 : 0;
}

static void parse_fdf(const char *name, const char *src) {
    char *buf = strdup(src);
    ASSERT_NOT_NULL(buf);
    if (!buf) {
        return;
    }
    UI_ParseFDF_Buffer(name, buf);
    free(buf);
}

static int require_not_null(const void *ptr) {
    ASSERT_NOT_NULL(ptr);
    return ptr != NULL;
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

static void test_fs_free_file(void *buf) {
    free(buf);
}

static int test_image_index(LPCSTR name) {
    return (name && *name) ? 123 : 0;
}

static int test_model_index(LPCSTR name) {
    return (name && *name) ? 456 : 0;
}

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

static VECTOR2 test_ui_get_mouse_fdf(void) {
    return test_mouse_pos;
}

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

static size2_t test_get_window_size(void) {
    return MAKE(size2_t, 1000, 750);
}

static LPRENDERER test_get_renderer(void) {
    static refExport_t renderer = {
        .LoadTexture = test_load_texture,
        .LoadModel = test_load_model,
        .LoadFont = test_load_font,
        .GetWindowSize = test_get_window_size,
        .DrawImageEx = test_draw_image_ex,
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
    if (name && !strcmp(name, "fs_expansion")) {
        return test_fs_expansion ? "1" : "0";
    }
    if (name && !strcmp(name, "game_port")) {
        return "27910";
    }
    return fallback;
}

static void load_ui_file(LPCSTR file_name) {
    uiImport_t saved = uiimport;

    UI_ClearTemplates();
    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = test_fs_read_file;
    uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.ImageIndex = test_image_index;
    uiimport.ModelIndex = test_model_index;
    uiimport.FontIndex = test_font_index;
    uiimport.Printf = test_ui_printf;
    uiimport.Error = test_ui_printf;
    UI_ParseFDF(file_name);
    uiimport = saved;
}

static void load_ui_files(LPCSTR const *file_names, size_t count) {
    uiImport_t saved = uiimport;

    UI_ClearTemplates();
    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = test_fs_read_file;
    uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.ImageIndex = test_image_index;
    uiimport.ModelIndex = test_model_index;
    uiimport.FontIndex = test_font_index;
    uiimport.Printf = test_ui_printf;
    uiimport.Error = test_ui_printf;
    for (size_t i = 0; i < count; i++) {
        UI_ParseFDF(file_names[i]);
    }
    uiimport = saved;
}

static void reset_ui_state(void) {
    UI_ClearTemplates();
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
    hover_texture = NULL;
    captured_hover_draws = 0;
    memset(captured_text_rects, 0, sizeof(captured_text_rects));
    fake_text_size = MAKE(VECTOR2, 0.050f, 0.016f);
    test_mouse_pos = MAKE(VECTOR2, 0, 0);
    UI_ClearEditFocus();
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;
    uiimport.ImageIndex = fake_image_index;
    uiimport.ModelIndex = fake_model_index;
    uiimport.FontIndex = test_font_index;
    uiimport.GetRenderer = test_get_renderer;
    uiimport.GetMouseFdf = test_ui_get_mouse_fdf;
    uiimport.Printf = test_ui_printf;
    uiimport.Error = test_ui_printf;
}

static void test_parse_single_frame_definition(void) {
    LPFRAMEDEF root;

    reset_ui_state();
    parse_fdf("single.fdf",
              "Frame \"FRAME\" \"Root\" { Width 0.5, Height 0.25, }");

    root = UI_FindFrame("Root");
    if (!require_not_null(root)) return;
    ASSERT_EQ_INT(root->Type, FT_FRAME);
    ASSERT_FLOAT_EQ(root->Width, 0.5f);
    ASSERT_FLOAT_EQ(root->Height, 0.25f);
    ASSERT_NULL(root->Parent);
}

static void test_parse_nested_parent_child_relationship(void) {
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
    ASSERT_EQ_INT(child->Type, FT_TEXT);
    ASSERT(child->Parent == root);
    ASSERT_STR_EQ(child->Text, "Hello");
}

static void test_inherits_copies_compatible_type_fields(void) {
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
    ASSERT_FLOAT_EQ(derived->Width, base->Width);
    ASSERT_FLOAT_EQ(derived->Height, base->Height);
    ASSERT_EQ_INT(derived->Type, FT_FRAME);
}

static void test_inherits_rejects_incompatible_type(void) {
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
    ASSERT_FLOAT_EQ(base_text->Width, 0.77f);
    ASSERT_FLOAT_EQ(derived_frame->Height, 0.25f);
    ASSERT_FLOAT_EQ(derived_frame->Width, 0.0f);
}

static void test_setpoint_top_left_sets_top_y_anchor(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MIN].offset, 0.01f);
    ASSERT(child->Points.x[FPP_MIN].relativeTo == root);

    ASSERT_EQ_INT(child->Points.y[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MIN].offset, -0.02f);
    ASSERT(child->Points.y[FPP_MIN].relativeTo == root);
}

static void test_setallpoints_sets_min_and_max(void) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setall.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"FRAME\" \"Child\" { SetAllPoints, }"
              "}");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    ASSERT_EQ_INT(child->Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MAX].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MAX].used, 1);
}

static void test_anchor_translates_to_setpoint_state(void) {
    LPFRAMEDEF frame;

    reset_ui_state();
    parse_fdf("anchor.fdf",
              "Frame \"FRAME\" \"Root\" { Anchor BOTTOMRIGHT, -0.03, 0.04, }");

    frame = UI_FindFrame("Root");
    if (!require_not_null(frame)) return;
    ASSERT_EQ_INT(frame->Points.x[FPP_MAX].used, 1);
    ASSERT_EQ_INT(frame->Points.y[FPP_MAX].used, 1);
    ASSERT_FLOAT_EQ(frame->Points.x[FPP_MAX].offset, -0.03f);
    ASSERT_FLOAT_EQ(frame->Points.y[FPP_MAX].offset, 0.04f);
}

static void test_backdrop_flags_and_insets_are_parsed(void) {
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
    ASSERT_EQ_INT(frame->Type, FT_BACKDROP);
    ASSERT_EQ_INT(frame->Backdrop.TileBackground, 1);
    ASSERT_EQ_INT(frame->Backdrop.BlendAll, 1);
    ASSERT_FLOAT_EQ(frame->Backdrop.BackgroundInsets[0], 0.1f);
    ASSERT_FLOAT_EQ(frame->Backdrop.BackgroundInsets[1], 0.2f);
    ASSERT_FLOAT_EQ(frame->Backdrop.BackgroundInsets[2], 0.3f);
    ASSERT_FLOAT_EQ(frame->Backdrop.BackgroundInsets[3], 0.4f);
}

static void test_vector_parser_accepts_f_suffixes(void) {
    LPFRAMEDEF frame;

    reset_ui_state();
    parse_fdf("vector_f_suffix.fdf",
              "Frame \"TEXTBUTTON\" \"Button\" {"
              " ButtonPushedTextOffset -0.002f -0.003f,"
              "}");

    frame = UI_FindFrame("Button");
    if (!require_not_null(frame)) return;
    ASSERT_FLOAT_EQ(frame->Button.PushedTextOffset.x, -0.002f);
    ASSERT_FLOAT_EQ(frame->Button.PushedTextOffset.y, -0.003f);
}

static void test_chat_display_tokens_map_to_text_area(void) {
    reset_ui_state();
    parse_fdf("chat_display.fdf", "Frame \"CHATDISPLAY\" \"Chat\" { ChatDisplayLineHeight 0.012, ChatDisplayBorderSize 0.034, }");
    LPFRAMEDEF frame = UI_FindFrame("Chat");
    if (!require_not_null(frame)) return;
    ASSERT_FLOAT_EQ(frame->TextArea.LineHeight, 0.012f);
    ASSERT_FLOAT_EQ(frame->TextArea.Inset, 0.034f);
}

static void test_comments_are_ignored_inside_frame_bodies(void) {
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
    ASSERT_FLOAT_EQ(frame->Points.x[FPP_MIN].offset, 0.25f);
    ASSERT_FLOAT_EQ(frame->Points.y[FPP_MIN].offset, -0.125f);
    ASSERT_FLOAT_EQ(frame->Width, 0.5f);
    ASSERT_FLOAT_EQ(frame->Height, 0.25f);
}

static void test_comments_are_ignored_between_setpoint_arguments(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MIN].offset, 0.125f);
    ASSERT(child->Points.x[FPP_MIN].relativeTo == root);

    ASSERT_EQ_INT(child->Points.y[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MIN].offset, -0.25f);
    ASSERT(child->Points.y[FPP_MIN].relativeTo == root);
}

static void test_comment_markers_inside_quoted_strings_are_preserved(void) {
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
    ASSERT_STR_EQ(text->Text, "literal // text and /* block marker */ text");
    ASSERT_STR_EQ(more_text->Text, "more /* marker */ and // marker text");
}

static void test_shipped_style_disabled_properties_do_not_escape_comments(void) {
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

    ASSERT(icon->Parent == root);
    ASSERT(text->Parent == root);
    ASSERT_EQ_INT(icon->Type, FT_TEXTURE);
    ASSERT_EQ_INT(text->Type, FT_STRING);
    ASSERT_EQ_INT(icon->Points.x[FPP_MIN].used, 1);
    ASSERT_FLOAT_EQ(icon->Points.x[FPP_MIN].offset, 0.010f);
    ASSERT_EQ_INT(text->Points.x[FPP_MIN].used, 1);
    ASSERT_FLOAT_EQ(text->Points.x[FPP_MIN].offset, 0.030f);
    ASSERT(text->Points.x[FPP_MIN].relativeTo == icon);
    ASSERT_STR_EQ(text->Text, "No Upkeep");
    ASSERT_NOT_NULL(UI_FindFrame("AfterCommentText"));
}

static void test_backdrop_background_adds_blp_extension(void) {
    LPFRAMEDEF frame;

    reset_ui_state();
    parse_fdf("backdrop_bg_path.fdf",
              "Frame \"BACKDROP\" \"BD\" {"
              " BackdropBackground \"TestUI/Textures/checker_8x8\","
              "}");

    frame = UI_FindFrame("BD");
    if (!require_not_null(frame)) return;
    ASSERT_EQ_INT(frame->Backdrop.Background, 1);
    ASSERT_NOT_NULL(captured_image_path);
    ASSERT_STR_EQ(captured_image_path, "TestUI/Textures/checker_8x8.blp");
}

static void test_background_art_uses_model_index(void) {
    LPFRAMEDEF sprite;

    reset_ui_state();
    parse_fdf("sprite_path.fdf",
              "Frame \"SPRITE\" \"SpriteA\" {"
              " BackgroundArt \"TestUI/Models/quad_sprite.mdx\","
              "}");

    sprite = UI_FindFrame("SpriteA");
    if (!require_not_null(sprite)) return;
    ASSERT_EQ_INT(sprite->Type, FT_SPRITE);
    ASSERT_EQ_INT(sprite->Portrait.model, 1);
    ASSERT_NOT_NULL(captured_model_path);
    ASSERT_STR_EQ(captured_model_path, "TestUI/Models/quad_sprite.mdx");
}

static void test_collect_frame_tree_preorder_matches_writer_traversal(void) {
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

    ASSERT_EQ_INT((int)count, 4);
    ASSERT(out[0] == root);
    ASSERT(out[1] == child_a);
    ASSERT(out[2] == grand);
    ASSERT(out[3] == child_b);
}

static void test_collect_frame_tree_skips_hidden_children(void) {
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

    ASSERT_EQ_INT((int)count, 2);
    ASSERT(out[0] == root);
    ASSERT(out[1] == visible);
}

static void test_collect_frame_tree_skips_button_control_art(void) {
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

    ASSERT_EQ_INT((int)count, 2);
    ASSERT(out[0] == button);
    ASSERT(out[1] == text);
}

static void test_collect_frame_tree_skips_editbox_text_frame(void) {
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

    ASSERT_EQ_INT((int)count, 1);
    ASSERT(out[0] == editbox);
}

static void test_collect_frame_tree_returns_total_when_truncated(void) {
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

    ASSERT_EQ_INT((int)count, 4);
    ASSERT(out[0] == root);
    ASSERT_NOT_NULL(out[1]);
}

static void test_find_child_frame_descends_recursively(void) {
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
    ASSERT_STR_EQ(found->Name, "L2");
}

static void test_programmatic_setpoint_maps_to_points(void) {
    FRAMEDEF root;
    FRAMEDEF child;

    reset_ui_state();

    UI_InitFrame(&root, FT_FRAME);
    strcpy(root.Name, "Root");

    UI_InitFrame(&child, FT_FRAME);
    strcpy(child.Name, "Child");

    UI_SetPoint(&child, FRAMEPOINT_CENTER, &root, FRAMEPOINT_TOPLEFT, 0.11f, -0.22f);

    ASSERT_EQ_INT(child.Points.x[FPP_MID].used, 1);
    ASSERT_EQ_INT(child.Points.y[FPP_MID].used, 1);
    ASSERT_EQ_INT(child.Points.x[FPP_MID].targetPos, FPP_MIN);
    ASSERT_EQ_INT(child.Points.y[FPP_MID].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child.Points.x[FPP_MID].offset, 0.11f);
    ASSERT_FLOAT_EQ(child.Points.y[FPP_MID].offset, -0.22f);
    ASSERT(child.Points.x[FPP_MID].relativeTo == &root);
    ASSERT(child.Points.y[FPP_MID].relativeTo == &root);
}

static void test_programmatic_setallpoints_sets_both_axes(void) {
    FRAMEDEF frame;

    reset_ui_state();
    UI_InitFrame(&frame, FT_FRAME);
    UI_SetAllPoints(&frame);

    ASSERT_EQ_INT(frame.Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(frame.Points.x[FPP_MAX].used, 1);
    ASSERT_EQ_INT(frame.Points.y[FPP_MIN].used, 1);
    ASSERT_EQ_INT(frame.Points.y[FPP_MAX].used, 1);
}

/* --- SetPoint: coverage for each FRAMEPOINT position --- */

static void test_setpoint_top_maps_mid_x_max_y(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MID].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MID].targetPos, FPP_MID);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MID].offset, 0.03f);

    ASSERT_EQ_INT(child->Points.y[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MIN].offset, -0.07f);
}

static void test_setpoint_topright_maps_max_x_max_y(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MAX].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MAX].targetPos, FPP_MAX);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MAX].offset, 0.05f);

    ASSERT_EQ_INT(child->Points.y[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MIN].offset, -0.10f);
}

static void test_setpoint_left_maps_min_x_mid_y(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MIN].offset, 0.02f);

    ASSERT_EQ_INT(child->Points.y[FPP_MID].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MID].targetPos, FPP_MID);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MID].offset, 0.04f);
}

static void test_setpoint_center_maps_mid_x_mid_y(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MID].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MID].targetPos, FPP_MID);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MID].offset, 0.0f);

    ASSERT_EQ_INT(child->Points.y[FPP_MID].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MID].targetPos, FPP_MID);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MID].offset, 0.0f);
}

static void test_setpoint_right_maps_max_x_mid_y(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MAX].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MAX].targetPos, FPP_MAX);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MAX].offset, -0.06f);

    ASSERT_EQ_INT(child->Points.y[FPP_MID].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MID].targetPos, FPP_MID);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MID].offset, 0.0f);
}

static void test_setpoint_bottomleft_maps_min_x_min_y(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MIN].offset, 0.01f);

    ASSERT_EQ_INT(child->Points.y[FPP_MAX].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MAX].targetPos, FPP_MAX);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MAX].offset, 0.02f);
}

static void test_setpoint_bottom_maps_mid_x_min_y(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MID].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MID].targetPos, FPP_MID);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MID].offset, 0.0f);

    ASSERT_EQ_INT(child->Points.y[FPP_MAX].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MAX].targetPos, FPP_MAX);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MAX].offset, 0.08f);
}

static void test_setpoint_bottomright_maps_max_x_min_y(void) {
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

    ASSERT_EQ_INT(child->Points.x[FPP_MAX].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MAX].targetPos, FPP_MAX);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MAX].offset, -0.04f);

    ASSERT_EQ_INT(child->Points.y[FPP_MAX].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MAX].targetPos, FPP_MAX);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MAX].offset, 0.05f);
}

/* --- SetPoint: center-mutex behavior --- */

static void test_setpoint_edge_overrides_and_clears_center(void) {
    FRAMEDEF frame;

    reset_ui_state();
    UI_InitFrame(&frame, FT_FRAME);

    /* Set CENTER first — both mid slots occupied */
    UI_SetPoint(&frame, FRAMEPOINT_CENTER, NULL, FRAMEPOINT_CENTER, 0.5f, 0.5f);
    ASSERT_EQ_INT(frame.Points.x[FPP_MID].used, 1);
    ASSERT_EQ_INT(frame.Points.y[FPP_MID].used, 1);

    /* Set TOPLEFT — should clear x-mid and y-mid, then fill x-min and y-min */
    UI_SetPoint(&frame, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    ASSERT_EQ_INT(frame.Points.x[FPP_MID].used, 0);
    ASSERT_EQ_INT(frame.Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(frame.Points.y[FPP_MID].used, 0);
    ASSERT_EQ_INT(frame.Points.y[FPP_MIN].used, 1);
}

static void test_setpoint_center_ignored_when_edges_set(void) {
    FRAMEDEF frame;

    reset_ui_state();
    UI_InitFrame(&frame, FT_FRAME);

    /* Anchor x and y edges via TOPLEFT + BOTTOMRIGHT */
    UI_SetPoint(&frame, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.0f, 0.0f);
    UI_SetPoint(&frame, FRAMEPOINT_BOTTOMRIGHT, NULL, FRAMEPOINT_BOTTOMRIGHT, 0.0f, 0.0f);

    /* Attempt to set CENTER — should be ignored on both axes */
    UI_SetPoint(&frame, FRAMEPOINT_CENTER, NULL, FRAMEPOINT_CENTER, 0.5f, 0.5f);

    ASSERT_EQ_INT(frame.Points.x[FPP_MID].used, 0);
    ASSERT_EQ_INT(frame.Points.y[FPP_MID].used, 0);
    /* Edges must still be intact */
    ASSERT_EQ_INT(frame.Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(frame.Points.x[FPP_MAX].used, 1);
    ASSERT_EQ_INT(frame.Points.y[FPP_MIN].used, 1);
    ASSERT_EQ_INT(frame.Points.y[FPP_MAX].used, 1);
}

/* --- SetAllPoints: detailed field verification --- */

static void test_setallpoints_zero_offsets_and_target_positions(void) {
    FRAMEDEF frame;

    reset_ui_state();
    UI_InitFrame(&frame, FT_FRAME);
    UI_SetAllPoints(&frame);

    /* TOPLEFT anchor: x[MIN] left->left, y[MIN] top->top */
    ASSERT_FLOAT_EQ(frame.Points.x[FPP_MIN].offset, 0.0f);
    ASSERT_EQ_INT(frame.Points.x[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_NULL(frame.Points.x[FPP_MIN].relativeTo);

    ASSERT_FLOAT_EQ(frame.Points.y[FPP_MIN].offset, 0.0f);
    ASSERT_EQ_INT(frame.Points.y[FPP_MIN].targetPos, FPP_MIN);
    ASSERT_NULL(frame.Points.y[FPP_MIN].relativeTo);

    /* BOTTOMRIGHT anchor: x[MAX] right->right, y[MAX] bottom->bottom */
    ASSERT_FLOAT_EQ(frame.Points.x[FPP_MAX].offset, 0.0f);
    ASSERT_EQ_INT(frame.Points.x[FPP_MAX].targetPos, FPP_MAX);
    ASSERT_NULL(frame.Points.x[FPP_MAX].relativeTo);

    ASSERT_FLOAT_EQ(frame.Points.y[FPP_MAX].offset, 0.0f);
    ASSERT_EQ_INT(frame.Points.y[FPP_MAX].targetPos, FPP_MAX);
    ASSERT_NULL(frame.Points.y[FPP_MAX].relativeTo);

    /* Center slot must remain unused */
    ASSERT_EQ_INT(frame.Points.x[FPP_MID].used, 0);
    ASSERT_EQ_INT(frame.Points.y[FPP_MID].used, 0);
}

static void test_setallpoints_with_relative_frame_propagates_to_both_anchors(void) {
    LPFRAMEDEF child;

    reset_ui_state();
    parse_fdf("setallpoints_rel.fdf",
              "Frame \"FRAME\" \"Anchor\" { Width 0.8, Height 0.6, }"
              "Frame \"FRAME\" \"Child\" { SetAllPoints, }");

    child = UI_FindFrame("Child");
    if (!require_not_null(child)) return;

    /* SetAllPoints via FDF uses NULL relativeTo (resolved at layout time) */
    ASSERT_EQ_INT(child->Points.x[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.x[FPP_MAX].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MIN].used, 1);
    ASSERT_EQ_INT(child->Points.y[FPP_MAX].used, 1);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MIN].offset, 0.0f);
    ASSERT_FLOAT_EQ(child->Points.x[FPP_MAX].offset, 0.0f);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MIN].offset, 0.0f);
    ASSERT_FLOAT_EQ(child->Points.y[FPP_MAX].offset, 0.0f);
}

static void test_text_uses_key_when_no_stringlist_entry_exists(void) {
    LPFRAMEDEF text;

    reset_ui_state();
    parse_fdf("text_key_passthrough.fdf",
              "Frame \"TEXT\" \"TextA\" {"
              " Text \"TRIGSTR_999\","
              "}");

    text = UI_FindFrame("TextA");
    if (!require_not_null(text)) return;
    ASSERT_STR_EQ(text->Text, "TRIGSTR_999");
}

static void test_long_stringlist_text_uses_dynamic_storage(void) {
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
    ASSERT_EQ_INT(strlen(text->Text), 124);
    ASSERT(text->Text != text->TextStorage);
    ASSERT_NULL(text->Tip);
    ASSERT_NULL(text->Ubertip);
}

static void test_duplicate_name_prefers_first_template(void) {
    LPFRAMEDEF found;

    reset_ui_state();
    parse_fdf("dup_name.fdf",
              "Frame \"FRAME\" \"Dup\" { Width 0.10, }"
              "Frame \"FRAME\" \"Dup\" { Width 0.20, }");

    found = UI_FindFrame("Dup");
    if (!require_not_null(found)) return;
    ASSERT_FLOAT_EQ(found->Width, 0.10f);
}

static void test_unknown_token_does_not_crash_existing_definitions(void) {
    LPFRAMEDEF good;

    reset_ui_state();
    parse_fdf("unknown_token.fdf",
              "Frame \"FRAME\" \"Good\" { Width 0.5, }"
              "Frame \"FRAME\" \"Bad\" { UnknownThing 1, }");

    good = UI_FindFrame("Good");
    if (!require_not_null(good)) return;
    ASSERT_FLOAT_EQ(good->Width, 0.5f);
}

static void test_single_line_text_auto_height_uses_fdf_font_size(void) {
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

    ASSERT_EQ_INT(captured_text_draws, 2);
    ASSERT_EQ_FLOAT(captured_text_rects[0].h, 0.013f, 0.0001f);
    ASSERT_EQ_FLOAT(captured_text_rects[1].h, 0.016f, 0.0001f);
}

static void test_glue_checkbox_toggles_and_draws_check_highlight(void) {
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
    ASSERT(!checkbox->CheckBox.Checked);
    ASSERT_EQ_INT(captured_draw_calls, 1);

    test_mouse_pos.x = 130;
    test_mouse_pos.y = 130;
    UI_MouseEventLocal(130, 130, 1, false);
    captured_draw_calls = 0;
    UI_DrawFrame(root);
    ASSERT(checkbox->CheckBox.Checked);
    ASSERT_EQ_INT(captured_draw_calls, 2);

    UI_MouseEventLocal(130, 130, 1, false);
    captured_draw_calls = 0;
    UI_DrawFrame(root);
    ASSERT(!checkbox->CheckBox.Checked);
    ASSERT_EQ_INT(captured_draw_calls, 1);
}

static void test_button1_dropdown_backdrop_gets_hover_highlight(void) {
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
    ASSERT_NOT_NULL(hover_texture);

    test_mouse_pos.x = 130;
    test_mouse_pos.y = 130;
    captured_hover_draws = 0;
    UI_DrawFrame(root);

    ASSERT_EQ_INT(captured_hover_draws, 1);
}

static void test_backdrop_edge_without_corner_size_logs_error(void) {
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
    ASSERT(strstr(captured_printf, "BackdropCornerSize is zero") != NULL);
}

static void test_editbox_without_text_frame_click_focus_accepts_text_input(void) {
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
    UI_MouseEventLocal(130, 130, 1, true);
    UI_DrawFrame(root);

    ASSERT(UI_EditHasFocus(editbox));
    UI_TextInputLocal("hello");
    ASSERT_STR_EQ(UI_EditValue(editbox), "hello");
    ASSERT(!UI_EditKey(13));
}

static void test_options_game_port_enter_applies_and_blurs(void) {
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
    captured_command[0] = '\0';

    ASSERT(optionsMenuScreen.load());
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
    UI_MouseEventLocal(130, 130, 1, true);
    UI_DrawFrame(root);
    ASSERT(UI_EditHasFocus(editbox));

    optionsMenuScreen.key_event(13, true);

    ASSERT_STR_EQ(captured_command, "seta game_port 27911\n");
    ASSERT(!UI_EditHasFocus(editbox));

    uiimport = saved;
}

static void test_esc_menu_confirm_quit_panel_is_available(void) {
    LPFRAMEDEF panel;
    LPFRAMEDEF quit_button;
    LPFRAMEDEF cancel_button;
    LPFRAMEDEF message;

    load_ui_file("UI\\FrameDef\\UI\\EscMenuMainPanel.fdf");

    panel = UI_FindFrame("ConfirmQuitPanel");
    quit_button = UI_FindFrame("ConfirmQuitQuitButton");
    cancel_button = UI_FindFrame("ConfirmQuitCancelButton");
    message = UI_FindFrame("ConfirmQuitMessageText");

    if (!require_not_null(panel)) return;
    if (!require_not_null(quit_button)) return;
    if (!require_not_null(cancel_button)) return;
    if (!require_not_null(message)) return;

    ASSERT_STR_EQ(quit_button->Text, "ConfirmQuitQuitButtonText");
    ASSERT_STR_EQ(cancel_button->Text, "ConfirmQuitCancelButtonText");
    ASSERT_STR_EQ(message->Text, "CONFIRM_EXIT_MESSAGE");
}

static void test_dialog_war3_supports_configurable_button_modes(void) {
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

    ASSERT(UI_DialogWar3Init(&dialog, root, &init));
    ASSERT(!UI_DialogWar3Visible(&dialog));

    UI_DialogWar3Show(&dialog, &config);
    ASSERT(UI_DialogWar3Visible(&dialog));
    ASSERT(dialog.modal->Parent == NULL);
    ASSERT_STR_EQ(dialog.frames.DialogWar3->DialogBackdropName, "DialogBackdrop");
    ASSERT_NOT_NULL(UI_FindChildFrame(dialog.frames.DialogWar3, dialog.frames.DialogWar3->DialogBackdropName));
    ASSERT_STR_EQ(dialog.frames.DialogText->Text, "Are you sure you want to exit?");
    ASSERT(dialog.frames.DialogIcon->Backdrop.Background != 0);
    ASSERT(!dialog.frames.DialogButtonOKBackdrop->hidden);
    ASSERT(dialog.frames.DialogButtonNoBackdrop->hidden);
    ASSERT(dialog.frames.DialogButtonYesBackdrop->hidden);
    ASSERT_STR_EQ(dialog.frames.DialogButtonOK->OnClick, "menu_main");

    UI_DialogWar3Hide(&dialog);
    ASSERT(!UI_DialogWar3Visible(&dialog));
}

static void test_dialog_supports_battlenet_template(void) {
    LPCSTR files[] = {
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\DialogWar3.fdf",
        "UI\\FrameDef\\Glue\\BattleNetTemplates.fdf",
    };
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

    load_ui_files(files, sizeof(files) / sizeof(files[0]));
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Printf = test_ui_printf;

    root = UI_Spawn(FT_FRAME, NULL);
    if (!require_not_null(root)) return;
    UI_SetSize(root, UI_BASE_WIDTH, UI_BASE_HEIGHT);

    ASSERT(UI_DialogWar3Init(&dialog, root, &init));
    ASSERT(dialog.frame->Width > 0.5f);
    ASSERT(dialog.frame->Height > 0.3f);
    ASSERT(dialog.icon == NULL);
    ASSERT_NOT_NULL(dialog.text);
    ASSERT_NOT_NULL(dialog.ok_button);
    ASSERT_NOT_NULL(dialog.frame->DialogBackdrop);
    ASSERT(dialog.ok_backdrop == dialog.ok_button);
    ASSERT(dialog.ok_button->Parent == dialog.frame);

    UI_DialogWar3Show(&dialog, &config);
    ASSERT(UI_DialogWar3Visible(&dialog));
    ASSERT_STR_EQ(dialog.text->Text, "OpenWarcraft3\nA larger dialog template.");
    ASSERT_STR_EQ(dialog.ok_button->OnClick, "menu_main");
}

static void test_main_menu_quit_dialog_commands_quit(void) {
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
    captured_command[0] = '\0';

    ASSERT(mainMenuScreen.load());
    mainMenuScreen.init();

    global_exit_button = UI_FindFrame("ExitButton");
    exit_button = UI_FindChildFrame(UI_FindFrame("MainMenuFrame"), "ExitButton");
    if (!require_not_null(exit_button)) {
        uiimport = saved;
        return;
    }
    ASSERT(global_exit_button != exit_button);
    ASSERT(!exit_button->hidden);
    ASSERT_STR_EQ(exit_button->OnClick, "menu_quit");

    modal = UI_FindFrame("MainMenuQuitModal");
    if (!require_not_null(modal)) {
        uiimport = saved;
        return;
    }
    ASSERT_EQ_INT(modal->Type, FT_DIALOG);
    ASSERT(modal->Parent == NULL);
    ASSERT(modal->hidden);
    ASSERT(UI_FindChildFrame(modal, "MainMenuQuitModalCover") == NULL);

    dialog = UI_FindChildFrame(modal, "DialogWar3");
    if (!require_not_null(dialog)) {
        uiimport = saved;
        return;
    }
    ASSERT_EQ_INT(dialog->Type, FT_DIALOG);
    ASSERT(dialog->hidden);

    MainMenu_ShowQuitConfirm();
    ASSERT(!modal->hidden);
    ASSERT(!dialog->hidden);

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

    ASSERT_STR_EQ(message->Text, "Do you want to Quit?");
    ASSERT(icon->Backdrop.Background != 0);
    ASSERT(ok_backdrop->hidden);
    ASSERT(!no_backdrop->hidden);
    ASSERT(!yes_backdrop->hidden);
    ASSERT_STR_EQ(no_button->OnClick, "menu_main");
    ASSERT_STR_EQ(yes_button->OnClick, "quit");

    UI_MenuCommandLocal(no_button->OnClick);
    ASSERT(modal->hidden);
    ASSERT(dialog->hidden);

    UI_MenuCommandLocal(yes_button->OnClick);
    ASSERT_STR_EQ(captured_command, "quit");

    uiimport = saved;
}

static void test_main_menu_realm_select_uses_realm_panel_anim(void) {
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

    ASSERT(mainMenuScreen.load());
    mainMenuScreen.init();
    MainMenu_ShowRealmSelect();
    captured_stand_sprites = 0;
    captured_realm_panel_sprites = 0;
    mainMenuScreen.draw();
    ASSERT_EQ_INT(captured_stand_sprites, 0);
    ASSERT_EQ_INT(captured_realm_panel_sprites, 2);
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
    LPFRAMEDEF cancel_button;
    LPFRAMEDEF campaign_select_frame;
    LPFRAMEDEF campaign_list_box;

    hide_expansion_campaign_file = false;
    test_fs_expansion = tft;
    load_ui_files(files, sizeof(files) / sizeof(files[0]));

    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.Printf = test_ui_printf;
    uiimport.GetRenderer = test_get_renderer;
    uiimport.Cmd_ExecuteText = test_cmd_execute_text;
    uiimport.Cvar_String = test_cvar_string;
    uiimport.FS_ReadFile = test_fs_read_file;
    uiimport.FS_FreeFile = test_fs_free_file;
    uiimport.MemAlloc = test_ui_mem_alloc;
    uiimport.MemFree = test_ui_mem_free;

    if (!singlePlayerMenuScreen.load()) {
        ASSERT(false);
        uiimport = saved;
        return;
    }
    singlePlayerMenuScreen.init();

    root = UI_FindFrame("SinglePlayerMenu");
    campaign_button = UI_FindFrame("CampaignButton");
    cancel_button = UI_FindFrame("CancelButton");

    if (!require_not_null(root)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(campaign_button)) {
        uiimport = saved;
        return;
    }
    if (!require_not_null(cancel_button)) {
        uiimport = saved;
        return;
    }
    ASSERT(!root->hidden);
    ASSERT_STR_EQ(campaign_button->OnClick, "menu_single_player_campaign");
    ASSERT_STR_EQ(cancel_button->OnClick, "menu_main");

    SinglePlayerMenu_ShowCampaign();
    campaign_select_frame = UI_FindFrame("CampaignSelectFrame");
    campaign_list_box = campaign_select_frame
        ? UI_FindChildFrame(campaign_select_frame, "MapListBox")
        : NULL;

    if (!require_not_null(campaign_list_box)) {
        uiimport = saved;
        return;
    }
    ASSERT(!campaign_list_box->hidden);
    ASSERT(campaign_list_box->MapListControl.State != NULL);
    ASSERT_EQ_INT((int)campaign_list_box->MapListControl.State->count, 4);
    ASSERT_STR_EQ(campaign_list_box->MapListControl.State->items[0].name,
                  tft
                      ? "Sentinels Campaign: Terror of the Tides"
                      : "Human Campaign: The Scourge of Lordaeron");
    ASSERT_STR_EQ(campaign_list_box->MapListControl.State->items[1].path,
                  tft ? "Human" : "Undead");
    ASSERT_STR_EQ(campaign_list_box->MapListControl.SelectCommand,
                  "menu_single_player_campaign_select %u");

    captured_command[0] = '\0';
    SinglePlayerMenu_LaunchCampaignIndex(tft ? 1 : 0);
    ASSERT_STR_EQ(captured_command,
                  tft
                      ? "map \"Maps\\FrozenThrone\\Campaign\\HumanX01.w3x\""
                      : "map \"Maps\\Campaign\\Human01.w3m\"");

    hide_expansion_campaign_file = false;
    test_fs_expansion = false;
    uiimport = saved;
}

static void test_single_player_screen_loads_roc_campaigns(void) {
    test_single_player_campaign_profile(false);
}

static void test_single_player_screen_loads_tft_campaigns(void) {
    test_single_player_campaign_profile(true);
}

BEGIN_SUITE(ui_fdf)
    RUN_TEST(test_parse_single_frame_definition);
    RUN_TEST(test_parse_nested_parent_child_relationship);
    RUN_TEST(test_inherits_copies_compatible_type_fields);
    RUN_TEST(test_inherits_rejects_incompatible_type);
    RUN_TEST(test_setpoint_top_left_sets_top_y_anchor);
    RUN_TEST(test_setallpoints_sets_min_and_max);
    RUN_TEST(test_anchor_translates_to_setpoint_state);
    RUN_TEST(test_backdrop_flags_and_insets_are_parsed);
    RUN_TEST(test_vector_parser_accepts_f_suffixes);
    RUN_TEST(test_chat_display_tokens_map_to_text_area);
    RUN_TEST(test_comments_are_ignored_inside_frame_bodies);
    RUN_TEST(test_comments_are_ignored_between_setpoint_arguments);
    RUN_TEST(test_comment_markers_inside_quoted_strings_are_preserved);
    RUN_TEST(test_shipped_style_disabled_properties_do_not_escape_comments);
    RUN_TEST(test_backdrop_background_adds_blp_extension);
    RUN_TEST(test_background_art_uses_model_index);
    RUN_TEST(test_collect_frame_tree_preorder_matches_writer_traversal);
    RUN_TEST(test_collect_frame_tree_skips_hidden_children);
    RUN_TEST(test_collect_frame_tree_skips_button_control_art);
    RUN_TEST(test_collect_frame_tree_skips_editbox_text_frame);
    RUN_TEST(test_collect_frame_tree_returns_total_when_truncated);
    RUN_TEST(test_find_child_frame_descends_recursively);
    RUN_TEST(test_programmatic_setpoint_maps_to_points);
    RUN_TEST(test_programmatic_setallpoints_sets_both_axes);
    RUN_TEST(test_setpoint_top_maps_mid_x_max_y);
    RUN_TEST(test_setpoint_topright_maps_max_x_max_y);
    RUN_TEST(test_setpoint_left_maps_min_x_mid_y);
    RUN_TEST(test_setpoint_center_maps_mid_x_mid_y);
    RUN_TEST(test_setpoint_right_maps_max_x_mid_y);
    RUN_TEST(test_setpoint_bottomleft_maps_min_x_min_y);
    RUN_TEST(test_setpoint_bottom_maps_mid_x_min_y);
    RUN_TEST(test_setpoint_bottomright_maps_max_x_min_y);
    RUN_TEST(test_setpoint_edge_overrides_and_clears_center);
    RUN_TEST(test_setpoint_center_ignored_when_edges_set);
    RUN_TEST(test_setallpoints_zero_offsets_and_target_positions);
    RUN_TEST(test_setallpoints_with_relative_frame_propagates_to_both_anchors);
    RUN_TEST(test_text_uses_key_when_no_stringlist_entry_exists);
    RUN_TEST(test_long_stringlist_text_uses_dynamic_storage);
    RUN_TEST(test_duplicate_name_prefers_first_template);
    RUN_TEST(test_unknown_token_does_not_crash_existing_definitions);
    RUN_TEST(test_single_line_text_auto_height_uses_fdf_font_size);
    RUN_TEST(test_glue_checkbox_toggles_and_draws_check_highlight);
    RUN_TEST(test_button1_dropdown_backdrop_gets_hover_highlight);
    RUN_TEST(test_backdrop_edge_without_corner_size_logs_error);
    RUN_TEST(test_editbox_without_text_frame_click_focus_accepts_text_input);
    RUN_TEST(test_esc_menu_confirm_quit_panel_is_available);
    RUN_TEST(test_options_game_port_enter_applies_and_blurs);
    RUN_TEST(test_dialog_war3_supports_configurable_button_modes);
    RUN_TEST(test_dialog_supports_battlenet_template);
    RUN_TEST(test_main_menu_quit_dialog_commands_quit);
    RUN_TEST(test_main_menu_realm_select_uses_realm_panel_anim);
    RUN_TEST(test_single_player_screen_loads_roc_campaigns);
    RUN_TEST(test_single_player_screen_loads_tft_campaigns);
END_SUITE()
