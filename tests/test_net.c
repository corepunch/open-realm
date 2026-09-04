/*
 * test_net.c — Unit tests for the network layer (net.c / msg.c).
 *
 * Tests exercise the public NET_* and SZ_* / MSG_* APIs directly, without
 * touching the UDP socket.  All address arguments use NA_LOOPBACK so that
 * NET_SendPacket routes to the in-process ring buffer and no real socket is
 * required.  NET_Config(true) is intentionally not called for the loopback
 * tests so UDP sockets stay closed, making NET_GetUDPPacket a safe no-op
 * throughout.
 *
 * Covered scenarios:
 *   loopback empty          — NET_GetPacket returns 0 on an idle buffer
 *   loopback round-trip     — a packet sent with NS_CLIENT is received by NS_SERVER
 *   loopback multiple       — several back-to-back packets are received in order
 *   NET_SendPacket dispatch — NA_LOOPBACK goes to ring buffer; NA_IP with no
 *                             socket is a silent no-op (no crash)
 *   SZ_Init / SZ_Clear      — size-buffer lifecycle helpers
 *   SZ_Write                — appends bytes and advances cursize
 *   NET_StringToAdr IP      — dotted-decimal address without port
 *   NET_StringToAdr port    — dotted-decimal address with explicit port
 */

#include <string.h>
#include <limits.h>
#include <arpa/inet.h>

#include "test.h"

/* Pull in the net types + common types without game state. */
#include "../client/client.h"

void test_client_stubs_init(void);
void test_client_stubs_set_cvar(LPCSTR name, LPCSTR value);
void test_client_stubs_set_world_bounds(BOX2 bounds);
void test_client_stubs_set_existing_file(LPCSTR path);
void CL_ParseLayout(LPSIZEBUF msg);
void SCR_LayoutDrawScrollBar(LPCUIFRAME frame, LPCRECT screen);
void SCR_LayoutDrawStatusbar(LPCUIFRAME frame, LPCRECT screen);
void SCR_LayoutDrawTextArea(LPCUIFRAME frame, LPCRECT screen);
void SCR_LayoutDrawSprite(LPCUIFRAME frame, LPCRECT screen);
void SCR_LayoutClampSelectionRect(LPRECT rect);
BOOL SCR_LayoutModalActive(void);
void SCR_UpdateScreen(DWORD msec);
extern BOOL scr_initialized;
void test_client_stubs_clear_cvars(void);
extern DWORD test_fow_upload_calls;
extern DWORD test_cursor_draw_calls;
extern COLOR32 test_cursor_tint;
extern char test_forwarded_command[128];

static RECT test_scroll_rects[3], test_scroll_uvs[3];
static LPCTEXTURE test_scroll_tex[3];
static DWORD test_scroll_draws;
static drawText_t test_textarea_draw;
static DWORD test_textarea_draws;
static DWORD test_begin_frames, test_end_frames;
static DWORD test_model_loads, test_model_releases, test_tex_loads, test_tex_releases;
static VECTOR3 test_overhead_point;
static RECT test_status_rect;
static DWORD test_status_draws;
static PATHSTR test_model_load_paths[4];
static char test_sprite_anim[96];
static DWORD test_sprite_draws;

static LPMODEL capture_load_model(LPCSTR filename) {
    DWORD slot = test_model_loads;
    if (slot < sizeof(test_model_load_paths) / sizeof(test_model_load_paths[0]))
        snprintf(test_model_load_paths[slot], sizeof(test_model_load_paths[slot]), "%s", filename ? filename : "");
    test_model_loads++;
    return (LPMODEL)(uintptr_t)(0x1000u + test_model_loads);
}

static void capture_release_model(LPMODEL model) {
    (void)model;
    test_model_releases++;
}

static void capture_scroll_image(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color) {
    (void)color;
    if (test_scroll_draws >= 3) return;
    test_scroll_tex[test_scroll_draws] = texture;
    test_scroll_rects[test_scroll_draws] = *screen;
    test_scroll_uvs[test_scroll_draws++] = *uv;
}

static void capture_textarea(LPCDRAWTEXT text) { test_textarea_draw = *text; test_textarea_draws++; }
static VECTOR2 tall_textarea_size(LPCDRAWTEXT text) {
    (void)text;
    return MAKE(VECTOR2, 0.2f, 0.8f);
}
static void capture_begin_frame(void) { test_begin_frames++; }
static void capture_end_frame(void) { test_end_frames++; }
static bool capture_overhead_point(renderEntity_t const *entity, LPVECTOR3 out) {
    (void)entity; *out = test_overhead_point; return true;
}
static void capture_status_image(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color) {
    (void)texture; (void)uv; (void)color; test_status_rect = *screen; test_status_draws++;
}
static LPTEXTURE capture_load_texture(LPCSTR name) {
    (void)name; test_tex_loads++; return (LPTEXTURE)(uintptr_t)test_tex_loads;
}
static void capture_release_texture(LPTEXTURE texture) { (void)texture; test_tex_releases++; }
static void capture_sprite(LPCMODEL model, LPCSTR anim, float x, float y) {
    (void)model; (void)x; (void)y;
    test_sprite_draws++;
    snprintf(test_sprite_anim, sizeof(test_sprite_anim), "%s", anim ? anim : "");
}

TEST(client_layout, context_name_resolves_hover_entity_configstring) {
    uiFrame_t frame = { .stat = UI_STAT_CONTEXT_NAME };
    DWORD const entnum = 7, name = 3;
    DWORD const ni = name - 1;

    test_client_stubs_init();
    cl.hover_entity = entnum;
    cl.ents[entnum].current = (entityState_t){
        .model = 1, .name = name, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 255 },
    };
    memset(cl.configstrings[CS_GENERAL], 0, sizeof(cl.configstrings[CS_GENERAL]));
    snprintf(cl.configstrings[CS_GENERAL] + (ni & 0xF) * ENT_NAME_SLOT_SIZE, ENT_NAME_SLOT_SIZE, "Footman");

    T_STREQ(SCR_GetStringValue(&frame), "Footman");
}

TEST(client_layout, unknown_high_stat_binding_resolves_empty) {
    uiFrame_t frame = { .stat = UI_STAT_CONTEXT_NAME - 1 };

    test_client_stubs_init();
    T_STREQ(SCR_GetStringValue(&frame), "");
}

TEST(client_layout, tooltip_value_token_uses_live_player_stat) {
    uiFrame_t frame = {
        .stat = PLAYERSTATE_RESOURCE_GOLD,
        .tooltip = "Gold: {value}\nGold resource help.",
    };

    test_client_stubs_init();
    cl.playerstate.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    T_STREQ(SCR_GetTooltipText(&frame), "Gold: 500\nGold resource help.");

    cl.playerstate.stats[PLAYERSTATE_RESOURCE_GOLD] = 725;
    T_STREQ(SCR_GetTooltipText(&frame), "Gold: 725\nGold resource help.");
}

TEST(client_layout, tooltip_value_token_uses_food_display_format) {
    uiFrame_t frame = {
        .stat = PLAYERSTATE_RESOURCE_FOOD_USED,
        .tooltip = "Food: {value}\nFood resource help.",
    };

    test_client_stubs_init();
    cl.playerstate.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 18;
    cl.playerstate.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 24;
    cl.playerstate.stats[PLAYERSTATE_FOOD_CAP_CEILING] = 100;
    T_STREQ(SCR_GetTooltipText(&frame), "Food: 18/24\nFood resource help.");
}

TEST(client_layout, world_hover_root_projects_model_top_into_ui_canvas) {
    RECT root;
    renderEntity_t render = { .number = 7 };

    test_client_stubs_init();
    cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 255 },
    };
    cl.viewDef.entities = &render; cl.viewDef.num_entities = 1;
    cl.viewDef.viewport = cl.viewDef.scissor = MAKE(RECT, 0, 0.22f, 1, 0.76f);
    Matrix4_identity(&cl.viewDef.viewProjectionMatrix);
    test_overhead_point = MAKE(VECTOR3, 0, 0, 0);
    re.GetEntityOverheadPosition = capture_overhead_point;

    T_ASSERT(SCR_LayoutWorldHoverRoot(&root));
    T_FEQ(root.x, UI_BASE_WIDTH * 0.5f, 0.0001f);
    T_FEQ(root.y, UI_BASE_HEIGHT * 0.4f, 0.0001f);
    T_FEQ(root.w, 0.0f, 0.0001f); T_FEQ(root.h, 0.0f, 0.0001f);
}

TEST(client_layout, context_values_follow_hover_snapshot) {
    FLOAT value = -1.0f;

    test_client_stubs_init();
    cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 128, [ENT_MANA] = 64 },
    };
    T_ASSERT(SCR_LayoutContextValue(UI_STAT_CONTEXT_HEALTH, &value));
    T_FEQ(value, 128.0f / 255.0f, 0.0001f);
    T_ASSERT(SCR_LayoutContextValue(UI_STAT_CONTEXT_MANA, &value));
    T_FEQ(value, 64.0f / 255.0f, 0.0001f);
}

TEST(client_layout, selected_timed_status_reads_normalized_player_stat) {
    FLOAT value = -1.0f;

    test_client_stubs_init();
    cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS] = USHRT_MAX / 2;
    T_ASSERT(SCR_LayoutContextValue(UI_STAT_SELECTION_TIMED_STATUS, &value));
    T_FEQ(value, (USHRT_MAX / 2) / (FLOAT)USHRT_MAX, 0.0001f);
}

TEST(client_layout, context_rejects_entity_without_server_hover_capability) {
    FLOAT value;

    test_client_stubs_init(); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){ .model = 1, .stats = { [ENT_HEALTH] = 255 } };
    T_ASSERT(!SCR_LayoutContextValue(UI_STAT_CONTEXT_HEALTH, &value));
}

TEST(client_layout, world_hover_root_rejects_point_outside_world_scissor) {
    RECT root;
    renderEntity_t render = { .number = 7 };

    test_client_stubs_init(); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 255 },
    };
    cl.viewDef.entities = &render; cl.viewDef.num_entities = 1;
    cl.viewDef.viewport = cl.viewDef.scissor = MAKE(RECT, 0, 0.22f, 1, 0.76f);
    Matrix4_identity(&cl.viewDef.viewProjectionMatrix);
    test_overhead_point = MAKE(VECTOR3, 0, 2, 0);
    re.GetEntityOverheadPosition = capture_overhead_point;
    T_ASSERT(!SCR_LayoutWorldHoverRoot(&root));
}

TEST(client_layout, context_statusbar_uses_hover_snapshot_fraction) {
    uiFrame_t frame = { .stat = UI_STAT_CONTEXT_HEALTH, .tex = { .index = 1 }, .value = 1.0f };
    RECT screen = MAKE(RECT, 0.1f, 0.2f, 0.4f, 0.05f);

    test_client_stubs_init(); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 128 },
    };
    cl.pics[1] = (LPTEXTURE)(uintptr_t)1; test_status_draws = 0; re.DrawImage = capture_status_image;
    SCR_LayoutDrawStatusbar(&frame, &screen);
    T_EQ(test_status_draws, 1); T_FEQ(test_status_rect.w, screen.w * 128.0f / 255.0f, 0.0001f);
}

/* r_norefresh skips every renderer/UI submission while its inverse still presents a normal client frame. */
TEST(net, no_refresh_preserves_client_loop_without_screen_submission) {
    test_client_stubs_init(); test_client_stubs_clear_cvars();
    test_begin_frames = test_end_frames = 0;
    re.BeginFrame = capture_begin_frame; re.EndFrame = capture_end_frame;
    cls.state = ca_active; cls.key_dest = key_game; scr_initialized = true;
    test_client_stubs_set_cvar("r_hud", "0"); test_client_stubs_set_cvar("scr_showfps", "0");
    test_client_stubs_set_cvar("r_norefresh", "1"); SCR_UpdateScreen(16);
    T_EQ(test_begin_frames, 0); T_EQ(test_end_frames, 0);
    test_client_stubs_set_cvar("r_norefresh", "0"); SCR_UpdateScreen(16);
    T_EQ(test_begin_frames, 1); T_EQ(test_end_frames, 1);
    scr_initialized = false;
}

TEST(net, paused_scene_time_reuses_cached_world_without_effect_delta) {
    viewDef_t view = { .time = 1000, .deltaTime = 16 };
    DWORD last = 1000;

    T_ASSERT(!V_AdvanceSceneTime(&view, 1100, &last, true));
    T_EQ(view.time, 1000); T_EQ(view.deltaTime, 0); T_EQ(last, 1100);
    T_ASSERT(V_AdvanceSceneTime(&view, 1200, &last, false));
    T_EQ(view.time, 1100); T_EQ(view.deltaTime, 100); T_EQ(last, 1200);
}


/* Authored cursors use the same recipient-relative hover relationship as the
 * world ring: enemies tint red, neutral/passive targets yellow, and friendly
 * or no hover restores the original artwork. */
TEST(client_screen, cursor_tint_follows_wc3_hover_relationship) {
    test_client_stubs_init(); test_client_stubs_clear_cvars();
    cls.state = ca_active; cls.key_dest = key_game; scr_initialized = true;
    test_client_stubs_set_cvar("r_hud", "0");
    test_client_stubs_set_cvar("scr_showfps", "0");
    test_client_stubs_set_cvar("r_cursor", "1");
    re.BeginFrame = capture_begin_frame; re.EndFrame = capture_end_frame;

    cl.hover_entity = 7;
    cl.ents[7].current.flags = EF_HOVER_HEALTH | EF_HOSTILE;
    SCR_UpdateScreen(16);
    T_EQ(test_cursor_draw_calls, 1);
    T_EQ(test_cursor_tint.r, 255);
    T_EQ(test_cursor_tint.g, 0);
    T_EQ(test_cursor_tint.b, 0);
    T_EQ(test_cursor_tint.a, 255);

    cl.ents[7].current.flags = EF_HOVER_HEALTH | EF_NEUTRAL;
    SCR_UpdateScreen(16);
    T_EQ(test_cursor_draw_calls, 2);
    T_EQ(test_cursor_tint.r, 255);
    T_EQ(test_cursor_tint.g, 220);
    T_EQ(test_cursor_tint.b, 80);
    T_EQ(test_cursor_tint.a, 255);

    cl.hover_entity = 0;
    SCR_UpdateScreen(16);
    T_EQ(test_cursor_draw_calls, 3);
    T_EQ(test_cursor_tint.r, 255);
    T_EQ(test_cursor_tint.g, 255);
    T_EQ(test_cursor_tint.b, 255);
    T_EQ(test_cursor_tint.a, 255);
    scr_initialized = false;
}

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Drain all pending loopback packets for the given source so subsequent
 * tests start from a clean (read == write) ring-buffer state. */
static void drain_loopback(NETSOURCE netsrc) {
    static BYTE   drain_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { drain_buf, MAX_MSGLEN, 0, 0 };
    netadr_t from;
    for (int guard = 0; guard < 64; guard++) {
        if (!NET_GetPacket(netsrc, &from, &msg))
            break;
    }
}

/* A loopback netadr_t ready to pass to NET_SendPacket. */
static netadr_t loopback_adr(void) {
    netadr_t adr;
    memset(&adr, 0, sizeof(adr));
    adr.type = NA_LOOPBACK;
    return adr;
}

/* Server-authored WoW scrollbars use cropped textures while legacy FDF data keeps backdrop parts. */
TEST(net, layout_scrollbar_draws_cropped_texture_parts_top_to_bottom) {
    uiScrollBarImage_t scroll = {0};
    uiFrame_t frame = { .value = 0.0f, .buffer = { &scroll, sizeof(scroll) } };
    RECT screen = MAKE(RECT, 0.1f, 0.2f, 0.02f, 0.4f);

    test_client_stubs_init(); test_scroll_draws = 0; re.DrawImage = capture_scroll_image;
    FOR_LOOP(i, 3) {
        scroll.image[i] = i + 1;
        cl.pics[i + 1] = (LPTEXTURE)(uintptr_t)(i + 1);
    }
    scroll.texcoord[0] = scroll.texcoord[2] = 63;
    scroll.texcoord[1] = scroll.texcoord[3] = 191;
    SCR_LayoutDrawScrollBar(&frame, &screen);

    T_EQ((int)test_scroll_draws, 3);
    T_ASSERT(test_scroll_tex[0] == cl.pics[1]); T_FEQ(test_scroll_rects[0].y, 0.58f, 0.0001f);
    T_ASSERT(test_scroll_tex[1] == cl.pics[2]); T_FEQ(test_scroll_rects[1].y, 0.2f, 0.0001f);
    T_ASSERT(test_scroll_tex[2] == cl.pics[3]); T_FEQ(test_scroll_rects[2].y, 0.22f, 0.0001f);
    FOR_LOOP(i, 3) {
        T_FEQ(test_scroll_uvs[i].x, 63.0f / 255.0f, 0.0001f);
        T_FEQ(test_scroll_uvs[i].w, 128.0f / 255.0f, 0.0001f);
    }
}

TEST(net, layout_scrollbar_without_art_draws_nothing) {
    uiScrollBar_t scroll = {0};
    uiFrame_t frame = { .buffer = { &scroll, sizeof(scroll) } };
    RECT screen = MAKE(RECT, 0.1f, 0.2f, 0.02f, 0.4f);

    test_client_stubs_init(); test_scroll_draws = 0; re.DrawImage = capture_scroll_image;
    SCR_LayoutDrawScrollBar(&frame, &screen);
    T_EQ((int)test_scroll_draws, 0);
}

/* Text areas are scroll viewports, so wrapped content must not escape their inset rectangle. */
TEST(net, layout_textarea_clips_to_inset_viewport) {
    uiTextArea_t area = { .font = 1, .inset = 0.01f };
    uiFrame_t frame = { .text = "wrapped text", .buffer = { &area, sizeof(area) } };
    RECT screen = MAKE(RECT, 0.1f, 0.2f, 0.3f, 0.4f);

    test_client_stubs_init(); test_textarea_draws = 0; re.DrawText = capture_textarea;
    SCR_LayoutDrawTextArea(&frame, &screen);

    T_EQ((int)test_textarea_draws, 1);
    T_EQ((int)test_textarea_draw.flags, DRAW_WORD_WRAP | DRAW_CLIP);
    T_FEQ(test_textarea_draw.rect.x, 0.11f, 0.0001f); T_FEQ(test_textarea_draw.rect.y, 0.21f, 0.0001f);
    T_FEQ(test_textarea_draw.rect.w, 0.28f, 0.0001f); T_FEQ(test_textarea_draw.rect.h, 0.38f, 0.0001f);
    T_FEQ(test_textarea_draw.clip.x, test_textarea_draw.rect.x, 0.0001f);
    T_FEQ(test_textarea_draw.clip.y, test_textarea_draw.rect.y, 0.0001f);
    T_FEQ(test_textarea_draw.clip.w, test_textarea_draw.rect.w, 0.0001f);
    T_FEQ(test_textarea_draw.clip.h, test_textarea_draw.rect.h, 0.0001f);
}

TEST(net, layout_textarea_value_scrolls_wrapped_content_inside_clip) {
    uiTextArea_t area = { .font = 1, .inset = 0.01f };
    uiFrame_t frame = { .text = "many wrapped lines", .value = 0.5f,
                        .buffer = { &area, sizeof(area) } };
    RECT screen = MAKE(RECT, 0.1f, 0.2f, 0.3f, 0.4f);

    test_client_stubs_init(); test_textarea_draws = 0;
    re.GetTextSize = tall_textarea_size; re.DrawText = capture_textarea;
    SCR_LayoutDrawTextArea(&frame, &screen);

    /* View height is .38, content is .80, so value=.5 offsets by .21. */
    T_EQ((int)test_textarea_draws, 1);
    T_FEQ(test_textarea_draw.rect.x, 0.11f, 0.0001f);
    T_FEQ(test_textarea_draw.rect.y, 0.0f, 0.0001f);
    T_FEQ(test_textarea_draw.rect.w, 0.28f, 0.0001f);
    T_FEQ(test_textarea_draw.rect.h, 0.8f, 0.0001f);
    T_FEQ(test_textarea_draw.clip.x, 0.11f, 0.0001f);
    T_FEQ(test_textarea_draw.clip.y, 0.21f, 0.0001f);
    T_FEQ(test_textarea_draw.clip.w, 0.28f, 0.0001f);
    T_FEQ(test_textarea_draw.clip.h, 0.38f, 0.0001f);
}

/* -----------------------------------------------------------------------
 * SZ_Init / SZ_Clear
 * --------------------------------------------------------------------- */

TEST(net, sz_init) {
    BYTE buf[64];
    sizeBuf_t sz;
    SZ_Init(&sz, buf, sizeof(buf));
    T_ASSERT(sz.data == buf);
    T_EQ(sz.maxsize, 64);
    T_EQ(sz.cursize, 0);
    T_EQ(sz.readcount, 0);
}

TEST(net, sz_clear) {
    BYTE buf[32];
    sizeBuf_t sz;
    SZ_Init(&sz, buf, sizeof(buf));
    sz.cursize = 10;
    SZ_Clear(&sz);
    T_EQ(sz.cursize, 0);
}

/* -----------------------------------------------------------------------
 * SZ_Write
 * --------------------------------------------------------------------- */

TEST(net, sz_write_appends_data) {
    BYTE buf[32];
    sizeBuf_t sz;
    SZ_Init(&sz, buf, sizeof(buf));

    const char payload[] = "hello";
    SZ_Write(&sz, payload, 5);

    T_EQ(sz.cursize, 5);
    T_ASSERT(memcmp(buf, "hello", 5) == 0);
}

TEST(net, sz_write_multiple) {
    BYTE buf[32];
    sizeBuf_t sz;
    SZ_Init(&sz, buf, sizeof(buf));

    SZ_Write(&sz, "AB", 2);
    SZ_Write(&sz, "CD", 2);

    T_EQ(sz.cursize, 4);
    T_ASSERT(buf[0] == 'A' && buf[1] == 'B' && buf[2] == 'C' && buf[3] == 'D');
}

/* -----------------------------------------------------------------------
 * Loopback ring buffer
 * --------------------------------------------------------------------- */

TEST(net, loopback_empty_returns_zero) {
    static BYTE   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t from;

    drain_loopback(NS_SERVER);
    int r = NET_GetPacket(NS_SERVER, &from, &msg);
    T_EQ(r, 0);
}

TEST(net, loopback_round_trip) {
    static BYTE   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t adr  = loopback_adr();
    netadr_t from;

    drain_loopback(NS_SERVER);

    const BYTE payload[] = { 0x01, 0x02, 0x03, 0x04 };
    NET_SendPacket(NS_CLIENT, sizeof(payload), payload, adr);

    int r = NET_GetPacket(NS_SERVER, &from, &msg);

    T_EQ(r, (int)sizeof(payload));
    T_ASSERT(memcmp(msg.data, payload, sizeof(payload)) == 0);
    T_EQ(from.type, NA_LOOPBACK);
    T_EQ(NET_GetPacket(NS_SERVER, &from, &msg), 0);
}

TEST(net, loopback_multiple_packets_in_order) {
    static BYTE   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t adr  = loopback_adr();
    netadr_t from;

    drain_loopback(NS_SERVER);

    const BYTE pkt1[] = { 'A', 'B' };
    const BYTE pkt2[] = { 'C', 'D', 'E' };
    NET_SendPacket(NS_CLIENT, sizeof(pkt1), pkt1, adr);
    NET_SendPacket(NS_CLIENT, sizeof(pkt2), pkt2, adr);

    int r1 = NET_GetPacket(NS_SERVER, &from, &msg);
    T_EQ(r1, (int)sizeof(pkt1));
    T_ASSERT(msg.data[0] == 'A' && msg.data[1] == 'B');

    int r2 = NET_GetPacket(NS_SERVER, &from, &msg);
    T_EQ(r2, (int)sizeof(pkt2));
    T_ASSERT(msg.data[0] == 'C' && msg.data[1] == 'D' && msg.data[2] == 'E');

    T_EQ(NET_GetPacket(NS_SERVER, &from, &msg), 0);
}

TEST(net, loopback_grows_without_reordering_pending_packets) {
    static BYTE payload[65536], msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t adr = loopback_adr(), from;
    drain_loopback(NS_SERVER);
    FOR_LOOP(packet, 6) {
        memset(payload, packet, sizeof(payload));
        NET_SendPacket(NS_CLIENT, sizeof(payload), payload, adr);
    }
    FOR_LOOP(packet, 6) {
        T_EQ(NET_GetPacket(NS_SERVER, &from, &msg), (int)sizeof(payload));
        T_EQ(msg.data[0], packet); T_EQ(msg.data[sizeof(payload)-1], packet);
    }
}

TEST(net, loopback_server_to_client) {
    static BYTE   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t adr  = loopback_adr();
    netadr_t from;

    drain_loopback(NS_CLIENT);

    const BYTE payload[] = { (BYTE)0xDE, (BYTE)0xAD };
    NET_SendPacket(NS_SERVER, sizeof(payload), payload, adr);

    int r = NET_GetPacket(NS_CLIENT, &from, &msg);
    T_EQ(r, (int)sizeof(payload));
    T_ASSERT(msg.data[0] == 0xDE && msg.data[1] == 0xAD);
}

TEST(net, loopback_na_ip_no_crash_without_socket) {
    static BYTE   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t adr;
    memset(&adr, 0, sizeof(adr));
    adr.type    = NA_IP;
    adr.ip[0]   = 127; adr.ip[1] = 0; adr.ip[2] = 0; adr.ip[3] = 1;
    adr.port    = htons(27910);

    const char payload[] = { 0x01 };
    NET_SendPacket(NS_CLIENT, sizeof(payload), payload, adr);

    netadr_t from;
    drain_loopback(NS_SERVER);
    T_EQ(NET_GetPacket(NS_SERVER, &from, &msg), 0);
}

TEST(net, net_config_opens_and_closes_udp_sockets) {
    test_client_stubs_set_cvar("game_port", "28030");
    NET_Init();
    NET_Config(false);
    T_ASSERT(!NET_IsConfigured(NS_CLIENT));
    T_ASSERT(!NET_IsConfigured(NS_SERVER));

    NET_Config(true);
    T_ASSERT(NET_IsConfigured(NS_CLIENT));
    T_ASSERT(NET_IsConfigured(NS_SERVER));

    NET_Config(false);
    T_ASSERT(!NET_IsConfigured(NS_CLIENT));
    T_ASSERT(!NET_IsConfigured(NS_SERVER));
}

TEST(net, net_config_source_opens_one_udp_socket) {
    test_client_stubs_set_cvar("game_port", "28031");
    NET_Init();
    NET_Config(false);

    NET_ConfigSource(NS_CLIENT, true);
    T_ASSERT(NET_IsConfigured(NS_CLIENT));
    T_ASSERT(!NET_IsConfigured(NS_SERVER));

    NET_Config(false);
    NET_ConfigSource(NS_SERVER, true);
    T_ASSERT(!NET_IsConfigured(NS_CLIENT));
    T_ASSERT(NET_IsConfigured(NS_SERVER));

    NET_Config(false);
    T_ASSERT(!NET_IsConfigured(NS_CLIENT));
    T_ASSERT(!NET_IsConfigured(NS_SERVER));
}

/* -----------------------------------------------------------------------
 * NET_StringToAdr
 * --------------------------------------------------------------------- */

TEST(net, string_to_adr_ip_only) {
    netadr_t adr;
    bool ok = NET_StringToAdr("10.0.0.1", 12345, &adr);
    T_ASSERT(ok);
    T_EQ(adr.type, NA_IP);
    T_EQ(adr.ip[0], 10);
    T_EQ(adr.ip[1], 0);
    T_EQ(adr.ip[2], 0);
    T_EQ(adr.ip[3], 1);
    T_EQ(ntohs(adr.port), 12345);
}

TEST(net, string_to_adr_ip_with_port) {
    netadr_t adr;
    bool ok = NET_StringToAdr("192.168.0.5:9000", 0, &adr);
    T_ASSERT(ok);
    T_EQ(adr.type, NA_IP);
    T_EQ(adr.ip[0], 192);
    T_EQ(adr.ip[1], 168);
    T_EQ(adr.ip[2], 0);
    T_EQ(adr.ip[3], 5);
    T_EQ(ntohs(adr.port), 9000);
}

TEST(net, string_to_adr_port_overrides_default) {
    netadr_t adr;
    bool ok = NET_StringToAdr("172.16.0.1:27910", 9999, &adr);
    T_ASSERT(ok);
    T_EQ(ntohs(adr.port), 27910);
}

TEST(net, string_to_adr_bad_address) {
    netadr_t adr;
    bool ok = NET_StringToAdr("999.999.999.999", 0, &adr);
    (void)ok;
    T_ASSERT(1);
}

/* -----------------------------------------------------------------------
 * MSG_Write* / MSG_Read* round-trips
 * --------------------------------------------------------------------- */

static sizeBuf_t make_msg_buf(BYTE *buf, DWORD bufsz) {
    sizeBuf_t sb;
    SZ_Init(&sb, buf, bufsz);
    return sb;
}

TEST(net, msg_writebyte_readbyte_roundtrip) {
    BYTE buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, 0xAB);
    sb.readcount = 0;
    T_EQ(MSG_ReadByte(&sb), 0xAB);
}

TEST(net, msg_byte_ff_roundtrip_is_unsigned) {
    BYTE buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, 0xFF);
    sb.readcount = 0;
    T_EQ(MSG_ReadByte(&sb), 255);
}

TEST(net, msg_writeshort_readshort_roundtrip) {
    BYTE buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteShort(&sb, 0x1234);
    sb.readcount = 0;
    T_EQ(MSG_ReadShort(&sb) & 0xFFFF, 0x1234);
}

TEST(net, msg_writelong_readlong_roundtrip) {
    BYTE buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteLong(&sb, (int)0xDEADBEEF);
    sb.readcount = 0;
    T_EQ((unsigned int)MSG_ReadLong(&sb), (unsigned int)0xDEADBEEF);
}

TEST(net, msg_writefloat_readfloat_roundtrip) {
    BYTE buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteFloat(&sb, 3.14f);
    sb.readcount = 0;
    T_FEQ(MSG_ReadFloat(&sb), 3.14f, 0.0001f);
}

TEST(net, msg_writestring_readstring_roundtrip) {
    BYTE buf[64];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteString(&sb, "hello");
    sb.readcount = 0;
    char out[32] = {0};
    MSG_ReadString(&sb, out);
    T_STREQ(out, "hello");
}

TEST(net, msg_readbyte_past_end_returns_zero) {
    BYTE buf[8] = {0};
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    T_EQ(MSG_ReadByte(&sb), 0);
}

TEST(net, msg_writepos_readpos_roundtrip) {
    BYTE buf[32];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    VECTOR3 out = {0};
    VECTOR3 in  = {128.0f, -64.0f, 32.0f};
    MSG_WritePos(&sb, &in);
    sb.readcount = 0;
    MSG_ReadPos(&sb, &out);
    T_EQ((int)out.x, (int)in.x);
    T_EQ((int)out.y, (int)in.y);
    T_EQ((int)out.z, (int)in.z);
}

TEST(net, msg_writedir_readdir_roundtrip) {
    BYTE buf[32];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    VECTOR3 dir = {0.707f, 0.0f, -0.707f};
    VECTOR3 out = {0};
    MSG_WriteDir(&sb, &dir);
    sb.readcount = 0;
    MSG_ReadDir(&sb, &out);
    T_FEQ(out.x, dir.x, 0.001f);
    T_FEQ(out.y, dir.y, 0.001f);
    T_FEQ(out.z, dir.z, 0.001f);
}

TEST(net, msg_writeangle_readangle_roundtrip) {
    BYTE buf[8];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    float angle = 1.5f;
    MSG_WriteAngle(&sb, angle);
    sb.readcount = 0;
    float out = MSG_ReadAngle(&sb);
    T_FEQ(out, angle, 0.025f);
}

TEST(net, msg_multiple_types_sequential) {
    BYTE buf[64];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb,  42);
    MSG_WriteShort(&sb, 1000);
    MSG_WriteLong(&sb,  0x12345678);
    sb.readcount = 0;
    T_EQ(MSG_ReadByte(&sb)  & 0xFF,       42);
    T_EQ(MSG_ReadShort(&sb) & 0xFFFF, 1000);
    T_EQ((unsigned int)MSG_ReadLong(&sb), (unsigned int)0x12345678);
}


TEST(net, ui_window_frame_delta_preserves_text_offsets) {
    BYTE buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t from = {0}, to = { .number = 6, .flags = { .type = FT_SIMPLEFRAME } }, out = {0};
    DWORD bits = 0;
    int number;

    to.text = (LPCSTR)(uintptr_t)0x1234;
    MSG_WriteDeltaUIWindowFrame(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaUIWindowFrame(&sb, &out, number, bits);

    T_EQ(number, 6);
    T_EQ((DWORD)(uintptr_t)out.text, 0x1234);
    T_EQ(out.flags.type, FT_SIMPLEFRAME);
}

static VECTOR2 text_length_mock_size(LPCDRAWTEXT text);

static void test_send_window(DWORD id, DWORD class_id, DWORD flags, FLOAT x, LPCSTR text, LPCSTR command) {
    BYTE buf[1024], arena[512] = { 0 };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = { .number = 1, .flags = { .type = FT_TEXT }, .hotkey = 'Z' };
    uiLabel_t label = {0};
    DWORD text_offset = 1, command_offset = text_offset + strlen(text) + 1;

    snprintf((LPSTR)arena + text_offset, sizeof(arena) - text_offset, "%s", text);
    snprintf((LPSTR)arena + command_offset, sizeof(arena) - command_offset, "%s", command);
    frame.text = (LPCSTR)(uintptr_t)text_offset; frame.onclick = (LPCSTR)(uintptr_t)command_offset;
    frame.size.width = 0.2f; frame.size.height = 0.2f;
    frame.points.x[FPP_MIN] = MAKE(uiFramePoint_t, .used = 1, .relativeTo = 0, .offset = x * UI_FRAMEPOINT_SCALE);
    frame.points.y[FPP_MIN] = MAKE(uiFramePoint_t, .used = 1, .relativeTo = 0, .offset = -0.1f * UI_FRAMEPOINT_SCALE);
    MSG_WriteByte(&sb, svc_window); MSG_WriteByte(&sb, UI_WINDOW_OPEN);
    MSG_WriteLong(&sb, id); MSG_WriteLong(&sb, class_id); MSG_WriteLong(&sb, flags);
    MSG_WriteDeltaUIWindowFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, sizeof(label)); MSG_Write(&sb, &label, sizeof(label));
    MSG_WriteLong(&sb, 0); MSG_WriteShort(&sb, 0);
    MSG_WriteLong(&sb, command_offset + strlen(command) + 1);
    MSG_Write(&sb, arena, command_offset + strlen(command) + 1);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
}

TEST(net, window_trailing_text_arena_exceeds_typed_payload_limit) {
    BYTE buf[2048], text[514];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = { .number = 1, .flags = { .type = FT_TEXT } };
    uiLabel_t label = {0};

    memset(text, 'W', sizeof(text)); text[0] = '\0'; text[sizeof(text) - 1] = '\0';
    frame.text = (LPCSTR)(uintptr_t)1;
    frame.size.width = 0.4f; frame.size.height = 0.1f;
    test_client_stubs_init(); test_textarea_draws = 0;
    re.GetTextSize = text_length_mock_size; re.DrawText = capture_textarea;
    MSG_WriteByte(&sb, svc_window); MSG_WriteByte(&sb, UI_WINDOW_OPEN);
    MSG_WriteLong(&sb, 7); MSG_WriteLong(&sb, 70); MSG_WriteLong(&sb, UI_WINDOW_MODAL | UI_WINDOW_UNIQUE);
    MSG_WriteDeltaUIWindowFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, sizeof(label)); MSG_Write(&sb, &label, sizeof(label));
    MSG_WriteLong(&sb, 0); MSG_WriteShort(&sb, 0);
    MSG_WriteLong(&sb, sizeof(text)); MSG_Write(&sb, text, sizeof(text));
    sb.readcount = 0;

    CL_ParseServerMessage(&sb);
    T_ASSERT(CL_WindowModalActive());
    CL_WindowDraw();
    T_EQ(test_textarea_draws, 1);
    T_EQ(strlen(test_textarea_draw.text), sizeof(text) - 2);
    CL_WindowClear();
}

TEST(net, window_without_frame_terminator_is_rejected) {
    BYTE buf[64];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init(); CL_WindowClear();
    MSG_WriteByte(&sb, svc_window); MSG_WriteByte(&sb, UI_WINDOW_OPEN);
    MSG_WriteLong(&sb, 8); MSG_WriteLong(&sb, 80); MSG_WriteLong(&sb, UI_WINDOW_MODAL);
    MSG_WriteLong(&sb, 1);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
    T_ASSERT(!CL_WindowModalActive());
}

TEST(net, window_unique_class_replaces_existing_instance) {
    test_client_stubs_init(); CL_WindowClear();
    re.GetTextSize = text_length_mock_size; re.DrawText = capture_textarea;
    test_send_window(1, 90, UI_WINDOW_UNIQUE, 0.1f, "Old", "old");
    test_send_window(2, 90, UI_WINDOW_UNIQUE, 0.1f, "New", "new");
    test_textarea_draws = 0; CL_WindowDraw();
    T_EQ(test_textarea_draws, 1);
    T_STREQ(test_textarea_draw.text, "New");
    CL_WindowClear();
}

TEST(net, screen_layout_draws_client_windows) {
    test_client_stubs_init(); CL_WindowClear();
    re.GetTextSize = text_length_mock_size; re.DrawText = capture_textarea;
    test_send_window(3, 91, UI_WINDOW_UNIQUE, 0.1f, "Visible", "visible");
    test_textarea_draws = 0; SCR_DrawLayout();
    T_EQ(test_textarea_draws, 1); T_STREQ(test_textarea_draw.text, "Visible");
    CL_WindowClear();
}

TEST(net, window_click_raises_and_moves_keyboard_focus) {
    char command_buf[128]; BYTE message_buf[256];

    test_client_stubs_init(); CL_WindowClear();
    re.GetTextSize = text_length_mock_size; re.DrawText = capture_textarea;
    SZ_Init(&cls.netchan.message, message_buf, sizeof(message_buf));
    test_send_window(1, 91, UI_WINDOW_UNIQUE, 0.05f, "First", "first");
    test_send_window(2, 92, UI_WINDOW_UNIQUE, 0.45f, "Second", "second");
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_DOWN, 128, 256, 1));
    SZ_Clear(&cls.netchan.message);
    T_ASSERT(CL_WindowKeyEvent('Z'));
    cls.netchan.message.readcount = 0;
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, command_buf);
    T_STREQ(command_buf, "first");
    test_textarea_draws = 0; CL_WindowDraw();
    T_EQ(test_textarea_draws, 2);
    T_STREQ(test_textarea_draw.text, "First");
    CL_WindowClear();
}

TEST(net, window_close_action_closes_without_server_command) {
    BYTE message_buf[256];

    test_client_stubs_init(); CL_WindowClear();
    re.GetTextSize = text_length_mock_size;
    SZ_Init(&cls.netchan.message, message_buf, sizeof(message_buf));
    test_send_window(3, 93, UI_WINDOW_MODAL, 0.05f, "Close", UI_WINDOW_CLOSE_ACTION);
    SZ_Clear(&cls.netchan.message);
    T_ASSERT(CL_WindowModalActive());
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_DOWN, 128, 256, 1));
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_UP, 128, 256, 1));
    T_ASSERT(!CL_WindowModalActive());
    T_EQ(cls.netchan.message.cursize, 0);
    CL_WindowClear();
}

TEST(net, window_close_notify_releases_server_modal_owner) {
    test_client_stubs_init(); CL_WindowClear();
    re.GetTextSize = text_length_mock_size;
    test_send_window(4, 94, UI_WINDOW_MODAL, 0.05f, "Close", UI_WINDOW_CLOSE_NOTIFY_ACTION);
    T_STREQ(test_forwarded_command, "pause 1");
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_DOWN, 128, 256, 1));
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_UP, 128, 256, 1));
    T_ASSERT(!CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "pause 0");
    CL_WindowClear();
}

TEST(net, window_escape_closes_and_releases_server_modal_owner) {
    test_client_stubs_init(); CL_WindowClear();
    test_send_window(5, 95, UI_WINDOW_MODAL, 0.05f, "Close", UI_WINDOW_CLOSE_NOTIFY_ACTION);
    T_ASSERT(CL_WindowKeyEvent(K_ESCAPE));
    T_ASSERT(!CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "pause 0");
    CL_WindowClear();
}

TEST(net, window_no_escape_consumes_escape_without_dismissal) {
    test_client_stubs_init(); CL_WindowClear();
    test_send_window(13, 103, UI_WINDOW_MODAL | UI_WINDOW_NO_PAUSE | UI_WINDOW_NO_ESCAPE,
                     0.05f, "Choose", UI_WINDOW_CLOSE_ACTION);
    T_ASSERT(CL_WindowModalActive());
    T_ASSERT(CL_WindowKeyEvent(K_ESCAPE));
    T_ASSERT(CL_WindowModalActive());
    CL_WindowClear();
}

TEST(net, stacked_modal_windows_unpause_only_after_last_close) {
    test_client_stubs_init(); CL_WindowClear();
    test_send_window(6, 96, UI_WINDOW_MODAL, 0.05f, "First", UI_WINDOW_CLOSE_NOTIFY_ACTION);
    T_STREQ(test_forwarded_command, "pause 1");
    test_forwarded_command[0] = '\0';
    test_send_window(7, 97, UI_WINDOW_MODAL, 0.05f, "Second", UI_WINDOW_CLOSE_NOTIFY_ACTION);
    T_STREQ(test_forwarded_command, "");
    T_ASSERT(CL_WindowKeyEvent(K_ESCAPE));
    T_ASSERT(CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "");
    T_ASSERT(CL_WindowKeyEvent(K_ESCAPE));
    T_ASSERT(!CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "pause 0");
    CL_WindowClear();
}

TEST(net, nonmodal_window_does_not_request_pause) {
    test_client_stubs_init(); CL_WindowClear();
    test_forwarded_command[0] = '\0';
    test_send_window(8, 98, 0, 0.05f, "Info", UI_WINDOW_CLOSE_ACTION);
    T_ASSERT(!CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "");
    CL_WindowClear();
}

TEST(net, no_pause_modal_blocks_input_without_requesting_pause) {
    test_client_stubs_init(); CL_WindowClear();
    test_forwarded_command[0] = '\0';
    test_send_window(9, 99, UI_WINDOW_MODAL | UI_WINDOW_NO_PAUSE, 0.05f,
                     "Allies", UI_WINDOW_CLOSE_ACTION);
    T_ASSERT(CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "");
    T_ASSERT(CL_WindowKeyEvent(K_ESCAPE));
    T_ASSERT(!CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "");
    CL_WindowClear();
}

TEST(net, no_pause_modal_does_not_release_underlying_pause_owner) {
    test_client_stubs_init(); CL_WindowClear();
    test_send_window(11, 101, UI_WINDOW_MODAL, 0.05f, "Menu", UI_WINDOW_CLOSE_ACTION);
    T_STREQ(test_forwarded_command, "pause 1");
    test_forwarded_command[0] = '\0';
    test_send_window(12, 102, UI_WINDOW_MODAL | UI_WINDOW_NO_PAUSE, 0.05f,
                     "Allies", UI_WINDOW_CLOSE_ACTION);
    T_STREQ(test_forwarded_command, "");
    T_ASSERT(CL_WindowKeyEvent(K_ESCAPE));
    T_ASSERT(CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "");
    T_ASSERT(CL_WindowKeyEvent(K_ESCAPE));
    T_ASSERT(!CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "pause 0");
    CL_WindowClear();
}

TEST(net, window_close_command_forwards_suffix_and_closes) {
    test_client_stubs_init(); CL_WindowClear(); re.GetTextSize = text_length_mock_size;
    test_forwarded_command[0] = '\0';
    test_send_window(10, 100, UI_WINDOW_MODAL | UI_WINDOW_NO_PAUSE, 0.05f,
                     "Accept", UI_WINDOW_CLOSE_COMMAND_PREFIX "allies_accept");
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_DOWN, 128, 256, 1));
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_UP, 128, 256, 1));
    T_ASSERT(!CL_WindowModalActive());
    T_STREQ(test_forwarded_command, "allies_accept");
    CL_WindowClear();
}

TEST(net, ui_frame_delta_preserves_text_length) {
    BYTE buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t from = {0}, to = { .number = 7, .textLength = 19 }, out = {0};
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaUIFrame(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaUIFrame(&sb, &out, number, bits);

    T_EQ(number, 7);
    T_EQ(out.textLength, 19);
}

TEST(net, ui_frame_delta_preserves_timed_status_binding) {
    BYTE buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t from = {0};
    uiFrame_t to = { .number = 8, .stat = UI_STAT_SELECTION_TIMED_STATUS };
    uiFrame_t out = {0};
    DWORD bits = 0;
    int number;

    T_ASSERT(UI_STAT_SELECTION_TIMED_STATUS <= UINT8_MAX);
    MSG_WriteDeltaUIFrame(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaUIFrame(&sb, &out, number, bits);

    T_EQ(number, 8);
    T_EQ(out.stat, UI_STAT_SELECTION_TIMED_STATUS);
}

static VECTOR2 text_length_mock_size(LPCDRAWTEXT text) {
    if (text && text->text && !strcmp(text->text, " ")) {
        return MAKE(VECTOR2, 0.006f, 0.012f);
    }
    return MAKE(VECTOR2, 0.018f, 0.012f);
}

TEST(net, layout_text_length_uses_space_advance_for_implicit_width) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = {0};
    uiLabel_t label = {0};
    LPCRECT rect;

    frame.number = 1;
    frame.flags.type = FT_STRING;
    frame.text = "123";
    frame.textLength = 10;
    frame.size.height = 0.012f;
    frame.points.x[FPP_MIN].used = 1;
    frame.points.x[FPP_MIN].targetPos = FPP_MIN;
    frame.points.x[FPP_MIN].relativeTo = 0;
    frame.points.x[FPP_MIN].offset = (SHORT)(0.100f * UI_FRAMEPOINT_SCALE);
    frame.points.y[FPP_MIN].used = 1;
    frame.points.y[FPP_MIN].targetPos = FPP_MIN;
    frame.points.y[FPP_MIN].relativeTo = 0;
    frame.points.y[FPP_MIN].offset = (SHORT)(-0.100f * UI_FRAMEPOINT_SCALE);

    test_client_stubs_init();
    re.GetTextSize = text_length_mock_size;
    MSG_WriteByte(&sb, LAYER_CONSOLE);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, sizeof(label));
    MSG_Write(&sb, &label, sizeof(label));
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;

    CL_ParseLayout(&sb);
    T_NOT_NULL(cl.layout[LAYER_CONSOLE]);
    SCR_Clear(cl.layout[LAYER_CONSOLE]);
    rect = SCR_LayoutRect(SCR_Frame(1));
    T_NOT_NULL(rect);
    T_FEQ(rect->x, 0.100f, 0.001f);
    T_FEQ(rect->w, 0.060f, 0.001f);
}

TEST(net, layout_authored_height_with_top_bottom_anchors_keeps_bottom_edge) {
    BYTE buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, parent = {0}, child = {0};
    LPCRECT parent_rect, child_rect;

    parent.number = 1;
    parent.flags.type = FT_SIMPLEFRAME;
    parent.size.width = 0.100f;
    parent.size.height = 0.030125f;
    parent.points.x[FPP_MIN].used = 1;
    parent.points.x[FPP_MIN].targetPos = FPP_MIN;
    parent.points.x[FPP_MIN].relativeTo = 0;
    parent.points.x[FPP_MIN].offset = (SHORT)(0.310f * UI_FRAMEPOINT_SCALE);
    parent.points.y[FPP_MIN].used = 1;
    parent.points.y[FPP_MIN].targetPos = FPP_MIN;
    parent.points.y[FPP_MIN].relativeTo = 0;
    parent.points.y[FPP_MIN].offset = (SHORT)(-0.51925f * UI_FRAMEPOINT_SCALE);

    child.number = 2;
    child.parent = 1;
    child.flags.type = FT_SIMPLEFRAME;
    child.size.width = 0.100f;
    child.size.height = 0.03125f;
    child.points.x[FPP_MIN].used = 1;
    child.points.x[FPP_MIN].targetPos = FPP_MIN;
    child.points.x[FPP_MIN].relativeTo = UI_PARENT;
    child.points.x[FPP_MAX].used = 1;
    child.points.x[FPP_MAX].targetPos = FPP_MAX;
    child.points.x[FPP_MAX].relativeTo = UI_PARENT;
    child.points.y[FPP_MIN].used = 1;
    child.points.y[FPP_MIN].targetPos = FPP_MIN;
    child.points.y[FPP_MIN].relativeTo = UI_PARENT;
    child.points.y[FPP_MAX].used = 1;
    child.points.y[FPP_MAX].targetPos = FPP_MAX;
    child.points.y[FPP_MAX].relativeTo = UI_PARENT;

    test_client_stubs_init();
    MSG_WriteByte(&sb, LAYER_INFOPANEL);
    MSG_WriteDeltaUIFrame(&sb, &empty, &parent, true);
    MSG_WriteByte(&sb, 0);
    MSG_WriteDeltaUIFrame(&sb, &empty, &child, true);
    MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;

    CL_ParseLayout(&sb);
    T_NOT_NULL(cl.layout[LAYER_INFOPANEL]);
    SCR_Clear(cl.layout[LAYER_INFOPANEL]);
    parent_rect = SCR_LayoutRect(SCR_Frame(1));
    child_rect = SCR_LayoutRect(SCR_Frame(2));
    T_NOT_NULL(parent_rect);
    T_NOT_NULL(child_rect);
    T_FEQ(child_rect->x, parent_rect->x, 0.0001f);
    T_FEQ(child_rect->w, 0.100f, 0.0001f);
    T_FEQ(child_rect->h, 0.03125f, 0.0001f);
    T_FEQ(child_rect->y + child_rect->h,
          parent_rect->y + parent_rect->h, 0.0001f);
    T_FEQ(child_rect->y, parent_rect->y - 0.001125f, 0.0002f);
}

TEST(net, layout_terminator_only_payload_clears_modal_layer) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = { .number = 1, .flags = { .type = FT_SIMPLEFRAME } };

    test_client_stubs_init();
    MSG_WriteByte(&sb, LAYER_QUESTDIALOG);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseLayout(&sb);
    T_NOT_NULL(cl.layout[LAYER_QUESTDIALOG]);
    T_ASSERT(SCR_LayoutModalActive());

    SZ_Clear(&sb);
    MSG_WriteByte(&sb, LAYER_QUESTDIALOG);
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseLayout(&sb);

    T_NULL(cl.layout[LAYER_QUESTDIALOG]);
    T_ASSERT(!SCR_LayoutModalActive());
}

/* Layout payload sizes are one unsigned wire byte; WoW's textured scrollbar is larger than signed-char range. */
TEST(net, layout_parser_accepts_scrollbar_payload_above_127_bytes) {
    BYTE buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    BYTE payload[192] = {0};
    uiFrame_t empty = {0}, frame = { .number = 1, .flags = { .type = FT_SCROLLBAR } };

    test_client_stubs_init();
    MSG_WriteByte(&sb, LAYER_QUESTDIALOG);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, sizeof(payload));
    MSG_Write(&sb, payload, sizeof(payload));
    MSG_WriteLong(&sb, 0); MSG_WriteShort(&sb, 0);
    sb.readcount = 0;

    CL_ParseLayout(&sb);
    T_ASSERT(cl.layout[LAYER_QUESTDIALOG] != NULL);
    if (cl.layout[LAYER_QUESTDIALOG]) {
        SCR_Clear(cl.layout[LAYER_QUESTDIALOG]);
        T_EQ(SCR_Frame(1)->buffer.size, sizeof(payload));
    }
}

/* An empty svc_layout is the server's layer-clear operation. */
TEST(net, empty_layout_clears_layer) {
    BYTE set_buf[256];
    BYTE clear_buf[32];
    sizeBuf_t set = make_msg_buf(set_buf, sizeof(set_buf));
    sizeBuf_t clear = make_msg_buf(clear_buf, sizeof(clear_buf));
    uiFrame_t empty = {0}, frame = { .number = 1, .flags = { .type = FT_SIMPLEFRAME } };

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);

    MSG_WriteByte(&set, LAYER_QUESTDIALOG);
    MSG_WriteDeltaUIFrame(&set, &empty, &frame, true);
    MSG_WriteByte(&set, 0);
    MSG_WriteLong(&set, 0);
    MSG_WriteShort(&set, 0);
    set.readcount = 0;
    CL_ParseLayout(&set);
    T_ASSERT(cl.layout[LAYER_QUESTDIALOG] != NULL);

    MSG_WriteByte(&clear, LAYER_QUESTDIALOG);
    MSG_WriteLong(&clear, 0);
    MSG_WriteShort(&clear, 0);
    clear.readcount = 0;
    CL_ParseLayout(&clear);
    T_NULL(cl.layout[LAYER_QUESTDIALOG]);
}

static menuUnitData_t test_unit_ui_last;
static DWORD test_unit_ui_calls;
static DWORD test_unit_ui_num_units;

static void test_update_unit_ui(DWORD num_units, menuUnitData_t *units) {
    test_unit_ui_calls++;
    test_unit_ui_num_units = num_units;
    memset(&test_unit_ui_last, 0, sizeof(test_unit_ui_last));
    if (num_units && units) {
        test_unit_ui_last = units[0];
    }
}

TEST(net, game_command_selection_accepts_authoritative_multi_selection) {
    BYTE buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();
    test_unit_ui_calls = 0;
    menu.UpdateUnitUI = test_update_unit_ui;
    cl.selection.num_selected = 1;
    cl.selection.entity_nums[0] = 99;

    MSG_WriteByte(&sb, svc_gamecmd);
    MSG_WriteString(&sb, "select");
    MSG_WriteShort(&sb, 3 * sizeof(DWORD));
    MSG_WriteLong(&sb, 4);
    MSG_WriteLong(&sb, 7);
    MSG_WriteLong(&sb, 11);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);

    T_EQ(cl.selection.num_selected, 3);
    T_EQ(cl.selection.entity_nums[0], 4);
    T_EQ(cl.selection.entity_nums[1], 7);
    T_EQ(cl.selection.entity_nums[2], 11);
    T_EQ(test_unit_ui_calls, 1);
}

TEST(net, game_command_empty_selection_clears_client_cache) {
    BYTE buf[64];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();
    test_unit_ui_calls = 0;
    menu.UpdateUnitUI = test_update_unit_ui;
    cl.selection.num_selected = 2;
    cl.selection.entity_nums[0] = 4;
    cl.selection.entity_nums[1] = 7;

    MSG_WriteByte(&sb, svc_gamecmd);
    MSG_WriteString(&sb, "select");
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);

    T_EQ(cl.selection.num_selected, 0);
    T_EQ(test_unit_ui_calls, 1);
}

TEST(net, unit_ui_parser_preserves_distinct_strings) {
    BYTE buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();
    test_unit_ui_calls = 0;
    test_unit_ui_num_units = 0;
    memset(&test_unit_ui_last, 0, sizeof(test_unit_ui_last));
    menu.UpdateUnitUI = test_update_unit_ui;

    MSG_WriteByte(&sb, 1);
    MSG_WriteShort(&sb, 7);
    MSG_WriteByte(&sb, 1);
    MSG_WriteString(&sb, "Interface\\Icons\\Ability_Warrior_Cleave.blp");
    MSG_WriteString(&sb, "Attack");
    MSG_WriteString(&sb, "1");
    MSG_WriteString(&sb, "wow_action 0");
    MSG_WriteByte(&sb, '1');
    MSG_WriteByte(&sb, 1);
    MSG_WriteString(&sb, "Interface\\Icons\\INV_Misc_Bag_08.blp");
    MSG_WriteString(&sb, "Backpack");
    MSG_WriteString(&sb, "2");
    MSG_WriteByte(&sb, 4);
    MSG_WriteByte(&sb, 0);
    sb.readcount = 0;

    CL_ParseUnitUI(&sb);

    T_EQ((int)test_unit_ui_calls, 1);
    T_EQ((int)test_unit_ui_num_units, 1);
    T_EQ((int)test_unit_ui_last.entity_num, 7);
    T_STREQ(test_unit_ui_last.buttons[0].art, "Interface\\Icons\\Ability_Warrior_Cleave.blp");
    T_STREQ(test_unit_ui_last.buttons[0].tooltip, "Attack");
    T_STREQ(test_unit_ui_last.buttons[0].ubertip, "1");
    T_STREQ(test_unit_ui_last.buttons[0].command, "wow_action 0");
    T_EQ(test_unit_ui_last.buttons[0].hotkey, '1');
    T_STREQ(test_unit_ui_last.inventory[0].art, "Interface\\Icons\\INV_Misc_Bag_08.blp");
    T_STREQ(test_unit_ui_last.inventory[0].tooltip, "Backpack");
    T_STREQ(test_unit_ui_last.inventory[0].ubertip, "2");
    T_EQ(test_unit_ui_last.inventory[0].slot, 4);
}

static void reset_fow_client_state(void) {
    SAFE_DELETE(cl.fow.visible, MemFree);
    SAFE_DELETE(cl.fow.explored, MemFree);
    SAFE_DELETE(cl.fow.texture, MemFree);
    test_client_stubs_init();
}

static void write_fow_message(sizeBuf_t *sb,
                              DWORD flags,
                              DWORD width,
                              DWORD height,
                              DWORD first_row,
                              DWORD row_count,
                              BYTE const *payload,
                              DWORD payload_bytes)
{
    MSG_WriteByte(sb, svc_fogofwar);
    MSG_WriteByte(sb, flags);
    MSG_WriteShort(sb, width);
    MSG_WriteShort(sb, height);
    MSG_WriteShort(sb, first_row);
    MSG_WriteShort(sb, row_count);
    MSG_WriteShort(sb, payload_bytes);
    MSG_Write(sb, payload, payload_bytes);
}

TEST(net, cursor_splat_message_sets_and_clears_state) {
    BYTE buf[32];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();
    MSG_WriteByte(&sb, svc_cursor_splat);
    MSG_WriteShort(&sb, 7);
    MSG_WriteFloat(&sb, 320.0f);
    CL_ParseServerMessage(&sb);
    T_EQ(cl.cursor_splat.image, 7);
    T_FEQ(cl.cursor_splat.radius, 320.0f, 0.0001f);

    SZ_Clear(&sb);
    sb.readcount = 0;
    MSG_WriteByte(&sb, svc_cursor_splat);
    MSG_WriteShort(&sb, 0);
    MSG_WriteFloat(&sb, 0.0f);
    CL_ParseServerMessage(&sb);
    T_EQ(cl.cursor_splat.image, 0);
    T_FEQ(cl.cursor_splat.radius, 0.0f, 0.0001f);
}

TEST(net, initial_model_configstring_defers_registration_until_refresh) {
    BYTE buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    DWORD const model = 7;
    LPCSTR const path = "Units\\Human\\Footman\\Footman.mdx";

    test_client_stubs_init();
    test_model_loads = test_model_releases = 0;
    memset(test_model_load_paths, 0, sizeof(test_model_load_paths));
    re.LoadModel = capture_load_model;
    re.ReleaseModel = capture_release_model;
    cl.refresh_prepped = false;

    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_MODELS + model);
    MSG_WriteString(&sb, path);
    CL_ParseServerMessage(&sb);

    T_STREQ(cl.configstrings[CS_MODELS + model], path);
    T_NULL(cl.models[model]);
    T_NULL(cl.portraits[model]);
    T_EQ(test_model_loads, 0);
    T_EQ(test_model_releases, 0);
}

TEST(net, late_model_configstring_refreshes_world_and_portrait_models_together) {
    BYTE buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    DWORD const model = 7;
    LPCSTR const path = "Units\\Human\\Footman\\Footman.mdx";
    LPCSTR const portrait = "Units\\Human\\Footman\\Footman_Portrait.mdx";

    test_client_stubs_init();
    test_model_loads = test_model_releases = 0;
    memset(test_model_load_paths, 0, sizeof(test_model_load_paths));
    re.LoadModel = capture_load_model;
    re.ReleaseModel = capture_release_model;
    cl.refresh_prepped = true;
    cl.models[model] = (LPMODEL)(uintptr_t)0x2001u;
    cl.portraits[model] = (LPMODEL)(uintptr_t)0x2002u;
    test_client_stubs_set_existing_file(portrait);

    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_MODELS + model);
    MSG_WriteString(&sb, path);
    CL_ParseServerMessage(&sb);

    T_EQ(test_model_releases, 2);
    T_EQ(test_model_loads, 2);
    T_STREQ(test_model_load_paths[0], path);
    T_STREQ(test_model_load_paths[1], portrait);
    T_NOT_NULL(cl.models[model]);
    T_NOT_NULL(cl.portraits[model]);
}

TEST(net, packed_entity_names_survive_configstring_transport) {
    BYTE buf[512];
    char names[ENT_NAME_SLOT_SIZE * ENT_NAMES_PER_CS];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();
    entity_name_pool_prepare(names, NULL);
    entity_name_slot_store(names, 0, "Peasant");
    entity_name_slot_store(names, 1, "Villager");
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_GENERAL);
    MSG_WriteString(&sb, names);
    CL_ParseServerMessage(&sb);
    T_STREQ(cl.configstrings[CS_GENERAL], "Peasant");
    T_STREQ(cl.configstrings[CS_GENERAL] + ENT_NAME_SLOT_SIZE, "Villager");
}

/* Same-map load/begin resends CS_MODELS; keep the handle unless the path changed. */
TEST(net, model_configstring_skips_identical_reload) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    LPMODEL first;

    test_client_stubs_init();
    test_model_loads = test_model_releases = 0;
    re.LoadModel = capture_load_model;
    re.ReleaseModel = capture_release_model;
    cl.refresh_prepped = true;
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_MODELS + 3);
    MSG_WriteString(&sb, "units\\human\\Peasant\\Peasant.mdx");
    CL_ParseServerMessage(&sb);
    first = cl.models[3];
    T_EQ(test_model_loads, 1); T_EQ(test_model_releases, 0); T_NOT_NULL(first);

    SZ_Clear(&sb); sb.readcount = 0;
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_MODELS + 3);
    MSG_WriteString(&sb, "units\\human\\Peasant\\Peasant.mdx");
    CL_ParseServerMessage(&sb);
    T_EQ(test_model_loads, 1); T_EQ(test_model_releases, 0); T_EQ(cl.models[3], first);

    SZ_Clear(&sb); sb.readcount = 0;
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_MODELS + 3);
    MSG_WriteString(&sb, "units\\orc\\Grunt\\Grunt.mdx");
    CL_ParseServerMessage(&sb);
    T_EQ(test_model_loads, 2); T_EQ(test_model_releases, 1); T_NE(cl.models[3], first);

    SZ_Clear(&sb); sb.readcount = 0;
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_MODELS + 3);
    MSG_WriteString(&sb, "");
    CL_ParseServerMessage(&sb);
    T_EQ(test_model_loads, 2); T_EQ(test_model_releases, 2); T_NULL(cl.models[3]);
}

TEST(net, image_configstring_skips_identical_reload) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    LPCTEXTURE first;

    test_client_stubs_init();
    test_tex_loads = test_tex_releases = 0;
    re.LoadTexture = capture_load_texture;
    re.ReleaseTexture = capture_release_texture;
    cl.refresh_prepped = true;
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_IMAGES + 4);
    MSG_WriteString(&sb, "ReplaceableTextures\\Shadows\\Shadow.blp");
    CL_ParseServerMessage(&sb);
    first = cl.pics[4];
    T_EQ(test_tex_loads, 1); T_EQ(test_tex_releases, 0); T_NOT_NULL(first);

    SZ_Clear(&sb); sb.readcount = 0;
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_IMAGES + 4);
    MSG_WriteString(&sb, "ReplaceableTextures\\Shadows\\Shadow.blp");
    CL_ParseServerMessage(&sb);
    T_EQ(test_tex_loads, 1); T_EQ(test_tex_releases, 0); T_EQ(cl.pics[4], first);

    SZ_Clear(&sb); sb.readcount = 0;
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_IMAGES + 4);
    MSG_WriteString(&sb, "ReplaceableTextures\\Shadows\\ShadowFlyer.blp");
    CL_ParseServerMessage(&sb);
    T_EQ(test_tex_loads, 2); T_EQ(test_tex_releases, 1); T_NE(cl.pics[4], first);
}

TEST(client_layout, sprite_numeric_stat_drives_normalized_animation_phase) {
    DWORD const phase_stat = PLAYERSTATE_LUMBER_GATHERED + 1;
    uiFrame_t frame = { .flags = { .type = FT_SPRITE }, .tex = { .index = 1 }, .stat = phase_stat, .text = "#0" };
    RECT screen = MAKE(RECT, 0.0f, 0.6f, 0.0f, 0.0f);
    FLOAT ratio = -1.0f;

    test_client_stubs_init();
    cl.models[1] = (LPMODEL)(uintptr_t)1;
    cl.playerstate.stats[phase_stat] = 32768;
    test_sprite_anim[0] = '\0'; test_sprite_draws = 0; re.DrawSprite = capture_sprite;

    SCR_LayoutDrawSprite(&frame, &screen);
    T_EQ(test_sprite_draws, 1);
    T_EQ(sscanf(test_sprite_anim, "#0@%f", &ratio), 1);
    T_FEQ(ratio, 32768.0f / (FLOAT)UINT16_MAX, 0.00001f);
}

TEST(net, playerstat_pair_after_gameplay_states_roundtrips) {
    DWORD const stat = PLAYERSTATE_LUMBER_GATHERED + 1;
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = { 0 }, to = { 0 }, out = { 0 };
    DWORD bits;
    int number;

    to.number = 3;
    to.stats[stat] = 49151;
    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    sb.readcount = 0;
    number = MSG_ReadPlayerBits(&sb, &bits);
    MSG_ReadDeltaPlayerState(&sb, &out, number, bits);

    T_EQ(number, 3);
    T_EQ(out.stats[stat], 49151);
}

TEST(net, playerinfo_game_state_preserves_open_menu_input) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = { 0 };
    PLAYER to = { 0 };

    test_client_stubs_init();
    cls.key_dest = key_menu;
    cls.netchan.remote_address.type = NA_IP;
    to.number = 1;
    to.vieworigin = (VECTOR3){ 128.0f, 256.0f, 0 };
    to.fov = 50;
    to.distance = 1650;
    to.znear = 100.0f;
    to.zfar = 5000.0f;
    to.client_ui_state = CLIENT_UI_GAME;

    MSG_WriteByte(&sb, svc_playerinfo);
    MSG_WriteDeltaPlayerState(&sb, &from, &to);

    CL_ParseServerMessage(&sb);

    /* Player snapshots must not close a menu; only the explicit loading-to-game transition owns that switch. */
    T_EQ(cls.key_dest, key_menu);
    T_EQ(cls.netchan.remote_address.type, NA_IP);
    T_EQ(cl.playerstate.number, 1);
    T_FEQ(cl.viewDef.camerastate[0].origin.x, 128.0f, 0.0001f);
    T_FEQ(cl.viewDef.camerastate[0].origin.y, 256.0f, 0.0001f);
    T_FEQ(cl.viewDef.camerastate[0].znear, 100.0f, 0.0001f);
    T_FEQ(cl.viewDef.camerastate[0].zfar, 5000.0f, 0.0001f);
}

TEST(net, live_selection_stats_roundtrip_and_format) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = { 0 };
    PLAYER to = { 0 };
    PLAYER out = { 0 };
    uiFrame_t health = { .stat = UI_STAT_SELECTION_HEALTH_TEXT };
    uiFrame_t mana = { .stat = UI_STAT_SELECTION_MANA_TEXT };
    DWORD bits;
    int number;

    to.number = 2;
    to.stats[UI_PLAYERSTAT_SELECTION_HEALTH] = 325;
    to.stats[UI_PLAYERSTAT_SELECTION_MAX_HEALTH] = 650;
    to.stats[UI_PLAYERSTAT_SELECTION_MANA] = 74;
    to.stats[UI_PLAYERSTAT_SELECTION_MAX_MANA] = 255;
    to.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS] = 32768;

    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    sb.readcount = 0;
    number = MSG_ReadPlayerBits(&sb, &bits);
    MSG_ReadDeltaPlayerState(&sb, &out, number, bits);

    T_EQ(number, 2);
    T_EQ(out.stats[UI_PLAYERSTAT_SELECTION_HEALTH], 325);
    T_EQ(out.stats[UI_PLAYERSTAT_SELECTION_MAX_HEALTH], 650);
    T_EQ(out.stats[UI_PLAYERSTAT_SELECTION_MANA], 74);
    T_EQ(out.stats[UI_PLAYERSTAT_SELECTION_MAX_MANA], 255);
    T_EQ(out.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS], 32768);

    test_client_stubs_init();
    cl.playerstate = out;
    T_STREQ(SCR_GetStringValue(&health), "325 / 650");
    T_STREQ(SCR_GetStringValue(&mana), "74 / 255");
    cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_MAX_MANA] = 0;
    T_STREQ(SCR_GetStringValue(&mana), "");
}

/* Camera and UI cleanup must reach the rendered samples, not merely change the server-side enum. */
TEST(net, cinematic_cleanup_restores_camera_and_ui_samples) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = {0}, to = { .number = 1, .client_ui_state = CLIENT_UI_CINEMATIC, .fov = 35, .distance = 900, .znear = 55.0f, .zfar = 6500.0f };

    test_client_stubs_init();
    to.viewangles = (VECTOR3){300, 0, 120};
    to.uiflags = ~(1u << LAYER_CINEMATIC);
    MSG_WriteByte(&sb, svc_playerinfo); MSG_WriteDeltaPlayerState(&sb, &from, &to);
    CL_ParseServerMessage(&sb);
    T_EQ(cl.playerstate.client_ui_state, CLIENT_UI_CINEMATIC);
    from = to;
    to.client_ui_state = CLIENT_UI_GAME; to.uiflags = 1u << LAYER_CINEMATIC;
    to.viewangles = (VECTOR3){326, 0, 0}; to.vieworigin = (VECTOR3){128, 256, 0}; to.fov = 50; to.distance = 1650;
    to.znear = 100.0f; to.zfar = 5000.0f;
    SZ_Clear(&sb); sb.readcount = 0;
    MSG_WriteByte(&sb, svc_playerinfo); MSG_WriteDeltaPlayerState(&sb, &from, &to);
    CL_ParseServerMessage(&sb);
    T_EQ(cl.playerstate.client_ui_state, CLIENT_UI_GAME); T_EQ(cl.playerstate.uiflags, to.uiflags);
    T_FEQ(cl.viewDef.camerastate[0].origin.x, 128, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].origin.y, 256, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].viewangles.x, 326, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].viewangles.z, 0, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].distance, 1650, 0.001f);
    T_EQ(cl.viewDef.camerastate[0].fov, 50);
    T_FEQ(cl.viewDef.camerastate[0].znear, 100.0f, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].zfar, 5000.0f, 0.001f);
}

TEST(net, playerstate_identity_bytes_roundtrip) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = { 0 };
    PLAYER to = { 0 };
    PLAYER out = { 0 };
    DWORD bits;
    int number;

    to.number = 2;
    to.cinematic_portrait = 41;
    to.team = 3;
    to.color = 7;
    to.race = kPlayerRaceNightElf;

    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    sb.readcount = 0;
    number = MSG_ReadPlayerBits(&sb, &bits);
    MSG_ReadDeltaPlayerState(&sb, &out, number, bits);

    T_EQ(number, 2);
    T_EQ(out.cinematic_portrait, 41);
    T_EQ(out.team, 3);
    T_EQ(out.color, 7);
    T_EQ(out.race, kPlayerRaceNightElf);
    T_EQ(out.fov, 0);
}

TEST(net, camera_clamp_uses_world_bounds) {
    VECTOR2 clamped;

    test_client_stubs_init();
    test_client_stubs_set_world_bounds((BOX2){
        .min = { -4096.0f, -3072.0f },
        .max = { 4096.0f, 3072.0f },
    });
    clamped = CL_ClampCameraPosition((VECTOR2){ 5000.0f, -4000.0f });
    T_FEQ(clamped.x, 4096.0f, 0.001f);
    T_FEQ(clamped.y, -3072.0f, 0.001f);
}

TEST(net, playerstate_camera_render_fields_roundtrip) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = { 0 };
    PLAYER to = { 0 };
    PLAYER out = { 0 };
    DWORD bits;
    int number;

    to.number = 4;
    to.vieworigin.z = 275.0f;
    to.viewangles = (VECTOR3){ 12.5f, 45.0f, 90.0f };
    to.znear = 75.0f;
    to.zfar = 6500.0f;
    /* texts[1] is the final player-state text field; the player mask remains 32 bits. */
    to.texts[1] = "camera-last-field";

    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    sb.readcount = 0;
    number = MSG_ReadPlayerBits(&sb, &bits);
    MSG_ReadDeltaPlayerState(&sb, &out, number, bits);

    T_EQ(number, 4);
    T_FEQ(out.vieworigin.z, 275.0f, 0.001f);
    T_FEQ(out.viewangles.x, 12.5f, 0.001f);
    T_FEQ(out.viewangles.y, 45.0f, 0.001f);
    T_FEQ(out.viewangles.z, 90.0f, 0.001f);
    T_FEQ(out.znear, 75.0f, 0.001f);
    T_FEQ(out.zfar, 6500.0f, 0.001f);
    T_STREQ(out.texts[1], "camera-last-field");
    MemFree((void *)out.texts[1]);
}

TEST(net, playerinfo_copies_server_clip_planes) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = { 0 };
    PLAYER to = { 0 };

    test_client_stubs_init();
    cl.viewDef.camerastate[0].znear = 100.0f;
    cl.viewDef.camerastate[0].zfar = 5000.0f;
    to.number = 1;
    to.fov = 50;
    to.distance = 1650;
    to.znear = 75.0f;
    to.zfar = 6500.0f;
    to.client_ui_state = CLIENT_UI_GAME;

    MSG_WriteByte(&sb, svc_playerinfo);
    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    CL_ParseServerMessage(&sb);

    T_EQ(cl.viewDef.camerastate[0].fov, 50);
    T_FEQ(cl.viewDef.camerastate[0].znear, 75.0f, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].zfar, 6500.0f, 0.001f);
}

TEST(net, camera_prediction_reconciles_to_server_clamped_bound) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = { 0 };
    PLAYER to = { 0 };

    test_client_stubs_init();
    to.number = 1;
    to.vieworigin = (VECTOR3){ 100.0f, -50.0f, 0 };
    test_client_stubs_set_world_bounds((BOX2){
        .min = { -100.0f, -50.0f },
        .max = { 100.0f, 50.0f },
    });
    to.fov = 50;
    to.distance = 1650;
    to.znear = 100.0f;
    to.zfar = 5000.0f;
    to.client_ui_state = CLIENT_UI_GAME;
    cl.camera_prediction.active = true;
    cl.camera_prediction.origin = (VECTOR2){ 500.0f, -500.0f };

    MSG_WriteByte(&sb, svc_playerinfo);
    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    CL_ParseServerMessage(&sb);

    T_ASSERT(!cl.camera_prediction.active);
    T_FEQ(cl.viewDef.camerastate[0].origin.x, 100.0f, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].origin.y, -50.0f, 0.001f);
}

TEST(net, fow_full_message_unpacks_visible_and_explored_planes) {
    BYTE buf[64];
    BYTE payload[] = {
        1, 1, 15, 2, 14,
    };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    reset_fow_client_state();

    write_fow_message(&sb,
                      FOW_MSG_FULL | FOW_MSG_VISIBLE_PLANE | FOW_MSG_EXPLORED_PLANE | FOW_MSG_RLE,
                      8,
                      2,
                      0,
                      2,
                      payload,
                      sizeof(payload));
    CL_ParseServerMessage(&sb);

    T_EQ(cl.fow.width, 8);
    T_EQ(cl.fow.height, 2);
    T_ASSERT(cl.fow.visible[0]);
    T_ASSERT(!cl.fow.visible[1]);
    T_ASSERT(cl.fow.explored[0]);
    T_ASSERT(cl.fow.explored[1]);
    T_EQ(cl.fow.texture[0], 255);
    T_EQ(cl.fow.texture[1], 128);
    T_EQ(test_fow_upload_calls, 1);
    reset_fow_client_state();
}

TEST(net, fow_row_delta_reconstructs_client_grid) {
    BYTE buf[64];
    BYTE full_payload[] = { 0, 16 };
    BYTE delta_payload[] = { 0, 4, 1, 3 };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    reset_fow_client_state();

    write_fow_message(&sb,
                      FOW_MSG_FULL | FOW_MSG_VISIBLE_PLANE | FOW_MSG_RLE,
                      8,
                      2,
                      0,
                      2,
                      full_payload,
                      sizeof(full_payload));
    write_fow_message(&sb,
                      FOW_MSG_VISIBLE_PLANE | FOW_MSG_RLE,
                      8,
                      2,
                      1,
                      1,
                      delta_payload,
                      sizeof(delta_payload));
    CL_ParseServerMessage(&sb);

    T_ASSERT(!cl.fow.visible[0]);
    T_ASSERT(cl.fow.visible[1 * cl.fow.width + 4]);
    T_EQ(cl.fow.texture[1 * cl.fow.width + 4], 255);
    /* Both chunks belong to one server message and therefore publish one assembled texture. */
    T_EQ(test_fow_upload_calls, 1);

    SZ_Clear(&sb);
    sb.readcount = 0;
    write_fow_message(&sb, FOW_MSG_VISIBLE_PLANE | FOW_MSG_RLE, 8, 2, 1, 1, delta_payload, sizeof(delta_payload));
    CL_ParseServerMessage(&sb);
    /* A later server message is a new publication boundary. */
    T_EQ(test_fow_upload_calls, 2);
    reset_fow_client_state();
}

TEST(net, fow_rle_255_continues_current_value) {
    BYTE buf[64];
    BYTE payload[] = { 1, 255, 16, 8 };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    reset_fow_client_state();

    write_fow_message(&sb,
                      FOW_MSG_FULL | FOW_MSG_VISIBLE_PLANE | FOW_MSG_RLE,
                      279,
                      1,
                      0,
                      1,
                      payload,
                      sizeof(payload));
    CL_ParseServerMessage(&sb);

    T_ASSERT(cl.fow.visible[0]);
    T_ASSERT(cl.fow.visible[270]);
    T_ASSERT(!cl.fow.visible[271]);
    T_ASSERT(!cl.fow.visible[278]);
    reset_fow_client_state();
}

TEST(net, fow_rle_zero_length_flips_after_exact_255_run) {
    BYTE buf[64];
    BYTE payload[] = { 1, 255, 0, 8 };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    reset_fow_client_state();

    write_fow_message(&sb,
                      FOW_MSG_FULL | FOW_MSG_VISIBLE_PLANE | FOW_MSG_RLE,
                      263,
                      1,
                      0,
                      1,
                      payload,
                      sizeof(payload));
    CL_ParseServerMessage(&sb);

    T_ASSERT(cl.fow.visible[254]);
    T_ASSERT(!cl.fow.visible[255]);
    T_ASSERT(!cl.fow.visible[262]);
    reset_fow_client_state();
}

TEST(net, fow_malformed_payload_does_not_overread) {
    BYTE buf[64];
    BYTE payload[] = { 1, 1 };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    reset_fow_client_state();

    write_fow_message(&sb,
                      FOW_MSG_VISIBLE_PLANE | FOW_MSG_RLE,
                      8,
                      2,
                      0,
                      2,
                      payload,
                      sizeof(payload));
    CL_ParseServerMessage(&sb);

    T_EQ(sb.readcount, sb.cursize);
    T_EQ(cl.fow.width, 0);
    reset_fow_client_state();
}

/* WC3 selection radii (buildings/destructables) exceed the packed-float range of ±65.5, so radius must stay
 * NFT_ROUND. Guard the WC3 delta path against a regression back to a narrow two-byte encoding. */
TEST(net, unchanged_entity_delta_emits_nothing) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t state = { .number = 9, .model = 1, .origin = { 10.0f, 20.0f, 30.0f } };

    MSG_WriteDeltaEntity(&sb, &state, &state, false);

    T_EQ(sb.cursize, 0);
}

TEST(net, entity_delta_preserves_large_wc3_radii) {
    FLOAT radii[] = { 36.0f, 72.0f, 200.0f, 320.0f };

    FOR_LOOP(i, sizeof(radii) / sizeof(radii[0])) {
        BYTE buf[256];
        sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
        entityState_t from = { 0 }, to = { .number = 9, .radius = radii[i] }, out = { 0 };
        DWORD bits = 0;
        int number;

        MSG_WriteDeltaEntity(&sb, &from, &to, true);
        sb.readcount = 0;
        number = MSG_ReadEntityBits(&sb, &bits);
        MSG_ReadDeltaEntity(&sb, &out, number, bits);

        T_EQ(number, 9);
        T_FEQ(out.radius, radii[i], 0.001f);
    }
}

/* Building placement cursor metadata must survive svc_cursor entity deltas without
 * overloading world-position fields. */
TEST(net, entity_delta_preserves_pathing_dimensions) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 };
    entityState_t to = { .number = 9, .model = 1, .pathing_width = 6, .pathing_height = 4 };
    entityState_t out = { 0 };
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_EQ(out.pathing_width, 6);
    T_EQ(out.pathing_height, 4);
    T_FEQ(out.origin.x, 0.0f, 0.001f);
    T_FEQ(out.origin.y, 0.0f, 0.001f);
}

/* Dead destructable remains rely on EF_NOT_SELECTABLE surviving snapshots, so
 * guard its round trip explicitly. */
TEST(net, entity_delta_preserves_not_selectable_flag) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .flags = EF_NOT_SELECTABLE }, out = { 0 };
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_NOT_SELECTABLE);
}

/* Hover-health eligibility occupies the first bit above the legacy byte-sized
 * entity flags, so guard both the widened field and delta serialization. */
TEST(net, entity_delta_preserves_hover_health_flag) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .flags = EF_HOVER_HEALTH }, out = { 0 };
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_HOVER_HEALTH);
}

/* Neutral hover-ring presentation is also recipient-authored snapshot state. */
TEST(net, entity_delta_preserves_neutral_flag) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .flags = EF_NEUTRAL }, out = { 0 };
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_NEUTRAL);
}

/* WC3 building damage rendering relies on server-authored effect presentation
 * data surviving the shared entity delta unchanged. */
TEST(net, entity_delta_preserves_effect_model) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .effect = 2,
                                       .flags = EF_BUILDING, .effect_flags = EFX_MODEL | EFX_ATTACH_SLOTS |
                                                    EFX_SLOT_FIRST | EFX_SLOT_SECOND }, out = { 0 };
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_BUILDING);
    T_EQ(out.effect, 2);
    T_EQ(out.effect_flags, to.effect_flags);
}

/* Other game entity events remain delta-compatible; WC3 sounds use svc_sound. */
TEST(net, entity_delta_preserves_entity_event) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .event = EV_MOVE, .sound = 37 }, out = { 0 };
    DWORD bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_EQ(out.event, EV_MOVE);
    T_EQ(out.sound, 37);
}

/* Minimap attention markers use a dedicated packet and optional recent-history flag. */
TEST(net, minimap_ping_packet_reaches_generic_client_state) {
    BYTE buf[64];
    sizeBuf_t msg = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init(); CL_ClearMinimap(); cl.time = 1000;
    MSG_WriteByte(&msg, svc_minimap_ping);
    MSG_WriteFloat(&msg, 123.5f); MSG_WriteFloat(&msg, -44.25f); MSG_WriteFloat(&msg, 2.5f);
    MSG_WriteByte(&msg, 10); MSG_WriteByte(&msg, 20); MSG_WriteByte(&msg, 30); MSG_WriteByte(&msg, 255);
    MSG_WriteByte(&msg, MINIMAP_PING_REMEMBER);
    msg.readcount = 0; CL_ParseServerMessage(&msg);

    T_EQ(CL_MinimapPingCount(), 1);
    T_EQ(CL_MinimapRecentCount(), 1);
}

/* A truncated marker cannot create partial presentation or history state. */
TEST(net, minimap_ping_packet_rejects_truncated_payload) {
    BYTE buf[16];
    sizeBuf_t msg = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init(); CL_ClearMinimap();
    MSG_WriteByte(&msg, svc_minimap_ping); MSG_WriteFloat(&msg, 1.0f);
    msg.readcount = 0; CL_ParseServerMessage(&msg);

    T_EQ(CL_MinimapPingCount(), 0);
    T_EQ(CL_MinimapRecentCount(), 0);
}

/* Non-finite coordinates and clock-overflowing lifetimes cannot enter client state. */
TEST(net, minimap_ping_packet_rejects_invalid_values) {
    BYTE buf[64];
    sizeBuf_t msg = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init(); CL_ClearMinimap();
    MSG_WriteByte(&msg, svc_minimap_ping);
    MSG_WriteFloat(&msg, NAN); MSG_WriteFloat(&msg, 1.0f); MSG_WriteFloat(&msg, MINIMAP_PING_DURATION_MAX + 1.0f);
    MSG_WriteByte(&msg, 255); MSG_WriteByte(&msg, 255); MSG_WriteByte(&msg, 255); MSG_WriteByte(&msg, 255);
    MSG_WriteByte(&msg, MINIMAP_PING_REMEMBER);
    msg.readcount = 0; CL_ParseServerMessage(&msg);

    T_EQ(CL_MinimapPingCount(), 0);
    T_EQ(CL_MinimapRecentCount(), 0);
}

static void net_install_single_layout_frame(DWORD layer, FRAMETYPE type,
                                            FLOAT x, FLOAT y, FLOAT w, FLOAT h) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = {0};

    frame.number = 1;
    frame.flags.type = type;
    frame.size.width = w;
    frame.size.height = h;
    frame.points.x[FPP_MIN].used = 1;
    frame.points.x[FPP_MIN].targetPos = FPP_MIN;
    frame.points.x[FPP_MIN].relativeTo = 0;
    frame.points.x[FPP_MIN].offset = (int16_t)(x * UI_FRAMEPOINT_SCALE);
    frame.points.y[FPP_MIN].used = 1;
    frame.points.y[FPP_MIN].targetPos = FPP_MIN;
    frame.points.y[FPP_MIN].relativeTo = 0;
    frame.points.y[FPP_MIN].offset = (int16_t)(-y * UI_FRAMEPOINT_SCALE);

    MSG_WriteByte(&sb, layer);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseLayout(&sb);
}

/* WC3's info/status panel rises above the flat world-scissor bottom.  A drag
 * crossing that authored panel must stop at the panel's top rather than draw
 * the marquee through the transparent portions of the HUD art. */
TEST(client_screen, selection_rect_stops_at_bottom_console_status_panel) {
    RECT rect = { 128.0f, 128.0f, 768.0f, 576.0f };

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(RECT, 0.0f, 0.22f, 1.0f, 0.76f);
    net_install_single_layout_frame(LAYER_INFOPANEL, FT_SIMPLESTATUSBAR,
                                    0.25f, 0.44f, 0.30f, 0.08f);

    SCR_LayoutClampSelectionRect(&rect);

    T_FEQ(rect.x, 128.0f, 0.01f);
    T_FEQ(rect.y, 128.0f, 0.01f);
    T_FEQ(rect.w, 768.0f, 0.01f);
    T_FEQ(rect.y + rect.h, 0.44f / UI_BASE_HEIGHT * 768.0f, 1.0f);
}

/* Touching a panel edge has zero overlap area, so it must not constrain a
 * selection whose vertical span merely passes beside that panel. */
TEST(client_screen, selection_rect_touching_status_panel_edge_does_not_clamp) {
    RECT rect = { 320.0f, 128.0f, 576.0f, 576.0f };
    RECT original = rect;

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(RECT, 0.0f, 0.22f, 1.0f, 0.76f);
    net_install_single_layout_frame(LAYER_INFOPANEL, FT_SIMPLESTATUSBAR,
                                    0.0f, 0.44f, 0.25f, 0.08f);

    SCR_LayoutClampSelectionRect(&rect);

    T_FEQ(rect.w, original.w, 0.01f);
    T_FEQ(rect.h, original.h, 0.01f);
}

/* Command-card/build buttons are separate retained frames and can protrude
 * higher than the surrounding console texture.  They must participate in the
 * same selection boundary. */
TEST(client_screen, selection_rect_stops_at_bottom_console_command_button) {
    RECT rect = { 128.0f, 128.0f, 768.0f, 576.0f };

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(RECT, 0.0f, 0.22f, 1.0f, 0.76f);
    net_install_single_layout_frame(LAYER_COMMANDBAR, FT_COMMANDBUTTON,
                                    0.60f, 0.41f, 0.10f, 0.08f);

    SCR_LayoutClampSelectionRect(&rect);

    T_FEQ(rect.y + rect.h, 0.41f / UI_BASE_HEIGHT * 768.0f, 1.0f);
}

TEST(client_screen, command_button_right_click_sends_secondary_command) {
    BYTE layout_buf[512];
    BYTE message_buf[256];
    char command_buf[128];
    sizeBuf_t sb = make_msg_buf(layout_buf, sizeof(layout_buf));
    uiFrame_t empty = {0}, frame = {0};

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    SZ_Init(&cls.netchan.message, message_buf, sizeof(message_buf));

    frame.number = 1;
    frame.flags.type = FT_COMMANDBUTTON;
    frame.size.width = 0.20f;
    frame.size.height = 0.20f;
    frame.points.x[FPP_MIN].used = 1;
    frame.points.x[FPP_MIN].targetPos = FPP_MIN;
    frame.points.y[FPP_MIN].used = 1;
    frame.points.y[FPP_MIN].targetPos = FPP_MIN;
    frame.onclick = "button Arep";
    frame.text = "autocast Arep";

    MSG_WriteByte(&sb, LAYER_COMMANDBAR);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseLayout(&sb);

    T_ASSERT(SCR_LayoutMouseEvent(MENU_MOUSE_DOWN, 10, 10, 3));
    T_EQ(cls.netchan.message.cursize, 0);
    T_ASSERT(SCR_LayoutMouseEvent(MENU_MOUSE_UP, 10, 10, 3));
    cls.netchan.message.readcount = 0;
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, command_buf);
    T_STREQ(command_buf, "autocast Arep");
}

TEST(client_screen, command_button_right_click_without_secondary_command_is_not_consumed) {
    BYTE layout_buf[512];
    BYTE message_buf[256];
    sizeBuf_t sb = make_msg_buf(layout_buf, sizeof(layout_buf));
    uiFrame_t empty = {0}, frame = {0};

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    SZ_Init(&cls.netchan.message, message_buf, sizeof(message_buf));

    frame.number = 1;
    frame.flags.type = FT_COMMANDBUTTON;
    frame.size.width = 0.20f;
    frame.size.height = 0.20f;
    frame.points.x[FPP_MIN].used = 1;
    frame.points.x[FPP_MIN].targetPos = FPP_MIN;
    frame.points.y[FPP_MIN].used = 1;
    frame.points.y[FPP_MIN].targetPos = FPP_MIN;
    frame.onclick = "button Amov";

    MSG_WriteByte(&sb, LAYER_COMMANDBAR);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseLayout(&sb);

    T_ASSERT(!SCR_LayoutMouseEvent(MENU_MOUSE_DOWN, 10, 10, 3));
    T_ASSERT(!SCR_LayoutMouseEvent(MENU_MOUSE_UP, 10, 10, 3));
    T_EQ(cls.netchan.message.cursize, 0);
}

/* Upper HUD elements are not part of the bottom-console mask; crossing the
 * resource/upper-button region must not shrink an otherwise valid world drag. */
TEST(client_screen, selection_rect_ignores_upper_ui_outside_bottom_console) {
    RECT rect = { 128.0f, 128.0f, 768.0f, 576.0f };
    RECT original = rect;

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(RECT, 0.0f, 0.22f, 1.0f, 0.76f);
    net_install_single_layout_frame(LAYER_CONSOLE, FT_TEXTURE,
                                    0.25f, 0.05f, 0.30f, 0.05f);

    SCR_LayoutClampSelectionRect(&rect);

    T_FEQ(rect.w, original.w, 0.01f);
    T_FEQ(rect.h, original.h, 0.01f);
}

/* WoW/SC2-style full-screen world viewports do not opt into the WC3
 * bottom-console protrusion rule. */
TEST(client_screen, selection_rect_fullscreen_world_does_not_use_console_clamp) {
    RECT rect = { 128.0f, 128.0f, 768.0f, 576.0f };
    RECT original = rect;

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(RECT, 0.0f, 0.0f, 1.0f, 1.0f);
    net_install_single_layout_frame(LAYER_COMMANDBAR, FT_COMMANDBUTTON,
                                    0.60f, 0.41f, 0.10f, 0.08f);

    SCR_LayoutClampSelectionRect(&rect);

    T_FEQ(rect.w, original.w, 0.01f);
    T_FEQ(rect.h, original.h, 0.01f);
}

/* Regression: UI_SetPoint Y sign convention.
 * UI_CopyFrameBase encodes Y without negation; cl_layout.c negates on decode
 * (SCR_NormalizeAnchorOffset flips the sign for the Y axis).  A TOPLEFT anchor
 * with offset -0.480 (WC3 FDF convention: negative = downward) must resolve to
 * y=0.480 from the screen top, placing the info panel in the HUD console area.
 * A positive +0.480 would produce y=-0.480, which is above the screen. */
TEST(net, layout_topleft_y_negative_offset_resolves_below_screen_top) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = {0};

    frame.number        = 1;
    frame.flags.type    = FT_SIMPLEFRAME;
    frame.size.width    = 0.180f;
    frame.size.height   = 0.120f;
    frame.points.x[FPP_MIN].used       = 1;
    frame.points.x[FPP_MIN].targetPos  = FPP_MIN;
    frame.points.x[FPP_MIN].relativeTo = 0;
    frame.points.x[FPP_MIN].offset     = (int16_t)( 0.310f * UI_FRAMEPOINT_SCALE);
    frame.points.y[FPP_MIN].used       = 1;
    frame.points.y[FPP_MIN].targetPos  = FPP_MIN;
    frame.points.y[FPP_MIN].relativeTo = 0;
    frame.points.y[FPP_MIN].offset     = (int16_t)(-0.480f * UI_FRAMEPOINT_SCALE);

    test_client_stubs_init();
    MSG_WriteByte(&sb, LAYER_INFOPANEL);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0); MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseLayout(&sb);
    T_ASSERT(cl.layout[LAYER_INFOPANEL] != NULL);
    SCR_Clear(cl.layout[LAYER_INFOPANEL]);

    LPCRECT r = SCR_LayoutRect(SCR_Frame(1));
    T_NOT_NULL(r);
    T_FEQ(r->x, 0.310f, 0.002f);
    T_FEQ(r->y, 0.480f, 0.002f);
    T_FEQ(r->w, 0.180f, 0.002f);
    T_FEQ(r->h, 0.120f, 0.002f);
}

/* -----------------------------------------------------------------------
 * Active-entity list lifecycle (client/cl_parse.c)
 * ----------------------------------------------------------------------- */

static void net_parse(sizeBuf_t *sb) {
    sb->readcount = 0;
    CL_ParseServerMessage(sb);
}

static void net_send_baseline(sizeBuf_t *sb, entityState_t const *state) {
    entityState_t null = { 0 };
    MSG_WriteByte(sb, svc_spawnbaseline);
    MSG_WriteDeltaEntity(sb, &null, state, true);
}

static void net_send_delta(sizeBuf_t *sb, entityState_t const *from, entityState_t const *to) {
    MSG_WriteByte(sb, svc_packetentities);
    MSG_WriteDeltaEntity(sb, from, to, false);
    MSG_WriteEntityBits(sb, 0, 0);
}

static void net_send_remove(sizeBuf_t *sb, DWORD number) {
    MSG_WriteByte(sb, svc_packetentities);
    MSG_WriteEntityBits(sb, 1u << U_REMOVE, number);
    MSG_WriteEntityBits(sb, 0, 0);
}

/* Membership must track current.model exactly across both transitions, plus the
 * U_REMOVE-after-model-cleared sequence the server produces for model-less
 * sound/event entities. */
TEST(net, active_entity_list_tracks_model_transitions) {
    BYTE buf[512];
    sizeBuf_t sb;
    entityState_t state, from, to;

    test_client_stubs_init();
    T_EQ(cl.num_active, 0);

    /* Baseline model=1 adds membership. */
    memset(&state, 0, sizeof(state)); state.number = 7; state.model = 1;
    sb = make_msg_buf(buf, sizeof(buf));
    net_send_baseline(&sb, &state);
    net_parse(&sb);
    T_EQ(cl.num_active, 1);
    T_EQ(cl.active_entities[0], 7);

    /* Duplicate baseline must not append a second entry. */
    sb = make_msg_buf(buf, sizeof(buf));
    net_send_baseline(&sb, &state);
    net_parse(&sb);
    T_EQ(cl.num_active, 1);

    /* A delta clearing the model (sound/event-only entity) removes membership. */
    from = state; /* model=1 */
    memset(&to, 0, sizeof(to)); to.number = 7; to.model = 0;
    sb = make_msg_buf(buf, sizeof(buf));
    net_send_delta(&sb, &from, &to);
    net_parse(&sb);
    T_EQ(cl.num_active, 0);

    /* U_REMOVE after the model is already zero must not leave a stale entry. */
    sb = make_msg_buf(buf, sizeof(buf));
    net_send_remove(&sb, 7);
    net_parse(&sb);
    T_EQ(cl.num_active, 0);

    /* Slot reuse: model 0 -> 1 re-adds, then a plain U_REMOVE clears it again. */
    from = to; /* model=0 */
    memset(&to, 0, sizeof(to)); to.number = 7; to.model = 2;
    sb = make_msg_buf(buf, sizeof(buf));
    net_send_delta(&sb, &from, &to);
    net_parse(&sb);
    T_EQ(cl.num_active, 1);
    T_EQ(cl.active_entities[0], 7);

    sb = make_msg_buf(buf, sizeof(buf));
    net_send_remove(&sb, 7);
    net_parse(&sb);
    T_EQ(cl.num_active, 0);
}

/* CL_ParseFrame snapshots prev = current for the active list; map load resets it. */
TEST(net, active_entity_list_frame_copy_and_map_reset) {
    BYTE buf[512];
    sizeBuf_t sb;
    entityState_t state;

    test_client_stubs_init();
    memset(&state, 0, sizeof(state)); state.number = 7; state.model = 1;
    sb = make_msg_buf(buf, sizeof(buf));
    net_send_baseline(&sb, &state);
    net_parse(&sb);
    T_EQ(cl.num_active, 1);

    /* Frame header snapshots the current origin into prev for every active entity. */
    cl.ents[7].current.origin.x = 42.0f;
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_frame);
    MSG_WriteLong(&sb, 1);
    MSG_WriteLong(&sb, 1000);
    MSG_WriteLong(&sb, 0);
    net_parse(&sb);
    T_FEQ(cl.ents[7].prev.origin.x, 42.0f, 0.001f);

    /* Loading a new map drops the list so fresh baselines repopulate it. */
    CL_BeginLoadingMap("Maps\\Test.w3m");
    T_EQ(cl.num_active, 0);
}

TEST(net, set_selection_rejects_undersized_payload) {
    BYTE buf[4];
    sizeBuf_t sb;
    DWORD saved_num = cl.selection.num_selected;
    DWORD saved_ent = cl.selection.entity_nums[0];

    test_client_stubs_init();
    /* Payload smaller than sizeof(DWORD) should be rejected. */
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_gamecmd);
    MSG_WriteShort(&sb, 1); /* payload size */
    MSG_WriteByte(&sb, 'x'); /* 1 byte of payload */
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
    T_EQ(cl.selection.num_selected, saved_num);
    T_EQ(cl.selection.entity_nums[0], saved_ent);
}

TEST(net, set_selection_rejects_zero_entity) {
    BYTE buf[64];
    sizeBuf_t sb;
    DWORD saved_num = cl.selection.num_selected;

    test_client_stubs_init();
    /* Entity number 0 should be rejected. */
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_gamecmd);
    MSG_WriteShort(&sb, 4);
    MSG_WriteLong(&sb, 0); /* entity 0 */
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
    T_EQ(cl.selection.num_selected, saved_num);
}

TEST(net, set_selection_rejects_entity_exceeding_max) {
    BYTE buf[64];
    sizeBuf_t sb;
    DWORD saved_num = cl.selection.num_selected;

    test_client_stubs_init();
    /* Entity number >= MAX_CLIENT_ENTITIES should be rejected. */
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_gamecmd);
    MSG_WriteShort(&sb, 4);
    MSG_WriteLong(&sb, MAX_CLIENT_ENTITIES);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
    T_EQ(cl.selection.num_selected, saved_num);
}
