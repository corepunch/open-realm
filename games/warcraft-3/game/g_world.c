#include "g_local.h"

/* Routing consumes game-owned surface policy; only this edict contract contains WC3 destructable state. */
static BOOL entity_is_live_walkable_surface(edict_t const *ent) {
    return ent && ent->destructable.initialized && !ent->destructable.dead &&
        ent->destructable.placement_solid && ent->pathtex &&
        ent->data.DestructableData && ent->data.DestructableData->walkable;
}

static BYTE entity_dynamic_pathing_flags(edict_t const *ent) {
    return M_UnitStaticPathingFlags(ent);
}
static BOOL entity_is_pathing_ignored(edict_t const *ent) {
    /* A construction-site indicator is a visible reservation, not a building
     * obstacle. Once construction starts, the real structure blocks movement. */
    return ent && (ent->s.flags & (EF_BUILDING | EF_NOT_SELECTABLE)) ==
        (EF_BUILDING | EF_NOT_SELECTABLE) && !ent->construction.active;
}

/* WC3 pathing TGAs are transposed relative to model/world axes.  Only bridge
 * targets use the authored angle to select a quarter-turn; ordinary footprints
 * retain their existing unrotated contract. */
static void entity_pathtex_transform(pathTexTransformParams_t const *params, pathTexTransform_t *transform) {
    pathTex_t const *pt = params ? params->pathtex : NULL;
    FLOAT const angle = params && params->ent ? params->ent->s.angle : 0.0f;
    int quarter;

    if (!transform || !pt) return;
    quarter = (pt->width != pt->height) + (int)lroundf(angle / ((FLOAT)M_PI / 2.0f));
    transform->turn = ((quarter % 4) + 4) % 4;
    transform->width = transform->turn & 1 ? pt->height : pt->width;
    transform->height = transform->turn & 1 ? pt->width : pt->height;
    if (!params->ent || params->ent->targtype != TARG_BRIDGE)
        transform->turn = 0, transform->width = pt->width, transform->height = pt->height;
}

static inline HANDLE G_WorldReadFile(LPCSTR filename, LPDWORD size) { return gi.ReadFile(filename, size); }
static inline HANDLE G_WorldMemAlloc(long size) { return gi.MemAlloc(size); }
static inline void G_WorldMemFree(HANDLE mem) { gi.MemFree(mem); }
static inline void G_WorldSetPriorityArchive(HANDLE archive) { gi.SetPriorityArchive(archive); }
static inline BOMStatus G_WorldTextRemoveBom(LPSTR buffer) {
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

/* WC3 Way Gate entry selection uses the shared router's static grid, but this
 * rectangle-specific policy belongs to the game that consumes it. */
BOOL G_ClosestStaticPathablePointInRectForRadiusFlags(LPCVECTOR2 location, LPCBOX2 bounds,
                                                      FLOAT radius, BYTE blocked_flags, LPVECTOR2 out) {
    BOX2 rect;
    VECTOR2 nmin, nmax;
    FLOAT best_distance = FLT_MAX;
    int radius_cells, x0, x1, y0, y1;
    BOOL found = false;

    if (!location || !bounds || !out) return false;
    rect.min = (VECTOR2){ MIN(bounds->min.x, bounds->max.x), MIN(bounds->min.y, bounds->max.y) };
    rect.max = (VECTOR2){ MAX(bounds->min.x, bounds->max.x), MAX(bounds->min.y, bounds->max.y) };
    if (rect.max.x <= rect.min.x || rect.max.y <= rect.min.y) return false;
    if (!pathmap.original || !pathmap.width || !pathmap.height) {
        *out = (VECTOR2){ MIN(rect.max.x, MAX(rect.min.x, location->x)),
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
        VECTOR2 a, b, candidate, check;
        FLOAT min_x, max_x, min_y, max_y, distance;
        int check_x, check_y;

        if (!is_pathable_node_original_for_radius_cells_flags(x, y, radius_cells, blocked_flags)) continue;
        a = CM_GetDenormalizedMapPosition((FLOAT)x / pathmap.width, (FLOAT)y / pathmap.height);
        b = CM_GetDenormalizedMapPosition((FLOAT)(x + 1) / pathmap.width,
                                           (FLOAT)(y + 1) / pathmap.height);
        min_x = MAX(rect.min.x, MIN(a.x, b.x)); max_x = MIN(rect.max.x, MAX(a.x, b.x));
        min_y = MAX(rect.min.y, MIN(a.y, b.y)); max_y = MIN(rect.max.y, MAX(a.y, b.y));
        if (min_x > max_x || min_y > max_y) continue;
        candidate = (VECTOR2){ MIN(max_x, MAX(min_x, location->x)), MIN(max_y, MAX(min_y, location->y)) };
        check = CM_GetNormalizedMapPosition(candidate.x, candidate.y);
        check_x = (int)floorf(check.x * pathmap.width); check_y = (int)floorf(check.y * pathmap.height);
        if (check_x != x || check_y != y)
            candidate = (VECTOR2){ (min_x + max_x) * 0.5f, (min_y + max_y) * 0.5f };
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
