/* Awan: autonomous neutral wandering. The unit owns only scheduling and the
 * identity of its internal goal; the standard Move ability owns pathfinding.
 * Timing and distance below are provisional until measured in classic retail. */
#include "s_skills.h"
#include <math.h>

#define WANDER_MIN_DELAY_MS 8000u
#define WANDER_DELAY_SPREAD_MS 2001u
#define WANDER_MIN_DISTANCE 64.0f
#define WANDER_DISTANCE_SPREAD 192.0f
#define WANDER_ATTEMPTS 6u /* destination rolls per decision; a dense camp then waits for the next deadline */
#define WANDER_TAU 6.2831853071795864769f
#define WANDER_BUILDING_CLEARANCE 32.0f /* world units; keeps a critter from parking against a mill wall */
#define WANDER_FLEE_DISTANCE 192.0f /* world units; full retreat from an attacker, halved per failed attempt */
#define WANDER_FLEE_ATTEMPTS 4u /* 192/96/48/24 units; a shorter hop is not a visible retreat */

static bool wander_present(edict_t const *unit) {
    return unit && S_ResolveAbilityAlias((edict_t *)unit, MAKEFOURCC('A', 'w', 'a', 'n')).alias != 0;
}

static uint32_t wander_random(edict_t *unit) {
    if (!unit->wander->random_state) {
        /* Stable world/unit identity, not the process RNG or renderer clock. */
        unit->wander->random_state = (unit->s.number + 1u) * 2654435761u ^
                                    (unit->spawn_time + 1u);
        if (!unit->wander->random_state) unit->wander->random_state = 1u;
    }
    unit->wander->random_state = unit->wander->random_state * 1664525u + 1013904223u;
    return unit->wander->random_state;
}

/* Uniform [0, 1) from the unit's own PRNG. */
static float wander_random_unit(edict_t *unit) {
    return wander_random(unit) / 4294967296.0f;
}

static vec2_t wander_random_direction(edict_t *unit) {
    float const angle = wander_random_unit(unit) * WANDER_TAU;
    return MAKE(vec2_t, cosf(angle), sinf(angle));
}

static void wander_schedule(edict_t *unit) {
    unit->wander->next_time = G_Time() + WANDER_MIN_DELAY_MS +
                             wander_random(unit) % WANDER_DELAY_SPREAD_MS;
}

static bool wander_owns_move(edict_t const *unit);

static bool wander_eligible(edict_t *unit) {
    return wander_present(unit) && !M_IsDead(unit) && !G_UnitHasFlag(unit, UNIT_FLAG_PAUSED) &&
           !unit_hasstatusstate(unit, WC3_STATUS_STATE_STUNNED) && !G_UnitHasFlag(unit, UNIT_FLAG_TRAINING) && !G_GetConstruction(unit) &&
           !(unit->aiflags & AI_IMMOBILE) && !unit->orders->holding_position &&
           (!G_UnitHasActiveOrder(unit) || wander_owns_move(unit)) &&
           !S_UnitIsCycloned(unit) && !S_UnitIsEntanglingRooted(unit) &&
           !S_UnitIsEnsnared(unit) && !S_PurgeIsImmobilized(unit) &&
           !G_UnitQueuedOrderCount(unit);
}

/* Keep autonomous destinations visibly clear of live buildings after contact.
 * A destroyed structure keeps its edict through the death animation but no
 * longer blocks pathing, so it does not constrain the destination. */
static bool wander_clear_of_buildings(edict_t *unit, vec2_t const *point) {
    FOR_LOOP(i, MAX_ENTITIES) {
        edict_t *building = &g_edicts[i];
        if (building == unit || !building->inuse || !(building->s.flags & EF_BUILDING) ||
            M_IsDead(building)) continue;
        if (Vector2_distance(point, &building->s.origin2) <
            unit->collision + building->collision + WANDER_BUILDING_CLEARANCE) return false;
    }
    return true;
}

static bool wander_choose_destination(edict_t *unit, vec2_t *result) {
    FOR_LOOP(i, WANDER_ATTEMPTS) {
        vec2_t const direction = wander_random_direction(unit);
        float const radius = WANDER_MIN_DISTANCE + wander_random_unit(unit) * WANDER_DISTANCE_SPREAD;
        vec2_t const point = Vector2_mad(&unit->s.origin2, radius, &direction);
        /* M_MoveIsValid allows stopping just outside collision. A clearance
         * margin prevents an autonomous route from ending against a mill. */
        if (!M_MoveIsValid(unit, &point) || !wander_clear_of_buildings(unit, &point)) continue;
        *result = point;
        return true;
    }
    return false;
}

/* A freed private destination can be reused by an unrelated unit or route owner. */
static bool wander_waypoint_valid(edict_t const *unit) {
    edict_t const *goal = unit && unit->wander ? unit->wander->waypoint : NULL;
    return goal && goal->inuse && goal->spawn_time == unit->wander->waypoint_spawn_time && goal->route;
}

/* Pointer equality alone is unsafe: waypoints are reused in a fixed ring. */
static bool wander_owns_move(edict_t const *unit) {
    return wander_waypoint_valid(unit) && unit->wander->goal == unit->wander->waypoint &&
           unit->goalentity == unit->wander->goal &&
           unit->wander->goal->inuse &&
           unit->wander->goal_generation == unit->wander->goal->route->serial &&
           move_is_active_order_walk(unit);
}

static void wander_clear(edict_t *unit) {
    if (!unit->wander) return;
    unit->wander->goal = NULL;
    unit->wander->goal_generation = 0;
}

/* Each wandering unit keeps its own destination edict. A shared ring waypoint
 * may be overwritten while still referenced by an active Move, regardless of
 * its generation; the stable private goal avoids mutating an in-flight route. */
static void wander_release_waypoint(edict_t *unit) {
    edict_t *goal = unit->wander->waypoint;
    if (!goal) return;
    /* Never free an edict that remains reachable through the unit's current
     * movement target, including after unit_stand() changes the move proc. */
    if (unit->goalentity == goal) unit->goalentity = NULL;
    unit->wander->waypoint = NULL;
    if (unit->wander->goal == goal) wander_clear(unit);
    bool const owned = goal->inuse && goal->spawn_time == unit->wander->waypoint_spawn_time;
    unit->wander->waypoint_spawn_time = 0;
    if (owned) G_FreeEdict(goal);
}

static void wander_start(edict_t *unit, vec2_t const *point) {
    edict_t *goal = wander_waypoint_valid(unit) ? unit->wander->waypoint : NULL;
    if (!goal) {
        goal = G_Spawn();
        if (!goal) return;
        goal->svflags |= SVF_NOCLIENT;
        goal->spawn_time = G_Time();
        unit->wander->waypoint = goal;
        unit->wander->waypoint_spawn_time = goal->spawn_time;
    }
    /* Only rewrite a private waypoint when no Move currently uses it. */
    if (unit->goalentity == goal && move_is_active_order_walk(unit)) return;
    if (!goal->route) goal->route = G_AllocRoute();
    goal->route->serial++;
    if (!goal->route->serial) goal->route->serial = 1;
    goal->s.origin.x = point->x;
    goal->s.origin.y = point->y;
    goal->route->field = 0;
    goal->route->radius = 0;
    goal->route->shared_goal = NULL;
    goal->collision = 0;
    M_CheckGround(goal);
    order_move(unit, goal);
    if (unit->goalentity == goal && move_is_active_order_walk(unit)) {
        unit->wander->goal = goal;
        unit->wander->goal_generation = goal->route->serial;
    }
}

/* Invoked by Move before its terminal Hold transition. A_MOVE_LEAVE can
 * already have retired wander->goal, so the stable private target is the
 * authoritative identity here. Never intercept ordinary player Move targets. */
static bool wander_recover_blocked_move(edict_t *unit) {
    if (!wander_waypoint_valid(unit) ||
        unit->goalentity != unit->wander->waypoint ||
        !unit->wander->waypoint->inuse ||
        !move_is_active_order_walk(unit)) return false;
    unit->goalentity = NULL;
    wander_clear(unit);
    unit->movement->last_distance = 0;
    unit->movement->blocked_frames = 0;
    /* The old deadline may have elapsed during the failed Move. Begin a
     * fresh pause now, not another attempt on the very next idle tick. */
    wander_schedule(unit);
    unit_stand(unit);
    return true;
}

static void wander_on_damage(edict_t *unit, edict_t *attacker);

BZ_ABILITY_PROC(CAbilityWander) {
    if ((msg == A_IDLE || msg == A_DAMAGED || msg == A_UNIT_INIT || msg == A_ENABLE || msg == A_UNIT_STAND) && wander_present(ent)) {
        if (!ent->wander) ent->wander = G_AllocWander();
    }
    if (!ent || !ent->wander) return false;
    switch (msg) {
    case A_DAMAGED:
        wander_on_damage(ent, call ? call->entity : NULL);
        return false;
    case A_IDLE:
        if (!wander_present(ent)) return false;
        /* Suppress normal auto-acquisition even while waiting. */
        if (!wander_eligible(ent)) return true;
        if (!ent->wander->next_time) { wander_schedule(ent); return true; }
        if ((int32_t)(G_Time() - ent->wander->next_time) < 0) return true;
        {
            vec2_t point;
            /* The next interval begins after arrival or failure, not now.
             * Retain a retry deadline if a destination cannot be started. */
            wander_schedule(ent);
            if (wander_choose_destination(ent, &point)) wander_start(ent, &point);
        }
        return true;
    case A_NO_RETALIATE:
        return wander_present(ent);
    case A_ORDER_ACCEPTED:
        /* An accepted order may preserve the current move object. */
        if (ent) wander_clear(ent);
        return false;
    case A_MOVE_ARRIVE:
        if (ent && ent->wander->waypoint &&
            ent->goalentity == ent->wander->waypoint &&
            move_is_active_order_walk(ent)) {
            wander_clear(ent);
            wander_schedule(ent);
        }
        return false;
    case A_MOVE_BLOCKED:
        return wander_recover_blocked_move(ent);
    case A_MOVE_LEAVE:
        if (ent) wander_clear(ent);
        return false;
    case A_MOVE_START:
        if (ent && (!call || call->entity != ent->wander->waypoint))
            wander_clear(ent);
        return false;
    case A_DISABLE:
    case A_DEATH:
    case A_UNIT_REMOVE:
        if (ent) {
            /* The private goal is never exposed to external orders. Even if
             * ownership was cleared on a preceding transition, any live Move
             * still targeting that private edict must be stopped before free. */
            bool const using_private_goal = ent->wander->waypoint &&
                ent->goalentity == ent->wander->waypoint;
            if (using_private_goal && move_is_active_order_walk(ent))
                unit_stand_no_queue(ent);
            wander_clear(ent);
            ent->wander->next_time = 0;
            wander_release_waypoint(ent);
            G_FreeWander(ent);
        }
        return false;
    default:
        return false;
    }
}

/* A damage response belongs to Awan rather than shared combat AI. Preserve
 * every active explicit order (attack, build, cast, patrol, harvest, move),
 * not just an ordinary walking order. Only Awan-owned movement may yield. */
static void wander_on_damage(edict_t *unit, edict_t *attacker) {
    vec2_t direction;
    if (!attacker || !attacker->inuse || attacker == unit || !wander_eligible(unit)) return;
    if (G_UnitHasActiveOrder(unit) && !wander_owns_move(unit)) return;
    /* A new hit can interrupt an existing autonomous Move. Retire it before
     * rewriting the stable private destination; never mutate a live route. */
    if (wander_owns_move(unit)) {
        unit_stand_no_queue(unit);
        if (unit->goalentity == unit->wander->waypoint) unit->goalentity = NULL;
        wander_clear(unit);
    }
    /* Away from the attacker; a coincident attacker gives no direction. */
    direction = Vector2_sub(&unit->s.origin2, &attacker->s.origin2);
    if (Vector2_len(&direction) < 1.0f) direction = wander_random_direction(unit);
    else Vector2_normalize(&direction);
    /* Try progressively shorter retreats if a full escape is obstructed. */
    FOR_LOOP(i, WANDER_FLEE_ATTEMPTS) {
        vec2_t const destination = Vector2_mad(&unit->s.origin2, WANDER_FLEE_DISTANCE / (1u << i), &direction);
        if (!M_MoveIsValid(unit, &destination) || !wander_clear_of_buildings(unit, &destination)) continue;
        wander_start(unit, &destination);
        wander_schedule(unit);
        return;
    }
}
