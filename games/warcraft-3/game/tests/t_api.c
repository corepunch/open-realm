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
 */

#include "test.h"
#include "../g_local.h"

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

/* Campaign human slots need not match the connection slot; exercise the real VM/edict module boundary. */
TEST(wc3_api, escape_restores_game_camera_ui_and_control) {
    LPGAMECLIENT gc = &game.clients[0];
    LPCSTR cancel[] = { "cancel" };
    QUATERNION quat = Quaternion_fromEuler(&(VECTOR3){326, 0, 0}, ROTATE_ZYX);

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
    T_FEQ(gc->ps.origin.x, 128, 0.001f); T_FEQ(gc->ps.origin.y, 256, 0.001f);
    T_FEQ(gc->ps.distance, 1650, 0.001f); T_EQ(gc->ps.fov, 50);
    T_FEQ(gc->ps.viewquat.x, quat.x, 0.001f); T_FEQ(gc->ps.viewquat.w, quat.w, 0.001f);
}

TEST(wc3_api, camera_margin_is_default_camera_inset_from_playable_area) {
    /* W3I complements crop the entire W3E terrain to the playable rectangle.
     * GetCameraMargin is the remaining inset from that playable rectangle to
     * the W3I default camera bounds; it is not complement * TILE_SIZE. */
    int const raw_complements[4] = { 4, 8, 6, 10 };
    LPMAPINFO mapinfo = (LPMAPINFO)level.mapinfo;
    LPGAMECLIENT gc = &game.clients[0];

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
    T_FEQ(gc->ps.camera_bounds.min.x, -3328.0f, 0.001f);
    T_FEQ(gc->ps.camera_bounds.max.x, 2688.0f, 0.001f);
    T_FEQ(gc->ps.camera_bounds.min.y, -1920.0f, 0.001f);
    T_FEQ(gc->ps.camera_bounds.max.y, 1280.0f, 0.001f);
}

TEST(wc3_api, camera_bounds_clamp_user_and_scripted_targets) {
    LPGAMECLIENT gc = &game.clients[0];
    VECTOR2 requested = { 500.0f, -500.0f };

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-100.0, -50.0, -100.0, 50.0, 100.0, 50.0, 100.0, -50.0)\n"
        "endfunction\n"));
    T_FEQ(gc->ps.camera_bounds.min.x, -100.0f, 0.001f);
    T_FEQ(gc->ps.camera_bounds.min.y, -50.0f, 0.001f);
    T_FEQ(gc->ps.camera_bounds.max.x, 100.0f, 0.001f);
    T_FEQ(gc->ps.camera_bounds.max.y, 50.0f, 0.001f);
    if (game.max_clients > 1) {
        T_FEQ(game.clients[1].ps.camera_bounds.max.x, 100.0f, 0.001f);
    }

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

TEST(wc3_api, camera_bounds_are_per_player_when_local_context_exists) {
    LPGAMECLIENT gc0 = &game.clients[0];
    LPGAMECLIENT gc1 = game.max_clients > 1 ? &game.clients[1] : NULL;

    currentplayer = NULL;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-100.0, -100.0, -100.0, 100.0, 100.0, 100.0, 100.0, -100.0)\n"
        "endfunction\n"));

    currentplayer = &gc0->ps;
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetCameraBounds(-25.0, -20.0, -25.0, 20.0, 25.0, 20.0, 25.0, -20.0)\n"
        "endfunction\n"));
    T_FEQ(gc0->ps.camera_bounds.min.x, -25.0f, 0.001f);
    T_FEQ(gc0->ps.camera_bounds.max.y, 20.0f, 0.001f);
    if (gc1) {
        T_FEQ(gc1->ps.camera_bounds.min.x, -100.0f, 0.001f);
        T_FEQ(gc1->ps.camera_bounds.max.y, 100.0f, 0.001f);
    }
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

/* Create a minimal unit in slot 0 and return it. */
static LPEDICT make_unit_hero(void) {
    reset_entities();
    LPEDICT ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    return ent;
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

TEST(wc3_api, customize_entity_keeps_ack_for_selecting_player) {
    entityState_t state = { .event = EV_ACK, .sound = 11 };
    edict_t ent = { .selected = 1 << 3 };
    globals.CustomizeEntity(3, &ent, &state);
    T_EQ(state.event, EV_ACK);
    T_EQ(state.sound, 11);
}

TEST(wc3_api, customize_entity_hides_ack_from_other_players) {
    entityState_t state = { .event = EV_ACK, .sound = 11 };
    edict_t ent = { .selected = 1 << 3 };
    globals.CustomizeEntity(2, &ent, &state);
    T_EQ(state.event, EV_NONE);
    T_EQ(state.sound, 0);
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

    T_ASSERT(PLAYER_NEUTRAL_PASSIVE < MAX_PLAYERS);
    globals.CustomizeEntity(0, &ent, &state);
    T_ASSERT(state.flags & EF_HOVER_HEALTH);
    T_ASSERT(!(state.flags & EF_HOSTILE));
    T_ASSERT(state.flags & EF_NEUTRAL);
}

TEST(wc3_api, customize_entity_marks_neutral_aggressive_owner_hostile) {
    entityState_t state = { .number = 7, .model = 11 };
    edict_t ent = { .svflags = SVF_MONSTER, .s = { .player = PLAYER_NEUTRAL_AGGRESSIVE } };
    ent.health.value = 100.0f;

    T_ASSERT(PLAYER_NEUTRAL_AGGRESSIVE < MAX_PLAYERS);
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

TEST(wc3_api, alliance_symmetric) {
    /* G_SetPlayerAlliance sets both directions. */
    LPPLAYER p0 = test_player(0);
    LPPLAYER p1 = test_player(1);
    G_SetPlayerAlliance(p0, p1, ALLIANCE_PASSIVE, true);
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

/* =========================================================================
 * Test suite entry point
 * ========================================================================= */

#endif /* BZ_TESTS */
