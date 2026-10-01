#include "s_skills.h"

static void ai_holdpos_stand(edict_t *self) {
    if (G_UnitQueuedOrderCount(self) && G_UnitStartNextQueuedOrder(self))
        return;
    if (!G_ShouldAcquireThisFrame(self))
        return;
    /* Hold Position still detects hostile units at the data-defined acquisition
     * radius; the order controls the post-acquisition chase, not perception. */
    edict_t *enemy = G_FindNearestEnemy(self, G_AcquisitionRange(self));
    if (enemy) {
        order_attack(self, enemy);
    }
}

umove_t holdpos_move_stand = { "stand", ai_holdpos_stand, unit_stand };
umove_t holdpos_move_stand_ready = { "stand ready", ai_holdpos_stand, unit_stand };

static bool hold_position_state(edict_t *unit, bool preserve_queue) {
    if (!unit || M_IsDead(unit) || S_GoldMineWorkerIsInside(unit))
        return false;
    /* Hold is an authoritative replacement order just like Stop. Interrupt an
     * active channel before installing the persistent no-chase state. */
    S_SpellCancelChannel(unit);
    unit->movement.attackmove_waypoint = NULL;
    unit->movement.patrol_a = NULL;
    unit->movement.patrol_b = NULL;
    unit->movement.patrol_target = NULL;
    unit->movement.follow_target = NULL;
    G_ClearUnitGuardPosition(unit);
    unit->movement.holding_position = true;
    unit_leavecombat(unit);
    if (preserve_queue) unit_stand_no_queue(unit);
    else unit_stand(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
    return true;
}

bool S_HoldPosition(edict_t *unit) {
    if (!unit) return false;
    G_ClearUnitOrderQueue(unit);
    return hold_position_state(unit, false);
}

bool S_HoldPositionQueued(edict_t *unit) {
    return hold_position_state(unit, true);
}

BZ_COMMAND_PROC(AbilityHoldPosition) {
    gameClient_t *client = clent->client;
    FOR_CONTROLLABLE_SELECTED_UNITS(client, e) {
        if (client->menu.order_queued && G_UnitHasActiveOrder(e)) {
            if (G_QueueUnitOrder(e, "holdposition", UNIT_ORDER_TARGET_NONE, NULL, NULL,
                                 client->ps.number, 0.0f, 0)) {
                G_PublishIssuedImmediateOrder(e, G_OrderId("holdposition"), client->ps.number, "holdposition");
            }
        } else {
            unit_issueimmediateorder(e, "holdposition");
        }
    }
}
