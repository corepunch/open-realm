/*
 * test_collision.c — Collision resolution and TGA pathfinding texture tests.
 *
 * Covered scenarios:
 *
 *  G_PushEntity
 *    - moves entity by the specified distance in the given direction
 *
 *  Move-time collision (block-and-slide, unit_trymove in g_ai.c)
 *    - an idle unit is a hard, immovable obstacle (never pushed)
 *    - a mover slides around an obstacle between it and its goal
 *    - units that start overlapped can slide apart (penetration rule)
 *    - flyers and ground units are separate collision layers
 *    - hollow (dead/hidden) entities are ignored by collision
 *
 *  LoadTGA (g_pathing.c)
 *    - a minimal 1×1 8-bit grayscale TGA is decoded correctly
 *    - a minimal 2×2 24-bit RGB TGA is decoded correctly
 *    - unsupported image types return NULL
 */

#include <math.h>
#include <string.h>
#include "test_framework.h"
#include "test_harness.h"

/* -----------------------------------------------------------------------
 * Helpers
 * --------------------------------------------------------------------- */

/*
 * Create a live, dynamic (MOVETYPE_STEP) unit suitable for collision
 * testing.  Setting s.model to a non-zero value is required so that
 * IS_HOLLOW() evaluates to false.
 */
static LPEDICT make_collision_unit(FLOAT x, FLOAT y, FLOAT radius) {
    LPEDICT ent   = alloc_test_unit(UNIT_ID("hpea"), x, y);
    ent->movetype  = MOVETYPE_STEP;
    ent->collision = radius;
    ent->s.model   = 1;   /* IS_HOLLOW requires s.model != 0 */
    ent->stand     = unit_stand;
    unit_stand(ent);
    gi.LinkEntity(ent);   /* update bounds after setting collision */
    return ent;
}

/* Distance between two 2-D origins. */
static FLOAT dist2(LPCVECTOR2 a, LPCVECTOR2 b) {
    FLOAT dx = a->x - b->x;
    FLOAT dy = a->y - b->y;
    return sqrtf(dx*dx + dy*dy);
}

/* Distance from point p to segment [a,b] — mirrors the swept test in g_ai.c so a
 * test can assert a unit's per-tick path never crossed a blocker. */
static FLOAT seg_dist(LPCVECTOR2 a, LPCVECTOR2 b, LPCVECTOR2 p) {
    FLOAT abx = b->x - a->x, aby = b->y - a->y;
    FLOAT ab2 = abx*abx + aby*aby;
    FLOAT t = ab2 > 0.0001f ? ((p->x - a->x)*abx + (p->y - a->y)*aby) / ab2 : 0.0f;
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    VECTOR2 c = { a->x + t*abx, a->y + t*aby };
    return dist2(&c, p);
}

/* Defined in routing.c (test builds only). */
void CM_SetupTestPathmap(DWORD width, DWORD height, BYTE const *cells);

/* Reset the entity pool and clear any pathmap a previous suite left loaded so
 * these unit-vs-unit collision tests run with passthrough CM_* (no terrain).
 * The pathfinding suite loads a small synthetic pathmap; left in place it would
 * make the far-apart world coordinates used here resolve as unwalkable (and
 * routes CM_ClosestPathablePointForRadius through code that needs a live world). */
static void reset_collision_world(void) {
    reset_entities();
    CM_SetupTestPathmap(0, 0, NULL);
}

/* -----------------------------------------------------------------------
 * G_PushEntity tests
 * --------------------------------------------------------------------- */

static void test_push_entity_moves_in_direction(void) {
    reset_entities();
    LPEDICT ent = make_collision_unit(0.0f, 0.0f, 0.0f);
    VECTOR2 dir = {1.0f, 0.0f};
    G_PushEntity(ent, 50.0f, &dir);
    ASSERT_EQ_FLOAT(ent->s.origin2.x, 50.0f, 0.01f);
    ASSERT_EQ_FLOAT(ent->s.origin2.y,  0.0f, 0.01f);
}

static void test_push_entity_negative_distance_moves_back(void) {
    reset_entities();
    LPEDICT ent = make_collision_unit(100.0f, 0.0f, 0.0f);
    VECTOR2 dir = {1.0f, 0.0f};
    G_PushEntity(ent, -30.0f, &dir);
    ASSERT_EQ_FLOAT(ent->s.origin2.x, 70.0f, 0.01f);
}

/* Step a unit's move-order think loop for up to `frames`, stopping early once
 * it leaves the walk state.  Tracks the closest it ever came to `other` so a
 * test can assert the mover never penetrated another unit's collision circle. */
static FLOAT run_move_tracking_min_dist(LPEDICT mover, LPEDICT other, int frames) {
    FLOAT min_dist = other ? dist2(&mover->s.origin2, &other->s.origin2) : 0.0f;
    for (int i = 0; i < frames; i++) {
        if (!mover->currentmove || strcmp(mover->currentmove->animation, "walk") != 0)
            break;
        mover->currentmove->think(mover);
        if (other) {
            FLOAT d = dist2(&mover->s.origin2, &other->s.origin2);
            if (d < min_dist) min_dist = d;
        }
    }
    return min_dist;
}

/* -----------------------------------------------------------------------
 * Move-time collision — block and slide
 *
 * Collision is now enforced when a unit steps (unit_trymove in g_ai.c), driven
 * here through the public order path (unit_issueorder + currentmove->think).
 * The old post-move push solver is retired, so these tests assert the WC3
 * invariant it violated: walking into a unit never displaces that unit.
 * --------------------------------------------------------------------- */

/* The headline regression guard: a stationary unit is a hard, immovable
 * obstacle.  A unit ordered straight into it never pushes it and never
 * penetrates its collision circle. */
static void test_idle_unit_is_immovable_obstacle(void) {
    reset_collision_world();
    LPEDICT blocker = make_collision_unit(50.0f, 0.0f, 16.0f);  /* idle */
    LPEDICT mover   = make_collision_unit( 0.0f, 0.0f, 16.0f);
    VECTOR2 b0 = blocker->s.origin2;
    VECTOR2 dest = {50.0f, 0.0f};

    unit_issueorder(mover, "move", &dest);
    FLOAT min_dist = run_move_tracking_min_dist(mover, blocker, 40);

    /* Blocker was never pushed. */
    ASSERT_EQ_FLOAT(blocker->s.origin2.x, b0.x, 0.001f);
    ASSERT_EQ_FLOAT(blocker->s.origin2.y, b0.y, 0.001f);
    /* Mover never overlapped the blocker. */
    ASSERT(min_dist >= blocker->collision + mover->collision - 0.5f);
}

/* When an obstacle sits between a mover and its goal, the mover slides around
 * it (developing lateral motion) instead of pushing through — and reaches the
 * far side.  The obstacle stays put. */
static void test_mover_slides_around_idle_unit(void) {
    reset_collision_world();
    LPEDICT blocker = make_collision_unit(60.0f, 0.0f, 16.0f);
    LPEDICT mover   = make_collision_unit( 0.0f, 0.0f, 16.0f);
    VECTOR2 dest = {120.0f, 0.0f};

    unit_issueorder(mover, "move", &dest);
    BOOL went_lateral = false;
    for (int i = 0; i < 80; i++) {
        if (!mover->currentmove || strcmp(mover->currentmove->animation, "walk") != 0)
            break;
        mover->currentmove->think(mover);
        if (fabsf(mover->s.origin2.y) > 1.0f) went_lateral = true;
    }

    ASSERT(went_lateral);                                  /* slid around */
    ASSERT_EQ_FLOAT(blocker->s.origin2.x, 60.0f, 0.001f);  /* not pushed */
    ASSERT_EQ_FLOAT(blocker->s.origin2.y,  0.0f, 0.001f);
    ASSERT(mover->s.origin2.x > 60.0f);                    /* got past it */
}

/* Units that start overlapping (spawn / blink / a building dropped on them)
 * can still slide apart: the penetration rule allows a step that does not move
 * closer to the overlapped neighbour. */
static void test_overlapped_units_separate_on_move(void) {
    reset_collision_world();
    LPEDICT a = make_collision_unit(0.0f,  0.0f, 16.0f);
    LPEDICT b = make_collision_unit(10.0f, 0.0f, 16.0f);  /* overlaps a (dist 10 < 32) */
    FLOAT d0 = dist2(&a->s.origin2, &b->s.origin2);
    VECTOR2 dest = {-100.0f, 0.0f};

    unit_issueorder(a, "move", &dest);
    for (int i = 0; i < 5; i++) {
        if (!a->currentmove || strcmp(a->currentmove->animation, "walk") != 0) break;
        a->currentmove->think(a);
    }

    ASSERT(dist2(&a->s.origin2, &b->s.origin2) > d0);  /* separated */
    ASSERT_EQ_FLOAT(b->s.origin2.x, 10.0f, 0.001f);    /* idle b not pushed */
}

/* Air and ground are separate collision layers: a flyer passes straight over a
 * ground unit (no block, no slide) and never pushes it. */
static void test_flyer_passes_over_ground_unit(void) {
    reset_collision_world();
    LPEDICT ground = make_collision_unit(50.0f, 0.0f, 16.0f);  /* idle ground */
    LPEDICT flyer  = make_collision_unit( 0.0f, 0.0f, 16.0f);
    flyer->aiflags |= AI_FLYING;
    VECTOR2 dest = {100.0f, 0.0f};

    unit_issueorder(flyer, "move", &dest);
    run_move_tracking_min_dist(flyer, NULL, 20);

    ASSERT(flyer->s.origin2.x > 50.0f);                 /* not blocked */
    ASSERT(fabsf(flyer->s.origin2.y) < 1.0f);           /* flew straight */
    ASSERT_EQ_FLOAT(ground->s.origin2.x, 50.0f, 0.001f);/* not pushed */
}

/* Hollow (dead/hidden) entities are not collision obstacles. */
static void test_mover_passes_through_dead_unit(void) {
    reset_collision_world();
    LPEDICT dead  = make_collision_unit(50.0f, 0.0f, 16.0f);
    dead->svflags |= SVF_DEADMONSTER;  /* IS_HOLLOW == true */
    LPEDICT mover = make_collision_unit(0.0f, 0.0f, 16.0f);
    VECTOR2 dest = {100.0f, 0.0f};

    unit_issueorder(mover, "move", &dest);
    run_move_tracking_min_dist(mover, NULL, 20);

    ASSERT(mover->s.origin2.x > 50.0f);        /* walked the straight line through */
    ASSERT(fabsf(mover->s.origin2.y) < 1.0f);
}

/* Order a mover east past a stationary *moving* blocker directly in its path and
 * return the mover's peak lateral deviation.  Mover speed is fixed across calls
 * so only the give-way ring count (faster holds line vs slower swings wide)
 * changes the result. */
static FLOAT peak_lateral_against_blocker(FLOAT mover_speed, FLOAT blocker_speed) {
    reset_collision_world();
    LPEDICT mover   = make_collision_unit( 0.0f, 0.0f, 16.0f);
    LPEDICT blocker = make_collision_unit(45.0f, 0.0f, 16.0f);
    mover->unitinfo.MoveSpeed   = mover_speed;
    blocker->unitinfo.MoveSpeed = blocker_speed;
    VECTOR2 dest = {300.0f, 0.0f};
    unit_issueorder(blocker, "move", &dest);   /* blocker is in the walking state */
    unit_issueorder(mover, "move", &dest);     /* (only the mover is stepped)     */
    FLOAT peak = 0.0f;
    for (int i = 0; i < 10; i++) {
        if (!mover->currentmove || strcmp(mover->currentmove->animation, "walk") != 0) break;
        mover->currentmove->think(mover);
        FLOAT const lat = fabsf(mover->s.origin2.y);
        if (lat > peak) peak = lat;
    }
    return peak;
}

/* Speed-priority give-way (RE finding): the slower unit yields to the faster.
 * A faster mover holds its line (narrow slide) against a slower mover; a slower
 * mover swings wide to get around a faster one. */
static void test_faster_unit_holds_line_slower_yields(void) {
    FLOAT lateral_when_faster = peak_lateral_against_blocker(200.0f, 100.0f);
    FLOAT lateral_when_slower = peak_lateral_against_blocker(200.0f, 300.0f);
    ASSERT(lateral_when_faster < lateral_when_slower);
}

/* A very fast unit (per-tick step larger than a unit) ordered straight at a
 * stationary unit must NOT jump clean over it between ticks — the swept-circle
 * test blocks the path, not just the endpoint.  Assert the mover's per-tick
 * path never crosses the blocker's combined-radius circle. */
static void test_fast_unit_cannot_jump_through(void) {
    reset_collision_world();
    LPEDICT blocker = make_collision_unit(40.0f, 0.0f, 16.0f);
    LPEDICT mover   = make_collision_unit( 0.0f, 0.0f, 16.0f);
    mover->unitinfo.MoveSpeed = 1000.0f;  /* ~100 units/tick, well over a unit width */
    FLOAT const rr = mover->collision + blocker->collision;
    VECTOR2 dest = {200.0f, 0.0f};
    unit_issueorder(mover, "move", &dest);

    VECTOR2 prev = mover->s.origin2;
    for (int i = 0; i < 10; i++) {
        if (!mover->currentmove || strcmp(mover->currentmove->animation, "walk") != 0) break;
        mover->currentmove->think(mover);
        ASSERT(seg_dist(&prev, &mover->s.origin2, &blocker->s.origin2) >= rr - 1.0f);
        prev = mover->s.origin2;
    }
    ASSERT_EQ_FLOAT(blocker->s.origin2.x, 40.0f, 0.001f);  /* blocker never pushed */
}

/* -----------------------------------------------------------------------
 * LoadTGA — pathfinding texture decoding
 * --------------------------------------------------------------------- */

/*
 * Build a minimal packed TGA image in a byte buffer and return its size.
 * image_type 3 = uncompressed grayscale, pixel_size 8.
 *
 * The layout below intentionally mirrors tgaHeader_t defined in
 * g_pathing.c (same fields, same pack(1) alignment).  If that struct
 * ever changes, this one must be updated to match.
 */
#pragma pack(push, 1)
typedef struct {
    BYTE  id_length, colormap_type, image_type;
    WORD  colormap_index, colormap_length;
    BYTE  colormap_size;
    WORD  x_origin, y_origin, width, height;
    BYTE  pixel_size, attributes;
} test_tga_hdr_t;   /* mirrors tgaHeader_t from g_pathing.c */
#pragma pack(pop)

static size_t make_tga_grayscale_1x1(BYTE buf[static 32], BYTE gray) {
    test_tga_hdr_t hdr = {0};
    hdr.image_type  = 3;
    hdr.width       = 1;
    hdr.height      = 1;
    hdr.pixel_size  = 8;
    memcpy(buf, &hdr, sizeof(hdr));
    buf[sizeof(hdr)] = gray;
    return sizeof(hdr) + 1;
}

static size_t make_tga_rgb_2x2(BYTE buf[static 64]) {
    test_tga_hdr_t hdr = {0};
    hdr.image_type  = 2;
    hdr.width       = 2;
    hdr.height      = 2;
    hdr.pixel_size  = 24;
    memcpy(buf, &hdr, sizeof(hdr));
    /* 4 pixels × 3 bytes: blue, green, red (BGR in TGA). */
    BYTE *px = buf + sizeof(hdr);
    /* pixel (0,0): R=0xFF G=0x00 B=0x00 → stored BGR */
    *px++ = 0x00; *px++ = 0x00; *px++ = 0xFF;
    /* pixel (1,0): G=0xFF */
    *px++ = 0x00; *px++ = 0xFF; *px++ = 0x00;
    /* pixel (0,1) */
    *px++ = 0xFF; *px++ = 0x00; *px++ = 0x00;
    /* pixel (1,1) */
    *px++ = 0x80; *px++ = 0x80; *px++ = 0x80;
    return sizeof(hdr) + 4 * 3;
}

static size_t make_tga_bgra_1x1(BYTE buf[static 64], BYTE b, BYTE g, BYTE r, BYTE a) {
    test_tga_hdr_t hdr = {0};
    hdr.image_type  = 2;
    hdr.width       = 1;
    hdr.height      = 1;
    hdr.pixel_size  = 32;
    memcpy(buf, &hdr, sizeof(hdr));
    BYTE *px = buf + sizeof(hdr);
    px[0] = b;
    px[1] = g;
    px[2] = r;
    px[3] = a;
    return sizeof(hdr) + 4;
}

static size_t make_tga_grayscale_1x1_with_id(BYTE buf[static 64], BYTE gray, BYTE id_len) {
    test_tga_hdr_t hdr = {0};
    hdr.id_length   = id_len;
    hdr.image_type  = 3;
    hdr.width       = 1;
    hdr.height      = 1;
    hdr.pixel_size  = 8;
    memcpy(buf, &hdr, sizeof(hdr));
    memset(buf + sizeof(hdr), 0xEE, id_len);
    buf[sizeof(hdr) + id_len] = gray;
    return sizeof(hdr) + id_len + 1;
}

static void test_load_tga_grayscale_1x1_dimensions(void) {
    BYTE buf[64];
    size_t sz = make_tga_grayscale_1x1(buf, 0xAB);
    pathTex_t *tex = LoadTGA(buf, sz);
    ASSERT_NOT_NULL(tex);
    ASSERT_EQ_INT(tex->width,  1);
    ASSERT_EQ_INT(tex->height, 1);
    gi.MemFree(tex);
}

static void test_load_tga_grayscale_pixel_value(void) {
    BYTE buf[64];
    size_t sz = make_tga_grayscale_1x1(buf, 0xAB);
    pathTex_t *tex = LoadTGA(buf, sz);
    ASSERT_NOT_NULL(tex);
    /* Grayscale pixel is replicated to R, G, B; alpha = 0xFF. */
    ASSERT_EQ_INT(tex->map[0].r, 0xAB);
    ASSERT_EQ_INT(tex->map[0].g, 0xAB);
    ASSERT_EQ_INT(tex->map[0].b, 0xAB);
    ASSERT_EQ_INT(tex->map[0].a, 0xFF);
    gi.MemFree(tex);
}

static void test_load_tga_rgb_2x2_dimensions(void) {
    BYTE buf[128];
    size_t sz = make_tga_rgb_2x2(buf);
    pathTex_t *tex = LoadTGA(buf, sz);
    ASSERT_NOT_NULL(tex);
    ASSERT_EQ_INT(tex->width,  2);
    ASSERT_EQ_INT(tex->height, 2);
    gi.MemFree(tex);
}

static void test_load_tga_rgba_channel_order(void) {
    BYTE buf[64];
    size_t sz = make_tga_bgra_1x1(buf, 0x00, 0x00, 0xFF, 0x7A);
    pathTex_t *tex = LoadTGA(buf, sz);
    ASSERT_NOT_NULL(tex);
    /* Loader does not remap BGRA bytes; WC3 pathing's file red (unwalkable) is read from COLOR32.b. */
    ASSERT_EQ_INT(tex->map[0].r, 0x00);
    ASSERT_EQ_INT(tex->map[0].g, 0x00);
    ASSERT_EQ_INT(tex->map[0].b, 0xFF);
    ASSERT_EQ_INT(tex->map[0].a, 0x7A);
    gi.MemFree(tex);
}

static void test_load_tga_grayscale_with_id_field_skips_id_bytes(void) {
    BYTE buf[64];
    size_t sz = make_tga_grayscale_1x1_with_id(buf, 0x7C, 3);
    pathTex_t *tex = LoadTGA(buf, sz);
    ASSERT_NOT_NULL(tex);
    ASSERT_EQ_INT(tex->map[0].r, 0x7C);
    ASSERT_EQ_INT(tex->map[0].g, 0x7C);
    ASSERT_EQ_INT(tex->map[0].b, 0x7C);
    gi.MemFree(tex);
}

static void test_load_tga_colormap_not_supported_returns_null(void) {
    BYTE buf[64] = {0};
    test_tga_hdr_t *hdr = (test_tga_hdr_t *)buf;
    hdr->image_type    = 2;
    hdr->colormap_type = 1; /* colormapped images are unsupported by LoadTGA */
    hdr->width         = 1;
    hdr->height        = 1;
    hdr->pixel_size    = 24;
    pathTex_t *tex = LoadTGA(buf, sizeof(buf));
    ASSERT_NULL(tex);
}

static void test_load_tga_unsupported_type_returns_null(void) {
    BYTE buf[64] = {0};
    test_tga_hdr_t *hdr = (test_tga_hdr_t *)buf;
    hdr->image_type = 10; /* RLE-compressed — not supported */
    hdr->width      = 1;
    hdr->height     = 1;
    hdr->pixel_size = 8;
    pathTex_t *tex = LoadTGA(buf, sizeof(buf));
    ASSERT_NULL(tex);
}

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

BEGIN_SUITE(collision)
    RUN_TEST(test_push_entity_moves_in_direction);
    RUN_TEST(test_push_entity_negative_distance_moves_back);

    RUN_TEST(test_idle_unit_is_immovable_obstacle);
    RUN_TEST(test_mover_slides_around_idle_unit);
    RUN_TEST(test_overlapped_units_separate_on_move);
    RUN_TEST(test_flyer_passes_over_ground_unit);
    RUN_TEST(test_mover_passes_through_dead_unit);
    RUN_TEST(test_faster_unit_holds_line_slower_yields);
    RUN_TEST(test_fast_unit_cannot_jump_through);

    RUN_TEST(test_load_tga_grayscale_1x1_dimensions);
    RUN_TEST(test_load_tga_grayscale_pixel_value);
    RUN_TEST(test_load_tga_rgb_2x2_dimensions);
    RUN_TEST(test_load_tga_rgba_channel_order);
    RUN_TEST(test_load_tga_grayscale_with_id_field_skips_id_bytes);
    RUN_TEST(test_load_tga_colormap_not_supported_returns_null);
    RUN_TEST(test_load_tga_unsupported_type_returns_null);
END_SUITE()
