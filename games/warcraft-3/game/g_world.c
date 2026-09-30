#include "g_local.h"
#include "../common/wc3_pathing_fine.h"

typedef struct { int size; uint8_t flags; } moveFineGraph_t;
static wc3FineSearch_t move_fine;

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
static bool move_foot_ok(moveFineGraph_t const *graph, wc3FinePoint_t pos) {
    wc3FineBox_t box = wc3_fine_cover((unsigned)graph->size - 1, pos);
    int x0 = box.min.x, y0 = box.min.y, x1 = box.max.x, y1 = box.max.y;
    if (x0 < 0 || y0 < 0 || x1 > (int)pathmap.width || y1 > (int)pathmap.height) return false;
    uint32_t const *prefix = graph->flags == CM_PATHING_UNWALKABLE ? pathmap.obstacle_prefix :
                             graph->flags == CM_PATHING_UNFLYABLE ? pathmap.nofly_prefix : NULL;
    if (prefix) {
        uint32_t stride = pathmap.width + 1;
        return prefix[x1 + y1 * stride] - prefix[x0 + y1 * stride] -
               prefix[x1 + y0 * stride] + prefix[x0 + y0 * stride] == 0;
    }
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++)
        if (!is_pathable_node_original_flags(x, y, graph->flags)) return false;
    return true;
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
    vec2_t n = CM_GetNormalizedMapPosition(params->from->x, params->from->y);
    float fx = n.x * pathmap.width, fy = n.y * pathmap.height;
    int tx = (int)floorf(fx), ty = (int)floorf(fy), bound = (int)MAX(pathmap.width, pathmap.height);
    moveFineGraph_t graph = move_foot_shape(params);
    for (int ring = 1; ring <= bound; ring++) {
        float best = FLT_MAX;
        wc3FinePoint_t chosen = {0};
        bool found = false;
        for (int y = ty - ring; y <= ty + ring; y++) for (int x = tx - ring; x <= tx + ring; x++) {
            if (x != tx - ring && x != tx + ring && y != ty - ring && y != ty + ring) continue;
            if (!move_foot_ok(&graph, (wc3FinePoint_t){x,y})) continue;
            float dx = x + 0.5f - fx, dy = y + 0.5f - fy, dist = dx * dx + dy * dy;
            if (!found || dist < best) { best = dist; chosen = (wc3FinePoint_t){x,y}; found = true; }
        }
        if (!found) continue;
        *out = CM_GetDenormalizedMapPosition((chosen.x + 0.5f) / pathmap.width, (chosen.y + 0.5f) / pathmap.height);
        return true;
    }
    return false;
}

/* TODO: retain Bresenham/corner sampling until the all-class retail sampled
 * segment port is verified. Both adapters now consume the same class shape. */
bool G_MovePathLineIsPathable(pathAccelParams_t const *params) {
    if (!params || !params->from || !params->target) return false;
    if (!pathmap.width || !pathmap.height) return true;
    vec2_t a = CM_GetNormalizedMapPosition(params->from->x, params->from->y);
    vec2_t b = CM_GetNormalizedMapPosition(params->target->x, params->target->y);
    int x = (int)floorf(a.x * pathmap.width), y = (int)floorf(a.y * pathmap.height);
    int bx = (int)floorf(b.x * pathmap.width), by = (int)floorf(b.y * pathmap.height);
    int dx = abs(bx - x), dy = abs(by - y), sx = x < bx ? 1 : -1, sy = y < by ? 1 : -1;
    int err = dx - dy, guard = dx + dy + 2;
    moveFineGraph_t graph = move_foot_shape(params);
    while (guard-- > 0) {
        if (!move_foot_ok(&graph, (wc3FinePoint_t){x,y})) return false;
        if (x == bx && y == by) return true;
        int twice = 2 * err;
        bool step_x = twice > -dy, step_y = twice < dx;
        if (step_x && step_y && (!move_foot_ok(&graph, (wc3FinePoint_t){x + sx,y}) ||
                                !move_foot_ok(&graph, (wc3FinePoint_t){x,y + sy}))) return false;
        if (step_x) { err -= dy; x += sx; }
        if (step_y) { err += dx; y += sy; }
    }
    return false;
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

/* Return the farthest currently visible point on the verified fine cell chain;
 * Move retains this turn while it travels. Geometry stays in the game world. */
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
    while (at >= 0) {
        wc3FineNode_t const *node = &move_fine.nodes[at];
        if (node->parent < 0) return false;
        vec2_t point = CM_GetDenormalizedMapPosition((node->pos.x + 0.5f) / pathmap.width, (node->pos.y + 0.5f) / pathmap.height);
        pathAccelParams_t line = *params; line.target = &point;
        if (G_MovePathLineIsPathable(&line)) { *out = point; return true; }
        at = node->parent;
    }
    return false;
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
