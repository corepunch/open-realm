#include "g_local.h"

#define WC3_BUILD_CELL_SIZE 32.0f
#define WC3_BUILD_GRID_SIZE 64.0f
#define WC3_BUILD_START_LIFE 0.10f
#define WC3_BUILD_CANCEL_REFUND_PERCENT 75 // percent; base construction-cancel refund
#define WC3_UNDEAD_BUILD_WORK_MS 2267 // milliseconds; Warsmash CBehaviorUndeadBuild summon-work window
#define WC3_PATH_UNWALKABLE 0x02
#define WC3_PATH_UNBUILDABLE 0x08
#define WC3_PATH_BLIGHTED 0x20
#define ID_UPGRADE_EFFECT_ATTACK_DAMAGE MAKEFOURCC('r', 'a', 't', 'x')
#define ID_UPGRADE_EFFECT_ATTACK_DICE   MAKEFOURCC('r', 'a', 't', 'd')
#define ID_UPGRADE_EFFECT_ARMOR         MAKEFOURCC('r', 'a', 'r', 'm')
#define ID_UPGRADE_EFFECT_MAX_MANA      MAKEFOURCC('r', 'm', 'n', 'x') // fourcc; maximum-mana upgrade effect
#define ID_UPGRADE_EFFECT_MANA_REGEN    MAKEFOURCC('r', 'm', 'n', 'r') // fourcc; mana-regeneration upgrade effect

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
        } else if (effect == ID_UPGRADE_EFFECT_MAX_MANA) {
            FLOAT const delta = G_UpgradeEffectValue(upgrade, i, new_level) -
                                G_UpgradeEffectValue(upgrade, i, old_level);
            if (delta != 0.0f) {
                unit->mana.max_value = MAX(0.0f, unit->mana.max_value + delta);
                unit->mana.value = MAX(0.0f, MIN(unit->mana.max_value, unit->mana.value + delta));
                changed = true;
            }
        } else if (effect == ID_UPGRADE_EFFECT_MANA_REGEN) {
            unit->mana_regen_bonus += G_UpgradeEffectValue(upgrade, i, new_level) -
                                      G_UpgradeEffectValue(upgrade, i, old_level);
            changed = true;
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

BOOL G_ProducerCanUpgrade(LPEDICT producer, DWORD unit_id) {
    return producer && producer->data.UnitProfile &&
        G_UnitIsBuilding(producer->class_id) && G_UnitIsBuilding(unit_id) &&
        G_ProducerContains(producer->data.UnitProfile->upgrade, unit_id);
}

BOOL G_BuildingUpgradeActive(LPCEDICT building) {
    return building && building->inuse && !building->training &&
        building->research.upgrade != 0 &&
        G_UnitIsBuilding(building->class_id) &&
        G_UnitIsBuilding(building->research.upgrade);
}

static BOOL G_RelativeBuildingUpgradeCosts(void) {
    LPCSTR value = Stb_IniCacheFind(&game.config.misc, "Misc", "RelativeUpgradeCost");
    /* Warsmash treats zero as the relative-cost mode. Missing Misc data follows
     * the same default so stock total unit costs do not get charged twice. */
    return !value || !*value || atoi(value) == 0;
}

void G_GetBuildingUpgradeCosts(LPCEDICT building, DWORD unit_id,
                               LONG *gold, LONG *lumber, LONG *food) {
    UnitBalance_t const *from = building ? building->data.UnitBalance : NULL;
    UnitBalance_t const *to = G_UnitBalance(unit_id);
    LONG relative_gold = to ? MAX(0, to->goldCost) : 0;
    LONG relative_lumber = to ? MAX(0, to->lumberCost) : 0;
    LONG food_delta = to ? MAX(0, to->foodUsed) : 0;

    if (from) {
        if (G_RelativeBuildingUpgradeCosts()) {
            relative_gold -= MAX(0, from->goldCost);
            relative_lumber -= MAX(0, from->lumberCost);
        }
        food_delta -= MAX(0, from->foodUsed);
    }
    if (gold) *gold = MAX(0, relative_gold);
    if (lumber) *lumber = MAX(0, relative_lumber);
    if (food) *food = MAX(0, food_delta);
}

static LONG G_RequirementAmount(LPCSTR amounts, DWORD index) {
    char amount[32];
    LONG value = 1;

    if (!amounts || !G_CsvToken(amounts, index, amount, sizeof(amount))) return 1;
    if (sscanf(amount, "%d", &value) != 1) {
        fprintf(stderr, "G_RequirementAmount: invalid Requiresamount token '%s' at index %u\n",
                amount, (unsigned)index);
        return 1;
    }
    return MAX(1, value);
}

/* Count the owner's completed real heroes for tiered WC3 requirements. Dead
 * heroes still occupy a hero tier; queued training entities and illusions do not. */
static DWORD G_PlayerHeroCount(LPGAMECLIENT client) {
    DWORD count = 0;

    if (!client) return 0;
    FILTER_EDICTS(ent, ent->inuse && ent->data.UnitBalance && ent->s.player == client->ps.number &&
                         !ent->training && G_UnitIsHero(ent) && !(ent->aiflags & AI_ILLUSION)) count++;
    return count;
}

static BOOL G_UnitTypeIsHero(DWORD type_id) {
    UnitBalance_t const *balance = G_UnitBalance(type_id);
    return balance && (balance->strength > 0 || balance->agility > 0 || balance->intelligence > 0);
}

static BOOL G_UnitTypeSatisfiesRequirement_r(DWORD type_id, DWORD requirement_id,
                                               DWORD *visited, DWORD visited_count) {
    UnitProfile_t const *profile;
    char token[64];

    if (!type_id || !requirement_id) return false;
    if (type_id == requirement_id) return true;
    if (visited_count >= 32) return false;
    for (DWORD i = 0; i < visited_count; i++) {
        if (visited[i] == requirement_id) return false;
    }
    visited[visited_count++] = requirement_id;

    profile = G_UnitProfile(requirement_id);
    if (!profile || !profile->upgrade || !*profile->upgrade) return false;
    for (DWORD i = 0; G_CsvToken(profile->upgrade, i, token, sizeof(token)); i++) {
        DWORD upgrade_id;

        if (strlen(token) != 4) continue;
        memcpy(&upgrade_id, token, sizeof(upgrade_id));
        if (G_UnitTypeSatisfiesRequirement_r(type_id, upgrade_id, visited, visited_count)) return true;
    }
    return false;
}

/* Warcraft prerequisite checks treat an in-place upgraded structure as also
 * satisfying requirements on its predecessor. For example, a Keep satisfies
 * a Town Hall requirement, and a Castle satisfies both Keep and Town Hall.
 * Follow UnitProfile.Upgrade (uupt) transitively rather than teaching each
 * production command about race-specific town-hall tiers. */
static BOOL G_UnitTypeSatisfiesRequirement(DWORD type_id, DWORD requirement_id) {
    DWORD visited[32];
    return G_UnitTypeSatisfiesRequirement_r(type_id, requirement_id, visited, 0);
}

static LONG G_PlayerRequirementCount(LPGAMECLIENT client, DWORD techid) {
    LONG count = G_GetPlayerTechResearchedLevel(client, techid);
    DWORD player;

    if (!client || !techid) return 0;
    player = client->ps.number;
    FILTER_EDICTS(ent, ent->inuse && ent->s.player == player &&
                         !(ent->svflags & SVF_DEADMONSTER) && !ent->construction.active && !ent->training &&
                         G_UnitTypeSatisfiesRequirement(ent->class_id, techid)) {
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
    LPCSTR amounts = G_UpgradeLevelField(upgrade_id, "Requiresamount", level_value);
    return G_RequirementAmount(amounts, index);
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

static BOOL G_RequirementsListSatisfied(LPGAMECLIENT client, DWORD type_id, LPCSTR requirements,
                                        LPCSTR amounts, LPSTR reason, DWORD reason_size) {
    char requirement[64];

    for (DWORD i = 0; G_CsvToken(requirements, i, requirement, sizeof(requirement)); i++) {
        DWORD rawcode;
        LONG required;
        if (strlen(requirement) != 4) {
            fprintf(stderr, "G_RequirementsSatisfied: unsupported requirement token '%s' for 0x%08x\n",
                    requirement, (unsigned)type_id);
            continue;
        }
        memcpy(&rawcode, requirement, sizeof(rawcode));
        required = G_RequirementAmount(amounts, i);
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

static BOOL G_RequirementsSatisfied(LPGAMECLIENT client, DWORD type_id, LPSTR reason, DWORD reason_size) {
    UnitProfile_t const *profile = G_UnitProfile(type_id);
    DWORD hero_count, tier_count, tier;

    if (!G_RequirementsListSatisfied(client, type_id, profile->requires, profile->requiresAmount,
                                     reason, reason_size)) return false;
    if (!G_UnitTypeIsHero(type_id) || !profile->requiresCount) return true;
    hero_count = G_PlayerHeroCount(client);
    tier_count = MIN(sizeof(profile->requiresLevel) / sizeof(profile->requiresLevel[0]),
                     (DWORD)MAX(0, atoi(profile->requiresCount)));
    if (!hero_count || hero_count > tier_count) return true;
    tier = hero_count;
    if (!profile->requiresLevel[tier - 1] || !*profile->requiresLevel[tier - 1]) return true;
    return G_RequirementsListSatisfied(client, type_id, profile->requiresLevel[tier - 1], NULL,
                                       reason, reason_size);
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

buildCommandState_t G_GetBuildingUpgradeCommandState(LPGAMECLIENT client, LPEDICT producer, DWORD unit_id,
                                                     LPSTR reason, DWORD reason_size) {
    LONG maximum;
    LONG gold, lumber, food;
    UnitBalance_t const *target;

    if (reason && reason_size) reason[0] = '\0';
    if (!client || !G_ProducerCanUpgrade(producer, unit_id)) return BUILD_COMMAND_ABSENT;
    target = G_UnitBalance(unit_id);
    if (!target || target->id != unit_id || !G_UnitUI(unit_id)->modelFile) return BUILD_COMMAND_ABSENT;
    if (G_BuildingUpgradeActive(producer) || producer->construction.active || producer->training || producer->build) {
        return BUILD_COMMAND_DISABLED;
    }

    if (!G_BuildAllEnabled()) {
        maximum = G_GetPlayerTechMaxAllowed(client, unit_id);
        if (maximum >= 0 &&
            G_GetPlayerTechCountValue(client, unit_id) + G_GetPlayerTechInProgress(client, unit_id) >= maximum) {
            return BUILD_COMMAND_HIDDEN;
        }
        if (!G_RequirementsSatisfied(client, unit_id, reason, reason_size)) {
            return BUILD_COMMAND_DISABLED;
        }
    }

    G_GetBuildingUpgradeCosts(producer, unit_id, &gold, &lumber, &food);
    if (gold > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD]) {
        if (reason && reason_size) snprintf(reason, reason_size, "Not enough gold");
        return BUILD_COMMAND_UNAFFORDABLE;
    }
    if (lumber > (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER]) {
        if (reason && reason_size) snprintf(reason, reason_size, "Not enough lumber");
        return BUILD_COMMAND_UNAFFORDABLE;
    }
    if (!G_PlayerHasFoodFor(client, food)) {
        if (reason && reason_size) snprintf(reason, reason_size, "Not enough food");
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

static void G_RefreshBuildingUpgradeUI(LPEDICT building) {
    LPGAMECLIENT client;
    LPEDICT clent;

    if (!building) return;
    client = G_GetPlayerClientByNumber(building->s.player);
    if (!client || client->ps.number != building->s.player) return;
    G_InvalidateCommands(client);
    clent = G_GetPlayerEntityByNumber(building->s.player);
    if (!clent || !client->connected) return;
    G_RefreshResourceBar(clent);
    Get_Commands_f(clent);
    Get_Portrait_f(clent);
}

void G_UpdateBuildingUpgradeAnimation(LPEDICT building) {
    LPCANIMATION anim;
    FLOAT fraction;
    DWORD first, last, span, frame;

    if (!G_BuildingUpgradeActive(building) || building->research.duration <= 0.0f) return;
    anim = building->animation;
    if (!G_AnimationHasPrimary(anim, "birth")) anim = G_GetUnitAnimation(building, "birth");
    if (!anim || anim->interval[1] <= anim->interval[0]) return;
    building->animation = anim;

    fraction = MAX(0.0f, MIN(1.0f, building->research.progress / building->research.duration));
    first = anim->interval[0];
    last = anim->interval[1];
    span = last - first;
    frame = first + (DWORD)((FLOAT)span * fraction);
    if (frame >= last) frame = last - 1;
    building->s.frame = frame;
}

BOOL G_StartBuildingUpgrade(LPEDICT building, DWORD unit_id) {
    LPGAMECLIENT client;
    LPEDICT clent;
    buildCommandState_t state;
    UnitBalance_t const *target;
    LONG gold, lumber, food;
    char reason[128];

    if (!building || !unit_id) return false;
    client = G_GetPlayerClientByNumber(building->s.player);
    if (!client || client->ps.number != building->s.player) return false;
    clent = G_GetPlayerEntityByNumber(building->s.player);
    state = G_GetBuildingUpgradeCommandState(client, building, unit_id, reason, sizeof(reason));
    if (state != BUILD_COMMAND_AVAILABLE) {
        if (clent && client->connected && reason[0]) G_ShowCommandErrorText(clent, reason);
        return false;
    }

    target = G_UnitBalance(unit_id);
    G_GetBuildingUpgradeCosts(building, unit_id, &gold, &lumber, &food);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] -= gold;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] -= lumber;

    /* Upgrade construction is owned by the existing edict. Reuse the otherwise
     * producer-local research scalars for target/cost/timing state; queued
     * UpgradeData research continues to live on hidden training edicts. */
    G_ClearUnitOrderQueue(building);
    if (building->stand) building->stand(building);
    memset(&building->research, 0, sizeof(building->research));
    building->research.upgrade = unit_id;
    building->research.gold = gold;
    building->research.lumber = lumber;
    building->research.duration = (FLOAT)MAX(0, target->buildTime);
    building->research.progress = 0.0f;
    G_SetUnitFoodUsed(building, MAX(0, target->foodUsed));
    G_AddPlayerTechInProgress(client, unit_id, 1);
    building->aiflags |= AI_HOLD_FRAME;
    G_UpdateBuildingUpgradeAnimation(building);
    G_PublishEvent(building, EVENT_PLAYER_UNIT_UPGRADE_START);
    G_PublishEvent(building, EVENT_UNIT_UPGRADE_START);
    G_RefreshBuildingUpgradeUI(building);
    return true;
}

void G_StopBuildingUpgrade(LPEDICT building, BOOL refund) {
    LPGAMECLIENT client;
    DWORD unit_id;

    if (!G_BuildingUpgradeActive(building)) return;
    unit_id = building->research.upgrade;
    client = G_GetPlayerClientByNumber(building->s.player);
    if (client && client->ps.number == building->s.player) {
        if (refund) {
            LONG gold = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] +
                        MAX(0, building->research.gold);
            LONG lumber = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] +
                          MAX(0, building->research.lumber);
            client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = (USHORT)MIN(gold, USHRT_MAX);
            client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = (USHORT)MIN(lumber, USHRT_MAX);
        }
        G_AddPlayerTechInProgress(client, unit_id, -1);
    }
    G_SetUnitFoodUsed(building, building->data.UnitBalance ? MAX(0, building->data.UnitBalance->foodUsed) : 0);
    memset(&building->research, 0, sizeof(building->research));
    if (!building->construction.active && !(building->svflags & SVF_DEADMONSTER) && !M_IsDead(building)) {
        building->aiflags &= ~AI_HOLD_FRAME;
        if (building->stand) building->stand(building);
    }
    G_RefreshBuildingUpgradeUI(building);
}

BOOL G_CancelBuildingUpgrade(LPEDICT building) {
    if (!G_BuildingUpgradeActive(building)) return false;
    G_PublishEvent(building, EVENT_PLAYER_UNIT_UPGRADE_CANCEL);
    G_PublishEvent(building, EVENT_UNIT_UPGRADE_CANCEL);
    G_StopBuildingUpgrade(building, true);
    return true;
}

static BOOL G_CompleteBuildingUpgrade(LPEDICT building) {
    LPGAMECLIENT client;
    DWORD unit_id;
    LONG charged_gold, charged_lumber;

    if (!G_BuildingUpgradeActive(building)) return false;
    unit_id = building->research.upgrade;
    charged_gold = MAX(0, building->research.gold);
    charged_lumber = MAX(0, building->research.lumber);
    client = G_GetPlayerClientByNumber(building->s.player);
    if (client && client->ps.number == building->s.player)
        G_AddPlayerTechInProgress(client, unit_id, -1);

    /* Clear the transient state before type rebinding so the new unit profile
     * owns the command card immediately and the transform's food refresh uses
     * only the completed target type. */
    memset(&building->research, 0, sizeof(building->research));
    building->aiflags &= ~AI_HOLD_FRAME;
    if (!G_TransformUnitType(building, unit_id)) {
        /* Command acceptance validates the target, so this is defensive. Do
         * not strand resources/food if map data becomes invalid mid-upgrade. */
        if (client && client->ps.number == building->s.player) {
            LONG gold = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] + charged_gold;
            LONG lumber = (LONG)client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] + charged_lumber;
            client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = (USHORT)MIN(gold, USHRT_MAX);
            client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = (USHORT)MIN(lumber, USHRT_MAX);
        }
        G_SetUnitFoodUsed(building, building->data.UnitBalance ? MAX(0, building->data.UnitBalance->foodUsed) : 0);
        if (building->stand) building->stand(building);
        G_RefreshBuildingUpgradeUI(building);
        return false;
    }
    building->aiflags &= ~AI_HOLD_FRAME;
    if (building->stand) building->stand(building);
    G_PublishEvent(building, EVENT_PLAYER_UNIT_UPGRADE_FINISH);
    G_PublishEvent(building, EVENT_UNIT_UPGRADE_FINISH);
    G_RefreshBuildingUpgradeUI(building);
    return true;
}

void G_RunBuildingUpgradeFrame(LPEDICT building) {
    if (!G_BuildingUpgradeActive(building)) return;
    if (M_IsDead(building) || (building->svflags & SVF_DEADMONSTER)) {
        G_StopBuildingUpgrade(building, false);
        return;
    }
    if (building->paused) return;
    if (building->research.duration <= 0.0f || G_PlayerInstantBuild(building->s.player)) {
        G_CompleteBuildingUpgrade(building);
        return;
    }
    building->research.progress += (FLOAT)FRAMETIME / 1000.0f;
    G_UpdateBuildingUpgradeAnimation(building);
    if (building->research.progress >= building->research.duration)
        G_CompleteBuildingUpgrade(building);
}

void G_GetBuildPlacementPathingFlags(DWORD building_id, LPBYTE prevented, LPBYTE required) {
    UnitBalance_t const *balance = G_UnitBalance(building_id);
    UnitUI_t const *ui = G_UnitUI(building_id);
    LPCSTR prevent = balance->preventPlace ? balance->preventPlace : ui->preventPlace;
    LPCSTR require = balance->requirePlace ? balance->requirePlace : ui->requirePlace;

    if (prevented) {
        *prevented = WC3_PATH_UNBUILDABLE | WC3_PATH_UNWALKABLE | G_PlacementFlags(prevent);
    }
    if (required) {
        *required = G_PlacementFlags(require);
    }
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

BOOL G_FindBuildOnTarget(DWORD building_id, LPCVECTOR2 point, LPEDICT *out) {
    UnitData_t const *data = G_UnitData(building_id);
    if (out) *out = NULL;
    if (!data->isBuildOn) return true;
    FILTER_EDICTS(ent, ent->inuse && G_UnitIsBuilding(ent->class_id) && ent->data.UnitData->canBuildOn) {
        /* Build-on targets may be off the placement lattice; accept the whole
         * snap cell so clicking a mine does not fail after the ghost moves. */
        if (fabsf(ent->s.origin2.x - point->x) <= WC3_BUILD_GRID_SIZE &&
            fabsf(ent->s.origin2.y - point->y) <= WC3_BUILD_GRID_SIZE) {
            if (out) *out = ent;
#ifdef WC3_DEBUG_MINING
            fprintf(stderr, "WC3_MINING build-target building=%.4s parent=%ld id=%.4s parent=(%.1f,%.1f) point=(%.1f,%.1f)\n",
                    (LPCSTR)&building_id, (long)(ent - globals.edicts), (LPCSTR)&ent->class_id,
                    ent->s.origin2.x, ent->s.origin2.y, point->x, point->y);
#endif
            return true;
        }
    }
#ifdef WC3_DEBUG_MINING
    fprintf(stderr, "WC3_MINING build-target-missing building=%.4s point=(%.1f,%.1f)\n",
            (LPCSTR)&building_id, point ? point->x : 0.0f, point ? point->y : 0.0f);
#endif
    return false;
}

static BOOL G_BuildUnitCanDisplace(LPEDICT builder, LPEDICT ent) {
    return builder && ent && ent->s.player == builder->s.player && !G_UnitIsBuilding(ent->class_id) &&
           ent->movetype != MOVETYPE_NONE && ent->collision > 0.0f;
}

static BOOL G_LiveUnitBlocksBuild(LPEDICT builder, LPEDICT build_on, LPCBOX2 footprint) {
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) && !(ent->svflags & SVF_DEADMONSTER)) {
        FLOAT x, y;
        /* Friendly mobile units can be displaced when construction starts; everything else is a hard blocker. */
        if (ent == builder || ent == build_on || ent->collision <= 0.0f) continue;
        x = MAX(footprint->min.x, MIN(footprint->max.x, ent->s.origin2.x));
        y = MAX(footprint->min.y, MIN(footprint->max.y, ent->s.origin2.y));
        VECTOR2 nearest = { x, y };
        if (Vector2_distance(&nearest, &ent->s.origin2) < ent->collision && !G_BuildUnitCanDisplace(builder, ent)) return true;
    }
    return false;
}

/* Move friendly mobile units clear of a newly baked footprint while retaining their active orders. */
BOOL G_DisplaceBuildOccupants(LPEDICT builder, LPEDICT building) {
    LPEDICT *units;
    VECTOR2 *positions;
    FLOAT *angles;
    DWORD count = 0;

    if (!builder || !building || !globals.num_edicts) return false;
    units = gi.MemAlloc(sizeof(*units) * globals.num_edicts);
    positions = gi.MemAlloc(sizeof(*positions) * globals.num_edicts);
    angles = gi.MemAlloc(sizeof(*angles) * globals.num_edicts);
    if (!units || !positions || !angles) {
        fprintf(stderr, "WC3: unable to allocate construction occupant displacement buffers\n");
        if (angles) gi.MemFree(angles);
        if (positions) gi.MemFree(positions);
        if (units) gi.MemFree(units);
        return false;
    }
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) && !(ent->svflags & SVF_DEADMONSTER) &&
                  G_BuildUnitCanDisplace(builder, ent) && ent != builder &&
                  CM_DistanceToPathingFootprint(building, &ent->s.origin2) < ent->collision) {
        if (!SP_FindUnitExitPosition(building, ent, &positions[count], &angles[count])) {
            gi.MemFree(angles); gi.MemFree(positions); gi.MemFree(units); return false;
        }
        units[count++] = ent;
    }
    FOR_LOOP(i, count) {
        FOR_LOOP(j, i) {
            if (Vector2_distance(&positions[i], &positions[j]) < units[i]->collision + units[j]->collision) {
                gi.MemFree(angles); gi.MemFree(positions); gi.MemFree(units); return false;
            }
        }
    }
    FOR_LOOP(i, count) {
        units[i]->s.origin2 = positions[i];
        units[i]->s.origin.x = positions[i].x; units[i]->s.origin.y = positions[i].y;
        units[i]->s.origin.z = CM_GetHeightAtPoint(positions[i].x, positions[i].y);
        units[i]->s.angle = angles[i];
        gi.LinkEntity(units[i]);
        move_reset_progress(units[i]);
    }
    gi.MemFree(angles); gi.MemFree(positions); gi.MemFree(units);
    return true;
}

buildPlacementResult_t G_EvaluateBuildPlacement(LPEDICT builder, DWORD building_id, LPCVECTOR2 requested,
                                                LPVECTOR2 snapped) {
    UnitData_t const *data = G_UnitData(building_id);
    BYTE prevented = 0;
    BYTE required = 0;
    pathTex_t *pathtex = NULL;
    LPEDICT build_on = NULL;
    DWORD width = 1, height = 1;
    BOX2 footprint;
    VECTOR2 point;

    if (!requested || !G_UnitIsBuilding(building_id)) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING placement result=%d reason=invalid-building building=%.4s\n",
                PLACE_INVALID_BUILDING, (LPCSTR)&building_id);
#endif
        return PLACE_INVALID_BUILDING;
    }
    G_GetBuildPlacementPathingFlags(building_id, &prevented, &required);
    point = *requested;
    G_SnapBuildingPoint(building_id, &point);

    if (!G_FindBuildOnTarget(building_id, &point, &build_on)) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING placement result=%d reason=parent-missing building=%.4s requested=(%.1f,%.1f) snapped=(%.1f,%.1f)\n",
                PLACE_REQUIRED_PARENT_MISSING, (LPCSTR)&building_id, requested->x, requested->y, point.x, point.y);
#endif
        return PLACE_REQUIRED_PARENT_MISSING;
    }
    /* Build-on structures inherit the parent's authored center; the grid is
     * only for cursor placement and must not offset the mine overlay. */
    if (build_on) point = build_on->s.origin2;
    if (snapped) *snapped = point;
    pathtex = M_LoadPathTex(data->pathingTexture);
    if (data->pathingTexture && strlen(data->pathingTexture) > 1 && !pathtex) {
 #ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING placement result=%d reason=pathing-texture building=%.4s point=(%.1f,%.1f)\n",
                PLACE_INVALID_BUILDING, (LPCSTR)&building_id, point.x, point.y);
 #endif
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
 #ifdef WC3_DEBUG_MINING
                    fprintf(stderr, "WC3_MINING placement result=%d reason=out-of-bounds building=%.4s sample=(%.1f,%.1f)\n",
                            PLACE_OUT_OF_BOUNDS, (LPCSTR)&building_id, sample.x, sample.y);
 #endif
                    return PLACE_OUT_OF_BOUNDS;
                }
                if (flags & prevented) {
                    if (pathtex) gi.MemFree(pathtex);
 #ifdef WC3_DEBUG_MINING
                    fprintf(stderr, "WC3_MINING placement result=%d reason=terrain-blocked building=%.4s sample=(%.1f,%.1f) flags=0x%x prevented=0x%x\n",
                            PLACE_TERRAIN_BLOCKED, (LPCSTR)&building_id, sample.x, sample.y, flags, prevented);
 #endif
                    return PLACE_TERRAIN_BLOCKED;
                }
                if ((flags & required) != required) {
                    if (pathtex) gi.MemFree(pathtex);
 #ifdef WC3_DEBUG_MINING
                    fprintf(stderr, "WC3_MINING placement result=%d reason=required-pathing building=%.4s sample=(%.1f,%.1f) flags=0x%x required=0x%x\n",
                            PLACE_REQUIRED_PATHING_MISSING, (LPCSTR)&building_id, sample.x, sample.y, flags, required);
 #endif
                    return PLACE_REQUIRED_PATHING_MISSING;
                }
            }
        }
    }
    if (pathtex) gi.MemFree(pathtex);
    if (G_LiveUnitBlocksBuild(builder, build_on, &footprint)) {
#ifdef WC3_DEBUG_MINING
        fprintf(stderr, "WC3_MINING placement result=%d reason=unit-blocked building=%.4s point=(%.1f,%.1f) parent=%ld\n",
                PLACE_UNIT_BLOCKED, (LPCSTR)&building_id, point.x, point.y,
                build_on ? (long)(build_on - globals.edicts) : -1L);
#endif
        return PLACE_UNIT_BLOCKED;
    }
#ifdef WC3_DEBUG_MINING
    fprintf(stderr, "WC3_MINING placement result=%d reason=ok building=%.4s point=(%.1f,%.1f) parent=%ld\n",
            PLACE_OK, (LPCSTR)&building_id, point.x, point.y,
            build_on ? (long)(build_on - globals.edicts) : -1L);
#endif
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
    if (!G_AnimationHasPrimary(anim, "birth"))
        anim = G_GetUnitAnimation(building, "birth");
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

static BOOL G_ConstructionHasClassification(LPCEDICT unit, LPCSTR wanted) {
    LPCSTR list;
    UnitBalance_t const *balance;
    UnitData_t const *data;

    if (!unit || !wanted || !*wanted) return false;
    balance = unit->data.UnitBalance;
    data = unit->data.UnitData;
    list = balance ? balance->type : NULL;
    if (!list || !*list) list = data ? data->unitClassification : NULL;
    if (!list || !*list) return false;

    PARSE_LIST(list, item, parse_segment) {
        if (!strcasecmp(item, wanted)) return true;
    }
    return false;
}

static BOOL G_StartConstruction(LPEDICT building, constructionType_t type, BOOL paused) {
    edictStat_s *hp;

    if (!building || !G_UnitIsBuilding(building->class_id)) return false;
    hp = &building->health;
    building->construction.active = true;
    building->construction.paused = paused;
    building->construction.type = type;
    building->construction.primary_builder = NULL;
    building->construction.worker = NULL;
    building->construction.worker_spawn_time = 0;
    building->construction.worker_inside = false;
    building->construction.consumes_worker = false;
    building->construction.restore_invulnerable = false;
    building->construction.restore_paused = false;
    building->construction.restore_hidden = false;
    building->construction.worker_release_time = 0;
    building->construction.progress = 0.0f;
    building->construction.paid = false;
    building->construction.payer = 0;
    building->construction.gold = 0;
    building->construction.lumber = 0;
    building->aiflags |= AI_HOLD_FRAME;
    G_SetHealth(building, MAX(1.0f, hp->max_value * WC3_BUILD_START_LIFE));

    G_UpdateConstructionAnimation(building);
    return true;
}

static void G_AssignConstructionWorker(LPEDICT building, LPEDICT worker, BOOL inside) {
    if (!building || !worker) return;
    building->construction.worker = worker;
    building->construction.worker_spawn_time = worker->spawn_time;
    building->construction.worker_inside = inside;
    building->construction.restore_invulnerable = worker->invulnerable;
    building->construction.restore_paused = worker->paused;
    building->construction.restore_hidden = worker->s.renderfx & RF_HIDDEN;
    worker->build = building;
    worker->goalentity = building;
    if (!inside) return;

    worker->s.renderfx |= RF_HIDDEN;
    worker->paused = true;
    worker->invulnerable = true;
    G_InvalidateUnitShortcutsForUnit(worker);
}

BOOL G_StartHumanConstruction(LPEDICT builder, LPEDICT building) {
    if (!builder || !G_StartConstruction(building, CONSTRUCTION_HUMAN, true)) return false;
    building->construction.primary_builder = builder;
    return true;
}

BOOL G_StartOrcConstruction(LPEDICT builder, LPEDICT building) {
    if (!builder || !G_StartConstruction(building, CONSTRUCTION_ORC, false)) return false;
    G_AssignConstructionWorker(building, builder, true);
    return true;
}

BOOL G_StartUndeadConstruction(LPEDICT builder, LPEDICT building) {
    if (!builder || !G_StartConstruction(building, CONSTRUCTION_UNDEAD, false)) return false;
    G_AssignConstructionWorker(building, builder, false);
    building->construction.worker_release_time = G_Time() + WC3_UNDEAD_BUILD_WORK_MS;
    return true;
}

BOOL G_StartNightElfConstruction(LPEDICT builder, LPEDICT building) {
    if (!builder || !G_StartConstruction(building, CONSTRUCTION_NIGHTELF, false)) return false;
    G_AssignConstructionWorker(building, builder, true);
    if (G_ConstructionHasClassification(building, "ancient")) {
        building->construction.consumes_worker = true;
        /* Warsmash removes the Wisp's food contribution as soon as it becomes
         * part of an Ancient. Cancellation restores the worker and its food. */
        G_SetUnitFoodUsed(builder, 0);
    }
    return true;
}

/* Entangle Gold Mine creates a Night Elf building without consuming/owning a
 * Wisp. It still uses the same authoritative autonomous construction clock. */
BOOL G_StartNightElfOverlayConstruction(LPEDICT building) {
    return G_StartConstruction(building, CONSTRUCTION_NIGHTELF, false);
}

static LPEDICT G_ConstructionWorker(LPEDICT building) {
    LPEDICT worker;

    if (!building || !(worker = building->construction.worker)) return NULL;
    if (!worker->inuse || worker->spawn_time != building->construction.worker_spawn_time) {
        building->construction.worker = NULL;
        building->construction.worker_spawn_time = 0;
        building->construction.worker_inside = false;
        building->construction.worker_release_time = 0;
        return NULL;
    }
    return worker;
}

static void G_ReleaseConstructionWorker(LPEDICT building, BOOL completed) {
    LPEDICT worker;
    BOOL consumes, inside;

    if (!building) return;
    worker = G_ConstructionWorker(building);
    consumes = building->construction.consumes_worker;
    inside = building->construction.worker_inside;
    building->construction.worker = NULL;
    building->construction.worker_spawn_time = 0;
    building->construction.worker_inside = false;
    building->construction.worker_release_time = 0;
    if (!worker) return;

    if (completed && consumes) {
        /* The Wisp was already removed from Food Used at construction start. */
        G_FreeEdict(worker);
        return;
    }

    worker->paused = building->construction.restore_paused;
    worker->invulnerable = building->construction.restore_invulnerable;
    if (building->construction.restore_hidden) worker->s.renderfx |= RF_HIDDEN;
    else worker->s.renderfx &= ~RF_HIDDEN;
    G_InvalidateUnitShortcutsForUnit(worker);
    if (consumes && worker->data.UnitBalance)
        G_SetUnitFoodUsed(worker, worker->data.UnitBalance->foodUsed);

    if (inside) {
        VECTOR2 origin;
        FLOAT angle;
        if (SP_FindUnitExitPosition(building, worker, &origin, &angle)) {
            worker->s.origin2 = origin;
            worker->s.angle = angle - M_PI;
        }
    }
    gi.LinkEntity(worker);
    worker->build = NULL;
    if (worker->goalentity == building) worker->goalentity = NULL;
    if (worker->stand) worker->stand(worker);
}

void G_RunConstructionFrame(LPEDICT building) {
    FLOAT duration, hp_gain;
    edictStat_s *hp;

    if (!building || !building->construction.active || building->construction.paused ||
        building->paused || !building->data.UnitBalance) return;
    if (building->construction.type != CONSTRUCTION_ORC &&
        building->construction.type != CONSTRUCTION_UNDEAD &&
        building->construction.type != CONSTRUCTION_NIGHTELF) return;

    if (building->construction.type == CONSTRUCTION_UNDEAD &&
        building->construction.worker_release_time &&
        G_Time() >= building->construction.worker_release_time) {
        G_ReleaseConstructionWorker(building, false);
    }

    duration = MAX(1.0f, (FLOAT)building->data.UnitBalance->buildTime * 1000.0f);
    hp = &building->health;
    building->construction.progress += (FLOAT)FRAMETIME;
    hp_gain = (hp->max_value - MAX(1.0f, hp->max_value * WC3_BUILD_START_LIFE)) *
              ((FLOAT)FRAMETIME / duration);
    hp->value = MIN(hp->max_value, hp->value + MAX(0.0f, hp_gain));
    G_UpdateConstructionAnimation(building);
    if (building->construction.progress >= duration) G_CompleteConstruction(building);
}

/* Construction teardown releases Human Repair participants and any race-owned
 * worker before the target enters death/completion cleanup; otherwise workers
 * retain pointers to an entity whose construction state no longer exists. */
void G_StopConstruction(LPEDICT building) {
    if (!building || !building->construction.active) return;

    FILTER_EDICTS(worker, worker->inuse && worker != building && worker->build == building &&
                           worker->buildwork.ability) {
        S_CancelRepair(worker);
        if (worker->stand) worker->stand(worker);
    }

    G_ReleaseConstructionWorker(building, false);

    /* The construction info panel historically used a self-linked build queue.
     * Clear it before unit_die() walks production/revival ownership. */
    if (building->build == building) building->build = NULL;
    building->construction.active = false;
    building->construction.paused = false;
    building->construction.type = CONSTRUCTION_NONE;
    building->construction.primary_builder = NULL;
    building->construction.worker = NULL;
    building->construction.worker_spawn_time = 0;
    building->construction.worker_inside = false;
    building->construction.consumes_worker = false;
    building->construction.restore_invulnerable = false;
    building->construction.restore_paused = false;
    building->construction.restore_hidden = false;
    building->construction.worker_release_time = 0;
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
    BOOL legacy;

    if (!building) return;
    /* Human construction has explicit construction state.  The still-legacy
     * Orc/Night Elf/Undead build path marks an in-progress structure by
     * self-linking building->build.  Both lifecycles must converge here so
     * completion grants supply and publishes CONSTRUCT_FINISH exactly once. */
    legacy = building->build == building;
    if (!building->construction.active && !legacy) return;
    if (WC3_TUTORIAL_DEBUG_ENABLED()) {
        fprintf(stderr,
                "WC3_QUEST_BUILD complete-enter building=%ld id=%.4s player=%u legacy=%d active=%d primary_builder=%ld build_link=%ld health=%.1f/%.1f\n",
                (long)(building - globals.edicts), (LPCSTR)&building->class_id,
                (unsigned)building->s.player, legacy, (int)building->construction.active,
                building->construction.primary_builder
                    ? (long)(building->construction.primary_builder - globals.edicts) : -1L,
                building->build ? (long)(building->build - globals.edicts) : -1L,
                building->health.value, building->health.max_value);
    }
    client = G_GetPlayerClientByNumber(building->s.player);
    if (client && client->ps.number != building->s.player) client = NULL;
    G_ReleaseConstructionWorker(building, true);
    building->construction.active = false;
    building->construction.paused = false;
    building->construction.type = CONSTRUCTION_NONE;
    building->construction.primary_builder = NULL;
    building->construction.worker = NULL;
    building->construction.worker_spawn_time = 0;
    building->construction.worker_inside = false;
    building->construction.consumes_worker = false;
    building->construction.restore_invulnerable = false;
    building->construction.restore_paused = false;
    building->construction.restore_hidden = false;
    building->construction.worker_release_time = 0;
    building->construction.progress = 0.0f;
    building->construction.paid = false;
    building->construction.payer = 0;
    building->construction.gold = 0;
    building->construction.lumber = 0;
    building->aiflags &= ~AI_HOLD_FRAME;
    if (building->build == building) building->build = NULL;
    G_SetHealth(building, building->health.max_value);
    building->stand(building);
#ifdef WC3_DEBUG_AI
    fprintf(stderr, "WC3_DEBUG_AI construction complete building=%ld id=%.4s player=%u\n",
        (long)(building - g_edicts), (LPCSTR)&building->class_id, building->s.player);
#endif
    G_SetUnitFoodMade(building, building->data.UnitBalance->foodMade);
    G_QueueOwnerUISound(building, "JobDoneSound");
    G_SendOwnerMinimapAlert(building);
    G_PublishEvent(building, EVENT_PLAYER_UNIT_CONSTRUCT_FINISH);
    G_PublishEvent(building, EVENT_UNIT_CONSTRUCT_FINISH);
    if (WC3_TUTORIAL_DEBUG_ENABLED()) {
        fprintf(stderr,
                "WC3_QUEST_BUILD complete-publish building=%ld id=%.4s player=%u event=%u build_link=%ld food_made=%d\n",
                (long)(building - globals.edicts), (LPCSTR)&building->class_id,
                (unsigned)building->s.player, (unsigned)EVENT_PLAYER_UNIT_CONSTRUCT_FINISH,
                building->build ? (long)(building->build - globals.edicts) : -1L,
                building->food.made);
    }
    if (client) {
        LPEDICT clent = G_GetPlayerEntityByNumber(client->ps.number);
        G_InvalidateCommands(client);
        G_RefreshResourceBar(clent);
        Get_Portrait_f(clent);
    }
}
