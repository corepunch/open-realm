#ifndef BZ_WC3_PATHING_ADAPTIVE_H
#define BZ_WC3_PATHING_ADAPTIVE_H
#include "wc3_pathing_route.h"

typedef struct wc3AccGate_s { uint32_t active; wc3FinePoint_t destination; } wc3AccGate_t;
typedef struct { uint32_t width, height; uint8_t const *classes; int *indices; } wc3AccMap_t;
typedef struct {
    wc3FineSearch_t work;
    wc3AccMap_t maps[4];
    uint8_t *levels,*source_ids,*gate_ids;
    uint8_t const *markers;
    wc3AccGate_t const *gates;
    bool warp;
    uint32_t warps;
    uint32_t level_capacity;
    uint32_t index_epoch;
    bool reuse_indices;
    uint32_t size;
    wc3FinePoint_t goal;
} wc3AccSearch_t;
typedef struct { int parent, level, side; wc3FinePoint_t pos; } wc3AccEdge_t;
typedef struct { wc3FineVector_t start, goal; uint32_t size, budget; } wc3AccRequest_t;

static inline void wc3_acc_free(wc3AccSearch_t *search) {
    wc3_fine_free(&search->work);
    free(search->source_ids);free(search->gate_ids);
    search->source_ids=search->gate_ids=NULL;
    free(search->levels); search->levels = NULL; search->level_capacity = 0;
    search->index_epoch=0;search->reuse_indices=false;
}

/* Original1d58e0 stops when root - n/root is -1, 0 or 1. Its final
 * truncated average is floor(sqrt(n)), including n=0 and UINT32_MAX. */
static uint32_t wc3_acc_sqrt(uint32_t n) {
    return wc3_isqrt(n);
}

static uint32_t wc3_acc_cost(wc3FinePoint_t a, wc3FinePoint_t b) {
    uint32_t x = 24u * ((uint32_t)a.x - (uint32_t)b.x), y = 24u * ((uint32_t)a.y - (uint32_t)b.y);
    return wc3_acc_sqrt(x * x + y * y);
}

static inline int wc3_acc_index(wc3AccSearch_t const *search, int entry) {
    if (!search->reuse_indices) return entry;
    return (uint32_t)entry >> 16 == search->index_epoch ? (int)((uint32_t)entry & 65535) : -1;
}

static inline int wc3_acc_identity(wc3AccSearch_t const *search, uint32_t at) {
    return (int)((search->reuse_indices ? search->index_epoch << 16 : 0) | (uint16_t)at);
}

/* Class1 blocks, class2 subdivides; class0 promotes through clear ancestors. */
static int wc3_acc_find(wc3AccSearch_t *search, int level, wc3FinePoint_t pos) {
    wc3AccMap_t *map = search->maps + level;
    uint32_t x = (uint32_t)(pos.x >> level), y = (uint32_t)(pos.y >> level);
    if (x >= map->width || y >= map->height) return -1;
    uint32_t cell = y * map->width + x;
    int *requested = map->indices + cell;
    /* Classes are immutable during a request. A current-epoch identity is
     * the canonical node or a clear descendant's alias. Reuse it before
     * walking the hierarchy; reset expires both without changing the retail
     * ushort node identity. */
    int found = wc3_acc_index(search, *requested);
    if (found >= 0) return found;
    uint8_t cls = map->classes[cell];
    if (cls == 1) return -1;
    if (cls == 2) return level ? -2 : -1;
    while (level < 3) {
        wc3AccMap_t *parent = search->maps + level + 1;
        x = (uint32_t)(pos.x >> (level + 1)); y = (uint32_t)(pos.y >> (level + 1));
        if (x >= parent->width || y >= parent->height || parent->classes[y * parent->width + x]) break;
        map = parent; cell = y * map->width + x; level++;
    }
    if (wc3_acc_index(search, map->indices[cell]) < 0) {
        wc3FineSearch_t *work = &search->work;
        /* Original163ef0 appends without a fine-style identity cap, but
         * 1625f0 publishes only the low16 bits in the cell metadata. */
        wc3_fine_reserve(work, work->count + 1, 0);
        if (search->level_capacity < work->node_capacity) {
            uint8_t *levels = realloc(search->levels, work->node_capacity);
            if (!levels) { fprintf(stderr, "WC3 adaptive search: cannot retain %u levels\n", work->node_capacity); abort(); }
            search->levels = levels;
            uint8_t *sources=realloc(search->source_ids,work->node_capacity),*gates=realloc(search->gate_ids,work->node_capacity);
            if(!sources || !gates){fprintf(stderr,"WC3 adaptive gate allocation failed\n");abort();}
            search->source_ids=sources;search->gate_ids=gates;search->level_capacity = work->node_capacity;
        }
        uint32_t at = work->count++;
        if (work->count > work->initialized) work->initialized = work->count;
        map->indices[cell] = wc3_acc_identity(search, at);
        work->nodes[at] = (wc3FineNode_t){.pos=pos,.parent=-1}; search->levels[at] = (uint8_t)level;
        search->source_ids[at]=search->warp && !level && search->markers ? search->markers[cell]:0;search->gate_ids[at]=0;
    }
    /* A clear descendant resolves to this same ancestor for the entire
     * request. Retain that identity without assigning another representative,
     * creating a node, or changing first-encounter ordering. */
    if (search->reuse_indices) *requested = map->indices[cell];
    return wc3_acc_index(search, map->indices[cell]);
}

static bool wc3_acc_clear(wc3AccSearch_t const *search, wc3FinePoint_t pos) {
    wc3AccMap_t const *map = search->maps;
    return (uint32_t)pos.x < map->width && (uint32_t)pos.y < map->height &&
        map->classes[(uint32_t)pos.y * map->width + (uint32_t)pos.x] == 0;
}

/* Side-specific size2 tests are intentionally asymmetric, including the east-boundary veto. */
static bool wc3_acc_side_ok(wc3AccSearch_t const *search, wc3AccEdge_t edge, bool boundary) {
    int x = edge.pos.x, y = edge.pos.y;
    bool right = wc3_acc_clear(search,(wc3FinePoint_t){x+1,y});
    bool down = wc3_acc_clear(search,(wc3FinePoint_t){x,y+1});
    bool diag = wc3_acc_clear(search,(wc3FinePoint_t){x+1,y+1});
    if (!boundary) return edge.side == 0 ? right : edge.side == 3 ? down : right && down && diag;
    if (edge.side == 0)
        return right && diag && (wc3_acc_clear(search,(wc3FinePoint_t){x+1,y+2}) || wc3_acc_clear(search,(wc3FinePoint_t){x-1,y}));
    if (edge.side == 3)
        return down && diag && (wc3_acc_clear(search,(wc3FinePoint_t){x+2,y+1}) || wc3_acc_clear(search,(wc3FinePoint_t){x,y-1}));
    bool a, b, c, d;
    if (edge.side == 1) {
        a = wc3_acc_clear(search,(wc3FinePoint_t){x,y-1}); b = wc3_acc_clear(search,(wc3FinePoint_t){x+1,y-1});
        c = wc3_acc_clear(search,(wc3FinePoint_t){x-2,y+1}); d = wc3_acc_clear(search,(wc3FinePoint_t){x-1,y+1});
    } else {
        a = wc3_acc_clear(search,(wc3FinePoint_t){x-1,y}); b = wc3_acc_clear(search,(wc3FinePoint_t){x-1,y+1});
        c = wc3_acc_clear(search,(wc3FinePoint_t){x+1,y-2}); d = wc3_acc_clear(search,(wc3FinePoint_t){x+1,y-1});
    }
    return right && down && diag && ((a && b) || (c && d) || (d && a));
}

/* The adaptive queue uses the fine heap's exact tie policy, with a distinct integer-distance cost. */
static void wc3_acc_relax_edge(wc3AccSearch_t *search, int at, int parent,bool warp) {
    wc3FineSearch_t *work = &search->work;
    wc3FineNode_t *next = work->nodes + at;
    uint32_t cost = work->nodes[parent].g + (warp?1:wc3_acc_cost(next->pos,work->nodes[parent].pos));
    if (next->state == WC3_FINE_NEW) {
        uint32_t distance = wc3_fine_dist2(next->pos,search->goal);
        if (distance < work->dist2) { work->dist2 = distance; work->nearest = (uint32_t)at; }
    } else {
        if (next->g <= cost) return;
        next->gen++;
    }
    search->gate_ids[at]=warp?search->source_ids[parent]:0;
    next->parent = parent; next->g = cost;
    /* A node's representative and this request's goal never change. Keep
     * the lazy first-relax publication (unreached nodes retain h=0), but do
     * not recompute the same heuristic after every improved incoming edge. */
    if (!next->h) next->h = wc3_acc_cost(next->pos,search->goal);
    wc3_fine_enqueue(work,(uint32_t)at);
}

static void wc3_acc_relax(wc3AccSearch_t *search,int at,int parent) {wc3_acc_relax_edge(search,at,parent,false);}

/* Mixed side squares split in original traversal order; flags describe accepted coarse edge ends. */
static void wc3_acc_side(wc3AccSearch_t *search, wc3AccEdge_t edge, bool ends[2]) {
    int at = wc3_acc_find(search,edge.level,edge.pos);
    while (at < 0) {
        if (at != -2) return;
        int old = edge.level--, half = 1 << edge.level;
        int pos = edge.side & 1 ? edge.pos.y : edge.pos.x;
        int mid = ((pos >> old) << old) + half;
        wc3AccEdge_t first = edge;
        int before = pos;
        if (mid <= pos) { before = mid - (edge.level ? (int)search->size : 1); mid = pos; }
        if (edge.side & 1) { first.pos.y = before; edge.pos.y = mid; }
        else { first.pos.x = before; edge.pos.x = mid; }
        bool split[2] = {false,false}; wc3_acc_side(search,first,split);
        ends[0] |= split[0];
        bool rest[2] = {false,false}; wc3_acc_side(search,edge,rest);
        ends[1] |= rest[1]; return;
    }
    if (search->size == 2) {
        int pos = edge.side & 1 ? edge.pos.y : edge.pos.x;
        bool boundary = pos == ((pos >> edge.level) << edge.level) + (1 << edge.level) - 1;
        /* ACC-01.1/01.2: an interior side lies in the clear square just
         * resolved by lookup, including every size2 cell it would test.
         * Keep boundary/corner predicates and the ushort-alias fallback. */
        bool clear_interior = !boundary && search->work.count <= (uint32_t)UINT16_MAX+1u;
        if (!clear_interior && !wc3_acc_side_ok(search,edge,boundary)) return;
    }
    wc3_acc_relax(search,at,edge.parent);
    if (edge.level) ends[0] = ends[1] = true;
}

/* Diagonal coarse neighbors subdivide without side recursion. */
static void wc3_acc_corner(wc3AccSearch_t *search, wc3AccEdge_t edge) {
    int at = wc3_acc_find(search,edge.level,edge.pos);
    while (at == -2) { edge.level--; at = wc3_acc_find(search,edge.level,edge.pos); }
    if (at < 0) return;
    if (search->size == 2) {
        bool accepted = edge.side == 3 || (edge.side == 0
            ? wc3_acc_clear(search,(wc3FinePoint_t){edge.pos.x+1,edge.pos.y})
            : edge.side == 2 ? wc3_acc_clear(search,(wc3FinePoint_t){edge.pos.x,edge.pos.y+1})
            : wc3_acc_side_ok(search,(wc3AccEdge_t){.pos=edge.pos,.side=1},false));
        if (!accepted) return;
    }
    wc3_acc_relax(search,at,edge.parent);
}

/* Base expansion relaxes N,E,S,W before NE,SE,SW,NW; each cardinal gates its adjacent corners. */
static void wc3_acc_base(wc3AccSearch_t *search, int parent) {
    wc3FinePoint_t pos = search->work.nodes[parent].pos;
    wc3FinePoint_t points[4] = {{pos.x,pos.y-1},{pos.x+1,pos.y},{pos.x,pos.y+1},{pos.x-1,pos.y}};
    bool sides[4] = {false,false,false,false};
    for (int side = 0; side < 4; side++) {
        wc3FinePoint_t p = points[side]; int at = wc3_acc_find(search,0,p);
        if (at < 0) continue;
        if (search->size == 2) {
            bool right = wc3_acc_clear(search,(wc3FinePoint_t){p.x+1,p.y});
            bool down = wc3_acc_clear(search,(wc3FinePoint_t){p.x,p.y+1});
            bool diag = wc3_acc_clear(search,(wc3FinePoint_t){p.x+1,p.y+1});
            bool accepted = side == 0 ? right : side == 1 ? right && diag : side == 2 ? down && diag : down;
            if (side == 0 && (p.x & 1) && search->levels[at])
                accepted &= wc3_acc_clear(search,(wc3FinePoint_t){p.x+1,p.y-2}) || wc3_acc_clear(search,(wc3FinePoint_t){p.x-1,p.y});
            if (side == 3 && (p.y & 1) && search->levels[at])
                accepted &= wc3_acc_clear(search,(wc3FinePoint_t){p.x-2,p.y+1}) || wc3_acc_clear(search,(wc3FinePoint_t){p.x,p.y-1});
            if (!accepted) continue;
        }
        wc3_acc_relax(search,at,parent); sides[side] = true;
    }
    wc3FinePoint_t corners[4] = {{pos.x+1,pos.y-1},{pos.x+1,pos.y+1},{pos.x-1,pos.y+1},{pos.x-1,pos.y-1}};
    for (int side = 0; side < 4; side++) if (sides[side] && sides[(side+1)&3]) {
        int at = wc3_acc_find(search,0,corners[side]);
        if (at < 0) continue;
        wc3FinePoint_t p = corners[side];
        if (search->size == 2 && side < 3 && !wc3_acc_clear(search,
            (wc3FinePoint_t){p.x+(side<2),p.y+(side>0)})) continue;
        wc3_acc_relax(search,at,parent);
    }
    uint8_t id=search->source_ids[parent];
    if(id && search->gates && (search->gates[id].active&1)) {
        wc3FinePoint_t dest=search->gates[id].destination;
        if(dest.x!=pos.x || dest.y!=pos.y) {
            int at=wc3_acc_find(search,0,dest);
            if(at>=0)wc3_acc_relax_edge(search,at,parent,true);
        }
    }
}

/* Coarse expansion retains square alignment, source representative and original size2 edge adjustment. */
static void wc3_acc_coarse(wc3AccSearch_t *search, int parent) {
    wc3FinePoint_t pos = search->work.nodes[parent].pos;
    int level = search->levels[parent], span = 1 << level;
    int x = (pos.x >> level) << level, y = (pos.y >> level) << level;
    if (search->size == 2) {
        if (pos.x > x+span-2) pos.x = x+span-2;
        if (pos.y > y+span-2) pos.y = y+span-2;
    }
    wc3FinePoint_t points[4] = {{pos.x,y-1},{x+span,pos.y},{pos.x,y+span},{x-1,pos.y}};
    bool ends[4][2] = {{false,false},{false,false},{false,false},{false,false}};
    for (int side = 0; side < 4; side++)
        wc3_acc_side(search,(wc3AccEdge_t){parent,level,side,points[side]},ends[side]);
    wc3FinePoint_t corners[4] = {{x+span,y-1},{x+span,y+span},{x-1,y+span},{x-1,y-1}};
    bool eligible[4] = {ends[0][1] && ends[1][0],ends[1][1] && ends[2][1],
        ends[2][0] && ends[3][1],ends[3][0] && ends[0][0]};
    for (int side = 0; side < 4; side++) if (eligible[side])
        wc3_acc_corner(search,(wc3AccEdge_t){parent,level,side,corners[side]});
}

/* Shared ordinary/special setup and search; -2 is the original direct setup result, -1 is a partial search. */
static int wc3_acc_search(wc3AccSearch_t *search, wc3AccRequest_t const *req) {
    wc3FineSearch_t *work = &search->work;
    work->heap_growth = BZ_WC3_ACC_HEAP_GROW;
    search->warps=0;
    work->count = work->queued = work->pops = work->reopens = work->stale = 0;
    search->size = req->size;
    wc3FinePoint_t start = {(int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(req->start.x))),
        (int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(req->start.y)))};
    search->goal = (wc3FinePoint_t){(int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(req->goal.x))),
        (int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(req->goal.y)))};
    bool valid = (uint32_t)start.x < search->maps[0].width && (uint32_t)start.y < search->maps[0].height;
    int first, goal;
    if (valid) {
        if (start.x == search->goal.x && start.y == search->goal.y) return -2;
        /* High bits own request lifetime; low bits retain retail's ushort node
         * identity. Ordinary reset is O(1), including promoted-cell aliases. Only
         * epoch wrap clears the retained planes; owners replacing them reset epoch. */
        if (!search->reuse_indices || !search->index_epoch || search->index_epoch == 65535) {
            for(unsigned level=0;level<4;level++) {
                wc3AccMap_t *map=search->maps+level;
                memset(map->indices,search->reuse_indices ? 0 : 255,sizeof(int)*map->width*map->height);
            }
            search->index_epoch = search->reuse_indices ? 1 : 0;
        } else search->index_epoch++;
        work->initialized = 0;
        first = wc3_acc_find(search,0,start);
        goal = first >= 0 ? wc3_acc_find(search,0,search->goal) : -1;
        work->source_node = (uint32_t)first; work->goal_node = (uint32_t)goal;
        work->query_initialized = first >= 0;
        if (first == goal) return -2;
        work->nearest = (uint32_t)first; work->dist2 = wc3_fine_dist2(start,search->goal);
        work->work_limit = req->budget;
    } else {
        /* 164c30 updates lane/size/warp and coordinates before bounds, but
         * retains the previous identities, cell epoch and work limit. */
        if (!work->query_initialized) return -2;
        first = (int)work->source_node; goal = (int)work->goal_node;
    }
    work->nodes[first].h = wc3_acc_cost(start,search->goal); wc3_fine_enqueue(work,(uint32_t)first);
    int at = -1;
    while (work->queued) {
        if (work->pops++ >= work->work_limit) break;
        wc3FineEntry_t entry = wc3_fine_pop(work);
        wc3FineNode_t *node = work->nodes + entry.node;
        if (node->gen != entry.gen) { work->stale++; continue; }
        node->state = WC3_FINE_NEW; node->gen++;
        if ((int)entry.node == goal) { at = goal; break; }
        if (search->levels[entry.node]) wc3_acc_coarse(search,(int)entry.node);
        else wc3_acc_base(search,(int)entry.node);
        node = work->nodes + entry.node;
        node->state = WC3_FINE_CLOSED; node->gen++;
    }
    return at;
}

/* Original162a30 emits incoming-edge sentinels before walking each parent. */
static uint32_t wc3_acc_route(wc3AccSearch_t *search, wc3AccRequest_t const *req, wc3FineVector_t *points) {
    int at = wc3_acc_search(search,req);
    wc3FineSearch_t *work = &search->work;
    if (at == -2) { points[0] = req->goal; return 1; }
    bool complete = at >= 0;
    if (!complete) at = (int)work->nearest;
    if (!complete && work->nodes[at].parent < 0) { points[0] = req->start; return 0x80000001u; }
    uint32_t count = 0;
    for (int cur = at; cur >= 0; cur = work->nodes[cur].parent) {
        wc3FinePoint_t p = work->nodes[cur].pos; int level = search->levels[cur], span = 1 << level;
        if (req->size == 2 && level) {
            if (p.x == ((p.x >> level) << level) + span - 1) p.x--;
            if (p.y == ((p.y >> level) << level) + span - 1) p.y--;
        }
        float offset = req->size == 2 ? 1.25f : .75f;
        points[count++] = (wc3FineVector_t){wc3_add(wc3_float(wc3_from_int((uint32_t)p.x)),offset),
            wc3_add(wc3_float(wc3_from_int((uint32_t)p.y)),offset)};
        if(search->gate_ids[cur]) {points[count++]=(wc3FineVector_t){wc3_float(0xc7fa0001),wc3_float(wc3_from_int(search->gate_ids[cur]))};search->warps++;}
    }
    points[count-1] = req->start;
    points[0] = complete ? req->goal : wc3_route_center(work->nodes[at].pos);
    return count | (complete ? 0 : 0x80000000u);
}
/* Original1627e0/163440 sums integer lengths per parent edge, not the A* cost divided by12. */
static uint32_t wc3_acc_query_distance(wc3AccSearch_t *search, wc3AccRequest_t const *req, wc3FineVector_t *out) {
    int at = wc3_acc_search(search,req);
    wc3FineSearch_t *work = &search->work;
    *out = req->goal;
    if (at == -2) {
        wc3FinePoint_t start = {(int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(req->start.x))),
            (int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(req->start.y)))};
        return wc3_acc_sqrt(4u * wc3_fine_dist2(start,search->goal));
    }
    if (at < 0) {
        wc3FineNode_t const *node = work->nodes + work->nearest;
        *out = node->parent < 0 ? req->start : wc3_route_center(node->pos);
        return UINT32_MAX;
    }
    uint32_t length = 0;
    while (work->nodes[at].parent >= 0) {
        int parent = work->nodes[at].parent;
        /* Original163440 tests the parent tag, including the geometric jump quirk. */
        if(search->gate_ids[parent]){length+=2;search->warps++;}
        else length += wc3_acc_sqrt(4u * wc3_fine_dist2(work->nodes[at].pos,work->nodes[parent].pos));
        at = parent;
    }
    return length;
}

#endif
