#include "cl_control_groups.h"
#include "client.h"

#include <string.h>

DWORD CL_ControlGroupAppendUnique(DWORD *group, DWORD count, DWORD capacity,
                                  DWORD const *ids, DWORD num_ids) {
    if (!group || capacity == 0) return 0;
    if (count > capacity) count = capacity;
    if (!ids) return count;

    FOR_LOOP(i, num_ids) {
        BOOL duplicate = false;
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
