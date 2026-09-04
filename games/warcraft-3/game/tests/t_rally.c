#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
extern void ai_train_build(LPEDICT ent);

static UnitProfile_t const rally_train_profile = {
    .trains = "hpea",
};

static UnitProfile_t const rally_revive_profile = {
    .revive = "1",
};

static UnitProfile_t const rally_research_profile = {
    .researches = "Rhme",
};

static LPEDICT rally_unit(DWORD class_id, FLOAT x, FLOAT y) {
    LPEDICT ent = alloc_test_unit(class_id, x, y);
    ent->svflags |= SVF_MONSTER;
    ent->health.value = ent->health.max_value = 100.0f;
    return ent;
}

TEST(wc3_rally, capability_is_train_or_revive_driven) {
    LPEDICT producer;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    producer->data.UnitProfile = &rally_train_profile;
    T_ASSERT(G_UnitHasRally(producer));

    producer->data.UnitProfile = &rally_revive_profile;
    T_ASSERT(G_UnitHasRally(producer));

    producer->data.UnitProfile = &rally_research_profile;
    T_ASSERT(!G_UnitHasRally(producer));
}

TEST(wc3_rally, command_handler_is_registered) {
    ability_t const *ability = FindAbilityByClassname(STR_CmdRally);
    T_NOT_NULL(ability);
    T_NOT_NULL(ability->cmd);
}

TEST(wc3_rally, default_target_is_producer_itself) {
    LPEDICT producer;
    LPEDICT target = NULL;
    VECTOR2 point;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 64, 96);
    producer->data.UnitProfile = &rally_train_profile;

    T_EQ(G_ResolveRallyTarget(producer, &point, &target), RALLY_TARGET_SELF);
    T_ASSERT(target == producer);
    T_FEQ(point.x, 64.0f, 0.01f);
    T_FEQ(point.y, 96.0f, 0.01f);
}

TEST(wc3_rally, setrally_and_smart_store_point_and_widget_targets) {
    LPEDICT producer;
    LPEDICT target;
    LPEDICT resolved = NULL;
    VECTOR2 point = { 320.0f, 448.0f };
    VECTOR2 resolved_point;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    target = rally_unit(MAKEFOURCC('h','f','o','o'), 128, 160);
    producer->data.UnitProfile = &rally_train_profile;
    producer->aiflags |= AI_IMMOBILE;

    T_ASSERT(unit_issueorder(producer, "setrally", &point));
    T_EQ(G_ResolveRallyTarget(producer, &resolved_point, &resolved), RALLY_TARGET_POINT);
    T_NULL(resolved);
    T_FEQ(resolved_point.x, point.x, 0.01f);
    T_FEQ(resolved_point.y, point.y, 0.01f);

    T_ASSERT(unit_issuetargetorder(producer, "smart", target));
    T_EQ(G_ResolveRallyTarget(producer, &resolved_point, &resolved), RALLY_TARGET_ENTITY);
    T_ASSERT(resolved == target);

    target->s.origin2 = (VECTOR2){ 192.0f, 224.0f };
    T_EQ(G_ResolveRallyTarget(producer, &resolved_point, &resolved), RALLY_TARGET_ENTITY);
    T_FEQ(resolved_point.x, 192.0f, 0.01f);
    T_FEQ(resolved_point.y, 224.0f, 0.01f);
}

TEST(wc3_rally, setting_producer_as_target_restores_default) {
    LPEDICT producer;
    LPEDICT target = NULL;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    producer->data.UnitProfile = &rally_train_profile;
    T_ASSERT(G_SetRallyPoint(producer, &MAKE(VECTOR2, 64.0f, 64.0f)));
    T_ASSERT(G_SetRallyEntity(producer, producer));
    T_EQ(G_ResolveRallyTarget(producer, NULL, &target), RALLY_TARGET_SELF);
    T_ASSERT(target == producer);
}

TEST(wc3_rally, dead_unit_target_resets_to_producer) {
    LPEDICT producer;
    LPEDICT target;
    LPEDICT resolved = NULL;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    target = rally_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    producer->data.UnitProfile = &rally_train_profile;
    T_ASSERT(G_SetRallyEntity(producer, target));

    target->svflags |= SVF_DEADMONSTER;
    T_EQ(G_ResolveRallyTarget(producer, NULL, &resolved), RALLY_TARGET_SELF);
    T_ASSERT(resolved == producer);
}

TEST(wc3_rally, removing_widget_target_resets_before_edict_reuse) {
    LPEDICT producer;
    LPEDICT target;
    LPEDICT resolved = NULL;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    target = rally_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    producer->data.UnitProfile = &rally_train_profile;
    T_ASSERT(G_SetRallyEntity(producer, target));

    G_FreeEdict(target);
    T_EQ(G_ResolveRallyTarget(producer, NULL, &resolved), RALLY_TARGET_SELF);
    T_ASSERT(resolved == producer);
}

TEST(wc3_rally, selected_producer_owns_one_snapshot_indicator) {
    LPEDICT clent, producer, target, indicator;
    LPGAMECLIENT client;

    reset_entities();
    setup_test_world();
    clent = &g_edicts[0]; client = &game.clients[0];
    clent->inuse = true; clent->client = client;
    client->connected = true; client->ps.number = 0;
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 64, 96);
    producer->data.UnitProfile = &rally_train_profile;
    producer->s.player = 0; producer->selected = 1;

    T_ASSERT(G_SetRallyPoint(producer, &MAKE(VECTOR2, 320.0f, 448.0f)));
    indicator = client->rally_indicator;
    T_NOT_NULL(indicator);
    T_ASSERT(indicator->inuse);
    T_ASSERT(indicator->rally_indicator);
    T_ASSERT(indicator->svflags & SVF_OWNER_ONLY);
    T_EQ(indicator->s.player, 0);
    T_ASSERT(indicator->s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(indicator->s.flags & EF_GROUND_ANCHOR);
    T_NULL(indicator->goalentity);
    T_EQ(indicator->movetype, MOVETYPE_NONE);

    target = rally_unit(MAKEFOURCC('h','f','o','o'), 128, 160);
    target->s.player = 0;
    T_ASSERT(G_SetRallyEntity(producer, target));
    T_ASSERT(client->rally_indicator == indicator);
    T_ASSERT(indicator->goalentity == target);
    T_EQ(indicator->movetype, MOVETYPE_LINK);
    target->s.origin = (VECTOR3){ 192.0f, 224.0f, 32.0f };
    G_RunEntity(indicator);
    T_FEQ(indicator->s.origin.x, 192.0f, 0.01f);
    T_FEQ(indicator->s.origin.y, 224.0f, 0.01f);
    T_FEQ(indicator->s.origin.z, 32.0f, 0.01f);

    producer->selected = 0;
    G_UpdateRallyIndicator(client);
    T_NULL(client->rally_indicator);
    T_ASSERT(!indicator->inuse);
}

TEST(wc3_rally, point_handoff_uses_smart_movement) {
    LPEDICT producer;
    LPEDICT produced;
    VECTOR2 point = { 384.0f, 256.0f };

    reset_entities();
    setup_test_world();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    produced = rally_unit(MAKEFOURCC('h','f','o','o'), 64, 64);
    producer->data.UnitProfile = &rally_train_profile;
    produced->collision = 16.0f;
    produced->stand = unit_stand;

    T_ASSERT(G_SetRallyPoint(producer, &point));
    T_ASSERT(G_ApplyRallyOrder(producer, produced));
    T_NOT_NULL(produced->goalentity);
    T_FEQ(produced->goalentity->s.origin2.x, point.x, 0.01f);
    T_FEQ(produced->goalentity->s.origin2.y, point.y, 0.01f);
}

TEST(wc3_rally, training_completion_reads_latest_rally_target) {
    UnitBalance_t balance = { .buildTime = 1, .foodUsed = 0, .foodMade = 0 };
    LPEDICT producer;
    LPEDICT trained;
    VECTOR2 first = { 256.0f, 128.0f };
    VECTOR2 latest = { 512.0f, 320.0f };

    reset_entities();
    setup_test_world();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    trained = rally_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    producer->data.UnitProfile = &rally_train_profile;
    producer->s.player = trained->s.player = 0;
    producer->movetype = MOVETYPE_NONE;
    producer->collision = 64.0f;
    producer->stand = unit_stand;
    trained->collision = 16.0f;
    trained->stand = unit_stand;
    trained->data.UnitBalance = &balance;
    trained->health.value = trained->health.max_value = 100.0f;
    trained->training = true;
    trained->s.renderfx |= RF_HIDDEN;
    producer->build = trained;

    T_ASSERT(G_SetRallyPoint(producer, &first));
    T_ASSERT(G_SetRallyPoint(producer, &latest));
    ai_train_build(producer);

    T_ASSERT(!trained->training);
    T_ASSERT(!(trained->s.renderfx & RF_HIDDEN));
    T_NOT_NULL(trained->goalentity);
    T_FEQ(trained->goalentity->s.origin2.x, latest.x, 0.01f);
    T_FEQ(trained->goalentity->s.origin2.y, latest.y, 0.01f);
}

#endif
