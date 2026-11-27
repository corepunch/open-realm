#include "g_wow_local.h"
#include <string.h>

/* Prototype provisions; prices are copper per item. This is not imported
 * npc_vendor stock, and deliberately does not infer unknown item prices. */
static wowMerchantItem_t const stock[] = {
    { { "Interface\\Icons\\INV_Potion_51.blp", "Minor Healing Potion", 1, 118 }, 20, 5 },
    { { "Interface\\Icons\\INV_Misc_Food_24.blp", "Tough Jerky", 1, 117 }, 4, 1 },
};

uint32_t Wow_MerchantItemCount(void) { return sizeof(stock) / sizeof(stock[0]); }
wowMerchantItem_t const *Wow_MerchantItem(uint32_t i) {
    return i < Wow_MerchantItemCount() ? &stock[i] : NULL;
}

static bool CanTrade(edict_t *player, edict_t *npc) {
    wowEntityLocal_t *p, *n;
    wowCreature_t const *creature;
    vec2_t delta;
    if (!player || !player->inuse || !player->client || !npc || !npc->inuse || player == npc)
        return false;
    p = Wow_EntityLocal(player); n = Wow_EntityLocal(npc);
    if (!p || p->dead || !n || n->dead || n->hostile || !n->creature_entry) return false;
    creature = Wow_CreatureByEntry(n->creature_entry);
    if (!creature || !(creature->npc_flags & 128)) return false;
    delta = Vector2_sub(&player->s.origin2, &npc->s.origin2);
    return delta.x * delta.x + delta.y * delta.y <= 100.0f;
}

bool Wow_MerchantOpen(edict_t *player, edict_t *npc) {
    wowClient_t *client;
    if (!CanTrade(player, npc)) return false;
    client = (wowClient_t *)player->client;
    client->bank_target = client->bank_entry = 0;
    client->merchant_target = npc->s.number;
    client->merchant_entry = Wow_EntityLocal(npc)->creature_entry;
    client->merchant_spawn_serial = Wow_EntityLocal(npc)->spawn_serial;
    if (++client->merchant_revision == 0) ++client->merchant_revision;
    return true;
}

bool Wow_MerchantValid(edict_t *player) {
    wowClient_t *client;
    edict_t *npc;
    if (!player || !player->client) return false;
    client = (wowClient_t *)player->client;
    if (!client->merchant_target || client->merchant_target >= (uint32_t)globals.num_edicts ||
        client->merchant_target >= WOW_MAX_EDICTS) return false;
    npc = &wow_edicts[client->merchant_target];
    return CanTrade(player, npc) && Wow_EntityLocal(npc)->creature_entry == client->merchant_entry &&
        Wow_EntityLocal(npc)->spawn_serial == client->merchant_spawn_serial;
}

bool Wow_MerchantBuy(edict_t *player, uint32_t index) {
    wowClient_t *client;
    wowEntityLocal_t *p;
    wowMerchantItem_t const *item = Wow_MerchantItem(index);
    if (!item || !Wow_MerchantValid(player)) return false;
    client = (wowClient_t *)player->client; p = Wow_EntityLocal(player);
    if (p->copper < item->buy_price || !Wow_InventoryAdd(client, &item->item)) return false;
    p->copper -= item->buy_price;
    if (++client->merchant_revision == 0) ++client->merchant_revision;
    return true;
}

bool Wow_MerchantSell(edict_t *player, uint32_t slot) {
    wowClient_t *client;
    wowEntityLocal_t *p;
    wowHudIcon_t *v;
    if (slot >= WOW_UI_INVENTORY_SLOTS || !Wow_MerchantValid(player)) return false;
    client = (wowClient_t *)player->client; p = Wow_EntityLocal(player); v = &client->inventory[slot];
    if (!v->count) return false;
    for (uint32_t i = 0; i < Wow_MerchantItemCount(); i++) {
        wowMerchantItem_t const *item = &stock[i];
        if (v->item_id != item->item.item_id) continue;
        uint64_t amount = (uint64_t)item->sell_price * v->count;
        if (amount > UINT32_MAX - p->copper) return false;
        memmove(&client->merchant_buyback[1], &client->merchant_buyback[0], 11 * sizeof(client->merchant_buyback[0]));
        memmove(&client->merchant_buyback_price[1], &client->merchant_buyback_price[0], 11 * sizeof(client->merchant_buyback_price[0]));
        client->merchant_buyback[0] = *v;
        client->merchant_buyback_price[0] = (uint32_t)amount;
        p->copper += (uint32_t)amount;
        if (++client->merchant_revision == 0) ++client->merchant_revision;
        Wow_InventoryRemove(client, slot, v->count);
        return true;
    }
    return false;
}

bool Wow_MerchantBuyback(edict_t *player, uint32_t slot) {
    wowClient_t *client;
    wowEntityLocal_t *p;
    if (slot >= 12 || !Wow_MerchantValid(player)) return false;
    client = (wowClient_t *)player->client; p = Wow_EntityLocal(player);
    if (!client->merchant_buyback[slot].count || p->copper < client->merchant_buyback_price[slot]) return false;
    if (!Wow_InventoryAdd(client, &client->merchant_buyback[slot])) return false;
    p->copper -= client->merchant_buyback_price[slot];
    if (++client->merchant_revision == 0) ++client->merchant_revision;
    memmove(&client->merchant_buyback[slot], &client->merchant_buyback[slot + 1], (11 - slot) * sizeof(client->merchant_buyback[0]));
    memmove(&client->merchant_buyback_price[slot], &client->merchant_buyback_price[slot + 1], (11 - slot) * sizeof(client->merchant_buyback_price[0]));
    memset(&client->merchant_buyback[11], 0, sizeof(client->merchant_buyback[11]));
    client->merchant_buyback_price[11] = 0;
    return true;
}
