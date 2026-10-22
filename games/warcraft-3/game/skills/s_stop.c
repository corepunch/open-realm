#include "s_skills.h"

#define GUARD_RETURN_EPSILON 1.0f // world units; avoids starting a return when the unit is already at its Stop anchor

/* Record the point Stop must restore after its automatic-combat detour. */
void G_SetUnitGuardPosition(edict_t *ent) {
    if (!ent) return;
    ent->movement.guard_position = ent->s.origin2;
    ent->movement.guard_state = GUARD_IDLE;
}

/* Remove the Stop anchor and any in-progress guard-return phase. */
void G_ClearUnitGuardPosition(edict_t *ent) {
    if (!ent) return;
    ent->movement.guard_state = GUARD_NONE;
}

/* Resume ordinary Move toward the saved Stop point after idle combat ends. */
static bool start_guard_return(edict_t *ent) {
    edict_t *waypoint;

    if (!ent || ent->movement.guard_state == GUARD_NONE || ent->movement.holding_position ||
        (ent->aiflags & AI_IMMOBILE) || G_UnitQueuedOrderCount(ent)) {
        return false;
    }
    if (Vector2_distance(&ent->s.origin2, &ent->movement.guard_position) <= GUARD_RETURN_EPSILON) {
        ent->movement.guard_state = GUARD_IDLE;
        return false;
    }
    waypoint = Waypoint_add(&ent->movement.guard_position);
    if (!waypoint) return false;
    order_move(ent, waypoint);
    ent->movement.guard_state = GUARD_RETURNING;
    return true;
}

// Disabled until stop owns a custom stand move; Linux -Wall warns on unused static hooks.
// static umove_t stop_stand = { "stand", ai_stand, NULL, CAbilityStop};

/* A d0144 consumer cancels physical movement independently of the public
 * animation state. The old-task and new-task consumers are separate scopes. */
static void stop_cancel_movement(edict_t *ent) {
    /* A rooted structure has no Move subscriber; mobile Ancients do. */
    if (G_UnitIsStructure(ent)) S_StopUnitMovement(ent);
    else S_StopUnitMovementWithRecovery(ent);
    S_ClearUnitOrderReferences(ent);
}

void S_ClearUnitOrderReferences(edict_t *ent) {
    S_SetMoveGoal(ent, &ent->movement.attackmove_waypoint, NULL);
    S_SetMoveGoal(ent, &ent->movement.patrol_a, NULL);
    S_SetMoveGoal(ent, &ent->movement.patrol_b, NULL);
    S_SetMoveGoal(ent, &ent->movement.patrol_target, NULL);
    S_SetFollowTarget(ent, NULL);
    ent->movement.holding_position = false;
    unit_leavecombat(ent);
}

/* Finish only after the new Stop task has admitted its position. */
static void stop_finish(edict_t *ent, bool preserve_queue, bool record_guard) {
    if (record_guard) G_SetUnitGuardPosition(ent);
    ent->current_order_id = 0;
    if (preserve_queue) unit_stand_no_queue(ent);
    else if (ent->stand) ent->stand(ent);
}

/* Internal cleanup already owns its task; it does not issue a new Stop. */
static void order_stop_state(edict_t *ent, bool preserve_queue, bool record_guard) {
    if (S_GoldMineWorkerIsInside(ent)) return;
    S_SpellCancelChannel(ent);
    stop_cancel_movement(ent);
    stop_finish(ent, preserve_queue, record_guard);
}

void order_stop(edict_t *ent) {
    G_ClearUnitOrderQueue(ent);
    order_stop_state(ent, false, true);
}

/*673fe0 retires the old task before notification.690b80 constructs a separate
 * Stop cleanup after both synchronous event families. A callback can change
 * terrain between the two recovery scopes or replace the pending task. */
bool S_IssueStopOrder(edict_t *ent) {
    if(S_UnitOrderExecutionHeld(ent))
        return G_RetainSuspendedUnitOrder(ent,"stop",UNIT_ORDER_TARGET_NONE,NULL,NULL,false,ent->s.player,0);
    uint32_t const order_id = G_OrderId("stop"), spawn_time = ent->spawn_time;
    G_ClearUnitOrderQueue(ent);
    S_SpellCancelChannel(ent);
    stop_cancel_movement(ent);
    /* Recovery publishes region filters before the new Stop is admitted.
     * Death rejects that admission; deferred removal still owns the old
     * physical task until its cleanup completes. */
    if (!ent->inuse || ent->spawn_time != spawn_time || M_IsDead(ent)) return true;
    bool const removing = G_IsDeferredFree(ent);
    /* Retire the old logical owner before publishing Stop. Its inverse may
     * synchronously issue orders; the pending outer Stop still owns admission.
     * Stand has no procedure: report the actual next move, not a command owner. */
    S_UnitAbilityMoveLeave(ent, NULL);
    if (!ent->inuse || ent->spawn_time != spawn_time ||
        (G_IsDeferredFree(ent) && !removing) || M_IsDead(ent)) return true;
    uint32_t const revision = G_UnitMoveRevision(ent);
    ent->current_order_id = order_id;
    S_UnitAbilityOrderAccepted(ent, "stop");
    G_PublishIssuedImmediateOrder(ent, order_id, ent->s.player, "stop");
    if (!ent->inuse || ent->spawn_time != spawn_time ||
        (G_IsDeferredFree(ent) && !removing) || M_IsDead(ent)) return true;
    if (S_UnitExecutionPaused(ent)) {
        /* A disabled Move subscriber cannot construct Stop's physical task.
         * Retire only our own head, preserving a callback's replacement. */
        if (ent->current_order_id == order_id) ent->current_order_id = 0;
        return true;
    }
    if (G_UnitHasActiveOrder(ent) && G_UnitMoveRevision(ent) != revision) return true;
    /* A completed nested Stop leaves no active task, so the outer cleanup
     * resumes even though its public user head has already retired to zero. */
    stop_cancel_movement(ent);
    stop_finish(ent, false, true);
    return true;
}

void order_stop_cleanup(edict_t *ent) {
    G_ClearUnitOrderQueue(ent);
    order_stop_state(ent, false, false);
}

/* Internal Stop tasks retain the user chain and deliver the ordinary Stand
 * ability event. Their Attack-owned d014a policy is separate from the legacy
 * explicit-Stop guard position, and no public Stop notification is emitted. */
void order_stop_internal(edict_t *ent) {
    order_stop_state(ent, true, false);
}

void order_stop_queued(edict_t *ent) {
    order_stop_state(ent, true, true);
}

static void AbilityStop_Command(edict_t *clent);

BZ_ABILITY_PROC(CAbilityStop) {
    if (msg == A_UNIT_EVENT_MASK)
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_COMMAND, A_AUTO_COMBAT_START, A_AUTO_COMBAT_END, A_ORDER_ACCEPTED);
    if (msg == A_COMMAND) {
        AbilityStop_Command(call && call->client ? call->client : ent);
        return true;
    }
    if (msg == A_AUTO_COMBAT_START) {
        if (ent && ent->movement.guard_state == GUARD_IDLE && ent->currentmove &&
            ent->currentmove->think == ai_stand)
            ent->movement.guard_state = GUARD_COMBAT;
    } else if (msg == A_AUTO_COMBAT_END) {
        if (!ent || ent->movement.guard_state != GUARD_COMBAT) return false;
        if (start_guard_return(ent)) return true;
        if (ent->movement.guard_state == GUARD_COMBAT) ent->movement.guard_state = GUARD_IDLE;
    } else if (msg == A_ORDER_ACCEPTED && ent && call && call->order && strcmp(call->order, "stop")) {
        G_ClearUnitGuardPosition(ent);
    }
    return false;
}

static void AbilityStop_Command(edict_t *clent) {
    gameClient_t *client = clent->client;
    FOR_CONTROLLABLE_SELECTED_UNITS(client, e) {
        if (client->menu.order_queued && G_UnitHasActiveOrder(e)) {
            G_QueueUnitOrder(e, "stop", UNIT_ORDER_TARGET_NONE, NULL, NULL, client->ps.number, 0.0f, 0);
        } else {
            unit_issueimmediateorder(e, "stop");
        }
    }
}
