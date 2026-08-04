#include "s_skills.h"

#define ID_ITEM_HEAL           MAKEFOURCC('A', 'I', 'h', 'e')
#define ID_ITEM_MANA           MAKEFOURCC('A', 'I', 'm', 'a')
#define ID_ITEM_LIFE_GAIN      MAKEFOURCC('A', 'I', 'm', 'i')
#define ID_ITEM_PERM_STR       MAKEFOURCC('A', 'I', 's', 'm')
#define ID_ITEM_PERM_AGI       MAKEFOURCC('A', 'I', 'a', 'm')
#define ID_ITEM_PERM_INT       MAKEFOURCC('A', 'I', 'i', 'm')
#define ID_ITEM_PERM_MULTI     MAKEFOURCC('A', 'I', 'x', 'm')
#define ID_ITEM_XP_GAIN        MAKEFOURCC('A', 'I', 'e', 'm')
#define ID_ITEM_LEVEL_GAIN     MAKEFOURCC('A', 'I', 'l', 'm')
#define ID_ITEM_FIGURINE       MAKEFOURCC('A', 'I', 'f', 's')

/* ---- Active items (consume on use) -------------------------------------- */

static void item_heal_command(LPEDICT clent) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_HEAL);
    FLOAT amount = S_SpellData(code, 1, 1);

    if (!S_SpellIsAliveTarget(target)) {
        return;
    }
    S_SpellHeal(target, amount);
}

static void item_mana_command(LPEDICT clent) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_MANA);
    FLOAT amount = S_SpellData(code, 1, 1);

    if (!target || amount <= 0) {
        return;
    }
    target->mana.value = MIN(target->mana.max_value, target->mana.value + amount);
}

static void item_permanent_life_command(LPEDICT clent) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_LIFE_GAIN);
    FLOAT amount = S_SpellData(code, 1, 1);

    if (!target || amount <= 0) {
        return;
    }
    target->health.max_value += amount;
    target->health.value += amount;
}

/* WarSmash: CAbilityItemPermanentStatGain.checkBeforeQueue
 * Permanently adds to hero base stats, consumes the item. */
static void item_permanent_stat_command(LPEDICT clent) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    FLOAT str = S_SpellData(code, 1, 1);
    FLOAT agi = S_SpellData(code, 1, 2);
    FLOAT intel = S_SpellData(code, 1, 3);

    if (!target || !G_UnitIsHero(target)) {
        return;
    }
    target->hero.str += (DWORD)str;
    target->hero.agi += (DWORD)agi;
    target->hero.intel += (DWORD)intel;
    G_RecomputeHeroStats(target);
}

/* WarSmash: CAbilityItemExperienceGain — grants XP. */
static void item_experience_command(LPEDICT clent) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_XP_GAIN);
    DWORD amount = (DWORD)S_SpellData(code, 1, 1);

    if (!target || !G_UnitIsHero(target) || amount == 0) {
        return;
    }
    G_HeroSetXP(target, target->hero.xp + amount);
}

/* WarSmash: CAbilityItemLevelGain — grants hero level. */
static void item_level_command(LPEDICT clent) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_LEVEL_GAIN);
    DWORD levels = (DWORD)S_SpellData(code, 1, 1);

    if (!target || !G_UnitIsHero(target) || levels == 0) {
        return;
    }
    DWORD target_level = MIN(target->hero.level + levels, G_MaxHeroLevel());
    DWORD target_xp = G_HeroXPForLevel(target_level);
    if (target_xp > target->hero.xp) {
        G_HeroSetXP(target, target_xp);
    }
}

/* WarSmash: CAbilityItemFigurineSummon — summons a unit. */
static void item_figurine_command(LPEDICT clent) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_FIGURINE);
    DWORD unit_id = S_SpellUnitId(code, 1);

    if (!target || !unit_id) {
        return;
    }
    SP_SpawnAtLocation(unit_id, target->s.player, &target->s.origin2);
}

/* ---- Ability definitions ------------------------------------------------ */

ability_t a_item_heal = {
    .cmd = item_heal_command,
};

ability_t a_item_mana_regain = {
    .cmd = item_mana_command,
};

ability_t a_item_permanent_life_gain = {
    .cmd = item_permanent_life_command,
};

/* Passive items: init reads bonus value from SLK, actual apply/remove
 * handled by s_item_stats.c via inventory lifecycle hooks. */
ability_t a_item_attack_bonus = {
    .init = SP_ability_item_attack_bonus,
};

ability_t a_item_stat_bonus = {
    .init = SP_ability_item_stat_bonus,
};

ability_t a_item_defense_bonus = {
    .init = SP_ability_item_defense_bonus,
};

ability_t a_item_life_bonus = {
    .init = SP_ability_item_life_bonus,
};

ability_t a_item_mana_bonus = {
    .init = SP_ability_item_mana_bonus,
};

/* Consume-on-use items. */
ability_t a_item_permanent_stat_gain = {
    .cmd = item_permanent_stat_command,
};

ability_t a_item_figurine_summon = {
    .cmd = item_figurine_command,
};

ability_t a_item_experience_gain = {
    .cmd = item_experience_command,
};

ability_t a_item_level_gain = {
    .cmd = item_level_command,
};
