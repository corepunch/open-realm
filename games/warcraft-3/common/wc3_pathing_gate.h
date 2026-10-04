#ifndef BZ_WC3_PATHING_GATE_H
#define BZ_WC3_PATHING_GATE_H
#include <stdint.h>

#define BZ_WC3_GATE_RECORDS 256 // original CPaWarp backing; zero is reserved

/* Original04e510: first free availability byte from1 through255, else0.
 * The engine rebuilds these bytes from live ability-owned IDs, so save/load
 * and map teardown do not retain a second allocation registry. */
static inline uint32_t wc3_gate_allocate(uint8_t used[BZ_WC3_GATE_RECORDS]) {
    for (uint32_t id=1; id<BZ_WC3_GATE_RECORDS; id++)
        if (!used[id]) { used[id]=1; return id; }
    return 0;
}
#endif
