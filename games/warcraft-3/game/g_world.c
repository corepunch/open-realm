#include "g_local.h"
#include "../common/wc3_pathing_segment.h"

typedef struct { int size; uint8_t flags; } moveFineGraph_t;
static wc3FineSearch_t move_fine;
static wc3FineVector_t move_fine_points[BZ_WC3_FINE_NODES];

/* Routing consumes game-owned surface policy; only this edict contract contains WC3 destructable state. */
static bool entity_is_live_walkable_surface(edict_t const *ent) {
    return ent && ent->destructable.initialized && !ent->destructable.dead &&
        ent->destructable.placement_solid && ent->pathtex &&
        ent->data.DestructableData && ent->data.DestructableData->walkable;
}

static uint8_t entity_dynamic_pathing_flags(edict_t const *ent) {
    return M_UnitStaticPathingFlags(ent);
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

/* Original 16ee80 checks a class-sized square, biased left/up for even sizes.
 * ceil(radius) instead imposed 3/5-cell squares on retail's 1/2/3/4 classes. */
static pathGridQuery_t move_field_shape(float radius, uint8_t flags) {
    unsigned cls = wc3_fine_class(radius / pathmap_cell_world_size());
    wc3FineBox_t box = wc3_fine_cover(cls, (wc3FinePoint_t){0, 0});
    return (pathGridQuery_t){ {box.min.x, box.min.y}, {box.max.x, box.max.y},
                             normalize_blocked_flags(flags) };
}

static bool move_foot_ok(moveFineGraph_t const *graph, wc3FinePoint_t pos) {
    wc3FineBox_t box = wc3_fine_cover((unsigned)graph->size - 1, (wc3FinePoint_t){0, 0});
    pathGridQuery_t query = { {box.min.x, box.min.y}, {box.max.x, box.max.y}, graph->flags };
    return path_query_ok((point2_t){pos.x, pos.y}, &query);
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
    return (moveFineGraph_t){ (int)wc3_fine_class(params->radius / pathmap_cell_world_size()) + 1,
                             normalize_blocked_flags(params->blocked_flags) };
}

/* Endpoint geometry is shared by routing and the Move step validator. */
bool G_MovePathPointIsPathable(pathAccelParams_t const *params) {
    if (!params || !params->from) return false;
    if (!pathmap.width || !pathmap.height) return true;
    vec2_t n = CM_GetNormalizedMapPosition(params->from->x, params->from->y);
    moveFineGraph_t graph = move_foot_shape(params);
    return move_foot_ok(&graph, (wc3FinePoint_t){ (int)floorf(n.x * pathmap.width), (int)floorf(n.y * pathmap.height) });
}

/* Keep existing nearest-ring endpoint correction while using the actual
 * class footprint. Retail public admission/exclusion remains FOOT-04. */
bool G_ClosestMovePathPoint(pathAccelParams_t const *params, vec2_t *out) {
    if (!params || !params->from || !out) return false;
    if (G_MovePathPointIsPathable(params)) { *out = *params->from; return true; }
    pathGridQuery_t query = move_field_shape(params->radius, params->blocked_flags);
    point2_t chosen;
    if (!closest_pathable_node_query(params->from, &query, &chosen)) return false;
    *out = CM_GetDenormalizedMapPosition((chosen.x + 0.5f) / pathmap.width,
                                         (chosen.y + 0.5f) / pathmap.height);
    return true;
}

static bool move_segment_cell(void const *data, wc3FinePoint_t pos) {
    uint8_t const *flags = data;
    return is_pathable_node_original_flags(pos.x, pos.y, *flags);
}

/* Engine admission checks both endpoints; the recovered interior sampler
 * itself leaves them unchecked and starts its previous cell at0,0. */
bool G_MovePathLineIsPathable(pathAccelParams_t const *params) {
    if (!params || !params->from || !params->target) return false;
    if (!pathmap.width || !pathmap.height) return true;
    pathAccelParams_t end = *params; end.from = params->target;
    if (!G_MovePathPointIsPathable(params) || !G_MovePathPointIsPathable(&end)) return false;
    vec2_t a = CM_GetNormalizedMapPosition(params->from->x, params->from->y);
    vec2_t b = CM_GetNormalizedMapPosition(params->target->x, params->target->y);
    uint8_t flags = normalize_blocked_flags(params->blocked_flags);
    wc3FineSegment_t query = { .start = {a.x * pathmap.width, a.y * pathmap.height},
        .cls = wc3_fine_class(params->radius / pathmap_cell_world_size()),
        .cell = move_segment_cell, .data = &flags };
    query.direction[0] = wc3_sub(b.x * pathmap.width, query.start[0]);
    query.direction[1] = wc3_sub(b.y * pathmap.height, query.start[1]);
    query.length = wc3_segment_normalize(query.direction);
    return wc3_segment_test(&query);
}

/* For a legal current footprint, checking the new square and both diagonal
 * side squares is equivalent to the original entering perimeter strips. */
static uint8_t move_fine_edges(void const *data, wc3FinePoint_t pos) {
    moveFineGraph_t const *graph = data;
    uint8_t edges = 0;
    bool legal[8];
    for (int dir = 0; dir < 8; dir++) {
        wc3FinePoint_t delta = wc3_fine_dirs[dir];
        legal[dir] = move_foot_ok(graph, (wc3FinePoint_t){ pos.x + delta.x, pos.y + delta.y });
    }
    for (int dir = 0; dir < 8; dir++) {
        wc3FinePoint_t delta = wc3_fine_dirs[dir];
        if (!legal[dir] || (delta.x && delta.y &&
            (!legal[delta.x < 0 ? 3 : 4] || !legal[delta.y < 0 ? 1 : 6]))) continue;
        edges |= (uint8_t)(1u << dir);
    }
    return edges;
}

/* Reconstruct destination-first points, then use the original next-point /
 * progressively farther selection policy. Move retains the selected turn. */
bool G_FindMovePathWaypoint(pathAccelParams_t const *params, vec2_t *out) {
    vec2_t source, target;
    if (!params || !params->from || !params->target || !out || !pathmap.width || !pathmap.height) return false;
    pathAccelParams_t dest = *params; dest.from = params->target;
    if (!G_ClosestMovePathPoint(params, &source) || !G_ClosestMovePathPoint(&dest, &target)) return false;
    vec2_t a = CM_GetNormalizedMapPosition(source.x, source.y), b = CM_GetNormalizedMapPosition(target.x, target.y);
    wc3FinePoint_t start = { (int)floorf(a.x * pathmap.width), (int)floorf(a.y * pathmap.height) };
    wc3FinePoint_t goal = { (int)floorf(b.x * pathmap.width), (int)floorf(b.y * pathmap.height) };
    if (abs(start.x - goal.x) > PATH_ACCEL_MAX_DISTANCE || abs(start.y - goal.y) > PATH_ACCEL_MAX_DISTANCE) return false;
    moveFineGraph_t graph = move_foot_shape(params);
    wc3FineRequest_t req = { .start = {start.x, start.y}, .goal = {goal.x, goal.y},
        .width = pathmap.width, .height = pathmap.height, .budget = BZ_WC3_FINE_WORK,
        .edges = move_fine_edges, .data = &graph };
    int at = wc3_fine_search(&move_fine, &req);
    uint32_t count = 0;
    while (at >= 0) {
        wc3FineNode_t const *node = &move_fine.nodes[at];
        assert(count < BZ_WC3_FINE_NODES);
        move_fine_points[count++] = (wc3FineVector_t){node->pos.x + 0.5f, node->pos.y + 0.5f};
        at = node->parent;
    }
    if (count < 2) return false;
    uint8_t flags = graph.flags;
    wc3FineSegment_t query = { .start = {a.x * pathmap.width, a.y * pathmap.height},
        .cls = (unsigned)graph.size - 1, .cell = move_segment_cell, .data = &flags };
    uint32_t chosen = wc3_segment_waypoint(&query, (wc3FineRoute_t){move_fine_points, count - 1});
    wc3FineVector_t point = move_fine_points[chosen];
    *out = CM_GetDenormalizedMapPosition(point.x / pathmap.width, point.y / pathmap.height);
    return true;
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

    nmin = CM_GetNormalizedMapPosition(rect.min.x, rect.min.y);
    nmax = CM_GetNormalizedMapPosition(rect.max.x, rect.max.y);
    x0 = MIN((int)pathmap.width - 1, MAX(0, (int)floorf(MIN(nmin.x, nmax.x) * pathmap.width)));
    x1 = MIN((int)pathmap.width - 1, MAX(0, (int)floorf(MAX(nmin.x, nmax.x) * pathmap.width)));
    y0 = MIN((int)pathmap.height - 1, MAX(0, (int)floorf(MIN(nmin.y, nmax.y) * pathmap.height)));
    y1 = MIN((int)pathmap.height - 1, MAX(0, (int)floorf(MAX(nmin.y, nmax.y) * pathmap.height)));
    radius_cells = (int)ceilf(MAX(0.f, radius) / pathmap_cell_world_size());

    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        vec2_t a, b, candidate, check;
        float min_x, max_x, min_y, max_y, distance;
        int check_x, check_y;

        if (!is_pathable_node_original_for_radius_cells_flags(x, y, radius_cells, blocked_flags)) continue;
        a = CM_GetDenormalizedMapPosition((float)x / pathmap.width, (float)y / pathmap.height);
        b = CM_GetDenormalizedMapPosition((float)(x + 1) / pathmap.width,
                                           (float)(y + 1) / pathmap.height);
        min_x = MAX(rect.min.x, MIN(a.x, b.x)); max_x = MIN(rect.max.x, MAX(a.x, b.x));
        min_y = MAX(rect.min.y, MIN(a.y, b.y)); max_y = MIN(rect.max.y, MAX(a.y, b.y));
        if (min_x > max_x || min_y > max_y) continue;
        candidate = (vec2_t){ MIN(max_x, MAX(min_x, location->x)), MIN(max_y, MAX(min_y, location->y)) };
        check = CM_GetNormalizedMapPosition(candidate.x, candidate.y);
        check_x = (int)floorf(check.x * pathmap.width); check_y = (int)floorf(check.y * pathmap.height);
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
