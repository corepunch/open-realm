#ifndef WC3_SC2API_SERVER_H
#define WC3_SC2API_SERVER_H

#include "sc2api_transport.h"

bool WC3_SC2API_ExternalActive(void);
bool WC3_SC2API_ExternalOwnsClock(void);
uint32_t WC3_SC2API_ExternalFrame(void);
bool WC3_SC2API_ExternalCanAdvance(void);
void WC3_SC2API_ExternalStepComplete(uint32_t steps);
void WC3_SC2API_ExternalMapComplete(cstring_t map, bool success);
void WC3_SC2API_RecordDeath(edict_t const *ent);
void WC3_SC2API_ServerShutdown(void);

#endif
