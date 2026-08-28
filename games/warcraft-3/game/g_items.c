#include "g_local.h"
#include "skills/s_skills.h"

/* Passive item-effect hooks remain centralized here so every inventory exit
 * reverses the corresponding inventory entry. Effect coverage is expanded in
 * a later item-system phase. */
void item_stat_apply(LPEDICT unit, DWORD item_code);
void item_stat_remove(LPEDICT unit, DWORD item_code);

/* Keep the native itemtype mapping in one table shared by GetItemType and the
 * random-item selectors. */
DWORD G_ItemTypeFromClass(LPCSTR cls) {
    static struct { LPCSTR name; DWORD type; } const types[] = {
        { "Permanent", 0 }, { "Charged", 1 }, { "PowerUp", 2 }, { "Artifact", 3 },
        { "Purchasable", 4 }, { "Campaign", 5 }, { "Miscellaneous", 6 },
    };
    if (cls) FOR_LOOP(i, sizeof(types) / sizeof(*types)) if (!strcasecmp(cls, types[i].name)) return types[i].type;
    return 7; /* ITEM_TYPE_UNKNOWN */
}

static FLOAT G_MiscVectorValue(LPCSTR name, DWORD index) {
    LPCSTR value = FS_FindSheetCell(game.config.misc, "Misc", name);
    if (!value) {
        return 0;
    }

    for (DWORD i = 0; i < index; i++) {
        value = strchr(value, ',');
        if (!value) {
            return 0;
        }
        value++;
    }

    return atof(value);
}

static void G_RefreshInventoryUI(LPEDICT unit) {
    if (!unit) {
        return;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT player = globals.edicts + i;
        if (player->inuse && player->client && G_IsEntitySelected(player->client, unit)) {
            Get_Portrait_f(player);
        }
    }
}

static void G_ShowInventoryFull(LPEDICT unit) {
    LPEDICT player;

    if (!unit || unit->s.player >= MAX_PLAYERS || !level.mapinfo) {
        return;
    }
    player = G_GetPlayerEntityByNumber(unit->s.player);
    if (player && player->client) {
        UI_ShowText(player, &MAKE(VECTOR2, 0, 0), "Inventory is full.", 2.0f);
    }
}

/* ItemData stores passive effects as an ability list; the item rawcode itself
 * is not an ability code. */
static void G_ApplyItemStats(LPEDICT unit, LPCEDICT item, BOOL apply) {
    LPCSTR abilities = FindConfigValue(GetClassName(item->class_id), "abilList");
    if (!abilities || !*abilities) return;
    PARSE_LIST(abilities, ability, parse_segment) {
        DWORD code = *((DWORD const *)ability);
        apply ? item_stat_apply(unit, code) : item_stat_remove(unit, code);
    }
}

void SP_SpawnItem(LPEDICT self) {
    PATHSTR model_filename;
    LPCSTR model;
    FLOAT scale;

    if (!self || !(model = ITEM_FILE(self->class_id))) {
        return;
    }
    strlcpy(model_filename, model, sizeof(model_filename));
    self->s.model = G_RegisterModel(model_filename);
    scale = ITEM_SCALE(self->class_id);
    if (scale > 0) {
        self->s.scale = scale;
    }
    self->s.radius = ITEM_SELECTION_SIZE(self->class_id);
#ifndef USE_SHADOWMAPS
    self->s.shadow = G_LoadShadowTexture(FS_FindSheetCell(game.config.misc, "Misc", "ItemShadowFile"), false);
    self->s.shadow_rect = ShadowPackRect(
        G_MiscVectorValue("ItemShadowOffset", 0),
        G_MiscVectorValue("ItemShadowOffset", 1),
        G_MiscVectorValue("ItemShadowSize", 0),
        G_MiscVectorValue("ItemShadowSize", 1));
#endif
    self->movetype = MOVETYPE_NONE;
    self->targtype = TARG_ITEM;
    self->item.carrier = NULL;
    self->item.inventory_slot = -1;
    self->item.in_world = true;
}

BOOL G_IsItem(LPCEDICT item) {
    if (!item || !item->inuse || !item->class_id) {
        return false;
    }
    return item->item.in_world || item->item.carrier || ITEM_FILE(item->class_id) != NULL;
}

BOOL G_UnitHasInventory(LPEDICT unit) {
    return unit && unit->inuse && G_ActorHasSkill(unit, "AInv");
}

LONG G_FindFreeInventorySlot(LPCEDICT unit) {
    if (!unit) {
        return -1;
    }
    FOR_LOOP(i, MAX_INVENTORY) {
        if (!unit->inventory[i]) {
            return (LONG)i;
        }
    }
    return -1;
}

BOOL G_CanPickupItem(LPEDICT unit, LPEDICT item) {
    if (!G_UnitHasInventory(unit) || M_IsDead(unit) || !G_IsItem(item)) {
        return false;
    }
    return item->item.in_world && !item->item.carrier && item->item.inventory_slot == -1 &&
           !(item->s.renderfx & RF_HIDDEN) && !(item->svflags & SVF_NOCLIENT);
}

BOOL G_AddItemToSlot(LPEDICT unit, LPEDICT item, DWORD slot) {
    if (slot >= MAX_INVENTORY || !G_CanPickupItem(unit, item) || unit->inventory[slot]) {
        return false;
    }

    gi.UnlinkEntity(item);
    item->s.renderfx |= RF_HIDDEN;
    item->svflags |= SVF_NOCLIENT;
    item->item.in_world = false;
    item->item.carrier = unit;
    item->item.inventory_slot = (LONG)slot;
    unit->inventory[slot] = item;
    G_ApplyItemStats(unit, item, true);
    G_RefreshInventoryUI(unit);
    return true;
}

BOOL G_PickupItem(LPEDICT unit, LPEDICT item) {
    LONG slot = G_FindFreeInventorySlot(unit);

    if (slot < 0) {
        return false;
    }
    return G_AddItemToSlot(unit, item, (DWORD)slot);
}

static void G_StopPickupOrder(LPEDICT unit) {
    unit->goalentity = NULL;
    if (unit->stand) {
        unit->stand(unit);
    } else {
        unit_stand(unit);
    }
}

static void G_PickupItemThink(LPEDICT unit) {
    LPEDICT item = unit->goalentity;
    FLOAT distance;
    FLOAT move_distance;

    if (!G_CanPickupItem(unit, item)) {
        G_StopPickupOrder(unit);
        return;
    }
    if (G_FindFreeInventorySlot(unit) < 0) {
        G_ShowInventoryFull(unit);
        G_StopPickupOrder(unit);
        return;
    }

    distance = M_DistanceToGoal(unit);
    if (distance <= ITEM_PICKUP_RANGE) {
        if (!G_PickupItem(unit, item) && G_FindFreeInventorySlot(unit) < 0) {
            G_ShowInventoryFull(unit);
        }
        G_StopPickupOrder(unit);
        return;
    }

    move_distance = unit_movedistance(unit);
    if (move_is_blocked(unit, distance, move_distance)) {
        G_StopPickupOrder(unit);
        return;
    }
    unit_changeangle(unit);
    unit_moveindirection(unit);
}

static umove_t item_move_pickup = { "walk", G_PickupItemThink, NULL, &a_inventory };

BOOL G_OrderPickupItem(LPEDICT unit, LPEDICT item) {
    if (!G_CanPickupItem(unit, item) || (unit->aiflags & AI_IMMOBILE)) {
        return false;
    }
    if (G_FindFreeInventorySlot(unit) < 0) {
        G_ShowInventoryFull(unit);
        return false;
    }

    unit->goalentity = item;
    move_reset_progress(unit);
    unit_setmove(unit, &item_move_pickup);
    return true;
}

BOOL G_DropItemAt(LPEDICT unit, DWORD slot, LPCVECTOR2 position) {
    LPEDICT item;

    if (!unit || !position || slot >= MAX_INVENTORY) {
        return false;
    }
    item = unit->inventory[slot];
    if (!G_IsItem(item) || item->item.carrier != unit || item->item.inventory_slot != (LONG)slot ||
        item->item.in_world) {
        return false;
    }

    G_ApplyItemStats(unit, item, false);
    unit->inventory[slot] = NULL;
    item->item.carrier = NULL;
    item->item.inventory_slot = -1;
    item->item.in_world = true;
    item->s.origin.x = position->x;
    item->s.origin.y = position->y;
    item->s.origin.z = CM_GetHeightAtPoint(position->x, position->y);
    item->s.origin2 = *position;
    item->s.renderfx &= ~RF_HIDDEN;
    item->svflags &= ~SVF_NOCLIENT;
    gi.LinkEntity(item);
    G_RefreshInventoryUI(unit);
    return true;
}

BOOL G_DropItem(LPEDICT unit, DWORD slot) {
    if (!unit) {
        return false;
    }
    return G_DropItemAt(unit, slot, &unit->s.origin2);
}

void G_RemoveItem(LPEDICT item) {
    LPEDICT carrier;
    LONG slot;

    if (!item || !item->inuse) {
        return;
    }
    carrier = item->item.carrier;
    slot = item->item.inventory_slot;
    if (carrier && carrier->inuse) {
        if (slot < 0 || slot >= MAX_INVENTORY || carrier->inventory[slot] != item) {
            slot = -1;
            FOR_LOOP(i, MAX_INVENTORY) {
                if (carrier->inventory[i] == item) {
                    slot = (LONG)i;
                    break;
                }
            }
        }
        if (slot >= 0) {
            G_ApplyItemStats(carrier, item, false);
            carrier->inventory[slot] = NULL;
        }
        G_RefreshInventoryUI(carrier);
    }
    item->item.carrier = NULL;
    item->item.inventory_slot = -1;
    item->item.in_world = false;
    G_FreeEdict(item);
}

/* Use an item in inventory by slot index. Calls the item's ability cmd handler. */
void G_UseItem(LPEDICT unit, DWORD slot) {
    LPEDICT item;
    ability_t const *abil;

    if (!unit || !unit->client || slot >= MAX_INVENTORY) {
        return;
    }
    item = unit->inventory[slot];
    if (!item) {
        return;
    }
    /* Look up the item's ability code in the ability registry.
     * Item abilities use the item's class_id as their ability code. */
    abil = FindAbilityByClassname((LPCSTR)&item->class_id);
    if (abil && abil->cmd) {
        unit->client->menu.ability_code = item->class_id;
        abil->cmd(unit);
    }
}
