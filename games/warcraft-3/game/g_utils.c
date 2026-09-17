#include "g_local.h"

typedef struct {
    LPEDICT ent;
    DWORD spawn_time;
} deferred_free_t;

static deferred_free_t deferred_frees[MAX_ENTITIES];
static DWORD deferred_free_count;

/* Drop a queued removal when another lifecycle path frees the same edict first. */
static void G_CancelDeferredFree(LPEDICT ent) {
    FOR_LOOP(i, deferred_free_count) {
        if (deferred_frees[i].ent != ent) continue;
        deferred_frees[i] = deferred_frees[--deferred_free_count];
        i--;
    }
}

/* Identify hidden-but-live edicts whose JASS handles must already behave as null. */
BOOL G_IsDeferredFree(LPCEDICT ent) {
    if (!ent) return false;
    FOR_LOOP(i, deferred_free_count)
        if (deferred_frees[i].ent == ent && deferred_frees[i].spawn_time == ent->spawn_time) return true;
    return false;
}

/* Remove an entity from every live JASS group before its handle becomes stale. */
static void G_RemoveEntityFromJassGroups(LPEDICT ent) {
    FOR_LOOP(i, level.num_groups) {
        ggroup_t *group = level.groups[i];
        if (!group->inuse) continue;
        for (DWORD k = 0; k < group->num_units;) {
            if (group->units[k] != ent) { k++; continue; }
            for (DWORD n = k + 1; n < group->num_units; n++) group->units[n - 1] = group->units[n];
            group->num_units--;
        }
    }
}

void G_SetPlayerText(LPGAMECLIENT client, PLAYERTEXT index, LPCSTR text) {
    DWORD cursor;

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

void G_FreeEdict(LPEDICT ent) {
    if (!ent) return;
    G_CancelDeferredFree(ent);
    S_UnitAbilityEvent(ent, A_UNIT_REMOVE);
    /* Direct JASS RemoveUnit must release transient construction/upgrade state
     * before the edict is cleared. Forced removal does not grant a player
     * cancellation refund. */
    if (G_BuildingUpgradeActive(ent)) G_StopBuildingUpgrade(ent, false);
    if (ent->construction.active) G_StopConstruction(ent);
    if (ent->mineoverlay.parent || ent->think == blight_mine_think) S_MineOverlayRelease(ent);
    if (S_AcolyteHarvestIsActive(ent)) S_AcolyteHarvestRelease(ent);
    S_CargoReleaseUnit(ent);
    if (ent->cargo.count > 0) cargo_drop_all(ent);
    if (ent->buildwork.ability) S_CancelRepair(ent);
    /* Removed units cannot remain in JASS groups: save files require every group member to resolve to a live edict. */
    FOR_LOOP(i, level.num_groups) {
        ggroup_t *group = level.groups[i];
        if (!group->inuse) continue;
        for (DWORD k = 0; k < group->num_units;) {
            if (group->units[k] != ent) { k++; continue; }
            for (DWORD n = k + 1; n < group->num_units; n++) group->units[n - 1] = group->units[n];
            group->num_units--;
        }
    }
    G_UnregisterGroundSurface(ent);
    G_InvalidateUnitShortcutsForUnit(ent);
    G_InvalidateRallyTarget(ent);
    if (ent->revival.reviving) G_CancelHeroRevive(ent->revival.producer, ent);
    if (ent->training) G_ClearTrainingQueueFood(ent);
    else { G_CancelHeroRevives(ent); G_CancelTrainingQueue(ent, true); }
    G_ClearUnitFood(ent);
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    S_GoldMineReleaseWorker(ent);
    gi.UnlinkEntity(ent);
    memset(ent, 0, sizeof(*ent));
    ent->freetime = level.time;
}

/* Match Warsmash RemoveUnit: hide now, then retire the handle after this simulation tick. */
void G_DeferFreeEdict(LPEDICT ent) {
    if (!ent || !ent->inuse) return;
    FOR_LOOP(i, deferred_free_count)
        if (deferred_frees[i].ent == ent && deferred_frees[i].spawn_time == ent->spawn_time) return;
    if (deferred_free_count >= MAX_ENTITIES) {
        fprintf(stderr, "WC3: deferred unit removal queue exhausted\n");
        return;
    }
    ent->s.renderfx |= RF_HIDDEN;
    if (ent->s.flags & EF_FOW_BLOCKER) G_FowMarkBlockersDirty();
    G_InvalidateCommands(G_GetPlayerClientByNumber(ent->s.player));
    G_RemoveEntityFromJassGroups(ent);
    deferred_frees[deferred_free_count++] = (deferred_free_t){ .ent = ent, .spawn_time = ent->spawn_time };
}

/* Complete queued JASS removals after entity iteration and before the next snapshot. */
void G_RunDeferredFrees(void) {
    while (deferred_free_count) {
        deferred_free_t pending = deferred_frees[--deferred_free_count];
        if (pending.ent->inuse && pending.ent->spawn_time == pending.spawn_time) G_FreeEdict(pending.ent);
    }
}

void G_ResetDeferredFrees(void) { deferred_free_count = 0; }

LPEVENT G_MakeEvent(EVENTTYPE type) {
    FOR_LOOP(i, MAX_EVENTS) if (!level.events.handlers[i].inuse) {
        LPEVENT evt = &level.events.handlers[i];
        memset(evt, 0, sizeof(*evt)); evt->inuse = true; evt->type = type; return evt;
    }
    fprintf(stderr, "WC3: event slot limit %u reached\n", MAX_EVENTS);
    return NULL;
}

#define JASS_GROUP_DEBUG_CHAIN_SIZE 256 // characters; bounds one captured JASS call chain for group diagnostics
#define JASS_GROUP_DEBUG_MAX_STATS 128 // entries; bounds distinct group-debug chains retained per map

typedef struct {
    char chain[JASS_GROUP_DEBUG_CHAIN_SIZE];
    LONG trigger_ordinal;
    DWORD allocations;
    DWORD frees;
} jass_group_debug_stat_t;

typedef struct {
    LPCSTR creator;
    char chain[JASS_GROUP_DEBUG_CHAIN_SIZE];
    LONG trigger_ordinal;
} jass_group_debug_slot_t;

static jass_group_debug_slot_t *jass_group_debug_slots;
static DWORD jass_group_debug_slot_capacity;
static jass_group_debug_stat_t jass_group_debug_stats[JASS_GROUP_DEBUG_MAX_STATS];
static DWORD jass_group_debug_num_stats;
static BOOL jass_group_debug_full_reported;

BOOL G_JassGroupDebugEnabled(void) {
    return gi.CvarString && atoi(gi.CvarString("wc3_group_debug", "0")) != 0;
}

static BOOL G_EnsureJassGroupDebugSlots(DWORD count) {
    jass_group_debug_slot_t *slots;
    DWORD capacity;

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

static jass_group_debug_stat_t *G_FindJassGroupDebugStat(LPCSTR chain, LONG trigger_ordinal, BOOL create) {
    LPCSTR key = chain && *chain ? chain : "<unknown>";
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

BOOL G_JassGroupIndex(ggroup_t const *group, DWORD *index) {
    DWORD id;
    if (!group || !level.groups) return false;
    id = group->handle_id;
    if (id >= level.num_groups || level.groups[id] != group) return false;
    if (index) *index = id;
    return true;
}

ggroup_t *G_JassGroupByIndex(DWORD index) {
    return index < level.num_groups && level.groups ? level.groups[index] : NULL;
}

BOOL G_JassGroupValid(ggroup_t const *group) {
    return G_JassGroupIndex(group, NULL) && group->inuse;
}

BOOL G_QuestValid(QUEST const *quest) {
    return quest && quest >= level.quests && quest < level.quests + MAX_QUESTS && quest->inuse;
}

BOOL G_QuestItemValid(QUESTITEM const *item) {
    FOR_LOOP(i, MAX_QUESTS) if (level.quests[i].inuse && item >= level.quests[i].items &&
                                item < level.quests[i].items + MAX_QUESTITEMS)
        return item->inuse;
    return false;
}

void G_SetJassGroupDebugContext(ggroup_t *group, LPCSTR creator, LPCSTR chain, LONG trigger_ordinal) {
    DWORD index;
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

void G_SetJassGroupDebugCreator(ggroup_t *group, LPCSTR creator) {
    G_SetJassGroupDebugContext(group, creator, creator, -1);
}

LPCSTR G_GetJassGroupDebugCreator(ggroup_t const *group) {
    DWORD index;
    if (!G_JassGroupValid(group) || !G_JassGroupIndex(group, &index) ||
        index >= jass_group_debug_slot_capacity) return NULL;
    return jass_group_debug_slots[index].creator;
}

LPCSTR G_GetJassGroupDebugChain(ggroup_t const *group) {
    DWORD index;
    LPCSTR chain;
    if (!G_JassGroupValid(group) || !G_JassGroupIndex(group, &index) ||
        index >= jass_group_debug_slot_capacity) return NULL;
    chain = jass_group_debug_slots[index].chain;
    return *chain ? chain : NULL;
}

LONG G_GetJassGroupDebugTrigger(ggroup_t const *group) {
    DWORD index;
    if (!G_JassGroupValid(group) || !G_JassGroupIndex(group, &index) ||
        index >= jass_group_debug_slot_capacity) return -1;
    return jass_group_debug_slots[index].trigger_ordinal;
}

void G_DumpJassGroupDebug(LPCSTR failing_creator, LPCSTR failing_chain, LONG failing_trigger) {
    typedef struct {
        char chain[JASS_GROUP_DEBUG_CHAIN_SIZE];
        LONG trigger_ordinal;
        DWORD live;
    } group_chain_count_t;
    group_chain_count_t counts[JASS_GROUP_DEBUG_MAX_STATS] = {0};
    DWORD num_counts = 0;
#ifdef WC3_DEBUG_GROUPS
    DWORD live = 0;
#endif

    if (!G_JassGroupDebugEnabled() || jass_group_debug_full_reported) return;
    jass_group_debug_full_reported = true;

    FOR_LOOP(i, level.num_groups) {
        ggroup_t const *group = level.groups[i];
        LPCSTR chain = "<unknown>";
        LONG trigger_ordinal = -1;
        DWORD k;
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
        DWORD allocations = stat ? stat->allocations : counts[i].live;
        DWORD frees = stat ? stat->frees : 0;
        fprintf(stderr,
                "WC3_GROUP_DEBUG chain trigger=%ld live=%u allocated=%u freed=%u outstanding=%u path=\"%s\"\n",
                (long)counts[i].trigger_ordinal, (unsigned)counts[i].live,
                (unsigned)allocations, (unsigned)frees,
                (unsigned)(allocations >= frees ? allocations - frees : 0),
                counts[i].chain);
    }
#endif
}

static BOOL G_GrowJassGroupRegistry(DWORD count) {
    ggroup_t **groups;
    DWORD capacity;
#ifdef WC3_DEBUG_GROUPS
    DWORD const old_capacity = level.group_capacity;
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

BOOL G_EnsureJassGroupSlots(DWORD count) {
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

    for (DWORD i = level.first_free_group; i < level.num_groups; i++) {
        group = level.groups[i];
        if (group && !group->inuse) {
            DWORD const handle_id = group->handle_id;
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
    DWORD index;
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

LPTRIGGER G_AllocJassTrigger(void) {
    if (level.num_triggers >= MAX_TRIGGERS) return NULL;
    LPTRIGGER trigger = &level.triggers[level.num_triggers++];
    memset(trigger, 0, sizeof(*trigger)); return trigger;
}

BOOL G_RegionContains(LPCREGION region, LPCVECTOR2 point) {
    FOR_LOOP(i, region->num_rects) {
        if (Box2_containsPoint(region->rects+i, point)) {
            return true;
        }
    }
    return false;
}

LPQUEST G_MakeQuest(void) {
    FOR_LOOP(i, MAX_QUESTS) if (!level.quests[i].inuse) {
    LPQUEST quest = &level.quests[i];
    memset(quest, 0, sizeof(*quest));
    /* CreateQuestBJ does not call QuestSetEnabled; Warcraft quests are usable
     * immediately unless a map explicitly disables them. */
    quest->inuse = true; quest->enabled = true;
    return quest;
    }
    fprintf(stderr, "WC3: quest slot limit %u reached\n", MAX_QUESTS);
    return NULL;
}

static void DeleteQuestItem(LPQUESTITEM questitem) {
    free(questitem->description);
    memset(questitem, 0, sizeof(*questitem));
}

static void DeleteQuest(LPQUEST quest) {
    FOR_LOOP(i, MAX_QUESTITEMS) if (quest->items[i].inuse) DeleteQuestItem(&quest->items[i]);
    free(quest->description);
    free(quest->title);
    free(quest->iconPath);
    memset(quest, 0, sizeof(*quest));
}

void G_RemoveQuest(LPQUEST quest) {
    if (quest && quest->inuse) DeleteQuest(quest);
}

void G_InitPlayerAlliances(LPCMAPINFO mapinfo) {
    DWORD const passive = 1u << ALLIANCE_PASSIVE;

    memset(level.alliances, 0, sizeof(level.alliances));

    /* Warcraft's reserved Neutral Passive owner is mutually passive-allied
     * with every player.  Keep this in the normal directional alliance table
     * so triggers can subsequently revoke/change the relation instead of
     * relying on owner-ID special cases in every consumer. */
    FOR_LOOP(player, MAX_PLAYERS) {
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

void G_SetPlayerAlliance(LPCPLAYER p1, LPCPLAYER p2, PLAYERALLIANCE type, BOOL value) {
    DWORD const flag = 1u << type;
    DWORD const before = level.alliances[p1->number][p2->number];

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

BOOL G_GetPlayerAlliance(LPCPLAYER p1, LPCPLAYER p2, PLAYERALLIANCE type) {
    return level.alliances[p1->number][p2->number] & (1 << type);
}

BOOL G_PlayerTreatsPlayerAsAlly(DWORD source, DWORD other) {
    if (source >= MAX_PLAYERS || other >= MAX_PLAYERS) return false;
    if (source == other) return true;
    return (level.alliances[source][other] & (1u << ALLIANCE_PASSIVE)) != 0;
}
