#include "s_skills.h"

#define BZ_AWRP MAKEFOURCC('A','w','r','p')
#define BZ_AMOV MAKEFOURCC('A','m','o','v')

/* Way Gate is authored as a passive ability. Resolve aliases rather than
 * hard-coding Awrp so map object-data copies retain their DataA/DataB entry
 * rectangle and normal JASS Waygate* behavior. */
static uint32_t waygate_actor_ability_alias(edict_t const *gate) {
    char alias_name[5] = {0};

    if (!gate) return 0;
    if (gate->data.UnitAbilities && gate->data.UnitAbilities->abilList) {
        PARSE_LIST(gate->data.UnitAbilities->abilList, token, parse_segment) {
            uint32_t alias = 0;
            if (strlen(token) != 4 || !G_ActorHasSkill(gate, token)) continue;
            memcpy(&alias, token, 4);
            if (G_AbilityCode(alias) == BZ_AWRP) return alias;
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(gate->abilities.added)) {
        uint32_t const alias = gate->abilities.added[i];
        if (!alias) continue;
        memcpy(alias_name, &alias, 4);
        if (G_ActorHasSkill(gate, alias_name) && G_AbilityCode(alias) == BZ_AWRP)
            return alias;
    }
    return 0;
}

static bool waygate_dimensions(edict_t const *gate, float *width, float *height) {
    uint32_t const alias = waygate_actor_ability_alias(gate);
    abilityLevel_t const *row;

    if (!alias || !width || !height) return false;
    row = G_AbilityLevel(alias, MAX(1u, G_UnitAbilityLevel(gate, alias)));
    if (!row) return false;
    *width = MAX(0.0f, row->data[0].number);  /* Wrp1 / DataA */
    *height = MAX(0.0f, row->data[1].number); /* Wrp2 / DataB */
    return *width > 0.0f && *height > 0.0f;
}

bool S_WaygateIsGate(edict_t const *gate) {
    return gate && gate->inuse && waygate_actor_ability_alias(gate) != 0;
}

bool S_WaygateIsActive(edict_t const *gate) {
    return S_WaygateIsGate(gate) && gate->waygate.active;
}

bool S_WaygateGetDestination(edict_t const *gate, vec2_t *destination) {
    if (!S_WaygateIsGate(gate) || !destination) return false;
    *destination = gate->waygate.destination;
    return gate->waygate.destination_set;
}

void S_WaygateSetDestination(edict_t *gate, vec2_t const *destination) {
    if (!S_WaygateIsGate(gate) || !destination) return;
    gate->waygate.destination = *destination;
    gate->waygate.destination_set = true;
}

void S_WaygateSetActive(edict_t *gate, bool active) {
    if (!S_WaygateIsGate(gate)) return;
    gate->waygate.active = active != false;
    G_AddUnitAnimationProperties(gate, "alternate", gate->waygate.active);
}

static bool waygate_point_inside(edict_t const *gate, vec2_t const *point) {
    float width, height;

    if (!gate || !point || !waygate_dimensions(gate, &width, &height)) return false;
    return fabsf(point->x - gate->s.origin2.x) <= width * 0.5f &&
           fabsf(point->y - gate->s.origin2.y) <= height * 0.5f;
}

static bool waygate_target_inside(edict_t const *gate, edict_t const *unit) {
    return unit && waygate_point_inside(gate, &unit->s.origin2);
}

static bool waygate_target_valid(edict_t const *unit, edict_t const *gate, uint32_t spawn_time) {
    if (!unit || !gate || unit == gate || !gate->inuse || gate->spawn_time != spawn_time) return false;
    if (M_IsDead(unit) || M_IsDead(gate) || !S_UnitCanTranslate(unit)) return false;
    return S_WaygateIsActive(gate) && gate->waygate.destination_set;
}

static bool waygate_behavior_active(edict_t const *unit) {
    return unit && (unit->movement.waygate_target || unit->movement.waygate_goal ||
                    unit->movement.waygate_target_spawn_time);
}

static void waygate_cancel(edict_t *unit);

/* CAbilityWarp owns only its pointers. In particular, secondarygoal is shared
 * by unrelated movement behaviors and must never be cleared by Way Gate exit. */
static void waygate_clear_order(edict_t *unit) {
    if (!unit) return;
    if (unit->goalentity == unit->movement.waygate_goal)
        unit->goalentity = NULL;
    unit->movement.waygate_target = NULL;
    unit->movement.waygate_goal = NULL;
    unit->movement.waygate_target_spawn_time = 0;
    move_reset_progress(unit);
}

static bool waygate_complete(edict_t *unit, edict_t *gate) {
    vec2_t source, position;

    if (!unit || !gate) return false;
    source = unit->s.origin2;
    if (!G_FindUnitUnstuckPosition(unit, &gate->waygate.destination, &position)) {
        fprintf(stderr, "WC3 Waygate: no legal destination for unit %u gate %u at (%.1f, %.1f); traversal cancelled\n",
                unit->s.number, gate->s.number, gate->waygate.destination.x, gate->waygate.destination.y);
        waygate_cancel(unit);
        return false;
    }
    G_SpawnAbilityEffectAtPoint(BZ_AMOV, WC3_EFFECT_SPECIAL, 0, &source, true);
    unit->s.origin2 = position;
    unit->s.origin.x = position.x;
    unit->s.origin.y = position.y;
    if (unit->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    gi.LinkEntity(unit);
    G_SpawnAbilityEffectAtPoint(BZ_AMOV, WC3_EFFECT_SPECIAL, 0, &unit->s.origin2, true);
    waygate_clear_order(unit);
    unit_stand(unit);
    return true;
}

static bool waygate_find_entry_point(edict_t *unit, edict_t *gate, vec2_t *out) {
    float width, height;
    box2_t entry;

    if (!unit || !gate || !out || !waygate_dimensions(gate, &width, &height)) return false;
    entry.min = (vec2_t){ gate->s.origin2.x - width * 0.5f, gate->s.origin2.y - height * 0.5f };
    entry.max = (vec2_t){ gate->s.origin2.x + width * 0.5f, gate->s.origin2.y + height * 0.5f };
    return G_ClosestStaticPathablePointInRectForRadiusFlags(&unit->s.origin2, &entry,
        unit->collision, M_UnitStaticPathingFlags(unit), out);
}

static edict_t *waygate_create_approach_goal(edict_t *unit, edict_t *gate) {
    vec2_t approach;

    if (waygate_find_entry_point(unit, gate, &approach))
        return Waypoint_add(&approach);
    if (!(gate->s.flags & EF_BUILDING) || !gate->pathtex)
        return gate; /* Models without a blocked authored footprint can be followed directly. */
    return NULL;
}

static void waygate_cancel(edict_t *unit) {
    waygate_clear_order(unit);
    unit_stand(unit);
}

static void ai_waygate_walk(edict_t *unit) {
    edict_t *gate = unit ? unit->movement.waygate_target : NULL;
    uint32_t const spawn_time = unit ? unit->movement.waygate_target_spawn_time : 0;
    float distance, step;

    if (!waygate_target_valid(unit, gate, spawn_time)) {
        waygate_cancel(unit);
        return;
    }
    if (waygate_target_inside(gate, unit)) {
        waygate_complete(unit, gate);
        return;
    }
    if (!unit->movement.waygate_goal || !unit->movement.waygate_goal->inuse) {
        edict_t *goal = waygate_create_approach_goal(unit, gate);
        if (!goal) {
            waygate_cancel(unit);
            return;
        }
        unit->movement.waygate_goal = goal;
        unit->goalentity = goal;
    }
    distance = M_DistanceToGoal(unit);
    step = unit_movedistance(unit);
    if (move_is_blocked(unit, distance, step) || unit->movement.flow_unreachable) {
        waygate_cancel(unit);
        return;
    }
    unit_changeangle_for_radius(unit, unit->collision);
    if (unit->movement.flow_goal_reached && !waygate_target_inside(gate, unit)) {
        waygate_cancel(unit);
        return;
    }
    unit_moveindirection(unit);
}

static umove_t waygate_move_walk = { "walk", ai_waygate_walk, NULL, CAbilityWarp };

static bool waygate_order_use(edict_t *unit, edict_t *gate) {
    edict_t *goal = NULL;
    uint32_t const spawn_time = gate ? gate->spawn_time : 0;

    if (!waygate_target_valid(unit, gate, spawn_time)) return false;
    if (!waygate_target_inside(gate, unit)) {
        goal = waygate_create_approach_goal(unit, gate);
        if (!goal) return false; /* A rejected Smart order must not disturb the current behavior. */
    }

    unit->movement.follow_target = NULL;
    unit->movement.attackmove_waypoint = NULL;
    unit->movement.patrol_a = NULL;
    unit->movement.patrol_b = NULL;
    unit->movement.patrol_target = NULL;
    unit->movement.holding_position = false;
    waygate_clear_order(unit);

    if (!goal) {
        unit->goalentity = NULL;
        unit->movement.waygate_target = gate;
        unit->movement.waygate_target_spawn_time = spawn_time;
        waygate_complete(unit, gate);
        return true;
    }

    /* Install the movement first: unit_setmove publishes A_MOVE_LEAVE for the
     * old behavior, which must not be allowed to clear the new gate state. */
    unit_setmove(unit, &waygate_move_walk);
    unit->movement.waygate_target = gate;
    unit->movement.waygate_target_spawn_time = spawn_time;
    unit->movement.waygate_goal = goal;
    unit->goalentity = goal;
    move_reset_progress(unit);
    return true;
}

BZ_ABILITY_PROC(CAbilityWarp) {
    switch (msg) {
        case A_TARGET_ORDER:
            return call && call->target_order.issuer && call->target_order.order &&
                   !strcmp(call->target_order.order, "smart") &&
                   waygate_order_use(call->target_order.issuer, ent);
        case A_MOVE_LEAVE:
            if (!waygate_behavior_active(ent)) return false;
            waygate_clear_order(ent);
            return true;
        case A_ORDER_ACCEPTED: {
            bool const owns_move = ent && ent->currentmove && ent->currentmove->proc == CAbilityWarp;
            if (!waygate_behavior_active(ent)) return false;
            waygate_clear_order(ent);
            /* The accepted order may already have installed its own cast/move.
             * Only replace the old Way Gate walk when it is still current. */
            if (owns_move) unit_stand(ent);
            return true;
        }
        case A_UNIT_REMOVE:
            if (!waygate_behavior_active(ent)) return false;
            waygate_clear_order(ent);
            return true;
        default:
            return false;
    }
}
