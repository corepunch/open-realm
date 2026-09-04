#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"

TEST(wc3_smoke, compress_stat_roundtrip) {
    edictStat_s s = {500.0f, 750.0f};
    BYTE c = compress_stat(&s);
    T_ASSERT(c > 0);

    edictStat_s s2 = {1.0f, 100.0f};
    BYTE c2 = compress_stat(&s2);
    T_ASSERT(c2 > 0);
    T_NE(c, c2);
}

TEST(wc3_smoke, region_contains_center) {
    REGION r = {0};
    VECTOR2 p = {5.0f, 5.0f};
    r.rects[0] = (BOX2){{0.0f, 0.0f}, {10.0f, 10.0f}};
    r.num_rects = 1;
    T_ASSERT(G_RegionContains(&r, &p));
}

TEST(wc3_smoke, region_rejects_outside) {
    REGION r = {0};
    VECTOR2 p = {15.0f, 15.0f};
    r.rects[0] = (BOX2){{0.0f, 0.0f}, {10.0f, 10.0f}};
    r.num_rects = 1;
    T_ASSERT(!G_RegionContains(&r, &p));
}
#endif
