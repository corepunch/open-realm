#include "wow_weapon_data.h"
#include <stdio.h>
#include <stdlib.h>

static const WOWWEAPON wow_weapons[] = {
    { 25, "Worn Shortsword", 7, 1542, 21, 2, 1, 1.0f, 3.0f, 0, 1900 },
    { 35, "Bent Staff", 10, 472, 17, 2, 1, 3.0f, 5.0f, 0, 2900 },
    { 36, "Worn Mace", 4, 5194, 21, 2, 1, 1.0f, 3.0f, 0, 1900 },
    { 37, "Worn Axe", 0, 14029, 21, 2, 1, 1.0f, 3.0f, 0, 2000 },
};

LPCWOWWEAPON Wow_WeaponByEntry(DWORD entry) {
    FOR_LOOP(i, sizeof(wow_weapons) / sizeof(wow_weapons[0]))
        if (wow_weapons[i].entry == entry) return &wow_weapons[i];
    return NULL;
}

DWORD Wow_RollWeaponDamage(DWORD entry) {
    static DWORD last_missing = ~0u;
    LPCWOWWEAPON weapon = Wow_WeaponByEntry(entry);
    DWORD min_damage, max_damage;

    if (!entry) return 1;
    if (!weapon) {
        if (last_missing != entry) {
            fprintf(stderr, "WoW: missing server weapon data for item %u; using 1 damage\n", entry);
            last_missing = entry;
        }
        return 1;
    }
    min_damage = (DWORD)weapon->damage_min;
    max_damage = (DWORD)weapon->damage_max;
    return min_damage + (max_damage > min_damage ? (DWORD)(rand() % (max_damage - min_damage + 1)) : 0);
}
