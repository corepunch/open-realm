#include "test.h"
#include "cl_control_groups.h"
#include "../../client/ui_layout.h"

void SCR_LayoutDrawCommandButton(LPCUIFRAME frame, LPCRECT screen);
static BOOL button_glow;
static FLOAT button_radial_shade;
static void capture_button_glow(LPCDRAWIMAGE draw) { button_glow = draw->uActiveGlow; button_radial_shade = draw->uRadialShade; }

/* Test the renderer submission, including the shared sentinel and independent autocast flag. */
TEST(client_layout, command_glow_requires_an_ability_or_autocast) {
    void (*saved_draw)(LPCDRAWIMAGE) = re.DrawImageEx;
    DWORD saved_count = cl.num_entities;
    entityState_t saved_ent = cl.ents[0].current;
    uiFrame_t frame = { .flags.type = FT_COMMANDBUTTON, .stat = UINT8_MAX };
    RECT screen = { .w = 0.039f, .h = 0.039f };
    re.DrawImageEx = capture_button_glow;
    cl.num_entities = 1;
    cl.ents[0].current = (entityState_t){ .renderfx = RF_SELECTED, .ability = UINT8_MAX };
    SCR_LayoutDrawCommandButton(&frame, &screen);
    T_ASSERT(!button_glow);
    frame.stat = cl.ents[0].current.ability = 0;
    SCR_LayoutDrawCommandButton(&frame, &screen);
    T_ASSERT(button_glow);
    frame.stat = 1;
    SCR_LayoutDrawCommandButton(&frame, &screen);
    T_ASSERT(!button_glow);
    frame.flagsvalue = UIFLAG_ALTERNATE_ACTIVE;
    SCR_LayoutDrawCommandButton(&frame, &screen);
    T_ASSERT(button_glow);
    cl.num_entities = 0;
    frame.flagsvalue = 0;
    SCR_LayoutDrawCommandButton(&frame, &screen);
    T_ASSERT(!button_glow);
    re.DrawImageEx = saved_draw;
    cl.num_entities = saved_count;
    cl.ents[0].current = saved_ent;
}

TEST(client_layout, command_cooldown_uses_local_clock_for_radial_shade) {
    void (*saved_draw)(LPCDRAWIMAGE) = re.DrawImageEx;
    DWORD const saved_time = cl.time;
    uiCommandButton_t state = { .radialStartTime = 1000, .radialEndTime = 5000 };
    uiFrame_t frame = { .flags.type = FT_COMMANDBUTTON, .stat = UINT8_MAX };
    RECT screen = { .w = 0.039f, .h = 0.039f };

    frame.flagsvalue |= UIFLAG_RADIAL_SHADE;
    frame.buffer.data = &state;
    frame.buffer.size = sizeof(state);
    re.DrawImageEx = capture_button_glow;
    cl.time = 2000;
    SCR_LayoutDrawCommandButton(&frame, &screen);
    T_FEQ(button_radial_shade, 0.75f, 0.001f);
    frame.flagsvalue &= ~UIFLAG_RADIAL_SHADE;
    SCR_LayoutDrawCommandButton(&frame, &screen);
    T_FEQ(button_radial_shade, 0.0f, 0.001f);
    frame.flagsvalue |= UIFLAG_RADIAL_SHADE;
    cl.time = 5000;
    SCR_LayoutDrawCommandButton(&frame, &screen);
    T_FEQ(button_radial_shade, 0.0f, 0.001f);

    re.DrawImageEx = saved_draw;
    cl.time = saved_time;
}

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
