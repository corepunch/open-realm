#ifndef BZ_WC3_PATHING_ROUTE_H
#define BZ_WC3_PATHING_ROUTE_H
#include "wc3_pathing_segment.h"

typedef struct {
    wc3FineNode_t const *nodes;
    uint32_t count;
    int at;
    wc3FineVector_t start, goal;
} wc3FineReconstruct_t;

typedef struct { uint32_t index; bool gate; } wc3AccSelection_t;

/* Original167ae0 walks the reverse coarse route to ten accelerator units.
 * Index zero is never inspected. A gate sentinel selects its adjoining point;
 * selection alone does not execute portal traversal. */
static inline wc3AccSelection_t wc3_acc_select(wc3FineRoute_t route, bool reverse) {
    float length=0;
    if (!route.index) return (wc3AccSelection_t){0,false};
    for (uint32_t i=route.index-1;i;i--) {
        if (route.points[i].x==wc3_float(0xc7fa0001))
            return (wc3AccSelection_t){reverse?i-1:i+1,true};
        float dx=wc3_sub(route.points[i+1].x,route.points[i].x);
        float dy=wc3_sub(route.points[i+1].y,route.points[i].y);
        length=wc3_add(length,wc3_sqrt(wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy))));
        if (length>=10) return (wc3AccSelection_t){i,false};
    }
    return (wc3AccSelection_t){0,false};
}

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

/* Full original148100 setup/result contract, distinct from the A* kernel.
 * Same-cell setup returns before footprint/stamp/obstruction initialization.
 * A failed search with nearest==source emits the exact fractional source. */
static inline uint32_t wc3_fine_build_route(wc3FineSearch_t *search, wc3FineRequest_t const *request,
        wc3FineVector_t source, wc3FineVector_t goal, wc3FineVector_t *points, uint32_t capacity, bool *complete) {
    assert(capacity);
    if ((uint32_t)request->start.x<request->width && (uint32_t)request->start.y<request->height &&
        request->start.x==request->goal.x && request->start.y==request->goal.y) {
        search->count=search->queued=search->pops=search->reopens=search->stale=0;
        memset(search->hash,0,sizeof(search->hash)); wc3_fine_reserve(search,0,1);
        points[0]=goal; *complete=true; return 1;
    }
    int at=wc3_fine_search(search,request); *complete=at>=0;
    if (at<0) {
        if (!search->count) return 0;
        at=(int)search->nearest;
        if (at==0) {points[0]=source; return 1;}
        goal=wc3_route_center(search->nodes[at].pos);
    }
    wc3FineReconstruct_t route={search->nodes,search->count,at,source,goal};
    return wc3_fine_reconstruct(&route,points,capacity);
}
#endif
