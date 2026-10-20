#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Original6b8cc0 consumes the resolved head and selection predicates before
 *6b93a0 chooses Shift append, transient dispatch, fallback or replacement. */
static inline bool S_PlayerOrderFallback(uint32_t command, bool head,
        bool protected_cast, bool working, bool preserve, bool instant,
        bool subgroup_only, bool all_working, bool primary_subgroup) {
    if (instant || !head) return false;
    switch (command) {
    case 0xd0004: /* Stop */
    case 0xd0005: /* Suspension */
    case 0xd0019: /* Hold Position */
    case 0xd00a3: /* Ambush */
        return false;
    default:
        return protected_cast || (working && !preserve && !all_working &&
                                   (!subgroup_only || !primary_subgroup));
    }
}
