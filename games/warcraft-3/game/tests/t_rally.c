#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
extern void ai_train_build(edict_t *ent);

static UnitProfile_t const rally_train_profile = {
    .trains = "hpea",
};

static UnitProfile_t const rally_revive_profile = {
    .revive = "1",
};

static UnitProfile_t const rally_research_profile = {
    .researches = "Rhme",
};

static UnitAbilities_t rally_ancient_abilities = {
    .abilList = "Aro1",
};

static edict_t *rally_unit(uint32_t class_id, float x, float y) {
    edict_t *ent = alloc_test_unit(class_id, x, y);
    ent->svflags |= SVF_MONSTER;
    ent->health.value = ent->health.max_value = 100.0f;
    return ent;
}

TEST(wc3_rally, capability_is_train_or_revive_driven) {
    edict_t *producer;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    producer->data.UnitProfile = &rally_train_profile;
    T_ASSERT(G_UnitHasRally(producer));

    producer->data.UnitProfile = &rally_revive_profile;
    T_ASSERT(G_UnitHasRally(producer));

    producer->data.UnitProfile = &rally_research_profile;
    T_ASSERT(!G_UnitHasRally(producer));
}

TEST(wc3_rally, ancient_smart_orders_are_rally_only_while_rooted) {
    edict_t *producer;
    edict_t *target;
    vec2_t point = { 256.0f, 192.0f };
    edict_t *rally_target = NULL;

    reset_entities(); setup_test_world();
    producer = rally_unit(MAKEFOURCC('e','t','o','l'), 64.0f, 64.0f);
    target = rally_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 64.0f);
    producer->data.UnitProfile = &rally_train_profile;
    producer->data.UnitAbilities = &rally_ancient_abilities;
    if (!producer->ancient_root) producer->ancient_root = G_AllocAncientRoot();
    assert(producer->ancient_root);
    producer->ancient_root->ability = MAKEFOURCC('A','r','o','1');
    producer->ancient_root->mode = ANCIENT_ROOTED;
    producer->s.flags |= EF_BUILDING;
    producer->aiflags |= AI_IMMOBILE;

    T_ASSERT(G_UnitHasRally(producer));
    T_ASSERT(unit_issuetargetorder(producer, "smart", target));
    T_EQ(G_ResolveRallyTarget(producer, NULL, &rally_target), RALLY_TARGET_ENTITY);
    T_ASSERT(rally_target == target);

    producer->ancient_root->mode = ANCIENT_UPROOTED;
    producer->s.flags &= ~EF_BUILDING;
    producer->aiflags &= ~AI_IMMOBILE;
    producer->runtime.flags &= ~UNIT_BALANCE_BUILDING;
    producer->movetype = MOVETYPE_STEP;
    T_ASSERT(!G_UnitHasRally(producer));
    T_ASSERT(unit_issueorder(producer, "smart", &point));
    T_ASSERT(producer->currentmove && producer->currentmove->proc == CAbilityMove);
    T_ASSERT(producer->goalentity);
}

TEST(wc3_rally, command_handler_is_registered) {
    ability_t const *ability = FindAbilityByClassname(STR_CmdRally);
    T_NOT_NULL(ability);
    T_ASSERT(ability->flags & AB_COMMAND);
    T_EQ(ability->proc, CAbilityRally);
}

TEST(wc3_rally, default_target_is_producer_itself) {
    edict_t *producer;
    edict_t *target = NULL;
    vec2_t point;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 64, 96);
    producer->data.UnitProfile = &rally_train_profile;

    T_EQ(G_ResolveRallyTarget(producer, &point, &target), RALLY_TARGET_SELF);
    T_ASSERT(target == producer);
    T_FEQ(point.x, 64.0f, 0.01f);
    T_FEQ(point.y, 96.0f, 0.01f);
}

TEST(wc3_rally, default_self_target_does_not_issue_order_to_trained_unit) {
    edict_t *producer;
    edict_t *trained;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    trained = rally_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 0.0f);
    producer->data.UnitProfile = &rally_train_profile;
    producer->s.player = trained->s.player = 0;

    /* The default self target is displayed as the rally point, but completing
     * production must not Smart-interact with the producer. */
    T_ASSERT(!G_ApplyRallyOrder(producer, trained));
    T_NULL(trained->goalentity);
    T_NULL(trained->movement.follow_target);
    T_NULL(trained->build);
}

TEST(wc3_rally, explicit_smart_order_stops_at_building_footprint) {
    enum { W = 8, H = 8 };
    float const old_structure = game.constants.structureFollowRange;
    size_t const pathtex_size = sizeof(pathTex_t) + W * H * sizeof(color32_t);
    pathTex_t *pathtex;
    edict_t *producer, *trained;
    vec2_t exit;
    float angle;

    reset_entities();
    setup_test_world();
    producer = rally_unit(MAKEFOURCC('o','b','a','r'), 0.0f, 0.0f);
    trained = rally_unit(MAKEFOURCC('o','g','r','u'), 0.0f, 0.0f);
    producer->data.UnitProfile = &rally_train_profile;
    producer->s.player = trained->s.player = 0;
    producer->s.flags |= EF_BUILDING;
    producer->movetype = MOVETYPE_NONE;
    producer->collision = 128.0f;
    trained->collision = 16.0f;
    trained->stand = unit_stand;
    game.constants.structureFollowRange = 100.0f;

    pathtex = gi.MemAlloc(pathtex_size);
    T_NOT_NULL(pathtex);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = W;
    pathtex->height = H;
    FOR_LOOP(i, W * H) pathtex->map[i].b = 0xff;
    producer->pathtex = pathtex;
    CM_BakeStaticObstacles();

    T_ASSERT(SP_FindUnitExitPosition(producer, trained, &exit, &angle));
    trained->s.origin2 = exit;
    trained->s.origin.x = exit.x;
    trained->s.origin.y = exit.y;
    T_ASSERT(CM_PointIsPathableForRadius(&exit, trained->collision));
    T_ASSERT(CM_DistanceToPathingFootprint(producer, &exit) <
             game.constants.structureFollowRange);
    T_ASSERT(Vector2_distance(&producer->s.origin2, &exit) >
             G_FollowStopRange(trained, producer));

    /* Exercise the independent Smart-follow path without relying on default rally. */
    T_ASSERT(unit_issuetargetorder(trained, "smart", producer));
    T_ASSERT(trained->movement.follow_target == producer);
    T_ASSERT(trained->goalentity == producer);
    trained->animation = &(animation_t){ .name = "stand", .interval = { 0, 300 } };
    trained->currentmove->think(trained);
    T_ASSERT(trained->movement.follow_target == producer);
    T_ASSERT(G_AnimationHasPrimary(trained->animation, "stand"));

    producer->pathtex = NULL;
    gi.MemFree(pathtex);
    CM_BakeStaticObstacles();
    game.constants.structureFollowRange = old_structure;
}

TEST(wc3_rally, setrally_and_smart_store_point_and_widget_targets) {
    edict_t *producer;
    edict_t *target;
    edict_t *resolved = NULL;
    vec2_t point = { 320.0f, 448.0f };
    vec2_t resolved_point;

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

    target->s.origin2 = (vec2_t){ 192.0f, 224.0f };
    T_EQ(G_ResolveRallyTarget(producer, &resolved_point, &resolved), RALLY_TARGET_ENTITY);
    T_FEQ(resolved_point.x, 192.0f, 0.01f);
    T_FEQ(resolved_point.y, 224.0f, 0.01f);
}

TEST(wc3_rally, explicitly_setting_producer_target_is_distinct_from_default) {
    edict_t *producer;
    edict_t *produced;
    edict_t *target = NULL;

    reset_entities();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    produced = rally_unit(MAKEFOURCC('h','p','e','a'), 64, 0);
    producer->data.UnitProfile = &rally_train_profile;
    T_ASSERT(G_SetRallyPoint(producer, &MAKE(vec2_t, 64.0f, 64.0f)));
    T_ASSERT(G_SetRallyEntity(producer, producer));
    T_EQ(G_ResolveRallyTarget(producer, NULL, &target), RALLY_TARGET_ENTITY);
    T_ASSERT(target == producer);
    T_ASSERT(G_ApplyRallyOrder(producer, produced));
    T_ASSERT(produced->movement.follow_target == producer);
}

TEST(wc3_rally, dead_unit_target_resets_to_producer) {
    edict_t *producer;
    edict_t *target;
    edict_t *resolved = NULL;

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
    edict_t *producer;
    edict_t *target;
    edict_t *resolved = NULL;

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
    edict_t *clent, *producer, *target, *indicator;
    gameClient_t *client;

    reset_entities();
    setup_test_world();
    clent = &g_edicts[0]; client = &game.clients[0];
    clent->inuse = true; clent->client = client;
    client->connected = true; client->ps.number = 0; client->ps.color = 6;
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 64, 96);
    producer->data.UnitProfile = &rally_train_profile;
    producer->s.player = 0; G_SetEntitySelectionMask(producer, 1);

    T_ASSERT(G_SetRallyPoint(producer, &MAKE(vec2_t, 320.0f, 448.0f)));
    indicator = client->rally_indicator;
    T_NOT_NULL(indicator);
    T_ASSERT(indicator->inuse);
    T_ASSERT(indicator->rally_indicator);
    T_ASSERT(indicator->svflags & SVF_OWNER_ONLY);
    T_EQ(indicator->s.player, 0);
    T_EQ((indicator->s.effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT, 7);
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
    target->s.origin = (vec3_t){ 192.0f, 224.0f, 32.0f };
    G_RunEntity(indicator);
    T_FEQ(indicator->s.origin.x, 192.0f, 0.01f);
    T_FEQ(indicator->s.origin.y, 224.0f, 0.01f);
    T_FEQ(indicator->s.origin.z, 32.0f, 0.01f);

    G_SetEntitySelectionMask(producer, 0);
    G_UpdateRallyIndicator(client);
    T_NULL(client->rally_indicator);
    T_ASSERT(!indicator->inuse);
}

TEST(wc3_rally, point_handoff_uses_smart_movement) {
    edict_t *producer;
    edict_t *produced;
    vec2_t point = { 384.0f, 256.0f };

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

TEST(wc3_rally, explicit_entity_handoff_uses_smart_target_order) {
    edict_t *producer;
    edict_t *target;
    edict_t *produced;

    reset_entities();
    setup_test_world();
    producer = rally_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    target = rally_unit(MAKEFOURCC('h','t','o','w'), 256, 0);
    produced = rally_unit(MAKEFOURCC('h','p','e','a'), 64, 64);
    producer->data.UnitProfile = &rally_train_profile;
    produced->stand = unit_stand;

    T_ASSERT(G_SetRallyEntity(producer, target));
    T_ASSERT(G_ApplyRallyOrder(producer, produced));
    T_ASSERT(produced->movement.follow_target == target);
    T_ASSERT(produced->goalentity == target);
}

TEST(wc3_rally, training_completion_reads_latest_rally_target) {
    UnitBalance_t balance = { .buildTime = 1, .foodUsed = 0, .foodMade = 0 };
    edict_t *producer;
    edict_t *trained;
    vec2_t first = { 256.0f, 128.0f };
    vec2_t latest = { 512.0f, 320.0f };

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

TEST(wc3_rally, training_completion_leaves_unit_idle_on_default_rally) {
    UnitBalance_t balance = { .buildTime = 1, .foodUsed = 0, .foodMade = 0 };
    edict_t *producer;
    edict_t *trained;

    reset_entities();
    setup_test_world();
    producer = rally_unit(MAKEFOURCC('h','t','o','w'), 0, 0);
    trained = rally_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
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
    producer->health.max_value = 1000.0f;
    producer->health.value = 500.0f;

    ai_train_build(producer);

    T_ASSERT(!trained->training);
    T_ASSERT(!(trained->s.renderfx & RF_HIDDEN));
    T_NULL(trained->goalentity);
    T_NULL(trained->movement.follow_target);
    T_NULL(trained->build);
    T_ASSERT(!trained->buildwork || trained->buildwork->ability == 0);
}

/* Public native119 reaches normal order admission even for Rally metadata. */
TEST(wc3_rally, authored_ability_admits_native_replacement_and_rejection_preserves_queue) {
    char const slk[]="ID;PWXL;N;E\nB;X2;Y2;D0\n"
        "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;Y2;X1;K\"ARal\"\nC;X2;K\"ARal\"\nE\n";
    slkTestData_t *rows=parse_slk_string(slk),*old=G_SetSLKRows("AbilityData",rows);
    reset_entities();setup_test_world();
    edict_t *unit=rally_unit(MAKEFOURCC('h','f','o','o'),272,512);
    unit->stand=unit_stand;unit_stand(unit);
    vec2_t goal={1280,512},point={1600,768};uint32_t code=MAKEFOURCC('A','R','a','l');
    T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_UnitHasRally(unit));
    T_ASSERT(unit_issueorder(unit,"move",&goal));
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&goal,true,0,0));
    T_ASSERT(unit_issueorder(unit,"setrally",&point));
    T_EQ(unit->current_order_id,0);T_EQ(G_UnitQueuedOrderCount(unit),0);T_ASSERT(!unit_is_walking(unit));
    vec2_t resolved;T_EQ(G_ResolveRallyTarget(unit,&resolved,NULL),RALLY_TARGET_POINT);
    T_FEQ(resolved.x,point.x,0);T_FEQ(resolved.y,point.y,0);
    T_ASSERT(G_ActorRemoveSkill(unit,code));T_ASSERT(!G_UnitHasRally(unit));
    T_ASSERT(unit_issueorder(unit,"move",&goal));T_ASSERT(G_IssueUnitPointOrder(unit,"move",&goal,true,0,0));
    T_ASSERT(!unit_issueorder(unit,"setrally",&point));
    T_EQ(unit->current_order_id,G_OrderId("move"));T_EQ(G_UnitQueuedOrderCount(unit),1);
    T_ASSERT(WriteGame(Test_TempPath("wc3-rally119.bin")));
    T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(unit_issueorder(unit,"setrally",&point));
    T_ASSERT(ReadGame(Test_TempPath("wc3-rally119.bin")));
    T_ASSERT(!G_UnitHasRally(unit));T_EQ(unit->current_order_id,G_OrderId("move"));
    T_EQ(G_UnitQueuedOrderCount(unit),1);T_ASSERT(!unit_issueorder(unit,"setrally",&point));
    T_EQ(G_UnitQueuedOrderCount(unit),1);remove(Test_TempPath("wc3-rally119.bin"));
    G_SetSLKRows("AbilityData",old);free_slk_rows(rows);
}

#endif
