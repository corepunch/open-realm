#include "s_skills.h"

/* Passive item stat bonuses — apply on pickup, reverse on drop.
 * Follows WarSmash pattern: CAbilityItemAttackBonus.onAdd/onRemove,
 * CAbilityItemDefenseBonus.onAdd/onRemove, etc. */

static void apply_attack(edict_t *unit, float amount) {
    G_ApplyTemporaryAttackDamageBonus(unit, amount);
}

static void apply_defense(edict_t *unit, float amount) {
    G_ApplyTemporaryArmorBonus(unit, amount);
}

static void apply_life(edict_t *unit, float amount) {
    G_ApplyTemporaryMaxHealthBonus(unit, amount);
}

static void apply_mana(edict_t *unit, float amount) {
    G_ApplyTemporaryMaxManaBonus(unit, amount);
}

/* Arel: DataA hit points per second (Ring of Regeneration). */
static void apply_life_regen(edict_t *unit, float amount) {
    G_InitUnitState(unit);
    unit->unit_state->health_regen_bonus += amount;
}

/* Attribute aliases share the authored Agility/Intelligence/Strength field order used by tomes. */
static void apply_stat(edict_t *unit, uint32_t code, float sign) {
    float str = sign * S_SpellData(code, 1, 3);
    float agi = sign * S_SpellData(code, 1, 1);
    float intel = sign * S_SpellData(code, 1, 2);
    if (!G_UnitIsHero(unit)) {
        return;
    }
    unit->unit_state->hero.bonus_str += (int32_t)str;
    unit->unit_state->hero.bonus_agi += (int32_t)agi;
    unit->unit_state->hero.bonus_intel += (int32_t)intel;
    unit->unit_state->hero.str = (uint32_t)MAX(0, (int32_t)unit->unit_state->hero.str + (int32_t)str);
    unit->unit_state->hero.agi = (uint32_t)MAX(0, (int32_t)unit->unit_state->hero.agi + (int32_t)agi);
    unit->unit_state->hero.intel = (uint32_t)MAX(0, (int32_t)unit->unit_state->hero.intel + (int32_t)intel);
    G_RecomputeHeroStats(unit);
}

/* Item bonus abilities also work on a unit directly: UnitAddAbility sends
 * A_ENABLE and UnitRemoveAbility A_DISABLE. */
static bool S_ItemBonusMessage(abilityMsg_t msg) {
    return msg == A_ITEM_ADD || msg == A_ITEM_REMOVE || msg == A_ENABLE || msg == A_DISABLE;
}

/* Inventory messages retain the authored alias, including distinct values on stacked items. */
#define BZ_ITEM_BONUS_PROC(NAME, APPLY) \
    BZ_ABILITY_PROC(C##NAME) { \
        if (!S_ItemBonusMessage(msg)) return CAbilityPassive(ent, msg, call); \
        if (!ent || !call || !call->item) return false; \
        float sign = msg == A_ITEM_ADD || msg == A_ENABLE ? 1.0f : -1.0f; \
        APPLY(ent, sign * S_SpellData(call->item->code, 1, 1)); \
        return true; \
    }

BZ_ITEM_BONUS_PROC(AbilityAttackBonus, apply_attack)
BZ_ITEM_BONUS_PROC(AbilityDefenseBonus, apply_defense)
BZ_ITEM_BONUS_PROC(AbilityMaxLifeBonus, apply_life)
BZ_ITEM_BONUS_PROC(AbilityMaxManaBonus, apply_mana)
BZ_ITEM_BONUS_PROC(AbilityLifeRegenBonus, apply_life_regen)

/* Read every attribute from the same alias on acquisition and removal. */
BZ_ABILITY_PROC(CAbilityAttributeBonus) {
    if (!S_ItemBonusMessage(msg)) return CAbilityPassive(ent, msg, call);
    if (!ent || !call || !call->item) return false;
    float sign = msg == A_ITEM_ADD || msg == A_ENABLE ? 1.0f : -1.0f;
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

/* Held AIsr bracers reduce spell damage by their authored percentage. Use the
 * strongest carried reduction once; duplicate item copies do not compound. */
float S_ItemSpellDamageReduction(edict_t const *unit) {
    float reduction = 0.0f;
    if (!unit || !G_InventoryCanUseItems(unit)) return 0.0f;
    FOR_LOOP(slot, G_InventoryCapacity(unit)) {
        edict_t *item = G_InventoryItem((edict_t *)unit, slot);
        cstring_t abilities = item ? G_ItemAbilityList(item) : NULL;
        if (!abilities) continue;
        PARSE_LIST(abilities, name, parse_segment) {
            uint32_t const code = strlen(name) == 4 ? FS_SLKKey(name) : 0;
            float amount;
            if (code != MAKEFOURCC('A','I','s','r')) continue;
            amount = S_SpellData(code, 1, 2); /* Runed Bracers author reduction as DataB. */
            if (amount > 1.0f) amount *= 0.01f;
            reduction = MAX(reduction, MIN(1.0f, MAX(0.0f, amount)));
        }
    }
    return reduction;
}

static cstring_t orb_buff(uint32_t code) {
    static struct { uint32_t code; cstring_t buff; } const fallback[] = {
        { ID_ORB_FROST, "Bfro" },
        { ID_ORB_CORRUPTION, "BIcb" },
        { ID_ORB_FREEZE, "Bfre" },
    };
    cstring_t buff = S_SpellBuffId(code, 1);
    if (buff && strlen(buff) >= 4) return buff;
    FOR_LOOP(i, sizeof(fallback) / sizeof(fallback[0]))
        if (fallback[i].code == G_AbilityCode(code)) return fallback[i].buff;
    return NULL;
}

static void orb_apply(edict_t *attacker, edict_t *target, uint32_t orb, uint32_t *seen, uint32_t *count) {
    cstring_t buff;
    uint32_t level;
    FOR_LOOP(i, *count)
        if (seen[i] == orb) return;
    buff = orb_buff(orb);
    if (!buff) return;
    level = MAX(1, G_UnitAbilityLevel(attacker, orb));
    seen[(*count)++] = orb;
    S_SpellApplyTimedStatus(target, buff, level, S_SpellHeroDuration(orb, level, target));
}

/* Called from S_ResolveAttackHit after a hit lands on an enemy. Checks native
 * orb ownership and held orb items; the same orb from both sources applies once. */
void S_OrbOnHit(edict_t *attacker, edict_t *target) {
    uint32_t seen[sizeof(orb_codes) / sizeof(orb_codes[0])];
    uint32_t count = 0;
    if (!attacker || !target || !S_SpellIsEnemy(attacker, target)) return;
    FOR_LOOP(o, sizeof(orb_codes) / sizeof(orb_codes[0])) {
        /* Owned map aliases carry their own BuffID and duration. */
        abilityAliasRef_t const ref = S_ResolveAbilityAlias(attacker, orb_codes[o]);
        uint32_t const code = ref.alias ? ref.alias : orb_codes[o];
        if (G_UnitAbilityLevel(attacker, code)) orb_apply(attacker, target, code, seen, &count);
    }
    if (!G_InventoryCanUseItems(attacker)) return;
    FOR_LOOP(i, MAX_INVENTORY) {
        edict_t *item = G_InventoryItem(attacker, i);
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
