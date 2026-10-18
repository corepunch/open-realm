#include "g_local.h"
#include "skills/s_skills.h"

/* Target events share a registration history across concrete behavior owners.
 * Logical entries survive checkpoints; hash/ring links and callback snapshots
 * are derived. The node pool/hash grow independently of callback nesting;
 * snapshots retain registration identities, never pointers into those arrays. */
#define TARGET_OWNER_LIMIT (2*MAX_ENTITIES+MAX_PLAYERS*BOT_CAPTAIN_COUNT)
typedef struct {
    uint64_t sequence;
    uint32_t target,owner,target_birth,owner_birth,kind,mask;
} targetSubscriptionRecord_t;
typedef struct {
    targetSubscriptionRecord_t saved;
    uint32_t target_prev,target_next,owner_prev,owner_next,free_next;
    bool touched;
} targetSubscription_t;
static targetSubscription_t *target_subscriptions;
static uint32_t *target_subscription_hash;
static uint32_t target_subscription_capacity,target_subscription_hash_capacity;
static uint32_t target_subscription_heads[MAX_ENTITIES],target_subscription_tails[MAX_ENTITIES];
static uint32_t target_subscription_owners[TARGET_OWNER_LIMIT];
static uint32_t target_subscription_count,target_subscription_used,target_subscription_free,target_subscription_tombstones;
static uint64_t target_subscription_sequence;
#ifdef BZ_TESTS
static void (*target363_test_visit)(targetSubscriberKind_t,void *,edict_t *);
#endif

static uint32_t target_subscription_unit(edict_t const *unit) {
    uintptr_t offset=(uintptr_t)unit-(uintptr_t)g_edicts;
    return g_edicts && offset<sizeof(*unit)*MAX_ENTITIES && !(offset%sizeof(*unit)) ? offset/sizeof(*unit)+1 : 0;
}
static uint32_t target_subscription_captain(botCaptain_t const *captain) {
    FOR_LOOP(p,MAX_PLAYERS)FOR_LOOP(c,BOT_CAPTAIN_COUNT)
        if(captain==level.bots[p].captains+c)return p*BOT_CAPTAIN_COUNT+c+1;
    return 0;
}
static uint32_t target_subscription_owner_key(uint32_t kind,uint32_t owner) {
    return kind==TARGET_OWNER_CAPTAIN ? 2*MAX_ENTITIES+owner-1 : (kind-1)*MAX_ENTITIES+owner-1;
}
static uint32_t target_subscription_bucket(uint32_t kind,uint32_t owner,uint32_t target,uint32_t mask) {
    uint32_t value=target*0x9e3779b9u^owner*0x85ebca6bu^kind*0xc2b2ae35u^mask*0x27d4eb2du;
    value^=value>>16;return value&(target_subscription_hash_capacity-1);
}
static uint32_t target_subscription_find(uint32_t kind,uint32_t owner,uint32_t target,uint32_t mask,bool inserting) {
    if(!target_subscription_hash_capacity)return UINT32_MAX;
    uint32_t pos=target_subscription_bucket(kind,owner,target,mask),empty=UINT32_MAX;
    FOR_LOOP(i,target_subscription_hash_capacity) {
        uint32_t slot=target_subscription_hash[pos];
        if(!slot)return inserting && empty!=UINT32_MAX ? empty : pos;
        if(slot==UINT32_MAX) {if(empty==UINT32_MAX)empty=pos;}
        else {
            targetSubscriptionRecord_t const *r=&target_subscriptions[slot-1].saved;
            if(r->kind==kind && r->owner==owner && r->target==target && r->mask==mask)return pos;
        }
        pos=(pos+1)&(target_subscription_hash_capacity-1);
    }
    gi.error("Target events: exhausted subscriber hash");return 0;
}
static void target_subscription_rehash(uint32_t capacity) {
    uint32_t *table=gi.MemAlloc((size_t)capacity*sizeof(*table));
    memset(table,0,(size_t)capacity*sizeof(*table));gi.MemFree(target_subscription_hash);
    target_subscription_hash=table;target_subscription_hash_capacity=capacity;target_subscription_tombstones=0;
    FOR_LOOP(i,target_subscription_used) {
        targetSubscriptionRecord_t const *r=&target_subscriptions[i].saved;
        if(r->mask)target_subscription_hash[target_subscription_find(r->kind,r->owner,r->target,r->mask,true)]=i+1;
    }
}
static void target_subscription_remove(uint32_t id) {
    targetSubscription_t *node=target_subscriptions+id-1;
    targetSubscriptionRecord_t const *r=&node->saved;
    uint32_t target=r->target-1,owner=target_subscription_owner_key(r->kind,r->owner);
    uint32_t hash=target_subscription_find(r->kind,r->owner,r->target,r->mask,false);
    assert(target_subscription_hash[hash]==id);
    target_subscription_hash[hash]=UINT32_MAX;target_subscription_tombstones++;
    if(node->target_prev)target_subscriptions[node->target_prev-1].target_next=node->target_next;
    else target_subscription_heads[target]=node->target_next;
    if(node->target_next)target_subscriptions[node->target_next-1].target_prev=node->target_prev;
    else target_subscription_tails[target]=node->target_prev;
    if(node->owner_prev)target_subscriptions[node->owner_prev-1].owner_next=node->owner_next;
    else target_subscription_owners[owner]=node->owner_next;
    if(node->owner_next)target_subscriptions[node->owner_next-1].owner_prev=node->owner_prev;
    *node=(targetSubscription_t){.free_next=target_subscription_free};target_subscription_free=id;
    assert(target_subscription_count);target_subscription_count--;
}
static uint32_t target_subscription_insert(targetSubscriptionRecord_t const *record) {
    if(!target_subscription_hash_capacity)target_subscription_rehash(256);
    else if(target_subscription_count+target_subscription_tombstones>=target_subscription_hash_capacity/2) {
        uint32_t capacity=target_subscription_hash_capacity;
        if(target_subscription_count>=capacity/4) {
            if(capacity>UINT32_MAX/2)gi.error("Target events: subscriber hash address space exhausted");
            capacity*=2;
        }
        target_subscription_rehash(capacity);
    }
    uint32_t hash=target_subscription_find(record->kind,record->owner,record->target,record->mask,true);
    uint32_t id=target_subscription_hash[hash];
    if(id && id!=UINT32_MAX) {
        targetSubscription_t *node=target_subscriptions+id-1;
        if(node->saved.target_birth==record->target_birth && node->saved.owner_birth==record->owner_birth) {
            node->saved.mask=record->mask;node->touched=true;return id;
        }
        target_subscription_remove(id);
    }
    if(target_subscription_free) {id=target_subscription_free;target_subscription_free=target_subscriptions[id-1].free_next;}
    else {
        if(target_subscription_used==target_subscription_capacity) {
            uint32_t capacity=target_subscription_capacity ? target_subscription_capacity*2 : 128;
            if(capacity<target_subscription_capacity || (size_t)capacity>SIZE_MAX/sizeof(*target_subscriptions))
                gi.error("Target events: subscriber pool address space exhausted");
            targetSubscription_t *nodes=gi.MemAlloc((size_t)capacity*sizeof(*nodes));
            if(target_subscription_used)memcpy(nodes,target_subscriptions,(size_t)target_subscription_used*sizeof(*nodes));
            gi.MemFree(target_subscriptions);target_subscriptions=nodes;target_subscription_capacity=capacity;
        }
        id=++target_subscription_used;
    }
    uint32_t target=record->target-1,owner=target_subscription_owner_key(record->kind,record->owner);
    targetSubscription_t *node=target_subscriptions+id-1;
    *node=(targetSubscription_t){.saved=*record,.target_prev=target_subscription_tails[target],
        .owner_next=target_subscription_owners[owner],.touched=true};
    if(!node->saved.sequence) {
        if(target_subscription_sequence==UINT64_MAX)gi.error("Target events: exhausted registration sequence");
        node->saved.sequence=++target_subscription_sequence;
    }
    if(node->target_prev)target_subscriptions[node->target_prev-1].target_next=id;
    else target_subscription_heads[target]=id;
    target_subscription_tails[target]=id;
    if(node->owner_next)target_subscriptions[node->owner_next-1].owner_prev=id;
    target_subscription_owners[owner]=id;
    if(target_subscription_hash[hash]==UINT32_MAX)target_subscription_tombstones--;
    target_subscription_hash[hash]=id;target_subscription_count++;return id;
}
void G_ResetTargetSubscriptions(void) {
    gi.MemFree(target_subscriptions);target_subscriptions=NULL;target_subscription_capacity=0;
    gi.MemFree(target_subscription_hash);target_subscription_hash=NULL;target_subscription_hash_capacity=0;
    memset(target_subscription_heads,0,sizeof(target_subscription_heads));
    memset(target_subscription_tails,0,sizeof(target_subscription_tails));
    memset(target_subscription_owners,0,sizeof(target_subscription_owners));
    target_subscription_count=target_subscription_used=target_subscription_free=target_subscription_tombstones=0;
    target_subscription_sequence=0;
}
void G_ClearTargetSubscriptionOwner(targetSubscriberKind_t kind,void *owner) {
    uint32_t identity=kind==TARGET_OWNER_CAPTAIN ? target_subscription_captain(owner) : target_subscription_unit(owner);
    if(!identity)return;
    uint32_t key=target_subscription_owner_key(kind,identity);
    while(target_subscription_owners[key])target_subscription_remove(target_subscription_owners[key]);
}
void G_SetUnitTargetSubscription(targetSubscriberKind_t kind,edict_t *owner,edict_t *target) {
    assert(kind==TARGET_OWNER_MOVE || kind==TARGET_OWNER_ATTACK);
    uint32_t identity=target_subscription_unit(owner),other=target_subscription_unit(target);
    if(!identity)return;
    /* Concrete target rebinding retires its previous callback first, even
     * for the same target (notably Move5fc640 approach-to-Follow handoff).
     * The lower event registry still deduplicates direct same-event updates. */
    G_ClearTargetSubscriptionOwner(kind,owner);
    if(other)FOR_LOOP(i,2)target_subscription_insert(&(targetSubscriptionRecord_t){.target=other,.owner=identity,
        .target_birth=target->spawn_time,.owner_birth=owner->spawn_time,.kind=kind,.mask=1u<<i});
}
/* Native9cf680 roster registration selects death/owner, while retained
 * target roles also select lost. One callback/event entry owns the union;
 * adding a role must not reorder existing death/owner registrations. */
static uint32_t target_subscription_captain_mask(botCaptain_t const *captain,edict_t const *target) {
    uint32_t mask=0;
    FOR_EACH_ARRAY(edict_t *,member,captain->units)if(*member==target){mask=6;break;}
    botCaptainTarget_t const *roles[]={&captain->target,&captain->follow,&captain->backup};
    FOR_LOOP(i,3)if(roles[i]->unit==target && roles[i]->birth==target->spawn_time)return 7;
    return mask;
}
static void target_subscription_add_captain(botCaptain_t *captain,edict_t *target,uint32_t mask) {
    uint32_t identity=target_subscription_captain(captain),other=target_subscription_unit(target);
    uint32_t const events[]={1,4,2}; /*9d4a50 lost, death, owner.*/
    if(identity && other)FOR_LOOP(i,3)if(mask&events[i])target_subscription_insert(&(targetSubscriptionRecord_t){.target=other,.owner=identity,
        .target_birth=target->spawn_time,.kind=TARGET_OWNER_CAPTAIN,.mask=events[i]});
}
/* The enrollment producer already knows this is a roster member. Avoid a
 * second roster scan for each insertion or each full refresh. */
void G_AddCaptainTargetSubscription(botCaptain_t *captain,edict_t *target) {
    target_subscription_add_captain(captain,target,6);
}
void G_RemoveCaptainTargetSubscription(botCaptain_t *captain,edict_t *target) {
    uint32_t identity=target_subscription_captain(captain),other=target_subscription_unit(target);
    if(!identity || !other)return;
    FOR_LOOP(i,3) {
        uint32_t pos=target_subscription_find(TARGET_OWNER_CAPTAIN,identity,other,1u<<i,false);
        uint32_t id=pos==UINT32_MAX ? 0 : target_subscription_hash[pos];
        if(id && id!=UINT32_MAX)target_subscription_remove(id);
    }
}
void G_RefreshCaptainTargetSubscriptions(botCaptain_t *captain) {
    uint32_t identity=target_subscription_captain(captain);if(!identity)return;
    uint32_t key=target_subscription_owner_key(TARGET_OWNER_CAPTAIN,identity);
    for(uint32_t id=target_subscription_owners[key];id;id=target_subscriptions[id-1].owner_next)
        target_subscriptions[id-1].touched=false;
    FOR_EACH_ARRAY(edict_t *,member,captain->units)G_AddCaptainTargetSubscription(captain,*member);
    botCaptainTarget_t const *roles[]={&captain->target,&captain->follow,&captain->backup};
    FOR_LOOP(i,3)if(roles[i]->unit && roles[i]->unit->inuse && roles[i]->unit->spawn_time==roles[i]->birth)
        target_subscription_add_captain(captain,roles[i]->unit,7);
    uint32_t id=target_subscription_owners[key];
    while(id) {uint32_t next=target_subscriptions[id-1].owner_next;if(!target_subscriptions[id-1].touched)target_subscription_remove(id);id=next;}
}
void G_DispatchTargetSubscriptions(edict_t *target,abilityMsg_t message) {
    uint32_t identity=target_subscription_unit(target);if(!identity)return;
    uint32_t mask=message==A_TARGET_OWNER_CHANGED ? 2 : message==A_DEATH ? 4 : 1,count=0;
    for(uint32_t id=target_subscription_heads[identity-1];id;id=target_subscriptions[id-1].target_next)
        if(target_subscriptions[id-1].saved.mask&mask)count++;
    if(!count)return;
    struct targetSubscriptionDelivery {uint32_t id;uint64_t sequence;} *delivery=gi.MemAlloc(count*sizeof(*delivery));
    uint32_t pos=0;
    for(uint32_t id=target_subscription_heads[identity-1];id;id=target_subscriptions[id-1].target_next)
        if(target_subscriptions[id-1].saved.mask&mask)delivery[pos++]=(typeof(*delivery)){id,target_subscriptions[id-1].saved.sequence};
    uint32_t birth=target->spawn_time;
    FOR_LOOP(i,count) {
        targetSubscriptionRecord_t r=target_subscriptions[delivery[i].id-1].saved;
        if(!(r.mask&mask) || r.sequence!=delivery[i].sequence || r.target!=identity || r.target_birth!=birth)continue;
        if(!target->inuse || target->spawn_time!=birth)break;
        if(r.kind==TARGET_OWNER_CAPTAIN) {
            uint32_t owner=r.owner-1;
#ifdef BZ_TESTS
            if(target363_test_visit)target363_test_visit(TARGET_OWNER_CAPTAIN,
                level.bots[owner/BOT_CAPTAIN_COUNT].captains+owner%BOT_CAPTAIN_COUNT,target);
#endif
            G_BotCaptainTargetEvent(level.bots[owner/BOT_CAPTAIN_COUNT].captains+owner%BOT_CAPTAIN_COUNT,target);
        } else {
            edict_t *owner=g_edicts+r.owner-1;
            if(!owner->inuse || G_IsDeferredFree(owner) || owner->spawn_time!=r.owner_birth)continue;
#ifdef BZ_TESTS
            if(target363_test_visit)target363_test_visit(r.kind,owner,target);
#endif
            if(r.kind==TARGET_OWNER_MOVE)S_DeliverMoveTargetEvent(owner,target,message);
            else S_DeliverAttackTargetEvent(owner,target,message);
        }
    }
    gi.MemFree(delivery);
}

static bool target_subscription_record_valid(targetSubscriptionRecord_t const *r) {
    if(!r->sequence || r->sequence>target_subscription_sequence || !r->target || r->target>globals.num_edicts ||
       r->kind<TARGET_OWNER_MOVE || r->kind>TARGET_OWNER_CAPTAIN || !r->owner)return false;
    edict_t const *target=g_edicts+r->target-1;
    if(!target->inuse || target->spawn_time!=r->target_birth)return false;
    if(r->kind==TARGET_OWNER_CAPTAIN) {
        if(r->owner>MAX_PLAYERS*BOT_CAPTAIN_COUNT || r->owner_birth || (r->mask!=1 && r->mask!=2 && r->mask!=4))return false;
        uint32_t owner=r->owner-1;
        return (target_subscription_captain_mask(level.bots[owner/BOT_CAPTAIN_COUNT].captains+owner%BOT_CAPTAIN_COUNT,target)&r->mask)!=0;
    }
    if(r->owner>globals.num_edicts || (r->mask!=1 && r->mask!=2))return false;
    edict_t const *owner=g_edicts+r->owner-1;
    if(!owner->inuse || owner->spawn_time!=r->owner_birth)return false;
    return r->kind==TARGET_OWNER_MOVE ? owner->movement.follow_target==target && owner->movement.follow_target_spawn_time==target->spawn_time :
        owner->attack_target==target && owner->attack_target_spawn_time==target->spawn_time;
}
static bool target_subscription_exists(uint32_t kind,uint32_t owner,edict_t const *target,uint32_t mask) {
    uint32_t identity=target_subscription_unit(target);if(!identity)return false;
    uint32_t pos=target_subscription_find(kind,owner,identity,mask,false);
    uint32_t slot=pos==UINT32_MAX ? 0 : target_subscription_hash[pos];
    return slot && slot!=UINT32_MAX && target_subscription_record_valid(&target_subscriptions[slot-1].saved);
}
static int target_subscription_compare(void const *a,void const *b) {
    uint64_t x=target_subscriptions[*(uint32_t const *)a].saved.sequence;
    uint64_t y=target_subscriptions[*(uint32_t const *)b].saved.sequence;
    return (x>y)-(x<y);
}
bool G_ValidateTargetSubscriptions(void) {
    uint32_t *ordered=gi.MemAlloc(MAX(1,target_subscription_count)*sizeof(*ordered)),count=0;
    bool valid=true;
    FOR_LOOP(i,target_subscription_used)if(target_subscriptions[i].saved.mask) {
        if(!target_subscription_record_valid(&target_subscriptions[i].saved)) {valid=false;break;}
        ordered[count++]=i;
    }
    if(valid) {
        qsort(ordered,count,sizeof(*ordered),target_subscription_compare);
        FOR_LOOP(i,count)if(i && !target_subscription_compare(ordered+i-1,ordered+i)) {valid=false;break;}
    }
    if(count!=target_subscription_count)valid=false;
    gi.MemFree(ordered);
    if(!valid)return false;
    FOR_LOOP(i,globals.num_edicts) {
        edict_t const *unit=g_edicts+i;if(!unit->inuse)continue;
        FOR_LOOP(e,2) {
            if(unit->movement.follow_target && !target_subscription_exists(TARGET_OWNER_MOVE,i+1,unit->movement.follow_target,1u<<e))return false;
            if(unit->attack_target && !target_subscription_exists(TARGET_OWNER_ATTACK,i+1,unit->attack_target,1u<<e))return false;
        }
    }
    FOR_LOOP(p,MAX_PLAYERS)FOR_LOOP(c,BOT_CAPTAIN_COUNT) {
        botCaptain_t const *captain=level.bots[p].captains+c;
        uint32_t owner=p*BOT_CAPTAIN_COUNT+c+1;
        FOR_LOOP(e,3) {
            if(e)FOR_EACH_ARRAY(edict_t *,member,captain->units)
                if(!target_subscription_exists(TARGET_OWNER_CAPTAIN,owner,*member,1u<<e))return false;
            botCaptainTarget_t const *roles[]={&captain->target,&captain->follow,&captain->backup};
            FOR_LOOP(i,3)if(roles[i]->unit && !target_subscription_exists(TARGET_OWNER_CAPTAIN,owner,roles[i]->unit,1u<<e))return false;
        }
    }
    return true;
}
bool G_WriteTargetSubscriptions(FILE *file) {
    if(!G_ValidateTargetSubscriptions())return false;
    if(fwrite(&target_subscription_sequence,sizeof(target_subscription_sequence),1,file)!=1 ||
       fwrite(&target_subscription_count,sizeof(target_subscription_count),1,file)!=1)return false;
    uint32_t *ordered=gi.MemAlloc(MAX(1,target_subscription_count)*sizeof(*ordered)),count=0;
    FOR_LOOP(i,target_subscription_used)if(target_subscriptions[i].saved.mask)ordered[count++]=i;
    qsort(ordered,count,sizeof(*ordered),target_subscription_compare);
    bool ok=true;
    FOR_LOOP(i,count)if(fwrite(&target_subscriptions[ordered[i]].saved,sizeof(targetSubscriptionRecord_t),1,file)!=1) {ok=false;break;}
    gi.MemFree(ordered);return ok;
}
bool G_ReadTargetSubscriptions(FILE *file) {
    uint64_t sequence;uint32_t count;
    if(fread(&sequence,sizeof(sequence),1,file)!=1 || fread(&count,sizeof(count),1,file)!=1)return false;
    uint64_t maximum=4*(uint64_t)globals.num_edicts+9*MAX_PLAYERS*BOT_CAPTAIN_COUNT;
    FOR_LOOP(p,MAX_PLAYERS)FOR_LOOP(c,BOT_CAPTAIN_COUNT)maximum+=3*(uint64_t)ARRAY_COUNT(level.bots[p].captains[c].units);
    if(count>maximum)return false;
    G_ResetTargetSubscriptions();target_subscription_sequence=sequence;
    uint64_t previous=0;
    FOR_LOOP(i,count) {
        targetSubscriptionRecord_t record;
        if(fread(&record,sizeof(record),1,file)!=1 || record.sequence<=previous ||
           !target_subscription_record_valid(&record))return false;
        uint32_t pos=target_subscription_find(record.kind,record.owner,record.target,record.mask,false);
        uint32_t slot=pos==UINT32_MAX ? 0 : target_subscription_hash[pos];
        if(slot && slot!=UINT32_MAX)return false;
        target_subscription_insert(&record);previous=record.sequence;
    }
    return G_ValidateTargetSubscriptions();
}
