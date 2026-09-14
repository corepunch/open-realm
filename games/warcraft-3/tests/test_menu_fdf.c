#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "test.h"
#include "common/common.h"
#include "../menu/menu_local.h"
#include "../renderer/mdx/r_mdx.h"
#include "../menu/menu_dialog.h"
#include "../menu/menu_screen.h"
#include "../common/minimap.h"
#include "../common/campaign_progress.h"
#include "../../../common/mpq.h"
#include "../../../common/video_modes.h"

static const char *captured_image_path;
static const char *captured_model_path;
static char captured_command[8192];
static char captured_movie_path[MAX_PATHLEN];
static char captured_cvar_name[64];
static char captured_cvar_value[64];
static char captured_printf[512];
static DWORD captured_draw_calls;
static DWORD captured_dim_draws;
static DWORD captured_dim_draw_index;
static DWORD captured_text_draws;
static DWORD captured_stand_sprites;
static DWORD captured_realm_panel_sprites;
static DWORD captured_sprite_calls;
static FLOAT captured_sprite_x[2];
static PATHSTR captured_sprite_anim[2];
static size2_t test_window_size = { 1000, 750 };
static DWORD captured_birth_sprites;
static DWORD captured_death_sprites;
static DWORD captured_glue_changes;
static uintptr_t fake_texture_id;
static LPTEXTURE hover_texture;
static DWORD captured_hover_draws;
static RECT captured_text_rects[8];
static VECTOR2 fake_text_size;
static HANDLE test_mpq_archive;
static BOOL hide_expansion_campaign_file;
static BOOL test_fs_expansion;
static int test_vid_native = -1;
static LPCSTR test_campaign_visibility;
static PATHSTR test_campaign_progress_path = "campaign-progress-menu-test.orcp";
static VECTOR2 test_mouse_pos;
static LPCSTR test_map = "";
static DWORD map_reads, texture_releases;
static char forwarded_command[1024];

/* Host boundaries only: tests link the real command buffer, tokenizer, registration, and cvars. */
void Key_Init(void) {}
void Key_WriteBindings(FILE *file) { (void)file; }
void Cmd_ForwardToServer(LPCSTR text) { snprintf(forwarded_command, sizeof(forwarded_command), "%s", text); }
void CL_SetGameplayBindings(void) {}
void CL_Connect(LPCSTR host, unsigned short port) { (void)host; (void)port; }
void CL_BeginLoadingMap(LPCSTR map) { (void)map; Cbuf_AddText("menu_ingame\n"); }
void CL_Shutdown(void) {}
void SV_Map(LPCSTR path) { (void)path; }
BOOL SV_GetSaveMap(LPCSTR name, LPSTR map, DWORD size) { (void)name; (void)map; (void)size; return false; }
BOOL SV_LoadGame(LPCSTR name, LPCSTR map) { (void)name; (void)map; return false; }
void SV_Shutdown(void) {}
void Sys_Quit(void) {}
void PF_Sleep(DWORD msec) { (void)msec; }

static void test_command_imports(void) {
    static BOOL ready;
    if (!ready) { Cbuf_Init(); Cvar_Init(); ready = true; }
    mi.Cmd_AddCommand = Cmd_AddCommand;
    mi.Cmd_Argc = Cmd_Argc;
    mi.Cmd_Argv = Cmd_Argv;
    mi.Cmd_ArgsFrom = Cmd_ArgsFrom;
}

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

static void test_draw_sprite(drawSprite_t const *sprite) {
    LPCSTR anim = sprite->anim;
    FLOAT x = sprite->x;
    if (captured_sprite_calls < 2) {
        captured_sprite_x[captured_sprite_calls] = x;
        snprintf(captured_sprite_anim[captured_sprite_calls], sizeof(captured_sprite_anim[0]), "%s", anim ? anim : "");
    }
    captured_sprite_calls++;
    if (anim && !strcmp(anim, "Stand"))
        captured_stand_sprites++;
    if (anim && !strcmp(anim, "RealmSelection Stand"))
        captured_realm_panel_sprites++;
    if (anim && !strncmp(anim, "MainMenu Birth", strlen("MainMenu Birth")))
        captured_birth_sprites++;
    if (anim && !strncmp(anim, "MainMenu Death", strlen("MainMenu Death")))
        captured_death_sprites++;
}

static void test_glue_changed(void) { captured_glue_changes++; }

static void test_draw_backdrop(LPCDRAWBACKDROP draw_backdrop) {
    (void)draw_backdrop;
    captured_draw_calls++;
}

static size2_t test_get_window_size(void) {
    return test_window_size;
}

static void test_release_texture(LPTEXTURE texture) { (void)texture; texture_releases++; }
static void test_release_model(LPMODEL model) { (void)model; }
static bool test_entity_anim(LPCMODEL model, LPCSTR anim, renderEntity_t *entity) {
    (void)model; (void)anim; (void)entity;
    return true;
}
static void test_render_frame(viewDef_t const *view) { (void)view; }
static LPRENDERER test_get_renderer(void) {
    static refExport_t renderer = {
        .LoadTexture = test_load_texture,
        .ReleaseTexture = test_release_texture,
        .LoadModel = test_load_model,
        .ReleaseModel = test_release_model,
        .LoadFont = test_load_font,
        .GetWindowSize = test_get_window_size,
        .DrawImageEx = test_draw_image_ex,
        .DrawBackdrop = test_draw_backdrop,
        .DrawText = test_draw_text,
        .DrawSprite = test_draw_sprite,
        .SetEntityAnimFrame = test_entity_anim,
        .RenderFrame = test_render_frame,
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
    if (text && !strcmp(text, "\n")) return;
    snprintf(captured_command, sizeof(captured_command), "%s", text ? text : "");
}

static BOOL test_play_movie(LPCSTR path) {
    snprintf(captured_movie_path, sizeof(captured_movie_path), "%s", path ? path : "");
    return true;
}

static LPCSTR test_cvar_string(LPCSTR name, LPCSTR fallback) {
    if (!strcmp(name, "map")) return test_map;
    if (name && !strcmp(name, "fs_expansion")) {
        return test_fs_expansion ? "1" : "0";
    }
    if (name && !strcmp(name, "game_port")) {
        return "27910";
    }
    if (name && !strcmp(name, "vid_native") && test_vid_native >= 0) {
        return test_vid_native ? "1" : "0";
    }
    if (name && !strcmp(name, "wc3_campaign_visibility") && test_campaign_visibility) {
        return test_campaign_visibility;
    }
    return fallback;
}

static void test_cvar_set(LPCSTR name, LPCSTR value) {
    snprintf(captured_cvar_name, sizeof(captured_cvar_name), "%s", name ? name : "");
    snprintf(captured_cvar_value, sizeof(captured_cvar_value), "%s", value ? value : "");
    if (name && !strcmp(name, "fs_expansion")) test_fs_expansion = value && atoi(value) != 0;
}

static void test_user_path(LPCSTR rel, LPSTR out, DWORD out_size) {
    (void)rel;
    if (!out || !out_size) return;
    snprintf(out, out_size, "%s", test_campaign_progress_path);
}

static void load_ui_files(LPCSTR const *file_names, size_t count) {
    menuImport_t saved = mi;

    UI_ClearTemplates();
    memset(&mi, 0, sizeof(mi));
    mi.FS_ReadFile = test_fs_read_file;
    mi.FS_FreeFile = test_fs_free_file;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;

    mi.ImageIndex = test_image_index;
    mi.FontIndex = test_font_index;
    mi.Printf = test_ui_printf;
    test_command_imports();
    for (size_t i = 0; i < count; i++) {
        UI_ParseFDF(file_names[i]);
    }
    mi = saved;
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
    captured_sprite_calls = 0;
    memset(captured_sprite_x, 0, sizeof(captured_sprite_x));
    memset(captured_sprite_anim, 0, sizeof(captured_sprite_anim));
    captured_birth_sprites = 0;
    captured_death_sprites = 0;
    captured_glue_changes = 0;
    fake_texture_id = 0;
    texture_releases = map_reads = 0;
    menu_player = NULL; test_map = "";
    test_vid_native = -1;
    hover_texture = NULL;
    captured_hover_draws = 0;
    memset(captured_text_rects, 0, sizeof(captured_text_rects));
    fake_text_size = MAKE(VECTOR2, 0.050f, 0.016f);
    test_mouse_pos = MAKE(VECTOR2, 0, 0);
    UI_ClearEditFocus();
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;
    mi.ImageIndex = fake_image_index;
    mi.FontIndex = test_font_index;
    mi.GetRenderer = test_get_renderer;
    mi.Printf = test_ui_printf;
    test_command_imports();

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

TEST(menu_fdf, find_child_frame_type_descends_through_control_wrapper) {
    LPFRAMEDEF root;
    LPFRAMEDEF found;

    reset_ui_state();
    parse_fdf("find_child_type.fdf",
              "Frame \"FRAME\" \"Root\" {"
              " Frame \"CONTROL\" \"Decorated\" {"
              "  Frame \"FRAME\" \"Inner\" {"
              "   Frame \"LISTBOX\" \"Rows\" { }"
              "  }"
              " }"
              "}");

    root = UI_FindFrame("Root");
    if (!require_not_null(root)) return;

    found = UI_FindChildFrameType(root, FT_LISTBOX);
    if (!require_not_null(found)) return;
    T_STREQ(found->Name, "Rows");
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
    menuImport_t saved = mi;
    LPFRAMEDEF root;
    LPFRAMEDEF button;

    reset_ui_state();
    mi.FS_ReadFile = test_missing_fs_read;
    mi.FS_FreeFile = test_fs_free_file;
    mi.Cvar_String = test_cvar_string;
    mi.Cmd_ExecuteText = test_cmd_execute_text;

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
        mi = saved;
        return;
    }
    UI_SetOnClick(button, "test_stale_menu_click");

    /* Simulate the retained rectangle from the last glue draw. The cache is
     * still hit-testable internally, but gameplay must not dispatch through it. */
    UI_DrawFrame(root);
    T_ASSERT(UI_HitTest(0.15f, 0.15f) != NULL);
    captured_command[0] = '\0';
    T_ASSERT(!M_MouseEvent(MENU_MOUSE_UP, 188, 144, 1));
    T_STREQ(captured_command, "");

    M_Shutdown();
    test_map = "";
    mi = saved;
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
    M_MouseEvent(MENU_MOUSE_UP, 130, 130, 1);
    captured_draw_calls = 0;
    UI_DrawFrame(root);
    T_ASSERT(checkbox->CheckBox.Checked);
    T_EQ(captured_draw_calls, 2);

    M_MouseEvent(MENU_MOUSE_UP, 130, 130, 1);
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
    mi.Cmd_ExecuteText = test_cmd_execute_text;
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
    M_MouseEvent(MENU_MOUSE_UP, 188, 144, 1);
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

    M_MouseEvent(MENU_MOUSE_MOVE, px, py, 0);

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
    M_MouseEvent(MENU_MOUSE_MOVE, 130, 130, 0);
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
    M_MouseEvent(MENU_MOUSE_DOWN, 130, 130, 1);
    captured_draw_calls = 0;
    UI_DrawFrame(root);

    T_ASSERT(UI_EditHasFocus(editbox));
    M_TextInput("hello");
    T_STREQ(UI_EditValue(editbox), "hello");
    T_ASSERT(!M_EditKey(13));
}

TEST(menu_fdf, options_video_mode_command_selects_fixed_or_native_mode) {
    menuImport_t saved = mi;
    char command[64];

    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.Cmd_ExecuteText = test_cmd_execute_text;

    captured_command[0] = '\0';
    Cmd_ExecuteString("menu_video_mode 5");
    T_STREQ(captured_command, "seta vid_native 0\nseta vid_mode 5\n");

    captured_command[0] = '\0';
    snprintf(command, sizeof(command), "menu_video_mode %u", (unsigned)video_mode_count());
    Cmd_ExecuteString(command);
    T_STREQ(captured_command, "seta vid_native 1\nseta vid_fullscreen 1\n");

    snprintf(captured_command, sizeof(captured_command), "unchanged");
    snprintf(command, sizeof(command), "menu_video_mode %u", (unsigned)video_mode_count() + 1);
    Cmd_ExecuteString(command);
    T_STREQ(captured_command, "unchanged");

    mi = saved;
}

TEST(menu_fdf, options_resolution_popup_appends_and_selects_native_mode) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\OptionsMenu.fdf",
    };
    menuImport_t saved = mi;
    LPFRAMEDEF popup;
    LPFRAMEDEF title;
    LPFRAMEDEF menu;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));
    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.GetRenderer = test_get_renderer;
    mi.Cmd_ExecuteText = test_cmd_execute_text;
    mi.Cvar_String = test_cvar_string;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;
    test_vid_native = 1;

    T_ASSERT(optionsMenuScreen.load());
    optionsMenuScreen.init();

    /* Follow the native OptionsMenu ownership hierarchy so the fixture verifies the generated binding contract. */
    popup = UI_FindChildFrame(UI_FindFrame("OptionsMenu"), "ResolutionMenu");
    menu = popup ? UI_FindChildFrame(popup, "ResolutionPopupMenuMenu") : NULL;
    if (!require_not_null(popup) || !require_not_null(menu)) {
        test_vid_native = -1;
        mi = saved;
        return;
    }
    title = UI_FindChildFrame(popup, popup->Popup.TitleFrame);
    if (title) {
        LPFRAMEDEF text = UI_FindChildFrame(title, "StandardPopupMenuTitleTextTemplate");
        if (text) title = text;
    }

    T_EQ(menu->Menu.ItemCount, video_mode_count() + 1);
    T_STREQ(menu->Menu.Items[video_mode_count()].text, "Native");
    if (require_not_null(title)) {
        T_STREQ(title->Text, "Native");
    }

    test_vid_native = -1;
    mi = saved;
}

TEST(menu_fdf, options_game_port_enter_applies_and_blurs) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\OptionsMenu.fdf",
    };
    LPFRAMEDEF root;
    LPFRAMEDEF editbox;
    menuImport_t saved = mi;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));

    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.GetRenderer = test_get_renderer;
    mi.Cmd_ExecuteText = test_cmd_execute_text;
    mi.Cvar_String = test_cvar_string;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;

    captured_command[0] = '\0';

    T_ASSERT(optionsMenuScreen.load());
    optionsMenuScreen.init();

    root = UI_FindFrame("OptionsMenu");
    editbox = UI_FindFrame("GamePortEditBox");
    if (!require_not_null(root)) {
        mi = saved;
        return;
    }
    if (!require_not_null(editbox)) {
        mi = saved;
        return;
    }

    UI_SetEditValue(editbox, "27911");
    test_mouse_pos.x = 130;
    test_mouse_pos.y = 130;
    /* Event hit testing consumes the layout cache built by the preceding draw. */
    UI_DrawFrame(root);
    M_MouseEvent(MENU_MOUSE_DOWN, 130, 130, 1);
    T_ASSERT(UI_EditHasFocus(editbox));

    optionsMenuScreen.key_event(13, true);

    T_STREQ(captured_command, "seta game_port 27911\n");
    T_ASSERT(!UI_EditHasFocus(editbox));

    OptionsMenu_Apply();
    T_STREQ(captured_command, "vid_apply\nwriteconfig\n");

    mi = saved;
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
    mi.GetRenderer = test_get_renderer;
    mi.Printf = test_ui_printf;
    test_command_imports();

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
     * FS_ReadFile (load_ui_files restores mi before UI_DialogWar3Init runs). */
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
    mi.GetRenderer = test_get_renderer;
    mi.Printf = test_ui_printf;
    test_command_imports();

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
    mi.GetRenderer = test_get_renderer;
    mi.Printf = test_ui_printf;
    test_command_imports();
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
    mi.GetRenderer = test_get_renderer;
    mi.Printf = test_ui_printf;
    test_command_imports();
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
    menuImport_t saved = mi;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));

    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.GetRenderer = test_get_renderer;
    mi.Cmd_ExecuteText = test_cmd_execute_text;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;

    captured_command[0] = '\0';

    M_ShowMainMenu();

    global_exit_button = UI_FindFrame("ExitButton");
    exit_button = UI_FindChildFrame(UI_FindFrame("MainMenuFrame"), "ExitButton");
    if (!require_not_null(exit_button)) {
        mi = saved;
        return;
    }
    T_ASSERT(global_exit_button != exit_button);
    T_ASSERT(!exit_button->hidden);
    T_STREQ(exit_button->OnClick, "menu_quit");
    logo = UI_FindChildFrame(UI_FindFrame("MainMenuFrame"), "WarCraftIIILogo");
    if (!require_not_null(logo)) { mi = saved; return; }
    T_FEQ(logo->Points.x[FPP_MIN].offset, 0.13f, 0.001f);
    T_FEQ(logo->Points.y[FPP_MIN].offset, -0.08f, 0.001f);

    modal = UI_FindFrame("MainMenuQuitModal");
    if (!require_not_null(modal)) {
        mi = saved;
        return;
    }
    T_EQ(modal->Type, FT_DIALOG);
    T_ASSERT(modal->Parent == NULL);
    T_ASSERT(modal->hidden);
    T_ASSERT(UI_FindChildFrame(modal, "MainMenuQuitModalCover") == NULL);

    dialog = UI_FindChildFrame(modal, "DialogWar3");
    if (!require_not_null(dialog)) {
        mi = saved;
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
        mi = saved;
        return;
    }
    if (!require_not_null(icon)) {
        mi = saved;
        return;
    }
    if (!require_not_null(ok_backdrop)) {
        mi = saved;
        return;
    }
    if (!require_not_null(no_backdrop)) {
        mi = saved;
        return;
    }
    if (!require_not_null(yes_backdrop)) {
        mi = saved;
        return;
    }
    if (!require_not_null(no_button)) {
        mi = saved;
        return;
    }
    if (!require_not_null(yes_button)) {
        mi = saved;
        return;
    }

    T_STREQ(message->Text, "Do you want to Quit?");
    T_ASSERT(icon->Backdrop.Background != 0);
    T_ASSERT(ok_backdrop->hidden);
    T_ASSERT(!no_backdrop->hidden);
    T_ASSERT(!yes_backdrop->hidden);
    T_STREQ(no_button->OnClick, "menu_main");
    T_STREQ(yes_button->OnClick, "quit");

    Cmd_ExecuteString(no_button->OnClick);
    T_ASSERT(modal->hidden);
    T_ASSERT(dialog->hidden);

    Cmd_ExecuteString(yes_button->OnClick);
    T_STREQ(forwarded_command, "quit");

    mi = saved;
}

TEST(menu_fdf, main_menu_realm_select_uses_realm_panel_anim) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\UI\\EscMenuTemplates.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\DialogWar3.fdf",
        "UI\\FrameDef\\Glue\\MainMenu.fdf",
    };
    menuImport_t saved = mi;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));
    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.GetRenderer = test_get_renderer;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;
    UI_ResetGlueSceneModels();

    T_ASSERT(mainMenuScreen.load());
    mainMenuScreen.init();
    UI_GotoGluePanel(mainMenuScreen.glue, NULL, NULL);
    M_SetActive(true);
    M_Refresh(M_Time() + 1000);
    Cmd_ExecuteString("menu_realm_select");
    captured_stand_sprites = 0;
    captured_realm_panel_sprites = 0;
    captured_sprite_calls = 0;
    captured_death_sprites = 0;
    UI_DrawGlueScene();
    T_EQ(captured_death_sprites, 2);
    M_SetActive(true);
    M_Refresh(M_Time() + 667);
    M_Refresh(M_Time() + 1000);
    captured_stand_sprites = 0;
    captured_realm_panel_sprites = 0;
    UI_DrawGlueScene();
    T_EQ(captured_stand_sprites, 0);
    T_EQ(captured_realm_panel_sprites, 2);
    mi = saved;
}

TEST(menu_fdf, glue_sprite_layers_follow_widescreen_edges) {
    menuImport_t saved = mi;
    RECT centered;

    reset_ui_state();
    load_ui_files((LPCSTR[]){
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\MainMenu.fdf",
    }, 3);
    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.GetRenderer = test_get_renderer;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;
    test_window_size = MAKE(size2_t, 1280, 720);
    UI_ResetGlueSceneModels();
    captured_stand_sprites = 0;
    captured_realm_panel_sprites = 0;
    memset(captured_sprite_x, 0, sizeof(captured_sprite_x));

    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_MAIN_MENU }, NULL, NULL);
    UI_DrawGlueScene();
    T_EQ(captured_sprite_calls, 2);
    T_FEQ(captured_sprite_x[0], 0.0f, 0.0001f);
    T_FEQ(captured_sprite_x[1], 0.266666f, 0.0001f);
    UI_DrawFrames((LPCFRAMEDEF[]){ UI_FindFrame("MainMenuFrame") }, 1);
    T_FEQ(UI_GetSceneRect().x, 0.0f, 0.0001f);
    T_FEQ(UI_GetSceneRect().w, 1.066666f, 0.0001f);
    centered = UI_GetCenteredSceneRect();
    T_FEQ(centered.x, 0.133333f, 0.0001f);
    T_FEQ(centered.w, 0.8f, 0.0001f);
    test_window_size = MAKE(size2_t, 1000, 750);

    mi = saved;
}

TEST(menu_fdf, initial_glue_panel_finishes_birth_before_opening_screen) {
    menuImport_t saved = mi;

    reset_ui_state();
    memset(&mi, 0, sizeof(mi));
    mi.GetRenderer = test_get_renderer;
    UI_ResetGlueSceneModels();

    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_MAIN_MENU }, NULL, test_glue_changed);
    UI_DrawGlueScene();
    T_EQ(captured_birth_sprites, 2);
    T_EQ(captured_death_sprites, 0);
    T_EQ(captured_glue_changes, 0);
    M_SetActive(true);
    M_Refresh(M_Time() + 1000);
    T_EQ(captured_glue_changes, 1);

    mi = saved;
}

TEST(menu_fdf, options_glue_panel_tab_state_during_birth_and_idle) {
    menuImport_t saved = mi;

    reset_ui_state();
    memset(&mi, 0, sizeof(mi));
    mi.GetRenderer = test_get_renderer;
    UI_ResetGlueSceneModels();

    /* Navigate to Options panel, activating tab 1 (the "alternate" state). */
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_MAIN_MENU }, NULL, NULL);
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_OPTIONS, .tab = 1 }, NULL, NULL);

    /* The left enters its alternate pose while the right enters the base panel. */
    UI_DrawGlueScene();
    T_STREQ(captured_sprite_anim[0], "Options Morph@0.0000");
    T_STREQ(captured_sprite_anim[1], "Options Birth@0.0000");
    T_ASSERT(!UI_GlueSideReady(UI_GLUE_LEFT));
    T_ASSERT(!UI_GlueSideReady(UI_GLUE_RIGHT));
    M_SetActive(true);
    M_Refresh(M_Time() + 500);
    T_ASSERT(UI_GlueIsTransitioning());
    M_Refresh(M_Time() + 500);

    /* Now IDLE with active_tab=1: left shows Stand Alternate, right shows Stand. */
    captured_sprite_calls = 0;
    UI_DrawGlueScene();
    T_STREQ(captured_sprite_anim[0], "Options Stand Alternate");
    T_STREQ(captured_sprite_anim[1], "Options Stand");
    T_ASSERT(!UI_GlueIsTransitioning());

    /* Leave the alternate left pose through its authored Morph Alternate. */
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_MAIN_MENU }, NULL, NULL);
    captured_sprite_calls = 0;
    UI_DrawGlueScene();
    T_STREQ(captured_sprite_anim[0], "Options Morph Alternate@0.0000");
    T_STREQ(captured_sprite_anim[1], "Options Death@0.0000");

    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, tab_morph_plays_when_switching_tabs_at_idle) {
    menuImport_t saved = mi;

    reset_ui_state();
    memset(&mi, 0, sizeof(mi));
    mi.GetRenderer = test_get_renderer;
    UI_ResetGlueSceneModels();

    /* Arrive at SinglePlayer panel with default tab, then wait for IDLE. */
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_SINGLE_PLAYER }, NULL, NULL);
    M_SetActive(true);
    M_Refresh(M_Time() + 1000);  /* let Birth finish */

    /* Switch to tab 1 (SinglePlayerSkirmish) while IDLE. */
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_SINGLE_PLAYER, .tab = 1 }, NULL, NULL);
    captured_sprite_calls = 0;
    UI_DrawGlueScene();
    /* Left plays SinglePlayerSkirmish Morph, right stays at SinglePlayer Stand. */
    T_STREQ(captured_sprite_anim[0], "SinglePlayerSkirmish Morph@0.0000");
    T_STREQ(captured_sprite_anim[1], "SinglePlayer Stand");

    /* Advance past Morph duration (1000 ms). */
    M_Refresh(M_Time() + 1000);
    captured_sprite_calls = 0;
    UI_DrawGlueScene();
    T_STREQ(captured_sprite_anim[0], "SinglePlayerSkirmish Stand");
    T_STREQ(captured_sprite_anim[1], "SinglePlayer Stand");

    /* Switch back to tab 0. */
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_SINGLE_PLAYER }, NULL, NULL);
    captured_sprite_calls = 0;
    UI_DrawGlueScene();
    /* Left plays SinglePlayerSkirmish Morph Alternate (667 ms), right stays at Stand. */
    T_STREQ(captured_sprite_anim[0], "SinglePlayerSkirmish Morph Alternate@0.0000");
    T_STREQ(captured_sprite_anim[1], "SinglePlayer Stand");

    UI_ResetGlueSceneModels();
    mi = saved;
}

/* Screen commands and the scene share the production owner, but use fixture I/O. */
static void test_glue_setup(void) {
    mi.Printf = test_ui_printf;
    test_command_imports();
    Cmd_ExecuteString("menu_ingame");
    reset_ui_state();
    mi.FS_ReadFile = test_fs_read_file;
    mi.FS_FreeFile = test_fs_free_file;
    mi.Cvar_String = test_cvar_string;
    mi.Cmd_ExecuteText = test_cmd_execute_text;
    mi.UserPath = test_user_path;
    UI_ResetGlueSceneModels();
}

static void test_glue_tick(DWORD msec) {
    captured_sprite_calls = 0;
    M_Refresh(M_Time() + msec);
}

/* A click must finish its event stack before screen lifecycle callbacks run. */
TEST(menu_fdf, console_queue_defers_clicks_and_separates_commands) {
    menuImport_t saved = mi;
    test_glue_setup();
    Cmd_ExecuteString("menu_main");
    test_glue_tick(1000);
    LPFRAMEDEF button = UI_FindFrame("OptionsButton");
    T_NOT_NULL(button);
    mi.Cmd_ExecuteText = Cbuf_AddText;
    mi.Cvar_Set = test_cvar_set;
    button->event_handler(button, MENU_MOUSE_UP, 0, 0, 1);
    T_ASSERT(UI_GetCurrentScreen() == &mainMenuScreen);
    T_ASSERT(!M_IsTransitioning());
    Cbuf_Execute();
    T_ASSERT(M_IsTransitioning());
    test_glue_tick(667);
    test_glue_tick(1000);
    T_ASSERT(UI_GetCurrentScreen() == &optionsMenuScreen);
    UI_QueueCommand("menu_video_mode 5");
    UI_QueueCommand("menu_single_player_difficulty 2");
    Cbuf_Execute();
    T_STREQ(Cvar_String("vid_mode", ""), "5");
    T_STREQ(captured_cvar_name, "wc3_campaign_difficulty");
    T_STREQ(captured_cvar_value, "2");
    Cmd_ExecuteString("menu_ingame");
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, text_input_respects_menu_activity_and_transition_lock) {
    menuImport_t saved = mi;
    test_glue_setup();
    Cmd_ExecuteString("menu_video");
    test_glue_tick(1000);
    LPFRAMEDEF edit = UI_FindFrame("GamePortEditBox");
    T_NOT_NULL(edit);
    UI_SetEditValue(edit, "1234");
    UI_EditboxFocusOnHit(edit);
    M_SetActive(false);
    M_TextInput("5");
    T_STREQ(UI_EditValue(edit), "1234");
    M_SetActive(true);
    M_TextInput("5");
    T_STREQ(UI_EditValue(edit), "12345");
    Cmd_ExecuteString("menu_main");
    T_ASSERT(M_IsTransitioning());
    M_TextInput("6");
    menuExport_t api = M_GetAPI(mi);
    api.KeyEvent(8, true, M_Time());
    T_STREQ(UI_EditValue(edit), "12345");
    test_glue_tick(667);
    test_glue_tick(1000);
    T_ASSERT(!M_IsTransitioning());
    Cmd_ExecuteString("menu_ingame");
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, console_commands_reject_malformed_numeric_arguments) {
    menuImport_t saved = mi;
    test_glue_setup();
    mi.Cvar_Set = test_cvar_set;
    const char *bad[] = {
        "menu_video_mode", "menu_video_mode -1", "menu_video_mode 4294967296",
        "menu_video_mode 999999999999999999999999999999999", "menu_video_mode 1junk",
        "menu_video_mode 1 2", "menu_single_player_difficulty -1", "menu_single_player_difficulty 2junk",
        "menu_game_setup_slot_type 1", "menu_game_setup_slot_race 1 2 3",
    };
    FOR_LOOP(i, sizeof(bad) / sizeof(bad[0])) {
        captured_command[0] = captured_cvar_name[0] = forwarded_command[0] = 0;
        Cmd_ExecuteString(bad[i]);
        T_STREQ(captured_command, "");
        T_STREQ(captured_cvar_name, "");
        T_STREQ(forwarded_command, "");
    }
    Cmd_ExecuteString(" \tmenu_video_mode \"5\"  // console comment");
    T_STREQ(captured_command, "seta vid_native 0\nseta vid_mode 5\n");
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, console_screen_commands_and_campaign_shortcuts) {
    menuImport_t saved = mi;
    test_glue_setup();
    mi.Cvar_Set = test_cvar_set;
    const struct { LPCSTR cmd; uiScreen_t *screen; } cases[] = {
        { "menu_main", &mainMenuScreen },
        { "menu_quit", &mainMenuScreen },
        { "menu_disconnected", &mainMenuScreen },
        { "menu_realm_select", &mainMenuScreen },
        { "menu_credits", &creditsMenuScreen },
        { "menu_game", &singlePlayerMenuScreen },
        { "menu_single_player_skirmish", &lanJoinScreen },
        { "menu_multiplayer", &lanJoinScreen },
        { "menu_startserver", &lanJoinScreen },
        { "menu_joinserver", &lanJoinScreen },
        { "menu_options", &optionsMenuScreen },
        { "menu_video", &optionsMenuScreen },
        { "menu_options_sound", &optionsMenuScreen },
        { "menu_options_gameplay", &optionsMenuScreen },
        { "menu_keys", &optionsMenuScreen },
        { "menu_options_apply", &mainMenuScreen },
    };
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        forwarded_command[0] = 0;
        Cmd_ExecuteString(cases[i].cmd);
        test_glue_tick(667);
        test_glue_tick(1000);
        T_ASSERT(UI_GetCurrentScreen() == cases[i].screen);
        T_ASSERT(!M_IsTransitioning());
        T_STREQ(forwarded_command, "");
    }
    Cmd_ExecuteString("menu_loadgame");
    T_STREQ(captured_command, "load quick\n");
    Cmd_ExecuteString("menu_savegame");
    T_STREQ(captured_command, "save quick\n");
    Cmd_ExecuteString("menu_game");
    test_glue_tick(667);
    test_glue_tick(1000);
    Cmd_ExecuteString("menu_single_player_campaign");
    test_glue_tick(667);
    const struct { LPCSTR cmd, title; } races[] = {
        { "menu_single_player_campaign_human", "The Scourge of Lordaeron" },
        { "menu_single_player_campaign_undead", "Path of the Damned" },
        { "menu_single_player_campaign_orc", "The Invasion of Kalimdor" },
        { "menu_single_player_campaign_night_elf", "Eternity's End" },
        { "menu_single_player_campaign_tutorial", "Exodus of the Horde" },
    };
    FOR_LOOP(i, sizeof(races) / sizeof(races[0])) {
        Cmd_ExecuteString(races[i].cmd);
        T_STREQ(UI_FindFrame("MissionName")->Text, races[i].title);
        T_ASSERT(!UI_FindFrame("MissionSelectFrame")->hidden);
        Cmd_ExecuteString("menu_single_player_campaign_back");
        T_ASSERT(!UI_FindFrame("CampaignSelectFrame")->hidden);
    }
    Cmd_ExecuteString("menu_ingame");
    UI_ResetGlueSceneModels();
    mi = saved;
}

/* Exercise real screen commands, moving contents and input locks at the handoff edges. */
TEST(menu_fdf, glue_options_tabs_keep_right_controls_and_defer_left_content) {
    menuImport_t saved = mi;
    test_glue_setup();
    Cmd_ExecuteString("menu_options");
    test_glue_tick(1000);
    LPFRAMEDEF old = UI_FindFrame("GameplayPanel"), next = UI_FindFrame("VideoPanel");
    LPFRAMEDEF button = UI_FindFrame("VideoButton"), edit = UI_FindFrame("GamePortEditBox");
    T_NOT_NULL(old); T_NOT_NULL(next); T_NOT_NULL(button); T_NOT_NULL(edit);
    T_ASSERT(!old->hidden && next->hidden);
    T_FEQ(UI_ScreenFrameOffset(old), 0, 0.00001f);
    T_FEQ(UI_ScreenFrameOffset(button), 0, 0.00001f);
    Cmd_ExecuteString("menu_video");
    test_glue_tick(0);
    T_STREQ(captured_sprite_anim[0], "Options Morph Alternate@0.0000");
    T_STREQ(captured_sprite_anim[1], "Options Stand");
    T_ASSERT(!old->hidden && next->hidden); // Selection still belongs to the outgoing tab.
    T_FEQ(UI_ScreenFrameOffset(old), 0, 0.00001f);
    T_FEQ(UI_ScreenFrameOffset(edit), UI_ScreenFrameOffset(old), 0.00001f);
    T_FEQ(UI_ScreenFrameOffset(button), 0, 0.00001f);
    T_ASSERT(M_IsTransitioning());
    test_glue_tick(400);
    T_ASSERT(UI_ScreenFrameOffset(old) < -0.07f);
    T_FEQ(UI_ScreenFrameOffset(button), 0, 0.00001f);
    test_glue_tick(266);
    T_ASSERT(!old->hidden && next->hidden);
    test_glue_tick(1);
    T_ASSERT(old->hidden && !next->hidden);
    T_STREQ(captured_sprite_anim[0], "Options Morph@0.0000");
    T_ASSERT(UI_ScreenFrameOffset(next) < -0.59f);
    test_glue_tick(700);
    T_ASSERT(UI_ScreenFrameOffset(next) > 0.015f); // Native landing overshoot.
    T_FEQ(UI_ScreenFrameOffset(button), 0, 0.00001f);
    test_glue_tick(299);
    T_FEQ(UI_ScreenFrameOffset(next), 0, 0.001f);
    test_glue_tick(1);
    T_FEQ(UI_ScreenFrameOffset(next), 0, 0.00001f);
    T_ASSERT(!M_IsTransitioning());
    Cmd_ExecuteString("menu_video"); // Reselecting the same page must not animate.
    T_ASSERT(!M_IsTransitioning());
    Cmd_ExecuteString("menu_options_sound");
    test_glue_tick(300);
    Cmd_ExecuteString("menu_options_gameplay"); // Latest page wins without restarting the exit.
    test_glue_tick(367);
    T_ASSERT(!old->hidden && next->hidden);
    test_glue_tick(1000);
    T_ASSERT(!UI_FindFrame("GameplayPanel")->hidden);
    T_ASSERT(UI_FindFrame("SoundPanel")->hidden);
    T_ASSERT(!M_IsTransitioning());
    Cmd_ExecuteString("menu_ingame");
    mi = saved;
}

TEST(menu_fdf, glue_lan_content_never_draws_on_outgoing_main_panels) {
    menuImport_t saved = mi;
    test_glue_setup();
    Cmd_ExecuteString("menu_main");
    test_glue_tick(1000);
    Cmd_ExecuteString("menu_multiplayer");
    T_ASSERT(UI_GetCurrentScreen() == &mainMenuScreen);
    captured_text_draws = 0;
    test_glue_tick(0);
    T_FEQ(UI_ScreenFrameOffset(UI_FindFrame("OptionsButton")), 0, 0.00001f);
    T_STREQ(captured_sprite_anim[0], "MainMenu Death@0.0000");
    test_glue_tick(666);
    T_ASSERT(UI_GetCurrentScreen() == &mainMenuScreen);
    captured_text_draws = 0;
    test_glue_tick(1);
    T_ASSERT(UI_GetCurrentScreen() == &lanJoinScreen);
    T_ASSERT(captured_text_draws > 0);
    T_ASSERT(UI_ScreenFrameOffset(UI_FindFrame("GameListPanel")) < -0.59f);
    T_ASSERT(UI_ScreenFrameOffset(UI_FindFrame("JoinButton")) < -0.59f);
    test_glue_tick(999);
    T_ASSERT(captured_text_draws > 0);
    test_glue_tick(1);
    T_ASSERT(captured_text_draws > 0);
    T_FEQ(UI_ScreenFrameOffset(UI_FindFrame("GameListPanel")), 0, 0.00001f);
    T_FEQ(UI_ScreenFrameOffset(UI_FindFrame("JoinButton")), 0, 0.00001f);
    Cmd_ExecuteString("menu_startserver");
    T_ASSERT(UI_ScreenFrameOffset(UI_FindFrame("GameListPanel")) < -0.59f);
    T_ASSERT(UI_GlueSideReady(UI_GLUE_RIGHT));
    T_ASSERT(UI_ScreenFrameOffset(UI_FindFrame("CreateButton")) < -0.59f);
    test_glue_tick(1000);
    T_STREQ(captured_sprite_anim[0], "BattlenetCustomCreate Stand");
    Cmd_ExecuteString("menu_ingame");
    T_ASSERT(!M_IsTransitioning());
    mi = saved;
}

/* Cross-side anchors remain in FDF space; nested content receives motion once. */
TEST(menu_fdf, glue_motion_translates_draw_rects_without_changing_anchors) {
    menuImport_t saved = mi;
    test_glue_setup();
    Cmd_ExecuteString("menu_options");
    test_glue_tick(1000);
    parse_fdf("motion.fdf",
        "Frame \"FRAME\" \"MotionRoot\" { Width 0.8, Height 0.6,"
        " Frame \"TEXT\" \"FixedLabel\" { Width 0.1, Height 0.02,"
        "  SetPoint TOPLEFT, \"MotionRoot\", TOPLEFT, 0.6, -0.4,"
        "  FrameFont \"MasterFont\", 0.013, \"\", Text \"Fixed\", }"
        " Frame \"FRAME\" \"GameplayPanel\" { Width 0.3, Height 0.4,"
        "  SetPoint TOPLEFT, \"FixedLabel\", TOPLEFT, -0.5, 0,"
        "  Frame \"TEXT\" \"MovingLabel\" { Width 0.1, Height 0.02,"
        "   SetPoint TOPLEFT, \"GameplayPanel\", TOPLEFT, 0, 0,"
        "   FrameFont \"MasterFont\", 0.013, \"\", Text \"Moving\", }"
        "  Frame \"TEXT\" \"SiblingLabel\" { Width 0.1, Height 0.02,"
        "   SetPoint TOPLEFT, \"MovingLabel\", BOTTOMLEFT, 0, -0.01,"
        "   FrameFont \"MasterFont\", 0.013, \"\", Text \"Sibling\", }"
        " } }");
    LPFRAMEDEF root = UI_FindFrame("MotionRoot");
    T_NOT_NULL(root);
    Cmd_ExecuteString("menu_video");
    test_glue_tick(400);
    captured_text_draws = 0;
    UI_DrawFrame(root);
    T_EQ(captured_text_draws, 3);
    FLOAT offset = UI_GlueSideOffset(UI_GLUE_LEFT);
    T_ASSERT(offset < -0.07f);
    T_FEQ(captured_text_rects[0].y, 0.4f, 0.00001f);
    T_FEQ(captured_text_rects[1].y, 0.4f + offset, 0.00001f);
    T_FEQ(captured_text_rects[2].y, 0.43f + offset, 0.00001f);
    RECT scene = {0, 0, .8f, .6f};
    captured_text_draws = 0;
    UI_DrawFrameInScene(root, &scene);
    T_FEQ(captured_text_rects[1].y, 0.4f, 0.00001f);
    Cmd_ExecuteString("menu_ingame");
    mi = saved;
}

TEST(menu_fdf, glue_same_panel_screen_navigation_completes) {
    menuImport_t saved = mi;
    test_glue_setup();
    Cmd_ExecuteString("menu_main");
    test_glue_tick(1000);
    Cmd_ExecuteString("menu_credits");
    T_ASSERT(UI_GetCurrentScreen() == &creditsMenuScreen);
    Cmd_ExecuteString("menu_main");
    T_ASSERT(UI_GetCurrentScreen() == &mainMenuScreen);
    T_ASSERT(!M_IsTransitioning());

    Cmd_ExecuteString("menu_game");
    test_glue_tick(667);
    test_glue_tick(1000);
    Cmd_ExecuteString("menu_single_player_skirmish");
    test_glue_tick(1000);
    T_ASSERT(UI_GetCurrentScreen() == &lanJoinScreen);
    Cmd_ExecuteString("menu_game");
    T_ASSERT(UI_GetCurrentScreen() == &lanJoinScreen);
    T_ASSERT(M_IsTransitioning());
    test_glue_tick(667);
    T_ASSERT(UI_GetCurrentScreen() == &singlePlayerMenuScreen);
    T_ASSERT(!M_IsTransitioning());
    T_STREQ(captured_sprite_anim[0], "SinglePlayer Stand");
    Cmd_ExecuteString("menu_ingame");
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, glue_create_destination_and_live_lan_mode_changes) {
    menuImport_t saved = mi;
    test_glue_setup();
    Cmd_ExecuteString("menu_main");
    test_glue_tick(1000);
    Cmd_ExecuteString("menu_startserver");
    test_glue_tick(667);
    test_glue_tick(1000);
    T_STREQ(captured_sprite_anim[0], "BattlenetCustomCreate Stand");
    T_STREQ(captured_sprite_anim[1], "BattlenetCustom Stand");
    Cmd_ExecuteString("menu_multiplayer");
    test_glue_tick(0);
    T_STREQ(captured_sprite_anim[0], "BattlenetCustomCreate Death@0.0000");
    test_glue_tick(667);
    T_STREQ(captured_sprite_anim[0], "BattlenetCustom Stand");
    Cmd_ExecuteString("menu_startserver");
    test_glue_tick(0);
    T_STREQ(captured_sprite_anim[0], "BattlenetCustomCreate Birth@0.0000");
    test_glue_tick(1000);
    T_STREQ(captured_sprite_anim[0], "BattlenetCustomCreate Stand");
    Cmd_ExecuteString("menu_ingame");
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, glue_morph_requests_queue_without_restarting) {
    menuImport_t saved = mi;
    GLUEDEST base = { .panel = UI_GLUE_SINGLE_PLAYER }, tab = { .panel = UI_GLUE_SINGLE_PLAYER, .tab = 1 };
    test_glue_setup();
    UI_GotoGluePanel(base, NULL, NULL);
    test_glue_tick(1000);
    UI_GotoGluePanel(tab, NULL, NULL);
    test_glue_tick(500);
    UI_GotoGluePanel(tab, NULL, NULL);
    test_glue_tick(0);
    T_STREQ(captured_sprite_anim[0], "SinglePlayerSkirmish Morph@0.5000");
    UI_GotoGluePanel(base, NULL, NULL);
    test_glue_tick(0);
    T_STREQ(captured_sprite_anim[0], "SinglePlayerSkirmish Morph@0.5000");
    test_glue_tick(500);
    T_STREQ(captured_sprite_anim[0], "SinglePlayerSkirmish Morph Alternate@0.0000");
    test_glue_tick(667);
    T_STREQ(captured_sprite_anim[0], "SinglePlayer Stand");

    UI_GotoGluePanel(tab, NULL, NULL);
    test_glue_tick(500);
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_OPTIONS, .tab = 1 }, NULL, NULL);
    test_glue_tick(500); // Finish the in-flight skirmish Morph before leaving.
    test_glue_tick(667);
    test_glue_tick(1000);
    T_STREQ(captured_sprite_anim[0], "Options Stand Alternate");
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, glue_startup_screen_override_completes_birth) {
    menuImport_t saved = mi;
    test_glue_setup();
    Cmd_ExecuteString("menu_main");
    test_glue_tick(250);
    Cmd_ExecuteString("menu_options");
    T_ASSERT(UI_GetCurrentScreen() == &optionsMenuScreen);
    T_ASSERT(M_IsTransitioning());
    test_glue_tick(999);
    T_ASSERT(M_IsTransitioning());
    test_glue_tick(1);
    T_ASSERT(!M_IsTransitioning());
    T_STREQ(captured_sprite_anim[0], "Options Stand Alternate");
    Cmd_ExecuteString("menu_ingame");
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, glue_nondefault_tabs_exit_before_entering_next) {
    menuImport_t saved = mi;
    test_glue_setup();
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_BATTLENET_CUSTOM, .tab = 1 }, NULL, NULL);
    test_glue_tick(1000);
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_BATTLENET_CUSTOM, .tab = 2 }, NULL, NULL);
    test_glue_tick(0);
    T_STREQ(captured_sprite_anim[0], "BattlenetCustomCreate Death@0.0000");
    test_glue_tick(667);
    T_STREQ(captured_sprite_anim[0], "BattlenetAdvancedOptions Morph@0.0000");
    test_glue_tick(1000);
    T_STREQ(captured_sprite_anim[0], "BattlenetAdvancedOptions Morph@1.0000");
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, glue_retarget_and_close_deliver_only_latest_completion) {
    menuImport_t saved = mi;
    test_glue_setup();
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_MAIN_MENU }, NULL, test_glue_changed);
    test_glue_tick(500);
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_OPTIONS, .tab = 1 }, NULL, test_glue_changed);
    test_glue_tick(1000);
    T_EQ(captured_glue_changes, 1);
    T_STREQ(captured_sprite_anim[0], "Options Stand Alternate");
    UI_GotoGluePanel((GLUEDEST){ .panel = UI_GLUE_MAIN_MENU }, test_glue_changed, test_glue_changed);
    test_glue_tick(300);
    UI_CloseGluePanel(test_glue_changed);
    test_glue_tick(367);
    T_EQ(captured_glue_changes, 2);
    test_glue_tick(1000);
    T_EQ(captured_glue_changes, 2);
    UI_ResetGlueSceneModels();
    mi = saved;
}

/* The fixture contains only the original MDLX SEQS chunk, not rendering data. */
static void test_glue_sequence_exists(LPCSTR anim) {
    void *data = NULL;
    int size = test_fs_read_file("UI\\Glues\\SpriteLayers\\TopLeftPanel.mdx", &data);
    char name[80];
    BOOL found = false;
    snprintf(name, sizeof(name), "%s", anim);
    char *ratio = strchr(name, '@');
    if (ratio) *ratio = 0;
    T_ASSERT(size >= 12);
    if (size >= 12) {
        const DWORD *head = data;
        T_EQ(head[0], MAKEFOURCC('M', 'D', 'L', 'X'));
        T_EQ(head[1], MAKEFOURCC('S', 'E', 'Q', 'S'));
        T_EQ(head[2], size - 12);
        const mdxSequence_t *seqs = (const mdxSequence_t *)((const BYTE *)data + 12);
        FOR_LOOP(i, head[2] / sizeof(*seqs))
            if (!strcmp(name, seqs[i].name)) found = true;
    }
    if (!found) fprintf(stderr, "Test: unauthored glue sequence '%s'\n", name);
    T_ASSERT(found);
    free(data);
}

TEST(menu_fdf, glue_all_tab_sequences_exist_in_authored_model) {
    menuImport_t saved = mi;
    const GLUEDEST tabs[] = {
        { .panel = UI_GLUE_SINGLE_PLAYER, .tab = 1 },
        { .panel = UI_GLUE_OPTIONS, .tab = 1 },
        { .panel = UI_GLUE_MULTIPLAYER_PRE_GAME_CHAT, .tab = 1 },
        { .panel = UI_GLUE_BATTLENET_CUSTOM, .tab = 1 },
        { .panel = UI_GLUE_BATTLENET_CUSTOM, .tab = 2 },
    };
    test_glue_setup();
    FOR_LOOP(i, sizeof(tabs) / sizeof(tabs[0])) {
        GLUEDEST base = { .panel = tabs[i].panel };
        UI_ResetGlueSceneModels();
        UI_GotoGluePanel(base, NULL, NULL);
        test_glue_tick(1000);
        UI_GotoGluePanel(tabs[i], NULL, NULL);
        test_glue_tick(500);
        test_glue_sequence_exists(captured_sprite_anim[0]);
        test_glue_tick(500);
        test_glue_sequence_exists(captured_sprite_anim[0]);
        UI_GotoGluePanel(base, NULL, NULL);
        test_glue_tick(300);
        test_glue_sequence_exists(captured_sprite_anim[0]);
    }
    UI_ResetGlueSceneModels();
    mi = saved;
}

TEST(menu_fdf, main_menu_edition_button_defers_restart_after_death_frame) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\UI\\EscMenuTemplates.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\DialogWar3.fdf",
        "UI\\FrameDef\\Glue\\MainMenu.fdf",
    };
    menuImport_t saved = mi;
    LPFRAMEDEF root;
    LPFRAMEDEF edition;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));
    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.GetRenderer = test_get_renderer;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;
    mi.FS_ReadFile = test_fs_read_file;
    mi.FS_FreeFile = test_fs_free_file;
    mi.Cmd_ExecuteText = test_cmd_execute_text;
    mi.Cvar_String = test_cvar_string;
    mi.Cvar_Set = test_cvar_set;
    test_fs_expansion = false;
    hide_expansion_campaign_file = false;
    captured_command[0] = '\0';
    captured_cvar_name[0] = '\0';
    captured_cvar_value[0] = '\0';
    captured_death_sprites = 0;

    T_ASSERT(mainMenuScreen.load());
    mainMenuScreen.init();
    UI_ResetGlueSceneModels();
    UI_GotoGluePanel(mainMenuScreen.glue, NULL, NULL);
    test_glue_tick(1000);
    root = UI_FindFrame("MainMenuFrame");
    edition = root ? UI_FindChildFrame(root, "EditionButton") : NULL;
    if (!require_not_null(root) || !require_not_null(edition)) {
        mi = saved;
        return;
    }
    T_STREQ(edition->OnClick, "menu_edition");

    Cmd_ExecuteString(edition->OnClick);
    T_STREQ(captured_command, "");
    UI_DrawGlueScene();
    T_EQ(captured_death_sprites, 2);
    T_ASSERT(!test_fs_expansion);
    T_STREQ(captured_command, "");
    Cmd_ExecuteString("menu_edition");
    T_STREQ(captured_command, "");
    M_SetActive(true);
    M_Refresh(M_Time() + 667);
    T_ASSERT(test_fs_expansion);
    T_STREQ(captured_cvar_name, "fs_expansion");
    T_STREQ(captured_cvar_value, "1");
    T_STREQ(captured_command, "menu_restart\n");
    mainMenuScreen.shutdown();
    mi = saved;
}

TEST(menu_fdf, main_menu_edition_button_rolls_back_when_tft_data_is_missing) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\UI\\EscMenuTemplates.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\DialogWar3.fdf",
        "UI\\FrameDef\\Glue\\MainMenu.fdf",
    };
    menuImport_t saved = mi;
    LPFRAMEDEF root;

    load_ui_files(files, sizeof(files) / sizeof(files[0]));
    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.GetRenderer = test_get_renderer;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;
    mi.FS_ReadFile = test_fs_read_file;
    mi.FS_FreeFile = test_fs_free_file;
    mi.Cmd_ExecuteText = test_cmd_execute_text;
    mi.Cvar_String = test_cvar_string;
    mi.Cvar_Set = test_cvar_set;
    test_fs_expansion = false;
    hide_expansion_campaign_file = true;
    captured_command[0] = '\0';
    captured_printf[0] = '\0';

    T_ASSERT(mainMenuScreen.load());
    mainMenuScreen.init();
    UI_ResetGlueSceneModels();
    UI_GotoGluePanel(mainMenuScreen.glue, NULL, NULL);
    test_glue_tick(1000);
    root = UI_FindFrame("MainMenuFrame");
    if (!require_not_null(root)) {
        hide_expansion_campaign_file = false;
        mi = saved;
        return;
    }

    Cmd_ExecuteString("menu_edition");
    M_SetActive(true);
    M_Refresh(M_Time() + 667);
    T_ASSERT(!test_fs_expansion);
    T_STREQ(captured_command, "menu_restart\n");
    T_ASSERT(strstr(captured_printf, "The Frozen Throne data is unavailable.") != NULL);

    hide_expansion_campaign_file = false;
    mainMenuScreen.shutdown();
    mi = saved;
}

static void test_single_player_campaign_profile(BOOL tft) {
    LPCSTR files[] = {
        "UI\\FrameDef\\GlobalStrings.fdf",
        "UI\\FrameDef\\Glue\\StandardTemplates.fdf",
        "UI\\FrameDef\\Glue\\SinglePlayerMenu.fdf",
        "UI\\FrameDef\\Glue\\CampaignMenu.fdf",
        "UI\\FrameDef\\Glue\\MapListBox.fdf",
    };
    menuImport_t saved = mi;
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
    test_campaign_visibility = NULL;
    remove(test_campaign_progress_path);
    load_ui_files(files, sizeof(files) / sizeof(files[0]));

    memset(&mi, 0, sizeof(mi));
    mi.Printf = test_ui_printf;
    test_command_imports();
    mi.GetRenderer = test_get_renderer;
    mi.Cmd_ExecuteText = test_cmd_execute_text;
    mi.Cvar_String = test_cvar_string;
    mi.Cvar_Set = test_cvar_set;
    mi.FS_ReadFile = test_fs_read_file;
    mi.FS_FreeFile = test_fs_free_file;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;
    mi.PlayMovie = test_play_movie;
    mi.UserPath = test_user_path;

    if (!singlePlayerMenuScreen.load()) {
        T_ASSERT(false);
        mi = saved;
        return;
    }
    singlePlayerMenuScreen.init();

    root = UI_FindFrame("SinglePlayerMenu");
    campaign_button = UI_FindFrame("CampaignButton");
    skirmish_button = UI_FindFrame("SkirmishButton");
    cancel_button = UI_FindFrame("CancelButton");
    back_button = UI_FindFrame("BackButton");

    if (!require_not_null(root)) {
        mi = saved;
        return;
    }
    if (!require_not_null(campaign_button)) {
        mi = saved;
        return;
    }
    if (!require_not_null(skirmish_button)) {
        mi = saved;
        return;
    }
    if (!require_not_null(cancel_button)) {
        mi = saved;
        return;
    }
    if (!require_not_null(back_button)) { mi = saved; return; }
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
        mi = saved;
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
    Cmd_ExecuteString(tft ? "menu_single_player_campaign_select 1" : "menu_single_player_campaign_select 0");
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
        mi = saved;
        return;
    }
    T_ASSERT(campaign_select_frame->hidden);
    T_ASSERT(!mission_select_frame->hidden);
    T_ASSERT(!mission_list_box->hidden);
    T_STREQ(mission_name->Text,
            tft ? "Curse of the Blood Elves" : "The Scourge of Lordaeron");
    T_STREQ(mission_name_header->Text,
            tft ? "Alliance Campaign" : "Human Campaign");
#ifdef BZ_FFMPEG
    T_EQ((int)mission_list_box->MapListControl.State->count, 6);
    T_STREQ(mission_list_box->MapListControl.State->items[0].name,
            tft
                ? "Cinematic: Introduction: Alliance Introduction"
                : "Cinematic: Introduction: Human Introduction");
    T_STREQ(mission_list_box->MapListControl.State->items[0].path,
            tft ? "Movies\\HumanXIntro.mpq" : "Movies\\HumanIntro.mpq");
    T_STREQ(mission_list_box->MapListControl.State->items[1].name,
            tft
                ? "Cinematic: Cinematic: Alliance Opening"
                : "Cinematic: Cinematic: Human Opening");
    T_STREQ(mission_list_box->MapListControl.State->items[2].name,
            tft ? "Chapter One: Misconceptions" : "The Defense of Strahnbrad");
    T_STREQ(mission_list_box->MapListControl.State->items[3].name,
            tft ? "Chapter Two: A Dark Covenant" : "Blackrock & Roll");
    T_STREQ(mission_list_box->MapListControl.State->items[5].name,
            tft
                ? "Cinematic: Cinematic: Alliance Ending"
                : "Cinematic: Cinematic: Human Ending");
    captured_movie_path[0] = '\0';
    captured_command[0] = '\0';
    Cmd_ExecuteString("menu_single_player_mission_select 0");
    T_STREQ(captured_movie_path,
            tft ? "Movies\\HumanXIntro.mpq" : "Movies\\HumanIntro.mpq");
    T_STREQ(captured_command, "");
#else
    T_EQ((int)mission_list_box->MapListControl.State->count, 3);
    T_STREQ(mission_list_box->MapListControl.State->items[0].name,
            tft ? "Chapter One: Misconceptions" : "The Defense of Strahnbrad");
    T_STREQ(mission_list_box->MapListControl.State->items[1].name,
            tft ? "Chapter Two: A Dark Covenant" : "Blackrock & Roll");
#endif
    T_STREQ(mission_list_box->MapListControl.SelectCommand,
            "menu_single_player_mission_select %u");
    T_NOT_NULL(mission_list_box->event_handler);

    Cmd_ExecuteString(back_button->OnClick);
    T_ASSERT(!campaign_select_frame->hidden);
    T_ASSERT(mission_select_frame->hidden);

    captured_cvar_name[0] = '\0';
    captured_cvar_value[0] = '\0';
    Cmd_ExecuteString("menu_single_player_difficulty 2");
    T_STREQ(captured_cvar_name, "wc3_campaign_difficulty");
    T_STREQ(captured_cvar_value, "2");
    difficulty_title = UI_FindFrame("CampaignPopupMenuTitleTextTemplate");
    if (difficulty_title) {
        T_STREQ(difficulty_title->Text, UI_GetString("HARD"));
    }

    {
        wc3CampaignProgress_t progress;
        DWORD const edition = tft ? WC3_CAMPAIGN_EDITION_TFT : WC3_CAMPAIGN_EDITION_ROC;
        wc3CampaignProgressKey_t const undead = MAKE(wc3CampaignProgressKey_t,
                                                      .edition = edition,
                                                      .campaign = 2);
        wc3CampaignProgressKey_t const human02 = MAKE(wc3CampaignProgressKey_t,
                                                       .edition = edition,
                                                       .campaign = 1,
                                                       .mission = 1);

        wc3_campaign_progress_init(&progress);
        T_ASSERT(wc3_campaign_progress_set_campaign(&progress, undead, true));
        T_ASSERT(wc3_campaign_progress_set_mission(&progress, human02, true));
        T_ASSERT(wc3_campaign_progress_save(test_campaign_progress_path, &progress));
    }
    test_campaign_visibility = "unlocked";
    SinglePlayerMenu_ShowCampaign();
    T_EQ((int)campaign_list_box->MapListControl.State->count, 2);
    T_STREQ(campaign_list_box->MapListControl.State->items[0].path, "Human");
    T_STREQ(campaign_list_box->MapListControl.State->items[1].path, "Undead");

    Cmd_ExecuteString("menu_single_player_campaign_select 0");
#ifdef BZ_FFMPEG
    T_EQ((int)mission_list_box->MapListControl.State->count, 5);
    T_STREQ(mission_list_box->MapListControl.State->items[0].name,
            tft
                ? "Cinematic: Introduction: Alliance Introduction"
                : "Cinematic: Introduction: Human Introduction");
    T_STREQ(mission_list_box->MapListControl.State->items[1].name,
            tft
                ? "Cinematic: Cinematic: Alliance Opening"
                : "Cinematic: Cinematic: Human Opening");
    T_EQ((int)mission_list_box->MapListControl.State->items[2].flags, 0);
    T_STREQ(mission_list_box->MapListControl.State->items[2].name,
            tft ? "Chapter One: Misconceptions" : "The Defense of Strahnbrad");
    T_EQ((int)mission_list_box->MapListControl.State->items[3].flags, 1);
    T_STREQ(mission_list_box->MapListControl.State->items[3].name,
            tft ? "Chapter Two: A Dark Covenant" : "Blackrock & Roll");
    T_STREQ(mission_list_box->MapListControl.State->items[4].name,
            tft
                ? "Cinematic: Cinematic: Alliance Ending"
                : "Cinematic: Cinematic: Human Ending");
#else
    T_EQ((int)mission_list_box->MapListControl.State->count, 2);
    T_EQ((int)mission_list_box->MapListControl.State->items[0].flags, 0);
    T_STREQ(mission_list_box->MapListControl.State->items[0].name,
            tft ? "Chapter One: Misconceptions" : "The Defense of Strahnbrad");
    T_EQ((int)mission_list_box->MapListControl.State->items[1].flags, 1);
    T_STREQ(mission_list_box->MapListControl.State->items[1].name,
            tft ? "Chapter Two: A Dark Covenant" : "Blackrock & Roll");
#endif

    captured_command[0] = '\0';
    captured_cvar_name[0] = '\0';
    captured_cvar_value[0] = '\0';
#ifdef BZ_FFMPEG
    Cmd_ExecuteString("menu_single_player_mission_select 3");
#else
    Cmd_ExecuteString("menu_single_player_mission_select 1");
#endif
    T_STREQ(captured_cvar_name, "");
    T_STREQ(captured_cvar_value, "");
    T_STREQ(captured_command,
            tft
                ? "map \"Maps\\FrozenThrone\\Campaign\\HumanX02.w3x\""
                : "map \"Maps\\Campaign\\Human02.w3m\"");

    remove(test_campaign_progress_path);
    hide_expansion_campaign_file = false;
    test_fs_expansion = false;
    test_campaign_visibility = NULL;
    mi = saved;
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
    menuImport_t saved = mi;
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

    memset(&mi, 0, sizeof(mi));
    mi.FS_ReadFile = utf16le_fs_read;
    mi.FS_FreeFile = utf16le_fs_free;
    mi.MemAlloc = test_ui_mem_alloc;
    mi.MemFree = test_ui_mem_free;
    mi.ImageIndex = fake_image_index;
    mi.FontIndex = test_font_index;
    mi.Printf = test_ui_printf;
    test_command_imports();

    UI_ParseFDF("utf16le_test.fdf");
    mi = saved;

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
    LPCSTR text = "[Default]\nBackground=Default.blp\n[Human]\nBackground=Human.blp\nConsoleTexture05=Custom05.blp\n";
    (void)name; *buf = strdup(text); return (int)strlen(text);
}

static int test_versioned_theme_read(LPCSTR name, void **buf) {
    LPCSTR text =
        "[Default]\n"
        "GlueSpriteLayerBackground_V0=RocBackground.mdl\n"
        "GlueSpriteLayerBackground_V1=TftBackground.mdl\n"
        "GlueSpriteLayerTopLeft_V0=RocTopLeft.mdl\n"
        "GlueSpriteLayerTopLeft_V1=TftTopLeft.mdl\n"
        "CampaignFile_V0=UI\\CampaignStrings.txt\n"
        "CampaignFile_V1=UI\\CampaignStrings_exp.txt\n"
        "MainMenuLogo=SharedLogo.mdl\n"
        "MainMenuLogo_V0=RocLogo.mdl\n"
        "MainMenuLogo_V1=TftLogo.mdl\n"
        "OnlyTft_V1=TftOnly.mdl\n";
    (void)name; *buf = strdup(text); return (int)strlen(text);
}

TEST(menu_fdf, versioned_theme_keys_follow_expansion_edition) {
    menuImport_t saved = mi;

    reset_ui_state();
    mi.FS_ReadFile = test_versioned_theme_read;
    mi.FS_FreeFile = test_fs_free_file;
    mi.Cvar_String = test_cvar_string;
    UI_LoadTheme("UI\\war3skins.txt");

    test_fs_expansion = false;
    T_STREQ(Theme_String("GlueSpriteLayerBackground", "Default"), "RocBackground.mdl");
    T_STREQ(Theme_String("GlueSpriteLayerTopLeft", "Default"), "RocTopLeft.mdl");
    T_STREQ(Theme_String("CampaignFile", "Default"), "UI\\CampaignStrings.txt");

    test_fs_expansion = true;
    T_STREQ(Theme_String("GlueSpriteLayerBackground", "Default"), "TftBackground.mdl");
    T_STREQ(Theme_String("GlueSpriteLayerTopLeft", "Default"), "TftTopLeft.mdl");
    T_STREQ(Theme_String("CampaignFile", "Default"), "UI\\CampaignStrings_exp.txt");

    test_fs_expansion = false;
    UI_ClearTheme();
    mi = saved;
}

TEST(menu_fdf, unversioned_theme_key_precedes_edition_variant) {
    menuImport_t saved = mi;

    reset_ui_state();
    mi.FS_ReadFile = test_versioned_theme_read;
    mi.FS_FreeFile = test_fs_free_file;
    mi.Cvar_String = test_cvar_string;
    UI_LoadTheme("UI\\war3skins.txt");

    test_fs_expansion = false;
    T_STREQ(Theme_String("MainMenuLogo", "Default"), "SharedLogo.mdl");
    test_fs_expansion = true;
    T_STREQ(Theme_String("MainMenuLogo", "Default"), "SharedLogo.mdl");

    test_fs_expansion = false;
    T_STREQ(Theme_String("OnlyTft", "Default"), "OnlyTft");

    UI_ClearTheme();
    mi = saved;
}

TEST(menu_fdf, deferred_texture_cache_tracks_theme_changes) {
    menuImport_t saved = mi;
    PLAYER player = { .race = kPlayerRaceHuman };
    reset_ui_state();
    mi.FS_ReadFile = test_theme_read; mi.FS_FreeFile = test_fs_free_file;
    UI_LoadTheme("UI\\war3skins.txt");
    DWORD index = UI_LoadTexture("Background", true);
    T_EQ(UI_LoadTexture("Background", true), index);
    T_EQ(fake_texture_id, 0);
    menu_player = &player;
    T_NOT_NULL(UI_GetTexture(index));
    T_STREQ(captured_image_path, "Human.blp");
    T_EQ(texture_releases, 0); T_EQ(fake_texture_id, 1);
    T_NOT_NULL(UI_GetTexture(index)); T_EQ(fake_texture_id, 1);
    menu_player = NULL;
    T_NOT_NULL(UI_GetTexture(index));
    T_STREQ(captured_image_path, "Default.blp");
    T_EQ(texture_releases, 1); T_EQ(fake_texture_id, 2);
    T_NOT_NULL(UI_GetTexture(index)); T_EQ(fake_texture_id, 2);
    UI_ClearTheme(); mi = saved;
}


TEST(menu_fdf, exported_image_resolver_uses_local_player_skin) {
    menuImport_t saved = mi;
    PLAYER player = { .race = kPlayerRaceHuman };

    reset_ui_state();
    mi.FS_ReadFile = test_theme_read; mi.FS_FreeFile = test_fs_free_file;

    menu_player = &player;
    UI_LoadTheme("UI\\war3skins.txt");
    T_STREQ(M_ResolveImagePath("Background"), "Human.blp");
    T_STREQ(M_ResolveImagePath("ConsoleTexture05"), "Custom05.blp");
    T_STREQ(M_ResolveImagePath("ConsoleTexture06"), "Custom06.blp");
    T_STREQ(M_ResolveImagePath("UI\\Textures\\fixed.blp"), "UI\\Textures\\fixed.blp");
    menu_player = NULL;
    UI_ClearTheme(); mi = saved;
}

static DWORD menu_test_refreshes, menu_test_connected;
static void test_lan_refresh(void) { menu_test_refreshes++; }
static DWORD test_lan_count(void) { return 1; }
static void test_lan_connect(DWORD index) { menu_test_connected = index; }
static BOOL test_lan_server(DWORD index, menuLanGame_t *out) {
    *out = (menuLanGame_t){ .hostname = "Fixture game", .mapname = "Maps\\Melee\\TwinRivers.w3m", .maxPlayers = 2 };
    return index == 0;
}
static int test_menu_map_list(LPCSTR path, LPCSTR ext, LPSTR out, int size) {
    LPCSTR map = !strcmp(ext, ".w3m") ? "Maps\\Melee\\TwinRivers.w3m" : "Maps\\FrozenThrone\\TwinRivers.w3x";
    (void)path;
    snprintf(out, size, "%s", map);
    return 1;
}

/* Native preview placement must fit the entire authored border above the compact metadata rows. */
TEST(menu_fdf, map_preview_fits_compact_and_full_panes) {
    menuImport_t saved = mi;
    test_glue_setup();
    T_ASSERT(UI_EnsureFDF("UI\\FrameDef\\Glue\\MapInfoPane.fdf"));
    LPFRAMEDEF source = UI_FindFrame("MapInfoPane");
    T_NOT_NULL(source);
    FLOAT heights[] = { 0.223125f, 0.36f };
    FOR_LOOP(i, 2) {
        LPFRAMEDEF root = UI_CloneFrameTree(source, NULL);
        UI_SetSize(root, 0.271875f, heights[i]);
        UI_LayoutMapInfoPane(root);
        LPFRAMEDEF preview = UI_FindChildFrame(root, "MinimapImage");
        LPFRAMEDEF border = UI_FindChildFrame(root, "MinimapImageBackdrop");
        LPFRAMEDEF label = UI_FindChildFrame(root, "SuggestedPlayersLabel");
        FLOAT bottom = -preview->Points.y[FPP_MIN].offset + (preview->Height + border->Height) * 0.5f;
        T_ASSERT(bottom <= -label->Points.y[FPP_MIN].offset);
        T_FEQ(preview->Width / preview->Height, 1.0f, 0.0001f);
        FLOAT width = preview->Width;
        UI_LayoutMapInfoPane(root);
        T_FEQ(preview->Width, width, 0.0001f);
        if (i == 1) T_FEQ(preview->Width, UI_FindChildFrame(source, "MinimapImage")->Width, 0.0001f);
    }
    Cmd_ExecuteString("menu_ingame");
    mi = saved;
}

TEST(menu_fdf, console_lan_and_lobby_commands_deliver_arguments) {
    menuImport_t saved = mi;
    test_glue_setup();
    mi.FS_GetFileList = test_menu_map_list;
    mi.Cvar_Set = test_cvar_set;
    mi.LAN_RefreshServers = test_lan_refresh;
    mi.LAN_NumServers = test_lan_count;
    mi.LAN_Server = test_lan_server;
    mi.LAN_ConnectServer = test_lan_connect;
    menu_test_refreshes = 0;
    menu_test_connected = UINT32_MAX;
    Cmd_ExecuteString("menu_joinserver");
    T_ASSERT(menu_test_refreshes > 0);
    Cmd_ExecuteString("menu_lan_select 0");
    Cmd_ExecuteString("menu_lan_join");
    T_EQ(menu_test_connected, 0);
    test_glue_tick(1000);
    Cmd_ExecuteString("menu_startserver");
    test_glue_tick(1000);
    Cmd_ExecuteString("menu_lan_refresh");
    Cmd_ExecuteString("menu_lan_select 1");
    T_NOT_NULL(LAN_SelectedMapPath());
    Cmd_ExecuteString("menu_lan_start");
    test_glue_tick(667);
    test_glue_tick(1000);
    T_ASSERT(UI_GetCurrentScreen() == &gameSetupScreen);
    Cmd_ExecuteString("menu_game_setup_map \"Maps\\Melee\\TwinRivers.w3m\"");
    T_ASSERT(strstr(captured_command, "lobby_start \"Maps\\\\Melee\\\\TwinRivers.w3m\"") != NULL);
    lobbyState_t state = {
        .active = true, .map_path = "Maps\\Melee\\TwinRivers.w3m", .slot_count = 2,
        .slots = { { .visible = true, .type = LOBBY_SLOT_HUMAN }, { .visible = true, .type = LOBBY_SLOT_COMPUTER } },
    };
    GameSetup_UpdateLobbySetup(&state);
    const char *commands[] = {
        "menu_game_setup_slot_type 0 1", "menu_game_setup_slot_race 0 1",
        "menu_game_setup_slot_team_next 0", "menu_game_setup_slot_color_next 0",
    };
    FOR_LOOP(i, sizeof(commands) / sizeof(commands[0])) {
        captured_command[0] = 0;
        Cmd_ExecuteString(commands[i]);
        unsigned slot, visible, player, type, race, team, color;
        T_EQ(sscanf(captured_command, "lobby_slot %u %u %u %u %u %u %u", &slot, &visible, &player, &type, &race, &team, &color), 7);
        T_EQ(slot, 0);
        if (i == 0) T_EQ(type, 1);
        if (i == 1) T_EQ(race, 1);
        if (i == 2) T_EQ(team, 1);
        if (i == 3) T_EQ(color, 1);
    }
    Cmd_ExecuteString("menu_game_setup_chat 1 \"hello  team\"");
    LPFRAMEDEF chat = UI_FindFrame("GameSetupChatText");
    T_NOT_NULL(chat);
    T_ASSERT(strstr(chat->Text, "|cffffffffhello  team|r") != NULL);
    Cmd_ExecuteString("menu_game_setup_chat hello everyone");
    T_ASSERT(strstr(chat->Text, "|cffffcc00hello everyone|r") != NULL);
    Cmd_ExecuteString("menu_game_setup_chat 0 \"123abc stays text\"");
    T_ASSERT(strstr(chat->Text, "123abc stays text") != NULL);
    captured_command[0] = 0;
    Cmd_ExecuteString("menu_game_setup_start");
    T_NULL(UI_GetCurrentScreen());
    T_ASSERT(strstr(captured_command, "map \"Maps\\\\Melee\\\\TwinRivers.w3m\"") != NULL);
    /* Reuse the loaded lobby fixture to check the solo presentation contract. */
    Cmd_ExecuteString("menu_single_player_skirmish");
    test_glue_tick(1000);
    M_ShowGameSetupMenu();
    test_glue_tick(0);
    T_ASSERT(UI_GetCurrentScreen() == &gameSetupScreen);
    T_STREQ(captured_sprite_anim[0], "SinglePlayerSkirmish Stand");
    T_STREQ(captured_sprite_anim[1], "SinglePlayer Stand");
    Cmd_ExecuteString("menu_game_setup_map \"Maps\\Melee\\TwinRivers.w3m\"");
    test_glue_tick(0);
    T_STREQ(captured_sprite_anim[0], "SinglePlayerSkirmish Stand");
    T_STREQ(captured_sprite_anim[1], "SinglePlayer Stand");
    Cmd_ExecuteString("menu_ingame");
    UI_ResetGlueSceneModels();
    mi = saved;
}
