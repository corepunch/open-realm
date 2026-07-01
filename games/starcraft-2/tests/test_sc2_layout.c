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
#include "games/starcraft-2/ui/sc2_layout.h"
#include "test_framework.h"

/* Define the uiimport global that sc2_layout.c references via extern */
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
    ASSERT(FS_AddArchive(TEST_SC2_MPQ) != NULL);

    /* Set up the uiimport table so SC2_LayoutParseFile can read from the MPQ */
    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = FS_ReadFileQ3;
    uiimport.FS_FreeFile = FS_FreeFile;
    uiimport.ImageIndex = test_image_index;
    uiimport.Printf = (void (*)(LPCSTR, ...))printf;

    sc2_layout_tests_initialized = true;
}

/* ---- Test: constants are parsed and resolvable ---- */
static void test_layout_constants_parsed(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestConstants.SC2Layout"));

    LPCSTR red = SC2_LayoutResolveConstant("##TestColorRed");
    ASSERT_NOT_NULL(red);
    ASSERT_STR_EQ(red, "255,0,0");

    LPCSTR green = SC2_LayoutResolveConstant("##TestColorGreen");
    ASSERT_NOT_NULL(green);
    ASSERT_STR_EQ(green, "0,255,0");

    LPCSTR gap = SC2_LayoutResolveConstant("##TestGap");
    ASSERT_NOT_NULL(gap);
    ASSERT_STR_EQ(gap, "4");

    SC2_LayoutShutdown();
}

/* ---- Test: unknown constants return NULL ---- */
static void test_layout_unknown_constant_returns_null(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();

    ASSERT_NULL(SC2_LayoutResolveConstant("##Nonexistent"));

    SC2_LayoutShutdown();
}

/* ---- Test: include resolution loads child file ---- */
static void test_layout_include_resolves(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestIncluded.SC2Layout"));

    sc2Frame_t *frame = SC2_LayoutFindTemplate("IncludedFrame");
    ASSERT_NOT_NULL(frame);
    ASSERT_STR_EQ(frame->name, "IncludedFrame");
    ASSERT_EQ_INT(frame->type, SC2_FRAMETYPE_FRAME);
    ASSERT(frame->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(frame->width, 100.0f, 0.01f);
    ASSERT(frame->flags & SC2_FRAME_HAS_HEIGHT);
    ASSERT_EQ_FLOAT(frame->height, 50.0f, 0.01f);
    ASSERT(frame->flags & SC2_FRAME_HAS_VISIBLE);
    ASSERT(frame->flags & SC2_FRAME_VISIBLE);

    SC2_LayoutShutdown();
}

/* ---- Test: template inheritance — child inherits parent properties ---- */
static void test_layout_template_inheritance(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    /* Base template: 200x100 */
    sc2Frame_t *base = SC2_LayoutFindTemplate("TestBaseTemplate");
    ASSERT_NOT_NULL(base);
    ASSERT(base->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(base->width, 200.0f, 0.01f);
    ASSERT(base->flags & SC2_FRAME_HAS_HEIGHT);
    ASSERT_EQ_FLOAT(base->height, 100.0f, 0.01f);
    ASSERT_EQ_INT(base->num_anchors, 2);

    /* Button template inherits from base, overrides width/height */
    sc2Frame_t *button = SC2_LayoutFindTemplate("TestButtonTemplate");
    ASSERT_NOT_NULL(button);
    ASSERT(button->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(button->width, 300.0f, 0.01f);
    ASSERT(button->flags & SC2_FRAME_HAS_HEIGHT);
    ASSERT_EQ_FLOAT(button->height, 75.0f, 0.01f);
    /* Should have 3 anchors: 2 inherited from base + 1 own */
    ASSERT_EQ_INT(button->num_anchors, 3);

    SC2_LayoutShutdown();
}

/* ---- Test: template with nested children ---- */
static void test_layout_template_children(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    sc2Frame_t *container = SC2_LayoutFindTemplate("TestContainerTemplate");
    ASSERT_NOT_NULL(container);
    ASSERT_EQ_INT(container->num_children, 2);

    /* First child: Background image */
    sc2Frame_t *bg = container->children[0];
    ASSERT_NOT_NULL(bg);
    ASSERT_STR_EQ(bg->name, "Background");
    ASSERT_EQ_INT(bg->type, SC2_FRAMETYPE_IMAGE);
    ASSERT(bg->num_textures > 0);
    ASSERT(bg->textures[0].flags & SC2_TEX_HAS_TEXTURE);

    /* Second child: TitleLabel */
    sc2Frame_t *label = container->children[1];
    ASSERT_NOT_NULL(label);
    ASSERT_STR_EQ(label->name, "TitleLabel");
    ASSERT_EQ_INT(label->type, SC2_FRAMETYPE_LABEL);

    SC2_LayoutShutdown();
}

/* ---- Test: full GameUI parse with includes, templates, and nesting ---- */
static void test_layout_gameui_full_parse(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    /* Root GameUI frame should exist */
    sc2Frame_t *gameui = SC2_LayoutFindTemplate("TestGameUI");
    ASSERT_NOT_NULL(gameui);
    ASSERT_EQ_INT(gameui->type, SC2_FRAMETYPE_GAME_UI);

    /* Should have 4 children: IncludedPanel, ActionButton01, InfoPanel, CommandArea */
    ASSERT(gameui->num_children >= 4);

    /* Check IncludedPanel uses the included template */
    sc2Frame_t *included = gameui->children[0];
    ASSERT_NOT_NULL(included);
    ASSERT_STR_EQ(included->name, "IncludedPanel");
    ASSERT(included->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(included->width, 100.0f, 0.01f);

    /* Check ActionButton01 inherits from TestButtonTemplate (300x75) */
    sc2Frame_t *button = gameui->children[1];
    ASSERT_NOT_NULL(button);
    ASSERT_STR_EQ(button->name, "ActionButton01");
    ASSERT(button->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(button->width, 300.0f, 0.01f);
    ASSERT(button->flags & SC2_FRAME_HAS_HEIGHT);
    ASSERT_EQ_FLOAT(button->height, 75.0f, 0.01f);

    SC2_LayoutShutdown();
}

/* ---- Test: deeply nested children in GameUI ---- */
static void test_layout_nested_children(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    sc2Frame_t *gameui = SC2_LayoutFindTemplate("TestGameUI");
    ASSERT_NOT_NULL(gameui);

    /* Find the CommandArea frame */
    sc2Frame_t *cmd_area = NULL;
    for (int i = 0; i < gameui->num_children; i++) {
        if (!strcasecmp(gameui->children[i]->name, "CommandArea")) {
            cmd_area = gameui->children[i];
            break;
        }
    }
    ASSERT_NOT_NULL(cmd_area);
    ASSERT(cmd_area->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(cmd_area->width, 450.0f, 0.01f);
    ASSERT(cmd_area->flags & SC2_FRAME_HAS_HEIGHT);
    ASSERT_EQ_FLOAT(cmd_area->height, 300.0f, 0.01f);

    /* CommandArea should have 4 children: CommandBackground, Cmd01, Cmd02, UnitName */
    ASSERT_EQ_INT(cmd_area->num_children, 4);

    /* Cmd01 and Cmd02 should be buttons with 76x76 */
    sc2Frame_t *cmd01 = cmd_area->children[1];
    ASSERT_NOT_NULL(cmd01);
    ASSERT_STR_EQ(cmd01->name, "Cmd01");
    ASSERT_EQ_INT(cmd01->type, SC2_FRAMETYPE_BUTTON);
    ASSERT(cmd01->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(cmd01->width, 76.0f, 0.01f);

    sc2Frame_t *cmd02 = cmd_area->children[2];
    ASSERT_NOT_NULL(cmd02);
    ASSERT_STR_EQ(cmd02->name, "Cmd02");
    ASSERT(cmd02->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(cmd02->width, 76.0f, 0.01f);

    SC2_LayoutShutdown();
}

/* ---- Test: shorthand <Anchor relative="$parent"/> fills all sides ---- */
static void test_layout_shorthand_anchor(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    /* Look for a template that uses the shorthand anchor.
     * TestContainerTemplate's child Background has <Anchor relative="$parent"/>. */
    sc2Frame_t *container = SC2_LayoutFindTemplate("TestContainerTemplate");
    ASSERT_NOT_NULL(container);
    sc2Frame_t *bg = container->children[0];
    ASSERT_NOT_NULL(bg);
    /* If shorthand was parsed, it should have 4 anchors (Top/Min, Bottom/Max, Left/Min, Right/Max) */
    ASSERT_EQ_INT(bg->num_anchors, 4);

    /* Verify the four anchors */
    int found_top = 0, found_bottom = 0, found_left = 0, found_right = 0;
    for (int i = 0; i < bg->num_anchors; i++) {
        if (bg->anchors[i].side == SC2_SIDE_TOP)    found_top    = 1;
        if (bg->anchors[i].side == SC2_SIDE_BOTTOM) found_bottom = 1;
        if (bg->anchors[i].side == SC2_SIDE_LEFT)   found_left   = 1;
        if (bg->anchors[i].side == SC2_SIDE_RIGHT)  found_right  = 1;
        ASSERT_STR_EQ(bg->anchors[i].relative, "$parent");
    }
    ASSERT(found_top);
    ASSERT(found_bottom);
    ASSERT(found_left);
    ASSERT(found_right);

    SC2_LayoutShutdown();
}

/* ---- Test: anchor parsing ---- */
static void test_layout_anchors_parsed(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    sc2Frame_t *base = SC2_LayoutFindTemplate("TestBaseTemplate");
    ASSERT_NOT_NULL(base);
    ASSERT_EQ_INT(base->num_anchors, 2);

    /* First anchor: Top, Min, offset 0 */
    ASSERT_EQ_INT(base->anchors[0].side, SC2_SIDE_TOP);
    ASSERT_EQ_INT(base->anchors[0].pos, SC2_POS_MIN);
    ASSERT_EQ_INT(base->anchors[0].offset, 0);
    ASSERT_STR_EQ(base->anchors[0].relative, "$parent");

    /* Second anchor: Left, Min, offset 0 */
    ASSERT_EQ_INT(base->anchors[1].side, SC2_SIDE_LEFT);
    ASSERT_EQ_INT(base->anchors[1].pos, SC2_POS_MIN);

    SC2_LayoutShutdown();
}

/* ---- Test: texture references parsed ---- */
static void test_layout_textures_parsed(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    sc2Frame_t *container = SC2_LayoutFindTemplate("TestContainerTemplate");
    ASSERT_NOT_NULL(container);

    /* Background image child has a texture */
    sc2Frame_t *bg = container->children[0];
    ASSERT_NOT_NULL(bg);
    ASSERT(bg->num_textures > 0);
    ASSERT(bg->textures[0].flags & SC2_TEX_HAS_TEXTURE);
    ASSERT_STR_EQ(bg->textures[0].resource, "@@Test/Background");

    SC2_LayoutShutdown();
}

/* ---- Test: frame tree flattening ---- */
static void test_layout_flatten_to_frames(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("TestGameUI"));

    /* Build the frame array from the TestGameUI root */
    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* Should have at least the GameUI frame and its direct children */
    ASSERT(count >= 5);

    /* First frame should be the root TestGameUI */
    ASSERT_EQ_INT(frames[0].type, FT_FRAME);
    ASSERT(frames[0].size.width > 0 || frames[0].size.height > 0 ||
           frames[0].parent_index == (DWORD)-1);
    sc2BaseFrame_t *background = SC2_LayoutFindFrameByName("CommandBackground");
    sc2BaseFrame_t *label = SC2_LayoutFindFrameByName("UnitName");
    ASSERT_NOT_NULL(background);
    ASSERT_EQ_INT(background->image, 17);
    ASSERT_NOT_NULL(label);
    ASSERT_EQ_FLOAT(label->size.width, 200.0f, 0.001f);
    ASSERT_EQ_FLOAT(label->size.height, 20.0f, 0.001f);

    SC2_LayoutShutdown();
}

/* ---- Test: multiple parses accumulate templates ---- */
static void test_layout_multiple_parses(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();

    ASSERT(SC2_LayoutParseFile("UI/Layout/TestConstants.SC2Layout"));
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestIncluded.SC2Layout"));

    /* Both should be findable */
    ASSERT_NOT_NULL(SC2_LayoutFindTemplate("IncludedFrame"));
    LPCSTR red = SC2_LayoutResolveConstant("##TestColorRed");
    ASSERT_NOT_NULL(red);

    SC2_LayoutShutdown();
}

/* ---- Test: reinit clears state ---- */
static void test_layout_reinit_clears(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestIncluded.SC2Layout"));
    ASSERT_NOT_NULL(SC2_LayoutFindTemplate("IncludedFrame"));

    SC2_LayoutShutdown();
    SC2_LayoutInit();

    /* After reinit, template should not be found */
    ASSERT_NULL(SC2_LayoutFindTemplate("IncludedFrame"));

    SC2_LayoutShutdown();
}

/* ---- Test: constant reference in anchor offset ---- */
static void test_layout_constant_offset(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    sc2Frame_t *gameui = SC2_LayoutFindTemplate("TestGameUI");
    ASSERT_NOT_NULL(gameui);

    /* ActionButton01 has anchor with #TestGap offset (should be "4") */
    sc2Frame_t *button = gameui->children[1];
    ASSERT_NOT_NULL(button);
    ASSERT(button->num_anchors > 0);

    /* The constant value "4" should have been parsed as offset 4 */
    ASSERT_EQ_INT(button->anchors[0].offset, 4);

    SC2_LayoutShutdown();
}

/* ---- Test: multiple textures (layers) ---- */
static void test_layout_texture_layers(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestTemplates.SC2Layout"));

    /* TestColorBox has no textures defined — verify empty state */
    sc2Frame_t *box = SC2_LayoutFindTemplate("TestColorBox");
    ASSERT_NOT_NULL(box);
    ASSERT_EQ_INT(box->num_textures, 0);

    /* Background has exactly 1 texture */
    sc2Frame_t *container = SC2_LayoutFindTemplate("TestContainerTemplate");
    ASSERT_NOT_NULL(container);
    sc2Frame_t *bg = container->children[0];
    ASSERT_EQ_INT(bg->num_textures, 1);

    SC2_LayoutShutdown();
}

/* ---- Test: flattened frames from test fixture have valid parent structure ---- */
static void test_layout_flattened_frames_hierarchy(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("TestGameUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);
    ASSERT(count >= 5);

    /* Root: parent_index == -1 */
    ASSERT_EQ_INT(frames[0].parent_index, (DWORD)-1);

    /* All non-root frames must have a valid parent_index */
    for (DWORD i = 1; i < count; i++)
        ASSERT(frames[i].parent_index < count);

    SC2_LayoutShutdown();
}

/* ---- Test: SC2_LayoutFindFrameByType returns correct root types ---- */
static void test_layout_find_by_type(void) {
    setup_sc2_layout_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("TestGameUI"));

    /* GameUI root should be findable */
    sc2BaseFrame_t *gameui = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_GAME_UI);
    ASSERT_NOT_NULL(gameui);
    ASSERT_EQ_INT(gameui->parent_index, (DWORD)-1);

    /* TestGameUI fixture has a GameUI type root; child panels may not exist
     * since TestGameUI.SC2Layout doesn't define ConsolePanel/ResourcePanel. */
    sc2BaseFrame_t *console = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_CONSOLE_PANEL);
    sc2BaseFrame_t *resource = SC2_LayoutFindFrameByType(SC2_FRAMETYPE_RESOURCE_PANEL);
    /* These are allowed to be NULL since the test fixture doesn't define them.
     * The important thing is they don't crash and return the expected type when present. */
    if (console) ASSERT(console->parent_index != (DWORD)-1);
    if (resource) ASSERT(resource->parent_index != (DWORD)-1);

    SC2_LayoutShutdown();
}

void run_sc2_layout_tests(void) {
    RUN_TEST(test_layout_constants_parsed);
    RUN_TEST(test_layout_unknown_constant_returns_null);
    RUN_TEST(test_layout_include_resolves);
    RUN_TEST(test_layout_template_inheritance);
    RUN_TEST(test_layout_template_children);
    RUN_TEST(test_layout_gameui_full_parse);
    RUN_TEST(test_layout_nested_children);
    RUN_TEST(test_layout_anchors_parsed);
    RUN_TEST(test_layout_textures_parsed);
    RUN_TEST(test_layout_flatten_to_frames);
    RUN_TEST(test_layout_multiple_parses);
    RUN_TEST(test_layout_reinit_clears);
    RUN_TEST(test_layout_constant_offset);
    RUN_TEST(test_layout_texture_layers);
    RUN_TEST(test_layout_flattened_frames_hierarchy);
    RUN_TEST(test_layout_find_by_type);
    RUN_TEST(test_layout_shorthand_anchor);
}
