#include "s_skills.h"

/* ROC AbilityData omits BuffID; apply the TFT token like Aams→Bams / Acyc→Bcyc. */
static cstring_t melee_buff_fallback(uint32_t code) {
    static struct { uint32_t code; cstring_t buff; } const table[] = {
        { MAKEFOURCC('A', 'b', 'l', 'o'), "Bblo" },
        { MAKEFOURCC('A', 'C', 'b', 'l'), "Bblo" },
        { MAKEFOURCC('A', 'C', 'b', 'b'), "Bblo" },
        { MAKEFOURCC('A', 'f', 'a', 'e'), "Bfae" },
        { MAKEFOURCC('A', 'r', 'e', 'j'), "Brej" },
        { MAKEFOURCC('A', 'r', 'o', 'a'), "Broa" },
        { MAKEFOURCC('A', 'c', 'r', 's'), "Bcrs" },
        { MAKEFOURCC('A', 'C', 'c', 's'), "Bcrs" },
        { MAKEFOURCC('A', 'u', 'h', 'f'), "BUhf" },
        { MAKEFOURCC('A', 'C', 'u', 'f'), "BUhf" },
        { MAKEFOURCC('S', 'u', 'h', 'f'), "BUhf" },
        { MAKEFOURCC('A', 'f', 'z', 'y'), "Bfzy" },
        { MAKEFOURCC('A', 'C', 'r', 'o'), "Broa" },
        { MAKEFOURCC('A', 'C', 'r', '1'), "Broa" },
        { MAKEFOURCC('A', 'C', 'r', 'j'), "Brej" },
        { MAKEFOURCC('A', 'C', 'r', '2'), "Brej" },
        { MAKEFOURCC('A', 'C', 'f', 'f'), "Bfae" },
    };
    FOR_LOOP(i, sizeof(table) / sizeof(table[0]))
        if (table[i].code == code) return table[i].buff;
    return NULL;
}

static cstring_t melee_buff(abilityitem_t const *spell, uint32_t level) {
    cstring_t buff = S_SpellBuffId(spell->code, level);
    return buff ? buff : melee_buff_fallback(spell->code);
}

static void melee_status_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = melee_buff(spell, level);
    if (!st.entity || !buff) return;
    heroabilitystatus_t *status = S_SpellApplyTimedTargetStatus(st.entity, spell->code, level, buff,
                                  S_SpellHeroDuration(spell->code, level, st.entity));
    if (status && status->code == MAKEFOURCC('B','b','l','o'))
        status->buff_flags = WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_TRANSFERABLE;
    else if (status && status->code == MAKEFOURCC('B','f','a','e'))
        status->buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_TRANSFERABLE;
    else if (status && (status->code == MAKEFOURCC('B','r','e','j') ||
                        status->code == MAKEFOURCC('B','r','o','a')))
        status->buff_flags |= WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL;
    else if (status && (status->code == MAKEFOURCC('B','c','r','s') ||
                        status->code == MAKEFOURCC('B','U','h','f') ||
                        status->code == MAKEFOURCC('B','u','h','f')))
        status->buff_flags |= WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL;
}

static bool melee_status_capacity(edict_t *caster, edict_t *target, abilityitem_t const *spell) {
    uint32_t const level = S_SpellLevel(caster, spell->code);
    cstring_t const buff = melee_buff(spell, level);
    wc3_status_apply_check_t const check = buff ? unit_status_checkapplication(target,
        &(status_application_t){ .buff = buff, .level = level }) : WC3_STATUS_APPLY_INVALID;
    return check == WC3_STATUS_APPLY_REUSE || check == WC3_STATUS_APPLY_FREE_SLOT;
}

static bool bloodlust_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    return spell && st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsFriend(caster, st.entity) &&
        melee_status_capacity(caster, st.entity, spell);
}

static bool faerie_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    return spell && st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsEnemy(caster, st.entity) &&
        melee_status_capacity(caster, st.entity, spell);
}

static bool rejuv_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    uint32_t level;
    cstring_t buff;
    wc3_status_apply_check_t check;
    if (!spell || !st.entity || !S_SpellIsAliveTarget(st.entity) || !S_SpellIsFriend(caster, st.entity))
        return false;
    level = S_SpellLevel(caster, spell->code);
    buff = melee_buff(spell, level);
    if (!buff) return false;
    check = unit_status_checkapplication(st.entity, &(status_application_t){ .buff = buff, .level = level });
    return check == WC3_STATUS_APPLY_REUSE || check == WC3_STATUS_APPLY_FREE_SLOT;
}

/* Name=Bloodlust
 * Ubertip="Increases a friendly unit's attack rate by <Ablo,DataA1,%>% and movement speed by <Ablo,DataB1,%>%. |nLasts <Ablo,Dur1> seconds."
 * Untip="|cffc3dbffRight-click to activate auto-casting.|r"
 * Unubertip="|cffc3dbffRight-click to deactivate auto-casting.|r"
 */
BZ_ABILITY_PROC(CAbilityBloodlust) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return bloodlust_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: melee_status_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return S_AutocastAcquireUnit(ent, code, true, false, 900.0f);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* Name=Faerie Fire
 * Ubertip="Reduces a target enemy unit's armor by <Afae,DataA1> and gives vision of that unit. |nLasts <Afae,Dur1> seconds."
 * Untip="|cffc3dbffRight-click to activate auto-casting.|r"
 * Unubertip="|cffc3dbffRight-click to deactivate auto-casting.|r"
 */
BZ_ABILITY_PROC(CAbilityFaerieFire) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return faerie_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: melee_status_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return S_AutocastAcquireUnit(ent, code, false, false, 900.0f);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* Name=Rejuvenation
 * Ubertip="Heals a target friendly unit for <Arej,DataA1> hit points over <Arej,Dur1> seconds."
 */
BZ_VALIDATED_SPELL_PROC(AbilityRejuvination, rejuv_validate, melee_status_execute)

/* Name=Roar
 * Ubertip="Gives friendly nearby units a <Aroa,DataA1,%>% bonus to damage. |nLasts <Aroa,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityRoar) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    cstring_t buff = melee_buff(spell, level);
    float duration = S_SpellDuration(spell->code, level, false);
    if (!buff) return;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area) {
        heroabilitystatus_t *status = S_SpellApplyTimedStatus(target, buff, level, duration);
        if (status && status->code == MAKEFOURCC('B','r','o','a'))
            status->buff_flags |= WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL;
    }
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, caster, NULL, true);
}

/* DataA owns the attack-rate bonus as a fraction (0.4 = +40%); DataB owns move speed. */
float S_BloodlustAttackBonus(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'b', 'l', 'o'));
    return level ? S_SpellData(MAKEFOURCC('A', 'b', 'l', 'o'), level, 1) : 0.0f;
}

float S_BloodlustMoveBonus(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'b', 'l', 'o'));
    return level ? S_SpellData(MAKEFOURCC('A', 'b', 'l', 'o'), level, 2) : 0.0f;
}

/* DataA owns the armor reduction as a flat amount. */
float S_FaerieArmorDelta(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'f', 'a', 'e'));
    return level ? -S_SpellData(MAKEFOURCC('A', 'f', 'a', 'e'), level, 1) : 0.0f;
}

/* DataA owns the damage bonus as a fraction (0.25 = +25%). */
float S_RoarDamageBonus(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'r', 'o', 'a'));
    return level ? S_SpellData(MAKEFOURCC('A', 'r', 'o', 'a'), level, 1) : 0.0f;
}

/* DataA owns total healing over Dur seconds; tick rate derives from both. */
float S_RejuvHealRate(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'r', 'e', 'j'));
    float duration;
    if (!level) return 0.0f;
    duration = S_SpellDuration(MAKEFOURCC('A', 'r', 'e', 'j'), level, false);
    return duration > 0.0f ? S_SpellData(MAKEFOURCC('A', 'r', 'e', 'j'), level, 1) / duration : 0.0f;
}

/* Name=Frenzy
 * Ubertip="Increases a unit's attack rate by <Afzy,DataA1,%>% but reduces its armor by <Afzy,DataB1>. |nLasts <Afzy,Dur1> seconds."
 * Untip="|cffc3dbffRight-click to activate auto-casting.|r"
 * Unubertip="|cffc3dbffRight-click to deactivate auto-casting.|r"
 */
BZ_ABILITY_PROC(CAbilityFrenzy) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return bloodlust_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: melee_status_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return S_AutocastAcquireUnit(ent, code, true, false, 900.0f);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* DataA owns the attack-rate bonus as a fraction; DataB owns the armor reduction (flat). */
float S_FrenzyAttackBonus(edict_t const *unit) {
    (void)unit;
    return 0.0f; /* Consumed through WC3_STATUS_MOD_ATTACK_SPEED_PERCENT. */
}

float S_FrenzyArmorDelta(edict_t const *unit) {
    (void)unit;
    return 0.0f; /* Consumed through WC3_STATUS_MOD_ARMOR_FLAT. */
}

/* Name=Unholy Frenzy
 * Ubertip="Increases the attack rate of a target unit by <Auhf,DataA1,%>%, but drains <Auhf,DataB1> hit points per second. |nLasts <Auhf,Dur1> seconds."
 * targs are air,ground,organic with no allegiance token; enemy casts are legal.
 */
static bool unholy_frenzy_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    return spell && st.entity && S_SpellIsAliveTarget(st.entity) &&
        S_SpellAllowsTarget(spell->code, caster, st.entity);
}

BZ_VALIDATED_SPELL_PROC(AbilityUnholyFrenzy, unholy_frenzy_validate, melee_status_execute)

/* TFT BuffID is BUhf; item AIuf authors Buhf. Consumers accept both fourccs. */
static uint32_t unholy_frenzy_level(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'U', 'h', 'f'));
    return level ? level : G_UnitStatusLevel(unit, MAKEFOURCC('B', 'u', 'h', 'f'));
}

/* DataA owns the attack-rate bonus as a fraction; DataB owns the life drain in HP/second. */
float S_UnholyFrenzyAttackBonus(edict_t const *unit) {
    (void)unit;
    return 0.0f; /* Consumed through WC3_STATUS_MOD_ATTACK_SPEED_PERCENT. */
}

float S_UnholyFrenzyLifeDrain(edict_t const *unit) {
    uint32_t level = unholy_frenzy_level(unit);
    return level ? S_SpellData(MAKEFOURCC('A', 'u', 'h', 'f'), level, 2) : 0.0f;
}

/* Name=Curse
 * Ubertip="Curses a target enemy unit, giving it a <Acrs,DataA1,%>% chance to miss when attacking. |nLasts <Acrs,Dur1> seconds."
 * Untip="|cffc3dbffRight-click to activate auto-casting.|r"
 * Unubertip="|cffc3dbffRight-click to deactivate auto-casting.|r"
 */
BZ_ABILITY_PROC(CAbilityCurse) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return faerie_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: melee_status_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return S_AutocastAcquireUnit(ent, code, false, false, 900.0f);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* DataA owns the miss chance as a fraction (0.33 = 33% miss chance). */
float S_CurseMissChance(edict_t const *unit) {
    uint32_t level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'c', 'r', 's'));
    return level ? S_SpellData(MAKEFOURCC('A', 'c', 'r', 's'), level, 1) : 0.0f;
}
