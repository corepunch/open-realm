#include "s_skills.h"

void build_walk(LPEDICT ent);
void build_build(LPEDICT ent);
void repair_build(LPEDICT ent, LPEDICT building);
void repair_build_primary(LPEDICT ent, LPEDICT building);

static BOOL G_IsHumanBuilder(LPEDICT ent) {
    return ent && ent->UnitData && ent->UnitData->race && !strcmp(ent->UnitData->race, STR_HUMAN);
}

static void G_BuildError(LPEDICT clent, LPCSTR text) {
    if (!clent) return;
    UI_ShowText(clent, &MAKE(VECTOR2, 0, 0), text && *text ? text : "Unable to build there.", 2.0f);
}

static void ai_build_walk(LPEDICT ent) {
    FLOAT const reach = unit_movedistance(ent) + G_BuildApproachDistance(ent->build_project);
    if (M_DistanceToGoal(ent) <= reach) {
        build_build(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static umove_t build_move_walk = { "walk", ai_build_walk, NULL, &a_build };

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
            /* Cursor-only metadata: preserve the authored footprint dimensions so
             * the client uses the same 64/32 placement snap before confirmation. */
            ent->origin.x = (FLOAT)pathtex->width;
            ent->origin.y = (FLOAT)pathtex->height;
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
    if (placement != PLACE_OK || state != BUILD_COMMAND_AVAILABLE) {
        G_BuildError(G_GetPlayerEntityByNumber(ent->s.player),
                     placement == PLACE_OK ? "Unable to build: requirements changed." : "Unable to build there.");
        ent->stand(ent);
        return;
    }
    if (!G_ChargeBuilding(client, ent->build_project)) {
        G_BuildError(G_GetPlayerEntityByNumber(ent->s.player), "Not enough resources.");
        ent->stand(ent);
        return;
    }

    building = SP_SpawnAtLocation(ent->build_project, ent->s.player, &snapped);
    if (!building) {
        G_RefundBuilding(client, ent->build_project);
        ent->stand(ent);
        return;
    }

    if (G_IsHumanBuilder(ent)) {
        G_StartHumanConstruction(ent, building);
        repair_build_primary(ent, building);
    } else {
        /* Other race lifecycles remain the legacy behavior until their
         * worker-inside/summon construction strategies are implemented. */
        repair_build(ent, building);
        building->health.value = 0;
    }
    building->build = building;
    G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_START);
    CM_BakeStaticObstacles();
    G_RefreshResourceBar(G_GetPlayerEntityByNumber(ent->s.player));
    Get_Portrait_f(G_GetPlayerEntityByNumber(ent->s.player));
}

BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location) {
    LPEDICT builder;
    VECTOR2 snapped;
    buildPlacementResult_t placement;
    buildCommandState_t state;
    LPEDICT waypoint;
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
        G_BuildError(clent, "Unable to build there.");
        return false;
    }

    waypoint = Waypoint_add(&snapped);
    builder->goalentity = waypoint;
    builder->build_project = clent->build_project;
    unit_setmove(builder, &build_move_walk);
    entityState_t empty;
    memset(&empty, 0, sizeof(entityState_t));
    gi.Write(PF_BYTE, &(LONG){svc_cursor});
    gi.Write(PF_ENTITY, &empty);
    gi.unicast(clent);
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
    UI_WRITE_LAYER(edict, ui_builds, LAYER_COMMANDBAR);
    edict->client->menu.cmdbutton = build_menu_selectlocation;
}

ability_t a_build = {
    .cmd = build_command,
};
