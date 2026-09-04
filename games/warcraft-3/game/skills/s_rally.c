#include "s_skills.h"

static void G_ClearRallyIndicator(LPGAMECLIENT client) {
    if (!client || !client->rally_indicator) return;
    G_FreeEdict(client->rally_indicator);
    client->rally_indicator = NULL;
}

static void G_RefreshRallyIndicatorForProducer(LPEDICT producer) {
    if (!producer) return;
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        if (!client->connected || G_GetMainSelectedUnit(client) != producer) continue;
        G_UpdateRallyIndicator(client);
    }
}

BOOL G_UnitHasRally(LPCEDICT producer) {
    LPCSTR trains;

    if (!producer || !producer->data.UnitProfile) return false;
    trains = producer->data.UnitProfile->trains;
    return (trains && *trains) || G_UnitCanReviveHeroes(producer);
}

void G_ResetRallyTarget(LPEDICT producer) {
    if (!producer) return;
    memset(&producer->rally, 0, sizeof(producer->rally));
}

BOOL G_SetRallyPoint(LPEDICT producer, LPCVECTOR2 point) {
    if (!G_UnitHasRally(producer) || !point) return false;
    producer->rally.type = RALLY_TARGET_POINT;
    producer->rally.point = *point;
    producer->rally.entity = NULL;
    producer->rally.entity_spawn_time = 0;
    G_RefreshRallyIndicatorForProducer(producer);
    return true;
}

BOOL G_SetRallyEntity(LPEDICT producer, LPEDICT target) {
    if (!G_UnitHasRally(producer) || !target || !target->inuse) return false;
    if (target == producer) {
        G_ResetRallyTarget(producer);
        G_RefreshRallyIndicatorForProducer(producer);
        return true;
    }
    producer->rally.type = RALLY_TARGET_ENTITY;
    producer->rally.entity = target;
    producer->rally.entity_spawn_time = target->spawn_time;
    producer->rally.point = (VECTOR2){ 0, 0 };
    G_RefreshRallyIndicatorForProducer(producer);
    return true;
}

static BOOL G_RallyEntityIsValid(LPEDICT producer) {
    LPEDICT target;

    if (!producer || producer->rally.type != RALLY_TARGET_ENTITY) return false;
    target = producer->rally.entity;
    if (!target || !target->inuse || target->spawn_time != producer->rally.entity_spawn_time) {
        return false;
    }
    /* Current Warsmash explicitly drops dead unit targets. Destructable/item
     * death/removal semantics are less certain, so only unit death is folded
     * into the default here; freed/reused edicts are rejected for every type. */
    /* Death is published before health reaches zero, so the server death bit
     * must invalidate rally targets immediately. */
    if ((target->svflags & SVF_MONSTER) && ((target->svflags & SVF_DEADMONSTER) || M_IsDead(target))) return false;
    return true;
}

rallyTargetType_t G_ResolveRallyTarget(LPEDICT producer, LPVECTOR2 point, LPEDICT *target) {
    if (point) *point = (VECTOR2){ 0, 0 };
    if (target) *target = NULL;
    if (!G_UnitHasRally(producer)) return RALLY_TARGET_NONE;

    if (producer->rally.type == RALLY_TARGET_POINT) {
        if (point) *point = producer->rally.point;
        return RALLY_TARGET_POINT;
    }
    if (producer->rally.type == RALLY_TARGET_ENTITY) {
        if (!G_RallyEntityIsValid(producer)) {
            G_ResetRallyTarget(producer);
        } else {
            if (point) *point = producer->rally.entity->s.origin2;
            if (target) *target = producer->rally.entity;
            return RALLY_TARGET_ENTITY;
        }
    }

    if (point) *point = producer->s.origin2;
    if (target) *target = producer;
    return RALLY_TARGET_SELF;
}

void G_UpdateRallyIndicator(LPGAMECLIENT client) {
    static LPCSTR const default_model = "UI\\Feedback\\RallyPoint\\RallyPoint.mdx";
    LPEDICT clent;
    LPEDICT producer;
    LPEDICT target = NULL;
    LPCSTR model_path;
    VECTOR2 point;
    VECTOR3 origin = { 0 };
    rallyTargetType_t type;
    DWORD model;
    FLOAT angle;
    LPEDICT indicator;
    LPCANIMATION animation;

    if (!client || !(clent = G_GetPlayerEntityByNumber(client->ps.number)) || !clent->client) return;
    producer = G_GetMainSelectedUnit(client);
    if (!producer || !G_UnitHasRally(producer)) {
        G_ClearRallyIndicator(client);
        return;
    }

    model_path = Theme_PlayerString(client, "RallyIndicatorDst", default_model);
    model = model_path && model_path[0] ? (DWORD)G_RegisterModel(model_path) : 0;
    if (!model) {
        G_ClearRallyIndicator(client);
        return;
    }

    type = G_ResolveRallyTarget(producer, &point, &target);
    angle = game.constants.buildingAngle * (FLOAT)M_PI / 180.0f;
    if (type == RALLY_TARGET_POINT) {
        origin = (VECTOR3){ point.x, point.y, 0 };
    } else if ((type == RALLY_TARGET_SELF || type == RALLY_TARGET_ENTITY) && target) {
        origin = target->s.origin;
        if (target->destructable.initialized) {
            origin.z += 192.0f;
        }
    } else {
        G_ClearRallyIndicator(client);
        return;
    }
    indicator = client->rally_indicator;
    if (!indicator) indicator = client->rally_indicator = G_Spawn();
    if (!indicator) return;
    indicator->s.origin = origin;
    indicator->s.angle = angle;
    indicator->s.scale = 1.0f;
    indicator->s.player = client->ps.number;
    indicator->s.model = model;
    animation = G_GetAnimation(model, "stand");
    indicator->s.frame = animation ? animation->interval[0] : 0;
    indicator->s.renderfx = RF_NO_FOGOFWAR;
    indicator->s.flags = EF_NOT_SELECTABLE;
    indicator->svflags = SVF_OWNER_ONLY;
    indicator->rally_indicator = true;
    indicator->owner = clent;
    indicator->goalentity = type == RALLY_TARGET_POINT ? NULL : target;
    indicator->movetype = indicator->goalentity ? MOVETYPE_LINK : MOVETYPE_NONE;
    if (type == RALLY_TARGET_POINT || target->targtype == TARG_ITEM) {
        M_CheckGround(indicator);
        indicator->s.flags |= EF_GROUND_ANCHOR;
    }
    gi.LinkEntity(indicator);
}

BOOL G_ApplyRallyOrder(LPEDICT producer, LPEDICT produced) {
    VECTOR2 point;
    LPEDICT target;
    rallyTargetType_t type;

    if (!producer || !produced || !produced->inuse) return false;
    type = G_ResolveRallyTarget(producer, &point, &target);
    if (type == RALLY_TARGET_POINT) return unit_issueorder(produced, "smart", &point);
    if (type == RALLY_TARGET_SELF || type == RALLY_TARGET_ENTITY)
        return unit_issuetargetorder(produced, "smart", target);
    return false;
}

void G_InvalidateRallyTarget(LPEDICT target) {
    if (!target) return;
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT producer = &globals.edicts[i];
        if (!producer->inuse || producer->rally.type != RALLY_TARGET_ENTITY ||
            producer->rally.entity != target ||
            producer->rally.entity_spawn_time != target->spawn_time) {
            continue;
        }
        G_ResetRallyTarget(producer);
        G_RefreshRallyIndicatorForProducer(producer);
    }
}

static BOOL rally_selecttarget(LPEDICT clent, LPEDICT target) {
    BOOL any = false;

    if (!clent || !clent->client || !target) return false;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, producer) {
        if (G_SetRallyEntity(producer, target)) any = true;
    }
    if (any) G_PlayUISoundForPlayer(clent, "RallyPointPlace");
    return any;
}

static BOOL rally_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    BOOL any = false;

    if (!clent || !clent->client || !point) return false;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, producer) {
        if (G_SetRallyPoint(producer, point)) any = true;
    }
    if (any) {
        G_SendPointConfirmation(clent, point, false);
        G_PlayUISoundForPlayer(clent, "RallyPointPlace");
    }
    return any;
}

static void rally_command(LPEDICT clent) {
    LPEDICT producer;

    if (!clent || !clent->client) return;
    producer = G_GetMainSelectedUnit(clent->client);
    if (!G_UnitCanControl(clent->client, producer) || !G_UnitHasRally(producer)) return;
    UI_AddCancelButton(clent);
    clent->client->menu.on_entity_selected = rally_selecttarget;
    clent->client->menu.on_location_selected = rally_selectlocation;
}

ability_t a_rally = {
    .cmd = rally_command,
};
