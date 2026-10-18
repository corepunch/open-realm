#if defined(BZ_TESTS) && defined(WC3_SC2API)

#include "shared/test.h"

/* Drive the actual request/session dispatcher. Only delivery is captured; the
 * protobuf writer, game validation, queries and lifecycle are production code. */
static uint8_t sc2_test_reply[1024 * 1024];
static uint32_t sc2_test_reply_size;
static bool sc2_test_capture(uint8_t const *data, uint32_t size) {
    if (size > sizeof(sc2_test_reply)) return false;
    memcpy(sc2_test_reply, data, size);
    sc2_test_reply_size = size;
    return true;
}

static void sc2_test_begin(void) {
    WC3_SC2API_ServerShutdown();
    reset_entities();
    setup_test_world();
    sc2_test_reply_size = 0;
    sc2_transport_test_send = sc2_test_capture;
}

static void sc2_test_dispatch(uint32_t field, uint8_t const *payload, uint32_t size, uint32_t id) {
    uint8_t bytes[4096];
    wc3Sc2PbWriter_t writer;
    WC3_SC2API_PbWriterInit(&writer, bytes, sizeof(bytes));
    WC3_SC2API_PbWriteBytesField(&writer, field, payload, size);
    WC3_SC2API_PbWriteVarintField(&writer, 97, id);
    sc2_dispatch_request(bytes, writer.size);
}

static bool sc2_test_has_error(void) {
    wc3Sc2PbReader_t reader;
    uint32_t field, wire;
    WC3_SC2API_PbReaderInit(&reader, sc2_test_reply, sc2_test_reply_size);
    while (WC3_SC2API_PbNext(&reader, &field, &wire)) {
        if (field == 98) return true;
        if (!WC3_SC2API_PbSkip(&reader, wire)) break;
    }
    return false;
}

TEST(wc3_sc2api_protocol, ping_echoes_explicit_zero_id_and_rejects_invalid_lifecycle) {
    wc3Sc2PbReader_t reader;
    uint32_t field, wire;
    bool has_zero_id = false;
    sc2_test_begin();
    sc2_test_dispatch(19, NULL, 0, 0);
    T_ASSERT(sc2_test_reply_size > 0);
    T_ASSERT(!sc2_test_has_error());
    WC3_SC2API_PbReaderInit(&reader, sc2_test_reply, sc2_test_reply_size);
    while (WC3_SC2API_PbNext(&reader, &field, &wire)) {
        if (field == 97 && wire == 0) {
            uint64_t id;
            T_ASSERT(WC3_SC2API_PbReadVarint(&reader, &id));
            has_zero_id = id == 0;
        } else T_ASSERT(WC3_SC2API_PbSkip(&reader, wire));
    }
    T_ASSERT(has_zero_id);
    sc2_test_dispatch(12, NULL, 0, 1);
    T_ASSERT(sc2_test_has_error());
    sc2_transport_test_send = NULL;
}

TEST(wc3_sc2api_protocol, joined_player_consumes_normal_fog_grid) {
    uint8_t payload[128];
    wc3Sc2PbWriter_t writer;
    wc3Sc2PbMessageMark_t options;
    sc2_test_begin();
    G_FowInit();
    ((mapInfo_t *)level.mapinfo)->players[0] = (mapPlayer_t){
        .used = true, .playerType = kPlayerTypeHuman, .playerRace = kPlayerRaceHuman,
    };
    game.clients[0].ps.race = kPlayerRaceHuman;
    session.status = SC2_STATUS_INIT_GAME;
    WC3_SC2API_PbWriterInit(&writer, payload, sizeof(payload));
    WC3_SC2API_PbWriteVarintField(&writer, 1, WC3_SC2_RACE_HUMAN);
    options = WC3_SC2API_PbBeginMessage(&writer, 3);
    WC3_SC2API_PbWriteBoolField(&writer, 1, true);
    WC3_SC2API_PbEndMessage(&writer, options);
    sc2_test_dispatch(2, payload, writer.size, 2);
    T_ASSERT(session.joined);
    T_ASSERT(level.fow.players[0].client_connected);
    sc2_transport_test_send = NULL;
    G_FowShutdown();
}

static cstring_t sc2_test_enabled_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "sc2_api") ? "1" : fallback;
}

TEST(wc3_sc2api_protocol, manual_clock_stays_frozen_before_join_and_after_terminal_result) {
    cstring_t (*saved_cvar)(cstring_t, cstring_t) = gi.CvarString;
    sc2_test_begin();
    gi.CvarString = sc2_test_enabled_cvar;
    session.status = SC2_STATUS_INIT_GAME;
    T_ASSERT(WC3_SC2API_ExternalOwnsClock());
    session.joined = true;
    session.status = SC2_STATUS_ENDED;
    T_ASSERT(WC3_SC2API_ExternalOwnsClock());
    gi.CvarString = saved_cvar;
    sc2_transport_test_send = NULL;
}

#endif

#if defined(BZ_TESTS) && defined(WC3_SC2API)
TEST(wc3_sc2api_protocol, observed_enemy_building_keeps_snapshot_after_an_unseen_death) {
    wc3Sc2RawUnit_t raw;
    edict_t *building;
    sc2_test_begin();
    G_FowInit();
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    building->s.player = 1;
    building->svflags |= SVF_MONSTER;
    G_FowSetStateRadius(&(fogWrite_t){0, WC3_FOG_STATE_VISIBLE, false}, &building->s.origin2, 128);
    T_ASSERT(WC3_SC2API_FillRawUnit(0, building, &raw));
    uint64_t const tag = raw.tag;
    G_FowSetStateRadius(&(fogWrite_t){0, WC3_FOG_STATE_FOGGED, false}, &building->s.origin2, 128);
    building->health.value = 0;
    building->svflags |= SVF_DEADMONSTER;
    T_ASSERT(WC3_SC2API_FillRawUnit(0, building, &raw));
    T_EQ(raw.display_type, WC3_SC2_DISPLAY_SNAPSHOT);
    T_EQ(raw.tag, tag);
    T_ASSERT(!raw.has_live_data);
    G_FowSetStateRadius(&(fogWrite_t){0, WC3_FOG_STATE_VISIBLE, false}, &building->s.origin2, 128);
    T_ASSERT(!WC3_SC2API_FillRawUnit(0, building, &raw));
    G_FowShutdown();
    sc2_transport_test_send = NULL;
}

TEST(wc3_sc2api_protocol, query_preserves_live_routing_search_scratch) {
    uint8_t cells[64] = {0};
    vec2_t from = {1.5f, 2.5f}, target = {6.5f, 2.5f};
    pathNode_t *nodes;
    uint32_t *heap;
    sc2_test_begin();
    for (int y = 0; y < 6; y++) cells[4 + y * 8] = CM_PATHING_UNWALKABLE;
    setup_test_pathmap(8, 8, cells);
    nodes = malloc(sizeof(*nodes) * 64);
    heap = malloc(sizeof(*heap) * 64);
    memcpy(nodes, pathmap.pathnodes, sizeof(*nodes) * 64);
    memcpy(heap, pathmap.pathheap, sizeof(*heap) * 64);
    uint32_t const stamp = path_search_stamp;
    T_ASSERT(CM_PathDistanceForRadiusFlags(&from, &target, 0, CM_PATHING_UNWALKABLE) > 0);
    T_EQ(path_search_stamp, stamp);
    T_ASSERT(!memcmp(nodes, pathmap.pathnodes, sizeof(*nodes) * 64));
    T_ASSERT(!memcmp(heap, pathmap.pathheap, sizeof(*heap) * 64));
    free(nodes);
    free(heap);
    sc2_transport_test_send = NULL;
}
#endif

#if defined(BZ_TESTS) && defined(WC3_SC2API)
TEST(wc3_sc2api_protocol, websocket_backpressure_never_appends_a_partial_frame) {
    uint8_t payload[10] = {0};
    sc2_test_begin();
    transport.tx = gi.MemAlloc(SC2API_TX_CAPACITY);
    transport.tx_size = SC2API_TX_CAPACITY - 5;
    uint32_t const before = transport.tx_size;
    T_ASSERT(!transport_queue_frame(2, payload, sizeof(payload)));
    T_EQ(transport.tx_size, before);
    WC3_SC2API_ServerShutdown();
    sc2_transport_test_send = NULL;
}
#endif

#if defined(BZ_TESTS) && defined(WC3_SC2API)
static uint64_t sc2_test_nested_integer(uint8_t const *bytes, uint32_t size, uint32_t const *path, uint32_t depth) {
    wc3Sc2PbReader_t reader;
    uint32_t field, wire;
    WC3_SC2API_PbReaderInit(&reader, bytes, size);
    while (WC3_SC2API_PbNext(&reader, &field, &wire)) {
        if (field == path[0]) {
            if (depth == 1 && wire == 0) {
                uint64_t value;
                if (WC3_SC2API_PbReadVarint(&reader, &value)) return value;
            } else if (depth > 1 && wire == 2) {
                wc3Sc2PbReader_t sub;
                if (WC3_SC2API_PbReadSubmessage(&reader, &sub))
                    return sc2_test_nested_integer(sub.data, sub.size, path + 1, depth - 1);
            }
            return UINT64_MAX;
        }
        if (!WC3_SC2API_PbSkip(&reader, wire)) break;
    }
    return UINT64_MAX;
}

TEST(wc3_sc2api_protocol, realtime_clock_waits_for_join_and_stops_on_terminal_or_failed_create) {
    cstring_t (*saved)(cstring_t, cstring_t) = gi.CvarString;
    sc2_test_begin();
    gi.CvarString = sc2_test_enabled_cvar;
    session.realtime = true;
    session.status = SC2_STATUS_INIT_GAME;
    T_ASSERT(WC3_SC2API_ExternalOwnsClock());
    session.joined = true;
    session.status = SC2_STATUS_IN_GAME;
    T_ASSERT(!WC3_SC2API_ExternalOwnsClock());
    session.status = SC2_STATUS_ENDED;
    T_ASSERT(WC3_SC2API_ExternalOwnsClock());
    session.status = SC2_STATUS_LAUNCHED;
    T_ASSERT(WC3_SC2API_ExternalOwnsClock());
    gi.CvarString = saved;
    sc2_transport_test_send = NULL;
}

TEST(wc3_sc2api_protocol, rally_entity_cannot_reveal_hidden_movement_or_death) {
    static UnitProfile_t const profile = { .trains = "hfoo" };
    wc3Sc2RawUnit_t raw;
    sc2_test_begin();
    G_FowInit();
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256, 256);
    producer->s.player = 0; producer->svflags |= SVF_MONSTER; producer->data.UnitProfile = &profile;
    enemy->s.player = 1; enemy->svflags |= SVF_MONSTER;
    G_FowSetStateRadius(&(fogWrite_t){0, WC3_FOG_STATE_VISIBLE, false}, &enemy->s.origin2, 128);
    T_ASSERT(G_SetRallyEntity(producer, enemy));
    T_ASSERT(WC3_SC2API_FillRawUnit(0, producer, &raw));
    T_EQ(raw.rally_target_count, 1);
    G_FowSetStateRadius(&(fogWrite_t){0, WC3_FOG_STATE_FOGGED, false}, &enemy->s.origin2, 128);
    enemy->s.origin2 = (vec2_t){512, 512}; enemy->s.origin.x = 512; enemy->s.origin.y = 512;
    T_ASSERT(WC3_SC2API_FillRawUnit(0, producer, &raw));
    T_EQ(raw.rally_target_count, 0);
    G_InvalidateRallyTarget(enemy);
    T_ASSERT(WC3_SC2API_FillRawUnit(0, producer, &raw));
    T_EQ(raw.rally_target_count, 0);
    G_FowShutdown();
    sc2_transport_test_send = NULL;
}

TEST(wc3_sc2api_protocol, placement_query_rejects_an_explicit_stale_builder_tag) {
    uint8_t payload[128];
    wc3Sc2PbWriter_t writer;
    sc2_test_begin();
    session.joined = session.raw = true; session.status = SC2_STATUS_IN_GAME;
    WC3_SC2API_PbWriterInit(&writer, payload, sizeof(payload));
    wc3Sc2PbMessageMark_t placement = WC3_SC2API_PbBeginMessage(&writer, 3);
    WC3_SC2API_PbWriteVarintField(&writer, 1, MAKEFOURCC('h','b','a','r'));
    sc2_write_point2d(&writer, 2, &MAKE(vec2_t, .x = 32, .y = 32));
    WC3_SC2API_PbWriteVarintField(&writer, 3, UINT64_MAX);
    WC3_SC2API_PbEndMessage(&writer, placement);
    sc2_test_dispatch(14, payload, writer.size, 22);
    uint32_t const path[] = {14, 3, 1};
    T_EQ(sc2_test_nested_integer(sc2_test_reply, sc2_test_reply_size, path, 3), WC3_SC2_ACTION_CANT_TARGET);
    sc2_transport_test_send = NULL;
}
#endif

#if defined(BZ_TESTS) && defined(WC3_SC2API)
TEST(wc3_sc2api_protocol, production_action_uses_normal_training_and_exports_the_real_fifo) {
    static UnitProfile_t const profile = { .trains = "hpea" };
    wc3Sc2RawUnit_t raw;
    sc2_test_begin();
    gameClient_t *client = &game.clients[0];
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    producer->s.player = 0; producer->svflags |= SVF_MONSTER;
    producer->data.UnitProfile = &profile; producer->stand = unit_stand;
    wc3Sc2RawUnitCommand_t command = { .ability_id = MAKEFOURCC('h','p','e','a'),
        .unit_tag = WC3_SC2API_UnitTag(producer), .target_type = WC3_SC2_TARGET_NONE };
    T_EQ(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_EQ(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_EQ(G_ProductionQueueCount(producer), 2);
    T_ASSERT(WC3_SC2API_FillRawUnit(0, producer, &raw));
    T_EQ(raw.order_count, 2);
    T_EQ(raw.orders[0].ability_id, command.ability_id);
    T_EQ(raw.orders[1].ability_id, command.ability_id);
    T_ASSERT(!WC3_SC2API_FillRawUnit(0, producer->build, &raw));
    sc2_transport_test_send = NULL;
}

TEST(wc3_sc2api_protocol, known_deaths_and_terminal_results_are_consumed_after_delivery) {
    uint32_t const deaths[] = {10, 3, 5, 4, 1}, result[] = {10, 4, 2};
    sc2_test_begin();
    session.joined = session.raw = true; session.status = SC2_STATUS_IN_GAME;
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    unit->svflags |= SVF_MONSTER; unit->s.player = 0;
    uint64_t tag = WC3_SC2API_UnitTag(unit);
    WC3_SC2API_RecordDeath(unit);
    T_EQ(session.dead_unit_count, 1);
    game.clients[0].jass.removed = true;
    game.clients[0].ps.stats[PLAYERSTATE_GAME_RESULT] = 0;
    sc2_update_terminal_status();
    sc2_test_dispatch(10, NULL, 0, 30);
    T_EQ(sc2_test_nested_integer(sc2_test_reply, sc2_test_reply_size, deaths, 5), tag);
    T_EQ(sc2_test_nested_integer(sc2_test_reply, sc2_test_reply_size, result, 3), WC3_SC2_RESULT_VICTORY);
    T_EQ(session.dead_unit_count, 0);
    sc2_test_dispatch(10, NULL, 0, 31);
    T_EQ(sc2_test_nested_integer(sc2_test_reply, sc2_test_reply_size, deaths, 5), UINT64_MAX);
    T_EQ(sc2_test_nested_integer(sc2_test_reply, sc2_test_reply_size, result, 3), UINT64_MAX);
    game.clients[0].jass.removed = false;
    sc2_transport_test_send = NULL;
}
#endif

#if defined(BZ_TESTS) && defined(WC3_SC2API)
TEST(wc3_sc2api_protocol, allied_production_requires_advanced_control_and_spends_owner_resources) {
    static UnitProfile_t const profile = { .trains = "hpea" };
    gameCommandButton_t button = { .command = "hpea" };
    sc2_test_begin();
    gameClient_t *viewer = &game.clients[0], *owner = &game.clients[1];
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_SHARED_CONTROL, true);
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    producer->s.player = 1; producer->svflags |= SVF_MONSTER;
    producer->data.UnitProfile = &profile; producer->stand = unit_stand;
    owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    owner->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    owner->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    viewer->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    viewer->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;
    wc3Sc2RawUnitCommand_t command = { .ability_id = MAKEFOURCC('h','p','e','a'),
        .unit_tag = WC3_SC2API_UnitTag(producer), .target_type = WC3_SC2_TARGET_NONE };
    T_EQ(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_CANT_CONTROL_UNIT);
    T_EQ(G_ProductionQueueCount(producer), 0);
    T_ASSERT(!sc2_query_button_available(producer, &button, true));
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_SHARED_ADVANCED_CONTROL, true);
    T_ASSERT(sc2_query_button_available(producer, &button, false));
    T_EQ(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_EQ(G_ProductionQueueCount(producer), 1);
    T_EQ(viewer->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 0);
    T_EQ(viewer->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);
    T_ASSERT(owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] < 10000);
    sc2_transport_test_send = NULL;
}
#endif

#if defined(BZ_TESTS) && defined(WC3_SC2API)
TEST(wc3_sc2api_protocol, raw_spell_cannot_bypass_player_disable_mana_or_cooldown) {
    uint32_t const code = MAKEFOURCC('A','H','t','c');
    sc2_test_begin();
    cstring_t slk = "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"Cool1\"\n"
        "C;Y2;X1;K\"AHtc\"\nC;Y2;X2;K\"AHtc\"\nC;Y2;X3;K37\nC;Y2;X4;K5\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *saved = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    caster->s.player = 0; caster->svflags |= SVF_MONSTER; caster->stand = unit_stand;
    caster->heroabilities[0] = (heroability_t){ .code = code, .level = 1 };
    T_EQ(G_UnitAbilityLevel(caster, code), 1);
    caster->mana.max_value = caster->mana.value = 1000;
    wc3Sc2RawUnitCommand_t command = { .ability_id = G_OrderId("thunderclap"),
        .unit_tag = WC3_SC2API_UnitTag(caster), .target_type = WC3_SC2_TARGET_NONE };
    G_SetPlayerAbilityAvailable(&game.clients[0], code, false);
    T_NE(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_FEQ(caster->mana.value, 1000, 0.001f);
    G_SetPlayerAbilityAvailable(&game.clients[0], code, true);
    S_SpellStartCooldownDuration(caster, code, 60);
    gameCommandButton_t button = { .command = "AHtc", .manacost = 37, .cooldown = 1 };
    T_ASSERT(!sc2_query_button_available(caster, &button, false));
    T_ASSERT(sc2_query_button_available(caster, &button, true));
    T_NE(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_FEQ(caster->mana.value, 1000, 0.001f);
    S_SpellEndCooldown(caster, code);
    caster->mana.value = 0;
    T_NE(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_FEQ(caster->mana.value, 0, 0.001f);
    T_ASSERT(!sc2_query_button_available(caster, &button, true));
    caster->mana.value = 1000;
    caster->stunned = true;
    T_ASSERT(!sc2_query_button_available(caster, &button, true));
    caster->stunned = false;
    T_EQ(WC3_SC2API_IssueRawUnitCommand(0, &command), WC3_SC2_ACTION_SUCCESS);
    T_FEQ(caster->mana.value, 963, 0.001f);
    G_SetSLKRows("AbilityData", saved);
    free_slk_rows(rows);
    sc2_transport_test_send = NULL;
}
#endif
