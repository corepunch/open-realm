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

typedef struct {
    float grid[2], origin[2], world[2];
} wc3GridPose_t;

/* Retain native fine pose precision between commits. World coordinates are
 * the published result; reprojecting them every tick loses native low bits. */
static inline void wc3_grid_step(wc3GridPose_t *pose, float const velocity[2], float elapsed) {
    float fine_velocity[2];
    for (unsigned k = 0; k < 2; k++) fine_velocity[k] = wc3_mul(velocity[k], wc3_float(0x3d000000));
    wc3_integrate(pose->grid, fine_velocity, elapsed);
    for (unsigned k = 0; k < 2; k++) pose->world[k] = wc3_world_coordinate(pose->grid[k], pose->origin[k], 32);
}

#endif
