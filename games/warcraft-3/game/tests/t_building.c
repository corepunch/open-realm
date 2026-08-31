#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void setup_test_world(void);
void repair_build_primary(LPEDICT ent, LPEDICT building);
BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location);
slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);

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

static LPCSTR building_all_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_build_all") ? "1" : fallback;
}

static const char building_repair_slk[] =
    "ID;PWXL;N;E\n"
    "B;X9;Y4;D0\n"
    "C;X1;Y1;K\"alias\"\n"
    "C;X2;K\"code\"\n"
    "C;X3;K\"DataA1\"\n"
    "C;X4;K\"DataB1\"\n"
    "C;X5;K\"DataC1\"\n"
    "C;X6;K\"DataD1\"\n"
    "C;X7;K\"DataE1\"\n"
    "C;X8;K\"Rng1\"\n"
    "C;X9;K\"targs1\"\n"
    "C;X1;Y2;K\"Arep\"\n"
    "C;X2;K\"Arep\"\n"
    "C;X3;K1\n"
    "C;X4;K1\n"
    "C;X5;K0.5\n"
    "C;X6;K0.5\n"
    "C;X7;K0\n"
    "C;X8;K128\n"
    "C;X9;K\"ground,structure,friend\"\n"
    "C;X1;Y3;K\"Aren\"\n"
    "C;X2;K\"Aren\"\n"
    "C;X3;K1\n"
    "C;X4;K2\n"
    "C;X5;K0\n"
    "C;X6;K0\n"
    "C;X7;K0\n"
    "C;X8;K128\n"
    "C;X9;K\"ground,structure,friend\"\n"
    "C;X1;Y4;K\"Arst\"\n"
    "C;X2;K\"Arst\"\n"
    "C;X3;K0.75\n"
    "C;X4;K1\n"
    "C;X5;K0\n"
    "C;X6;K0\n"
    "C;X7;K0\n"
    "C;X8;K96\n"
    "C;X9;K\"ground,structure,friend\"\n"
    "E\n";

static slkTestData_t *building_install_repair_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(building_repair_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    if (rows_out) *rows_out = rows;
    return old;
}

static void building_restore_repair_data(slkTestData_t *old, slkTestData_t *rows) {
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

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
    T_ASSERT(client->commands_dirty);

    G_SetPlayerTechMaxAllowed(client, barracks, -1);
    T_EQ(G_GetPlayerTechMaxAllowed(client, barracks), -1);
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

TEST(wc3_building, train_command_state_uses_trains_list_and_player_maximum) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    DWORD const trainee = MAKEFOURCC('u','0','0','1');
    UnitProfile_t producer_profile = { .trains = "u001" };
    char reason[128];

    producer->UnitProfile = &producer_profile;
    producer->s.player = client->ps.number;

    T_ASSERT(G_ProducerCanTrain(producer, trainee));
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    G_SetPlayerTechMaxAllowed(client, trainee, 0);
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_HIDDEN);

    G_SetPlayerTechMaxAllowed(client, trainee, -1);
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    producer_profile.trains = "u002";
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_ABSENT);
}

TEST(wc3_building, build_all_cvar_bypasses_training_tech_gates_but_not_trains_list) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    DWORD const trainee = MAKEFOURCC('u','0','0','1');
    UnitProfile_t producer_profile = { .trains = "u001" };

    producer->UnitProfile = &producer_profile;
    producer->s.player = client->ps.number;
    G_SetPlayerTechMaxAllowed(client, trainee, 0);

    gi.CvarString = building_all_cvar;
    T_EQ(G_GetTrainCommandState(client, producer, trainee, NULL, 0), BUILD_COMMAND_AVAILABLE);

    producer_profile.trains = "u002";
    T_EQ(G_GetTrainCommandState(client, producer, trainee, NULL, 0), BUILD_COMMAND_ABSENT);
    gi.CvarString = old_cvar;
}

TEST(wc3_building, queued_training_counts_against_player_tech_maximum) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    LPEDICT queued = alloc_test_unit(MAKEFOURCC('u','0','0','1'), 0, 0);
    DWORD const trainee = MAKEFOURCC('u','0','0','1');
    UnitProfile_t producer_profile = { .trains = "u001" };

    producer->UnitProfile = &producer_profile;
    producer->s.player = client->ps.number;
    queued->s.player = client->ps.number;
    queued->training = true;
    G_SetPlayerTechMaxAllowed(client, trainee, 1);

    T_EQ(G_GetPlayerTechCountValue(client, trainee), 1);
    T_EQ(G_GetTrainCommandState(client, producer, trainee, NULL, 0), BUILD_COMMAND_HIDDEN);
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
    UnitAbilities_t abilities = { .abilList = "Arep" };
    slkTestData_t *rows, *old_abilities;
    size_t const pathtex_size = sizeof(*pathtex) +
                                FOOTPRINT_W * FOOTPRINT_H * sizeof(COLOR32);

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    builder->UnitAbilities = &abilities;
    builder->collision = 16.0f;
    builder->s.model = 1;
    building->s.model = 1;
    building->movetype = MOVETYPE_NONE;
    building->svflags |= SVF_MONSTER;
    building->health.max_value = 1200.0f;
    building->health.value = 1200.0f;
    T_ASSERT(G_StartHumanConstruction(builder, building));

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
    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, completed_repair_uses_repair_time_ratios_and_fractional_costs) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    UnitBalance_t balance;
    LPGAMECLIENT saved_client;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->UnitAbilities = &abilities;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    worker->s.player = client->ps.number;
    building->s.player = client->ps.number;
    balance = *building->UnitBalance;
    balance.reptm = 10;
    balance.buildTime = 100;
    balance.goldRep = 5;
    balance.lumberRep = 3;
    building->UnitBalance = &balance;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    building->svflags |= SVF_MONSTER;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100;

    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    T_NOT_NULL(worker->currentmove);
    T_STREQ(worker->currentmove->animation, "stand work");

    saved_client = g_edicts[0].client;
    g_edicts[0].client = NULL;
    FOR_LOOP(i, 20) worker->currentmove->think(worker);
    g_edicts[0].client = saved_client;

    /* Aren fixture: DataA=1, DataB=2, reptm=10. Over two seconds the
     * building gains 400 HP. Costs accumulate at 1 gold/sec and 0.6
     * lumber/sec, proving sub-unit tick costs are retained. buildTime=100
     * also proves reptm wins as the completed-repair duration source. */
    T_FEQ(building->health.value, 900.0f, 0.001f);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 98);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 99);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_order_walks_to_remote_target_without_teleporting) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    VECTOR2 start;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -256, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 256, 0);
    worker->UnitAbilities = &abilities;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    building->svflags |= SVF_MONSTER;
    building->s.player = worker->s.player;
    start = worker->s.origin2;

    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    T_FEQ(worker->s.origin2.x, start.x, 0.001f);
    T_FEQ(worker->s.origin2.y, start.y, 0.001f);
    T_ASSERT(worker->goalentity == building);
    T_STREQ(worker->currentmove->animation, "walk");

    unit_stand(worker);
    T_EQ(worker->buildwork.ability, 0);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_button_then_target_issues_repair_order) {
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    slkTestData_t *rows, *old_abilities;
    char target_number[16];
    LPCSTR button[] = { "button", "Arep" };
    LPCSTR select_target[] = { "select", target_number };

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->UnitAbilities = &abilities;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->svflags |= SVF_MONSTER;
    building->targtype = TARG_STRUCTURE;
    worker->s.player = client->ps.number;
    building->s.player = client->ps.number;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)building->s.number);
    G_SelectEntity(client, worker);

    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    T_EQ(client->menu.ability_code, MAKEFOURCC('A','r','e','p'));

    G_ClientCommand(clent, 2, select_target);

    T_ASSERT(worker->build == building);
    T_EQ(worker->buildwork.ability, MAKEFOURCC('A','r','e','p'));
    T_NOT_NULL(worker->currentmove);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, smart_order_repairs_damaged_owned_building) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->UnitAbilities = &abilities;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->svflags |= SVF_MONSTER;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", building));
    T_NE(worker->buildwork.ability, 0);
    T_ASSERT(worker->build == building);
    T_STREQ(worker->currentmove->animation, "stand work");

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_stops_and_releases_state_when_target_dies) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->UnitAbilities = &abilities;
    worker->stand = unit_stand;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->svflags |= SVF_MONSTER;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;

    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    building->health.value = 0.0f;
    worker->currentmove->think(worker);

    T_EQ(worker->buildwork.ability, 0);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, standard_repair_rejects_construction_and_human_requires_paused_state) {
    LPEDICT human;
    LPEDICT standard;
    LPEDICT building;
    UnitAbilities_t human_abilities = { .abilList = "Arep" };
    UnitAbilities_t standard_abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    human = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    standard = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 32);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    human->UnitAbilities = &human_abilities;
    standard->UnitAbilities = &standard_abilities;
    human->s.player = 0;
    standard->s.player = 0;
    building->s.player = 0;
    building->health.max_value = 1000.0f;
    building->health.value = 100.0f;
    building->svflags |= SVF_MONSTER;
    building->construction.active = true;
    building->construction.paused = true;

    T_ASSERT(!S_OrderRepair(standard, building, MAKEFOURCC('A','r','e','n')));
    building->construction.paused = false;
    T_ASSERT(!S_OrderRepair(human, building, MAKEFOURCC('A','r','e','p')));

    building_restore_repair_data(old_abilities, rows);
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
