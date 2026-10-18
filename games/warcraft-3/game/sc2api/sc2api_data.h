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
    float damage;
    uint32_t attacks;
    float range;
    float speed;
} wc3Sc2WeaponData_t;

typedef struct {
    uint32_t unit_type_id;
    cstring_t name; /*Loaded WC3 display/catalog name; serializer copies it. */
    bool available;
    uint32_t cargo_size;
    int32_t mineral_cost; /* Warcraft lumber. */
    int32_t vespene_cost; /* Warcraft gold. */
    float food_required;
    float food_provided;
    float movement_speed;
    float armor;
    bool is_structure;
    bool is_heroic;
    bool has_ability_id;
    wc3Sc2WeaponData_t weapons[2];
    uint32_t weapon_count;
    float build_time_seconds; /*WC3 authored seconds. */
    float sight_range;
    wc3Sc2Race_t race;
} wc3Sc2UnitTypeData_t;

typedef struct {
    uint32_t upgrade_id;
    cstring_t name; /*Resolved WC3 object/upgrade name; serializer copies it. */
    int32_t mineral_cost; /* Warcraft lumber, level 1. */
    int32_t vespene_cost; /* Warcraft gold, level 1. */
    float research_time; /*WC3 authored seconds, level 1. */
    uint32_t ability_id; /*WC3 research rawcode/order id. */
} wc3Sc2UpgradeData_t;

typedef struct {
    uint32_t buff_id;
    cstring_t name; /*Resolved WC3 buff tooltip/name; serializer copies it. */
} wc3Sc2BuffData_t;

bool WC3_SC2API_FillUnitTypeData(uint32_t unit_type, wc3Sc2UnitTypeData_t *out);
/*Upper bound for caller allocation; BuildUnitTypeData returns rows actually
 * written and de-duplicates map-created unit ids. */
uint32_t WC3_SC2API_UnitTypeDataCapacity(void);
uint32_t WC3_SC2API_BuildUnitTypeData(wc3Sc2UnitTypeData_t *out, uint32_t max_out);

bool WC3_SC2API_FillUpgradeData(uint32_t upgrade_id, wc3Sc2UpgradeData_t *out);
uint32_t WC3_SC2API_UpgradeDataCapacity(void);
uint32_t WC3_SC2API_BuildUpgradeData(wc3Sc2UpgradeData_t *out, uint32_t max_out);

bool WC3_SC2API_FillBuffData(uint32_t buff_id, wc3Sc2BuffData_t *out);
uint32_t WC3_SC2API_BuffDataCapacity(void);
uint32_t WC3_SC2API_BuildBuffData(wc3Sc2BuffData_t *out, uint32_t max_out);

#endif
