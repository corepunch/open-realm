#include "g_local.h"
#include "skills/s_skills.h"

/* Keep the native itemtype mapping in one table shared by GetItemType and the
 * random-item selectors. */
uint32_t G_ItemTypeFromClass(cstring_t cls) {
    static struct { cstring_t name; uint32_t type; } const types[] = {
        { "Permanent", 0 }, { "Charged", 1 }, { "PowerUp", 2 }, { "Artifact", 3 },
        { "Purchasable", 4 }, { "Campaign", 5 }, { "Miscellaneous", 6 },
    };
    if (cls) FOR_LOOP(i, sizeof(types) / sizeof(*types)) if (!strcasecmp(cls, types[i].name)) return types[i].type;
    return 7; /* ITEM_TYPE_UNKNOWN */
}

static float G_MiscVectorValue(cstring_t name, uint32_t index) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "Misc", name);
    if (!value) {
        return 0;
    }

    for (uint32_t i = 0; i < index; i++) {
        value = strchr(value, ',');
        if (!value) {
            return 0;
        }
        value++;
    }

    return atof(value);
}

static void G_RefreshInventoryUI(edict_t * unit) {
    if (!unit) {
        return;
    }

    /* Player/client edicts occupy the reserved [0, max_clients) range and are
     * intentionally not normal in-use gameplay entities.  Refresh inventory by
     * connection state instead of edict->inuse, otherwise a successful pickup
     * never re-sends LAYER_INVENTORY to the selecting client. */
    FOR_LOOP(i, game.max_clients) {
        edict_t * player = globals.edicts + i;
        if (player->client && player->client->connected &&
            G_IsEntitySelected(player->client, unit)) {
            G_RefreshInventoryLayer(player);
        }
    }
}

static void G_ShowInventoryFull(edict_t * unit) {
    edict_t * player;

    if (!unit || unit->s.player >= MAX_PLAYERS || !level.mapinfo) {
        return;
    }
    player = G_GetPlayerEntityByNumber(unit->s.player);
    if (player && player->client) {
        G_ShowCommandErrorText(player, "Inventory is full.");
    }
}

cstring_t G_ItemAbilityList(edict_t const * item) {
    cstring_t abilities;

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
static void G_ApplyItemStats(edict_t * unit, edict_t const * item, bool apply) {
    cstring_t abilities;

    /* Item-use permission gates gaining passive item effects, but removal must
     * always reverse effects that were already applied. The permission can
     * change while an item is carried (custom abilities/tech/scripted data),
     * and blocking A_ITEM_REMOVE would leak the old stat bonus after a drop. */
    if (apply && !G_InventoryCanUseItems(unit)) return;
    abilities = G_ItemAbilityList(item);
    if (!abilities || !*abilities) return;
    PARSE_LIST(abilities, ability, parse_segment) {
        abilityitem_t entry = S_AbilityItem(FS_SLKKey(ability));
        abilityCall_t call = MAKE(abilityCall_t, .item = &entry);
        S_AbilityMessage(unit, apply ? A_ITEM_ADD : A_ITEM_REMOVE, &call);
    }
}

void SP_SpawnItem(edict_t * self) {
    PATHSTR model_filename;
    cstring_t model;
    float scale;

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
    self->item.drop_id = 0;
    self->item.charges = (uint32_t)MAX(0, (int32_t)(G_ItemData(self->class_id) ? G_ItemData(self->class_id)->uses : 0));
}

bool G_IsItem(edict_t const * item) {
    if (!item || !item->inuse || !item->class_id) {
        return false;
    }
    /* The item state shares storage with other entity kinds; classify by the
     * target type before reading it so destructables cannot be mistaken for
     * items and dereference the wrong data union member. */
    return item->targtype == TARG_ITEM &&
        (item->item.in_world || item->item.carrier || (item->data.ItemData && item->data.ItemData->file));
}

/* The item currently being visited by EnumItemsInRect, read back by the
 * GetEnumItem native inside the enum action (mirrors currentdestructable). */
edict_t * currentenumitem = NULL;

static uint32_t G_InventoryRequiredUpgrade(uint32_t ability_id) {
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

static bool G_InventoryAbilityAvailable(edict_t const * unit, cstring_t ability) {
    uint32_t ability_id;
    uint32_t required_upgrade;
    gameClient_t * owner;

    if (!unit || !ability || strlen(ability) != 4) return false;
    memcpy(&ability_id, ability, sizeof(ability_id));
    required_upgrade = G_InventoryRequiredUpgrade(ability_id);
    if (!required_upgrade) return true;

    owner = G_GetPlayerClientByNumber(unit->s.player);
    if (!owner || owner->ps.number != unit->s.player) return false;
    return G_GetPlayerTechResearchedLevel(owner, required_upgrade) > 0;
}

/* Classic ROC has a built-in six-slot hero inventory and no AInv data row.
 * AIab/AIa6 are attribute bonuses, never inventory aliases. An authored AInv
 * row (including zero capacity) remains authoritative when present. */
static bool G_UsesClassicHeroInventory(edict_t const * unit, cstring_t ability) {
    return !strcmp(ability, "AInv") && G_IsReignOfChaosMap(level.mapinfo) &&
        G_UnitIsHero(unit) && !G_AbilityDataName(ability)->id;
}

static bool G_InventoryAbilityFlag(edict_t const * unit, uint32_t data_index, bool roc_hero_default) {
    cstring_t abilities = NULL;
    bool has_inventory_ability = false;

    if (!unit || !unit->inuse) return false;
    if (unit->data.UnitAbilities) abilities = unit->data.UnitAbilities->abilList;
    if (abilities) {
        PARSE_LIST(abilities, abil, parse_segment) {
            abilityLevel_t const *ability_level;
            uint32_t const code = G_AbilityCodeName(abil);

            if (code != MAKEFOURCC('A','I','n','v')) continue;
            has_inventory_ability = true;
            if (!G_InventoryAbilityAvailable(unit, abil)) continue;
            if (G_UsesClassicHeroInventory(unit, abil)) return roc_hero_default;
            ability_level = G_AbilityLevel(FS_SLKKey(abil), 1);
            return ability_level &&
                   data_index < sizeof(ability_level->data) / sizeof(ability_level->data[0]) &&
                   ability_level->data[data_index].number != 0.0f;
        }
    }

    /* Reign of Chaos synthesizes the normal hero inventory when no inventory
     * ability is authored. That stock hero inventory can use items and does
     * not drop them on death; callers supply the appropriate default. */
    if (!has_inventory_ability && G_IsReignOfChaosMap(level.mapinfo) && G_UnitIsHero(unit))
        return roc_hero_default;
    return false;
}

bool G_InventoryCanUseItems(edict_t const * unit) {
    /* inv3 / DataC: heroes may use/equip item abilities; Backpack carriers
     * such as Aihn have this disabled and therefore only transport items. */
    return G_InventoryAbilityFlag(unit, 2, true);
}

bool G_InventoryCanGetItems(edict_t const * unit) {
    /* inv4 / DataD gates player-issued pickup orders. Scripted/native item
     * insertion deliberately uses G_PickupItem/G_AddItemToSlot directly,
     * matching Warsmash's giveItem path which bypasses canGetItems. */
    return G_InventoryAbilityFlag(unit, 3, true);
}

bool G_InventoryCanDropItems(edict_t const * unit) {
    /* inv5 / DataE gates player-issued drop/handoff orders. Direct native
     * removal remains available through G_DropItem/G_DropItemAt. */
    return G_InventoryAbilityFlag(unit, 4, true);
}

static bool G_InventoryDropsItemsOnDeath(edict_t const * unit) {
    /* inv2 / DataB: ordinary Backpack carriers drop their contents, while the
     * stock hero inventory retains items across death/revival. */
    return G_InventoryAbilityFlag(unit, 1, false);
}

static uint32_t G_InventoryAbilityCapacity(edict_t const * unit, cstring_t ability) {
    int32_t capacity;

    if (!unit || !ability || strlen(ability) != 4) return 0;
    capacity = (int32_t)AB_Data(ability, 1, 1); /* inv1 / Item Capacity */
    if (G_UsesClassicHeroInventory(unit, ability)) return MAX_INVENTORY;
    if (capacity < 0 || !G_AbilityDataName(ability)->id) {
        fprintf(stderr, "G_InventoryCapacity: %.4s inventory ability %.4s has invalid inv1=%ld\n",
                (char *)&unit->class_id, ability, (long)capacity);
        return 0;
    }
    return (uint32_t)MIN(capacity, MAX_INVENTORY);
}

uint32_t G_InventoryCapacity(edict_t const * unit) {
    cstring_t abilities = NULL;
    bool has_inventory_ability = false;

    if (!unit || !unit->inuse) return 0;
    if (unit->data.UnitAbilities) abilities = unit->data.UnitAbilities->abilList;
    if (abilities) {
        PARSE_LIST(abilities, abil, parse_segment) {
            uint32_t const code = G_AbilityCodeName(abil);
            if (code != MAKEFOURCC('A','I','n','v')) continue;
            has_inventory_ability = true;
            if (!G_InventoryAbilityAvailable(unit, abil)) continue;
            return G_InventoryAbilityCapacity(unit, abil);
        }
    }

    /* Restore the classic hero inventory when no inventory ability is authored.
     * ROC map formats are <= 24; TFT/custom data may
     * intentionally omit inventory, so do not synthesize there. */
    if (!has_inventory_ability && G_IsReignOfChaosMap(level.mapinfo) && G_UnitIsHero(unit))
        return G_InventoryAbilityCapacity(unit, "AInv");
    return 0;
}

bool G_UnitHasInventory(edict_t * unit) {
    return G_InventoryCapacity(unit) > 0;
}

uint32_t G_ItemCharges(edict_t const * item) {
    return G_IsItem(item) ? item->item.charges : 0;
}

void G_SetItemCharges(edict_t * item, uint32_t charges) {
    if (!G_IsItem(item) || item->item.charges == charges) return;
    item->item.charges = charges;
    if (item->item.carrier) G_RefreshInventoryUI(item->item.carrier);
}

void G_ConsumeItemCharge(edict_t * item) {
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

int32_t G_FindFreeInventorySlot(edict_t const * unit) {
    uint32_t capacity = G_InventoryCapacity(unit);

    FOR_LOOP(i, capacity) if (!unit->inventory[i]) return (int32_t)i;
    return -1;
}

bool G_CanPickupItem(edict_t * unit, edict_t * item) {
    if (!G_UnitHasInventory(unit) || M_IsDead(unit) || !G_IsItem(item)) {
        return false;
    }
    return item->item.in_world && !item->item.carrier && item->item.inventory_slot == -1 &&
           !(item->s.renderfx & RF_HIDDEN) && !(item->svflags & SVF_NOCLIENT);
}

bool G_AddItemToSlotInternal(edict_t * unit, edict_t * item, uint32_t slot, bool publish_event) {
    if (slot >= G_InventoryCapacity(unit) || !G_CanPickupItem(unit, item) || unit->inventory[slot]) {
        return false;
    }

    gi.UnlinkEntity(item);
    item->s.renderfx |= RF_HIDDEN;
    item->svflags |= SVF_NOCLIENT;
    item->item.in_world = false;
    item->item.carrier = unit;
    item->item.inventory_slot = (int32_t)slot;
    unit->inventory[slot] = item;
    G_ApplyItemStats(unit, item, true);
    G_RefreshInventoryUI(unit);
    if (publish_event) {
        G_PublishEventWithSource(unit, EVENT_PLAYER_UNIT_PICKUP_ITEM, item);
        G_PublishEventWithSource(unit, EVENT_UNIT_PICKUP_ITEM, item);
    }
    return true;
}

bool G_AddItemToSlot(edict_t * unit, edict_t * item, uint32_t slot) {
    return G_AddItemToSlotInternal(unit, item, slot, true);
}

bool G_PickupItem(edict_t * unit, edict_t * item) {
    int32_t slot = G_FindFreeInventorySlot(unit);
    bool added;

    if (slot < 0) {
        return false;
    }
    added = G_AddItemToSlot(unit, item, (uint32_t)slot);
    if (added) G_QueueOwnerSoundAlias(unit, "ItemGet");
    return added;
}

static void G_StopPickupOrder(edict_t * unit) {
    unit->goalentity = NULL;
    if (unit->stand) {
        unit->stand(unit);
    } else {
        unit_stand(unit);
    }
}

static void G_PickupItemThink(edict_t * unit) {
    edict_t * item = unit->goalentity;
    float distance;
    float move_distance;

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

static umove_t item_move_pickup = { "walk", G_PickupItemThink, NULL, CAbilityInventory };

bool G_OrderPickupItem(edict_t * unit, edict_t * item) {
    if (!G_InventoryCanGetItems(unit) || !G_CanPickupItem(unit, item) ||
        (unit->aiflags & AI_IMMOBILE)) {
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

static bool G_DropItemAtInternal(edict_t * unit, uint32_t slot, vector2_t const * position, bool play_sound) {
    edict_t * item;
    vector2_t drop_position;

    if (!unit || !position || slot >= (uint32_t)G_InventoryCapacity(unit)) {
        return false;
    }
    item = unit->inventory[slot];
    if (!G_IsItem(item) || item->item.carrier != unit || item->item.inventory_slot != (int32_t)slot ||
        item->item.in_world) {
        return false;
    }

    /* Warsmash finishes a point drop through setPointAndCheckUnstuck rather
     * than assigning the requested coordinates blindly. Reuse OpenRealm's
     * deterministic WC3 unstuck search so blocked terrain resolves to a legal
     * nearby position while preserving the requested point as its fallback. */
    drop_position = *position;
    G_FindUnitUnstuckPosition(item, position, &drop_position);

    G_ApplyItemStats(unit, item, false);
    unit->inventory[slot] = NULL;
    item->item.carrier = NULL;
    item->item.inventory_slot = -1;
    item->item.in_world = true;
    item->s.origin.x = drop_position.x;
    item->s.origin.y = drop_position.y;
    item->s.origin.z = CM_GetHeightAtPoint(drop_position.x, drop_position.y);
    item->s.origin2 = drop_position;
    item->s.renderfx &= ~RF_HIDDEN;
    item->svflags &= ~SVF_NOCLIENT;
    gi.LinkEntity(item);
    G_RefreshInventoryUI(unit);
    if (play_sound) G_QueueOwnerSoundAlias(unit, "ItemDrop");
    return true;
}

bool G_DropItemAt(edict_t * unit, uint32_t slot, vector2_t const * position) {
    return G_DropItemAtInternal(unit, slot, position, true);
}

bool G_DropItem(edict_t * unit, uint32_t slot) {
    if (!unit) {
        return false;
    }
    return G_DropItemAt(unit, slot, &unit->s.origin2);
}

void G_DropInventoryOnDeath(edict_t * unit) {
    uint32_t capacity;

    if (!unit || !G_InventoryDropsItemsOnDeath(unit)) return;
    capacity = G_InventoryCapacity(unit);
    FOR_LOOP(slot, capacity) {
        if (unit->inventory[slot])
            G_DropItemAtInternal(unit, slot, &unit->s.origin2, false);
    }
}

static void G_StopDropItemOrder(edict_t * unit) {
    if (!unit) return;
    unit->goalentity = NULL;
    unit->item_drop = NULL;
    if (unit->stand) unit->stand(unit);
    else unit_stand(unit);
}

static void G_DropItemThink(edict_t * unit) {
    edict_t * item = unit ? unit->item_drop : NULL;
    edict_t * destination = unit ? unit->goalentity : NULL;
    float distance;
    float move_distance;
    int32_t slot;

    if (!unit || !destination || !G_IsItem(item) || item->item.carrier != unit || item->item.in_world) {
        G_StopDropItemOrder(unit);
        return;
    }
    slot = item->item.inventory_slot;
    if (slot < 0 || slot >= MAX_INVENTORY || unit->inventory[slot] != item) {
        G_StopDropItemOrder(unit);
        return;
    }

    distance = M_DistanceToGoal(unit);
    if (distance <= ITEM_DROP_RANGE) {
        vector2_t const position = destination->s.origin2;
        G_DropItemAt(unit, (uint32_t)slot, &position);
        G_StopDropItemOrder(unit);
        return;
    }

    move_distance = unit_movedistance(unit);
    if (move_is_blocked(unit, distance, move_distance)) {
        G_StopDropItemOrder(unit);
        return;
    }
    unit_changeangle(unit);
    unit_moveindirection(unit);
}

static umove_t item_move_drop = {
    .animation = "walk", .think = G_DropItemThink, .endfunc = NULL, .proc = CAbilityInventory
};

bool G_OrderDropItemAt(edict_t * unit, edict_t * item, vector2_t const * position) {
    if (!unit || !item || !position || !G_InventoryCanDropItems(unit) ||
        (unit->aiflags & AI_IMMOBILE) ||
        !G_IsItem(item) || item->item.carrier != unit || item->item.in_world ||
        item->item.inventory_slot < 0 || item->item.inventory_slot >= MAX_INVENTORY ||
        unit->inventory[item->item.inventory_slot] != item) {
        return false;
    }

    unit->goalentity = Waypoint_add(position);
    if (!unit->goalentity) return false;
    move_reset_progress(unit);
    unit_setmove(unit, &item_move_drop);
    unit->item_drop = item;
    unit_setanimation(unit, "stand");
    return true;
}

void G_RemoveItem(edict_t * item) {
    edict_t * carrier;
    int32_t slot;

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
                    slot = (int32_t)i;
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
void G_UseItem(edict_t * unit, uint32_t slot) {
    edict_t * item;
    edict_t * clent;
    cstring_t abilities;

    if (!unit || !G_InventoryCanUseItems(unit) ||
        slot >= G_InventoryCapacity(unit) || unit->s.player >= MAX_PLAYERS) {
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
        abilityitem_t ability_item = MAKE(abilityitem_t, .code = FS_SLKKey(ability_name), .ability = ability);
        abilityCall_t call = MAKE(abilityCall_t, .item = &ability_item, .client = clent);
        bool succeeded = false;

        if (!ability) continue;
        clent->client->menu.ability_code = *((uint32_t const *)ability_name);
        if (ability->flags & AB_ITEM) {
            succeeded = S_AbilityMessage(clent, A_ITEM_USE, &call);
        } else if (S_AbilityHasCommand(ability)) {
            S_AbilityCommand(clent, ability);
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
