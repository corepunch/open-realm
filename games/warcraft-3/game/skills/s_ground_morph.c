#include "s_skills.h"

/* Burrow and Robo-Goblin use the common CAbilityMorph transition request.
 * Their source type remains bound until the request completes. */
static void ground_morph_animation_end(edict_t *unit) {
    if(!S_HoldMorphAnimation(unit))unit_stand(unit);
}
static umove_t ground_morph_forward={"morph",NULL,ground_morph_animation_end,CAbilityGroundMorph};
static umove_t ground_morph_reverse={"morph alternate",NULL,ground_morph_animation_end,CAbilityGroundMorph};

/* Concrete RoboGoblin v41c is54c640. Burrow keeps its unconditional override. */
static bool ground_morph_is_configured(abilityitem_t const *item) {
    return item && item->ability && !strcmp(item->ability->classname,"ANrg");
}
static bool ground_morph_is_burrow(abilityitem_t const *item) {
    return item && item->ability && !strcmp(item->ability->classname,"Abur");
}
bool S_UnitIsBurrowed(edict_t const *unit) {
    return unit && unit->inuse && unit->morph && unit->morph->burrow_invisibility_depth>0;
}
static bool ground_morph_category(edict_t *unit,uint32_t code,bool hidden) {
    if(!unit->morph) {
        if(!hidden)return true;
        unit->morph=G_AllocMorph();assert(unit->morph);
        abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
        unit->morph->code=code;unit->morph->base_type=data->data[0].id;
        unit->morph->target_type=data->unitID;
    }
    /*4ba8d0 changes occupancy only: hide clears2, inverse sets2; stock
     * Burrow keeps the building-blocker bit8 and leaves its query untouched. */
    if(!S_SetBurrowInvisibilityContribution(unit,code,hidden))return false;
    unit->morph->category_clear=hidden ? 2 : 0;
    unit->morph->category_set=hidden ? 8 : 10;
    gi.LinkEntity(unit);
    return true;
}
static bool ground_morph_packet_live(edict_t *unit,uint64_t epoch,uint32_t birth,
                                     uint32_t type,uint64_t packet,uint32_t movement) {
    return epoch==S_SpellPreparationEpoch() && unit->inuse && unit->spawn_time==birth &&
        !G_IsDeferredFree(unit) && !M_IsDead(unit) && unit->class_id==type &&
        unit->active_order_revision==packet && G_UnitMoveRevision(unit)==movement;
}
static uint32_t ground_morph_order_alias(edict_t *unit,cstring_t order,bool executing) {
    if(!unit || !order)return 0;
    abilityAliasRef_t ref=S_ResolveAbilityAlias(unit,MAKEFOURCC('A','b','u','r'));
    if(!ref.alias)return 0;
    abilityitem_t item=S_AbilityItem(ref.alias);
    abilityLevel_t const *row=G_AbilityLevel(ref.alias,S_SpellLevel(unit,ref.alias));
    bool reverse=!strcmp(order,item.ability->orders[1]);
    if(strcmp(order,item.ability->orders[reverse?1:0]) ||
       unit->class_id!=(reverse?row->unitID:row->data[0].id) ||
       !G_UnitAbilityClassLevel(unit,ref.alias) || S_UnitOrderDisabled(unit,order) ||
       (unit->morph && unit->morph->transition.active))return 0;
    /* Admission retains a valid command through the common execution hold.
     * Its public activation and full spell validation occur only on release;
     * execution-disabled is not the same owner as order-disabled. */
    if(!executing && S_UnitOrderExecutionHeld(unit))return ref.alias;
    return S_ValidateNoTargetSpell(unit,ref.alias) ? ref.alias : 0;
}
static bool ground_morph_begin(edict_t *unit,abilityitem_t const *item,bool reverse,bool common) {
    uint32_t code=item->code;
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(unit,code));
    uint32_t base=data->data[0].id,target=reverse ? base : data->unitID;
    if(!base || !data->unitID || target==unit->class_id ||
       (unit->class_id!=base && unit->class_id!=data->unitID) ||
       (unit->morph && unit->morph->transition.active))return false;
    uint64_t const epoch=S_SpellPreparationEpoch(),packet=unit->active_order_revision;
    uint32_t const birth=unit->spawn_time,type=unit->class_id,head=unit->current_order_id;
    uint32_t movement=G_UnitMoveRevision(unit);
    if(common) {
        /* Physical cancellation may publish callbacks. Preserve the admitted
         * user packet/FIFO, and never continue over callback replacement. */
        if(G_UnitIsStructure(unit))S_StopUnitMovement(unit);
        else S_StopUnitMovementWithRecovery(unit);
        if(!ground_morph_packet_live(unit,epoch,birth,type,packet,movement))return true;
        S_ClearUnitOrderReferences(unit);
        if(!ground_morph_packet_live(unit,epoch,birth,type,packet,movement))return true;
    } else order_stop_cleanup(unit);
    umove_t *move=reverse ? &ground_morph_reverse : &ground_morph_forward;
    unit_setmove(unit,move);
    if(common) {
        if(epoch!=S_SpellPreparationEpoch() || !unit->inuse || unit->spawn_time!=birth ||
           G_IsDeferredFree(unit) || M_IsDead(unit) || unit->class_id!=type ||
           unit->active_order_revision!=packet || unit->currentmove!=move)return true;
        movement=G_UnitMoveRevision(unit);
        unit->current_order_id=head;
    } else unit->current_order_id=data->dur==0 ? 0 : G_OrderId(item->ability->orders[reverse ? 1 : 0]);
    /*4c85b0 removes114 and republishes occupancy before the reverse hold. */
    bool burrow_reverse=reverse && ground_morph_is_burrow(item);
    if(reverse && !S_NotifyMorphReverse(unit))return true;
    if(common && !ground_morph_packet_live(unit,epoch,birth,type,packet,movement))return true;
    if(burrow_reverse && !ground_morph_category(unit,code,false))return true;
    if(common && !ground_morph_packet_live(unit,epoch,birth,type,packet,movement))return true;
    S_BeginMorphTransition(unit,code,base,target,reverse,true,data->dur);
    if(burrow_reverse) {unit->morph->category_clear=0;unit->morph->category_set=10;}
    return true;
}

BZ_ABILITY_PROC(CAbilityGroundMorph) {
    if(!call || !call->item || !call->item->ability)return false;
    uint32_t code=call->item->code;
    abilityLevel_t const *data=G_AbilityLevel(code,S_SpellLevel(ent,code));
    switch(msg) {
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_COMMAND,A_VALIDATE,A_DISABLE,A_UNIT_INIT,
            A_UNIT_TYPE_CHANGED,A_ENABLE,A_MOVE_LEAVE,A_TARGET_MODE_EXIT,A_QUEUE_ORDER_START);
    case A_IMMEDIATE_ORDER_ADMIT:
        if(!ground_morph_is_burrow(call->item))return ABILITY_ORDER_UNHANDLED;
        return ground_morph_order_alias(ent,call->order,false) ? ABILITY_ORDER_ACCEPTED : ABILITY_ORDER_REJECTED;
    case A_QUEUE_ORDER_START: {
        if(!ground_morph_is_burrow(call->item) || !call->queued_order)return QUEUED_ORDER_UNHANDLED;
        if(strcmp(call->queued_order->order,"burrow") && strcmp(call->queued_order->order,"unburrow"))
            return QUEUED_ORDER_UNHANDLED;
        unitOrder_t const packet=*call->queued_order;
        uint64_t const epoch=S_SpellPreparationEpoch(),revision=ent->active_order_revision;
        uint32_t const birth=ent->spawn_time;
        /* The issued callback may already have replaced this packet. */
        if(!ent->active_order || memcmp(ent->active_order,&packet,sizeof(packet)) ||
           ent->current_order_id!=packet.order_id)return QUEUED_ORDER_REPLACED;
        uint32_t alias=ground_morph_order_alias(ent,packet.order,true);
        bool accepted=alias && S_CastNoTargetSpell(ent,alias);
        if(accepted)return QUEUED_ORDER_STARTED;
        /* Handled rejection cannot fall through to the opposite toggle.
         * Retire only the admitted packet, never a callback's successor. */
        if(epoch==S_SpellPreparationEpoch() && ent->inuse && ent->spawn_time==birth &&
           !G_IsDeferredFree(ent) && ent->active_order_revision==revision &&
           ent->active_order && !memcmp(ent->active_order,&packet,sizeof(packet)) &&
           ent->current_order_id==packet.order_id) {
            ent->current_order_id=0;
            G_ClearActiveUnitOrder(ent);
        }
        return QUEUED_ORDER_REPLACED;
    }
    case A_SPELL_COMMAND_PROFILE:
        if(!ground_morph_is_burrow(call->item) || !call->spell_command)return false;
        *call->spell_command=(spellCommandProfile_t){
            G_OrderId(call->item->ability->orders[ent->class_id==data->unitID ? 1 : 0]),SPELL_TARGET_NONE};
        return true;
    case A_CAST_PROFILE:
        if(!ground_morph_is_burrow(call->item) || !call->cast_profile)return false;
        S_DefaultSpellCastProfile(ent,code,call->cast_profile);
        call->cast_profile->channel_delay=0;call->cast_profile->cast_point=0;
        call->cast_profile->backswing=0;call->cast_profile->morph_completion=true;
        return true;
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
    case A_EXECUTE:return ground_morph_begin(ent,call->item,ent->class_id==data->unitID,
        ground_morph_is_burrow(call->item) && call->ordered_cast);
    case A_TOGGLE_ON:return ent && ent->class_id==data->unitID;
    case A_ENABLE:case A_UNIT_INIT:case A_UNIT_TYPE_CHANGED:
        /*55d380's initial-alternate fast path has no transition hold, but
         *552120 still owns the authored lifetime. An existing owner identifies
         *type refresh, so its request and serial are retained. */
        bool initialize=ent && ent->class_id==data->unitID && (!ent->morph || ent->morph->code!=code);
        if(ent && ground_morph_is_burrow(call->item))
            initialize=S_InitializeBurrowAbility(ent,code) && ent->class_id==data->unitID;
        if(ground_morph_is_configured(call->item))S_InitializeConfiguredMorphAvailability(ent,call);
        if(initialize) {
            if(!ent->morph)ent->morph=G_AllocMorph();
            assert(ent->morph);
            ent->morph->code=code;ent->morph->base_type=data->data[0].id;ent->morph->target_type=data->unitID;
            S_ScheduleMorphExpiration(ent,data->heroDur);
        }
        if(ent && ground_morph_is_burrow(call->item)) {
            if(initialize) {S_SetMorphAbilityDisables(ent,code,true);if(!ground_morph_category(ent,code,true))return true;}
            else if(ent->class_id!=data->unitID && ent->morph && ent->morph->code==code) {
                ent->morph->category_clear=0;ent->morph->category_set=0;gi.LinkEntity(ent);
            }
        }
        return true;
    case A_MORPH_COMPLETE:
        if(ground_morph_is_configured(call->item))S_CompleteConfiguredMorphAvailability(ent,call);
        if(ground_morph_is_burrow(call->item)) {
            if(!call->enabled) {S_SetMorphAbilityDisables(ent,code,true);if(!ground_morph_category(ent,code,true))return true;}
            else S_SetMorphAbilityDisables(ent,code,false);
            if(call->enabled) {ent->morph->category_clear=0;ent->morph->category_set=0;gi.LinkEntity(ent);}
        }
        if(!call->enabled)S_ScheduleMorphExpiration(ent,data->heroDur);
        if(!S_MorphSpellOwnsCompletion(ent,code))unit_stand(ent);
        return true;
    case A_MORPH_EXPIRE:return ground_morph_begin(ent,call->item,true,false);
    case A_DISABLE:
        if(ground_morph_is_configured(call->item)) {
            S_DetachConfiguredMorphAvailability(ent,call);gi.LinkEntity(ent);return true;
        }
        /* Common detach finishes its pending form before subtype inverse. */
        if(ground_morph_is_burrow(call->item))S_CompleteMorphDetachTransition(ent,code);
        {
            bool release=ground_morph_is_burrow(call->item) && ent && ent->class_id==data->unitID;
            /*540e30 skips the reverse for permanent forms. */
            bool permanent=(wc3_int_bits(wc3_float_bits(S_SpellData(code,S_SpellLevel(ent,code),2)))&8)!=0;
            if(ent && ent->class_id==data->unitID && !permanent && !S_NotifyMorphReverse(ent))return true;
            if(release && ent->morph && !ground_morph_category(ent,code,false))return true;
            /* Type rebind refreshes retained invisibility. Keep the passive
             * suspended through base restoration, then restart its fade. */
            S_RemoveMorphAbility(ent,code);
            if(release)S_SetMorphAbilityDisables(ent,code,false);
        }
        gi.LinkEntity(ent);return true;
    default:return CAbilitySimpleSpell(ent,msg,call);
    }
}

/* Corporeal/Ethereal Form share CAbilityMorph's deferred rebind and counted
 * transition. Their separate Unit158 contribution changes only at completion. */
static intptr_t ethereal_form_message(edict_t *ent,abilityMsg_t msg,abilityCall_t const *call,bool corporeal) {
    if(!call || !call->item)return false;
    switch(msg) {
    case A_UNIT_EVENT_MASK:return UNIT_MESSAGE_SUBSCRIPTIONS(A_COMMAND,A_VALIDATE,A_DISABLE,A_UNIT_INIT,
        A_MOVE_LEAVE,A_TARGET_MODE_EXIT);
    case A_ENABLE:case A_UNIT_INIT:
        if(ent && S_InitializeEtherealAbility(ent,call->item->code,corporeal)) {
            abilityLevel_t const *data=G_AbilityLevel(call->item->code,S_SpellLevel(ent,call->item->code));
            /*55d380 initial-alternate branch applies subtype40c synchronously,
             * without a separation transition. */
            if(ent->class_id==data->unitID) {
                S_SetEtherealAbility(ent,call->item->code,!corporeal);
                CAbilityGroundMorph(ent,A_UNIT_INIT,call);
            }
        }
        return true;
    case A_MORPH_COMPLETE:
        if(G_ActorHasAbilityCode(ent,call->item->code))
            S_SetEtherealAbility(ent,call->item->code,corporeal==call->enabled);
        /*552120 applies the shared authored form lifetime after subclass40c. */
        return CAbilityGroundMorph(ent,msg,call);
    case A_DISABLE:
        if(ent)S_SetEtherealAbility(ent,call->item->code,false);
        return CAbilityGroundMorph(ent,msg,call);
    default:return CAbilityGroundMorph(ent,msg,call);
    }
}
BZ_ABILITY_PROC(CAbilityCorporealForm) {return ethereal_form_message(ent,msg,call,true);}
BZ_ABILITY_PROC(CAbilityEtherealForm) {return ethereal_form_message(ent,msg,call,false);}
