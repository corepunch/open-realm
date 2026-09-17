#include "s_skills.h"

#define BZ_CYCLONE_BUFF MAKEFOURCC('B','c','y','c') // rawcode; primary Cyclone status; locks actions and targeting
#define BZ_CYCLONE_BUFF_EXTRA MAKEFOURCC('B','c','y','2') // rawcode; extra Cyclone status; locks actions and targeting

BOOL S_UnitIsCycloned(LPCEDICT unit) {
    return unit && (G_UnitStatusLevel(unit, BZ_CYCLONE_BUFF) || G_UnitStatusLevel(unit, BZ_CYCLONE_BUFF_EXTRA));
}

/* Validate via authored targs; empty BuffID falls back to Bcyc like Aams → Bams. DataA is dispel-only. */
BZ_ABILITY_PROC(CAbilityCyclone) {
    spellTarget_t const *target;
    DWORD level;
    LPCSTR buff;

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
    target->entity->goalentity = NULL;
    target->entity->currentmove = &holdpos_move_stand;
    return true;
}
