#include "test.h"
#include "cl_control_groups.h"

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
