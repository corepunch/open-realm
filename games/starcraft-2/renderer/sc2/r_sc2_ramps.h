#ifndef R_SC2_RAMPS_H
#define R_SC2_RAMPS_H

#include "games/starcraft-2/common/sc2_map.h"

typedef struct sc2RampPiece_s {
    box2_t bounds;
    char config[5];
    uint32_t rotation, level;
} sc2RampPiece_t;

/* A bilinear patch differs from the emitted 00--11 triangles inside non-planar cells. */
static inline float r_sc2_ground_triangle_height(float const h[4], vec2_t p) {
    return p.x >= p.y ? h[0] + p.x*(h[1]-h[0]) + p.y*(h[3]-h[1]) :
        h[0] + p.x*(h[3]-h[2]) + p.y*(h[2]-h[0]);
}

/* Box axes and half extents are authored in height-grid coordinates, independently of world cell size. */
static inline bool r_sc2_ramp_contains(sc2RampBox_t const *box, vec2_t p) {
    vec2_t d = Vector2_sub(&p, &box->center);
    return box->width > 0 && box->height > 0 &&
        fabsf(Vector2_dot(&d, &box->right)) <= box->width + 0.001f &&
        fabsf(Vector2_dot(&d, &box->up)) <= box->height + 0.001f;
}

static inline box2_t r_sc2_ramp_bounds(sc2RampBox_t const *box) {
    vec2_t half = { fabsf(box->right.x) * box->width + fabsf(box->up.x) * box->height,
        fabsf(box->right.y) * box->width + fabsf(box->up.y) * box->height };
    return (box2_t){ Vector2_sub(&box->center, &half), Vector2_add(&box->center, &half) };
}

static inline uint32_t r_sc2_ramp_sample(sc2Map_t const *map, vec2_t p) {
    sc2MapSyncCliffLevel_t const *grid = map->t3SyncCliffLevel;
    int x = MAX(0, MIN((int)grid->width-1, (int)lroundf(p.x)));
    int y = MAX(0, MIN((int)grid->height-1, (int)lroundf(p.y)));
    return grid->data[x+y*grid->width];
}

static inline uint32_t r_sc2_ramp_level(sc2Map_t const *map, vec2_t p) {
    uint32_t value = r_sc2_ramp_sample(map, p);
    return value >= 64 ? value >> 6 : value;
}

/* Like WC3 LH transitions, SC2 marks slope corners separately: P/Q/R/S correspond to tiers 0/1/2/3. */
static inline sc2RampPiece_t r_sc2_ramp_piece(sc2Map_t const *map, sc2Ramp_t const *ramp, uint32_t edge) {
    sc2RampPiece_t piece = { .bounds = r_sc2_ramp_bounds(&ramp->edge[edge]), .level = ramp->lo };
    vec2_t corner[] = { piece.bounds.min, {piece.bounds.max.x, piece.bounds.min.y},
        piece.bounds.max, {piece.bounds.min.x, piece.bounds.max.y} };
    char raw[4];
    FOR_LOOP(i, 4) {
        if (r_sc2_ramp_contains(&ramp->mid, corner[i])) {
            vec2_t d = Vector2_sub(&corner[i], &ramp->mid.center);
            float t = (Vector2_dot(&d, &ramp->mid.up) / ramp->mid.height + 1) * 0.5f;
            raw[i] = 'P' + (int)floorf(LerpNumber(ramp->lo, ramp->hi, t) + 0.001f);
        } else raw[i] = 'A' + r_sc2_ramp_level(map, corner[i]);
    }
    FOR_LOOP(r, 4) FOR_LOOP(k, 4) {
        char a = raw[(piece.rotation+k)&3], b = raw[(r+k)&3];
        if (b < a) { piece.rotation = r; break; }
        if (b > a) break;
    }
    FOR_LOOP(i, 4) piece.config[i] = raw[(piece.rotation+i)&3];
    return piece;
}

/* Transition meshes own whole 2x2 cliff blocks. Diagonal BQQR/QBRQ meshes omit one corner block. */
static inline bool r_sc2_ramp_covers_ground(sc2Map_t const *map, vec2_t p) {
    vec2_t center = { floorf(p.x/2)*2+1, floorf(p.y/2)*2+1 };
    FOR_EACH_ARRAY(sc2Ramp_t, ramp, map->t3Terrain.ramps) {
        vec2_t d = Vector2_sub(&center, &ramp->mid.center);
        if (fabsf(Vector2_dot(&d, &ramp->mid.right)) < ramp->mid.width-0.001f &&
            fabsf(Vector2_dot(&d, &ramp->mid.up)) < ramp->mid.height-0.001f) continue;
        FOR_LOOP(i, 4)
            if (ramp->variant[i] != ~0u && r_sc2_ramp_contains(&ramp->edge[i], center)) return true;
    }
    return false;
}
#endif
