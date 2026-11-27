#include "g_wow_local.h"
#include "g_crafting.h"
#include <string.h>

/* Spell.dbc create-item effect and reagent columns; these recipes do not
 * require a world object. Profession skill and cast time are not yet modeled. */
static wowRecipe_t const recipes[] = {
    {3275, 2589, 1, 1251, 1, "Linen Bandage", "Interface\\Icons\\INV_Misc_Bandage_18.blp"},
    {3276, 2589, 2, 2581, 1, "Heavy Linen Bandage", "Interface\\Icons\\INV_Misc_Bandage_18.blp"},
};
uint32_t Wow_RecipeCount(void) { return sizeof(recipes) / sizeof(recipes[0]); }
wowRecipe_t const *Wow_Recipe(uint32_t index) { return index < Wow_RecipeCount() ? &recipes[index] : NULL; }

bool Wow_Craft(edict_t *player, uint32_t spell) {
    if (!player || !player->client) return false;
    wowEntityLocal_t *local = Wow_EntityLocal(player);
    wowClient_t *client = (wowClient_t *)player->client;
    wowRecipe_t const *recipe = NULL;
    wowHudIcon_t slots[WOW_UI_INVENTORY_SLOTS], output = {0};
    uint32_t need;
    if (!local || local->dead || !local->health || !client->crafting_open) return false;
    for (uint32_t i = 0; i < Wow_RecipeCount(); ++i)
        if (recipes[i].spell == spell) recipe = &recipes[i];
    if (!recipe) return false;
    memcpy(slots, client->inventory, sizeof(slots));
    need = recipe->reagent_count;
    for (uint32_t i = 0; i < WOW_UI_INVENTORY_SLOTS && need; ++i) {
        if (slots[i].item_id != recipe->reagent || !slots[i].count) continue;
        uint32_t count = MIN(need, slots[i].count);
        if (!Wow_StorageRemove(slots, WOW_UI_INVENTORY_SLOTS, i, count)) return false;
        need -= count;
    }
    if (need) return false;
    snprintf(output.icon, sizeof(output.icon), "%s", recipe->icon);
    snprintf(output.name, sizeof(output.name), "%s", recipe->name);
    output.item_id = recipe->result;
    output.count = recipe->result_count;
    if (!Wow_StorageAdd(slots, WOW_UI_INVENTORY_SLOTS, &output)) return false;
    memcpy(client->inventory, slots, sizeof(slots));
    return true;
}
