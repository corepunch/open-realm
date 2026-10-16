#include "s_skills.h"
#include "games/warcraft-3/common/wc3_math.h"

enum {STRIKE_RECOVER,STRIKE_DAMAGE,STRIKE_EXPIRE,STRIKE_TIMERS};
static edict_t *strike_heap[MAX_ENTITIES];
static uint32_t strike_positions[MAX_ENTITIES],strike_count;
static umove_t strike_missile_move={.animation="stand",.endfunc=S_ShadowStrikeProjectileHit,.proc=CAbilityShadowStrike};

/* Three independently ordered requests share one sparse owning-unit heap row. */
static abilityPrimaryTimer_t *strike_request(edict_t const *unit) {
    abilityPrimaryTimer_t *best=NULL;
    FOR_LOOP(i,STRIKE_TIMERS) {
        abilityPrimaryTimer_t *r=&unit->shadow_strike->timers[i];
        if(r->active && (!best || r->deadline.time<best->deadline.time ||
           (r->deadline.time==best->deadline.time && r->sequence<best->sequence)))best=r;
    }
    return best;
}
static bool strike_less(edict_t const *a,edict_t const *b) {
    abilityPrimaryTimer_t *x=strike_request(a),*y=strike_request(b);
    return x->deadline.time==y->deadline.time ? x->sequence<y->sequence : x->deadline.time<y->deadline.time;
}
static void strike_put(uint32_t pos,edict_t *unit) {strike_heap[pos]=unit;strike_positions[unit-g_edicts]=pos+1;}
void S_ForgetShadowStrike(edict_t *unit) {
    uint32_t pos=strike_positions[unit-g_edicts];
    if(!pos)return;
    strike_positions[unit-g_edicts]=0;pos--;
    edict_t *last=strike_heap[--strike_count];
    if(pos==strike_count)return;
    while(pos && strike_less(last,strike_heap[(pos-1)/2])) {strike_put(pos,strike_heap[(pos-1)/2]);pos=(pos-1)/2;}
    while(pos*2+1<strike_count) {
        uint32_t child=pos*2+1;
        if(child+1<strike_count && strike_less(strike_heap[child+1],strike_heap[child]))child++;
        if(!strike_less(strike_heap[child],last))break;
        strike_put(pos,strike_heap[child]);pos=child;
    }
    strike_put(pos,last);
}
static void strike_insert(edict_t *unit) {
    uint32_t pos=strike_count++;
    while(pos && strike_less(unit,strike_heap[(pos-1)/2])) {strike_put(pos,strike_heap[(pos-1)/2]);pos=(pos-1)/2;}
    strike_put(pos,unit);
}
static void strike_schedule(abilityPrimaryTimer_t *request,wc3Clock_t now,float delay) {
    *request=(abilityPrimaryTimer_t){.deadline=now,.sequence=++level.timer_sequence,.active=true};
    request->deadline.time=wc3_add(now.time,MAX(delay,G_ClockMinimumDelay()));
}

/*502600 preserves both duration-minus-remaining and elapsed-minus-duration stores. */
static float strike_factor(shadowStrike_t const *state,wc3Clock_t now) {
    if(fabsf(wc3_sub(state->duration,0))<wc3_float(0x3a83126f))return 0;
    float remaining=wc3_elapsed(&state->timers[STRIKE_EXPIRE].deadline,&now);
    float elapsed=wc3_sub(state->duration,remaining),base=fabsf(wc3_sub(elapsed,state->duration));
    float divisor,numerator;
    if(!wc3_pow(state->duration,state->exponent,&divisor) || !wc3_pow(base,state->exponent,&numerator))
        gi.error("Shadow Strike: duration power did not terminate");
    float result=wc3_div(numerator,divisor);
    return fabsf(wc3_sub(result,0))>=wc3_float(0x3a83126f) ? MAX(result,0) : 0;
}
static void strike_modifiers(edict_t *unit,heroabilitystatus_t *slot,shadowStrike_t const *state) {
    S_UpdateMoveSpeedContribution(unit,slot,-wc3_mul(state->move,state->factor));
    wc3_status_modifier_t attack={.type=WC3_STATUS_MOD_ATTACK_SPEED_PERCENT,.policy=WC3_STATUS_MOD_ADD,
        .value=-wc3_mul(state->attack,state->factor),.owner=WC3_STATUS_MOD_OWNER_AUTHORED};
    unit_status_setmodifier(slot,0,&attack);
}
static void strike_fire(edict_t *unit) {
    shadowStrike_t *state=unit->shadow_strike;
    abilityPrimaryTimer_t *request=strike_request(unit);
    unsigned kind=request-state->timers;
    uint32_t instance=state->status_id;
    heroabilitystatus_t *slot=NULL;
    FOR_LOOP(i,G_UnitStatusSlotCount(unit))if(unit->abilstatus[i].level && unit->abilstatus[i].instance_id==instance) {
        slot=unit->abilstatus+i;break;
    }
    wc3Clock_t now=request->deadline;
    S_ForgetShadowStrike(unit);
    if(!slot || slot->instance_id!=instance) {G_FreeShadowStrike(unit);return;}
    if(kind==STRIKE_EXPIRE) {unit_removestatus(unit,slot,STATUS_REMOVE_EXPIRE);return;}
    strike_schedule(request,now,kind==STRIKE_RECOVER ? 1 : state->interval);
    if(kind==STRIKE_RECOVER) {
        state->factor=strike_factor(state,now);strike_modifiers(unit,slot,state);
    } else {
        edict_t *source=slot->source;
        if(!source || !source->inuse || source->spawn_time!=slot->source_spawn_time)source=NULL;
        S_SpellDamage(unit,source,(int)wc3_int_bits(wc3_float_bits(state->damage)));
    }
    if(unit->inuse && unit->shadow_strike && unit->shadow_strike->status_id==instance &&
       !strike_positions[unit-g_edicts] && strike_request(unit))strike_insert(unit);
}

void S_ShadowStrikeProjectileHit(edict_t *missile) {
    edict_t *target=S_SpellProjectileTarget(missile),*caster=S_SpellProjectileOwner(missile);
    shadowStrike_t payload=*missile->shadow_strike;
    uint32_t code=missile->class_id;
    if(caster && S_SpellIsAliveTarget(target) && !S_UnitSpellImmune(target) &&
       !G_UnitIsInvulnerable(target) && !S_TryBlockSpellShield(caster,code,target)) {
        S_SpellDamage(target,caster,(int)wc3_int_bits(wc3_float_bits(payload.initial)));
        if(!M_IsDead(target)) {
            bool replacing=target->shadow_strike && target->shadow_strike->status_id;
            float old_delta=replacing ? -wc3_mul(target->shadow_strike->move,target->shadow_strike->factor) : 0;
            /* 500a60 refreshes Unit_FindAbilityByClass(BEsh), independently
             * of the authored display buff. Keep the same contribution owner. */
            if(replacing)FOR_LOOP(i,G_UnitStatusSlotCount(target)) {
                heroabilitystatus_t *prior=target->abilstatus+i;
                if(prior->level && prior->instance_id==target->shadow_strike->status_id) {
                    prior->code=payload.buff;break;
                }
            }
            char buff[5]={0};memcpy(buff,&payload.buff,4);
            heroabilitystatus_t *slot=S_SpellApplyTimedTargetStatus(target,code,payload.rank,buff,payload.duration);
            if(slot) {
                if(!target->shadow_strike)target->shadow_strike=G_AllocShadowStrike();
                shadowStrike_t *state=target->shadow_strike;S_ForgetShadowStrike(target);
                *state=payload;state->status_id=slot->instance_id;state->factor=1;
                slot->source=caster;slot->source_spawn_time=caster->spawn_time;slot->rank=payload.rank;
                if(replacing) {slot->move_speed_applied=1;slot->move_speed_delta=old_delta;}
                strike_modifiers(target,slot,state);
                wc3Clock_t now=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
                strike_schedule(&state->timers[STRIKE_RECOVER],now,1);
                strike_schedule(&state->timers[STRIKE_EXPIRE],now,payload.duration);
                if(payload.interval>0)strike_schedule(&state->timers[STRIKE_DAMAGE],now,payload.interval);
                strike_insert(target);
            }
        }
    }
    G_FreeEdict(missile);
}
static void strike_execute(edict_t *caster,spellTarget_t target,abilityitem_t const *spell) {
    uint32_t rank=S_SpellLevel(caster,spell->code);
    cstring_t speed=FindConfigValue(GetClassName(spell->code),"Missilespeed");
    float velocity=speed ? atof(speed) : 0;
    edict_t *missile=S_SpawnUnitTargetSpellMissile(caster,spell->code,target.entity,velocity>0 ? velocity : 1200,&strike_missile_move);
    if(!missile)return;
    cstring_t buff=S_SpellBuffId(spell->code,rank);
    uint32_t buff_code=MAKEFOURCC('B','E','s','h');
    if(buff && strlen(buff)>=4)memcpy(&buff_code,buff,4);
    missile->shadow_strike=G_AllocShadowStrike();
    *missile->shadow_strike=(shadowStrike_t){.rank=rank,.buff=buff_code,.duration=S_SpellResistantDuration(spell->code,rank,target.entity),
        .interval=S_SpellNumber(spell->code,ABILITY_NUMBER_CAST,rank),.damage=S_SpellData(spell->code,rank,1),
        .move=S_SpellData(spell->code,rank,2),.attack=S_SpellData(spell->code,rank,3),
        .exponent=S_SpellData(spell->code,rank,4),.initial=S_SpellData(spell->code,rank,5)};
}
BZ_ABILITY_PROC(CAbilityShadowStrike) {
    switch(msg) {
    case A_EXECUTE:
        if(!ent || !call || !call->item || !call->target)return false;
        strike_execute(ent,*call->target,call->item);return true;
    case A_CAST_PROFILE:
        if(!ent || !call || !call->item || !call->cast_profile)return false;
        S_DefaultSpellCastProfile(ent,call->item->code,call->cast_profile);
        call->cast_profile->channel_delay=0;return true; /* Cast is poison period, not cast delay. */
    case A_STATUS_POLICY:return UNIT_BUFF_KNOWN|UNIT_BUFF_NEGATIVE|UNIT_BUFF_MAGIC|UNIT_BUFF_AUTO_DISPEL;
    case A_STATUS_REPLACE:
        if(ent && ent->shadow_strike)S_ForgetShadowStrike(ent);
        return true;
    case A_STATUS_REMOVE:
        if(ent && call && call->status.slot && ent->shadow_strike &&
           ent->shadow_strike->status_id==call->status.slot->instance_id)G_FreeShadowStrike(ent);
        if(call)S_RemoveMoveSpeedContribution(ent,call->status.slot);
        return true;
    case A_STATUS_DEATH:
        if(ent && call && call->status.slot)unit_expirestatus(ent,call->status.slot);
        return true;
    case A_TIMERS_RESET:
        strike_count=0;memset(strike_positions,0,sizeof(strike_positions));return true;
    case A_TIMERS_REBUILD:
        strike_count=0;memset(strike_positions,0,sizeof(strike_positions));
        FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse && g_edicts[i].shadow_strike &&
            g_edicts[i].shadow_strike->status_id && strike_request(g_edicts+i))strike_insert(g_edicts+i);
        return true;
    case A_PRIMARY_TIMER_NEXT:
        if(!strike_count || !call || !call->primary_timer)return false;
        *call->primary_timer=(abilityTimerRequest_t){strike_request(strike_heap[0])->deadline,
            strike_request(strike_heap[0])->sequence,CAbilityShadowStrike};return true;
    case A_PRIMARY_TIMER_FIRE:
        if(strike_count)strike_fire(strike_heap[0]);
        return true;
    case A_PRIMARY_TIMER_REBASE:
        FOR_LOOP(i,strike_count)FOR_LOOP(k,STRIKE_TIMERS) {
            abilityPrimaryTimer_t *request=&strike_heap[i]->shadow_strike->timers[k];
            if(request->active) {request->deadline.time=wc3_sub(request->deadline.time,call->clock_span);request->deadline.epoch++;}
        }
        return true;
    case A_PRIMARY_TIMER:
        if(!level.scheduled_frame)while(strike_count) {
            wc3Clock_t now=G_TimerQueryClock(NULL);
            if(strike_request(strike_heap[0])->deadline.time>now.time)break;
            strike_fire(strike_heap[0]);
        }
        return true;
    default:return CAbilitySimpleSpell(ent,msg,call);
    }
}
