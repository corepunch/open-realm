#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../hud/hud_local.h"
#include "common/ui_constants.h"
#include <string.h>

bool run_test_jass(cstring_t src);
void setup_test_world(void);
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
extern player_t *currentplayer;

#define MB_MAX_LAYOUTS 16 // svc_layout messages one test captures
#define MB_MAX_TEXTS 64   // FT_STRING texts one test captures
#define TT_MAX_FIELDS 20 // fields; captures the 17-field update and 5-field removal packets

typedef struct {
    pfWriteType_t types[TT_MAX_FIELDS];
    int32_t integral[TT_MAX_FIELDS];
    float real[TT_MAX_FIELDS];
    vec3_t position;
    char text[32];
    uint32_t count, multicast_count;
    multicast_t multicast_to;
    vec3_t multicast_origin;
    char font_name[MAX_PATHLEN];
    uint32_t font_size;
} texttagCapture_t;

static texttagCapture_t texttag_capture;

static void texttag_capture_write(pfWriteType_t type, void const *value) {
    uint32_t const slot = texttag_capture.count++;
    if (slot >= TT_MAX_FIELDS || !value) return;
    texttag_capture.types[slot] = type;
    switch (type) {
        case PF_BYTE:
        case PF_SHORT:
        case PF_LONG:
            texttag_capture.integral[slot] = *(int32_t const *)value;
            break;
        case PF_FLOAT:
            texttag_capture.real[slot] = *(float const *)value;
            break;
        case PF_POSITION:
            texttag_capture.position = *(vec3_t const *)value;
            break;
        case PF_STRING:
            strlcpy(texttag_capture.text, value, sizeof(texttag_capture.text));
            break;
        default:
            break;
    }
}

static int texttag_capture_font(cstring_t name, uint32_t size) {
    strlcpy(texttag_capture.font_name, name ? name : "", sizeof(texttag_capture.font_name));
    texttag_capture.font_size = size;
    return 17;
}

static void texttag_capture_multicast(vec3_t const *origin, multicast_t to) {
    texttag_capture.multicast_count++;
    texttag_capture.multicast_to = to;
    if (origin) texttag_capture.multicast_origin = *origin;
}

/* svc_layout capture: one header (svc_layout byte + layer byte) per message,
 * then the frames until UI_WriteEnd.  Mirrors the client contract that one
 * message owns one layer, so a layer byte is read only after svc_layout. */
static int32_t mb_layout_layers[MB_MAX_LAYOUTS];
static uint32_t mb_layout_frames[MB_MAX_LAYOUTS];
static uint32_t mb_layout_count;
static bool mb_layer_pending;
static char mb_texts[MB_MAX_TEXTS][32];
static int32_t mb_text_layers[MB_MAX_TEXTS];
static uint32_t mb_text_count;
static uint32_t mb_unicast_count;
static edict_t *mb_unicast_target;

static void mb_reset_capture(void) {
    memset(mb_layout_layers, 0, sizeof(mb_layout_layers));
    memset(mb_layout_frames, 0, sizeof(mb_layout_frames));
    memset(mb_texts, 0, sizeof(mb_texts));
    memset(mb_text_layers, 0, sizeof(mb_text_layers));
    mb_layout_count = mb_text_count = mb_unicast_count = 0;
    mb_layer_pending = false;
    mb_unicast_target = NULL;
}

static void mb_capture_write(pfWriteType_t type, void const *data) {
    if (type == PF_BYTE && data) {
        int32_t const value = *(int32_t const *)data;
        if (mb_layer_pending) {
            if (mb_layout_count < MB_MAX_LAYOUTS) mb_layout_layers[mb_layout_count] = value;
            mb_layout_count++;
            mb_layer_pending = false;
        } else if (value == svc_layout) {
            mb_layer_pending = true;
        }
        return;
    }
    if (type == PF_UIFRAME && data && mb_layout_count && mb_layout_count <= MB_MAX_LAYOUTS) {
        uiFrame_t const *frame = data;
        mb_layout_frames[mb_layout_count - 1]++;
        if (frame->flags.type == FT_STRING && frame->text && mb_text_count < MB_MAX_TEXTS) {
            mb_text_layers[mb_text_count] = mb_layout_layers[mb_layout_count - 1];
            snprintf(mb_texts[mb_text_count++], sizeof(mb_texts[0]), "%s", frame->text);
        }
    }
}

static void mb_capture_unicast(edict_t *ent) { mb_unicast_count++; mb_unicast_target = ent; }

static uint32_t mb_layouts_on(int32_t layer) {
    uint32_t count = 0;
    FOR_LOOP(i, MIN(mb_layout_count, (uint32_t)MB_MAX_LAYOUTS)) if (mb_layout_layers[i] == layer) count++;
    return count;
}

/* FT_STRING text written inside a message on `layer`. */
static bool mb_text_seen_on(int32_t layer, cstring_t text) {
    FOR_LOOP(i, mb_text_count) if (mb_text_layers[i] == layer && !strcmp(mb_texts[i], text)) return true;
    return false;
}

typedef struct {
    void (*write)(pfWriteType_t, void const *);
    void (*unicast)(edict_t *);
} mbCaptureSaved_t;

static mbCaptureSaved_t mb_install_capture(void) {
    mbCaptureSaved_t saved = { gi.Write, gi.unicast };
    mb_reset_capture();
    gi.Write = mb_capture_write;
    gi.unicast = mb_capture_unicast;
    return saved;
}

static void mb_restore_capture(mbCaptureSaved_t saved) { gi.Write = saved.write; gi.unicast = saved.unicast; }

/* viewer -> owner advanced sharing, the only alliance edge Team Resources reads. */
static void mb_grant_advanced(gameClient_t *viewer, gameClient_t *owner, bool advanced) {
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_SHARED_CONTROL, true);
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_SHARED_ADVANCED_CONTROL, advanced);
}

TEST(wc3_multiboard, team_resources_layer_is_distinct_from_command_error_layer) {
    edict_t *viewer = &g_edicts[0];
    mbCaptureSaved_t saved;

    setup_test_world();
    G_SetClientConnected(viewer, true);
    mb_grant_advanced(viewer->client, &game.clients[1], true);
    game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 321;

    saved = mb_install_capture();
    UI_WriteMultiboard(viewer);
    UI_WriteCommandError(viewer, "Not enough gold.");
    mb_restore_capture(saved);

    /* The client clears a layer before storing a message for it, so the
     * command-failure overlay must never share the Team Resources layer. */
    T_EQ(mb_layout_count, 2);
    T_EQ(mb_layout_layers[0], WC3_LAYER_MULTIBOARD);
    T_EQ(mb_layout_layers[1], WC3_LAYER_COMMAND_ERROR);
    T_NE(mb_layout_layers[0], mb_layout_layers[1]);
    T_ASSERT(mb_layout_frames[0] > 0);
    T_ASSERT(mb_text_seen_on(WC3_LAYER_MULTIBOARD, "321"));
    T_NE(WC3_LAYER_MULTIBOARD, WC3_LAYER_LEADERBOARD);
    T_NE(WC3_LAYER_MULTIBOARD, WC3_LAYER_TIMERDIALOG);
    T_ASSERT(WC3_LAYER_MULTIBOARD < MAX_LAYOUT_LAYERS);
}

static bool mb_gold_text_seen(gameClient_t const *owner) {
    char gold[32];
    snprintf(gold, sizeof(gold), "%ld", (long)owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD]);
    return mb_text_seen_on(WC3_LAYER_MULTIBOARD, gold);
}

/* A Barracks the AI owner can afford to train from; SP_TrainUnit charges the
 * owner's gold directly, exactly like bot and map-script production. */
static edict_t *mb_alloc_ai_barracks(gameClient_t *owner, UnitProfile_t *profile) {
    edict_t *barracks = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    barracks->s.player = owner->ps.number;
    barracks->data.UnitProfile = profile;
    owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    owner->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    owner->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    return barracks;
}

/* Frames in the most recent WC3_LAYER_MULTIBOARD message; UINT32_MAX when none. */
static uint32_t mb_last_multiboard_frames(void) {
    uint32_t frames = UINT32_MAX;
    FOR_LOOP(i, MIN(mb_layout_count, (uint32_t)MB_MAX_LAYOUTS))
        if (mb_layout_layers[i] == WC3_LAYER_MULTIBOARD) frames = mb_layout_frames[i];
    return frames;
}

TEST(wc3_multiboard, team_resources_panel_follows_advanced_control) {
    edict_t *viewer = &g_edicts[0];
    gameClient_t *owner = &game.clients[1];
    mbCaptureSaved_t saved;

    setup_test_world();
    G_SetClientConnected(viewer, true);
    owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 777;
    owner->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 55;

    /* Basic shared control reveals nothing: the layer is written empty. */
    mb_grant_advanced(viewer->client, owner, false);
    saved = mb_install_capture();
    UI_WriteMultiboard(viewer);
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 1);
    T_EQ(mb_last_multiboard_frames(), 0);
    T_ASSERT(mb_unicast_target == viewer);

    /* Advanced control shows the owner's name row with gold and lumber. */
    mb_grant_advanced(viewer->client, owner, true);
    mb_reset_capture();
    UI_WriteMultiboard(viewer);
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 1);
    T_ASSERT(mb_last_multiboard_frames() > 0);
    T_ASSERT(mb_text_seen_on(WC3_LAYER_MULTIBOARD, "Team Resources"));
    T_ASSERT(mb_text_seen_on(WC3_LAYER_MULTIBOARD, "777"));
    T_ASSERT(mb_text_seen_on(WC3_LAYER_MULTIBOARD, "55"));

    /* The reverse edge grants nothing to the owner's own panel. */
    mb_reset_capture();
    G_SetClientConnected(&g_edicts[1], true);
    UI_WriteMultiboard(&g_edicts[1]);
    T_EQ(mb_last_multiboard_frames(), 0);

    /* Revoking advanced control clears the panel again. */
    G_SetPlayerAlliance(&viewer->client->ps, &owner->ps, ALLIANCE_SHARED_ADVANCED_CONTROL, false);
    mb_reset_capture();
    UI_WriteMultiboard(viewer);
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 1);
    T_EQ(mb_last_multiboard_frames(), 0);
    mb_restore_capture(saved);
}

TEST(wc3_multiboard, run_frame_tracks_computer_ally_spending_for_team_resources) {
    edict_t *viewer = &g_edicts[0];
    gameClient_t *owner = &game.clients[1];
    UnitProfile_t profile = { .trains = "hpea" };
    edict_t *barracks;
    mbCaptureSaved_t saved;
    uint32_t gold_before;

    setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    G_SetClientConnected(viewer, true);
    owner->connected = false; /* computer ally: no console of its own */
    barracks = mb_alloc_ai_barracks(owner, &profile);
    mb_grant_advanced(viewer->client, owner, true);
    level.started = level.scriptsStarted = true;

    saved = mb_install_capture();
    /* The alliance grant dirtied the viewer; the scheduler consumes the bit. */
    T_ASSERT(level.multiboard_dirty_clients & 1u);
    globals.RunFrame();
    T_ASSERT(!(level.multiboard_dirty_clients & 1u));
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 1);
    T_ASSERT(mb_text_seen_on(WC3_LAYER_MULTIBOARD, "Team Resources"));
    T_ASSERT(mb_gold_text_seen(owner));

    /* A quiet frame re-sends nothing. */
    mb_reset_capture();
    globals.RunFrame();
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 0);

    /* The computer ally spends gold without any connected resource bar. */
    gold_before = owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    T_ASSERT(SP_TrainUnit(barracks, MAKEFOURCC('h','p','e','a')));
    T_ASSERT(owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] < gold_before);
    mb_reset_capture();
    globals.RunFrame();
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 1);
    T_ASSERT(mb_gold_text_seen(owner));
    T_ASSERT(mb_unicast_target == viewer);
    T_ASSERT(!(level.multiboard_dirty_clients & 1u));
    mb_restore_capture(saved);
}

TEST(wc3_multiboard, run_frame_refreshes_team_resources_in_the_spending_frame) {
    edict_t *viewer = &g_edicts[0];
    edict_t *owner_ent = &g_edicts[1];
    gameClient_t *owner = owner_ent->client;
    mbCaptureSaved_t saved;

    setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    G_SetClientConnected(viewer, true);
    G_SetClientConnected(owner_ent, true);
    owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    mb_grant_advanced(viewer->client, owner, true);
    level.started = level.scriptsStarted = true;

    saved = mb_install_capture();
    /* The first frame authors the owner's console and the ally's panel from
     * the same resource snapshot; a quiet frame then re-sends neither. */
    globals.RunFrame();
    T_EQ(mb_layouts_on(LAYER_CONSOLE), 2); /* viewer and owner consoles */
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 1);
    T_ASSERT(mb_gold_text_seen(owner));
    mb_reset_capture();
    globals.RunFrame();
    T_EQ(mb_layouts_on(LAYER_CONSOLE), 0);
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 0);

    /* A map script changes the owner's gold without touching food or any
     * resource bar.  The owner's console and the ally's panel must both
     * follow in the same frame; the panel must not trail by a tick. */
    mb_restore_capture(saved);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetPlayerState(Player(1), PLAYER_STATE_RESOURCE_GOLD, 850)\n"
        "endfunction\n"));
    T_EQ(owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 850);
    saved = mb_install_capture();
    globals.RunFrame();
    T_EQ(mb_layouts_on(LAYER_CONSOLE), 1);
    T_EQ(mb_layouts_on(WC3_LAYER_MULTIBOARD), 1);
    T_ASSERT(mb_gold_text_seen(owner));
    mb_restore_capture(saved);
}

TEST(wc3_multiboard, team_resources_offset_follows_each_viewers_own_leaderboard) {
    /* A stale shared Height proves the offset is computed per viewer, not
     * read back from whichever leaderboard was authored last. */
    FRAMEDEF root = { .Type = FT_SIMPLEFRAME, .Height = 0.5f };
    FRAMEDEF backdrop = { .Type = FT_BACKDROP };
    FRAMEDEF title = { .Type = FT_STRING };
    FRAMEDEF container = { .Type = FT_SIMPLEFRAME };
    LeaderBoard_t const old_binding = hud.leaderboard;
    leaderboard_t *small, *large;
    float offset_small, offset_large;
    mbCaptureSaved_t saved;

    setup_test_world();
    hud.leaderboard.Leaderboard = &root;
    hud.leaderboard.LeaderboardBackdrop = &backdrop;
    hud.leaderboard.LeaderboardTitle = &title;
    hud.leaderboard.LeaderboardListContainer = &container;
    G_SetClientConnected(&g_edicts[0], true);
    G_SetClientConnected(&g_edicts[1], true);
    small = G_AllocLeaderboard();
    large = G_AllocLeaderboard();
    T_ASSERT(small && large);
    small->item_count = 1;
    large->item_count = 4;
    G_SetPlayerLeaderboard(0, small);
    G_SetPlayerLeaderboard(1, large);
    G_SetLeaderboardDisplayed(small, &game.clients[0].ps, true);
    G_SetLeaderboardDisplayed(large, &game.clients[1].ps, true);

    offset_small = UI_LeaderboardMultiboardOffset(0);
    offset_large = UI_LeaderboardMultiboardOffset(1);
    T_ASSERT(offset_small > 0.0f);
    T_ASSERT(offset_large > offset_small);
    T_ASSERT(offset_small < 0.5f);

    /* Authoring both boards through the scheduler pass neither moves the
     * other viewer's stack nor leaves the panels below them stale. */
    level.multiboard_dirty_clients = 0;
    saved = mb_install_capture();
    G_UpdateLeaderboards();
    mb_restore_capture(saved);
    T_EQ(mb_layouts_on(WC3_LAYER_LEADERBOARD), 2);
    T_FEQ(UI_LeaderboardMultiboardOffset(0), offset_small, 0.00001f);
    T_FEQ(UI_LeaderboardMultiboardOffset(1), offset_large, 0.00001f);
    T_EQ(level.multiboard_dirty_clients & 3u, 3u);

    /* Hidden or absent boards contribute no offset. */
    G_SetLeaderboardDisplayed(small, &game.clients[0].ps, false);
    T_FEQ(UI_LeaderboardMultiboardOffset(0), 0.0f, 0.00001f);
    T_FEQ(UI_LeaderboardMultiboardOffset(2), 0.0f, 0.00001f);

    hud.leaderboard = old_binding;
}

TEST(wc3_api, multiboard_natives_manage_cells_display_and_minimize) {
    player_t *saved = currentplayer;
    multiboard_t *board;
    struct gmultiboardcell_s *cell;
    multiboardItem_t *stale;
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  multiboard mb = null\n"
        "  multiboarditem mi = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set mb = CreateMultiboard()\n"
        "  call BJassAssert(mb != null, \"multiboard handle\")\n"
        "  call MultiboardSetTitleText(mb, \"Kills\")\n"
        "  call MultiboardSetRowCount(mb, 2)\n"
        "  call MultiboardSetColumnCount(mb, 3)\n"
        "  call MultiboardSetItemsStyle(mb, true, false)\n"
        "  call MultiboardSetItemsWidth(mb, 0.08)\n"
        "  set mi = MultiboardGetItem(mb, 0, 1)\n"
        "  call BJassAssert(mi != null, \"item handle\")\n"
        "  call MultiboardSetItemValue(mi, \"12\")\n"
        "  call MultiboardSetItemValueColor(mi, 255, 200, 0, 255)\n"
        "  call MultiboardSetItemWidth(mi, 0.05)\n"
        "  call MultiboardSetItemIcon(mi, \"ReplaceableTextures\\\\CommandButtons\\\\BTNHero.blp\")\n"
        "  call MultiboardSetItemStyle(mi, true, true)\n"
        "  call MultiboardReleaseItem(mi)\n"
        "  call MultiboardDisplay(mb, true)\n"
        "  call MultiboardMinimize(mb, true)\n"
        "  call BJassAssert(IsMultiboardMinimized(mb), \"minimized\")\n"
        "  call MultiboardMinimize(mb, false)\n"
        "  call BJassAssert(not IsMultiboardMinimized(mb), \"restored\")\n"
        "endfunction\n"));

    board = &level.multiboards[0];
    T_ASSERT(board->inuse);
    T_STREQ(board->title, "Kills");
    T_EQ(board->rows, 2);
    T_EQ(board->cols, 3);
    T_ASSERT(board->displayed_clients & 1u);
    cell = G_MultiboardCell(board, 0, 0);
    T_ASSERT(cell);
    T_ASSERT(cell->show_value);
    T_ASSERT(!cell->show_icon);
    T_FEQ(cell->width, 0.08f, 0.0001f);
    cell = G_MultiboardCell(board, 0, 1);
    T_ASSERT(cell);
    T_STREQ(cell->value, "12");
    T_ASSERT(cell->value_color_set);
    T_EQ(cell->value_color.r, 255);
    T_EQ(cell->value_color.g, 200);
    T_FEQ(cell->width, 0.05f, 0.0001f);
    T_ASSERT(cell->show_icon);
    T_ASSERT(strstr(cell->icon, "BTNHero.blp"));

    stale = G_MultiboardGetItem(board, 0, 1);
    T_ASSERT(stale);
    G_FreeMultiboard(board);
    T_ASSERT(stale->board < 0);
    T_NULL(G_MultiboardItemBoard(stale));
    G_MultiboardReleaseItem(stale);

    T_ASSERT(run_test_jass(
        "globals\n"
        "  multiboard mb = null\n"
        "  multiboarditem a = null\n"
        "  multiboarditem b = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set mb = CreateMultiboard()\n"
        "  call MultiboardSetRowCount(mb, 1)\n"
        "  call MultiboardSetColumnCount(mb, 1)\n"
        "  set a = MultiboardGetItem(mb, 0, 0)\n"
        "  set b = MultiboardGetItem(mb, 0, 0)\n"
        "  call BJassAssert(a != null and b != null and a != b, \"distinct item views\")\n"
        "  call MultiboardSetItemValue(a, \"A\")\n"
        "  call MultiboardReleaseItem(a)\n"
        "  call MultiboardReleaseItem(b)\n"
        "  call DestroyMultiboard(mb)\n"
        "  call MultiboardSetItemValue(a, \"gone\")\n"
        "endfunction\n"));
    currentplayer = saved;
}

TEST(wc3_api, multiboard_display_uses_client_slot_for_mapped_player) {
    multiboard_t *board;
    setup_test_world();
    game.clients[0].ps.number = 1;
    game.clients[1].ps.number = 0;
    board = G_AllocMultiboard();
    G_SetMultiboardDisplayed(board, &game.clients[1].ps, true);
    G_SetMultiboardMinimized(board, &game.clients[1].ps, true);
    T_ASSERT(board->displayed_clients & (1u << 1));
    T_ASSERT(!(board->displayed_clients & 1u));
    T_ASSERT(board->minimized_clients & (1u << 1));
    T_ASSERT(G_IsMultiboardDisplayed(board, &game.clients[1].ps));
    T_ASSERT(!G_IsMultiboardDisplayed(board, &game.clients[0].ps));
    T_ASSERT(G_IsMultiboardMinimized(board, &game.clients[1].ps));
}

TEST(wc3_api, multiboard_one_visible_board_per_client_and_suppression) {
    multiboard_t *first, *second;
    player_t *saved = currentplayer;
    setup_test_world();
    first = G_AllocMultiboard();
    second = G_AllocMultiboard();
    T_ASSERT(first && second);
    G_SetMultiboardDisplayed(first, &game.clients[0].ps, true);
    G_SetMultiboardDisplayed(first, &game.clients[1].ps, true);
    T_ASSERT(G_VisibleMultiboard(0) == first);
    T_ASSERT(G_VisibleMultiboard(1) == first);
    G_SetMultiboardDisplayed(second, &game.clients[0].ps, true);
    T_ASSERT(G_VisibleMultiboard(0) == second);
    T_ASSERT(G_VisibleMultiboard(1) == first);
    T_ASSERT(!G_IsMultiboardDisplayed(first, &game.clients[0].ps));
    G_SuppressMultiboardDisplay(&game.clients[0].ps, true);
    T_NULL(G_VisibleMultiboard(0));
    T_ASSERT(G_VisibleMultiboard(1) == first);
    G_SuppressMultiboardDisplay(&game.clients[0].ps, false);
    T_ASSERT(G_VisibleMultiboard(0) == second);
    G_FreeMultiboard(second);
    T_NULL(G_VisibleMultiboard(0));
    T_ASSERT(G_VisibleMultiboard(1) == first);
    currentplayer = saved;
}

TEST(wc3_api, multiboard_display_queries_and_suppress_native) {
    player_t *saved = currentplayer;
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  multiboard mb = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set mb = CreateMultiboard()\n"
        "  call MultiboardDisplay(mb, true)\n"
        "  call BJassAssert(IsMultiboardDisplayed(mb), \"displayed\")\n"
        "  call MultiboardSuppressDisplay(true)\n"
        "  call BJassAssert(IsMultiboardDisplayed(mb), \"suppression keeps requested display\")\n"
        "  call MultiboardSuppressDisplay(false)\n"
        "  call MultiboardDisplay(mb, false)\n"
        "  call BJassAssert(not IsMultiboardDisplayed(mb), \"hidden\")\n"
        "endfunction\n"));
    T_ASSERT(!level.multiboard_suppressed_clients);
    currentplayer = saved;
}

TEST(wc3_api, texttag_natives_store_unit_anchor_and_style) {
    player_t *saved = currentplayer;
    texttag_t *tag;
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  texttag tt = null\n"
        "  unit u = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set u = CreateUnit(Player(0), 'hfoo', 100.0, 200.0, 0.0)\n"
        "  set tt = CreateTextTag()\n"
        "  call BJassAssert(tt != null, \"texttag handle\")\n"
        "  call SetTextTagText(tt, \"+25\", 0.024)\n"
        "  call SetTextTagColor(tt, 255, 220, 0, 255)\n"
        "  call SetTextTagPosUnit(tt, u, 40.0)\n"
        "  call SetTextTagVelocity(tt, 0.0, 0.03)\n"
        "  call SetTextTagVisibility(tt, true)\n"
        "  call SetTextTagPermanent(tt, false)\n"
        "  call SetTextTagLifespan(tt, 2.0)\n"
        "  call SetTextTagFadepoint(tt, 1.0)\n"
        "endfunction\n"));

    T_ASSERT(level.texttags[0].inuse);
    tag = &level.texttags[0];
    T_STREQ(tag->text, "+25");
    T_FEQ(tag->height, 0.024f, 0.0001f);
    T_EQ(tag->color.r, 255);
    T_EQ(tag->color.g, 220);
    T_EQ(tag->color.b, 0);
    T_EQ(tag->color.a, 255);
    T_ASSERT(tag->unit != NULL);
    T_FEQ(tag->height_offset, 40.0f, 0.001f);
    T_FEQ(tag->xvel, 0.0f, 0.0001f);
    T_FEQ(tag->yvel, 0.03f, 0.0001f);
    T_ASSERT(!tag->permanent);
    T_FEQ(tag->lifespan, 2.0f, 0.001f);
    T_FEQ(tag->fadepoint, 1.0f, 0.001f);
    T_ASSERT(G_IsTextTagVisible(tag, NULL));

    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  texttag tt = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set tt = CreateTextTag()\n"
        "  call SetTextTagVisibility(tt, false)\n"
        "  call DestroyTextTag(tt)\n"
        "endfunction\n"));
    T_ASSERT(!level.texttags[1].inuse);
    currentplayer = saved;
}

TEST(wc3_save, texttag_presentation_state_round_trips) {
    PATHSTR filename;
    edict_t *unit;
    texttag_t *tag;

    strlcpy(filename, Test_TempPath("wc3-texttag-presentation-save.bin"), sizeof(filename));
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100.0f, 200.0f);
    tag = G_AllocTextTag();
    T_NOT_NULL(unit);
    T_NOT_NULL(tag);
    if (!unit || !tag) return;
    tag->has_text = true;
    tag->has_position = true;
    tag->generation = 19;
    tag->unit = unit;
    tag->height = 0.024f;
    tag->height_offset = 40.0f;
    tag->lifespan = 2.0f;
    tag->fadepoint = 1.0f;
    strlcpy(tag->text, "150!", sizeof(tag->text));
    T_ASSERT(WriteGame(filename));

    tag->has_text = false;
    tag->has_position = false;
    tag->generation = 0;
    tag->unit = NULL;
    T_ASSERT(ReadGame(filename));

    tag = level.texttags;
    T_ASSERT(tag->inuse);
    T_ASSERT(tag->has_text);
    T_ASSERT(tag->has_position);
    T_EQ(tag->generation, 19u);
    T_STREQ(tag->text, "150!");
    T_NOT_NULL(tag->unit);
    if (tag->unit) T_EQ(tag->unit->class_id, MAKEFOURCC('h','f','o','o'));
    T_FEQ(tag->height, 0.024f, 0.0001f);
    T_FEQ(tag->height_offset, 40.0f, 0.001f);
    remove(filename);
}

TEST(wc3_api, texttag_updates_and_removal_match_client_wire_contract) {
    void (*saved_write)(pfWriteType_t, void const *) = gi.Write;
    void (*saved_multicast)(vec3_t const *, multicast_t) = gi.multicast;
    int (*saved_font)(cstring_t, uint32_t) = gi.FontIndex;
    uint32_t saved_max_clients = game.max_clients;
    uint32_t saved_player_number = game.clients[0].ps.number;
    edict_t *unit;
    texttag_t *tag;

    setup_test_world();
    memset(&texttag_capture, 0, sizeof(texttag_capture));
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100.0f, 200.0f);
    tag = G_AllocTextTag();
    T_NOT_NULL(unit);
    T_NOT_NULL(tag);
    if (!unit || !tag) return;
    game.max_clients = 1;
    game.clients[0].ps.number = 0;
    unit->s.origin.z = 3.0f;
    tag->has_text = true;
    tag->has_position = true;
    tag->generation = 19;
    tag->visible_clients = 1;
    tag->unit = unit;
    tag->height = 0.024f;
    tag->height_offset = 40.0f;
    tag->lifespan = 2.0f;
    tag->fadepoint = 1.0f;
    tag->xvel = 0.03f;
    tag->color = MAKE(color32_t, 255, 0, 0, 255);
    tag->permanent = false;
    strlcpy(tag->text, "150!", sizeof(tag->text));
    gi.Write = texttag_capture_write;
    gi.multicast = texttag_capture_multicast;
    gi.FontIndex = texttag_capture_font;

    G_TextTagPresentation(tag, false);
    T_EQ(texttag_capture.count, 17);
    T_EQ(texttag_capture.types[0], PF_BYTE);
    T_EQ(texttag_capture.integral[0], svc_temp_entity);
    T_EQ(texttag_capture.types[1], PF_BYTE);
    T_EQ(texttag_capture.integral[1], TE_TEXT_TAG);
    T_EQ(texttag_capture.types[2], PF_SHORT);
    T_EQ(texttag_capture.integral[2], 0);
    T_EQ(texttag_capture.types[3], PF_LONG);
    T_EQ((uint32_t)texttag_capture.integral[3], 19u);
    T_EQ(texttag_capture.integral[4], 1);
    T_EQ(texttag_capture.integral[5], 1);
    T_EQ(texttag_capture.types[6], PF_POSITION);
    T_FEQ(texttag_capture.position.x, 100.0f, 0.001f);
    T_FEQ(texttag_capture.position.y, 200.0f, 0.001f);
    T_FEQ(texttag_capture.position.z, 43.0f, 0.001f);
    T_EQ(texttag_capture.types[7], PF_LONG);
    T_EQ(texttag_capture.integral[7], (int32_t)(unit - globals.edicts));
    T_EQ(texttag_capture.types[8], PF_FLOAT);
    T_FEQ(texttag_capture.real[8], 40.0f, 0.001f);
    T_EQ(texttag_capture.types[9], PF_STRING);
    T_STREQ(texttag_capture.text, "150!");
    T_EQ(texttag_capture.types[10], PF_LONG);
    T_EQ((uint32_t)texttag_capture.integral[10], 0xff0000ffu);
    T_EQ(texttag_capture.types[11], PF_SHORT);
    T_EQ(texttag_capture.integral[11], 17);
    T_EQ(texttag_capture.types[12], PF_LONG);
    T_EQ(texttag_capture.integral[12], 2000);
    T_EQ(texttag_capture.types[13], PF_LONG);
    T_EQ(texttag_capture.integral[13], 1000);
    T_EQ(texttag_capture.types[14], PF_FLOAT);
    T_FEQ(texttag_capture.real[14], 0.03f, 0.0001f);
    T_EQ(texttag_capture.types[15], PF_FLOAT);
    T_FEQ(texttag_capture.real[15], 0.0f, 0.0001f);
    T_EQ(texttag_capture.types[16], PF_BYTE);
    T_EQ(texttag_capture.integral[16], 0);
    T_STREQ(texttag_capture.font_name, "Fonts\\FRIZQT__.TTF");
    T_EQ(texttag_capture.font_size, 12);
    T_EQ(texttag_capture.multicast_count, 1);
    T_EQ(texttag_capture.multicast_to, MULTICAST_ALL);
    T_FEQ(texttag_capture.multicast_origin.z, 43.0f, 0.001f);

    texttag_capture.count = 0;
    G_TextTagPresentation(tag, true);
    T_EQ(texttag_capture.count, 5);
    T_EQ(texttag_capture.types[4], PF_BYTE);
    T_EQ(texttag_capture.integral[4], 0);
    T_EQ(texttag_capture.multicast_count, 2);

    gi.Write = saved_write;
    gi.multicast = saved_multicast;
    gi.FontIndex = saved_font;
    game.max_clients = saved_max_clients;
    game.clients[0].ps.number = saved_player_number;
}
#endif
