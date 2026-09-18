#include <math.h>

#include "g_local.h"
#include "skills/s_skills.h"

#define SHOP_DEFAULT_ACTIVATION_RADIUS 450.0f // world units; retail custom-data interaction fallback; used when Aneu/Aall DataA is absent

static BOOL shop_warned_activation_fallback, shop_warned_interaction_missing;
static BOOL shop_warned_pawn_rate, shop_warned_give_range;

static void G_ResetItemStock(LPEDICT unit) {
    if (!unit) return;
    unit->stock.items_initialized = false;
    unit->stock.item_count = 0;
    memset(unit->stock.items, 0, sizeof(unit->stock.items));
}

static void G_ResetUnitStock(LPEDICT unit) {
    if (!unit) return;
    unit->stock.units_initialized = false;
    unit->stock.unit_count = 0;
    memset(unit->stock.units, 0, sizeof(unit->stock.units));
}

/* Units created after a global slot change inherit the current capacities. */
void G_InitStockSlots(LPEDICT unit) {
    if (!unit) return;
    unit->stock.item_slots = level.stock.item_slots;
    unit->stock.unit_slots = level.stock.unit_slots;
    G_ResetItemStock(unit);
    G_ResetUnitStock(unit);
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
            if (unit->stock.unit_slots == value) continue;
            unit->stock.unit_slots = value;
            G_ResetUnitStock(unit);
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
        if (unit->stock.unit_slots == value) return;
        unit->stock.unit_slots = value;
        G_ResetUnitStock(unit);
    }
}

/* Identifies live units whose authored merchandise makes them shops. */
BOOL G_IsItemShop(LPCEDICT shop) {
    LPCSTR items;

    if (!shop || !shop->inuse || !shop->class_id || M_IsDead((LPEDICT)shop)) return false;
    items = shop->data.UnitProfile ? shop->data.UnitProfile->sellItems : NULL;
    return (items && *items) || (shop->stock.items_initialized && shop->stock.item_count);
}

BOOL G_IsUnitShop(LPCEDICT shop) {
    LPCSTR units;

    if (!shop || !shop->inuse || !shop->class_id || M_IsDead((LPEDICT)shop)) return false;
    units = shop->data.UnitProfile ? shop->data.UnitProfile->sellUnits : NULL;
    return (units && *units) || (shop->stock.units_initialized && shop->stock.unit_count);
}

static BOOL G_CanUseShop(LPGAMECLIENT client, LPCEDICT shop) {
    if (!client || !shop || (!G_IsItemShop(shop) && !G_IsUnitShop(shop))) return false;
    /* Neutral Passive shops are public. Owned/racial shops remain usable by
     * players with normal command authority. Aall/allied shop-sharing is a
     * separate relationship policy and is intentionally not inferred here. */
    return shop->s.player == PLAYER_NEUTRAL_PASSIVE ||
           G_UnitCanControl(client, (LPEDICT)shop);
}

/* Applies the neutral/public versus owned/control-authority policy for shop access. */
BOOL G_CanUseItemShop(LPGAMECLIENT client, LPCEDICT shop) {
    return G_IsItemShop(shop) && G_CanUseShop(client, shop);
}

BOOL G_CanUseUnitShop(LPGAMECLIENT client, LPCEDICT shop) {
    return G_IsUnitShop(shop) && G_CanUseShop(client, shop);
}

/* Aneu/Aall DataA is the neutral-building activation radius in Warcraft and
 * Warsmash.  Keep a retail-compatible 450 fallback for custom/minimal data
 * that declares Sellitems without carrying the neutral-building ability. */
FLOAT G_ShopActivationRadius(LPCEDICT shop) {
    LPCSTR abilities;

    if (!shop || !shop->data.UnitAbilities) {
        if (!shop_warned_activation_fallback) {
            fprintf(stderr, "WC3 shop: missing Aneu/Aall DataA; using %.0f activation radius\n",
                    SHOP_DEFAULT_ACTIVATION_RADIUS);
            shop_warned_activation_fallback = true;
        }
        return SHOP_DEFAULT_ACTIVATION_RADIUS;
    }
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
    if (!shop_warned_activation_fallback) {
        fprintf(stderr, "WC3 shop: invalid or missing Aneu/Aall DataA; using %.0f activation radius\n",
                SHOP_DEFAULT_ACTIVATION_RADIUS);
        shop_warned_activation_fallback = true;
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

enum {
    SHOP_INTERACT_INVENTORY = 1,
    SHOP_INTERACT_NON_BUILDING = 2,
    SHOP_INTERACT_ANY = 4,
    SHOP_INTERACT_ANY_ANE2 = 16,
};

static LONG G_ShopInteractionType(LPCEDICT shop) {
    LPCSTR abilities;

    if (!shop || !shop->data.UnitAbilities) return 0;
    abilities = shop->data.UnitAbilities->abilList;
    if (!abilities) return 0;
    PARSE_LIST(abilities, ability, parse_segment) {
        DWORD code;
        LONG type;

        if (strlen(ability) != 4) continue;
        code = G_AbilityCodeName(ability);
        if (code != MAKEFOURCC('A','n','e','u') && code != MAKEFOURCC('A','a','l','l')) continue;
        /* S_SpellData takes (level, Data-slot); neutral interaction is DataB1. */
        type = (LONG)S_SpellData(FS_SLKKey(ability), 1, 2);
        if (type > 0) return type;
    }
    return 0;
}

static BOOL G_ShopPatronEligible(LPCEDICT shop, LPCEDICT unit, BOOL require_inventory) {
    LONG const interaction = G_ShopInteractionType(shop);
    BOOL const is_unit = unit && ((unit->svflags & SVF_MONSTER) || G_UnitIsBuilding(unit->class_id));

    if (!shop || !is_unit || M_IsDead((LPEDICT)unit) || !G_ShopPatronInRange(shop, unit)) return false;
    switch (interaction) {
    case SHOP_INTERACT_INVENTORY:
        if (!G_UnitHasInventory((LPEDICT)unit)) return false;
        break;
    case SHOP_INTERACT_NON_BUILDING:
        if (G_UnitIsBuilding(unit->class_id)) return false;
        break;
    case SHOP_INTERACT_ANY:
    case SHOP_INTERACT_ANY_ANE2:
        break;
    default:
        /* Item shops predate Aneu decoding and keep their inventory-carrier
         * fallback. Sellunits has no safe authored fallback, so reject it. */
        if (!shop_warned_interaction_missing) {
            fprintf(stderr, "WC3 shop: missing Aneu/Aall DataB; item fallback only\n");
            shop_warned_interaction_missing = true;
        }
        return require_inventory && G_UnitHasInventory((LPEDICT)unit);
    }
    return !require_inventory || G_UnitHasInventory((LPEDICT)unit);
}

/* Warsmash's CAbilityNeutralBuilding stores one eligible unit per player and
 * periodically reacquires it within DataA according to DataB interaction type.
 * OpenRealm resolves the nearest valid owned unit deterministically when shop
 * state is requested; item purchases additionally require inventory capacity. */
static LPEDICT G_FindShopPatronInternal(LPGAMECLIENT client, LPEDICT shop, BOOL require_inventory) {
    LPEDICT best = NULL;
    FLOAT best_distance = FLT_MAX;

    if (!G_CanUseShop(client, shop)) return NULL;
    FILTER_EDICTS(unit,
        unit->inuse && unit != shop && unit->s.player == client->ps.number &&
        G_ShopPatronEligible(shop, unit, require_inventory)) {
        FLOAT distance = Vector2_distance(&shop->s.origin2, &unit->s.origin2);

        if (!best || distance < best_distance ||
            (distance == best_distance && unit->s.number < best->s.number)) {
            best = unit;
            best_distance = distance;
        }
    }
    return best;
}

LPEDICT G_FindShopPatron(LPGAMECLIENT client, LPEDICT shop) {
    if (!G_CanUseItemShop(client, shop)) return NULL;
    return G_FindShopPatronInternal(client, shop, true);
}

LPEDICT G_FindUnitShopPatron(LPGAMECLIENT client, LPEDICT shop) {
    if (!G_CanUseUnitShop(client, shop)) return NULL;
    return G_FindShopPatronInternal(client, shop, false);
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
        if (strlen(item_name) != 4) {
            fprintf(stderr, "WC3 shop: invalid Sellitems entry '%s' on unit %.4s\n", item_name, (LPCSTR)&shop->class_id);
            continue;
        }
        memcpy(&item_id, item_name, sizeof(item_id));
        item = G_ItemData(item_id);
        if (!item) {
            fprintf(stderr, "WC3 shop: unresolved Sellitems item %.4s on unit %.4s\n", item_name, (LPCSTR)&shop->class_id);
            continue;
        }
        if (!item->file) {
            fprintf(stderr, "WC3 shop: item %.4s on unit %.4s has no model\n", item_name, (LPCSTR)&shop->class_id);
            continue;
        }

        index = shop->stock.item_count++;
        shop->stock.items[index].id = item_id;
        shop->stock.items[index].maximum = MAX(0, item->stockMax);
        if (shop->stock.items[index].maximum <= 0) continue;
        start_delay = G_StockDelayMs(item->stockStart);
        if (!start_delay) {
            shop->stock.items[index].current = shop->stock.items[index].maximum;
        } else {
            shop->stock.items[index].current = 0;
            shop->stock.items[index].delay_start = shop->spawn_time;
            shop->stock.items[index].delay_end = shop->spawn_time + start_delay;
        }
    }
}

static void G_UpdateStockEntry(edictShopStockItem_t *entry, LONG regen_seconds) {
    DWORD now, regen, increments, elapsed;
    LONG max_stock;

    if (!entry) return;
    max_stock = MAX(0, entry->maximum);
    if (entry->current >= max_stock) {
        entry->current = max_stock;
        entry->delay_start = entry->delay_end = 0;
        return;
    }
    if (!entry->delay_end || (now = G_Time()) < entry->delay_end) return;
    regen = G_StockDelayMs(regen_seconds);
    if (!regen) {
        /* A zero interval cannot advance a recurring timer. After an authored
         * start delay expires, expose the configured maximum immediately. */
        entry->current = max_stock;
        entry->delay_start = entry->delay_end = 0;
        return;
    }
    elapsed = now - entry->delay_end;
    increments = 1u + elapsed / regen;
    entry->current = MIN(max_stock, entry->current + (LONG)increments);
    if (entry->current >= max_stock) entry->delay_start = entry->delay_end = 0;
    else {
        entry->delay_start = entry->delay_end + (increments - 1u) * regen;
        entry->delay_end += increments * regen;
    }
}

static void G_StartStockRestock(edictShopStockItem_t *entry, LONG regen_seconds) {
    DWORD regen, now;

    if (!entry || entry->current >= MAX(0, entry->maximum) || entry->delay_end) return;
    regen = G_StockDelayMs(regen_seconds);
    if (!regen) return;
    now = G_Time();
    entry->delay_start = now;
    entry->delay_end = now + regen;
}

static void G_UpdateItemStockEntry(LPEDICT shop, DWORD index) {
    ItemData_t const *item;

    if (!shop || index >= shop->stock.item_count) return;
    item = G_ItemData(shop->stock.items[index].id);
    if (item) G_UpdateStockEntry(&shop->stock.items[index], item->stockRegen);
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

    if (!shop || index >= shop->stock.item_count) return;
    item = G_ItemData(shop->stock.items[index].id);
    if (item) G_StartStockRestock(&shop->stock.items[index], item->stockRegen);
}

static LONG G_FindItemStockEntry(LPEDICT shop, DWORD item_id) {
    if (!shop) return -1;
    FOR_LOOP(i, shop->stock.item_count)
        if (shop->stock.items[i].id == item_id) return (LONG)i;
    return -1;
}

/* Dynamic item stock mirrors Warcraft's neutral-shop natives: supplied
 * current/max values take effect immediately and authored stockRegen owns
 * later replenishment. */
BOOL G_AddItemStock(LPEDICT shop, DWORD item_id, LONG current, LONG maximum) {
    ItemData_t const *item;
    LONG index;
    DWORD limit;

    if (!shop || !shop->inuse || !G_ActorHasSkill(shop, "Asid")) return false;
    item = G_ItemData(item_id);
    if (!item || item->id != item_id) return false;

    G_InitItemStock(shop);
    index = G_FindItemStockEntry(shop, item_id);
    if (index < 0) {
        limit = MIN(shop->stock.item_slots, (DWORD)MAX_SHOP_STOCK);
        if (shop->stock.item_count >= limit) return false;
        index = (LONG)shop->stock.item_count++;
    }

    shop->stock.items[index] = (edictShopStockItem_t){
        .id = item_id,
        .current = MIN(MAX(0, current), MAX(0, maximum)),
        .maximum = MAX(0, maximum),
    };
    G_StartItemRestock(shop, (DWORD)index);
    return true;
}

void G_RemoveItemStock(LPEDICT shop, DWORD item_id) {
    LONG index;

    if (!shop || !shop->inuse || !G_ActorHasSkill(shop, "Asid")) return;
    G_InitItemStock(shop);
    index = G_FindItemStockEntry(shop, item_id);
    if (index < 0) return;
    if ((DWORD)index + 1u < shop->stock.item_count)
        memmove(&shop->stock.items[index], &shop->stock.items[index + 1],
                (shop->stock.item_count - (DWORD)index - 1u) * sizeof(shop->stock.items[0]));
    shop->stock.item_count--;
    memset(&shop->stock.items[shop->stock.item_count], 0, sizeof(shop->stock.items[0]));
}

void G_AddItemStockAll(DWORD item_id, LONG current, LONG maximum) {
    FILTER_EDICTS(shop, shop->inuse && G_ActorHasSkill(shop, "Asid"))
        G_AddItemStock(shop, item_id, current, maximum);
}

void G_RemoveItemStockAll(DWORD item_id) {
    FILTER_EDICTS(shop, shop->inuse && G_ActorHasSkill(shop, "Asid"))
        G_RemoveItemStock(shop, item_id);
}

static void G_InitUnitStock(LPEDICT shop) {
    LPCSTR units;
    DWORD limit;

    if (!shop || shop->stock.units_initialized) return;
    shop->stock.units_initialized = true;
    shop->stock.unit_count = 0;
    memset(shop->stock.units, 0, sizeof(shop->stock.units));
    if (!G_IsUnitShop(shop) || !shop->stock.unit_slots) return;

    units = shop->data.UnitProfile->sellUnits;
    limit = MIN(shop->stock.unit_slots, (DWORD)MAX_SHOP_STOCK);
    PARSE_LIST(units, unit_name, parse_segment) {
        DWORD unit_id;
        UnitBalance_t const *unit;
        UnitUI_t const *ui;
        DWORD index;
        DWORD start_delay;

        if (shop->stock.unit_count >= limit) break;
        if (strlen(unit_name) != 4) {
            fprintf(stderr, "WC3 shop: bad Sellunits '%s' on %.4s\n", unit_name, (LPCSTR)&shop->class_id);
            continue;
        }
        memcpy(&unit_id, unit_name, sizeof(unit_id));
        unit = G_UnitBalance(unit_id);
        if (!unit || unit->id != unit_id) {
            fprintf(stderr, "WC3 shop: unresolved Sellunits %.4s on %.4s\n", unit_name, (LPCSTR)&shop->class_id);
            continue;
        }
        ui = G_UnitUI(unit_id);
        if (!ui || !ui->modelFile || !*ui->modelFile) {
            fprintf(stderr, "WC3 shop: unit %.4s on unit %.4s has no model\n", unit_name, (LPCSTR)&shop->class_id);
            continue;
        }
        /* Taverns also use Sellunits, but Hero availability/limits are a
         * separate neutral-Hero contract. Do not silently sell them as ordinary mercenaries. */
        if (unit->strength > 0 || unit->agility > 0 || unit->intelligence > 0) {
            fprintf(stderr, "WC3 shop: Hero %.4s on %.4s needs Tavern support\n", unit_name, (LPCSTR)&shop->class_id);
            continue;
        }

        index = shop->stock.unit_count++;
        shop->stock.units[index].id = unit_id;
        shop->stock.units[index].maximum = MAX(0, unit->stockMax);
        if (shop->stock.units[index].maximum <= 0) continue;
        start_delay = G_StockDelayMs(unit->stockStart);
        if (!start_delay) {
            shop->stock.units[index].current = shop->stock.units[index].maximum;
        } else {
            shop->stock.units[index].current = 0;
            shop->stock.units[index].delay_start = shop->spawn_time;
            shop->stock.units[index].delay_end = shop->spawn_time + start_delay;
        }
    }
}

static void G_UpdateUnitStockEntry(LPEDICT shop, DWORD index) {
    UnitBalance_t const *unit;

    if (!shop || index >= shop->stock.unit_count) return;
    unit = G_UnitBalance(shop->stock.units[index].id);
    if (unit && unit->id == shop->stock.units[index].id)
        G_UpdateStockEntry(&shop->stock.units[index], unit->stockRegen);
}

static LONG G_FindShopUnitStock(LPEDICT shop, DWORD unit_id) {
    G_InitUnitStock(shop);
    FOR_LOOP(i, shop ? shop->stock.unit_count : 0) {
        if (shop->stock.units[i].id != unit_id) continue;
        G_UpdateUnitStockEntry(shop, i);
        return (LONG)i;
    }
    return -1;
}

static void G_StartUnitRestock(LPEDICT shop, DWORD index) {
    UnitBalance_t const *unit;

    if (!shop || index >= shop->stock.unit_count) return;
    unit = G_UnitBalance(shop->stock.units[index].id);
    if (unit && unit->id == shop->stock.units[index].id)
        G_StartStockRestock(&shop->stock.units[index], unit->stockRegen);
}

static LONG G_FindUnitStockEntry(LPEDICT shop, DWORD unit_id) {
    if (!shop) return -1;
    FOR_LOOP(i, shop->stock.unit_count)
        if (shop->stock.units[i].id == unit_id) return (LONG)i;
    return -1;
}

/* Warcraft's stock natives override the current/max stock immediately; the
 * unit type's authored stockRegen continues to own later replenishment. */
BOOL G_AddUnitStock(LPEDICT shop, DWORD unit_id, LONG current, LONG maximum) {
    UnitBalance_t const *unit;
    LONG index;
    DWORD limit;

    if (!shop || !shop->inuse || !G_ActorHasSkill(shop, "Asud")) return false;
    unit = G_UnitBalance(unit_id);
    if (!unit || unit->id != unit_id) return false;

    G_InitUnitStock(shop);
    index = G_FindUnitStockEntry(shop, unit_id);
    if (index < 0) {
        limit = MIN(shop->stock.unit_slots, (DWORD)MAX_SHOP_STOCK);
        if (shop->stock.unit_count >= limit) return false;
        index = (LONG)shop->stock.unit_count++;
    }

    shop->stock.units[index] = (edictShopStockItem_t){
        .id = unit_id,
        .current = MIN(MAX(0, current), MAX(0, maximum)),
        .maximum = MAX(0, maximum),
    };
    G_StartUnitRestock(shop, (DWORD)index);
    return true;
}

void G_RemoveUnitStock(LPEDICT shop, DWORD unit_id) {
    LONG index;

    if (!shop || !shop->inuse || !G_ActorHasSkill(shop, "Asud")) return;
    G_InitUnitStock(shop);
    index = G_FindUnitStockEntry(shop, unit_id);
    if (index < 0) return;
    if ((DWORD)index + 1u < shop->stock.unit_count)
        memmove(&shop->stock.units[index], &shop->stock.units[index + 1],
                (shop->stock.unit_count - (DWORD)index - 1u) * sizeof(shop->stock.units[0]));
    shop->stock.unit_count--;
    memset(&shop->stock.units[shop->stock.unit_count], 0, sizeof(shop->stock.units[0]));
}

void G_AddUnitStockAll(DWORD unit_id, LONG current, LONG maximum) {
    FILTER_EDICTS(shop, shop->inuse && G_ActorHasSkill(shop, "Asud"))
        G_AddUnitStock(shop, unit_id, current, maximum);
}

void G_RemoveUnitStockAll(DWORD unit_id) {
    FILTER_EDICTS(shop, shop->inuse && G_ActorHasSkill(shop, "Asud"))
        G_RemoveUnitStock(shop, unit_id);
}

BOOL G_ShopSellsItem(LPEDICT shop, DWORD item_id) {
    return G_IsItemShop(shop) && G_FindShopItemStock(shop, item_id) >= 0;
}

BOOL G_ShopSellsUnit(LPEDICT shop, DWORD unit_id) {
    return G_IsUnitShop(shop) && G_FindShopUnitStock(shop, unit_id) >= 0;
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

/* Builds the visible merchandise card while revalidating patron, stock, and resources. */
BYTE G_GetShopItemButtons(shopItemButtonsParams_t *params) {
    LPGAMECLIENT client = params ? params->client : NULL;
    LPEDICT shop = params ? params->shop : NULL;
    gameCommandButton_t *buttons = params ? params->buttons : NULL;
    BYTE max_buttons = params ? params->max_buttons : 0;
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
        if (shop->stock.items[i].maximum > 0) button->number = (DWORD)MAX(0, shop->stock.items[i].current);

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


BYTE G_GetShopUnitButtons(shopItemButtonsParams_t *params) {
    LPGAMECLIENT client = params ? params->client : NULL;
    LPEDICT shop = params ? params->shop : NULL;
    gameCommandButton_t *buttons = params ? params->buttons : NULL;
    BYTE max_buttons = params ? params->max_buttons : 0;
    LPEDICT patron;
    BYTE count = 0;

    if (!client || !buttons || !max_buttons || !G_CanUseUnitShop(client, shop)) return 0;
    memset(buttons, 0, sizeof(*buttons) * max_buttons);
    G_InitUnitStock(shop);
    patron = G_FindUnitShopPatron(client, shop);

    FOR_LOOP(i, shop->stock.unit_count) {
        char code[5] = {0};
        gameCommandButton_t *button;
        UnitBalance_t const *unit;
        DWORD now;

        if (count >= max_buttons) break;
        G_UpdateUnitStockEntry(shop, i);
        memcpy(code, &shop->stock.units[i].id, 4);
        button = &buttons[count];
        if (!G_BuildCommandButton(shop, code, false, 0, button)) continue;
        button->x = count % 4;
        button->y = count / 4;
        unit = G_UnitBalance(shop->stock.units[i].id);
        if (shop->stock.units[i].maximum > 0) button->number = (DWORD)MAX(0, shop->stock.units[i].current);

        if (!patron) {
            G_DisableShopButton(button, "No eligible purchaser is nearby.");
        } else if (shop->stock.units[i].current <= 0) {
            G_DisableShopButton(button, "Out of stock.");
            if (shop->stock.units[i].delay_end > shop->stock.units[i].delay_start) {
                now = G_Time();
                button->cooldown_start_time = shop->stock.units[i].delay_start;
                button->cooldown_end_time = shop->stock.units[i].delay_end;
                if (now < shop->stock.units[i].delay_end) {
                    button->cooldown = (FLOAT)(shop->stock.units[i].delay_end - now) /
                        (FLOAT)(shop->stock.units[i].delay_end - shop->stock.units[i].delay_start);
                }
            }
        } else if (unit && client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] < (DWORD)MAX(0, unit->goldCost)) {
            G_DisableShopButton(button, "Not enough gold.");
        } else if (unit && client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] < (DWORD)MAX(0, unit->lumberCost)) {
            G_DisableShopButton(button, "Not enough lumber.");
        } else if (unit && !G_PlayerHasFoodFor(client, MAX(0, unit->foodUsed))) {
            G_DisableShopButton(button, "Not enough food.");
        }
        count++;
    }
    return count;
}

BYTE G_GetShopButtons(shopItemButtonsParams_t *params) {
    gameCommandButton_t temp[12];
    shopItemButtonsParams_t sub;
    BYTE count = 0, added;

    if (!params || !params->buttons || !params->max_buttons) return 0;
    memset(params->buttons, 0, sizeof(*params->buttons) * params->max_buttons);
    sub = *params;
    sub.buttons = temp;

    if (G_CanUseItemShop(params->client, params->shop) && count < params->max_buttons) {
        sub.max_buttons = (BYTE)MIN((DWORD)(sizeof(temp) / sizeof(temp[0])), (DWORD)(params->max_buttons - count));
        added = G_GetShopItemButtons(&sub);
        FOR_LOOP(i, added) {
            params->buttons[count] = temp[i];
            params->buttons[count].x = count % 4;
            params->buttons[count].y = count / 4;
            count++;
        }
    }
    if (G_CanUseUnitShop(params->client, params->shop) && count < params->max_buttons) {
        sub.max_buttons = (BYTE)MIN((DWORD)(sizeof(temp) / sizeof(temp[0])), (DWORD)(params->max_buttons - count));
        added = G_GetShopUnitButtons(&sub);
        FOR_LOOP(i, added) {
            params->buttons[count] = temp[i];
            params->buttons[count].x = count % 4;
            params->buttons[count].y = count / 4;
            count++;
        }
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

BOOL G_ShopPurchaseUnit(LPEDICT clent, LPEDICT shop, DWORD unit_id) {
    LPGAMECLIENT client = clent ? clent->client : NULL;
    LPEDICT patron;
    LPEDICT unit_ent;
    UnitBalance_t const *unit;
    UnitUI_t const *ui;
    LONG stock_index;
    DWORD gold;
    DWORD lumber;
    VECTOR2 exit_origin;
    FLOAT exit_angle;

    if (!G_CanUseUnitShop(client, shop)) return false;
    stock_index = G_FindShopUnitStock(shop, unit_id);
    if (stock_index < 0) return false;
    patron = G_FindUnitShopPatron(client, shop);
    if (!patron) {
        G_ShowCommandErrorText(clent, "No eligible purchaser is nearby.");
        return false;
    }

    unit = G_UnitBalance(unit_id);
    ui = G_UnitUI(unit_id);
    if (!unit || unit->id != unit_id || !ui || !ui->modelFile || !*ui->modelFile) return false;
    if (shop->stock.units[stock_index].current <= 0) {
        G_ShowCommandErrorText(clent, "Out of stock.");
        return false;
    }

    gold = (DWORD)MAX(0, unit->goldCost);
    lumber = (DWORD)MAX(0, unit->lumberCost);
    if (client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] < gold) {
        G_ShowCommandErrorText(clent, "Not enough gold.");
        return false;
    }
    if (client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] < lumber) {
        G_ShowCommandErrorText(clent, "Not enough lumber.");
        return false;
    }
    if (!G_PlayerHasFoodFor(client, MAX(0, unit->foodUsed))) {
        G_ShowCommandErrorText(clent, "Not enough food.");
        return false;
    }

    /* Neutral-unit sales are immediate rather than a training queue. Spawn
     * without Birth, find the same deterministic legal exit used by trained
     * units, then activate food only after placement succeeds. */
    unit_ent = SP_SpawnAtLocationNoBirth(unit_id, client->ps.number, &shop->s.origin2);
    if (!unit_ent) return false;
    if (!SP_FindUnitExitPosition(shop, unit_ent, &exit_origin, &exit_angle)) {
        G_FreeEdict(unit_ent);
        return false;
    }

    unit_ent->s.origin2 = exit_origin;
    unit_ent->s.origin.x = exit_origin.x;
    unit_ent->s.origin.y = exit_origin.y;
    unit_ent->s.origin.z = CM_GetHeightAtPoint(exit_origin.x, exit_origin.y);
    unit_ent->s.angle = exit_angle;
    G_ActivateUnitFood(unit_ent);
    if (unit_ent->stand) unit_ent->stand(unit_ent);
    gi.LinkEntity(unit_ent);

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= gold;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= lumber;
    G_RefreshResourceBar(clent);
    shop->stock.units[stock_index].current--;
    G_StartUnitRestock(shop, (DWORD)stock_index);
    G_InvalidateCommands(client);
    return true;
}

static FLOAT G_ShopPawnRate(void) {
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", "PawnItemRate");
    FLOAT rate;

    if (!value) {
        if (!shop_warned_pawn_rate) {
            fprintf(stderr, "WC3 shop: missing Misc.PawnItemRate; using 0.5\n");
            shop_warned_pawn_rate = true;
        }
        rate = 0.5f;
    } else rate = atof(value);
    return MAX(0.0f, rate);
}

static FLOAT G_ShopGiveItemRange(void) {
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", "GiveItemRange");
    FLOAT range;

    if (!value) {
        if (!shop_warned_give_range) {
            fprintf(stderr, "WC3 shop: missing Misc.GiveItemRange; using %.0f\n", ITEM_DROP_RANGE);
            shop_warned_give_range = true;
        }
        range = ITEM_DROP_RANGE;
    } else range = atof(value);
    return MAX(0.0f, range);
}

/* Sells a carried pawnable item only after validating shop authority and authored range. */
BOOL G_ShopPawnItem(shopPawnItemParams_t *params) {
    LPEDICT clent = params ? params->clent : NULL;
    LPEDICT shop = params ? params->shop : NULL;
    LPEDICT carrier = params ? params->carrier : NULL;
    LPEDICT item = params ? params->item : NULL;
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
