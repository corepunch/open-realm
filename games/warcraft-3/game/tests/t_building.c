#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void setup_test_world(void);
void repair_build_primary(LPEDICT ent, LPEDICT building);
BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location);

static DWORD building_stand_calls;
static uiFrame_t building_command_frame;
static BOOL building_command_frame_seen;
static BOOL building_cursor_opcode_seen;
static BOOL building_cursor_clear_seen;

static void building_test_stand(LPEDICT ent) {
    (void)ent;
    building_stand_calls++;
}

static int building_test_image_index(LPCSTR name) {
    (void)name;
    return 1;
}

static void building_capture_write(pfWriteType_t type, void const *value) {
    if (!value) return;
    if (type == PF_UIFRAME) {
        building_command_frame = *(uiFrame_t const *)value;
        building_command_frame_seen = true;
        return;
    }
    if (type == PF_BYTE) {
        building_cursor_opcode_seen = *(LONG const *)value == svc_cursor;
        return;
    }
    if (type == PF_ENTITY && building_cursor_opcode_seen) {
        entityState_t const *cursor = value;
        building_cursor_clear_seen = cursor->model == 0;
        building_cursor_opcode_seen = false;
    }
}

static void building_noop_write(pfWriteType_t type, void const *value) { (void)type; (void)value; }
static void building_noop_unicast(LPEDICT ent) { (void)ent; }

TEST(wc3_building, player_tech_state_tracks_max_and_researched_levels) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    T_EQ(G_GetPlayerTechMaxAllowed(client, barracks), -1);
    T_EQ(G_GetPlayerTechResearchedLevel(client, barracks), 0);

    G_SetPlayerTechMaxAllowed(client, barracks, 2);
    G_SetPlayerTechResearched(client, barracks, 1);
    G_AddPlayerTechResearched(client, barracks, 2);

    T_EQ(G_GetPlayerTechMaxAllowed(client, barracks), 2);
    T_EQ(G_GetPlayerTechResearchedLevel(client, barracks), 3);
}

TEST(wc3_building, tech_count_includes_owned_structures_and_research) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    LPEDICT building = alloc_test_unit(barracks, 0, 0);

    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    G_SetPlayerTechResearched(client, barracks, 1);

    T_EQ(G_GetPlayerTechCountValue(client, barracks), 2);

    building->svflags |= SVF_DEADMONSTER;
    T_EQ(G_GetPlayerTechCountValue(client, barracks), 1);
}

TEST(wc3_building, building_charge_checks_and_deducts_resources) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    UnitBalance_t const *balance = G_UnitBalance(barracks);

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = balance->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = balance->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    T_ASSERT(G_ChargeBuilding(client, barracks));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);
}

TEST(wc3_building, build_command_state_covers_available_hidden_disabled_and_absent) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    UnitProfile_t worker_profile = { .builds = "hbar" };
    char reason[128];

    worker->UnitProfile = &worker_profile;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    G_SetPlayerTechMaxAllowed(client, barracks, 0);
    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_HIDDEN);

    memset(client->tech, 0, sizeof(client->tech));
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_DISABLED);
    T_STREQ(reason, "Not enough gold");

    worker_profile.builds = "hfoo";
    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_ABSENT);
}

TEST(wc3_building, disabled_command_button_is_inert_and_available_button_is_clickable) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    gameCommandButton_t button;

    memset(&button, 0, sizeof(button));
    snprintf(button.command, sizeof(button.command), "CmdBuild");
    snprintf(button.art, sizeof(button.art), "test");
    button.hotkey = 'B';
    button.x = 0;
    button.y = 0;

    gi.Write = building_capture_write;
    gi.ImageIndex = building_test_image_index;

    building_command_frame_seen = false;
    button.disabled = 1;
    UI_WriteCommandButtonFrame(&button);
    T_ASSERT(building_command_frame_seen);
    T_EQ(building_command_frame.flags.type, FT_COMMANDBUTTON);
    T_EQ(building_command_frame.hotkey, 0);
    T_NULL(building_command_frame.onclick);
    T_EQ(building_command_frame.color.r, 128);
    T_EQ(building_command_frame.color.g, 128);
    T_EQ(building_command_frame.color.b, 128);

    building_command_frame_seen = false;
    button.disabled = 0;
    UI_WriteCommandButtonFrame(&button);
    T_ASSERT(building_command_frame_seen);
    T_EQ(building_command_frame.hotkey, 'B');
    T_NOT_NULL(building_command_frame.onclick);

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, tech_state_capacity_is_bounded_without_clobbering_existing_entries) {
    LPGAMECLIENT client = &game.clients[0];

    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        DWORD const techid = 0x41000000u + i + 1;
        G_SetPlayerTechMaxAllowed(client, techid, (LONG)i);
    }
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        DWORD const techid = 0x41000000u + i + 1;
        T_EQ(G_GetPlayerTechMaxAllowed(client, techid), (LONG)i);
    }

    G_SetPlayerTechMaxAllowed(client, 0x42000001u, 7);
    T_EQ(G_GetPlayerTechMaxAllowed(client, 0x42000001u), -1);
    T_EQ(G_GetPlayerTechMaxAllowed(client, 0x41000001u), 0);
}

TEST(wc3_building, building_charge_rejects_short_gold_and_refund_restores_resources) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    UnitBalance_t const *balance = G_UnitBalance(barracks);

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = MAX(0, balance->goldCost - 1);
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = balance->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    T_ASSERT(!G_ChargeBuilding(client, barracks));

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = balance->goldCost;
    T_ASSERT(G_ChargeBuilding(client, barracks));
    G_RefundBuilding(client, barracks);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], balance->goldCost);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], balance->lumberCost);
}

TEST(wc3_building, placement_accepts_open_ground_rejects_live_unit_and_map_edge) {
    LPEDICT builder;
    LPEDICT blocker;
    VECTOR2 requested = { 64.0f, 64.0f };
    VECTOR2 snapped;
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128, -128);

    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_OK);

    blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), snapped.x, snapped.y);
    blocker->svflags |= SVF_MONSTER;
    blocker->collision = 16.0f;
    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_UNIT_BLOCKED);

    requested = (VECTOR2){ 100000.0f, 100000.0f };
    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_OUT_OF_BOUNDS);
}

TEST(wc3_building, building_snap_without_authored_pathing_uses_32_unit_grid) {
    VECTOR2 point = { 47.0f, 79.0f };

    G_SnapBuildingPoint(MAKEFOURCC('h','p','e','a'), &point);

    T_FEQ(point.x, 32.0f, 0.001f);
    T_FEQ(point.y, 64.0f, 0.001f);
}

TEST(wc3_building, human_construction_start_sets_explicit_state_and_start_life) {
    LPEDICT builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);

    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;

    T_ASSERT(!G_StartHumanConstruction(builder, builder));
    T_ASSERT(G_StartHumanConstruction(builder, building));
    T_ASSERT(building->construction.active);
    T_ASSERT(building->construction.paused);
    T_ASSERT(building->construction.primary_builder == builder);
    T_FEQ(building->construction.progress, 0.0f, 0.001f);
    T_ASSERT(building->aiflags & AI_HOLD_FRAME);
    T_FEQ(building->health.value, 100.0f, 0.001f);
}

TEST(wc3_building, completing_construction_clears_state_publishes_once_and_grants_food_once) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    UnitBalance_t balance = *building->UnitBalance;

    balance.foodMade = 6;
    building->UnitBalance = &balance;
    building->s.player = client->ps.number;
    building->health.max_value = 1000.0f;
    building->health.value = 400.0f;
    building->construction.active = true;
    building->construction.paused = true;
    building->construction.primary_builder = builder;
    building->construction.progress = 500.0f;
    building->aiflags |= AI_HOLD_FRAME;
    building->stand = building_test_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    level.events.write = 0;
    level.events.read = 0;
    building_stand_calls = 0;

    {
        /* Prevent G_CompleteConstruction from invoking HUD refresh (which
         * requires FDF/UI state unavailable in tests) by hiding the player
         * entity from G_GetPlayerEntityByNumber while the call runs. */
        LPGAMECLIENT saved_client = g_edicts[0].client;
        g_edicts[0].client = NULL;
        G_CompleteConstruction(building);
        g_edicts[0].client = saved_client;
    }

    T_ASSERT(!building->construction.active);
    T_ASSERT(!building->construction.paused);
    T_NULL(building->construction.primary_builder);
    T_FEQ(building->construction.progress, 0.0f, 0.001f);
    T_ASSERT(!(building->aiflags & AI_HOLD_FRAME));
    T_FEQ(building->health.value, building->health.max_value, 0.001f);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 16);
    T_EQ(building_stand_calls, 1);
    T_EQ(level.events.write, 1);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_CONSTRUCT_FINISH);
    T_ASSERT(level.events.queue[0].edict == building);

    G_CompleteConstruction(building);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 16);
    T_EQ(building_stand_calls, 1);
    T_EQ(level.events.write, 1);
}

TEST(wc3_building, plain_build_error_text_is_not_resolved_as_trigger_string_zero) {
    mapTrigStr_t zero = { .id = 0 };

    snprintf(zero.text, sizeof(zero.text), "Human02");
    ((LPMAPINFO)level.mapinfo)->strings = &zero;

    T_STREQ(G_LevelString("Unable to build there."), "Unable to build there.");
    T_STREQ(G_LevelString("TRIGSTR_0"), "Human02");
    T_STREQ(G_LevelString("TRIGSTR_bad"), "TRIGSTR_bad");
}

TEST(wc3_building, human_repair_capability_comes_from_unit_ability_list) {
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    UnitAbilities_t human_repair = { .abilList = "Arep" };
    UnitAbilities_t generic_repair = { .abilList = "Aren" };

    worker->UnitAbilities = &human_repair;
    T_ASSERT(G_UnitHasHumanRepair(worker));

    worker->UnitAbilities = &generic_repair;
    T_ASSERT(!G_UnitHasHumanRepair(worker));

    worker->UnitAbilities = NULL;
    T_ASSERT(!G_UnitHasHumanRepair(worker));
}

TEST(wc3_building, human_builder_exit_is_outside_baked_building_footprint) {
    enum { FOOTPRINT_W = 10, FOOTPRINT_H = 10 };
    LPEDICT builder;
    LPEDICT building;
    pathTex_t *pathtex;
    size_t const pathtex_size = sizeof(*pathtex) +
                                FOOTPRINT_W * FOOTPRINT_H * sizeof(COLOR32);

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    builder->collision = 16.0f;
    builder->s.model = 1;
    building->s.model = 1;
    building->movetype = MOVETYPE_NONE;

    pathtex = gi.MemAlloc(pathtex_size);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = FOOTPRINT_W;
    pathtex->height = FOOTPRINT_H;
    FOR_LOOP(i, FOOTPRINT_W * FOOTPRINT_H) pathtex->map[i].b = 0xff;
    building->pathtex = pathtex;

    gi.LinkEntity(builder);
    gi.LinkEntity(building);
    CM_BakeStaticObstacles();

    repair_build_primary(builder, building);

    T_ASSERT(builder->build == building);
    T_ASSERT(CM_PointIsPathableForRadius(&builder->s.origin2, builder->collision));
    T_ASSERT(fabsf(builder->s.origin2.x - building->s.origin2.x) >= 160.0f ||
             fabsf(builder->s.origin2.y - building->s.origin2.y) >= 160.0f);

    building->pathtex = NULL;
    gi.MemFree(pathtex);
}


TEST(wc3_building, cancel_command_clears_active_build_placement_cursor) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;

    client->menu.on_location_selected = build_menu_send_builder;
    clent->build_project = MAKEFOURCC('h','b','a','r');
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;

    CMD_CancelCommand(clent);

    T_EQ(clent->build_project, 0);
    T_NULL(client->menu.on_location_selected);
    T_ASSERT(building_cursor_clear_seen);

    gi.Write = old_write;
}

TEST(wc3_building, smartpoint_cancels_build_placement_without_moving_selected_worker) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPCSTR command[] = { "smartpoint", "256", "256" };

    G_SelectEntity(client, worker);
    client->menu.on_location_selected = build_menu_send_builder;
    clent->build_project = MAKEFOURCC('h','b','a','r');
    worker->goalentity = NULL;
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;

    G_ClientCommand(clent, 3, command);

    T_EQ(clent->build_project, 0);
    T_NULL(client->menu.on_location_selected);
    T_NULL(worker->goalentity);
    T_ASSERT(building_cursor_clear_seen);

    gi.Write = old_write;
}

TEST(wc3_building, smart_target_cancels_build_placement_before_issuing_order) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    char target_number[16];
    LPCSTR command[] = { "smart", target_number };

    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);
    G_SelectEntity(client, worker);
    client->menu.on_location_selected = build_menu_send_builder;
    clent->build_project = MAKEFOURCC('h','b','a','r');
    worker->goalentity = NULL;
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;

    G_ClientCommand(clent, 2, command);

    T_EQ(clent->build_project, 0);
    T_NULL(client->menu.on_location_selected);
    T_NULL(worker->goalentity);
    T_ASSERT(building_cursor_clear_seen);

    gi.Write = old_write;
}

#endif
