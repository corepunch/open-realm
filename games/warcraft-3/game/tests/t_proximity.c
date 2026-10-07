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

TEST(wc3_proximity, removal_keeps_records_until_maintenance) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,8,8,2));
    wc3FineBox_t box={{2,2},{3,3}};
    T_ASSERT(wc3_proximity_update(&map,0,box,true));
    T_EQ(map.capacity,131072);
    T_ASSERT(wc3_proximity_update(&map,0,(wc3FineBox_t){0},false));
    T_EQ(map.count,2);T_EQ(map.free_count,0);
    proximityTestQuery_t q=proximity_test_query(&map,box);T_EQ(q.count,0);
    /* The entity slot is reusable while its old spatial identity still has
     * retained records. Those records must never resolve to the new owner. */
    T_ASSERT(wc3_proximity_update(&map,0,(wc3FineBox_t){{4,4},{5,5}},true));
    q=proximity_test_query(&map,box);T_EQ(q.count,0);
    q=proximity_test_query(&map,(wc3FineBox_t){{4,4},{5,5}});T_EQ(q.count,1);T_EQ(q.values[0],0);
    T_EQ(map.count,3);T_EQ(map.free_count,0);
    wc3_proximity_free(&map);
}

TEST(wc3_proximity, growth_reuse_and_maintenance_preserve_dense_order) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,8,8,4096));
    wc3FineBox_t box={{2,2},{4,4}};
    FOR_LOOP(i,4096)T_ASSERT(wc3_proximity_update(&map,i,box,true));
    T_EQ(map.count,16384);T_EQ(map.capacity,131072);
    proximityTestQuery_t q=proximity_test_query(&map,box);T_EQ(q.count,4096);FOR_LOOP(i,32)T_EQ(q.values[i],4095-i);
    wc3_records_compact(&map,false);
    q=proximity_test_query(&map,box);T_EQ(q.count,4096);FOR_LOOP(i,32)T_EQ(q.values[i],4095-i);
    FOR_LOOP(i,4096)T_ASSERT(wc3_proximity_update(&map,i,(wc3FineBox_t){0},false));
    T_EQ(map.free_count,0);T_EQ(map.records,32768);
    q=proximity_test_query(&map,box);T_EQ(q.count,0);
    wc3_records_compact(&map,false);T_EQ(map.free_count,32768);T_EQ(map.records,0);
    FOR_LOOP(i,4096)T_ASSERT(wc3_proximity_update(&map,i,box,true));
    T_EQ(map.count,32768);T_EQ(map.free_count,16384);q=proximity_test_query(&map,box);T_EQ(q.count,4096);
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

TEST(wc3_proximity, retail_link_boundary_reclaims_and_reuses_exact_slots) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,64,64,8));
    wc3FineBox_t big={{0,0},{16,16}},alt={{32,32},{48,48}};
    unsigned flip=0;
    while(map.count+512<=131054) {
        T_ASSERT(wc3_proximity_update(&map,0,flip ? alt : big,true));flip^=1;
    }
    T_ASSERT(wc3_proximity_update(&map,1,(wc3FineBox_t){{0,60},{1,61}},true));
    unsigned pos=0;
    while(map.count<131054) {
        if(131054-map.count==1)T_ASSERT(wc3_proximity_update(&map,2,(wc3FineBox_t){{3,63},{4,64}},true));
        else {pos^=1;T_ASSERT(wc3_proximity_update(&map,1,(wc3FineBox_t){{pos,60},{pos+1,61}},true));}
    }
    T_ASSERT(wc3_proximity_update(&map,3,(wc3FineBox_t){{20,20},{22,22}},true));
    T_ASSERT(wc3_proximity_update(&map,4,(wc3FineBox_t){{20,20},{23,23}},true));
    T_EQ(map.count,131067);T_EQ(map.capacity,131072);
    wc3FineBox_t box={{19,19},{25,25}};
    T_ASSERT(wc3_proximity_update(&map,5,box,true));
    uint32_t identity=map.objects[5];wc3RecordObject_t *object=wc3_records_object(&map,identity);
    T_EQ(map.capacity,262144);T_EQ(map.count,131103);T_EQ(object->refs,36);
    FOR_LOOP(y,6)FOR_LOOP(x,6) {
        uint32_t cell=(19+y)*64+19+x,id=map.cells[cell]&WC3_RECORD_END;
        T_EQ(id,131067+y*6+x);T_EQ(map.links[id].payload,identity);
        T_EQ(map.links[id].next&~WC3_RECORD_END,WC3_RECORD_INSERT);
    }
    T_ASSERT(wc3_proximity_update(&map,5,(wc3FineBox_t){0},false));
    T_EQ(map.count,131139);T_EQ(object->refs,72);T_EQ(map.free_count,0);
    wc3_records_compact(&map,false);
    T_EQ(map.free_count,130868);T_EQ(map.free_object,identity);T_EQ(object->refs,0);
    /* Frozen original MAP-05.1 free-list prefix, not a second implementation. */
    uint32_t const expected_free[]={130818,130819,130822,130823,130826,130827,130830,130831,130834,130835,130838,130839,130842,130843,130846,130847,130850,130851,130854,130855,130858,130859,130862,130863,130866,130867,130870,130871,130874,130875,130878,130879,130882,130883,130886,130887,130890,130891,130894,130895,130898,130899,130902,130903,130906,130907,130910,130911,130914,130915,130918,130919,130922,130923,130926,130927,130930,130931,130934,130935,130938,130939,130942,130943,130946,130947,130950,130951,130954,130955,130958,130959,130962,130963,130966,130967,130970,130971,130974,130975};
    uint32_t at=map.free_head;
    FOR_LOOP(i,sizeof(expected_free)/sizeof(*expected_free)) {T_EQ(at,expected_free[i]);at=map.links[at].next&WC3_RECORD_END;}
    T_ASSERT(wc3_proximity_update(&map,6,box,true));
    T_EQ(map.objects[6],identity);T_ASSERT(wc3_records_object(&map,map.objects[6])==object);
    T_EQ(map.count,131139);T_EQ(map.capacity,262144);T_EQ(map.free_count,130832);
    uint32_t const expected_slots[]={130818,130819,130822,130823,130826,130827,130830,130831,130834,130835,130838,130839,130842,130843,130846,130847,130850,130851,130854,130855,130858,130859,130862,130863,130866,130867,130870,130871,130874,130875,130878,130879,130882,130883,130886,130887};
    FOR_LOOP(y,6)FOR_LOOP(x,6)T_EQ(map.cells[(19+y)*64+19+x]&WC3_RECORD_END,expected_slots[y*6+x]);
    proximityTestQuery_t q=proximity_test_query(&map,(wc3FineBox_t){{18,18},{26,26}});
    T_EQ(q.count,3);T_EQ(q.values[0],6);T_EQ(q.values[1],4);T_EQ(q.values[2],3);
    wc3_records_clear(&map);T_EQ(map.capacity,262144);T_EQ(map.count,0);T_EQ(map.query,0);
    T_EQ(map.free_head,WC3_RECORD_END);T_EQ(map.raw_objects,0);
    T_ASSERT(wc3_proximity_update(&map,0,box,true));T_EQ(map.objects[0],0);T_EQ(map.count,36);
    q=proximity_test_query(&map,box);T_EQ(q.count,1);T_EQ(q.values[0],0);
    wc3_proximity_free(&map);
}

TEST(wc3_proximity, pooled_objects_recycle_only_after_last_record) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,8,8,70));
    FOR_LOOP(i,65)T_ASSERT(wc3_proximity_update(&map,i,(wc3FineBox_t){{1,1},{2,2}},true));
    T_EQ(map.block_count,2);T_EQ(map.raw_objects,65);T_EQ(map.live_objects,65);
    wc3RecordObject_t *first=wc3_records_object(&map,0);
    unsigned const removed[]={1,4,2};
    FOR_LOOP(i,3)T_ASSERT(wc3_proximity_update(&map,removed[i],(wc3FineBox_t){0},false));
    T_EQ(map.free_object,WC3_RECORD_END);T_EQ(map.live_objects,65);
    wc3_records_compact(&map,false);T_EQ(map.live_objects,62);
    unsigned const recycled[]={1,2,4};
    FOR_LOOP(i,3) {
        T_ASSERT(wc3_proximity_update(&map,65+i,(wc3FineBox_t){{1,1},{2,2}},true));
        T_EQ(map.objects[65+i],recycled[i]);
    }
    T_ASSERT(wc3_records_object(&map,0)==first);T_EQ(map.raw_objects,65);T_EQ(map.block_count,2);
    proximityTestQuery_t q=proximity_test_query(&map,(wc3FineBox_t){{1,1},{2,2}});
    T_EQ(q.count,65);T_EQ(q.values[0],67);T_EQ(q.values[1],66);T_EQ(q.values[2],65);
    wc3_proximity_free(&map);
}

TEST(wc3_proximity, labelled_forced_stamp_repair_retains_native_aliases) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,8,8,2));
    wc3FineBox_t box={{1,1},{2,2}};
    T_ASSERT(wc3_proximity_update(&map,0,box,true));
    wc3RecordObject_t *object=wc3_records_owned(&map,0);
    object->stamp=15;map.query=0x80000000;
    wc3_records_compact(&map,false);T_EQ(map.query,0);T_EQ(object->stamp,15);
    /* Labelled jump: no natural seven-hour wrap is claimed. */
    map.query=14;proximityTestQuery_t q=proximity_test_query(&map,box);T_EQ(q.count,0);
    q=proximity_test_query(&map,box);T_EQ(q.count,1);
    wc3_records_prepend(&map,9,0x2211,WC3_RECORD_METADATA);
    object->stamp=1;map.query=0x80000000;
    wc3_records_compact(&map,false);T_EQ(map.query,1);T_EQ(map.records,0);T_EQ(object->refs,0);
    T_ASSERT(!memcmp(&object->box,&box,sizeof(box)));T_EQ(map.live_objects,1);
    q=proximity_test_query(&map,box);T_EQ(q.count,0);
    /* Unchanged geometry does not resurrect a link discarded by stamp alias. */
    T_ASSERT(wc3_proximity_update(&map,0,box,true));T_EQ(map.records,0);
    T_ASSERT(wc3_proximity_update(&map,0,(wc3FineBox_t){{2,1},{3,2}},true));T_EQ(map.records,2);
    wc3_proximity_free(&map);
}

TEST(wc3_proximity, flagged_retirement_waits_for_full_sweep) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,8,8,1));
    uint32_t id=wc3_records_create(&map,0,WC3_RECORD_REGION);
    wc3_records_update(&map,id,(wc3FineBox_t){{0,0},{2,2}});
    wc3_records_retire(&map,id);T_EQ(map.records,4);T_EQ(map.live_objects,1);
    wc3_records_compact(&map,false);T_EQ(map.records,4);T_EQ(map.live_objects,1);
    wc3_records_compact(&map,true);T_EQ(map.records,0);T_EQ(map.live_objects,0);T_EQ(map.free_object,id);
    wc3_proximity_free(&map);
}

extern uint32_t S_TestMoveProximityLinks(void);
TEST(wc3_proximity, scalar_maintenance_deadlines_and_owner_boundary) {
    reset_entities();setup_test_world();level.pathing_clock=(wc3Clock_t){.span=300};
    level.timer_clock_valid=false;level.scheduled_frame=true;level.num_timers=level.timer_heap_count=0;
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),304,304);
    unit->collision=8;unit->s.model=1;G_PublishMoveSpatialObject(unit);
    wc3Clock_t deadline={0};uint32_t sequence=0;
    T_ASSERT(S_NextMoveSpatialMaintenance(&deadline,&sequence));
    T_EQ(wc3_float_bits(deadline.time),0x3dccccce);
    G_RemoveMoveSpatialObject(unit);T_EQ(S_TestMoveProximityLinks(),2);
    /* All12 native deadlines, including the accumulated tenth below1second. */
    uint32_t const words[]={0x3dccccce,0x3e4cccce,0x3e99999a,0x3ecccccd,0x3f000000,0x3f199999,0x3f333332,0x3f4ccccb,0x3f666664,0x3f7ffffd,0x3f8ccccb,0x3f999997};
    FOR_LOOP(i,sizeof(words)/sizeof(*words)) {
        T_EQ(wc3_float_bits(deadline.time),words[i]);
        level.pathing_clock=deadline;
        wc3Clock_t early=deadline;early.time=wc3_float(words[i]-1);
        level.pathing_owner_sequence=UINT32_MAX;G_RunTimersBeforePathOwner(&early);
        wc3Clock_t next={0};uint32_t serial=0;T_ASSERT(S_NextMoveSpatialMaintenance(&next,&serial));
        T_EQ(wc3_float_bits(next.time),words[i]);T_EQ(serial,sequence);
        level.pathing_owner_sequence=sequence-1;G_RunTimersBeforePathOwner(&deadline);
        T_ASSERT(S_NextMoveSpatialMaintenance(&next,&serial));T_EQ(wc3_float_bits(next.time),words[i]);
        level.pathing_owner_sequence=sequence;G_RunTimersBeforePathOwner(&deadline);
        T_EQ(S_TestMoveProximityLinks(),0);
        T_ASSERT(S_NextMoveSpatialMaintenance(&deadline,&serial));T_EQ(serial,sequence);
    }
    /* The same request survives a primary-clock epoch rebase. */
    S_RebaseMoveSpatialMaintenance(300);
    wc3Clock_t rebased={0};uint32_t serial=0;T_ASSERT(S_NextMoveSpatialMaintenance(&rebased,&serial));
    T_EQ(rebased.epoch,deadline.epoch+1);T_EQ(wc3_float_bits(rebased.time),wc3_float_bits(wc3_sub(deadline.time,300)));
    level.scheduled_frame=false;reset_entities();setup_test_world();
    T_ASSERT(!S_NextMoveSpatialMaintenance(&rebased,&serial));
}

TEST(wc3_proximity, save_compacts_links_and_restores_logical_stamps) {
    reset_entities();setup_test_world();level.pathing_clock=(wc3Clock_t){.time=3,.span=300};level.timer_clock_valid=false;
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),304,304);
    unit->collision=8;unit->s.model=1;G_PublishMoveSpatialObject(unit);
    S_SetUnitAxisPosition(unit,0,600);S_SetUnitAxisPosition(unit,0,304);
    T_EQ(S_TestMoveProximityLinks(),5);
    uint32_t index=unit-g_edicts;
    cstring_t file="/tmp/wc3-proximity-stamp-save.bin";
    T_ASSERT(WriteGame(file));T_EQ(S_TestMoveProximityLinks(),1);
    uint32_t query=S_GetMoveProximityQuery(),stamp=S_GetMoveProximityStamp(index);
    T_ASSERT(ReadGame(file));T_EQ(S_TestMoveProximityLinks(),1);
    T_EQ(S_GetMoveProximityQuery(),query);T_EQ(S_GetMoveProximityStamp(index),stamp);
    wc3Clock_t next;uint32_t sequence;T_ASSERT(S_NextMoveSpatialMaintenance(&next,&sequence));
    T_EQ(wc3_float_bits(next.time),wc3_add_bits(wc3_float_bits(G_TimerQueryClock(NULL).time),wc3_float_bits(wc3_div(1,10))));
    remove(file);reset_entities();setup_test_world();
}

TEST(wc3_proximity, full_sweep_preserves_dirty_bitmap_and_next_visit_stamps) {
    wc3ProximityMap_t map={0};T_ASSERT(wc3_proximity_init(&map,8,8,1));
    T_ASSERT(wc3_proximity_update(&map,0,(wc3FineBox_t){{0,0},{2,1}},true));
    T_ASSERT(wc3_proximity_update(&map,0,(wc3FineBox_t){{1,0},{3,1}},true));
    T_EQ(map.dirty[0],1);wc3_records_compact(&map,true);
    T_EQ(map.dirty[0],1);T_EQ(map.records,2);
    uint32_t stamp=map.query;wc3_records_compact(&map,false);
    T_EQ(map.dirty[0],0);T_EQ(map.query,stamp); /* dirty cell0 now empty */
    wc3_records_prepend(&map,1,0x2211,WC3_RECORD_METADATA);
    wc3_records_compact(&map,true);T_EQ(map.dirty[0],2);stamp=map.query;
    wc3_records_compact(&map,false);T_EQ(map.query,stamp+1);T_EQ(map.dirty[0],0);
    wc3_proximity_free(&map);
}
#endif
