#include "../g_local.h"
#include "../../common/wc3_pathing_proximity.h"
#include "../../common/wc3_pathing_coordinates.h"

static wc3ProximityMap_t move_proximity;
typedef struct { vec2_t world,fine,published;float radius;bool valid,pose_valid; } moveProximityGeometry_t;
static moveProximityGeometry_t move_proximity_geometry[MAX_ENTITIES];

void S_ClearMoveProximity(void) {
    if(move_proximity.cells)memset(move_proximity.cells,0,(size_t)move_proximity.width*move_proximity.height*sizeof(*move_proximity.cells));
    if(move_proximity.objects)memset(move_proximity.objects,0,(size_t)move_proximity.object_count*sizeof(*move_proximity.objects));
    move_proximity.count=move_proximity.free_head=move_proximity.free_count=0;move_proximity.query=0;
    memset(move_proximity_geometry,0,sizeof(move_proximity_geometry));
}

void S_FreeMoveProximity(void) {
    wc3_proximity_free(&move_proximity);memset(move_proximity_geometry,0,sizeof(move_proximity_geometry));
}

static void move_proximity_prepare(void) {
    box2_t bounds=CM_GetWorldBounds();
    uint32_t width=(uint32_t)((bounds.max.x-bounds.min.x)/32+16)/8+1;
    uint32_t height=(uint32_t)((bounds.max.y-bounds.min.y)/32+16)/8+1;
    if(move_proximity.cells && move_proximity.width==width && move_proximity.height==height)return;
    if(!wc3_proximity_init(&move_proximity,width,height,MAX_ENTITIES))gi.error("Move proximity: cannot allocate %ux%u map",width,height);
    memset(move_proximity_geometry,0,sizeof(move_proximity_geometry));
    /* A map geometry change resets both grids through G_ClearMoveSpatial;
     * only initialization may reach here. Do not scan/re-publish other units. */
}

void S_RemoveMoveProximity(edict_t const *unit) {
    uint32_t index=unit-g_edicts;
    if(move_proximity.objects && index<move_proximity.object_count && move_proximity.objects[index].active)
        if(!wc3_proximity_update(&move_proximity,index,(wc3FineBox_t){0},false))gi.error("Move proximity: invalid removal");
    if(index<MAX_ENTITIES)move_proximity_geometry[index].valid=false;
}

void S_PublishMoveProximity(edict_t const *unit) {
    if(!unit || !g_edicts || !pathmap.width || !pathmap.height)return;
    uint32_t index=unit-g_edicts;
    if(index>=MAX_ENTITIES)return;
    if(!unit->inuse || IS_HOLLOW(unit) || !unit->data.UnitData || unit->collision<=0) {S_RemoveMoveProximity(unit);return;}
    move_proximity_prepare();
    moveProximityGeometry_t key={.world=unit->s.origin2,.fine=unit->movement.fine_pose,
        .published=unit->movement.pose_world,.radius=unit->collision,.valid=true,.pose_valid=unit->movement.pose_valid};
    moveProximityGeometry_t const *cached=move_proximity_geometry+index;
    if(cached->valid && cached->pose_valid==key.pose_valid &&
        !memcmp(&cached->world,&key.world,sizeof(vec2_t)) &&
        !memcmp(&cached->fine,&key.fine,sizeof(vec2_t)) &&
        !memcmp(&cached->published,&key.published,sizeof(vec2_t)) &&
        wc3_float_bits(cached->radius)==wc3_float_bits(key.radius))return;
    box2_t bounds=CM_GetWorldBounds();
    vec2_t point=key.pose_valid && !memcmp(&key.world,&key.published,sizeof(vec2_t)) ? key.fine :
        (vec2_t){wc3_grid_coordinate(key.world.x,bounds.min.x,32),wc3_grid_coordinate(key.world.y,bounds.min.y,32)};
    wc3FineBox_t box=wc3_proximity_bounds((float[]){point.x,point.y},wc3_div(unit->collision,32));
    if(!wc3_proximity_update(&move_proximity,index,box,true))gi.error("Move proximity: cannot publish complete membership");
    move_proximity_geometry[index]=key;
}

/* Pair accumulation does not mutate membership. Resolve duplicate candidates
 * once in first-cell/newest-link order, before the ability's eligibility test. */
typedef struct { bool (*candidate)(edict_t const *); } moveProximityQuery_t;
static void move_proximity_candidate(void *data,uint32_t index) {
    moveProximityQuery_t const *query=data;
    query->candidate(g_edicts+index);
}

void S_QueryMoveProximity(edict_t const *source,float const point[2],float radius,bool (*candidate)(edict_t const *)) {
    G_SyncMoveSpatial();move_proximity_prepare();
    moveProximityQuery_t query={candidate};
    wc3_proximity_query(&move_proximity,wc3_proximity_bounds(point,radius),source ? (uint32_t)(source-g_edicts) : UINT32_MAX,
        move_proximity_candidate,&query);
}

wc3FineBox_t const *S_GetMoveProximity(uint32_t index) {
    return move_proximity.objects && index<move_proximity.object_count && move_proximity.objects[index].active ?
        &move_proximity.objects[index].box : NULL;
}

bool S_LoadMoveProximity(uint32_t index,wc3FineBox_t box) {
    if(index>=globals.num_edicts || !g_edicts[index].inuse || S_GetMoveProximity(index) ||
        box.min.x>=box.max.x || box.min.y>=box.max.y)return false;
    move_proximity_prepare();
    if(!wc3_proximity_update(&move_proximity,index,box,true))return false;
    edict_t const *unit=g_edicts+index;
    move_proximity_geometry[index]=(moveProximityGeometry_t){.world=unit->s.origin2,.fine=unit->movement.fine_pose,
        .published=unit->movement.pose_world,.radius=unit->collision,.valid=true,.pose_valid=unit->movement.pose_valid};
    return true;
}

#ifdef BZ_TESTS
uint32_t S_TestMoveProximityLinks(void) {return move_proximity.count-move_proximity.free_count;}
#endif
