#include "s_skills.h"

static void ai_holdpos_stand(edict_t * self) {
    if (!G_ShouldAcquireThisFrame(self))
        return;
    /* Hold Position still detects hostile units at the data-defined acquisition
     * radius; the order controls the post-acquisition chase, not perception. */
    edict_t * enemy = G_FindNearestEnemy(self, G_AcquisitionRange(self));
    if (enemy) {
        order_attack(self, enemy);
    }
}

umove_t holdpos_move_stand = { "stand", ai_holdpos_stand, unit_stand };
umove_t holdpos_move_stand_ready = { "stand ready", ai_holdpos_stand, unit_stand };

bool S_HoldPosition(edict_t * unit) {
    if (!unit || M_IsDead(unit) || S_GoldMineWorkerIsInside(unit))
        return false;
    G_ClearUnitOrderQueue(unit);
    unit->movement.attackmove_waypoint = NULL;
    unit->movement.patrol_a = NULL;
    unit->movement.patrol_b = NULL;
    unit->movement.patrol_target = NULL;
    unit->movement.follow_target = NULL;
    unit->movement.holding_position = true;
    unit_leavecombat(unit);
    unit_stand(unit);
    G_InvalidateUnitShortcutsForUnit(unit);
    return true;
}

BZ_COMMAND_PROC(AbilityHoldPosition) {
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, e)
        S_HoldPosition(e);
}
