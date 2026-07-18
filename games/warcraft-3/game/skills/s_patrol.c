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
        ent->patrol_target = ent->patrol_target == ent->patrol_a ? ent->patrol_b : ent->patrol_a;
        ent->goalentity = ent->patrol_target;
        move_reset_progress(ent);
    } else {
        unit_changeangle(ent);
        unit_moveindirection(ent);
    }
}

static umove_t patrol_move_walk = { "walk", ai_patrol_walk, NULL, &a_patrol };

void order_patrol_resume(LPEDICT self) {
    self->goalentity = self->patrol_target;
    move_reset_progress(self);
    unit_setmove(self, &patrol_move_walk);
}

void order_patrol(LPEDICT self, LPEDICT b) {
    self->attackmove_waypoint = NULL;
    self->holding_position = false;
    self->patrol_a = Waypoint_add(&self->s.origin2);
    self->patrol_b = b;
    self->patrol_target = b;
    order_patrol_resume(self);
}

static BOOL patrol_selectlocation(LPEDICT clent, LPCVECTOR2 location) {
    BOOL any = false;

    FOR_SELECTED_UNITS(clent->client, ent) {
        if (UNIT_IS_BUILDING(ent->class_id) || UNIT_SPEED(ent->class_id) <= 0) {
            continue;
        }
        VECTOR2 target = *location;
        CM_ClosestPathablePointForRadius(location, ent->collision, &target);
        order_patrol(ent, Waypoint_add(&target));
        any = true;
    }
    return any;
}

static void patrol_command(LPEDICT ent) {
    UI_AddCancelButton(ent);
    ent->client->menu.on_location_selected = patrol_selectlocation;
}

ability_t a_patrol = {
    .cmd = patrol_command,
};
