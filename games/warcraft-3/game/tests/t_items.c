/*
 * t_items.c — Authoritative world-item and phase-one inventory tests.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
bool run_test_jass(cstring_t src);
slkTestData_t *parse_slk_string(char const *slk_text);
void free_slk_rows(slkTestData_t *rows);

static uint32_t inventory_refresh_unicast_count;
static edict_t *inventory_refresh_unicast_target;
static bool inventory_refresh_layout_pending;
static bool inventory_refresh_saw_inventory_layer;
static bool inventory_refresh_saw_other_layer;
static PATHSTR inventory_panel_images[8];
static uint32_t inventory_panel_image_count;
static uiFrame_t inventory_panel_frame;
static bool inventory_panel_frame_seen;

/* Supply a valid target callback so the Cancel command exercises target-mode cleanup. */
static bool item_test_target_callback(edict_t *clent, edict_t *target) {
    (void)clent;
    (void)target;
    return false;
}

/* Supply a valid point callback for the same target-mode cleanup test. */
static bool item_test_location_callback(edict_t *clent, vec2_t const *location) {
    (void)clent;
    (void)location;
    return false;
}

static void item_noop_write(pfWriteType_t type, void const *value) {
    (void)type;
    (void)value;
}

static void item_noop_unicast(edict_t *ent) {
    (void)ent;
}

static int capture_inventory_panel_image(cstring_t name) {
    uint32_t index = inventory_panel_image_count;
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
    int32_t byte;

    if (type == PF_UIFRAME && value) {
        inventory_panel_frame = *(uiFrame_t const *)value;
        inventory_panel_frame_seen = true;
        return;
    }
    if (type != PF_BYTE || !value) {
        return;
    }
    byte = *(int32_t const *)value;
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

static void capture_inventory_refresh_unicast(edict_t *ent) {
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

static edict_t *make_item_test_inventory_unit(float x, float y) {
    static UnitAbilities_t abilities = { .abilList = "AInv", .heroAbilList = "" };
    edict_t *unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), x, y);
    unit->data.UnitAbilities = &abilities;
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

static edict_t *make_item_test_world_item(uint32_t class_id, float x, float y) {
    edict_t *item = alloc_test_unit(class_id, x, y);
    item->s.model = 1;
    item->movetype = MOVETYPE_NONE;
    item->targtype = TARG_ITEM;
    item->item.carrier = NULL;
    item->item.inventory_slot = -1;
    item->item.in_world = true;
    gi.LinkEntity(item);
    return item;
}

static edict_t *make_item_test_shop(float x, float y) {
    static UnitProfile_t profile;
    static UnitAbilities_t abilities = { .abilList = "Apit", .heroAbilList = "" };
    edict_t *shop = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, y);

    memset(&profile, 0, sizeof(profile));
    profile.sellItems = "spro";
    shop->data.UnitProfile = &profile;
    shop->data.UnitAbilities = &abilities;
    shop->s.player = PLAYER_NEUTRAL_PASSIVE;
    shop->collision = 32.0f;
    shop->spawn_time = G_Time();
    shop->stock.item_slots = 11;
    gi.LinkEntity(shop);
    return shop;
}

static edict_t *make_unit_test_shop(float x, float y) {
    static UnitProfile_t profile;
    static UnitAbilities_t abilities = { .abilList = "Aneu,Asud", .heroAbilList = "" };
    edict_t *shop = alloc_test_unit(MAKEFOURCC('h','f','o','o'), x, y);

    memset(&profile, 0, sizeof(profile));
    profile.sellUnits = "nmer";
    shop->data.UnitProfile = &profile;
    shop->data.UnitAbilities = &abilities;
    shop->s.player = PLAYER_NEUTRAL_PASSIVE;
    shop->collision = 32.0f;
    shop->spawn_time = G_Time();
    shop->stock.unit_slots = 11;
    gi.LinkEntity(shop);
    return shop;
}

static edict_t *make_unit_shop_patron(float x, float y, uint32_t player) {
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    unit->s.player = player;
    unit->health.value = unit->health.max_value = 100.0f;
    unit->movetype = MOVETYPE_STEP;
    unit->collision = 16.0f;
    unit->svflags |= SVF_MONSTER;
    gi.LinkEntity(unit);
    return unit;
}

TEST(wc3_items, spawn_initializes_world_state) {
    edict_t *item = alloc_test_unit(MAKEFOURCC('r','a','t','f'), 32, 64);

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
    edict_t *item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 32, 64);

    SP_SpawnItem(item);

    T_EQ(G_ItemCharges(item), 1);
}

TEST(wc3_items, change_time_item_uses_ability_hour_minute_and_duration) {
    ability_t const *ability;
    edict_t *player;
    edict_t *hero;

    setup_test_world();
    player = &g_edicts[0];
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    hero->s.player = 0;
    hero->health.value = hero->health.max_value = 100.0f;
    player->client->ps.number = 0;
    G_SelectEntity(player->client, hero);
    player->client->menu.ability_code = MAKEFOURCC('A','I','c','t');

    ability = FindAbilityForCommand("AIct");
    T_NOT_NULL(ability);
    T_ASSERT(ability->flags & AB_ITEM);
    {
        abilityitem_t item = S_AbilityItem(MAKEFOURCC('A','I','c','t'));
        abilityCall_t call = MAKE(abilityCall_t, .item = &item, .client = player);
        T_ASSERT(S_AbilityMessage(player, A_ITEM_USE, &call));
    }
    T_ASSERT(level.timeofday.false_time.active);
    T_ASSERT(!level.timeofday.false_time.initialized);
    T_EQ(level.timeofday.false_time.hour, 18);
    T_EQ(level.timeofday.false_time.minute, 30);
    T_EQ(level.timeofday.false_time.ticks_remaining,
         (int32_t)(30.0f / ((float)FRAMETIME / 1000.0f)));
}

TEST(wc3_items, inventory_capacity_comes_from_inventory_ability_data) {
    edict_t *standard = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    edict_t *small = alloc_test_unit(MAKEFOURCC('H','0','0','1'), 0, 0);
    edict_t *none = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);

    T_EQ(G_InventoryCapacity(standard), 6);
    T_EQ(G_InventoryCapacity(small), 2);
    T_EQ(G_InventoryCapacity(none), 0);
}

TEST(wc3_items, roc_hero_without_authored_inventory_gets_default_ainv_capacity) {
    UnitAbilities_t no_inventory = { .abilList = "", .heroAbilList = "AHhb" };
    edict_t *hero;

    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->fileFormat = 24;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    hero->data.UnitAbilities = &no_inventory;

    T_ASSERT(G_UnitIsHero(hero));
    T_EQ(G_InventoryCapacity(hero), 6);
}

TEST(wc3_items, tft_hero_without_authored_inventory_does_not_get_roc_default) {
    UnitAbilities_t no_inventory = { .abilList = "", .heroAbilList = "AHhb" };
    edict_t *hero;

    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->fileFormat = 25;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    hero->data.UnitAbilities = &no_inventory;

    T_ASSERT(G_UnitIsHero(hero));
    T_EQ(G_InventoryCapacity(hero), 0);
}

/* ROC AIa1/AIa3/AIa6 are agility bonuses, not inventory capacities. The real
 * archive has these in this order and has no AInv row. Use ROC column names. */
static const char roc_inventory_slk[] =
    "ID;PWXL;N;EBB;Y6;X3\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Data11\"\n"
    "C;Y2;X1;K\"AIa1\"\nC;Y2;X2;K\"AIab\"\nC;Y2;X3;K\"1\"\n"
    "C;Y3;X1;K\"AIa3\"\nC;Y3;X2;K\"AIab\"\nC;Y3;X3;K\"3\"\n"
    "C;Y4;X1;K\"AIa6\"\nC;Y4;X2;K\"AIab\"\nC;Y4;X3;K\"6\"\n"
    "C;Y5;X1;K\"Aiv4\"\nC;Y5;X2;K\"AInv\"\nC;Y5;X3;K\"4\"\n"
    "C;Y6;X1;K\"Aiv0\"\nC;Y6;X2;K\"AInv\"\nC;Y6;X3;K\"0\"\nE\n";

TEST(wc3_items, roc_hero_inventory_does_not_read_attribute_bonus_as_capacity) {
    UnitAbilities_t abilities[] = { { .abilList = "" }, { .abilList = "AInv" }, { .abilList = "AIa1" } };
    uint32_t capacity[3], image_count[3]; bool use[3], get[3], drop[3];
    slkTestData_t *rows = parse_slk_string(roc_inventory_slk), *old = G_SetSLKRows("AbilityData", rows);
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->fileFormat = 24;
    edict_t *player = &g_edicts[0], *hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    player->client->ps.race = kPlayerRaceUndead; G_SelectEntity(player->client, hero);
    gi.Write = capture_inventory_refresh_write; gi.unicast = capture_inventory_refresh_unicast;
    gi.ImageIndex = capture_inventory_panel_image;
    FOR_LOOP(i, 3) {
        hero->data.UnitAbilities = &abilities[i];
        capacity[i] = G_InventoryCapacity(hero);
        use[i] = G_InventoryCanUseItems(hero); get[i] = G_InventoryCanGetItems(hero); drop[i] = G_InventoryCanDropItems(hero);
        reset_inventory_panel_capture(); G_RefreshInventoryLayer(player);
        image_count[i] = inventory_panel_image_count;
    }
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    FOR_LOOP(i, 3) {
        T_EQ(capacity[i], 6); T_EQ(image_count[i], 0); /* No blocked-slot texture requests. */
        T_ASSERT(use[i]); T_ASSERT(get[i]); T_ASSERT(drop[i]);
    }
}

TEST(wc3_items, attribute_bonus_does_not_grant_unit_inventory) {
    UnitAbilities_t abilities = { .abilList = "AIa6" };
    slkTestData_t *rows = parse_slk_string(roc_inventory_slk), *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->fileFormat = 24;
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    unit->data.UnitAbilities = &abilities;
    uint32_t capacity = G_InventoryCapacity(unit);
    bool use = G_InventoryCanUseItems(unit), get = G_InventoryCanGetItems(unit), drop = G_InventoryCanDropItems(unit);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_EQ(capacity, 0); T_ASSERT(!use); T_ASSERT(!get); T_ASSERT(!drop);
}

TEST(wc3_items, roc_authored_inventory_capacity_overrides_hero_default_including_zero) {
    UnitAbilities_t abilities = { .abilList = "Aiv4" };
    slkTestData_t *rows = parse_slk_string(roc_inventory_slk), *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->fileFormat = 24;
    edict_t *hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    hero->data.UnitAbilities = &abilities;
    uint32_t capacity = G_InventoryCapacity(hero);
    abilities.abilList = "Aiv0";
    uint32_t zero = G_InventoryCapacity(hero);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    T_EQ(capacity, 4); T_EQ(zero, 0);
}

TEST(wc3_items, inventory_capacity_rejects_zero_and_clamps_above_storage_limit) {
    edict_t *zero = alloc_test_unit(MAKEFOURCC('H','0','0','2'), 0, 0);
    edict_t *oversized = alloc_test_unit(MAKEFOURCC('H','0','0','9'), 0, 0);

    T_EQ(G_InventoryCapacity(zero), 0);
    T_EQ(G_InventoryCapacity(oversized), MAX_INVENTORY);
}

TEST(wc3_items, pickup_respects_inventory_capacity_not_storage_size) {
    setup_test_world();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('H','0','0','1'), 0, 0);
    edict_t *first = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);
    edict_t *second = make_item_test_world_item(MAKEFOURCC('r','d','e','2'), 64, 0);
    edict_t *extra = make_item_test_world_item(MAKEFOURCC('s','p','r','o'), 96, 0);

    unit->health.value = unit->health.max_value = 100;
    T_ASSERT(G_AddItemToSlot(unit, first, 0));
    T_ASSERT(G_AddItemToSlot(unit, second, 1));
    T_ASSERT(!G_AddItemToSlot(unit, extra, 2));
    T_EQ(G_FindFreeInventorySlot(unit), -1);
    T_ASSERT(extra->item.in_world);
}

TEST(wc3_items, pickup_sets_both_sides_of_inventory_state) {
    setup_test_world();
    edict_t *unit = make_item_test_inventory_unit(0, 0);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 64, 0);

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
    edict_t *unit = make_item_test_inventory_unit(0, 0);
    edict_t *first = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);
    edict_t *second = make_item_test_world_item(MAKEFOURCC('r','d','e','2'), 64, 0);

    T_ASSERT(G_AddItemToSlot(unit, first, 0));
    T_ASSERT(G_PickupItem(unit, second));
    T_ASSERT(unit->inventory[1] == second);
    T_EQ(second->item.inventory_slot, 1);
}

TEST(wc3_items, unit_without_inventory_capability_rejects_item) {
    setup_test_world();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);

    unit->health.value = unit->health.max_value = 100;
    T_ASSERT(!G_UnitHasInventory(unit));
    T_ASSERT(!G_PickupItem(unit, item));
    T_ASSERT(item->item.in_world);
    T_NULL(item->item.carrier);
}

TEST(wc3_items, full_inventory_leaves_item_in_world) {
    setup_test_world();
    edict_t *unit = make_item_test_inventory_unit(0, 0);

    FOR_LOOP(slot, MAX_INVENTORY) {
        edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32.0f + slot, 0);
        T_ASSERT(G_AddItemToSlot(unit, item, slot));
    }
    edict_t *extra = make_item_test_world_item(MAKEFOURCC('r','d','e','2'), 96, 0);

    T_ASSERT(!G_PickupItem(unit, extra));
    T_ASSERT(extra->item.in_world);
    T_NULL(extra->item.carrier);
    T_EQ(extra->item.inventory_slot, -1);
    T_ASSERT(!(extra->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(extra->svflags & SVF_NOCLIENT));
    T_NOT_NULL(extra->area.prev);
}

TEST(wc3_items, reserved_client_connection_state_transitions_both_directions) {
    edict_t *player = &g_edicts[0];
    gameClient_t *client = player->client;

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
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *player;
    gameClient_t *client;
    edict_t *unit;
    edict_t *first;
    edict_t *second;
    bool first_picked;
    bool second_picked;
    uint32_t disconnected_unicasts;

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
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *player, *peasant;
    gameClient_t *client;

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
    T_EQ(inventory_panel_frame.tex.coord[2], (uint8_t)(0.380859375f * 0xff));
    T_EQ(inventory_panel_frame.tex.coord[3], 0xff);
    T_FEQ(inventory_panel_frame.size.width, 0.128f, 0.001f);
    T_FEQ(inventory_panel_frame.size.height, 0.175f, 0.001f);
    T_ASSERT(inventory_panel_frame.points.x[FPP_MAX].used);
    T_ASSERT(inventory_panel_frame.points.y[FPP_MAX].used);
    T_FEQ((float)inventory_panel_frame.points.x[FPP_MAX].offset / UI_FRAMEPOINT_SCALE - inventory_panel_frame.size.width,
          0.472f, 0.001f);
    T_FEQ(-(float)inventory_panel_frame.points.y[FPP_MAX].offset / UI_FRAMEPOINT_SCALE - inventory_panel_frame.size.height,
          0.425f, 0.001f);
}

TEST(wc3_items, footman_unit_inventory_stays_covered_until_human_backpack_is_researched) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *player, *footman;
    gameClient_t *client;

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

TEST(wc3_items, human_backpack_carrier_cannot_use_item_abilities) {
    edict_t *player, *footman, *hero, *item;
    gameClient_t *client;

    setup_test_world();
    player = &g_edicts[0]; client = player->client;
    footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 64, 0);
    footman->s.player = hero->s.player = client->ps.number;
    footman->health.value = footman->health.max_value = 100.0f;
    hero->health.value = hero->health.max_value = 100.0f;

    G_SetPlayerTechResearched(client, MAKEFOURCC('R','h','p','m'), 1);
    T_EQ(G_InventoryCapacity(footman), 2);
    T_ASSERT(!G_InventoryCanUseItems(footman));
    T_ASSERT(G_InventoryCanUseItems(hero));

    item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 32, 0);
    SP_SpawnItem(item); gi.LinkEntity(item);
    T_ASSERT(G_AddItemToSlot(footman, item, 0));
    T_EQ(G_ItemCharges(item), 1);
    T_ASSERT(!level.timeofday.false_time.active);

    {
        cstring_t command[] = { "inventory", "0" };
        G_ClientCommand(player, 2, command);
    }
    T_ASSERT(footman->inventory[0] == item);
    T_EQ(G_ItemCharges(item), 1);

    G_UseItem(footman, 0);

    T_ASSERT(footman->inventory[0] == item);
    T_EQ(G_ItemCharges(item), 1);
    T_ASSERT(!level.timeofday.false_time.active);
}

TEST(wc3_items, backpack_carrier_drops_items_on_death_but_hero_retains_them) {
    edict_t *player, *footman, *hero, *carried, *hero_item;
    gameClient_t *client;

    setup_test_world();
    player = &g_edicts[0]; client = player->client;
    footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    footman->s.player = client->ps.number;
    footman->health.value = footman->health.max_value = 100.0f;
    G_SetPlayerTechResearched(client, MAKEFOURCC('R','h','p','m'), 1);

    carried = alloc_test_unit(MAKEFOURCC('r','a','t','f'), 32, 0);
    SP_SpawnItem(carried); gi.LinkEntity(carried);
    T_ASSERT(G_AddItemToSlot(footman, carried, 0));
    unit_die(footman, NULL);
    T_NULL(footman->inventory[0]);
    T_NULL(carried->item.carrier);
    T_EQ(carried->item.inventory_slot, -1);
    T_ASSERT(carried->item.in_world);
    T_ASSERT(!(carried->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(carried->svflags & SVF_NOCLIENT));

    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 128, 0);
    hero->s.player = client->ps.number;
    hero->health.value = hero->health.max_value = 100.0f;
    hero_item = alloc_test_unit(MAKEFOURCC('r','a','t','f'), 160, 0);
    SP_SpawnItem(hero_item); gi.LinkEntity(hero_item);
    T_ASSERT(G_AddItemToSlot(hero, hero_item, 0));
    unit_die(hero, NULL);
    T_ASSERT(hero->inventory[0] == hero_item);
    T_ASSERT(hero_item->item.carrier == hero);
    T_ASSERT(!hero_item->item.in_world);
}

TEST(wc3_items, inventory_get_and_drop_flags_gate_orders_but_not_script_style_mutation) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y2;X1;K\"Agt0\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"0\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Agt0", .heroAbilList = "" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit, *item;
    vec2_t destination = { 64.0f, 0.0f };

    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    unit->data.UnitAbilities = &abilities;
    unit->health.value = unit->health.max_value = 100.0f;
    unit->movetype = MOVETYPE_STEP;
    unit->stand = unit_stand;
    unit_stand(unit);
    item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);

    T_EQ(G_InventoryCapacity(unit), 2);
    T_ASSERT(!G_InventoryCanGetItems(unit));
    T_ASSERT(!G_InventoryCanDropItems(unit));
    T_ASSERT(!G_OrderPickupItem(unit, item));

    /* UnitAddItem/UnitAddItemToSlot style script operations call the direct
     * mutation path and are intentionally not blocked by inv4. */
    T_ASSERT(G_PickupItem(unit, item));
    T_ASSERT(unit->inventory[0] == item);

    /* Player drop orders respect inv5, while UnitRemoveItem-style direct
     * removal still uses the authoritative direct drop primitive. */
    T_ASSERT(!G_OrderDropItemAt(unit, item, &destination));
    T_ASSERT(unit->inventory[0] == item);
    T_ASSERT(G_DropItem(unit, 0));
    T_NULL(unit->inventory[0]);
    T_ASSERT(item->item.in_world);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_items, inventory_panel_uses_local_player_race_not_selected_unit_race) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *player, *peasant; gameClient_t *client;

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
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *player, *peasant; gameClient_t *client;

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
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *player, *unit; gameClient_t *client;

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
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *player, *unit; gameClient_t *client;

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
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *player, *peasant, *inventory_unit;
    gameClient_t *client;

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
    edict_t *unit;
    edict_t *item;
    uint8_t count;

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

TEST(wc3_items, neutral_shop_purchases_authored_item_into_nearby_hero_inventory) {
    edict_t *player;
    gameClient_t *client;
    edict_t *hero;
    edict_t *shop;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100;
    hero = make_item_test_inventory_unit(100, 0);
    hero->s.player = 0;
    shop = make_item_test_shop(0, 0);

    T_ASSERT(G_FindShopPatron(client, shop) == hero);
    T_ASSERT(G_ShopPurchaseItem(player, shop, MAKEFOURCC('s','p','r','o')));
    T_NOT_NULL(hero->inventory[0]);
    T_EQ(hero->inventory[0]->class_id, MAKEFOURCC('s','p','r','o'));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 350);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 75);
}

TEST(wc3_items, enemy_item_shop_does_not_gain_neutral_shop_access) {
    edict_t *player;
    gameClient_t *client;
    edict_t *hero;
    edict_t *shop;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    hero = make_item_test_inventory_unit(100, 0);
    hero->s.player = 0;
    shop = make_item_test_shop(0, 0);
    shop->s.player = 1;

    T_ASSERT(!G_CanUseItemShop(client, shop));
    T_NULL(G_FindShopPatron(client, shop));
}

TEST(wc3_items, neutral_shop_rejects_purchase_without_nearby_inventory_unit) {
    edict_t *player;
    gameClient_t *client;
    edict_t *hero;
    edict_t *shop;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100;
    hero = make_item_test_inventory_unit(900, 0);
    hero->s.player = 0;
    shop = make_item_test_shop(0, 0);

    T_NULL(G_FindShopPatron(client, shop));
    T_ASSERT(!G_ShopPurchaseItem(player, shop, MAKEFOURCC('s','p','r','o')));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 500);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 100);
    T_NULL(hero->inventory[0]);
}

TEST(wc3_items, neutral_shop_stock_is_shared_and_replenishes_from_item_data) {
    edict_t *player;
    gameClient_t *client;
    edict_t *hero;
    edict_t *shop;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;
    hero = make_item_test_inventory_unit(100, 0);
    hero->s.player = 0;
    shop = make_item_test_shop(0, 0);

    T_ASSERT(G_ShopPurchaseItem(player, shop, MAKEFOURCC('s','p','r','o')));
    T_ASSERT(G_ShopPurchaseItem(player, shop, MAKEFOURCC('s','p','r','o')));
    T_ASSERT(!G_ShopPurchaseItem(player, shop, MAKEFOURCC('s','p','r','o')));
    level.time += 60000;
    T_ASSERT(G_ShopPurchaseItem(player, shop, MAKEFOURCC('s','p','r','o')));
}

TEST(wc3_items, neutral_item_shop_runtime_stock_override_is_immediate_and_replenishes) {
    static UnitAbilities_t abilities = { .abilList = "Apit,Asid", .heroAbilList = "" };
    edict_t *player;
    gameClient_t *client;
    edict_t *shop;
    gameCommandButton_t buttons[12];
    shopItemButtonsParams_t params;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;
    make_item_test_inventory_unit(100, 0)->s.player = 0;
    shop = make_item_test_shop(0, 0);
    shop->data.UnitAbilities = &abilities;
    params = (shopItemButtonsParams_t){ .client = client, .shop = shop, .buttons = buttons, .max_buttons = 12 };

    T_ASSERT(G_AddItemStock(shop, MAKEFOURCC('s','p','r','o'), 1, 1));
    T_EQ(G_GetShopButtons(&params), 1);
    T_EQ(shop->stock.items[0].current, 1);
    T_EQ(shop->stock.items[0].maximum, 1);
    T_ASSERT(!buttons[0].disabled);

    T_ASSERT(G_ShopPurchaseItem(player, shop, MAKEFOURCC('s','p','r','o')));
    T_EQ(shop->stock.items[0].current, 0);
    level.time += 60000;
    T_EQ(G_GetShopButtons(&params), 1);
    T_EQ(shop->stock.items[0].current, 1);
    T_ASSERT(!buttons[0].disabled);

    G_RemoveItemStock(shop, MAKEFOURCC('s','p','r','o'));
    T_EQ(G_GetShopButtons(&params), 0);
}

TEST(wc3_items, neutral_unit_shop_uses_non_inventory_patron_and_hires_immediately) {
    edict_t *player;
    gameClient_t *client;
    edict_t *patron;
    edict_t *shop;
    edict_t *hired = NULL;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;
    patron = make_unit_shop_patron(100, 0, 0);
    shop = make_unit_test_shop(0, 0);
    {
        edict_t *nonunit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20, 0);
        nonunit->s.player = 0;
        nonunit->collision = 8.0f;
        gi.LinkEntity(nonunit);
    }

    /* DataA1=137 is deliberately non-retail fixture data. DataB1=2 accepts
     * the non-inventory Peasant but not the closer non-unit edict. */
    T_FEQ(G_ShopActivationRadius(shop), 137.0f, 0.001f);
    T_EQ(G_FindUnitShopPatron(client, shop), patron);
    level.time += 3000;
    T_ASSERT(G_ShopPurchaseUnit(player, shop, MAKEFOURCC('n','m','e','r')));
    FILTER_EDICTS(unit, unit->inuse && unit != shop && unit != patron &&
                  unit->class_id == MAKEFOURCC('n','m','e','r') && unit->s.player == 0) {
        hired = unit;
        break;
    }
    T_NOT_NULL(hired);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 350);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 75);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 2);
    T_EQ(shop->stock.unit_count, 1);
    T_EQ(shop->stock.units[0].current, 0);
}

TEST(wc3_items, neutral_unit_shop_runtime_stock_override_is_immediate_and_replenishes) {
    edict_t *player;
    gameClient_t *client;
    edict_t *shop;
    gameCommandButton_t buttons[12];
    shopItemButtonsParams_t params;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 20;
    make_unit_shop_patron(100, 0, 0);
    shop = make_unit_test_shop(0, 0);
    params = (shopItemButtonsParams_t){ .client = client, .shop = shop, .buttons = buttons, .max_buttons = 12 };

    /* Campaign stock natives override the authored initial delay immediately. */
    T_ASSERT(G_AddUnitStock(shop, MAKEFOURCC('n','m','e','r'), 1, 1));
    T_EQ(G_GetShopButtons(&params), 1);
    T_EQ(shop->stock.units[0].current, 1);
    T_EQ(shop->stock.units[0].maximum, 1);
    T_ASSERT(!buttons[0].disabled);

    T_ASSERT(G_ShopPurchaseUnit(player, shop, MAKEFOURCC('n','m','e','r')));
    T_EQ(shop->stock.units[0].current, 0);
    level.time += 5000;
    T_EQ(G_GetShopButtons(&params), 1);
    T_EQ(shop->stock.units[0].current, 1);
    T_ASSERT(!buttons[0].disabled);

    G_RemoveUnitStock(shop, MAKEFOURCC('n','m','e','r'));
    T_EQ(G_GetShopButtons(&params), 0);
}

TEST(wc3_items, neutral_unit_shop_stock_delay_and_replenishment_are_shared) {
    edict_t *player;
    gameClient_t *client;
    edict_t *shop;
    gameCommandButton_t buttons[12];
    shopItemButtonsParams_t params;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 20;
    make_unit_shop_patron(100, 0, 0);
    shop = make_unit_test_shop(0, 0);
    params = (shopItemButtonsParams_t){ .client = client, .shop = shop, .buttons = buttons, .max_buttons = 12 };

    T_EQ(G_GetShopButtons(&params), 1);
    T_EQ(shop->stock.units[0].current, 0);
    T_ASSERT(buttons[0].disabled);
    level.time += 3000;
    T_EQ(G_GetShopButtons(&params), 1);
    T_EQ(shop->stock.units[0].current, 1);
    T_ASSERT(!buttons[0].disabled);
    level.time += 5000;
    T_EQ(G_GetShopButtons(&params), 1);
    T_EQ(shop->stock.units[0].current, 2);
    level.time += 5000;
    T_EQ(G_GetShopButtons(&params), 1);
    T_EQ(shop->stock.units[0].current, 2);
}

TEST(wc3_items, neutral_unit_shop_food_failure_preserves_stock_and_resources) {
    edict_t *player;
    gameClient_t *client;
    edict_t *shop;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 1;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;
    make_unit_shop_patron(100, 0, 0);
    shop = make_unit_test_shop(0, 0);
    level.time += 3000;

    T_ASSERT(!G_ShopPurchaseUnit(player, shop, MAKEFOURCC('n','m','e','r')));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 500);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 0);
    T_EQ(shop->stock.unit_count, 1);
    T_EQ(shop->stock.units[0].current, 1);
}

TEST(wc3_items, neutral_shop_pawns_pawnable_item_at_misc_rate) {
    edict_t *player;
    gameClient_t *client;
    edict_t *hero;
    edict_t *shop;
    edict_t *item;

    setup_test_world();
    player = &g_edicts[0];
    client = player->client;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100;
    hero = make_item_test_inventory_unit(100, 0);
    hero->s.player = 0;
    shop = make_item_test_shop(0, 0);

    T_ASSERT(G_ShopPurchaseItem(player, shop, MAKEFOURCC('s','p','r','o')));
    item = hero->inventory[0];
    T_NOT_NULL(item);
    T_ASSERT(G_ShopPawnItem(&(shopPawnItemParams_t){
        .clent = player, .shop = shop, .carrier = hero, .item = item }));
    T_NULL(hero->inventory[0]);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 425);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 88);
}

TEST(wc3_items, carried_charge_change_refreshes_inventory_and_same_value_is_noop) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *player;
    gameClient_t *client;
    edict_t *unit;
    edict_t *item;

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
    edict_t *item;

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
    edict_t *item;

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
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent;
    gameClient_t *client;
    edict_t *unit;
    edict_t *item;
    float base_armor;
    bool found_buff = false;
    cstring_t command[] = { "inventory", "0" };

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
    T_ASSERT(item->item.pending_use_removal);
    G_RunEvents();
    G_RunConsumedItemFrees();
    T_ASSERT(!item->inuse);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_items, drop_preserves_item_charges) {
    setup_test_world();
    edict_t *unit = make_item_test_inventory_unit(128, 256);
    edict_t *item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 64, 0);

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

TEST(wc3_items, point_target_item_out_of_range_click_does_not_approach_or_consume_charge) {
    static char const ability_slk[] =
        "ID;PWXL;N;EBB;Y4;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
        "C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
        "C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"UnitID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X10;K\"6\"\nC;Y2;X12;K\"1\"\n"
        "C;Y3;X1;K\"AIpm\"\nC;Y3;X2;K\"AIpm\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"ground\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\n"
        "C;Y3;X7;K\"96\"\nC;Y3;X8;K\"0\"\nC;Y3;X14;K\"hfoo\"\n"
        "C;Y4;X1;K\"AOfs\"\nC;Y4;X2;K\"AOfs\"\nC;Y4;X3;K\"1\"\n"
        "C;Y4;X4;K\"ground,enemy\"\nC;Y4;X7;K\"500\"\nC;Y4;X8;K\"1\"\n"
        "C;Y4;X16;K\"128\"\nE\n";
    static UnitAbilities_t abilities = { .abilList = "AInv", .heroAbilList = "" };
    ItemData_t item_data = { .abilList = "AIpm", .uses = 2, .perishable = false };
    slkTestData_t *rows, *old;
    edict_t *player, *hero, *item, *mine = NULL;
    cstring_t far_click[] = { "point", "700", "0" };
    cstring_t valid_click[] = { "point", "64", "0" };
    cstring_t far_sight[] = { "button", "AOfs" };

    setup_test_world();
    rows = parse_slk_string(ability_slk); old = G_SetSLKRows("AbilityData", rows);
    player = &g_edicts[0]; player->client->ps.number = 0;
    hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    hero->data.UnitAbilities = &abilities; hero->s.player = 0; hero->svflags |= SVF_MONSTER;
    hero->targtype = TARG_GROUND; hero->health.value = hero->health.max_value = 100;
    hero->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','f','s'), .level = 1);
    item = make_item_test_world_item(MAKEFOURCC('g','o','b','m'), 0, 0);
    item->data.ItemData = &item_data; item->item.charges = 2; item->spawn_time = 1234;
    T_ASSERT(G_AddItemToSlot(hero, item, 0));
    G_SelectEntity(player->client, hero);

    G_UseItem(hero, 0);
    T_NOT_NULL(player->client->menu.on_location_selected);
    T_EQ(player->client->menu.ability_item, item);
    T_EQ(player->client->menu.ability_item_spawn_time, item->spawn_time);
    T_EQ(G_ItemCharges(item), 2);
    G_ClientCommand(player, 3, far_click);
    T_NOT_NULL(player->client->menu.on_location_selected);
    T_NULL(hero->goalentity); /* Point items reject out-of-range clicks instead of walking into range. */
    T_EQ(player->client->menu.ability_item, item);
    T_EQ(G_ItemCharges(item), 2);
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == MAKEFOURCC('h','f','o','o') && ent->owner == hero) {
        mine = ent; break;
    }
    T_NULL(mine);

    G_ClientCommand(player, 2, far_sight);
    T_NULL(player->client->menu.ability_item);
    T_EQ(player->client->menu.ability_item_spawn_time, 0);
    G_ClientCommand(player, 3, valid_click);
    T_EQ(G_ItemCharges(item), 2);

    G_UseItem(hero, 0);
    G_ClientCommand(player, 3, valid_click);
    T_NULL(player->client->menu.on_location_selected);
    T_NULL(player->client->menu.ability_item);
    T_EQ(player->client->menu.ability_item_spawn_time, 0);
    T_EQ(G_ItemCharges(item), 1);
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == MAKEFOURCC('h','f','o','o') && ent->owner == hero) {
        mine = ent; break;
    }
    T_NOT_NULL(mine);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_items, pickup_event_detects_arthas_urn) {
    static UnitAbilities_t abilities = { .abilList = "AInv", .heroAbilList = "" };
    edict_t *arthas = NULL;
    edict_t *urn;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit arthas = null\n"
        "  quest urnQuest = null\n"
        "endglobals\n"
        "function on_pickup takes nothing returns nothing\n"
        "  if GetItemTypeId(GetManipulatedItem()) == 'ktrm' and GetManipulatingUnit() == arthas then\n"
        "    call QuestSetCompleted(urnQuest, true)\n"
        "  endif\n"
        "endfunction\n"
        "function verify_pickup takes nothing returns nothing\n"
        "  call BJassAssert(IsQuestCompleted(urnQuest), \"Arthas urn pickup did not complete the quest\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set arthas = CreateUnit(Player(3), 'Hpal', 64.0, 64.0, 0.0)\n"
        "  set urnQuest = CreateQuest()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(3), EVENT_PLAYER_UNIT_PICKUP_ITEM, null)\n"
        "  call TriggerAddAction(t, function on_pickup)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('H','p','a','l')) {
            arthas = g_edicts + i;
            break;
        }
    }
    T_NOT_NULL(arthas);
    arthas->data.UnitAbilities = &abilities;
    arthas->s.model = 1;
    arthas->movetype = MOVETYPE_STEP;
    arthas->collision = 16.0f;
    arthas->health.value = arthas->health.max_value = 100.0f;
    urn = make_item_test_world_item(MAKEFOURCC('k','t','r','m'), 64, 64);
    T_ASSERT(G_PickupItem(arthas, urn));
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verify_pickup", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

TEST(wc3_items, jass_set_item_drop_id_stores_unit_rawcode) {
    edict_t *item = NULL;
    edict_t *unit;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local item i = CreateItem('spro', 64.0, 64.0)\n"
        "  call SetItemDropID(i, 'hpea')\n"
        "  call BJassAssert(GetItemDropID(i) == 'hpea', \"GetItemDropID did not read the assigned rawcode\")\n"
        "  call SetItemDropID(i, 'hfoo')\n"
        "  call BJassAssert(GetItemDropID(i) == 'hfoo', \"GetItemDropID did not observe the overwrite\")\n"
        "  call SetItemDropID(null, 'hpea')\n"
        "  call BJassAssert(GetItemDropID(null) == 0, \"GetItemDropID(null) was not zero\")\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('s','p','r','o')) {
            item = g_edicts + i;
            break;
        }
    }
    T_NOT_NULL(item);
    T_EQ(item->item.drop_id, MAKEFOURCC('h','f','o','o'));
    unit = make_item_test_inventory_unit(64, 64);
    T_ASSERT(G_PickupItem(unit, item));
    T_EQ(unit->inventory[0], item);
    T_ASSERT(G_DropItem(unit, 0));
    T_EQ(item->item.drop_id, MAKEFOURCC('h','f','o','o'));
}

TEST(wc3_items, jass_set_item_drop_id_round_trips_save) {
    cstring_t path = "/tmp/openwarcraft3-wc3-item-drop-id.bin";
    edict_t *item = NULL;
    uint32_t index;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  call SetItemDropID(CreateItem('spro', 64.0, 64.0), 'hfoo')\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('s','p','r','o')) {
            item = g_edicts + i;
            break;
        }
    }
    T_NOT_NULL(item);
    T_EQ(item->item.drop_id, MAKEFOURCC('h','f','o','o'));
    index = item->s.number;
    T_ASSERT(WriteGame(path));
    item->item.drop_id = 0;
    T_ASSERT(ReadGame(path));
    T_EQ(g_edicts[index].item.drop_id, MAKEFOURCC('h','f','o','o'));
    remove(path);
}

TEST(wc3_items, drop_restores_same_item_to_world) {
    setup_test_world();
    edict_t *unit = make_item_test_inventory_unit(128, 256);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 64, 0);

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
    edict_t *unit = make_item_test_inventory_unit(0, 0);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), ITEM_PICKUP_RANGE - 1, 0);

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
    void (*old_unicast)(edict_t *) = gi.unicast;
    UnitAbilities_t no_inventory = { .abilList = "", .heroAbilList = "AHhb" };
    edict_t *clent;
    gameClient_t *client;
    edict_t *footman;
    edict_t *hero;
    edict_t *item;
    char item_number[16];
    cstring_t command[] = { "smart", item_number };

    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->fileFormat = 24;
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
    edict_t *unit = make_item_test_inventory_unit(0, 0);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), ITEM_PICKUP_RANGE + 100, 0);

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

TEST(wc3_items, point_drop_waits_for_simulation_tick) {
    setup_test_world();
    edict_t *unit = make_item_test_inventory_unit(0, 0);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);
    vec2_t destination = { ITEM_DROP_RANGE - 1.0f, 0.0f };

    T_ASSERT(G_PickupItem(unit, item));
    T_ASSERT(G_OrderDropItemAt(unit, item, &destination));
    T_ASSERT(unit->inventory[0] == item);
    T_ASSERT(!item->item.in_world);

    unit->currentmove->think(unit);

    T_NULL(unit->inventory[0]);
    T_ASSERT(item->item.in_world);
    T_FEQ(item->s.origin2.x, destination.x, 0.001f);
    T_FEQ(item->s.origin2.y, destination.y, 0.001f);
    T_NULL(unit->item_drop);
    T_NULL(unit->goalentity);
}

TEST(wc3_items, distant_point_drop_moves_before_releasing_item) {
    setup_test_world();
    edict_t *unit = make_item_test_inventory_unit(0, 0);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);
    vec2_t destination = { ITEM_DROP_RANGE + 200.0f, 0.0f };

    T_ASSERT(G_PickupItem(unit, item));
    T_ASSERT(G_OrderDropItemAt(unit, item, &destination));
    unit->currentmove->think(unit);

    T_ASSERT(unit->inventory[0] == item);
    T_ASSERT(!item->item.in_world);
    T_ASSERT(unit->s.origin2.x > 0.0f);
    T_ASSERT(unit->item_drop == item);
    T_NOT_NULL(unit->goalentity);
}

TEST(wc3_items, point_drop_revalidates_carried_item) {
    setup_test_world();
    edict_t *unit = make_item_test_inventory_unit(0, 0);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);
    vec2_t destination = { ITEM_DROP_RANGE + 200.0f, 0.0f };

    T_ASSERT(G_PickupItem(unit, item));
    T_ASSERT(G_OrderDropItemAt(unit, item, &destination));
    item->item.carrier = NULL;
    unit->currentmove->think(unit);

    T_NULL(unit->item_drop);
    T_NULL(unit->goalentity);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_items, cancel_command_clears_point_drop_target_mode) {
    gameClient_t *client;
    edict_t *clent;
    edict_t *unit;
    edict_t *item;
    cstring_t command[] = { "cancel" };

    setup_test_world();
    clent = &g_edicts[0];
    client = clent->client;
    unit = make_item_test_inventory_unit(0, 0);
    item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);
    T_ASSERT(G_PickupItem(unit, item));
    client->menu.dragged_item = item;
    client->menu.on_entity_selected = item_test_target_callback;
    client->menu.on_location_selected = item_test_location_callback;

    G_ClientCommand(clent, 1, command);

    T_NULL(client->menu.dragged_item);
    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);
    T_ASSERT(unit->inventory[0] == item);
}

TEST(wc3_items, removing_carried_item_clears_slot) {
    setup_test_world();
    edict_t *unit = make_item_test_inventory_unit(0, 0);
    edict_t *item = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 32, 0);

    T_ASSERT(G_PickupItem(unit, item));
    G_RemoveItem(item);

    T_NULL(unit->inventory[0]);
    T_ASSERT(!item->inuse);
}

TEST(wc3_items, drop_at_rejects_slot_beyond_capacity) {
    /* G_DropItemAt must use G_InventoryCapacity, not MAX_INVENTORY, so that a
     * slot index in-range for the hard array but beyond the unit's authored
     * capacity is rejected cleanly instead of silently failing on item guard. */
    setup_test_world();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('H','0','0','1'), 0, 0);
    unit->s.model = 1;
    unit->s.player = PLAYER_NEUTRAL_PASSIVE;
    unit->movetype = MOVETYPE_STEP;
    unit->health.value = 100.0f;
    unit->health.max_value = 100.0f;
    unit->stand = unit_stand;
    unit_stand(unit);
    gi.LinkEntity(unit);
    T_EQ(G_InventoryCapacity(unit), 2);

    edict_t *item0 = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 64, 0);
    edict_t *item1 = make_item_test_world_item(MAKEFOURCC('r','a','t','f'), 64, 32);
    T_ASSERT(G_AddItemToSlot(unit, item0, 0));
    T_ASSERT(G_AddItemToSlot(unit, item1, 1));

    vec2_t pos = MAKE(vec2_t, 0, 0);
    T_ASSERT(!G_DropItemAt(unit, 4, &pos));
    T_ASSERT(unit->inventory[0] == item0);
    T_ASSERT(unit->inventory[1] == item1);
}


TEST(wc3_items, soul_gem_abilities_are_registered_for_targeted_item_flow) {
    ability_t const *trap = FindAbilityForCommand("AIso");
    ability_t const *trapped = FindAbilityForCommand("Asou");

    T_NOT_NULL(trap);
    T_ASSERT(trap->proc == CAbilitySoulTrap);
    T_ASSERT(trap->flags & AB_SPELL);
    T_EQ(trap->target_type, SPELL_TARGET_UNIT);
    T_NOT_NULL(trapped);
    T_ASSERT(trapped->proc == CAbilitySoulTrapped);
    T_ASSERT(trapped->flags & AB_PASSIVE);
}

TEST(wc3_items, soul_gem_targets_grom_after_death_trigger_revives_him) {
    gameClient_t *client;
    edict_t *clent, *carrier, *grom, *gem;
    char number[16];
    cstring_t select[] = { "select", number };

    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] = level.alliances[1][0] = 0;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit grom = null\n"
        "  location destination = null\n"
        "endglobals\n"
        "function grom_died takes nothing returns boolean\n"
        "  return GetDyingUnit() == grom\n"
        "endfunction\n"
        "function revive_grom takes nothing returns nothing\n"
        "  call BJassAssert(ReviveHeroLoc(grom, destination, false), \"death trigger failed to revive Grom\")\n"
        "  call PauseUnit(grom, true)\n"
        "endfunction\n"
        "function gem_used takes nothing returns nothing\n"
        "  call TriggerSleepAction(0.10)\n"
        "  call UnitAddItemById(GetManipulatingUnit(), 'soul')\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger died = CreateTrigger()\n"
        "  local trigger used = CreateTrigger()\n"
        "  set grom = CreateUnit(Player(1), 'Hpal', 256.0, 64.0, 0.0)\n"
        "  set destination = Location(96.0, 64.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(died, Player(1), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddCondition(died, Condition(function grom_died))\n"
        "  call TriggerAddAction(died, function revive_grom)\n"
        "  call TriggerRegisterPlayerUnitEvent(used, Player(0), EVENT_PLAYER_UNIT_USE_ITEM, null)\n"
        "  call TriggerAddAction(used, function gem_used)\n"
        "endfunction\n"));

    carrier = make_item_test_inventory_unit(64, 64);
    carrier->s.player = 0;
    carrier->svflags |= SVF_MONSTER;
    grom = NULL;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('H','p','a','l') && ent->s.player == 1) {
            grom = ent;
            break;
        }
    }
    T_NOT_NULL(grom);
    if (!grom) return;
    grom->targtype = TARG_GROUND; /* The Orc08 Opgh map unit is a ground Hero. */
    T_EQ((int)grom->s.origin2.x, 256);
    G_SetHealth(grom, 0.0f);
    unit_die(grom, NULL);
    G_RunEvents();
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(!M_IsDead(grom));
    T_ASSERT(!(grom->svflags & SVF_DEADMONSTER));
    T_FEQ(grom->s.origin2.x, 96.0f, 0.001f);
    T_FEQ(grom->s.origin2.y, 64.0f, 0.001f);
    T_ASSERT(grom->paused);
    T_ASSERT(G_UnitIsWorldActive(grom));
    T_ASSERT(grom->svflags & SVF_MONSTER);
    T_EQ(grom->targtype, TARG_GROUND);
    T_ASSERT(S_SpellAllowsTarget(MAKEFOURCC('A','I','s','o'), carrier, grom));

    gem = make_item_test_world_item(MAKEFOURCC('g','s','o','u'), 64, 64);
    gem->data.ItemData = G_ItemData(MAKEFOURCC('g','s','o','u'));
    gem->item.charges = 1;
    T_ASSERT(G_AddItemToSlot(carrier, gem, 0));
    clent = G_GetPlayerEntityByNumber(0);
    client = clent->client;
    G_SelectEntity(client, carrier);
    G_UseItem(carrier, 0);
    T_NOT_NULL(client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", (unsigned)grom->s.number);
    G_ClientCommand(clent, 2, select);

    T_NULL(client->menu.on_entity_selected);
    T_ASSERT(grom->aiflags & AI_SOUL_TRAPPED);
    T_ASSERT(!M_IsDead(grom));
    T_ASSERT(grom->s.renderfx & RF_HIDDEN);
    T_ASSERT(carrier->soul_trap_head == grom);
    T_ASSERT(grom->soul_trap_carrier == carrier);
    T_ASSERT(gem->item.pending_use_removal);
    G_RunEvents(); jass_runevents(level.vm); G_RunConsumedItemFrees();
    level.time += 100; jass_runevents(level.vm); G_RunConsumedItemFrees();
    T_ASSERT(carrier->inventory[0] && carrier->inventory[0]->class_id == MAKEFOURCC('s','o','u','l'));
}

TEST(wc3_items, soul_gem_approach_is_cancelled_if_grom_dies_before_revival_dispatch) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"levels\"\nC;Y1;X8;K\"DataA1\"\n"
        "C;Y1;X9;K\"DataB1\"\nC;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"DataE1\"\n"
        "C;Y2;X1;K\"AIso\"\nC;Y2;X2;K\"AIso\"\nC;Y2;X3;K\"enemy,ground,hero\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"96\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AInv\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X8;K\"6\"\nC;Y3;X9;K\"0\"\n"
        "C;Y3;X10;K\"1\"\nC;Y3;X11;K\"1\"\nC;Y3;X12;K\"1\"\n"
        "C;Y4;X1;K\"Asou\"\nC;Y4;X2;K\"Asou\"\nC;Y4;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    gameClient_t *client;
    edict_t *clent, *carrier, *grom, *gem, *thinker;
    uint32_t thinker_slot;
    char number[16];
    cstring_t select[] = { "select", number };

    old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] = level.alliances[1][0] = 0;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit grom = null\n"
        "  location destination = null\n"
        "endglobals\n"
        "function grom_died takes nothing returns boolean\n"
        "  return GetDyingUnit() == grom\n"
        "endfunction\n"
        "function revive_grom takes nothing returns nothing\n"
        "  call BJassAssert(ReviveHeroLoc(grom, destination, false), \"death trigger failed to revive Grom\")\n"
        "  call PauseUnit(grom, true)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger died = CreateTrigger()\n"
        "  set grom = CreateUnit(Player(1), 'Hpal', 1400.0, 64.0, 0.0)\n"
        "  set destination = Location(1400.0, 64.0)\n"
        "  call TriggerRegisterPlayerUnitEvent(died, Player(1), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddCondition(died, Condition(function grom_died))\n"
        "  call TriggerAddAction(died, function revive_grom)\n"
        "endfunction\n"));

    carrier = make_item_test_inventory_unit(64, 64);
    carrier->s.player = 0;
    carrier->svflags |= SVF_MONSTER;
    grom = NULL;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('H','p','a','l') && ent->s.player == 1) {
            grom = ent;
            break;
        }
    }
    T_NOT_NULL(grom);
    if (!grom) { G_SetSLKRows("AbilityData", old); free_slk_rows(rows); return; }
    grom->targtype = TARG_GROUND;
    gem = make_item_test_world_item(MAKEFOURCC('g','s','o','u'), 64, 64);
    gem->data.ItemData = G_ItemData(MAKEFOURCC('g','s','o','u'));
    gem->item.charges = 1;
    T_ASSERT(G_AddItemToSlot(carrier, gem, 0));

    clent = G_GetPlayerEntityByNumber(0);
    client = clent->client;
    G_SelectEntity(client, carrier);
    G_UseItem(carrier, 0);
    T_NOT_NULL(client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", (unsigned)grom->s.number);
    thinker_slot = globals.num_edicts;
    G_ClientCommand(clent, 2, select);
    thinker = &globals.edicts[thinker_slot];
    T_ASSERT(thinker->inuse && thinker->think == S_SpellUnitTargetApproachThink);
    T_ASSERT(carrier->goalentity == grom);
    T_FEQ(S_SpellRange(MAKEFOURCC('A','I','s','o'), 1), 96.0f, 0.001f);
    T_ASSERT(Vector2_distance(&carrier->s.origin2, &grom->s.origin2) > S_SpellRange(MAKEFOURCC('A','I','s','o'), 1));

    G_SetHealth(grom, 0.0f);
    unit_die(grom, NULL);
    T_ASSERT(M_IsDead(grom));
    /* Model a frame where the approach check runs before the queued death-trigger revival. */
    thinker->think(thinker);
    T_ASSERT(!thinker->inuse);
    T_ASSERT(!(grom->aiflags & AI_SOUL_TRAPPED));
    T_ASSERT(carrier->inventory[0] == gem);
    T_ASSERT(!gem->item.pending_use_removal);

    G_RunEvents();
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(!M_IsDead(grom));
    T_ASSERT(grom->paused);
    T_ASSERT(G_UnitIsWorldActive(grom));
    T_ASSERT(!(grom->aiflags & AI_SOUL_TRAPPED));
    T_ASSERT(S_SpellAllowsTarget(MAKEFOURCC('A','I','s','o'), carrier, grom));
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_items, soul_gem_target_capture_keeps_live_hero_until_carrier_death) {
    gameClient_t *client;
    edict_t *clent, *carrier, *target, *target2, *gem, *gem2, *filled, *filled2, *existing_soul;
    uint32_t soul_count;
    char number[16];
    cstring_t select[] = { "select", number };

    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] = level.alliances[1][0] = 0;
    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer targetDeaths = 0\n"
        "endglobals\n"
        "function soul_gem_used takes nothing returns nothing\n"
        "  local item filled = null\n"
        "  call TriggerSleepAction(0.10)\n"
        "  set filled = UnitAddItemById(GetManipulatingUnit(), 'soul')\n"
        "  call SetItemDroppable(filled, false)\n"
        "endfunction\n"
        "function target_died takes nothing returns nothing\n"
        "  set targetDeaths = targetDeaths + 1\n"
        "endfunction\n"
        "function verify_no_target_death takes nothing returns nothing\n"
        "  call BJassAssert(targetDeaths == 0, \"Soul Trap fired a unit death event\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger used = CreateTrigger()\n"
        "  local trigger died = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(used, Player(0), EVENT_PLAYER_UNIT_USE_ITEM, null)\n"
        "  call TriggerAddAction(used, function soul_gem_used)\n"
        "  call TriggerRegisterPlayerUnitEvent(died, Player(1), EVENT_PLAYER_UNIT_DEATH, null)\n"
        "  call TriggerAddAction(died, function target_died)\n"
        "endfunction\n"));
    G_FowInit(); G_FowConnectPlayer(0); G_FowConnectPlayer(1); G_FowConnectPlayer(2);
    carrier = make_item_test_inventory_unit(64, 64);
    carrier->s.player = 0;
    carrier->svflags |= SVF_MONSTER;
    existing_soul = make_item_test_world_item(MAKEFOURCC('s','o','u','l'), 64, 64);
    existing_soul->data.ItemData = G_ItemData(MAKEFOURCC('s','o','u','l'));
    T_ASSERT(G_AddItemToSlot(carrier, existing_soul, 0));
    target = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 96, 64);
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->s.model = 1;
    target->targtype = TARG_GROUND;
    target->movetype = MOVETYPE_STEP;
    target->stand = unit_stand;
    unit_stand(target);
    gi.LinkEntity(target);
    gem = make_item_test_world_item(MAKEFOURCC('g','s','o','u'), 64, 64);
    gem->data.ItemData = G_ItemData(MAKEFOURCC('g','s','o','u'));
    gem->item.charges = 1;
    T_ASSERT(G_AddItemToSlot(carrier, gem, 1));

    clent = G_GetPlayerEntityByNumber(0);
    client = clent->client;
    G_SelectEntity(client, carrier);
    G_UseItem(carrier, 1);
    T_NOT_NULL(client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", (unsigned)target->s.number);
    G_ClientCommand(clent, 2, select);

    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.ability_item);
    T_EQ(client->menu.ability_item_spawn_time, 0);
    T_ASSERT(!M_IsDead(target));
    T_ASSERT(target->s.renderfx & RF_HIDDEN);
    T_ASSERT(carrier->inventory[0] == existing_soul);
    T_ASSERT(gem->item.pending_use_removal);
    G_FowUpdate();
    T_ASSERT(G_FowPlayerCanSeeEntity(1, carrier));
    T_ASSERT(!G_FowPlayerCanSeeEntity(2, carrier));
    T_EQ(level.fow.players[1].visible[G_FowWorldToCellY(carrier->s.origin2.y) * level.fow.width +
                                      G_FowWorldToCellX(carrier->s.origin2.x)], 0);
    G_RunEvents(); jass_runevents(level.vm); G_RunConsumedItemFrees();
    T_ASSERT(gem->inuse && gem->item.pending_use_removal);
    T_ASSERT(carrier->inventory[0] == existing_soul);
    level.time += 100; jass_runevents(level.vm); G_RunConsumedItemFrees();
    jass_callbyname(level.vm, "verify_no_target_death", true); jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    filled = carrier->inventory[1];
    T_ASSERT(filled && filled->class_id == MAKEFOURCC('s','o','u','l'));
    soul_count = 0;
    FOR_LOOP(i, G_InventoryCapacity(carrier))
        if (carrier->inventory[i] && carrier->inventory[i]->class_id == MAKEFOURCC('s','o','u','l')) soul_count++;
    T_EQ(soul_count, 2);
    T_ASSERT(!G_ItemDroppable(filled));
    T_NULL(existing_soul->item.soul_target);
    T_ASSERT(target->soul_trap_item != existing_soul);
    T_ASSERT(target->soul_trap_item == filled);
    T_ASSERT(filled->item.soul_target == target);
    T_EQ(carrier->forced_visibility_count[1], 1);
    T_ASSERT(!G_DropItemAtScripted(carrier, 1, &carrier->s.origin2));
    target->s.player = 2; /* Forced visibility remains paired with capture-time owner. */

    target2 = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 128, 64);
    target2->s.player = 1; target2->svflags |= SVF_MONSTER; target2->s.model = 1;
    target2->targtype = TARG_GROUND; target2->movetype = MOVETYPE_STEP;
    target2->stand = unit_stand; unit_stand(target2); gi.LinkEntity(target2);
    gem2 = make_item_test_world_item(MAKEFOURCC('g','s','o','u'), 64, 64);
    gem2->data.ItemData = G_ItemData(MAKEFOURCC('g','s','o','u'));
    gem2->item.charges = 1;
    T_ASSERT(G_AddItemToSlot(carrier, gem2, 2));
    G_UseItem(carrier, 2);
    T_NOT_NULL(client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", (unsigned)target2->s.number);
    G_ClientCommand(clent, 2, select);
    T_NULL(client->menu.on_entity_selected);
    T_ASSERT(target2->aiflags & AI_SOUL_TRAPPED);
    G_RunEvents(); jass_runevents(level.vm); G_RunConsumedItemFrees();
    T_ASSERT(gem2->inuse && gem2->item.pending_use_removal);
    T_NULL(carrier->inventory[2]);
    level.time += 100; jass_runevents(level.vm); G_RunConsumedItemFrees();
    jass_callbyname(level.vm, "verify_no_target_death", true); jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    filled2 = carrier->inventory[2];
    T_ASSERT(filled2 && filled2->class_id == MAKEFOURCC('s','o','u','l'));
    soul_count = 0;
    FOR_LOOP(i, G_InventoryCapacity(carrier))
        if (carrier->inventory[i] && carrier->inventory[i]->class_id == MAKEFOURCC('s','o','u','l')) soul_count++;
    T_EQ(soul_count, 3);
    T_ASSERT(filled2 != filled);
    T_ASSERT(target2->soul_trap_item == filled2);
    T_ASSERT(filled2->item.soul_target == target2);
    T_EQ(carrier->forced_visibility_count[1], 2);

    carrier->s.origin2 = MAKE(vec2_t, 192, 224);
    carrier->s.origin.x = 192;
    carrier->s.origin.y = 224;
    T_ASSERT(G_FowPlayerCanSeeEntity(1, carrier));
    G_SetHealth(carrier, 0.0f);
    unit_die(carrier, NULL);
    T_ASSERT(!M_IsDead(target));
    T_ASSERT(!(target->s.renderfx & RF_HIDDEN));
    T_FEQ(target->s.origin2.x, 192.0f, 0.001f);
    T_FEQ(target->s.origin2.y, 224.0f, 0.001f);
    T_ASSERT(!M_IsDead(target2));
    T_ASSERT(!(target2->s.renderfx & RF_HIDDEN));
    T_FEQ(target2->s.origin2.x, 192.0f, 0.001f);
    T_FEQ(target2->s.origin2.y, 224.0f, 0.001f);
    T_ASSERT(!filled->inuse);
    T_ASSERT(!filled2->inuse);
    T_EQ(carrier->forced_visibility_count[1], 0);
    T_EQ(carrier->forced_visibility_count[2], 0);
    T_ASSERT(!G_UnitIsForcedVisibleToPlayer(carrier, 1));
    G_FowShutdown();
}

TEST(wc3_items, soul_gem_rejects_nonhero_from_authored_target_mask) {
    gameClient_t *client;
    edict_t *clent, *carrier, *target, *gem;
    char number[16];
    cstring_t select[] = { "select", number };

    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] = level.alliances[1][0] = 0;
    carrier = make_item_test_inventory_unit(64, 64);
    carrier->s.player = 0;
    carrier->svflags |= SVF_MONSTER;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 64);
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->s.model = 1;
    target->targtype = TARG_GROUND;
    gem = make_item_test_world_item(MAKEFOURCC('g','s','o','u'), 64, 64);
    gem->data.ItemData = G_ItemData(MAKEFOURCC('g','s','o','u'));
    gem->item.charges = 1;
    T_ASSERT(G_AddItemToSlot(carrier, gem, 0));

    clent = G_GetPlayerEntityByNumber(0);
    client = clent->client;
    G_SelectEntity(client, carrier);
    G_UseItem(carrier, 0);
    T_NOT_NULL(client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", (unsigned)target->s.number);
    G_ClientCommand(clent, 2, select);
    T_NOT_NULL(client->menu.on_entity_selected);
    T_ASSERT(carrier->inventory[0] == gem);
    T_EQ(gem->item.charges, 1);
    T_ASSERT(!target->soul_trap_carrier);
    T_ASSERT(!(target->s.renderfx & RF_HIDDEN));
}

TEST(wc3_items, soul_trap_remove_target_cleans_bound_item) {
    edict_t *carrier, *target, *filled;
    uint32_t const asou = MAKEFOURCC('A','s','o','u');

    setup_test_world();
    carrier = make_item_test_inventory_unit(64, 64);
    target = make_item_test_inventory_unit(96, 64);
    carrier->s.player = 0;
    target->s.player = 1;
    T_ASSERT(G_ActorAddSkill(carrier, asou));
    T_ASSERT(G_ActorAddSkill(target, asou));
    carrier->soul_possession_added = true;
    filled = make_item_test_world_item(MAKEFOURCC('s','o','u','l'), 64, 64);
    filled->data.ItemData = G_ItemData(MAKEFOURCC('s','o','u','l'));
    T_ASSERT(G_AddItemToSlot(carrier, filled, 0));

    carrier->soul_trap_head = target;
    carrier->soul_trap_head_spawn_time = target->spawn_time;
    target->soul_trap_carrier = carrier;
    target->soul_trap_carrier_spawn_time = carrier->spawn_time;
    target->soul_trap_viewer = target->s.player;
    G_AddUnitForcedVisibility(carrier, target->s.player);
    target->soul_trap_item = filled;
    target->soul_trap_item_spawn_time = filled->spawn_time;
    target->aiflags |= AI_SOUL_TRAPPED;
    target->s.renderfx |= RF_HIDDEN;
    target->svflags |= SVF_NOCLIENT;
    target->s.flags |= EF_NOT_SELECTABLE;
    filled->item.soul_target = target;
    filled->item.soul_target_spawn_time = target->spawn_time;

    T_ASSERT(G_UnitIsForcedVisibleToPlayer(carrier, 1));
    G_FreeEdict(target);

    T_ASSERT(!target->inuse);
    T_NULL(carrier->soul_trap_head);
    T_ASSERT(!carrier->soul_possession_added);
    T_ASSERT(!G_ActorHasSkill(carrier, "Asou"));
    T_NULL(carrier->inventory[0]);
    T_ASSERT(!filled->inuse);
    T_ASSERT(!G_UnitIsForcedVisibleToPlayer(carrier, 1));
}

TEST(wc3_items, soul_gem_pending_approach_round_trips_save) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"levels\"\nC;Y1;X8;K\"DataA1\"\n"
        "C;Y1;X9;K\"DataB1\"\nC;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"DataE1\"\n"
        "C;Y2;X1;K\"AIso\"\nC;Y2;X2;K\"AIso\"\nC;Y2;X3;K\"enemy,ground,hero\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"96\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AInv\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X8;K\"6\"\nC;Y3;X9;K\"0\"\n"
        "C;Y3;X10;K\"1\"\nC;Y3;X11;K\"1\"\nC;Y3;X12;K\"1\"\n"
        "C;Y4;X1;K\"Asou\"\nC;Y4;X2;K\"Asou\"\nC;Y4;X7;K\"1\"\nE\n";
    cstring_t const path = "/tmp/openwarcraft3-wc3-soul-gem-approach-save.bin";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    gameClient_t *client;
    edict_t *clent, *carrier, *target, *gem, *thinker;
    uint32_t thinker_slot;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    char number[16];
    cstring_t select[] = { "select", number };

    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    G_FowInit(); G_FowConnectPlayer(0); G_FowConnectPlayer(1);
    carrier = make_item_test_inventory_unit(64, 64);
    carrier->s.player = 0; carrier->svflags |= SVF_MONSTER;
    target = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 512, 64);
    target->s.player = 1; target->svflags |= SVF_MONSTER; target->s.model = 1;
    target->targtype = TARG_GROUND; target->movetype = MOVETYPE_STEP;
    target->stand = unit_stand; unit_stand(target); gi.LinkEntity(target);
    gem = make_item_test_world_item(MAKEFOURCC('g','s','o','u'), 64, 64);
    gem->data.ItemData = G_ItemData(MAKEFOURCC('g','s','o','u'));
    gem->item.charges = 1;
    T_ASSERT(G_AddItemToSlot(carrier, gem, 0));

    clent = G_GetPlayerEntityByNumber(0); client = clent->client;
    gi.Write = item_noop_write; gi.unicast = item_noop_unicast;
    G_SelectEntity(client, carrier); G_UseItem(carrier, 0);
    T_ASSERT(G_IsEntitySelected(client, carrier));
    T_ASSERT(G_InventoryCanUseItems(carrier));
    T_STREQ(G_ItemAbilityList(gem), "AIso");
    T_FEQ(S_SpellRange(MAKEFOURCC('A','I','s','o'), 1), 96.0f, 0.001f);
    T_NOT_NULL(client->menu.on_entity_selected);
    if (!client->menu.on_entity_selected) goto cleanup_soul_approach_save;
    snprintf(number, sizeof(number), "%u", (unsigned)target->s.number);
    thinker_slot = globals.num_edicts;
    G_ClientCommand(clent, 2, select);
    thinker = &globals.edicts[thinker_slot];
    T_ASSERT(thinker->inuse && thinker->think);
    T_ASSERT(thinker->spell_item == gem);

    bool const saved = WriteGame(path);
    T_ASSERT(saved);
    if (saved) {
        thinker->think = NULL; thinker->spell_item = NULL;
        T_ASSERT(ReadGame(path));
        thinker = &globals.edicts[thinker_slot];
        T_NOT_NULL(thinker->think);
        T_ASSERT(thinker->spell_item == gem);
        carrier->s.origin2.x = carrier->s.origin.x = 480.0f;
        if (thinker->think) thinker->think(thinker);
        T_ASSERT(!thinker->inuse);
        T_ASSERT(target->aiflags & AI_SOUL_TRAPPED);
    }

cleanup_soul_approach_save:
    gi.Write = old_write; gi.unicast = old_unicast;
    remove(path);
    G_FowShutdown();
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_items, set_item_droppable_blocks_manual_drop_but_not_scripted_move) {
    edict_t *carrier = NULL;
    edict_t *item = NULL;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit carrier = null\n"
        "  item gem = null\n"
        "endglobals\n"
        "function scripted_drop takes nothing returns nothing\n"
        "  call UnitDropItemPoint(carrier, gem, 128.0, 64.0)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set carrier = CreateUnit(Player(0), 'Hpal', 64.0, 64.0, 0.0)\n"
        "  set gem = CreateItem('spro', 64.0, 64.0)\n"
        "  call UnitAddItem(carrier, gem)\n"
        "  call SetItemDroppable(gem, false)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = g_edicts + i;
        if (!ent->inuse) continue;
        if (!carrier && ent->class_id == MAKEFOURCC('H','p','a','l') && ent->s.player == 0) carrier = ent;
        if (!item && G_IsItem(ent) && ent->class_id == MAKEFOURCC('s','p','r','o')) item = ent;
    }
    T_NOT_NULL(carrier);
    T_NOT_NULL(item);
    T_ASSERT(item->item.carrier == carrier);
    T_ASSERT(!G_ItemDroppable(item));
    T_ASSERT(!G_DropItem(carrier, (uint32_t)item->item.inventory_slot));
    T_ASSERT(item->item.carrier == carrier);

    jass_callbyname(level.vm, "scripted_drop", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_NULL(item->item.carrier);
    T_ASSERT(item->item.in_world);
}

TEST(wc3_items, orc08_scripted_soul_slot_swap_keeps_item_bound_to_carrier) {
    edict_t *carrier = NULL, *other = NULL, *target = NULL, *soul = NULL;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  unit carrier = null\n"
        "  unit other = null\n"
        "  item soul = null\n"
        "  item emptyGem = null\n"
        "endglobals\n"
        "function swap_soul_slot takes nothing returns nothing\n"
        "  call UnitRemoveItem(carrier, soul)\n"
        "  call BJassAssert(not UnitHasItem(carrier, soul), \"UnitRemoveItem left filled Soul in its slot\")\n"
        "  set emptyGem = CreateItem('gsou', 64.0, 64.0)\n"
        "  call BJassAssert(UnitAddItem(carrier, emptyGem), \"empty gem did not occupy freed slot\")\n"
        "  call BJassAssert(not UnitAddItem(other, soul), \"filled Soul transferred to another carrier\")\n"
        "  call BJassAssert(UnitAddItem(carrier, soul), \"filled Soul did not return to its carrier\")\n"
        "  call BJassAssert(not IsItemVisible(soul), \"filled Soul became a world item during slot swap\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  set carrier = CreateUnit(Player(0), 'Hpal', 64.0, 64.0, 0.0)\n"
        "  set other = CreateUnit(Player(0), 'Hpal', 96.0, 64.0, 0.0)\n"
        "  set soul = CreateItem('soul', 64.0, 64.0)\n"
        "  call UnitAddItem(carrier, soul)\n"
        "  call SetItemDroppable(soul, false)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = globals.edicts + i;
        if (!ent->inuse) continue;
        if (ent->class_id == MAKEFOURCC('H','p','a','l') && ent->s.player == 0) {
            if (!carrier) carrier = ent;
            else if (!other) other = ent;
        } else if (ent->class_id == MAKEFOURCC('s','o','u','l')) soul = ent;
        else if (ent->class_id == MAKEFOURCC('H','p','a','l') && ent->s.player == 1) target = ent;
    }
    /* Bind this test item to a live trapped target as capture does. */
    if (!target) target = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 512.0f, 64.0f);
    T_NOT_NULL(carrier); T_NOT_NULL(other); T_NOT_NULL(soul); T_NOT_NULL(target);
    if (!carrier || !other || !soul || !target) return;
    target->s.player = 1;
    target->aiflags |= AI_SOUL_TRAPPED;
    target->soul_trap_carrier = carrier;
    target->soul_trap_carrier_spawn_time = carrier->spawn_time;
    carrier->soul_trap_head = target;
    carrier->soul_trap_head_spawn_time = target->spawn_time;
    target->soul_trap_item = soul;
    target->soul_trap_item_spawn_time = soul->spawn_time;
    soul->item.soul_target = target;
    soul->item.soul_target_spawn_time = target->spawn_time;
    T_ASSERT(G_ActorAddSkill(carrier, MAKEFOURCC('A','s','o','u')));

    jass_callbyname(level.vm, "swap_soul_slot", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_ASSERT(soul->inuse);
    T_ASSERT(soul->item.carrier == carrier);
    T_EQ(soul->item.inventory_slot, 1);
    T_ASSERT(carrier->inventory[1] == soul);
    T_ASSERT(!soul->item.in_world);
}

TEST(wc3_items, missing_item_data_does_not_make_item_droppable) {
    edict_t *item;

    setup_test_world();
    item = alloc_test_unit(MAKEFOURCC('z','z','z','z'), 32.0f, 32.0f);
    item->targtype = TARG_ITEM;
    item->item.in_world = true;
    item->item.inventory_slot = -1;
    item->data.ItemData = NULL;
    T_EQ(G_ItemData(item->class_id)->id, 0);
    T_ASSERT(!G_ItemDroppable(item));
}

TEST(wc3_items, consumed_perishable_keeps_manipulated_item_through_sleep) {
    static ItemData_t soul_data = { .perishable = true, .droppable = true, .file = "test.mdx" };
    edict_t *carrier = NULL;
    edict_t *item;

    setup_test_world();
    T_ASSERT(run_test_jass(
        "globals\n"
        "  quest soulQuest = null\n"
        "endglobals\n"
        "function soul_gem_condition takes nothing returns boolean\n"
        "  return GetItemTypeId(GetManipulatedItem()) == 'gsou'\n"
        "endfunction\n"
        "function soul_gem_action takes nothing returns nothing\n"
        "  call TriggerSleepAction(0.0)\n"
        "  call BJassAssert(GetItemTypeId(GetManipulatedItem()) == 'gsou', \"manipulated item lost across sleep\")\n"
        "  call QuestSetCompleted(soulQuest, true)\n"
        "endfunction\n"
        "function verify_soul_gem takes nothing returns nothing\n"
        "  call BJassAssert(IsQuestCompleted(soulQuest), \"Soul Gem use event did not complete\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  local unit u = CreateUnit(Player(0), 'Hpal', 64.0, 64.0, 0.0)\n"
        "  set soulQuest = CreateQuest()\n"
        "  call TriggerRegisterPlayerUnitEvent(t, Player(0), EVENT_PLAYER_UNIT_USE_ITEM, null)\n"
        "  call TriggerAddCondition(t, Condition(function soul_gem_condition))\n"
        "  call TriggerAddAction(t, function soul_gem_action)\n"
        "endfunction\n"));

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = g_edicts + i;
        if (ent->inuse && ent->class_id == MAKEFOURCC('H','p','a','l') && ent->s.player == 0) {
            carrier = ent;
            break;
        }
    }
    T_NOT_NULL(carrier);
    item = make_item_test_world_item(MAKEFOURCC('g','s','o','u'), 64, 64);
    item->data.ItemData = &soul_data;
    item->item.charges = 1;
    T_ASSERT(G_AddItemToSlot(carrier, item, 0));

    G_CompleteItemUse(carrier, item);
    T_NULL(carrier->inventory[0]);
    T_ASSERT(item->inuse);
    T_ASSERT(item->item.pending_use_removal);
    T_EQ(item->item.charges, 0);

    G_RunEvents();
    jass_runevents(level.vm);
    G_RunConsumedItemFrees();
    T_ASSERT(item->inuse);
    T_ASSERT(item->item.pending_use_removal);

    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    jass_callbyname(level.vm, "verify_soul_gem", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));
    G_RunConsumedItemFrees();
    T_ASSERT(!item->inuse);
}

#endif /* BZ_TESTS */
