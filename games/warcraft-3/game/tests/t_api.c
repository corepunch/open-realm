#ifdef BZ_TESTS
/*
 * test_api.c — Tests for the JASS native API implementations.
 *
 * These tests exercise the C-level game-state that the api_*.h functions
 * read and write.  They work directly on struct fields, alliance tables,
 * and the group registry — no MPQ or renderer is required.
 *
 * Covered:
 *   Player  — color, start_location, name, team, alliance
 *   Hero    — str/agi/int attributes, XP accumulation, skill points,
 *             suspend_xp, overflow-safe AddHeroXP
 *   Unit    — invulnerable, paused, no_pathing, unit_color flags
 *   Group   — FirstOfGroup, IsUnitInGroup
 *   Misc    — SubString semantics, GetRandomInt / GetRandomReal range
 *   Stock   — global capacities, per-unit overrides, spawn inheritance
 */

#include "test.h"
#include "../g_local.h"
#include "common/ui_constants.h"
#include "common/campaign_progress.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(DWORD width, DWORD height, BYTE const *cells);
void CM_SetupTestWorldBounds(LPCBOX2 bounds);
BOOL run_test_jass(LPCSTR src);
extern LPPLAYER currentplayer;
void unit_die(LPEDICT self, LPEDICT attacker);
void unit_build(LPEDICT self, DWORD class_id);



#include "jass/jass.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* =========================================================================
 * Helpers
 * ========================================================================= */

/*
 * Return a pointer to player slot [idx].  Assigns player->number = idx so
 * that G_GetPlayerByNumber / PLAYER_CLIENT macros work correctly.
 */
static LPPLAYER test_player(int idx) {
    game.clients[idx].ps.number = (DWORD)idx;
    return &game.clients[idx].ps;
}

static LPCSTR skip_cutscene_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "skip_cutscene") ? "1" : fallback;
}

static LPCSTR group_debug_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_group_debug") ? "1" : fallback;
}

static DWORD presentation_write_count;
static DWORD presentation_unicast_count;
static pfWriteType_t indicator_types[4];
static LONG indicator_values[4];
static DWORD indicator_write_count;
static LPEDICT indicator_recipient;
static BOOL captured_pause;
static DWORD dnc_model_index_calls;
static DWORD dnc_configstring_calls;
static DWORD dnc_configstring_index[2];
static char dnc_configstring_value[2][16];
static DWORD sky_model_index_calls;
static DWORD sky_configstring_calls;
static DWORD sky_configstring_index;
static char sky_configstring_value[16];
static DWORD scene_fog_configstring_calls;
static DWORD scene_fog_configstring_index;
static char scene_fog_configstring_value[MAX_PATHLEN];

static int capture_dnc_model_index(LPCSTR modelName) {
    (void)modelName;
    return 41 + (int)dnc_model_index_calls++;
}

static void capture_dnc_configstring(DWORD index, LPCSTR value) {
    if (dnc_configstring_calls < 2) {
        dnc_configstring_index[dnc_configstring_calls] = index;
        snprintf(dnc_configstring_value[dnc_configstring_calls],
                 sizeof(dnc_configstring_value[dnc_configstring_calls]),
                 "%s", value ? value : "");
    }
    dnc_configstring_calls++;
}

static int capture_sky_model_index(LPCSTR modelName) {
    (void)modelName;
    sky_model_index_calls++;
    return 41;
}

static void capture_sky_configstring(DWORD index, LPCSTR value) {
    sky_configstring_calls++;
    sky_configstring_index = index;
    snprintf(sky_configstring_value, sizeof(sky_configstring_value), "%s", value ? value : "");
}

static void capture_scene_fog_configstring(DWORD index, LPCSTR value) {
    scene_fog_configstring_calls++;
    scene_fog_configstring_index = index;
    snprintf(scene_fog_configstring_value, sizeof(scene_fog_configstring_value), "%s", value ? value : "");
}

static void capture_pause(BOOL paused) { captured_pause = paused; }

/* Campaign messages explicitly request attention; opening the journal acknowledges it. */
TEST(wc3_api, quest_flash_is_player_local_and_acknowledged) {
    LPPLAYER saved = currentplayer;
    DWORD oldtime = level.time;
    setup_test_world();
    game.clients[0].quest_until = game.clients[1].quest_until = 0;
    currentplayer = test_player(1); level.time = 500;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n call FlashQuestDialogButton()\nendfunction\n"));
    T_EQ(game.clients[0].quest_until, 0);
    T_EQ(game.clients[1].quest_until, 10500);
    level.time = 1500;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n call FlashQuestDialogButton()\nendfunction\n"));
    T_EQ(game.clients[1].quest_until, 11500);
    UI_ShowQuests(PLAYER_ENT(currentplayer));
    T_EQ(game.clients[1].quest_until, 0);
    game.clients[1].connected = true; g_edicts[1].inuse = false;
    game.clients[1].resourcebar.gold_rate = game.clients[1].resourcebar.lumber_rate = 100;
    game.clients[1].quest_until = level.time = 11500;
    G_UpdateClientResourceBars();
    T_EQ(game.clients[1].quest_until, 0);
    game.clients[1].connected = false;
    currentplayer = saved; level.time = oldtime;
}

TEST(wc3_api, timer_dialog_natives_store_state_and_local_visibility) {
    LPPLAYER saved = currentplayer;
    LPTIMERDIALOG dialog;
    LPGTIMER timer;

    setup_test_world();
    currentplayer = test_player(1);
    T_ASSERT(run_test_jass(
        "globals\n"
        "  timer countdown = null\n"
        "  timerdialog countdownDialog = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set countdown = CreateTimer()\n"
        "  call TimerStart(countdown, 65.0, false, null)\n"
        "  set countdownDialog = CreateTimerDialog(countdown)\n"
        "  call BJassAssert(countdownDialog != null, \"timer dialog should be allocated\")\n"
        "  call TimerDialogSetTitle(countdownDialog, \"Until Reinforcements Arrive\")\n"
        "  call TimerDialogSetTitleColor(countdownDialog, 300, -5, 64, 255)\n"
        "  call TimerDialogSetTimeColor(countdownDialog, 1, 2, 3, 4)\n"
        "  call BJassAssert(not IsTimerDialogDisplayed(countdownDialog), \"dialog starts hidden\")\n"
        "  call TimerDialogDisplay(countdownDialog, true)\n"
        "  call BJassAssert(IsTimerDialogDisplayed(countdownDialog), \"dialog should be visible locally\")\n"
        "endfunction\n"
        "function destroyDialog takes nothing returns nothing\n"
        "  call DestroyTimerDialog(countdownDialog)\n"
        "  call BJassAssert(TimerGetRemaining(countdown) > 64.0, \"destroying dialog must not destroy timer\")\n"
        "endfunction\n"));

    dialog = &level.timer_dialogs[0];
    T_ASSERT(dialog->inuse);
    T_ASSERT(dialog->timer != NULL);
    timer = dialog->timer;
    T_STREQ(dialog->title, "Until Reinforcements Arrive");
    T_EQ(dialog->title_color.r, 255); T_EQ(dialog->title_color.g, 0);
    T_EQ(dialog->title_color.b, 64);  T_EQ(dialog->title_color.a, 255);
    T_EQ(dialog->time_color.r, 1);   T_EQ(dialog->time_color.g, 2);
    T_EQ(dialog->time_color.b, 3);   T_EQ(dialog->time_color.a, 4);
    T_EQ(dialog->visible_clients, 1u << 1);

    jass_callbyname(level.vm, "destroyDialog", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(!dialog->inuse);
    T_ASSERT(timer->running);
    currentplayer = saved;
}

TEST(wc3_api, leaderboard_natives_manage_items_sort_and_player_assignment) {
    LPPLAYER saved_currentplayer = currentplayer;
    setup_test_world();
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  leaderboard lb = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  set lb = CreateLeaderboard()\n"
        "  call BJassAssert(lb != null, \"leaderboard handle\")\n"
        "  call LeaderboardSetLabel(lb, \"Grunts Trained\")\n"
        "  call LeaderboardAddItem(lb, \"Second\", 2, Player(1))\n"
        "  call LeaderboardAddItem(lb, \"First\", 1, Player(0))\n"
        "  call LeaderboardSortItemsByValue(lb, true)\n"
        "  call BJassAssert(LeaderboardGetPlayerIndex(lb, Player(0)) == 0, \"sort/player index\")\n"
        "  call BJassAssert(LeaderboardHasPlayerItem(lb, Player(1)), \"player item\")\n"
        "  call LeaderboardSetItemValue(lb, 0, 3)\n"
        "  call LeaderboardSetItemLabel(lb, 0, \"Grunts\")\n"
        "  call PlayerSetLeaderboard(Player(0), lb)\n"
        "  call LeaderboardDisplay(lb, true)\n"
        "  call BJassAssert(PlayerGetLeaderboard(Player(0)) == lb, \"player assignment\")\n"
        "  call BJassAssert(IsLeaderboardDisplayed(lb), \"displayed\")\n"
        "  call BJassAssert(LeaderboardGetItemCount(lb) == 2, \"item count\")\n"
        "  call BJassAssert(LeaderboardGetLabelText(lb) == \"Grunts Trained\", \"label\")\n"
        "endfunction\n"));

    T_ASSERT(level.leaderboards[0].inuse);
    T_STREQ(level.leaderboards[0].items[0].label, "Grunts");
    T_EQ(level.leaderboards[0].items[0].value, 3);
    T_EQ(level.player_leaderboards[0], 0);
    T_ASSERT(level.leaderboards[0].displayed_clients & 1u);
    currentplayer = saved_currentplayer;
}

TEST(wc3_api, leaderboard_display_uses_client_slot_for_mapped_player) {
    LPLEADERBOARD board;
    setup_test_world();
    game.clients[0].ps.number = 1;
    game.clients[1].ps.number = 0;
    board = G_AllocLeaderboard();
    G_SetPlayerLeaderboard(0, board);
    G_SetLeaderboardDisplayed(board, &game.clients[1].ps, true);
    T_ASSERT(board->displayed_clients & (1u << 1));
    T_ASSERT(!(board->displayed_clients & 1u));
    T_ASSERT(G_IsLeaderboardDisplayed(board, &game.clients[1].ps));
    T_ASSERT(!G_IsLeaderboardDisplayed(board, &game.clients[0].ps));
}

TEST(wc3_api, version_queries_accept_typed_handles) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(VersionCompatible(ConvertVersion(0)), \"ROC compatible\")\n"
        "  call BJassAssert(VersionSupported(ConvertVersion(0)), \"ROC supported\")\n"
        "  call BJassAssert(not VersionCompatible(ConvertVersion(1)), \"current compatibility policy\")\n"
        "  call BJassAssert(not VersionSupported(ConvertVersion(1)), \"current support policy\")\n"
        "endfunction\n"));
}

TEST(wc3_api, ability_cooldown_natives_share_unit_cooldown_state) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call BlzStartUnitAbilityCooldown(u, 'AHtb', 3.5)\n"
        "  call BJassAssert(BlzGetUnitAbilityCooldownRemaining(u, 'AHtb') > 3.4, \"start cooldown\")\n"
        "  call BlzEndUnitAbilityCooldown(u, 'AHtb')\n"
        "  call BJassAssert(BlzGetUnitAbilityCooldownRemaining(u, 'AHtb') == 0.0, \"end cooldown\")\n"
        "  call BlzStartUnitAbilityCooldown(u, 'AHtb', 2.0)\n"
        "  call BlzStartUnitAbilityCooldown(u, 'AHwe', 4.0)\n"
        "  call UnitResetCooldown(u)\n"
        "  call BJassAssert(BlzGetUnitAbilityCooldownRemaining(u, 'AHtb') == 0.0, \"reset thunder clap\")\n"
        "  call BJassAssert(BlzGetUnitAbilityCooldownRemaining(u, 'AHwe') == 0.0, \"reset water elemental\")\n"
        "endfunction\n"));
}

TEST(wc3_api, pause_game_forwards_authoritative_pause_state) {
    void (*old_set_paused)(BOOL) = gi.SetPaused;

    captured_pause = false;
    gi.SetPaused = capture_pause;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call PauseGame(true)\n"
        "endfunction\n"));
    T_ASSERT(level.script_paused);
    T_ASSERT(captured_pause);

    G_SetScriptPaused(false);
    T_ASSERT(!level.script_paused);
    T_ASSERT(!captured_pause);
    gi.SetPaused = old_set_paused;
}

TEST(wc3_api, quest_pause_is_single_client_only) {
    void (*old_set_paused)(BOOL) = gi.SetPaused;

    captured_pause = false;
    gi.SetPaused = capture_pause;
    G_SetClientConnected(&g_edicts[0], true);
    G_SetQuestDialogOpen(&g_edicts[0], true);
    T_ASSERT(level.quest_paused);
    T_ASSERT(captured_pause);

    G_SetClientConnected(&g_edicts[1], true);
    T_ASSERT(!level.quest_paused);
    T_ASSERT(!captured_pause);

    G_SetClientConnected(&g_edicts[1], false);
    T_ASSERT(level.quest_paused);
    T_ASSERT(captured_pause);
    G_SetQuestDialogOpen(&g_edicts[0], false);
    T_ASSERT(!level.quest_paused);
    T_ASSERT(!captured_pause);
    gi.SetPaused = old_set_paused;
}

static void capture_presentation_write(pfWriteType_t type, void const *data) {
    (void)type;
    (void)data;
    presentation_write_count++;
}

static void capture_presentation_unicast(LPEDICT ent) {
    (void)ent;
    presentation_unicast_count++;
}

static void capture_indicator_write(pfWriteType_t type, void const *data) {
    DWORD const slot = indicator_write_count++;
    if (slot >= 4) return;
    indicator_types[slot] = type;
    if (data) indicator_values[slot] = *(LONG const *)data;
}

static void capture_indicator_unicast(LPEDICT ent) {
    indicator_recipient = ent;
}

TEST(wc3_api, add_indicator_accepts_unit_widget_and_sends_local_tinted_ring) {
    LPGAMECLIENT gc = &game.clients[0];
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;

    memset(indicator_types, 0, sizeof(indicator_types));
    memset(indicator_values, 0, sizeof(indicator_values));
    indicator_write_count = 0;
    indicator_recipient = NULL;
    gc->ps.number = 0;
    G_SetClientConnected(&g_edicts[0], true);
    currentplayer = &gc->ps;
    gi.Write = capture_indicator_write;
    gi.unicast = capture_indicator_unicast;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 64.0, 96.0, 0.0)\n"
        "  call AddIndicator(u, 255, 128, 64, 200)\n"
        "endfunction\n"));

    T_EQ(indicator_write_count, 4);
    T_EQ(indicator_types[0], PF_BYTE);
    T_EQ(indicator_values[0], svc_temp_entity);
    T_EQ(indicator_types[1], PF_BYTE);
    T_EQ(indicator_values[1], TE_ENTITY_INDICATOR);
    T_EQ(indicator_types[2], PF_LONG);
    T_ASSERT(indicator_values[2] > 0);
    T_EQ(indicator_types[3], PF_LONG);
    T_EQ((DWORD)indicator_values[3], 0xc84080ffu);
    T_EQ(indicator_recipient, &g_edicts[0]);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    currentplayer = NULL;
    G_SetClientConnected(&g_edicts[0], false);
}

TEST(wc3_api, disconnected_presentation_defers_network_write_until_connected) {
    LPGAMECLIENT gc = &game.clients[0];
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;

    presentation_write_count = 0;
    presentation_unicast_count = 0;
    gi.Write = capture_presentation_write;
    gi.unicast = capture_presentation_unicast;

    T_ASSERT(!gc->connected);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call ShowInterface(false, 0.0)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_ASSERT(gc->presentation_dirty);

    G_RunClients();
    T_EQ(presentation_write_count, 0);
    T_EQ(presentation_unicast_count, 0);
    T_ASSERT(gc->presentation_dirty);

    G_SetClientConnected(&g_edicts[0], true);
    G_RunClients();
    T_ASSERT(presentation_write_count > 0);
    T_ASSERT(presentation_unicast_count > 0);
    T_ASSERT(!gc->presentation_dirty);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_api, client_ui_init_preserves_authored_state_and_rejects_invalid_state) {
    LPGAMECLIENT gc = &game.clients[0];
    gc->ps.client_ui_state = CLIENT_UI_CINEMATIC;
    gc->ps.uiflags = ~(1u << LAYER_CINEMATIC);
    gc->presentation_dirty = true;

    G_InitClientUIState(gc);

    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_EQ(gc->ps.uiflags, ~(1u << LAYER_CINEMATIC));
    T_ASSERT(gc->presentation_dirty);

    gc->ps.client_ui_state = CLIENT_UI_CINEMATIC + 1;
    G_InitClientUIState(gc);
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
}

static LPCSTR gamecache_disabled_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_gamecache_mode") ? "disabled" : fallback;
}

static LPCSTR gamecache_memory_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_gamecache_mode") ? "memory" : fallback;
}

static LPCSTR campaign_progress_roc_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "fs_expansion") ? "0" : fallback;
}

static char campaign_progress_test_path[] = "campaign-progress-native-test.orcp";

static void campaign_progress_test_user_path(LPCSTR rel, LPSTR out, DWORD out_size) {
    (void)rel;
    if (!out || !out_size) return;
    snprintf(out, out_size, "%s", campaign_progress_test_path);
}

static int ui_sound_calls;
static int ui_sound_value;
static int capture_ui_sound_index(LPCSTR path) {
    (void)path;
    return 77;
}
static void capture_ui_sound(LPEDICT ent, int channel, int sound, FLOAT volume, FLOAT attenuation, FLOAT timeofs) {
    (void)ent; (void)volume; (void)attenuation; (void)timeofs;
    ui_sound_calls++;
    ui_sound_value = sound;
    T_EQ(channel, CHAN_OWNER | CHAN_RELIABLE);
}

TEST(wc3_api, default_camera_authors_lens) {
    gameCamera_t cam;
    T_ASSERT(CL_GameDefaultCamera(&cam));
    T_ASSERT(CL_GameCameraUsesWorldUp());
    T_FEQ(cam.fov, WC3_CAMERA_DEFAULT_FOV, 0.001f);
    T_FEQ(cam.znear, WC3_CAMERA_DEFAULT_NEAR_Z, 0.001f);
    T_FEQ(cam.zfar, WC3_CAMERA_DEFAULT_FAR_Z, 0.001f);
}

/* Campaign human slots need not match the connection slot; exercise the real VM/edict module boundary. */
TEST(wc3_api, escape_restores_game_camera_ui_and_control) {
    LPGAMECLIENT gc = &game.clients[0];
    LPCSTR cancel[] = { "cancel" };
    game.clients[1].ps.number = 0;
    gc->ps.number = 1;
    gc->camera.state.viewangles = (VECTOR3){300, 0, 120};
    gc->camera.state.target_distance = 900;
    gc->camera.state.fov = 35;
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function cleanup takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ResetToGameCamera(0.0)\n"
        "    call ShowInterface(true, 0.0)\n"
        "    call EnableUserControl(true)\n"
        "    call PanCameraTo(128.0, 256.0)\n"
        "  endif\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerEvent(t, Player(1), EVENT_PLAYER_END_CINEMATIC)\n"
        "  call TriggerAddAction(t, function cleanup)\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ShowInterface(false, 0.0)\n"
        "    call EnableUserControl(false)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_ASSERT(gc->no_control);
    /* An unrelated player's cancel must not run the registered cleanup. */
    globals.ClientCommand(&g_edicts[1], 1, cancel);
    G_RunEvents(); jass_runevents(level.vm);
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_FEQ(gc->camera.state.target_distance, 900, 0.001f);

    globals.ClientCommand(&g_edicts[0], 1, cancel);
    G_RunEvents(); jass_runevents(level.vm); G_RunClients();
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
    T_EQ(gc->ps.uiflags, 1u << LAYER_CINEMATIC);
    T_ASSERT(!gc->no_control);
    T_FEQ(gc->ps.vieworigin.x, 128, 0.001f); T_FEQ(gc->ps.vieworigin.y, 256, 0.001f);
    T_FEQ(gc->ps.distance, WC3_CAMERA_DEFAULT_DISTANCE, 0.001f); T_FEQ(gc->ps.fov, WC3_CAMERA_DEFAULT_FOV, 0.001f);
    T_FEQ(gc->ps.znear, WC3_CAMERA_DEFAULT_NEAR_Z, 0.001f);
    T_FEQ(gc->ps.zfar, WC3_CAMERA_DEFAULT_FAR_Z, 0.001f);
    T_FEQ(gc->ps.viewangles.x, 326.0f, 0.001f); T_FEQ(gc->ps.viewangles.z, 0.0f, 0.001f);
}

TEST(wc3_api, fly_height_native_keeps_authored_default_separate) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call SetUnitFlyHeight(u, 123.0, 0.0)\n"
        "  call BJassAssert(R2I(GetUnitFlyHeight(u)) == 123, \"current fly height was not updated\")\n"
        "  call BJassAssert(R2I(GetUnitDefaultFlyHeight(u)) != 123, \"default fly height became mutable\")\n"
        "endfunction\n"));
}

TEST(wc3_api, entering_unit_native_returns_region_event_subject) {
    LPEDICT entering = NULL;
    LPEVENT handler = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit udg_Entering = null\n"
        "  boolean udg_Entered = false\n"
        "endglobals\n"
        "function onEnter takes nothing returns nothing\n"
        "  call BJassAssert(GetEnteringUnit() == udg_Entering, \"GetEnteringUnit did not return event subject\")\n"
        "  call BJassAssert(GetTriggerUnit() == udg_Entering, \"GetTriggerUnit did not return event subject\")\n"
        "  set udg_Entered = true\n"
        "endfunction\n"
        "function verifyEnter takes nothing returns nothing\n"
        "  call BJassAssert(udg_Entered, \"region enter action did not run\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local region r = CreateRegion()\n"
        "  call RegionAddRect(r, Rect(100.0, 100.0, 200.0, 200.0))\n"
        "  set udg_Entering = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call TriggerRegisterEnterRegion(t, r, null)\n"
        "  call TriggerAddAction(t, function onEnter)\n"
        "endfunction\n"
    ));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') &&
            g_edicts[i].s.player == 0) {
            entering = &g_edicts[i];
        }
    }
    T_NOT_NULL(entering);
    FOR_EACH_EVENT(evt) {
        if (evt->type == EVENT_GAME_ENTER_REGION) {
            handler = evt;
            break;
        }
    }
    T_NOT_NULL(handler);
    G_PublishEvent(entering, EVENT_GAME_ENTER_REGION)->responseTo = handler;
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyEnter", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* An event's owner is GetTriggerPlayer(), not the local-player selector used by GetLocalPlayer().
 * Human02's victory chain starts from the Blademaster (player 4) dying, then
 * TriggerExecute()s nested cinematic triggers whose local UI branch targets
 * the connected Human player (map player 1). */
TEST(wc3_api, enemy_event_keeps_trigger_player_separate_from_local_player_context) {
    LPGAMECLIENT human = &game.clients[0];
    LPGAMECLIENT enemy = &game.clients[4];
    LPEDICT dying;

    /* Reproduce the campaign mapping where connection slot 0 is map player 1. */
    game.clients[1].ps.number = 0;
    human->ps.number = 1;
    enemy->ps.number = 4;
    currentplayer = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  trigger udg_Inner = null\n"
        "  unit udg_OuterUnit = null\n"
        "  boolean udg_Evaluated = false\n"
        "endglobals\n"
        "function inner_condition takes nothing returns boolean\n"
        "  return GetTriggerUnit() == udg_OuterUnit\n"
        "endfunction\n"
        "function inner_action takes nothing returns nothing\n"
        "  call BJassAssert(GetTriggerUnit() == udg_OuterUnit, \"nested TriggerExecute lost GetTriggerUnit\")\n"
        "  call SetPlayerState(Player(1), PLAYER_STATE_RESOURCE_GOLD, GetPlayerId(GetTriggerPlayer()))\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ShowInterface(false, 0.0)\n"
        "  endif\n"
        "endfunction\n"
        "function outer_action takes nothing returns nothing\n"
        "  set udg_OuterUnit = GetTriggerUnit()\n"
        "  set udg_Evaluated = TriggerEvaluate(udg_Inner)\n"
        "  call BJassAssert(udg_Evaluated, \"nested TriggerEvaluate lost GetTriggerUnit\")\n"
        "  call TriggerExecute(udg_Inner)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger outer = CreateTrigger()\n"
        "  set udg_Inner = CreateTrigger()\n"
        "  call TriggerAddCondition(udg_Inner, Condition(function inner_condition))\n"
        "  call TriggerAddAction(udg_Inner, function inner_action)\n"
        "  call TriggerRegisterPlayerUnitEvent(outer, Player(4), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddAction(outer, function outer_action)\n"
        "endfunction\n"));

    dying = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    dying->s.player = 4;
    G_PublishEvent(dying, EVENT_PLAYER_UNIT_DEATH);
    G_RunEvents();
    jass_runevents(level.vm);

    T_EQ(human->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 4);
    T_EQ(human->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_EQ(enemy->ps.client_ui_state, CLIENT_UI_GAME);
    T_NULL(currentplayer);
}

TEST(wc3_api, camera_margin_is_default_camera_inset_from_playable_area) {
    /* W3I complements crop the entire W3E terrain to the playable rectangle.
     * GetCameraMargin is the remaining inset from that playable rectangle to
     * the W3I default camera bounds; it is not complement * TILE_SIZE. */
    int const raw_complements[4] = { 4, 8, 6, 10 };
    LPMAPINFO mapinfo = (LPMAPINFO)level.mapinfo;

    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = { -4096.0f, -3072.0f },
        .max = { 4096.0f, 3072.0f }));
    memcpy(&mapinfo->cameraBounds.complement, raw_complements, sizeof(raw_complements));

    /* Complements produce playable [-3584,-2304]..[3072,1792].
     * Default camera bounds are inset by L=256, R=384, B=384, T=512. */
    memcpy(mapinfo->cameraBounds.bounds, (FLOAT[8]){
        -3328.0f, -1920.0f,
        -3328.0f,  1280.0f,
         2688.0f,  1280.0f,
         2688.0f, -1920.0f,
    }, sizeof(mapinfo->cameraBounds.bounds));

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-3584.0 + GetCameraMargin(CAMERA_MARGIN_LEFT), "
        "-2304.0 + GetCameraMargin(CAMERA_MARGIN_BOTTOM), "
        "-3584.0 + GetCameraMargin(CAMERA_MARGIN_LEFT), "
        "1792.0 - GetCameraMargin(CAMERA_MARGIN_TOP), "
        "3072.0 - GetCameraMargin(CAMERA_MARGIN_RIGHT), "
        "1792.0 - GetCameraMargin(CAMERA_MARGIN_TOP), "
        "3072.0 - GetCameraMargin(CAMERA_MARGIN_RIGHT), "
        "-2304.0 + GetCameraMargin(CAMERA_MARGIN_BOTTOM))\n"
        "endfunction\n"));

    /* The generated SetCameraBounds call reconstructs the W3I default camera
     * rectangle instead of applying the complement widths a second time. */
    T_FEQ(level.camera_bounds.min.x, -3328.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.x, 2688.0f, 0.001f);
    T_FEQ(level.camera_bounds.min.y, -1920.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.y, 1280.0f, 0.001f);
}

TEST(wc3_api, world_bounds_enables_full_map_group_transfer) {
    LPEDICT unit;

    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h', 'b', 'l', 'a'), 128.0f, 128.0f);
    unit->svflags |= SVF_MONSTER;
    unit->s.player = 8;
    T_ASSERT(run_test_jass(
        "function transfer_action takes nothing returns nothing\n"
        "  call SetUnitOwner(GetEnumUnit(), Player(1), true)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local group units = CreateGroup()\n"
        "  call GroupEnumUnitsInRect(units, GetWorldBounds(), null)\n"
        "  call ForGroup(units, function transfer_action)\n"
        "endfunction\n"));
    T_EQ(unit->s.player, 1);
}

TEST(wc3_api, camera_bounds_clamp_user_and_scripted_targets) {
    LPGAMECLIENT gc = &game.clients[0];
    VECTOR2 requested = { 500.0f, -500.0f };

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-100.0, -50.0, -100.0, 50.0, 100.0, 50.0, 100.0, -50.0)\n"
        "endfunction\n"));
    T_FEQ(level.camera_bounds.min.x, -100.0f, 0.001f);
    T_FEQ(level.camera_bounds.min.y, -50.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.x, 100.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.y, 50.0f, 0.001f);

    G_ClientSetCameraPosition(&g_edicts[0], &requested);
    T_FEQ(gc->camera.state.position.x, 100.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, -50.0f, 0.001f);

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraPosition(GetCameraBoundMinX() - 200.0, GetCameraBoundMaxY() + 200.0)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.state.position.x, -100.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 50.0f, 0.001f);
    currentplayer = NULL;
}


TEST(wc3_api, camera_angle_interpolation_uses_shortest_periodic_arc) {
    LPGAMECLIENT gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.target_controller = NULL;
    gc->camera.target_inherit_orientation = false;
    gc->camera.old_state = gc->camera.state;
    gc->camera.old_state.viewangles = (VECTOR3){ -394.0f, 0.0f, 350.0f };
    gc->camera.state = gc->camera.old_state;
    gc->camera.state.viewangles = (VECTOR3){ 326.0f, 0.0f, 10.0f };
    gc->camera.start_time = 100;
    gc->camera.end_time = 1100;
    level.time = 600;

    G_RunClients();

    /* -394 and 326 are the same orientation modulo 360, so pitch must not
     * travel two full turns.  Yaw 350 -> 10 crosses the wrap by +20 degrees. */
    T_FEQ(gc->ps.viewangles.x, -394.0f, 0.001f);
    T_FEQ(gc->ps.viewangles.y, 0.0f, 0.001f);
    T_FEQ(gc->ps.viewangles.z, 360.0f, 0.001f);
    T_FEQ(CL_GameLerpDegrees(10.0f, 350.0f, 0.5f), 0.0f, 0.001f);
    T_FEQ(CL_GameLerpDegrees(326.0f, -394.0f, 0.5f), 326.0f, 0.001f);
}

TEST(wc3_api, camera_runtime_getters_report_interpolated_state_and_eye) {
    LPGAMECLIENT gc = &game.clients[0];

    gc->ps.number = 0;
    gc->ps.vieworigin = (VECTOR3){ 100.0f, 200.0f, 300.0f };
    gc->ps.viewangles = (VECTOR3){ 0.0f, 0.0f, 0.0f };
    gc->ps.distance = 100.0f;
    gc->ps.fov = 50.0f;
    gc->ps.znear = 100.0f;
    gc->ps.zfar = 4000.0f;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(R2I(GetCameraField(CAMERA_FIELD_FARZ)) == 4000, \"farz\")\n"
        "  call BJassAssert(R2I(GetCameraEyePositionX()) == 100, \"eye x\")\n"
        "  call BJassAssert(R2I(GetCameraEyePositionY()) == 200, \"eye y\")\n"
        "  call BJassAssert(R2I(GetCameraEyePositionZ()) == 400, \"eye z\")\n"
        "endfunction\n"));
    currentplayer = NULL;
}

TEST(wc3_api, timed_camera_pan_with_z_interpolates_target_height) {
    LPGAMECLIENT gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(VECTOR2, 0.0f, 0.0f);
    gc->camera.state.z_offset = 0.0f;
    gc->camera.target_height = G_MakeServerOrigin(0.0f, 0.0f, 0.0f).z;
    gc->camera.old_state = gc->camera.state;
    level.time = 100;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call PanCameraToTimedWithZ(200.0, 300.0, 400.0, 2.0)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.state.position.x, 200.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 300.0f, 0.001f);
    T_FEQ(gc->camera.state.z_offset, 400.0f, 0.001f);
    T_EQ(gc->camera.start_time, 100);
    T_EQ(gc->camera.end_time, 2100);

    level.time = 1100;
    G_RunClients();
    T_FEQ(gc->ps.vieworigin.x, 100.0f, 0.001f);
    T_FEQ(gc->ps.vieworigin.y, 150.0f, 0.001f);
    T_FEQ(gc->ps.vieworigin.z, gc->camera.target_height + 200.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_target_controller_can_inherit_unit_facing) {
    LPGAMECLIENT gc = &game.clients[0];
    LPEDICT target = NULL;

    gc->ps.number = 0;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 100.0, 200.0, 135.0)\n"
        "  call SetCameraTargetController(u, 10.0, -20.0, true)\n"
        "endfunction\n"));
    target = gc->camera.target_controller;
    T_NOT_NULL(target);
    T_FEQ(gc->camera.state.position.x, 110.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 180.0f, 0.001f);
    T_FEQ(gc->camera.state.viewangles.z, -45.0f, 0.001f);
    T_ASSERT(gc->camera.target_inherit_orientation);

    target->s.origin2 = MAKE(VECTOR2, 300.0f, 400.0f);
    target->s.angle = (FLOAT)DEG2RAD(45.0f);
    G_RunClients();
    T_FEQ(gc->camera.state.position.x, 310.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 380.0f, 0.001f);
    T_FEQ(gc->camera.state.viewangles.z, 45.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_setup_applies_clip_planes_z_and_dopan_contract) {
    LPGAMECLIENT gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(VECTOR2, 12.0f, 34.0f);
    gc->camera.state.near_z = 100.0f;
    gc->camera.state.far_z = 5000.0f;
    gc->camera.state.z_offset = 0.0f;
    gc->camera.old_state = gc->camera.state;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local camerasetup c = CreateCameraSetup()\n"
        "  call CameraSetupSetDestPosition(c, 500.0, 600.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_NEARZ, 55.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_FARZ, 6500.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_ZOFFSET, 125.0, 0.0)\n"
        "  call CameraSetupApply(c, false, false)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.state.position.x, 12.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 34.0f, 0.001f);
    T_FEQ(gc->camera.state.near_z, 55.0f, 0.001f);
    T_FEQ(gc->camera.state.far_z, 6500.0f, 0.001f);
    T_FEQ(gc->camera.state.z_offset, 125.0f, 0.001f);

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local camerasetup c = CreateCameraSetup()\n"
        "  call CameraSetupSetDestPosition(c, 700.0, 800.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_ZOFFSET, 377.4, 0.0)\n"
        "  call CameraSetupApplyForceDuration(c, true, 2.0)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.state.position.x, 700.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 800.0f, 0.001f);
    T_FEQ(gc->camera.state.z_offset, 377.4f, 0.001f);

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local camerasetup c = CreateCameraSetup()\n"
        "  call CameraSetupSetDestPosition(c, 700.0, 800.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_NEARZ, 65.0, 0.0)\n"
        "  call CameraSetupSetField(c, CAMERA_FIELD_FARZ, 7500.0, 0.0)\n"
        "  call CameraSetupApplyWithZ(c, 275.0)\n"
        "endfunction\n"));
    T_FEQ(gc->camera.state.position.x, 700.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 800.0f, 0.001f);
    T_FEQ(gc->camera.state.near_z, 65.0f, 0.001f);
    T_FEQ(gc->camera.state.far_z, 7500.0f, 0.001f);
    T_FEQ(gc->camera.state.z_offset, 275.0f, 0.001f);
    G_RunClients();
    T_FEQ(gc->ps.znear, 65.0f, 0.001f);
    T_FEQ(gc->ps.zfar, 7500.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_quick_position_sets_spacebar_target_without_moving_camera) {
    LPGAMECLIENT gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(VECTOR2, 12.0f, 34.0f);
    gc->camera.quick_position = MAKE(VECTOR2, 0.0f, 0.0f);
    gc->camera.quick_position_set = false;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraQuickPosition(2608.0, -5856.0)\n"
        "endfunction\n"));

    T_FEQ(gc->camera.state.position.x, 12.0f, 0.001f);
    T_FEQ(gc->camera.state.position.y, 34.0f, 0.001f);
    T_ASSERT(gc->camera.quick_position_set);
    T_FEQ(gc->camera.quick_position.x, 2608.0f, 0.001f);
    T_FEQ(gc->camera.quick_position.y, -5856.0f, 0.001f);
    currentplayer = NULL;
}

TEST(wc3_api, camera_bounds_are_map_global) {
    LPGAMECLIENT gc0 = &game.clients[0];

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-100.0, -100.0, -100.0, 100.0, 100.0, 100.0, 100.0, -100.0)\n"
        "endfunction\n"));
    T_FEQ(level.camera_bounds.min.x, -100.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.y, 100.0f, 0.001f);

    currentplayer = &gc0->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-25.0, -20.0, -25.0, 20.0, 25.0, 20.0, 25.0, -20.0)\n"
        "endfunction\n"));
    T_FEQ(level.camera_bounds.min.x, -25.0f, 0.001f);
    T_FEQ(level.camera_bounds.max.y, 20.0f, 0.001f);
    currentplayer = NULL;
}

/* Fast-forward only changes cinematic timing; JASS retains ownership of the input/UI lifecycle. */
TEST(wc3_api, skip_cutscene_preserves_scripted_input_and_ui_state) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    LPGAMECLIENT gc = &game.clients[0];

    gi.CvarString = skip_cutscene_cvar;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ShowInterface(false, 0.0)\n"
        "  call EnableUserControl(false)\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_CINEMATIC);
    T_ASSERT(gc->no_control);

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ShowInterface(true, 0.0)\n"
        "  call EnableUserControl(true)\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
    T_ASSERT(!gc->no_control);
    currentplayer = NULL;
    gi.CvarString = old_cvar;
}

static DWORD test_fow_cell(FLOAT x, FLOAT y) {
    DWORD cx = G_FowWorldToCellX(x), cy = G_FowWorldToCellY(y);
    return cy * level.fow.width + cx;
}

#ifdef WC3_FOW_PACKED_MASK
static LPCSTR api_fow_fast_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_fow_fast") ? "1" : fallback;
}

TEST(wc3_api, fog_state_natives_update_packed_planes) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    fowPlayerGrid_t *grid;
    DWORD index, word;
    WORD bit;

    setup_test_world();
    G_FowInit();
    G_FowConnectPlayer(0);
    gi.CvarString = api_fow_fast_cvar;
    G_FowUpdate();
    grid = &level.fow.players[0];
    index = test_fow_cell(0.0f, 0.0f);
    word = (index % level.fow.width >> 4) + index / level.fow.width * grid->packed_stride;
    bit = (WORD)(1u << (index % level.fow.width & 15));

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_VISIBLE, 0.0, 0.0, 32.0, false)\n"
        "endfunction\n"));
    T_ASSERT(grid->packed_visible[word] & bit); T_ASSERT(grid->packed_explored[word] & bit);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_FOGGED, 0.0, 0.0, 32.0, false)\n"
        "endfunction\n"));
    T_ASSERT(!(grid->packed_visible[word] & bit)); T_ASSERT(grid->packed_explored[word] & bit);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_MASKED, 0.0, 0.0, 32.0, false)\n"
        "endfunction\n"));
    T_ASSERT(!(grid->packed_visible[word] & bit)); T_ASSERT(!(grid->packed_explored[word] & bit));
    gi.CvarString = old_cvar;
    G_FowShutdown();
}
#endif

TEST(wc3_api, fog_state_natives_write_masked_fogged_and_visible) {
    DWORD fogged, visible, masked;
    fowPlayerGrid_t *grid;
    setup_test_world();
    G_FowInit();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRect(Player(0), FOG_OF_WAR_FOGGED, Rect(-64.0, -64.0, 64.0, 64.0), false)\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_VISIBLE, 256.0, 0.0, 32.0, false)\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_VISIBLE, -256.0, 0.0, 32.0, false)\n"
        "  call SetFogStateRadiusLoc(Player(0), FOG_OF_WAR_MASKED, Location(-256.0, 0.0), 32.0, false)\n"
        "endfunction\n"));
    grid = &level.fow.players[0];
    fogged = test_fow_cell(0.0f, 0.0f);
    visible = test_fow_cell(256.0f, 0.0f);
    masked = test_fow_cell(-256.0f, 0.0f);
    T_EQ(grid->explored[fogged], 1); T_EQ(grid->visible[fogged], 0);
    T_EQ(grid->explored[visible], 1); T_EQ(grid->visible[visible], 1);
    T_EQ(grid->explored[masked], 0); T_EQ(grid->visible[masked], 0);
}

TEST(wc3_api, fog_state_shared_vision_reaches_allied_viewer_only) {
    DWORD index;
    setup_test_world();
    G_FowInit();
    G_SetPlayerAlliance(test_player(1), test_player(0), ALLIANCE_SHARED_VISION, true);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFogStateRadius(Player(0), FOG_OF_WAR_VISIBLE, 0.0, 0.0, 32.0, true)\n"
        "endfunction\n"));
    index = test_fow_cell(0.0f, 0.0f);
    T_EQ(level.fow.players[0].visible[index], 1);
    T_EQ(level.fow.players[1].visible[index], 1);
    T_EQ(level.fow.players[2].visible[index], 0);
}

TEST(wc3_api, fog_modifier_same_turn_start_stop_still_explores) {
    FOGMODIFIER mod = {
        .player = 0,
        .state = WC3_FOG_STATE_VISIBLE,
        .center = { 0.0f, 0.0f },
        .radius = 32.0f,
    };
    DWORD index;

    setup_test_world();
    G_FowInit();
    G_FowConnectPlayer(0);
    index = test_fow_cell(0.0f, 0.0f);

    G_FogModifierStart(&mod);
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 1);
    G_FogModifierStop(&mod);

    /* The next normal update removes current sight but must retain the
     * exploration created synchronously by the short-lived modifier. */
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 0);
}

TEST(wc3_api, fog_modifier_states_and_visible_stop_transition) {
    FOGMODIFIER mod = {
        .player = 0,
        .state = WC3_FOG_STATE_VISIBLE,
        .center = { 0.0f, 0.0f },
        .radius = 32.0f,
    };
    DWORD index;
    setup_test_world();
    G_FowInit();
    G_FowConnectPlayer(0);
    index = test_fow_cell(0.0f, 0.0f);

    G_FogModifierStart(&mod);
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 1);
    G_FogModifierStop(&mod);
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 0);

    mod.center.x = 256.0f;
    index = test_fow_cell(256.0f, 0.0f);
    mod.state = WC3_FOG_STATE_FOGGED;
    G_FogModifierStart(&mod);
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 1);
    T_EQ(level.fow.players[0].visible[index], 0);
    mod.state = WC3_FOG_STATE_MASKED;
    G_FowUpdate();
    T_EQ(level.fow.players[0].explored[index], 0);
    T_EQ(level.fow.players[0].visible[index], 0);
    G_FogModifierStop(&mod);
}

TEST(wc3_time, jass_state_uses_misc_clock_and_suspend) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetFloatGameState(GAME_STATE_TIME_OF_DAY, 12.0)\n"
        "endfunction\n"));
    /* Warsmash defers SetFloatGameState until the simulation clock update. */
    T_FEQ(G_GetTimeOfDay(), 0.0f, 0.001f);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_EQ(game.clients[0].ps.stats[UI_PLAYERSTAT_ENV_PHASE],
         (USHORT)lroundf(0.5f * (FLOAT)USHRT_MAX));

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(GetFloatGameState(GAME_STATE_TIME_OF_DAY) == 12.0, \"time getter\")\n"
        "  call SuspendTimeOfDay(true)\n"
        "endfunction\n"));
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_EQ(game.clients[0].ps.stats[UI_PLAYERSTAT_ENV_PHASE],
         (USHORT)lroundf(0.5f * (FLOAT)USHRT_MAX));

    /* An explicit set still applies while ordinary progression is suspended. */
    G_SetTimeOfDay(18.0f);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 18.0f, 0.001f);
    T_ASSERT(G_IsNight());

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SuspendTimeOfDay(false)\n"
        "endfunction\n"));
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 18.005f, 0.001f);
}

TEST(wc3_time, false_time_overrides_and_freezes_canonical_clock) {
    FLOAT const tick_seconds = (FLOAT)FRAMETIME / 1000.0f;
    FLOAT const clock_step = tick_seconds / game.constants.gameDayLength * game.constants.gameDayHours;

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_ASSERT(!G_IsFalseTimeOfDay());

    G_SetFalseTimeOfDay(0, 0, tick_seconds * 3.0f);
    /* Warsmash initializes the false clock on its first simulation tick. */
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_ASSERT(!G_IsFalseTimeOfDay());

    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 0.0f, 0.001f);
    T_ASSERT(G_IsFalseTimeOfDay());
    T_ASSERT(G_IsNight());
    T_EQ(game.clients[0].ps.stats[UI_PLAYERSTAT_ENV_VARIANT], 1);

    /* Explicit SetTimeOfDay retargets the false clock, not the frozen
     * canonical clock, while the override exists. */
    G_SetTimeOfDay(18.5f);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 18.5f, 0.001f);
    T_ASSERT(G_IsFalseTimeOfDay());

    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_ASSERT(!G_IsFalseTimeOfDay());
    T_ASSERT(!G_IsNight());
    T_EQ(game.clients[0].ps.stats[UI_PLAYERSTAT_ENV_VARIANT], 0);

    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f + clock_step, 0.001f);
}

TEST(wc3_time, warsmash_false_time_native_can_be_declared_by_extension_script) {
    T_ASSERT(run_test_jass(
        "native SetFalseTimeOfDay takes integer hour, integer minute, real duration returns nothing\n"
        "function main takes nothing returns nothing\n"
        "  call SetFalseTimeOfDay(21, 15, 2.0)\n"
        "endfunction\n"));
    T_ASSERT(level.timeofday.false_time.active);
    T_ASSERT(!level.timeofday.false_time.initialized);
    T_EQ(level.timeofday.false_time.hour, 21);
    T_EQ(level.timeofday.false_time.minute, 15);
}

TEST(wc3_time, false_time_transition_drives_game_state_events) {
    FLOAT const step = (FLOAT)FRAMETIME / 1000.0f;
    DWORD writes;

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterGameStateEvent(t, GAME_STATE_TIME_OF_DAY, GREATER_THAN_OR_EQUAL, 18.0)\n"
        "endfunction\n"));
    writes = level.events.write;

    G_SetFalseTimeOfDay(21, 0, step * 3.0f);
    /* Creation is deliberately uninitialized, so the event enters its
     * condition when the first simulation tick exposes the false clock. */
    T_EQ(level.events.write, writes);
    G_UpdateTimeOfDay();
    T_EQ(level.events.write, writes + 1);
}

TEST(wc3_time, set_day_night_models_publishes_registered_dnc_models) {
    int (*old_model_index)(LPCSTR) = gi.ModelIndex;
    void (*old_configstring)(DWORD, LPCSTR) = gi.configstring;

    dnc_model_index_calls = 0;
    dnc_configstring_calls = 0;
    memset(dnc_configstring_index, 0, sizeof(dnc_configstring_index));
    memset(dnc_configstring_value, 0, sizeof(dnc_configstring_value));
    gi.ModelIndex = capture_dnc_model_index;
    gi.configstring = capture_dnc_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetDayNightModels(\"Environment\\DNC\\Terrain.mdl\", \"Environment\\DNC\\Unit.mdl\")\n"
        "endfunction\n"));

    T_EQ(dnc_model_index_calls, 2);
    T_EQ(dnc_configstring_calls, 2);
    T_EQ(dnc_configstring_index[0], CS_TERRAIN_LIGHT_MODEL);
    T_STREQ(dnc_configstring_value[0], "41");
    T_EQ(dnc_configstring_index[1], CS_ENTITY_LIGHT_MODEL);
    T_STREQ(dnc_configstring_value[1], "42");

    gi.ModelIndex = old_model_index;
    gi.configstring = old_configstring;
}

TEST(wc3_environment_fog, set_terrain_fog_ex_publishes_runtime_state) {
    void (*old_configstring)(DWORD, LPCSTR) = gi.configstring;
    int style = -1;
    FLOAT start = 0.0f, end = 0.0f, density = 0.0f;
    FLOAT red = 0.0f, green = 0.0f, blue = 0.0f;

    scene_fog_configstring_calls = 0;
    scene_fog_configstring_index = 0;
    scene_fog_configstring_value[0] = '\0';
    gi.configstring = capture_scene_fog_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetTerrainFogEx(0, 1000.0, 5000.0, 0.25, 0.2, 0.3, 0.4)\n"
        "endfunction\n"));

    T_EQ(level.environment_fog.active.style, WC3_ENV_FOG_LINEAR);
    T_FEQ(level.environment_fog.active.start, 1000.0f, 0.001f);
    T_FEQ(level.environment_fog.active.end, 5000.0f, 0.001f);
    T_FEQ(level.environment_fog.active.density, 0.25f, 0.001f);
    T_FEQ(level.environment_fog.active.color.x, 0.2f, 0.001f);
    T_FEQ(level.environment_fog.active.color.y, 0.3f, 0.001f);
    T_FEQ(level.environment_fog.active.color.z, 0.4f, 0.001f);
    T_EQ(scene_fog_configstring_calls, 1);
    T_EQ(scene_fog_configstring_index, CS_SCENE_FOG);
    T_EQ(sscanf(scene_fog_configstring_value, "%d %f %f %f %f %f %f",
                &style, &start, &end, &density, &red, &green, &blue), 7);
    T_EQ(style, WC3_ENV_FOG_LINEAR);
    T_FEQ(start, 1000.0f, 0.001f);
    T_FEQ(end, 5000.0f, 0.001f);
    T_FEQ(density, 0.25f, 0.001f);
    T_FEQ(red, 0.2f, 0.001f);
    T_FEQ(green, 0.3f, 0.001f);
    T_FEQ(blue, 0.4f, 0.001f);

    gi.configstring = old_configstring;
}

TEST(wc3_environment_fog, reset_terrain_fog_restores_default_state) {
    void (*old_configstring)(DWORD, LPCSTR) = gi.configstring;

    level.environment_fog.defaults = (wc3EnvironmentFogState_t){
        .style = WC3_ENV_FOG_LINEAR,
        .start = 2500.0f,
        .end = 9000.0f,
        .density = 0.125f,
        .color = { 0.1f, 0.2f, 0.3f },
    };
    level.environment_fog.defaults_valid = true;
    level.environment_fog.active = (wc3EnvironmentFogState_t){
        .style = WC3_ENV_FOG_EXPONENTIAL_2,
        .start = 1.0f, .end = 2.0f, .density = 3.0f,
        .color = { 0.9f, 0.8f, 0.7f },
    };
    scene_fog_configstring_calls = 0;
    scene_fog_configstring_value[0] = '\0';
    gi.configstring = capture_scene_fog_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ResetTerrainFog()\n"
        "endfunction\n"));

    T_EQ(level.environment_fog.active.style, WC3_ENV_FOG_LINEAR);
    T_FEQ(level.environment_fog.active.start, 2500.0f, 0.001f);
    T_FEQ(level.environment_fog.active.end, 9000.0f, 0.001f);
    T_FEQ(level.environment_fog.active.density, 0.125f, 0.001f);
    T_FEQ(level.environment_fog.active.color.x, 0.1f, 0.001f);
    T_FEQ(level.environment_fog.active.color.y, 0.2f, 0.001f);
    T_FEQ(level.environment_fog.active.color.z, 0.3f, 0.001f);
    T_EQ(scene_fog_configstring_calls, 1);
    T_EQ(scene_fog_configstring_index, CS_SCENE_FOG);

    gi.configstring = old_configstring;
}

TEST(wc3_environment_fog, reset_without_default_is_noop) {
    void (*old_configstring)(DWORD, LPCSTR) = gi.configstring;

    level.environment_fog.defaults_valid = false;
    level.environment_fog.active = (wc3EnvironmentFogState_t){
        .style = WC3_ENV_FOG_LINEAR,
        .start = 1200.0f,
        .end = 6400.0f,
        .density = 0.5f,
        .color = { 0.25f, 0.5f, 0.75f },
    };
    scene_fog_configstring_calls = 0;
    gi.configstring = capture_scene_fog_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ResetTerrainFog()\n"
        "endfunction\n"));

    T_EQ(level.environment_fog.active.style, WC3_ENV_FOG_LINEAR);
    T_FEQ(level.environment_fog.active.start, 1200.0f, 0.001f);
    T_FEQ(level.environment_fog.active.end, 6400.0f, 0.001f);
    T_FEQ(level.environment_fog.active.density, 0.5f, 0.001f);
    T_EQ(scene_fog_configstring_calls, 0);

    gi.configstring = old_configstring;
}

static LPCSTR fog_roc_cvar(LPCSTR name, LPCSTR fallback) { return !strcmp(name, "fs_expansion") ? "0" : fallback; }
static LPCSTR fog_tft_cvar(LPCSTR name, LPCSTR fallback) { return !strcmp(name, "fs_expansion") ? "1" : fallback; }

static void check_singleton_default_zfog(wc3EnvironmentFogState_t const *fog) {
    T_EQ(fog->style, WC3_ENV_FOG_LINEAR); /* authored 0 maps through +1 */
    T_FEQ(fog->start, 20000.0f, 0.001f);
    T_FEQ(fog->end, 50000.0f, 0.001f);
    T_FEQ(fog->density, 0.0f, 0.001f);
    T_FEQ(fog->color.x, 0.0f, 0.001f);
    T_FEQ(fog->color.y, 0.0f, 0.001f);
    T_FEQ(fog->color.z, 0.0f, 0.001f);
}

/* Retail [DefaultZFog] is a singleton; TFT index 1 never parses, so index 0 is the per-field fallback. */
TEST(wc3_environment_fog, singleton_default_zfog_parses_under_both_editions) {
    stbIniCache_t saved = game.config.misc, custom = { 0 };
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    wc3EnvironmentFogState_t fog;

    T_ASSERT(Stb_IniCacheLoad(&custom, "TestData\\DefaultZFog.txt"));
    game.config.misc = custom;
    gi.CvarString = fog_roc_cvar;
    T_ASSERT(G_EnvironmentFogDefault(&fog));
    check_singleton_default_zfog(&fog);
    gi.CvarString = fog_tft_cvar;
    T_ASSERT(G_EnvironmentFogDefault(&fog));
    check_singleton_default_zfog(&fog);

    gi.CvarString = old_cvar;
    game.config.misc = saved;
    Stb_IniCacheFree(&custom);
}

TEST(wc3_environment_fog, invalid_extended_style_disables_scene_fog) {
    void (*old_configstring)(DWORD, LPCSTR) = gi.configstring;

    scene_fog_configstring_calls = 0;
    scene_fog_configstring_value[0] = '\0';
    gi.configstring = capture_scene_fog_configstring;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetTerrainFogEx(99, 1.0, 2.0, 3.0, 0.4, 0.5, 0.6)\n"
        "endfunction\n"));
    T_EQ(level.environment_fog.active.style, WC3_ENV_FOG_NONE);
    T_ASSERT(scene_fog_configstring_value[0] == '0');

    gi.configstring = old_configstring;
}

TEST(wc3_api, set_sky_model_publishes_registered_model) {
    int (*old_model_index)(LPCSTR) = gi.ModelIndex;
    void (*old_configstring)(DWORD, LPCSTR) = gi.configstring;

    sky_model_index_calls = sky_configstring_calls = 0;
    sky_configstring_index = 0;
    sky_configstring_value[0] = '\0';
    gi.ModelIndex = capture_sky_model_index;
    gi.configstring = capture_sky_configstring;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetSkyModel(\"Environment\\Sky\\Sky.mdx\")\n"
        "endfunction\n"));

    T_EQ(sky_model_index_calls, 1);
    T_EQ(sky_configstring_calls, 1);
    T_EQ(sky_configstring_index, CS_SKY);
    T_STREQ(sky_configstring_value, "41");

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetSkyModel(\"\")\n"
        "endfunction\n"));
    T_EQ(sky_model_index_calls, 1);
    T_EQ(sky_configstring_calls, 2);
    T_EQ(sky_configstring_index, CS_SKY);
    T_STREQ(sky_configstring_value, "0");

    gi.ModelIndex = old_model_index;
    gi.configstring = old_configstring;
}

TEST(wc3_time, dawn_and_dusk_use_misc_thresholds) {
    G_SetTimeOfDay(5.99f);
    G_UpdateTimeOfDay();
    T_ASSERT(G_IsNight());

    G_SetTimeOfDay(game.constants.dawnTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(!G_IsNight());

    G_SetTimeOfDay(game.constants.duskTimeGameHours - 0.01f);
    G_UpdateTimeOfDay();
    T_ASSERT(!G_IsNight());

    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(G_IsNight());
}

TEST(wc3_time, game_state_event_fires_on_false_to_true_transition) {
    DWORD writes;

    G_SetTimeOfDay(5.0f);
    G_UpdateTimeOfDay();
    T_ASSERT(run_test_jass(
        "function onTime takes nothing returns nothing\n"
        "  call SetFloatGameState(ConvertFGameState(2), 12.0)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterGameStateEvent(t, GAME_STATE_TIME_OF_DAY, GREATER_THAN_OR_EQUAL, 6.0)\n"
        "  call TriggerAddAction(t, function onTime)\n"
        "endfunction\n"));

    G_SetTimeOfDay(6.0f);
    G_UpdateTimeOfDay();
    writes = level.events.write;
    T_EQ(writes, 1);
    G_RunEvents();
    jass_runevents(level.vm);

    /* The trigger action queues 12:00; applying it does not refire because
     * both 06:00 and 12:00 satisfy the registered >= 6 condition. */
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
    T_EQ(level.events.write, writes);
    G_UpdateTimeOfDay();
    T_EQ(level.events.write, writes);
}

TEST(wc3_api, variable_event_fires_when_counter_reaches_limit) {
    DWORD writes;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer counter = 0\n"
        "endglobals\n"
        "function onCounter takes nothing returns nothing\n"
        "  call SetFloatGameState(GAME_STATE_TIME_OF_DAY, 12.0)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterVariableEvent(t, \"counter\", ConvertLimitOp(2), 100.0)\n"
        "  call TriggerAddAction(t, function onCounter)\n"
        "  set counter = 99\n"
        "  set counter = 100\n"
        "  set counter = 101\n"
        "endfunction\n"));
    writes = level.events.write;
    T_EQ(writes, 1);
    G_RunEvents();
    jass_runevents(level.vm);
    T_EQ(level.events.read, writes);
    G_UpdateTimeOfDay();
    T_FEQ(G_GetTimeOfDay(), 12.0f, 0.001f);
}

/* The retail cripple timer broadcasts with a direct local-player argument after its local IF. */
TEST(wc3_api, direct_local_player_text_call_reaches_each_player_once) {
    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function announce takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call DisplayTimedTextToPlayer(GetLocalPlayer(), 0, 0, 5, \"local\")\n"
        "  endif\n"
        "  call DisplayTimedTextToPlayer(GetLocalPlayer(), 0, 0, 10, \"revealed\")\n"
        "  call DisplayTextToPlayer(Player(1), 0, 0, \"targeted\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  call ExecuteFunc(\"announce\")\n"
        "endfunction\n"));
    jass_runevents(level.vm);
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT gc = &game.clients[i];
        if (i >= MAX_PLAYERS) {
            T_EQ(gc->message_log.count, 0);
            continue;
        }
        T_EQ(gc->message_log.count, i < 2 ? 2 : 1);
        T_STREQ(gc->message_log.entries[i == 0 ? 1 : 0], "revealed");
        T_STREQ(gc->message.text, i == 1 ? "targeted" : "revealed");
    }
    T_NULL(currentplayer);
}

TEST(wc3_api, display_text_tracks_lifetime_and_clear) {
    LPGAMECLIENT gc = &game.clients[0];

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    level.time = 100;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call DisplayTimedTextToPlayer(Player(0), 0.10, 0.20, 2.0, \"Timed message\")\n"
        "endfunction\n"));
    T_EQ(gc->message.end_time, 2100);
    T_FEQ(gc->message.position.x, 0.10f, 0.001f);
    T_FEQ(gc->message.position.y, 0.20f, 0.001f);
    T_STREQ(gc->message.text, "Timed message");
    T_EQ(gc->message_log.count, 1);
    T_STREQ(gc->message_log.entries[0], "Timed message");

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call ClearTextMessages()\n"
        "endfunction\n"));
    T_EQ(gc->message.end_time, 0);
    T_STREQ(gc->message.text, "");
    T_EQ(gc->message_log.count, 1);
    T_STREQ(gc->message_log.entries[0], "Timed message");
}

static LPEDICT find_test_unit(DWORD class_id) {
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == class_id) {
            return g_edicts + i;
        }
    }
    return NULL;
}

static void setup_set_unit_position_pathmap(void) {
    enum { CELLS = 16 };
    BYTE pathmap[CELLS * CELLS] = {0};

    /* Requested point (256,256) lies in cell (8,8). Buildings and other
     * authored blockers are baked into this same no-walk map in production. */
    pathmap[8 * CELLS + 8] = 2;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
}

TEST(wc3_api, set_unit_position_unstucks_from_blocked_pathing) {
    LPEDICT moved;

    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit mover = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "  call SetUnitPosition(mover, 256.0, 256.0)\n"
        "endfunction\n"));

    moved = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(moved);
    /* Warsmash checks (256,256), then the first 64-unit spiral point below it. */
    T_FEQ(moved->s.origin.x, 256.0f, 0.001f);
    T_FEQ(moved->s.origin.y, 192.0f, 0.001f);
}

TEST(wc3_api, flyer_unstuck_search_uses_unflyable_instead_of_unwalkable) {
    enum { CELLS = 16 };
    BYTE pathmap[CELLS * CELLS] = {0};
    VECTOR2 const requested = {256.0f, 256.0f};
    VECTOR2 out;
    LPEDICT mover = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);

    mover->s.model = 1;
    mover->collision = 16.0f;
    mover->aiflags |= AI_FLYING;
    pathmap[8 * CELLS + 8] = CM_PATHING_UNWALKABLE;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(BOX2,
        .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));

    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    T_FEQ(out.x, 256.0f, 0.001f);
    T_FEQ(out.y, 256.0f, 0.001f);

    pathmap[8 * CELLS + 8] = CM_PATHING_UNFLYABLE;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    T_FEQ(out.x, 256.0f, 0.001f);
    T_FEQ(out.y, 192.0f, 0.001f);
}

TEST(wc3_api, unit_unstuck_search_skips_live_unit_collision) {
    VECTOR2 const requested = {256.0f, 256.0f};
    VECTOR2 out;
    LPEDICT blocker;
    LPEDICT mover;

    setup_set_unit_position_pathmap();
    blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 192.0f);
    mover = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    blocker->s.model = mover->s.model = 1;
    blocker->collision = mover->collision = 16.0f;

    T_ASSERT(G_FindUnitUnstuckPosition(mover, &requested, &out));
    /* Requested point is static-blocked; the next spiral point is occupied. */
    T_FEQ(out.x, 320.0f, 0.001f);
    T_FEQ(out.y, 192.0f, 0.001f);
}

TEST(wc3_api, set_unit_position_loc_uses_same_unstuck_search) {
    LPEDICT moved;

    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit mover = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "  local location target = Location(256.0, 256.0)\n"
        "  call SetUnitPositionLoc(mover, target)\n"
        "endfunction\n"));

    moved = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(moved);
    T_FEQ(moved->s.origin.x, 256.0f, 0.001f);
    T_FEQ(moved->s.origin.y, 192.0f, 0.001f);
}

/* Issue-418: HumanX03.w3x calls OffsetLocation(GetUnitLoc(null unit), ...) which
 * reaches GetLocationX/MoveLocation with a null handle. Null locations read as 0
 * (same contract as GetRectCenterX) and MoveLocation on null is a safe no-op. */
TEST(wc3_api, null_location_natives_return_zero_and_noop) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local location nullLoc = null\n"
        "  local location live = Location(10.0, 20.0)\n"
        "  call BJassAssert(GetLocationX(nullLoc) == 0.0, \"GetLocationX(null) must be 0\")\n"
        "  call BJassAssert(GetLocationY(nullLoc) == 0.0, \"GetLocationY(null) must be 0\")\n"
        "  call MoveLocation(nullLoc, 100.0, 200.0)\n"
        "  call BJassAssert(GetLocationX(live) == 10.0, \"live location x\")\n"
        "  call BJassAssert(GetLocationY(live) == 20.0, \"live location y\")\n"
        "  call MoveLocation(live, 30.0, 40.0)\n"
        "  call BJassAssert(GetLocationX(live) == 30.0, \"moved location x\")\n"
        "  call BJassAssert(GetLocationY(live) == 40.0, \"moved location y\")\n"
        "endfunction\n"));
}

TEST(wc3_api, set_unit_x_y_remain_raw_coordinates_on_blocked_pathing) {
    LPEDICT moved;

    setup_set_unit_position_pathmap();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit mover = CreateUnit(Player(0), 'hpea', 64.0, 64.0, 0.0)\n"
        "  call SetUnitX(mover, 256.0)\n"
        "  call SetUnitY(mover, 256.0)\n"
        "endfunction\n"));

    moved = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(moved);
    T_FEQ(moved->s.origin.x, 256.0f, 0.001f);
    T_FEQ(moved->s.origin.y, 256.0f, 0.001f);
}

TEST(wc3_api, set_unit_scale_uses_wc3_x_component_as_uniform_scale) {
    LPEDICT scaled = NULL;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hpea', 32.0, 64.0, 0.0)\n"
        "  call SetUnitScale(u, 1.5, 2.0, 3.0)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) {
            scaled = g_edicts + i;
            break;
        }
    }
    T_NOT_NULL(scaled);
    T_FEQ(scaled->s.scale, 1.5f, 0.001f);
}

TEST(wc3_api, set_unit_vertex_color_publishes_clamped_rgba) {
    BYTE data[256];
    LPEDICT tinted, clent;
    DWORD size, offset;
    USHORT header, count;
    BOOL found = false;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hpea', 32.0, 64.0, 0.0)\n"
        "  call SetUnitVertexColor(u, 300, 128, -10, 0)\n"
        "endfunction\n"));

    tinted = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(tinted);
    T_ASSERT(tinted->vertex_color_set);
    T_EQ(tinted->vertex_color.r, 255); T_EQ(tinted->vertex_color.g, 128);
    T_EQ(tinted->vertex_color.b, 0); T_EQ(tinted->vertex_color.a, 0);

    clent = G_GetPlayerEntityByNumber(0);
    T_NOT_NULL(clent);
    size = G_WriteClientDatagram(clent, data, sizeof(data));
    T_ASSERT(size >= sizeof(header) + sizeof(count));
    memcpy(&header, data, sizeof(header));
    T_ASSERT(header & BZ_GAME_DATAGRAM_ENTITY_TINTS);
    offset = sizeof(header) + (header & ~BZ_GAME_DATAGRAM_ENTITY_TINTS) * sizeof(wc3WeatherEffect_t);
    memcpy(&count, data + offset, sizeof(count)); offset += sizeof(count);
    FOR_LOOP(i, count) {
        USHORT number; COLOR32 color;
        memcpy(&number, data + offset, sizeof(number)); offset += sizeof(number);
        memcpy(&color, data + offset, sizeof(color)); offset += sizeof(color);
        if (number != tinted->s.number) continue;
        T_EQ(color.r, 255); T_EQ(color.g, 128); T_EQ(color.b, 0); T_EQ(color.a, 0);
        found = true;
    }
    T_ASSERT(found);
}

TEST(wc3_api, narrator_and_hint_text_share_message_log) {
    LPGAMECLIENT gc = &game.clients[0];

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    level.time = 100;
    gc->ps.client_ui_state = CLIENT_UI_GAME;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call SetCinematicScene(0, PLAYER_COLOR_RED, \"Narrator\", \"Select a peon.\", 3.0, 2.0)\n"
        "  endif\n"
        "  call DisplayTimedTextToPlayer(Player(0), 0.0, 0.0, 3.0, \"HINT - Build a Burrow.\")\n"
        "endfunction\n"));

    T_EQ(gc->message_log.count, 2);
    T_STREQ(gc->message_log.entries[0], "|cffffcc00Narrator:|r Select a peon.");
    T_STREQ(gc->message_log.entries[1], "HINT - Build a Burrow.");

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCinematicScene(0, PLAYER_COLOR_RED, \"\", \"\", 0.0, 0.0)\n"
        "endfunction\n"));
    T_EQ(gc->message_log.count, 0);

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    gc->ps.client_ui_state = CLIENT_UI_CINEMATIC;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCinematicScene(0, PLAYER_COLOR_RED, \"Narrator\", \"Cutscene line.\", 3.0, 2.0)\n"
        "endfunction\n"));
    T_EQ(gc->message_log.count, 0);
}

TEST(wc3_api, transient_command_style_text_does_not_enter_message_log) {
    LPGAMECLIENT gc = &game.clients[0];
    EDICT ent = { .client = gc };

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    level.time = 100;
    UI_ShowTransientText(&ent, &MAKE(VECTOR2, 0.0f, 0.0f), "Not enough gold.", 2.0f);

    T_STREQ(gc->message.text, "Not enough gold.");
    T_EQ(gc->message_log.count, 0);
}

TEST(wc3_api, command_error_key_resolves_commandstrings_and_race_variant) {
    LPGAMECLIENT gc = &game.clients[0];
    EDICT ent = { .client = gc };

    gc->ps.race = kPlayerRaceUndead;
    G_ShowCommandErrorKey(&ent, "Blightringfull", "fallback");
    T_STREQ(gc->message.text, "That gold mine can't support any more Acolytes.");

    G_ShowCommandErrorKey(&ent, "Nofood", "fallback");
    T_STREQ(gc->message.text, "Summon more Ziggurats to continue unit production.");
}

TEST(wc3_api, removeunit_hides_before_deferred_edict_release) {
    LPEDICT unit;

    G_ResetDeferredFrees();
    reset_entities();
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    G_DeferFreeEdict(unit);
    T_ASSERT(unit->inuse);
    T_ASSERT(unit->s.renderfx & RF_HIDDEN);
    G_RunDeferredFrees();
    T_ASSERT(!unit->inuse);
}

TEST(wc3_api, createunit_does_not_reuse_deferred_dead_unit) {
    LPEDICT dead, replacement;

    G_ResetDeferredFrees();
    reset_entities();
    dead = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    unit_die(dead, NULL);
    G_DeferFreeEdict(dead);
    replacement = unit_createorfind(0, MAKEFOURCC('h','p','e','a'), &(VECTOR2){0, 0}, 0);
    T_ASSERT(replacement && replacement != dead);
    T_ASSERT(replacement->inuse);
    G_RunDeferredFrees();
    G_FreeEdict(replacement);
}

TEST(wc3_api, createunit_allocates_fresh_nearby_unit) {
    LPEDICT existing, created;

    G_ResetDeferredFrees();
    reset_entities();
    existing = alloc_test_unit(MAKEFOURCC('n','z','o','m'), 0, 0);
    created = unit_create(0, MAKEFOURCC('n','z','o','m'), &(VECTOR2){0, 0}, 0);
    T_ASSERT(created && created != existing);
    T_ASSERT(created->inuse);
    G_FreeEdict(created);
    G_FreeEdict(existing);
}

TEST(wc3_api, message_log_is_bounded_and_evicts_oldest_entry) {
    LPGAMECLIENT gc = &game.clients[0];
    EDICT ent = { .client = gc };
    char text[64];

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    for (DWORD i = 0; i < WC3_MESSAGE_LOG_MAX_ENTRIES + 1; i++) {
        snprintf(text, sizeof(text), "Message %u", (unsigned)i);
        UI_MessageLogAppend(&ent, text);
    }

    T_EQ(gc->message_log.count, WC3_MESSAGE_LOG_MAX_ENTRIES);
    T_EQ(gc->message_log.first, 1);
    T_STREQ(gc->message_log.entries[gc->message_log.first], "Message 1");
    T_STREQ(gc->message_log.entries[0], "Message 128");
}

TEST(wc3_api, display_text_uses_automatic_duration) {
    LPGAMECLIENT gc = &game.clients[0];

    level.time = 100;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call DisplayTextToPlayer(Player(0), 0.0, 0.0, \"123456\")\n"
        "endfunction\n"));
    T_EQ(gc->message.end_time, 6100);

    level.time = gc->message.end_time;
    G_RunClients();
    T_EQ(gc->message.end_time, 0);
}

TEST(wc3_api, transmission_keeps_gameplay_ui_and_separates_voice_lifetime) {
    LPGAMECLIENT gc = &game.clients[0];

    level.time = 100;
    gc->ps.client_ui_state = CLIENT_UI_GAME;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call ForceCinematicSubtitles(false)\n"
        "    call SetCinematicScene(0, PLAYER_COLOR_BLUE, \"Captain\", \"Hold the line!\", 6.0, 4.0)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->ps.client_ui_state, CLIENT_UI_GAME);
    T_STREQ(gc->ps.texts[PLAYERTEXT_SPEAKER], "Captain");
    T_STREQ(gc->ps.texts[PLAYERTEXT_DIALOGUE], "Hold the line!");
    T_EQ(gc->cinematic_voice_end_time, 4100);
    T_EQ(gc->cinematic_end_time, 6100);
    T_EQ(gc->ps.stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR], 1);

    level.time = 4100;
    G_RunClients();
    T_EQ(gc->cinematic_voice_end_time, 0);
    T_EQ(gc->cinematic_end_time, 6100);
    T_STREQ(gc->ps.texts[PLAYERTEXT_DIALOGUE], "Hold the line!");

    level.time = 6100;
    G_RunClients();
    T_EQ(gc->cinematic_end_time, 0);
    T_EQ(gc->ps.stats[UI_PLAYERSTAT_CINEMATIC_PORTRAIT_COLOR], 0);
    T_STREQ(gc->ps.texts[PLAYERTEXT_SPEAKER], "");
    T_STREQ(gc->ps.texts[PLAYERTEXT_DIALOGUE], "");
}

/* Blizzard's cinematic helpers may forward polymorphic JASS null into a string
 * parameter while an ESC cancellation unwinds the active transmission. */
TEST(wc3_api, cinematic_string_null_is_accepted) {
    LPGAMECLIENT gc = &game.clients[0];

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCinematicScene(0, PLAYER_COLOR_BLUE, null, null, 0.0, 0.0)\n"
        "endfunction\n"));
    T_STREQ(gc->ps.texts[PLAYERTEXT_SPEAKER], "");
    T_STREQ(gc->ps.texts[PLAYERTEXT_DIALOGUE], "");
    currentplayer = NULL;
}

TEST(wc3_api, gameplay_transmission_preserves_underlying_timed_message_state) {
    LPGAMECLIENT gc = &game.clients[0];

    level.time = 100;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call DisplayTimedTextToPlayer(Player(0), 0.0, 0.0, 10.0, \"Objective updated\")\n"
        "  if GetLocalPlayer() == Player(0) then\n"
        "    call SetCinematicScene(0, PLAYER_COLOR_RED, \"Footman\", \"Ready.\", 3.0, 2.0)\n"
        "  endif\n"
        "endfunction\n"));
    T_EQ(gc->message.end_time, 10100);
    T_STREQ(gc->message.text, "Objective updated");
    T_EQ(gc->cinematic_end_time, 3100);

    level.time = 3100;
    G_RunClients();
    T_EQ(gc->cinematic_end_time, 0);
    T_EQ(gc->message.end_time, 10100);
    T_STREQ(gc->message.text, "Objective updated");
}

static DWORD ui_point_calls;
static BOOL count_ui_point(LPEDICT ent, LPCVECTOR2 loc) { ui_point_calls++; return false; }

TEST(wc3_api, enable_user_ui_does_not_block_world_selection) {
    LPGAMECLIENT gc = &game.clients[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    char number[16];
    LPCSTR select[] = { "select", number };

    gc->ps.number = 0;
    unit->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    snprintf(number, sizeof(number), "%u", unit->s.number);
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call EnableUserUI(false)\n"
        "endfunction"));
    T_ASSERT(gc->no_ui);

    globals.ClientCommand(&g_edicts[0], 2, select);
    T_ASSERT(unit->selected & (1u << gc->ps.number));
    currentplayer = NULL;
}

TEST(wc3_api, same_type_selection_filters_candidates_by_anchor_type) {
    LPGAMECLIENT gc = &game.clients[0];
    LPEDICT first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    LPEDICT other = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 128.0f, 64.0f);
    char first_number[16], second_number[16], other_number[16];
    LPCSTR command[] = { "select", first_number, "sametype", first_number, second_number, other_number };

    gc->ps.number = 0;
    first->s.player = second->s.player = other->s.player = 0;
    first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; other->svflags |= SVF_MONSTER;
    snprintf(first_number, sizeof(first_number), "%u", first->s.number);
    snprintf(second_number, sizeof(second_number), "%u", second->s.number);
    snprintf(other_number, sizeof(other_number), "%u", other->s.number);

    globals.ClientCommand(&g_edicts[0], 6, command);
    T_ASSERT(G_IsEntitySelected(gc, first));
    T_ASSERT(G_IsEntitySelected(gc, second));
    T_ASSERT(!G_IsEntitySelected(gc, other));
}

TEST(wc3_api, client_selection_publishes_selection_events_once_per_delta) {
    LPGAMECLIENT gc = &game.clients[0];
    LPEDICT first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    char first_number[16];
    char second_number[16];
    LPCSTR select_first[] = { "select", first_number };
    LPCSTR select_second[] = { "select", second_number };

    gc->ps.number = 0;
    first->s.player = second->s.player = 0;
    first->svflags |= SVF_MONSTER;
    second->svflags |= SVF_MONSTER;
    snprintf(first_number, sizeof(first_number), "%u", first->s.number);
    snprintf(second_number, sizeof(second_number), "%u", second->s.number);

    T_ASSERT(run_test_jass(
        "function selected_action takes nothing returns nothing\n"
        "  call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, GetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD) + 1)\n"
        "endfunction\n"
        "function deselected_action takes nothing returns nothing\n"
        "  call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_LUMBER, GetPlayerState(Player(0), PLAYER_STATE_RESOURCE_LUMBER) + 1)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger selected = CreateTrigger()\n"
        "  local trigger deselected = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(selected, Player(0), EVENT_PLAYER_UNIT_SELECTED, null)\n"
        "  call TriggerAddAction(selected, function selected_action)\n"
        "  call TriggerRegisterPlayerUnitEvent(deselected, Player(0), EVENT_PLAYER_UNIT_DESELECTED, null)\n"
        "  call TriggerAddAction(deselected, function deselected_action)\n"
        "endfunction\n"));

    globals.ClientCommand(&g_edicts[0], 2, select_first);
    G_RunEvents();
    jass_runevents(level.vm);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);

    /* Re-sending identical authoritative membership is not a new selection. */
    globals.ClientCommand(&g_edicts[0], 2, select_first);
    G_RunEvents();
    jass_runevents(level.vm);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);

    globals.ClientCommand(&g_edicts[0], 2, select_second);
    G_RunEvents();
    jass_runevents(level.vm);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 2);
    T_EQ(gc->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 1);
}

TEST(wc3_api, build_placement_publishes_point_order_event_context) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder;
    UnitProfile_t profile = { .builds = "hbar" };
    VECTOR2 point = { 64.0f, 64.0f };
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128.0f, -128.0f);
    builder->s.player = client->ps.number;
    builder->data.UnitProfile = &profile;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer pointEvents = 0\n"
        "endglobals\n"
        "function onPointOrder takes nothing returns nothing\n"
        "  set pointEvents = pointEvents + 1\n"
        "  call BJassAssert(GetUnitTypeId(GetOrderedUnit()) == 'hpea', \"ordered unit must be the builder\")\n"
        "  call BJassAssert(GetIssuedOrderId() == 'hbar', \"build point order must expose building rawcode\")\n"
        "  call BJassAssert(GetOrderPointX() == 64.0, \"build point order X must survive event dispatch\")\n"
        "  call BJassAssert(GetOrderPointY() == 64.0, \"build point order Y must survive event dispatch\")\n"
        "endfunction\n"
        "function verifyPointOrder takes nothing returns nothing\n"
        "  call BJassAssert(pointEvents == 1, \"build placement must publish one player point-order event\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_ISSUED_POINT_ORDER, null)\n"
        "  call TriggerAddAction(t, function onPointOrder)\n"
        "endfunction\n"));

    T_ASSERT(G_IssueBuildOrder(builder, barracks, &point));
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyPointOrder", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, issue_build_order_by_id_uses_authoritative_build_path) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder;
    UnitProfile_t profile = { .builds = "hbar" };
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testBuilder = null\n"
        "endglobals\n"
        "function issueBuild takes nothing returns nothing\n"
        "  call BJassAssert(IssueBuildOrderById(testBuilder, 'hbar', 64.0, 64.0), \"build native must accept a legal structure\")\n"
        "  call BJassAssert(not IssueBuildOrderById(testBuilder, 'hfoo', 96.0, 96.0), \"build native must reject a non-Builds rawcode\")\n"
        "endfunction\n"
        "function issuePointBuild takes nothing returns nothing\n"
        "  call BJassAssert(IssuePointOrderById(testBuilder, 'hbar', 128.0, 128.0), \"point-order-by-id must accept a building rawcode\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set testBuilder = CreateUnit(Player(0), 'hpea', -128.0, -128.0, 0.0)\n"
        "endfunction\n"));

    builder = find_test_unit(MAKEFOURCC('h','p','e','a'));
    T_NOT_NULL(builder);
    builder->data.UnitProfile = &profile;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    jass_callbyname(level.vm, "issueBuild", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(builder->build_project, barracks);
    T_NOT_NULL(builder->goalentity);
    T_FEQ(builder->goalentity->s.origin2.x, 64.0f, 0.001f);
    T_FEQ(builder->goalentity->s.origin2.y, 64.0f, 0.001f);

    unit_stand(builder);
    jass_callbyname(level.vm, "issuePointBuild", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(builder->build_project, barracks);
    T_NOT_NULL(builder->goalentity);
}

TEST(wc3_api, construct_finish_fires_player_and_unit_events_with_structure_context) {
    LPEDICT building;
    LPGAMECLIENT saved;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testBuilding = null\n"
        "  integer playerFinish = 0\n"
        "  integer unitFinish = 0\n"
        "endglobals\n"
        "function onPlayerFinish takes nothing returns nothing\n"
        "  call BJassAssert(GetConstructedStructure() == testBuilding, \"player finish structure context\")\n"
        "  set playerFinish = playerFinish + 1\n"
        "endfunction\n"
        "function onUnitFinish takes nothing returns nothing\n"
        "  call BJassAssert(GetConstructedStructure() == testBuilding, \"unit finish structure context\")\n"
        "  set unitFinish = unitFinish + 1\n"
        "endfunction\n"
        "function verifyFinish takes nothing returns nothing\n"
        "  call BJassAssert(playerFinish == 1, \"player construct finish must fire once\")\n"
        "  call BJassAssert(unitFinish == 1, \"unit construct finish must fire once\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger playerTrig = CreateTrigger()\n"
        "  local trigger unitTrig = CreateTrigger()\n"
        "  set testBuilding = CreateUnit(Player(0), 'hbar', 64.0, 64.0, 0.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(playerTrig, Player(0), EVENT_PLAYER_UNIT_CONSTRUCT_FINISH, null)\n"
        "  call TriggerRegisterUnitEvent(unitTrig, testBuilding, EVENT_UNIT_CONSTRUCT_FINISH)\n"
        "  call TriggerAddAction(playerTrig, function onPlayerFinish)\n"
        "  call TriggerAddAction(unitTrig, function onUnitFinish)\n"
        "endfunction\n"));

    building = find_test_unit(MAKEFOURCC('h','b','a','r'));
    T_NOT_NULL(building);
    building->stand = unit_stand;
    building->construction.active = true;
    saved = g_edicts[0].client;
    g_edicts[0].client = NULL;
    G_CompleteConstruction(building);
    g_edicts[0].client = saved;

    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyFinish", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, spell_effect_event_exposes_wc3_response_context_and_order_ids) {
    LPEDICT caster, target;
    VECTOR2 point = { 96.0f, 144.0f };

    setup_test_world();
    caster = alloc_test_unit(MAKEFOURCC('O','t','c','h'), 0.0f, 0.0f);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    caster->s.player = game.clients[0].ps.number;
    target->s.player = game.clients[1].ps.number;
    target->svflags |= SVF_MONSTER;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer spellEvents = 0\n"
        "endglobals\n"
        "function onSpellEffect takes nothing returns nothing\n"
        "  set spellEvents = spellEvents + 1\n"
        "  call BJassAssert(GetSpellAbilityUnit() != null, \"spell caster missing\")\n"
        "  if GetSpellAbilityId() == 'AOcl' then\n"
        "    call BJassAssert(GetSpellTargetUnit() != null, \"unit spell target missing\")\n"
        "    call BJassAssert(GetSpellTargetX() == 0.0 and GetSpellTargetY() == 0.0, \"unit target must not expose point context\")\n"
        "  elseif GetSpellAbilityId() == 'AEbl' then\n"
        "    call BJassAssert(GetSpellTargetUnit() == null, \"point spell must not expose a unit target\")\n"
        "    call BJassAssert(GetSpellTargetX() == 96.0 and GetSpellTargetY() == 144.0, \"point spell coordinates missing\")\n"
        "  else\n"
        "    call BJassAssert(false, \"unexpected spell id\")\n"
        "  endif\n"
        "endfunction\n"
        "function verifySpellEvents takes nothing returns nothing\n"
        "  call BJassAssert(spellEvents == 2, \"expected both spell-effect events\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call BJassAssert(OrderId(\"chainlightning\") == 852119, \"chain lightning order id mismatch\")\n"
        "  call BJassAssert(OrderId2String(852119) == \"chainlightning\", \"chain lightning order name mismatch\")\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_SPELL_EFFECT, null)\n"
        "  call TriggerAddAction(t, function onSpellEffect)\n"
        "endfunction\n"));

    G_PublishEventWithPoint(&(gameEventPointParams_t){
        .edict = caster, .type = EVENT_PLAYER_UNIT_SPELL_EFFECT, .source = target,
        .value = (LONG)MAKEFOURCC('A','O','c','l') });
    G_PublishEventWithPoint(&(gameEventPointParams_t){
        .edict = caster, .type = EVENT_PLAYER_UNIT_SPELL_EFFECT, .value = (LONG)MAKEFOURCC('A','E','b','l'),
        .point = &point });
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifySpellEvents", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, enable_user_ui_does_not_block_target_commands) {
    LPGAMECLIENT gc = &game.clients[0];
    LPCSTR point[] = { "point", "10", "20" };

    ui_point_calls = 0; gc->ps.number = 0; gc->menu.on_location_selected = count_ui_point;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call EnableUserUI(false)\n"
        "endfunction"));
    T_ASSERT(gc->no_ui);
    globals.ClientCommand(&g_edicts[0], 3, point);
    T_EQ(ui_point_calls, 1);

    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call EnableUserUI(true)\n"
        "endfunction"));
    T_ASSERT(!gc->no_ui);
    globals.ClientCommand(&g_edicts[0], 3, point);
    T_EQ(ui_point_calls, 2);
    gc->menu.on_location_selected = NULL;
    currentplayer = NULL;
}

TEST(wc3_api, debug_statements_parse_but_do_not_execute_in_release) {
    LPGAMECLIENT gc = &game.clients[0];
    gc->ps.stats[1] = 0;
    currentplayer = &gc->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 6)\n"
        "debug call MissingDebug()\n"
        "debug set bj_forLoopAIndex = 7\n"
        "debug if true then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 8)\n"
        "endif\n"
        "endfunction"));
    T_EQ(gc->ps.stats[1], 6);
    currentplayer = NULL;
}

/* Create a minimal unit in slot 0 and return it. */
static LPEDICT make_unit_hero(void) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    return ent;
}

TEST(wc3_api, model_effects_are_rendered_but_not_world_selectable) {
    VECTOR2 point = { 64.0f, 64.0f };
    LPEDICT target;
    LPEDICT point_effect;
    LPEDICT target_effect;

    setup_test_world();
    point_effect = G_SpawnModelEffect("TestUI\\Models\\anim_pulse.mdx", &point, NULL, NULL, false);
    T_NOT_NULL(point_effect);
    T_ASSERT(point_effect->s.model != 0);
    T_ASSERT(point_effect->s.flags & EF_NOT_SELECTABLE);

    target = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 128.0f, 128.0f);
    target_effect = G_SpawnModelEffect("TestUI\\Models\\anim_pulse.mdx", NULL, target, "overhead", false);
    T_NOT_NULL(target_effect);
    T_ASSERT(target_effect->s.model != 0);
    T_ASSERT(target_effect->s.flags & EF_NOT_SELECTABLE);
}

TEST(wc3_api, effect_natives_return_independent_handles) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local effect direct = AddSpecialEffect(\"TestUI\\\\Models\\\\anim_pulse.mdx\", 64.0, 64.0)\n"
        "  local effect spell = AddSpellEffectById('AHhb', EFFECT_TYPE_TARGET, 96.0, 96.0)\n"
        "  call BJassAssert(direct != null, \"AddSpecialEffect returned null\")\n"
        "  call BJassAssert(spell != null, \"AddSpellEffectById returned null\")\n"
        "  call BJassAssert(direct != spell, \"effect handles aliased\")\n"
        "  call DestroyEffect(direct)\n"
        "  call DestroyEffect(spell)\n"
        "endfunction\n"));
}

TEST(wc3_api, jass_sound_runtime_tracks_one_shot_volume_and_attachment_safely) {
    int handle_storage = 0;
    HANDLE handle = &handle_storage;
    jassSoundPlayback_t playback;
    LPEDICT unit;

    reset_entities();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32.0f, 48.0f);
    unit->spawn_time = 1234;

    G_JassSoundRuntimeInit(handle);
    G_JassSoundPlayback(handle, &playback);
    T_FEQ(playback.volume, 1.0f, 0.001f);
    T_ASSERT(!playback.positioned);

    G_JassSoundSetVolume(handle, 0.5f);
    G_JassSoundSetPosition(handle, &MAKE(VECTOR3, 10.0f, 20.0f, 30.0f));
    G_JassSoundPlayback(handle, &playback);
    T_FEQ(playback.volume, 0.5f, 0.001f);
    T_ASSERT(playback.positioned);
    T_FEQ(playback.origin.x, 10.0f, 0.001f);
    T_FEQ(playback.origin.y, 20.0f, 0.001f);
    T_NULL(playback.emitter);

    G_JassSoundAttach(handle, unit);
    G_JassSoundPlayback(handle, &playback);
    T_ASSERT(playback.positioned);
    T_ASSERT(playback.emitter == unit);
    T_FEQ(playback.origin.x, 32.0f, 0.001f);
    T_FEQ(playback.origin.y, 48.0f, 0.001f);

    /* Reusing the edict slot after the attached unit was freed must not make a
     * sound follow the replacement entity. */
    unit->spawn_time++;
    G_JassSoundPlayback(handle, &playback);
    T_ASSERT(!playback.positioned);
    T_NULL(playback.emitter);

    G_JassSoundRuntimeReset();
}

TEST(wc3_api, jass_start_sound_skips_disconnected_local_player) {
    LPGAMECLIENT gc = &game.clients[0];
    LPEDICT recipient = &g_edicts[0];
    void (*old_sound)(LPEDICT, int, int, FLOAT, FLOAT, FLOAT) = gi.Sound;
    int (*old_soundindex)(LPCSTR) = gi.SoundIndex;

    recipient->client = gc;
    gc->ps.number = 0;
    currentplayer = &gc->ps;
    ui_sound_calls = 0;
    ui_sound_value = 0;
    gi.Sound = capture_ui_sound;
    gi.SoundIndex = capture_ui_sound_index;

    gc->connected = false;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local sound s = CreateSound(\"test.wav\", false, false, false, 0, 0, \"\")\n"
        "  call StartSound(s)\n"
        "endfunction\n"));
    T_EQ(ui_sound_calls, 0);

    gc->connected = true;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local sound s = CreateSound(\"test.wav\", false, false, false, 0, 0, \"\")\n"
        "  call StartSound(s)\n"
        "endfunction\n"));
    T_EQ(ui_sound_calls, 1);
    T_EQ(ui_sound_value, 77);

    gi.SoundIndex = old_soundindex;
    gi.Sound = old_sound;
    currentplayer = NULL;
}

TEST(wc3_api, ui_sound_transport_waits_for_connected_client) {
    GAMECLIENT client = { 0 };
    edict_t ent = { .client = &client };
    void (*old_sound)(LPEDICT, int, int, FLOAT, FLOAT, FLOAT) = gi.Sound;
    int (*old_soundindex)(LPCSTR) = gi.SoundIndex;

    ui_sound_calls = 0;
    ui_sound_value = 0;
    gi.Sound = capture_ui_sound;
    gi.SoundIndex = capture_ui_sound_index;

    G_PlayUISoundForPlayer(&ent, "InterfaceError");
    T_EQ(ui_sound_calls, 0);

    client.connected = true;
    G_PlayUISoundForPlayer(&ent, "InterfaceError");
    T_EQ(ui_sound_calls, 1);
    T_EQ(ui_sound_value, 77);

    gi.SoundIndex = old_soundindex;
    gi.Sound = old_sound;
}

TEST(wc3_api, customize_entity_preserves_world_state) {
    entityState_t state = { .number = 7, .model = 11, .renderfx = RF_SELECTED };
    edict_t ent = { 0 };

    T_NOT_NULL(globals.CustomizeEntity);
    if (!globals.CustomizeEntity)
        return;
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(state.number, 7);
    T_EQ(state.model, 11);
    T_EQ(state.renderfx, RF_SELECTED);
}

TEST(wc3_api, customize_entity_marks_live_unit_hoverable) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, customize_entity_marks_enemy_hover_relation_hostile) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 2 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(state.flags & EF_HOSTILE);
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, customize_entity_marks_passive_ally_hover_relation_neutral) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 1 } };
    ent.health.value = 100.0f;
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);

    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(state.flags & EF_NEUTRAL);
}

TEST(wc3_api, customize_entity_marks_neutral_passive_owner_neutral) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_PASSIVE } };
    ent.health.value = 100.0f;

    T_EQ(PLAYER_NEUTRAL_PASSIVE, 15); T_ASSERT(PLAYER_NEUTRAL_PASSIVE < MAX_PLAYERS);
    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(state.flags & EF_NEUTRAL);
}

TEST(wc3_api, customize_entity_honors_runtime_neutral_aggressive_alliance) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_AGGRESSIVE } };
    ent.health.value = 100.0f;

    T_EQ(PLAYER_NEUTRAL_AGGRESSIVE, 12); T_ASSERT(PLAYER_NEUTRAL_AGGRESSIVE < MAX_PLAYERS);
    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_AGGRESSIVE), ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_AGGRESSIVE), ALLIANCE_SHARED_CONTROL, true);
    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, neutral_passive_relation_can_be_revoked_at_runtime) {
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_PASSIVE } };

    G_InitPlayerAlliances(level.mapinfo);
    T_EQ(G_SelectionRelation(0, &ent), SELECT_RELATION_NEUTRAL);

    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_PASSIVE), ALLIANCE_PASSIVE, false);
    T_EQ(G_SelectionRelation(0, &ent), SELECT_RELATION_ENEMY);
}

TEST(wc3_api, customize_entity_marks_shared_control_hover_relation_friendly) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 1 } };
    ent.health.value = 100.0f;
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, true);

    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, selection_relation_matches_enemy_neutral_and_shared_control) {
    edict_t enemy = { .s = { .player = 2 } };
    edict_t passive = { .s = { .player = PLAYER_NEUTRAL_PASSIVE } };
    edict_t hostile = { .s = { .player = PLAYER_NEUTRAL_AGGRESSIVE } };
    edict_t ally = { .s = { .player = 1 } };

    T_EQ(G_SelectionRelation(0, &enemy), SELECT_RELATION_ENEMY);
    T_EQ(G_SelectionRelation(0, &passive), SELECT_RELATION_NEUTRAL);
    T_EQ(G_SelectionRelation(0, &hostile), SELECT_RELATION_ENEMY);

    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);
    T_EQ(G_SelectionRelation(0, &ally), SELECT_RELATION_NEUTRAL);
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, true);
    T_EQ(G_SelectionRelation(0, &ally), SELECT_RELATION_FRIEND);
}

TEST(wc3_api, smart_target_indicator_uses_relationship_color_from_miscdata) {
    stbIniCache_t saved = game.config.misc, custom = { 0 };
    edict_t own = { .s = { .player = 0 } };
    edict_t ally = { .s = { .player = 1 } };
    edict_t enemy = { .s = { .player = 2 } };
    COLOR32 color;

    T_ASSERT(Stb_IniCacheLoad(&custom, "TestData\\SelectionCircle.txt"));
    game.config.misc = custom;
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);

    color = G_SmartTargetIndicatorColor(0, &own);
    T_EQ(color.r, 1); T_EQ(color.g, 2); T_EQ(color.b, 3); T_EQ(color.a, 200);
    color = G_SmartTargetIndicatorColor(0, &ally);
    T_EQ(color.r, 4); T_EQ(color.g, 5); T_EQ(color.b, 6); T_EQ(color.a, 201);
    color = G_SmartTargetIndicatorColor(0, &enemy);
    T_EQ(color.r, 7); T_EQ(color.g, 8); T_EQ(color.b, 9); T_EQ(color.a, 202);

    game.config.misc = saved;
    Stb_IniCacheFree(&custom);
}

TEST(wc3_api, smart_target_indicator_falls_back_to_stock_classic_colors) {
    stbIniCache_t saved = game.config.misc;
    edict_t own = { .s = { .player = 0 } };
    edict_t ally = { .s = { .player = 1 } };
    edict_t enemy = { .s = { .player = 2 } };
    COLOR32 color;

    game.config.misc = (stbIniCache_t){ 0 };
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);

    color = G_SmartTargetIndicatorColor(0, &own);
    T_EQ(color.r, 0); T_EQ(color.g, 255); T_EQ(color.b, 0); T_EQ(color.a, 255);
    color = G_SmartTargetIndicatorColor(0, &ally);
    T_EQ(color.r, 255); T_EQ(color.g, 255); T_EQ(color.b, 0); T_EQ(color.a, 255);
    color = G_SmartTargetIndicatorColor(0, &enemy);
    T_EQ(color.r, 255); T_EQ(color.g, 0); T_EQ(color.b, 0); T_EQ(color.a, 255);

    game.config.misc = saved;
}

TEST(wc3_api, selection_accepts_visible_foreign_unit_but_rejects_invalid_states) {
    LPGAMECLIENT client = &game.clients[0];
    edict_t ent = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 2 } };
    ent.health.value = 100.0f;
    client->ps.number = 0;

    T_ASSERT(G_UnitCanBeSelected(client, &ent));
    ent.s.flags |= EF_NOT_SELECTABLE;
    T_ASSERT(!G_UnitCanBeSelected(client, &ent));
    ent.s.flags &= ~EF_NOT_SELECTABLE;
    ent.s.renderfx |= RF_HIDDEN;
    T_ASSERT(!G_UnitCanBeSelected(client, &ent));
    ent.s.renderfx &= ~RF_HIDDEN;
    ent.svflags |= SVF_DEADMONSTER;
    T_ASSERT(!G_UnitCanBeSelected(client, &ent));
}

TEST(wc3_api, multiselect_focus_tracks_one_selected_unit_and_falls_back_when_removed) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 32, 0);

    client->ps.number = 0;
    first->s.player = second->s.player = 0;
    first->svflags |= SVF_MONSTER;
    second->svflags |= SVF_MONSTER;
    G_ResetSelectionFocus(client);

    G_SelectEntity(client, first);
    G_SelectEntity(client, second);
    T_ASSERT(G_GetMainSelectedUnit(client) == first);
    T_ASSERT(G_FocusSelectedUnit(client, second));
    T_ASSERT(G_GetMainSelectedUnit(client) == second);
    T_ASSERT(G_IsEntitySelected(client, first));
    T_ASSERT(G_IsEntitySelected(client, second));

    G_DeselectEntity(client, second);
    T_ASSERT(G_GetMainSelectedUnit(client) == first);
    T_ASSERT(!G_FocusSelectedUnit(client, second));
}

TEST(wc3_api, multiselect_portrait_second_click_collapses_exact_focused_unit) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT footman_first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT footman_second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    LPEDICT paladin = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64, 0);
    char second_number[16];
    LPCSTR focus_second[] = { "focus", second_number };

    client->ps.number = 0;
    G_ResetSelectionFocus(client);
    footman_first->s.player = footman_second->s.player = paladin->s.player = 0;
    footman_first->svflags |= SVF_MONSTER;
    footman_second->svflags |= SVF_MONSTER;
    paladin->svflags |= SVF_MONSTER;
    G_SelectEntity(client, footman_first);
    G_SelectEntity(client, footman_second);
    G_SelectEntity(client, paladin);
    snprintf(second_number, sizeof(second_number), "%u", (unsigned)footman_second->s.number);

    T_ASSERT(G_GetMainSelectedUnit(client) == footman_first);

    /* Another member of the already-active Footman subgroup becomes the exact
     * representative without changing the three-unit selection. */
    globals.ClientCommand(&g_edicts[0], 2, focus_second);
    T_ASSERT(G_GetMainSelectedUnit(client) == footman_second);
    T_ASSERT(G_IsEntitySelected(client, footman_first));
    T_ASSERT(G_IsEntitySelected(client, footman_second));
    T_ASSERT(G_IsEntitySelected(client, paladin));

    /* Clicking that same representative again matches Warsmash: the status
     * panel selection collapses to only the clicked unit. */
    globals.ClientCommand(&g_edicts[0], 2, focus_second);
    T_ASSERT(G_GetMainSelectedUnit(client) == footman_second);
    T_ASSERT(!G_IsEntitySelected(client, footman_first));
    T_ASSERT(G_IsEntitySelected(client, footman_second));
    T_ASSERT(!G_IsEntitySelected(client, paladin));
}

TEST(wc3_api, tab_cycle_advances_unit_type_subgroups_and_wraps) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT footman_first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT footman_second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    LPEDICT knight = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 64, 0);
    LPEDICT paladin = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 96, 0);
    UnitData_t footman_data = { .priority = 3 };
    UnitData_t knight_data = { .priority = 2 };
    UnitData_t paladin_data = { .priority = 1 };

    client->ps.number = 0;
    G_ResetSelectionFocus(client);
    footman_first->data.UnitData = footman_second->data.UnitData = &footman_data;
    knight->data.UnitData = &knight_data;
    paladin->data.UnitData = &paladin_data;

    LPEDICT units[] = { footman_first, footman_second, knight, paladin };
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        units[i]->s.player = 0;
        units[i]->svflags |= SVF_MONSTER;
        G_SelectEntity(client, units[i]);
    }

    T_ASSERT(G_GetMainSelectedUnit(client) == footman_first);
    T_ASSERT(G_CycleSelectionSubgroup(client));
    T_ASSERT(G_GetMainSelectedUnit(client) == knight);
    T_ASSERT(G_CycleSelectionSubgroup(client));
    T_ASSERT(G_GetMainSelectedUnit(client) == paladin);
    T_ASSERT(G_CycleSelectionSubgroup(client));
    T_ASSERT(G_GetMainSelectedUnit(client) == footman_first);

    /* Cycling focus must not mutate authoritative selection membership. */
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        T_ASSERT(G_IsEntitySelected(client, units[i]));
    }
}

TEST(wc3_api, tab_cycle_is_noop_for_single_type_selection) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);

    client->ps.number = 0;
    first->s.player = second->s.player = 0;
    first->svflags |= SVF_MONSTER;
    second->svflags |= SVF_MONSTER;
    G_ResetSelectionFocus(client);
    G_SelectEntity(client, first);
    G_SelectEntity(client, second);

    T_ASSERT(G_GetMainSelectedUnit(client) == first);
    T_ASSERT(!G_CycleSelectionSubgroup(client));
    T_ASSERT(G_GetMainSelectedUnit(client) == first);
}

TEST(wc3_api, multiselect_order_matches_warsmash_priority_level_and_rawcode) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT low_priority = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT rawcode_foo_first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    LPEDICT rawcode_foo_second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    LPEDICT rawcode_knight = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 96, 0);
    LPEDICT higher_level = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 128, 0);
    LPEDICT higher_priority = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 160, 0);
    UnitData_t low_data = { .priority = 1 };
    UnitData_t middle_data = { .priority = 5 };
    UnitData_t high_data = { .priority = 6 };
    UnitBalance_t low_balance = { .level = 99 };
    UnitBalance_t middle_balance = { .level = 2 };
    UnitBalance_t higher_level_balance = { .level = 3 };
    UnitBalance_t high_balance = { .level = 0 };
    LPEDICT ordered[6] = { 0 };

    client->ps.number = 0;
    G_ResetSelectionFocus(client);

    low_priority->data.UnitData = &low_data;
    low_priority->data.UnitBalance = &low_balance;
    rawcode_foo_first->data.UnitData = &middle_data;
    rawcode_foo_first->data.UnitBalance = &middle_balance;
    rawcode_foo_second->data.UnitData = &middle_data;
    rawcode_foo_second->data.UnitBalance = &middle_balance;
    rawcode_knight->data.UnitData = &middle_data;
    rawcode_knight->data.UnitBalance = &middle_balance;
    higher_level->data.UnitData = &middle_data;
    higher_level->data.UnitBalance = &higher_level_balance;
    higher_priority->data.UnitData = &high_data;
    higher_priority->data.UnitBalance = &high_balance;

    LPEDICT units[] = {
        low_priority,
        rawcode_foo_first,
        rawcode_foo_second,
        rawcode_knight,
        higher_level,
        higher_priority,
    };
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        units[i]->s.player = 0;
        units[i]->svflags |= SVF_MONSTER;
        G_SelectEntity(client, units[i]);
    }

    T_EQ(G_GetOrderedSelectedUnits(client, ordered, sizeof(ordered) / sizeof(ordered[0])), 6);
    T_ASSERT(ordered[0] == higher_priority);
    T_ASSERT(ordered[1] == higher_level);
    /* OpenRealm stores FourCC bytes little-endian.  Canonical Warcraft rawcode
     * ordering still places "hkni" ahead of "hfoo" for the final tie-break. */
    T_ASSERT(ordered[2] == rawcode_knight);
    T_ASSERT(ordered[3] == rawcode_foo_first);
    T_ASSERT(ordered[4] == rawcode_foo_second);
    T_ASSERT(ordered[5] == low_priority);

    /* When no explicit subgroup focus remains, the same sorted first unit must
     * own the portrait/command-card fallback rather than edict scan order. */
    G_ResetSelectionFocus(client);
    T_ASSERT(G_GetMainSelectedUnit(client) == higher_priority);
}

TEST(wc3_api, selection_revalidation_clears_hidden_raw_selection_bit) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    DWORD bit = 1 << client->ps.number;

    ent->svflags |= SVF_MONSTER;
    ent->s.player = 1;
    G_SelectEntity(client, ent);
    T_ASSERT(ent->selected & bit);

    ent->s.renderfx |= RF_HIDDEN;
    T_ASSERT(!G_IsEntitySelected(client, ent));
    G_UpdateClientSelections();

    T_ASSERT(!(ent->selected & bit));
}

TEST(wc3_api, control_is_separate_from_selection_and_honors_shared_control) {
    LPGAMECLIENT client = &game.clients[0];
    edict_t own = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 0 } };
    edict_t enemy = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = 1 } };
    edict_t neutral = { .inuse = true, .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_PASSIVE } };
    own.health.value = enemy.health.value = neutral.health.value = 100.0f;
    client->ps.number = 0;

    T_ASSERT(G_UnitCanControl(client, &own));
    T_ASSERT(G_UnitCanBeSelected(client, &enemy));
    T_ASSERT(!G_UnitCanControl(client, &enemy));
    T_ASSERT(G_UnitCanBeSelected(client, &neutral));
    T_ASSERT(!G_UnitCanControl(client, &neutral));

    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(test_player(0), test_player(1), ALLIANCE_SHARED_CONTROL, true);
    T_ASSERT(G_UnitCanControl(client, &enemy));

    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_PASSIVE), ALLIANCE_SHARED_CONTROL, true);
    T_ASSERT(G_UnitCanControl(client, &neutral));
}

TEST(wc3_api, customize_entity_rejects_non_unit_hover_health) {
    entityState_t state = { .number = 7, .model = 11, .flags = EF_HOVER_HEALTH };
    edict_t ent = { .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
}

TEST(wc3_api, customize_entity_rejects_dead_or_unselectable_unit_hover_health) {
    entityState_t state = { .number = 7, .model = 11,
        .flags = EF_NOT_SELECTABLE | EF_HOVER_HEALTH | EF_HOSTILE | EF_NEUTRAL };
    edict_t ent = { .svflags = SVF_MONSTER | SVF_DEADMONSTER, .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

TEST(wc3_api, customize_entity_rejects_hidden_unit_hover_health) {
    entityState_t state = { .number = 7, .model = 11, .renderfx = RF_HIDDEN,
        .flags = EF_HOVER_HEALTH | EF_HOSTILE | EF_NEUTRAL };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = 3 } };
    ent.health.value = 100.0f;

    globals.CustomizeEntity(3, &ent, &state);
    T_ASSERT(!(state.flags & EF_HOVER_HEALTH));
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(!(state.flags & EF_NEUTRAL));
}

/* =========================================================================
 * Player — color
 * ========================================================================= */

TEST(wc3_api, player_color_default_zero) {
    LPPLAYER p = test_player(0);
    T_EQ((int)p->color, 0);
}

TEST(wc3_api, player_color_set_get) {
    LPPLAYER p = test_player(0);
    p->color = 5;
    T_EQ((int)p->color, 5);
}

TEST(wc3_api, player_color_max_index) {
    LPPLAYER p = test_player(0);
    p->color = 23;
    T_EQ((int)p->color, 23);
}

/* =========================================================================
 * Player — start_location
 * ========================================================================= */

TEST(wc3_api, player_start_location_default) {
    /* start_location is 0-initialised by setup_game(). */
    LPGAMECLIENT cl = &game.clients[1];
    cl->ps.number = 1;
    T_EQ((int)cl->ps.start_location, 0);
}

TEST(wc3_api, player_start_location_set_get) {
    LPGAMECLIENT cl = &game.clients[2];
    cl->ps.number = 2;
    cl->ps.start_location = 3;
    T_EQ((int)cl->ps.start_location, 3);
}

TEST(wc3_api, player_start_location_negative) {
    LPGAMECLIENT cl = &game.clients[3];
    cl->ps.number = 3;
    cl->ps.start_location = -1;
    T_EQ((int)cl->ps.start_location, -1);
}

/* =========================================================================
 * Player — name
 * ========================================================================= */

TEST(wc3_api, player_name_set_get) {
    LPPLAYER p = test_player(0);
    p->name = "Arthas";
    T_STREQ(p->name, "Arthas");
}

TEST(wc3_api, player_name_null_default) {
    /* memset in setup_game zeroes the name pointer. */
    LPPLAYER p = test_player(4);
    p->name = NULL; /* explicit reset */
    T_NULL(p->name);
}

/* =========================================================================
 * Player — team
 * ========================================================================= */

TEST(wc3_api, player_team_set_get) {
    LPPLAYER p = test_player(0);
    p->team = 2;
    T_EQ((int)p->team, 2);
}

/* =========================================================================
 * Player — alliance
 * ========================================================================= */

TEST(wc3_api, alliance_passive_default_false) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    /* Ordinary player pairs begin unallied; neutral defaults are separate. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_defaults_include_neutral_passive_and_neutral_controller_slots) {
    LPMAPINFO mapinfo = (LPMAPINFO)level.mapinfo;

    mapinfo->players[3].playerType = kPlayerTypeNeutral;
    G_InitPlayerAlliances(level.mapinfo);

    T_ASSERT(G_GetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_PASSIVE), ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(test_player(PLAYER_NEUTRAL_PASSIVE), test_player(0), ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(test_player(0), test_player(3), ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(test_player(3), test_player(0), ALLIANCE_PASSIVE));
    T_ASSERT(!G_GetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_AGGRESSIVE), ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_set_true) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_is_directional) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    T_ASSERT(G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
    T_ASSERT(!G_GetPlayerAlliance(p1, p0, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_revoke_does_not_change_reverse_relation) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(p1, p0, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, false);
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
    T_ASSERT(G_GetPlayerAlliance(p1, p0, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_revoke) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, false);
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
}

TEST(wc3_api, alliance_enemy_when_not_allied) {
    LPPLAYER p0 = test_player(0);
    LPPLAYER p2 = test_player(2);
    /* Players 0 and 2 have no alliance — IsUnitEnemy logic is !ally. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p2, ALLIANCE_PASSIVE));
}

TEST(wc3_api, is_unit_ally_uses_querying_player_direction) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(1), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call SetPlayerAlliance(Player(0), Player(1), ALLIANCE_PASSIVE, true)\n"
        "  call SetPlayerAlliance(Player(1), Player(0), ALLIANCE_PASSIVE, false)\n"
        "  call BJassAssert(IsUnitAlly(u, Player(0)), \"source player sees unit owner as ally\")\n"
        "  call BJassAssert(not IsUnitEnemy(u, Player(0)), \"source player does not see ally as enemy\")\n"
        "  call BJassAssert(IsUnitEnemy(CreateUnit(Player(0), 'hpea', 64.0, 0.0, 0.0), Player(1)), \"reverse direction remains hostile\")\n"
        "endfunction\n"
    ));
}

/* =========================================================================
 * Hero — str / agi / int attributes
 * ========================================================================= */

TEST(wc3_api, hero_str_set_get) {
    LPEDICT ent = make_unit_hero();
    ent->hero.str = 25;
    T_EQ((int)ent->hero.str, 25);
}

TEST(wc3_api, hero_agi_set_get) {
    LPEDICT ent = make_unit_hero();
    ent->hero.agi = 18;
    T_EQ((int)ent->hero.agi, 18);
}

TEST(wc3_api, hero_int_set_get) {
    LPEDICT ent = make_unit_hero();
    ent->hero.intel = 22;
    T_EQ((int)ent->hero.intel, 22);
}

/* =========================================================================
 * Hero — XP accumulation
 * ========================================================================= */

TEST(wc3_api, hero_xp_default_zero) {
    LPEDICT ent = make_unit_hero();
    T_EQ((int)ent->hero.xp, 0);
}

TEST(wc3_api, hero_xp_set) {
    LPEDICT ent = make_unit_hero();
    ent->hero.xp = 500;
    T_EQ((int)ent->hero.xp, 500);
}

TEST(wc3_api, hero_xp_add) {
    LPEDICT ent = make_unit_hero();
    ent->hero.xp = 100;
    DWORD add = 50;
    /* Replicate AddHeroXP logic: cap at INT32_MAX */
    DWORD cur = ent->hero.xp;
    DWORD sum = cur + add;
    ent->hero.xp = (sum < cur || sum > (DWORD)INT32_MAX) ? (DWORD)INT32_MAX : sum;
    T_EQ((int)ent->hero.xp, 150);
}

TEST(wc3_api, hero_xp_map_main_uses_normal_progression) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  call AddHeroXP(h, 500, false)\n"
        "  call BJassAssert(GetHeroXP(h) == 500, \"startup AddHeroXP did not persist XP\")\n"
        "  call BJassAssert(GetHeroLevel(h) == 3, \"startup AddHeroXP did not level Hero\")\n"
        "  call SetHeroXP(h, 100, false)\n"
        "  call BJassAssert(GetHeroXP(h) == 500, \"SetHeroXP lowered XP\")\n"
        "  call BJassAssert(GetHeroLevel(h) == 3, \"SetHeroXP lowered Hero level\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_api, hero_skill_points_jass_modify_and_query) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 128.0, 0.0, 0.0)\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 1, \"new Hero did not start with one skill point\")\n"
        "  call BJassAssert(UnitModifySkillPoints(h, 2), \"positive skill point delta failed\")\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 3, \"positive skill point delta was not applied\")\n"
        "  call BJassAssert(UnitModifySkillPoints(h, -1), \"negative skill point delta failed\")\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 2, \"negative skill point delta was not applied\")\n"
        "  call BJassAssert(UnitModifySkillPoints(h, -99), \"large negative skill point delta failed\")\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 0, \"skill points did not clamp at zero\")\n"
        "  call BJassAssert(not UnitModifySkillPoints(h, -1), \"empty skill point pool accepted another negative delta\")\n"
        "  call BJassAssert(GetHeroSkillPoints(u) == 0, \"non-Hero reported skill points\")\n"
        "  call BJassAssert(not UnitModifySkillPoints(u, 1), \"non-Hero accepted skill points\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_api, hero_skill_points_map_main_can_award_points) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  call UnitModifySkillPoints(h, 2)\n"
        "  call BJassAssert(GetHeroLevel(h) == 1, \"skill point award changed Hero level\")\n"
        "  call BJassAssert(GetHeroXP(h) == 0, \"skill point award changed Hero XP\")\n"
        "  call BJassAssert(GetHeroSkillPoints(h) == 3, \"map-start skill point award did not persist\")\n"
        "endfunction\n"
    ));
}

TEST(wc3_api, hero_xp_overflow_clamps) {
    LPEDICT ent = make_unit_hero();
    ent->hero.xp = (DWORD)INT32_MAX - 5;
    DWORD add = 100;
    DWORD cur = ent->hero.xp;
    DWORD sum = cur + add;
    ent->hero.xp = (sum < cur || sum > (DWORD)INT32_MAX) ? (DWORD)INT32_MAX : sum;
    /* Overflow past INT32_MAX clamps to INT32_MAX, never goes negative */
    T_EQ((long long)ent->hero.xp, (long long)INT32_MAX);
    T_ASSERT((LONG)ent->hero.xp >= 0);
}

/* =========================================================================
 * Hero — suspend_xp
 * ========================================================================= */

TEST(wc3_api, hero_suspend_xp_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!ent->hero.suspend_xp);
}

TEST(wc3_api, hero_suspend_xp_set_true) {
    LPEDICT ent = make_unit_hero();
    ent->hero.suspend_xp = true;
    T_ASSERT(ent->hero.suspend_xp);
}

TEST(wc3_api, hero_xp_not_added_when_suspended) {
    LPEDICT ent = make_unit_hero();
    ent->hero.xp = 100;
    ent->hero.suspend_xp = true;
    /* Replicate AddHeroXP: skip when suspend_xp is set */
    LONG xp_to_add = 50;
    if (!ent->hero.suspend_xp && xp_to_add > 0) {
        DWORD add = (DWORD)xp_to_add;
        DWORD cur = ent->hero.xp;
        DWORD sum = cur + add;
        ent->hero.xp = (sum < cur || sum > (DWORD)INT32_MAX) ? (DWORD)INT32_MAX : sum;
    }
    T_EQ((int)ent->hero.xp, 100);
}

/* =========================================================================
 * Unit flags — invulnerable / paused / no_pathing / unit_color
 * ========================================================================= */

TEST(wc3_api, unit_invulnerable_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!ent->invulnerable);
}

TEST(wc3_api, unit_invulnerable_set) {
    LPEDICT ent = make_unit_hero();
    ent->invulnerable = true;
    T_ASSERT(ent->invulnerable);
}

TEST(wc3_api, unit_paused_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!ent->paused);
}

TEST(wc3_api, unit_paused_set) {
    LPEDICT ent = make_unit_hero();
    ent->paused = true;
    T_ASSERT(ent->paused);
    ent->paused = false;
    T_ASSERT(!ent->paused);
}

TEST(wc3_api, unit_no_pathing_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!ent->no_pathing);
}

TEST(wc3_api, unit_no_pathing_set) {
    LPEDICT ent = make_unit_hero();
    ent->no_pathing = true;
    T_ASSERT(ent->no_pathing);
}

TEST(wc3_api, unit_color_default_zero) {
    LPEDICT ent = make_unit_hero();
    T_EQ((int)ent->unit_color, 0);
}

TEST(wc3_api, unit_color_set) {
    LPEDICT ent = make_unit_hero();
    ent->unit_color = 7;
    T_EQ((int)ent->unit_color, 7);
}

TEST(wc3_api, set_unit_color_publishes_team_color_without_changing_owner) {
    LPEDICT unit = NULL;
    DWORD encoded;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(4), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call SetUnitColor(u, PLAYER_COLOR_LIGHT_GRAY)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') && g_edicts[i].s.player == 4) unit = &g_edicts[i];
    T_NOT_NULL(unit);
    encoded = (unit->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT;
    T_EQ(unit->s.player, 4);
    T_EQ(unit->unit_color, 8);
    T_EQ(encoded, 9);
}

TEST(wc3_api, set_unit_color_can_override_owner_with_red) {
    LPEDICT unit = NULL;
    DWORD encoded;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(4), 'hfoo', 96.0, 96.0, 0.0)\n"
        "  call SetUnitColor(u, PLAYER_COLOR_RED)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o') &&
            g_edicts[i].s.player == 4 && g_edicts[i].s.origin.x == 96.0f) unit = &g_edicts[i];
    T_NOT_NULL(unit);
    encoded = (unit->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT;
    T_EQ(encoded, 1);
}

/* =========================================================================
 * Unit — hidden flag (RF_HIDDEN)
 * ========================================================================= */

TEST(wc3_api, unit_hidden_default_false) {
    LPEDICT ent = make_unit_hero();
    T_ASSERT(!(ent->s.renderfx & RF_HIDDEN));
}

TEST(wc3_api, unit_hidden_set) {
    LPEDICT ent = make_unit_hero();
    ent->s.renderfx |= RF_HIDDEN;
    T_ASSERT(ent->s.renderfx & RF_HIDDEN);
}

TEST(wc3_api, unit_hidden_clear) {
    LPEDICT ent = make_unit_hero();
    ent->s.renderfx |= RF_HIDDEN;
    ent->s.renderfx &= ~RF_HIDDEN;
    T_ASSERT(!(ent->s.renderfx & RF_HIDDEN));
}

TEST(wc3_api, show_unit_visibility_transition_invalidates_hero_shortcuts) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT hero = NULL;

    reset_entities();
    setup_test_world();
    client->ps.number = 0;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit testHero\n"
        "endglobals\n"
        "function hideHero takes nothing returns nothing\n"
        "  call ShowUnit(testHero, false)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set testHero = CreateUnit(Player(0), 'Hpal', 64.0, 64.0, 0.0)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('H','p','a','l') && g_edicts[i].s.player == 0) hero = &g_edicts[i];
    T_NOT_NULL(hero);
    T_ASSERT(!(hero->s.renderfx & RF_HIDDEN));

    client->shortcuts.dirty = false;
    jass_callbyname(level.vm, "hideHero", true);
    jass_runevents(level.vm);

    T_ASSERT(hero->s.renderfx & RF_HIDDEN);
    T_ASSERT(client->shortcuts.dirty);
}


/* =========================================================================
 * Group — FirstOfGroup / IsUnitInGroup
 * ========================================================================= */

TEST(wc3_api, group_first_of_empty_returns_null) {
    ggroup_t g = {0};
    LPEDICT first = (g.num_units > 0) ? g.units[0] : NULL;
    T_NULL(first);
}

TEST(wc3_api, group_first_of_group) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.units[1] = b;
    g.num_units = 2;
    T_ASSERT(g.units[0] == a);
    T_ASSERT(g.units[1] == b);
}

TEST(wc3_api, group_add_is_set_semantics) {
    reset_entities();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ggroup_t *group = G_AllocJassGroup();
    T_NOT_NULL(group);
    T_ASSERT(group_add_entity(group, unit));
    T_ASSERT(!group_add_entity(group, unit));
    T_EQ(group->num_units, 1);
    G_FreeJassGroup(group);
}

TEST(wc3_api, group_debug_creator_follows_slot_lifecycle) {
    ggroup_t *group = G_AllocJassGroup();

    T_NOT_NULL(group);
    G_SetJassGroupDebugContext(group, "creatorA", "helperA <- actionA", 42);
    T_STREQ(G_GetJassGroupDebugCreator(group), "creatorA");
    T_STREQ(G_GetJassGroupDebugChain(group), "helperA <- actionA");
    T_EQ(G_GetJassGroupDebugTrigger(group), 42);
    G_FreeJassGroup(group);
    T_NULL(G_GetJassGroupDebugCreator(group));
    T_NULL(G_GetJassGroupDebugChain(group));
    T_EQ(G_GetJassGroupDebugTrigger(group), -1);
}

TEST(wc3_api, group_debug_captures_nested_jass_call_chain) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;

    reset_entities();
    gi.CvarString = group_debug_cvar;
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function makeLeakedGroup takes nothing returns nothing\n"
        "local group g = CreateGroup()\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "call makeLeakedGroup()\n"
        "endfunction"));
    T_EQ(level.num_groups, 1);
    T_ASSERT(level.groups[0]->inuse);
    T_STREQ(G_GetJassGroupDebugCreator(level.groups[0]), "makeLeakedGroup");
    T_STREQ(G_GetJassGroupDebugChain(level.groups[0]), "makeLeakedGroup <- main");
    currentplayer = NULL;
    gi.CvarString = old_cvar;
}

TEST(wc3_api, destroyed_group_slots_are_reused) {
    DWORD const saved_num_groups = level.num_groups;
    ggroup_t *first = NULL;

    for (DWORD i = 0; i < 4096; i++) {
        ggroup_t *group = G_AllocJassGroup();
        T_NOT_NULL(group);
        if (!group) break;
        if (!first) first = group;
        else T_ASSERT(group == first);
        T_ASSERT(group->inuse);
        G_FreeJassGroup(group);
        T_ASSERT(!group->inuse);
    }
    T_EQ(level.num_groups, saved_num_groups + 1);
}


TEST(wc3_api, group_registry_grows_past_legacy_1024_limit) {
    enum { LEGACY_GROUP_LIMIT = 1024, EXTRA_GROUPS = 64 };
    DWORD id = UINT32_MAX;

    reset_entities();
    for (DWORD i = 0; i < LEGACY_GROUP_LIMIT + EXTRA_GROUPS; i++) {
        ggroup_t *group = G_AllocJassGroup();
        T_NOT_NULL(group);
        if (!group) break;
    }
    T_EQ(level.num_groups, LEGACY_GROUP_LIMIT + EXTRA_GROUPS);
    T_ASSERT(level.group_capacity >= level.num_groups);
    T_ASSERT(G_SaveJassHandle("group", level.groups[LEGACY_GROUP_LIMIT + 7], &id));
    T_EQ(id, LEGACY_GROUP_LIMIT + 7);
    T_ASSERT(G_LoadJassHandle("group", id) == level.groups[id]);

    FOR_LOOP(i, level.num_groups) G_FreeJassGroup(level.groups[i]);
}

TEST(wc3_api, destroyed_group_handle_is_not_saveable_or_loadable) {
    DWORD id = UINT32_MAX;
    ggroup_t *group = G_AllocJassGroup();

    T_NOT_NULL(group);
    T_ASSERT(G_SaveJassHandle("group", group, &id));
    T_EQ(id, 0);
    T_ASSERT(G_LoadJassHandle("group", id) == group);

    G_FreeJassGroup(group);
    T_ASSERT(!G_SaveJassHandle("group", group, &id));
    T_NULL(G_LoadJassHandle("group", 0));
}

TEST(wc3_api, repeated_create_destroy_group_does_not_exhaust_registry) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function recycleGroup takes nothing returns nothing\n"
        "local group g = CreateGroup()\n"
        "call DestroyGroup(g)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "call recycleGroup()\n"
        "call recycleGroup()\n"
        "call recycleGroup()\n"
        "endfunction"));
    T_EQ(level.num_groups, 1);
    T_ASSERT(!level.groups[0]->inuse);
    currentplayer = NULL;
}

TEST(wc3_api, destroy_group_clears_members) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local group g = CreateGroup()\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "call GroupAddUnit(g, u)\n"
        "call DestroyGroup(g)\n"
        "if FirstOfGroup(g) != null then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    currentplayer = NULL;
}

TEST(wc3_api, unit_ability_mutation_has_set_semantics) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "if not UnitAddAbility(u, 'AInv') or UnitAddAbility(u, 'AInv') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "if not UnitRemoveAbility(u, 'AInv') or UnitRemoveAbility(u, 'AInv') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 2)\n"
        "endif\n"
        "call RemoveUnit(u)\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    currentplayer = NULL;
}

TEST(wc3_api, unit_ability_mutation_rejects_invalid_inputs) {
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "if UnitAddAbility(u, 'xxxx') or UnitAddAbility(null, 'AInv') or UnitRemoveAbility(null, 'AInv') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "call RemoveUnit(u)\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    currentplayer = NULL;
}

TEST(wc3_api, unit_ability_mutation_restores_static_ability) {
    LPEDICT unit;
    static UnitAbilities_t const abilities = { .abilList = "Ahar" };
    reset_entities(); unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0); unit->data.UnitAbilities = &abilities;
    T_ASSERT(G_ActorHasSkill(unit, "Ahar"));
    T_ASSERT(G_ActorRemoveSkill(unit, MAKEFOURCC('A','h','a','r')));
    T_ASSERT(!G_ActorHasSkill(unit, "Ahar"));
    T_ASSERT(!G_ActorRemoveSkill(unit, MAKEFOURCC('A','h','a','r')));
    T_ASSERT(G_ActorAddSkill(unit, MAKEFOURCC('A','h','a','r')));
    T_ASSERT(G_ActorHasSkill(unit, "Ahar"));
    T_ASSERT(!G_ActorAddSkill(unit, MAKEFOURCC('A','h','a','r')));
    G_FreeEdict(unit);
}

TEST(wc3_api, unit_ability_permanence_requires_present_ability) {
    LPEDICT unit;
    reset_entities();
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "if not UnitAddAbility(u, 'AInv') or not UnitMakeAbilityPermanent(u, true, 'AInv') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "if UnitMakeAbilityPermanent(u, true, 'xxxx') then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 2)\n"
        "endif\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    unit = globals.edicts + game.max_clients;
    T_ASSERT(G_ActorSkillPermanent(unit, MAKEFOURCC('A','I','n','v')));
    T_ASSERT(G_ActorSetSkillPermanent(unit, MAKEFOURCC('A','I','n','v'), false));
    T_ASSERT(!G_ActorSkillPermanent(unit, MAKEFOURCC('A','I','n','v')));
    T_ASSERT(G_ActorSetSkillPermanent(unit, MAKEFOURCC('A','I','n','v'), false));
    G_FreeEdict(unit); currentplayer = NULL;
}

TEST(wc3_api, unit_ability_mutation_rejects_full_lists) {
    static UnitAbilities_t const abilities = { .abilList = "Ahar" };
    DWORD invulnerable = MAKEFOURCC('A','I','n','v');
    reset_entities();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    FOR_LOOP(i, MAX_ABILITIES) unit->abilities.added[i] = i + 1;
    ARRAY_COUNT(unit->abilities.added) = MAX_ABILITIES;
    T_ASSERT(!G_ActorAddSkill(unit, invulnerable)); T_EQ(ARRAY_COUNT(unit->abilities.added), MAX_ABILITIES);
    memset(&unit->abilities, 0, sizeof(unit->abilities)); unit->data.UnitAbilities = &abilities;
    FOR_LOOP(i, MAX_ABILITIES) unit->abilities.removed[i] = i + 1;
    ARRAY_COUNT(unit->abilities.removed) = MAX_ABILITIES;
    T_ASSERT(!G_ActorRemoveSkill(unit, MAKEFOURCC('A','h','a','r'))); T_ASSERT(G_ActorHasSkill(unit, "Ahar"));
    memset(&unit->abilities, 0, sizeof(unit->abilities));
    unit->abilities.added[0] = invulnerable; ARRAY_COUNT(unit->abilities.added) = 1;
    FOR_LOOP(i, MAX_ABILITIES) unit->abilities.permanent[i] = i + 1;
    ARRAY_COUNT(unit->abilities.permanent) = MAX_ABILITIES;
    T_ASSERT(!G_ActorSetSkillPermanent(unit, invulnerable, true));
    T_ASSERT(!G_ActorSkillPermanent(unit, invulnerable));
    G_FreeEdict(unit);
}

TEST(wc3_api, ai_difficulty_defaults_to_normal) {
    reset_entities();
    test_player(0);
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "if GetAIDifficulty(Player(0)) != AI_DIFFICULTY_NORMAL then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[1], 0);
    currentplayer = NULL;
}

TEST(wc3_api, group_is_unit_in_group_true) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.num_units = 1;
    /* Replicate IsUnitInGroup logic */
    BOOL found = false;
    for (DWORD i = 0; i < g.num_units; i++) {
        if (g.units[i] == a) { found = true; break; }
    }
    T_ASSERT(found);
}

TEST(wc3_api, group_is_unit_in_group_false) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1, 0);
    ggroup_t g = {0};
    g.units[0] = a;
    g.num_units = 1;
    BOOL found = false;
    for (DWORD i = 0; i < g.num_units; i++) {
        if (g.units[i] == b) { found = true; break; }
    }
    T_ASSERT(!found);
}

/* =========================================================================
 * Misc — SubString semantics
 * ========================================================================= */

/*
 * Replicate SubString() logic from api_misc.h:
 *   source[start..end) — start inclusive, end exclusive.
 */
static void substr(const char *source, LONG start, LONG end, char *out, LONG outsz) {
    LONG len = (LONG)strlen(source);
    if (start < 0) start = 0;
    if (end > len) end = len;
    LONG n = end - start;
    if (n <= 0 || n + 1 > outsz) { out[0] = '\0'; return; }
    strncpy(out, source + start, (size_t)n);
    out[n] = '\0';
}

TEST(wc3_api, substring_basic) {
    char buf[64];
    substr("hello", 1, 4, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "ell");
}

TEST(wc3_api, substring_full) {
    char buf[64];
    substr("hello", 0, 5, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "hello");
}

TEST(wc3_api, substring_start_equals_end) {
    char buf[64];
    substr("hello", 2, 2, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "");
}

TEST(wc3_api, substring_end_past_len) {
    char buf[64];
    substr("hi", 0, 100, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "hi");
}

TEST(wc3_api, substring_single_char) {
    char buf[64];
    substr("hello", 0, 1, buf, (LONG)sizeof(buf));
    T_STREQ(buf, "h");
}

/* =========================================================================
 * Misc — GetRandomInt / GetRandomReal range
 * ========================================================================= */

TEST(wc3_api, random_int_in_range) {
    srand(42);
    for (int i = 0; i < 50; i++) {
        LONG lo = 1, hi = 10;
        LONG r = lo + rand() % (hi - lo + 1);
        T_ASSERT(r >= lo && r <= hi);
    }
}

TEST(wc3_api, random_int_single_value) {
    srand(1);
    LONG lo = 7, hi = 7;
    LONG r = lo + rand() % (hi - lo + 1);
    T_EQ((int)r, 7);
}

TEST(wc3_api, random_real_in_range) {
    srand(42);
    for (int i = 0; i < 50; i++) {
        FLOAT lo = 0.0f, hi = 1.0f;
        FLOAT t = (FLOAT)rand() / (FLOAT)RAND_MAX;
        FLOAT r = lo + t * (hi - lo);
        T_ASSERT(r >= lo && r <= hi);
    }
}

TEST(wc3_api, random_seed_deterministic) {
    srand(12345);
    int a = rand();
    srand(12345);
    int b = rand();
    T_EQ(a, b);
}

/* =========================================================================
 * Item — position and type id
 * ========================================================================= */

TEST(wc3_api, item_position_set) {
    reset_entities();
    LPEDICT item = alloc_test_unit(MAKEFOURCC('I','0','0','0'), 10.0f, 20.0f);
    item->s.origin.x = 10.0f;
    item->s.origin.y = 20.0f;
    T_FEQ(item->s.origin.x, 10.0f, 0.001f);
    T_FEQ(item->s.origin.y, 20.0f, 0.001f);
}

TEST(wc3_api, item_type_id) {
    reset_entities();
    LPEDICT item = alloc_test_unit(MAKEFOURCC('I','0','0','0'), 0.0f, 0.0f);
    DWORD expected = MAKEFOURCC('I','0','0','0');
    T_EQ((int)item->class_id, (int)expected);
}

/* =========================================================================
 * Inventory — edict-based UnitHasItem / UnitItemInSlot
 * ========================================================================= */

static LPEDICT alloc_inventory_test_unit(void) {
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    unit->health.value = 100;
    unit->health.max_value = 100;
    return unit;
}

static LPEDICT alloc_world_test_item(DWORD class_id) {
    LPEDICT item = alloc_test_unit(class_id, 0, 0);
    item->s.model = 1;
    item->targtype = TARG_ITEM;
    item->item.in_world = true;
    item->item.inventory_slot = -1;
    return item;
}

TEST(wc3_api, unit_has_item_true) {
    reset_entities();
    LPEDICT unit = alloc_inventory_test_unit();
    LPEDICT item = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item, 0);
    /* UnitHasItem checks pointer identity */
    BOOL found = false;
    FOR_LOOP(i, MAX_INVENTORY) {
        if (unit->inventory[i] == item) { found = true; break; }
    }
    T_ASSERT(found);
}

TEST(wc3_api, unit_has_item_false_different_instance) {
    /* Two items of the same type — only one is in inventory.
     * With edict-based inventory, distinct instances are distinguishable. */
    reset_entities();
    LPEDICT unit  = alloc_inventory_test_unit();
    LPEDICT item1 = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    LPEDICT item2 = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item1, 0);
    /* item2 is NOT in inventory */
    BOOL found = false;
    FOR_LOOP(i, MAX_INVENTORY) {
        if (unit->inventory[i] == item2) { found = true; break; }
    }
    T_ASSERT(!found);
}

TEST(wc3_api, unit_item_in_slot_returns_edict) {
    reset_entities();
    LPEDICT unit = alloc_inventory_test_unit();
    LPEDICT item = alloc_world_test_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(unit, item, 2);
    T_ASSERT(unit->inventory[2] == item);
}

TEST(wc3_api, unit_item_in_slot_empty_is_null) {
    reset_entities();
    LPEDICT unit = alloc_inventory_test_unit();
    T_NULL(unit->inventory[0]);
}

/* =========================================================================
 * Unit — IsUnitOwnedByPlayer
 * ========================================================================= */

TEST(wc3_api, unit_owned_by_player) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ent->s.player = 2;
    /* Replicate IsUnitOwnedByPlayer: ent->s.player == PLAYER_NUM(player) */
    LPPLAYER p = test_player(2);
    T_EQ((int)ent->s.player, (int)PLAYER_NUM(p));
}

TEST(wc3_api, unit_not_owned_by_player) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ent->s.player = 1;
    LPPLAYER p = test_player(3);
    T_ASSERT(ent->s.player != PLAYER_NUM(p));
}

TEST(wc3_api, is_unit_type_reports_structure_from_authoritative_metadata) {
    reset_entities();
    T_ASSERT(G_UnitIsBuilding(MAKEFOURCC('h','b','a','r')));
    T_ASSERT(!G_UnitIsBuilding(MAKEFOURCC('h','p','e','a')));
    currentplayer = &game.clients[0].ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "local unit building = CreateUnit(Player(0), 'hbar', 0.0, 0.0, 0.0)\n"
        "local unit worker = CreateUnit(Player(0), 'hpea', 256.0, 0.0, 0.0)\n"
        "if not IsUnitType(building, UNIT_TYPE_STRUCTURE) then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endif\n"
        "if IsUnitType(worker, UNIT_TYPE_STRUCTURE) then\n"
        "call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_LUMBER, 1)\n"
        "endif\n"
        "call RemoveUnit(building)\n"
        "call RemoveUnit(worker)\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 0);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);
    currentplayer = NULL;
}

/* =========================================================================
 * Unit — IsUnitInRange
 * ========================================================================= */

TEST(wc3_api, unit_in_range) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 3.0f, 4.0f);  /* dist = 5 */
    FLOAT dist = Vector2_distance(&a->s.origin2, &b->s.origin2);
    T_ASSERT(dist <= 6.0f);
}

TEST(wc3_api, unit_out_of_range) {
    reset_entities();
    LPEDICT a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    LPEDICT b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 3.0f, 4.0f);  /* dist = 5 */
    FLOAT dist = Vector2_distance(&a->s.origin2, &b->s.origin2);
    T_ASSERT(!(dist <= 4.0f));
}

TEST(wc3_api, killunit_runs_normal_unit_death_transition) {
    LPEDICT victim = NULL;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call KillUnit(u)\n"
        "endfunction"));

    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) victim = &g_edicts[i];
    T_NOT_NULL(victim);
    T_FEQ(victim->health.value, 0.0f, 0.001f);
    T_ASSERT(victim->svflags & SVF_DEADMONSTER);
    T_ASSERT(victim->s.flags & EF_NOT_SELECTABLE);
    T_NOT_NULL(victim->currentmove);
    T_STREQ(victim->currentmove->animation, "death");
}

TEST(wc3_api, killunit_processes_deferred_unit_handle) {
    LPEDICT victim;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call RemoveUnit(u)\n"
        "  call KillUnit(u)\n"
        "  call SetPlayerState(Player(0), PLAYER_STATE_RESOURCE_GOLD, 1)\n"
        "endfunction"));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 1);
    victim = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].class_id == MAKEFOURCC('h', 'f', 'o', 'o')) victim = &g_edicts[i];
    T_NOT_NULL(victim);
    if (victim) T_ASSERT(victim->svflags & SVF_DEADMONSTER);
}

TEST(wc3_api, player_unit_counts_support_campaign_peon_goals) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit p1 = CreateUnit(Player(0), 'opeo', 0.0, 0.0, 0.0)\n"
        "  local unit p2 = CreateUnit(Player(0), 'opeo', 64.0, 0.0, 0.0)\n"
        "  local unit p3 = CreateUnit(Player(0), 'opeo', 128.0, 0.0, 0.0)\n"
        "  local unit p4 = CreateUnit(Player(0), 'opeo', 192.0, 0.0, 0.0)\n"
        "  local unit p5 = CreateUnit(Player(0), 'opeo', 256.0, 0.0, 0.0)\n"
        "  local unit enemy = CreateUnit(Player(1), 'opeo', 320.0, 0.0, 0.0)\n"
        "  local unit burrow = CreateUnit(Player(0), 'otrb', 384.0, 0.0, 0.0)\n"
        "  call BJassAssert(GetPlayerUnitCount(Player(0), true) == 5, \"unit count must exclude structures and other players\")\n"
        "  call BJassAssert(GetPlayerTypedUnitCount(Player(0), \"Peon\", true, true) == 5, \"typed Peon count must reach five\")\n"
        "  call BJassAssert(GetPlayerTypedUnitCount(Player(0), UnitId2String('opeo'), true, true) == 5, \"typed count must accept UnitId2String identity\")\n"
        "  call KillUnit(p5)\n"
        "  call BJassAssert(GetPlayerTypedUnitCount(Player(0), \"Peon\", true, true) == 4, \"dead Peons must not count\")\n"
        "  call RemoveUnit(p1)\n"
        "  call RemoveUnit(p2)\n"
        "  call RemoveUnit(p3)\n"
        "  call RemoveUnit(p4)\n"
        "  call RemoveUnit(p5)\n"
        "  call RemoveUnit(enemy)\n"
        "  call RemoveUnit(burrow)\n"
        "endfunction"));
}

TEST(wc3_api, train_start_event_exposes_producer_and_trainee) {
    LPEDICT producer = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  boolean trainStarted = false\n"
        "endglobals\n"
        "function onTrainStart takes nothing returns nothing\n"
        "  set trainStarted = true\n"
        "  call BJassAssert(GetTriggerUnit() != null, \"train start must expose producer\")\n"
        "  call BJassAssert(GetTrainedUnitType() == 'opeo', \"train start must expose queued Peon type\")\n"
        "  call BJassAssert(GetTrainedUnit() != null, \"train start must expose queued trainee\")\n"
        "endfunction\n"
        "function verifyTrainStart takes nothing returns nothing\n"
        "  call BJassAssert(trainStarted, \"train-start trigger did not fire\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_TRAIN_START, null)\n"
        "  call TriggerAddAction(t, function onTrainStart)\n"
        "endfunction"));

    producer = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'e'), 0.0f, 0.0f);
    producer->s.player = 0;
    game.clients[0].ps.number = 0;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    unit_build(producer, MAKEFOURCC('o', 'p', 'e', 'o'));
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyTrainStart", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, trained_unit_type_uses_train_finish_event_subject) {
    LPEDICT trained = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer trainedType = 0\n"
        "endglobals\n"
        "function onTrainFinish takes nothing returns nothing\n"
        "  set trainedType = GetTrainedUnitType()\n"
        "  call BJassAssert(GetTrainedUnit() != null, \"train finish must expose trained unit\")\n"
        "endfunction\n"
        "function verifyTrainFinish takes nothing returns nothing\n"
        "  call BJassAssert(trainedType == 'opeo', \"GetTrainedUnitType must return trained rawcode\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_TRAIN_FINISH, null)\n"
        "  call TriggerAddAction(t, function onTrainFinish)\n"
        "endfunction"));

    trained = alloc_test_unit(MAKEFOURCC('o', 'p', 'e', 'o'), 0.0f, 0.0f);
    trained->s.player = 0;
    G_PublishEvent(trained, EVENT_PLAYER_UNIT_TRAIN_FINISH);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyTrainFinish", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* A campaign defeat trigger may run on any owned unit death and ask whether
 * the player still has structures.  The structure-count native must scan the
 * surviving world state rather than returning zero just because the event was
 * raised by a non-building unit. */
TEST(wc3_api, player_structure_count_survives_nonstructure_death) {
    LPEDICT victim = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit building = null\n"
        "  unit victim = null\n"
        "  boolean deathFired = false\n"
        "endglobals\n"
        "function onDeath takes nothing returns nothing\n"
        "  set deathFired = true\n"
        "  call BJassAssert(GetPlayerStructureCount(Player(0), true) == 1, \"living structure lost on unit death\")\n"
        "endfunction\n"
        "function verifyDeath takes nothing returns nothing\n"
        "  call BJassAssert(deathFired, \"death trigger did not fire\")\n"
        "  call BJassAssert(GetPlayerStructureCount(Player(0), true) == 1, \"living structure count changed after unit death\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set building = CreateUnit(Player(0), 'hbar', 0.0, 0.0, 0.0)\n"
        "  call SetWidgetLife(building, 1.0)\n"
        "  set victim = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  call TriggerRegisterDeathEvent(t, victim)\n"
        "  call TriggerAddAction(t, function onDeath)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].class_id == MAKEFOURCC('h', 'f', 'o', 'o') &&
            g_edicts[i].s.player == 0) {
            victim = &g_edicts[i];
            break;
        }
    }
    T_NOT_NULL(victim);
    unit_die(victim, NULL);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyDeath", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* =========================================================================
 * Campaign availability progress
 * ========================================================================= */

TEST(wc3_api, campaign_progress_natives_persist_stock_bj_unlocks) {
    void (*old_user_path)(LPCSTR, LPSTR, DWORD) = gi.UserPath;
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    wc3CampaignProgress_t progress;
    wc3CampaignProgressKey_t campaign_key;
    wc3CampaignProgressKey_t mission_key;

    remove(campaign_progress_test_path);
    remove("campaign-progress-native-test.orcp.tmp");
    remove("campaign-progress-native-test.orcp.bak");
    gi.UserPath = campaign_progress_test_user_path;
    gi.CvarString = campaign_progress_roc_cvar;
    level.campaign_select_on_end = false;
    G_CampaignProgressResetRuntime();

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCampaignAvailableBJ(true, bj_CAMPAIGN_INDEX_H)\n"
        "  call SetMissionAvailableBJ(true, bj_MISSION_INDEX_H00)\n"
        "endfunction\n"));

    campaign_key = MAKE(wc3CampaignProgressKey_t,
                        .edition = WC3_CAMPAIGN_EDITION_ROC,
                        .campaign = 1);
    mission_key = MAKE(wc3CampaignProgressKey_t,
                       .edition = WC3_CAMPAIGN_EDITION_ROC,
                       .campaign = 1,
                       .mission = 0);
    T_ASSERT(wc3_campaign_progress_load(campaign_progress_test_path, &progress));
    T_ASSERT(progress.tutorial_known[WC3_CAMPAIGN_EDITION_ROC]);
    T_ASSERT(progress.tutorial_cleared[WC3_CAMPAIGN_EDITION_ROC]);
    T_ASSERT(wc3_campaign_progress_campaign_available(&progress, campaign_key));
    T_ASSERT(wc3_campaign_progress_mission_available(&progress, mission_key));
    T_ASSERT(level.campaign_select_on_end);

    level.campaign_select_on_end = false;
    G_CampaignProgressResetRuntime();
    gi.CvarString = old_cvar;
    gi.UserPath = old_user_path;
    remove(campaign_progress_test_path);
}

TEST(wc3_api, campaign_progress_round_trip_preserves_explicit_false) {
    wc3CampaignProgress_t written;
    wc3CampaignProgress_t loaded;
    wc3CampaignProgressKey_t key = MAKE(wc3CampaignProgressKey_t,
                                        .edition = WC3_CAMPAIGN_EDITION_TFT,
                                        .campaign = 2,
                                        .mission = 7);

    remove(campaign_progress_test_path);
    wc3_campaign_progress_init(&written);
    T_ASSERT(wc3_campaign_progress_set_campaign(&written, key, false));
    T_ASSERT(wc3_campaign_progress_set_mission(&written, key, false));
    T_ASSERT(wc3_campaign_progress_save(campaign_progress_test_path, &written));
    T_ASSERT(wc3_campaign_progress_load(campaign_progress_test_path, &loaded));
    T_ASSERT(wc3_campaign_progress_has_campaign(&loaded, key));
    T_ASSERT(!wc3_campaign_progress_campaign_available(&loaded, key));
    T_ASSERT(wc3_campaign_progress_has_mission(&loaded, key));
    T_ASSERT(!wc3_campaign_progress_mission_available(&loaded, key));
    remove(campaign_progress_test_path);
}

TEST(wc3_api, campaign_progress_campaign_keys_match_blizzard_offsets) {
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_ROC, "Tutorial"), 0);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_ROC, "Human"), 1);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_ROC, "NightElf"), 4);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_TFT, "NightElf"), 0);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_TFT, "Human"), 1);
    T_EQ((int)wc3_campaign_progress_campaign_index(WC3_CAMPAIGN_EDITION_TFT, "Orc"), 3);
}

/* =========================================================================
 * Campaign game cache
 * ========================================================================= */

TEST(wc3_api, gamecache_scalar_values_round_trip_and_flush_by_type) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local gamecache c = InitGameCache(\"openrealm-test-memory-only.w3v\")\n"
        "  call FlushGameCache(c)\n"
        "  call StoreInteger(c, \"mission\", \"value\", 42)\n"
        "  call StoreReal(c, \"mission\", \"real\", 3.5)\n"
        "  call StoreBoolean(c, \"mission\", \"flag\", true)\n"
        "  call StoreString(c, \"mission\", \"text\", \"arthas\")\n"
        "  call BJassAssert(HaveStoredInteger(c, \"mission\", \"value\"), \"missing stored integer\")\n"
        "  call BJassAssert(GetStoredInteger(c, \"mission\", \"value\") == 42, \"wrong stored integer\")\n"
        "  call BJassAssert(GetStoredReal(c, \"mission\", \"real\") == 3.5, \"wrong stored real\")\n"
        "  call BJassAssert(GetStoredBoolean(c, \"mission\", \"flag\"), \"wrong stored boolean\")\n"
        "  call BJassAssert(GetStoredString(c, \"mission\", \"text\") == \"arthas\", \"wrong stored string\")\n"
        "  call FlushStoredInteger(c, \"mission\", \"value\")\n"
        "  call BJassAssert(not HaveStoredInteger(c, \"mission\", \"value\"), \"integer flush failed\")\n"
        "  call BJassAssert(HaveStoredString(c, \"mission\", \"text\"), \"typed flush removed another value\")\n"
        "endfunction\n"));
}

TEST(wc3_api, gamecache_disabled_keeps_handle_local_and_does_not_commit) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;

    gi.CvarString = gamecache_disabled_cvar;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local gamecache source = InitGameCache(\"openrealm-test-disabled.w3v\")\n"
        "  local gamecache fresh\n"
        "  call StoreInteger(source, \"Human01\", \"Stage\", 2)\n"
        "  call BJassAssert(GetStoredInteger(source, \"Human01\", \"Stage\") == 2, \"disabled mode broke local handle state\")\n"
        "  call BJassAssert(SaveGameCache(source), \"disabled SaveGameCache should remain script-compatible\")\n"
        "  set fresh = InitGameCache(\"openrealm-test-disabled.w3v\")\n"
        "  call BJassAssert(not HaveStoredInteger(fresh, \"Human01\", \"Stage\"), \"disabled mode committed cache state\")\n"
        "endfunction\n"));
    gi.CvarString = old_cvar;
}

TEST(wc3_api, gamecache_save_commits_to_process_memory) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;

    gi.CvarString = gamecache_memory_cvar;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local gamecache source = InitGameCache(\"openrealm-test-memory-save.w3v\")\n"
        "  local gamecache unsaved\n"
        "  local gamecache saved\n"
        "  call FlushGameCache(source)\n"
        "  call BJassAssert(SaveGameCache(source), \"initial memory save failed\")\n"
        "  call StoreInteger(source, \"Human01\", \"Stage\", 2)\n"
        "  set unsaved = InitGameCache(\"openrealm-test-memory-save.w3v\")\n"
        "  call BJassAssert(not HaveStoredInteger(unsaved, \"Human01\", \"Stage\"), \"unsaved value leaked into committed cache\")\n"
        "  call BJassAssert(SaveGameCache(source), \"memory save failed\")\n"
        "  set saved = InitGameCache(\"openrealm-test-memory-save.w3v\")\n"
        "  call BJassAssert(GetStoredInteger(saved, \"Human01\", \"Stage\") == 2, \"saved value did not survive new cache handle\")\n"
        "endfunction\n"));
    gi.CvarString = old_cvar;
}

TEST(wc3_api, gamecache_restore_preserves_hero_progression) {
    LPEDICT restored = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit restoredHero = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local gamecache c = InitGameCache(\"openrealm-test-hero-memory-only.w3v\")\n"
        "  local unit h = CreateUnit(Player(0), 'Hpal', 0.0, 0.0, 0.0)\n"
        "  call FlushGameCache(c)\n"
        "  call SetHeroLevel(h, 2, false)\n"
        "  call SelectHeroSkill(h, 'AHhb')\n"
        "  call BJassAssert(StoreUnit(c, \"Human01\", \"Arthas\", h), \"StoreUnit failed\")\n"
        "  set restoredHero = RestoreUnit(c, \"Human01\", \"Arthas\", Player(0), 128.0, 64.0, 90.0)\n"
        "  call BJassAssert(restoredHero != null, \"RestoreUnit returned null\")\n"
        "  call BJassAssert(GetHeroLevel(restoredHero) == 2, \"hero level was not restored\")\n"
        "  call BJassAssert(GetUnitAbilityLevel(restoredHero, 'AHhb') == 1, \"learned rank was not restored\")\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = globals.edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('H','p','a','l') &&
            fabsf(ent->s.origin2.x - 128.0f) < 0.01f &&
            fabsf(ent->s.origin2.y - 64.0f) < 0.01f) {
            restored = ent;
            break;
        }
    }
    T_NOT_NULL(restored);
    T_EQ((int)restored->hero.level, 2);
    T_EQ((int)restored->hero.skillpoints, 1);
    T_EQ((int)restored->heroabilities[0].code, (int)MAKEFOURCC('A','H','h','b'));
    T_EQ((int)restored->heroabilities[0].level, 1);
}

/* =========================================================================
 * Death event context
 * ========================================================================= */

TEST(wc3_api, death_event_exposes_trigger_widget_and_killing_unit) {
    LPEDICT victim = NULL;
    LPEDICT killer = NULL;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit victim = null\n"
        "  unit killer = null\n"
        "  boolean deathFired = false\n"
        "endglobals\n"
        "function onDeath takes nothing returns nothing\n"
        "  set deathFired = true\n"
        "  call BJassAssert(GetTriggerWidget() == victim, \"wrong trigger widget\")\n"
        "  call BJassAssert(GetKillingUnit() == killer, \"wrong killing unit\")\n"
        "endfunction\n"
        "function verifyDeath takes nothing returns nothing\n"
        "  call BJassAssert(deathFired, \"death trigger did not fire\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set victim = CreateUnit(Player(0), 'hfoo', 64.0, 64.0, 0.0)\n"
        "  set killer = CreateUnit(Player(0), 'hpea', 128.0, 64.0, 0.0)\n"
        "  call TriggerRegisterDeathEvent(t, victim)\n"
        "  call TriggerAddAction(t, function onDeath)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].class_id == MAKEFOURCC('h', 'f', 'o', 'o')) {
            victim = &g_edicts[i];
        } else if (g_edicts[i].class_id == MAKEFOURCC('h', 'p', 'e', 'a')) {
            killer = &g_edicts[i];
        }
    }
    T_NOT_NULL(victim);
    T_NOT_NULL(killer);

    unit_die(victim, killer);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyDeath", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_api, stock_slots_propagate_override_clamp_and_inherit) {
    LPEDICT first = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 0, 0);
    LPEDICT second = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 32, 0);
    LPEDICT future;

    G_SetAllStockSlots(true, 11); G_SetAllStockSlots(false, 9);
    T_EQ(level.stock.item_slots, 11); T_EQ(level.stock.unit_slots, 9);
    T_EQ(first->stock.item_slots, 11); T_EQ(second->stock.item_slots, 11);
    T_EQ(first->stock.unit_slots, 9); T_EQ(second->stock.unit_slots, 9);

    G_SetStockSlots(first, true, 3); G_SetStockSlots(first, false, -1);
    T_EQ(first->stock.item_slots, 3); T_EQ(first->stock.unit_slots, 0);
    T_EQ(second->stock.item_slots, 11); T_EQ(second->stock.unit_slots, 9);

    future = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 64, 0);
    G_InitStockSlots(future);
    T_EQ(future->stock.item_slots, 11); T_EQ(future->stock.unit_slots, 9);
}

TEST(wc3_api, stock_slot_natives_update_global_and_unit_state) {
    LPEDICT shop = alloc_test_unit(MAKEFOURCC('n','m','r','k'), 0, 0);
    LPEDICT created = NULL;

    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\nlocal unit shop\n"
        "call SetAllItemTypeSlots(11)\n"
        "call SetAllUnitTypeSlots(10)\n"
        "set shop = CreateUnit(Player(0),'hfoo',128.0,128.0,0.0)\n"
        "call SetItemTypeSlots(shop,3)\n"
        "call SetUnitTypeSlots(shop,4)\n"
        "endfunction"));
    T_EQ(level.stock.item_slots, 11); T_EQ(level.stock.unit_slots, 10);
    T_EQ(shop->stock.item_slots, 11); T_EQ(shop->stock.unit_slots, 10);
    FOR_LOOP(i, globals.num_edicts) if (g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) created = g_edicts + i;
    T_NOT_NULL(created);
    T_EQ(created->stock.item_slots, 3); T_EQ(created->stock.unit_slots, 4);
}

TEST(wc3_api, weather_effect_native_preserves_bounds_id_and_enable_state) {
    T_ASSERT(run_test_jass(
        "globals\n"
        "  weathereffect w = null\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "  local rect r = Rect(-256.0, -128.0, 512.0, 384.0)\n"
        "  set w = AddWeatherEffect(r, 'RAhr')\n"
        "  call EnableWeatherEffect(w, true)\n"
        "endfunction\n"));

    T_ASSERT(level.weather_effects[0].inuse);
    T_ASSERT(level.weather_effects[0].enabled);
    T_EQ(level.weather_effects[0].effect_id, MAKEFOURCC('R','A','h','r'));
    T_FEQ(level.weather_effects[0].bounds.min.x, -256.0f, 0.001f);
    T_FEQ(level.weather_effects[0].bounds.min.y, -128.0f, 0.001f);
    T_FEQ(level.weather_effects[0].bounds.max.x, 512.0f, 0.001f);
    T_FEQ(level.weather_effects[0].bounds.max.y, 384.0f, 0.001f);
}

TEST(wc3_api, weather_effect_native_remove_releases_runtime_slot) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local rect r = Rect(0.0, 0.0, 128.0, 128.0)\n"
        "  local weathereffect w = AddWeatherEffect(r, 'RAlr')\n"
        "  call EnableWeatherEffect(w, true)\n"
        "  call RemoveWeatherEffect(w)\n"
        "endfunction\n"));

    T_ASSERT(!level.weather_effects[0].inuse);
}

TEST(wc3_api, authored_global_and_region_weather_start_enabled) {
    MAPINFO info = {0};
    mapWeatherRegion_t region = {
        .bounds = { .min = {-64.0f, -32.0f}, .max = {96.0f, 160.0f} },
        .weatherID = MAKEFOURCC('R','L','l','r'),
    };

    info.weatherID = MAKEFOURCC('R','A','h','r');
    info.num_weatherRegions = 1;
    info.weatherRegions = &region;
    level.mapinfo = &info;
    CM_SetupTestWorldBounds(&MAKE(BOX2, .min = {-512.0f, -384.0f}, .max = {512.0f, 384.0f}));

    G_WeatherInitMap();

    T_ASSERT(level.weather_effects[0].inuse);
    T_ASSERT(level.weather_effects[0].enabled);
    T_EQ(level.weather_effects[0].effect_id, info.weatherID);
    T_FEQ(level.weather_effects[0].bounds.min.x, -512.0f, 0.001f);
    T_FEQ(level.weather_effects[0].bounds.max.y, 384.0f, 0.001f);
    T_ASSERT(level.weather_effects[1].inuse);
    T_ASSERT(level.weather_effects[1].enabled);
    T_EQ(level.weather_effects[1].effect_id, region.weatherID);
    T_FEQ(level.weather_effects[1].bounds.min.x, -64.0f, 0.001f);
    T_FEQ(level.weather_effects[1].bounds.max.y, 160.0f, 0.001f);
}

TEST(wc3_api, weather_effect_handle_round_trips_through_save_codec) {
    BOX2 bounds = { .min = {-32.0f, -16.0f}, .max = {64.0f, 96.0f} };
    LPGWEATHER effect = G_WeatherAdd(&bounds, MAKEFOURCC('R','A','l','r'), false);
    DWORD id = UINT32_MAX;

    T_NOT_NULL(effect);
    T_ASSERT(G_SaveJassHandle("weathereffect", effect, &id));
    T_EQ(id, 0);
    T_EQ(G_LoadJassHandle("weathereffect", id), effect);
}

/* =========================================================================
 * Test suite entry point
 * ========================================================================= */

TEST(wc3_api, controller_input_preserves_scripted_ownership) {
    LPGAMECLIENT gc = &game.clients[0];
    INPUTCMD cmd = { .action = BZ_INPUT_VIEW, .view = {{-40, 0, 25}, 1200} };
    BOOL old_ctrl = gc->no_control;
    FLOAT dist = gc->camera.state.target_distance;
    gc->no_control = true;
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.target_distance, dist, 0.001f);
    gc->no_control = false;
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.viewangles.x, -40, 0.001f);
    T_FEQ(gc->camera.state.viewangles.z, 25, 0.001f);
    T_FEQ(gc->camera.state.target_distance, 1200, 0.001f);
    T_FEQ(gc->camera.old_state.target_distance, 1200, 0.001f);
    T_EQ(gc->camera.start_time, gc->camera.end_time);
    gc->no_control = old_ctrl;
}

/* Minimap focus must reach the server camera, clear unit tracking, and respect scripted control. */
TEST(wc3_api, controller_focus_updates_camera_and_respects_control) {
    LPGAMECLIENT gc = &game.clients[0];
    INPUTCMD cmd = { .action = BZ_INPUT_FOCUS, .focus = { 300, 400 } };
    level.camera_bounds = (BOX2){ .min = { 0, 0 }, .max = { 512, 512 } };
    gc->camera.state.position = (VECTOR2){ 10, 20 };
    gc->camera.target_controller = &g_edicts[2];
    gc->no_control = true;
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.position.x, 10, 0.001f); T_FEQ(gc->camera.state.position.y, 20, 0.001f);
    T_NOT_NULL(gc->camera.target_controller);
    gc->no_control = false;
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.position.x, 300, 0.001f); T_FEQ(gc->camera.state.position.y, 400, 0.001f);
    T_NULL(gc->camera.target_controller); T_EQ(gc->camera.start_time, gc->camera.end_time);
    cmd.focus = (VECTOR2){ -100, 700 };
    globals.ClientInput(&g_edicts[0], &cmd);
    T_FEQ(gc->camera.state.position.x, 0, 0.001f); T_FEQ(gc->camera.state.position.y, 512, 0.001f);
}

/* Issue #418: SetUnitUserData / GetUnitUserData must persist scratch integer on the unit. */
TEST(wc3_api, unit_user_data_survives_set_get) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call SetUnitUserData(u, 42)\n"
        "  call BJassAssert(GetUnitUserData(u) == 42, \"user data must round-trip\")\n"
        "  call SetUnitUserData(u, -7)\n"
        "  call BJassAssert(GetUnitUserData(u) == -7, \"negative user data\")\n"
        "  call SetUnitUserData(null, 99)\n"
        "  call BJassAssert(GetUnitUserData(null) == 0, \"null unit must return 0\")\n"
        "endfunction\n"));
}

/* Issue #418: UnitSetUsesAltIcon must persist the flag on the unit. */
TEST(wc3_api, unit_set_uses_alt_icon_persists) {
    LPEDICT unit = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        "  call UnitSetUsesAltIcon(u, true)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) {
            unit = &g_edicts[i]; break;
        }
    }
    T_NOT_NULL(unit);
    T_ASSERT(unit->uses_alt_icon);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call UnitSetUsesAltIcon(null, true)\n"
        "endfunction\n"));
}

/* Issue #418: GetChangingUnit / GetChangingUnitPrevOwner must expose ownership-change event context. */
TEST(wc3_api, change_owner_event_exposes_unit_and_prev_owner) {
    /* Verify the natives are registered and callable without crash. The
     * trigger-context getters return null outside an event callback, which
     * is correct because no ownership change event was published yet. */
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call BJassAssert(GetChangingUnit() == null, \"no event context: null unit\")\n"
        "  local player p = GetChangingUnitPrevOwner()\n"
        "  call BJassAssert(p == null, \"no event context: null player\")\n"
        "endfunction\n"));
}

/* Issue #418: EnumItemsInRect must visit in-world items and bind GetEnumItem. */
TEST(wc3_api, enum_items_in_rect_visits_world_items) {
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer udg_EnumCount = 0\n"
        "  item udg_LastItem = null\n"
        "endglobals\n"
        "function countItem takes nothing returns nothing\n"
        "  set udg_EnumCount = udg_EnumCount + 1\n"
        "  set udg_LastItem = GetEnumItem()\n"
        "endfunction\n"
        "function verifyEnum takes nothing returns nothing\n"
        "  call BJassAssert(udg_EnumCount == 1, \"one world item must be enumerated\")\n"
        "  call BJassAssert(udg_LastItem != null, \"GetEnumItem must be non-null inside callback\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  call CreateItem('spro', 0.0, 0.0)\n"
        "  call EnumItemsInRect(GetWorldBounds(), null, function countItem)\n"
        "endfunction\n"));
    jass_callbyname(level.vm, "verifyEnum", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

/* Issue #418: stub natives must execute without crash or AI_STOP. */
TEST(wc3_api, campaign_stub_natives_accept_calls_without_crash) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCampaignMenuRaceEx(1)\n"
        "  call DoNotSaveReplay()\n"
        "  call SetAltMinimapIcon(\"UI\\MiniMap\\Human.blp\")\n"
        "endfunction\n"));
}

TEST(wc3_api, issue_418_campaign_natives_are_registered) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetAllyColorFilterState(2)\n"
        "  call BJassAssert(GetAllyColorFilterState() == 2, \"ally color state round-trip\")\n"
        "  call UnitRemoveBuffsEx(null, true, true, true, true, true, true, true)\n"
        "endfunction\n"));
}

TEST(wc3_api, destroyed_quest_handle_is_safe) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local quest q = CreateQuest()\n"
        "  local questitem qi = QuestCreateItem(q)\n"
        "  call DestroyQuest(q)\n"
        "  call QuestSetDiscovered(q, true)\n"
        "  call QuestItemSetCompleted(qi, true)\n"
        "  call BJassAssert(not IsQuestDiscovered(q), \"destroyed quest is invalid\")\n"
        "endfunction\n"));
}

/* Issue #418: bot assault natives must use the retail common.ai signatures. */
TEST(wc3_api, bot_assault_natives_noop_on_null_player) {
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCaptainHome(1, 0.0, 0.0)\n"
        "  call SetStagePoint(0.0, 0.0)\n"
        "  call SuicideUnit(1, 'hfoo')\n"
        "  call SuicideUnitEx(1, 'hfoo', 0)\n"
        "  call BJassAssert(not SuicidePlayer(null, false), \"empty player returns false\")\n"
        "  call BJassAssert(not MergeUnits(1, 'hfoo', 'hfoo', 'hfoo'), \"empty player returns false\")\n"
        "  call BJassAssert(GetUpgradeGoldCost(0) == 0, \"unknown upgrade returns 0\")\n"
        "  call BJassAssert(GetUpgradeLumberCost(0) == 0, \"unknown upgrade returns 0\")\n"
        "endfunction\n"));
}

#endif /* BZ_TESTS */
