#ifndef BZ_WC3_PATHING_ROUTE_H
#define BZ_WC3_PATHING_ROUTE_H
#include "wc3_pathing_segment.h"

typedef struct {
    wc3FineNode_t const *nodes;
    uint32_t count;
    int at;
    wc3FineVector_t start, goal;
} wc3FineReconstruct_t;

static inline bool wc3_route_same_cell(wc3FineVector_t a, wc3FineVector_t b) {
    return wc3_int_bits(wc3_floor_bits(wc3_float_bits(a.x))) == wc3_int_bits(wc3_floor_bits(wc3_float_bits(b.x))) &&
           wc3_int_bits(wc3_floor_bits(wc3_float_bits(a.y))) == wc3_int_bits(wc3_floor_bits(wc3_float_bits(b.y)));
}

static inline wc3FineVector_t wc3_route_center(wc3FinePoint_t pos) {
    return (wc3FineVector_t){wc3_add(wc3_float(wc3_from_int((uint32_t)pos.x)), .5f),
                            wc3_add(wc3_float(wc3_from_int((uint32_t)pos.y)), .5f)};
}

/* Original147dc0: destination-first centres, exact source, then conditional
 * exact destination. A one-node chain may become the goal after source copy.
 * Internal callers supply valid parent indices and sufficient route capacity. */
static inline uint32_t wc3_fine_reconstruct(wc3FineReconstruct_t const *query, wc3FineVector_t *points, uint32_t capacity) {
    uint32_t count = 0;
    for (int at = query->at; at >= 0; at = query->nodes[at].parent) {
        assert((uint32_t)at < query->count && count < capacity);
        points[count++] = wc3_route_center(query->nodes[at].pos);
    }
    assert(count);
    points[count - 1] = query->start;
    if (wc3_route_same_cell(points[0], query->goal)) points[0] = query->goal;
    return count;
}
#endif
