#ifdef BZ_TESTS
/*
 * test_pathfinding.c — Unit tests for routing.c (heatmap / flow-field).
 *
 * Uses setup_test_pathmap() to build synthetic pathmaps without an MPQ
 * archive, then calls CM_BuildHeatmap() and get_flow_direction() directly.
 *
 * Test areas:
 *   Heatmap cache — same goal returns cached generation; different goal
 *                   triggers a rebuild (generation advances).
 *   Static obstacles — nowalk cells are avoided; the heatmap never
 *                      propagates into a wall cell.
 *   Unit obstacles separation — a live unit does NOT invalidate the heatmap
 *                               cache for the same goal (fix #2: unit
 *                               obstacles are handled by collision, not by
 *                               the heatmap).
 *   Multi-goal cache — two different goals each maintain their own cached
 *                      generation; switching between them does not force a
 *                      full rebuild every frame (fix #3).
 *   Flow direction — the flow vector at a cell points toward the goal.
 *   Static point test — CM_PointIsPathableForRadius rejects wall cells and
 *                       accepts open ground (the static half of move-time
 *                       collision; see unit_trymove in skills/s_move.c).
 */

#include <math.h>
#include <limits.h>
#include <string.h>
#include "test.h"
#include "../g_local.h"
#include "../common/wc3_pathing_masks.h"
#include "retail_map_load.h"
#include "retail_constructed_maps.h"
#include "retail_passages.h"
#include "retail_fine_queue.h"
#include "retail_fine_storage.h"
#include "retail_fine_results.h"
#include "../../common/wc3_pathing_adaptive.h"
#include "retail_adaptive_wrap.h"
#include "retail_adaptive_producer.h"

/* Helpers defined in t_utils.c */
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);



/* -----------------------------------------------------------------------
 * Symbols from routing.c / g_phys.c used directly by these tests.
 * --------------------------------------------------------------------- */

/* Defined in routing.c, only compiled for test builds. */
void setup_test_pathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void CM_SetupTestWorldBounds(box2_t const *bounds);

struct routePerfStats_s;
void CM_ResetTestPathPerfStats(void);
extern struct routePerfStats_s CM_GetTestPathPerfStats(void);

/* Public API from routing.c. */
uint32_t  CM_BuildHeatmap(edict_t *goalentity);
uint32_t  CM_BuildHeatmapForRadius(edict_t *goalentity, float radius);
uint32_t  CM_RequestHeatmapForRadius(edict_t *goalentity, float radius);
uint32_t  CM_RequestHeatmapForRadiusFlags(edict_t *goalentity, float radius, uint8_t blocked_flags);
void   CM_ProcessPathJobs(uint32_t work_budget);
bool   CM_ClosestPathablePointForRadius(vec2_t const *location, float radius, vec2_t *out);
bool   CM_ClosestPathablePointForRadiusFlags(vec2_t const *location, float radius, uint8_t blocked_flags, vec2_t *out);
bool   G_ClosestStaticPathablePointInRectForRadiusFlags(vec2_t const *location, box2_t const *bounds, float radius, uint8_t blocked_flags, vec2_t *out);
bool   CM_ClosestReachablePointForRadius(vec2_t const *from, vec2_t const *target, float radius, vec2_t *out);
bool   CM_ClosestReachablePointForRadiusFlags(vec2_t const *from, vec2_t const *target, float radius,
                                              uint8_t blocked_flags, vec2_t *out);
bool   CM_LineIsWalkableForRadius(vec2_t const *a, vec2_t const *b, float radius);
bool   CM_LineIsPathableForRadiusFlags(vec2_t const *a, vec2_t const *b, float radius, uint8_t blocked_flags);
bool   CM_FindDirectApproachPointForRadius(vec2_t const *from, vec2_t const *target, float range, float radius, vec2_t *out);
bool   CM_FindApproachPointToFootprintForRadius(edict_t const *target, vec2_t const *from, float range, float radius, vec2_t *out);
bool   CM_FindInnerApproachPointToFootprintForRadius(edict_t const *target, vec2_t const *from, float range, float radius, vec2_t *out);
bool   CM_FlowReachedGoal(uint32_t generation, float x, float y);
bool   CM_FlowCanReach(uint32_t generation, float x, float y);
vec2_t get_flow_direction(uint32_t heatmapindex, float fnx, float fny);

/* Static-map point test from routing.c — the static half of move-time
 * collision (unit_trymove in skills/s_move.c). */
bool CM_PointIsPathableForRadius(vec2_t const *location, float radius);
bool CM_PointIsPathableForRadiusFlags(vec2_t const *location, float radius, uint8_t blocked_flags);

/* From g_monster.c */
edict_t *Waypoint_add(vec2_t const *spot);
uint32_t M_RefreshHeatmap(edict_t *goal, float radius);

/* From s_move.c — needed to set up a moving unit. */
void order_move(edict_t *self, edict_t *target);
void order_patrol(edict_t *self, edict_t *target);
void order_attackmove(edict_t *self, edict_t *target);
bool M_MoveIsValid(edict_t *self, vec2_t const *pos);
void unit_changeangle(edict_t *self);

/* From m_unit.c */
void unit_stand(edict_t *self);

/* -----------------------------------------------------------------------
 * Test-world helpers
 *
 * The test pathmap is 8×8 cells.  World coordinates are 1:1 with cell
 * coordinates (cell_size = 1) because CM_GetNormalizedMapPosition and
 * CM_GetDenormalizedMapPosition return identity transforms when called
 * from game common/world_*.c stubs are absent — routing.c uses them
 * only to convert; the test harness's world bounds mock covers this.
 *
 * We use a 10×10 cell map with a wall column at x=5 to test obstacle
 * avoidance.
 * --------------------------------------------------------------------- */

#define MAP_W 10
#define MAP_H 10

/*  0 = open, 2 = nowalk (bit 1 set, matching pathMapCell_t.nowalk). */
static uint8_t open_map[MAP_W * MAP_H];   /* all open */

/* Wall column at x=5, rows 0-7, leaving rows 8-9 as a gap. */
static uint8_t wall_map[MAP_W * MAP_H];

/* Full wall column at x=5, splitting the map into two unreachable halves. */
static uint8_t split_map[MAP_W * MAP_H];

static void build_open_map(void) {
    memset(open_map, 0, sizeof(open_map));
}

static void build_wall_map(void) {
    memset(wall_map, 0, sizeof(wall_map));
    for (int y = 0; y < 8; y++) {
        wall_map[y * MAP_W + 5] = 2; /* nowalk */
    }
}

static void build_split_map(void) {
    memset(split_map, 0, sizeof(split_map));
    for (int y = 0; y < MAP_H; y++) {
        split_map[y * MAP_W + 5] = 2; /* nowalk */
    }
}

/* The test world maps one world unit to one pathmap cell. */
static edict_t *make_waypoint(float cell_x, float cell_y) {
    vec2_t pos = { cell_x, cell_y };
    return Waypoint_add(&pos);
}

/* Build the flow field for a goal and remember its generation so flow_at_cell
 * can pass the real handle (get_flow_direction now activates the field for that
 * generation rather than reading whatever was globally active). */
static uint32_t g_flow_gen = 0;
static uint32_t build_flow(edict_t *goal) {
    g_flow_gen = CM_BuildHeatmap(goal);
    return g_flow_gen;
}

/* Query flow direction at a cell in the one-unit-per-cell test world. */
static vec2_t flow_at_cell(float cell_x, float cell_y) {
    return get_flow_direction(g_flow_gen, cell_x, cell_y);
}

/* Make a minimal unit that looks "stopped" (no currentmove).
 * s.model is set to 1 so the entity is not treated as IS_HOLLOW by
 * G_SolveCollisions (which skips entities with model == 0). */
static edict_t *make_unit_at(float x, float y) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), x, y);
    /* Runtime units are dynamic occupants. In particular, freeing an idle
     * peer must not bake the surviving mover into the static terrain. */
    ent->svflags |= SVF_MONSTER;
    ent->movetype  = MOVETYPE_STEP;
    ent->collision = 16.0f;
    ent->s.model   = 1;
    ent->stand     = unit_stand;
    unit_stand(ent);
    return ent;
}

/* The live point command publishes a minimum range of .49 fine cells, checks
 * a separate .2-radian arrival tolerance, and stops at the predicted pose. */
unsigned G_TestStaticPathMask(unsigned x, unsigned y);
int G_TestMovePathClass(uint8_t mask, unsigned level, unsigned x, unsigned y);
void G_TestMovePathRefresh(point2_t,point2_t);
point2_t G_TestMovePathSize(unsigned level);
void CM_ReadPathMap(handle_t archive);

static void path_load_expand(unsigned const runs[][2], unsigned count, uint8_t *out, unsigned size) {
    unsigned at=0;
    FOR_LOOP(i,count) {
        T_ASSERT(runs[i][0]<=size-at);
        if(runs[i][0]>size-at) return;
        memset(out+at,runs[i][1],runs[i][0]); at+=runs[i][0];
    }
    T_EQ(at,size);
}

static void assert_retail_loaded_map(void) {
    uint8_t *fine=malloc(384*256), *classes=malloc(36462);
    path_load_expand(retail_load_fine_runs,sizeof(retail_load_fine_runs)/sizeof(*retail_load_fine_runs),fine,384*256);
    path_load_expand(retail_load_class_runs,sizeof(retail_load_class_runs)/sizeof(*retail_load_class_runs),classes,36462);
    FOR_LOOP(y,256) FOR_LOOP(x,384)
        T_EQ(G_TestStaticPathMask(x,y)&0xc6,fine[y*384+x]&0xc6);
    uint8_t const lanes[]={2,0x80,0x40,4};
    unsigned at=0;
    FOR_LOOP(level,4) {
        unsigned w=retail_load_size[level][0], h=retail_load_size[level][1];
        point2_t size=G_TestMovePathSize(level);
        T_EQ(size.x,w); T_EQ(size.y,h);
        FOR_LOOP(y,h) FOR_LOOP(x,w) {
            unsigned value=classes[at++];
            FOR_LOOP(lane,4) T_EQ(G_TestMovePathClass(lanes[lane],level,x,y),(value>>(6-2*lane))&3);
        }
    }
    free(fine); free(classes);
}

TEST(pathfinding, file_backed_wpm_matches_complete_retail_initial_hierarchy) {
    char const *path="/tmp/wc3-retail-wpm-load-test.mpq";
    unsigned count=384*256;
    uint8_t *file=malloc(count+16), *decoded=malloc(count);
    uint32_t header[]={0x5733504d,0,384,256};
    memcpy(file,header,sizeof(header));
    path_load_expand(retail_load_wpm_runs,sizeof(retail_load_wpm_runs)/sizeof(*retail_load_wpm_runs),file+16,count);
    FOR_LOOP(i,count) decoded[i]=wc3_wpm_movement_flags(file[16+i]);
    handle_t archive=NULL;
    remove(path);
    T_ASSERT(SFileCreateArchive(path,0,16,&archive));
    if(!archive) {free(file); free(decoded); return;}
    T_ASSERT(SFileAddFileFromBuffer(archive,"war3map.wpm",file,count+16));
    T_ASSERT(SFileCloseArchive(archive)); archive=NULL;
    T_ASSERT(SFileOpenArchive(path,0,0,&archive));
    if(!archive) {free(file); free(decoded); remove(path); return;}
    reset_entities(); setup_test_world();
    CM_SetupTestWorldBounds(&(box2_t){{-7168,-3072},{5120,5120}});
    CM_ReadPathMap(archive);
    assert_retail_loaded_map();
    T_ASSERT(SFileCloseArchive(archive)); remove(path);
    /* Compare the same decoded terrain through the no-file map adapter. */
    CM_SetupTestPathmap(384,256,decoded);
    assert_retail_loaded_map();
    /* Original base ground mask06000006 differs from individual fine mask2. */
    uint8_t no_fly[16]={4};
    CM_SetupTestPathmap(4,4,no_fly);
    T_EQ(G_TestStaticPathMask(0,0)&2,0);
    T_EQ(G_TestMovePathClass(2,0,0,0),2);
    T_EQ(G_TestMovePathClass(4,0,0,0),2);
    T_EQ(G_TestMovePathClass(0x40,0,0,0),0);
    free(file); free(decoded);
    reset_entities(); setup_test_world();
}

vec2_t G_TestMoveWorldGrid(vec2_t point, bool inverse);

static void assert_constructed_classes(retailConstructedMap_t const *map, unsigned const range[2]) {
    unsigned total=0,at=0;
    FOR_LOOP(level,4) total+=map->dimensions[level+2][0]*map->dimensions[level+2][1];
    uint8_t *classes=malloc(total);
    path_load_expand(retail_constructed_runs+range[0],range[1],classes,total);
    uint8_t const lanes[]={2,0x80,0x40,4};
    FOR_LOOP(level,4) {
        unsigned w=map->dimensions[level+2][0],h=map->dimensions[level+2][1];
        point2_t size=G_TestMovePathSize(level);
        T_EQ(size.x,w); T_EQ(size.y,h);
        FOR_LOOP(y,h) FOR_LOOP(x,w) {
            unsigned value=classes[at++];
            FOR_LOOP(lane,4) T_EQ(G_TestMovePathClass(lanes[lane],level,x,y),(value>>(6-2*lane))&3);
        }
    }
    free(classes);
}

TEST(pathfinding, constructed_negative_uneven_maps_match_retail_corners_padding_and_reversal) {
    reset_entities(); setup_test_world();
    FOR_LOOP(m,sizeof(retail_constructed_maps)/sizeof(*retail_constructed_maps)) {
        retailConstructedMap_t const *map=retail_constructed_maps+m;
        unsigned w=map->dimensions[1][0],h=map->dimensions[1][1];
        uint8_t *cells=calloc(w*h,1);
        box2_t bounds={{wc3_float(map->bounds[0]),wc3_float(map->bounds[1])},
                       {wc3_float(map->bounds[2]),wc3_float(map->bounds[3])}};
        CM_SetupTestWorldBounds(&bounds); CM_SetupTestPathmap(w,h,cells);
        assert_constructed_classes(map,map->initial);
        FOR_LOOP(i,100) {
            retailMapCorner_t const *row=retail_constructed_corners[m]+i;
            vec2_t point={wc3_float(row->words[0]),wc3_float(row->words[1])};
            vec2_t grid=G_TestMoveWorldGrid(point,false),world=G_TestMoveWorldGrid(grid,true);
            T_EQ(wc3_float_bits(grid.x),row->words[2]); T_EQ(wc3_float_bits(grid.y),row->words[3]);
            T_EQ(wc3_int_bits(wc3_floor_bits(wc3_float_bits(grid.x))),row->words[4]);
            T_EQ(wc3_int_bits(wc3_floor_bits(wc3_float_bits(grid.y))),row->words[5]);
            T_EQ(wc3_float_bits(world.x),row->words[6]); T_EQ(wc3_float_bits(world.y),row->words[7]);
            uint8_t flags=0xa5;
            T_EQ(G_GetTerrainPathingFlags(&point,&flags),row->index>=0);
            T_EQ(flags,row->index>=0 ? 0 : 0xa5);
            terrainPathingEdit_t edit={point,2,true};
            T_EQ(G_SetTerrainPathingFlags(&edit),row->index>=0);
            if(row->index>=0) {
                T_ASSERT(G_GetTerrainPathingFlags(&point,&flags)); T_EQ(flags,2);
                T_EQ(G_TestStaticPathMask(row->index%w,row->index/w)&0xc6,2);
            }
            edit.blocked=false;
            T_EQ(G_SetTerrainPathingFlags(&edit),row->index>=0);
        }
        FOR_LOOP(y,h) FOR_LOOP(x,w) T_EQ(G_TestStaticPathMask(x,y)&0xc6,0);
        assert_constructed_classes(map,map->initial);
        FOR_LOOP(i,16) {
            uint32_t const *e=retail_constructed_edits[m][i];
            terrainPathingEdit_t edit={{wc3_float(e[0]),wc3_float(e[1])},e[2],true};
            T_ASSERT(G_SetTerrainPathingFlags(&edit));
        }
        path_load_expand(retail_constructed_runs+map->fine[0],map->fine[1],cells,w*h);
        FOR_LOOP(y,h) FOR_LOOP(x,w) T_EQ(G_TestStaticPathMask(x,y)&0xc6,cells[y*w+x]);
        assert_constructed_classes(map,map->initial);
        G_TestMovePathRefresh((point2_t){0,0},(point2_t){w,h});
        assert_constructed_classes(map,map->edited);
        for(int i=15;i>=0;i--) {
            uint32_t const *e=retail_constructed_edits[m][i];
            terrainPathingEdit_t edit={{wc3_float(e[0]),wc3_float(e[1])},e[2],false};
            T_ASSERT(G_SetTerrainPathingFlags(&edit));
        }
        FOR_LOOP(y,h) FOR_LOOP(x,w) T_EQ(G_TestStaticPathMask(x,y)&0xc6,0);
        assert_constructed_classes(map,map->edited);
        G_TestMovePathRefresh((point2_t){0,0},(point2_t){w,h});
        assert_constructed_classes(map,map->initial);
        free(cells);
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_pathfinding, point_move_stops_in_range_without_snapping) {
    vec2_t target = {138.f, 128.f};
    reset_entities();
    setup_test_world();
    edict_t *unit = make_unit_at(128.f, 128.f);
    unit->unitinfo.MoveSpeed = 100.f;
    gi.LinkEntity(unit);
    T_ASSERT(unit_issueorder(unit, "move", &target));
    T_EQ(unit->current_order_id, 851986);
    unit->currentmove->think(unit);
    T_FEQ(unit->s.origin2.x, 128.f, .00001f);
    T_FEQ(unit->s.origin2.y, 128.f, .00001f);
    T_FEQ(unit->movement.velocity.x, 0.f, .00001f);
    T_EQ(unit->current_order_id, 0);
    reset_entities();
    setup_test_world();
}

TEST(wc3_pathfinding, point_move_arrival_heading_is_stricter_than_propwindow) {
    vec2_t target = {138.f, 128.f};
    uint32_t old_time = level.time;
    reset_entities();
    setup_test_world();
    edict_t *unit = make_unit_at(128.f, 128.f);
    unit->unitinfo.MoveSpeed = 100.f;
    unit->unitinfo.TurnSpeed = .001f;
    unit->unitinfo.move_flags |= BZ_UNIT_TURN_SET;
    unit->s.angle = .21f;
    gi.LinkEntity(unit);
    T_ASSERT(unit_issueorder(unit, "move", &target));
    unit->currentmove->think(unit);
    T_EQ(unit->current_order_id, 851986);
    T_FEQ(unit->s.origin2.x, 128.f, .00001f);
    T_FEQ(unit->movement.velocity.x, 0.f, .00001f);
    T_ASSERT(unit->s.angle < .21f);
    for (int tick = 0; tick < 50 && unit->current_order_id; tick++) {
        level.time += FRAMETIME;
        unit->currentmove->think(unit);
    }
    T_EQ(unit->current_order_id, 0);
    /* Original vector heading for (.3125, 0) is 3ba9540a, a small
     * software-math residual; arrival compares against that bearing. */
    T_ASSERT(fabsf(unit->s.angle - .005167489f) <= .2f);
    T_FEQ(unit->s.origin2.x, 128.f, .00001f);
    level.time = old_time;
    reset_entities();
    setup_test_world();
}

TEST(wc3_pathfinding, point_move_arrival_commits_previous_velocity_then_stops) {
    vec2_t target = {155.f, 128.f};
    uint32_t old_time = level.time;
    reset_entities();
    setup_test_world();
    edict_t *unit = make_unit_at(128.f, 128.f);
    unit->unitinfo.MoveSpeed = 100.f;
    gi.LinkEntity(unit);
    T_ASSERT(unit_issueorder(unit, "move", &target));
    unit->currentmove->think(unit);
    T_EQ(unit->current_order_id, 851986);
    T_FEQ(unit->s.origin2.x, 138.f, .001f);
    level.time += FRAMETIME;
    unit->currentmove->think(unit);
    T_EQ(unit->current_order_id, 0);
    T_FEQ(unit->s.origin2.x, 148.f, .001f);
    T_FEQ(unit->movement.velocity.x, 0.f, .00001f);
    level.time += FRAMETIME;
    if (unit->currentmove->think) unit->currentmove->think(unit);
    T_FEQ(unit->s.origin2.x, 148.f, .001f);
    level.time = old_time;
    reset_entities();
    setup_test_world();
}

/* Save immediately before the final velocity step with a pending successor.
 * Restoring must reproduce the same pose/heading/velocity words and FIFO handoff. */
TEST(wc3_pathfinding, point_move_arrival_replays_saved_velocity_and_queued_successor) {
    vec2_t target = {155.f, 128.f}, next = {188.f, 128.f};
    cstring_t file = "/tmp/openwarcraft3-point-arrival-save.bin";
    reset_entities();
    setup_test_world();
    edict_t *unit = make_unit_at(128.f, 128.f);
    unit->unitinfo.MoveSpeed = 100.f; unit->svflags |= SVF_MONSTER;
    gi.LinkEntity(unit);
    T_ASSERT(unit_issueorder(unit, "move", &target));
    unit->currentmove->think(unit);
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &next, true, 0, 0));
    T_ASSERT(WriteGame(file));
    unit->currentmove->think(unit);
    float expected[5] = {unit->s.origin2.x, unit->s.origin2.y, unit->s.angle,
                         unit->movement.velocity.x, unit->movement.velocity.y};
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_EQ(unit->current_order_id, 851986);
    T_FEQ(unit->s.origin2.x, 148.f, .001f);
    T_FEQ(unit->goalentity->s.origin2.x, next.x, .00001f);
    T_ASSERT(ReadGame(file));
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    unit->currentmove->think(unit);
    float actual[5] = {unit->s.origin2.x, unit->s.origin2.y, unit->s.angle,
                       unit->movement.velocity.x, unit->movement.velocity.y};
    T_EQ(memcmp(actual, expected, sizeof(actual)), 0);
    T_EQ(G_UnitQueuedOrderCount(unit), 0);
    T_EQ(unit->current_order_id, 851986);
    T_FEQ(unit->goalentity->s.origin2.x, next.x, .00001f);
    T_ASSERT(unit_issueimmediateorder(unit, "stop"));
    T_EQ(unit->current_order_id, 0);
    T_FEQ(unit->s.origin2.x, 148.f, .001f);
    remove(file);
    reset_entities();
    setup_test_world();
}

/* -----------------------------------------------------------------------
 * Cache tests
 * --------------------------------------------------------------------- */

/* The game initializer and flag/radius queries must share routing storage, never the executable's client cells. */
TEST(wc3_pathfinding, terrain_flags_and_routing_share_game_storage) {
    uint8_t cells[] = { 2, 0, 0, 0 }, flags = 0;
    vec2_t point = { 0.5f, 0.5f };
    setup_test_pathmap(2, 2, cells);
    T_ASSERT(CM_GetPathingFlagsAt(&point, &flags)); T_EQ(flags, 2);
    T_ASSERT(!CM_PointIsPathableForRadius(&point, 0));
    cells[0] = 0;
    setup_test_pathmap(2, 2, cells);
    T_ASSERT(CM_GetPathingFlagsAt(&point, &flags)); T_EQ(flags, 0);
    T_ASSERT(CM_PointIsPathableForRadius(&point, 0));
    setup_test_world();
}

/* Original04d870 on the96-cell map keeps raw435fffff (just before224)
 * in cell6. Normalizing through the whole width rounds it into blocked cell7. */
TEST(wc3_pathfinding, world_cell_boundary_keeps_original_side_and_point_words) {
    uint32_t const old_time = level.time;
    reset_entities(); setup_test_world();
    uint8_t cells[96 * 96] = {0};
    cells[16 * 96 + 7] = CM_PATHING_UNWALKABLE;
    setup_test_pathmap(96, 96, cells);
    CM_SetupTestWorldBounds(&(box2_t){ .min = {0, 0}, .max = {3072, 3072} });
    vec2_t const before = {wc3_float(0x435fffffu), 528};
    vec2_t const edge = {224, 528}, after = {wc3_float(0x43600001u), 528};
    vec2_t corrected;
    pathAccelParams_t query = { .from = &before, .blocked_flags = CM_PATHING_UNWALKABLE };
    T_ASSERT(G_MovePathPointIsPathable(&query));
    T_ASSERT(G_ClosestMovePathPoint(&query, &corrected));
    T_EQ(wc3_float_bits(corrected.x), wc3_float_bits(before.x));
    T_EQ(wc3_float_bits(corrected.y), wc3_float_bits(before.y));
    query.from = &edge; T_ASSERT(!G_MovePathPointIsPathable(&query));
    query.from = &after; T_ASSERT(!G_MovePathPointIsPathable(&query));
    box2_t const rectangle = { .min = {before.x, 520}, .max = {224, 530} };
    T_ASSERT(G_ClosestStaticPathablePointInRectForRadiusFlags(&before, &rectangle, 0, CM_PATHING_UNWALKABLE, &corrected));
    T_EQ(wc3_float_bits(corrected.x), wc3_float_bits(before.x));
    T_EQ(wc3_float_bits(corrected.y), wc3_float_bits(before.y));
    edict_t *unit = make_unit_at(before.x, before.y);
    unit->collision = 0; unit->unitinfo.MoveSpeed = 100; unit->s.angle = M_PI;
    gi.LinkEntity(unit);
    T_ASSERT(unit_issueorder(unit, "move", &(vec2_t){160, 528}));
    level.time += FRAMETIME; unit->currentmove->think(unit);
    T_ASSERT(unit->s.origin2.x < before.x);
    T_EQ(unit->current_order_id, G_OrderId("move"));
    reset_entities(); setup_test_world();
    uint8_t restricted[23 * 23]; memset(restricted, CM_PATHING_UNWALKABLE, sizeof(restricted));
    restricted[4 * 23 + 3] = 0;
    setup_test_pathmap(23, 23, restricted);
    CM_SetupTestWorldBounds(&(box2_t){ .min = {0, 0}, .max = {736, 736} });
    vec2_t const blocked = {144, 144};
    query.from = &blocked;
    T_ASSERT(G_ClosestMovePathPoint(&query, &corrected));
    /* Original scalar inverse of fine(3.5,4.5), scale32, origin0. */
    T_EQ(wc3_float_bits(corrected.x), 0x42e00000u);
    T_EQ(wc3_float_bits(corrected.y), 0x43100000u);
    level.time = old_time;
    reset_entities(); setup_test_world();
}

TEST(wc3_pathfinding, movement_class_pathing_distinguishes_walk_and_fly_bits) {
    uint8_t cells[10 * 10] = { 0 };
    vec2_t nowalk = { 4.5f, 3.5f };
    vec2_t nofly = { 4.5f, 6.5f };
    vec2_t walk_from = { 1.5f, 3.5f }, walk_to = { 8.5f, 3.5f };
    vec2_t fly_from = { 1.5f, 6.5f }, fly_to = { 8.5f, 6.5f };

    cells[3 * 10 + 4] = CM_PATHING_UNWALKABLE;
    cells[6 * 10 + 4] = CM_PATHING_UNFLYABLE;
    setup_test_pathmap(10, 10, cells);

    T_ASSERT(!CM_PointIsPathableForRadiusFlags(&nowalk, 0.0f, CM_PATHING_UNWALKABLE));
    T_ASSERT(CM_PointIsPathableForRadiusFlags(&nowalk, 0.0f, CM_PATHING_UNFLYABLE));
    T_ASSERT(CM_PointIsPathableForRadiusFlags(&nofly, 0.0f, CM_PATHING_UNWALKABLE));
    T_ASSERT(!CM_PointIsPathableForRadiusFlags(&nofly, 0.0f, CM_PATHING_UNFLYABLE));

    T_ASSERT(!CM_LineIsPathableForRadiusFlags(&walk_from, &walk_to, 0.0f, CM_PATHING_UNWALKABLE));
    T_ASSERT(CM_LineIsPathableForRadiusFlags(&walk_from, &walk_to, 0.0f, CM_PATHING_UNFLYABLE));
    T_ASSERT(CM_LineIsPathableForRadiusFlags(&fly_from, &fly_to, 0.0f, CM_PATHING_UNWALKABLE));
    T_ASSERT(!CM_LineIsPathableForRadiusFlags(&fly_from, &fly_to, 0.0f, CM_PATHING_UNFLYABLE));
}

TEST(wc3_pathfinding, static_path_texture_green_channel_marks_unflyable) {
    uint8_t cells[8 * 8] = { 0 }, flags = 0;
    vec2_t center = { 4.5f, 4.5f };
    edict_t *building;
    pathTex_t *pathtex;

    setup_test_pathmap(8, 8, cells);
    reset_entities();
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), center.x, center.y);
    pathtex = gi.MemAlloc(sizeof(*pathtex) + sizeof(color32_t));
    T_NOT_NULL(pathtex);
    pathtex->width = 1;
    pathtex->height = 1;
    pathtex->map[0] = (color32_t){ .g = 255, .a = 255 };
    building->pathtex = pathtex;

    CM_BakeStaticObstacles();

    T_ASSERT(CM_GetPathingFlagsAt(&center, &flags));
    T_ASSERT(flags & CM_PATHING_UNFLYABLE);
    T_ASSERT(CM_PointIsPathableForRadiusFlags(&center, 0.0f, CM_PATHING_UNWALKABLE));
    T_ASSERT(!CM_PointIsPathableForRadiusFlags(&center, 0.0f, CM_PATHING_UNFLYABLE));

    building->pathtex = NULL;
    gi.MemFree(pathtex);
}

TEST(wc3_pathfinding, flyer_move_validation_uses_unflyable_static_pathing) {
    uint8_t cells[8 * 8] = { 0 };
    vec2_t target = { 4.5f, 4.5f };
    edict_t *flyer;

    cells[4 * 8 + 4] = CM_PATHING_UNWALKABLE;
    setup_test_pathmap(8, 8, cells);
    reset_entities();
    flyer = make_unit_at(3.5f, 4.5f);
    flyer->collision = 0.0f;
    flyer->aiflags |= AI_FLYING;
    T_ASSERT(M_MoveIsValid(flyer, &target));

    cells[4 * 8 + 4] = CM_PATHING_UNFLYABLE;
    setup_test_pathmap(8, 8, cells);
    T_ASSERT(!M_MoveIsValid(flyer, &target));
}

/* Original public stock profiles publish foot/horse/hover=2, float=64,
 * amph=128, fly=4. Exercise the same Move validation and routing entry points. */
TEST(wc3_pathfinding, authored_water_and_amphibious_masks_reach_move_queries) {
    static cstring_t const names[] = { "foot", "horse", "hover", "float", "amph", "fly" };
    static uint8_t const masks[] = { 2, 2, 2, 64, 128, 4 };
    static uint8_t const bits[] = { 2, 4, 64, 128 };
    vec2_t point = {8.5f, 4.5f};
    FOR_LOOP(i, sizeof(names) / sizeof(*names)) {
        reset_entities();
        setup_test_world();
        edict_t *unit = make_unit_at(4.5f, 4.5f);
        UnitData_t const *original = unit->data.UnitData;
        UnitData_t data = *original;
        data.moveTypeName = names[i];
        unit->data.UnitData = &data;
        unit->collision = 0.5f;
        if (masks[i] == 4) unit->aiflags |= AI_FLYING;
        gi.LinkEntity(unit);
        T_EQ(M_UnitStaticPathingFlags(unit), masks[i]);
        FOR_LOOP(j, sizeof(bits) / sizeof(*bits)) {
            uint8_t cells[16 * 16] = {0};
            cells[4 * 16 + 8] = bits[j];
            setup_test_pathmap(16, 16, cells);
            T_EQ(M_MoveIsValid(unit, &point), masks[i] != bits[j]);
        }
        uint8_t cells[16 * 16] = {0};
        for (unsigned y = 3; y < 6; y++) cells[y * 16 + 8] = masks[i];
        setup_test_pathmap(16, 16, cells);
        vec2_t target = {12.5f, 4.5f};
        unit->unitinfo.MoveSpeed = 2.f;
        T_ASSERT(unit_issueorder(unit, "move", &target));
        unit_changeangle(unit);
        T_ASSERT(!unit->movement.flow_direct);
        T_ASSERT(unit->movement.path.valid);
        /* The raw first successor can be straight; the retained chain owns the detour. */
        bool detour = false;
        FOR_LOOP(k,unit->movement.fine_route.count)
            detour |= fabsf(unit->movement.fine_route.points[k].y - 4.5f) > 0.01f;
        T_ASSERT(detour);
        unit->data.UnitData = original;
    }
    reset_entities();
    setup_test_world();
}

/* Original widget blue coverage creates categoryc2: walk/float/amph.
 * It must survive baking and release through the existing footprint lifetime. */
TEST(wc3_pathfinding, baked_widget_blocks_water_lanes_and_release_restores_them) {
    uint8_t cells[16 * 16] = {0};
    vec2_t point = {8.5f, 8.5f};
    reset_entities();
    setup_test_world();
    setup_test_pathmap(16, 16, cells);
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), point.x, point.y);
    pathTex_t *texture = gi.MemAlloc(sizeof(*texture) + sizeof(color32_t));
    texture->width = texture->height = 1;
    texture->map[0] = (color32_t){.b = 255};
    building->pathtex = texture;
    CM_BakeStaticObstacles();
    T_ASSERT(!CM_PointIsPathableForRadiusFlags(&point, 0, 2));
    T_ASSERT(!CM_PointIsPathableForRadiusFlags(&point, 0, 64));
    T_ASSERT(!CM_PointIsPathableForRadiusFlags(&point, 0, 128));
    T_ASSERT(CM_PointIsPathableForRadiusFlags(&point, 0, 4));
    building->pathtex = NULL;
    G_FreeEdict(building);
    gi.MemFree(texture);
    CM_BakeStaticObstacles();
    T_ASSERT(CM_PointIsPathableForRadiusFlags(&point, 0, 2));
    T_ASSERT(CM_PointIsPathableForRadiusFlags(&point, 0, 64));
    T_ASSERT(CM_PointIsPathableForRadiusFlags(&point, 0, 128));
    reset_entities();
    setup_test_world();
}

TEST(wc3_pathfinding, command_destination_uses_object_category_for_water_queries) {
    uint8_t cells[16 * 16] = {0};
    vec2_t point = {8.5f, 8.5f}, out;
    reset_entities();
    setup_test_world();
    setup_test_pathmap(16, 16, cells);
    edict_t *idle = make_unit_at(point.x, point.y);
    idle->collision = 0.5f;
    idle->svflags |= SVF_MONSTER;
    gi.LinkEntity(idle);
    static uint8_t const masks[] = {2, 64, 128};
    FOR_LOOP(i, sizeof(masks) / sizeof(*masks)) {
        T_ASSERT(CM_ClosestPathablePointForRadiusFlags(&point, 0, masks[i], &out));
        T_ASSERT(Vector2_distance(&point, &out) > 0.5f);
    }
    idle->aiflags |= AI_FLYING;
    T_ASSERT(CM_ClosestPathablePointForRadiusFlags(&point, 0, 4, &out));
    T_EQ(out.x, point.x); T_EQ(out.y, point.y);
    reset_entities();
    setup_test_world();
}

TEST(wc3_pathfinding, heatmap_cache_separates_ground_and_flying_pathing) {
    uint8_t cells[10 * 10] = { 0 };
    edict_t *goal;
    uint32_t fly_gen, ground_gen;

    for (int y = 0; y < 10; y++)
        cells[y * 10 + 4] = CM_PATHING_UNWALKABLE;
    setup_test_pathmap(10, 10, cells);
    reset_entities();
    goal = make_waypoint(8.5f, 5.5f);

    T_EQ(CM_RequestHeatmapForRadiusFlags(goal, 0.0f, CM_PATHING_UNFLYABLE), 0);
    CM_ProcessPathJobs(UINT_MAX);
    fly_gen = CM_RequestHeatmapForRadiusFlags(goal, 0.0f, CM_PATHING_UNFLYABLE);
    T_ASSERT(fly_gen != 0);
    T_ASSERT(CM_FlowCanReach(fly_gen, 1.5f, 5.5f));

    T_EQ(CM_RequestHeatmapForRadiusFlags(goal, 0.0f, CM_PATHING_UNWALKABLE), 0);
    CM_ProcessPathJobs(UINT_MAX);
    ground_gen = CM_RequestHeatmapForRadiusFlags(goal, 0.0f, CM_PATHING_UNWALKABLE);
    T_ASSERT(ground_gen != 0);
    T_NE(fly_gen, ground_gen);
    T_ASSERT(!CM_FlowCanReach(ground_gen, 1.5f, 5.5f));
}

TEST(wc3_pathfinding, heatmap_cache_hit_same_goal) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *wp = make_waypoint(5.0f, 5.0f);
    uint32_t gen1 = CM_BuildHeatmap(wp);
    uint32_t gen2 = CM_BuildHeatmap(wp);

    /* Same goal: generation must be identical — no rebuild. */
    T_EQ(gen1, gen2);
}

TEST(wc3_pathfinding, heatmap_cache_hit_same_target_different_waypoint) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *wp1 = make_waypoint(5.0f, 5.0f);
    edict_t *wp2 = make_waypoint(5.0f, 5.0f);
    uint32_t gen1 = CM_BuildHeatmap(wp1);
    uint32_t gen2 = CM_BuildHeatmap(wp2);

    T_ASSERT(wp1 != wp2);
    T_EQ(gen1, gen2);
}

TEST(wc3_pathfinding, heatmap_cache_perf_same_target_builds_once) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();
    CM_ResetTestPathPerfStats();

    edict_t *wp1 = make_waypoint(5.0f, 5.0f);
    edict_t *wp2 = make_waypoint(5.0f, 5.0f);
    CM_BuildHeatmap(wp1);
    CM_BuildHeatmap(wp2);

    struct routePerfStats_s stats = CM_GetTestPathPerfStats();
    T_EQ(stats.cache_misses, 1);
    T_EQ(stats.cache_hits, 1);
    T_EQ(stats.heatmap_iterations, MAP_W * MAP_H);
    T_EQ(stats.flow_cells_computed, 0);

    /* Route creation caches prices only; flow work is local to a query. */
    (void)get_flow_direction(CM_BuildHeatmap(wp1), 2.0f, 5.0f);
    stats = CM_GetTestPathPerfStats();
    T_ASSERT(stats.flow_cells_computed > 0);
    T_ASSERT(stats.flow_cells_computed <= 4);
}

TEST(wc3_pathfinding, heatmap_cache_miss_different_goal) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *wp1 = make_waypoint(2.0f, 2.0f);
    edict_t *wp2 = make_waypoint(7.0f, 7.0f);
    uint32_t gen1 = CM_BuildHeatmap(wp1);
    uint32_t gen2 = CM_BuildHeatmap(wp2);

    /* Different goals must produce different generations. */
    T_ASSERT(gen1 != gen2);
}

TEST(wc3_pathfinding, heatmap_generation_is_nonzero) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *wp = make_waypoint(3.0f, 3.0f);
    uint32_t gen = CM_BuildHeatmap(wp);

    T_ASSERT(gen != 0);
}

/* Static pathing rebuilds invalidate the cache while route entities may still
 * retain their old generation handle.  A replacement field must never recycle
 * that handle or CM_ActivateCachedFlow could bind the stale route to unrelated
 * post-build prices. */
TEST(wc3_pathfinding, invalidation_does_not_recycle_heatmap_generation) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *first = make_waypoint(2.0f, 2.0f);
    edict_t *second = make_waypoint(7.0f, 7.0f);
    uint32_t old_gen = CM_BuildHeatmap(first);

    T_ASSERT(old_gen != 0);
    CM_InvalidatePathCache();
    T_ASSERT(!CM_ActivateCachedFlow(old_gen));

    uint32_t new_gen = CM_BuildHeatmap(second);
    T_ASSERT(new_gen != 0);
    T_ASSERT(new_gen != old_gen);
    T_ASSERT(!CM_ActivateCachedFlow(old_gen));
    T_ASSERT(CM_ActivateCachedFlow(new_gen));
}

TEST(wc3_pathfinding, incremental_heatmap_serializes_cache_misses_without_losing_later_goal) {
    cmPathJobStatus_t job;
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();
    CM_ResetTestPathPerfStats();

    edict_t *first = make_waypoint(2.0f, 2.0f);
    edict_t *second = make_waypoint(7.0f, 7.0f);

    /* A cache miss starts work; a second destination joins the FIFO instead
     * of being forgotten while the first field owns the build slot. */
    T_EQ(CM_RequestHeatmapForRadius(first, 0.0f), 0);
    T_EQ(CM_RequestHeatmapForRadius(second, 0.0f), 0);
    CM_GetPathJobStatus(&job);
    T_EQ(job.pending_jobs, 1);

    for (int i = 0; i < 200; i++)
        CM_ProcessPathJobs(4);

    uint32_t first_gen = CM_RequestHeatmapForRadius(first, 0.0f);
    T_ASSERT(first_gen != 0);

    /* FIFO service completes the queued destination without requiring a
     * fresh miss from its caller. */
    uint32_t second_gen = CM_RequestHeatmapForRadius(second, 0.0f);
    T_ASSERT(second_gen != 0);
    T_ASSERT(second_gen != first_gen);
}

TEST(wc3_pathfinding, production_budget_completes_large_open_field_in_two_frames) {
    enum { WIDTH = 256, HEIGHT = 256 };
    static uint8_t open[WIDTH * HEIGHT];
    edict_t *goal;

    memset(open, 0, sizeof(open));
    setup_test_pathmap(WIDTH, HEIGHT, open);
    reset_entities();
    goal = make_waypoint(128.0f, 128.0f);

    T_EQ(CM_RequestHeatmapForRadius(goal, 0.0f), 0);
    CM_ProcessPathJobs(BZ_PATH_WORK_BUDGET);
    if (!CM_RequestHeatmapForRadius(goal, 0.0f))
        CM_ProcessPathJobs(BZ_PATH_WORK_BUDGET);
    T_ASSERT(CM_RequestHeatmapForRadius(goal, 0.0f) != 0);
}

TEST(wc3_pathfinding, heatmap_reuses_neighbor_pathability_queries) {
    enum { WIDTH = 256, HEIGHT = 256 };
    static uint8_t open[WIDTH * HEIGHT];
    struct routePerfStats_s stats;
    edict_t *goal;

    memset(open, 0, sizeof(open));
    setup_test_pathmap(WIDTH, HEIGHT, open);
    reset_entities();
    CM_ResetTestPathPerfStats();
    goal = make_waypoint(128.5f, 128.5f);

    T_ASSERT(CM_BuildHeatmapForRadius(goal, 2.0f) != 0);
    stats = CM_GetTestPathPerfStats();
    T_ASSERT(stats.heatmap_iterations > 0);
    /* A flood checks each of eight candidate neighbors once; diagonal corner
     * checks reuse the already computed cardinal-neighbor results. */
    T_ASSERT(stats.pathability_checks <= stats.heatmap_iterations * 8 + 1);
}

TEST(wc3_pathfinding, nearby_detour_accelerator_returns_clear_waypoint) {
    vec2_t from = {2.0f, 5.0f}, target = {7.0f, 5.0f}, waypoint;
    pathAccelParams_t params = { &from, &target, 0.0f, CM_PATHING_UNWALKABLE };

    build_wall_map();
    setup_test_pathmap(MAP_W, MAP_H, wall_map);
    T_ASSERT(!CM_LineIsWalkableForRadius(&from, &target, 0.0f));
    T_ASSERT(CM_FindPathWaypoint(&params, &waypoint));
    T_ASSERT(G_FindMovePathWaypoint(&params, &waypoint));
    pathAccelParams_t line = params; line.target = &waypoint;
    T_ASSERT(G_MovePathLineIsPathable(&line));
    T_ASSERT(waypoint.y > 7.0f);
}

/* Cell chains frozen from the original 1.27 DLL, verify_wc3_pathing_grid.py.
 * Keep the current line-legality adapter explicit; retail smoothing is separate. */
TEST(wc3_pathfinding, mover_detours_follow_retail_fine_routes) {
    /* Original167bf0/165e60 on these complete frozen chains; destination-
     * first selected indices11/6/2/0 from cases8/12/16/20 in segment fixture. */
    static vec2_t const selected[] = {{11.5f,10.5f}, {15.5f,13.5f}, {18.5f,17.5f}, {19.5f,19.5f}};
    uint8_t cells[24 * 24] = {0};
    vec2_t from = {4.5f, 4.5f}, target = {19.5f, 19.5f}, actual;
    pathAccelParams_t params = { &from, &target, 0, CM_PATHING_UNWALKABLE };

    for (int gap = 1; gap <= 4; gap++) {
        reset_entities();
        setup_test_world();
        memset(cells, 0, sizeof(cells));
        for (int y = 0; y < 24; y++) cells[y * 24 + 12] = y < 10 || y >= 10 + gap ? 2 : 0;
        setup_test_pathmap(24, 24, cells);
        vec2_t expected = selected[gap - 1];
        T_ASSERT(G_FindMovePathWaypoint(&params, &actual));
        T_FEQ(actual.x, expected.x, 0.001f);
        T_FEQ(actual.y, expected.y, 0.001f);
        edict_t *unit = make_unit_at(from.x, from.y), *wp = make_waypoint(target.x, target.y);
        unit->collision = 0;
        order_move(unit, wp);
        T_ASSERT(CM_BuildHeatmapForRadius(wp, 0));
        unit_changeangle(unit);
        if (CM_LineIsPathableForRadiusFlags(&from, &target, 0, CM_PATHING_UNWALKABLE)) {
            /* A clear corridor still enters retail's retained fine route. */
            T_ASSERT(!unit->movement.flow_direct);
            T_ASSERT(unit->movement.path.valid);
            T_ASSERT(unit->movement.fine_route.count>1);
            continue;
        }
        T_ASSERT(unit->movement.path.valid);
        T_FEQ(unit->movement.path.waypoint.x, 5.5f, 0.001f);
        T_FEQ(unit->movement.path.waypoint.y, 5.5f, 0.001f);
        /* Original168870 starts at count-2, before167bf0 lookahead. A ready
         * generic field must not replace that first point before .49-cell progress. */
        T_EQ(unit->movement.fine_route.index,unit->movement.fine_route.count-2);
        unit->s.origin.x += 0.1f;
        unit_changeangle(unit);
        T_ASSERT(unit->movement.path.valid);
        T_FEQ(unit->movement.path.waypoint.x, 5.5f, 0.001f);
        T_FEQ(unit->movement.path.waypoint.y, 5.5f, 0.001f);
    }
    reset_entities();
    setup_test_world();
}

TEST(wc3_pathfinding, distant_detour_skips_bounded_accelerator) {
    enum { WIDTH = 128, HEIGHT = 16 };
    static uint8_t open[WIDTH * HEIGHT];
    vec2_t from = {2.0f, 8.0f}, target = {100.0f, 8.0f}, waypoint;
    pathAccelParams_t params = { &from, &target, 0.0f, 0 };

    memset(open, 0, sizeof(open));
    setup_test_pathmap(WIDTH, HEIGHT, open);
    T_ASSERT(!CM_FindPathWaypoint(&params, &waypoint));
    T_ASSERT(!G_FindMovePathWaypoint(&params, &waypoint));
}

TEST(wc3_pathfinding, nearby_detour_accelerator_respects_collision_radius) {
    uint8_t narrow[MAP_W * MAP_H];
    vec2_t from = {2.0f, 5.0f}, target = {7.0f, 5.0f}, waypoint;
    pathAccelParams_t point = { &from, &target, 0.0f, CM_PATHING_UNWALKABLE };
    pathAccelParams_t wide = { &from, &target, 1.0f, CM_PATHING_UNWALKABLE };

    memset(narrow, 0, sizeof(narrow));
    FOR_LOOP(y, MAP_H) narrow[5 + y * MAP_W] = 0x02;
    narrow[5 + 5 * MAP_W] = 0;
    setup_test_pathmap(MAP_W, MAP_H, narrow);
    T_ASSERT(CM_FindPathWaypoint(&point, &waypoint));
    T_ASSERT(!CM_FindPathWaypoint(&wide, &waypoint));
    T_ASSERT(G_FindMovePathWaypoint(&point, &waypoint));
    T_ASSERT(!G_FindMovePathWaypoint(&wide, &waypoint));
}

/* Original 14ad50/16ee80 use widths 1/2/3/4 at radius .5/1/1.5 cells;
 * ceil(radius) around a centre requires wider corridors than retail. */
void G_TestMoveFinePopTrace(void (*trace)(void *,uint32_t const[10]),void *data);
typedef struct { unsigned count,stale,reopens; bool seen[1024]; } fineQueueTrace_t;
static void assert_retail_fine_pop(void *data,uint32_t const words[10]) {
    fineQueueTrace_t *trace=data;
    unsigned i=trace->count++;
    T_ASSERT(i<sizeof(retail_queue_pops)/sizeof(*retail_queue_pops));
    if(i>=sizeof(retail_queue_pops)/sizeof(*retail_queue_pops)) return;
    FOR_LOOP(k,10) T_EQ(words[k],retail_queue_pops[i][k]);
    if(words[2]!=words[3]) trace->stale++;
    else {
        T_ASSERT(words[1]<1024);
        if(words[1]<1024) { trace->reopens+=trace->seen[words[1]]; trace->seen[words[1]]=true; }
    }
}

TEST(pathfinding, full_fine_request_matches_retail_ties_reopening_stale_generations_and_work) {
    reset_entities(); setup_test_world();
    uint8_t cells[48*48];
    FOR_LOOP(y,48) FOR_LOOP(x,48) cells[y*48+x]=(retail_queue_rows[y]&(1ULL<<x)) ? 2 : 0;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{1536,1536}});
    CM_SetupTestPathmap(48,48,cells);
    vec2_t source={4.25f*32,4.75f*32},target={43.25f*32,43.75f*32},selected;
    movePathQuery_t query={.geometry={&source,&target,8,2},.units=true};
    /* Reuse the same production search/cache without clearing its backing. */
    FOR_LOOP(pass,2) {
        fineQueueTrace_t trace={0}; moveFineRoute_t route={0};
        G_TestMoveFinePopTrace(assert_retail_fine_pop,&trace);
        bool built=G_BuildUnitMoveLocalRoute(&query,&route,&selected);
        G_TestMoveFinePopTrace(NULL,NULL);
        T_ASSERT(built); T_EQ(trace.count,1068); T_EQ(trace.stale,190); T_EQ(trace.reopens,1);
        T_ASSERT(!route.partial); T_EQ(route.count,sizeof(retail_queue_route)/sizeof(*retail_queue_route));
        if(route.count==sizeof(retail_queue_route)/sizeof(*retail_queue_route)) FOR_LOOP(i,route.count) {
            T_EQ(wc3_float_bits(route.points[i].x),retail_queue_route[i][0]);
            T_EQ(wc3_float_bits(route.points[i].y),retail_queue_route[i][1]);
        }
        free(route.points);
    }
    reset_entities(); setup_test_world();
}

wc3FineSearch_t const *G_TestMoveFineSearch(void);
typedef struct { uint8_t const *cells; } fineStorageGraph_t;
static bool fine_storage_cell(void const *data, wc3FinePoint_t pos) {
    fineStorageGraph_t const *graph=data;
    return (uint32_t)pos.x<256 && (uint32_t)pos.y<256 && !(graph->cells[pos.y*256+pos.x]&2);
}
static uint8_t fine_storage_edges(void const *data, wc3FinePoint_t pos) {
    wc3FineSegment_t query={.cls=0,.cell=fine_storage_cell,.data=data};
    return wc3_fine_cell_edges(&query,pos);
}
static uint64_t fine_storage_node_hash(wc3FineSearch_t const *search) {
    uint64_t hash=UINT64_C(14695981039346656037);
    FOR_LOOP(i,search->count) {
        wc3FineNode_t const *n=search->nodes+i;
        uint32_t words[]={n->pos.x,n->pos.y,n->g,n->h,n->gen,(uint32_t)n->parent,n->state};
        FOR_LOOP(j,7) FOR_LOOP(k,4) hash=(hash^(uint8_t)(words[j]>>(k*8)))*UINT64_C(1099511628211);
    }
    return hash;
}
static void fine_storage_terrain(uint8_t *cells) {
    FOR_LOOP(y,256) FOR_LOOP(x,256) cells[y*256+x]=x>=192 || (abs(x-128)<=8 && abs(y-128)<=8) ? 2 : 0;
}

TEST(pathfinding, fine_capacity_failure_retains_partial_route_and_next_request_recovers) {
    wc3FineSearch_t *search=calloc(1,sizeof(*search));
    uint8_t *cells=malloc(256*256);
    wc3FineVector_t *points=malloc(BZ_WC3_FINE_NODES*sizeof(*points));
    T_ASSERT(search && cells && points);
    if (!search || !cells || !points) { free(search); free(cells); free(points); return; }
    fine_storage_terrain(cells); fineStorageGraph_t graph={cells};
    FOR_LOOP(i,sizeof(retail_fine_storage)/sizeof(*retail_fine_storage)) {
        retailFineStorage_t const *row=retail_fine_storage+i;
        wc3FineRequest_t request={.start={(int)row->source[0],(int)row->source[1]},
            .goal={(int)row->goal[0],(int)row->goal[1]},.width=256,.height=256,.budget=row->budget,
            .edges=fine_storage_edges,.data=&graph};
        int at=wc3_fine_search(search,&request);
        T_EQ(at>=0,row->result); T_EQ(search->count,row->nodes); T_EQ(search->pops,row->work);
        T_EQ(search->node_capacity,row->node_capacity); T_EQ(search->heap_capacity,row->heap_capacity);
        T_ASSERT(fine_storage_node_hash(search)==row->node_hash);
        if (i==1) {
            uint32_t count=search->count;
            T_EQ(wc3_fine_node(search,&request,request.start),0);
            T_EQ(wc3_fine_node(search,&request,(wc3FinePoint_t){256,0}),-1);
            T_EQ(search->count,count);
        }
        bool complete=at>=0;
        if (!complete) at=(int)search->nearest;
        wc3FineReconstruct_t route={search->nodes,search->count,at,{row->source[0],row->source[1]},
            complete ? (wc3FineVector_t){row->goal[0],row->goal[1]} : wc3_route_center(search->nodes[at].pos)};
        uint32_t count=wc3_fine_reconstruct(&route,points,BZ_WC3_FINE_NODES);
        T_EQ(count,row->points);
        if (count==row->points) FOR_LOOP(j,count) {
            T_EQ(wc3_float_bits(points[j].x),row->route[j][0]); T_EQ(wc3_float_bits(points[j].y),row->route[j][1]);
        }
    }
    wc3_fine_free(search); T_ASSERT(!search->nodes && !search->heap);
    T_EQ(search->node_capacity,0); T_EQ(search->heap_capacity,0);
    free(search); free(cells); free(points);
}

TEST(pathfinding, production_fine_storage_grows_and_survives_partial_then_short_routes) {
    reset_entities(); setup_test_world(); G_FreeMovePathCache();
    uint8_t *cells=malloc(256*256); T_ASSERT(cells);
    if (!cells) return;
    fine_storage_terrain(cells);
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{8192,8192}}); CM_SetupTestPathmap(256,256,cells);
    FOR_LOOP(pass,2) FOR_LOOP(k,3) {
        retailFineStorage_t const *row=retail_fine_storage+(k ? k+1 : 0);
        vec2_t source={row->source[0]*32,row->source[1]*32},target={row->goal[0]*32,row->goal[1]*32},
            fine_target={row->goal[0],row->goal[1]},selected;
        movePathQuery_t query={.geometry={&source,&target,8,2},.units=true,.fine_target=&fine_target};
        moveFineRoute_t route={0}; T_ASSERT(G_BuildUnitMoveLocalRoute(&query,&route,&selected));
        wc3FineSearch_t const *search=G_TestMoveFineSearch();
        T_EQ(search->count,row->nodes); T_EQ(search->pops,row->work);
        T_EQ(search->node_capacity,4096); T_ASSERT(fine_storage_node_hash(search)==row->node_hash);
        T_EQ(route.partial,!row->result); T_EQ(route.count,row->points);
        if (route.count==row->points) FOR_LOOP(j,route.count) {
            T_EQ(wc3_float_bits(route.points[j].x),row->route[j][0]); T_EQ(wc3_float_bits(route.points[j].y),row->route[j][1]);
        }
        free(route.points);
    }
    G_FreeMovePathCache();
    T_ASSERT(!G_TestMoveFineSearch()->nodes && !G_TestMoveFineSearch()->heap);
    free(cells); reset_entities(); setup_test_world();
}

TEST(pathfinding, retained_fine_storage_matches_native_stamp_wrap_nodes_and_routes) {
    reset_entities(); setup_test_world();
    uint8_t cells[48*48];
    FOR_LOOP(y,48) FOR_LOOP(x,48) cells[y*48+x]=(retail_budget_goal_rows[y]&(1ULL<<x)) ? 2 : 0;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{1536,1536}});
    CM_SetupTestPathmap(48,48,cells);
    vec2_t source={4.25f*32,4.75f*32},target={43.25f*32,43.75f*32},selected;
    /* Engine clears its sparse lookup each request; physical nodes/heap remain
     * allocated. Both traversal orders must reproduce the native clean control. */
    FOR_LOOP(pass,2) FOR_LOOP(k,4) {
        retailFineWrap_t const *row=retail_fine_wrap+(pass ? 3-k : k);
        movePathQuery_t query={.geometry={&source,&target,(.25f+.5f*row->cls)*32,row->mask},.units=true};
        moveFineRoute_t route={0};
        T_ASSERT(G_BuildUnitMoveLocalRoute(&query,&route,&selected));
        wc3FineSearch_t const *search=G_TestMoveFineSearch();
        T_EQ(search->pops,row->work); T_EQ(search->count,row->nodes);
        T_EQ(search->dist2,0); T_EQ(search->nodes[search->nearest].pos.x,43);
        T_EQ(search->nodes[search->nearest].pos.y,43);
        if(search->count==row->nodes) FOR_LOOP(i,row->nodes) {
            wc3FineNode_t const *n=search->nodes+i;
            uint32_t words[]={n->pos.x,n->pos.y,n->g,n->h,n->gen,(uint32_t)n->parent,n->state};
            FOR_LOOP(j,7) T_EQ(words[j],row->state[i][j]);
        }
        T_ASSERT(!route.partial); T_EQ(route.count,row->points);
        if(route.count==row->points) FOR_LOOP(i,row->points) {
            T_EQ(wc3_float_bits(route.points[i].x),row->route[i][0]);
            T_EQ(wc3_float_bits(route.points[i].y),row->route[i][1]);
        }
        free(route.points);
    }
    reset_entities(); setup_test_world();
}

wc3AccSearch_t const *G_TestMoveAdaptiveSearch(void);
TEST(pathfinding, owned_adaptive_requests_reuse_all_lanes_sizes_and_partial_node_states) {
    reset_entities(); setup_test_world(); S_ClearMoveFineRequests();
    uint8_t cells[64*64], masks[]={2,0x80,0x40,4};
    FOR_LOOP(y,64) FOR_LOOP(x,64) {
        unsigned flags=0;
        FOR_LOOP(lane,4) if(retail_adaptive_wrap_rows[lane][y/2]&(1u<<(x/2))) flags|=masks[lane];
        cells[y*64+x]=flags;
    }
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    CM_SetupTestPathmap(64,64,cells);
    vec2_t source={4.25f*64,4.75f*64},target={27.25f*64,27.75f*64},selected;
    edict_t *unit=make_unit_at(source.x,source.y);
    uint32_t old_counter=level.pathing_counter;
    moveFineRoute_t route={0};
    /* Keep the actual cached hierarchy, request nodes/heap and route backing.
     * Lane/size changes invalidate old coarse buffers through production code. */
    FOR_LOOP(pass,2) FOR_LOOP(k,8) {
        retailAdaptiveWrap_t const *row=retail_adaptive_wrap+k;
        unit->collision=row->size==2 ? 40 : 8;
        /* The original oracle explicitly enables adaptive routing in every
         * query lane; ordinary flight profiles instead disable that policy. */
        unit->aiflags=0;
        level.pathing_counter=400+(pass*8+k)*20;
        /* Each request has a fresh owner work window; scheduler cadence is a
         * separate contract from retained adaptive storage. */
        level.move_fine_budgets[0].work=0;
        movePathQuery_t query={.geometry={&source,&target,unit->collision,masks[row->lane]},.units=true,.mover=unit};
        T_ASSERT(G_BuildUnitMoveFineRoute(&query,&route,&selected));
        wc3AccSearch_t const *search=G_TestMoveAdaptiveSearch();
        T_EQ(search->size,row->size); T_EQ(search->work.pops,row->work); T_EQ(search->work.count,row->nodes);
        if(search->work.count==row->nodes) FOR_LOOP(i,row->nodes) {
            wc3FineNode_t const *n=search->work.nodes+i;
            uint32_t words[]={n->pos.x,n->pos.y,n->g,n->h,n->gen,(uint32_t)n->parent,n->state,search->levels[i]};
            FOR_LOOP(j,8) T_EQ(words[j],row->state[i][j]);
        }
        T_EQ(route.adaptive_count,row->points);
        if(route.adaptive_count==row->points) FOR_LOOP(i,row->points) {
            T_EQ(wc3_float_bits(route.adaptive_points[i].x),row->route[i][0]);
            T_EQ(wc3_float_bits(route.adaptive_points[i].y),row->route[i][1]);
        }
    }
    free(route.points); free(route.adaptive_points); level.pathing_counter=old_counter;
    S_ClearMoveFineRequests(); reset_entities(); setup_test_world();
}

TEST(pathfinding, adaptive_source_and_target_exclusion_restore_nofly_ground_classes) {
    reset_entities(); setup_test_world(); S_ClearMoveFineRequests();
    uint8_t cells[64*64]={0};
    /* Original15d360 emits base flag byte41 ->0 ->41 on no-fly-only4.
     * Initial/recovery ground classification includes6; fine ground uses2. */
    FOR_LOOP(y,2) FOR_LOOP(x,2) {
        cells[(8+y)*64+8+x]=4;
        cells[(54+y)*64+54+x]=4;
    }
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    CM_SetupTestPathmap(64,64,cells);
    vec2_t source={4.25f*64,4.75f*64},target={27.25f*64,27.75f*64},selected;
    edict_t *unit=make_unit_at(source.x,source.y),*object=make_unit_at(target.x,target.y);
    unit->collision=object->collision=8;
    unsigned before[4][4][41*41];
    unsigned masks[]={2,4,0x40,0x80};
    FOR_LOOP(lane,4) FOR_LOOP(level,4) {
        point2_t size=G_TestMovePathSize(level);
        FOR_LOOP(y,size.y) FOR_LOOP(x,size.x)
            before[lane][level][y*size.x+x]=G_TestMovePathClass(masks[lane],level,x,y);
    }
    T_EQ(G_TestMovePathClass(2,0,4,4),1); T_EQ(G_TestMovePathClass(4,0,4,4),1);
    pathAccelParams_t admission={&source,&target,8,2};
    T_ASSERT(G_MovePathPointIsPathable(&admission));
    uint32_t old_counter=level.pathing_counter; level.pathing_counter=400;
    movePathQuery_t query={.geometry=admission,.units=true,.mover=unit,.target=object};
    moveFineRoute_t route={0}; T_ASSERT(G_BuildUnitMoveFineRoute(&query,&route,&selected));
    /* A successful request restores both excluded rectangles and all parents,
     * even when the terrain rejects only the original coarse-ground mask. */
    FOR_LOOP(lane,4) FOR_LOOP(level,4) {
        point2_t size=G_TestMovePathSize(level);
        FOR_LOOP(y,size.y) FOR_LOOP(x,size.x)
            T_EQ(G_TestMovePathClass(masks[lane],level,x,y),before[lane][level][y*size.x+x]);
    }
    free(route.points); free(route.adaptive_points); level.pathing_counter=old_counter;
    S_ClearMoveFineRequests(); reset_entities(); setup_test_world();
}

TEST(pathfinding, terrain_producers_preserve_retail_size2_passage_veto_and_partial_route) {
    reset_entities(); setup_test_world(); S_ClearMoveFineRequests();
    uint8_t cells[64*64]={0},masks[]={2,0x80,0x40,4};
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{2048,2048}});
    /* All54 attainable ordinary lane tuples have original producer witnesses.
     * Ground hierarchy uses6, so clear ground cannot coexist with blocked air. */
    FOR_LOOP(k,54) {
        memset(cells,0,sizeof(cells));
        FOR_LOOP(i,4) cells[(i/2)*64+i%2]=retail_producer_inventory[k][i];
        CM_SetupTestPathmap(64,64,cells);
        FOR_LOOP(lane,4) T_EQ(G_TestMovePathClass(masks[lane],0,0,0),retail_producer_inventory[k][4+lane]);
    }
    FOR_LOOP(y,64) FOR_LOOP(x,64)
        cells[y*64+x]=retail_producer_rows[y/2]&(1u<<(x/2)) ? 0xc6 : 0;
    CM_SetupTestPathmap(64,64,cells);
    unsigned offset=0;
    FOR_LOOP(l,4) {
        point2_t size=G_TestMovePathSize(l);
        T_EQ(size.x,41u>>l); T_EQ(size.y,41u>>l);
        FOR_LOOP(y,size.y) FOR_LOOP(x,size.x) {
            unsigned byte=retail_producer_classes[offset+y*size.x+x];
            FOR_LOOP(lane,4) T_EQ(G_TestMovePathClass(masks[lane],l,x,y),(byte>>(6-2*lane))&3);
        }
        offset+=size.x*size.y;
    }
    T_EQ(offset,2206);
    vec2_t source={4.25f*64,4.75f*64},target={27.25f*64,27.75f*64},selected;
    edict_t *unit=make_unit_at(source.x,source.y);
    unit->collision=40;
    uint32_t old_counter=level.pathing_counter;
    moveFineRoute_t route={0};
    FOR_LOOP(pass,2) FOR_LOOP(lane,4) {
        /* This oracle forces adaptive requests in every query lane. Keep the
         * ordinary adaptive caller; authored flight disables that policy. */
        unit->aiflags=0;
        level.pathing_counter=400+(pass*4+lane)*20; level.move_fine_budgets[0].work=0;
        movePathQuery_t query={.geometry={&source,&target,40,masks[lane]},.units=true,.mover=unit};
        T_ASSERT(G_BuildUnitMoveFineRoute(&query,&route,&selected));
        wc3AccSearch_t const *search=G_TestMoveAdaptiveSearch();
        T_EQ(search->size,2); T_EQ(search->work.pops,38); T_EQ(search->work.count,56);
        if(search->work.count==56) FOR_LOOP(i,56) {
            wc3FineNode_t const *n=search->work.nodes+i;
            uint32_t words[]={n->pos.x,n->pos.y,n->g,n->h,n->gen,(uint32_t)n->parent,n->state,search->levels[i]};
            FOR_LOOP(j,8) T_EQ(words[j],retail_producer_nodes[i][j]);
        }
        T_EQ(route.adaptive_count,6);
        if(route.adaptive_count==6) FOR_LOOP(i,6) {
            T_EQ(wc3_float_bits(route.adaptive_points[i].x),retail_producer_route[i][0]);
            T_EQ(wc3_float_bits(route.adaptive_points[i].y),retail_producer_route[i][1]);
        }
        /* The issued click survives the partial search; route0 is its nearest
         * centre. Future fine legs/retries must retain this native limitation. */
        T_EQ(wc3_float_bits(route.adaptive_goal.x),wc3_float_bits(target.x/32));
        T_EQ(wc3_float_bits(route.adaptive_goal.y),wc3_float_bits(target.y/32));
    }
    free(route.points); free(route.adaptive_points); level.pathing_counter=old_counter;
    S_ClearMoveFineRequests(); reset_entities(); setup_test_world();
}

TEST(pathfinding, exhausted_fine_budget_keeps_discovered_goal_centre_and_charges_denied_pop) {
    reset_entities(); setup_test_world(); S_ClearMoveFineRequests();
    uint8_t cells[48*48];
    FOR_LOOP(y,48) FOR_LOOP(x,48) cells[y*48+x]=(retail_budget_goal_rows[y]&(1ULL<<x)) ? 2 : 0;
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{1536,1536}});
    CM_SetupTestPathmap(48,48,cells);
    vec2_t source={4.25f*32,4.75f*32},target={43.25f*32,43.75f*32},selected;
    edict_t *unit=make_unit_at(source.x,source.y); unit->collision=8;
    uint32_t old_counter=level.pathing_counter; level.pathing_counter=400;
    movePathQuery_t query={.geometry={&source,&target,8,2},.units=true,.mover=unit};
    moveFineRoute_t route={0};
    T_ASSERT(G_BuildUnitMoveLocalRoute(&query,&route,&selected));
    T_ASSERT(route.partial); T_EQ(level.move_fine_budgets[0].work,701);
    T_EQ(route.count,sizeof(retail_budget_goal_route)/sizeof(*retail_budget_goal_route));
    if(route.count==sizeof(retail_budget_goal_route)/sizeof(*retail_budget_goal_route)) FOR_LOOP(i,route.count) {
        T_EQ(wc3_float_bits(route.points[i].x),retail_budget_goal_route[i][0]);
        T_EQ(wc3_float_bits(route.points[i].y),retail_budget_goal_route[i][1]);
    }
    /* The user's click remains available for subsequent refill/arrival. */
    T_EQ(wc3_float_bits(target.x),wc3_float_bits(43.25f*32));
    T_EQ(wc3_float_bits(target.y),wc3_float_bits(43.75f*32));
    free(route.points); level.pathing_counter=old_counter;
    S_ClearMoveFineRequests(); reset_entities(); setup_test_world();
}

TEST(pathfinding, retail_passages_match_all_lanes_classes_offsets_corners_and_edges) {
    reset_entities(); setup_test_world();
    CM_SetupTestWorldBounds(&(box2_t){{0,0},{512,512}});
    uint8_t cells[16*16];
    FOR_LOOP(i,sizeof(retail_passages)/sizeof(*retail_passages)) {
        retailPassage_t const *row=retail_passages+i;
        FOR_LOOP(y,16) FOR_LOOP(x,16)
            cells[y*16+x]=(retail_passage_shapes[row->shape][y]&(1u<<x)) ? row->flags : 0;
        CM_SetupTestPathmap(16,16,cells);
        vec2_t source={wc3_float(row->words[0])*32,wc3_float(row->words[1])*32};
        vec2_t target={wc3_float(row->words[2])*32,wc3_float(row->words[3])*32};
        float radius=(.25f+.5f*row->cls)*32;
        pathAccelParams_t params={&source,&target,radius,row->mask};
        T_EQ(G_MovePathPointIsPathable(&params),row->endpoints[0]);
        params.from=&target;
        T_EQ(G_MovePathPointIsPathable(&params),row->endpoints[1]);
        /* Public admission/correction owns blocked endpoints. Compare complete
         * route construction where both original footprint consumers admit. */
        if(!row->endpoints[0] || !row->endpoints[1]) continue;
        params.from=&source;
        movePathQuery_t query={.geometry=params,.units=true};
        moveFineRoute_t route={0}; vec2_t selected;
        bool built=G_BuildUnitMoveLocalRoute(&query,&route,&selected);
        T_EQ(built,row->count>=2);
        if(built && route.count) {
            T_EQ(route.partial,!row->result); T_EQ(route.count,row->count);
            T_EQ(wc3_float_bits(route.points[0].x),row->words[4]);
            T_EQ(wc3_float_bits(route.points[0].y),row->words[5]);
            T_EQ(wc3_float_bits(route.points[route.count-1].x),row->words[0]);
            T_EQ(wc3_float_bits(route.points[route.count-1].y),row->words[1]);
            if(route.count==row->count) for(unsigned k=1;k+1<route.count;k++) {
                T_EQ(wc3_float_bits(route.points[k].x),retail_passage_middle[row->middle+2*(k-1)]);
                T_EQ(wc3_float_bits(route.points[k].y),retail_passage_middle[row->middle+2*(k-1)+1]);
            }
        }
        free(route.points);
    }
    reset_entities(); setup_test_world();
}

TEST(wc3_pathfinding, retail_collision_classes_fit_their_cardinal_corridors) {
    static float const radii[] = {0.499f, 0.5f, 0.999f, 1.0f, 1.499f, 1.5f, 2.0f};
    static int const sizes[] = {1, 2, 2, 3, 3, 4, 4};
    uint8_t cells[16 * 16];
    vec2_t from = {8.25f, 4.75f}, target = {8.25f, 11.75f}, step = {8.25f, 5.75f}, point;

    for (int i = 0; i < 7; i++) for (int width = sizes[i] - 1; width <= sizes[i] + 1; width++) {
        reset_entities();
        setup_test_world();
        memset(cells, CM_PATHING_UNWALKABLE, sizeof(cells));
        for (int y = 0; y < 16; y++) for (int x = 8 - width / 2; x < 8 - width / 2 + width; x++)
            cells[y * 16 + x] = 0;
        setup_test_pathmap(16, 16, cells);
        edict_t *unit = make_unit_at(from.x, from.y);
        unit->collision = radii[i];
        pathAccelParams_t params = { &from, &target, radii[i], CM_PATHING_UNWALKABLE };
        T_EQ(G_FindMovePathWaypoint(&params, &point), width >= sizes[i]);
        T_EQ(M_MoveIsValid(unit, &step), width >= sizes[i]);
        if (width >= sizes[i]) {
            unit->unitinfo.MoveSpeed = 2.0f;
            unit->s.angle = (float)M_PI / 2;
            T_ASSERT(unit_issueorder(unit, "move", &target));
            T_FEQ(unit->goalentity->s.origin.x, target.x, 0.001f);
            T_FEQ(unit->goalentity->s.origin.y, target.y, 0.001f);
            unit->currentmove->think(unit);
            T_ASSERT(unit->s.origin.y > from.y);
        }
    }
    reset_entities();
    setup_test_world();
}

/* 168d30's initial previous cell0,0 makes a positive cardinal first sample
 * use southeast entering strips. These predecessor-side blockers are outside
 * both endpoint footprints; Bresenham omitted every one. */
TEST(wc3_pathfinding, move_segments_use_retail_first_sample_strips) {
    static float const radii[] = {0.f, 0.5f, 1.f, 1.5f};
    static int const blockers[][2] = {{9,7},{9,6},{10,6},{10,5}};
    vec2_t from = {8.25f,8.75f}, target = {11.25f,8.75f};
    uint8_t cells[16 * 16];
    for (int i = 0; i < 4; i++) {
        reset_entities();
        setup_test_world();
        memset(cells, 0, sizeof(cells));
        cells[blockers[i][1] * 16 + blockers[i][0]] = CM_PATHING_UNWALKABLE;
        setup_test_pathmap(16, 16, cells);
        pathAccelParams_t query = { &from, &target, radii[i], CM_PATHING_UNWALKABLE };
        T_ASSERT(G_MovePathPointIsPathable(&query));
        pathAccelParams_t end = query; end.from = &target;
        T_ASSERT(G_MovePathPointIsPathable(&end));
        T_ASSERT(!G_MovePathLineIsPathable(&query));
    }
    reset_entities();
    setup_test_world();
}

/* Live ground-crowd Frida fine queries reject idle Footman objects with
 * category010000ca/flags0 under mask02000002. An idle unit ahead must affect
 * routing before it becomes a one-step local collision. */
/* Original147dc0 replaces a successful fine endpoint with the exact supplied
 * destination when floors match. Cell centres would change fractional orders. */
TEST(wc3_pathfinding, fine_route_retains_fractional_destination_for_every_class) {
    uint8_t cells[24 * 24] = {0};
    static float const radii[] = {0.25f, 0.5f, 1.f, 1.5f};
    vec2_t source = {4.25f, 4.75f}, target = {19.875f, 17.125f}, out;
    reset_entities();
    setup_test_world();
    setup_test_pathmap(24, 24, cells);
    FOR_LOOP(i, sizeof(radii) / sizeof(*radii)) {
        pathAccelParams_t query = {&source, &target, radii[i], CM_PATHING_UNWALKABLE};
        T_ASSERT(G_FindMovePathWaypoint(&query, &out));
        T_EQ(out.x, target.x);
        T_EQ(out.y, target.y);
    }
    reset_entities();
    setup_test_world();
}

/* Original148100 retains the current node centre on a target identity hit,
 * even though the requested point is farther away. Target suppression must
 * leave the object observable to the perimeter queries. */
TEST(wc3_pathfinding, fine_target_exit_preserves_approach_endpoint) {
    uint8_t cells[24 * 24] = {0};
    static float const radii[] = {0.25f, 0.5f, 1.f, 1.5f};
    vec2_t goal = {19.25f, 19.75f}, out;
    for (unsigned cls = 0; cls < 4; cls++) {
        reset_entities();
        setup_test_world();
        setup_test_pathmap(24, 24, cells);
        edict_t *unit = make_unit_at(4.25f, 4.75f), *target = make_unit_at(12.25f, 12.25f);
        unit->collision = radii[cls]; target->collision = 0.25f;
        gi.LinkEntity(unit); gi.LinkEntity(target);
        movePathQuery_t query = {{&unit->s.origin2, &goal, radii[cls], CM_PATHING_UNWALKABLE}, unit, target, true};
        T_ASSERT(G_FindUnitMovePathWaypoint(&query, &out));
        T_EQ(out.x, cls < 2 ? 11.5f : 10.5f);
        T_EQ(out.y, cls < 2 ? 11.5f : 10.5f);
        target->movement.velocity.x = 0.1f;
        T_ASSERT(G_FindUnitMovePathWaypoint(&query, &out));
        T_EQ(out.x, cls < 2 ? 11.5f : 10.5f);
        T_EQ(out.y, cls < 2 ? 11.5f : 10.5f);
        query.target = NULL;
        target->no_pathing = true;
        T_ASSERT(G_FindUnitMovePathWaypoint(&query, &out));
        T_EQ(out.x, goal.x); T_EQ(out.y, goal.y);
        target->movement.velocity = (vec2_t){0};
        query.target = target; /* SetUnitPathing(false) preserves the target occupancy category. */
        T_ASSERT(G_FindUnitMovePathWaypoint(&query, &out));
        T_EQ(out.x, cls < 2 ? 11.5f : 10.5f); T_EQ(out.y, cls < 2 ? 11.5f : 10.5f);
    }
    reset_entities();
    setup_test_world();
}

TEST(wc3_pathfinding, nearby_move_routes_around_idle_unit_footprint) {
    uint8_t cells[24 * 24] = {0};
    vec2_t target = {19.5f, 4.5f};
    reset_entities();
    setup_test_world();
    setup_test_pathmap(24, 24, cells);
    edict_t *unit = make_unit_at(4.5f, 4.5f), *idle = make_unit_at(12.5f, 4.5f);
    unit->collision = idle->collision = 0.5f;
    unit->unitinfo.MoveSpeed = 2.f;
    gi.LinkEntity(unit);
    gi.LinkEntity(idle);
    T_ASSERT(unit_issueorder(unit, "move", &target));
    unit_changeangle(unit);
    T_ASSERT(!unit->movement.flow_direct);
    T_ASSERT(unit->movement.path.valid);
    T_ASSERT(unit->movement.path.waypoint.x > unit->s.origin2.x);
    bool detour = false;
    FOR_LOOP(k,unit->movement.fine_route.count)
        detour |= fabsf(unit->movement.fine_route.points[k].y - 4.5f) > 0.01f;
    T_ASSERT(detour);
    reset_entities();
    setup_test_world();
}

/* Captured hfoo objects useca/2, hgry objects0/4; original1606e0 sets and
 * clears20000000 with velocity. Mixed chains retain the idle obstruction. */
TEST(wc3_pathfinding, nearby_unit_routes_follow_live_object_eligibility) {
    uint8_t cells[24 * 24] = {0};
    vec2_t target = {19.5f, 4.5f};
    reset_entities();
    setup_test_world();
    setup_test_pathmap(24, 24, cells);
    edict_t *unit = make_unit_at(4.5f, 4.5f), *idle = make_unit_at(12.5f, 4.5f);
    unit->collision = idle->collision = 0.5f;
    gi.LinkEntity(unit);
    gi.LinkEntity(idle);
    movePathQuery_t query = { {&unit->s.origin2, &target, 0.5f, CM_PATHING_UNWALKABLE}, unit, NULL, true };
    T_ASSERT(!G_UnitMovePathLineIsPathable(&query));
    query.geometry.blocked_flags = 0; /* Retail query zero bypasses this ordinary occupied category. */
    T_ASSERT(G_UnitMovePathLineIsPathable(&query));
    query.geometry.blocked_flags = CM_PATHING_UNWALKABLE;
    edict_t *goal = make_waypoint(target.x, target.y);
    G_RequestMovePathField(NULL, goal, 0.5f, CM_PATHING_UNWALKABLE);
    CM_ProcessPathJobs(UINT_MAX);
    uint32_t gen = G_RequestMovePathField(NULL, goal, 0.5f, CM_PATHING_UNWALKABLE);
    T_ASSERT(G_ActivateMovePathField(gen, 0.5f, CM_PATHING_UNWALKABLE));
    idle->movement.velocity.x = 0.2f;
    T_ASSERT(G_UnitMovePathLineIsPathable(&query));
    T_ASSERT(G_ActivateMovePathField(gen, 0.5f, CM_PATHING_UNWALKABLE));
    T_EQ(G_RequestMovePathField(NULL, goal, 0.5f, CM_PATHING_UNWALKABLE), gen);
    edict_t *other = make_unit_at(12.5f, 4.5f);
    other->collision = 0.5f;
    gi.LinkEntity(other);
    T_ASSERT(!G_UnitMovePathLineIsPathable(&query));
    other->aiflags |= AI_FLYING;
    T_ASSERT(G_UnitMovePathLineIsPathable(&query));
    idle->movement.velocity = (vec2_t){0};
    T_ASSERT(!G_UnitMovePathLineIsPathable(&query));
    query.target = idle;
    T_ASSERT(G_UnitMovePathLineIsPathable(&query));
    query.target = NULL;
    query.units = false;
    T_ASSERT(G_UnitMovePathLineIsPathable(&query));
    query.units = true;
    unit->aiflags |= AI_FLYING;
    query.geometry.blocked_flags = CM_PATHING_UNFLYABLE;
    T_ASSERT(G_UnitMovePathLineIsPathable(&query));
    reset_entities();
    setup_test_world();
}

/* Use actual order/think/step entry points for each mover/object footprint.
 * The idle unit stays fixed; the walker must get past it without cancelling. */
TEST(wc3_pathfinding, nearby_move_passes_idle_units_for_all_fine_classes) {
    float const radii[] = {0.25f, 0.5f, 1.0f, 1.5f};
    uint8_t cells[24 * 24] = {0};
    vec2_t target = {19.5f, 4.5f};
    uint32_t old_time = level.time;
    for (unsigned i = 0; i < 4; i++) {
        reset_entities();
        setup_test_world();
        setup_test_pathmap(24, 24, cells);
        T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
        level.started=level.scriptsConfigured=level.scriptsStarted=true;
        level.time=level.pathing_msec=level.pathing_phase=0; level.pathing_due=false;
        level.pathing_clock=(wc3Clock_t){0,0,300};
        edict_t *unit = make_unit_at(4.5f, 4.5f), *idle = make_unit_at(12.5f, 4.5f);
        unit->collision = idle->collision = radii[i];
        unit->unitinfo.MoveSpeed = 2.f;
        gi.LinkEntity(unit);
        gi.LinkEntity(idle);
        T_ASSERT(unit_issueorder(unit, "move", &target));
        unit_changeangle(unit);
        T_ASSERT(!unit->movement.flow_direct);
        T_ASSERT(unit->movement.path.valid);
        for (int tick = 0; tick < 140; tick++) {
            level.time += FRAMETIME;
            globals.RunFrame();
        }
        T_ASSERT(unit->s.origin2.x > idle->s.origin2.x + 2.f);
        T_ASSERT(Vector2_distance(&unit->s.origin2, &target) <= 4.1f);
        T_FEQ(idle->s.origin2.x, 12.5f, 0.00001f);
        T_FEQ(idle->s.origin2.y, 4.5f, 0.00001f);
    }
    level.time = old_time;
    level.started=false;
    reset_entities();
    setup_test_world();
}

/* Original165ae0 retains the leg;166140 admits blockers at the next native
 * step. An object farther along the segment must not trigger eager replanning. */
TEST(wc3_pathfinding, nearby_move_retains_segment_until_next_step_is_blocked) {
    uint8_t cells[24 * 24] = {0};
    vec2_t target = {19.5f, 4.5f};
    reset_entities();
    setup_test_world();
    setup_test_pathmap(24, 24, cells);
    edict_t *unit = make_unit_at(4.5f, 4.5f), *idle = make_unit_at(12.5f, 4.5f);
    unit->collision = idle->collision = 0.5f;
    gi.LinkEntity(unit);
    gi.LinkEntity(idle);
    T_ASSERT(unit_issueorder(unit, "move", &target));
    unit_changeangle(unit);
    T_ASSERT(unit->movement.path.valid);
    /* Progress to the initial raw successor so retail lookahead supplies a
     * retained segment with interior samples; adjacent endpoints have none. */
    unit->s.origin2 = unit->movement.path.waypoint;
    gi.LinkEntity(unit);
    unit_changeangle(unit);
    vec2_t old = unit->movement.path.waypoint;
    edict_t *other = make_unit_at((unit->s.origin2.x+old.x)*0.5f,(unit->s.origin2.y+old.y)*0.5f);
    other->collision = 0.5f;
    gi.LinkEntity(other);
    movePathQuery_t query = { {&unit->s.origin2, &old, 0.5f, CM_PATHING_UNWALKABLE}, unit, unit->goalentity, true };
    T_ASSERT(!G_UnitMovePathLineIsPathable(&query));
    unit_changeangle(unit);
    T_ASSERT(unit->movement.path.valid);
    T_FEQ(Vector2_distance(&old, &unit->movement.path.waypoint),0,0.00001f);
    /* Move the same peer into the entering cell and exercise the real step
     * collector/Move retry path rather than the old whole-leg invalidation. */
    vec2_t dir=Vector2_sub(&old,&unit->s.origin2); Vector2_normalize(&dir);
    other->s.origin2=(vec2_t){unit->s.origin2.x+dir.x,unit->s.origin2.y+dir.y};
    gi.LinkEntity(other);
    edict_t *blockers[32]; float fine[]={old.x,old.y};
    T_ASSERT(G_CollectUnitMoveStepBlockers(&query,fine,blockers)>0);
    unit_changeangle(unit);
    T_ASSERT(!unit->movement.path.valid);
    T_ASSERT(unit->movement.turn_blocked);
    reset_entities();
    setup_test_world();
}

/* Retail148100 retains the nearest reachable chain on failure. A full-height
 * idle-unit wall must still give Move a useful approach turn. */
TEST(wc3_pathfinding, nearby_move_retains_partial_approach_to_idle_object_wall) {
    uint8_t cells[24 * 24] = {0};
    vec2_t target = {19.875f, 4.125f};
    edict_t *wall[24];
    uint32_t old_time = level.time;
    reset_entities();
    setup_test_world();
    setup_test_pathmap(24, 24, cells);
    edict_t *unit = make_unit_at(4.5f, 4.5f);
    unit->collision = 0.5f;
    unit->unitinfo.MoveSpeed = 2.f;
    gi.LinkEntity(unit);
    for (int y = 0; y < 24; y++) {
        edict_t *idle = make_unit_at(12.5f, y + 0.5f);
        wall[y] = idle;
        idle->collision = 0.5f;
        gi.LinkEntity(idle);
    }
    T_ASSERT(unit_issueorder(unit, "move", &target));
    unit_changeangle(unit);
    T_ASSERT(unit->movement.path.valid);
    T_ASSERT(!unit->movement.flow_direct);
    T_FEQ(unit->movement.fine_route.points[0].x, 10.5f, 0.00001f);
    T_FEQ(unit->movement.fine_route.points[0].y, 4.5f, 0.00001f);
    T_FEQ(unit->movement.path.waypoint.x, 5.5f, 0.00001f);
    T_FEQ(unit->movement.path.waypoint.y, 4.5f, 0.00001f);
    T_FEQ(unit->goalentity->s.origin2.x, target.x, 0.00001f);
    for (int tick = 0; tick < 10; tick++) {
        level.time += FRAMETIME;
        unit->currentmove->think(unit);
    }
    T_ASSERT(unit->s.origin2.x > 4.5f);
    T_ASSERT(unit->s.origin2.x < 10.5f);
    for (int y = 0; y < 24; y++) G_FreeEdict(wall[y]);
    for (int tick = 0; tick < 100; tick++) {
        level.time += FRAMETIME;
        if (unit->currentmove && unit->currentmove->think) unit->currentmove->think(unit);
    }
    T_ASSERT(Vector2_distance(&unit->s.origin2, &target) <= 4.1f);
    level.time = old_time;
    reset_entities();
    setup_test_world();
}

/* Fine rectangles can extend beyond an entity's physical circle. Broadphase
 * pruning must keep that biased class3 edge for a class0 line query. */
TEST(wc3_pathfinding, nearby_line_sees_quantized_object_edge_beyond_physical_bounds) {
    uint8_t cells[24 * 24] = {0};
    vec2_t target = {19.5f, 16.2f};
    reset_entities();
    setup_test_world();
    setup_test_pathmap(24, 24, cells);
    edict_t *unit = make_unit_at(4.5f, 16.2f), *idle = make_unit_at(12.5f, 18.9f);
    unit->collision = 0.25f;
    idle->collision = 1.5f;
    gi.LinkEntity(unit);
    gi.LinkEntity(idle);
    movePathQuery_t query = { {&unit->s.origin2, &target, 0.25f, CM_PATHING_UNWALKABLE}, unit, NULL, true };
    T_ASSERT(idle->bounds.min.y > target.y);
    T_ASSERT(!G_UnitMovePathLineIsPathable(&query));
    idle->s.origin2.y = 19.1f;
    gi.LinkEntity(idle);
    T_ASSERT(G_UnitMovePathLineIsPathable(&query));
    reset_entities();
    setup_test_world();
}

/* Direct/retained checks run for each mover every tick. Off-route crowds
 * should not require sorting the entire map's idle objects each time. */
TEST(wc3_perf, nearby_line_query_with_1900_idle_units) {
    reset_entities();
    setup_test_world();
    for (int i = 0; i < 1900; i++) {
        edict_t *idle = make_unit_at((i % 50) * 32.f, 512.f + (i / 50) * 32.f);
        idle->collision = 16.f;
        gi.LinkEntity(idle);
    }
    edict_t *unit = make_unit_at(128.f, 128.f);
    vec2_t target = {448.f, 128.f};
    movePathQuery_t query = { {&unit->s.origin2, &target, 16.f, CM_PATHING_UNWALKABLE}, unit, NULL, true };
    T_ASSERT(G_UnitMovePathLineIsPathable(&query));
    T_BENCH("Move direct line (1900 idle units)", 100, G_UnitMovePathLineIsPathable(&query));
    reset_entities();
    setup_test_world();
}

/* The endpoint is beyond the synchronous fine-search envelope. A two-cell L
 * corridor must remain usable by class1 in the shared incremental field. */
TEST(wc3_pathfinding, class_sized_long_field_reaches_winding_corridor_and_invalidates) {
    enum { WIDTH = 128, HEIGHT = 20 };
    uint8_t cells[WIDTH * HEIGHT];
    vec2_t target = {100.5f, 15.5f};
    uint32_t gen = 0;
    reset_entities();
    setup_test_world();
    memset(cells, CM_PATHING_UNWALKABLE, sizeof(cells));
    for (int x = 4; x <= 100; x++) cells[4 * WIDTH + x] = cells[5 * WIDTH + x] = 0;
    for (int y = 4; y <= 15; y++) cells[y * WIDTH + 99] = cells[y * WIDTH + 100] = 0;
    setup_test_pathmap(WIDTH, HEIGHT, cells);
    edict_t *unit = make_unit_at(5.5f, 5.5f), *wp = make_waypoint(target.x, target.y);
    unit->collision = 0.5f;
    order_move(unit, wp);
    pathAccelParams_t params = { &unit->s.origin2, &target, unit->collision, CM_PATHING_UNWALKABLE };
    vec2_t turn;
    T_ASSERT(!G_MovePathLineIsPathable(&params));
    T_ASSERT(!G_FindMovePathWaypoint(&params, &turn));
    T_EQ(M_RefreshHeatmapForMover(unit, wp, unit->collision), 0);
    for (int tick = 0; tick < 64 && !gen; tick++) {
        CM_ProcessPathJobs(16);
        gen = M_RefreshHeatmapForMover(unit, wp, unit->collision);
    }
    T_ASSERT(gen != 0);
    T_ASSERT(CM_FlowCanReach(gen, unit->s.origin.x, unit->s.origin.y));
    unit_changeangle(unit);
    T_EQ(unit->movement.flow_generation, gen);
    T_ASSERT(!unit->movement.flow_unreachable);
    unit->unitinfo.MoveSpeed = 2.f;
    unit->s.angle = 0.f;
    for (int tick = 0; tick < 8; tick++) {
        level.time += FRAMETIME;
        unit->currentmove->think(unit);
        int x = (int)floorf(unit->s.origin.x), y = (int)floorf(unit->s.origin.y);
        T_ASSERT(x > 0 && x < WIDTH && y > 0 && y < HEIGHT);
        T_EQ(cells[(y - 1) * WIDTH + x - 1] | cells[(y - 1) * WIDTH + x] |
             cells[y * WIDTH + x - 1] | cells[y * WIDTH + x], 0);
    }
    T_ASSERT(unit->s.origin.x > 6.f);
    T_ASSERT(unit->movement.path.valid); /* adaptive routing now supplies the long Move's local turn */
    movePathQuery_t leg = {{&unit->s.origin2,&unit->movement.path.waypoint,
        unit->collision,CM_PATHING_UNWALKABLE},unit,NULL,true};
    T_ASSERT(G_UnitMovePathLineIsPathable(&leg));

    /* Pinch the passage to one cell. The class1 field must become unreachable
     * while a class0 field with the same ceil radius still crosses the gap. */
    cells[4 * WIDTH + 50] = CM_PATHING_UNWALKABLE;
    setup_test_pathmap(WIDTH, HEIGHT, cells);
    T_ASSERT(!CM_ActivateCachedFlowForFlags(gen, CM_PATHING_UNWALKABLE));
    uint32_t blocked = 0;
    for (int tick = 0; tick < 64 && !blocked; tick++) {
        blocked = M_RefreshHeatmapForMover(unit, wp, unit->collision);
        CM_ProcessPathJobs(16);
    }
    T_ASSERT(blocked != 0);
    T_NE(blocked, gen);
    T_ASSERT(!CM_FlowCanReach(blocked, unit->s.origin.x, unit->s.origin.y));
    uint32_t small = 0;
    for (int tick = 0; tick < 64 && !small; tick++) {
        small = M_RefreshHeatmapForMover(unit, wp, 0.499f);
        CM_ProcessPathJobs(16);
    }
    T_ASSERT(small != 0);
    T_NE(small, blocked);
    T_ASSERT(CM_FlowCanReach(small, unit->s.origin.x, unit->s.origin.y));
    pathAccelParams_t fallback = { &unit->s.origin2, &target, 0.499f, CM_PATHING_UNWALKABLE };
    vec2_t closest;
    T_ASSERT(G_ClosestReachableMovePoint(&fallback, &closest));
    T_FEQ(closest.x, target.x, 0.0001f);
    T_FEQ(closest.y, target.y, 0.0001f);
    fallback.radius = 0.5f;
    T_ASSERT(G_ClosestReachableMovePoint(&fallback, &closest));
    T_FEQ(closest.x, 49.5f, 0.0001f);
    T_FEQ(closest.y, 5.5f, 0.0001f);
    reset_entities();
    setup_test_world();
}

/* Exceptional source floods and incremental goal floods share scratch. A
 * source query between job ticks must never publish prices under the goal. */
TEST(wc3_pathfinding, closest_reachable_preserves_pending_field_target) {
    uint8_t cells[128 * 128] = {0};
    vec2_t from = {2.f, 2.f}, target = {8.f, 8.f}, out;
    uint32_t gen = 0;
    reset_entities();
    setup_test_world();
    setup_test_pathmap(128, 128, cells);
    edict_t *wp = make_waypoint(120.f, 120.f);
    T_EQ(CM_RequestHeatmapForRadius(wp, 0.f), 0);
    CM_ProcessPathJobs(16);
    T_ASSERT(CM_ClosestReachablePointForRadius(&from, &target, 0.f, &out));
    for (int tick = 0; tick < 128 && !gen; tick++) {
        gen = CM_RequestHeatmapForRadius(wp, 0.f);
        CM_ProcessPathJobs(4096);
    }
    T_ASSERT(gen != 0);
    vec2_t dir = get_flow_direction(gen, wp->s.origin.x, wp->s.origin.y);
    T_FEQ(dir.x, 0.f, 0.0001f);
    T_FEQ(dir.y, 0.f, 0.0001f);
    reset_entities();
    setup_test_world();
}

TEST(wc3_pathfinding, heatmap_cache_separates_collision_radius) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *wp = make_waypoint(5.0f, 5.0f);
    uint32_t point_gen = CM_BuildHeatmapForRadius(wp, 0.0f);
    uint32_t wide_gen = CM_BuildHeatmapForRadius(wp, 1.0f);

    T_ASSERT(point_gen != 0);
    T_ASSERT(wide_gen != 0);
    T_ASSERT(point_gen != wide_gen);
}

/* -----------------------------------------------------------------------
 * Multi-goal cache (fix #3): switching between two known goals should
 * not force a full rebuild every time; each goal keeps its own cached
 * generation.
 * --------------------------------------------------------------------- */

TEST(wc3_pathfinding, multi_goal_cache_no_thrash) {
    /* Must call setup_test_pathmap once only — it resets the cache. */
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);

    /* Use distinct waypoint slots from the global pool so pointers differ. */
    edict_t *wp_a = make_waypoint(1.0f, 1.0f);
    edict_t *wp_b = make_waypoint(8.0f, 8.0f);

    /* Verify the two waypoints are actually different pointers. */
    T_ASSERT(wp_a != wp_b);

    /* Build both goals once — they each occupy a cache slot. */
    uint32_t gen_a1 = CM_BuildHeatmap(wp_a);
    uint32_t gen_b1 = CM_BuildHeatmap(wp_b);

    /* Both goals are different so their generations must differ. */
    T_ASSERT(gen_a1 != gen_b1);

    /* Switch back to each — both should hit the cache (same generation). */
    uint32_t gen_a2 = CM_BuildHeatmap(wp_a);
    uint32_t gen_b2 = CM_BuildHeatmap(wp_b);

    T_EQ(gen_a1, gen_a2);
    T_EQ(gen_b1, gen_b2);
}

TEST(wc3_pathfinding, heatmap_cache_ignores_stale_dynamic_pathmap_stamps) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *wp1 = make_waypoint(5.0f, 5.0f);
    uint32_t gen1 = CM_BuildHeatmap(wp1);

    edict_t *unit = make_unit_at(wp1->s.origin.x, wp1->s.origin.y);
    vec2_t pathable;
    CM_ClosestPathablePointForRadius(&wp1->s.origin2, unit->collision, &pathable);

    edict_t *wp2 = make_waypoint(5.0f, 5.0f);
    uint32_t gen2 = CM_BuildHeatmap(wp2);

    T_EQ(gen1, gen2);
}

TEST(wc3_pathfinding, heatmap_build_does_not_bake_whole_flow_field) {
    build_split_map();
    setup_test_pathmap(MAP_W, MAP_H, split_map);
    reset_entities();
    CM_ResetTestPathPerfStats();

    edict_t *wp = make_waypoint(7.0f, 5.0f);
    CM_BuildHeatmap(wp);

    struct routePerfStats_s stats = CM_GetTestPathPerfStats();
    T_EQ(stats.cache_misses, 1);
    T_EQ(stats.cache_hits, 0);
    T_EQ(stats.heatmap_iterations, (MAP_W - 6) * MAP_H);
    T_EQ(stats.flow_cells_computed, 0);

    (void)get_flow_direction(CM_BuildHeatmap(wp), 7.0f, 5.0f);
    stats = CM_GetTestPathPerfStats();
    T_ASSERT(stats.flow_cells_computed > 0);
    T_ASSERT(stats.flow_cells_computed <= 4);
}

TEST(wc3_pathfinding, flow_reachability_distinguishes_disconnected_component) {
    build_split_map();
    setup_test_pathmap(MAP_W, MAP_H, split_map);
    reset_entities();

    edict_t *wp = make_waypoint(7.0f, 5.0f);
    uint32_t gen = CM_BuildHeatmap(wp);

    T_ASSERT(CM_FlowCanReach(gen, 7.0f, 5.0f));
    T_ASSERT(!CM_FlowCanReach(gen, 3.0f, 5.0f));
}

/* -----------------------------------------------------------------------
 * Static obstacle tests
 * --------------------------------------------------------------------- */

TEST(wc3_pathfinding, wall_routes_flow_around_obstacle) {
    build_wall_map();
    setup_test_pathmap(MAP_W, MAP_H, wall_map);
    reset_entities();

    /* Goal on the right side of the wall at (7, 5), reachable only through
     * the gap at y=8 or y=9.  Sample flow at (3, 5): left side of wall.
     * The direct rightward path is blocked, so the flow must deviate — it
     * should NOT point straight right (+x only) through the wall; it will
     * bend toward the gap at y=8/9, giving a downward (y) component.
     * We also verify the goal IS reachable from the right side (7,5). */
    edict_t *wp = make_waypoint(7.0f, 5.0f);
    build_flow(wp);

    /* Flow at (7, 5) itself is zero by contract.  Flow at (8, 5) — right
     * side, open — should point toward the goal
     * i.e. leftward (-x component). */
    vec2_t dir_right = flow_at_cell(8.0f, 5.0f);
    T_ASSERT(dir_right.x < 0.0f);

    /* Flow at (3, 5) — left of wall — must have a non-zero y component
     * to route around the wall (can't go straight right). */
    vec2_t dir_left = flow_at_cell(3.0f, 5.0f);
    T_ASSERT(dir_left.y != 0.0f || dir_left.x != 0.0f);
}

TEST(wc3_pathfinding, flow_direction_points_toward_goal_open) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    /* Goal at right edge; sample from left side. */
    edict_t *wp = make_waypoint(9.0f, 5.0f);
    build_flow(wp);

    /* At cell (2, 5), flow should point roughly rightward (+x). */
    vec2_t dir = flow_at_cell(2.0f, 5.0f);
    T_ASSERT(dir.x > 0.0f);
}

TEST(wc3_pathfinding, flow_goal_has_no_outward_direction) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *wp = make_waypoint(5.0f, 5.0f);
    uint32_t gen = CM_BuildHeatmap(wp);
    vec2_t dir = get_flow_direction(gen, 5.0f, 5.0f);

    T_ASSERT(CM_FlowReachedGoal(gen, 5.0f, 5.0f));
    T_FEQ(dir.x, 0.0f, 0.001f);
    T_FEQ(dir.y, 0.0f, 0.001f);
}

TEST(wc3_pathfinding, flow_goal_reports_adjusted_blocked_target_cell) {
    uint8_t blocked_goal[MAP_W * MAP_H];
    memset(blocked_goal, 0, sizeof(blocked_goal));
    blocked_goal[5 * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, blocked_goal);
    reset_entities();

    edict_t *wp = make_waypoint(5.0f, 5.0f);
    uint32_t gen = CM_BuildHeatmap(wp);

    T_ASSERT(gen != 0);
    T_ASSERT(!CM_FlowReachedGoal(gen, 5.0f, 5.0f));
    /* Deterministic closest_pathable_node scans the upper ring first. */
    T_ASSERT(CM_FlowReachedGoal(gen, 5.0f, 4.0f) ||
             CM_FlowReachedGoal(gen, 4.0f, 4.0f));
}

/* Point routes are used for interactions whose real target can be blocked,
 * such as a Gold Mine or Town Hall centre.  The adjusted route-end cell must
 * not produce a vector back out into the map: all workers sharing that field
 * would otherwise orbit the same wrong location instead of completing the
 * behavior-owned footprint/range interaction. */
TEST(wc3_pathfinding, point_flow_adjusted_goal_has_no_outward_direction) {
    uint8_t blocked_goal[MAP_W * MAP_H];
    float goal_x = 5.0f, goal_y = 4.0f;
    vec2_t dir;

    memset(blocked_goal, 0, sizeof(blocked_goal));
    blocked_goal[5 * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, blocked_goal);
    reset_entities();

    edict_t *wp = make_waypoint(5.0f, 5.0f);
    uint32_t gen = CM_BuildHeatmap(wp);

    T_ASSERT(gen != 0);
    if (!CM_FlowReachedGoal(gen, goal_x, goal_y)) {
        goal_x = 4.0f;
        goal_y = 4.0f;
    }
    T_ASSERT(CM_FlowReachedGoal(gen, goal_x, goal_y));
    dir = get_flow_direction(gen, goal_x, goal_y);
    T_FEQ(dir.x, 0.0f, 0.001f);
    T_FEQ(dir.y, 0.0f, 0.001f);
}

/* Generic interactions intentionally use a radius-zero field and own their
 * final range test.  Reaching the field's adjusted target is therefore not an
 * unreachable/stall state: steering must expose that route-end and continue
 * toward the real entity target so mine entry, resource deposit, attack range,
 * and similar behavior checks can finish. */
TEST(wc3_pathfinding, interaction_point_route_reports_adjusted_goal) {
    uint8_t blocked_goal[MAP_W * MAP_H];
    float goal_x = 5.0f, goal_y = 4.0f;

    memset(blocked_goal, 0, sizeof(blocked_goal));
    blocked_goal[5 * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, blocked_goal);
    reset_entities();

    edict_t *target = make_waypoint(5.0f, 5.0f);
    uint32_t gen = CM_BuildHeatmap(target);
    T_ASSERT(gen != 0);
    if (!CM_FlowReachedGoal(gen, goal_x, goal_y)) {
        goal_x = 4.0f;
        goal_y = 4.0f;
    }
    T_ASSERT(CM_FlowReachedGoal(gen, goal_x, goal_y));

    edict_t *unit = make_unit_at(goal_x, goal_y);
    unit->collision = 0.0f;
    unit->goalentity = target;
    target->heatmap2 = gen;
    target->heatmap2_radius = 0.0f;

    unit_changeangle(unit);

    T_EQ(unit->movement.flow_generation, gen);
    T_ASSERT(unit->movement.flow_goal_reached);
    T_ASSERT(!unit->movement.flow_unreachable);
}

TEST(wc3_pathfinding, line_walkability_respects_collision_radius) {
    uint8_t corridor[MAP_W * MAP_H];
    memset(corridor, 0, sizeof(corridor));
    for (int x = 0; x < MAP_W; x++) {
        corridor[4 * MAP_W + x] = 2;
        corridor[6 * MAP_W + x] = 2;
    }
    setup_test_pathmap(MAP_W, MAP_H, corridor);
    reset_entities();

    vec2_t a = { 1.0f, 5.0f };
    vec2_t b = { 8.0f, 5.0f };
    T_ASSERT(CM_LineIsWalkableForRadius(&a, &b, 0.0f));
    T_ASSERT(!CM_LineIsWalkableForRadius(&a, &b, 1.0f));
}

TEST(wc3_pathfinding, direct_approach_stops_before_blocked_target_center) {
    uint8_t blocked_target[MAP_W * MAP_H];
    vec2_t from = { 1.0f, 5.0f };
    vec2_t target = { 5.0f, 5.0f };
    vec2_t approach = { 0 };

    memset(blocked_target, 0, sizeof(blocked_target));
    blocked_target[5 * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, blocked_target);
    reset_entities();

    T_ASSERT(CM_FindDirectApproachPointForRadius(&from, &target, 2.0f, 0.0f, &approach));
    T_ASSERT(Vector2_distance(&approach, &target) <= 2.0f);
    T_ASSERT(CM_PointIsPathableForRadius(&approach, 0.0f));
    T_ASSERT(CM_LineIsWalkableForRadius(&from, &approach, 0.0f));
}


TEST(wc3_pathfinding, footprint_approach_returns_legal_point_beside_blocked_building) {
    uint8_t blocked_target[MAP_W * MAP_H];
    edict_t *building;
    pathTex_t *pathtex;
    vec2_t from = { 1.0f, 5.0f };
    vec2_t approach = { 0 };

    memset(blocked_target, 0, sizeof(blocked_target));
    blocked_target[5 * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, blocked_target);
    reset_entities();

    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 5.0f, 5.0f);
    pathtex = gi.MemAlloc(sizeof(*pathtex) + sizeof(color32_t));
    T_NOT_NULL(pathtex);
    pathtex->width = 1;
    pathtex->height = 1;
    pathtex->map[0] = (color32_t){ 0, 0, 255, 255 };
    building->pathtex = pathtex;

    T_ASSERT(CM_FindApproachPointToFootprintForRadius(building, &from, 2.0f, 0.0f, &approach));
    T_ASSERT(CM_DistanceToPathingFootprint(building, &approach) <= 2.0f);
    T_ASSERT(CM_PointIsPathableForRadius(&approach, 0.0f));

    building->pathtex = NULL;
    gi.MemFree(pathtex);
}

/* The generic footprint helper intentionally chooses the candidate closest to
 * the mover, which is useful for adaptive staging but can be the OUTERMOST
 * legal point in a broad interaction band.  Return Resources needs the inverse
 * ordering: choose the innermost legal ring first, then preserve the worker's
 * side among points on that ring. */
TEST(wc3_pathfinding, footprint_inner_approach_prefers_contact_ring_over_outer_staging) {
    uint8_t blocked_target[MAP_W * MAP_H];
    edict_t *building;
    pathTex_t *pathtex;
    vec2_t from = { 0.5f, 5.5f };
    vec2_t staging = { 0 }, inner = { 0 };

    memset(blocked_target, 0, sizeof(blocked_target));
    blocked_target[5 * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, blocked_target);
    reset_entities();

    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 5.0f, 5.0f);
    pathtex = gi.MemAlloc(sizeof(*pathtex) + sizeof(color32_t));
    T_NOT_NULL(pathtex);
    pathtex->width = 1;
    pathtex->height = 1;
    pathtex->map[0] = (color32_t){ 0, 0, 255, 255 };
    building->pathtex = pathtex;

    T_ASSERT(CM_FindApproachPointToFootprintForRadius(
        building, &from, 3.0f, 0.0f, &staging));
    T_ASSERT(CM_FindInnerApproachPointToFootprintForRadius(
        building, &from, 3.0f, 0.0f, &inner));
    T_ASSERT(CM_DistanceToPathingFootprint(building, &inner) <
             CM_DistanceToPathingFootprint(building, &staging));
    T_ASSERT(inner.x > staging.x); /* both are on the worker's left/near side */
    T_ASSERT(inner.x < building->s.origin2.x);

    building->pathtex = NULL;
    gi.MemFree(pathtex);
}

/* The fast footprint-approach mask must preserve irregular/sparse pathing
 * textures exactly.  A bounding-box shortcut would incorrectly accept the
 * open middle of this five-cell texture even though it is outside range of
 * either authored blocked pixel. */
TEST(wc3_pathfinding, footprint_approach_respects_sparse_path_texture) {
    uint8_t blocked_target[MAP_W * MAP_H];
    edict_t *building;
    pathTex_t *pathtex;
    vec2_t from = { 5.5f, 5.5f };
    vec2_t approach = { 0 };

    memset(blocked_target, 0, sizeof(blocked_target));
    blocked_target[5 * MAP_W + 3] = 2;
    blocked_target[5 * MAP_W + 7] = 2;
    setup_test_pathmap(MAP_W, MAP_H, blocked_target);
    reset_entities();

    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 5.0f, 5.0f);
    pathtex = gi.MemAlloc(sizeof(*pathtex) + 5 * sizeof(color32_t));
    T_NOT_NULL(pathtex);
    pathtex->width = 5;
    pathtex->height = 1;
    FOR_LOOP(i, 5)
        pathtex->map[i] = (color32_t){ 0, 0, 0, 255 };
    pathtex->map[0].b = 255;
    pathtex->map[4].b = 255;
    building->pathtex = pathtex;

    T_ASSERT(CM_FindApproachPointToFootprintForRadius(
        building, &from, 0.6f, 0.0f, &approach));
    T_ASSERT(CM_DistanceToPathingFootprint(building, &approach) <= 0.6f);
    T_ASSERT(fabsf(approach.x - from.x) >= 0.9f);
    T_ASSERT(CM_PointIsPathableForRadius(&approach, 0.0f));

    building->pathtex = NULL;
    gi.MemFree(pathtex);
}

TEST(wc3_pathfinding, heatmap_rejects_corridor_too_narrow_for_radius) {
    uint8_t corridor[MAP_W * MAP_H];
    memset(corridor, 2, sizeof(corridor));
    for (int x = 0; x < MAP_W; x++)
        corridor[5 * MAP_W + x] = 0;
    setup_test_pathmap(MAP_W, MAP_H, corridor);
    reset_entities();

    edict_t *wp = make_waypoint(8.0f, 5.0f);
    T_ASSERT(CM_BuildHeatmapForRadius(wp, 0.0f) != 0);
    T_EQ(CM_BuildHeatmapForRadius(wp, 1.0f), 0);
}

/* Move steering and move-time collision must use the same footprint.  A point
 * route fits through this one-cell opening, but a radius-one unit does not. */
TEST(wc3_pathfinding, move_order_requests_collision_sized_route) {
    uint8_t gap_map[MAP_W * MAP_H];
    memset(gap_map, 0, sizeof(gap_map));
    for (int y = 2; y <= 6; y++)
        if (y != 5) gap_map[y * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, gap_map);
    reset_entities();

    edict_t *unit = make_unit_at(2.0f, 5.0f);
    edict_t *wp = make_waypoint(8.0f, 5.0f);
    unit->collision = 1.0f;
    order_move(unit, wp);

    T_ASSERT(CM_LineIsWalkableForRadius(&unit->s.origin2, &wp->s.origin2, 0.0f));
    T_ASSERT(!CM_LineIsWalkableForRadius(&unit->s.origin2, &wp->s.origin2, unit->collision));
    T_ASSERT(CM_BuildHeatmapForRadius(wp, unit->collision));
    unit_changeangle(unit);
    T_FEQ(wp->heatmap2_radius, unit->collision, 0.001f);
}

TEST(wc3_pathfinding, patrol_requests_collision_sized_route) {
    uint8_t gap_map[MAP_W * MAP_H];
    memset(gap_map, 0, sizeof(gap_map));
    for (int y = 2; y <= 6; y++)
        if (y != 5) gap_map[y * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, gap_map);
    reset_entities();

    edict_t *unit = make_unit_at(2.0f, 5.0f);
    edict_t *wp = make_waypoint(8.0f, 5.0f);
    unit->collision = 1.0f;
    order_patrol(unit, wp);
    T_ASSERT(CM_BuildHeatmapForRadius(wp, unit->collision));
    unit_changeangle(unit);

    T_FEQ(wp->heatmap2_radius, unit->collision, 0.001f);
}

TEST(wc3_pathfinding, attack_move_requests_collision_sized_route) {
    uint8_t gap_map[MAP_W * MAP_H];
    memset(gap_map, 0, sizeof(gap_map));
    for (int y = 2; y <= 6; y++)
        if (y != 5) gap_map[y * MAP_W + 5] = 2;
    setup_test_pathmap(MAP_W, MAP_H, gap_map);
    reset_entities();

    edict_t *unit = make_unit_at(2.0f, 5.0f);
    edict_t *wp = make_waypoint(8.0f, 5.0f);
    unit->collision = 1.0f;
    order_attackmove(unit, wp);
    T_ASSERT(CM_BuildHeatmapForRadius(wp, unit->collision));
    unit_changeangle(unit);

    T_FEQ(wp->heatmap2_radius, unit->collision, 0.001f);
}

/* -----------------------------------------------------------------------
 * Unit-obstacle separation (fix #2):
 * A live unit entity at a given location must NOT cause the heatmap for
 * the same goal to be rebuilt (unit obstacles are handled by collision
 * resolution, not the heatmap).  We verify by checking that calling
 * CM_BuildHeatmap twice with a unit present returns the same generation.
 * --------------------------------------------------------------------- */

TEST(wc3_pathfinding, unit_presence_does_not_invalidate_heatmap_cache) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    edict_t *wp   = make_waypoint(7.0f, 5.0f);
    edict_t *unit = make_unit_at(3.0f, 5.0f);
    (void)unit;

    uint32_t gen1 = CM_BuildHeatmap(wp);
    uint32_t gen2 = CM_BuildHeatmap(wp);

    T_EQ(gen1, gen2);
}

/* -----------------------------------------------------------------------
 * Static-map point test (CM_PointIsPathableForRadius):
 *
 * The static half of move-time collision.  A point on a wall cell is not
 * pathable; open ground is.  World coordinates are cell/MAP in the test
 * harness (same convention as make_waypoint).
 * --------------------------------------------------------------------- */

TEST(wc3_pathfinding, point_pathable_rejects_wall_accepts_open) {
    build_wall_map();
    setup_test_pathmap(MAP_W, MAP_H, wall_map);
    reset_entities();

    vec2_t wall_pt = { 5.0f, 5.0f }; /* on the wall column */
    vec2_t open_pt = { 2.0f, 5.0f }; /* clear ground */

    T_ASSERT(!CM_PointIsPathableForRadius(&wall_pt, 0.0f));
    T_ASSERT(CM_PointIsPathableForRadius(&open_pt, 0.0f));
}

TEST(wc3_pathfinding, closest_pathable_keeps_exact_open_point) {
    vec2_t point = { 2.25f, 5.75f }, out = {0};
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    T_ASSERT(CM_ClosestPathablePointForRadius(&point, 0, &out));
    T_FEQ(out.x, point.x, 0.001f);
    T_FEQ(out.y, point.y, 0.001f);
}

/* Dead units/buildings are hollow and must not be reintroduced by the
 * command-time dynamic obstacle pass after their static footprint is gone. */
TEST(wc3_pathfinding, closest_pathable_ignores_dead_dynamic_unit) {
    vec2_t point = { 2.0f, 5.0f }, live_out = {0}, dead_out = {0};
    edict_t *blocker;

    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();
    blocker = make_unit_at(point.x, point.y);
    blocker->svflags |= SVF_MONSTER;
    blocker->collision = 0.5f;

    T_ASSERT(CM_ClosestPathablePointForRadius(&point, 0, &live_out));
    T_ASSERT(fabsf(live_out.x - point.x) > 0.001f ||
             fabsf(live_out.y - point.y) > 0.001f);

    blocker->svflags |= SVF_DEADMONSTER;
    T_ASSERT(CM_ClosestPathablePointForRadius(&point, 0, &dead_out));
    T_FEQ(dead_out.x, point.x, 0.001f);
    T_FEQ(dead_out.y, point.y, 0.001f);
}

/* Stock hgry publishes category0 even though its own terrain query is4. */
TEST(wc3_pathfinding, closest_pathable_dynamic_units_use_published_category) {
    uint8_t cells[MAP_W * MAP_H] = { 0 };
    vec2_t point = { 2.0f, 5.0f }, out = { 0 };
    edict_t *blocker;

    setup_test_pathmap(MAP_W, MAP_H, cells);
    reset_entities();
    blocker = make_unit_at(point.x, point.y);
    blocker->svflags |= SVF_MONSTER;
    blocker->collision = 0.5f;
    blocker->aiflags |= AI_FLYING;

    T_ASSERT(CM_ClosestPathablePointForRadiusFlags(&point, 0.0f, CM_PATHING_UNWALKABLE, &out));
    T_FEQ(out.x, point.x, 0.001f);
    T_FEQ(out.y, point.y, 0.001f);
    T_ASSERT(CM_ClosestPathablePointForRadiusFlags(&point, 0.0f, CM_PATHING_UNFLYABLE, &out));
    T_FEQ(out.x, point.x, 0.001f);
    T_FEQ(out.y, point.y, 0.001f);

    blocker->aiflags &= ~AI_FLYING;
    T_ASSERT(CM_ClosestPathablePointForRadiusFlags(&point, 0.0f, CM_PATHING_UNWALKABLE, &out));
    T_ASSERT(fabsf(out.x - point.x) > 0.001f || fabsf(out.y - point.y) > 0.001f);
    T_ASSERT(CM_ClosestPathablePointForRadiusFlags(&point, 0.0f, CM_PATHING_UNFLYABLE, &out));
    T_FEQ(out.x, point.x, 0.001f);
    T_FEQ(out.y, point.y, 0.001f);
}

TEST(wc3_pathfinding, closest_reachable_keeps_exact_reachable_point) {
    vec2_t from = { 1.25f, 5.25f }, target = { 3.75f, 5.75f }, out = {0};
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);

    T_ASSERT(CM_ClosestReachablePointForRadius(&from, &target, 0.0f, &out));
    T_FEQ(out.x, target.x, 0.001f);
    T_FEQ(out.y, target.y, 0.001f);
}

TEST(wc3_pathfinding, closest_reachable_stops_at_disconnected_wall) {
    vec2_t from = { 1.5f, 5.5f }, target = { 8.5f, 5.5f }, out = {0};
    build_split_map();
    setup_test_pathmap(MAP_W, MAP_H, split_map);

    T_ASSERT(CM_ClosestReachablePointForRadius(&from, &target, 0.0f, &out));
    T_FEQ(out.x, 4.5f, 0.001f);
    T_FEQ(out.y, 5.5f, 0.001f);
}

TEST(wc3_pathfinding, closest_reachable_respects_collision_radius) {
    vec2_t from = { 1.5f, 5.5f }, target = { 8.5f, 5.5f }, out = {0};
    build_split_map();
    setup_test_pathmap(MAP_W, MAP_H, split_map);

    T_ASSERT(CM_ClosestReachablePointForRadius(&from, &target, 1.0f, &out));
    T_FEQ(out.x, 3.5f, 0.001f);
    T_FEQ(out.y, 5.5f, 0.001f);
}

/* An impossible footprint must not synchronously reflood the map on every
 * steering tick while the same move order remains active. */
TEST(wc3_pathfinding, movement_throttles_repeated_unreachable_fallback) {
    uint8_t blocked_map[MAP_W * MAP_H];
    uint32_t first_time, second_time, first_calls;
    struct routePerfStats_s stats;
    edict_t *unit, *goal;

    memset(blocked_map, 0, sizeof(blocked_map));
    setup_test_pathmap(MAP_W, MAP_H, blocked_map);
    reset_entities();
    level.time = 1000;
    unit = make_unit_at(100.0f, 100.0f);
    unit->collision = 1.0f;
    goal = make_waypoint(8.5f, 8.5f);
    order_move(unit, goal);
    T_ASSERT(CM_BuildHeatmapForRadius(goal, unit->collision) != 0);

    CM_ResetTestPathPerfStats();
    unit_changeangle(unit);
    T_EQ(unit->movement.flow_fallback_state, MOVE_FALLBACK_RETRY);
    first_time = unit->movement.flow_fallback_time;
    stats = CM_GetTestPathPerfStats();
    first_calls = stats.closest_reachable_calls;
    T_EQ(first_calls, 1);
    unit_changeangle(unit);
    second_time = unit->movement.flow_fallback_time;
    T_EQ(second_time, first_time);
    stats = CM_GetTestPathPerfStats();
    T_EQ(stats.closest_reachable_calls, first_calls);
}

/* A successful fallback retarget must not be recomputed when its move order
 * has already been adjusted once. */
TEST(wc3_pathfinding, movement_remembers_applied_unreachable_fallback) {
    uint8_t split[MAP_W * MAP_H];
    edict_t *unit, *goal;

    build_split_map();
    memcpy(split, split_map, sizeof(split));
    setup_test_pathmap(MAP_W, MAP_H, split);
    reset_entities();
    level.time = 1000;
    unit = make_unit_at(1.5f, 5.5f);
    unit->collision = 1.0f;
    goal = make_waypoint(8.5f, 5.5f);
    order_move(unit, goal);
    T_ASSERT(CM_BuildHeatmapForRadius(goal, unit->collision) != 0);

    unit_changeangle(unit);
    T_EQ(unit->movement.flow_fallback_state, MOVE_FALLBACK_APPLIED);
    T_ASSERT(unit->movement.flow_fallback_goal == goal);
}

/* The flood and the flow must not cut diagonally through a wall corner: with
 * walls at (1,0) and (0,1), the cell (0,0) is boxed off from a goal at (1,1)
 * (squeezing the corner is not a legal move), so its flow is zero, not a
 * diagonal pointing into the corner. */
TEST(wc3_pathfinding, no_diagonal_corner_cutting) {
    uint8_t corner_map[MAP_W * MAP_H];
    memset(corner_map, 0, sizeof(corner_map));
    corner_map[0 * MAP_W + 1] = 2;  /* wall at (1,0) */
    corner_map[1 * MAP_W + 0] = 2;  /* wall at (0,1) */
    setup_test_pathmap(MAP_W, MAP_H, corner_map);
    reset_entities();

    edict_t *wp = make_waypoint(1.0f, 1.0f);  /* goal at cell (1,1) */
    build_flow(wp);

    vec2_t flow = flow_at_cell(0.0f, 0.0f);
    T_FEQ(flow.x, 0.0f, 0.001f);
    T_FEQ(flow.y, 0.0f, 0.001f);
}

/* The direct-line shortcut must obey the same corner rule as the flow field.
 * Otherwise generic movement bypasses routing for the exact ox/xo fence shape
 * and repeatedly asks collision to enter the blocked diagonal gap. */
TEST(wc3_pathfinding, direct_line_rejects_diagonal_corner_cutting) {
    uint8_t corner_map[MAP_W * MAP_H];
    vec2_t start = { 0.5f, 0.5f };
    vec2_t goal = { 1.5f, 1.5f };
    memset(corner_map, 0, sizeof(corner_map));
    corner_map[0 * MAP_W + 1] = 2;
    corner_map[1 * MAP_W + 0] = 2;
    setup_test_pathmap(MAP_W, MAP_H, corner_map);

    T_ASSERT(!CM_LineIsWalkable(&start, &goal));
}

TEST(wc3_pathfinding, closest_reachable_does_not_cross_diagonal_corner) {
    uint8_t corner_map[MAP_W * MAP_H];
    vec2_t start = { 0.5f, 0.5f };
    vec2_t target = { 1.5f, 1.5f };
    vec2_t out = {0};
    memset(corner_map, 0, sizeof(corner_map));
    corner_map[0 * MAP_W + 1] = 2;
    corner_map[1 * MAP_W + 0] = 2;
    setup_test_pathmap(MAP_W, MAP_H, corner_map);

    T_ASSERT(CM_ClosestReachablePointForRadius(&start, &target, 0.0f, &out));
    T_FEQ(out.x, start.x, 0.001f);
    T_FEQ(out.y, start.y, 0.001f);
}

TEST(wc3_pathfinding, movement_rejects_swept_static_obstacle) {
    uint8_t blocked_map[MAP_W * MAP_H];
    vec2_t from = { 0.0f, 0.0f }, to = { 2.0f, 2.0f };
    edict_t *unit;
    memset(blocked_map, 0, sizeof(blocked_map));
    blocked_map[1 * MAP_W + 1] = 2;
    setup_test_pathmap(MAP_W, MAP_H, blocked_map);
    reset_entities();
    unit = make_unit_at(from.x, from.y);
    unit->collision = 0.0f;

    T_ASSERT(!M_MoveIsValid(unit, &to));
}

/* -----------------------------------------------------------------------
 * Flow-field cache consistency
 *
 * After a cache hit, get_flow_direction() must return the same vector as
 * it did immediately after the original build.  This verifies that cached
 * integration prices produce stable on-demand flow across cache lookups.
 * --------------------------------------------------------------------- */

TEST(wc3_pathfinding, flow_cache_consistent_after_hit) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);

    edict_t *wp = make_waypoint(9.0f, 5.0f);
    build_flow(wp);
    vec2_t dir1 = flow_at_cell(2.0f, 5.0f);

    /* Second build of the same goal must hit the cache. */
    build_flow(wp);
    vec2_t dir2 = flow_at_cell(2.0f, 5.0f);

    T_FEQ(dir1.x, dir2.x, 0.001f);
    T_FEQ(dir1.y, dir2.y, 0.001f);
}

/* -----------------------------------------------------------------------
 * Multi-goal flow consistency
 *
 * After building two goals and switching back, the flow for each goal
 * is consistent — switching between cached goals doesn't corrupt directions.
 * --------------------------------------------------------------------- */

TEST(wc3_pathfinding, flow_consistent_across_goal_switches) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);

    edict_t *wp_left  = make_waypoint(1.0f, 5.0f);
    edict_t *wp_right = make_waypoint(9.0f, 5.0f);

    /* Build both goals. */
    build_flow(wp_right);
    vec2_t flow_right = flow_at_cell(5.0f, 5.0f); /* should point right (+x) */

    build_flow(wp_left);
    vec2_t flow_left = flow_at_cell(5.0f, 5.0f);  /* should point left (-x) */

    /* Switch back to right goal from cache. */
    build_flow(wp_right);
    vec2_t flow_right2 = flow_at_cell(5.0f, 5.0f);

    /* Flow directions must be in opposite x halves. */
    T_ASSERT(flow_right.x > 0.0f);
    T_ASSERT(flow_left.x < 0.0f);
    /* Cached right goal must match original. */
    T_FEQ(flow_right.x, flow_right2.x, 0.001f);
}

/* -----------------------------------------------------------------------
 * Proximity shortcut
 *
 * unit_changeangle uses direct vector math when the unit is within
 * a clear direct corridor to its goal, skipping the heatmap.  Verify the unit
 * gets a valid angle pointing toward the goal regardless.
 * --------------------------------------------------------------------- */

TEST(wc3_pathfinding, proximity_shortcut_gives_correct_angle) {
    build_open_map();
    setup_test_pathmap(MAP_W, MAP_H, open_map);
    reset_entities();

    /* Place the unit beside its goal in the open corridor. */
    edict_t *unit = make_unit_at(0.0f, 0.0f);
    edict_t *wp   = make_waypoint(5.0f, 5.0f);
    unit->collision = 0.0f;
    unit->goalentity = wp;
    unit->stand      = unit_stand;
    unit_stand(unit);
    order_move(unit, wp);

    T_FEQ(M_DistanceToGoal(unit), sqrtf(50.0f), 0.001f);

    /* Units now turn gradually (at their turn rate) toward the target facing
     * rather than snapping instantly, so step a few ticks to let the facing
     * converge before checking the final direction. */
    for (int i = 0; i < 16; i++) {
        unit_changeangle(unit);
    }

    /* Angle must point from (0,0) toward the goal. */
    float expected = atan2f(wp->s.origin.y - unit->s.origin.y,
                            wp->s.origin.x - unit->s.origin.x);
    T_FEQ(unit->s.angle, expected, 0.01f);
}

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

TEST(wc3_pathfinding, static_rect_query_handles_subcell_interaction_area) {
    uint8_t cells[4 * 4] = { 0 };
    box2_t rect = { .min = {1.10f, 1.10f}, .max = {1.20f, 1.20f} };
    vec2_t from = {0.25f, 0.25f}, out = {0};

    setup_test_pathmap(4, 4, cells);
    T_ASSERT(G_ClosestStaticPathablePointInRectForRadiusFlags(&from, &rect, 0.0f,
        CM_PATHING_UNWALKABLE, &out));
    T_ASSERT(out.x >= rect.min.x && out.x <= rect.max.x);
    T_ASSERT(out.y >= rect.min.y && out.y <= rect.max.y);
    setup_test_world();
}

TEST(wc3_pathfinding, static_rect_query_skips_blocked_intersecting_cell) {
    uint8_t cells[4 * 4] = { 0 };
    box2_t rect = { .min = {1.10f, 1.10f}, .max = {2.90f, 1.90f} };
    vec2_t from = {1.20f, 1.20f}, out = {0};

    cells[1 * 4 + 1] = CM_PATHING_UNWALKABLE;
    setup_test_pathmap(4, 4, cells);
    T_ASSERT(G_ClosestStaticPathablePointInRectForRadiusFlags(&from, &rect, 0.0f,
        CM_PATHING_UNWALKABLE, &out));
    T_ASSERT(out.x >= 2.0f && out.x <= rect.max.x);
    T_ASSERT(out.y >= rect.min.y && out.y <= rect.max.y);
    setup_test_world();
}

TEST(wc3_pathfinding, static_rect_query_ignores_temporary_unit_occupancy) {
    uint8_t cells[4 * 4] = { 0 };
    box2_t rect = { .min = {1.25f, 1.25f}, .max = {1.75f, 1.75f} };
    vec2_t from = {1.50f, 1.50f}, out = {0};
    edict_t *blocker;

    setup_test_pathmap(4, 4, cells);
    reset_entities();
    blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1.50f, 1.50f);
    blocker->svflags |= SVF_MONSTER;
    blocker->collision = 0.25f;
    blocker->s.model = 1;
    gi.LinkEntity(blocker);
    T_ASSERT(G_ClosestStaticPathablePointInRectForRadiusFlags(&from, &rect, 0.0f,
        CM_PATHING_UNWALKABLE, &out));
    T_FEQ(out.x, from.x, 0.001f);
    T_FEQ(out.y, from.y, 0.001f);
    setup_test_world();
}

TEST(wc3_pathfinding, blight_world_state_uses_wpm_seed_and_survives_static_rebuild) {
    uint8_t cells[10 * 10] = { 0 };
    vec2_t authored = { 16.0f, 16.0f };
    vec2_t runtime = { 144.0f, 144.0f };
    uint8_t saved[100];

    cells[0] = 0x20;
    CM_SetupTestPathmap(10, 10, cells);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = {0, 0}, .max = {320, 320}));
    G_BlightInit();
    T_ASSERT(G_IsPointBlighted(&authored));
    T_ASSERT(!G_IsPointBlighted(&runtime));

    G_SetBlightPoint(&runtime, true);
    T_ASSERT(G_IsPointBlighted(&runtime));
    CM_BakeStaticObstacles();
    T_ASSERT(G_IsPointBlighted(&runtime));

    T_EQ(G_GetBlightStateSize(), sizeof(saved));
    T_ASSERT(G_GetBlightState(saved, sizeof(saved)));
    G_SetBlightPoint(&runtime, false);
    T_ASSERT(!G_IsPointBlighted(&runtime));
    T_ASSERT(G_SetBlightState(saved, sizeof(saved)));
    T_ASSERT(G_IsPointBlighted(&runtime));

    G_BlightShutdown();
    setup_test_world();
}

TEST(pathfinding, admitted_fine_requests_preserve_raw_source_and_same_cell_zero_work) {
    reset_entities(); setup_test_world();
    uint8_t cells[24*24];
    FOR_LOOP(i,sizeof(retail_fine_results)/sizeof(*retail_fine_results)) {
        retailFineResult_t const *r=retail_fine_results+i;
        if(r->kind>3)continue; /* Zero-budget/target-object cases use the complete kernel corpus. */
        memset(cells,0,sizeof(cells));
        if(r->kind==1)cells[4+4*24]=2;
        if(r->kind==2)cells[19+19*24]=2;
        if(r->kind==3)FOR_LOOP(y,24)cells[12+y*24]=2;
        CM_SetupTestWorldBounds(&(box2_t){{0,0},{768,768}}); CM_SetupTestPathmap(24,24,cells);
        vec2_t source={r->source[0]*32,r->source[1]*32},goal={r->goal[0]*32,r->goal[1]*32},
            native_source={r->source[0],r->source[1]},native_goal={r->goal[0],r->goal[1]},selected;
        S_ClearMoveFineRequests();
        edict_t *mover=G_Spawn(); mover->collision=(.25f+.5f*r->cls)*32;
        movePathQuery_t query={.geometry={&source,&goal,mover->collision,2},.units=true,.mover=mover,
            .fine=&native_source,.fine_target=&native_goal};
        moveFineRoute_t route={0}; T_ASSERT(G_BuildUnitMoveLocalRoute(&query,&route,&selected));
        wc3FineSearch_t const *search=G_TestMoveFineSearch();
        T_EQ(search->pops,r->work); T_EQ(search->count,r->nodes);
        T_EQ(route.count,r->count); T_EQ(route.index,r->index);
        T_EQ(route.partial,!!(r->flags&0x10000000));
        if(route.count==r->count)FOR_LOOP(j,route.count) {
            T_EQ(wc3_float_bits(route.points[j].x),r->words[j][0]);
            T_EQ(wc3_float_bits(route.points[j].y),r->words[j][1]);
        }
        free(route.points); G_FreeEdict(mover);
    }
    reset_entities(); setup_test_world();
}

#endif /* BZ_TESTS */
