#include "s_skills.h"

/* Do not borrow a FIFO slot for an infinite Patrol. Remember one owner-
 * local continuation while accepted user commands execute at the leg boundary. */
static void patrol_defer_after_queue(edict_t *ent) {
    unitOrders_t *orders = ent->orders;
    orders->patrol_resume_a = orders->patrol_a->s.origin2;
    orders->patrol_resume_b = orders->patrol_b->s.origin2;
    orders->patrol_resume_to_b = orders->patrol_target == orders->patrol_b;
    orders->patrol_resume_owner_spawn = ent->spawn_time;
    orders->patrol_resume_pending = true;
}

bool G_ResumePatrolContinuation(edict_t *self) {
    unitOrders_t *orders;
    edict_t *a, *b;
    bool to_b;
    if (!self || !self->inuse || M_IsDead(self) || !self->orders) return false;
    orders = self->orders;
    if (!orders->patrol_resume_pending || G_UnitQueuedOrderCount(self)) return false;
    if (orders->patrol_resume_owner_spawn != self->spawn_time) {
        orders->patrol_resume_pending = false;
        return false;
    }
    a = Waypoint_add(&orders->patrol_resume_a);
    b = Waypoint_add(&orders->patrol_resume_b);
    if (!a || !b) return false;
    to_b = orders->patrol_resume_to_b;
    orders->patrol_resume_pending = false; /* consume exactly once */
    orders->patrol_a = a;
    orders->patrol_b = b;
    orders->patrol_target = to_b ? b : a;
    orders->attackmove_waypoint = NULL;
    orders->follow_target = NULL;
    orders->follow_target_spawn_time = 0;
    order_patrol_resume(self);
    return true;
}

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

    if (move_should_arrive(ent, move_distance)) {
        /* A leg boundary is the only point that may hand over ownership.
         * Keep original A/B, and resume toward the opposite endpoint after
         * all previously accepted user commands have completed. */
        ent->orders->patrol_target = ent->orders->patrol_target == ent->orders->patrol_a
            ? ent->orders->patrol_b : ent->orders->patrol_a;
        if (G_UnitQueuedOrderCount(ent)) {
            patrol_defer_after_queue(ent);
            if (G_UnitStartNextQueuedOrder(ent)) return;
            ent->orders->patrol_resume_pending = false; /* invalid queued entries */
        }
        ent->goalentity = ent->orders->patrol_target;
        move_reset_progress(ent);
        return;
    }
    /* A blocked or partial route is not endpoint arrival. In particular,
     * never reverse because a legal partial approach was reached. */
    if (move_is_blocked(ent, distance, move_distance)) {
        unit_changeangle(ent); /* retry on static revision / fresh route */
        return;
    }
    unit_changeangle(ent);
    if (!ent->movement || ent->movement->flow_unreachable) return;
    unit_moveindirection(ent);
}

static umove_t patrol_move_walk = { "walk", ai_patrol_walk, NULL, CAbilityPatrol, .travel = true };

void order_patrol_resume(edict_t *self) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    self->goalentity = self->orders->patrol_target;
    move_reset_progress(self);
    unit_setmove(self, &patrol_move_walk);
}

void order_patrol(edict_t *self, edict_t *b) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    S_SpellCancelChannel(self);
    self->orders->attackmove_waypoint = NULL;
    self->orders->patrol_resume_pending = false; /* a fresh Patrol replaces old continuation */
    self->orders->patrol_a = Waypoint_add(&self->s.origin2);
    self->orders->patrol_b = b;
    self->orders->patrol_target = b;
    self->orders->follow_target = NULL;
    self->orders->holding_position = false;
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
        if (G_IssueUnitPointOrder(ent, "patrol", &target,
                                  clent->client->menu.order_queued,
                                  clent->client->ps.number, 0.0f))
            any = true;
    }
    if (any) G_SendPointConfirmation(clent, location, false);
    return any;
}

BZ_COMMAND_PROC(AbilityPatrol) {
    UI_AddCancelButton(clent);
    clent->client->menu.on_location_selected = patrol_selectlocation;
}
