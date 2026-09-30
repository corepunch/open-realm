#ifndef BZ_WC3_PATHING_FINE_H
#define BZ_WC3_PATHING_FINE_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BZ_WC3_FINE_WORK 2048 // queue attempts/request; retain the engine's bounded synchronous routing budget
#define BZ_WC3_FINE_NODES (8 * BZ_WC3_FINE_WORK + 2) // nodes; eight discoveries/pop plus start and goal
#define BZ_WC3_FINE_HASH 32768 // slots; power of two, roughly half full at maximum node capacity

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
} wc3FineRequest_t;
typedef struct {
    wc3FineNode_t nodes[BZ_WC3_FINE_NODES];
    wc3FineEntry_t heap[BZ_WC3_FINE_NODES];
    uint32_t hash[BZ_WC3_FINE_HASH];
    uint32_t count, queued, pops, reopens, stale;
} wc3FineSearch_t;

static wc3FinePoint_t const wc3_fine_dirs[] = {
    {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1}
};

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
    while (search->hash[slot]) {
        uint32_t at = search->hash[slot] - 1;
        if (search->nodes[at].pos.x == pos.x && search->nodes[at].pos.y == pos.y) return (int)at;
        slot = (slot + 1) & (BZ_WC3_FINE_HASH - 1);
    }
    assert(search->count < BZ_WC3_FINE_NODES);
    uint32_t at = search->count++;
    search->hash[slot] = at + 1;
    search->nodes[at] = (wc3FineNode_t){ .pos = pos, .parent = -1 };
    return (int)at;
}

/* Original 1483f0 promotes a newly inserted entry above equal-key parents. */
static void wc3_fine_enqueue(wc3FineSearch_t *search, uint32_t at) {
    wc3FineNode_t *node = &search->nodes[at];
    wc3FineEntry_t entry = { node->g + node->h, at, ++node->gen };
    uint32_t pos = ++search->queued;
    assert(pos < BZ_WC3_FINE_NODES);
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
    if (next->state != WC3_FINE_NEW) {
        if (step.cost >= next->g) return;
        if (next->state == WC3_FINE_CLOSED) search->reopens++;
        next->gen++;
    }
    next->parent = (int)step.parent; next->g = step.cost; next->h = wc3_fine_heuristic(next->pos, goal);
    wc3_fine_enqueue(search, step.node);
}

/* 14aa10/14a4c0 search policy; callers supply the legal eight-edge graph.
 * TODO: target exits and public admission remain separate. The game adapter
 * now supplies verified entering strips and idle-object eligibility. */
static int wc3_fine_search(wc3FineSearch_t *search, wc3FineRequest_t const *req) {
    assert(req->budget <= BZ_WC3_FINE_WORK);
    memset(search->hash, 0, sizeof(search->hash));
    search->count = search->queued = search->pops = search->reopens = search->stale = 0;
    int start = wc3_fine_node(search, req, req->start), goal = wc3_fine_node(search, req, req->goal);
    if (start < 0 || goal < 0) return -1;
    search->nodes[start].h = wc3_fine_heuristic(req->start, req->goal);
    wc3_fine_enqueue(search, (uint32_t)start);
    while (search->queued) {
        if (search->pops++ >= req->budget) return -1;
        wc3FineEntry_t entry = wc3_fine_pop(search);
        wc3FineNode_t *node = &search->nodes[entry.node];
        if (node->gen != entry.gen) { search->stale++; continue; }
        node->state = WC3_FINE_NEW; node->gen++;
        if ((int)entry.node == goal) return goal;
        uint8_t edges = req->edges(req->data, node->pos);
        for (int dir = 0; dir < 8; dir++) {
            if (!(edges & (1u << dir))) continue;
            wc3FinePoint_t delta = wc3_fine_dirs[dir];
            int at = wc3_fine_node(search, req, (wc3FinePoint_t){ node->pos.x + delta.x, node->pos.y + delta.y });
            if (at < 0) continue;
            uint32_t cost = node->g + (delta.x && delta.y ? 21u : 15u);
            wc3_fine_relax(search, req->goal, (wc3FineStep_t){ (uint32_t)at, entry.node, cost });
        }
        node->state = WC3_FINE_CLOSED; node->gen++;
    }
    return -1;
}
#endif
