#include "g_local.h"
#include "../common/wc3_pathing_widget.h"
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

static void G_RefreshInventoryUI(edict_t *unit) {
    if (!unit) {
        return;
    }

    /* Player/client edicts occupy the reserved [0, max_clients) range and are
     * intentionally not normal in-use gameplay entities.  Refresh inventory by
     * connection state instead of edict->inuse, otherwise a successful pickup
     * never re-sends LAYER_INVENTORY to the selecting client. */
    FOR_LOOP(i, game.max_clients) {
        edict_t *player = globals.edicts + i;
        if (player->client && player->client->connected &&
            G_IsEntitySelected(player->client, unit)) {
            G_RefreshInventoryLayer(player);
        }
    }
}

void G_ShowInventoryFull(edict_t *unit) {
    edict_t *player;

    if (!unit || unit->s.player >= MAX_PLAYERS || !level.mapinfo) {
        return;
    }
    player = G_GetPlayerEntityByNumber(unit->s.player);
    if (player && player->client) {
        G_ShowCommandErrorKey(player, "Inventoryfull", NULL);
    }
}

cstring_t G_ItemAbilityList(edict_t const *item) {
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

static bool G_ItemSendAbilityMessage(edict_t *unit, edict_t const *item, abilityMsg_t msg) {
    abilityCall_t call = MAKE(abilityCall_t, .source_item = (edict_t *)item,
                              .source_item_spawn_time = item ? item->spawn_time : 0);
    return G_IsItem((edict_t *)item) && S_UnitAbilityMessage(unit, msg, &call);
}

bool G_ItemAbilitiesPreventDrop(edict_t *unit, edict_t const *item) {
    return G_ItemSendAbilityMessage(unit, item, A_ITEM_PREVENT_DROP);
}

bool G_ItemAbilityScriptedRemove(edict_t *unit, edict_t const *item) {
    return G_ItemSendAbilityMessage(unit, item, A_ITEM_SCRIPT_REMOVE);
}

bool G_ItemAbilityScriptedReattach(edict_t *unit, edict_t const *item) {
    return G_ItemSendAbilityMessage(unit, item, A_ITEM_SCRIPT_REATTACH);
}

/* ItemData stores passive effects as an ability list; the item rawcode itself
 * is not an ability code. */
static void G_ApplyItemStats(edict_t *unit, edict_t const *item, bool apply) {
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
        abilityCall_t call = MAKE(abilityCall_t, .item = &entry,
            .source_item=(edict_t *)item,.source_item_spawn_time=item->spawn_time);
        S_AbilityMessage(unit, apply ? A_ITEM_ADD : A_ITEM_REMOVE, &call);
    }
}

/* CItem vt160/164/168: independent mover category18, item query10,
 * world radius1. Authored selectionSize is presentation geometry only. */
static struct { float radius; uint8_t category, query; } const item_pathing = {1.f, 0x18, 0x10};

uint8_t G_ItemPathingCategory(void) { return item_pathing.category; }

static vec2_t G_ItemPlacementPosition(edict_t *item, vec2_t point, bool match_level) {
    if (match_level) {
        box2_t bounds = CM_GetWorldBounds();
        point.x = wc3_widget_clamp_axis(point.x, bounds.min.x, bounds.max.x);
        point.y = wc3_widget_clamp_axis(point.y, bounds.min.y, bounds.max.y);
    }
    vec2_t admitted = point;
    G_FindWidgetPlacementPosition(item, &point, item_pathing.radius, item_pathing.query,
                                  match_level, &admitted);
    return admitted;
}

static void G_CommitItemPosition(edict_t *item, vec2_t point) {
    item->s.origin2 = point;
    item->s.origin.x = point.x;
    item->s.origin.y = point.y;
    item->s.origin.z = CM_GetHeightAtPoint(point.x, point.y);
}

void SP_SpawnItem(edict_t *self) {
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
    self->collision = item_pathing.radius;
    self->targtype = TARG_ITEM;
    if (!self->item) self->item = G_AllocItem();
    assert(self->item);
    self->item->carrier = NULL;
    self->item->inventory_slot = -1;
    self->item->in_world = true;
    self->item->drop_id = 0;
    /* Constructor658c10 supplies no support-level callback; public position
     * writes6515f0 do. Do not substitute unit unstuck/recovery here. */
    G_CommitItemPosition(self, G_ItemPlacementPosition(self, self->s.origin2, false));
    self->item->charges = (uint32_t)MAX(0, (int32_t)(G_ItemData(self->class_id) ? G_ItemData(self->class_id)->uses : 0));
}

bool G_IsItem(edict_t const *item) {
    if (!item || !item->inuse || !item->class_id) {
        return false;
    }
    /* The item state shares storage with other entity kinds; classify by the
     * target type before reading it so destructables cannot be mistaken for
     * items and dereference the wrong data union member. */
    return item->targtype == TARG_ITEM && item->item &&
        (item->item->in_world || item->item->carrier || item->item->pending_use_removal ||
         (item->data.ItemData && item->data.ItemData->file));
}

/* The item currently being visited by EnumItemsInRect, read back by the
 * GetEnumItem native inside the enum action (mirrors currentdestructable). */
edict_t *currentenumitem = NULL;

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

/* Changing a player's Backpack research changes the *effective* inventory
 * capacity and permissions of owned carriers. Command-card invalidation alone
 * does not rebuild LAYER_INVENTORY for an already selected carrier. Refresh
 * its focused presentation for every viewer, including shared-control allies.
 * This remains generic: object data chooses the inventory ability and the
 * player-tech table chooses whether that ability is unlocked. */
void G_RefreshInventoryAfterTechChange(gameClient_t const *owner, uint32_t techid) {
    if (!owner || !techid || !globals.edicts) return;
    if (techid != MAKEFOURCC('R','h','p','m') &&
        techid != MAKEFOURCC('R','o','p','m') &&
        techid != MAKEFOURCC('R','e','p','m') &&
        techid != MAKEFOURCC('R','u','p','m')) return;

    FOR_LOOP(i, game.max_clients) {
        edict_t *player = globals.edicts + i;
        edict_t *focused;
        cstring_t abilities;
        bool affected = false;
        if (!player->client || !player->client->connected) continue;
        focused = G_GetMainSelectedUnit(player->client);
        if (!focused || focused->s.player != owner->ps.number ||
            !focused->data.UnitAbilities) continue;
        abilities = focused->data.UnitAbilities->abilList;
        if (!abilities) continue;
        PARSE_LIST(abilities, abil, parse_segment) {
            if (strlen(abil) == 4 && G_InventoryRequiredUpgrade(FS_SLKKey(abil)) == techid) {
                affected = true;
                break;
            }
        }
        if (affected) G_RefreshInventoryLayer(player);
    }
}

static bool G_InventoryAbilityAvailable(edict_t const *unit, cstring_t ability) {
    uint32_t ability_id;
    uint32_t required_upgrade;
    gameClient_t *owner;

    if (!unit || !ability || strlen(ability) != 4) return false;
    memcpy(&ability_id, ability, sizeof(ability_id));
    required_upgrade = G_InventoryRequiredUpgrade(ability_id);
    if (!required_upgrade) return true;

    owner = G_GetPlayerClientByNumber(unit->s.player);
    if (!owner || owner->ps.number != unit->s.player) return false;
    return G_GetPlayerTechResearchedLevel(owner, required_upgrade) > 0;
}

/* Capacity and permissions share one lookup, including UnitAddAbility
 * attachments. Authored inventory rows retain their existing priority. */
static uint32_t inventory_ability_code(edict_t const *unit,bool *seen) {
    *seen=false;
    if(!unit || !unit->inuse)return 0;
    cstring_t list=unit->data.UnitAbilities ? unit->data.UnitAbilities->abilList : NULL;
    if(list) {
        PARSE_LIST(list,abil,parse_segment) {
            if(G_AbilityCodeName(abil)!=MAKEFOURCC('A','I','n','v'))continue;
            *seen=true;
            /* An authored row remains known after UnitRemoveAbility, but its
             * removed attachment no longer grants slots or permission flags. */
            if(G_ActorHasAbilityCode(unit,FS_SLKKey(abil)) && G_InventoryAbilityAvailable(unit,abil))return FS_SLKKey(abil);
        }
    }
    FOR_LOOP(i,ARRAY_COUNT(unit->abilities.added)) {
        uint32_t code=unit->abilities.added[i];
        if(G_AbilityCode(code)!=MAKEFOURCC('A','I','n','v'))continue;
        *seen=true;
        char name[5]={0};memcpy(name,&code,4);
        if(G_InventoryAbilityAvailable(unit,name))return code;
    }
    return 0;
}

/* Classic ROC has a built-in six-slot hero inventory and no AInv data row.
 * AIab/AIa6 are attribute bonuses, never inventory aliases. An authored AInv
 * row (including zero capacity) remains authoritative when present. */
static bool G_UsesClassicHeroInventory(edict_t const *unit, cstring_t ability) {
    return !strcmp(ability, "AInv") && G_IsReignOfChaosMap(level.mapinfo) &&
        G_UnitIsHero(unit) && !G_AbilityDataName(ability)->id;
}

static bool G_InventoryAbilityFlag(edict_t const *unit,uint32_t data_index,bool roc_hero_default) {
    if(!unit || !unit->inuse)return false;
    bool has_inventory;
    uint32_t code=inventory_ability_code(unit,&has_inventory);
    if(code) {
        char name[5]={0};memcpy(name,&code,4);
        if(G_UsesClassicHeroInventory(unit,name))return roc_hero_default;
        abilityLevel_t const *row=G_AbilityLevel(code,1);
        return row && data_index<sizeof(row->data)/sizeof(row->data[0]) && row->data[data_index].number!=0;
    }
    /* Preserve the stock RoC Hero fallback when no inventory is attached. */
    return !has_inventory && G_IsReignOfChaosMap(level.mapinfo) && G_UnitIsHero(unit) && roc_hero_default;
}

bool G_InventoryCanUseItems(edict_t const *unit) {
    /* inv3 / DataC: heroes may use/equip item abilities; Backpack carriers
     * such as Aihn have this disabled and therefore only transport items. */
    return G_InventoryAbilityFlag(unit, 2, true);
}

bool G_InventoryCanGetItems(edict_t const *unit) {
    /* inv4 / DataD gates player-issued pickup orders. Scripted/native item
     * insertion uses G_AddItemScripted, whose forced admission bypasses
     * canGetItems as in original66eba0. */
    return G_InventoryAbilityFlag(unit, 3, true);
}

bool G_InventoryCanDropItems(edict_t const *unit) {
    /* inv5 / DataE gates player-issued drop/handoff orders. ItemData.droppable
     * (plus SetItemDroppable overrides) is a separate per-item gate. */
    return G_InventoryAbilityFlag(unit, 4, true);
}

bool G_ItemDroppable(edict_t const *item) {
    ItemData_t const *data;

    if (!G_IsItem(item)) return false;
    if (item->item->droppable_set) return item->item->droppable;
    data = item->data.ItemData ? item->data.ItemData : G_ItemData(item->class_id);
    if (!data || !data->id) {
        fprintf(stderr, "WC3 ItemData: row unresolved for item %.4s; drop rejected\n",
                (cstring_t)&item->class_id);
        return false;
    }
    return data->droppable;
}

static bool G_InventoryDropsItemsOnDeath(edict_t const *unit) {
    /* inv2 / DataB: ordinary Backpack carriers drop their contents, while the
     * stock hero inventory retains items across death/revival. */
    return G_InventoryAbilityFlag(unit, 1, false);
}

static uint32_t G_InventoryAbilityCapacity(edict_t const *unit, cstring_t ability) {
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

uint32_t G_InventoryCapacity(edict_t const *unit) {
    if(!unit || !unit->inuse)return 0;
    bool has_inventory;
    uint32_t code=inventory_ability_code(unit,&has_inventory);
    if(code) {
        char name[5]={0};memcpy(name,&code,4);
        return G_InventoryAbilityCapacity(unit,name);
    }
    /* Classic Hero inventory is synthesized only when no inventory row exists. */
    if(!has_inventory && G_IsReignOfChaosMap(level.mapinfo) && G_UnitIsHero(unit))
        return G_InventoryAbilityCapacity(unit,"AInv");
    return 0;
}

bool G_UnitHasInventory(edict_t *unit) {
    return G_InventoryCapacity(unit) > 0;
}

uint32_t G_ItemCharges(edict_t const *item) {
    return G_IsItem(item) ? item->item->charges : 0;
}

void G_SetItemCharges(edict_t *item, uint32_t charges) {
    if (!G_IsItem(item) || item->item->charges == charges) return;
    item->item->charges = charges;
    if (item->item->carrier) G_RefreshInventoryUI(item->item->carrier);
}

void G_ConsumeItemCharge(edict_t *item) {
    if (!G_IsItem(item) || !item->data.ItemData || item->item->charges == 0) return;

    /* All charged item uses decrement charges. Perishable only controls the
     * zero-charge lifetime: Warsmash removes perishables, while reusable
     * zero-charge items remain held. Avoid publishing a transient zero-charge
     * copy immediately before final perishable removal. */
    if (item->item->charges == 1 && item->data.ItemData->perishable) {
        item->item->charges = 0;
        G_RemoveItem(item);
        return;
    }
    G_SetItemCharges(item, item->item->charges - 1);
}

static void G_RetainConsumedItemForUseEvent(edict_t *item) {
    edict_t *carrier;
    int32_t slot;

    if (!G_IsItem(item) || item->item->pending_use_removal) return;
    carrier = item->item->carrier;
    slot = item->item->inventory_slot;
    item->item->pending_use_carrier = carrier;
    item->item->pending_use_carrier_spawn_time = carrier ? carrier->spawn_time : 0;
    item->item->pending_use_slot = slot;
    if (carrier && carrier->inuse) {
        if (slot < 0 || slot >= MAX_INVENTORY || carrier->inventory[slot] != item) {
            slot = -1;
            FOR_LOOP(i, MAX_INVENTORY) {
                if (carrier->inventory[i] == item) { slot = (int32_t)i; break; }
            }
        }
        if (slot >= 0) {
            G_ApplyItemStats(carrier, item, false);
            carrier->inventory[slot] = NULL;
        }
        G_RefreshInventoryUI(carrier);
    }

    gi.UnlinkEntity(item);
    item->item->carrier = NULL;
    item->item->inventory_slot = -1;
    item->item->in_world = false;
    item->item->pending_use_removal = true;
    G_SetEntityHidden(item,true);
    item->svflags |= SVF_NOCLIENT;
    level.pending_consumed_item_cleanup = true;
}

void G_CompleteItemUse(edict_t *unit, edict_t *item) {
    if (!unit || !unit->inuse || !G_IsItem(item) || item->item->pending_use_removal) return;

    G_PublishEventWithSource(unit, EVENT_PLAYER_UNIT_USE_ITEM, item);
    G_PublishEventWithSource(unit, EVENT_UNIT_USE_ITEM, item);

    /* A one-charge perishable has to leave gameplay immediately, but its JASS
     * handle remains observable as GetManipulatedItem() until the queued event
     * and any sleeping response action have released that event context. */
    if (item->data.ItemData && item->item->charges == 1 && item->data.ItemData->perishable) {
        item->item->charges = 0;
        G_RetainConsumedItemForUseEvent(item);
        return;
    }
    G_ConsumeItemCharge(item);
}

static bool G_ConsumedItemHasQueuedEvent(edict_t const *item) {
    if (!item) return false;
    for (uint32_t i = level.events.read; i < level.events.write; i++) {
        gameEvent_t const *evt = &level.events.queue[i % MAX_EVENT_QUEUE];
        if (evt->source == item &&
            (!evt->source_spawn_tracked || evt->source_spawn_time == item->spawn_time))
            return true;
    }
    return false;
}

void G_RunConsumedItemFrees(void) {
    if (!level.pending_consumed_item_cleanup) return;
    level.pending_consumed_item_cleanup = false;

    FOR_LOOP(i, globals.num_edicts) {
        edict_t *item = globals.edicts + i;
        if (!item->inuse || item->targtype != TARG_ITEM || !item->item->pending_use_removal) continue;
        if (G_ConsumedItemHasQueuedEvent(item) ||
            (level.vm && jass_context_references_entity(level.vm, item))) {
            level.pending_consumed_item_cleanup = true;
            continue;
        }
        if (item->item->pending_use_carrier && item->item->pending_use_carrier->inuse &&
            item->item->pending_use_carrier->spawn_time == item->item->pending_use_carrier_spawn_time &&
            !G_IsDeferredFree(item->item->pending_use_carrier)) {
            abilityCall_t call = MAKE(abilityCall_t, .source_item = item,
                                      .source_item_spawn_time = item->spawn_time);
            S_UnitAbilityMessage(item->item->pending_use_carrier, A_ITEM_FINALIZE, &call);
        }
        G_FreeEdict(item);
    }
}

int32_t G_FindFreeInventorySlot(edict_t const *unit) {
    uint32_t capacity = G_InventoryCapacity(unit);

    FOR_LOOP(i, capacity) if (!unit->inventory[i]) return (int32_t)i;
    return -1;
}

/* A zero-slot inventory may still use a rune or ability-less quest powerup.
 * Ordinary items still require an available inventory slot. */
static bool G_IsAbilitylessPowerup(edict_t const *item);
bool G_CanConsumePickupPowerup(edict_t *unit, edict_t *item) {
    return G_InventoryCanUseItems(unit) &&
           (S_ItemPowerup(unit, item).ability || G_IsAbilitylessPowerup(item));
}

bool G_CanPickupItem(edict_t *unit, edict_t *item) {
    if (!G_UnitIsWorldActive(unit) || G_IsDeferredFree(unit) || M_IsDead(unit) ||
        !G_IsItem(item) || G_IsDeferredFree(item) ||
        (!G_UnitHasInventory(unit) && !G_CanConsumePickupPowerup(unit, item))) {
        return false;
    }
    return item->item->in_world && !item->item->carrier && item->item->inventory_slot == -1 &&
           !(item->s.renderfx & RF_HIDDEN) && !(item->svflags & SVF_NOCLIENT);
}

/* Binding is shared by ordinary pickups and forced scripted insertion. The
 * caller owns admission and the synchronous versus queued event boundary. */
static void G_BindItemToSlot(edict_t *unit, edict_t *item, uint32_t slot) {
    gi.UnlinkEntity(item);
    G_SetEntityHidden(item,true);
    item->svflags |= SVF_NOCLIENT;
    item->item->in_world = false;
    item->item->carrier = unit;
    item->item->inventory_slot = (int32_t)slot;
    unit->inventory[slot] = item;
    G_PublishMoveSpatialObject(item);
    G_ApplyItemStats(unit, item, true);
    G_RefreshInventoryUI(unit);
}

bool G_AddItemToSlotInternal(edict_t *unit, edict_t *item, uint32_t slot, bool publish_event) {
    if (slot >= G_InventoryCapacity(unit) || !G_CanPickupItem(unit, item) || unit->inventory[slot]) return false;
    G_BindItemToSlot(unit,item,slot);
    if (publish_event) {
        G_PlayerScoreItemGained(unit);
        G_PublishEventWithSource(unit, EVENT_PLAYER_UNIT_PICKUP_ITEM, item);
        G_PublishEventWithSource(unit, EVENT_UNIT_PICKUP_ITEM, item);
    }
    return true;
}

bool G_AddItemToSlot(edict_t *unit, edict_t *item, uint32_t slot) {
    return G_AddItemToSlotInternal(unit, item, slot, true);
}

/*2182a0/66eba0 force insertion accepts hidden and detached unowned items.
 * Visibility belongs to the world pickup producer, not this native. */
static bool G_CanAddItemScripted(edict_t *unit, edict_t *item) {
    return unit && unit->inuse && unit->targtype!=TARG_ITEM && !unit->destructable && !unit->client &&
        !M_IsDead(unit) && G_IsItem(item) && !G_IsDeferredFree(item) && !item->item->pending_use_removal &&
        (G_UnitHasInventory(unit) || G_CanConsumePickupPowerup(unit,item));
}

/* Script insertion completes Pickup on the current JASS stack. */
bool G_AddItemToSlotScripted(edict_t *unit, edict_t *item, uint32_t slot) {
    if(!G_CanAddItemScripted(unit,item) || item->item->carrier || slot>=G_InventoryCapacity(unit) || unit->inventory[slot])return false;
    G_BindItemToSlot(unit,item,slot);
    G_PlayerScoreItemGained(unit);
    G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=unit,.source=item,.type=EVENT_PLAYER_UNIT_PICKUP_ITEM},EVENT_UNIT_PICKUP_ITEM,false);
    return true;
}

/* Detachment is separate from world publication and the next recipient's
 * admission. A failed Give attachment retains a hidden unowned item, while
 * RemoveUnit's retirement producer observes the already-cleared source slot. */
void G_DetachCarriedItem(edict_t *item) {
    edict_t *carrier=item->item->carrier;
    int32_t slot=item->item->inventory_slot;
    if(carrier && carrier->inuse) {
        if(slot<0 || slot>=MAX_INVENTORY || carrier->inventory[slot]!=item) {
            slot=-1;
            FOR_LOOP(i,MAX_INVENTORY)if(carrier->inventory[i]==item){slot=i;break;}
        }
        if(slot>=0) {
            G_ApplyItemStats(carrier,item,false);
            carrier->inventory[slot]=NULL;
        }
        G_RefreshInventoryUI(carrier);
    }
    item->item->carrier=NULL;item->item->inventory_slot=-1;item->item->in_world=false;
}

/* Player Give detaches without world publication, then attempts recipient
 * attachment. Scripted UnitAddItem uses its own world-drop producer below. */
bool G_GiveCarriedItem(edict_t *recipient, edict_t *item) {
    edict_t *source;
    int32_t source_slot, target_slot;

    if (!G_IsItem(item) || item->item->in_world || item->item->pending_use_removal ||
        !recipient || !recipient->inuse || recipient->targtype == TARG_ITEM ||
        recipient->destructable || recipient->client || M_IsDead(recipient) ||
        !G_UnitHasInventory(recipient))
        return false;
    source = item->item->carrier;
    source_slot = item->item->inventory_slot;
    if (!source || !source->inuse || source_slot < 0 || source_slot >= MAX_INVENTORY ||
        source->inventory[source_slot] != item) return false;
    if (source == recipient) return true;
    /* Ability-bound items (notably the filled Soul Gem) are never transferable
     * to a different carrier, even through a scripted UnitAddItem. Plain
     * ItemData.droppable=false remains bypassable by scripted natives. */
    if (G_ItemAbilitiesPreventDrop(source, item)) return false;
    target_slot = G_FindFreeInventorySlot(recipient);
    if (target_slot < 0) return false;

    {
        uint32_t const generation=item->spawn_time,recipient_spawn=recipient->spawn_time;
        /*5667d0 publishes Drop while the original source slot still contains
         * the item. A nested order changes movement, not this transaction. */
        G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=source,.source=item,.type=EVENT_PLAYER_UNIT_DROP_ITEM},EVENT_UNIT_DROP_ITEM,false);
        if(!recipient->inuse || recipient->spawn_time!=recipient_spawn)return false;
        if(!G_IsItem(item) || item->spawn_time!=generation) {
            G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=recipient,.type=EVENT_PLAYER_UNIT_PICKUP_ITEM},EVENT_UNIT_PICKUP_ITEM,false);
            return true;
        }
        source_slot=item->item->inventory_slot;
        if(item->item->carrier!=source || source_slot<0 || source_slot>=MAX_INVENTORY ||
           source->inventory[source_slot]!=item)return false;
    }

    G_DetachCarriedItem(item);
    /*6695c0 rechecks capacity AFTER5667d0 detached the item. A callback that
     * fills/removes Inventory must not roll that completed detach back. */
    target_slot=G_FindFreeInventorySlot(recipient);
    if(target_slot<0)return false;
    recipient->inventory[target_slot] = item;
    item->item->carrier = recipient;
    item->item->inventory_slot = target_slot;
    G_ApplyItemStats(recipient, item, true);
    G_RefreshInventoryUI(recipient);
    G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=recipient,.source=item,.type=EVENT_PLAYER_UNIT_PICKUP_ITEM},EVENT_UNIT_PICKUP_ITEM,false);
    return true;
}

/* Per-instance SetItemPawnable overrides the authored ItemData flag. */
bool G_ItemPawnable(edict_t const *item) {
    if(!G_IsItem(item))return false;
    ItemData_t const *data=item->data.ItemData ? item->data.ItemData : G_ItemData(item->class_id);
    return item->item->pawnable_set ? item->item->pawnable : data && data->pawnable;
}

/* Ability-less powerups such as campaign Shimmerweed are quest pickups,
 * not inventory items. Keep ability-bearing powerups on the existing ability
 * dispatch path; an unsupported ability must not silently lose its effect. */
static bool G_IsAbilitylessPowerup(edict_t const *item) {
    cstring_t abilities;
    if (!G_IsItem(item) || !item->data.ItemData || !item->data.ItemData->powerup)
        return false;
    abilities = G_ItemAbilityList(item);
    return !abilities || !*abilities || !strcmp(abilities, "_");
}

/* Direct pickup callers retain queued publication. Scripted insertion and the
 * Inventory terminal use the synchronous insertion sink recovered from retail. */
static void G_ItemPickupEvent(edict_t *unit, edict_t *item, bool scripted) {
    if(scripted)G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=unit,.source=item,.type=EVENT_PLAYER_UNIT_PICKUP_ITEM},EVENT_UNIT_PICKUP_ITEM,false);
    else {
        G_PublishEventWithSource(unit, EVENT_PLAYER_UNIT_PICKUP_ITEM, item);
        G_PublishEventWithSource(unit, EVENT_UNIT_PICKUP_ITEM, item);
    }
}

static void G_CompletePowerupUse(edict_t *unit, edict_t *item, bool scripted) {
    uint32_t const generation=item->spawn_time;
    G_ItemPickupEvent(unit,item,scripted);
    if(!G_IsItem(item) || item->spawn_time!=generation)return;
    if(scripted)G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=unit,.source=item,.type=EVENT_PLAYER_UNIT_USE_ITEM},EVENT_UNIT_USE_ITEM,false);
    else {
        G_PublishEventWithSource(unit, EVENT_PLAYER_UNIT_USE_ITEM, item);
        G_PublishEventWithSource(unit, EVENT_UNIT_USE_ITEM, item);
    }
    if(!G_IsItem(item) || item->spawn_time!=generation)return;
    item->item->charges = 0;
    G_RetainConsumedItemForUseEvent(item);
}

static bool G_PickupItemInternal(edict_t *unit, edict_t *item, bool scripted) {
    if(!(scripted ? G_CanAddItemScripted(unit,item) : G_CanPickupItem(unit,item)))return false;
    uint32_t const generation=item->spawn_time;
    abilityitem_t powerup = S_ItemPowerup(unit, item);
    int32_t slot;
    bool added;

    if (powerup.ability) {
        abilityCall_t call = MAKE(abilityCall_t, .item = &powerup, .source_item = item, .source_item_spawn_time = item->spawn_time);
        bool const used = S_AbilityMessage(unit, A_ITEM_USE, &call);
        /* Resurrection is a validated use: with no legal corpse, WC3 reports
         * failure and leaves the powerup in the world. Other supported runes
         * keep their consume-on-touch behavior even when no target qualifies. */
        if (!used && (powerup.ability->proc == CAbilityItemResurrection ||
                      powerup.ability->proc == CAbilityItemRebirth)) return false;
        G_QueueOwnerSoundAlias(unit, "ItemGet");
        if(G_IsItem(item) && item->spawn_time==generation)G_CompletePowerupUse(unit, item, scripted);
        return true;
    }
    if (G_CanConsumePickupPowerup(unit, item) &&
        G_IsAbilitylessPowerup(item)) {
        G_QueueOwnerSoundAlias(unit, "ItemGet");
        G_ItemPickupEvent(unit,item,scripted);
        if(!G_IsItem(item) || item->spawn_time!=generation)return true;
        /* A quest powerup is consumed just once even without an ability.
         * Retain its live handle for pickup-trigger callbacks, but do not
         * fabricate a use-item event for an ability-less pickup. */
        item->item->charges = 0;
        G_RetainConsumedItemForUseEvent(item);
        return true;
    }
    slot = G_FindFreeInventorySlot(unit);
    if (slot < 0) {
        return false;
    }
    added = scripted ? G_AddItemToSlotScripted(unit,item,(uint32_t)slot) : G_AddItemToSlot(unit,item,(uint32_t)slot);
    if (added) G_QueueOwnerSoundAlias(unit, "ItemGet");
    return added;
}

bool G_PickupItem(edict_t *unit, edict_t *item) {
    return G_PickupItemInternal(unit,item,false);
}

/*2182a0 holds the original carrier, removes at its current position, then
 * inserts. Source droppable metadata does not gate this scripted transaction. */
bool G_AddItemScripted(edict_t *unit, edict_t *item) {
    if(!G_CanAddItemScripted(unit,item))return false;
    if(!G_CanConsumePickupPowerup(unit,item) && G_FindFreeInventorySlot(unit)<0)return false;
    edict_t *carrier=item->item->carrier;
    if(carrier) {
        int32_t const slot=item->item->inventory_slot;
        uint32_t const generation=item->spawn_time;
        if(slot<0 || slot>=MAX_INVENTORY || !G_DropItemAtScripted(carrier,(uint32_t)slot,&carrier->s.origin2))return false;
        if(!G_IsItem(item) || item->spawn_time!=generation)return false;
    }
    return G_PickupItemInternal(unit,item,true);
}

/* World publication is shared by explicit drops and Inventory detachment;
 * their callback boundaries remain owned by the respective producers. */
static void G_PublishDroppedItem(edict_t *item,vec2_t point) {
    item->item->in_world=true;
    G_CommitItemPosition(item,G_ItemPlacementPosition(item,point,true));
    G_SetEntityHidden(item,false);
    item->svflags &= ~SVF_NOCLIENT;
    gi.LinkEntity(item);
    G_PublishMoveSpatialObject(item);
}

/* V3 scene32: the removed Inventory no longer reports capacity. Release the
 * retained raw slots without cancelling its already-admitted physical task. */
void G_ReleaseRemovedInventory(edict_t *unit) {
    uint32_t const spawn=unit->spawn_time;
    FOR_LOOP(slot,MAX_INVENTORY) {
        edict_t *item=unit->inventory[slot];
        if(!G_IsItem(item) || item->item->carrier!=unit || item->item->inventory_slot!=(int32_t)slot)continue;
        vec2_t const point=unit->s.origin2;
        G_DetachCarriedItem(item);
        G_PublishDroppedItem(item,point);
        G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=unit,.source=item,.type=EVENT_PLAYER_UNIT_DROP_ITEM},EVENT_UNIT_DROP_ITEM,false);
        if(!unit->inuse || unit->spawn_time!=spawn || G_IsDeferredFree(unit))return;
    }
}

static bool G_DropItemAtInternal(edict_t *unit, uint32_t slot, vec2_t const *position, bool play_sound) {
    edict_t *item;

    if (!unit || !position || slot >= (uint32_t)G_InventoryCapacity(unit)) {
        return false;
    }
    item = unit->inventory[slot];
    if (!G_IsItem(item) || item->item->carrier != unit || item->item->inventory_slot != (int32_t)slot ||
        item->item->in_world) {
        return false;
    }

    vec2_t const requested=*position;
    uint32_t const generation=item->spawn_time;
    G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=unit,.source=item,.type=EVENT_PLAYER_UNIT_DROP_ITEM},EVENT_UNIT_DROP_ITEM,false);
    if(!G_IsItem(item) || item->spawn_time!=generation)return true;
    if(item->item->carrier!=unit || item->item->inventory_slot!=(int32_t)slot || unit->inventory[slot]!=item)return false;
    G_DetachCarriedItem(item);
    G_PublishDroppedItem(item,requested);
    if (play_sound) G_QueueOwnerSoundAlias(unit, "ItemDrop");
    return true;
}

bool G_DropItemAt(edict_t *unit, uint32_t slot, vec2_t const *position) {
    edict_t *item;

    if (!unit || slot >= (uint32_t)G_InventoryCapacity(unit)) return false;
    item = unit->inventory[slot];
    if (!G_ItemDroppable(item) || G_ItemAbilitiesPreventDrop(unit, item)) return false;
    return G_DropItemAtInternal(unit, slot, position, true);
}

bool G_DropItemAtScripted(edict_t *unit, uint32_t slot, vec2_t const *position) {
    if (unit && slot < G_InventoryCapacity(unit) &&
        G_ItemAbilitiesPreventDrop(unit, unit->inventory[slot])) return false;
    return G_DropItemAtInternal(unit, slot, position, true);
}

/* Native213940 removes a carried item through694ce0, publishes at the
 * carrier, then performs the requested admitted write through65a340. */
void G_SetItemPosition(edict_t *item, vec2_t const *position) {
    if (!G_IsItem(item) || !position) return;
    edict_t *carrier = item->item->carrier;
    if (carrier) {
        int32_t slot = item->item->inventory_slot;
        if (slot < 0 || !G_DropItemAtInternal(carrier, (uint32_t)slot, &carrier->s.origin2, false)) return;
    }
    if(!G_IsItem(item) || item->item->pending_use_removal || item->item->carrier)return;
    item->item->in_world=true;
    G_SetEntityHidden(item,false);
    item->svflags &= ~SVF_NOCLIENT;
    G_CommitItemPosition(item, G_ItemPlacementPosition(item, *position, true));
    gi.LinkEntity(item);
    G_PublishMoveSpatialObject(item);
}

bool G_DropItem(edict_t *unit, uint32_t slot) {
    if (!unit) {
        return false;
    }
    return G_DropItemAt(unit, slot, &unit->s.origin2);
}

void G_DropInventoryOnDeath(edict_t *unit) {
    uint32_t capacity;
    bool default_drop;

    if (!unit) return;
    default_drop = G_InventoryDropsItemsOnDeath(unit);
    capacity = G_InventoryCapacity(unit);
    FOR_LOOP(slot, capacity) {
        edict_t *item = unit->inventory[slot];
        if (!G_IsItem(item) || G_ItemAbilitiesPreventDrop(unit, item)) continue;
        if (item->item->drop_on_death_set ? item->item->drop_on_death : default_drop)
            G_DropItemAtInternal(unit, slot, &unit->s.origin2, false);
    }
}

bool G_DetachItemAtScripted(edict_t *unit, uint32_t slot) {
    edict_t *item;

    if (!unit || slot >= (uint32_t)G_InventoryCapacity(unit)) return false;
    item = unit->inventory[slot];
    if (!G_IsItem(item) || item->item->carrier != unit || item->item->inventory_slot != (int32_t)slot ||
        item->item->in_world) return false;
    G_ApplyItemStats(unit, item, false);
    unit->inventory[slot] = NULL;
    item->item->inventory_slot = -1;
    G_RefreshInventoryUI(unit);
    return true;
}

bool G_ReattachItemAtScripted(edict_t *unit, edict_t *item, uint32_t slot) {
    if (!unit || slot >= (uint32_t)G_InventoryCapacity(unit) || unit->inventory[slot] ||
        !G_IsItem(item) || item->item->carrier != unit || item->item->inventory_slot != -1 ||
        item->item->in_world || item->item->pending_use_removal) return false;
    item->item->inventory_slot = (int32_t)slot;
    unit->inventory[slot] = item;
    G_ApplyItemStats(unit, item, true);
    G_RefreshInventoryUI(unit);
    G_PublishEventWithSource(unit, EVENT_PLAYER_UNIT_PICKUP_ITEM, item);
    G_PublishEventWithSource(unit, EVENT_UNIT_PICKUP_ITEM, item);
    return true;
}

/* Public RemoveItem calls the carried item's Drop producer before detaching.
 * Internal consumption/Pawn already owns its event boundary and uses the
 * transaction below directly, avoiding duplicate removal notifications. */
void G_RemoveItemScripted(edict_t *item) {
    if(!G_IsItem(item))return;
    uint32_t const generation=item->spawn_time;
    edict_t *carrier=item->item->carrier;
    int32_t slot=item->item->inventory_slot;
    if(carrier && carrier->inuse && slot>=0 && slot<MAX_INVENTORY && carrier->inventory[slot]==item)
        G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=carrier,.source=item,.type=EVENT_PLAYER_UNIT_DROP_ITEM},EVENT_UNIT_DROP_ITEM,false);
    if(G_IsItem(item) && item->spawn_time==generation)G_RemoveItem(item);
}

void G_RemoveItem(edict_t *item) {
    if(!G_IsItem(item))return;
    G_DetachCarriedItem(item);
    G_FreeEdict(item);
}

/* V4 13 publishes retirement Drop after clearing the old unit's slot, while
 * the item identity still resolves. A nested deletion cannot free its reuse. */
static void G_RemoveRetiredItem(edict_t *unit,edict_t *item) {
    uint32_t const generation=item->spawn_time;
    G_DetachCarriedItem(item);
    G_DispatchUnitEventFamilies(&(gameEventPointParams_t){.edict=unit,.source=item,.type=EVENT_PLAYER_UNIT_DROP_ITEM},EVENT_UNIT_DROP_ITEM,false);
    if(G_IsItem(item) && item->spawn_time==generation)G_RemoveItem(item);
}

/* RemoveUnit is not a death drop: inventory objects carried by the retired
 * unit are disposed of, not spawned into the world or orphaned. Run after
 * A_UNIT_REMOVE so ability-owned releases (e.g. Soul Trap) happen first.
 *
 * A script may temporarily detach an item from its slot while keeping the
 * carrier pointer (A_ITEM_SCRIPT_REMOVE). Also, consumed items can retain a
 * pending-use carrier pointer while an event action reads their JASS handle.
 * Scan only inventory-capable owners; ordinary unit and effect removal must
 * not turn into an O(num_edicts) operation on every frame. */
void G_ReleaseInventoryOnUnitRemoval(edict_t *unit) {
    bool inventory_owner;

    if (!unit || !unit->inuse || unit->targtype == TARG_ITEM) return;
    inventory_owner = G_InventoryCapacity(unit) > 0;
    FOR_LOOP(slot, MAX_INVENTORY) {
        edict_t *item = unit->inventory[slot];
        if (!item) continue;
        inventory_owner = true;
        if (G_IsItem(item) && item->item->carrier == unit)
            G_RemoveRetiredItem(unit,item);
        else
            unit->inventory[slot] = NULL;
    }
    if (!inventory_owner) return;
    /* Detached items have carrier==unit but are not present in any slot.
     * Pending-use items keep their own handle until queued events complete;
     * clear only the obsolete carrier link in that case. */
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *item = globals.edicts + i;
        if (!item->inuse || item->targtype != TARG_ITEM || !item->item) continue;
        if (item->item->carrier == unit) G_RemoveRetiredItem(unit,item);
        else if (item->item->pending_use_carrier == unit) {
            item->item->pending_use_carrier = NULL;
            item->item->pending_use_carrier_spawn_time = 0;
        }
    }
}

/* Use an item in inventory by slot index. Calls the item's ability cmd handler. */
void G_UseItem(edict_t *unit, uint32_t slot) {
    edict_t *item;
    edict_t *clent;
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
        clent->client->menu.ability_code = FS_SLKKey(ability_name);
        if (ability->flags & AB_ITEM) {
            succeeded = S_AbilityMessage(clent, A_ITEM_USE, &call);
        } else if (S_AbilityHasCommand(ability)) {
            clent->client->menu.ability_item = item;
            clent->client->menu.ability_item_spawn_time = item->spawn_time;
            S_AbilityCommand(clent, ability);
            if (!clent->client->menu.on_entity_selected && !clent->client->menu.on_location_selected) {
                clent->client->menu.ability_item = NULL;
                clent->client->menu.ability_item_spawn_time = 0;
            }
            return;
        } else {
            continue;
        }

        if (succeeded) G_CompleteItemUse(unit, item);
        return;
    }
}

/* A held-item command refers to an inventory instance, never a recycled edict. */
edict_t *G_GetDraggedItem(gameClient_t *client) {
    edict_t *item = client ? client->menu.dragged_item : NULL;
    edict_t *carrier = G_IsItem(item) ? item->item->carrier : NULL;
    if (!item || !carrier || item->spawn_time != client->menu.dragged_item_spawn_time ||
        !G_UnitCanControl(client, carrier) || !G_InventoryCanDropItems(carrier) || item->item->in_world ||
        item->item->inventory_slot >= G_InventoryCapacity(carrier) ||
        carrier->inventory[item->item->inventory_slot] != item) return NULL;
    return item;
}

void G_UpdateItemDrag(gameClient_t *client) {
    if (client && client->menu.dragged_item && !G_GetDraggedItem(client))
        G_CancelTargetMode(G_GetPlayerEntityByNumber(client->ps.number));
}
