/*
 * t_items.c — Authoritative world-item and phase-one inventory tests.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void setup_test_world(void);
BOOL run_test_jass(LPCSTR src);

static DWORD inventory_refresh_unicast_count;
static LPEDICT inventory_refresh_unicast_target;
static BOOL inventory_refresh_layout_pending;
static BOOL inventory_refresh_saw_inventory_layer;
static BOOL inventory_refresh_saw_other_layer;
static PATHSTR inventory_panel_images[8];
static DWORD inventory_panel_image_count;
static uiFrame_t inventory_panel_frame;
static BOOL inventory_panel_frame_seen;

static void item_noop_write(pfWriteType_t type, void const *value) {
    (void)type;
    (void)value;
}

static void item_noop_unicast(LPEDICT ent) {
    (void)ent;
}

static int capture_inventory_panel_image(LPCSTR name) {
    DWORD index = inventory_panel_image_count;
    if (index < sizeof(inventory_panel_images) / sizeof(inventory_panel_images[0]))
        snprintf(inventory_panel_images[index], sizeof(inventory_panel_images[index]), "%s", name ? name : "");
    inventory_panel_image_count++;
    return (int)(index + 1);
}

static void reset_inventory_panel_capture(void) {
    memset(inventory_panel_images, 0, sizeof(inventory_panel_images));
    inventory_panel_image_count = 0;
    memset(&inventory_panel_frame, 0, sizeof(inventory_panel_frame));
    inventory_panel_frame_seen = false;
}

static void capture_inventory_refresh_write(pfWriteType_t type, void const *value) {
    LONG byte;

    if (type == PF_UIFRAME && value) {
        inventory_panel_frame = *(uiFrame_t const *)value;
        inventory_panel_frame_seen = true;
        return;
    }
    if (type != PF_BYTE || !value) {
        return;
    }
    byte = *(LONG const *)value;
    if (inventory_refresh_layout_pending) {
        if (byte == LAYER_INVENTORY) {
            inventory_refresh_saw_inventory_layer = true;
        } else {
            inventory_refresh_saw_other_layer = true;
        }
        inventory_refresh_layout_pending = false;
        return;
    }
    inventory_refresh_layout_pending = byte == svc_layout;
}

static void capture_inventory_refresh_unicast(LPEDICT ent) {
    inventory_refresh_unicast_count++;
    inventory_refresh_unicast_target = ent;
}

static void reset_inventory_refresh_capture(void) {
    inventory_refresh_unicast_count = 0;
    inventory_refresh_unicast_target = NULL;
    inventory_refresh_layout_pending = false;
    inventory_refresh_saw_inventory_layer = false;
    inventory_refresh_saw_other_layer = false;
}

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

TEST(wc3_items, spawn_initializes_scroll_charges_from_item_data) {
    LPEDICT item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 32, 64);

    SP_SpawnItem(item);

    T_EQ(G_ItemCharges(item), 1);
}

TEST(wc3_items, inventory_capacity_comes_from_inventory_ability_data) {
    LPEDICT standard = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    LPEDICT small = alloc_test_unit(MAKEFOURCC('H','0','0','1'), 0, 0);
    LPEDICT none = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);

    T_EQ(G_InventoryCapacity(standard), 6);
    T_EQ(G_InventoryCapacity(small), 2);
    T_EQ(G_InventoryCapacity(none), 0);
}

TEST(wc3_items, roc_hero_without_authored_inventory_gets_default_ainv_capacity) {
    UnitAbilities_t no_inventory = { .abilList = "", .heroAbilList = "AHhb" };
    LPEDICT hero;

    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->fileFormat = 24;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    hero->data.UnitAbilities = &no_inventory;

    T_ASSERT(G_UnitIsHero(hero));
    T_EQ(G_InventoryCapacity(hero), 6);
}

TEST(wc3_items, tft_hero_without_authored_inventory_does_not_get_roc_default) {
    UnitAbilities_t no_inventory = { .abilList = "", .heroAbilList = "AHhb" };
    LPEDICT hero;

    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->fileFormat = 25;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    hero->data.UnitAbilities = &no_inventory;

    T_ASSERT(G_UnitIsHero(hero));
    T_EQ(G_InventoryCapacity(hero), 0);
}

TEST(wc3_items, inventory_capacity_rejects_zero_and_clamps_above_storage_limit) {
    LPEDICT zero = alloc_test_unit(MAKEFOURCC('H','0','0','2'), 0, 0);
    LPEDICT oversized = alloc_test_unit(MAKEFOURCC('H','0','0','9'), 0, 0);

    T_EQ(G_InventoryCapacity(zero), 0);
    T_EQ(G_InventoryCapacity(oversized), MAX_INVENTORY);
}

TEST(wc3_items, pickup_respects_inventory_capacity_not_storage_size) {
    setup_test_world();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('H','0','0','1'), 0, 0);
    LPEDICT first = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);
    LPEDICT second = make_item_test_world_item(MAKEFOURCC('r','d','e','2'), 64, 0);
    LPEDICT extra = make_item_test_world_item(MAKEFOURCC('s','p','r','o'), 96, 0);

    unit->health.value = unit->health.max_value = 100;
    T_ASSERT(G_AddItemToSlot(unit, first, 0));
    T_ASSERT(G_AddItemToSlot(unit, second, 1));
    T_ASSERT(!G_AddItemToSlot(unit, extra, 2));
    T_EQ(G_FindFreeInventorySlot(unit), -1);
    T_ASSERT(extra->item.in_world);
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

TEST(wc3_items, reserved_client_connection_state_transitions_both_directions) {
    LPEDICT player = &g_edicts[0];
    LPGAMECLIENT client = player->client;

    T_NOT_NULL(client);
    T_ASSERT(!player->inuse);
    T_ASSERT(!client->connected);

    G_SetClientConnected(player, true);
    T_ASSERT(client->connected);

    G_SetClientConnected(player, false);
    T_ASSERT(!client->connected);
}

TEST(wc3_items, pickup_refreshes_inventory_for_connected_reserved_client_edict) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    LPEDICT player;
    LPGAMECLIENT client;
    LPEDICT unit;
    LPEDICT first;
    LPEDICT second;
    BOOL first_picked;
    BOOL second_picked;
    DWORD disconnected_unicasts;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    unit = make_item_test_inventory_unit(0, 0);
    first = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 32, 0);
    second = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 64, 0);
    SP_SpawnItem(first);
    SP_SpawnItem(second);
    gi.LinkEntity(first);
    gi.LinkEntity(second);

    T_NOT_NULL(client);
    T_ASSERT(!player->inuse);
    client->ps.number = 0;
    G_SelectEntity(client, unit);

    G_SetClientConnected(player, false);
    reset_inventory_refresh_capture();
    gi.Write = capture_inventory_refresh_write;
    gi.unicast = capture_inventory_refresh_unicast;
    first_picked = G_PickupItem(unit, first);
    disconnected_unicasts = inventory_refresh_unicast_count;
    gi.Write = old_write;
    gi.unicast = old_unicast;

    G_SetClientConnected(player, true);
    reset_inventory_refresh_capture();
    gi.Write = capture_inventory_refresh_write;
    gi.unicast = capture_inventory_refresh_unicast;
    second_picked = G_PickupItem(unit, second);
    gi.Write = old_write;
    gi.unicast = old_unicast;

    T_ASSERT(first_picked);
    T_EQ(disconnected_unicasts, 0);
    T_ASSERT(second_picked);
    T_ASSERT(inventory_refresh_saw_inventory_layer);
    T_ASSERT(!inventory_refresh_saw_other_layer);
    T_ASSERT(inventory_refresh_unicast_count > 0);
    T_ASSERT(inventory_refresh_unicast_target == player);
}

TEST(wc3_items, inventory_panel_uses_race_cover_when_selected_unit_has_no_inventory) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    LPEDICT player, peasant;
    LPGAMECLIENT client;

    setup_test_world();
    player = &g_edicts[0]; client = player->client;
    peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    client->ps.race = kPlayerRaceHuman;
    G_SelectEntity(client, peasant);

    reset_inventory_refresh_capture(); reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image;
    G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;

    T_ASSERT(inventory_refresh_saw_inventory_layer);
    T_ASSERT(!inventory_refresh_saw_other_layer);
    T_EQ(inventory_panel_image_count, 1);
    T_STREQ(inventory_panel_images[0], "ConsoleInventoryCoverTexture");
    T_ASSERT(inventory_panel_frame_seen);
    T_EQ(inventory_panel_frame.flags.type, FT_TEXTURE);
    T_EQ(inventory_panel_frame.flags.alphaMode, BLEND_MODE_ALPHAKEY);
    T_EQ(inventory_panel_frame.tex.coord[0], 0);
    T_EQ(inventory_panel_frame.tex.coord[1], 0xff);
    T_EQ(inventory_panel_frame.tex.coord[2], (BYTE)(0.380859375f * 0xff));
    T_EQ(inventory_panel_frame.tex.coord[3], 0xff);
    T_FEQ(inventory_panel_frame.size.width, 0.128f, 0.001f);
    T_FEQ(inventory_panel_frame.size.height, 0.175f, 0.001f);
    T_ASSERT(inventory_panel_frame.points.x[FPP_MAX].used);
    T_ASSERT(inventory_panel_frame.points.y[FPP_MAX].used);
    T_FEQ((FLOAT)inventory_panel_frame.points.x[FPP_MAX].offset / UI_FRAMEPOINT_SCALE - inventory_panel_frame.size.width,
          0.472f, 0.001f);
    T_FEQ(-(FLOAT)inventory_panel_frame.points.y[FPP_MAX].offset / UI_FRAMEPOINT_SCALE - inventory_panel_frame.size.height,
          0.425f, 0.001f);
}

TEST(wc3_items, footman_unit_inventory_stays_covered_until_human_backpack_is_researched) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    LPEDICT player, footman;
    LPGAMECLIENT client;

    setup_test_world();
    player = &g_edicts[0]; client = player->client;
    footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    client->ps.race = kPlayerRaceHuman;
    G_SelectEntity(client, footman);

    T_EQ(G_InventoryCapacity(footman), 0);
    reset_inventory_refresh_capture(); reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image;
    G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_EQ(inventory_panel_image_count, 1);
    T_STREQ(inventory_panel_images[0], "ConsoleInventoryCoverTexture");

    G_SetPlayerTechResearched(client, MAKEFOURCC('R','h','p','m'), 1);
    T_EQ(G_InventoryCapacity(footman), 2);
    reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image;
    G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_EQ(inventory_panel_image_count, 4);
    FOR_LOOP(i, 4) T_STREQ(inventory_panel_images[i], "ConsoleInventoryNoCapacity");
}

TEST(wc3_items, inventory_panel_uses_local_player_race_not_selected_unit_race) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    LPEDICT player, peasant; LPGAMECLIENT client;

    setup_test_world(); player = &g_edicts[0]; client = player->client;
    peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    client->ps.race = kPlayerRaceOrc; G_SelectEntity(client, peasant);
    reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image; G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_EQ(inventory_panel_image_count, 1);
    T_STREQ(inventory_panel_images[0], "ConsoleInventoryCoverTexture");
}

TEST(wc3_items, inventory_panel_falls_back_to_default_skin_for_unknown_player_race) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    LPEDICT player, peasant; LPGAMECLIENT client;

    setup_test_world(); player = &g_edicts[0]; client = player->client;
    peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    client->ps.race = kPlayerRaceNone; G_SelectEntity(client, peasant);
    reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image; G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_EQ(inventory_panel_image_count, 1);
    T_STREQ(inventory_panel_images[0], "ConsoleInventoryCoverTexture");
}

TEST(wc3_items, inventory_panel_marks_only_slots_outside_reduced_capacity) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    LPEDICT player, unit; LPGAMECLIENT client;

    setup_test_world(); player = &g_edicts[0]; client = player->client;
    unit = alloc_test_unit(MAKEFOURCC('H','0','0','1'), 0, 0);
    client->ps.race = kPlayerRaceHuman; G_SelectEntity(client, unit);
    reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image; G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_EQ(G_InventoryCapacity(unit), 2); T_EQ(inventory_panel_image_count, 4);
    FOR_LOOP(i, 4) T_STREQ(inventory_panel_images[i], "ConsoleInventoryNoCapacity");
}

TEST(wc3_items, inventory_panel_leaves_all_slots_visible_at_full_capacity) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    LPEDICT player, unit; LPGAMECLIENT client;

    setup_test_world(); player = &g_edicts[0]; client = player->client;
    unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    client->ps.race = kPlayerRaceHuman; G_SelectEntity(client, unit);
    reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image; G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_EQ(G_InventoryCapacity(unit), MAX_INVENTORY); T_EQ(inventory_panel_image_count, 0);
}

TEST(wc3_items, multiselect_inventory_panel_follows_focused_selected_unit) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    LPEDICT player, peasant, inventory_unit;
    LPGAMECLIENT client;

    setup_test_world();
    player = &g_edicts[0]; client = player->client;
    peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    inventory_unit = alloc_test_unit(MAKEFOURCC('H','0','0','1'), 32, 0);
    client->ps.race = kPlayerRaceHuman;
    G_ResetSelectionFocus(client);
    G_SelectEntity(client, peasant);
    G_SelectEntity(client, inventory_unit);

    T_ASSERT(G_GetMainSelectedUnit(client) == peasant);
    T_ASSERT(G_IsEntitySelected(client, peasant));
    T_ASSERT(G_IsEntitySelected(client, inventory_unit));

    reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image; G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_EQ(inventory_panel_image_count, 1);
    T_STREQ(inventory_panel_images[0], "ConsoleInventoryCoverTexture");

    T_ASSERT(G_FocusSelectedUnit(client, inventory_unit));
    T_ASSERT(G_GetMainSelectedUnit(client) == inventory_unit);
    T_ASSERT(G_IsEntitySelected(client, peasant));
    T_ASSERT(G_IsEntitySelected(client, inventory_unit));

    reset_inventory_panel_capture();
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image; G_RefreshInventoryLayer(player);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_EQ(inventory_panel_image_count, 4);
    FOR_LOOP(i, 4)
        T_STREQ(inventory_panel_images[i], "ConsoleInventoryNoCapacity");
}

TEST(wc3_items, inventory_ui_resolves_scroll_metadata_and_charge) {
    gameInventoryItem_t items[MAX_INVENTORY];
    LPEDICT unit;
    LPEDICT item;
    BYTE count;

    setup_test_world();
    unit = make_item_test_inventory_unit(0, 0);
    item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 32, 0);
    SP_SpawnItem(item); gi.LinkEntity(item);

    T_ASSERT(G_PickupItem(unit, item));
    count = G_GetInventory(unit, items, MAX_INVENTORY);
    T_EQ(count, 1);
    T_EQ(items[0].slot, 0);
    T_EQ(items[0].charges, 1);
    T_STREQ(items[0].art, "TestUI\\Textures\\solid_white.blp");
    T_STREQ(items[0].tooltip, "Scroll of Protection");
    T_STREQ(items[0].ubertip, "Temporarily increases the armor of nearby units.");

    G_SetItemCharges(item, 0);
    count = G_GetInventory(unit, items, MAX_INVENTORY);
    T_EQ(count, 1);
    T_EQ(items[0].charges, 0);
}

TEST(wc3_items, carried_charge_change_refreshes_inventory_and_same_value_is_noop) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    LPEDICT player;
    LPGAMECLIENT client;
    LPEDICT unit;
    LPEDICT item;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    unit = make_item_test_inventory_unit(0, 0);
    item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 32, 0);
    SP_SpawnItem(item);
    gi.LinkEntity(item);

    T_NOT_NULL(client);
    client->ps.number = 0;
    G_SelectEntity(client, unit);

    /* Pick up while disconnected so the assertion below observes only the
     * charge-change refresh path. */
    G_SetClientConnected(player, false);
    T_ASSERT(G_PickupItem(unit, item));
    G_SetClientConnected(player, true);

    reset_inventory_refresh_capture();
    gi.Write = capture_inventory_refresh_write;
    gi.unicast = capture_inventory_refresh_unicast;
    G_SetItemCharges(item, 3);
    gi.Write = old_write;
    gi.unicast = old_unicast;

    T_EQ(G_ItemCharges(item), 3);
    T_ASSERT(inventory_refresh_saw_inventory_layer);
    T_ASSERT(!inventory_refresh_saw_other_layer);
    T_ASSERT(inventory_refresh_unicast_count > 0);
    T_ASSERT(inventory_refresh_unicast_target == player);

    reset_inventory_refresh_capture();
    gi.Write = capture_inventory_refresh_write;
    gi.unicast = capture_inventory_refresh_unicast;
    G_SetItemCharges(item, 3);
    gi.Write = old_write;
    gi.unicast = old_unicast;

    T_EQ(inventory_refresh_unicast_count, 0);
    T_ASSERT(!inventory_refresh_saw_inventory_layer);
}

TEST(wc3_items, perishable_success_consumes_charge_and_removes_at_zero) {
    ItemData_t perishable = { .perishable = true };
    LPEDICT item;

    setup_test_world();
    item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 64, 0);
    item->data.ItemData = &perishable;
    item->item.charges = 2;

    G_ConsumeItemCharge(item);
    T_ASSERT(item->inuse);
    T_EQ(item->item.charges, 1);

    G_ConsumeItemCharge(item);
    T_ASSERT(!item->inuse);
}

TEST(wc3_items, nonperishable_use_decrements_charges_but_keeps_item_at_zero) {
    ItemData_t reusable = { .perishable = false };
    LPEDICT item;

    setup_test_world();
    item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 64, 0);
    item->data.ItemData = &reusable;
    item->item.charges = 2;

    G_ConsumeItemCharge(item);
    T_ASSERT(item->inuse);
    T_EQ(item->item.charges, 1);

    G_ConsumeItemCharge(item);
    T_ASSERT(item->inuse);
    T_EQ(item->item.charges, 0);
}

TEST(wc3_items, inventory_click_uses_itemdata_ability_list_and_applies_scroll) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    LPEDICT clent;
    LPGAMECLIENT client;
    LPEDICT unit;
    LPEDICT item;
    FLOAT base_armor;
    BOOL found_buff = false;
    LPCSTR command[] = { "inventory", "0" };

    setup_test_world();
    clent = &g_edicts[0];
    client = clent->client;
    gi.Write = item_noop_write;
    gi.unicast = item_noop_unicast;

    unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    unit->s.player = client->ps.number;
    unit->svflags |= SVF_MONSTER;
    unit->targtype = TARG_GROUND;
    unit->armor_value = 3.0f;
    base_armor = G_UnitArmorValue(unit);
    G_SelectEntity(client, unit);

    item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 32, 0);
    SP_SpawnItem(item);
    gi.LinkEntity(item);
    T_STREQ(G_ItemAbilityList(item), "AIda");
    T_ASSERT(G_AddItemToSlot(unit, item, 0));

    G_ClientCommand(clent, 2, command);

    T_FEQ(G_UnitArmorValue(unit), base_armor + 2.0f, 0.01f);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == MAKEFOURCC('B','d','e','f')) {
            found_buff = true;
            break;
        }
    }
    T_ASSERT(found_buff);
    T_NULL(unit->inventory[0]);
    T_ASSERT(!item->inuse);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_items, drop_preserves_item_charges) {
    setup_test_world();
    LPEDICT unit = make_item_test_inventory_unit(128, 256);
    LPEDICT item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 64, 0);

    SP_SpawnItem(item); gi.LinkEntity(item);
    T_EQ(G_ItemCharges(item), 1);
    T_ASSERT(G_PickupItem(unit, item));
    T_ASSERT(G_DropItem(unit, 0));
    T_EQ(G_ItemCharges(item), 1);
}

TEST(wc3_items, jass_item_charge_natives_use_runtime_item_state) {
    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local item i = CreateItem('spro', 64.0, 64.0)\n"
        "  call BJassAssert(GetItemCharges(i) == 1, \"initial charges\")\n"
        "  call SetItemCharges(i, 3)\n"
        "  call BJassAssert(GetItemCharges(i) == 3, \"updated charges\")\n"
        "  call SetItemCharges(i, -1)\n"
        "  call BJassAssert(GetItemCharges(i) == 0, \"negative charges clamp\")\n"
        "endfunction\n"));
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

TEST(wc3_items, mixed_selection_smart_item_orders_roc_hero_even_when_nonhero_is_first) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(LPEDICT) = gi.unicast;
    UnitAbilities_t no_inventory = { .abilList = "", .heroAbilList = "AHhb" };
    LPEDICT clent;
    LPGAMECLIENT client;
    LPEDICT footman;
    LPEDICT hero;
    LPEDICT item;
    char item_number[16];
    LPCSTR command[] = { "smart", item_number };

    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->fileFormat = 24;
    clent = &g_edicts[0];
    client = clent->client;
    gi.Write = item_noop_write;
    gi.unicast = item_noop_unicast;

    /* Allocate the ordinary unit first so it is also the server's primary
     * selected unit. Smart target dispatch must still reach the later hero. */
    footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 32, 0);
    item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 96, 0);
    footman->s.player = hero->s.player = client->ps.number;
    footman->svflags |= SVF_MONSTER;
    hero->svflags |= SVF_MONSTER;
    hero->data.UnitAbilities = &no_inventory;
    footman->stand = unit_stand;
    hero->stand = unit_stand;
    unit_stand(footman);
    unit_stand(hero);
    G_SelectEntity(client, footman);
    G_SelectEntity(client, hero);
    snprintf(item_number, sizeof(item_number), "%u", (unsigned)item->s.number);

    G_ClientCommand(clent, 2, command);

    T_NULL(footman->goalentity);
    T_ASSERT(hero->goalentity == item);
    T_NOT_NULL(hero->currentmove);
    T_STREQ(hero->currentmove->animation, "walk");

    gi.Write = old_write;
    gi.unicast = old_unicast;
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
