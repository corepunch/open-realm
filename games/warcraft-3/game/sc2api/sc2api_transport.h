#ifndef WC3_SC2API_TRANSPORT_H
#define WC3_SC2API_TRANSPORT_H

#include "sc2api_wire.h"

typedef void (*wc3Sc2TransportMessageFn)(uint8_t const *data, uint32_t size);

bool WC3_SC2API_TransportPoll(wc3Sc2TransportMessageFn on_message);
bool WC3_SC2API_TransportSend(uint8_t const *data, uint32_t size);
bool WC3_SC2API_TransportConnected(void);
void WC3_SC2API_TransportShutdown(void);

#endif
