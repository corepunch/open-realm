#include "s_skills.h"

#define BZ_CYCLONE_BUFF MAKEFOURCC('B','c','y','c') // rawcode; primary Cyclone status; locks actions and targeting
#define BZ_CYCLONE_BUFF_EXTRA MAKEFOURCC('B','c','y','2') // rawcode; extra Cyclone status; locks actions and targeting

BOOL S_UnitIsCycloned(LPCEDICT unit) {
    return unit && (G_UnitStatusLevel(unit, BZ_CYCLONE_BUFF) || G_UnitStatusLevel(unit, BZ_CYCLONE_BUFF_EXTRA));
}

/* DataA==0 on the applying Cyclone rawcode (status.data) means Dispel/Purge/Devour must leave the buff. */
BOOL S_StatusIsUndispellable(heroabilitystatus_t const *status) {
    abilityitem_t item;
    if (!status || !status->level || !status->data) return false;
    item = S_AbilityItem(status->data);
    if (!item.ability || item.ability->proc != CAbilityCyclone) return false;
    return S_SpellData(status->data, status->level, 1) == 0.0f;
}

/* Validate via authored targs; empty BuffID falls back to Bcyc like Aams → Bams. DataA is dispel-only. */
BZ_ABILITY_PROC(CAbilityCyclone) {
    spellTarget_t const *target;
    DWORD level, buff_code;
    LPCSTR buff;
    heroabilitystatus_t *slot;

    if ((msg != A_VALIDATE && msg != A_EXECUTE) || !call || !call->item || !call->target)
        return CAbilitySimpleSpell(ent, msg, call);
    target = call->target;
    if (msg == A_VALIDATE)
        return target->type == SPELL_TARGET_UNIT && target->entity &&
               S_SpellAllowsTarget(call->item->code, ent, target->entity);
    if (target->type != SPELL_TARGET_UNIT || !target->entity) return false;
    level = S_SpellLevel(ent, call->item->code);
    buff = G_AbilityLevel(call->item->code, level)->buffID;
    if (!buff || strlen(buff) < 4) buff = "Bcyc";
    unit_addtimedstatus(target->entity, buff, level,
                        S_SpellDuration(call->item->code, level, G_UnitIsHero(target->entity)));
    /* unit_addtimedstatus zeroes data on replace; store applying rawcode after add like Purge. */
    buff_code = *((DWORD const *)buff);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        slot = target->entity->abilstatus + i;
        if (slot->level && slot->code == buff_code) { slot->data = call->item->code; break; }
    }
    target->entity->goalentity = NULL;
    target->entity->currentmove = &holdpos_move_stand;
    return true;
}
