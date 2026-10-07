#include "s_skills.h"
#include "jass/jass.h"

/* UnitApplyTimedLife constructs independent buff objects, including repeated
 * applications of the same class. Records are address-stable, allocated only
 * for affected units, and indexed by the primary clock's deadline/serial.
 * Neither script timer capacity nor an all-entity frame scan limits them. */
typedef struct timedLife_s {
    struct timedLife_s *next;
    edict_t *unit;
    wc3Clock_t deadline;
    uint32_t code, sequence, heap_position;
    float remaining;
    bool finite, paused;
} timedLife_t;
typedef struct timedLifeBlock_s {
    struct timedLifeBlock_s *next;
    timedLife_t records[64];
} timedLifeBlock_t;
static timedLife_t *timed_life_heads[MAX_ENTITIES], *timed_life_free;
static timedLifeBlock_t *timed_life_blocks;
static timedLife_t **timed_life_heap;
static uint32_t timed_life_count, timed_life_capacity;

static timedLife_t **TimedLifeHead(edict_t const *unit) {
    uintptr_t index=((uintptr_t)unit-(uintptr_t)g_edicts)/sizeof(*unit);
    return g_edicts && unit && index<MAX_ENTITIES ? timed_life_heads+index : NULL;
}

static bool TimedLifeLess(timedLife_t const *a,timedLife_t const *b) {
    return a->deadline.time==b->deadline.time ? a->sequence<b->sequence : a->deadline.time<b->deadline.time;
}
static void TimedLifePut(uint32_t position,timedLife_t *life) {
    timed_life_heap[position]=life;life->heap_position=position+1;
}
static void TimedLifeRemoveRequest(timedLife_t *life) {
    if(!life->heap_position)return;
    uint32_t position=life->heap_position-1;life->heap_position=0;
    timedLife_t *last=timed_life_heap[--timed_life_count];
    if(position==timed_life_count)return;
    while(position && TimedLifeLess(last,timed_life_heap[(position-1)/2])) {
        TimedLifePut(position,timed_life_heap[(position-1)/2]);position=(position-1)/2;
    }
    while(position*2+1<timed_life_count) {
        uint32_t child=position*2+1;
        if(child+1<timed_life_count && TimedLifeLess(timed_life_heap[child+1],timed_life_heap[child]))child++;
        if(!TimedLifeLess(timed_life_heap[child],last))break;
        TimedLifePut(position,timed_life_heap[child]);position=child;
    }
    TimedLifePut(position,last);
}
static void TimedLifeInsertRequest(timedLife_t *life) {
    if(timed_life_count==timed_life_capacity) {
        uint32_t capacity=timed_life_capacity ? timed_life_capacity*2 : 64;
        timedLife_t **heap=gi.MemAlloc(capacity*sizeof(*heap));
        if(timed_life_count)memcpy(heap,timed_life_heap,timed_life_count*sizeof(*heap));
        gi.MemFree(timed_life_heap);timed_life_heap=heap;timed_life_capacity=capacity;
    }
    uint32_t position=timed_life_count++;
    while(position && TimedLifeLess(life,timed_life_heap[(position-1)/2])) {
        TimedLifePut(position,timed_life_heap[(position-1)/2]);position=(position-1)/2;
    }
    TimedLifePut(position,life);
}
static timedLife_t *TimedLifeAlloc(void) {
    if(!timed_life_free) {
        timedLifeBlock_t *block=gi.MemAlloc(sizeof(*block));
        block->next=timed_life_blocks;timed_life_blocks=block;
        FOR_LOOP(i,64){block->records[i].next=timed_life_free;timed_life_free=block->records+i;}
    }
    timedLife_t *life=timed_life_free;timed_life_free=life->next;
    memset(life,0,sizeof(*life));return life;
}
static wc3Clock_t TimedLifeClock(void) {
    return G_TimerQueryClock(level.vm ? jass_getcontext(level.vm) : NULL);
}
static uint32_t TimedLifeClass(uint32_t code) {
    /* The seven specialized factories are selected by class IDs, not authored
     * BuffData aliases. Every other input selects CBuffTimedLife (48b930). */
    switch(code) {
    case MAKEFOURCC('B','U','a','n'):case MAKEFOURCC('B','F','i','g'):
    case MAKEFOURCC('B','E','f','n'):case MAKEFOURCC('B','h','w','d'):
    case MAKEFOURCC('B','p','l','g'):case MAKEFOURCC('B','r','a','i'):
    case MAKEFOURCC('B','H','w','e'):return code;
    default:return MAKEFOURCC('B','T','L','F');
    }
}
uint32_t S_TimedLifeLevel(edict_t const *unit,uint32_t code) {
    timedLife_t **head=TimedLifeHead(unit);
    if(!head || !code)return 0;
    for(timedLife_t *life=*head;life;life=life->next)
        if(life->code==code)return 1;
    return 0;
}
bool S_UnitHasTimedLife(edict_t const *unit) {
    timedLife_t **head=TimedLifeHead(unit);
    return unit && ((head && *head) || G_QueryUnitStatusLevel(unit,MAKEFOURCC('B','T','L','F')));
}
void S_ReleaseTimedLives(edict_t *unit) {
    timedLife_t **head=TimedLifeHead(unit);
    if(!head)return;
    timedLife_t *life=*head;*head=NULL;
    while(life) {
        timedLife_t *next=life->next;TimedLifeRemoveRequest(life);
        life->next=timed_life_free;timed_life_free=life;life=next;
    }
}
void S_ClearTimedLives(void) {
    while(timed_life_blocks) {
        timedLifeBlock_t *next=timed_life_blocks->next;gi.MemFree(timed_life_blocks);timed_life_blocks=next;
    }
    memset(timed_life_heads,0,sizeof(timed_life_heads));timed_life_free=NULL;
    gi.MemFree(timed_life_heap);timed_life_heap=NULL;timed_life_count=timed_life_capacity=0;
}
static void TimedLifeKill(edict_t *unit) {
    if(M_IsDead(unit))return;
    G_SetHealth(unit,0);
    if(unit->die)unit->die(unit,unit->owner);
}
void S_ApplyTimedLife(edict_t *unit,uint32_t code,float duration) {
    if(!unit || !unit->inuse)return;
    G_BotTemporaryUnitReady(unit);
    timedLife_t *life=TimedLifeAlloc();life->unit=unit;life->code=TimedLifeClass(code);
    life->deadline=TimedLifeClock();life->finite=duration>0;
    if(life->finite) {
        life->deadline.time=wc3_add(life->deadline.time,MAX(duration,wc3_float(0x38d1b717)));
        life->sequence=++level.timer_sequence;TimedLifeInsertRequest(life);
    }
    life->next=timed_life_heads[unit-g_edicts];timed_life_heads[unit-g_edicts]=life;
    unit->aiflags|=AI_SUMMONED;
    if(life->code==MAKEFOURCC('B','U','a','n') || life->code==MAKEFOURCC('B','F','i','g') ||
       life->code==MAKEFOURCC('B','H','w','e'))unit->aiflags|=AI_CORPSE_NO_DECAY;
    G_SetUnitFoodUsed(unit,0);G_InvalidateUnitInfoPanel(unit);
}
bool S_RemoveTimedLife(edict_t *unit,uint32_t code) {
    if(!S_TimedLifeLevel(unit,code))return false;
    timedLife_t **link=timed_life_heads+(unit-g_edicts);
    while((*link)->code!=code)link=&(*link)->next;
    timedLife_t *life=*link;*link=life->next;TimedLifeRemoveRequest(life);
    life->next=timed_life_free;timed_life_free=life;
    TimedLifeKill(unit);return true;
}
void S_PauseTimedLife(edict_t *unit,bool paused) {
    if(!unit)return;
    wc3Clock_t now=TimedLifeClock();
    for(timedLife_t *life=timed_life_heads[unit-g_edicts];life;life=life->next) {
        if(!life->finite || life->paused==paused)continue;
        if(paused) {
            float due=life->deadline.time;
            if(now.epoch!=life->deadline.epoch)due=wc3_sub(due,now.span);
            life->remaining=wc3_sub(due,now.time);TimedLifeRemoveRequest(life);
        } else {
            life->deadline=now;
            life->deadline.time=wc3_add(now.time,MAX(life->remaining,wc3_float(0x38d1b717)));
            life->sequence=++level.timer_sequence;TimedLifeInsertRequest(life);
        }
        life->paused=paused;
    }
}
/* Save logical records in their per-unit encounter order. Heap positions,
 * pointers and free-list backing are rebuilt, never serialized. */
typedef struct {
    wc3Clock_t deadline;
    uint32_t code, sequence;
    float remaining;
    uint32_t finite, paused;
} timedLifeSave_t;
bool S_WriteTimedLives(FILE *file) {
    uint32_t owners=0;
    FOR_LOOP(i,globals.num_edicts)if(timed_life_heads[i])owners++;
    if(fwrite(&owners,sizeof(owners),1,file)!=1)return false;
    FOR_LOOP(i,globals.num_edicts)if(timed_life_heads[i]) {
        uint32_t count=0;for(timedLife_t *life=timed_life_heads[i];life;life=life->next)count++;
        if(fwrite(&i,sizeof(i),1,file)!=1 || fwrite(&count,sizeof(count),1,file)!=1)return false;
        for(timedLife_t *life=timed_life_heads[i];life;life=life->next) {
            timedLifeSave_t saved={life->deadline,life->code,life->sequence,life->remaining,life->finite,life->paused};
            if(fwrite(&saved,sizeof(saved),1,file)!=1)return false;
        }
    }
    return true;
}
bool S_ReadTimedLives(FILE *file) {
    uint32_t owners;
    if(fread(&owners,sizeof(owners),1,file)!=1 || owners>globals.num_edicts)return false;
    FOR_LOOP(i,owners) {
        uint32_t index,count;
        if(fread(&index,sizeof(index),1,file)!=1 || index>=globals.num_edicts || !g_edicts[index].inuse ||
           timed_life_heads[index] || fread(&count,sizeof(count),1,file)!=1 || !count)return false;
        timedLife_t **tail=timed_life_heads+index;
        FOR_LOOP(j,count) {
            timedLifeSave_t saved;
            if(fread(&saved,sizeof(saved),1,file)!=1 || saved.code!=TimedLifeClass(saved.code) ||
               saved.finite>1 || saved.paused>1 || saved.sequence>level.timer_sequence ||
               !isfinite(saved.deadline.time) || !isfinite(saved.deadline.span) || saved.deadline.span<=0 ||
               !isfinite(saved.remaining))return false;
            timedLife_t *life=TimedLifeAlloc();life->unit=g_edicts+index;life->deadline=saved.deadline;
            life->code=saved.code;life->sequence=saved.sequence;life->remaining=saved.remaining;
            life->finite=saved.finite;life->paused=saved.paused;*tail=life;tail=&life->next;
        }
    }
    return true;
}
BZ_ABILITY_PROC(CAbilityTimedLife) {
    switch(msg) {
    case A_UNIT_EVENT_MASK:return UNIT_MESSAGE_SUBSCRIPTIONS(A_DEATH,A_UNIT_REMOVING,A_UNIT_REMOVE);
    case A_DEATH:case A_UNIT_REMOVING:case A_UNIT_REMOVE:
        if(ent)S_ReleaseTimedLives(ent);
        return true;
    case A_TIMERS_RESET:
        timed_life_count=0;
        FOR_LOOP(i,globals.num_edicts)for(timedLife_t *life=timed_life_heads[i];life;life=life->next)life->heap_position=0;
        return true;
    case A_TIMERS_REBUILD:
        timed_life_count=0;
        FOR_LOOP(i,globals.num_edicts)for(timedLife_t *life=timed_life_heads[i];life;life=life->next) {
            life->heap_position=0;if(life->finite && !life->paused)TimedLifeInsertRequest(life);
        }
        return true;
    case A_PRIMARY_TIMER_NEXT:
        if(!timed_life_count)return false;
        call->primary_timer->deadline=timed_life_heap[0]->deadline;
        call->primary_timer->sequence=timed_life_heap[0]->sequence;return true;
    case A_PRIMARY_TIMER_FIRE: {
        if(!timed_life_count)return false;
        edict_t *unit=timed_life_heap[0]->unit;
        S_ReleaseTimedLives(unit);TimedLifeKill(unit);return true;
    }
    case A_PRIMARY_TIMER_REBASE:
        FOR_LOOP(i,timed_life_count) {
            timedLife_t *life=timed_life_heap[i];life->deadline.time=wc3_sub(life->deadline.time,call->clock_span);
            life->deadline.epoch++;
        }
        return true;
    default:return false;
    }
}
