#include "game/g_local.h"

#define ge (&globals)
#ifndef EDICT_NUM
#define EDICT_NUM(n) (globals.edicts + (n))
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
    int parent, f, g, heap_pos;
    DWORD stamp;
    BOOL closed;
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
    pathMapCell_t *terrain;  /* immutable WPM terrain before entity footprints */
    pathMapCell_t *original;
    pathMapCell_t *data;
    routeNode_t *heatmap;
    DWORD *queue;          /* SPFA relaxation queue (ring buffer, width*height+1) */
    pathNode_t *pathnodes; /* generation-stamped scratch nodes for bounded point routes */
    DWORD *pathheap;       /* binary min-heap of pathmap indexes */
    DWORD *obstacle_prefix; /* summed-area table for static nowalk cells */
    BYTE *approach_mask;    /* reusable footprint-approach candidate mask */
} pathmap = { 0 };

#define HEATMAP_CACHE_SLOTS 4

typedef struct {
    point2_t target;    /* pathmap cell coordinate of the goal; {-1,-1} = invalid */
    int      radius_cells; /* mover footprint used when this field was built */
    DWORD    generation;
    int     *prices;      /* cached distance-to-goal price per cell */
} heatmapCacheEntry_t;

static heatmapCacheEntry_t heatmap_cache[HEATMAP_CACHE_SLOTS];
static DWORD    heatmap_next_generation = 1;
static int      heatmap_lru[HEATMAP_CACHE_SLOTS];
static int      heatmap_lru_clock = 0;
static heatmapCacheEntry_t *active_heatmap = NULL;

typedef struct {
    BOOL active;
    BOOL started;
    point2_t target;
    int radius_cells;
    DWORD head;
    DWORD tail;
} heatmapJob_t;

/* Generic game routing is built incrementally.  The old game-side cap allowed
 * only two synchronous whole-map floods for the lifetime of the map, which
 * made later reachable right-click destinations fall back to straight-line
 * steering and stop at trees/buildings.  Keep one resumable build here so the
 * expensive relaxation work is bounded per simulation frame. */
static heatmapJob_t heatmap_job = { 0 };
static DWORD path_search_stamp = 0;

#define PATH_ACCEL_MAX_EXPANSIONS 2048 // nodes/request; bounds immediate point-route work before shared-field fallback
#define PATH_ACCEL_MAX_DISTANCE 48 // pathing cells/axis; limits the accelerator to nearby obstacle detours

static void heatmap_job_cancel(void) {
    memset(&heatmap_job, 0, sizeof(heatmap_job));
}

#if defined(TOOL_COMMON_NO_MPQ) || defined(BZ_TESTS)
/* Per-call perf counters; only tracked in test builds to avoid overhead. */
static struct {
    DWORD cache_hits, cache_misses, heatmap_iterations, flow_cells_computed;
} g_perf;

void CM_ResetTestPathPerfStats(void) { memset(&g_perf, 0, sizeof(g_perf)); }

typedef struct routePerfStats_s {
    DWORD cache_hits, cache_misses, heatmap_iterations, flow_cells_computed;
} routePerfStats_t;

routePerfStats_t CM_GetTestPathPerfStats(void) {
    return (routePerfStats_t){
        g_perf.cache_hits, g_perf.cache_misses,
        g_perf.heatmap_iterations, g_perf.flow_cells_computed,
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
        heatmap_cache[i].radius_cells = -1;
        heatmap_cache[i].generation = 0;
        /* Keep price buffers allocated to avoid malloc churn on map reload. */
    }
    heatmap_next_generation = 1;
    heatmap_lru_clock       = 0;
    active_heatmap          = NULL;
    memset(heatmap_lru, 0, sizeof(heatmap_lru));
    heatmap_job_cancel();
}

void CM_InvalidatePathCache(void) {
    heatmap_cache_invalidate();
}

/* Activate the cached integration field for a generation without rebuilding.
 * The public name is retained for callers, but cache entries now store prices
 * rather than a pre-baked VECTOR2 for every map cell. */
BOOL CM_ActivateCachedFlow(DWORD generation) {
    if (!generation)
        return false;
    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        if (heatmap_cache[i].generation == generation && heatmap_cache[i].prices) {
            active_heatmap = &heatmap_cache[i];
            heatmap_lru[i] = heatmap_lru_clock++;
            return true;
        }
    }
    return false;
}

BOOL CM_FlowReachedGoal(DWORD generation, FLOAT x, FLOAT y) {
    VECTOR2 n;
    int cx, cy;

    if (!generation || !pathmap.width || !pathmap.height)
        return false;

    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        if (heatmap_cache[i].generation != generation || !heatmap_cache[i].prices)
            continue;
        n = CM_GetNormalizedMapPosition(x, y);
        cx = (int)floorf(n.x * pathmap.width);
        cy = (int)floorf(n.y * pathmap.height);
        return cx == (int)heatmap_cache[i].target.x &&
               cy == (int)heatmap_cache[i].target.y;
    }
    return false;
}

BOOL CM_FlowCanReach(DWORD generation, FLOAT x, FLOAT y) {
    VECTOR2 n;
    int cx, cy;

    if (!generation || !pathmap.width || !pathmap.height)
        return false;

    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        if (heatmap_cache[i].generation != generation || !heatmap_cache[i].prices)
            continue;
        n = CM_GetNormalizedMapPosition(x, y);
        cx = (int)floorf(n.x * pathmap.width);
        cy = (int)floorf(n.y * pathmap.height);
        if (cx < 0 || cy < 0 || cx >= (int)pathmap.width || cy >= (int)pathmap.height)
            return false;
        return heatmap_cache[i].prices[cx + cy * pathmap.width] != INT_MAX;
    }
    return false;
}

static void rebuild_static_obstacle_prefix(void) {
    DWORD const stride = pathmap.width + 1;
    DWORD const rows = pathmap.height + 1;

    if (!pathmap.obstacle_prefix || !pathmap.original)
        return;

    memset(pathmap.obstacle_prefix, 0, stride * rows * sizeof(DWORD));
    FOR_LOOP(y, pathmap.height) {
        DWORD row_sum = 0;
        FOR_LOOP(x, pathmap.width) {
            row_sum += pathmap.original[x + y * pathmap.width].nowalk ? 1 : 0;
            pathmap.obstacle_prefix[(x + 1) + (y + 1) * stride] =
                pathmap.obstacle_prefix[(x + 1) + y * stride] + row_sum;
        }
    }
}

void CM_SetupPathMap(DWORD width, DWORD height, BYTE const *cells) {
    DWORD n = width * height;

    SAFE_DELETE(pathmap.data, MemFree);
    SAFE_DELETE(pathmap.terrain, MemFree);
    SAFE_DELETE(pathmap.original, MemFree);
    SAFE_DELETE(pathmap.heatmap, MemFree);
    SAFE_DELETE(pathmap.queue, MemFree);
    SAFE_DELETE(pathmap.pathnodes, MemFree);
    SAFE_DELETE(pathmap.pathheap, MemFree);
    SAFE_DELETE(pathmap.obstacle_prefix, MemFree);
    SAFE_DELETE(pathmap.approach_mask, MemFree);
    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        SAFE_DELETE(heatmap_cache[i].prices, MemFree);
    }

    pathmap.width = width;
    pathmap.height = height;
    if (!n) {
        heatmap_cache_invalidate();
        return;
    }

    pathmap.data = MemAlloc(n);
    pathmap.terrain = MemAlloc(n);
    pathmap.original = MemAlloc(n);
    pathmap.heatmap = MemAlloc(n * sizeof(routeNode_t));
    pathmap.queue = MemAlloc((n + 1) * sizeof(DWORD));
    pathmap.pathnodes = MemAlloc(n * sizeof(pathNode_t));
    pathmap.pathheap = MemAlloc(n * sizeof(DWORD));
    pathmap.obstacle_prefix = MemAlloc((width + 1) * (height + 1) * sizeof(DWORD));
    pathmap.approach_mask = MemAlloc(n);

    if (cells) {
        memcpy(pathmap.terrain, cells, n);
    } else {
        memset(pathmap.terrain, 0, n);
    }
    memcpy(pathmap.original, pathmap.terrain, n);
    memcpy(pathmap.data, pathmap.original, n);
    memset(pathmap.heatmap, 0, n * sizeof(routeNode_t));
    memset(pathmap.pathnodes, 0, n * sizeof(pathNode_t));
    memset(pathmap.approach_mask, 0, n);
    rebuild_static_obstacle_prefix();

    heatmap_cache_invalidate();
}

static point2_t LocationToPathMap(LPCVECTOR2 location);

static int const dx[] = {-1, 1, 0, 0, -1, -1, 1, 1};
static int const dy[] = {0, 0, -1, 1, -1, 1, -1, 1};
static int const gv[] = {10, 10, 10, 10, 14, 14, 14, 14};



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

static bool is_pathable_node_original_for_radius_cells(int x, int y, int radius_cells);

static void begin_heatmap_build(heatmapJob_t *job, point2_t target, int radius_cells) {
    DWORD const width = pathmap.width;
    DWORD const ti = (DWORD)target.x + (DWORD)target.y * width;

    clear_heatmap();
    job->target = target;
    job->radius_cells = radius_cells;
    job->head = 0;
    job->tail = 1;
    job->started = true;
    pathmap.heatmap[ti].price = 0;
    pathmap.heatmap[ti].closed = true;
    pathmap.queue[0] = ti;
}

/* Advance one reverse shortest-path build by at most work_budget queue pops.
 * Returns true when the relaxation queue is empty.  Keeping the queue/head/tail
 * in heatmapJob_t lets game routing spread a large map flood over many frames,
 * while the synchronous test/tool API can run the same implementation to
 * completion in one call. */
static BOOL step_heatmap_build(heatmapJob_t *job, DWORD work_budget) {
    DWORD const width = pathmap.width;
    DWORD const cap = pathmap.width * pathmap.height + 1;
    DWORD *const q = pathmap.queue;
    DWORD work = 0;

    while (job->head != job->tail && work < work_budget) {
        DWORD const u = q[job->head];
        job->head = (job->head + 1) % cap;
        work++;
        PERF_INC(heatmap_iterations);
        routeNode_t *const un = &pathmap.heatmap[u];
        un->closed = false;
        int const up = un->price;
        int const ux = (int)(u % width);
        int const uy = (int)(u / width);
        FOR_LOOP(i, 8) {
            int const nx = ux + dx[i];
            int const ny = uy + dy[i];
            if (!is_pathable_node_original_for_radius_cells(nx, ny, job->radius_cells))
                continue;
            if (i >= 4 && !(is_pathable_node_original_for_radius_cells(nx, uy, job->radius_cells) &&
                            is_pathable_node_original_for_radius_cells(ux, ny, job->radius_cells)))
                continue;
            DWORD const v = (DWORD)nx + (DWORD)ny * width;
            routeNode_t *const vn = &pathmap.heatmap[v];
            int const np = up + gv[i];
            if (np < vn->price) {
                vn->price = np;
                if (!vn->closed) {
                    vn->closed = true;
                    q[job->tail] = v;
                    job->tail = (job->tail + 1) % cap;
                }
            }
        }
    }
    return job->head == job->tail;
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
    } else if (!(ent->svflags & SVF_MONSTER) && ent->collision > 0.0f) {
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

static BOOL entity_blocks_static_pathing(edict_t const *ent) {
    if (!ent || !ent->inuse || (ent->s.renderfx & RF_HIDDEN)) return false;
    /* A dead object's replacement path texture remains authoritative; dead
     * entities without one no longer contribute their alive collision. */
    if (ent->pathtex) return true;
    if (ent->svflags & SVF_DEADMONSTER) return false;
    return !(ent->svflags & SVF_MONSTER) && ent->collision > 0.0f;
}

/* Rebuild current static obstacles from the immutable terrain baseline.  This
 * is normally called once after map spawning, and again only when a static
 * footprint changes (building creation or destructable death). */
void CM_BakeStaticObstacles(void) {
    DWORD const cells = pathmap.width * pathmap.height;

    if (!pathmap.terrain || !pathmap.original)
        return;
    memcpy(pathmap.original, pathmap.terrain, cells);
    FOR_LOOP(i, ge->num_edicts) {
        edict_t *ent = EDICT_NUM(i);
        if (!entity_blocks_static_pathing(ent))
            continue;
        stamp_entity_obstacle(ent, pathmap.original);
    }
    if (pathmap.data) {
        memcpy(pathmap.data, pathmap.original, cells);
    }
    rebuild_static_obstacle_prefix();
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

static void pathmap_cell_world_dimensions(FLOAT *cell_x, FLOAT *cell_y) {
    *cell_x = FLT_MAX;
    *cell_y = FLT_MAX;

    if (pathmap.width > 0) {
        VECTOR2 a = CM_GetDenormalizedMapPosition(0, 0);
        VECTOR2 b = CM_GetDenormalizedMapPosition(1.f / pathmap.width, 0);
        *cell_x = fabsf(b.x - a.x);
    }
    if (pathmap.height > 0) {
        VECTOR2 a = CM_GetDenormalizedMapPosition(0, 0);
        VECTOR2 b = CM_GetDenormalizedMapPosition(0, 1.f / pathmap.height);
        *cell_y = fabsf(b.y - a.y);
    }
}

static FLOAT pathmap_cell_world_size(void) {
    FLOAT cell_x, cell_y;

    pathmap_cell_world_dimensions(&cell_x, &cell_y);
    return MAX(1.f, MIN(cell_x, cell_y));
}

FLOAT CM_PathCellWorldSize(void) {
    return pathmap_cell_world_size();
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
    VECTOR2 n;
    int tx, ty, radius_cells;

    if (!location || !out) {
        return false;
    }
    if (!pathmap.data || !pathmap.original) {
        *out = *location;
        return true;
    }

    reset_pathmap_data();
    apply_dynamic_obstacles(NULL);
    n = CM_GetNormalizedMapPosition(location->x, location->y);
    tx = (int)floorf(n.x * pathmap.width);
    ty = (int)floorf(n.y * pathmap.height);
    radius_cells = (int)ceilf(MAX(0.f, radius) / pathmap_cell_world_size());
    /* A legal click is already the most accurate destination; the old code snapped every
     * valid point to its cell center, changing straight orders into diagonal movement. */
    if (is_pathable_node_for_radius_cells(tx, ty, radius_cells)) {
        *out = *location;
        return true;
    }
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
    int const x0 = x - radius_cells;
    int const y0 = y - radius_cells;
    int const x1 = x + radius_cells;
    int const y1 = y + radius_cells;
    DWORD stride, blocked;

    if (x0 < 0 || y0 < 0 || x1 >= (int)pathmap.width || y1 >= (int)pathmap.height)
        return false;
    if (!pathmap.obstacle_prefix)
        return is_pathable_node_original(x, y);

    /* O(1) square-footprint test from the summed-area table.  Radius-aware
     * heatmap expansion previously re-scanned this whole square for every
     * neighbour of every visited cell. */
    stride = pathmap.width + 1;
    blocked = pathmap.obstacle_prefix[(x1 + 1) + (y1 + 1) * stride]
            - pathmap.obstacle_prefix[x0 + (y1 + 1) * stride]
            - pathmap.obstacle_prefix[(x1 + 1) + y0 * stride]
            + pathmap.obstacle_prefix[x0 + y0 * stride];
    return blocked == 0;
}

static bool closest_pathable_node_original(LPCVECTOR2 location, FLOAT radius, point2_t *out) {
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

    if (is_pathable_node_original_for_radius_cells(tx, ty, radius_cells)) {
        *out = (point2_t){ tx, ty };
        return true;
    }
    for (int search_radius = 1; search_radius <= max_radius && !found; search_radius++) {
        for (int y = ty - search_radius; y <= ty + search_radius; y++) {
            for (int x = tx - search_radius; x <= tx + search_radius; x++) {
                FLOAT cx, cy, dist;
                if (x != tx - search_radius && x != tx + search_radius &&
                    y != ty - search_radius && y != ty + search_radius)
                    continue;
                if (!is_pathable_node_original_for_radius_cells(x, y, radius_cells))
                    continue;
                cx = x + 0.5f;
                cy = y + 0.5f;
                dist = (cx - fx) * (cx - fx) + (cy - fy) * (cy - fy);
                if (!found || dist < best_dist) {
                    best_dist = dist;
                    best = (point2_t){ x, y };
                    found = true;
                }
            }
        }
    }
    if (found)
        *out = best;
    return found;
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
 * pathmap cells along the segment (Bresenham) and fail on the first position
 * where the mover's full collision footprint would overlap static pathing.
 * O(cells on the line) — vastly cheaper than a full flow-field bake, so a unit
 * chasing a target in the open can steer directly instead of flood-filling. */

BOOL CM_GetPathingFlagsAt(LPCVECTOR2 location, LPBYTE flags) {
    VECTOR2 n;
    int x, y;

    if (flags) *flags = 0;
    if (!location || !flags || !pathmap.original || !pathmap.width || !pathmap.height) return false;
    n = CM_GetNormalizedMapPosition(location->x, location->y);
    x = (int)floorf(n.x * pathmap.width);
    y = (int)floorf(n.y * pathmap.height);
    if (x < 0 || y < 0 || !is_valid_point((DWORD)x, (DWORD)y)) return false;
    memcpy(flags, &pathmap.original[x + y * pathmap.width], sizeof(*flags));
    return true;
}

BOOL CM_LineIsWalkableForRadius(LPCVECTOR2 a, LPCVECTOR2 b, FLOAT radius) {
    if (!a || !b)
        return false;
    if (pathmap.width == 0 || pathmap.height == 0)
        return true;
    int radius_cells = (int)ceilf(MAX(0.f, radius) / pathmap_cell_world_size());
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
        if (!is_pathable_node_original_for_radius_cells(x, y, radius_cells))
            return false;
        if (x == bx && y == by) {
            return true;
        }
        int e2 = 2 * err;
        BOOL const step_x = e2 > -dy;
        BOOL const step_y = e2 < dx;
        /* A simultaneous Bresenham step crosses a cell corner.  Check both
         * cardinal neighbours so the direct shortcut cannot bypass the
         * flow-field rule and steer through touching obstacle corners. */
        if (step_x && step_y &&
            !(is_pathable_node_original_for_radius_cells(x + sx, y, radius_cells) &&
              is_pathable_node_original_for_radius_cells(x, y + sy, radius_cells)))
            return false;
        if (step_x) { err -= dy; x += sx; }
        if (step_y) { err += dx; y += sy; }
    }
    return true;
}

BOOL CM_LineIsWalkable(LPCVECTOR2 a, LPCVECTOR2 b) {
    return CM_LineIsWalkableForRadius(a, b, 0);
}

static int path_octile(int ax, int ay, int bx, int by) {
    int const x = abs(ax - bx), y = abs(ay - by);
    return 10 * MAX(x, y) + 4 * MIN(x, y);
}

static BOOL path_heap_less(DWORD a, DWORD b) {
    pathNode_t const *an = &pathmap.pathnodes[a], *bn = &pathmap.pathnodes[b];
    return an->f < bn->f || (an->f == bn->f && an->g > bn->g);
}

static void path_heap_swap(DWORD a, DWORD b) {
    DWORD const tmp = pathmap.pathheap[a];
    pathmap.pathheap[a] = pathmap.pathheap[b]; pathmap.pathheap[b] = tmp;
    pathmap.pathnodes[pathmap.pathheap[a]].heap_pos = (int)a;
    pathmap.pathnodes[pathmap.pathheap[b]].heap_pos = (int)b;
}

static void path_heap_up(DWORD pos) {
    while (pos) {
        DWORD const parent = (pos - 1) / 2;
        if (!path_heap_less(pathmap.pathheap[pos], pathmap.pathheap[parent])) break;
        path_heap_swap(pos, parent); pos = parent;
    }
}

static DWORD path_heap_pop(DWORD *count) {
    DWORD const result = pathmap.pathheap[0];
    pathmap.pathnodes[result].heap_pos = -1;
    if (!--*count) return result;
    pathmap.pathheap[0] = pathmap.pathheap[*count]; pathmap.pathnodes[pathmap.pathheap[0]].heap_pos = 0;
    for (DWORD pos = 0;;) {
        DWORD child = pos * 2 + 1;
        if (child >= *count) break;
        if (child + 1 < *count && path_heap_less(pathmap.pathheap[child + 1], pathmap.pathheap[child])) child++;
        if (!path_heap_less(pathmap.pathheap[child], pathmap.pathheap[pos])) break;
        path_heap_swap(pos, child); pos = child;
    }
    return result;
}

/* Retail keeps a compact path object per mover and consults a separate pathing
 * accelerator before its longer-lived route state. This bounded A* supplies
 * the same useful behavior for nearby detours: return one persistent waypoint
 * immediately, while long searches remain on the shared incremental field. */
BOOL CM_FindPathWaypoint(pathAccelParams_t const *params, LPVECTOR2 out) {
    point2_t start, target;
    DWORD heap_count = 0, expanded = 0, cells = pathmap.width * pathmap.height;
    int radius_cells;

    if (!params || !params->from || !params->target || !out || !pathmap.pathnodes || !pathmap.pathheap || !cells)
        return false;
    radius_cells = (int)ceilf(MAX(0.f, params->radius) / pathmap_cell_world_size());
    if (!closest_pathable_node_original(params->from, params->radius, &start) ||
        !closest_pathable_node_original(params->target, params->radius, &target) ||
        abs(start.x - target.x) > PATH_ACCEL_MAX_DISTANCE || abs(start.y - target.y) > PATH_ACCEL_MAX_DISTANCE)
        return false;

    if (++path_search_stamp == 0) {
        FOR_LOOP(i, cells) pathmap.pathnodes[i].stamp = 0;
        path_search_stamp = 1;
    }
    DWORD const start_index = (DWORD)start.x + (DWORD)start.y * pathmap.width;
    DWORD const target_index = (DWORD)target.x + (DWORD)target.y * pathmap.width;
    pathNode_t *node = &pathmap.pathnodes[start_index];
    *node = (pathNode_t){ .parent = -1, .f = path_octile(start.x, start.y, target.x, target.y),
                         .g = 0, .heap_pos = 0, .stamp = path_search_stamp };
    pathmap.pathheap[heap_count++] = start_index;

    while (heap_count && expanded++ < PATH_ACCEL_MAX_EXPANSIONS) {
        DWORD const current = path_heap_pop(&heap_count);
        int const cx = (int)(current % pathmap.width), cy = (int)(current / pathmap.width);
        node = &pathmap.pathnodes[current]; node->closed = true;
        if (current == target_index) {
            DWORD count = 0;
            for (int at = (int)current; at >= 0 && count < cells; at = pathmap.pathnodes[at].parent)
                pathmap.pathheap[count++] = (DWORD)at;
            for (DWORD i = 0; i + 1 < count; i++) {
                DWORD const at = pathmap.pathheap[i];
                VECTOR2 candidate = CM_GetDenormalizedMapPosition(((FLOAT)(at % pathmap.width) + 0.5f) / pathmap.width,
                    ((FLOAT)(at / pathmap.width) + 0.5f) / pathmap.height);
                if (CM_LineIsWalkableForRadius(params->from, &candidate, params->radius)) {
                    *out = candidate;
                    return true;
                }
            }
            return false;
        }
        FOR_LOOP(dir, 8) {
            int const nx = cx + dx[dir], ny = cy + dy[dir];
            if (!is_pathable_node_original_for_radius_cells(nx, ny, radius_cells) ||
                (dir >= 4 && !(is_pathable_node_original_for_radius_cells(nx, cy, radius_cells) &&
                              is_pathable_node_original_for_radius_cells(cx, ny, radius_cells)))) continue;
            DWORD const next = (DWORD)nx + (DWORD)ny * pathmap.width;
            pathNode_t *next_node = &pathmap.pathnodes[next];
            int const next_g = node->g + gv[dir];
            if (next_node->stamp == path_search_stamp && (next_node->closed || next_g >= next_node->g)) continue;
            if (next_node->stamp != path_search_stamp)
                *next_node = (pathNode_t){ .heap_pos = -1, .stamp = path_search_stamp };
            next_node->parent = (int)current; next_node->g = next_g;
            next_node->f = next_g + path_octile(nx, ny, target.x, target.y);
            if (next_node->heap_pos < 0) {
                next_node->heap_pos = (int)heap_count;
                pathmap.pathheap[heap_count++] = next; path_heap_up(heap_count - 1);
            } else path_heap_up((DWORD)next_node->heap_pos);
        }
    }
    return false;
}

BOOL CM_FindDirectApproachPointForRadius(LPCVECTOR2 from, LPCVECTOR2 target,
                                             FLOAT range, FLOAT radius, LPVECTOR2 out) {
    VECTOR2 n;
    FLOAT const cell_size = pathmap_cell_world_size();
    int tx, ty, search_cells, radius_cells;
    FLOAT best_dist2 = FLT_MAX;
    BOOL found = false;

    if (!from || !target || !out || range < 0.0f ||
        !pathmap.original || !pathmap.width || !pathmap.height)
        return false;

    n = CM_GetNormalizedMapPosition(target->x, target->y);
    tx = (int)floorf(n.x * pathmap.width);
    ty = (int)floorf(n.y * pathmap.height);
    search_cells = (int)ceilf(range / cell_size) + 1;
    radius_cells = (int)ceilf(MAX(0.f, radius) / cell_size);

    /* A behavior needs an interaction point, not the blocked target centre.
     * Search only cells inside the small interaction disc and choose the
     * directly visible legal cell nearest the mover. */
    for (int y = ty - search_cells; y <= ty + search_cells; y++) {
        for (int x = tx - search_cells; x <= tx + search_cells; x++) {
            VECTOR2 candidate;
            FLOAT dxw, dyw, dist2;

            if (!is_pathable_node_original_for_radius_cells(x, y, radius_cells))
                continue;
            candidate = CM_GetDenormalizedMapPosition((x + 0.5f) / pathmap.width,
                                                       (y + 0.5f) / pathmap.height);
            if (Vector2_distance(&candidate, target) > range)
                continue;
            if (!CM_LineIsWalkableForRadius(from, &candidate, radius))
                continue;

            dxw = candidate.x - from->x;
            dyw = candidate.y - from->y;
            dist2 = dxw * dxw + dyw * dyw;
            if (!found || dist2 < best_dist2) {
                best_dist2 = dist2;
                *out = candidate;
                found = true;
            }
        }
    }
    return found;
}

FLOAT CM_DistanceToPathingFootprint(struct edict_s const *target, LPCVECTOR2 point) {
    point2_t center;
    pathTex_t const *pt;
    FLOAT best = FLT_MAX;

    if (!target || !point || !(pt = target->pathtex) ||
        !pathmap.width || !pathmap.height)
        return FLT_MAX;

    center = LocationToPathMap(&target->s.origin2);
    FOR_LOOP(x, pt->width) {
        FOR_LOOP(y, pt->height) {
            int const px = (int)x + center.x - (int)pt->width / 2;
            int const py = (int)y + center.y - (int)pt->height / 2;
            VECTOR2 a, b;
            FLOAT min_x, max_x, min_y, max_y, dx = 0.0f, dy = 0.0f;

            if (!pt->map[x + y * pt->width].b || !is_valid_point(px, py))
                continue;

            /* Use the exact same cell placement as stamp_entity_obstacle(),
             * but measure to the blocked cell rectangle instead of reducing a
             * square/irregular footprint to one collision circle. */
            a = CM_GetDenormalizedMapPosition((FLOAT)px / pathmap.width,
                                               (FLOAT)py / pathmap.height);
            b = CM_GetDenormalizedMapPosition((FLOAT)(px + 1) / pathmap.width,
                                               (FLOAT)(py + 1) / pathmap.height);
            min_x = MIN(a.x, b.x); max_x = MAX(a.x, b.x);
            min_y = MIN(a.y, b.y); max_y = MAX(a.y, b.y);
            if (point->x < min_x) dx = min_x - point->x;
            else if (point->x > max_x) dx = point->x - max_x;
            if (point->y < min_y) dy = min_y - point->y;
            else if (point->y > max_y) dy = point->y - max_y;
            best = MIN(best, sqrtf(dx * dx + dy * dy));
        }
    }
    return best;
}

BOOL CM_FindApproachPointToFootprintForRadius(struct edict_s const *target,
                                                LPCVECTOR2 from, FLOAT range,
                                                FLOAT radius, LPVECTOR2 out) {
    pathTex_t const *pt;
    point2_t center;
    FLOAT cell_x, cell_y;
    FLOAT cell_size;
    FLOAT const range_sq = range * range;
    FLOAT best_direct_dist2 = FLT_MAX;
    FLOAT best_any_dist2 = FLT_MAX;
    VECTOR2 best_direct = { 0, 0 };
    VECTOR2 best_any = { 0, 0 };
    int radius_cells, padding_cells, reach_x, reach_y;
    int min_x, max_x, min_y, max_y;
    BOOL found_direct = false;
    BOOL found_any = false;

    if (!target || !from || !out || range < 0.0f ||
        !(pt = target->pathtex) || !pathmap.original || !pathmap.approach_mask ||
        !pathmap.width || !pathmap.height) {
        return false;
    }

    pathmap_cell_world_dimensions(&cell_x, &cell_y);
    cell_x = MAX(1.0f, cell_x);
    cell_y = MAX(1.0f, cell_y);
    cell_size = MIN(cell_x, cell_y);
    center = LocationToPathMap(&target->s.origin2);
    radius_cells = (int)ceilf(MAX(0.0f, radius) / cell_size);
    padding_cells = (int)ceilf(MAX(0.0f, range) / cell_size) + radius_cells + 1;
    min_x = MAX(0, center.x - (int)pt->width / 2 - padding_cells);
    max_x = MIN((int)pathmap.width - 1,
                center.x - (int)pt->width / 2 + (int)pt->width - 1 + padding_cells);
    min_y = MAX(0, center.y - (int)pt->height / 2 - padding_cells);
    max_y = MIN((int)pathmap.height - 1,
                center.y - (int)pt->height / 2 + (int)pt->height - 1 + padding_cells);
    if (min_x > max_x || min_y > max_y)
        return false;

    /* The previous implementation called CM_DistanceToPathingFootprint for
     * every candidate cell.  That helper scans every authored footprint pixel,
     * turning one small edge search into candidate_count * footprint_area work
     * every time a worker adjusted its lane.  Build an exact reusable mask of
     * cells that lie within range of any blocked footprint pixel instead. */
    for (int y = min_y; y <= max_y; y++)
        memset(&pathmap.approach_mask[min_x + y * pathmap.width], 0,
               (size_t)(max_x - min_x + 1));

    reach_x = (int)ceilf(range / cell_x + 0.5f);
    reach_y = (int)ceilf(range / cell_y + 0.5f);
    FOR_LOOP(px_local, pt->width) {
        FOR_LOOP(py_local, pt->height) {
            int const px = (int)px_local + center.x - (int)pt->width / 2;
            int const py = (int)py_local + center.y - (int)pt->height / 2;
            int const x0 = MAX(min_x, px - reach_x);
            int const x1 = MIN(max_x, px + reach_x);
            int const y0 = MAX(min_y, py - reach_y);
            int const y1 = MIN(max_y, py + reach_y);

            if (!pt->map[px_local + py_local * pt->width].b ||
                !is_valid_point(px, py))
                continue;

            for (int y = y0; y <= y1; y++) {
                int const dy_cells = abs(y - py);
                FLOAT const dyw = dy_cells > 0
                    ? ((FLOAT)dy_cells - 0.5f) * cell_y : 0.0f;
                FLOAT const dy2 = dyw * dyw;

                if (dy2 > range_sq)
                    continue;
                for (int x = x0; x <= x1; x++) {
                    int const dx_cells = abs(x - px);
                    FLOAT const dxw = dx_cells > 0
                        ? ((FLOAT)dx_cells - 0.5f) * cell_x : 0.0f;

                    if (dxw * dxw + dy2 <= range_sq + 0.001f)
                        pathmap.approach_mask[x + y * pathmap.width] = 1;
                }
            }
        }
    }

    /* Building interaction targets are blocked shapes, not reachable points.
     * Search the exact marked ring around the authored footprint. Prefer a
     * legal point with a direct static route; otherwise return the nearest
     * legal point and let the caller's collision-sized flow field route around
     * intervening terrain/buildings. */
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            VECTOR2 candidate;
            FLOAT dxw, dyw, dist2;

            if (!pathmap.approach_mask[x + y * pathmap.width] ||
                !is_pathable_node_original_for_radius_cells(x, y, radius_cells))
                continue;
            candidate = CM_GetDenormalizedMapPosition((x + 0.5f) / pathmap.width,
                                                       (y + 0.5f) / pathmap.height);
            dxw = candidate.x - from->x;
            dyw = candidate.y - from->y;
            dist2 = dxw * dxw + dyw * dyw;
            if (!found_any || dist2 < best_any_dist2) {
                best_any_dist2 = dist2;
                best_any = candidate;
                found_any = true;
            }

            /* Once a direct point has been found, a farther candidate cannot
             * improve it, so avoid another line walkability trace. */
            if ((!found_direct || dist2 < best_direct_dist2) &&
                CM_LineIsWalkableForRadius(from, &candidate, radius)) {
                best_direct_dist2 = dist2;
                best_direct = candidate;
                found_direct = true;
            }
        }
    }

    if (found_direct) {
        *out = best_direct;
        return true;
    }
    if (found_any) {
        *out = best_any;
        return true;
    }
    return false;
}

static VECTOR2 compute_flow_at(int const *prices_field, DWORD x, DWORD y, int radius_cells) {
    int prices[8];
    int min_price = INT_MAX;
    int current_price;
    VECTOR2 direction = { 0, 0 };

    if (!prices_field || !is_valid_point(x, y))
        return direction;
    current_price = prices_field[x + y * pathmap.width];
    if (current_price == INT_MAX)
        return direction;

    PERF_INC(flow_cells_computed);
    FOR_LOOP(dir, 8)
        prices[dir] = INT_MAX;

    FOR_LOOP(dir, 8) {
        int new_x = (int)x + dx[dir];
        int new_y = (int)y + dy[dir];
        int new_price;

        if (!is_pathable_node_original_for_radius_cells(new_x, new_y, radius_cells))
            continue;
        new_price = prices_field[new_x + new_y * pathmap.width];
        if (new_price == INT_MAX)
            continue;
        /* A flow field must always descend toward its integration target.
         * Allowing a point route to blend equal/higher-cost neighbours makes
         * an adjusted target beside a blocked interaction object point back
         * out into the map, which can make every unit sharing that field orbit
         * the same off-target cell.  Interaction behaviors may continue from
         * the adjusted route end toward their real target; the field itself
         * must never direct them away from the route end. */
        if (new_price >= current_price)
            continue;
        if (dir >= 4 &&
            !(is_pathable_node_original_for_radius_cells((int)x + dx[dir], (int)y, radius_cells) &&
              is_pathable_node_original_for_radius_cells((int)x, (int)y + dy[dir], radius_cells)))
            continue;
        prices[dir] = new_price;
        min_price = MIN(new_price, min_price);
    }

    FOR_LOOP(dir, 8) {
        VECTOR2 dirvec;
        float k;
        if (prices[dir] == INT_MAX)
            continue;
        k = 10.f / MAX(1, 10 + (prices[dir] - min_price));
        dirvec = (VECTOR2){ dx[dir], dy[dir] };
        Vector2_normalize(&dirvec);
        direction.x += dirvec.x * k;
        direction.y += dirvec.y * k;
    }
    return direction;
}

VECTOR2 get_flow_direction(DWORD heatmapindex, float fnx, float fny) {
    VECTOR2 n, a, b, c, d, ab, cd;
    DWORD cx, cy, cx1, cy1;
    FLOAT tx, ty;

    /* Cache only the integration prices.  Flow is derived for the four cells
     * around the current mover and interpolated on demand; route creation no
     * longer computes a vector for every reachable cell in the map. */
    if (!CM_ActivateCachedFlow(heatmapindex) || !active_heatmap ||
        !active_heatmap->prices || !pathmap.width || !pathmap.height)
        return (VECTOR2){ 0, 0 };

    n = CM_GetNormalizedMapPosition(fnx, fny);
    n.x *= pathmap.width;
    n.y *= pathmap.height;
    cx = (DWORD)floorf(n.x);
    cy = (DWORD)floorf(n.y);
    if (!is_valid_point(cx, cy))
        return (VECTOR2){ 0, 0 };

    cx1 = (cx + 1 < pathmap.width) ? cx + 1 : cx;
    cy1 = (cy + 1 < pathmap.height) ? cy + 1 : cy;
    tx = n.x - (FLOAT)cx;
    ty = n.y - (FLOAT)cy;
    a = compute_flow_at(active_heatmap->prices, cx,  cy,  active_heatmap->radius_cells);
    b = compute_flow_at(active_heatmap->prices, cx1, cy,  active_heatmap->radius_cells);
    c = compute_flow_at(active_heatmap->prices, cx1, cy1, active_heatmap->radius_cells);
    d = compute_flow_at(active_heatmap->prices, cx,  cy1, active_heatmap->radius_cells);
    ab = Vector2_lerp(&a, &b, tx);
    cd = Vector2_lerp(&d, &c, tx);
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
static BOOL resolve_heatmap_request(edict_t *goalentity, FLOAT radius,
                                    point2_t *target, int *radius_cells) {
    DWORD const map_cells = pathmap.width * pathmap.height;

    if (!goalentity || !target || !radius_cells || !pathmap.data ||
        !pathmap.original || !pathmap.heatmap || !map_cells)
        return false;

    *radius_cells = (int)ceilf(MAX(0.f, radius) / pathmap_cell_world_size());
    *target = LocationToPathMap(&goalentity->s.origin2);
    if (!is_pathable_node_original_for_radius_cells(target->x, target->y, *radius_cells)) {
        if (!closest_pathable_node_original(&goalentity->s.origin2, radius, target))
            return false;
    }
    return true;
}

/* Resolve a click to the closest legal point in the mover's static connected
 * component.  This is used only after destination-rooted routing proves the
 * mover cannot reach that component, so the whole-component flood is paid once
 * for an exceptional order rather than on every ordinary right click. */
BOOL CM_ClosestReachablePointForRadius(LPCVECTOR2 from, LPCVECTOR2 target, FLOAT radius, LPVECTOR2 out) {
    VECTOR2 n;
    point2_t start;
    heatmapJob_t job = { 0 };
    FLOAT tx, ty, best_dist = FLT_MAX;
    int radius_cells, target_x, target_y, best_x = -1, best_y = -1;

    if (!from || !target || !out || !pathmap.original || !pathmap.heatmap)
        return false;
    radius_cells = (int)ceilf(MAX(0.f, radius) / pathmap_cell_world_size());
    if (!closest_pathable_node_original(from, radius, &start))
        return false;

    begin_heatmap_build(&job, start, radius_cells);
    while (!step_heatmap_build(&job, UINT_MAX)) {
        /* UINT_MAX is already effectively unbounded for WC3 pathmap sizes. */
    }
    n = CM_GetNormalizedMapPosition(target->x, target->y);
    tx = n.x * pathmap.width;
    ty = n.y * pathmap.height;
    target_x = (int)floorf(tx);
    target_y = (int)floorf(ty);
    if (is_valid_point(target_x, target_y) &&
        pathmap.heatmap[target_x + target_y * pathmap.width].price != INT_MAX &&
        is_pathable_node_original_for_radius_cells(target_x, target_y, radius_cells)) {
        *out = *target;
        return true;
    }
    FOR_LOOP(y, pathmap.height) {
        FOR_LOOP(x, pathmap.width) {
            FLOAT dxw, dyw, dist;
            if (pathmap.heatmap[x + y * pathmap.width].price == INT_MAX)
                continue;
            dxw = (FLOAT)x + 0.5f - tx;
            dyw = (FLOAT)y + 0.5f - ty;
            dist = dxw * dxw + dyw * dyw;
            if (dist < best_dist) {
                best_dist = dist;
                best_x = x;
                best_y = y;
            }
        }
    }
    if (best_x < 0)
        return false;
    *out = CM_GetDenormalizedMapPosition(((FLOAT)best_x + 0.5f) / pathmap.width,
                                         ((FLOAT)best_y + 0.5f) / pathmap.height);
    return true;
}

static int find_cached_heatmap(point2_t target, int radius_cells) {
    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        if (heatmap_cache[i].generation &&
            heatmap_cache[i].target.x == target.x &&
            heatmap_cache[i].target.y == target.y &&
            heatmap_cache[i].radius_cells == radius_cells &&
            heatmap_cache[i].prices) {
            heatmap_lru[i] = heatmap_lru_clock++;
            active_heatmap = &heatmap_cache[i];
            return i;
        }
    }
    return -1;
}

static int choose_heatmap_cache_slot(void) {
    int evict = 0;

    FOR_LOOP(i, HEATMAP_CACHE_SLOTS) {
        if (!heatmap_cache[i].generation)
            return i;
        if (heatmap_lru[i] < heatmap_lru[evict])
            evict = i;
    }
    return evict;
}

static DWORD commit_heatmap(point2_t target, int radius_cells) {
    DWORD const map_cells = pathmap.width * pathmap.height;
    int const evict = choose_heatmap_cache_slot();

    if (!heatmap_cache[evict].prices)
        heatmap_cache[evict].prices = MemAlloc(map_cells * sizeof(int));
    FOR_LOOP(i, map_cells)
        heatmap_cache[evict].prices[i] = pathmap.heatmap[i].price;

    heatmap_cache[evict].target = target;
    heatmap_cache[evict].radius_cells = radius_cells;
    heatmap_cache[evict].generation = heatmap_next_generation++;
    if (heatmap_next_generation == 0)
        heatmap_next_generation = 1;
    heatmap_lru[evict] = heatmap_lru_clock++;
    active_heatmap = &heatmap_cache[evict];
    return heatmap_cache[evict].generation;
}

/* Synchronous build retained for tests/tools and callers that explicitly need
 * a completed field now.  Game movement uses CM_RequestHeatmapForRadius() so a
 * large flood does not run to completion inside one unit think. */
DWORD CM_BuildHeatmapForRadius(edict_t *goalentity, FLOAT radius) {
    point2_t target;
    int radius_cells;
    int cached;
    heatmapJob_t job = { 0 };

    if (!resolve_heatmap_request(goalentity, radius, &target, &radius_cells))
        return 0;

    cached = find_cached_heatmap(target, radius_cells);
    if (cached >= 0) {
        PERF_INC(cache_hits);
        return heatmap_cache[cached].generation;
    }
    PERF_INC(cache_misses);

    /* The synchronous API and the resumable game job share one scratch map.
     * No production gameplay caller uses this path; cancel a pending job so a
     * direct test/tool build cannot leave its queue state half-valid. */
    heatmap_job_cancel();
    begin_heatmap_build(&job, target, radius_cells);
    while (!step_heatmap_build(&job, UINT_MAX)) {
        /* UINT_MAX is already effectively unbounded for WC3 pathmap sizes. */
    }
    return commit_heatmap(target, radius_cells);
}

DWORD CM_BuildHeatmap(edict_t *goalentity) {
    return CM_BuildHeatmapForRadius(goalentity, 0);
}

/* Request a game-routing field without doing a synchronous whole-map flood.
 * A cache hit returns its generation immediately.  A miss starts (or waits for)
 * the single resumable build and returns zero until CM_ProcessPathJobs()
 * finishes it.  Repeated unit thinks naturally retry the same request, so a
 * second destination cannot be lost: it starts after the current job completes. */
DWORD CM_RequestHeatmapForRadius(edict_t *goalentity, FLOAT radius) {
    point2_t target;
    int radius_cells;
    int cached;

    if (!resolve_heatmap_request(goalentity, radius, &target, &radius_cells))
        return 0;

    cached = find_cached_heatmap(target, radius_cells);
    if (cached >= 0) {
        PERF_INC(cache_hits);
        return heatmap_cache[cached].generation;
    }

    if (heatmap_job.active)
        return 0;

    PERF_INC(cache_misses);
    heatmap_job.active = true;
    heatmap_job.started = false;
    heatmap_job.target = target;
    heatmap_job.radius_cells = radius_cells;
    return 0;
}

void CM_ProcessPathJobs(DWORD work_budget) {
    if (!heatmap_job.active || !work_budget || !pathmap.width || !pathmap.height)
        return;

    if (!heatmap_job.started)
        begin_heatmap_build(&heatmap_job, heatmap_job.target, heatmap_job.radius_cells);

    if (!step_heatmap_build(&heatmap_job, work_budget))
        return;

    commit_heatmap(heatmap_job.target, heatmap_job.radius_cells);
    heatmap_job_cancel();
}

#if defined(TOOL_COMMON_NO_MPQ) || defined(BZ_TESTS)
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
}
#endif /* !TOOL_COMMON_NO_MPQ */
