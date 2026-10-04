#ifndef BZ_WC3_PATHING_FINE_H
#define BZ_WC3_PATHING_FINE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wc3_pathing_limits.h"

typedef struct { int x, y; } wc3FinePoint_t;
typedef struct { wc3FinePoint_t min, max; } wc3FineBox_t;
typedef struct { uint32_t mask, flags; bool linked; } wc3FineObject_t;
typedef enum { WC3_FINE_NEW, WC3_FINE_OPEN, WC3_FINE_CLOSED } wc3FineState_t;
typedef struct {
    wc3FinePoint_t pos;
    int parent;
    uint32_t g, h, gen;
    wc3FineState_t state;
} wc3FineNode_t;
typedef struct { uint32_t key, node, gen; } wc3FineEntry_t;
typedef struct { uint32_t node, parent, cost; } wc3FineStep_t;
typedef struct {
    wc3FinePoint_t start, goal;
    uint32_t width, height, budget;
    uint8_t (*edges)(void const *data, wc3FinePoint_t pos);
    void const *data;
    bool *target_hit; /* Occupancy observer, consumed after neighbor creation. */
} wc3FineRequest_t;
typedef struct {
    wc3FineNode_t *nodes;
    wc3FineEntry_t *heap;
    uint32_t node_capacity, heap_capacity, heap_growth;
    uint32_t hash[BZ_WC3_FINE_HASH];
    uint32_t hash_stamps[BZ_WC3_FINE_HASH], hash_epoch;
    uint32_t count, queued, pops, reopens, stale, nearest, dist2;
    bool observed_obstruction;
#if defined(BZ_TESTS) || defined(BZ_WC3_FINE_TRACE)
    /* Read-only exact queue diagnostics; absent from ordinary game builds. */
    void (*pop_trace)(void *data, uint32_t const words[10]);
    void *trace_data;
#endif
} wc3FineSearch_t;

/* Original containers retain backing between requests. Slot0 is the heap
 * sentinel; node identity capacity is independent of queue entries/work.
 * Host allocation failure is fatal, never a fabricated native partial path. */
static inline void wc3_fine_reserve(wc3FineSearch_t *search, uint32_t nodes, uint32_t slots) {
    if (nodes > search->node_capacity) {
        uint32_t capacity = (nodes + BZ_WC3_FINE_NODE_GROW - 1) / BZ_WC3_FINE_NODE_GROW * BZ_WC3_FINE_NODE_GROW;
        wc3FineNode_t *data = realloc(search->nodes, (size_t)capacity * sizeof(*data));
        if (!data) { fprintf(stderr, "WC3 fine search: cannot allocate %u nodes\n", capacity); abort(); }
        search->nodes = data; search->node_capacity = capacity;
    }
    if (slots > search->heap_capacity) {
        uint32_t growth = search->heap_growth ? search->heap_growth : BZ_WC3_FINE_HEAP_GROW;
        uint32_t capacity = (slots + growth - 1) / growth * growth;
        wc3FineEntry_t *data = realloc(search->heap, (size_t)capacity * sizeof(*data));
        if (!data) { fprintf(stderr, "WC3 fine search: cannot allocate %u open slots\n", capacity); abort(); }
        search->heap = data; search->heap_capacity = capacity;
    }
}

static inline void wc3_fine_free(wc3FineSearch_t *search) {
    free(search->nodes); free(search->heap);
    search->nodes = NULL; search->heap = NULL;
    search->node_capacity = search->heap_capacity = 0;
    search->count = search->queued = 0;
}

/* Lookup is scratch, not a native generation. Advancing its epoch makes an
 * empty request O(1); only uint32 wrap needs to clear the retained stamps. */
static inline void wc3_fine_reset_lookup(wc3FineSearch_t *search) {
    if(!++search->hash_epoch) {
        memset(search->hash_stamps,0,sizeof(search->hash_stamps));
        search->hash_epoch=1;
    }
}

static wc3FinePoint_t const wc3_fine_dirs[] = {
    {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1}
};

/* Original14a560 compares unsigned wrapped squared distance, with strict
 * improvement: equal-distance cells preserve the first admitted identity. */
static inline uint32_t wc3_fine_dist2(wc3FinePoint_t a, wc3FinePoint_t b) {
    uint32_t x = (uint32_t)a.x - (uint32_t)b.x, y = (uint32_t)a.y - (uint32_t)b.y;
    return x * x + y * y;
}

/* 14ad50/16ee80 use the same three comparisons on the fine-cell scalar. */
static inline unsigned wc3_fine_class(float radius) {
    return radius >= 1.5f ? 3u : radius >= 1.0f ? 2u : radius >= 0.5f ? 1u : 0u;
}

/* Half-open 1/2/3/4-cell bounds from original 1492b0/16ee80. */
static inline wc3FineBox_t wc3_fine_cover(unsigned cls, wc3FinePoint_t pos) {
    int size = (int)cls + 1;
    wc3FinePoint_t min = { pos.x - size / 2, pos.y - size / 2 };
    return (wc3FineBox_t){ min, { min.x + size, min.y + size } };
}

/* Original1489a0: endpoint mode includes moving/transient objects; ordinary
 * searches exclude them. Suppression counters and the high disable bit win. */
static inline bool wc3_fine_object_blocks(wc3FineObject_t object, uint32_t mask, bool endpoint) {
    return object.linked && (object.mask & 0x01000000) && !(object.flags & 0x8fffffff) &&
        (endpoint || !(object.flags & 0x60000000)) && (object.mask & mask & 0xffffff);
}

/* Original 14a560's integer heuristic requires reopening cheaper closed nodes. */
static uint32_t wc3_fine_heuristic(wc3FinePoint_t pos, wc3FinePoint_t goal) {
    uint32_t a = 15u * (uint32_t)abs(pos.x - goal.x), b = 15u * (uint32_t)abs(pos.y - goal.y);
    if (a < b) { uint32_t swap = a; a = b; b = swap; }
    if (b <= (a >> 2)) return a;
    b += b >> 1;
    return a + (a >> 6) - (a >> 4) + (b >> 2) + (b >> 7);
}

/* Sparse request storage keeps short routes independent of total map area. */
static int wc3_fine_node(wc3FineSearch_t *search, wc3FineRequest_t const *req, wc3FinePoint_t pos) {
    if ((uint32_t)pos.x >= req->width || (uint32_t)pos.y >= req->height) return -1;
    uint32_t slot = ((uint32_t)pos.x * 0x9e3779b1u ^ (uint32_t)pos.y * 0x85ebca6bu) & (BZ_WC3_FINE_HASH - 1);
    while (search->hash_stamps[slot]==search->hash_epoch) {
        uint32_t at = search->hash[slot] - 1;
        if (search->nodes[at].pos.x == pos.x && search->nodes[at].pos.y == pos.y) return (int)at;
        slot = (slot + 1) & (BZ_WC3_FINE_HASH - 1);
    }
    if (search->count == BZ_WC3_FINE_NODES) return -1;
    wc3_fine_reserve(search, search->count + 1, 0);
    uint32_t at = search->count++;
    search->hash[slot] = at + 1;
    search->hash_stamps[slot]=search->hash_epoch;
    search->nodes[at] = (wc3FineNode_t){ .pos = pos, .parent = -1 };
    return (int)at;
}

/* Original 1483f0 promotes a newly inserted entry above equal-key parents. */
static void wc3_fine_enqueue(wc3FineSearch_t *search, uint32_t at) {
    wc3FineNode_t *node = &search->nodes[at];
    wc3FineEntry_t entry = { node->g + node->h, at, ++node->gen };
    wc3_fine_reserve(search, 0, search->queued + 2);
    uint32_t pos = ++search->queued;
    node->state = WC3_FINE_OPEN;
    while (pos > 1 && search->heap[pos / 2].key >= entry.key) {
        search->heap[pos] = search->heap[pos / 2]; pos /= 2;
    }
    search->heap[pos] = entry;
}

/* Original 148240 selects the right child on ties and retains stale records. */
static wc3FineEntry_t wc3_fine_pop(wc3FineSearch_t *search) {
    wc3FineEntry_t result = search->heap[1], last = search->heap[search->queued--];
    uint32_t pos = 1;
    while (pos * 2 <= search->queued) {
        uint32_t child = pos * 2;
        if (child < search->queued && search->heap[child + 1].key <= search->heap[child].key) child++;
        if (last.key <= search->heap[child].key) break;
        search->heap[pos] = search->heap[child]; pos = child;
    }
    if (search->queued) search->heap[pos] = last;
    return result;
}

/* Cheaper open paths leave stale records; cheaper closed paths reopen. The
 * original does not relax equal costs, preserving the first parent on ties. */
static void wc3_fine_relax(wc3FineSearch_t *search, wc3FinePoint_t goal, wc3FineStep_t step) {
    wc3FineNode_t *next = &search->nodes[step.node];
    if (next->state == WC3_FINE_NEW) {
        uint32_t dist2 = wc3_fine_dist2(next->pos, goal);
        if (dist2 < search->dist2) { search->dist2 = dist2; search->nearest = step.node; }
    }
    if (next->state != WC3_FINE_NEW) {
        if (step.cost >= next->g) return;
        if (next->state == WC3_FINE_CLOSED) search->reopens++;
        next->gen++;
    }
    next->parent = (int)step.parent; next->g = step.cost; next->h = wc3_fine_heuristic(next->pos, goal);
    wc3_fine_enqueue(search, step.node);
}

/* 14aa10/14a4c0 search policy; callers supply the legal eight-edge graph.
 * Target identity is observed during perimeter sampling, including suppressed
 * objects. Original14b760 creates neighbors, then skips relaxation on a hit. */
static int wc3_fine_search(wc3FineSearch_t *search, wc3FineRequest_t const *req) {
    wc3_fine_reset_lookup(search);
    search->count = search->queued = search->pops = search->reopens = search->stale = 0;
    search->observed_obstruction = false;
    search->nearest = 0; search->dist2 = wc3_fine_dist2(req->start, req->goal);
    int start = wc3_fine_node(search, req, req->start), goal = wc3_fine_node(search, req, req->goal);
    if (start < 0 || goal < 0) return -1;
    search->nodes[start].h = wc3_fine_heuristic(req->start, req->goal);
    wc3_fine_enqueue(search, (uint32_t)start);
    while (search->queued) {
        if (search->pops++ >= req->budget) return -1;
#if defined(BZ_TESTS) || defined(BZ_WC3_FINE_TRACE)
        if(search->pop_trace) {
            wc3FineEntry_t e=search->heap[1]; wc3FineNode_t const *n=search->nodes+e.node;
            uint32_t words[]={e.key,e.node,e.gen,n->gen,search->pops,n->g,n->h,
                              (uint32_t)n->parent,n->state,search->queued};
            search->pop_trace(search->trace_data,words);
        }
#endif
        wc3FineEntry_t entry = wc3_fine_pop(search);
        wc3FineNode_t *node = &search->nodes[entry.node];
        if (node->gen != entry.gen) { search->stale++; continue; }
        node->state = WC3_FINE_NEW; node->gen++;
        if ((int)entry.node == goal) return goal;
        /* Neighbor creation may relocate retained storage. Keep values and
         * indices across allocation, then reacquire the current node. */
        wc3FinePoint_t pos = node->pos;
        uint32_t parent_cost = node->g;
        uint8_t edges = req->edges(req->data, pos);
        /* Original1489a0 latches d0 on any denied perimeter cell. The four
         * neighbor-mask unions cover that entire perimeter, even off-route. */
        if (edges != 0xff) search->observed_obstruction = true;
        int neighbors[8];
        for (int dir = 0; dir < 8; dir++) {
            wc3FinePoint_t delta = wc3_fine_dirs[dir];
            neighbors[dir] = edges & (1u << dir) ? wc3_fine_node(search, req,
                (wc3FinePoint_t){ pos.x + delta.x, pos.y + delta.y }) : -1;
        }
        if (req->target_hit && *req->target_hit) {
            *req->target_hit = false;
            return (int)entry.node;
        }
        for (int dir = 0; dir < 8; dir++) {
            int at = neighbors[dir];
            if (at < 0) continue;
            wc3FinePoint_t delta = wc3_fine_dirs[dir];
            uint32_t cost = parent_cost + (delta.x && delta.y ? 21u : 15u);
            wc3_fine_relax(search, req->goal, (wc3FineStep_t){ (uint32_t)at, entry.node, cost });
        }
        node = &search->nodes[entry.node];
        node->state = WC3_FINE_CLOSED; node->gen++;
    }
    return -1;
}
#endif
