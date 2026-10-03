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

/* Original05b970 clips the routing point inside four fine cells of each
 * world edge before subtracting the origin. Public task coordinates remain
 * unchanged. Preserve its ordered lower/upper comparisons and scalar math. */
static inline float wc3_point_order_coordinate(float value, float minimum, float maximum, float cell) {
    float margin=wc3_mul(cell,4);
    float lower=wc3_add(minimum,margin),upper=wc3_sub(maximum,margin);
    if (lower>value) return lower;
    if (value>upper) return upper;
    return value;
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

/* Original05c820 applies an admitted fine point without first publishing world XY. */
static inline void wc3_grid_place_fine(wc3GridPose_t *pose, float const point[2]) {
    for (unsigned k = 0; k < 2; k++) {
        pose->grid[k] = wc3_add(pose->grid[k], wc3_sub(point[k], pose->grid[k]));
        pose->world[k] = wc3_world_coordinate(pose->grid[k], pose->origin[k], 32);
    }
}

/* Original05c200 writes a delta from predicted fine pose, then integrates it.
 * Preserve that cancellation and reproject both axes, even for an axis setter. */
static inline void wc3_grid_place(wc3GridPose_t *pose, float const point[2]) {
    for (unsigned k = 0; k < 2; k++) {
        float target = wc3_grid_coordinate(point[k], pose->origin[k], 32);
        pose->grid[k] = wc3_add(pose->grid[k], wc3_sub(target, pose->grid[k]));
        pose->world[k] = wc3_world_coordinate(pose->grid[k], pose->origin[k], 32);
    }
}

/* Original15ed40 seeds both axes from ce4584 before CreateUnit's first05c200
 * write. This cancellation is observable through public fractional getters. */
static inline void wc3_grid_spawn_place(wc3GridPose_t *pose, float const point[2]) {
    pose->grid[0] = pose->grid[1] = wc3_float(0xc7fa0040u);
    wc3_grid_place(pose,point);
    /* The factory's notified second write consumes the first published world pair. */
    float world[2] = {pose->world[0],pose->world[1]};
    wc3_grid_place(pose,world);
}

#endif
