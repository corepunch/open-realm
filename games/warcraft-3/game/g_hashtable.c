#include "g_local.h"

static int32_t hashtable_index(hashtable_t const *table) {
    uintptr_t ptr = (uintptr_t)table, base = (uintptr_t)level.hashtables;
    size_t span = sizeof(level.hashtables);
    if (!table || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*table) != 0 || !table->inuse) return -1;
    return (int32_t)((ptr - base) / sizeof(*table));
}

bool G_HashtableIndex(hashtable_t const *table, uint32_t *index) {
    int32_t id = hashtable_index(table);
    if (id < 0) return false;
    if (index) *index = (uint32_t)id;
    return true;
}

/* Linear-probe slot for one typed (parent, child) key; 0 marks an empty slot. */
static uint32_t hashtable_slot(hashtable_t const *table, int32_t parent, int32_t child, hashtableSlotType_t type) {
    uint32_t h = (uint32_t)parent * 0x9e3779b1u ^ ((uint32_t)child + 0x7f4a7c15u) * 0x85ebca77u ^ (uint32_t)type * 0xc2b2ae3du;
    h ^= h >> 15;
    for (uint32_t i = h & table->slot_mask;; i = (i + 1) & table->slot_mask) {
        uint32_t at = table->slots[i];
        if (!at) return i;
        hashtableEntry_t const *e = table->entries + at - 1;
        if (e->parent == parent && e->child == child && e->type == type) return i;
    }
}

/* The index stays at most half full, so probes terminate and remain short. */
bool G_HashtableRebuildIndex(hashtable_t *table) {
    uint32_t count = 16;
    while (count < table->num_entries * 2) count *= 2;
    uint32_t *slots = gi.MemAlloc(count * sizeof(*slots));
    if (!slots) return false;
    memset(slots, 0, count * sizeof(*slots));
    if (table->slots) gi.MemFree(table->slots);
    table->slots = slots; table->slot_mask = count - 1;
    FOR_LOOP(i, table->num_entries) {
        hashtableEntry_t const *e = table->entries + i;
        table->slots[hashtable_slot(table, e->parent, e->child, e->type)] = i + 1;
    }
    return true;
}

bool G_HashtableReserve(hashtable_t *table, uint32_t need) {
    hashtableEntry_t *next;
    uint32_t cap;
    if (!table || !table->inuse) return false;
    if (need <= table->capacity) return true;
    if (need > MAX_HASHTABLE_ENTRIES) {
        fprintf(stderr, "InitHashtable: entry limit %u reached\n", (unsigned)MAX_HASHTABLE_ENTRIES);
        return false;
    }
    cap = table->capacity ? table->capacity : 16;
    while (cap < need) cap = MIN(cap * 2, MAX_HASHTABLE_ENTRIES);
    next = gi.MemAlloc(cap * sizeof(*next));
    if (!next) return false;
    if (table->entries) {
        memcpy(next, table->entries, table->num_entries * sizeof(*next));
        gi.MemFree(table->entries);
    }
    table->entries = next;
    table->capacity = cap;
    return true;
}

hashtableEntry_t *G_HashtableFind(hashtable_t *table, int32_t parent, int32_t child, hashtableSlotType_t type) {
    if (!table || !table->inuse || !table->slots) return NULL;
    uint32_t at = table->slots[hashtable_slot(table, parent, child, type)];
    return at ? table->entries + at - 1 : NULL;
}

/* The previous flat scan made every Save/Load O(n) and capped tables at 65536 slots. */
hashtableEntry_t *G_HashtableEnsure(hashtable_t *table, int32_t parent, int32_t child, hashtableSlotType_t type) {
    hashtableEntry_t *e = G_HashtableFind(table, parent, child, type);
    if (e) return e;
    if (!table || !G_HashtableReserve(table, table->num_entries + 1)) return NULL;
    if ((table->num_entries + 1) * 2 > table->slot_mask + 1 || !table->slots)
        if (!G_HashtableRebuildIndex(table)) return NULL;
    e = table->entries + table->num_entries;
    memset(e, 0, sizeof(*e));
    e->parent = parent; e->child = child; e->type = type;
    table->slots[hashtable_slot(table, parent, child, type)] = ++table->num_entries;
    return e;
}

/* Swap-remove keeps the entry array dense. Reinserting the cluster after the hole keeps
 * every probe chain contiguous; the moved last entry's slot is repointed to its index. */
void G_HashtableRemoveAt(hashtable_t *table, uint32_t index) {
    if (!table || index >= table->num_entries) return;
    hashtableEntry_t const *gone = table->entries + index;
    uint32_t hole = hashtable_slot(table, gone->parent, gone->child, gone->type);
    table->slots[hole] = 0;
    /* Reinsert the following cluster so no lookup stops at the new hole. */
    for (uint32_t i = (hole + 1) & table->slot_mask; table->slots[i]; i = (i + 1) & table->slot_mask) {
        uint32_t at = table->slots[i];
        hashtableEntry_t const *e = table->entries + at - 1;
        table->slots[i] = 0;
        table->slots[hashtable_slot(table, e->parent, e->child, e->type)] = at;
    }
    uint32_t last = table->num_entries - 1;
    if (index != last) {
        hashtableEntry_t const *moved = table->entries + last;
        table->slots[hashtable_slot(table, moved->parent, moved->child, moved->type)] = index + 1;
        table->entries[index] = table->entries[last];
    }
    table->num_entries = last;
}

void G_HashtableFlush(hashtable_t *table) {
    if (!table || !table->inuse) return;
    if (table->entries) gi.MemFree(table->entries);
    if (table->slots) gi.MemFree(table->slots);
    table->entries = NULL; table->slots = NULL;
    table->num_entries = table->capacity = table->slot_mask = 0;
}

hashtable_t *G_AllocHashtable(void) {
    FOR_LOOP(i, MAX_HASHTABLES) if (!level.hashtables[i].inuse) {
        hashtable_t *table = &level.hashtables[i];
        memset(table, 0, sizeof(*table));
        table->inuse = true;
        return table;
    }
    fprintf(stderr, "InitHashtable: hashtable registry is full (%u)\n", (unsigned)MAX_HASHTABLES);
    return NULL;
}

void G_FreeHashtable(hashtable_t *table) {
    if (hashtable_index(table) < 0) return;
    G_HashtableFlush(table);
    memset(table, 0, sizeof(*table));
}

void G_ClearHashtableRegistry(void) {
    FOR_LOOP(i, MAX_HASHTABLES) {
        if (level.hashtables[i].entries) gi.MemFree(level.hashtables[i].entries);
        if (level.hashtables[i].slots) gi.MemFree(level.hashtables[i].slots);
        memset(&level.hashtables[i], 0, sizeof(level.hashtables[i]));
    }
}
