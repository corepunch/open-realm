#include "s_skills.h"

/* CAbilityMorph's d01c4 is a primary-clock owner, independent of MDX end
 * callbacks and of a pending Stop or scripted pause. */
static edict_t *morph_heap[MAX_ENTITIES];
static uint32_t morph_positions[MAX_ENTITIES], morph_count;
static bool morph_is_permanent(edict_t *unit,uint32_t code) {
    /*55d380 maps authored DataB bit8 to ability flag02000000. This is
     * independent of UnitMakeAbilityPermanent's attachment lifetime. */
    uint32_t flags=wc3_int_bits(wc3_float_bits(S_SpellData(code,S_SpellLevel(unit,code),2)));
    return (flags&8)!=0;
}
static abilityPrimaryTimer_t *morph_request(edict_t const *unit) {
    return unit->morph->transition.active ? &unit->morph->transition : &unit->morph->expiration;
}
static bool morph_timer_less(edict_t const *a, edict_t const *b) {
    abilityPrimaryTimer_t const *x=morph_request(a),*y=morph_request(b);
    return x->deadline.time==y->deadline.time ? x->sequence<y->sequence : x->deadline.time<y->deadline.time;
}
static void morph_timer_put(uint32_t position, edict_t *unit) {
    morph_heap[position]=unit;morph_positions[unit-g_edicts]=position+1;
}
static void morph_timer_remove(edict_t *unit) {
    if(unit->morph) {unit->morph->transition.active=false;unit->morph->expiration.active=false;}
    uint32_t position=morph_positions[unit-g_edicts];
    if(!position)return;
    morph_positions[unit-g_edicts]=0;position--;
    edict_t *last=morph_heap[--morph_count];
    if(position==morph_count)return;
    while(position && morph_timer_less(last,morph_heap[(position-1)/2])) {
        morph_timer_put(position,morph_heap[(position-1)/2]);position=(position-1)/2;
    }
    while(position*2+1<morph_count) {
        uint32_t child=position*2+1;
        if(child+1<morph_count && morph_timer_less(morph_heap[child+1],morph_heap[child]))child++;
        if(!morph_timer_less(morph_heap[child],last))break;
        morph_timer_put(position,morph_heap[child]);position=child;
    }
    morph_timer_put(position,last);
}
static void morph_timer_insert(edict_t *unit) {
    uint32_t position=morph_count++;
    while(position && morph_timer_less(unit,morph_heap[(position-1)/2])) {
        morph_timer_put(position,morph_heap[(position-1)/2]);position=(position-1)/2;
    }
    morph_timer_put(position,unit);
}

static void morph_transition_finish(edict_t *unit) {
    morph_t *state=unit->morph;
    uint32_t code=state->code,target=state->target_type;
    bool reverse=state->reverse,deferred=state->deferred_rebind;
    morph_timer_remove(unit);
    S_ReleaseUnitSeparationSuppression(unit);
    if(state->animation_held) {
        unit->aiflags&=~AI_HOLD_FRAME;state->animation_held=false;
    }
    if(deferred && !G_TransformUnitType(unit,target)) {unit_stand(unit);return;}
    abilityitem_t item=S_AbilityItem(code);
    abilityCall_t call={.item=&item,.enabled=reverse};
    S_AbilityMessage(unit,A_MORPH_COMPLETE,&call);
    if(morph_is_permanent(unit,code) && !unit->morph->expiration.active)
        G_ActorRemoveSkill(unit,code);
}

void S_BeginMorphTransition(edict_t *unit,uint32_t code,uint32_t base,uint32_t target,
                           bool reverse,bool deferred,float delay) {
    if(!unit->morph)unit->morph=G_AllocMorph();
    assert(unit->morph);
    morph_timer_remove(unit);
    wc3Clock_t now=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    *unit->morph=(morph_t){.code=code,.base_type=base,.target_type=target,.reverse=reverse,.deferred_rebind=deferred,
        .transition={.deadline=now,.sequence=++level.timer_sequence,.active=true}};
    /* Native d01c4 accepts literal zero: public return precedes delivery at
     * the same primary clock. It is not the generic JASS timer minimum. */
    unit->morph->transition.deadline.time=wc3_add(now.time,delay==0 ? delay : MAX(delay,G_ClockMinimumDelay()));
    S_AcquireUnitSeparationSuppression(unit);
    morph_timer_insert(unit);
}

void S_ScheduleMorphExpiration(edict_t *unit,float duration) {
    assert(unit && unit->morph && !unit->morph->transition.active);
    morph_timer_remove(unit);
    if(duration<=0)return;
    wc3Clock_t now=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    unit->morph->expiration=(abilityPrimaryTimer_t){.deadline=now,
        .sequence=++level.timer_sequence,.active=true};
    unit->morph->expiration.deadline.time=wc3_add(now.time,MAX(duration,G_ClockMinimumDelay()));
    morph_timer_insert(unit);
}

static void morph_timer_fire(edict_t *unit) {
    if(!unit->morph->expiration.active) {morph_transition_finish(unit);return;}
    abilityitem_t item=S_AbilityItem(unit->morph->code);
    morph_timer_remove(unit);
    abilityCall_t call={.item=&item};
    S_AbilityMessage(unit,A_MORPH_EXPIRE,&call);
}

/* Removal completes the live transition before its immediate reverse. Death
 * and unit retirement instead cancel the request and retain their count. */
void S_RemoveMorphAbility(edict_t *unit,uint32_t code) {
    if(!unit || M_IsDead(unit) || G_IsDeferredFree(unit))return;
    morph_t *state=unit->morph;
    if(morph_is_permanent(unit,code)) {
        /*540e30 cancels requests but skips both transition inverse and base
         * restoration for permanent forms. An early removal retains198. */
        bool active=state && state->code==code && state->transition.active;
        if(state && state->code==code)morph_timer_remove(unit);
        /* Height movement remains independent of the removed ability. Only
         * an active morph owns the current order; inactive removal keeps it. */
        if(active)unit_stand(unit);
        return;
    }
    uint32_t base=state && state->code==code ? state->base_type : G_AbilityLevel(code,S_SpellLevel(unit,code))->data[0].id;
    if(state && state->code==code && state->transition.active)morph_transition_finish(unit);
    if(state && state->code==code)morph_timer_remove(unit);
    if(state && state->code==code) {state->category_clear=0;state->category_set=0;}
    if(base && unit->class_id!=base)G_TransformUnitType(unit,base);
    if(unit->raven)unit->raven->rise_state=RAVEN_RISE_NONE;
}

/* A shorter model clip may finish before the authored transition. Hold its
 * final frame; the primary timer owns type/hold/order completion. */
bool S_HoldMorphAnimation(edict_t *unit) {
    if(!unit->morph || !unit->morph->transition.active)return false;
    unit->morph->animation_held=true;unit->aiflags|=AI_HOLD_FRAME;
    if(unit->animation && unit->animation->interval[1]>unit->animation->interval[0])
        unit->s.frame=unit->animation->interval[1]-1;
    return true;
}

BZ_ABILITY_PROC(CAbilityMorphTransition) {
    if(msg==A_UNIT_TYPE_UPDATE)return ent ? 0 : UNIT_UPDATE_POINTER(raven);
    switch(msg) {
    case A_UPDATE:S_UpdateMorphRise(ent);return true;
    case A_UNIT_EVENT_MASK:return UNIT_MESSAGE_SUBSCRIPTIONS(A_DEATH,A_UNIT_REMOVING,A_UNIT_REMOVE);
    case A_DEATH:case A_UNIT_REMOVING:case A_UNIT_REMOVE:
        if(ent)morph_timer_remove(ent); /* Native retirement retains the old contribution. */
        return true;
    case A_TIMERS_RESET:
        morph_count=0;memset(morph_positions,0,sizeof(morph_positions));return true;
    case A_TIMERS_REBUILD:
        morph_count=0;memset(morph_positions,0,sizeof(morph_positions));
        FOR_LOOP(i,globals.num_edicts) {
            edict_t *unit=g_edicts+i;
            if(unit->inuse && unit->morph && (unit->morph->transition.active || unit->morph->expiration.active))morph_timer_insert(unit);
        }
        return true;
    case A_PRIMARY_TIMER_NEXT:
        if(!morph_count || !call || !call->primary_timer)return false;
        *call->primary_timer=(abilityTimerRequest_t){morph_request(morph_heap[0])->deadline,
            morph_request(morph_heap[0])->sequence,CAbilityMorphTransition};return true;
    case A_PRIMARY_TIMER_FIRE:
        if(morph_count)morph_timer_fire(morph_heap[0]);
        return true;
    case A_PRIMARY_TIMER_REBASE:
        FOR_LOOP(i,morph_count) {
            abilityPrimaryTimer_t *request=morph_request(morph_heap[i]);
            request->deadline.time=wc3_sub(request->deadline.time,call->clock_span);request->deadline.epoch++;
        }
        return true;
    case A_PRIMARY_TIMER:
        if(!level.scheduled_frame)while(morph_count) {
            wc3Clock_t now=G_TimerQueryClock(NULL);
            if(morph_request(morph_heap[0])->deadline.time>now.time)break;
            morph_timer_fire(morph_heap[0]);
        }
        return true;
    default:return false;
    }
}
