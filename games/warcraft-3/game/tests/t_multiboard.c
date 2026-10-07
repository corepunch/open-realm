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
#endif
