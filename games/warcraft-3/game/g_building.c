#include "g_local.h"

#define WC3_BUILD_CELL_SIZE 32.0f
#define WC3_BUILD_GRID_SIZE 64.0f
#define WC3_BUILD_START_LIFE 0.10f
#define WC3_BUILD_CANCEL_REFUND_PERCENT 75 // percent; base construction-cancel refund
#define WC3_PATH_UNWALKABLE 0x02
#define WC3_PATH_UNBUILDABLE 0x08
#define WC3_PATH_BLIGHTED 0x20
#define ID_UPGRADE_EFFECT_ATTACK_DAMAGE MAKEFOURCC('r', 'a', 't', 'x')
#define ID_UPGRADE_EFFECT_ATTACK_DICE   MAKEFOURCC('r', 'a', 't', 'd')
#define ID_UPGRADE_EFFECT_ARMOR         MAKEFOURCC('r', 'a', 'r', 'm')

static BYTE G_PlacementFlags(LPCSTR list) {
    BYTE flags = 0;
    LPCSTR p = list;

    if (!list) return 0;
    while (*p) {
        char token[64];
        DWORD n = 0;

        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        while (*p && *p != ',' && n + 1 < sizeof(token)) {
            token[n++] = (char)tolower((unsigned char)*p++);
        }
        token[n] = '\0';
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
        while (n && (token[n - 1] == ' ' || token[n - 1] == '\t')) token[--n] = '\0';

        if (!token[0] || !strcmp(token, "_")) continue;
        if (!strcmp(token, "unwalkable")) {
            flags |= WC3_PATH_UNWALKABLE;
        } else if (!strcmp(token, "unbuildable")) {
            flags |= WC3_PATH_UNBUILDABLE;
        } else if (!strcmp(token, "blighted")) {
            flags |= WC3_PATH_BLIGHTED;
        } else {
            /* TODO: decode the remaining Warcraft placement predicates from the
             * authoritative unit data instead of silently treating them as no-op. */
            fprintf(stderr, "G_PlacementFlags: unsupported placement type '%s'\n", token);
        }
    }
    return flags;
}

static DWORD G_CsvToken(LPCSTR list, DWORD index, LPSTR out, DWORD out_size) {
    DWORD current = 0;
    LPCSTR p = list;

    if (!out || !out_size) return 0;
    out[0] = '\0';
    if (!list) return 0;
    while (*p) {
        LPCSTR start;
        DWORD len;
        while (*p == ',' || *p == ' ' || *p == '\t') p++;
        start = p;
        while (*p && *p != ',') p++;
        len = (DWORD)(p - start);
        while (len && (start[len - 1] == ' ' || start[len - 1] == '\t')) len--;
        if (current++ == index) {
            len = MIN(len, out_size - 1);
            memcpy(out, start, len);
            out[len] = '\0';
            return len;
        }
        if (*p == ',') p++;
    }
    return 0;
}

BOOL G_BuildAllEnabled(void) {
    return atoi(gi.CvarString("wc3_build_all", "0")) != 0;
}

static LONG G_FindTechSlot(LPGAMECLIENT client, DWORD techid, BOOL create) {
    LONG free_slot = -1;

    if (!client || !techid) return -1;
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        if (client->tech[i].id == techid) return (LONG)i;
        if (!client->tech[i].id && free_slot < 0) free_slot = (LONG)i;
    }
    if (!create) return -1;
    if (free_slot < 0) {
        fprintf(stderr, "G_FindTechSlot: player %u tech state capacity %u exhausted for 0x%08x\n",
                (unsigned)client->ps.number, (unsigned)MAX_PLAYER_TECH_STATE, (unsigned)techid);
        return -1;
    }
    client->tech[free_slot].id = techid;
    client->tech[free_slot].max_allowed = -1;
    return free_slot;
}

static BOOL G_UnitUsesUpgrade(LPCEDICT unit, DWORD upgrade_id) {
    char token[64];
    LPCSTR upgrades;

    if (!unit || !unit->data.UnitBalance || !upgrade_id) return false;
    upgrades = unit->data.UnitBalance->upgrades;
    for (DWORD i = 0; G_CsvToken(upgrades, i, token, sizeof(token)); i++) {
        if (strlen(token) == 4 && !memcmp(token, &upgrade_id, 4)) return true;
    }
    return false;
}

DWORD G_GetUnitUpgradeForClass(LPCEDICT unit, LPCSTR wanted_class) {
    char token[64];
    LPCSTR upgrades;

    if (!unit || !unit->data.UnitBalance || !wanted_class || !*wanted_class) return 0;
    upgrades = unit->data.UnitBalance->upgrades;
    for (DWORD i = 0; G_CsvToken(upgrades, i, token, sizeof(token)); i++) {
        DWORD upgrade_id;
        UpgradeData_t const *upgrade;

        if (strlen(token) != 4) continue;
        memcpy(&upgrade_id, token, sizeof(upgrade_id));
        upgrade = G_UpgradeData(upgrade_id);
        if (upgrade && upgrade->id == upgrade_id && upgrade->upgradeClass &&
            !strcasecmp(upgrade->upgradeClass, wanted_class)) {
            return upgrade_id;
        }
    }
    return 0;
}

static FLOAT G_UpgradeEffectValue(UpgradeData_t const *upgrade, DWORD effect, LONG level_value) {
    if (!upgrade || effect >= 4 || level_value <= 0) return 0.0f;
    return upgrade->effectBase[effect] + upgrade->effectMod[effect] * (FLOAT)(level_value - 1);
}

static void G_ApplyUpgradeLevelDelta(LPEDICT unit, UpgradeData_t const *upgrade,
                                     LONG old_level, LONG new_level) {
    BOOL changed = false;

    if (!unit || !upgrade || old_level == new_level || !G_UnitUsesUpgrade(unit, upgrade->id)) return;

    FOR_LOOP(i, 4) {
        DWORD const effect = upgrade->effect[i];
        if (!effect) continue;
        if (effect == ID_UPGRADE_EFFECT_ATTACK_DAMAGE) {
            LONG const old_value = (LONG)G_UpgradeEffectValue(upgrade, i, old_level);
            LONG const new_value = (LONG)G_UpgradeEffectValue(upgrade, i, new_level);
            LONG const delta = new_value - old_value;

            if (delta && unit->attack1.numberOfDice) {
                unit->attack1.permanentDamageBonus += (FLOAT)delta;
                unit->attack1.damageBase = (DWORD)MAX(0, (LONG)unit->attack1.damageBase + delta);
                changed = true;
            }
            if (delta && unit->attack2.numberOfDice) {
                unit->attack2.permanentDamageBonus += (FLOAT)delta;
                unit->attack2.damageBase = (DWORD)MAX(0, (LONG)unit->attack2.damageBase + delta);
                changed = true;
            }
        } else if (effect == ID_UPGRADE_EFFECT_ATTACK_DICE) {
            LONG const old_value = (LONG)G_UpgradeEffectValue(upgrade, i, old_level);
            LONG const new_value = (LONG)G_UpgradeEffectValue(upgrade, i, new_level);
            LONG const delta = new_value - old_value;

            if (delta && unit->attack1.numberOfDice) {
                unit->attack1.numberOfDice = MAX(0, (LONG)unit->attack1.numberOfDice + delta);
                changed = true;
            }
            if (delta && unit->attack2.numberOfDice) {
                unit->attack2.numberOfDice = MAX(0, (LONG)unit->attack2.numberOfDice + delta);
                changed = true;
            }
        } else if (effect == ID_UPGRADE_EFFECT_ARMOR) {
            FLOAT const delta = unit->data.UnitBalance->armorPerUpgrade * (FLOAT)(new_level - old_level);
            if (delta != 0.0f) {
                unit->permanent_armor_bonus += delta;
                unit->armor_value += delta;
                changed = true;
            }
        }
    }
    if (changed) G_InvalidateUnitInfoPanel(unit);
}

static void G_ApplyTechLevelToOwnedUnits(LPGAMECLIENT client, DWORD techid,
                                         LONG old_level, LONG new_level) {
    UpgradeData_t const *upgrade;
    DWORD player;

    if (!client || !techid || old_level == new_level) return;
    upgrade = G_UpgradeData(techid);
    if (!upgrade || upgrade->id != techid) return;
    player = client->ps.number;
    FILTER_EDICTS(unit, unit->inuse && unit->s.player == player && unit->data.UnitBalance) {
        G_ApplyUpgradeLevelDelta(unit, upgrade, old_level, new_level);
    }
}

void G_ApplyPlayerUpgradesToUnit(LPEDICT unit) {
    LPGAMECLIENT client;
    char token[64];
    LPCSTR upgrades;

    if (!unit || !unit->data.UnitBalance) return;
    client = G_GetPlayerClientByNumber(unit->s.player);
    if (!client || client->ps.number != unit->s.player) return;
    upgrades = unit->data.UnitBalance->upgrades;
    for (DWORD i = 0; G_CsvToken(upgrades, i, token, sizeof(token)); i++) {
        DWORD upgrade_id;
        UpgradeData_t const *upgrade;
        LONG level_value;

        if (strlen(token) != 4) continue;
        memcpy(&upgrade_id, token, sizeof(upgrade_id));
        upgrade = G_UpgradeData(upgrade_id);
        level_value = G_GetPlayerTechResearchedLevel(client, upgrade_id);
        if (upgrade && upgrade->id == upgrade_id && level_value > 0) {
            G_ApplyUpgradeLevelDelta(unit, upgrade, 0, level_value);
        }
    }
}

void G_SetPlayerTechMaxAllowed(LPGAMECLIENT client, DWORD techid, LONG maximum) {
    LONG slot = G_FindTechSlot(client, techid, true);
    if (slot < 0) return;
    client->tech[slot].max_allowed = maximum < 0 ? -1 : maximum;
    G_InvalidateCommands(client);
}

LONG G_GetPlayerTechMaxAllowed(LPGAMECLIENT client, DWORD techid) {
    LONG slot = G_FindTechSlot(client, techid, false);
    return slot < 0 ? -1 : client->tech[slot].max_allowed;
}

void G_SetPlayerTechResearched(LPGAMECLIENT client, DWORD techid, LONG level_value) {
    LONG slot = G_FindTechSlot(client, techid, true);
    LONG old_level;
    LONG new_level;

    if (slot < 0) return;
    old_level = MAX(0, client->tech[slot].researched);
    new_level = MAX(0, level_value);
    client->tech[slot].researched = new_level;
    G_ApplyTechLevelToOwnedUnits(client, techid, old_level, new_level);
    G_InvalidateCommands(client);
}

void G_AddPlayerTechResearched(LPGAMECLIENT client, DWORD techid, LONG levels) {
    LONG slot = G_FindTechSlot(client, techid, true);
    LONG old_level;
    LONG new_level;

    if (slot < 0) return;
    old_level = MAX(0, client->tech[slot].researched);
    new_level = MAX(0, old_level + levels);
    client->tech[slot].researched = new_level;
    G_ApplyTechLevelToOwnedUnits(client, techid, old_level, new_level);
    G_InvalidateCommands(client);
}

LONG G_GetPlayerTechResearchedLevel(LPGAMECLIENT client, DWORD techid) {
    LONG slot = G_FindTechSlot(client, techid, false);
    return slot < 0 ? 0 : MAX(0, client->tech[slot].researched);
}

LONG G_GetPlayerTechInProgress(LPGAMECLIENT client, DWORD techid) {
    LONG slot = G_FindTechSlot(client, techid, false);
    return slot < 0 ? 0 : MAX(0, client->tech[slot].in_progress);
}

void G_AddPlayerTechInProgress(LPGAMECLIENT client, DWORD techid, LONG levels) {
    LONG slot = G_FindTechSlot(client, techid, true);
    if (slot < 0) return;
    client->tech[slot].in_progress = MAX(0, client->tech[slot].in_progress + levels);
    G_InvalidateCommands(client);
}

LONG G_UpgradeGoldCost(DWORD upgrade_id, LONG level_value) {
    UpgradeData_t const *upgrade = G_UpgradeData(upgrade_id);
    if (!upgrade || upgrade->id != upgrade_id || level_value <= 0) return 0;
    return MAX(0, upgrade->goldBase + upgrade->goldMod * (level_value - 1));
}

LONG G_UpgradeLumberCost(DWORD upgrade_id, LONG level_value) {
    UpgradeData_t const *upgrade = G_UpgradeData(upgrade_id);
    if (!upgrade || upgrade->id != upgrade_id || level_value <= 0) return 0;
    return MAX(0, upgrade->lumberBase + upgrade->lumberMod * (level_value - 1));
}

FLOAT G_UpgradeResearchTime(DWORD upgrade_id, LONG level_value) {
    UpgradeData_t const *upgrade = G_UpgradeData(upgrade_id);
    if (!upgrade || upgrade->id != upgrade_id || level_value <= 0) return 0.0f;
    return (FLOAT)MAX(0, upgrade->timeBase + upgrade->timeMod * (level_value - 1));
}

LONG G_GetPlayerTechCountValue(LPGAMECLIENT client, DWORD techid) {
    LONG count = G_GetPlayerTechResearchedLevel(client, techid);
    DWORD player;

    if (!client || !techid) return 0;
    player = client->ps.number;
    /* A dead Hero still consumes its techtree/hero-limit slot.  Revival
     * restores the same object and must not make a second unlock available. */
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == techid && ent->s.player == player &&
                         (!(ent->svflags & SVF_DEADMONSTER) || G_UnitIsHero(ent))) {
        count++;
    }
    return count;
}

static BOOL G_ProducerContains(LPCSTR list, DWORD type_id) {
    char token[64];

    if (!list || !type_id) return false;
    for (DWORD i = 0; G_CsvToken(list, i, token, sizeof(token)); i++) {
        if (strlen(token) == 4 && !memcmp(token, &type_id, 4)) return true;
    }
    return false;
}

BOOL G_WorkerCanBuild(LPEDICT worker, DWORD building_id) {
    return worker && worker->data.UnitProfile &&
        G_ProducerContains(worker->data.UnitProfile->builds, building_id);
}

BOOL G_ProducerCanTrain(LPEDICT producer, DWORD unit_id) {
    return producer && producer->data.UnitProfile &&
        G_ProducerContains(producer->data.UnitProfile->trains, unit_id);
}

BOOL G_ProducerCanResearch(LPEDICT producer, DWORD upgrade_id) {
    return producer && producer->data.UnitProfile &&
        G_ProducerContains(producer->data.UnitProfile->researches, upgrade_id);
}

static LONG G_RequirementAmount(UnitProfile_t const *profile, DWORD index) {
    char amount[32];
    LONG value = 1;

    if (!profile || !profile->requiresAmount ||
        !G_CsvToken(profile->requiresAmount, index, amount, sizeof(amount))) return 1;
    if (sscanf(amount, "%d", &value) != 1) {
        fprintf(stderr, "G_RequirementAmount: invalid Requiresamount token '%s' at index %u\n",
                amount, (unsigned)index);
        return 1;
    }
    return MAX(1, value);
}

static LONG G_PlayerRequirementCount(LPGAMECLIENT client, DWORD techid) {
    LONG count = G_GetPlayerTechResearchedLevel(client, techid);
    DWORD player;

    if (!client || !techid) return 0;
    player = client->ps.number;
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == techid && ent->s.player == player &&
                         !(ent->svflags & SVF_DEADMONSTER) && !ent->construction.active && !ent->training) {
        count++;
    }
    return count;
}

static LPCSTR G_UpgradeLevelField(DWORD upgrade_id, LPCSTR base, LONG level_value) {
    static char fields[4][32];
    static DWORD cursor;
    LPSTR field = fields[cursor++ & 3];
    LONG suffix = MAX(0, level_value - 1);

    if (!upgrade_id || !base || !*base || level_value <= 0) return NULL;
    if (suffix == 0) snprintf(field, sizeof(fields[0]), "%s", base);
    else snprintf(field, sizeof(fields[0]), "%s%d", base, suffix);
    return FindConfigValue(GetClassName(upgrade_id), field);
}

static LONG G_UpgradeRequirementAmount(DWORD upgrade_id, LONG level_value, DWORD index) {
    char amount[32];
    LPCSTR amounts = G_UpgradeLevelField(upgrade_id, "Requiresamount", level_value);
    LONG value = 1;

    if (!amounts || !G_CsvToken(amounts, index, amount, sizeof(amount))) return 1;
    if (sscanf(amount, "%d", &value) != 1) return 1;
    return MAX(1, value);
}

static BOOL G_UpgradeRequirementsSatisfied(LPGAMECLIENT client, DWORD upgrade_id, LONG level_value,
                                           LPSTR reason, DWORD reason_size) {
    LPCSTR requirements = G_UpgradeLevelField(upgrade_id, "Requires", level_value);
    char requirement[64];

    if (!requirements || !*requirements || !strcmp(requirements, "_")) return true;
    for (DWORD i = 0; G_CsvToken(requirements, i, requirement, sizeof(requirement)); i++) {
        DWORD rawcode;
        LONG required;
        LPCSTR name;

        if (strlen(requirement) != 4) continue;
        memcpy(&rawcode, requirement, sizeof(rawcode));
        required = G_UpgradeRequirementAmount(upgrade_id, level_value, i);
        if (G_PlayerRequirementCount(client, rawcode) >= required) continue;

        if (reason && reason_size) {
            name = G_UnitProfile(rawcode)->name;
            if (!name || !*name) name = FindConfigValue(GetClassName(rawcode), "Name");
            if (required > 1) {
                snprintf(reason, reason_size, "Requires %s x%d",
                         name && *name ? name : requirement, required);
            } else {
                snprintf(reason, reason_size, "Requires %s",
                         name && *name ? name : requirement);
            }
        }
        return false;
    }
    return true;
}

static BOOL G_RequirementsSatisfied(LPGAMECLIENT client, DWORD type_id, LPSTR reason, DWORD reason_size) {
    UnitProfile_t const *profile = G_UnitProfile(type_id);
    char requirement[64];

    if (!profile->requires || !*profile->requires) return true;
    for (DWORD i = 0; G_CsvToken(profile->requires, i, requirement, sizeof(requirement)); i++) {
        DWORD rawcode;
        LONG required;
        if (strlen(requirement) != 4) {
            fprintf(stderr, "G_RequirementsSatisfied: unsupported requirement token '%s' for 0x%08x\n",
                    requirement, (unsigned)type_id);
            continue;
        }
        memcpy(&rawcode, requirement, sizeof(rawcode));
        required = G_RequirementAmount(profile, i);
        if (G_PlayerRequirementCount(client, rawcode) < required) {
            if (reason && reason_size) {
                LPCSTR name = G_UnitProfile(rawcode)->name;
                if (required > 1) {
                    snprintf(reason, reason_size, "Requires %s x%d",
                             name && *name ? name : requirement, required);
                } else {
                    snprintf(reason, reason_size, "Requires %s",
                             name && *name ? name : requirement);
                }
            }
            return false;
        }
    }
    return true;
}

static BOOL G_ProductionResourcesAvailable(LPGAMECLIENT client, DWORD type_id, LPSTR reason, DWORD reason_size) {
    UnitBalance_t const *b = G_UnitBalance(type_id);

    if (!client) return false;
    if (b->goldCost > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD]) {
        if (reason && reason_size) snprintf(reason, reason_size, "Not enough gold");
        return false;
    }
    if (b->lumberCost > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]) {
        if (reason && reason_size) snprintf(reason, reason_size, "Not enough lumber");
        return false;
    }
    if (!G_PlayerHasFoodFor(client, MAX(0, b->foodUsed))) {
        if (reason && reason_size) snprintf(reason, reason_size, "Not enough food");
        return false;
    }
    return true;
}

buildCommandState_t G_GetBuildCommandState(LPGAMECLIENT client, LPEDICT worker, DWORD building_id,
                                           LPSTR reason, DWORD reason_size) {
    LONG maximum;

    if (reason && reason_size) reason[0] = '\0';
    if (!client || !G_WorkerCanBuild(worker, building_id)) return BUILD_COMMAND_ABSENT;
    if (!G_UnitIsBuilding(building_id)) return BUILD_COMMAND_ABSENT;
    if (G_BuildAllEnabled()) return BUILD_COMMAND_AVAILABLE;

    maximum = G_GetPlayerTechMaxAllowed(client, building_id);
    if (maximum >= 0 && G_GetPlayerTechCountValue(client, building_id) >= maximum) {
        return BUILD_COMMAND_HIDDEN;
    }
    if (!G_RequirementsSatisfied(client, building_id, reason, reason_size)) {
        return BUILD_COMMAND_DISABLED;
    }
    if (!G_ProductionResourcesAvailable(client, building_id, reason, reason_size)) {
        return BUILD_COMMAND_UNAFFORDABLE;
    }
    return BUILD_COMMAND_AVAILABLE;
}

buildCommandState_t G_GetTrainCommandState(LPGAMECLIENT client, LPEDICT producer, DWORD unit_id,
                                           LPSTR reason, DWORD reason_size) {
    LONG maximum;

    if (reason && reason_size) reason[0] = '\0';
    if (!client || !G_ProducerCanTrain(producer, unit_id)) return BUILD_COMMAND_ABSENT;
    if (!G_BuildAllEnabled()) {
        maximum = G_GetPlayerTechMaxAllowed(client, unit_id);
        if (maximum >= 0 && G_GetPlayerTechCountValue(client, unit_id) >= maximum) {
            return BUILD_COMMAND_HIDDEN;
        }
        if (!G_RequirementsSatisfied(client, unit_id, reason, reason_size)) {
            return BUILD_COMMAND_DISABLED;
        }
    }
    if (!G_ProductionResourcesAvailable(client, unit_id, reason, reason_size)) {
        return BUILD_COMMAND_UNAFFORDABLE;
    }
    return BUILD_COMMAND_AVAILABLE;
}

buildCommandState_t G_GetResearchCommandState(LPGAMECLIENT client, LPEDICT producer, DWORD upgrade_id,
                                              LONG *next_level, LPSTR reason, DWORD reason_size) {
    UpgradeData_t const *upgrade;
    LONG current;
    LONG maximum;
    LONG player_max;
    LONG level_value;

    if (reason && reason_size) reason[0] = '\0';
    if (next_level) *next_level = 0;
    if (!client || !G_ProducerCanResearch(producer, upgrade_id)) return BUILD_COMMAND_ABSENT;
    upgrade = G_UpgradeData(upgrade_id);
    if (!upgrade || upgrade->id != upgrade_id || upgrade->maxLevel <= 0) return BUILD_COMMAND_ABSENT;

    current = G_GetPlayerTechResearchedLevel(client, upgrade_id);
    level_value = current + 1;
    maximum = upgrade->maxLevel;
    player_max = G_GetPlayerTechMaxAllowed(client, upgrade_id);
    if (player_max >= 0) maximum = MIN(maximum, player_max);
    if (current >= maximum || G_GetPlayerTechInProgress(client, upgrade_id) > 0) {
        return BUILD_COMMAND_HIDDEN;
    }
    if (next_level) *next_level = level_value;

    if (!G_BuildAllEnabled() &&
        !G_UpgradeRequirementsSatisfied(client, upgrade_id, level_value, reason, reason_size)) {
        return BUILD_COMMAND_DISABLED;
    }
    if (G_UpgradeGoldCost(upgrade_id, level_value) > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD]) {
        if (reason && reason_size) snprintf(reason, reason_size, "Not enough gold");
        return BUILD_COMMAND_UNAFFORDABLE;
    }
    if (G_UpgradeLumberCost(upgrade_id, level_value) > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]) {
        if (reason && reason_size) snprintf(reason, reason_size, "Not enough lumber");
        return BUILD_COMMAND_UNAFFORDABLE;
    }
    return BUILD_COMMAND_AVAILABLE;
}

BOOL G_ChargeBuilding(LPGAMECLIENT client, DWORD building_id) {
    UnitBalance_t const *b;

    if (!client) return false;
    if (G_BuildAllEnabled()) return true;
    if (!G_ProductionResourcesAvailable(client, building_id, NULL, 0)) return false;
    b = G_UnitBalance(building_id);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= MAX(0, b->goldCost);
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= MAX(0, b->lumberCost);
    return true;
}

void G_RefundBuilding(LPGAMECLIENT client, DWORD building_id) {
    UnitBalance_t const *b;
    if (!client || G_BuildAllEnabled()) return;
    b = G_UnitBalance(building_id);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] += MAX(0, b->goldCost);
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] += MAX(0, b->lumberCost);
}

void G_SnapBuildingPoint(DWORD building_id, LPVECTOR2 point) {
    pathTex_t *pathtex;
    UnitData_t const *data;

    if (!point) return;
    data = G_UnitData(building_id);
    pathtex = M_LoadPathTex(data->pathingTexture);
    if (!pathtex) {
        point->x = floorf(point->x / WC3_BUILD_CELL_SIZE) * WC3_BUILD_CELL_SIZE;
        point->y = floorf(point->y / WC3_BUILD_CELL_SIZE) * WC3_BUILD_CELL_SIZE;
        return;
    }
    point->x = floorf(point->x / WC3_BUILD_GRID_SIZE) * WC3_BUILD_GRID_SIZE;
    point->y = floorf(point->y / WC3_BUILD_GRID_SIZE) * WC3_BUILD_GRID_SIZE;
    if (((pathtex->width / 2) & 1) != 0) point->x += WC3_BUILD_CELL_SIZE;
    if (((pathtex->height / 2) & 1) != 0) point->y += WC3_BUILD_CELL_SIZE;
    gi.MemFree(pathtex);
}

static BOOL G_PathCellUsed(pathTex_t const *pathtex, DWORD x, DWORD y) {
    if (!pathtex) return true;
    return pathtex->map[x + y * pathtex->width].b != 0;
}

static BOOL G_FindBuildOnTarget(DWORD building_id, LPCVECTOR2 point, LPEDICT *out) {
    UnitData_t const *data = G_UnitData(building_id);
    if (out) *out = NULL;
    if (!data->isBuildOn) return true;
    FILTER_EDICTS(ent, ent->inuse && G_UnitIsBuilding(ent->class_id) && ent->data.UnitData->canBuildOn) {
        if (fabsf(ent->s.origin2.x - point->x) <= WC3_BUILD_CELL_SIZE &&
            fabsf(ent->s.origin2.y - point->y) <= WC3_BUILD_CELL_SIZE) {
            if (out) *out = ent;
            return true;
        }
    }
    return false;
}

static BOOL G_LiveUnitBlocksBuild(LPEDICT builder, LPEDICT build_on, LPCBOX2 footprint) {
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) && !(ent->svflags & SVF_DEADMONSTER)) {
        FLOAT x, y;
        if (ent == builder || ent == build_on || ent->collision <= 0.0f) continue;
        x = MAX(footprint->min.x, MIN(footprint->max.x, ent->s.origin2.x));
        y = MAX(footprint->min.y, MIN(footprint->max.y, ent->s.origin2.y));
        VECTOR2 nearest = { x, y };
        if (Vector2_distance(&nearest, &ent->s.origin2) < ent->collision) return true;
    }
    return false;
}

buildPlacementResult_t G_EvaluateBuildPlacement(LPEDICT builder, DWORD building_id, LPCVECTOR2 requested,
                                                LPVECTOR2 snapped) {
    UnitBalance_t const *balance = G_UnitBalance(building_id);
    UnitUI_t const *ui = G_UnitUI(building_id);
    UnitData_t const *data = G_UnitData(building_id);
    LPCSTR prevent = balance->preventPlace ? balance->preventPlace : ui->preventPlace;
    LPCSTR require = balance->requirePlace ? balance->requirePlace : ui->requirePlace;
    BYTE prevented = WC3_PATH_UNBUILDABLE | WC3_PATH_UNWALKABLE | G_PlacementFlags(prevent);
    BYTE required = G_PlacementFlags(require);
    pathTex_t *pathtex = NULL;
    LPEDICT build_on = NULL;
    DWORD width = 1, height = 1;
    BOX2 footprint;
    VECTOR2 point;

    if (!requested || !G_UnitIsBuilding(building_id)) return PLACE_INVALID_BUILDING;
    point = *requested;
    G_SnapBuildingPoint(building_id, &point);
    if (snapped) *snapped = point;

    if (!G_FindBuildOnTarget(building_id, &point, &build_on)) return PLACE_REQUIRED_PARENT_MISSING;
    pathtex = M_LoadPathTex(data->pathingTexture);
    if (data->pathingTexture && strlen(data->pathingTexture) > 1 && !pathtex) {
        return PLACE_INVALID_BUILDING;
    }
    if (pathtex) {
        width = MAX(1, pathtex->width);
        height = MAX(1, pathtex->height);
    }
    footprint.min.x = point.x - width * WC3_BUILD_CELL_SIZE * 0.5f;
    footprint.min.y = point.y - height * WC3_BUILD_CELL_SIZE * 0.5f;
    footprint.max.x = point.x + width * WC3_BUILD_CELL_SIZE * 0.5f;
    footprint.max.y = point.y + height * WC3_BUILD_CELL_SIZE * 0.5f;

    if (!build_on) {
        FOR_LOOP(x, width) {
            FOR_LOOP(y, height) {
                VECTOR2 sample;
                BYTE flags;
                if (pathtex && !G_PathCellUsed(pathtex, x, y)) continue;
                sample.x = point.x + ((FLOAT)x + 0.5f - (FLOAT)width * 0.5f) * WC3_BUILD_CELL_SIZE;
                sample.y = point.y + ((FLOAT)y + 0.5f - (FLOAT)height * 0.5f) * WC3_BUILD_CELL_SIZE;
                if (!CM_GetPathingFlagsAt(&sample, &flags)) {
                    if (pathtex) gi.MemFree(pathtex);
                    return PLACE_OUT_OF_BOUNDS;
                }
                if (flags & prevented) {
                    if (pathtex) gi.MemFree(pathtex);
                    return PLACE_TERRAIN_BLOCKED;
                }
                if ((flags & required) != required) {
                    if (pathtex) gi.MemFree(pathtex);
                    return PLACE_REQUIRED_PATHING_MISSING;
                }
            }
        }
    }
    if (pathtex) gi.MemFree(pathtex);
    if (G_LiveUnitBlocksBuild(builder, build_on, &footprint)) return PLACE_UNIT_BLOCKED;
    return PLACE_OK;
}

FLOAT G_BuildApproachDistance(DWORD building_id) {
    pathTex_t *pathtex = M_LoadPathTex(G_UnitData(building_id)->pathingTexture);
    FLOAT result;
    if (!pathtex) return MAX(WC3_BUILD_CELL_SIZE, G_UnitCollision(building_id));
    result = MAX(pathtex->width, pathtex->height) * WC3_BUILD_CELL_SIZE * 0.5f;
    gi.MemFree(pathtex);
    return result;
}

void G_UpdateConstructionAnimation(LPEDICT building) {
    LPCANIMATION anim;
    FLOAT duration, fraction;
    DWORD first, last, span, frame;

    if (!building || !building->construction.active || !building->data.UnitBalance) return;
    if (building->data.UnitBalance->buildTime <= 0) return;

    /* Construction owns the birth sequence. Re-resolve it instead of relying
     * on whatever animation happened to be left on the entity by a previous
     * state transition. */
    anim = building->animation;
    if (!anim || strcasecmp(anim->name, "birth"))
        anim = G_GetAnimation(building->s.model, "birth");
    if (!anim || anim->interval[1] <= anim->interval[0]) return;
    building->animation = anim;

    duration = (FLOAT)building->data.UnitBalance->buildTime * 1000.0f;
    fraction = MAX(0.0f, MIN(1.0f, building->construction.progress / duration));
    first = anim->interval[0];
    last = anim->interval[1];
    span = last - first;
    frame = first + (DWORD)((FLOAT)span * fraction);
    if (frame >= last) frame = last - 1;
    building->s.frame = frame;
}

BOOL G_StartHumanConstruction(LPEDICT builder, LPEDICT building) {
    edictStat_s *hp;

    if (!builder || !building || !G_UnitIsBuilding(building->class_id)) return false;
    hp = &building->health;
    building->construction.active = true;
    building->construction.paused = true;
    building->construction.primary_builder = builder;
    building->construction.progress = 0.0f;
    building->construction.paid = false;
    building->construction.payer = 0;
    building->construction.gold = 0;
    building->construction.lumber = 0;
    building->aiflags |= AI_HOLD_FRAME;
    hp->value = MAX(1.0f, hp->max_value * WC3_BUILD_START_LIFE);

    G_UpdateConstructionAnimation(building);
    return true;
}

/* Construction teardown must release every Human Repair participant before the
 * target enters death/completion cleanup; otherwise workers retain pointers to
 * an entity whose construction state no longer exists. */
void G_StopConstruction(LPEDICT building) {
    if (!building || !building->construction.active) return;

    FILTER_EDICTS(worker, worker->inuse && worker != building && worker->build == building &&
                           worker->buildwork.ability) {
        S_CancelRepair(worker);
        if (worker->stand) worker->stand(worker);
    }

    /* The construction info panel historically used a self-linked build queue.
     * Clear it before unit_die() walks production/revival ownership. */
    if (building->build == building) building->build = NULL;
    building->construction.active = false;
    building->construction.paused = false;
    building->construction.primary_builder = NULL;
    building->construction.progress = 0.0f;
    building->construction.paid = false;
    building->construction.payer = 0;
    building->construction.gold = 0;
    building->construction.lumber = 0;
    building->aiflags &= ~AI_HOLD_FRAME;
}

static LONG G_ConstructionCancelRefund(LONG paid) {
    if (paid <= 0) return 0;
    return (paid * WC3_BUILD_CANCEL_REFUND_PERCENT) / 100;
}

/* A player cancellation is distinct from destruction: publish the Warcraft
 * construct-cancel events and refund only the recorded base construction
 * payment, then use ordinary unit death for selection/food/death semantics. */
BOOL G_CancelStructureConstruction(LPEDICT building) {
    LPGAMECLIENT payer;
    LONG gold, lumber;

    if (!building || !building->inuse || !building->construction.active ||
        (building->svflags & SVF_DEADMONSTER) || !G_UnitIsBuilding(building->class_id)) {
        return false;
    }

    gold = building->construction.paid
        ? G_ConstructionCancelRefund(building->construction.gold) : 0;
    lumber = building->construction.paid
        ? G_ConstructionCancelRefund(building->construction.lumber) : 0;
    payer = G_GetPlayerClientByNumber(building->construction.payer);

    G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_CANCEL);
    G_PublishEvent(building, EVENT_UNIT_CONSTRUCT_CANCEL);

    if (building->construction.paid && payer &&
        payer->ps.number == building->construction.payer) {
        payer->ps.stats[PLAYERSTATE_RESOURCE_GOLD] += gold;
        payer->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] += lumber;
        building->construction.paid = false;
    }

    unit_die(building, NULL);
    return true;
}

void G_CompleteConstruction(LPEDICT building) {
    LPGAMECLIENT client;
    if (!building || !building->construction.active) return;
    client = G_GetPlayerClientByNumber(building->s.player);
    if (client && client->ps.number != building->s.player) client = NULL;
    building->construction.active = false;
    building->construction.paused = false;
    building->construction.primary_builder = NULL;
    building->construction.progress = 0.0f;
    building->construction.paid = false;
    building->construction.payer = 0;
    building->construction.gold = 0;
    building->construction.lumber = 0;
    building->aiflags &= ~AI_HOLD_FRAME;
    building->health.value = building->health.max_value;
    building->stand(building);
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI construction complete building=%ld id=%.4s player=%u\n",
        (long)(building - g_edicts), (LPCSTR)&building->class_id, building->s.player);
#endif
    G_SetUnitFoodMade(building, building->data.UnitBalance->foodMade);
    G_QueueOwnerUISound(building, "JobDoneSound");
    G_SendOwnerMinimapAlert(building);
    G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_FINISH);
    if (client) {
        LPEDICT clent = G_GetPlayerEntityByNumber(client->ps.number);
        G_InvalidateCommands(client);
        G_RefreshResourceBar(clent);
        Get_Portrait_f(clent);
    }
}
