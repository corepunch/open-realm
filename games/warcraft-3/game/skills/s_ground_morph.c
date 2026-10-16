#include "s_skills.h"

/* Burrow and Robo-Goblin use the common CAbilityMorph transition request.
 * Their source type remains bound until the request completes. */
static void ground_morph_animation_end(edict_t *unit) {
    if(!S_HoldMorphAnimation(unit))unit_stand(unit);
}
static umove_t ground_morph_forward={"morph",NULL,ground_morph_animation_end,CAbilityGroundMorph};
static umove_t ground_morph_reverse={"morph alternate",NULL,ground_morph_animation_end,CAbilityGroundMorph};

static bool ground_morph_is_burrow(abilityitem_t const *item) {
    return item && item->ability && !strcmp(item->ability->classname,"Abur");
}
static void ground_morph_category(edict_t *unit,uint32_t code,bool hidden) {
    if(!unit->morph) {
        if(!hidden)return;
        unit->morph=G_AllocMorph();assert(unit->morph);
        abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
        unit->morph->code=code;unit->morph->base_type=data->data[0].id;
        unit->morph->target_type=data->unitID;
    }
    /*4ba8d0 changes occupancy only: hide clears2, inverse sets2; stock
     * Burrow keeps the building-blocker bit8 and leaves its query untouched. */
    unit->morph->category_clear=hidden ? 2 : 0;
    unit->morph->category_set=hidden ? 8 : 10;
    gi.LinkEntity(unit);
}
static bool ground_morph_begin(edict_t *unit,abilityitem_t const *item,bool reverse) {
    uint32_t code=item->code;
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
    uint32_t base=data->data[0].id,target=reverse ? base : data->unitID;
    if(!base || !data->unitID || target==unit->class_id ||
       (unit->class_id!=base && unit->class_id!=data->unitID) ||
       (unit->morph && unit->morph->transition.active))return false;
    order_stop_cleanup(unit);
    unit_setmove(unit,reverse ? &ground_morph_reverse : &ground_morph_forward);
    unit->current_order_id=G_OrderId(item->ability->orders[reverse ? 1 : 0]);
    S_BeginMorphTransition(unit,code,base,target,reverse,true,data->dur);
    /* The collision inverse precedes the delayed reverse type rebind. */
    if(reverse && ground_morph_is_burrow(item))ground_morph_category(unit,code,false);
    return true;
}

BZ_ABILITY_PROC(CAbilityGroundMorph) {
    if(!call || !call->item || !call->item->ability)return false;
    uint32_t code=call->item->code;
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(ent,code));
    switch(msg) {
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_COMMAND,A_VALIDATE,A_DISABLE,A_UNIT_INIT,
            A_UNIT_TYPE_CHANGED,A_MOVE_LEAVE,A_TARGET_MODE_EXIT);
    case A_ORDER: {
        if(!ent || !call->order)return false;
        abilityAliasRef_t ref=S_ResolveAbilityAlias(ent,FS_SLKKey(call->item->ability->classname));
        if(!ref.alias)return false;
        abilityitem_t item=S_AbilityItem(ref.alias);
        abilityLevel_t const *row=G_AbilityLevel(ref.alias,S_SpellLevel(ent,ref.alias));
        bool reverse=!strcmp(call->order,item.ability->orders[1]);
        if(strcmp(call->order,item.ability->orders[reverse ? 1 : 0]) ||
           ent->class_id!=(reverse ? row->unitID : row->data[0].id))return false;
        return S_CastNoTargetSpell(ent,ref.alias);
    }
    case A_VALIDATE:
        return ent && data->data[0].id && data->unitID &&
            (ent->class_id==data->data[0].id || ent->class_id==data->unitID) &&
            !(ent->morph && ent->morph->transition.active);
    case A_EXECUTE:return ground_morph_begin(ent,call->item,ent->class_id==data->unitID);
    case A_TOGGLE_ON:return ent && ent->class_id==data->unitID;
    case A_UNIT_INIT:case A_UNIT_TYPE_CHANGED:
        if(ent && ground_morph_is_burrow(call->item)) {
            if(ent->class_id==data->unitID)ground_morph_category(ent,code,true);
            else if(ent->morph && ent->morph->code==code) {
                ent->morph->category_clear=0;ent->morph->category_set=0;gi.LinkEntity(ent);
            }
        }
        return true;
    case A_MORPH_COMPLETE:
        if(ground_morph_is_burrow(call->item)) {
            if(!call->enabled)ground_morph_category(ent,code,true);
            else {ent->morph->category_clear=0;ent->morph->category_set=0;gi.LinkEntity(ent);}
        }
        if(!call->enabled)S_ScheduleMorphExpiration(ent,data->heroDur);
        unit_stand(ent);return true;
    case A_MORPH_EXPIRE:return ground_morph_begin(ent,call->item,true);
    case A_DISABLE:
        S_RemoveMorphAbility(ent,code);gi.LinkEntity(ent);return true;
    default:return CAbilitySimpleSpell(ent,msg,call);
    }
}
