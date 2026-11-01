#include "s_skills.h"

/* Acoi is the stock auto-selection/ordinary-spell-approach family. It does
 * not use the legacy Acoa/Acoh reciprocal ready latch. */
#define INSTANT_COUPLE_TARGET 852507u
#define INSTANT_COUPLE_AUTO 852508u

typedef struct { EVENTTYPE player,unit; } instantPhase_t;
static instantPhase_t const instant_channel={EVENT_PLAYER_UNIT_SPELL_CHANNEL,EVENT_UNIT_SPELL_CHANNEL};
static instantPhase_t const instant_cast={EVENT_PLAYER_UNIT_SPELL_CAST,EVENT_UNIT_SPELL_CAST};
static instantPhase_t const instant_effect={EVENT_PLAYER_UNIT_SPELL_EFFECT,EVENT_UNIT_SPELL_EFFECT};
static instantPhase_t const instant_endcast={EVENT_PLAYER_UNIT_SPELL_ENDCAST,EVENT_UNIT_SPELL_ENDCAST};
static instantPhase_t const instant_finish={EVENT_PLAYER_UNIT_SPELL_FINISH,EVENT_UNIT_SPELL_FINISH};

static bool instant_phase(edict_t *unit,coupleInstant_t const *state,instantPhase_t const *phase) {
    return S_DispatchOwnedSpellPhase(&(spellPhaseParams_t){.caster=unit,.code=state->ability,.target={.type=SPELL_TARGET_UNIT,.entity=state->partner},.player_event=phase->player,.unit_event=phase->unit},state->serial);
}

/* Pool teardown has no public spell callbacks, but still releases the retained
 * attachment independently of the unit's live availability registry. */
void S_ReleaseCoupleInstantState(edict_t *unit) {
    coupleInstant_t *state=unit->couple_instant;
    if(!state)return;
    S_ReleaseTargetReservation(&state->reservation);
    if(!state->channel_owner)return;
    S_ReleaseAbilityAvailability(state->channel_owner);
    state->channel_owner=NULL;
}
bool S_RestoreCoupleInstantState(edict_t *unit) {
    coupleInstant_t *state=unit->couple_instant;
    if(!state || !state->channel_open)return true;
    if(!state->active || !state->ability || !S_AbilityOrderBusy(unit,state->ability))return false;
    state->channel_owner=S_RetainAbilityAvailability(unit,state->ability,0);
    return state->channel_owner!=NULL;
}
static void instant_end_channel(edict_t *unit) {
    coupleInstant_t *state=unit->couple_instant;
    if(!state || !state->channel_open)return;
    coupleInstant_t packet=*state;
    state->channel_open=false;state->channel_owner=NULL;
    S_AdjustAbilityBusy(packet.channel_owner,-1);
    instant_phase(unit,&packet,&instant_endcast);
    S_ClearOwnedSpellEventTarget(unit,packet.ability,packet.serial);
    S_ReleaseAbilityAvailability(packet.channel_owner);
}

static bool instant_alive(edict_t const *unit,uint32_t spawn) {
    return unit && unit->inuse && unit->spawn_time==spawn && !G_IsDeferredFree(unit) && !M_IsDead(unit);
}
static uint32_t instant_alias(edict_t const *unit) {
    if(!unit || !unit->inuse)return 0;
    uint32_t count;unitAbilityToken_t const *tokens=G_UnitAbilityTokens(unit->data.UnitAbilities,&count);
    FOR_LOOP(i,count)if(tokens[i].base==MAKEFOURCC('A','c','o','i') && G_ActorHasAbilityCode(unit,tokens[i].code))return tokens[i].code;
    FOR_LOOP(i,ARRAY_COUNT(unit->abilities.added)) {
        uint32_t code=unit->abilities.added[i];
        if(code && G_AbilityCode(code)==MAKEFOURCC('A','c','o','i') && G_ActorHasAbilityCode(unit,code))return code;
    }
    return 0;
}
static bool instant_available(edict_t *unit,uint32_t code) {
    return unit && unit->inuse && code && !S_AbilityOrderBusy(unit,code) && !S_AbilityExecutionDisabled(unit,code) && G_ActorHasAbilityCode(unit,code) && G_UnitAbilityResearchAvailable(unit,code) &&
        S_SpellCanActivate(unit,code,S_SpellLevel(unit,code),false);
}
static bool instant_validate(edict_t *unit,edict_t *target,uint32_t code) {
    if(!instant_available(unit,code) || !target || target==unit || !S_SpellIsAliveTarget(target) ||
       target->s.player!=unit->s.player || !G_UnitHasWorldPresence(target) || !S_UnitCanTranslate(target) || !S_SpellAllowsTarget(code,unit,target))return false;
    uint32_t rank=S_SpellLevel(unit,code),other=instant_alias(target);
    if(unit->couple_instant && unit->couple_instant->active && unit->couple_instant->partner!=target)return false;
    if(target->couple_instant && target->couple_instant->active && target->couple_instant->partner!=unit)return false;
    uint32_t reserved=S_CountTargetReservations(target,code,unit->s.player);
    bool retained=unit->couple_instant && unit->couple_instant->active &&
        unit->couple_instant->partner==target && unit->couple_instant->partner_spawn_time==target->spawn_time;
    if(reserved>1 || (reserved && !retained))return false;
    return S_SpellUnitId(code,rank) && target->class_id==S_SpellDataId(code,rank,1) && other &&
        !S_AbilityExecutionDisabled(target,other) && G_UnitAbilityResearchAvailable(target,other) &&
        (S_UnitCanTranslate(unit) || S_SpellTargetInRange(unit,target,S_SpellRange(code,rank)));
}
typedef struct {
    edict_t *source,*result;
    uint32_t code;
    vec2_t center;
    float area,best_squared;
    bool best_reserved;
} instantCandidateQuery_t;

static void instant_visit_candidate(void *data,edict_t const *candidate) {
    instantCandidateQuery_t *query=data;
    edict_t *target=(edict_t *)candidate;
    if(!instant_validate(query->source,target,query->code))return;
    float dx=wc3_sub(target->s.origin2.x,query->center.x);
    float dy=wc3_sub(target->s.origin2.y,query->center.y);
    float squared=wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy));
    float reach=wc3_add(query->area,S_MoveCollisionRadius(target));
    if(squared>wc3_mul(reach,reach))return;
    /*402060 retains the later visited candidate at equal center distance.
     * The spatial query owns visitation order, independently of edict IDs. */
    bool reserved=S_HasTargetReservation(target,MAKEFOURCC('A','c','o','i'),query->source->s.player);
    if(!query->result || (query->best_reserved && !reserved) ||
       (query->best_reserved==reserved && squared<=query->best_squared)) {
        query->result=target;query->best_squared=squared;query->best_reserved=reserved;
    }
}
static edict_t *instant_candidate(edict_t *unit,uint32_t code) {
    if(!instant_available(unit,code))return NULL;
    instantCandidateQuery_t query={.source=unit,.code=code,.center=unit->s.origin2,
        .area=G_AbilityLevel(code,S_SpellLevel(unit,code))->area};
    box2_t bounds=CM_GetWorldBounds();
    float point[2]={wc3_grid_coordinate(query.center.x,bounds.min.x,32),
        wc3_grid_coordinate(query.center.y,bounds.min.y,32)};
    /* The captured Acoi query visits east before north, regardless of spawn
     * order. Use the Y-outer collector; its candidate bounds include radius,
     * while the exact circle above excludes source radius. */
    S_QueryMoveRangeCandidates(unit,point,wc3_div(query.area,32),instant_visit_candidate,&query);
    return query.result;
}
static void instant_cancel(edict_t *unit) {
    coupleInstant_t *state=unit->couple_instant;
    if(!state || !state->active || (state->constructing && !state->retiring && !state->channel_open))return;
    uint32_t spawn=unit->spawn_time,serial=state->serial;
    edict_t *partner=state->partner;uint32_t partner_spawn=state->partner_spawn_time;
    /* The indefinite wait cleanup retains its logical target through the
     * first peer inverse. Approach cleanup releases before that inverse.
     * Thus Stop of the waiting initiator can recurse A-H-A, while Stop of
     * the approaching peer produces A-H. */
    bool waited=state->waiting || state->channel_open;state->waiting=false;state->linked=false;
    if(!waited) {state->active=false;S_ReleaseTargetReservation(&state->reservation);}
    instant_end_channel(unit);
    if(!unit->inuse || unit->spawn_time!=spawn || !unit->couple_instant || unit->couple_instant->serial!=serial)return;
    if(instant_alive(partner,partner_spawn) && instant_alias(partner) && partner->couple_instant && partner->couple_instant->active &&
       partner->couple_instant->partner==unit && partner->couple_instant->partner_spawn_time==spawn)
        S_IssueOwnerStopOrder(partner);
    if(unit->inuse && unit->spawn_time==spawn && unit->couple_instant && unit->couple_instant->serial==serial) {
        S_ReleaseTargetReservation(&unit->couple_instant->reservation);
        unit->couple_instant->active=false;
    }
}

static umove_t instant_wait_move={.animation="stand",.proc=CAbilityCoupleInstant};

/* Only DataB zero enters the indefinite wait. The merging actor supplies
 * its own authored result and pose; originals retire world/life separately. */
static bool instant_complete(edict_t *unit,abilityCall_t const *call) {
    coupleInstant_t *state=unit->couple_instant;
    if(!state || !state->active || !call || !call->item || state->ability!=call->item->code)return false;
    uint32_t spawn=unit->spawn_time,serial=state->serial,code=state->ability,rank=S_SpellLevel(unit,code);
    edict_t *target=state->partner;uint32_t target_spawn=state->partner_spawn_time;
    if(!instant_alive(target,target_spawn)) {instant_cancel(unit);unit_stand(unit);return true;}
    /* 3fbb80 publishes CHANNEL before v384 acquires its signed +44
     * contribution. Retain the exact attachment across reentrant inverse and
     * detach; a trailing acquire belongs to that old owner, never a new alias. */
    state->channel_open=true;
    state->channel_owner=S_RetainAbilityAvailability(unit,code,0);
    unitAbilityDisable_t *owner=S_RetainAbilityAvailability(unit,code,0);
    coupleInstant_t packet=*state;
    bool current=instant_phase(unit,&packet,&instant_channel);
    S_AdjustAbilityBusy(owner,1);
    S_ReleaseAbilityAvailability(owner);
    if(!current || !instant_alive(unit,spawn) || !unit->couple_instant ||
       unit->couple_instant->serial!=serial || !unit->couple_instant->channel_open) {
        /* A CHANNEL callback can cancel before the trailing acquire. Native
         * phase-1 rejection then finishes the old spell with a cleared target. */
        if(instant_alive(unit,spawn) && unit->couple_instant && unit->couple_instant->serial==serial &&
           !unit->couple_instant->channel_open) {
            packet.partner=NULL;
            instant_phase(unit,&packet,&instant_finish);
        }
        return true;
    }
    if(!instant_phase(unit,&packet,&instant_cast))return true;
    if(!instant_alive(unit,spawn) || !unit->couple_instant || unit->couple_instant->serial!=serial ||
       !unit->couple_instant->channel_open || !instant_alive(target,target_spawn))return true;
    if(!instant_phase(unit,&packet,&instant_effect))return true;
    if(!instant_alive(unit,spawn) || !unit->couple_instant || unit->couple_instant->serial!=serial ||
       !unit->couple_instant->channel_open || !instant_alive(target,target_spawn))return true;
    state=unit->couple_instant;
    if(S_SpellData(code,rank,2)!=0) {
        vec2_t origin=unit->s.origin2;float angle=unit->s.angle;
        uint32_t result=S_SpellUnitId(code,rank),player=unit->s.player;
        if(!S_RetireCoupleInputWorld(unit) || !instant_alive(target,target_spawn))return true;
        if(!S_RetireCoupleInputWorld(target) || !instant_alive(unit,spawn))return true;
        edict_t *rider=SP_SpawnAtLocationNoBirth(result,player,&origin);
        if(!rider)return true; /* Native failed spawn leaves world-retired originals. */
        rider->s.angle=angle;G_ActivateUnitFood(rider);unit_stand(rider);
        if(instant_alive(unit,spawn))S_CompleteCoupleInputRetirement(unit);
        if(instant_alive(target,target_spawn))S_CompleteCoupleInputRetirement(target);
        return true;
    }
    state->waiting=true;state->constructing=true;
    unit_setmove(unit,&instant_wait_move);
    if(!instant_alive(unit,spawn))return true;
    state=unit->couple_instant;
    if(state && state->active && state->serial==serial) {
        state->constructing=false;unit->current_order_id=state->order_id;
    }
    return true;
}
static queuedOrderResult_t instant_activate(edict_t *unit,unitOrder_t const *packet) {
    uint32_t code=instant_alias(unit),spawn=unit->spawn_time;
    bool automatic=packet->order_id==INSTANT_COUPLE_AUTO;
    edict_t *target=automatic ? instant_candidate(unit,code) :
        packet->target_number<globals.num_edicts ? g_edicts+packet->target_number : NULL;
    if(!automatic && target && target->spawn_time!=packet->target_spawn_time)target=NULL;
    if(!target || !instant_validate(unit,target,code)) {unit_stand(unit);return QUEUED_ORDER_REPLACED;}
    uint32_t target_spawn=target->spawn_time;
    if(!unit->couple_instant)unit->couple_instant=G_AllocCoupleInstant();
    coupleInstant_t *state=unit->couple_instant;
    if(!state) {unit_stand(unit);return QUEUED_ORDER_REPLACED;}
    uint32_t serial=state->serial+1;
    *state=(coupleInstant_t){.partner=target,.partner_spawn_time=target_spawn,.ability=code,
        .order_id=packet->order_id,.serial=serial,.active=true,.constructing=true};
    bool accepted=S_IssueUnitTargetSpell(unit,code,target);
    if(!instant_alive(unit,spawn) || !unit->couple_instant || unit->couple_instant->serial!=serial)
        return QUEUED_ORDER_REPLACED;
    unit->couple_instant->constructing=false;
    if(!accepted) {unit->couple_instant->active=false;unit_stand(unit);return QUEUED_ORDER_REPLACED;}
    uint32_t revision=G_UnitMoveRevision(unit);
    if(automatic) {
        G_PublishIssuedTargetOrderId(unit,INSTANT_COUPLE_AUTO,"coupleinstant",target,packet->issuer_player);
        /*423d90 continues coordination after the synthetic notification even
         * when that callback installs a new source Move. Source/target lifetime
         * remains a separate guard from command ownership. */
        if(instant_alive(unit,spawn) && instant_alive(target,target_spawn) &&
           S_SpellData(code,S_SpellLevel(unit,code),2)==0)
            G_IssueUnitTargetOrder(target,"coupletarget",unit,false,packet->issuer_player);
    }
    return instant_alive(unit,spawn) && G_UnitMoveRevision(unit)==revision ? QUEUED_ORDER_STARTED : QUEUED_ORDER_REPLACED;
}
BZ_ABILITY_PROC(CAbilityCoupleInstant) {
    switch(msg) {
    case A_UNIT_EVENT_MASK:return UNIT_MESSAGE_SUBSCRIPTIONS(A_TARGET_ORDER_ADMIT,A_QUEUE_ORDER_START,A_MOVE_LEAVE,A_UNIT_REMOVE_PREPARE,A_COMMAND_OWNER_LEAVE,A_UNIT_SUSPEND_PREPARE,A_ORDER_RESUME,A_UNIT_SUSPEND,A_DEATH,A_UNIT_REMOVING,A_UNIT_REMOVE);
    case A_COMMAND: {
        if(!call || !call->client)return false;
        edict_t *unit=G_GetMainSelectedUnit(call->client->client);
        return unit && G_UnitCanControl(call->client->client,unit) && unit_issueimmediateorder(unit,"coupleinstant");
    }
    case A_IMMEDIATE_ORDER_ADMIT:
        if(!call || !call->order || strcmp(call->order,"coupleinstant"))return ABILITY_ORDER_UNHANDLED;
        return instant_candidate(ent,instant_alias(ent)) ? ABILITY_ORDER_ACCEPTED : ABILITY_ORDER_REJECTED;
    case A_TARGET_ORDER_ADMIT:
        if(!call || strcmp(call->issued_target_order.order,"coupletarget"))return ABILITY_ORDER_UNHANDLED;
        return instant_validate(ent,call->issued_target_order.target,instant_alias(ent)) ? ABILITY_ORDER_ACCEPTED : ABILITY_ORDER_REJECTED;
    case A_QUEUE_ORDER_START:
        if(!call || !call->queued_order ||
           (call->queued_order->order_id!=INSTANT_COUPLE_AUTO && call->queued_order->order_id!=INSTANT_COUPLE_TARGET))return QUEUED_ORDER_UNHANDLED;
        return instant_activate(ent,call->queued_order);
    case A_SPELL_TARGET_ACQUIRED: {
        coupleInstant_t *state=ent->couple_instant;
        if(!state || !state->active || !state->constructing || !call || !call->item || !call->target ||
           state->ability!=call->item->code || state->partner!=call->target->entity ||
           !instant_alive(state->partner,state->partner_spawn_time))return false;
        return S_AcquireTargetReservation(&state->reservation,&(targetReservationParams_t){.target=state->partner,.code=state->ability,.player=ent->s.player});
    }
    case A_SPELL_COMMAND_PROFILE:
        if(!call || !call->spell_command)return false;
        *call->spell_command=(spellCommandProfile_t){ent->couple_instant && ent->couple_instant->active ?
            ent->couple_instant->order_id : INSTANT_COUPLE_TARGET,SPELL_TARGET_UNIT};return true;
    case A_COMMAND_OWNER_LEAVE:instant_cancel(ent);return false;
    case A_MOVE_LEAVE:
        if(call && call->next_move_proc!=CAbilityCoupleInstant)instant_cancel(ent);
        return false;
    case A_UNIT_REMOVE_PREPARE: {
        coupleInstant_t *state=ent->couple_instant;
        if(!state || !state->active)return false;
        edict_t *peer=state->partner;
        uint32_t spawn=ent->spawn_time,peer_spawn=state->partner_spawn_time;
        bool waiting=state->waiting;
        if(waiting && instant_alive(peer,peer_spawn) && peer->couple_instant && peer->couple_instant->active &&
           peer->couple_instant->partner==ent && peer->couple_instant->partner_spawn_time==spawn) {
            uint32_t serial=peer->couple_instant->serial;
            instant_cancel(peer);
            /* Target-loss cleanup replays the retained target notification;
             * the absent target then rejects construction. A callback's new
             * owner must not be replaced by this old packet. */
            if(instant_alive(ent,spawn) && instant_alive(peer,peer_spawn) && peer->couple_instant &&
               peer->couple_instant->serial==serial && !peer->couple_instant->active &&
               peer->current_order_id==INSTANT_COUPLE_TARGET) {
                uint32_t revision=G_UnitMoveRevision(peer);
                G_PublishIssuedTargetOrderId(peer,INSTANT_COUPLE_TARGET,"coupletarget",ent,peer->s.player);
                if(instant_alive(peer,peer_spawn) && G_UnitMoveRevision(peer)==revision)
                    unit_stand(peer);
            }
        } else instant_cancel(ent);
        return true;
    }
    case A_UNIT_SUSPEND_PREPARE: {
        coupleInstant_t *state=ent->couple_instant;
        if(!state || !state->active)return false;
        uint32_t spawn=ent->spawn_time,serial=state->serial;
        bool resume=S_SpellData(state->ability,S_SpellLevel(ent,state->ability),2)!=0;
        instant_cancel(ent);
        if(instant_alive(ent,spawn) && ent->couple_instant && ent->couple_instant->serial==serial) {
            ent->couple_instant->resume=resume;
            if(!resume)ent->movement.pause_order_id=0;
        }
        return true;
    }
    case A_ORDER_RESUME: {
        coupleInstant_t *state=ent->couple_instant;
        if(!state || !state->resume)return false;
        state->resume=false;
        edict_t *partner=state->partner;
        if(instant_alive(partner,state->partner_spawn_time))
            G_IssueUnitTargetOrder(ent,"coupletarget",partner,false,ent->s.player);
        else unit_stand(ent);
        return true;
    }
    case A_DISABLE: {
        /* Detach first delivers A_UNIT_SUSPEND, which publishes the peer's
         * inverse Stop and releases the pairing. The indefinite wait has no
         * physical completion to retire its own head afterward. An approach
         * retains its existing terminal path, and a callback's new owner must
         * survive this final detach notification. */
        coupleInstant_t const *state=ent ? ent->couple_instant : NULL;
        if(state && call && call->item && state->ability==call->item->code &&
           !state->active && ent->currentmove==&instant_wait_move &&
           ent->current_order_id==state->order_id)
            unit_stand(ent);
        return false;
    }
    case A_DEATH: {
        /* d01a0 invokes the peer inverse before the dying actor's task is
         * retired. Ordinary v388 Stop cleanup instead ends its own cast first. */
        coupleInstant_t *state=ent->couple_instant;
        if(state && state->active) {
            uint32_t spawn=ent->spawn_time,serial=state->serial;
            edict_t *peer=state->partner;
            if(instant_alive(peer,state->partner_spawn_time) && peer->couple_instant &&
               peer->couple_instant->active && peer->couple_instant->partner==ent &&
               peer->couple_instant->partner_spawn_time==spawn)S_IssueOwnerStopOrder(peer);
            if(!ent->inuse || ent->spawn_time!=spawn || !ent->couple_instant ||
               ent->couple_instant->serial!=serial)return false;
        }
        instant_cancel(ent);return false;
    }
    case A_UNIT_SUSPEND:case A_UNIT_REMOVING:case A_UNIT_REMOVE:
        instant_cancel(ent);return false;
    case A_TARGET_REMOVED:
        if(call && ent->couple_instant && ent->couple_instant->active &&
           call->removed_target==ent->couple_instant->partner) {
            if(ent->couple_instant->retiring && call->removed_target->couple_instant &&
               call->removed_target->couple_instant->retiring)return false;
            uint32_t spawn=ent->spawn_time,revision=G_UnitMoveRevision(ent);
            instant_cancel(ent);
            if(instant_alive(ent,spawn) && G_UnitMoveRevision(ent)==revision)unit_stand(ent);
        }
        return false;
    case A_VALIDATE:return call && call->target && call->item && instant_validate(ent,call->target->entity,call->item->code);
    case A_SPELL_TARGET_COMPLETE:return instant_complete(ent,call);
    default:return false;
    }
}
