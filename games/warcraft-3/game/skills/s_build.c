#include "s_skills.h"

void build_walk(LPEDICT ent);
void build_build(LPEDICT ent);
void repair_build_legacy(LPEDICT ent, LPEDICT building);
void repair_build_primary(LPEDICT ent, LPEDICT building);

static void G_BuildError(LPEDICT clent, LPCSTR text) {
    if (!clent || !text || !*text) return;
    G_ShowCommandErrorText(clent, text);
}

static void G_BuildPlacementError(LPEDICT clent) {
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

static umove_t build_move_walk = { "walk", ai_build_walk, NULL, &a_build };

/* Shared callers submit only validated legal orders; build_build revalidates before charging at arrival. */
BOOL G_IssueBuildOrder(LPEDICT builder, DWORD building_id, LPCVECTOR2 location) {
    LPGAMECLIENT client;
    VECTOR2 snapped;
    LPEDICT waypoint;

    if (!builder || !location || !(client = G_GetPlayerClientByNumber(builder->s.player)) ||
        G_GetBuildCommandState(client, builder, building_id, NULL, 0) != BUILD_COMMAND_AVAILABLE ||
        G_EvaluateBuildPlacement(builder, building_id, location, &snapped) != PLACE_OK) return false;
    waypoint = Waypoint_add(&snapped);
    if (!waypoint) return false;
    /* Build orders used to strand selected miners hidden inside the mine, permanently consuming its worker capacity. */
    S_GoldMineReleaseWorker(builder);
    builder->goalentity = waypoint;
    builder->build_project = building_id;
    move_reset_progress(builder);
    unit_setmove(builder, &build_move_walk);
    return true;
}

static void FillUnitData(LPENTITYSTATE ent, DWORD unit_id, LPCSTR anim) {
    PATHSTR buffer = { 0 };
    UnitUI_t const *ui = G_UnitUI(unit_id);
    LPCSTR model_filename = ui->modelFile;
    if (!model_filename)
        return;
    snprintf(buffer, sizeof(buffer), "%s.mdx", model_filename);
    memset(ent, 0, sizeof(entityState_t));
    ent->class_id = unit_id;
    ent->model = G_RegisterModel(buffer);
    ent->scale = ui->modelScale;
    ent->angle = -M_PI / 2;
    {
        pathTex_t *pathtex = M_LoadPathTex(G_UnitData(unit_id)->pathingTexture);
        if (pathtex) {
            ent->pathing_width = (USHORT)MIN(pathtex->width, USHRT_MAX);
            ent->pathing_height = (USHORT)MIN(pathtex->height, USHRT_MAX);
            gi.MemFree(pathtex);
        }
    }
    LPCANIMATION animation = G_GetAnimation(ent->model, anim);
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

    if (!ent || !ent->goalentity || !ent->build_project) {
        if (ent) ent->stand(ent);
        return;
    }
    client = G_GetPlayerClientByNumber(ent->s.player);
    placement = G_EvaluateBuildPlacement(ent, ent->build_project, &ent->goalentity->s.origin2, &snapped);
    state = G_GetBuildCommandState(client, ent, ent->build_project, NULL, 0);
    if (placement != PLACE_OK) {
#ifdef WC3_DEBUG_AI
        fprintf(stderr, "WC3_DEBUG_AI build arrival rejected worker=%ld id=%.4s placement=%d\n",
            (long)(ent - g_edicts), (LPCSTR)&ent->build_project, placement);
#endif
        G_BuildPlacementError(G_GetPlayerEntityByNumber(ent->s.player));
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
        G_RefundBuilding(client, ent->build_project);
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

    /* The structure blocks pathing as soon as construction starts. Bake its
     * authored footprint before relocating the worker so the egress search
     * cannot choose a point that becomes blocked immediately afterward. */
    CM_BakeStaticObstacles();
    if (G_UnitHasHumanRepair(ent)) {
        G_StartHumanConstruction(ent, building);
        /* Cancellation refunds the exact base construction payment, not later
         * power-build Repair spending. Record that transaction on the spawned
         * structure while the paying client and authored cost are still known. */
        building->construction.payer = client->ps.number;
        if (!G_BuildAllEnabled()) {
            building->construction.paid = true;
            building->construction.gold = MAX(0, building->data.UnitBalance->goldCost);
            building->construction.lumber = MAX(0, building->data.UnitBalance->lumberCost);
        }
        repair_build_primary(ent, building);
    } else {
        /* Other race lifecycles remain the legacy behavior until their
         * worker-inside/summon construction strategies are implemented. */
        repair_build_legacy(ent, building);
        building->health.value = 0;
    }
    building->build = building;
    G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_START);
    G_RefreshResourceBar(G_GetPlayerEntityByNumber(ent->s.player));
    Get_Portrait_f(G_GetPlayerEntityByNumber(ent->s.player));
}

BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location) {
    LPEDICT builder;
    VECTOR2 snapped;
    buildPlacementResult_t placement;
    buildCommandState_t state;
    char reason[128];

    if (!clent || !clent->client || !location || !clent->build_project) return false;
    builder = G_GetMainSelectedUnit(clent->client);
    if (!builder) return false;

    state = G_GetBuildCommandState(clent->client, builder, clent->build_project, reason, sizeof(reason));
    if (state != BUILD_COMMAND_AVAILABLE) {
        G_BuildError(clent, reason[0] ? reason : "Unable to build that structure.");
        return false;
    }
    placement = G_EvaluateBuildPlacement(builder, clent->build_project, location, &snapped);
    if (placement != PLACE_OK) {
        G_BuildPlacementError(clent);
        return false;
    }

    if (!G_IssueBuildOrder(builder, clent->build_project, &snapped)) return false;
    G_PlayUISoundForPlayer(clent, "PlaceBuildingDefault");
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
    buildCommandState_t state;
    char reason[128];

    if (!ent || !ent->client) return;
    worker = G_GetMainSelectedUnit(ent->client);
    if (!worker || !G_WorkerCanBuild(worker, building_id)) return;
    state = G_GetBuildCommandState(ent->client, worker, building_id, reason, sizeof(reason));
    if (state != BUILD_COMMAND_AVAILABLE) {
        G_BuildError(ent, reason[0] ? reason : "Unable to build that structure.");
        return;
    }

    FillUnitData(&cursor, building_id, "stand");
    cursor.player = worker->s.player;
    UI_AddCancelButton(ent);
    gi.Write(PF_BYTE, &(LONG){svc_cursor});
    gi.Write(PF_ENTITY, &cursor);
    gi.unicast(ent);
    ent->client->menu.on_location_selected = build_menu_send_builder;
    ent->build_project = building_id;
}

void ui_builds(LPGAMECLIENT client) {
    LPEDICT ent = G_GetMainSelectedUnit(client);
    LPCSTR builds = ent ? G_UnitProfile(ent->class_id)->builds : NULL;
    if (!ent || !builds)
        return;
    PARSE_LIST(builds, build, parse_segment) {
        DWORD building_id = 0;
        gameCommandButton_t button;
        buildCommandState_t state;
        char reason[128];
        size_t used;

        if (strlen(build) != 4) continue;
        memcpy(&building_id, build, sizeof(building_id));
        state = G_GetBuildCommandState(client, ent, building_id, reason, sizeof(reason));
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

void build_command(LPEDICT edict) {
    LPGAMECLIENT client;

    if (!edict || !edict->client) return;
    client = edict->client;
    client->menu.cmdbutton = build_menu_selectlocation;
    client->menu.refresh = build_command;

    /* The menu callbacks are gameplay state.  The command-bar payload is
     * presentation and cannot be serialized before ClientBegin. */
    if (client->connected) UI_WRITE_LAYER(edict, ui_builds, LAYER_COMMANDBAR);
}

ability_t a_build = {
    .cmd = build_command,
};
