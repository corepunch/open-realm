#include "s_skills.h"

#define GUARD_RETURN_EPSILON 1.0f

void G_SetUnitGuardPosition(edict_t *ent) {
    if (!ent) return;
    ent->movement.guard_position = ent->s.origin2;
    ent->movement.guard_position_valid = true;
    ent->movement.guard_combat = false;
    ent->movement.guard_returning = false;
}

void G_ClearUnitGuardPosition(edict_t *ent) {
    if (!ent) return;
    ent->movement.guard_position_valid = false;
    ent->movement.guard_combat = false;
    ent->movement.guard_returning = false;
}

static bool start_guard_return(edict_t *ent) {
    edict_t *waypoint;

    if (!ent || !ent->movement.guard_position_valid || ent->movement.holding_position ||
        (ent->aiflags & AI_IMMOBILE) || G_UnitQueuedOrderCount(ent)) {
        return false;
    }
    ent->movement.guard_combat = false;
    if (Vector2_distance(&ent->s.origin2, &ent->movement.guard_position) <= GUARD_RETURN_EPSILON) {
        ent->movement.guard_returning = false;
        return false;
    }
    waypoint = Waypoint_add(&ent->movement.guard_position);
    if (!waypoint) return false;
    order_move(ent, waypoint);
    ent->movement.guard_returning = true;
    return true;
}

// Disabled until stop owns a custom stand move; Linux -Wall warns on unused static hooks.
// static umove_t stop_stand = { "stand", ai_stand, NULL, CAbilityStop};

static void order_stop_state(edict_t *ent, bool preserve_queue, bool record_guard) {
    if (S_GoldMineWorkerIsInside(ent))
        return;
    /* Channeling can retain the idle move, so Stop must cancel even without a move-leave notification. */
    S_SpellCancelChannel(ent);
    ent->movement.attackmove_waypoint = NULL;
    ent->movement.patrol_a = NULL;
    ent->movement.patrol_b = NULL;
    ent->movement.patrol_target = NULL;
    ent->movement.follow_target = NULL;
    ent->movement.holding_position = false;
    if (record_guard) G_SetUnitGuardPosition(ent);
    unit_leavecombat(ent);
    if (preserve_queue) unit_stand_no_queue(ent);
    else ent->stand(ent);
}

void order_stop(edict_t *ent) {
    G_ClearUnitOrderQueue(ent);
    order_stop_state(ent, false, true);
}

void order_stop_cleanup(edict_t *ent) {
    G_ClearUnitOrderQueue(ent);
    order_stop_state(ent, false, false);
}

void order_stop_queued(edict_t *ent) {
    order_stop_state(ent, true, true);
}

static void AbilityStop_Command(edict_t *clent);

BZ_ABILITY_PROC(CAbilityStop) {
    if (msg == A_COMMAND) {
        AbilityStop_Command(call && call->client ? call->client : ent);
        return true;
    }
    if (msg == A_AUTO_COMBAT_START) {
        if (ent) {
            ent->movement.guard_combat = ent->movement.guard_position_valid && ent->currentmove &&
                ent->currentmove->think == ai_stand;
        }
    } else if (msg == A_AUTO_COMBAT_END) {
        if (!ent || !ent->movement.guard_combat) return false;
        if (start_guard_return(ent)) return true;
        ent->movement.guard_combat = false;
    } else if (msg == A_ORDER_ACCEPTED && ent && call && call->order && strcmp(call->order, "stop")) {
        G_ClearUnitGuardPosition(ent);
    }
    return false;
}

static void AbilityStop_Command(edict_t *clent) {
    gameClient_t *client = clent->client;
    FOR_CONTROLLABLE_SELECTED_UNITS(client, e) {
        if (client->menu.order_queued && G_UnitHasActiveOrder(e)) {
            if (G_QueueUnitOrder(e, "stop", UNIT_ORDER_TARGET_NONE, NULL, NULL,
                                 client->ps.number, 0.0f, 0)) {
                G_PublishIssuedImmediateOrder(e, G_OrderId("stop"), client->ps.number, "stop");
            }
        } else {
            unit_issueimmediateorder(e, "stop");
        }
    }
}
