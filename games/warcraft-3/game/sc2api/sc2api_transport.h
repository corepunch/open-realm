#ifndef WC3_SC2API_TRANSPORT_H
#define WC3_SC2API_TRANSPORT_H

#include "sc2api_wire.h"

typedef void (*wc3Sc2TransportMessageFn)(BYTE const *data, DWORD size);

BOOL WC3_SC2API_TransportPoll(wc3Sc2TransportMessageFn on_message);
BOOL WC3_SC2API_TransportSend(BYTE const *data, DWORD size);
BOOL WC3_SC2API_TransportConnected(void);
void WC3_SC2API_TransportShutdown(void);

#endif
