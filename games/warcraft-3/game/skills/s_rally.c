#include "s_skills.h"

BOOL G_UnitHasRally(LPCEDICT producer) {
    LPCSTR trains;

    if (!producer || !producer->UnitProfile) return false;
    trains = producer->UnitProfile->trains;
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
    return true;
}

BOOL G_SetRallyEntity(LPEDICT producer, LPEDICT target) {
    if (!G_UnitHasRally(producer) || !target || !target->inuse) return false;
    if (target == producer) {
        G_ResetRallyTarget(producer);
        return true;
    }
    producer->rally.type = RALLY_TARGET_ENTITY;
    producer->rally.entity = target;
    producer->rally.entity_spawn_time = target->spawn_time;
    producer->rally.point = (VECTOR2){ 0, 0 };
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
    }
}

static BOOL rally_selecttarget(LPEDICT clent, LPEDICT target) {
    BOOL any = false;

    if (!clent || !clent->client || !target) return false;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, producer) {
        if (G_SetRallyEntity(producer, target)) any = true;
    }
    return any;
}

static BOOL rally_selectlocation(LPEDICT clent, LPCVECTOR2 point) {
    BOOL any = false;

    if (!clent || !clent->client || !point) return false;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, producer) {
        if (G_SetRallyPoint(producer, point)) any = true;
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
