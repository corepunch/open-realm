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
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", name);
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

    /* Player/client edicts occupy the reserved [0, max_clients) range and are
     * intentionally not normal in-use gameplay entities.  Refresh inventory by
     * connection state instead of edict->inuse, otherwise a successful pickup
     * never re-sends LAYER_INVENTORY to the selecting client. */
    FOR_LOOP(i, game.max_clients) {
        LPEDICT player = globals.edicts + i;
        if (player->client && player->client->connected &&
            G_IsEntitySelected(player->client, unit)) {
            G_RefreshInventoryLayer(player);
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
        G_ShowCommandErrorText(player, "Inventory is full.");
    }
}

LPCSTR G_ItemAbilityList(LPCEDICT item) {
    LPCSTR abilities;

    if (!item || !item->class_id) return NULL;

    /* abilList is authored on ItemData.slk. Prefer the normalized typed row:
     * FindConfigValue() searches the TXT/INI configuration tables and cannot
     * be relied on to find this SLK field. Keep the config lookup only as a
     * compatibility fallback for hand-authored/custom data that did not make
     * it into the typed item row. */
    if (item->data.ItemData && item->data.ItemData->abilList && *item->data.ItemData->abilList)
        return item->data.ItemData->abilList;

    abilities = FindConfigValue(GetClassName(item->class_id), "abilList");
    return abilities && *abilities ? abilities : NULL;
}

/* ItemData stores passive effects as an ability list; the item rawcode itself
 * is not an ability code. */
static void G_ApplyItemStats(LPEDICT unit, LPCEDICT item, BOOL apply) {
    LPCSTR abilities = G_ItemAbilityList(item);
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

    if (!self || !(model = self->data.ItemData->file)) {
        return;
    }
    strlcpy(model_filename, model, sizeof(model_filename));
    self->s.model = G_RegisterModel(model_filename);
    scale = self->data.ItemData->scale;
    if (scale > 0) {
        self->s.scale = scale;
    }
    self->s.radius = self->data.ItemData->selectionSize;
#ifndef USE_SHADOWMAPS
    self->s.shadow = G_LoadShadowTexture(Stb_IniCacheFind(&game.config.misc, "Misc", "ItemShadowFile"), false);
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
    self->item.charges = (DWORD)MAX(0, (LONG)(G_ItemData(self->class_id) ? G_ItemData(self->class_id)->uses : 0));
}

BOOL G_IsItem(LPCEDICT item) {
    if (!item || !item->inuse || !item->class_id) {
        return false;
    }
    return item->item.in_world || item->item.carrier || item->data.ItemData->file != NULL;
}

static DWORD G_InventoryRequiredUpgrade(DWORD ability_id) {
    /* Stock unit-inventory abilities are present on the unit before the race
     * Backpack upgrade is researched. UpgradeData effects are not normalized
     * yet, so keep this small stock dependency table explicit until that data
     * becomes authoritative here. Hero/custom AInv-derived abilities remain
     * immediately available. */
    switch (ability_id) {
        case MAKEFOURCC('A','i','h','n'): return MAKEFOURCC('R','h','p','m');
        case MAKEFOURCC('A','i','o','n'):
        case MAKEFOURCC('A','p','a','k'): return MAKEFOURCC('R','o','p','m');
        case MAKEFOURCC('A','i','e','n'): return MAKEFOURCC('R','e','p','m');
        case MAKEFOURCC('A','i','u','n'): return MAKEFOURCC('R','u','p','m');
        default: return 0;
    }
}

static BOOL G_InventoryAbilityAvailable(LPCEDICT unit, LPCSTR ability) {
    DWORD ability_id;
    DWORD required_upgrade;
    LPGAMECLIENT owner;

    if (!unit || !ability || strlen(ability) != 4) return false;
    memcpy(&ability_id, ability, sizeof(ability_id));
    required_upgrade = G_InventoryRequiredUpgrade(ability_id);
    if (!required_upgrade) return true;

    owner = G_GetPlayerClientByNumber(unit->s.player);
    if (!owner || owner->ps.number != unit->s.player) return false;
    return G_GetPlayerTechResearchedLevel(owner, required_upgrade) > 0;
}

static DWORD G_InventoryAbilityCapacity(LPCEDICT unit, LPCSTR ability) {
    LONG capacity;

    if (!unit || !ability || strlen(ability) != 4) return 0;
    capacity = (LONG)AB_Data(ability, 1, 1); /* inv1 / Item Capacity */
    if (capacity <= 0) {
        fprintf(stderr, "G_InventoryCapacity: %.4s inventory ability %.4s has invalid inv1=%ld\n",
                (char *)&unit->class_id, ability, (long)capacity);
        return 0;
    }
    return (DWORD)MIN(capacity, MAX_INVENTORY);
}

DWORD G_InventoryCapacity(LPCEDICT unit) {
    LPCSTR abilities = NULL;
    BOOL has_inventory_ability = false;

    if (!unit || !unit->inuse) return 0;
    if (unit->data.UnitAbilities) abilities = unit->data.UnitAbilities->abilList;
    if (abilities) {
        PARSE_LIST(abilities, abil, parse_segment) {
            /* Typed AbilityData resolves custom inventory abilities without returning to the removed sheet cache. */
            if (G_AbilityCodeName(abil) != MAKEFOURCC('A','I','n','v')) continue;
            has_inventory_ability = true;
            if (!G_InventoryAbilityAvailable(unit, abil)) continue;
            return G_InventoryAbilityCapacity(unit, abil);
        }
    }

    /* Warsmash restores the classic ROC hero contract by adding the stock
     * AInv ability when a hero has no inventory ability authored in its normal
     * ability list.  ROC map formats are <= 24; TFT/custom data may intentionally
     * omit inventory, so do not synthesize AInv there. */
    if (!has_inventory_ability && level.mapinfo && level.mapinfo->fileFormat > 0 &&
        level.mapinfo->fileFormat <= 24 && G_UnitIsHero(unit)) {
        return G_InventoryAbilityCapacity(unit, "AInv");
    }
    return 0;
}

BOOL G_UnitHasInventory(LPEDICT unit) {
    return G_InventoryCapacity(unit) > 0;
}

DWORD G_ItemCharges(LPCEDICT item) {
    return G_IsItem(item) ? item->item.charges : 0;
}

void G_SetItemCharges(LPEDICT item, DWORD charges) {
    if (!G_IsItem(item) || item->item.charges == charges) return;
    item->item.charges = charges;
    if (item->item.carrier) G_RefreshInventoryUI(item->item.carrier);
}

void G_ConsumeItemCharge(LPEDICT item) {
    if (!G_IsItem(item) || !item->data.ItemData || item->item.charges == 0) return;

    /* All charged item uses decrement charges. Perishable only controls the
     * zero-charge lifetime: Warsmash removes perishables, while reusable
     * zero-charge items remain held. Avoid publishing a transient zero-charge
     * copy immediately before final perishable removal. */
    if (item->item.charges == 1 && item->data.ItemData->perishable) {
        item->item.charges = 0;
        G_RemoveItem(item);
        return;
    }
    G_SetItemCharges(item, item->item.charges - 1);
}

LONG G_FindFreeInventorySlot(LPCEDICT unit) {
    DWORD capacity = G_InventoryCapacity(unit);

    FOR_LOOP(i, capacity) if (!unit->inventory[i]) return (LONG)i;
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
    if (slot >= G_InventoryCapacity(unit) || !G_CanPickupItem(unit, item) || unit->inventory[slot]) {
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
    BOOL added;

    if (slot < 0) {
        return false;
    }
    added = G_AddItemToSlot(unit, item, (DWORD)slot);
    if (added) G_QueueOwnerSoundAlias(unit, "ItemGet");
    return added;
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
    G_QueueOwnerSoundAlias(unit, "ItemDrop");
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
    LPEDICT clent;
    LPCSTR abilities;

    if (!unit || slot >= G_InventoryCapacity(unit) || unit->s.player >= MAX_PLAYERS) {
        return;
    }
    item = unit->inventory[slot];
    if (!item) {
        return;
    }

    clent = G_GetPlayerEntityByNumber(unit->s.player);
    if (!clent || !clent->client) return;
    abilities = G_ItemAbilityList(item);
    if (!abilities) return;

    PARSE_LIST(abilities, ability_name, parse_segment) {
        ability_t const *ability = FindAbilityForCommand(ability_name);
        BOOL succeeded = false;

        if (!ability) continue;
        clent->client->menu.ability_code = *((DWORD const *)ability_name);
        if (ability->item_use) {
            succeeded = ability->item_use(clent);
        } else if (ability->cmd) {
            ability->cmd(clent);
            return;
        } else {
            continue;
        }

        if (succeeded) {
            G_PublishEvent(unit, EVENT_PLAYER_UNIT_USE_ITEM);
            G_PublishEvent(unit, EVENT_UNIT_USE_ITEM);
            G_ConsumeItemCharge(item);
        }
        return;
    }
}
