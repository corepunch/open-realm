#include "s_skills.h"

/* Night Elf passive attack abilities: Moon Glaive (Amgl/Amgr) and Slow Poison (Aspo). */

#define ID_MOON_GLAIVE  MAKEFOURCC('A','m','g','l')
#define ID_MOON_GLAIVER MAKEFOURCC('A','m','g','r')
#define ID_SLOW_POISON  MAKEFOURCC('A','s','p','o')
#define ID_BARKSKIN     MAKEFOURCC('A','b','a','r')
#define BUFF_SLOW_POI   MAKEFOURCC('B','s','p','o')
#define BUFF_BARKSKIN   MAKEFOURCC('B','b','a','r')

cstring_t const barkskin_orders[] = { "barkskinon", "barkskinoff", NULL };

/* ---- Moon Glaive (Amgl / Amgr): passive attack bounce -------------------- */

BZ_ABILITY_PROC(CAbilityMoonGlaive) { return CAbilityPassive(ent, msg, call); }

/* Called from S_ResolveAttackHit after the primary hit lands. Stock Amgl DataA
 * and Area are 0; that means one extra bounce inside attack range, not "no bounce".
 * Authored DataA>0 is total targets. Damage is the already-mitigated primary hit. */
void S_MoonGlaiveAttack(edict_t *attacker, edict_t *primary, int damage) {
    uint32_t level = G_UnitAbilityLevel(attacker, ID_MOON_GLAIVE);
    float data_a, range;
    uint32_t total;
    edict_t *visited[8], *current;
    uint32_t nvisited = 0;
    if (!level) level = G_UnitAbilityLevel(attacker, ID_MOON_GLAIVER);
    if (!level || !primary) return;
    data_a = S_SpellData(ID_MOON_GLAIVE, level, 1);
    total = data_a > 0.0f ? (uint32_t)data_a : 2; /* stock 0 → primary + 1 bounce */
    range = S_SpellNumber(ID_MOON_GLAIVE, ABILITY_NUMBER_AREA, level);
    if (range <= 0.0f) range = attacker->attack1.range;
    if (nvisited < 8) visited[nvisited++] = primary;
    current = primary;
    for (uint32_t i = 1; i < total; i++) {
        edict_t *next = NULL;
        FILTER_EDICTS(other, other != attacker && S_SpellIsAliveTarget(other) &&
                      S_SpellIsEnemy(attacker, other) &&
                      Vector2_distance(&other->s.origin2, &current->s.origin2) <= range) {
            bool seen = false;
            FOR_LOOP(j, nvisited) seen |= other == visited[j];
            if (!seen) { next = other; break; }
        }
        if (!next) break;
        if (nvisited < 8) visited[nvisited++] = next;
        T_Damage(next, attacker, damage);
        current = next;
    }
}

/* ---- Slow Poison (Aspo): passive poison on hit --------------------------- */

BZ_ABILITY_PROC(CAbilitySlowPoison) { return CAbilityPassive(ent, msg, call); }

/* Called from S_ResolveAttackHit after a hit lands on an enemy.  Applies the
 * Bspo buff which the movement and attack-speed hooks read each frame. */
void S_SlowPoisonOnHit(edict_t *attacker, edict_t *target) {
    uint32_t level = G_UnitAbilityLevel(attacker, ID_SLOW_POISON);
    if (!level || !target || !S_SpellIsEnemy(attacker, target)) return;
    /* DataA DPS / BuffID Bssd are leftover; this slice only applies Bspo slow. */
    unit_addtimedstatus(target, "Bspo", level, S_SpellDuration(ID_SLOW_POISON, level, S_UnitIsResistant(target)));
}

/* DataB/DataC are fractions (stock 0.5 / 0.25), same %>% convention as Bloodlust. */
float S_SlowPoisonMoveReduction(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, BUFF_SLOW_POI);
    if (!level) return 0.0f;
    return S_SpellData(ID_SLOW_POISON, level, 2);
}

float S_SlowPoisonAttackReduction(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, BUFF_SLOW_POI);
    if (!level) return 0.0f;
    return S_SpellData(ID_SLOW_POISON, level, 3);
}

/* ---- Barkskin (Abar): modal autocast of a timed friendly armor buff ------- */

static bool barkskin_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    return spell && st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsFriend(caster, st.entity) &&
        S_SpellAllowsTarget(spell->code, caster, st.entity);
}

static void barkskin_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = G_AbilityLevel(spell->code, level)->buffID;
    if (!st.entity || !buff || strlen(buff) < 4) return;
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, S_UnitIsResistant(st.entity)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

static bool barkskin_acquire(edict_t *caster, uint32_t code) {
    edict_t *best = NULL;
    float range = S_SpellRange(code, S_SpellLevel(caster, code)), dist = FLT_MAX;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target)) {
        float cur;
        if (G_UnitStatusLevel(target, BUFF_BARKSKIN) || !S_SpellAllowsTarget(code, caster, target)) continue;
        cur = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if ((range <= 0.0f || cur <= range) && cur < dist) { best = target; dist = cur; }
    }
    return best && S_CastUnitTargetSpell(caster, code, best);
}

BZ_ABILITY_PROC(CAbilityBarkskin) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t code = call && call->item && call->item->code ? call->item->code : ID_BARKSKIN;
    switch (msg) {
    case A_VALIDATE: return barkskin_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: barkskin_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ACQUIRE: return barkskin_acquire(ent, code);
    case A_ORDER:
        if (call && call->order && !strcmp(call->order, "barkskinon")) return G_SetUnitAutocast(ent, code, true);
        if (call && call->order && !strcmp(call->order, "barkskinoff")) return G_SetUnitAutocast(ent, code, false);
        return false;
    default: return CAbilityModalSpell(ent, msg, call);
    }
}

/* Bbar reads Abar DataA so expiry removes the armor without mutating base unit state. */
float S_BarkskinArmorBonus(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, BUFF_BARKSKIN);
    return level ? S_SpellData(ID_BARKSKIN, level, 1) : 0.0f;
}
