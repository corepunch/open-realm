#include "s_skills.h"

static LPCSTR status_buff(abilityitem_t const *spell, DWORD level) {
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : NULL;
}

static void status_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff = status_buff(spell, level);
    if (!st.entity || !buff) return;
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

/* ---- Cripple (Acri) -------------------------------------------------------
 * Name=Cripple
 * Ubertip="Reduces movement speed by <Acri,DataA1,%>%, attack rate by <Acri,DataB1,%>%, and damage by <Acri,DataC1,%>% of a target enemy unit. |nLasts <Acri,Dur1> seconds."
 */
static BOOL cripple_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsEnemy(caster, st.entity);
}

BZ_VALIDATED_SPELL_PROC(AbilityCripple, cripple_validate, status_execute)

/* DataA = movement speed reduction fraction; DataB = attack rate reduction; DataC = damage reduction. */
FLOAT S_CrippleMoveReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 1) : 0.0f;
}

FLOAT S_CrippleAttackReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 2) : 0.0f;
}

FLOAT S_CrippleDamageReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 'i'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 'i'), level, 3) : 0.0f;
}

/* ---- Soul Burn (ANso) -----------------------------------------------------
 * Name=Soul Burn
 * Ubertip="Wreaths an enemy unit in magical flames which cause <ANso,DataA1> damage per second, prevent the casting of spells, and reduce attack damage by <ANso,DataC1,%>%.|nLasts <ANso,Dur1> seconds."
 */
#define BZ_SILENCE_BUFF MAKEFOURCC('B', 'N', 's', 'i') // rawcode; Silence (ANsi) cast lock
#define BZ_SOUL_BURN_BUFF MAKEFOURCC('B', 'N', 's', 'o') // rawcode; Soul Burn cast lock + drain

/* BNsi (Silence) and BNso (Soul Burn) both reject spell casts with "Silenced." */
BOOL S_UnitIsSilenced(LPCEDICT unit) {
    return unit && (S_UnitHasStatus(unit, BZ_SILENCE_BUFF) || S_UnitHasStatus(unit, BZ_SOUL_BURN_BUFF));
}

static BOOL soul_burn_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsEnemy(caster, st.entity);
}

BZ_VALIDATED_SPELL_PROC(AbilitySoulBurn, soul_burn_validate, status_execute)

/* DataA = damage per second; DataC = attack damage reduction fraction. */
FLOAT S_SoulBurnDamageRate(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, BZ_SOUL_BURN_BUFF);
    return level ? S_SpellData(MAKEFOURCC('A', 'N', 's', 'o'), level, 1) : 0.0f;
}

FLOAT S_SoulBurnDamageReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, BZ_SOUL_BURN_BUFF);
    return level ? S_SpellData(MAKEFOURCC('A', 'N', 's', 'o'), level, 3) : 0.0f;
}

/* ---- Taunt (Atau) ----------------------------------------------------------
 * Name=Taunt
 * Ubertip="Forces nearby enemy units to attack the caster. Lasts <Atau,Dur1> seconds."
 * One-shot AOE that re-targets all enemies in Area to attack the caster.
 */
BZ_SIMPLE_SPELL_PROC(AbilityTaunt) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsEnemy(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        order_attack(target, caster);
}
