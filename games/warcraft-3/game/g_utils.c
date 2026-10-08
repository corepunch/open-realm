#include "g_local.h"
#include "games/warcraft-3/common/wc3_pathing_coordinates.h"

typedef struct {
    wc3Clock_t deadline;
    uint32_t spawn_time, sequence, heap_index; /* index plus one; zero means absent */
} deferred_free_t;

static deferred_free_t deferred_frees[MAX_ENTITIES];
static uint32_t deferred_free_heap[MAX_ENTITIES];
static uint32_t deferred_free_count;

static bool DeferredFreeLess(uint32_t a,uint32_t b) {
    deferred_free_t const *x=deferred_frees+a,*y=deferred_frees+b;
    return x->deadline.time==y->deadline.time ? x->sequence<y->sequence : x->deadline.time<y->deadline.time;
}
static void DeferredFreePut(uint32_t index,uint32_t slot) {
    deferred_free_heap[index]=slot;deferred_frees[slot].heap_index=index+1;
}
static void DeferredFreeDown(uint32_t index,uint32_t slot) {
    while(index*2+1<deferred_free_count) {
        uint32_t child=index*2+1;
        if(child+1<deferred_free_count && DeferredFreeLess(deferred_free_heap[child+1],deferred_free_heap[child]))child++;
        if(DeferredFreeLess(slot,deferred_free_heap[child]))break;
        DeferredFreePut(index,deferred_free_heap[child]);index=child;
    }
    DeferredFreePut(index,slot);
}

/* A trapped unit keeps its identity but is absent from normal world interaction. */
bool G_UnitIsWorldActive(edict_t const *ent) {
    return ent && ent->inuse && !(ent->aiflags & AI_SOUL_TRAPPED);
}

/* Hidden actors leave fine occupancy; showing them is a new publication.
 * Queue this at the owner so same-tick queries never need a world scan. */
void G_SetEntityHidden(edict_t *ent, bool hidden) {
    if(hidden)ent->s.renderfx|=RF_HIDDEN;
    else ent->s.renderfx&=~RF_HIDDEN;
    G_MarkMoveSpatialObject(ent);
}

/* Drop a queued removal when another lifecycle path frees the same edict first. */
static void DeferredFreeRemove(uint32_t slot) {
    if(slot>=MAX_ENTITIES || !deferred_frees[slot].heap_index)return;
    uint32_t index=deferred_frees[slot].heap_index-1,last=deferred_free_heap[--deferred_free_count];
    deferred_frees[slot].heap_index=0;
    if(index==deferred_free_count)return;
    while(index && DeferredFreeLess(last,deferred_free_heap[(index-1)/2])) {
        DeferredFreePut(index,deferred_free_heap[(index-1)/2]);index=(index-1)/2;
    }
    DeferredFreeDown(index,last);
}

static void G_CancelDeferredFree(edict_t *ent) { DeferredFreeRemove(ent->s.number); }

/* Pending removal suspends execution; queries retain the live identity until release. */
bool G_IsDeferredFree(edict_t const *ent) {
    return ent && ent->s.number<MAX_ENTITIES && deferred_frees[ent->s.number].heap_index &&
        deferred_frees[ent->s.number].spawn_time==ent->spawn_time;
}

/* Remove an entity from every live JASS group before its handle becomes stale. */
static void G_RemoveEntityFromJassGroups(edict_t *ent) {
    FOR_LOOP(i, level.num_groups) {
        ggroup_t *group = level.groups[i];
        if (!group->inuse) continue;
        for (uint32_t k = 0; k < group->num_units;) {
            if (group->units[k] != ent) { k++; continue; }
            for (uint32_t n = k + 1; n < group->num_units; n++) group->units[n - 1] = group->units[n];
            group->num_units--;
        }
    }
}

void G_SetPlayerText(gameClient_t *client, PLAYERTEXT index, cstring_t text) {
    uint32_t cursor;

    if (!client || index >= PLAYERTEXT_COUNT) {
        return;
    }
    cursor = ++client->playerTextCursor[index] & PLAYER_TEXT_MASK;
    snprintf(client->playerTextStorage[index][cursor],
             sizeof(client->playerTextStorage[index][cursor]),
             "%s",
             text ? text : "");
    client->ps.texts[index] = client->playerTextStorage[index][cursor];
}

void G_FreeEdict(edict_t *ent) {
    if (!ent) return;
    bool const had_static_pathing = G_EntityHasStaticPathing(ent);
    bool const had_aura = S_UnitHasAuraSource(ent);
    if (had_aura) S_MarkAuraSource(ent);
    G_ClearUnitResponses(ent);
    G_CancelDeferredFree(ent);
    /* Native69c510 commits inactivity before availability and detach callbacks.
     * No death event is produced by removal; the identity stays queryable. */
    if(ent->class_id && !M_IsDead(ent)) {
        ent->svflags|=SVF_DEADMONSTER;
        G_SetHealth(ent,0);
        S_UnitAbilityEvent(ent,A_UNIT_RETIRE);
    }
    S_UnitAbilityEvent(ent, A_UNIT_REMOVE);
    G_BotRemoveCaptainUnit(ent);
    /* Direct JASS RemoveUnit must release transient construction/upgrade state
     * before the edict is cleared. Forced removal does not grant a player
     * cancellation refund. */
    if (G_BuildingUpgradeActive(ent)) G_StopBuildingUpgrade(ent, false);
    if (ent->construction) G_StopConstruction(ent);
    if ((ent->mineoverlay && ent->mineoverlay->parent) || ent->think == blight_mine_think) S_MineOverlayRelease(ent);
    if (S_AcolyteHarvestIsActive(ent)) S_AcolyteHarvestRelease(ent);
    S_CargoReleaseUnit(ent);
    if (ent->cargo && ent->cargo->count > 0) cargo_drop_all(ent);
    if (ent->buildwork && ent->buildwork->ability) S_CancelRepair(ent);
    /* Remove both the active accepted-build indicator and any owner-only
     * indicators attached to delayed Shift-build queue entries. Direct
     * RemoveUnit must not leave construction placeholders behind. */
    G_ClearBuildPreview(ent);
    G_ClearUnitOrderQueue(ent);
    /* Removed units cannot remain in JASS groups: save files require every group member to resolve to a live edict. */
    FOR_LOOP(i, level.num_groups) {
        ggroup_t *group = level.groups[i];
        if (!group->inuse) continue;
        for (uint32_t k = 0; k < group->num_units;) {
            if (group->units[k] != ent) { k++; continue; }
            for (uint32_t n = k + 1; n < group->num_units; n++) group->units[n - 1] = group->units[n];
            group->num_units--;
        }
    }
    G_UnregisterGroundSurface(ent);
    G_InvalidateUnitShortcutsForUnit(ent);
    G_InvalidateRallyTarget(ent);
    if ((ent->revival && ent->revival->reviving)) G_CancelHeroRevive(ent->revival->producer, ent);
    if (ent->training) G_ClearTrainingQueueFood(ent);
    else { G_CancelHeroRevives(ent); G_CancelTrainingQueue(ent, true); }
    G_ClearUnitFood(ent);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    S_GoldMineReleaseWorker(ent);
    gi.UnlinkEntity(ent);
    G_RemoveMoveSpatialObject(ent);
    G_PoolsReleaseEdict(ent);
    S_MarkMoveGoals(ent);
    G_SetEntitySelectionMask(ent, 0);
    /* A removal callback can request this already-retired identity again. */
    G_CancelDeferredFree(ent);
    memset(ent, 0, sizeof(*ent));
    /* Removal callbacks can consume the earlier notification while still live. */
    if (had_aura) S_MarkAuraSource(ent);
    M_TrackMove(ent);
    S_TrackMoveTimers(ent);
    ent->freetime = level.time;
    G_MarkFreeEdict(ent);
    if (had_static_pathing) CM_BakeStaticObstacles();
}

/* Retail RemoveUnit retires commands now, then queues a primary-clock release. */
void G_DeferFreeEdict(edict_t *ent) {
    wc3Clock_t clock=G_TimerQueryClock(NULL);G_DeferFreeEdictAt(ent,&clock);
}

void G_DeferFreeEdictAt(edict_t *ent,wc3Clock_t const *clock) {
    if (!ent || !ent->inuse) return;
    if(G_IsDeferredFree(ent))return;
    G_CancelDeferredFree(ent);
    if (deferred_free_count >= MAX_ENTITIES) {
        fprintf(stderr, "WC3: deferred unit removal queue exhausted\n");
        return;
    }
    bool const had_static_pathing = G_EntityHasStaticPathing(ent);
    G_SetEntityHidden(ent,true);
    G_RemoveMoveSpatialObject(ent);
    /* RemoveUnit becomes absent now; a route issued in the same JASS callback
     * must already see the remaining footprints and terrain baseline. */
    if (had_static_pathing) CM_BakeStaticObstacles();
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    G_InvalidateCommands(G_GetPlayerClientByNumber(ent->s.player));
    G_RemoveEntityFromJassGroups(ent);
    uint32_t const slot=ent->s.number;
    deferred_frees[slot]=(deferred_free_t){.deadline=*clock,.spawn_time=ent->spawn_time,
        .sequence=++level.timer_sequence};
    deferred_frees[slot].deadline.time=wc3_add(clock->time,G_ClockMinimumDelay());
    uint32_t index=deferred_free_count++;
    while(index && DeferredFreeLess(slot,deferred_free_heap[(index-1)/2])) {
        DeferredFreePut(index,deferred_free_heap[(index-1)/2]);index=(index-1)/2;
    }
    DeferredFreePut(index,slot);
    /* Native694690 retires the order chain before returning from RemoveUnit;
     * deferred storage release must not keep its commands or physical task. */
    G_ClearUnitOrderQueue(ent);
    ent->current_order_id = 0;
    S_UnitAbilityEvent(ent, A_UNIT_REMOVING);
    S_UnitTargetRemoved(ent);
    /* Removal replaces the old task. Keeping its callback in the scheduled
     * owner set would execute canceled movement before deferred storage free. */
    M_SetMove(ent, NULL);
}

bool G_NextUnitRelease(wc3Clock_t *deadline,uint32_t *sequence) {
    if(!deferred_free_count)return false;
    deferred_free_t const *next=deferred_frees+deferred_free_heap[0];
    *deadline=next->deadline;*sequence=next->sequence;return true;
}

void G_FireUnitRelease(void) {
    uint32_t const slot=deferred_free_heap[0];
    deferred_free_t pending=deferred_frees[slot];edict_t *ent=g_edicts+slot;
    /* Legacy queued death producers still deliver before their unit release.
     * Dispatch can mutate the heap, so the caller samples its new head. */
    if (level.vm && ent->inuse && ent->spawn_time == pending.spawn_time && G_HasPendingDeathEvent(ent)) {
        G_RunEvents();jass_runevents(level.vm);return;
    }
    DeferredFreeRemove(slot);
    if(ent->inuse && ent->spawn_time==pending.spawn_time)G_FreeEdict(ent);
}

void G_RebaseUnitReleases(float span) {
    FOR_LOOP(i,deferred_free_count) {
        deferred_free_t *pending=deferred_frees+deferred_free_heap[i];
        pending->deadline.time=wc3_sub(pending->deadline.time,span);pending->deadline.epoch++;
    }
}

/* Post-entity work may have requested another release after the primary drain.
 * Do not advance its clock or expose a future callback before the next quantum. */
void G_RunDeferredFrees(void) {
    wc3Clock_t now=G_TimerQueryClock(NULL),due;uint32_t serial;
    wc3Clock_t saved=level.timer_clock,source=level.timer_source_clock;
    bool const valid=level.timer_clock_valid;
    level.timer_source_clock=level.pathing_clock;level.timer_clock_valid=true;
    while(G_NextUnitRelease(&due,&serial) && (due.epoch==now.epoch ? due.time<=now.time :
        (int32_t)(due.epoch-now.epoch)<0)) {
        level.timer_clock=due;G_FireUnitRelease();
    }
    level.timer_clock=saved;level.timer_source_clock=source;level.timer_clock_valid=valid;
}

void G_ResetDeferredFrees(void) {
    FOR_LOOP(i,deferred_free_count)deferred_frees[deferred_free_heap[i]].heap_index=0;
    deferred_free_count=0;
}

#ifdef BZ_TESTS
/* Isolated cleanup tests supply the release boundary, without claiming a clock
 * producer. Scheduler tests call the real primary drain or globals.RunFrame. */
void G_TestFinishDeferredFrees(void) { while(deferred_free_count)G_FireUnitRelease(); }
#endif

event_t *G_MakeEvent(EVENTTYPE type) {
    FOR_LOOP(i, MAX_EVENTS) if (G_EventSlotAvailable(level.events.handlers+i)) {
        event_t *evt = &level.events.handlers[i];
        uintptr_t generation = evt->handle_generation;
        memset(evt, 0, sizeof(*evt)); evt->handle_generation = generation;
        evt->inuse = true; evt->type = type;
        if(level.events.registration_sequence==UINT64_MAX)gi.error("WC3: event registration sequence exhausted");
        evt->registration_sequence=++level.events.registration_sequence;
        G_TrackEventSubscriber(evt);G_TrackMoveRegionEvent(evt);
        return evt;
    }
    fprintf(stderr, "WC3: event slot limit %u reached\n", MAX_EVENTS);
    return NULL;
}

void G_SetEventSubject(event_t *evt, edict_t *subject) {
    evt->subject = subject;
    evt->subject_spawn_time = subject ? subject->spawn_time : 0;
    evt->subject_spawn_tracked = subject != NULL;
    G_TrackEventSubscriber(evt);
}

void G_SetPlayerEventSubject(event_t *evt, edict_t *subject) {
    evt->subject = subject;
    evt->subject_spawn_time = 0;
    evt->subject_spawn_tracked = false;
    G_TrackEventSubscriber(evt);
}

bool G_EventSubjectIsCurrent(event_t *evt) {
    return !evt->subject || !evt->subject_spawn_tracked ||
        (evt->subject->inuse && evt->subject->spawn_time == evt->subject_spawn_time &&
         (G_IsDeathEvent(evt->type) || !G_IsDeferredFree(evt->subject)));
}

#define JASS_GROUP_DEBUG_CHAIN_SIZE 256 // characters; bounds one captured JASS call chain for group diagnostics
#define JASS_GROUP_DEBUG_MAX_STATS 128 // entries; bounds distinct group-debug chains retained per map

typedef struct {
    char chain[JASS_GROUP_DEBUG_CHAIN_SIZE];
    int32_t trigger_ordinal;
    uint32_t allocations;
    uint32_t frees;
} jass_group_debug_stat_t;

typedef struct {
    cstring_t creator;
    char chain[JASS_GROUP_DEBUG_CHAIN_SIZE];
    int32_t trigger_ordinal;
} jass_group_debug_slot_t;

static jass_group_debug_slot_t *jass_group_debug_slots;
static uint32_t jass_group_debug_slot_capacity;
static jass_group_debug_stat_t jass_group_debug_stats[JASS_GROUP_DEBUG_MAX_STATS];
static uint32_t jass_group_debug_num_stats;
static bool jass_group_debug_full_reported;

bool G_JassGroupDebugEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("wc3_group_debug", "0")) != 0;
}

static bool G_EnsureJassGroupDebugSlots(uint32_t count) {
    jass_group_debug_slot_t *slots;
    uint32_t capacity;

    if (count <= jass_group_debug_slot_capacity) return true;
    capacity = level.group_capacity > count ? level.group_capacity : count;
    if (!capacity) capacity = JASS_GROUP_INITIAL_CAPACITY;
    if ((size_t)capacity > (size_t)-1 / sizeof(*slots)) return false;
    slots = gi.MemAlloc((size_t)capacity * sizeof(*slots));
    if (!slots) return false;
    memset(slots, 0, (size_t)capacity * sizeof(*slots));
    FOR_LOOP(i, capacity) slots[i].trigger_ordinal = -1;
    if (jass_group_debug_slots) {
        memcpy(slots, jass_group_debug_slots,
               (size_t)jass_group_debug_slot_capacity * sizeof(*slots));
        gi.MemFree(jass_group_debug_slots);
    }
    jass_group_debug_slots = slots;
    jass_group_debug_slot_capacity = capacity;
    return true;
}

void G_ResetJassGroupDebug(void) {
    if (jass_group_debug_slots) gi.MemFree(jass_group_debug_slots);
    jass_group_debug_slots = NULL;
    jass_group_debug_slot_capacity = 0;
    memset(jass_group_debug_stats, 0, sizeof(jass_group_debug_stats));
    jass_group_debug_num_stats = 0;
    jass_group_debug_full_reported = false;
}

static jass_group_debug_stat_t *G_FindJassGroupDebugStat(cstring_t chain, int32_t trigger_ordinal, bool create) {
    cstring_t key = chain && *chain ? chain : "<unknown>";
    FOR_LOOP(i, jass_group_debug_num_stats) {
        jass_group_debug_stat_t *stat = &jass_group_debug_stats[i];
        if (stat->trigger_ordinal == trigger_ordinal && !strcmp(stat->chain, key)) return stat;
    }
    if (!create || jass_group_debug_num_stats >= JASS_GROUP_DEBUG_MAX_STATS) return NULL;
    jass_group_debug_stat_t *stat = &jass_group_debug_stats[jass_group_debug_num_stats++];
    snprintf(stat->chain, sizeof(stat->chain), "%s", key);
    stat->trigger_ordinal = trigger_ordinal;
    return stat;
}

bool G_JassGroupIndex(ggroup_t const *group, uint32_t *index) {
    uint32_t id;
    if (!group || !level.groups) return false;
    id = group->handle_id;
    if (id >= level.num_groups || level.groups[id] != group) return false;
    if (index) *index = id;
    return true;
}

ggroup_t *G_JassGroupByIndex(uint32_t index) {
    return index < level.num_groups && level.groups ? level.groups[index] : NULL;
}

bool G_JassGroupValid(ggroup_t const *group) {
    return G_JassGroupIndex(group, NULL) && group->inuse;
}

bool G_QuestValid(quest_t const *quest) {
    return quest && quest >= level.quests && quest < level.quests + MAX_QUESTS && quest->inuse;
}

bool G_QuestItemValid(questItem_t const *item) {
    FOR_LOOP(i, MAX_QUESTS) if (level.quests[i].inuse && item >= level.quests[i].items &&
                                item < level.quests[i].items + MAX_QUESTITEMS)
        return item->inuse;
    return false;
}

void G_SetJassGroupDebugContext(ggroup_t *group, cstring_t creator, cstring_t chain, int32_t trigger_ordinal) {
    uint32_t index;
    jass_group_debug_slot_t *slot;
    jass_group_debug_stat_t *stat;
    if (!G_JassGroupValid(group) || !G_JassGroupIndex(group, &index) ||
        !G_EnsureJassGroupDebugSlots(index + 1)) return;
    slot = &jass_group_debug_slots[index];
    slot->creator = creator;
    snprintf(slot->chain, sizeof(slot->chain), "%s",
             chain && *chain ? chain : (creator && *creator ? creator : "<unknown>"));
    slot->trigger_ordinal = trigger_ordinal;
    stat = G_FindJassGroupDebugStat(slot->chain, trigger_ordinal, true);
    if (stat) stat->allocations++;
}

void G_SetJassGroupDebugCreator(ggroup_t *group, cstring_t creator) {
    G_SetJassGroupDebugContext(group, creator, creator, -1);
}

cstring_t G_GetJassGroupDebugCreator(ggroup_t const *group) {
    uint32_t index;
    if (!G_JassGroupValid(group) || !G_JassGroupIndex(group, &index) ||
        index >= jass_group_debug_slot_capacity) return NULL;
    return jass_group_debug_slots[index].creator;
}

cstring_t G_GetJassGroupDebugChain(ggroup_t const *group) {
    uint32_t index;
    cstring_t chain;
    if (!G_JassGroupValid(group) || !G_JassGroupIndex(group, &index) ||
        index >= jass_group_debug_slot_capacity) return NULL;
    chain = jass_group_debug_slots[index].chain;
    return *chain ? chain : NULL;
}

int32_t G_GetJassGroupDebugTrigger(ggroup_t const *group) {
    uint32_t index;
    if (!G_JassGroupValid(group) || !G_JassGroupIndex(group, &index) ||
        index >= jass_group_debug_slot_capacity) return -1;
    return jass_group_debug_slots[index].trigger_ordinal;
}

void G_DumpJassGroupDebug(cstring_t failing_creator, cstring_t failing_chain, int32_t failing_trigger) {
    typedef struct {
        char chain[JASS_GROUP_DEBUG_CHAIN_SIZE];
        int32_t trigger_ordinal;
        uint32_t live;
    } group_chain_count_t;
    group_chain_count_t counts[JASS_GROUP_DEBUG_MAX_STATS] = {0};
    uint32_t num_counts = 0;
#ifdef WC3_DEBUG_GROUPS
    uint32_t live = 0;
#endif

    if (!G_JassGroupDebugEnabled() || jass_group_debug_full_reported) return;
    jass_group_debug_full_reported = true;

    FOR_LOOP(i, level.num_groups) {
        ggroup_t const *group = level.groups[i];
        cstring_t chain = "<unknown>";
        int32_t trigger_ordinal = -1;
        uint32_t k;
        if (!group || !group->inuse) continue;
    #ifdef WC3_DEBUG_GROUPS
        live++;
    #endif
        if (i < jass_group_debug_slot_capacity) {
            if (jass_group_debug_slots[i].chain[0]) chain = jass_group_debug_slots[i].chain;
            trigger_ordinal = jass_group_debug_slots[i].trigger_ordinal;
        }
        for (k = 0; k < num_counts; k++) {
            if (counts[k].trigger_ordinal == trigger_ordinal && !strcmp(counts[k].chain, chain)) {
                counts[k].live++;
                break;
            }
        }
        if (k == num_counts && num_counts < JASS_GROUP_DEBUG_MAX_STATS) {
            snprintf(counts[num_counts].chain, sizeof(counts[num_counts].chain), "%s", chain);
            counts[num_counts].trigger_ordinal = trigger_ordinal;
            counts[num_counts].live = 1;
            num_counts++;
        }
    }

#ifdef WC3_DEBUG_GROUPS
    fprintf(stderr,
            "WC3_GROUP_DEBUG allocation-failed failing_creator=\"%s\" failing_trigger=%ld failing_chain=\"%s\" live=%u highwater=%u capacity=%u chains=%u\n",
            failing_creator ? failing_creator : "<unknown>", (long)failing_trigger,
            failing_chain && *failing_chain ? failing_chain : "<unknown>",
            (unsigned)live, (unsigned)level.num_groups,
            (unsigned)level.group_capacity, (unsigned)num_counts);
    FOR_LOOP(i, num_counts) {
        jass_group_debug_stat_t *stat = G_FindJassGroupDebugStat(counts[i].chain, counts[i].trigger_ordinal, false);
        uint32_t allocations = stat ? stat->allocations : counts[i].live;
        uint32_t frees = stat ? stat->frees : 0;
        fprintf(stderr,
                "WC3_GROUP_DEBUG chain trigger=%ld live=%u allocated=%u freed=%u outstanding=%u path=\"%s\"\n",
                (long)counts[i].trigger_ordinal, (unsigned)counts[i].live,
                (unsigned)allocations, (unsigned)frees,
                (unsigned)(allocations >= frees ? allocations - frees : 0),
                counts[i].chain);
    }
#endif
}

static bool G_GrowJassGroupRegistry(uint32_t count) {
    ggroup_t **groups;
    uint32_t capacity;
#ifdef WC3_DEBUG_GROUPS
    uint32_t const old_capacity = level.group_capacity;
#endif

    if (count <= level.group_capacity) return true;
    capacity = level.group_capacity ? level.group_capacity : JASS_GROUP_INITIAL_CAPACITY;
    while (capacity < count) {
        if (capacity > UINT32_MAX / 2) { capacity = count; break; }
        capacity *= 2;
    }
    if ((size_t)capacity > (size_t)-1 / sizeof(*groups)) return false;
    groups = gi.MemAlloc((size_t)capacity * sizeof(*groups));
    if (!groups) return false;
    memset(groups, 0, (size_t)capacity * sizeof(*groups));
    if (level.groups) {
        memcpy(groups, level.groups, (size_t)level.num_groups * sizeof(*groups));
        gi.MemFree(level.groups);
    }
    level.groups = groups;
    level.group_capacity = capacity;
    if (jass_group_debug_slots) (void)G_EnsureJassGroupDebugSlots(capacity);
#ifdef WC3_DEBUG_GROUPS
    if (G_JassGroupDebugEnabled()) {
        fprintf(stderr, "WC3_GROUP_DEBUG grow old_capacity=%u new_capacity=%u highwater=%u\n",
                (unsigned)old_capacity, (unsigned)capacity, (unsigned)level.num_groups);
    }
#endif
    return true;
}

bool G_EnsureJassGroupSlots(uint32_t count) {
    if (!G_GrowJassGroupRegistry(count)) return false;
    while (level.num_groups < count) {
        ggroup_t *group = gi.MemAlloc(sizeof(*group));
        if (!group) return false;
        memset(group, 0, sizeof(*group));
        group->handle_id = level.num_groups;
        level.groups[level.num_groups++] = group;
    }
    return true;
}

ggroup_t *G_AllocJassGroup(void) {
    ggroup_t *group;

    for (uint32_t i = level.first_free_group; i < level.num_groups; i++) {
        group = level.groups[i];
        if (group && !group->inuse) {
            uint32_t const handle_id = group->handle_id;
            memset(group, 0, sizeof(*group));
            group->handle_id = handle_id;
            group->inuse = true;
            level.first_free_group = i + 1;
            while (level.first_free_group < level.num_groups &&
                   level.groups[level.first_free_group]->inuse) level.first_free_group++;
            if (i < jass_group_debug_slot_capacity) {
                memset(&jass_group_debug_slots[i], 0, sizeof(jass_group_debug_slots[i]));
                jass_group_debug_slots[i].trigger_ordinal = -1;
            }
            jass_group_debug_full_reported = false;
            return group;
        }
    }
    level.first_free_group = level.num_groups;
    if (!G_EnsureJassGroupSlots(level.num_groups + 1)) return NULL;
    group = level.groups[level.num_groups - 1];
    group->inuse = true;
    level.first_free_group = level.num_groups;
    jass_group_debug_full_reported = false;
    return group;
}

void G_FreeJassGroup(ggroup_t *group) {
    uint32_t index;
    jass_group_debug_stat_t *stat = NULL;
    if (!G_JassGroupValid(group) || !G_JassGroupIndex(group, &index)) return;
    if (index < jass_group_debug_slot_capacity) {
        jass_group_debug_slot_t *slot = &jass_group_debug_slots[index];
        stat = G_FindJassGroupDebugStat(slot->chain, slot->trigger_ordinal, false);
        if (stat) stat->frees++;
        memset(slot, 0, sizeof(*slot));
        slot->trigger_ordinal = -1;
    }
    memset(group, 0, sizeof(*group));
    group->handle_id = index;
    if (index < level.first_free_group) level.first_free_group = index;
    jass_group_debug_full_reported = false;
}

void G_ClearJassGroupRegistry(void) {
    if (level.groups) {
        FOR_LOOP(i, level.num_groups) if (level.groups[i]) gi.MemFree(level.groups[i]);
        gi.MemFree(level.groups);
    }
    level.groups = NULL;
    level.num_groups = 0;
    level.group_capacity = 0;
    level.first_free_group = 0;
    G_ResetJassGroupDebug();
}

void G_ClearRegionRegistry(void) {
    memset(level.regions, 0, sizeof(level.regions));
    level.num_regions = 0;
}

region_t *G_RegionFromHandle(handle_t handle) {
    uint32_t slot, generation;
    region_t *region;
    if (!G_RegionHandleParts(handle, &slot, &generation) || slot >= level.num_regions) return NULL;
    region = &level.regions[slot];
    return region->inuse && region->generation == generation ? region : NULL;
}

bool G_RegionHandleParts(handle_t handle, uint32_t *slot, uint32_t *generation) {
    uintptr_t token = (uintptr_t)handle;
    uint32_t const index = (uint32_t)((token & (((uintptr_t)1 << REGION_TOKEN_SLOT_BITS) - 1)) >> 2);
    uintptr_t const gen = token >> REGION_TOKEN_SLOT_BITS;
    if ((token & 3) != 1 || index >= MAX_REGIONS || gen > REGION_HANDLE_GENERATION_MAX) return false;
    if (slot) *slot = index;
    if (generation) *generation = (uint32_t)gen;
    return true;
}

handle_t G_RegionHandle(uint32_t slot) {
    region_t *region;
    if (slot >= level.num_regions || slot >= MAX_REGIONS) return NULL;
    region = &level.regions[slot];
    if (!region->inuse) return NULL;
    return (handle_t)((region->generation << REGION_TOKEN_SLOT_BITS) | ((uintptr_t)slot << 2) | 1);
}

event_t *G_EventFromHandle(handle_t handle) {
    uintptr_t token = (uintptr_t)handle, base = (uintptr_t)level.events.handlers;
    if ((token & 3) == 3) {
        uint32_t slot, generation;
        event_t *event;
        if (!G_EventHandleParts(handle, &slot, &generation)) return NULL;
        event = &level.events.handlers[slot];
        return event->inuse && (event->type == EVENT_GAME_ENTER_REGION || event->type == EVENT_GAME_LEAVE_REGION) &&
            event->handle_generation == generation ? event : NULL;
    }
    if (token < base || token >= base + sizeof(level.events.handlers) ||
        (token - base) % sizeof(*level.events.handlers)) return NULL;
    {
        event_t *event = handle;
        return event->inuse ? event : NULL;
    }
}

bool G_EventHandleParts(handle_t handle, uint32_t *slot, uint32_t *generation) {
    uintptr_t token = (uintptr_t)handle;
    uint32_t const index = (uint32_t)((token & (((uintptr_t)1 << EVENT_TOKEN_SLOT_BITS) - 1)) >> 2);
    uintptr_t const gen = token >> EVENT_TOKEN_SLOT_BITS;
    if ((token & 3) != 3 || index >= MAX_EVENTS || gen > EVENT_HANDLE_GENERATION_MAX) return false;
    if (slot) *slot = index;
    if (generation) *generation = (uint32_t)gen;
    return true;
}

handle_t G_EventHandle(event_t *event) {
    uint32_t slot;
    if (!event) return NULL;
    if (event->type != EVENT_GAME_ENTER_REGION && event->type != EVENT_GAME_LEAVE_REGION) return event;
    slot = (uint32_t)(event - level.events.handlers);
    return (handle_t)((event->handle_generation << EVENT_TOKEN_SLOT_BITS) | ((uintptr_t)slot << 2) | 3);
}

trigger_t *G_AllocJassTrigger(void) {
    if (level.num_triggers >= MAX_TRIGGERS) return NULL;
    trigger_t *trigger = &level.triggers[level.num_triggers++];
    memset(trigger, 0, sizeof(*trigger)); return trigger;
}

/* Original05fcf0 registers every endpoint cell;05fa10 queries floor of the
 * same world-to-fine transform. A region is a union of cells, not rectangles
 * tested against the continuously predicted world position. */
bool G_RegionContains(region_t const *region, vec2_t const *point) {
    box2_t const bounds=CM_GetWorldBounds();
    if(point->x<bounds.min.x || point->y<bounds.min.y || point->x>=bounds.max.x || point->y>=bounds.max.y)return false;
    int32_t const x=(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_grid_coordinate(point->x,bounds.min.x,32))));
    int32_t const y=(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_grid_coordinate(point->y,bounds.min.y,32))));
    FOR_LOOP(i,region->num_rects) {
        box2_t const *r=region->rects+i;
        int32_t min_x=(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_grid_coordinate(r->min.x,bounds.min.x,32))));
        int32_t min_y=(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_grid_coordinate(r->min.y,bounds.min.y,32))));
        int32_t max_x=(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_grid_coordinate(r->max.x,bounds.min.x,32))));
        int32_t max_y=(int32_t)wc3_int_bits(wc3_floor_bits(wc3_float_bits(wc3_grid_coordinate(r->max.y,bounds.min.y,32))));
        if(x>=min_x && x<=max_x && y>=min_y && y<=max_y)return true;
    }
    return false;
}

quest_t *G_MakeQuest(void) {
    FOR_LOOP(i, MAX_QUESTS) if (!level.quests[i].inuse) {
    quest_t *quest = &level.quests[i];
    memset(quest, 0, sizeof(*quest));
    /* CreateQuestBJ does not call QuestSetEnabled; Warcraft quests are usable
     * immediately unless a map explicitly disables them. */
    quest->inuse = true; quest->enabled = true;
    return quest;
    }
    fprintf(stderr, "WC3: quest slot limit %u reached\n", MAX_QUESTS);
    return NULL;
}

static void DeleteQuestItem(questItem_t *questitem) {
    free(questitem->description);
    memset(questitem, 0, sizeof(*questitem));
}

static void DeleteQuest(quest_t *quest) {
    FOR_LOOP(i, MAX_QUESTITEMS) if (quest->items[i].inuse) DeleteQuestItem(&quest->items[i]);
    free(quest->description);
    free(quest->title);
    free(quest->iconPath);
    memset(quest, 0, sizeof(*quest));
}

void G_RemoveQuest(quest_t *quest) {
    if (quest && quest->inuse) DeleteQuest(quest);
}

void G_InitPlayerAlliances(mapInfo_t const *mapinfo) {
    uint32_t const passive = 1u << ALLIANCE_PASSIVE;

    memset(level.alliances, 0, sizeof(level.alliances));

    /* Warcraft's reserved Neutral Passive owner is mutually passive-allied
     * with every player.  Keep this in the normal directional alliance table
     * so triggers can subsequently revoke/change the relation instead of
     * relying on owner-ID special cases in every consumer. */
    FOR_LOOP(player, MAX_PLAYERS) {
        /* Retail help queries intersect both directional diplomacy masks,
         * including their own-owner bit. Keep it mutable like other relations. */
        level.alliances[player][player] |= (1u<<ALLIANCE_HELP_REQUEST)|(1u<<ALLIANCE_HELP_RESPONSE);
        level.alliances[player][PLAYER_NEUTRAL_PASSIVE] |= passive;
        level.alliances[PLAYER_NEUTRAL_PASSIVE][player] |= passive;
    }

    /* Ordinary W3I player slots controlled by MAP_CONTROL_NEUTRAL receive the
     * same bilateral passive alliance defaults.  The four reserved neutral
     * slots have distinct Warcraft semantics; only Neutral Passive is covered
     * above, so do not turn Neutral Aggressive/Victim/Extra into allies here. */
    if (!mapinfo) return;
    FOR_LOOP(neutral, PLAYER_NEUTRAL_AGGRESSIVE) {
        if (mapinfo->players[neutral].playerType != kPlayerTypeNeutral) continue;
        FOR_LOOP(player, MAX_PLAYERS) {
            level.alliances[neutral][player] |= passive;
            level.alliances[player][neutral] |= passive;
        }
    }
}

void G_SetPlayerAlliance(player_t const *p1, player_t const *p2, PLAYERALLIANCE type, bool value) {
    uint32_t const flag = 1u << type;
    uint32_t const before = level.alliances[p1->number][p2->number];

    if (value) level.alliances[p1->number][p2->number] |= flag;
    else level.alliances[p1->number][p2->number] &= ~flag;

    /* Warcraft alliance state is directional: SetPlayerAlliance(source, other, ...)
     * changes only source -> other. Consumers such as fog and shared command
     * authority already read the matrix in that direction. */
    if ((type == ALLIANCE_PASSIVE || type == ALLIANCE_SHARED_CONTROL) &&
        before != level.alliances[p1->number][p2->number]) {
        G_InvalidateAllUnitShortcuts();
    }
}

bool G_GetPlayerAlliance(player_t const *p1, player_t const *p2, PLAYERALLIANCE type) {
    return level.alliances[p1->number][p2->number] & (1 << type);
}

bool G_PlayerTreatsPlayerAsAlly(uint32_t source, uint32_t other) {
    if (source >= MAX_PLAYERS || other >= MAX_PLAYERS) return false;
    if (source == other) return true;
    return (level.alliances[source][other] & (1u << ALLIANCE_PASSIVE)) != 0;
}
