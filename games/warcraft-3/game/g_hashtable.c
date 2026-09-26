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
    if (table->entries) gi.MemFree(table->entries);
    memset(table, 0, sizeof(*table));
}

void G_ClearHashtableRegistry(void) {
    FOR_LOOP(i, MAX_HASHTABLES) {
        if (level.hashtables[i].entries) gi.MemFree(level.hashtables[i].entries);
        memset(&level.hashtables[i], 0, sizeof(level.hashtables[i]));
    }
}
