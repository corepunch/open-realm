/*
 * s_move.c — Move ability: movement orders for units.
 *
 * When a player right-clicks on empty ground, move_selectlocation() is called
 * on the server.  It creates a waypoint entity at the target position and
 * calls order_move() for each selected unit.
 *
 * order_move() sets the unit's goalentity to the waypoint and switches to the
 * movement state.  Each game frame, ai_move_walk() checks the remaining distance: if
 * the unit has arrived it switches to the stand (idle) state; otherwise it
 * rotates toward the goal and advances by one frame's worth of movement.
 *
 * Steering, collision-aware steps, route goals, and support heights are owned here.
 */
#include "s_skills.h"
#include "games/warcraft-3/common/wc3_pathing_yield.h"
#include "games/warcraft-3/common/wc3_pathing_retry.h"
#include "games/warcraft-3/common/wc3_math.h"
#include "games/warcraft-3/common/wc3_pathing_arrival.h"
#include "games/warcraft-3/common/wc3_pathing_speed.h"
#include "games/warcraft-3/common/wc3_pathing_formation.h"
#include "games/warcraft-3/common/wc3_pathing_coordinates.h"

/* With move-time collision (block-and-slide), "blocked" now means the unit
 * could not take a step this frame because it was boxed in — common and
 * transient while a group slides around obstacles.  These thresholds are
 * raised from the old free-move-plus-push values so units keep trying to
 * thread through instead of giving up the instant they are briefly packed. */
#define MOVE_BLOCKED_FRAMES 24
#define MOVE_ORDER_SUSPENDED 851973u // order ID; original688d77 scripted-pause suspension head
#define MOVE_SETTLE_FRAMES 8
#define MOVE_SLOT_MARGIN 8.0f
#define MOVE_MIN_SLOT_SPACING 16.0f
#define MOVE_ARRIVE_TOLERANCE 4.0f
#define EARTHQUAKE_MIN_MOVE_SPEED 140.0f
#define BZ_MOVE_FALLBACK_RETRY_MS 500 // milliseconds; bounds repeated unreachable floods; used as the retry interval

typedef struct {
    vec2_t point;
    float radius;
} moveSlot_t;

typedef struct {
    wc3Velocity_t velocity;
    wc3GridPose_t pose;
} moveStep_t;

static entitySet_t move_timer_members;

/* Timer ownership changes at admission/cancellation, never by scanning scenery
 * every five milliseconds. Three passes below retain the original phase order. */
void S_TrackMoveTimers(edict_t const *ent) {
    uintptr_t index=((uintptr_t)ent-(uintptr_t)g_edicts)/sizeof(*ent);
    if(!g_edicts || index>=MAX_ENTITIES)return;
    entity_set_put(&move_timer_members,index,ent->inuse && (ent->movement.captain_home.actor ||
        ent->movement.captain_home.roster_actor || ent->movement.pause_resume_pending || ent->movement.type_rebind_pending));
}

#ifdef BZ_TESTS
/* Read-only observer of scheduled Move commits, before same-clock map timers. */
static void (*move_test_motion_commit)(edict_t *unit);
static void (*move_test_group_route)(moveGroup_t const *group, edict_t *singleton);
typedef struct { wc3RetryInput_t input; wc3Random_t owner; uint32_t count, result; } moveRetryTrace_t;
static void (*move_test_retry)(edict_t *unit,moveRetryTrace_t const *trace);
typedef struct { wc3Repulse_t state; vec2_t point; wc3Random_t owner; } moveRepulseTrace_t;
static void (*move_test_repulse)(edict_t *unit,moveRepulseTrace_t const *trace);
static uint32_t move_retry_member_visits;
#endif

#define MOVE_SLIDE_STEP BZ_ROUTE_SLIDE_STEP
#define MOVE_SLIDE_RINGS BZ_ROUTE_SLIDE_RINGS
#define MOVE_SLIDE_RINGS_YIELD 2                               /* +/- 30 deg: faster unit holds its line */
#define MOVE_WORKER_QUEUE_TICKS 4                              /* same-stream blocker: queue before passing */
#define MOVE_WORKER_ESCAPE_TICKS 8                             /* widen bounded escape corridor after this */
#define MOVE_WORKER_CORRIDOR_RESET (30.0f * (float)M_PI / 180.0f)
#define MOVE_WORKER_MAX_DEVIATION 5.0f                         /* collision radii */
#define MOVE_WORKER_ESCAPE_DEVIATION 6.0f                      /* collision radii */
#define MAX_MOVE_COLLIDERS     256

typedef enum {
    MOVE_AVOID_GENERIC,
    MOVE_AVOID_RESOURCE_WORKER,
    MOVE_AVOID_STATIC_ONLY,
} moveAvoidPolicy_t;
typedef struct {
    vec2_t const *point;
    float radius;
    moveAvoidPolicy_t policy;
} moveRoutePoint_t;

typedef enum {
    MOVE_COLLIDE_UNITS,
    MOVE_IGNORE_UNITS,
} moveCollisionPolicy_t;

typedef struct { edict_t *self; wc3RepulsePair_t pair; uint32_t category, rank; } moveRepulseQuery_t;
static moveRepulseQuery_t *repulse_query;
#ifdef BZ_TESTS
static void (*move_test_repulse_pair)(edict_t const *,edict_t const *,wc3RepulsePair_t const *,wc3Repulse_t const *,wc3Random_t);
static void (*move_test_repulse_endpoint)(edict_t const *,float const [2],bool,bool);
static uint64_t move_test_repulse_unlink_visits;
#endif
static edict_t *trymove_self = NULL;
static void unit_predicted_pose(edict_t const *, wc3GridPose_t *);
static moveGroup_t const *move_deciding_group;
static uint32_t move_deciding_excluded;
/* Physical group allocations are stable until map teardown; IDs remain truth. */
static moveGroup_t *move_unit_groups[MAX_ENTITIES];
/* All request identities are minted by move_allocate_group_id. Copies and
 * retirement cannot introduce an ID above this conservative upper bound.
 * Reconstruct once after map/save replacement; wrapped IDs still use the
 * authoritative collision check below. No serialized state is duplicated. */
static uint32_t move_group_id_bound;
static bool move_group_id_bound_valid;
typedef struct {
    vec2_t world,fine,published,origin,velocity;
    wc3Clock_t now,committed;
    uint32_t flags;
} movePoseKey_t;
_Static_assert(sizeof(movePoseKey_t)==68,"Pose key must contain no padding");
typedef struct {
    edict_t const *unit;
    movePoseKey_t key;
    wc3GridPose_t pose;
    bool valid;
} movePoseCache_t;
/* Entity numbers give every live owner its own prediction slot at full game
 * capacity. Complete input bits remain truth; callbacks and external writers
 * need no invalidation protocol. Tags also protect non-pool test actors. */
static movePoseCache_t move_pose_cache[MAX_ENTITIES];
typedef struct { moveGroup_t *group; uint64_t sequence; } moveGroupVisit_t;
static moveGroup_t *move_group_head;
static uint32_t move_group_first_free;
static bool move_group_order_valid;
static int move_compare_group_visits(void const *,void const *);
/* Saves record creation sequences, not process links. Sort once on restore;
 * allocation prepends new owners and retirement unlinks them in constant time. */
static void move_prepare_group_order(void) {
    if(move_group_order_valid)return;
    uint32_t count=ARRAY_COUNT(level.move_groups),visits=0;
    moveGroupVisit_t *owners=count ? malloc(count*sizeof(*owners)) : NULL;
    if(count && !owners)gi.error("Move: cannot reconstruct physical owner order");
    move_group_first_free=count;
    FOR_LOOP(i,count) {
        moveGroup_t *group=level.move_groups[i];group->slot=i;
        group->newer=group->older=NULL;
        if(group->inuse)owners[visits++]=(moveGroupVisit_t){group,group->sequence};
        else move_group_first_free=MIN(move_group_first_free,i);
    }
    if(visits)qsort(owners,visits,sizeof(*owners),move_compare_group_visits);
    FOR_LOOP(i,visits) {
        owners[i].group->newer=i ? owners[i-1].group : NULL;
        owners[i].group->older=i+1<visits ? owners[i+1].group : NULL;
    }
    move_group_head=visits ? owners[0].group : NULL;
    move_group_order_valid=true;free(owners);
}
#ifdef BZ_TESTS
static uint32_t move_group_id_visits;
static uint32_t move_pose_cache_hits,move_pose_cache_misses;
#endif
/* Registry identity survives callback-driven pool growth and save relocation. */
static moveGroup_t *move_find_group(uint32_t id) {
    if (!id) return NULL;
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) {
        moveGroup_t *group=level.move_groups[i];
        if (group->inuse && group->id==id) return group;
    }
    return NULL;
}

static moveGroup_t *move_unit_group(edict_t const *unit) {
    uint32_t id=unit->movement.group_id;
    if(!id)return NULL;
    uintptr_t index=((uintptr_t)unit-(uintptr_t)g_edicts)/sizeof(*unit);
    if(index>=MAX_ENTITIES)return move_find_group(id);
    moveGroup_t *group=move_unit_groups[index];
    if(group && group->inuse && group->id==id)return group;
    /* First lookup after activation/restoration repairs the derived binding. */
    return move_unit_groups[index]=move_find_group(id);
}

/* An edict address alone is insufficient after removal and slot reuse. */
static moveGroupMember_t *move_find_member(edict_t const *unit) {
    moveGroup_t *group=move_unit_group(unit);
    if (!group || group->individual) return NULL;
    FOR_LOOP(i,group->count) if (group->members[i].unit==unit && group->members[i].spawn==unit->spawn_time)
        return group->members+i;
    return NULL;
}

/* Original16c250 temporarily marks eligible cohort objects40000000 around all decisions.
 * Capture eligibility before any member step changes flags; endpoint queries still see the objects. */
uint32_t S_UnitMoveFineObjectFlags(edict_t const *unit) {
    uint32_t flags=unit->movement.velocity.x || unit->movement.velocity.y ? 0x20000000 : 0;
    if (move_deciding_group) FOR_LOOP(i,move_deciding_group->count) {
        moveGroupMember_t const *member=move_deciding_group->members+i;
        if (member->unit==unit && member->spawn==unit->spawn_time && (move_deciding_excluded&(1u<<i)))
            flags|=0x40000000;
    }
    return flags;
}

/* Original16 player rows own independent ordinary fine1100-work FIFOs.
 * TODO SCHED-03/04: the three accelerated policy buckets remain separate. */
static void move_unlink_fine_request(edict_t *unit) {
    if (!unit->movement.fine_queued) return;
    assert(unit->movement.fine_class<MAX_PLAYERS);
    moveFineBudget_t *budget=level.move_fine_budgets+unit->movement.fine_class;
    edict_t *prev=unit->movement.fine_prev,*next=unit->movement.fine_next;
    if (prev) prev->movement.fine_next=next;
    else budget->head=next;
    if (next) next->movement.fine_prev=prev;
    else budget->tail=prev;
    assert(budget->count); budget->count--;
    unit->movement.fine_prev=unit->movement.fine_next=NULL; unit->movement.fine_queued=false;
}

void S_CancelUnitMoveFineRequest(edict_t *unit) {
    move_unlink_fine_request(unit);
}

void S_InitMoveFineScheduler(void) {
    cstring_t policy=gi.CvarString("wc3_path_scheduler","responsive");
    if (strcmp(policy,"responsive") && strcmp(policy,"retail"))
        gi.error("wc3_path_scheduler must be responsive or retail (takes effect on map start)");
    level.move_fine_responsive=!strcmp(policy,"responsive");
}

unsigned S_MoveSchedulingClass(edict_t const *mover) {
    /* Native missile path/line producers publish class15 independently of
     * the launching player. Rendering and damage still retain that owner. */
    return mover->movetype==MOVETYPE_FLYMISSILE ? 15u : mover->s.player;
}

void S_InitMoveProjectile(edict_t *mover) {
    mover->movetype=MOVETYPE_FLYMISSILE;
    mover->movement.adaptive_disabled=true;
    mover->movement.fine_class=15;
    mover->collision=0;
    mover->no_pathing=true;
}

bool S_AdmitUnitMoveFineRequest(edict_t *unit) {
    unsigned player=S_MoveSchedulingClass(unit);
    if (player>=MAX_PLAYERS) {
        fprintf(stderr,"Move: invalid fine-search class %u for unit %u\n",player,unit->s.number);
        return false;
    }
    if (unit->movement.fine_class!=player) {
        move_unlink_fine_request(unit);
        unit->movement.fine_class=player;
    }
    moveFineBudget_t *budget=level.move_fine_budgets+unit->movement.fine_class;
    uint32_t now=level.pathing_counter;
    if (now<unit->movement.fine_request_time) unit->movement.fine_request_time=now-BZ_WC3_FINE_REQUEST_INTERVAL;
    if (now-unit->movement.fine_request_time<BZ_WC3_FINE_REQUEST_INTERVAL) return false;
    unit->movement.fine_request_time=now;
    uint32_t limit=level.move_fine_responsive ? MAX(BZ_WC3_FINE_OWNER_WORK,budget->limit) : BZ_WC3_FINE_OWNER_WORK;
    if (budget->work<=limit && (!budget->head || budget->head==unit)) {
        move_unlink_fine_request(unit); return true;
    }
    if (!unit->movement.fine_queued) {
        unit->movement.fine_prev=budget->tail; unit->movement.fine_next=NULL;
        if (budget->tail) budget->tail->movement.fine_next=unit;
        else budget->head=unit;
        budget->tail=unit; budget->count++; unit->movement.fine_queued=true;
    }
    unit->movement.fine_request_time=0;
    return false;
}

void S_ChargeUnitMoveFineRequest(edict_t *unit, uint32_t work) {
    assert(unit->movement.fine_class<MAX_PLAYERS);
    moveFineBudget_t *budget=level.move_fine_budgets+unit->movement.fine_class;
    budget->work+=work;
    if (budget->work<BZ_WC3_FINE_FAST_WORK) unit->movement.fine_request_time=0;
}

/* Retail 1679c0 selects independent coarse pools; 167fa0 reloads after
 * reload+1 owner visits. Request links are stable within edicts/heap groups. */
static uint32_t const move_coarse_work[3]={800,300,900};
static uint32_t const move_coarse_reload[3]={3,2,2};
/* The priority bucket's selector is 2000, but a target group's path keeps
 * its own 5000-node limit. Responsive grants must cover the actual search. */
static uint32_t const move_coarse_search[3]={5000,5000,400};

void S_CancelMoveCoarseRequest(moveCoarseRequest_t *request) {
    if(request->queued) {
        assert(request->player<MAX_PLAYERS && request->policy<3);
        moveCoarseBudget_t *budget=&level.move_coarse_budgets[request->player][request->policy];
        if(request->prev)request->prev->next=request->next;
        else budget->head=request->next;
        if(request->next)request->next->prev=request->prev;
        else budget->tail=request->prev;
        assert(budget->count);budget->count--;
    }
    request->prev=request->next=NULL;request->sequence=0;
    request->queued=request->waiting=false;
}

/* Native168800 selects the individual's current pool; all engine-side local
 * request records must be retired before changing that individual's task. */
static void move_unlink_requests(edict_t *self) {
    move_unlink_fine_request(self);
    S_CancelMoveCoarseRequest(&self->movement.fine_route.adaptive_admission);
    S_CancelMoveCoarseRequest(&self->movement.fine_route.group_admission);
}

void S_SetMoveCoarseTarget(moveCoarseRequest_t *request,bool target) {
    unsigned policy=target ? 1 : 0;
    if(request->policy==policy)return;
    /* Native168ab0 changes membership without resetting the path's clock. */
    S_CancelMoveCoarseRequest(request);
    request->policy=policy;
}

bool S_AdmitMoveCoarseRequest(edict_t *unit,moveCoarseRequest_t *request,unsigned policy) {
    if(!unit || !unit->inuse || S_MoveSchedulingClass(unit)>=MAX_PLAYERS || policy>=3)
        gi.error("Move: invalid coarse admission owner/policy");
    unsigned player=S_MoveSchedulingClass(unit);
    if(request->queued && (request->player!=player || request->policy!=policy))
        S_CancelMoveCoarseRequest(request);
    request->player=player;request->policy=policy;
    uint32_t now=level.pathing_counter;
    if(!now)now=level.pathing_counter=BZ_WC3_PATH_OWNER_START;
    if(now<request->time)request->time=now-BZ_WC3_FINE_REQUEST_INTERVAL;
    request->waiting=true;
    if(now-request->time<BZ_WC3_FINE_REQUEST_INTERVAL)return false;
    request->time=now;
    /* 166c30 checks the interval before leaving the member's fine queue. */
    if(policy==2)S_CancelUnitMoveFineRequest(unit);
    moveCoarseBudget_t *budget=&level.move_coarse_budgets[request->player][policy];
    uint32_t limit=level.move_fine_responsive ? MAX(move_coarse_work[policy],budget->limit) : move_coarse_work[policy];
    if(budget->work<=limit && (!budget->head || budget->head==request)) {
        S_CancelMoveCoarseRequest(request);return true;
    }
    if(!request->queued) {
        if(level.move_coarse_sequence==UINT64_MAX)gi.error("Move: coarse queue sequence exhausted");
        request->sequence=++level.move_coarse_sequence;
        request->prev=budget->tail;request->next=NULL;
        if(budget->tail)budget->tail->next=request;
        else budget->head=request;
        budget->tail=request;budget->count++;request->queued=true;
    }
    request->time=0;return false;
}

void S_ChargeMoveCoarseRequest(moveCoarseRequest_t *request,uint32_t work) {
    assert(request->player<MAX_PLAYERS && request->policy<3);
    level.move_coarse_budgets[request->player][request->policy].work+=work;
    /* 166c30 uses this request's work, unlike fine's cumulative <64 test. */
    if(work<32)request->time=0;
}

void S_ClearMoveCoarseRequests(void) {
    FOR_LOOP(i,globals.num_edicts) {
        moveFineRoute_t *route=&g_edicts[i].movement.fine_route;
        route->adaptive_admission=route->group_admission=(moveCoarseRequest_t){0};
    }
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) {
        moveFineRoute_t *route=&level.move_groups[i]->route;
        route->adaptive_admission=route->group_admission=(moveCoarseRequest_t){0};
    }
    memset(level.move_coarse_budgets,0,sizeof(level.move_coarse_budgets));
    level.move_coarse_sequence=0;
}

static int move_compare_coarse_requests(void const *a,void const *b) {
    uint64_t x=(*(moveCoarseRequest_t *const *)a)->sequence,y=(*(moveCoarseRequest_t *const *)b)->sequence;
    return (x>y)-(x<y);
}

/* Save/restore is outside owner ticks. Rebuild only the process links from
 * saved insertion ranks; gameplay admission/unlink never scans the pool. */
static bool move_check_coarse_requests(bool restore) {
    size_t capacity=2*((size_t)globals.num_edicts+ARRAY_COUNT(level.move_groups)),count=0;
    moveCoarseRequest_t **requests=capacity ? malloc(capacity*sizeof(*requests)) : NULL;
    if(capacity && !requests)return false;
    bool valid=true;
    FOR_LOOP(i,globals.num_edicts+ARRAY_COUNT(level.move_groups)) {
        bool active=i<globals.num_edicts ? g_edicts[i].inuse : level.move_groups[i-globals.num_edicts]->inuse;
        moveFineRoute_t *route=i<globals.num_edicts ? &g_edicts[i].movement.fine_route : &level.move_groups[i-globals.num_edicts]->route;
        moveCoarseRequest_t *owners[2]={&route->adaptive_admission,&route->group_admission};
        FOR_LOOP(j,2) {
            moveCoarseRequest_t *r=owners[j];
            if(*(uint8_t *)&r->queued>1 || *(uint8_t *)&r->waiting>1 || r->player>=MAX_PLAYERS || r->policy>=3)valid=false;
            if(r->queued) {
                if(!active || !r->waiting || !r->sequence || r->sequence>level.move_coarse_sequence)valid=false;
                requests[count++]=r;
            } else if(r->sequence || r->prev || r->next)valid=false;
        }
    }
    if(count)qsort(requests,count,sizeof(*requests),move_compare_coarse_requests);
    uint32_t counts[MAX_PLAYERS][3]={{0}};
    moveCoarseRequest_t *first[MAX_PLAYERS][3]={{0}},*last[MAX_PLAYERS][3]={{0}};
    uint64_t previous=0;
    if(valid)FOR_LOOP(i,count) {
        moveCoarseRequest_t *r=requests[i];unsigned p=r->player,k=r->policy;
        if(r->sequence<=previous)valid=false;
        previous=r->sequence;
        if(!first[p][k])first[p][k]=r;
        if(!restore && (r->prev!=last[p][k] || (last[p][k] && last[p][k]->next!=r)))valid=false;
        counts[p][k]++;last[p][k]=r;
    }
    FOR_LOOP(p,MAX_PLAYERS)FOR_LOOP(k,3) {
        moveCoarseBudget_t const *b=&level.move_coarse_budgets[p][k];
        uint64_t maximum=(uint64_t)MAX_ENTITIES*3*(move_coarse_search[k]+1u);
        if(b->count!=counts[p][k] || b->countdown>move_coarse_reload[k] ||
           (b->limit && (b->limit<move_coarse_work[k] || b->limit>maximum ||
            (!level.move_fine_responsive && b->limit!=move_coarse_work[k]))) ||
           b->work>(uint64_t)MAX(b->limit,move_coarse_work[k])+move_coarse_search[k]+1u)valid=false;
        if(!restore && (b->head!=first[p][k] || b->tail!=last[p][k] || (last[p][k] && last[p][k]->next)))valid=false;
    }
    if(valid && restore) {
        FOR_LOOP(p,MAX_PLAYERS)FOR_LOOP(k,3) {
            level.move_coarse_budgets[p][k].head=level.move_coarse_budgets[p][k].tail=NULL;
        }
        FOR_LOOP(i,count) {
            moveCoarseRequest_t *r=requests[i];moveCoarseBudget_t *b=&level.move_coarse_budgets[r->player][r->policy];
            r->prev=b->tail;r->next=NULL;
            if(b->tail)b->tail->next=r;else b->head=r;
            b->tail=r;
        }
    }
    free(requests);return valid;
}

bool S_ValidateMoveCoarseRequests(void) {return move_check_coarse_requests(false);}
bool S_RestoreMoveCoarseRequests(void) {return move_check_coarse_requests(true);}

static void move_update_coarse_budgets(void) {
    FOR_LOOP(p,MAX_PLAYERS)FOR_LOOP(k,3) {
        moveCoarseBudget_t *budget=&level.move_coarse_budgets[p][k];
        if(!budget->countdown) {
            budget->work=0;
            /* Responsive admission services waiting coarse owners at every
             * owner boundary. Retail retains each pool's reload+1 cadence. */
            budget->countdown=level.move_fine_responsive ? 0 : move_coarse_reload[k];
            uint64_t grant=level.move_fine_responsive ? (uint64_t)budget->count*(move_coarse_search[k]+1u) : 0;
            if(grant>UINT32_MAX)gi.error("Move: coarse service grant exceeds counter domain");
            budget->limit=MAX(move_coarse_work[k],(uint32_t)grant);
        } else budget->countdown--;
    }
}

void S_ClearMoveFineRequests(void) {
    S_ClearMoveCoarseRequests();
    move_timer_members=(entitySet_t){0};
    FOR_LOOP(i,globals.num_edicts) {
        g_edicts[i].movement.fine_prev=g_edicts[i].movement.fine_next=NULL;
        g_edicts[i].movement.fine_queued=false;
    }
    memset(level.move_fine_budgets,0,sizeof(level.move_fine_budgets));
    level.pathing_owner_clock_valid=false;
    level.pathing_counter=BZ_WC3_PATH_OWNER_START;
}

/* Original167310 visits every row;167fa0 clears work on countdown0 and reloads1. */
static void move_update_fine_budget(void) {
    if (!++level.pathing_counter) level.pathing_counter=BZ_WC3_PATH_OWNER_START;
    move_update_coarse_budgets();
    FOR_LOOP(i,MAX_PLAYERS) {
        moveFineBudget_t *budget=level.move_fine_budgets+i;
        if (!budget->countdown) {
            budget->work=0; budget->countdown=1;
            /* Freeze a service grant for the requests already waiting at this
             * simulation boundary. Every queued request can execute its full
             * bounded search, including the charged over-budget pop. Keep the
             * grant fixed while it drains; recomputing from the shrinking FIFO
             * would strand its tail. New requests still obey FIFO admission.
             * No wall clock, worker timing or local selection affects this. */
            budget->limit=level.move_fine_responsive ?
                MAX(BZ_WC3_FINE_OWNER_WORK,budget->count*(BZ_WC3_UNIT_FINE_WORK+1u)) :
                BZ_WC3_FINE_OWNER_WORK;
        }
        else budget->countdown--;
    }
}

moveShared_t *S_FindMoveShared(uint64_t id) {
    if (!id) return NULL;
    FOR_LOOP(i,ARRAY_COUNT(level.move_shared)) {
        moveShared_t *shared=level.move_shared+i;
        if (shared->inuse && shared->id==id) return shared;
    }
    return NULL;
}

static moveShared_t *move_group_shared(moveGroup_t const *group) {
    if (!group->shared_id) return NULL;
    moveShared_t *shared=S_FindMoveShared(group->shared_id);
    if (!shared) gi.error("Move: stale shared parameter owner %llu",(unsigned long long)group->shared_id);
    return shared;
}

/* Reference0 is reclaimed in the next owner prepass, as original16c220 does. */
static uint64_t move_alloc_shared(void) {
    if (level.next_move_shared_id==UINT64_MAX) gi.error("Move: shared owner identity exhausted");
    uint32_t slot=0;
    while(slot<ARRAY_COUNT(level.move_shared) && level.move_shared[slot].inuse) slot++;
    if (slot==ARRAY_COUNT(level.move_shared)) {
        if (slot==level.move_shared_capacity) {
            uint32_t capacity=level.move_shared_capacity ? level.move_shared_capacity*2 : 16;
            if (capacity<level.move_shared_capacity) gi.error("Move: shared owner capacity exhausted");
            moveShared_t *pool=realloc(level.move_shared,capacity*sizeof(*pool));
            if (!pool) gi.error("Move: cannot allocate %u shared parameter owners",capacity);
            level.move_shared=pool; level.move_shared_capacity=capacity;
        }
        ARRAY_COUNT(level.move_shared)++;
    }
    moveShared_t *shared=level.move_shared+slot;
    *shared=(moveShared_t){.id=++level.next_move_shared_id,.inuse=true,.speed=FLT_MAX,.next_speed=FLT_MAX};
    return shared->id;
}

bool S_ValidateMoveShared(void) {
    FOR_LOOP(i,ARRAY_COUNT(level.move_shared)) {
        moveShared_t const *shared=level.move_shared+i;
        if (*(uint8_t const *)&shared->inuse>1) return false;
        if (!shared->inuse) continue;
        if (!shared->id || shared->id>level.next_move_shared_id ||
            !isfinite(shared->speed) || shared->speed<0 || !isfinite(shared->next_speed) || shared->next_speed<0 ||
            !isfinite(shared->radius) || shared->radius<0) return false;
        FOR_LOOP(j,i) if (level.move_shared[j].inuse && level.move_shared[j].id==shared->id) return false;
        uint32_t references=0;
        FOR_LOOP(g,ARRAY_COUNT(level.move_groups)) {
            moveGroup_t const *group=level.move_groups[g];
            if (group->inuse && group->shared_id==shared->id) references++;
        }
        if (references!=shared->references) return false;
    }
    FOR_LOOP(g,ARRAY_COUNT(level.move_groups)) {
        moveGroup_t const *group=level.move_groups[g];
        if (group->inuse && group->shared_id && !S_FindMoveShared(group->shared_id)) return false;
    }
    return true;
}

/* Publish the previous speed accumulator, then collect all bound groups'
 * live mover radii before any physical owner routes. Original15aa80 orders
 *16c220 for shared owners,16e1f0 for groups, and only then16c570 movement. */
static void move_update_shared(void) {
    FOR_LOOP(i,ARRAY_COUNT(level.move_shared)) {
        moveShared_t *shared=level.move_shared+i;
        if (!shared->inuse) continue;
        if (!shared->references) {memset(shared,0,sizeof(*shared));continue;}
        shared->speed=shared->next_speed; shared->next_speed=FLT_MAX; shared->radius=0;
    }
    FOR_LOOP(g,ARRAY_COUNT(level.move_groups)) {
        moveGroup_t const *group=level.move_groups[g];
        if (!group->inuse) continue;
        moveShared_t *shared=move_group_shared(group); if (!shared) continue;
        FOR_LOOP(i,group->count) {
            moveGroupMember_t const *member=group->members+i; edict_t const *unit=member->unit;
            if (unit && unit->inuse && unit->spawn_time==member->spawn && unit->movement.group_id==group->id)
                shared->radius=MAX(shared->radius,unit->collision);
        }
    }
}

static void move_free_group_routes(moveGroup_t *group) {
    S_CancelMoveCoarseRequest(&group->route.group_admission);
    S_CancelMoveCoarseRequest(&group->route.adaptive_admission);
    free(group->route.points); free(group->route.adaptive_points); free(group->route.group_points);
}

/* Retire route allocations independently of the originating JASS collection. */
static void move_release_group(moveGroup_t *group) {
    moveShared_t *shared=move_group_shared(group);
    if (shared) {
        if (!shared->references) gi.error("Move: shared parameter reference underflow");
        shared->references--;
    }
    if(move_group_order_valid) {
        if(group->newer)group->newer->older=group->older;
        else move_group_head=group->older;
        if(group->older)group->older->newer=group->newer;
        move_group_first_free=MIN(move_group_first_free,group->slot);
    }
    move_free_group_routes(group); memset(group,0,sizeof(*group));
}

void S_ClearMoveGroups(void) {
    S_ClearMoveCoarseRequests();
    memset(move_unit_groups,0,sizeof(move_unit_groups));
    move_group_id_bound=0;move_group_id_bound_valid=false;
    move_group_head=NULL;move_group_first_free=0;move_group_order_valid=false;
    memset(move_pose_cache,0,sizeof(move_pose_cache));
    /* Atomic teardown also handles a partially rejected save. It must not
     * consume unchecked serialized bindings or reference counts. */
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) {move_free_group_routes(level.move_groups[i]);free(level.move_groups[i]);}
    free(level.move_groups); level.move_groups=NULL;
    ARRAY_COUNT(level.move_groups)=level.move_group_capacity=0;
    free(level.move_shared); level.move_shared=NULL;
    ARRAY_COUNT(level.move_shared)=level.move_shared_capacity=0;
}

/* Swap removal preserves the original surviving-row order contract. */
static void move_detach_group(edict_t *unit) {
    moveGroup_t *group=move_unit_group(unit);
    if (!group) return;
    FOR_LOOP(i,group->count) if (group->members[i].unit==unit && group->members[i].spawn==unit->spawn_time) {
        group->members[i]=group->members[--group->count];
        /* Empty owners retain allocations/FIFO position until their next visit.
         * Their borrowed target is no longer needed by any member. Drop it
         * here so a removal/save needs neither a dangling reference nor a scan
         * of all physical groups. */
        if (!group->count) {
            group->target = NULL;
            group->target_spawn = group->target_refresh = 0;
            group->flags &= ~0x1000u;
        }
        /* Native171340 detaches now, but16c150 retires the empty owner at its
         * next visit. Its coarse FIFO entry and allocations remain until then,
         * for ordinary groups as well as groups with shared parameters. */
        return;
    }
}

static umove_t move_move_walk;
static void move_run_group_updates(void);
static void move_start_follow_group(edict_t *unit, edict_t *target, bool persistent);
static void move_leave(edict_t *self);
static void move_captain_actor_point(edict_t *,vec2_t const *,float);

static edict_t *trymove_blocker = NULL;  /* unit that rejected the last candidate (NULL = clear or terrain) */
static edict_t *trymove_colliders[MAX_MOVE_COLLIDERS];

static void unit_apply_heading(edict_t *self, vec2_t const *dir, moveAvoidPolicy_t policy);
static bool move_fallback_steer(edict_t *self, moveAvoidPolicy_t policy);
static bool move_displacement_steer(edict_t *self, moveAvoidPolicy_t policy);

#define MOVE_ROUTE_RESUME_MS 500u

static void move_route_resume_save(edict_t *self, edict_t *goal, float radius,
                                  uint8_t blocked_flags, vec2_t const *direction) {
    if (!self || !goal || !direction || Vector2_len(direction) <= 0.001f) return;
    self->movement.route_resume_direction = *direction;
    S_SetMoveGoal(self, &self->movement.route_resume_goal, goal);
    self->movement.route_resume_goal_origin = goal->s.origin2;
    self->movement.route_resume_goal_spawn = goal->spawn_time;
    self->movement.route_resume_time = level.time;
    self->movement.route_resume_radius = radius;
    self->movement.route_resume_flags = blocked_flags;
    self->movement.route_resume_valid = true;
}

static bool move_route_resume(edict_t *self, edict_t *goal, float radius,
                              uint8_t blocked_flags, vec2_t *direction) {
    if (!self || !goal || !direction || !self->movement.route_resume_valid ||
        self->movement.route_resume_goal != goal ||
        self->movement.route_resume_goal_spawn != goal->spawn_time ||
        Vector2_distance(&self->movement.route_resume_goal_origin, &goal->s.origin2) > 128.0f ||
        fabsf(self->movement.route_resume_radius - radius) >= 0.01f ||
        self->movement.route_resume_flags != blocked_flags ||
        (uint32_t)(level.time - self->movement.route_resume_time) > MOVE_ROUTE_RESUME_MS)
        return false;
    *direction = self->movement.route_resume_direction;
    return Vector2_len(direction) > 0.001f;
}

#ifdef WC3_DEBUG_ROUTING
static bool move_route_wait_debug_enabled(void) {
    cstring_t value = gi.CvarString ? gi.CvarString("wc3_route_wait_debug", "0") : "0";
    return value && atoi(value) != 0;
}

static cstring_t move_diag_state_name(moveDiagState_t state) {
    return state == MOVE_DIAG_ROUTE_WAIT ? "route_wait" : "none";
}

static void move_route_wait_diag(edict_t *self, bool waiting, moveDiagState_t resume_state) {
    cmPathJobStatus_t job;
    edict_t *goal;
    if (!self) return;
    goal = self->goalentity && self->goalentity->inuse ? self->goalentity : NULL;
    if (waiting) {
        if (self->movement.path_wait_active) return;
        self->movement.path_wait_active = true;
        self->movement.path_wait_start = level.time;
        self->movement.path_wait_goal_number = goal ? goal->s.number : 0;
        self->movement.path_wait_goal_spawn = goal ? goal->spawn_time : 0;
        self->movement.path_wait_origin = self->s.origin2;
        if (!move_route_wait_debug_enabled()) return;
        CM_GetPathJobStatus(&job);
        fprintf(stderr,
            "WC3_ROUTE_WAIT begin t=%u unit=%u rawcode=%08x owner=%u pos=%.1f,%.1f goal=%u@%u goal_rawcode=%08x goal_owner=%u goalpos=%.1f,%.1f collision=%.1f active=%u requester=%u jobgoal=%u target=%d,%d pending=%u queued=%u work=%u\n",
            (unsigned)level.time, (unsigned)self->s.number, (unsigned)self->class_id,
            (unsigned)self->s.player, self->s.origin2.x, self->s.origin2.y,
            (unsigned)(goal ? goal->s.number : 0), (unsigned)(goal ? goal->spawn_time : 0),
            (unsigned)(goal ? goal->class_id : 0), (unsigned)(goal ? goal->s.player : 0),
            goal ? goal->s.origin2.x : 0.0f, goal ? goal->s.origin2.y : 0.0f,
            self->collision, job.active, job.requester_number,
            job.goal_number, job.target_cell_x, job.target_cell_y,
            (unsigned)job.pending_cells, (unsigned)job.pending_jobs, (unsigned)job.work_done);
        return;
    }
    if (!self->movement.path_wait_active) return;
    self->movement.path_wait_active = false;
    if (!move_route_wait_debug_enabled()) return;
    CM_GetPathJobStatus(&job);
    fprintf(stderr,
        "WC3_ROUTE_WAIT end t=%u unit=%u rawcode=%08x duration=%u start_goal=%u@%u goal=%u@%u pos=%.1f,%.1f dpos=%.1f,%.1f result=%s flow=%u direct=%u route=%u active=%u requester=%u jobgoal=%u target=%d,%d pending=%u queued=%u work=%u\n",
        (unsigned)level.time, (unsigned)self->s.number, (unsigned)self->class_id,
        (unsigned)(level.time - self->movement.path_wait_start),
        (unsigned)self->movement.path_wait_goal_number, (unsigned)self->movement.path_wait_goal_spawn,
        (unsigned)(goal ? goal->s.number : 0), (unsigned)(goal ? goal->spawn_time : 0),
        self->s.origin2.x, self->s.origin2.y,
        self->s.origin2.x - self->movement.path_wait_origin.x,
        self->s.origin2.y - self->movement.path_wait_origin.y,
        move_diag_state_name(resume_state), (unsigned)self->movement.flow_generation,
        self->movement.flow_direct, self->movement.path.valid,
        job.active, job.requester_number,
        job.goal_number, job.target_cell_x, job.target_cell_y,
        (unsigned)job.pending_cells, (unsigned)job.pending_jobs, (unsigned)job.work_done);
}
#else
#define move_route_wait_diag(self, waiting, resume_state) ((void)0)
#endif

/* Keep a failed exceptional route search from monopolizing the frame while
 * the same goal remains unreachable; order changes clear this state below. */
static bool move_fallback_throttled(edict_t *self, vec2_t const *target, float radius) {
    if (self->movement.flow_fallback_state == MOVE_FALLBACK_APPLIED &&
        self->movement.flow_fallback_goal == self->goalentity) {
        self->movement.flow_unreachable = true;
        return true;
    }
    if (self->movement.flow_fallback_state != MOVE_FALLBACK_RETRY)
        return false;
    if (self->movement.flow_fallback_goal != self->goalentity ||
        fabsf(self->movement.flow_fallback_target.x - target->x) >= 0.01f ||
        fabsf(self->movement.flow_fallback_target.y - target->y) >= 0.01f ||
        fabsf(self->movement.flow_fallback_radius - radius) >= 0.01f ||
        (uint32_t)(level.time - self->movement.flow_fallback_time) >= BZ_MOVE_FALLBACK_RETRY_MS) {
        self->movement.flow_fallback_state = MOVE_FALLBACK_NONE;
        return false;
    }
    self->movement.flow_unreachable = true;
    return true;
}

static bool move_has_active_construction(void) {
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *ent = &g_edicts[i];
        if (ent->inuse && !(ent->s.flags & EF_NOT_SELECTABLE) &&
            G_UnitIsStructure(ent) && ent->construction)
            return true;
    }
    return false;
}

/* Wrap an angle delta into [-PI, PI]. */
static float angle_wrap(float a) {
    while (a > (float)M_PI)  a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

/* unit_changeangle is defined lower down — it needs the move-validity test and
 * the give-way helpers, which are declared below. */

/* A unit is actively executing a ground move order (right-click move). */
bool unit_is_walking(edict_t const *ent) {
    return ent->currentmove && ent->currentmove->proc == CAbilityMove;
}

/* Location orders own a legal ground endpoint; interaction orders route to an
 * entity centre and let their behavior-specific range decide arrival. */
static bool unit_routes_to_location(edict_t const *ent) {
    if (!ent->currentmove)
        return false;
    if (ent->currentmove->proc == CAbilityMove || ent->currentmove->proc == CAbilityPatrol)
        return true;
    return ent->currentmove->proc == CAbilityAttack && ent->goalentity == ent->movement.attackmove_waypoint;
}

/* Unit's effective current move speed.  Group moves travel at the slowest
 * member's speed so the selection stays a cohesive formation instead of
 * stringing out (WC3); the cap is gated on the move state so it never leaks
 * into a later attack/harvest order that reuses this.  Using the *capped*
 * speed means members of one group compare equal (no give-way within a group). */
static float unit_apply_earthquake_speed(edict_t const *unit, float speed) {
    float reduction = S_EarthquakeMoveReduction(unit);
    if (reduction <= 0.0f) return speed;
    /* Stock Earthquake cannot force a normally faster unit below 140, and it
     * must never speed up a custom unit whose authored speed is already lower. */
    return MIN(speed, MAX(EARTHQUAKE_MIN_MOVE_SPEED, speed * (1.0f - reduction)));
}

static float move_active_group_speed(edict_t const *self);
static float unit_effective_speed(edict_t *ent);

static float unit_current_speed(edict_t *self) {
    /* Step budgets and group caps must consume the same status/aura speed.
     * Using raw speed here left individual walkers unaffected by slows/bonuses. */
    float speed = unit_effective_speed(self);
    if (unit_is_walking(self)) {
        float cap = move_find_member(self) ? 0 : self->movement.group_id ? move_active_group_speed(self) : self->movement.group_speed;
        if (cap > 0 && cap < speed) speed = cap;
    }
    return speed;
}

float unit_movedistance(edict_t *self) {
    float elapsed = level.scheduled_think ? (self->movement.clock_valid ?
        wc3_elapsed(&level.pathing_clock, &self->movement.pose_clock) : 0) : 10.0f / FRAMETIME;
    return level.scheduled_think ? wc3_mul(unit_current_speed(self), elapsed) :
        10 * unit_current_speed(self) / FRAMETIME;
}

/* --- Collision-aware movement (block-and-slide) ---------------------------
 *
 * A unit only commits a step into a position that is valid for its movement class
 * and of other units' collision circles.  When the steered heading is blocked
 * it tries progressively larger left/right deflections ("sliding"), so units
 * flow around obstacles instead of plowing through them.  Idle units are hard,
 * immovable obstacles: walking into one never displaces it (the WC3 invariant
 * that the old post-move push solver violated). */

static bool unit_is_flying(edict_t const *ent) {
    return ent && (ent->aiflags & AI_FLYING) != 0;
}

static unitMovementType_t move_type_from_name(cstring_t name) {
    return wc3_movement_parse(name);
}

void S_CompileMovementData(UnitData_t *data) {
    data->compiledMoveTypeName = data->moveTypeName;
    data->compiledMoveType = move_type_from_name(data->moveTypeName);
}

unitMovementType_t S_UnitMovementType(UnitData_t const *data) {
    if (!data) return UNIT_MOVE_UNSPECIFIED;
    if (data->compiledMoveTypeName == data->moveTypeName) return data->compiledMoveType;
    /* In-memory fixture rows and an explicitly rebound field need not have
     * passed through the metadata loader. Decode the live value in that case;
     * never mutate a borrowed row or rely on its rawcode to identify it. */
    return move_type_from_name(data->moveTypeName);
}

uint8_t M_UnitStaticPathingFlags(edict_t const *ent) {
    if (unit_is_flying(ent)) return CM_PATHING_UNFLYABLE;
    if(ent && ent->movement.captain_actor_type)return CM_PATHING_UNWALKABLE;
    wc3MovementProfile_t const *profile=S_UnitMovementProfile(ent ? ent->data.UnitData : NULL);
    /* Forced-ground producers change the fine profile, retaining the
     * authored/coarse class. The physical layer is already ability-owned. */
    return profile->support==WC3_SUPPORT_FLIGHT ? wc3_movement_profiles[UNIT_MOVE_FOOT].query : profile->query;
}

wc3MovementProfile_t const *S_UnitMovementProfile(UnitData_t const *data) {
    return wc3_movement_profile(S_UnitMovementType(data));
}

uint8_t S_UnitMoveCategory(edict_t const *ent) {
    if(!ent || unit_is_flying(ent))return 0;
    if(ent->movement.captain_actor_type)return 2;
    wc3MovementProfile_t const *profile=S_UnitMovementProfile(ent->data.UnitData);
    return profile->support==WC3_SUPPORT_FLIGHT ? wc3_movement_profiles[UNIT_MOVE_FOOT].category : profile->category;
}

uint8_t S_UnitMoveCoarseMask(edict_t const *ent) {
    if(ent && ent->movement.captain_actor_type)
        return unit_is_flying(ent) ? CM_PATHING_UNFLYABLE : CM_PATHING_UNWALKABLE;
    return wc3_movement_coarse_mask(S_UnitMovementProfile(ent ? ent->data.UnitData : NULL)->path_class);
}

/*68a060 constructs the implicit Move owner when the captured authored speed
 * is nonzero, regardless of movetp.685310/698af0 read and write that owner;
 * a zero pathing query must not disable a positive-speed unit. The instance
 * speed and explicit setter flag retain an existing owner when shared defaults
 * change; allocator-only owners also publish their speed there. Captains own
 * their independent speed even without a UnitBalance row. */
bool M_UnitMoveDisabled(edict_t const *ent) {
    return ent && !ent->movement.captain_actor_type && ent->data.UnitBalance &&
        ent->data.UnitBalance->speed==0 && ent->unitinfo.MoveSpeed==0 &&
        !(ent->unitinfo.move_flags&BZ_UNIT_SPEED_SET);
}

/* BoxEdicts predicate: solid units/buildings sharing this mover's collision
 * layer.  Excludes self, hollow entities, zero-collision entities (waypoints,
 * effects, missiles), and the opposite air/ground layer (flyers and ground
 * units pass through each other). */
static bool filter_blockers(edict_t const *ent) {
    if (ent == trymove_self || IS_HOLLOW(ent) || ent->collision <= 0.0f)
        return false;
    /* An alive walkable destructable is a ground surface, not a circle-shaped
     * obstacle. Its authored path texture remains responsible for deck edges. */
    if (G_IsDestructable(ent) && !ent->destructable->dead &&
        ent->destructable->placement_solid && ent->pathtex &&
        ent->data.DestructableData && ent->data.DestructableData->walkable) return false;
    /* Trees have collisionSize 0 (they block only via their baked footprint) so
     * they are already excluded above; buildings keep a real collision circle
     * and ARE counted here — relying on the terrain footprint alone lets units
     * leak through coarse 32u cells. Flyers and non-flyers retain separate layers;
     * native query masks do not define a second ground/sea collision domain. */
    return unit_is_flying(ent) == unit_is_flying(trymove_self);
}

/* Distance from point p to the segment [a,b]. */
static float point_segment_distance(vec2_t const *a, vec2_t const *b, vec2_t const *p) {
    vec2_t const ab = Vector2_sub(b, a);
    vec2_t const ap = Vector2_sub(p, a);
    float const ab2 = ab.x * ab.x + ab.y * ab.y;
    float t = ab2 > 0.0001f ? (ap.x * ab.x + ap.y * ab.y) / ab2 : 0.0f;
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    vec2_t const closest = { a->x + t * ab.x, a->y + t * ab.y };
    return Vector2_distance(&closest, p);
}

/* All Move geometry consumers use the same retail footprint class. */
static bool move_static_point(edict_t const *self, vec2_t const *point) {
    pathAccelParams_t params = { point, NULL, self->collision, M_UnitStaticPathingFlags(self) };
    return G_MovePathPointIsPathable(&params);
}

static bool move_static_line(edict_t const *self, vec2_t const *point, float radius) {
    pathAccelParams_t params = { &self->s.origin2, point, radius, M_UnitStaticPathingFlags(self) };
    return G_MovePathLineIsPathable(&params);
}

/* Route eligibility follows the same ability-owned collision query as steps.
 * Fine search ignores moving neighbours; precise step collision still sees them. */
static movePathQuery_t move_route_query(edict_t *self, moveRoutePoint_t point) {
    /* Interaction abilities retain their range/queue policy; this increment
     * adds live fine occupancy to location orders only. */
    bool units = unit_routes_to_location(self) && point.policy != MOVE_AVOID_STATIC_ONLY &&
        !S_UnitStatusAbilityEvent(self, A_MOVE_COLLISION_QUERY, NULL);
    /* Original16a790 passes05bdd0's fine prediction to16fbd0. Reversing a
     * published world coordinate loses low bits at nonzero map origins. */
    vec2_t const *fine = self->movement.pose_valid &&
        wc3_float_bits(self->movement.pose_world.x)==wc3_float_bits(self->s.origin2.x) &&
        wc3_float_bits(self->movement.pose_world.y)==wc3_float_bits(self->s.origin2.y) ?
        &self->movement.sampled_pose : NULL;
    moveGroupMember_t const *member=move_find_member(self);
    edict_t *target=self->movement.captain_home.active ? self->movement.captain_home.actor : self->goalentity;
    /* A point-order waypoint carries coordinates, not a resolved group target. */
    if(target && (target->svflags&SVF_MOVE_WAYPOINT))target=NULL;
    return (movePathQuery_t){ .geometry={&self->s.origin2,member ? &member->world_destination : point.point,point.radius,self->no_pathing ? 0 : M_UnitStaticPathingFlags(self)},
        .mover=self,.target=target,
        .units=units,.fine=fine,.fine_target=member ? &member->destination : NULL,
        .coarse_mask=S_UnitMoveCoarseMask(self) };
}

static bool move_route_line(edict_t *self, moveRoutePoint_t point) {
    /* Original215540 clears only the mover query. Its category remains an obstacle to others. */
    if (self->no_pathing) return true;
    movePathQuery_t query = move_route_query(self, point);
    return G_UnitMovePathLineIsPathable(&query);
}

/* Check static pathing and live units; retain the rejecting unit for give-way. */
static bool move_is_valid_policy(edict_t *self, vec2_t const *cand,
                                 moveCollisionPolicy_t collision_policy) {
    trymove_blocker = NULL;
    /* Pathing-disabled units (SetUnitPathing(false), scripted moves) ignore
     * all collision, matching the old unconditional translate. */
    if (self->no_pathing)
        return true;

    /* Static world: terrain + baked building footprints (pathmap.original). */
    if (!move_static_point(self, cand))
        return false;
    /* WC3's pathing grid rejects a swept step that cuts a diagonal corner. Keep
     * the escape case for units spawned inside stale/changed pathing, where the
     * endpoint remains the authoritative legal position. */
    if (move_static_point(self, &self->s.origin2) && !move_static_line(self, cand, self->collision))
        return false;

    if (collision_policy == MOVE_IGNORE_UNITS)
        return true;

    /* Dynamic units: precise circle test.  The "don't deepen penetration" rule
     * ignores a neighbour the unit already overlaps unless the candidate moves
     * closer to it, so units that start overlapped (spawn / blink / a building
     * dropped on them) can still slide apart instead of dead-locking. */
    /* Broad-phase box must cover the whole swept segment (origin -> cand), not
     * just the endpoint: a fast unit's step spans many units, and a box centred
     * on cand would miss a blocker sitting near the START of the path — letting
     * the unit jump clean over it.  BoxEdicts tests each entity's bounds (which
     * already extend by its own collision radius), so inflating by self's radius
     * is enough to catch any blocker within rr of the corridor. */
    float const reach = self->collision + 1.0f;
    float const ox = self->s.origin2.x, oy = self->s.origin2.y;
    box2_t const box = {
        { (ox < cand->x ? ox : cand->x) - reach, (oy < cand->y ? oy : cand->y) - reach },
        { (ox > cand->x ? ox : cand->x) + reach, (oy > cand->y ? oy : cand->y) + reach },
    };
    trymove_self = self;
    uint32_t const num = gi.BoxEdicts(&box, trymove_colliders, MAX_MOVE_COLLIDERS, filter_blockers);
    FOR_LOOP(i, num) {
        edict_t *const b = trymove_colliders[i];
        float const rr = self->collision + b->collision;
        /* Swept test: the unit's whole PATH this tick (origin -> cand) must
         * clear b, not just the endpoint — otherwise a fast unit (step ~one
         * cell) jumps clean over a smaller unit between ticks.  Mirrors WC3's
         * swept-circle collision. */
        float const seg_d = point_segment_distance(&self->s.origin2, cand, &b->s.origin2);
        if (seg_d >= rr)
            continue;  /* the swept path clears b */
        float const cur_d = Vector2_distance(&self->s.origin2, &b->s.origin2);
        if (cur_d < rr && seg_d >= cur_d - 0.5f)
            continue;  /* already overlapping b: allow only a step whose path does
                        * not go deeper into b — lets it separate, never slide
                        * tangentially or jump THROUGH it. */
        trymove_blocker = b;
        return false;
    }
    return true;
}

static bool move_is_valid(edict_t *self, vec2_t const *cand) {
    return move_is_valid_policy(self, cand, MOVE_COLLIDE_UNITS);
}

/* Shared steering uses the same static-only policy as resource interaction movement. */
static bool move_static_is_valid(edict_t *self, vec2_t const *cand) { return move_is_valid_policy(self, cand, MOVE_IGNORE_UNITS); }

/* Public: would 'pos' be a free standing spot for 'self' (terrain + units)?
 * Used by the move arrival to avoid snapping a unit onto an occupied goal. */
bool M_MoveIsValid(edict_t *self, vec2_t const *pos) {
    return move_is_valid(self, pos);
}

static void unit_commit_world(edict_t *self, vec2_t const *cand) {
    if (self->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    self->s.origin2 = *cand;
    self->movement.pose_valid = false;
    self->movement.clock_valid = false;
    self->movement.worker_avoid_blocked_frames = 0;
    self->s.origin.x = cand->x;
    self->s.origin.y = cand->y;
    gi.LinkEntity(self);
    if (self->movement.route_resume_active && self->movement.route_resume_goal &&
        self->movement.route_resume_goal->inuse) {
        self->movement.route_resume_time = level.time;
        self->movement.route_resume_goal_origin = self->movement.route_resume_goal->s.origin2;
    }
}

static void unit_commit_step(edict_t *self, vec2_t const *cand) {
    unit_commit_world(self,cand); G_PublishMoveSpatialObject(self);
}

/* Preserve native low bits on unchanged axes; world position writers reproject changed axes. */
static void unit_grid_pose(edict_t const *self, wc3GridPose_t *pose) {
    box2_t const bounds = CM_GetWorldBounds();
    *pose = (wc3GridPose_t){ .grid = {self->movement.fine_pose.x, self->movement.fine_pose.y},
        .origin = {bounds.min.x, bounds.min.y}, .world = {self->s.origin2.x, self->s.origin2.y} };
    float const published[2] = {self->movement.pose_world.x, self->movement.pose_world.y};
    FOR_LOOP(k, 2) {
        float old = published[k];
        if (!self->movement.pose_valid || wc3_float_bits(old) != wc3_float_bits(pose->world[k]))
            pose->grid[k] = wc3_grid_coordinate(pose->world[k], pose->origin[k], 32);
    }
}

/* Retain accepted native pose after the common explicit-world commit invalidates its old value. */
static void unit_commit_pose(edict_t *self, wc3GridPose_t const *pose) {
    vec2_t cand = {pose->world[0], pose->world[1]};
    unit_commit_world(self, &cand);
    self->movement.fine_pose = self->movement.sampled_pose = (vec2_t){pose->grid[0], pose->grid[1]};
    self->movement.pose_valid = true;
    self->movement.pose_world = cand;
    if (level.scheduled_think) {
        self->movement.pose_clock = level.pathing_clock;
        self->movement.clock_valid = true;
    }
    G_PublishMoveSpatialObject(self);
    M_CheckGround(self);
}

/* Release the virtual target only after logical ownership and all physical
 * task references end. Recreation may retire an actor with live followers. */
static botCaptain_t *move_actor_captain(edict_t const *actor) {
    uint32_t type=actor->movement.captain_actor_type;
    if (!actor->movement.captain_actor_owned || actor->s.player>=MAX_PLAYERS ||
        !type || type>BOT_CAPTAIN_COUNT) return NULL;
    botCaptain_t *captain=level.bots[actor->s.player].captains+type-1;
    return captain->home_actor==actor ? captain : NULL;
}

/* Registered inner membership is retained across physical order replacement.
 * Ordinary callbacks use the edge-maintained count, not an entity/roster scan. */
static uint32_t move_captain_entered(edict_t const *actor) {
    botCaptain_t const *captain=move_actor_captain(actor);
    if (captain) return captain->entered_members;
    uint32_t count=0;
    for(uint32_t i=entity_set_next(&move_timer_members,0);i<globals.num_edicts;i=entity_set_next(&move_timer_members,i+1)) {
        edict_t const *unit=g_edicts+i;
        if(unit->inuse && unit->movement.captain_home.entered &&
            (unit->movement.captain_home.roster_actor==actor ||
             (!unit->movement.captain_home.roster_actor && unit->movement.captain_home.active && unit->movement.captain_home.actor==actor))) count++;
    }
    return count;
}

static void move_free_unowned_captain_actor(edict_t *actor) {
    if (!actor || !actor->inuse || actor->movement.captain_actor_owned) return;
    FILTER_EDICTS(ent,ent->inuse && (ent->movement.captain_home.actor==actor || ent->movement.captain_home.roster_actor==actor)) return;
    G_FreeEdict(actor);
}

static void move_release_captain_reference(edict_t *self) {
    edict_t *actor=self->movement.captain_home.actor;
    self->movement.captain_home.actor=NULL;
    S_TrackMoveTimers(self);
    self->movement.captain_home.active=false;
    if (!self->movement.captain_home.roster_actor) self->movement.captain_home.entered=false;
    move_free_unowned_captain_actor(actor);
}

/* Native9d5610 removes the retained member, independently of its canceled task.
 * Compact logical encounter indices before the edict slot can be reused. */
static void move_remove_captain_roster_member(edict_t *self) {
    edict_t *actor=self->movement.captain_home.roster_actor;
    if (!actor) return;
    botCaptain_t *captain=move_actor_captain(actor);
    if (captain && self->movement.captain_home.entered) captain->entered_members--;
    uint32_t index=self->movement.captain_home.member_index;
    self->movement.captain_home.roster_actor=NULL;
    self->movement.captain_home.outer=false;
    self->movement.captain_home.entered=false;
    S_TrackMoveTimers(self);
    if (actor->movement.captain_actor_members) actor->movement.captain_actor_members--;
    actor->movement.captain_actor_siege=false;
    FILTER_EDICTS(ent,ent->inuse && ent->movement.captain_home.roster_actor==actor) {
        if (ent->movement.captain_home.member_index>index) ent->movement.captain_home.member_index--;
        if (S_UnitHasLongRangeSiegeAttack(ent)) actor->movement.captain_actor_siege=true;
    }
}

void S_DetachCaptainUnit(edict_t *self) {
    move_remove_captain_roster_member(self);
    move_release_captain_reference(self);
}

void S_ReleaseCaptainHomeActor(edict_t *actor) {
    if (!actor) return;
    actor->movement.captain_actor_owned=false;
    FILTER_EDICTS(ent,ent->inuse && ent->movement.captain_home.roster_actor==actor) {
        ent->movement.captain_home.roster_actor=NULL;
        S_TrackMoveTimers(ent);
        ent->movement.captain_home.outer=false;
        if (!ent->movement.captain_home.active) ent->movement.captain_home.entered=false;
    }
    move_free_unowned_captain_actor(actor);
}

/* Native9d2f90 retains category2 with radius0. Fine occupancy still covers
 * one cell; no model, ordinary unit data or client presentation is required. */
void S_SetCaptainHomeActor(botCaptain_t *captain, uint32_t player, uint32_t type) {
    if (captain->home_actor) {
        /* Authored home and the retained point request are separate. Native
         *9d5c70 leaves an occupied actor/request in place before reevaluation. */
        return;
    }
    edict_t *actor=captain->home_actor;
    if (!actor) {
        actor=captain->home_actor=G_Spawn();
        captain->update_due=level.pathing_clock;
        wc3_clock_advance(&captain->update_due,1,0);
        captain->state=type==BOT_CAPTAIN_ATTACK+1 ? BOT_CAPTAIN_ACTIVE : BOT_CAPTAIN_FORMING;
        captain->goal=captain->home;
        captain->request_range=200;
    }
    actor->svflags=SVF_NOCLIENT;
    actor->stand=unit_stand;
    actor->s.player=player;
    actor->movement.captain_actor_type=type;
    actor->movement.captain_actor_owned=true;
    /* Native9d2f90 admits its zero-radius actor through ordinary point
     * placement. The authored home remains the later shared point request. */
    actor->s.origin2=captain->home;
    if (!G_FindUnitPlacementPosition(actor,&captain->home,&actor->s.origin2))
        fprintf(stderr,"WC3 captain placement: no admitted home player=%u type=%u at (%.9g,%.9g)\n",player,type,captain->home.x,captain->home.y);
    gi.LinkEntity(actor);
    G_PublishMoveSpatialObject(actor);
}

static int move_captain_reference_compare(void const *a,void const *b) {
    uint64_t x=*(uint64_t const *)a,y=*(uint64_t const *)b;
    return (x>y)-(x<y);
}

/* The logical actor and physical references are saved; the bot VM is not.
 * Rebuild only runtime captain links and reject stale physical references. */
bool S_ValidateCaptainHomeActors(bool rebind) {
    uint64_t references[MAX_ENTITIES];uint32_t reference_count=0;
    edict_t *owners[MAX_PLAYERS][BOT_CAPTAIN_COUNT]={{0}};
    uint32_t counts[MAX_PLAYERS][BOT_CAPTAIN_COUNT]={{0}};
    FILTER_EDICTS(actor,actor->inuse && actor->movement.captain_actor_type) {
        uint32_t type=actor->movement.captain_actor_type;
        if (type>BOT_CAPTAIN_COUNT || actor->s.player>=MAX_PLAYERS || actor->collision!=0 ||
            actor->movement.captain_actor_members>globals.num_edicts) return false;
        if (!actor->movement.captain_actor_owned) continue;
        edict_t **slot=&owners[actor->s.player][type-1];
        if (*slot) return false;
        *slot=actor;
    }
    FILTER_EDICTS(ent,ent->inuse) {
        if (ent->movement.captain_actor_owned && !ent->movement.captain_actor_type) return false;
        if (*(uint8_t *)&ent->movement.captain_actor_siege>1 ||
            (ent->movement.captain_actor_siege && !ent->movement.captain_actor_type)) return false;
        edict_t *actor=ent->movement.captain_home.actor;
        edict_t *roster=ent->movement.captain_home.roster_actor;
        if (*(uint8_t *)&ent->movement.captain_home.active>1 ||
            *(uint8_t *)&ent->movement.captain_home.entered>1 ||
            *(uint8_t *)&ent->movement.captain_home.outer>1 ||
            (ent->movement.captain_home.entered && !ent->movement.captain_home.active && !roster) ||
            (ent->movement.captain_home.outer && !roster)) return false;
        if (roster && (!roster->inuse || !roster->movement.captain_actor_owned ||
            !roster->movement.captain_actor_type || roster->movement.captain_actor_type>BOT_CAPTAIN_COUNT ||
            (actor && actor!=roster))) return false;
        if (roster) counts[roster->s.player][roster->movement.captain_actor_type-1]++;
        if (ent->movement.captain_home.active && !actor) return false;
        if (!actor) actor=roster;
        if (actor && (!actor->movement.captain_actor_members ||
            actor->movement.captain_actor_members>globals.num_edicts ||
            ent->movement.captain_home.member_index>=actor->movement.captain_actor_members)) return false;
        if (actor && (!actor->inuse || !actor->movement.captain_actor_type ||
            actor->movement.captain_actor_type>BOT_CAPTAIN_COUNT)) return false;
        if(actor)references[reference_count++]=((uint64_t)(actor-g_edicts+1)<<32)|ent->movement.captain_home.member_index;
        if (actor &&
            (!isfinite(ent->movement.captain_home.due.time) || !isfinite(ent->movement.captain_home.due.span) ||
             ent->movement.captain_home.due.span<=0 || !isfinite(ent->movement.captain_home.home.x) ||
             !isfinite(ent->movement.captain_home.home.y))) return false;
    }
    /* Pair identity, not world encounter order, establishes uniqueness.
     * Sorting O(N log N) replaces the old pairwise O(N^2) save check. */
    qsort(references,reference_count,sizeof(*references),move_captain_reference_compare);
    for(uint32_t i=1;i<reference_count;i++)if(references[i]==references[i-1])return false;
    FOR_LOOP(p,MAX_PLAYERS) FOR_LOOP(c,BOT_CAPTAIN_COUNT)
        if (owners[p][c] && owners[p][c]->movement.captain_actor_members!=counts[p][c]) return false;
    if (rebind) {
        FOR_LOOP(p,MAX_PLAYERS) FOR_LOOP(c,BOT_CAPTAIN_COUNT) {
            level.bots[p].captains[c].home_actor=owners[p][c];
            level.bots[p].captains[c].entered_members=0;
        }
        FILTER_EDICTS(ent,ent->inuse && ent->movement.captain_home.roster_actor && ent->movement.captain_home.entered) {
            botCaptain_t *captain=move_actor_captain(ent->movement.captain_home.roster_actor);
            if(captain)captain->entered_members++;
        }
    }
    return true;
}

static bool move_group_point_order(groupPointOrder_t const *request,uint64_t shared_id);
static bool move_group_captain_order(groupPointOrder_t const *request,uint64_t shared_id,edict_t *target);

/* Native9d2ee0 with retained GoHome range500:2*r*r+5000 world squared.
 * Initializers019720/019890/001c30 provide500/5000/2; PE slots are zero. */
static bool move_captain_near_point(edict_t const *actor,vec2_t const *point,float range) {
    wc3GridPose_t pose; unit_predicted_pose(actor,&pose);
    float x=wc3_sub(pose.world[0],point->x),y=wc3_sub(pose.world[1],point->y);
    float limit=range<100 ? 20000 : wc3_add(wc3_mul(2,wc3_mul(range,range)),5000);
    return wc3_add(wc3_mul(x,x),wc3_mul(y,y))<=limit;
}

static bool move_captain_near_home(edict_t const *actor,vec2_t const *home) {
    return move_captain_near_point(actor,home,500);
}

bool S_CaptainNearHome(botCaptain_t const *captain) {
    return captain->home_actor && move_captain_near_home(captain->home_actor,&captain->home);
}

bool S_CaptainNearRequest(botCaptain_t const *captain) {
    return captain->home_actor && move_captain_near_point(captain->home_actor,&captain->goal,captain->request_range);
}

static void move_captain_collect_member(edict_t *actor,edict_t **roster,edict_t *unit) {
    if(!unit->inuse || (unit->movement.captain_home.roster_actor!=actor &&
        (unit->movement.captain_home.roster_actor || !unit->movement.captain_home.active || unit->movement.captain_home.actor!=actor))) return;
    uint32_t index=unit->movement.captain_home.member_index;
    if(index>=actor->movement.captain_actor_members || roster[index])
        gi.error("Move: invalid captain roster member %u/%u",index,actor->movement.captain_actor_members);
    roster[index]=unit;
}

/* Request-local storage remains valid through nested order callbacks. Physical
 * indices are separate from the AI's prepend order, including after load. */
static edict_t **move_captain_collect_roster(edict_t *actor) {
    uint32_t count=actor->movement.captain_actor_members;
    if(!count)return NULL;
    if(count>globals.num_edicts)gi.error("Move: invalid captain roster size %u",count);
    edict_t **roster=gi.MemAlloc(count*sizeof(*roster));
    memset(roster,0,count*sizeof(*roster));
    botCaptain_t *captain=move_actor_captain(actor);
    if(captain && ARRAY_COUNT(captain->units)==count) {
        FOR_EACH_ARRAY(edict_t *,member,captain->units) move_captain_collect_member(actor,roster,*member);
    } else {
        for(uint32_t i=entity_set_next(&move_timer_members,0);i<globals.num_edicts;i=entity_set_next(&move_timer_members,i+1))
            move_captain_collect_member(actor,roster,g_edicts+i);
    }
    FOR_LOOP(i,count)if(!roster[i])gi.error("Move: missing captain roster member %u/%u",i,count);
    return roster;
}

/* Native9d27c0 retains roster order through bounded physical batches. */
static void move_captain_shared_point(edict_t *actor,edict_t **roster,uint32_t members,vec2_t const *home,bool logical) {
    /* Retail queued AI point orders preserve an already active identical
     * request. A completed physical task remains eligible for new admission. */
    bool unchanged=members>0;
    FOR_LOOP(i,members) {
        edict_t *ent=roster[i]; moveGroup_t const *group=move_unit_group(ent);
        if (!group || group->target || !group->shared_id || group->goal.x!=home->x || group->goal.y!=home->y)
            unchanged=false;
    }
    if (unchanged) return;
    typeof(actor->movement.captain_home) *retained=gi.MemAlloc(members*sizeof(*retained));
    FOR_LOOP(i,members) {
        edict_t *ent=roster[i];
        retained[i]=ent->movement.captain_home;
        ent->movement.captain_home.active=false;
        /* Transfer physical references across order replacement. */
        ent->movement.captain_home.actor=NULL;
        S_TrackMoveTimers(ent);
        ent->movement.captain_home.roster_actor=NULL;
        S_TrackMoveTimers(ent);
    }
    uint64_t shared_id=members>1 ? move_alloc_shared() : 0;
    if (members==1) S_IssueMoveOrder(roster[0],Waypoint_add(home),G_OrderId("move"));
    else for(uint32_t first=0;first<members;first+=BZ_WC3_GROUP_ORDER_UNITS) {
        groupPointOrder_t request={.count=MIN(members-first,BZ_WC3_GROUP_ORDER_UNITS),
            .order="move",.order_id=G_OrderId("move"),.issuer_player=actor->s.player,
            .point=home};
        FOR_LOOP(i,request.count) {
            request.units[i].unit=roster[first+i];
            request.units[i].spawn=roster[first+i]->spawn_time;
        }
        if (!move_group_point_order(&request,shared_id))
            fprintf(stderr,"WC3 Move: captain shared home point batch rejected at %u/%u members\n",first,members);
    }
    FOR_LOOP(i,members) {
        roster[i]->movement.captain_home=retained[i];
        S_TrackMoveTimers(roster[i]);
        roster[i]->movement.captain_home.actor=actor;
        S_TrackMoveTimers(roster[i]);
        roster[i]->movement.captain_home.active=false;
        if (!logical) roster[i]->movement.captain_home.entered=false;
    }
    gi.MemFree(retained);
}

/* Native9d16c0 prepares only members whose current target is not this captain,
 * then admits them in roster order through twelve-row physical requests. */
static void move_captain_shared_target(edict_t *actor) {
    uint32_t members=actor->movement.captain_actor_members,count=0;
    edict_t **roster=move_captain_collect_roster(actor);
    edict_t **selected=gi.MemAlloc(members*sizeof(*selected));
    FOR_LOOP(i,members) {
        edict_t *ent=roster[i];
        if (!ent) gi.error("Move: missing captain target member %u/%u",i,members);
        moveGroup_t const *group=move_unit_group(ent);
        if (group && group->target==actor) continue;
        selected[count++]=ent;
    }
    gi.MemFree(roster);
    if (!count) {gi.MemFree(selected);return;}
    typeof(actor->movement.captain_home) *retained=gi.MemAlloc(count*sizeof(*retained));
    FOR_LOOP(i,count) {
        retained[i]=selected[i]->movement.captain_home;
        selected[i]->movement.captain_home.actor=selected[i]->movement.captain_home.roster_actor=NULL;
        S_TrackMoveTimers(selected[i]);
    }
    uint64_t shared_id=move_alloc_shared();
    for (uint32_t first=0;first<count;first+=BZ_WC3_GROUP_ORDER_UNITS) {
        groupPointOrder_t request={.count=MIN(count-first,BZ_WC3_GROUP_ORDER_UNITS),
            .order="move",.order_id=G_OrderId("move"),.issuer_player=actor->s.player,
            .point=&retained[first].home};
        FOR_LOOP(i,request.count) {
            request.units[i].unit=selected[first+i];
            request.units[i].spawn=selected[first+i]->spawn_time;
        }
        if (!move_group_captain_order(&request,shared_id,actor))
            gi.error("Move: captain shared target batch rejected at %u/%u",first,count);
    }
    FOR_LOOP(i,count) {
        selected[i]->movement.captain_home=retained[i];
        selected[i]->movement.captain_home.actor=actor;
        selected[i]->movement.captain_home.active=true;
        S_TrackMoveTimers(selected[i]);
    }
    gi.MemFree(retained);
    gi.MemFree(selected);
}

/*9d4c20 point policy: scan the retained logical roster, not all entities.
 * The reduction uses total membership, but only eligible Move owners supply
 * the minimum and Adro classification. Combat target policies are separate. */
static float move_captain_point_speed(botCaptain_t const *captain) {
    if (captain->policy_flags&BOT_CAPTAIN_RETREAT_FLAG) return 500;
    float speed=9999;uint32_t entered=captain->entered_members,members=ARRAY_COUNT(captain->units);
    uint32_t board=G_OrderId("board");
    bool ordinary=false;
    FOR_EACH_ARRAY(edict_t *,member,captain->units) {
        edict_t *unit=*member;
        if (M_UnitMoveDisabled(unit) || !G_ActorHasAbilityCode(unit,MAKEFOURCC('A','m','o','v')) ||
            S_CargoTransportForUnit(unit) || unit->current_order_id==board) continue;
        speed=MIN(speed,S_UnitMoveSpeed(unit));
        if (!S_UnitHasAbilityFlags(unit,AB_MOVE_TARGET_NO_WARP)) ordinary=true;
    }
    float x=wc3_float(wc3_float_bits(wc3_sub(captain->goal.x,captain->home.x))&0x7fffffffu);
    float y=wc3_float(wc3_float_bits(wc3_sub(captain->goal.y,captain->home.y))&0x7fffffffu);
    if (ordinary && entered<members &&
        !(x<wc3_float(0x3a83126f) && y<wc3_float(0x3a83126f))) {
        float count=wc3_float(wc3_from_int(members));
        float factor=wc3_add(wc3_mul(count,wc3_float(0x3c321643)),wc3_float(0x3f3d37a7));
        speed=wc3_mul(speed,factor);
    }
    return speed;
}

/* Strict predicted membership retains creation phase and exact timer deadline. */
static void move_captain_home_update(edict_t *self) {
    edict_t *actor=self->movement.captain_home.roster_actor ? self->movement.captain_home.roster_actor : self->movement.captain_home.actor;
    if (!actor) return;
    wc3Clock_t *due=&self->movement.captain_home.due,next=level.pathing_clock;
    wc3_clock_advance(&next,wc3_float(0x3ba3d70a),0);
    bool ready=next.epoch==due->epoch ? next.time>=due->time : (int32_t)(next.epoch-due->epoch)>0;
    if (!ready) return;
    /* Native0522e0 dispatches at the request's exact deadline, then restores
     * the primary clock. Testing only the published quantum fires5ms late. */
    wc3Clock_t now=level.pathing_clock;
    level.pathing_clock=*due;
    wc3_clock_advance(due,1,0);
    wc3GridPose_t pose,target;
    unit_predicted_pose(self,&pose);unit_predicted_pose(actor,&target);
    /* The range listener follows the admitted actor, independently of the
     * authored home used for the later shared point order. */
    float delta[2];
    FOR_LOOP(k,2) delta[k]=wc3_sub(target.grid[k],pose.grid[k]);
    uint32_t members=actor->movement.captain_actor_members;
    bool logical=self->movement.captain_home.roster_actor!=NULL;
    float radius=wc3_add(wc3_div(wc3_add(800,wc3_mul(25,members)),32),wc3_div(self->collision,32));
    float distance=wc3_add(wc3_mul(delta[0],delta[0]),wc3_mul(delta[1],delta[1]));
    bool entered=distance<wc3_mul(radius,radius),was_entered=self->movement.captain_home.entered;
    if (logical) {
        botCaptain_t *captain=move_actor_captain(actor);
        if(captain && entered!=was_entered) {
            if(entered)captain->entered_members++;
            else captain->entered_members--;
        }
        self->movement.captain_home.entered=entered;
    }
    if (logical || !self->movement.captain_home.active) {
        /* Native d01cd leaves the registered outer circle and replaces the
         * shared point leg with a private virtual-target approach. Retain the
         * creation-phase deadline through the shared leg, including on load. */
        float outer=wc3_add(wc3_div(wc3_add(1000,wc3_mul(25,members)),32),wc3_div(self->collision,32));
        bool outside=distance>=wc3_mul(outer,outer),was_outer=self->movement.captain_home.outer;
        if (logical) self->movement.captain_home.outer=!outside;
        if (outside && (!logical || was_outer) && !self->movement.captain_home.active) {
            /* Original9d8eb0 uses the inner count, not the outer departure
             * count: more than floor(roster/10) missing members updates the
             * complete roster. Already following members retain their heads. */
            uint32_t entered_count=logical ? move_captain_entered(actor) : 0;
            if (logical && members/10<members-entered_count) {
                move_captain_shared_target(actor);
                level.pathing_clock=now;
                return;
            }
            typeof(self->movement.captain_home) retained=self->movement.captain_home;
            /* Transfer the physical reference without briefly releasing the
             * final follower of an actor whose logical captain was retired. */
            self->movement.captain_home.actor=NULL;
            S_TrackMoveTimers(self);
            move_leave(self);
            S_RecoverStoppedUnitPosition(self);
            S_IssueMoveOrder(self,self->goalentity,G_OrderId("move"));
            self->movement.captain_home=retained;
            S_TrackMoveTimers(self);
            self->movement.captain_home.actor=actor;
            S_TrackMoveTimers(self);
            self->movement.captain_home.active=true;
            self->movement.captain_home.entered=false;
            move_start_follow_group(self,actor,true);
        }
        if (!logical || !entered || was_entered) {
            level.pathing_clock=now;
            return;
        }
    }
    if (!logical && entered)
        self->movement.captain_home.entered=true;
    /* Native9d9020 only publishes after bc <= cc+c4. Preserve roster order
     * through the two-pass shared point admission, including after load. */
    uint32_t entered_count=move_captain_entered(actor);
    if (entered_count==members) {
        edict_t **roster=move_captain_collect_roster(actor);
        /* Native9d4600 replaces the moving virtual point request first.
         * Consume its old velocity at the exact callback deadline. */
        if (actor->unitinfo.move_flags&BZ_UNIT_SPEED_SET) {
            botCaptain_t *captain=move_actor_captain(actor);
            if (captain) actor->unitinfo.MoveSpeed=move_captain_point_speed(captain);
            move_captain_actor_point(actor,&self->movement.captain_home.home,200);
        }
        move_captain_shared_point(actor,roster,members,&self->movement.captain_home.home,logical);
        gi.MemFree(roster);
    }
    level.pathing_clock=now;
}

/* Dispatch after a due path owner, before ordinary timer/event actions. Pose
 * sampling is observational and must not replace tasks ahead of that owner. */
void S_RunMoveTimers(void) {
    G_RunCaptainTimers();
    for(uint32_t i=entity_set_next(&move_timer_members,0);i<globals.num_edicts;i=entity_set_next(&move_timer_members,i+1)) {
        edict_t *ent=g_edicts+i;
        if(ent->inuse && (ent->movement.captain_home.actor || ent->movement.captain_home.roster_actor))move_captain_home_update(ent);
    }
    for(uint32_t i=entity_set_next(&move_timer_members,0);i<globals.num_edicts;i=entity_set_next(&move_timer_members,i+1)) {
        edict_t *ent=g_edicts+i;
        if(!ent->inuse || !ent->movement.pause_resume_pending || ent->paused)continue;
        wc3Clock_t const *due=&ent->movement.pause_deadline;
        bool ready=level.pathing_clock.epoch==due->epoch ? level.pathing_clock.time>=due->time :
            (int32_t)(level.pathing_clock.epoch-due->epoch)>0;
        if(!ready)continue;
        uint32_t order=ent->movement.pause_order_id;
        ent->movement.pause_resume_pending=false;ent->movement.pause_order_id=0;
        S_TrackMoveTimers(ent);
        if(ent->goalentity && ent->currentmove==&move_move_walk)
            S_IssueMoveOrder(ent,ent->goalentity,order);
    }
    for(uint32_t i=entity_set_next(&move_timer_members,0);i<globals.num_edicts;i=entity_set_next(&move_timer_members,i+1)) {
        edict_t *ent=g_edicts+i;
        if(!ent->inuse || !ent->movement.type_rebind_pending)continue;
        wc3Clock_t const *due=&ent->movement.type_rebind_deadline;
        bool ready=level.pathing_clock.epoch==due->epoch ? level.pathing_clock.time>=due->time :
            (int32_t)(level.pathing_clock.epoch-due->epoch)>0;
        if (!ready)continue;
        ent->movement.type_rebind_pending=false;
        S_TrackMoveTimers(ent);
        if (ent->currentmove==&move_move_walk && ent->goalentity)
            S_IssueMoveOrder(ent,ent->goalentity,ent->current_order_id);
    }

}

/* Queries predict from the retained fine pose without committing its time origin. */
void S_PublishMovement(edict_t *self) {
    if (!self->movement.clock_valid || !self->movement.pose_valid) return;
    if (self->paused || self->stunned) {
        self->movement.fine_pose = self->movement.sampled_pose;
        self->movement.pose_clock = level.pathing_clock;
        return;
    }
    wc3GridPose_t pose;
    unit_predicted_pose(self, &pose);
    vec2_t point = {pose.world[0], pose.world[1]};
    self->movement.sampled_pose = (vec2_t){pose.grid[0], pose.grid[1]};
    if (wc3_float_bits(point.x) == wc3_float_bits(self->s.origin2.x) &&
        wc3_float_bits(point.y) == wc3_float_bits(self->s.origin2.y)) return;
    if (self->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    self->s.origin2 = self->movement.pose_world = point;
    gi.LinkEntity(self);
}

/* A native write first commits the old velocity at the current clock. */
static void unit_commit_current_pose(edict_t *self) {
    wc3GridPose_t pose;
    unit_grid_pose(self, &pose);
    bool clocked = self->movement.clock_valid;
    uint32_t blocked = self->movement.worker_avoid_blocked_frames;
    if (clocked) {
        float velocity[2] = {self->movement.velocity.x, self->movement.velocity.y};
        wc3_grid_step(&pose, velocity, wc3_elapsed(&level.pathing_clock, &self->movement.pose_clock));
    }
    unit_commit_pose(self, &pose);
    self->movement.worker_avoid_blocked_frames = blocked;
    if (clocked) {
        self->movement.pose_clock = level.pathing_clock;
        self->movement.clock_valid = true;
    }
}

/* Derived intrusive-link slots keep the serialized owner order and make
 * retirement O(1). Neither process pointers nor this cache enter saves. */
static edict_t **move_repulse_links[MAX_ENTITIES];
static edict_t *move_repulse_link_base,*move_repulse_link_head;
static bool move_repulse_links_valid;

static void move_repulse_clear_links(void) {
    memset(move_repulse_links,0,sizeof(move_repulse_links));
    move_repulse_link_base=move_repulse_link_head=NULL;move_repulse_links_valid=false;
}

bool S_RestoreMoveRepulsors(void) {
    move_repulse_clear_links();
    edict_t **link=&level.repulse_head;
    while(*link) {
#ifdef BZ_TESTS
        move_test_repulse_unlink_visits++;
#endif
        uintptr_t delta=(uintptr_t)*link-(uintptr_t)g_edicts;
        uint32_t index=delta/sizeof(*g_edicts);
        if(!g_edicts || delta%sizeof(*g_edicts) || delta/sizeof(*g_edicts)>=globals.num_edicts ||
            move_repulse_links[index] || !(*link)->inuse || !(*link)->movement.repulse.active)goto failed;
        move_repulse_links[index]=link;link=&(*link)->movement.repulse.next;
    }
    FOR_LOOP(i,globals.num_edicts) {
#ifdef BZ_TESTS
        move_test_repulse_unlink_visits++;
#endif
        if(g_edicts[i].movement.repulse.active && (!g_edicts[i].inuse || !move_repulse_links[i]))goto failed;
    }
    move_repulse_link_base=g_edicts;move_repulse_link_head=level.repulse_head;move_repulse_links_valid=true;return true;
failed:
    move_repulse_clear_links();return false;
}

static void move_repulse_prepare_links(void) {
    if(move_repulse_links_valid && move_repulse_link_base==g_edicts && move_repulse_link_head==level.repulse_head)return;
    if(!level.repulse_head) {
        /* An empty runtime list needs no whole-edict discovery. Load uses the
         * full validator separately, including orphan active records. */
        move_repulse_clear_links();move_repulse_link_base=g_edicts;move_repulse_links_valid=true;return;
    }
    if(!S_RestoreMoveRepulsors())gi.error("Move repulsors: invalid owner membership");
}

/* Membership survives idle/attack orders and is removed before freeing or rebinding an actor. */
static void move_repulse_unlink(edict_t *self) {
    if(self->movement.repulse.active) {
        move_repulse_prepare_links();
        uint32_t index=self-g_edicts;edict_t **link=move_repulse_links[index];
        assert(link && *link==self);
#ifdef BZ_TESTS
        move_test_repulse_unlink_visits++;
#endif
        *link=self->movement.repulse.next;
        if(*link)move_repulse_links[*link-g_edicts]=link;
        move_repulse_links[index]=NULL;move_repulse_link_head=level.repulse_head;
    }
    memset(&self->movement.repulse,0,sizeof(self->movement.repulse));
}

/* Original1710e0 replaces the old repulsor and inserts the new object at the list head. */
static void move_repulse_init(edict_t *self) {
    if (self->movement.repulse.active) move_repulse_unlink(self);
    UnitBalance_t const *balance = self->data.UnitBalance;
    /* Native66fc50 is independent of the authored Move ability: zero-speed
     * units still own separation, participate in queries and can be displaced. */
    if (!balance || !balance->repulse || self->paused ||
        S_SpellIsChanneling(self)) return;
    move_repulse_prepare_links();
    uint32_t category = wc3_repulse_category(self->s.player,balance->repulseGroup,false);
    self->movement.repulse.state.packed = wc3_repulse_policy(0,balance->repulseParam,category,balance->repulsePrio);
    self->movement.repulse.active = true;
    self->movement.repulse.next = level.repulse_head;
    if(level.repulse_head)move_repulse_links[level.repulse_head-g_edicts]=&self->movement.repulse.next;
    level.repulse_head=self;move_repulse_links[self-g_edicts]=&level.repulse_head;
    move_repulse_link_head=level.repulse_head;
    /* Mechanical Critter's flag60 bit0 producer and counted work suppression
     * remain in SEP-01.2; merely adding Amec must not force category15. */
}

/* Predict from the committed fine pose without consuming its clock or velocity. */
static void unit_predicted_pose_raw(edict_t const *self, wc3GridPose_t *pose) {
    unit_grid_pose(self,pose);
    if (self->movement.clock_valid) {
        float velocity[2] = {self->movement.velocity.x,self->movement.velocity.y};
        wc3_grid_step(pose,velocity,wc3_elapsed(&level.pathing_clock,&self->movement.pose_clock));
    }
}

static void unit_predicted_pose(edict_t const *self, wc3GridPose_t *pose) {
    box2_t bounds=CM_GetWorldBounds();
    movePoseKey_t key={.world=self->s.origin2,.fine=self->movement.fine_pose,
        .published=self->movement.pose_world,.origin=bounds.min,.velocity=self->movement.velocity,
        .now=level.pathing_clock,.committed=self->movement.pose_clock,
        .flags=(self->movement.pose_valid ? 1u : 0) | (self->movement.clock_valid ? 2u : 0)};
    /* Zero velocity contributes exactly zero through the retail integer
     * scalar multiply, including signed zero and an epoch crossing. Keep the
     * original calculation on a miss; time cannot change its result. */
    if(!self->movement.clock_valid || (!key.velocity.x && !key.velocity.y))
        key.now=key.committed=(wc3Clock_t){0};
    movePoseCache_t *entry=move_pose_cache+(uint32_t)self->s.number%MAX_ENTITIES;
    if(entry->valid && entry->unit==self && !memcmp(&entry->key,&key,sizeof(key))) {
#ifdef BZ_TESTS
        move_pose_cache_hits++;
#endif
        *pose=entry->pose;return;
    }
#ifdef BZ_TESTS
    move_pose_cache_misses++;
#endif
    unit_predicted_pose_raw(self,pose);
    *entry=(movePoseCache_t){.unit=self,.key=key,.pose=*pose,.valid=true};
}

/* Admitted Move publishes nonzero requested speed with its committed velocity;
 * turn/retry stops clear both components. Native c0 excludes moving repulsors. */
static bool move_repulse_moving(edict_t const *unit) {
    return unit->movement.velocity.x!=0 || unit->movement.velocity.y!=0;
}

/* The callback only accumulates; endpoint application precedes this query on the next eligible visit. */
static bool move_repulse_candidate(edict_t const *other) {
    moveRepulseQuery_t *query = repulse_query;
    uint32_t word = other->movement.repulse.state.packed;
    if (other == query->self || !other->movement.repulse.active || !G_UnitIsWorldActive(other) ||
        IS_HOLLOW(other) || other->collision <= 0 || move_repulse_moving(other) || other->paused || other->stunned || other->no_pathing ||
        ((word >> 20) & 255) != query->category || (word >> 28) < query->rank) return false;
    wc3GridPose_t pose; unit_predicted_pose(other,&pose);
    for (unsigned i = 0; i < 2; i++) query->pair.other[i] = pose.grid[i];
#ifdef BZ_TESTS
    wc3Repulse_t before=query->self->movement.repulse.state;
    wc3Random_t random_before=level.pathing_random;
#endif
    wc3_repulse_pair(&query->self->movement.repulse.state,&query->pair);
#ifdef BZ_TESTS
    if(move_test_repulse_pair)move_test_repulse_pair(query->self,other,&query->pair,&before,random_before);
#endif
    return false;
}

/* Retained displacement is an endpoint admission, not a swept collision/slide or a replacement order. */
static void move_repulse_update(edict_t *self) {
    wc3Repulse_t *state = &self->movement.repulse.state;
    if (wc3_repulse_cooldown(state)) return;
    if (move_repulse_moving(self) || self->paused || self->stunned) {
        state->vector[0] = state->vector[1] = 0; state->packed = (state->packed & 0xffff0000u) | 7; return;
    }
    wc3GridPose_t pose; unit_predicted_pose(self,&pose);
    wc3GridPose_t next = pose;
    for (unsigned i = 0; i < 2; i++) next.grid[i] = wc3_add(next.grid[i],state->vector[i]);
    for (unsigned i = 0; i < 2; i++) next.world[i] = wc3_world_coordinate(next.grid[i],next.origin[i],32);
    vec2_t point = {next.world[0],next.world[1]}, old = self->s.origin2;
    movePathQuery_t endpoint = {{&point,NULL,self->collision,M_UnitStaticPathingFlags(self)},self,NULL,true};
    float sq = wc3_add(wc3_mul(state->vector[0],state->vector[0]),wc3_mul(state->vector[1],state->vector[1]));
    bool admitted=sq != 0 && G_UnitMovePathFinePointIsPathable(&endpoint,next.grid);
#ifdef BZ_TESTS
    if(move_test_repulse_endpoint)move_test_repulse_endpoint(self,next.grid,sq != 0,admitted);
#endif
    if (admitted) {
        /* Original05c820 subtracts the predicted position before15f7b0 adds the delta back. */
        for (unsigned i = 0; i < 2; i++) {
            next.grid[i] = wc3_add(pose.grid[i],wc3_sub(next.grid[i],pose.grid[i]));
            next.world[i] = wc3_world_coordinate(next.grid[i],next.origin[i],32);
        }
        unit_commit_pose(self,&next); pose = next;
        G_UnitPositionChanged(self,&old);
    }
    wc3RepulseConfig_t config = wc3_repulse_config(state->packed);
    moveRepulseQuery_t query = {.self=self,.pair={.source={pose.grid[0],pose.grid[1]},
        .config=config,.random=&level.pathing_random},.category=(state->packed >> 20) & 255,.rank=state->packed >> 28};
    repulse_query = &query;
    S_QueryMoveProximity(self,pose.grid,config.radius,move_repulse_candidate);
    repulse_query = NULL;
    wc3_repulse_tail(state,&config);
}

/* Original15aa80 toggles parity first, then visits every other linked repulsor after movement. */
static void move_repulse_owner_update(void) {
    level.repulse_phase ^= 1;
    unsigned skip = level.repulse_phase;
    for (edict_t *self = level.repulse_head, *next; self; self = next) {
        next = self->movement.repulse.next;
        if (!G_UnitIsWorldActive(self) || IS_HOLLOW(self)) { move_repulse_unlink(self); continue; }
        if (skip) { skip--; continue; }
#ifdef BZ_TESTS
        moveRepulseTrace_t before={.state=self->movement.repulse.state,.point=self->movement.fine_pose,.owner=level.pathing_random};
#endif
        move_repulse_update(self); skip = 1;
#ifdef BZ_TESTS
        if(move_test_repulse)move_test_repulse(self,&before);
#endif
    }
}

/* Original scripted pause admits a suspension head, stopping the physical
 * task while retaining the user's point order beneath it. Resume is delayed
 * by the original two primary quanta, then builds from the current pose. */
void S_SetUnitPaused(edict_t *self, bool paused) {
    if (!self || self->paused == paused) return;
    if (paused) {
        uint32_t order=self->current_order_id;
        bool point=self->currentmove==&move_move_walk && self->goalentity &&
            (order==G_OrderId("move") || order==G_OrderId("smart"));
        if (point) {
            move_leave(self); move_reset_progress(self);
            self->movement.pause_order_id=order;
            self->current_order_id=MOVE_ORDER_SUSPENDED;
        } else if(self->movement.clock_valid) {
            unit_commit_current_pose(self);
            self->movement.velocity=(vec2_t){0};self->movement.clock_valid=false;
        }
    } else if(self->movement.pause_order_id) {
        self->movement.pause_resume_pending=true;
        S_TrackMoveTimers(self);
        self->movement.pause_deadline=level.pathing_clock;
        FOR_LOOP(i,2)wc3_clock_advance(&self->movement.pause_deadline,wc3_float(0x3ba3d70a),0);
    }
    self->paused=paused;
    /* Native66fc50 disables separation while suspension depth54 or scripted
     * flag5c.200000 is set;693d50 retires/recreates the repulsor at transition. */
    if(paused)move_repulse_unlink(self);
    else move_repulse_init(self);
}

/* A different behavior must not inherit the previous Move's prediction velocity. */
static void move_leave(edict_t *self) {
    self->movement.pause_order_id=0;self->movement.pause_resume_pending=false;
    S_TrackMoveTimers(self);
    move_detach_group(self);
    self->movement.group_id=0;
    move_release_captain_reference(self);
    self->movement.point_forced_arrival=false;
    /* Native171340 sets the sentinel destination through168b80: counts and
     * indices are invalidated without releasing the path's owned storage. */
    moveFineRoute_t *route=&self->movement.fine_route;
    route->count=route->adaptive_count=route->group_count=0;
    route->index=route->adaptive_index=route->group_index=UINT32_MAX;
    route->partial=false;
    self->movement.path.valid=false;
    if (!self->movement.clock_valid) return;
    unit_commit_current_pose(self);
    self->movement.velocity = (vec2_t){0};
    self->movement.clock_valid = false;
}

/* Retail axis setters reproject both coordinates through the predicted fine pose. */
void S_SetUnitAxisPosition(edict_t *self, uint32_t axis, float value) {
    wc3GridPose_t pose;
    uint32_t blocked = self->movement.worker_avoid_blocked_frames;
    bool clocked = self->movement.clock_valid;
    if (clocked) unit_commit_current_pose(self);
    unit_grid_pose(self, &pose);
    float point[2] = {pose.world[0], pose.world[1]}; point[axis] = value;
    wc3_grid_place(&pose, point); unit_commit_pose(self, &pose);
    self->movement.pose_clock = level.pathing_clock;
    if (clocked) {
        self->movement.clock_valid = true;
    }
    self->movement.worker_avoid_blocked_frames = blocked;
}

/* Stop's bounded recovery uses the native fine pose; a world round trip loses low bits. */
void S_RecoverStoppedUnitPosition(edict_t *self) {
    wc3GridPose_t pose; unit_grid_pose(self,&pose);
    vec2_t fine={pose.grid[0],pose.grid[1]}, admitted;
    if (!G_FindUnitMoveRecoveryPosition(self,&fine,&admitted)) return;
    float point[2]={admitted.x,admitted.y};
    wc3_grid_place_fine(&pose,point); unit_commit_pose(self,&pose);
}

/* Portal movement keeps the order, route buffers and current velocity. */
bool S_MoveThroughPortal(edict_t *self,vec2_t const *fine) {
    vec2_t admitted;
    if(!G_FindUnitMovePortalPosition(self,fine,&admitted))return false;
    wc3GridPose_t pose;unit_predicted_pose(self,&pose);
    vec2_t old=self->s.origin2;
    float point[2]={admitted.x,admitted.y};
    wc3_grid_place_fine(&pose,point);unit_commit_pose(self,&pose);
    G_UnitPositionChanged(self,&old);
    return true;
}

/* Public ground spawn admits first, then commits from the fresh mover sentinel.
 * Applying the normal setter to an already inverted requested pose loses the
 * original initialization cancellation and publishes different fractional XY. */
void S_InitUnitPosition(edict_t *self, vec2_t const *requested) {
    if (M_UnitMoveDisabled(self)) return;
    vec2_t old = self->s.origin2, point;
    if (!G_FindUnitPlacementPosition(self,requested,&point))
        fprintf(stderr,"WC3 CreateUnit: no legal point for %08x at (%.9g, %.9g); retaining requested position\n",self->class_id,requested->x,requested->y);
    box2_t const bounds = CM_GetWorldBounds();
    /* The retail factory overwrites both fine axes with its sentinel before
     * either placement write. Only the map origin survives that initialization. */
    wc3GridPose_t pose = {.origin = {bounds.min.x,bounds.min.y}};
    float world[2] = {point.x,point.y};
    wc3_grid_spawn_place(&pose,world);
    unit_commit_pose(self,&pose);
    self->movement.pose_clock = level.pathing_clock;
    self->movement.clock_valid = false;
    G_UnitPositionChanged(self,&old);
}

/* Both public placement natives replace the order before admitting position. */
void S_SetUnitPosition(edict_t *self, vec2_t const *requested) {
    if (!self || !requested) return;
    vec2_t old_position = self->s.origin2, position;
    order_stop(self);
    self->movement.velocity = (vec2_t){0};
    self->movement.clock_valid = false;
    G_FindUnitPlacementPosition(self, requested, &position);
    wc3GridPose_t pose;
    unit_grid_pose(self, &pose);
    float point[2] = {position.x, position.y};
    wc3_grid_place(&pose, point);
    unit_commit_pose(self, &pose);
    self->movement.clock_valid = false;
    self->movement.pose_clock = level.pathing_clock;
    move_reset_progress(self);
    G_UnitPositionChanged(self, &old_position);
}

/* Native scheduled Move integrates old velocity before requesting its new heading.
 * Other owners retain their existing snapshot-step contract until separately measured. */
static vec2_t unit_step_heading(edict_t *self, float angle, moveStep_t *step) {
    float speed = unit_current_speed(self);
    wc3Velocity_t *v = &step->velocity;
    *v = (wc3Velocity_t){ .vel = {self->movement.velocity.x, self->movement.velocity.y},
        .speed = speed, .heading = angle, .limit = speed };
    unit_grid_pose(self, &step->pose);
    if (level.scheduled_think) {
        float old[2] = {self->movement.velocity.x, self->movement.velocity.y};
        float elapsed = self->movement.clock_valid ?
            wc3_elapsed(&level.pathing_clock, &self->movement.pose_clock) : 0;
        wc3_grid_step(&step->pose, old, elapsed);
        wc3_velocity_update_world(v);
    } else {
        wc3_velocity_update_world(v);
        wc3_grid_step(&step->pose, v->vel, 10.0f / FRAMETIME);
    }
    return (vec2_t){step->pose.world[0], step->pose.world[1]};
}

/* Retail160060 commits facing from velocity. Only accepted candidates retain the previewed fine pose. */
static void unit_commit_motion(edict_t *self, moveStep_t const *step) {
    wc3Velocity_t const *v = &step->velocity;
    self->movement.velocity = (vec2_t){v->vel[0], v->vel[1]};
    float grid_x = wc3_mul(v->vel[0], wc3_float(0x3d000000));
    float grid_y = wc3_mul(v->vel[1], wc3_float(0x3d000000));
    self->s.angle = v->speed > 0 ? wc3_velocity_heading(grid_x, grid_y, self->s.angle) : wc3_facing_angle(v->heading);
    unit_commit_pose(self, &step->pose);
    G_UnitRegionPositionChanged(self,&self->s.origin2);
    if (self->movement.route_resume_active && self->movement.route_resume_goal &&
        self->movement.route_resume_goal->inuse) {
        self->movement.route_resume_time = level.time;
        self->movement.route_resume_goal_origin = self->movement.route_resume_goal->s.origin2;
    }
#ifdef BZ_TESTS
    if (level.scheduled_think && move_test_motion_commit) move_test_motion_commit(self);
#endif
}

/* Advance the unit one tick.  Avoidance is decided ONCE per tick in
 * unit_changeangle (which picks a free heading via unit_desired_heading and
 * turns the facing toward it); this function only commits the step.  WC3 moves a
 * unit ALONG ITS FACING, so we try the facing first; if the facing momentarily
 * lags into an obstacle while it is still turning toward the chosen heading, we
 * fall back to that already-validated heading so the unit keeps progressing
 * around the obstacle instead of stalling.
 *
 * We deliberately do NOT run a second deflection search here.  The previous
 * version searched +/- slide rings off the (turn-rate-lagged) facing, which
 * disagreed with the heading unit_changeangle had already chosen and re-decided
 * a different direction every tick — that disagreement is what made units
 * visibly rotate/wobble and crab sideways past each other and trees. */
static void unit_moveindirection_policy(edict_t *self,
                                        moveCollisionPolicy_t collision_policy) {
    if ((self->aiflags & AI_IMMOBILE) || self->movement.turn_blocked) {
        /* Preserve upstream's Stand clip while using retail's pre-turn gate. */
        if (self->movement.turn_blocked && !G_AnimationHasPrimary(self->animation,"stand"))
            unit_setanimation(self,"stand");
        /* Original1603d0 integrates the previous velocity before publishing a turn-induced stop. */
        if (level.scheduled_think && self->movement.turn_blocked && !(self->aiflags & AI_IMMOBILE)) {
            unit_commit_current_pose(self);
            self->s.angle = wc3_facing_angle(self->s.angle);
        }
        self->movement.velocity = (vec2_t){0};
#ifdef BZ_TESTS
        if (level.scheduled_think && self->movement.turn_blocked && !(self->aiflags & AI_IMMOBILE) && move_test_motion_commit)
            move_test_motion_commit(self);
#endif
        move_route_wait_diag(self, false, MOVE_DIAG_NONE);
        return;
    }

    /* unit_changeangle* clears both routing fields before resolving this
     * tick's heading.  A resumable cache miss deliberately leaves both clear;
     * in that state there is no valid movement decision yet.  Never commit a
     * step using the unit's previous facing/heading while the requested route
     * is still being built.  This is the common safety net for Move, Harvest,
     * Patrol, Attack, Build, Repair, and resource-return walkers. */
    if (!self->movement.flow_direct && !self->movement.path.valid && self->movement.flow_generation == 0 &&
        !self->movement.route_resume_active) {
        self->movement.velocity = (vec2_t){0};
        move_route_wait_diag(self, true, MOVE_DIAG_ROUTE_WAIT);
        return;
    }

    if (!G_AnimationHasPrimary(self->animation,"walk")) unit_setanimation(self,"walk");
    moveStep_t motion;
    vec2_t facing_dir, heading_dir;
    wc3_sincos(self->s.angle, &facing_dir.y, &facing_dir.x);
    wc3_sincos(self->movement.heading, &heading_dir.y, &heading_dir.x);
    vec2_t const origin = self->s.origin2;
    vec2_t const progress_goal = self->movement.displacement_active ? self->movement.displacement_target :
        self->goalentity ? self->goalentity->s.origin2 : self->s.origin2;
    vec2_t const by_facing = unit_step_heading(self, self->s.angle, &motion);
    bool const facing_progress = !self->goalentity ||
        Vector2_distance(&by_facing, &progress_goal) <=
        Vector2_distance(&origin, &progress_goal) + 0.001f;
    /* Point orders must progress toward their reserved destination. Ranged
     * interaction orders follow an approach route around a blocked target;
     * applying the centre-distance guard there reversed a lumber worker at
     * the flow endpoint before it could reach the dropoff boundary. */
    if ((!unit_routes_to_location(self) ||
         (Vector2_dot(&facing_dir, &heading_dir) >= 0.0f && facing_progress)) &&
        move_is_valid_policy(self, &by_facing, collision_policy)) {
        unit_commit_motion(self, &motion);
        move_route_wait_diag(self, false, MOVE_DIAG_NONE);
        return;
    }
    vec2_t const by_heading = unit_step_heading(self, self->movement.heading, &motion);
    if (move_is_valid_policy(self, &by_heading, collision_policy)) {
        unit_commit_motion(self, &motion);
        move_route_wait_diag(self, false, MOVE_DIAG_NONE);
        return;
    }
    self->movement.velocity = (vec2_t){0};
}

void unit_moveindirection(edict_t *self) {
    unit_moveindirection_policy(self,
        S_UnitStatusAbilityEvent(self, A_MOVE_COLLISION_QUERY, NULL) ?
        MOVE_IGNORE_UNITS : MOVE_COLLIDE_UNITS);
}

void unit_moveindirection_ignore_units(edict_t *self) {
    unit_moveindirection_policy(self, MOVE_IGNORE_UNITS);
}

/* Interaction routing may finish at a collision-safe staging point rather
 * than at the blocked building centre.  When that endpoint is within this
 * tick's movement budget, land exactly on it instead of stepping past it and
 * selecting it again from the opposite side next think.  This is the same
 * arrival snap used by ordinary Move, but deliberately ignores live units for
 * Warsmash-style Mine/drop-off legs while retaining all static pathing. */
bool unit_snap_to_point_ignore_units(edict_t *self, vec2_t const *point) {
    if (!self || !point || (self->aiflags & AI_IMMOBILE))
        return false;
    if (Vector2_distance(&self->s.origin2, point) > unit_movedistance(self) + 0.001f)
        return false;
    if (!move_is_valid_policy(self, point, MOVE_IGNORE_UNITS))
        return false;
    unit_commit_step(self, point);
    return true;
}

/* Retail's stock constructor and native setter share normalization and the minimum turn rate. */
float unit_turnspeed(edict_t const *self) {
    if (self->unitinfo.move_flags & BZ_UNIT_TURN_SET) return self->unitinfo.TurnSpeed;
    return MAX(wc3_float(0x3a83126f), wc3_angle(self->data.UnitData->turnRate));
}

/* 6785d0 converts authored degrees with the stored scalar before the 05c890 setter normalizes them. */
float unit_propwindow(edict_t const *self) {
    if (self->unitinfo.move_flags & BZ_UNIT_WINDOW_SET) return self->unitinfo.PropWindow;
    return wc3_angle(wc3_mul(self->data.UnitData->propWin, wc3_float(0x3c8efa35)));
}

/* Use retail's scalar turn update instead of accumulating host sin/cos rotation error. */
static void unit_turn_toward(edict_t *self, float target) {
    wc3Motion_t motion = { .heading = self->s.angle, .error = wc3_turn_error(target, self->s.angle),
        .turn = unit_turnspeed(self), .window = unit_propwindow(self) };
    /* Retail stops from the error before turning; testing the new angle allowed premature travel. */
    self->movement.turn_blocked = !wc3_motion_update(&motion);
    self->s.angle = motion.heading;
}

/* Resource workers need a different local crowd rule from ordinary combat
 * movement.  A same-direction worker is a queue, not an obstacle to weave
 * around; crossing traffic may pass immediately.  This is the minimal policy
 * that stayed close to the direct Human02 resource corridor in the 30-worker
 * simulation while still breaking counterflow deadlocks. */
static bool unit_worker_same_stream(edict_t const *blocker, float goal_angle) {
    vec2_t dir, goal;
    float len;

    if (!blocker || !blocker->currentmove || !blocker->goalentity ||
        (blocker->aiflags & AI_IMMOBILE))
        return false;
    dir = Vector2_sub(&blocker->goalentity->s.origin2, &blocker->s.origin2);
    len = Vector2_len(&dir);
    if (len <= 0.001f)
        return false;
    goal = MAKE(vec2_t, cosf(goal_angle), sinf(goal_angle));
    return Vector2_dot(&goal, &dir) / len > 0.25f;
}

static float unit_worker_lateral_deviation(edict_t const *self, vec2_t const *point) {
    vec2_t const delta = Vector2_sub(point, &self->movement.worker_avoid_origin);
    vec2_t const direct = { cosf(self->movement.worker_avoid_heading),
                             sinf(self->movement.worker_avoid_heading) };
    return fabsf(direct.x * delta.y - direct.y * delta.x);
}

static float unit_worker_desired_heading(edict_t *self, float goal_angle, float dist) {
    vec2_t const straight = Vector2_mad(&self->s.origin2, dist,
                                         &MAKE(vec2_t, cosf(goal_angle), sinf(goal_angle)));
    edict_t *blocker;
    float max_deviation;

    if (move_is_valid(self, &straight)) {
        self->movement.worker_avoid_blocked_frames = 0;
        self->movement.worker_avoid_active = false;
        return goal_angle;
    }

    blocker = trymove_blocker;
    if (!self->movement.worker_avoid_active ||
        fabsf(angle_wrap(goal_angle - self->movement.worker_avoid_heading)) >
            MOVE_WORKER_CORRIDOR_RESET) {
        self->movement.worker_avoid_origin = self->s.origin2;
        self->movement.worker_avoid_heading = goal_angle;
        self->movement.worker_avoid_blocked_frames = 0;
        self->movement.worker_avoid_active = true;
    }
    self->movement.worker_avoid_blocked_frames++;

    /* Do not turn a short pause in a resource stream into overtaking.  Four
     * blocked decisions let the queue advance naturally; a genuinely pinned
     * queue then gets the same bounded escape used for crossing traffic. */
    if (unit_worker_same_stream(blocker, goal_angle) &&
        self->movement.worker_avoid_blocked_frames <= MOVE_WORKER_QUEUE_TICKS)
        return goal_angle;

    max_deviation = self->collision *
        (self->movement.worker_avoid_blocked_frames <= MOVE_WORKER_ESCAPE_TICKS
            ? MOVE_WORKER_MAX_DEVIATION : MOVE_WORKER_ESCAPE_DEVIATION);

    /* Deterministic right-hand passing avoids the +/- re-decision that made
     * packed Peasants dance.  Retry the exact direct heading next think; no
     * passing lane is cached. */
    for (int sign = -1; sign <= 1; sign += 2) {
        for (int ring = 1; ring <= MOVE_SLIDE_RINGS; ring++) {
            float const angle = angle_wrap(goal_angle + sign * ring * MOVE_SLIDE_STEP);
            vec2_t const cand = Vector2_mad(&self->s.origin2, dist,
                                             &MAKE(vec2_t, cosf(angle), sinf(angle)));
            if (unit_worker_lateral_deviation(self, &cand) > max_deviation)
                continue;
            if (move_is_valid(self, &cand)) {
                /* A legal passing step may still require turning. Only a committed step
                 * or cleared direct corridor resets the queue, so this turn can finish. */
                return angle;
            }
        }
    }
    return goal_angle;
}

/* Pick the heading the unit actually wants to move along this tick.  Generic
 * units retain speed-priority block-and-slide; resource workers use the
 * queue/pass-right policy above. */
static float unit_desired_heading(edict_t *self, float goal_angle, float dist,
                                  moveAvoidPolicy_t policy) {
    moveCollisionPolicy_t const collision_policy =
        (policy == MOVE_AVOID_STATIC_ONLY ||
         S_UnitStatusAbilityEvent(self, A_MOVE_COLLISION_QUERY, NULL)) ?
        MOVE_IGNORE_UNITS : MOVE_COLLIDE_UNITS;
    vec2_t const straight = Vector2_mad(&self->s.origin2, dist,
                                         &MAKE(vec2_t, cosf(goal_angle), sinf(goal_angle)));
    if (policy == MOVE_AVOID_RESOURCE_WORKER)
        return unit_worker_desired_heading(self, goal_angle, dist);
    if (move_is_valid_policy(self, &straight, collision_policy))
        return goal_angle;

    int max_rings = MOVE_SLIDE_RINGS;
    edict_t *const b = trymove_blocker;
    if (b && unit_is_walking(self) && unit_is_walking(b) &&
        unit_current_speed(self) > unit_current_speed(b)) {
        max_rings = MOVE_SLIDE_RINGS_YIELD;
    }
    routeSlide_t slide = { .ent = self, .angle = goal_angle, .dist = dist, .rings = max_rings,
        .valid = collision_policy == MOVE_IGNORE_UNITS ? move_static_is_valid : move_is_valid };
    return CM_SlideRoute(&slide);
}

/* Countdown and fine-retry callers restore the final destination before
 * turning. Keep subtraction in native coordinates, before world publication. */
static void move_hold_goal_heading(edict_t *self) {
    wc3GridPose_t pose; unit_predicted_pose(self,&pose);
    moveGroupMember_t const *member=move_find_member(self);
    float x=member ? member->destination.x : wc3_grid_coordinate(self->goalentity->s.origin2.x,pose.origin[0],32);
    float y=member ? member->destination.y : wc3_grid_coordinate(self->goalentity->s.origin2.y,pose.origin[1],32);
    moveFineRoute_t const *route=&self->movement.fine_route;
    if (!member && route->group_count && route->group_index<route->group_count) {
        vec2_t point=route->group_index ? (vec2_t){wc3_mul(route->group_points[route->group_index].x,2),wc3_mul(route->group_points[route->group_index].y,2)} : route->group_goal;
        x=point.x;y=point.y;
    }
    float heading=wc3_vector_heading(wc3_sub(x,pose.grid[0]),wc3_sub(y,pose.grid[1]));
    self->movement.heading=heading; unit_turn_toward(self,heading);
    self->movement.turn_blocked=true;
}

/* Both real retry callers share the saved owner; observers never consume draws. */
static uint32_t move_advance_retry(edict_t *unit,wc3RetryInput_t const *input) {
#ifdef BZ_TESTS
    moveRetryTrace_t before={.input=*input,.owner=level.pathing_random,.count=unit->movement.retry_count};
#endif
    uint32_t result=wc3_retry_advance(&unit->movement.retry_count,input,&level.pathing_random);
#ifdef BZ_TESTS
    before.result=result;
    if(move_test_retry)move_test_retry(unit,&before);
#endif
    return result;
}

static uint32_t move_retry_members(edict_t const *self) {
    uint32_t count=0;
    /* A physical owner contains every actor assigned its unique ID. Count
     * live rows, including paused members, before the next pruning pass. */
    moveGroup_t const *group=move_unit_group(self);
    if(group) {
        FOR_LOOP(i,group->count) {
#ifdef BZ_TESTS
            move_retry_member_visits++;
#endif
            edict_t const *peer=group->members[i].unit;
            if(peer->inuse && peer->movement.group_id==group->id)count++;
        }
        return count;
    }
    /* Larger/mixed legacy selections have an ID without a physical owner. */
    FOR_LOOP(i,globals.num_edicts) {
#ifdef BZ_TESTS
        move_retry_member_visits++;
#endif
        edict_t const *peer=g_edicts+i;
        if(peer->inuse && peer->movement.group_id==self->movement.group_id)count++;
    }
    return count;
}

/* Original166c30 publishes the reconstructed coarse endpoint to path+24
 * when it differs from the requested destination. Both retry callers read
 * that same adjusted goal, even before the coarse index reaches zero. The
 * retained first point already owns these words; no new saved cache is needed. */
static vec2_t move_retry_goal(moveFineRoute_t const *route,vec2_t requested) {
    if (route->adaptive_count)
        return (vec2_t){wc3_mul(route->adaptive_points[0].x,2),wc3_mul(route->adaptive_points[0].y,2)};
    return requested;
}

/* Original167290 resets the fine leg only; retaining the coarse route lets
 * the following thinker refill around the peer that has now stopped. */
static void move_retry_fine(edict_t *self) {
    moveFineRoute_t *route=&self->movement.fine_route;
    wc3GridPose_t pose; unit_predicted_pose(self,&pose);
    moveGroupMember_t const *member=move_find_member(self);
    vec2_t goal=move_retry_goal(route,member ? member->destination : route->points[0]);
    wc3RetryInput_t in={{pose.grid[0],pose.grid[1]},{goal.x,goal.y},1};
    /* TODO GROUP: engine cohorts supply members until the original group
     * activation/membership producer replaces the current selection owner. */
    if (self->movement.group_id) {
        in.members=move_retry_members(self);
    }
    uint32_t result=move_advance_retry(self,&in);
    assert(result==1); /* admitted fine progress clears the budget before collection */
    route->count=0; route->index=UINT32_MAX;
    self->movement.path.valid=false;
    move_hold_goal_heading(self);
}

/* Original165c60 consumes a reached partial endpoint before the next refill;
 *167290 preserves every buffer on terminal4 and resets only fine on retry1. */
static uint32_t move_retry_endpoint(edict_t *unit, wc3GridPose_t const *pose, vec2_t goal, uint32_t members) {
    moveFineRoute_t *route=&unit->movement.fine_route;
    if (!route->partial || !route->count || route->index) return 0;
    float x=wc3_sub(route->points[0].x,pose->grid[0]),y=wc3_sub(route->points[0].y,pose->grid[1]);
    float range=wc3_float(0x3efae148);
    if (wc3_add(wc3_mul(x,x),wc3_mul(y,y))>wc3_mul(range,range)) return 0;
    /* Failed adaptive reconstruction publishes its retained endpoint to the
     * adjusted fine goal used by1689d0, independently of the user click. */
    goal=move_retry_goal(route,goal);
    wc3RetryInput_t in={{pose->grid[0],pose->grid[1]},{goal.x,goal.y},members};
    uint32_t result=move_advance_retry(unit,&in);
    if (result!=4) {route->count=0;route->index=UINT32_MAX;unit->movement.path.valid=false;}
    return result;
}

static bool move_point_retry_endpoint(edict_t *unit) {
    if (!level.scheduled_think || move_find_member(unit) || !unit->movement.fine_route.group_count ||
        (unit->current_order_id!=G_OrderId("move") && unit->current_order_id!=G_OrderId("smart"))) return false;
    wc3GridPose_t pose;unit_predicted_pose(unit,&pose);
    vec2_t goal=unit->movement.fine_route.group_goal;
    uint32_t result=move_retry_endpoint(unit,&pose,goal,1);
    if (!result) return false;
    if (result==4) unit->movement.point_forced_arrival=true;
    move_hold_goal_heading(unit);
    return true;
}

static void unit_apply_heading(edict_t *self, vec2_t const *dir, moveAvoidPolicy_t policy) {
    if (policy==MOVE_AVOID_GENERIC && move_point_retry_endpoint(self)) return;
    self->movement.turn_blocked = false;
    float const dirlen = Vector2_len(dir);
    if (dirlen <= 0.001f)
        return;  /* no meaningful heading this tick: hold current facing */

    /* Local avoidance resolves into ONE heading; the facing turns toward it and
     * the move step (unit_moveindirection) follows it, keeping facing and motion
     * aligned (no second, disagreeing search). */
    float const goal_angle = wc3_vector_heading(dir->x, dir->y);
    bool wait=false;
    bool fine_heading=false;
    if (policy==MOVE_AVOID_GENERIC && unit_routes_to_location(self)) {
        movePathQuery_t query=move_route_query(self,(moveRoutePoint_t){&self->goalentity->s.origin2,self->collision,policy});
        wc3GridPose_t prediction;unit_predicted_pose(self,&prediction);
        vec2_t source={prediction.grid[0],prediction.grid[1]};
        if (query.units) query.fine=&source;
        moveFineRoute_t const *route=&self->movement.fine_route;
        float point[2]; float const *fine=NULL;
        if (self->movement.path.valid && route->count && route->index<route->count) {
            point[0]=route->points[route->index].x; point[1]=route->points[route->index].y; fine=point;
            fine_heading=true;
        }
        /* 167070/1687e0 clear retry and delay before collection, including
         * empty vectors. TODO ROUTE: index0 arrival/perimeter and direct-flow
         * callers still need their complete original owner transitions. */
        bool progress=fine && route->index>0;
        if (fine && !progress) {
            wc3GridPose_t pose; unit_predicted_pose(self,&pose);
            float x=wc3_sub(pose.grid[0],fine[0]),y=wc3_sub(pose.grid[1],fine[1]);
            float range=wc3_float(0x3efae148);
            progress=wc3_add(wc3_mul(x,x),wc3_mul(y,y))>wc3_mul(range,range);
        }
        if (progress) self->movement.retry_count=self->movement.wait_delay=0;
        edict_t *blockers[32];
        uint32_t count=G_CollectUnitMoveStepBlockers(&query,fine,blockers);
        /* Original168360 clears the previous blocker even for an empty vector.
         * A stale identity would suppress another requester's later yield. */
        wc3YieldDecision_t choice=S_ResolveMoveBlockers(self,blockers,count);
        if (count) {
            wait=choice==WC3_YIELD_SELF || self->movement.wait_delay;
            /* Original165ae0 retries every nonempty admitted blocker vector,
             * including a stationary peer for which168360 chooses neither yield side. */
            if (progress && !wait) {
                move_retry_fine(self);
                return;
            }
        }
    }
    if (wait) {
        self->movement.heading=goal_angle;
        unit_turn_toward(self,goal_angle);
        self->movement.turn_blocked=true;
        return;
    }
    /* The admitted fine path already owns terrain and next-step obstruction.
     * A second world-space slide search changes the original native heading. */
    float const desired = fine_heading ? goal_angle :
        unit_desired_heading(self,goal_angle,unit_movedistance(self),policy);
    self->movement.heading = desired;
    unit_turn_toward(self, desired);
}

static bool move_displacement_steer(edict_t *self, moveAvoidPolicy_t policy) {
    vec2_t dir;

    if (!self || !self->movement.displacement_active) return false;
    if (move_displacement_reached(self)) return false;
    dir = Vector2_sub(&self->movement.displacement_target, &self->s.origin2);
    self->movement.flow_direct = true;
    unit_apply_heading(self, &dir, policy);
    return true;
}

static void unit_changeangle_towards_point_policy(edict_t *self, vec2_t const *point,
                                                   moveAvoidPolicy_t policy) {
    vec2_t dir;

    if (!self || !point || (self->aiflags & AI_IMMOBILE))
        return;
    if (move_displacement_steer(self, policy)) return;
    self->movement.heading = self->s.angle;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = true;
    dir = Vector2_sub(point, &self->s.origin2);
    unit_apply_heading(self, &dir, policy);
}

/* Raw curves are process-owned: actor removal, reload and shutdown release them before edict replacement. */
void S_FreeMoveRoute(edict_t *self) {
    S_CancelMoveCoarseRequest(&self->movement.fine_route.group_admission);
    S_CancelMoveCoarseRequest(&self->movement.fine_route.adaptive_admission);
    free(self->movement.fine_route.points);
    free(self->movement.fine_route.adaptive_points);
    free(self->movement.fine_route.group_points);
    self->movement.fine_route = (moveFineRoute_t){0};
    self->movement.path.valid = false;
}

/* Original168360 clears the requester identity, keeps prior delay and scans in order. */
wc3YieldDecision_t S_ResolveMoveBlockers(edict_t *self, edict_t *const *blockers, uint32_t count) {
    self->movement.wait_blocker=NULL;
    wc3YieldDecision_t result=WC3_YIELD_SKIP;
    float velocity[]={self->movement.velocity.x,self->movement.velocity.y};
    FOR_LOOP(i,count) {
        edict_t *peer=blockers[i];
        if (!peer || !peer->inuse || peer==self) continue;
        wc3YieldPeer_t other={.velocity={peer->movement.velocity.x,peer->movement.velocity.y},
            .player=peer->s.player,.grouped=unit_routes_to_location(peer),
            .same_group=self->movement.group_id && self->movement.group_id==peer->movement.group_id,
            .blocked=peer->movement.wait_blocker && peer->movement.wait_blocker->inuse};
        /* TODO GROUP/ROUTE-05.1: ordinary engine cohorts retain activation's flags0;
         * the original group-bit8 writer/producer remains unimplemented. */
        wc3YieldDecision_t choice=wc3_yield_decide(velocity,self->s.player,&other);
        if (choice==WC3_YIELD_SELF) {
            self->movement.wait_blocker=peer;
            self->movement.wait_delay=MAX(self->movement.wait_delay,4u);
            return WC3_YIELD_SELF;
        }
        if (choice==WC3_YIELD_PEER) {
            peer->movement.wait_blocker=self;
            peer->movement.wait_delay=MAX(peer->movement.wait_delay,20u);
            result=WC3_YIELD_PEER;
        }
    }
    return result;
}

/* Keep the bounded point-route turn until it is reached; retail likewise owns
 * route progress on each mover instead of rebuilding from its current point. */
typedef enum { MOVE_ROUTE_FAILED, MOVE_ROUTE_READY, MOVE_ROUTE_STOP } moveRouteResult_t;
static moveRouteResult_t unit_accel_direction(edict_t *self, moveRoutePoint_t point, vec2_t *dir) {
    if (!self || !point.point || !dir) return MOVE_ROUTE_FAILED;
    movePathQuery_t query = move_route_query(self, point);
    wc3GridPose_t before;unit_predicted_pose(self,&before);
    /* Native16fbd0 consumes this owner's prediction, not the last sampled
     * presentation pose. Keep the fine source alive for the whole query. */
    vec2_t source={before.grid[0],before.grid[1]};
    if (query.units) query.fine=&source;
    routePath_t *path = &self->movement.path;
    moveFineRoute_t *curve = &self->movement.fine_route;
    vec2_t local,fine_destination;
    if (query.units && curve->group_count && curve->group_index<curve->group_count) {
        vec2_t fine=curve->group_index ? (vec2_t){wc3_mul(curve->group_points[curve->group_index].x,2),wc3_mul(curve->group_points[curve->group_index].y,2)} : curve->group_goal;
        box2_t bounds=CM_GetWorldBounds();
        local=(vec2_t){wc3_add(bounds.min.x,wc3_mul(fine.x,CM_PathCellWorldSize())),wc3_add(bounds.min.y,wc3_mul(fine.y,CM_PathCellWorldSize()))};
        query.geometry.target=&local;
        /* Original16a790 submits the selected fine destination unchanged.
         * A blocked final leg must admit a partial route and its retries,
         * rather than becoming a successful nearest-point Move. */
        fine_destination=fine;query.fine_target=&fine_destination;
    }
    box2_t bounds=CM_GetWorldBounds();
    vec2_t final=query.fine_target ? *query.fine_target :
        (vec2_t){wc3_grid_coordinate(query.geometry.target->x,bounds.min.x,32),wc3_grid_coordinate(query.geometry.target->y,bounds.min.y,32)};
    vec2_t held={wc3_sub(final.x,before.grid[0]),wc3_sub(final.y,before.grid[1])};
    moveRoutePoint_t turn = { &path->waypoint, point.radius, point.policy };
    if (query.units) {
        uint32_t advance=0;
        /* Original165ae0 retains the admitted leg until progress/refill. A
         * fresh full-length interior sample every tick can reject a valid
         * cached turn as its fractional source crosses sample-cell boundaries.
         * Advance checks terrain epoch/mask; the step collector handles peers. */
        if (path->valid && (Vector2_distance(&path->target,point.point) >= 1.f ||
            fabsf(path->radius-point.radius) >= .01f)) path->valid=false;
        /* Native165b60 checks the retained coarse point before every fine
         * refill. A failed portal must retry after its wait without walking
         * toward that point merely because the fine leg was invalidated. */
        if ((path->valid || curve->adaptive_count) &&
            !G_AdvanceUnitMoveFineRouteStatus(&query,curve,&path->waypoint,&advance)) path->valid=false;
        if(advance){*dir=held;return MOVE_ROUTE_STOP;}
        if (!path->valid) {
            if (!G_BuildUnitMoveFineRouteStatus(&query,curve,&path->waypoint,&advance)) {
                if (advance) {*dir=held;return MOVE_ROUTE_STOP;}
                /* Native16fbd0 stops and turns toward the held destination
                 * when an admitted fine refill is denied or empty. Other
                 * ability routes retain their caller's movement policy until
                 * they enter the same member admission path. */
                if (!query.fine_target && !curve->adaptive_admission.waiting) return MOVE_ROUTE_FAILED;
                *dir=held;return MOVE_ROUTE_STOP;
            }
            path->target = *point.point; path->radius = point.radius; path->valid = true;
        }
        *dir = G_MoveFineRouteDirection(&query,curve);
        return MOVE_ROUTE_READY;
    }
    curve->count = curve->index = 0;
    if (path->valid && (Vector2_distance(&path->target, point.point) >= 1.0f ||
        fabsf(path->radius - point.radius) >= 0.01f ||
        Vector2_distance(query.geometry.from, &path->waypoint) <= CM_PathCellWorldSize() ||
        !move_route_line(self, turn))) path->valid = false;
    if (!path->valid) {
        if (!G_FindUnitMovePathWaypoint(&query, &path->waypoint)) return MOVE_ROUTE_FAILED;
        path->target = *point.point; path->radius = point.radius; path->valid = true;
    }
    *dir = Vector2_sub(&path->waypoint, query.geometry.from);
    return MOVE_ROUTE_READY;
}

#ifdef BZ_TESTS
/* Exercise the production steering consumer without advancing animation,
 * clocks or translation. Return its own FAILED/READY/STOP result, not EAX. */
unsigned S_TestMoveRouteDirection(edict_t *self, vec2_t const *point, vec2_t *dir) {
    return unit_accel_direction(self,(moveRoutePoint_t){point,self->collision,MOVE_AVOID_GENERIC},dir);
}
#endif

/* A nonzero Path_Advance status turns toward the pre-crossing goal and
 * suppresses translation for this visit. Keep it distinct from route failure. */
static void unit_apply_route_heading(edict_t *self,vec2_t const *dir,moveAvoidPolicy_t policy,moveRouteResult_t result) {
    if(result==MOVE_ROUTE_STOP) {
        self->movement.heading=wc3_vector_heading(dir->x,dir->y);
        unit_turn_toward(self,self->movement.heading);self->movement.turn_blocked=true;
    } else unit_apply_heading(self,dir,policy);
}

void unit_changeangle_towards_point(edict_t *self, vec2_t const *point) {
    unit_changeangle_towards_point_policy(self, point, MOVE_AVOID_GENERIC);
}

void unit_changeangle_towards_point_worker(edict_t *self, vec2_t const *point) {
    unit_changeangle_towards_point_policy(self, point, MOVE_AVOID_RESOURCE_WORKER);
}

bool unit_changeangle_towards_point_ignore_units(edict_t *self, vec2_t const *point) {
    moveRouteResult_t route_result=MOVE_ROUTE_FAILED;
    vec2_t dir;

    if (!self || !point || (self->aiflags & AI_IMMOBILE))
        return false;

    self->movement.heading = self->s.angle;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;

    /* Resource-return behaviors keep the building as their authoritative goal,
     * but navigation may target a worker-relative footprint edge.  Prefer that
     * exact point when it is directly reachable; otherwise use the same
     * collision-sized mover-owned A* accelerator used while shared fields are
     * pending.  Live units remain ignored by the steering/move policy. */
    if (move_route_line(self, (moveRoutePoint_t){point, self->collision, MOVE_AVOID_STATIC_ONLY})) {
        self->movement.path.valid = false;
        self->movement.flow_direct = true;
        dir = Vector2_sub(point, &self->s.origin2);
    } else if (!(route_result=unit_accel_direction(self, (moveRoutePoint_t){point, self->collision, MOVE_AVOID_STATIC_ONLY}, &dir))) {
        return false;
    }

    unit_apply_route_heading(self, &dir, MOVE_AVOID_STATIC_ONLY, route_result);
    return true;
}

static void unit_changeangle_policy(edict_t *self, moveAvoidPolicy_t policy) {
    moveRouteResult_t route_result=MOVE_ROUTE_FAILED;
    if ((self->aiflags & AI_IMMOBILE) && !(S_AncientIsRooted(self) && self->ancient_root->rooted_turning))
        return;
    if (policy==MOVE_AVOID_GENERIC && unit_routes_to_location(self) &&
        wc3_yield_advance(&self->movement.wait_delay,false)) {
        /* Original165ae0's countdown leaves the caller destination unchanged.
         * 16fbd0 subtracts it from the predicted native source, before world
         * projection; acquisition-time waits separately retain the waypoint. */
        move_hold_goal_heading(self);
        return;
    }
    if (move_displacement_steer(self, policy))
        return;
    if (move_fallback_steer(self, policy))
        return;
    self->movement.route_resume_active = false;
    vec2_t to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    vec2_t dir;
    /* Attack retains an entity/range goal, but its route must still fit the
     * attacker's footprint. A point-only field can thread a tower gap that
     * move-time collision rejects, leaving local slide to stall at the obstacle.
     * flow_goal_reached below still hands the real target to the attack behavior
     * for its authored interaction-range check. */
    float const radius = (unit_routes_to_location(self) ||
        (self->currentmove && self->currentmove->proc == CAbilityAttack))
        ? self->collision : 0.0f;
    uint8_t const blocked_flags = M_UnitStaticPathingFlags(self);

    self->movement.heading = self->s.angle;  /* default if no heading is resolved this tick */
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;

    /* An admitted retail point cohort owns partial routing too. A generic
     * static field for the raw click must not replace its task destination
     * or skip the current fine leg when that click lies outside the world. */
    if (policy==MOVE_AVOID_GENERIC && unit_routes_to_location(self) &&
        self->movement.fine_route.group_count &&
        (route_result=unit_accel_direction(self,(moveRoutePoint_t){&self->goalentity->s.origin2,radius,policy},&dir))) {
        unit_apply_route_heading(self, &dir, policy, route_result);
        return;
    }

    /* Generic interaction movement keeps the original point-route contract.
     * Attack, mine entry, resource return, repair, and other ranged behaviors
     * decide when their interaction boundary has been reached.  Do not stop
     * those orders at a collision-expanded flow goal outside that boundary.
     * Move orders own radius-valid reserved destinations, so their route must
     * use the same footprint as move-time collision; point routing previously
     * sent units into narrow gaps and touching obstacle corners. */
    if (move_route_line(self, (moveRoutePoint_t){&self->goalentity->s.origin2, radius, policy}) &&
        !(unit_routes_to_location(self) && self->movement.path.valid && self->movement.fine_route.count)) {
        /* Retail16fbd0 advances even a clear point route. Its retained fine
         * turn and predicted native source determine the heading; subtracting
         * published world positions changes velocity/facing before a blocker. */
        if (policy==MOVE_AVOID_GENERIC && unit_routes_to_location(self) &&
            (route_result=unit_accel_direction(self,(moveRoutePoint_t){&self->goalentity->s.origin2,radius,policy},&dir))) {
            unit_apply_route_heading(self, &dir, policy, route_result);
            return;
        }
        self->movement.path.valid = false;
        self->movement.flow_direct = true;
        dir = to_goal;
    } else {
        uint32_t heatmap = M_RefreshHeatmapForMover(self, self->goalentity, radius);
        self->movement.flow_generation = heatmap;
        /* A completed generic field previously discarded the mover's fine
         * turn. Keep retail route choices throughout nearby detours, while
         * known unreachable/adjusted endpoints retain their interaction path. */
        /* Nearby orders retain retail search choices; long fields use the
         * same footprint geometry for expansion and flow sampling. */
        bool fine = unit_routes_to_location(self) || !heatmap ||
                    !CM_FlowReachedGoal(heatmap, self->s.origin.x, self->s.origin.y);
        /* A completed static field already proves disconnection. Preserve its
         * component fallback; a partial fine turn must not postpone it. */
        if (heatmap && !CM_FlowCanReach(heatmap, self->s.origin.x, self->s.origin.y)) fine = false;
        if (fine && (route_result=unit_accel_direction(self, (moveRoutePoint_t){&self->goalentity->s.origin2, radius, policy}, &dir))) {
            unit_apply_route_heading(self, &dir, policy, route_result);
            if (!self->movement.route_resume_active)
                move_route_resume_save(self, self->goalentity, radius, blocked_flags, &dir);
            return;
        }
        if (!heatmap && !unit_routes_to_location(self) &&
            move_route_resume(self, self->goalentity, radius, blocked_flags, &dir)) {
            self->movement.route_resume_active = true;
            unit_apply_route_heading(self, &dir, policy, route_result);
            return;
        }
        if (!heatmap) {
            /* A live object can occupy the goal while the static field is
             * still pending. Keep collision-aware local steering in that
             * clear static corridor; pausing here stranded occupied-goal Move.
             * A nearest chain containing only the current cell gives no turn. */
            if (move_static_line(self, &self->goalentity->s.origin2, radius)) {
                unit_apply_heading(self, &to_goal, policy);
                self->movement.flow_direct = true;
            }
            return;
        }
        self->movement.path.valid = false;
        if (CM_FlowReachedGoal(heatmap, self->s.origin.x, self->s.origin.y)) {
            /* Location orders stop at their collision-safe route endpoint in
             * the owning behavior. Interaction goals may be blocked or have
             * their own range boundary; once the adjusted route end is reached
             * they steer toward the real entity target so the behavior's
             * range check can complete. */
            self->movement.flow_goal_reached = true;
            dir = to_goal;
        } else {
            dir = get_flow_direction(heatmap, self->s.origin.x, self->s.origin.y);
            if (Vector2_len(&dir) <= 0.001f) {
                self->movement.flow_unreachable = !CM_FlowCanReach(heatmap, self->s.origin.x, self->s.origin.y);
                /* Keep the reachable approach on the mover. Only a private
                 * location waypoint may adopt that endpoint; interaction
                 * targets retain their authoritative pose and footprint. */
                if (radius > 0.0f && self->movement.flow_unreachable) {
                    vec2_t const *from = &self->s.origin2, *target = &self->goalentity->s.origin2;
                    vec2_t closest;
                    if (move_fallback_throttled(self, target, radius))
                        return;
                    self->movement.flow_fallback_target = *target;
                    self->movement.flow_fallback_radius = radius;
                    self->movement.flow_fallback_time = level.time;
                    S_SetMoveGoal(self, &self->movement.flow_fallback_goal, self->goalentity);
                    self->movement.flow_fallback_state = MOVE_FALLBACK_RETRY;
                    pathAccelParams_t query = {from, target, radius, blocked_flags};
                    if (G_ClosestReachableMovePoint(&query, &closest)) {
                        self->goalentity->heatmap2 = 0;
                        self->goalentity->heatmap2_radius = 0;
                        move_reset_progress(self);
                        self->movement.flow_fallback_target = *target;
                        self->movement.flow_fallback_approach = closest;
                        self->movement.flow_fallback_radius = radius;
                        S_SetMoveGoal(self, &self->movement.flow_fallback_goal, self->goalentity);
                        if (!(self->goalentity->svflags & SVF_MOVE_WAYPOINT) ||
                            (move_has_active_construction() &&
                             Vector2_distance(&closest, target) > 1.0f)) {
                            self->movement.flow_fallback_state = MOVE_FALLBACK_APPLIED;
                            dir = Vector2_sub(&closest, &self->s.origin2);
                            self->movement.flow_direct = true;
                            unit_apply_route_heading(self, &dir, policy, route_result);
                        } else {
                            self->goalentity->s.origin2 = closest;
                            S_SetMoveGoal(self->goalentity, &self->goalentity->secondarygoal, NULL);
                            self->movement.flow_fallback_state = MOVE_FALLBACK_APPLIED;
                        }
                    }
                    return;
                }
                return;
            }
        }
    }

    self->movement.route_resume_active = false;
    unit_apply_route_heading(self, &dir, policy, route_result);
    move_route_resume_save(self, self->goalentity, radius, blocked_flags, &dir);
}

void unit_changeangle(edict_t *self) {
    unit_changeangle_policy(self, MOVE_AVOID_GENERIC);
}

void unit_changeangle_worker(edict_t *self) {
    unit_changeangle_policy(self, MOVE_AVOID_RESOURCE_WORKER);
}

/* Behaviors that route around authored blocked geometry may request a
 * collision-sized field. Lumber uses its route-end state to retarget an
 * unreachable tree. Build and Repair instead route toward behavior-owned legal
 * approach points, so reaching the adjusted flow goal never changes their
 * gameplay target. Generic point movement continues through unit_changeangle(). */
static void unit_changeangle_for_radius_policy(edict_t *self, float radius,
                                               moveAvoidPolicy_t policy,
                                               bool continue_to_target) {
    moveRouteResult_t route_result=MOVE_ROUTE_FAILED;
    if ((self->aiflags & AI_IMMOBILE) && !(S_AncientIsRooted(self) && self->ancient_root->rooted_turning))
        return;
    uint8_t const blocked_flags = M_UnitStaticPathingFlags(self);
    vec2_t to_goal = Vector2_sub(&self->goalentity->s.origin2, &self->s.origin2);
    vec2_t dir;
    self->movement.heading = self->s.angle;
    self->movement.route_resume_active = false;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;

    if (move_route_line(self, (moveRoutePoint_t){&self->goalentity->s.origin2, radius, policy})) {
        self->movement.path.valid = false;
        self->movement.flow_direct = true;
        dir = to_goal;
    } else {
        uint32_t heatmap = M_RefreshHeatmapForMover(self, self->goalentity, radius);
        self->movement.flow_generation = heatmap;
        if (!heatmap) {
            if (!(route_result=unit_accel_direction(self, (moveRoutePoint_t){&self->goalentity->s.origin2, radius, policy}, &dir))) {
                if (!move_route_resume(self, self->goalentity, radius, blocked_flags, &dir))
                    return; /* long incremental route is still building */
                self->movement.route_resume_active = true;
            }
            unit_apply_route_heading(self, &dir, policy, route_result);
            if (!self->movement.route_resume_active)
                move_route_resume_save(self, self->goalentity, radius, blocked_flags, &dir);
            return;
        }
        self->movement.path.valid = false;

        if (CM_FlowReachedGoal(heatmap, self->s.origin.x, self->s.origin.y)) {
            self->movement.flow_goal_reached = true;
            if (!continue_to_target)
                return;
            /* Ranged interactions target a blocked unit/building centre.  A
             * collision-sized route deliberately ends at the nearest legal
             * cell around that footprint; from there keep steering at the real
             * target and let the behavior's precise range check complete the
             * interaction before a step would enter static pathing. */
            dir = to_goal;
        } else {
            dir = get_flow_direction(heatmap, self->s.origin.x, self->s.origin.y);
        }
        if (!self->movement.flow_goal_reached && Vector2_len(&dir) <= 0.001f) {
            self->movement.flow_unreachable =
                !CM_FlowCanReach(heatmap, self->s.origin.x, self->s.origin.y);
            return;
        }
    }

    self->movement.route_resume_active = false;
    unit_apply_route_heading(self, &dir, policy, route_result);
    move_route_resume_save(self, self->goalentity, radius, blocked_flags, &dir);
}

void unit_changeangle_for_radius(edict_t *self, float radius) {
    unit_changeangle_for_radius_policy(self, radius, MOVE_AVOID_GENERIC, false);
}

void unit_changeangle_for_radius_worker(edict_t *self, float radius) {
    unit_changeangle_for_radius_policy(self, radius, MOVE_AVOID_RESOURCE_WORKER, false);
}

void unit_changeangle_interaction_ignore_units(edict_t *self) {
    /* Warsmash's mover owns a collision-sized path even when the ranged
     * behavior disables unit collision.  Use the worker radius for static
     * routing, but keep mobile-unit collision disabled at steering/move time. */
    unit_changeangle_for_radius_policy(self, self ? self->collision : 0.0f,
                                       MOVE_AVOID_STATIC_ONLY, true);
}

static entitySet_t waypoint_available;
static bool waypoint_cache_valid;
static bool waypoint_index_valid;
static entitySet_t waypoint_dirty, waypoint_roots, waypoint_live, waypoint_kinds, waypoint_chains;
static uint32_t waypoint_references[MAX_ENTITIES];
static uint16_t waypoint_owner_roots[MAX_ENTITIES][11];
_Static_assert(MAX_ENTITIES <= UINT16_MAX, "Waypoint root identity must fit with a null sentinel");
#ifdef BZ_TESTS
static uint32_t waypoint_owner_visits;
#endif

void G_ResetWaypointCache(void) { waypoint_cache_valid=waypoint_index_valid=false; }

/* Reference writers publish the pointer synchronously. Reclamation reconciles
 * each changed owner once, after all writes, using compact identity columns.
 * Reads never allocate and this derived index never claims entity identities. */
void S_MarkMoveGoals(edict_t const *owner) {
    uintptr_t offset=(uintptr_t)owner-(uintptr_t)g_edicts;
    if(waypoint_index_valid && g_edicts && offset<sizeof(*owner)*MAX_ENTITIES && !(offset%sizeof(*owner)))
        entity_set_put(&waypoint_dirty,offset/sizeof(*owner),true);
}

edict_t *S_SetMoveGoal(edict_t *owner, edict_t **slot, edict_t *goal) {
    *slot=goal;
    S_MarkMoveGoals(owner);
    return goal;
}

static uint16_t waypoint_identity(edict_t const *point) {
    uintptr_t offset=(uintptr_t)point-(uintptr_t)g_edicts;
    return g_edicts && offset<sizeof(*point)*MAX_ENTITIES && !(offset%sizeof(*point)) ?
        (uint16_t)(offset/sizeof(*point)+1) : 0;
}

static void waypoint_update_owner(uint32_t index) {
    edict_t const *unit=g_edicts+index;
    bool kind=(unit->svflags&SVF_MOVE_WAYPOINT)!=0;
    edict_t *heads[11]={0};
    if(unit->inuse && !kind) {
        edict_t *live[]={unit->goalentity,unit->secondarygoal,unit->movement.attackmove_waypoint,
            unit->movement.patrol_a,unit->movement.patrol_b,unit->movement.patrol_target,
            unit->movement.waygate_goal,unit->movement.cargo_unload_goal,unit->movement.flow_fallback_goal,
            unit->movement.route_resume_valid?unit->movement.route_resume_goal:NULL,
            unit->ancient_root?unit->ancient_root->approach_goal:NULL};
        memcpy(heads,live,sizeof(heads));
    }
    FOR_LOOP(i,sizeof(heads)/sizeof(*heads)) {
        uint16_t next=waypoint_identity(heads[i]),old=waypoint_owner_roots[index][i];
        if(next==old)continue;
        if(old) {
            assert(waypoint_references[old-1]);
            if(!--waypoint_references[old-1])entity_set_put(&waypoint_roots,old-1,false);
        }
        if(next && !waypoint_references[next-1]++)entity_set_put(&waypoint_roots,next-1,true);
        waypoint_owner_roots[index][i]=next;
    }
    entity_set_put(&waypoint_live,index,unit->inuse && kind);
    entity_set_put(&waypoint_kinds,index,kind);
    entity_set_put(&waypoint_chains,index,kind && waypoint_identity(unit->secondarygoal));
#ifdef BZ_TESTS
    waypoint_owner_visits++;
#endif
}

static void waypoint_reconcile_owners(void) {
    if(!waypoint_index_valid) {
        waypoint_dirty=waypoint_roots=waypoint_live=waypoint_kinds=waypoint_chains=(entitySet_t){0};
        memset(waypoint_references,0,sizeof(waypoint_references));
        memset(waypoint_owner_roots,0,sizeof(waypoint_owner_roots));
        FOR_LOOP(i,globals.num_edicts)waypoint_update_owner(i);
        waypoint_index_valid=true;
    } else {
        for(uint32_t i=entity_set_next(&waypoint_dirty,0);i<globals.num_edicts;i=entity_set_next(&waypoint_dirty,i+1))
            waypoint_update_owner(i);
        waypoint_dirty=(entitySet_t){0};
    }
}

static void waypoint_retain(entitySet_t *retained,edict_t *point) {
    while(point) {
        uintptr_t address=(uintptr_t)point,base=(uintptr_t)g_edicts;
        if(address<base || address>=base+globals.num_edicts*sizeof(*point) ||
            (address-base)%sizeof(*point))return;
        uint32_t index=(address-base)/sizeof(*point);
        if(!(point->svflags&SVF_MOVE_WAYPOINT) ||
            (retained->bits[index/64]&(UINT64_C(1)<<(index%64))))return;
        entity_set_put(retained,index,true);
        point=point->secondarygoal;
    }
}

/* Trace the retained heads once per allocation batch. Newly allocated slots
 * remain leased until the next trace, so callers can install their pointers
 * after Waypoint_add returns. The ordinary F_EDICT serializer owns all heads. */
static void waypoint_collect_available(void) {
    entitySet_t retained={0};
    waypoint_reconcile_owners();
    FOR_LOOP(word,sizeof(retained.bits)/sizeof(*retained.bits)) {
        retained.bits[word]=waypoint_roots.bits[word]&waypoint_kinds.bits[word];
        if(retained.bits[word])retained.top[word/64]|=UINT64_C(1)<<(word%64);
    }
    /* Only roots with a secondary edge need graph traversal. Shared chains
     * and cycles retain the original first-mark stopping rule. */
    for(uint32_t i=entity_set_next(&waypoint_chains,0);i<globals.num_edicts;i=entity_set_next(&waypoint_chains,i+1))
        if(waypoint_references[i])waypoint_retain(&retained,g_edicts[i].secondarygoal);
    waypoint_available=(entitySet_t){0};
    FOR_LOOP(word,sizeof(retained.bits)/sizeof(*retained.bits)) {
        waypoint_available.bits[word]=waypoint_live.bits[word]&~retained.bits[word];
        if(waypoint_available.bits[word])waypoint_available.top[word/64]|=UINT64_C(1)<<(word%64);
    }
    waypoint_cache_valid=true;
}

/* Reserve the initial contiguous destination batch before map actors spawn. */
void G_InitWaypoints(void) {
    uint32_t base;
    if (level.waypoints.count) return;
    base = level.waypoints.base = globals.num_edicts;
    FOR_LOOP(i, MAX_WAYPOINTS) {
        edict_t *waypoint = G_Spawn();
        if (waypoint != g_edicts + base + i) gi.error("G_InitWaypoints: waypoint ring is not contiguous\n");
        waypoint->svflags |= SVF_NOCLIENT|SVF_MOVE_WAYPOINT;
    }
    level.waypoints.count = MAX_WAYPOINTS;
    G_ResetWaypointCache();
}

/* Reuse only unreferenced destinations; grow in batches when every head is
 * still live. A body queue may replace visible corpses, but a Move target
 * must retain its coordinates until its owner releases it. */
edict_t *Waypoint_add(vec2_t const *spot) {
    edict_t *waypoint;
    G_InitWaypoints();
    if(!waypoint_cache_valid)waypoint_collect_available();
    uint32_t index=entity_set_next(&waypoint_available,level.waypoints.base+level.waypoints.cursor);
    if(index==MAX_ENTITIES)index=entity_set_next(&waypoint_available,0);
    if(index==MAX_ENTITIES) {
        waypoint_collect_available();
        index=entity_set_next(&waypoint_available,0);
    }
    if(index==MAX_ENTITIES) {
        uint32_t reserve=MIN(128,globals.max_edicts-globals.num_edicts);
        if(!reserve)gi.error("Move: no destination storage available (%u entities)\n",globals.max_edicts);
        FOR_LOOP(i,reserve) {
            edict_t *point=G_Spawn();
            point->svflags|=SVF_NOCLIENT|SVF_MOVE_WAYPOINT;
            entity_set_put(&waypoint_available,point-g_edicts,true);
        }
        index=entity_set_next(&waypoint_available,0);
    }
    waypoint=g_edicts+index;
    entity_set_put(&waypoint_available,index,false);
    level.waypoints.cursor=index>=level.waypoints.base && index<level.waypoints.base+MAX_WAYPOINTS ?
        (index-level.waypoints.base+1)%MAX_WAYPOINTS : 0;
    waypoint->s.origin.x = spot->x;
    waypoint->s.origin.y = spot->y;
    waypoint->heatmap2 = 0;
    waypoint->heatmap2_radius = 0;
    S_SetMoveGoal(waypoint, &waypoint->secondarygoal, NULL);
    waypoint->collision = 0;
    M_CheckGround(waypoint);
    return waypoint;
}

static int move_harvest_path_debug_level(void) {
    cstring_t value;
    value = gi.CvarString("wc3_harvest_path_debug", "0");
    return value ? atoi(value) : 0;
}

uint32_t M_RefreshHeatmapForMover(edict_t const *mover, edict_t *self, float radius) {
    edict_t *route = self && self->secondarygoal ? self->secondarygoal : self;
    uint8_t const blocked_flags = M_UnitStaticPathingFlags(mover);
    bool cached = false;
    uint32_t generation;

    if (!route)
        return 0;

    if (route->heatmap2)
        cached = G_ActivateMovePathField(route->heatmap2, radius, blocked_flags);

    /* Fixed waypoints never move, so a still-cached field remains valid until
     * static pathing invalidates the routing cache. */
    if (cached && !(route->svflags & SVF_MONSTER))
        return route->heatmap2;

    if (cached && (route->svflags & SVF_MONSTER)) {
        float const target_movement = Vector2_distance(&route->s.origin2, &route->heatmap2_origin);
        float const refresh_distance = mover
            ? Vector2_distance(&mover->s.origin2, &route->s.origin2) * 0.1f
            : 64.0f;
        bool const moved = target_movement > refresh_distance;
        bool const stale = (uint32_t)(level.time - route->heatmap2_time) >= 400;
        if (!moved || !stale)
            return route->heatmap2;
    }

    /* Shared routing resumes cache misses. Return the old field for a moving
     * target while its replacement is being built; fixed goals with
     * no field simply wait until a later tick instead of steering straight into
     * the obstacle that caused routing to be needed. */
    generation = G_RequestMovePathField(mover, route, radius, blocked_flags);
    if (!generation)
        return cached ? route->heatmap2 : 0;

    route->heatmap2 = generation;
    route->heatmap2_origin = route->s.origin2;
    route->heatmap2_time = level.time;
    route->heatmap2_radius = radius;

    if (move_harvest_path_debug_level() >= 2 && route->targtype == TARG_TREE) {
        fprintf(stderr,
                "WC3_HARVEST_PATH heatmap target=%d reason=ready generation=%u radius=%.1f\n",
                route->s.number, route->heatmap2, radius);
    }
    return route->heatmap2;
}

uint32_t M_RefreshHeatmap(edict_t *self, float radius) {
    return M_RefreshHeatmapForMover(NULL, self, radius);
}

static bool M_UnitUsesWaterSurface(edict_t const *self, wc3MovementProfile_t const *profile) {
    if (profile->support == WC3_SUPPORT_FLIGHT || profile->support == WC3_SUPPORT_WATER_MAX)
        return true;
    if (profile->support == WC3_SUPPORT_DEEP_WATER) {
        return (self->movement.support_flags & WC3_SUPPORT_IN_DEEP_WATER) != 0;
    }
    return false;
}

static bool move_fallback_steer(edict_t *self, moveAvoidPolicy_t policy) {
    vec2_t dir;

    if (!self || self->movement.flow_fallback_state != MOVE_FALLBACK_APPLIED ||
        self->movement.flow_fallback_goal != self->goalentity)
        return false;
    if (Vector2_distance(&self->s.origin2, &self->movement.flow_fallback_approach) <=
        unit_movedistance(self) + MOVE_ARRIVE_TOLERANCE) {
        self->movement.flow_fallback_state = MOVE_FALLBACK_NONE;
        S_SetMoveGoal(self, &self->movement.flow_fallback_goal, NULL);
        return false;
    }
    dir = Vector2_sub(&self->movement.flow_fallback_approach, &self->s.origin2);
    self->movement.flow_direct = true;
    unit_apply_heading(self, &dir, policy);
    return true;
}

/*66d780/684480: all ground movement types accept higher deck support. AMPH
 * selects water using the previous refresh's deep flag; publish the current
 * terrain classification only AFTER selecting height. */
void S_RefreshUnitSupport(edict_t *self, bool force) {
    /*684480 still publishes cached XY when the displacement is below.01.
     * Explicit setters and pose commits force the query through M_CheckGround. */
    bool const moved = !self->movement.support_valid ||
        fabsf(self->s.origin.x - self->movement.support_point.x) >= 0.01f ||
        fabsf(self->s.origin.y - self->movement.support_point.y) >= 0.01f;
    self->movement.support_point = self->s.origin2;
    if (!force && !moved) return;
    unitMovementType_t const type = S_UnitMovementType(self->data.UnitData);
    float height = CM_GetHeightAtPoint(self->s.origin.x, self->s.origin.y);
    float const cell = CM_PathCellWorldSize();
    uint8_t terrain_flags = 0;
    uint32_t support_flags = 0;

    for (edict_t *surface = level.ground_surfaces; surface; surface = surface->ground_next) {
        pathTex_t const *pathtex = surface->pathtex;
        pathTexTransform_t const transform = CM_GetPathTexTransform(surface);
        if (!surface->inuse || surface->destructable->dead ||
            !surface->destructable->placement_solid || !pathtex) continue;
        if (fabsf(self->s.origin.x - surface->s.origin.x) > transform.width * cell * 0.5f ||
            fabsf(self->s.origin.y - surface->s.origin.y) > transform.height * cell * 0.5f) continue;
        if (surface->s.origin.z > height) {
            height = surface->s.origin.z;
            support_flags |= WC3_SUPPORT_ON_DECK;
        }
    }
    if (M_UnitUsesWaterSurface(self, wc3_movement_profile(type)))
        height = MAX(height, CM_GetWaterHeightAtPoint(self->s.origin.x, self->s.origin.y));

    if (G_GetTerrainPathingFlags(&self->s.origin2, &terrain_flags) &&
        (terrain_flags & CM_PATHING_UNWALKABLE) && !(terrain_flags & CM_PATHING_UNFLOATABLE))
        support_flags |= WC3_SUPPORT_IN_DEEP_WATER;
    self->movement.support_flags = support_flags;
    self->movement.support_valid = true;
    self->s.ground_offset = self->unitinfo.FlyHeight;
    self->s.origin.z = height + self->s.ground_offset;
}

void M_CheckGround(edict_t *self) {
    S_RefreshUnitSupport(self, true);
}

float M_DistanceToGoal(edict_t *ent) {
    if (ent->goalentity) {
        return Vector2_distance(&ent->goalentity->s.origin2, &ent->s.origin2);
    } else {
        return 0;
    }
}

static float move_slot_spacing(edict_t *const *units, uint32_t count) {
    float max_radius = 0;
    FOR_LOOP(i, count) {
        max_radius = MAX(max_radius, units[i]->collision);
    }
    return MAX(MOVE_MIN_SLOT_SPACING, max_radius * 2 + MOVE_SLOT_MARGIN);
}

static bool move_slot_overlaps(vec2_t const *point,
                               float radius,
                               moveSlot_t const *reserved,
                               uint32_t num_reserved) {
    FOR_LOOP(i, num_reserved) {
        float min_distance = radius + reserved[i].radius + MOVE_SLOT_MARGIN;
        if (Vector2_distance(point, &reserved[i].point) < min_distance) {
            return true;
        }
    }
    return false;
}

static bool move_try_slot(vec2_t const *point,
                          float radius,
                          uint8_t blocked_flags,
                          moveSlot_t const *reserved,
                          uint32_t num_reserved,
                          vec2_t *out) {
    vec2_t pathable = *point;
    pathAccelParams_t query = { point, NULL, radius, blocked_flags };
    if (!G_ClosestMovePathPoint(&query, &pathable)) {
        return false;
    }
    if (move_slot_overlaps(&pathable, radius, reserved, num_reserved)) {
        return false;
    }
    *out = pathable;
    return true;
}

static bool move_find_reserved_slot(vec2_t const *location,
                                    vec2_t const *preferred,
                                    float radius,
                                    uint8_t blocked_flags,
                                    float spacing,
                                    uint32_t unit_count,
                                    moveSlot_t const *reserved,
                                    uint32_t num_reserved,
                                    vec2_t *out) {
    float best_distance = 0;
    bool found = false;
    vec2_t best = *location;
    int max_ring = (int)ceilf(sqrtf(MAX(1, unit_count))) + 8;

    if (move_try_slot(preferred, radius, blocked_flags, reserved, num_reserved, out)) {
        return true;
    }

    for (int ring = 0; ring <= max_ring; ring++) {
        int min = -ring;
        int max = ring;
        for (int y = min; y <= max; y++) {
            for (int x = min; x <= max; x++) {
                if (ring > 0 && x != min && x != max && y != min && y != max) {
                    continue;
                }
                vec2_t candidate = {
                    location->x + x * spacing,
                    location->y + y * spacing,
                };
                vec2_t pathable;
                float distance;

                if (!move_try_slot(&candidate, radius, blocked_flags, reserved, num_reserved, &pathable)) {
                    continue;
                }

                distance = Vector2_distance(&pathable, preferred);
                if (!found || distance < best_distance) {
                    best_distance = distance;
                    best = pathable;
                    found = true;
                }
            }
        }
        if (found) {
            *out = best;
            return true;
        }
    }
    return false;
}

static vec2_t move_preferred_slot(edict_t *ent,
                                   vec2_t const *group_center,
                                   vec2_t const *location,
                                   float spacing,
                                   uint32_t unit_count) {
    vec2_t offset = Vector2_sub(&ent->s.origin2, group_center);
    float max_offset = spacing * (sqrtf(MAX(1, unit_count)) + 1);
    float len = Vector2_len(&offset);
    if (len > max_offset && len > 0.001f) {
        offset = Vector2_scale(&offset, max_offset / len);
    }
    return Vector2_add(location, &offset);
}

static uint32_t move_collect_selected(gameClient_t *client,
                                   edict_t * *units,
                                   uint32_t max_units,
                                   vec2_t *center) {
    uint32_t count = 0;
    *center = MAKE(vec2_t, 0, 0);

    FOR_CONTROLLABLE_SELECTED_UNITS(client, ent) {
        if (count >= max_units) {
            break;
        }
        if ((ent->aiflags & AI_IMMOBILE) || ent->data.UnitBalance->speed <= 0) {
            continue;
        }
        units[count++] = ent;
        center->x += ent->s.origin2.x;
        center->y += ent->s.origin2.y;
    }

    if (count > 0) {
        center->x /= count;
        center->y /= count;
    }
    return count;
}

void move_reset_progress(edict_t *self) {
    self->movement.pause_order_id=0;self->movement.pause_resume_pending=false;
    S_TrackMoveTimers(self);
    move_release_captain_reference(self);
    self->movement.type_rebind_pending=false;
    S_TrackMoveTimers(self);
    move_unlink_requests(self);
    /* Original166060 activates a replacement path with fresh7c/80 admission
     * timestamps. A previous follower's throttle must not delay its group leg. */
    self->movement.fine_request_time=0;
    self->movement.fine_route.adaptive_admission.time=0;
    self->movement.fine_route.group_admission.time=0;
    /* Native task activation owns a fresh local path. In particular, the old
     * partial88.10000000 flag must not deny classification of the new group. */
    self->movement.fine_route.count=self->movement.fine_route.adaptive_count=0;
    self->movement.fine_route.index=self->movement.fine_route.adaptive_index=UINT32_MAX;
    self->movement.fine_route.partial=false;
    /* Replacement/internal approaches own a new group plan. Reusing the last
     * point Move's destination can strand an ability at its previous endpoint. */
    self->movement.fine_route.group_count=0;
    self->movement.fine_route.group_index=UINT32_MAX;
    self->movement.last_origin = self->s.origin2;
    self->movement.last_distance = -1;
    self->movement.blocked_frames = 0;
    self->movement.flow_generation = 0;
    self->movement.flow_goal_reached = false;
    self->movement.flow_unreachable = false;
    self->movement.flow_direct = false;
    S_SetMoveGoal(self, &self->movement.flow_fallback_goal, NULL);
    self->movement.flow_fallback_state = MOVE_FALLBACK_NONE;
    self->movement.worker_avoid_origin = self->s.origin2;
    self->movement.worker_avoid_heading = self->s.angle;
    self->movement.worker_avoid_blocked_frames = 0;
    self->movement.worker_avoid_active = false;
    self->movement.wait_delay=0;
    self->movement.retry_count=0;
    self->movement.point_forced_arrival=false;
    self->movement.wait_blocker=NULL;
    move_detach_group(self);
    self->movement.group_id = 0;
    self->movement.group_speed = 0;  /* single-unit/default: travel at own speed */
    self->movement.turn_blocked = false;
    self->movement.velocity = (vec2_t){0};
}

void move_cancel_displacement(edict_t *self) {
    if (!self) return;
    /* A behavior transition cancels its pending local-route request as well as displacement. */
    move_unlink_requests(self);
    self->movement.displacement_active = false;
}

bool move_displacement_active(edict_t const *self) {
    return self && self->movement.displacement_active;
}

bool move_displacement_reached(edict_t *self) {
    if (!self || !self->movement.displacement_active) return false;
    if (Vector2_distance(&self->s.origin2, &self->movement.displacement_target) >
        unit_movedistance(self) + MOVE_ARRIVE_TOLERANCE)
        return false;
    if (M_MoveIsValid(self, &self->movement.displacement_target)) {
        /* Retire the old pose clock with the endpoint commit; a raw world write
         * let the next heading decision integrate the old velocity past it. */
        unit_commit_step(self, &self->movement.displacement_target);
        self->s.origin.z = CM_GetHeightAtPoint(self->s.origin2.x, self->s.origin2.y);
    }
    move_cancel_displacement(self);
    return true;
}

void move_start_displacement(edict_t *self, vec2_t const *target) {
    if (!self || !target) return;
    /* Retail widget escape admits a real Move even for an idle occupant.
     * A displacement flag alone left the stand thinker running forever.
     * Existing walkers/builders retain their order and consume this target
     * temporarily; only an idle stand needs a new Move destination. */
    if (self->currentmove && self->currentmove->think == ai_stand &&
        !self->current_order_id && !self->movement.holding_position)
        S_IssueMoveOrder(self, Waypoint_add(target), G_OrderId("move"));
    move_reset_progress(self);
    self->movement.displacement_target = *target;
    self->movement.displacement_active = true;
    unit_setanimation(self, "walk");
}

/* Effective current move speed of a unit (runtime override, else data table). */
static float unit_effective_speed_with_bonus(edict_t *ent, float bonus) {
    if (M_UnitMoveDisabled(ent)) return 0;
    if (ent->movement.captain_actor_type) return ent->unitinfo.MoveSpeed;
    unitStatusQuery_t statuses;
    G_BeginUnitStatusQuery(ent,&statuses);
    float speed = (ent->unitinfo.move_flags & BZ_UNIT_SPEED_SET) || ent->unitinfo.MoveSpeed > 0
        ? ent->unitinfo.MoveSpeed : ent->data.UnitBalance->speed;
    speed = wc3_add(speed, bonus);
    uint32_t level = G_QueryUnitStatusLevel(ent, MAKEFOURCC('B', 'O', 'w', 'k'));
    if (level) speed *= 1.0f + G_AbilityLevel(MAKEFOURCC('A', 'O', 'w', 'k'), level)->data[0].number * 0.01f;
    speed *= 1.0f + S_UnholyMoveBonus(ent);
    speed = wc3_mul(speed, wc3_add(1, S_BloodlustMoveBonus(ent)));
    speed = wc3_mul(speed, S_HumanMoveFactor(ent));
    speed *= 1.0f - S_CrippleMoveReduction(ent);
    speed = unit_apply_earthquake_speed(ent, speed);
    speed *= 1.0f - S_PurgeMoveReduction(ent);
    speed *= 1.0f - S_SlowPoisonMoveReduction(ent);
    speed=S_ApplyEnduranceMoveSpeed(ent,speed);
    bool building = G_UnitIsStructure(ent);
    wc3SpeedLimit_t limits = { .value = speed,
        .minimum = ent->data.UnitBalance->minSpeed, .maximum = ent->data.UnitBalance->maxSpeed,
        .default_minimum = building ? game.constants.minBldgSpeed : game.constants.minUnitSpeed,
        .default_maximum = building ? game.constants.maxBldgSpeed : game.constants.maxUnitSpeed };
    float result=wc3_speed_limit_update(&limits);
    G_EndUnitStatusQuery(&statuses);
    return result;
}

/* Retail inventory changes update the queried maximum without publishing
 * an existing mover cap. Public setters and new orders publish that maximum.
 * TODO: MOVE-01.2 covers other effect notifications and their timing. */
static float unit_effective_speed(edict_t *ent) {
    return unit_effective_speed_with_bonus(ent, ent->movement.flat_speed_bonus);
}

/* Public current speed shares the status/profile consumer used by stepping
 * and group caps. It is independent of a particular group's slower cap. */
float S_UnitMoveSpeed(edict_t *ent) { return ent ? unit_effective_speed_with_bonus(ent, S_MoveSpeedBonus(ent)) : 0; }

float S_UnitDefaultMoveSpeed(edict_t const *ent) {
    /* TODO: MOVE-01.1 recovers the hero-specific default-speed contribution;
     * this immutable profile value closes the captured nonhero producers. */
    return ent && ent->data.UnitBalance ? ent->data.UnitBalance->speed : 0;
}

void S_SetUnitMoveSpeed(edict_t *ent, float speed) {
    if (!ent || M_UnitMoveDisabled(ent)) return;
    ent->unitinfo.MoveSpeed = speed;
    ent->unitinfo.move_flags |= BZ_UNIT_SPEED_SET;
    ent->movement.flat_speed_bonus = S_MoveSpeedBonus(ent);
    wc3Velocity_t v = { .vel = {ent->movement.velocity.x, ent->movement.velocity.y},
        .limit = unit_effective_speed(ent) };
    if (wc3_velocity_cap_world(&v)) {
        if (ent->movement.clock_valid) unit_commit_current_pose(ent);
        ent->movement.velocity = (vec2_t){v.vel[0], v.vel[1]};
    }

}

/* Slowest move speed across a group, so the whole group travels at it. */
static float move_group_speed(edict_t *const *units, uint32_t count) {
    float slowest = 0;
    FOR_LOOP(i, count) {
        float const s = S_UnitMoveSpeed(units[i]);
        if (s > 0 && (slowest == 0 || s < slowest)) {
            slowest = s;
        }
    }
    return slowest;
}

/* Retail re-resolves ownership before speed selection. A cohort token keeps
 * this contract independent of the cyclic waypoint storage. The full retail
 * member flags, shared override and decision/commit arrays remain GROUP-04.6. */
static float move_active_group_speed(edict_t const *self) {
    float slowest = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *member = g_edicts + i;
        if (!member->inuse || member->movement.group_id != self->movement.group_id ||
            M_IsDead(member) || !unit_is_walking(member) || !member->currentmove->think ||
            !member->goalentity)
            continue;
        float speed = unit_effective_speed(member);
        if (speed > 0 && (slowest == 0 || speed < slowest)) slowest = speed;
    }
    return slowest;
}

static uint32_t move_allocate_group_id(void) {
    if (!move_group_id_bound_valid) {
        FOR_LOOP(i,globals.num_edicts) {
#ifdef BZ_TESTS
            move_group_id_visits++;
#endif
            edict_t const *unit=g_edicts+i;
            if (!unit->inuse) continue;
            move_group_id_bound=MAX(move_group_id_bound,unit->movement.group_id);
            move_group_id_bound=MAX(move_group_id_bound,unit->movement.previous_request_id);
            FOR_LOOP(q,unit->order_queue.count) {
                unsigned slot=(unit->order_queue.head+q)%MAX_UNIT_ORDER_QUEUE;
                move_group_id_bound=MAX(move_group_id_bound,unit->order_queue.entries[slot].owner_context);
            }
        }
        FOR_LOOP(i,ARRAY_COUNT(level.move_groups))
            if(level.move_groups[i]->inuse)
                move_group_id_bound=MAX(move_group_id_bound,level.move_groups[i]->request_id);
        move_group_id_bound_valid=true;
    }
    bool used;
    do {
        if (++level.next_move_group_id == 0) ++level.next_move_group_id;
        used = false;
        if (level.next_move_group_id>move_group_id_bound) break;
        FOR_LOOP(i, globals.num_edicts) {
#ifdef BZ_TESTS
            move_group_id_visits++;
#endif
            edict_t const *unit=g_edicts+i;
            if (!unit->inuse) continue;
            FOR_LOOP(q,unit->order_queue.count) {
                unsigned slot=(unit->order_queue.head+q)%MAX_UNIT_ORDER_QUEUE;
                if (unit->order_queue.entries[slot].owner_context==level.next_move_group_id) used=true;
            }
            if (used || unit->movement.group_id == level.next_move_group_id ||
                    unit->movement.previous_request_id == level.next_move_group_id) {
                used = true;
                break;
            }
        }
        FOR_LOOP(i,ARRAY_COUNT(level.move_groups))
            if (level.move_groups[i]->inuse && level.move_groups[i]->request_id==level.next_move_group_id) used=true;
    } while (used);
    move_group_id_bound=MAX(move_group_id_bound,level.next_move_group_id);
    return level.next_move_group_id;
}

bool move_should_arrive(edict_t *ent, float move_distance) {
    vec2_t to_goal = Vector2_sub(&ent->goalentity->s.origin2, &ent->s.origin2);
    float distance = Vector2_len(&to_goal);

    if (distance <= move_distance) {
        return true;
    }

    /*
     * If the goal lies within this frame's movement corridor, snap to it
     * rather than letting the unit wobble around the destination.  This keeps
     * short path segments and near-goal collision nudges from producing a
     * visible back-and-forth at the endpoint.
     */
    vec2_t direction = { cosf(ent->s.angle), sinf(ent->s.angle) };
    float projected = Vector2_dot(&to_goal, &direction);
    if (projected < 0 || projected > move_distance + MOVE_ARRIVE_TOLERANCE) {
        return false;
    }

    float lateral = fabsf(to_goal.x * direction.y - to_goal.y * direction.x);
    return lateral <= MAX(MOVE_ARRIVE_TOLERANCE, ent->collision + MOVE_SLOT_MARGIN);
}

bool move_is_blocked(edict_t *ent, float distance, float move_distance) {
    float const settle_distance = move_distance + ent->collision + MOVE_SLOT_MARGIN;
    if (ent->movement.last_distance >= 0) {
        /* move_last_distance is the *closest* the unit has come to its goal (a
         * watermark), not just the previous frame's distance.  With move-time
         * block-and-slide a unit boxed in near its goal orbits it: distance
         * oscillates but never beats the watermark.  Measuring progress against
         * the best-so-far (instead of frame-to-frame) lets the stuck counter
         * accumulate through the orbit so the unit settles, instead of the
         * lateral motion resetting it every frame and walking forever. */
        float const improvement = ent->movement.last_distance - distance;
        float const moved = Vector2_distance(&ent->s.origin2, &ent->movement.last_origin);
        /* A valid slow step must beat the watermark before the settle window
         * expires. The old1-world-unit floor stopped speed2 walkers when the
         * owner cadence changed from100ms to30ms, despite continued motion.
         * TODO: replace this legacy guard with the full retail retry/task policy. */
        float const floor_step=move_distance>0 ? MIN(1.f,move_distance) : 1.f;
        float const min_progress = MAX(floor_step, move_distance * 0.05f);
        float const min_moved = MAX(floor_step, move_distance * 0.25f);

        /* "Near goal" is judged by the watermark (the closest the unit has
         * ever come), not the current position: once a unit has reached its
         * best distance and can no longer improve on it, it is stuck even if
         * its orbit around the blocked goal momentarily flings it back out
         * past settle_distance. */
        if (improvement >= min_progress) {
            ent->movement.blocked_frames = 0;
            ent->movement.last_distance = distance;     /* advance the watermark */
        } else if (ent->movement.last_distance <= settle_distance || moved < min_moved) {
            ent->movement.blocked_frames++;             /* near goal, or barely moving */
        } else {
            ent->movement.blocked_frames = 0;           /* far away but still making way */
        }
    } else {
        ent->movement.last_distance = distance;
    }

    ent->movement.last_origin = ent->s.origin2;
    return ent->movement.last_distance <= settle_distance
        ? ent->movement.blocked_frames >= MOVE_SETTLE_FRAMES
        : ent->movement.blocked_frames >= MOVE_BLOCKED_FRAMES;
}

/* Interaction walkers sometimes stop just outside a blocked building because
 * another worker occupies the final approach lane.  Reuse Move's established
 * near-goal settle window instead of duplicating its margin/frame constants in
 * each behavior.  This only reports true when the unit has both stopped making
 * progress and reached the same near-goal band where an ordinary Move would
 * settle; a wall or disconnected route farther away is not an arrival. */
bool move_is_settled_near_goal(edict_t *ent, float distance, float move_distance) {
    float const settle_distance = move_distance + ent->collision + MOVE_SLOT_MARGIN;
    bool const blocked = move_is_blocked(ent, distance, move_distance);
    return blocked && ent->movement.last_distance <= settle_distance;
}

/* Unit-target Move/Smart is a persistent follow order rather than a snapshot
 * point move. Keep the target entity authoritative so a moving ally can be
 * tracked and the retained goal can be resumed after opportunistic combat. */
static bool follow_target_is_valid(edict_t const *self, edict_t const *target) {
    uint32_t owner;

    if (!self || !target || !target->inuse || !(target->svflags & SVF_MONSTER) || M_IsDead((edict_t *)target)) {
        return false;
    }
    if (self->s.player >= MAX_PLAYERS || target->s.player >= MAX_PLAYERS) {
        return false;
    }
    owner = target->s.player;
    if (owner == self->s.player) {
        return true;
    }
    if (owner < PLAYER_NEUTRAL_AGGRESSIVE && level.mapinfo &&
        level.mapinfo->players[owner].playerType == kPlayerTypeNone) {
        return false;
    }
    return G_PlayerTreatsPlayerAsAlly(self->s.player, owner);
}

static bool follow_can_auto_attack(edict_t const *self) {
    if (!self || self->current_order_id == 851986 || !S_CargoAttacksEnabled(self) || S_AttackProfileRead(self, 0)->cooldown <= 0.0f ||
        (S_AttackProfileRead(self, 0)->damageBase <= 0 && S_AttackProfileRead(self, 0)->numberOfDice <= 0)) {
        return false;
    }
    return !level.mapinfo || level.mapinfo->players[self->s.player].playerType != kPlayerTypeNeutral;
}

float G_FollowStopRange(edict_t const *follower, edict_t const *target) {
    float configured;
    float collision_range;

    if (!follower || !target) return 0.0f;
    configured = G_UnitIsStructure(target)
        ? game.constants.structureFollowRange
        : game.constants.followRange;
    /* A pathing-footprint distance already includes the building extent, so
     * only the follower radius remains as its no-overlap lower bound. */
    if (!G_UnitIsStructure(target))
        return wc3_mul(MAX(wc3_float(0x3efae148),wc3_div(wc3_add(wc3_add(configured,follower->collision),target->collision),32)),32);
    /* TODO TARGET-01.2: retain the existing footprint-edge structure policy
     * until its complete native approach producer is captured. */
    collision_range = follower->collision;
    if (!G_UnitIsStructure(target) || !target->pathtex)
        collision_range += target->collision;
    return MAX(configured, collision_range);
}

/* Warsmash's unit canReach() tests a building target against its authored
 * pathing pixels instead of requiring the follower to approach the building
 * centre.  OpenRealm already uses the same footprint distance for Attack,
 * Repair, harvesting, militia, and cargo interactions; follow/rally must use
 * it too or a freshly trained unit can be nudged legally out of its producer
 * and then immediately walk back into the producer's blocked footprint.
 *
 * CM_DistanceToPathingFootprint() measures from the follower centre to the
 * blocked footprint edge.  StructureFollowRange remains the authored follow
 * margin, while the follower radius is the hard no-overlap lower bound.  The
 * target collision radius is intentionally not added when a real footprint is
 * available because that would count the building extent twice. */
static bool follow_footprint_distance(edict_t const *follower, edict_t const *target,
                                      float *distance) {
    float footprint;

    if (!follower || !target || !distance ||
        !G_UnitIsStructure(target) || !target->pathtex) {
        return false;
    }
    footprint = CM_DistanceToPathingFootprint(target, &follower->s.origin2);
    if (footprint >= FLT_MAX) return false;

    *distance = footprint;
    return true;
}

static void ai_follow_walk(edict_t *ent) {
    edict_t *target = ent->movement.follow_target;
    float distance;
    float follow_range;
    bool standing;

    if (!follow_target_is_valid(ent, target) ||
        ent->movement.follow_target_spawn_time != target->spawn_time) {
        ent->movement.follow_target = NULL;
        if (ent->goalentity == target) S_SetMoveGoal(ent, &ent->goalentity, NULL);
        unit_stand(ent);
        return;
    }

    S_SetMoveGoal(ent, &ent->goalentity, target);
    if (follow_can_auto_attack(ent) && G_ShouldAcquireThisFrame(ent)) {
        edict_t *enemy = G_FindNearestEnemy(ent, G_AcquisitionRange(ent));
        if (enemy) {
            order_attack(ent, enemy);
            return;
        }
    }

    if (move_unit_group(ent)) return;

    distance = M_DistanceToGoal(ent);
    follow_range = G_FollowStopRange(ent, target);
    follow_footprint_distance(ent, target, &distance);
    standing = G_AnimationHasPrimary(ent->animation, "stand");
    if (distance <= follow_range) {
        if (!standing) {
            move_reset_progress(ent);
            unit_setanimation(ent, "stand");
        }
        return;
    }

    if (standing) move_reset_progress(ent);
    unit_changeangle(ent);
    if (ent->movement.flow_unreachable) {
        unit_setanimation(ent, "stand");
        return;
    }
    unit_moveindirection(ent);
}

static umove_t follow_move_walk = { .animation="walk", .think=ai_follow_walk, .proc=CAbilityMove,
    .scheduled_think=true, .sample_pose=S_PublishMovement, .leave=move_leave };
static umove_t follow_move_legacy = { .animation="walk", .think=ai_follow_walk, .proc=CAbilityMove };

static bool move_is_following(edict_t const *unit) {
    return unit && (unit->currentmove==&follow_move_walk || unit->currentmove==&follow_move_legacy);
}

void order_follow_resume(edict_t *self) {
    edict_t *target;

    if (!self || S_GoldMineWorkerIsInside(self) || (self->aiflags & AI_IMMOBILE)) {
        return;
    }
    target = self->movement.follow_target;
    if (!follow_target_is_valid(self, target) ||
        self->movement.follow_target_spawn_time != target->spawn_time) {
        self->movement.follow_target = NULL;
        if (self->goalentity == target) S_SetMoveGoal(self, &self->goalentity, NULL);
        unit_stand(self);
        return;
    }
    S_SetMoveGoal(self, &self->goalentity, target);
    self->movement.holding_position = false;
    move_reset_progress(self);
    bool physical=!(self->aiflags&AI_FLYING) && !(target->aiflags&AI_FLYING) && !G_UnitIsStructure(target);
    unit_setmove(self, physical ? &follow_move_walk : &follow_move_legacy);
    if (physical) {
        self->movement.flat_speed_bonus=S_MoveSpeedBonus(self);
        unit_commit_current_pose(self); self->movement.pose_clock=level.pathing_clock;
        self->movement.clock_valid=true;
        move_start_follow_group(self,target,false);
        unit_setanimation(self,"stand");
    }
    /* TODO TARGET-02.1: flight and structure-footprint approach producers retain
     * their existing traversal pending full native physical-owner evidence. */
}

void order_follow(edict_t *self, edict_t *target) {
    if (!self || (self->aiflags & AI_IMMOBILE) || S_GoldMineWorkerIsInside(self) ||
        !follow_target_is_valid(self, target)) {
        return;
    }
    S_SetMoveGoal(self, &self->movement.attackmove_waypoint, NULL);
    S_SetMoveGoal(self, &self->movement.patrol_a, NULL);
    S_SetMoveGoal(self, &self->movement.patrol_b, NULL);
    S_SetMoveGoal(self, &self->movement.patrol_target, NULL);
    self->movement.follow_target = target;
    self->movement.follow_target_spawn_time = target->spawn_time;
    self->movement.holding_position = false;
    order_follow_resume(self);
}

bool S_IssueFollowOrder(edict_t *self, edict_t *target, uint32_t order_id) {
    order_follow(self, target);
    if (!self || self->goalentity != target || !move_is_following(self))
        return false;
    self->current_order_id = order_id;
    return true;
}

static umove_t move_move_hold = { "stand", NULL, NULL, CAbilityMove };

bool move_is_terminal_hold(edict_t const *ent) {
    return ent && ent->currentmove == &move_move_hold;
}

static void move_hold(edict_t *ent) {
    if (ent->movement.guard_state == GUARD_RETURNING) {
        /* Treat the normal near-goal blocked settle as completion of the
         * internal guard return; do not strand the unit in an active Move pose. */
        ent->movement.guard_state = GUARD_IDLE;
        S_SetMoveGoal(ent, &ent->goalentity, NULL);
        unit_stand(ent);
        return;
    }
    /* A terminal blocked/unreachable Move is complete for queue purposes.
     * Continue a Shift chain instead of stranding pending commands behind the
     * legacy hold pose. */
    if (G_UnitStartNextQueuedOrder(ent)) return;
    ent->build = NULL;
    ent->s.renderfx &= ~RF_NO_UBERSPLAT;
    ent->s.ability = 0;
    ent->movement.last_distance = 0;
    ent->movement.blocked_frames = 0;
    unit_setmove(ent, &move_move_hold);
}

/* Actual zero-range point Move publishes .49 fine cells. Target approaches
 * and other ability owners retain their own arrival contract (TARGET-01.2/3).
 * Scheduled Move predicts from its last primary-clock commit. */
static bool move_point_arrival(edict_t *ent) {
    wc3GridPose_t pose; unit_grid_pose(ent, &pose);
    float velocity[2] = {ent->movement.velocity.x, ent->movement.velocity.y};
    float elapsed = level.scheduled_think ? (ent->movement.clock_valid ?
        wc3_elapsed(&level.pathing_clock, &ent->movement.pose_clock) : 0) : 10.0f / FRAMETIME;
    wc3_grid_step(&pose, velocity, elapsed);
    vec2_t forecast = {pose.world[0], pose.world[1]};
    float cell = CM_PathCellWorldSize();
    float target[2] = {ent->goalentity->s.origin2.x, ent->goalentity->s.origin2.y};
    moveGroupMember_t const *member=move_find_member(ent);
    wc3Arrival_t a = { .heading = ent->s.angle, .range = member ? member->arrival_range : wc3_float(0x3efae148),
        .flags = ent->movement.point_forced_arrival ? 0x10000u : 0 };
    /* Stock WPM cells are32. Preserve authoritative cell geometry for synthetic maps,
     * while the native retained pose remains in32-unit scalar coordinates. */
    FOR_LOOP(k, 2) {
        a.source[k] = wc3_div(wc3_mul(pose.grid[k], 32), cell);
        a.target[k] = wc3_grid_coordinate(target[k], pose.origin[k], cell);
    }
    moveFineRoute_t *route=&ent->movement.fine_route;
    if (route->group_count && route->group_index<route->group_count) {
        vec2_t fine=route->group_index ? (vec2_t){wc3_mul(route->group_points[route->group_index].x,2),wc3_mul(route->group_points[route->group_index].y,2)} : route->group_goal;
        a.target[0]=fine.x; a.target[1]=fine.y;
    }
    bool reached = wc3_arrival_update(&a);
    if (!a.in_range || !M_MoveIsValid(ent, &forecast)) return false;
    /* Original range acceptance stops translation even while the separate
     * arrival heading gate still needs a turn. Commit the old velocity step
     * once, then publish zero; never snap the unit onto the destination. */
    if (!reached)
        unit_turn_toward(ent, wc3_vector_heading(wc3_sub(a.target[0], a.source[0]),
                                                wc3_sub(a.target[1], a.source[1])));
    ent->s.angle = wc3_facing_angle(ent->s.angle);
    ent->movement.velocity = (vec2_t){0};
    unit_commit_pose(ent, &pose);
#ifdef BZ_TESTS
    if (level.scheduled_think && move_test_motion_commit) move_test_motion_commit(ent);
#endif
    if (reached) {
        ent->movement.point_forced_arrival=false;
        if (G_AdvanceUnitMoveGroupDestination(route)) {
            ent->movement.path.valid=false;
            return true;
        }
        if (!S_UnitAbilityMoveArrive(ent)) ent->stand(ent);
    }
    return true;
}

static void ai_move_walk(edict_t *ent) {
    moveRouteResult_t route_result=MOVE_ROUTE_FAILED;
    if(ent->movement.pause_order_id)return;
    if (ent->movement.type_rebind_pending) return;
    if (move_find_member(ent)) return; /* Shared owner stages all members before any commit. */
    moveGroup_t *owner=move_unit_group(ent);
    if (owner && owner->individual && level.scheduled_frame && !owner->ticking) return;
    float distance = M_DistanceToGoal(ent);
    float move_distance = unit_movedistance(ent);
    float const settle_distance = move_distance + ent->collision + MOVE_SLOT_MARGIN;
    bool blocked;
    bool point_order;

    if (S_UnitIsCycloned(ent) || G_UnitStatusLevel(ent, MAKEFOURCC('B', 'E', 'e', 'r'))
        || S_PurgeIsImmobilized(ent)) {
        move_route_wait_diag(ent, false, MOVE_DIAG_NONE);
        ent->stand(ent);
        return;
    }

    if (move_displacement_active(ent) && !move_displacement_reached(ent)) {
        /* A retained solid widget can make escape impossible. Retail's original
         * can't-path recovery drains this Move without changing the footprint;
         * the old early return never evaluated progress and walked forever.
         * TODO: match the retail retry/task cadence after NUM-02.3; use the
         * existing engine progress budget until its simulation clock is ported. */
        if (!move_static_point(ent, &ent->s.origin2) &&
            move_is_blocked(ent, Vector2_distance(&ent->s.origin2, &ent->movement.displacement_target), move_distance)) {
            move_cancel_displacement(ent);
            ent->stand(ent);
            return;
        }
        unit_changeangle(ent);
        unit_moveindirection(ent);
        return;
    }

    /* Completing a temporary escape can expose the ordinary point order in
     * this same visit. Classify it after retiring displacement ownership. */
    point_order = (ent->current_order_id == G_OrderId("move") || ent->current_order_id == G_OrderId("smart")) &&
        ent->goalentity && (ent->goalentity->svflags & SVF_MOVE_WAYPOINT);

    if (point_order && (!ent->movement.group_id || (owner && owner->individual))) {
        movePathQuery_t query=move_route_query(ent,(moveRoutePoint_t){&ent->goalentity->s.origin2,ent->collision,MOVE_AVOID_GENERIC});
        vec2_t destination;
        uint32_t revision=ent->movement.fine_route.group_revision;
        vec2_t previous=ent->movement.fine_route.group_goal;
        if (query.units && !G_UnitMoveGroupDestination(&query,&ent->movement.fine_route,&destination)) {
            /* TODO GROUP-04.6: blocked-source group recovery/physical admission.
             * The existing member recovery remains visible through its route state. */
            ent->movement.fine_route.group_count=0;
            if(ent->movement.fine_route.group_admission.waiting) {
                ent->movement.heading=ent->s.angle;ent->movement.turn_blocked=true;
                unit_moveindirection(ent);return;
            }
        }
        if (ent->movement.fine_route.group_count && (revision!=ent->movement.fine_route.group_revision ||
            previous.x!=ent->movement.fine_route.group_goal.x || previous.y!=ent->movement.fine_route.group_goal.y))
            ent->movement.path.valid=false;
#ifdef BZ_TESTS
        if (level.scheduled_think && ent->movement.fine_route.group_count && move_test_group_route)
            move_test_group_route(NULL,ent);
#endif
    }
    if (point_order && move_point_arrival(ent)) return;
    if (point_order && move_point_retry_endpoint(ent)) {unit_moveindirection(ent);return;}

    if (!point_order && move_should_arrive(ent, move_distance)) {
        /* A point inside the step budget still requires facing inside the propagation window;
         * the old snap bypassed the movement decision and completed the order while turning. */
        unit_changeangle(ent);
        if (ent->movement.turn_blocked) return;
#ifdef WC3_DEBUG_BUILD
        if (ent->class_id == MAKEFOURCC('h','p','e','a'))
            fprintf(stderr, "WC3_BUILD move-arrive unit=%ld origin=(%.1f,%.1f) target=(%.1f,%.1f) distance=%.1f goal=%ld\n",
                    (long)(ent - g_edicts), ent->s.origin2.x, ent->s.origin2.y,
                    ent->goalentity ? ent->goalentity->s.origin2.x : 0.0f,
                    ent->goalentity ? ent->goalentity->s.origin2.y : 0.0f,
                    distance, ent->goalentity ? (long)(ent->goalentity - g_edicts) : -1L);
#endif
        /* Snap exactly onto the goal only if that spot is actually free; if the
         * goal is occupied (e.g. ordered onto another unit, or an attack target)
         * stop where we are rather than overlapping it. */
        if (M_MoveIsValid(ent, &ent->goalentity->s.origin2))
            unit_commit_step(ent, &ent->goalentity->s.origin2);
        if (S_UnitAbilityMoveArrive(ent)) return;
        if (ent->movement.guard_state == GUARD_RETURNING) {
            ent->movement.guard_state = GUARD_IDLE;
            S_SetMoveGoal(ent, &ent->goalentity, NULL);
        }
        ent->stand(ent);
    } else {
        blocked = move_is_blocked(ent, distance, move_distance);

        /* Plain Move owns a private destination, so location-aware steering
         * uses the mover footprint and retargets a disconnected click before
         * this behavior treats an unresolved route as terminal. */
        unit_changeangle(ent);

        if (ent->movement.turn_blocked) {
            unit_moveindirection(ent);
            return;
        }
        if (ent->movement.flow_unreachable) {
            vec2_t approach;
            vec2_t direction;
            /* A newly started construction can split the old route field while
             * a cinematic Peasant is still travelling to a broad trigger
             * region. Preserve the point order: retarget only the movement
             * endpoint to the closest reachable cell, and never consume the
             * order as a terminal hold merely because the original point is
             * temporarily behind the construction footprint. */
            pathAccelParams_t query = { &ent->s.origin2, &ent->goalentity->s.origin2,
                                        ent->collision, M_UnitStaticPathingFlags(ent) };
            if (G_ClosestReachableMovePoint(&query, &approach)) {
#ifdef WC3_DEBUG_BUILD
                if (ent->class_id == MAKEFOURCC('h','p','e','a'))
                    fprintf(stderr, "WC3_BUILD move-approach unit=%ld from=(%.1f,%.1f) approach=(%.1f,%.1f) target=(%.1f,%.1f) goal=%ld\n",
                            (long)(ent - g_edicts), ent->s.origin2.x, ent->s.origin2.y,
                            approach.x, approach.y, ent->goalentity->s.origin2.x,
                            ent->goalentity->s.origin2.y, (long)(ent->goalentity - g_edicts));
#endif
                /* unit_changeangle() normally owns this fallback. Keep the
                 * original waypoint authoritative if it could not install a
                 * temporary approach route for this tick. */
            }
            /* The closest-cell query can return the original point even when
             * the flow interpolation has no descending neighbour. Use the
             * persistent A* accelerator for the actual detour before falling
             * back to local steering; a cinematic move must not be cancelled. */
            if ((route_result=unit_accel_direction(ent, (moveRoutePoint_t){&ent->goalentity->s.origin2, ent->collision, MOVE_AVOID_GENERIC}, &direction))) {
                ent->movement.flow_unreachable = false;
                ent->movement.flow_direct = false;
                unit_apply_route_heading(ent, &direction, MOVE_AVOID_GENERIC, route_result);
                unit_moveindirection(ent);
                return;
            }
            ent->movement.flow_unreachable = false;
            ent->movement.flow_direct = true;
            unit_changeangle_towards_point(ent, &ent->goalentity->s.origin2);
            unit_moveindirection(ent);
            return;
        }
        if (!ent->movement.flow_direct && !ent->movement.path.valid && !ent->movement.flow_generation &&
            !ent->movement.route_resume_active) {
            move_route_wait_diag(ent, true, MOVE_DIAG_ROUTE_WAIT);
            return; /* resumable route field is still being built */
        }
        move_route_wait_diag(ent, false, MOVE_DIAG_NONE);
        if (ent->movement.flow_goal_reached) {
            return;
        }

        /* Retail move orders keep trying when another unit temporarily blocks
         * the path.  Preserve the old near-goal settle behavior so an occupied
         * final slot does not orbit forever, but do not cancel a distant move
         * merely because local avoidance failed for a short period. */
        /* Scheduled retail point routes own arrival and retry. A turn wait
         * increments the legacy progress counter, but must not terminate
         * the Move before its fine arrival test accepts the destination. */
        if (blocked && (!level.scheduled_think || !point_order || !ent->movement.fine_route.group_count) &&
            ent->movement.last_distance <= settle_distance) {
            move_hold(ent);
            return;
        }
        if (blocked)
            ent->movement.blocked_frames = 0;
        unit_moveindirection(ent);
    }
}

static umove_t move_move_walk = { .animation = "walk", .think = ai_move_walk,
    .proc = CAbilityMove, .scheduled_think = true, .sample_pose = S_PublishMovement, .leave = move_leave };

/* Identify the ordinary walk move so spell approach orders can detect replacement. */
bool move_is_active_order_walk(edict_t const *ent) {
    return ent && ent->currentmove == &move_move_walk;
}

bool S_UnitIsEntanglingRooted(edict_t const *unit) {
    return unit && G_UnitStatusLevel(unit, MAKEFOURCC('B', 'E', 'e', 'r'));
}

/* Move owns translation eligibility. False means the unit cannot change
 * position this tick (immobile, Cyclone, Entangling Roots, Ensnare, Purge
 * pause). Entangling Roots is also a disarm; attack owns that separate check. */
bool S_UnitCanTranslate(edict_t const *unit) {
    if (!unit) return false;
    if ((unit->aiflags & AI_IMMOBILE) || S_UnitIsCycloned(unit) ||
        S_UnitIsEntanglingRooted(unit) || S_UnitIsEnsnared(unit) || S_PurgeIsImmobilized(unit)) return false;
    return true;
}

/* Set the unit's move target and begin walking.
 * goalentity must be a waypoint or any entity whose origin is the destination. */
void order_move(edict_t *self, edict_t *target) {
    if (S_GoldMineWorkerIsInside(self))
        return;
    if ((self->aiflags & AI_IMMOBILE) || S_UnitIsCycloned(self) || S_UnitIsEntanglingRooted(self)
        || S_UnitIsEnsnared(self) || S_PurgeIsImmobilized(self))
        return;
    if (self->movement.clock_valid) unit_commit_current_pose(self);
    move_cancel_displacement(self);
    S_SetMoveGoal(self, &self->goalentity, target);
    self->attack_target_spawn_time = 0;
    S_SetMoveGoal(self, &self->movement.attackmove_waypoint, NULL);
    S_SetMoveGoal(self, &self->movement.patrol_a, NULL);
    S_SetMoveGoal(self, &self->movement.patrol_b, NULL);
    S_SetMoveGoal(self, &self->movement.patrol_target, NULL);
    self->movement.follow_target = NULL;
    self->movement.holding_position = false;
#ifdef WC3_DEBUG_BUILD
    if (self->class_id == MAKEFOURCC('h','p','e','a'))
        fprintf(stderr, "WC3_BUILD move-order unit=%ld origin=(%.1f,%.1f) target=(%.1f,%.1f) goal=%ld\n",
                (long)(self - g_edicts), self->s.origin2.x, self->s.origin2.y,
                target->s.origin2.x, target->s.origin2.y, (long)(target - g_edicts));
#endif
    self->movement.flat_speed_bonus = S_MoveSpeedBonus(self);
    move_reset_progress(self);
    unit_setmove(self, &move_move_walk);
    unit_commit_current_pose(self);
    self->movement.pose_clock = level.pathing_clock;
    self->movement.clock_valid = true;
    /* No route heading exists at submission time. Hold the stand pose instead
     * of showing a walking unit facing its previous, often opposite, heading. */
    unit_setanimation(self, "stand");
}

/* A public point command owns the current user head. Internal approaches use
 * order_move without replacing that identity. Queue replay comes here only
 * when this command actually becomes active. */
void S_IssueMoveOrder(edict_t *self, edict_t *goal, uint32_t order_id) {
    /* Public admission69a881 stops through05ca50 before the new point task,
     * even for an idle actor embedded in a peer. Recover before cohort seeding. */
    move_leave(self); S_RecoverStoppedUnitPosition(self);
    order_move(self, goal);
    if (self->goalentity == goal && self->currentmove == &move_move_walk)
        self->current_order_id = order_id;
}

/* AI admission remains an independent follower until the all-entered gate.
 * Saved physical state retains this phase without a process-owned bot VM. */
bool S_IssueCaptainHomeMove(edict_t *self, botCaptain_t const *captain) {
    if (!captain->home_actor) {
        fprintf(stderr,"WC3 Move: captain home has no retained virtual actor\n");
        return false;
    }
    if (!G_IssueUnitPointOrder(self,"move",&captain->home,false,self->s.player,0)) return false;
    uint32_t members=ARRAY_COUNT(captain->units);
    captain->home_actor->movement.captain_actor_members=members;
    captain->home_actor->movement.captain_actor_siege=false;
    FOR_EACH_ARRAY(edict_t *, member,captain->units)
        if (S_UnitHasLongRangeSiegeAttack(*member)) captain->home_actor->movement.captain_actor_siege=true;
    self->movement.captain_home.actor=captain->home_actor;
    S_TrackMoveTimers(self);
    self->movement.captain_home.roster_actor=captain->home_actor;
    S_TrackMoveTimers(self);
    FOR_LOOP(i,members) {
        edict_t *member=captain->units[i];
        if (member==self || member->movement.captain_home.roster_actor==captain->home_actor)
            member->movement.captain_home.member_index=i;
    }
    if(self->movement.captain_home.entered) {
        botCaptain_t *owner=move_actor_captain(captain->home_actor);
        if(owner)owner->entered_members--;
    }
    self->movement.captain_home.entered=false;
    self->movement.captain_home.outer=false;
    self->movement.captain_home.home=captain->home;
    self->movement.captain_home.due=captain->created;
    do wc3_clock_advance(&self->movement.captain_home.due,1,0);
    while (self->movement.captain_home.due.epoch==level.pathing_clock.epoch ?
        self->movement.captain_home.due.time<=level.pathing_clock.time :
        (int32_t)(self->movement.captain_home.due.epoch-level.pathing_clock.epoch)<0);
    self->movement.captain_home.active=true;
    /* Native admission creates a private target-follow physical owner for
     * each recruit. The task retains an AI approach range plus mover radius,
     * independently of its later shared formation destination. */
    move_start_follow_group(self,captain->home_actor,true);
    return true;
}

/* New temporary enrollment retires the old public task before the internal
 * AI callback reissues Move. It does not emit another issued-order event. */
bool S_AdmitTemporaryCaptainUnit(edict_t *self,botCaptain_t const *captain,edict_t *defense) {
    edict_t *actor=captain->home_actor;
    order_stop_cleanup(self);
    if (!actor) return false;
    wc3GridPose_t source;unit_predicted_pose(actor,&source);
    vec2_t from={source.world[0],source.world[1]},to=self->s.origin2;
    bool follow=unit_is_flying(self) || G_CaptainMoveReachable(actor,NULL,self,&from,&to);
    vec2_t point=captain->home;
    if (!follow && !G_CaptainMoveReachable(actor,self,NULL,&to,&captain->home)) {
        edict_t *fallback=defense;
        if (fallback) {
            point=fallback->s.origin2;
            if (!G_CaptainMoveReachable(actor,self,fallback,&to,&point)) fallback=NULL;
        }
        /* Native058900 queries the committed fallback position. The
         * predicted Captain pose above belongs only to reachability. */
        if (!fallback) point=actor->s.origin2;
    }
    S_IssueMoveOrder(self,Waypoint_add(&point),G_OrderId("move"));
    if (self->currentmove!=&move_move_walk) return false;
    uint32_t count=ARRAY_COUNT(captain->units);
    actor->movement.captain_actor_members=count;
    actor->movement.captain_actor_siege=false;
    FOR_LOOP(i,count) {
        edict_t *member=captain->units[i];
        if (S_UnitHasLongRangeSiegeAttack(member)) actor->movement.captain_actor_siege=true;
        if (member==self || member->movement.captain_home.roster_actor==actor)
            member->movement.captain_home.member_index=i;
    }
    self->movement.captain_home.actor=actor;
    self->movement.captain_home.roster_actor=actor;
    self->movement.captain_home.home=captain->home;
    self->movement.captain_home.due=captain->created;
    do wc3_clock_advance(&self->movement.captain_home.due,1,0);
    while (self->movement.captain_home.due.epoch==level.pathing_clock.epoch ?
        self->movement.captain_home.due.time<=level.pathing_clock.time :
        (int32_t)(self->movement.captain_home.due.epoch-level.pathing_clock.epoch)<0);
    self->movement.captain_home.active=follow;
    self->movement.captain_home.entered=self->movement.captain_home.outer=false;
    S_TrackMoveTimers(self);
    if (follow) move_start_follow_group(self,actor,true);
    return true;
}

/* Individual owners keep stable addresses when callbacks grow the slot array. */
static moveGroup_t *move_alloc_group(void) {
    move_prepare_group_order();
    if (level.next_move_group_sequence==UINT64_MAX) gi.error("Move: physical owner sequence exhausted");
    uint64_t sequence=++level.next_move_group_sequence;
    moveGroup_t *group=NULL;
    while(move_group_first_free<ARRAY_COUNT(level.move_groups)) {
        uint32_t slot=move_group_first_free++;
        if(level.move_groups[slot]->inuse)continue;
        group=level.move_groups[slot];group->slot=slot;break;
    }
    if(group)goto acquired;
    if (ARRAY_COUNT(level.move_groups)==level.move_group_capacity) {
        uint32_t capacity=level.move_group_capacity ? level.move_group_capacity*2 : 16;
        moveGroup_t **groups=realloc(level.move_groups,capacity*sizeof(*groups));
        if (!groups) gi.error("Move: cannot allocate %u group slots",capacity);
        level.move_groups=groups; level.move_group_capacity=capacity;
    }
    group=calloc(1,sizeof(*group));
    if (!group) gi.error("Move: cannot allocate a physical group");
    group->slot=ARRAY_COUNT(level.move_groups);
    level.move_groups[ARRAY_COUNT(level.move_groups)++]=group;
    move_group_first_free=ARRAY_COUNT(level.move_groups);
acquired:
    group->sequence=sequence;group->older=move_group_head;group->newer=NULL;
    if(move_group_head)move_group_head->newer=group;
    move_group_head=group;
    return group;
}

/* Native Shift preserves the common point and publishes the latest submitted
 * request identity independently of queued activation. Each completion can
 * start alone;5faaf0 rebuilds a matching
 * nearby cohort when another member starts the same point. */
static bool move_queue_group_point(groupPointOrder_t const *request) {
    uint32_t context=move_allocate_group_id();
    bool any=false;
    FOR_LOOP(i,request->count) {
        edict_t *unit=request->units[i].unit;
        if (!unit->inuse || unit->spawn_time!=request->units[i].spawn || G_IsDeferredFree(unit) ||
            M_IsDead(unit) || (unit->aiflags&AI_IMMOBILE) || G_BuildingUpgradeActive(unit) ||
            !S_AncientCanReceiveOrder(unit)) continue;
        bool active=G_UnitHasActiveOrder(unit);
        if (!G_QueueUnitOrder(unit,request->order,UNIT_ORDER_TARGET_POINT,request->point,NULL,
                request->issuer_player,0,0)) continue;
        unitOrderQueue_t *queue=&unit->order_queue;
        unsigned slot=(queue->head+queue->count-1)%MAX_UNIT_ORDER_QUEUE;
        queue->entries[slot].owner_context=context;
        unit->movement.previous_request_id=context;
        if (!active) G_UnitStartNextQueuedOrder(unit);
        any=true;
        G_PublishIssuedPointOrder(unit,request->order_id,request->point,request->issuer_player,request->order);
    }
    return any;
}

static void move_group_seed_route(moveGroup_t *group);

/* Native target Move approaches once, then a persistent Follow task creates
 * another physical owner while the public Smart/Move head remains retained. */
/* Original5fd270 admits a nearby target with half its current edge distance;
 * its later persistent task restores the authored FollowRange. */
static float move_follow_approach_range(edict_t *unit, edict_t *target, bool persistent) {
    if (target->movement.captain_actor_type) {
        /* Original9d86f0:70 + .6*maximum enabled attack range. The target
         * wrapper05a5c0 subsequently adds both physical radii and divides32.
         * A virtual captain has zero radius. The physical member stores this
         * result, including across save/load and later weapon changes.
         * Temporary units return50 before Hero/siege adjustment. Native Hero
         * classification here tests the rawcode, independently of Hero stats. */
        if ((unit->aiflags&AI_ILLUSION) || S_UnitHasTimedLife(unit))
            return wc3_div(wc3_add(50,MAX(1,unit->collision)),32);
        float attack_range=0;
        bool armed=S_UnitAttackApproachRange(unit,&attack_range);
        float world=armed ?
            wc3_add(wc3_mul(attack_range,wc3_float(0x3f19999a)),70) : 300;
        uint32_t first=unit->class_id&0xffu;
        if (first>='A' && first<='Z' && world<600) world=600;
        if (target->movement.captain_actor_siege) world=wc3_add(world,200);
        return MAX(wc3_float(0x3efae148),wc3_div(wc3_add(world,MAX(1,unit->collision)),32));
    }
    float range=wc3_div(G_FollowStopRange(unit,target),32);
    if (persistent) return range;
    wc3GridPose_t source,point;
    unit_predicted_pose(unit,&source); unit_predicted_pose(target,&point);
    float x=wc3_sub(point.grid[0],source.grid[0]),y=wc3_sub(point.grid[1],source.grid[1]);
    float distance2=wc3_add(wc3_mul(x,x),wc3_mul(y,y));
    float range2=wc3_mul(range,range);
    if (distance2>=range2 && wc3_float(wc3_float_bits(wc3_sub(distance2,range2))&0x7fffffffu)>=wc3_float(0x3a83126f)) return range;
    float target_radius=wc3_div(MAX(1,target->collision),32),source_radius=wc3_div(MAX(1,unit->collision),32);
    float edge=wc3_sub(wc3_sub(wc3_sqrt(distance2),target_radius),source_radius);
    float world=wc3_mul(MAX(0,edge),32);
    return MAX(wc3_float(0x3efae148),wc3_div(wc3_add(wc3_add(wc3_div(world,2),wc3_mul(source_radius,32)),wc3_mul(target_radius,32)),32));
}

static void move_start_follow_group(edict_t *unit, edict_t *target, bool persistent) {
    /* Move->Follow shares the procedure, so unit_setmove need not dispatch
     * leave. Transfer physical ownership before installing its successor. */
    move_detach_group(unit);
    moveGroup_t *group=move_alloc_group();
    group->inuse=group->ticking=true; group->id=move_allocate_group_id();
    group->target=target; group->target_spawn=target->spawn_time;
    group->flags=0x1000u|(persistent ? 0x801u : 0); group->age=UINT32_MAX;
    if (S_UnitHasAbilityFlags(target,AB_MOVE_TARGET_NO_WARP)) group->flags|=0x10u;
    group->radius=unit->collision; group->request_id=unit->movement.previous_request_id;
    wc3GridPose_t pose; unit_predicted_pose(target,&pose);
    group->goal=(vec2_t){pose.world[0],pose.world[1]};
    group->members[group->count++]=(moveGroupMember_t){.unit=unit,.spawn=unit->spawn_time,
        .arrival_range=move_follow_approach_range(unit,target,persistent)};
    unit->movement.group_id=group->id;
    move_group_seed_route(group); group->ticking=false;
}

static bool move_start_queued_group(edict_t *unit, unitOrder_t const *queued) {
    if (!queued->owner_context || queued->target_type!=UNIT_ORDER_TARGET_POINT) return false;
    S_IssueMoveOrder(unit,Waypoint_add(&queued->point),G_OrderId(queued->order));
    if (unit->currentmove!=&move_move_walk || !unit->goalentity) return false;
    moveGroup_t *group=move_alloc_group();
    group->inuse=group->ticking=true; group->id=move_allocate_group_id();
    group->request_id=unit->movement.previous_request_id; group->goal=queued->point; group->age=UINT32_MAX;
    group->members[group->count++]=(moveGroupMember_t){.unit=unit,.spawn=unit->spawn_time};
    group->radius=unit->collision; unit->movement.group_id=group->id;
    wc3GridPose_t source; unit_predicted_pose(unit,&source);
    FOR_LOOP(g,ARRAY_COUNT(level.move_groups)) {
        moveGroup_t *peer=level.move_groups[g];
        if (peer==group || !peer->inuse ||
            peer->goal.x!=group->goal.x || peer->goal.y!=group->goal.y ||
            group->count+peer->count>BZ_WC3_GROUP_ORDER_UNITS) continue;
        bool nearby=false;
        FOR_LOOP(i,peer->count) {
            edict_t *other=peer->members[i].unit;
            if (other->movement.previous_request_id!=unit->movement.previous_request_id) continue;
            wc3GridPose_t pose; unit_predicted_pose(other,&pose);
            float dx=wc3_sub(source.grid[0],pose.grid[0]),dy=wc3_sub(source.grid[1],pose.grid[1]);
            uint32_t distance=wc3_int_bits(wc3_float_bits(wc3_sqrt(wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy)))));
            if (distance<=40 && M_UnitStaticPathingFlags(other)==M_UnitStaticPathingFlags(unit)) nearby=true;
        }
        if (!nearby) continue;
        FOR_LOOP(i,peer->count) {
            edict_t *other=peer->members[i].unit;
            group->members[group->count++]=(moveGroupMember_t){.unit=other,.spawn=other->spawn_time};
            other->movement.group_id=group->id;
            if (other->collision>group->radius) group->radius=other->collision;
        }
        move_release_group(peer);
    }
    /* TODO GROUP-04.6: accelerated preferred-distance, range90 and wider
     * neighbor producer policies need original witnesses before extension. */
    move_group_seed_route(group);
    group->ticking=false;
    return true;
}

/* Move owns the shared request; generic order admission still handles each
 * candidate's validation, Smart rally behavior and issued-order callbacks. */
/* A prepared packet supplies the physical owner after each ordinary admission.
 * Keep this stack-scoped producer separate from nested orders issued by callbacks. */
static struct { edict_t *unit; vec2_t point; } move_group_admission;

static bool move_group_captain_order(groupPointOrder_t const *request,uint64_t shared_id,edict_t *target) {
    if (!request->count) return false;
    if (request->queued) return move_queue_group_point(request);
    moveGroup_t *group=move_alloc_group();
    group->inuse=group->ticking=true; group->id=move_allocate_group_id();
    group->goal=*request->point; group->age=UINT32_MAX;
    if (target) {
        wc3GridPose_t pose; unit_predicted_pose(target,&pose);
        group->goal=(vec2_t){pose.world[0],pose.world[1]};
        group->target=target; group->target_spawn=target->spawn_time;
    }
    /* Native89caf0 sets canonical2/4/8 from packet10;16bdb0 copies
     * them to the physical owner before its first tick. */
    group->flags=request->formation_toggle ? 14u : 0;
    if (shared_id) {
        moveShared_t *shared=S_FindMoveShared(shared_id);
        if (!shared) gi.error("Move: missing new shared parameter owner");
        if (shared->references==UINT32_MAX) gi.error("Move: shared parameter reference overflow");
        group->shared_id=shared_id; shared->references++;
        /* Target policy0 omits100; activation enables persistent completion.
         * Both captain families retain800 and extra target refresh400. */
        group->flags|=target ? 0x1c01 : 0xd00;
    }
    bool any=false;
    FOR_LOOP(i,request->count) {
        edict_t *unit=request->units[i].unit;
        if (!unit->inuse || unit->spawn_time!=request->units[i].spawn || G_IsDeferredFree(unit)) continue;
        typeof(move_group_admission) previous=move_group_admission;
        move_group_admission=(typeof(move_group_admission)){unit,*request->point};
        bool accepted=unit_issueorder(unit,request->order,request->point);
        move_group_admission=previous;
        if (!accepted) continue;
        any=true;
        if (unit->current_order_id!=request->order_id || unit->currentmove!=&move_move_walk) continue;
        moveGroupMember_t *member=group->members+group->count++;
        /* The prepared captain packet carries zero approach range. Its Move
         * activation installs the ordinary .49 threshold, even with a target;
         * this differs from individually reissued captain followers. */
        *member=(moveGroupMember_t){.unit=unit,.spawn=unit->spawn_time};
        unit->movement.group_id=group->id;
        unit->movement.previous_request_id=group->id;
        if (unit->collision>group->radius) group->radius=unit->collision;
    }
    if (group->count) move_group_seed_route(group);
    group->ticking=false;
    if (!group->count) move_release_group(group);
    return any;
}

static bool move_group_point_order(groupPointOrder_t const *request,uint64_t shared_id) {
    return move_group_captain_order(request,shared_id,NULL);
}

/* Original16c6d0 chooses the closest predicted member, with strict ties.
 * TODO GROUP-04.6: preserve the path88.200000 preference and group200 bypass. */
static edict_t *move_group_source(moveGroup_t const *group, wc3GridPose_t *selected) {
    box2_t bounds=CM_GetWorldBounds();
    float goal[2]={wc3_grid_coordinate(group->goal.x,bounds.min.x,32),wc3_grid_coordinate(group->goal.y,bounds.min.y,32)};
    float best=FLT_MAX; edict_t *source=NULL;
    FOR_LOOP(i,group->count) {
        edict_t *unit=group->members[i].unit; wc3GridPose_t pose; unit_predicted_pose(unit,&pose);
        float dx=wc3_sub(goal[0],pose.grid[0]),dy=wc3_sub(goal[1],pose.grid[1]);
        float distance=wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy));
        if (distance<best) { best=distance; source=unit; *selected=pose; }
    }
    return source;
}

/* Original16de50 seeds the formation origin when the cohort is created,
 * before the next owner pass can predict a moving member at a later clock. */
static void move_group_seed_route(moveGroup_t *group) {
    wc3GridPose_t pose;
    edict_t *source=move_group_source(group,&pose);
    if (!source) gi.error("Move: physical group has no route source");
    group->point=(vec2_t){pose.grid[0],pose.grid[1]};
}

static void move_start_point_group(edict_t *actor,vec2_t const *home,float range) {
    moveGroup_t *group=move_alloc_group();
    group->inuse=group->ticking=true; group->id=move_allocate_group_id();
    group->goal=*home; group->age=UINT32_MAX; group->radius=actor->collision;
    group->members[group->count++]=(moveGroupMember_t){.unit=actor,.spawn=actor->spawn_time,.arrival_range=wc3_div(range,32)};
    actor->movement.group_id=group->id;
    move_group_seed_route(group); group->ticking=false;
}

static void move_captain_actor_point(edict_t *actor,vec2_t const *home,float range) {
    move_leave(actor);
    S_IssueMoveOrder(actor,Waypoint_add(home),G_OrderId("move"));
    move_start_point_group(actor,home,range);
}

/* Internal reissue preserves the member's range-listener state. It is not
 * temporary enrollment and must not reset entered/outer flags or deadlines. */
void S_ReissueCaptainUnit(edict_t *unit,edict_t *actor) {
    typeof(unit->movement.captain_home) retained=unit->movement.captain_home;
    unit->movement.captain_home.actor=unit->movement.captain_home.roster_actor=NULL;
    S_TrackMoveTimers(unit);
    S_IssueMoveOrder(unit,Waypoint_add(&retained.home),G_OrderId("move"));
    unit->movement.captain_home=retained;
    unit->movement.captain_home.actor=actor;
    unit->movement.captain_home.active=true;
    S_TrackMoveTimers(unit);
    move_start_follow_group(unit,actor,true);
}

/*9d44d0 retains the point/range before publishing the virtual mover request.
 * Physical roster publication remains in encounter order, in twelve-row batches. */
void S_CaptainPointMove(botCaptain_t *captain,vec2_t const *point,float range) {
    edict_t *actor=captain->home_actor;
    if (!actor) return;
    uint32_t members=actor->movement.captain_actor_members;
    edict_t **roster=move_captain_collect_roster(actor);
    captain->goal=*point;captain->request_range=range;
    uint32_t entered=0;
    FOR_EACH_ARRAY(edict_t *,member,captain->units) {
        edict_t *ent=*member;
        if (!ent->inuse || ent->movement.captain_home.roster_actor!=actor) continue;
        uint32_t index=ent->movement.captain_home.member_index;
        if (index>=members || roster[index]!=ent) gi.error("Move: invalid captain point roster %u/%u",index,members);
        if (ent->movement.captain_home.entered) entered++;
        ent->movement.captain_home.home=*point;
    }
    actor->unitinfo.MoveSpeed=move_captain_point_speed(captain);
    actor->unitinfo.TurnSpeed=wc3_float(0x3ecccccd);
    actor->unitinfo.PropWindow=wc3_float(0x3dcccccd);
    actor->unitinfo.move_flags|=BZ_UNIT_SPEED_SET|BZ_UNIT_TURN_SET|BZ_UNIT_WINDOW_SET;
    move_captain_actor_point(actor,point,range);
    if (!members) {gi.MemFree(roster);return;}
    if (entered==members) move_captain_shared_point(actor,roster,members,point,true);
    else FOR_LOOP(i,members) S_ReissueCaptainUnit(roster[i],actor);
    gi.MemFree(roster);
}

/*9d2670 near-home inverse uses the roster range count, not the state enum.
 * Empty captains and state1 place the actor before requesting their home. */
void S_CaptainGoHome(botCaptain_t *captain) {
    edict_t *actor=captain->home_actor;
    if (!actor || !captain->home_set) return;
    if (S_CaptainNearHome(captain)) {
        uint32_t entered=move_captain_entered(actor);
        if (entered>=ARRAY_COUNT(captain->units)/2) captain->policy_flags&=~BOT_CAPTAIN_RETREAT_FLAG;
        return;
    }
    if (!ARRAY_COUNT(captain->units) || captain->state==BOT_CAPTAIN_FORMING)
        S_SetUnitPosition(actor,&captain->home);
    S_CaptainPointMove(captain,&captain->home,500);
}

/* Native171070 installs the profile;16cb80 consumes its low rank nibble.
 * Rebinding object-data pointers alone must not change a live mover's row. */
void S_SetMoveFormationRank(edict_t *unit, uint32_t rank) {
    unit->movement.formation_rank=rank&15u;
}

/* Original16b2f0 projects predicted positions, not formation offsets.
 * Cohorts have at most twelve rows: fixed scratch storage and the native
 * strict comparisons and minimum-to-tail swaps also retain the native tie behavior. */
static void move_group_classify(moveGroup_t *group) {
    /* Equal ranks cannot exceed the running rank minimum or encounter a
     * lower-ranked peer. Skip pure scalar projection/sorting for this common
     * case, independently of positions, radii and arrived-member flags. */
    uint32_t rank=group->members[0].unit->movement.formation_rank;
    bool mixed=false;
    for(uint32_t i=1;i<group->count;i++) {
        if(group->members[i].unit->movement.formation_rank!=rank) {mixed=true;break;}
    }
    if(!mixed)return;
    struct { float lower, upper; uint32_t member; } intervals[BZ_WC3_GROUP_ORDER_UNITS];
    float sine,cosine;
    wc3_sincos(wc3_float(wc3_float_bits(group->heading)^0x80000000u),&sine,&cosine);
    FOR_LOOP(i,group->count) {
        edict_t *unit=group->members[i].unit;wc3GridPose_t pose;
        unit_predicted_pose(unit,&pose);
        float position=wc3_sub(wc3_mul(pose.grid[0],cosine),wc3_mul(pose.grid[1],sine));
        float radius=wc3_add(wc3_div(unit->collision,32),1);
        intervals[i]=(typeof(intervals[0])){wc3_sub(position,radius),wc3_add(position,radius),i};
    }
    for(uint32_t tail=group->count;tail>1;) {
        tail--;uint32_t minimum=tail;
        for(uint32_t i=tail;i>0;) {
            i--;if(intervals[i].lower<intervals[minimum].lower)minimum=i;
        }
        if(minimum!=tail) {
            typeof(intervals[0]) swap=intervals[tail];intervals[tail]=intervals[minimum];intervals[minimum]=swap;
        }
    }
    uint32_t rank_limit=16;
    for(uint32_t i=group->count;i>0;) {
        i--;moveGroupMember_t *member=group->members+intervals[i].member;
        if(member->flags&0x10000)continue;
        uint32_t rank=member->unit->movement.formation_rank;
        if(rank>rank_limit) {member->flags|=0x200000;continue;}
        rank_limit=rank;
        for(uint32_t j=i;j>0 && !(member->flags&0x200000);) {
            j--;if(!(intervals[j].lower<intervals[i].upper))break;
            moveGroupMember_t const *other=group->members+intervals[j].member;
            if(!(other->flags&0x10000) && other->unit->movement.formation_rank<rank)
                member->flags|=0x200000;
        }
    }
}

static bool move_group_route(moveGroup_t *group) {
    wc3GridPose_t pose;
    edict_t *source=move_group_source(group,&pose); if (!source) return false;
    /* Original16c940 scans the live resolved members when routing samples a
     * local group. Membership pruning has already removed departed owners. */
    group->radius=0;
    FOR_LOOP(i,group->count) if (group->members[i].unit->collision>group->radius)
        group->radius=group->members[i].unit->collision;
    moveShared_t const *shared=move_group_shared(group);
    if (shared) group->radius=shared->radius;
    vec2_t from={pose.world[0],pose.world[1]},fine={pose.grid[0],pose.grid[1]},point;
    movePathQuery_t query={.geometry={&from,&group->goal,group->radius,M_UnitStaticPathingFlags(source)},
        .mover=source,.target=group->target,.units=true,.fine=&fine,.coarse_mask=S_UnitMoveCoarseMask(source),
        .no_warp=(group->flags&0x10u)!=0};
    uint32_t revision=group->route.group_revision;
    if (!G_UnitMoveGroupDestination(&query,&group->route,&point)) {
        if(!group->route.group_admission.waiting)
            fprintf(stderr,"Move group %u: route unavailable at (%.9g,%.9g) to (%.9g,%.9g)\n",group->id,from.x,from.y,group->goal.x,group->goal.y);
        return false;
    }
    if (group->initialized && point.x==group->point.x && point.y==group->point.y && revision==group->route.group_revision) return true;
    float dx=wc3_sub(point.x,group->point.x),dy=wc3_sub(point.y,group->point.y);
    if (dx!=0 || dy!=0) group->heading=wc3_vector_heading(dx,dy);
    group->point=point; group->initialized=true; group->flags|=0x30000;
    wc3FormationMember_t members[BZ_WC3_GROUP_ORDER_UNITS];
    FOR_LOOP(i,group->count) {
        edict_t *unit=group->members[i].unit; unit_predicted_pose(unit,&pose);
        members[i]=(wc3FormationMember_t){.position={pose.grid[0],pose.grid[1]},
            .radius=wc3_div(unit->collision,32),.rank=unit->movement.captain_actor_type ? 0 : unit->movement.formation_rank};
    }
    wc3Formation_t formation={members,group->count,group->heading};
    if (!wc3_formation_layout(&formation)) gi.error("Move: invalid %u-member formation",group->count);
    FOR_LOOP(i,group->count) group->members[i].offset=
        (vec2_t){members[i].offset[0],members[i].offset[1]};
    return true;
}

/* Native167070 advances a nonfinal coarse leg without consuming retry.
 * Final partial endpoints retain165c60/167290's stopped retry transition. */
static uint32_t move_group_advance_endpoint(moveGroup_t *group, moveGroupMember_t *member,
                                            wc3GridPose_t const *pose, vec2_t *direction) {
    edict_t *unit=member->unit; moveFineRoute_t *route=&unit->movement.fine_route;
    if (route->partial && route->count && !route->index && route->adaptive_index &&
        route->adaptive_index<route->adaptive_count) {
        float x=wc3_sub(route->points[0].x,pose->grid[0]),y=wc3_sub(route->points[0].y,pose->grid[1]);
        float range=wc3_float(0x3efae148);
        if (wc3_add(wc3_mul(x,x),wc3_mul(y,y))<=wc3_mul(range,range)) {
            *direction=(vec2_t){x,y};
            bool warped=false;
            if (!G_AdvanceUnitMoveAdaptiveDestination(unit,route,&warped))
                unit->movement.wait_delay=MAX(unit->movement.wait_delay,20u);
            unit->movement.path.valid=false;
            return 2;
        }
    }
    uint32_t result=move_retry_endpoint(unit,pose,member->destination,group->count);
    if (result==4) {member->forced_arrival=true;member->flags|=0x20000;}
    return result;
}

/* Native16a790 adjusts a refreshed slot after the cohort classification.
 * Keep the previous destination/flags until this member's decision: the next
 * owner visit observes adjusted40000 and seeds the classification cooldown. */
static void move_group_adjust_destination(moveGroup_t *group, moveGroupMember_t *member) {
    vec2_t previous=member->destination;
    member->destination=(vec2_t){wc3_add(group->point.x,member->offset.x),wc3_add(group->point.y,member->offset.y)};
    member->flags&=~0x70000u;
    if ((member->offset.x!=0 || member->offset.y!=0) &&
        G_AdjustUnitMoveFormationDestination(member->unit,group->point,&member->destination)) member->flags|=0x40000;
    box2_t bounds=CM_GetWorldBounds();
    member->world_destination=(vec2_t){wc3_add(bounds.min.x,wc3_mul(member->destination.x,32)),wc3_add(bounds.min.y,wc3_mul(member->destination.y,32))};
    /* Accept destination changes before advancing a retained route wait. */
    if (((int32_t)floorf(previous.x)>>1)!=((int32_t)floorf(member->destination.x)>>1) ||
        ((int32_t)floorf(previous.y)>>1)!=((int32_t)floorf(member->destination.y)>>1)) {
        edict_t *unit=member->unit;moveFineRoute_t *route=&unit->movement.fine_route;
        route->count=route->adaptive_count=0;
        route->index=route->adaptive_index=UINT32_MAX;route->partial=false;
        unit->movement.retry_count=unit->movement.wait_delay=0;
    }
    member->unit->movement.path.valid=false;
}

static void move_group_decide(moveGroup_t *group, moveGroupMember_t *member) {
    if (group->flags&0x10000) move_group_adjust_destination(group,member);
    moveRouteResult_t route_result=MOVE_ROUTE_FAILED;
    edict_t *unit=member->unit;
    wc3GridPose_t pose; unit_predicted_pose(unit,&pose);
    wc3Arrival_t arrival={.source={pose.grid[0],pose.grid[1]},.target={member->destination.x,member->destination.y},
        .heading=unit->s.angle,.range=member->arrival_range ? member->arrival_range : wc3_float(0x3efae148),
        .flags=member->forced_arrival ? 0x10000 : 0};
    /* Original16a790 replaces arrival10000 from this visit's result. A cached
     * slot may cease to be reached after SetUnitX/Y or physical displacement. */
    member->flags&=~0x10000u;
    if (unit->paused || unit->stunned) {
        member->arrived=member->in_range=false; member->speed=0; member->heading=unit->s.angle;
        return;
    }
    member->arrived=wc3_arrival_update(&arrival); member->in_range=arrival.in_range;
    float old_angle=unit->s.angle;
    if (member->arrived) {
        member->flags|=0x10000; member->forced_arrival=false;
        member->speed=0; member->heading=old_angle;
        /* Held16fd90 retires pending work even on its early arrival branch. */
        if (member->flags&0x200000) move_unlink_requests(unit);
        return;
    }
    if (member->flags&0x200000) {
        /* Original16fd90 turns a held member with stop1 and cancels its
         * pending scheduler records; it does not run a fresh local route. */
        float x=wc3_sub(member->destination.x,pose.grid[0]),y=wc3_sub(member->destination.y,pose.grid[1]);
        unit_turn_toward(unit,wc3_vector_heading(x,y));
        member->speed=0;member->heading=unit->s.angle;unit->s.angle=old_angle;
        move_unlink_requests(unit);
        return;
    }
    if (wc3_yield_advance(&unit->movement.wait_delay,false)) {
        move_hold_goal_heading(unit);
        member->speed=0; member->heading=unit->s.angle; unit->s.angle=old_angle;
        return;
    }
    float x=wc3_sub(member->destination.x,pose.grid[0]),y=wc3_sub(member->destination.y,pose.grid[1]);
    vec2_t direction;
    /* Native arrival can stop translation before its angular gate completes.
     * In-range/forced members turn without another path or retry request. */
    uint32_t progress=arrival.in_range ? 0 : move_group_advance_endpoint(group,member,&pose,&direction);
    if (progress==2) {
        /* Native returns the consumed fine point, including a zero vector.
         * 16fbd0 passes stop1 for every nonzero Path_Advance status. */
        unit->movement.heading=wc3_vector_heading(direction.x,direction.y);
        unit_turn_toward(unit,unit->movement.heading);
        unit->movement.turn_blocked=true;
    } else if (arrival.in_range || progress) {
        unit_turn_toward(unit,wc3_vector_heading(x,y)); unit->movement.turn_blocked=true;
        if (arrival.in_range) move_unlink_requests(unit);
    } else {
        if ((route_result=unit_accel_direction(unit,(moveRoutePoint_t){&group->goal,unit->collision,MOVE_AVOID_GENERIC},&direction))) {
            progress=move_group_advance_endpoint(group,member,&pose,&direction);
            if (progress==2) {
                unit->movement.heading=wc3_vector_heading(direction.x,direction.y);
                unit_turn_toward(unit,unit->movement.heading);
                unit->movement.turn_blocked=true;
            } else if (progress) move_hold_goal_heading(unit);
            else unit_apply_route_heading(unit, &direction, MOVE_AVOID_GENERIC, route_result);
        }
        else { unit_turn_toward(unit,wc3_vector_heading(x,y)); unit->movement.turn_blocked=true; }
    }
    member->speed=unit->movement.turn_blocked ? 0 : unit_effective_speed(unit);
    member->heading=unit->s.angle; unit->s.angle=old_angle;
    if (member->speed>0) member->flags|=0x100000;
}

/* Original16b120/16c4f0 regroup ordinary members before advancing the shared
 * coarse point. Arrived members retain their zero velocity during this wait. */
static void move_group_regroup(moveGroup_t *group) {
    uint32_t arrived=0,near=0;
    bool exempt=false;
    float range=group->flags&0x100 ? 16 : 256;
    FOR_LOOP(i,group->count) {
        moveGroupMember_t const *member=group->members+i;
        exempt|=member->unit->attack_speed_cap.active;
        if (member->flags&0x10000) { arrived++; continue; }
        wc3GridPose_t pose; unit_predicted_pose(member->unit,&pose);
        float x=wc3_sub(member->destination.x,pose.grid[0]),y=wc3_sub(member->destination.y,pose.grid[1]);
        if (wc3_add(wc3_mul(x,x),wc3_mul(y,y))<range) near++;
    }
    uint32_t status=arrived ? group->count-arrived-near : group->count;
    if (arrived && (group->cooldown || exempt || (group->flags&4))) status=0;
    uint32_t limit=group->flags&0x100 ? (group->flags&0x20000 ? 396 : 198) : 99;
    if (!status || group->completion_counter>limit) {
        if (G_AdvanceUnitMoveGroupDestination(&group->route)) {
            FOR_LOOP(i,group->count) {
                moveGroupMember_t *member=group->members+i; edict_t *unit=member->unit;
                member->flags=0; member->forced_arrival=false;
                moveFineRoute_t *route=&unit->movement.fine_route;
                route->count=route->adaptive_count=0; route->index=route->adaptive_index=UINT32_MAX;
                route->partial=false; unit->movement.path.valid=false;
                unit->movement.wait_delay=unit->movement.retry_count=0; unit->movement.wait_blocker=NULL;
            }
            group->age=group->completion_counter=0;
            move_group_route(group); group->flags&=~0x20000u;
        }
    } else if (arrived) group->completion_counter++;
}

static int move_compare_group_visits(void const *a, void const *b) {
    uint64_t x=((moveGroupVisit_t const *)a)->sequence,y=((moveGroupVisit_t const *)b)->sequence;
    return x<y ? 1 : x>y ? -1 : 0;
}

static void move_run_group_updates(void) {
    move_update_shared();
    move_prepare_group_order();
    /* Freeze physical generations before callbacks can allocate or reuse slots.
     * Native visits newest cohorts first; newly created owners wait one pass. */
    uint32_t count=ARRAY_COUNT(level.move_groups),visits=0;
    if (!count) return;
    moveGroupVisit_t *owners=malloc(count*sizeof(*owners));
    if (!owners) gi.error("Move: cannot allocate physical owner visits");
    for(moveGroup_t *group=move_group_head;group;group=group->older) if(group->inuse) {
        owners[visits++]=(moveGroupVisit_t){group,group->sequence};
    }
    FOR_LOOP(g,visits) {
        if (!(g & 7u)) gi.FrameCheckpoint();
        moveGroup_t *group=owners[g].group;
        if (!group->inuse || group->sequence!=owners[g].sequence) continue;
        group->ticking=true;
        for (uint32_t i=0;i<group->count;) {
            moveGroupMember_t const *member=group->members+i; edict_t *unit=member->unit;
            if (!unit->inuse || unit->spawn_time!=member->spawn || G_IsDeferredFree(unit) || (!unit->movement.captain_actor_type && M_IsDead(unit)) ||
                unit->movement.group_id!=group->id ||
                (unit->currentmove!=&move_move_walk && (!group->target || unit->currentmove!=&follow_move_walk)) ||
                !unit->goalentity) {
                if (unit->inuse && unit->spawn_time==member->spawn && unit->movement.group_id==group->id)
                    unit->movement.group_id=0;
                group->members[i]=group->members[--group->count]; continue;
            }
            if (!S_UnitCanTranslate(unit)) { unit->stand(unit); continue; }
            i++;
        }
        if (!group->count) { move_release_group(group); continue; }
        if (group->individual) {
            edict_t *unit=group->members[0].unit;
            if (!unit->paused && !unit->stunned) ai_move_walk(unit);
            group->ticking=false;
            continue;
        }
        if (group->target && (!group->target->inuse || group->target->spawn_time!=group->target_spawn ||
                G_IsDeferredFree(group->target) || (!group->target->movement.captain_actor_type && M_IsDead(group->target)))) {
            while(group->count) {
                edict_t *unit=group->members[group->count-1].unit;
                unit->movement.follow_target=NULL; S_SetMoveGoal(unit, &unit->goalentity, NULL); unit_stand(unit);
            }
            move_release_group(group); continue;
        }
        /* Original16bc10 samples each member's predicted region cell before
         * route/decision work; actions dispatch after all owner commits. */
        for(uint32_t i=group->count;i>0;i--) {
            edict_t *unit=group->members[i-1].unit;
            wc3GridPose_t pose;unit_predicted_pose(unit,&pose);
            vec2_t point={pose.world[0],pose.world[1]};
            G_UnitRegionPositionChanged(unit,&point);
        }
        group->age++;
        vec2_t sampled=group->initialized ? group->route.group_goal :
            (vec2_t){wc3_grid_coordinate(group->goal.x,CM_GetWorldBounds().min.x,32),
                     wc3_grid_coordinate(group->goal.y,CM_GetWorldBounds().min.y,32)};
        bool visible=!group->target || group->target->movement.captain_actor_type ||
            G_FowPlayerCanTrackUnit(group->members[0].unit->s.player,group->target);
        group->unseen_counter=visible ? 0 : group->unseen_counter+1;
        if (group->target && visible && !group->target_refresh) {
            wc3GridPose_t pose; unit_predicted_pose(group->target,&pose);
            sampled=(vec2_t){pose.grid[0],pose.grid[1]};
            box2_t bounds=CM_GetWorldBounds();
            vec2_t old=group->initialized ? group->route.group_goal :
                (vec2_t){wc3_grid_coordinate(group->goal.x,bounds.min.x,32),wc3_grid_coordinate(group->goal.y,bounds.min.y,32)};
            /* Original167e40 compares floor(fine)>>1 before replacing the
             * cached destination. Sub-cell target motion must not reset a
             * retained route, even when the refresh countdown reaches zero.
             * TODO TARGET-02.1: delayed destination changes also require both
             * native path timestamps to be at least ten owner ticks old;
             * their producers remain unported (scene53 retains zeroes). */
            if (((int32_t)floorf(old.x)>>1)!=((int32_t)floorf(pose.grid[0])>>1) ||
                    ((int32_t)floorf(old.y)>>1)!=((int32_t)floorf(pose.grid[1])>>1))
                group->goal=(vec2_t){pose.world[0],pose.world[1]};
            group->target_refresh=-1;
        }
        if (!move_group_route(group)) { group->ticking=false; continue; }
#ifdef BZ_TESTS
        if (move_test_group_route) move_test_group_route(group,NULL);
#endif
        FOR_LOOP(i,group->count) group->members[i].flags&=~0x200000u;
        if (group->count>1 && !(group->flags&0x200)) {
            if (!group->cooldown && !(group->flags&2)) {
                /* Original169b00 denies projected classification for adjusted,
                 * forced or special members and partial member paths. The
                 * denial owns a66-tick regroup cooldown. Attack owns the
                 * independent mover01000000 exemption lifetime. */
                FOR_LOOP(i,group->count) if ((group->members[i].flags&0xe0000) ||
                    group->members[i].unit->attack_speed_cap.active ||
                    group->members[i].unit->movement.fine_route.partial) {group->cooldown=66;break;}
                if (!group->cooldown) move_group_classify(group);
            }
            /* Original16c630 keeps peers eligible after65 ticks, even when
             * their current requested speed is zero. */
            if (!(group->flags&0x20000) || group->age>65)
                FOR_LOOP(i,group->count) group->members[i].flags|=0x100000;
        }
        move_deciding_group=group; move_deciding_excluded=0;
        if (group->count>1 && !(group->flags&0x200)) FOR_LOOP(i,group->count)
            if (!(group->members[i].flags&0x300000)) move_deciding_excluded|=1u<<i;
        FOR_LOOP(i,group->count) move_group_decide(group,group->members+i);
        move_deciding_group=NULL; move_deciding_excluded=0;
        float cap=FLT_MAX; bool share=!(group->flags&8);
        FOR_LOOP(i,group->count) {
            moveGroupMember_t const *member=group->members+i;
            if ((member->flags&0x210000) || member->unit->attack_speed_cap.active) share=false;
            float speed=unit_effective_speed(member->unit);
            if (!(member->flags&0x200000) && speed<cap) cap=speed;
        }
        moveShared_t *shared=move_group_shared(group);
        if (shared && share) {
            shared->next_speed=MIN(shared->next_speed,cap);
            if (shared->speed!=FLT_MAX) cap=shared->speed;
        }
        edict_t *finished[BZ_WC3_GROUP_ORDER_UNITS]; uint32_t count=0;
        FOR_LOOP(i,group->count) {
            moveGroupMember_t *member=group->members+i; edict_t *unit=member->unit;
            if (unit->paused || unit->stunned) continue;
            cstring_t animation=member->speed>0 ? "walk" : "stand";
            if (!G_AnimationHasPrimary(unit->animation,animation)) unit_setanimation(unit,animation);
            moveStep_t step={.velocity={.vel={unit->movement.velocity.x,unit->movement.velocity.y},
                .speed=share ? MIN(member->speed,cap) : member->speed,.heading=member->heading,.limit=unit_effective_speed(unit)}};
            unit_grid_pose(unit,&step.pose);
            float old[2]={unit->movement.velocity.x,unit->movement.velocity.y};
            wc3_grid_step(&step.pose,old,unit->movement.clock_valid ? wc3_elapsed(&level.pathing_clock,&unit->movement.pose_clock) : 0);
            if ((group->flags&0x800) && group->target && !group->unseen_counter) {
                wc3GroupSpeed_t speed={.requested=wc3_mul(member->speed,1.0f/32),
                    .cap=share ? wc3_mul(cap,1.0f/32) : FLT_MAX,
                    .flags=group->flags,.target=true,
                    .source={step.pose.grid[0],step.pose.grid[1]},
                    .destination={sampled.x,sampled.y},.arrival_range=member->arrival_range,
                    .target_maximum=wc3_mul(unit_effective_speed(group->target),1.0f/32),
                    .target_velocity={wc3_mul(group->target->movement.velocity.x,1.0f/32),
                                      wc3_mul(group->target->movement.velocity.y,1.0f/32)}};
                step.velocity.speed=wc3_mul(wc3_group_commit_speed(&speed),32);
            }
            wc3_velocity_update_world(&step.velocity); unit_commit_motion(unit,&step);
            if (member->arrived && !group->route.group_index && !(group->flags&1)) finished[count++]=unit;
        }
        group->flags&=~0x10000u;
        if (group->target) {
            if (group->target_refresh==-1) {
                wc3GridPose_t pose; unit_predicted_pose(group->members[0].unit,&pose);
                vec2_t goal=group->route.group_goal;
                float x=wc3_sub(goal.x,pose.grid[0]),y=wc3_sub(goal.y,pose.grid[1]);
                float distance=wc3_sqrt(wc3_add(wc3_mul(x,x),wc3_mul(y,y)));
                int32_t reload=(int32_t)wc3_int_bits(wc3_float_bits(wc3_add(wc3_mul(distance,wc3_float(0x3ea8f5c3)),.5f)));
                group->target_refresh=reload<16 ? 16 : reload>132 ? 132 : reload;
                if (group->flags&0x400) group->target_refresh+=165;
            } else if (group->target_refresh) group->target_refresh--;
        }
        if (!group->route.group_index) {
            if (!(group->flags&1)) group->completion_counter++;
        } else move_group_regroup(group);
        if (count) group->completion_counter=0;
        while (count) {
            edict_t *unit=finished[--count];
            if (unit->movement.group_id==group->id) {
                edict_t *target=group->target;
                move_detach_group(unit); unit->movement.group_id=0;
                if (target) move_start_follow_group(unit,target,true);
                else {
                    edict_t *actor=unit->movement.captain_home.roster_actor;
                    /* Native9d8a90 reissues an idle roster member while
                     * the captain is outside its retained request range.
                     * The all-entered200 point range does not replace the
                     * retained GoHome500 range used by9cff90. */
                    bool follow=actor && (actor->unitinfo.move_flags&BZ_UNIT_SPEED_SET) &&
                        !move_captain_near_home(actor,&unit->movement.captain_home.home);
                    if (follow) {
                        typeof(unit->movement.captain_home) retained=unit->movement.captain_home;
                        unit->movement.captain_home.actor=NULL;
                        S_TrackMoveTimers(unit);
                        move_leave(unit); S_RecoverStoppedUnitPosition(unit);
                        S_IssueMoveOrder(unit,unit->goalentity,G_OrderId("move"));
                        unit->movement.captain_home=retained;
                        S_TrackMoveTimers(unit);
                        unit->movement.captain_home.actor=actor;
                        S_TrackMoveTimers(unit);
                        unit->movement.captain_home.active=true;
                        move_start_follow_group(unit,actor,true);
                    } else {
                        unit->stand(unit);
                        if (unit->movement.captain_actor_owned) G_BotCaptainGoalEvent(unit);
                    }
                }
            }
        }
        if (group->cooldown) group->cooldown--;
        group->ticking=false;
        if (!group->count) move_release_group(group);
    }
    free(owners);
}

/* Handle a right-click move command from the client.
 * Creates a shared waypoint at the clicked map position, issues move orders
 * to all currently selected units, and sends a move-confirmation effect back
 * to the commanding client (svc_temp_entity / TE_MOVE_CONFIRMATION). */
bool move_selectlocation(edict_t *clent, vec2_t const *location) {
    edict_t *units[MAX_SELECTED_ENTITIES];
    moveSlot_t reserved[MAX_SELECTED_ENTITIES];
    vec2_t center;
    vec2_t confirmation = *location;
    bool have_confirmation = false;
    bool issued = false;
    uint32_t num_units = move_collect_selected(clent->client, units, MAX_SELECTED_ENTITIES, &center);
    float spacing = move_slot_spacing(units, num_units);
    edict_t *route_waypoint;

    if (num_units == 0) {
        return false;
    }
    /* Native NetUnit.cpp prepares shared requests before ordinary ground point admission.
     * Reuse the verified cohort owner instead of assigning an ID to independent walkers.
     * TODO GROUP-04.6: air/mixed-lane and larger selection producers remain open. */
    if (num_units>1 && num_units<=BZ_WC3_GROUP_ORDER_UNITS) {
        bool ground=true; uint8_t mask=M_UnitStaticPathingFlags(units[0]);
        FOR_LOOP(i,num_units) if ((units[i]->aiflags&AI_FLYING) || M_UnitStaticPathingFlags(units[i])!=mask) ground=false;
        bool queued=clent->client->menu.order_queued;
        bool idle=true;
        FOR_LOOP(i,num_units) {
            if (G_UnitHasActiveOrder(units[i]) || units[i]->order_queue.count) idle=false;
        }
        if (ground) {
            groupPointOrder_t request={.count=num_units,.order_id=G_OrderId("move"),.order="move",.point=location,.queued=queued && !idle,.formation_toggle=clent->client->menu.order_alt,.issuer_player=clent->client->ps.number};
            FOR_LOOP(i,num_units) request.units[i]=(typeof(request.units[0])){units[i],units[i]->spawn_time};
            bool accepted=G_IssueGroupPointOrder(&request);
            if (accepted) G_SendPointConfirmation(clent,location,false);
            return accepted;
        }
    }
    wc3FormationMember_t members[WC3_FORMATION_MEMBERS];
    bool const retail_layout = num_units <= WC3_FORMATION_MEMBERS;
    if (retail_layout) {
        box2_t const bounds = CM_GetWorldBounds();
        float mean[2] = {0};
        FOR_LOOP(i, num_units) {
            members[i].position[0] = wc3_grid_coordinate(units[i]->s.origin2.x, bounds.min.x, 32);
            members[i].position[1] = wc3_grid_coordinate(units[i]->s.origin2.y, bounds.min.y, 32);
            members[i].radius = wc3_div(units[i]->collision, 32);
            members[i].rank = units[i]->movement.formation_rank;
            FOR_LOOP(k, 2) mean[k] = wc3_add(mean[k], members[i].position[k]);
        }
        float const reciprocal = wc3_recip(wc3_float(wc3_from_int(num_units)));
        float const dx = wc3_sub(wc3_grid_coordinate(location->x, bounds.min.x, 32), wc3_mul(mean[0], reciprocal));
        float const dy = wc3_sub(wc3_grid_coordinate(location->y, bounds.min.y, 32), wc3_mul(mean[1], reciprocal));
        wc3Formation_t formation = { members, num_units, dx == 0 && dy == 0 ? 0 : wc3_atan2(dy, dx) };
        wc3_formation_layout(&formation);
        /* TODO: FORM-03 owns the original clock prediction and refresh-to-motion
         * chain. Initial engine orders use current committed fine positions. */
    } else {
        /* TODO: FORM-02.3 must establish the original twelve-slot bucket caller
         * precondition before extending it to our larger engine selections. */
        fprintf(stderr, "WC3 movement: %u-member formation exceeds verified retail domain; retaining source offsets\n",
                num_units);
    }
    /* A multi-unit move travels at the slowest member's speed so the group
     * stays together (WC3).  A lone unit keeps its own speed (cap 0). */
    float const group_speed = num_units > 1 ? move_group_speed(units, num_units) : 0;
    uint32_t group_id = num_units > 1 && !clent->client->menu.order_queued ? move_allocate_group_id() : 0;
    route_waypoint = clent->client->menu.order_queued ? NULL : Waypoint_add(location);

    FOR_LOOP(i, num_units) {
        edict_t *ent = units[i];
        vec2_t preferred;
        if (retail_layout) {
            preferred.x = wc3_add(location->x, wc3_mul(members[i].offset[0], 32));
            preferred.y = wc3_add(location->y, wc3_mul(members[i].offset[1], 32));
        } else {
            preferred = move_preferred_slot(ent, &center, location, spacing, num_units);
        }
        vec2_t target;

        if (!move_find_reserved_slot(location,
                                     &preferred,
                                     ent->collision,
                                     M_UnitStaticPathingFlags(ent),
                                     spacing,
                                     num_units,
                                     reserved,
                                     i,
                                     &target)) {
            target = *location;
            pathAccelParams_t query = { location, NULL, ent->collision, M_UnitStaticPathingFlags(ent) };
            G_ClosestMovePathPoint(&query, &target);
        }
        reserved[i] = (moveSlot_t){ target, ent->collision };
        if (!have_confirmation) {
            confirmation = target;
            have_confirmation = true;
        }
        if (clent->client->menu.order_queued) {
            /* Queued units may reach this leg at different times, so retain the
             * resolved per-unit slot and speed in the unit's own FIFO. */
            if (G_IssueUnitPointOrder(ent, "move", &target, true,
                                      clent->client->ps.number, group_speed)) {
                issued = true;
            }
        } else {
            edict_t *waypoint = Waypoint_add(&target);
            S_SetMoveGoal(waypoint, &waypoint->secondarygoal, route_waypoint);
            G_ClearUnitOrderQueue(ent);
            ent->movement.holding_position = false;
            S_IssueMoveOrder(ent, waypoint, G_OrderId("move"));
            if (ent->goalentity == waypoint && ent->currentmove == &move_move_walk) {
                ent->movement.group_id = group_id;
                ent->movement.group_speed = group_speed;
            }
            S_UnitAbilityOrderAccepted(ent, "move");
            issued = true;
        }
    }
    if (issued) G_SendPointConfirmation(clent, &confirmation, false);
    return issued;
}

/* Follow loses its user head immediately even though RemoveUnit defers edict reclamation. */
BZ_ABILITY_PROC(CAbilityMove) {
    switch (msg) {
    case A_UNIT_OWNED:
        return ent && (ent->data.UnitBalance || ent->movement.captain_actor_type) && !M_UnitMoveDisabled(ent);
    case A_UNIT_TYPE_INIT: return ent ? UNIT_INIT_UNKNOWN : UNIT_INIT_RUN_LOCAL;
    case A_UNIT_EVENT_MASK:
        return UNIT_MESSAGE_SUBSCRIPTIONS(A_MOVE_PARAMETERS_CHANGED, A_DEATH, A_QUEUE_ORDER_START,
            A_GROUP_POINT_ORDER, A_OWNER_BEGIN, A_OWNER_UPDATE, A_UNIT_TYPE_CHANGING,
            A_PRIMARY_TIMER, A_ORDER_ACCEPTED, A_UNIT_TYPE_CHANGED, A_UNIT_INIT, A_UNIT_OWNER_CHANGING,
            A_UNIT_OWNER_CHANGED, A_UNIT_REMOVE, A_UNIT_REMOVING, A_COMMAND, A_TARGET_REMOVED,
            A_CHANNEL_STATE_CHANGED);
    case A_MOVE_PARAMETERS_CHANGED: {
        wc3Velocity_t velocity = { .vel = {ent->movement.velocity.x, ent->movement.velocity.y},
            .limit = unit_effective_speed(ent) };
        if (wc3_velocity_cap_world(&velocity)) {
            if (ent->movement.clock_valid) unit_commit_current_pose(ent);
            ent->movement.velocity = (vec2_t){velocity.vel[0], velocity.vel[1]};
        }
        return true;
    }
    case A_DEATH:
        /* Native death retires active Follow heads synchronously, before the
         * next physical-owner update or a replacement target can be created. */
        S_UnitTargetRemoved(ent);
        return true;
    case A_QUEUE_ORDER_START:
        return move_start_queued_group(ent,call->queued_order);
    case A_GROUP_POINT_ORDER:
        return move_group_point_order(call->group_order,0) ? ABILITY_ORDER_ACCEPTED : ABILITY_ORDER_REJECTED;
    case A_ORDER_ACCEPTED:
        /* Retail point activation creates a physical singleton too. Route
         * decisions, scheduler admission and retry RNG therefore follow the
         * newest-first owner list rather than entity allocation order. A
         * prepared packet installs its own owner after admission; independent
         * nested point orders must still receive their physical singleton. */
        if (ent->currentmove==&move_move_walk && ent->goalentity &&
            (ent->goalentity->svflags&SVF_MOVE_WAYPOINT) && !move_unit_group(ent) &&
            !(move_group_admission.unit==ent &&
              move_group_admission.point.x==ent->goalentity->s.origin2.x &&
              move_group_admission.point.y==ent->goalentity->s.origin2.y))
        {
            move_start_point_group(ent,&ent->goalentity->s.origin2,0);
            move_unit_group(ent)->individual=!ent->movement.captain_home.roster_actor;
        }
        return true;
    case A_OWNER_BEGIN: move_update_fine_budget(); return true;
    case A_OWNER_UPDATE: move_run_group_updates(); move_repulse_owner_update(); return true;
    case A_CHANNEL_STATE_CHANGED:
        /* 48ef40/48bca0 refresh after publishing the channel-work flag. */
        move_repulse_init(ent); return true;
    case A_UNIT_TYPE_CHANGING:
        if (ent->currentmove==&move_move_walk) {
            unit_commit_current_pose(ent);
            move_reset_progress(ent);
            ent->movement.pose_clock=level.pathing_clock;
        }
        return true;
    case A_TIMERS_RESET:
        if(ent)return 0;
        move_repulse_clear_links();
        move_timer_members=(entitySet_t){0};return true;
    case A_TIMERS_REBUILD:
        if(ent)return 0;
        move_timer_members=(entitySet_t){0};
        FOR_LOOP(i,globals.num_edicts) S_TrackMoveTimers(g_edicts+i);
        return true;
    case A_PRIMARY_TIMER:
        S_RunMoveTimers(); return true;
    case A_UNIT_TYPE_CHANGED:
        /* 670d4d replaces the repulsor after binding the new authored policy. */
        move_repulse_init(ent);
        ent->movement.adaptive_disabled=G_UnitIsStructure(ent) || (ent->aiflags&AI_FLYING);
        G_PublishMoveSpatialObject(ent);
        /* Original670950 retires the physical task and reissues the retained
         * point head after binding the replacement speed/radius. */
        if (ent->currentmove==&move_move_walk && ent->goalentity) {
            /* Scene64 original5fd270/5ffb60 run ten ms after670950, after
             * any due movement owner. Generic timer arithmetic remains NUM-02.9. */
            ent->movement.type_rebind_pending=true;
            S_TrackMoveTimers(ent);
            ent->movement.type_rebind_deadline=level.pathing_clock;
            FOR_LOOP(i,2) wc3_clock_advance(&ent->movement.type_rebind_deadline,wc3_float(0x3ba3d70a),0);
        }
        return true;
    case A_UNIT_INIT:
        /* Native68a060 enables nonstructures before publishing movement type;
         * a newly created flyer retains adaptive routing.670950 rebind differs. */
        ent->movement.adaptive_disabled=G_UnitIsStructure(ent);
        ent->movement.fine_class=ent->s.player;
        move_repulse_init(ent); return true;
    case A_UNIT_OWNER_CHANGING:
        /* Original698d92 cancels ordinary Move while callbacks still see the old player. */
        if (unit_is_walking(ent)) order_stop(ent);
        move_unlink_requests(ent); return true;
    case A_UNIT_OWNER_CHANGED:
        /* Original05c800/168a80 publishes the new player class after cancellation. */
        ent->movement.fine_class=ent->s.player;
        move_repulse_init(ent); return true;
    case A_UNIT_REMOVING:
        /* RemoveUnit cancels its task now; the mover and its route storage
         * remain owned until deferred removal, as in native694690/171340. */
        move_leave(ent);
        move_cancel_displacement(ent);
        S_SetMoveGoal(ent, &ent->goalentity, NULL);
        ent->movement.follow_target=NULL;
        return true;
    case A_UNIT_REMOVE:
        move_remove_captain_roster_member(ent);
        move_release_captain_reference(ent);
        move_unlink_fine_request(ent);
        move_detach_group(ent);
        FILTER_EDICTS(other,other->inuse && other->movement.wait_blocker==ent)
            other->movement.wait_blocker=NULL;
        if (ent->movement.repulse.active) move_repulse_unlink(ent);
        S_FreeMoveRoute(ent);
        return true;
    case A_COMMAND: {
        edict_t *clent = call && call->client ? call->client : ent;
        UI_AddCancelButton(clent);
        clent->client->menu.on_location_selected = move_selectlocation;
        clent->client->menu.supports_order_queue = true;
        return true;
    }
    case A_TARGET_REMOVED:
        if (!call || ent->movement.follow_target != call->removed_target) return false;
        ent->movement.follow_target = NULL;
        ent->movement.follow_target_spawn_time = 0;
        /* Attack can temporarily own Smart's task. Retire its Follow parent
         * without completing the public head or advancing the pending FIFO. */
        if (move_is_following(ent)) {
            move_detach_group(ent); ent->movement.group_id=0;
            if (ent->goalentity == call->removed_target) S_SetMoveGoal(ent, &ent->goalentity, NULL);
            unit_stand(ent);
        }
        return true;
    default:
        return false;
    }
}
