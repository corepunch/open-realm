#include "s_skills.h"

/* Inventory (AInv and its 2-slot/Pack Mule rows) owns pickup, give and point
 * drop. Point drop follows retail54efe0/56a670: an ordinary Move point task,
 * then a range-checked drop action. Give/Pawn retain a recipient and independent
 * item identity; Move owns their half-range approach. */
static void inventory_drop_think(edict_t *),inventory_drop_leave(edict_t *);
static void inventory_target_think(edict_t *);
static umove_t inventory_move_drop = {.animation="walk",.think=inventory_drop_think,.proc=CAbilityInventory,
    .sample_pose=S_PublishMovement,.leave=inventory_drop_leave};
/*54efe0 retains the admitted recipient even when it now rejects receipt. */
static umove_t inventory_move_target_drop={.animation="walk",.think=inventory_target_think,.proc=CAbilityInventory,
    .sample_pose=S_PublishMovement,.leave=inventory_drop_leave};
static umove_t inventory_move_give={.animation="walk",.think=inventory_target_think,.proc=CAbilityInventory,
    .sample_pose=S_PublishMovement,.leave=inventory_drop_leave};
static umove_t inventory_move_pawn={.animation="walk",.think=inventory_target_think,.proc=CAbilityInventory,
    .sample_pose=S_PublishMovement,.leave=inventory_drop_leave};
static queuedOrderResult_t inventory_drop_activate(edict_t *,unitOrder_t const *);
static queuedOrderResult_t inventory_target_activate(edict_t *,unitOrder_t const *);

/* Inventory is also the move identity for pickup and drop orders. */
BZ_ABILITY_PROC(CAbilityInventory) {
    switch(msg) {
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_QUEUE_ORDER_START,A_QUEUE_ORDER_CANCEL,A_MOVE_LEAVE,A_DISABLE);
    case A_DISABLE:
        /* Removal has already erased the concrete rawcode. Ordinary disable
         * contributions retain it and must not release carried items. */
        if(call->item && !G_ActorHasAbilityCode(ent,call->item->code) && !G_InventoryCapacity(ent))
            G_ReleaseRemovedInventory(ent);
        return true;
    case A_MOVE_LEAVE:
        /* A successor can stage the very same target before unit_setmove.
         * Only a transition to Stand/absence releases the old goal here. */
        if(ent->currentmove && ent->currentmove->proc==CAbilityInventory && call->next_move_proc!=CAbilityInventory) {
            if(!call->next_move_proc)S_SetMoveGoal(ent,&ent->goalentity,NULL);
            inventory_drop_leave(ent);
        }
        return false;
    case A_QUEUE_ORDER_START:
        if(call->queued_order->target_type==UNIT_ORDER_TARGET_ENTITY_ITEM)
            return inventory_target_activate(ent,call->queued_order);
        if(call->queued_order->target_type!=UNIT_ORDER_TARGET_POINT_ITEM)return QUEUED_ORDER_UNHANDLED;
        return inventory_drop_activate(ent,call->queued_order);
    case A_QUEUE_ORDER_CANCEL:
        return call->queued_order->target_type==UNIT_ORDER_TARGET_POINT_ITEM ||
            call->queued_order->target_type==UNIT_ORDER_TARGET_ENTITY_ITEM;
    default:
        return CAbilityPassive(ent,msg,call);
    }
}

/*54efe0 reads Misc.DropItemRange through047840 with default0 on each
 * order. An absent key therefore publishes Move's minimum arrival range;
 * this is the original default, not a substitute reach constant. */
static float inventory_range(cstring_t key) {
    cstring_t value=Stb_IniCacheFind(&game.config.misc,"Misc",key);
    return value && *value ? wc3_decimal(value) : 0;
}

/* A retained slot must still be the item's live carrier binding. */
static bool inventory_carries(edict_t const *unit,edict_t const *item) {
    if(!G_IsItem(item) || item->item->carrier!=unit || item->item->in_world)return false;
    int32_t slot=item->item->inventory_slot;
    return slot>=0 && slot<MAX_INVENTORY && unit->inventory[slot]==item;
}

/*54efe0 prepends d016b at the requested point with DropItemRange and no
 * association, before the d017c action. The physical task belongs to Move. */
static void inventory_drop_start(edict_t *unit) {
    if(S_UnitTargetChaseActive(unit,CAbilityInventory))return;
    if(!S_BeginUnitPointApproach(unit,&unit->goalentity->s.origin2,unit->item_drop_range,CAbilityInventory,S_InventoryDropComplete))
        S_InventoryDropComplete(unit,unit,false);
}

static void inventory_drop_think(edict_t *unit) {
    /* Native scenes10/11 retain the point task after RemoveItem/UnitRemoveItem.
     * Item ownership is checked by the terminal action, not by its Move owner. */
    if(!unit->goalentity) {
        unit->stand(unit);
        return;
    }
    inventory_drop_start(unit);
}

static void inventory_drop_leave(edict_t *unit) {
    S_EndUnitTargetChase(unit,CAbilityInventory);
    unit->item_drop=NULL;unit->item_drop_spawn_time=unit->item_drop_target_spawn_time=0;
    unit->item_drop_range=0;
}

/*56a670 runs after Move arrival and after can't-path alike. It releases the
 * item at the exact requested point only when05b440 (predicted source plus
 * source collision) admits that point; otherwise the order ends carrying it. */
void S_InventoryDropComplete(edict_t *receiver,edict_t *unit,bool arrived) {
    bool const target_drop=unit->currentmove==&inventory_move_target_drop;
    if(receiver!=unit || (unit->currentmove!=&inventory_move_drop && !target_drop))return;
    /* Cancellation calls back while the physical binding is retained; only
     * the terminal completion scan clears it before reporting can't-path. */
    if(!arrived && unit->movement.group_id)return;
    uint32_t const spawn=unit->spawn_time,revision=G_UnitMoveRevision(unit);
    edict_t *item=unit->item_drop;
    /*55b420 resolves a retained AgentIdentity, so a new item occupying the
     * same storage cannot inherit the removed item's terminal drop action. */
    /* Target-associated d017c reads its retained widget's current center.
     * An invalid incarnation cannot supply a replacement drop location. */
    if(unit->goalentity && (!target_drop || (unit->goalentity->inuse && unit->goalentity->spawn_time==unit->item_drop_target_spawn_time)) &&
       item && item->spawn_time==unit->item_drop_spawn_time && inventory_carries(unit,item)) {
        vec2_t const point=unit->goalentity->s.origin2;
        if(S_UnitPointInMoveRange(unit,&point,unit->item_drop_range))
            G_DropItemAt(unit,(uint32_t)item->item->inventory_slot,&point);
    }
    if(unit->inuse && unit->spawn_time==spawn && !G_IsDeferredFree(unit) && G_UnitMoveRevision(unit)==revision) {
        S_SetMoveGoal(unit,&unit->goalentity,NULL);
        unit->stand(unit);
    }
}

/*54efe0 resolves the packet's item before creating its point/action tasks.
 * Queued activation never borrows a new incarnation at the same edict slot. */
static queuedOrderResult_t inventory_drop_activate(edict_t *unit,unitOrder_t const *packet) {
    edict_t *item=packet->item_number<globals.num_edicts ? g_edicts+packet->item_number : NULL;
    if(!item || item->spawn_time!=packet->item_spawn_time || !inventory_carries(unit,item))return QUEUED_ORDER_UNHANDLED;
    edict_t *goal=Waypoint_add(&packet->point);
    if(!goal)return QUEUED_ORDER_UNHANDLED;
    S_SetMoveGoal(unit,&unit->goalentity,goal);
    move_reset_progress(unit);
    unit_setmove(unit,&inventory_move_drop);
    unit->item_drop=item;unit->item_drop_spawn_time=packet->item_spawn_time;
    unit->item_drop_range=inventory_range("DropItemRange");
    unit->current_order_id=packet->order_id;
    inventory_drop_start(unit);
    /* An immobile carrier completes the point/action chain synchronously.
     * Do not restore its retired public head in the common activation caller. */
    return unit->currentmove==&inventory_move_drop ? QUEUED_ORDER_STARTED : QUEUED_ORDER_REPLACED;
}

/*2188b0/206b90 validate first, then680320 replaces through the common FIFO.
 * ort2 is not an ordinary point/target subtype, so activation emits no issued event. */
bool S_OrderDropItemPoint(edict_t *unit,edict_t *item,vec2_t const *point) {
    if(!unit || !point || S_UnitOrderDisabled(unit,"dropitem") || !G_InventoryCanDropItems(unit) || !inventory_carries(unit,item) ||
       !G_ItemDroppable(item) || G_ItemAbilitiesPreventDrop(unit,item))return false;
    unitOrder_t packet={.target_type=UNIT_ORDER_TARGET_POINT_ITEM,.point=*point,.item_number=item->s.number,
        .item_spawn_time=item->spawn_time,.issuer_player=unit->s.player,.order_id=G_OrderId("dropitem")};
    snprintf(packet.order,sizeof(packet.order),"%s","dropitem");
    return G_AdmitUnitOrder(unit,&packet,true);
}

/*545ed0 gives Apit precedence over Inventory. A rejected recipient becomes
 * an unassociated point/drop task at its captured position (206c40). */
static bool inventory_can_pawn(edict_t *source,edict_t *item,edict_t *target) {
    return target!=source && G_UnitIsWorldActive(target) && !G_IsDeferredFree(target) && !M_IsDead(target) &&
        !(target->s.renderfx&RF_HIDDEN) && !target->destructable && !G_IsItem(target) && !target->client &&
        G_ActorHasAbilityCode(target,MAKEFOURCC('A','p','i','t')) &&
        G_IsUnitAbilityAvailable(target,MAKEFOURCC('A','p','i','t')) &&
        !S_AbilityExecutionDisabled(target,MAKEFOURCC('A','p','i','t')) &&
        G_PlayerTreatsPlayerAsAlly(target->s.player,source->s.player) && G_ItemPawnable(item);
}

/* V4 4..7: the source's shared-spells grant permits receipt. The target's
 * incoming-grant table in1eaa40 is not the public setter's direction. A full
 * recipient at admission takes point fallback; a recipient
 * that fills later fails the retained Give action without dropping the item.
 * V7: inv4 controls ordinary pickup only; Give forces receipt like66eba0. */
static bool inventory_can_give(edict_t *source,edict_t *target) {
    if(target==source || !G_UnitIsWorldActive(target) || G_IsDeferredFree(target) || M_IsDead(target) ||
       target->destructable || G_IsItem(target) || target->client || (target->s.renderfx&RF_HIDDEN) ||
       !G_UnitHasInventory(target) || G_FindFreeInventorySlot(target)<0)return false;
    return source->s.player==target->s.player ||
        (source->s.player<MAX_PLAYERS && target->s.player<MAX_PLAYERS &&
         G_GetPlayerAlliance(&game.clients[source->s.player].ps,&game.clients[target->s.player].ps,ALLIANCE_SHARED_SPELLS));
}

/*56aa50/56ab80 revalidate both identities and authored terminal reach. The
 * original perimeter exception admits a building edge independently of range. */
void S_InventoryTargetComplete(edict_t *receiver,edict_t *unit,bool arrived) {
    if(receiver!=unit || (unit->currentmove!=&inventory_move_give && unit->currentmove!=&inventory_move_pawn))return;
    if(!arrived && unit->movement.group_id)return;
    edict_t *target=unit->goalentity,*item=unit->item_drop;
    uint32_t const spawn=unit->spawn_time,revision=G_UnitMoveRevision(unit);
    if(target && target->inuse && target->spawn_time==unit->item_drop_target_spawn_time &&
       item && item->spawn_time==unit->item_drop_spawn_time && inventory_carries(unit,item) &&
       G_InventoryCanDropItems(unit) && G_ItemDroppable(item) && !G_ItemAbilitiesPreventDrop(unit,item) &&
       (S_UnitTargetInMoveRange(unit,target,unit->item_drop_range) || S_UnitTargetOnPerimeter(unit,target))) {
        if(unit->currentmove==&inventory_move_pawn) {
            if(inventory_can_pawn(unit,item,target))G_PawnCarriedItem(target,item);
        } else if(inventory_can_give(unit,target))G_GiveCarriedItem(target,item);
    }
    /* A synchronous item callback can replace the order or remove its actor.
     * Retire only the Inventory action that entered this completion scope. */
    if(unit->inuse && unit->spawn_time==spawn && !G_IsDeferredFree(unit) && G_UnitMoveRevision(unit)==revision) {
        S_SetMoveGoal(unit,&unit->goalentity,NULL);
        unit->stand(unit);
    }
}

/*54efe0 prepends d0171 unless half-range AND the .2-radian facing query
 * both succeed. Immobile sources still execute the terminal action. */
static void inventory_target_start(edict_t *unit) {
    if(S_UnitTargetChaseActive(unit,CAbilityInventory))return;
    edict_t *target=unit->goalentity;
    float range=wc3_mul(unit->item_drop_range,.5f);
    void (*complete)(edict_t *,edict_t *,bool)=unit->currentmove==&inventory_move_target_drop ? S_InventoryDropComplete : S_InventoryTargetComplete;
    if(!target || !target->inuse || target->spawn_time!=unit->item_drop_target_spawn_time ||
       (S_UnitTargetInMoveRange(unit,target,range) && S_UnitTargetInFacingWindow(unit,target,wc3_float(0x3e4ccccd))) ||
       !S_BeginUnitTargetChase(unit,target,range,CAbilityInventory,complete))
        complete(unit,unit,false);
    else S_SetUnitTargetChaseLife(unit,MOVE_TARGET_RETAIN_DEAD);
}

static void inventory_target_think(edict_t *unit) { inventory_target_start(unit); }

/* The concrete umove identity retains Give, Pawn or target-bound Drop across
 * save/load. Only an absent recipient takes the unassociated point branch. */
static queuedOrderResult_t inventory_target_activate(edict_t *unit,unitOrder_t const *packet) {
    edict_t *item=packet->item_number<globals.num_edicts ? g_edicts+packet->item_number : NULL;
    edict_t *target=packet->target_number<globals.num_edicts ? g_edicts+packet->target_number : NULL;
    if(!item || item->spawn_time!=packet->item_spawn_time || !inventory_carries(unit,item))return QUEUED_ORDER_UNHANDLED;
    if(!target || !target->inuse || target->spawn_time!=packet->target_spawn_time)return inventory_drop_activate(unit,packet);
    bool const pawn=inventory_can_pawn(unit,item,target),give=!pawn && inventory_can_give(unit,target);
    S_SetMoveGoal(unit,&unit->goalentity,target);
    move_reset_progress(unit);
    /* A live retained recipient that changed eligibility still owns d017c
     * and d0171 at half Drop range; it is not an unassociated d016b point. */
    umove_t *move=pawn ? &inventory_move_pawn : give ? &inventory_move_give : &inventory_move_target_drop;
    unit_setmove(unit,move);
    unit->item_drop=item;unit->item_drop_spawn_time=packet->item_spawn_time;
    unit->item_drop_target_spawn_time=packet->target_spawn_time;
    unit->item_drop_range=inventory_range(pawn ? "PawnItemRange" : give ? "GiveItemRange" : "DropItemRange");
    unit->current_order_id=packet->order_id;
    uint32_t revision=G_UnitMoveRevision(unit);
    inventory_target_start(unit);
    return unit->currentmove==move && G_UnitMoveRevision(unit)==revision ? QUEUED_ORDER_STARTED : QUEUED_ORDER_REPLACED;
}

/*218980/206c40 retains widget position and independent AgentIdentity pairs.
 * Unsupported recipient classes are accepted through the point/drop validator. */
bool S_OrderDropItemTarget(edict_t *unit,edict_t *item,edict_t *target) {
    if(!unit || !target || !target->inuse || !G_UnitIsWorldActive(unit) || G_IsDeferredFree(unit) || M_IsDead(unit) ||
       S_UnitOrderDisabled(unit,"dropitem") || !G_InventoryCanDropItems(unit) || !inventory_carries(unit,item) ||
       !G_ItemDroppable(item) || G_ItemAbilitiesPreventDrop(unit,item))return false;
    unitOrder_t packet={.target_type=UNIT_ORDER_TARGET_ENTITY_ITEM,.point=target->s.origin2,
        .target_number=target->s.number,.target_spawn_time=target->spawn_time,
        .item_number=item->s.number,.item_spawn_time=item->spawn_time,
        .issuer_player=unit->s.player,.order_id=G_OrderId("dropitem")};
    /*206c40 clears an invalid recipient before constructing ort2. Preserve
     * the clicked widget position, without advertising a target association. */
    if(!inventory_can_pawn(unit,item,target) && !inventory_can_give(unit,target)) {
        packet.target_type=UNIT_ORDER_TARGET_POINT_ITEM;
        packet.target_number=packet.target_spawn_time=0;
    }
    snprintf(packet.order,sizeof(packet.order),"%s","dropitem");
    return G_AdmitUnitOrder(unit,&packet,true);
}
