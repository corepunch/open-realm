#include "g_wow_local.h"

#define WOW_BANKER_FLAG 0x20000u
#define WOW_BANK_RANGE_SQUARED 100.0f
#define WOW_BANK_SLOTS 16u

static bool Wow_CanBank(edict_t *player, edict_t *banker) {
    if (!player || !player->inuse || !player->client || !banker || !banker->inuse || player == banker)
        return false;
    wowEntityLocal_t *p = Wow_EntityLocal(player), *b = Wow_EntityLocal(banker);
    if (!p || p->dead || !p->health || !b || b->dead || b->hostile || !b->creature_entry)
        return false;
    wowCreature_t const *creature = Wow_CreatureByEntry(b->creature_entry);
    if (!creature || !(creature->npc_flags & WOW_BANKER_FLAG)) return false;
    vec2_t d = Vector2_sub(&player->s.origin2, &banker->s.origin2);
    return d.x * d.x + d.y * d.y <= WOW_BANK_RANGE_SQUARED;
}

bool Wow_BankOpen(edict_t *player, edict_t *banker) {
    if (!Wow_CanBank(player, banker)) return false;
    wowClient_t *client = (wowClient_t *)player->client;
    client->bank_target = banker->s.number;
    client->bank_entry = Wow_EntityLocal(banker)->creature_entry;
    client->bank_spawn_serial = Wow_EntityLocal(banker)->spawn_serial;
    client->merchant_target = client->merchant_entry = 0;
    return true;
}

bool Wow_BankValid(edict_t *player) {
    if (!player || !player->client) return false;
    wowClient_t *client = (wowClient_t *)player->client;
    if (!client->bank_target || client->bank_target >= (uint32_t)globals.num_edicts ||
        client->bank_target >= WOW_MAX_EDICTS) return false;
    edict_t *banker = &wow_edicts[client->bank_target];
    return Wow_CanBank(player, banker) && Wow_EntityLocal(banker)->creature_entry == client->bank_entry &&
        Wow_EntityLocal(banker)->spawn_serial == client->bank_spawn_serial;
}

bool Wow_BankTransfer(edict_t *player, bool deposit, uint32_t slot, bool half) {
    if (!Wow_BankValid(player)) return false;
    wowClient_t *client = (wowClient_t *)player->client;
    wowHudIcon_t *source = deposit ? client->inventory : client->bank;
    wowHudIcon_t *destination = deposit ? client->bank : client->inventory;
    uint32_t source_count = deposit ? WOW_UI_INVENTORY_SLOTS : WOW_BANK_SLOTS;
    uint32_t destination_count = deposit ? WOW_BANK_SLOTS : WOW_UI_INVENTORY_SLOTS;
    if (slot >= source_count || !source[slot].icon[0] || !source[slot].count ||
        (half && source[slot].count < 2)) return false;
    wowHudIcon_t item = source[slot];
    if (half) item.count /= 2;
    if (!Wow_StorageAdd(destination, destination_count, &item)) return false;
    return Wow_StorageRemove(source, source_count, slot, item.count);
}
