#ifndef WC3_PATHING_ARRIVAL_H
#define WC3_PATHING_ARRIVAL_H

#include "wc3_math.h"

typedef struct {
    float source[2], target[2], heading, range;
    uint32_t flags;
    float distance, error;
    bool in_range;
} wc3Arrival_t;

/* Original05b970,05bb06..05bb4f: guarded exponent subtraction precedes
 * the COMISS/CMOVBE minimum. Unordered inputs select the minimum too. */
static inline float wc3_point_arrival_range(float world) {
    uint32_t word=wc3_float_bits(world);
    float fine=wc3_float((word ^ (word-0x03000000u)) & 0x80000000u ? 0 : word-0x02800000u);
    float minimum=wc3_float(0x3efae148u);
    return fine>minimum ? fine : minimum;
}

/* Original16e910: range and heading are independent gates. Callers supply
 * the predicted pose; forced range does not bypass the angular tolerance. */
static inline bool wc3_arrival_update(wc3Arrival_t *a) {
    float x = wc3_sub(a->target[0], a->source[0]);
    float y = wc3_sub(a->target[1], a->source[1]);
    a->distance = wc3_sqrt(wc3_add(wc3_mul(x, x), wc3_mul(y, y)));
    a->error = wc3_heading_error(x, y, a->heading);
    a->in_range = (a->flags & 0x10000u) || a->distance <= a->range;
    return a->in_range && wc3_float(wc3_float_bits(a->error) & 0x7fffffffu) <= wc3_float(0x3e4ccccd);
}

#endif
