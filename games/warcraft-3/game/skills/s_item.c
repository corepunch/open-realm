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
#define ID_ITEM_DEFENSE_AOE    MAKEFOURCC('A', 'I', 'd', 'a')
#define ID_ITEM_CHANGE_TIME    MAKEFOURCC('A', 'I', 'c', 't')

/* ---- Active items (consume on use) -------------------------------------- */

BZ_ITEM_PROC(AbilityItemHeal) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_HEAL);
    FLOAT amount = S_SpellData(code, 1, 1);

    if (!S_SpellIsAliveTarget(target) || amount <= 0 || target->health.value >= target->health.max_value) {
        return false;
    }
    S_SpellHeal(target, amount);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

BZ_ITEM_PROC(AbilityItemManaRestore) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_MANA);
    FLOAT amount = S_SpellData(code, 1, 1);

    if (!target || amount <= 0 || target->mana.value >= target->mana.max_value) {
        return false;
    }
    target->mana.value = MIN(target->mana.max_value, target->mana.value + amount);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

BZ_ITEM_PROC(AbilityMaxLifeMod) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_LIFE_GAIN);
    FLOAT amount = S_SpellData(code, 1, 1);

    if (!target || amount <= 0) {
        return false;
    }
    target->health.max_value += amount;
    G_AddHealth(target, amount);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

/* WarSmash: CAbilityItemPermanentStatGain.checkBeforeQueue
 * Permanently adds to hero base stats, consumes the item. */
BZ_ITEM_PROC(AbilityStrengthMod) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, 0);
    FLOAT str = S_SpellData(code, 1, 3);
    FLOAT agi = S_SpellData(code, 1, 1);
    FLOAT intel = S_SpellData(code, 1, 2);

    if (!target || !G_UnitIsHero(target)) {
        return false;
    }
    target->hero.str += (DWORD)str;
    target->hero.agi += (DWORD)agi;
    target->hero.intel += (DWORD)intel;
    G_RecomputeHeroStats(target);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

/* WarSmash: CAbilityItemExperienceGain — grants XP. */
BZ_ITEM_PROC(AbilityExperienceMod) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_XP_GAIN);
    DWORD amount = (DWORD)S_SpellData(code, 1, 1);

    if (!target || !G_UnitIsHero(target) || amount == 0) {
        return false;
    }
    G_HeroSetXP(target, target->hero.xp + amount);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

/* WarSmash: CAbilityItemLevelGain — grants hero level. */
BZ_ITEM_PROC(AbilityLevelMod) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_LEVEL_GAIN);
    DWORD levels = (DWORD)S_SpellData(code, 1, 1);

    if (!target || !G_UnitIsHero(target) || levels == 0) {
        return false;
    }
    DWORD target_level = MIN(target->hero.level + levels, G_MaxHeroLevel());
    DWORD target_xp = G_HeroXPForLevel(target_level);
    if (target_xp <= target->hero.xp) {
        return false;
    }
    G_HeroSetXP(target, target_xp);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    return true;
}

/* WarSmash: CAbilityItemFigurineSummon — summons a unit. */
BZ_ITEM_PROC(AbilityFigurineSkeleton) {
    LPEDICT target = G_GetMainSelectedUnit(clent->client);
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_FIGURINE);
    DWORD unit_id = S_SpellUnitId(code, 1);

    if (!target || !unit_id) {
        return false;
    }
    LPEDICT summon = SP_SpawnAtLocation(unit_id, target->s.player, &target->s.origin2);
    if (!summon) {
        return false;
    }
    G_ActivateUnitFood(summon);
    G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, summon, NULL, true);
    return true;
}

/* Scroll of Protection / item defense AOE (AIda). Warcraft data carries the
 * defense amount in DataA, radius in Area, duration in Dur/HeroDur and the
 * visible status rawcode in BuffID. Keep the item itself as a thin ability
 * carrier: the ability data decides the actual numbers. */
BZ_ITEM_PROC(AbilityItemDefenseAoe) {
    LPEDICT caster = clent && clent->client ? G_GetMainSelectedUnit(clent->client) : NULL;
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_DEFENSE_AOE);
    DWORD level = 1;
    FLOAT bonus = S_SpellData(code, level, 1);
    FLOAT area = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    LPCSTR buff = G_AbilityLevel(code, level)->buffID;
    DWORD affected = 0;

    if (!caster || bonus <= 0.0f || area < 0.0f || !buff || strlen(buff) < 4) {
        return false;
    }

#define ITEM_DEFENSE_AOE_TARGET(t) \
    ((t)->inuse && S_SpellIsAliveTarget(t) && S_SpellIsFriend(caster, (t)) && \
     S_SpellAllowsTarget(code, caster, (t)) && \
     Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= area)

    FILTER_EDICTS(target, ITEM_DEFENSE_AOE_TARGET(target)) {
        FLOAT duration = S_SpellDuration(code, level, G_UnitIsHero(target));
        unit_addtimedstatus(target, buff, level, duration);
        G_SpawnAbilityEffectTarget(code, WC3_EFFECT_TARGET, 0, target, NULL, true);
        affected++;
    }
#undef ITEM_DEFENSE_AOE_TARGET

    return affected != 0;
}

/* Warsmash itemSimple.json: AIct (itemchangetimeofday) reads DataA/DataB as
 * hour/minute and Dur as the false-time lifetime.  The false clock is a
 * simulation override, not a renderer-only tint, so all day/night consumers
 * see the same temporary time. */
BZ_ITEM_PROC(AbilityItemChangeTOD) {
    LPEDICT caster = clent && clent->client ? G_GetMainSelectedUnit(clent->client) : NULL;
    DWORD code = S_SpellCurrentCode(clent, ID_ITEM_CHANGE_TIME);
    LONG hour = (LONG)S_SpellData(code, 1, 1);
    LONG minute = (LONG)S_SpellData(code, 1, 2);
    FLOAT duration = S_SpellDuration(code, 1, false);

    if (!caster) {
        return false;
    }
    G_SetFalseTimeOfDay(hour, minute, duration);
    return true;
}
