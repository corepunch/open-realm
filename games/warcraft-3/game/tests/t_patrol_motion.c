#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_patrol_motion.h"

bool run_test_jass(cstring_t);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(unsigned, unsigned, uint8_t const *);
void CM_SetupTestWorldBounds(box2_t const *);

/* Paired Move/Patrol Footmen on the retail patrol_motion probe geometry
 * (tools/frida/wc3_patrol_motion_probe.j): a 64x64 fine pathmap over 2048x2048,
 * with the same SetTerrainPathable walls. Move subjects are reissued toward the
 * opposite endpoint once idle, as the probe does at its 0.1s sample. */
typedef struct { bool patrol; vec2_t a, b; } patrolMotionCase_t;
static patrolMotionCase_t const patrol_motion_cases[10] = {
    {false,{272,256},{1008,256}}, {true,{272,448},{1008,448}},
    {false,{272,768},{1008,768}}, {true,{272,1216},{1008,1216}},
    {true,{272,1600},{1008,1600}}, {true,{1008,1600},{272,1600}},
    {false,{272,1792},{1008,1792}}, {false,{1008,1792},{272,1792}},
    {true,{1392,320},{1392,1088}}, {false,{1648,320},{1648,1088}},
};

typedef struct { float x, y, facing; uint32_t order; } patrolMotionSample_t;
typedef struct { bool ran; edict_t *units[10]; bool toward_b[10]; patrolMotionSample_t samples[301][10]; } patrolMotionRun_t;

static void patrol_motion_sample(patrolMotionRun_t *run, unsigned tick) {
    FOR_LOOP(i, 10) {
        edict_t const *u = run->units[i];
        run->samples[tick][i] = (patrolMotionSample_t){u->s.origin2.x, u->s.origin2.y,
            S_UnitFacing((edict_t *)u) * 180.0f / (float)M_PI, u->current_order_id};
    }
}

static void patrol_motion_dump(patrolMotionRun_t const *run, char const *path) {
    FILE *f = fopen(path, "w"); if (!f) return;
    FOR_LOOP(t, 301) FOR_LOOP(i, 10) {
        patrolMotionSample_t const *s = &run->samples[t][i];
        fprintf(f, "{\"t\":%u,\"case\":%u,\"order\":%u,\"facing\":%.3f,\"x\":%.3f,\"y\":%.3f}\n",
                (unsigned)t, (unsigned)i, s->order, s->facing, s->x, s->y);
    }
    fclose(f);
}

static void patrol_motion_run(patrolMotionRun_t *run) {
    run->ran = true;
    reset_entities(); setup_test_world();
    uint8_t cells[64*64] = {0}; CM_SetupTestPathmap(64, 64, cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    T_ASSERT(run_test_jass("function block takes real x0, real y0, real x1, real y1 returns nothing\n"
        " local real x=x0\n local real y\n loop\n  exitwhen x>x1\n  set y=y0\n  loop\n   exitwhen y>y1\n"
        "   call SetTerrainPathable(x,y,PATHING_TYPE_WALKABILITY,false)\n   set y=y+32.0\n  endloop\n"
        "  set x=x+32.0\n endloop\nendfunction\n"
        "function main takes nothing returns nothing\n"
        " call block(608.0,608.0,672.0,928.0)\n call block(608.0,1056.0,672.0,1376.0)\n"
        " call block(1344.0,1040.0,1440.0,1136.0)\n call block(1600.0,1040.0,1696.0,1136.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',272.0,256.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',272.0,448.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',272.0,768.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',272.0,1216.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',272.0,1600.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',1008.0,1600.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',272.0,1792.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',1008.0,1792.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',1392.0,320.0,0.0),0.0)\n"
        " call SetUnitAcquireRange(CreateUnit(Player(0),'hfoo',1648.0,320.0,0.0),0.0)\nendfunction\n"));
    unsigned n = 0;
    FILTER_EDICTS(u, u->inuse && u->class_id == MAKEFOURCC('h','f','o','o')) {
        if (n < 10) run->units[n] = u, run->toward_b[n] = true;
        n++;
    }
    T_EQ(n, 10); if (n != 10) return;
    FOR_LOOP(i, 10) {
        edict_t *u = run->units[i];
        T_FEQ(u->s.origin2.x, patrol_motion_cases[i].a.x, 0.01f); T_FEQ(u->s.origin2.y, patrol_motion_cases[i].a.y, 0.01f);
        if (!u->collision) u->collision = 16;
    }
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    patrol_motion_sample(run, 0);
    FOR_LOOP(i, 10)
        T_ASSERT(unit_issueorder(run->units[i], patrol_motion_cases[i].patrol ? "patrol" : "move",
                                 &patrol_motion_cases[i].b));
    for (unsigned tick = 1; tick <= 300; tick++) {
        level.time += FRAMETIME; globals.RunFrame();
        FOR_LOOP(i, 10) {
            edict_t *u = run->units[i];
            if (patrol_motion_cases[i].patrol || u->current_order_id) continue;
            run->toward_b[i] = !run->toward_b[i];
            T_ASSERT(unit_issueorder(u, "move", run->toward_b[i] ? &patrol_motion_cases[i].b : &patrol_motion_cases[i].a));
        }
        patrol_motion_sample(run, tick);
    }
    char const *path = getenv("WC3_PATROL_MOTION_DUMP");
    if (path) patrol_motion_dump(run, path);
    reset_entities(); setup_test_world();
}


static patrolMotionRun_t patrol_motion;

static void patrol_motion_scene(void) { if (!patrol_motion.ran) patrol_motion_run(&patrol_motion); }

static float patrol_motion_word(uint32_t word) { float f; memcpy(&f, &word, sizeof f); return f; }

/* First tick (1-based) where subject p leaves subject m shifted by (dx,dy); 0 when shared throughout. */
static unsigned patrol_motion_split(unsigned m, unsigned p, float dx, float dy, unsigned ticks) {
    for (unsigned t = 1; t <= ticks; t++) {
        patrolMotionSample_t const *a = &patrol_motion.samples[t][m], *b = &patrol_motion.samples[t][p];
        if (fabsf(a->x + dx - b->x) > 0.01f || fabsf(a->y + dy - b->y) > 0.01f) return t;
    }
    return 0;
}

TEST(wc3_patrol_motion, open_lane_matches_original_public_samples) {
    patrol_motion_scene();
    /* Open lanes: an alternating Patrol and a reissued Move both replay the original journey. */
    FOR_LOOP(i, 2) {
        unsigned diverged = 0;
        for (unsigned t = 1; t <= 300 && !diverged; t++) {
            patrolMotionSample_t const *s = &patrol_motion.samples[t][i];
            uint32_t const *r = patrol_motion_retail[i][t - 1];
            if (s->order != r[0] || fabsf(s->x - patrol_motion_word(r[2])) > 0.01f ||
                fabsf(s->y - patrol_motion_word(r[3])) > 0.01f) diverged = t;
        }
        T_EQ(diverged, 0u);
    }
}

TEST(wc3_patrol_motion, patrol_legs_are_move_legs) {
    patrol_motion_scene();
    /* Original5fdff0 runs each leg as the ordinary point task: until the Move subject goes
     * idle for its reissue, the Patrol subject is the same journey shifted to its lane. */
    T_EQ(patrol_motion_split(0, 1, 0, 192, 58), 0u);
    T_EQ(patrol_motion_split(2, 3, 0, 448, 16), 0u);
    T_EQ(patrol_motion_split(9, 8, -256, 0, 28), 0u);
    unsigned const patrols[] = {1, 3, 4, 5, 8};
    FOR_LOOP(k, 5) {
        unsigned other = 0;
        for (unsigned t = 1; t <= 300; t++) if (patrol_motion.samples[t][patrols[k]].order != WC3_ORDER_ID_PATROL_TWO_POINTS) other++;
        T_EQ(other, 0u);
    }
    /* No stalls or in-place spinning on the open, walled and enclosed lanes. The head-on pair
     * (4/5) is excluded: its stalls are the shared Move avoidance gap, not Patrol ownership. */
    unsigned const lanes[] = {1, 3, 8};
    FOR_LOOP(k, 3) {
        unsigned still = 0, worst = 0;
        for (unsigned t = 2; t <= 300; t++) {
            patrolMotionSample_t const *a = &patrol_motion.samples[t - 1][lanes[k]], *b = &patrol_motion.samples[t][lanes[k]];
            still = a->x == b->x && a->y == b->y ? still + 1 : 0;
            if (still > worst) worst = still;
        }
        T_ASSERT(worst <= 2);
    }
}

TEST(wc3_patrol_motion, enclosed_endpoint_reverses_at_closest_reachable_point) {
    /* The far endpoint sits in a blocked 3x3 cell block. Original reverses at the closest
     * reachable point (y~1094) rather than stalling or spinning, and keeps alternating. */
    patrol_motion_scene();
    unsigned reversals = 0; bool north = true;
    float peak = 0, west = 1392.0f, east = 1392.0f;
    for (unsigned t = 1; t <= 300; t++) {
        float y = patrol_motion.samples[t][8].y;
        west = fminf(west, patrol_motion.samples[t][8].x), east = fmaxf(east, patrol_motion.samples[t][8].x);
        T_ASSERT(y < 1100.0f);
        peak = fmaxf(peak, y);
        if (north && y > 1080.0f) north = false, reversals++;
        else if (!north && y < 420.0f) north = true, reversals++;
    }
    T_ASSERT(reversals >= 5);
    T_FEQ(peak, patrol_motion_word(patrol_motion_retail[8][30][3]), 1.0f);
    /* Original slides beside the block between x 1328.2 and 1394.5 on the returns. */
    T_ASSERT(west > 1320.0f && east < 1400.0f);
}
#endif
