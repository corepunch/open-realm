#ifndef WC3_ALERTS_H
#define WC3_ALERTS_H

#include "common/shared.h"

#define WC3_RECENT_ALERT_COUNT 8
#define WC3_ACTIVE_MINIMAP_PINGS 16

#define WC3_MINIMAP_PING_REMEMBER      (1u << 0)
#define WC3_MINIMAP_PING_EXTRA_EFFECTS (1u << 1)

typedef struct {
    VECTOR2 position;
    FLOAT duration;
    COLOR32 color;
    DWORD flags;
    char model[MAX_PATHLEN];
} wc3MinimapPing_t;

#endif
