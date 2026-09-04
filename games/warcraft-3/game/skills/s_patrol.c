#include "s_skills.h"

static void ai_patrol_walk(LPEDICT ent) {
    if (G_ShouldAcquireThisFrame(ent)) {
        LPEDICT enemy = G_FindNearestEnemy(ent, G_AcquisitionRange(ent));
        if (enemy) {
            order_attack(ent, enemy);
            return;
        }
    }

    FLOAT distance = M_DistanceToGoal(ent);
    FLOAT move_distance = unit_movedistance(ent);

    if (move_should_arrive(ent, move_distance) || move_is_blocked(ent, distance, move_distance)) {
        ent->movement.patrol_target = ent->movement.patrol_target == ent->movement.patrol_a
            ? ent->movement.patrol_b : ent->movement.patrol_a;
        ent->goalentity = ent->movement.patrol_target;
        move_reset_progress(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static umove_t patrol_move_walk = { "walk", ai_patrol_walk, NULL, &a_patrol };

void order_patrol_resume(LPEDICT self) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    self->goalentity = self->movement.patrol_target;
    move_reset_progress(self);
    unit_setmove(self, &patrol_move_walk);
}

void order_patrol(LPEDICT self, LPEDICT b) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    self->movement.attackmove_waypoint = NULL;
    self->movement.patrol_a = Waypoint_add(&self->s.origin2);
    self->movement.patrol_b = b;
    self->movement.patrol_target = b;
    self->movement.follow_target = NULL;
    self->movement.holding_position = false;
    order_patrol_resume(self);
}

static BOOL patrol_selectlocation(LPEDICT clent, LPCVECTOR2 location) {
    BOOL any = false;

    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if ((ent->aiflags & AI_IMMOBILE) || ent->data.UnitBalance->speed <= 0) {
            continue;
        }
        VECTOR2 target = *location;
        CM_ClosestPathablePointForRadius(location, ent->collision, &target);
        order_patrol(ent, Waypoint_add(&target));
        any = true;
    }
    if (any) G_SendPointConfirmation(clent, location, false);
    return any;
}

static void patrol_command(LPEDICT ent) {
    UI_AddCancelButton(ent);
    ent->client->menu.on_location_selected = patrol_selectlocation;
}

ability_t a_patrol = {
    .cmd = patrol_command,
};
