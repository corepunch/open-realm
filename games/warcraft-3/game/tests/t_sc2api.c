#if defined(BZ_TESTS) && defined(WC3_SC2API)

#include "test.h"
#include "../sc2api/sc2api_compat.h"
#include "../sc2api/sc2api_data.h"
#include "../sc2api/sc2api_game.h"
#include "../sc2api/sc2api_map.h"
#include "../sc2api/sc2api_raw.h"
#include "../sc2api/sc2api_wire.h"
#include "games/warcraft-3/common/terrain.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
void unit_stand(LPEDICT self);
void CM_SetupTestPathmap(DWORD width, DWORD height, BYTE const *cells);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);


TEST(wc3_sc2api, maps_warcraft_races_without_reusing_sc2_values) {
    T_EQ(WC3_SC2API_RaceFromPlayerRace(kPlayerRaceNone), WC3_SC2_RACE_NONE);
    T_EQ(WC3_SC2API_RaceFromPlayerRace(kPlayerRaceHuman), WC3_SC2_RACE_HUMAN);
    T_EQ(WC3_SC2API_RaceFromPlayerRace(kPlayerRaceOrc), WC3_SC2_RACE_ORC);
    T_EQ(WC3_SC2API_RaceFromPlayerRace(kPlayerRaceUndead), WC3_SC2_RACE_UNDEAD);
    T_EQ(WC3_SC2API_RaceFromPlayerRace(kPlayerRaceNightElf), WC3_SC2_RACE_NIGHT_ELF);
    T_EQ(WC3_SC2API_RequestedRaceFromPlayerRace(kPlayerRaceNone), WC3_SC2_RACE_RANDOM);
    T_EQ(WC3_SC2API_RequestedRaceFromPlayerRace(kPlayerRaceHuman), WC3_SC2_RACE_HUMAN);
    T_EQ(WC3_SC2_RACE_HUMAN, 5);
    T_EQ(WC3_SC2_RACE_NIGHT_ELF, 8);
}

TEST(wc3_sc2api, player_common_maps_minerals_to_lumber_and_vespene_to_gold) {
    wc3Sc2PlayerCommon_t common;
    LPGAMECLIENT client;

    setup_test_world();
    client = &game.clients[2];
    client->ps.number = 2;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1250;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 800;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 42;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 31;

    T_ASSERT(WC3_SC2API_FillPlayerCommon(2, &common));
    T_EQ(common.player_id, 3);
    T_EQ(common.minerals, 800);
    T_EQ(common.vespene, 1250);
    T_EQ(common.food_cap, 42);
    T_EQ(common.food_used, 31);
    T_ASSERT(!WC3_SC2API_FillPlayerCommon(game.max_clients, &common));
}


TEST(wc3_sc2api, player_common_splits_worker_and_army_food_from_owned_live_units) {
    wc3Sc2PlayerCommon_t common;
    LPGAMECLIENT client;
    LPEDICT worker, soldier;

    reset_entities();
    setup_test_world();
    client = &game.clients[0];
    client->ps.number = 0;

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    worker->svflags |= SVF_MONSTER;
    worker->s.player = 0;
    worker->stand = unit_stand;
    unit_stand(worker);
    G_SetUnitFoodUsed(worker, 1);

    soldier = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    soldier->svflags |= SVF_MONSTER;
    soldier->s.player = 0;
    G_SetUnitFoodUsed(soldier, 2);

    T_ASSERT(WC3_SC2API_FillPlayerCommon(0, &common));
    T_EQ(common.food_workers, 1);
    T_EQ(common.food_army, 2);
    T_EQ(common.idle_worker_count, 1);
    T_EQ(common.army_count, 1);
}

TEST(wc3_sc2api, player_lookup_uses_warcraft_player_number_not_client_index) {
    wc3Sc2PlayerCommon_t common;

    setup_test_world();
    game.clients[0].ps.number = 5;
    game.clients[1].ps.number = 0;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 111;
    game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 777;
    game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 333;

    T_EQ(WC3_SC2API_PlayerClient(0), &game.clients[1]);
    T_ASSERT(WC3_SC2API_FillPlayerCommon(0, &common));
    T_EQ(common.player_id, 1);
    T_EQ(common.minerals, 333);
    T_EQ(common.vespene, 777);
}

TEST(wc3_sc2api, game_info_exposes_playable_participants_and_warcraft_races) {
    wc3Sc2GameInfo_t info;
    LPMAPINFO mapinfo;

    setup_test_world();
    mapinfo = (LPMAPINFO)level.mapinfo;
    mapinfo->mapName = "Agent Test";
    mapinfo->players[0].used = true;
    mapinfo->players[0].playerType = kPlayerTypeHuman;
    mapinfo->players[0].playerRace = kPlayerRaceHuman;
    mapinfo->players[0].playerName = "Human Agent";
    mapinfo->players[1].used = true;
    mapinfo->players[1].playerType = kPlayerTypeComputer;
    mapinfo->players[1].playerRace = kPlayerRaceNone;
    mapinfo->players[1].playerName = "Orc Computer";
    mapinfo->players[PLAYER_NEUTRAL_PASSIVE].used = true;
    mapinfo->players[PLAYER_NEUTRAL_PASSIVE].playerType = kPlayerTypeNeutral;
    game.clients[0].ps.number = 0;
    game.clients[0].ps.race = kPlayerRaceHuman;
    strlcpy(game.clients[0].jass.name, "Human Agent", sizeof(game.clients[0].jass.name));
    game.clients[1].ps.number = 1;
    game.clients[1].ps.race = kPlayerRaceOrc;
    strlcpy(game.clients[1].jass.name, "Orc Computer", sizeof(game.clients[1].jass.name));
    strlcpy(level.map_path, "Maps\\AgentTest.w3x", sizeof(level.map_path));

    T_ASSERT(WC3_SC2API_FillGameInfo(&info));
    T_STREQ(info.map_name, "Agent Test");
    T_STREQ(info.local_map_path, "Maps\\AgentTest.w3x");
    T_EQ(info.player_count, 2);
    T_EQ(info.players[0].player_id, 1);
    T_EQ(info.players[0].type, WC3_SC2_PLAYER_PARTICIPANT);
    T_EQ(info.players[0].race_actual, WC3_SC2_RACE_HUMAN);
    T_EQ(info.players[1].player_id, 2);
    T_EQ(info.players[1].type, WC3_SC2_PLAYER_COMPUTER);
    T_EQ(info.players[1].race_requested, WC3_SC2_RACE_RANDOM);
    T_EQ(info.players[1].race_actual, WC3_SC2_RACE_ORC);
}

TEST(wc3_sc2api, observation_uses_authoritative_game_loop_and_player_number) {
    wc3Sc2Observation_t observation;
    wc3Sc2RawUnit_t units[4];

    reset_entities();
    setup_test_world();
    game.clients[0].ps.number = 4;
    game.clients[1].ps.number = 0;
    game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 900;
    game.clients[1].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 450;
    level.framenum = 1234;

    T_ASSERT(WC3_SC2API_BuildObservation(0, &observation, units, 4));
    T_EQ(observation.game_loop, 1234);
    T_EQ(observation.player_common.player_id, 1);
    T_EQ(observation.player_common.minerals, 450);
    T_EQ(observation.player_common.vespene, 900);
}

TEST(wc3_sc2api, observation_exports_researched_upgrade_ids_once_per_tech) {
    wc3Sc2Observation_t observation;
    wc3Sc2RawUnit_t units[1];
    LPGAMECLIENT client;
    DWORD const melee = MAKEFOURCC('R','h','m','e');
    DWORD const armor = MAKEFOURCC('R','h','a','r');

    reset_entities();
    setup_test_world();
    client = &game.clients[0];
    client->ps.number = 0;
    client->tech[0] = (playerTechState_t){ .id = melee, .researched = 2 };
    client->tech[1] = (playerTechState_t){ .id = armor, .in_progress = 1 };

    T_ASSERT(WC3_SC2API_BuildObservation(0, &observation, units, 1));
    T_EQ(observation.upgrade_count, 1);
    T_EQ(observation.upgrade_ids[0], melee);
}

TEST(wc3_sc2api, player_result_maps_wc3_terminal_state_to_sc2_values) {
    wc3Sc2PlayerResult_t result;
    LPGAMECLIENT client;

    setup_test_world();
    client = &game.clients[0];
    client->ps.number = 0;
    T_ASSERT(!WC3_SC2API_FillPlayerResult(0, &result));

    client->jass.removed = true;
    client->ps.stats[PLAYERSTATE_GAME_RESULT] = 2;
    T_ASSERT(WC3_SC2API_FillPlayerResult(0, &result));
    T_EQ(result.player_id, 1);
    T_EQ(result.result, WC3_SC2_RESULT_TIE);
}

TEST(wc3_sc2api, unit_type_data_builder_enumerates_loaded_wc3_rows) {
    DWORD const footman = MAKEFOURCC('h','f','o','o');
    DWORD const capacity = WC3_SC2API_UnitTypeDataCapacity();
    wc3Sc2UnitTypeData_t *rows;
    DWORD count;
    BOOL found = false;

    T_ASSERT(capacity > 0);
    rows = calloc(capacity, sizeof(*rows));
    T_NOT_NULL(rows);
    count = WC3_SC2API_BuildUnitTypeData(rows, capacity);
    FOR_LOOP(i, count) if (rows[i].unit_type_id == footman) {
        found = true;
        break;
    }
    T_ASSERT(found);
    free(rows);
}


TEST(wc3_sc2api, unit_type_data_builder_includes_map_created_unit_ids) {
    DWORD const base_id = MAKEFOURCC('h','f','o','o');
    DWORD const custom_id = MAKEFOURCC('x','a','g','t');
    unitData_t custom = { .originalUnitID = base_id, .newUnitID = custom_id };
    MAPINFO mapinfo = { .num_userCreatedUnits = 1, .userCreatedUnits = &custom };
    LPCMAPINFO saved_mapinfo;
    wc3Sc2UnitTypeData_t *rows;
    DWORD capacity, count;
    BOOL found = false;

    setup_test_world();
    saved_mapinfo = level.mapinfo;
    level.mapinfo = &mapinfo;
    G_SetMapUnitOverrides(&mapinfo);
    capacity = WC3_SC2API_UnitTypeDataCapacity();
    rows = calloc(capacity, sizeof(*rows));
    T_NOT_NULL(rows);
    count = WC3_SC2API_BuildUnitTypeData(rows, capacity);
    FOR_LOOP(i, count) if (rows[i].unit_type_id == custom_id) {
        found = true;
        break;
    }
    T_ASSERT(found);
    free(rows);
    G_SetMapUnitOverrides(NULL);
    level.mapinfo = saved_mapinfo;
}

TEST(wc3_sc2api, unit_tags_reject_reused_entity_lifetimes) {
    LPEDICT unit = G_Spawn();
    uint64_t tag;

    unit->spawn_time = 12345;
    tag = WC3_SC2API_UnitTag(unit);
    T_ASSERT(tag != 0);
    T_EQ(WC3_SC2API_ResolveUnitTag(tag), unit);

    unit->spawn_time++;
    T_NULL(WC3_SC2API_ResolveUnitTag(tag));
}

TEST(wc3_sc2api, death_retires_sc2_tag_without_changing_warcraft_lifetime) {
    LPEDICT unit = G_Spawn();
    uint64_t before, after;
    DWORD spawn_time;

    unit->spawn_time = 23456;
    spawn_time = unit->spawn_time;
    before = WC3_SC2API_UnitTag(unit);
    T_ASSERT(before != 0);

    WC3_SC2API_AdvanceUnitTagLifetime(unit);
    after = WC3_SC2API_UnitTag(unit);
    T_ASSERT(after != 0);
    T_NE(after, before);
    T_EQ(unit->spawn_time, spawn_time);
    T_NULL(WC3_SC2API_ResolveUnitTag(before));
    T_EQ(WC3_SC2API_ResolveUnitTag(after), unit);
}


TEST(wc3_sc2api, unit_type_data_maps_lumber_to_minerals_and_gold_to_vespene) {
    DWORD const footman = MAKEFOURCC('h','f','o','o');
    UnitBalance_t const *balance = G_UnitBalance(footman);
    wc3Sc2UnitTypeData_t data;

    T_ASSERT(balance && balance->id);
    T_ASSERT(WC3_SC2API_FillUnitTypeData(footman, &data));
    T_EQ(data.unit_type_id, footman);
    T_EQ(data.mineral_cost, MAX(0, balance->lumberCost));
    T_EQ(data.vespene_cost, MAX(0, balance->goldCost));
    T_FEQ(data.movement_speed, WC3_SC2API_WorldToBoardDistance(MAX(0.0f, balance->speed)), 0.001f);
    T_FEQ(data.build_time_seconds, (FLOAT)MAX(0, balance->buildTime), 0.001f);
    T_FEQ(data.sight_range, WC3_SC2API_WorldToBoardDistance(MAX(0.0f, balance->sightRadius)), 0.001f);
    T_EQ(data.race, WC3_SC2_RACE_HUMAN);
}

TEST(wc3_sc2api, upgrade_data_uses_level_one_wc3_research_costs) {
    DWORD const upgrade = MAKEFOURCC('R','h','m','e');
    wc3Sc2UpgradeData_t data;

    T_ASSERT(WC3_SC2API_FillUpgradeData(upgrade, &data));
    T_EQ(data.upgrade_id, upgrade);
    T_EQ(data.mineral_cost, G_UpgradeLumberCost(upgrade, 1));
    T_EQ(data.vespene_cost, G_UpgradeGoldCost(upgrade, 1));
    T_FEQ(data.research_time, G_UpgradeResearchTime(upgrade, 1), 0.001f);
    T_EQ(data.ability_id, upgrade);
}

TEST(wc3_sc2api, buff_data_uses_loaded_wc3_buff_name) {
    static char const buff_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"Bufftip\"\n"
        "C;Y2;X1;K\"Btst\"\n"
        "C;Y2;X2;K\"Agent Test Buff\"\n"
        "E\n";
    DWORD const buff = MAKEFOURCC('B','t','s','t');
    wc3Sc2BuffData_t data;
    slkTestData_t *rows = parse_slk_string(buff_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityBuffData", rows);

    T_ASSERT(WC3_SC2API_FillBuffData(buff, &data));
    T_EQ(data.buff_id, buff);
    T_STREQ(data.name, "Agent Test Buff");

    G_SetSLKRows("AbilityBuffData", old);
    free_slk_rows(rows);
}

TEST(wc3_sc2api, raw_unit_observation_uses_player_filtered_game_state) {
    wc3Sc2RawUnit_t raw;
    LPEDICT unit;

    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 256.0f);
    unit->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    unit->mana.value = 17.0f;
    unit->mana.max_value = 30.0f;

    T_ASSERT(WC3_SC2API_FillRawUnit(0, unit, &raw));
    T_EQ(raw.display_type, WC3_SC2_DISPLAY_VISIBLE);
    T_EQ(raw.alliance, WC3_SC2_ALLIANCE_SELF);
    T_EQ(raw.tag, WC3_SC2API_UnitTag(unit));
    T_EQ(raw.unit_type, unit->class_id);
    T_EQ(raw.owner, 1);
    T_FEQ(raw.pos.x, 36.0f, 0.001f);
    T_FEQ(raw.pos.y, 40.0f, 0.001f);
    T_ASSERT(raw.has_live_data);
    T_FEQ(raw.health, unit->health.value, 0.001f);
    T_FEQ(raw.energy, 17.0f, 0.001f);
}

TEST(wc3_sc2api, raw_visible_unit_exports_active_buff_ids_without_duplicates) {
    wc3Sc2RawUnit_t raw;
    LPEDICT unit;
    DWORD const buff = MAKEFOURCC('B','t','s','t');

    reset_entities();
    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 256.0f);
    unit->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    unit->abilstatus[0] = (heroabilitystatus_t){ .code = buff, .level = 1 };
    unit->abilstatus[1] = (heroabilitystatus_t){ .code = buff, .level = 2 };
    unit->abilstatus[2] = (heroabilitystatus_t){ .code = MAKEFOURCC('B','o','l','d'), .level = 1, .timestamp = 1 };
    level.time = 100;

    T_ASSERT(WC3_SC2API_FillRawUnit(0, unit, &raw));
    T_EQ(raw.buff_count, 1);
    T_EQ(raw.buff_ids[0], buff);
}

TEST(wc3_sc2api, raw_friendly_transport_exports_passenger_details) {
    static char const cargo_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"Acar\"\n"
        "C;Y2;X2;K\"Acar\"\n"
        "C;Y2;X3;K8\n"
        "E\n";
    static UnitAbilities_t const abilities = { .abilList = "Acar" };
    wc3Sc2RawUnit_t raw;
    slkTestData_t *rows = parse_slk_string(cargo_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    LPEDICT transport, passenger;

    reset_entities();
    setup_test_world();
    transport = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 256.0f);
    passenger = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 128.0f, 256.0f);
    transport->s.player = passenger->s.player = 0;
    transport->svflags |= SVF_MONSTER;
    passenger->svflags |= SVF_MONSTER;
    transport->data.UnitAbilities = &abilities;
    transport->cargo.units[0] = passenger;
    transport->cargo.count = 1;
    passenger->health.value = 37.0f;
    passenger->health.max_value = 100.0f;
    passenger->mana.value = 11.0f;
    passenger->mana.max_value = 20.0f;

    T_ASSERT(WC3_SC2API_FillRawUnit(0, transport, &raw));
    T_EQ(raw.cargo_space_taken, 1);
    T_EQ(raw.cargo_space_max, 8);
    T_EQ(raw.passenger_count, 1);
    T_EQ(raw.passengers[0].tag, WC3_SC2API_UnitTag(passenger));
    T_EQ(raw.passengers[0].unit_type, passenger->class_id);
    T_FEQ(raw.passengers[0].health, 37.0f, 0.001f);
    T_FEQ(raw.passengers[0].energy, 11.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_sc2api, raw_enemy_observation_does_not_export_issued_orders) {
    wc3Sc2RawUnit_t raw;
    LPEDICT enemy;

    reset_entities();
    setup_test_world();
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 256.0f);
    enemy->s.player = 1;
    enemy->svflags |= SVF_MONSTER;
    G_PublishIssuedImmediateOrder(enemy, G_OrderId("stop"), 1, "stop");

    T_ASSERT(WC3_SC2API_FillRawUnit(0, enemy, &raw));
    T_EQ(raw.alliance, WC3_SC2_ALLIANCE_ENEMY);
    T_EQ(raw.order_count, 0);
}


TEST(wc3_sc2api, map_state_maps_fog_categories_and_blight_to_creep_bits) {
    wc3Sc2MapState_t map_state;
    LPBYTE visibility, creep;
    DWORD visibility_size, creep_size;

    setup_test_world();
    game.clients[0].ps.number = 0;
    G_FowInit();
    visibility_size = WC3_SC2API_VisibilityDataSize();
    creep_size = WC3_SC2API_CreepDataSize();
    T_ASSERT(visibility_size >= 3);
    T_ASSERT(creep_size > 0);
    visibility = calloc(visibility_size, 1);
    creep = calloc(creep_size, 1);
    T_NOT_NULL(visibility);
    T_NOT_NULL(creep);

    level.fow.players[0].explored[0] = 1;
    level.fow.players[0].explored[1] = 1;
    level.fow.players[0].visible[1] = 1;
    level.blight.cells[0] = 1;
    level.blight.cells[7] = 1;

    T_ASSERT(WC3_SC2API_FillMapState(0, visibility, visibility_size,
                                     creep, creep_size, &map_state));
    T_ASSERT(map_state.has_visibility);
    T_EQ(map_state.visibility.bits_per_pixel, 8);
    T_EQ(map_state.visibility.width, (LONG)WC3_SC2API_MapWidth());
    T_EQ(map_state.visibility.height, (LONG)WC3_SC2API_MapHeight());
    T_EQ(map_state.visibility.data[0], 1);
    T_EQ(map_state.visibility.data[4], 2);
    T_EQ(map_state.visibility.data[8], 0);
    T_ASSERT(map_state.has_creep);
    T_EQ(map_state.creep.bits_per_pixel, 1);
    T_EQ(map_state.creep.data[0], 0x81);

    free(visibility);
    free(creep);
    G_FowShutdown();
}

TEST(wc3_sc2api, raw_friendly_target_order_exports_lifetime_safe_target_tag) {
    wc3Sc2RawUnit_t raw;
    LPEDICT unit, target;

    reset_entities();
    setup_test_world();
    game.clients[0].ps.number = 0;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 256.0f);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 384.0f, 256.0f);
    unit->s.player = target->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    target->stand = unit_stand;

    T_ASSERT(G_IssueUnitTargetOrder(unit, "smart", target, false, 0));
    T_EQ(G_GetIssuedOrderTarget(unit), target);
    T_ASSERT(WC3_SC2API_FillRawUnit(0, unit, &raw));
    T_EQ(raw.order_count, 1);
    T_ASSERT(raw.orders[0].has_target_unit);
    T_EQ(raw.orders[0].target_unit_tag, WC3_SC2API_UnitTag(target));
    T_ASSERT(!raw.orders[0].has_point);

    target->spawn_time++;
    T_NULL(G_GetIssuedOrderTarget(unit));
    T_ASSERT(WC3_SC2API_FillRawUnit(0, unit, &raw));
    T_EQ(raw.order_count, 0);
}

TEST(wc3_sc2api, raw_single_unit_command_uses_existing_order_entry_point) {
    wc3Sc2RawUnitCommand_t command;
    LPEDICT unit;

    reset_entities();
    setup_test_world();
    game.clients[0].ps.number = 0;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 256.0f);
    unit->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;

    memset(&command, 0, sizeof(command));
    command.ability_id = G_OrderId("stop");
    command.unit_tag = WC3_SC2API_UnitTag(unit);
    command.target_type = WC3_SC2_TARGET_NONE;

    T_EQ(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_EQ(G_GetIssuedOrderId(unit), G_OrderId("stop"));

    command.queue_command = true;
    T_EQ(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_CANT_QUEUE);
}


TEST(wc3_sc2api, board_coordinates_use_pathing_cell_scale_and_lower_left_origin) {
    VECTOR2 const world = { 128.0f, 256.0f };
    VECTOR2 board, roundtrip;

    setup_test_world();
    T_FEQ(WC3_SC2API_BoardCellWorldSize(), 32.0f, 0.001f);
    T_EQ(WC3_SC2API_MapWidth(), 64);
    T_EQ(WC3_SC2API_MapHeight(), 64);
    board = WC3_SC2API_WorldToBoardPoint(&world);
    T_FEQ(board.x, 36.0f, 0.001f);
    T_FEQ(board.y, 40.0f, 0.001f);
    roundtrip = WC3_SC2API_BoardToWorldPoint(&board);
    T_FEQ(roundtrip.x, world.x, 0.001f);
    T_FEQ(roundtrip.y, world.y, 0.001f);
    T_FEQ(WC3_SC2API_WorldToBoardDistance(64.0f), 2.0f, 0.001f);
    T_FEQ(WC3_SC2API_BoardToWorldDistance(2.0f), 64.0f, 0.001f);
}

TEST(wc3_sc2api, start_raw_exports_sc2_shaped_static_map_layers) {
    wc3Sc2StartRaw_t start;
    BYTE cells[64 * 64] = { 0 };
    LPBYTE pathing, terrain, placement;
    DWORD pathing_size, terrain_size, placement_size;
    LPMAPINFO mapinfo;

    setup_test_world();
    game.clients[0].ps.number = 0;
    world.map->center = CM_GetWorldBounds().min;
    cells[0] = WC3_PATH_UNWALKABLE;
    cells[1] = WC3_PATH_UNBUILDABLE;
    CM_SetupTestPathmap(64, 64, cells);
    mapinfo = (LPMAPINFO)level.mapinfo;
    mapinfo->cameraBounds.complement.left = 1;
    mapinfo->cameraBounds.complement.right = 2;
    mapinfo->cameraBounds.complement.bottom = 3;
    mapinfo->cameraBounds.complement.top = 4;
    mapinfo->players[0].used = true;
    mapinfo->players[0].playerType = kPlayerTypeHuman;
    mapinfo->players[0].startingPosition = (VECTOR2){ 0.0f, 0.0f };

    pathing_size = WC3_SC2API_PathingDataSize();
    terrain_size = WC3_SC2API_TerrainHeightDataSize();
    placement_size = WC3_SC2API_PlacementDataSize();
    pathing = calloc(pathing_size, 1);
    terrain = calloc(terrain_size, 1);
    placement = calloc(placement_size, 1);
    T_NOT_NULL(pathing);
    T_NOT_NULL(terrain);
    T_NOT_NULL(placement);

    T_ASSERT(WC3_SC2API_FillStartRaw(pathing, pathing_size,
                                     terrain, terrain_size,
                                     placement, placement_size, &start));
    T_EQ(start.map_size.x, 64);
    T_EQ(start.map_size.y, 64);
    T_EQ(start.pathing_grid.bits_per_pixel, 1);
    T_EQ(start.terrain_height.bits_per_pixel, 8);
    T_EQ(start.placement_grid.bits_per_pixel, 1);
    T_EQ(start.pathing_grid.data[0], 0x7f);
    T_EQ(start.placement_grid.data[0], 0x3f);
    T_EQ(start.terrain_height.data[0], 122);
    T_EQ(start.playable_area.p0.x, 4);
    T_EQ(start.playable_area.p0.y, 12);
    T_EQ(start.playable_area.p1.x, 56);
    T_EQ(start.playable_area.p1.y, 48);
    T_EQ(start.start_location_count, 1);
    T_FEQ(start.start_locations[0].x, 32.0f, 0.001f);
    T_FEQ(start.start_locations[0].y, 32.0f, 0.001f);

    free(pathing);
    free(terrain);
    free(placement);
}

TEST(wc3_sc2api, raw_point_command_converts_board_target_back_to_wc3_world) {
    wc3Sc2RawUnitCommand_t command;
    VECTOR2 issued;
    LPEDICT unit;

    reset_entities();
    setup_test_world();
    game.clients[0].ps.number = 0;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 256.0f);
    unit->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;

    memset(&command, 0, sizeof(command));
    command.ability_id = G_OrderId("move");
    command.unit_tag = WC3_SC2API_UnitTag(unit);
    command.target_type = WC3_SC2_TARGET_POINT;
    command.target_point = (VECTOR2){ 40.0f, 44.0f };

    T_EQ(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_ASSERT(G_GetIssuedOrderPoint(unit, &issued));
    T_FEQ(issued.x, 256.0f, 0.001f);
    T_FEQ(issued.y, 384.0f, 0.001f);
}

TEST(wc3_sc2api, protobuf_wire_codec_preserves_sc2_request_response_field_numbers) {
    BYTE data[64] = { 0 };
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbReader_t outer, ping;
    wc3Sc2PbMessageMark_t mark;
    DWORD field = 0, wire = 0;
    uint64_t value = 0;

    WC3_SC2API_PbWriterInit(&writer, data, sizeof(data));
    mark = WC3_SC2API_PbBeginMessage(&writer, 19); /* RequestPing / ResponsePing */
    T_ASSERT(mark.valid);
    T_ASSERT(WC3_SC2API_PbEndMessage(&writer, mark));
    T_ASSERT(WC3_SC2API_PbWriteVarintField(&writer, 97, 0));
    T_ASSERT(WC3_SC2API_PbWriteVarintField(&writer, 99, 1));
    T_ASSERT(!writer.failed);

    WC3_SC2API_PbReaderInit(&outer, data, writer.size);
    T_ASSERT(WC3_SC2API_PbNext(&outer, &field, &wire));
    T_EQ(field, 19);
    T_EQ(wire, 2);
    T_ASSERT(WC3_SC2API_PbReadSubmessage(&outer, &ping));
    T_EQ(ping.size, 0);
    T_ASSERT(WC3_SC2API_PbNext(&outer, &field, &wire));
    T_EQ(field, 97);
    T_ASSERT(WC3_SC2API_PbReadVarint(&outer, &value));
    T_EQ(value, 0);
    T_ASSERT(WC3_SC2API_PbNext(&outer, &field, &wire));
    T_EQ(field, 99);
    T_ASSERT(WC3_SC2API_PbReadVarint(&outer, &value));
    T_EQ(value, 1);
    T_ASSERT(!outer.failed);
}


TEST(wc3_sc2api, raw_producer_exports_rally_target_in_board_coordinates) {
    static UnitProfile_t const producer_profile = { .trains = "hfoo" };
    wc3Sc2RawUnit_t raw;
    LPEDICT producer;
    VECTOR2 point = { 320.0f, 448.0f };

    reset_entities();
    setup_test_world();
    producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 128.0f, 256.0f);
    producer->s.player = 0;
    producer->svflags |= SVF_MONSTER;
    producer->data.UnitProfile = &producer_profile;
    T_ASSERT(G_SetRallyPoint(producer, &point));

    T_ASSERT(WC3_SC2API_FillRawUnit(0, producer, &raw));
    T_EQ(raw.rally_target_count, 1);
    T_ASSERT(!raw.rally_targets[0].has_tag);
    T_FEQ(raw.rally_targets[0].point.x, WC3_SC2API_WorldToBoardPoint(&point).x, 0.001f);
    T_FEQ(raw.rally_targets[0].point.y, WC3_SC2API_WorldToBoardPoint(&point).y, 0.001f);
}

TEST(wc3_sc2api, unit_type_data_exports_structure_armor_and_weapon_shape) {
    DWORD const footman = MAKEFOURCC('h','f','o','o');
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    UnitBalance_t const *footman_balance = G_UnitBalance(footman);
    wc3Sc2UnitTypeData_t unit, building;

    T_ASSERT(WC3_SC2API_FillUnitTypeData(footman, &unit));
    T_ASSERT(WC3_SC2API_FillUnitTypeData(barracks, &building));
    T_FEQ(unit.armor, footman_balance->armor, 0.001f);
    T_ASSERT(unit.weapon_count > 0);
    T_ASSERT(unit.weapons[0].target >= WC3_SC2_WEAPON_GROUND && unit.weapons[0].target <= WC3_SC2_WEAPON_ANY);
    T_ASSERT(unit.weapons[0].attacks == 1);
    T_ASSERT(unit.weapons[0].speed > 0.0f);
    T_ASSERT(!unit.is_structure);
    T_ASSERT(building.is_structure);
}


TEST(wc3_sc2api, raw_orders_export_active_then_shift_queue_fifo) {
    wc3Sc2RawUnit_t raw;
    LPEDICT unit;
    VECTOR2 first = { 192.0f, 64.0f };
    VECTOR2 second = { 320.0f, 64.0f };

    reset_entities();
    setup_test_world();
    game.clients[0].ps.number = 0;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    unit->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit_stand(unit);

    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &first, false, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &second, true, 0, 0.0f));
    T_ASSERT(WC3_SC2API_FillRawUnit(0, unit, &raw));
    T_EQ(raw.order_count, 2);
    T_EQ(raw.orders[0].ability_id, G_OrderId("move"));
    T_ASSERT(raw.orders[0].has_point);
    T_FEQ(raw.orders[0].point.x, WC3_SC2API_WorldToBoardPoint(&first).x, 0.001f);
    T_EQ(raw.orders[1].ability_id, G_OrderId("move"));
    T_ASSERT(raw.orders[1].has_point);
    T_FEQ(raw.orders[1].point.x, WC3_SC2API_WorldToBoardPoint(&second).x, 0.001f);
}

TEST(wc3_sc2api, raw_stop_clears_active_order_and_hold_becomes_active_order) {
    wc3Sc2RawUnit_t raw;
    LPEDICT unit;
    VECTOR2 point = { 192.0f, 64.0f };

    reset_entities();
    setup_test_world();
    game.clients[0].ps.number = 0;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    unit->s.player = 0;
    unit->svflags |= SVF_MONSTER;
    unit->stand = unit_stand;
    unit_stand(unit);

    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &point, false, 0, 0.0f));
    T_ASSERT(unit_issueimmediateorder(unit, "stop"));
    T_ASSERT(WC3_SC2API_FillRawUnit(0, unit, &raw));
    T_EQ(raw.order_count, 0);

    T_ASSERT(unit_issueimmediateorder(unit, "holdposition"));
    T_ASSERT(WC3_SC2API_FillRawUnit(0, unit, &raw));
    T_EQ(raw.order_count, 1);
    T_EQ(raw.orders[0].ability_id, G_OrderId("holdposition"));
    T_ASSERT(!raw.orders[0].has_point);
    T_ASSERT(!raw.orders[0].has_target_unit);
}

TEST(wc3_sc2api, raw_destructable_uses_neutral_namespaced_unit_type) {
    static DestructableData_t const data = {
        .id = MAKEFOURCC('L','T','l','t'), .file = "Tree.mdx", .displayName = "Tree",
    };
    wc3Sc2RawUnit_t raw;
    LPEDICT ent;

    reset_entities();
    setup_test_world();
    game.clients[0].ps.number = 0;
    ent = G_Spawn();
    T_NOT_NULL(ent);
    ent->class_id = ent->s.class_id = data.id;
    ent->spawn_time = 1234;
    ent->data.DestructableData = &data;
    ent->destructable.initialized = true;
    ent->health.max_value = ent->health.value = 100.0f;
    ent->s.player = PLAYER_NEUTRAL_PASSIVE;
    gi.LinkEntity(ent);

    T_ASSERT(WC3_SC2API_FillRawUnit(0, ent, &raw));
    T_EQ(raw.alliance, WC3_SC2_ALLIANCE_NEUTRAL);
    T_EQ(raw.unit_type, WC3_SC2API_DestructableTypeId(data.id));
    T_FEQ(raw.health, 100.0f, 0.001f);
    T_EQ(raw.order_count, 0);
}

TEST(wc3_sc2api, unit_type_data_marks_heroes_with_sc2_heroic_attribute_source) {
    wc3Sc2UnitTypeData_t paladin;
    DWORD const hero = MAKEFOURCC('H','p','a','l');

    T_ASSERT(WC3_SC2API_FillUnitTypeData(hero, &paladin));
    T_ASSERT(paladin.is_heroic);
    T_ASSERT(paladin.has_ability_id);
}


#endif
