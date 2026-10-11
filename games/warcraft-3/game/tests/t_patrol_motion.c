#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "retail_patrol_motion.h"

bool run_test_jass(cstring_t);
extern void (*test_preload_marker)(cstring_t);
void reset_entities(void);
void setup_test_world(void);
void CM_SetupTestPathmap(unsigned, unsigned, uint8_t const *);
void CM_SetupTestWorldBounds(box2_t const *);

/* Replays the captured patrol_motion probe (tools/frida/wc3_patrol_motion_probe.j)
 * on its flat 64x64 copied map: the probe's own timer issues Move/Patrol and
 * reissues idle Move subjects, and its exported hashtable rows are the samples. */
typedef struct { float x, y, facing; uint32_t order; } patrolMotionSample_t;
typedef struct { bool ran; unsigned rows; patrolMotionSample_t samples[301][10]; } patrolMotionRun_t;
static patrolMotionRun_t *patrol_motion_active;

static hashtableEntry_t const *patrol_motion_cell(int32_t row, int32_t col) {
    hashtable_t const *table = &level.hashtables[0];
    for (uint32_t i = table->num_entries; i-- > 0;)
        if (table->entries[i].parent == row && table->entries[i].child == col) return table->entries + i;
    return NULL;
}

/* Each probe row is exported before its PATHMETA marker; read it back at the marker. */
static void patrol_motion_marker(cstring_t text) {
    unsigned row, tick, unit; char label[16];
    if (sscanf(text, "PATHMETA patrol_motion row=%u tick=%u case=%u label=%15s", &row, &tick, &unit, label) != 4) return;
    patrolMotionRun_t *run = patrol_motion_active;
    T_EQ(row, run->rows); run->rows++;
    if (strcmp(label, "sample") || tick > 300 || unit >= 10) return;
    hashtableEntry_t const *cells[6];
    FOR_LOOP(c, 6) { cells[c] = patrol_motion_cell(row, c); T_NOT_NULL(cells[c]); if (!cells[c]) return; }
    T_EQ(cells[0]->value.integer, (int32_t)tick); T_EQ(cells[1]->value.integer, (int32_t)unit);
    run->samples[tick][unit] = (patrolMotionSample_t){cells[4]->value.real, cells[5]->value.real,
        cells[3]->value.real, (uint32_t)cells[2]->value.integer};
}

static void patrol_motion_run(patrolMotionRun_t *run, unsigned step) {
    run->ran = true;
    FOR_LOOP(i, level.num_timers) G_TimerDestroy(level.timers + i);
    reset_entities(); setup_test_world();
    uint8_t cells[64*64] = {0}; CM_SetupTestPathmap(64, 64, cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    level.waypoints = (typeof(level.waypoints)){0}; level.pathing_clock = (wc3Clock_t){0,0,300};
    level.time = level.pathing_msec = 0; level.pathing_phase = 0; level.pathing_due = false;
    patrol_motion_active = run; test_preload_marker = patrol_motion_marker;
    static char script[sizeof(patrol_motion_probe) + 128];
    snprintf(script, sizeof(script), "%sfunction main takes nothing returns nothing\n call PathProbeInit()\nendfunction\n",
             patrol_motion_probe);
    T_ASSERT(run_test_jass(script));
    G_FinishMovePathingInitialization();
    /* TODO: the fixture MPQ's hfoo row lacks the authored Footman collision16. */
    FILTER_EDICTS(u, u->inuse && u->class_id == MAKEFOURCC('h','f','o','o')) if (!u->collision) u->collision = 16;
    level.started = level.scriptsConfigured = level.scriptsStarted = true;
    while (level.time < 31000 && !run->samples[300][9].order) { level.time += step; globals.RunFrame(); }
    T_ASSERT(!jass_rterror_pending(level.vm));
    test_preload_marker = NULL; patrol_motion_active = NULL;
    FOR_LOOP(i, level.num_timers) G_TimerDestroy(level.timers + i);
    level.started = false; reset_entities(); setup_test_world();
}

static patrolMotionRun_t patrol_motion;

static void patrol_motion_scene(void) { if (!patrol_motion.ran) patrol_motion_run(&patrol_motion, 5); }

static float patrol_motion_word(uint32_t word) { float f; memcpy(&f, &word, sizeof f); return f; }

/* First tick (1-based) where subject p leaves subject m shifted by (dx,dy); 0 when shared throughout. */
static unsigned patrol_motion_split(unsigned m, unsigned p, float dx, float dy, unsigned ticks) {
    for (unsigned t = 1; t <= ticks; t++) {
        patrolMotionSample_t const *a = &patrol_motion.samples[t][m], *b = &patrol_motion.samples[t][p];
        if (fabsf(a->x + dx - b->x) > 0.01f || fabsf(a->y + dy - b->y) > 0.01f) return t;
    }
    return 0;
}

TEST(wc3_patrol_motion, every_subject_matches_original_public_samples) {
    patrol_motion_scene();
    /* All ten subjects (open, walled, enclosed and head-on lanes) replay the original
     * journey word for word: current order, GetUnitFacing, GetUnitX and GetUnitY. */
    FOR_LOOP(i, 10) {
        unsigned diverged = 0;
        for (unsigned t = 1; t <= 300 && !diverged; t++) {
            patrolMotionSample_t const *s = &patrol_motion.samples[t][i];
            uint32_t const *r = patrol_motion_retail[i][t - 1];
            uint32_t const actual[4] = {s->order, wc3_float_bits(s->facing), wc3_float_bits(s->x), wc3_float_bits(s->y)};
            if (memcmp(actual, r, sizeof(actual))) {
                diverged = t;
                fprintf(stderr, "patrol_motion subject%u tick%u actual=%u/%08x/%08x/%08x expected=%u/%08x/%08x/%08x\n",
                        (unsigned)i, t, actual[0], actual[1], actual[2], actual[3], r[0], r[1], r[2], r[3]);
            }
        }
        T_EQ(diverged, 0u);
    }
}

TEST(wc3_patrol_motion, server_frame_cadence_replays_the_same_journey) {
    /* The scheduler subdivides each frame on the retail clock, so the shipped 10 Hz
     * server loop reproduces the 5 ms replay (and thus the original) sample for sample. */
    patrol_motion_scene();
    static patrolMotionRun_t coarse;
    patrol_motion_run(&coarse, FRAMETIME);
    T_EQ(coarse.rows, patrol_motion.rows);
    T_ASSERT(!memcmp(coarse.samples, patrol_motion.samples, sizeof(coarse.samples)));
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
    /* No stalls or in-place spinning on any Patrol lane, including the head-on pair. */
    unsigned const lanes[] = {1, 3, 4, 5, 8};
    FOR_LOOP(k, 5) {
        unsigned still = 0, worst = 0;
        for (unsigned t = 2; t <= 300; t++) {
            patrolMotionSample_t const *a = &patrol_motion.samples[t - 1][lanes[k]], *b = &patrol_motion.samples[t][lanes[k]];
            still = a->x == b->x && a->y == b->y ? still + 1 : 0;
            if (still > worst) worst = still;
        }
        T_ASSERT(worst <= 5);
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
