#ifndef BZ_WC3_PATHING_MASKS_H
#define BZ_WC3_PATHING_MASKS_H
#include <stdint.h>

/* Original04caba WPM consumer: both nowalk02 and nofloat40 imply noamph80.
 * Preserve the authored byte; only the missing movement bit is derived here. */
static inline uint8_t wc3_wpm_movement_flags(uint8_t flags) {
    return flags | ((flags & 0x42) == 0x42 ? 0x80 : 0);
}
#endif
