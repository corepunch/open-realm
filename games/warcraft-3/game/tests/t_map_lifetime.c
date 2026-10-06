#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

extern void reset_entities(void);
extern void setup_test_world(void);
extern void CM_SetupTestWorldBounds(box2_t const *);
extern bool run_test_jass(char const *);

static struct {
    bool pending;
    unsigned count, observations;
    war3map_t const *map;
} map_lifetime_release;

/* The first loader yield precedes CM_LoadMapFormat's destruction of the old
 * world. Inspect the actual LoadMap boundary, with every old owner still
 * addressable; this does not replace its loader or movement implementation. */
static void map_lifetime_loading(void) {
    if (!map_lifetime_release.pending) return;
    map_lifetime_release.pending=false;
    map_lifetime_release.observations++;
    T_EQ(world.map,map_lifetime_release.map);
    T_NULL(level.vm);T_NULL(level.move_groups);
    T_EQ(level.move_groups_count,0);T_EQ(G_GetMoveSpatialSerial(),0);
    T_EQ(globals.num_edicts,game.max_clients);
    FOR_LOOP(i,map_lifetime_release.count) {
        edict_t const *unit=g_edicts+i;
        T_ASSERT(!unit->inuse);T_EQ(unit->current_order_id,0);
        T_NULL(unit->movement.fine_route.points);
        T_NULL(unit->movement.fine_route.adaptive_points);
        T_NULL(unit->movement.fine_route.group_points);
    }
    cmPathJobStatus_t status;CM_GetPathJobStatus(&status);
    T_ASSERT(!status.active);T_EQ(status.pending_jobs,0);
    T_EQ(status.pending_cells,0);
}

typedef struct { uint32_t words[23]; } mapLifetimeFrame_t;
static mapLifetimeFrame_t map_lifetime_frame(edict_t const *unit) {
    uint32_t words[]={wc3_float_bits(unit->s.origin2.x),wc3_float_bits(unit->s.origin2.y),
        wc3_float_bits(unit->movement.fine_pose.x),wc3_float_bits(unit->movement.fine_pose.y),
        wc3_float_bits(unit->movement.velocity.x),wc3_float_bits(unit->movement.velocity.y),
        wc3_float_bits(unit->s.angle),unit->current_order_id,
        unit->movement.fine_route.count,unit->movement.fine_route.index,
        unit->movement.fine_route.adaptive_count,unit->movement.fine_route.adaptive_index,
        unit->movement.fine_route.group_count,unit->movement.fine_route.group_index,
        unit->movement.wait_delay,unit->movement.repulse.state.packed,
        wc3_float_bits(unit->movement.repulse.state.vector[0]),wc3_float_bits(unit->movement.repulse.state.vector[1]),
        level.pathing_random.sum,level.pathing_random.index,level.repulse_phase,
        wc3_float_bits(level.pathing_clock.time),level.pathing_counter};
    mapLifetimeFrame_t frame;memcpy(frame.words,words,sizeof(words));return frame;
}

TEST(wc3_map_lifetime, actual_reload_retires_live_routes_before_world_and_rows) {
    reset_entities();setup_test_world();
    /* setup_test_world owns static terrain. Detach it before the real map
     * loader starts owning heap terrain; use the native transform from now on. */
    world.map=NULL;CM_SetupTestWorldBounds(NULL);
    __typeof__(gi.LoadingFrame) loading=gi.LoadingFrame;
    gi.LoadingFrame=map_lifetime_loading;
    map_lifetime_release=(typeof(map_lifetime_release)){0};
    G_SetPathWorkerEnabled(false);
    enum { FRAMES=200 };
    mapLifetimeFrame_t reference[2][FRAMES];
    FOR_LOOP(pass,11) {
        /* First load, eight restarts, then a different-sized level twice. */
        unsigned shape=pass>=9;
        T_ASSERT(globals.LoadMap(shape ? "Maps/Test/PathingChangeLevel.w3m" : "Maps/Test/PathingReload.w3m"));
        T_ASSERT(!map_lifetime_release.pending);
        T_EQ(world.map->width,shape ? 25 : 17);T_EQ(world.map->height,17);
        T_EQ(level.time,0);T_EQ(level.pathing_clock.span,300);
        T_ASSERT(level.scriptsStarted);T_NOT_NULL(level.vm);
        edict_t *unit=NULL;unsigned count=0;
        FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o')) {unit=ent;count++;}
        T_EQ(count,1);if(count!=1)break;
        T_EQ(unit->current_order_id,G_OrderId("move"));
        float start=unit->s.origin2.y;
        FOR_LOOP(frame,FRAMES) {
            level.time+=10;globals.RunFrame();
            mapLifetimeFrame_t actual=map_lifetime_frame(unit);
            if(pass==0 || pass==9)reference[shape][frame]=actual;
            else FOR_LOOP(k,23)T_EQ(actual.words[k],reference[shape][frame].words[k]);
        }
        T_ASSERT(unit->s.origin2.y>start);T_EQ(unit->current_order_id,G_OrderId("move"));
        T_ASSERT(unit->movement.fine_route.points || unit->movement.fine_route.adaptive_points ||
                 unit->movement.fine_route.group_points);
        /* These actors occupy slots absent from the next map's main(). Their
         * routes must be freed rather than forgotten by resetting num_edicts. */
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
            "local unit u\nlocal integer i=0\nloop\nexitwhen i==12\n"
            "set u=CreateUnit(Player(0),'hfoo',304+I2R(i)*80,336,0)\n"
            "call IssuePointOrder(u,\"move\",1000+I2R(i)*32,1600)\n"
            "set i=i+1\nendloop\nendfunction\n"));
        edict_t *goal=G_Spawn();goal->s.origin2=(vec2_t){1776,1648};
        goal->s.origin=G_MakeServerOrigin(1776,1648,0);
        G_SetPathWorkerEnabled(((pass+1)&1)!=0);
        T_EQ(CM_RequestHeatmapForMoverFlags(unit,goal,0,CM_PATHING_UNWALKABLE),0);
        cmPathJobStatus_t status;CM_GetPathJobStatus(&status);T_ASSERT(status.active);
        /* Leave a frontier owned by the executor. LoadMap must join it before
         * releasing the borrowed geometry, on worker and inline runs alike. */
        CM_BeginPathJobs(64);
        map_lifetime_release.pending=true;
        map_lifetime_release.count=globals.num_edicts;
        map_lifetime_release.map=world.map;
    }
    T_EQ(map_lifetime_release.observations,10);
    G_ReleaseLevel();map_lifetime_loading();
    gi.LoadingFrame=loading;G_SetPathWorkerEnabled(false);
    /* Retire the loader's archive, metadata and heap terrain before restoring
     * the static fixture. This is the same format-release path used by reload. */
    CM_W3ClearMapData();T_NULL(world.map);
    setup_test_world();reset_entities();
}
#endif
