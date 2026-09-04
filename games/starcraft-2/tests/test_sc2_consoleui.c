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
#include "games/starcraft-2/menu/menu_layout.h"
#include "test.h"

/* Define the uiimport global that menu_layout.c references via extern */
extern uiImport_t uiimport;

#ifndef TEST_SC2_MPQ
#define TEST_SC2_MPQ "build/tests/test-sc2.SC2Maps"
#endif

static BOOL sc2_consoleui_tests_initialized;

static void setup_sc2_consoleui_tests(void) {
    if (sc2_consoleui_tests_initialized) return;

    LPCSTR argv[] = { "test_sc2_consoleui", "-config", "" };
    Com_Init(3, argv);
    T_ASSERT(FS_AddArchive(TEST_SC2_MPQ) != NULL);

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
TEST(sc2_consoleui, adapter_constant_hash_stripping) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    LPCSTR margin = SC2_LayoutResolveConstant("##HUDMargin");
    T_NOT_NULL(margin);
    T_STREQ(margin, "8");

    LPCSTR width = SC2_LayoutResolveConstant("##PanelWidth");
    T_NOT_NULL(width);
    T_STREQ(width, "200");

    /* Also verify the old TestConstants fixture still works */
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestConstants.SC2Layout"));
    LPCSTR red = SC2_LayoutResolveConstant("##TestColorRed");
    T_NOT_NULL(red);
    T_STREQ(red, "255,0,0");

    SC2_LayoutShutdown();
}

/* Bug 3: Constant resolution in anchor offsets */
TEST(sc2_consoleui, adapter_constant_offset_resolves) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    sc2Frame_t *panel = find_template("ResourcePanel");
    T_NOT_NULL(panel);

    /* ResourcePanel's Left anchor has offset="#HUDMargin" which should resolve to 8 */
    T_ASSERT(panel->num_anchors > 0);
    BOOL found_left = false;
    for (int i = 0; i < panel->num_anchors; i++) {
        if (panel->anchors[i].side == SC2_SIDE_LEFT) {
            T_EQ(panel->anchors[i].offset, 8);
            found_left = true;
            break;
        }
    }
    T_ASSERT(found_left);

    SC2_LayoutShutdown();
}

/* Bug 2: Template inheritance ordering (same-file templates) */
TEST(sc2_consoleui, adapter_template_inheritance_ordering) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    /* ActionButton01 inherits from TestButtonTemplate (300x75).
     * TestButtonTemplate has 3 anchors (Top+Min, Left+Min from base, Right+Max own).
     * ActionButton01 has 2 inline anchors (Top+Min, Left+Min) which override
     * the same sides from the template. Result: 3 anchors total. */
    sc2Frame_t *button = find_template("ActionButton01");
    T_NOT_NULL(button);
    T_ASSERT(button->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(button->width, 300.0f, 0.01f);
    T_ASSERT(button->flags & SC2_FRAME_HAS_HEIGHT);
    T_FEQ(button->height, 75.0f, 0.01f);

    /* Should have 3 anchors (2 template sides overridden + 1 template side kept) */
    T_EQ(button->num_anchors, 3);

    SC2_LayoutShutdown();
}

/* Bug 4: Cross-file template inheritance */
TEST(sc2_consoleui, adapter_cross_file_template_inheritance) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestGameUI.SC2Layout"));

    /* IncludedPanel inherits from IncludedFrame in TestIncluded.SC2Layout */
    sc2Frame_t *panel = find_template("IncludedPanel");
    T_NOT_NULL(panel);
    T_ASSERT(panel->flags & SC2_FRAME_HAS_WIDTH);
    T_FEQ(panel->width, 100.0f, 0.01f);
    T_ASSERT(panel->flags & SC2_FRAME_HAS_HEIGHT);
    T_FEQ(panel->height, 50.0f, 0.01f);
    T_ASSERT(panel->flags & SC2_FRAME_HAS_VISIBLE);
    T_ASSERT(panel->flags & SC2_FRAME_VISIBLE);

    SC2_LayoutShutdown();
}

/* =====================================================================
 * Group 2: Anchor Resolution Tests
 * ===================================================================== */

TEST(sc2_consoleui, adapter_single_anchor_left_min) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);
    T_ASSERT(count > 0);

    /* MineralIcon has Left+Min anchor */
    sc2BaseFrame_t *icon = find_frame(frames, count, "MineralIcon");
    T_NOT_NULL(icon);
    T_ASSERT(icon->points.x[FPP_MIN].used);
    T_EQ(icon->points.x[FPP_MIN].targetPos, FPP_MIN);

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_single_anchor_top_min) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* MineralIcon has Top+Min anchor */
    sc2BaseFrame_t *icon = find_frame(frames, count, "MineralIcon");
    T_NOT_NULL(icon);
    T_ASSERT(icon->points.y[FPP_MIN].used);
    T_EQ(icon->points.y[FPP_MIN].targetPos, FPP_MIN);

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_dual_anchor_stretch) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* ResourcePanel has Top+Max and Bottom+Max → y-axis dual anchor */
    sc2BaseFrame_t *panel = find_frame(frames, count, "ResourcePanel");
    T_NOT_NULL(panel);
    T_ASSERT(panel->points.y[FPP_MIN].used); /* Top+Max maps to y[FPP_MIN] with pos=Max */
    T_ASSERT(panel->points.y[FPP_MAX].used); /* Bottom+Max maps to y[FPP_MAX] with pos=Max */

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_mid_anchor) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* CenterAlert has Left+Mid and Top+Mid.
     * Left → x[FPP_MIN] (element's left edge), pos=Mid → targetPos=FPP_MID (parent center).
     * Top  → y[FPP_MIN] (element's top edge),  pos=Mid → targetPos=FPP_MID (parent center). */
    sc2BaseFrame_t *alert = find_frame(frames, count, "CenterAlert");
    T_NOT_NULL(alert);
    T_ASSERT(alert->points.x[FPP_MIN].used);
    T_ASSERT(alert->points.x[FPP_MIN].targetPos == FPP_MID);
    T_ASSERT(alert->points.y[FPP_MIN].used);
    T_ASSERT(alert->points.y[FPP_MIN].targetPos == FPP_MID);

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_cross_frame_relative) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* Cmd02's Left anchor references $parent/Cmd01 */
    sc2BaseFrame_t *cmd02 = find_frame(frames, count, "Cmd02");
    T_NOT_NULL(cmd02);
    T_ASSERT(cmd02->points.x[FPP_MIN].used);

    /* The relative_index should point to Cmd01, not the parent */
    sc2BaseFrame_t *cmd01 = find_frame(frames, count, "Cmd01");
    T_NOT_NULL(cmd01);
    T_EQ(cmd02->points.x[FPP_MIN].relative_index, cmd01->number);

    SC2_LayoutShutdown();
}

/* =====================================================================
 * Group 3: Flatten / Frame Population Tests
 * ===================================================================== */

TEST(sc2_consoleui, adapter_flatten_frame_count) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    SC2_LayoutGetFrames(&count);

    /* ConsoleUI root + ResourcePanel + MineralIcon + MineralCount +
     * InfoPanel + UnitName + Portrait + CommandArea + Cmd01 + Cmd02 +
     * Cmd03 + CenterAlert + HiddenPanel + HiddenChild = 14 */
    T_EQ(count, 14);

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_flatten_types_mapped) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* ConsoleUI (GameUI) → FT_FRAME */
    sc2BaseFrame_t *root = find_frame(frames, count, "ConsoleUI");
    T_NOT_NULL(root);
    T_EQ(root->type, FT_FRAME);

    /* Cmd01 (CommandButton) → FT_FRAME: SC2 buttons are containers; their visual
     * appearance comes from child NormalImage/HoverImage frames (FT_TEXTURE).
     * FT_BUTTON on the client calls SCR_LayoutGlueTextButton which expects a
     * uiGlueTextButton_t buffer that SC2 buttons don't carry. */
    sc2BaseFrame_t *cmd = find_frame(frames, count, "Cmd01");
    T_NOT_NULL(cmd);
    T_EQ(cmd->type, FT_FRAME);

    /* MineralIcon (Image) → FT_TEXTURE (2D image, not a 3D model sprite) */
    sc2BaseFrame_t *icon = find_frame(frames, count, "MineralIcon");
    T_NOT_NULL(icon);
    T_EQ(icon->type, FT_TEXTURE);

    /* MineralCount (Label) → FT_TEXT */
    sc2BaseFrame_t *label = find_frame(frames, count, "MineralCount");
    T_NOT_NULL(label);
    T_EQ(label->type, FT_TEXT);

    /* Portrait (Image) → FT_TEXTURE (2D image, not a 3D model sprite) */
    sc2BaseFrame_t *portrait = find_frame(frames, count, "Portrait");
    T_NOT_NULL(portrait);
    T_EQ(portrait->type, FT_TEXTURE);

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_flatten_hidden_flags) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* HiddenPanel should be hidden */
    sc2BaseFrame_t *hidden = find_frame(frames, count, "HiddenPanel");
    T_NOT_NULL(hidden);
    T_ASSERT(hidden->ui_flags & SC2_UIFLAG_HIDDEN);

    /* CenterAlert should be hidden (Visible val="false") */
    sc2BaseFrame_t *alert = find_frame(frames, count, "CenterAlert");
    T_NOT_NULL(alert);
    T_ASSERT(alert->ui_flags & SC2_UIFLAG_HIDDEN);

    /* ResourcePanel should NOT be hidden */
    sc2BaseFrame_t *panel = find_frame(frames, count, "ResourcePanel");
    T_NOT_NULL(panel);
    T_ASSERT(!(panel->ui_flags & SC2_UIFLAG_HIDDEN));

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_flatten_color_alpha) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* ResourcePanel has Color val="255,255,255,200" */
    sc2BaseFrame_t *panel = find_frame(frames, count, "ResourcePanel");
    T_NOT_NULL(panel);
    T_EQ(panel->color.r, 255);
    T_EQ(panel->color.g, 255);
    T_EQ(panel->color.b, 255);
    T_EQ(panel->color.a, 200);

    /* Root ConsoleUI should default to white */
    sc2BaseFrame_t *root = find_frame(frames, count, "ConsoleUI");
    T_NOT_NULL(root);
    T_EQ(root->color.r, 255);
    T_EQ(root->color.g, 255);
    T_EQ(root->color.b, 255);
    T_EQ(root->color.a, 255);

    SC2_LayoutShutdown();
}

/* =====================================================================
 * Group 4: Screen Rect Pipeline Tests
 * ===================================================================== */

TEST(sc2_consoleui, adapter_root_parent_is_scene) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* ConsoleUI root has Anchor relative="$parent" → parent_index == -1 */
    sc2BaseFrame_t *root = find_frame(frames, count, "ConsoleUI");
    T_NOT_NULL(root);
    T_EQ(root->parent_index, (DWORD)-1);

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_cross_frame_relative_index) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* Cmd03 references $parent/Cmd02 — relative should be Cmd02's index */
    sc2BaseFrame_t *cmd03 = find_frame(frames, count, "Cmd03");
    sc2BaseFrame_t *cmd02 = find_frame(frames, count, "Cmd02");
    T_NOT_NULL(cmd03);
    T_NOT_NULL(cmd02);
    T_EQ(cmd03->points.x[FPP_MIN].relative_index, cmd02->number);

    /* MineralCount references $parent/MineralIcon — critical for correct label positioning.
     * If this relative_index is wrong, the label renders at the scene edge instead of
     * next to the icon, making text appear "not drawn" in the resource bar. */
    sc2BaseFrame_t *label = find_frame(frames, count, "MineralCount");
    sc2BaseFrame_t *icon  = find_frame(frames, count, "MineralIcon");
    T_NOT_NULL(label);
    T_NOT_NULL(icon);
    T_EQ(label->points.x[FPP_MIN].relative_index, icon->number);

    SC2_LayoutShutdown();
}

TEST(sc2_consoleui, adapter_hidden_flagged_for_skip) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    /* HiddenPanel and CenterAlert should have SC2_UIFLAG_HIDDEN set */
    sc2BaseFrame_t *hidden = find_frame(frames, count, "HiddenPanel");
    sc2BaseFrame_t *alert = find_frame(frames, count, "CenterAlert");
    T_NOT_NULL(hidden);
    T_NOT_NULL(alert);
    T_ASSERT(hidden->ui_flags & SC2_UIFLAG_HIDDEN);
    T_ASSERT(alert->ui_flags & SC2_UIFLAG_HIDDEN);

    /* MineralIcon should NOT have SC2_UIFLAG_HIDDEN */
    sc2BaseFrame_t *icon = find_frame(frames, count, "MineralIcon");
    T_NOT_NULL(icon);
    T_ASSERT(!(icon->ui_flags & SC2_UIFLAG_HIDDEN));

    SC2_LayoutShutdown();
}

/* sc2_type is set on flattened frames so fallback lookup by SC2 type works */
TEST(sc2_consoleui, adapter_sc2_type_preserved) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);

    sc2BaseFrame_t *label = find_frame(frames, count, "MineralCount");
    T_NOT_NULL(label);
    T_EQ(label->type, FT_TEXT);
    T_EQ((int)label->sc2_type, (int)SC2_FRAMETYPE_LABEL);

    sc2BaseFrame_t *panel = find_frame(frames, count, "ResourcePanel");
    T_NOT_NULL(panel);
    T_EQ(panel->type, FT_FRAME);
    T_EQ((int)panel->sc2_type, (int)SC2_FRAMETYPE_FRAME);

    SC2_LayoutShutdown();
}

static int test_stub_font_index(LPCSTR name, DWORD size) {
    (void)name; (void)size;
    return 7; /* sentinel: any non-zero value */
}

/* label.font is non-zero when a FontIndex callback is wired up */
TEST(sc2_consoleui, adapter_label_font_set_when_fontindex_wired) {
    setup_sc2_consoleui_tests();
    SC2_LayoutInit();
    uiimport.FontIndex = test_stub_font_index;

    T_ASSERT(SC2_LayoutParseFile("UI/Layout/TestAdapter.SC2Layout"));
    T_ASSERT(SC2_LayoutFlatten("ConsoleUI"));

    DWORD count = 0;
    sc2BaseFrame_t *frames = SC2_LayoutGetFrames(&count);
    sc2BaseFrame_t *label = find_frame(frames, count, "MineralCount");
    T_NOT_NULL(label);
    T_EQ(label->type, FT_TEXT);
    T_EQ((int)label->label.font, 7);

    uiimport.FontIndex = NULL;
    SC2_LayoutShutdown();
}

/* =====================================================================
 * Test runner
 * ===================================================================== */

