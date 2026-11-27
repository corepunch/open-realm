#include "g_wow_local.h"
#include "g_character_save.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

/* Version 1 records fixed-width values and fixed-size item strings. Snapshot is
 * built from zero, checksummed, and validated before applying any live state. */
typedef struct {
    uint32_t magic, version, size, checksum;
    wowHudIcon_t inventory[WOW_UI_INVENTORY_SLOTS], bank[16];
    uint32_t copper, experience, fireball_rank2, quest_count;
    uint32_t quest_id[SV_MAX_QUEST_LOG], quest_status[SV_MAX_QUEST_LOG];
    uint32_t quest_tracked[SV_MAX_QUEST_LOG];
    uint32_t kill_progress[SV_MAX_QUEST_LOG][WOW_QUEST_MAX_KILL_OBJECTIVES];
    uint8_t rewarded[8192], taxi[8192], explored[8192];
    uint32_t action_spells[WOW_UI_ACTION_SLOTS];
} wowCharacterSave_t;

static uint32_t Wow_SaveChecksum(wowCharacterSave_t const *save, size_t size) {
    uint8_t const *data = (uint8_t const *)save;
    uint32_t hash = 2166136261u;
    for (size_t i = offsetof(wowCharacterSave_t, inventory); i < size; i++)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static bool Wow_SaveItemsValid(wowHudIcon_t const *items, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        wowHudIcon_t const *item = &items[i];
        if (!memchr(item->icon, 0, sizeof(item->icon)) || !memchr(item->name, 0, sizeof(item->name))) return false;
        if ((item->icon[0] == 0) != (item->count == 0)) return false;
    }
    return true;
}

bool Wow_CharacterSavePath(char const *path, wowClient_t const *client, wowEntityLocal_t const *player) {
    if (!path || !path[0] || !client || !player || client->quest_count > SV_MAX_QUEST_LOG) return false;
    wowCharacterSave_t save = {0};
    save.magic = 0x3153574fu; save.version = 1; save.size = sizeof(save);
    memcpy(save.inventory, client->inventory, sizeof(save.inventory));
    memcpy(save.bank, client->bank, sizeof(save.bank));
    save.copper = player->copper; save.experience = player->experience;
    save.fireball_rank2 = client->fireball_rank2_learned;
    save.quest_count = client->quest_count;
    for (uint32_t i = 0; i < save.quest_count; i++) {
        save.quest_id[i] = client->quest_log[i].quest_id;
        save.quest_status[i] = client->quest_log[i].status;
        save.quest_tracked[i] = client->quest_tracked[i];
    }
    memcpy(save.kill_progress, client->kill_progress, sizeof(save.kill_progress));
    memcpy(save.rewarded, client->rewarded_quests, sizeof(save.rewarded));
    memcpy(save.taxi, client->taxi_known, sizeof(save.taxi));
    memcpy(save.explored, client->exploration.visited, sizeof(save.explored));
    memcpy(save.action_spells, client->action_spells, sizeof(save.action_spells));
    save.checksum = Wow_SaveChecksum(&save, sizeof(save));
    char tmp[1024];
    static unsigned sequence;
    FILE *f = NULL;
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    for (unsigned attempt = 0; attempt < 100 && !f; attempt++) {
        if (snprintf(tmp, sizeof(tmp), "%s.tmp.%lu.%u", path, pid, ++sequence) >= (int)sizeof(tmp)) return false;
        f = fopen(tmp, "wbx");
        if (!f && errno != EEXIST) return false;
    }
    if (!f) return false;
    bool ok = fwrite(&save, 1, sizeof(save), f) == sizeof(save) && fflush(f) == 0;
#ifdef _WIN32
    if (ok) ok = _commit(_fileno(f)) == 0;
#else
    if (ok) ok = fsync(fileno(f)) == 0;
#endif
    if (fclose(f) != 0) ok = false;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(tmp, path) == 0;
#endif
    }
    if (!ok) remove(tmp);
    return ok;
}

int Wow_CharacterLoadPath(char const *path, wowClient_t *client, wowEntityLocal_t *player) {
    if (!path || !path[0] || !client || !player) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 0 : -1;
    wowCharacterSave_t save = {0};
    size_t read_size = fread(&save, 1, sizeof(save), f);
    bool ok = fgetc(f) == EOF && !ferror(f) &&
        (read_size == sizeof(save) || read_size == offsetof(wowCharacterSave_t, action_spells));
    fclose(f);
    if (!ok || save.magic != 0x3153574fu || save.version != 1 || save.size != read_size ||
        save.checksum != Wow_SaveChecksum(&save, read_size) || save.fireball_rank2 > 1 || save.quest_count > SV_MAX_QUEST_LOG ||
        !Wow_SaveItemsValid(save.inventory, WOW_UI_INVENTORY_SLOTS) || !Wow_SaveItemsValid(save.bank, 16)) return -1;
    for (uint32_t i = 0; i < save.quest_count; i++)
        if (!save.quest_id[i] || save.quest_status[i] < SV_QUEST_ACTIVE || save.quest_status[i] > SV_QUEST_COMPLETE || save.quest_tracked[i] > 1) return -1;
    memcpy(client->inventory, save.inventory, sizeof(save.inventory));
    memcpy(client->bank, save.bank, sizeof(save.bank));
    client->inventory_initialized = true;
    player->copper = save.copper; player->experience = save.experience;
    client->fireball_rank2_learned = save.fireball_rank2 != 0;
    client->quest_count = save.quest_count;
    memset(client->quest_log, 0, sizeof(client->quest_log));
    memset(client->quest_tracked, 0, sizeof(client->quest_tracked));
    for (uint32_t i = 0; i < save.quest_count; i++) {
        client->quest_log[i].quest_id = save.quest_id[i];
        client->quest_log[i].status = (svQuestStatus_t)save.quest_status[i];
        client->quest_tracked[i] = save.quest_tracked[i] != 0;
    }
    memcpy(client->kill_progress, save.kill_progress, sizeof(save.kill_progress));
    memcpy(client->rewarded_quests, save.rewarded, sizeof(save.rewarded));
    memcpy(client->taxi_known, save.taxi, sizeof(save.taxi));
    memcpy(client->exploration.visited, save.explored, sizeof(save.explored));
    if (read_size == sizeof(save)) {
        uint32_t class_id = gi.GetConfigstring ? Wow_GetPlayerClass() : WOW_CLASS_WARRIOR;
        for (uint32_t i = 0; i < WOW_UI_ACTION_SLOTS; i++) {
            uint32_t spell = save.action_spells[i];
            if (spell != SPELL_NONE && (!Wow_SpellKnown(class_id, spell) ||
                (spell == WOW_SPELL_FIREBALL_RANK2 && !client->fireball_rank2_learned))) spell = SPELL_NONE;
            client->action_spells[i] = spell;
            memset(&client->actions[i], 0, sizeof(client->actions[i]));
            if (spell != SPELL_NONE) {
                snprintf(client->actions[i].icon, sizeof(client->actions[i].icon), "%s", Wow_SpellIcon(spell));
                snprintf(client->actions[i].name, sizeof(client->actions[i].name), "%s", wow_spells[spell].name);
                client->actions[i].count = 1;
            }
        }
    }
    return 1;
}
