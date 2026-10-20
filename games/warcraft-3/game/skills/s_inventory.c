#include "s_skills.h"

/* Inventory (AInv and its 2-slot/Pack Mule rows) owns pickup, give and point
 * drop. Point drop follows retail54efe0/56a670: an ordinary Move point task,
 * then a range-checked drop action. Pickup/give remain in g_items.c. */
static void inventory_drop_think(edict_t *),inventory_drop_leave(edict_t *);
static umove_t inventory_move_drop = {.animation="walk",.think=inventory_drop_think,.proc=CAbilityInventory,
    .sample_pose=S_PublishMovement,.leave=inventory_drop_leave};
static queuedOrderResult_t inventory_drop_activate(edict_t *,unitOrder_t const *);

/* Inventory is also the move identity for pickup and drop orders. */
BZ_ABILITY_PROC(CAbilityInventory) {
    switch(msg) {
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_QUEUE_ORDER_START,A_QUEUE_ORDER_CANCEL);
    case A_QUEUE_ORDER_START:
        if(call->queued_order->target_type!=UNIT_ORDER_TARGET_POINT_ITEM)return QUEUED_ORDER_UNHANDLED;
        return inventory_drop_activate(ent,call->queued_order);
    case A_QUEUE_ORDER_CANCEL:
        return call->queued_order->target_type==UNIT_ORDER_TARGET_POINT_ITEM;
    default:
        return CAbilityPassive(ent,msg,call);
    }
}

/*54efe0 reads Misc.DropItemRange through047840 with default0 on each
 * order. An absent key therefore publishes Move's minimum arrival range;
 * this is the original default, not a substitute reach constant. */
static float inventory_drop_range(void) {
    cstring_t value=Stb_IniCacheFind(&game.config.misc,"Misc","DropItemRange");
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
    if(!S_BeginUnitPointApproach(unit,&unit->goalentity->s.origin2,inventory_drop_range(),CAbilityInventory,S_InventoryDropComplete))
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
}

/*56a670 runs after Move arrival and after can't-path alike. It releases the
 * item at the exact requested point only when05b440 (predicted source plus
 * source collision) admits that point; otherwise the order ends carrying it. */
void S_InventoryDropComplete(edict_t *receiver,edict_t *unit,bool arrived) {
    if(receiver!=unit || unit->currentmove!=&inventory_move_drop)return;
    /* Cancellation calls back while the physical binding is retained; only
     * the terminal completion scan clears it before reporting can't-path. */
    if(!arrived && unit->movement.group_id)return;
    edict_t *item=unit->item_drop;
    /*55b420 resolves a retained AgentIdentity, so a new item occupying the
     * same storage cannot inherit the removed item's terminal drop action. */
    if(unit->goalentity && item && item->spawn_time==unit->item_drop_spawn_time && inventory_carries(unit,item)) {
        vec2_t const point=unit->goalentity->s.origin2;
        if(S_UnitPointInMoveRange(unit,&point,inventory_drop_range()))
            G_DropItemAt(unit,(uint32_t)item->item->inventory_slot,&point);
    }
    S_SetMoveGoal(unit,&unit->goalentity,NULL);
    unit->stand(unit);
}

/*54efe0 resolves the packet's item before creating its point/action tasks.
 * Queued activation never borrows a new incarnation at the same edict slot. */
static queuedOrderResult_t inventory_drop_activate(edict_t *unit,unitOrder_t const *packet) {
    edict_t *item=packet->target_number<globals.num_edicts ? g_edicts+packet->target_number : NULL;
    if(!item || item->spawn_time!=packet->target_spawn_time || !inventory_carries(unit,item))return QUEUED_ORDER_UNHANDLED;
    edict_t *goal=Waypoint_add(&packet->point);
    if(!goal)return QUEUED_ORDER_UNHANDLED;
    S_SetMoveGoal(unit,&unit->goalentity,goal);
    move_reset_progress(unit);
    unit_setmove(unit,&inventory_move_drop);
    unit->item_drop=item;unit->item_drop_spawn_time=packet->target_spawn_time;
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
    unitOrder_t packet={.target_type=UNIT_ORDER_TARGET_POINT_ITEM,.point=*point,.target_number=item->s.number,
        .target_spawn_time=item->spawn_time,.issuer_player=unit->s.player,.order_id=G_OrderId("dropitem")};
    snprintf(packet.order,sizeof(packet.order),"%s","dropitem");
    return G_AdmitUnitOrder(unit,&packet,true);
}
