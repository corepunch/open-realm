#include "../common/wc3_pathing_gate.h"
#include "g_local.h"
#include "../common/wc3_pathing_route.h"
#include "../common/wc3_pathing_coordinates.h"
#include "../common/wc3_pathing_placement.h"
#include "../common/wc3_pathing_adaptive.h"
#include "../common/wc3_pathing_widget.h"
#include "../common/wc3_pathing_records.h"
#include "../common/wc3_pathing_cell.h"
#include "../common/wc3_pathing_regions.h"

typedef struct {
    int size;
    uint8_t flags;
    movePathQuery_t const *query;
    edict_t const *target; /* Search observation identity, separate from current suppression. */
    bool has_target, endpoint, suppress_target, counted_scope;
    uint32_t level, cell_epoch;
    bool *target_hit;
    wc3FineBox_t *rejection; /* query-local placement witness, never retained */
} moveFineGraph_t;
/* Setup0/1 retains query parameters; suppression still belongs to the current
 * synchronous caller. Never retain the caller's stack-local query pointer. */
static struct {
    int size;
    uint8_t flags;
    edict_t const *target;
    uint32_t target_spawn;
    bool has_target;
} move_fine_profile;
static uint64_t move_spatial_serial;
#ifdef BZ_TESTS
static uint32_t move_spatial_visits;
static uint32_t move_spatial_link_visits;
uint32_t G_TestMoveSpatialLinkVisits(bool reset) {
    uint32_t count=move_spatial_link_visits;
    if(reset)move_spatial_link_visits=0;
    return count;
}
uint32_t G_TestMoveSpatialVisits(bool reset) {
    uint32_t count=move_spatial_visits;
    if(reset)move_spatial_visits=0;
    return count;
}
#endif
/* Raw record heads and their occupied summary belong to Move. */
static uint32_t move_spatial_width,move_spatial_height;
static uint64_t *move_static_edges[4];
static uint32_t move_edge_epoch=1,move_cell_epoch;
static uint64_t *move_cell_lookup;

static void move_invalidate_edges(void);
typedef struct {
    vec2_t world, fine, published;
    float collision;
    bool valid, pose_valid;
} moveSpatialGeometry_t;
static moveSpatialGeometry_t move_spatial_geometry[MAX_ENTITIES];
/* Two-level dirty bits preserve the former ascending publication order while
 * visiting only changed owners. No entity scan occurs on an unchanged query. */
static entitySet_t move_dirty;
static void (*move_link)(edict_t *);
static void move_spatial_prepare(void);

void G_MarkMoveSpatialObject(edict_t const *ent) {
    if(!ent)return;
    uintptr_t index=((uintptr_t)ent-(uintptr_t)g_edicts)/sizeof(*ent);
    /* Metadata inspectors also bind non-world actor records. Only allocator
     * edicts participate in the simulation spatial index. */
    if(!g_edicts || index>=MAX_ENTITIES)return;
    entity_set_put(&move_dirty,index,true);
}

static void move_spatial_clean(uint32_t index) {
    entity_set_put(&move_dirty,index,false);
}

/* Link is the game-owned geometry observation point for non-Move writers.
 * Do not publish here: presentation links must not advance the native pose. */
static void move_link_entity(edict_t *ent) {
    G_MarkMoveSpatialObject(ent);
    move_link(ent);
    G_AcquisitionEntityLinked(ent);
}

void G_InitMoveSpatialLink(void) {
    move_link=gi.LinkEntity;
    gi.LinkEntity=move_link_entity;
}

static void move_spatial_sync(void) {
    move_spatial_prepare();
    for(uint32_t index=entity_set_next(&move_dirty,0);index<MAX_ENTITIES;index=entity_set_next(&move_dirty,index+1)) {
        G_PublishMoveSpatialObject(g_edicts+index);
    }
}

void G_SyncMoveSpatial(void) {move_spatial_sync();}

void G_ClearMoveSpatial(void) {
    S_ClearMoveProximity();
    S_ClearMoveFineSpatial();move_spatial_serial=0;
    memset(move_spatial_geometry,0,sizeof(move_spatial_geometry));
    move_dirty=(entitySet_t){0};
}

static void move_remove_fine_spatial(edict_t const *ent) {
    uint32_t index=ent-g_edicts;
    move_spatial_clean(index);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    if(map->objects && wc3_records_owned(map,index)) {
#ifdef BZ_TESTS
        wc3FineBox_t box=wc3_records_clip(map,wc3_records_owned(map,index)->box);
        move_spatial_link_visits+=MAX(0,box.max.x-box.min.x)*MAX(0,box.max.y-box.min.y);
#endif
        uint32_t id=map->objects[index];wc3_records_retire(map,id);map->objects[index]=WC3_RECORD_END;
    }
    move_spatial_geometry[index].valid=false;
}

void G_RemoveMoveSpatialObject(edict_t const *ent) {
    if (ent) {
        S_RemoveMoveProximity(ent);
        move_remove_fine_spatial(ent);
        S_RetireMoveRegions(ent);
    }
}
static wc3FineSearch_t move_fine;
static wc3FineVector_t move_fine_points[BZ_WC3_FINE_NODES];
static wc3AccSearch_t move_acc;
static wc3AccGate_t move_acc_gates[BZ_WC3_GATE_RECORDS];
static wc3FineVector_t move_acc_points[BZ_WC3_ACC_ROUTE_NODES];
static void *move_acc_storage;
static uint32_t move_map_revision, move_acc_width, move_acc_height;
static uint8_t *move_acc_classes[4][4];
static uint8_t *move_acc_markers;
static uint8_t const move_acc_masks[4] = {2,4,0x40,0x80};
typedef struct {
    wc3FineBox_t box;
    vec2_t point;
    pathTex_t const *texture;
    uint64_t pixels;
    uint32_t birth, turn, flags;
    bool active, surface;
} moveStaticPathing_t;
static moveStaticPathing_t move_static[MAX_ENTITIES];
static void move_acc_prepare(void);
static void move_acc_enable_gates(void);
static void move_acc_initialize(void);
static void move_acc_rebuild_rectangle(wc3FineBox_t box, bool clear);
typedef struct {
    movePathQuery_t const *input;
    wc3FineVector_t source, target;
    moveFineRoute_t *route;
    uint32_t *status;
    bool retain_fine;
} moveAdaptiveQuery_t;
static bool move_find_route(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *out,uint32_t *status);

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
    return G_IsItem(ent) ? G_ItemPathingCategory() : S_UnitMoveCategory(ent);
}

/* Region categories belong to their pooled identities; ordinary category and
 * transient flags follow the live ability owner. Reads never publish links. */
static wc3FineObject_t move_cell_object(void *data,wc3RecordObject_t const *object) {
    (void)data;
    if(object->flags&WC3_RECORD_REGION)return (wc3FineObject_t){object->category,object->flags,true};
    edict_t const *ent=g_edicts+object->owner;
    return (wc3FineObject_t){(object->category&WC3_RECORD_INSERT)|entity_dynamic_pathing_flags(ent),
        object->flags|(G_IsItem(ent) ? 0 : S_UnitMoveFineObjectFlags(ent)),true};
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
static void move_cell_world_dimensions(float *, float *);
#define PATH_CELL_WORLD_DIMENSIONS move_cell_world_dimensions
#define PATHMAP_SETUP_COMPLETE move_acc_initialize
#define PATH_JOB_RUN G_RunPathJob
#define PATH_JOB_WAIT G_WaitPathJob
#define CM_BakeStaticObstacles move_bake_static_masks
#include "server/sv_routing.c"
#undef CM_BakeStaticObstacles
#undef PATHMAP_SETUP_COMPLETE
#undef PATH_JOB_RUN
#undef PATH_JOB_WAIT
#undef PATH_CELL_WORLD_DIMENSIONS

static void move_invalidate_edges(void) {
    if(++move_edge_epoch==(1u<<28)) {
        FOR_LOOP(lane,4)if(move_static_edges[lane])
            memset(move_static_edges[lane],0,(size_t)pathmap.width*pathmap.height*sizeof(uint64_t));
        move_edge_epoch=1;
    }
}
/* Fine edge caching follows baked terrain, independently of the deliberately
 * delayed adaptive hierarchy. A terrain edit must invalidate fine admission. */
static void CM_BakeStaticMasks(void) {
    if(!pathmap.terrain || !pathmap.original)return;
    move_bake_static_masks();
    move_invalidate_edges();
}

static uint32_t move_terrain_word(wc3FinePoint_t pos) {
    if(!is_valid_point(pos.x,pos.y))return UINT32_MAX;
    uint32_t cell=(uint32_t)pos.y*pathmap.width+pos.x;
    return (uint32_t)((uint8_t const *)pathmap.terrain)[cell]<<24;
}

/* Footprint publication is independent of the authored terrain lanes. */
bool G_EntityHasStaticPathing(edict_t const *ent) {
    return entity_blocks_static_pathing(ent);
}

/* Map extents/dimensions describe cell sizes; simulation coordinates then use
 * the direct software transform. Stock WC3 WPM cells are32 world units. */
#ifdef BZ_TESTS
static uint32_t move_geometry_builds, move_group_goal_conversions;
uint32_t G_TestMoveGroupGoalConversions(bool reset) {
    uint32_t result = move_group_goal_conversions;
    if (reset) move_group_goal_conversions = 0;
    return result;
}
uint32_t G_TestMoveGeometryBuilds(bool reset) {
    uint32_t result = move_geometry_builds;
    if (reset) move_geometry_builds = 0;
    return result;
}
#endif
typedef struct {
    box2_t bounds;
    war3map_t const *map;
    vec2_t center, cell, native_scale, extent_cell;
    uint32_t width, height, terrain_width, terrain_height, revision;
    bool valid;
} moveGridGeometry_t;
/* Workers share map inputs, but never write another thread's derived cache. */
static _Thread_local moveGridGeometry_t move_grid_geometry;

static moveGridGeometry_t const *move_geometry(void) {
    bool have_world = world.map != NULL;
#ifdef BZ_TESTS
    have_world |= test_world_bounds_set;
#endif
    box2_t bounds = have_world ? CM_GetWorldBounds() : (box2_t){0};
    moveGridGeometry_t *geometry = &move_grid_geometry;
    vec2_t center = world.map ? world.map->center : (vec2_t){0};
    uint32_t width = world.map ? world.map->width : 0, height = world.map ? world.map->height : 0;
    if (geometry->valid && geometry->map == world.map && geometry->width == pathmap.width &&
        geometry->height == pathmap.height && geometry->terrain_width == width &&
        geometry->terrain_height == height && !memcmp(&geometry->center, &center, sizeof(center)) &&
        !memcmp(&geometry->bounds, &bounds, sizeof(bounds))) return geometry;
#ifdef BZ_TESTS
    move_geometry_builds++;
#endif
    uint32_t revision = geometry->revision + 1;
    if (!revision) { gi.error("Move geometry revision exhausted"); abort(); }
    *geometry = (moveGridGeometry_t){.map = world.map, .center = center, .bounds = bounds, .revision = revision,
        .width = pathmap.width, .height = pathmap.height, .terrain_width = width, .terrain_height = height,
        .cell = {FLT_MAX, FLT_MAX}, .valid = true};
    geometry->extent_cell = (vec2_t){(bounds.max.x - bounds.min.x) / pathmap.width,
                                   (bounds.max.y - bounds.min.y) / pathmap.height};
    geometry->native_scale = (vec2_t){wc3_div(32, geometry->extent_cell.x),
                                    wc3_div(32, geometry->extent_cell.y)};
    /* Retain the legacy rounding through the denormalized map transform.
     * Its subtraction can differ from the extent division on uneven maps. */
    if (pathmap.width) {
        vec2_t a = CM_GetDenormalizedMapPosition(0, 0);
        vec2_t b = CM_GetDenormalizedMapPosition(1.f / pathmap.width, 0);
        geometry->cell.x = fabsf(b.x - a.x);
    }
    if (pathmap.height) {
        vec2_t a = CM_GetDenormalizedMapPosition(0, 0);
        vec2_t b = CM_GetDenormalizedMapPosition(0, 1.f / pathmap.height);
        geometry->cell.y = fabsf(b.y - a.y);
    }
    return geometry;
}
static void move_cell_world_dimensions(float *cell_x, float *cell_y) {
    moveGridGeometry_t const *geometry = move_geometry();
    *cell_x = geometry->cell.x; *cell_y = geometry->cell.y;
}
static vec2_t move_grid_from_world(float x, float y) {
    moveGridGeometry_t const *geometry = move_geometry();
    return (vec2_t){wc3_grid_coordinate(x, geometry->bounds.min.x, geometry->extent_cell.x),
                   wc3_grid_coordinate(y, geometry->bounds.min.y, geometry->extent_cell.y)};
}

/* A published native pose is authoritative; world inversion can round into a
 * different heading or progress decision. Synthetic maps retain their scale. */
static vec2_t move_query_source(movePathQuery_t const *input) {
    if (!input->fine) return move_grid_from_world(input->geometry.from->x,input->geometry.from->y);
    moveGridGeometry_t const *geometry = move_geometry();
    return (vec2_t){wc3_mul(input->fine->x,geometry->native_scale.x),
                   wc3_mul(input->fine->y,geometry->native_scale.y)};
}

/* Cell centres and retained route points must not round through map fractions. */
static vec2_t move_world_from_grid(float x, float y) {
    moveGridGeometry_t const *geometry = move_geometry();
    return (vec2_t){wc3_world_coordinate(x, geometry->bounds.min.x, geometry->extent_cell.x),
                   wc3_world_coordinate(y, geometry->bounds.min.y, geometry->extent_cell.y)};
}

static void move_free_adaptive(void) {
    free(move_acc_storage);move_acc_storage=NULL;move_acc_markers=NULL;
    wc3_acc_free(&move_acc);
    move_acc_width=move_acc_height=0;
}

/* Classification is derived map state; release it when the game module shuts down. */
void G_FreeMovePathCache(void) {
    S_FreeMoveProximity();
    CM_FinishPathJobs();
    move_grid_geometry.valid = false;
    move_free_adaptive();
    wc3_fine_free(&move_fine);
    memset(&move_fine_profile,0,sizeof(move_fine_profile));
    S_FreeMoveFineSpatial();
    FOR_LOOP(lane,4){free(move_static_edges[lane]);move_static_edges[lane]=NULL;}
    move_edge_epoch=1;
    free(move_cell_lookup);move_cell_lookup=NULL;move_cell_epoch=0;
    move_spatial_width=move_spatial_height=0;
    memset(move_spatial_geometry,0,sizeof(move_spatial_geometry));
}

/* Four ordinary static lanes share node-index scratch; cache the retail 2x fine base and three parents. */
static void move_acc_prepare(void) {
    /* Original15ab60 adds16 fine cells, truncates division by2, then adds1.
     * Padding is allocation space, not additional blocked terrain. */
    uint32_t width=(pathmap.width+16)/2+1, height=(pathmap.height+16)/2+1;
    if (!pathmap.width || !pathmap.height) return;
    if (move_acc_storage && move_acc_width == width && move_acc_height == height) return;
    if (move_acc_width != width || move_acc_height != height || !move_acc_storage) {
        /* Lazy hierarchy allocation is not a map release. Fine/proximity
         * objects may already be live, including while preparing a save. */
        move_free_adaptive();
        uint32_t cells = 0;
        FOR_LOOP(level,4) cells += (width>>level)*(height>>level);
        move_acc_storage = malloc((size_t)cells*(sizeof(int)+4)+(size_t)width*height);
        if (!move_acc_storage) gi.error("WC3 adaptive routing: cannot allocate %u hierarchy cells",cells);
        int *indices = move_acc_storage;
        uint8_t *classes = (uint8_t *)(indices+cells);
        FOR_LOOP(level,4) {
            uint32_t w = width>>level, h = height>>level;
            move_acc.maps[level] = (wc3AccMap_t){.width=w,.height=h,.indices=indices}; indices += w*h;
            FOR_LOOP(lane,4) { move_acc_classes[lane][level] = classes; classes += w*h; }
        }
        move_acc_markers=classes;memset(move_acc_markers,0,(size_t)width*height);
        move_acc_width = width; move_acc_height = height;
        move_acc.reuse_indices=true; /* This owner retains all four index planes. */
    }
    FOR_LOOP(lane,4) FOR_LOOP(level,4)
        memset(move_acc_classes[lane][level],0,(size_t)move_acc.maps[level].width*move_acc.maps[level].height);
    move_acc_rebuild_rectangle((wc3FineBox_t){{0,0},{pathmap.width,pathmap.height}},false);
}

/* Original15d360 clips once in fine coordinates. Each level independently
 * visits floor(min/scale)..floor(max/scale), including the upper edge. */
static void move_acc_rebuild_rectangle_from(wc3FineBox_t box, bool clear, unsigned first_level) {
    static unsigned const lanes[]={0,3,2,1}; /*15cf80:06/80/40/04, storage unchanged. */
    static wc3FinePoint_t const corners[]={{0,0},{1,0},{1,1},{0,1}};
    box.min.x=MAX(0,box.min.x); box.min.y=MAX(0,box.min.y);
    box.max.x=MIN((int)pathmap.width,box.max.x); box.max.y=MIN((int)pathmap.height,box.max.y);
    if (box.min.x>=box.max.x || box.min.y>=box.max.y) return;
    for(unsigned level=first_level;level<4;level++) {
        wc3AccMap_t const *map=move_acc.maps+level;
        unsigned scale=2u<<level;
        unsigned minx=box.min.x/scale,miny=box.min.y/scale;
        unsigned maxx=MIN(map->width,box.max.x/scale+1),maxy=MIN(map->height,box.max.y/scale+1);
        for(unsigned y=miny;y<maxy;y++) for(unsigned x=minx;x<maxx;x++) FOR_LOOP(order,4) {
            unsigned lane=lanes[order];
            unsigned value=0;
            if (!level) {
                unsigned blocked=0;
                if (!clear) FOR_LOOP(corner,4) {
                    wc3FinePoint_t pos={x*2+corners[corner].x,y*2+corners[corner].y};
                    uint32_t mask=lane ? move_acc_masks[lane] : 6;
                    bool allowed=!(move_terrain_word(pos)&(mask<<24));
                    wc3SpatialRecords_t *records=S_GetMoveFineSpatial();
                    if(allowed && records->cells) {
                        wc3CellQuery_t query={.mode=WC3_CELL_HIERARCHY,.mask=mask|mask<<24,
                            .target=WC3_RECORD_END,.describe=move_cell_object};
                        allowed=wc3_records_cell(records,pos,0,&query).value;
                    }
                    blocked+=!allowed;
                }
                value=blocked==4 ? 1 : blocked ? 2 : 0;
            } else {
                wc3AccMap_t const *child_map=move_acc.maps+level-1;
                uint8_t const *child=move_acc_classes[lane][level-1];
                value=wc3_gate_parent(child,level==1?move_acc_markers:NULL,
                    child_map->width,child_map->height,x*2,y*2);
            }
            move_acc_classes[lane][level][y*map->width+x]=value;
        }
    }
}

static void move_acc_rebuild_rectangle(wc3FineBox_t box, bool clear) {
    move_acc_rebuild_rectangle_from(box,clear,0);
}

/* Marker publication does not expose pending terrain edits by rebuilding base
 * classes. It overwrites the source byte and publishes only the three parents. */
void G_PublishWaygateSource(box2_t const *rectangle,uint8_t id) {
    if(!rectangle)return;
    if(!pathmap.width || !pathmap.height) {
        fprintf(stderr,"WC3 Waygate: source publication before path map construction id=%u\n",id);return;
    }
    move_acc_prepare();
    vec2_t min=move_grid_from_world(MIN(rectangle->min.x,rectangle->max.x),MIN(rectangle->min.y,rectangle->max.y));
    vec2_t max=move_grid_from_world(MAX(rectangle->min.x,rectangle->max.x),MAX(rectangle->min.y,rectangle->max.y));
    wc3FineBox_t box={{(int)floorf(min.x),(int)floorf(min.y)},
        {(int)(wc3_int_bits(wc3_floor_bits(wc3_float_bits(max.x)))+1u),
         (int)(wc3_int_bits(wc3_floor_bits(wc3_float_bits(max.y)))+1u)}};
    box.min.x=MAX(0,box.min.x);box.min.y=MAX(0,box.min.y);
    box.max.x=MIN((int)pathmap.width,box.max.x);box.max.y=MIN((int)pathmap.height,box.max.y);
    if(box.min.x>=box.max.x || box.min.y>=box.max.y)return;
    wc3_gate_stamp(move_acc_markers,move_acc_width,move_acc_height,box.min.x,box.min.y,box.max.x,box.max.y,id);
    move_acc_rebuild_rectangle_from(box,false,1);
}

static void move_acc_initialize(void) {
    G_FreeMovePathCache(); memset(move_static,0,sizeof(move_static));
    if (!++move_map_revision) ++move_map_revision;
    move_acc_prepare();
}

/* A footprint producer publishes even when its pixels were already blocked
 * by terrain. Comparing baked bytes alone would miss that refresh. Textures
 * are game resources; retain identity/contents, never ownership of them. */
static moveStaticPathing_t move_static_pathing(edict_t const *ent) {
    moveStaticPathing_t state={0};
    if (!entity_blocks_static_pathing(ent)) return state;
    point2_t p=LocationToPathMap(&ent->s.origin2);
    pathTexTransform_t transform=CM_GetPathTexTransform(ent);
    unsigned width=transform.width,height=transform.height;
    if (!ent->pathtex) width=height=MAX(1,collision_radius_cells(ent->collision)*2);
    state.active=true; state.texture=ent->pathtex; state.birth=ent->spawn_time;
    state.point=ent->s.origin2;
    state.turn=transform.turn; state.flags=entity_static_pathing_flags(ent);
    state.surface=entity_is_live_walkable_surface(ent);
    state.box=(wc3FineBox_t){{p.x-(int)width/2,p.y-(int)height/2},
        {p.x-(int)width/2+(int)width+1,p.y-(int)height/2+(int)height+1}};
    if (ent->pathtex) {
        state.pixels=UINT64_C(14695981039346656037);
        FOR_LOOP(i,ent->pathtex->width*ent->pathtex->height) {
            uint8_t const *pixel=(uint8_t const *)(ent->pathtex->map+i);
            FOR_LOOP(k,sizeof(*ent->pathtex->map)) state.pixels=(state.pixels^pixel[k])*UINT64_C(1099511628211);
        }
    }
    return state;
}

/* Original1eab40 publishes the full hierarchy after generated map main and
 * bulk widget setup. Subsequent terrain natives have no such full refresh. */
void G_FinishMovePathingInitialization(void) {
    CM_BakeStaticMasks();
    move_acc_prepare();
    FOR_LOOP(i,globals.num_edicts)if(entity_blocks_static_pathing(g_edicts+i)) {
        if(!S_GetMoveRegions(i)->count)S_PublishMoveRegions(g_edicts+i);
    }
    move_acc_rebuild_rectangle((wc3FineBox_t){{0,0},{pathmap.width,pathmap.height}},false);
    FOR_LOOP(i,MAX_ENTITIES)
        move_static[i]=i<globals.num_edicts ? move_static_pathing(g_edicts+i) : (moveStaticPathing_t){0};
}

/* Fine masks/legacy fields follow every bake. Adaptive lanes follow only
 * footprint producers; existing paths remain owned until their normal retry,
 * completion or replacement, rather than inheriting a fine-field epoch. */
void CM_BakeStaticObstacles(void) {
    if (!pathmap.terrain || !pathmap.original) return;
    move_acc_prepare();
    CM_BakeStaticMasks();
    FOR_LOOP(i,MAX_ENTITIES) {
        moveStaticPathing_t next=i<globals.num_edicts ? move_static_pathing(g_edicts+i) : (moveStaticPathing_t){0};
        moveStaticPathing_t *before=move_static+i;
        if (before->active==next.active && before->texture==next.texture && before->pixels==next.pixels &&
            !memcmp(&before->point,&next.point,sizeof(next.point)) &&
            before->birth==next.birth && before->turn==next.turn && before->flags==next.flags && before->surface==next.surface &&
            before->box.min.x==next.box.min.x && before->box.min.y==next.box.min.y &&
            before->box.max.x==next.box.max.x && before->box.max.y==next.box.max.y &&
            (!next.active || S_GetMoveRegions(i)->count)) continue;
        if(next.active)S_PublishMoveRegions(g_edicts+i);
        else if(i<globals.num_edicts && (g_edicts[i].svflags&SVF_DEADMONSTER))S_RetireMoveRegions(g_edicts+i);
        else if(i<globals.num_edicts)S_UnrasterMoveRegions(g_edicts+i);
        if (before->active) move_acc_rebuild_rectangle(before->box,false);
        if (next.active) move_acc_rebuild_rectangle(next.box,false);
        *before=next;
    }
}

uint32_t G_GetMoveAdaptiveStateSize(void) {
    if (!pathmap.width || !pathmap.height) return 0;
    move_acc_prepare(); uint32_t size=0;
    FOR_LOOP(level,4) size+=move_acc.maps[level].width*move_acc.maps[level].height*4;
    return size+move_acc_width*move_acc_height;
}

point2_t G_GetMoveAdaptiveMapSize(unsigned level) {
    move_acc_prepare();
    if (level>=4 || !pathmap.width || !pathmap.height) return (point2_t){0,0};
    return (point2_t){move_acc.maps[level].width,move_acc.maps[level].height};
}

bool G_GetMoveAdaptiveState(uint8_t *data, uint32_t size) {
    if (size!=G_GetMoveAdaptiveStateSize() || (size && !data)) return false;
    if (!size) return true;
    FOR_LOOP(level,4) FOR_LOOP(lane,4) {
        uint32_t n=move_acc.maps[level].width*move_acc.maps[level].height;
        memcpy(data,move_acc_classes[lane][level],n); data+=n;
    }
    memcpy(data,move_acc_markers,(size_t)move_acc_width*move_acc_height);
    return true;
}

bool G_SetMoveAdaptiveState(uint8_t const *data, uint32_t size) {
    if (size!=G_GetMoveAdaptiveStateSize() || (size && !data)) return false;
    FOR_LOOP(i,size-move_acc_width*move_acc_height) if (data[i]>2) return false;
    if (!size) return true;
    FOR_LOOP(level,4) FOR_LOOP(lane,4) {
        uint32_t n=move_acc.maps[level].width*move_acc.maps[level].height;
        memcpy(move_acc_classes[lane][level],data,n); data+=n;
    }
    memcpy(move_acc_markers,data,(size_t)move_acc_width*move_acc_height);
    return true;
}

/* Saved publication state is authoritative: rebuilding all adaptive classes
 * here would expose pending terrain edits that were not visible when saved. */
void G_RebuildSavedMovePathing(void) {
    CM_BakeStaticMasks();
    FOR_LOOP(i,MAX_ENTITIES)
        move_static[i]=i<globals.num_edicts ? move_static_pathing(g_edicts+i) : (moveStaticPathing_t){0};
    G_RebindSavedMoveRoutes();
}

#ifdef BZ_TESTS
/* Construction oracles explicitly call15d360 after terrain writes. Public
 * terrain natives intentionally do not perform this producer operation. */
void G_TestMovePathRefresh(point2_t min, point2_t max) {
    move_acc_prepare();
    move_acc_rebuild_rectangle((wc3FineBox_t){{min.x,min.y},{max.x,max.y}},false);
}
wc3AccSearch_t const *G_TestMoveAdaptiveSearch(void) {
    return &move_acc;
}
wc3FineVector_t const *G_TestMoveRouteScratch(bool accelerated) {
    return accelerated ? move_acc_points : move_fine_points;
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
unsigned G_TestMoveTerrainByte(unsigned x,unsigned y) {
    return move_terrain_word((wc3FinePoint_t){x,y})>>24;
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

static void move_spatial_prepare(void) {
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    bool changed=!map->cells || map->width!=pathmap.width || map->height!=pathmap.height;
    S_PrepareMoveFineSpatial();
    move_spatial_width=map->width;move_spatial_height=map->height;
    if(!changed)return;
    memset(move_spatial_geometry,0,sizeof(move_spatial_geometry));
    FOR_LOOP(i,globals.num_edicts)if(g_edicts[i].inuse)G_MarkMoveSpatialObject(g_edicts+i);
}

/* Original1603d0/160590 publish occupancy only when the native pose commits.
 * Presentation samples predict ahead without moving the fine object. */
static vec2_t move_object_point(edict_t const *ent) {
    if (!ent->movement.pose_valid ||
        wc3_float_bits(ent->movement.pose_world.x)!=wc3_float_bits(ent->s.origin2.x) ||
        wc3_float_bits(ent->movement.pose_world.y)!=wc3_float_bits(ent->s.origin2.y))
        return move_grid_from_world(ent->s.origin2.x,ent->s.origin2.y);
    moveGridGeometry_t const *geometry = move_geometry();
    return (vec2_t){wc3_mul(ent->movement.fine_pose.x,geometry->native_scale.x),
        wc3_mul(ent->movement.fine_pose.y,geometry->native_scale.y)};
}

/* Authored fine publication is independent of the optional Move ability.
 * Buildings retain their own category-zero rectangle independently of static
 * textures. Items publish their own mover category (BASE-02.2). */
static bool move_has_spatial_record(edict_t const *ent) {
    if (ent->movement.captain_actor_type) return ent->inuse;
    return !IS_HOLLOW(ent) && ent->collision>0 &&
        (ent->data.UnitData || (G_IsItem(ent) && ent->item->in_world));
}

/* Flight publishes an active fine rectangle with category zero. Spatial
 * lifetime is independent of eligibility for a ground collision query. */
/* Pose commits own publication; dirty owners are synchronized before queries.
 * Authored size/category and explicit world writes retain their order. Intersection
 * links survive; leaving/re-entering prepends even within one JASS callback. */
void G_PublishMoveSpatialObject(edict_t const *ent) {
#ifdef BZ_TESTS
    move_spatial_visits++;
#endif
    if (!ent || !pathmap.width || !pathmap.height) return;
    S_PublishMoveProximity(ent);
    move_spatial_clean(ent-g_edicts);
    if (!ent->inuse || !move_has_spatial_record(ent)) {
        if(move_spatial_geometry[ent-g_edicts].valid ||
            G_GetMoveSpatialObject(ent-g_edicts))
            move_remove_fine_spatial(ent);
        return;
    }
    move_spatial_prepare();
    move_spatial_clean(ent-g_edicts);
    uint32_t index=ent-g_edicts;
    moveSpatialGeometry_t *geometry=move_spatial_geometry+index;
    if(geometry->valid && geometry->pose_valid==ent->movement.pose_valid &&
        !memcmp(&geometry->world,&ent->s.origin2,sizeof(vec2_t)) &&
        !memcmp(&geometry->fine,&ent->movement.fine_pose,sizeof(vec2_t)) &&
        !memcmp(&geometry->published,&ent->movement.pose_world,sizeof(vec2_t)) &&
        wc3_float_bits(geometry->collision)==wc3_float_bits(ent->collision))return;
    vec2_t point=move_object_point(ent);
    wc3FineBox_t box=wc3_fine_cover(wc3_fine_class(ent->collision/pathmap_cell_world_size()),
        (wc3FinePoint_t){(int)floorf(point.x),(int)floorf(point.y)});
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    wc3RecordObject_t *object=wc3_records_owned(map,index);
    if(!object || memcmp(&object->box,&box,sizeof(box))) {
#ifdef BZ_TESTS
        if(object) {
            wc3FineBox_t old=wc3_records_clip(map,object->box),next=wc3_records_clip(map,box);
            int width=MAX(0,MIN(old.max.x,next.max.x)-MAX(old.min.x,next.min.x));
            int height=MAX(0,MIN(old.max.y,next.max.y)-MAX(old.min.y,next.min.y));
            move_spatial_link_visits+=MAX(0,old.max.x-old.min.x)*MAX(0,old.max.y-old.min.y)-width*height;
        }
#endif
        uint32_t id=object ? map->objects[index] : (map->objects[index]=wc3_records_create(map,index,0));
        wc3_records_update(map,id,box);move_spatial_serial++;
    }
    *geometry=(moveSpatialGeometry_t){ent->s.origin2,ent->movement.fine_pose,ent->movement.pose_world,
        ent->collision,true,ent->movement.pose_valid};
}

uint64_t G_GetMoveSpatialSerial(void) { return move_spatial_serial; }
uint16_t G_GetMoveFineSearchStamp(void) {return move_fine.search_stamp;}
void G_SetMoveFineSearchStamp(uint16_t stamp) {move_fine.search_stamp=stamp;}

wc3RecordObject_t const *G_GetMoveSpatialObject(uint32_t index) {
    assert(index<MAX_ENTITIES);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    return map->objects ? wc3_records_owned(map,index) : NULL;
}

/*14d000 re-emits the saved logical rectangle in object load order. */
bool G_LoadMoveSpatialObject(uint32_t index,wc3FineBox_t const *saved) {
    if(!saved)return false;
    wc3FineBox_t box=*saved;
    int64_t width=(int64_t)box.max.x-box.min.x,height=(int64_t)box.max.y-box.min.y;
    if(index>=globals.num_edicts || !g_edicts[index].inuse ||
        width<=0 || height<=0 || width>4 || height>4 || G_GetMoveSpatialObject(index))return false;
    move_spatial_prepare();wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    uint32_t id=map->objects[index]=wc3_records_create(map,index,0);
    wc3_records_update(map,id,box);move_spatial_serial++;
    move_spatial_geometry[index].valid=false;
    return true;
}

/* Original15d360 rounds a fine object's half-open rectangle into base
 * cells, clears their traversal lanes for admission, then rebuilds the same
 * rectangle and its three parents. Edge terrain is deliberately excluded too. */
static void move_acc_object_rectangle(edict_t const *object, bool clear) {
    if (!object) return;
    wc3RecordObject_t const *record=G_GetMoveSpatialObject(object-g_edicts);
    if(!record)return;
    wc3FineBox_t box=record->box;
    if (box.min.x>=box.max.x || box.min.y>=box.max.y) return;
    move_acc_rebuild_rectangle(box,clear);
}

#ifdef BZ_TESTS
static void (*move_coarse_scope_trace)(void *,unsigned,movePathQuery_t const *);
static void *move_coarse_scope_data;
static void (*move_fine_scope_trace)(void *,unsigned,movePathQuery_t const *);
static void *move_fine_scope_data;
static void (*move_object_scope_trace)(void *,moveScopeTrace_t const *);
static void *move_object_scope_data;
static void (*move_route_trace)(moveRouteTrace_t const *);
void G_TestMoveRouteTrace(void (*trace)(moveRouteTrace_t const *)) { move_route_trace=trace; }
void G_TestMoveObjectScopeTrace(void (*trace)(void *,moveScopeTrace_t const *),void *data) {
    move_object_scope_trace=trace;move_object_scope_data=data;
}
static void move_trace_object_scope(unsigned kind,unsigned stage,movePathQuery_t const *input) {
    if(move_object_scope_trace)move_object_scope_trace(move_object_scope_data,&(moveScopeTrace_t){input,kind,stage});
}
void G_TestMoveFineScopeTrace(void (*trace)(void *,unsigned,movePathQuery_t const *),void *data) {
    move_fine_scope_trace=trace; move_fine_scope_data=data;
}
static void move_trace_fine_scope(unsigned stage,movePathQuery_t const *input) {
    if(move_fine_scope_trace)move_fine_scope_trace(move_fine_scope_data,stage,input);
}
void G_TestMoveCoarseScopeTrace(void (*trace)(void *,unsigned,movePathQuery_t const *),void *data) {
    move_coarse_scope_trace=trace; move_coarse_scope_data=data;
}
static void move_trace_coarse_scope(unsigned stage,movePathQuery_t const *input) {
    if(move_coarse_scope_trace)move_coarse_scope_trace(move_coarse_scope_data,stage,input);
}
#else
#define move_trace_coarse_scope(stage,input) ((void)0)
#define move_trace_fine_scope(stage,input) ((void)0)
#define move_trace_object_scope(kind,stage,input) ((void)0)
#endif

/*166c30 owns one complete synchronous scope. Admission precedes this call;
 * every search result restores self then target before its caller can return.
 * Publication belongs to the fine-object owner, independent of the category
 * used by the selected query. Group paths have no self; borrowing a member
 * pose does not acquire its exclusion. Restoration re-reads the rectangles. */
static uint32_t move_build_acc_route(movePathQuery_t const *input,wc3AccRequest_t const *request,
                                    moveCoarseRequest_t *admission) {
    move_spatial_sync();
    edict_t const *self=input->group_path ? NULL : input->mover;
    move_acc_object_rectangle(self,true);
    move_trace_coarse_scope(0,input);
    move_acc_object_rectangle(input->target,true);
    move_trace_coarse_scope(1,input);
    uint32_t result=wc3_acc_route(&move_acc,request,move_acc_points);
#ifdef BZ_TESTS
    if(move_route_trace)move_route_trace(&(moveRouteTrace_t){.kind=1,.budget=request->budget,
        .pops=move_acc.work.pops,.count=result&0x7fffffffu,.complete=!(result&0x80000000u),.points=move_acc_points});
#endif
    move_trace_coarse_scope(2,input);
    if(admission)S_ChargeMoveCoarseRequest(admission,move_acc.work.pops);
    move_acc_object_rectangle(self,false);
    move_trace_coarse_scope(3,input);
    move_acc_object_rectangle(input->target,false);
    move_trace_coarse_scope(4,input);
    return result;
}

/* Captain9d8c70 -> bridge0594f0 ->059590. This distance query has footprint
 * class0 and its own5000-work budget. It excludes target, auxiliary source,
 * then predicted source, preserving duplicates and restoration order. */
bool G_CaptainMoveReachable(edict_t const *actor,edict_t const *source,edict_t const *target,
                          vec2_t const *from,vec2_t const *to) {
    if (!pathmap.width || !pathmap.height) return false;
    unsigned lane=0;uint8_t mask=S_UnitMoveCoarseMask(actor);
    while (lane<4 && move_acc_masks[lane]!=mask) lane++;
    if (lane==4) gi.error("Captain: unsupported coarse mask %02x",mask);
    move_acc_prepare();move_acc_enable_gates();move_spatial_sync();
    FOR_LOOP(i,4) move_acc.maps[i].classes=move_acc_classes[lane][i];
    edict_t const *objects[]={target ? target : actor,source ? source : actor,source ? source : actor};
    FOR_LOOP(i,3) move_acc_object_rectangle(objects[i],true);
    vec2_t a=move_grid_from_world(from->x,from->y),b=move_grid_from_world(to->x,to->y);
    wc3AccRequest_t request={{wc3_mul(a.x,.5f),wc3_mul(a.y,.5f)},
        {wc3_mul(b.x,.5f),wc3_mul(b.y,.5f)},1,BZ_WC3_GROUP_ACC_WORK};
    wc3FineVector_t endpoint;
    uint32_t result=wc3_acc_query_distance(&move_acc,&request,&endpoint);
    FOR_LOOP(i,3) move_acc_object_rectangle(objects[i],false);
    return result!=UINT32_MAX;
}

/* Observe changed game owners before querying; unrelated world objects never
 * participate in publication or fine-cell lookup. */
static void move_query_objects(moveFineGraph_t *graph, movePathQuery_t const *query, box2_t const *bounds) {
    (void)bounds;
    graph->query=NULL;
    if (!query->units) return;
    move_spatial_sync();
    graph->query=query;
}

/*167bf0/166140/16ee80 hold the pooled self identity, not an edict overlay.
 * A nested depth survives release, and a null self leaves other objects live. */
static wc3RecordObject_t *move_hold_self(movePathQuery_t const *input) {
    wc3RecordObject_t *self=input->units && input->mover ?
        wc3_records_owned(S_GetMoveFineSpatial(),input->mover-g_edicts) : NULL;
    if(self)self->flags++;
    return self;
}
static void move_release_self(wc3RecordObject_t *self) {if(self)self->flags--;}

/*167bf0 owns every visible-waypoint query, including the first selection
 * immediately after a fine search. Fresh and retained routes share the same
 * captured-record lifetime; search suppression has already been released. */
static uint32_t move_select_visible_waypoint(movePathQuery_t const *input,moveFineGraph_t *graph,
        wc3FineSegment_t const *segment,wc3FineRoute_t route) {
    bool counted=graph->counted_scope;
    move_trace_object_scope(MOVE_SCOPE_WAYPOINT,0,input);
    wc3RecordObject_t *self=move_hold_self(input);graph->counted_scope=true;
    move_trace_object_scope(MOVE_SCOPE_WAYPOINT,1,input);
    uint32_t index=wc3_segment_waypoint(segment,route);
    move_release_self(self);graph->counted_scope=counted;
    move_trace_object_scope(MOVE_SCOPE_WAYPOINT,2,input);
    return index;
}

/* Counted consumers read the held record; remaining placement/line adapters
 * keep their local suppression until their owning scopes are integrated. */
static wc3FineObject_t move_fine_cell_object(void *data,wc3RecordObject_t const *object) {
    moveFineGraph_t const *graph=data;
    wc3FineObject_t shape=move_cell_object(NULL,object);
    if(!(object->flags&WC3_RECORD_REGION)) {
        if(!graph->query) {shape.mask=0;return shape;}
        edict_t const *ent=g_edicts+object->owner;
        if(!graph->counted_scope &&
            (ent==graph->query->mover || (graph->suppress_target && ent==graph->query->target)))shape.flags|=1;
    }
    return shape;
}

/*1489a0: one shared raw traversal, including active-state and flag gates. */
static bool move_occupancy_cell(void const *data,wc3FinePoint_t pos) {
    moveFineGraph_t const *graph=data;
    if(!is_valid_point(pos.x,pos.y))return false;
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    if(!map->cells)return true;
    uint32_t target=WC3_RECORD_END,mask=graph->flags;
    if(graph->has_target && graph->target)target=map->objects[graph->target-g_edicts];
    wc3CellQuery_t query={.mode=WC3_CELL_FINE,.mask=mask|mask<<24,.target=target,.endpoint=graph->endpoint,
        .describe=move_fine_cell_object,.data=(void *)graph};
    wc3CellResult_t result=wc3_records_cell(map,pos,0,&query);
    if(result.target_seen && graph->target_hit)*graph->target_hit=true;
    if(!result.value && graph->rejection && result.blocker!=WC3_RECORD_END) {
        wc3RecordObject_t const *object=wc3_records_object(map,result.blocker);
        *graph->rejection=object->flags&WC3_RECORD_REGION ? (wc3FineBox_t){pos,{pos.x+1,pos.y+1}} : object->box;
    }
    return result.value;
}

#ifdef BZ_TESTS
/* Observe the same raw predicate without introducing another self exclusion. */
unsigned G_TestMoveScopeCell(wc3FinePoint_t pos,uint8_t mask,edict_t const *target) {
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    wc3CellQuery_t query={.mode=WC3_CELL_FINE,.mask=mask|mask<<24,
        .target=target ? map->objects[target-g_edicts] : WC3_RECORD_END,.describe=move_cell_object};
    wc3CellResult_t result=wc3_records_cell(map,pos,move_terrain_word(pos),&query);
    return result.value | result.target_seen<<1;
}
#endif

static bool move_cell_uncached(void const *data, wc3FinePoint_t pos) {
    moveFineGraph_t const *graph = data;
    /* Public placement can carry a real zero query after SetUnitPathing(false).
     * Generic routing normalizes its legacy zero before constructing this graph. */
    if (!is_valid_point(pos.x,pos.y) ||
        (move_terrain_word(pos)&((uint32_t)graph->flags<<24))) {
        if (graph->rejection) *graph->rejection = (wc3FineBox_t){pos, {pos.x + 1, pos.y + 1}};
        return false;
    }
    return move_occupancy_cell(data,pos);
}

/* Terrain-only admission can reuse a verdict. Dynamic predicates must execute
 * every time: their cell/object stamps also feed ordered maintenance. */
static void move_begin_cell_query(moveFineGraph_t *graph) {
    if(graph->query || S_GetMoveFineSpatial()->records){graph->cell_epoch=0;return;}
    if(!move_cell_lookup) {
        move_cell_lookup=calloc((size_t)pathmap.width*pathmap.height,sizeof(*move_cell_lookup));
        if(!move_cell_lookup)gi.error("WC3 fine cell query: cannot allocate lookup");
    }
    if(!++move_cell_epoch) {
        memset(move_cell_lookup,0,(size_t)pathmap.width*pathmap.height*sizeof(*move_cell_lookup));
        move_cell_epoch=1;
    }
    graph->cell_epoch=move_cell_epoch;
}

static bool move_cell_ok(void const *data,wc3FinePoint_t pos) {
    moveFineGraph_t const *graph=data;
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    if(graph->query || !graph->cell_epoch || !is_valid_point(pos.x,pos.y) ||
        (map->cells && (map->cells[(uint32_t)pos.y*map->width+pos.x]&WC3_RECORD_END)!=WC3_RECORD_END))return move_cell_uncached(data,pos);
    uint64_t *entry=move_cell_lookup+(uint32_t)pos.y*pathmap.width+pos.x;
    if((*entry>>32)!=graph->cell_epoch) {
        bool hit=false;
        moveFineGraph_t observed=*graph;observed.target_hit=&hit;
        bool allowed=move_cell_uncached(&observed,pos);
        *entry=((uint64_t)graph->cell_epoch<<32) | allowed | ((uint64_t)hit<<1);
    }
    if((*entry&2) && graph->target_hit)*graph->target_hit=true;
    return (*entry&1)!=0;
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
    CM_FinishPathJobs();
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
    CM_BakeStaticMasks();
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
static bool move_place_widget(edict_t *unit, vec2_t point, float radius, uint8_t flags,
                              uint32_t limit, bool match_level, bool counted_scope, vec2_t *out) {
    wc3FineBox_t rejection = {0};
    moveFineGraph_t graph = {.flags = flags, .endpoint = true, .rejection = &rejection};
    float fine[2] = {point.x,point.y};
    if (match_level) graph.level = placement_terrain_level(fine);
    movePathQuery_t objects = {.mover = unit, .units = true};
    move_query_objects(&graph,&objects,NULL);
    graph.counted_scope=counted_scope;
    wc3FinePlacement_t query = {.point = {point.x,point.y}, .limit = limit,
        .footprint = {.cls = wc3_fine_class(radius / pathmap_cell_world_size()),
                      .cell = move_cell_ok, .data = &graph}, .admit = match_level ? placement_admit : NULL};
    float admitted[2];
    if (!wc3_fine_place_indexed(&query, admitted, &rejection)) return false;
    *out = (vec2_t){admitted[0],admitted[1]};
    return true;
}

static bool move_place_unit(edict_t *unit, vec2_t point, uint32_t limit, bool counted_scope, vec2_t *out) {
    uint8_t mask = unit->no_pathing ? 0 : M_UnitStaticPathingFlags(unit);
    return move_place_widget(unit, point, unit->collision, mask, limit, true, counted_scope, out);
}

/* A widget owns its radius, query mask and support policy. Admission owns
 * self suppression, fine geometry and the original32-ring encounter order. */
bool G_FindWidgetPlacementPosition(edict_t *widget, vec2_t const *requested, float radius,
                                   uint8_t mask, bool match_level, vec2_t *out) {
    *out = *requested;
    if (!mask) return true;
    if (!world.map || !world.map->vertices || !pathmap.width || !pathmap.height) return false;
    vec2_t point = move_grid_from_world(requested->x, requested->y), admitted;
    if (!move_place_widget(widget, point, radius, mask, 32, match_level, false, &admitted)) return false;
    *out = move_world_from_grid(admitted.x, admitted.y);
    return true;
}

/* Original public CreateUnit and SetUnitPosition share32-ring point admission.
 * Item drops retain their separately tracked producer. */
bool G_FindUnitPlacementPosition(edict_t *unit, vec2_t const *requested, vec2_t *out) {
    *out = *requested;
    if (!M_UnitStaticPathingFlags(unit)) return true;
    if (!world.map || !world.map->vertices || !pathmap.width || !pathmap.height) {
        fprintf(stderr,"WC3 placement: terrain/pathing data unavailable\n");
        return false;
    }
    vec2_t point = move_grid_from_world(requested->x,requested->y), admitted;
    if (!move_place_unit(unit,point,32,false,&admitted)) return false;
    *out = move_world_from_grid(admitted.x,admitted.y);
    return true;
}

typedef struct {
    moveFineGraph_t graph;
    wc3FineVector_t source;
    uint32_t size;
} movePortalPlacement_t;

/* Original16ecc0 passes filter32, query budget24 and six placement rings.
 * This is independent of the public placement terrain-level callback. */
static bool move_portal_admit(void const *data,float const *point) {
    movePortalPlacement_t const *query=data;
    wc3AccRequest_t req={query->source,{wc3_mul(point[0],.5f),wc3_mul(point[1],.5f)},query->size,24};
    wc3FineVector_t endpoint;
    return wc3_acc_query_distance(&move_acc,&req,&endpoint)<=32;
}

bool G_FindUnitMovePortalPosition(edict_t *unit,vec2_t const *fine,vec2_t *out) {
    if(!unit || !world.map || !world.map->vertices || !pathmap.width || !pathmap.height)return false;
    uint8_t mask=S_UnitMoveCoarseMask(unit);unsigned lane=0;
    while(lane<4 && move_acc_masks[lane]!=mask)lane++;
    if(lane==4)gi.error("Move portal placement: unsupported movement mask %02x",mask);
    move_acc_prepare();move_acc_enable_gates();
    FOR_LOOP(i,4)move_acc.maps[i].classes=move_acc_classes[lane][i];
    uint32_t cls=wc3_fine_class(unit->collision/pathmap_cell_world_size());
    movePortalPlacement_t data={.graph={.flags=unit->no_pathing?0:M_UnitStaticPathingFlags(unit),.endpoint=true},
        .source={wc3_mul(fine->x,.5f),wc3_mul(fine->y,.5f)},.size=1u<<(cls>>1)};
    movePathQuery_t objects={.mover=unit,.units=true};move_query_objects(&data.graph,&objects,NULL);
    wc3FinePlacement_t query={.point={fine->x,fine->y},.limit=6,
        .footprint={.cls=cls,.cell=move_cell_ok,.data=&data},.admit=move_portal_admit};
    /*16ec00 captures self once;16ee00's distance callback runs while held.
     * Portal placement has no outer gameplay callback. Release before commit. */
    move_trace_object_scope(MOVE_SCOPE_PORTAL,0,&objects);
    wc3RecordObject_t *self=move_hold_self(&objects);data.graph.counted_scope=true;
    move_trace_object_scope(MOVE_SCOPE_PORTAL,1,&objects);
    float admitted[2];bool placed=wc3_fine_place(&query,admitted);
    move_release_self(self);
    move_trace_object_scope(MOVE_SCOPE_PORTAL,2,&objects);
    if(!placed)return false;
    *out=(vec2_t){admitted[0],admitted[1]};return true;
}

/* Original170080 recovers the predicted fine point with five attempts. No
 * admitted point means the stopped unit remains embedded; queryzero stays put. */
bool G_FindUnitMoveRecoveryPosition(edict_t *unit, vec2_t const *fine, vec2_t *out) {
    /* Recovery is inactive before a movement world is loaded or after it is released. */
    if (M_UnitMoveDisabled(unit) || !world.map || !world.map->vertices ||
        !pathmap.width || !pathmap.height) return false;
    movePathQuery_t query = {.geometry.from=&unit->s.origin2,.fine=fine};
    vec2_t point=move_query_source(&query), admitted;
    /* Move owns the captured record hold through the subsequent pose commit. */
    if (!move_place_unit(unit,point,5,true,&admitted) ||
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
        .flags = params->blocked_flags };
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
    move_trace_object_scope(MOVE_SCOPE_ENDPOINT,0,input);
    wc3RecordObject_t *self=move_hold_self(input);graph.counted_scope=true;
    move_trace_object_scope(MOVE_SCOPE_ENDPOINT,1,input);
    bool admitted=move_foot_ok(&graph,(wc3FinePoint_t){(int)floorf(fine[0]),(int)floorf(fine[1])});
    move_release_self(self);
    move_trace_object_scope(MOVE_SCOPE_ENDPOINT,2,input);
    return admitted;
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
    if (query.length > 1.f) move_query_objects(&graph,input,NULL);
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

/* Includes every cell in the retail entering strips, including predecessor
 * sides of diagonals. A false positive only selects the original ordered walk. */
static bool move_empty_edge_neighborhood(moveFineGraph_t const *graph,wc3FinePoint_t pos) {
    int minx=MAX(0,pos.x-graph->size/2-1),maxx=MIN((int)move_spatial_width,pos.x+graph->size-graph->size/2+1);
    int miny=MAX(0,pos.y-graph->size/2-1),maxy=MIN((int)move_spatial_height,pos.y+graph->size-graph->size/2+1);
    if(minx>=maxx || miny>=maxy)return true;
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    unsigned first=(unsigned)minx>>6,last=(unsigned)(maxx-1)>>6;
    uint64_t low=UINT64_MAX<<(minx&63),high=UINT64_MAX>>(63-((maxx-1)&63));
    for(int y=miny;y<maxy;y++) {
        uint64_t const *row=map->occupied+(uint32_t)y*map->occupied_stride;
        if(first==last ? (row[first]&low&high) : ((row[first]&low)||(row[last]&high)))return false;
    }
    return true;
}

/* Cache static admission only in neighborhoods with no raw records. Nearby
 * objects, retained removals and metadata keep every native observation. */
static uint8_t move_fine_edges(void const *data, wc3FinePoint_t pos) {
    moveFineGraph_t const *graph=data;
    wc3FineSegment_t query={.cls=(unsigned)graph->size-1,.cell=move_cell_ok,.data=graph};
    unsigned lane=0;while(lane<4 && move_acc_masks[lane]!=graph->flags)lane++;
    if(lane==4 || !is_valid_point(pos.x,pos.y) ||
        ((graph->query || graph->has_target) && !move_empty_edge_neighborhood(graph,pos)))
        return wc3_fine_cell_edges(&query,pos);
    if(!move_static_edges[lane]) {
        move_static_edges[lane]=calloc((size_t)pathmap.width*pathmap.height,sizeof(uint64_t));
        if(!move_static_edges[lane])gi.error("WC3 fine edges: cannot allocate lane %u",lane);
    }
    uint64_t *entry=move_static_edges[lane]+(uint32_t)pos.y*pathmap.width+pos.x;
    if((*entry>>36)!=move_edge_epoch)*entry=(uint64_t)move_edge_epoch<<36;
    uint64_t valid=UINT64_C(1)<<(32+query.cls);
    if(!(*entry&valid)) {
        moveFineGraph_t terrain={.size=graph->size,.flags=graph->flags};query.data=&terrain;
        *entry|=valid | ((uint64_t)wc3_fine_cell_edges(&query,pos)<<(query.cls*8));
    }
    return (uint8_t)(*entry>>(query.cls*8));
}

static void move_publish_fine_node(void const *data,wc3FinePoint_t pos,uint16_t stamp,uint16_t node) {
    (void)data;
    wc3_records_node(S_GetMoveFineSpatial(),pos,stamp,node);
}

#ifdef BZ_TESTS
uint8_t G_TestMoveFineEdges(movePathQuery_t const *input,point2_t pos,bool cached,bool *hit) {
    moveFineGraph_t graph=move_foot_shape(&input->geometry);move_query_objects(&graph,input,NULL);
    graph.suppress_target=true;
    if(input->target) {
        graph.has_target=true;graph.target=input->target;graph.target_hit=hit;
    }
    if(cached)move_begin_cell_query(&graph);
    wc3FinePoint_t point={pos.x,pos.y};
    wc3FineSegment_t query={.cls=(unsigned)graph.size-1,.cell=move_cell_ok,.data=&graph};
    return cached ? move_fine_edges(&graph,point) : wc3_fine_cell_edges(&query,point);
}
#endif

typedef struct { movePathQuery_t const *query; edict_t **items; uint32_t count; } moveBlockerQuery_t;

static wc3FineObject_t move_collector_object(void *data,wc3RecordObject_t const *object) {
    return move_cell_object(data,object);
}
static void move_collector_emit(void *data,uint32_t id) {
    moveBlockerQuery_t *scan=data;
    if(scan->count==32)return; /* Capacity limits append, never traversal. */
    edict_t *ent=NULL;
    if(id!=WC3_RECORD_END) {
        wc3RecordObject_t const *object=wc3_records_object(S_GetMoveFineSpatial(),id);
        if(!(object->flags&WC3_RECORD_REGION) && !G_IsItem(g_edicts+object->owner))ent=g_edicts+object->owner;
    }
    scan->items[scan->count++]=ent;
}

/*148ad0 shares active/suppression gates, includes moving objects and emits
 * NULL for terrain and non-mover payloads. Stamp the suffix after32 outputs. */
static bool move_collect_blocker_cell(void const *data,wc3FinePoint_t pos) {
    moveBlockerQuery_t *scan=(moveBlockerQuery_t *)data;
    uint32_t mask=scan->query->geometry.blocked_flags;
    wc3CellQuery_t query={.mode=WC3_CELL_COLLECT,.mask=mask|mask<<24,.target=WC3_RECORD_END,
        .describe=move_collector_object,.emit=move_collector_emit,.data=scan};
    wc3_records_cell(S_GetMoveFineSpatial(),pos,is_valid_point(pos.x,pos.y) ? move_terrain_word(pos) : 0,&query);
    return true;
}

/* Original166140 normalizes the native next step and collects every entering
 * cell. Returning true from the callback keeps scanning after a rejection. */
static uint32_t move_step_blockers(movePathQuery_t const *input,float const fine_goal[2],
                                   edict_t **out,wc3YieldDecision_t *decision) {
    if (!input || !input->units || !out || !input->geometry.target ||
        !pathmap.width || !pathmap.height || (input->mover && (input->mover->aiflags&AI_FLYING))) {
        if(decision)*decision=S_ResolveMoveBlockers((edict_t *)input->mover,NULL,0);
        return 0;
    }
    move_spatial_sync();
    vec2_t source=move_query_source(input), goal=fine_goal ? (vec2_t){fine_goal[0],fine_goal[1]} :
        move_grid_from_world(input->geometry.target->x,input->geometry.target->y);
    moveBlockerQuery_t scan={input,out,0};
    wc3FineSegment_t query={.start={source.x,source.y},.direction={wc3_sub(goal.x,source.x),wc3_sub(goal.y,source.y)},
        .cls=wc3_fine_class(input->geometry.radius/pathmap_cell_world_size()),.cell=move_collect_blocker_cell,.data=&scan};
    wc3_segment_normalize(query.direction);
    wc3FinePoint_t current={(int)floorf(source.x),(int)floorf(source.y)}, next=wc3_segment_point(&query,1.f);
    unsigned code=(next.x<current.x?8u:next.x>current.x?2u:0u)|(next.y<current.y?1u:next.y>current.y?4u:0u);
    move_trace_object_scope(MOVE_SCOPE_BLOCKERS,0,input);
    wc3RecordObject_t *self=move_hold_self(input);
    move_trace_object_scope(MOVE_SCOPE_BLOCKERS,1,input);
    wc3_segment_foot(&query,next,code);
    /*168360 may return early when the requester yields. The captured self
     * still belongs to166140 until that complete resolver returns. */
    if(decision)*decision=S_ResolveMoveBlockers((edict_t *)input->mover,out,scan.count);
    move_release_self(self);
    move_trace_object_scope(MOVE_SCOPE_BLOCKERS,2,input);
    return scan.count;
}

/* The collection adapter exposes tokens for independent query consumers.
 * Gameplay uses the combined operation below, retaining the same hold. */
uint32_t G_CollectUnitMoveStepBlockers(movePathQuery_t const *input,float const fine_goal[2],edict_t **out) {
    return move_step_blockers(input,fine_goal,out,NULL);
}

/*166140 owns collection and resolution as one synchronous operation. */
uint32_t G_ResolveUnitMoveStepBlockers(movePathQuery_t const *input,float const fine_goal[2],
                                     wc3YieldDecision_t *decision) {
    if(!decision)return 0;
    *decision=WC3_YIELD_SKIP;
    if(!input || !input->mover)return 0;
    edict_t *items[32];
    return move_step_blockers(input,fine_goal,items,decision);
}

/* SetUnitPathing changes the member's fine query, not its authored hierarchy
 * lane or the acquired coarse chain. Fresh local refinement uses query0. */
static uint8_t move_adaptive_mask(movePathQuery_t const *input) {
    if(input->coarse_mask)return input->coarse_mask;
    if(input->mover && input->mover->no_pathing)return S_UnitMoveCoarseMask(input->mover);
    return input->geometry.blocked_flags ? input->geometry.blocked_flags : CM_PATHING_UNWALKABLE;
}

/* Native165f10 checks newly admitted coarse routes before any fine refill.
 * A portal can place the mover inside the next entrance in the same chain. */
static bool move_adaptive_progress(movePathQuery_t const *input,vec2_t source,
        moveFineRoute_t *route,uint32_t *status) {
    if (!route->adaptive_index) return false;
    vec2_t point=route->adaptive_points[route->adaptive_index];
    if (!wc3_acc_in_range((wc3FineVector_t){wc3_mul(source.x,.5f),wc3_mul(source.y,.5f)},
        (wc3FineVector_t){point.x,point.y})) return false;
    bool warped=false;
    if (!G_AdvanceUnitMoveAdaptiveDestination(input->mover,route,&warped)) {
        if (input->mover) input->mover->movement.wait_delay=MAX(input->mover->movement.wait_delay,20u);
        *status=2;
    } else if (warped) *status=1;
    return true;
}

/* Ordinary owned paths enable adaptive search at activation, including nearby orders. */
static void move_acc_enable_gates(void) {
    S_WaygateBuildEdges(move_acc_gates);
    move_acc.markers=move_acc_markers;move_acc.gates=move_acc_gates;move_acc.warp=true;
}

/* Counts and indices can be erased independently of backing storage. Shorter
 * refills do not allocate, and empty construction does not reserve capacity. */
void G_ReserveMoveRouteBuffer(vec2_t **points,uint32_t *capacity,uint32_t count) {
    if (count <= *capacity) return;
    uint32_t grown = (count + BZ_WC3_ROUTE_POINT_GROW - 1) / BZ_WC3_ROUTE_POINT_GROW * BZ_WC3_ROUTE_POINT_GROW;
    vec2_t *data = realloc(*points,(size_t)grown * sizeof(*data));
    if (!data) gi.error("WC3 route: cannot retain %u points",grown);
    *points=data; *capacity=grown;
}

static bool move_adaptive_waypoint(moveAdaptiveQuery_t const *query, vec2_t *out) {
    movePathQuery_t const *input = query->input;
    wc3FineVector_t source = query->source, target = query->target;
    if (!input->units || !input->mover) return false;
    uint8_t mask=move_adaptive_mask(input);
    unsigned lane = 0;
    while (lane < 4 && move_acc_masks[lane] != mask) lane++;
    if (lane == 4) {
        fprintf(stderr,"WC3 adaptive routing: unsupported movement mask %02x\n",input->geometry.blocked_flags);
        return false;
    }
    moveFineRoute_t *route=query->route;
    bool retained=route && route->adaptive_points && route->adaptive_count &&
        route->adaptive_index<route->adaptive_count && route->adaptive_mask==mask &&
        route->adaptive_revision==move_map_revision && route->adaptive_radius==input->geometry.radius &&
        route->adaptive_goal.x==target.x && route->adaptive_goal.y==target.y;
    wc3FineVector_t point;
    if (retained) {
        vec2_t selected=route->adaptive_points[route->adaptive_index];
        point=route->adaptive_index ? (wc3FineVector_t){wc3_mul(selected.x,2),wc3_mul(selected.y,2)} : target;
    } else {
        /* Original166c30 leaves the fine FIFO before acquiring a replacement
         * adaptive route. A later fine refill is a new tail request. */
        if(route && !S_AdmitMoveCoarseRequest(input->mover,&route->adaptive_admission,2)) {
            if(query->status)*query->status=2;
            return false;
        }
        if(!route)S_CancelUnitMoveFineRequest(input->mover);
        move_acc_prepare();
        FOR_LOOP(level,4) move_acc.maps[level].classes = move_acc_classes[lane][level];
            move_acc_enable_gates();
        wc3AccRequest_t req = {{wc3_mul(source.x,.5f),wc3_mul(source.y,.5f)},
            {wc3_mul(target.x,.5f),wc3_mul(target.y,.5f)},
            input->geometry.radius >= pathmap_cell_world_size() ? 2 : 1,BZ_WC3_UNIT_ACC_WORK};
        uint32_t result=move_build_acc_route(input,&req,route?&route->adaptive_admission:NULL),
            count=result&0x7fffffffu;
        if (!count) return false;
        wc3AccSelection_t selected=wc3_acc_select((wc3FineRoute_t){move_acc_points,count-1},false);
        point=selected.index ? (wc3FineVector_t){wc3_mul(move_acc_points[selected.index].x,2),wc3_mul(move_acc_points[selected.index].y,2)} : target;
        if (route) {
            G_ReserveMoveRouteBuffer(&route->adaptive_points,&route->adaptive_capacity,count);
            vec2_t *points=route->adaptive_points;
            route->adaptive_points=points; route->adaptive_count=count; route->adaptive_index=selected.index;
            route->adaptive_goal=(vec2_t){target.x,target.y}; route->adaptive_radius=input->geometry.radius;
            route->adaptive_revision=move_map_revision; route->adaptive_mask=mask;
            route->warp_markers=move_acc.warps!=0;
            memcpy(points,move_acc_points,count*sizeof(*points));
        }
    }
    if (!retained && route && query->status &&
        move_adaptive_progress(input,(vec2_t){source.x,source.y},route,query->status)) {
        if (*query->status) return false;
        point=route->adaptive_index ?
            (wc3FineVector_t){wc3_mul(route->adaptive_points[route->adaptive_index].x,2),wc3_mul(route->adaptive_points[route->adaptive_index].y,2)} : target;
    }
    /* 166c30 replaces the coarse table independently. It does not expire a
     * valid fine index;165ae0 subsequently consumes that cache through167ce0.
     * Actual coarse progress above owns fine invalidation. */
    if(query->retain_fine && route && route->points && route->index<route->count)
        return G_AdvanceUnitMoveFineRouteStatus(input,route,out,query->status);
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
    unsigned lane=0; uint32_t mask=S_UnitMoveCoarseMask(unit);
    while (lane<4 && move_acc_masks[lane]!=mask) lane++;
    if (lane==4) gi.error("Move formation: unsupported movement mask %02x",mask);
    move_acc_prepare();
    FOR_LOOP(i,4) move_acc.maps[i].classes=move_acc_classes[lane][i];
    move_acc_enable_gates();
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
bool G_UnitMoveGroupDestinationStatus(movePathQuery_t const *input, moveFineRoute_t *route,
                                     vec2_t *fine, bool *rebuilt) {
    if (rebuilt) *rebuilt=false;
    if (!input || !route || !fine || !input->geometry.target || !pathmap.width || !pathmap.height) return false;
    /* Native16ce10 updates target scheduling even when retaining a route. */
    S_SetMoveCoarseTarget(&route->group_admission,input->target!=NULL);
    uint8_t mask=move_adaptive_mask(input);
    moveGridGeometry_t const *geometry = move_geometry();
    bool retained = route->group_points && route->group_count && route->group_index < route->group_count &&
        route->group_revision == move_map_revision && route->group_mask == mask;
    vec2_t goal;
    /* A route consumes a task destination, not a new public order every tick.
     * Reuse only the exact request under the same world transform. Changed
     * requests still clip before comparing, retaining equivalent destinations. */
    if (retained && route->group_geometry == geometry->revision &&
        !memcmp(&route->group_request, input->geometry.target, sizeof(vec2_t))) {
        goal = route->group_goal;
    } else {
        box2_t bounds = geometry->bounds;
        float cell = pathmap_cell_world_size();
        /*16cd30 ->16ce10 ->168b80 copies sampled target coordinates.
         * Public point admission owns its separate playable-map inset. */
        vec2_t clipped = input->target ? *input->geometry.target :
            (vec2_t){wc3_point_order_coordinate(input->geometry.target->x, bounds.min.x, bounds.max.x, cell),
                wc3_point_order_coordinate(input->geometry.target->y, bounds.min.y, bounds.max.y, cell)};
        goal = move_grid_from_world(clipped.x, clipped.y);
#ifdef BZ_TESTS
        move_group_goal_conversions++;
#endif
    }
    /* Original16e430 tests path88.200000 independently of movement class.
     * Creation enables nonstructures; a later flight rebind disables it. */
    if (input->mover && input->mover->movement.adaptive_disabled) {
        route->group_count=route->group_index=0;
        *fine=goal; return true;
    }
    /* Original16ce10 resamples16c940 and writes path+b4 only when it admits a
     * route. A surviving cached route keeps its footprint after a peer leaves;
     * the current live maximum is used when the destination/map/mask changes. */
    retained = retained && route->group_goal.x == goal.x && route->group_goal.y == goal.y;
    if (!retained) {
        unsigned lane=0;
        while (lane<4 && move_acc_masks[lane]!=mask) lane++;
        if (lane==4) {
            fprintf(stderr,"WC3 group routing: unsupported movement mask %02x\n",input->geometry.blocked_flags);
            return false;
        }
        /* Native168b80 publishes the destination before admission. A denied
         * request retains it and an invalid route for refresh/save/retry. */
        route->group_goal=goal;route->group_request=*input->geometry.target;
        route->group_geometry=geometry->revision;
        route->group_count=0;route->group_index=UINT32_MAX;
        if(input->mover && !S_AdmitMoveCoarseRequest(input->mover,&route->group_admission,route->group_admission.policy))return false;
        vec2_t source=move_query_source(input);
        move_acc_prepare();
        FOR_LOOP(i,4) move_acc.maps[i].classes=move_acc_classes[lane][i];
        move_acc_enable_gates();
        move_acc.warp=!input->no_warp;
        wc3AccRequest_t req={{wc3_mul(source.x,.5f),wc3_mul(source.y,.5f)},
            {wc3_mul(goal.x,.5f),wc3_mul(goal.y,.5f)},input->geometry.radius>=pathmap_cell_world_size()?2:1,BZ_WC3_GROUP_ACC_WORK};
        uint32_t count=move_build_acc_route(input,&req,input->mover?&route->group_admission:NULL)&0x7fffffffu;
        if (!count) return false;
        wc3AccSelection_t selected={wc3_acc_group_advance((wc3FineRoute_t){move_acc_points,count-1}),false};
        G_ReserveMoveRouteBuffer(&route->group_points,&route->group_capacity,count);
        vec2_t *points=route->group_points;
        route->group_points=points; route->group_count=count; route->group_index=selected.index;
        route->group_goal=goal; route->group_radius=input->geometry.radius; route->group_revision=move_map_revision;
        route->group_mask=mask;
        route->warp_markers=move_acc.warps!=0;
        route->adaptive_count=route->count=0;
        memcpy(points,move_acc_points,count*sizeof(*points));
        if (rebuilt) *rebuilt=true;
    }
    route->group_request = *input->geometry.target;
    route->group_geometry = geometry->revision;
    vec2_t point=route->group_points[route->group_index];
    *fine=route->group_index ? (vec2_t){wc3_mul(point.x,2),wc3_mul(point.y,2)} : route->group_goal;
    return true;
}

bool G_UnitMoveGroupDestination(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *fine) {
    return G_UnitMoveGroupDestinationStatus(input,route,fine,NULL);
}

/* Singleton regroup succeeds after the member's actual arrival/zero commit.
 * The next member update rebuilds its path to the new group destination. */
bool G_AdvanceUnitMoveGroupDestination(moveFineRoute_t *route) {
    if (!route || !route->group_count || !route->group_index || route->group_index>=route->group_count) return false;
    wc3AccSelection_t selected={wc3_acc_group_advance((wc3FineRoute_t){route->group_points,route->group_index}),false};
    route->group_index=selected.index;
    route->count=route->adaptive_count=0;
    route->index=route->adaptive_index=UINT32_MAX;
    return true;
}

/* Native167070 consumes an intermediate coarse leg even when a partial
 * fine search returns only the source. Retry belongs to adaptive index0. */
static bool move_gate_active(void const *context,uint8_t id) {
    (void)context;return S_WaygateEdgeIsActive(id);
}
static bool move_gate_place(void *context,wc3FineVector_t point) {
    if(!context){fprintf(stderr,"Move portal consumer: missing mover for cached exit\n");return false;}
    vec2_t fine={point.x,point.y};return S_MoveThroughPortal(context,&fine);
}

bool G_AdvanceUnitMoveAdaptiveDestination(edict_t *unit,moveFineRoute_t *route,bool *warped) {
    if (!route || !route->adaptive_points || !route->adaptive_count ||
        !route->adaptive_index || route->adaptive_index>=route->adaptive_count) return false;
    wc3FineRoute_t path={route->adaptive_points,route->adaptive_index};
    if(!wc3_acc_advance(&path,true,move_gate_active,move_gate_place,unit,warped))return false;
    route->adaptive_index=path.index;
    /* Original165f10/167070 invalidate the fine index, retaining its table.
     * Admission on a later visit owns replacement and charged search work. */
    route->index=UINT32_MAX;
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
    if (input->fine) source=*params->from; /* Published owner pose reaches fine setup unchanged. */
    else if (!G_ClosestMovePathPoint(params, &source)) return false;
    if (input->fine_target) target=*params->target;
    else if (!G_ClosestMovePathPoint(&dest,&target)) return false;
    vec2_t a = source.x==params->from->x && source.y==params->from->y ?
        move_query_source(input) : move_grid_from_world(source.x,source.y);
    vec2_t b = input->fine_target ? *input->fine_target : move_grid_from_world(target.x,target.y);
    wc3FinePoint_t start = { (int)floorf(a.x), (int)floorf(a.y) };
    wc3FinePoint_t goal = { (int)floorf(b.x), (int)floorf(b.y) };

    moveFineGraph_t graph = move_foot_shape(params);
    move_query_objects(&graph, input, NULL);
    /* Original166e90 suppresses self and target only around route building.
     * Later167bf0/166140 consumers suppress self, retaining target occupancy. */
    graph.suppress_target=true;
    bool target_hit = false;
    edict_t const *object = input->target;
    /* A suppressed target still terminates fine expansion at its region,
     * independently of the mover's collision lane (native166e90/1489a0).
     * Native category2 captains have radius0 but retain one fine cell. */
    if (input->units && object && object->inuse &&
        move_has_spatial_record(object)) {
        graph.has_target = true;
        graph.target = object;
        graph.target_hit = &target_hit;
    }
    bool initialized=(uint32_t)start.x<pathmap.width && (uint32_t)start.y<pathmap.height &&
        (start.x!=goal.x || start.y!=goal.y);
    if (!initialized && move_fine.query_initialized) {
        graph.size=move_fine_profile.size; graph.flags=move_fine_profile.flags;
        graph.target=move_fine_profile.target;
        graph.has_target=move_fine_profile.has_target && graph.target && graph.target->inuse &&
            graph.target->spawn_time==move_fine_profile.target_spawn;
        graph.target_hit=graph.has_target ? &target_hit : NULL;
    }
    wc3FineRequest_t req = { .start = {start.x, start.y}, .goal = {goal.x, goal.y},
        .width = pathmap.width, .height = pathmap.height,
        .budget = input->mover ? BZ_WC3_UNIT_FINE_WORK : BZ_WC3_FINE_WORK,
        .edges = move_fine_edges, .data = &graph, .target_hit = &target_hit,
        .publish_node=move_publish_fine_node };
    if (input->units && input->mover && !S_AdmitUnitMoveFineRequest((edict_t *)input->mover)) return false;
    move_spatial_prepare();
    if (initialized) {
        move_fine_profile.size=graph.size; move_fine_profile.flags=graph.flags;
        move_fine_profile.target=graph.target;
        move_fine_profile.target_spawn=graph.target ? graph.target->spawn_time : 0;
        move_fine_profile.has_target=graph.has_target;
    }
    /*166e90 captures both records before search. An alias is incremented twice,
     * and an already-held exclusion survives the reverse-order restoration.
     * Unlike the old boolean overlay, this also covers hierarchy participants. */
    wc3SpatialRecords_t *spatial=S_GetMoveFineSpatial();
    wc3RecordObject_t *self=input->units && input->mover ? wc3_records_owned(spatial,input->mover-g_edicts) : NULL;
    wc3RecordObject_t *target_object=input->units && input->target ? wc3_records_owned(spatial,input->target-g_edicts) : NULL;
    move_trace_fine_scope(0,input);
    if(self)self->flags++;
    move_trace_fine_scope(1,input);
    if(target_object)target_object->flags++;
    move_trace_fine_scope(2,input);
    graph.counted_scope=true;
    move_begin_cell_query(&graph);
    bool complete;
    move_trace_fine_scope(3,input);
    uint32_t count=wc3_fine_build_route(&move_fine,&req,(wc3FineVector_t){a.x,a.y},
        (wc3FineVector_t){b.x,b.y},move_fine_points,BZ_WC3_FINE_NODES,&complete);
#ifdef BZ_TESTS
    if(move_route_trace)move_route_trace(&(moveRouteTrace_t){.budget=req.budget,
        .pops=move_fine.pops,.count=count,.complete=complete,.points=move_fine_points});
#endif
    if (input->units && input->mover) S_ChargeUnitMoveFineRequest((edict_t *)input->mover,move_fine.pops);
    if(target_object)target_object->flags--;
    move_trace_fine_scope(4,input);
    if(self)self->flags--;
    move_trace_fine_scope(5,input);
    graph.counted_scope=false;
    if (!count || (!complete && !input->units)) return false;
    /* Native166e90 publishes count1 as an admitted route. A caller that
     * derives its fine goal here has the same route contract as a member. */
    if (curve) {
        G_ReserveMoveRouteBuffer(&curve->points,&curve->capacity,count);
        vec2_t *points=curve->points;
        /* Original166e90 starts at the destination if expansion observed no
         * obstruction; seeing a blocked cell selects the next parent point. */
        curve->points = points; curve->count = count; curve->partial=move_fine_points[0].x!=b.x || move_fine_points[0].y!=b.y;
        curve->index = move_fine.observed_obstruction && count>1 ? count-2 : 0;
        curve->mask = params->blocked_flags;
        memcpy(curve->points,move_fine_points,count*sizeof(*points));
        *out = move_world_from_grid(curve->points[curve->index].x,curve->points[curve->index].y);
        return true;
    }
    graph.suppress_target=false;
    graph.cell_epoch=0; /* Cached search cells were evaluated under suppression. */
    wc3FineSegment_t query = { .start = {a.x, a.y},
        .cls = (unsigned)graph.size - 1, .cell = move_cell_ok, .data = &graph };
    uint32_t chosen = move_select_visible_waypoint(input,&graph,&query,
        (wc3FineRoute_t){move_fine_points,count-1});
    wc3FineVector_t point = move_fine_points[chosen];
    /* Preserve admitted world words when the selected point is the exact goal. */
    *out = complete && point.x == b.x && point.y == b.y && chosen == 0
        ? target : move_world_from_grid(point.x, point.y);
    return true;
}

/* A retained coarse chain remains authoritative when a local fine buffer is exhausted. */
static bool move_find_route(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *out,uint32_t *status) {
    if (!input || !input->geometry.from || !input->geometry.target || !out || !pathmap.width || !pathmap.height) return false;
    vec2_t source,target;
    pathAccelParams_t dest=input->geometry; dest.from=dest.target;
    if (input->fine) source=*input->geometry.from;
    else if (!G_ClosestMovePathPoint(&input->geometry,&source)) return false;
    if (input->fine_target) target=*input->geometry.target;
    else if (!G_ClosestMovePathPoint(&dest,&target)) return false;
    vec2_t a=source.x==input->geometry.from->x && source.y==input->geometry.from->y ?
        move_query_source(input) : move_grid_from_world(source.x,source.y);
    vec2_t b=input->fine_target && target.x==input->geometry.target->x && target.y==input->geometry.target->y ?
        *input->fine_target : move_grid_from_world(target.x,target.y);
    if (route && route->adaptive_count && (route->adaptive_revision!=move_map_revision ||
        route->adaptive_mask!=move_adaptive_mask(input) || route->adaptive_radius!=input->geometry.radius ||
        route->adaptive_goal.x!=b.x || route->adaptive_goal.y!=b.y))
        route->adaptive_count=route->adaptive_index=0;
    /* Original165b60 uses one adjusted destination when adaptive routing is
     * disabled. It still performs the ordinary budget700 fine search with
     * its own terrain mask, including partial results. */
    if (input->mover && input->mover->movement.adaptive_disabled) {
        /* Native165b60 still owns a coarse cache. Only an exhausted index
         * replaces it with the exact adjusted destination; fine admission
         * can fail after this publication without erasing either buffer. */
        if (route && route->adaptive_index>=route->adaptive_count) {
            G_ReserveMoveRouteBuffer(&route->adaptive_points,&route->adaptive_capacity,1);
            route->adaptive_points[0]=(vec2_t){wc3_mul(b.x,.5f),wc3_mul(b.y,.5f)};
            route->adaptive_count=1;route->adaptive_index=0;
            route->adaptive_goal=b;route->adaptive_radius=input->geometry.radius;
            route->adaptive_revision=move_map_revision;route->adaptive_mask=move_adaptive_mask(input);
        }
        return G_BuildUnitMoveLocalRoute(input,route,out);
    }
    int dx=abs((int)floorf(a.x)-(int)floorf(b.x)),dy=abs((int)floorf(a.y)-(int)floorf(b.y));
    if ((input->units && input->mover) || (route && route->adaptive_count) ||
        dx>PATH_ACCEL_MAX_DISTANCE || dy>PATH_ACCEL_MAX_DISTANCE)
        return move_adaptive_waypoint(&(moveAdaptiveQuery_t){input,{a.x,a.y},{b.x,b.y},route,status,false},out);
    return G_BuildUnitMoveLocalRoute(input,route,out);
}

/* A loaded world has a new process-local map lifetime. Retained routes belong
 * to its saved fine masks and independently saved hierarchy publication. */
void G_RebindSavedMoveRoutes(void) {
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups)) {
        moveFineRoute_t *route=&level.move_groups[i]->route;
        if (route->adaptive_count) route->adaptive_revision=move_map_revision;
        if (route->group_count) route->group_revision=move_map_revision;
    }
    FILTER_EDICTS(ent,ent->inuse) {
        moveFineRoute_t *route=&ent->movement.fine_route;
        if (route->adaptive_count) route->adaptive_revision=move_map_revision;
        if (route->group_count) route->group_revision=move_map_revision;
    }
}

bool G_FindUnitMovePathWaypoint(movePathQuery_t const *input, vec2_t *out) {
    return move_find_route(input,NULL,out,NULL);
}

bool G_BuildUnitMoveFineRoute(movePathQuery_t const *input, moveFineRoute_t *route, vec2_t *out) {
    return route && move_find_route(input,route,out,NULL);
}

bool G_BuildUnitMoveFineRouteStatus(movePathQuery_t const *input,moveFineRoute_t *route,vec2_t *out,uint32_t *status) {
    *status=0;
    return route && move_find_route(input,route,out,status);
}

/* Original16fbd0 subtracts the predicted fine source from the returned native
 * waypoint before vector-heading calculation; world publication is downstream. */
vec2_t G_MoveFineRouteDirection(movePathQuery_t const *input, moveFineRoute_t const *route) {
    assert(input && route && route->points && route->index<route->count);
    vec2_t source=move_query_source(input), target=route->points[route->index];
    return (vec2_t){wc3_sub(target.x,source.x),wc3_sub(target.y,source.y)};
}

/* Original167070 retains the current point until .49 cells, then165e60 skips visible successors. */
bool G_AdvanceUnitMoveFineRoute(movePathQuery_t const *input,moveFineRoute_t *route,vec2_t *out) {
    uint32_t status;return G_AdvanceUnitMoveFineRouteStatus(input,route,out,&status);
}

bool G_AdvanceUnitMoveFineRouteStatus(movePathQuery_t const *input,moveFineRoute_t *route,vec2_t *out,uint32_t *status) {
    *status=0;
    if (!input || !input->geometry.from || !out || !route) return false;
    vec2_t source = move_query_source(input);
    if (route->adaptive_count) {
        if (!input->geometry.target) return false;
        vec2_t goal=input->fine_target ? *input->fine_target : move_grid_from_world(input->geometry.target->x,input->geometry.target->y);
        if (!route->adaptive_points || route->adaptive_count>BZ_WC3_ACC_ROUTE_NODES ||
            route->adaptive_revision!=move_map_revision || route->adaptive_radius!=input->geometry.radius ||
            route->adaptive_mask!=move_adaptive_mask(input) ||
            route->adaptive_goal.x!=goal.x || route->adaptive_goal.y!=goal.y) return false;
        if(route->adaptive_index>=route->adaptive_count) {
            if(input->mover && input->mover->movement.adaptive_disabled)return false;
            return move_adaptive_waypoint(&(moveAdaptiveQuery_t){input,{source.x,source.y},{goal.x,goal.y},route,status,true},out);
        }
        if (move_adaptive_progress(input,source,route,status)) {
            if (*status) return false;
            return move_adaptive_waypoint(&(moveAdaptiveQuery_t){input,{source.x,source.y},{goal.x,goal.y},route,status,false},out);
        }
    }

    /* Native167ce0 validates unsigned index<count, including a one-point
     * cache. Its distance gate, rather than length, owns endpoint consumption. */
    if (!route->points || !route->count || route->count > BZ_WC3_FINE_NODES ||
        route->index >= route->count) return false;
    vec2_t point = route->points[route->index];
    float dx = wc3_sub(point.x,source.x), dy = wc3_sub(point.y,source.y), range = wc3_float(0x3efae148);

    if (wc3_add(wc3_mul(dx,dx),wc3_mul(dy,dy)) <= wc3_mul(range,range)) {
        if (!route->index) return false;
        moveFineGraph_t graph = move_foot_shape(&input->geometry); move_query_objects(&graph,input,NULL);
        wc3FineSegment_t segment = {.start={source.x,source.y},.cls=(unsigned)graph.size-1,.cell=move_cell_ok,.data=&graph};
        route->index = move_select_visible_waypoint(input,&graph,&segment,
            (wc3FineRoute_t){route->points,route->index});
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
