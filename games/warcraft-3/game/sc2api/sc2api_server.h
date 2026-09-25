#ifndef WC3_SC2API_SERVER_H
#define WC3_SC2API_SERVER_H

#include "sc2api_transport.h"

BOOL WC3_SC2API_ExternalActive(void);
BOOL WC3_SC2API_ExternalOwnsClock(void);
DWORD WC3_SC2API_ExternalFrame(void);
BOOL WC3_SC2API_ExternalCanAdvance(void);
void WC3_SC2API_ExternalStepComplete(DWORD steps);
void WC3_SC2API_ExternalMapComplete(LPCSTR map, BOOL success);
void WC3_SC2API_RecordDeath(LPCEDICT ent);
void WC3_SC2API_ServerShutdown(void);

#endif
