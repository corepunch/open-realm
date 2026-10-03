#include "g_local.h"
#include "../common/wc3_pathing_route.h"
#include "../common/wc3_pathing_coordinates.h"
#include "../common/wc3_pathing_placement.h"
#include "../common/wc3_pathing_adaptive.h"
#include "../common/wc3_pathing_widget.h"
#include "../common/wc3_pathing_spatial.h"

typedef struct {
    int size;
    uint8_t flags;
    uint32_t objects;
    wc3SpatialActive_t const *target_links;
    bool has_target, endpoint;
    uint32_t level;
    bool *target_hit;
} moveFineGraph_t;
typedef struct { wc3FineBox_t box; wc3SpatialActive_t const *links; } moveFineObject_t;
static moveFineObject_t move_objects[MAX_ENTITIES];
static wc3SpatialActive_t move_spatial[MAX_ENTITIES];
static uint64_t move_spatial_serial;

void G_ClearMoveSpatial(void) {
    memset(move_spatial,0,sizeof(move_spatial)); move_spatial_serial=0;
}

void G_RemoveMoveSpatialObject(edict_t const *ent) {
    if (ent) memset(move_spatial+(ent-g_edicts),0,sizeof(*move_spatial));
}
typedef struct { moveFineGraph_t *graph; movePathQuery_t const *query; } moveObjectScan_t;
static moveObjectScan_t *move_scan;
static wc3FineSearch_t move_fine;
static wc3FineVector_t move_fine_points[BZ_WC3_FINE_NODES];
static wc3AccSearch_t move_acc;
static wc3FineVector_t move_acc_points[BZ_WC3_FINE_NODES];
static void *move_acc_storage;
static uint32_t move_acc_revision, move_acc_width, move_acc_height;
static uint8_t *move_acc_classes[4][4];
static uint8_t const move_acc_masks[4] = {2,4,0x40,0x80};
typedef struct { movePathQuery_t const *input; wc3FineVector_t source, target; moveFineRoute_t *route; } moveAdaptiveQuery_t;
static bool move_find_route(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *out);

/* Routing consumes game-owned surface policy; only this edict contract contains WC3 destructable state. */
static bool entity_is_live_walkable_surface(edict_t const *ent) {
    return ent && ent->destructable && !ent->destructable->dead &&
        ent->destructable->placement_solid && ent->pathtex &&
        ent->data.DestructableData && ent->data.DestructableData->walkable;
}

/* Original widget blue regions usec2: walk, float and amphibious blockage. */
static uint8_t entity_static_pathing_flags(edict_t const *ent) { (void)ent; return 0xc2; }
static uint8_t entity_dynamic_pathing_flags(edict_t const *ent) {
    /* Query masks describe the mover; occupancy describes the encountered unit. */
    return !ent || M_UnitMoveDisabled(ent) || (ent->aiflags & AI_FLYING) ? 0 : 0xca;
}
static bool entity_is_pathing_ignored(edict_t const *ent) {
    /* A construction-site indicator is a visible reservation, not a building
     * obstacle. Once construction starts, the real structure blocks movement. */
    return G_UnitIsStructure(ent) && (ent->s.flags & EF_NOT_SELECTABLE) && !ent->construction;
}

/* WC3 pathing TGAs are transposed relative to model/world axes. Destructable
 * path textures follow their facing regardless of rawcode or target type;
 * non-destructable footprints retain their axis-aligned contract. */
static void entity_pathtex_transform(pathTexTransformParams_t const *params, pathTexTransform_t *transform) {
    pathTex_t const *pt = params ? params->pathtex : NULL;
    float const angle = params && params->ent ? params->ent->s.angle : 0.0f;

    if (!transform || !pt) return;
    transform->turn = wc3_widget_texture_turn(angle,pt->width,pt->height);
    transform->width = transform->turn & 1 ? pt->height : pt->width;
    transform->height = transform->turn & 1 ? pt->width : pt->height;
    if (!params->ent || !params->ent->destructable)
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

/* Use the bake's predicate for lifecycle invalidation, including dead rubble
 * and live bridge decks that replace terrain rather than adding a blocker. */
bool G_EntityHasStaticPathing(edict_t const *ent) {
    return entity_blocks_static_pathing(ent);
}

/* Map extents/dimensions describe cell sizes; simulation coordinates then use
 * the direct software transform. Stock WC3 WPM cells are32 world units. */
static vec2_t move_grid_from_world(float x, float y) {
    box2_t const bounds = CM_GetWorldBounds();
    float const cx = (bounds.max.x - bounds.min.x) / pathmap.width;
    float const cy = (bounds.max.y - bounds.min.y) / pathmap.height;
    return (vec2_t){wc3_grid_coordinate(x, bounds.min.x, cx), wc3_grid_coordinate(y, bounds.min.y, cy)};
}

/* A published native pose is authoritative; world inversion can round into a
 * different heading or progress decision. Synthetic maps retain their scale. */
static vec2_t move_query_source(movePathQuery_t const *input) {
    if (!input->fine) return move_grid_from_world(input->geometry.from->x,input->geometry.from->y);
    box2_t const bounds=CM_GetWorldBounds();
    float cx=(bounds.max.x-bounds.min.x)/pathmap.width, cy=(bounds.max.y-bounds.min.y)/pathmap.height;
    return (vec2_t){wc3_mul(input->fine->x,wc3_div(32,cx)),wc3_mul(input->fine->y,wc3_div(32,cy))};
}

/* Cell centres and retained route points must not round through map fractions. */
static vec2_t move_world_from_grid(float x, float y) {
    box2_t const bounds = CM_GetWorldBounds();
    float const cx = (bounds.max.x - bounds.min.x) / pathmap.width;
    float const cy = (bounds.max.y - bounds.min.y) / pathmap.height;
    return (vec2_t){wc3_world_coordinate(x, bounds.min.x, cx), wc3_world_coordinate(y, bounds.min.y, cy)};
}

/* Classification is derived map state; release it when the game module shuts down. */
void G_FreeMovePathCache(void) {
    free(move_acc_storage); move_acc_storage = NULL;
    move_acc_width = move_acc_height = move_acc_revision = 0;
}

/* Four ordinary static lanes share node-index scratch; cache the retail 2x fine base and three parents. */
static void move_acc_prepare(void) {
    /* Original15ab60 adds16 fine cells, truncates division by2, then adds1.
     * Padding is allocation space, not additional blocked terrain. */
    uint32_t width=(pathmap.width+16)/2+1, height=(pathmap.height+16)/2+1;
    if (move_acc_storage && move_acc_width == width && move_acc_height == height &&
        move_acc_revision == pathmap.revision) return;
    if (move_acc_width != width || move_acc_height != height || !move_acc_storage) {
        G_FreeMovePathCache();
        uint32_t cells = 0;
        FOR_LOOP(level,4) cells += (width>>level)*(height>>level);
        move_acc_storage = malloc((size_t)cells*(sizeof(int)+4));
        if (!move_acc_storage) gi.error("WC3 adaptive routing: cannot allocate %u hierarchy cells",cells);
        int *indices = move_acc_storage;
        uint8_t *classes = (uint8_t *)(indices+cells);
        FOR_LOOP(level,4) {
            uint32_t w = width>>level, h = height>>level;
            move_acc.maps[level] = (wc3AccMap_t){.width=w,.height=h,.indices=indices}; indices += w*h;
            FOR_LOOP(lane,4) { move_acc_classes[lane][level] = classes; classes += w*h; }
        }
        move_acc_width = width; move_acc_height = height;
    }
    FOR_LOOP(lane,4) FOR_LOOP(level,4) {
        wc3AccMap_t const *map = move_acc.maps+level;
        uint8_t *classes = move_acc_classes[lane][level];
        memset(classes,0,(size_t)map->width*map->height);
        unsigned cols=MIN(map->width,pathmap.width/(2u<<level)+1);
        unsigned rows=MIN(map->height,pathmap.height/(2u<<level)+1);
        FOR_LOOP(y,rows) FOR_LOOP(x,cols) {
            unsigned blocked = 0;
            if (!level) {
                FOR_LOOP(dy,2) FOR_LOOP(dx,2)
                    blocked += !is_pathable_node_original_flags(x*2+dx,y*2+dy,lane ? move_acc_masks[lane] : 6);
                classes[y*map->width+x] = blocked == 4 ? 1 : blocked ? 2 : 0;
            } else {
                uint32_t w = move_acc.maps[level-1].width;
                uint8_t const *child = move_acc_classes[lane][level-1];
                uint8_t first = child[y*2*w+x*2]; bool same = true;
                FOR_LOOP(dy,2) FOR_LOOP(dx,2) same &= child[(y*2+dy)*w+x*2+dx] == first;
                classes[y*map->width+x] = same && first < 2 ? first : 2;
            }
        }
    }
    /* TODO MAP/ACC: special bytes, dynamic classification/exclusion and stale retail terrain-edit
     * invalidation producers remain separate. This cache follows the engine's static bake epoch. */
    move_acc_revision = pathmap.revision;
}

#ifdef BZ_TESTS
wc3AccSearch_t const *G_TestMoveAdaptiveSearch(void) {
    return &move_acc;
}
wc3FineSearch_t const *G_TestMoveFineSearch(void) {
    return &move_fine;
}
void G_TestMoveFinePopTrace(void (*trace)(void *,uint32_t const[10]),void *data) {
    move_fine.pop_trace=trace; move_fine.trace_data=data;
}
vec2_t G_TestMoveWorldGrid(vec2_t point, bool inverse) {
    return inverse ? move_world_from_grid(point.x,point.y) : move_grid_from_world(point.x,point.y);
}
point2_t G_TestMovePathSize(unsigned level) {
    move_acc_prepare();
    if(level>=4) return (point2_t){0,0};
    return (point2_t){move_acc.maps[level].width,move_acc.maps[level].height};
}
unsigned G_TestStaticPathMask(unsigned x, unsigned y) {
    unsigned result=0;
    FOR_LOOP(bit,8) if(!is_pathable_node_original_flags(x,y,1u<<bit)) result|=1u<<bit;
    return result;
}
/* Compare the production cached hierarchy directly with frozen retail cells. */
int G_TestMovePathClass(uint8_t mask, unsigned level, unsigned x, unsigned y) {
    move_acc_prepare();
    FOR_LOOP(lane,4) if(move_acc_masks[lane]==mask && level<4 &&
        x<move_acc.maps[level].width && y<move_acc.maps[level].height)
        return move_acc_classes[lane][level][y*move_acc.maps[level].width+x];
    return -1;
}
#endif

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
    moveFineObject_t const *left = a, *right = b;
    return (left->box.min.x > right->box.min.x) - (left->box.min.x < right->box.min.x);
}

/* Original1603d0/160590 publish occupancy only when the native pose commits.
 * Presentation samples predict ahead without moving the fine object. */
static vec2_t move_object_point(edict_t const *ent) {
    if (!ent->movement.pose_valid ||
        wc3_float_bits(ent->movement.pose_world.x)!=wc3_float_bits(ent->s.origin2.x) ||
        wc3_float_bits(ent->movement.pose_world.y)!=wc3_float_bits(ent->s.origin2.y))
        return move_grid_from_world(ent->s.origin2.x,ent->s.origin2.y);
    box2_t bounds=CM_GetWorldBounds();
    float cx=(bounds.max.x-bounds.min.x)/pathmap.width,cy=(bounds.max.y-bounds.min.y)/pathmap.height;
    return (vec2_t){wc3_mul(ent->movement.fine_pose.x,wc3_div(32,cx)),
        wc3_mul(ent->movement.fine_pose.y,wc3_div(32,cy))};
}

/* TODO: the complete authored category table is BASE-02. This game adapter
 * uses the observed foot/horse/hover/float/amph categoryca;
 * flyers and disabled rows publish0. Buildings/destructables already own static footprints. */
static bool move_has_dynamic_occupancy(edict_t const *ent) {
    if (ent->movement.captain_actor_type) return ent->inuse;
    return !IS_HOLLOW(ent) && ent->data.UnitData && !G_UnitIsStructure(ent) &&
        !M_UnitMoveDisabled(ent) && ent->collision>0 && !(ent->aiflags&AI_FLYING);
}

/* Pose commits own publication. A query also observes authored size/category
 * changes and explicit world writes by non-Move game owners. Intersection
 * links survive; leaving/re-entering prepends even within one JASS callback. */
void G_PublishMoveSpatialObject(edict_t const *ent) {
    if (!ent || !pathmap.width || !pathmap.height) return;
    if (!ent->inuse || !move_has_dynamic_occupancy(ent)) {
        G_RemoveMoveSpatialObject(ent); return;
    }
    vec2_t point=move_object_point(ent);
    wc3FineBox_t box=wc3_fine_cover(wc3_fine_class(ent->collision/pathmap_cell_world_size()),
        (wc3FinePoint_t){(int)floorf(point.x),(int)floorf(point.y)});
    if (!wc3_spatial_update(move_spatial+(ent-g_edicts),box,&move_spatial_serial))
        gi.error("WC3 fine spatial history: invalid rectangle or exhausted publication rank");
}

uint64_t G_GetMoveSpatialSerial(void) { return move_spatial_serial; }

void G_SetMoveSpatialSerial(uint64_t serial) { move_spatial_serial=serial; }

wc3SpatialActive_t const *G_GetMoveSpatialObject(uint32_t index) {
    assert(index<MAX_ENTITIES); return move_spatial+index;
}

bool G_SetMoveSpatialObject(uint32_t index, wc3SpatialActive_t const *data) {
    if (index>=MAX_ENTITIES || !data ||
        !wc3_spatial_valid(data,move_spatial_serial)) return false;
    move_spatial[index]=*data; return true;
}

static bool move_object_collect(edict_t const *ent) {
    moveFineGraph_t *graph = move_scan->graph;
    movePathQuery_t const *query = move_scan->query;
    G_PublishMoveSpatialObject(ent);
    if (ent==query->mover || ent==query->target || !move_has_dynamic_occupancy(ent)) return false;
    /* Virtual captains publish ordinary velocity flags once they move. */
    uint32_t flags=S_UnitMoveFineObjectFlags(ent);
    uint32_t mask = graph->flags;
    mask |= mask << 24;
    if (!wc3_fine_object_blocks((wc3FineObject_t){ent->movement.captain_actor_type ? 0x01000002 : 0x010000ca, flags, true}, mask, graph->endpoint)) return false;
    wc3SpatialActive_t const *links=move_spatial+(ent-g_edicts);
    assert(graph->objects < MAX_ENTITIES);
    move_objects[graph->objects++] = (moveFineObject_t){links->box,links};
    return false; /* Collect rectangles directly; no capped BoxEdicts pointer list. */
}

/* Original15d360 rounds a fine object's half-open rectangle into base
 * cells, clears their traversal lanes for admission, then rebuilds the same
 * rectangle and its three parents. Edge terrain is deliberately excluded too. */
static void move_acc_object_rectangle(edict_t const *object, bool clear) {
    if (!object || !object->inuse || !move_has_dynamic_occupancy(object)) return;
    vec2_t p=move_object_point(object);
    wc3FineBox_t box=wc3_fine_cover(wc3_fine_class(object->collision/pathmap_cell_world_size()),
        (wc3FinePoint_t){(int)floorf(p.x),(int)floorf(p.y)});
    int minx=MAX(0,box.min.x)/2,miny=MAX(0,box.min.y)/2;
    int maxx=(MIN((int)pathmap.width,box.max.x)+1)/2,maxy=(MIN((int)pathmap.height,box.max.y)+1)/2;
    if(minx>=maxx || miny>=maxy)return;
    /* TODO MAP-03.3: dynamic coarse cell-link composition and spatial dirty
     * publication remain separate; these lanes retain static terrain geometry. */
    FOR_LOOP(lane,4) {
        for(int y=miny;y<maxy;y++)for(int x=minx;x<maxx;x++) {
            unsigned blocked=0;
            /* Original15cf80 restores coarse ground6, including no-fly4,
             * just as the initial hierarchy bake does. Fine ground stays2. */
            if (!clear) FOR_LOOP(dy,2)FOR_LOOP(dx,2)
                blocked+=!is_pathable_node_original_flags(x*2+dx,y*2+dy,lane ? move_acc_masks[lane] : 6);
            move_acc_classes[lane][0][y*move_acc.maps[0].width+x]=clear ? 0 : blocked==4 ? 1 : blocked ? 2 : 0;
        }
        int lo_x=minx,lo_y=miny,hi_x=maxx,hi_y=maxy;
        for(unsigned level=1;level<4;level++) {
            lo_x/=2;lo_y/=2;hi_x=(hi_x+1)/2;hi_y=(hi_y+1)/2;
            uint32_t w=move_acc.maps[level-1].width;
            uint8_t const *child=move_acc_classes[lane][level-1];
            for(int y=lo_y;y<hi_y;y++)for(int x=lo_x;x<hi_x;x++) {
                uint8_t first=child[y*2*w+x*2];bool same=true;
                FOR_LOOP(dy,2)FOR_LOOP(dx,2)same&=child[(y*2+dy)*w+x*2+dx]==first;
                move_acc_classes[lane][level][y*move_acc.maps[level].width+x]=same && first<2 ? first : 2;
            }
        }
    }
}

/* A segment uses area-tree pruning; a fine detour may leave that rectangle,
 * so its snapshot scans the actor set once. Neither invalidates static fields. */
static void move_query_objects(moveFineGraph_t *graph, movePathQuery_t const *query, box2_t const *bounds) {
    graph->objects = 0;
    if (!query->units || !query->mover || (query->mover->aiflags & AI_FLYING)) return;
    if (query->target) G_PublishMoveSpatialObject(query->target);
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
static bool move_occupancy_cell(void const *data, wc3FinePoint_t pos) {
    moveFineGraph_t const *graph = data;
    if (!is_valid_point(pos.x,pos.y)) return false;
    uint32_t lo = 0, hi = graph->objects;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (move_objects[mid].box.min.x < pos.x - 3) lo = mid + 1;
        else hi = mid;
    }
    uint64_t blocked=0;
    for (uint32_t i = lo; i < graph->objects && move_objects[i].box.min.x <= pos.x; i++) {
        uint64_t rank=wc3_spatial_rank(move_objects[i].links,pos);
        if (rank>blocked) blocked=rank;
    }
    /* Original1489a0 observes identity before eligibility but returns on the
     * first foreign rejection. Ineligible links do not change this ordering. */
    if (graph->has_target && wc3_spatial_rank(graph->target_links,pos)>blocked)
        *graph->target_hit = true;
    return !blocked;
}

static bool move_cell_ok(void const *data, wc3FinePoint_t pos) {
    moveFineGraph_t const *graph = data;
    /* Public placement can carry a real zero query after SetUnitPathing(false).
     * Generic routing normalizes its legacy zero before constructing this graph. */
    if (!is_valid_point(pos.x,pos.y) ||
        (graph->flags && !is_pathable_node_original_flags(pos.x,pos.y,graph->flags))) return false;
    return move_occupancy_cell(data,pos);
}

/* Native terrain words use the original direct software world/fine transform.
 * Static entity footprints are composed separately from this mutable WPM byte. */
static bool terrain_pathing_cell(vec2_t const *point, uint32_t *index) {
    if (!point || !pathmap.terrain || !pathmap.width || !pathmap.height) return false;
    vec2_t fine = move_grid_from_world(point->x,point->y);
    uint32_t x = wc3_int_bits(wc3_floor_bits(wc3_float_bits(fine.x)));
    uint32_t y = wc3_int_bits(wc3_floor_bits(wc3_float_bits(fine.y)));
    if (x >= pathmap.width || y >= pathmap.height) return false;
    *index = y * pathmap.width + x;
    return true;
}

bool G_GetTerrainPathingFlags(vec2_t const *point, uint8_t *flags) {
    uint32_t index;
    if (!terrain_pathing_cell(point,&index)) return false;
    *flags = *(uint8_t const *)(pathmap.terrain+index);
    return true;
}

/* Blight owns its cell/dirty-row lifecycle; the pathing byte is its world
 * consumer. Updating this bit does not alter movement obstacle masks. */
void G_SetTerrainBlightCell(uint32_t x, uint32_t y, bool add) {
    if (x >= pathmap.width || y >= pathmap.height || !pathmap.terrain) return;
    uint32_t index = y * pathmap.width + x;
    pathMapCell_t *grids[] = {pathmap.terrain,pathmap.original,pathmap.data};
    FOR_LOOP(i,3) if (grids[i]) {
        uint8_t *flags = (uint8_t *)(grids[i]+index);
        *flags = add ? *flags | WC3_PATH_BLIGHTED : *flags & (uint8_t)~WC3_PATH_BLIGHTED;
    }
}

bool G_SetTerrainPathingFlags(terrainPathingEdit_t const *edit) {
    uint32_t index;
    if (!terrain_pathing_cell(&edit->point,&index)) return false;
    uint8_t *flags = (uint8_t *)(pathmap.terrain+index), before = *flags;
    *flags = wc3_terrain_pathing_edit(*flags,edit->mask,edit->blocked);
    if (*flags == before) return true;
    if (edit->mask & WC3_PATH_BLIGHTED)
        G_SetBlightPathCell(index%pathmap.width,index/pathmap.width,(*flags & WC3_PATH_BLIGHTED)!=0);
    /* The legacy field cache consumes baked masks. Invalidate it when the
     * mutable terrain changes; retail adaptive classification remains separate. */
    CM_BakeStaticObstacles();
    return true;
}

uint32_t G_GetTerrainPathingStateSize(void) { return pathmap.terrain ? pathmap.width * pathmap.height : 0; }

bool G_GetTerrainPathingState(uint8_t *data, uint32_t size) {
    if (size != G_GetTerrainPathingStateSize() || (size && !data)) return false;
    if (size) memcpy(data,pathmap.terrain,size);
    return true;
}

/* Save restores terrain before Blight and before the entity obstacle bake.
 * Snapshot the mutable WPM bytes, never transient dynamic occupancy. */
bool G_SetTerrainPathingState(uint8_t const *data, uint32_t size) {
    if (size != G_GetTerrainPathingStateSize() || (size && !data)) return false;
    if (size) memcpy(pathmap.terrain,data,size);
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

/* Public placement and Stop recovery share geometry, but retain distinct attempt limits. */
static bool move_place_unit(edict_t *unit, vec2_t point, uint32_t limit, vec2_t *out) {
    uint8_t flags = unit->no_pathing ? 0 : M_UnitStaticPathingFlags(unit);
    moveFineGraph_t graph = {.flags = flags, .endpoint = true};
    float fine[2] = {point.x,point.y}; graph.level = placement_terrain_level(fine);
    movePathQuery_t objects = {.mover = unit, .units = true};
    move_query_objects(&graph,&objects,NULL);
    wc3FinePlacement_t query = {.point = {point.x,point.y}, .limit = limit,
        .footprint = {.cls = wc3_fine_class(unit->collision / pathmap_cell_world_size()),
                      .cell = move_cell_ok, .data = &graph}, .admit = placement_admit};
    float admitted[2];
    if (!wc3_fine_place(&query,admitted)) return false;
    *out = (vec2_t){admitted[0],admitted[1]};
    return true;
}

/* Original public CreateUnit and SetUnitPosition share32-ring point admission.
 * Item drops retain their separately tracked producer. */
bool G_FindUnitPlacementPosition(edict_t *unit, vec2_t const *requested, vec2_t *out) {
    *out = *requested;
    if (M_UnitMoveDisabled(unit)) return true;
    if (!world.map || !world.map->vertices || !pathmap.width || !pathmap.height) {
        fprintf(stderr,"WC3 placement: terrain/pathing data unavailable\n");
        return false;
    }
    vec2_t point = move_grid_from_world(requested->x,requested->y), admitted;
    if (!move_place_unit(unit,point,32,&admitted)) return false;
    *out = move_world_from_grid(admitted.x,admitted.y);
    return true;
}

/* Original170080 recovers the predicted fine point with five attempts. No
 * admitted point means the stopped unit remains embedded; queryzero stays put. */
bool G_FindUnitMoveRecoveryPosition(edict_t *unit, vec2_t const *fine, vec2_t *out) {
    /* Recovery is inactive before a movement world is loaded or after it is released. */
    if (M_UnitMoveDisabled(unit) || !world.map || !world.map->vertices ||
        !pathmap.width || !pathmap.height) return false;
    movePathQuery_t query = {.geometry.from=&unit->s.origin2,.fine=fine};
    vec2_t point=move_query_source(&query), admitted;
    if (!move_place_unit(unit,point,5,&admitted) ||
        (wc3_float_bits(point.x)==wc3_float_bits(admitted.x) &&
         wc3_float_bits(point.y)==wc3_float_bits(admitted.y))) return false;
    box2_t bounds=CM_GetWorldBounds();
    float cx=(bounds.max.x-bounds.min.x)/pathmap.width, cy=(bounds.max.y-bounds.min.y)/pathmap.height;
    *out=(vec2_t){wc3_mul(admitted.x,wc3_div(cx,32)),wc3_mul(admitted.y,wc3_div(cy,32))};
    return true;
}

static bool move_foot_ok(moveFineGraph_t const *graph, wc3FinePoint_t pos) {
    wc3FineBox_t box = wc3_fine_cover((unsigned)graph->size - 1, pos);
    for (int y = box.min.y; y < box.max.y; y++) for (int x = box.min.x; x < box.max.x; x++)
        if (!move_cell_ok(graph, (wc3FinePoint_t){x, y})) return false;
    return true;
}

uint32_t G_RequestMovePathField(edict_t const *mover, edict_t const *goal, float radius, uint8_t flags) {
    pathGridQuery_t query = move_field_shape(radius, flags);
    point2_t target;
    if (!resolve_heatmap_request(goal, &query, &target)) return 0;
    return request_heatmap_query(target, &query, (edict_t *)mover, (edict_t *)goal);
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

/* Repulsion validates its proposed endpoint with live occupancy, excluding its own mover. */
bool G_UnitMovePathFinePointIsPathable(movePathQuery_t const *input, float const fine[2]) {
    if (!input || !input->geometry.from || !pathmap.width || !pathmap.height) return false;
    moveFineGraph_t graph = move_foot_shape(&input->geometry); graph.endpoint = true;
    move_query_objects(&graph,input,NULL);
    return move_foot_ok(&graph,(wc3FinePoint_t){(int)floorf(fine[0]),(int)floorf(fine[1])});
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
static bool move_query_line(movePathQuery_t const *input, vec2_t const *fine_target, bool occupancy) {
    pathAccelParams_t const *params = &input->geometry;
    if (!params || !params->from || !params->target) return false;
    if (!pathmap.width || !pathmap.height) return true;
    pathAccelParams_t end = *params; end.from = params->target;
    if (!occupancy && (!G_MovePathPointIsPathable(params) || !G_MovePathPointIsPathable(&end))) return false;
    vec2_t a = move_query_source(input);
    vec2_t b = fine_target ? *fine_target : move_grid_from_world(params->target->x, params->target->y);
    moveFineGraph_t graph = move_foot_shape(params);
    wc3FineSegment_t query = { .start = {a.x, a.y},
        .cls = wc3_fine_class(params->radius / pathmap_cell_world_size()),
        .cell = occupancy ? move_occupancy_cell : move_cell_ok, .data = &graph };
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
    return move_query_line(&(movePathQuery_t){ .geometry = *params },NULL,false);
}

bool G_UnitMovePathLineIsPathable(movePathQuery_t const *query) {
    return query && move_query_line(query,NULL,false);
}

/* Original fine expansion tests entering strips, including both diagonal sides. */
static uint8_t move_fine_edges(void const *data, wc3FinePoint_t pos) {
    moveFineGraph_t const *graph = data;
    wc3FineSegment_t query = { .cls = (unsigned)graph->size - 1, .cell = move_cell_ok, .data = graph };
    return wc3_fine_cell_edges(&query, pos);
}

typedef struct { movePathQuery_t const *query; edict_t **items; uint32_t count; } moveBlockerQuery_t;

/* Original148ad0 includes terrain/null tokens and moving objects. Deduplication
 * is per cell; a wider object can consume multiple slots across the footprint. */
static bool move_collect_blocker_cell(void const *data, wc3FinePoint_t pos) {
    moveBlockerQuery_t *scan=(moveBlockerQuery_t *)data;
    if (scan->count==32) return true;
    uint8_t mask=scan->query->geometry.blocked_flags;
    if (!is_valid_point(pos.x,pos.y) || (mask && !is_pathable_node_original_flags(pos.x,pos.y,mask))) {
        scan->items[scan->count++]=NULL;
        return true;
    }
    uint64_t ranks[32]; unsigned first=scan->count;
    FILTER_EDICTS(ent,ent->inuse && ent!=scan->query->mover && ent!=scan->query->target &&
        move_has_dynamic_occupancy(ent) && ((ent->movement.captain_actor_type ? 2 : entity_dynamic_pathing_flags(ent))&mask)) {
        uint64_t rank=wc3_spatial_rank(move_spatial+(ent-g_edicts),pos);
        if (!rank) continue;
        unsigned at=scan->count;
        if(at<32)scan->count++;
        while(at>first && ranks[at-1]<rank) {
            if(at<32){ranks[at]=ranks[at-1];scan->items[at]=scan->items[at-1];}
            at--;
        }
        if(at<32){ranks[at]=rank;scan->items[at]=ent;}
    }
    /* Keep the same cell order and32-token cap; newest active object first. */
    return true;
}

/* Original166140 normalizes the native next step and collects every entering
 * cell. Returning true from the callback keeps scanning after a rejection. */
uint32_t G_CollectUnitMoveStepBlockers(movePathQuery_t const *input, float const fine_goal[2], edict_t **out) {
    if (!input || !input->units || !input->mover || !out || !input->geometry.target ||
        !pathmap.width || !pathmap.height || (input->mover->aiflags&AI_FLYING)) return 0;
    FILTER_EDICTS(ent,ent->inuse) G_PublishMoveSpatialObject(ent);
    vec2_t source=move_query_source(input), goal=fine_goal ? (vec2_t){fine_goal[0],fine_goal[1]} :
        move_grid_from_world(input->geometry.target->x,input->geometry.target->y);
    moveBlockerQuery_t scan={input,out,0};
    wc3FineSegment_t query={.start={source.x,source.y},.direction={wc3_sub(goal.x,source.x),wc3_sub(goal.y,source.y)},
        .cls=wc3_fine_class(input->geometry.radius/pathmap_cell_world_size()),.cell=move_collect_blocker_cell,.data=&scan};
    wc3_segment_normalize(query.direction);
    wc3FinePoint_t current={(int)floorf(source.x),(int)floorf(source.y)}, next=wc3_segment_point(&query,1.f);
    unsigned code=(next.x<current.x?8u:next.x>current.x?2u:0u)|(next.y<current.y?1u:next.y>current.y?4u:0u);
    wc3_segment_foot(&query,next,code);
    return scan.count;
}

/* Ordinary owned paths enable adaptive search at activation, including nearby orders. */
static bool move_adaptive_waypoint(moveAdaptiveQuery_t const *query, vec2_t *out) {
    movePathQuery_t const *input = query->input;
    wc3FineVector_t source = query->source, target = query->target;
    if (!input->units || !input->mover) return false;
    unsigned lane = 0;
    while (lane < 4 && move_acc_masks[lane] != input->geometry.blocked_flags) lane++;
    if (lane == 4) {
        fprintf(stderr,"WC3 adaptive routing: unsupported movement mask %02x\n",input->geometry.blocked_flags);
        return false;
    }
    moveFineRoute_t *route=query->route;
    bool retained=route && route->adaptive_points && route->adaptive_count &&
        route->adaptive_index<route->adaptive_count && route->mask==input->geometry.blocked_flags &&
        route->adaptive_revision==pathmap.revision && route->adaptive_radius==input->geometry.radius &&
        route->adaptive_goal.x==target.x && route->adaptive_goal.y==target.y;
    wc3FineVector_t point;
    if (retained) {
        vec2_t selected=route->adaptive_points[route->adaptive_index];
        point=route->adaptive_index ? (wc3FineVector_t){wc3_mul(selected.x,2),wc3_mul(selected.y,2)} : target;
    } else {
        move_acc_prepare();
        FOR_LOOP(level,4) move_acc.maps[level].classes = move_acc_classes[lane][level];
        wc3AccRequest_t req = {{wc3_mul(source.x,.5f),wc3_mul(source.y,.5f)},
            {wc3_mul(target.x,.5f),wc3_mul(target.y,.5f)},
            input->geometry.radius >= pathmap_cell_world_size() ? 2 : 1,BZ_WC3_UNIT_ACC_WORK};
        move_acc_object_rectangle(input->mover,true);
        move_acc_object_rectangle(input->target,true);
        uint32_t result=wc3_acc_route(&move_acc,&req,move_acc_points),count=result&0x7fffffffu;
        move_acc_object_rectangle(input->mover,false);
        move_acc_object_rectangle(input->target,false);
        if (!count) return false;
        wc3AccSelection_t selected=wc3_acc_select((wc3FineRoute_t){move_acc_points,count-1},false);
        assert(!selected.gate); /* Ordinary hierarchy search has no portal producer. */
        point=selected.index ? (wc3FineVector_t){wc3_mul(move_acc_points[selected.index].x,2),wc3_mul(move_acc_points[selected.index].y,2)} : target;
        if (route) {
            vec2_t *points=realloc(route->adaptive_points,count*sizeof(*points));
            if (!points) gi.error("WC3 adaptive routing: cannot retain %u points",count);
            route->adaptive_points=points; route->adaptive_count=count; route->adaptive_index=selected.index;
            route->adaptive_goal=(vec2_t){target.x,target.y}; route->adaptive_radius=input->geometry.radius;
            route->adaptive_revision=pathmap.revision;
            FOR_LOOP(i,count) points[i]=(vec2_t){move_acc_points[i].x,move_acc_points[i].y};
        }
    }
    vec2_t local = move_world_from_grid(point.x,point.y);
    movePathQuery_t nearby = *input; nearby.geometry.target = &local;
    /* The local leg targets the selected coarse point; retaining the formation
     * endpoint here bypassed the accelerator whenever a member had an offset. */
    vec2_t fine={point.x,point.y};
    if (input->fine_target) nearby.fine_target=&fine;
    /* Coarse representatives lie within the next8-base-cell region; refine that local leg with live units. */
    if (!G_BuildUnitMoveLocalRoute(&nearby,query->route,out)) return false;
    return true;
}

/* Original16e250 validates an offset with a separate30-attempt distance query, not a member fine route. */
bool G_AdjustUnitMoveFormationDestination(edict_t const *unit, vec2_t point, vec2_t *dest) {
    unsigned lane=0; uint32_t mask=M_UnitStaticPathingFlags(unit);
    while (lane<4 && move_acc_masks[lane]!=mask) lane++;
    if (lane==4) gi.error("Move formation: unsupported movement mask %02x",mask);
    move_acc_prepare();
    FOR_LOOP(i,4) move_acc.maps[i].classes=move_acc_classes[lane][i];
    wc3AccRequest_t req={{wc3_mul(point.x,.5f),wc3_mul(point.y,.5f)},
        {wc3_mul(dest->x,.5f),wc3_mul(dest->y,.5f)},1u<<(wc3_fine_class(unit->collision/pathmap_cell_world_size())>>1),30};
    wc3FineVector_t endpoint;
    uint32_t distance=wc3_acc_query_distance(&move_acc,&req,&endpoint);
    if (distance<=20) return false;
    *dest=distance==UINT32_MAX ? (vec2_t){wc3_mul(endpoint.x,2),wc3_mul(endpoint.y,2)} : point;
    return true;
}

/* Original16ce10/1697a0 owns a coarse group plan before16a790 passes its
 * selected destination to the member's separate path. Singleton layout adds
 * zero offset. Shared cohort storage/formation admission remains GROUP-04.6. */
bool G_UnitMoveGroupDestination(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *fine) {
    if (!input || !route || !fine || !input->geometry.target || !pathmap.width || !pathmap.height) return false;
    box2_t bounds=CM_GetWorldBounds();float cell=pathmap_cell_world_size();
    vec2_t clipped={wc3_point_order_coordinate(input->geometry.target->x,bounds.min.x,bounds.max.x,cell),
        wc3_point_order_coordinate(input->geometry.target->y,bounds.min.y,bounds.max.y,cell)};
    vec2_t goal=move_grid_from_world(clipped.x,clipped.y);
    /* Original16ce10 resamples16c940 and writes path+b4 only when it admits a
     * route. A surviving cached route keeps its footprint after a peer leaves;
     * the current live maximum is used when the destination/map/mask changes. */
    bool retained=route->group_points && route->group_count && route->group_index<route->group_count &&
        route->group_revision==pathmap.revision && route->mask==input->geometry.blocked_flags &&
        route->group_goal.x==goal.x && route->group_goal.y==goal.y;
    if (!retained) {
        unsigned lane=0;
        while (lane<4 && move_acc_masks[lane]!=input->geometry.blocked_flags) lane++;
        if (lane==4) {
            fprintf(stderr,"WC3 group routing: unsupported movement mask %02x\n",input->geometry.blocked_flags);
            return false;
        }
        vec2_t source=move_query_source(input);
        move_acc_prepare();
        FOR_LOOP(i,4) move_acc.maps[i].classes=move_acc_classes[lane][i];
        wc3AccRequest_t req={{wc3_mul(source.x,.5f),wc3_mul(source.y,.5f)},
            {wc3_mul(goal.x,.5f),wc3_mul(goal.y,.5f)},input->geometry.radius>=pathmap_cell_world_size()?2:1,BZ_WC3_GROUP_ACC_WORK};
        move_acc_object_rectangle(input->mover,true);
        move_acc_object_rectangle(input->target,true);
        uint32_t count=wc3_acc_route(&move_acc,&req,move_acc_points)&0x7fffffffu;
        move_acc_object_rectangle(input->mover,false);
        move_acc_object_rectangle(input->target,false);
        if (!count) return false;
        wc3AccSelection_t selected=wc3_acc_select((wc3FineRoute_t){move_acc_points,count-1},false);
        assert(!selected.gate); /* This engine hierarchy has no portal producer. */
        vec2_t *points=realloc(route->group_points,count*sizeof(*points));
        if (!points) gi.error("WC3 group routing: cannot retain %u points",count);
        route->group_points=points; route->group_count=count; route->group_index=selected.index;
        route->group_goal=goal; route->group_radius=input->geometry.radius; route->group_revision=pathmap.revision;
        route->mask=input->geometry.blocked_flags;
        route->adaptive_count=route->count=0;
        FOR_LOOP(i,count) points[i]=(vec2_t){move_acc_points[i].x,move_acc_points[i].y};
    }
    vec2_t point=route->group_points[route->group_index];
    *fine=route->group_index ? (vec2_t){wc3_mul(point.x,2),wc3_mul(point.y,2)} : route->group_goal;
    return true;
}

/* Singleton regroup succeeds after the member's actual arrival/zero commit.
 * The next member update rebuilds its path to the new group destination. */
bool G_AdvanceUnitMoveGroupDestination(moveFineRoute_t *route) {
    if (!route || !route->group_count || !route->group_index || route->group_index>=route->group_count) return false;
    FOR_LOOP(i,route->group_count) move_acc_points[i]=(wc3FineVector_t){route->group_points[i].x,route->group_points[i].y};
    wc3AccSelection_t selected=wc3_acc_select((wc3FineRoute_t){move_acc_points,route->group_index},false);
    assert(!selected.gate);
    route->group_index=selected.index;
    route->count=route->adaptive_count=0;
    route->index=route->adaptive_index=UINT32_MAX;
    return true;
}

/* Native167070 consumes an intermediate coarse leg even when a partial
 * fine search returns only the source. Retry belongs to adaptive index0. */
bool G_AdvanceUnitMoveAdaptiveDestination(moveFineRoute_t *route) {
    if (!route || !route->adaptive_points || !route->adaptive_count ||
        !route->adaptive_index || route->adaptive_index>=route->adaptive_count) return false;
    FOR_LOOP(i,route->adaptive_count)
        move_acc_points[i]=(wc3FineVector_t){route->adaptive_points[i].x,route->adaptive_points[i].y};
    wc3AccSelection_t selected=wc3_acc_select((wc3FineRoute_t){move_acc_points,route->adaptive_index},false);
    assert(!selected.gate);
    route->adaptive_index=selected.index;
    route->count=0; route->index=UINT32_MAX;
    return true;
}

/* Reconstruct destination-first points, then use the original next-point /
 * progressively farther selection policy. Move retains the selected turn. */
bool G_BuildUnitMoveLocalRoute(movePathQuery_t const *input, moveFineRoute_t *curve, vec2_t *out) {
    if (!input) return false;
    pathAccelParams_t const *params = &input->geometry;
    vec2_t source, target;
    if (!params || !params->from || !params->target || !out || !pathmap.width || !pathmap.height) return false;
    pathAccelParams_t dest = *params; dest.from = params->target;
    if (!G_ClosestMovePathPoint(params, &source)) return false;
    if (input->fine_target) target=*params->target;
    else if (!G_ClosestMovePathPoint(&dest,&target)) return false;
    vec2_t a = source.x==params->from->x && source.y==params->from->y ?
        move_query_source(input) : move_grid_from_world(source.x,source.y);
    vec2_t b = input->fine_target ? *input->fine_target : move_grid_from_world(target.x,target.y);
    wc3FinePoint_t start = { (int)floorf(a.x), (int)floorf(a.y) };
    wc3FinePoint_t goal = { (int)floorf(b.x), (int)floorf(b.y) };

    moveFineGraph_t graph = move_foot_shape(params);
    move_query_objects(&graph, input, NULL);
    bool target_hit = false;
    edict_t const *object = input->target;
    /* A suppressed target still terminates fine expansion at its region.
     * Native category2 captains have radius0 but retain one fine cell. */
    if (input->units && input->mover && !(input->mover->aiflags & AI_FLYING) && object && object->inuse &&
        (object->movement.captain_actor_type || (!IS_HOLLOW(object) && object->data.UnitData &&
         !G_UnitIsStructure(object) && !M_UnitMoveDisabled(object) && object->collision>0 &&
         !(object->aiflags & AI_FLYING)))) {
        graph.target_links=move_spatial+(object-g_edicts);
        graph.has_target = true;
        graph.target_hit = &target_hit;
    }
    wc3FineRequest_t req = { .start = {start.x, start.y}, .goal = {goal.x, goal.y},
        .width = pathmap.width, .height = pathmap.height,
        .budget = input->mover ? BZ_WC3_UNIT_FINE_WORK : BZ_WC3_FINE_WORK,
        .edges = move_fine_edges, .data = &graph, .target_hit = &target_hit };
    if (input->units && input->mover && !S_AdmitUnitMoveFineRequest((edict_t *)input->mover)) return false;
    int at = wc3_fine_search(&move_fine, &req);
    if (input->units && input->mover) S_ChargeUnitMoveFineRequest((edict_t *)input->mover,move_fine.pops);
    bool complete = at >= 0;
    /* Original148100 retains the nearest admitted chain after exhaustion.
     * Location Move can approach that endpoint without replacing its order. */
    if (at < 0 && input->units) at = (int)move_fine.nearest;
    if (at < 0) return false;
    wc3FinePoint_t endpoint = move_fine.nodes[at].pos;

    /* Original148100 adjusts a non-source nearest endpoint to its centre on
     * failure, even if the goal was admitted before its denied final pop.
     * Preserve the exact source for a one-node failure and retain the click. */
    wc3FineVector_t end = {b.x,b.y};
    if (!complete && (endpoint.x != start.x || endpoint.y != start.y))
        end = wc3_route_center(endpoint);
    wc3FineReconstruct_t route = {move_fine.nodes, move_fine.count, at,
        {a.x, a.y}, end};
    uint32_t count = wc3_fine_reconstruct(&route, move_fine_points, BZ_WC3_FINE_NODES);
    if (count < 2 && !input->fine_target) return false;
    if (curve) {
        vec2_t *points = realloc(curve->points,count*sizeof(*points));
        if (!points) gi.error("WC3 fine routing: cannot retain %u route points",count);
        /* Original166e90 starts at the destination if expansion observed no
         * obstruction; seeing a blocked cell selects the next parent point. */
        curve->points = points; curve->count = count; curve->partial=!complete;
        curve->index = move_fine.observed_obstruction && count>1 ? count-2 : 0;
        curve->mask = params->blocked_flags;
        FOR_LOOP(i,count) curve->points[i] = (vec2_t){move_fine_points[i].x,move_fine_points[i].y};
        *out = move_world_from_grid(curve->points[curve->index].x,curve->points[curve->index].y);
        return true;
    }
    wc3FineSegment_t query = { .start = {a.x, a.y},
        .cls = (unsigned)graph.size - 1, .cell = move_cell_ok, .data = &graph };
    uint32_t chosen = wc3_segment_waypoint(&query, (wc3FineRoute_t){move_fine_points, count - 1});
    wc3FineVector_t point = move_fine_points[chosen];
    /* Preserve admitted world words when the selected point is the exact goal. */
    *out = complete && endpoint.x == goal.x && endpoint.y == goal.y && chosen == 0
        ? target : move_world_from_grid(point.x, point.y);
    return true;
}

/* A retained coarse chain remains authoritative when a local fine buffer is exhausted. */
static bool move_find_route(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *out) {
    if (!input || !input->geometry.from || !input->geometry.target || !out || !pathmap.width || !pathmap.height) return false;
    vec2_t source,target;
    pathAccelParams_t dest=input->geometry; dest.from=dest.target;
    if (!G_ClosestMovePathPoint(&input->geometry,&source)) return false;
    if (input->fine_target) target=*input->geometry.target;
    else if (!G_ClosestMovePathPoint(&dest,&target)) return false;
    vec2_t a=source.x==input->geometry.from->x && source.y==input->geometry.from->y ?
        move_query_source(input) : move_grid_from_world(source.x,source.y);
    vec2_t b=input->fine_target && target.x==input->geometry.target->x && target.y==input->geometry.target->y ?
        *input->fine_target : move_grid_from_world(target.x,target.y);
    if (route && route->adaptive_count && (route->adaptive_revision!=pathmap.revision ||
        route->mask!=input->geometry.blocked_flags || route->adaptive_radius!=input->geometry.radius ||
        route->adaptive_goal.x!=b.x || route->adaptive_goal.y!=b.y))
        route->adaptive_count=route->adaptive_index=0;
    int dx=abs((int)floorf(a.x)-(int)floorf(b.x)),dy=abs((int)floorf(a.y)-(int)floorf(b.y));
    if ((input->units && input->mover) || (route && route->adaptive_count) ||
        dx>PATH_ACCEL_MAX_DISTANCE || dy>PATH_ACCEL_MAX_DISTANCE)
        return move_adaptive_waypoint(&(moveAdaptiveQuery_t){input,{a.x,a.y},{b.x,b.y},route},out);
    return G_BuildUnitMoveLocalRoute(input,route,out);
}

/* A loaded world has a new process-local bake epoch, with its saved terrain and
 * obstacles already restored. Retained routes belong to that rebuilt world. */
void G_RebindSavedMoveRoutes(void) {
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) {
        moveFineRoute_t *route=&level.move_groups[i]->route;
        if (route->adaptive_count) route->adaptive_revision=pathmap.revision;
        if (route->group_count) route->group_revision=pathmap.revision;
    }
    FILTER_EDICTS(ent,ent->inuse) {
        moveFineRoute_t *route=&ent->movement.fine_route;
        if (route->adaptive_count) route->adaptive_revision=pathmap.revision;
        if (route->group_count) route->group_revision=pathmap.revision;
    }
}

bool G_FindUnitMovePathWaypoint(movePathQuery_t const *input, vec2_t *out) {
    return move_find_route(input,NULL,out);
}

bool G_BuildUnitMoveFineRoute(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *out) {
    return route && move_find_route(input,route,out);
}

/* Original16fbd0 subtracts the predicted fine source from the returned native
 * waypoint before vector-heading calculation; world publication is downstream. */
vec2_t G_MoveFineRouteDirection(movePathQuery_t const *input, moveFineRoute_t const *route) {
    assert(input && route && route->points && route->index<route->count);
    vec2_t source=move_query_source(input), target=route->points[route->index];
    return (vec2_t){wc3_sub(target.x,source.x),wc3_sub(target.y,source.y)};
}

/* Original167070 retains the current point until .49 cells, then165e60 skips visible successors. */
bool G_AdvanceUnitMoveFineRoute(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *out) {
    if (!input || !input->geometry.from || !out || !route || !route->points || route->count < 2 ||
        route->count > BZ_WC3_FINE_NODES || route->index >= route->count || route->mask != input->geometry.blocked_flags)
        return false;
    vec2_t source = move_query_source(input);
    if (route->adaptive_count) {
        if (!input->geometry.target) return false;
        vec2_t goal=input->fine_target ? *input->fine_target : move_grid_from_world(input->geometry.target->x,input->geometry.target->y);
        if (!route->adaptive_points || route->adaptive_count>BZ_WC3_FINE_NODES || route->adaptive_index>=route->adaptive_count ||
            route->adaptive_revision!=pathmap.revision || route->adaptive_radius!=input->geometry.radius ||
            route->adaptive_goal.x!=goal.x || route->adaptive_goal.y!=goal.y) return false;
        if (route->adaptive_index) {
            vec2_t point=route->adaptive_points[route->adaptive_index];
            float dx=wc3_sub(wc3_mul(source.x,.5f),point.x),dy=wc3_sub(wc3_mul(source.y,.5f),point.y);
            float range=wc3_float(0x3efae148);
            if (wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy))<=wc3_mul(range,range)) {
                FOR_LOOP(i,route->adaptive_count) move_acc_points[i]=(wc3FineVector_t){route->adaptive_points[i].x,route->adaptive_points[i].y};
                wc3AccSelection_t selected=wc3_acc_select((wc3FineRoute_t){move_acc_points,route->adaptive_index},false);
                assert(!selected.gate);
                route->adaptive_index=selected.index;
                return move_adaptive_waypoint(&(moveAdaptiveQuery_t){input,{source.x,source.y},{goal.x,goal.y},route},out);
            }
        }
    }
    vec2_t point = route->points[route->index];
    float dx = wc3_sub(point.x,source.x), dy = wc3_sub(point.y,source.y), range = wc3_float(0x3efae148);

    if (wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy)) <= wc3_mul(range,range)) {
        if (!route->index) return false;
        FOR_LOOP(i,route->count) move_fine_points[i] = (wc3FineVector_t){route->points[i].x,route->points[i].y};
        moveFineGraph_t graph = move_foot_shape(&input->geometry); move_query_objects(&graph,input,NULL);
        wc3FineSegment_t segment = {.start={source.x,source.y},.cls=(unsigned)graph.size-1,.cell=move_cell_ok,.data=&graph};
        route->index = wc3_segment_waypoint(&segment,(wc3FineRoute_t){move_fine_points,route->index});
        point = route->points[route->index];
    }
    *out = move_world_from_grid(point.x,point.y);
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
