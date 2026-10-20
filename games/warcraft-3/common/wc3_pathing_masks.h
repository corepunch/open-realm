#ifndef BZ_WC3_PATHING_MASKS_H
#define BZ_WC3_PATHING_MASKS_H
#include <stdint.h>

/* Original04caba WPM consumer: both nowalk02 and nofloat40 imply noamph80.
 * Preserve the authored byte; only the missing movement bit is derived here. */
static inline uint8_t wc3_wpm_movement_flags(uint8_t flags) {
    return flags | ((flags & 0x42) == 0x42 ? 0x80 : 0);
}
/* Public201630 pathingtype enumeration; this is a high-byte terrain mask,
 * distinct from the duplicated high/low movement query masks. */
static inline uint8_t wc3_pathingtype_mask(uint32_t type) {
    if (type > 7) return 0;
    return type ? (uint8_t)(1u << type) : 0xff;
}
static inline uint8_t wc3_terrain_pathing_edit(uint8_t flags, uint8_t mask, int blocked) {
    return blocked ? flags | mask : flags & (uint8_t)~mask;
}
#endif
