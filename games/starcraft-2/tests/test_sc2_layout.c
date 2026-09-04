/*
 * test_sc2_layout.c — SC2 .SC2Layout XML parser fixture tests.
 *
 * Tests parsing of custom .SC2Layout fixtures packed into the test MPQ,
 * verifying frame hierarchy, template inheritance, anchor resolution,
 * constants, include resolution, and frame tree flattening.
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "games/starcraft-2/menu/menu_layout.h"
#include "test.h"

/* Define the uiimport global that menu_layout.c references via extern */
uiImport_t uiimport;

#ifndef TEST_SC2_MPQ
#define TEST_SC2_MPQ "build/tests/test-sc2.SC2Maps"
#endif

static BOOL sc2_layout_tests_initialized;
static int test_image_index(LPCSTR name) { return name && *name ? 17 : 0; }

static void setup_sc2_layout_tests(void) {
    if (sc2_layout_tests_initialized) return;

    LPCSTR argv[] = { "test_sc2_layout", "-config", "" };
    Com_Init(3, argv);
    T_ASSERT(FS_AddArchive(TEST_SC2_MPQ) != NULL);

    /* Set up the uiimport table so SC2_LayoutParseFile can read from the MPQ */
    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = FS_ReadFileQ3;
    uiimport.FS_FreeFile = FS_FreeFile;
    uiimport.ImageIndex = test_image_index;
    uiimport.Printf = (void (*)(LPCSTR, ...))printf;

    sc2_layout_tests_initialized = true;
}

/* ---- Test: constants are parsed and resolvable ---- */
TEST(sc2_layout, layout_constants_parsed) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestConstants.SC2Layout"));

    LPCSTR red = SC2_LayoutResolveConstant("##TestColorRed");
    T_NOT_NULL(red);
    T_STREQ(red, "255,0,0");

    LPCSTR green = SC2_LayoutResolveConstant("##TestColorGreen");
    T_NOT_NULL(green);
    T_STREQ(green, "0,255,0");

    LPCSTR gap = SC2_LayoutResolveConstant("##TestGap");
    T_NOT_NULL(gap);
    T_STREQ(gap, "4");

    SC2_LayoutShutdown();
}

/* ---- Test: unknown constants return NULL ---- */
TEST(sc2_layout, layout_unknown_constant_returns_null) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();

    T_NULL(SC2_LayoutResolveConstant("##Nonexistent"));

    SC2_LayoutShutdown();
}

/* ---- Test: include resolution loads child file ---- */
TEST(sc2_layout, layout_include_resolves) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestIncluded.SC2Layout"));

    sc2Frame_t *frame = SC2_LayoutFindTemplate("IncludedFrame");
    T_NOT_NULL(frame);
    T_STREQ(frame->name, "IncludedFrame");
    T_EQ(frame->type, SC2_FRAMETYPE_FRAME);
    T_ASSERT(frame->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(frame->width, 100.0f, 0.01f);
    T_ASSERT(frame->flags & SC2_FRAME_HAS_HEIGHT);
    T_FEQ(frame->height, 50.0f, 0.01f);
    T_ASSERT(frame->flags & SC2_FRAME_HAS_VISIBLE);
    T_ASSERT(frame->flags & SC2_FRAME_VISIBLE);

    SC2_LayoutShutdown();
}

/* ---- Test: template inheritance — child inherits parent properties ---- */
TEST(sc2_layout, layout_template_inheritance) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    /* Base template: 200x100 */
    sc2Frame_t *base = SC2_LayoutFindTemplate("TestBaseTemplate");
    T_NOT_NULL(base);
    T_ASSERT(base->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(base->width, 200.0f, 0.01f);
    T_ASSERT(base->flags & SC2_FRAME_HAS_HEIGHT);
    T_FEQ(base->height, 100.0f, 0.01f);
    T_EQ(base->num_anchors, 2);

    /* Button template inherits from base, overrides width/height */
    sc2Frame_t *button = SC2_LayoutFindTemplate("TestButtonTemplate");
    T_NOT_NULL(button);
    T_ASSERT(button->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(button->width, 300.0f, 0.01f);
    T_ASSERT(button->flags & SC2_FRAME_HAS_HEIGHT);
    T_FEQ(button->height, 75.0f, 0.01f);
    /* Should have 3 anchors: 2 inherited from base + 1 own */
    T_EQ(button->num_anchors, 3);

    SC2_LayoutShutdown();
}

/* ---- Test: template with nested children ---- */
TEST(sc2_layout, layout_template_children) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    sc2Frame_t *container = SC2_LayoutFindTemplate("TestContainerTemplate");
    T_NOT_NULL(container);
    T_EQ(container->num_children, 2);

    /* First child: Background image */
    sc2Frame_t *bg = container->children[0];
    T_NOT_NULL(bg);
    T_STREQ(bg->name, "Background");
    T_EQ(bg->type, SC2_FRAMETYPE_IMAGE);
    T_ASSERT(bg->num_textures > 0);
    T_ASSERT(bg->textures[0].flags & SC2_TEX_HAS_TEXTURE);

    /* Second child: TitleLabel */
    sc2Frame_t *label = container->children[1];
    T_NOT_NULL(label);
    T_STREQ(label->name, "TitleLabel");
    T_EQ(label->type, SC2_FRAMETYPE_LABEL);

    SC2_LayoutShutdown();
}

/* ---- Test: full GameUI parse with includes, templates, and nesting ---- */
TEST(sc2_layout, layout_gameui_full_parse) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    /* Root GameUI frame should exist */
    sc2Frame_t *gameui = SC2_LayoutFindTemplate("TestGameUI");
    T_NOT_NULL(gameui);
    T_EQ(gameui->type, SC2_FRAMETYPE_GAME_UI);

    /* Should have 4 children: IncludedPanel, ActionButton01, InfoPanel, CommandArea */
    T_ASSERT(gameui->num_children >= 4);

    /* Check IncludedPanel uses the included template */
    sc2Frame_t *included = gameui->children[0];
    T_NOT_NULL(included);
    T_STREQ(included->name, "IncludedPanel");
    T_ASSERT(included->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(included->width, 100.0f, 0.01f);

    /* Check ActionButton01 inherits from TestButtonTemplate (300x75) */
    sc2Frame_t *button = gameui->children[1];
    T_NOT_NULL(button);
    T_STREQ(button->name, "ActionButton01");
    T_ASSERT(button->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(button->width, 300.0f, 0.01f);
    T_ASSERT(button->flags & SC2_FRAME_HAS_HEIGHT);
    T_FEQ(button->height, 75.0f, 0.01f);

    SC2_LayoutShutdown();
}

/* ---- Test: deeply nested children in GameUI ---- */
TEST(sc2_layout, layout_nested_children) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    sc2Frame_t *gameui = SC2_LayoutFindTemplate("TestGameUI");
    T_NOT_NULL(gameui);

    /* Find the CommandArea frame */
    sc2Frame_t *cmd_area = NULL;
    for (int i = 0; i < gameui->num_children; i++) {
        if (!strcasecmp(gameui->children[i]->name, "CommandArea")) {
            cmd_area = gameui->children[i];
            break;
        }
    }
    T_NOT_NULL(cmd_area);
    T_ASSERT(cmd_area->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(cmd_area->width, 450.0f, 0.01f);
    T_ASSERT(cmd_area->flags & SC2_FRAME_HAS_HEIGHT);
    T_FEQ(cmd_area->height, 300.0f, 0.01f);

    /* CommandArea should have 4 children: CommandBackground, Cmd01, Cmd02, UnitName */
    T_EQ(cmd_area->num_children, 4);

    /* Cmd01 and Cmd02 should be buttons with 76x76 */
    sc2Frame_t *cmd01 = cmd_area->children[1];
    T_NOT_NULL(cmd01);
    T_STREQ(cmd01->name, "Cmd01");
    T_EQ(cmd01->type, SC2_FRAMETYPE_BUTTON);
    T_ASSERT(cmd01->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(cmd01->width, 76.0f, 0.01f);

    sc2Frame_t *cmd02 = cmd_area->children[2];
    T_NOT_NULL(cmd02);
    T_STREQ(cmd02->name, "Cmd02");
    T_ASSERT(cmd02->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(cmd02->width, 76.0f, 0.01f);

    SC2_LayoutShutdown();
}

/* ---- Test: shorthand <Anchor relative="$parent"/> fills all sides ---- */
TEST(sc2_layout, layout_shorthand_anchor) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    /* Look for a template that uses the shorthand anchor.
     * TestContainerTemplate's child Background has <Anchor relative="$parent"/>. */
    sc2Frame_t *container = SC2_LayoutFindTemplate("TestContainerTemplate");
    T_NOT_NULL(container);
    sc2Frame_t *bg = container->children[0];
    T_NOT_NULL(bg);
    /* If shorthand was parsed, it should have 4 anchors (Top/Min, Bottom/Max, Left/Min, Right/Max) */
    T_EQ(bg->num_anchors, 4);

    /* Verify the four anchors */
    int found_top = 0, found_bottom = 0, found_left = 0, found_right = 0;
    for (int i = 0; i < bg->num_anchors; i++) {
        if (bg->anchors[i].side == SC2_SIDE_TOP)    found_top    = 1;
        if (bg->anchors[i].side == SC2_SIDE_BOTTOM) found_bottom = 1;
        if (bg->anchors[i].side == SC2_SIDE_LEFT)   found_left   = 1;
        if (bg->anchors[i].side == SC2_SIDE_RIGHT)  found_right  = 1;
        T_STREQ(bg->anchors[i].relative, "$parent");
    }
    T_ASSERT(found_top);
    T_ASSERT(found_bottom);
    T_ASSERT(found_left);
    T_ASSERT(found_right);

    SC2_LayoutShutdown();
}

/* ---- Test: anchor parsing ---- */
TEST(sc2_layout, layout_anchors_parsed) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    sc2Frame_t *base = SC2_LayoutFindTemplate("TestBaseTemplate");
    T_NOT_NULL(base);
    T_EQ(base->num_anchors, 2);

    /* First anchor: Top, Min, offset 0 */
    T_EQ(base->anchors[0].side, SC2_SIDE_TOP);
    T_EQ(base->anchors[0].pos, SC2_POS_MIN);
    T_EQ(base->anchors[0].offset, 0);
    T_STREQ(base->anchors[0].relative, "$parent");

    /* Second anchor: Left, Min, offset 0 */
    T_EQ(base->anchors[1].side, SC2_SIDE_LEFT);
    T_EQ(base->anchors[1].pos, SC2_POS_MIN);

    SC2_LayoutShutdown();
}

/* ---- Test: texture references parsed ---- */
TEST(sc2_layout, layout_textures_parsed) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    sc2Frame_t *container = SC2_LayoutFindTemplate("TestContainerTemplate");
    T_NOT_NULL(container);

    /* Background image child has a texture */
    sc2Frame_t *bg = container->children[0];
    T_NOT_NULL(bg);
    T_ASSERT(bg->num_textures > 0);
    T_ASSERT(bg->textures[0].flags & SC2_TEX_HAS_TEXTURE);
    T_STREQ(bg->textures[0].resource, "@@Test/Background");

    SC2_LayoutShutdown();
}

/* ---- Test: frame tree flattening ---- */
TEST(sc2_layout, layout_flatten_to_frames) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("TestGameUI"));

    /* Build the frame array from the TestGameUI root */
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* Should have at least the GameUI frame and its direct children */
    T_ASSERT(count >= 5);

    /* First frame should be the root TestGameUI */
    T_EQ(frames[0].type, FT_FRAME);
    T_ASSERT(frames[0].size.width > 0 || frames[0].size.height > 0 ||
           frames[0].parent_index == (DWORD)-1);
    sc2BaseFrame_t *background = SC2_LayoutFindFrameByName("CommandBackground");
    sc2BaseFrame_t *label = SC2_LayoutFindFrameByName("UnitName");
    T_NOT_NULL(background);
    T_EQ(background->image, 17);
    T_NOT_NULL(label);
    T_FEQ(label->size.width, 200.0f, 0.001f);
    T_FEQ(label->size.height, 20.0f, 0.001f);

    SC2_LayoutShutdown();
}

/* ---- Test: multiple parses accumulate templates ---- */
TEST(sc2_layout, layout_multiple_parses) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();

    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestConstants.SC2Layout"));
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestIncluded.SC2Layout"));

    /* Both should be findable */
    T_NOT_NULL(SC2_LayoutFindTemplate("IncludedFrame"));
    LPCSTR red = SC2_LayoutResolveConstant("##TestColorRed");
    T_NOT_NULL(red);

    SC2_LayoutShutdown();
}

/* ---- Test: reinit clears state ---- */
TEST(sc2_layout, layout_reinit_clears) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestIncluded.SC2Layout"));
    T_NOT_NULL(SC2_LayoutFindTemplate("IncludedFrame"));

    SC2_LayoutShutdown();
    SC2_LayoutInit();

    /* After reinit, template should not be found */
    T_NULL(SC2_LayoutFindTemplate("IncludedFrame"));

    SC2_LayoutShutdown();
}

/* ---- Test: constant reference in anchor offset ---- */
TEST(sc2_layout, layout_constant_offset) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    sc2Frame_t *gameui = SC2_LayoutFindTemplate("TestGameUI");
    T_NOT_NULL(gameui);

    /* ActionButton01 has anchor with #TestGap offset (should be "4") */
    sc2Frame_t *button = gameui->children[1];
    T_NOT_NULL(button);
    T_ASSERT(button->num_anchors > 0);

    /* The constant value "4" should have been parsed as offset 4 */
    T_EQ(button->anchors[0].offset, 4);

    SC2_LayoutShutdown();
}

/* ---- Test: multiple textures (layers) ---- */
TEST(sc2_layout, layout_texture_layers) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    /* TestColorBox has no textures defined — verify empty state */
    sc2Frame_t *box = SC2_LayoutFindTemplate("TestColorBox");
    T_NOT_NULL(box);
    T_EQ(box->num_textures, 0);

    /* Background has exactly 1 texture */
    sc2Frame_t *container = SC2_LayoutFindTemplate("TestContainerTemplate");
    T_NOT_NULL(container);
    sc2Frame_t *bg = container->children[0];
    T_EQ(bg->num_textures, 1);

    SC2_LayoutShutdown();
}

/* ---- Test: flattened frames from test fixture have valid parent structure ---- */
TEST(sc2_layout, layout_flattened_frames_hierarchy) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("TestGameUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);
    T_ASSERT(count >= 5);

    /* Root: parent_index == -1 */
    T_EQ(frames[0].parent_index, (DWORD)-1);

    /* All non-root frames must have a valid parent_index */
    for (DWORD i = 1; i < count; i++)
        T_ASSERT(frames[i].parent_index < count);

    SC2_LayoutShutdown();
}

/* ---- Test: SC2_LayoutFindFrameByType returns correct root types ---- */
TEST(sc2_layout, layout_find_by_type) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("TestGameUI"));

    /* GameUI root should be findable */
    sc2BaseFrame_t *gameui = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_GAME_UI);
    T_NOT_NULL(gameui);
    T_EQ(gameui->parent_index, (DWORD)-1);

    /* TestGameUI fixture has a GameUI type root; child panels may not exist
     * since TestGameUI.SC2Layout doesn't define ConsolePanel/ResourcePanel. */
    sc2BaseFrame_t *console = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_CONSOLE_PANEL);
    sc2BaseFrame_t *resource = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    /* These are allowed to be NULL since the test fixture doesn't define them.
     * The important thing is they don't crash and return the expected type when present. */
    if (console) T_ASSERT(console->parent_index != (DWORD)-1);
    if (resource) T_ASSERT(resource->parent_index != (DWORD)-1);

    SC2_LayoutShutdown();
}


/* Native console cameras survive flattening and partial template overrides, including explicit zeroes. */
TEST(sc2_layout, model_camera_payload) {
    setup_sc2_layout_tests(); SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleModelBase"));
    sc2Frame_t *base = SC2_LayoutFindTemplate("ConsoleModelBase");
    T_NOT_NULL(base); T_NOT_NULL(base->resolved_frame);
    UIMODEL model = base->resolved_frame->model;
    T_EQ(base->resolved_frame->model_flags, BZ_SC2_MODEL_FIELDS);
    T_FEQ(model.pos.x, -1, .0001f); T_FEQ(model.pos.y, -1, .0001f);
    T_FEQ(model.eye.y, -5, .0001f); T_FEQ(model.scale.z, 1, .0001f);
    T_FEQ(model.znear, 1, .0001f); T_FEQ(model.zfar, 1000, .0001f);
    T_FEQ(model.aspect, 4.0f / 3, .0001f); T_EQ(model.projection, UI_MODEL_ORTHOGRAPHIC);
    T_ASSERT(SC2_LayoutFlatten("ConsoleModelOverride"));
    sc2Frame_t *child = SC2_LayoutFindTemplate("ConsoleModelOverride");
    T_NOT_NULL(child); T_NOT_NULL(child->resolved_frame);
    model = child->resolved_frame->model;
    T_EQ(child->resolved_frame->model_flags, BZ_SC2_MODEL_FIELDS);
    T_FEQ(model.pos.x, 0, .0001f); T_FEQ(model.pos.y, 0, .0001f);
    T_FEQ(model.eye.y, -5, .0001f); T_FEQ(model.fov, 60, .0001f);
    T_EQ(model.projection, UI_MODEL_PERSPECTIVE);
    SC2_LayoutShutdown();
}

/* Widening a viewport expands horizontal space, retaining authored size and bottom/side anchors. */
TEST(sc2_layout, model_camera_widescreen) {
    UIMODEL model = { .eye = {0,-5,0}, .pos = {-1,-1,0}, .scale = {1,1,1},
        .fov = 90, .znear = 1, .zfar = 1000, .aspect = 4.0f / 3, .projection = UI_MODEL_ORTHOGRAPHIC };
    MATRIX4 narrow, wide;
    UI_ModelMatrix(&model, 4.0f / 3, &narrow); UI_ModelMatrix(&model, 16.0f / 9, &wide);
    VECTOR3 a = Matrix4_multiply_vector3(&narrow, &(VECTOR3){0,0,0});
    VECTOR3 b = Matrix4_multiply_vector3(&wide, &(VECTOR3){0,0,0});
    T_FEQ(a.x, -1, .0001f); T_FEQ(b.x, a.x, .0001f); T_FEQ(a.y, -1, .0001f);
    T_FEQ(b.y, a.y, .0001f);
    a = Matrix4_multiply_vector3(&narrow, &(VECTOR3){.5f,0,.25f});
    b = Matrix4_multiply_vector3(&wide, &(VECTOR3){.5f,0,.25f});
    T_FEQ(b.y, a.y, .0001f); T_FEQ((b.x + 1) * (16.0f/9), (a.x + 1) * (4.0f/3), .0001f);
    model.pos.x = 1; UI_ModelMatrix(&model, 16.0f/9, &wide);
    b = Matrix4_multiply_vector3(&wide, &(VECTOR3){0,0,0}); T_FEQ(b.x, 1, .0001f);
    model.projection = UI_MODEL_PERSPECTIVE; model.pos = (VECTOR3){0};
    UI_ModelMatrix(&model, 16.0f/9, &wide);
    b = Matrix4_multiply_vector3(&wide, &(VECTOR3){0,0,0});
    T_FEQ(b.x, 0, .0001f); T_FEQ(b.y, 0, .0001f); T_ASSERT(b.z > -1 && b.z < 1);
}
