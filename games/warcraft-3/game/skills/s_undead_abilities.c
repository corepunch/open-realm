#include "s_skills.h"

#define UNDEAD_AUTOCAST_RADIUS 900.0f // world units; fallback acquisition radius when the spell range is zero

static LPCSTR undead_buff(abilityitem_t const *spell, DWORD level) {
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : NULL;
}

/* Shared unit-target autocast acquire: friendly wounded targets for replenish. */
static BOOL undead_unit_autocast_acquire(LPEDICT caster, DWORD code, BOOL wounded) {
    LPEDICT best = NULL;
    FLOAT range = S_SpellRange(code, S_SpellLevel(caster, code));
    FLOAT best_distance = FLT_MAX;
    if (range <= 0.0f) range = UNDEAD_AUTOCAST_RADIUS;
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target)) {
        FLOAT distance;
        if (wounded && target->health.value >= target->health.max_value) continue;
        if (!S_SpellAllowsTarget(code, caster, target)) continue;
        distance = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if (distance <= range && distance < best_distance) { best = target; best_distance = distance; }
    }
    return best && S_CastUnitTargetSpell(caster, code, best);
}

/* Shared area autocast acquire: cast self-spell if a worthy friendly exists in area. */
static BOOL undead_area_autocast_acquire(LPEDICT caster, DWORD code, BOOL needs_hp, BOOL needs_mana) {
    DWORD level = S_SpellLevel(caster, code);
    FLOAT area = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    if (area <= 0.0f) area = UNDEAD_AUTOCAST_RADIUS;
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area) {
        if (needs_hp && target->health.value < target->health.max_value) return S_CastNoTargetSpell(caster, code);
        if (needs_mana && target->mana.max_value > 0 && target->mana.value < target->mana.max_value)
            return S_CastNoTargetSpell(caster, code);
    }
    return false;
}

/* ---- Replenish (Arpb) --------------------------------------------------------
 * Name=Replenish
 * Ubertip="Replenish the life and mana of a target friendly unit."
 * Untip="Right-click to activate auto-casting."
 * DataA = HP restored, DataB = mana restored. BuffID = Brpb.
 */
static BOOL replenish_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsFriend(caster, st.entity) &&
           !G_UnitIsHero(st.entity);
}

static void replenish_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT target = st.entity;
    LPCSTR buff = undead_buff(spell, level);
    if (!target) return;
    S_SpellHeal(target, S_SpellData(spell->code, level, 1));
    target->mana.value = MIN(target->mana.max_value, target->mana.value + S_SpellData(spell->code, level, 2));
    if (buff) G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
}

BZ_ABILITY_PROC(CAbilityReplenish) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return replenish_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: replenish_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return undead_unit_autocast_acquire(ent, code, true);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Essence of Blight (Arpl) ------------------------------------------------
 * Name=Essence of Blight
 * Ubertip="Restores DataA1 hit points to nearby friendly units."
 * Untip="Right-click to activate auto-casting."
 * DataA = HP restored per unit in area.
 */
static void replenish_life_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FLOAT amount = S_SpellData(spell->code, level, 1);
    (void)st;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        S_SpellHeal(target, amount);
}

BZ_ABILITY_PROC(CAbilityReplenishLife) {
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_EXECUTE: replenish_life_execute(ent, MAKE(spellTarget_t, .type = SPELL_TARGET_NONE), call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return undead_area_autocast_acquire(ent, code, true, false);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Spirit Touch (Arpm) -----------------------------------------------------
 * Name=Spirit Touch
 * Ubertip="Restores DataB1 mana to nearby friendly units."
 * Untip="Right-click to activate auto-casting."
 * DataB = mana restored per unit in area.
 */
static void replenish_mana_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FLOAT amount = S_SpellData(spell->code, level, 2);
    (void)st;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        target->mana.value = MIN(target->mana.max_value, target->mana.value + amount);
}

BZ_ABILITY_PROC(CAbilityReplenishMana) {
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_EXECUTE: replenish_mana_execute(ent, MAKE(spellTarget_t, .type = SPELL_TARGET_NONE), call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return undead_area_autocast_acquire(ent, code, false, true);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Raise Dead (Arai) -------------------------------------------------------
 * Name=Raise Dead
 * Ubertip="Raises DataA1 skeletons from a corpse."
 * Untip="Right-click to activate auto-casting."
 * DataA = count of skeletons, UnitID = skeleton type, BuffID = Brai.
 */
static BOOL raise_dead_has_corpse(LPEDICT caster, FLOAT range) {
    FILTER_EDICTS(unit, unit->inuse && M_IsDead(unit) && !G_UnitIsHero(unit) &&
                  Vector2_distance(&unit->s.origin2, &caster->s.origin2) <= range) return true;
    return false;
}

static BOOL raise_dead_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)st;
    return raise_dead_has_corpse(caster, S_SpellRange(spell->code, S_SpellLevel(caster, spell->code)));
}

static void raise_dead_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD count = (DWORD)MAX(1.0f, S_SpellData(spell->code, level, 1));
    FLOAT range = S_SpellRange(spell->code, level);
    LPEDICT corpse = NULL;
    FLOAT best = FLT_MAX;
    (void)st;
    FILTER_EDICTS(unit, unit->inuse && M_IsDead(unit) && !G_UnitIsHero(unit)) {
        FLOAT d = Vector2_distance(&unit->s.origin2, &caster->s.origin2);
        if (d <= range && d < best) { corpse = unit; best = d; }
    }
    if (!corpse) return;
    FOR_LOOP(i, count) S_SummonAt(caster, S_SpellUnitId(spell->code, level), &corpse->s.origin2,
                                  S_SpellDuration(spell->code, level, false));
    G_FreeEdict(corpse);
}

static BOOL raise_dead_autocast_acquire(LPEDICT caster, DWORD code) {
    FLOAT range = S_SpellRange(code, S_SpellLevel(caster, code));
    if (range <= 0.0f) range = UNDEAD_AUTOCAST_RADIUS;
    return raise_dead_has_corpse(caster, range) && S_CastNoTargetSpell(caster, code);
}

BZ_ABILITY_PROC(CAbilityRaiseDead) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return raise_dead_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: raise_dead_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return raise_dead_autocast_acquire(ent, code);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}
