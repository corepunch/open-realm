#include "g_local.h"
#include "jass/jass.h"

/* Canonical range regions poll independently of entity movement. Authoritative
 * rows keep native encounter order; the heap and stamped lookup are derived. */
typedef struct { uint32_t slot,spawn,flags; } rangeOccupant_t;
typedef struct { wc3Clock_t deadline;uint32_t sequence,index; } rangeRequest_t;
typedef struct {
    rangeRequest_t poll,release;
    rangeOccupant_t *rows;
    uint32_t count,capacity;
    uint8_t release_stage;
    bool active;
} rangeListener_t;
static rangeListener_t range_listeners[MAX_EVENTS];
static uint32_t range_heap[MAX_EVENTS*2],range_count;
static uint32_t range_stamps[MAX_ENTITIES],range_old_rows[MAX_ENTITIES],range_stamp;

static float RangePeriod(void) {return .125f;}
static rangeRequest_t *RangeRequest(uint32_t key) {
    rangeListener_t *listener=range_listeners+key/2;
    return key&1 ? &listener->release : &listener->poll;
}
static bool RangeLess(uint32_t a,uint32_t b) {
    rangeRequest_t const *x=RangeRequest(a),*y=RangeRequest(b);
    return x->deadline.time==y->deadline.time ? x->sequence<y->sequence : x->deadline.time<y->deadline.time;
}
static void RangePut(uint32_t index,uint32_t key) {
    range_heap[index]=key;RangeRequest(key)->index=index+1;
}
static void RangeDown(uint32_t index,uint32_t key) {
    while(index*2+1<range_count) {
        uint32_t child=index*2+1;
        if(child+1<range_count && RangeLess(range_heap[child+1],range_heap[child]))child++;
        if(RangeLess(key,range_heap[child]))break;
        RangePut(index,range_heap[child]);index=child;
    }
    RangePut(index,key);
}
static void RangeRemove(uint32_t key) {
    rangeRequest_t *request=RangeRequest(key);
    if(!request->index)return;
    uint32_t index=request->index-1,last=range_heap[--range_count];request->index=0;
    if(index==range_count)return;
    while(index && RangeLess(last,range_heap[(index-1)/2])) {
        RangePut(index,range_heap[(index-1)/2]);index=(index-1)/2;
    }
    RangeDown(index,last);
}
static void RangeInsert(uint32_t key) {
    uint32_t index=range_count++;
    while(index && RangeLess(key,range_heap[(index-1)/2])) {
        RangePut(index,range_heap[(index-1)/2]);index=(index-1)/2;
    }
    RangePut(index,key);
}
static void RangeSchedule(uint32_t key,wc3Clock_t const *clock,float period) {
    RangeRemove(key);rangeRequest_t *request=RangeRequest(key);
    request->deadline=*clock;request->deadline.time=wc3_add(clock->time,period);
    request->sequence=++level.timer_sequence;RangeInsert(key);
}
void G_ResetRangeListeners(void) {
    FOR_LOOP(i,MAX_EVENTS)if(range_listeners[i].rows)gi.MemFree(range_listeners[i].rows);
    memset(range_listeners,0,sizeof(range_listeners));range_count=0;
    /* No callbacks use this scratch lookup outside collection. */
    range_stamp=0;memset(range_stamps,0,sizeof(range_stamps));
}
void G_StartRangeListener(event_t *event,wc3Clock_t const *clock) {
    uint32_t slot=event-level.events.handlers;rangeListener_t *listener=range_listeners+slot;
    assert(!listener->active);listener->active=true;
    /*15ecb0 default timer,062c80 requested period,15e610 owner-bound begin.
     * Eagerly discard cancelled private nodes, retaining each creation serial. */
    RangeSchedule(slot*2,clock,1);
    RangeSchedule(slot*2,clock,RangePeriod());
    RangeSchedule(slot*2,clock,RangePeriod());
}
void G_ReleaseRangeListener(event_t *event) {
    uint32_t slot=event-level.events.handlers;rangeListener_t *listener=range_listeners+slot;
    if(!listener->active){G_ReleaseEvent(event);return;}
    if(listener->release_stage)return;
    listener->release_stage=1;wc3Clock_t clock=G_TimerQueryClock(NULL);
    RangeSchedule(slot*2+1,&clock,G_ClockMinimumDelay());
}
bool G_NextRangeRequest(wc3Clock_t *deadline,uint32_t *sequence) {
    if(!range_count)return false;
    rangeRequest_t const *request=RangeRequest(range_heap[0]);
    *deadline=request->deadline;*sequence=request->sequence;return true;
}
void G_RebaseRangeRequests(float span) {
    FOR_LOOP(i,range_count) {
        rangeRequest_t *request=RangeRequest(range_heap[i]);
        request->deadline.time=wc3_sub(request->deadline.time,span);request->deadline.epoch++;
    }
}
static void RangeReserve(rangeListener_t *listener,uint32_t count) {
    if(count<=listener->capacity)return;
    uint32_t capacity=MIN(MAX_ENTITIES,MAX(count,listener->capacity ? listener->capacity*2 : 8));
    rangeOccupant_t *rows=gi.MemAlloc(capacity*sizeof(*rows));
    if(listener->count)memcpy(rows,listener->rows,listener->count*sizeof(*rows));
    if(listener->rows)gi.MemFree(listener->rows);
    listener->rows=rows;listener->capacity=capacity;
}
static edict_t *RangeResolve(rangeOccupant_t const *row) {
    edict_t *unit=row->slot<globals.num_edicts ? g_edicts+row->slot : NULL;
    return G_UnitIsWorldActive(unit) && unit->spawn_time==row->spawn && !G_IsDeferredFree(unit) ? unit : NULL;
}
typedef struct { rangeListener_t *listener;wc3Clock_t clock;float point[2],radius;uint32_t stamp; } rangeQuery_t;
static void RangeCollect(void *data,edict_t const *unit) {
    rangeQuery_t *query=data;rangeListener_t *listener=query->listener;
    if(!G_UnitIsWorldActive(unit) || G_IsDeferredFree(unit) || !(unit->svflags&SVF_MONSTER) ||
        IS_HOLLOW(unit) || !(unit->collision>0))return;
    float point[2];S_PredictUnitFinePointAt(unit,&query->clock,point);
    float radius=wc3_add(query->radius,wc3_div(unit->collision,32));
    float x=wc3_sub(point[0],query->point[0]),y=wc3_sub(point[1],query->point[1]);
    if(!(wc3_add(wc3_mul(x,x),wc3_mul(y,y))<wc3_mul(radius,radius)))return;
    uint32_t slot=unit-g_edicts;
    if(range_stamps[slot]==query->stamp) {
        rangeOccupant_t *row=listener->rows+range_old_rows[slot];
        if(row->spawn==unit->spawn_time){row->flags|=1;return;}
    }
    RangeReserve(listener,listener->count+1);
    listener->rows[listener->count++]=(rangeOccupant_t){slot,unit->spawn_time,0};
}
static void RangeErase(rangeListener_t *listener,uint32_t index) {
    listener->rows[index]=listener->rows[--listener->count];
}
static void RangeNotify(event_t *event,rangeOccupant_t const *row) {
    edict_t *unit=RangeResolve(row);if(!unit || !event->trigger || event->trigger->destroyed || !level.vm)return;
    uint32_t spawn=unit->spawn_time;
    if(event->filter && !jass_evaluateboolexpr(level.vm,event->filter,unit))return;
    if(!unit->inuse || unit->spawn_time!=spawn || G_IsDeferredFree(unit) || event->trigger->destroyed)return;
    gameEvent_t response={.type=EVENT_UNIT_IN_RANGE,.edict=unit,.responseTo=event};
    jass_dispatchtriggerevent(level.vm,event->trigger,&response);
}
static void RangePoll(event_t *event,rangeListener_t *listener,wc3Clock_t const *clock) {
    if(!G_EventSubjectIsCurrent(event) || event->range==0)return;
    uint32_t old_count=listener->count;
    if(!++range_stamp){memset(range_stamps,0,sizeof(range_stamps));range_stamp=1;}
    FOR_LOOP(i,old_count) {
        rangeOccupant_t const *row=listener->rows+i;
        range_stamps[row->slot]=range_stamp;range_old_rows[row->slot]=i;
    }
    rangeQuery_t query={.listener=listener,.clock=*clock,.radius=wc3_div(event->range,32),.stamp=range_stamp};
    S_PredictUnitFinePointAt(event->subject,clock,query.point);
    S_QueryMoveRangeCandidates(event->subject,query.point,query.radius,RangeCollect,&query);
    /* Native15f2a0 freezes the candidate set before invoking any user callback. */
    for(uint32_t i=listener->count;i>old_count;) {
        --i;
        if(RangeResolve(listener->rows+i))RangeNotify(event,listener->rows+i);
        else RangeErase(listener,i);
    }
    for(uint32_t i=old_count;i;) {
        --i;rangeOccupant_t *row=listener->rows+i;
        if(!RangeResolve(row) || !(row->flags&1))RangeErase(listener,i);
        else row->flags&=~1u;
    }
}
void G_FireRangeRequest(void) {
    uint32_t key=range_heap[0],slot=key/2;
    rangeRequest_t request=*RangeRequest(key);RangeRemove(key);
    rangeListener_t *listener=range_listeners+slot;event_t *event=level.events.handlers+slot;
    if(key&1) {
        if(listener->release_stage==1) {
            listener->release_stage=2;RangeSchedule(key,&request.deadline,G_ClockMinimumDelay());
        } else {
            RangeRemove(slot*2);G_ReleaseEvent(event);
            if(listener->rows)gi.MemFree(listener->rows);
            memset(listener,0,sizeof(*listener));
        }
        return;
    }
    RangePoll(event,listener,&request.deadline);
    /* Release scheduled during the callback runs later in this same drain;
     * it must not prevent this rearm or alter the repeating request's serial. */
    if(listener->active) {
        listener->poll=request;listener->poll.index=0;
        listener->poll.deadline.time=wc3_add(request.deadline.time,RangePeriod());RangeInsert(key);
    }
}

/* Logical rows and clocks only. Pool pointers, capacity and heap placement are
 * reconstructed; edits to this section require the game save-version bump. */
static bool RangeIO(FILE *file,void *data,size_t size,bool write) {
    return write ? fwrite(data,1,size,file)==size : fread(data,1,size,file)==size;
}
static bool RangeState(FILE *file,bool write) {
    uint32_t count=0;
    if(write)FOR_LOOP(i,MAX_EVENTS)if(range_listeners[i].active)count++;
    if(!RangeIO(file,&count,sizeof(count),write) || count>MAX_EVENTS)return false;
    for(uint32_t ordinal=0,slot=0;ordinal<count;ordinal++,slot++) {
        if(write)while(!range_listeners[slot].active)slot++;
        uint32_t identity=slot;
        if(!RangeIO(file,&identity,sizeof(identity),write) || identity>=MAX_EVENTS)return false;
        rangeListener_t *listener=range_listeners+identity;
        event_t const *event=level.events.handlers+identity;
        if(!write && (listener->active || !event->inuse || event->type!=EVENT_UNIT_IN_RANGE))return false;
        uint32_t fields[]={listener->release_stage,listener->poll.sequence,listener->release.sequence,listener->count};
        if(!RangeIO(file,fields,sizeof(fields),write) || fields[0]>2 || fields[3]>MAX_ENTITIES)return false;
        wc3Clock_t clocks[]={listener->poll.deadline,listener->release.deadline};
        if(!RangeIO(file,clocks,sizeof(clocks),write))return false;
        if(!write) {
            FOR_LOOP(k,fields[0] ? 2 : 1)if(!isfinite(clocks[k].time) || !isfinite(clocks[k].span) || clocks[k].span<=0)return false;
            listener->active=true;listener->release_stage=fields[0];
            listener->poll=(rangeRequest_t){.deadline=clocks[0],.sequence=fields[1]};
            listener->release=(rangeRequest_t){.deadline=clocks[1],.sequence=fields[2]};
            RangeReserve(listener,fields[3]);listener->count=fields[3];
        }
        if(!RangeIO(file,listener->rows,listener->count*sizeof(*listener->rows),write))return false;
        if(!write) {
            if(!++range_stamp){memset(range_stamps,0,sizeof(range_stamps));range_stamp=1;}
            FOR_LOOP(k,listener->count) {
                rangeOccupant_t const *row=listener->rows+k;
                if(row->slot>=MAX_ENTITIES || row->flags>3 || range_stamps[row->slot]==range_stamp)return false;
                range_stamps[row->slot]=range_stamp;
            }
            RangeInsert(identity*2);if(listener->release_stage)RangeInsert(identity*2+1);
        }
    }
    return true;
}
bool G_WriteRangeListeners(FILE *file) {return RangeState(file,true);}
bool G_ReadRangeListeners(FILE *file) {
    G_ResetRangeListeners();
    if(RangeState(file,false))return true;
    G_ResetRangeListeners();return false;
}
