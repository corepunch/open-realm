#ifndef WC3_SC2API_DATA_H
#define WC3_SC2API_DATA_H

#include "sc2api_compat.h"

typedef enum {
    WC3_SC2_WEAPON_GROUND = 1,
    WC3_SC2_WEAPON_AIR = 2,
    WC3_SC2_WEAPON_ANY = 3,
} wc3Sc2WeaponTarget_t;

typedef struct {
    wc3Sc2WeaponTarget_t target;
    FLOAT damage;
    DWORD attacks;
    FLOAT range;
    FLOAT speed;
} wc3Sc2WeaponData_t;

typedef struct {
    DWORD unit_type_id;
    LPCSTR name; /* Loaded WC3 display/catalog name; serializer copies it. */
    BOOL available;
    DWORD cargo_size;
    LONG mineral_cost; /* Warcraft lumber. */
    LONG vespene_cost; /* Warcraft gold. */
    FLOAT food_required;
    FLOAT food_provided;
    FLOAT movement_speed;
    FLOAT armor;
    BOOL is_structure;
    BOOL is_heroic;
    BOOL has_ability_id;
    wc3Sc2WeaponData_t weapons[2];
    DWORD weapon_count;
    FLOAT build_time_seconds; /* WC3 authored seconds. */
    FLOAT sight_range;
    wc3Sc2Race_t race;
} wc3Sc2UnitTypeData_t;

typedef struct {
    DWORD upgrade_id;
    LPCSTR name; /* Resolved WC3 object/upgrade name; serializer copies it. */
    LONG mineral_cost; /* Warcraft lumber, level 1. */
    LONG vespene_cost; /* Warcraft gold, level 1. */
    FLOAT research_time; /* WC3 authored seconds, level 1. */
    DWORD ability_id; /* WC3 research rawcode/order id. */
} wc3Sc2UpgradeData_t;

typedef struct {
    DWORD buff_id;
    LPCSTR name; /* Resolved WC3 buff tooltip/name; serializer copies it. */
} wc3Sc2BuffData_t;

BOOL WC3_SC2API_FillUnitTypeData(DWORD unit_type, wc3Sc2UnitTypeData_t *out);
/* Upper bound for caller allocation; BuildUnitTypeData returns rows actually
 * written and de-duplicates map-created unit ids. */
DWORD WC3_SC2API_UnitTypeDataCapacity(void);
DWORD WC3_SC2API_BuildUnitTypeData(wc3Sc2UnitTypeData_t *out, DWORD max_out);

BOOL WC3_SC2API_FillUpgradeData(DWORD upgrade_id, wc3Sc2UpgradeData_t *out);
DWORD WC3_SC2API_UpgradeDataCapacity(void);
DWORD WC3_SC2API_BuildUpgradeData(wc3Sc2UpgradeData_t *out, DWORD max_out);

BOOL WC3_SC2API_FillBuffData(DWORD buff_id, wc3Sc2BuffData_t *out);
DWORD WC3_SC2API_BuffDataCapacity(void);
DWORD WC3_SC2API_BuildBuffData(wc3Sc2BuffData_t *out, DWORD max_out);

#endif
