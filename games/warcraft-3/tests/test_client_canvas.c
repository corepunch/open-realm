/*
 * test_client_canvas.c — client UI canvas: policy resolution, pointer/layout agreement, renderer push,
 * settle-gated class commits and the begin handshake (docs/architecture/ui-canvas.md).
 */
#include "test.h"
#include "../../client/ui_layout.h"

void CL_ParseLayout(sizeBuf_t *msg);
void test_client_stubs_init(void);
void test_client_stubs_set_window_size(DWORD width, DWORD height);
void test_client_stubs_set_canvas_policy(UICANVASPOLICY policy);
RECT test_client_stubs_ui_scene(void);
extern char test_forwarded_command[128];

#define SETTLE_MS 250 // milliseconds; mirrors CL_CANVAS_SETTLE_MS so the tests document the contract they pin

static BOOL rect_eq(RECT a, RECT b) {
    return fabsf(a.x - b.x) < 0.0001f && fabsf(a.y - b.y) < 0.0001f &&
           fabsf(a.w - b.w) < 0.0001f && fabsf(a.h - b.h) < 0.0001f;
}

/* Console tile anchored on x edge `edge` with an authored offset; retail hangs its wide tiles 0.256 outside
 * the root. */
static void write_console_tile(sizeBuf_t *msg, DWORD number, int edge, FLOAT offset) {
    UIFRAME empty = {0};
    UIFRAME frame = { .number = number, .flags.type = FT_TEXTURE, .size = {0.256f, 0.176f} };
    SHORT wire = (SHORT)(offset * UI_FRAMEPOINT_SCALE);
    frame.points.x[edge] = MAKE(uiFramePoint_t, .used = 1, .targetPos = edge, .offset = wire);
    frame.points.y[FPP_MAX] = MAKE(uiFramePoint_t, .used = 1, .targetPos = FPP_MAX);
    MSG_WriteDeltaUIFrame(msg, &empty, &frame, true);
    MSG_WriteByte(msg, 0);
}

/* Retail 1.30+ console: 4:3 tiles on both root edges plus extension tiles hanging outside the root. */
static void parse_retail_console(void) {
    BYTE packet[1024]; sizeBuf_t msg;
    SZ_Init(&msg, packet, sizeof(packet));
    MSG_WriteByte(&msg, LAYER_CONSOLE);
    write_console_tile(&msg, 1, FPP_MIN, 0.0f);
    write_console_tile(&msg, 2, FPP_MAX, 0.0f);
    write_console_tile(&msg, 3, FPP_MIN, -0.256f);
    write_console_tile(&msg, 4, FPP_MAX, 0.256f);
    MSG_WriteLong(&msg, 0); MSG_WriteShort(&msg, 0);
    CL_ParseLayout(&msg);
}

TEST(client_canvas, stretch_policy_keeps_the_authored_scene_at_every_aspect) {
    size2_t sizes[] = { {1024,768}, {1920,1200}, {1920,1080}, {3440,1440}, {720,1280}, {1024,768} };
    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_STRETCH);
    FOR_LOOP(i, sizeof(sizes) / sizeof(sizes[0])) {
        test_client_stubs_set_window_size(sizes[i].width, sizes[i].height);
        LPCUICANVAS canvas = CL_Canvas();
        VECTOR2 corner = SCR_ScreenToUI(sizes[i].width, sizes[i].height);
        T_ASSERT(rect_eq(canvas->scene, MAKE(RECT, 0, 0, 0.8f, 0.6f)));
        T_ASSERT(rect_eq(SCR_LayoutSceneRect(), canvas->scene));
        T_ASSERT(rect_eq(test_client_stubs_ui_scene(), canvas->scene));
        T_EQ(canvas->chrome, UI_CANVAS_STANDARD);
        T_FEQ(SCR_UICanvasWidth(), 0.8f, 0.0001f);
        T_FEQ(corner.x, 0.8f, 0.0001f); T_FEQ(corner.y, 0.6f, 0.0001f);
    }
}

TEST(client_canvas, expand_center_widens_the_scene_and_centers_the_hud_root) {
    struct { size2_t size; FLOAT width, root_x; UICANVASCLASS chrome; } cases[] = {
        { {1024,768}, 0.8f, 0.0f, UI_CANVAS_STANDARD },
        { {1280,1024}, 0.8f, 0.0f, UI_CANVAS_STANDARD },
        { {720,1280}, 0.8f, 0.0f, UI_CANVAS_STANDARD },
        { {1920,1200}, 0.96f, 0.08f, UI_CANVAS_WIDE },
        { {1280,720}, 1.0666667f, 0.1333333f, UI_CANVAS_WIDE },
        { {2560,1080}, 1.4222222f, 0.3111111f, UI_CANVAS_WIDE },
        { {3840,1080}, 2.1333334f, 0.6666667f, UI_CANVAS_WIDE },
    };
    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    FOR_LOOP(i, sizeof(cases) / sizeof(cases[0])) {
        test_client_stubs_set_window_size(cases[i].size.width, cases[i].size.height);
        LPCUICANVAS canvas = CL_Canvas();
        VECTOR2 middle = SCR_ScreenToUI(cases[i].size.width / 2, cases[i].size.height / 2);
        T_ASSERT(rect_eq(canvas->scene, MAKE(RECT, 0, 0, cases[i].width, 0.6f)));
        T_ASSERT(rect_eq(canvas->root, MAKE(RECT, cases[i].root_x, 0, 0.8f, 0.6f)));
        T_ASSERT(rect_eq(SCR_LayoutSceneRect(), canvas->root));
        T_ASSERT(rect_eq(test_client_stubs_ui_scene(), canvas->scene));
        T_EQ(canvas->chrome, cases[i].chrome);
        T_FEQ(middle.x, cases[i].width * 0.5f, 0.0001f); T_FEQ(middle.y, 0.3f, 0.0001f);
    }
}

TEST(client_canvas, expand_policy_gives_the_hud_root_the_whole_widened_scene) {
    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND);
    test_client_stubs_set_window_size(1280, 720);
    T_ASSERT(rect_eq(CL_Canvas()->scene, MAKE(RECT, 0, 0, 1.0666667f, 0.6f)));
    T_ASSERT(rect_eq(SCR_LayoutSceneRect(), CL_Canvas()->scene));
    T_EQ(CL_Canvas()->chrome, UI_CANVAS_STANDARD);
}

TEST(client_canvas, zero_sized_window_keeps_the_last_canvas) {
    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    test_client_stubs_set_window_size(1280, 720);
    UICANVAS before = *CL_Canvas();
    test_client_stubs_set_window_size(0, 0);
    test_client_stubs_set_window_size(1280, 0);
    T_ASSERT(rect_eq(CL_Canvas()->scene, before.scene));
    T_EQ(CL_Canvas()->window.width, 1280); T_EQ(CL_Canvas()->window.height, 720);
    T_FEQ(SCR_ScreenToUI(1280, 720).x, before.scene.w, 0.0001f);
    CL_CanvasFrame(SETTLE_MS);
    CL_CanvasFrame(SETTLE_MS * 2);
    T_EQ(CL_CanvasSettledChrome(), UI_CANVAS_WIDE);
}

/* Retail tiles cover 0.8 + 2 * 0.256 = 1.312 scene units, so extension chrome reaches the window edge up to
 * about 2.19:1; a 21:9 window keeps the authored gap beyond the tiles, exactly like retail 1.30+. */
TEST(client_canvas, console_edges_capture_input_under_both_policies) {
    size2_t sizes[] = { {1024,768}, {1920,1200}, {1920,1080}, {2560,1200}, {1024,768} };
    UICANVASPOLICY policies[] = { UI_CANVAS_STRETCH, UI_CANVAS_EXPAND_CENTER };
    test_client_stubs_init();
    cl.playerstate.uiflags = 0;
    parse_retail_console();
    FOR_LOOP(p, 2) {
        test_client_stubs_set_canvas_policy(policies[p]);
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
    }
    /* Under the stretched policy the extension tiles sit outside the scene: only the 4:3 tiles are reachable. */
    test_client_stubs_set_canvas_policy(UI_CANVAS_STRETCH);
    test_client_stubs_set_window_size(3440, 1440);
    T_ASSERT(SCR_LayoutHitTest(1, 1439) && SCR_LayoutHitTest(3439, 1439));
    /* Under the centered policy the 4:3 tiles end at the root edges; the extension tiles reach 0.256 further. */
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    test_client_stubs_set_window_size(3440, 1440);
    RECT root = SCR_LayoutSceneRect();
    FLOAT scale = 3440.0f / CL_Canvas()->scene.w;
    T_ASSERT(!SCR_LayoutHitTest(1, 1439) && !SCR_LayoutHitTest(3439, 1439));
    T_ASSERT(SCR_LayoutHitTest((int)((root.x - 0.256f) * scale) + 2, 1439));
    T_ASSERT(!SCR_LayoutHitTest((int)((root.x - 0.256f) * scale) - 2, 1439));
    T_ASSERT(SCR_LayoutHitTest((int)((root.x + 0.8f + 0.256f) * scale) - 2, 1439));
    T_ASSERT(!SCR_LayoutHitTest((int)((root.x + 0.8f + 0.256f) * scale) + 2, 1439));
    SCR_ClearLayoutLayer(LAYER_CONSOLE);
}

TEST(client_canvas, world_projection_matches_the_pointer_canvas) {
    size2_t sizes[] = { {1024,768}, {1920,1200}, {1920,1080}, {3440,1440} };
    UICANVASPOLICY policies[] = { UI_CANVAS_STRETCH, UI_CANVAS_EXPAND_CENTER, UI_CANVAS_EXPAND };
    test_client_stubs_init();
    Matrix4_identity(&cl.viewDef.viewProjectionMatrix);
    cl.viewDef.viewport = cl.viewDef.scissor = MAKE(RECT, 0, 0.22f, 1, 0.76f);
    FOR_LOOP(p, 3) {
        test_client_stubs_set_canvas_policy(policies[p]);
        FOR_LOOP(i, sizeof(sizes) / sizeof(sizes[0])) {
            VECTOR2 screen;
            test_client_stubs_set_window_size(sizes[i].width, sizes[i].height);
            T_ASSERT(SCR_ProjectWorldPoint(&MAKE(VECTOR3, 0, 0, 0), &screen));
            T_FEQ(screen.x, CL_Canvas()->scene.w * 0.5f, 0.0001f); T_FEQ(screen.y, 0.24f, 0.0001f);
            T_ASSERT(SCR_ProjectWorldPoint(&MAKE(VECTOR3, 1, 0, 0), &screen));
            T_FEQ(screen.x, SCR_ScreenToUI(sizes[i].width, 0).x, 0.0001f);
            T_ASSERT(!SCR_ProjectWorldPoint(&MAKE(VECTOR3, 1.1f, 0, 0), &screen));
        }
    }
}

/* A drag across the 4:3 boundary re-lays out every frame but tells the server once, after the window holds still. */
TEST(client_canvas, class_commits_only_after_the_window_settles) {
    DWORD now = 0;
    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    /* Connected at 4:3: begin reported the standard class, then the world became active. */
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    CL_CanvasWriteChrome();
    cls.state = ca_active;
    CL_CanvasFrame(now);
    T_EQ(CL_CanvasSettledChrome(), UI_CANVAS_STANDARD);
    T_STREQ(test_forwarded_command, "");
    FOR_LOOP(i, 40) {
        now += 16;
        test_client_stubs_set_window_size(i & 1 ? 1280 : 1024, i & 1 ? 720 : 768);
        CL_CanvasFrame(now);
        T_EQ(CL_Canvas()->chrome, i & 1 ? UI_CANVAS_WIDE : UI_CANVAS_STANDARD);
        T_EQ(CL_CanvasSettledChrome(), UI_CANVAS_STANDARD);
        T_STREQ(test_forwarded_command, "");
    }
    /* The last drag step (1280x720) was observed at `now`; it settles exactly SETTLE_MS later. */
    test_client_stubs_set_window_size(1280, 720);
    CL_CanvasFrame(now + SETTLE_MS - 1);
    T_EQ(CL_CanvasSettledChrome(), UI_CANVAS_STANDARD);
    T_STREQ(test_forwarded_command, "");
    CL_CanvasFrame(now + SETTLE_MS);
    T_EQ(CL_CanvasSettledChrome(), UI_CANVAS_WIDE);
    T_STREQ(test_forwarded_command, "ui_canvas 1");
    test_forwarded_command[0] = '\0';
    now += SETTLE_MS * 4;
    CL_CanvasFrame(now);
    T_STREQ(test_forwarded_command, "");
    /* 16:10 is still wide: a settled resize inside one class costs no round trip. */
    test_client_stubs_set_window_size(1920, 1200);
    CL_CanvasFrame(now);
    CL_CanvasFrame(now += SETTLE_MS);
    T_STREQ(test_forwarded_command, "");
    test_client_stubs_set_window_size(1024, 768);
    CL_CanvasFrame(now);
    T_STREQ(test_forwarded_command, "");
    CL_CanvasFrame(now += SETTLE_MS);
    T_STREQ(test_forwarded_command, "ui_canvas 0");
    cls.state = ca_disconnected;
}

TEST(client_canvas, class_is_sent_when_the_connection_becomes_active) {
    DWORD now = 0;
    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    cls.state = ca_connected;
    test_client_stubs_set_window_size(1280, 720);
    CL_CanvasFrame(now);
    CL_CanvasFrame(now += SETTLE_MS);
    T_EQ(CL_CanvasSettledChrome(), UI_CANVAS_WIDE);
    T_STREQ(test_forwarded_command, "");
    cls.state = ca_active;
    CL_CanvasFrame(now + 16);
    T_STREQ(test_forwarded_command, "ui_canvas 1");
    cls.state = ca_disconnected;
}

/* The begin handshake authors the HUD from scratch, so it commits the live window without waiting to settle. */
TEST(client_canvas, begin_writes_the_live_class_ahead_of_the_begin_command) {
    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    cls.state = ca_connected;
    test_client_stubs_set_window_size(1280, 720);
    T_EQ(CL_CanvasSettledChrome(), UI_CANVAS_STANDARD);
    CL_CanvasWriteChrome();
    T_EQ(CL_CanvasSettledChrome(), UI_CANVAS_WIDE);
    cls.netchan.message.readcount = 0;
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    T_STREQ(MSG_ReadString2(&cls.netchan.message), "ui_canvas 1");
    cls.state = ca_active;
    CL_CanvasFrame(SETTLE_MS * 4);
    T_STREQ(test_forwarded_command, "");
    cls.state = ca_disconnected;
}

/* An edition switch re-resolves the policy with no window event; layout follows at once, the class after settling. */
TEST(client_canvas, policy_change_re_resolves_without_a_window_event) {
    test_client_stubs_init();
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, MAX_MSGLEN);
    test_client_stubs_set_window_size(1280, 720);
    CL_CanvasWriteChrome();
    cls.state = ca_active;
    CL_CanvasFrame(0);
    T_STREQ(test_forwarded_command, "");
    T_ASSERT(rect_eq(SCR_LayoutSceneRect(), MAKE(RECT, 0, 0, 0.8f, 0.6f)));
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    T_ASSERT(rect_eq(SCR_LayoutSceneRect(), MAKE(RECT, 0.1333333f, 0, 0.8f, 0.6f)));
    T_ASSERT(rect_eq(test_client_stubs_ui_scene(), CL_Canvas()->scene));
    CL_CanvasFrame(SETTLE_MS * 2);
    T_STREQ(test_forwarded_command, "ui_canvas 1");
    test_client_stubs_set_canvas_policy(UI_CANVAS_STRETCH);
    CL_CanvasFrame(SETTLE_MS * 4);
    T_STREQ(test_forwarded_command, "ui_canvas 0");
    cls.state = ca_disconnected;
}
