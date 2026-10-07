#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../../common/wc3_pathing_segment.h"
#include "../../common/wc3_pathing_records.h"

void reset_entities(void);
void setup_test_world(void);
edict_t *alloc_test_unit(uint32_t,float,float);
uint32_t G_TestMoveSpatialLinkVisits(bool);

typedef struct {wc3FinePoint_t points[64];unsigned count;bool blocked;} fineSpatialTrace_t;
static bool fine_spatial_trace(void const *data,wc3FinePoint_t pos) {
    fineSpatialTrace_t *trace=(fineSpatialTrace_t *)data;
    if(trace->count<64)trace->points[trace->count]=pos;
    trace->count++;return !trace->blocked;
}

/*148d00 visits the whole perimeter once, including after an obstruction.
 * Repeating eight entering-strip queries changes stamps and misses observers. */
TEST(wc3_fine_spatial, expansion_visits_complete_clockwise_perimeter_once) {
    static wc3FinePoint_t const expected[4][20]={
        {{-1,-1},{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0}},
        {{-2,-2},{-1,-2},{0,-2},{1,-2},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-2,1},{-2,0},{-2,-1}},
        {{-2,-2},{-1,-2},{0,-2},{1,-2},{2,-2},{2,-1},{2,0},{2,1},{2,2},{1,2},{0,2},{-1,2},{-2,2},{-2,1},{-2,0},{-2,-1}},
        {{-3,-3},{-2,-3},{-1,-3},{0,-3},{1,-3},{2,-3},{2,-2},{2,-1},{2,0},{2,1},{2,2},{1,2},{0,2},{-1,2},{-2,2},{-3,2},{-3,1},{-3,0},{-3,-1},{-3,-2}}
    };
    FOR_LOOP(cls,4)FOR_LOOP(blocked,2) {
        fineSpatialTrace_t trace={.blocked=blocked};
        wc3FineSegment_t query={.cls=cls,.cell=fine_spatial_trace,.data=&trace};
        T_EQ(wc3_fine_cell_edges(&query,(wc3FinePoint_t){0,0}),blocked ? 0 : 255);
        unsigned count=8+4*cls;T_EQ(trace.count,count);
        FOR_LOOP(i,count) {T_EQ(trace.points[i].x,expected[cls][i].x);T_EQ(trace.points[i].y,expected[cls][i].y);}
    }
}

/*14e770 changes only leaving/entering strips. A one-cell shift of a4x4
 * owner must leave its twelve retained cells alone. */
TEST(wc3_fine_spatial, one_cell_shift_does_not_unlink_retained_intersection) {
    reset_entities();setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),304,304);
    unit->collision=48;unit->s.model=1;G_PublishMoveSpatialObject(unit);
    G_TestMoveSpatialLinkVisits(true);
    unit->s.origin2.x+=32;G_PublishMoveSpatialObject(unit);
    T_EQ(G_TestMoveSpatialLinkVisits(true),4);
    reset_entities();setup_test_world();
}
extern void CM_SetupTestPathmap(unsigned,unsigned,uint8_t const *);
extern void CM_SetupTestWorldBounds(box2_t const *);
static void fine_spatial_world(void) {
    reset_entities();setup_test_world();
    uint8_t cells[64*64]={0};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    level.pathing_clock=(wc3Clock_t){.span=300};level.timer_clock_valid=false;
}
static edict_t *fine_spatial_unit(float x,float y,float radius) {
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),x*32,y*32);
    unit->collision=radius;unit->s.model=1;G_PublishMoveSpatialObject(unit);return unit;
}

TEST(wc3_fine_spatial, retained_heads_and_retired_identity_survive_owner_slot_reuse) {
    fine_spatial_world();edict_t *unit=fine_spatial_unit(9.5f,9.5f,48);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    uint32_t owner=unit-g_edicts,id=map->objects[owner];wc3RecordObject_t *object=wc3_records_object(map,id);
    T_EQ(map->records,16);T_EQ(object->refs,16);
    uint32_t heads[12],count=0;
    for(unsigned y=7;y<11;y++)for(unsigned x=8;x<11;x++)heads[count++]=map->cells[y*64+x];
    unit->s.origin2.x+=32;G_PublishMoveSpatialObject(unit);
    T_EQ(map->records,24);T_EQ(object->refs,24);count=0;
    for(unsigned y=7;y<11;y++)for(unsigned x=8;x<11;x++)T_EQ(map->cells[y*64+x],heads[count++]);
    G_RemoveMoveSpatialObject(unit);T_EQ(map->records,40);T_EQ(object->refs,40);T_EQ(object->stamp,UINT32_MAX);
    T_NULL(G_GetMoveSpatialObject(owner));
    /* Same edict, fresh spatial identity; old links cannot resolve to it. */
    unit->s.origin2=(vec2_t){40.5f*32,40.5f*32};G_PublishMoveSpatialObject(unit);
    T_ASSERT(map->objects[owner]!=id);T_EQ(object->stamp,UINT32_MAX);T_EQ(map->records,56);
    S_CompactMoveFineSpatial();T_EQ(map->records,16);T_EQ(object->refs,0);T_EQ(map->free_object,id);
    T_EQ(map->free_count+map->records,map->count);
    reset_entities();setup_test_world();
}

TEST(wc3_fine_spatial, real_search_publishes_head_metadata_and_reuses_it_across_stamp_wrap) {
    fine_spatial_world();edict_t *unit=fine_spatial_unit(5.5f,5.5f,8);
    float const fine_source[]={5.5f,5.5f},fine_goal[]={48.5f,43.5f};
    vec2_t source={fine_source[0],fine_source[1]},goal={fine_goal[0],fine_goal[1]},out;
    movePathQuery_t query={.geometry={&source,&goal,8,2},.mover=unit,.units=true,.fine=&source,.fine_target=&goal};
    G_SetMoveFineSearchStamp(UINT16_MAX-1);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();uint32_t retained=0;
    FOR_LOOP(pass,4) {
        S_ClearMoveFineRequests();level.pathing_counter+=10*(pass+1);moveFineRoute_t route={0};
        T_ASSERT(G_BuildUnitMoveLocalRoute(&query,&route,&out));
        uint16_t stamp=UINT16_MAX+pass;
        T_EQ(G_GetMoveFineSearchStamp(),stamp);
        uint32_t head=map->cells[5*64+5]&WC3_RECORD_END;
        T_ASSERT(head!=WC3_RECORD_END);T_EQ(map->links[head].next&~WC3_RECORD_END,WC3_RECORD_METADATA);
        T_EQ(map->links[head].payload,stamp); /* source index0 */
        if(!pass)retained=map->records;
        else T_EQ(map->records,retained);
        T_ASSERT(route.count>1);
    }
    T_ASSERT(retained>100);
    S_CompactMoveFineSpatial();T_EQ(map->records,1);T_EQ(map->free_count+map->records,map->count);
    reset_entities();setup_test_world();
}

TEST(wc3_fine_spatial, metadata_and_removals_wait_for_independent_fine_deadline) {
    fine_spatial_world();edict_t *units[4];
    FOR_LOOP(i,4)units[i]=fine_spatial_unit(i+2.5f,i+2.5f,48);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    /* Frozen MAP-05.3 constructor: 64 initial +2000 metadata +16 removals
     * +4 new +16 retired removals =2100, then36 live memberships. */
    FOR_LOOP(i,2000)wc3_records_prepend(map,(i%32)+(i/32%32)*64,i,WC3_RECORD_METADATA);
    units[0]->collision=16;units[0]->s.origin2=(vec2_t){21.5f*32,21.5f*32};G_PublishMoveSpatialObject(units[0]);
    G_RemoveMoveSpatialObject(units[1]);T_EQ(map->records,2100);uint32_t high=map->count;
    wc3Clock_t due;uint32_t sequence;T_ASSERT(S_NextMoveSpatialMaintenance(&due,&sequence));
    T_EQ(wc3_float_bits(due.time),0x3dccccce);
    S_RunMoveSpatialMaintenance(); /* proximity first; fine remains unchanged */
    T_EQ(map->records,2100);wc3Clock_t fine;uint32_t fine_sequence;
    T_ASSERT(S_NextMoveSpatialMaintenance(&fine,&fine_sequence));T_EQ(fine_sequence,sequence+1);
    T_EQ(wc3_float_bits(fine.time),0x3dccccce);
    S_RunMoveSpatialMaintenance();T_EQ(map->records,36);T_EQ(map->count,high);T_EQ(map->free_count,2064);
    T_EQ(map->records+map->free_count,map->count);
    FOR_LOOP(cell,64*64)for(uint32_t at=map->cells[cell]&WC3_RECORD_END;at!=WC3_RECORD_END;at=map->links[at].next&WC3_RECORD_END)
        T_EQ(map->links[at].next&~WC3_RECORD_END,WC3_RECORD_INSERT);
    reset_entities();setup_test_world();
}

/* Labelled forced counter jump, not seven hours of unobserved gameplay.
 * Repair retains object stamps; native compaction can alias the first stamp. */
TEST(wc3_fine_spatial, labelled_stamp_repair_retains_native_unlink_alias) {
    fine_spatial_world();edict_t *unit=fine_spatial_unit(5.5f,5.5f,8);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    wc3RecordObject_t *object=wc3_records_owned(map,unit-g_edicts);
    wc3_records_node(map,(wc3FinePoint_t){5,5},0,0);object->stamp=1;map->query=INT32_MAX+1u;
    S_RunMoveSpatialMaintenance();T_EQ(map->query,INT32_MAX+1u);
    S_RunMoveSpatialMaintenance();T_EQ(map->query,1);T_EQ(map->records,0);T_EQ(object->refs,0);
    T_NOT_NULL(G_GetMoveSpatialObject(unit-g_edicts)); /* identity still live */
    reset_entities();setup_test_world();
}
TEST(wc3_fine_spatial, growth_inside_publication_keeps_every_entered_cell) {
    fine_spatial_world();edict_t *filler=fine_spatial_unit(9.5f,9.5f,48);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    FOR_LOOP(i,4095) {
        filler->s.origin2.x=(i&1 ? 9.5f : 30.5f)*32;G_PublishMoveSpatialObject(filler);
    }
    fine_spatial_unit(50.5f,50.5f,16);fine_spatial_unit(54.5f,50.5f,16);
    T_EQ(map->count,WC3_RECORD_GROWTH-8);T_EQ(map->capacity,WC3_RECORD_GROWTH);
    edict_t *unit=fine_spatial_unit(20.5f,20.5f,48);
    wc3RecordObject_t const *object=G_GetMoveSpatialObject(unit-g_edicts);
    T_EQ(map->capacity,2*WC3_RECORD_GROWTH);T_EQ(object->refs,16);
    for(unsigned y=18;y<22;y++)for(unsigned x=18;x<22;x++) {
        uint32_t head=map->cells[y*64+x]&WC3_RECORD_END;
        T_EQ(map->links[head].payload,map->objects[unit-g_edicts]);
        T_EQ(map->links[head].next&~WC3_RECORD_END,WC3_RECORD_INSERT);
    }
    S_CompactMoveFineSpatial();T_EQ(map->records,40);
    T_EQ(map->capacity,2*WC3_RECORD_GROWTH);T_EQ(map->free_count+map->records,map->count);
    reset_entities();setup_test_world();
}
TEST(wc3_fine_spatial, fresh_sixty_four_object_block_keeps_addresses_and_recycles_lifo) {
    fine_spatial_world();G_FreeMovePathCache();edict_t *units[65];
    units[0]=fine_spatial_unit(.5f,.5f,8);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();wc3RecordObject_t *first=wc3_records_owned(map,units[0]-g_edicts);
    FOR_LOOP(i,64)units[i+1]=fine_spatial_unit((i+1)%64+.5f,(i+1)/64+.5f,8);
    T_EQ(map->block_count,2);T_EQ(map->raw_objects,65);T_EQ(map->records,65);
    T_EQ(wc3_records_owned(map,units[0]-g_edicts),first);
    FOR_LOOP(i,65)G_RemoveMoveSpatialObject(units[i]);
    T_EQ(map->records,130);T_EQ(map->live_objects,65);
    S_CompactMoveFineSpatial();T_EQ(map->records,0);T_EQ(map->live_objects,0);T_EQ(map->free_object,64);
    G_PublishMoveSpatialObject(units[0]);T_EQ(map->objects[units[0]-g_edicts],64);
    T_EQ(map->raw_objects,65);T_EQ(map->block_count,2);
    reset_entities();setup_test_world();
}
#endif
