#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../../common/wc3_pathing_segment.h"
#include "../../common/wc3_pathing_records.h"
#include "../../common/wc3_pathing_regions.h"
#include "../../common/wc3_pathing_cell.h"
#include "fixtures/retail_region_bounds230.h"

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
    unit->collision=radius;unit->s.model=1;unit->svflags|=SVF_MONSTER;
    G_PublishMoveSpatialObject(unit);return unit;
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

/* FOOT-03.1: flags and active state are properties of the retained identity,
 * independent of owner type or encounter kind. Use real endpoint/collector. */
TEST(wc3_fine_spatial, inactive_and_suppressed_objects_do_not_block_or_emit) {
    fine_spatial_world();edict_t *source=fine_spatial_unit(8.5f,9.5f,8);
    edict_t *unit=fine_spatial_unit(9.5f,9.5f,8);
    wc3RecordObject_t *object=wc3_records_owned(S_GetMoveFineSpatial(),unit-g_edicts);
    vec2_t goal={304,304};movePathQuery_t query={{&source->s.origin2,&goal,8,2},source,NULL,true};
    uint32_t const flags[]={1,2,0x0fffffff,0x80000000};
    FOR_LOOP(i,sizeof(flags)/sizeof(*flags)) {
        object->flags=flags[i];edict_t *tokens[32];
        T_ASSERT(G_UnitMovePathFinePointIsPathable(&query,(float[]){9.5f,9.5f}));
        T_EQ(G_CollectUnitMoveStepBlockers(&query,NULL,tokens),0);
    }
    object->flags=0;object->category=0;edict_t *tokens[32];
    uint32_t stamp=object->stamp;
    T_ASSERT(G_UnitMovePathFinePointIsPathable(&query,(float[]){9.5f,9.5f}));
    T_EQ(G_CollectUnitMoveStepBlockers(&query,NULL,tokens),0);T_EQ(object->stamp,stamp);
    object->category=WC3_RECORD_INSERT;
    T_ASSERT(!G_UnitMovePathFinePointIsPathable(&query,(float[]){9.5f,9.5f}));
    reset_entities();setup_test_world();
}

extern void G_TestMovePathRefresh(point2_t,point2_t);
extern int G_TestMovePathClass(uint8_t,unsigned,unsigned,unsigned);
/*49 raw records, not49 eligible objects. Ordinary movers never qualify;
 * metadata/inactive records still consume the hierarchy's examination budget. */
TEST(wc3_fine_spatial, hierarchy_counts_raw_records_and_excludes_ordinary_movers) {
    fine_spatial_world();edict_t *unit=fine_spatial_unit(9.5f,9.5f,8);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();wc3RecordObject_t *object=wc3_records_owned(map,unit-g_edicts);
    G_TestMovePathRefresh((point2_t){8,8},(point2_t){10,10});
    T_EQ(G_TestMovePathClass(2,0,4,4),0);
    object->flags=WC3_RECORD_REGION;object->category=WC3_RECORD_INSERT|0xca;
    FOR_LOOP(i,48)wc3_records_prepend(map,9*64+9,0,WC3_RECORD_METADATA);
    G_TestMovePathRefresh((point2_t){8,8},(point2_t){10,10});
    T_EQ(G_TestMovePathClass(2,0,4,4),2); /* blocker is record49 */
    wc3_records_prepend(map,9*64+9,0,WC3_RECORD_METADATA);
    G_TestMovePathRefresh((point2_t){8,8},(point2_t){10,10});
    T_EQ(G_TestMovePathClass(2,0,4,4),0); /* blocker is record50 */
    reset_entities();setup_test_world();
}

TEST(wc3_fine_spatial, full_collector_keeps_stamping_eligible_suffix) {
    fine_spatial_world();edict_t *source=fine_spatial_unit(8.5f,9.5f,8),*units[40];
    FOR_LOOP(i,40)units[i]=fine_spatial_unit(9.5f,9.5f,8);
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
    wc3RecordObject_t *newest=wc3_records_owned(map,units[39]-g_edicts);newest->flags=1;
    wc3RecordObject_t *inactive=wc3_records_owned(map,units[38]-g_edicts);inactive->category=0;
    uint32_t old_stamp=inactive->stamp;
    vec2_t goal={304,304};movePathQuery_t query={{&source->s.origin2,&goal,8,2},source,NULL,true};
    edict_t *tokens[32];T_EQ(G_CollectUnitMoveStepBlockers(&query,NULL,tokens),32);
    FOR_LOOP(i,32)T_EQ(tokens[i],units[37-i]);
    T_EQ(inactive->stamp,old_stamp);
    T_EQ(wc3_records_owned(map,units[0]-g_edicts)->stamp,newest->stamp);
    T_EQ(newest->stamp,map->query);
    reset_entities();setup_test_world();
}

/*15cf80 is row-major by coarse cell, then06/80/40/04 lanes;15d0e0
 * visits TL,TR,BR,BL. A terrain80 corner skips that lane's raw traversal. */
TEST(wc3_fine_spatial, hierarchy_rebuild_preserves_lane_and_clockwise_cell_stamp_order) {
    fine_spatial_world();uint8_t terrain[64*64]={0};terrain[8*64+9]=0x80;
    CM_SetupTestPathmap(64,64,terrain);
    wc3RecordObject_t *objects[4];
    static point2_t const corners[]={{8,8},{9,8},{9,9},{8,9}};
    FOR_LOOP(i,4) {
        edict_t *unit=fine_spatial_unit(corners[i].x+.5f,corners[i].y+.5f,8);
        objects[i]=wc3_records_owned(S_GetMoveFineSpatial(),unit-g_edicts);
        objects[i]->flags=WC3_RECORD_REGION;objects[i]->category=WC3_RECORD_INSERT|0xca;
    }
    S_GetMoveFineSpatial()->query=1000;
    G_TestMovePathRefresh((point2_t){8,8},(point2_t){10,10});
    T_EQ(S_GetMoveFineSpatial()->query,1015);
    FOR_LOOP(i,4)T_EQ(objects[i]->stamp,1012+i);
    T_EQ(G_TestMovePathClass(2,0,4,4),1);T_EQ(G_TestMovePathClass(0x80,0,4,4),1);
    T_EQ(G_TestMovePathClass(0x40,0,4,4),1);T_EQ(G_TestMovePathClass(4,0,4,4),0);
    reset_entities();setup_test_world();
}

typedef struct {uint16_t width,height;color32_t map[9];} fineSpatialTexture_t;
static fineSpatialTexture_t fine_spatial_widget_texture={3,3,{
    {255,0,255,255},{255,0,255,255},{255,0,255,255},
    {255,0,255,255},{255,0,255,255},{255,0,255,255},
    {255,0,255,255},{255,0,255,255},{255,0,255,255}}};
static edict_t *fine_spatial_widget(void) {
    edict_t *widget=G_Spawn();widget->class_id=MAKEFOURCC('L','T','c','r');
    widget->s.origin2=(vec2_t){208,208};widget->s.model=1;
    widget->pathtex=(pathTex_t *)&fine_spatial_widget_texture;
    CM_BakeStaticObstacles();return widget;
}

/* FOOT-03.2 original collection producer: C2/10/08, nine pixels each.
 * A unit remains a separate CA identity. Static pixels are not terrain. */
TEST(wc3_fine_spatial, widget_and_unit_keep_distinct_categories_and_collector_order) {
    FOR_LOOP(order,2) {
        fine_spatial_world();edict_t *unit,*widget;
        if(order){widget=fine_spatial_widget();unit=fine_spatial_unit(7,7,31);}
        else {unit=fine_spatial_unit(7,7,31);widget=fine_spatial_widget();}
        wc3SpatialRecords_t *map=S_GetMoveFineSpatial();T_EQ(map->records,31);
        unsigned count=0;FOR_LOOP(i,map->raw_objects) {
            wc3RecordObject_t *object=wc3_records_object(map,i);
            if(!(object->flags&WC3_RECORD_REGION))continue;
            T_EQ(object->owner,widget-g_edicts);T_EQ(object->refs,9);count++;
        }
        T_EQ(count,3);
        edict_t *source=alloc_test_unit(MAKEFOURCC('h','f','o','o'),176,208);
        source->collision=0;vec2_t goal={208,208};
        movePathQuery_t query={{&source->s.origin2,&goal,0,2},source,NULL,true};
        edict_t *tokens[32];T_EQ(G_CollectUnitMoveStepBlockers(&query,NULL,tokens),2);
        T_EQ(tokens[0],order ? unit : NULL);T_EQ(tokens[1],order ? NULL : unit);
        query.geometry.blocked_flags=0x10;
        T_ASSERT(!G_UnitMovePathFinePointIsPathable(&query,(float[]){6.5f,6.5f}));
        G_RemoveMoveSpatialObject(widget);widget->pathtex=NULL;CM_BakeStaticObstacles();
        T_EQ(map->records,31); /* Region retirement adds no removal records. */
        T_ASSERT(G_UnitMovePathFinePointIsPathable(&query,(float[]){6.5f,6.5f}));
        T_EQ(G_TestMovePathClass(2,0,3,3),0); /* Unit alone never blocks hierarchy. */
        G_RemoveMoveSpatialObject(unit);T_EQ(map->records,35);
        wc3_records_compact(map,false);T_EQ(map->records,15);
        S_CompactMoveFineSpatial();T_EQ(map->records,0);
        reset_entities();setup_test_world();
    }
}

/* Frozen FOOT-03.2: all16 insertion/removal combinations exercise the actual
 * producer and distinct retire/inverse-raster paths, not fabricated flags. */
TEST(wc3_fine_spatial, mixed_region_lifetimes_preserve_exact_reference_counts) {
    FOR_LOOP(first,2)FOR_LOOP(move,2)FOR_LOOP(unraster,2)FOR_LOOP(remove_first,2) {
        fine_spatial_world();edict_t *unit,*widget;
        if(first){widget=fine_spatial_widget();unit=fine_spatial_unit(7,7,31);}
        else {unit=fine_spatial_unit(7,7,31);widget=fine_spatial_widget();}
        wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
        wc3RecordObject_t *a=wc3_records_owned(map,unit-g_edicts),*b[3];
        wc3RegionCollection_t const *collection=S_GetMoveRegions(widget-g_edicts);
        T_EQ(collection->count,3);
        if(collection->count!=3){reset_entities();setup_test_world();continue;}
        FOR_LOOP(i,3)b[i]=wc3_records_object(map,collection->objects[i]);
        FOR_LOOP(removal,2) {
            if(removal==remove_first) {
                if(move){unit->s.origin2=(vec2_t){384,384};G_PublishMoveSpatialObject(unit);}
                else G_RemoveMoveSpatialObject(unit);
                T_EQ(a->refs,move ? 12 : 8);
            } else {
                if(unraster)S_UnrasterMoveRegions(widget);else S_RetireMoveRegions(widget);
                FOR_LOOP(i,3){T_EQ(b[i]->refs,unraster ? 18 : 9);T_EQ(b[i]->stamp==UINT32_MAX,!unraster);}
            }
        }
        T_EQ(map->records,31+(move ? 8 : 4)+(unraster ? 27 : 0));
        wc3_records_compact(map,false);T_EQ(map->records,(unraster ? 0 : 15)+(move ? 4 : 0));
        T_EQ(a->refs,move ? 4 : 0);FOR_LOOP(i,3)T_EQ(b[i]->refs,unraster ? 0 : 5);
        S_CompactMoveFineSpatial();T_EQ(map->records,move ? 4 : 0);
        FOR_LOOP(i,3)T_EQ(b[i]->refs,0);
        reset_entities();setup_test_world();
    }
}

TEST(wc3_fine_spatial, fractional_widget_move_replaces_pixels_within_same_owner_cell) {
    fine_spatial_world();
    struct {uint16_t width,height;color32_t map[2];} texture={2,1,{{255,0,255,255},{255,0,255,255}}};
    edict_t *widget=G_Spawn();widget->pathtex=(pathTex_t *)&texture;
    widget->s.origin2=(vec2_t){264,208};CM_BakeStaticObstacles();
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();T_EQ(map->records,6);
    widget->s.origin2.x=272;CM_BakeStaticObstacles();T_EQ(map->records,18);
    wc3CellQuery_t query={.mode=WC3_CELL_FINE,.mask=0x02000002,.target=WC3_RECORD_END};
    T_EQ(wc3_records_cell(map,(wc3FinePoint_t){7,6},0,&query).value,1);
    T_EQ(wc3_records_cell(map,(wc3FinePoint_t){9,6},0,&query).value,0);
    S_CompactMoveFineSpatial();T_EQ(map->records,6);
    reset_entities();setup_test_world();
}

TEST(wc3_fine_spatial, flight_query_stamps_active_ordinary_identity_before_category_miss) {
    fine_spatial_world();edict_t *source=fine_spatial_unit(8.5f,9.5f,8);
    source->aiflags|=AI_FLYING;edict_t *unit=fine_spatial_unit(9.5f,9.5f,8);
    vec2_t goal={304,304};movePathQuery_t query={{&source->s.origin2,&goal,8,4},source,NULL,true};
    wc3SpatialRecords_t *map=S_GetMoveFineSpatial();wc3RecordObject_t *object=wc3_records_owned(map,unit-g_edicts);
    object->stamp=100;map->query=1000;
    T_ASSERT(G_UnitMovePathFinePointIsPathable(&query,(float[]){9.5f,9.5f}));
    T_EQ(map->query,1001);T_EQ(object->stamp,1001);
    reset_entities();setup_test_world();
}
TEST(wc3_fine_spatial, widget_region_bounds_match_original_producer_and_cached_restore) {
    FOR_LOOP(i,sizeof(region_bounds230)/sizeof(*region_bounds230)) {
        typeof(*region_bounds230) const *row=region_bounds230+i;
        fine_spatial_world();
        CM_SetupTestWorldBounds(&(box2_t){{row->origin[0],row->origin[1]},
            {row->origin[0]+2048,row->origin[1]+2048}});
        struct {uint16_t width,height;color32_t map[81];} texture={row->width,row->height};
        FOR_LOOP(p,row->width*row->height)texture.map[p]=(color32_t){255,255,255,255};
        edict_t *widget=G_Spawn();widget->pathtex=(pathTex_t *)&texture;
        widget->s.origin2=(vec2_t){row->point[0],row->point[1]};
        /* Use the actual destructable orientation producer, without snapping
         * this supplied point:0642f0 consumes the already captured centre. */
        widget->destructable=G_AllocDestructable();
        widget->s.angle=((int)row->turn-(row->width!=row->height))*0x1.921fb6p0f;
        S_PublishMoveRegions(widget);
        wc3SpatialRecords_t *map=S_GetMoveFineSpatial();
        wc3RegionCollection_t const *regions=S_GetMoveRegions(widget-g_edicts);
        T_EQ(regions->count,4);
        moveRegionSave_t saved;S_GetMoveRegionState(widget-g_edicts,&saved);
        uint8_t pixels[81];memcpy(pixels,saved.pixels,row->width*row->height);saved.pixels=pixels;
        for(unsigned pass=0;pass<2;pass++) {
            if(pass) {
                S_RetireMoveRegions(widget);
                T_ASSERT(S_LoadMoveRegions(widget-g_edicts,4,&saved));
            }
            FOR_LOOP(slot,regions->count) {
                wc3RecordObject_t const *record=wc3_records_object(map,regions->objects[slot]);
                T_EQ(record->box.min.y,row->box[0]);T_EQ(record->box.min.x,row->box[1]);
                T_EQ(record->box.max.y,row->box[2]);T_EQ(record->box.max.x,row->box[3]);
            }
        }
        widget->pathtex=NULL;reset_entities();setup_test_world();
    }
}
#endif
