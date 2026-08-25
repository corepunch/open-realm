#include "g_local.h"

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

void SP_SpawnItem(LPEDICT self) {
    PATHSTR model_filename;
    strlcpy(model_filename, ITEM_FILE(self->class_id), sizeof(model_filename));
    self->s.model = G_RegisterModel(model_filename);
#ifndef USE_SHADOWMAPS
    self->s.shadow = G_LoadShadowTexture(FS_FindSheetCell(game.config.misc, "Misc", "ItemShadowFile"), false);
    self->s.shadow_rect = ShadowPackRect(
        G_MiscVectorValue("ItemShadowOffset", 0),
        G_MiscVectorValue("ItemShadowOffset", 1),
        G_MiscVectorValue("ItemShadowSize", 0),
        G_MiscVectorValue("ItemShadowSize", 1));
#endif
    self->movetype = MOVETYPE_NONE;
}

/* Forward declarations for passive item stat apply/remove (s_item_stats.c). */
void item_stat_apply(LPEDICT unit, DWORD item_code);
void item_stat_remove(LPEDICT unit, DWORD item_code);

/* Add an item to a unit's inventory. Returns true if successful. */
BOOL G_PickupItem(LPEDICT unit, LPEDICT item) {
    if (!unit || !item) {
        return false;
    }
    for (DWORD i = 0; i < MAX_INVENTORY; i++) {
        if (!unit->inventory[i]) {
            unit->inventory[i] = item;
            item->s.renderfx |= RF_HIDDEN;
            item->invulnerable = true;
            /* Apply passive stat bonuses from this item's abilities. */
            item_stat_apply(unit, item->class_id);
            return true;
        }
    }
    return false;
}

/* Remove an item from a unit's inventory by slot index. */
void G_DropItem(LPEDICT unit, DWORD slot) {
    LPEDICT item;

    if (!unit || slot >= MAX_INVENTORY) {
        return;
    }
    item = unit->inventory[slot];
    if (!item) {
        return;
    }
    /* Reverse passive stat bonuses. */
    item_stat_remove(unit, item->class_id);
    unit->inventory[slot] = NULL;
    item->s.renderfx &= ~RF_HIDDEN;
    item->invulnerable = false;
    item->s.origin2 = unit->s.origin2;
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
