#ifdef BZ_TESTS
/*
 * test_movement.c — Unit movement and pathfinding tests.
 *
 * Tests cover the complete move-order pipeline:
 *
 *  order_move / ai_walk integration
 *    - order_move wires up goalentity and switches to "walk" animation
 *    - unit advances toward goal each frame  (via currentmove->think)
 *    - unit transitions to "stand" once it reaches the goal
 *    - unit_movedistance matches speed × 10 / FRAMETIME
 *
 *  Waypoint helpers
 *    - Waypoint_add places a waypoint at the requested 2-D location
 *
 *  Goal-distance helper
 *    - M_DistanceToGoal returns the 2-D Euclidean distance to goalentity
 *
 * All tests use the test harness mock gi; no actual map or MPQ is needed.
 * Units are given collision = 0 for these movement tests so they don't
 * interact with each other; collision behaviour is covered in
 * test_collision.c.
 */

#include <math.h>
#include "test.h"
#include "../g_local.h"
#include "games/warcraft-3/common/terrain.h"

/* Helpers defined in t_utils.c */
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void CM_SetupTestWorldBounds(box2_t const *bounds);
void CM_ProcessPathJobs(uint32_t work_budget);
bool run_test_jass(cstring_t src);
extern void ai_train_build(edict_t *ent);



/* NAVI_THRESHOLD is the distance below which ai_walk uses direct
 * vector math rather than the heatmap flow field.  It is defined in
 * g_ai.c; the test helpers that place waypoints reference it. */

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/* Create a unit at (x, y) with the lifecycle callbacks and zero collision
 * (movement tests don't want unintended push-apart).  Resets entity pool
 * so each test starts from a clean slate. */
static edict_t *make_moving_unit(float x, float y) {
    reset_entities();
    setup_test_world();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    ent->movetype  = MOVETYPE_STEP;
    ent->stand     = unit_stand;
    ent->birth     = unit_birth;
    ent->die       = unit_die;
    ent->collision = 0.0f;
    ent->health.value     = 250.0f;
    ent->health.max_value = 250.0f;
    unit_stand(ent);
    return ent;
}

/* Drive the native setters before steering, as a map script does. */
static edict_t *make_scripted_turn_unit(void) {
    reset_entities(); setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        "  local unit u = CreateUnit(Player(0), 'hpea', 0.0, 0.0, 0.0)\n"
        "  call SetUnitTurnSpeed(u, 0.125)\n"
        "  call SetUnitPropWindow(u, 0.5)\n"
        "endfunction\n"));
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) {
            /* The minimal fixture lacks the retail model; install the same live lifecycle as make_moving_unit. */
            g_edicts[i].collision = 0; g_edicts[i].health.value = 250;
            g_edicts[i].movetype = MOVETYPE_STEP; g_edicts[i].stand = unit_stand;
            unit_stand(&g_edicts[i]);
            return &g_edicts[i];
        }
    return NULL;
}

/* Oblique motion exposes the nearest-rounded host trig and multiply/add used by the old step. */
TEST(wc3_movement, retail_vector_heading_words) {
    edict_t *unit = make_moving_unit(0, 0);
    vec2_t const point = {4, 0.125f};
    unit_changeangle_towards_point(unit, &point);
    /* Full original16f630 vector/length/acos/shortest-error chain, current heading0. */
    T_EQ(wc3_float_bits(unit->s.angle), 0x3d00f7e3u);
    T_EQ(wc3_float_bits(unit->movement.heading), 0x3d00f7e3u);
    T_ASSERT(!unit->movement.turn_blocked);
}

/* Actual game-owner dispatch must move idle authored repulsors; Footman opts out. */
TEST(wc3_movement, repulsion_reaches_idle_units_through_owner_scheduler) {
    reset_entities(); setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "local unit a=CreateUnit(Player(0),'hgry',512,512,0)\n"
        "local unit b=CreateUnit(Player(0),'hgry',512,512,0)\n"
        "local unit c=CreateUnit(Player(0),'hfoo',1024,512,0)\n"
        "local unit d=CreateUnit(Player(0),'hfoo',1024,512,0)\n"
        "call SetUnitX(a,512)\ncall SetUnitY(a,512)\ncall SetUnitX(b,512)\ncall SetUnitY(b,512)\n"
        "call SetUnitX(c,1024)\ncall SetUnitY(c,512)\ncall SetUnitX(d,1024)\ncall SetUnitY(d,512)\n"
        "call SetRandomSeed(12345)\nendfunction\n"));
    edict_t *fly[2]={0}; unsigned count=0;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','g','r','y')) {
        if (count<2) fly[count++]=ent;
    }
    T_EQ(count,2); if (count!=2) return;
    /* Synthetic models do not supply a birth sequence; establish the ordinary idle lifecycle. */
    FOR_LOOP(i,2) unit_stand(fly[i]);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    level.time=level.pathing_msec=0; level.pathing_phase=0; level.pathing_due=false;
    level.repulse_phase=0; level.time=35; globals.RunFrame();
    /* Original first-overlap composition: saved owner seed, one direction draw, then damping. */
    T_EQ(wc3_float_bits(fly[0]->movement.repulse.state.vector[0]),1031535248u);
    T_EQ(wc3_float_bits(fly[0]->movement.repulse.state.vector[1]),1043093862u);
    T_EQ(level.pathing_random.sum,815324285u); T_EQ(level.pathing_random.index,1957431332u);
    T_EQ(fly[1]->movement.repulse.state.vector[0],0); T_EQ(fly[1]->movement.repulse.state.vector[1],0);
    cstring_t file="/tmp/openwarcraft3-repulsion-save.bin";
    T_ASSERT(WriteGame(file));
    FOR_LOOP(i,10) { level.time+=100; globals.RunFrame(); }
    vec2_t positions[2]={fly[0]->s.origin2,fly[1]->s.origin2};
    wc3Repulse_t pending[2]={fly[0]->movement.repulse.state,fly[1]->movement.repulse.state};
    wc3Random_t random=level.pathing_random;
    T_ASSERT(Vector2_distance(&fly[0]->s.origin2,&fly[1]->s.origin2)>1);
    FOR_LOOP(i,2) { T_EQ(fly[i]->current_order_id,0); T_EQ(fly[i]->movement.velocity.x,0); T_EQ(fly[i]->movement.velocity.y,0); }
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o')) {
        T_EQ(ent->s.origin2.x,1024); T_EQ(ent->s.origin2.y,512);
    }
    T_ASSERT(ReadGame(file));
    T_EQ(level.repulse_phase,1); T_ASSERT(level.repulse_head==fly[1]);
    T_ASSERT(fly[1]->movement.repulse.next==fly[0]); T_ASSERT(!fly[0]->movement.repulse.next);
    T_EQ(wc3_float_bits(fly[0]->movement.repulse.state.vector[0]),1031535248u);
    T_EQ(wc3_float_bits(fly[0]->movement.repulse.state.vector[1]),1043093862u);
    FOR_LOOP(i,10) { level.time+=100; globals.RunFrame(); }
    FOR_LOOP(i,2) {
        T_EQ(wc3_float_bits(fly[i]->s.origin2.x),wc3_float_bits(positions[i].x));
        T_EQ(wc3_float_bits(fly[i]->s.origin2.y),wc3_float_bits(positions[i].y));
        FOR_LOOP(k,2) T_EQ(wc3_float_bits(fly[i]->movement.repulse.state.vector[k]),wc3_float_bits(pending[i].vector[k]));
        T_EQ(fly[i]->movement.repulse.state.packed,pending[i].packed);
    }
    T_EQ(level.pathing_random.sum,random.sum); T_EQ(level.pathing_random.index,random.index);
    remove(file);
    reset_entities(); setup_test_world();
}

/* Public owner transfer must replace policy and membership before the next owner visit. */
TEST(wc3_movement, repulsion_owner_change_pause_and_removal) {
    reset_entities(); setup_test_world();
    T_ASSERT(run_test_jass("globals\nunit a\nunit b\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set a=CreateUnit(Player(0),'hgry',512,512,0)\nset b=CreateUnit(Player(1),'hgry',512,512,0)\n"
        "call SetUnitX(a,512)\ncall SetUnitY(a,512)\ncall SetUnitX(b,512)\ncall SetUnitY(b,512)\n"
        "call SetRandomSeed(12345)\nendfunction\n"
        "function transfer takes nothing returns nothing\ncall SetUnitOwner(b,Player(0),false)\nendfunction\n"
        "function freeze takes nothing returns nothing\ncall PauseUnit(a,true)\nendfunction\n"
        "function retire takes nothing returns nothing\ncall RemoveUnit(b)\nendfunction\n"));
    edict_t *fly[2]={0}; unsigned count=0;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','g','r','y'))
        if (count<2) fly[count++]=ent;
    T_EQ(count,2); if (count!=2) return;
    FOR_LOOP(i,2) unit_stand(fly[i]);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    level.time=level.pathing_msec=0; level.pathing_phase=level.repulse_phase=0; level.pathing_due=false;
    level.time=65; globals.RunFrame();
    FOR_LOOP(i,2) {
        T_EQ(fly[i]->s.origin2.x,512); T_EQ(fly[i]->s.origin2.y,512);
        T_EQ(fly[i]->movement.repulse.state.packed & 65535,7);
    }
    T_EQ(level.pathing_random.sum,2689401862u); T_EQ(level.pathing_random.index,2025332800u);
    jass_callbyname(level.vm,"transfer",false); T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(fly[1]->movement.repulse.state.packed,0); T_ASSERT(level.repulse_head==fly[1]);
    level.time=125; globals.RunFrame();
    T_ASSERT(fly[1]->movement.repulse.state.vector[0]!=0 || fly[1]->movement.repulse.state.vector[1]!=0);
    jass_callbyname(level.vm,"freeze",false); T_ASSERT(!jass_rterror_pending(level.vm));
    vec2_t frozen=fly[0]->s.origin2;
    level.time=335; globals.RunFrame();
    T_EQ(fly[0]->s.origin2.x,frozen.x); T_EQ(fly[0]->s.origin2.y,frozen.y);
    jass_callbyname(level.vm,"retire",false); T_ASSERT(!jass_rterror_pending(level.vm));
    level.time+=100; globals.RunFrame();
    T_ASSERT(level.repulse_head==fly[0]); T_ASSERT(!fly[0]->movement.repulse.next);
    T_ASSERT(!fly[1]->movement.repulse.active);
    reset_entities(); setup_test_world();
}

/* A non-stock SLK row must select authored configuration, group and priority, rather than Gryphon defaults. */
TEST(wc3_movement, repulsion_uses_authored_nonstock_policy) {
    reset_entities(); setup_test_world();
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\n"
        "local unit a=CreateUnit(Player(0),'hRPL',512,512,0)\n"
        "local unit b=CreateUnit(Player(0),'hRPL',512,512,0)\n"
        "local unit c=CreateUnit(Player(0),'hgry',512,512,0)\n"
        "call SetUnitX(a,512)\ncall SetUnitY(a,512)\ncall SetUnitX(b,512)\ncall SetUnitY(b,512)\n"
        "call SetUnitX(c,512)\ncall SetUnitY(c,512)\ncall SetRandomSeed(12345)\nendfunction\n"));
    edict_t *first=NULL, *stock=NULL; unsigned count=0;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','R','P','L')) {
        if (!first) first=ent;
        count++; unit_stand(ent); T_EQ(ent->movement.repulse.state.packed,0x30310000u);
    }
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','g','r','y')) stock=ent;
    T_EQ(count,2); T_NOT_NULL(stock); if (!first || !stock) return;
    unit_stand(stock);
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    level.time=level.pathing_msec=0; level.pathing_phase=level.repulse_phase=0; level.pathing_due=false;
    level.time=65; globals.RunFrame();
    T_ASSERT(first->movement.repulse.state.vector[0]!=0 || first->movement.repulse.state.vector[1]!=0);
    T_EQ(stock->movement.repulse.state.vector[0],0); T_EQ(stock->movement.repulse.state.vector[1],0);
    T_EQ(stock->movement.repulse.state.packed & 65535,7);
    FOR_LOOP(i,10) { level.time+=100; globals.RunFrame(); }
    T_ASSERT(first->s.origin2.x!=512 || first->s.origin2.y!=512);
    T_EQ(stock->s.origin2.x,512); T_EQ(stock->s.origin2.y,512);
    reset_entities(); setup_test_world();
}

/* A long location order must retain a retail adaptive turn rather than discard it for a flow field. */
TEST(wc3_movement, retail_adaptive_long_move_reaches_engine) {
    reset_entities(); setup_test_world();
    uint8_t cells[64*64]={0};
    FOR_LOOP(y,64) FOR_LOOP(x,2) if (y<44 || y>=52) cells[y*64+32+x]=2;
    box2_t bounds={{0,0},{2048,2048}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(64,64,cells);
    T_ASSERT(run_test_jass("globals\nunit mover\nendglobals\n"
        "function main takes nothing returns nothing\nset mover=CreateUnit(Player(0),'hfoo',272,304,0)\n"
        "call SetUnitMoveSpeed(mover,200)\nendfunction\n"
        "function go takes nothing returns nothing\ncall IssuePointOrder(mover,\"move\",1936,1776)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','f','o','o')) unit=ent;
    T_NOT_NULL(unit); if (!unit) return;
    unit_stand(unit); jass_callbyname(level.vm,"go",false); T_ASSERT(!jass_rterror_pending(level.vm));
    vec2_t target={1936,1776}, waypoint;
    movePathQuery_t query={{&unit->s.origin2,&target,unit->collision,M_UnitStaticPathingFlags(unit)},unit,NULL,true};
    T_ASSERT(G_FindUnitMovePathWaypoint(&query,&waypoint));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    level.time=level.pathing_msec=0; level.pathing_phase=0; level.pathing_due=false;
    FOR_LOOP(i,10) { level.time+=100; globals.RunFrame(); }
    T_ASSERT(unit->movement.path.valid);
    T_ASSERT(Vector2_distance(&unit->s.origin2,&(vec2_t){272,304})>1);
    cstring_t file="/tmp/openwarcraft3-adaptive-move-save.bin";
    uint32_t continued[180][6];
    T_NOT_NULL(unit->movement.fine_route.adaptive_points);
    uint32_t coarse_count=unit->movement.fine_route.adaptive_count,coarse_index=unit->movement.fine_route.adaptive_index;
    T_ASSERT(coarse_count>1);
    T_ASSERT(WriteGame(file));
    FOR_LOOP(i,180) {
        level.time+=100; globals.RunFrame();
        movePathQuery_t legal={{&unit->s.origin2,NULL,unit->collision,M_UnitStaticPathingFlags(unit)},unit,NULL,true};
        float fine[2]={unit->s.origin2.x/32,unit->s.origin2.y/32};
        T_ASSERT(G_UnitMovePathFinePointIsPathable(&legal,fine));
        continued[i][0]=wc3_float_bits(unit->s.origin2.x); continued[i][1]=wc3_float_bits(unit->s.origin2.y);
        continued[i][2]=wc3_float_bits(unit->s.angle); continued[i][3]=wc3_float_bits(unit->movement.velocity.x);
        continued[i][4]=wc3_float_bits(unit->movement.velocity.y); continued[i][5]=unit->current_order_id;
    }
    T_EQ(unit->current_order_id,0); T_ASSERT(Vector2_distance(&unit->s.origin2,&target)<=32*wc3_float(0x3efae148));
    T_ASSERT(ReadGame(file));
    T_NOT_NULL(unit->movement.fine_route.adaptive_points);
    T_EQ(unit->movement.fine_route.adaptive_count,coarse_count);
    T_EQ(unit->movement.fine_route.adaptive_index,coarse_index);
    FOR_LOOP(i,180) {
        level.time+=100; globals.RunFrame();
        T_EQ(wc3_float_bits(unit->s.origin2.x),continued[i][0]); T_EQ(wc3_float_bits(unit->s.origin2.y),continued[i][1]);
        T_EQ(wc3_float_bits(unit->s.angle),continued[i][2]); T_EQ(wc3_float_bits(unit->movement.velocity.x),continued[i][3]);
        T_EQ(wc3_float_bits(unit->movement.velocity.y),continued[i][4]); T_EQ(unit->current_order_id,continued[i][5]);
    }
    remove(file);
    reset_entities(); setup_test_world();
}

/* Initial adaptive-to-fine route buffers from complete original165ae0 calls.
 * Coarse destination selection follows route length, before the fine request. */
TEST(wc3_movement, retail_adaptive_handoff_route_words) {
    static uint32_t const handoff_words[]={
        0x42060000u,0x42060000u,0x42020000u,0x42020000u,0x41fc0000u,0x41fc0000u,0x41f40000u,0x41f40000u,
        0x41ec0000u,0x41ec0000u,0x41e40000u,0x41e40000u,0x41dc0000u,0x41dc0000u,0x41d40000u,0x41d40000u,
        0x41cc0000u,0x41cc0000u,0x41c40000u,0x41c40000u,0x41bc0000u,0x41bc0000u,0x41b40000u,0x41b40000u,
        0x41ac0000u,0x41ac0000u,0x41a40000u,0x41a40000u,0x419c0000u,0x419c0000u,0x41940000u,0x41940000u,
        0x418c0000u,0x418c0000u,0x41840000u,0x41840000u,0x41780000u,0x41780000u,0x41680000u,0x41680000u,
        0x41580000u,0x41580000u,0x41480000u,0x41480000u,0x41380000u,0x41380000u,0x41280000u,0x41280000u,
        0x41180000u,0x41180000u,0x41080000u,0x41080000u,0x40f00000u,0x40f00000u,0x40d00000u,0x40d00000u,
        0x40b00000u,0x40b00000u,0x40880000u,0x40980000u,0x42060000u,0x42060000u,0x42020000u,0x42020000u,
        0x41fc0000u,0x41fc0000u,0x41f40000u,0x41f40000u,0x41ec0000u,0x41ec0000u,0x41e40000u,0x41e40000u,
        0x41dc0000u,0x41dc0000u,0x41d40000u,0x41d40000u,0x41cc0000u,0x41cc0000u,0x41c40000u,0x41c40000u,
        0x41bc0000u,0x41bc0000u,0x41b40000u,0x41b40000u,0x41ac0000u,0x41ac0000u,0x41a40000u,0x41a40000u,
        0x419c0000u,0x419c0000u,0x41940000u,0x41940000u,0x418c0000u,0x418c0000u,0x41840000u,0x41840000u,
        0x41780000u,0x41780000u,0x41680000u,0x41680000u,0x41580000u,0x41580000u,0x41480000u,0x41480000u,
        0x41380000u,0x41380000u,0x41280000u,0x41280000u,0x41180000u,0x41180000u,0x41080000u,0x41080000u,
        0x40f00000u,0x40f00000u,0x40d00000u,0x40d00000u,0x40b00000u,0x40b00000u,0x40880000u,0x40980000u,
        0x420a0000u,0x420a0000u,0x42060000u,0x42060000u,0x42020000u,0x42020000u,0x41fc0000u,0x41fc0000u,
        0x41f40000u,0x41f40000u,0x41ec0000u,0x41ec0000u,0x41e40000u,0x41e40000u,0x41dc0000u,0x41dc0000u,
        0x41d40000u,0x41d40000u,0x41cc0000u,0x41cc0000u,0x41c40000u,0x41c40000u,0x41bc0000u,0x41bc0000u,
        0x41b40000u,0x41b40000u,0x41ac0000u,0x41ac0000u,0x41a40000u,0x41a40000u,0x419c0000u,0x419c0000u,
        0x41940000u,0x41940000u,0x418c0000u,0x418c0000u,0x41840000u,0x41840000u,0x41780000u,0x41780000u,
        0x41680000u,0x41680000u,0x41580000u,0x41580000u,0x41480000u,0x41480000u,0x41380000u,0x41380000u,
        0x41280000u,0x41280000u,0x41180000u,0x41180000u,0x41080000u,0x41080000u,0x40f00000u,0x40f00000u,
        0x40d00000u,0x40d00000u,0x40b00000u,0x40b00000u,0x40880000u,0x40980000u,0x420a0000u,0x420a0000u,
        0x42060000u,0x42060000u,0x42020000u,0x42020000u,0x41fc0000u,0x41fc0000u,0x41f40000u,0x41f40000u,
        0x41ec0000u,0x41ec0000u,0x41e40000u,0x41e40000u,0x41dc0000u,0x41dc0000u,0x41d40000u,0x41d40000u,
        0x41cc0000u,0x41cc0000u,0x41c40000u,0x41c40000u,0x41bc0000u,0x41bc0000u,0x41b40000u,0x41b40000u,
        0x41ac0000u,0x41ac0000u,0x41a40000u,0x41a40000u,0x419c0000u,0x419c0000u,0x41940000u,0x41940000u,
        0x418c0000u,0x418c0000u,0x41840000u,0x41840000u,0x41780000u,0x41780000u,0x41680000u,0x41680000u,
        0x41580000u,0x41580000u,0x41480000u,0x41480000u,0x41380000u,0x41380000u,0x41280000u,0x41280000u,
        0x41180000u,0x41180000u,0x41080000u,0x41080000u,0x40f00000u,0x40f00000u,0x40d00000u,0x40d00000u,
        0x40b00000u,0x40b00000u,0x40880000u,0x40980000u,0x41780000u,0x42060000u,0x41680000u,0x42020000u,
        0x41680000u,0x41fc0000u,0x41580000u,0x41f40000u,0x41480000u,0x41ec0000u,0x41380000u,0x41e40000u,
        0x41280000u,0x41dc0000u,0x41280000u,0x41d40000u,0x41180000u,0x41cc0000u,0x41080000u,0x41c40000u,
        0x40f00000u,0x41bc0000u,0x40d00000u,0x41b40000u,0x40d00000u,0x41ac0000u,0x40d00000u,0x41a40000u,
        0x40d00000u,0x419c0000u,0x40d00000u,0x41940000u,0x40d00000u,0x418c0000u,0x40d00000u,0x41840000u,
        0x40b00000u,0x41780000u,0x40900000u,0x41680000u,0x40900000u,0x41580000u,0x40900000u,0x41480000u,
        0x40900000u,0x41380000u,0x40900000u,0x41280000u,0x40900000u,0x41180000u,0x40900000u,0x41080000u,
        0x40900000u,0x40f00000u,0x40900000u,0x40d00000u,0x40900000u,0x40b00000u,0x40880000u,0x40980000u,
        0x41780000u,0x42060000u,0x41680000u,0x42020000u,0x41680000u,0x41fc0000u,0x41580000u,0x41f40000u,
        0x41480000u,0x41ec0000u,0x41380000u,0x41e40000u,0x41280000u,0x41dc0000u,0x41280000u,0x41d40000u,
        0x41180000u,0x41cc0000u,0x41080000u,0x41c40000u,0x40f00000u,0x41bc0000u,0x40d00000u,0x41b40000u,
        0x40d00000u,0x41ac0000u,0x40d00000u,0x41a40000u,0x40d00000u,0x419c0000u,0x40d00000u,0x41940000u,
        0x40d00000u,0x418c0000u,0x40d00000u,0x41840000u,0x40b00000u,0x41780000u,0x40900000u,0x41680000u,
        0x40900000u,0x41580000u,0x40900000u,0x41480000u,0x40900000u,0x41380000u,0x40900000u,0x41280000u,
        0x40900000u,0x41180000u,0x40900000u,0x41080000u,0x40900000u,0x40f00000u,0x40900000u,0x40d00000u,
        0x40900000u,0x40b00000u,0x40880000u,0x40980000u,0x41680000u,0x420a0000u,0x41580000u,0x42060000u,
        0x41480000u,0x42020000u,0x41380000u,0x41fc0000u,0x41380000u,0x41f40000u,0x41380000u,0x41ec0000u,
        0x41280000u,0x41e40000u,0x41280000u,0x41dc0000u,0x41180000u,0x41d40000u,0x41080000u,0x41cc0000u,
        0x41080000u,0x41c40000u,0x41080000u,0x41bc0000u,0x40f00000u,0x41b40000u,0x40d00000u,0x41ac0000u,
        0x40b00000u,0x41a40000u,0x40900000u,0x419c0000u,0x40900000u,0x41940000u,0x40900000u,0x418c0000u,
        0x40900000u,0x41840000u,0x40900000u,0x41780000u,0x40900000u,0x41680000u,0x40900000u,0x41580000u,
        0x40900000u,0x41480000u,0x40900000u,0x41380000u,0x40900000u,0x41280000u,0x40900000u,0x41180000u,
        0x40900000u,0x41080000u,0x40900000u,0x40f00000u,0x40900000u,0x40d00000u,0x40900000u,0x40b00000u,
        0x40880000u,0x40980000u,0x41680000u,0x420a0000u,0x41580000u,0x42060000u,0x41480000u,0x42020000u,
        0x41380000u,0x41fc0000u,0x41380000u,0x41f40000u,0x41380000u,0x41ec0000u,0x41280000u,0x41e40000u,
        0x41280000u,0x41dc0000u,0x41180000u,0x41d40000u,0x41080000u,0x41cc0000u,0x41080000u,0x41c40000u,
        0x41080000u,0x41bc0000u,0x40f00000u,0x41b40000u,0x40d00000u,0x41ac0000u,0x40b00000u,0x41a40000u,
        0x40900000u,0x419c0000u,0x40900000u,0x41940000u,0x40900000u,0x418c0000u,0x40900000u,0x41840000u,
        0x40900000u,0x41780000u,0x40900000u,0x41680000u,0x40900000u,0x41580000u,0x40900000u,0x41480000u,
        0x40900000u,0x41380000u,0x40900000u,0x41280000u,0x40900000u,0x41180000u,0x40900000u,0x41080000u,
        0x40900000u,0x40f00000u,0x40900000u,0x40d00000u,0x40900000u,0x40b00000u,0x40880000u,0x40980000u,
        0x42060000u,0x41fc0000u,0x42020000u,0x41f40000u,0x41fc0000u,0x41f40000u,0x41f40000u,0x41f40000u,
        0x41ec0000u,0x41ec0000u,0x41e40000u,0x41e40000u,0x41dc0000u,0x41dc0000u,0x41d40000u,0x41d40000u,
        0x41cc0000u,0x41cc0000u,0x41c40000u,0x41c40000u,0x41bc0000u,0x41bc0000u,0x41b40000u,0x41b40000u,
        0x41ac0000u,0x41ac0000u,0x41a40000u,0x41a40000u,0x419c0000u,0x419c0000u,0x41940000u,0x41940000u,
        0x418c0000u,0x418c0000u,0x41840000u,0x41840000u,0x41780000u,0x41780000u,0x41680000u,0x41680000u,
        0x41580000u,0x41580000u,0x41480000u,0x41480000u,0x41380000u,0x41380000u,0x41280000u,0x41280000u,
        0x41180000u,0x41180000u,0x41080000u,0x41080000u,0x40f00000u,0x40f00000u,0x40d00000u,0x40d00000u,
        0x40b00000u,0x40b00000u,0x40880000u,0x40980000u,0x42060000u,0x41fc0000u,0x42020000u,0x41f40000u,
        0x41fc0000u,0x41f40000u,0x41f40000u,0x41f40000u,0x41ec0000u,0x41ec0000u,0x41e40000u,0x41e40000u,
        0x41dc0000u,0x41dc0000u,0x41d40000u,0x41d40000u,0x41cc0000u,0x41cc0000u,0x41c40000u,0x41c40000u,
        0x41bc0000u,0x41bc0000u,0x41b40000u,0x41b40000u,0x41ac0000u,0x41ac0000u,0x41a40000u,0x41a40000u,
        0x419c0000u,0x419c0000u,0x41940000u,0x41940000u,0x418c0000u,0x418c0000u,0x41840000u,0x41840000u,
        0x41780000u,0x41780000u,0x41680000u,0x41680000u,0x41580000u,0x41580000u,0x41480000u,0x41480000u,
        0x41380000u,0x41380000u,0x41280000u,0x41280000u,0x41180000u,0x41180000u,0x41080000u,0x41080000u,
        0x40f00000u,0x40f00000u,0x40d00000u,0x40d00000u,0x40b00000u,0x40b00000u,0x40880000u,0x40980000u,
        0x420a0000u,0x42020000u,0x42060000u,0x41fc0000u,0x42020000u,0x41f40000u,0x41fc0000u,0x41f40000u,
        0x41f40000u,0x41f40000u,0x41ec0000u,0x41ec0000u,0x41e40000u,0x41e40000u,0x41dc0000u,0x41dc0000u,
        0x41d40000u,0x41d40000u,0x41cc0000u,0x41cc0000u,0x41c40000u,0x41c40000u,0x41bc0000u,0x41bc0000u,
        0x41b40000u,0x41b40000u,0x41ac0000u,0x41ac0000u,0x41a40000u,0x41a40000u,0x419c0000u,0x419c0000u,
        0x41940000u,0x41940000u,0x418c0000u,0x418c0000u,0x41840000u,0x41840000u,0x41780000u,0x41780000u,
        0x41680000u,0x41680000u,0x41580000u,0x41580000u,0x41480000u,0x41480000u,0x41380000u,0x41380000u,
        0x41280000u,0x41280000u,0x41180000u,0x41180000u,0x41080000u,0x41080000u,0x40f00000u,0x40f00000u,
        0x40d00000u,0x40d00000u,0x40b00000u,0x40b00000u,0x40880000u,0x40980000u,0x420a0000u,0x42020000u,
        0x42060000u,0x41fc0000u,0x42020000u,0x41fc0000u,0x41fc0000u,0x41fc0000u,0x41f40000u,0x41fc0000u,
        0x41f40000u,0x41f40000u,0x41ec0000u,0x41ec0000u,0x41e40000u,0x41e40000u,0x41dc0000u,0x41dc0000u,
        0x41d40000u,0x41d40000u,0x41cc0000u,0x41cc0000u,0x41c40000u,0x41c40000u,0x41bc0000u,0x41bc0000u,
        0x41b40000u,0x41b40000u,0x41ac0000u,0x41ac0000u,0x41a40000u,0x41a40000u,0x419c0000u,0x419c0000u,
        0x41940000u,0x41940000u,0x418c0000u,0x418c0000u,0x41840000u,0x41840000u,0x41780000u,0x41780000u,
        0x41680000u,0x41680000u,0x41580000u,0x41580000u,0x41480000u,0x41480000u,0x41380000u,0x41380000u,
        0x41280000u,0x41280000u,0x41180000u,0x41180000u,0x41080000u,0x41080000u,0x40f00000u,0x40f00000u,
        0x40d00000u,0x40d00000u,0x40b00000u,0x40b00000u,0x40880000u,0x40980000u,
    };
    static uint32_t const cases[12][4]={
        {0,0,0,30},
        {0,1,60,30},
        {0,2,120,31},
        {0,3,182,31},
        {1,0,244,30},
        {1,1,304,30},
        {1,2,364,31},
        {1,3,426,31},
        {2,0,488,30},
        {2,1,548,30},
        {2,2,608,31},
        {2,3,670,32},
    };
    static uint8_t const masks[]={2,4,0x40,0x80};
    FOR_LOOP(c,12) FOR_LOOP(lane,4) {
        edict_t *unit=make_moving_unit(136,152);
        uint8_t cells[64*64]={0};
        FOR_LOOP(y,64) if (cases[c][0] && (cases[c][0]==1 || y<29 || y>34)) cells[y*64+32]=0xc6;
        box2_t bounds={{0,0},{2048,2048}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(64,64,cells);
        vec2_t goal={1896,1912},waypoint;
        unit->collision=8+cases[c][1]*16;
        movePathQuery_t query={{&unit->s.origin2,&goal,unit->collision,masks[lane]},unit,NULL,true};
        moveFineRoute_t route={0};
        T_ASSERT(G_BuildUnitMoveFineRoute(&query,&route,&waypoint));
        T_EQ(route.count,cases[c][3]);
        T_EQ(route.index,cases[c][3]-2);
        if (route.count==cases[c][3]) FOR_LOOP(i,route.count) {
            T_EQ(wc3_float_bits(route.points[i].x),handoff_words[cases[c][2]+2*i]);
            T_EQ(wc3_float_bits(route.points[i].y),handoff_words[cases[c][2]+2*i+1]);
        }
        free(route.points); free(route.adaptive_points);
    }
    reset_entities(); setup_test_world();
}

/* Public Move must consume the same initial local leg as the original full path advance. */
TEST(wc3_movement, retail_adaptive_handoff_reaches_public_move) {
    reset_entities(); setup_test_world();
    level.waypoints=(typeof(level.waypoints)){0}; level.pathing_clock=(wc3Clock_t){0,0,8};
    uint8_t cells[64*64]={0};
    FOR_LOOP(y,64) cells[y*64+32]=0xc6;
    box2_t bounds={{0,0},{2048,2048}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(64,64,cells);
    T_ASSERT(run_test_jass("globals\nunit mover\nendglobals\nfunction main takes nothing returns nothing\n"
        "set mover=CreateUnit(Player(0),'hRTE',136,152,0)\nendfunction\n"
        "function go takes nothing returns nothing\ncall IssuePointOrder(mover,\"move\",1896,1912)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','R','T','E')) unit=ent;
    T_NOT_NULL(unit); if (!unit) return;
    unit_stand(unit); jass_callbyname(level.vm,"go",false); unit_changeangle(unit);
    T_ASSERT(unit->movement.path.valid);
    T_NOT_NULL(unit->movement.fine_route.points); if (!unit->movement.fine_route.points) return;
    T_EQ(unit->movement.fine_route.count,30); T_EQ(unit->movement.fine_route.index,28);
    T_EQ(wc3_float_bits(unit->movement.fine_route.points[0].x),wc3_float_bits(15.5f));
    T_EQ(wc3_float_bits(unit->movement.fine_route.points[0].y),wc3_float_bits(33.5f));
    T_EQ(wc3_float_bits(unit->movement.path.waypoint.x),wc3_float_bits(144));
    T_EQ(wc3_float_bits(unit->movement.path.waypoint.y),wc3_float_bits(176));
    T_EQ(unit->goalentity->s.origin2.x,1896); T_EQ(unit->goalentity->s.origin2.y,1912);
    T_ASSERT(!jass_rterror_pending(level.vm));
    reset_entities(); setup_test_world();
}

/* Original coarse approach threshold is measured before fine-route progression:
 * .48 coarse units (.96 fine) already selects/refills the following local leg. */
TEST(wc3_movement, retail_adaptive_progress_refills_before_fine_endpoint) {
    static uint32_t const progress_words[]={
        0x426d0000u,0x426f0000u,0x426a0000u,0x426a0000u,0x42660000u,0x426a0000u,0x42620000u,0x42660000u,
        0x425e0000u,0x42620000u,0x425a0000u,0x425e0000u,0x42560000u,0x425a0000u,0x42520000u,0x42560000u,
        0x424e0000u,0x42520000u,0x424a0000u,0x424e0000u,0x42460000u,0x424a0000u,0x42420000u,0x42460000u,
        0x423e0000u,0x42420000u,0x423a0000u,0x423e0000u,0x42360000u,0x423a0000u,0x42320000u,0x42360000u,
        0x422e0000u,0x42320000u,0x422a0000u,0x422e0000u,0x42260000u,0x422a0000u,0x42220000u,0x42260000u,
        0x421e0000u,0x42220000u,0x421a0000u,0x421e0000u,0x42160000u,0x421a0000u,0x42120000u,0x42160000u,
        0x420e0000u,0x42120000u,0x420a0000u,0x420e0000u,0x42060000u,0x420a0000u,0x420228f6u,0x42060000u,
        0x426d0000u,0x426f0000u,0x426a0000u,0x426a0000u,0x42660000u,0x426a0000u,0x42620000u,0x42660000u,
        0x425e0000u,0x42620000u,0x425a0000u,0x425e0000u,0x42560000u,0x425a0000u,0x42520000u,0x42560000u,
        0x424e0000u,0x42520000u,0x424a0000u,0x424e0000u,0x42460000u,0x424a0000u,0x42420000u,0x42460000u,
        0x423e0000u,0x42420000u,0x423a0000u,0x423e0000u,0x42360000u,0x423a0000u,0x42320000u,0x42360000u,
        0x422e0000u,0x42320000u,0x422a0000u,0x422e0000u,0x42260000u,0x422a0000u,0x42220000u,0x42260000u,
        0x421e0000u,0x42220000u,0x421a0000u,0x421e0000u,0x42160000u,0x421a0000u,0x42120000u,0x42160000u,
        0x420e0000u,0x42120000u,0x420a0000u,0x420e0000u,0x42060000u,0x420a0000u,0x420228f6u,0x42060000u,
        0x426d0000u,0x426f0000u,0x426a0000u,0x426a0000u,0x42660000u,0x426a0000u,0x42620000u,0x42660000u,
        0x425e0000u,0x42620000u,0x425a0000u,0x425e0000u,0x42560000u,0x425a0000u,0x42520000u,0x42560000u,
        0x424e0000u,0x42520000u,0x424a0000u,0x424e0000u,0x42460000u,0x424a0000u,0x42420000u,0x42460000u,
        0x423e0000u,0x42420000u,0x423a0000u,0x423e0000u,0x42360000u,0x423a0000u,0x42320000u,0x42360000u,
        0x422e0000u,0x42320000u,0x422a0000u,0x422e0000u,0x42260000u,0x422a0000u,0x42220000u,0x42260000u,
        0x421e0000u,0x42220000u,0x421a0000u,0x421e0000u,0x42160000u,0x421a0000u,0x42120000u,0x42160000u,
        0x420e0000u,0x42120000u,0x420a0000u,0x420e0000u,0x420628f6u,0x420a0000u,0x426d0000u,0x426f0000u,
        0x426a0000u,0x426a0000u,0x42660000u,0x426a0000u,0x42620000u,0x42660000u,0x425e0000u,0x42620000u,
        0x425a0000u,0x425e0000u,0x42560000u,0x425a0000u,0x42520000u,0x42560000u,0x424e0000u,0x42520000u,
        0x424a0000u,0x424e0000u,0x42460000u,0x424a0000u,0x42420000u,0x42460000u,0x423e0000u,0x42420000u,
        0x423a0000u,0x423e0000u,0x42360000u,0x423a0000u,0x42320000u,0x42360000u,0x422e0000u,0x42320000u,
        0x422a0000u,0x422e0000u,0x42260000u,0x422a0000u,0x42220000u,0x42260000u,0x421e0000u,0x42220000u,
        0x421a0000u,0x421e0000u,0x42160000u,0x421a0000u,0x42120000u,0x42160000u,0x420e0000u,0x42120000u,
        0x420a0000u,0x420e0000u,0x420628f6u,0x420a0000u,0x426d0000u,0x426f0000u,0x426a0000u,0x426a0000u,
        0x426a0000u,0x42660000u,0x42660000u,0x42620000u,0x42620000u,0x425e0000u,0x425e0000u,0x425a0000u,
        0x425a0000u,0x42560000u,0x42560000u,0x42520000u,0x42520000u,0x424e0000u,0x424e0000u,0x424a0000u,
        0x424a0000u,0x42460000u,0x42460000u,0x42420000u,0x42420000u,0x423e0000u,0x423e0000u,0x423a0000u,
        0x423a0000u,0x42360000u,0x42360000u,0x42320000u,0x42320000u,0x422e0000u,0x422e0000u,0x422a0000u,
        0x422a0000u,0x42260000u,0x42260000u,0x42220000u,0x42220000u,0x421e0000u,0x421e0000u,0x421a0000u,
        0x421a0000u,0x42160000u,0x42160000u,0x42120000u,0x42120000u,0x420e0000u,0x420e0000u,0x420a0000u,
        0x420a0000u,0x42060000u,0x42060000u,0x42020000u,0x420228f6u,0x41fc0000u,0x426d0000u,0x426f0000u,
        0x426a0000u,0x426a0000u,0x426a0000u,0x42660000u,0x42660000u,0x42620000u,0x42620000u,0x425e0000u,
        0x425e0000u,0x425a0000u,0x425a0000u,0x42560000u,0x42560000u,0x42520000u,0x42520000u,0x424e0000u,
        0x424e0000u,0x424a0000u,0x424a0000u,0x42460000u,0x42460000u,0x42420000u,0x42420000u,0x423e0000u,
        0x423e0000u,0x423a0000u,0x423a0000u,0x42360000u,0x42360000u,0x42320000u,0x42320000u,0x422e0000u,
        0x422e0000u,0x422a0000u,0x422a0000u,0x42260000u,0x42260000u,0x42220000u,0x42220000u,0x421e0000u,
        0x421e0000u,0x421a0000u,0x421a0000u,0x42160000u,0x42160000u,0x42120000u,0x42120000u,0x420e0000u,
        0x420e0000u,0x420a0000u,0x420a0000u,0x42060000u,0x42060000u,0x42020000u,0x420228f6u,0x41fc0000u,
        0x426d0000u,0x426f0000u,0x426a0000u,0x426a0000u,0x426a0000u,0x42660000u,0x42660000u,0x42620000u,
        0x42620000u,0x425e0000u,0x425e0000u,0x425a0000u,0x425a0000u,0x42560000u,0x42560000u,0x42520000u,
        0x42520000u,0x424e0000u,0x424e0000u,0x424a0000u,0x424a0000u,0x42460000u,0x42460000u,0x42420000u,
        0x42420000u,0x423e0000u,0x423e0000u,0x423a0000u,0x423a0000u,0x42360000u,0x42360000u,0x42320000u,
        0x42320000u,0x422e0000u,0x422e0000u,0x422a0000u,0x422a0000u,0x42260000u,0x42260000u,0x42220000u,
        0x42220000u,0x421e0000u,0x421e0000u,0x421a0000u,0x421a0000u,0x42160000u,0x42160000u,0x42120000u,
        0x42120000u,0x420e0000u,0x420e0000u,0x420a0000u,0x420a0000u,0x42060000u,0x420628f6u,0x42020000u,
        0x426d0000u,0x426f0000u,0x426a0000u,0x426a0000u,0x426a0000u,0x42660000u,0x426a0000u,0x42620000u,
        0x42660000u,0x425e0000u,0x42620000u,0x425a0000u,0x425e0000u,0x42560000u,0x425a0000u,0x42520000u,
        0x42560000u,0x424e0000u,0x42520000u,0x424a0000u,0x424e0000u,0x42460000u,0x424a0000u,0x42420000u,
        0x42460000u,0x423e0000u,0x42420000u,0x423a0000u,0x423e0000u,0x42360000u,0x423a0000u,0x42320000u,
        0x42360000u,0x422e0000u,0x42320000u,0x422a0000u,0x422e0000u,0x42260000u,0x422a0000u,0x42220000u,
        0x42260000u,0x421e0000u,0x42220000u,0x421a0000u,0x421e0000u,0x42160000u,0x421a0000u,0x42120000u,
        0x42160000u,0x420e0000u,0x42120000u,0x420a0000u,0x420e0000u,0x42060000u,0x420a0000u,0x42060000u,
        0x420628f6u,0x42020000u,
    };
    static uint32_t const cases[8][6]={
        {0x00000000u,0x00000000u,0x00000000u,0x0000001cu,0x420228f6u,0x42060000u},
        {0x00000000u,0x00000001u,0x00000038u,0x0000001cu,0x420228f6u,0x42060000u},
        {0x00000000u,0x00000002u,0x00000070u,0x0000001bu,0x420628f6u,0x420a0000u},
        {0x00000000u,0x00000003u,0x000000a6u,0x0000001bu,0x420628f6u,0x420a0000u},
        {0x00000001u,0x00000000u,0x000000dcu,0x0000001du,0x420228f6u,0x41fc0000u},
        {0x00000001u,0x00000001u,0x00000116u,0x0000001du,0x420228f6u,0x41fc0000u},
        {0x00000001u,0x00000002u,0x00000150u,0x0000001cu,0x420628f6u,0x42020000u},
        {0x00000001u,0x00000003u,0x00000188u,0x0000001du,0x420628f6u,0x42020000u},
    };
    static uint8_t const masks[]={2,4,0x40,0x80};
    FOR_LOOP(c,8) FOR_LOOP(lane,4) {
        edict_t *unit=make_moving_unit(136,152);
        uint8_t cells[64*64]={0};
        FOR_LOOP(y,64) if (cases[c][0] && (y<29 || y>34)) cells[y*64+32]=0xc6;
        box2_t bounds={{0,0},{2048,2048}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(64,64,cells);
        vec2_t goal={1896,1912},waypoint;
        unit->collision=8+cases[c][1]*16;
        movePathQuery_t query={{&unit->s.origin2,&goal,unit->collision,masks[lane]},unit,NULL,true};
        moveFineRoute_t *route=&unit->movement.fine_route;
        T_ASSERT(G_BuildUnitMoveFineRoute(&query,route,&waypoint));
        T_EQ(route->adaptive_count,cases[c][0]?8:4);
        T_EQ(route->adaptive_index,cases[c][0]?5:1);
        /* Supply the same terminal fine index and published native source as the original caller. */
        route->index=0;
        vec2_t source={wc3_float(cases[c][4]),wc3_float(cases[c][5])};
        unit->s.origin2=(vec2_t){wc3_mul(source.x,32),wc3_mul(source.y,32)};
        query.fine=&source;
        if (!G_AdvanceUnitMoveFineRoute(&query,route,&waypoint))
            T_ASSERT(G_BuildUnitMoveFineRoute(&query,route,&waypoint));
        T_EQ(route->adaptive_index,0);
        T_EQ(route->count,cases[c][3]);
        T_EQ(route->index,cases[c][3]-2);
        if (route->count==cases[c][3]) FOR_LOOP(i,route->count) {
            T_EQ(wc3_float_bits(route->points[i].x),progress_words[cases[c][2]+2*i]);
            T_EQ(wc3_float_bits(route->points[i].y),progress_words[cases[c][2]+2*i+1]);
        }
    }
    reset_entities(); setup_test_world();
}

/* Cache identity must include the map bake epoch, lane and size selected by each mover. */
TEST(wc3_movement, retail_adaptive_lanes_sizes_and_terrain_edits) {
    edict_t *unit=make_moving_unit(272,304);
    uint8_t cells[64*64]={0};
    FOR_LOOP(y,64) FOR_LOOP(x,2) if (y<44 || y>=52) cells[y*64+32+x]=0xc6;
    box2_t bounds={{0,0},{2048,2048}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(64,64,cells);
    vec2_t target={1936,1776}, waypoint, before;
    uint8_t masks[4]={2,4,0x40,0x80};
    FOR_LOOP(round,2) FOR_LOOP(lane,4) FOR_LOOP(size,2) {
        movePathQuery_t query={{&unit->s.origin2,&target,size?32:31,masks[lane]},unit,NULL,true};
        T_ASSERT(G_FindUnitMovePathWaypoint(&query,&waypoint));
        movePathQuery_t leg=query; leg.geometry.target=&waypoint;
        T_ASSERT(G_UnitMovePathLineIsPathable(&leg));
    }
    movePathQuery_t query={{&unit->s.origin2,&target,31,2},unit,NULL,true};
    T_ASSERT(G_FindUnitMovePathWaypoint(&query,&before));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nlocal integer y=0\n"
        "loop\nexitwhen y==64\ncall SetTerrainPathable(1040,y*32+16,ConvertPathingType(1),y>=12 and y<20)\n"
        "call SetTerrainPathable(1072,y*32+16,ConvertPathingType(1),y>=12 and y<20)\nset y=y+1\nendloop\nendfunction\n"));
    T_ASSERT(G_FindUnitMovePathWaypoint(&query,&waypoint));
    T_ASSERT(waypoint.y<before.y);
    movePathQuery_t leg=query; leg.geometry.target=&waypoint;
    T_ASSERT(G_UnitMovePathLineIsPathable(&leg));
    query.geometry.blocked_flags=4;
    T_ASSERT(G_FindUnitMovePathWaypoint(&query,&waypoint));
    T_EQ(wc3_float_bits(waypoint.x),wc3_float_bits(before.x));
    T_EQ(wc3_float_bits(waypoint.y),wc3_float_bits(before.y));
    reset_entities(); setup_test_world();
}

/* Full original16c150/165ae0 starts with fine index count-2; lookahead occurs after .49-cell approach. */
TEST(wc3_movement, retail_fine_route_first_point_is_not_skipped) {
    reset_entities(); setup_test_world();
    uint8_t cells[16*16]={0};
    for (int y=2;y<=6;y++) cells[y*16+6]=2;
    box2_t bounds={{0,0},{512,512}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(16,16,cells);
    T_ASSERT(run_test_jass("globals\nunit mover\nendglobals\nfunction main takes nothing returns nothing\n"
        "set mover=CreateUnit(Player(0),'hRTE',128,128,0)\nendfunction\n"
        "function go takes nothing returns nothing\ncall IssuePointOrder(mover,\"move\",256,128)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','R','T','E')) unit=ent;
    T_NOT_NULL(unit); if (!unit) return;
    T_EQ(unit->collision,8); T_EQ(unit_current_speed(unit),256);
    T_EQ(unit->data.UnitData->turnRate,0.6f); T_EQ(unit->data.UnitData->propWin,60);
    unit_stand(unit); jass_callbyname(level.vm,"go",false); T_ASSERT(!jass_rterror_pending(level.vm));
    unit_changeangle(unit);
    T_ASSERT(unit->movement.path.valid);
    T_EQ(wc3_float_bits(unit->movement.path.waypoint.x),wc3_float_bits(176));
    T_EQ(wc3_float_bits(unit->movement.path.waypoint.y),wc3_float_bits(112));
    reset_entities(); setup_test_world();
}


/* Complete original controlled singleton wall trajectory; clock is advanced by the same1/32 producer input. */
TEST(wc3_movement, retail_fine_route_wall_trajectory_words) {
    static uint32_t const expected[34][6]={
        {0x408796e7u,0x407af0bau,0x40f2dc43u,0xc021ec63u,0x40bec3dcu,0x00000007u},
        {0x408f2dc9u,0x4075e156u,0x40f2dbe3u,0xc021eea1u,0x40bec3b6u,0x00000007u},
        {0x4096c4a8u,0x4070d1e0u,0x40f2dd02u,0xc021e7e6u,0x40bec428u,0x00000007u},
        {0x409e5b90u,0x406bc2a0u,0x40f2dca3u,0xc021ea24u,0x40bec402u,0x00000007u},
        {0x40a5f275u,0x4066b34eu,0x00000000u,0x00000000u,0x40aec402u,0x00000005u},
        {0x40a5f275u,0x4066b34eu,0x00000000u,0x00000000u,0x409ec402u,0x00000005u},
        {0x40a5f275u,0x4066b34eu,0x3f972a78u,0xc0fd31f3u,0x409b89a8u,0x00000005u},
        {0x40a720c9u,0x4056e02eu,0x3f972c07u,0xc0fd31e4u,0x409b89b4u,0x00000005u},
        {0x40a84f21u,0x40470d0fu,0x3f972c07u,0xc0fd31e4u,0x409b89b4u,0x00000005u},
        {0x40a97d79u,0x403739f0u,0x3f972c07u,0xc0fd31e4u,0x409b89b4u,0x00000005u},
        {0x40aaabd1u,0x402766d1u,0x3f972c07u,0xc0fd31e4u,0x409b89b4u,0x00000005u},
        {0x40abda29u,0x401793b2u,0x3f972c07u,0xc0fd31e4u,0x409b89b4u,0x00000005u},
        {0x40ad0881u,0x4007c093u,0x3f972c07u,0xc0fd31e4u,0x409b89b4u,0x00000005u},
        {0x40ae36d9u,0x3fefdae9u,0x00000000u,0x00000000u,0x40ab89b4u,0x00000003u},
        {0x40ae36d9u,0x3fefdae9u,0x00000000u,0x00000000u,0x40bb89b4u,0x00000003u},
        {0x40ae36d9u,0x3fefdae9u,0x40fbddbdu,0xbfb746e0u,0x40c34de0u,0x00000003u},
        {0x40b615c6u,0x3fea20b2u,0x40fbde4du,0xbfb73a76u,0x40c34e45u,0x00000003u},
        {0x40bdf4b8u,0x3fe466deu,0x40fbde4du,0xbfb73a76u,0x40c34e45u,0x00000003u},
        {0x40c5d3aau,0x3fdead0au,0x40fbdebau,0xbfb73127u,0x40c34e91u,0x00000003u},
        {0x40cdb29fu,0x3fd8f380u,0x40fbde72u,0xbfb7375cu,0x40c34e5fu,0x00000003u},
        {0x40d59192u,0x3fd339c5u,0x40fbdde1u,0xbfb743c5u,0x40c34df9u,0x00000003u},
        {0x40dd7081u,0x3fcd7fa6u,0x40fbde72u,0xbfb7375cu,0x40c34e5fu,0x00000003u},
        {0x40e54f74u,0x3fc7c5ebu,0x00000000u,0x00000000u,0x3ea3e863u,0x00000002u},
        {0x40e54f74u,0x3fc7c5ebu,0x00000000u,0x00000000u,0x3f51f431u,0x00000002u},
        {0x40e54f74u,0x3fc7c5ebu,0x402b90a8u,0x40f13323u,0x3f9d5310u,0x00000002u},
        {0x40e7fdb6u,0x3fe5ec4fu,0x402b8feau,0x40f13345u,0x3f9d5343u,0x00000002u},
        {0x40eaabf5u,0x4002095bu,0x402b90a8u,0x40f13323u,0x3f9d5310u,0x00000002u},
        {0x40ed5a37u,0x40111c8du,0x3f092a94u,0x40ff6cd4u,0x3fc07b8cu,0x00000001u},
        {0x40ede361u,0x4021135au,0x3f09276eu,0x40ff6cdbu,0x3fc07bbeu,0x00000001u},
        {0x40ee6c88u,0x40310a27u,0x3f09276eu,0x40ff6cdbu,0x3fc07bbeu,0x00000001u},
        {0x40eef5afu,0x404100f4u,0x40739ec0u,0x40e129abu,0x3f89964cu,0x00000000u},
        {0x40f2c42au,0x404f138eu,0x40739ec0u,0x40e129abu,0x3f89964cu,0x00000000u},
        {0x40f692a5u,0x405d2628u,0x40739f72u,0x40e1297bu,0x3f899619u,0x00000000u},
        {0x40fa6122u,0x406b38bfu,0x00000000u,0x00000000u,0x3f899619u,0xffffffffu}
    };
    /* Original motion/path points are native fine coordinates. Their words
     * are independent of the world projection; test translated publications
     * against the same authoritative trace rather than rounded world deltas. */
    static vec2_t const origins[]={{0,0},{-256,-256},{-2048,512},{.125f,-19.25f}};
    FOR_LOOP(k,sizeof(origins)/sizeof(*origins)) {
        vec2_t origin=origins[k];
        reset_entities(); setup_test_world();
        level.waypoints=(typeof(level.waypoints)){0};
        level.pathing_clock=(wc3Clock_t){0,0,8}; level.time=0;
        uint8_t cells[16*16]={0};
        for (int y=2;y<=6;y++) cells[y*16+6]=2;
        box2_t bounds={origin,{origin.x+512,origin.y+512}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(16,16,cells);
        char script[1024];
        snprintf(script,sizeof(script),"globals\nunit mover\nendglobals\nfunction main takes nothing returns nothing\n"
            "set mover=CreateUnit(Player(0),'hRTE',%.9g,%.9g,0)\ncall SetUnitTurnSpeed(mover,0.5)\ncall SetUnitPropWindow(mover,0.5)\nendfunction\n"
            "function go takes nothing returns nothing\ncall IssuePointOrder(mover,\"move\",%.9g,%.9g)\nendfunction\n",
            origin.x+128,origin.y+128,origin.x+256,origin.y+128);
        T_ASSERT(run_test_jass(script));
        edict_t *unit=NULL;
        FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','R','T','E')) unit=ent;
        T_NOT_NULL(unit); if (!unit) return;
        unit_stand(unit); jass_callbyname(level.vm,"go",false);
        level.scheduled_think=true; level.pathing_clock=(wc3Clock_t){0,0,8}; level.time=0;
        unit->currentmove->think(unit);
        cstring_t file="/tmp/openwarcraft3-retail-fine-route-save.bin";
        FOR_LOOP(i,34) {
            level.pathing_clock.time=(i+1)/32.f; level.time=(i+1)*32;
            S_PublishMovement(unit); unit->currentmove->think(unit);
            T_EQ(wc3_float_bits(unit->movement.fine_pose.x),expected[i][0]);
            T_EQ(wc3_float_bits(unit->movement.fine_pose.y),expected[i][1]);
            T_EQ(wc3_float_bits(wc3_mul(unit->movement.velocity.x,1/32.f)),expected[i][2]);
            T_EQ(wc3_float_bits(wc3_mul(unit->movement.velocity.y,1/32.f)),expected[i][3]);
            T_EQ(wc3_float_bits(unit->s.angle),expected[i][4]);
            if (i<33) T_EQ(unit->movement.fine_route.index,expected[i][5]);
            if (i==11) T_ASSERT(WriteGame(file));
        }
        T_EQ(unit->current_order_id,0);
        T_ASSERT(ReadGame(file));
        /* Save clears this transient dispatch flag; a real owner callback installs it again. */
        level.scheduled_think=true;
        T_NOT_NULL(unit->movement.fine_route.points);
        for (int i=12;i<34;i++) {
            level.pathing_clock.time=(i+1)/32.f; level.time=(i+1)*32;
            S_PublishMovement(unit); unit->currentmove->think(unit);
            T_EQ(wc3_float_bits(unit->movement.fine_pose.x),expected[i][0]);
            T_EQ(wc3_float_bits(unit->movement.fine_pose.y),expected[i][1]);
            T_EQ(wc3_float_bits(wc3_mul(unit->movement.velocity.x,1/32.f)),expected[i][2]);
            T_EQ(wc3_float_bits(wc3_mul(unit->movement.velocity.y,1/32.f)),expected[i][3]);
            T_EQ(wc3_float_bits(unit->s.angle),expected[i][4]);
            if (i<33) T_EQ(unit->movement.fine_route.index,expected[i][5]);
        }
        T_EQ(unit->current_order_id,0); remove(file);
        level.scheduled_think=false; reset_entities(); setup_test_world();
    }
}

/* Original15aa80 detour under six authentic5ms clock advances per owner pass.
 * Supply the same fresh callback atclock0, then use actual engine frames. */
TEST(wc3_movement, retail_primary_owner_wall_trajectory_words) {
    static uint32_t const primary_expected[34][9]={
        {0x4087492fu,0x407b248au,0x40f2dca3u,0xc021ea24u,0x40bec402u,0x00000007u,0x3cf5c28eu,0x00000000u,0x41000000u},
        {0x408e925cu,0x40764909u,0x40f2dce2u,0xc021e8a5u,0x40bec41bu,0x00000007u,0x3d75c28du,0x00000000u,0x41000000u},
        {0x4095db8bu,0x40716d93u,0x40f2dcc3u,0xc021e965u,0x40bec40fu,0x00000007u,0x3db851e7u,0x00000000u,0x41000000u},
        {0x409d24b9u,0x406c9217u,0x40f2dca3u,0xc021ea24u,0x40bec402u,0x00000007u,0x3df5c287u,0x00000000u,0x41000000u},
        {0x40a46de6u,0x4067b696u,0x00000000u,0x00000000u,0x40aec402u,0x00000005u,0x3e199993u,0x00000000u,0x41000000u},
        {0x40a46de6u,0x4067b696u,0x00000000u,0x00000000u,0x409ec402u,0x00000005u,0x3e3851e3u,0x00000000u,0x41000000u},
        {0x40a46de6u,0x4067b696u,0x3fac1ee0u,0xc0fc5b90u,0x409c336du,0x00000005u,0x3e570a33u,0x00000000u,0x41000000u},
        {0x40a5b85eu,0x40589260u,0x3fac1ee0u,0xc0fc5b90u,0x409c336du,0x00000005u,0x3e75c283u,0x00000000u,0x41000000u},
        {0x40a702d6u,0x40496e2au,0x3fac1ee0u,0xc0fc5b90u,0x409c336du,0x00000005u,0x3e8a3d69u,0x00000000u,0x41000000u},
        {0x40a84d4eu,0x403a49f4u,0x3fac1ee0u,0xc0fc5b90u,0x409c336du,0x00000005u,0x3e999991u,0x00000000u,0x41000000u},
        {0x40a997c6u,0x402b25beu,0x3fac206du,0xc0fc5b7fu,0x409c3379u,0x00000005u,0x3ea8f5b9u,0x00000000u,0x41000000u},
        {0x40aae241u,0x401c0189u,0x3fac206du,0xc0fc5b7fu,0x409c3379u,0x00000005u,0x3eb851e1u,0x00000000u,0x41000000u},
        {0x40ac2cbcu,0x400cdd54u,0x3fac206du,0xc0fc5b7fu,0x409c3379u,0x00000005u,0x3ec7ae09u,0x00000000u,0x41000000u},
        {0x40ad7737u,0x3ffb723eu,0x00000000u,0x00000000u,0x40ac3379u,0x00000003u,0x3ed70a31u,0x00000000u,0x41000000u},
        {0x40ad7737u,0x3ffb723eu,0x00000000u,0x00000000u,0x40bc3379u,0x00000003u,0x3ee66659u,0x00000000u,0x41000000u},
        {0x40ad7737u,0x3ffb723eu,0x40f9d706u,0xbfdf489cu,0x40c20737u,0x00000003u,0x3ef5c281u,0x00000000u,0x41000000u},
        {0x40b4f5fbu,0x3ff4bf6du,0x40f9d7e2u,0xbfdf3939u,0x40c207b5u,0x00000003u,0x3f028f54u,0x00000000u,0x41000000u},
        {0x40bc74c6u,0x3fee0d12u,0x40f9d78au,0xbfdf3f60u,0x40c20783u,0x00000003u,0x3f0a3d68u,0x00000000u,0x41000000u},
        {0x40c3f38eu,0x3fe75a87u,0x40f9d83au,0xbfdf3311u,0x40c207e8u,0x00000003u,0x3f11eb7cu,0x00000000u,0x41000000u},
        {0x40cb725cu,0x3fe0a85bu,0x40f9d732u,0xbfdf4588u,0x40c20750u,0x00000003u,0x3f199990u,0x00000000u,0x41000000u},
        {0x40d2f122u,0x3fd9f5a1u,0x40f9d774u,0xbfdf40eau,0x40c20776u,0x00000003u,0x3f2147a4u,0x00000000u,0x41000000u},
        {0x40da6feau,0x3fd3430bu,0x40f9d7b6u,0xbfdf3c4cu,0x40c2079cu,0x00000003u,0x3f28f5b8u,0x00000000u,0x41000000u},
        {0x40e1eeb4u,0x3fcc9098u,0x00000000u,0x00000000u,0x3e8f7c30u,0x00000002u,0x3f30a3ccu,0x00000000u,0x41000000u},
        {0x40e1eeb4u,0x3fcc9098u,0x00000000u,0x00000000u,0x3f47be18u,0x00000002u,0x3f3851e0u,0x00000000u,0x41000000u},
        {0x40e1eeb4u,0x3fcc9098u,0x4060582eu,0x40e61dafu,0x3f8f016du,0x00000002u,0x3f3ffff4u,0x00000000u,0x41000000u},
        {0x40e54c2fu,0x3fe82dbfu,0x4060582eu,0x40e61dafu,0x3f8f016du,0x00000002u,0x3f47ae08u,0x00000000u,0x41000000u},
        {0x40e8a9aau,0x4001e573u,0x4060582eu,0x40e61dafu,0x3f8f016du,0x00000002u,0x3f4f5c1cu,0x00000000u,0x41000000u},
        {0x40ec0725u,0x400fb406u,0x3f49a250u,0x40fec195u,0x3fbc7079u,0x00000001u,0x3f570a30u,0x00000000u,0x41000000u},
        {0x40ecc8b6u,0x401efd13u,0x3f49a250u,0x40fec195u,0x3fbc7079u,0x00000001u,0x3f5eb844u,0x00000000u,0x41000000u},
        {0x40ed8a47u,0x402e4620u,0x3f49a250u,0x40fec195u,0x3fbc7079u,0x00000001u,0x3f666658u,0x00000000u,0x41000000u},
        {0x40ee4bd8u,0x403d8f2du,0x3f49a250u,0x40fec195u,0x3fbc7079u,0x00000001u,0x3f6e146cu,0x00000000u,0x41000000u},
        {0x40ef0d69u,0x404cd83au,0x408d6677u,0x40d567d2u,0x3f7c51b0u,0x00000000u,0x3f75c280u,0x00000000u,0x41000000u},
        {0x40f34b5cu,0x4059a621u,0x408d66cbu,0x40d5679au,0x3f7c514cu,0x00000000u,0x3f7d7094u,0x00000000u,0x41000000u},
        {0x40f78952u,0x40667405u,0x00000000u,0x00000000u,0x3f7c514cu,0xffffffffu,0x3f828f54u,0x00000000u,0x41000000u},
    };
    reset_entities(); setup_test_world();
    level.waypoints=(typeof(level.waypoints)){0};
    level.pathing_clock=(wc3Clock_t){0,0,8}; level.time=0;
    level.pathing_msec=level.pathing_phase=0; level.pathing_due=false;
    uint8_t cells[16*16]={0};
    for (int y=2;y<=6;y++) cells[y*16+6]=2;
    box2_t bounds={{0,0},{512,512}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(16,16,cells);
    T_ASSERT(run_test_jass("globals\nunit mover\nendglobals\nfunction main takes nothing returns nothing\n"
        "set mover=CreateUnit(Player(0),'hRTE',128,128,0)\ncall SetUnitTurnSpeed(mover,0.5)\ncall SetUnitPropWindow(mover,0.5)\nendfunction\n"
        "function go takes nothing returns nothing\ncall IssuePointOrder(mover,\"move\",256,128)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','R','T','E')) unit=ent;
    T_NOT_NULL(unit); if (!unit) return;
    unit->stand=unit_stand; unit->think=monster_think;
    unit->svflags|=SVF_MONSTER; unit->movetype=MOVETYPE_STEP;
    unit_stand(unit); jass_callbyname(level.vm,"go",false);
    level.scheduled_think=true; unit->currentmove->think(unit); level.scheduled_think=false;
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    cstring_t file="/tmp/openwarcraft3-retail-primary-owner-route-save.bin";
    FOR_LOOP(pass,2) {
        int first=pass?12:0;
        if (pass) { T_ASSERT(ReadGame(file)); T_NOT_NULL(unit->movement.fine_route.points); }
        for (int i=first;i<34;i++) {
            level.time+=30; globals.RunFrame();
            T_EQ(wc3_float_bits(unit->movement.fine_pose.x),primary_expected[i][0]);
            T_EQ(wc3_float_bits(unit->movement.fine_pose.y),primary_expected[i][1]);
            T_EQ(wc3_float_bits(wc3_mul(unit->movement.velocity.x,1/32.f)),primary_expected[i][2]);
            T_EQ(wc3_float_bits(wc3_mul(unit->movement.velocity.y,1/32.f)),primary_expected[i][3]);
            T_EQ(wc3_float_bits(unit->s.angle),primary_expected[i][4]);
            if (i<33) T_EQ(unit->movement.fine_route.index,primary_expected[i][5]);
            T_EQ(wc3_float_bits(level.pathing_clock.time),primary_expected[i][6]);
            T_EQ(level.pathing_clock.epoch,primary_expected[i][7]);
            T_EQ(wc3_float_bits(level.pathing_clock.span),primary_expected[i][8]);
            if (!pass && i==11) T_ASSERT(WriteGame(file));
        }
        T_EQ(unit->current_order_id,0);
    }
    remove(file); level.started=false; reset_entities(); setup_test_world();
}

/* Use the real order owner and scheduler to check retained curves through pause,
 * Stop, replacement and deferred public removal; inactive storage is not an order. */
TEST(wc3_movement, retained_fine_route_public_lifecycle) {
    reset_entities(); setup_test_world();
    uint8_t cells[16*16]={0};
    for (int y=2;y<=6;y++) cells[y*16+6]=2;
    box2_t bounds={{0,0},{512,512}}; CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(16,16,cells);
    T_ASSERT(run_test_jass("globals\nunit mover\nendglobals\nfunction main takes nothing returns nothing\n"
        "set mover=CreateUnit(Player(0),'hRTE',128,128,0)\ncall IssuePointOrder(mover,\"move\",256,128)\nendfunction\n"
        "function freeze takes nothing returns nothing\ncall PauseUnit(mover,true)\nendfunction\n"
        "function resume takes nothing returns nothing\ncall PauseUnit(mover,false)\nendfunction\n"
        "function stop takes nothing returns nothing\ncall IssueImmediateOrder(mover,\"stop\")\nendfunction\n"
        "function replace takes nothing returns nothing\ncall IssuePointOrder(mover,\"move\",128,384)\nendfunction\n"
        "function remove takes nothing returns nothing\ncall RemoveUnit(mover)\nendfunction\n"));
    edict_t *unit=NULL;
    FILTER_EDICTS(ent,ent->inuse && ent->class_id==MAKEFOURCC('h','R','T','E')) unit=ent;
    T_NOT_NULL(unit); if (!unit) return;
    unit->stand=unit_stand; unit->think=monster_think;
    unit->svflags|=SVF_MONSTER; unit->movetype=MOVETYPE_STEP;
    unit_stand(unit); T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){256,128}));
    level.started=level.scriptsConfigured=level.scriptsStarted=true;
    FOR_LOOP(i,3) { level.time+=FRAMETIME; globals.RunFrame(); }
    T_ASSERT(unit->movement.path.valid); T_NOT_NULL(unit->movement.fine_route.points);
    vec2_t frozen=unit->s.origin2; uint32_t index=unit->movement.fine_route.index;
    jass_callbyname(level.vm,"freeze",false);
    FOR_LOOP(i,4) { level.time+=FRAMETIME; globals.RunFrame(); }
    T_EQ(unit->s.origin2.x,frozen.x); T_EQ(unit->s.origin2.y,frozen.y);
    T_EQ(unit->movement.fine_route.index,index);
    jass_callbyname(level.vm,"resume",false);
    FOR_LOOP(i,3) { level.time+=FRAMETIME; globals.RunFrame(); }
    T_ASSERT(Vector2_distance(&frozen,&unit->s.origin2)>0);
    jass_callbyname(level.vm,"stop",false); frozen=unit->s.origin2;
    T_EQ(unit->movement.velocity.x,0); T_EQ(unit->movement.velocity.y,0);
    T_EQ(unit->current_order_id,0);
    FOR_LOOP(i,3) { level.time+=FRAMETIME; globals.RunFrame(); }
    T_EQ(unit->s.origin2.x,frozen.x); T_EQ(unit->s.origin2.y,frozen.y);
    jass_callbyname(level.vm,"replace",false);
    FOR_LOOP(i,3) { level.time+=FRAMETIME; globals.RunFrame(); }
    T_NOT_NULL(unit->goalentity);
    T_EQ(unit->goalentity->s.origin2.y,384); T_ASSERT(unit->current_order_id!=0);
    jass_callbyname(level.vm,"remove",false);
    level.time+=FRAMETIME; globals.RunFrame();
    T_ASSERT(!unit->inuse); T_ASSERT(!unit->movement.fine_route.points);
    T_EQ(unit->movement.fine_route.count,0); T_ASSERT(!jass_rterror_pending(level.vm));
    level.started=false; reset_entities(); setup_test_world();
}

TEST(wc3_movement, retail_oblique_velocity_and_step_words) {
    edict_t *unit = make_moving_unit(320, 320);
    /* Original fixture starts in fine cell10 with a zero world origin. */
    box2_t bounds = {{0, 0}, {4096, 4096}}; CM_SetupTestWorldBounds(&bounds);
    unit->unitinfo.MoveSpeed = 100;
    unit->s.angle = 0.125f;
    unit->movement.flow_direct = true;
    unit_moveindirection(unit);
    T_EQ(wc3_float_bits(unit->s.origin2.x), 0x43a4f603u);
    T_EQ(wc3_float_bits(unit->s.origin2.y), 0x43a09f94u);
    T_EQ(wc3_float_bits(unit->movement.velocity.x), 0x42c67084u);
    T_EQ(wc3_float_bits(unit->movement.velocity.y), 0x41477a18u);
    /* Original160060 reconstructs facing from the committed velocity, not requested0.125. */
    T_EQ(wc3_float_bits(unit->s.angle), 0x3dfffadcu);
    move_reset_progress(unit);
    T_EQ(unit->movement.velocity.x, 0); T_EQ(unit->movement.velocity.y, 0);
}

/* Complete original16fe20/1603d0 commits from retail-native-pose-1.27.json.
 * A nonzero map origin exposes world-space rounding through the real Move entry. */
TEST(wc3_movement, native_fine_pose_retains_original_commits) {
    uint32_t const expected[16][5] = {
        {0x412ec060u, 0x404fca00u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41996710u, 0x410e4a20u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x41e8c740u, 0x41223ca0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42156710u, 0x417c9440u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x423d1728u, 0x41884360u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x425e1a98u, 0x41b56f30u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x4282e558u, 0x41bf6870u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42936710u, 0x41ec9440u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x42a73f1cu, 0x41f68d80u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b7c0d4u, 0x4211dca8u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x42cb98e0u, 0x4216d948u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42dc1a98u, 0x422d6f30u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x42eff2a4u, 0x42326bd0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x43003a2eu, 0x424901b8u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x430a2634u, 0x424dfe58u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x43126710u, 0x42649440u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
    };
    edict_t *unit = make_moving_unit(1, 2);
    uint8_t cells[16 * 16] = {0};
    box2_t bounds = {{-256, -256}, {256, 256}};
    CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(16, 16, cells);
    unit->unitinfo.MoveSpeed = 100; unit->movement.flow_direct = true;
    FOR_LOOP(i, 16) {
        unit->s.angle = (i & 1) ? 0.6f : 0.125f;
        unit_moveindirection(unit);
        T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][0]);
        T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][1]);
        T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][2]);
        T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][3]);
        T_EQ(wc3_float_bits(unit->s.angle), expected[i][4]);
    }
    reset_entities(); setup_test_world();
}

/* Positive world origins discard native pose bits; persist the fine words rather than reprojecting a save. */
TEST(wc3_movement, native_fine_pose_survives_save_and_reposition) {
    uint32_t const expected[16][7] = {
        {0x3fb7b01au, 0x3fe4fca7u, 0xc4fa4280u, 0x440e4fcau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x3fd8b38du, 0x3ffb9290u, 0xc4f93a64u, 0x440fb929u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x400031d3u, 0x4000479bu, 0xc4f7fce3u, 0x441008f3u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4010b38cu, 0x400b928fu, 0xc4f6f4c8u, 0x44117251u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x40248b99u, 0x400e10e2u, 0xc4f5b747u, 0x4411c21cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40350d52u, 0x40195bd6u, 0xc4f4af2bu, 0x44132b7au, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x4048e55fu, 0x401bda29u, 0xc4f371abu, 0x44137b45u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40596718u, 0x4027251du, 0xc4f2698fu, 0x4414e4a3u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x406d3f25u, 0x4029a370u, 0xc4f12c0eu, 0x4415346eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x407dc0deu, 0x4034ee64u, 0xc4f023f3u, 0x44169dccu, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x4088cc75u, 0x40376cb7u, 0xc4eee672u, 0x4416ed96u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40910d51u, 0x4042b7abu, 0xc4edde56u, 0x441856f5u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x409af957u, 0x404535feu, 0xc4eca0d6u, 0x4418a6bfu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40a33a33u, 0x405080f2u, 0xc4eb98bau, 0x441a101eu, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x40ad2639u, 0x4052ff45u, 0xc4ea5b39u, 0x441a5fe8u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40b56715u, 0x405e4a39u, 0xc4e9531eu, 0x441bc947u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
    };
    edict_t *unit = make_moving_unit(-2012, 568);
    uint8_t cells[16 * 16] = {0};
    box2_t bounds = {{-2048, 512}, {-1536, 1024}};
    cstring_t file = "/tmp/openwarcraft3-native-fine-pose-save.bin";
    CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(16, 16, cells);
    unit->unitinfo.MoveSpeed = 100; unit->movement.flow_direct = true;
    FOR_LOOP(i, 16) {
        unit->s.angle = (i & 1) ? 0.6f : 0.125f;
        unit_moveindirection(unit);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.x), expected[i][0]);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.y), expected[i][1]);
        T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][2]);
        T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][3]);
        T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][4]);
        T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][5]);
        T_EQ(wc3_float_bits(unit->s.angle), expected[i][6]);
        T_ASSERT(unit->movement.pose_valid);
        if (i == 7) T_ASSERT(WriteGame(file));
    }
    T_ASSERT(ReadGame(file));
    T_ASSERT(unit->movement.pose_valid);
    FOR_LOOP(k, 8) {
        unsigned i = k + 8;
        unit->s.angle = (i & 1) ? 0.6f : 0.125f;
        unit_moveindirection(unit);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.x), expected[i][0]);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.y), expected[i][1]);
        T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][2]);
        T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][3]);
        T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][4]);
        T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][5]);
        T_EQ(wc3_float_bits(unit->s.angle), expected[i][6]);
    }
    vec2_t fine = unit->movement.fine_pose, world = unit->s.origin2;
    memset(cells, 2, sizeof(cells)); CM_SetupTestPathmap(16, 16, cells);
    unit->s.angle = 0.125f; unit_moveindirection(unit);
    T_ASSERT(unit->movement.pose_valid);
    T_EQ(unit->movement.fine_pose.x, fine.x); T_EQ(unit->movement.fine_pose.y, fine.y);
    T_EQ(unit->s.origin2.x, world.x); T_EQ(unit->s.origin2.y, world.y);
    memset(cells, 0, sizeof(cells)); CM_SetupTestPathmap(16, 16, cells);
    /* External world repositioning must discard stale axes on the next accepted preview. */
    unit->s.origin2 = (vec2_t){-2012, 568}; gi.LinkEntity(unit);
    unit->s.angle = 0.125f; unit_moveindirection(unit);
    T_EQ(wc3_float_bits(unit->movement.fine_pose.x), expected[0][0]);
    T_EQ(wc3_float_bits(unit->movement.fine_pose.y), expected[0][1]);
    T_EQ(wc3_float_bits(unit->s.origin2.x), expected[0][2]);
    T_EQ(wc3_float_bits(unit->s.origin2.y), expected[0][3]);
    world = unit->s.origin2; world.x += 1;
    T_ASSERT(unit_snap_to_point_ignore_units(unit, &world));
    T_ASSERT(!unit->movement.pose_valid);
    remove(file); reset_entities(); setup_test_world();
}

/* Public X/Y wrappers write both native fine axes, even when the requested world word is unchanged. */
TEST(wc3_movement, public_axis_position_retains_original_write_and_next_step_words) {
    uint32_t const expected[4][4] = {
        {0x3fb7b000u, 0x3fe4fca0u, 0xc4fa4280u, 0x440e4fcau},
        {0x3fb7b000u, 0x3fe4fca0u, 0xc4fa4280u, 0x440e4fcau},
        {0x3fb83000u, 0x3fe4fca0u, 0xc4fa3e80u, 0x440e4fcau},
        {0x3fb7b000u, 0x3fe57ca0u, 0xc4fa4280u, 0x440e57cau}
    };
    uint32_t const next[4][7] = {
        {0x3fd8b373u, 0x3ffb9289u, 0xc4f93a65u, 0x440fb928u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x3fd8b373u, 0x3ffb9289u, 0xc4f93a65u, 0x440fb928u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x3fd93373u, 0x3ffb9289u, 0xc4f93665u, 0x440fb928u, 0x42a51143u, 0x4261db20u, 0x3f19994fu},
        {0x3fd8b373u, 0x3ffc1289u, 0xc4f93a65u, 0x440fc128u, 0x42a51143u, 0x4261db20u, 0x3f19994fu}
    };
    cstring_t const calls[4] = {"same_x", "same_y", "shift_x", "shift_y"};
    uint8_t cells[16 * 16] = {0};
    box2_t bounds = {{-2048, 512}, {-1536, 1024}};
    cstring_t file = "/tmp/openwarcraft3-public-axis-pose-save.bin";
    FOR_LOOP(i, 4) {
        reset_entities(); setup_test_world();
        memset(&level.waypoints, 0, sizeof(level.waypoints));
        CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(16, 16, cells);
        T_ASSERT(run_test_jass(
            "globals\n unit axisUnit = null\nendglobals\n"
            "function main takes nothing returns nothing\n"
            " set axisUnit = CreateUnit(Player(0), 'hfoo', -2012.0, 568.0, 0.0)\nendfunction\n"
            "function same_x takes nothing returns nothing\n"
            " call SetUnitX(axisUnit, GetUnitX(axisUnit))\nendfunction\n"
            "function same_y takes nothing returns nothing\n"
            " call SetUnitY(axisUnit, GetUnitY(axisUnit))\nendfunction\n"
            "function shift_x takes nothing returns nothing\n"
            " call SetUnitX(axisUnit, GetUnitX(axisUnit) + 0.125)\nendfunction\n"
            "function shift_y takes nothing returns nothing\n"
            " call SetUnitY(axisUnit, GetUnitY(axisUnit) + 0.125)\nendfunction\n"));
        edict_t *unit = NULL;
        FOR_LOOP(n, globals.num_edicts)
            if (g_edicts[n].inuse && g_edicts[n].class_id == MAKEFOURCC('h','f','o','o')) unit = &g_edicts[n];
        T_NOT_NULL(unit);
        if (!unit) continue;
        unit->collision = 0; unit->movetype = MOVETYPE_STEP; unit->stand = unit_stand; unit_stand(unit);
        unit->unitinfo.MoveSpeed = 100;
        vec2_t target = {-1800, 600}; T_ASSERT(unit_issueorder(unit, "move", &target));
        unit->s.angle = .125f; unit->movement.flow_direct = true; unit_moveindirection(unit);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.x), 0x3fb7b01au);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.y), 0x3fe4fca7u);
        vec2_t velocity = unit->movement.velocity; float facing = unit->s.angle;
        edict_t *goal = unit->goalentity;
        unit->movement.worker_avoid_blocked_frames = 3;
        jass_callbyname(level.vm, calls[i], false);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.x), expected[i][0]);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.y), expected[i][1]);
        T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][2]);
        T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][3]);
        T_EQ(unit->movement.velocity.x, velocity.x); T_EQ(unit->movement.velocity.y, velocity.y);
        T_EQ(unit->s.angle, facing); T_EQ(unit->current_order_id, G_OrderId("move"));
        T_ASSERT(unit->goalentity == goal); T_ASSERT(unit->movement.pose_valid);
        T_EQ(unit->movement.worker_avoid_blocked_frames, 3);
        T_ASSERT(WriteGame(file));
        FOR_LOOP(round, 2) {
            if (round) T_ASSERT(ReadGame(file));
            unit->s.angle = .6f; unit_moveindirection(unit);
            T_EQ(wc3_float_bits(unit->movement.fine_pose.x), next[i][0]);
            T_EQ(wc3_float_bits(unit->movement.fine_pose.y), next[i][1]);
            T_EQ(wc3_float_bits(unit->s.origin2.x), next[i][2]);
            T_EQ(wc3_float_bits(unit->s.origin2.y), next[i][3]);
            T_EQ(wc3_float_bits(unit->movement.velocity.x), next[i][4]);
            T_EQ(wc3_float_bits(unit->movement.velocity.y), next[i][5]);
            T_EQ(wc3_float_bits(unit->s.angle), next[i][6]);
        }
        remove(file);
    }
    reset_entities(); setup_test_world();
}

/* Complete original primary clock:20 five-ms advances per server frame,
 * Move commits each sixth advance before the next one; queries predict the residue. */
static void fixed_oblique_clock_move(edict_t *unit) {
    /* Constant requested input for the original kernel trajectory. */
    unit->s.angle = .125f;
    unit_moveindirection(unit);
}

static umove_t fixed_oblique_clock_walk = { .animation = "walk",
    .think = fixed_oblique_clock_move, .proc = CAbilityMove,
    .scheduled_think = true, .sample_pose = S_PublishMovement };

TEST(wc3_movement, primary_clock_and_previous_velocity_match_fixed_oblique_frames) {
    /* Complete original producer/mover/query words from retail-primary-clock-trajectory. */
    uint32_t const expected[300][7] = {
        {0x3fa7d00eu, 0x3fe2fdfcu, 0xc4faa1c0u, 0x440e37dau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x3fcb8822u, 0x3fe77af6u, 0xc4f9643fu, 0x440e87a4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x3ffb283du, 0x3fed76eeu, 0xc4f826bfu, 0x440ed76eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x400f7028u, 0x3ff1f3e8u, 0xc4f6e93eu, 0x440f2739u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40214c31u, 0x3ff670e2u, 0xc4f5abbdu, 0x440f7703u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40391c3du, 0x3ffc6cdau, 0xc4f46e3du, 0x440fc6cdu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x404af846u, 0x400074eau, 0xc4f330bcu, 0x44101697u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x405cd44fu, 0x4002b367u, 0xc4f1f33bu, 0x44106662u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4074a45bu, 0x4005b163u, 0xc4f0b5bbu, 0x4410b62cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40834031u, 0x4007efe0u, 0xc4ef783au, 0x441105f6u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x408c2e34u, 0x400a2e5du, 0xc4ee3abau, 0x441155c0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40981638u, 0x400d2c59u, 0xc4ecfd39u, 0x4411a58bu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40a1043bu, 0x400f6ad6u, 0xc4ebbfb9u, 0x4411f555u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40a9f23eu, 0x4011a953u, 0xc4ea8239u, 0x4412451fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40b5da42u, 0x4014a74fu, 0xc4e944b8u, 0x441294e9u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40bec845u, 0x4016e5ccu, 0xc4e80738u, 0x4412e4b4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40c7b648u, 0x40192449u, 0xc4e6c9b7u, 0x4413347eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40d39e4cu, 0x401c2245u, 0xc4e58c37u, 0x44138448u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40dc8c4fu, 0x401e60c2u, 0xc4e44eb7u, 0x4413d412u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40e57a52u, 0x40209f3fu, 0xc4e31136u, 0x441423ddu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40f16249u, 0x40239d38u, 0xc4e1d3b7u, 0x441473a7u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x40fa5040u, 0x4025dbb2u, 0xc4e09639u, 0x4414c370u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41019f1bu, 0x40281a2cu, 0xc4df58bau, 0x4415133au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41079313u, 0x402b1824u, 0xc4de1b3cu, 0x44156304u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x410c0a0du, 0x402d569eu, 0xc4dcddbdu, 0x4415b2ceu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41108107u, 0x402f9518u, 0xc4dba03fu, 0x44160298u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x411674ffu, 0x40329310u, 0xc4da62c1u, 0x44165262u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x411aebf9u, 0x4034d18au, 0xc4d92542u, 0x4416a22bu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x411f62f3u, 0x40371004u, 0xc4d7e7c4u, 0x4416f1f5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x412556ebu, 0x403a0dfcu, 0xc4d6aa46u, 0x441741bfu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4129cde5u, 0x403c4c76u, 0xc4d56cc7u, 0x44179189u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x412e44dfu, 0x403e8af0u, 0xc4d42f49u, 0x4417e153u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x413438d7u, 0x404188e8u, 0xc4d2f1cbu, 0x4418311du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4138afd1u, 0x4043c762u, 0xc4d1b44cu, 0x441880e6u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x413d26cbu, 0x404605dcu, 0xc4d076ceu, 0x4418d0b0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41431ac3u, 0x404903d4u, 0xc4cf3950u, 0x4419207au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x414791bdu, 0x404b424eu, 0xc4cdfbd1u, 0x44197044u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x414c08b7u, 0x404d80c8u, 0xc4ccbe53u, 0x4419c00eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4151fcafu, 0x40507ec0u, 0xc4cb80d5u, 0x441a0fd8u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x415673a9u, 0x4052bd3au, 0xc4ca4356u, 0x441a5fa1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x415aea96u, 0x4054fbacu, 0xc4c905dcu, 0x441aaf6au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4160de7au, 0x4057f998u, 0xc4c7c862u, 0x441aff33u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41655565u, 0x405a3809u, 0xc4c68ae8u, 0x441b4efbu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4169cc50u, 0x405c767au, 0xc4c54d6eu, 0x441b9ec4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x416fc034u, 0x405f7466u, 0xc4c40ff3u, 0x441bee8cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4174371fu, 0x4061b2d7u, 0xc4c2d279u, 0x441c3e55u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4178ae0au, 0x4063f148u, 0xc4c194ffu, 0x441c8e1du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x417ea1eeu, 0x4066ef34u, 0xc4c05785u, 0x441cdde6u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41818c6bu, 0x40692da5u, 0xc4bf1a0cu, 0x441d2dafu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4183c7dfu, 0x406b6c16u, 0xc4bddc92u, 0x441d7d77u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4186c1cfu, 0x406e6a02u, 0xc4bc9f19u, 0x441dcd40u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4188fd43u, 0x4070a873u, 0xc4bb61a0u, 0x441e1d08u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x418b38b7u, 0x4072e6e4u, 0xc4ba2426u, 0x441e6cd1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x418e32a7u, 0x4075e4d0u, 0xc4b8e6adu, 0x441ebc9au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41906e1bu, 0x40782341u, 0xc4b7a934u, 0x441f0c62u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4192a98fu, 0x407a61b2u, 0xc4b66bbau, 0x441f5c2bu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4195a37fu, 0x407d5f9eu, 0xc4b52e41u, 0x441fabf3u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4197def3u, 0x407f9e0fu, 0xc4b3f0c8u, 0x441ffbbcu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x419a1a67u, 0x4080ee3fu, 0xc4b2b34eu, 0x44204b84u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x419d1457u, 0x40826d33u, 0xc4b175d5u, 0x44209b4cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x419f4fcbu, 0x40838c6au, 0xc4b0385cu, 0x4420eb14u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41a18b3fu, 0x4084aba1u, 0xc4aefae2u, 0x44213addu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41a4852fu, 0x40862a95u, 0xc4adbd69u, 0x44218aa5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41a6c0a3u, 0x408749ccu, 0xc4ac7ff0u, 0x4421da6du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41a8fc17u, 0x40886903u, 0xc4ab4276u, 0x44222a35u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41abf607u, 0x4089e7f7u, 0xc4aa04fdu, 0x442279fdu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41ae317bu, 0x408b072eu, 0xc4a8c784u, 0x4422c9c5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41b06cefu, 0x408c2665u, 0xc4a78a0au, 0x4423198eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41b366dfu, 0x408da559u, 0xc4a64c91u, 0x44236956u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41b5a253u, 0x408ec490u, 0xc4a50f18u, 0x4423b91eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41b7ddc7u, 0x408fe3c7u, 0xc4a3d19eu, 0x442408e6u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41bad7b7u, 0x409162bbu, 0xc4a29425u, 0x442458aeu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41bd132bu, 0x409281f2u, 0xc4a156acu, 0x4424a876u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41bf4e9fu, 0x4093a129u, 0xc4a01932u, 0x4424f83fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41c2488fu, 0x4095201du, 0xc49edbb9u, 0x44254807u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41c48403u, 0x40963f54u, 0xc49d9e40u, 0x442597cfu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41c6bf77u, 0x40975e8bu, 0xc49c60c6u, 0x4425e797u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41c9b967u, 0x4098dd7fu, 0xc49b234du, 0x4426375fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41cbf4dbu, 0x4099fcb6u, 0xc499e5d4u, 0x44268727u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41ce304fu, 0x409b1bedu, 0xc498a85au, 0x4426d6f0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41d12a32u, 0x409c9adbu, 0xc4976ae7u, 0x442726b6u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41d3659au, 0x409dba0cu, 0xc4962d75u, 0x4427767du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41d5a102u, 0x409ed93du, 0xc494f002u, 0x4427c643u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41d89ae2u, 0x40a05829u, 0xc493b28fu, 0x4428160au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41dad64au, 0x40a1775au, 0xc492751du, 0x442865d0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41dd11b2u, 0x40a2968bu, 0xc49137aau, 0x4428b597u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41e00b92u, 0x40a41577u, 0xc48ffa37u, 0x4429055du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41e246fau, 0x40a534a8u, 0xc48ebcc5u, 0x44295524u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41e48262u, 0x40a653d9u, 0xc48d7f52u, 0x4429a4eau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41e77c42u, 0x40a7d2c5u, 0xc48c41dfu, 0x4429f4b1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41e9b7aau, 0x40a8f1f6u, 0xc48b046du, 0x442a4477u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41ebf312u, 0x40aa1127u, 0xc489c6fau, 0x442a943eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41eeecf2u, 0x40ab9013u, 0xc4888987u, 0x442ae404u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41f1285au, 0x40acaf44u, 0xc4874c15u, 0x442b33cbu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41f363c2u, 0x40adce75u, 0xc4860ea2u, 0x442b8391u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41f65da2u, 0x40af4d61u, 0xc484d12fu, 0x442bd358u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41f8990au, 0x40b06c92u, 0xc48393bdu, 0x442c231eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41fad472u, 0x40b18bc3u, 0xc482564au, 0x442c72e5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x41fdce52u, 0x40b30aafu, 0xc48118d7u, 0x442cc2abu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x420004ddu, 0x40b429e0u, 0xc47fb6cau, 0x442d1272u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42012291u, 0x40b54911u, 0xc47d3be4u, 0x442d6238u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42029f81u, 0x40b6c7fdu, 0xc47ac0feu, 0x442db1ffu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4203bd35u, 0x40b7e72eu, 0xc478461au, 0x442e01c5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4204dae9u, 0x40b9065fu, 0xc475cb34u, 0x442e518cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x420657d9u, 0x40ba854bu, 0xc473504eu, 0x442ea152u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4207758du, 0x40bba47cu, 0xc470d56au, 0x442ef119u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42089341u, 0x40bcc3adu, 0xc46e5a84u, 0x442f40dfu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x420a1031u, 0x40be4299u, 0xc46bdf9eu, 0x442f90a6u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x420b2de5u, 0x40bf61cau, 0xc46964bau, 0x442fe06cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x420c4b99u, 0x40c080fbu, 0xc466e9d4u, 0x44303033u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x420dc889u, 0x40c1ffe7u, 0xc4646eeeu, 0x44307ff9u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x420ee63du, 0x40c31f18u, 0xc461f40au, 0x4430cfc0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421003f1u, 0x40c43e49u, 0xc45f7924u, 0x44311f86u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421180e1u, 0x40c5bd35u, 0xc45cfe3eu, 0x44316f4du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42129e95u, 0x40c6dc66u, 0xc45a835au, 0x4431bf13u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4213bc49u, 0x40c7fb97u, 0xc4580874u, 0x44320edau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42153939u, 0x40c97a83u, 0xc4558d8eu, 0x44325ea0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421656edu, 0x40ca99b4u, 0xc45312aau, 0x4432ae67u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421774a1u, 0x40cbb8e5u, 0xc45097c4u, 0x4432fe2du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4218f191u, 0x40cd37d1u, 0xc44e1cdeu, 0x44334df4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421a0f45u, 0x40ce5702u, 0xc44ba1fau, 0x44339dbau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421b2cf9u, 0x40cf7633u, 0xc4492714u, 0x4433ed81u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421ca9e9u, 0x40d0f51fu, 0xc446ac2eu, 0x44343d47u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421dc79du, 0x40d21450u, 0xc444314au, 0x44348d0eu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x421ee551u, 0x40d33381u, 0xc441b664u, 0x4434dcd4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42206241u, 0x40d4b26du, 0xc43f3b7eu, 0x44352c9bu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42217ff5u, 0x40d5d19eu, 0xc43cc09au, 0x44357c61u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42229da9u, 0x40d6f0cfu, 0xc43a45b4u, 0x4435cc28u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42241a99u, 0x40d86fbbu, 0xc437caceu, 0x44361beeu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4225384du, 0x40d98eecu, 0xc4354feau, 0x44366bb5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42265601u, 0x40daae1du, 0xc432d504u, 0x4436bb7bu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4227d2f1u, 0x40dc2d09u, 0xc4305a1eu, 0x44370b42u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4228f0a5u, 0x40dd4c3au, 0xc42ddf3au, 0x44375b08u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x422a0e59u, 0x40de6b6bu, 0xc42b6454u, 0x4437aacfu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x422b8b49u, 0x40dfea57u, 0xc428e96eu, 0x4437fa95u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x422ca8fdu, 0x40e10988u, 0xc4266e8au, 0x44384a5cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x422dc6b1u, 0x40e228b9u, 0xc423f3a4u, 0x44389a22u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x422f43a1u, 0x40e3a7a5u, 0xc42178beu, 0x4438e9e9u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42306155u, 0x40e4c6d6u, 0xc41efddau, 0x443939afu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42317f09u, 0x40e5e607u, 0xc41c82f4u, 0x44398976u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4232fbf9u, 0x40e764f3u, 0xc41a080eu, 0x4439d93cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x423419adu, 0x40e88424u, 0xc4178d2au, 0x443a2903u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42353761u, 0x40e9a355u, 0xc4151244u, 0x443a78c9u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4236b451u, 0x40eb2241u, 0xc412975eu, 0x443ac890u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4237d205u, 0x40ec4172u, 0xc4101c7au, 0x443b1856u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4238efb9u, 0x40ed60a3u, 0xc40da194u, 0x443b681du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x423a6ca9u, 0x40eedf8fu, 0xc40b26aeu, 0x443bb7e3u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x423b8a5du, 0x40effec0u, 0xc408abcau, 0x443c07aau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x423ca811u, 0x40f11df1u, 0xc40630e4u, 0x443c5770u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x423e2501u, 0x40f29cddu, 0xc403b5feu, 0x443ca737u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x423f42b5u, 0x40f3bc0eu, 0xc4013b1au, 0x443cf6fdu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42406069u, 0x40f4db3fu, 0xc3fd8068u, 0x443d46c4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4241dd59u, 0x40f65a2bu, 0xc3f88a9cu, 0x443d968au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4242fb0du, 0x40f7795cu, 0xc3f394d4u, 0x443de651u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x424418c1u, 0x40f8988du, 0xc3ee9f08u, 0x443e3617u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x424595b1u, 0x40fa1779u, 0xc3e9a93cu, 0x443e85deu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4246b365u, 0x40fb36aau, 0xc3e4b374u, 0x443ed5a4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4247d119u, 0x40fc55dbu, 0xc3dfbda8u, 0x443f256bu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42494e09u, 0x40fdd4c7u, 0xc3dac7dcu, 0x443f7531u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x424a6bbdu, 0x40fef3f8u, 0xc3d5d214u, 0x443fc4f8u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x424b8971u, 0x41000994u, 0xc3d0dc48u, 0x444014beu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x424d0661u, 0x4100c908u, 0xc3cbe67cu, 0x44406484u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x424e2415u, 0x4101589fu, 0xc3c6f0b4u, 0x4440b449u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x424f41c9u, 0x4101e836u, 0xc3c1fae8u, 0x4441040fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4250beb9u, 0x4102a7aau, 0xc3bd051cu, 0x444153d5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4251dc6du, 0x41033741u, 0xc3b80f54u, 0x4441a39au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4252fa21u, 0x4103c6d8u, 0xc3b31988u, 0x4441f360u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42547711u, 0x4104864cu, 0xc3ae23bcu, 0x44424326u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x425594c5u, 0x410515e3u, 0xc3a92df4u, 0x444292ebu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4256b279u, 0x4105a57au, 0xc3a43828u, 0x4442e2b1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42582f69u, 0x410664eeu, 0xc39f425cu, 0x44433277u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42594d1du, 0x4106f485u, 0xc39a4c94u, 0x4443823cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x425a6ad1u, 0x4107841cu, 0xc39556c8u, 0x4443d202u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x425be7c1u, 0x41084390u, 0xc39060fcu, 0x444421c8u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x425d0575u, 0x4108d327u, 0xc38b6b34u, 0x4444718du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x425e2329u, 0x410962beu, 0xc3867568u, 0x4444c153u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x425fa019u, 0x410a2232u, 0xc3817f9cu, 0x44451119u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4260bdcdu, 0x410ab1c9u, 0xc37913a8u, 0x444560deu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4261db81u, 0x410b4160u, 0xc36f2810u, 0x4445b0a4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42635871u, 0x410c00d4u, 0xc3653c78u, 0x4446006au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42647625u, 0x410c906bu, 0xc35b50e8u, 0x4446502fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x426593d9u, 0x410d2002u, 0xc3516550u, 0x44469ff5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x426710c9u, 0x410ddf76u, 0xc34779b8u, 0x4446efbbu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42682e7du, 0x410e6f0du, 0xc33d8e28u, 0x44473f80u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42694c31u, 0x410efea4u, 0xc333a290u, 0x44478f46u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x426ac921u, 0x410fbe18u, 0xc329b6f8u, 0x4447df0cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x426be6d5u, 0x41104dafu, 0xc31fcb68u, 0x44482ed1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x426d0489u, 0x4110dd46u, 0xc315dfd0u, 0x44487e97u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x426e8179u, 0x41119cbau, 0xc30bf438u, 0x4448ce5du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x426f9f2du, 0x41122c51u, 0xc30208a8u, 0x44491e22u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4270bce1u, 0x4112bbe8u, 0xc2f03a20u, 0x44496de8u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x427239d1u, 0x41137b5cu, 0xc2dc62f0u, 0x4449bdaeu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42735785u, 0x41140af3u, 0xc2c88bd0u, 0x444a0d73u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42747539u, 0x41149a8au, 0xc2b4b4a0u, 0x444a5d39u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4275f229u, 0x411559feu, 0xc2a0dd70u, 0x444aacffu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42770fddu, 0x4115e995u, 0xc28d0650u, 0x444afcc4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42782d91u, 0x4116792cu, 0xc2725e40u, 0x444b4c8au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4279aa81u, 0x411738a0u, 0xc24aafe0u, 0x444b9c50u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x427ac835u, 0x4117c837u, 0xc22301a0u, 0x444bec15u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x427be5e9u, 0x411857ceu, 0xc1f6a680u, 0x444c3bdbu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x427d62d9u, 0x41191742u, 0xc1a749c0u, 0x444c8ba1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x427e808du, 0x4119a6d9u, 0xc12fda80u, 0x444cdb66u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x427f9e41u, 0x411a3670u, 0xbf890800u, 0x444d2b2cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42808d98u, 0x411af5e4u, 0x410d9800u, 0x444d7af2u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42811c72u, 0x411b857bu, 0x41962880u, 0x444dcab7u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4281ab4cu, 0x411c1512u, 0x41e58500u, 0x444e1a7du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428269c4u, 0x411cd486u, 0x421a7100u, 0x444e6a43u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4282f89eu, 0x411d641du, 0x42421f40u, 0x444eba08u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42838778u, 0x411df3b4u, 0x4269cd80u, 0x444f09ceu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428445f0u, 0x411eb328u, 0x4288be00u, 0x444f5994u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4284d4cau, 0x411f42bfu, 0x429c9520u, 0x444fa959u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428563a4u, 0x411fd256u, 0x42b06c40u, 0x444ff91fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4286221cu, 0x412091cau, 0x42c44380u, 0x445048e5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4286b0f6u, 0x41212161u, 0x42d81aa0u, 0x445098aau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42873fd0u, 0x4121b0f8u, 0x42ebf1c0u, 0x4450e870u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4287fe48u, 0x4122706cu, 0x42ffc900u, 0x44513836u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42888d22u, 0x41230003u, 0x4309d010u, 0x445187fbu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42891bfcu, 0x41238f9au, 0x4313bba0u, 0x4451d7c1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4289da74u, 0x41244f0eu, 0x431da740u, 0x44522787u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428a694eu, 0x4124dea5u, 0x432792d0u, 0x4452774cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428af828u, 0x41256e3cu, 0x43317e60u, 0x4452c712u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428bb6a0u, 0x41262db0u, 0x433b6a00u, 0x445316d8u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428c457au, 0x4126bd47u, 0x43455590u, 0x4453669du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428cd454u, 0x41274cdeu, 0x434f4120u, 0x4453b663u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428d92ccu, 0x41280c52u, 0x43592cc0u, 0x44540629u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428e21a6u, 0x41289be9u, 0x43631850u, 0x445455eeu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428eb080u, 0x41292b80u, 0x436d03e0u, 0x4454a5b4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428f6ef8u, 0x4129eaf4u, 0x4376ef80u, 0x4454f57au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x428ffdd2u, 0x412a7a8bu, 0x43806d88u, 0x4455453fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42908cacu, 0x412b0a22u, 0x43856350u, 0x44559505u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42914b24u, 0x412bc996u, 0x438a5920u, 0x4455e4cbu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4291d9feu, 0x412c592du, 0x438f4ee8u, 0x44563490u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429268d8u, 0x412ce8c4u, 0x439444b0u, 0x44568456u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42932750u, 0x412da838u, 0x43993a80u, 0x4456d41cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4293b62au, 0x412e37cfu, 0x439e3048u, 0x445723e1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42944504u, 0x412ec766u, 0x43a32610u, 0x445773a7u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4295037cu, 0x412f86dau, 0x43a81be0u, 0x4457c36du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42959256u, 0x41301671u, 0x43ad11a8u, 0x44581332u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42962130u, 0x4130a608u, 0x43b20770u, 0x445862f8u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4296dfa8u, 0x4131657cu, 0x43b6fd40u, 0x4458b2beu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42976e82u, 0x4131f513u, 0x43bbf308u, 0x44590283u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4297fd5cu, 0x413284aau, 0x43c0e8d0u, 0x44595249u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4298bbd4u, 0x4133441eu, 0x43c5dea0u, 0x4459a20fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42994aaeu, 0x4133d3b5u, 0x43cad468u, 0x4459f1d4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x4299d988u, 0x4134634cu, 0x43cfca30u, 0x445a419au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429a9800u, 0x413522c0u, 0x43d4c000u, 0x445a9160u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429b26dau, 0x4135b257u, 0x43d9b5c8u, 0x445ae125u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429bb5b4u, 0x413641eeu, 0x43deab90u, 0x445b30ebu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429c742cu, 0x41370162u, 0x43e3a160u, 0x445b80b1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429d0306u, 0x413790f9u, 0x43e89728u, 0x445bd076u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429d91e0u, 0x41382090u, 0x43ed8cf0u, 0x445c203cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429e5058u, 0x4138e004u, 0x43f282c0u, 0x445c7002u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429edf32u, 0x41396f9bu, 0x43f77888u, 0x445cbfc7u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x429f6e0cu, 0x4139ff32u, 0x43fc6e50u, 0x445d0f8du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a02c84u, 0x413abea6u, 0x4400b210u, 0x445d5f53u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a0bb5eu, 0x413b4e3du, 0x44032cf4u, 0x445daf18u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a14a38u, 0x413bddd4u, 0x4405a7d8u, 0x445dfedeu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a208b0u, 0x413c9d48u, 0x440822c0u, 0x445e4ea4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a2978au, 0x413d2cdfu, 0x440a9da4u, 0x445e9e69u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a32664u, 0x413dbc76u, 0x440d1888u, 0x445eee2fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a3e4dcu, 0x413e7beau, 0x440f9370u, 0x445f3df5u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a473b6u, 0x413f0b81u, 0x44120e54u, 0x445f8dbau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a50290u, 0x413f9b18u, 0x44148938u, 0x445fdd80u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a5c108u, 0x41405a8cu, 0x44170420u, 0x44602d46u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a64fe2u, 0x4140ea23u, 0x44197f04u, 0x44607d0bu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a6debcu, 0x414179bau, 0x441bf9e8u, 0x4460ccd1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a79d34u, 0x4142392eu, 0x441e74d0u, 0x44611c97u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a82c0eu, 0x4142c8c5u, 0x4420efb4u, 0x44616c5cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a8bae8u, 0x4143585cu, 0x44236a98u, 0x4461bc22u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42a97960u, 0x414417d0u, 0x4425e580u, 0x44620be8u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42aa083au, 0x4144a767u, 0x44286064u, 0x44625badu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42aa9714u, 0x414536feu, 0x442adb48u, 0x4462ab73u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42ab558cu, 0x4145f672u, 0x442d5630u, 0x4462fb39u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42abe466u, 0x41468609u, 0x442fd114u, 0x44634afeu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42ac7340u, 0x414715a0u, 0x44324bf8u, 0x44639ac4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42ad31b8u, 0x4147d514u, 0x4434c6e0u, 0x4463ea8au, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42adc092u, 0x414864abu, 0x443741c4u, 0x44643a4fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42ae4f6cu, 0x4148f442u, 0x4439bca8u, 0x44648a15u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42af0de4u, 0x4149b3b6u, 0x443c3790u, 0x4464d9dbu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42af9cbeu, 0x414a434du, 0x443eb274u, 0x446529a0u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b02b98u, 0x414ad2e4u, 0x44412d58u, 0x44657966u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b0ea10u, 0x414b9258u, 0x4443a840u, 0x4465c92cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b178eau, 0x414c21efu, 0x44462324u, 0x446618f1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b207c4u, 0x414cb186u, 0x44489e08u, 0x446668b7u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b2c63cu, 0x414d70fau, 0x444b18f0u, 0x4466b87du, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b35516u, 0x414e0091u, 0x444d93d4u, 0x44670842u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b3e3f0u, 0x414e9028u, 0x44500eb8u, 0x44675808u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b4a268u, 0x414f4f9cu, 0x445289a0u, 0x4467a7ceu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b53142u, 0x414fdf33u, 0x44550484u, 0x4467f793u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b5c01cu, 0x41506ecau, 0x44577f68u, 0x44684759u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b67e94u, 0x41512e3eu, 0x4459fa50u, 0x4468971fu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b70d6eu, 0x4151bdd5u, 0x445c7534u, 0x4468e6e4u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b79c48u, 0x41524d6cu, 0x445ef018u, 0x446936aau, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b85ac0u, 0x41530ce0u, 0x44616b00u, 0x44698670u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b8e99au, 0x41539c77u, 0x4463e5e4u, 0x4469d635u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42b97874u, 0x41542c0eu, 0x446660c8u, 0x446a25fbu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42ba36ecu, 0x4154eb82u, 0x4468dbb0u, 0x446a75c1u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42bac5c6u, 0x41557b19u, 0x446b5694u, 0x446ac586u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42bb54a0u, 0x41560ab0u, 0x446dd178u, 0x446b154cu, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
        {0x42bc1318u, 0x4156ca24u, 0x44704c60u, 0x446b6512u, 0x42c67084u, 0x41477a18u, 0x3dfffadcu},
    };
    edict_t *unit = make_moving_unit(-2012, 568);
    uint8_t cells[128 * 128] = {0};
    box2_t bounds = {{-2048, 512}, {2048, 4608}};
    CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(128, 128, cells);
    unit->unitinfo.MoveSpeed = 100; unit->movement.flow_direct = true;
    unit->think = monster_think;
    unit->currentmove = &fixed_oblique_clock_walk;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    FOR_LOOP(i, 300) {
        level.time += FRAMETIME;
        globals.RunFrame();
        T_EQ(wc3_float_bits(unit->movement.fine_pose.x), expected[i][0]);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.y), expected[i][1]);
        T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][2]);
        T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][3]);
        T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][4]);
        T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][5]);
        T_EQ(wc3_float_bits(unit->s.angle), expected[i][6]);
    }
    reset_entities(); setup_test_world();
}

/* Exercise the production point-order owner and serializer at all three snapshot phases. */
TEST(wc3_movement, primary_clock_public_move_save_pause_and_stop) {
    cstring_t file = "/tmp/openwarcraft3-primary-clock-move.bin";
    FOR_LOOP(phase, 3) {
        reset_entities(); setup_test_world();
        level.waypoints = (typeof(level.waypoints)){0};
        /* These phases create independent VMs: retire their native registries
         * before run_test_jass closes the previous VM and its trigger code. */
        G_ClearRegionRegistry();
        memset(level.triggers, 0, sizeof(level.triggers)); level.num_triggers = 0;
        memset(&level.events, 0, sizeof(level.events));
        T_ASSERT(run_test_jass(
            "globals\nunit mover\nboolean entered = false\nendglobals\n"
            "function onEnter takes nothing returns nothing\nset entered = true\nendfunction\n"
            "function verifyEnter takes nothing returns nothing\n"
            "call BJassAssert(entered, \"scheduled Move missed region entry\")\nendfunction\n"
            "function main takes nothing returns nothing\n"
            "local trigger t = CreateTrigger()\nlocal region r = CreateRegion()\n"
            "call RegionAddRect(r, Rect(32, -100, 2000, 500))\n"
            "call TriggerRegisterEnterRegion(t, r, null)\ncall TriggerAddAction(t, function onEnter)\n"
            "set mover = CreateUnit(Player(0), 'hpea', 0, 0, 0)\nendfunction\n"
            "function freeze takes nothing returns nothing\ncall PauseUnit(mover, true)\nendfunction\n"
            "function resume takes nothing returns nothing\ncall PauseUnit(mover, false)\nendfunction\n"));
        edict_t *unit = NULL;
        FOR_LOOP(i, globals.num_edicts)
            if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) unit = g_edicts + i;
        T_NOT_NULL(unit); if (!unit) continue;
        unit->collision = 0; unit->stand = unit_stand; unit->think = monster_think;
        unit->svflags |= SVF_MONSTER; unit->movetype = MOVETYPE_STEP;
        unit->health.value = unit->health.max_value = 250;
        unit_stand(unit); S_SetUnitMoveSpeed(unit, 173);
        T_ASSERT(unit_issueorder(unit, "move", &(vec2_t){1800, 256}));
        level.started = level.scriptsConfigured = level.scriptsStarted = true;
        FOR_LOOP(i, phase + 1) { level.time += FRAMETIME; globals.RunFrame(); }
        T_ASSERT(unit->movement.clock_valid);
        T_ASSERT(unit->movement.velocity.x > 0);
        wc3Clock_t before_cap = unit->movement.pose_clock;
        vec2_t before_fine = unit->movement.fine_pose;
        S_SetUnitMoveSpeed(unit, 200);
        T_EQ(wc3_float_bits(unit->movement.pose_clock.time), wc3_float_bits(before_cap.time));
        T_EQ(wc3_float_bits(unit->movement.fine_pose.x), wc3_float_bits(before_fine.x));
        T_EQ(wc3_float_bits(unit->movement.fine_pose.y), wc3_float_bits(before_fine.y));
        T_ASSERT(WriteGame(file));
        uint32_t expected[6][10];
        FOR_LOOP(i, 6) {
            level.time += FRAMETIME; globals.RunFrame();
            expected[i][0] = wc3_float_bits(unit->movement.fine_pose.x);
            expected[i][1] = wc3_float_bits(unit->movement.fine_pose.y);
            expected[i][2] = wc3_float_bits(unit->s.origin2.x);
            expected[i][3] = wc3_float_bits(unit->s.origin2.y);
            expected[i][4] = wc3_float_bits(unit->movement.velocity.x);
            expected[i][5] = wc3_float_bits(unit->movement.velocity.y);
            expected[i][6] = wc3_float_bits(unit->s.angle);
            expected[i][7] = wc3_float_bits(level.pathing_clock.time);
            expected[i][8] = level.pathing_phase;
            expected[i][9] = wc3_float_bits(unit->movement.pose_clock.time);
        }
        jass_callbyname(level.vm, "verifyEnter", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_ASSERT(ReadGame(file));
        FOR_LOOP(i, 6) {
            level.time += FRAMETIME; globals.RunFrame();
            T_EQ(wc3_float_bits(unit->movement.fine_pose.x), expected[i][0]);
            T_EQ(wc3_float_bits(unit->movement.fine_pose.y), expected[i][1]);
            T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][2]);
            T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][3]);
            T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][4]);
            T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][5]);
            T_EQ(wc3_float_bits(unit->s.angle), expected[i][6]);
            T_EQ(wc3_float_bits(level.pathing_clock.time), expected[i][7]);
            T_EQ(level.pathing_phase, expected[i][8]);
            T_EQ(wc3_float_bits(unit->movement.pose_clock.time), expected[i][9]);
        }
        jass_callbyname(level.vm, "freeze", false);
        vec2_t frozen = unit->s.origin2;
        FOR_LOOP(i, 4) { level.time += FRAMETIME; globals.RunFrame(); }
        T_EQ(wc3_float_bits(unit->s.origin2.x), wc3_float_bits(frozen.x));
        T_EQ(wc3_float_bits(unit->s.origin2.y), wc3_float_bits(frozen.y));
        jass_callbyname(level.vm, "resume", false);
        level.time += FRAMETIME; globals.RunFrame();
        T_ASSERT(Vector2_distance(&frozen, &unit->s.origin2) < 25);
        T_ASSERT(unit->s.origin2.x > frozen.x);
        frozen = unit->s.origin2; unit->stunned = true;
        FOR_LOOP(i, 4) { level.time += FRAMETIME; globals.RunFrame(); }
        T_EQ(wc3_float_bits(unit->s.origin2.x), wc3_float_bits(frozen.x));
        T_EQ(wc3_float_bits(unit->s.origin2.y), wc3_float_bits(frozen.y));
        box2_t bounds = CM_GetWorldBounds();
        T_EQ(wc3_float_bits(wc3_world_coordinate(unit->movement.fine_pose.x, bounds.min.x, 32)), wc3_float_bits(frozen.x));
        T_EQ(wc3_float_bits(wc3_world_coordinate(unit->movement.fine_pose.y, bounds.min.y, 32)), wc3_float_bits(frozen.y));
        unit->stunned = false;
        level.time += FRAMETIME; globals.RunFrame();
        T_ASSERT(Vector2_distance(&frozen, &unit->s.origin2) < 25);
        T_ASSERT(unit_issueimmediateorder(unit, "stop"));
        frozen = unit->s.origin2;
        T_EQ(unit->movement.velocity.x, 0); T_EQ(unit->movement.velocity.y, 0);
        FOR_LOOP(i, 3) { level.time += FRAMETIME; globals.RunFrame(); }
        T_EQ(wc3_float_bits(unit->s.origin2.x), wc3_float_bits(frozen.x));
        T_EQ(wc3_float_bits(unit->s.origin2.y), wc3_float_bits(frozen.y));
        remove(file); level.started = false;
    }
}

/* Retail SetUnitPosition replaces the active order before placement; even a
 * same-position call cancels Move. Exercise the public native, real frame
 * scheduler, queued replacement and a save made after the placement. */
TEST(wc3_movement, forced_position_retires_move_and_queued_orders) {
    uint8_t cells[64 * 64] = {0};
    box2_t bounds = {{0,0},{2048,2048}};
    cstring_t file = "/tmp/openwarcraft3-forced-position-save.bin";
    reset_entities(); setup_test_world();
    memset(level.regions, 0, sizeof(level.regions)); level.num_regions = 0;
    memset(level.triggers, 0, sizeof(level.triggers)); level.num_triggers = 0;
    memset(&level.events, 0, sizeof(level.events));
    CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(64,64,cells);
    T_ASSERT(run_test_jass(
        "globals\nunit mover\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set mover = CreateUnit(Player(0), 'hpea', 128, 128, 0)\nendfunction\n"
        "function same takes nothing returns nothing\n"
        "call SetUnitPosition(mover, GetUnitX(mover), GetUnitY(mover))\nendfunction\n"
        "function shift takes nothing returns nothing\n"
        "call SetUnitPosition(mover, 640.125, 704.375)\nendfunction\n"
        "function shiftLoc takes nothing returns nothing\n"
        "local location p = Location(640.125, 704.375)\n"
        "call SetUnitPositionLoc(mover, p)\ncall RemoveLocation(p)\nendfunction\n"
        "function verifyStopped takes nothing returns nothing\n"
        "call BJassAssert(GetUnitCurrentOrder(mover) == 0, \"forced placement retained order\")\nendfunction\n"));
    edict_t *unit = NULL;
    FOR_LOOP(i, globals.num_edicts) if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) unit = g_edicts + i;
    T_NOT_NULL(unit); if (!unit) return;
    unit->collision = 0; unit->stand = unit_stand; unit->think = monster_think;
    unit->svflags |= SVF_MONSTER; unit->movetype = MOVETYPE_STEP;
    unit->health.value = unit->health.max_value = 250; unit_stand(unit);
    S_SetUnitMoveSpeed(unit,173);
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    for (unsigned mode = 0; mode < 5; mode++) {
        vec2_t goal = {1800,1536}, queued = {1600,128};
        T_ASSERT(unit_issueorder(unit, mode == 2 ? "patrol" : "move", &goal));
        T_ASSERT(G_IssueUnitPointOrder(unit,"move",&queued,true,0,0));
        T_EQ(G_UnitQueuedOrderCount(unit),1);
        FOR_LOOP(i,4) { level.time += FRAMETIME; globals.RunFrame(); }
        if (mode != 2) T_ASSERT(unit->movement.clock_valid);
        if (mode == 3) S_SetUnitPaused(unit,true);
        unit->movement.group_id = 123; unit->movement.group_speed = 139;
        vec2_t before = unit->s.origin2;
        jass_callbyname(level.vm, mode == 4 ? "shiftLoc" : mode ? "shift" : "same", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(unit->current_order_id,0); T_EQ(G_UnitQueuedOrderCount(unit),0);
        T_EQ(unit->movement.group_id,0); T_EQ(unit->movement.group_speed,0);
        T_EQ(unit->movement.velocity.x,0); T_EQ(unit->movement.velocity.y,0);
        T_ASSERT(!unit->movement.clock_valid);
        T_ASSERT(!move_is_active_order_walk(unit));
        jass_callbyname(level.vm,"verifyStopped",false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        vec2_t placed = unit->s.origin2;
        if (mode) { T_FEQ(placed.x,640.125f,0.001f); T_FEQ(placed.y,704.375f,0.001f); }
        else { T_EQ(placed.x,before.x); T_EQ(placed.y,before.y); }
        T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file));
        if (mode == 3) S_SetUnitPaused(unit,false);
        FOR_LOOP(i,4) { level.time += FRAMETIME; globals.RunFrame(); }
        T_EQ(unit->s.origin2.x,placed.x); T_EQ(unit->s.origin2.y,placed.y);
        T_EQ(unit->current_order_id,0); T_EQ(G_UnitQueuedOrderCount(unit),0);
    }
    remove(file); level.started = false;
}

/* Captured before-Stop clocks and fine poses drive the actual public native.
 * Exact expected placement words come from original38, not the C helper. */
TEST(wc3_movement, forced_position_matches_original_native_pose_words) {
    static uint8_t cells[256 * 128];
    box2_t bounds = {{-7168,-3072},{1024,1024}};
    static uint32_t const cases[][19] = {
        {0x3ffd7094u,0x00000000u,0x43244b1cu,0x428bd688u,0x3f71e3f6u,0x4092eba4u,0x40960000u,0x3faf1514u,0x3ffffff0u,0x00000000u,0x43960000u,0xc4eec038u,0xc44fe9d4u,0x43244ff2u,0x428c058bu,0xc4eec038u,0xc44fe9d4u,0xc4eec038u,0xc44fe9d4u},
        {0x407f5b52u,0x00000000u,0x4325271cu,0x4294db4bu,0x3f78dd65u,0x4092bd05u,0x40960000u,0x3fae5285u,0x407fff28u,0x00000000u,0x43960000u,0xc4eb559cu,0xc42c3cecu,0x43252a99u,0x4294f0c5u,0xc4eb559cu,0xc42c3cecu,0xc4eb599cu,0xc42c34ecu},
        {0x407fff28u,0x00000000u,0x43252a99u,0x4294f0c5u,0x00000000u,0x00000000u,0x40960000u,0x3fae5285u,0x409ffefcu,0x00000000u,0x43960000u,0xc4eb559cu,0xc42c3cecu,0x43252a99u,0x4294f0c5u,0xc4eb559cu,0xc42c3cecu,0xc4eb559cu,0xc42c3cecu},
        {0x40dfabe2u,0x00000000u,0x43262491u,0x429db7a9u,0x3f7041c7u,0x4092f65cu,0x40960000u,0x3faf429du,0x40dffdccu,0x00000000u,0x43960000u,0xc4e76020u,0xc408cb50u,0x432627f8u,0x429dcd2cu,0xc4e76020u,0xc408cb50u,0xc4e76420u,0xc408c350u},
    };
    for (unsigned i = 0; i < 4; i++) {
        reset_entities(); setup_test_world();
        CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(256,128,cells);
        memset(level.regions,0,sizeof(level.regions)); level.num_regions = 0;
        memset(level.triggers,0,sizeof(level.triggers)); level.num_triggers = 0;
        memset(&level.events,0,sizeof(level.events));
        T_ASSERT(run_test_jass("globals\nunit mover\nendglobals\n"
            "function main takes nothing returns nothing\n"
            "set mover = CreateUnit(Player(0),'hfoo',0,0,0)\nendfunction\n"
            "function same takes nothing returns nothing\n"
            "call SetUnitPosition(mover,GetUnitX(mover),GetUnitY(mover))\nendfunction\n"
            "function shift takes nothing returns nothing\n"
            "call SetUnitPosition(mover,GetUnitX(mover)+0.125,GetUnitY(mover)-0.125)\nendfunction\n"));
        edict_t *unit = NULL;
        FOR_LOOP(n,globals.num_edicts) if (g_edicts[n].inuse && g_edicts[n].class_id == MAKEFOURCC('h','f','o','o')) unit = g_edicts + n;
        T_NOT_NULL(unit); if (!unit) continue;
        unit->collision = 0; unit->stand = unit_stand; unit_stand(unit);
        unit->s.origin2 = (vec2_t){wc3_float(cases[i][17]),wc3_float(cases[i][18])};
        level.pathing_clock = (wc3Clock_t){wc3_float(cases[i][8]),cases[i][9],wc3_float(cases[i][10])};
        vec2_t goal = {-1600,-144}; T_ASSERT(unit_issueorder(unit,"move",&goal));
        unit->movement.pose_clock = (wc3Clock_t){wc3_float(cases[i][0]),cases[i][1],300};
        unit->movement.fine_pose = (vec2_t){wc3_float(cases[i][2]),wc3_float(cases[i][3])};
        unit->movement.velocity = (vec2_t){wc3_float(cases[i][4])*32.f,wc3_float(cases[i][5])*32.f};
        unit->movement.clock_valid = unit->movement.pose_valid = true;
        unit->movement.pose_world = unit->s.origin2;
        jass_callbyname(level.vm,i == 1 || i == 3 ? "shift" : "same",false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(wc3_float_bits(unit->movement.fine_pose.x),cases[i][13]);
        T_EQ(wc3_float_bits(unit->movement.fine_pose.y),cases[i][14]);
        T_EQ(wc3_float_bits(unit->s.origin2.x),cases[i][15]);
        T_EQ(wc3_float_bits(unit->s.origin2.y),cases[i][16]);
        T_EQ(wc3_float_bits(unit->movement.pose_clock.time),cases[i][8]);
        T_EQ(unit->movement.pose_clock.epoch,cases[i][9]);
        T_EQ(unit->movement.velocity.x,0); T_EQ(unit->movement.velocity.y,0);
        T_EQ(unit->current_order_id,0); T_ASSERT(!unit->movement.clock_valid);
    }
    reset_entities(); setup_test_world();
}

/* Original public scene39 ring endpoints, with its terrain edits and31-unit
 * Footman footprint. Use the actual JASS native rather than the ring helper. */
TEST(wc3_movement, blocked_position_matches_original_ring_endpoints) {
    static uint8_t cells[256 * 128];
    box2_t bounds = {{-7168,-3072},{1024,1024}};
    static vec2_t const expected[] = {{-1968,-560},{-1936,-560},{-2000,-464},
                                    {-1936,-560},{-1872,-400},{-2000,-592}};
    static cstring_t const functions[] = {"centre","fractional","west","east","north","centre"};
    reset_entities(); setup_test_world(); memset(cells,0,sizeof(cells));
    CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(256,128,cells); G_BlightInit();
    memset(level.regions,0,sizeof(level.regions)); level.num_regions = 0;
    memset(level.triggers,0,sizeof(level.triggers)); level.num_triggers = 0;
    memset(&level.events,0,sizeof(level.events));
    T_ASSERT(run_test_jass("globals\nunit mover\nendglobals\n"
        "function patch takes integer radius returns nothing\n"
        "local integer y = 80-radius\nlocal integer x\n"
        "loop\nexitwhen y>80+radius\nset x=163-radius\n"
        "loop\nexitwhen x>163+radius\n"
        "call SetTerrainPathable(-7168+x*32+16,-3072+y*32+16,ConvertPathingType(1),false)\n"
        "set x=x+1\nendloop\nset y=y+1\nendloop\nendfunction\n"
        "function small takes nothing returns nothing\ncall patch(1)\nendfunction\n"
        "function large takes nothing returns nothing\ncall patch(2)\nendfunction\n"
        "function main takes nothing returns nothing\n"
        "set mover = CreateUnit(Player(0),'hfoo',-1936,-976,0)\nendfunction\n"
        "function centre takes nothing returns nothing\ncall SetUnitPosition(mover,-1936,-512)\nendfunction\n"
        "function fractional takes nothing returns nothing\ncall SetUnitPosition(mover,-1935.875,-512.125)\nendfunction\n"
        "function west takes nothing returns nothing\ncall SetUnitPosition(mover,-1968,-512)\nendfunction\n"
        "function east takes nothing returns nothing\ncall SetUnitPosition(mover,-1904,-512)\nendfunction\n"
        "function north takes nothing returns nothing\ncall SetUnitPosition(mover,-1936,-480)\nendfunction\n"));
    edict_t *unit = NULL;
    FOR_LOOP(i,globals.num_edicts) if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) unit = g_edicts+i;
    T_NOT_NULL(unit); if (!unit) return;
    unit->collision = 31; unit->stand = unit_stand; unit_stand(unit);
    for (unsigned i=0;i<6;i++) {
        jass_callbyname(level.vm,i==5 ? "large" : "small",false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        jass_callbyname(level.vm,functions[i],false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(wc3_float_bits(unit->s.origin2.x),wc3_float_bits(expected[i].x));
        T_EQ(wc3_float_bits(unit->s.origin2.y),wc3_float_bits(expected[i].y));
        T_EQ(unit->current_order_id,0);
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_movement, terrain_native_edits_redirect_active_move_and_invalidate_cached_route) {
    edict_t *unit = make_moving_unit(80,176);
    uint8_t cells[16*16] = {0};
    vec2_t target = {400,176};
    unit->svflags |= SVF_MONSTER; unit->unitinfo.MoveSpeed = 256;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{512,512}});
    CM_SetupTestPathmap(16,16,cells); G_BlightInit(); gi.LinkEntity(unit);
    T_ASSERT(unit_issueorder(unit,"move",&target));
    uint32_t generation = CM_BuildHeatmap(unit->goalentity);
    CM_ProcessPathJobs(4096);
    T_ASSERT(CM_ActivateCachedFlow(generation));
    unit->currentmove->think(unit);
    T_ASSERT(unit->movement.flow_direct);
    T_ASSERT(run_test_jass(
        "function wall takes boolean passable returns nothing\nlocal integer y=0\n"
        "loop\nexitwhen y>12\n"
        "call SetTerrainPathable(272,y*32+16,ConvertPathingType(1),passable)\n"
        "set y=y+1\nendloop\nendfunction\n"
        "function clear takes nothing returns nothing\ncall wall(true)\nendfunction\n"
        "function main takes nothing returns nothing\ncall wall(false)\nendfunction\n"));
    T_ASSERT(!CM_ActivateCachedFlow(generation));
    for (unsigned frame=0;frame<15;frame++) {
        level.time += FRAMETIME; unit->currentmove->think(unit); CM_ProcessPathJobs(4096);
        T_ASSERT(CM_PointIsPathableForRadius(&unit->s.origin2,0));
    }
    T_ASSERT(fabsf(unit->s.origin2.y-176)>2);
    T_ASSERT(!unit->movement.flow_direct);
    jass_callbyname(level.vm,"clear",false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    for (unsigned frame=0;frame<160 && unit->current_order_id;frame++) {
        level.time += FRAMETIME; unit->currentmove->think(unit); CM_ProcessPathJobs(4096);
    }
    T_EQ(unit->current_order_id,0);
    /* Ordinary zero-range Move finishes inside the recovered0.49 fine-cell gate. */
    T_ASSERT(Vector2_distance(&unit->s.origin2,&target)<=wc3_mul(wc3_float(0x3efae148),32));
    reset_entities(); setup_test_world();
}

/* Natural point completion must integrate the same retained fine pose before publishing zero velocity. */
TEST(wc3_movement, native_fine_pose_reaches_final_point_commit) {
    edict_t *unit = make_moving_unit(-2012, 568);
    uint8_t cells[16 * 16] = {0};
    box2_t bounds = {{-2048, 512}, {-1536, 1024}};
    vec2_t target = {-1800, 600};
    CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(16, 16, cells);
    unit->unitinfo.MoveSpeed = 100;
    T_ASSERT(unit_issueorder(unit, "move", &target));
    unit->movement.flow_direct = true;
    FOR_LOOP(i, 2) { unit->s.angle = .125f; unit_moveindirection(unit); }
    /* Third constant-heading commit in retail-native-pose-1.27.json, with a nearby accepted goal. */
    target = (vec2_t){wc3_float(0xc4f7c77e) + 5, wc3_float(0x440eef5f)};
    unit->goalentity->s.origin2 = target;
    unit->currentmove->think(unit);
    T_EQ(wc3_float_bits(unit->s.origin2.x), 0xc4f7c77eu);
    T_EQ(wc3_float_bits(unit->s.origin2.y), 0x440eef5fu);
    T_EQ(wc3_float_bits(unit->movement.fine_pose.x), 0x40038827u);
    T_EQ(wc3_float_bits(unit->movement.fine_pose.y), 0x3feef5f5u);
    T_ASSERT(unit->movement.pose_valid);
    T_EQ(unit->movement.velocity.x, 0); T_EQ(unit->movement.velocity.y, 0);
    T_EQ(unit->current_order_id, 0); T_STREQ(unit->currentmove->animation, "stand");
    reset_entities(); setup_test_world();
}

/* Original160060 measures squared velocity in fine-grid units, with32 world units per cell. */
TEST(wc3_movement, retail_committed_facing_guard_uses_grid_velocity) {
    edict_t *unit = make_moving_unit(320, 320);
    /* An authored Misc minimum override exposes these sub-bound guards. */
    float minimum = game.constants.minUnitSpeed; game.constants.minUnitSpeed = 0.01f;
    unit->unitinfo.MoveSpeed = 0.016f;
    unit->s.angle = 0.125f;
    unit->movement.flow_direct = true;
    unit_moveindirection(unit);
    T_ASSERT(unit->movement.velocity.x > 0);
    T_EQ(wc3_float_bits(unit->s.angle), 0x3e000000u);
    game.constants.minUnitSpeed = minimum;
}

/* Original16fe20 clears squared fine velocity below2e-7, before the distinct facing guard. */
TEST(wc3_movement, retail_velocity_guard_uses_fine_grid_scale) {
    edict_t *unit = make_moving_unit(320, 320);
    /* An authored Misc minimum override exposes these sub-bound guards. */
    float minimum = game.constants.minUnitSpeed; game.constants.minUnitSpeed = 0.01f;
    unit->unitinfo.MoveSpeed = 0.01f;
    unit->s.angle = 0.125f;
    unit->movement.flow_direct = true;
    unit_moveindirection(unit);
    T_EQ(wc3_float_bits(unit->movement.velocity.x), 0u);
    T_EQ(wc3_float_bits(unit->movement.velocity.y), 0u);
    T_EQ(wc3_float_bits(unit->s.origin2.x), 0x43a00000u);
    T_EQ(wc3_float_bits(unit->s.origin2.y), 0x43a00000u);
    T_EQ(wc3_float_bits(unit->s.angle), 0x3e000000u);
    game.constants.minUnitSpeed = minimum;
}

/* Saved velocity must resume with the same cancellation words, not a fresh zero-velocity approximation. */
TEST(wc3_movement, retail_velocity_resume_is_deterministic) {
    edict_t *unit = make_moving_unit(320, 320);
    cstring_t file = "/tmp/openwarcraft3-retail-velocity-save.bin";
    uint32_t expected[12][4];
    unit->unitinfo.MoveSpeed = 100; unit->s.angle = 0.125f; unit->movement.flow_direct = true;
    unit_moveindirection(unit);
    T_ASSERT(WriteGame(file));
    FOR_LOOP(i, 12) {
        unit->s.angle = (i & 1) ? 0.6f : 0.125f;
        unit_moveindirection(unit);
        expected[i][0] = wc3_float_bits(unit->s.origin2.x); expected[i][1] = wc3_float_bits(unit->s.origin2.y);
        expected[i][2] = wc3_float_bits(unit->movement.velocity.x);
        expected[i][3] = wc3_float_bits(unit->movement.velocity.y);
    }
    T_ASSERT(ReadGame(file));
    FOR_LOOP(i, 12) {
        unit->s.angle = (i & 1) ? 0.6f : 0.125f;
        unit_moveindirection(unit);
        T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][0]); T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][1]);
        T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][2]);
        T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][3]);
    }
    remove(file);
}

/* Retail 170880 tests the heading error BEFORE turning; equality stops travel. */
TEST(wc3_movement, stock_window_stops_translation_while_turning) {
    edict_t *unit = make_moving_unit(0, 0);
    UnitData_t data = *unit->data.UnitData;
    data.turnRate = 0.6f; data.propWin = 60;
    unit->data.UnitData = &data;
    T_EQ(wc3_float_bits(unit_propwindow(unit)), 0x3f860a91u);
    vec2_t const north = {0, 512};
    unit_changeangle_towards_point(unit, &north);
    unit_moveindirection(unit);
    T_ASSERT(unit->movement.turn_blocked);
    T_EQ(unit->s.origin2.x, 0); T_EQ(unit->s.origin2.y, 0);
    T_EQ(unit->s.angle, 0.6f);
    unit_changeangle_towards_point(unit, &north);
    unit_moveindirection(unit);
    T_ASSERT(!unit->movement.turn_blocked);
    T_ASSERT(unit->s.origin2.y > 0);
}

TEST(wc3_movement, stock_window_turns_before_close_goal_arrival) {
    edict_t *unit = make_moving_unit(0, 0);
    UnitData_t data = *unit->data.UnitData;
    data.turnRate = 0.6f; data.propWin = 60;
    unit->data.UnitData = &data;
    unit->s.angle = 2;
    vec2_t const east = {10, 0};
    T_ASSERT(unit_issueorder(unit, "move", &east));
    unit->currentmove->think(unit);
    T_EQ(unit->s.origin2.x, 0); T_EQ(unit->s.origin2.y, 0);
    T_EQ(unit->s.angle, wc3_sub(2, 0.6f));
    T_EQ(unit->currentmove->proc, CAbilityMove);
    FOR_LOOP(i, 4) unit->currentmove->think(unit);
    T_EQ(unit->s.origin2.x, 0);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, stock_getters_keep_authored_defaults_after_setters) {
    reset_entities(); setup_test_world();
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        " local unit u = CreateUnit(Player(0), 'hfoo', 0.0, 0.0, 0.0)\n"
        " call BJassAssert(GetUnitTurnSpeed(u) == 0.6, \"stock turn speed\")\n"
        " call BJassAssert(GetUnitPropWindow(u) > 1.047 and GetUnitPropWindow(u) < 1.048, \"stock radians\")\n"
        " call BJassAssert(GetUnitDefaultTurnSpeed(u) == 0.6, \"authored turn speed\")\n"
        " call BJassAssert(GetUnitDefaultPropWindow(u) == 60.0, \"authored degrees\")\n"
        " call SetUnitTurnSpeed(u, 0.125)\n"
        " call SetUnitPropWindow(u, 0.5)\n"
        " call BJassAssert(GetUnitTurnSpeed(u) == 0.125 and GetUnitPropWindow(u) == 0.5, \"current overrides\")\n"
        " call BJassAssert(GetUnitDefaultTurnSpeed(u) == 0.6 and GetUnitDefaultPropWindow(u) == 60.0, \"defaults immutable\")\n"
        "endfunction\n"));
}

/* Same authored clone as the retail speed-input witness: integer w3u fields,
 * immutable237 default and173..389 limits. Exercise real natives and Move. */
TEST(wc3_movement, public_speed_setter_uses_authored_limits_and_keeps_default) {
    int32_t speed = 237, minimum = 173, maximum = 389;
    unitModification_t mods[] = {
        { .modID = MAKEFOURCC('u','m','v','s'), .type = mod_int, .data = &speed },
        { .modID = MAKEFOURCC('u','m','i','s'), .type = mod_int, .data = &minimum },
        { .modID = MAKEFOURCC('u','m','a','s'), .type = mod_int, .data = &maximum }
    };
    unitData_t custom = { .originalUnitID = MAKEFOURCC('h','f','o','o'),
        .newUnitID = MAKEFOURCC('h','0','0','1'), .numbeOfModifications = 3, .modifications = mods };
    reset_entities();
    setup_test_world();
    mapInfo_t const *saved_mapinfo = level.mapinfo;
    mapInfo_t info = *saved_mapinfo;
    info.num_userCreatedUnits = 1;
    info.userCreatedUnits = &custom;
    level.mapinfo = &info;
    G_SetMapUnitOverrides(&info);
    T_ASSERT(run_test_jass(
        "globals\n unit speedUnit\nendglobals\n"
        "function main takes nothing returns nothing\n"
        " set speedUnit = CreateUnit(Player(0), 'h001', 320.0, 320.0, 0.0)\n"
        " call SetUnitMoveSpeed(speedUnit, 100.0)\n"
        " call IssuePointOrder(speedUnit, \"move\", 600.0, 320.0)\n"
        " call BJassAssert(GetUnitMoveSpeed(speedUnit) == 173.0, \"authored speed minimum\")\n"
        " call BJassAssert(GetUnitDefaultMoveSpeed(speedUnit) == 237.0, \"immutable authored speed\")\n"
        "endfunction\n"
        "function SetZeroSpeed takes nothing returns nothing\n"
        " call SetUnitMoveSpeed(speedUnit, 0.0)\n"
        " call BJassAssert(GetUnitMoveSpeed(speedUnit) == 173.0, \"explicit zero clamps to minimum\")\n"
        " call BJassAssert(GetUnitDefaultMoveSpeed(speedUnit) == 237.0, \"zero keeps default\")\n"
        "endfunction\n"
        "function SetHighSpeed takes nothing returns nothing\n"
        " call SetUnitMoveSpeed(speedUnit, 1000.0)\n"
        " call BJassAssert(GetUnitMoveSpeed(speedUnit) == 389.0, \"authored speed maximum\")\n"
        " call BJassAssert(GetUnitDefaultMoveSpeed(speedUnit) == 237.0, \"high keeps default\")\n"
        "endfunction\n"));
    edict_t *unit = NULL;
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == custom.newUnitID) unit = g_edicts + i;
    }
    T_NOT_NULL(unit);
    if (unit) {
        T_FEQ(unit->data.UnitBalance->speed, 237.f, .00001f);
        T_FEQ(unit->data.UnitBalance->minSpeed, 173.f, .00001f);
        T_FEQ(unit->data.UnitBalance->maxSpeed, 389.f, .00001f);
        T_FEQ(unit_movedistance(unit), 17.3f, .001f);
        unit->currentmove->think(unit);
        T_FEQ(unit->s.origin2.x, 337.3f, .001f);
        jass_callbyname(level.vm, "SetZeroSpeed", true); jass_runevents(level.vm);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_FEQ(unit_movedistance(unit), 17.3f, .001f);
        jass_callbyname(level.vm, "SetHighSpeed", true); jass_runevents(level.vm);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_FEQ(unit_movedistance(unit), 38.9f, .001f);
    }
    reset_entities();
    G_SetMapUnitOverrides(NULL);
    level.mapinfo = saved_mapinfo;
    setup_test_world();
}

TEST(wc3_movement, public_speed_disabled_owner_ignores_setter) {
    int32_t speed = 0;
    char move_type[] = "_";
    unitModification_t mods[] = {
        { .modID = MAKEFOURCC('u','m','v','s'), .type = mod_int, .data = &speed },
        { .modID = MAKEFOURCC('u','m','v','t'), .type = mod_string, .data = move_type }
    };
    unitData_t custom = { .originalUnitID = MAKEFOURCC('h','f','o','o'),
        .newUnitID = MAKEFOURCC('h','0','0','2'), .numbeOfModifications = 2, .modifications = mods };
    reset_entities(); setup_test_world();
    mapInfo_t const *saved = level.mapinfo;
    mapInfo_t info = *saved;
    info.num_userCreatedUnits = 1; info.userCreatedUnits = &custom;
    level.mapinfo = &info; G_SetMapUnitOverrides(&info);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        " local unit u = CreateUnit(Player(0), 'h002', 320.0, 320.0, 0.0)\n"
        " call SetUnitMoveSpeed(u, 1000.0)\n"
        " call BJassAssert(GetUnitMoveSpeed(u) == 0.0, \"disabled owner speed remains zero\")\n"
        " call BJassAssert(GetUnitDefaultMoveSpeed(u) == 0.0, \"disabled authored default\")\n"
        "endfunction\n"));
    edict_t *unit = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == custom.newUnitID) unit = g_edicts + i;
    T_NOT_NULL(unit);
    if (unit) {
        T_ASSERT(M_UnitMoveDisabled(unit));
        T_EQ(unit->unitinfo.MoveSpeed, 0); T_EQ(unit->unitinfo.move_flags & BZ_UNIT_SPEED_SET, 0);
        T_EQ(unit_movedistance(unit), 0);
    }
    reset_entities(); G_SetMapUnitOverrides(NULL); level.mapinfo = saved; setup_test_world();
}

TEST(wc3_movement, public_speed_drop_clamps_existing_velocity_before_next_think) {
    reset_entities(); setup_test_world();
    cstring_t file = "/tmp/openwarcraft3-speed-drop-save.bin";
    float old_minimum = game.constants.minUnitSpeed, old_maximum = game.constants.maxUnitSpeed;
    game.constants.minUnitSpeed = 150; game.constants.maxUnitSpeed = 400;
    T_ASSERT(run_test_jass(
        "globals\n unit speedUnit\nendglobals\n"
        "function main takes nothing returns nothing\n"
        " set speedUnit = CreateUnit(Player(0), 'hpea', 320.0, 320.0, 90.0)\n"
        " call SetUnitMoveSpeed(speedUnit, 401.0)\n"
        "endfunction\n"
        "function BeginSpeedMove takes nothing returns nothing\n"
        " call BJassAssert(IssuePointOrder(speedUnit, \"move\", 320.0, 1500.0), \"moving speed drop admitted\")\n"
        "endfunction\n"
        "function DropSpeed takes nothing returns nothing\n"
        " call SetUnitMoveSpeed(speedUnit, 100.0)\n"
        " call BJassAssert(GetUnitMoveSpeed(speedUnit) == 150.0, \"lower cap effective\")\n"
        "endfunction\n"
        "function RaiseSpeed takes nothing returns nothing\n"
        " call SetUnitMoveSpeed(speedUnit, 401.0)\n"
        " call BJassAssert(GetUnitMoveSpeed(speedUnit) == 400.0, \"raised cap effective\")\n"
        "endfunction\n"));
    edict_t *unit = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) unit = g_edicts + i;
    T_NOT_NULL(unit);
    if (unit) {
        unit->health.value = unit->health.max_value = 250; unit->stand = unit_stand; unit_stand(unit);
        jass_callbyname(level.vm, "BeginSpeedMove", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_NOT_NULL(unit->currentmove->think);
        if (!unit->currentmove->think) goto done;
        unit->currentmove->think(unit);
        T_ASSERT(unit->movement.velocity.y > 300);
        vec2_t position = unit->s.origin2;
        uint32_t facing = wc3_float_bits(unit->s.angle);
        jass_callbyname(level.vm, "DropSpeed", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_FEQ(Vector2_len(&unit->movement.velocity), 150, .01f);
        T_EQ(unit->s.origin2.x, position.x); T_EQ(unit->s.origin2.y, position.y);
        T_EQ(wc3_float_bits(unit->s.angle), facing);
        T_ASSERT(WriteGame(file));
        uint32_t expected[8][4];
        FOR_LOOP(i, 8) {
            unit->currentmove->think(unit);
            expected[i][0] = wc3_float_bits(unit->s.origin2.x); expected[i][1] = wc3_float_bits(unit->s.origin2.y);
            expected[i][2] = wc3_float_bits(unit->movement.velocity.x); expected[i][3] = wc3_float_bits(unit->movement.velocity.y);
        }
        T_ASSERT(ReadGame(file));
        T_FEQ(Vector2_len(&unit->movement.velocity), 150, .01f);
        FOR_LOOP(i, 8) {
            unit->currentmove->think(unit);
            T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][0]); T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][1]);
            T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][2]); T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][3]);
        }
        vec2_t velocity = unit->movement.velocity;
        position = unit->s.origin2;
        jass_callbyname(level.vm, "RaiseSpeed", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(wc3_float_bits(unit->movement.velocity.x), wc3_float_bits(velocity.x));
        T_EQ(wc3_float_bits(unit->movement.velocity.y), wc3_float_bits(velocity.y));
        T_EQ(unit->s.origin2.x, position.x); T_EQ(unit->s.origin2.y, position.y);
    }
done:
    remove(file);
    game.constants.minUnitSpeed = old_minimum; game.constants.maxUnitSpeed = old_maximum;
}

/* Original AIms virtual184 and live TFT Boots on a RoC-format map supply60; list aggregation
 * keeps the largest nonnegative flat bonus before multiplication and clamps. */
TEST(wc3_movement, public_boots_pickup_and_removal_reach_current_speed_and_steps) {
    reset_entities(); setup_test_world();
    const char ability_slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataC1\"\nC;Y1;X5;K\"DataD1\"\nC;Y1;X6;K\"DataE1\"\n"
        "C;Y2;X1;K\"AIms\"\nC;Y2;X2;K\"AIms\"\nC;Y2;X3;K\"60\"\n"
        "C;Y3;X1;K\"AInv\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X3;K\"6\"\n"
        "C;Y3;X4;K\"1\"\nC;Y3;X5;K\"1\"\nC;Y3;X6;K\"1\"\nE\n";
    const char item_slk[] =
        "ID;PWXL;N;EBB;Y2;X4\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\nC;Y1;X4;K\"file\"\n"
        "C;Y2;X1;K\"bspd\"\nC;Y2;X2;K\"AIms\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"Objects\\\\InventoryItems\\\\TreasureChest\\\\treasurechest.mdl\"\nE\n";
    slkTestData_t *abilities = parse_slk_string(ability_slk), *old_abilities = G_SetSLKRows("AbilityData", abilities);
    slkTestData_t *items = parse_slk_string(item_slk), *old_items = G_SetSLKRows("ItemData", items);
    T_ASSERT(run_test_jass(
        "globals\n unit bootsUnit\n item bootsOne\n item bootsTwo\n real bootsDefault\nendglobals\n"
        "function main takes nothing returns nothing\n"
        " set bootsUnit = CreateUnit(Player(0), 'Hpal', 320.0, 320.0, 90.0)\n"
        " call UnitAddAbility(bootsUnit, 'AInv')\n call SetUnitMoveSpeed(bootsUnit, 270.0)\n"
        " set bootsDefault = GetUnitDefaultMoveSpeed(bootsUnit)\n"
        " set bootsOne = CreateItem('bspd', 256.0, 320.0)\n set bootsTwo = CreateItem('bspd', 192.0, 320.0)\nendfunction\n"
        "function BeginBootsMove takes nothing returns nothing\n"
        " call BJassAssert(IssuePointOrder(bootsUnit, \"move\", 320.0, 1800.0), \"boots move admitted\")\nendfunction\n"
        "function PickupBootsOne takes nothing returns nothing\n"
        " call BJassAssert(UnitAddItem(bootsUnit, bootsOne), \"first boots admitted\")\n"
        " call BJassAssert(GetUnitMoveSpeed(bootsUnit) == 330.0, \"first flat bonus reaches public getter\")\nendfunction\n"
        "function PickupBootsTwo takes nothing returns nothing\n"
        " call BJassAssert(UnitAddItem(bootsUnit, bootsTwo), \"second boots admitted\")\n"
        " call BJassAssert(GetUnitMoveSpeed(bootsUnit) == 330.0, \"duplicate flat bonuses use maximum\")\nendfunction\n"
        "function RemoveBootsOne takes nothing returns nothing\n"
        " call UnitRemoveItem(bootsUnit, bootsOne)\n"
        " call BJassAssert(GetUnitMoveSpeed(bootsUnit) == 330.0, \"remaining boots retain bonus\")\nendfunction\n"
        "function PublishBootsSpeed takes nothing returns nothing\n"
        " call SetUnitMoveSpeed(bootsUnit, 270.0)\nendfunction\n"
        "function RemoveBootsTwo takes nothing returns nothing\n"
        " call UnitRemoveItem(bootsUnit, bootsTwo)\n"
        " call BJassAssert(GetUnitMoveSpeed(bootsUnit) == 270.0, \"last boots restore base\")\n"
        " call BJassAssert(GetUnitDefaultMoveSpeed(bootsUnit) == bootsDefault, \"item bonus keeps immutable default\")\nendfunction\n"));
    edict_t *unit = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('H','p','a','l')) unit = g_edicts + i;
    T_NOT_NULL(unit);
    if (unit) {
        unit->health.value = unit->health.max_value = 1000; unit->stand = unit_stand; unit_stand(unit);
        jass_callbyname(level.vm, "BeginBootsMove", false);
        char const *phases[] = {"PickupBootsOne", "PickupBootsTwo", "RemoveBootsOne", "RemoveBootsTwo"};
        FOR_LOOP(i, sizeof(phases) / sizeof(*phases)) {
            jass_callbyname(level.vm, phases[i], false);
            T_ASSERT(!jass_rterror_pending(level.vm));
            T_FEQ(S_UnitMoveSpeed(unit), i == 3 ? 270 : 330, .001f);
            if (i == 1) {
                jass_callbyname(level.vm, "PublishBootsSpeed", false);
                jass_callbyname(level.vm, "BeginBootsMove", false);
                T_ASSERT(!jass_rterror_pending(level.vm));
            }
            vec2_t before = unit->s.origin2;
            unit->currentmove->think(unit);
            T_FEQ(Vector2_distance(&unit->s.origin2, &before), i == 0 ? 27 : 33, .01f);
        }
        /* The last item has gone but its published cap remains until a setter.
         * Saving must retain that distinction as well as the following steps. */
        cstring_t file = "/tmp/openwarcraft3-boots-published-speed-save.bin";
        T_EQ(unit->movement.flat_speed_bonus, 60);
        T_ASSERT(WriteGame(file));
        uint32_t expected[8][4];
        FOR_LOOP(i, 8) {
            unit->currentmove->think(unit);
            expected[i][0] = wc3_float_bits(unit->s.origin2.x); expected[i][1] = wc3_float_bits(unit->s.origin2.y);
            expected[i][2] = wc3_float_bits(unit->movement.velocity.x); expected[i][3] = wc3_float_bits(unit->movement.velocity.y);
        }
        T_ASSERT(ReadGame(file));
        T_EQ(unit->movement.flat_speed_bonus, 60);
        T_EQ(S_UnitMoveSpeed(unit), 270);
        FOR_LOOP(i, 8) {
            unit->currentmove->think(unit);
            T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][0]); T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][1]);
            T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][2]); T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][3]);
        }
        jass_callbyname(level.vm, "PublishBootsSpeed", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_EQ(unit->movement.flat_speed_bonus, 0);
        T_FEQ(Vector2_len(&unit->movement.velocity), 270, .01f);
        vec2_t before = unit->s.origin2;
        unit->currentmove->think(unit);
        T_FEQ(Vector2_distance(&unit->s.origin2, &before), 27, .01f);
        remove(file);
    }
    reset_entities();
    G_SetSLKRows("AbilityData", old_abilities); free_slk_rows(abilities);
    G_SetSLKRows("ItemData", old_items); free_slk_rows(items);
}

TEST(wc3_movement, flat_speed_bonus_aliases_reduce_all_sources_and_ignore_transport_items) {
    reset_entities(); setup_test_world();
    const char slk[] =
        "ID;PWXL;N;EBB;Y8;X4\nC;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\nC;Y2;X4;K\"1\"\n"
        "C;Y3;X1;K\"Aivb\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X3;K\"6\"\nC;Y3;X4;K\"0\"\n"
        "C;Y4;X1;K\"A001\"\nC;Y4;X2;K\"AIms\"\nC;Y4;X3;K\"50\"\n"
        "C;Y5;X1;K\"A002\"\nC;Y5;X2;K\"AIms\"\nC;Y5;X3;K\"25\"\n"
        "C;Y6;X1;K\"A003\"\nC;Y6;X2;K\"AIms\"\nC;Y6;X3;K\"-10\"\n"
        "C;Y7;X1;K\"A004\"\nC;Y7;X2;K\"AIms\"\nC;Y7;X3;K\"25\"\n"
        "C;Y8;X1;K\"A005\"\nC;Y8;X2;K\"AIms\"\nC;Y8;X3;K\"75\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    UnitAbilities_t native = {.abilList = "AInv,A004,A005"}, lower = {.abilList = "AInv,A004"};
    UnitAbilities_t transport = {.abilList = "Aivb"};
    ItemData_t stronger = {.abilList = "A001"}, weaker = {.abilList = "A002"}, negative = {.abilList = "A003"};
    edict_t *unit = make_moving_unit(320, 320);
    unit->data.UnitAbilities = &native;
    S_SetUnitMoveSpeed(unit, 270);
    T_EQ(S_UnitMoveSpeed(unit), 345); /* All native contributors must be visited. */
    T_EQ(unit->movement.flat_speed_bonus, 75);
    ItemData_t *profiles[] = {&stronger, &weaker, &negative};
    FOR_LOOP(i, sizeof(profiles) / sizeof(*profiles)) {
        edict_t *item = alloc_test_unit(MAKEFOURCC('s','p','r','o'), 64 + i * 32, 320);
        item->targtype = TARG_ITEM; item->data.ItemData = profiles[i];
        item->item.inventory_slot = -1; item->item.in_world = true;
        T_ASSERT(G_PickupItem(unit, item));
    }
    T_EQ(S_UnitMoveSpeed(unit), 345);
    unit->data.UnitAbilities = &lower;
    T_EQ(S_UnitMoveSpeed(unit), 320); /* Item50 beats native25, never sums. */
    T_ASSERT(G_DetachItemAtScripted(unit, 0));
    T_EQ(S_UnitMoveSpeed(unit), 295);
    unit->data.UnitAbilities = &transport;
    T_ASSERT(!G_InventoryCanUseItems(unit));
    T_EQ(S_UnitMoveSpeed(unit), 270);
    S_SetUnitMoveSpeed(unit, 270);
    T_EQ(unit->movement.flat_speed_bonus, 0);
    reset_entities(); G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, map_movement_profiles_inherit_and_bind_to_created_units) {
    reset_entities(); setup_test_world();
    uint32_t base_id = MAKEFOURCC('h','f','o','o');
    UnitData_t const *stock = G_UnitData(base_id);
    float turn = .125f, window = .25f;
    char amphibious[] = "amph", floating[] = "float", flying[] = "fly";
    unitModification_t original_mods[] = {
        { .modID = MAKEFOURCC('u','m','v','t'), .type = mod_string, .data = amphibious },
        { .modID = MAKEFOURCC('u','m','v','r'), .type = mod_real, .data = &turn },
        { .modID = MAKEFOURCC('u','p','r','w'), .type = mod_real, .data = &window }
    };
    unitModification_t float_mod = { .modID = MAKEFOURCC('u','m','v','t'), .type = mod_string, .data = floating };
    unitModification_t fly_mod = { .modID = MAKEFOURCC('u','m','v','t'), .type = mod_string, .data = flying };
    unitData_t original = { .originalUnitID = base_id, .numbeOfModifications = 3, .modifications = original_mods };
    unitData_t custom[] = {
        { .originalUnitID = base_id, .newUnitID = MAKEFOURCC('h','0','0','3') },
        { .originalUnitID = base_id, .newUnitID = MAKEFOURCC('h','0','0','4'), .numbeOfModifications = 1, .modifications = &float_mod },
        { .originalUnitID = base_id, .newUnitID = MAKEFOURCC('h','0','0','5'), .numbeOfModifications = 1, .modifications = &fly_mod }
    };
    mapInfo_t const *saved = level.mapinfo;
    mapInfo_t info = *saved;
    info.num_originalUnits = 1; info.originalUnits = &original;
    info.num_userCreatedUnits = 3; info.userCreatedUnits = custom;
    level.mapinfo = &info; G_SetMapUnitOverrides(&info);
    T_ASSERT(run_test_jass(
        "function main takes nothing returns nothing\n"
        " local unit a = CreateUnit(Player(0), 'h003', 320.0, 320.0, 0.0)\n"
        " local unit b = CreateUnit(Player(0), 'h004', 480.0, 320.0, 0.0)\n"
        " local unit c = CreateUnit(Player(0), 'h005', 640.0, 320.0, 0.0)\n"
        "endfunction\n"));
    uint32_t found = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *unit = g_edicts + i;
        if (!unit->inuse) continue;
        FOR_LOOP(j, 3) {
            if (unit->class_id != custom[j].newUnitID) continue;
            cstring_t types[] = { amphibious, floating, flying };
            uint8_t masks[] = { CM_PATHING_UNAMPHIBIOUS, CM_PATHING_UNFLOATABLE, CM_PATHING_UNFLYABLE };
            ++found;
            T_EQ(unit->data.UnitData, G_UnitData(custom[j].newUnitID));
            T_STREQ(unit->data.UnitData->moveTypeName, types[j]);
            T_EQ(unit->data.UnitData->turnRate, turn); T_EQ(unit->data.UnitData->propWin, window);
            T_EQ(M_UnitStaticPathingFlags(unit), masks[j]);
        }
    }
    T_EQ(found, 3); T_STREQ(G_UnitData(base_id)->moveTypeName, amphibious);
    reset_entities(); G_SetMapUnitOverrides(NULL); level.mapinfo = saved; setup_test_world();
    T_EQ(G_UnitData(base_id), stock);
}

/* Explicit zero is a setter value, and must survive the normal save image. */
TEST(wc3_movement, public_speed_zero_survives_save_and_resumes_identically) {
    reset_entities(); setup_test_world();
    float old_minimum = game.constants.minUnitSpeed, old_maximum = game.constants.maxUnitSpeed;
    game.constants.minUnitSpeed = 150; game.constants.maxUnitSpeed = 400;
    cstring_t file = "/tmp/openwarcraft3-speed-zero-save.bin";
    T_ASSERT(run_test_jass(
        "globals\n unit speedUnit\nendglobals\n"
        "function main takes nothing returns nothing\n"
        " set speedUnit = CreateUnit(Player(0), 'hpea', 320.0, 320.0, 0.0)\n"
        " call SetUnitMoveSpeed(speedUnit, 0.0)\n"
        " call BJassAssert(GetUnitMoveSpeed(speedUnit) == 150.0, \"zero selects configured minimum\")\n"
        "endfunction\n"
        "function BeginSpeedMove takes nothing returns nothing\n"
        " call BJassAssert(IssuePointOrder(speedUnit, \"move\", 800.0, 320.0), \"saved Move accepted\")\n"
        "endfunction\n"));
    edict_t *unit = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','p','e','a')) unit = g_edicts + i;
    T_NOT_NULL(unit);
    if (unit) {
        /* Minimal fixture needs the ordinary active lifecycle before public orders. */
        unit->health.value = unit->health.max_value = 250; unit->stand = unit_stand; unit_stand(unit);
        jass_callbyname(level.vm, "BeginSpeedMove", false);
        T_ASSERT(!jass_rterror_pending(level.vm));
        T_NOT_NULL(unit->currentmove->think);
        if (!unit->currentmove->think) goto done;
        unit->currentmove->think(unit);
        T_ASSERT(WriteGame(file));
        uint32_t expected[8][4];
        FOR_LOOP(i, 8) {
            unit->currentmove->think(unit);
            expected[i][0] = wc3_float_bits(unit->s.origin2.x); expected[i][1] = wc3_float_bits(unit->s.origin2.y);
            expected[i][2] = wc3_float_bits(unit->movement.velocity.x); expected[i][3] = wc3_float_bits(unit->movement.velocity.y);
        }
        S_SetUnitMoveSpeed(unit, 1000);
        T_ASSERT(ReadGame(file));
        T_EQ(unit->unitinfo.MoveSpeed, 0); T_ASSERT(unit->unitinfo.move_flags & BZ_UNIT_SPEED_SET);
        T_EQ(S_UnitMoveSpeed(unit), 150); T_FEQ(unit_movedistance(unit), 15, .001f);
        FOR_LOOP(i, 8) {
            unit->currentmove->think(unit);
            T_EQ(wc3_float_bits(unit->s.origin2.x), expected[i][0]); T_EQ(wc3_float_bits(unit->s.origin2.y), expected[i][1]);
            T_EQ(wc3_float_bits(unit->movement.velocity.x), expected[i][2]); T_EQ(wc3_float_bits(unit->movement.velocity.y), expected[i][3]);
        }
    }
done:
    remove(file);
    game.constants.minUnitSpeed = old_minimum; game.constants.maxUnitSpeed = old_maximum;
}

TEST(wc3_movement, authored_zero_window_and_turn_rate_keep_their_meaning) {
    edict_t *unit = make_moving_unit(0, 0);
    UnitData_t data = *unit->data.UnitData;
    data.turnRate = 0; data.propWin = 0;
    unit->data.UnitData = &data;
    vec2_t const north = {0, 512};
    unit_changeangle_towards_point(unit, &north);
    unit_moveindirection(unit);
    T_EQ(unit->unitinfo.move_flags, 0);
    T_EQ(wc3_float_bits(unit_turnspeed(unit)), 0x3a83126fu);
    T_EQ(wc3_float_bits(unit->s.angle), 0x3a83126fu);
    T_ASSERT(unit->movement.turn_blocked);
    T_EQ(unit->s.origin2.x, 0); T_EQ(unit->s.origin2.y, 0);
}

TEST(wc3_movement, scripted_window_stops_translation_while_turning) {
    edict_t *unit = make_scripted_turn_unit();
    vec2_t const goal = {0, 512};
    T_NOT_NULL(unit);
    if (!unit) return;
    unit_changeangle_towards_point(unit, &goal);
    unit_moveindirection(unit);
    T_EQ(unit->s.origin2.x, 0); T_EQ(unit->s.origin2.y, 0);
    T_EQ(unit->s.angle, 0.125f);
    T_EQ(unit->movement.velocity.x, 0); T_EQ(unit->movement.velocity.y, 0);
    FOR_LOOP(i, 16) {
        unit_changeangle_towards_point(unit, &goal);
        unit_moveindirection(unit);
    }
    T_ASSERT(unit->s.origin2.y > 0);
}

TEST(wc3_movement, scripted_window_equality_and_zero_keep_turning) {
    edict_t *unit = make_scripted_turn_unit();
    vec2_t const east = {512, 0};
    T_NOT_NULL(unit);
    if (!unit) return;
    unit->s.angle = 0.5f;
    unit_changeangle_towards_point(unit, &east);
    unit_moveindirection(unit);
    T_EQ(unit->s.origin2.x, 0); T_EQ(unit->s.origin2.y, 0);
    T_EQ(unit->s.angle, 0.375f);
    unit->unitinfo.PropWindow = 0;
    unit_changeangle_towards_point(unit, &east);
    unit_moveindirection(unit);
    T_EQ(unit->s.origin2.x, 0); T_EQ(unit->s.origin2.y, 0);
    T_EQ(unit->s.angle, 0.25f);
}

/* Point arrival keeps turning inside its minimum range without translation. */
TEST(wc3_movement, scripted_point_order_turns_before_arriving) {
    edict_t *unit = make_scripted_turn_unit();
    vec2_t const goal = {10, 0};
    T_NOT_NULL(unit);
    if (!unit) return;
    unit->s.angle = 1;
    bool accepted = unit_issueorder(unit, "move", &goal);
    T_ASSERT(accepted);
    if (!accepted) return;
    unit->currentmove->think(unit);
    T_EQ(unit->s.origin2.x, 0); T_EQ(unit->s.origin2.y, 0);
    T_EQ(unit->s.angle, 0.875f);
    T_EQ(unit->currentmove->proc, CAbilityMove);
    FOR_LOOP(i, 8) unit->currentmove->think(unit);
    T_EQ(unit->s.origin2.x, 0);
    T_STREQ(unit->currentmove->animation, "stand");
}

/* Primitive override flags and the pre-turn decision travel in the ordinary edict save image. */
TEST(wc3_movement, scripted_turn_state_survives_save_load) {
    edict_t *unit = make_scripted_turn_unit();
    cstring_t file = "/tmp/openwarcraft3-scripted-turn-save.bin";
    vec2_t const goal = {0, 512};
    T_NOT_NULL(unit);
    if (!unit) return;
    unit_changeangle_towards_point(unit, &goal);
    T_ASSERT(unit->movement.turn_blocked);
    T_ASSERT(WriteGame(file));
    unit->unitinfo.move_flags = 0; unit->unitinfo.TurnSpeed = 0; unit->unitinfo.PropWindow = 0;
    unit->movement.turn_blocked = false;
    T_ASSERT(ReadGame(file));
    T_EQ(unit->unitinfo.move_flags, BZ_UNIT_TURN_SET | BZ_UNIT_WINDOW_SET);
    T_EQ(unit->unitinfo.TurnSpeed, 0.125f); T_EQ(unit->unitinfo.PropWindow, 0.5f);
    T_ASSERT(unit->movement.turn_blocked);
    unit_moveindirection(unit);
    T_EQ(unit->s.origin2.x, 0); T_EQ(unit->s.origin2.y, 0);
    remove(file);
}

/* Harvest damage now uses the authoritative destructable lifecycle, so test
 * trees must carry the initialization normally supplied by SP_SpawnDestructable. */
static edict_t *make_harvest_tree(float x, float y, float life) {
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), x, y);
    SP_monster_tree(tree);
    tree->destructable.initialized = true;
    tree->destructable.item_table = (uint32_t)-1;
    tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = life;
    return tree;
}

static UnitAbilities_t const harvest_abilities = { .abilList = "Ahar" };
static UnitAbilities_t const ghoul_harvest_abilities = { .abilList = "Ahrl" };
static UnitAbilities_t const wisp_harvest_abilities = { .abilList = "Awha" };
static UnitProfile_t const wisp_rally_producer_profile = { .trains = "ewsp" };
static UnitAbilities_t const return_gold_lumber_abilities = { .abilList = "Argl" };
static UnitAbilities_t const return_lumber_abilities = { .abilList = "Arlm" };

static void make_live_dropoff(edict_t *building, UnitAbilities_t const *abilities) {
    building->data.UnitAbilities = abilities;
    building->health.value = building->health.max_value = 1000.0f;
}

/* Command-integration tests exercise server selection/target-mode state, not
 * svc_layout transport. Keep their HUD refreshes inside the game-test boundary. */
static void movement_noop_write(pfWriteType_t type, void const *value) { (void)type; (void)value; }
static void movement_noop_unicast(edict_t *ent) { (void)ent; }

typedef struct {
    uint32_t count;
    pfWriteType_t type[4];
    int32_t value[4];
    edict_t *recipient;
} smartIndicatorCapture_t;

static smartIndicatorCapture_t smart_indicator_capture;

static void movement_capture_indicator_write(pfWriteType_t type, void const *value) {
    uint32_t slot = smart_indicator_capture.count++;
    if (slot >= 4) return;
    smart_indicator_capture.type[slot] = type;
    if (value) smart_indicator_capture.value[slot] = *(int32_t const *)value;
}

static void movement_capture_indicator_unicast(edict_t *ent) {
    if (!smart_indicator_capture.recipient) smart_indicator_capture.recipient = ent;
}

slkTestData_t *parse_slk_string(char const *slk_text);
void free_slk_rows(slkTestData_t *rows);


extern float HARVEST_GOLD_CAPACITY;
extern float HARVEST_TREE_DAMAGE;
extern float HARVEST_LUMBER_CAPACITY;
extern float HARVEST_RANGE;
extern float HARVEST_COOLDOWN;
extern float HARVEST_SEARCH_RANGE;
extern void harvest_cooldown(edict_t *);
bool harvest_menu_selecttarget(edict_t *clent, edict_t *target);

static const char slk_wisp_harvest_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"DataA1\"\n"
    "C;Y1;X4;K\"Rng1\"\n"
    "C;Y1;X5;K\"Dur1\"\n"
    "C;Y2;X1;K\"Awha\"\n"
    "C;Y2;X2;K\"Awha\"\n"
    "C;Y2;X3;K9\n"
    "C;Y2;X4;K500\n"
    "C;Y2;X5;K1.0\n"
    "E\n";

/* ROC/TFT stock Awha values from ability_audit -raw Awha. Keep a stock-shaped
 * fixture beside the non-stock DataA case above. */
static const char slk_wisp_harvest_stock_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
    "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"Rng1\"\n"
    "C;Y1;X7;K\"Dur1\"\n"
    "C;Y2;X1;K\"Awha\"\nC;Y2;X2;K\"Awha\"\n"
    "C;Y2;X3;K5\nC;Y2;X4;K5\nC;Y2;X5;K150\n"
    "C;Y2;X6;K900\nC;Y2;X7;K8\nE\n";

static const char slk_ghoul_harvest_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"DataA1\"\n"
    "C;Y1;X4;K\"DataB1\"\n"
    "C;Y1;X5;K\"Rng1\"\n"
    "C;Y1;X6;K\"Dur1\"\n"
    "C;Y2;X1;K\"Ahrl\"\n"
    "C;Y2;X2;K\"Ahrl\"\n"
    "C;Y2;X3;K3\n"
    "C;Y2;X4;K5\n"
    "C;Y2;X5;K128\n"
    "C;Y2;X6;K0.25\n"
    "E\n";

static slkTestData_t *install_ghoul_harvest_test_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(slk_ghoul_harvest_test_data);
    *rows_out = rows;
    return G_SetSLKRows("AbilityData", rows);
}

TEST(wc3_movement, harvest_command_button_toggles_to_return_resources_ui) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    gameCommandButton_t button;

    worker->data.UnitAbilities = &harvest_abilities;

    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.command, "Ahar");
    T_STREQ(button.art, "TestUI\\Textures\\gather.blp");
    T_STREQ(button.tooltip, "Gather");
    T_STREQ(button.ubertip, "Gather resources from a Gold Mine or tree.");
    T_EQ(button.hotkey, 'G');
    T_EQ(button.x, 0);
    T_EQ(button.y, 1);

    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 1);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.command, "Ahar");
    T_STREQ(button.art, "TestUI\\Textures\\return-resources.blp");
    T_STREQ(button.tooltip, "Return Resources");
    T_STREQ(button.ubertip, "Return carried resources to a compatible drop-off.");
    T_EQ(button.hotkey, 'R');
    T_EQ(button.x, 3);
    T_EQ(button.y, 2);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 7);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.tooltip, "Return Resources");

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 0);
    T_ASSERT(G_BuildCommandButton(worker, "Ahar", false, 0, &button));
    T_STREQ(button.tooltip, "Gather");
}

TEST(wc3_movement, ghoul_ahrl_smart_uses_lumber_only_harvest_data) {
    slkTestData_t *rows, *old_abilities;
    edict_t *worker, *tree;
    float saved_range = HARVEST_RANGE;
    float saved_damage = HARVEST_TREE_DAMAGE;
    float saved_capacity = HARVEST_LUMBER_CAPACITY;

    worker = make_moving_unit(0.0f, 0.0f);
    old_abilities = install_ghoul_harvest_test_data(&rows);
    worker->data.UnitAbilities = &ghoul_harvest_abilities;
    worker->unitinfo.MoveSpeed = 100.0f;
    tree = make_harvest_tree(64.0f, 0.0f, 100.0f);

    /* Deliberately make the legacy globals incompatible with this order. The
     * Ahrl row must supply capacity/range/damage for this worker instead. */
    HARVEST_RANGE = 1.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 1.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 2);

    T_ASSERT(S_HarvestCanLumber(worker));
    T_ASSERT(!S_HarvestCanGold(worker));
    T_ASSERT(unit_issuetargetorder(worker, "smart", tree));
    T_ASSERT(worker->goalentity == tree);
    T_STREQ(worker->currentmove->animation, "walk");

    /* Ahrl Rng1=128 means the 64-unit target is already in chop range even
     * though the legacy global above is only 1. */
    worker->currentmove->think(worker);
    T_STREQ(worker->currentmove->animation, "attack");
    worker->wait = FRAMETIME / 1000.0f;
    worker->currentmove->think(worker);
    T_FEQ(tree->health.value, 97.0f, 0.001f);
    T_EQ(worker->harvested_lumber, 5);

    HARVEST_RANGE = saved_range;
    HARVEST_TREE_DAMAGE = saved_damage;
    HARVEST_LUMBER_CAPACITY = saved_capacity;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, ghoul_ahrl_command_targets_tree_and_autoharvests_lumber) {
    slkTestData_t *rows, *old_abilities;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client;
    edict_t *worker, *tree;
    abilityCall_t call;

    worker = make_moving_unit(0.0f, 0.0f);
    client = &game.clients[0];
    clent->client = client;
    old_abilities = install_ghoul_harvest_test_data(&rows);
    worker->data.UnitAbilities = &ghoul_harvest_abilities;
    worker->s.player = client->ps.number;
    tree = make_harvest_tree(96.0f, 0.0f, 100.0f);
    G_SelectEntity(client, worker);

    call = MAKE(abilityCall_t, .client = clent);
    T_ASSERT(CAbilityHarvestLumber(worker, A_COMMAND, &call));
    T_NOT_NULL(client->menu.on_entity_selected);
    T_ASSERT(client->menu.on_entity_selected(clent, tree));
    T_ASSERT(worker->goalentity == tree);

    unit_stand(worker);
    T_ASSERT(unit_issueimmediateorder(worker, "autoharvestlumber"));
    T_ASSERT(worker->goalentity == tree);
    T_ASSERT(!unit_issueimmediateorder(worker, "autoharvestgold"));

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, runtime_added_call_to_arms_exposes_on_and_off_buttons) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    gameCommandButton_t buttons[16];
    uint8_t count;
    bool found_on = false, found_off = false;

    T_ASSERT(G_ActorAddSkill(worker, MAKEFOURCC('A','m','i','c')));
    count = G_GetCommandButtons(worker, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "Amic")) {
            found_on = true;
            T_STREQ(buttons[i].tooltip, "Call to Arms");
            T_EQ(buttons[i].x, 1);
            T_EQ(buttons[i].y, 1);
        } else if (!strcmp(buttons[i].command, "Amic:off")) {
            found_off = true;
            T_STREQ(buttons[i].tooltip, "Back to Work");
            T_EQ(buttons[i].x, 2);
            T_EQ(buttons[i].y, 1);
        }
    }
    T_ASSERT(found_on);
    T_ASSERT(found_off);

    T_ASSERT(G_ActorRemoveSkill(worker, MAKEFOURCC('A','m','i','c')));
    count = G_GetCommandButtons(worker, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        T_ASSERT(strcmp(buttons[i].command, "Amic") != 0);
        T_ASSERT(strcmp(buttons[i].command, "Amic:off") != 0);
    }
}

TEST(wc3_movement, missing_melee_amic_recovers_only_first_tier_one_hall) {
    static UnitAbilities_t const townhall_abilities = {
        .id = MAKEFOURCC('h','t','o','w'),
        .abilList = "",
    };
    edict_t *first = make_moving_unit(0.0f, 0.0f);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 512.0f, 0.0f);

    first->class_id = first->s.class_id = MAKEFOURCC('h','t','o','w');
    first->data.UnitAbilities = &townhall_abilities;
    first->svflags |= SVF_MONSTER;
    first->s.player = 0;
    first->spawn_time = 100;

    second->data.UnitAbilities = &townhall_abilities;
    second->svflags |= SVF_MONSTER;
    second->s.player = 0;
    second->spawn_time = 200;

    T_ASSERT(S_MilitiaEnsureHallAbility(first));
    T_ASSERT(G_ActorHasSkill(first, "Amic"));
    T_ASSERT(!S_MilitiaEnsureHallAbility(second));
    T_ASSERT(!G_ActorHasSkill(second, "Amic"));
}

TEST(wc3_movement, carried_resource_toggle_invalidates_selected_command_card) {
    gameClient_t *client = &game.clients[0];
    edict_t *worker = make_moving_unit(0.0f, 0.0f);

    worker->s.player = client->ps.number;
    G_SelectEntity(client, worker);
    client->commands_dirty = false;

    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 1);
    T_ASSERT(client->commands_dirty);

    client->commands_dirty = false;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 2);
    T_ASSERT(!client->commands_dirty);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 7);
    T_ASSERT(!client->commands_dirty);

    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 0);
    T_ASSERT(client->commands_dirty);
}

static const char slk_goldmine_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"Data11\"\n"
    "C;Y1;X4;K\"Data12\"\n"
    "C;Y1;X5;K\"Data13\"\n"
    "C;Y2;X1;K\"Agld\"\n"
    "C;Y2;X2;K\"Agld\"\n"
    "C;Y2;X3;K12500\n"
    "C;Y2;X4;K1\n"
    "C;Y2;X5;K1\n"
    "C;Y3;X1;K\"A001\"\n"
    "C;Y3;X2;K\"Agld\"\n"
    "C;Y3;X3;K100\n"
    "C;Y3;X4;K0.01\n"
    "C;Y3;X5;K1\n"
    "C;Y4;X1;K\"A002\"\n"
    "C;Y4;X2;K\"Agld\"\n"
    "C;Y4;X3;K200\n"
    "C;Y4;X4;K2\n"
    "C;Y4;X5;K2\n"
    "E\n";

static UnitAbilities_t const test_goldmine_stock = { .abilList = "Agld" };
static UnitAbilities_t const test_goldmine_cap1 = { .abilList = "A001" };
static UnitAbilities_t const test_goldmine_cap2 = { .abilList = "A002" };

static const char slk_racial_goldmine_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"Rng1\"\n"
    "C;Y1;X4;K\"DataA1\"\n"
    "C;Y1;X5;K\"DataB1\"\n"
    "C;Y1;X6;K\"DataC1\"\n"
    "C;Y1;X7;K\"DataD1\"\n"
    "C;Y1;X8;K\"UnitID1\"\n"
    "C;Y1;X9;K\"Rng2\"\n"
    "C;Y1;X10;K\"Dur1\"\n"
    "C;Y1;X11;K\"DataA1\"\n"
    "C;Y1;X12;K\"DataB1\"\n"
    "C;Y1;X13;K\"UnitID1\"\n"
    "C;Y1;X14;K\"isbldg\"\n"
    "C;Y1;X15;K\"alias\"\n"
    "C;Y1;X16;K\"Dur1\"\n"
    "C;Y1;X17;K\"HeroDur1\"\n"
    "C;Y2;X1;K\"Agld\"\n"
    "C;Y2;X2;K\"Agld\"\n"
    "C;Y3;X1;K\"Aaha\"\n"
    "C;Y3;X2;K\"Aaha\"\n"
    "C;Y3;X3;K64\n"
    "C;Y4;X1;K\"Abgm\"\n"
    "C;Y4;X2;K\"Abgm\"\n"
    "C;Y4;X4;K10\n"
    "C;Y4;X5;K1\n"
    "C;Y4;X6;K5\n"
    "C;Y4;X7;K200\n"
    "C;Y5;X1;K\"Aegm\"\n"
    "C;Y5;X2;K\"Aegm\"\n"
    "C;Y5;X4;K10\n"
    "C;Y5;X5;K1\n"
    "C;Y6;X1;K\"Aenc\"\n"
    "C;Y6;X2;K\"Aenc\"\n"
    "C;Y6;X4;K5\n"
    "C;Y7;X1;K\"Agl2\"\n"
    "C;Y7;X2;K\"Agl2\"\n"
    "C;Y8;X1;K\"Aent\"\n"
    "C;Y8;X2;K\"Aent\"\n"
    "C;Y8;X3;K64\n"
    "C;Y8;X13;K\"hbar\"\n"
    "C;Y8;X14;K1\n"
    "C;Y8;X15;K\"Aent\"\n"
    "C;Y9;X1;K\"Aro1\"\n"
    "C;Y9;X2;K\"Aro1\"\n"
    "C;Y9;X16;K1\n"
    "C;Y9;X17;K1\n"
    "E\n";

static UnitAbilities_t const test_haunted_mine = { .abilList = "Abgm" };
static UnitAbilities_t const test_acolyte_harvest = { .abilList = "Aaha" };
static UnitAbilities_t const test_entangled_mine = { .abilList = "Aegm,Aenc" };
static UnitAbilities_t const test_entangle_caster = { .abilList = "Aent,Aro1" };

static slkTestData_t *install_racial_goldmine_test_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(slk_racial_goldmine_test_data);
    *rows_out = rows;
    return G_SetSLKRows("AbilityData", rows);
}

static uint32_t count_haunted_ring_effects(edict_t const *mine) {
    uint32_t count = 0;
    FILTER_EDICTS(effect, effect->inuse && effect->owner == mine &&
                  effect->summon_ability == MAKEFOURCC('A','b','g','m') &&
                  effect->resources > 0 && (effect->s.flags & EF_NOT_SELECTABLE)) {
        count++;
    }
    return count;
}

static edict_t *haunted_ring_effect_slot(edict_t *mine, uint32_t slot) {
    FILTER_EDICTS(effect, effect->inuse && effect->owner == mine &&
                  effect->summon_ability == MAKEFOURCC('A','b','g','m') &&
                  effect->resources == slot + 1 && (effect->s.flags & EF_NOT_SELECTABLE)) {
        return effect;
    }
    return NULL;
}

static slkTestData_t *install_goldmine_test_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(slk_goldmine_test_data);
    *rows_out = rows;
    return G_SetSLKRows("AbilityData", rows);
}

static void setup_test_goldmine(edict_t *mine, UnitAbilities_t const *abilities, uint32_t resources) {
    mine->data.UnitAbilities = abilities;
    mine->resources = resources;
    mine->health.value = mine->health.max_value = 1000.0f;
}

static pathTex_t *movement_make_goldmine_pathtex(void) {
    enum { W = 16, H = 16 };
    pathTex_t *tex = gi.MemAlloc(sizeof(*tex) + W * H * sizeof(color32_t));
    T_ASSERT(tex != NULL);
    tex->width = W;
    tex->height = H;
    FOR_LOOP(i, W * H)
        tex->map[i] = (color32_t){ 0, 0, 0, 255 };
    for (int y = 4; y < 12; y++) {
        for (int x = 4; x < 12; x++)
            tex->map[x + y * W].b = 255;
    }
    return tex;
}

static edict_t *add_gold_worker(float x, float y) {
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    worker->movetype = MOVETYPE_STEP;
    worker->stand = unit_stand;
    worker->die = unit_die;
    worker->collision = 16.0f;
    worker->health.value = worker->health.max_value = 250.0f;
    worker->unitinfo.MoveSpeed = 100.0f;
    unit_stand(worker);
    return worker;
}

static bool tree_died;
static uint32_t tree_pained;
static void test_tree_die(edict_t *tree, edict_t *attacker) { (void)tree; (void)attacker; tree_died = true; }
static void test_tree_pain(edict_t *tree) { (void)tree; tree_pained++; }

typedef struct {
    gameMsg_t msg[32];
    uint32_t count;
} msgTrace_t;

static void trace_message(gameMsg_t const *msg, void *ctx) {
    msgTrace_t *trace = ctx;
    if (trace->count < sizeof(trace->msg) / sizeof(trace->msg[0]))
        trace->msg[trace->count++] = *msg;
}

/* Worker resource movement mirrors CBehaviorHarvest's
 * disableCollision=true contract for unit targets.  A live Peasant directly
 * in the mine lane must therefore not deflect or stop the approaching miner;
 * static pathing remains enabled separately. */
TEST(wc3_movement, worker_resource_gold_approach_ignores_live_units) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    vec2_t const origin = worker->s.origin2;
    slkTestData_t *rows, *old_abilities;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(mine);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.origin2.x > origin.x);
    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) <
             worker->collision + blocker->collision);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Resource movement must route static geometry with the worker's real collision radius.
 * A Farm-sized obstacle across the direct mine lane reproduces the failure
 * where the old point-sized field chose cells a Peasant could not physically
 * traverse.  The bounded per-mover accelerator should immediately own a
 * collision-sized detour while the shared field is rebuilt. */
TEST(wc3_movement, worker_resource_static_detour_uses_worker_radius) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    slkTestData_t *rows, *old_abilities;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    /* Begin on an open lane, then rebuild the static map with a 4x4 block
     * centred ahead of the already-moving worker, matching construction start. */
    worker->currentmove->think(worker);
    T_ASSERT(worker->s.origin2.x > -320.0f);
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path.valid);
    T_FEQ(worker->movement.path.radius, worker->collision, 0.001f);
    T_ASSERT(fabsf(worker->movement.path.waypoint.y) >= CM_PathCellWorldSize());

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Returning gold should target the nearest legal edge of a blocked drop-off,
 * not the arbitrary pathable cell chosen around its centre.  Keep a Farm-sized
 * obstacle in the lane so this also proves the mover-owned detour is aimed at
 * that near-side edge rather than at the Town Hall centre/far side. */
TEST(wc3_movement, worker_resource_gold_return_targets_near_side_edge) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -500.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    /* Hall authored footprint: centre cell is x=42/y=32 in these bounds and
     * movement_make_goldmine_pathtex() blocks local cells 4..11. */
    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    /* Farm-sized obstacle between the mine side and the Hall's left edge. */
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path.valid);
    T_FEQ(worker->movement.path.radius, worker->collision, 0.001f);
    T_ASSERT(worker->movement.path.target.x < hall->s.origin2.x);
    T_ASSERT(worker->movement.path.target.x > worker->s.origin2.x);

    hall->pathtex = NULL;
    gi.MemFree(hall_pathtex);
}

/* Lumber Return Resources uses the same collision contract but a separate
 * behavior.  A Lumber Mill to the right must likewise keep the route endpoint
 * on its left/near edge, even when the worker has to detour around new static
 * construction on the way there. */
TEST(wc3_movement, worker_resource_lumber_return_targets_near_side_edge) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 0.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    mill->collision = 64.0f;
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(mill);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    for (int y = 30; y <= 33; y++)
        for (int x = 30; x <= 33; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_lumber_return_to(worker, mill));
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.path.valid);
    T_FEQ(worker->movement.path.radius, worker->collision, 0.001f);
    T_ASSERT(worker->movement.path.target.x < mill->s.origin2.x);
    T_ASSERT(worker->movement.path.target.x > worker->s.origin2.x);

    mill->pathtex = NULL;
    gi.MemFree(mill_pathtex);
}

/* Collision-sized static routing is cell-centred and therefore can stop just
 * outside the continuous footprint+step deposit test.  Once resource routing reaches
 * the innermost legal near-side endpoint, Return Resources must accept that
 * route end instead of repeatedly steering back across it. */
TEST(wc3_movement, worker_resource_gold_deposits_at_near_side_route_endpoint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    vec2_t approach;
    float route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    route_band = worker->collision + CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindInnerApproachPointToFootprintForRadius(
        hall, &worker->s.origin2, route_band, worker->collision, &approach));
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &approach) >
             worker->collision + unit_movedistance(worker));
    worker->s.origin2 = approach;
    gi.LinkEntity(worker);

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));

    hall->pathtex = NULL;
    gi.MemFree(hall_pathtex);
}

TEST(wc3_movement, worker_resource_lumber_deposits_at_near_side_route_endpoint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 0.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];
    vec2_t approach;
    float route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    mill->collision = 64.0f;
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(mill);

    for (int y = 28; y < 36; y++)
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    route_band = worker->collision + CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindInnerApproachPointToFootprintForRadius(
        mill, &worker->s.origin2, route_band, worker->collision, &approach));
    T_ASSERT(CM_DistanceToPathingFootprint(mill, &approach) >
             worker->collision + unit_movedistance(worker));
    worker->s.origin2 = approach;
    gi.LinkEntity(worker);

    T_ASSERT(harvest_lumber_return_to(worker, mill));
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));

    mill->pathtex = NULL;
    gi.MemFree(mill_pathtex);
}

/* Destructables are the opposite branch in Warsmash: Harvest resets the same
 * generic mover with collision enabled.  A tree approach may route/slide around
 * another unit, but must never commit a step through its collision circle. */
TEST(wc3_movement, worker_resource_tree_approach_keeps_live_unit_collision) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    float const saved_range = HARVEST_RANGE;
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    edict_t *tree = make_harvest_tree(400.0f, 0.0f, 100.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(tree);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) >=
             worker->collision + blocker->collision);
    HARVEST_RANGE = saved_range;
}

/* CBehaviorReturnResources always disables live-unit collision in Warsmash,
 * independent of whether the carried resource is gold or lumber. */
TEST(wc3_movement, worker_resource_lumber_return_ignores_live_units) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 35.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 400.0f, 0.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f;
    S_SetCarriedResource(worker, RETURN_RESOURCE_LUMBER, 10);
    blocker->collision = 16.0f;
    blocker->s.model = 1;
    blocker->movetype = MOVETYPE_NONE;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(blocker);
    gi.LinkEntity(hall);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_lumber_return_to(worker, hall));
    worker->currentmove->think(worker);

    T_ASSERT(Vector2_distance(&worker->s.origin2, &blocker->s.origin2) <
             worker->collision + blocker->collision);
}

/* Gold workers enter at the mine boundary; the mine's collision footprint must
 * not strand them just outside the older fixed interaction radius. */
TEST(wc3_movement, gold_worker_enters_large_mine_footprint) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; /* 8 blocked cells across in ROC 16x16Goldmine.tga. */
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 40) {
        worker->currentmove->think(worker);
        if (worker->s.renderfx & RF_HIDDEN) break;
    }

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}


/* A worker at the last legal cell beside an authored mine footprint must be
 * admitted when one movement step reaches the footprint.  Keep this fixture at
 * the interaction boundary so it tests mine-entry semantics independently of
 * global route-cache/build-budget state left by earlier pathfinding tests. */
TEST(wc3_movement, gold_worker_enters_mine_with_blocked_pathing_footprint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(158.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    /* Mirror the mine path texture's central 8x8 no-walk cells into the
     * static test map.  The entity carries the same authored pathtex so
     * interaction distance and movement pathing describe one footprint. */
    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(CM_PointIsPathableForRadius(&worker->s.origin2, worker->collision));
    T_ASSERT(!CM_PointIsPathableForRadius(&mine->s.origin2, 0.0f));
    T_ASSERT(CM_DistanceToPathingFootprint(mine, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* Resource-building legs ignore live units, so the old Human02 crowd-settle
 * shortcut is no longer part of mine entry. Static pathing remains authoritative:
 * a worker that cannot get its real collision radius within the authored mine
 * interaction boundary must keep the Harvest order alive rather than entering
 * through a blocked edge. */
TEST(wc3_movement, gold_worker_static_blocked_edge_does_not_fake_mine_entry) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(151.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 0.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();
    float footprint;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    /* An actual wall separates the worker from the interaction boundary.
     * The mine footprint alone leaves a legal class1 approach: its two-cell
     * query can advance to cell37 and enter at the authored distance. */
    for (int y = 0; y < CELLS; y++) pathmap[37 + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    footprint = CM_DistanceToPathingFootprint(mine, &worker->s.origin2);
    T_ASSERT(footprint > worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 20) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (worker->s.renderfx & RF_HIDDEN)
            break;
    }

    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(worker->goalentity == mine);
    T_STREQ(worker->currentmove->animation, "walk");
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* The mine pathing footprint is square/texture-authored, while mine->collision
 * is only a scalar approximation.  At a footprint corner the worker can be one
 * legal movement step from the no-walk cells while its centre distance is still
 * greater than worker+mine collision+step.  Mine entry must use the authored
 * footprint so routing cannot strand a diagonally approaching worker. */
TEST(wc3_movement, gold_worker_enters_at_pathing_footprint_corner) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(170.0f, 170.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 320.0f, 320.0f);
    pathTex_t *mine_pathtex = movement_make_goldmine_pathtex();

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    mine->pathtex = mine_pathtex;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    /* Centre-circle entry is deliberately still false at this corner.
     * Check the fixture geometry directly: harvest_gold_start() has not yet
     * assigned worker->goalentity, so M_DistanceToGoal() is not valid here. */
    T_ASSERT(Vector2_distance(&worker->s.origin2, &mine->s.origin2) >
             worker->collision + mine->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(mine, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    harvest_gold_start(worker, mine);
    worker->currentmove->think(worker);

    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_EQ(mine->peonsinside, 1);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(mine_pathtex);
}

/* A final chop equal to the remaining life must run the tree's death callback,
 * which owns its fall animation and pathing removal. */
TEST(wc3_movement, lumber_final_chop_fells_tree) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 10.0f);
    worker->attack1.damagePoint = 0.01f;
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;
    harvest_start(worker, tree);

    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);
    G_UnsubscribeMessage(trace_message, &trace);

    T_FEQ(tree->health.value, 0.0f, 0.01f);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_ASSERT(tree->svflags & SVF_DEADMONSTER);
    T_STREQ(tree->currentmove->animation, "death");
    T_EQ(trace.count, 4);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_LUMBER);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_START_CHOP);
    T_EQ(trace.msg[2].type, GAME_MSG_HARVEST_CHOP);
    T_EQ(trace.msg[3].type, GAME_MSG_HARVEST_TREE_FELLED);
    FOR_LOOP(i, trace.count) {
        T_EQ(trace.msg[i].actor, worker->s.number);
        T_EQ(trace.msg[i].target, tree->s.number);
    }
}

/* Non-lethal chops damage but do not fell a living tree. */
TEST(wc3_movement, lumber_nonlethal_chop_keeps_tree_standing) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 11.0f);
    tree->pain = test_tree_pain;
    tree->die = test_tree_die;
    tree_died = false;
    tree_pained = 0;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;
    harvest_start(worker, tree);

    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_ASSERT(!tree_died);
    T_EQ(tree_pained, 1);
    T_FEQ(tree->health.value, 1.0f, 0.01f);
}

/* A resumable route miss has not chosen a heading yet.  Harvest used to call
 * unit_moveindirection anyway, which committed a step along the worker's stale
 * facing while flow_generation=0/direct=false.  Hold the order and position
 * until CM_ProcessPathJobs completes the requested field. */
TEST(wc3_movement, lumber_pending_flow_does_not_move_on_stale_heading) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(-320.0f, 0.0f);
    edict_t *tree = make_harvest_tree(320.0f, 0.0f, 500.0f);
    vec2_t const origin = worker->s.origin2;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 1.5707963f; /* stale north-facing movement is legal */
    tree->collision = 0.0f;

    /* A full-height wall blocks the direct approach so the first Harvest tick
     * must request a resumable collision-sized field.  Do not process the job:
     * this test covers the pending state itself, not route completion. */
    for (int y = 0; y < CELLS; y++)
        pathmap[32 + y * CELLS] = 0x02;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    T_EQ(worker->movement.flow_generation, 0);
    T_ASSERT(!worker->movement.flow_direct);
    T_FEQ(worker->s.origin2.x, origin.x, 0.01f);
    T_FEQ(worker->s.origin2.y, origin.y, 0.01f);
    T_ASSERT(worker->goalentity == tree);
}

/* Same-tree workers keep the same chop target.  A worker directly behind
 * another Peasant may queue for a tick while the front worker advances, but it
 * must not be assigned a persistent angular harvest slot. */
TEST(wc3_movement, lumber_same_tree_workers_preserve_direct_order) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *first = make_moving_unit(-400.0f, 0.0f);
    edict_t *second = add_gold_worker(-365.0f, 0.0f);
    edict_t *tree = make_harvest_tree(0.0f, 0.0f, 500.0f);
    vec2_t const first_origin = first->s.origin2;
    vec2_t const second_origin = second->s.origin2;

    first->collision = second->collision = 16.0f;
    first->unitinfo.MoveSpeed = second->unitinfo.MoveSpeed = 190.0f;
    first->s.model = second->s.model = 1;
    tree->collision = 0.0f;
    gi.LinkEntity(first);
    gi.LinkEntity(second);
    gi.LinkEntity(tree);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 116.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(first, tree);
    harvest_start(second, tree);
    /* If the rear worker is processed first it may queue for the occupied
     * direct step.  Once the front worker advances, the rear worker must resume
     * the same direct route on its next think rather than keeping a side lane. */
    first->currentmove->think(first);
    T_FEQ(first->s.origin2.x, first_origin.x, 0.01f);
    second->currentmove->think(second);
    first->currentmove->think(first);
    T_ASSERT(first->goalentity == tree);
    T_ASSERT(second->goalentity == tree);
    T_ASSERT(first->movement.flow_direct);
    T_ASSERT(second->movement.flow_direct);
    T_ASSERT(first->s.origin2.x > first_origin.x);
    T_ASSERT(first->s.origin2.x < second->s.origin2.x);
    T_ASSERT(fabsf(first->s.origin2.y - first_origin.y) < 2.0f);
    T_ASSERT(second->s.origin2.x > second_origin.x);
    T_ASSERT(fabsf(second->s.origin2.y - second_origin.y) < 2.0f);
}

/* A Peasant already chopping the shared tree is a permanent live-unit blocker
 * for the direct radial approach.  Generic left/right slide selection can make
 * following workers fight over that same line indefinitely.  Harvest uses the
 * resource-worker crowd policy: wait briefly behind same-stream traffic, then
 * take a deterministic bounded pass while preserving live-unit collision. */
TEST(wc3_movement, lumber_same_tree_worker_routes_around_chopper) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *chopper = make_moving_unit(-60.0f, 0.0f);
    edict_t *follower = add_gold_worker(-95.0f, 0.0f);
    edict_t *tree = make_harvest_tree(0.0f, 0.0f, 500.0f);
    float const saved_range = HARVEST_RANGE;
    bool follower_started_chopping = false;

    chopper->collision = follower->collision = 16.0f;
    chopper->unitinfo.MoveSpeed = follower->unitinfo.MoveSpeed = 190.0f;
    chopper->attack1.damagePoint = follower->attack1.damagePoint = 0.01f;
    chopper->s.model = follower->s.model = 1;
    tree->collision = 0.0f;
    gi.LinkEntity(chopper);
    gi.LinkEntity(follower);
    gi.LinkEntity(tree);
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    harvest_start(chopper, tree);
    harvest_start(follower, tree);

    /* The front worker is already inside chop range and therefore remains a
     * stationary live collision circle while its attack animation is active. */
    chopper->currentmove->think(chopper);
    T_STREQ(chopper->currentmove->animation, "attack");

    FOR_LOOP(i, 24) {
        follower->currentmove->think(follower);
        if (follower->currentmove && !strcmp(follower->currentmove->animation, "attack")) {
            follower_started_chopping = true;
            break;
        }
    }

    T_ASSERT(follower_started_chopping);
    T_ASSERT(follower->goalentity == tree);
    T_ASSERT(Vector2_distance(&follower->s.origin2, &tree->s.origin2) <= HARVEST_RANGE);
    T_ASSERT(Vector2_distance(&follower->s.origin2, &chopper->s.origin2) >=
             follower->collision + chopper->collision - 0.5f);
    T_ASSERT(fabsf(follower->s.origin2.y) <= follower->collision * 6.0f + 0.5f);

    HARVEST_RANGE = saved_range;
}

/* A nearby static detour uses the bounded per-mover accelerator immediately;
 * it must not wait for the destination field to cover the whole pathmap. */
TEST(wc3_movement, nearby_move_starts_on_accelerated_waypoint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(320.0f, 0.0f);
    vec2_t const origin = unit->s.origin2;
    vec2_t dest = {-320.0f, 0.0f};

    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    for (int y = 39; y <= 41; y++)
        pathmap[32 + y * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 190.0f;
    unit->s.angle = 0.0f;
    order_move(unit, Waypoint_add(&dest));
    unit->currentmove->think(unit);

    T_EQ(unit->movement.flow_generation, 0);
    T_ASSERT(!unit->movement.flow_direct);
    T_ASSERT(unit->movement.path.valid);
    T_ASSERT(CM_LineIsWalkableForRadius(&origin, &unit->movement.path.waypoint, unit->collision));
    /* Routing is available immediately, but a westward detour from east-facing must turn first. */
    T_ASSERT(unit->movement.turn_blocked);
    T_FEQ(Vector2_distance(&unit->s.origin2, &origin), 0, 0.001f);
    FOR_LOOP(i, 8) {
        unit->currentmove->think(unit);
        if (Vector2_distance(&unit->s.origin2, &origin) > 0.001f) break;
    }
    T_ASSERT(Vector2_distance(&unit->s.origin2, &origin) > 0.001f);
    T_FEQ(unit->s.origin.x, unit->s.origin2.x, 0.001f);
    T_FEQ(unit->s.origin.y, unit->s.origin2.y, 0.001f);
    T_STREQ(unit->currentmove->animation, "walk");
}

/* A turn-lagged facing may still be collision-free while pointing away from
 * the route heading.  Movement must use the resolved heading in that case so
 * a short scripted move cannot step past its marker. */
TEST(wc3_movement, turn_lag_does_not_step_away_from_route_heading) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t const dest = {-32.0f, 64.0f};
    float before, after;

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
    unit->unitinfo.MoveSpeed = 190.0f;
    unit->s.angle = 0.0f;
    order_move(unit, Waypoint_add(&dest));
    before = Vector2_distance(&unit->s.origin2, &dest);
    /* The verified propagation window may stop the first turning ticks.
     * No stopped tick may drift away; the first admitted step must progress. */
    after = before;
    FOR_LOOP(frame, 16) {
        unit->currentmove->think(unit);
        after = Vector2_distance(&unit->s.origin2, &dest);
        T_ASSERT(after <= before);
        if (after < before) break;
    }

    T_ASSERT(after < before);
    T_FEQ(unit->s.origin.x, unit->s.origin2.x, 0.001f);
    T_FEQ(unit->s.origin.y, unit->s.origin2.y, 0.001f);
}

TEST(wc3_movement, turn_lag_facing_must_agree_with_resolved_route_heading) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(320.0f, 0.0f);
    vec2_t dest = {-320.0f, 0.0f};

    FOR_LOOP(y, CELLS) pathmap[32 + y * CELLS] = 0x02;
    for (int y = 39; y <= 41; y++) pathmap[32 + y * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f}, .max = {1024.0f, 1024.0f}));
    unit->unitinfo.MoveSpeed = 190.0f;
    unit->s.angle = 0.0f;
    order_move(unit, Waypoint_add(&dest));
    unit->currentmove->think(unit);

    T_ASSERT(Vector2_dot(&(vec2_t){cosf(unit->s.angle), sinf(unit->s.angle)},
                         &(vec2_t){cosf(unit->movement.heading), sinf(unit->movement.heading)}) < 0.0f);
    T_ASSERT(unit->movement.turn_blocked);
    T_FEQ(unit->s.origin2.x, 320.0f, 0.001f);
    FOR_LOOP(frame, 16) {
        unit->currentmove->think(unit);
        T_ASSERT(unit->s.origin2.x <= 320.0f);
        if (unit->s.origin2.x < 320.0f) break;
    }
    T_ASSERT(unit->s.origin2.x < 320.0f);
    T_FEQ(unit->s.origin.x, unit->s.origin2.x, 0.001f);
}

/* Retail WC3 does not leave a worker orbiting an unreachable tree buried in a
 * forest.  The clicked tree remains authoritative while a route exists; once
 * the collision-sized flow field reaches its closest legal approach point and
 * that point is still outside chop range, Harvest selects a reachable edge
 * tree and begins chopping it. */
TEST(wc3_movement, lumber_unreachable_clicked_tree_retargets_reachable_edge_tree) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, -320.0f);
    edict_t *edge = make_harvest_tree(0.0f, -96.0f, 500.0f);
    edict_t *interior = make_harvest_tree(0.0f, 0.0f, 500.0f);

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->attack1.damagePoint = 0.01f;
    edge->collision = interior->collision = 0.0f;

    /* Seven blocked rows/columns model a dense forest around the clicked
     * interior tree.  With a 16u worker radius the closest legal route goal is
     * outside the forest, still >64u from the interior target but within 64u of
     * the southern edge tree. */
    for (int y = 29; y <= 35; y++) {
        for (int x = 29; x <= 35; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    harvest_start(worker, interior);

    FOR_LOOP(i, 200) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (worker->goalentity == edge &&
            worker->currentmove &&
            !strcmp(worker->currentmove->animation, "attack"))
            break;
    }

    T_ASSERT(worker->goalentity == edge);
    T_ASSERT(worker->secondarygoal == edge);
    T_NOT_NULL(worker->currentmove);
    T_STREQ(worker->currentmove->animation, "attack");
    T_ASSERT(Vector2_distance(&worker->s.origin2, &edge->s.origin2) <= HARVEST_RANGE);
}

TEST(wc3_movement, lumber_tree_dying_during_approach_retargets_immediately) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *dead = make_harvest_tree(400.0f, 0.0f, 100.0f);
    edict_t *live = make_harvest_tree(100.0f, 0.0f, 100.0f);

    HARVEST_RANGE = 64.0f;
    HARVEST_SEARCH_RANGE = 1000.0f;
    harvest_start(worker, dead);
    dead->health.value = 0.0f;
    dead->svflags |= SVF_DEADMONSTER;

    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == live);
    T_ASSERT(worker->secondarygoal == live);
}

/* Ahar slots 1=1 (damage/lumber per swing), 2=10 (capacity): 10 swings are
 * needed per trip. Drives the full cooldown+swing cycle. */
TEST(wc3_movement, lumber_worker_takes_ten_swings_per_trip) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 500.0f);
    worker->attack1.damagePoint = 0.01f;
    tree->pain = test_tree_pain; tree->die = test_tree_die;
    tree_pained = 0; tree_died = false;
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f; HARVEST_COOLDOWN = 0.01f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker); /* ai_walktree → harvest_swing (within range) */
    /* Drive the first chop. */
    worker->wait = 0.01f;
    worker->currentmove->think(worker); /* ai_chop: lumber=1, tree-=1 */
    /* Cycle through cooldown+swing until capacity fills; expect exactly 9 more chops. */
    FOR_LOOP(i, 15) {
        if (worker->harvested_lumber >= HARVEST_LUMBER_CAPACITY) break;
        harvest_cooldown(worker);           /* anim end: <cap → cooldown state */
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* ai_cooldown → harvest_swing */
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* ai_chop */
    }
    T_EQ(tree_pained, 10);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_FEQ(tree->health.value, 490.0f, 0.01f);
    T_ASSERT(!tree_died);
}

/* A custom/non-even capacity must clamp the final successful chop instead of
 * allowing the worker to carry more lumber than the Harvest capacity. */
TEST(wc3_movement, lumber_final_chop_clamps_to_capacity) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 25.0f;
    HARVEST_COOLDOWN = 0.01f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);

    FOR_LOOP(i, 3) {
        worker->wait = 0.01f;
        worker->currentmove->think(worker);
        if (i < 2) {
            harvest_cooldown(worker);
            worker->wait = 0.01f;
            worker->currentmove->think(worker);
        }
    }

    T_EQ(worker->harvested_lumber, 25);
    T_FEQ(tree->health.value, 70.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Harvest only awards carried lumber when the tree can actually take the
 * chop. Invulnerable destructibles reject G_DestructableApplyDamage. */
TEST(wc3_movement, lumber_invulnerable_tree_does_not_award_lumber) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    tree->invulnerable = true;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 10.0f;
    HARVEST_LUMBER_CAPACITY = 25.0f;
    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_FEQ(tree->health.value, 100.0f, 0.01f);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
}

/* A worker has one carried-resource presentation.  Starting to collect lumber
 * after gold must replace the gold bag rather than leaving both carry flags set. */
TEST(wc3_movement, lumber_chop_replaces_gold_carry_state) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->attack1.damagePoint = 0.01f;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_gold, 0);
    T_EQ(worker->harvested_lumber, 1);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Smart-clicking a tree after an interrupted partial lumber trip resumes the
 * same trip and preserves the amount already gathered. */
TEST(wc3_movement, lumber_smart_click_resumes_partial_trip) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->attack1.damagePoint = 0.01f;
    worker->harvested_lumber = 3;
    worker->s.renderfx |= RF_HAS_LUMBER;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", tree));
    T_EQ(worker->harvested_lumber, 3);
    worker->currentmove->think(worker);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_lumber, 4);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* Switching from lumber to gold keeps the lumber carry while travelling and
 * mining.  The first actual gold pickup replaces it atomically. */
TEST(wc3_movement, lumber_smart_click_gold_mine_switches_on_gold_pickup) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->harvested_lumber = 5;
    worker->s.renderfx |= RF_HAS_LUMBER;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    HARVEST_GOLD_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", mine));
    T_EQ(worker->harvested_lumber, 5);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));

    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 10);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Switching from gold to lumber similarly keeps the gold while approaching
 * the tree.  Only a successful chop replaces the carried gold with lumber. */
TEST(wc3_movement, gold_smart_click_tree_switches_on_successful_chop) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    worker->attack1.damagePoint = 0.01f;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    HARVEST_RANGE = 64.0f;
    HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", tree));
    T_EQ(worker->harvested_gold, 7);
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    worker->currentmove->think(worker);
    T_EQ(worker->harvested_gold, 7);
    worker->wait = 0.01f;
    worker->currentmove->think(worker);

    T_EQ(worker->harvested_gold, 0);
    T_EQ(worker->harvested_lumber, 1);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* A worker already carrying gold honors the clicked mine first.  Reaching the
 * mine redirects the existing load to the nearest gold drop-off without
 * entering/mining, then deposit resumes the originally clicked mine. */
TEST(wc3_movement, gold_smart_click_gold_mine_visits_mine_then_returns_and_resumes) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->data.UnitAbilities = &harvest_abilities;
    worker->harvested_gold = 7;
    worker->s.renderfx |= RF_HAS_GOLD;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);

    T_ASSERT(unit_issuetargetorder(worker, "smart", mine));
    T_ASSERT(worker->goalentity == mine);
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(worker->harvested_gold, 7);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold);

    /* Reaching the clicked mine redirects the existing load without entering
     * the mine or collecting any additional gold. */
    worker->currentmove->think(worker);
    T_ASSERT(worker->goalentity == hall);
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(worker->harvested_gold, 7);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(!S_GoldMineWorkerIsInside(worker));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold);

    /* The return completes immediately in this fixture because the hall is at
     * the worker position, then the original clicked mine becomes the goal. */
    worker->currentmove->think(worker);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 7);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(worker->goalentity == mine);
    T_ASSERT(worker->secondarygoal == mine);
    T_STREQ(worker->currentmove->animation, "walk");
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Resumable routing returns generation 0 until its shared flow job completes.
 * The bounded mover-owned accelerator is intentionally best-effort: a longer
 * detour may exceed its immediate work budget. Three miners must then hold their
 * starting positions instead of walking along stale facing, and resume once the
 * shared collision-sized field becomes available. */
TEST(wc3_movement, gold_three_workers_hold_while_shared_route_is_pending) {
    enum { CELLS = 64, WORKERS = 3 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *mine;
    edict_t *workers[WORKERS];
    vec2_t origin[WORKERS];
    slkTestData_t *rows, *old_abilities;

    /* make_moving_unit() resets the shared entity array for isolated tests.
     * This test needs three workers and their mine alive at the same time, so
     * create the common world once and initialize each worker in-place. */
    reset_entities();
    setup_test_world();
    mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -320.0f, 0.0f);
    workers[0] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 320.0f, -64.0f);
    workers[1] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 352.0f,   0.0f);
    workers[2] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 384.0f,  64.0f);

    /* Block the direct westward line but leave a reachable opening north of
     * the workers so the shared mine route requires a resumable flow field. */
    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 40 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    old_abilities = install_goldmine_test_data(&rows);
    mine->collision = 128.0f;
    mine->s.model = 1;
    mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);

    FOR_LOOP(i, WORKERS) {
        workers[i]->movetype = MOVETYPE_STEP;
        workers[i]->stand = unit_stand;
        workers[i]->birth = unit_birth;
        workers[i]->die = unit_die;
        workers[i]->collision = 16.0f;
        workers[i]->health.value = workers[i]->health.max_value = 250.0f;
        workers[i]->unitinfo.MoveSpeed = 190.0f;
        workers[i]->s.angle = 0.0f; /* stale facing points east, away from mine */
        unit_stand(workers[i]);
        origin[i] = workers[i]->s.origin2;
        harvest_gold_start(workers[i], mine);
    }

    FOR_LOOP(i, WORKERS) {
        workers[i]->currentmove->think(workers[i]);
        T_FEQ(Vector2_distance(&workers[i]->s.origin2, &origin[i]), 0.0f, 0.001f);
        T_EQ(workers[i]->movement.flow_generation, 0);
        T_ASSERT(!workers[i]->movement.flow_direct);
        T_ASSERT(!workers[i]->movement.path.valid);
    }

    CM_ProcessPathJobs(65536);
    FOR_LOOP(i, WORKERS) {
        workers[i]->currentmove->think(workers[i]);
        T_ASSERT(workers[i]->movement.flow_generation != 0);
        T_ASSERT(!workers[i]->movement.path.valid);
        T_ASSERT(Vector2_distance(&workers[i]->s.origin2, &origin[i]) > 0.001f);
    }

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A Town Hall is a blocked footprint, not a reachable centre point.  The
 * interaction walker should first take a collision-sized edge lane instead of
 * waiting for a point-flow toward the blocked centre.  This reproduces the
 * Human02 return stall where a Peasant could sit more than 100 units from the
 * footprint until another worker vacated the shared centre-directed lane. */
TEST(wc3_movement, gold_return_prefers_direct_footprint_edge_lane) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    vec2_t const origin = worker->s.origin2;
    float const before = 192.0f;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    /* 320 world units maps to cell 42 in this fixture.  Mirror the 8x8
     * no-walk centre of movement_make_goldmine_pathtex(). */
    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    T_FEQ(CM_DistanceToPathingFootprint(hall, &worker->s.origin2), before, 0.01f);
    worker->currentmove->think(worker);

    T_ASSERT(worker->movement.flow_direct);
    T_ASSERT(worker->s.origin2.x > origin.x);
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &worker->s.origin2) < before);
    gi.MemFree(hall_pathtex);
}

/* Local collision can move a returner away from the edge lane that was nearest
 * on the previous think.  Re-select from the current position: retaining one
 * lane for the whole return leg makes packed Peasants steer back across the
 * Town Hall footprint and oscillate around one another. */
TEST(wc3_movement, gold_return_reselects_footprint_edge_after_displacement) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    vec2_t const displaced = { 640.0f, 160.0f };
    vec2_t expected, expected_dir, actual_dir;
    float step, route_band;

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(hall);

    for (int y = 28; y < 36; y++) {
        for (int x = 38; x < 46; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(harvest_gold_return_to(worker, hall));
    worker->currentmove->think(worker);

    /* Simulate collision avoidance having displaced this worker to the other
     * side of the drop-off without restarting the Harvest order. */
    worker->s.origin2 = displaced;
    gi.LinkEntity(worker);
    step = unit_movedistance(worker);
    route_band = worker->collision + step +
                 CM_PathCellWorldSize() * 1.41421356237f;
    T_ASSERT(CM_FindApproachPointToFootprintForRadius(
        hall, &worker->s.origin2, route_band, worker->collision, &expected));
    T_ASSERT(CM_LineIsWalkableForRadius(
        &worker->s.origin2, &expected, worker->collision));
    expected_dir = Vector2_sub(&expected, &worker->s.origin2);
    Vector2_normalize(&expected_dir);

    worker->currentmove->think(worker);
    actual_dir = MAKE(vec2_t, cosf(worker->movement.heading),
                               sinf(worker->movement.heading));
    T_ASSERT(Vector2_dot(&expected_dir, &actual_dir) > 0.99f);
    gi.MemFree(hall_pathtex);
}

/* Gold return can miss the shared cache independently of mine approach. The
 * bounded mover route is best-effort; when this long detour exceeds that local
 * accelerator, Return Resources must hold rather than use stale facing, then
 * resume from the shared collision-sized field when its job completes. */
TEST(wc3_movement, gold_return_holds_while_shared_route_is_pending) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(320.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 500.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), -320.0f, 0.0f);
    vec2_t origin;

    FOR_LOOP(y, CELLS)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 40 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.angle = 0.0f; /* stale facing points east, away from hall */
    worker->harvested_gold = 10;
    worker->s.renderfx |= RF_HAS_GOLD;
    worker->secondarygoal = mine;
    hall->collision = 64.0f;
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(hall);

    T_ASSERT(harvest_gold_return_to(worker, hall));
    origin = worker->s.origin2;
    worker->currentmove->think(worker);

    T_FEQ(Vector2_distance(&worker->s.origin2, &origin), 0.0f, 0.001f);
    T_EQ(worker->movement.flow_generation, 0);
    T_ASSERT(!worker->movement.flow_direct);
    T_ASSERT(!worker->movement.path.valid);

    CM_ProcessPathJobs(65536);
    worker->currentmove->think(worker);
    T_ASSERT(worker->movement.flow_generation != 0);
    T_ASSERT(!worker->movement.path.valid);
    T_ASSERT(Vector2_distance(&worker->s.origin2, &origin) > 0.001f);
}

/* Right-click is also the cancel gesture for an active targeted command.
 * Leaving Harvest target mode armed lets the next left-click on an idle worker
 * be consumed as the old target click, so the previous worker group stays
 * selected and a following lumber Smart order retasks that entire group. */
TEST(wc3_movement, harvest_target_mode_right_click_cancel_prevents_stale_group_retask) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *miner1, *miner2, *idle, *tree;
    char tree_number[16];
    cstring_t cancel_command[] = { "smartpoint", "256", "256" };
    cstring_t harvest_command[] = { "smart", tree_number };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    miner1 = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    miner2 = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32.0f, 0.0f);
    idle = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 0.0f);
    tree = make_harvest_tree(160.0f, 0.0f, 100.0f);
    miner1->data.UnitAbilities = miner2->data.UnitAbilities = idle->data.UnitAbilities = &harvest_abilities;
    G_SelectEntity(client, miner1);
    G_SelectEntity(client, miner2);
    client->menu.on_entity_selected = harvest_menu_selecttarget;

    G_ClientCommand(clent, 3, cancel_command);

    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);
    T_NULL(miner1->goalentity);
    T_NULL(miner2->goalentity);

    /* Selection UI rebuilds the portrait/info panel, which is outside this
     * movement test fixture. Once target mode is proven cleared, update the
     * selected set directly and verify the next Smart order cannot reach the
     * old miner group through a stale callback. */
    G_DeselectEntity(client, miner1);
    G_DeselectEntity(client, miner2);
    G_SelectEntity(client, idle);
    T_ASSERT(!G_IsEntitySelected(client, miner1));
    T_ASSERT(!G_IsEntitySelected(client, miner2));
    T_ASSERT(G_IsEntitySelected(client, idle));

    snprintf(tree_number, sizeof(tree_number), "%u", (unsigned)tree->s.number);
    G_ClientCommand(clent, 2, harvest_command);
    T_ASSERT(idle->goalentity == tree);
    T_ASSERT(idle->secondarygoal == tree);
    T_NULL(miner1->goalentity);
    T_NULL(miner2->goalentity);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

/* Entity Smart/right-click uses the same cancel contract as ground Smart. */
TEST(wc3_movement, harvest_target_mode_right_click_entity_cancels_without_order) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker, *tree;
    char tree_number[16];
    cstring_t command[] = { "smart", tree_number };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    tree = make_harvest_tree(160.0f, 0.0f, 100.0f);
    worker->data.UnitAbilities = &harvest_abilities;
    G_SelectEntity(client, worker);
    client->menu.on_entity_selected = harvest_menu_selecttarget;
    snprintf(tree_number, sizeof(tree_number), "%u", (unsigned)tree->s.number);

    G_ClientCommand(clent, 2, command);

    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);
    T_NULL(worker->goalentity);
    T_NULL(worker->secondarygoal);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

static edict_t *make_smart_destructable(float x, float y,
                                        DestructableData_t const *data,
                                        TARGTYPE targtype) {
    edict_t *dest = G_Spawn();
    dest->class_id = MAKEFOURCC('L','T','0','5');
    dest->data.DestructableData = data;
    dest->destructable.initialized = true;
    dest->destructable.placement_solid = true;
    dest->health.value = dest->health.max_value = 500.0f;
    dest->targtype = targtype;
    dest->s.origin2 = (vec2_t){ x, y };
    dest->s.origin.x = x;
    dest->s.origin.y = y;
    return dest;
}

TEST(wc3_movement, smart_unit_target_sends_classic_relationship_indicator) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *unit, *target;
    char target_number[16];
    cstring_t command[] = { "smart", target_number };

    setup_test_world();
    memset(&smart_indicator_capture, 0, sizeof(smart_indicator_capture));
    gi.Write = movement_capture_indicator_write;
    gi.unicast = movement_capture_indicator_unicast;
    G_SetClientConnected(clent, true);
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 0.0f);
    unit->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
    unit->s.player = target->s.player = client->ps.number;
    unit->movetype = MOVETYPE_STEP; unit->stand = unit_stand; unit_stand(unit);
    G_SelectEntity(client, unit);
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);

    G_ClientCommand(clent, 2, command);

    T_ASSERT(smart_indicator_capture.count >= 4);
    T_EQ(smart_indicator_capture.type[0], PF_BYTE);
    T_EQ(smart_indicator_capture.value[0], svc_temp_entity);
    T_EQ(smart_indicator_capture.type[1], PF_BYTE);
    T_EQ(smart_indicator_capture.value[1], TE_ENTITY_INDICATOR);
    T_EQ(smart_indicator_capture.type[2], PF_LONG);
    T_EQ(smart_indicator_capture.value[2], (int32_t)target->s.number);
    T_EQ(smart_indicator_capture.type[3], PF_LONG);
    T_EQ((uint32_t)smart_indicator_capture.value[3], 0xff00ff00u);
    T_EQ(smart_indicator_capture.recipient, clent);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, smart_without_accepted_unit_target_sends_no_indicator) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    edict_t *target;
    char target_number[16];
    cstring_t command[] = { "smart", target_number };

    setup_test_world();
    memset(&smart_indicator_capture, 0, sizeof(smart_indicator_capture));
    gi.Write = movement_capture_indicator_write;
    gi.unicast = movement_capture_indicator_unicast;
    G_SetClientConnected(clent, true);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 0.0f);
    target->svflags |= SVF_MONSTER; target->s.player = 1;
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);

    G_ClientCommand(clent, 2, command);

    T_EQ(smart_indicator_capture.count, 0);
    T_NULL(smart_indicator_capture.recipient);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

/* Entity picking wins over the terrain trace on a bridge. Preserve that
 * traced point so rejected bridge Smart attack semantics can still become the
 * same formation-aware ground move as an ordinary SmartPoint click. */
TEST(wc3_movement, smart_walkable_bridge_falls_back_to_clicked_ground_point) {
    static DestructableData_t const bridge_data = {
        .file = "Doodads/Terrain/WoodBridgeLarge45/WoodBridgeLarge45.mdx",
        .walkable = true,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker, *bridge;
    char bridge_number[16];
    cstring_t command[] = { "smart", bridge_number, "192", "64" };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->collision = 0.0f;
    worker->stand = unit_stand;
    unit_stand(worker);
    bridge = make_smart_destructable(256.0f, 64.0f, &bridge_data, TARG_BRIDGE);
    G_SelectEntity(client, worker);
    snprintf(bridge_number, sizeof(bridge_number), "%u", (unsigned)bridge->s.number);

    G_ClientCommand(clent, 4, command);

    T_NOT_NULL(worker->goalentity);
    T_FEQ(worker->goalentity->s.origin2.x, 192.0f, 0.01f);
    T_FEQ(worker->goalentity->s.origin2.y, 64.0f, 0.01f);
    T_ASSERT(worker->goalentity != bridge);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, smart_nonwalkable_destructable_does_not_fall_back_to_move) {
    static DestructableData_t const wall_data = {
        .file = "Doodads/TestWall.mdx",
        .walkable = false,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker, *wall;
    char wall_number[16];
    cstring_t command[] = { "smart", wall_number, "192", "64" };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->stand = unit_stand;
    unit_stand(worker);
    wall = make_smart_destructable(256.0f, 64.0f, &wall_data, TARG_WALL);
    G_SelectEntity(client, worker);
    snprintf(wall_number, sizeof(wall_number), "%u", (unsigned)wall->s.number);

    G_ClientCommand(clent, 4, command);

    T_NULL(worker->goalentity);
    T_EQ(G_UnitQueuedOrderCount(worker), 0);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, smart_attackable_wall_targets_gate) {
    static DestructableData_t const gate_data = {
        .file = "Doodads/TestGate.mdx",
        .walkable = false,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *attacker, *gate;
    char gate_number[16];
    cstring_t command[] = { "smart", gate_number };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    attacker->stand = unit_stand;
    attacker->attack1.type = ATK_NORMAL;
    attacker->attack1.targetsAllowed = WC3_TARGET_FLAG_WALL;
    unit_stand(attacker);
    gate = make_smart_destructable(256.0f, 64.0f, &gate_data, TARG_WALL);
    G_SelectEntity(client, attacker);
    snprintf(gate_number, sizeof(gate_number), "%u", (unsigned)gate->s.number);

    G_ClientCommand(clent, 2, command);

    T_ASSERT(attacker->goalentity == gate);
    T_EQ(G_UnitQueuedOrderCount(attacker), 0);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, shift_smart_walkable_bridge_queues_clicked_ground_point) {
    static DestructableData_t const bridge_data = {
        .file = "Doodads/Terrain/WoodBridgeLarge45/WoodBridgeLarge45.mdx",
        .walkable = true,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker, *bridge;
    vec2_t first = { 64.0f, 0.0f };
    char bridge_number[16];
    cstring_t command[] = { "smart", bridge_number, "192", "64", "queue" };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->collision = 0.0f;
    worker->stand = unit_stand;
    unit_stand(worker);
    bridge = make_smart_destructable(256.0f, 64.0f, &bridge_data, TARG_BRIDGE);
    G_SelectEntity(client, worker);
    T_ASSERT(G_IssueUnitPointOrder(worker, "move", &first, false,
                                   client->ps.number, 0.0f));
    snprintf(bridge_number, sizeof(bridge_number), "%u", (unsigned)bridge->s.number);

    G_ClientCommand(clent, 5, command);

    T_EQ(G_UnitQueuedOrderCount(worker), 1);
    T_EQ(worker->order_queue.entries[worker->order_queue.head].target_type,
         UNIT_ORDER_TARGET_POINT);
    T_STREQ(worker->order_queue.entries[worker->order_queue.head].order, "move");
    T_FEQ(worker->order_queue.entries[worker->order_queue.head].point.x, 192.0f, 0.01f);
    T_FEQ(worker->order_queue.entries[worker->order_queue.head].point.y, 64.0f, 0.01f);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_movement, smart_walkable_debris_keeps_entity_attack_precedence) {
    static DestructableData_t const debris_data = {
        .file = "Doodads/TestDebris.mdx",
        .walkable = true,
    };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *unit, *debris;
    char debris_number[16];
    cstring_t command[] = { "smart", debris_number, "192", "64" };

    setup_test_world();
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    unit->stand = unit_stand;
    unit_stand(unit);
    unit->attack1.type = ATK_NORMAL;
    unit->attack1.targetsAllowed = 256u; /* debris */
    debris = make_smart_destructable(256.0f, 64.0f, &debris_data, TARG_DEBRIS);
    G_SelectEntity(client, unit);
    snprintf(debris_number, sizeof(debris_number), "%u", (unsigned)debris->s.number);

    G_ClientCommand(clent, 4, command);

    T_ASSERT(unit->goalentity == debris);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

/* Reissuing Harvest while already full remembers the requested tree but begins
 * return immediately, so no extra over-capacity chop can occur. */
TEST(wc3_movement, lumber_full_worker_returns_before_new_chop) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(100.0f, 0.0f, 100.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 300.0f, 0.0f);

    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    HARVEST_LUMBER_CAPACITY = 10.0f;

    harvest_start(worker, tree);

    T_ASSERT(worker->secondarygoal == tree);
    T_ASSERT(worker->goalentity == hall);
    T_STREQ(worker->currentmove->animation, "walk");
    T_EQ(worker->harvested_lumber, 10);
}

/* The capacity-filling chop must fell the tree before return starts.  After
 * depositing, the worker must reject that dead tree and select the next one. */
TEST(wc3_movement, lumber_lethal_trip_fells_then_selects_next_tree) {
    reset_entities();
    setup_test_world();
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->movetype = MOVETYPE_STEP; worker->stand = unit_stand; worker->die = unit_die;
    worker->collision = 0.0f; worker->attack1.damagePoint = 0.01f;
    edict_t *tree1 = make_harvest_tree(20.0f, 0.0f, 10.0f);
    tree1->s.model = G_RegisterModel("Doodads\\Terrain\\LordaeronTree\\LordaeronTree0.mdx");
    edict_t *tree2 = make_harvest_tree(30.0f, 0.0f, 500.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f;
    HARVEST_LUMBER_CAPACITY = 10.0f; HARVEST_COOLDOWN = 0.01f; HARVEST_SEARCH_RANGE = 1000.0f;
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_start(worker, tree1);
    worker->currentmove->think(worker); /* enter the first swing */
    FOR_LOOP(i, 10) {
        worker->wait = 0.01f;
        worker->currentmove->think(worker); /* chop */
        harvest_cooldown(worker);           /* cooldown, or return on chop ten */
        if (i < 9) {
            worker->wait = 0.01f;
            worker->currentmove->think(worker); /* start the next swing */
        }
    }
    worker->currentmove->think(worker); /* deposit and select tree2 */
    worker->currentmove->think(worker); /* begin chopping tree2 */
    G_UnsubscribeMessage(trace_message, &trace);

    T_FEQ(tree1->health.value, 0.0f, 0.01f);
    T_ASSERT(tree1->svflags & SVF_DEADMONSTER);
    T_STREQ(tree1->currentmove->animation, "death");
    if (tree1->animation) {
        T_STREQ(tree1->animation->name, "death");
        T_EQ(tree1->s.frame, tree1->animation->interval[0]);
    } else {
        T_EQ(tree1->s.frame, 0);
    }
    T_ASSERT(worker->goalentity == tree2);
    T_ASSERT(worker->secondarygoal == tree2);
    T_EQ(trace.count, 17);
    T_EQ(trace.msg[12].type, GAME_MSG_HARVEST_TREE_FELLED);
    T_EQ(trace.msg[12].target, tree1->s.number);
    T_EQ(trace.msg[13].type, GAME_MSG_HARVEST_RETURN_LUMBER);
    T_EQ(trace.msg[13].target, hall->s.number);
    T_EQ(trace.msg[14].type, GAME_MSG_HARVEST_DEPOSIT_LUMBER);
    T_EQ(trace.msg[15].type, GAME_MSG_HARVEST_RESUME_LUMBER);
    T_EQ(trace.msg[15].target, tree2->s.number);
    T_EQ(trace.msg[16].type, GAME_MSG_HARVEST_START_CHOP);
    T_EQ(trace.msg[16].target, tree2->s.number);
}

/* With no live tree left, depositing lumber ends in stand and emits no false
 * resume transition naming the felled tree. */
TEST(wc3_movement, lumber_deposit_without_live_tree_stops) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 1.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    worker->attack1.damagePoint = 0.01f;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    HARVEST_RANGE = 64.0f; HARVEST_TREE_DAMAGE = 1.0f; HARVEST_LUMBER_CAPACITY = 1.0f;
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_start(worker, tree);
    worker->currentmove->think(worker);
    worker->wait = 0.01f; worker->currentmove->think(worker);
    harvest_cooldown(worker);
    worker->currentmove->think(worker);
    G_UnsubscribeMessage(trace_message, &trace);

    T_ASSERT(worker->goalentity == NULL);
    T_ASSERT(worker->secondarygoal == NULL);
    T_STREQ(worker->currentmove->animation, "stand");
    T_EQ(trace.count, 6);
    T_EQ(trace.msg[4].type, GAME_MSG_HARVEST_RETURN_LUMBER);
    T_EQ(trace.msg[5].type, GAME_MSG_HARVEST_DEPOSIT_LUMBER);
}

/* A manual return may carry lumber without a remembered tree target. */
TEST(wc3_movement, lumber_manual_return_without_tree_stops) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    worker->harvested_lumber = 1;
    worker->s.renderfx |= RF_HAS_LUMBER;
    harvest_walkback(worker);
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == NULL);
    T_ASSERT(worker->secondarygoal == NULL);
    T_EQ(worker->harvested_lumber, 0);
    T_STREQ(worker->currentmove->animation, "stand");
}

/* If the remembered tree dies while the worker is away, replacement-tree
 * selection is centered on that forest rather than the return building. */
TEST(wc3_movement, lumber_dead_previous_tree_searches_near_old_tree) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *old_tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    edict_t *forest_tree = make_harvest_tree(-450.0f, 0.0f, 100.0f);
    edict_t *dropoff_tree = make_harvest_tree(50.0f, 0.0f, 100.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);

    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    old_tree->health.value = 0.0f;
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = old_tree;
    HARVEST_SEARCH_RANGE = 1000.0f;

    harvest_walkback(worker);
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == forest_tree);
    T_ASSERT(worker->secondarygoal == forest_tree);
    T_ASSERT(worker->goalentity != dropoff_tree);
    T_EQ(worker->harvested_lumber, 0);
}

/* Wisps do not use the normal chop/carry/drop-off loop. Awha attaches to a
 * tree, leaves the tree intact, and credits authored DataA lumber each Duration
 * interval directly to the owning player. */
TEST(wc3_movement, wisp_harvest_persists_and_credits_periodic_lumber) {
    slkTestData_t *rows = parse_slk_string(slk_wisp_harvest_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    float const old_range = HARVEST_RANGE;
    edict_t * wisp = make_moving_unit(0.0f, 0.0f);
    edict_t * tree = make_harvest_tree(20.0f, 0.0f, 100.0f);
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    HARVEST_RANGE = 128.0f;
    wisp->s.player = 0;
    gi.LinkEntity(wisp);
    gi.LinkEntity(tree);

    T_ASSERT(unit_issuetargetorder(wisp, "smart", tree));
    T_ASSERT(wisp->goalentity == tree);
    wisp->currentmove->think(wisp); /* attach to tree */
    T_ASSERT(wisp->inuse && !M_IsDead(wisp));
    T_ASSERT(wisp->goalentity == tree);
    T_STREQ(wisp->currentmove->animation, "stand lumber");
    T_FEQ(tree->health.value, 100.0f, 0.001f);

    wisp->wait = FRAMETIME / 2000.0f;
    wisp->currentmove->think(wisp); /* first periodic credit */
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 9);
    T_ASSERT(wisp->inuse && !M_IsDead(wisp));
    T_EQ(wisp->harvested_lumber, 0);
    T_FEQ(tree->health.value, 100.0f, 0.001f);
    T_FEQ(wisp->wait, 1.0f, 0.001f);

    HARVEST_RANGE = old_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_uses_stock_range_and_duration) {
    slkTestData_t *rows = parse_slk_string(slk_wisp_harvest_stock_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    float const old_range = HARVEST_RANGE;
    float const old_search_range = HARVEST_SEARCH_RANGE;
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *first_tree = make_harvest_tree(32.0f, 0.0f, 100.0f);
    edict_t *next_tree = make_harvest_tree(700.0f, 0.0f, 100.0f);

    /* Make the old general Harvest search radius too short to find next_tree. */
    HARVEST_RANGE = 128.0f;
    HARVEST_SEARCH_RANGE = 100.0f;
    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    wisp->s.player = 0;
    T_ASSERT(unit_issuetargetorder(wisp, "smart", first_tree));
    wisp->currentmove->think(wisp);
    T_ASSERT(wisp->goalentity == first_tree);
    T_FEQ(wisp->wait, 8.0f, 0.001f);

    G_SetHealth(first_tree, 0.0f);
    wisp->currentmove->think(wisp);
    T_ASSERT(wisp->goalentity == next_tree);

    HARVEST_RANGE = old_range;
    HARVEST_SEARCH_RANGE = old_search_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_reads_roc_data_columns) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Data11\"\n"
        "C;Y1;X4;K\"Rng1\"\nC;Y1;X5;K\"Dur1\"\n"
        "C;Y2;X1;K\"Awha\"\nC;Y2;X2;K\"Awha\"\nC;Y2;X3;K9\n"
        "C;Y2;X4;K500\nC;Y2;X5;K1\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    float const old_range = HARVEST_RANGE;
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    HARVEST_RANGE = 128.0f;
    wisp->s.player = 0;
    T_ASSERT(unit_issuetargetorder(wisp, "smart", tree));
    wisp->currentmove->think(wisp);
    wisp->wait = FRAMETIME / 2000.0f;
    wisp->currentmove->think(wisp);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 9);

    HARVEST_RANGE = old_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_rejects_missing_ability_data) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"Afoo\"\nC;Y2;X2;K\"Afoo\"\nC;Y2;X3;K7\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    T_ASSERT(!unit_issuetargetorder(wisp, "smart", tree));
    T_ASSERT(!wisp->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_rejects_missing_duration) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Rng1\"\n"
        "C;Y2;X1;K\"Awha\"\nC;Y2;X2;K\"Awha\"\nC;Y2;X3;K9\nC;Y2;X4;K500\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    T_ASSERT(!unit_issuetargetorder(wisp, "smart", tree));
    T_ASSERT(!wisp->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_rejects_missing_range) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Dur1\"\n"
        "C;Y2;X1;K\"Awha\"\nC;Y2;X2;K\"Awha\"\nC;Y2;X3;K9\nC;Y2;X4;K1\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(20.0f, 0.0f, 100.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    T_ASSERT(!unit_issuetargetorder(wisp, "smart", tree));
    T_ASSERT(!wisp->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_movement, wisp_harvest_move_leave_releases_its_tree_effect) {
    slkTestData_t *rows = parse_slk_string(slk_wisp_harvest_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *wisp = make_moving_unit(0.0f, 0.0f);
    edict_t *effect = alloc_test_unit(MAKEFOURCC('e','f','f','t'), 0.0f, 0.0f);

    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    T_ASSERT(unit_issuetargetorder(wisp, "smart", make_harvest_tree(20.0f, 0.0f, 100.0f)));
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityWispHarvest);
    effect->owner = wisp;
    effect->summon_ability = MAKEFOURCC('A','w','h','a');
    effect->s.flags |= EF_NOT_SELECTABLE;
    unit_stand(wisp);
    T_ASSERT(!effect->inuse);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Warsmash reserves an actively harvested tree to one Wisp. If two Wisps were
 * ordered to the same tree, the later arrival should acquire the nearest free
 * live tree instead of stacking on the occupied target. */
TEST(wc3_movement, wisp_harvest_retargets_when_clicked_tree_is_owned) {
    slkTestData_t *rows = parse_slk_string(slk_wisp_harvest_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    float const old_range = HARVEST_RANGE;
    edict_t * first = make_moving_unit(0.0f, 0.0f);
    edict_t * second = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 20.0f);
    edict_t * tree1 = make_harvest_tree(20.0f, 0.0f, 100.0f);
    edict_t * tree2 = make_harvest_tree(80.0f, 0.0f, 100.0f);

    first->data.UnitAbilities = second->data.UnitAbilities = &wisp_harvest_abilities;
    HARVEST_RANGE = 128.0f;
    first->s.player = second->s.player = 0;
    second->movetype = MOVETYPE_STEP;
    second->stand = unit_stand;
    second->collision = 0.0f;
    second->health.value = second->health.max_value = 120.0f;
    gi.LinkEntity(first); gi.LinkEntity(second); gi.LinkEntity(tree1); gi.LinkEntity(tree2);

    wisp_harvest_start(first, tree1);
    first->currentmove->think(first); /* tree1 becomes actively owned */
    T_ASSERT(first->goalentity == tree1);
    T_STREQ(first->currentmove->animation, "stand lumber");

    wisp_harvest_start(second, tree1);
    second->currentmove->think(second); /* collision-free reservation retarget */
    T_ASSERT(second->goalentity == tree2);
    T_ASSERT(second->goalentity != tree1);
    T_ASSERT(second->inuse && !M_IsDead(second));

    HARVEST_RANGE = old_range;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Smart-targeting a compatible drop-off while carrying lumber honors the
 * building the player clicked instead of silently choosing another nearer one. */
TEST(wc3_movement, lumber_smart_click_returns_to_clicked_dropoff) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 100.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 500.0f, 0.0f);

    worker->data.UnitAbilities = &harvest_abilities;
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    T_ASSERT(unit_issuetargetorder(worker, "smart", mill));
    T_ASSERT(worker->goalentity == mill);
    T_EQ(worker->harvested_lumber, 10);
}

/* A large Town Hall footprint can block the next step before the old +5u
 * lumber deposit tolerance is reached. Deposit at contact plus one simulation
 * step so the worker does not get stuck against the building pathing map. */
TEST(wc3_movement, lumber_return_deposits_at_next_step_contact) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 220.0f, 0.0f);
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 190.0f;
    hall->collision = 192.0f; hall->s.model = 1; hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker); gi.LinkEntity(tree); gi.LinkEntity(hall);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;

    harvest_walkback(worker);
    T_ASSERT(M_DistanceToGoal(worker) > worker->collision + hall->collision + 5.0f);
    T_ASSERT(M_DistanceToGoal(worker) <= worker->collision + hall->collision + unit_movedistance(worker));
    worker->s.renderfx |= RF_HAS_GOLD; /* stale opposite carry tag must not survive deposit */
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == tree);
}


/* Returning lumber to a building with authored blocking pathing uses the same
 * generic point-route contract as mine entry.  Routing may approach the blocked
 * center, but the resource behavior owns the contact+step completion boundary. */
/* Lumber return uses the authored no-walk footprint as the physical deposit
 * boundary, matching gold return.  A drop-off can have a scalar collision
 * circle smaller than its pathing texture; in that case the worker must not
 * wait for or route toward the blocked model centre after it has already
 * reached the building footprint. */
TEST(wc3_movement, lumber_return_deposits_at_dropoff_footprint_corner) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(170.0f, 170.0f);
    edict_t *tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 320.0f, 320.0f);
    pathTex_t *mill_pathtex = movement_make_goldmine_pathtex();
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mill->collision = 64.0f; /* deliberately smaller than authored footprint */
    mill->s.model = 1;
    mill->s.player = worker->s.player;
    mill->pathtex = mill_pathtex;
    make_live_dropoff(mill, &return_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(tree);
    gi.LinkEntity(mill);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;
    harvest_walkback(worker);

    T_ASSERT(worker->goalentity == mill);
    T_ASSERT(M_DistanceToGoal(worker) >
             worker->collision + mill->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(mill, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->goalentity == tree);
    gi.MemFree(mill_pathtex);
}

TEST(wc3_movement, lumber_return_reaches_blocked_townhall_footprint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *tree = make_harvest_tree(-400.0f, 0.0f, 100.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 0.0f);
    uint32_t const old_lumber = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    hall->collision = 192.0f;
    hall->s.model = 1;
    hall->movetype = MOVETYPE_NONE;
    hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    gi.LinkEntity(worker);
    gi.LinkEntity(tree);
    gi.LinkEntity(hall);

    /* 12x12 Town Hall footprint centered on world (320,0). */
    for (int y = 26; y < 38; y++) {
        for (int x = 36; x < 48; x++)
            pathmap[x + y * CELLS] = 0x02;
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->secondarygoal = tree;
    harvest_walkback(worker);

    FOR_LOOP(i, 80) {
        worker->currentmove->think(worker);
        CM_ProcessPathJobs(65536);
        if (!worker->harvested_lumber) break;
    }

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], old_lumber + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->goalentity == tree);
}

/* The old training helper checked only dynamic circles and could choose a
 * point inside the producer's baked pathing footprint. This reproduces the
 * Human02 trained-Peasant regression observed while validating resource return. */
TEST(wc3_movement, trained_unit_exit_skips_blocked_producer_footprint) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    enum { FOOT_W = 16, FOOT_H = 16 };
    size_t const pathtex_size = sizeof(pathTex_t) + FOOT_W * FOOT_H * sizeof(color32_t);
    pathTex_t *pathtex;
    edict_t *producer = make_moving_unit(0.0f, 0.0f);
    edict_t *trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    vec2_t exit;
    float angle;

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->collision = 192.0f;
    trained->collision = 16.0f;

    /* 16x16 no-walk cells centered on the producer model a large authored
     * building footprint. WPM bit 1 is the no-walk flag. */
    for (int y = 24; y < 40; y++) {
        for (int x = 24; x < 40; x++) {
            pathmap[x + y * CELLS] = 0x02;
        }
    }
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    pathtex = gi.MemAlloc(pathtex_size);
    T_NOT_NULL(pathtex);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = FOOT_W;
    pathtex->height = FOOT_H;
    FOR_LOOP(i, FOOT_W * FOOT_H) pathtex->map[i].b = 0xff;
    producer->pathtex = pathtex;

    T_ASSERT(SP_FindUnitExitPosition(producer, trained, &exit, &angle));
    T_ASSERT(CM_PointIsPathableForRadius(&exit, trained->collision));
    T_ASSERT(Vector2_distance(&producer->s.origin2, &exit) > 256.0f);

    producer->pathtex = NULL;
    gi.MemFree(pathtex);
}

/* Dynamic unit circles are also part of legal exit placement. The first
 * deterministic candidate is occupied, so the trained unit must pick another. */
TEST(wc3_movement, trained_unit_exit_skips_dynamic_blocker) {
    edict_t *producer = make_moving_unit(0.0f, 0.0f);
    edict_t *trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -64.0f, -64.0f);
    vec2_t exit;
    float angle;

    producer->movetype = MOVETYPE_NONE;
    trained->collision = 16.0f;
    blocker->movetype = MOVETYPE_STEP;
    blocker->collision = 16.0f;
    blocker->s.model = 1;

    T_ASSERT(SP_FindUnitExitPosition(producer, trained, &exit, &angle));
    T_ASSERT(Vector2_distance(&blocker->s.origin2, &exit) >=
             trained->collision + blocker->collision);
}

/* Completing the head of a multi-unit queue must preserve the next link.
 * unit_stand() clears the completed unit's build pointer, which is also the
 * queue link while that unit is waiting behind the producer. */
TEST(wc3_movement, trained_unit_completion_preserves_remaining_queue) {
    edict_t *producer = make_moving_unit(0.0f, 0.0f);
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    UnitBalance_t balance = { .buildTime = 1, .foodUsed = 2, .foodMade = 4 };
    gameClient_t *client = &game.clients[0];

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->s.player = first->s.player = second->s.player = client->ps.number;
    first->stand = second->stand = unit_stand;
    first->data.UnitBalance = second->data.UnitBalance = &balance;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    first->health.max_value = second->health.max_value = 100.0f;
    first->health.value = 100.0f;
    second->health.value = 0.0f;
    first->training = second->training = true;
    first->s.renderfx |= RF_HIDDEN;
    second->s.renderfx |= RF_HIDDEN;
    first->build = second;
    producer->build = first;

    ai_train_build(producer);

    T_ASSERT(producer->build == second);
    T_NULL(first->build);
    T_ASSERT(!first->training);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(second->training);
    T_ASSERT(second->s.renderfx & RF_HIDDEN);
    T_FEQ(second->health.value, 0.0f, 0.01f);
    T_EQ(first->food.used, 2);
    T_EQ(first->food.made, 4);
    T_EQ(second->food.used, 2);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED], 4);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 104);
}

/* A completed unit must remain hidden and queued when no legal exit exists;
 * revealing it on blocked pathing recreates the permanent stuck-unit bug. */
TEST(wc3_movement, trained_unit_waits_when_no_exit_position_exists) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS];
    edict_t *producer = make_moving_unit(0.0f, 0.0f);
    edict_t *trained = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);

    memset(pathmap, 0x02, sizeof(pathmap));
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    producer->class_id = MAKEFOURCC('h','t','o','w');
    producer->movetype = MOVETYPE_NONE;
    producer->build = trained;
    UnitBalance_t balance = { .buildTime = 1 };
    trained->data.UnitBalance = &balance;
    trained->collision = 16.0f;
    trained->health.max_value = 100.0f;
    trained->health.value = 100.0f;
    trained->s.renderfx |= RF_HIDDEN;

    ai_train_build(producer);

    T_ASSERT(producer->build == trained);
    T_ASSERT(trained->s.renderfx & RF_HIDDEN);
    T_FEQ(trained->s.origin2.x, 0.0f, 0.01f);
    T_FEQ(trained->s.origin2.y, 0.0f, 0.01f);
}

/* Lumber return is ability-driven and chooses the nearest compatible
 * same-owner building rather than preferring a Town Hall class. */
TEST(wc3_movement, lumber_return_prefers_nearer_lumber_mill) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    harvest_walkback(worker);

    T_ASSERT(worker->goalentity == mill);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
}

/* Return Resources is unavailable while a structure is under construction.
 * An unfinished War Mill must not become a lumber drop-off merely because its
 * Arlm ability is already present in unit data. */
TEST(wc3_movement, lumber_return_skips_unfinished_lumber_mill) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('o','g','r','e'), 500.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('o','w','a','r'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    mill->construction.active = true;
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    T_ASSERT(!S_CanReturnResourceAt(worker, mill, RETURN_RESOURCE_LUMBER));
    harvest_walkback(worker);

    T_ASSERT(worker->goalentity == hall);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);

    mill->construction.active = false;
    T_ASSERT(S_CanReturnResourceAt(worker, mill, RETURN_RESOURCE_LUMBER));
}

/* The same construction gate applies to gold.  A Town Hall whose Argl data is
 * already loaded must not receive carried gold until construction completes. */
TEST(wc3_movement, gold_return_skips_unfinished_town_hall) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *complete_hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    edict_t *unfinished_hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 100.0f, 0.0f);
    complete_hall->s.player = unfinished_hall->s.player = worker->s.player;
    make_live_dropoff(complete_hall, &return_gold_lumber_abilities);
    make_live_dropoff(unfinished_hall, &return_gold_lumber_abilities);
    unfinished_hall->construction.active = true;
    S_SetCarriedResource(worker, RETURN_RESOURCE_GOLD, 10);

    T_ASSERT(!S_CanReturnResourceAt(worker, unfinished_hall, RETURN_RESOURCE_GOLD));
    T_ASSERT(S_FindNearestResourceDropoff(worker, RETURN_RESOURCE_GOLD) == complete_hall);
    T_ASSERT(harvest_gold_return_to(worker, S_FindNearestResourceDropoff(worker, RETURN_RESOURCE_GOLD)));
    T_ASSERT(worker->goalentity == complete_hall);
    T_EQ(worker->harvested_gold, 10);
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);

    unfinished_hall->construction.active = false;
    T_ASSERT(S_CanReturnResourceAt(worker, unfinished_hall, RETURN_RESOURCE_GOLD));
    T_ASSERT(S_FindNearestResourceDropoff(worker, RETURN_RESOURCE_GOLD) == unfinished_hall);
}

/* If the chosen Lumber Mill dies during the trip, retain the carried lumber
 * and redirect to the nearest remaining compatible return building. */
TEST(wc3_movement, lumber_return_retargets_after_lumber_mill_dies) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->harvested_lumber = 10;
    worker->s.renderfx |= RF_HAS_LUMBER;

    harvest_walkback(worker);
    T_ASSERT(worker->goalentity == mill);
    mill->health.value = 0;
    worker->currentmove->think(worker);

    T_ASSERT(worker->goalentity == hall);
    T_FEQ(worker->harvested_lumber, 10.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_LUMBER);
}

/* The complete gold loop enters, exits carrying gold, deposits it, and resumes mining. */
TEST(wc3_movement, gold_worker_deposits_and_resumes_mining) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 0.0f, 0.0f);
    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1;
    hall->collision = 64.0f; hall->s.model = 1;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_gold_start(worker, mine);

    FOR_LOOP(i, 100) {
        worker->currentmove->think(worker);
        if (game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] > old_gold) break;
    }
    G_UnsubscribeMessage(trace_message, &trace);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(worker->secondarygoal == mine);
    T_EQ(trace.count, 5);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_GOLD);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_ENTER_MINE);
    T_EQ(trace.msg[2].type, GAME_MSG_HARVEST_RETURN_GOLD);
    T_EQ(trace.msg[3].type, GAME_MSG_HARVEST_DEPOSIT_GOLD);
    T_EQ(trace.msg[4].type, GAME_MSG_HARVEST_RESUME_GOLD);
    FOR_LOOP(i, trace.count)
        T_EQ(trace.msg[i].actor, worker->s.number);
    T_EQ(trace.msg[0].target, mine->s.number);
    T_EQ(trace.msg[1].target, mine->s.number);
    T_EQ(trace.msg[2].target, hall->s.number);
    T_EQ(trace.msg[3].target, hall->s.number);
    T_EQ(trace.msg[4].target, mine->s.number);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Gold pickup replaces a prior lumber carry state.  RF_HAS_LUMBER used to
 * survive here, and the renderer checks lumber before gold, so the Peasant
 * continued to display the lumber-carry model while actually carrying gold. */
TEST(wc3_movement, gold_pickup_replaces_lumber_carry_state) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);

    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->harvested_lumber = 5;
    worker->s.renderfx |= RF_HAS_LUMBER;
    worker->goalentity = worker->secondarygoal = mine;

    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 10);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A large Town Hall footprint can block the next step before the old +5u
 * deposit tolerance is reached. The interaction must complete at contact plus
 * one simulation step, just like entering a gold mine. */
TEST(wc3_movement, gold_return_deposits_at_next_step_contact) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 220.0f, 0.0f);
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f; mine->s.model = 1;
    hall->collision = 192.0f; hall->s.model = 1; hall->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = mine; worker->secondarygoal = mine;
    harvestgold_minegold(worker);
    harvestgold_walkback(worker);
    T_ASSERT(M_DistanceToGoal(worker) > worker->collision + hall->collision + 5.0f);
    T_ASSERT(M_DistanceToGoal(worker) <= worker->collision + hall->collision + unit_movedistance(worker));
    worker->s.renderfx |= RF_HAS_LUMBER; /* stale opposite carry tag must not survive deposit */
    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_lumber, 0);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_LUMBER));
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == mine);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Return-to-building range must use the authored footprint, not only the
 * building's scalar collision circle.  At a Town Hall corner the Peasant can
 * be one legal step from the no-walk cells while centre distance is still well
 * outside collision+step; gold must deposit at that footprint edge. */
TEST(wc3_movement, gold_return_deposits_at_townhall_footprint_corner) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *worker = make_moving_unit(170.0f, 170.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 320.0f, 320.0f);
    pathTex_t *hall_pathtex = movement_make_goldmine_pathtex();
    uint32_t const old_gold = game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    mine->collision = 128.0f;
    mine->s.model = 1;
    hall->collision = 64.0f; /* deliberately smaller than its authored footprint */
    hall->s.model = 1;
    hall->s.player = worker->s.player;
    hall->pathtex = hall_pathtex;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker);
    gi.LinkEntity(mine);
    gi.LinkEntity(hall);

    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = worker->secondarygoal = mine;
    harvestgold_minegold(worker);
    harvestgold_walkback(worker);

    T_ASSERT(worker->goalentity == hall);
    T_ASSERT(M_DistanceToGoal(worker) >
             worker->collision + hall->collision + unit_movedistance(worker));
    T_ASSERT(CM_DistanceToPathingFootprint(hall, &worker->s.origin2) <=
             worker->collision + unit_movedistance(worker));

    worker->currentmove->think(worker);

    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], old_gold + 10);
    T_EQ(worker->harvested_gold, 0);
    T_ASSERT(!(worker->s.renderfx & RF_HAS_GOLD));
    T_ASSERT(worker->goalentity == mine);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
    gi.MemFree(hall_pathtex);
}

/* A lumber-only return ability is incompatible with carried gold even when it
 * is closer than a gold+lumber return building. */
TEST(wc3_movement, gold_return_rejects_nearer_lumber_only_dropoff) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -400.0f, 0.0f);
    edict_t *hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 500.0f, 0.0f);
    edict_t *mill = alloc_test_unit(MAKEFOURCC('h','l','u','m'), 100.0f, 0.0f);
    hall->s.player = mill->s.player = worker->s.player;
    make_live_dropoff(hall, &return_gold_lumber_abilities);
    make_live_dropoff(mill, &return_lumber_abilities);
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(worker); gi.LinkEntity(mine); gi.LinkEntity(hall); gi.LinkEntity(mill);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;
    worker->goalentity = worker->secondarygoal = mine;
    harvestgold_minegold(worker); /* registers worker in mine */

    harvestgold_walkback(worker);

    T_ASSERT(worker->goalentity == hall);
    T_FEQ(worker->harvested_gold, 10.0f, 0.01f);
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Unsubscription is part of the callback lifetime contract. */
TEST(wc3_movement, gameplay_message_unsubscribe_stops_delivery) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    G_PublishMessage(worker, GAME_MSG_HARVEST_MOVE_GOLD, worker);
    G_UnsubscribeMessage(trace_message, &trace);
    G_PublishMessage(worker, GAME_MSG_HARVEST_ENTER_MINE, worker);
    T_EQ(trace.count, 1);
}

/* Duplicate subscriptions are idempotent, and exhaustion is explicit. */
TEST(wc3_movement, gameplay_message_subscription_capacity_is_bounded) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    msgTrace_t trace[MAX_MESSAGE_SUBSCRIBERS + 1] = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace[0]));
    T_ASSERT(G_SubscribeMessage(trace_message, &trace[0]));
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS - 1)
        T_ASSERT(G_SubscribeMessage(trace_message, &trace[i + 1]));
    T_ASSERT(!G_SubscribeMessage(trace_message, &trace[MAX_MESSAGE_SUBSCRIBERS]));
    G_PublishMessage(worker, GAME_MSG_HARVEST_MOVE_GOLD, worker);
    FOR_LOOP(i, MAX_MESSAGE_SUBSCRIBERS) {
        T_EQ(trace[i].count, 1);
        G_UnsubscribeMessage(trace_message, &trace[i]);
    }
}

/* -----------------------------------------------------------------------
 * order_move tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, order_move_sets_goalentity) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp   = alloc_test_unit(0, 30.0f, 0.0f); /* reuse edict as waypoint */
    order_move(unit, wp);
    T_ASSERT(unit->goalentity == wp);
}

TEST(wc3_movement, order_move_sets_walk_animation) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp   = alloc_test_unit(0, 30.0f, 0.0f);
    order_move(unit, wp);
    T_NOT_NULL(unit->currentmove);
    T_STREQ(unit->currentmove->animation, "walk");
}

/* -----------------------------------------------------------------------
 * Waypoint_add tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, waypoint_add_sets_origin) {
    vec2_t dest = {128.0f, 256.0f};
    edict_t *wp = Waypoint_add(&dest);
    T_NOT_NULL(wp);
    T_FEQ(wp->s.origin.x, 128.0f, 0.01f);
    T_FEQ(wp->s.origin.y, 256.0f, 0.01f);
}

/* -----------------------------------------------------------------------
 * unit_movedistance tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, unit_movedistance_matches_formula) {
    /* unit_movedistance = 10 * speed / FRAMETIME */
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    float expected = 10.0f * G_UnitBalance(MAKEFOURCC('h','p','e','a'))->speed / (float)FRAMETIME;
    T_FEQ(unit_movedistance(unit), expected, 0.01f);
}

TEST(wc3_movement, unit_movedistance_uses_scripted_move_speed) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;

    float expected = 10.0f * 300.0f / (float)FRAMETIME;
    T_FEQ(unit_movedistance(unit), expected, 0.01f);
}

/* -----------------------------------------------------------------------
 * M_DistanceToGoal tests
 * --------------------------------------------------------------------- */

TEST(wc3_movement, distance_to_goal_along_x_axis) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp   = alloc_test_unit(0, 100.0f, 0.0f);
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 100.0f, 0.01f);
}

TEST(wc3_movement, distance_to_goal_diagonal) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp   = alloc_test_unit(0, 30.0f, 40.0f); /* 3-4-5 right triangle → 50 */
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 50.0f, 0.1f);
}

TEST(wc3_movement, distance_to_goal_zero_when_at_goal) {
    edict_t *unit = make_moving_unit(10.0f, 10.0f);
    edict_t *wp   = alloc_test_unit(0, 10.0f, 10.0f);
    unit->goalentity = wp;
    T_FEQ(M_DistanceToGoal(unit), 0.0f, 0.01f);
}

/* Human01 LT05 is a walkable 32x32 destructable above river terrain; ground snapping must retain its deck Z. */
TEST(wc3_movement, ground_unit_stands_on_walkable_bridge_surface) {
    static DestructableData_t const bridge_data = { .walkable = true };
    struct { uint16_t width, height; color32_t map[4]; } bridge_path = { .width = 2, .height = 2 };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *bridge = G_Spawn();
    float const terrain = CM_GetHeightAtPoint(0.0f, 0.0f);

    bridge->class_id = MAKEFOURCC('L', 'T', '0', '5');
    bridge->data.DestructableData = &bridge_data;
    bridge->destructable.initialized = bridge->destructable.placement_solid = true;
    bridge->pathtex = (pathTex_t *)&bridge_path;
    bridge->s.origin = MAKE(vec3_t, 0.0f, 0.0f, terrain + 64.0f);
    G_RegisterGroundSurface(bridge);
    T_ASSERT(bridge->s.flags & EF_GROUND_SURFACE);
    M_CheckGround(unit);
    T_FEQ(unit->s.origin.z, terrain + 64.0f, 0.01f);
    T_FEQ(unit->s.ground_offset, unit->unitinfo.FlyHeight, 0.01f);

    unit->s.origin.x = CM_PathCellWorldSize() * 2.0f;
    M_CheckGround(unit);
    T_FEQ(unit->s.origin.z, CM_GetHeightAtPoint(unit->s.origin.x, unit->s.origin.y), 0.01f);
}

TEST(wc3_movement, rectangular_bridge_support_bounds_follow_quarter_turns) {
    static DestructableData_t const bridge_data = { .walkable = true };
    struct { uint16_t width, height; color32_t map[15]; } bridge_path = { .width = 5, .height = 3 };

    FOR_LOOP(angle, 4) {
        bool const vertical = !(angle & 1);
        float cell, width;
        edict_t *unit = make_moving_unit(0.0f, 0.0f);
        edict_t *bridge = G_Spawn();

        cell = CM_PathCellWorldSize();
        width = (vertical ? 3.0f : 5.0f) * cell;

        bridge->class_id = MAKEFOURCC('Y', 'T', '2', '0');
        bridge->data.DestructableData = &bridge_data;
        bridge->destructable.initialized = bridge->destructable.placement_solid = true;
        bridge->pathtex = (pathTex_t *)&bridge_path;
        bridge->s.origin = MAKE(vec3_t, 0.0f, 0.0f, 100.0f);
        bridge->targtype = TARG_BRIDGE;
        bridge->s.angle = angle * (float)M_PI / 2.0f;
        G_RegisterGroundSurface(bridge);

        unit->s.origin.x = width * 0.5f - 1.0f;
        M_CheckGround(unit);
        T_FEQ(unit->s.origin.z, 100.0f, 0.01f);
        unit->s.origin.x = width * 0.5f + 1.0f;
        M_CheckGround(unit);
        T_FEQ(unit->s.origin.z, CM_GetHeightAtPoint(unit->s.origin.x, unit->s.origin.y), 0.01f);
    }
}


TEST(wc3_movement, ground_surface_flag_clears_when_unregistered) {
    static DestructableData_t const bridge_data = { .walkable = true };
    edict_t *bridge = G_Spawn();

    bridge->class_id = MAKEFOURCC('L', 'T', '0', '5');
    bridge->data.DestructableData = &bridge_data;
    bridge->destructable.initialized = true;
    bridge->destructable.placement_solid = true;

    G_RegisterGroundSurface(bridge);
    T_ASSERT(bridge->s.flags & EF_GROUND_SURFACE);

    G_UnregisterGroundSurface(bridge);
    T_ASSERT(!(bridge->s.flags & EF_GROUND_SURFACE));
}

static void set_uniform_test_water_height(float height) {
    war3mapVertex_t *vertices = (war3mapVertex_t *)world.map->vertices;
    uint16_t const encoded = (uint16_t)(0x2000 + (height + WATER_HEIGHT_COR) * 4.0f);
    uint32_t const count = world.map->width * world.map->height;
    FOR_LOOP(i, count) vertices[i].waterlevel = encoded;
}

TEST(wc3_movement, fly_height_is_added_to_support_surface) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    float const terrain = CM_GetHeightAtPoint(0.0f, 0.0f);

    unit->unitinfo.FlyHeight = 300.0f;
    M_CheckGround(unit);

    T_FEQ(unit->s.origin.z, terrain + 300.0f, 0.01f);
    T_FEQ(unit->s.ground_offset, 300.0f, 0.01f);
}

TEST(wc3_movement, flyer_uses_water_surface_before_fly_height) {
    static UnitData_t const fly_data = { .moveTypeName = "fly" };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);

    unit->data.UnitData = &fly_data;
    unit->unitinfo.FlyHeight = 300.0f;
    set_uniform_test_water_height(64.0f);
    M_CheckGround(unit);

    T_FEQ(unit->s.origin.z, 364.0f, 0.01f);
}

TEST(wc3_movement, float_unit_uses_water_surface_and_ignores_bridge) {
    static UnitData_t const float_data = { .moveTypeName = "float" };
    static DestructableData_t const bridge_data = { .walkable = true };
    struct { uint16_t width, height; color32_t map[4]; } bridge_path = { .width = 2, .height = 2 };
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *bridge = G_Spawn();

    unit->data.UnitData = &float_data;
    set_uniform_test_water_height(32.0f);
    bridge->data.DestructableData = &bridge_data;
    bridge->destructable.initialized = bridge->destructable.placement_solid = true;
    bridge->pathtex = (pathTex_t *)&bridge_path;
    bridge->s.origin = MAKE(vec3_t, 0.0f, 0.0f, 96.0f);
    G_RegisterGroundSurface(bridge);
    M_CheckGround(unit);

    T_FEQ(unit->s.origin.z, 32.0f, 0.01f);
}

/* WPM water stays unwalkable; only the explicitly passable bridge lane may connect its banks. */
TEST(wc3_movement, water_is_blocked_except_at_authored_bridge_lane) {
    uint8_t pathmap[15] = { 0 };
    vec2_t const from = { 0.5f, 1.5f }, target = { 4.5f, 1.5f };

    pathmap[2] = pathmap[12] = 2;
    setup_test_pathmap(5, 3, pathmap);
    T_ASSERT(CM_LineIsWalkable(&from, &target));
    pathmap[7] = 2;
    setup_test_pathmap(5, 3, pathmap);
    T_ASSERT(!CM_LineIsWalkable(&from, &target));
}

/* -----------------------------------------------------------------------
 * ai_walk / movement frame tests
 *
 * ai_walk is static inside s_move.c; it is accessed via the think
 * function-pointer stored in move_move_walk.  After calling order_move
 * we invoke ent->currentmove->think() to simulate one game frame.
 * ===================================================================== */

TEST(wc3_movement, unit_moves_closer_to_goal_after_one_frame) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    /* Place waypoint within NAVI_THRESHOLD so direct vector math is used
     * and we don't need the heatmap mock to return a meaningful direction. */
    vec2_t dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);
    T_NOT_NULL(unit->currentmove);
    T_NOT_NULL(unit->currentmove->think);

    float dist_before = M_DistanceToGoal(unit);
    unit->currentmove->think(unit);
    float dist_after = M_DistanceToGoal(unit);

    T_ASSERT(dist_after < dist_before);
}

TEST(wc3_movement, unit_reaches_goal_and_transitions_to_stand) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    /* Distance = 40, move_distance ≈ 27.  After two frames the unit
     * should have arrived (40 - 27 = 13 < 27) and called stand(). */
    vec2_t dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Run up to 10 frames — should arrive well within that. */
    for (int i = 0; i < 10; i++) {
        if (!unit->currentmove || !unit->currentmove->think) break;
        if (strcmp(unit->currentmove->animation, "walk") != 0) break;
        unit->currentmove->think(unit);
    }

    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_position_changes_after_move_frame) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    float x0 = unit->s.origin2.x;
    unit->currentmove->think(unit);

    /* Unit must have moved in the X direction. */
    T_ASSERT(unit->s.origin2.x > x0);
}

/* A radius16 mover is retail class1: a 2x2 footprint. Verify its actual covered
 * cells throughout the detour, independently of the router's point predicate. */
TEST(wc3_movement, move_order_detours_with_unit_collision_radius) {
    enum { CELLS = 16 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(80.0f, 240.0f);
    vec2_t dest = {432.0f, 240.0f};

    for (int y = 3; y <= 12; y++)
        pathmap[y * CELLS + 7] = 2;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 80.0f;
    gi.LinkEntity(unit);
    order_move(unit, Waypoint_add(&dest));

    for (int frame = 0; frame < 200 && unit->currentmove->think; frame++) {
        unit->currentmove->think(unit);
        CM_ProcessPathJobs(4096);
        int cx = (int)floorf(unit->s.origin.x / 32), cy = (int)floorf(unit->s.origin.y / 32);
        for (int y = cy - 1; y <= cy; y++) for (int x = cx - 1; x <= cx; x++)
            T_ASSERT(x >= 0 && y >= 0 && x < CELLS && y < CELLS && !pathmap[y * CELLS + x]);
    }

    T_STREQ(unit->currentmove->animation, "stand");
    T_FEQ(Vector2_distance(&unit->s.origin2, &dest), 0.0f, 0.01f);
}

/* A click in another static connected component cannot be reached.  Retail
 * movement still advances as far as collision permits, then settles at the
 * closest boundary instead of freezing at the order origin or walking forever. */
TEST(wc3_movement, unreachable_move_settles_at_closest_boundary) {
    enum { CELLS = 16 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(80.0f, 240.0f);
    vec2_t const start = unit->s.origin2;
    vec2_t dest = {432.0f, 240.0f};

    for (int y = 0; y < CELLS; y++)
        pathmap[y * CELLS + 7] = 2;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {0.0f, 0.0f}, .max = {512.0f, 512.0f}));
    unit->collision = 16.0f;
    unit->unitinfo.MoveSpeed = 80.0f;
    gi.LinkEntity(unit);
    order_move(unit, Waypoint_add(&dest));

    for (int frame = 0; frame < 200 && unit->currentmove->think; frame++) {
        unit->currentmove->think(unit);
        CM_ProcessPathJobs(4096);
        /* This mover covers class1's four cells, including its newly closer
         * reachable boundary. Check the fixture independently of routing. */
        int cx = (int)floorf(unit->s.origin.x / 32), cy = (int)floorf(unit->s.origin.y / 32);
        for (int y = cy - 1; y <= cy; y++) for (int x = cx - 1; x <= cx; x++)
            T_ASSERT(x >= 0 && y >= 0 && x < CELLS && y < CELLS && !pathmap[y * CELLS + x]);
    }

    T_STREQ(unit->currentmove->animation, "stand");
    T_ASSERT(unit->s.origin2.x > start.x);
    T_ASSERT(unit->s.origin2.x < 7.0f * 32.0f);
    T_ASSERT(Vector2_distance(&unit->s.origin2, &dest) < Vector2_distance(&start, &dest));
}

/* Immobile is a single movement/facing contract, not just a command-menu filter. */
TEST(wc3_movement, immobile_unit_neither_moves_nor_rotates) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *wp = alloc_test_unit(0, 100.0f, 100.0f);
    vec2_t const origin = unit->s.origin2;
    float const angle = unit->s.angle;
    unit->aiflags |= AI_IMMOBILE;
    unit->goalentity = wp;

    unit_changeangle(unit);
    unit_moveindirection(unit);

    T_FEQ(unit->s.origin2.x, origin.x, 0.01f);
    T_FEQ(unit->s.origin2.y, origin.y, 0.01f);
    T_FEQ(unit->s.angle, angle, 0.01f);
}

TEST(wc3_movement, immobile_unit_rejects_ground_move_order) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t dest = {100.0f, 0.0f};
    unit->aiflags |= AI_IMMOBILE;

    T_ASSERT(!unit_issueorder(unit, "move", &dest));
    T_NULL(unit->goalentity);
    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_stops_inside_goal_arrival_range) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t dest = {40.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Run frames until the unit stands. */
    for (int i = 0; i < 20; i++) {
        if (!unit->currentmove || !unit->currentmove->think) break;
        if (strcmp(unit->currentmove->animation, "walk") != 0) break;
        unit->currentmove->think(unit);
    }

    /* Retail point Move stops within .49 fine cells without snapping. */
    float dist = M_DistanceToGoal(unit);
    T_ASSERT(dist <= .49f * CM_PathCellWorldSize() + .001f);
    T_EQ(unit->movement.velocity.x, 0);
    T_EQ(unit->movement.velocity.y, 0);
    T_EQ(unit->current_order_id, 0);
}

/* Frozen complete original16a5b0 engine-order-rank-witness: two front-rank
 * members and one rear rank. The old preserved-source-offset heuristic puts
 * all three at the same X and cannot reproduce this row assignment. */
TEST(wc3_movement, group_move_uses_retail_ranked_formation_destinations) {
    reset_entities(); setup_test_world();
    uint8_t cells[128 * 128] = {0};
    CM_SetupTestPathmap(128, 128, cells);
    CM_SetupTestWorldBounds(&(box2_t){ .min = {-2048, -2048}, .max = {2048, 2048} });
    edict_t *clent = alloc_test_unit(0, 0, 0);
    clent->client = &game.clients[0]; clent->client->ps.number = 0;
    edict_t *units[3];
    UnitData_t rows[3];
    uint32_t const targets[3][2] = {{0x44845555u, 0xc27fffffu}, {0x44845555u, 0x427fffffu}, {0x445caaaau, 0x0u}};
    FOR_LOOP(i, 3) {
        units[i] = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -640, (int)i * 200 - 200);
        rows[i] = *units[i]->data.UnitData; rows[i].formationRank = i == 2 ? 1 : 0;
        units[i]->data.UnitData = rows + i;
        units[i]->collision = 16; units[i]->selected = 1; units[i]->svflags |= SVF_MONSTER;
        units[i]->stand = unit_stand; units[i]->movetype = MOVETYPE_STEP;
        unit_stand(units[i]); gi.LinkEntity(units[i]);
    }
    vec2_t const destination = {1000, 0};
    T_ASSERT(move_selectlocation(clent, &destination));
    FOR_LOOP(i, 3) {
        T_NOT_NULL(units[i]->goalentity);
        T_EQ(wc3_float_bits(units[i]->goalentity->s.origin2.x), targets[i][0]);
        T_EQ(wc3_float_bits(units[i]->goalentity->s.origin2.y), targets[i][1]);
        T_ASSERT(units[i]->goalentity->secondarygoal == units[0]->goalentity->secondarygoal);
        T_EQ(units[i]->goalentity->secondarygoal->s.origin2.x, destination.x);
        T_EQ(units[i]->goalentity->secondarygoal->s.origin2.y, destination.y);
    }
    clent->client->menu.order_queued = true;
    T_ASSERT(move_selectlocation(clent, &destination));
    FOR_LOOP(i, 3) {
        T_EQ(units[i]->order_queue.count, 1);
        unitOrder_t const *queued = units[i]->order_queue.entries + units[i]->order_queue.head;
        T_EQ(wc3_float_bits(queued->point.x), targets[i][0]);
        T_EQ(wc3_float_bits(queued->point.y), targets[i][1]);
        T_EQ(wc3_float_bits(units[i]->goalentity->s.origin2.x), targets[i][0]);
    }
    clent->client->menu.order_queued = false;
    FOR_LOOP(frame, 8) {
        level.time += FRAMETIME;
        FOR_LOOP(i, 3) units[i]->currentmove->think(units[i]);
        CM_ProcessPathJobs(4096);
    }
    FOR_LOOP(i, 3) T_ASSERT(units[i]->s.origin2.x > -640);
    cstring_t file = "/tmp/openwarcraft3-ranked-formation-save.bin";
    uint32_t expected[8][3][4];
    T_ASSERT(WriteGame(file));
    FOR_LOOP(frame, 8) {
        level.time += FRAMETIME;
        FOR_LOOP(i, 3) {
            units[i]->currentmove->think(units[i]);
            expected[frame][i][0] = wc3_float_bits(units[i]->s.origin2.x);
            expected[frame][i][1] = wc3_float_bits(units[i]->s.origin2.y);
            expected[frame][i][2] = wc3_float_bits(units[i]->movement.velocity.x);
            expected[frame][i][3] = wc3_float_bits(units[i]->movement.velocity.y);
        }
        CM_ProcessPathJobs(4096);
    }
    T_ASSERT(ReadGame(file));
    FOR_LOOP(frame, 8) {
        level.time += FRAMETIME;
        FOR_LOOP(i, 3) {
            units[i]->currentmove->think(units[i]);
            T_EQ(wc3_float_bits(units[i]->s.origin2.x), expected[frame][i][0]);
            T_EQ(wc3_float_bits(units[i]->s.origin2.y), expected[frame][i][1]);
            T_EQ(wc3_float_bits(units[i]->movement.velocity.x), expected[frame][i][2]);
            T_EQ(wc3_float_bits(units[i]->movement.velocity.y), expected[frame][i][3]);
        }
        CM_ProcessPathJobs(4096);
    }
    FOR_LOOP(i, 3) {
        T_EQ(wc3_float_bits(units[i]->goalentity->s.origin2.x), targets[i][0]);
        T_EQ(wc3_float_bits(units[i]->goalentity->s.origin2.y), targets[i][1]);
    }
    remove(file); reset_entities();
}

TEST(wc3_movement, group_move_assigns_distinct_reserved_destinations) {
    reset_entities();
    edict_t *clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    edict_t *a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);
    edict_t *c = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 20.0f);
    edict_t *units[] = { a, b, c };

    FOR_LOOP(i, 3) {
        units[i]->collision = 16.0f;
        units[i]->selected = 1 << clent->client->ps.number;
        units[i]->stand = unit_stand;
        unit_stand(units[i]);
    }

    vec2_t dest = {100.0f, 100.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    T_NOT_NULL(a->goalentity);
    T_NOT_NULL(b->goalentity);
    T_NOT_NULL(c->goalentity);
    T_NOT_NULL(a->goalentity->secondarygoal);
    T_ASSERT(a->goalentity->secondarygoal == b->goalentity->secondarygoal);
    T_ASSERT(a->goalentity->secondarygoal == c->goalentity->secondarygoal);
    T_ASSERT(Vector2_distance(&a->goalentity->s.origin2, &b->goalentity->s.origin2) >= 32.0f);
    T_ASSERT(Vector2_distance(&a->goalentity->s.origin2, &c->goalentity->s.origin2) >= 32.0f);
    T_ASSERT(Vector2_distance(&b->goalentity->s.origin2, &c->goalentity->s.origin2) >= 32.0f);
}

TEST(wc3_movement, group_move_ignores_selected_buildings) {
    reset_entities();
    edict_t *clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    edict_t *peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);

    building->collision = 64.0f;
    building->aiflags |= AI_IMMOBILE;
    building->selected = 1 << clent->client->ps.number;
    building->stand = unit_stand;
    unit_stand(building);

    peasant->collision = 16.0f;
    peasant->selected = 1 << clent->client->ps.number;
    peasant->stand = unit_stand;
    unit_stand(peasant);

    vec2_t dest = {100.0f, 100.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    T_NULL(building->goalentity);
    T_NOT_NULL(peasant->goalentity);
}

/* A mixed-speed group travels at its slowest member's speed so it stays
 * together instead of stringing out (WC3 group movement). */
TEST(wc3_movement, group_move_travels_at_slowest_member_speed) {
    reset_entities();
    edict_t *clent = alloc_test_unit(0, 0.0f, 0.0f);
    clent->client = &game.clients[0];

    edict_t *fast = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    edict_t *slow = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 20.0f, 0.0f);
    fast->unitinfo.MoveSpeed = 300.0f;
    slow->unitinfo.MoveSpeed = 100.0f;
    edict_t *units[] = { fast, slow };
    FOR_LOOP(i, 2) {
        units[i]->collision = 16.0f;
        units[i]->selected = 1 << clent->client->ps.number;
        units[i]->stand = unit_stand;
        unit_stand(units[i]);
    }

    vec2_t dest = {400.0f, 0.0f};
    T_ASSERT(move_selectlocation(clent, &dest));

    /* Both units adopt the slowest member's speed for the group move... */
    T_FEQ(fast->movement.group_speed, 100.0f, 0.01f);
    T_FEQ(slow->movement.group_speed, 100.0f, 0.01f);
    /* ...so the fast unit's per-frame travel is capped to the slow speed. */
    T_FEQ(unit_movedistance(fast), 10.0f * 100.0f / (float)FRAMETIME, 0.01f);
}

/* A selection's cap follows its active members, not the speed captured at
 * submission. Retail PrepareMembers re-resolves ownership before each commit. */
TEST(wc3_movement, group_move_refreshes_survivor_speed) {
    FOR_LOOP(change, 6) {
        reset_entities(); setup_test_world();
        edict_t *clent = alloc_test_unit(0, 0, 0);
        clent->client = &game.clients[0];
        edict_t *fast = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
        edict_t *slow = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 0);
        edict_t *units[] = { fast, slow };
        FOR_LOOP(i, 2) {
            units[i]->selected = 1 << clent->client->ps.number;
            units[i]->stand = unit_stand;
            unit_stand(units[i]);
        }
        fast->unitinfo.MoveSpeed = 300; slow->unitinfo.MoveSpeed = 100;
        T_ASSERT(move_selectlocation(clent, &(vec2_t){400, 0}));
        T_FEQ(unit_movedistance(fast), 10.0f * 100 / FRAMETIME, 0.001f);
        switch (change) {
        case 0: T_ASSERT(unit_issueimmediateorder(slow, "stop")); break;
        case 1: G_FreeEdict(slow); break;
        case 2: slow->stand(slow); break;
        case 3: T_ASSERT(unit_issueorder(slow, "move", &(vec2_t){800, 0})); break;
        case 4: G_SetHealth(slow, 0); break;
        case 5: slow->unitinfo.MoveSpeed = 200; break;
        }
        T_FEQ(unit_movedistance(fast), 10.0f * (change == 5 ? 200 : 300) / FRAMETIME, 0.001f);
    }
}

TEST(wc3_movement, group_move_identity_survives_counter_wrap_and_unit_reuse) {
    reset_entities(); setup_test_world();
    edict_t *clent = alloc_test_unit(0, 0, 0);
    clent->client = &game.clients[0];
    edict_t *a = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 0);
    edict_t *c = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 128);
    edict_t *d = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 128);
    edict_t *units[] = {a,b,c,d};
    FOR_LOOP(i, 4) { units[i]->stand = unit_stand; unit_stand(units[i]); }
    a->unitinfo.MoveSpeed = c->unitinfo.MoveSpeed = 300;
    b->unitinfo.MoveSpeed = 100; d->unitinfo.MoveSpeed = 200;
    a->selected = b->selected = 1 << clent->client->ps.number;
    level.next_move_group_id = 0;
    T_ASSERT(move_selectlocation(clent, &(vec2_t){400, 0}));
    uint32_t first_group = a->movement.group_id;
    T_ASSERT(first_group != 0 && first_group == b->movement.group_id);
    a->selected = b->selected = 0;
    c->selected = d->selected = 1 << clent->client->ps.number;
    level.next_move_group_id = UINT32_MAX;
    T_ASSERT(move_selectlocation(clent, &(vec2_t){400, 0}));
    T_ASSERT(c->movement.group_id != 0 && c->movement.group_id != first_group);
    T_EQ(c->movement.group_id, d->movement.group_id);
    T_FEQ(unit_movedistance(a), 10.0f * 100 / FRAMETIME, 0.001f);
    T_FEQ(unit_movedistance(c), 10.0f * 200 / FRAMETIME, 0.001f);
    uint32_t saved_time = level.time;
    G_FreeEdict(b); level.time += 1001;
    edict_t *replacement = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 0);
    T_ASSERT(replacement == b);
    replacement->stand = unit_stand; unit_stand(replacement);
    replacement->unitinfo.MoveSpeed = 50;
    T_ASSERT(unit_issueorder(replacement, "move", &(vec2_t){400, 0}));
    T_EQ(replacement->movement.group_id, 0);
    T_FEQ(unit_movedistance(a), 10.0f * 300 / FRAMETIME, 0.001f);
    T_FEQ(unit_movedistance(c), 10.0f * 200 / FRAMETIME, 0.001f);
    level.time = saved_time;
}

/* Retail keeps the surviving slot until an actual replacement order creates
 * a fresh request. Exercise the same ownership boundary through server frames. */
TEST(wc3_movement, group_survivor_reorder_after_member_reuse_reaches_new_goal) {
    FOR_LOOP(victim, 2) {
        edict_t *first = make_moving_unit(0, 0);
        edict_t *second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 0);
        edict_t *clent = alloc_test_unit(0, 0, 0);
        edict_t *units[] = {first, second};
        FOR_LOOP(i, game.max_clients) game.clients[i].connected = false;
        clent->client = &game.clients[0]; clent->client->ps.number = 0;
        FOR_LOOP(i, 2) {
            units[i]->movetype = MOVETYPE_STEP;
            units[i]->svflags |= SVF_MONSTER;
            units[i]->stand = unit_stand; units[i]->die = unit_die;
            units[i]->think = monster_think;
            units[i]->health.value = units[i]->health.max_value = 250;
            units[i]->collision = 8;
            units[i]->selected = 1;
            unit_stand(units[i]);
            gi.LinkEntity(units[i]);
        }
        first->unitinfo.MoveSpeed = 256; second->unitinfo.MoveSpeed = 128;
        T_ASSERT(move_selectlocation(clent, &(vec2_t){400, 0}));
        edict_t *survivor = units[1-victim];
        edict_t *old_goal = survivor->goalentity;
        vec2_t old_slot = old_goal->s.origin2;
        uint32_t old_group = survivor->movement.group_id;
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        level.started = level.scriptsConfigured = level.scriptsStarted = true;
        FOR_LOOP(frame, 3) { level.time += FRAMETIME; globals.RunFrame(); }
        T_ASSERT(first->s.origin2.x != 0 || first->s.origin2.y != 0);
        T_ASSERT(second->s.origin2.x != 64 || second->s.origin2.y != 0);
        T_ASSERT(unit_issueimmediateorder(units[victim], "stop"));
        G_FreeEdict(units[victim]); level.time += 1001;
        edict_t *reused = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 128);
        T_ASSERT(reused == units[victim]);
        T_EQ(reused->movement.group_id, 0);
        T_ASSERT(survivor->goalentity == old_goal);
        T_FEQ(old_goal->s.origin2.x, old_slot.x, 0);
        T_FEQ(old_goal->s.origin2.y, old_slot.y, 0);
        T_EQ(survivor->movement.group_id, old_group);
        vec2_t new_goal = {384, 448};
        T_ASSERT(unit_issueorder(survivor, "move", &new_goal));
        T_EQ(survivor->movement.group_id, 0);
        T_FEQ(survivor->goalentity->s.origin2.x, new_goal.x, 0);
        T_FEQ(survivor->goalentity->s.origin2.y, new_goal.y, 0);
        for (int frame = 0; frame < 240 && move_is_active_order_walk(survivor); frame++) {
            level.time += FRAMETIME; globals.RunFrame();
        }
        T_ASSERT(!move_is_active_order_walk(survivor));
        /* Move's idle transition retains the last goal as cached state, as
         * retail Stop retains its original goal. Activity is owned by Move. */
        T_NOT_NULL(survivor->goalentity);
        T_FEQ(survivor->goalentity->s.origin2.x, new_goal.x, 0);
        T_FEQ(survivor->goalentity->s.origin2.y, new_goal.y, 0);
        T_ASSERT(Vector2_distance(&survivor->s.origin2, &new_goal) <= survivor->collision + 16);
        T_EQ(survivor->order_queue.count, 0);
        level.started = false;
    }
}

/* A lone unit keeps its own speed (no group cap). */
TEST(wc3_movement, single_unit_move_keeps_own_speed) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    unit->unitinfo.MoveSpeed = 300.0f;
    vec2_t dest = {200.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    T_FEQ(unit->movement.group_speed, 0.0f, 0.01f);
    T_FEQ(unit_movedistance(unit), 10.0f * 300.0f / (float)FRAMETIME, 0.01f);
}

TEST(wc3_movement, plain_move_uses_collision_sized_static_route) {
    enum { CELLS = 64 };
    uint8_t pathmap[CELLS * CELLS] = {0};
    edict_t *unit = make_moving_unit(-320.0f, 0.0f);
    vec2_t dest = {320.0f, 0.0f};

    unit->collision = 16.0f; /* one 32u path-cell radius in this fixture */
    unit->unitinfo.MoveSpeed = 190.0f;

    /* A one-cell opening is traversable by a point route but not by this
     * mover's 3x3 collision footprint.  Plain move must use the latter. */
    for (int y = 0; y < CELLS; y++)
        pathmap[32 + y * CELLS] = 0x02;
    pathmap[32 + 32 * CELLS] = 0;
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t,
        .min = {-1024.0f, -1024.0f},
        .max = { 1024.0f,  1024.0f}));

    T_ASSERT(unit_issueorder(unit, "move", &dest));
    unit->currentmove->think(unit); /* queues the resumable radius field */
    CM_ProcessPathJobs(65536);
    unit->currentmove->think(unit); /* completed field retargets the private waypoint */

    T_STREQ(unit->currentmove->animation, "walk");
    T_ASSERT(!unit->movement.flow_unreachable);
    T_ASSERT(unit->goalentity->s.origin2.x > unit->s.origin2.x);
    T_ASSERT(unit->goalentity->s.origin2.x < 0.0f);
}

TEST(wc3_movement, blocked_move_keeps_order_alive_away_from_goal) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t origin = unit->s.origin2;
    vec2_t dest = {400.0f, 0.0f};
    unit_issueorder(unit, "move", &dest);

    /* Budget exceeds MOVE_BLOCKED_FRAMES.  A distant plain move must remain
     * active: another unit may be the temporary blocker and retail keeps the
     * right-click order alive until the route can make progress. */
    for (int i = 0; i < 30; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        unit->s.origin2 = origin;
        unit->s.origin.x = origin.x;
        unit->s.origin.y = origin.y;
        unit->bounds.min.x = unit->s.origin2.x - unit->collision;
    unit->bounds.min.y = unit->s.origin2.y - unit->collision;
    unit->bounds.max.x = unit->s.origin2.x + unit->collision;
    unit->bounds.max.y = unit->s.origin2.y + unit->collision;
    }

    T_STREQ(unit->currentmove->animation, "walk");
}

TEST(wc3_movement, near_goal_jitter_settles_to_stand) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    vec2_t dest = {100.0f, 0.0f};
    /* Keep the fixture inside the settle band but beyond arrival tolerance for
     * both ROC and TFT, whose archive-backed Peasant move speeds differ. */
    unit->s.origin2.x = dest.x - unit_movedistance(unit) - 6.0f;
    unit->s.origin.x = unit->s.origin2.x;
    gi.LinkEntity(unit);
    vec2_t jitter = unit->s.origin2;
    unit_issueorder(unit, "move", &dest);

    for (int i = 0; i < 10; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        unit->s.origin2 = jitter;
        unit->s.origin.x = jitter.x;
        unit->s.origin.y = jitter.y;
        unit->bounds.min.x = unit->s.origin2.x - unit->collision;
    unit->bounds.min.y = unit->s.origin2.y - unit->collision;
    unit->bounds.max.x = unit->s.origin2.x + unit->collision;
    unit->bounds.max.y = unit->s.origin2.y + unit->collision;
    }

    T_STREQ(unit->currentmove->animation, "stand");
}

TEST(wc3_movement, unit_stops_when_goal_is_occupied) {
    edict_t *unit = make_moving_unit(0.0f, 0.0f);
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100.0f, 0.0f);
    vec2_t dest = {100.0f, 0.0f};

    unit->collision = 16.0f;
    blocker->collision = 16.0f;
    blocker->s.model = 1;            /* non-hollow so it is a collision obstacle */
    blocker->stand = unit_stand;
    blocker->movetype = MOVETYPE_NONE;
    unit_stand(blocker);
    /* Collision is assigned after allocation, so link both fixtures with their final radii. */
    gi.LinkEntity(unit);
    gi.LinkEntity(blocker);

    unit_issueorder(unit, "move", &dest);

    /* Move-time collision blocks the unit short of the occupied goal (it never
     * steps into the blocker), then the blocked-frame accumulator settles it to
     * stand.  No post-move solver is involved any more.  Track the closest the
     * unit ever comes to the goal: it should reach right up against the blocker
     * (just outside the combined collision radius) but never inside it. */
    float min_goal_dist = M_DistanceToGoal(unit);
    for (int i = 0; i < 40; i++) {
        if (!unit->currentmove || strcmp(unit->currentmove->animation, "walk") != 0) {
            break;
        }
        unit->currentmove->think(unit);
        float d = M_DistanceToGoal(unit);
        if (d < min_goal_dist) min_goal_dist = d;
    }

    float combined = unit->collision + blocker->collision;
    T_STREQ(unit->currentmove->animation, "stand");/* settled, didn't walk forever */
    T_ASSERT(min_goal_dist >= combined - 1.0f);                    /* never penetrated the blocker */
    T_ASSERT(min_goal_dist <= combined + unit_movedistance(unit)); /* but reached right up to it */
}

/* Without a town hall the worker exits the mine carrying gold but has nowhere
 * to go: it stops in stand state.  The RETURN_GOLD, DEPOSIT_GOLD, and
 * RESUME_GOLD messages must NOT be published. */
TEST(wc3_movement, gold_worker_stops_when_no_townhall) {
    edict_t *worker = make_moving_unit(0.0f, 0.0f);
    edict_t *mine   = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker->collision = 16.0f; worker->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1; mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;

    msgTrace_t trace = {0};
    T_ASSERT(G_SubscribeMessage(trace_message, &trace));
    harvest_gold_start(worker, mine);

    /* Drive until harvested_gold is set (harvestgold_walkback fired). */
    FOR_LOOP(i, 100) {
        worker->currentmove->think(worker);
        if (worker->harvested_gold > 0) break;
    }
    G_UnsubscribeMessage(trace_message, &trace);

    T_ASSERT(worker->harvested_gold > 0);           /* gold carried, not deposited */
    T_ASSERT(worker->s.renderfx & RF_HAS_GOLD);     /* visual bag still on worker */
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));    /* not inside mine */
    T_STREQ(worker->currentmove->animation, "stand");
    /* Only MOVE_GOLD and ENTER_MINE — no return/deposit/resume. */
    T_EQ((int)trace.count, 2);
    T_EQ(trace.msg[0].type, GAME_MSG_HARVEST_MOVE_GOLD);
    T_EQ(trace.msg[1].type, GAME_MSG_HARVEST_ENTER_MINE);
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* A second worker ordered to mine when the mine is already at capacity waits
 * outside.  When the first worker exits, it wakes the second, which enters
 * immediately without a new walk order from the player. */
TEST(wc3_movement, gold_mine_queues_second_worker_when_at_capacity) {
    edict_t *worker1 = make_moving_unit(0.0f, 0.0f);
    edict_t *mine    = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 400.0f, 0.0f);
    worker1->collision = 16.0f; worker1->unitinfo.MoveSpeed = 100.0f;
    mine->collision = 128.0f; mine->s.model = 1; mine->movetype = MOVETYPE_NONE;
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    gi.LinkEntity(mine);
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    HARVEST_GOLD_CAPACITY = 10.0f;

    /* Worker1 walks to and enters the mine. */
    harvest_gold_start(worker1, mine);
    FOR_LOOP(i, 60) {
        worker1->currentmove->think(worker1);
        if (worker1->s.renderfx & RF_HIDDEN) break;
    }
    T_ASSERT(worker1->s.renderfx & RF_HIDDEN);
    T_EQ((int)mine->peonsinside, 1);

    /* Worker2: wire and place at the mine entrance so it reaches immediately. */
    edict_t *worker2 = alloc_test_unit(MAKEFOURCC('h','p','e','a'),
                                      mine->s.origin2.x - mine->collision - 16.0f,
                                      mine->s.origin2.y);
    worker2->movetype = MOVETYPE_STEP;
    worker2->stand    = unit_stand;
    worker2->die      = unit_die;
    worker2->collision = 16.0f;
    worker2->health.value = worker2->health.max_value = 250.0f;
    worker2->unitinfo.MoveSpeed = 100.0f;
    unit_stand(worker2);
    gi.LinkEntity(worker2);

    harvest_gold_start(worker2, mine);
    worker2->currentmove->think(worker2); /* immediately at mine — enters wait state */

    T_ASSERT(!(worker2->s.renderfx & RF_HIDDEN));   /* waiting outside */
    T_EQ((int)mine->peonsinside, 1);                /* still only worker1 */
    T_STREQ(worker2->currentmove->animation, "stand");

    /* Worker1 exits; harvestgold_walkback wakes worker2 in the same call. */
    worker1->currentmove->think(worker1);
    T_ASSERT(worker2->s.renderfx & RF_HIDDEN);   /* worker2 now inside */
    T_EQ((int)mine->peonsinside, 1);             /* worker1 left (−1) worker2 entered (+1) */
    T_ASSERT(!(worker1->s.renderfx & RF_HIDDEN));/* worker1 exited */
    T_ASSERT(worker1->s.renderfx & RF_HAS_GOLD); /* worker1 carrying gold */
    T_EQ(mine->resources, 90);
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Stock Agld has one internal mining slot. Six assigned workers may all keep
 * Harvest orders, but only one may ever be registered/hidden inside. */
TEST(wc3_movement, gold_mine_stock_capacity_never_exceeds_one_with_six_workers) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    setup_test_goldmine(mine, &test_goldmine_stock, 12500);
    HARVEST_GOLD_CAPACITY = 10.0f;

    T_EQ(S_GoldMineCapacity(mine), 1);
    FOR_LOOP(i, 6) {
        edict_t *worker = add_gold_worker(150.0f + (float)i, 0.0f);
        worker->goalentity = worker->secondarygoal = mine;
        harvestgold_minegold(worker);
        T_ASSERT(mine->peonsinside <= 1);
        if (i == 0) {
            T_ASSERT(S_GoldMineWorkerIsInside(worker));
            T_ASSERT(worker->s.renderfx & RF_HIDDEN);
        } else {
            T_ASSERT(!S_GoldMineWorkerIsInside(worker));
            T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
            T_STREQ(worker->currentmove->animation, "stand");
        }
    }
    T_EQ(mine->peonsinside, 1);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Capacity, duration, and initial gold come from the specific Agld-derived
 * ability on each mine rather than process-wide globals. */
TEST(wc3_movement, gold_mines_keep_independent_custom_capacity_duration_and_gold) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    edict_t *mine1 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    edict_t *mine2 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 500.0f, 0.0f);
    setup_test_goldmine(mine1, &test_goldmine_cap1, 0);
    setup_test_goldmine(mine2, &test_goldmine_cap2, 0);
    S_GoldMineInitUnit(mine1);
    S_GoldMineInitUnit(mine2);

    T_EQ(mine1->resources, 100);
    T_EQ(mine2->resources, 200);
    T_EQ(S_GoldMineCapacity(mine1), 1);
    T_EQ(S_GoldMineCapacity(mine2), 2);
    T_FEQ(S_GoldMineMiningDuration(mine1), 0.01f, 0.001f);
    T_FEQ(S_GoldMineMiningDuration(mine2), 2.0f, 0.001f);

    edict_t *a = add_gold_worker(0.0f, 0.0f);
    edict_t *b = add_gold_worker(0.0f, 0.0f);
    edict_t *c = add_gold_worker(500.0f, 0.0f);
    edict_t *d = add_gold_worker(500.0f, 0.0f);
    edict_t *e = add_gold_worker(500.0f, 0.0f);
    a->goalentity = b->goalentity = mine1;
    c->goalentity = d->goalentity = e->goalentity = mine2;
    harvestgold_minegold(a);
    harvestgold_minegold(b);
    harvestgold_minegold(c);
    harvestgold_minegold(d);
    harvestgold_minegold(e);

    T_EQ(mine1->peonsinside, 1);
    T_EQ(mine2->peonsinside, 2);
    T_FEQ(c->wait, 2.0f, 0.001f);
    T_ASSERT(!S_GoldMineWorkerIsInside(b));
    T_ASSERT(!S_GoldMineWorkerIsInside(e));

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Inside membership is authoritative: duplicate entry cannot increment the
 * mine twice, ordinary orders are rejected, and exit restores protection and
 * unregisters exactly once. */
TEST(wc3_movement, gold_miner_inside_is_non_orderable_and_unregisters_once) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    edict_t *worker = add_gold_worker(0.0f, 0.0f);
    vec2_t point = { 100.0f, 100.0f };
    setup_test_goldmine(mine, &test_goldmine_cap1, 100);
    worker->goalentity = worker->secondarygoal = mine;
    HARVEST_GOLD_CAPACITY = 10.0f;

    harvestgold_minegold(worker);
    T_EQ(mine->peonsinside, 1);
    T_ASSERT(strstr(mine->animation_props, "work") != NULL);
    T_ASSERT(worker->invulnerable);
    T_ASSERT(S_GoldMineWorkerIsInside(worker));
    harvestgold_minegold(worker);
    T_EQ(mine->peonsinside, 1);
    T_ASSERT(!unit_issueimmediateorder(worker, "stop"));
    T_ASSERT(!unit_issueorder(worker, "move", &point));
    T_ASSERT(!unit_issuetargetorder(worker, "attack", mine));
    T_EQ(mine->peonsinside, 1);

    harvestgold_walkback(worker);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(strstr(mine->animation_props, "work") == NULL);
    T_ASSERT(!S_GoldMineWorkerIsInside(worker));
    T_ASSERT(!worker->invulnerable);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_EQ(worker->harvested_gold, 10);
    harvestgold_walkback(worker); /* cannot unregister/decrement twice */
    T_EQ(mine->peonsinside, 0);

    edict_t *removed = add_gold_worker(0.0f, 0.0f);
    removed->goalentity = removed->secondarygoal = mine;
    harvestgold_minegold(removed);
    T_EQ(mine->peonsinside, 1);
    G_FreeEdict(removed);
    T_EQ(mine->peonsinside, 0);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* The final trip is clamped to remaining mine gold. Draining the mine to zero
 * depletes it and prevents an already-waiting worker from entering. */
TEST(wc3_movement, gold_mine_partial_final_trip_depletes_and_rejects_waiter) {
    reset_entities();
    setup_test_world();
    slkTestData_t *rows, *old_abilities = install_goldmine_test_data(&rows);
    edict_t *mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    edict_t *miner = add_gold_worker(0.0f, 0.0f);
    edict_t *waiter = add_gold_worker(0.0f, 0.0f);
    setup_test_goldmine(mine, &test_goldmine_cap1, 6);
    HARVEST_GOLD_CAPACITY = 10.0f;
    miner->goalentity = miner->secondarygoal = mine;
    waiter->goalentity = waiter->secondarygoal = mine;

    harvestgold_minegold(miner);
    harvestgold_minegold(waiter);
    T_EQ(mine->peonsinside, 1);
    T_STREQ(waiter->currentmove->animation, "stand");

    harvestgold_walkback(miner);
    T_EQ(miner->harvested_gold, 6);
    T_EQ(mine->resources, 0);
    T_EQ(mine->peonsinside, 0);
    T_ASSERT(M_IsDead(mine));
    T_ASSERT(!S_GoldMineWorkerIsInside(waiter));
    T_ASSERT(!(waiter->s.renderfx & RF_HIDDEN));
    T_STREQ(waiter->currentmove->animation, "stand");

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Haunted mining keeps the underlying Agld unit as the sole resource pool.
 * Acolytes take deterministic external ring slots and the mine grants income
 * directly according to Warsmash's integer worker-count interval scaling. */
TEST(wc3_movement, haunted_mine_uses_acolyte_ring_slots_and_parent_gold) {
    slkTestData_t *rows, *old_abilities;
    gameClient_t *client;
    edict_t *parent, *haunted, *first, *second;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    client = &game.clients[0];
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    haunted = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    setup_test_goldmine(parent, &test_goldmine_stock, 100);
    haunted->data.UnitAbilities = &test_haunted_mine;
    haunted->health.value = haunted->health.max_value = 1000.0f;
    first->data.UnitAbilities = second->data.UnitAbilities = &test_acolyte_harvest;
    haunted->s.player = first->s.player = second->s.player = client->ps.number;
    first->stand = second->stand = unit_stand;
    first->collision = second->collision = 16.0f;
    first->unitinfo.MoveSpeed = second->unitinfo.MoveSpeed = 100.0f;
    unit_stand(first); unit_stand(second);

    T_ASSERT(S_MineOverlayBind(haunted, parent));
    T_ASSERT(parent->s.renderfx & RF_HIDDEN);
    T_ASSERT(parent->paused);
    T_ASSERT(S_AcolyteHarvestOrder(first, haunted));
    T_ASSERT(S_AcolyteHarvestOrder(second, haunted));
    first->currentmove->think(first);
    second->currentmove->think(second);
    T_ASSERT(S_AcolyteHarvestIsActive(first));
    T_ASSERT(S_AcolyteHarvestIsActive(second));
    T_ASSERT(first->acolyte_mine.slot != second->acolyte_mine.slot);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(second->s.renderfx & RF_HIDDEN));
    T_STREQ(first->currentmove->animation, "stand work");

    /* Five authored slots with two active Acolytes use integer multiplier 2. */
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    level.time = 1999;
    blight_mine_think(haunted);
    T_EQ(count_haunted_ring_effects(haunted), 5);
    FOR_LOOP(slot, 5) {
        edict_t *effect = haunted_ring_effect_slot(haunted, slot);
        float const angle = (float)(M_PI / 2.0 + (M_PI * 2.0 / 5.0) * slot);
        T_NOT_NULL(effect);
        T_FEQ(effect->s.angle, angle, 0.001f);
    }
    T_EQ(parent->resources, 100);
    level.time = 2000;
    blight_mine_think(haunted);
    T_EQ(parent->resources, 90);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 10);

    unit_stand(first);
    T_ASSERT(!S_AcolyteHarvestIsActive(first));
    T_ASSERT(S_AcolyteHarvestIsActive(second));
    /* Actual destruction releases the overlay relationship before the death
     * animation; the original mine must immediately become usable again. The
     * parent is restored by its bound identity, not by reclassifying abilities
     * during teardown. */
    parent->data.UnitAbilities = NULL;
    unit_die(haunted, NULL);
    T_ASSERT(M_IsDead(haunted));
    T_ASSERT(!S_AcolyteHarvestIsActive(second));
    T_ASSERT(!(parent->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent->paused);
    T_EQ(parent->resources, 90);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Map-loaded overlays must bind to the neutral mine at the same authored location. */
TEST(wc3_movement, preplaced_haunted_mine_binds_to_neutral_parent) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *haunted;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 128.0f, 128.0f);
    haunted = alloc_test_unit(MAKEFOURCC('u','g','o','l'), 128.0f, 128.0f);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    haunted->s.player = 0;
    setup_test_goldmine(parent, &test_goldmine_stock, 4500);
    haunted->data.UnitAbilities = &test_haunted_mine;
    haunted->health.value = haunted->health.max_value = 1000.0f;

    S_MineOverlayBindPreplaced();
    T_EQ(haunted->mineoverlay.parent, parent);
    T_ASSERT(parent->s.renderfx & RF_HIDDEN);
    T_ASSERT(parent->paused);
    T_EQ(parent->resources, 4500);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Script-created Haunted Mines must return a live bound overlay and preserve parent gold. */
TEST(wc3_movement, scripted_haunted_mine_creation_binds_parent) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *haunted;
    vec2_t point = { 256.0f, 256.0f };

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), point.x, point.y);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 3200);

    haunted = S_CreateBlightedGoldmine(0, &point, 90.0f);
    T_NOT_NULL(haunted);
    T_EQ(haunted->mineoverlay.parent, parent);
    T_FEQ(haunted->s.angle, 90.0f, 0.001f);
    T_EQ(parent->resources, 3200);
    T_ASSERT(parent->s.renderfx & RF_HIDDEN);
    T_ASSERT(parent->paused);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Restoration spawns must initialize gameplay data without replaying Birth presentation. */
TEST(wc3_movement, no_birth_spawn_skips_birth_callback) {
    edict_t *unit;

    reset_entities();
    setup_test_world();
    unit = SP_SpawnAtLocationNoBirth(MAKEFOURCC('u','g','o','l'), 0, &MAKE(vec2_t, 0, 0));
    T_NOT_NULL(unit);
    T_ASSERT(unit->birth != NULL);
    T_ASSERT(unit->currentmove == NULL || strcmp(unit->currentmove->animation, "birth"));
}

/* Entangled gold income reuses generic cargo occupancy. The periodic slot
 * cursor advances before testing occupancy, skips empty slots, and depletion
 * kills the overlay, unloads Wisps, and restores the original mine. */
TEST(wc3_movement, entangled_mine_round_robin_income_depletes_parent_and_unloads_wisps) {
    slkTestData_t *rows, *old_abilities;
    gameClient_t *client;
    edict_t *parent, *mine, *first, *second;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    client = &game.clients[0];
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    first = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    second = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    setup_test_goldmine(parent, &test_goldmine_stock, 25);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->think = monster_think;
    mine->health.value = mine->health.max_value = 1000.0f;
    mine->s.player = first->s.player = second->s.player = client->ps.number;
    first->stand = second->stand = unit_stand;
    first->s.renderfx |= RF_HIDDEN; second->s.renderfx |= RF_HIDDEN;
    first->paused = second->paused = true;
    mine->cargo.units[0] = first; mine->cargo.units[1] = second; mine->cargo.count = 2;

    T_ASSERT(S_MineOverlayBind(mine, parent));
    S_CargoInitUnit(mine);
    T_ASSERT(strstr(mine->animation_props, "second") != NULL);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;

    level.time = 0;
    G_RunEntity(mine); /* index 1: occupied */
    T_EQ(parent->resources, 15);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 10);
    T_EQ(mine->mineoverlay.active_interval_index, 1);
    level.time = 1000; G_RunEntity(mine); /* index 2: empty */
    level.time = 2000; G_RunEntity(mine); /* index 3: empty */
    level.time = 3000; G_RunEntity(mine); /* index 4: empty */
    T_EQ(parent->resources, 15);
    level.time = 4000; G_RunEntity(mine); /* index 0: occupied */
    T_EQ(parent->resources, 5);
    level.time = 5000; G_RunEntity(mine); /* index 1: final 5 */

    T_EQ(parent->resources, 0);
    T_ASSERT(M_IsDead(mine));
    T_EQ(mine->cargo.count, 0);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(!(second->s.renderfx & RF_HIDDEN));
    T_ASSERT(!first->paused && !second->paused);
    T_ASSERT(!(parent->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent->paused);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}


/* A successful Entangle owns a caster-local hidden/permanent command state for
 * exactly the live overlay generation.  The helper deliberately derives this
 * from saved overlay state rather than the optional CasterArt effect. */
TEST(wc3_movement, entangle_command_hidden_tracks_live_overlay_caster_generation) {
    edict_t * caster, *overlay, *parent;
    uint32_t const ability = MAKEFOURCC('A','e','n','t');

    reset_entities();
    setup_test_world();
    caster = alloc_test_unit(MAKEFOURCC('e','t','o','l'), 0.0f, 0.0f);
    overlay = alloc_test_unit(MAKEFOURCC('e','g','o','l'), 64.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 64.0f, 0.0f);
    overlay->mineoverlay.parent = parent;
    overlay->mineoverlay.parent_spawn_time = parent->spawn_time;
    overlay->mineoverlay.caster = caster;
    overlay->mineoverlay.caster_spawn_time = caster->spawn_time;
    overlay->mineoverlay.entangle_ability = ability;

    T_ASSERT(S_EntangleCommandHidden(caster, ability));
    overlay->mineoverlay.caster_spawn_time++;
    T_ASSERT(!S_EntangleCommandHidden(caster, ability));
}

static edict_t *movement_find_entangle_overlay(edict_t *caster, edict_t *parent) {
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *overlay = globals.edicts + i;
        if (overlay->inuse && overlay->mineoverlay.parent == parent &&
            overlay->mineoverlay.caster == caster &&
            overlay->mineoverlay.entangle_ability == MAKEFOURCC('A','e','n','t'))
            return overlay;
    }
    return NULL;
}

static bool movement_issue_entangle_command(edict_t *clent, gameClient_t *client,
                                            edict_t *caster, edict_t *parent) {
    abilityCall_t call = MAKE(abilityCall_t, .client = clent);
    if (!CAbilityEntangle(caster, A_COMMAND, &call) || !client->menu.on_entity_selected)
        return false;
    return client->menu.on_entity_selected(clent, parent);
}

static void movement_prepare_rooted_entangle_caster(edict_t *caster, uint32_t player) {
    uint32_t const entangle = MAKEFOURCC('A','e','n','t');
    uint32_t const root = MAKEFOURCC('A','r','o','1');
    caster->s.player = player;
    caster->data.UnitAbilities = &test_entangle_caster;
    G_ActorAddSkill(caster, entangle);
    G_ActorAddSkill(caster, root);
    caster->ancient_root.ability = root;
    caster->ancient_root.mode = ANCIENT_ROOTED;
    caster->s.flags |= EF_BUILDING;
    caster->aiflags |= AI_IMMOBILE;
    caster->runtime.flags |= UNIT_BALANCE_BUILDING;
}

TEST(wc3_movement, entangleinstant_target_order_creates_completed_overlay) {
    edict_t *caster, *parent, *overlay;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);

    T_ASSERT(unit_issuetargetorder(caster, "entangleinstant", parent));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && !overlay->construction.active);
    T_ASSERT(overlay && overlay->build != overlay);
    T_ASSERT(parent->s.renderfx & RF_HIDDEN);
    T_ASSERT(parent->paused);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, queued_entangleinstant_dispatches_when_previous_order_finishes) {
    edict_t *caster, *parent, *overlay;
    slkTestData_t *rows, *old_abilities;
    umove_t active_order = { .animation = "walk", .proc = CAbilityMove };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    caster->currentmove = &active_order;

    T_ASSERT(G_IssueUnitTargetOrder(caster, "entangleinstant", parent, true, 0));
    T_EQ(G_UnitQueuedOrderCount(caster), 1);
    T_NULL(movement_find_entangle_overlay(caster, parent));

    caster->currentmove = NULL;
    T_ASSERT(G_UnitStartNextQueuedOrder(caster));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && !overlay->construction.active);
    T_EQ(G_UnitQueuedOrderCount(caster), 0);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, entangle_range_uses_goldmine_footprint) {
    enum { W = 8, H = 8 };
    size_t const pathtex_size = sizeof(pathTex_t) + W * H * sizeof(color32_t);
    edict_t *caster, *parent, *overlay;
    pathTex_t *pathtex;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 200.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    caster->collision = 16.0f;
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    parent->s.flags |= EF_BUILDING;
    parent->runtime.flags |= UNIT_BALANCE_BUILDING;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    pathtex = gi.MemAlloc(pathtex_size);
    T_NOT_NULL(pathtex);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = W;
    pathtex->height = H;
    FOR_LOOP(i, W * H) pathtex->map[i].b = 0xff;
    parent->pathtex = pathtex;

    /* Aent's fixture range is 64. The centres are 200 apart, but the caster
     * is within its collision radius plus 64 of the authored mine footprint. */
    T_ASSERT(unit_issuetargetorder(caster, "entangleinstant", parent));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);

    parent->pathtex = NULL;
    gi.MemFree(pathtex);
    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, auto_entangle_nearby_starts_normal_construction) {
    edict_t *caster, *parent, *overlay;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);

    T_ASSERT(S_AutoEntangleNearby(caster, false));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && overlay->construction.active);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, root_completion_auto_entangles_nearest_mine_once) {
    slkTestData_t *gold_rows, *old_gold;
    edict_t *caster, *parent, *far_parent, *overlay;
    uint32_t end_time;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_gold = install_racial_goldmine_test_data(&gold_rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    far_parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 48.0f, 0.0f);
    movement_prepare_rooted_entangle_caster(caster, 0);
    caster->ancient_root.mode = ANCIENT_UPROOTED;
    caster->s.flags &= ~EF_BUILDING;
    caster->aiflags &= ~AI_IMMOBILE;
    caster->runtime.flags &= ~UNIT_BALANCE_BUILDING;
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    far_parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    setup_test_goldmine(far_parent, &test_goldmine_stock, 5000);

    S_AncientBeginMorph(caster, true);
    end_time = caster->ancient_root.transition_end_time;
    level.time = end_time - 1;
    S_RunAbilityUpdates(caster);
    T_EQ(caster->ancient_root.mode, ANCIENT_ROOTING);
    T_NULL(movement_find_entangle_overlay(caster, parent));

    level.time = end_time;
    S_RunAbilityUpdates(caster);
    T_EQ(caster->ancient_root.mode, ANCIENT_ROOTED);
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && overlay->construction.active);
    T_NULL(movement_find_entangle_overlay(caster, far_parent));

    S_RunAbilityUpdates(caster);
    T_EQ(movement_find_entangle_overlay(caster, parent), overlay);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_gold);
    free_slk_rows(gold_rows);
}

TEST(wc3_movement, wisp_waits_for_incomplete_entangled_mine_then_boards) {
    edict_t *mine, *wisp;
    slkTestData_t *rows, *old_abilities;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    wisp = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->s.player = wisp->s.player = 0;
    mine->construction.active = true;
    mine->health.value = mine->health.max_value = 1000.0f;
    wisp->stand = unit_stand;
    unit_stand(wisp);

    T_ASSERT(S_CargoOrderBoard(wisp, mine));
    T_EQ(mine->cargo.count, 0);
    T_EQ(wisp->secondarygoal, mine);
    T_NOT_NULL(wisp->currentmove);
    T_STREQ(wisp->currentmove->animation, "stand");
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityBattlestations);

    mine->construction.active = false;
    wisp->currentmove->think(wisp);
    T_EQ(mine->cargo.count, 1);
    T_EQ(mine->cargo.units[0], wisp);
    T_ASSERT(wisp->paused);
    T_ASSERT(wisp->s.renderfx & RF_HIDDEN);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, autoharvestgold_immediate_order_boards_wisp_into_entangled_mine) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *mine, *wisp;
    uint32_t steps;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('e','g','o','l'), 0.0f, 0.0f);
    wisp = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 180.0f, 0.0f);
    parent->s.player = PLAYER_NEUTRAL_PASSIVE;
    mine->s.player = wisp->s.player = 0;
    setup_test_goldmine(parent, &test_goldmine_stock, 24000);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->health.value = mine->health.max_value = 1000.0f;
    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    wisp->movetype = MOVETYPE_STEP;
    wisp->collision = 16.0f;
    wisp->unitinfo.MoveSpeed = 220.0f;
    wisp->stand = unit_stand;
    unit_stand(wisp);
    T_ASSERT(S_MineOverlayBind(mine, parent));

    /* NightElf07's script assigns this immediate order to Wisps. It must
     * select the player's Entangled Mine and enter the mine's cargo. */
    T_ASSERT(unit_issueimmediateorder(wisp, "autoharvestgold"));
    T_EQ(mine->cargo.count, 0);
    T_EQ(wisp->secondarygoal, mine);
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityBattlestations);
    for (steps = 0; steps < 64 && mine->cargo.count == 0; steps++) {
        if (!wisp->currentmove || !wisp->currentmove->think) break;
        wisp->currentmove->think(wisp);
    }
    T_EQ(mine->cargo.count, 1);
    T_EQ(mine->cargo.units[0], wisp);
    T_ASSERT(wisp->paused);
    T_ASSERT(wisp->s.renderfx & RF_HIDDEN);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, rallied_wisp_waits_for_entangled_mine_then_automatically_boards) {
    UnitBalance_t balance = { .buildTime = 1, .foodUsed = 0, .foodMade = 0 };
    edict_t *producer, *mine, *wisp;
    slkTestData_t *rows, *old_abilities;
    uint32_t steps;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), -128.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    wisp = alloc_test_unit(MAKEFOURCC('e','w','s','p'), 0.0f, 0.0f);
    producer->data.UnitProfile = &wisp_rally_producer_profile;
    producer->movetype = MOVETYPE_NONE;
    producer->collision = 64.0f;
    producer->stand = unit_stand;
    producer->s.player = mine->s.player = wisp->s.player = 0;
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->construction.active = true;
    mine->health.value = mine->health.max_value = 1000.0f;
    wisp->data.UnitAbilities = &wisp_harvest_abilities;
    wisp->data.UnitBalance = &balance;
    wisp->collision = 16.0f;
    wisp->movetype = MOVETYPE_STEP;
    wisp->unitinfo.MoveSpeed = 220.0f;
    wisp->health.value = wisp->health.max_value = 100.0f;
    wisp->stand = unit_stand;
    wisp->training = true;
    wisp->s.renderfx |= RF_HIDDEN;
    producer->build = wisp;
    unit_stand(wisp);

    T_ASSERT(G_SetRallyEntity(producer, mine));
    ai_train_build(producer);
    T_ASSERT(!wisp->training);
    T_ASSERT(!(wisp->s.renderfx & RF_HIDDEN));
    /* Training places the Wisp beside its producer; let its rally order reach
     * the mine before construction finishes. */
    for (steps = 0; steps < 64 && wisp->currentmove &&
         wisp->currentmove->proc == CAbilityBattlestations &&
         strcmp(wisp->currentmove->animation, "stand"); steps++)
        wisp->currentmove->think(wisp);
    T_EQ(mine->cargo.count, 0);
    T_EQ(wisp->secondarygoal, mine);
    T_ASSERT(wisp->currentmove && wisp->currentmove->proc == CAbilityBattlestations);
    T_STREQ(wisp->currentmove->animation, "stand");

    mine->construction.active = false;
    wisp->currentmove->think(wisp);
    T_EQ(mine->cargo.count, 1);
    T_EQ(mine->cargo.units[0], wisp);
    T_ASSERT(wisp->paused);
    T_ASSERT(wisp->s.renderfx & RF_HIDDEN);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, entangle_overlay_restores_original_permanent_state) {
    cstring_t const filename = "/tmp/openwarcraft3-wc3-entangle-lifecycle.bin";
    uint32_t const ability = MAKEFOURCC('A','e','n','t');
    edict_t *clent, *caster, *parent, *overlay;
    gameClient_t *client;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    /* The test archive omits normal unit metadata; make the resulting hbar
     * building explicit so runtime spawn follows the authored Aent contract. */
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->client = client;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    caster->no_pathing = true; /* rooted Ancient state */
    caster->s.player = parent->s.player = client->ps.number;
    setup_test_goldmine(parent, &test_goldmine_stock, 5000);
    caster->data.UnitAbilities = &test_entangle_caster;
    T_ASSERT(G_ActorSetSkillPermanent(caster, ability, true));
    G_ActorAddSkill(caster, ability);
    caster->ancient_root.ability = MAKEFOURCC('A','r','o','1');
    caster->ancient_root.mode = ANCIENT_UPROOTED;
    G_SelectEntity(client, caster);
    caster->no_pathing = false;
    T_ASSERT(!movement_issue_entangle_command(clent, client, caster, parent));
    caster->no_pathing = true;
    caster->ancient_root.mode = ANCIENT_ROOTED;
    caster->s.flags |= EF_BUILDING;
    caster->aiflags |= AI_IMMOBILE;
    caster->runtime.flags |= UNIT_BALANCE_BUILDING;
    T_ASSERT(movement_issue_entangle_command(clent, client, caster, parent));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && overlay->mineoverlay.entangle_permanent_before);
    T_ASSERT(overlay && overlay->construction.active);

    T_ASSERT(WriteGame(filename));
    T_ASSERT(G_ActorSetSkillPermanent(caster, ability, false));
    overlay->mineoverlay.entangle_permanent_before = false;
    T_ASSERT(ReadGame(filename));
    T_ASSERT(G_ActorSkillPermanent(caster, ability));
    overlay = movement_find_entangle_overlay(caster, parent);
    T_NOT_NULL(overlay);
    T_ASSERT(overlay && overlay->mineoverlay.entangle_permanent_before);
    S_MineOverlayRelease(overlay);
    T_ASSERT(G_ActorSkillPermanent(caster, ability));
    T_ASSERT(!(parent->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent->paused);
    remove(filename);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, entangle_missing_overlay_unit_id_reports_unavailable) {
    static char const missing_unit_id[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y2;X1;K\"Aent\"\nC;Y2;X2;K\"Aent\"\n"
        "C;Y2;X3;K10\nE\n";
    slkTestData_t *rows, *old_abilities;
    edict_t *clent, *caster, *parent;
    gameClient_t *client;
    uint32_t count_before;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(missing_unit_id);
    old_abilities = G_SetSLKRows("AbilityData", rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    clent = &g_edicts[0]; client = &game.clients[0];
    clent->client = client;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    caster->data.UnitAbilities = &test_entangle_caster;
    caster->s.player = parent->s.player = client->ps.number;
    caster->ancient_root.ability = MAKEFOURCC('A','r','o','1');
    caster->ancient_root.mode = ANCIENT_ROOTED;
    G_ActorAddSkill(caster, MAKEFOURCC('A','e','n','t'));
    G_SelectEntity(client, caster);
    count_before = globals.num_edicts;

    T_ASSERT(!movement_issue_entangle_command(clent, client, caster, parent));
    T_EQ(globals.num_edicts, count_before);
    T_NOT_NULL(client->menu.on_entity_selected);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, one_tree_cannot_entangle_multiple_gold_mines) {
    uint32_t const ability = MAKEFOURCC('A','e','n','t');
    edict_t *clent, *caster, *first, *second, *parent1, *parent2;
    gameClient_t *client;
    slkTestData_t *rows, *old_abilities;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    gi.Write = movement_noop_write;
    gi.unicast = movement_noop_unicast;
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->client = client;
    caster = alloc_test_unit(MAKEFOURCC('e','T','S','T'), 0.0f, 0.0f);
    parent1 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 32.0f, 0.0f);
    parent2 = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 48.0f, 0.0f);
    caster->s.player = parent1->s.player = parent2->s.player = client->ps.number;
    caster->no_pathing = true; /* rooted Ancient state */
    setup_test_goldmine(parent1, &test_goldmine_stock, 5000);
    setup_test_goldmine(parent2, &test_goldmine_stock, 5000);
    caster->abilities.added[0] = ability;
    caster->abilities.added[1] = MAKEFOURCC('A','r','o','1');
    caster->abilities.added_count = 2;
    caster->ancient_root.ability = MAKEFOURCC('A','r','o','1');
    caster->ancient_root.mode = ANCIENT_ROOTED;
    caster->s.flags |= EF_BUILDING;
    caster->aiflags |= AI_IMMOBILE;
    caster->runtime.flags |= UNIT_BALANCE_BUILDING;
    G_SelectEntity(client, caster);
    T_ASSERT(movement_issue_entangle_command(clent, client, caster, parent1));
    first = movement_find_entangle_overlay(caster, parent1);
    T_NOT_NULL(first);
    T_ASSERT(first && !first->mineoverlay.entangle_permanent_before);
    T_ASSERT(G_ActorSkillPermanent(caster, ability));

    T_ASSERT(!movement_issue_entangle_command(clent, client, caster, parent2));
    second = movement_find_entangle_overlay(caster, parent2);
    T_NULL(second);

    unit_die(caster, NULL);
    T_ASSERT(M_IsDead(first));
    T_NULL(first->mineoverlay.parent);
    T_ASSERT(!(parent1->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent1->paused);
    T_ASSERT(!G_ActorSkillPermanent(caster, ability));

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

/* Depletion must retire an Entangled Mine even when every Wisp has already left it. */
TEST(wc3_movement, empty_entangled_mine_dies_when_parent_is_depleted) {
    slkTestData_t *rows, *old_abilities;
    edict_t *parent, *mine;

    reset_entities();
    setup_test_world();
    old_abilities = install_racial_goldmine_test_data(&rows);
    parent = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 0.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    setup_test_goldmine(parent, &test_goldmine_stock, 0);
    mine->data.UnitAbilities = &test_entangled_mine;
    mine->think = monster_think;
    mine->health.value = mine->health.max_value = 1000.0f;
    T_ASSERT(S_MineOverlayBind(mine, parent));

    level.time = 0;
    G_RunEntity(mine);
    T_ASSERT(M_IsDead(mine));
    T_ASSERT(!(parent->s.renderfx & RF_HIDDEN));
    T_ASSERT(!parent->paused);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

static char const cargo_unload_test_data[] =
    "ID;PWXL;N;E\n"
    "C;Y1;X1;K\"alias\"\n"
    "C;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"DataA1\"\n"
    "C;Y1;X4;K\"Dur1\"\n"
    "C;Y2;X1;K\"Acar\"\n"
    "C;Y2;X2;K\"Acar\"\n"
    "C;Y2;X3;K8\n"
    "C;Y2;X4;K0.3\n"
    "E\n";

/* Give cargo scenarios the same lifecycle callbacks as spawned units. */
static edict_t *cargo_unload_transport(void) {
    static UnitAbilities_t const abilities = { .abilList = "Acar,Adro,Adri" };
    edict_t *transport = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256, 256);
    transport->data.UnitAbilities = &abilities;
    transport->think = monster_think; transport->stand = unit_stand; transport->die = unit_die;
    unit_stand(transport);
    FOR_LOOP(i, 3) {
        edict_t *unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256, 256);
        T_ASSERT(S_CargoTryLoad(transport, unit));
    }
    return transport;
}

TEST(wc3_movement, unload_all_stop_and_move_cancel_remaining_passengers) {
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    FOR_LOOP(i, 2) {
        edict_t *transport = cargo_unload_transport();
        level.time = 1000;
        T_ASSERT(S_CargoBeginUnloadAll(transport));
        T_EQ(transport->cargo.count, 2);
        if (i) order_move(transport, Waypoint_add(&MAKE(vec2_t, 512, 512)));
        else order_stop(transport);
        level.time += 1000; G_RunEntities();
        T_EQ(transport->cargo.count, 2);
        T_ASSERT(transport->cargo.units[0]->paused);
        T_ASSERT(transport->cargo.units[0]->s.renderfx & RF_HIDDEN);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_pause_and_stun_suspend_passengers) {
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    level.time = 1000;
    T_ASSERT(S_CargoBeginUnloadAll(transport));
    transport->paused = true;
    level.time += 300; G_RunEntities();
    T_EQ(transport->cargo.count, 2);
    transport->paused = false; transport->stunned = true;
    level.time += 300; G_RunEntities();
    T_EQ(transport->cargo.count, 2);
    transport->stunned = false;
    level.time += 300; G_RunEntities();
    T_EQ(transport->cargo.count, 1);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_command_and_instant_dispatch) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    gi.Write = movement_noop_write; gi.unicast = movement_noop_unicast;
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    cstring_t drop[] = { "button", "Adro" }, instant[] = { "button", "Adri" };
    edict_t *clent = &g_edicts[0];
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    transport->svflags |= SVF_MONSTER;
    G_SelectEntity(clent->client, transport);
    level.time = 1000;
    G_ClientCommand(clent, 2, drop);
    T_NOT_NULL(clent->client->menu.on_location_selected);
    if (clent->client->menu.on_location_selected)
        T_ASSERT(clent->client->menu.on_location_selected(clent, &transport->s.origin2));
    T_EQ(transport->cargo.count, 2);
    T_ASSERT(S_CargoBeginUnloadAll(transport)); /* Repeated clicks do not bypass Dur. */
    T_EQ(transport->cargo.count, 2);
    G_ClientCommand(clent, 2, instant);
    T_EQ(transport->cargo.count, 0);
    edict_t *passenger = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256, 256);
    T_ASSERT(S_CargoTryLoad(transport, passenger));
    level.time += 1000; G_RunEntities();
    T_EQ(transport->cargo.count, 1); /* Instant cancels the old timed unload. */
    gi.Write = old_write; gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_round_trip_resumes_remaining_cargo) {
    cstring_t filename = "/tmp/openwarcraft3-cargo-unload-save.bin";
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    /* Runtime abilities survive data-pointer rebinding during ReadGame. */
    transport->abilities.added[0] = MAKEFOURCC('A','c','a','r');
    ARRAY_COUNT(transport->abilities.added) = 1;
    level.time = 1000;
    T_ASSERT(S_CargoBeginUnloadAll(transport));
    edict_t *second = transport->cargo.units[0], *third = transport->cargo.units[1];
    level.time += 100;
    T_ASSERT(WriteGame(filename));
    order_stop(transport); cargo_drop_all(transport);
    T_ASSERT(ReadGame(filename));
    T_EQ(transport->cargo.count, 2);
    T_EQ(transport->cargo.units[0], second);
    T_EQ(transport->cargo.units[1], third);
    level.time = 1299; G_RunEntities(); T_EQ(transport->cargo.count, 2);
    level.time = 1300; G_RunEntities(); T_EQ(transport->cargo.count, 1);
    T_ASSERT(!second->paused && !(second->s.renderfx & RF_HIDDEN));
    level.time = 1600; G_RunEntities(); T_EQ(transport->cargo.count, 0);
    T_ASSERT(!third->paused && !(third->s.renderfx & RF_HIDDEN));
    remove(filename);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_zero_duration_roc_hold_and_single_slot) {
    slkTestData_t *rows = parse_slk_string(
        "ID;PWXL;N;E\nC;Y1;X1;K\"alias\"\nC;X2;K\"Data11\"\nC;X3;K\"Dur1\"\n"
        "C;Y2;X1;K\"Acar\"\nC;X2;K8\nC;X3;K0\nE\n");
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    T_ASSERT(S_CargoUnloadAt(transport, 1));
    level.time = 1000; G_RunEntities();
    T_EQ(transport->cargo.count, 2); /* A cargo-slot click only ejects that passenger. */
    T_ASSERT(S_CargoBeginUnloadAll(transport));
    T_EQ(transport->cargo.count, 1);
    G_RunEntities(); T_EQ(transport->cargo.count, 1);
    level.time += FRAMETIME; G_RunEntities(); T_EQ(transport->cargo.count, 0);
    T_ASSERT(!S_CargoBeginUnloadAll(transport));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_transport_death_ejects_remaining_cargo) {
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    edict_t *transport = cargo_unload_transport();
    level.time = 1000;
    T_ASSERT(S_CargoBeginUnloadAll(transport));
    edict_t *passenger = transport->cargo.units[0];
    transport->health.value = 0; unit_die(transport, NULL);
    T_EQ(transport->cargo.count, 0);
    T_ASSERT(!passenger->paused && !(passenger->s.renderfx & RF_HIDDEN));
    level.time += 1000; G_RunEntities();
    T_ASSERT(!S_CargoBeginUnloadAll(transport));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_movement, unload_all_repeats_one_passenger_per_cargo_duration) {
    static UnitAbilities_t const transport_abilities = { .abilList = "Acar" };
    slkTestData_t *rows = parse_slk_string(cargo_unload_test_data);
    slkTestData_t *old_abilities;
    edict_t *transport, *first, *second, *third;

    reset_entities();
    setup_test_world();
    old_abilities = G_SetSLKRows("AbilityData", rows);
    transport = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 256.0f);
    first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    third = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    transport->data.UnitAbilities = &transport_abilities;
    transport->think = monster_think; transport->stand = unit_stand;
    transport->cargo.units[0] = first;
    transport->cargo.units[1] = second;
    transport->cargo.units[2] = third;
    transport->cargo.count = 3;
    first->s.renderfx |= RF_HIDDEN; first->paused = true;
    second->s.renderfx |= RF_HIDDEN; second->paused = true;
    third->s.renderfx |= RF_HIDDEN; third->paused = true;
    level.time = 1000;

    T_ASSERT(S_CargoBeginUnloadAll(transport));
    T_EQ(transport->cargo.count, 2);
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(!first->paused);
    T_ASSERT(second->s.renderfx & RF_HIDDEN);
    T_ASSERT(third->s.renderfx & RF_HIDDEN);

    level.time += 299;
    G_RunEntities();
    T_EQ(transport->cargo.count, 2);

    level.time += 1;
    G_RunEntities();
    T_EQ(transport->cargo.count, 1);
    T_ASSERT(!(second->s.renderfx & RF_HIDDEN));
    T_ASSERT(!second->paused);
    T_ASSERT(third->s.renderfx & RF_HIDDEN);

    level.time += 300;
    G_RunEntities();
    T_EQ(transport->cargo.count, 0);
    T_ASSERT(!(third->s.renderfx & RF_HIDDEN));
    T_ASSERT(!third->paused);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(rows);
}

TEST(wc3_movement, occupied_burrow_exposes_attack_stop_and_stand_down_only_with_cargo) {
    static UnitAbilities_t const burrow_abilities = {
        .id = MAKEFOURCC('o','b','u','r'),
        .abilList = "Abun",
    };
    static UnitWeapons_t const burrow_weapons = {
        .id = MAKEFOURCC('o','b','u','r'),
        .attacksEnabled = 3,
        .attack1 = { .damageDice = 1 },
    };
    static UnitBalance_t const burrow_balance = {
        .id = MAKEFOURCC('o','b','u','r'),
        .speed = 0,
    };
    edict_t *burrow = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 256.0f, 256.0f);
    edict_t *peon = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    gameCommandButton_t buttons[16];
    uint8_t count;
    bool attack, stop, stand_down;

    burrow->data.UnitAbilities = &burrow_abilities;
    burrow->data.UnitWeapons = &burrow_weapons;
    burrow->data.UnitBalance = &burrow_balance;

    count = G_GetCommandButtons(burrow, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    attack = stop = stand_down = false;
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, STR_CmdAttack)) attack = true;
        if (!strcmp(buttons[i].command, STR_CmdStop)) stop = true;
        if (!strcmp(buttons[i].command, "Astd")) stand_down = true;
    }
    T_ASSERT(!attack);
    T_ASSERT(!stop);
    T_ASSERT(!stand_down);

    burrow->cargo.units[0] = peon;
    burrow->cargo.count = 1;
    count = G_GetCommandButtons(burrow, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    attack = stop = stand_down = false;
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, STR_CmdAttack)) attack = true;
        if (!strcmp(buttons[i].command, STR_CmdStop)) stop = true;
        if (!strcmp(buttons[i].command, "Astd")) stand_down = true;
    }
    T_ASSERT(attack);
    T_ASSERT(stop);
    T_ASSERT(stand_down);
}

TEST(wc3_movement, stand_down_stops_attack_before_unloading_burrow) {
    static UnitAbilities_t const burrow_abilities = {
        .id = MAKEFOURCC('o','b','u','r'),
        .abilList = "Abun",
    };
    edict_t *burrow = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 256.0f);
    edict_t *peon = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 320.0f, 256.0f);

    burrow->data.UnitAbilities = &burrow_abilities;
    burrow->stand = unit_stand;
    burrow->cargo.units[0] = peon;
    burrow->cargo.count = 1;
    peon->s.renderfx |= RF_HIDDEN;
    peon->paused = true;
    burrow->attack1.type = ATK_PIERCE;
    burrow->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    target->targtype = TARG_GROUND;

    order_attack(burrow, target);
    T_NOT_NULL(burrow->currentmove);
    T_ASSERT(burrow->currentmove->proc == CAbilityAttack);
    T_ASSERT(burrow->combatentity == target);

    S_CargoStandDown(burrow);

    T_EQ(burrow->cargo.count, 0);
    T_ASSERT(!(peon->s.renderfx & RF_HIDDEN));
    T_ASSERT(!peon->paused);
    T_NOT_NULL(burrow->currentmove);
    T_ASSERT(burrow->currentmove->proc != CAbilityAttack);
    T_NULL(burrow->combatentity);
    T_NULL(burrow->goalentity);
}

TEST(wc3_movement, removing_loaded_unit_releases_transport_slot) {
    edict_t *transport, *passenger;

    reset_entities();
    setup_test_world();
    transport = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0.0f, 0.0f);
    passenger = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    transport->cargo.units[0] = passenger;
    transport->cargo.count = 1;
    passenger->s.renderfx |= RF_HIDDEN;
    passenger->paused = true;

    G_FreeEdict(passenger);
    T_EQ(transport->cargo.count, 0);
    T_NULL(transport->cargo.units[0]);
    T_ASSERT(!passenger->inuse);
}

TEST(wc3_movement, cargo_unload_at_releases_requested_occupant_and_keeps_remaining_order) {
    edict_t *burrow = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 256.0f);
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 256.0f, 256.0f);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 320.0f, 256.0f);

    first->s.renderfx |= RF_HIDDEN; first->paused = true;
    second->s.renderfx |= RF_HIDDEN; second->paused = true;
    burrow->cargo.units[0] = first; burrow->cargo.units[1] = second; burrow->cargo.count = 2;

    T_ASSERT(S_CargoTransportForUnit(first) == burrow);
    T_ASSERT(S_CargoUnloadAt(burrow, 0));
    T_EQ(burrow->cargo.count, 1);
    T_ASSERT(S_CargoUnitAt(burrow, 0) == second);
    T_NULL(S_CargoTransportForUnit(first));
    T_ASSERT(!(first->s.renderfx & RF_HIDDEN));
    T_ASSERT(!first->paused);
    T_ASSERT(second->s.renderfx & RF_HIDDEN);
    T_ASSERT(second->paused);
}

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

#endif /* BZ_TESTS */
