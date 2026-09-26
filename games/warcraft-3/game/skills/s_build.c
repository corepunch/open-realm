#include "s_skills.h"

void build_walk(edict_t *ent);
void build_build(edict_t *ent);
void repair_build_legacy(edict_t *ent, edict_t *building);
void repair_build_primary(edict_t *ent, edict_t *building);

static void G_BuildError(edict_t *clent, cstring_t text) {
    if (!clent || !text || !*text) return;
    G_ShowCommandErrorText(clent, text);
}

static void G_BuildPlacementError(edict_t *clent, buildPlacementResult_t placement) {
    if (placement == PLACE_REQUIRES_BLIGHT) {
        G_ShowCommandErrorKey(clent, "Offblight", "Must summon structures upon Blight.");
        return;
    }
    if (placement == PLACE_TOO_CLOSE_TO_GOLD_MINE) {
        G_BuildError(clent, "Unable to build so close to the gold mine.");
        return;
    }
    G_BuildError(clent, "Unable to build there.");
}

static void G_ClearBuildPlacementCursor(edict_t *clent) {
    entityState_t empty = { 0 };

    if (!clent || !clent->client) return;
    clent->build_project = 0;
    gi.Write(PF_BYTE, &(int32_t){svc_cursor});
    gi.Write(PF_ENTITY, &empty);
    gi.unicast(clent);
}

static void ai_build_walk(edict_t *ent) {
    edict_t *goal = ent ? ent->goalentity : NULL;
    float distance, step, approach_range, reach;
    vec2_t approach = { 0, 0 };
    bool direct_approach;

    if (!ent || !goal || !ent->build_project) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD walk-stop worker=%ld reason=missing-state goal=%ld project=%.4s\n",
                ent ? (long)(ent - g_edicts) : -1L,
                goal && g_edicts ? (long)(goal - g_edicts) : -1L,
                ent && ent->build_project ? (cstring_t)&ent->build_project : "----");
#endif
        if (ent && ent->stand) ent->stand(ent);
        return;
    }

    distance = M_DistanceToGoal(ent);
    step = unit_movedistance(ent);
    if (move_displacement_active(ent) && !move_displacement_reached(ent)) {
        unit_setanimation(ent, "walk");
        unit_changeangle_towards_point(ent, &goal->s.origin2);
        unit_moveindirection(ent);
        return;
    }
    approach_range = G_BuildApproachDistance(ent->build_project) + ent->collision;
    reach = approach_range + step;
    if (distance <= reach) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD walk-arrive worker=%ld id=%.4s distance=%.1f reach=%.1f origin=(%.1f,%.1f) target=(%.1f,%.1f)\n",
                (long)(ent - g_edicts), (cstring_t)&ent->build_project, distance, reach,
                ent->s.origin2.x, ent->s.origin2.y, goal->s.origin2.x, goal->s.origin2.y);
#endif
        build_build(ent);
        return;
    }

    if (move_is_blocked(ent, distance, step)) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD walk-retry worker=%ld id=%.4s reason=blocked distance=%.1f step=%.1f flow=(%d,%d) origin=(%.1f,%.1f) target=(%.1f,%.1f)\n",
                (long)(ent - g_edicts), (cstring_t)&ent->build_project, distance, step,
                ent->movement.flow_goal_reached, ent->movement.flow_unreachable,
                ent->s.origin2.x, ent->s.origin2.y, goal->s.origin2.x, goal->s.origin2.y);
#endif
        /* A construction footprint makes the raw build center blocked, but
         * the worker may still have a valid route to its approach edge. Do
         * not turn a temporary settle watermark into an order cancellation;
         * clear it and let the build-specific route/fallback below retry. */
        move_reset_progress(ent);
    }

    direct_approach = CM_FindDirectApproachPointForRadius(
        &ent->s.origin2, &goal->s.origin2, approach_range, ent->collision, &approach);
    if (direct_approach)
        unit_changeangle_towards_point(ent, &approach);
    else
        unit_changeangle_for_radius(ent, ent->collision);

    if (ent->movement.flow_unreachable) {
        /* A real construction footprint is now static pathing, so its center
         * is intentionally unreachable. Build must continue to the closest
         * legal approach cell instead of treating the worker's point order as
         * cancelled; build_build() still validates and uses the original
         * waypoint when the approach range is reached. */
        if (CM_ClosestReachablePointForRadiusFlags(&ent->s.origin2, &goal->s.origin2,
                                                   ent->collision, M_UnitStaticPathingFlags(ent), &approach)) {
#ifdef WC3_DEBUG_BUILD
            fprintf(stderr, "WC3_BUILD walk-approach worker=%ld id=%.4s origin=(%.1f,%.1f) approach=(%.1f,%.1f) target=(%.1f,%.1f)\n",
                    (long)(ent - g_edicts), (cstring_t)&ent->build_project,
                    ent->s.origin2.x, ent->s.origin2.y, approach.x, approach.y,
                    goal->s.origin2.x, goal->s.origin2.y);
#endif
            unit_changeangle_towards_point(ent, &approach);
            if (!ent->movement.flow_unreachable) {
                unit_moveindirection(ent);
                return;
            }
        }
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD walk-stop worker=%ld id=%.4s reason=unreachable distance=%.1f origin=(%.1f,%.1f) target=(%.1f,%.1f)\n",
                (long)(ent - g_edicts), (cstring_t)&ent->build_project, distance,
                ent->s.origin2.x, ent->s.origin2.y, goal->s.origin2.x, goal->s.origin2.y);
#endif
        G_BuildError(G_GetPlayerEntityByNumber(ent->s.player), "Unable to reach build site.");
        ent->stand(ent);
        return;
    }
    unit_moveindirection(ent);
}

static umove_t build_move_walk = { "walk", ai_build_walk, NULL, CAbilityBuild };
/* Undead builders remain visible for the short summon animation while the
 * structure has already begun autonomous construction. The building-owned
 * construction timer releases this worker after the Warsmash 2.267 s window. */
static umove_t build_move_summon = { "stand work", NULL, NULL, CAbilityBuild };

/* Shared callers submit only validated legal orders; build_build revalidates before charging at arrival. */
bool G_ExecuteBuildOrder(edict_t *builder, uint32_t building_id, vec2_t const *location) {
    gameClient_t *client;
    vec2_t snapped;
    edict_t *waypoint;

    if (!builder || !location || !(client = G_GetPlayerClientByNumber(builder->s.player))) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD issue rejected worker=%ld id=%.4s reason=invalid-input\n",
                builder ? (long)(builder - g_edicts) : -1L, (cstring_t)&building_id);
#endif
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING issue-build rejected reason=invalid-input builder=%ld building=%.4s\n",
                builder ? (long)(builder - globals.edicts) : -1L, (cstring_t)&building_id);
#endif
        return false;
    }
    if (G_GetBuildCommandState(client, builder, building_id, NULL, 0) != BUILD_COMMAND_AVAILABLE) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD issue rejected worker=%ld id=%.4s reason=command-state\n",
                (long)(builder - g_edicts), (cstring_t)&building_id);
#endif
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING issue-build rejected reason=command-state builder=%ld building=%.4s\n",
                (long)(builder - globals.edicts), (cstring_t)&building_id);
#endif
        return false;
    }
    if (G_EvaluateBuildPlacement(builder, building_id, location, &snapped) != PLACE_OK) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD issue rejected worker=%ld id=%.4s reason=placement requested=(%.1f,%.1f)\n",
                (long)(builder - g_edicts), (cstring_t)&building_id, location->x, location->y);
#endif
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING issue-build rejected reason=placement builder=%ld building=%.4s requested=(%.1f,%.1f)\n",
                (long)(builder - globals.edicts), (cstring_t)&building_id, location->x, location->y);
#endif
        return false;
    }
    waypoint = Waypoint_add(&snapped);
    if (!waypoint) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD issue rejected worker=%ld id=%.4s reason=waypoint\n",
                (long)(builder - g_edicts), (cstring_t)&building_id);
#endif
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING issue-build rejected reason=waypoint builder=%ld building=%.4s point=(%.1f,%.1f)\n",
                (long)(builder - globals.edicts), (cstring_t)&building_id, snapped.x, snapped.y);
#endif
        return false;
    }
#ifdef WC3_DEBUG_BUILD
    fprintf(stderr, "WC3_BUILD issue accepted worker=%ld id=%.4s point=(%.1f,%.1f) origin=(%.1f,%.1f)\n",
            (long)(builder - g_edicts), (cstring_t)&building_id, snapped.x, snapped.y,
            builder->s.origin2.x, builder->s.origin2.y);
#endif
#ifdef WC3_DEBUG_MINING
    fprintf(stderr, "WC3_MINING issue-build accepted builder=%ld building=%.4s point=(%.1f,%.1f)\n",
            (long)(builder - globals.edicts), (cstring_t)&building_id, snapped.x, snapped.y);
#endif
    /* Build orders used to strand selected miners hidden inside the mine, permanently consuming its worker capacity. */
    S_GoldMineReleaseWorker(builder);
    builder->build_preview = G_CreateBuildPreview(builder, building_id, &snapped);
#ifdef WC3_DEBUG_BUILD
    fprintf(stderr, "WC3_BUILD issue preview worker=%ld id=%.4s preview=%ld\n",
            (long)(builder - g_edicts), (cstring_t)&building_id,
            builder->build_preview ? (long)(builder->build_preview - g_edicts) : -1L);
#endif
    if (!builder->build_preview) {
        G_FreeEdict(waypoint);
        return false;
    }
    builder->goalentity = waypoint;
    builder->build_project = building_id;
    move_reset_progress(builder);
    unit_setmove(builder, &build_move_walk);
    return true;
}

bool G_IssueBuildOrder(edict_t *builder, uint32_t building_id, vec2_t const *location) {
    vec2_t snapped;

    if (!G_ExecuteBuildOrder(builder, building_id, location)) return false;
    snapped = *location;
    G_SnapBuildingPoint(building_id, &snapped);
    /* Warcraft reports an accepted construction placement as a point order.
     * Build orders expose the building rawcode as GetIssuedOrderId(), which is
     * how campaign GUI triggers distinguish the structure that was placed. */
    G_PublishIssuedPointOrder(builder, building_id, &snapped, builder->s.player, "build");
    return true;
}

bool G_IssueUnitBuildOrder(edict_t *builder, uint32_t building_id, vec2_t const *location,
                           bool queue, uint32_t issuer_player) {
    vec2_t snapped;

    if (!builder || !building_id || !location || M_IsDead(builder) ||
        G_BuildingUpgradeActive(builder) || S_GoldMineWorkerIsInside(builder)) return false;
    snapped = *location;
    G_SnapBuildingPoint(building_id, &snapped);

    if (queue && G_UnitHasActiveOrder(builder)) {
        edict_t *preview;
        if (G_UnitQueuedOrderCount(builder) >= MAX_UNIT_ORDER_QUEUE) return false;
        preview = G_CreateBuildPreview(builder, building_id, &snapped);
        if (!preview) return false;
        if (!G_QueueUnitOrder(builder, "build", UNIT_ORDER_TARGET_BUILD, &snapped,
                              preview, issuer_player, 0.0f, building_id)) {
            G_FreeEdict(preview);
            return false;
        }
        G_PublishIssuedPointOrder(builder, building_id, &snapped, issuer_player, "build");
        return true;
    }
    if (!queue) G_ClearUnitOrderQueue(builder);
    if (!G_ExecuteBuildOrder(builder, building_id, &snapped)) return false;
    G_PublishIssuedPointOrder(builder, building_id, &snapped, issuer_player, "build");
    return true;
}

static void build_clear_queued_indicator(unitOrder_t const *queued) {
    edict_t *preview;

    if (!queued || queued->target_type != UNIT_ORDER_TARGET_BUILD ||
        !queued->target_number || queued->target_number >= globals.num_edicts) return;
    preview = globals.edicts + queued->target_number;
    if (preview->inuse && preview->spawn_time == queued->target_spawn_time)
        G_FreeEdict(preview);
}

static void FillUnitData(entityState_t *ent, uint32_t unit_id, cstring_t anim) {
    PATHSTR buffer = { 0 };
    UnitUI_t const *ui = G_UnitUI(unit_id);
    cstring_t model_filename = ui->modelFile;
    if (!model_filename)
        return;
    G_NormalizeModelFilename(model_filename, buffer, sizeof(buffer));
    memset(ent, 0, sizeof(entityState_t));
    ent->class_id = unit_id;
    ent->model = G_RegisterModel(buffer);
    ent->scale = ui->modelScale;
    ent->angle = -M_PI / 2;
    if (S_UnitTypeIsGoldMine(unit_id)) ent->flags |= EF_RESOURCE_SOURCE;
    if (S_UnitTypeReturnsGold(unit_id)) ent->flags |= EF_RESOURCE_RETURN;
    {
        UnitData_t const *data = G_UnitData(unit_id);
        pathTex_t *pathtex = M_LoadPathTex(data->pathingTexture);
        if (pathtex) {
            ent->pathing_width = (uint16_t)MIN(pathtex->width, USHRT_MAX);
            ent->pathing_height = (uint16_t)MIN(pathtex->height, USHRT_MAX);
            gi.MemFree(pathtex);
        }
        /* Build-on-target placement needs parent eligibility that the client does
         * not yet receive. Suppress its coloured grid rather than showing a
         * misleading all-green preview; snapping still uses the dimensions. */
        if (!data->isBuildOn) {
            uint8_t prevented = 0, required = 0;
            G_GetBuildPlacementPathingFlags(unit_id, &prevented, &required);
            ent->pathing_preview = EntityPathingPreviewPack(0, prevented, required);
        }
    }
    animation_t const *animation = G_GetAnimationForProperties(ent->model, anim, G_UnitProfile(unit_id)->animProps);
    if (animation) {
        ent->frame = animation->interval[0];
    }
}

void build_build(edict_t *ent) {
    gameClient_t *client;
    vec2_t snapped;
    buildPlacementResult_t placement;
    buildCommandState_t state;
    edict_t *building;
    edict_t *build_on = NULL;
    uint32_t building_id;
    bool construction_started = false;
    unitRace_t race;

    if (!ent || !ent->goalentity || !ent->build_project) {
        if (ent) ent->stand(ent);
        return;
    }
    building_id = ent->build_project;
#ifdef WC3_DEBUG_BUILD
    fprintf(stderr, "WC3_BUILD arrival worker=%ld id=%.4s origin=(%.1f,%.1f) goal=%ld preview=%ld\n",
            (long)(ent - g_edicts), (cstring_t)&building_id, ent->s.origin2.x, ent->s.origin2.y,
            ent->goalentity ? (long)(ent->goalentity - g_edicts) : -1L,
            ent->build_preview ? (long)(ent->build_preview - g_edicts) : -1L);
#endif
    /* The Birth placeholder is non-blocking and exists only until this
     * worker wins the arrival-time placement check. */
    G_ClearBuildPreview(ent);
    client = G_GetPlayerClientByNumber(ent->s.player);
    placement = G_EvaluateBuildPlacement(ent, ent->build_project, &ent->goalentity->s.origin2, &snapped);
    state = G_GetBuildCommandState(client, ent, ent->build_project, NULL, 0);
    if (placement != PLACE_OK) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD arrival-rejected worker=%ld id=%.4s reason=placement result=%d point=(%.1f,%.1f)\n",
                (long)(ent - g_edicts), (cstring_t)&building_id, placement,
                ent->goalentity->s.origin2.x, ent->goalentity->s.origin2.y);
#endif
#ifdef WC3_DEBUG_AI
        fprintf(stderr, "WC3_DEBUG_AI build arrival rejected worker=%ld id=%.4s placement=%d\n",
            (long)(ent - g_edicts), (cstring_t)&ent->build_project, placement);
#endif
        G_BuildPlacementError(G_GetPlayerEntityByNumber(ent->s.player), placement);
        ent->build_project = 0;
        ent->stand(ent);
        return;
    }
    if (state != BUILD_COMMAND_AVAILABLE) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD arrival-rejected worker=%ld id=%.4s reason=state state=%d\n",
                (long)(ent - g_edicts), (cstring_t)&building_id, state);
#endif
#ifdef WC3_DEBUG_AI
        fprintf(stderr, "WC3_DEBUG_AI build arrival rejected worker=%ld id=%.4s state=%d\n",
            (long)(ent - g_edicts), (cstring_t)&ent->build_project, state);
#endif
        G_BuildError(G_GetPlayerEntityByNumber(ent->s.player), "Unable to build: requirements changed.");
        ent->build_project = 0;
        ent->stand(ent);
        return;
    }
    if (!G_ChargeBuilding(client, ent->build_project)) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD arrival-rejected worker=%ld id=%.4s reason=payment\n",
                (long)(ent - g_edicts), (cstring_t)&building_id);
#endif
#ifdef WC3_DEBUG_AI
        fprintf(stderr, "WC3_DEBUG_AI build arrival rejected worker=%ld id=%.4s payment\n",
            (long)(ent - g_edicts), (cstring_t)&ent->build_project);
#endif
        G_BuildError(G_GetPlayerEntityByNumber(ent->s.player), "Not enough resources.");
        ent->build_project = 0;
        ent->stand(ent);
        return;
    }

    building = SP_SpawnAtLocation(ent->build_project, ent->s.player, &snapped);
    if (!building) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD arrival-rejected worker=%ld id=%.4s reason=spawn\n",
                (long)(ent - g_edicts), (cstring_t)&building_id);
#endif
        G_RefundBuilding(client, building_id);
        ent->build_project = 0;
        ent->stand(ent);
        return;
    }
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI build started worker=%ld building=%ld id=%.4s\n",
        (long)(ent - g_edicts), (long)(building - g_edicts), (cstring_t)&ent->build_project);
#endif
#ifdef WC3_DEBUG_BUILD
    fprintf(stderr, "WC3_BUILD spawned worker=%ld building=%ld id=%.4s point=(%.1f,%.1f)\n",
            (long)(ent - g_edicts), (long)(building - g_edicts), (cstring_t)&building_id,
            building->s.origin2.x, building->s.origin2.y);
#endif
    /* G_ChargeBuilding validates food before spawn. Once the structure exists,
     * make the entity own that accounted Food Used so death/removal can release
     * exactly the same contribution. The build-all override intentionally keeps
     * its historical no-resource-cost behavior. */
    if (!G_BuildAllEnabled()) G_SetUnitFoodUsed(building, building->data.UnitBalance->foodUsed);
    ent->build_project = 0;

    /* Build-on-mine structures retain the original Agld entity as the shared
     * finite resource reservoir. Placement already proved the parent exists;
     * bind before baking pathing so the hidden parent drops out as the overlay
     * footprint becomes authoritative. */
    if (!G_FindBuildOnTarget(building_id, &snapped, &build_on) ||
        (build_on && (G_ActorHasSkill(building, "Agl2") || G_ActorHasSkill(building, "Abgm") ||
                      G_ActorHasSkill(building, "Aegm")) &&
         !S_MineOverlayBind(building, build_on))) {
        G_FreeEdict(building);
        G_RefundBuilding(client, building_id);
        ent->stand(ent);
        return;
    }

    /* Retail's Birth construction site reserves placement but remains
     * walk-through.  Displacement uses the authored footprint directly; the
     * static obstacle is baked only after Birth completes. */
    if (!G_DisplaceBuildOccupants(ent, building)) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD spawned-displace-incomplete worker=%ld building=%ld id=%.4s\n",
                (long)(ent - g_edicts), (long)(building - g_edicts), (cstring_t)&building_id);
#endif
    }
    race = WC3_RaceFromString(ent->data.UnitData ? ent->data.UnitData->race : NULL);
    /* Repair is shared by worker data, but only Human construction uses the
     * external Repair clock; Orc Peons must enter the hidden worker-owned path. */
    if (race == RACE_HUMAN && G_UnitHasHumanRepair(ent)) {
        construction_started = G_StartHumanConstruction(ent, building);
    } else {
        switch (race) {
        case RACE_ORC: construction_started = G_StartOrcConstruction(ent, building); break;
        case RACE_UNDEAD: construction_started = G_StartUndeadConstruction(ent, building); break;
        case RACE_NIGHTELF: construction_started = G_StartNightElfConstruction(ent, building); break;
        default: break;
        }
    }
    if (construction_started) {
#ifdef WC3_DEBUG_BUILD
        fprintf(stderr, "WC3_BUILD construction-start worker=%ld building=%ld id=%.4s type=%d health=%.1f/%.1f\n",
                (long)(ent - g_edicts), (long)(building - g_edicts), (cstring_t)&building_id,
                building->construction.type, building->health.value, building->health.max_value);
#endif
        /* Cancellation refunds the exact base construction payment, not later
         * power-build Repair spending. Record that transaction on the spawned
         * structure while the paying client and authored cost are still known. */
        building->construction.payer = client->ps.number;
        if (!G_BuildAllEnabled()) {
            building->construction.paid = true;
            building->construction.gold = MAX(0, building->data.UnitBalance->goldCost);
            building->construction.lumber = MAX(0, building->data.UnitBalance->lumberCost);
        }
        if (building->construction.type == CONSTRUCTION_HUMAN)
            repair_build_primary(ent, building);
        else if (building->construction.type == CONSTRUCTION_UNDEAD)
            unit_setmove(ent, &build_move_summon);
    } else {
        /* Preserve the old generic fallback for custom/unknown workers. */
        repair_build_legacy(ent, building);
        G_SetHealth(building, 0);
    }
    building->build = building;
    if (!construction_started) G_SetConstructionLoopSound(building, true);
    if (WC3_TUTORIAL_DEBUG_ENABLED()) {
        fprintf(stderr,
                "WC3_QUEST_BUILD start worker=%ld worker_id=%.4s building=%ld id=%.4s player=%u build_time=%d health=%.1f/%.1f worker_build=%ld building_build=%ld\n",
                (long)(ent - globals.edicts), (cstring_t)&ent->class_id,
                (long)(building - globals.edicts), (cstring_t)&building->class_id,
                (unsigned)building->s.player, building->data.UnitBalance->buildTime,
                building->health.value, building->health.max_value,
                ent->build ? (long)(ent->build - globals.edicts) : -1L,
                building->build ? (long)(building->build - globals.edicts) : -1L);
    }
    G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_START);
    G_RefreshResourceBar(G_GetPlayerEntityByNumber(ent->s.player));
    Get_Portrait_f(G_GetPlayerEntityByNumber(ent->s.player));
}

bool build_menu_send_builder(edict_t *clent, vec2_t const *location) {
    edict_t *builder;
    gameClient_t *owner;
    vec2_t snapped;
    buildPlacementResult_t placement;
    buildCommandState_t state;
    char reason[128];

    if (!clent || !clent->client || !location || !clent->build_project) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING build-click rejected reason=invalid-placement-state client=%ld\n",
                clent ? (long)(clent - globals.edicts) : -1L);
#endif
        return false;
    }
    builder = G_GetMainSelectedUnit(clent->client);
    owner = builder ? G_GetPlayerClientByNumber(builder->s.player) : NULL;
    if (!owner || owner->ps.number != builder->s.player) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING build-click rejected reason=invalid-owner client=%ld builder=%ld building=%.4s\n",
                (long)(clent - globals.edicts), builder ? (long)(builder - globals.edicts) : -1L,
                (cstring_t)&clent->build_project);
#endif
        return false;
    }

    state = G_GetBuildCommandState(owner, builder, clent->build_project, reason, sizeof(reason));
    if (state != BUILD_COMMAND_AVAILABLE) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING build-click rejected reason=command-state client=%ld builder=%ld building=%.4s state=%d text=%s\n",
                (long)(clent - globals.edicts), (long)(builder - globals.edicts), (cstring_t)&clent->build_project,
                state, reason[0] ? reason : "(none)");
#endif
        G_BuildError(clent, reason[0] ? reason : "Unable to build that structure.");
        return false;
    }
    placement = G_EvaluateBuildPlacement(builder, clent->build_project, location, &snapped);
    if (placement != PLACE_OK) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING build-click rejected reason=placement client=%ld builder=%ld building=%.4s result=%d point=(%.1f,%.1f)\n",
                (long)(clent - globals.edicts), (long)(builder - globals.edicts), (cstring_t)&clent->build_project,
                placement, location->x, location->y);
#endif
        G_BuildPlacementError(clent, placement);
        return false;
    }

    if (!G_IssueUnitBuildOrder(builder, clent->build_project, &snapped,
                               clent->client->menu.order_queued,
                               clent->client->ps.number)) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING build-click rejected reason=issue-order client=%ld builder=%ld building=%.4s\n",
                (long)(clent - globals.edicts), (long)(builder - globals.edicts), (cstring_t)&clent->build_project);
#endif
        return false;
    }
    G_PlayUISoundForPlayer(clent, "PlaceBuildingDefault");
    if (clent->client->menu.order_queued) {
        /* Shift construction is sticky only after a successful queued click.
         * The client notifies the server when the final Shift key is released. */
        clent->client->menu.order_queue_chained = true;
        return true;
    }

    /* A successful non-Shift point click consumes placement mode. */
    G_ClearBuildPlacementMode(clent);
    return true;
}

bool G_ClearBuildPlacementMode(edict_t *clent) {
    if (!clent || !clent->client ||
        clent->client->menu.on_location_selected != build_menu_send_builder) {
        return false;
    }

    clent->client->menu.on_location_selected = NULL;
    clent->client->menu.supports_order_queue = false;
    clent->client->menu.order_queued = false;
    clent->client->menu.order_queue_chained = false;
    G_ClearBuildPlacementCursor(clent);
    return true;
}

bool G_CancelBuildPlacement(edict_t *clent) {
    if (!G_ClearBuildPlacementMode(clent)) return false;
    Get_Commands_f(clent);
    return true;
}

void build_menu_selectlocation(edict_t *ent, uint32_t building_id) {
    entityState_t cursor;
    edict_t *worker;
    gameClient_t *owner;
    buildCommandState_t state;
    char reason[128];

    if (!ent || !ent->client) return;
    worker = G_GetMainSelectedUnit(ent->client);
    owner = worker ? G_GetPlayerClientByNumber(worker->s.player) : NULL;
    if (!owner || owner->ps.number != worker->s.player || !G_WorkerCanBuild(worker, building_id)) return;
    state = G_GetBuildCommandState(owner, worker, building_id, reason, sizeof(reason));
    if (state != BUILD_COMMAND_AVAILABLE) {
        G_BuildError(ent, reason[0] ? reason : "Unable to build that structure.");
        return;
    }

    FillUnitData(&cursor, building_id, "stand");
    cursor.player = worker->s.player;
    G_SetEntityTeamColor(&cursor, owner->ps.color);
    cursor.pathing_preview = EntityPathingPreviewPack(
        worker->s.number,
        EntityPathingPreviewPrevented(cursor.pathing_preview),
        EntityPathingPreviewRequired(cursor.pathing_preview));
    UI_AddCancelButton(ent);
    gi.Write(PF_BYTE, &(int32_t){svc_cursor});
    gi.Write(PF_ENTITY, &cursor);
    gi.unicast(ent);
    ent->client->menu.on_location_selected = build_menu_send_builder;
    ent->client->menu.supports_order_queue = true;
    ent->client->menu.order_queue_chained = false;
    ent->build_project = building_id;
}

void ui_builds(gameClient_t *client) {
    edict_t *ent = G_GetMainSelectedUnit(client);
    gameClient_t *owner = ent ? G_GetPlayerClientByNumber(ent->s.player) : NULL;
    cstring_t builds = ent ? G_UnitProfile(ent->class_id)->builds : NULL;
    if (!ent || !owner || owner->ps.number != ent->s.player || !builds)
        return;
    PARSE_LIST(builds, build, parse_segment) {
        uint32_t building_id = 0;
        gameCommandButton_t button;
        buildCommandState_t state;
        char reason[128];
        size_t used;

        if (strlen(build) != 4) continue;
        memcpy(&building_id, build, sizeof(building_id));
        state = G_GetBuildCommandState(owner, ent, building_id, reason, sizeof(reason));
        if (state == BUILD_COMMAND_ABSENT || state == BUILD_COMMAND_HIDDEN) continue;
        if (!G_BuildCommandButton(ent, build, false, 0, &button)) continue;
        if (state == BUILD_COMMAND_DISABLED) {
            button.disabled = 1;
            used = strlen(button.ubertip);
            snprintf(button.ubertip + used, sizeof(button.ubertip) - used,
                     "%s|cffffcc00%s|r", used ? "|n" : "", reason);
        }
        UI_WriteCommandButtonFrame(&button);
    }
    UI_AddCommandButton(STR_CmdCancel);
    UI_WriteTooltipFrame();
}

static void AbilityBuild_Command(edict_t *clent);

BZ_ABILITY_PROC(CAbilityBuild) {
    unitOrder_t const *queued = call ? call->queued_order : NULL;

    if (msg == A_COMMAND) {
        AbilityBuild_Command(call && call->client ? call->client : ent);
        return true;
    }
    if ((msg == A_QUEUE_ORDER_START || msg == A_QUEUE_ORDER_CANCEL) && queued &&
        queued->target_type == UNIT_ORDER_TARGET_BUILD) {
        build_clear_queued_indicator(queued);
        if (msg == A_QUEUE_ORDER_CANCEL) return true;
        return call->item && call->item->code &&
               G_ExecuteBuildOrder(ent, call->item->code, &queued->point);
    }
    return false;
}

static void AbilityBuild_Command(edict_t *clent) {
    gameClient_t *client;

    if (!clent || !clent->client) return;
    client = clent->client;
    client->menu.cmdbutton = build_menu_selectlocation;
    client->menu.refresh = AbilityBuild_Command;

    /* The menu callbacks are gameplay state.  The command-bar payload is
     * presentation and cannot be serialized before ClientBegin. */
    if (client->connected) UI_WRITE_LAYER(clent, ui_builds, LAYER_COMMANDBAR);
}
