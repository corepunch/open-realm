#include "cl_control_groups.h"
#include "client.h"

#include <string.h>

uint32_t CL_ControlGroupAppendUnique(uint32_t *group, uint32_t count, uint32_t capacity,
                                  uint32_t const *ids, uint32_t num_ids) {
    if (!group || capacity == 0) return 0;
    if (count > capacity) count = capacity;
    if (!ids) return count;

    FOR_LOOP(i, num_ids) {
        bool duplicate = false;
        FOR_LOOP(j, count) {
            if (group[j] == ids[i]) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && count < capacity) {
            group[count++] = ids[i];
        }
    }
    return count;
}

void CL_ControlGroupsReset(void) {
    memset(cl.groups, 0, sizeof(cl.groups));
    cl.group_last = MAX_CONTROL_GROUPS;
    cl.group_last_ms = 0;
}
