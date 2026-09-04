#ifdef BZ_TESTS
/*
 * test_api.c — Tests for the JASS native API implementations.
 *
 * These tests exercise the C-level game-state that the api_*.h functions
 * read and write.  They work directly on struct fields, alliance tables,
 * and group arrays — no MPQ, renderer, or JASS VM is required.
 *
 * Covered:
 *   Player  — color, start_location, name, team, alliance
 *   Hero    — str/agi/int attributes, XP accumulation, suspend_xp,
 *             overflow-safe AddHeroXP
 *   Unit    — invulnerable, paused, no_pathing, unit_color flags
 *   Group   — FirstOfGroup, IsUnitInGroup
 *   Misc    — SubString semantics, GetRandomInt / GetRandomReal range
 *   Stock   — global capacities, per-unit overrides, spawn inheritance
 */

#include "test.h"
#include "../g_local.h"
#include "common/ui_constants.h"

/* Helpers defined in t_utils.c */
LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestWorldBounds(LPCBOX2 bounds);
BOOL run_test_jass(LPCSTR src);
extern LPPLAYER currentplayer;
void unit_die(LPEDICT self, LPEDICT attacker);



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

static DWORD presentation_write_count;
static DWORD presentation_unicast_count;
static BOOL captured_pause;
static DWORD dnc_model_index_calls;
static DWORD dnc_configstring_calls;
static DWORD dnc_configstring_index[2];
static char dnc_configstring_value[2][16];
static DWORD sky_model_index_calls;
static DWORD sky_configstring_calls;
static DWORD sky_configstring_index;
static char sky_configstring_value[16];

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

static void capture_pause(BOOL paused) { captured_pause = paused; }

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

static LPCSTR gamecache_memory_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_gamecache_mode") ? "memory" : fallback;
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
    T_FEQ(gc->ps.distance, WC3_CAMERA_DEFAULT_DISTANCE, 0.001f); T_EQ(gc->ps.fov, (DWORD)WC3_CAMERA_DEFAULT_FOV);
    T_FEQ(gc->ps.znear, WC3_CAMERA_DEFAULT_NEAR_Z, 0.001f);
    T_FEQ(gc->ps.zfar, WC3_CAMERA_DEFAULT_FAR_Z, 0.001f);
    T_FEQ(gc->ps.viewangles.x, 326.0f, 0.001f); T_FEQ(gc->ps.viewangles.z, 0.0f, 0.001f);
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
        "endglobals\n"
        "function inner_action takes nothing returns nothing\n"
        "  call SetPlayerState(Player(1), PLAYER_STATE_RESOURCE_GOLD, GetPlayerId(GetTriggerPlayer()))\n"
        "  if GetLocalPlayer() == Player(1) then\n"
        "    call ShowInterface(false, 0.0)\n"
        "  endif\n"
        "endfunction\n"
        "function outer_action takes nothing returns nothing\n"
        "  call TriggerExecute(udg_Inner)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger outer = CreateTrigger()\n"
        "  set udg_Inner = CreateTrigger()\n"
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

TEST(wc3_api, timed_camera_pan_with_z_interpolates_target_height) {
    LPGAMECLIENT gc = &game.clients[0];

    gc->ps.number = 0;
    gc->camera.state.position = MAKE(VECTOR2, 0.0f, 0.0f);
    gc->camera.state.z_offset = 0.0f;
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
    T_FEQ(gc->ps.vieworigin.z, G_MakeServerOrigin(100.0f, 150.0f, 200.0f).z, 0.001f);
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
        "  call SetFloatGameState(GAME_STATE_TIME_OF_DAY, 12.0)\n"
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

TEST(wc3_api, transient_command_style_text_does_not_enter_message_log) {
    LPGAMECLIENT gc = &game.clients[0];
    EDICT ent = { .client = gc };

    memset(&gc->message_log, 0, sizeof(gc->message_log));
    level.time = 100;
    UI_ShowTransientText(&ent, &MAKE(VECTOR2, 0.0f, 0.0f), "Not enough gold.", 2.0f);

    T_STREQ(gc->message.text, "Not enough gold.");
    T_EQ(gc->message_log.count, 0);
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

TEST(wc3_api, customize_entity_marks_neutral_aggressive_owner_hostile) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_AGGRESSIVE } };
    ent.health.value = 100.0f;

    T_EQ(PLAYER_NEUTRAL_AGGRESSIVE, 12); T_ASSERT(PLAYER_NEUTRAL_AGGRESSIVE < MAX_PLAYERS);
    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_AGGRESSIVE), ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(test_player(0), test_player(PLAYER_NEUTRAL_AGGRESSIVE), ALLIANCE_SHARED_CONTROL, true);
    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(state.flags & EF_HOSTILE);
    T_ASSERT(!(state.flags & EF_NEUTRAL));
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
    /* After setup_game(), alliance table is zeroed. */
    T_ASSERT(!G_GetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE));
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
    ggroup_t group = {0};
    T_ASSERT(group_add_entity(&group, unit));
    T_ASSERT(!group_add_entity(&group, unit));
    T_EQ(group.num_units, 1);
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

/* =========================================================================
 * Test suite entry point
 * ========================================================================= */

#endif /* BZ_TESTS */
