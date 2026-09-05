#include "test.h"
#include "cl_control_groups.h"
#include "../../client/ui_layout.h"

void test_client_stubs_set_window_size(DWORD width, DWORD height);

TEST(client_groups, append_preserves_existing_order_and_deduplicates) {
    DWORD group[6] = { 10, 20 };
    DWORD incoming[] = { 20, 30, 10, 40 };
    DWORD count = CL_ControlGroupAppendUnique(group, 2, 6, incoming, 4);

    T_EQ(count, 4);
    T_EQ(group[0], 10);
    T_EQ(group[1], 20);
    T_EQ(group[2], 30);
    T_EQ(group[3], 40);
}

TEST(client_groups, append_to_empty_group_assigns_current_selection) {
    DWORD group[4] = { 0 };
    DWORD incoming[] = { 7, 8 };
    DWORD count = CL_ControlGroupAppendUnique(group, 0, 4, incoming, 2);

    T_EQ(count, 2);
    T_EQ(group[0], 7);
    T_EQ(group[1], 8);
}

TEST(client_groups, append_keeps_existing_members_when_capacity_is_reached) {
    DWORD group[4] = { 1, 2, 3 };
    DWORD incoming[] = { 2, 4, 5 };
    DWORD count = CL_ControlGroupAppendUnique(group, 3, 4, incoming, 3);

    T_EQ(count, 4);
    T_EQ(group[0], 1);
    T_EQ(group[1], 2);
    T_EQ(group[2], 3);
    T_EQ(group[3], 4);
}

TEST(client_layout, wc3_hud_root_centers_on_widescreen) {
    RECT root;

    test_client_stubs_set_window_size(1280, 720);
    root = SCR_LayoutSceneRect();
    T_ASSERT(fabsf(root.x - 0.133333f) < 0.0001f);
    T_ASSERT(fabsf(root.y) < 0.0001f);
    T_ASSERT(fabsf(root.w - UI_BASE_WIDTH) < 0.0001f);
    T_ASSERT(fabsf(root.h - UI_BASE_HEIGHT) < 0.0001f);
}

TEST(client_layout, wc3_hud_root_fills_authored_scene_at_four_three) {
    RECT root;

    test_client_stubs_set_window_size(1024, 768);
    root = SCR_LayoutSceneRect();
    T_ASSERT(fabsf(root.x) < 0.0001f);
    T_ASSERT(fabsf(root.w - UI_BASE_WIDTH) < 0.0001f);
    T_ASSERT(fabsf(root.h - UI_BASE_HEIGHT) < 0.0001f);
}
