#ifdef BZ_TESTS
/*
 * test_game.c — Tests for game utilities not covered by other suites.
 *
 * Covered:
 *   G_RegionContains  — point-in-region containment (empty, hit, miss,
 *                       multi-rect, exclusive upper boundary)
 *   G_FreeEdict       — entity lifecycle: inuse cleared, freetime stamped
 *   M_IsDead          — health-based liveness check
 *   compress_stat     — 8-bit health/mana encoding
 *   FindEnumValue     — NULL-terminated string-enum lookup
 *   unit_runwait      — per-frame wait counter and callback dispatch
 *   unit_issuetargetorder — attack and unknown-order paths
 *   unit_learnability — hero ability slot management
 *   Alliance types    — ALLIANCE_SHARED_VISION and independent flags
 *   Player resources  — PLAYERSTATE_RESOURCE_GOLD / LUMBER set/get
 *   Fog of war        — grid sizing, circle reveal, visible/explored decay
 */

#include "test.h"
#include "../g_local.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);


#include "../game/hud/hud_utils.h"
#include "../hud/hud_local.h"
#include "../../../renderer/r_local.h"

/* Forward declarations for internal functions not exposed in any header. */
BOOL  M_IsDead(LPEDICT ent);
DWORD FindEnumValue(LPCSTR value, LPCSTR values[]);
void  unit_runwait(LPEDICT self, void (*callback)(LPEDICT));

/* =========================================================================
 * Helpers
 * ========================================================================= */

static LPPLAYER game_player(int idx) {
    game.clients[idx].ps.number = (DWORD)idx;
    return &game.clients[idx].ps;
}

static LPEDICT make_test_unit(void) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    ent->health.value     = 250.0f;
    ent->health.max_value = 250.0f;
    ent->stand            = unit_stand;
    ent->movetype         = MOVETYPE_STEP;
    unit_stand(ent);
    return ent;
}

/* =========================================================================
 * HUD frame numbering
 * ========================================================================= */

TEST(wc3_game, hud_proxy_number_advances_past_fdf_frame) {
    T_EQ(UI_NextProxyFrameNumber(1, 10), 11);
}

TEST(wc3_game, hud_proxy_number_never_moves_backwards) {
    T_EQ(UI_NextProxyFrameNumber(12, 10), 12);
}

static void test_text_exact_width_fits(void) { T_ASSERT(R_TextFitsWidth(0.0f)); }
static void test_text_subpixel_residue_fits(void) { T_ASSERT(R_TextFitsWidth(-0.0000005f)); }
static void test_text_real_overflow_does_not_fit(void) { T_ASSERT(!R_TextFitsWidth(-0.00001f)); }
TEST(wc3_game, hud_stale_attribute_texture_uses_infocard_asset) {
    T_STREQ(UI_ResolveTextureAlias("HeroStrengthIcon"),
                  "UI\\Widgets\\Console\\Human\\infocard-heroattributes-str.blp");
}
TEST(wc3_game, hud_valid_texture_path_is_unchanged) {
    T_STREQ(UI_ResolveTextureAlias("UI\\Feedback\\Resources\\ResourceGold.blp"),
                  "UI\\Feedback\\Resources\\ResourceGold.blp");
}
static void test_hud_second_attack_present_with_dice(void) { T_ASSERT(UI_HasSecondAttack(1)); }
static void test_hud_second_attack_absent_without_dice(void) { T_ASSERT(!UI_HasSecondAttack(0)); }
TEST(wc3_game, hud_portrait_model_uses_serialized_field) {
    FRAMEDEF frame = { 0 };
    UI_SetPortraitFrameModel(&frame, 42);
    T_EQ(frame.Type, FT_PORTRAIT);
    T_EQ(frame.Portrait.model, 42);
}

TEST(wc3_game, hud_authored_row_keeps_template_size) {
    FRAMEDEF tmpl = { .Type = FT_FRAME, .Width = 0.08f, .Height = 0.033f };
    FRAMEDEF parent = { .Type = FT_FRAME };
    LPFRAMEDEF row = UI_CloneStackedRow(&tmpl, &parent, 0);
    T_NOT_NULL(row);
    T_FEQ(row->Width, 0.08f, 0.001f);
    T_FEQ(row->Height, 0.033f, 0.001f);
    T_FEQ(row->Points.y[FPP_MIN].offset, 0.0f, 0.001f);
}

TEST(wc3_game, hud_authored_row_stride_uses_template_height) {
    FRAMEDEF tmpl = { .Type = FT_FRAME, .Width = 0.15f, .Height = 0.012f };
    FRAMEDEF parent = { .Type = FT_FRAME };
    LPFRAMEDEF row = UI_CloneStackedRow(&tmpl, &parent, 3);
    T_NOT_NULL(row);
    T_ASSERT(row->Points.y[FPP_MIN].relativeTo == &parent);
    T_FEQ(row->Points.y[FPP_MIN].offset, -0.036f, 0.001f);
}

TEST(wc3_game, hud_quest_rows_bind_authored_children) {
    QUEST quest = { .title = "Test Quest", .discovered = true, .required = true };
    QUESTITEM item = { .description = "Test Objective" };
    LPFRAMEDEF list, item_list, button, title, item_title;

    UI_ClearTemplates();
    quest_row_template = UI_Spawn(FT_FRAME, NULL);
    snprintf(quest_row_template->Name, sizeof(quest_row_template->Name), "QuestListItem");
    UI_SetSize(quest_row_template, 0.08f, 0.033f);
    button = UI_Spawn(FT_GLUEBUTTON, quest_row_template);
    snprintf(button->Name, sizeof(button->Name), "QuestListItemButton");
    title = UI_Spawn(FT_TEXT, quest_row_template);
    snprintf(title->Name, sizeof(title->Name), "QuestListItemTitle");
    quest_item_template = UI_Spawn(FT_FRAME, NULL);
    snprintf(quest_item_template->Name, sizeof(quest_item_template->Name), "QuestItemListItem");
    UI_SetSize(quest_item_template, 0.15f, 0.012f);
    item_title = UI_Spawn(FT_TEXT, quest_item_template);
    snprintf(item_title->Name, sizeof(item_title->Name), "QuestItemListItemTitle");
    list = UI_Spawn(FT_FRAME, NULL);
    item_list = UI_Spawn(FT_FRAME, NULL);
    quest.items = &item;
    level.quests = &quest;

    PopulateQuestList(list, true, &quest);
    PopulateQuestItems(item_list, &quest);
    title = UI_FindChildFrame(list, "QuestListItemTitle");
    button = UI_FindChildFrame(list, "QuestListItemButton");
    item_title = UI_FindChildFrame(item_list, "QuestItemListItemTitle");
    T_NOT_NULL(title);
    T_NOT_NULL(button);
    T_NOT_NULL(item_title);
    T_FEQ(title->Parent->Width, 0.08f, 0.001f);
    T_FEQ(item_title->Parent->Height, 0.012f, 0.001f);
    T_STREQ(title->Text, "> Test Quest");
    T_STREQ(button->OnClick, "quest 0");
    T_STREQ(item_title->Text, "- Test Objective");

    level.quests = NULL;
    quest_row_template = quest_item_template = NULL;
    quests_loaded = false;
    memset(&qd, 0, sizeof(qd));
    UI_ClearTemplates();
}

TEST(wc3_game, hud_message_overlay_loads_authored_geometry) {
    message_loaded = false;
    memset(&msg, 0, sizeof(msg));
    T_ASSERT(MessageEnsureLoaded());
    T_FEQ(msg.OpenWarcraftMessageText->Width, 0.30f, 0.001f);
    T_FEQ(msg.OpenWarcraftMessageText->Height, 0.145f, 0.001f);
    T_FEQ(msg.OpenWarcraftMessageText->Font.Size, 0.010f, 0.001f);
    T_FEQ(msg.OpenWarcraftMessageText->Points.x[FPP_MIN].offset, 0.05f, 0.001f);
    T_FEQ(msg.OpenWarcraftMessageText->Points.y[FPP_MIN].offset, -0.30f, 0.001f);
}

TEST(wc3_game, hud_message_overlay_position_is_runtime_data) {
    VECTOR2 pos = { 0.20f, 0.10f };
    FRAMEDEF frame = MessageFrame(&pos, "Runtime message");
    T_FEQ(frame.Width, 0.30f, 0.001f);
    T_FEQ(frame.Height, 0.145f, 0.001f);
    T_FEQ(frame.Points.x[FPP_MIN].offset, 0.20f, 0.001f);
    T_FEQ(frame.Points.y[FPP_MIN].offset, -0.10f, 0.001f);
    T_STREQ(frame.Text, "Runtime message");
}

TEST(wc3_game, hud_message_overlay_invalid_position_keeps_fdf_anchor) {
    VECTOR2 pos = { -1.0f, UI_BASE_HEIGHT + 1.0f };
    FRAMEDEF frame = MessageFrame(&pos, "Authored position");
    T_FEQ(frame.Points.x[FPP_MIN].offset, 0.05f, 0.001f);
    T_FEQ(frame.Points.y[FPP_MIN].offset, -0.30f, 0.001f);
}

TEST(wc3_game, hud_build_timer_stays_inside_info_panel) {
    T_ASSERT(BUILDQUEUE_TIMER_X + BUILDQUEUE_TIMER_W <= INFO_PANEL_X + INFO_PANEL_W);
}

TEST(wc3_game, hud_portraits_align_at_info_panel_top) {
    T_ASSERT(BUILDQUEUE_FIRST_Y < INFO_PANEL_Y + 0.0390f);
    T_ASSERT(PORTRAIT_Y <= INFO_PANEL_Y);
}

TEST(wc3_game, overhead_health_moves_above_single_bar_slot) {
    VECTOR2 const bars = R_StatusBarOffsets(8.0f, false);
    T_FEQ(bars.x, -8.0f, 0.01f);
    T_FEQ(bars.y, 0.0f, 0.01f);
}

TEST(wc3_game, overhead_mana_takes_lower_slot_and_pushes_health) {
    VECTOR2 const bars = R_StatusBarOffsets(8.0f, true);
    T_ASSERT(bars.x < -16.0f);
    T_FEQ(bars.y, 0.0f, 0.01f);
}

/* =========================================================================
 * G_RegionContains
 * ========================================================================= */

TEST(wc3_game, region_contains_empty_region_false) {
    REGION r = { .num_rects = 0 };
    VECTOR2 p = { 5.0f, 5.0f };
    T_ASSERT(!G_RegionContains(&r, &p));
}

TEST(wc3_game, region_contains_point_inside) {
    REGION r = {
        .rects[0] = { { 0.0f, 0.0f }, { 100.0f, 100.0f } },
        .num_rects = 1
    };
    VECTOR2 p = { 50.0f, 50.0f };
    T_ASSERT(G_RegionContains(&r, &p));
}

TEST(wc3_game, region_contains_point_outside) {
    REGION r = {
        .rects[0] = { { 0.0f, 0.0f }, { 100.0f, 100.0f } },
        .num_rects = 1
    };
    VECTOR2 p = { 200.0f, 200.0f };
    T_ASSERT(!G_RegionContains(&r, &p));
}

TEST(wc3_game, region_contains_multirect_hits_second) {
    /* Two non-overlapping rects; the point is in the second one. */
    REGION r = {
        .rects[0] = { {   0.0f,   0.0f }, {  50.0f,  50.0f } },
        .rects[1] = { { 200.0f, 200.0f }, { 300.0f, 300.0f } },
        .num_rects = 2
    };
    VECTOR2 p = { 250.0f, 250.0f };
    T_ASSERT(G_RegionContains(&r, &p));
}

TEST(wc3_game, region_contains_max_boundary_exclusive) {
    /* Box2_containsPoint uses x < max.x (exclusive upper bound). */
    REGION r = {
        .rects[0] = { { 0.0f, 0.0f }, { 100.0f, 100.0f } },
        .num_rects = 1
    };
    VECTOR2 p = { 100.0f, 50.0f };   /* exactly at max.x */
    T_ASSERT(!G_RegionContains(&r, &p));
}

/* =========================================================================
 * G_FreeEdict
 * ========================================================================= */

TEST(wc3_game, free_edict_clears_inuse) {
    LPEDICT ent = make_test_unit();
    T_ASSERT(ent->inuse);
    G_FreeEdict(ent);
    T_ASSERT(!ent->inuse);
}

TEST(wc3_game, free_edict_stamps_freetime) {
    LPEDICT ent = make_test_unit();
    level.time = 9876;
    G_FreeEdict(ent);
    T_EQ((int)ent->freetime, 9876);
}

/* =========================================================================
 * M_IsDead
 * ========================================================================= */

TEST(wc3_game, is_dead_alive_unit_false) {
    LPEDICT ent = make_test_unit();
    ent->health.value = 100.0f;
    T_ASSERT(!M_IsDead(ent));
}

TEST(wc3_game, is_dead_zero_hp_true) {
    LPEDICT ent = make_test_unit();
    ent->health.value = 0.0f;
    T_ASSERT(M_IsDead(ent));
}

TEST(wc3_game, is_dead_negative_hp_true) {
    LPEDICT ent = make_test_unit();
    ent->health.value = -1.0f;
    T_ASSERT(M_IsDead(ent));
}

/* =========================================================================
 * compress_stat
 * ========================================================================= */

TEST(wc3_game, compress_stat_full_health_is_255) {
    EDICTSTAT s = { 250.0f, 250.0f };
    T_EQ((int)compress_stat(&s), 255);
}

TEST(wc3_game, compress_stat_zero_health_is_0) {
    EDICTSTAT s = { 0.0f, 250.0f };
    T_EQ((int)compress_stat(&s), 0);
}

TEST(wc3_game, compress_stat_half_health) {
    EDICTSTAT s = { 125.0f, 250.0f };
    /* 255 * 125 / 250 = 127 (integer truncation). */
    T_EQ((int)compress_stat(&s), 127);
}

TEST(wc3_game, compress_stat_zero_max_is_0) {
    EDICTSTAT s = { 0.0f, 0.0f };
    T_EQ((int)compress_stat(&s), 0);
}

/* =========================================================================
 * FindEnumValue
 * ========================================================================= */

static LPCSTR test_attack_types[] = {
    "none", "normal", "pierce", "siege", "chaos", NULL
};

TEST(wc3_game, find_enum_first_value) {
    T_EQ((int)FindEnumValue("none", test_attack_types), 0);
}

TEST(wc3_game, find_enum_later_value) {
    T_EQ((int)FindEnumValue("pierce", test_attack_types), 2);
}

TEST(wc3_game, find_enum_null_input_returns_0) {
    T_EQ((int)FindEnumValue(NULL, test_attack_types), 0);
}

TEST(wc3_game, find_enum_unknown_returns_0) {
    T_EQ((int)FindEnumValue("magic", test_attack_types), 0);
}

/* =========================================================================
 * unit_runwait
 * ========================================================================= */

static int _runwait_cb_count = 0;

static void runwait_cb(LPEDICT ent) {
    (void)ent;
    _runwait_cb_count++;
}

TEST(wc3_game, runwait_zero_wait_no_callback) {
    LPEDICT ent = make_test_unit();
    ent->wait = 0.0f;
    _runwait_cb_count = 0;
    unit_runwait(ent, runwait_cb);
    T_EQ(_runwait_cb_count, 0);
}

TEST(wc3_game, runwait_large_wait_decrements) {
    /* FRAMETIME = 100 ms → FRAMETIME/1000.f = 0.1 s. */
    LPEDICT ent = make_test_unit();
    ent->wait = 1.0f;
    _runwait_cb_count = 0;
    unit_runwait(ent, runwait_cb);
    /* wait should decrease by 0.1. */
    T_FEQ(ent->wait, 0.9f, 0.01f);
    T_EQ(_runwait_cb_count, 0);
}

TEST(wc3_game, runwait_small_wait_triggers_callback) {
    /* wait == 0.05 < FRAMETIME/1000.f (0.1) → callback fires. */
    LPEDICT ent = make_test_unit();
    ent->wait = 0.05f;
    _runwait_cb_count = 0;
    unit_runwait(ent, runwait_cb);
    T_EQ(_runwait_cb_count, 1);
    T_FEQ(ent->wait, 0.0f, 0.0001f);
}

/* =========================================================================
 * unit_issuetargetorder
 * ========================================================================= */

TEST(wc3_game, issuetargetorder_attack_returns_true) {
    LPEDICT unit   = make_test_unit();
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50.0f, 0.0f);
    /* order_attack is the real implementation from s_attack.c — just verify return value. */
    BOOL result = unit_issuetargetorder(unit, "attack", target);
    T_ASSERT(result);
}

TEST(wc3_game, issuetargetorder_unknown_returns_false) {
    LPEDICT unit   = make_test_unit();
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50.0f, 0.0f);
    BOOL result = unit_issuetargetorder(unit, "heal", target);
    T_ASSERT(!result);
}

/* =========================================================================
 * unit_learnability
 * ========================================================================= */

TEST(wc3_game, learnability_first_ability_fills_slot0) {
    LPEDICT ent = make_test_unit();
    DWORD code = MAKEFOURCC('A','H','b','z');
    unit_learnability(ent, code);
    T_EQ((int)ent->heroabilities[0].code,  (int)code);
    T_EQ((int)ent->heroabilities[0].level, 1);
}

TEST(wc3_game, learnability_same_code_increments_level) {
    LPEDICT ent = make_test_unit();
    DWORD code = MAKEFOURCC('A','H','b','z');
    unit_learnability(ent, code);
    unit_learnability(ent, code);
    T_EQ((int)ent->heroabilities[0].level, 2);
    /* Should still be in slot 0, not duplicated in slot 1. */
    T_EQ((int)ent->heroabilities[1].code, 0);
}

TEST(wc3_game, learnability_different_codes_fill_consecutive_slots) {
    LPEDICT ent = make_test_unit();
    DWORD code1 = MAKEFOURCC('A','H','b','z');
    DWORD code2 = MAKEFOURCC('A','H','t','b');
    unit_learnability(ent, code1);
    unit_learnability(ent, code2);
    T_EQ((int)ent->heroabilities[0].code, (int)code1);
    T_EQ((int)ent->heroabilities[1].code, (int)code2);
    T_EQ((int)ent->heroabilities[1].level, 1);
}

/* =========================================================================
 * Alliance type variations
 * ========================================================================= */

TEST(wc3_game, alliance_shared_vision_set_get) {
    LPPLAYER p0 = game_player(0);
    LPPLAYER p1 = game_player(1);
    /* Clear alliance table. */
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION));
}

TEST(wc3_game, alliance_shared_vision_does_not_set_passive) {
    LPPLAYER p0 = game_player(0);
    LPPLAYER p1 = game_player(1);
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, true);
    /* Setting SHARED_VISION must not accidentally set PASSIVE. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_game, alliance_multiple_types_independent) {
    LPPLAYER p0 = game_player(0);
    LPPLAYER p1 = game_player(1);
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE,       true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION));
}

TEST(wc3_game, alliance_revoke_one_type_keeps_other) {
    LPPLAYER p0 = game_player(0);
    LPPLAYER p1 = game_player(1);
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE,       true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION, false);
    T_ASSERT( G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_SHARED_VISION));
}

/* =========================================================================
 * Player resource stats — GOLD and LUMBER
 * ========================================================================= */

TEST(wc3_game, player_gold_default_zero) {
    LPPLAYER p = game_player(0);
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], 0);
}

TEST(wc3_game, player_gold_set_get) {
    LPPLAYER p = game_player(0);
    p->stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD], 500);
}

TEST(wc3_game, player_lumber_set_get) {
    LPPLAYER p = game_player(0);
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 200;
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_LUMBER], 200);
}

TEST(wc3_game, player_gold_lumber_independent) {
    LPPLAYER p = game_player(1);
    p->stats[PLAYERSTATE_RESOURCE_GOLD]   = 300;
    p->stats[PLAYERSTATE_RESOURCE_LUMBER] = 150;
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_GOLD],   300);
    T_EQ((int)p->stats[PLAYERSTATE_RESOURCE_LUMBER], 150);
}

/* =========================================================================
 * Fog of war
 * ========================================================================= */

TEST(wc3_game, fow_grid_uses_two_by_two_cells_per_tile) {
    G_FowInit();
    T_EQ(level.fow.width, 8);
    T_EQ(level.fow.height, 6);
    T_EQ(G_FowWorldToCellX(0.0f), 0);
    T_EQ(G_FowWorldToCellX(63.0f), 0);
    T_EQ(G_FowWorldToCellX(64.0f), 1);
    T_EQ(G_FowWorldToCellY(128.0f), 2);
    G_FowShutdown();
}

TEST(wc3_game, fow_revealer_marks_visible_and_explored) {
    reset_entities();
    G_FowInit();

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    revealer->s.player = 0;
    revealer->balance.sight_radius.day = 128.0f;
    revealer->health.value = 1.0f;
    revealer->health.max_value = 1.0f;

    G_FowUpdate();
    DWORD index = G_FowWorldToCellY(64.0f) * level.fow.width + G_FowWorldToCellX(64.0f);
    T_ASSERT(level.fow.players[0].visible[index]);
    T_ASSERT(level.fow.players[0].explored[index]);
    G_FowShutdown();
}

TEST(wc3_game, fow_visible_clears_but_explored_remains) {
    reset_entities();
    G_FowInit();

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    revealer->s.player = 0;
    revealer->balance.sight_radius.day = 128.0f;
    revealer->health.value = 1.0f;
    revealer->health.max_value = 1.0f;

    G_FowUpdate();
    DWORD index = G_FowWorldToCellY(64.0f) * level.fow.width + G_FowWorldToCellX(64.0f);
    revealer->s.renderfx |= RF_HIDDEN;
    G_FowUpdate();

    T_ASSERT(!level.fow.players[0].visible[index]);
    T_ASSERT(level.fow.players[0].explored[index]);
    G_FowShutdown();
}

TEST(wc3_game, fow_blocker_stops_visibility_behind_it) {
    reset_entities();
    G_FowInit();

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 96.0f, 96.0f);
    revealer->s.player = 0;
    revealer->balance.sight_radius.day = 256.0f;
    revealer->health.value = 1.0f;
    revealer->health.max_value = 1.0f;

    LPEDICT blocker = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 160.0f, 96.0f);
    blocker->s.flags |= EF_FOW_BLOCKER;
    blocker->health.value = 1.0f;
    blocker->health.max_value = 1.0f;

    G_FowUpdate();

    DWORD blocker_index = G_FowWorldToCellY(96.0f) * level.fow.width + G_FowWorldToCellX(160.0f);
    /* Trees without a path texture dilate one cell for their canopy; test the
     * first cell behind that occluder, not a cell that is part of its visible rim. */
    DWORD behind_index = G_FowWorldToCellY(96.0f) * level.fow.width + G_FowWorldToCellX(288.0f);
    T_ASSERT(level.fow.players[0].visible[blocker_index]);
    T_ASSERT(!level.fow.players[0].visible[behind_index]);
    G_FowShutdown();
}

static pathTex_t *make_fow_pathtex(DWORD width, DWORD height, BYTE blocked) {
    pathTex_t *tex = gi.MemAlloc(sizeof(*tex) + width * height * sizeof(COLOR32));

    T_ASSERT(tex != NULL);
    tex->width = (WORD)width;
    tex->height = (WORD)height;
    FOR_LOOP(i, width * height) {
        tex->map[i] = (COLOR32){ 0, 0, blocked, 255 };
    }
    return tex;
}

TEST(wc3_game, fow_tree_pathtex_closes_gap_behind_canopy) {
    reset_entities();
    G_FowInit();

    LPEDICT revealer = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32.0f, 128.0f);
    revealer->s.player = 0;
    revealer->balance.sight_radius.day = 320.0f;
    revealer->health.value = 1.0f;
    revealer->health.max_value = 1.0f;

    LPEDICT tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 128.0f, 128.0f);
    tree->s.flags |= EF_FOW_BLOCKER;
    tree->targtype = TARG_TREE;
    tree->s.scale = 1.0f;
    tree->pathtex = make_fow_pathtex(4, 4, 1);
    tree->health.value = 1.0f;
    tree->health.max_value = 1.0f;

    G_FowUpdate();

    DWORD canopy_index = G_FowWorldToCellY(128.0f) * level.fow.width + G_FowWorldToCellX(192.0f);
    DWORD behind_index = G_FowWorldToCellY(128.0f) * level.fow.width + G_FowWorldToCellX(256.0f);
    T_ASSERT(level.fow.blocked[canopy_index]);
    T_ASSERT(level.fow.players[0].visible[canopy_index]);
    T_ASSERT(!level.fow.players[0].visible[behind_index]);
    G_FowShutdown();
}

TEST(wc3_game, fow_full_sync_marks_player_connected) {
    reset_entities();
    G_FowInit();

    LPEDICT clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->client->ps.number = 0;

    T_ASSERT(!level.fow.players[0].client_connected);
    G_FowSendFull(clent);
    T_ASSERT(level.fow.players[0].client_connected);
    T_ASSERT(!level.fow.players[1].client_connected);
    G_FowShutdown();
}

/* =========================================================================
 * Suite runner
 * ========================================================================= */

#endif /* BZ_TESTS */
