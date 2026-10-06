#include "s_skills.h"

static void ai_patrol_walk(edict_t *ent) {
    if (G_ShouldAcquireThisFrame(ent)) {
        edict_t *enemy = G_FindNearestEnemy(ent, G_AcquisitionRange(ent));
        if (enemy) {
            order_attack(ent, enemy);
            return;
        }
    }

    float distance = M_DistanceToGoal(ent);
    float move_distance = unit_movedistance(ent);

    if (move_should_arrive(ent, move_distance) || move_is_blocked(ent, distance, move_distance)) {
        S_SetMoveGoal(ent, &ent->movement.patrol_target, ent->movement.patrol_target == ent->movement.patrol_a
            ? ent->movement.patrol_b : ent->movement.patrol_a);
        S_SetMoveGoal(ent, &ent->goalentity, ent->movement.patrol_target);
        move_reset_progress(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static umove_t patrol_move_walk = { "walk", ai_patrol_walk, NULL, CAbilityPatrol };

void order_patrol_resume(edict_t *self) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    S_SetMoveGoal(self, &self->goalentity, self->movement.patrol_target);
    move_reset_progress(self);
    unit_setmove(self, &patrol_move_walk);
}

void order_patrol(edict_t *self, edict_t *b) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    S_SetMoveGoal(self, &self->movement.attackmove_waypoint, NULL);
    S_SetMoveGoal(self, &self->movement.patrol_a, Waypoint_add(&self->s.origin2));
    S_SetMoveGoal(self, &self->movement.patrol_b, b);
    S_SetMoveGoal(self, &self->movement.patrol_target, b);
    self->movement.follow_target = NULL;
    self->movement.holding_position = false;
    order_patrol_resume(self);
}

/* Activate the two-endpoint order; reversals and combat resumes retain it. */
bool S_IssuePatrolOrder(edict_t *self, edict_t *target) {
    if (!self || !target) return false;
    order_patrol(self, target);
    if (self->goalentity != target || self->currentmove != &patrol_move_walk) return false;
    /* Issued-order callbacks retain WC3_ORDER_ID_PATROL. The Patrol owner
     * publishes the active order only after both endpoints are admitted. */
    self->current_order_id = WC3_ORDER_ID_PATROL_TWO_POINTS;
    return true;
}

static bool patrol_selectlocation(edict_t *clent, vec2_t const *location) {
    bool any = false;

    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if ((ent->aiflags & AI_IMMOBILE) || ent->data.UnitBalance->speed <= 0) {
            continue;
        }
        if (G_IssueUnitPointOrder(ent, "patrol", location, clent->client->menu.order_queued, clent->client->ps.number, 0))
            any = true;
    }
    if (any) G_SendPointConfirmation(clent, location, false);
    return any;
}

BZ_COMMAND_PROC(AbilityPatrol) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = patrol_selectlocation;
    clent->client->menu.supports_order_queue = true;
}
