#include "s_skills.h"

/* Hippogryph couple: Acoa/Acoh merge two living same-owner units into UnitID;
 * Adec splits the rider into DataA+DataB. Not Defend, not cargo, not Raven Form. */

static bool couple_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	edict_t *target = st.entity;
	uint32_t level, partner, rider;

	if (!caster || !spell || !target || target == caster) return false;
	if (!S_SpellIsAliveTarget(target) || caster->s.player != target->s.player) return false;
	level = S_SpellLevel(caster, spell->code);
	partner = S_SpellDataId(spell->code, level, 1);
	rider = S_SpellUnitId(spell->code, level);
	if (!partner || !rider || target->class_id != partner) return false;
	return true;
}

/* Spawn the authored rider first, then remove both inputs (Warsmash CoupleInstant). */
static bool couple_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell,edict_t *anchor) {
	edict_t *target = st.entity, *rider;
	uint32_t level, result;
	vec2_t origin;

	if (!caster || !spell || !target || !couple_validate(caster, st, spell)) return false;
	level = S_SpellLevel(caster, spell->code);
	result = S_SpellUnitId(spell->code, level);
	origin = anchor->s.origin2;
	rider = SP_SpawnAtLocationNoBirth(result, caster->s.player, &origin);
	if (!rider) {
		fprintf(stderr, "WC3_COUPLE: failed to spawn rider %.4s from %.4s\n",
		        (cstring_t)&result, (cstring_t)&spell->code);
		return false;
	}
	rider->s.angle = anchor->s.angle;
	G_ActivateUnitFood(rider);
	if (rider->stand) rider->stand(rider);
	G_FreeEdict(target);
	G_FreeEdict(caster);
    return true;
}

static bool decouple_validate(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	uint32_t level, a, b;

	(void)st;
	if (!caster || !spell || !S_SpellIsAliveTarget(caster)) return false;
	level = S_SpellLevel(caster, spell->code);
	a = S_SpellDataId(spell->code, level, 1);
	b = S_SpellDataId(spell->code, level, 2);
	return a && b;
}

static edict_t *decouple_spawn(edict_t *rider, uint32_t unit_id) {
	edict_t *ent;

	if (!rider || !unit_id) return NULL;
	ent = SP_SpawnAtLocationNoBirth(unit_id, rider->s.player, &rider->s.origin2);
	if (!ent) {
		fprintf(stderr, "WC3_COUPLE: failed to spawn companion %.4s on dismount\n", (cstring_t)&unit_id);
		return NULL;
	}
	ent->s.angle = rider->s.angle;
	G_ActivateUnitFood(ent);
	if (ent->stand) ent->stand(ent);
	return ent;
}

static void decouple_execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
	uint32_t level, a, b;
	edict_t *first, *second;

	(void)st;
	if (!caster || !spell || !decouple_validate(caster, st, spell)) return;
	level = S_SpellLevel(caster, spell->code);
	a = S_SpellDataId(spell->code, level, 1);
	b = S_SpellDataId(spell->code, level, 2);
	first = decouple_spawn(caster, a);
	second = decouple_spawn(caster, b);
	if (!first || !second) {
		if (first) G_FreeEdict(first);
		if (second) G_FreeEdict(second);
		return;
	}
	G_FreeEdict(caster);
}

/* Target-couple owns its reciprocal tasks. The unnamed852141 packet is an
 * internal target order, not the unrelated public coupletarget command. */
#define COUPLE_RECIPROCAL 852141u
static uint32_t couple_alias(edict_t const *unit,uint32_t base) {
    uint32_t count;unitAbilityToken_t const *tokens=G_UnitAbilityTokens(unit->data.UnitAbilities,&count);
    FOR_LOOP(i,count)if((tokens[i].code==base || tokens[i].base==base) && G_ActorHasAbilityCode(unit,tokens[i].code))return tokens[i].code;
    FOR_LOOP(i,ARRAY_COUNT(unit->abilities.added)) {
        uint32_t code=unit->abilities.added[i];
        if(code && (code==base || G_AbilityCode(code)==base) && G_ActorHasAbilityCode(unit,code))return code;
    }
    return 0;
}
static uint32_t couple_base(uint32_t code) {return G_AbilityCode(code);}
static abilityProc_t couple_proc(uint32_t code) {
    return couple_base(code)==MAKEFOURCC('A','c','o','h') ? CAbilityCoupleHippogryph : CAbilityCoupleArcher;
}
static cstring_t couple_order(uint32_t code) {
    return couple_base(code)==MAKEFOURCC('A','c','o','h') ? "loadarcher" : "mounthippogryph";
}
static uint32_t couple_order_base(cstring_t order) {
    if(order && !strcmp(order,"loadarcher"))return MAKEFOURCC('A','c','o','h');
    if(order && !strcmp(order,"mounthippogryph"))return MAKEFOURCC('A','c','o','a');
    return 0;
}
static bool couple_alive(edict_t const *unit,uint32_t spawn) {
    return unit && unit->inuse && unit->spawn_time==spawn && !G_IsDeferredFree(unit) && !M_IsDead(unit);
}
static edict_t *couple_partner(edict_t *unit) {
    couple_t *state=unit->couple;
    return state && couple_alive(state->partner,state->partner_spawn_time) ? state->partner : NULL;
}
static uint32_t couple_partner_ability(edict_t *unit,edict_t *partner,uint32_t code) {
    if(!G_ActorHasAbilityCode(unit,code) || !partner || partner==unit || !S_SpellIsAliveTarget(partner) || unit->s.player!=partner->s.player ||
       !S_UnitCanTranslate(partner) || partner->class_id!=S_SpellDataId(code,S_SpellLevel(unit,code),1) ||
       !S_SpellUnitId(code,S_SpellLevel(unit,code)))return 0;
    uint32_t bases[]={MAKEFOURCC('A','c','o','a'),MAKEFOURCC('A','c','o','h')};
    FOR_LOOP(i,2) {
        uint32_t other=couple_alias(partner,bases[i]);
        if(other && S_SpellDataId(other,S_SpellLevel(partner,other),1)==unit->class_id)return other;
    }
    return 0;
}
static void couple_think(edict_t *);
static void couple_leave(edict_t *);
static umove_t couple_archer_move={.animation="walk",.think=couple_think,.proc=CAbilityCoupleArcher,.leave=couple_leave,.sample_pose=S_PublishMovement};
static umove_t couple_hippo_move={.animation="walk",.think=couple_think,.proc=CAbilityCoupleHippogryph,.leave=couple_leave,.sample_pose=S_PublishMovement};
static bool couple_owns(edict_t const *unit,uint32_t spawn,uint32_t serial) {
    return couple_alive(unit,spawn) && unit->couple && unit->couple->active && unit->couple->serial==serial;
}
static bool couple_linked(edict_t *unit,edict_t *partner) {
    return unit->couple && partner && partner->couple && partner->couple->active &&
        partner->couple->serial==unit->couple->partner_serial && partner->couple->partner==unit &&
        partner->couple->partner_spawn_time==unit->spawn_time && partner->couple->partner_serial==unit->couple->serial;
}
static void couple_cancel(edict_t *unit,bool suspend) {
    couple_t *state=unit->couple;
    if(!state || !state->active)return;
    edict_t *partner=couple_partner(unit);bool linked=couple_linked(unit,partner);
    state->ready=false;state->suspended=suspend;state->active=suspend;
    S_EndUnitTargetChase(unit,couple_proc(state->ability));
    if(linked) {
        /* Unlink before Stop's synchronous events; a new pairing may reuse
         * either attachment while this invocation is still on the stack. */
        partner->couple->active=false;partner->couple->ready=false;
        S_EndUnitTargetChase(partner,couple_proc(partner->couple->ability));
        S_SetMoveGoal(partner,&partner->goalentity,NULL);
        S_IssueStopOrder(partner);
    }
}
static void couple_leave(edict_t *unit) {couple_cancel(unit,false);}
static void couple_finish(edict_t *unit) {
    uint32_t spawn=unit->spawn_time,revision=G_UnitMoveRevision(unit);
    couple_cancel(unit,false);
    if(couple_alive(unit,spawn) && G_UnitMoveRevision(unit)==revision) {
        S_SetMoveGoal(unit,&unit->goalentity,NULL);unit_stand(unit);
    }
}

void S_CoupleApproachComplete(edict_t *receiver,edict_t *unit,bool arrived) {
    if(receiver!=unit || !unit->couple || !unit->couple->active || unit->couple->suspended)return;
    if(!arrived && unit->movement.group_id)return;
    if(!arrived) {couple_finish(unit);return;}
    unit->couple->ready=true;
    edict_t *partner=couple_partner(unit);
    if(!partner) {couple_finish(unit);return;}
    if(!couple_linked(unit,partner) || !partner->couple->ready)return;
    uint32_t ability=unit->couple->ability;
    if(!couple_partner_ability(unit,partner,ability) ||
       !(S_UnitTargetInMoveRange(unit,partner,S_SpellRange(ability,S_SpellLevel(unit,ability))) || S_UnitTargetOnPerimeter(unit,partner))) {
        couple_finish(unit);return;
    }
    /*4c2f20 selects the Hippogryph anchor, independently of who arrived last. */
    edict_t *hippo=couple_base(ability)==MAKEFOURCC('A','c','o','h') ? unit : partner;
    abilityitem_t item=S_AbilityItem(ability);
    uint32_t spawn=unit->spawn_time,other_spawn=partner->spawn_time;
    unit->couple->active=partner->couple->active=false;
    S_EndUnitTargetChase(unit,couple_proc(ability));S_EndUnitTargetChase(partner,couple_proc(partner->couple->ability));
    if(!couple_execute(unit,(spellTarget_t){.type=SPELL_TARGET_UNIT,.entity=partner},&item,hippo)) {
        if(couple_alive(unit,spawn))unit_stand(unit);
        if(couple_alive(partner,other_spawn))unit_stand(partner);
    }
}
static void couple_think(edict_t *unit) {
    couple_t *state=unit->couple;
    if(!state || !state->active) {unit_stand(unit);return;}
    if(state->suspended || state->ready || S_UnitExecutionPaused(unit))return;
    abilityProc_t owner=couple_proc(state->ability);
    if(S_UnitTargetChaseActive(unit,owner))return;
    edict_t *partner=couple_partner(unit);
    uint32_t spawn=unit->spawn_time,serial=state->serial,revision=G_UnitMoveRevision(unit);
    if(!partner || !S_BeginUnitTargetChase(unit,partner,0,owner,S_CoupleApproachComplete)) {
        if(couple_owns(unit,spawn,serial) && G_UnitMoveRevision(unit)==revision) {
            couple_finish(unit);
        }
    }
}
static bool couple_install(edict_t *unit,edict_t *partner,uint32_t ability,bool reciprocal) {
    if(!unit->couple)unit->couple=G_AllocCouple();
    uint32_t serial=unit->couple->serial+1;
    *unit->couple=(couple_t){.partner=partner,.partner_spawn_time=partner->spawn_time,.ability=ability,
        .serial=serial,.active=true,.reciprocal=reciprocal};
    S_SetMoveGoal(unit,&unit->goalentity,partner);move_reset_progress(unit);
    unit_setmove(unit,couple_proc(ability)==CAbilityCoupleArcher ? &couple_archer_move : &couple_hippo_move);
    unit->current_order_id=reciprocal ? COUPLE_RECIPROCAL : G_OrderId(couple_order(ability));
    return true;
}
static void couple_start_pair(edict_t *unit,bool replay) {
    edict_t *partner=couple_partner(unit);
    uint32_t spawn=unit->spawn_time,serial=unit->couple->serial,revision=G_UnitMoveRevision(unit);
    uint32_t ability=unit->couple->ability;
    if(replay) {
        unit->couple->suspended=false;unit->couple->ready=false;
        G_PublishIssuedTargetOrderId(unit,unit->couple->reciprocal ? COUPLE_RECIPROCAL : G_OrderId(couple_order(ability)),
            unit->couple->reciprocal ? NULL : couple_order(ability),partner,unit->s.player);
        if(!couple_owns(unit,spawn,serial) || G_UnitMoveRevision(unit)!=revision)return;
    }
    if(!unit->couple->reciprocal && partner) {
        uint32_t other=couple_partner_ability(unit,partner,ability),partner_spawn=partner->spawn_time;
        if(other) {
            G_ClearUnitOrderQueue(partner);S_ClearUnitOrderReferences(partner);unit_stand_no_queue(partner);
            if(!couple_owns(unit,spawn,serial) || G_UnitMoveRevision(unit)!=revision || !couple_alive(partner,partner_spawn))return;
            uint32_t partner_revision=G_UnitMoveRevision(partner);
            partner->current_order_id=COUPLE_RECIPROCAL;
            G_PublishIssuedTargetOrderId(partner,COUPLE_RECIPROCAL,NULL,unit,unit->s.player);
            /* A reciprocal callback can replace only that actor. The original
             * initiator still owns its approach and eventual ready wait. */
            if(!couple_owns(unit,spawn,serial) || G_UnitMoveRevision(unit)!=revision)return;
            if(couple_alive(partner,partner_spawn) && G_UnitMoveRevision(partner)==partner_revision) {
                /* Issued-order callbacks may remove the reciprocal ability or
                 * change ownership without replacing the movement owner. */
                other=couple_partner_ability(unit,partner,ability);
                if(other) {
                    couple_install(partner,unit,other,true);
                    unit->couple->partner_serial=partner->couple->serial;partner->couple->partner_serial=serial;
                    couple_think(partner);
                } else {
                    unit_stand(partner);
                }
            }
        }
    }
    if(couple_owns(unit,spawn,serial) && G_UnitMoveRevision(unit)==revision)couple_think(unit);
}
static abilityOrderResult_t couple_admit(edict_t *unit,abilityCall_t const *call) {
    uint32_t base=call ? couple_order_base(call->issued_target_order.order) : 0;
    if(!base)return ABILITY_ORDER_UNHANDLED;
    uint32_t ability=couple_alias(unit,base);edict_t *partner=call->issued_target_order.target;
    return ability && !M_IsDead(unit) && !unit->paused && S_UnitCanTranslate(unit) &&
        G_IsUnitAbilityAvailable(unit,ability) && G_UnitAbilityResearchAvailable(unit,ability) &&
        couple_partner_ability(unit,partner,ability) ? ABILITY_ORDER_ACCEPTED : ABILITY_ORDER_REJECTED;
}
static queuedOrderResult_t couple_activate(edict_t *unit,unitOrder_t const *packet) {
    edict_t *partner=packet->target_number<globals.num_edicts ? g_edicts+packet->target_number : NULL;
    abilityCall_t call={.issued_target_order={.target=partner,.order=packet->order}};
    if(!couple_alive(partner,packet->target_spawn_time) || couple_admit(unit,&call)!=ABILITY_ORDER_ACCEPTED) {
        unit_stand(unit);return QUEUED_ORDER_REPLACED;
    }
    uint32_t ability=couple_alias(unit,couple_order_base(packet->order)),spawn=unit->spawn_time;
    couple_install(unit,partner,ability,false);
    uint32_t serial=unit->couple->serial,revision=G_UnitMoveRevision(unit);
    couple_start_pair(unit,false);
    return couple_owns(unit,spawn,serial) && G_UnitMoveRevision(unit)==revision ? QUEUED_ORDER_STARTED : QUEUED_ORDER_REPLACED;
}
static bool couple_select(edict_t *clent,edict_t *target) {
    edict_t *unit=G_GetMainSelectedUnit(clent->client);
    if(!unit || !G_UnitCanControl(clent->client,unit))return false;
    uint32_t code=S_SpellCurrentCode(clent,0),base=couple_base(code);
    if(base!=MAKEFOURCC('A','c','o','a') && base!=MAKEFOURCC('A','c','o','h'))return false;
    return G_IssueUnitTargetOrder(unit,couple_order(code),target,clent->client->menu.order_queued,clent->client->ps.number);
}
static intptr_t couple_message(edict_t *unit,abilityMsg_t msg,abilityCall_t const *call) {
    switch(msg) {
    case A_UNIT_EVENT_MASK:return UNIT_MESSAGE_SUBSCRIPTIONS(A_TARGET_ORDER_ADMIT,A_QUEUE_ORDER_START,A_MOVE_LEAVE,A_DEATH,A_UNIT_REMOVE,A_UNIT_REMOVING,A_UNIT_SUSPEND,A_UNIT_RESUME);
    case A_COMMAND:
        if(!call || !call->client)return false;
        UI_AddCancelButton(call->client);call->client->client->menu.on_entity_selected=couple_select;
        call->client->client->menu.supports_order_queue=true;return true;
    case A_TARGET_ORDER_ADMIT:return couple_admit(unit,call);
    case A_QUEUE_ORDER_START:
        return call && call->queued_order->target_type==UNIT_ORDER_TARGET_ENTITY && couple_order_base(call->queued_order->order) ?
            couple_activate(unit,call->queued_order) : QUEUED_ORDER_UNHANDLED;
    case A_MOVE_LEAVE:
        if(unit->couple && call && call->next_move_proc!=couple_proc(unit->couple->ability))couple_cancel(unit,false);
        return false;
    case A_UNIT_SUSPEND:couple_cancel(unit,true);return false;
    case A_ORDER_RESUME:
        if(unit->couple && unit->couple->active && unit->couple->suspended)couple_start_pair(unit,true);
        return true;
    case A_TARGET_REMOVED:
        if(unit->couple && unit->couple->active && call && call->removed_target==unit->couple->partner) {
            couple_finish(unit);
        }
        return false;
    case A_DEATH:case A_UNIT_REMOVING:case A_UNIT_REMOVE:couple_cancel(unit,false);return false;
    case A_VALIDATE:return call && call->target && couple_validate(unit,*call->target,call->item);
    case A_EXECUTE:
        if(!call || !call->target || !couple_validate(unit,*call->target,call->item))return false;
        return couple_execute(unit,*call->target,call->item,unit);
    default:return false;
    }
}
BZ_ABILITY_PROC(CAbilityCoupleArcher) {return couple_message(ent,msg,call);}
BZ_ABILITY_PROC(CAbilityCoupleHippogryph) {return couple_message(ent,msg,call);}

BZ_VALIDATED_SPELL_PROC(AbilityDecouple, decouple_validate, decouple_execute)
