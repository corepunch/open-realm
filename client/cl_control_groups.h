#ifndef cl_control_groups_h
#define cl_control_groups_h

#include "common/shared.h"

/* Append entity IDs to a control-group array while preserving existing order.
 * Existing entries win when capacity is reached; incoming duplicates are
 * ignored. Returns the resulting number of stored IDs. */
uint32_t CL_ControlGroupAppendUnique(uint32_t *group, uint32_t count, uint32_t capacity,
                                  uint32_t const *ids, uint32_t num_ids);

void CL_ControlGroupsReset(void);
void CL_ControlGroupsInit(void);

#endif
