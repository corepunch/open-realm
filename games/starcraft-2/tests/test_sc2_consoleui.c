/*
 * test_sc2_consoleui.c — SC2 ConsoleUI adapter tests.
 *
 * Tests the adapter layer that maps SC2 parsed frame data (sc2Frame_t)
 * into sc2BaseFrame_t arrays: anchor resolution, frame type mapping,
 * visibility flags, color/alpha population, and flatten correctness.
 *
 * Also serves as regression tests for the 4 parser bugs fixed in this change.
 */

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "games/starcraft-2/ui/sc2_layout.h"
#include "test_framework.h"

/* Define the uiimport global that sc2_layout.c references via extern */
extern uiImport_t uiimport;

#ifndef TEST_SC2_MPQ
#define TEST_SC2_MPQ "build/tests/test-sc2.SC2Maps"
#endif

static BOOL sc2_consoleui_tests_initialized;

static void setup_sc2_consoleui_tests(void) {
    if (sc2_consoleui_tests_initialized) return;

    LPCSTR argv[] = { "test_sc2_consoleui", "-config", "" };
    Com_Init(3, argv);
    ASSERT(FS_AddArchive(TEST_SC2_MPQ) != NULL);

    memset(&uiimport, 0, sizeof(uiimport));
    uiimport.FS_ReadFile = FS_ReadFileQ3;
    uiimport.FS_FreeFile = FS_FreeFile;
    uiimport.Printf = (void (*)(LPCSTR, ...))printf;

    sc2_consoleui_tests_initialized = true;
}

/* Helper: find a frame in the flat array by name */
static sc2BaseFrame_t *find_frame(sc2BaseFrame_t *frames, DWORD count, LPCSTR name) {
    for (DWORD i = 0; i < count; i++) {
        for (int j = 0; j < SC2_LayoutNumTemplates(); j++) {
            sc2Frame_t *tmpl = SC2_LayoutGetTemplate(j);
            if (tmpl && tmpl->resolved_frame == &frames[i] && !strcasecmp(tmpl->name, name))
                return &frames[i];
        }
    }
    return NULL;
}

/* Helper: find template by name (wraps internal lookup) */
static sc2Frame_t *find_template(LPCSTR name) {
    return SC2_LayoutFindTemplate(name);
}

/* =====================================================================
 * Group 1: Parser Bug Regression Tests
 * ===================================================================== */

/* Bug 1: ## constant hash stripping */
static void test_adapter_constant_hash_stripping(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    LPCSTR margin = SC2_LayoutResolveConstant("##HUDMargin");
    ASSERT_NOT_NULL(margin);
    ASSERT_STR_EQ(margin, "8");

    LPCSTR width = SC2_LayoutResolveConstant("##PanelWidth");
    ASSERT_NOT_NULL(width);
    ASSERT_STR_EQ(width, "200");

    /* Also verify the old TestConstants fixture still works */
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestConstants.SC2Layout"));
    LPCSTR red = SC2_LayoutResolveConstant("##TestColorRed");
    ASSERT_NOT_NULL(red);
    ASSERT_STR_EQ(red, "255,0,0");

    SC2_LayoutShutdown();
}

/* Bug 3: Constant resolution in anchor offsets */
static void test_adapter_constant_offset_resolves(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    sc2Frame_t *panel = find_template("ResourcePanel");
    ASSERT_NOT_NULL(panel);

    /* ResourcePanel's Left anchor has offset="#HUDMargin" which should resolve to 8 */
    ASSERT(panel->num_anchors > 0);
    BOOL found_left = false;
    for (int i = 0; i < panel->num_anchors; i++) {
        if (panel->anchors[i].side == SC2_SIDE_LEFT) {
            ASSERT_EQ_INT(panel->anchors[i].offset, 8);
            found_left = true;
            break;
        }
    }
    ASSERT(found_left);

    SC2_LayoutShutdown();
}

/* Bug 2: Template inheritance ordering (same-file templates) */
static void test_adapter_template_inheritance_ordering(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    /* ActionButton01 inherits from TestButtonTemplate (300x75).
     * TestButtonTemplate has 3 anchors (Top+Min, Left+Min from base, Right+Max own).
     * ActionButton01 has 2 inline anchors (Top+Min, Left+Min) which override
     * the same sides from the template. Result: 3 anchors total. */
    sc2Frame_t *button = find_template("ActionButton01");
    ASSERT_NOT_NULL(button);
    ASSERT(button->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(button->width, 300.0f, 0.01f);
    ASSERT(button->flags & SC2_FRAME_HAS_HEIGHT);
    ASSERT_EQ_FLOAT(button->height, 75.0f, 0.01f);

    /* Should have 3 anchors (2 template sides overridden + 1 template side kept) */
    ASSERT_EQ_INT(button->num_anchors, 3);

    SC2_LayoutShutdown();
}

/* Bug 4: Cross-file template inheritance */
static void test_adapter_cross_file_template_inheritance(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    /* IncludedPanel inherits from IncludedFrame in TestIncluded.SC2Layout */
    sc2Frame_t *panel = find_template("IncludedPanel");
    ASSERT_NOT_NULL(panel);
    ASSERT(panel->flags & SC2_FRAME_HAS_WIDTH);
    ASSERT_EQ_FLOAT(panel->width, 100.0f, 0.01f);
    ASSERT(panel->flags & SC2_FRAME_HAS_HEIGHT);
    ASSERT_EQ_FLOAT(panel->height, 50.0f, 0.01f);
    ASSERT(panel->flags & SC2_FRAME_HAS_VISIBLE);
    ASSERT(panel->flags & SC2_FRAME_VISIBLE);

    SC2_LayoutShutdown();
}

/* =====================================================================
 * Group 2: Anchor Resolution Tests
 * ===================================================================== */

static void test_adapter_single_anchor_left_min(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);
    ASSERT(count > 0);

    /* MineralIcon has Left+Min anchor */
    sc2BaseFrame_t *icon = find_frame(frames, count, "MineralIcon");
    ASSERT_NOT_NULL(icon);
    ASSERT(icon->points.x[FPP_MIN].used);
    ASSERT_EQ_INT(icon->points.x[FPP_MIN].targetPos, FPP_MIN);

    SC2_LayoutShutdown();
}

static void test_adapter_single_anchor_top_min(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* MineralIcon has Top+Min anchor */
    sc2BaseFrame_t *icon = find_frame(frames, count, "MineralIcon");
    ASSERT_NOT_NULL(icon);
    ASSERT(icon->points.y[FPP_MIN].used);
    ASSERT_EQ_INT(icon->points.y[FPP_MIN].targetPos, FPP_MIN);

    SC2_LayoutShutdown();
}

static void test_adapter_dual_anchor_stretch(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* ResourcePanel has Top+Max and Bottom+Max → y-axis dual anchor */
    sc2BaseFrame_t *panel = find_frame(frames, count, "ResourcePanel");
    ASSERT_NOT_NULL(panel);
    ASSERT(panel->points.y[FPP_MIN].used); /* Top+Max maps to y[FPP_MIN] with pos=Max */
    ASSERT(panel->points.y[FPP_MAX].used); /* Bottom+Max maps to y[FPP_MAX] with pos=Max */

    SC2_LayoutShutdown();
}

static void test_adapter_mid_anchor(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* CenterAlert has Left+Mid and Top+Mid.
     * Left → x[FPP_MIN] (element's left edge), pos=Mid → targetPos=FPP_MID (parent center).
     * Top  → y[FPP_MIN] (element's top edge),  pos=Mid → targetPos=FPP_MID (parent center). */
    sc2BaseFrame_t *alert = find_frame(frames, count, "CenterAlert");
    ASSERT_NOT_NULL(alert);
    ASSERT(alert->points.x[FPP_MIN].used);
    ASSERT(alert->points.x[FPP_MIN].targetPos == FPP_MID);
    ASSERT(alert->points.y[FPP_MIN].used);
    ASSERT(alert->points.y[FPP_MIN].targetPos == FPP_MID);

    SC2_LayoutShutdown();
}

static void test_adapter_cross_frame_relative(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* Cmd02's Left anchor references $parent/Cmd01 */
    sc2BaseFrame_t *cmd02 = find_frame(frames, count, "Cmd02");
    ASSERT_NOT_NULL(cmd02);
    ASSERT(cmd02->points.x[FPP_MIN].used);

    /* The relative_index should point to Cmd01, not the parent */
    sc2BaseFrame_t *cmd01 = find_frame(frames, count, "Cmd01");
    ASSERT_NOT_NULL(cmd01);
    ASSERT_EQ_INT(cmd02->points.x[FPP_MIN].relative_index, cmd01->number);

    SC2_LayoutShutdown();
}

/* =====================================================================
 * Group 3: Flatten / Frame Population Tests
 * ===================================================================== */

static void test_adapter_flatten_frame_count(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    SC2_LayoutGetFrames(&count);

    /* ConsoleUI root + ResourcePanel + MineralIcon + MineralCount +
     * InfoPanel + UnitName + Portrait + CommandArea + Cmd01 + Cmd02 +
     * Cmd03 + CenterAlert + HiddenPanel + HiddenChild = 14 */
    ASSERT_EQ_INT(count, 14);

    SC2_LayoutShutdown();
}

static void test_adapter_flatten_types_mapped(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* ConsoleUI (GameUI) → FT_FRAME */
    sc2BaseFrame_t *root = find_frame(frames, count, "ConsoleUI");
    ASSERT_NOT_NULL(root);
    ASSERT_EQ_INT(root->type, FT_FRAME);

    /* Cmd01 (CommandButton) → FT_FRAME: SC2 buttons are containers; their visual
     * appearance comes from child NormalImage/HoverImage frames (FT_TEXTURE).
     * FT_BUTTON on the client calls SCR_LayoutGlueTextButton which expects a
     * uiGlueTextButton_t buffer that SC2 buttons don't carry. */
    sc2BaseFrame_t *cmd = find_frame(frames, count, "Cmd01");
    ASSERT_NOT_NULL(cmd);
    ASSERT_EQ_INT(cmd->type, FT_FRAME);

    /* MineralIcon (Image) → FT_TEXTURE (2D image, not a 3D model sprite) */
    sc2BaseFrame_t *icon = find_frame(frames, count, "MineralIcon");
    ASSERT_NOT_NULL(icon);
    ASSERT_EQ_INT(icon->type, FT_TEXTURE);

    /* MineralCount (Label) → FT_TEXT */
    sc2BaseFrame_t *label = find_frame(frames, count, "MineralCount");
    ASSERT_NOT_NULL(label);
    ASSERT_EQ_INT(label->type, FT_TEXT);

    /* Portrait (Image) → FT_TEXTURE (2D image, not a 3D model sprite) */
    sc2BaseFrame_t *portrait = find_frame(frames, count, "Portrait");
    ASSERT_NOT_NULL(portrait);
    ASSERT_EQ_INT(portrait->type, FT_TEXTURE);

    SC2_LayoutShutdown();
}

static void test_adapter_flatten_hidden_flags(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* HiddenPanel should be hidden */
    sc2BaseFrame_t *hidden = find_frame(frames, count, "HiddenPanel");
    ASSERT_NOT_NULL(hidden);
    ASSERT(hidden->ui_flags & SC2_UIFLAG_HIDDEN);

    /* CenterAlert should be hidden (Visible val="false") */
    sc2BaseFrame_t *alert = find_frame(frames, count, "CenterAlert");
    ASSERT_NOT_NULL(alert);
    ASSERT(alert->ui_flags & SC2_UIFLAG_HIDDEN);

    /* ResourcePanel should NOT be hidden */
    sc2BaseFrame_t *panel = find_frame(frames, count, "ResourcePanel");
    ASSERT_NOT_NULL(panel);
    ASSERT(!(panel->ui_flags & SC2_UIFLAG_HIDDEN));

    SC2_LayoutShutdown();
}

static void test_adapter_flatten_color_alpha(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* ResourcePanel has Color val="255,255,255,200" */
    sc2BaseFrame_t *panel = find_frame(frames, count, "ResourcePanel");
    ASSERT_NOT_NULL(panel);
    ASSERT_EQ_INT(panel->color.r, 255);
    ASSERT_EQ_INT(panel->color.g, 255);
    ASSERT_EQ_INT(panel->color.b, 255);
    ASSERT_EQ_INT(panel->color.a, 200);

    /* Root ConsoleUI should default to white */
    sc2BaseFrame_t *root = find_frame(frames, count, "ConsoleUI");
    ASSERT_NOT_NULL(root);
    ASSERT_EQ_INT(root->color.r, 255);
    ASSERT_EQ_INT(root->color.g, 255);
    ASSERT_EQ_INT(root->color.b, 255);
    ASSERT_EQ_INT(root->color.a, 255);

    SC2_LayoutShutdown();
}

/* =====================================================================
 * Group 4: Screen Rect Pipeline Tests
 * ===================================================================== */

static void test_adapter_root_parent_is_scene(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* ConsoleUI root has Anchor relative="$parent" → parent_index == -1 */
    sc2BaseFrame_t *root = find_frame(frames, count, "ConsoleUI");
    ASSERT_NOT_NULL(root);
    ASSERT_EQ_INT(root->parent_index, (DWORD)-1);

    SC2_LayoutShutdown();
}

static void test_adapter_cross_frame_relative_index(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* Cmd03 references $parent/Cmd02 — relative should be Cmd02's index */
    sc2BaseFrame_t *cmd03 = find_frame(frames, count, "Cmd03");
    sc2BaseFrame_t *cmd02 = find_frame(frames, count, "Cmd02");
    ASSERT_NOT_NULL(cmd03);
    ASSERT_NOT_NULL(cmd02);
    ASSERT_EQ_INT(cmd03->points.x[FPP_MIN].relative_index, cmd02->number);

    /* MineralCount references $parent/MineralIcon — critical for correct label positioning.
     * If this relative_index is wrong, the label renders at the scene edge instead of
     * next to the icon, making text appear "not drawn" in the resource bar. */
    sc2BaseFrame_t *label = find_frame(frames, count, "MineralCount");
    sc2BaseFrame_t *icon  = find_frame(frames, count, "MineralIcon");
    ASSERT_NOT_NULL(label);
    ASSERT_NOT_NULL(icon);
    ASSERT_EQ_INT(label->points.x[FPP_MIN].relative_index, icon->number);

    SC2_LayoutShutdown();
}

static void test_adapter_hidden_flagged_for_skip(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* HiddenPanel and CenterAlert should have SC2_UIFLAG_HIDDEN set */
    sc2BaseFrame_t *hidden = find_frame(frames, count, "HiddenPanel");
    sc2BaseFrame_t *alert = find_frame(frames, count, "CenterAlert");
    ASSERT_NOT_NULL(hidden);
    ASSERT_NOT_NULL(alert);
    ASSERT(hidden->ui_flags & SC2_UIFLAG_HIDDEN);
    ASSERT(alert->ui_flags & SC2_UIFLAG_HIDDEN);

    /* MineralIcon should NOT have SC2_UIFLAG_HIDDEN */
    sc2BaseFrame_t *icon = find_frame(frames, count, "MineralIcon");
    ASSERT_NOT_NULL(icon);
    ASSERT(!(icon->ui_flags & SC2_UIFLAG_HIDDEN));

    SC2_LayoutShutdown();
}

/* sc2_type is set on flattened frames so fallback lookup by SC2 type works */
static void test_adapter_sc2_type_preserved(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    sc2BaseFrame_t *label = find_frame(frames, count, "MineralCount");
    ASSERT_NOT_NULL(label);
    ASSERT_EQ_INT(label->type, FT_TEXT);
    ASSERT_EQ_INT((int)label->sc2_type, (int)SC2_FRAMETYPE_LABEL);

    sc2BaseFrame_t *panel = find_frame(frames, count, "ResourcePanel");
    ASSERT_NOT_NULL(panel);
    ASSERT_EQ_INT(panel->type, FT_FRAME);
    ASSERT_EQ_INT((int)panel->sc2_type, (int)SC2_FRAMETYPE_FRAME);

    SC2_LayoutShutdown();
}

static int test_stub_font_index(LPCSTR name, DWORD size) {
    (void)name; (void)size;
    return 7; /* sentinel: any non-zero value */
}

/* label.font is non-zero when a FontIndex callback is wired up */
static void test_adapter_label_font_set_when_fontindex_wired(void) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    uiimport.FontIndex = test_stub_font_index;

    ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);
    sc2BaseFrame_t *label = find_frame(frames, count, "MineralCount");
    ASSERT_NOT_NULL(label);
    ASSERT_EQ_INT(label->type, FT_TEXT);
    ASSERT_EQ_INT((int)label->label.font, 7);

    uiimport.FontIndex = NULL;
    SC2_LayoutShutdown();
}

/* =====================================================================
 * Test runner
 * ===================================================================== */

void run_sc2_consoleui_tests(void) {
    /* Group 1: Parser bug regression */
    RUN_TEST(test_adapter_constant_hash_stripping);
    RUN_TEST(test_adapter_constant_offset_resolves);
    RUN_TEST(test_adapter_template_inheritance_ordering);
    RUN_TEST(test_adapter_cross_file_template_inheritance);

    /* Group 2: Anchor resolution */
    RUN_TEST(test_adapter_single_anchor_left_min);
    RUN_TEST(test_adapter_single_anchor_top_min);
    RUN_TEST(test_adapter_dual_anchor_stretch);
    RUN_TEST(test_adapter_mid_anchor);
    RUN_TEST(test_adapter_cross_frame_relative);

    /* Group 3: Flatten / frame population */
    RUN_TEST(test_adapter_flatten_frame_count);
    RUN_TEST(test_adapter_flatten_types_mapped);
    RUN_TEST(test_adapter_flatten_hidden_flags);
    RUN_TEST(test_adapter_flatten_color_alpha);
    RUN_TEST(test_adapter_sc2_type_preserved);
    RUN_TEST(test_adapter_label_font_set_when_fontindex_wired);

    /* Group 4: Screen rect pipeline */
    RUN_TEST(test_adapter_root_parent_is_scene);
    RUN_TEST(test_adapter_cross_frame_relative_index);
    RUN_TEST(test_adapter_hidden_flagged_for_skip);
}
