#ifndef WOW_WEAPON_DATA_H
#define WOW_WEAPON_DATA_H

#include "common/shared.h"

typedef struct WOWWEAPON {
    DWORD entry;
    LPCSTR name;
    DWORD subclass;
    DWORD display_id;
    DWORD inventory_type;
    DWORD item_level;
    DWORD required_level;
    FLOAT damage_min;
    FLOAT damage_max;
    DWORD damage_type;
    DWORD delay;
} WOWWEAPON;

typedef const WOWWEAPON *LPCWOWWEAPON;

LPCWOWWEAPON Wow_WeaponByEntry(DWORD entry);
DWORD Wow_RollWeaponDamage(DWORD entry);

#endif
