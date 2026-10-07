#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../../common/wc3_pathing_records.h"

extern void reset_entities(void);
extern void setup_test_world(void);
extern edict_t *alloc_test_unit(uint32_t, float, float);
extern void CM_SetupTestPathmap(unsigned, unsigned, uint8_t const *);
extern void CM_SetupTestWorldBounds(box2_t const *);
extern bool run_test_jass(char const *);

static void spatial_load_world(void) {
    reset_entities(); setup_test_world();
    uint8_t cells[64*64]={0};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    CM_SetupTestPathmap(64,64,cells);
}

static edict_t *spatial_load_unit(float x, float y, float radius) {
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),x*32,y*32);
    unit->movetype=MOVETYPE_STEP; unit->stand=unit_stand; unit->die=unit_die;
    unit->svflags|=SVF_MONSTER; unit->s.model=1; unit->collision=radius;
    unit_stand(unit); gi.LinkEntity(unit); G_PublishMoveSpatialObject(unit);
    return unit;
}

/* Retail MAP-06.2: save/load emits objects in saved order, each at the cell
 * head. Movement can have changed the old order; it is deliberately rebuilt. */
TEST(wc3_spatial_load, loaded_blocker_chain_uses_save_order) {
    spatial_load_world();
    edict_t *self=spatial_load_unit(16.5f,16.5f,8), *peers[4];
    FOR_LOOP(i,4)peers[i]=spatial_load_unit(17.5f,16.5f,8);
    peers[0]->s.origin2.x=25.5f*32; G_PublishMoveSpatialObject(peers[0]);
    peers[0]->s.origin2.x=17.5f*32; G_PublishMoveSpatialObject(peers[0]);
    vec2_t source={16.5f,16.5f},goal={19.5f,16.5f};
    movePathQuery_t query={{&self->s.origin2,&goal,8,2},self,NULL,true,&source};
    edict_t *items[32];float endpoint[]={19.5f,16.5f};
    T_EQ(G_CollectUnitMoveStepBlockers(&query,endpoint,items),4);
    T_EQ(items[0],peers[0]);T_EQ(items[1],peers[3]);
    T_EQ(items[2],peers[2]);T_EQ(items[3],peers[1]);
    uint64_t rank=G_GetMoveSpatialSerial();
    wc3Random_t random=level.pathing_random;
    cstring_t file="/tmp/wc3-spatial-save-order.bin";
    T_ASSERT(WriteGame(file));T_EQ(G_GetMoveSpatialSerial(),rank);
    T_ASSERT(ReadGame(file));T_EQ(G_GetMoveSpatialSerial(),5);
    T_ASSERT(!memcmp(&level.pathing_random,&random,sizeof(random)));
    T_EQ(G_CollectUnitMoveStepBlockers(&query,endpoint,items),4);
    FOR_LOOP(i,4)T_EQ(items[i],peers[3-i]);
    /* Re-linking unchanged geometry must preserve the newly rebuilt order. */
    FOR_LOOP(i,4)gi.LinkEntity(peers[i]);
    T_EQ(G_CollectUnitMoveStepBlockers(&query,endpoint,items),4);
    FOR_LOOP(i,4)T_EQ(items[i],peers[3-i]);
    remove(file);reset_entities();setup_test_world();
}

/* The original fine query observes target identity only before its first
 * foreign blocker. Loading must use the rebuilt raw chain for this consumer. */
TEST(wc3_spatial_load, target_observation_changes_with_rebuilt_cell_order) {
    spatial_load_world();
    edict_t *self=spatial_load_unit(12.5f,10.5f,8);
    edict_t *target=spatial_load_unit(17.5f,10.5f,8);
    edict_t *blocker=spatial_load_unit(17.5f,10.5f,8);
    target->s.origin2.x=25.5f*32;G_PublishMoveSpatialObject(target);
    target->s.origin2.x=17.5f*32;G_PublishMoveSpatialObject(target);
    vec2_t goal={17.5f*32,10.5f*32};
    movePathQuery_t query={{&self->s.origin2,&goal,8,2},self,target,true};
    bool hit=false;
    uint8_t edges=G_TestMoveFineEdges(&query,(point2_t){16,10},false,&hit);
    T_ASSERT(hit);
    cstring_t file="/tmp/wc3-spatial-load-target-order.bin";
    T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));hit=false;
    T_EQ(G_TestMoveFineEdges(&query,(point2_t){16,10},false,&hit),edges);
    T_ASSERT(!hit);
    /* A real subsequent leave/reentry, unlike an unchanged presentation
     * link, publishes fresh order and makes the target observable again. */
    target->s.origin2.x=25.5f*32;G_PublishMoveSpatialObject(target);
    target->s.origin2.x=17.5f*32;G_PublishMoveSpatialObject(target);
    hit=false;T_EQ(G_TestMoveFineEdges(&query,(point2_t){16,10},true,&hit),edges);
    T_ASSERT(hit);T_ASSERT(blocker->inuse);
    remove(file);reset_entities();setup_test_world();
}

/* Saved rectangles may be partly outside the map. Preserve their logical
 * coordinates while clipping only cell memberships; do not admit a new pose. */
TEST(wc3_spatial_load, clipped_rectangles_keep_pose_and_rebuild_raw_membership) {
    float const radii[]={8,16,32,48};
    FOR_LOOP(cls,4) {
        spatial_load_world();
        edict_t *unit=spatial_load_unit(.25f,10.75f,radii[cls]);
        if(cls==3)unit->aiflags|=AI_FLYING;
        unit->s.origin2.x=32.25f;G_PublishMoveSpatialObject(unit);
        wc3RecordObject_t before=*G_GetMoveSpatialObject(unit->s.number);
        vec2_t point=unit->s.origin2;
        cstring_t file="/tmp/wc3-spatial-load-clipped.bin";
        T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));
        wc3RecordObject_t const *after=G_GetMoveSpatialObject(unit->s.number);
        T_ASSERT(!memcmp(&after->box,&before.box,sizeof(before.box)));
        T_EQ(wc3_float_bits(unit->s.origin2.x),wc3_float_bits(point.x));
        T_EQ(wc3_float_bits(unit->s.origin2.y),wc3_float_bits(point.y));
        for(int y=after->box.min.y;y<after->box.max.y;y++)
            for(int x=after->box.min.x;x<after->box.max.x;x++)
                T_ASSERT(wc3_records_contains(after,(wc3FinePoint_t){x,y}));
        remove(file);
    }
    reset_entities();setup_test_world();
}

/* Four actual public Move orders: compare every frame's pose, velocity,
 * route cursor, wait state, repulsion and shared RNG after three active saves.
 * This is engine preservation evidence; the separate full retail captures
 * certify the four-mover UI-load suffix, including its rebuilt cell chains. */
typedef struct { uint32_t units[4][16],random[2],time,clock,phase; } spatialLoadFrame_t;
static void spatial_load_frame(spatialLoadFrame_t *frame, edict_t *const units[4]) {
    *frame=(spatialLoadFrame_t){.random={level.pathing_random.sum,level.pathing_random.index},
        .time=level.time,.clock=wc3_float_bits(level.pathing_clock.time),.phase=level.repulse_phase};
    FOR_LOOP(i,4) {
        edict_t const *unit=units[i];
        uint32_t row[]={wc3_float_bits(unit->s.origin2.x),wc3_float_bits(unit->s.origin2.y),
            wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
            wc3_float_bits(unit->movement.velocity.x),wc3_float_bits(unit->movement.velocity.y),
            wc3_float_bits(unit->s.angle),unit->current_order_id,
            unit->movement.fine_route.count,unit->movement.fine_route.index,
            unit->movement.fine_route.adaptive_count,unit->movement.fine_route.adaptive_index,
            unit->movement.wait_delay,unit->movement.repulse.state.packed,
            wc3_float_bits(unit->movement.repulse.state.vector[0]),wc3_float_bits(unit->movement.repulse.state.vector[1])};
        memcpy(frame->units[i],row,sizeof(row));
    }
}

TEST(wc3_spatial_load, four_active_routes_resume_word_identically) {
    FOR_LOOP(i,level.num_timers)G_TimerDestroy(level.timers+i);
    spatial_load_world();
    float min=game.constants.minUnitSpeed,max=game.constants.maxUnitSpeed;
    game.constants.minUnitSpeed=80;game.constants.maxUnitSpeed=400;
    level.waypoints=(typeof(level.waypoints)){0};level.pathing_clock=(wc3Clock_t){0,0,300};
    level.time=level.pathing_msec=0;level.pathing_phase=0;level.pathing_due=false;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "local unit u\nlocal integer i=0\nloop\nexitwhen i==4\n"
        "set u=CreateUnit(Player(0),'hfoo',272+I2R(i)*384,304,90)\n"
        "call SetUnitMoveSpeed(u,80)\ncall IssuePointOrder(u,\"move\",272+I2R(i)*384,1776)\n"
        "set i=i+1\nendloop\nendfunction\n"));
    edict_t *units[4]={0};unsigned count=0;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o'))if(count<4)units[count++]=ent;
    T_EQ(count,4);if(count!=4)goto done;
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    enum { FRAMES=2200 };
    spatialLoadFrame_t *reference=calloc(FRAMES,sizeof(*reference));
    T_NOT_NULL(reference);if(!reference)goto done;
    unsigned const times[]={2000,6000,12000};
    cstring_t files[]={"/tmp/wc3-four-route-2000.bin","/tmp/wc3-four-route-6000.bin","/tmp/wc3-four-route-12000.bin"};
    FOR_LOOP(pass,4) {
        if(pass)T_ASSERT(ReadGame(files[pass-1]));
        unsigned start=level.time/10;
        FOR_LOOP(n,FRAMES-start) {
            unsigned frame=start+n;
            level.time+=10;globals.RunFrame();
            spatialLoadFrame_t actual;spatial_load_frame(&actual,units);
            if(!pass)reference[frame]=actual;
            else {
                FOR_LOOP(i,4)FOR_LOOP(k,16)T_EQ(actual.units[i][k],reference[frame].units[i][k]);
                FOR_LOOP(i,2)T_EQ(actual.random[i],reference[frame].random[i]);
                T_EQ(actual.time,reference[frame].time);T_EQ(actual.clock,reference[frame].clock);
                T_EQ(actual.phase,reference[frame].phase);
            }
            if(!pass)FOR_LOOP(i,3)if(level.time==times[i]) {
                FOR_LOOP(j,4)T_EQ(units[j]->current_order_id,G_OrderId("move"));
                T_ASSERT(WriteGame(files[i]));
            }
        }
        FOR_LOOP(i,4)T_EQ(units[i]->current_order_id,0);
    }
    FOR_LOOP(i,3)remove(files[i]);
    free(reference);
done:
    level.started=false;game.constants.minUnitSpeed=min;game.constants.maxUnitSpeed=max;
    reset_entities();setup_test_world();
}
#endif
