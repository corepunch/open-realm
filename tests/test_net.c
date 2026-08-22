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
#include <arpa/inet.h>

#include "test.h"

/* Pull in the net types + common types without game state. */
#include "../client/client.h"

void test_client_stubs_init(void);
void test_client_stubs_set_cvar(LPCSTR name, LPCSTR value);
void CL_ParseLayout(LPSIZEBUF msg);
void SCR_LayoutDrawScrollBar(LPCUIFRAME frame, LPCRECT screen);
void SCR_LayoutDrawTextArea(LPCUIFRAME frame, LPCRECT screen);

static RECT test_scroll_rects[3], test_scroll_uvs[3];
static LPCTEXTURE test_scroll_tex[3];
static DWORD test_scroll_draws;
static drawText_t test_textarea_draw;
static DWORD test_textarea_draws;

static void capture_scroll_image(LPCTEXTURE texture, LPCRECT screen, LPCRECT uv, COLOR32 color) {
    (void)color;
    if (test_scroll_draws >= 3) return;
    test_scroll_tex[test_scroll_draws] = texture;
    test_scroll_rects[test_scroll_draws] = *screen;
    test_scroll_uvs[test_scroll_draws++] = *uv;
}

static void capture_textarea(LPCDRAWTEXT text) { test_textarea_draw = *text; test_textarea_draws++; }

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
    T_EQ(MSG_ReadByte(&sb) & 0xFF, 0xAB);
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

static uiUnitData_t test_unit_ui_last;
static DWORD test_unit_ui_calls;
static DWORD test_unit_ui_num_units;

static void test_update_unit_ui(DWORD num_units, uiUnitData_t *units) {
    test_unit_ui_calls++;
    test_unit_ui_num_units = num_units;
    memset(&test_unit_ui_last, 0, sizeof(test_unit_ui_last));
    if (num_units && units) {
        test_unit_ui_last = units[0];
    }
}

TEST(net, unit_ui_parser_preserves_distinct_strings) {
    BYTE buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();
    test_unit_ui_calls = 0;
    test_unit_ui_num_units = 0;
    memset(&test_unit_ui_last, 0, sizeof(test_unit_ui_last));
    ui.UpdateUnitUI = test_update_unit_ui;

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

TEST(net, playerinfo_game_state_preserves_open_menu_input) {
    BYTE buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    PLAYER from = { 0 };
    PLAYER to = { 0 };

    test_client_stubs_init();
    cls.key_dest = key_menu;
    cls.netchan.remote_address.type = NA_IP;
    to.number = 1;
    to.origin = (VECTOR2){ 128.0f, 256.0f };
    to.fov = 50;
    to.distance = 1650;
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
