/* Freeze-family buffs retain the same counted ability-disabling owner as Stun.
 * The concrete applying ability selects whether new orders are disabled. */
#include "s_skills.h"

heroabilitystatus_t *S_ApplyFreezeStatus(edict_t *caster,edict_t *target,uint32_t code,
        uint32_t rank,cstring_t buff,float duration,bool orders) {
    if(!caster || !target || !buff)return NULL;
    status_application_t app={.buff=buff,.level=rank,.duration=duration,.source_ability=code,
        .rank=rank,.data=code,.source=caster,
        .state_mask=WC3_STATUS_STATE_FROZEN|WC3_STATUS_STATE_STUNNED|WC3_STATUS_STATE_ROOTED,
        .buff_flags=WC3_STATUS_BUFF_NEGATIVE|WC3_STATUS_BUFF_MAGICAL};
    heroabilitystatus_t *slot=unit_applystatus(target,&app);
    if(!slot)return NULL;
    S_AcquireBuffAbilityDisables(target,orders);
    /* The original disable refresh retires the physical task before returning. */
    order_stop_cleanup(target);
    return slot;
}

static intptr_t freeze_status_message(edict_t *ent,abilityMsg_t msg,
        abilityCall_t const *call,bool orders) {
    heroabilitystatus_t *slot=call ? call->status.slot : NULL;
    switch(msg) {
    case A_STATUS_REMOVE:
    case A_STATUS_REPLACE:
        if(slot && (slot->state_mask&WC3_STATUS_STATE_FROZEN)) {
            slot->state_mask&=~WC3_STATUS_STATE_FROZEN;
            S_ReleaseBuffAbilityDisables(ent,orders);
        }
        return true;
    case A_STATUS_DEATH:
        if(slot)unit_expirestatus(ent,slot);
        return true;
    case A_STATUS_POLICY:
        return UNIT_BUFF_KNOWN|UNIT_BUFF_NEGATIVE|UNIT_BUFF_MAGIC|UNIT_BUFF_AUTO_DISPEL;
    default:return false;
    }
}

BZ_ABILITY_PROC(CAbilityFreezeDamageBonus) {
    if(msg==A_STATUS_REMOVE || msg==A_STATUS_REPLACE || msg==A_STATUS_DEATH || msg==A_STATUS_POLICY)
        return freeze_status_message(ent,msg,call,true);
    return CAbilityAttackBonus(ent,msg,call);
}

BZ_ABILITY_PROC(CAbilityFreezingBreath) {
    if(msg==A_STATUS_REMOVE || msg==A_STATUS_REPLACE || msg==A_STATUS_DEATH || msg==A_STATUS_POLICY)
        return freeze_status_message(ent,msg,call,false);
    return CAbilityPassive(ent,msg,call);
}

/* Afrz is a weapon-hit passive. Its authored area and target mask determine
 * which nearby structures receive the timed execution-only owner. */
void S_FreezingBreathOnHit(edict_t *attacker,edict_t *primary) {
    if(!attacker || !primary)return;
    abilityAliasRef_t ability=S_ResolveAbilityAlias(attacker,MAKEFOURCC('A','f','r','z'));
    if(!ability.alias || !ability.level || S_AbilityExecutionDisabled(attacker,ability.alias) ||
       !G_IsUnitAbilityAvailable(attacker,ability.alias) ||
       !G_UnitAbilityResearchAvailable(attacker,ability.alias) ||
       !G_AbilityRequirementsSatisfied(attacker,ability.alias))return;
    float radius=S_SpellNumber(ability.alias,ABILITY_NUMBER_AREA,ability.level);
    cstring_t buff=S_SpellBuffId(ability.alias,ability.level);
    if(!buff || strlen(buff)<4)buff="Bfrz";
    FILTER_EDICTS(target,S_SpellAllowsAreaTarget(ability.alias,attacker,target) &&
            (target==primary || Vector2_distance(&target->s.origin2,&primary->s.origin2)<=radius)) {
        S_ApplyFreezeStatus(attacker,target,ability.alias,ability.level,buff,
            S_SpellHeroDuration(ability.alias,ability.level,target),false);
    }
}
