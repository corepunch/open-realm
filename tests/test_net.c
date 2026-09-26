#include <zlib.h>
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

static cstring_t minimap_map;
static void capture_minimap(rect_t const *screen, cstring_t map) { (void)screen; minimap_map = map; }

static sizeBuf_t make_msg_buf(uint8_t *buf, uint32_t bufsz);

void test_client_stubs_init(void);
void test_client_stubs_set_window_size(uint32_t width, uint32_t height);
void test_client_stubs_set_canvas_policy(UICANVASPOLICY policy);
void test_client_stubs_set_cvar(cstring_t name, cstring_t value);
void test_client_stubs_set_world_bounds(box2_t bounds);
void test_client_stubs_set_existing_file(cstring_t path);
void CL_ParseLayout(sizeBuf_t *msg);
void CL_ParseFrame(sizeBuf_t *msg);
void SCR_LayoutDrawScrollBar(uiFrame_t const *frame, rect_t const *screen);
void SCR_LayoutDrawStatusbar(uiFrame_t const *frame, rect_t const *screen);
void SCR_LayoutDrawSegmentedStatusbar(uiFrame_t const *frame, rect_t const *screen);
void SCR_LayoutDrawTexture(uiFrame_t const *frame, rect_t const *screen);
void SCR_LayoutDrawTextArea(uiFrame_t const *frame, rect_t const *screen);
void SCR_LayoutDrawListBox(uiFrame_t const *frame, rect_t const *screen);
void SCR_LayoutDrawSprite(uiFrame_t const *frame, rect_t const *screen);
void SCR_LayoutDrawOverlay(handle_t layout);
void SCR_LayoutDrawLoadingBar(uiFrame_t const *frame, rect_t const *screen);
void SCR_LayoutClampSelectionRect(rect_t *rect);
bool SCR_LayoutModalActive(void);
void SCR_UpdateScreen(uint32_t msec);
extern bool scr_initialized;
void test_client_stubs_clear_cvars(void);
extern uint32_t test_fow_upload_calls;
extern uint32_t test_cursor_draw_calls;
extern color32_t test_cursor_tint;
extern char test_forwarded_command[128];
extern char test_menu_action[32];
extern char test_menu_action_arg[128];
extern char test_console_message[MAX_CONSOLE_MESSAGE_LEN];

static rect_t test_scroll_rects[3], test_scroll_uvs[3];
static texture_t const *test_scroll_tex[3];
static uint32_t test_scroll_draws;
static drawText_t test_textarea_draw;
static uint32_t test_textarea_draws;
static drawText_t test_listbox_draw[8];
static uint32_t test_listbox_draws;
static uint32_t test_begin_frames, test_end_frames;
static uint32_t test_model_loads, test_model_releases, test_tex_loads, test_tex_releases;
static vec3_t test_overhead_point;
static rect_t test_status_rect;
static uint32_t test_status_draws;
static texture_t const *test_status_textures[16];
static color32_t test_status_colors[16];
static rect_t test_fade_rect;
static color32_t test_fade_color;
static uint32_t test_fade_draws;
static PATHSTR test_model_load_paths[4];
static char test_sprite_anim[96];
static uint32_t test_sprite_draws;

static model_t *capture_load_model(cstring_t filename) {
    uint32_t slot = test_model_loads;
    if (slot < sizeof(test_model_load_paths) / sizeof(test_model_load_paths[0]))
        snprintf(test_model_load_paths[slot], sizeof(test_model_load_paths[slot]), "%s", filename ? filename : "");
    test_model_loads++;
    return (model_t *)(uintptr_t)(0x1000u + test_model_loads);
}

static void capture_release_model(model_t *model) {
    (void)model;
    test_model_releases++;
}

static void capture_scroll_image(texture_t const *texture, rect_t const *screen, rect_t const *uv, color32_t color) {
    (void)color;
    if (test_scroll_draws >= 3) return;
    test_scroll_tex[test_scroll_draws] = texture;
    test_scroll_rects[test_scroll_draws] = *screen;
    test_scroll_uvs[test_scroll_draws++] = *uv;
}

static void capture_textarea(drawText_t const *text) { test_textarea_draw = *text; test_textarea_draws++; }
static void capture_listbox_text(drawText_t const *text) {
    if (test_listbox_draws < sizeof(test_listbox_draw) / sizeof(test_listbox_draw[0]))
        test_listbox_draw[test_listbox_draws] = *text;
    test_listbox_draws++;
}
static vec2_t tall_textarea_size(drawText_t const *text) {
    (void)text;
    return MAKE(vec2_t, 0.2f, 0.8f);
}
static void capture_begin_frame(void) { test_begin_frames++; }
static void capture_end_frame(void) { test_end_frames++; }
static bool capture_overhead_point(renderEntity_t const *entity, vec3_t *out) {
    (void)entity; *out = test_overhead_point; return true;
}
static void capture_status_image(texture_t const *texture, rect_t const *screen, rect_t const *uv, color32_t color) {
    (void)uv; test_status_rect = *screen;
    if (test_status_draws < sizeof(test_status_textures) / sizeof(test_status_textures[0])) {
        test_status_textures[test_status_draws] = texture;
        test_status_colors[test_status_draws] = color;
    }
    test_status_draws++;
}
static void capture_fade_image(texture_t const *texture, rect_t const *screen, rect_t const *uv, color32_t color) {
    (void)texture; (void)uv; test_fade_rect = *screen; test_fade_color = color; test_fade_draws++;
}
static texture_t *capture_load_texture(cstring_t name) {
    (void)name; test_tex_loads++; return (texture_t *)(uintptr_t)test_tex_loads;
}
static void capture_release_texture(texture_t *texture) { (void)texture; test_tex_releases++; }
static void capture_sprite(drawSprite_t const *sprite) {
    cstring_t anim = sprite->anim;
    test_sprite_draws++;
    snprintf(test_sprite_anim, sizeof(test_sprite_anim), "%s", anim ? anim : "");
}

TEST(client_layout, context_name_resolves_hover_entity_configstring) {
    uiFrame_t frame = { .stat = UI_STAT_CONTEXT_NAME };
    uint32_t const entnum = 7, name = 3;
    uint32_t const ni = name - 1;

    test_client_stubs_init();
    SCR_ClearLayer(NULL, LAYER_WORLD_HOVER);
    cl.hover_entity = entnum;
    cl.ents[entnum].current = (entityState_t){
        .model = 1, .name = name, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 255 },
    };
    memset(cl.configstrings[CS_GENERAL], 0, sizeof(cl.configstrings[CS_GENERAL]));
    snprintf(cl.configstrings[CS_GENERAL] + (ni & 0xF) * ENT_NAME_SLOT_SIZE, ENT_NAME_SLOT_SIZE, "Footman");

    T_STREQ(SCR_GetStringValue(&frame), "Footman");
}

TEST(client_layout, context_name_appends_live_hover_value) {
    uiFrame_t frame = { .stat = UI_STAT_CONTEXT_NAME, .text = "Gold:" };
    uint32_t const entnum = 7, name = 3;
    uint32_t const ni = name - 1;

    test_client_stubs_init();
    SCR_ClearLayer(NULL, LAYER_WORLD_HOVER);
    cl.hover_entity = entnum;
    cl.ents[entnum].current = (entityState_t){
        .model = 1, .name = name, .hover_value = 12501,
        .stats = { [ENT_HEALTH] = 255 },
    };
    memset(cl.configstrings[CS_GENERAL], 0, sizeof(cl.configstrings[CS_GENERAL]));
    snprintf(cl.configstrings[CS_GENERAL] + (ni & 0xF) * ENT_NAME_SLOT_SIZE, ENT_NAME_SLOT_SIZE, "Gold Mine");

    T_STREQ(SCR_GetStringValue(&frame), "Gold Mine\nGold: 12500");
    cl.ents[entnum].current.hover_value = 12491;
    T_STREQ(SCR_GetStringValue(&frame), "Gold Mine\nGold: 12490");
    cl.ents[entnum].current.hover_value = 1;
    T_STREQ(SCR_GetStringValue(&frame), "Gold Mine\nGold: 0");
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

TEST(client_layout, tooltip_time_token_uses_live_environment_phase) {
    uiFrame_t frame = {
        .stat = UI_PLAYERSTAT_ENV_PHASE,
        .value = 24.0f,
        .tooltip = "Time of Day ( |Cfffed312{time}|R )\nThis is the current time of day.",
    };
    uint32_t const minutes = 19u * 60u + 13u;

    test_client_stubs_init();
    cl.playerstate.stats[UI_PLAYERSTAT_ENV_PHASE] =
        (uint16_t)(((uint64_t)minutes * UINT16_MAX + (24u * 60u) / 2u) / (24u * 60u));
    T_STREQ(SCR_GetTooltipText(&frame),
            "Time of Day ( |Cfffed31219:13|R )\nThis is the current time of day.");

    /* The tooltip derives from snapshot state on every hover/update rather
     * than freezing the time into the original layout packet. */
    cl.playerstate.stats[UI_PLAYERSTAT_ENV_PHASE] = UINT16_MAX / 4u;
    T_STREQ(SCR_GetTooltipText(&frame),
            "Time of Day ( |Cfffed31206:00|R )\nThis is the current time of day.");
}

TEST(client_layout, world_hover_root_projects_model_top_into_ui_canvas) {
    rect_t root;
    renderEntity_t render = { .number = 7 };

    test_client_stubs_init();
    cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 255 },
    };
    cl.viewDef.entities = &render; cl.viewDef.num_entities = 1;
    cl.viewDef.viewport = cl.viewDef.scissor = MAKE(rect_t, 0, 0.22f, 1, 0.76f);
    Matrix4_identity(&cl.viewDef.viewProjectionMatrix);
    test_overhead_point = MAKE(vec3_t, 0, 0, 0);
    re.GetEntityOverheadPosition = capture_overhead_point;

    T_ASSERT(SCR_LayoutWorldHoverRoot(&root));
    T_FEQ(root.x, UI_BASE_WIDTH * 0.5f, 0.0001f);
    T_FEQ(root.y, UI_BASE_HEIGHT * 0.4f, 0.0001f);
    T_FEQ(root.w, 0.0f, 0.0001f); T_FEQ(root.h, 0.0f, 0.0001f);
}

TEST(client_layout, context_values_follow_hover_snapshot) {
    float value = -1.0f;

    test_client_stubs_init();
    SCR_ClearLayer(NULL, LAYER_WORLD_HOVER);
    cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH | EF_HOVER_MANA,
        .stats = { [ENT_HEALTH] = 128, [ENT_MANA] = 64 },
    };
    T_ASSERT(SCR_LayoutContextValue(UI_STAT_CONTEXT_HEALTH, &value));
    T_FEQ(value, 128.0f / 255.0f, 0.0001f);
    T_ASSERT(SCR_LayoutContextValue(UI_STAT_CONTEXT_MANA, &value));
    T_FEQ(value, 64.0f / 255.0f, 0.0001f);
}

TEST(client_layout, selected_timed_status_reads_normalized_player_stat) {
    float value = -1.0f;

    test_client_stubs_init();
    cl.playerstate.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS] = USHRT_MAX / 2;
    T_ASSERT(SCR_LayoutContextValue(UI_STAT_SELECTION_TIMED_STATUS, &value));
    T_FEQ(value, (USHRT_MAX / 2) / (float)USHRT_MAX, 0.0001f);
}

TEST(client_layout, context_rejects_entity_without_server_hover_capability) {
    float value;

    test_client_stubs_init(); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){ .model = 1, .stats = { [ENT_HEALTH] = 255 } };
    T_ASSERT(!SCR_LayoutContextValue(UI_STAT_CONTEXT_HEALTH, &value));
}

TEST(client_layout, context_rejects_dead_hover_entity) {
    float value;

    test_client_stubs_init(); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .name = 1, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 0 },
    };
    T_NULL(SCR_LayoutContextEntity());
    T_ASSERT(!SCR_LayoutContextValue(UI_STAT_CONTEXT_HEALTH, &value));
}

TEST(client_layout, context_name_survives_invulnerable_without_bars) {
    test_client_stubs_init(); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .name = 1, .stats = { [ENT_HEALTH] = 255 },
    };
    T_NOT_NULL(SCR_LayoutContextEntity());
}

TEST(client_layout, context_mana_zero_is_still_present) {
    float value = -1.0f;

    test_client_stubs_init(); SCR_ClearLayer(NULL, LAYER_WORLD_HOVER); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_MANA, .name = 1,
        .stats = { [ENT_HEALTH] = 255, [ENT_MANA] = 0 },
    };
    T_ASSERT(SCR_LayoutContextValue(UI_STAT_CONTEXT_MANA, &value));
    T_FEQ(value, 0.0f, 0.0001f);
    T_ASSERT(!SCR_LayoutContextValue(UI_STAT_CONTEXT_HEALTH, &value));
}

TEST(client_layout, hover_mana_backdrop_draws_at_empty_pool) {
    uiFrame_t frame = { .stat = UI_STAT_CONTEXT_MANA, .tex = { .index = 1 } };
    rect_t screen = MAKE(rect_t, 0.1f, 0.2f, 0.4f, 0.05f);

    test_client_stubs_init(); SCR_ClearLayer(NULL, LAYER_WORLD_HOVER); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_MANA, .name = 1,
        .stats = { [ENT_HEALTH] = 255, [ENT_MANA] = 0 },
    };
    cl.pics[1] = (texture_t *)(uintptr_t)1; test_status_draws = 0; re.DrawImage = capture_status_image;
    SCR_LayoutDrawTexture(&frame, &screen);
    T_EQ(test_status_draws, 1);
    frame.stat = UI_STAT_CONTEXT_HEALTH;
    SCR_LayoutDrawTexture(&frame, &screen);
    T_EQ(test_status_draws, 1);
}

TEST(client_layout, world_hover_root_rejects_point_outside_world_scissor) {
    rect_t root;
    renderEntity_t render = { .number = 7 };

    test_client_stubs_init(); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 255 },
    };
    cl.viewDef.entities = &render; cl.viewDef.num_entities = 1;
    cl.viewDef.viewport = cl.viewDef.scissor = MAKE(rect_t, 0, 0.22f, 1, 0.76f);
    Matrix4_identity(&cl.viewDef.viewProjectionMatrix);
    test_overhead_point = MAKE(vec3_t, 0, 2, 0);
    re.GetEntityOverheadPosition = capture_overhead_point;
    T_ASSERT(!SCR_LayoutWorldHoverRoot(&root));
}

TEST(client_layout, context_statusbar_uses_hover_snapshot_fraction) {
    uiFrame_t frame = { .stat = UI_STAT_CONTEXT_HEALTH, .tex = { .index = 1 }, .value = 1.0f };
    rect_t screen = MAKE(rect_t, 0.1f, 0.2f, 0.4f, 0.05f);

    test_client_stubs_init(); SCR_ClearLayer(NULL, LAYER_WORLD_HOVER); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH, .stats = { [ENT_HEALTH] = 128 },
    };
    cl.pics[1] = (texture_t *)(uintptr_t)1; test_status_draws = 0; re.DrawImage = capture_status_image;
    SCR_LayoutDrawStatusbar(&frame, &screen);
    T_EQ(test_status_draws, 1); T_FEQ(test_status_rect.w, screen.w * 128.0f / 255.0f, 0.0001f);
}

TEST(client_layout, segmented_statusbar_draws_filled_and_empty_slots_to_capacity) {
    uiFrame_t frame = { .stat = ENT_CARGO, .tex = { .index = 1, .index2 = 2 }, .value = 0.001f };
    rect_t screen = MAKE(rect_t, 0.1f, 0.2f, 0.043f, 0.004f);

    test_client_stubs_init(); SCR_ClearLayer(NULL, LAYER_WORLD_HOVER); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH,
        .stats = { [ENT_HEALTH] = 255, [ENT_CARGO] = EntityCargoPack(3, 8) },
    };
    cl.pics[1] = (texture_t *)(uintptr_t)1; cl.pics[2] = (texture_t *)(uintptr_t)2;
    test_status_draws = 0; re.DrawImage = capture_status_image;
    SCR_LayoutDrawSegmentedStatusbar(&frame, &screen);
    T_EQ(test_status_draws, 8);
    FOR_LOOP(i, 3) T_ASSERT(test_status_textures[i] == cl.pics[1]);
    for (uint32_t i = 3; i < 8; i++) T_ASSERT(test_status_textures[i] == cl.pics[2]);
    T_EQ(test_status_colors[0].r, frame.color.r); T_EQ(test_status_colors[0].g, frame.color.g);
    T_EQ(test_status_colors[0].b, frame.color.b); T_EQ(test_status_colors[0].a, frame.color.a);
    T_EQ(test_status_colors[7].r, COLOR32_WHITE.r); T_EQ(test_status_colors[7].g, COLOR32_WHITE.g);
    T_EQ(test_status_colors[7].b, COLOR32_WHITE.b); T_EQ(test_status_colors[7].a, COLOR32_WHITE.a);
    T_FEQ(test_status_rect.w, (0.043f - 0.007f) / 8.0f, 0.0001f);
    frame.tex.index2 = 0; test_status_draws = 0;
    SCR_LayoutDrawSegmentedStatusbar(&frame, &screen);
    T_EQ(test_status_draws, 3); /* Secondary art is optional for existing senders. */
    cl.ents[7].current.stats[ENT_CARGO] = 0; test_status_draws = 0;
    SCR_LayoutDrawSegmentedStatusbar(&frame, &screen);
    T_EQ(test_status_draws, 0);
}

TEST(client_layout, segmented_statusbar_keeps_empty_capacity_slots_visible) {
    uiFrame_t frame = { .stat = ENT_CARGO, .tex = { .index = 1, .index2 = 2 }, .value = 0.001f };
    rect_t screen = MAKE(rect_t, 0.1f, 0.2f, 0.043f, 0.004f);

    test_client_stubs_init(); SCR_ClearLayer(NULL, LAYER_WORLD_HOVER); cl.hover_entity = 7;
    cl.ents[7].current = (entityState_t){
        .model = 1, .flags = EF_HOVER_HEALTH,
        .stats = { [ENT_HEALTH] = 255, [ENT_CARGO] = EntityCargoPack(0, 8) },
    };
    cl.pics[1] = (texture_t *)(uintptr_t)1; cl.pics[2] = (texture_t *)(uintptr_t)2;
    test_status_draws = 0; re.DrawImage = capture_status_image;
    SCR_LayoutDrawSegmentedStatusbar(&frame, &screen);
    T_EQ(test_status_draws, 8);
    FOR_LOOP(i, 8) T_ASSERT(test_status_textures[i] == cl.pics[2]);
}

static rect_t context_bound_layout_rect(uint32_t layer, FRAMETYPE type, uint32_t stat, entityState_t state, float height) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = {0};

    frame.number = 1; frame.flags.type = type; frame.stat = stat;
    frame.size.width = 0.045f; frame.size.height = height;
    frame.points.x[FPP_MID].used = 1;
    frame.points.x[FPP_MID].targetPos = FPP_MID;
    frame.points.x[FPP_MID].relativeTo = 0;
    frame.points.y[FPP_MAX].used = 1;
    frame.points.y[FPP_MAX].targetPos = FPP_MIN;
    frame.points.y[FPP_MAX].relativeTo = 0;
    frame.points.y[FPP_MAX].offset = (int16_t)(0.002f * UI_FRAMEPOINT_SCALE);

    test_client_stubs_init(); cl.hover_entity = 7; cl.ents[7].current = state;
    MSG_WriteByte(&sb, layer);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true); MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0); MSG_WriteShort(&sb, 0); sb.readcount = 0;
    CL_ParseLayout(&sb); SCR_ClearLayer(cl.layout[layer], layer);
    return *SCR_LayoutRect(SCR_Frame(1));
}

TEST(client_layout, context_bindings_are_world_hover_layer_scoped) {
    entityState_t state = { .model = 1, .name = 1, .stats = { [ENT_HEALTH] = 255 } };
    uiFrame_t wow_name = { .flags.type = FT_STRING, .stat = UI_STAT_CONTEXT_NAME };
    rect_t rect;

    /* Reproduce the command-card failure through retained wire layout geometry,
     * not only through the visibility helper. WC3 command buttons use this same
     * byte for ability indices, including 255 as the no-active-ability sentinel. */
    rect = context_bound_layout_rect(LAYER_COMMANDBAR, FT_COMMANDBUTTON, UI_STAT_CONTEXT_NAME, state, 0.039f);
    T_FEQ(rect.h, 0.039f, 0.0001f);
    rect = context_bound_layout_rect(LAYER_COMMANDBAR, FT_COMMANDBUTTON, UI_STAT_CONTEXT_HEALTH, state, 0.039f);
    T_FEQ(rect.h, 0.039f, 0.0001f);
    rect = context_bound_layout_rect(LAYER_COMMANDBAR, FT_COMMANDBUTTON, UI_STAT_CONTEXT_MANA, state, 0.039f);
    T_FEQ(rect.h, 0.039f, 0.0001f);

    /* The layer gate must not become a WC3 frame-type gate: WoW authors its
     * hover name as FT_STRING rather than FT_NAMETAG. */
    test_client_stubs_init(); cl.hover_entity = 7; cl.ents[7].current = state;
    SCR_ClearLayer(NULL, LAYER_WORLD_HOVER);
    T_ASSERT(SCR_LayoutContextFrameVisible(&wow_name));
    cl.ents[7].current.name = 0;
    T_ASSERT(!SCR_LayoutContextFrameVisible(&wow_name));
}

TEST(client_layout, world_hover_context_rows_collapse_only_when_capability_is_absent) {
    entityState_t state = { .model = 1, .name = 1, .stats = { [ENT_HEALTH] = 255 } };
    rect_t rect;

    rect = context_bound_layout_rect(LAYER_WORLD_HOVER, FT_FRAME, UI_STAT_CONTEXT_HEALTH, state, 0.009f);
    T_FEQ(rect.h, 0.0f, 0.0001f);
    state.flags |= EF_HOVER_HEALTH;
    rect = context_bound_layout_rect(LAYER_WORLD_HOVER, FT_FRAME, UI_STAT_CONTEXT_HEALTH, state, 0.009f);
    T_FEQ(rect.h, 0.009f, 0.0001f);

    state.flags &= ~EF_HOVER_HEALTH;
    rect = context_bound_layout_rect(LAYER_WORLD_HOVER, FT_FRAME, UI_STAT_CONTEXT_MANA, state, 0.009f);
    T_FEQ(rect.h, 0.0f, 0.0001f);
    state.flags |= EF_HOVER_MANA; state.stats[ENT_MANA] = 0;
    rect = context_bound_layout_rect(LAYER_WORLD_HOVER, FT_FRAME, UI_STAT_CONTEXT_MANA, state, 0.009f);
    T_FEQ(rect.h, 0.009f, 0.0001f);

    state.stats[ENT_CARGO] = EntityCargoPack(0, 8);
    rect = context_bound_layout_rect(LAYER_WORLD_HOVER, FT_SEGMENTED_STATUSBAR, ENT_CARGO, state, 0.004f);
    T_FEQ(rect.h, 0.004f, 0.0001f);
    state.stats[ENT_CARGO] = 0;
    rect = context_bound_layout_rect(LAYER_WORLD_HOVER, FT_SEGMENTED_STATUSBAR, ENT_CARGO, state, 0.004f);
    T_FEQ(rect.h, 0.0f, 0.0001f);
}

TEST(client_layout, world_hover_context_rows_compact_through_relative_anchor_chain) {
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, cargo = {0}, mana = {0}, health = {0};
    rect_t const *cargo_rect, *mana_rect, *health_rect;

    cargo.number = 1; cargo.flags.type = FT_SEGMENTED_STATUSBAR; cargo.stat = ENT_CARGO;
    cargo.size.width = 0.043f; cargo.size.height = 0.004f;
    cargo.points.x[FPP_MID] = MAKE(uiFramePoint_t, .used = 1, .targetPos = FPP_MID, .relativeTo = 0);
    cargo.points.y[FPP_MAX] = MAKE(uiFramePoint_t, .used = 1, .targetPos = FPP_MIN, .relativeTo = 0,
                                           .offset = (int16_t)(0.002f * UI_FRAMEPOINT_SCALE));
    mana.number = 2; mana.flags.type = FT_FRAME; mana.stat = UI_STAT_CONTEXT_MANA;
    mana.size.width = 0.045f; mana.size.height = 0.009f;
    mana.points.x[FPP_MID] = MAKE(uiFramePoint_t, .used = 1, .targetPos = FPP_MID, .relativeTo = 0);
    mana.points.y[FPP_MAX] = MAKE(uiFramePoint_t, .used = 1, .targetPos = FPP_MIN, .relativeTo = 1);
    health.number = 3; health.flags.type = FT_FRAME; health.stat = UI_STAT_CONTEXT_HEALTH;
    health.size.width = 0.045f; health.size.height = 0.009f;
    health.points.x[FPP_MID] = MAKE(uiFramePoint_t, .used = 1, .targetPos = FPP_MID, .relativeTo = 0);
    health.points.y[FPP_MAX] = MAKE(uiFramePoint_t, .used = 1, .targetPos = FPP_MIN, .relativeTo = 2);

    test_client_stubs_init(); cl.hover_entity = 7;
    cl.ents[7].current = MAKE(entityState_t, .model = 1, .name = 1,
        .flags = EF_HOVER_HEALTH | EF_HOVER_MANA,
        .stats = { [ENT_HEALTH] = 255, [ENT_CARGO] = EntityCargoPack(0, 8) });
    MSG_WriteByte(&sb, LAYER_WORLD_HOVER);
    MSG_WriteDeltaUIFrame(&sb, &empty, &cargo, true); MSG_WriteByte(&sb, 0);
    MSG_WriteDeltaUIFrame(&sb, &empty, &mana, true); MSG_WriteByte(&sb, 0);
    MSG_WriteDeltaUIFrame(&sb, &empty, &health, true); MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0); MSG_WriteShort(&sb, 0); sb.readcount = 0;
    CL_ParseLayout(&sb); SCR_ClearLayer(cl.layout[LAYER_WORLD_HOVER], LAYER_WORLD_HOVER);
    cargo_rect = SCR_LayoutRect(SCR_Frame(1)); mana_rect = SCR_LayoutRect(SCR_Frame(2)); health_rect = SCR_LayoutRect(SCR_Frame(3));
    T_FEQ(mana_rect->y + mana_rect->h, cargo_rect->y, 0.0001f);
    T_FEQ(health_rect->y + health_rect->h, mana_rect->y, 0.0001f);

    cl.ents[7].current.stats[ENT_CARGO] = 0; SCR_ClearLayer(cl.layout[LAYER_WORLD_HOVER], LAYER_WORLD_HOVER);
    cargo_rect = SCR_LayoutRect(SCR_Frame(1)); mana_rect = SCR_LayoutRect(SCR_Frame(2)); health_rect = SCR_LayoutRect(SCR_Frame(3));
    T_FEQ(cargo_rect->h, 0.0f, 0.0001f);
    T_FEQ(mana_rect->y + mana_rect->h, cargo_rect->y, 0.0001f);
    T_FEQ(health_rect->y + health_rect->h, mana_rect->y, 0.0001f);

    cl.ents[7].current.flags &= ~EF_HOVER_MANA; SCR_ClearLayer(cl.layout[LAYER_WORLD_HOVER], LAYER_WORLD_HOVER);
    cargo_rect = SCR_LayoutRect(SCR_Frame(1)); mana_rect = SCR_LayoutRect(SCR_Frame(2)); health_rect = SCR_LayoutRect(SCR_Frame(3));
    T_FEQ(mana_rect->h, 0.0f, 0.0001f);
    T_FEQ(health_rect->y + health_rect->h, mana_rect->y, 0.0001f);
    T_FEQ(mana_rect->y, cargo_rect->y, 0.0001f);
}

/* Reuse one wire layout while snapshots add/remove capabilities; absent rows
 * must suppress draw calls without suppressing unrelated zero-size sprites. */
TEST(client_layout, context_visibility_and_empty_slot_art_survive_wire_draw_dispatch) {
    uint8_t data[512];
    sizeBuf_t msg = make_msg_buf(data, sizeof(data));
    uiFrame_t empty = {0}, frames[] = {
        { .number = 1, .flags.type = FT_TEXTURE, .stat = UI_STAT_CONTEXT_MANA,
          .tex.index = 1, .size = { .width = 0.04f, .height = 0.006f } },
        { .number = 2, .flags.type = FT_TEXTURE, .stat = UI_STAT_CONTEXT_HEALTH,
          .tex.index = 1, .size = { .width = 0.04f, .height = 0.006f } },
        { .number = 3, .flags.type = FT_SEGMENTED_STATUSBAR, .stat = ENT_CARGO,
          .tex = { .index = 1, .index2 = 2 }, .size = { .width = 0.04f, .height = 0.004f } },
    };
    test_client_stubs_init(); cl.hover_entity = 7;
    cl.pics[1] = (texture_t *)(uintptr_t)1; cl.pics[2] = (texture_t *)(uintptr_t)2;
    re.DrawImage = capture_status_image;
    MSG_WriteByte(&msg, LAYER_WORLD_HOVER);
    FOR_LOOP(i, sizeof(frames) / sizeof(frames[0])) {
        MSG_WriteDeltaUIFrame(&msg, &empty, &frames[i], true); MSG_WriteByte(&msg, 0);
    }
    MSG_WriteLong(&msg, 0); MSG_WriteShort(&msg, 0); msg.readcount = 0;
    CL_ParseLayout(&msg);
    FOR_LOOP(i, 6) {
        entityState_t *state = &cl.ents[7].current;
        if (i == 1) *state = MAKE(entityState_t, .model = 1, .flags = EF_HOVER_HEALTH,
                                 .stats = { [ENT_HEALTH] = 255 });
        if (i == 2) state->flags |= EF_HOVER_MANA;
        if (i == 3) state->stats[ENT_CARGO] = EntityCargoPack(0, 3);
        if (i == 4) { state->flags &= ~EF_HOVER_MANA; state->stats[ENT_CARGO] = 0; }
        if (i == 5) state->stats[ENT_HEALTH] = 0;
        SCR_ClearLayer(cl.layout[LAYER_WORLD_HOVER], LAYER_WORLD_HOVER); test_status_draws = 0;
        SCR_LayoutDrawOverlay(cl.layout[LAYER_WORLD_HOVER]);
        T_EQ(test_status_draws, i == 0 || i == 5 ? 0 : i == 3 ? 5 : i == 2 ? 2 : 1);
        if (i == 3) {
            T_EQ(SCR_Frame(3)->tex.index2, 2);
            for (uint32_t slot = 2; slot < 5; slot++) T_EQ(test_status_textures[slot], cl.pics[2]);
        }
    }
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

static uint32_t test_menu_draws, test_menu_keys;
static void capture_menu_key(int key, bool down, uint32_t time) { (void)key; (void)down; (void)time; test_menu_keys++; }
static void capture_menu_refresh(uint32_t time) { (void)time; test_menu_draws++; }

TEST(net, active_game_never_draws_main_menu) {
    test_client_stubs_init(); test_client_stubs_clear_cvars();
    re.BeginFrame = capture_begin_frame; re.EndFrame = capture_end_frame;
    menu.Refresh = capture_menu_refresh; menu.KeyEvent = capture_menu_key; test_menu_draws = test_menu_keys = 0;
    cls.state = ca_active; cls.key_dest = key_menu; scr_initialized = true;
    test_client_stubs_set_cvar("r_hud", "0");
    SCR_UpdateScreen(16);
    T_EQ(test_menu_draws, 0);
    Key_Event(K_F12, 0, true, 16); T_EQ(test_menu_keys, 0);
    cls.state = ca_connected; cl.playerstate.client_ui_state = CLIENT_UI_LOADING;
    T_ASSERT(!CL_MenuActive());
    Key_Event(K_F12, 0, true, 16); T_EQ(test_menu_keys, 0);
    cls.state = ca_disconnected; cl.playerstate.client_ui_state = CLIENT_UI_GAME;
    Key_Event(K_F12, 0, true, 16); T_EQ(test_menu_keys, 1);
    SCR_UpdateScreen(16);
    T_EQ(test_menu_draws, 1);
    scr_initialized = false;
}

TEST(net, paused_scene_time_reuses_cached_world_without_effect_delta) {
    viewDef_t view = { .time = 1000, .deltaTime = 16 };
    uint32_t last = 1000;

    T_ASSERT(!V_AdvanceSceneTime(&view, 1100, &last, true));
    T_EQ(view.time, 1000); T_EQ(view.deltaTime, 0); T_EQ(last, 1100);
    T_ASSERT(V_AdvanceSceneTime(&view, 1200, &last, false));
    T_EQ(view.time, 1100); T_EQ(view.deltaTime, 100); T_EQ(last, 1200);
}

TEST(net, scene_time_rewind_starts_a_new_render_epoch) {
    viewDef_t view = { .time = 22316, .deltaTime = 16 };
    uint32_t last = 22316;

    T_ASSERT(V_AdvanceSceneTime(&view, 400, &last, false));
    T_EQ(view.time, 400); T_EQ(view.deltaTime, 0); T_EQ(last, 400);
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
    cl.ents[7].current = (entityState_t){
        .model = 1, .name = 1, .flags = EF_HOVER_HEALTH | EF_HOSTILE,
        .stats = { [ENT_HEALTH] = 255 },
    };
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

    /* Invulnerable units keep a name with neither bar flag. */
    cl.ents[7].current.flags = EF_HOSTILE;
    SCR_UpdateScreen(16);
    T_EQ(test_cursor_draw_calls, 3);
    T_EQ(test_cursor_tint.r, 255);
    T_EQ(test_cursor_tint.g, 0);
    T_EQ(test_cursor_tint.b, 0);

    cl.hover_entity = 0;
    SCR_UpdateScreen(16);
    T_EQ(test_cursor_draw_calls, 4);
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
    static uint8_t   drain_buf[MAX_MSGLEN];
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
    rect_t screen = MAKE(rect_t, 0.1f, 0.2f, 0.02f, 0.4f);

    test_client_stubs_init(); test_scroll_draws = 0; re.DrawImage = capture_scroll_image;
    FOR_LOOP(i, 3) {
        scroll.image[i] = i + 1;
        cl.pics[i + 1] = (texture_t *)(uintptr_t)(i + 1);
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
    rect_t screen = MAKE(rect_t, 0.1f, 0.2f, 0.02f, 0.4f);

    test_client_stubs_init(); test_scroll_draws = 0; re.DrawImage = capture_scroll_image;
    SCR_LayoutDrawScrollBar(&frame, &screen);
    T_EQ((int)test_scroll_draws, 0);
}

/* Text areas are scroll viewports, so wrapped content must not escape their inset rectangle. */
TEST(net, layout_textarea_clips_to_inset_viewport) {
    uiTextArea_t area = { .font = 1, .inset = 0.01f };
    uiFrame_t frame = { .text = "wrapped text", .buffer = { &area, sizeof(area) } };
    rect_t screen = MAKE(rect_t, 0.1f, 0.2f, 0.3f, 0.4f);

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
    rect_t screen = MAKE(rect_t, 0.1f, 0.2f, 0.3f, 0.4f);

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
    uint8_t buf[64];
    sizeBuf_t sz;
    SZ_Init(&sz, buf, sizeof(buf));
    T_ASSERT(sz.data == buf);
    T_EQ(sz.maxsize, 64);
    T_EQ(sz.cursize, 0);
    T_EQ(sz.readcount, 0);
}

TEST(net, sz_clear) {
    uint8_t buf[32];
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
    uint8_t buf[32];
    sizeBuf_t sz;
    SZ_Init(&sz, buf, sizeof(buf));

    const char payload[] = "hello";
    SZ_Write(&sz, payload, 5);

    T_EQ(sz.cursize, 5);
    T_ASSERT(memcmp(buf, "hello", 5) == 0);
}

TEST(net, sz_write_multiple) {
    uint8_t buf[32];
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
    static uint8_t   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t from;

    drain_loopback(NS_SERVER);
    int r = NET_GetPacket(NS_SERVER, &from, &msg);
    T_EQ(r, 0);
}

TEST(net, loopback_round_trip) {
    static uint8_t   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t adr  = loopback_adr();
    netadr_t from;

    drain_loopback(NS_SERVER);

    const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };
    NET_SendPacket(NS_CLIENT, sizeof(payload), payload, adr);

    int r = NET_GetPacket(NS_SERVER, &from, &msg);

    T_EQ(r, (int)sizeof(payload));
    T_ASSERT(memcmp(msg.data, payload, sizeof(payload)) == 0);
    T_EQ(from.type, NA_LOOPBACK);
    T_EQ(NET_GetPacket(NS_SERVER, &from, &msg), 0);
}

TEST(net, loopback_multiple_packets_in_order) {
    static uint8_t   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t adr  = loopback_adr();
    netadr_t from;

    drain_loopback(NS_SERVER);

    const uint8_t pkt1[] = { 'A', 'B' };
    const uint8_t pkt2[] = { 'C', 'D', 'E' };
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
    static uint8_t payload[65536], msg_buf[MAX_MSGLEN];
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
    static uint8_t   msg_buf[MAX_MSGLEN];
    static sizeBuf_t msg = { msg_buf, MAX_MSGLEN, 0, 0 };
    netadr_t adr  = loopback_adr();
    netadr_t from;

    drain_loopback(NS_CLIENT);

    const uint8_t payload[] = { (uint8_t)0xDE, (uint8_t)0xAD };
    NET_SendPacket(NS_SERVER, sizeof(payload), payload, adr);

    int r = NET_GetPacket(NS_CLIENT, &from, &msg);
    T_EQ(r, (int)sizeof(payload));
    T_ASSERT(msg.data[0] == 0xDE && msg.data[1] == 0xAD);
}

TEST(net, loopback_na_ip_no_crash_without_socket) {
    static uint8_t   msg_buf[MAX_MSGLEN];
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

static sizeBuf_t make_msg_buf(uint8_t *buf, uint32_t bufsz) {
    sizeBuf_t sb;
    SZ_Init(&sb, buf, bufsz);
    return sb;
}

TEST(net, msg_writebyte_readbyte_roundtrip) {
    uint8_t buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, 0xAB);
    sb.readcount = 0;
    T_EQ(MSG_ReadByte(&sb), 0xAB);
}

TEST(net, msg_byte_ff_roundtrip_is_unsigned) {
    uint8_t buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, 0xFF);
    sb.readcount = 0;
    T_EQ(MSG_ReadByte(&sb), 255);
}

TEST(net, msg_writeshort_readshort_roundtrip) {
    uint8_t buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteShort(&sb, 0x1234);
    sb.readcount = 0;
    T_EQ(MSG_ReadShort(&sb) & 0xFFFF, 0x1234);
}

TEST(net, msg_writelong_readlong_roundtrip) {
    uint8_t buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteLong(&sb, (int)0xDEADBEEF);
    sb.readcount = 0;
    T_EQ((unsigned int)MSG_ReadLong(&sb), (unsigned int)0xDEADBEEF);
}

TEST(net, msg_writefloat_readfloat_roundtrip) {
    uint8_t buf[16];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteFloat(&sb, 3.14f);
    sb.readcount = 0;
    T_FEQ(MSG_ReadFloat(&sb), 3.14f, 0.0001f);
}

TEST(net, msg_writestring_readstring_roundtrip) {
    uint8_t buf[64];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteString(&sb, "hello");
    sb.readcount = 0;
    char out[32] = {0};
    MSG_ReadString(&sb, out);
    T_STREQ(out, "hello");
}

TEST(net, msg_readbyte_past_end_returns_zero) {
    uint8_t buf[8] = {0};
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    T_EQ(MSG_ReadByte(&sb), 0);
}

TEST(net, msg_writepos_readpos_roundtrip) {
    uint8_t buf[32];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    vec3_t out = {0};
    vec3_t in  = {128.0f, -64.0f, 32.0f};
    MSG_WritePos(&sb, &in);
    sb.readcount = 0;
    MSG_ReadPos(&sb, &out);
    T_EQ((int)out.x, (int)in.x);
    T_EQ((int)out.y, (int)in.y);
    T_EQ((int)out.z, (int)in.z);
}

TEST(net, msg_writedir_readdir_roundtrip) {
    uint8_t buf[32];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    vec3_t dir = {0.707f, 0.0f, -0.707f};
    vec3_t out = {0};
    MSG_WriteDir(&sb, &dir);
    sb.readcount = 0;
    MSG_ReadDir(&sb, &out);
    T_FEQ(out.x, dir.x, 0.001f);
    T_FEQ(out.y, dir.y, 0.001f);
    T_FEQ(out.z, dir.z, 0.001f);
}

TEST(net, msg_writeangle_readangle_roundtrip) {
    uint8_t buf[8];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    float angle = 1.5f;
    MSG_WriteAngle(&sb, angle);
    sb.readcount = 0;
    float out = MSG_ReadAngle(&sb);
    T_FEQ(out, angle, 0.025f);
}

TEST(net, msg_multiple_types_sequential) {
    uint8_t buf[64];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb,  42);
    MSG_WriteShort(&sb, 1000);
    MSG_WriteLong(&sb,  0x12345678);
    sb.readcount = 0;
    T_EQ(MSG_ReadByte(&sb)  & 0xFF,       42);
    T_EQ(MSG_ReadShort(&sb) & 0xFFFF, 1000);
    T_EQ((unsigned int)MSG_ReadLong(&sb), (unsigned int)0x12345678);
}


TEST(client_layout, listbox_draws_only_whole_rows_inside_viewport) {
    uiListBox_t list = { .border = 0.004f, .itemHeight = 0.020f, .selectedIndex = -1 };
    uiFrame_t frame = {
        .number = 1,
        .parent = 0,
        .flags = { .type = FT_LISTBOX },
        .color = COLOR32_WHITE,
        .size = { .width = 0.3620f, .height = 0.1250f },
        .text = "one\tone\ntwo\ttwo\nthree\tthree\nfour\tfour\nfive\tfive\nsix\tsix",
    };
    rect_t screen = { 0.3523f, 0.2040f, 0.3620f, 0.1250f };

    test_client_stubs_init();
    /* SCR_LayoutListBoxMaxScroll() measures the frame through the normal
     * layout runtime cache. Keep this synthetic frame inside the valid frame
     * range and reset that cache before drawing it. */
    SCR_Clear(NULL);
    frame.buffer.data = &list;
    frame.buffer.size = sizeof(list);
    re.DrawText = capture_listbox_text;
    test_listbox_draws = 0;

    SCR_LayoutDrawListBox(&frame, &screen);

    /* 0.125 - 2*0.004 = 0.117: five complete 0.020 rows fit.
     * A partial sixth row must not be rendered. */
    T_EQ(test_listbox_draws, 5);
    FOR_LOOP(i, test_listbox_draws) {
        drawText_t const *draw = &test_listbox_draw[i];
        T_ASSERT(draw->flags & DRAW_CLIP);
        T_FEQ(draw->rect.h, 0.020f, 0.0001f);
        T_FEQ(draw->clip.x, draw->rect.x, 0.0001f);
        T_FEQ(draw->clip.y, draw->rect.y, 0.0001f);
        T_FEQ(draw->clip.w, draw->rect.w, 0.0001f);
        T_FEQ(draw->clip.h, draw->rect.h, 0.0001f);
    }
}

TEST(net, ui_window_frame_delta_preserves_text_offsets) {
    uint8_t buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t from = {0}, to = { .number = 6, .flags = { .type = FT_SIMPLEFRAME } }, out = {0};
    uint32_t bits = 0;
    int number;

    to.text = (cstring_t)(uintptr_t)0x1234;
    MSG_WriteDeltaUIWindowFrame(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaUIWindowFrame(&sb, &out, number, bits);

    T_EQ(number, 6);
    T_EQ((uint32_t)(uintptr_t)out.text, 0x1234);
    T_EQ(out.flags.type, FT_SIMPLEFRAME);
}

static vec2_t text_length_mock_size(drawText_t const *text);

static uint32_t test_scoped_hud_text_draws;
static uint32_t test_scoped_edit_text_draws;

static void capture_layout_scoped_text(drawText_t const *text) {
    if (!text || !text->text) return;
    if (!strcmp(text->text, "HUD frame")) test_scoped_hud_text_draws++;
    if (!strcmp(text->text, "save-name")) test_scoped_edit_text_draws++;
}

static void test_send_edit_window(uint32_t id, uint32_t class_id, uint32_t flags) {
    uint8_t buf[2048], arena[128] = { 0 };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0};
    uiFrame_t edit_frame = { .number = 1, .flags = { .type = FT_EDITBOX } };
    uiFrame_t text_frame = { .number = 2, .parent = 1, .flags = { .type = FT_TEXT } };
    uiEditBox_t edit = { .textColor = COLOR32_WHITE, .cursorColor = COLOR32_WHITE, .maxChars = 63 };
    uiLabel_t label = {0};
    uint32_t text_offset = 1;

    strlcpy(edit.id, "SaveGameFileEditBox", sizeof(edit.id));
    snprintf((string_t)arena + text_offset, sizeof(arena) - text_offset, "%s", "save-name");
    edit_frame.size.width = 0.30f; edit_frame.size.height = 0.04f;
    text_frame.size.width = 0.20f; text_frame.size.height = 0.02f;
    text_frame.text = (cstring_t)(uintptr_t)text_offset;

    MSG_WriteByte(&sb, svc_window); MSG_WriteByte(&sb, UI_WINDOW_OPEN);
    MSG_WriteLong(&sb, id); MSG_WriteLong(&sb, class_id); MSG_WriteLong(&sb, flags);
    MSG_WriteDeltaUIWindowFrame(&sb, &empty, &edit_frame, true);
    MSG_WriteByte(&sb, sizeof(edit)); MSG_Write(&sb, &edit, sizeof(edit));
    MSG_WriteDeltaUIWindowFrame(&sb, &empty, &text_frame, true);
    MSG_WriteByte(&sb, sizeof(label)); MSG_Write(&sb, &label, sizeof(label));
    MSG_WriteLong(&sb, 0); MSG_WriteShort(&sb, 0);
    MSG_WriteLong(&sb, text_offset + strlen("save-name") + 1);
    MSG_Write(&sb, arena, text_offset + strlen("save-name") + 1);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
}

static void test_install_text_layout_frame(uint32_t layer, uint32_t number, cstring_t text) {
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0};
    uiFrame_t frame = { .number = number, .flags = { .type = FT_TEXT }, .color = COLOR32_WHITE, .text = text };
    uiLabel_t label = {0};

    frame.size.width = 0.20f; frame.size.height = 0.02f;
    MSG_WriteByte(&sb, layer);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, sizeof(label)); MSG_Write(&sb, &label, sizeof(label));
    MSG_WriteLong(&sb, 0); MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseLayout(&sb);
}

static void test_send_window(uint32_t id, uint32_t class_id, uint32_t flags, float x, cstring_t text, cstring_t command) {
    uint8_t buf[1024], arena[512] = { 0 };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = { .number = 1, .flags = { .type = FT_TEXT }, .hotkey = 'Z' };
    uiLabel_t label = {0};
    uint32_t text_offset = 1, command_offset = text_offset + strlen(text) + 1;

    snprintf((string_t)arena + text_offset, sizeof(arena) - text_offset, "%s", text);
    snprintf((string_t)arena + command_offset, sizeof(arena) - command_offset, "%s", command);
    frame.text = (cstring_t)(uintptr_t)text_offset; frame.onclick = (cstring_t)(uintptr_t)command_offset;
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
    uint8_t buf[2048], text[514];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = { .number = 1, .flags = { .type = FT_TEXT } };
    uiLabel_t label = {0};

    memset(text, 'W', sizeof(text)); text[0] = '\0'; text[sizeof(text) - 1] = '\0';
    frame.text = (cstring_t)(uintptr_t)1;
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
    uint8_t buf[64];
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

TEST(net, window_edit_text_does_not_leak_into_same_number_hud_frame) {
    test_client_stubs_init(); CL_WindowClear();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    re.GetTextSize = text_length_mock_size;
    re.DrawText = capture_layout_scoped_text;

    /* Frame indexes are local to each serialized layout. Deliberately make
     * persistent HUD frame 2 collide with the edit box's text child frame 2. */
    test_install_text_layout_frame(LAYER_INFOPANEL, 2, "HUD frame");
    test_send_edit_window(15, 105, UI_WINDOW_MODAL | UI_WINDOW_NO_PAUSE);
    test_scoped_hud_text_draws = 0;
    test_scoped_edit_text_draws = 0;

    SCR_DrawLayout();

    T_EQ(test_scoped_hud_text_draws, 1);
    T_EQ(test_scoped_edit_text_draws, 1);
    CL_WindowClear();
    SCR_ClearLayoutLayer(LAYER_INFOPANEL);
}

TEST(net, nonmodal_edit_yields_arrow_keys_to_gameplay) {
    test_client_stubs_init(); CL_WindowClear();
    test_send_edit_window(16, 106, 0);

    T_ASSERT(!CL_WindowModalActive());
    T_ASSERT(CL_WindowTextInputActive());
    T_ASSERT(!CL_WindowKeyEvent(K_LEFTARROW));
    T_ASSERT(!CL_WindowKeyEvent(K_RIGHTARROW));
    T_ASSERT(!CL_WindowKeyEvent(K_UPARROW));
    T_ASSERT(!CL_WindowKeyEvent(K_DOWNARROW));
    T_ASSERT(CL_WindowKeyEvent(8));

    CL_WindowClear();
}

TEST(net, modal_edit_keeps_arrow_keys_for_local_input) {
    test_client_stubs_init(); CL_WindowClear();
    test_send_edit_window(17, 107, UI_WINDOW_MODAL | UI_WINDOW_NO_PAUSE);

    T_ASSERT(CL_WindowModalActive());
    T_ASSERT(CL_WindowTextInputActive());
    T_ASSERT(CL_WindowKeyEvent(K_LEFTARROW));
    T_ASSERT(CL_WindowKeyEvent(K_RIGHTARROW));
    T_ASSERT(CL_WindowKeyEvent(K_UPARROW));
    T_ASSERT(CL_WindowKeyEvent(K_DOWNARROW));

    CL_WindowClear();
}

TEST(net, window_click_raises_and_moves_keyboard_focus) {
    test_client_stubs_init(); CL_WindowClear();
    re.GetTextSize = text_length_mock_size; re.DrawText = capture_textarea;
    test_send_window(1, 91, UI_WINDOW_UNIQUE, 0.05f, "First", "first");
    test_send_window(2, 92, UI_WINDOW_UNIQUE, 0.45f, "Second", "second");
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_DOWN, 128, 256, 1));
    test_forwarded_command[0] = '\0';
    T_ASSERT(CL_WindowKeyEvent('Z'));
    /* Window control substitution now forwards through the typed command boundary instead of writing netchan bytes. */
    T_STREQ(test_forwarded_command, "first");
    test_textarea_draws = 0; CL_WindowDraw();
    T_EQ(test_textarea_draws, 2);
    T_STREQ(test_textarea_draw.text, "First");
    CL_WindowClear();
}

TEST(net, nonmodal_window_keeps_gameplay_mouse_input_inside_its_bounds) {
    test_client_stubs_init(); CL_WindowClear();
    re.GetTextSize = text_length_mock_size; re.DrawText = capture_textarea;
    test_send_window(21, 111, UI_WINDOW_UNIQUE, 0.05f, "Welcome", UI_WINDOW_CLOSE_ACTION);
    T_ASSERT(CL_WindowMouseOver(128, 256));
    T_ASSERT(!CL_WindowMouseOver(900, 700));
    CL_WindowClear();
}

TEST(net, window_disconnect_action_defers_world_to_main_menu) {
    test_client_stubs_init(); CL_WindowClear(); re.GetTextSize = text_length_mock_size;
    test_send_window(14, 104, UI_WINDOW_MODAL | UI_WINDOW_NO_PAUSE, 0.05f,
                     "Quit", UI_WINDOW_DISCONNECT_ACTION);
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_DOWN, 128, 256, 1));
    T_ASSERT(CL_WindowMouseEvent(MENU_MOUSE_UP, 128, 256, 1));
    T_STREQ(test_menu_action, "menu");
    T_STREQ(test_menu_action_arg, "menu_main");
    CL_WindowClear();
}

TEST(net, window_close_action_closes_without_server_command) {
    uint8_t message_buf[256];

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
    uint8_t buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t from = {0}, to = { .number = 7, .textLength = 19 }, out = {0};
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaUIFrame(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaUIFrame(&sb, &out, number, bits);

    T_EQ(number, 7);
    T_EQ(out.textLength, 19);
}

TEST(net, ui_frame_delta_preserves_widescreen_extension_flag) {
    uint8_t buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t from = {0}, to = { .number = 8 }, out = {0};
    uint32_t bits = 0;
    int number;

    to.flags.type = FT_BACKDROP;
    to.flagsvalue |= UIFLAG_EXTEND_WIDESCREEN_X;
    MSG_WriteDeltaUIFrame(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaUIFrame(&sb, &out, number, bits);

    T_EQ(number, 8);
    T_EQ(out.flags.type, FT_BACKDROP);
    T_ASSERT(out.flagsvalue & UIFLAG_EXTEND_WIDESCREEN_X);
}

TEST(net, ui_frame_delta_preserves_timed_status_binding) {
    uint8_t buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t from = {0};
    uiFrame_t to = { .number = 8, .stat = UI_STAT_SELECTION_TIMED_STATUS };
    uiFrame_t out = {0};
    uint32_t bits = 0;
    int number;

    T_ASSERT(UI_STAT_SELECTION_TIMED_STATUS <= UINT8_MAX);
    MSG_WriteDeltaUIFrame(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaUIFrame(&sb, &out, number, bits);

    T_EQ(number, 8);
    T_EQ(out.stat, UI_STAT_SELECTION_TIMED_STATUS);
}

static vec2_t text_length_mock_size(drawText_t const *text) {
    if (text && text->text && !strcmp(text->text, " ")) {
        return MAKE(vec2_t, 0.006f, 0.012f);
    }
    return MAKE(vec2_t, 0.018f, 0.012f);
}

TEST(net, cinematic_fade_covers_widescreen_canvas) {
    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    test_client_stubs_set_window_size(1280, 720);
    test_fade_draws = 0;
    cl.playerstate.cinefade = 1.0f;
    re.DrawImage = capture_fade_image;

    SCR_DrawLayout();

    T_EQ(test_fade_draws, 1);
    T_FEQ(test_fade_rect.x, 0.0f, 0.0001f);
    T_FEQ(test_fade_rect.y, 0.0f, 0.0001f);
    T_FEQ(test_fade_rect.w, UI_BASE_HEIGHT * (1280.0f / 720.0f), 0.0001f);
    T_FEQ(test_fade_rect.h, UI_BASE_HEIGHT, 0.0001f);
    T_EQ(test_fade_color.a, 255);
}

TEST(net, layout_widescreen_extension_flag_reaches_full_canvas) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = {0};
    rect_t const *rect;

    frame.number = 1;
    frame.flags.type = FT_BACKDROP;
    frame.flagsvalue |= UIFLAG_EXTEND_WIDESCREEN_X;
    frame.size.width = UI_BASE_WIDTH;
    frame.size.height = 0.140f;
    frame.points.x[FPP_MAX].used = 1;
    frame.points.x[FPP_MAX].targetPos = FPP_MAX;
    frame.points.x[FPP_MAX].relativeTo = 0;
    frame.points.y[FPP_MAX].used = 1;
    frame.points.y[FPP_MAX].targetPos = FPP_MAX;
    frame.points.y[FPP_MAX].relativeTo = 0;

    test_client_stubs_init();
    test_client_stubs_set_canvas_policy(UI_CANVAS_EXPAND_CENTER);
    test_client_stubs_set_window_size(1280, 720);
    MSG_WriteByte(&sb, LAYER_CINEMATIC);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, 0);
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;

    CL_ParseLayout(&sb);
    SCR_Clear(cl.layout[LAYER_CINEMATIC]);
    rect = SCR_LayoutRect(SCR_Frame(1));
    T_NOT_NULL(rect);
    T_FEQ(rect->x, 0.0f, 0.0001f);
    T_FEQ(rect->w, UI_BASE_HEIGHT * (1280.0f / 720.0f), 0.0001f);
    T_FEQ(rect->y, UI_BASE_HEIGHT - 0.140f, 0.0001f);
    T_FEQ(rect->h, 0.140f, 0.0001f);
}

TEST(net, layout_text_length_uses_space_advance_for_implicit_width) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = {0};
    uiLabel_t label = {0};
    rect_t const *rect;

    frame.number = 1;
    frame.flags.type = FT_STRING;
    frame.text = "123";
    frame.textLength = 10;
    frame.size.height = 0.012f;
    frame.points.x[FPP_MIN].used = 1;
    frame.points.x[FPP_MIN].targetPos = FPP_MIN;
    frame.points.x[FPP_MIN].relativeTo = 0;
    frame.points.x[FPP_MIN].offset = (int16_t)(0.100f * UI_FRAMEPOINT_SCALE);
    frame.points.y[FPP_MIN].used = 1;
    frame.points.y[FPP_MIN].targetPos = FPP_MIN;
    frame.points.y[FPP_MIN].relativeTo = 0;
    frame.points.y[FPP_MIN].offset = (int16_t)(-0.100f * UI_FRAMEPOINT_SCALE);

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

TEST(net, layout_structural_frame_sizes_to_measured_text) {
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, frame = {0};
    uiNameTag_t fit = {0};
    rect_t const *rect;

    frame.number = 1;
    frame.flags.type = FT_SIMPLEFRAME;
    frame.flagsvalue |= UIFLAG_SIZE_TO_CONTENT;
    frame.text = "Timer title    00:30";
    frame.size.height = 0.030f;
    frame.points.x[FPP_MAX].used = 1;
    frame.points.x[FPP_MAX].targetPos = FPP_MAX;
    frame.points.x[FPP_MAX].relativeTo = 0;
    frame.points.x[FPP_MAX].offset = (int16_t)(-0.006f * UI_FRAMEPOINT_SCALE);
    frame.points.y[FPP_MIN].used = 1;
    frame.points.y[FPP_MIN].targetPos = FPP_MIN;
    frame.points.y[FPP_MIN].relativeTo = 0;
    fit.padding_x = 0.006f;
    fit.min_width = 0.020f;

    test_client_stubs_init();
    re.GetTextSize = text_length_mock_size;
    MSG_WriteByte(&sb, LAYER_CONSOLE);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, sizeof(fit));
    MSG_Write(&sb, &fit, sizeof(fit));
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;

    CL_ParseLayout(&sb);
    T_NOT_NULL(cl.layout[LAYER_CONSOLE]);
    SCR_Clear(cl.layout[LAYER_CONSOLE]);
    rect = SCR_LayoutRect(SCR_Frame(1));
    T_NOT_NULL(rect);
    T_FEQ(rect->w, 0.030f, 0.001f);
    T_FEQ(rect->x + rect->w, UI_BASE_WIDTH - 0.006f, 0.001f);
    T_FEQ(rect->h, 0.030f, 0.001f);
}

TEST(net, layout_authored_height_with_top_bottom_anchors_keeps_bottom_edge) {
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uiFrame_t empty = {0}, parent = {0}, child = {0};
    rect_t const *parent_rect, *child_rect;

    parent.number = 1;
    parent.flags.type = FT_SIMPLEFRAME;
    parent.size.width = 0.100f;
    parent.size.height = 0.030125f;
    parent.points.x[FPP_MIN].used = 1;
    parent.points.x[FPP_MIN].targetPos = FPP_MIN;
    parent.points.x[FPP_MIN].relativeTo = 0;
    parent.points.x[FPP_MIN].offset = (int16_t)(0.310f * UI_FRAMEPOINT_SCALE);
    parent.points.y[FPP_MIN].used = 1;
    parent.points.y[FPP_MIN].targetPos = FPP_MIN;
    parent.points.y[FPP_MIN].relativeTo = 0;
    parent.points.y[FPP_MIN].offset = (int16_t)(-0.51925f * UI_FRAMEPOINT_SCALE);

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
    uint8_t buf[256];
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
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uint8_t payload[192] = {0};
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
    uint8_t set_buf[256];
    uint8_t clear_buf[32];
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

TEST(net, set_selection_accepts_authoritative_multi_selection) {
    uint8_t buf[128];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();
    cl.selection.num_selected = 1;
    cl.selection.entity_nums[0] = 99;

    MSG_WriteByte(&sb, svc_set_selection);
    MSG_WriteByte(&sb, 3);
    MSG_WriteLong(&sb, 4);
    MSG_WriteLong(&sb, 7);
    MSG_WriteLong(&sb, 11);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);

    T_EQ(cl.selection.num_selected, 3);
    T_EQ(cl.selection.entity_nums[0], 4);
    T_EQ(cl.selection.entity_nums[1], 7);
    T_EQ(cl.selection.entity_nums[2], 11);
}

TEST(net, set_selection_empty_clears_client_cache) {
    uint8_t buf[64];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();
    cl.selection.num_selected = 2;
    cl.selection.entity_nums[0] = 4;
    cl.selection.entity_nums[1] = 7;

    MSG_WriteByte(&sb, svc_set_selection);
    MSG_WriteByte(&sb, 0);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);

    T_EQ(cl.selection.num_selected, 0);
}

TEST(net, legacy_unit_ui_consumes_payload_without_menu) {
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init();

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
    T_EQ(sb.readcount, sb.cursize);

}

static void reset_fow_client_state(void) {
    SAFE_DELETE(cl.fow.visible, MemFree);
    SAFE_DELETE(cl.fow.explored, MemFree);
    SAFE_DELETE(cl.fow.texture, MemFree);
    test_client_stubs_init();
}

TEST(net, terrain_mask_datagram_reconstructs_client_mask) {
    uint8_t buf[128];
    uint8_t payload[] = { 1, 1, 14, 1 }; /* RLE: 16 cells, cell 0 and cell 15 are Blight. */
    terrainMaskChunk_t chunk = {
        .width = 8, .height = 2, .first_row = 0, .row_count = 2, .payload_bytes = sizeof(payload),
        .min_x = -128.0f, .min_y = 64.0f, .cell_size = 32.0f,
    };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));

    SAFE_DELETE(cl.terrain_mask.cells, MemFree);
    MSG_WriteByte(&sb, svc_frame);
    MSG_WriteLong(&sb, 1); MSG_WriteLong(&sb, 100); MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, BZ_GAME_DATAGRAM_TERRAIN_MASK);
    MSG_Write(&sb, &chunk, sizeof(chunk)); MSG_Write(&sb, payload, sizeof(payload));
    CL_ParseServerMessage(&sb);

    T_EQ(cl.terrain_mask.width, 8); T_EQ(cl.terrain_mask.height, 2);
    T_FEQ(cl.terrain_mask.origin.x, -128.0f, 0.001f);
    T_FEQ(cl.terrain_mask.cell_size, 32.0f, 0.001f);
    T_ASSERT(cl.terrain_mask.cells[0]);
    T_ASSERT(cl.terrain_mask.cells[15]);
    T_ASSERT(!cl.terrain_mask.cells[1]);
    T_ASSERT(cl.terrain_mask.generation);
    SAFE_DELETE(cl.terrain_mask.cells, MemFree);
}

TEST(net, terrain_mask_two_chunks_preserve_both_ranges) {
    uint8_t buf[256];
    uint8_t payload0[] = { 1, 1, 7 }; /* RLE: row 0 cell 0 is Blight. */
    uint8_t payload1[] = { 0, 7, 1 }; /* RLE: row 1 cell 7 is Blight. */
    terrainMaskChunk_t chunk0 = {
        .width = 8, .height = 2, .first_row = 0, .row_count = 1, .payload_bytes = sizeof(payload0),
        .min_x = 0.0f, .min_y = 0.0f, .cell_size = 32.0f,
    };
    terrainMaskChunk_t chunk1 = {
        .width = 8, .height = 2, .first_row = 1, .row_count = 1, .payload_bytes = sizeof(payload1),
        .min_x = 0.0f, .min_y = 0.0f, .cell_size = 32.0f,
    };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uint32_t generation;

    SAFE_DELETE(cl.terrain_mask.cells, MemFree);
    memset(&cl.terrain_mask, 0, sizeof(cl.terrain_mask));
    MSG_WriteByte(&sb, svc_frame);
    MSG_WriteLong(&sb, 1); MSG_WriteLong(&sb, 100); MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, BZ_GAME_DATAGRAM_TERRAIN_MASK);
    MSG_Write(&sb, &chunk0, sizeof(chunk0)); MSG_Write(&sb, payload0, sizeof(payload0));
    CL_ParseServerMessage(&sb);
    generation = cl.terrain_mask.generation;
    T_ASSERT(generation);
    SZ_Clear(&sb); sb.readcount = 0;
    MSG_WriteByte(&sb, svc_frame);
    MSG_WriteLong(&sb, 2); MSG_WriteLong(&sb, 200); MSG_WriteLong(&sb, 1);
    MSG_WriteShort(&sb, BZ_GAME_DATAGRAM_TERRAIN_MASK);
    MSG_Write(&sb, &chunk1, sizeof(chunk1)); MSG_Write(&sb, payload1, sizeof(payload1));
    CL_ParseServerMessage(&sb);
    T_ASSERT(cl.terrain_mask.cells[0]);
    T_ASSERT(cl.terrain_mask.cells[15]);
    T_ASSERT(cl.terrain_mask.generation > generation);
    SAFE_DELETE(cl.terrain_mask.cells, MemFree);
    memset(&cl.terrain_mask, 0, sizeof(cl.terrain_mask));
}

TEST(net, terrain_mask_same_bits_do_not_bump_generation) {
    uint8_t buf[128];
    uint8_t payload[] = { 1, 1, 7 }; /* RLE: 8 cells, cell 0 is Blight. */
    terrainMaskChunk_t chunk = {
        .width = 8, .height = 1, .first_row = 0, .row_count = 1, .payload_bytes = sizeof(payload),
        .min_x = 0.0f, .min_y = 0.0f, .cell_size = 32.0f,
    };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uint32_t generation;

    SAFE_DELETE(cl.terrain_mask.cells, MemFree);
    memset(&cl.terrain_mask, 0, sizeof(cl.terrain_mask));
    MSG_WriteByte(&sb, svc_frame);
    MSG_WriteLong(&sb, 1); MSG_WriteLong(&sb, 100); MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, BZ_GAME_DATAGRAM_TERRAIN_MASK);
    MSG_Write(&sb, &chunk, sizeof(chunk)); MSG_Write(&sb, payload, sizeof(payload));
    CL_ParseServerMessage(&sb);
    T_ASSERT(cl.terrain_mask.cells[0]);
    generation = cl.terrain_mask.generation;
    SZ_Clear(&sb); sb.readcount = 0;
    MSG_WriteByte(&sb, svc_frame);
    MSG_WriteLong(&sb, 2); MSG_WriteLong(&sb, 200); MSG_WriteLong(&sb, 1);
    MSG_WriteShort(&sb, BZ_GAME_DATAGRAM_TERRAIN_MASK);
    MSG_Write(&sb, &chunk, sizeof(chunk)); MSG_Write(&sb, payload, sizeof(payload));
    CL_ParseServerMessage(&sb);
    T_EQ(cl.terrain_mask.generation, generation);
    SAFE_DELETE(cl.terrain_mask.cells, MemFree);
    memset(&cl.terrain_mask, 0, sizeof(cl.terrain_mask));
}

TEST(net, terrain_mask_corner_and_tile_mask) {
    uint8_t cells[16] = { 0 };
    uint8_t corners[9] = { 0 };
    cells[0] = 1;
    T_EQ(TerrainMask_CornerValue(cells, 4, 4, 1, 0, 0), 1);
    T_EQ(TerrainMask_CornerValue(cells, 4, 4, 1, 3, 3), 0);
    T_EQ(TerrainMask_CornerValue(cells, 4, 4, 1, 99, 99), 0);
    corners[0] = 1;
    T_EQ(TerrainMask_TileMask(corners, 3, 0, 0), 2u);
    corners[0] = corners[1] = corners[3] = corners[4] = 1;
    T_EQ(TerrainMask_TileMask(corners, 3, 0, 0), 15u);
}

TEST(net, terrain_mask_cell_lookup_agrees_at_edges) {
    vec2_t origin = { 0.0f, 0.0f };
    uint32_t x = 99, y = 99;
    T_ASSERT(TerrainMask_CellForPoint(origin, 32.0f, 8, 8, &(vec2_t){ 0.0f, 0.0f }, &x, &y));
    T_EQ(x, 0); T_EQ(y, 0);
    T_ASSERT(TerrainMask_CellForPoint(origin, 32.0f, 8, 8, &(vec2_t){ 255.9f, 255.9f }, &x, &y));
    T_EQ(x, 7); T_EQ(y, 7);
    T_ASSERT(!TerrainMask_CellForPoint(origin, 32.0f, 8, 8, &(vec2_t){ 256.0f, 0.0f }, &x, &y));
    T_ASSERT(!TerrainMask_CellForPoint(origin, 32.0f, 8, 8, &(vec2_t){ -0.1f, 0.0f }, &x, &y));
    T_ASSERT(!TerrainMask_CellForPoint(origin, 32.0f, 8, 8, &(vec2_t){ 0.0f, 256.0f }, &x, &y));
}

static void write_fow_message(sizeBuf_t *sb,
                              uint32_t flags,
                              uint32_t width,
                              uint32_t height,
                              uint32_t first_row,
                              uint32_t row_count,
                              uint8_t const *payload,
                              uint32_t payload_bytes)
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
    uint8_t buf[32];
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
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uint32_t const model = 7;
    cstring_t const path = "Units\\Human\\Footman\\Footman.mdx";

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
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    uint32_t const model = 7;
    cstring_t const path = "Units\\Human\\Footman\\Footman.mdx";
    cstring_t const portrait = "Units\\Human\\Footman\\Footman_Portrait.mdx";

    test_client_stubs_init();
    test_model_loads = test_model_releases = 0;
    memset(test_model_load_paths, 0, sizeof(test_model_load_paths));
    re.LoadModel = capture_load_model;
    re.ReleaseModel = capture_release_model;
    cl.refresh_prepped = true;
    cl.models[model] = (model_t *)(uintptr_t)0x2001u;
    cl.portraits[model] = (model_t *)(uintptr_t)0x2002u;
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
    uint8_t buf[512];
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
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    model_t *first;

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
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    texture_t const *first;

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
    uint32_t const phase_stat = PLAYERSTATE_LUMBER_GATHERED + 1;
    uiFrame_t frame = { .flags = { .type = FT_SPRITE }, .tex = { .index = 1 }, .stat = phase_stat, .text = "#0" };
    rect_t screen = MAKE(rect_t, 0.0f, 0.6f, 0.0f, 0.0f);
    float ratio = -1.0f;

    test_client_stubs_init();
    cl.models[1] = (model_t *)(uintptr_t)1;
    cl.playerstate.stats[phase_stat] = 32768;
    test_sprite_anim[0] = '\0'; test_sprite_draws = 0; re.DrawSprite = capture_sprite;

    SCR_LayoutDrawSprite(&frame, &screen);
    T_EQ(test_sprite_draws, 1);
    T_EQ(sscanf(test_sprite_anim, "#0@%f", &ratio), 1);
    T_FEQ(ratio, 32768.0f / (float)UINT16_MAX, 0.00001f);
}

/* An image with the same numeric index must not capture a model-backed loading sprite. */
TEST(client_layout, loading_sprite_uses_client_progress_and_model_namespace) {
    uiFrame_t frame = { .flags = { .type = FT_SPRITE }, .tex = { .index = 1 },
                        .stat = UI_STAT_LOADING_PROGRESS, .text = "#0" };
    rect_t screen = MAKE(rect_t, 0.0f, 0.6f, 0.0f, 0.0f);
    float ratio;

    test_client_stubs_init();
    cl.models[1] = (model_t *)(uintptr_t)1;
    cl.pics[1] = (texture_t *)(uintptr_t)2;
    re.DrawSprite = capture_sprite;
    FOR_LOOP(i, 3) {
        cl.loading_progress = i * 0.5f;
        test_sprite_draws = 0;
        SCR_LayoutDrawSprite(&frame, &screen);
        T_EQ(test_sprite_draws, 1);
        T_EQ(sscanf(test_sprite_anim, "#0@%f", &ratio), 1);
        T_FEQ(ratio, cl.loading_progress, 0.00001f);
    }
    frame.stat = 0; frame.text = "#!6";
    SCR_LayoutDrawSprite(&frame, &screen);
    T_STREQ(test_sprite_anim, "#!6");
}

/* Image loading bars retain their explicit texture contract even when a model shares the index. */
TEST(client_layout, loading_image_uses_texture_namespace) {
    uiFrame_t frame = { .flags = { .type = FT_LOADING_BAR }, .tex = { .index = 1 } };
    rect_t screen = MAKE(rect_t, 0, 0, 0.4f, 0.1f);

    test_client_stubs_init();
    cl.models[1] = (model_t *)(uintptr_t)1;
    cl.pics[1] = (texture_t *)(uintptr_t)2;
    cl.loading_progress = 0.25f;
    test_scroll_draws = 0; re.DrawImage = capture_scroll_image;
    SCR_LayoutDrawLoadingBar(&frame, &screen);
    T_EQ(test_scroll_draws, 1); T_ASSERT(test_scroll_tex[0] == cl.pics[1]);
    T_FEQ(test_scroll_rects[0].w, 0.1f, 0.00001f);
    T_FEQ(test_scroll_uvs[0].w, 0.25f, 0.00001f);
}

TEST(client_layout, sprite_sequence_can_be_selected_by_second_stat) {
    uiFrame_t frame = {
        .flags = { .type = FT_SPRITE },
        .tex = { .index = 1 },
        .stat = UI_PLAYERSTAT_ENV_PHASE,
        .text = "#0",
        .value = (float)UI_PLAYERSTAT_ENV_VARIANT,
    };
    rect_t screen = MAKE(rect_t, 0.0f, 0.6f, 0.0f, 0.0f);
    float ratio = -1.0f;

    frame.flagsvalue |= UIFLAG_SPRITE_STAT_SEQUENCE;
    test_client_stubs_init();
    cl.models[1] = (model_t *)(uintptr_t)1;
    cl.playerstate.stats[UI_PLAYERSTAT_ENV_PHASE] = 32768;
    cl.playerstate.stats[UI_PLAYERSTAT_ENV_VARIANT] = 1;
    test_sprite_anim[0] = '\0'; test_sprite_draws = 0; re.DrawSprite = capture_sprite;

    SCR_LayoutDrawSprite(&frame, &screen);
    T_EQ(test_sprite_draws, 1);
    T_EQ(sscanf(test_sprite_anim, "#1@%f", &ratio), 1);
    T_FEQ(ratio, 32768.0f / (float)UINT16_MAX, 0.00001f);
}

TEST(net, environment_variant_stat_roundtrips) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 }, to = { 0 }, out = { 0 };
    uint32_t bits;
    int number;

    to.number = 3;
    to.stats[UI_PLAYERSTAT_ENV_VARIANT] = 1;
    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    sb.readcount = 0;
    number = MSG_ReadPlayerBits(&sb, &bits);
    MSG_ReadDeltaPlayerState(&sb, &out, number, bits);

    T_EQ(number, 3);
    T_EQ(out.stats[UI_PLAYERSTAT_ENV_VARIANT], 1);
}

TEST(net, game_presentation_variant_stat_roundtrips) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 }, to = { 0 }, out = { 0 };
    uint32_t bits;
    int number;

    to.number = 3;
    to.stats[UI_PLAYERSTAT_GAME_VARIANT] = 2;
    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    sb.readcount = 0;
    number = MSG_ReadPlayerBits(&sb, &bits);
    MSG_ReadDeltaPlayerState(&sb, &out, number, bits);

    T_EQ(number, 3);
    T_EQ(out.stats[UI_PLAYERSTAT_GAME_VARIANT], 2);
}

TEST(net, playerstat_pair_after_gameplay_states_roundtrips) {
    uint32_t const stat = PLAYERSTATE_LUMBER_GATHERED + 1;
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 }, to = { 0 }, out = { 0 };
    uint32_t bits;
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
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 };
    player_t to = { 0 };

    test_client_stubs_init();
    cls.key_dest = key_menu;
    cls.netchan.remote_address.type = NA_IP;
    to.number = 1;
    to.vieworigin = (vec3_t){ 128.0f, 256.0f, 0 };
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
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 };
    player_t to = { 0 };
    player_t out = { 0 };
    uiFrame_t health = { .stat = UI_STAT_SELECTION_HEALTH_TEXT };
    uiFrame_t mana = { .stat = UI_STAT_SELECTION_MANA_TEXT };
    uint32_t bits;
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
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = {0}, to = { .number = 1, .client_ui_state = CLIENT_UI_CINEMATIC, .fov = 35, .distance = 900, .znear = 55.0f, .zfar = 6500.0f };

    test_client_stubs_init();
    to.viewangles = (vec3_t){300, 0, 120};
    to.uiflags = ~(1u << LAYER_CINEMATIC);
    MSG_WriteByte(&sb, svc_playerinfo); MSG_WriteDeltaPlayerState(&sb, &from, &to);
    CL_ParseServerMessage(&sb);
    T_EQ(cl.playerstate.client_ui_state, CLIENT_UI_CINEMATIC);
    from = to;
    to.client_ui_state = CLIENT_UI_GAME; to.uiflags = 1u << LAYER_CINEMATIC;
    to.viewangles = (vec3_t){326, 0, 0}; to.vieworigin = (vec3_t){128, 256, 0}; to.fov = 50; to.distance = 1650;
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
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 };
    player_t to = { 0 };
    player_t out = { 0 };
    uint32_t bits;
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
    vec2_t clamped;

    test_client_stubs_init();
    test_client_stubs_set_world_bounds((box2_t){
        .min = { -4096.0f, -3072.0f },
        .max = { 4096.0f, 3072.0f },
    });
    clamped = CL_ClampCameraPosition((vec2_t){ 5000.0f, -4000.0f });
    T_FEQ(clamped.x, 4096.0f, 0.001f);
    T_FEQ(clamped.y, -3072.0f, 0.001f);
}

TEST(net, playerstate_camera_render_fields_roundtrip) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 };
    player_t to = { 0 };
    player_t out = { 0 };
    uint32_t bits;
    int number;

    to.number = 4;
    to.vieworigin.z = 275.0f;
    to.viewangles = (vec3_t){ 12.5f, 45.0f, 90.0f };
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
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 };
    player_t to = { 0 };

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

static bool test_camera_terrain(void) { return true; }
static float test_camera_height(float x, float y) { (void)y; return x; }

/* Repeated prediction and pending/acknowledged packets must retain each sample's authored height offset. */
TEST(net, camera_prediction_preserves_terrain_offsets) {
    uint8_t buf[256];
    player_t from = { 0 }, to = { .number = 1, .client_ui_state = CLIENT_UI_GAME, .vieworigin = { 50, 0, 90 } };
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    test_client_stubs_init();
    re.CameraUsesTerrainHeight = test_camera_terrain; re.GetHeightAtPoint = test_camera_height;
    test_client_stubs_set_world_bounds((box2_t){ .min = { -1000, -1000 }, .max = { 1000, 1000 } });
    cl.viewDef.camerastate[0].origin = (vec3_t){ 0, 0, 20 };
    cl.viewDef.camerastate[1].origin = (vec3_t){ 10, 0, 40 };
    CL_PredictCameraPosition((vec2_t){ 100, 0 });
    T_FEQ(cl.viewDef.camerastate[0].origin.z, 120, 0.001f);
    T_FEQ(cl.viewDef.camerastate[1].origin.z, 130, 0.001f);
    CL_PredictCameraPosition((vec2_t){ 200, 0 });
    T_FEQ(cl.viewDef.camerastate[0].origin.z, 220, 0.001f);
    T_FEQ(cl.viewDef.camerastate[1].origin.z, 230, 0.001f);
    cl.camera_prediction.active = true;
    cl.camera_prediction.origin = (vec2_t){ 200, 0 };
    MSG_WriteByte(&sb, svc_playerinfo); MSG_WriteDeltaPlayerState(&sb, &from, &to);
    CL_ParseServerMessage(&sb);
    T_ASSERT(cl.camera_prediction.active);
    T_FEQ(cl.playerstate.vieworigin.x, 50, 0.001f); T_FEQ(cl.playerstate.vieworigin.z, 90, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].origin.x, 200, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].origin.z, 240, 0.001f);
    T_FEQ(cl.viewDef.camerastate[1].origin.z, 220, 0.001f);
    from = to; to.vieworigin = (vec3_t){ 200, 0, 240 };
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_playerinfo); MSG_WriteDeltaPlayerState(&sb, &from, &to);
    CL_ParseServerMessage(&sb);
    T_ASSERT(!cl.camera_prediction.active);
    FOR_LOOP(i, 2) T_FEQ(cl.viewDef.camerastate[i].origin.z, 240, 0.001f);
    test_client_stubs_init();
    cl.viewDef.camerastate[0].origin.z = 25;
    CL_PredictCameraPosition((vec2_t){ 200, 0 });
    T_FEQ(cl.viewDef.camerastate[0].origin.x, 200, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].origin.z, 25, 0.001f);
}

TEST(net, camera_prediction_reconciles_to_server_clamped_bound) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    player_t from = { 0 };
    player_t to = { 0 };

    test_client_stubs_init();
    to.number = 1;
    to.vieworigin = (vec3_t){ 100.0f, -50.0f, 0 };
    test_client_stubs_set_world_bounds((box2_t){
        .min = { -100.0f, -50.0f },
        .max = { 100.0f, 50.0f },
    });
    to.fov = 50;
    to.distance = 1650;
    to.znear = 100.0f;
    to.zfar = 5000.0f;
    to.client_ui_state = CLIENT_UI_GAME;
    cl.camera_prediction.active = true;
    cl.camera_prediction.origin = (vec2_t){ 500.0f, -500.0f };

    MSG_WriteByte(&sb, svc_playerinfo);
    MSG_WriteDeltaPlayerState(&sb, &from, &to);
    CL_ParseServerMessage(&sb);

    T_ASSERT(!cl.camera_prediction.active);
    T_FEQ(cl.viewDef.camerastate[0].origin.x, 100.0f, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].origin.y, -50.0f, 0.001f);
}

TEST(net, fow_full_message_unpacks_visible_and_explored_planes) {
    uint8_t buf[64];
    uint8_t payload[] = {
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
    /* Fog data is now published through viewDef_t; parsing alone does not upload GL state. */
    T_EQ(test_fow_upload_calls, 0);
    reset_fow_client_state();
}

TEST(net, fow_row_delta_reconstructs_client_grid) {
    uint8_t buf[64];
    uint8_t full_payload[] = { 0, 16 };
    uint8_t delta_payload[] = { 0, 4, 1, 3 };
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
    T_EQ(test_fow_upload_calls, 0);

    SZ_Clear(&sb);
    sb.readcount = 0;
    write_fow_message(&sb, FOW_MSG_VISIBLE_PLANE | FOW_MSG_RLE, 8, 2, 1, 1, delta_payload, sizeof(delta_payload));
    CL_ParseServerMessage(&sb);
    /* A later server message is a new publication boundary. */
    T_EQ(test_fow_upload_calls, 0);
    reset_fow_client_state();
}

TEST(net, fow_rle_255_continues_current_value) {
    uint8_t buf[64];
    uint8_t payload[] = { 1, 255, 16, 8 };
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
    uint8_t buf[64];
    uint8_t payload[] = { 1, 255, 0, 8 };
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
    uint8_t buf[64];
    uint8_t payload[] = { 1, 1 };
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

typedef struct { uint8_t const *bits; uint32_t count; } rleTestSrc_t;
static uint8_t rle_test_read(uint32_t index, void *ctx) { rleTestSrc_t *c = ctx; return index < c->count ? c->bits[index] : 0; }
typedef struct { uint8_t *out; uint32_t count; } rleTestDst_t;
static void rle_test_write(uint32_t index, uint8_t value, uint32_t count, void *ctx) {
    rleTestDst_t *c = ctx;
    FOR_LOOP(i, count) if (index + i < c->count) c->out[index + i] = value;
}

TEST(net, rle_roundtrip_sparse_and_dense) {
    uint8_t sparse[] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    uint8_t dense[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
    uint8_t out[16], back[16];
    rleTestSrc_t src = { sparse, sizeof(sparse) };
    rleTestDst_t dst = { back, sizeof(back) };
    uint32_t n = MSG_EncodeRLE(out, sizeof(out), sizeof(sparse), rle_test_read, &src);
    T_EQ(n, 4); T_EQ(out[0], 1); T_EQ(out[1], 1); T_EQ(out[2], 14); T_EQ(out[3], 1);
    T_ASSERT(MSG_ValidateRLE(out, n, sizeof(sparse)));
    memset(back, 0xFF, sizeof(back));
    T_EQ(MSG_DecodeRLE(out, n, sizeof(sparse), rle_test_write, &dst), sizeof(sparse));
    T_ASSERT(!memcmp(back, sparse, sizeof(sparse)));
    src.bits = dense; src.count = sizeof(dense); dst.count = sizeof(dense);
    n = MSG_EncodeRLE(out, sizeof(out), sizeof(dense), rle_test_read, &src);
    T_EQ(n, 2); T_EQ(out[0], 1); T_EQ(out[1], 8);
    T_ASSERT(MSG_ValidateRLE(out, n, sizeof(dense)));
    memset(back, 0, sizeof(back));
    T_EQ(MSG_DecodeRLE(out, n, sizeof(dense), rle_test_write, &dst), sizeof(dense));
    T_ASSERT(!memcmp(back, dense, sizeof(dense)));
}

TEST(net, rle_255_run_boundary_roundtrips) {
    static uint8_t bits[305];
    uint8_t out[8], back[sizeof(bits)];
    rleTestSrc_t src = { bits, sizeof(bits) };
    rleTestDst_t dst = { back, sizeof(back) };
    uint32_t n;
    memset(bits, 1, 300); memset(bits + 300, 0, 5);
    memset(out, 0, sizeof(out)); memset(back, 0xFF, sizeof(back));
    n = MSG_EncodeRLE(out, sizeof(out), sizeof(bits), rle_test_read, &src);
    T_EQ(n, 4); T_EQ(out[0], 1); T_EQ(out[1], 255); T_EQ(out[2], 45); T_EQ(out[3], 5);
    T_ASSERT(MSG_ValidateRLE(out, n, sizeof(bits)));
    T_EQ(MSG_DecodeRLE(out, n, sizeof(bits), rle_test_write, &dst), sizeof(bits));
    T_ASSERT(!memcmp(back, bits, sizeof(bits)));
}

TEST(net, rle_rejects_truncated_overlong_and_overflow) {
    uint8_t truncated[] = { 1, 1 }, overlong[] = { 1, 16, 1 }, bad_init[] = { 3, 5 };
    uint8_t alt[] = { 0, 1, 0, 1, 0, 1, 0, 1 }, tiny[2], back[16];
    rleTestSrc_t src = { alt, sizeof(alt) };
    rleTestDst_t dst = { back, sizeof(back) };
    T_ASSERT(!MSG_ValidateRLE(truncated, sizeof(truncated), 16));
    T_ASSERT(!MSG_ValidateRLE(overlong, sizeof(overlong), 16));
    T_ASSERT(!MSG_ValidateRLE(bad_init, sizeof(bad_init), 5));
    T_EQ(MSG_DecodeRLE(truncated, sizeof(truncated), 16, rle_test_write, &dst), 0);
    T_EQ(MSG_EncodeRLE(tiny, sizeof(tiny), sizeof(alt), rle_test_read, &src), 0); // 9-byte worst case
    T_EQ(MSG_EncodeRLE(NULL, 0, 0, NULL, NULL), 0);
}

/* Uniform runs pin the 255-continuation wiring: 255 stays one byte, 256 splits, 510 fills two, 511 spills. */
TEST(net, rle_uniform_run_lengths) {
    static uint8_t bits[511], out[8], back[sizeof(bits)];
    static uint32_t const counts[] = { 1, 254, 255, 256, 510, 511 };
    static uint8_t const wires[][4] = { { 1, 1 }, { 1, 254 }, { 1, 255 }, { 1, 255, 1 }, { 1, 255, 255 }, { 1, 255, 255, 1 } };
    static uint32_t const sizes[] = { 2, 2, 2, 3, 3, 4 };
    memset(bits, 1, sizeof(bits));
    FOR_LOOP(t, sizeof(counts) / sizeof(counts[0])) {
        rleTestSrc_t src = { bits, counts[t] };
        rleTestDst_t dst = { back, sizeof(back) };
        uint32_t n = MSG_EncodeRLE(out, sizeof(out), counts[t], rle_test_read, &src);
        T_EQ(n, sizes[t]);
        T_ASSERT(!memcmp(out, wires[t], sizes[t]));
        T_ASSERT(MSG_ValidateRLE(out, n, counts[t]));
        memset(back, 0xFF, sizeof(back)); dst.count = counts[t];
        T_EQ(MSG_DecodeRLE(out, n, counts[t], rle_test_write, &dst), counts[t]);
        T_ASSERT(!memcmp(back, bits, counts[t]));
    }
}

/* A toggle landing exactly on the 255 boundary needs the explicit 0 run; at 256 it must not appear. */
TEST(net, rle_toggle_at_255_boundary) {
    static uint8_t bits[261], out[8], back[sizeof(bits)];
    rleTestSrc_t src = { bits, 0 };
    rleTestDst_t dst = { back, sizeof(back) };
    uint32_t n;
    memset(bits, 1, 255); memset(bits + 255, 0, 5);
    src.count = 260; dst.count = 260;
    memset(out, 0, sizeof(out)); memset(back, 0xFF, sizeof(back));
    n = MSG_EncodeRLE(out, sizeof(out), 260, rle_test_read, &src);
    T_EQ(n, 4); T_EQ(out[0], 1); T_EQ(out[1], 255); T_EQ(out[2], 0); T_EQ(out[3], 5);
    T_ASSERT(MSG_ValidateRLE(out, n, 260));
    T_EQ(MSG_DecodeRLE(out, n, 260, rle_test_write, &dst), 260);
    T_ASSERT(!memcmp(back, bits, 260));
    memset(bits, 1, 256); memset(bits + 256, 0, 5);
    src.count = 261; dst.count = 261;
    memset(out, 0, sizeof(out)); memset(back, 0xFF, sizeof(back));
    n = MSG_EncodeRLE(out, sizeof(out), 261, rle_test_read, &src);
    T_EQ(n, 4); T_EQ(out[0], 1); T_EQ(out[1], 255); T_EQ(out[2], 1); T_EQ(out[3], 5);
    T_ASSERT(MSG_ValidateRLE(out, n, 261));
    T_EQ(MSG_DecodeRLE(out, n, 261, rle_test_write, &dst), 261);
    T_ASSERT(!memcmp(back, bits, 261));
}

/* Capacity exactly n succeeds, n-1 fails without a partial write the caller could mistake for data. */
TEST(net, rle_capacity_exact_and_short) {
    uint8_t sparse[] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 }; // encodes to 4 bytes
    uint8_t out[4];
    rleTestSrc_t src = { sparse, sizeof(sparse) };
    T_EQ(MSG_EncodeRLE(out, sizeof(out), sizeof(sparse), rle_test_read, &src), 4);
    T_EQ(MSG_EncodeRLE(out, sizeof(out) - 1, sizeof(sparse), rle_test_read, &src), 0);
    T_EQ(MSG_EncodeBitpack(out, 3, 16, rle_test_read, &src), 3); // 1 + 16/8 escape bytes
    T_EQ(MSG_EncodeBitpack(out, 2, 16, rle_test_read, &src), 0);
}

TEST(net, rle_validate_rejects_malformed) {
    uint8_t trailing[] = { 1, 8, 0 }, short_stream[] = { 1, 4 }, over[] = { 1, 9 }, bad_init[] = { 3, 5 };
    uint8_t short_pack[] = { 2, 0x01 };
    T_ASSERT(!MSG_ValidateRLE(trailing, sizeof(trailing), 8));
    T_ASSERT(!MSG_ValidateRLE(short_stream, sizeof(short_stream), 8));
    T_ASSERT(!MSG_ValidateRLE(over, sizeof(over), 8));
    T_ASSERT(!MSG_ValidateRLE(bad_init, sizeof(bad_init), 5));
    T_ASSERT(!MSG_ValidateRLE(short_pack, sizeof(short_pack), 16)); // bitpack of 16 bits needs 3 bytes
    T_ASSERT(!MSG_ValidateRLE(NULL, 0, 8));
}

/* The bitpack escape (init 2) carries whatever RLE cannot compress, at 1 + (bits+7)/8 bytes. */
TEST(net, bitpack_escape_roundtrips) {
    uint8_t sparse[] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 };
    uint8_t out[4], back[sizeof(sparse)];
    rleTestSrc_t src = { sparse, sizeof(sparse) };
    rleTestDst_t dst = { back, sizeof(back) };
    uint32_t n = MSG_EncodeBitpack(out, sizeof(out), sizeof(sparse), rle_test_read, &src);
    T_EQ(n, 3); T_EQ(out[0], 2); T_EQ(out[1], 0x01); T_EQ(out[2], 0x80);
    T_ASSERT(MSG_ValidateRLE(out, n, sizeof(sparse)));
    memset(back, 0xFF, sizeof(back));
    T_EQ(MSG_DecodeRLE(out, n, sizeof(sparse), rle_test_write, &dst), sizeof(sparse));
    T_ASSERT(!memcmp(back, sparse, sizeof(sparse)));
}

/* Alternating bits are the RLE worst case (~1 byte per bit); the escape bounds the same row at bitpack density. */
TEST(net, rle_checkerboard_falls_back_to_bitpack) {
    static uint8_t alt[64], out[72], back[sizeof(alt)];
    rleTestSrc_t src = { alt, sizeof(alt) };
    rleTestDst_t dst = { back, sizeof(back) };
    uint32_t n;
    FOR_LOOP(i, sizeof(alt)) alt[i] = (uint8_t)(i & 1);
    n = MSG_EncodeRLE(out, sizeof(out), sizeof(alt), rle_test_read, &src);
    T_EQ(n, sizeof(alt) + 1);
    T_ASSERT(MSG_ValidateRLE(out, n, sizeof(alt)));
    memset(back, 0, sizeof(back));
    T_EQ(MSG_DecodeRLE(out, n, sizeof(alt), rle_test_write, &dst), sizeof(alt));
    T_ASSERT(!memcmp(back, alt, sizeof(alt)));
    T_EQ(MSG_EncodeBitpack(out, sizeof(out), sizeof(alt), rle_test_read, &src), 1 + sizeof(alt) / 8);
}

TEST(net, rle_random_roundtrip) {
    static uint8_t bits[600], out[700], back[sizeof(bits)];
    rleTestSrc_t src = { bits, sizeof(bits) };
    rleTestDst_t dst = { back, sizeof(back) };
    uint32_t seed = 0x12345678u, n, i = 0;
    while (i < sizeof(bits)) { /* coherent runs with alternating patches, crossing 255 both ways */
        uint8_t v;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        v = (uint8_t)(seed & 1);
        if (seed & 0x80000000u) {
            uint32_t k = 1 + seed % 40;
            while (k-- && i < sizeof(bits)) { bits[i++] = v; v ^= 1; }
        } else {
            uint32_t run = 1 + seed % 300;
            while (run-- && i < sizeof(bits)) bits[i++] = v;
        }
    }
    n = MSG_EncodeRLE(out, sizeof(out), sizeof(bits), rle_test_read, &src);
    T_ASSERT(n > 0);
    T_ASSERT(MSG_ValidateRLE(out, n, sizeof(bits)));
    memset(back, 0xFF, sizeof(back));
    T_EQ(MSG_DecodeRLE(out, n, sizeof(bits), rle_test_write, &dst), sizeof(bits));
    T_ASSERT(!memcmp(back, bits, sizeof(bits)));
    n = MSG_EncodeBitpack(out, sizeof(out), sizeof(bits), rle_test_read, &src);
    T_EQ(n, 1 + (sizeof(bits) + 7) / 8);
    T_ASSERT(MSG_ValidateRLE(out, n, sizeof(bits)));
    memset(back, 0xFF, sizeof(back));
    T_EQ(MSG_DecodeRLE(out, n, sizeof(bits), rle_test_write, &dst), sizeof(bits));
    T_ASSERT(!memcmp(back, bits, sizeof(bits)));
}

/* Static entities must not consume snapshot bandwidth when their state is unchanged. */
TEST(net, unchanged_entity_delta_emits_nothing) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t state = { .number = 9, .model = 1, .origin = { 10.0f, 20.0f, 30.0f } };

    MSG_WriteDeltaEntity(&sb, &state, &state, false);

    T_EQ(sb.cursize, 0);
}

/* Headings are radians. Quarter turns must land exactly on the two-byte wire grid. */
TEST(net, entity_delta_preserves_radian_headings) {
    float angles[] = { 0, M_PI / 2, M_PI, 3 * M_PI / 2, -M_PI / 2, 2 * M_PI,
        -2 * M_PI, 0.8427f, -0.8427f, 8 * M_PI + 0.8427f };
    entityState_t from = {0}, out = {0};
    FOR_LOOP(i, sizeof(angles) / sizeof(angles[0])) {
        uint8_t bytes[256]; sizeBuf_t msg = make_msg_buf(bytes, sizeof(bytes));
        entityState_t to = { .number = 1, .model = 1, .angle = angles[i] };
        uint32_t bits;
        MSG_WriteDeltaEntity(&msg, &from, &to, true);
        int number = MSG_ReadEntityBits(&msg, &bits);
        MSG_ReadDeltaEntity(&msg, &out, number, bits);
        T_EQ(msg.readcount, msg.cursize);
        T_FEQ(remainderf(out.angle - to.angle, 2 * M_PI), 0, 0.00005f);
        if (i < 7) T_FEQ(remainderf(out.angle - to.angle, 2 * M_PI), 0, 0.000001f);
        from = to;
    }
}

TEST(net, entity_delta_preserves_large_wc3_radii) {
    float radii[] = { 36.0f, 72.0f, 200.0f, 320.0f };

    FOR_LOOP(i, sizeof(radii) / sizeof(radii[0])) {
        uint8_t buf[256];
        sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
        entityState_t from = { 0 }, to = { .number = 9, .radius = radii[i] }, out = { 0 };
        uint32_t bits = 0;
        int number;

        MSG_WriteDeltaEntity(&sb, &from, &to, true);
        sb.readcount = 0;
        number = MSG_ReadEntityBits(&sb, &bits);
        MSG_ReadDeltaEntity(&sb, &out, number, bits);

        T_EQ(number, 9);
        T_FEQ(out.radius, radii[i], 0.001f);
    }
}

/* Small RTS units need fractional radii and positions on both initial and moving snapshots. */
TEST(net, entity_delta_preserves_fractional_geometry) {
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .radius = 0.375f,
        .origin = { 42.375f, -44.625f, 8.125f }, .renderfx = RF_SELECTED }, out = { 0 };
    FOR_LOOP(i, 2) {
        uint8_t buf[256];
        sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
        uint32_t bits = 0;
        MSG_WriteDeltaEntity(&sb, &from, &to, true);
        int num = MSG_ReadEntityBits(&sb, &bits);
        MSG_ReadDeltaEntity(&sb, &out, num, bits);
        T_FEQ(out.radius, to.radius, 0.00001f);
        T_FEQ(out.origin.x, to.origin.x, 0.00001f);
        T_FEQ(out.origin.y, to.origin.y, 0.00001f);
        T_FEQ(out.origin.z, to.origin.z, 0.00001f);
        T_ASSERT(out.renderfx & RF_SELECTED);
        from = to;
        to.origin.x += 0.125f; to.origin.y -= 0.25f;
    }
}

/* Building placement cursor metadata must survive svc_cursor entity deltas without
 * overloading world-position fields. */
TEST(net, entity_delta_preserves_build_preview_fields) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 };
    entityState_t to = {
        .number = 9,
        .model = 1,
        .collision = 42.5f,
        .pathing_width = 6,
        .pathing_height = 4,
        .pathing_preview = EntityPathingPreviewPack(17, 0x0a, 0x20),
    };
    entityState_t out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_FEQ(out.collision, 42.5f, 0.001f);
    T_EQ(out.pathing_width, 6);
    T_EQ(out.pathing_height, 4);
    T_EQ(EntityPathingPreviewIgnore(out.pathing_preview), 17);
    T_EQ(EntityPathingPreviewPrevented(out.pathing_preview), 0x0a);
    T_EQ(EntityPathingPreviewRequired(out.pathing_preview), 0x20);
    T_FEQ(out.origin.x, 0.0f, 0.001f);
    T_FEQ(out.origin.y, 0.0f, 0.001f);
}

TEST(net, entity_delta_preserves_game_presentation_variant_bits) {
    FOR_LOOP(variant, 8) {
        uint8_t buf[256];
        sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
        entityState_t from = { 0 }, to = { .number = 9, .model = 1 }, out = { 0 };
        uint32_t bits = 0;
        int number;

        to.effect_flags = EFX_GAME_VARIANT_SET(EFX_MODEL, variant);
        MSG_WriteDeltaEntity(&sb, &from, &to, true);
        sb.readcount = 0;
        number = MSG_ReadEntityBits(&sb, &bits);
        MSG_ReadDeltaEntity(&sb, &out, number, bits);

        T_EQ(number, 9);
        T_EQ(out.effect_flags & EFX_MODEL, EFX_MODEL);
        T_EQ(EFX_GAME_VARIANT_GET(out.effect_flags), variant);
    }
}

TEST(net, entity_delta_preserves_hover_value) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .hover_value = 12501 }, out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_EQ(out.hover_value, 12501);
}

TEST(net, entity_delta_preserves_destructable_presentation_image) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .image = 7 }, out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);
    T_EQ(number, 9);
    T_EQ(out.image, 7);
}

/* Dead destructable remains rely on EF_NOT_SELECTABLE surviving snapshots, so
 * guard its round trip explicitly. */
TEST(net, entity_delta_preserves_not_selectable_flag) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .flags = EF_NOT_SELECTABLE }, out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_NOT_SELECTABLE);
}

TEST(net, entity_delta_preserves_wc3_resource_placement_flags) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 };
    entityState_t to = {
        .number = 9,
        .model = 1,
        .flags = EF_RESOURCE_SOURCE | EF_RESOURCE_RETURN,
    };
    entityState_t out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_RESOURCE_SOURCE);
    T_ASSERT(out.flags & EF_RESOURCE_RETURN);
}

/* Hover-health eligibility occupies the first bit above the legacy byte-sized
 * entity flags, so guard both the widened field and delta serialization. */
TEST(net, entity_delta_preserves_hover_health_flag) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .flags = EF_HOVER_HEALTH }, out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_HOVER_HEALTH);
}

TEST(net, entity_delta_preserves_hover_mana_flag) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .flags = EF_HOVER_MANA }, out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_HOVER_MANA);
}

TEST(net, entity_delta_preserves_packed_cargo_occupancy) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1 }, out = { 0 };
    uint32_t bits = 0;
    int number;

    to.stats[ENT_CARGO] = EntityCargoPack(3, 8);
    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_EQ(EntityCargoCount(out.stats[ENT_CARGO]), 3);
    T_EQ(EntityCargoCapacity(out.stats[ENT_CARGO]), 8);
}

/* Neutral hover-ring presentation is also recipient-authored snapshot state. */
TEST(net, entity_delta_preserves_neutral_flag) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .flags = EF_NEUTRAL }, out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_NEUTRAL);
}

/* Ground-surface presentation flags are shared snapshot state: WC3 uses them
 * to identify actors that need model-surface Z conformance and live walkable
 * destructables that can provide that authored surface. */
TEST(net, entity_delta_preserves_ground_surface_flags) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1,
        .flags = EF_GROUND_CONFORM | EF_GROUND_SURFACE, .ground_offset = 53.25f }, out = { 0 };
    uint32_t bits = 0;
    int number;

    MSG_WriteDeltaEntity(&sb, &from, &to, true);
    sb.readcount = 0;
    number = MSG_ReadEntityBits(&sb, &bits);
    MSG_ReadDeltaEntity(&sb, &out, number, bits);

    T_EQ(number, 9);
    T_ASSERT(out.flags & EF_GROUND_CONFORM);
    T_FEQ(out.ground_offset, 53.25f, 0.001f);
    T_ASSERT(out.flags & EF_GROUND_SURFACE);
}

/* WC3 building damage rendering relies on server-authored effect presentation
 * data surviving the shared entity delta unchanged. */
TEST(net, entity_delta_preserves_effect_model) {
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .model = 1, .effect = 2,
                                       .flags = EF_BUILDING, .effect_flags = EFX_MODEL | EFX_ATTACH_SLOTS |
                                                    EFX_SLOT_FIRST | EFX_SLOT_SECOND }, out = { 0 };
    uint32_t bits = 0;
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
    uint8_t buf[256];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    entityState_t from = { 0 }, to = { .number = 9, .event = EV_MOVE, .sound = 37 }, out = { 0 };
    uint32_t bits = 0;
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
    uint8_t buf[64];
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
    uint8_t buf[16];
    sizeBuf_t msg = make_msg_buf(buf, sizeof(buf));

    test_client_stubs_init(); CL_ClearMinimap();
    MSG_WriteByte(&msg, svc_minimap_ping); MSG_WriteFloat(&msg, 1.0f);
    msg.readcount = 0; CL_ParseServerMessage(&msg);

    T_EQ(CL_MinimapPingCount(), 0);
    T_EQ(CL_MinimapRecentCount(), 0);
}

/* Non-finite coordinates and clock-overflowing lifetimes cannot enter client state. */
TEST(net, minimap_ping_packet_rejects_invalid_values) {
    uint8_t buf[64];
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

static void net_install_single_layout_frame(uint32_t layer, FRAMETYPE type,
                                            float x, float y, float w, float h) {
    uint8_t buf[256];
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
    rect_t rect = { 128.0f, 128.0f, 768.0f, 576.0f };

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(rect_t, 0.0f, 0.22f, 1.0f, 0.76f);
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
    rect_t rect = { 320.0f, 128.0f, 576.0f, 576.0f };
    rect_t original = rect;

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(rect_t, 0.0f, 0.22f, 1.0f, 0.76f);
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
    rect_t rect = { 128.0f, 128.0f, 768.0f, 576.0f };

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(rect_t, 0.0f, 0.22f, 1.0f, 0.76f);
    net_install_single_layout_frame(LAYER_COMMANDBAR, FT_COMMANDBUTTON,
                                    0.60f, 0.41f, 0.10f, 0.08f);

    SCR_LayoutClampSelectionRect(&rect);

    T_FEQ(rect.y + rect.h, 0.41f / UI_BASE_HEIGHT * 768.0f, 1.0f);
}

TEST(client_screen, multiselect_left_click_is_consumed_and_sends_focus) {
    uint8_t layout_buf[512];
    uint8_t message_buf[256];
    uint8_t multiselect_buf[sizeof(uiMultiselect_t) + sizeof(uiMultiselectItem_t)];
    char command_buf[128];
    sizeBuf_t sb = make_msg_buf(layout_buf, sizeof(layout_buf));
    uiMultiselect_t *multi = (uiMultiselect_t *)multiselect_buf;
    uiFrame_t empty = {0}, frame = {0};

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    SZ_Init(&cls.netchan.message, message_buf, sizeof(message_buf));

    memset(multiselect_buf, 0, sizeof(multiselect_buf));
    multi->offset = MAKE(vec2_t, 0.031f, 0.050f);
    multi->numcolumns = 6;
    multi->numitems = 1;
    multi->items[0].entity = 77;

    frame.number = 1;
    frame.flags.type = FT_MULTISELECT;
    frame.size.width = 0.20f;
    frame.size.height = 0.20f;
    frame.points.x[FPP_MIN].used = 1;
    frame.points.x[FPP_MIN].targetPos = FPP_MIN;
    frame.points.y[FPP_MIN].used = 1;
    frame.points.y[FPP_MIN].targetPos = FPP_MIN;

    MSG_WriteByte(&sb, LAYER_INFOPANEL);
    MSG_WriteDeltaUIFrame(&sb, &empty, &frame, true);
    MSG_WriteByte(&sb, sizeof(multiselect_buf));
    MSG_Write(&sb, multiselect_buf, sizeof(multiselect_buf));
    MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0;
    CL_ParseLayout(&sb);

    T_ASSERT(SCR_LayoutMouseEvent(MENU_MOUSE_DOWN, 10, 10, 1));
    T_EQ(cls.netchan.message.cursize, 0);
    T_ASSERT(SCR_LayoutMouseEvent(MENU_MOUSE_UP, 10, 10, 1));
    cls.netchan.message.readcount = 0;
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    MSG_ReadString(&cls.netchan.message, command_buf);
    T_STREQ(command_buf, "focus 77");
}

TEST(client_screen, command_button_right_click_sends_secondary_command) {
    uint8_t layout_buf[512];
    uint8_t message_buf[256];
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
    uint8_t layout_buf[512];
    uint8_t message_buf[256];
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
    rect_t rect = { 128.0f, 128.0f, 768.0f, 576.0f };
    rect_t original = rect;

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(rect_t, 0.0f, 0.22f, 1.0f, 0.76f);
    net_install_single_layout_frame(LAYER_CONSOLE, FT_TEXTURE,
                                    0.25f, 0.05f, 0.30f, 0.05f);

    SCR_LayoutClampSelectionRect(&rect);

    T_FEQ(rect.w, original.w, 0.01f);
    T_FEQ(rect.h, original.h, 0.01f);
}

/* WoW/SC2-style full-screen world viewports do not opt into the WC3
 * bottom-console protrusion rule. */
TEST(client_screen, selection_rect_fullscreen_world_does_not_use_console_clamp) {
    rect_t rect = { 128.0f, 128.0f, 768.0f, 576.0f };
    rect_t original = rect;

    test_client_stubs_init();
    FOR_LOOP(layer, MAX_LAYOUT_LAYERS) SCR_ClearLayoutLayer(layer);
    cl.viewDef.scissor = MAKE(rect_t, 0.0f, 0.0f, 1.0f, 1.0f);
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
    uint8_t buf[256];
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

    rect_t const *r = SCR_LayoutRect(SCR_Frame(1));
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

static void net_send_remove(sizeBuf_t *sb, uint32_t number) {
    MSG_WriteByte(sb, svc_packetentities);
    MSG_WriteEntityBits(sb, 1u << U_REMOVE, number);
    MSG_WriteEntityBits(sb, 0, 0);
}

TEST(net, baseline_defaults_omitted_entity_scale) {
    uint8_t buf[512];
    sizeBuf_t sb;
    entityState_t state = { .number = 7, .model = 1 };

    test_client_stubs_init();
    sb = make_msg_buf(buf, sizeof(buf));
    net_send_baseline(&sb, &state);
    net_parse(&sb);
    T_FEQ(cl.ents[7].baseline.scale, 1.0f, 0.0001f);
    T_FEQ(cl.ents[7].current.scale, 1.0f, 0.0001f);
    T_FEQ(cl.ents[7].prev.scale, 1.0f, 0.0001f);

    state.scale = 1.25f;
    sb = make_msg_buf(buf, sizeof(buf));
    net_send_baseline(&sb, &state);
    net_parse(&sb);
    T_FEQ(cl.ents[7].baseline.scale, 1.25f, 0.0001f);
    T_FEQ(cl.ents[7].current.scale, 1.25f, 0.0001f);
    T_FEQ(cl.ents[7].prev.scale, 1.25f, 0.0001f);
}

/* Membership must track current.model exactly across both transitions, plus the
 * U_REMOVE-after-model-cleared sequence the server produces for model-less
 * sound/event entities. */
TEST(net, active_entity_list_tracks_model_transitions) {
    uint8_t buf[512];
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
    uint8_t buf[512];
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

/* The game-owned datagram must decode attached lightning and clear stale records on the next frame. */
TEST(net, lightning_datagram_round_trip_and_clear) {
    uint8_t buf[1024];
    sizeBuf_t sb;
    lightningEffect_t bolt = MAKE(lightningEffect_t,
        .handle = 7, .effect_id = MAKEFOURCC('C', 'L', 'P', 'B'),
        .source = { 1.0f, 2.0f, 3.0f }, .target = { 4.0f, 5.0f, 6.0f },
        .color = COLOR32_WHITE, .start_time = 100, .end_time = 2000);

    test_client_stubs_init();
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteLong(&sb, 1); MSG_WriteLong(&sb, 100); MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, BZ_GAME_DATAGRAM_LIGHTNING); MSG_WriteShort(&sb, 1);
    MSG_Write(&sb, &bolt, sizeof(bolt));
    sb.readcount = 0; CL_ParseFrame(&sb);
    T_EQ(cl.num_lightning_effects, 1);
    T_EQ(cl.lightning_effects[0].handle, 7);
    T_FEQ(cl.lightning_effects[0].target.z, 6.0f, 0.001f);

    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteLong(&sb, 2); MSG_WriteLong(&sb, 200); MSG_WriteLong(&sb, 1);
    MSG_WriteShort(&sb, 0);
    sb.readcount = 0; CL_ParseFrame(&sb);
    T_EQ(cl.num_lightning_effects, 0);
    T_EQ(cl.viewDef.num_lightning_effects, 0);
}

/* Malformed lightning counts must consume the frame without exposing partial client state. */
TEST(net, lightning_datagram_rejects_oversized_count) {
    uint8_t buf[64];
    sizeBuf_t sb;

    test_client_stubs_init();
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteLong(&sb, 1); MSG_WriteLong(&sb, 100); MSG_WriteLong(&sb, 0);
    MSG_WriteShort(&sb, BZ_GAME_DATAGRAM_LIGHTNING); MSG_WriteShort(&sb, MAX_LIGHTNING_EFFECTS + 1);
    sb.readcount = 0; CL_ParseFrame(&sb);
    T_EQ(cl.num_lightning_effects, 0);
    T_EQ(sb.readcount, sb.cursize);
}

TEST(net, console_print_message_is_consumed) {
    uint8_t buf[128];
    sizeBuf_t sb;

    test_client_stubs_init();
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_console_print);
    MSG_WriteString(&sb, "WC3: objective completion candidate trigger 40 enabled");
    sb.readcount = 0;

    CL_ParseServerMessage(&sb);

    T_EQ(sb.readcount, sb.cursize);
    T_STREQ(test_console_message, "WC3: objective completion candidate trigger 40 enabled");
}

TEST(net, set_selection_rejects_undersized_payload) {
    uint8_t buf[4];
    sizeBuf_t sb;
    uint32_t saved_num = cl.selection.num_selected;
    uint32_t saved_ent = cl.selection.entity_nums[0];

    test_client_stubs_init();
    /* Count says one entity, but the entity word is absent. */
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_set_selection);
    MSG_WriteByte(&sb, 1);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
    T_EQ(cl.selection.num_selected, saved_num);
    T_EQ(cl.selection.entity_nums[0], saved_ent);
}

TEST(net, set_selection_rejects_zero_entity) {
    uint8_t buf[64];
    sizeBuf_t sb;
    uint32_t saved_num = cl.selection.num_selected;

    test_client_stubs_init();
    /* Entity number 0 should be rejected. */
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_set_selection);
    MSG_WriteByte(&sb, 1);
    MSG_WriteLong(&sb, 0); /* entity 0 */
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
    T_EQ(cl.selection.num_selected, saved_num);
}

TEST(net, set_selection_rejects_entity_exceeding_max) {
    uint8_t buf[64];
    sizeBuf_t sb;
    uint32_t saved_num = cl.selection.num_selected;

    test_client_stubs_init();
    /* Entity number >= MAX_CLIENT_ENTITIES should be rejected. */
    sb = make_msg_buf(buf, sizeof(buf));
    MSG_WriteByte(&sb, svc_set_selection);
    MSG_WriteByte(&sb, 1);
    MSG_WriteLong(&sb, MAX_CLIENT_ENTITIES);
    sb.readcount = 0;
    CL_ParseServerMessage(&sb);
    T_EQ(cl.selection.num_selected, saved_num);
}

TEST(net, order_marker_configstring_precache_replace_and_clear) {
    uint8_t buf[512];
    sizeBuf_t sb = make_msg_buf(buf, sizeof(buf));
    cstring_t paths[] = { "TestUI/Models/quad_sprite.mdx", "TestUI/Models/panel_sprite.mdx", "" };
    test_client_stubs_init();
    test_model_loads = test_model_releases = 0;
    re.LoadModel = capture_load_model;
    re.ReleaseModel = capture_release_model;
    cl.refresh_prepped = false;
    MSG_WriteByte(&sb, svc_configstring);
    MSG_WriteShort(&sb, CS_ORDER_MARKER);
    MSG_WriteString(&sb, paths[0]);
    CL_ParseServerMessage(&sb);
    T_EQ(test_model_loads, 0);
    CL_RegisterConfigString(CS_ORDER_MARKER);
    T_EQ(test_model_loads, 1);
    T_STREQ(test_model_load_paths[0], paths[0]);
    cl.refresh_prepped = true;
    FOR_LOOP(i, 3) {
        SZ_Clear(&sb); sb.readcount = 0;
        MSG_WriteByte(&sb, svc_configstring);
        MSG_WriteShort(&sb, CS_ORDER_MARKER);
        MSG_WriteString(&sb, paths[i]);
        CL_ParseServerMessage(&sb);
        T_EQ(test_model_loads, i ? 2 : 1);
        T_EQ(test_model_releases, i);
    }
    T_NULL(cl.moveConfirmation);
}

/* Input payloads must remain framed for the next command and reject partial/invalid operations. */
TEST(net, typed_controller_input_roundtrip_and_validation) {
    uint8_t data[128];
    sizeBuf_t msg;
    inputCmd_t out, cmds[] = {
        { .action = BZ_INPUT_FOCUS, .focus = {12.5f, -34.25f} },
        { .action = BZ_INPUT_VIEW, .view = {{18, 0, -90}, 8.5f} },
        { .action = BZ_INPUT_MOVE, .move = {BZ_MOVE_FORWARD | BZ_MOVE_LEFT, 16} },
    };
    SZ_Init(&msg, data, sizeof(data));
    FOR_LOOP(i, 3) MSG_WriteInput(&msg, &cmds[i]);
    T_EQ(msg.cursize, 30);
    T_ASSERT(MSG_ReadInput(&msg, &out)); T_EQ(out.action, BZ_INPUT_FOCUS);
    T_FEQ(out.focus.x, 12.5f, 0.001f); T_FEQ(out.focus.y, -34.25f, 0.001f);
    T_ASSERT(MSG_ReadInput(&msg, &out)); T_EQ(out.action, BZ_INPUT_VIEW);
    T_FEQ(out.view.angles.x, 18, 0.001f); T_FEQ(out.view.angles.z, -90, 0.001f);
    T_FEQ(out.view.distance, 8.5f, 0.001f);
    T_ASSERT(MSG_ReadInput(&msg, &out)); T_EQ(out.action, BZ_INPUT_MOVE);
    T_EQ(out.move.buttons, BZ_MOVE_FORWARD | BZ_MOVE_LEFT); T_EQ(out.move.msec, 16);
    T_EQ(msg.readcount, msg.cursize); T_ASSERT(!MSG_ReadInput(&msg, &out));
    FOR_LOOP(i, 3) {
        SZ_Clear(&msg); msg.readcount = 0; MSG_WriteInput(&msg, &cmds[i]);
        uint32_t size = msg.cursize;
        FOR_LOOP(n, size) {
            msg.readcount = 0; msg.cursize = n;
            T_ASSERT(!MSG_ReadInput(&msg, &out));
        }
    }
    cmds[0].focus.x = NAN;
    cmds[1].view.distance = -1;
    cmds[2].move.buttons = 128;
    FOR_LOOP(i, 3) {
        SZ_Clear(&msg); msg.readcount = 0; MSG_WriteInput(&msg, &cmds[i]);
        T_ASSERT(!MSG_ReadInput(&msg, &out));
    }
    SZ_Clear(&msg); msg.readcount = 0; MSG_WriteByte(&msg, 255); T_ASSERT(!MSG_ReadInput(&msg, &out));
    SZ_Clear(&msg); msg.readcount = 0; cmds[2].move.buttons = 0; cmds[2].move.msec = BZ_INPUT_MAX_MSEC + 1;
    MSG_WriteInput(&msg, &cmds[2]); T_ASSERT(!MSG_ReadInput(&msg, &out));
}

/* Predict presentation while preserving the delta baseline, then yield to rejected input and cinematics. */
TEST(net, orbit_prediction_expires_and_yields_to_scripted_camera) {
    uint8_t data[256];
    sizeBuf_t msg = make_msg_buf(data, sizeof(data));
    player_t from = {0}, to = { .number = 1, .client_ui_state = CLIENT_UI_GAME, .viewangles = {18, 0, 0},
        .vieworigin = {10, 20, 40}, .distance = 8, .fov = 45, .znear = 0.1f, .zfar = 1000 };
    test_client_stubs_init();
    cl.time = 100;
    cl.camera_prediction.view = true;
    cl.camera_prediction.view_ms = cl.time;
    cl.camera_prediction.angles = (vec3_t){25, 0, 90};
    cl.camera_prediction.distance = 12;
    MSG_WriteByte(&msg, svc_playerinfo); MSG_WriteDeltaPlayerState(&msg, &from, &to);
    CL_ParseServerMessage(&msg);
    T_FEQ(cl.playerstate.viewangles.z, 0, 0.001f); T_FEQ(cl.playerstate.distance, 8, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].viewangles.z, 90, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].distance, 12, 0.001f);
    T_FEQ(cl.viewDef.camerastate[0].origin.z, 40, 0.001f);
    cl.time += BZ_INPUT_MAX_MSEC + 1;
    from = to;
    SZ_Clear(&msg); msg.readcount = 0;
    MSG_WriteByte(&msg, svc_playerinfo); MSG_WriteDeltaPlayerState(&msg, &from, &to);
    CL_ParseServerMessage(&msg);
    T_ASSERT(!cl.camera_prediction.view);
    T_FEQ(cl.viewDef.camerastate[0].distance, 8, 0.001f);
    cl.camera_prediction.view = true; cl.camera_prediction.view_ms = cl.time;
    to.client_ui_state = CLIENT_UI_CINEMATIC; to.distance = 20;
    SZ_Clear(&msg); msg.readcount = 0;
    MSG_WriteByte(&msg, svc_playerinfo); MSG_WriteDeltaPlayerState(&msg, &from, &to);
    CL_ParseServerMessage(&msg);
    T_ASSERT(!cl.camera_prediction.view);
    T_FEQ(cl.viewDef.camerastate[0].distance, 20, 0.001f);
}

static void capture_asset_scope(cstring_t scope) { T_STREQ(scope, "Test.w3m"); }

/* Loading dependencies become usable at the first batch boundary; later world resources still defer. */
TEST(net, loading_batch_registers_media_before_full_precache) {
    uint8_t buf[1024];
    sizeBuf_t msg = make_msg_buf(buf, sizeof(buf));
    uiFrame_t frame = { .number = 1, .flags.type = FT_TEXTURE, .tex.index = 1 };
    uiFrame_t empty = { 0 };
    bool old_init = scr_initialized;
    test_client_stubs_init(); scr_initialized = false;
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, sizeof(cls.netchan.message_buf));
    test_model_loads = test_tex_loads = 0;
    re.SetAssetScope = capture_asset_scope;
    re.LoadModel = capture_load_model; re.LoadTexture = capture_load_texture;
    snprintf(cl.configstrings[CS_WORLD], sizeof(PATHSTR), "Test.w3m");
    snprintf(cl.configstrings[CS_ASSET_SCOPE], sizeof(PATHSTR), "Test.w3m");
    MSG_WriteByte(&msg, svc_configstring); MSG_WriteShort(&msg, CS_MODELS + 1); MSG_WriteString(&msg, "Loading.mdx");
    MSG_WriteByte(&msg, svc_configstring); MSG_WriteShort(&msg, CS_IMAGES + 1); MSG_WriteString(&msg, "Loading.blp");
    uint8_t packed[512] = { 0 }, layout[256];
    sizeBuf_t screen = make_msg_buf(layout, sizeof(layout));
    MSG_WriteByte(&screen, LAYER_LOADING);
    MSG_WriteDeltaUIFrame(&screen, &empty, &frame, true); MSG_WriteByte(&screen, 0);
    MSG_WriteLong(&screen, 0); MSG_WriteShort(&screen, 0);
    uLongf size = sizeof(packed);
    T_EQ(compress2(packed, &size, layout, screen.cursize, Z_BEST_COMPRESSION), Z_OK);
    MSG_WriteByte(&msg, svc_loading_screen);
    MSG_WriteLong(&msg, size); MSG_WriteLong(&msg, 0); MSG_WriteLong(&msg, size / 2);
    MSG_Write(&msg, packed, size / 2);
    CL_ParseServerMessage(&msg);
    T_NULL(cl.layout[LAYER_LOADING]); T_EQ(test_model_loads, 0); T_EQ(test_tex_loads, 0);
    T_NOT_NULL(cl.loading.data);
    SZ_Clear(&msg); msg.readcount = 0;
    MSG_WriteByte(&msg, svc_loading_screen);
    MSG_WriteLong(&msg, size); MSG_WriteLong(&msg, size / 2); MSG_WriteLong(&msg, size - size / 2);
    MSG_Write(&msg, packed + size / 2, size - size / 2);
    MSG_WriteByte(&msg, svc_mirror); MSG_WriteString(&msg, "baselines 25");
    CL_ParseServerMessage(&msg);
    T_NULL(cl.loading.data); T_EQ(cl.loading.cursize, 0);
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    T_STREQ(MSG_ReadString2(&cls.netchan.message), "baselines 25");
    T_EQ(test_model_loads, 1); T_EQ(test_tex_loads, 1);
    T_NOT_NULL(cl.models[1]); T_NOT_NULL(cl.pics[1]); T_ASSERT(!cl.precache_ready);
    SZ_Clear(&msg); msg.readcount = 0;
    MSG_WriteByte(&msg, svc_configstring); MSG_WriteShort(&msg, CS_MODELS + 2); MSG_WriteString(&msg, "World.mdx");
    MSG_WriteByte(&msg, svc_mirror); MSG_WriteString(&msg, "baselines");
    CL_ParseServerMessage(&msg);
    T_ASSERT(!cl.precache_ready); T_NULL(cl.models[2]); T_EQ(test_model_loads, 1);
    SZ_Clear(&msg); msg.readcount = 0;
    MSG_WriteByte(&msg, svc_mirror); MSG_WriteString(&msg, "baselines 25");
    CL_ParseServerMessage(&msg);
    T_ASSERT(!cl.precache_ready);
    SZ_Clear(&msg); msg.readcount = 0;
    MSG_WriteByte(&msg, svc_mirror); MSG_WriteString(&msg, "precache");
    CL_ParseServerMessage(&msg);
    T_ASSERT(cl.precache_ready);
    MemFree(cl.layout[LAYER_LOADING]); cl.layout[LAYER_LOADING] = NULL;
    SCR_ClearLayoutLayer(LAYER_LOADING); scr_initialized = old_init;
}

/* Corruption, truncation, oversized lengths, and missing chunks must never publish a screen. */
TEST(net, loading_screen_rejects_invalid_payloads) {
    uint8_t buf[1024], packed[32] = { 0 };
    sizeBuf_t msg = make_msg_buf(buf, sizeof(buf));
    static const struct { uint32_t total, pos, len, bytes; } cases[] = {
        { 32, 0, 32, 32 },
        { 32, 0, 32, 31 },
        { MAX_MSGLEN + 1, 0, 1, 1 },
        { 32, 0, 33, 32 },
        { 32, 16, 16, 16 },
        { 0, 0, 0, 0 },
        { 32, 0, 0, 0 },
        { 32, 0xffffffff, 1, 1 },
    };
    test_client_stubs_init();
    FOR_LOOP(i, sizeof(cases) / sizeof(*cases)) {
        SZ_Clear(&msg); msg.readcount = 0;
        MSG_WriteByte(&msg, svc_loading_screen);
        MSG_WriteLong(&msg, cases[i].total); MSG_WriteLong(&msg, cases[i].pos); MSG_WriteLong(&msg, cases[i].len);
        MSG_Write(&msg, packed, cases[i].bytes);
        CL_ParseServerMessage(&msg);
        T_NULL(cl.layout[LAYER_LOADING]); T_ASSERT(!cl.precache_ready); T_NULL(cl.loading.data);
    }
    SZ_Clear(&msg); msg.readcount = 0;
    MSG_WriteByte(&msg, svc_loading_screen); MSG_WriteLong(&msg, 32);
    CL_ParseServerMessage(&msg);
    T_NULL(cl.layout[LAYER_LOADING]); T_NULL(cl.loading.data);
}

/* A continuation must match the initial allocation and the exact next offset. */
TEST(net, loading_screen_rejects_chunk_gaps_and_changed_total) {
    uint8_t buf[64];
    sizeBuf_t msg = make_msg_buf(buf, sizeof(buf));
    test_client_stubs_init();
    FOR_LOOP(i, 2) {
        SZ_Clear(&msg); msg.readcount = 0;
        MSG_WriteByte(&msg, svc_loading_screen);
        MSG_WriteLong(&msg, 32); MSG_WriteLong(&msg, 0); MSG_WriteLong(&msg, 1); MSG_WriteByte(&msg, 0);
        CL_ParseServerMessage(&msg);
        T_NOT_NULL(cl.loading.data); T_EQ(cl.loading.cursize, 1);
        SZ_Clear(&msg); msg.readcount = 0;
        MSG_WriteByte(&msg, svc_loading_screen);
        MSG_WriteLong(&msg, i ? 33 : 32); MSG_WriteLong(&msg, i ? 1 : 2);
        MSG_WriteLong(&msg, 1); MSG_WriteByte(&msg, 0);
        CL_ParseServerMessage(&msg);
        T_NULL(cl.layout[LAYER_LOADING]); T_NULL(cl.loading.data); T_EQ(cl.loading.cursize, 0);
    }
}

/* Transport keepalives must not replace lobby presentation or stop parsing the next message. */
TEST(net, keepalive_preserves_loading_state_and_continues_packet) {
    uint8_t buf[64];
    sizeBuf_t msg = make_msg_buf(buf, sizeof(buf));
    test_client_stubs_init();
    SZ_Init(&cls.netchan.message, cls.netchan.message_buf, sizeof(cls.netchan.message_buf));
    cl.loading_progress = 0.4f;
    MSG_WriteByte(&msg, svc_nop);
    MSG_WriteByte(&msg, svc_mirror); MSG_WriteString(&msg, "baselines 25");
    CL_ParseServerMessage(&msg);
    T_FEQ(cl.loading_progress, 0.4f, 0.001f); T_ASSERT(!cl.precache_ready);
    cls.netchan.message.readcount = 0;
    T_EQ(MSG_ReadByte(&cls.netchan.message), clc_stringcmd);
    T_STREQ(MSG_ReadString2(&cls.netchan.message), "baselines 25");
}

/* Preserve the high preview flag across the actual wire codec and client draw dispatch. */
static int sprite_order[3], sprite_order_count;
static void capture_sprite_order(drawSprite_t const *sprite) {
    if (sprite_order_count < 3) sprite_order[sprite_order_count++] = atoi(sprite->anim + 1);
}
static void capture_image_order(texture_t const *tex, rect_t const *rect, rect_t const *uv, color32_t color) {
    (void)tex; (void)rect; (void)uv; (void)color;
    if (sprite_order_count < 3) sprite_order[sprite_order_count++] = 2;
}

/* Foreground sprites must be drawn after artwork regardless of frame serialization order. */
TEST(client_layout, sprite_overlay_draws_after_button_artwork) {
    uint8_t data[1024];
    sizeBuf_t msg = make_msg_buf(data, sizeof(data));
    uiFrame_t empty = {0}, frames[3] = {
        { .number = 1, .flags.type = FT_SPRITE, .text = "#1" },
        { .number = 2, .flags.type = FT_LOADING_BAR },
        { .number = 3, .flags.type = FT_SPRITE, .text = "#3" },
    };
    test_client_stubs_init();
    __typeof__(re.DrawSprite) old_sprite = re.DrawSprite;
    __typeof__(re.DrawImage) old_image = re.DrawImage;
    frames[0].flagsvalue |= UIFLAG_SPRITE_OVERLAY;
    MSG_WriteByte(&msg, LAYER_CONSOLE);
    FOR_LOOP(i, 3) { MSG_WriteDeltaUIFrame(&msg, &empty, &frames[i], true); MSG_WriteByte(&msg, 0); }
    MSG_WriteLong(&msg, 0); MSG_WriteShort(&msg, 0); msg.readcount = 0;
    CL_ParseLayout(&msg);
    re.DrawSprite = capture_sprite_order; re.DrawImage = capture_image_order; sprite_order_count = 0;
    SCR_Clear(cl.layout[LAYER_CONSOLE]);
    SCR_LayoutDrawOverlay(cl.layout[LAYER_CONSOLE]);
    T_EQ(sprite_order_count, 3); T_EQ(sprite_order[0], 3); T_EQ(sprite_order[1], 2); T_EQ(sprite_order[2], 1);
    re.DrawSprite = old_sprite; re.DrawImage = old_image;
}

TEST(net, sprite_overlay_survives_layout_delta) {
    uint8_t data[256];
    sizeBuf_t msg = make_msg_buf(data, sizeof(data));
    uiFrame_t empty = {0}, input = { .number = 1, .flags.type = FT_SPRITE }, output = {0};
    uint32_t bits;
    input.flagsvalue |= UIFLAG_SPRITE_OVERLAY;
    MSG_WriteDeltaUIFrame(&msg, &empty, &input, true);
    uint32_t num = MSG_ReadEntityBits(&msg, &bits);
    MSG_ReadDeltaUIFrame(&msg, &output, num, bits);
    T_EQ(output.flagsvalue, input.flagsvalue);
}

TEST(net, loading_minimap_dispatches_static_map_after_delta_decode) {
    uint8_t data[256];
    sizeBuf_t msg = make_msg_buf(data, sizeof(data));
    uiFrame_t empty = {0}, input = { .number = 1, .flags.type = FT_MINIMAP,
        .text = "Maps\\FrozenThrone\\(2)BanditRidge.w3x" }, output = {0};
    uint32_t bits;
    test_client_stubs_init();
    __typeof__(re.DrawMinimap) old_draw = re.DrawMinimap;
    input.flagsvalue |= UIFLAG_MINIMAP_PREVIEW;
    MSG_WriteDeltaUIFrame(&msg, &empty, &input, true);
    uint32_t num = MSG_ReadEntityBits(&msg, &bits);
    MSG_ReadDeltaUIFrame(&msg, &output, num, bits);
    re.DrawMinimap = capture_minimap;
    CL_LayoutDrawMinimap(&output, &(rect_t){0, 0, 0.16f, 0.16f});
    T_ASSERT(output.flagsvalue & UIFLAG_MINIMAP_PREVIEW);
    T_STREQ(minimap_map, input.text);
    output.flagsvalue &= ~UIFLAG_MINIMAP_PREVIEW;
    CL_LayoutDrawMinimap(&output, &(rect_t){0, 0, 0.16f, 0.16f});
    T_NULL(minimap_map);
    re.DrawMinimap = old_draw;
}
