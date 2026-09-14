#include <math.h>

#include "g_local.h"
#include "skills/s_skills.h"

#define SHOP_DEFAULT_ACTIVATION_RADIUS 450.0f

static void G_ResetItemStock(LPEDICT unit) {
    if (!unit) return;
    unit->stock.items_initialized = false;
    unit->stock.item_count = 0;
    memset(unit->stock.items, 0, sizeof(unit->stock.items));
}

/* Units created after a global slot change inherit the current capacities. */
void G_InitStockSlots(LPEDICT unit) {
    if (!unit) return;
    unit->stock.item_slots = level.stock.item_slots;
    unit->stock.unit_slots = level.stock.unit_slots;
    G_ResetItemStock(unit);
}

/* Global slot changes affect current shops and become the default for units created later. */
void G_SetAllStockSlots(BOOL items, LONG slots) {
    DWORD value = (DWORD)MAX(0, slots);
    if (items) level.stock.item_slots = value;
    else level.stock.unit_slots = value;
    FILTER_EDICTS(unit, unit->inuse) {
        if (items) {
            if (unit->stock.item_slots == value) continue;
            unit->stock.item_slots = value;
            G_ResetItemStock(unit);
        } else {
            unit->stock.unit_slots = value;
        }
    }
}

/* Unit-specific stock limits override the current global capacity without changing later spawns. */
void G_SetStockSlots(LPEDICT unit, BOOL items, LONG slots) {
    DWORD value = (DWORD)MAX(0, slots);
    if (!unit) return;
    if (items) {
        if (unit->stock.item_slots == value) return;
        unit->stock.item_slots = value;
        G_ResetItemStock(unit);
    } else {
        unit->stock.unit_slots = value;
    }
}

BOOL G_IsItemShop(LPCEDICT shop) {
    LPCSTR items;

    if (!shop || !shop->inuse || !shop->class_id || M_IsDead((LPEDICT)shop)) return false;
    items = shop->data.UnitProfile ? shop->data.UnitProfile->sellItems : NULL;
    return items && *items;
}

BOOL G_CanUseItemShop(LPGAMECLIENT client, LPCEDICT shop) {
    if (!client || !G_IsItemShop(shop)) return false;
    /* Neutral Passive shops are public. Owned/racial shops remain usable by
     * players with normal command authority. Aall/allied shop-sharing is a
     * separate relationship policy and is intentionally not inferred here. */
    return shop->s.player == PLAYER_NEUTRAL_PASSIVE ||
           G_UnitCanControl(client, (LPEDICT)shop);
}

/* Aneu/Aall DataA is the neutral-building activation radius in Warcraft and
 * Warsmash.  Keep a retail-compatible 450 fallback for custom/minimal data
 * that declares Sellitems without carrying the neutral-building ability. */
FLOAT G_ShopActivationRadius(LPCEDICT shop) {
    LPCSTR abilities;

    if (!shop || !shop->data.UnitAbilities) return SHOP_DEFAULT_ACTIVATION_RADIUS;
    abilities = shop->data.UnitAbilities->abilList;
    if (abilities) {
        PARSE_LIST(abilities, ability, parse_segment) {
            DWORD code;
            FLOAT radius;

            if (strlen(ability) != 4) continue;
            code = G_AbilityCodeName(ability);
            if (code != MAKEFOURCC('A','n','e','u') && code != MAKEFOURCC('A','a','l','l')) continue;
            radius = S_SpellData(FS_SLKKey(ability), 1, 1);
            if (radius > 0.0f) return radius;
        }
    }
    return SHOP_DEFAULT_ACTIVATION_RADIUS;
}

static BOOL G_ShopPatronInRange(LPCEDICT shop, LPCEDICT unit) {
    FLOAT reach;
    FLOAT distance;

    if (!shop || !unit) return false;
    reach = G_ShopActivationRadius(shop) + MAX(0.0f, shop->collision) + MAX(0.0f, unit->collision);
    distance = Vector2_distance(&shop->s.origin2, &unit->s.origin2);
    return distance <= reach;
}

/* Warsmash's CAbilityNeutralBuilding stores one eligible unit per player and
 * automatically reacquires it within DataA according to the authored
 * interaction type. Item purchases ultimately require an inventory carrier,
 * so until OpenRealm persists that explicit per-player choice, resolve the
 * nearest living owned inventory unit deterministically. */
LPEDICT G_FindShopPatron(LPGAMECLIENT client, LPEDICT shop) {
    LPEDICT best = NULL;
    FLOAT best_distance = FLT_MAX;

    if (!G_CanUseItemShop(client, shop)) return NULL;
    FILTER_EDICTS(unit,
        unit->inuse && unit != shop && unit->s.player == client->ps.number &&
        !M_IsDead(unit) && G_UnitHasInventory(unit) && G_ShopPatronInRange(shop, unit)) {
        FLOAT distance = Vector2_distance(&shop->s.origin2, &unit->s.origin2);

        if (!best || distance < best_distance ||
            (distance == best_distance && unit->s.number < best->s.number)) {
            best = unit;
            best_distance = distance;
        }
    }
    return best;
}

static DWORD G_StockDelayMs(LONG seconds) {
    if (seconds <= 0) return 0;
    if ((DWORD)seconds > UINT_MAX / 1000u) return UINT_MAX;
    return (DWORD)seconds * 1000u;
}

static void G_InitItemStock(LPEDICT shop) {
    LPCSTR items;
    DWORD limit;

    if (!shop || shop->stock.items_initialized) return;
    shop->stock.items_initialized = true;
    shop->stock.item_count = 0;
    memset(shop->stock.items, 0, sizeof(shop->stock.items));
    if (!G_IsItemShop(shop) || !shop->stock.item_slots) return;

    items = shop->data.UnitProfile->sellItems;
    limit = MIN(shop->stock.item_slots, (DWORD)MAX_SHOP_STOCK);
    PARSE_LIST(items, item_name, parse_segment) {
        DWORD item_id;
        ItemData_t const *item;
        DWORD index;
        DWORD start_delay;

        if (shop->stock.item_count >= limit) break;
        if (strlen(item_name) != 4) continue;
        memcpy(&item_id, item_name, sizeof(item_id));
        item = G_ItemData(item_id);
        if (!item || !item->file) continue;

        index = shop->stock.item_count++;
        shop->stock.items[index].id = item_id;
        if (item->stockMax <= 0) continue;
        start_delay = G_StockDelayMs(item->stockStart);
        if (!start_delay) {
            shop->stock.items[index].current = item->stockMax;
        } else {
            shop->stock.items[index].current = 0;
            shop->stock.items[index].delay_start = shop->spawn_time;
            shop->stock.items[index].delay_end = shop->spawn_time + start_delay;
        }
    }
}

static void G_UpdateItemStockEntry(LPEDICT shop, DWORD index) {
    ItemData_t const *item;
    DWORD now;
    DWORD regen;
    DWORD increments;
    DWORD elapsed;
    LONG max_stock;

    if (!shop || index >= shop->stock.item_count) return;
    item = G_ItemData(shop->stock.items[index].id);
    if (!item) return;
    max_stock = MAX(0, item->stockMax);
    if (shop->stock.items[index].current >= max_stock) {
        shop->stock.items[index].current = max_stock;
        shop->stock.items[index].delay_start = 0;
        shop->stock.items[index].delay_end = 0;
        return;
    }
    if (!shop->stock.items[index].delay_end) return;

    now = G_Time();
    if (now < shop->stock.items[index].delay_end) return;

    regen = G_StockDelayMs(item->stockRegen);
    if (!regen) {
        /* A zero replenish interval cannot advance a recurring timer. Once an
         * authored start delay expires, make the configured stock available. */
        shop->stock.items[index].current = max_stock;
        shop->stock.items[index].delay_start = 0;
        shop->stock.items[index].delay_end = 0;
        return;
    }

    elapsed = now - shop->stock.items[index].delay_end;
    increments = 1u + elapsed / regen;
    shop->stock.items[index].current = MIN(max_stock,
        shop->stock.items[index].current + (LONG)increments);
    if (shop->stock.items[index].current >= max_stock) {
        shop->stock.items[index].delay_start = 0;
        shop->stock.items[index].delay_end = 0;
    } else {
        shop->stock.items[index].delay_start = shop->stock.items[index].delay_end + (increments - 1u) * regen;
        shop->stock.items[index].delay_end += increments * regen;
    }
}

static LONG G_FindShopItemStock(LPEDICT shop, DWORD item_id) {
    G_InitItemStock(shop);
    FOR_LOOP(i, shop ? shop->stock.item_count : 0) {
        if (shop->stock.items[i].id != item_id) continue;
        G_UpdateItemStockEntry(shop, i);
        return (LONG)i;
    }
    return -1;
}

static void G_StartItemRestock(LPEDICT shop, DWORD index) {
    ItemData_t const *item;
    DWORD regen;
    DWORD now;

    if (!shop || index >= shop->stock.item_count) return;
    item = G_ItemData(shop->stock.items[index].id);
    if (!item || shop->stock.items[index].current >= MAX(0, item->stockMax) ||
        shop->stock.items[index].delay_end) return;
    regen = G_StockDelayMs(item->stockRegen);
    if (!regen) return;
    now = G_Time();
    shop->stock.items[index].delay_start = now;
    shop->stock.items[index].delay_end = now + regen;
}

static void G_DisableShopButton(gameCommandButton_t *button, LPCSTR reason) {
    size_t used;

    if (!button) return;
    button->disabled = 1;
    if (!reason || !*reason) return;
    used = strlen(button->ubertip);
    snprintf(button->ubertip + used, sizeof(button->ubertip) - used,
             "%s|cffffcc00%s|r", used ? "|n" : "", reason);
}

BYTE G_GetShopItemButtons(LPGAMECLIENT client, LPEDICT shop, gameCommandButton_t *buttons, BYTE max_buttons) {
    LPEDICT patron;
    BYTE count = 0;

    if (!client || !buttons || !max_buttons || !G_CanUseItemShop(client, shop)) return 0;
    memset(buttons, 0, sizeof(*buttons) * max_buttons);
    G_InitItemStock(shop);
    patron = G_FindShopPatron(client, shop);

    FOR_LOOP(i, shop->stock.item_count) {
        char code[5] = {0};
        gameCommandButton_t *button;
        ItemData_t const *item;
        DWORD now;

        if (count >= max_buttons) break;
        G_UpdateItemStockEntry(shop, i);
        memcpy(code, &shop->stock.items[i].id, 4);
        button = &buttons[count];
        if (!G_BuildCommandButton(shop, code, false, 0, button)) continue;
        button->x = count % 4;
        button->y = count / 4;
        item = G_ItemData(shop->stock.items[i].id);
        if (item && item->stockMax > 0) button->number = (DWORD)MAX(0, shop->stock.items[i].current);

        if (!patron) {
            G_DisableShopButton(button, "No eligible purchaser is nearby.");
        } else if (shop->stock.items[i].current <= 0) {
            G_DisableShopButton(button, "Out of stock.");
            if (shop->stock.items[i].delay_end > shop->stock.items[i].delay_start) {
                now = G_Time();
                button->cooldown_start_time = shop->stock.items[i].delay_start;
                button->cooldown_end_time = shop->stock.items[i].delay_end;
                if (now < shop->stock.items[i].delay_end) {
                    button->cooldown = (FLOAT)(shop->stock.items[i].delay_end - now) /
                        (FLOAT)(shop->stock.items[i].delay_end - shop->stock.items[i].delay_start);
                }
            }
        } else if (G_FindFreeInventorySlot(patron) < 0) {
            G_DisableShopButton(button, "Inventory is full.");
        } else if (item && client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] < (DWORD)MAX(0, item->goldcost)) {
            G_DisableShopButton(button, "Not enough gold.");
        } else if (item && client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] < (DWORD)MAX(0, item->lumbercost)) {
            G_DisableShopButton(button, "Not enough lumber.");
        }
        count++;
    }
    return count;
}

BOOL G_ShopPurchaseItem(LPEDICT clent, LPEDICT shop, DWORD item_id) {
    LPGAMECLIENT client = clent ? clent->client : NULL;
    LPEDICT patron;
    LPEDICT item_ent;
    ItemData_t const *item;
    LONG stock_index;
    DWORD gold;
    DWORD lumber;

    if (!G_CanUseItemShop(client, shop)) return false;
    stock_index = G_FindShopItemStock(shop, item_id);
    if (stock_index < 0) return false;
    patron = G_FindShopPatron(client, shop);
    if (!patron) {
        G_ShowCommandErrorText(clent, "No eligible purchaser is nearby.");
        return false;
    }
    item = G_ItemData(item_id);
    if (!item || !item->file) return false;
    if (shop->stock.items[stock_index].current <= 0) {
        G_ShowCommandErrorText(clent, "Out of stock.");
        return false;
    }
    if (G_FindFreeInventorySlot(patron) < 0) {
        G_ShowCommandErrorText(clent, "Inventory is full.");
        return false;
    }

    gold = (DWORD)MAX(0, item->goldcost);
    lumber = (DWORD)MAX(0, item->lumbercost);
    if (client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] < gold) {
        G_ShowCommandErrorText(clent, "Not enough gold.");
        return false;
    }
    if (client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] < lumber) {
        G_ShowCommandErrorText(clent, "Not enough lumber.");
        return false;
    }

    item_ent = SP_SpawnAtLocation(item_id, client->ps.number, &shop->s.origin2);
    if (!item_ent || !G_PickupItem(patron, item_ent)) {
        if (item_ent) G_RemoveItem(item_ent);
        return false;
    }

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= gold;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= lumber;
    G_RefreshResourceBar(clent);
    shop->stock.items[stock_index].current--;
    G_StartItemRestock(shop, (DWORD)stock_index);
    G_InvalidateCommands(client);
    return true;
}

static FLOAT G_ShopPawnRate(void) {
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", "PawnItemRate");
    FLOAT rate = value ? atof(value) : 0.5f;
    return MAX(0.0f, rate);
}

static FLOAT G_ShopGiveItemRange(void) {
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", "GiveItemRange");
    FLOAT range = value ? atof(value) : ITEM_DROP_RANGE;
    return MAX(0.0f, range);
}

BOOL G_ShopPawnItem(LPEDICT clent, LPEDICT shop, LPEDICT carrier, LPEDICT item) {
    LPGAMECLIENT client = clent ? clent->client : NULL;
    ItemData_t const *data;
    FLOAT distance;
    FLOAT reach;
    FLOAT rate;
    DWORD gold;
    DWORD lumber;

    if (!G_CanUseItemShop(client, shop) || !G_ActorHasSkill(shop, "Apit") ||
        !carrier || carrier->s.player != client->ps.number || !G_IsItem(item) ||
        item->item.carrier != carrier || item->item.in_world) return false;
    data = item->data.ItemData ? item->data.ItemData : G_ItemData(item->class_id);
    if (!data || !data->pawnable) return false;

    distance = Vector2_distance(&carrier->s.origin2, &shop->s.origin2);
    reach = G_ShopGiveItemRange() + MAX(0.0f, carrier->collision) + MAX(0.0f, shop->collision);
    if (distance > reach) return false;

    rate = G_ShopPawnRate();
    gold = (DWORD)ceilf((FLOAT)MAX(0, data->goldcost) * rate);
    lumber = (DWORD)ceilf((FLOAT)MAX(0, data->lumbercost) * rate);
    G_RemoveItem(item);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = (USHORT)MIN((DWORD)USHRT_MAX,
        (DWORD)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] + gold);
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = (USHORT)MIN((DWORD)USHRT_MAX,
        (DWORD)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] + lumber);
    G_RefreshResourceBar(clent);
    G_InvalidateCommands(client);
    return true;
}
