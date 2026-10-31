#ifdef BZ_TESTS
/*
 * test_lumber_crowd.c — Several workers ordered to one tree must spread around
 * it instead of piling up outside the range circle and playing the walk
 * animation in place.  Regression for the reported "workers march in place
 * next to a tree" defect: a field rooted at the tree centre ends at one
 * grid-chosen corner of the authored footprint, so a whole group walked around
 * the tree to that same corner; the first worker chopped there and the rest
 * stayed blocked at the contact boundary in the Walk state forever.
 *
 * The harvest walk now steers each worker at its own nearest legal point
 * inside chop range, and a worker that cannot take a step holds the stand pose
 * while the crowd/pass policy retries.  Workers that still cannot reach a legal
 * chop position queue in the stand pose; they are not stuck in Walk.
 */

#include <math.h>
#include <string.h>
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

/* Helpers defined in t_utils.c */
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
void CM_SetupTestPathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void CM_SetupTestWorldBounds(box2_t const *bounds);
void CM_ProcessPathJobs(uint32_t work_budget);
void harvest_start(edict_t *self, edict_t *target);
extern float HARVEST_RANGE;
extern float HARVEST_SEARCH_RANGE;

#define LUMBER_CROWD_WORKERS 6
#define LUMBER_CROWD_RANGE 100

static edict_t *make_crowd_worker(float x, float y) {
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    worker->movetype = MOVETYPE_STEP;
    worker->stand = unit_stand;
    worker->die = unit_die;
    worker->collision = 16.0f;
    worker->s.model = 1;
    worker->health.value = worker->health.max_value = 250.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->unit_state->attack1.damagePoint = 0.01f;
    worker->unit_state->attack1.type = ATK_NORMAL;
    unit_stand(worker);
    gi.LinkEntity(worker);
    return worker;
}

static edict_t *make_crowd_tree(float x, float y, int footprint_cells) {
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), x, y);
    pathTex_t *pathtex;
    size_t const size = sizeof(*pathtex) +
                        (size_t)footprint_cells * footprint_cells * sizeof(color32_t);

    SP_monster_tree(tree);
    if (!tree->destructable) tree->destructable = G_AllocDestructable();
    assert(tree->destructable);
    tree->destructable->item_table = (uint32_t)-1;
    tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = 5000.0f;
    pathtex = gi.MemAlloc(size);
    memset(pathtex, 0, size);
    pathtex->width = pathtex->height = footprint_cells;
    for (int i = 0; i < footprint_cells * footprint_cells; i++) pathtex->map[i].b = 0xff;
    tree->pathtex = pathtex;
    tree->collision = 0.0f;
    gi.LinkEntity(tree);
    return tree;
}

/* Run a group of workers ordered to one tree and report how many waited in the
 * Walk presentation (the marching bug) and how many reached the chop state. */
static void run_lumber_crowd(int footprint_cells, int *outside_range, int *present_walking, int *in_range) {
    enum { CELLS = 64 };
    static uint8_t pathmap[CELLS * CELLS];
    edict_t *workers[LUMBER_CROWD_WORKERS];
    edict_t *tree;
    float const saved_range = HARVEST_RANGE;

    setup_test_world();
    memset(pathmap, 0, sizeof(pathmap));
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-1024.0f, -1024.0f},
                                           .max = { 1024.0f,  1024.0f}));
    tree = make_crowd_tree(0.0f, 0.0f, footprint_cells);
    for (int i = 0; i < LUMBER_CROWD_WORKERS; i++)
        workers[i] = make_crowd_worker(-200.0f + (float)i * 14.0f, -30.0f + (float)i * 12.0f);
    CM_BakeStaticObstacles();

    HARVEST_RANGE = (float)LUMBER_CROWD_RANGE;
    for (int i = 0; i < LUMBER_CROWD_WORKERS; i++) harvest_start(workers[i], tree);
    FOR_LOOP(tick, 160) {
        for (int i = 0; i < LUMBER_CROWD_WORKERS; i++) {
            if (workers[i]->currentmove && workers[i]->currentmove->think)
                workers[i]->currentmove->think(workers[i]);
        }
    }

    *outside_range = 0;
    *present_walking = 0;
    *in_range = 0;
    for (int i = 0; i < LUMBER_CROWD_WORKERS; i++) {
        float const distance = Vector2_distance(&workers[i]->s.origin2, &tree->s.origin2);
        if (distance > (float)LUMBER_CROWD_RANGE + 0.5f) {
            (*outside_range)++;
            fprintf(stderr, "WC3_LUMBER_CROWD tree=%dx%d worker=%d pos=(%.1f,%.1f) distance=%.1f anim=%s\n",
                    footprint_cells, footprint_cells, i,
                    workers[i]->s.origin2.x, workers[i]->s.origin2.y, distance,
                    workers[i]->animation_request ? workers[i]->animation_request : "<none>");
        }
        if (workers[i]->animation_request && !strcmp(workers[i]->animation_request, "walk"))
            (*present_walking)++;
        if (distance <= (float)LUMBER_CROWD_RANGE + 0.5f)
            (*in_range)++;
    }
    HARVEST_RANGE = saved_range;
}

/* A normal tree footprint: most of the group reaches chop range, and nobody is
 * left presenting the walk cycle in place outside range. */
TEST(wc3_lumber_crowd, group_ordered_to_one_tree_reaches_chop_range) {
    int outside_range, present_walking, in_range;
    run_lumber_crowd(2, &outside_range, &present_walking, &in_range);
    T_EQ(present_walking, 0);
    T_ASSERT(in_range >= 4);
    /* The remaining workers wait in the stand pose for a free chop slot. */
    T_ASSERT(outside_range <= LUMBER_CROWD_WORKERS - 4);
}

/* A large footprint leaves room for only a few chop positions.  Extra workers
 * may have to wait, but they must wait in the stand pose, not walk in place
 * forever outside range. */
TEST(wc3_lumber_crowd, crowded_tree_large_footprint_waits_without_marching) {
    int outside_range, present_walking, in_range;
    run_lumber_crowd(4, &outside_range, &present_walking, &in_range);
    T_EQ(present_walking, 0);
    T_ASSERT(in_range >= 1);
    (void)outside_range;
}

/* A group that cannot all fit around the clicked tree must be reassigned to
 * adjacent reachable trees instead of waiting at the crowded one forever. */
TEST(wc3_lumber_crowd, crowded_tree_spreads_group_to_adjacent_trees) {
    enum { CELLS = 64, TREES = 3 };
    static uint8_t pathmap[CELLS * CELLS];
    edict_t *workers[LUMBER_CROWD_WORKERS];
    edict_t *trees[TREES];
    int distinct_goals = 0;
    int present_walking = 0;
    float const saved_range = HARVEST_RANGE;
    float const saved_search_range = HARVEST_SEARCH_RANGE;

    setup_test_world();
    memset(pathmap, 0, sizeof(pathmap));
    CM_SetupTestPathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {-1024.0f, -1024.0f},
                                           .max = { 1024.0f,  1024.0f}));
    trees[0] = make_crowd_tree(0.0f, 0.0f, 2);
    trees[1] = make_crowd_tree(0.0f, -220.0f, 2);
    trees[2] = make_crowd_tree(0.0f, -440.0f, 2);
    for (int i = 0; i < LUMBER_CROWD_WORKERS; i++)
        workers[i] = make_crowd_worker(-200.0f + (float)i * 14.0f, -30.0f + (float)i * 12.0f);
    CM_BakeStaticObstacles();

    HARVEST_RANGE = (float)LUMBER_CROWD_RANGE;
    HARVEST_SEARCH_RANGE = 600.0f; /* the harvest command fills this from AbilityData */
    for (int i = 0; i < LUMBER_CROWD_WORKERS; i++) harvest_start(workers[i], trees[0]);

    /* The group is spread up front: extras never even target the crowded tree. */
    {
        int immediate = 0;
        for (int t = 0; t < TREES; t++) {
            int targeted = 0;
            for (int i = 0; i < LUMBER_CROWD_WORKERS; i++)
                if (workers[i]->orders && workers[i]->orders->resource_goal == trees[t])
                    targeted++;
            if (targeted) immediate++;
        }
        T_ASSERT(immediate >= 2);
    }

    FOR_LOOP(tick, 320) {
        for (int i = 0; i < LUMBER_CROWD_WORKERS; i++) {
            if (workers[i]->currentmove && workers[i]->currentmove->think)
                workers[i]->currentmove->think(workers[i]);
        }
    }

    for (int t = 0; t < TREES; t++) {
        int targeted = 0;
        for (int i = 0; i < LUMBER_CROWD_WORKERS; i++)
            if (workers[i]->orders && workers[i]->orders->resource_goal == trees[t])
                targeted++;
        if (targeted) distinct_goals++;
    }
    for (int i = 0; i < LUMBER_CROWD_WORKERS; i++)
        if (workers[i]->animation_request && !strcmp(workers[i]->animation_request, "walk"))
            present_walking++;

    T_ASSERT(distinct_goals >= 2);
    T_EQ(present_walking, 0);

    HARVEST_RANGE = saved_range;
    HARVEST_SEARCH_RANGE = saved_search_range;
}

#endif /* BZ_TESTS */