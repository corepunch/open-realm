#ifdef WC3_SC2API

#include "sc2api_data.h"
#include "sc2api_map.h"


static wc3Sc2WeaponTarget_t sc2api_weapon_target(uint32_t mask) {
    bool const ground = (mask & (WC3_TARGET_FLAG_GROUND | WC3_TARGET_FLAG_STRUCTURE)) != 0;
    bool const air = (mask & WC3_TARGET_FLAG_AIR) != 0;
    if (ground && air) return WC3_SC2_WEAPON_ANY;
    if (air) return WC3_SC2_WEAPON_AIR;
    return WC3_SC2_WEAPON_GROUND;
}

static void sc2api_append_weapon(wc3Sc2UnitTypeData_t *out, UnitWeapon_t const *weapon) {
    wc3Sc2WeaponData_t *dst;
    if (!out || !weapon || out->weapon_count >= 2 || weapon->cooldown <= 0.0f) return;
    if (!(weapon->targetsAllowed & (WC3_TARGET_FLAG_GROUND | WC3_TARGET_FLAG_AIR | WC3_TARGET_FLAG_STRUCTURE))) return;
    dst = &out->weapons[out->weapon_count++];
    dst->target = sc2api_weapon_target((uint32_t)weapon->targetsAllowed);
    dst->damage = MAX(0.0f, weapon->averageDamage);
    dst->attacks = 1;
    dst->range = WC3_SC2API_WorldToBoardDistance(MAX(0.0f, weapon->range));
    dst->speed = MAX(0.0f, weapon->cooldown);
}

static wc3Sc2Race_t sc2api_unit_race(cstring_t race) {
    if (!race) return WC3_SC2_RACE_NONE;
    if (!strcasecmp(race, "human")) return WC3_SC2_RACE_HUMAN;
    if (!strcasecmp(race, "orc")) return WC3_SC2_RACE_ORC;
    if (!strcasecmp(race, "undead")) return WC3_SC2_RACE_UNDEAD;
    if (!strcasecmp(race, "nightelf")) return WC3_SC2_RACE_NIGHT_ELF;
    return WC3_SC2_RACE_NONE;
}

bool WC3_SC2API_FillUnitTypeData(uint32_t unit_type, wc3Sc2UnitTypeData_t *out) {
    UnitBalance_t const *balance;
    UnitData_t const *data;
    UnitWeapons_t const *weapons;

    if (!out) return false;
    balance = G_UnitBalance(unit_type);
    data = G_UnitData(unit_type);
    weapons = G_UnitWeapons(unit_type);
    if (!balance || !balance->id) return false;

    memset(out, 0, sizeof(*out));
    out->unit_type_id = unit_type;
    out->name = G_UnitName(unit_type);
    out->available = true;
    out->cargo_size = data ? (uint32_t)MAX(0, data->cargoSize) : 0;
    out->mineral_cost = MAX(0, balance->lumberCost);
    out->vespene_cost = MAX(0, balance->goldCost);
    out->food_required = (float)MAX(0, balance->foodUsed);
    out->food_provided = (float)MAX(0, balance->foodMade);
    out->movement_speed = WC3_SC2API_WorldToBoardDistance(MAX(0.0f, balance->speed));
    out->armor = balance->armor;
    out->is_structure = G_UnitIsBuilding(unit_type);
    out->is_heroic = balance->strength > 0 || balance->agility > 0 || balance->intelligence > 0;
    out->has_ability_id = true;
    if (weapons) {
        if (weapons->attacksEnabled & 1) sc2api_append_weapon(out, &weapons->attack1);
        if (weapons->attacksEnabled & 2) sc2api_append_weapon(out, &weapons->attack2);
    }
    out->build_time_seconds = (float)MAX(0, balance->buildTime);
    out->sight_range = WC3_SC2API_WorldToBoardDistance(MAX(0.0f, balance->sightRadius));
    out->race = sc2api_unit_race(data ? data->race : NULL);
    return true;
}

uint32_t WC3_SC2API_UnitTypeDataCapacity(void) {
    uint32_t custom = level.mapinfo ? level.mapinfo->num_userCreatedUnits : 0;
    return g_UnitBalanceCount + custom + g_DestructableDataCount +
        (level.mapinfo ? level.mapinfo->num_userCreatedDestructables : 0);
}

static bool sc2api_unit_type_already_written(wc3Sc2UnitTypeData_t const *out,
                                              uint32_t count, uint32_t unit_type) {
    FOR_LOOP(i, count) if (out[i].unit_type_id == unit_type) return true;
    return false;
}

static bool sc2api_fill_destructable_type(uint32_t rawcode, wc3Sc2UnitTypeData_t *out) {
    DestructableData_t const *data = G_DestructableData(rawcode);
    cstring_t name;

    if (!out || !data || data->id != rawcode || !data->file) return false;
    memset(out, 0, sizeof(*out));
    out->unit_type_id = WC3_SC2API_DestructableTypeId(rawcode);
    name = data->displayName && *data->displayName ? G_LevelString(data->displayName) : NULL;
    out->name = name && *name ? name : GetClassName(rawcode);
    out->available = true;
    out->has_ability_id = false;
    out->movement_speed = 0.0f;
    out->armor = 0.0f; /*DestructableData armor is a material/armor type, not armor points. */
    out->build_time_seconds = 0.0f;
    out->sight_range = 0.0f;
    out->race = WC3_SC2_RACE_NONE;
    return true;
}

uint32_t WC3_SC2API_BuildUnitTypeData(wc3Sc2UnitTypeData_t *out, uint32_t max_out) {
    uint32_t count = 0;

    if (!out || !max_out) return 0;
    FOR_LOOP(i, g_UnitBalanceCount) {
        uint32_t const unit_type = g_UnitBalance[i].id;
        if (!unit_type || count >= max_out ||
            sc2api_unit_type_already_written(out, count, unit_type)) continue;
        if (WC3_SC2API_FillUnitTypeData(unit_type, &out[count])) count++;
    }
    if (level.mapinfo) FOR_LOOP(i, level.mapinfo->num_userCreatedUnits) {
        uint32_t const unit_type = level.mapinfo->userCreatedUnits[i].newUnitID;
        if (!unit_type || count >= max_out ||
            sc2api_unit_type_already_written(out, count, unit_type)) continue;
        if (WC3_SC2API_FillUnitTypeData(unit_type, &out[count])) count++;
    }
    uint32_t const custom_destructables = level.mapinfo ? level.mapinfo->num_userCreatedDestructables : 0;
    FOR_LOOP(i, g_DestructableDataCount + custom_destructables) {
        uint32_t const rawcode = i < g_DestructableDataCount ? g_DestructableData[i].id :
            level.mapinfo->userCreatedDestructables[i - g_DestructableDataCount].newUnitID;
        uint32_t const type_id = WC3_SC2API_DestructableTypeId(rawcode);
        if (!rawcode || count >= max_out || sc2api_unit_type_already_written(out, count, type_id)) continue;
        if (sc2api_fill_destructable_type(rawcode, &out[count])) count++;
    }
    return count;
}


bool WC3_SC2API_FillUpgradeData(uint32_t upgrade_id, wc3Sc2UpgradeData_t *out) {
    UpgradeData_t const *upgrade;

    if (!out || !upgrade_id) return false;
    upgrade = G_UpgradeData(upgrade_id);
    if (!upgrade || upgrade->id != upgrade_id) return false;

    memset(out, 0, sizeof(*out));
    out->upgrade_id = upgrade_id;
    out->name = G_ObjectName(upgrade_id);
    /*Keep the established compatibility convention: SC2 minerals are WC3
     * lumber and SC2 vespene is WC3 gold. Static upgrade costs represent the
     * first authored research level, matching SC2 UpgradeData's single cost. */
    out->mineral_cost = MAX(0, G_UpgradeLumberCost(upgrade_id, 1));
    out->vespene_cost = MAX(0, G_UpgradeGoldCost(upgrade_id, 1));
    out->research_time = MAX(0.0f, G_UpgradeResearchTime(upgrade_id, 1));
    out->ability_id = upgrade_id;
    return true;
}

uint32_t WC3_SC2API_UpgradeDataCapacity(void) {
    return g_UpgradeDataCount;
}

uint32_t WC3_SC2API_BuildUpgradeData(wc3Sc2UpgradeData_t *out, uint32_t max_out) {
    uint32_t count = 0;

    if (!out || !max_out) return 0;
    FOR_LOOP(i, g_UpgradeDataCount) {
        uint32_t const upgrade_id = g_UpgradeData[i].id;
        if (!upgrade_id || count >= max_out) continue;
        if (WC3_SC2API_FillUpgradeData(upgrade_id, &out[count])) count++;
    }
    return count;
}

bool WC3_SC2API_FillBuffData(uint32_t buff_id, wc3Sc2BuffData_t *out) {
    AbilityBuffData_t const *buff;

    if (!out || !buff_id) return false;
    buff = G_AbilityBuffData(buff_id);
    if (!buff || buff->id != buff_id) return false;
    memset(out, 0, sizeof(*out));
    out->buff_id = buff_id;
    out->name = buff->buffTip && *buff->buffTip ? G_LevelString(buff->buffTip) : G_ObjectName(buff_id);
    return true;
}

uint32_t WC3_SC2API_BuffDataCapacity(void) {
    return g_AbilityBuffDataCount;
}

uint32_t WC3_SC2API_BuildBuffData(wc3Sc2BuffData_t *out, uint32_t max_out) {
    uint32_t count = 0;

    if (!out || !max_out) return 0;
    FOR_LOOP(i, g_AbilityBuffDataCount) {
        uint32_t const buff_id = g_AbilityBuffData[i].id;
        if (!buff_id || count >= max_out) continue;
        if (WC3_SC2API_FillBuffData(buff_id, &out[count])) count++;
    }
    return count;
}

#endif
