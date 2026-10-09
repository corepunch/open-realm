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
/* Compose real MPQ reload, a reentrant producer burst, removal and cold saves.
 * The original component witnesses remain independent; these full engine
 * continuations are compared with the same uninterrupted inputs. */
extern void (*test_preload_marker)(cstring_t);
static uint32_t lifetime213_mover,lifetime213_victim;
static unsigned lifetime213_callbacks;
static void lifetime213_marker(cstring_t text) {
    if(strcmp(text,"L213 after-callback"))return;
    edict_t *unit=g_edicts+lifetime213_mover,*victim=g_edicts+lifetime213_victim;
    lifetime213_callbacks++;
    T_ASSERT(G_IsDeferredFree(victim));T_NULL(victim->order_queue.entries);
    T_EQ(victim->movement.group_id,0);T_NULL(victim->currentmove);
    T_ASSERT(ARRAY_COUNT(level.move_groups)>128);
    T_ASSERT(level.move_group_capacity>=256);
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1400,1200},true,0,0));
    T_ASSERT(G_IssueUnitPointOrder(unit,"move",&(vec2_t){1400,1400},true,0,0));
    T_EQ(unit->order_queue.count,2);
}

static char const lifetime213_script[]=
    "globals\nunit L213Mover=null\nunit L213Victim=null\nendglobals\n"
    "function L213Burst takes nothing returns nothing\nlocal integer i=0\n"
    "call RemoveUnit(L213Victim)\nloop\nexitwhen i==129\n"
    "call BJassAssert(IssuePointOrder(L213Mover,\"move\",1100+I2R(i),1200),\"callback replacement admitted\")\n"
    "set i=i+1\nendloop\ncall Preload(\"L213 after-callback\")\nendfunction\n"
    "function main takes nothing returns nothing\nlocal group g=CreateGroup()\nlocal timer t=CreateTimer()\n"
    "call GroupEnumUnitsOfPlayer(g,Player(0),null)\nset L213Mover=FirstOfGroup(g)\ncall DestroyGroup(g)\n"
    "call BJassAssert(L213Mover!=null,\"loaded map owns moving actor\")\n"
    "call SetUnitUserData(L213Mover,2130)\ncall SetUnitAcquireRange(L213Mover,0)\n"
    "call SetUnitMoveSpeed(L213Mover,320)\n"
    "set L213Victim=CreateUnit(Player(0),'hfoo',768,304,90)\n"
    "call SetUnitUserData(L213Victim,2131)\ncall SetUnitAcquireRange(L213Victim,0)\n"
    "call SetUnitMoveSpeed(L213Victim,160)\ncall IssuePointOrder(L213Victim,\"move\",1552,1776)\n"
    "call TimerStart(t,0.5,false,function L213Burst)\nendfunction\n";

static void lifetime213_install(void) {
    T_ASSERT(run_test_jass(lifetime213_script));
    edict_t *unit=NULL,*victim=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->user_data==2130)unit=ent;
    FILTER_EDICTS(ent,ent->inuse && ent->user_data==2131)victim=ent;
    T_NOT_NULL(unit);T_NOT_NULL(victim);if(!unit || !victim)return;
    /* The minimal SLK has zero collision. Exercise real membership rather
     * than mistaking an intentionally absent object for a load regression. */
    unit->collision=victim->collision=16;
    G_PublishMoveSpatialObject(unit);G_PublishMoveSpatialObject(victim);
    lifetime213_mover=unit->s.number;lifetime213_victim=victim->s.number;
    T_EQ(unit->current_order_id,G_OrderId("move"));
    FOR_LOOP(i,129)T_ASSERT(G_IssueUnitPointOrder(victim,"move",&(vec2_t){1552,1776},true,0,0));
    T_EQ(victim->order_queue.count,129);
    T_ASSERT(victim->order_queue.capacity>UNIT_ORDER_INITIAL_CAPACITY);
}

/* Leave genuinely borrowed work behind the active/idle owner before each
 * loader transition. Inline and threaded executors have the same boundary. */
static void lifetime213_before_reload(bool worker) {
    edict_t *unit=g_edicts+lifetime213_mover,*goal=G_Spawn();
    goal->s.origin2=(vec2_t){1776,1648};goal->s.origin=G_MakeServerOrigin(1776,1648,0);
    G_SetPathWorkerEnabled(worker);
    T_EQ(CM_RequestHeatmapForMoverFlags(unit,goal,0,CM_PATHING_UNWALKABLE),0);
    cmPathJobStatus_t status;CM_GetPathJobStatus(&status);T_ASSERT(status.active);
    CM_BeginPathJobs(64);
    map_lifetime_release.pending=true;
    map_lifetime_release.count=globals.num_edicts;
    map_lifetime_release.map=world.map;
}

typedef struct {mapLifetimeFrame_t mover;uint32_t ownership[7];} lifetime213Frame_t;
static lifetime213Frame_t lifetime213_frame(void) {
    edict_t *unit=g_edicts+lifetime213_mover,*victim=g_edicts+lifetime213_victim;
    unsigned owners=0;
    FOR_LOOP(i,ARRAY_COUNT(level.move_groups))owners+=level.move_groups[i]->inuse;
    return (lifetime213Frame_t){map_lifetime_frame(unit),{
        unit->order_queue.count,victim->order_queue.count,victim->inuse,G_IsDeferredFree(victim),
        owners,level.next_move_group_id,level.timer_sequence}};
}

static void lifetime213_journey(void) {
    enum {FRAMES=1400};
    lifetime213Frame_t *reference=calloc(FRAMES,sizeof(*reference));T_NOT_NULL(reference);if(!reference)return;
    reset_entities();setup_test_world();world.map=NULL;CM_SetupTestWorldBounds(NULL);
    __typeof__(gi.LoadingFrame) loading=gi.LoadingFrame;gi.LoadingFrame=map_lifetime_loading;
    map_lifetime_release=(typeof(map_lifetime_release)){0};
    test_preload_marker=lifetime213_marker;
    cstring_t file=Test_TempPath("wc3-lifetime213.bin");
    FOR_LOOP(shape,2)FOR_LOOP(variant,4) {
        char const *map=shape ? "Maps/Test/PathingChangeLevel.w3m" : "Maps/Test/PathingReload.w3m";
        T_ASSERT(globals.LoadMap(map));T_ASSERT(!map_lifetime_release.pending);
        lifetime213_callbacks=0;lifetime213_install();uint32_t sequence_delta=0;
        FOR_LOOP(frame,FRAMES) {
            level.time+=10;globals.RunFrame();
            lifetime213Frame_t actual=lifetime213_frame();
            if(!variant)reference[frame]=actual;
            else {
                FOR_LOOP(k,23)T_EQ(actual.mover.words[k],reference[frame].mover.words[k]);
                FOR_LOOP(k,6)T_EQ(actual.ownership[k],reference[frame].ownership[k]);
                /* MAP-06.2 rebuilds two recurring spatial requests. Their
                 * fresh serials differ; gameplay and queue words do not. */
                T_EQ(actual.ownership[6],reference[frame].ownership[6]+sequence_delta);
            }
            /* A cold map/VM/allocator graph, not an in-place pointer replay. */
            if((variant==2 && level.time==250) || (variant==3 && level.time==550)) {
                edict_t *unit=g_edicts+lifetime213_mover;
                T_EQ(unit->current_order_id,G_OrderId("move"));
                if(variant==2)T_EQ(g_edicts[lifetime213_victim].order_queue.count,129);
                else {T_EQ(lifetime213_callbacks,1);T_EQ(unit->order_queue.count,2);}
                uint32_t saved_sequence=level.timer_sequence;
                wc3RecordObject_t const *object=G_GetMoveSpatialObject(lifetime213_mover);
                wc3FineBox_t const *proximity=S_GetMoveProximity(lifetime213_mover);
                T_NOT_NULL(object);T_NOT_NULL(proximity);
                wc3FineBox_t fine_box=object ? object->box : (wc3FineBox_t){0};
                wc3FineBox_t proximity_box=proximity ? *proximity : (wc3FineBox_t){0};
                T_ASSERT(WriteGame(file));
                object=G_GetMoveSpatialObject(lifetime213_mover);
                proximity=S_GetMoveProximity(lifetime213_mover);
                T_NOT_NULL(object);T_NOT_NULL(proximity);
                if(object)T_EQ(memcmp(&object->box,&fine_box,sizeof(fine_box)),0);
                if(proximity)T_EQ(memcmp(proximity,&proximity_box,sizeof(proximity_box)),0);
                T_EQ(level.timer_sequence,saved_sequence);
                lifetime213_before_reload(variant==3);
                T_ASSERT(globals.LoadMap(map));lifetime213_install();
                T_ASSERT(ReadGame(file));T_ASSERT(!map_lifetime_release.pending);
                T_EQ(level.timer_sequence,saved_sequence+2);sequence_delta=2;
                wc3Clock_t due;uint32_t serial;T_ASSERT(S_NextMoveSpatialMaintenance(&due,&serial));
                wc3Clock_t clock=G_TimerQueryClock(NULL);
                T_EQ(wc3_float_bits(due.time),wc3_float_bits(wc3_add(clock.time,wc3_div(1,10))));
                T_EQ(due.epoch,clock.epoch);T_EQ(serial,saved_sequence+1);
                object=G_GetMoveSpatialObject(lifetime213_mover);proximity=S_GetMoveProximity(lifetime213_mover);
                T_NOT_NULL(object);T_NOT_NULL(proximity);
                if(object)T_EQ(memcmp(&object->box,&fine_box,sizeof(fine_box)),0);
                if(proximity)T_EQ(memcmp(proximity,&proximity_box,sizeof(proximity_box)),0);
            }
        }
        T_EQ(lifetime213_callbacks,1);
        edict_t *unit=g_edicts+lifetime213_mover;
        T_EQ(unit->current_order_id,0);T_EQ(unit->movement.group_id,0);
        T_NULL(unit->order_queue.entries);T_EQ(unit->order_queue.count,0);
        T_ASSERT(Vector2_distance(&unit->s.origin2,&(vec2_t){1400,1400})<32);
        T_ASSERT(!g_edicts[lifetime213_victim].inuse);
        FOR_LOOP(i,ARRAY_COUNT(level.move_groups))T_ASSERT(!level.move_groups[i]->inuse);
        FOR_LOOP(p,MAX_PLAYERS) {
            T_EQ(level.move_fine_budgets[p].count,0);
            T_NULL(level.move_fine_budgets[p].head);T_NULL(level.move_fine_budgets[p].tail);
        }
        T_ASSERT(S_ValidateMoveCoarseRequests());
        T_ASSERT(!jass_rterror_pending(level.vm));
        lifetime213_before_reload((variant&1)!=0);
    }
    G_ReleaseLevel();map_lifetime_loading();T_EQ(map_lifetime_release.observations,12);
    gi.LoadingFrame=loading;test_preload_marker=NULL;G_SetPathWorkerEnabled(false);
    CM_W3ClearMapData();T_NULL(world.map);setup_test_world();reset_entities();free(reference);remove(file);
}
static void lifetime213_cold_hierarchy(void) {
    reset_entities();setup_test_world();
    uint8_t cells[64*64]={0};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    CM_SetupTestPathmap(64,64,cells);G_FreeMovePathCache();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),304,304);
    unit->collision=8;unit->s.model=1;G_PublishMoveSpatialObject(unit);
    wc3RecordObject_t const *object=G_GetMoveSpatialObject(unit-g_edicts);
    wc3FineBox_t const *proximity=S_GetMoveProximity(unit-g_edicts);
    T_NOT_NULL(object);T_NOT_NULL(proximity);
    if(!object || !proximity)goto done;
    wc3FineBox_t fine_box=object->box,proximity_box=*proximity;
    uint32_t sequence=level.timer_sequence;
    T_ASSERT(G_GetMoveAdaptiveStateSize()>0);
    object=G_GetMoveSpatialObject(unit-g_edicts);proximity=S_GetMoveProximity(unit-g_edicts);
    T_NOT_NULL(object);T_NOT_NULL(proximity);
    if(object)T_EQ(memcmp(&object->box,&fine_box,sizeof(fine_box)),0);
    if(proximity)T_EQ(memcmp(proximity,&proximity_box,sizeof(proximity_box)),0);
    T_EQ(level.timer_sequence,sequence);
done:
    CM_SetupTestPathmap(0,0,NULL);CM_SetupTestWorldBounds(NULL);
    reset_entities();setup_test_world();
}
TEST(wc3_e2e213, real_map_callback_pool_pressure_matches_uninterrupted_and_cold_continuations) {
    FOR_LOOP(repeat,2) {
        e2e_journey(lifetime213_cold_hierarchy);
        e2e_journey(lifetime213_journey);
    }
}
#endif
