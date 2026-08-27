/*
 * t_items.c — Authoritative world-item and phase-one inventory tests.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void setup_test_world(void);

static LPEDICT make_item_test_inventory_unit(FLOAT x, FLOAT y) {
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), x, y);
    unit->s.model = 1;
    unit->s.player = PLAYER_NEUTRAL_PASSIVE;
    unit->movetype = MOVETYPE_STEP;
    unit->collision = 16.0f;
    unit->health.value = 100.0f;
    unit->health.max_value = 100.0f;
    unit->unitinfo.MoveSpeed = 270.0f;
    unit->unitinfo.TurnSpeed = 1.0f;
    unit->stand = unit_stand;
    unit_stand(unit);
    gi.LinkEntity(unit);
    return unit;
}

static LPEDICT make_item_test_world_item(DWORD class_id, FLOAT x, FLOAT y) {
    LPEDICT item = alloc_test_unit(class_id, x, y);
    item->s.model = 1;
    item->movetype = MOVETYPE_NONE;
    item->targtype = TARG_ITEM;
    item->item.carrier = NULL;
    item->item.inventory_slot = -1;
    item->item.in_world = true;
    gi.LinkEntity(item);
    return item;
}

TEST(wc3_items, spawn_initializes_world_state) {
    LPEDICT item = alloc_test_unit(MAKEFOURCC('r','a','t','f'), 32, 64);

    SP_SpawnItem(item);

    T_ASSERT(G_IsItem(item));
    T_ASSERT(item->item.in_world);
    T_NULL(item->item.carrier);
    T_EQ(item->item.inventory_slot, -1);
    T_EQ(item->targtype, TARG_ITEM);
    T_ASSERT(!(item->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(item->svflags & SVF_NOCLIENT));
}

TEST(wc3_items, pickup_sets_both_sides_of_inventory_state) {
    setup_test_world();
    LPEDICT unit = make_item_test_inventory_unit(0, 0);
    LPEDICT item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 64, 0);

    T_ASSERT(G_AddItemToSlot(unit, item, 2));
    T_ASSERT(unit->inventory[2] == item);
    T_ASSERT(item->item.carrier == unit);
    T_EQ(item->item.inventory_slot, 2);
    T_ASSERT(!item->item.in_world);
    T_ASSERT(item->s.renderfx & RF_HIDDEN);
    T_ASSERT(item->svflags & SVF_NOCLIENT);
    T_NULL(item->area.prev);
}

TEST(wc3_items, pickup_uses_first_empty_slot) {
    setup_test_world();
    LPEDICT unit = make_item_test_inventory_unit(0, 0);
    LPEDICT first = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);
    LPEDICT second = make_item_test_world_item(MAKEFOURCC('r','d','e','2'), 64, 0);

    T_ASSERT(G_AddItemToSlot(unit, first, 0));
    T_ASSERT(G_PickupItem(unit, second));
    T_ASSERT(unit->inventory[1] == second);
    T_EQ(second->item.inventory_slot, 1);
}

TEST(wc3_items, unit_without_inventory_capability_rejects_item) {
    setup_test_world();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);

    unit->health.value = unit->health.max_value = 100;
    T_ASSERT(!G_UnitHasInventory(unit));
    T_ASSERT(!G_PickupItem(unit, item));
    T_ASSERT(item->item.in_world);
    T_NULL(item->item.carrier);
}

TEST(wc3_items, full_inventory_leaves_item_in_world) {
    setup_test_world();
    LPEDICT unit = make_item_test_inventory_unit(0, 0);

    FOR_LOOP(slot, MAX_INVENTORY) {
        LPEDICT item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32.0f + slot, 0);
        T_ASSERT(G_AddItemToSlot(unit, item, slot));
    }
    LPEDICT extra = make_item_test_world_item(MAKEFOURCC('r','d','e','2'), 96, 0);

    T_ASSERT(!G_PickupItem(unit, extra));
    T_ASSERT(extra->item.in_world);
    T_NULL(extra->item.carrier);
    T_EQ(extra->item.inventory_slot, -1);
    T_ASSERT(!(extra->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(extra->svflags & SVF_NOCLIENT));
    T_NOT_NULL(extra->area.prev);
}

TEST(wc3_items, drop_restores_same_item_to_world) {
    setup_test_world();
    LPEDICT unit = make_item_test_inventory_unit(128, 256);
    LPEDICT item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 64, 0);

    T_ASSERT(G_AddItemToSlot(unit, item, 3));
    T_ASSERT(G_DropItem(unit, 3));
    T_NULL(unit->inventory[3]);
    T_NULL(item->item.carrier);
    T_EQ(item->item.inventory_slot, -1);
    T_ASSERT(item->item.in_world);
    T_FEQ(item->s.origin2.x, unit->s.origin2.x, 0.001f);
    T_FEQ(item->s.origin2.y, unit->s.origin2.y, 0.001f);
    T_ASSERT(!(item->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(item->svflags & SVF_NOCLIENT));
    T_NOT_NULL(item->area.prev);
}

TEST(wc3_items, pickup_order_waits_for_simulation_tick) {
    setup_test_world();
    LPEDICT unit = make_item_test_inventory_unit(0, 0);
    LPEDICT item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), ITEM_PICKUP_RANGE - 1, 0);

    T_ASSERT(unit_issuetargetorder(unit, "smart", item));
    T_NULL(unit->inventory[0]);
    T_ASSERT(item->item.in_world);

    unit->currentmove->think(unit);

    T_ASSERT(unit->inventory[0] == item);
    T_ASSERT(!item->item.in_world);
    T_NULL(unit->goalentity);
}

TEST(wc3_items, pickup_order_moves_and_revalidates_item) {
    setup_test_world();
    LPEDICT unit = make_item_test_inventory_unit(0, 0);
    LPEDICT item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), ITEM_PICKUP_RANGE + 100, 0);

    T_ASSERT(G_OrderPickupItem(unit, item));
    unit->currentmove->think(unit);
    T_ASSERT(item->item.in_world);
    T_ASSERT(unit->s.origin2.x > 0);

    item->item.in_world = false;
    unit->currentmove->think(unit);
    T_NULL(unit->goalentity);
    T_NULL(unit->inventory[0]);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_items, removing_carried_item_clears_slot) {
    setup_test_world();
    LPEDICT unit = make_item_test_inventory_unit(0, 0);
    LPEDICT item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);

    T_ASSERT(G_PickupItem(unit, item));
    G_RemoveItem(item);

    T_NULL(unit->inventory[0]);
    T_ASSERT(!item->inuse);
}

#endif /* BZ_TESTS */
