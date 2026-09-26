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
        ent->movement.patrol_target = ent->movement.patrol_target == ent->movement.patrol_a
            ? ent->movement.patrol_b : ent->movement.patrol_a;
        ent->goalentity = ent->movement.patrol_target;
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
    self->goalentity = self->movement.patrol_target;
    move_reset_progress(self);
    unit_setmove(self, &patrol_move_walk);
}

void order_patrol(edict_t *self, edict_t *b) {
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

static bool patrol_selectlocation(edict_t *clent, vec2_t const *location) {
    bool any = false;

    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client, ent) {
        if ((ent->aiflags & AI_IMMOBILE) || ent->data.UnitBalance->speed <= 0) {
            continue;
        }
        vec2_t target = *location;
        CM_ClosestPathablePointForRadiusFlags(location, ent->collision, M_UnitStaticPathingFlags(ent), &target);
        order_patrol(ent, Waypoint_add(&target));
        any = true;
    }
    if (any) G_SendPointConfirmation(clent, location, false);
    return any;
}

BZ_COMMAND_PROC(AbilityPatrol) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = patrol_selectlocation;
}
