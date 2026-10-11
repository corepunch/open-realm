#include "s_skills.h"
#include "games/warcraft-3/common/wc3_math.h"

/* Original5fe1a0 compares scalar squared distance with6fd6fb28, initialized
 * by013530 to10000. This gate applies only to issued, single-point Patrol. */
#define WC3_PATROL_MINIMUM_DISTANCE_SQUARED 10000.0f
#define PATROL_QUEUE_TWO_POINTS 1u // owner-local queue payload kind; not a retail order ID

static void patrol_clear(edict_t *ent) {
    S_SetMoveGoal(ent,&ent->movement.patrol_target,NULL);
    S_SetMoveGoal(ent,&ent->movement.patrol_b,NULL);
    S_SetMoveGoal(ent,&ent->movement.patrol_a,NULL);
    S_SetMoveGoal(ent,&ent->goalentity,NULL);
}

/* Original5fcc70 skips the active head and tests the queued commands. */
static bool patrol_has_queued_non_patrol(edict_t const *ent) {
    unitOrderQueue_t const *queue=&ent->order_queue;
    FOR_LOOP(i,queue->count) {
        unitOrder_t const *order=queue->entries+(queue->head+i)%queue->capacity;
        if (strcmp(order->order,"patrol")) return true;
    }
    return false;
}

static void patrol_append_leg(edict_t *ent, vec2_t const *point, vec2_t const *continuation) {
    unitOrder_t order={.target_type=UNIT_ORDER_TARGET_POINT,.point=*point,
        .continuation=*continuation,.issuer_player=ent->s.player,.owner_context=PATROL_QUEUE_TWO_POINTS};
    strlcpy(order.order,"patrol",sizeof(order.order));
    /* Retirement immediately pops one entry. The reserved slot ensures that
     * a full user FIFO cannot discard this original d0175 continuation. */
    if (!G_AppendUnitOrder(ent,&order)) gi.error("WC3 Patrol: continuation exceeded reserved FIFO capacity");
}

/* False when the head rotated behind queued work instead of starting a leg. */
static bool patrol_begin_leg(edict_t *ent, vec2_t const *point, vec2_t const *continuation) {
    /* Original5fdff0 rotates a newly activated two-point head behind any
     * queued non-Patrol command, retaining both endpoints without recapture.
     * It stops without moving; the activating queue pass pops the successor,
     * so starting it here would leave the outer pass restoring this head. */
    if (patrol_has_queued_non_patrol(ent)) {
        patrol_append_leg(ent,point,continuation);
        patrol_clear(ent);
        return false;
    }
    S_SetMoveGoal(ent,&ent->movement.attackmove_waypoint,NULL);
    S_SetMoveGoal(ent,&ent->movement.patrol_a,Waypoint_add(continuation));
    S_SetMoveGoal(ent,&ent->movement.patrol_b,Waypoint_add(point));
    S_SetMoveGoal(ent,&ent->movement.patrol_target,ent->movement.patrol_b);
    S_SetFollowTarget(ent,NULL);ent->movement.holding_position=false;
    order_patrol_resume(ent);
    ent->current_order_id=WC3_ORDER_ID_PATROL_TWO_POINTS;
    return true;
}

static bool patrol_leg_active(edict_t const *ent) {
    return ent->current_order_id==WC3_ORDER_ID_PATROL_TWO_POINTS && ent->movement.patrol_target &&
        ent->goalentity==ent->movement.patrol_target && move_is_active_order_walk(ent);
}

/* Each leg is retail's ordinary d016b point task (5fdff0), so it uses Move's
 * scheduled point ownership: arrival range, endpoint retry and detours. */
void order_patrol_resume(edict_t *self) {
    if (S_GoldMineWorkerIsInside(self)) return;
    S_IssueMoveOrder(self,self->movement.patrol_target,WC3_ORDER_ID_PATROL_TWO_POINTS);
}

/* d0175 runs at the point task's arrival (5fa7a0), which the generic can't-path
 * fallback also reaches. True when Patrol owns this leg's completion. */
bool S_PatrolMoveArrive(edict_t *ent) {
    if (!ent || !patrol_leg_active(ent)) return false;
    edict_t *next=ent->movement.patrol_target==ent->movement.patrol_a ? ent->movement.patrol_b : ent->movement.patrol_a;
    /* An empty FIFO's append/pop pair has no successor to reorder. Reuse
     * the retained leg without allocating queue or waypoint storage. */
    if (!ent->order_queue.count) {
        S_SetMoveGoal(ent,&ent->movement.patrol_target,next);
        order_patrol_resume(ent);
        S_UnitAbilityOrderAccepted(ent,"patrol");
        return true;
    }
    vec2_t point=next->s.origin2,continuation=ent->movement.patrol_target->s.origin2;
    /* d0175 appends the reversed leg before the current head completes.
     * Previously reversing in place starved every Shift successor. */
    patrol_append_leg(ent,&point,&continuation);
    patrol_clear(ent);unit_stand(ent);
    return true;
}

bool order_patrol(edict_t *self, edict_t *target) {
    if (S_GoldMineWorkerIsInside(self)) return false;
    vec2_t point=target->s.origin2,origin=self->s.origin2;
    return patrol_begin_leg(self,&point,&origin);
}

bool S_IssuePatrolOrder(edict_t *self, edict_t *target) {
    if (!self || !target || S_GoldMineWorkerIsInside(self)) return false;
    float x=wc3_sub(target->s.origin2.x,self->s.origin2.x),y=wc3_sub(target->s.origin2.y,self->s.origin2.y);
    float distance=wc3_add(wc3_mul(x,x),wc3_mul(y,y));
    if (distance<WC3_PATROL_MINIMUM_DISTANCE_SQUARED) {
        patrol_clear(self);unit_stand(self);
        return true;
    }
    return order_patrol(self,target);
}

static bool patrol_selectlocation(edict_t *clent, vec2_t const *location) {
    bool any=false;
    FOR_CONTROLLABLE_SELECTED_UNITS(clent->client,ent) {
        if ((ent->aiflags&AI_IMMOBILE) || ent->data.UnitBalance->speed<=0) continue;
        if (G_IssueUnitPointOrder(ent,"patrol",location,clent->client->menu.order_queued,clent->client->ps.number,0)) any=true;
    }
    if (any) G_SendPointConfirmation(clent,location,false);
    return any;
}

BZ_ABILITY_PROC(CAbilityPatrol) {
    switch(msg) {
    case A_UNIT_TYPE_UPDATE:
        return UNIT_UPDATE_POINTER(movement.patrol_target);
    case A_UPDATE:
        /* Routing runs on Move's owner clock. Keep automatic acquisition on
         * the existing once-per-frame ability pass without an unsaved stamp. */
        if (ent && !ent->paused && !ent->stunned && patrol_leg_active(ent) &&
            G_ShouldAcquireThisFrame(ent)) {
            edict_t *enemy=G_FindNearestEnemy(ent,G_AcquisitionRange(ent));
            if (enemy) order_attack(ent,enemy);
        }
        return true;
    case A_QUEUE_ORDER_START:
        if (!call || !call->queued_order || call->queued_order->owner_context!=PATROL_QUEUE_TWO_POINTS ||
            !ent || !ent->inuse || M_IsDead(ent) || S_GoldMineWorkerIsInside(ent)) return false;
        return patrol_begin_leg(ent,&call->queued_order->point,&call->queued_order->continuation);
    case A_COMMAND: {
        edict_t *clent=call && call->client ? call->client : ent;
        UI_AddCancelButton(clent);
        clent->client->menu.on_location_selected=patrol_selectlocation;
        clent->client->menu.supports_order_queue=true;
        return true;
    }
    default:return false;
    }
}
