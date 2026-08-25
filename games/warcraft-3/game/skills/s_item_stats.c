#include "s_skills.h"

/* Passive item stat bonuses — apply on pickup, reverse on drop.
 * Follows WarSmash pattern: CAbilityItemAttackBonus.onAdd/onRemove,
 * CAbilityItemDefenseBonus.onAdd/onRemove, etc. */

typedef struct {
    DWORD code;
    FLOAT bonus;
} itemstat_t;

/* Per-type bonus storage, populated by init functions. */
static FLOAT item_attack_bonus_val;
static FLOAT item_defense_bonus_val;
static FLOAT item_life_bonus_val;
static FLOAT item_mana_bonus_val;
static FLOAT item_stat_str_val;
static FLOAT item_stat_agi_val;
static FLOAT item_stat_int_val;

#define ID_ITEM_ATTACK   MAKEFOURCC('A', 'I', 'a', 't')
#define ID_ITEM_DEFENSE  MAKEFOURCC('A', 'I', 'd', 'e')
#define ID_ITEM_LIFE     MAKEFOURCC('A', 'I', 'm', 'l')
#define ID_ITEM_MANA_BONUS MAKEFOURCC('A', 'I', 'm', 'm')
#define ID_ITEM_STAT     MAKEFOURCC('A', 'I', 'a', 'b')

static void apply_attack(LPEDICT unit, FLOAT amount) {
    unit->attack1.damageBase += amount;
    unit->attack2.damageBase += amount;
}

static void apply_defense(LPEDICT unit, FLOAT amount) {
    unit->armor_value += amount;
}

static void apply_life(LPEDICT unit, FLOAT amount) {
    FLOAT old_max = unit->health.max_value;
    if (old_max <= 0) old_max = 1.0f;
    FLOAT ratio = unit->health.value / old_max;
    unit->health.max_value += amount;
    unit->health.value = unit->health.max_value * ratio;
}

static void apply_mana(LPEDICT unit, FLOAT amount) {
    FLOAT old_max = unit->mana.max_value;
    if (old_max <= 0) old_max = 1.0f;
    FLOAT ratio = unit->mana.value / old_max;
    unit->mana.max_value += amount;
    unit->mana.value = unit->mana.max_value * ratio;
}

static void apply_stat(LPEDICT unit, FLOAT str, FLOAT agi, FLOAT intel) {
    if (!G_UnitIsHero(unit)) {
        return;
    }
    unit->hero.str += (DWORD)str;
    unit->hero.agi += (DWORD)agi;
    unit->hero.intel += (DWORD)intel;
    G_RecomputeHeroStats(unit);
}

void item_stat_apply(LPEDICT unit, DWORD item_code) {
    if (!unit) {
        return;
    }
    if (item_code == ID_ITEM_ATTACK && item_attack_bonus_val != 0) {
        apply_attack(unit, item_attack_bonus_val);
    }
    if (item_code == ID_ITEM_DEFENSE && item_defense_bonus_val != 0) {
        apply_defense(unit, item_defense_bonus_val);
    }
    if (item_code == ID_ITEM_LIFE && item_life_bonus_val != 0) {
        apply_life(unit, item_life_bonus_val);
    }
    if (item_code == ID_ITEM_MANA_BONUS && item_mana_bonus_val != 0) {
        apply_mana(unit, item_mana_bonus_val);
    }
    if (item_code == ID_ITEM_STAT) {
        apply_stat(unit, item_stat_str_val, item_stat_agi_val, item_stat_int_val);
    }
}

void item_stat_remove(LPEDICT unit, DWORD item_code) {
    if (!unit) {
        return;
    }
    if (item_code == ID_ITEM_ATTACK && item_attack_bonus_val != 0) {
        apply_attack(unit, -item_attack_bonus_val);
    }
    if (item_code == ID_ITEM_DEFENSE && item_defense_bonus_val != 0) {
        apply_defense(unit, -item_defense_bonus_val);
    }
    if (item_code == ID_ITEM_LIFE && item_life_bonus_val != 0) {
        apply_life(unit, -item_life_bonus_val);
    }
    if (item_code == ID_ITEM_MANA_BONUS && item_mana_bonus_val != 0) {
        apply_mana(unit, -item_mana_bonus_val);
    }
    if (item_code == ID_ITEM_STAT) {
        apply_stat(unit, -item_stat_str_val, -item_stat_agi_val, -item_stat_int_val);
    }
}

/* Init functions — read bonus values from AbilityData.slk at startup. */

void SP_ability_item_attack_bonus(LPCSTR classname, ability_t *self) {
    item_attack_bonus_val = AB_Number(classname, "DataA1");
}

void SP_ability_item_defense_bonus(LPCSTR classname, ability_t *self) {
    item_defense_bonus_val = AB_Number(classname, "DataA1");
}

void SP_ability_item_life_bonus(LPCSTR classname, ability_t *self) {
    item_life_bonus_val = AB_Number(classname, "DataA1");
}

void SP_ability_item_mana_bonus(LPCSTR classname, ability_t *self) {
    item_mana_bonus_val = AB_Number(classname, "DataA1");
}

void SP_ability_item_stat_bonus(LPCSTR classname, ability_t *self) {
    item_stat_str_val = AB_Number(classname, "DataA1");
    item_stat_agi_val = AB_Number(classname, "DataB1");
    item_stat_int_val = AB_Number(classname, "DataC1");
}
