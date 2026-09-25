#include "test.h"
#include "cl_control_groups.h"
#include "../../client/ui_layout.h"

void CL_ParseLayout(sizeBuf_t *msg);

void test_client_stubs_set_window_size(DWORD width, DWORD height);

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

/* Resize the same client: authored console edges and pointer coordinates must agree. */
TEST(client_layout, wc3_canvas_fills_window_at_every_aspect) {
    size2_t saved = re.GetWindowSize();
    size2_t sizes[] = { {1024,768}, {1920,1200}, {1920,1080}, {3440,1440}, {720,1280}, {1024,768} };
    FOR_LOOP(i, sizeof(sizes) / sizeof(sizes[0])) {
        test_client_stubs_set_window_size(sizes[i].width, sizes[i].height);
        RECT root = SCR_LayoutSceneRect();
        VECTOR2 corner = SCR_ScreenToUI(sizes[i].width, sizes[i].height);
        VECTOR2 middle = SCR_ScreenToUI(sizes[i].width / 2, sizes[i].height / 2);
        T_FEQ(root.x, 0, 0.0001f); T_FEQ(root.y, 0, 0.0001f);
        T_FEQ(root.w, 0.8f, 0.0001f); T_FEQ(root.h, 0.6f, 0.0001f);
        T_FEQ(SCR_UICanvasWidth(), root.w, 0.0001f);
        T_FEQ(corner.x, root.w, 0.0001f); T_FEQ(corner.y, root.h, 0.0001f);
        T_FEQ(middle.x, 0.4f, 0.0001f); T_FEQ(middle.y, 0.3f, 0.0001f);
    }
    test_client_stubs_set_window_size(0, 0);
    VECTOR2 point = SCR_ScreenToUI(100, 100);
    T_FEQ(point.x, 0, 0.0001f); T_FEQ(point.y, 0, 0.0001f);
    T_FEQ(SCR_UICanvasWidth(), 0.8f, 0.0001f);
    test_client_stubs_set_window_size(saved.width, saved.height);
}

TEST(client_layout, wc3_console_edges_capture_input_after_resize) {
    size2_t saved = re.GetWindowSize();
    DWORD flags = cl.playerstate.uiflags;
    BYTE packet[1024]; sizeBuf_t msg;
    UIFRAME empty = {0};
    cl.playerstate.uiflags = 0;
    SZ_Init(&msg, packet, sizeof(packet));
    MSG_WriteByte(&msg, LAYER_CONSOLE);
    FOR_LOOP(i, 2) {
        UIFRAME frame = { .number = i + 1, .flags.type = FT_TEXTURE, .size = {0.16f, 0.15f} };
        int edge = i ? FPP_MAX : FPP_MIN;
        frame.points.x[edge] = MAKE(uiFramePoint_t, .used = 1, .targetPos = edge);
        frame.points.y[FPP_MAX] = MAKE(uiFramePoint_t, .used = 1, .targetPos = FPP_MAX);
        MSG_WriteDeltaUIFrame(&msg, &empty, &frame, true);
        MSG_WriteByte(&msg, 0);
    }
    MSG_WriteLong(&msg, 0); MSG_WriteShort(&msg, 0);
    CL_ParseLayout(&msg);
    size2_t sizes[] = { {1024,768}, {1920,1200}, {1920,1080}, {3440,1440}, {1024,768} };
    FOR_LOOP(i, sizeof(sizes) / sizeof(sizes[0])) {
        int w = sizes[i].width, h = sizes[i].height;
        test_client_stubs_set_window_size(w, h);
        T_ASSERT(SCR_LayoutHitTest(1, h - 1));
        T_ASSERT(SCR_LayoutHitTest(w - 1, h - 1));
        T_ASSERT(SCR_LayoutHitTest(w / 10, h * 9 / 10));
        T_ASSERT(SCR_LayoutHitTest(w * 9 / 10, h * 9 / 10));
        T_ASSERT(!SCR_LayoutHitTest(w / 2, h / 2));
        T_ASSERT(!SCR_LayoutHitTest(w / 2, h - 1));
    }
    SCR_ClearLayoutLayer(LAYER_CONSOLE);
    cl.playerstate.uiflags = flags;
    test_client_stubs_set_window_size(saved.width, saved.height);
}

TEST(client_layout, wc3_world_projection_matches_pointer_canvas) {
    viewDef_t saved_view = cl.viewDef;
    size2_t saved = re.GetWindowSize();
    size2_t sizes[] = { {1024,768}, {1920,1200}, {1920,1080}, {3440,1440} };
    Matrix4_identity(&cl.viewDef.viewProjectionMatrix);
    cl.viewDef.viewport = cl.viewDef.scissor = MAKE(RECT, 0, 0.22f, 1, 0.76f);
    FOR_LOOP(i, sizeof(sizes) / sizeof(sizes[0])) {
        VECTOR2 screen;
        test_client_stubs_set_window_size(sizes[i].width, sizes[i].height);
        T_ASSERT(SCR_ProjectWorldPoint(&MAKE(VECTOR3, 0, 0, 0), &screen));
        T_FEQ(screen.x, 0.4f, 0.0001f); T_FEQ(screen.y, 0.24f, 0.0001f);
        T_ASSERT(SCR_ProjectWorldPoint(&MAKE(VECTOR3, 1, 0, 0), &screen));
        T_FEQ(screen.x, SCR_ScreenToUI(sizes[i].width, 0).x, 0.0001f);
        T_ASSERT(!SCR_ProjectWorldPoint(&MAKE(VECTOR3, 1.1f, 0, 0), &screen));
    }
    cl.viewDef = saved_view;
    test_client_stubs_set_window_size(saved.width, saved.height);
}
