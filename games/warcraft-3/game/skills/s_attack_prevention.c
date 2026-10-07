/* Silence, Cloud and Drunken Haze share the retail Attacks Prevented buff
 * operations. Concrete abilities retain their target/area admission. */
#include "s_skills.h"
#include "games/warcraft-3/common/wc3_math.h"

static void prevention_remove(edict_t *unit, heroabilitystatus_t *slot) {
    uint32_t mask = slot ? slot->prevention_mask : 0;
    if (!unit || !mask) return;
    slot->prevention_mask = 0; /* Native buff20.200: inverse runs once. */
    S_AttackAdjustPrevention(unit, mask, true);
    if (mask & 8) ((unitStatusStorage_t *)unit->abilstatus)->spell_prevention--;
}

heroabilitystatus_t *S_ApplyAttackPrevention(edict_t *caster, edict_t *target,
        abilityitem_t const *spell, cstring_t buff, float duration) {
    heroabilitystatus_t *slot;
    uint32_t rank, mask;
    if (!caster || !target || !spell || !buff || strlen(buff) < 4) return NULL;
    rank = S_SpellLevel(caster, spell->code);
    mask = wc3_int_bits(wc3_float_bits(S_SpellData(spell->code, rank, 1)));
    slot = unit_findstatus(target, FS_SLKKey(buff));
    /* Public refresh first removes the previous buff object, including its
     * captured mask; never read changed object data to calculate an inverse. */
    if (slot) unit_expirestatus(target, slot);
    slot = S_SpellApplyTimedTargetStatus(target, spell->code, rank, buff, duration);
    if (!slot) return NULL;
    slot->data = spell->code;
    slot->source = caster; slot->source_spawn_time = caster->spawn_time;
    slot->rank = rank; slot->prevention_mask = mask;
    S_AttackAdjustPrevention(target, mask, false);
    if (mask & 8) {
        ((unitStatusStorage_t *)target->abilstatus)->spell_prevention++;
        S_SpellCancelChannel(target);
    }
    return slot;
}

BZ_ABILITY_PROC(CAbilityAttackPrevention) {
    heroabilitystatus_t *slot = call ? call->status.slot : NULL;
    switch (msg) {
    case A_STATUS_REMOVE:
    case A_STATUS_REPLACE:
        prevention_remove(ent, slot);
        return true;
    case A_STATUS_DEATH:
        if (slot) unit_expirestatus(ent, slot);
        return true;
    case A_STATUS_POLICY:
        return UNIT_BUFF_KNOWN | UNIT_BUFF_NEGATIVE | UNIT_BUFF_MAGIC | UNIT_BUFF_AUTO_DISPEL;
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}
