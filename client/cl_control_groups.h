#ifndef cl_control_groups_h
#define cl_control_groups_h

#include "common/shared.h"

/* Append entity IDs to a control-group array while preserving existing order.
 * Existing entries win when capacity is reached; incoming duplicates are
 * ignored. Returns the resulting number of stored IDs. */
DWORD CL_ControlGroupAppendUnique(DWORD *group, DWORD count, DWORD capacity,
                                  DWORD const *ids, DWORD num_ids);

void CL_ControlGroupsReset(void);

#endif
