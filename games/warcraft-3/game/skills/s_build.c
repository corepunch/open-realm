#include "s_skills.h"

void build_walk(LPEDICT ent);
void build_build(LPEDICT ent);
void repair_build(LPEDICT ent, LPEDICT building);

static void ai_build_walk(LPEDICT ent) {
    if (M_DistanceToGoal(ent) <= unit_movedistance(ent)) {
        build_build(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static void ai_build(LPEDICT ent) {
    FLOAT const k = (FLOAT)FRAMETIME / (FLOAT)UNIT_BUILD_TIME_MSEC(ent->build->class_id);
    EDICTSTAT *hp = &ent->build->health;
    hp->value += hp->max_value * k;
    if (hp->value >= hp->max_value) {
        LPEDICT clent = G_GetPlayerEntityByNumber(ent->s.player);

        hp->value = hp->max_value;
        G_PublishEvent(ent->build, EVENT_PLAYER_UNIT_CONSTRUCT_FINISH);
        ent->build->stand(ent->build);
        ent->stand(ent);
        Get_Portrait_f(clent);
    }
}

static umove_t build_move_walk = { "walk", ai_build_walk, NULL, &a_build };
static umove_t build_move_stand = { "stand", ai_build, NULL, &a_build };

static void FillUnitData(LPENTITYSTATE ent, DWORD unit_id, LPCSTR anim) {
    PATHSTR buffer = { 0 };
    LPCSTR model_filename = UNIT_MODEL(unit_id);
    if (!model_filename)
        return;
    sprintf(buffer, "%s.mdx", model_filename);
    memset(ent, 0, sizeof(entityState_t));
    ent->class_id = unit_id;
    ent->model = G_RegisterModel(buffer);
    ent->scale = UNIT_SCALING_VALUE(unit_id);
    ent->angle = -M_PI / 2;
    LPCANIMATION animation = G_GetAnimation(ent->model, anim);
    if (animation) {
        ent->frame = animation->interval[0];
    }
}

void build_build(LPEDICT ent) {
    if (player_pay(G_GetPlayerByNumber(ent->s.player), ent->build_project)) {
        LPEDICT building = SP_SpawnAtLocation(ent->build_project, ent->s.player, &ent->s.origin2);
        repair_build(ent, building);
//        VECTOR2 origin;
//        FLOAT angle;
//        M_SetMove(ent, &build_move_build);
//        SP_FindEmptySpaceAround(building, ent->class_id, &origin, &angle);
//        ent->s.origin2 = origin;
//        ent->s.angle = angle - M_PI;
//        ent->selected = 0;
//        ent->build = building;
        building->health.value = 0;
        building->build = building;
        G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_START);
        /* Bake the new building's footprint into the static pathmap so it
         * becomes a hard obstacle for move-time collision and the flow field
         * reroutes around it.  Units standing on the footprint are not pushed
         * (WC3 does not push them either); they path out on their next order. */
        CM_BakeStaticObstacles();
        Get_Portrait_f(G_GetPlayerEntityByNumber(ent->s.player));
    } else {
        ent->stand(ent);
    }
}

BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location) {
    LPEDICT waypoint = Waypoint_add(location);
    FOR_SELECTED_UNITS(clent->client, ent) {
        ent->goalentity = waypoint;
        ent->build_project = clent->build_project;
        unit_setmove(ent, &build_move_walk);
    }
    entityState_t empty;
    memset(&empty, 0, sizeof(entityState_t));
    gi.Write(PF_BYTE, &(LONG){svc_cursor});
    gi.Write(PF_ENTITY, &empty);
    gi.unicast(clent);
    return true;
}

void build_menu_selectlocation(LPEDICT ent, DWORD building_id) {
    entityState_t cursor;
    FillUnitData(&cursor, building_id, "stand");
    UI_AddCancelButton(ent);
    gi.Write(PF_BYTE, &(LONG){svc_cursor});
    gi.Write(PF_ENTITY, &cursor);
    gi.unicast(ent);
    ent->client->menu.on_location_selected = build_menu_send_builder;
    ent->build_project = building_id;
}

void build_command(LPEDICT edict) {
    edict->client->menu.cmdbutton = build_menu_selectlocation;
}

void build_start(LPEDICT self, LPEDICT target) {
//    self->goalentity = target;
    unit_setmove(self, &build_move_stand);
}

ability_t a_build = {
    .cmd = build_command,
};
