#include "s_skills.h"
#include "../../jass/jass.h"

/* Sleep and its initial guard own distinct suspension contributions and
 * primary requests. Script Pause freezes both embedded clocks independently
 * of its execution/order availability counts. */
enum { SPELL_SLEEP_MAIN=1, SPELL_SLEEP_INITIAL=2 };
static edict_t *sleep_owner_heap[MAX_ENTITIES];
static uint32_t sleep_owner_positions[MAX_ENTITIES],sleep_owner_count;

static heroabilitystatus_t *sleep_owner_next(edict_t const *unit) {
    heroabilitystatus_t *best=NULL;
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *s=unit->abilstatus+i;
        if(!s->level || !s->sleep_kind || !s->sleep_timer.active)continue;
        abilityPrimaryTimer_t *a=&s->sleep_timer,*b=best ? &best->sleep_timer : NULL;
        if(!b || a->deadline.time<b->deadline.time ||
           (a->deadline.time==b->deadline.time && a->sequence<b->sequence))best=s;
    }
    return best;
}
static bool sleep_owner_less(edict_t const *a,edict_t const *b) {
    abilityPrimaryTimer_t *x=&sleep_owner_next(a)->sleep_timer,*y=&sleep_owner_next(b)->sleep_timer;
    return x->deadline.time==y->deadline.time ? x->sequence<y->sequence : x->deadline.time<y->deadline.time;
}
static void sleep_owner_put(uint32_t pos,edict_t *unit) {
    sleep_owner_heap[pos]=unit;sleep_owner_positions[unit-g_edicts]=pos+1;
}
void S_ForgetSleepOwner(edict_t *unit) {
    uint32_t pos=sleep_owner_positions[unit-g_edicts];
    if(!pos)return;
    sleep_owner_positions[unit-g_edicts]=0;pos--;
    edict_t *last=sleep_owner_heap[--sleep_owner_count];
    if(pos==sleep_owner_count)return;
    while(pos && sleep_owner_less(last,sleep_owner_heap[(pos-1)/2])) {
        sleep_owner_put(pos,sleep_owner_heap[(pos-1)/2]);pos=(pos-1)/2;
    }
    while(pos*2+1<sleep_owner_count) {
        uint32_t child=pos*2+1;
        if(child+1<sleep_owner_count && sleep_owner_less(sleep_owner_heap[child+1],sleep_owner_heap[child]))child++;
        if(!sleep_owner_less(sleep_owner_heap[child],last))break;
        sleep_owner_put(pos,sleep_owner_heap[child]);pos=child;
    }
    sleep_owner_put(pos,last);
}
static void sleep_owner_insert(edict_t *unit) {
    if(!unit || !unit->inuse)return;
    if(!sleep_owner_next(unit) || sleep_owner_positions[unit-g_edicts])return;
    uint32_t pos=sleep_owner_count++;
    while(pos && sleep_owner_less(unit,sleep_owner_heap[(pos-1)/2])) {
        sleep_owner_put(pos,sleep_owner_heap[(pos-1)/2]);pos=(pos-1)/2;
    }
    sleep_owner_put(pos,unit);
}
static void sleep_owner_schedule(heroabilitystatus_t *slot,wc3Clock_t now,float duration) {
    slot->sleep_timer=(abilityPrimaryTimer_t){.deadline=now,.sequence=++level.timer_sequence,.active=true};
    slot->sleep_timer.deadline.time=wc3_add(now.time,MAX(duration,G_ClockMinimumDelay()));
}
void S_SetSleepClockPaused(edict_t *unit,bool paused) {
    if(!unit || !unit->abilstatus)return;
    wc3Clock_t now=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
    S_ForgetSleepOwner(unit);
    FOR_LOOP(i,G_UnitStatusSlotCount(unit)) {
        heroabilitystatus_t *s=unit->abilstatus+i;
        if(!s->level || !s->sleep_kind || s->sleep_clock_paused==paused)continue;
        s->sleep_clock_paused=paused;
        if(paused) {
            s->sleep_remaining=s->sleep_timer.active ? MAX(0,wc3_elapsed(&s->sleep_timer.deadline,&now)) : 0;
            s->sleep_timer.active=false;s->timestamp=0;
        } else if(s->sleep_remaining>0) {
            sleep_owner_schedule(s,now,s->sleep_remaining);
            s->timestamp=G_Time()+(uint32_t)(s->sleep_remaining*1000);
            s->sleep_remaining=0;
        }
    }
    sleep_owner_insert(unit);
}
static void sleep_owner_remove(edict_t *unit,heroabilitystatus_t *slot) {
    unsigned kind=slot->sleep_kind;
    if(!kind)return;
    S_ForgetSleepOwner(unit);slot->sleep_kind=0;slot->sleep_timer.active=false;
    S_ReleaseInternalSuspension(unit);
    if(kind==SPELL_SLEEP_INITIAL) {
        bool before=G_UnitIsInvulnerable(unit);
        unit->invulnerability_depth=(int32_t)((uint32_t)unit->invulnerability_depth-1);
        if(before && !G_UnitIsInvulnerable(unit))S_UnitTargetAvailable(unit);
    }
    sleep_owner_insert(unit);
}
static heroabilitystatus_t *sleep_owner_apply(edict_t *caster,edict_t *target,uint32_t ability,
        uint32_t rank,unsigned kind,float duration) {
    heroabilitystatus_t *existing=unit_findstatusclass(target,kind==SPELL_SLEEP_MAIN ?
        MAKEFOURCC('B','U','s','l') : MAKEFOURCC('B','U','s','p'));
    if(existing && existing->sleep_kind==kind) {
        /* 531a80 compares the requested duration with the live embedded
         * timer, including a canceled timer while Script Pause is held.
         * Refresh keeps the status identity and its suspension contribution. */
        wc3Clock_t now=G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
        float remaining=existing->sleep_timer.active ?
            MAX(0,wc3_elapsed(&existing->sleep_timer.deadline,&now)) : 0;
        if(duration>remaining) {
            S_ForgetSleepOwner(target);
            sleep_owner_schedule(existing,now,duration);
            existing->timestamp=G_Time()+(uint32_t)(duration*1000);
            existing->duration_ms=(uint32_t)(duration*1000);
            sleep_owner_insert(target);
        }
        existing->source=caster;existing->source_spawn_time=caster->spawn_time;
        existing->source_ability=existing->data=ability;
        existing->rank=existing->level=rank;
        return existing;
    }
    status_application_t app={.buff=kind==SPELL_SLEEP_MAIN ? "BUsl" : "BUsp",.level=rank,
        .duration=duration,.source_ability=ability,.data=ability,.rank=rank,.source=caster,
        .buff_flags=WC3_STATUS_BUFF_NEGATIVE|WC3_STATUS_BUFF_MAGICAL};
    heroabilitystatus_t *slot=unit_applystatus(target,&app);
    if(!slot)return NULL;
    uint32_t const birth=target->spawn_time,instance=slot->instance_id;
    S_ForgetSleepOwner(target);slot->sleep_kind=kind;
    if(duration>0)sleep_owner_schedule(slot,G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL),duration);
    S_AcquireInternalSuspension(target);
    if(!target->inuse || target->spawn_time!=birth || !slot->level ||
       slot->instance_id!=instance || slot->sleep_kind!=kind)return NULL;
    if(kind==SPELL_SLEEP_INITIAL) {
        bool before=G_UnitIsInvulnerable(target);
        target->invulnerability_depth=(int32_t)((uint32_t)target->invulnerability_depth+1);
        if(!before && G_UnitIsInvulnerable(target))S_UnitTargetLost(target);
    }
    if(!target->inuse || target->spawn_time!=birth || !slot->level ||
       slot->instance_id!=instance || slot->sleep_kind!=kind)return NULL;
    sleep_owner_insert(target);
    if(target->paused)S_SetSleepClockPaused(target,true);
    return slot;
}
void S_SleepDamageWake(edict_t *unit) {
    if(unit_findstatusclass(unit,MAKEFOURCC('B','U','s','p')) || unit_findstatusclass(unit,MAKEFOURCC('B','U','s','t')))return;
    heroabilitystatus_t *slot=unit_findstatusclass(unit,MAKEFOURCC('B','U','s','l'));
    if(!slot)return;
    if(!slot->sleep_kind) {unit_removestatus(unit,slot,STATUS_REMOVE_SCRIPT);return;}
    S_ForgetSleepOwner(unit);
    /* 5008a0 directly requests34c(0): the embedded primary timer clamps to
     * 0.0001 seconds, and the caller's public post-damage query retains BUsl. */
    sleep_owner_schedule(slot,G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL),0);
    sleep_owner_insert(unit);
}
static void sleep_owner_fire(void) {
    edict_t *unit=sleep_owner_heap[0];
    heroabilitystatus_t *slot=sleep_owner_next(unit);
    S_ForgetSleepOwner(unit);slot->sleep_timer.active=false;
    unit_removestatus(unit,slot,STATUS_REMOVE_EXPIRE);
    sleep_owner_insert(unit);
}
BZ_ABILITY_PROC(CAbilitySleepOwner) {
    switch(msg) {
    case A_TIMERS_RESET:
        sleep_owner_count=0;memset(sleep_owner_positions,0,sizeof(sleep_owner_positions));return true;
    case A_TIMERS_REBUILD:
        sleep_owner_count=0;memset(sleep_owner_positions,0,sizeof(sleep_owner_positions));
        FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse)sleep_owner_insert(g_edicts+i);
        return true;
    case A_PRIMARY_TIMER_NEXT:
        if(!sleep_owner_count || !call || !call->primary_timer)return false;
        *call->primary_timer=(abilityTimerRequest_t){sleep_owner_next(sleep_owner_heap[0])->sleep_timer.deadline,
            sleep_owner_next(sleep_owner_heap[0])->sleep_timer.sequence,CAbilitySleepOwner};return true;
    case A_PRIMARY_TIMER_FIRE:
        if(sleep_owner_count)sleep_owner_fire();
        return true;
    case A_PRIMARY_TIMER_REBASE:
        FOR_LOOP(i,sleep_owner_count)FOR_LOOP(j,G_UnitStatusSlotCount(sleep_owner_heap[i])) {
            abilityPrimaryTimer_t *t=&sleep_owner_heap[i]->abilstatus[j].sleep_timer;
            if(t->active){t->deadline.time=wc3_sub(t->deadline.time,call->clock_span);t->deadline.epoch++;}
        }
        return true;
    case A_PRIMARY_TIMER:
        if(!level.scheduled_frame)while(sleep_owner_count) {
            wc3Clock_t now=G_TimerQueryClock(NULL);
            if(sleep_owner_next(sleep_owner_heap[0])->sleep_timer.deadline.time>now.time)break;
            sleep_owner_fire();
        }
        return true;
    default:return false;
    }
}
BZ_ABILITY_PROC(CAbilitySleep) {
    heroabilitystatus_t *slot=call ? call->status.slot : NULL;
    switch(msg) {
    case A_STATUS_POLICY:return UNIT_BUFF_KNOWN|UNIT_BUFF_NEGATIVE|UNIT_BUFF_MAGIC|UNIT_BUFF_AUTO_DISPEL;
    case A_STATUS_REMOVE:case A_STATUS_REPLACE:
        if(slot)sleep_owner_remove(ent,slot);
        return true;
    case A_STATUS_DEATH:
        if(slot)unit_expirestatus(ent,slot);
        return true;
    case A_EXECUTE: {
        if(!call || !call->item || !call->target || !call->target->entity)return false;
        edict_t *target=call->target->entity;uint32_t code=call->item->code,rank=S_SpellLevel(ent,code);
        if(!sleep_owner_apply(ent,target,code,rank,SPELL_SLEEP_MAIN,S_SpellHeroDuration(code,rank,target)))return false;
        sleep_owner_apply(ent,target,code,rank,SPELL_SLEEP_INITIAL,S_SpellData(code,rank,1));
        return true;
    }
    default:return CAbilitySimpleSpell(ent,msg,call);
    }
}
