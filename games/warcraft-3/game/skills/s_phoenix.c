#include "s_skills.h"

/* Phoenix365: 621990 vetoes death before679bb0 commits the dead flag. The
 * private order uses the common Morph clock, with concrete subtype counters. */
static void phoenix_animation_end(edict_t *unit) {
    if(!S_HoldMorphAnimation(unit))unit_stand(unit);
}
static umove_t phoenix_forward={"morph",NULL,phoenix_animation_end,CAbilityPhoenix};
static umove_t phoenix_reverse={"morph alternate",NULL,phoenix_animation_end,CAbilityPhoenix};

static bool phoenix_begin(edict_t *unit,abilityitem_t const *item,bool reverse) {
    abilityLevel_t const *data=G_AbilityLevel(item->code,S_SpellLevel(unit,item->code));
    uint32_t base=data->data[0].id,alternate=data->unitID;
    if(!base || !alternate || unit->class_id!=(reverse ? alternate : base) ||
       (unit->morph && unit->morph->transition.active))return false;
    order_stop_cleanup(unit);
    unit_setmove(unit,reverse ? &phoenix_reverse : &phoenix_forward);
    unit->current_order_id=G_OrderId("phoenixmorph");
    /*62e220 restores the base before5698a0 acquires198. Only the forward
     *62e240 path owns E8, while both paths own the15c/160 contribution. */
    if(!reverse)unit->invulnerability_depth=(int32_t)((uint32_t)unit->invulnerability_depth+1);
    unit->spell_immunity_depth=(int32_t)((uint32_t)unit->spell_immunity_depth+1);
    S_UnitTargetLost(unit);
    if(reverse && !G_TransformUnitType(unit,base)) {
        unit->spell_immunity_depth=(int32_t)((uint32_t)unit->spell_immunity_depth-1);
        S_UnitTargetAvailable(unit);return false;
    }
    float duration=reverse ? data->dur : wc3_add(data->dur,S_SpellData(item->code,S_SpellLevel(unit,item->code),3));
    S_BeginMorphTransition(unit,item->code,base,reverse ? base : alternate,reverse,!reverse,duration);
    unit->morph->death_intercepted=!reverse;
    return true;
}

bool S_PhoenixInterceptDeath(edict_t *unit) {
    if(!unit || !unit->inuse || (unit->svflags&SVF_DEADMONSTER) || G_IsDeferredFree(unit))return false;
    if(unit->morph && unit->morph->death_intercepted)return true;
    abilityAliasRef_t ref=S_ResolveAbilityAlias(unit,MAKEFOURCC('A','p','h','x'));
    if(!ref.alias || S_AbilityExecutionDisabled(unit,ref.alias))return false;
    abilityLevel_t const *data=G_AbilityLevel(ref.alias,S_SpellLevel(unit,ref.alias));
    if(!data->data[0].id || !data->unitID || unit->class_id!=data->data[0].id)return false;
    if(!unit->morph)unit->morph=G_AllocMorph();
    assert(unit->morph);
    unit->morph->death_intercepted=true;
    /* Keep validation and synchronous spell callbacks on the live identity,
     * even though the public life value has already reached zero. */
    G_SetHealth(unit,0);
    if(unit_issueimmediateorder(unit,"phoenixmorph"))return true;
    unit->morph->death_intercepted=false;
    return false;
}

BZ_ABILITY_PROC(CAbilityPhoenix) {
    if(!call || !call->item)return false;
    abilityLevel_t const *data=G_AbilityLevel(call->item->code,S_SpellLevel(ent,call->item->code));
    switch(msg) {
    case A_ORDER: {
        if(!ent || !call->order || strcmp(call->order,"phoenixmorph"))return false;
        abilityAliasRef_t ref=S_ResolveAbilityAlias(ent,MAKEFOURCC('A','p','h','x'));
        return ref.alias && S_CastNoTargetSpell(ent,ref.alias);
    }
    case A_EXECUTE:return phoenix_begin(ent,call->item,ent->class_id==data->unitID);
    case A_MORPH_EXPIRE:return phoenix_begin(ent,call->item,true);
    case A_MORPH_COMPLETE:
        /*62e140/62dfb0 heal the rebound form and relinquish only this owner. */
        G_SetHealth(ent,ent->health.max_value);
        if(!call->enabled)ent->invulnerability_depth=(int32_t)((uint32_t)ent->invulnerability_depth-1);
        ent->spell_immunity_depth=(int32_t)((uint32_t)ent->spell_immunity_depth-1);
        ent->morph->death_intercepted=false;
        S_UnitTargetAvailable(ent);
        if(!call->enabled)S_ScheduleMorphExpiration(ent,data->heroDur);
        unit_stand(ent);return true;
    default:return CAbilityGroundMorph(ent,msg,call);
    }
}
