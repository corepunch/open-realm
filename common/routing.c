#ifdef GAME_WORLD
#if defined(WOW) || defined(SC2)
#include "server/server.h"
#else
#include "game/g_local.h"
#endif
#define ge (&globals)
#ifndef EDICT_NUM
#define EDICT_NUM(n) (globals.edicts + (n))
#endif
#else
#include "server/server.h"
#endif

#include <float.h>
#include <limits.h>
#include <stdlib.h>  /* abs (CM_LineIsWalkable) */

/* Upper bound on flow-field BFS expansion.  Each cell is closed at most once,
 * so the connected pathable component is fully covered in at most width*height
 * iterations, after which the open queue empties and the loop exits naturally.
 * A fixed 0xffff (65535) silently truncated the flow field on maps larger than
 * that many cells (e.g. NightElfX01 is 384*512=196608) — units beyond the
 * covered third got no flow vector and stopped mid-map, looking "gated" when
 * the goal was actually reachable.  Bound by the real map size for full
 * coverage, matching the original's whole-map pathing. */
#define HEATMAP_MAX_ITERATIONS(cells) ((int)(cells))

typedef struct {
    point2_t parent;
    int f, g, h, s;
} pathNode_t;

/* Per-build heatmap node.  x/y are NOT stored here — they are derivable
 * from the flat array index (x = index % width, y = index / width).
 * 'price' is the shortest-path cost to the goal (INT_MAX = unreached); 'closed'
 * is the SPFA "currently in the relaxation queue" flag. */
typedef struct routeNode_s {
    int price;
    bool closed;
} routeNode_t;

typedef struct {
    BYTE unused:1;
    BYTE nowalk:1;
    BYTE nofly:1;
    BYTE nobuild:1;
    BYTE unused2:1;
    BYTE blight:1;
    BYTE nowater:1;
    BYTE unknown:1;
} pathMapCell_t;

struct {
    DWORD width;
    DWORD height;
    pathMapCell_t *original;
    pathMapCell_t *data;
    routeNode_t *heatmap;
    DWORD *queue;          /* SPFA relaxation queue (ring buffer, width*height+1) */
} pathmap = { 0 };

#define HEATMAP_CACHE_SLOTS 4

typedef struct {
    point2_t target;    /* pathmap cell coordinate of the goal; {-1,-1} = invalid */
    DWORD    generation;
    VECTOR2 *flow;   /* pre-computed flow vector per cell; NULL until first use */
} heatmapCacheEntry_t;

static heatmapCacheEntry_t heatmap_cache[HEATMAP_CACHE_SLOTS];
static DWORD    heatmap_next_generation = 1;
static int      heatmap_lru[HEATMAP_CACHE_SLOTS];
static int      heatmap_lru_clock = 0;
static VECTOR2 *active_flow = NULL; /* points into the current cache slot */

#ifdef TOOL_COMMON_NO_MPQ
/* Per-call perf counters; only tracked in test builds to avoid overhead. */
static struct {
    DWORD cache_hits, cache_misses, heatmap_iterations, flow_cells_baked;
} g_perf;

void CM_ResetTestPathPerfStats(void) { memset(&g_perf, 0, sizeof(g_perf)); }

typedef struct routePerfStats_s {
    DWORD cache_hits, cache_misses, heatmap_iterations, flow_cells_baked;
} routePerfStats_t;

routePerfStats_t CM_GetTestPathPerfStats(void) {
    return (routePerfStats_t){
        g_perf.cache_hits, g_perf.cache_misses,
        g_perf.heatmap_iterations, g_perf.flow_cells_baked,
    };
}
#define PERF_INC(field) g_perf.field++
#define PERF_ADD(field, n) g_perf.field += (n)
#else
#define PERF_INC(field) ((void)0)
#define PERF_ADD(field, n) ((void)0)
#endif

static void heatmap_cache_invalidate(void) {
    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        heatmap_cache[i].target    = (point2_t){ -1, -1 };
        heatmap_cache[i].generation = 0;
        /* Keep flow buffers allocated to avoid malloc churn on map reload. */
    }
    heatmap_next_generation = 1;
    heatmap_lru_clock       = 0;
    active_flow             = NULL;
    memset(heatmap_lru, 0, sizeof(heatmap_lru));
}

void CM_InvalidatePathCache(void) {
    heatmap_cache_invalidate();
}

/* Set active_flow to the cached flow field for the given generation without
 * triggering a heatmap rebuild.  Returns true if the generation is still in
 * the cache (the caller may then skip the expensive CM_BuildHeatmap call).
 * Returns false when the entry was evicted; the caller must rebuild. */
BOOL CM_ActivateCachedFlow(DWORD generation) {
    if (!generation)
        return false;
    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        if (heatmap_cache[i].generation == generation && heatmap_cache[i].flow) {
            active_flow = heatmap_cache[i].flow;
            heatmap_lru[i] = heatmap_lru_clock++;
            return true;
        }
    }
    return false;
}

void CM_SetupPathMap(DWORD width, DWORD height, BYTE const *cells) {
    DWORD n = width * height;

    SAFE_DELETE(pathmap.data, MemFree);
    SAFE_DELETE(pathmap.original, MemFree);
    SAFE_DELETE(pathmap.heatmap, MemFree);
    SAFE_DELETE(pathmap.queue, MemFree);
    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        SAFE_DELETE(heatmap_cache[i].flow, MemFree);
    }

    pathmap.width = width;
    pathmap.height = height;
    if (!n) {
        heatmap_cache_invalidate();
        return;
    }

    pathmap.data = MemAlloc(n);
    pathmap.original = MemAlloc(n);
    pathmap.heatmap = MemAlloc(n * sizeof(routeNode_t));
    pathmap.queue = MemAlloc((n + 1) * sizeof(DWORD));

    if (cells) {
        memcpy(pathmap.original, cells, n);
    } else {
        memset(pathmap.original, 0, n);
    }
    memcpy(pathmap.data, pathmap.original, n);
    memset(pathmap.heatmap, 0, n * sizeof(routeNode_t));

    heatmap_cache_invalidate();
}

static point2_t LocationToPathMap(LPCVECTOR2 location);

static int const dx[] = {-1, 1, 0, 0, -1, -1, 1, 1};
static int const dy[] = {0, 0, -1, 1, -1, 1, -1, 1};
static int const gv[] = {10, 10, 10, 10, 14, 14, 14, 14};

#ifdef DEBUG_PATHFINDING
LPCOLOR32 pathDebug = NULL;
static void CM_FillDebugObstacles(void) {
    memset(pathDebug, 0x0, sizeof(pathmap.width * pathmap.height * sizeof(COLOR32)));
    FOR_LOOP(i, pathmap.width * pathmap.height) {
        pathDebug[i].r = pathmap.data[i].nowalk ? 255 : 0;
        pathDebug[i].g = 0;
        pathDebug[i].b = 0;
        pathDebug[i].a = 255;
    }
}
#endif

inline static pathMapCell_t *path_node(DWORD x, DWORD y) {
    int const index = x + y * pathmap.width;
    return &pathmap.data[index];
}

inline static routeNode_t *heatmap(DWORD x, DWORD y) {
    int const index = x + y * pathmap.width;
    return &pathmap.heatmap[index];
}

inline static bool is_valid_point(DWORD x, DWORD y) {
    return x < pathmap.width && y < pathmap.height;
}

inline static bool is_obstacle(DWORD x, DWORD y) {
    pathMapCell_t const *node = path_node(x, y);
    return !node || node->nowalk;
}

static void reset_pathmap_data(void) {
    if (pathmap.data && pathmap.original) {
        memcpy(pathmap.data, pathmap.original, pathmap.width * pathmap.height);
    }
}

static void clear_heatmap(void) {
    /* Reset every cell to "unreached" (price = INT_MAX) and out-of-queue before
     * an SPFA build.  Not a memset: price must be INT_MAX, not 0. */
    FOR_LOOP(i, pathmap.width * pathmap.height) {
        pathmap.heatmap[i].price = INT_MAX;
        pathmap.heatmap[i].closed = false;
    }
}

static FLOAT pathmap_cell_world_size(void);

/* Radius of an entity's collision in whole pathing cells (>=1).  WC3's pathing
 * cell is 32 world units; the stamp/query must use that same size so a unit's
 * footprint is the right number of cells (the old hard-coded /24 inflated every
 * footprint ~33% and disagreed with the /32 used by the query paths). */
static DWORD collision_radius_cells(FLOAT collision) {
    return MAX(1, (DWORD)ceilf(collision / pathmap_cell_world_size()));
}

/* Stamp a single entity's footprint into a pathmap byte array. */
static void stamp_entity_obstacle(edict_t const *ent, pathMapCell_t *target) {
    point2_t p = LocationToPathMap(&ent->s.origin2);
    if (ent->pathtex) {
        pathTex_t *pt = ent->pathtex;
        FOR_LOOP(x, pt->width) {
            FOR_LOOP(y, pt->height) {
                int px = (int)x + p.x - (int)pt->width / 2;
                int py = (int)y + p.y - (int)pt->height / 2;
                if (is_valid_point(px, py)) {
                    target[px + py * pathmap.width].nowalk |=
                        pt->map[x + y * pt->width].b;
                }
            }
        }
    } else if (!(ent->svflags & SVF_MONSTER)) {
        DWORD radius = collision_radius_cells(ent->collision);
        FOR_LOOP(x, MAX(1, radius * 2)) {
            FOR_LOOP(y, MAX(1, radius * 2)) {
                int px = (int)x + p.x - (int)radius;
                int py = (int)y + p.y - (int)radius;
                if (is_valid_point(px, py)) {
                    target[px + py * pathmap.width].nowalk |= 1;
                }
            }
        }
    }
}

/* Bake all current static entity obstacles (buildings, doodads with pathtex,
 * non-monster solid entities) permanently into pathmap.original.  Call this
 * once after SpawnEntities() — these obstacles never move so they don't need
 * to be re-stamped every heatmap build. */
void CM_BakeStaticObstacles(void) {
    if (!pathmap.original)
        return;
    FOR_LOOP(i, ge->num_edicts) {
        edict_t *ent = EDICT_NUM(i);
        if (!ent->inuse)
            continue;
        stamp_entity_obstacle(ent, pathmap.original);
    }
    /* Invalidate the cache so the next build uses the updated original. */
    heatmap_cache_invalidate();
}

/* Apply only dynamic (unit/monster) obstacles into pathmap.data for
 * closest-pathable-point queries at command time.  Static obstacles are
 * already baked into pathmap.original and copied in by reset_pathmap_data(). */
static void apply_dynamic_obstacles(edict_t const *ignore) {
    FOR_LOOP(i, ge->num_edicts) {
        edict_t *ent = EDICT_NUM(i);
        if (!ent->inuse || ent == ignore)
            continue;
        /* Only stamp units (SVF_MONSTER) — static obstacles are already in
         * pathmap.original and were restored by reset_pathmap_data(). */
        if (!(ent->svflags & SVF_MONSTER))
            continue;
        point2_t p = LocationToPathMap(&ent->s.origin2);
        DWORD radius = collision_radius_cells(ent->collision);
        FOR_LOOP(x, radius * 2) {
            FOR_LOOP(y, radius * 2) {
                int px = (int)x + p.x - (int)radius;
                int py = (int)y + p.y - (int)radius;
                if (is_valid_point(px, py))
                    path_node(px, py)->nowalk |= 1;
            }
        }
    }
}

static bool is_pathable_node(int x, int y) {
    return is_valid_point(x, y) && !is_obstacle(x, y);
}

static FLOAT pathmap_cell_world_size(void) {
    FLOAT cell_x = FLT_MAX;
    FLOAT cell_y = FLT_MAX;

    if (pathmap.width > 0) {
        VECTOR2 a = CM_GetDenormalizedMapPosition(0, 0);
        VECTOR2 b = CM_GetDenormalizedMapPosition(1.f / pathmap.width, 0);
        cell_x = fabsf(b.x - a.x);
    }
    if (pathmap.height > 0) {
        VECTOR2 a = CM_GetDenormalizedMapPosition(0, 0);
        VECTOR2 b = CM_GetDenormalizedMapPosition(0, 1.f / pathmap.height);
        cell_y = fabsf(b.y - a.y);
    }
    return MAX(1.f, MIN(cell_x, cell_y));
}

static bool is_pathable_node_for_radius_cells(int x, int y, int radius_cells) {
    if (!is_pathable_node(x, y)) {
        return false;
    }
    for (int py = y - radius_cells; py <= y + radius_cells; py++) {
        for (int px = x - radius_cells; px <= x + radius_cells; px++) {
            if (!is_pathable_node(px, py)) {
                return false;
            }
        }
    }
    return true;
}

static bool closest_pathable_node(LPCVECTOR2 location, FLOAT radius, point2_t *out) {
    VECTOR2 n = CM_GetNormalizedMapPosition(location->x, location->y);
    FLOAT fx = n.x * pathmap.width;
    FLOAT fy = n.y * pathmap.height;
    int tx = (int)floorf(fx);
    int ty = (int)floorf(fy);
    int max_radius = (int)MAX(pathmap.width, pathmap.height);
    int radius_cells = (int)ceilf(MAX(0.f, radius) / pathmap_cell_world_size());
    FLOAT best_dist = FLT_MAX;
    point2_t best = { 0, 0 };
    bool found = false;

    if (!pathmap.data || !pathmap.original || !pathmap.width || !pathmap.height) {
        return false;
    }
    if (is_pathable_node_for_radius_cells(tx, ty, radius_cells)) {
        *out = (point2_t){ tx, ty };
        return true;
    }

    for (int search_radius = 1; search_radius <= max_radius && !found; search_radius++) {
        for (int y = ty - search_radius; y <= ty + search_radius; y++) {
            for (int x = tx - search_radius; x <= tx + search_radius; x++) {
                if (x != tx - search_radius && x != tx + search_radius &&
                    y != ty - search_radius && y != ty + search_radius) {
                    continue;
                }
                if (!is_pathable_node_for_radius_cells(x, y, radius_cells)) {
                    continue;
                }

                FLOAT cx = x + 0.5f;
                FLOAT cy = y + 0.5f;
                FLOAT dist = (cx - fx) * (cx - fx) + (cy - fy) * (cy - fy);
                if (!found || dist < best_dist) {
                    best_dist = dist;
                    best = (point2_t){ x, y };
                    found = true;
                }
            }
        }
    }

    if (found) {
        *out = best;
    }
    return found;
}

BOOL CM_ClosestPathablePointForRadius(LPCVECTOR2 location, FLOAT radius, LPVECTOR2 out) {
    point2_t point;

    if (!location || !out) {
        return false;
    }
    if (!pathmap.data || !pathmap.original) {
        *out = *location;
        return true;
    }

    reset_pathmap_data();
    apply_dynamic_obstacles(NULL);
    if (!closest_pathable_node(location, radius, &point)) {
        return false;
    }

    *out = CM_GetDenormalizedMapPosition((point.x + 0.5f) / pathmap.width,
                                         (point.y + 0.5f) / pathmap.height);
    return true;
}

BOOL CM_ClosestPathablePoint(LPCVECTOR2 location, LPVECTOR2 out) {
    return CM_ClosestPathablePointForRadius(location, 0, out);
}

/* Static-map (original) variants of the walkability tests.  These read
 * pathmap.original — terrain plus baked building footprints — and never touch
 * the dynamic unit stamping in pathmap.data.  The move-time collision test
 * (move_is_valid in g_ai.c) checks units precisely with their collision radii
 * via BoxEdicts, so CM_PointIsPathableForRadius only has to answer "does the
 * static world block a unit of this radius here?" without mutating the pathmap
 * on the per-frame hot path. */
inline static bool is_obstacle_original(DWORD x, DWORD y) {
    int const index = x + y * pathmap.width;
    return !pathmap.original || pathmap.original[index].nowalk;
}

static bool is_pathable_node_original(int x, int y) {
    return is_valid_point(x, y) && !is_obstacle_original(x, y);
}

static bool is_pathable_node_original_for_radius_cells(int x, int y, int radius_cells) {
    if (!is_pathable_node_original(x, y)) {
        return false;
    }
    for (int py = y - radius_cells; py <= y + radius_cells; py++) {
        for (int px = x - radius_cells; px <= x + radius_cells; px++) {
            if (!is_pathable_node_original(px, py)) {
                return false;
            }
        }
    }
    return true;
}

/* Read-only test: can a unit with the given collision radius stand at this
 * world location without overlapping static terrain or a building footprint?
 * Used by the collision-aware move step.  Returns true when no pathmap is
 * loaded (e.g. headless tests) so movement is never blocked by a missing map. */
BOOL CM_PointIsPathableForRadius(LPCVECTOR2 location, FLOAT radius) {
    if (!location || !pathmap.original || !pathmap.width || !pathmap.height) {
        return true;
    }
    VECTOR2 n = CM_GetNormalizedMapPosition(location->x, location->y);
    int tx = (int)floorf(n.x * pathmap.width);
    int ty = (int)floorf(n.y * pathmap.height);
    int radius_cells = (int)ceilf(MAX(0.f, radius) / pathmap_cell_world_size());
    return is_pathable_node_original_for_radius_cells(tx, ty, radius_cells);
}

/* Cheap straight-line walkability test between two world points: walk the
 * pathmap cells along the segment (Bresenham) and fail on the first obstacle.
 * O(cells on the line) — vastly cheaper than a full flow-field bake, so a unit
 * chasing a target in the open can steer directly instead of flood-filling. */
BOOL CM_LineIsWalkable(LPCVECTOR2 a, LPCVECTOR2 b) {
    if (!a || !b || pathmap.width == 0 || pathmap.height == 0) {
        return false;
    }
    VECTOR2 na = CM_GetNormalizedMapPosition(a->x, a->y);
    VECTOR2 nb = CM_GetNormalizedMapPosition(b->x, b->y);
    int ax = (int)(na.x * pathmap.width),  ay = (int)(na.y * pathmap.height);
    int bx = (int)(nb.x * pathmap.width),  by = (int)(nb.y * pathmap.height);
    int dx = abs(bx - ax), dy = abs(by - ay);
    int sx = ax < bx ? 1 : -1, sy = ay < by ? 1 : -1;
    int err = dx - dy;
    int x = ax, y = ay;
    int guard = dx + dy + 2;
    while (guard-- > 0) {
        if (x < 0 || y < 0 || !is_valid_point((DWORD)x, (DWORD)y) ||
            is_obstacle((DWORD)x, (DWORD)y)) {
            return false;
        }
        if (x == bx && y == by) {
            return true;
        }
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 <  dx) { err += dx; y += sy; }
    }
    return true;
}

static VECTOR2 compute_flow_at(DWORD x, DWORD y) {
    int prices[8];
    int min_price = INT_MAX;

    FOR_LOOP(dir, 8) {
        prices[dir] = INT_MAX;
    }
    FOR_LOOP(dir, 8) {
        int new_x = (int)x + dx[dir];
        int new_y = (int)y + dy[dir];
        if (!is_valid_point(new_x, new_y) ||
            is_obstacle(new_x, new_y) ||
            heatmap(new_x, new_y)->price == INT_MAX)  /* unreached by the flood */
            continue;
        /* Don't let the flow vector point diagonally through a wall corner. */
        if (dir >= 4 && !(is_pathable_node((int)x + dx[dir], (int)y) &&
                          is_pathable_node((int)x, (int)y + dy[dir])))
            continue;
        prices[dir] = heatmap(new_x, new_y)->price;
        min_price = MIN(prices[dir], min_price);
    }

    VECTOR2 direction = { 0, 0 };
    FOR_LOOP(dir, 8) {
        if (prices[dir] == INT_MAX)
            continue;
        float k = 10.f / MAX(1, 10 + (prices[dir] - min_price));
        VECTOR2 dirvec = { dx[dir], dy[dir] };
        Vector2_normalize(&dirvec);
        direction.x += dirvec.x * k;
        direction.y += dirvec.y * k;
    }
    return direction;
}

/* Bake flow vectors only for cells reached by the heatmap (price != INT_MAX).
 * Unreachable cells get a zero vector; counting only reachable cells lets
 * tests verify the flood boundary without baking the whole map. */
static void bake_flow_field(VECTOR2 *flow) {
    DWORD cells = pathmap.width * pathmap.height;
    FOR_LOOP(i, cells) {
        if (pathmap.heatmap[i].price == INT_MAX) {
            flow[i] = (VECTOR2){ 0, 0 };
        } else {
            flow[i] = compute_flow_at(i % pathmap.width, i / pathmap.width);
            PERF_INC(flow_cells_baked);
        }
    }
}

VECTOR2 get_flow_direction(DWORD heatmapindex, float fnx, float fny) {
    /* Honor the generation handshake: point active_flow at THIS goal's field.
     * Previously the generation was ignored and we read whatever flow field was
     * globally active, so a unit that missed the per-frame build budget would
     * sample another unit's goal field and steer to the wrong place ('weird
     * routes' in multi-unit movement).  On a miss, return zero so the caller
     * falls back to the direct heading deterministically. */
    if (!CM_ActivateCachedFlow(heatmapindex)) {
        return (VECTOR2){ 0, 0 };
    }
    if (!active_flow || !pathmap.width || !pathmap.height) {
        return (VECTOR2){ 0, 0 };
    }
    VECTOR2 n = CM_GetNormalizedMapPosition(fnx, fny);
    n.x *= pathmap.width;
    n.y *= pathmap.height;
    DWORD cx = (DWORD)floorf(n.x);
    DWORD cy = (DWORD)floorf(n.y);
    if (!is_valid_point(cx, cy))
        return (VECTOR2){ 0, 0 };
    /* Bilinear interpolation over the pre-baked flow field. */
    DWORD cx1 = (cx + 1 < pathmap.width)  ? cx + 1 : cx;
    DWORD cy1 = (cy + 1 < pathmap.height) ? cy + 1 : cy;
    FLOAT tx = n.x - (FLOAT)cx;
    FLOAT ty = n.y - (FLOAT)cy;
    VECTOR2 a = active_flow[cx  + cy  * pathmap.width];
    VECTOR2 b = active_flow[cx1 + cy  * pathmap.width];
    VECTOR2 c = active_flow[cx1 + cy1 * pathmap.width];
    VECTOR2 d = active_flow[cx  + cy1 * pathmap.width];
    VECTOR2 ab = Vector2_lerp(&a, &b, tx);
    VECTOR2 cd = Vector2_lerp(&d, &c, tx);
    return Vector2_lerp(&ab, &cd, ty);
}

static point2_t LocationToPathMap(LPCVECTOR2 location) {
    VECTOR2 n_target = CM_GetNormalizedMapPosition(location->x, location->y);
    return (point2_t) { n_target.x * pathmap.width, n_target.y * pathmap.height };
}

/* Build the distance-to-goal field with SPFA (a queue-based Bellman-Ford):
 * relax each cell's neighbours and re-enqueue any whose cost improves, so the
 * octile (10 cardinal / 14 diagonal) costs yield true shortest paths.  The old
 * FIFO-BFS fixed each cell's cost on first visit with no relaxation, which is
 * wrong for mixed edge costs and produced visibly suboptimal, wandering routes.
 *
 * Diagonal moves are only taken when both adjacent cardinal cells are also
 * walkable, so the flow never cuts through the corner of a wall or building —
 * a corner a unit physically cannot squeeze through.
 *
 * The 'closed' flag means "currently queued"; since a cell is never queued
 * twice, at most width*height cells are queued at once and the ring buffer of
 * width*height+1 never overflows. */
DWORD build_heatmap(point2_t target) {
#ifdef DEBUG_PATHFINDING
    CM_FillDebugObstacles();
#endif

    DWORD const width = pathmap.width;
    DWORD const cap = pathmap.width * pathmap.height + 1;
    DWORD *const q = pathmap.queue;
    DWORD head = 0, tail = 0;

    DWORD const ti = (DWORD)target.x + (DWORD)target.y * width;
    pathmap.heatmap[ti].price = 0;
    pathmap.heatmap[ti].closed = true;
    q[tail++] = ti;

    while (head != tail) {
        DWORD const u = q[head];
        head = (head + 1) % cap;
        PERF_INC(heatmap_iterations);
        routeNode_t *const un = &pathmap.heatmap[u];
        un->closed = false;
        int const up = un->price;
        int const ux = (int)(u % width);
        int const uy = (int)(u / width);
        FOR_LOOP(i, 8) {
            int const nx = ux + dx[i];
            int const ny = uy + dy[i];
            if (!is_pathable_node(nx, ny))
                continue;
            /* no diagonal corner-cutting: both cardinal cells must be open too */
            if (i >= 4 && !(is_pathable_node(nx, uy) && is_pathable_node(ux, ny)))
                continue;
            DWORD const v = (DWORD)nx + (DWORD)ny * width;
            routeNode_t *const vn = &pathmap.heatmap[v];
            int const np = up + gv[i];
            if (np < vn->price) {
                vn->price = np;
                if (!vn->closed) {
                    vn->closed = true;
                    q[tail] = v;
                    tail = (tail + 1) % cap;
                }
            }
        }
    }

#ifdef DEBUG_PATHFINDING
    FOR_LOOP(i, pathmap.width * pathmap.height) {
        pathDebug[i].r = pathmap.data[i].nowalk ? 255 : 0;
    }
#endif
    return 0;
}

DWORD CM_BuildHeatmap(edict_t *goalentity) {
    DWORD map_cells = pathmap.width * pathmap.height;

    if (!goalentity || !pathmap.data || !pathmap.original || !pathmap.heatmap || !map_cells) {
        return 0;
    }

    /* Resolve the goal's pathmap cell, adjusting to the closest pathable
     * cell if the raw position falls into an obstacle.  The adjusted target
     * is used both as the cache key (so two entities at the same cell share
     * a cached heatmap) and as the source for building. */
    point2_t target = LocationToPathMap(&goalentity->s.origin2);
    if (!is_pathable_node(target.x, target.y)) {
        closest_pathable_node(&goalentity->s.origin2, 0, &target);
    }

    /* Cache lookup — find slot with matching target cell.
     * On a hit: point active_flow at the cached flow field and return
     * immediately.  No memcpy of the raw heatmap needed. */
    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        if (heatmap_cache[i].target.x == target.x &&
            heatmap_cache[i].target.y == target.y &&
            heatmap_cache[i].flow) {
            heatmap_lru[i] = heatmap_lru_clock++;
            active_flow = heatmap_cache[i].flow;
            PERF_INC(cache_hits);
            return heatmap_cache[i].generation;
        }
    }
    PERF_INC(cache_misses);

    /* Cache miss — evict the least-recently-used slot.
     * Prefer slots with generation==0 before comparing LRU timestamps. */
    int evict = 0;
    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        if (!heatmap_cache[i].generation) { evict = i; break; }
        if (!heatmap_cache[evict].generation) break;
        if (heatmap_lru[i] < heatmap_lru[evict]) evict = i;
    }
    if (!heatmap_cache[evict].flow)
        heatmap_cache[evict].flow = MemAlloc(map_cells * sizeof(VECTOR2));

    reset_pathmap_data();
    clear_heatmap();
    /* Static obstacles are already baked into pathmap.original by
     * CM_BakeStaticObstacles(); no per-frame entity scan needed here. */
    build_heatmap(target);
    bake_flow_field(heatmap_cache[evict].flow);

    heatmap_cache[evict].target    = target;
    heatmap_cache[evict].generation = heatmap_next_generation++;
    if (heatmap_next_generation == 0)
        heatmap_next_generation = 1;
    heatmap_lru[evict] = heatmap_lru_clock++;
    active_flow = heatmap_cache[evict].flow;

    return heatmap_cache[evict].generation;
}

#ifdef TOOL_COMMON_NO_MPQ
/* Synthesize a pathmap from a raw byte array for unit tests.
 * Each byte is treated as a pathMapCell_t (bit 1 = nowalk).
 * The world coordinate system is set up so cell (x,y) maps to
 * world position (x * cell_size, y * cell_size). */
void CM_SetupTestPathmap(DWORD width, DWORD height, BYTE const *cells) {
    CM_SetupPathMap(width, height, cells);
}
#endif

#ifndef TOOL_COMMON_NO_MPQ
void CM_ReadPathMap(HANDLE archive) {
    HANDLE file;
    DWORD header, version;
    DWORD width, height;
    LPBYTE cells;
    heatmap_cache_invalidate();
    if (!SFileOpenFileEx(archive, "war3map.wpm", SFILE_OPEN_FROM_MPQ, &file)) {
        CM_SetupPathMap(world.map ? world.map->width : 0, world.map ? world.map->height : 0, NULL);
        return;
    }
    SFileReadFile(file, &header, 4, NULL, NULL);
    SFileReadFile(file, &version, 4, NULL, NULL);
    SFileReadFile(file, &width, 4, NULL, NULL);
    SFileReadFile(file, &height, 4, NULL, NULL);
    if (!width || !height) {
        SFileCloseFile(file);
        CM_SetupPathMap(0, 0, NULL);
        return;
    }
    cells = MemAlloc(width * height);
    SFileReadFile(file, cells, width * height, 0, 0);
    SFileCloseFile(file);
    CM_SetupPathMap(width, height, cells);
    MemFree(cells);

#ifdef DEBUG_PATHFINDING
    pathDebug = MemAlloc(pathmap.width * pathmap.height * sizeof(COLOR32));
    CM_FillDebugObstacles();
#endif
}
#endif /* !TOOL_COMMON_NO_MPQ */
