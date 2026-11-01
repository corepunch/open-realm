#include "g_local.h"

/* ATrg counts are separate from event subscriptions. The saved owner lease is
 * authoritative; these two keyed indexes are rebuilt once after loading it. */
typedef struct {
    uint32_t target,birth,code,player;
} reservationKey_t;
typedef struct {
    reservationKey_t key;
    uint32_t count;
    uint8_t state;
} reservationCount_t;
typedef struct {
    reservationKey_t key;
    uint64_t generation;
    uint8_t state;
} reservationToken_t;
static reservationCount_t *reservation_counts;
static reservationToken_t *reservation_tokens;
static uint32_t count_capacity,count_used,count_tombs,token_capacity,token_used,token_tombs;
static uint64_t reservation_generation;

static uint32_t reservation_mix(uint32_t x) {
    x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);
}
static uint32_t reservation_hash(reservationKey_t key) {
    return reservation_mix(key.target^reservation_mix(key.birth)^reservation_mix(key.code)^reservation_mix(key.player));
}
static bool reservation_equal(reservationKey_t a,reservationKey_t b) {
    return a.target==b.target && a.birth==b.birth && a.code==b.code && a.player==b.player;
}
static bool reservation_key(targetReservationParams_t const *params,uint32_t birth,reservationKey_t *key) {
    edict_t const *target=params->target;
    uint32_t code=params->code,player=params->player;
    uintptr_t bytes=(uintptr_t)target-(uintptr_t)g_edicts;
    if(!g_edicts || !target || bytes>=sizeof(*target)*globals.max_edicts || bytes%sizeof(*target) || !code || player>=MAX_PLAYERS)return false;
    *key=(reservationKey_t){bytes/sizeof(*target),birth,code,player};return true;
}
static uint32_t count_find(reservationKey_t key) {
    uint32_t slot=reservation_hash(key)&(count_capacity-1),empty=UINT32_MAX;
    for(uint32_t i=0;i<count_capacity;i++,slot=(slot+1)&(count_capacity-1)) {
        reservationCount_t const *row=reservation_counts+slot;
        if(!row->state)return empty!=UINT32_MAX ? empty : slot;
        if(row->state==2) {if(empty==UINT32_MAX)empty=slot;}
        else if(reservation_equal(row->key,key))return slot;
    }
    assert(empty!=UINT32_MAX);return empty;
}
static uint32_t token_find(uint64_t generation) {
    uint32_t slot=reservation_mix((uint32_t)generation^(uint32_t)(generation>>32))&(token_capacity-1),empty=UINT32_MAX;
    for(uint32_t i=0;i<token_capacity;i++,slot=(slot+1)&(token_capacity-1)) {
        reservationToken_t const *row=reservation_tokens+slot;
        if(!row->state)return empty!=UINT32_MAX ? empty : slot;
        if(row->state==2) {if(empty==UINT32_MAX)empty=slot;}
        else if(row->generation==generation)return slot;
    }
    assert(empty!=UINT32_MAX);return empty;
}
static void count_reserve(void) {
    if(count_capacity && count_used+count_tombs<count_capacity/2)return;
    uint32_t old_capacity=count_capacity;
    reservationCount_t *old=reservation_counts;
    if(!count_capacity)count_capacity=64;
    else if(count_used>=count_capacity/4) {
        if(count_capacity>UINT32_MAX/2) {gi.error("Target reservation count index exhausted");abort();}
        count_capacity*=2;
    }
    reservation_counts=calloc(count_capacity,sizeof(*reservation_counts));
    if(!reservation_counts) {gi.error("Cannot allocate target reservation counts");abort();}
    count_tombs=0;
    for(uint32_t i=0;i<old_capacity;i++)if(old[i].state==1)reservation_counts[count_find(old[i].key)]=old[i];
    free(old);
}
static void token_reserve(void) {
    if(token_capacity && token_used+token_tombs<token_capacity/2)return;
    uint32_t old_capacity=token_capacity;
    reservationToken_t *old=reservation_tokens;
    if(!token_capacity)token_capacity=64;
    else if(token_used>=token_capacity/4) {
        if(token_capacity>UINT32_MAX/2) {gi.error("Target reservation token index exhausted");abort();}
        token_capacity*=2;
    }
    reservation_tokens=calloc(token_capacity,sizeof(*reservation_tokens));
    if(!reservation_tokens) {gi.error("Cannot allocate target reservation tokens");abort();}
    token_tombs=0;
    for(uint32_t i=0;i<old_capacity;i++)if(old[i].state==1)reservation_tokens[token_find(old[i].generation)]=old[i];
    free(old);
}
static bool reservation_insert(reservationKey_t key,uint64_t generation) {
    token_reserve();count_reserve();
    reservationToken_t *token=reservation_tokens+token_find(generation);
    if(token->state==1)return false;
    reservationCount_t *count=reservation_counts+count_find(key);
    if(count->state==1 && count->count==UINT32_MAX)return false;
    if(token->state==2)token_tombs--;
    *token=(reservationToken_t){.key=key,.generation=generation,.state=1};token_used++;
    if(count->state!=1) {
        if(count->state==2)count_tombs--;
        *count=(reservationCount_t){.key=key,.state=1};count_used++;
    }
    count->count++;return true;
}

bool S_AcquireTargetReservation(targetReservationLease_t *lease,targetReservationParams_t const *params) {
    reservationKey_t key;
    if(!lease || lease->held || !params || !params->target || !params->target->inuse ||
       !reservation_key(params,params->target->spawn_time,&key))return false;
    if(reservation_generation==UINT64_MAX) {gi.error("Target reservation generation exhausted");abort();}
    uint64_t generation=++reservation_generation;
    if(!reservation_insert(key,generation))return false;
    *lease=(targetReservationLease_t){.target=params->target,.target_spawn_time=key.birth,
        .code=key.code,.player=key.player,.generation=generation,.held=true};return true;
}
void S_ReleaseTargetReservation(targetReservationLease_t *lease) {
    if(!lease || !lease->held)return;
    targetReservationLease_t saved=*lease;
    *lease=(targetReservationLease_t){0};
    reservationKey_t key;
    if(!token_capacity || !reservation_key(&(targetReservationParams_t){saved.target,saved.code,saved.player},saved.target_spawn_time,&key))return;
    reservationToken_t *token=reservation_tokens+token_find(saved.generation);
    if(token->state!=1 || !reservation_equal(token->key,key))return;
    token->state=2;token_used--;token_tombs++;
    reservationCount_t *count=reservation_counts+count_find(key);
    assert(count->state==1 && count->count);
    if(!--count->count) {count->state=2;count_used--;count_tombs++;}
}
uint32_t S_CountTargetReservations(edict_t const *target,uint32_t code,uint32_t player) {
    reservationKey_t key;
    if(!count_capacity || !target || !target->inuse || !reservation_key(&(targetReservationParams_t){(edict_t *)target,code,player},target->spawn_time,&key))return 0;
    reservationCount_t const *count=reservation_counts+count_find(key);
    return count->state==1 ? count->count : 0;
}
bool S_HasTargetReservation(edict_t const *target,uint32_t runtime_class,uint32_t query_player) {
    if(query_player>=game.max_clients)return false;
    for(uint32_t player=0;player<game.max_clients && player<MAX_PLAYERS;player++)
        if(S_CountTargetReservations(target,runtime_class,player) &&
           (player==query_player || G_GetPlayerAlliance(&game.clients[player].ps,&game.clients[query_player].ps,ALLIANCE_SHARED_SPELLS)))return true;
    return false;
}
void S_ResetTargetReservations(void) {
    free(reservation_counts);free(reservation_tokens);reservation_counts=NULL;reservation_tokens=NULL;
    count_capacity=count_used=count_tombs=token_capacity=token_used=token_tombs=0;
    /* Never recycle a token while a synchronous caller may retain its copy.
     * Restored leases raise this high-water mark when necessary. */
}
bool S_RestoreTargetReservation(targetReservationLease_t const *lease) {
    if(!lease || !lease->held)return true;
    reservationKey_t key;
    if(!lease->generation || !lease->target || !lease->target->inuse ||
       lease->target->spawn_time!=lease->target_spawn_time ||
       !reservation_key(&(targetReservationParams_t){lease->target,lease->code,lease->player},lease->target_spawn_time,&key) ||
       !reservation_insert(key,lease->generation))return false;
    if(lease->generation>reservation_generation)reservation_generation=lease->generation;
    return true;
}

#ifdef BZ_TESTS
#include "test.h"
extern void reset_entities(void),setup_test_world(void);
TEST(wc3_target_reservations, configured_keys_counts_and_stale_release) {
    reset_entities();setup_test_world();
    edict_t *target=G_Spawn();T_NOT_NULL(target);
    uint32_t a=MAKEFOURCC('A','c','o','2'),b=MAKEFOURCC('A','c','o','3');
    targetReservationLease_t first={0},second={0},other_alias={0},other_player={0};
    T_ASSERT(S_AcquireTargetReservation(&first,&(targetReservationParams_t){target,a,0}));
    T_ASSERT(S_AcquireTargetReservation(&second,&(targetReservationParams_t){target,a,0}));
    T_ASSERT(S_AcquireTargetReservation(&other_alias,&(targetReservationParams_t){target,b,0}));
    T_ASSERT(S_AcquireTargetReservation(&other_player,&(targetReservationParams_t){target,a,1}));
    T_EQ(S_CountTargetReservations(target,a,0),2);
    T_EQ(S_CountTargetReservations(target,b,0),1);
    T_EQ(S_CountTargetReservations(target,a,1),1);
    T_EQ(S_CountTargetReservations(target,MAKEFOURCC('A','c','o','i'),0),0);
    targetReservationLease_t stale=first;
    uint64_t old_generation=first.generation;
    S_ReleaseTargetReservation(&first);T_EQ(S_CountTargetReservations(target,a,0),1);
    S_ReleaseTargetReservation(&stale);T_EQ(S_CountTargetReservations(target,a,0),1);
    T_ASSERT(S_AcquireTargetReservation(&first,&(targetReservationParams_t){target,a,0}));
    T_ASSERT(first.generation!=old_generation);
    uint32_t birth=target->spawn_time;target->spawn_time++;
    T_EQ(S_CountTargetReservations(target,a,0),0);
    targetReservationLease_t new_incarnation={0};
    T_ASSERT(S_AcquireTargetReservation(&new_incarnation,&(targetReservationParams_t){target,a,0}));
    S_ReleaseTargetReservation(&first);S_ReleaseTargetReservation(&second);
    T_EQ(S_CountTargetReservations(target,a,0),1);
    S_ReleaseTargetReservation(&other_alias);S_ReleaseTargetReservation(&other_player);
    S_ReleaseTargetReservation(&new_incarnation);target->spawn_time=birth;
    T_EQ(S_CountTargetReservations(target,a,0),0);
    reset_entities();setup_test_world();
}
TEST(wc3_target_reservations, runtime_class_preference_uses_directional_shared_spells) {
    reset_entities();setup_test_world();
    edict_t *target=G_Spawn();T_NOT_NULL(target);T_ASSERT(game.max_clients>1);
    player_t *p0=&game.clients[0].ps,*p1=&game.clients[1].ps;
    bool forward=G_GetPlayerAlliance(p1,p0,ALLIANCE_SHARED_SPELLS);
    bool reverse=G_GetPlayerAlliance(p0,p1,ALLIANCE_SHARED_SPELLS);
    bool passive=G_GetPlayerAlliance(p1,p0,ALLIANCE_PASSIVE);
    G_SetPlayerAlliance(p1,p0,ALLIANCE_SHARED_SPELLS,false);
    G_SetPlayerAlliance(p0,p1,ALLIANCE_SHARED_SPELLS,false);
    G_SetPlayerAlliance(p1,p0,ALLIANCE_PASSIVE,false);
    uint32_t runtime_class=MAKEFOURCC('A','c','o','i');
    targetReservationLease_t alias={0},base={0};
    T_ASSERT(S_AcquireTargetReservation(&alias,&(targetReservationParams_t){target,MAKEFOURCC('A','c','o','2'),0}));
    T_ASSERT(!S_HasTargetReservation(target,runtime_class,0));
    T_ASSERT(S_AcquireTargetReservation(&base,&(targetReservationParams_t){target,runtime_class,1}));
    T_ASSERT(S_HasTargetReservation(target,runtime_class,1));
    T_ASSERT(!S_HasTargetReservation(target,runtime_class,0));
    G_SetPlayerAlliance(p0,p1,ALLIANCE_SHARED_SPELLS,true);
    T_ASSERT(!S_HasTargetReservation(target,runtime_class,0));
    G_SetPlayerAlliance(p1,p0,ALLIANCE_SHARED_SPELLS,true);
    T_ASSERT(S_HasTargetReservation(target,runtime_class,0));
    G_SetPlayerAlliance(p0,p1,ALLIANCE_SHARED_SPELLS,false);
    T_ASSERT(S_HasTargetReservation(target,runtime_class,0));
    S_ReleaseTargetReservation(&base);S_ReleaseTargetReservation(&alias);
    G_SetPlayerAlliance(p1,p0,ALLIANCE_SHARED_SPELLS,forward);
    G_SetPlayerAlliance(p0,p1,ALLIANCE_SHARED_SPELLS,reverse);
    G_SetPlayerAlliance(p1,p0,ALLIANCE_PASSIVE,passive);
    reset_entities();setup_test_world();
}
TEST(wc3_target_reservations, rebuild_preserves_generations_and_rejects_duplicate_leases) {
    reset_entities();setup_test_world();
    edict_t *target=G_Spawn();T_NOT_NULL(target);
    uint32_t code=MAKEFOURCC('A','c','o','2');
    targetReservationLease_t first={0},second={0},fresh={0};
    T_ASSERT(S_AcquireTargetReservation(&first,&(targetReservationParams_t){target,code,0}));
    T_ASSERT(S_AcquireTargetReservation(&second,&(targetReservationParams_t){target,code,0}));
    S_ResetTargetReservations();
    T_EQ(S_CountTargetReservations(target,code,0),0);
    targetReservationLease_t invalid=first;invalid.target_spawn_time++;
    T_ASSERT(!S_RestoreTargetReservation(&invalid));
    T_EQ(S_CountTargetReservations(target,code,0),0);
    T_ASSERT(S_RestoreTargetReservation(&second));
    T_ASSERT(S_RestoreTargetReservation(&first));
    T_ASSERT(!S_RestoreTargetReservation(&first));
    T_EQ(S_CountTargetReservations(target,code,0),2);
    T_ASSERT(S_AcquireTargetReservation(&fresh,&(targetReservationParams_t){target,code,0}));
    T_ASSERT(fresh.generation>first.generation && fresh.generation>second.generation);
    S_ReleaseTargetReservation(&first);S_ReleaseTargetReservation(&second);S_ReleaseTargetReservation(&fresh);
    T_EQ(S_CountTargetReservations(target,code,0),0);
    reset_entities();setup_test_world();
}
TEST(wc3_target_reservations, keyed_indexes_grow_and_reclaim_tombstones) {
    reset_entities();setup_test_world();
    edict_t *target=G_Spawn();T_NOT_NULL(target);
    targetReservationLease_t leases[160]={0};
    for(uint32_t pass=0;pass<3;pass++) {
        for(uint32_t i=0;i<160;i++)
            T_ASSERT(S_AcquireTargetReservation(leases+i,&(targetReservationParams_t){target,0x58303030u+i,i%2}));
        for(uint32_t i=0;i<160;i++)T_EQ(S_CountTargetReservations(target,0x58303030u+i,i%2),1);
        for(uint32_t i=0;i<160;i+=2)S_ReleaseTargetReservation(leases+i);
        for(uint32_t i=0;i<160;i++)T_EQ(S_CountTargetReservations(target,0x58303030u+i,i%2),i%2);
        for(uint32_t i=1;i<160;i+=2)S_ReleaseTargetReservation(leases+i);
    }
    reset_entities();setup_test_world();
}
#endif
