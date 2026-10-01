#include "g_local.h"
#include "../common/wc3_pathing_route.h"
#include "../common/wc3_pathing_coordinates.h"
#include "../common/wc3_pathing_placement.h"

typedef struct {
    int size;
    uint8_t flags;
    uint32_t objects;
    wc3FineBox_t target;
    bool has_target, endpoint;
    uint32_t level;
    bool *target_hit;
} moveFineGraph_t;
static wc3FineBox_t move_objects[MAX_ENTITIES];
typedef struct { moveFineGraph_t *graph; movePathQuery_t const *query; } moveObjectScan_t;
static moveObjectScan_t *move_scan;
static wc3FineSearch_t move_fine;
static wc3FineVector_t move_fine_points[BZ_WC3_FINE_NODES];

/* Routing consumes game-owned surface policy; only this edict contract contains WC3 destructable state. */
static bool entity_is_live_walkable_surface(edict_t const *ent) {
    return ent && ent->destructable.initialized && !ent->destructable.dead &&
        ent->destructable.placement_solid && ent->pathtex &&
        ent->data.DestructableData && ent->data.DestructableData->walkable;
}

/* Original widget blue regions usec2: walk, float and amphibious blockage. */
static uint8_t entity_static_pathing_flags(edict_t const *ent) { (void)ent; return 0xc2; }
static uint8_t entity_dynamic_pathing_flags(edict_t const *ent) {
    /* Query masks describe the mover; occupancy describes the encountered unit. */
    return !ent || ent->no_pathing || M_UnitMoveDisabled(ent) || (ent->aiflags & AI_FLYING) ? 0 : 0xca;
}
static bool entity_is_pathing_ignored(edict_t const *ent) {
    /* A construction-site indicator is a visible reservation, not a building
     * obstacle. Once construction starts, the real structure blocks movement. */
    return G_UnitIsStructure(ent) && (ent->s.flags & EF_NOT_SELECTABLE) && !ent->construction.active;
}

/* WC3 pathing TGAs are transposed relative to model/world axes.  Only bridge
 * targets use the authored angle to select a quarter-turn; ordinary footprints
 * retain their existing unrotated contract. */
static void entity_pathtex_transform(pathTexTransformParams_t const *params, pathTexTransform_t *transform) {
    pathTex_t const *pt = params ? params->pathtex : NULL;
    float const angle = params && params->ent ? params->ent->s.angle : 0.0f;
    int quarter;

    if (!transform || !pt) return;
    quarter = (pt->width != pt->height) + (int)lroundf(angle / ((float)M_PI / 2.0f));
    transform->turn = ((quarter % 4) + 4) % 4;
    transform->width = transform->turn & 1 ? pt->height : pt->width;
    transform->height = transform->turn & 1 ? pt->width : pt->height;
    if (!params->ent || params->ent->targtype != TARG_BRIDGE)
        transform->turn = 0, transform->width = pt->width, transform->height = pt->height;
}

static inline handle_t G_WorldReadFile(cstring_t filename, uint32_t *size) { return gi.ReadFile(filename, size); }
static inline handle_t G_WorldMemAlloc(long size) { return gi.MemAlloc(size); }
static inline void G_WorldMemFree(handle_t mem) { gi.MemFree(mem); }
static inline void G_WorldSetPriorityArchive(handle_t archive) { gi.SetPriorityArchive(archive); }
static inline BOMStatus G_WorldTextRemoveBom(string_t buffer) {
	size_t len;
	if (!buffer) return INVALID_BOM;
	len = strlen(buffer);
	if (len >= 3 && !memcmp(buffer, "\xEF\xBB\xBF", 3)) { memmove(buffer, buffer + 3, len - 2); return UTF8_BOM_FOUND; }
	if (len >= 2 && !memcmp(buffer, "\xFF\xFE", 2)) { memmove(buffer, buffer + 2, len - 1); return UTF16LE_BOM_FOUND; }
	if (len >= 2 && !memcmp(buffer, "\xFE\xFF", 2)) { memmove(buffer, buffer + 2, len - 1); return UTF16BE_BOM_FOUND; }
	return NO_BOM;
}
#define FS_ReadFile G_WorldReadFile
#define FS_FreeFile G_WorldMemFree
#define FS_SetPriorityArchive G_WorldSetPriorityArchive
#define MemAlloc G_WorldMemAlloc
#define MemFree G_WorldMemFree
#define PF_TextRemoveBom G_WorldTextRemoveBom
#define Com_Error(code, ...) gi.error(__VA_ARGS__)
/* ELF otherwise binds server world calls to the executable's client copy, leaving routing state uninitialized. */
#pragma GCC visibility push(hidden)
#include "common/world.c"
#include "common/world_w3.c"
#include "server/sv_routing.c"

/* Map extents/dimensions describe cell sizes; simulation coordinates then use
 * the direct software transform. Stock WC3 WPM cells are32 world units. */
static vec2_t move_grid_from_world(float x, float y) {
    box2_t const bounds = CM_GetWorldBounds();
    float const cx = (bounds.max.x - bounds.min.x) / pathmap.width;
    float const cy = (bounds.max.y - bounds.min.y) / pathmap.height;
    return (vec2_t){wc3_grid_coordinate(x, bounds.min.x, cx), wc3_grid_coordinate(y, bounds.min.y, cy)};
}

/* Cell centres and retained route points must not round through map fractions. */
static vec2_t move_world_from_grid(float x, float y) {
    box2_t const bounds = CM_GetWorldBounds();
    float const cx = (bounds.max.x - bounds.min.x) / pathmap.width;
    float const cy = (bounds.max.y - bounds.min.y) / pathmap.height;
    return (vec2_t){wc3_world_coordinate(x, bounds.min.x, cx), wc3_world_coordinate(y, bounds.min.y, cy)};
}

/* Original 16ee80 checks a class-sized square, biased left/up for even sizes.
 * ceil(radius) instead imposed 3/5-cell squares on retail's 1/2/3/4 classes. */
static pathGridQuery_t move_field_shape(float radius, uint8_t flags) {
    unsigned cls = wc3_fine_class(radius / pathmap_cell_world_size());
    wc3FineBox_t box = wc3_fine_cover(cls, (wc3FinePoint_t){0, 0});
    return (pathGridQuery_t){ {box.min.x, box.min.y}, {box.max.x, box.max.y},
                             normalize_blocked_flags(flags) };
}

/* Sort quantized object rectangles once per request. A cell only needs objects
 * starting in its four-column range: retail's largest fine footprint is4 cells. */
static int move_object_compare(void const *a, void const *b) {
    wc3FineBox_t const *left = a, *right = b;
    return (left->min.x > right->min.x) - (left->min.x < right->min.x);
}

/* TODO: the complete authored category table is BASE-02. This game adapter
 * uses the observed foot/horse/hover/float/amph categoryca;
 * flyers and disabled rows publish0. Buildings/destructables already own static footprints. */
static bool move_object_collect(edict_t const *ent) {
    moveFineGraph_t *graph = move_scan->graph;
    movePathQuery_t const *query = move_scan->query;
    if (ent == query->mover || ent == query->target || IS_HOLLOW(ent) || !ent->data.UnitData ||
        G_UnitIsStructure(ent) || M_UnitMoveDisabled(ent) || ent->no_pathing || ent->collision <= 0 || (ent->aiflags & AI_FLYING)) return false;
    uint32_t flags = ent->movement.velocity.x || ent->movement.velocity.y ? 0x20000000 : 0;
    uint32_t mask = graph->flags;
    mask |= mask << 24;
    if (!wc3_fine_object_blocks((wc3FineObject_t){0x010000ca, flags, true}, mask, graph->endpoint)) return false;
    vec2_t n = move_grid_from_world(ent->s.origin2.x, ent->s.origin2.y);
    wc3FinePoint_t point = { (int)floorf(n.x), (int)floorf(n.y) };
    assert(graph->objects < MAX_ENTITIES);
    move_objects[graph->objects++] = wc3_fine_cover(wc3_fine_class(ent->collision / pathmap_cell_world_size()), point);
    return false; /* Collect rectangles directly; no capped BoxEdicts pointer list. */
}

/* A segment uses area-tree pruning; a fine detour may leave that rectangle,
 * so its snapshot scans the actor set once. Neither invalidates static fields. */
static void move_query_objects(moveFineGraph_t *graph, movePathQuery_t const *query, box2_t const *bounds) {
    graph->objects = 0;
    if (!query->units || !query->mover || (query->mover->aiflags & AI_FLYING)) return;
    moveObjectScan_t scan = {graph, query};
    move_scan = &scan;
    if (bounds) {
        edict_t *unused;
        gi.BoxEdicts(bounds, &unused, 1, move_object_collect);
    } else {
        FILTER_EDICTS(ent, ent->inuse) move_object_collect(ent);
    }
    move_scan = NULL;
    qsort(move_objects, graph->objects, sizeof(*move_objects), move_object_compare);
}

/* Integer samples are monotone along each axis. Include predecessor strips,
 * then invert the largest object's half-open cover to bound its centre.
 * Use actual first/last samples: software normalization can shift the last cell. */
static box2_t move_segment_bounds(wc3FineSegment_t const *query) {
    wc3FinePoint_t a = wc3_segment_point(query, 1.f), b = wc3_segment_point(query, ceilf(query->length) - 1.f);
    wc3FinePoint_t min = {MIN(a.x, b.x), MIN(a.y, b.y)}, max = {MAX(a.x, b.x), MAX(a.y, b.y)};
    wc3FineBox_t lo = wc3_fine_cover(query->cls, min), hi = wc3_fine_cover(query->cls, max);
    wc3FineBox_t obj = wc3_fine_cover(3, (wc3FinePoint_t){0, 0});
    vec2_t p = move_world_from_grid(lo.min.x - obj.max.x, lo.min.y - obj.max.y);
    vec2_t q = move_world_from_grid(hi.max.x + 1 - obj.min.x, hi.max.y + 1 - obj.min.y);
    return (box2_t){ {MIN(p.x, q.x), MIN(p.y, q.y)}, {MAX(p.x, q.x), MAX(p.y, q.y)} };
}

/* Rectangles coexist rather than overwriting a cell: a moving object must
 * never hide an idle object occupying the same cells. */
static bool move_cell_ok(void const *data, wc3FinePoint_t pos) {
    moveFineGraph_t const *graph = data;
    if (!is_pathable_node_original_flags(pos.x, pos.y, graph->flags)) return false;
    uint32_t lo = 0, hi = graph->objects;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (move_objects[mid].min.x < pos.x - 3) lo = mid + 1;
        else hi = mid;
    }
    for (uint32_t i = lo; i < graph->objects && move_objects[i].min.x <= pos.x; i++) {
        wc3FineBox_t const *box = &move_objects[i];
        if (pos.x < box->max.x && pos.y >= box->min.y && pos.y < box->max.y) return false;
    }
    /* TODO FINE-01.6: sorted rectangles do not retain retail cell-link order.
     * A foreign blocker above still hides a coincident target. Port the actual
     * link producer before claiming overlap parity; separate targets are exact. */
    if (graph->has_target && pos.x >= graph->target.min.x && pos.x < graph->target.max.x &&
        pos.y >= graph->target.min.y && pos.y < graph->target.max.y)
        *graph->target_hit = true;
    return true;
}

/* Original744040/750100 select the nearest W3E vertex from the fine cell.
 * TODO FOOT-04:78bc90 also overlays bridge levels; recover that producer before
 * claiming forced placement across a bridge or a layered support surface. */
static uint32_t placement_terrain_level(float const *point) {
    uint32_t x = wc3_int_bits(wc3_float_bits(point[0]));
    uint32_t y = wc3_int_bits(wc3_float_bits(point[1]));
    if (x >= pathmap.width || y >= pathmap.height) return UINT32_MAX;
    x = (x + 2) / 4; y = (y + 2) / 4;
    if (x >= world.map->width || y >= world.map->height) return UINT32_MAX;
    return CM_GetWar3MapVertex(x,y)->level;
}

static bool placement_admit(void const *data, float const *point) {
    moveFineGraph_t const *graph = data;
    return placement_terrain_level(point) == graph->level;
}

/* Public SetUnitPosition's point/ring admission. CreateUnit and item drops
 * retain their separately tracked producer rather than borrowing this policy. */
bool G_FindUnitPlacementPosition(edict_t *unit, vec2_t const *requested, vec2_t *out) {
    *out = *requested;
    if (M_UnitMoveDisabled(unit)) return true;
    if (!world.map || !world.map->vertices || !pathmap.width || !pathmap.height) {
        fprintf(stderr,"WC3 placement: terrain/pathing data unavailable\n");
        return false;
    }
    vec2_t point = move_grid_from_world(requested->x,requested->y);
    uint8_t flags = M_UnitStaticPathingFlags(unit);
    moveFineGraph_t graph = {.flags = flags, .endpoint = true};
    float fine[2] = {point.x,point.y}; graph.level = placement_terrain_level(fine);
    movePathQuery_t objects = {.mover = unit, .units = true};
    move_query_objects(&graph,&objects,NULL);
    wc3FinePlacement_t query = {.point = {point.x,point.y}, .limit = 32,
        .footprint = {.cls = wc3_fine_class(unit->collision / pathmap_cell_world_size()),
                      .cell = move_cell_ok, .data = &graph}, .admit = placement_admit};
    float admitted[2];
    if (!wc3_fine_place(&query,admitted)) return false;
    *out = move_world_from_grid(admitted[0],admitted[1]);
    return true;
}

static bool move_foot_ok(moveFineGraph_t const *graph, wc3FinePoint_t pos) {
    wc3FineBox_t box = wc3_fine_cover((unsigned)graph->size - 1, pos);
    for (int y = box.min.y; y < box.max.y; y++) for (int x = box.min.x; x < box.max.x; x++)
        if (!move_cell_ok(graph, (wc3FinePoint_t){x, y})) return false;
    return true;
}

uint32_t G_RequestMovePathField(edict_t const *goal, float radius, uint8_t flags) {
    pathGridQuery_t query = move_field_shape(radius, flags);
    point2_t target;
    if (!resolve_heatmap_request(goal, &query, &target)) return 0;
    return request_heatmap_query(target, &query);
}

bool G_ActivateMovePathField(uint32_t generation, float radius, uint8_t flags) {
    pathGridQuery_t query = move_field_shape(radius, flags);
    if (!CM_ActivateCachedFlow(generation)) return false;
    if (path_queries_equal(&active_heatmap->query, &query)) return true;
    active_heatmap = NULL;
    return false;
}

bool G_ClosestReachableMovePoint(pathAccelParams_t const *params, vec2_t *out) {
    if (!params) return false;
    pathGridQuery_t query = move_field_shape(params->radius, params->blocked_flags);
    return closest_reachable_point(params, &query, out);
}

static moveFineGraph_t move_foot_shape(pathAccelParams_t const *params) {
    return (moveFineGraph_t){ .size = (int)wc3_fine_class(params->radius / pathmap_cell_world_size()) + 1,
        .flags = normalize_blocked_flags(params->blocked_flags) };
}

/* Endpoint geometry is shared by routing and the Move step validator. */
bool G_MovePathPointIsPathable(pathAccelParams_t const *params) {
    if (!params || !params->from) return false;
    if (!pathmap.width || !pathmap.height) return true;
    vec2_t n = move_grid_from_world(params->from->x, params->from->y);
    moveFineGraph_t graph = move_foot_shape(params);
    return move_foot_ok(&graph, (wc3FinePoint_t){ (int)floorf(n.x), (int)floorf(n.y) });
}

/* Keep existing nearest-ring endpoint correction while using the actual
 * class footprint. Retail public admission/exclusion remains FOOT-04. */
bool G_ClosestMovePathPoint(pathAccelParams_t const *params, vec2_t *out) {
    if (!params || !params->from || !out) return false;
    if (G_MovePathPointIsPathable(params)) { *out = *params->from; return true; }
    pathGridQuery_t query = move_field_shape(params->radius, params->blocked_flags);
    point2_t chosen;
    if (!closest_pathable_node_query(params->from, &query, &chosen)) return false;
    *out = move_world_from_grid(chosen.x + 0.5f, chosen.y + 0.5f);
    return true;
}

/* Engine admission checks both endpoints; the recovered interior sampler
 * itself leaves them unchecked and starts its previous cell at0,0. */
static bool move_query_line(movePathQuery_t const *input) {
    pathAccelParams_t const *params = &input->geometry;
    if (!params || !params->from || !params->target) return false;
    if (!pathmap.width || !pathmap.height) return true;
    pathAccelParams_t end = *params; end.from = params->target;
    if (!G_MovePathPointIsPathable(params) || !G_MovePathPointIsPathable(&end)) return false;
    vec2_t a = move_grid_from_world(params->from->x, params->from->y);
    vec2_t b = move_grid_from_world(params->target->x, params->target->y);
    moveFineGraph_t graph = move_foot_shape(params);
    wc3FineSegment_t query = { .start = {a.x, a.y},
        .cls = wc3_fine_class(params->radius / pathmap_cell_world_size()),
        .cell = move_cell_ok, .data = &graph };
    query.direction[0] = wc3_sub(b.x, query.start[0]);
    query.direction[1] = wc3_sub(b.y, query.start[1]);
    query.length = wc3_segment_normalize(query.direction);
    if (query.length > 1.f) {
        box2_t bounds = move_segment_bounds(&query);
        move_query_objects(&graph, input, &bounds);
    }
    return wc3_segment_test(&query);
}

/* Static geometry remains available to admission and step validation. */
bool G_MovePathLineIsPathable(pathAccelParams_t const *params) {
    if (!params) return false;
    return move_query_line(&(movePathQuery_t){ .geometry = *params });
}

bool G_UnitMovePathLineIsPathable(movePathQuery_t const *query) {
    return query && move_query_line(query);
}

/* Original fine expansion tests entering strips, including both diagonal sides. */
static uint8_t move_fine_edges(void const *data, wc3FinePoint_t pos) {
    moveFineGraph_t const *graph = data;
    wc3FineSegment_t query = { .cls = (unsigned)graph->size - 1, .cell = move_cell_ok, .data = graph };
    return wc3_fine_cell_edges(&query, pos);
}

/* Reconstruct destination-first points, then use the original next-point /
 * progressively farther selection policy. Move retains the selected turn. */
bool G_FindUnitMovePathWaypoint(movePathQuery_t const *input, vec2_t *out) {
    if (!input) return false;
    pathAccelParams_t const *params = &input->geometry;
    vec2_t source, target;
    if (!params || !params->from || !params->target || !out || !pathmap.width || !pathmap.height) return false;
    pathAccelParams_t dest = *params; dest.from = params->target;
    if (!G_ClosestMovePathPoint(params, &source) || !G_ClosestMovePathPoint(&dest, &target)) return false;
    vec2_t a = move_grid_from_world(source.x, source.y), b = move_grid_from_world(target.x, target.y);
    wc3FinePoint_t start = { (int)floorf(a.x), (int)floorf(a.y) };
    wc3FinePoint_t goal = { (int)floorf(b.x), (int)floorf(b.y) };
    if (abs(start.x - goal.x) > PATH_ACCEL_MAX_DISTANCE || abs(start.y - goal.y) > PATH_ACCEL_MAX_DISTANCE) return false;
    moveFineGraph_t graph = move_foot_shape(params);
    move_query_objects(&graph, input, NULL);
    bool target_hit = false;
    edict_t const *object = input->target;
    if (input->units && input->mover && !(input->mover->aiflags & AI_FLYING) && object && object->inuse &&
        !IS_HOLLOW(object) && object->data.UnitData && !G_UnitIsStructure(object) &&
        !M_UnitMoveDisabled(object) && !object->no_pathing && object->collision > 0 && !(object->aiflags & AI_FLYING)) {
        vec2_t pos = move_grid_from_world(object->s.origin2.x, object->s.origin2.y);
        graph.target = wc3_fine_cover(wc3_fine_class(object->collision / pathmap_cell_world_size()),
            (wc3FinePoint_t){(int)floorf(pos.x), (int)floorf(pos.y)});
        graph.has_target = true;
        graph.target_hit = &target_hit;
    }
    wc3FineRequest_t req = { .start = {start.x, start.y}, .goal = {goal.x, goal.y},
        .width = pathmap.width, .height = pathmap.height, .budget = BZ_WC3_FINE_WORK,
        .edges = move_fine_edges, .data = &graph, .target_hit = &target_hit };
    int at = wc3_fine_search(&move_fine, &req);
    bool complete = at >= 0;
    /* Original148100 retains the nearest admitted chain after exhaustion.
     * Location Move can approach that endpoint without replacing its order. */
    if (at < 0 && input->units) at = (int)move_fine.nearest;
    if (at < 0) return false;
    wc3FinePoint_t endpoint = move_fine.nodes[at].pos;
    wc3FineReconstruct_t route = {move_fine.nodes, move_fine.count, at,
        {a.x, a.y},
        complete ? (wc3FineVector_t){b.x, b.y}
                 : wc3_route_center(endpoint)};
    uint32_t count = wc3_fine_reconstruct(&route, move_fine_points, BZ_WC3_FINE_NODES);
    if (count < 2) return false;
    wc3FineSegment_t query = { .start = {a.x, a.y},
        .cls = (unsigned)graph.size - 1, .cell = move_cell_ok, .data = &graph };
    uint32_t chosen = wc3_segment_waypoint(&query, (wc3FineRoute_t){move_fine_points, count - 1});
    wc3FineVector_t point = move_fine_points[chosen];
    /* Preserve admitted world words when the selected point is the exact goal. */
    *out = complete && endpoint.x == goal.x && endpoint.y == goal.y && chosen == 0
        ? target : move_world_from_grid(point.x, point.y);
    return true;
}

/* Asset-free/static callers deliberately request no transient unit objects. */
bool G_FindMovePathWaypoint(pathAccelParams_t const *params, vec2_t *out) {
    if (!params) return false;
    return G_FindUnitMovePathWaypoint(&(movePathQuery_t){ .geometry = *params }, out);
}

/* WC3 Way Gate entry selection uses the shared router's static grid, but this
 * rectangle-specific policy belongs to the game that consumes it. */
bool G_ClosestStaticPathablePointInRectForRadiusFlags(vec2_t const *location, box2_t const *bounds,
                                                      float radius, uint8_t blocked_flags, vec2_t *out) {
    box2_t rect;
    vec2_t nmin, nmax;
    float best_distance = FLT_MAX;
    int radius_cells, x0, x1, y0, y1;
    bool found = false;

    if (!location || !bounds || !out) return false;
    rect.min = (vec2_t){ MIN(bounds->min.x, bounds->max.x), MIN(bounds->min.y, bounds->max.y) };
    rect.max = (vec2_t){ MAX(bounds->min.x, bounds->max.x), MAX(bounds->min.y, bounds->max.y) };
    if (rect.max.x <= rect.min.x || rect.max.y <= rect.min.y) return false;
    if (!pathmap.original || !pathmap.width || !pathmap.height) {
        *out = (vec2_t){ MIN(rect.max.x, MAX(rect.min.x, location->x)),
                          MIN(rect.max.y, MAX(rect.min.y, location->y)) };
        return true;
    }

    nmin = move_grid_from_world(rect.min.x, rect.min.y);
    nmax = move_grid_from_world(rect.max.x, rect.max.y);
    x0 = MIN((int)pathmap.width - 1, MAX(0, (int)floorf(MIN(nmin.x, nmax.x))));
    x1 = MIN((int)pathmap.width - 1, MAX(0, (int)floorf(MAX(nmin.x, nmax.x))));
    y0 = MIN((int)pathmap.height - 1, MAX(0, (int)floorf(MIN(nmin.y, nmax.y))));
    y1 = MIN((int)pathmap.height - 1, MAX(0, (int)floorf(MAX(nmin.y, nmax.y))));
    radius_cells = (int)ceilf(MAX(0.f, radius) / pathmap_cell_world_size());

    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        vec2_t a, b, candidate, check;
        float min_x, max_x, min_y, max_y, distance;
        int check_x, check_y;

        if (!is_pathable_node_original_for_radius_cells_flags(x, y, radius_cells, blocked_flags)) continue;
        a = move_world_from_grid(x, y);
        b = move_world_from_grid(x + 1, y + 1);
        min_x = MAX(rect.min.x, MIN(a.x, b.x)); max_x = MIN(rect.max.x, MAX(a.x, b.x));
        min_y = MAX(rect.min.y, MIN(a.y, b.y)); max_y = MIN(rect.max.y, MAX(a.y, b.y));
        if (min_x > max_x || min_y > max_y) continue;
        candidate = (vec2_t){ MIN(max_x, MAX(min_x, location->x)), MIN(max_y, MAX(min_y, location->y)) };
        check = move_grid_from_world(candidate.x, candidate.y);
        check_x = (int)floorf(check.x); check_y = (int)floorf(check.y);
        if (check_x != x || check_y != y)
            candidate = (vec2_t){ (min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f };
        distance = Vector2_distance(location, &candidate);
        if (!found || distance < best_distance) best_distance = distance, *out = candidate, found = true;
    }
    return found;
}
#pragma GCC visibility pop

#undef FS_ReadFile
#undef FS_FreeFile
#undef FS_SetPriorityArchive
#undef MemAlloc
#undef MemFree
#undef PF_TextRemoveBom
#undef Com_Error
#undef ge
