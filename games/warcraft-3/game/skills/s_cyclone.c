#include "s_skills.h"

#define BZ_CYCLONE_BUFF MAKEFOURCC('B','c','y','c') // rawcode; primary Cyclone status; locks actions and targeting
#define BZ_CYCLONE_BUFF_EXTRA MAKEFOURCC('B','c','y','2') // rawcode; extra Cyclone status; locks actions and targeting
#define BZ_TIMED_LIFE_BUFF MAKEFOURCC('B','T','L','F') // lifecycle marker; dispel must not make a temporary unit permanent

bool S_UnitIsCycloned(edict_t const *unit) {
    return unit && (G_UnitStatusLevel(unit, BZ_CYCLONE_BUFF) || G_UnitStatusLevel(unit, BZ_CYCLONE_BUFF_EXTRA));
}

/* BTLF owns temporary-unit lifetime and is never dispellable. Cyclone additionally
 * uses DataA==0 on its applying rawcode (status.data) for an authored undispellable buff. */
bool S_StatusIsUndispellable(heroabilitystatus_t const *status) {
    abilityitem_t item;
    if (!status || !status->level) return false;
    if (status->code == BZ_TIMED_LIFE_BUFF || status->code == MAKEFOURCC('B','O','w','k')) return true;
    if (!status->data) return false;
    item = S_AbilityItem(status->data);
    if (!item.ability || item.ability->proc != CAbilityCyclone) return false;
    return S_SpellData(status->data, status->level, 1) == 0.0f;
}

/* Validate via authored targs; empty BuffID falls back to Bcyc like Aams → Bams. DataA is dispel-only. */
BZ_ABILITY_PROC(CAbilityCyclone) {
    spellTarget_t const *target;
    uint32_t level;
    cstring_t buff;
    heroabilitystatus_t *slot;

    if ((msg != A_VALIDATE && msg != A_EXECUTE) || !call || !call->item || !call->target)
        return CAbilitySimpleSpell(ent, msg, call);
    target = call->target;
    if (msg == A_VALIDATE) {
        wc3_status_apply_check_t result;
        if (target->type != SPELL_TARGET_UNIT || !target->entity ||
            !S_SpellAllowsTarget(call->item->code, ent, target->entity)) return false;
        level = S_SpellLevel(ent, call->item->code);
        buff = S_SpellBuffId(call->item->code, level);
        if (!buff || strlen(buff) < 4) buff = "Bcyc";
        result = unit_status_checkapplication(target->entity, &(status_application_t){
            .buff = buff, .level = level
        });
        return result == WC3_STATUS_APPLY_FREE_SLOT || result == WC3_STATUS_APPLY_REUSE;
    }
    if (target->type != SPELL_TARGET_UNIT || !target->entity) return false;
    level = S_SpellLevel(ent, call->item->code);
    buff = S_SpellBuffId(call->item->code, level);
    if (!buff || strlen(buff) < 4) buff = "Bcyc";
    slot = S_SpellApplyTimedStatus(target->entity, buff, level,
                                    S_SpellResistantDuration(call->item->code, level, target->entity));
    /* Timed-status replacement clears payload; restore the applying rawcode. */
    if (slot) { slot->data = call->item->code; slot->source_ability = call->item->code; }
    if (!slot) return false; /* Do not interrupt movement on allocation failure at impact time. */
    target->entity->goalentity = NULL;
    target->entity->currentmove = &holdpos_move_stand;
    return true;
}
