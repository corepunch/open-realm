#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../../common/wc3_pathing_proximity.h"
extern void reset_entities(void),setup_test_world(void);
extern edict_t *alloc_test_unit(uint32_t,float,float);
extern void CM_SetupTestPathmap(unsigned,unsigned,uint8_t const *);
extern void CM_SetupTestWorldBounds(box2_t const *);

typedef struct {uint32_t values[32],count;} proximityTestQuery_t;
static void proximity_test_visit(void *data,uint32_t owner) {
    proximityTestQuery_t *query=data;
    if(query->count<32)query->values[query->count]=owner;
    query->count++;
}
static proximityTestQuery_t proximity_test_query(wc3ProximityMap_t *map,wc3FineBox_t box) {
    proximityTestQuery_t query={0};wc3_proximity_query(map,box,UINT32_MAX,proximity_test_visit,&query);return query;
}

TEST(wc3_proximity, intersection_links_survive_and_reentry_prepends) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,8,8,3));
    wc3FineBox_t box={{2,2},{4,4}},query={{2,2},{3,3}};
    T_ASSERT(wc3_proximity_update(&map,0,box,true));T_ASSERT(wc3_proximity_update(&map,1,box,true));
    proximityTestQuery_t q=proximity_test_query(&map,query);T_EQ(q.count,2);T_EQ(q.values[0],1);T_EQ(q.values[1],0);
    T_ASSERT(wc3_proximity_update(&map,0,(wc3FineBox_t){{2,2},{5,4}},true));
    q=proximity_test_query(&map,query);T_EQ(q.count,2);T_EQ(q.values[0],1);T_EQ(q.values[1],0);
    T_ASSERT(wc3_proximity_update(&map,0,(wc3FineBox_t){{3,2},{5,4}},true));
    T_ASSERT(wc3_proximity_update(&map,0,box,true));
    q=proximity_test_query(&map,query);T_EQ(q.values[0],0);T_EQ(q.values[1],1);
    q=proximity_test_query(&map,(wc3FineBox_t){{0,0},{8,8}});T_EQ(q.count,2);
    T_ASSERT(wc3_proximity_update(&map,0,(wc3FineBox_t){0},false));
    q=proximity_test_query(&map,query);T_EQ(q.count,1);T_EQ(q.values[0],1);
    wc3_proximity_free(&map);
}

TEST(wc3_proximity, growth_reuse_and_epoch_wrap_preserve_dense_order) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,8,8,4096));
    wc3FineBox_t box={{2,2},{4,4}};
    FOR_LOOP(i,4096)T_ASSERT(wc3_proximity_update(&map,i,box,true));
    T_EQ(map.count,16384);T_EQ(map.capacity,16384);
    proximityTestQuery_t q=proximity_test_query(&map,box);T_EQ(q.count,4096);FOR_LOOP(i,32)T_EQ(q.values[i],4095-i);
    map.query=UINT64_MAX;
    q=proximity_test_query(&map,box);T_EQ(q.count,4096);FOR_LOOP(i,32)T_EQ(q.values[i],4095-i);
    FOR_LOOP(i,4096)T_ASSERT(wc3_proximity_update(&map,i,(wc3FineBox_t){0},false));
    T_EQ(map.free_count,16384);q=proximity_test_query(&map,box);T_EQ(q.count,0);
    FOR_LOOP(i,4096)T_ASSERT(wc3_proximity_update(&map,i,box,true));
    T_EQ(map.count,16384);T_EQ(map.free_count,0);q=proximity_test_query(&map,box);T_EQ(q.count,4096);
    FOR_LOOP(i,32)T_EQ(q.values[i],4095-i);
    wc3_proximity_free(&map);
}

static edict_t *proximity_save_units[3],*proximity_save_results[3];static unsigned proximity_save_count;
static bool proximity_save_visit(edict_t const *unit) {
    if(proximity_save_count<3)proximity_save_results[proximity_save_count]= (edict_t *)unit;
    proximity_save_count++;return false;
}
static void proximity_save_query(void) {
    proximity_save_count=0;S_QueryMoveProximity(NULL,(float[]){9.5f,9.5f},1,proximity_save_visit);
    T_EQ(proximity_save_count,3);
}

TEST(wc3_proximity, save_load_rebuilds_proximity_in_save_order) {
    reset_entities();setup_test_world();uint8_t cells[64*64]={0};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    FOR_LOOP(i,3) {
        edict_t *unit=proximity_save_units[i]=alloc_test_unit(MAKEFOURCC('h','f','o','o'),304+i*2,304);
        unit->collision=8;unit->stand=unit_stand;unit->s.model=1;unit->svflags|=SVF_MONSTER;
        unit_stand(unit);gi.LinkEntity(unit);G_PublishMoveSpatialObject(unit);
    }
    S_SetUnitAxisPosition(proximity_save_units[0],0,600);
    S_SetUnitAxisPosition(proximity_save_units[0],0,304);
    proximity_save_query();T_EQ(proximity_save_results[0],proximity_save_units[0]);
    cstring_t file="/tmp/wc3-proximity-save-order.bin";T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
    proximity_save_query();FOR_LOOP(i,3)T_EQ(proximity_save_results[i],proximity_save_units[2-i]);
    FOR_LOOP(i,3)gi.LinkEntity(proximity_save_units[i]);
    proximity_save_query();FOR_LOOP(i,3)T_EQ(proximity_save_results[i],proximity_save_units[2-i]);
    G_FreeEdict(proximity_save_units[1]);
    proximity_save_count=0;S_QueryMoveProximity(NULL,(float[]){9.5f,9.5f},1,proximity_save_visit);T_EQ(proximity_save_count,2);
    remove(file);reset_entities();setup_test_world();
}

TEST(wc3_proximity, saved_rectangles_reject_duplicate_and_inverted_records) {
    reset_entities();setup_test_world();edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),304,304);
    T_ASSERT(S_LoadMoveProximity(unit-g_edicts,(wc3FineBox_t){{1,1},{2,2}}));
    T_ASSERT(!S_LoadMoveProximity(unit-g_edicts,(wc3FineBox_t){{1,1},{2,2}}));
    S_ClearMoveProximity();T_NULL(S_GetMoveProximity(unit-g_edicts));
    T_ASSERT(!S_LoadMoveProximity(unit-g_edicts,(wc3FineBox_t){{2,1},{1,2}}));
    T_ASSERT(!S_LoadMoveProximity(globals.num_edicts,(wc3FineBox_t){{1,1},{2,2}}));
    reset_entities();setup_test_world();
}
TEST(wc3_proximity, building_keeps_independent_fine_and_proximity_records) {
    reset_entities();setup_test_world();uint8_t cells[64*64]={0};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),304,304);
    UnitData_t data={.moveTypeName="foot"};unit->data.UnitData=&data;
    unit->s.model=1;unit->collision=8;G_PublishMoveSpatialObject(unit);
    T_ASSERT(S_GetMoveProximity(unit-g_edicts)!=NULL);
    unit->s.flags|=EF_BUILDING;G_PublishMoveSpatialObject(unit);
    T_ASSERT(S_GetMoveProximity(unit-g_edicts)!=NULL);
    wc3FineBox_t fine=G_GetMoveSpatialObject(unit-g_edicts)->box;
    T_EQ(fine.min.x,9);T_EQ(fine.max.x,10);
    unit->s.flags&=~EF_BUILDING;G_PublishMoveSpatialObject(unit);
    T_ASSERT(S_GetMoveProximity(unit-g_edicts)!=NULL);
    G_RemoveMoveSpatialObject(unit);T_NULL(S_GetMoveProximity(unit-g_edicts));
    reset_entities();setup_test_world();
}
#endif
