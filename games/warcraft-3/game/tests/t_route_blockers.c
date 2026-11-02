#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

extern void reset_entities(void);
extern void setup_test_world(void);
extern edict_t *alloc_test_unit(uint32_t, float, float);
extern void CM_SetupTestPathmap(unsigned, unsigned, uint8_t const *);
extern void CM_SetupTestWorldBounds(box2_t const *);

static void route_blocker_world(void) {
    reset_entities(); setup_test_world();
    uint8_t cells[32*32]={0};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{1024,1024}});
    CM_SetupTestPathmap(32,32,cells);
}

static edict_t *route_blocker_unit(float x, float y, float radius) {
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),x*32,y*32);
    unit->movetype=MOVETYPE_STEP; unit->stand=unit_stand; unit->die=unit_die;
    unit->svflags|=SVF_MONSTER;
    unit->s.model=1; unit->collision=radius;
    unit_stand(unit);
    gi.LinkEntity(unit);
    return unit;
}

/* Frozen ROUTE-02.2 Part F: the selector and collector suppress self only.
 * A target ignored while building the fine route blocks its later consumers. */
TEST(wc3_route_blockers, target_blocks_selection_and_next_step) {
    float const radii[]={8,16,32,48};
    FOR_LOOP(cls,4) {
        route_blocker_world();
        edict_t *self=route_blocker_unit(12.25f,16.75f,radii[cls]);
        edict_t *target=route_blocker_unit(17.5f,16.5f,8);
        vec2_t source={12.25f,16.75f},goal={20.25f,16.75f},out;
        movePathQuery_t query={{&self->s.origin2,&goal,self->collision,2},self,target,true,&source};
        vec2_t points[]={{20.25f,16.75f},{18.25f,16.75f},{16.25f,16.75f},
            {14.25f,16.75f},{12.25f,16.75f}};
        moveFineRoute_t route={.points=points,.count=5,.index=4};
        T_ASSERT(G_AdvanceUnitMoveFineRoute(&query,&route,&out));
        T_EQ(route.index,2);
        /* Move the same target to the class2/3 entering column. */
        target->s.origin2=(vec2_t){14.5f*32,16.5f*32}; gi.LinkEntity(target);
        vec2_t peer_goal={900,600};
        order_move(target,Waypoint_add(&peer_goal));
        target->movement.group_id=2; self->movement.group_id=1;
        self->movement.velocity=(vec2_t){1,0}; target->movement.velocity=(vec2_t){3,0};
        edict_t *items[32]; float fine[]={20.25f,16.75f};
        uint32_t count=G_CollectUnitMoveStepBlockers(&query,fine,items);
        T_EQ(count,cls>=2 ? 1 : 0);
        if (cls>=2) {
            if(count) T_EQ(items[0],target);
            T_EQ(S_ResolveMoveBlockers(self,items,count),WC3_YIELD_SELF);
            T_EQ(self->movement.wait_blocker,target); T_EQ(self->movement.wait_delay,4);
        }
    }
    reset_entities(); setup_test_world();
}

/* Part B: chain position, rather than edict ID, decides which peers survive
 * truncation and whether the resolver assigns a peer before waiting itself. */
TEST(wc3_route_blockers, capped_order_controls_persistent_peer_waits) {
    struct { int slow, fast; bool all_slow; } const cases[]={
        {-1,0,false},{-1,15,false},{-1,31,false},{-1,32,false},{-1,39,false},
        {5,20,false},{20,5,false},{5,32,false},{32,5,false},{31,32,false},
        {-1,-1,true}
    };
    FOR_LOOP(row,sizeof(cases)/sizeof(*cases)) {
        route_blocker_world();
        edict_t *self=route_blocker_unit(16.5f,16.5f,8),*peers[40];
        vec2_t source={16.5f,16.5f},goal={19.5f,16.5f},peer_goal={900,600};
        self->movement.velocity=(vec2_t){1,0}; self->movement.group_id=1;
        /* Reverse allocation/publication creates the frozen stored chain. */
        for(int i=39;i>=0;i--) {
            peers[i]=route_blocker_unit(17.5f,16.5f,8);
            /* These are already admitted peers in the original fixture.
             * Public admission would recover them out of this overlap. */
            order_move(peers[i],Waypoint_add(&peer_goal));
            peers[i]->movement.group_id=2;
            peers[i]->movement.velocity=(vec2_t){cases[row].all_slow || i==cases[row].slow ? .5f :
                i==cases[row].fast ? 3.f : 0,0};
            G_PublishMoveSpatialObject(peers[i]);
        }
        movePathQuery_t query={{&self->s.origin2,&goal,8,2},self,NULL,true,&source};
        edict_t *items[32]; float fine[]={19.5f,16.5f};
        uint32_t count=G_CollectUnitMoveStepBlockers(&query,fine,items);
        T_EQ(count,32);
        FOR_LOOP(i,count) T_EQ(items[i],peers[i]);
        S_ResolveMoveBlockers(self,items,count);
        bool fast_kept=cases[row].fast>=0 && cases[row].fast<32;
        T_EQ(self->movement.wait_blocker,fast_kept ? peers[cases[row].fast] : NULL);
        T_EQ(self->movement.wait_delay,fast_kept ? 4 : 0);
        FOR_LOOP(i,40) {
            bool visited=i<32 && (!fast_kept || (int)i<cases[row].fast);
            bool assigned=visited && (cases[row].all_slow || (int)i==cases[row].slow);
            T_EQ(peers[i]->movement.wait_blocker,assigned ? self : NULL);
            T_EQ(peers[i]->movement.wait_delay,assigned ? 20 : 0);
        }
    }
    reset_entities(); setup_test_world();
}

/* Part C: each selection observes the current objects; no stale admission
 * result survives insertion, motion, stationary republishing or retirement. */
TEST(wc3_route_blockers, obstruction_changes_between_waypoint_selections) {
    float const radii[]={8,16,32,48};
    FOR_LOOP(cls,4) {
        route_blocker_world();
        edict_t *self=route_blocker_unit(12.25f,16.75f,radii[cls]);
        edict_t *peer=NULL;
        vec2_t source={12.25f,16.75f},goal={20.25f,16.75f},out;
        movePathQuery_t query={{&self->s.origin2,&goal,self->collision,2},self,NULL,true,&source};
        vec2_t points[]={{20.25f,16.75f},{18.25f,16.75f},{16.25f,16.75f},
            {14.25f,16.75f},{12.25f,16.75f}};
        unsigned const expected[]={0,2,0,2,0};
        FOR_LOOP(stage,5) {
            if(stage==1) peer=route_blocker_unit(17.5f,16.5f,8);
            if(stage==2) peer->movement.velocity=(vec2_t){3,0};
            if(stage==3) peer->movement.velocity=(vec2_t){0,0};
            if(stage==4) { G_FreeEdict(peer); peer=NULL; }
            moveFineRoute_t route={.points=points,.count=5,.index=4};
            T_ASSERT(G_AdvanceUnitMoveFineRoute(&query,&route,&out));
            T_EQ(route.index,expected[stage]);
        }
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_route_blockers, diagonal_duplicates_and_terrain_keep_token_order) {
    route_blocker_world();
    edict_t *self=route_blocker_unit(16.5f,16.5f,8);
    edict_t *peer=route_blocker_unit(17.5f,17.5f,32);
    vec2_t source={16.5f,16.5f},goal={19.5f,19.5f};
    movePathQuery_t query={{&self->s.origin2,&goal,8,2},self,NULL,true,&source};
    edict_t *items[32]; float fine[]={19.5f,19.5f};
    T_EQ(G_CollectUnitMoveStepBlockers(&query,fine,items),3);
    FOR_LOOP(i,3) T_EQ(items[i],peer);
    terrainPathingEdit_t edit={{17.5f*32,17.5f*32},2,true};
    T_ASSERT(G_SetTerrainPathingFlags(&edit));
    T_EQ(G_CollectUnitMoveStepBlockers(&query,fine,items),3);
    T_NULL(items[0]); T_EQ(items[1],peer); T_EQ(items[2],peer);
    G_FreeEdict(peer);
    T_EQ(G_CollectUnitMoveStepBlockers(&query,fine,items),1); T_NULL(items[0]);
    reset_entities(); setup_test_world();
}
#endif
