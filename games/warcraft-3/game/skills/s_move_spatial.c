#include "../g_local.h"
#include "../../common/wc3_pathing_proximity.h"
#include "../../common/wc3_pathing_coordinates.h"

static wc3ProximityMap_t move_proximity;
static wc3SpatialRecords_t move_fine_spatial;
typedef struct {wc3Clock_t deadline;uint32_t sequence;bool active;} moveSpatialRequest_t;
static moveSpatialRequest_t move_proximity_request,move_fine_request;

static void move_proximity_prepare(void);

static void move_spatial_request(moveSpatialRequest_t *request) {
    if(request->active)return;
    request->deadline=G_TimerQueryClock(NULL);
    request->deadline.time=wc3_add(request->deadline.time,wc3_div(1,10));
    request->sequence=++level.timer_sequence;request->active=true;
}

wc3SpatialRecords_t *S_GetMoveFineSpatial(void) {return &move_fine_spatial;}

void S_PrepareMoveFineSpatial(void) {
    move_proximity_prepare();
    if(!move_fine_spatial.cells || move_fine_spatial.width!=pathmap.width || move_fine_spatial.height!=pathmap.height) {
        if(!wc3_records_init(&move_fine_spatial,pathmap.width,pathmap.height,MAX_ENTITIES))
            gi.error("Move fine occupancy: invalid %ux%u map",pathmap.width,pathmap.height);
        wc3_records_occupancy(&move_fine_spatial);move_fine_request.active=false;
    }
    move_spatial_request(&move_fine_request);
}
void S_ClearMoveFineSpatial(void) {
    wc3_records_clear(&move_fine_spatial);move_fine_request.active=false;
}
void S_FreeMoveFineSpatial(void) {
    wc3_records_free(&move_fine_spatial);move_fine_request.active=false;
}
void S_CompactMoveFineSpatial(void) {
    if(move_fine_spatial.cells)wc3_records_compact(&move_fine_spatial,true);
}
typedef struct { vec2_t world,fine,published;float radius;bool valid,pose_valid; } moveProximityGeometry_t;
static moveProximityGeometry_t move_proximity_geometry[MAX_ENTITIES];

void S_ClearMoveProximity(void) {
    wc3_records_clear(&move_proximity);
    move_proximity_request.active=false;
    memset(move_proximity_geometry,0,sizeof(move_proximity_geometry));
}

void S_FreeMoveProximity(void) {
    wc3_proximity_free(&move_proximity);memset(move_proximity_geometry,0,sizeof(move_proximity_geometry));
    move_proximity_request.active=false;
}

static void move_proximity_prepare(void) {
    box2_t bounds=CM_GetWorldBounds();
    uint32_t width=(uint32_t)((bounds.max.x-bounds.min.x)/32+16)/8+1;
    uint32_t height=(uint32_t)((bounds.max.y-bounds.min.y)/32+16)/8+1;
    if(!move_proximity.cells || move_proximity.width!=width || move_proximity.height!=height) {
        if(!wc3_proximity_init(&move_proximity,width,height,MAX_ENTITIES))gi.error("Move proximity: invalid %ux%u map",width,height);
        memset(move_proximity_geometry,0,sizeof(move_proximity_geometry));
        move_proximity_request.active=false;
    }
    move_spatial_request(&move_proximity_request);
    /* A map geometry change resets both grids through G_ClearMoveSpatial;
     * only initialization may reach here. Do not scan/re-publish other units. */
}

void S_RemoveMoveProximity(edict_t const *unit) {
    uint32_t index=unit-g_edicts;
    if(move_proximity.objects && wc3_records_owned(&move_proximity,index))
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
    wc3RecordObject_t *object=move_proximity.objects ? wc3_records_owned(&move_proximity,index) : NULL;
    return object ? &object->box : NULL;
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

/* One recurring request per map, merged with the ordinary scalar timer heap.
 * Repeats add the retail software 1/10 word to the previous deadline; neither
 * record counts nor wall time select cleanup. Load registers a fresh request. */
static moveSpatialRequest_t *move_next_spatial_request(void) {
    if(!move_proximity_request.active)return move_fine_request.active ? &move_fine_request : NULL;
    if(!move_fine_request.active)return &move_proximity_request;
    int32_t epochs=(int32_t)(move_proximity_request.deadline.epoch-move_fine_request.deadline.epoch);
    int order=epochs ? (epochs<0 ? -1 : 1) : move_proximity_request.deadline.time<move_fine_request.deadline.time ? -1 :
        move_proximity_request.deadline.time>move_fine_request.deadline.time ? 1 : 0;
    return order<0 || (!order && move_proximity_request.sequence<move_fine_request.sequence) ?
        &move_proximity_request : &move_fine_request;
}
bool S_NextMoveSpatialMaintenance(wc3Clock_t *deadline,uint32_t *sequence) {
    moveSpatialRequest_t *request=move_next_spatial_request();
    if(!request)return false;
    *deadline=request->deadline;*sequence=request->sequence;return true;
}
void S_RunMoveSpatialMaintenance(void) {
    moveSpatialRequest_t *request=move_next_spatial_request();
    if(!request)return;
    wc3_records_compact(request==&move_proximity_request ? &move_proximity : &move_fine_spatial,false);
    request->deadline.time=wc3_add(request->deadline.time,wc3_div(1,10));
}
void S_ResetMoveSpatialMaintenance(void) {
    move_proximity_request.active=move_fine_request.active=false;
    if(move_proximity.cells)move_spatial_request(&move_proximity_request);
    if(move_fine_spatial.cells)move_spatial_request(&move_fine_request);
}
void S_RebaseMoveSpatialMaintenance(float span) {
    moveSpatialRequest_t *requests[]={&move_proximity_request,&move_fine_request};
    FOR_LOOP(i,2)if(requests[i]->active) {
        requests[i]->deadline.time=wc3_sub(requests[i]->deadline.time,span);
        requests[i]->deadline.epoch++;
    }
}
void S_CompactMoveProximity(void) {
    if(move_proximity.cells)wc3_records_compact(&move_proximity,true);
}
uint32_t S_GetMoveProximityQuery(void) {return move_proximity.query;}
void S_SetMoveProximityQuery(uint32_t stamp) {move_proximity.query=stamp;}
uint32_t S_GetMoveProximityStamp(uint32_t index) {
    wc3RecordObject_t *object=move_proximity.objects ? wc3_records_owned(&move_proximity,index) : NULL;
    return object ? object->stamp : UINT32_MAX;
}
void S_SetMoveProximityStamp(uint32_t index,uint32_t stamp) {
    wc3RecordObject_t *object=wc3_records_owned(&move_proximity,index);
    if(object)object->stamp=stamp;
}

#ifdef BZ_TESTS
uint32_t S_TestMoveProximityLinks(void) {return move_proximity.records;}
#endif
