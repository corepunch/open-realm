#ifndef WOW_QUEST_TRACKER_H
#define WOW_QUEST_TRACKER_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Tracking follows quest-log slots, including compaction after removal. */
static inline void Wow_QuestTrackerRemove(bool *tracked, uint32_t count, uint32_t slot) {
    if (slot >= count) return;
    memmove(tracked + slot, tracked + slot + 1, (count - slot - 1) * sizeof(*tracked));
    tracked[count - 1] = false;
}

static inline bool Wow_QuestTrackerToggle(bool *tracked, uint32_t count, uint32_t slot) {
    if (slot >= count) return false;
    tracked[slot] = !tracked[slot];
    return true;
}

#endif
