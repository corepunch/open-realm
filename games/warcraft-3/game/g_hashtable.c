#include "g_local.h"

static LONG hashtable_index(LPCHASHTABLE table) {
    uintptr_t ptr = (uintptr_t)table, base = (uintptr_t)level.hashtables;
    size_t span = sizeof(level.hashtables);
    if (!table || ptr < base || ptr >= base + span ||
        (ptr - base) % sizeof(*table) != 0 || !table->inuse) return -1;
    return (LONG)((ptr - base) / sizeof(*table));
}

BOOL G_HashtableIndex(LPCHASHTABLE table, DWORD *index) {
    LONG id = hashtable_index(table);
    if (id < 0) return false;
    if (index) *index = (DWORD)id;
    return true;
}

BOOL G_HashtableReserve(LPHASHTABLE table, DWORD need) {
    hashtableEntry_t *next;
    DWORD cap;
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

LPHASHTABLE G_AllocHashtable(void) {
    FOR_LOOP(i, MAX_HASHTABLES) if (!level.hashtables[i].inuse) {
        LPHASHTABLE table = &level.hashtables[i];
        memset(table, 0, sizeof(*table));
        table->inuse = true;
        return table;
    }
    fprintf(stderr, "InitHashtable: hashtable registry is full (%u)\n", (unsigned)MAX_HASHTABLES);
    return NULL;
}

void G_FreeHashtable(LPHASHTABLE table) {
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
