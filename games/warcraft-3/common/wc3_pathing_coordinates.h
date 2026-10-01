#ifndef WC3_PATHING_COORDINATES_H
#define WC3_PATHING_COORDINATES_H

#include "wc3_math.h"

/* Original04d870 subtracts the map origin and scales directly to fine cells.
 * Normalizing through the complete map extent can change the boundary cell. */
static inline float wc3_grid_coordinate(float value, float origin, float cell) {
    return wc3_div(wc3_sub(value, origin), cell);
}

/* Preserve separate original scalar multiply/add rounding on world output. */
static inline float wc3_world_coordinate(float value, float origin, float cell) {
    return wc3_add(wc3_mul(value, cell), origin);
}

#endif
