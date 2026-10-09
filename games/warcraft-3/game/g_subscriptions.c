#include "g_local.h"
#include "jass/jass.h"

/* Derived chains keep dispatch proportional to the owner's subscribers.
 * Stable registry slots and monotonic registration ranks replace native pool
 * pointers and stack sentinels; retired slots cannot be reused during delivery. */
#define EVENT_SUBSCRIBER_BUCKETS (MAX_EVENTS * 2)
typedef struct { uint32_t head, tail; } eventBucket_t;
typedef struct {
    uint32_t next, previous, bucket, trigger_slot, trigger_next, trigger_previous, retired_next;
    bool linked, retired;
} eventLink_t;
static eventBucket_t event_buckets[EVENT_SUBSCRIBER_BUCKETS];
static eventLink_t event_links[MAX_EVENTS];
static uint32_t trigger_events[MAX_TRIGGERS], event_retired, event_depth;
static uint32_t release_heap[MAX_TRIGGERS], release_count;
static bool subscribers_valid, releases_valid;
#ifdef BZ_TESTS
static uint32_t subscriber_visits;
uint32_t G_TestSubscriberVisits(bool reset) {
    uint32_t count=subscriber_visits;
    if(reset)subscriber_visits=0;
    return count;
}
#endif

void G_ResetEventSubscribers(void) {
    G_ResetRangeListeners();
    subscribers_valid=releases_valid=false;event_depth=event_retired=release_count=0;
    memset(event_links,0,sizeof(event_links));
}

static uint32_t EventBucket(edict_t const *subject, EVENTTYPE type) {
    /* Golden-ratio multiplicative hash; bucket placement is never simulation order. */
    return ((subject ? subject->s.number+1 : 0)*0x9e3779b9u^(uint32_t)type)&(EVENT_SUBSCRIBER_BUCKETS-1);
}

static void SubscriberUnlink(uint32_t slot) {
    eventLink_t *link=event_links+slot;
    if(!link->linked)return;
    eventBucket_t *bucket=event_buckets+link->bucket;
    if(link->previous)event_links[link->previous-1].next=link->next;
    else bucket->head=link->next;
    if(link->next)event_links[link->next-1].previous=link->previous;
    else bucket->tail=link->previous;
    if(link->trigger_slot) {
        if(link->trigger_previous)event_links[link->trigger_previous-1].trigger_next=link->trigger_next;
        else trigger_events[link->trigger_slot-1]=link->trigger_next;
        if(link->trigger_next)event_links[link->trigger_next-1].trigger_previous=link->trigger_previous;
    }
    memset(link,0,sizeof(*link));
}

static void SubscriberLink(event_t *event) {
    uint32_t slot=event-level.events.handlers,index=slot+1;
    eventLink_t *link=event_links+slot;
    link->bucket=EventBucket(event->subject,event->type);
    eventBucket_t *bucket=event_buckets+link->bucket;
    link->previous=bucket->tail;
    if(bucket->tail)event_links[bucket->tail-1].next=index;
    else bucket->head=index;
    bucket->tail=index;link->linked=true;
    if(event->trigger) {
        link->trigger_slot=(uint32_t)(event->trigger-level.triggers)+1;
        uint32_t *head=trigger_events+(event->trigger-level.triggers);
        link->trigger_next=*head;
        if(*head)event_links[*head-1].trigger_previous=index;
        *head=index;
    }
}

static int SubscriberCompare(void const *a, void const *b) {
    event_t const *x=*(event_t const *const *)a,*y=*(event_t const *const *)b;
    if(x->registration_sequence!=y->registration_sequence)
        return x->registration_sequence<y->registration_sequence ? -1 : 1;
    return x<y ? -1 : x>y;
}

static void SubscribersPrepare(void) {
    if(subscribers_valid)return;
    memset(event_buckets,0,sizeof(event_buckets));memset(trigger_events,0,sizeof(trigger_events));
    memset(event_links,0,sizeof(event_links));
    event_t *rows[MAX_EVENTS];uint32_t count=0;
    FOR_EACH_EVENT(event)rows[count++]=event;
    qsort(rows,count,sizeof(*rows),SubscriberCompare);
    FOR_LOOP(i,count)SubscriberLink(rows[i]);
    subscribers_valid=true;
}

void G_TrackEventSubscriber(event_t *event) {
    if(!subscribers_valid)return;
    SubscriberUnlink(event-level.events.handlers);
    if(event->inuse)SubscriberLink(event);
}

void G_SetEventTrigger(event_t *event, trigger_t *trigger) {
    if(subscribers_valid)SubscriberUnlink(event-level.events.handlers);
    event->trigger=trigger;
    if(subscribers_valid && event->inuse)SubscriberLink(event);
}

bool G_EventSlotAvailable(event_t const *event) {
    return !event->inuse && !event->generation_exhausted && !event_links[event-level.events.handlers].retired;
}

static bool SubscriberMatches(event_t const *registration, edict_t const *subject, EVENTTYPE type) {
    return registration->inuse && registration->subject==subject && registration->type==type &&
        (!registration->subject_spawn_tracked || (subject && subject->inuse &&
            subject->spawn_time==registration->subject_spawn_time));
}

static bool SubscriberExists(edict_t const *subject, EVENTTYPE type) {
    for(uint32_t index=event_buckets[EventBucket(subject,type)].head;index;index=event_links[index-1].next)
        if(SubscriberMatches(level.events.handlers+index-1,subject,type))return true;
    return false;
}

static void SubscribersReclaim(void) {
    while(!event_depth && event_retired) {
        uint32_t slot=event_retired-1;event_retired=event_links[slot].retired_next;
        SubscriberUnlink(slot);
    }
}

static void SubscriberDispatch(edict_t *subject, gameEvent_t const *event) {
    uint64_t cutoff=level.events.registration_sequence;
    event_depth++;
    for(uint32_t index=event_buckets[EventBucket(subject,event->type)].head;index;) {
        event_t *registration=level.events.handlers+index-1;
        if(registration->registration_sequence>cutoff)break;
#ifdef BZ_TESTS
        subscriber_visits++;
#endif
        if(SubscriberMatches(registration,subject,event->type) && registration->trigger &&
            !registration->trigger->destroyed && (!registration->filter ||
                jass_evaluateboolexpr(level.vm,registration->filter,event->edict)))
            jass_dispatchtriggerevent(level.vm,registration->trigger,event);
        index=event_links[index-1].next;
    }
    event_depth--;SubscribersReclaim();
}

void G_DispatchUnitEventFamilies(gameEventPointParams_t const *params, EVENTTYPE unit_type,
                                  bool freeze_unit_presence) {
    if(!level.vm)return;
    SubscribersPrepare();
    edict_t *player=G_GetPlayerEntityByNumber(params->edict->s.player);
    /* Issued orders67c230 freeze both families before player delivery. Death
     *67b8a0 samples the unit family only after the player death producer returns. */
    bool player_present=SubscriberExists(player,params->type);
    bool unit_present=freeze_unit_presence && SubscriberExists(params->edict,unit_type);
    gameEvent_t event={.edict=params->edict,.source=params->source,.value=params->value,
                      .has_point=params->point!=NULL};
    if(params->point)event.point=*params->point;
    if(player_present){event.type=params->type;SubscriberDispatch(player,&event);}
    if(!freeze_unit_presence)unit_present=SubscriberExists(params->edict,unit_type);
    if(unit_present){event.type=unit_type;SubscriberDispatch(params->edict,&event);}
}

static bool ReleaseLess(uint32_t a, uint32_t b) {
    trigger_t const *x=level.triggers+a,*y=level.triggers+b;
    return x->release_deadline.time==y->release_deadline.time ?
        x->release_sequence<y->release_sequence : x->release_deadline.time<y->release_deadline.time;
}

static void ReleaseInsert(uint32_t slot) {
    uint32_t index=release_count++;
    while(index && ReleaseLess(slot,release_heap[(index-1)/2])) {
        release_heap[index]=release_heap[(index-1)/2];index=(index-1)/2;
    }
    release_heap[index]=slot;
}

static void ReleasesPrepare(void) {
    if(releases_valid)return;
    release_count=0;
    FOR_LOOP(i,level.num_triggers)if(level.triggers[i].release_pending)ReleaseInsert(i);
    releases_valid=true;
}

void G_TriggerRequestDestroy(trigger_t *trigger, wc3Clock_t const *clock) {
    if(!trigger || trigger->destroyed)return;
    ReleasesPrepare();trigger->destroyed=trigger->release_pending=true;
    trigger->release_deadline=clock ? *clock : G_TimerQueryClock(NULL);
    trigger->release_deadline.time=wc3_add(trigger->release_deadline.time,G_ClockMinimumDelay());
    trigger->release_sequence=++level.timer_sequence;
    ReleaseInsert(trigger-level.triggers);
}

bool G_NextTriggerRelease(wc3Clock_t *deadline, uint32_t *sequence) {
    ReleasesPrepare();
    if(!release_count)return false;
    trigger_t const *trigger=level.triggers+release_heap[0];
    *deadline=trigger->release_deadline;*sequence=trigger->release_sequence;
    return true;
}

void G_ReleaseEvent(event_t *event) {
    uint32_t slot=event-level.events.handlers;
    if(!event->inuse)return;
    if(event->handle_generation==EVENT_HANDLE_GENERATION_MAX)event->generation_exhausted=true;
    else event->handle_generation++;
    if(event->variable){gi.MemFree((handle_t)event->variable);event->variable=NULL;}
    event->inuse=false;
    /* Reuse is delayed until the last observer has stopped reading links. */
    if(event_depth) {
        event_links[slot].retired=true;event_links[slot].retired_next=event_retired;
        event_retired=slot+1;
    } else SubscriberUnlink(slot);
    G_TrackMoveRegionEvent(event);
}

void G_FireTriggerRelease(void) {
    trigger_t *trigger=level.triggers+release_heap[0];
    uint32_t slot=release_heap[--release_count],index=0;
    if(release_count) {
        while(index*2+1<release_count) {
            uint32_t child=index*2+1;
            if(child+1<release_count && ReleaseLess(release_heap[child+1],release_heap[child]))child++;
            if(ReleaseLess(slot,release_heap[child]))break;
            release_heap[index]=release_heap[child];index=child;
        }
        release_heap[index]=slot;
    }
    trigger->release_pending=false;SubscribersPrepare();
    for(uint32_t link=trigger_events[trigger-level.triggers];link;) {
        uint32_t event_slot=link-1;event_t *event=level.events.handlers+event_slot;
        link=event_links[event_slot].trigger_next;
        if(event->type==EVENT_UNIT_IN_RANGE)G_ReleaseRangeListener(event);
        else G_ReleaseEvent(event);
    }
    DELETE_LIST(gTriggerAction_t,trigger->actions,gi.MemFree);
    DELETE_LIST(gTriggerCondition_t,trigger->conditions,gi.MemFree);
    trigger->actions=NULL;trigger->conditions=NULL;
}

void G_RebaseTriggerReleases(float span) {
    ReleasesPrepare();
    FOR_LOOP(i,release_count) {
        trigger_t *trigger=level.triggers+release_heap[i];
        trigger->release_deadline.time=wc3_sub(trigger->release_deadline.time,span);
        trigger->release_deadline.epoch++;
    }
}
