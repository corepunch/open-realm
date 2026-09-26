#include "s_skills.h"

/* Passive item stat bonuses — apply on pickup, reverse on drop.
 * Follows WarSmash pattern: CAbilityItemAttackBonus.onAdd/onRemove,
 * CAbilityItemDefenseBonus.onAdd/onRemove, etc. */

static void apply_attack(LPEDICT unit, float amount) {
    G_ApplyTemporaryAttackDamageBonus(unit, amount);
}

static void apply_defense(LPEDICT unit, float amount) {
    G_ApplyTemporaryArmorBonus(unit, amount);
}

static void apply_life(LPEDICT unit, float amount) {
    G_ApplyTemporaryMaxHealthBonus(unit, amount);
}

static void apply_mana(LPEDICT unit, float amount) {
    G_ApplyTemporaryMaxManaBonus(unit, amount);
}

/* Attribute aliases share the authored Agility/Intelligence/Strength field order used by tomes. */
static void apply_stat(LPEDICT unit, uint32_t code, float sign) {
    float str = sign * S_SpellData(code, 1, 3);
    float agi = sign * S_SpellData(code, 1, 1);
    float intel = sign * S_SpellData(code, 1, 2);
    if (!G_UnitIsHero(unit)) {
        return;
    }
    unit->hero.str = (uint32_t)MAX(0, (int32_t)unit->hero.str + (int32_t)str);
    unit->hero.agi = (uint32_t)MAX(0, (int32_t)unit->hero.agi + (int32_t)agi);
    unit->hero.intel = (uint32_t)MAX(0, (int32_t)unit->hero.intel + (int32_t)intel);
    G_RecomputeHeroStats(unit);
}

/* Inventory messages retain the authored alias, including distinct values on stacked items. */
#define BZ_ITEM_BONUS_PROC(NAME, APPLY) \
    BZ_ABILITY_PROC(C##NAME) { \
        if (msg != A_ITEM_ADD && msg != A_ITEM_REMOVE) return CAbilityPassive(ent, msg, call); \
        if (!ent || !call || !call->item) return false; \
        float sign = msg == A_ITEM_ADD ? 1.0f : -1.0f; \
        APPLY(ent, sign * S_SpellData(call->item->code, 1, 1)); \
        return true; \
    }

BZ_ITEM_BONUS_PROC(AbilityAttackBonus, apply_attack)
BZ_ITEM_BONUS_PROC(AbilityDefenseBonus, apply_defense)
BZ_ITEM_BONUS_PROC(AbilityMaxLifeBonus, apply_life)
BZ_ITEM_BONUS_PROC(AbilityMaxManaBonus, apply_mana)

/* Read every attribute from the same alias on acquisition and removal. */
BZ_ABILITY_PROC(CAbilityAttributeBonus) {
    if (msg != A_ITEM_ADD && msg != A_ITEM_REMOVE) return CAbilityPassive(ent, msg, call);
    if (!ent || !call || !call->item) return false;
    float sign = msg == A_ITEM_ADD ? 1.0f : -1.0f;
    apply_stat(ent, call->item->code, sign);
    return true;
}

/* Item orb family (AIfb/AIlb/AIob/AIpb/AIcb/AIzb, retail parent AIDB).
 * Bonus damage rides CAbilityAttackBonus pickup handling via DataA; this owns
 * the on-hit BuffID state next to S_SlowPoisonOnHit. ROC omits BuffID, so the
 * three buffed orbs fall back to their TFT tokens. AIfb/AIlb/AIpb author no
 * buff in either version. Buffs are state-only like Frost Nova's Bfro: no
 * movement/armor consumer reads Bfro/BIcb/Bfre yet. */
#define ID_ORB_FIRE MAKEFOURCC('A', 'I', 'f', 'b')
#define ID_ORB_LIGHTNING MAKEFOURCC('A', 'I', 'l', 'b')
#define ID_ORB_FROST MAKEFOURCC('A', 'I', 'o', 'b')
#define ID_ORB_POISON MAKEFOURCC('A', 'I', 'p', 'b')
#define ID_ORB_CORRUPTION MAKEFOURCC('A', 'I', 'c', 'b')
#define ID_ORB_FREEZE MAKEFOURCC('A', 'I', 'z', 'b')

static uint32_t const orb_codes[] = {
    ID_ORB_FIRE, ID_ORB_LIGHTNING, ID_ORB_FROST, ID_ORB_POISON, ID_ORB_CORRUPTION, ID_ORB_FREEZE,
};

static cstring_t orb_buff(uint32_t code) {
    static struct { uint32_t code; cstring_t buff; } const fallback[] = {
        { ID_ORB_FROST, "Bfro" },
        { ID_ORB_CORRUPTION, "BIcb" },
        { ID_ORB_FREEZE, "Bfre" },
    };
    cstring_t buff = G_AbilityLevel(code, 1)->buffID;
    if (buff && strlen(buff) >= 4) return buff;
    FOR_LOOP(i, sizeof(fallback) / sizeof(fallback[0]))
        if (fallback[i].code == code) return fallback[i].buff;
    return NULL;
}

static void orb_apply(LPEDICT attacker, LPEDICT target, uint32_t orb, uint32_t *seen, uint32_t *count) {
    cstring_t buff;
    uint32_t level;
    FOR_LOOP(i, *count)
        if (seen[i] == orb) return;
    buff = orb_buff(orb);
    if (!buff) return;
    level = MAX(1, G_UnitAbilityLevel(attacker, orb));
    seen[(*count)++] = orb;
    unit_addtimedstatus(target, buff, level, S_SpellDuration(orb, level, G_UnitIsHero(target)));
}

/* Called from S_ResolveAttackHit after a hit lands on an enemy. Checks native
 * orb ownership and held orb items; the same orb from both sources applies once. */
void S_OrbOnHit(LPEDICT attacker, LPEDICT target) {
    uint32_t seen[sizeof(orb_codes) / sizeof(orb_codes[0])];
    uint32_t count = 0;
    if (!attacker || !target || !S_SpellIsEnemy(attacker, target)) return;
    FOR_LOOP(o, sizeof(orb_codes) / sizeof(orb_codes[0]))
        if (G_UnitAbilityLevel(attacker, orb_codes[o])) orb_apply(attacker, target, orb_codes[o], seen, &count);
    if (!G_InventoryCanUseItems(attacker)) return;
    FOR_LOOP(i, MAX_INVENTORY) {
        LPEDICT item = attacker->inventory[i];
        cstring_t abilities;
        if (!item) continue;
        abilities = G_ItemAbilityList(item);
        if (!abilities) continue;
        PARSE_LIST(abilities, name, parse_segment) {
            uint32_t code = strlen(name) == 4 ? FS_SLKKey(name) : 0;
            FOR_LOOP(o, sizeof(orb_codes) / sizeof(orb_codes[0]))
                if (code && code == orb_codes[o]) orb_apply(attacker, target, code, seen, &count);
        }
    }
}
