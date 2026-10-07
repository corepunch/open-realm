#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_separation_crowds.h"
extern void reset_entities(void),setup_test_world(void);
extern bool run_test_jass(char const *);
extern void (*test_preload_marker)(cstring_t);
extern void CM_SetupTestPathmap(unsigned,unsigned,uint8_t const *);
extern void CM_SetupTestWorldBounds(box2_t const *);

static struct {
    edict_t *units[10]; unsigned unit_count,visit,pair,retry;
    crowdVisit_t const *visits; unsigned visit_count;
    uint32_t const (*pairs)[14]; unsigned pair_count;
    crowdRetry_t const *retries; unsigned retry_count;
    bool failed,endpoint,ground; unsigned sample;
} crowd_trace;

static unsigned crowd_member(edict_t const *unit) {
    FOR_LOOP(i,crowd_trace.unit_count)if(crowd_trace.units[i]==unit)return i;
    return UINT32_MAX;
}

static void crowd_words(uint32_t const *actual,uint32_t const *expected,unsigned count,char const *kind) {
    if(crowd_trace.failed)return;
    FOR_LOOP(i,count) {
        T_EQ(actual[i],expected[i]);
        if(actual[i]!=expected[i]) {
            fprintf(stderr,"Crowd %s field%u at%u visit%u pair%u retry%u: %08x != %08x\n",
                kind,i,level.time,crowd_trace.visit,crowd_trace.pair,crowd_trace.retry,actual[i],expected[i]);
            crowd_trace.failed=true;return;
        }
    }
}

static void crowd_endpoint(edict_t const *unit,float const point[2],bool attempted,bool admitted) {
    if(crowd_trace.failed)return;
    T_ASSERT(crowd_trace.visit<crowd_trace.visit_count);
    if(crowd_trace.visit>=crowd_trace.visit_count){crowd_trace.failed=true;return;}
    crowdVisit_t const *row=crowd_trace.visits+crowd_trace.visit;
    uint32_t actual[]={crowd_member(unit),crowd_trace.endpoint,attempted,admitted};
    uint32_t expected[]={row->unit,0,row->admission>=0,row->admission==1};
    crowd_words(actual,expected,4,"endpoint");crowd_trace.endpoint=true;
    if(attempted){uint32_t p[]={wc3_float_bits(point[0]),wc3_float_bits(point[1])};crowd_words(p,row->endpoint,2,"endpoint position");}
}

static void crowd_pair(edict_t const *unit,edict_t const *other,wc3RepulsePair_t const *pair,
    wc3Repulse_t const *before,wc3Random_t random_before) {
    if(crowd_trace.failed)return;
    T_ASSERT(crowd_trace.pair<crowd_trace.pair_count);
    if(crowd_trace.pair>=crowd_trace.pair_count){crowd_trace.failed=true;return;}
    uint32_t actual[]={crowd_member(unit),crowd_member(other),wc3_float_bits(pair->source[0]),wc3_float_bits(pair->source[1]),
        wc3_float_bits(pair->other[0]),wc3_float_bits(pair->other[1]),wc3_float_bits(before->vector[0]),wc3_float_bits(before->vector[1]),
        wc3_float_bits(unit->movement.repulse.state.vector[0]),wc3_float_bits(unit->movement.repulse.state.vector[1]),
        random_before.sum,random_before.index,level.pathing_random.sum,level.pathing_random.index};
    crowd_words(actual,crowd_trace.pairs[crowd_trace.pair++],14,"pair");
}

static void crowd_visit(edict_t *unit,moveRepulseTrace_t const *before) {
    if(crowd_trace.failed)return;
    T_ASSERT(crowd_trace.visit<crowd_trace.visit_count);
    if(crowd_trace.visit>=crowd_trace.visit_count){crowd_trace.failed=true;return;}
    crowdVisit_t const *row=crowd_trace.visits+crowd_trace.visit;
    uint32_t identity[]={crowd_member(unit),level.pathing_counter,crowd_trace.endpoint};
    uint32_t expected[]={row->unit,row->visit,row->admission!=-2};
    crowd_words(identity,expected,3,"owner");
    uint32_t previous[]={wc3_float_bits(before->point.x),wc3_float_bits(before->point.y),
        wc3_float_bits(before->state.vector[0]),wc3_float_bits(before->state.vector[1]),before->state.packed};
    uint32_t actual[]={wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
        wc3_float_bits(unit->movement.repulse.state.vector[0]),wc3_float_bits(unit->movement.repulse.state.vector[1]),unit->movement.repulse.state.packed};
    crowd_words(previous,row->before,5,"before");crowd_words(actual,row->after,5,"after");
    uint32_t random[]={before->owner.sum,before->owner.index,level.pathing_random.sum,level.pathing_random.index};
    uint32_t wanted[]={row->random_before[0],row->random_before[1],row->random_after[0],row->random_after[1]};
    crowd_words(random,wanted,4,"random");
    wc3FineBox_t box=G_GetMoveSpatialObject(unit-g_edicts)->box;
    uint32_t rectangle[]={box.min.y,box.min.x,box.max.y,box.max.x};
    crowd_words(rectangle,(uint32_t const *)row->box,4,"occupancy");
    crowd_trace.visit++;crowd_trace.endpoint=false;
}

static void crowd_retry(edict_t *unit,moveRetryTrace_t const *before) {
    if(crowd_trace.failed)return;
    T_ASSERT(crowd_trace.retry<crowd_trace.retry_count);
    if(crowd_trace.retry>=crowd_trace.retry_count){crowd_trace.failed=true;return;}
    crowdRetry_t const *row=crowd_trace.retries+crowd_trace.retry++;
    uint32_t actual[]={crowd_member(unit),level.pathing_counter,before->count,unit->movement.retry_count,before->result,
        before->owner.sum,before->owner.index,level.pathing_random.sum,level.pathing_random.index};
    uint32_t expected[]={row->unit,row->visit,row->before,row->after,row->result,
        row->random_before[0],row->random_before[1],row->random_after[0],row->random_after[1]};
    crowd_words(actual,expected,9,"retry");
    if(crowd_trace.failed) {
        moveFineRoute_t const *route=&unit->movement.fine_route;
        fprintf(stderr,"Retry input unit%u source %.9g/%.9g goal %.9g/%.9g members%u adaptive%u/%u goal %.9g/%.9g first %.9g/%.9g\n",
            row->unit,before->input.source[0],before->input.source[1],before->input.goal[0],before->input.goal[1],before->input.members,
            route->adaptive_index,route->adaptive_count,route->adaptive_goal.x,route->adaptive_goal.y,
            route->adaptive_count?route->adaptive_points[0].x:0,route->adaptive_count?route->adaptive_points[0].y:0);
    }
}

/* Public JASS output also covers the units with no separation object. Retail
 * printed four decimals; exact owner words above provide the numerical test. */
static void crowd_sample(cstring_t marker) {
    unsigned tick,unit,order,owner;float x,y;
    if(crowd_trace.failed || sscanf(marker,"PATHSEP tick=%u label=s i=%u x=%f y=%f o=%u p=%u",&tick,&unit,&x,&y,&order,&owner)!=6)return;
    unsigned count=crowd_trace.ground ? sizeof(ground_samples)/sizeof(*ground_samples) : sizeof(crowd_samples)/sizeof(*crowd_samples);
    T_ASSERT(crowd_trace.sample<count);
    if(crowd_trace.sample>=count){crowd_trace.failed=true;return;}
    typeof(crowd_samples[0]) const *expected=crowd_trace.ground ?
        (typeof(crowd_samples[0]) const *)ground_samples+crowd_trace.sample : crowd_samples+crowd_trace.sample;
    crowd_trace.sample++;
    uint32_t actual[]={tick,unit,order,owner},wanted[]={expected->tick,expected->unit,expected->order,expected->owner};
    crowd_words(actual,wanted,4,"public order");
    bool match=fabsf(x-expected->x)<=.00015f && fabsf(y-expected->y)<=.00015f;
    T_ASSERT(match);
    if(!match){fprintf(stderr,"Public sample unit%u tick%u %.4f/%.4f != %.4f/%.4f\n",unit,tick,x,y,expected->x,expected->y);crowd_trace.failed=true;}
}

static void crowd_journey(bool ground,bool saved) {
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    reset_entities();setup_test_world();G_FreeMovePathCache();
    char const *codes[]={"hS00","hSC1","hSC2","hSR1","hSR2","hS02","hS04","hSD0","hSD1"};
    float radii[]={8,16,32,8,8,8,8,8,16},speed=270;
    int enabled[]={1,1,1,1,1,1,1,0,0},rank[]={0,0,0,1,2,0,0,0,0},selector[]={0,0,0,0,0,2,4,0,0};
    unitModification_t mods[9][5];unitData_t types[9];
    FOR_LOOP(i,9) {
        mods[i][0]=(unitModification_t){.modID=MAKEFOURCC('u','c','o','l'),.type=mod_unreal,.data=radii+i};
        mods[i][1]=(unitModification_t){.modID=MAKEFOURCC('u','m','v','s'),.type=mod_real,.data=&speed};
        mods[i][2]=(unitModification_t){.modID=MAKEFOURCC('u','r','p','o'),.type=mod_int,.data=enabled+i};
        mods[i][3]=(unitModification_t){.modID=MAKEFOURCC('u','r','p','r'),.type=mod_int,.data=rank+i};
        mods[i][4]=(unitModification_t){.modID=MAKEFOURCC('u','r','p','p'),.type=mod_int,.data=selector+i};
        types[i]=(unitData_t){.originalUnitID=MAKEFOURCC('h','R','T','E'),.newUnitID=MAKEFOURCC(codes[i][0],codes[i][1],codes[i][2],codes[i][3]),.numbeOfModifications=5,.modifications=mods[i]};
    }
    mapInfo_t info={.num_userCreatedUnits=9,.userCreatedUnits=types};
    mapInfo_t const *old_info=level.mapinfo;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
    uint8_t cells[64*64]={0};
    unsigned const (*blocked)[2]=ground ? ground_blocked : crowd_blocked;
    unsigned blocked_count=ground ? sizeof(ground_blocked)/sizeof(*ground_blocked) : sizeof(crowd_blocked)/sizeof(*crowd_blocked);
    FOR_LOOP(i,blocked_count)cells[blocked[i][1]*64+blocked[i][0]]=0xc6;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});CM_SetupTestPathmap(64,64,cells);
    war3mapVertex_t vertices[17*17]={0};FOR_LOOP(i,sizeof(vertices)/sizeof(*vertices)){vertices[i].accurate_height=0x2000;vertices[i].level=2;}
    war3map_t terrain={.width=17,.height=17,.center={0,0},.vertices=vertices};world.map=&terrain;
    float old_min=game.constants.minUnitSpeed,old_max=game.constants.maxUnitSpeed;
    game.constants.minUnitSpeed=150;game.constants.maxUnitSpeed=400;
    level.pathing_clock=(wc3Clock_t){0,0,300};level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    level.pathing_random=(wc3Random_t){.sum=4273436052u,.index=209508436u};
    T_ASSERT(run_test_jass(ground ? ground_script : crowd_script));G_FinishMovePathingInitialization();
    crowd_trace=(typeof(crowd_trace)){.ground=ground,.unit_count=ground ? 8 : 10,
        .visits=ground ? ground_visits : crowd_visits,.visit_count=ground ? sizeof(ground_visits)/sizeof(*ground_visits) : sizeof(crowd_visits)/sizeof(*crowd_visits),
        .pairs=ground ? ground_pairs : crowd_pairs,.pair_count=ground ? sizeof(ground_pairs)/sizeof(*ground_pairs) : sizeof(crowd_pairs)/sizeof(*crowd_pairs),
        .retries=ground ? ground_retries : crowd_retries,.retry_count=ground ? sizeof(ground_retries)/sizeof(*ground_retries) : sizeof(crowd_retries)/sizeof(*crowd_retries)};
    /* Initial map timers precede the owner at the same scalar deadline. */
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    unsigned end=ground ? 20200 : 15200;
    test_preload_marker=crowd_sample;
    cstring_t file=ground ? "/tmp/wc3-ground-crowd.bin" : "/tmp/wc3-mixed-crowd.bin";
    while(level.time<end && !crowd_trace.failed) {
        unsigned found=0;
        FILTER_EDICTS(unit,unit->inuse && (unit->class_id&0xffffu)==MAKEFOURCC('h','S',0,0)) {
            if(found<crowd_trace.unit_count)crowd_trace.units[found++]=unit;
        }
        if(found==crowd_trace.unit_count) {move_test_repulse=crowd_visit;move_test_repulse_pair=crowd_pair;move_test_repulse_endpoint=crowd_endpoint;move_test_retry=crowd_retry;}
        level.time+=5;globals.RunFrame();
        if(saved && level.time==(ground ? 12995u : 6995u) && !crowd_trace.failed) {T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));}
    }
    T_ASSERT(!crowd_trace.failed);T_EQ(crowd_trace.visit,crowd_trace.visit_count);
    T_EQ(crowd_trace.pair,crowd_trace.pair_count);T_EQ(crowd_trace.retry,crowd_trace.retry_count);
    T_EQ(crowd_trace.sample,ground ? sizeof(ground_samples)/sizeof(*ground_samples) : sizeof(crowd_samples)/sizeof(*crowd_samples));
    T_ASSERT(!jass_rterror_pending(level.vm));
    test_preload_marker=NULL;if(saved)remove(file);
    move_test_repulse=NULL;move_test_repulse_pair=NULL;move_test_repulse_endpoint=NULL;move_test_retry=NULL;
    fprintf(stderr,"%s complete owner visits=%u/%u pairs=%u/%u retries=%u/%u\n",ground ? "Ground" : "Crowd",crowd_trace.visit,crowd_trace.visit_count,crowd_trace.pair,crowd_trace.pair_count,crowd_trace.retry,crowd_trace.retry_count);
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    level.started=false;reset_entities();setup_test_world();G_SetMapUnitOverrides(NULL);level.mapinfo=old_info;
    game.constants.minUnitSpeed=old_min;game.constants.maxUnitSpeed=old_max;
}

TEST(wc3_repulsion_crowds, mixed_owner_radius_rank_blocked_endpoint_matches_capture) {crowd_journey(false,false);}
TEST(wc3_repulsion_crowds, disabled_ground_control_matches_retry_and_stop_capture) {crowd_journey(true,false);}
TEST(wc3_repulsion_crowds, mixed_crowd_saved_mid_order_matches_complete_suffix) {crowd_journey(false,true);}
TEST(wc3_repulsion_crowds, ground_control_saved_mid_order_matches_complete_suffix) {crowd_journey(true,true);}
#endif
