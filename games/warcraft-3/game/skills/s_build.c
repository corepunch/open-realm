#include "s_skills.h"

void build_walk(LPEDICT ent);
void build_build(LPEDICT ent);
void repair_build_legacy(LPEDICT ent, LPEDICT building);
void repair_build_primary(LPEDICT ent, LPEDICT building);

static void G_BuildError(LPEDICT clent, LPCSTR text) {
    if (!clent || !text || !*text) return;
    G_ShowCommandErrorText(clent, text);
}

static void G_BuildPlacementError(LPEDICT clent, buildPlacementResult_t placement) {
    if (placement == PLACE_TOO_CLOSE_TO_GOLD_MINE) {
        G_BuildError(clent, "Unable to build so close to the gold mine.");
        return;
    }
    G_BuildError(clent, "Unable to build there.");
}

static void G_ClearBuildPlacementCursor(LPEDICT clent) {
    entityState_t empty = { 0 };

    if (!clent || !clent->client) return;
    clent->build_project = 0;
    gi.Write(PF_BYTE, &(LONG){svc_cursor});
    gi.Write(PF_ENTITY, &empty);
    gi.unicast(clent);
}

static void ai_build_walk(LPEDICT ent) {
    LPEDICT goal = ent ? ent->goalentity : NULL;
    FLOAT distance, step, approach_range, reach;
    VECTOR2 approach = { 0, 0 };
    BOOL direct_approach;

    if (!ent || !goal || !ent->build_project) {
        if (ent && ent->stand) ent->stand(ent);
        return;
    }

    distance = M_DistanceToGoal(ent);
    step = unit_movedistance(ent);
    approach_range = G_BuildApproachDistance(ent->build_project) + ent->collision;
    reach = approach_range + step;
    if (distance <= reach) {
        build_build(ent);
        return;
    }

    if (move_is_blocked(ent, distance, step)) {
        G_BuildError(G_GetPlayerEntityByNumber(ent->s.player), "Unable to reach build site.");
        ent->stand(ent);
        return;
    }

    direct_approach = CM_FindDirectApproachPointForRadius(
        &ent->s.origin2, &goal->s.origin2, approach_range, ent->collision, &approach);
    if (direct_approach)
        unit_changeangle_towards_point(ent, &approach);
    else
        unit_changeangle_for_radius(ent, ent->collision);

    if (ent->movement.flow_unreachable) {
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
BOOL G_IssueBuildOrder(LPEDICT builder, DWORD building_id, LPCVECTOR2 location) {
    LPGAMECLIENT client;
    VECTOR2 snapped;
    LPEDICT waypoint;

    if (!builder || !location || !(client = G_GetPlayerClientByNumber(builder->s.player))) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING issue-build rejected reason=invalid-input builder=%ld building=%.4s\n",
                builder ? (long)(builder - globals.edicts) : -1L, (LPCSTR)&building_id);
#endif
        return false;
    }
    if (G_GetBuildCommandState(client, builder, building_id, NULL, 0) != BUILD_COMMAND_AVAILABLE) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING issue-build rejected reason=command-state builder=%ld building=%.4s\n",
                (long)(builder - globals.edicts), (LPCSTR)&building_id);
#endif
        return false;
    }
    if (G_EvaluateBuildPlacement(builder, building_id, location, &snapped) != PLACE_OK) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING issue-build rejected reason=placement builder=%ld building=%.4s requested=(%.1f,%.1f)\n",
                (long)(builder - globals.edicts), (LPCSTR)&building_id, location->x, location->y);
#endif
        return false;
    }
    waypoint = Waypoint_add(&snapped);
    if (!waypoint) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING issue-build rejected reason=waypoint builder=%ld building=%.4s point=(%.1f,%.1f)\n",
                (long)(builder - globals.edicts), (LPCSTR)&building_id, snapped.x, snapped.y);
#endif
        return false;
    }
#ifdef WC3_DEBUG_MINING
    fprintf(stderr, "WC3_MINING issue-build accepted builder=%ld building=%.4s point=(%.1f,%.1f)\n",
            (long)(builder - globals.edicts), (LPCSTR)&building_id, snapped.x, snapped.y);
#endif
    /* Build orders used to strand selected miners hidden inside the mine, permanently consuming its worker capacity. */
    S_GoldMineReleaseWorker(builder);
    builder->goalentity = waypoint;
    builder->build_project = building_id;
    move_reset_progress(builder);
    unit_setmove(builder, &build_move_walk);
    /* Warcraft reports an accepted construction placement as a point order.
     * Build orders expose the building rawcode as GetIssuedOrderId(), which is
     * how campaign GUI triggers distinguish the structure that was placed. */
    G_PublishIssuedPointOrder(builder, building_id, &snapped, builder->s.player, "build");
    return true;
}

static void FillUnitData(LPENTITYSTATE ent, DWORD unit_id, LPCSTR anim) {
    PATHSTR buffer = { 0 };
    UnitUI_t const *ui = G_UnitUI(unit_id);
    LPCSTR model_filename = ui->modelFile;
    if (!model_filename)
        return;
    G_NormalizeModelFilename(model_filename, buffer, sizeof(buffer));
    memset(ent, 0, sizeof(entityState_t));
    ent->class_id = unit_id;
    ent->model = G_RegisterModel(buffer);
    ent->scale = ui->modelScale;
    ent->angle = -M_PI / 2;
    if (S_UnitTypeIsGoldMine(unit_id)) ent->flags |= EF_RESOURCE_GOLD_SOURCE;
    if (S_UnitTypeReturnsGold(unit_id)) ent->flags |= EF_RESOURCE_GOLD_RETURN;
    {
        UnitData_t const *data = G_UnitData(unit_id);
        pathTex_t *pathtex = M_LoadPathTex(data->pathingTexture);
        if (pathtex) {
            ent->pathing_width = (USHORT)MIN(pathtex->width, USHRT_MAX);
            ent->pathing_height = (USHORT)MIN(pathtex->height, USHRT_MAX);
            gi.MemFree(pathtex);
        }
        /* Build-on-target placement needs parent eligibility that the client does
         * not yet receive. Suppress its coloured grid rather than showing a
         * misleading all-green preview; snapping still uses the dimensions. */
        if (!data->isBuildOn) {
            BYTE prevented = 0, required = 0;
            G_GetBuildPlacementPathingFlags(unit_id, &prevented, &required);
            ent->pathing_preview = EntityPathingPreviewPack(0, prevented, required);
        }
    }
    LPCANIMATION animation = G_GetAnimationForProperties(ent->model, anim, G_UnitProfile(unit_id)->animProps);
    if (animation) {
        ent->frame = animation->interval[0];
    }
}

void build_build(LPEDICT ent) {
    LPGAMECLIENT client;
    VECTOR2 snapped;
    buildPlacementResult_t placement;
    buildCommandState_t state;
    LPEDICT building;
    LPEDICT build_on = NULL;
    DWORD building_id;
    BOOL construction_started = false;
    unitRace_t race;

    if (!ent || !ent->goalentity || !ent->build_project) {
        if (ent) ent->stand(ent);
        return;
    }
    building_id = ent->build_project;
    client = G_GetPlayerClientByNumber(ent->s.player);
    placement = G_EvaluateBuildPlacement(ent, ent->build_project, &ent->goalentity->s.origin2, &snapped);
    state = G_GetBuildCommandState(client, ent, ent->build_project, NULL, 0);
    if (placement != PLACE_OK) {
#ifdef WC3_DEBUG_AI
        fprintf(stderr, "WC3_DEBUG_AI build arrival rejected worker=%ld id=%.4s placement=%d\n",
            (long)(ent - g_edicts), (LPCSTR)&ent->build_project, placement);
#endif
        G_BuildPlacementError(G_GetPlayerEntityByNumber(ent->s.player), placement);
        ent->build_project = 0;
        ent->stand(ent);
        return;
    }
    if (state != BUILD_COMMAND_AVAILABLE) {
#ifdef WC3_DEBUG_AI
        fprintf(stderr, "WC3_DEBUG_AI build arrival rejected worker=%ld id=%.4s state=%d\n",
            (long)(ent - g_edicts), (LPCSTR)&ent->build_project, state);
#endif
        G_BuildError(G_GetPlayerEntityByNumber(ent->s.player), "Unable to build: requirements changed.");
        ent->build_project = 0;
        ent->stand(ent);
        return;
    }
    if (!G_ChargeBuilding(client, ent->build_project)) {
#ifdef WC3_DEBUG_AI
        fprintf(stderr, "WC3_DEBUG_AI build arrival rejected worker=%ld id=%.4s payment\n",
            (long)(ent - g_edicts), (LPCSTR)&ent->build_project);
#endif
        G_BuildError(G_GetPlayerEntityByNumber(ent->s.player), "Not enough resources.");
        ent->build_project = 0;
        ent->stand(ent);
        return;
    }

    building = SP_SpawnAtLocation(ent->build_project, ent->s.player, &snapped);
    if (!building) {
        G_RefundBuilding(client, building_id);
        ent->build_project = 0;
        ent->stand(ent);
        return;
    }
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI build started worker=%ld building=%ld id=%.4s\n",
        (long)(ent - g_edicts), (long)(building - g_edicts), (LPCSTR)&ent->build_project);
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

    /* The structure blocks pathing as soon as construction starts. Bake its
     * authored footprint before relocating the worker so the egress search
     * cannot choose a point that becomes blocked immediately afterward. */
    CM_BakeStaticObstacles();
    if (!G_DisplaceBuildOccupants(ent, building)) {
        G_FreeEdict(building);
        CM_BakeStaticObstacles();
        G_RefundBuilding(client, building_id);
        ent->build_project = 0;
        ent->stand(ent);
        return;
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
    if (WC3_TUTORIAL_DEBUG_ENABLED()) {
        fprintf(stderr,
                "WC3_QUEST_BUILD start worker=%ld worker_id=%.4s building=%ld id=%.4s player=%u build_time=%d health=%.1f/%.1f worker_build=%ld building_build=%ld\n",
                (long)(ent - globals.edicts), (LPCSTR)&ent->class_id,
                (long)(building - globals.edicts), (LPCSTR)&building->class_id,
                (unsigned)building->s.player, building->data.UnitBalance->buildTime,
                building->health.value, building->health.max_value,
                ent->build ? (long)(ent->build - globals.edicts) : -1L,
                building->build ? (long)(building->build - globals.edicts) : -1L);
    }
    G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_START);
    G_RefreshResourceBar(G_GetPlayerEntityByNumber(ent->s.player));
    Get_Portrait_f(G_GetPlayerEntityByNumber(ent->s.player));
}

BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location) {
    LPEDICT builder;
    LPGAMECLIENT owner;
    VECTOR2 snapped;
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
                (LPCSTR)&clent->build_project);
#endif
        return false;
    }

    state = G_GetBuildCommandState(owner, builder, clent->build_project, reason, sizeof(reason));
    if (state != BUILD_COMMAND_AVAILABLE) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING build-click rejected reason=command-state client=%ld builder=%ld building=%.4s state=%d text=%s\n",
                (long)(clent - globals.edicts), (long)(builder - globals.edicts), (LPCSTR)&clent->build_project,
                state, reason[0] ? reason : "(none)");
#endif
        G_BuildError(clent, reason[0] ? reason : "Unable to build that structure.");
        return false;
    }
    placement = G_EvaluateBuildPlacement(builder, clent->build_project, location, &snapped);
    if (placement != PLACE_OK) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING build-click rejected reason=placement client=%ld builder=%ld building=%.4s result=%d point=(%.1f,%.1f)\n",
                (long)(clent - globals.edicts), (long)(builder - globals.edicts), (LPCSTR)&clent->build_project,
                placement, location->x, location->y);
#endif
        G_BuildPlacementError(clent, placement);
        return false;
    }

    if (!G_IssueBuildOrder(builder, clent->build_project, &snapped)) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING build-click rejected reason=issue-order client=%ld builder=%ld building=%.4s\n",
                (long)(clent - globals.edicts), (long)(builder - globals.edicts), (LPCSTR)&clent->build_project);
#endif
        return false;
    }
    G_PlayUISoundForPlayer(clent, "PlaceBuildingDefault");
    /* A successful point click consumes placement mode; leaving this callback
     * armed after build_project is cleared makes later Select commands look
     * like stale building placement. */
    clent->client->menu.on_location_selected = NULL;
    G_ClearBuildPlacementCursor(clent);
    return true;
}

BOOL G_CancelBuildPlacement(LPEDICT clent) {
    if (!clent || !clent->client ||
        clent->client->menu.on_location_selected != build_menu_send_builder) {
        return false;
    }

    clent->client->menu.on_location_selected = NULL;
    G_ClearBuildPlacementCursor(clent);
    Get_Commands_f(clent);
    return true;
}

void build_menu_selectlocation(LPEDICT ent, DWORD building_id) {
    entityState_t cursor;
    LPEDICT worker;
    LPGAMECLIENT owner;
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
    cursor.pathing_preview = EntityPathingPreviewPack(
        worker->s.number,
        EntityPathingPreviewPrevented(cursor.pathing_preview),
        EntityPathingPreviewRequired(cursor.pathing_preview));
    UI_AddCancelButton(ent);
    gi.Write(PF_BYTE, &(LONG){svc_cursor});
    gi.Write(PF_ENTITY, &cursor);
    gi.unicast(ent);
    ent->client->menu.on_location_selected = build_menu_send_builder;
    ent->build_project = building_id;
}

void ui_builds(LPGAMECLIENT client) {
    LPEDICT ent = G_GetMainSelectedUnit(client);
    LPGAMECLIENT owner = ent ? G_GetPlayerClientByNumber(ent->s.player) : NULL;
    LPCSTR builds = ent ? G_UnitProfile(ent->class_id)->builds : NULL;
    if (!ent || !owner || owner->ps.number != ent->s.player || !builds)
        return;
    PARSE_LIST(builds, build, parse_segment) {
        DWORD building_id = 0;
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

BZ_COMMAND_PROC(AbilityBuild) {
    LPGAMECLIENT client;

    if (!clent || !clent->client) return;
    client = clent->client;
    client->menu.cmdbutton = build_menu_selectlocation;
    client->menu.refresh = AbilityBuild_Command;

    /* The menu callbacks are gameplay state.  The command-bar payload is
     * presentation and cannot be serialized before ClientBegin. */
    if (client->connected) UI_WRITE_LAYER(clent, ui_builds, LAYER_COMMANDBAR);
}
