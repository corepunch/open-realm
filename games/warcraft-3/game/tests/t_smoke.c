#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "jass/jass.h"

TEST(wc3_smoke, compress_stat_roundtrip) {
    edictStat_s s = {500.0f, 750.0f};
    uint8_t c = compress_stat(&s);
    T_ASSERT(c > 0);

    edictStat_s s2 = {1.0f, 100.0f};
    uint8_t c2 = compress_stat(&s2);
    T_ASSERT(c2 > 0);
    T_NE(c, c2);
}

TEST(wc3_smoke, region_contains_center) {
    region_t r = {0};
    vec2_t p = {5.0f, 5.0f};
    r.rects[0] = (box2_t){{0.0f, 0.0f}, {10.0f, 10.0f}};
    r.num_rects = 1;
    T_ASSERT(G_RegionContains(&r, &p));
}

TEST(wc3_smoke, region_rejects_outside) {
    region_t r = {0};
    vec2_t p = {32.0f, 32.0f};
    r.rects[0] = (box2_t){{0.0f, 0.0f}, {10.0f, 10.0f}};
    r.num_rects = 1;
    T_ASSERT(!G_RegionContains(&r, &p));
}
/* This also runs in the Android APK: its allocated type pointers carry heap tags. */
extern jassModule_t jass_funcs[];

TEST(wc3_smoke, declared_handle_types_survive_copy_clear_and_reassignment) {
    char source[] =
        "type mapcontrol extends handle\n"
        "native ConvertMapControl takes integer i returns mapcontrol\n"
        "native BJassAssert takes boolean cond, string message returns nothing\n"
        "globals\n"
        "constant mapcontrol USER = ConvertMapControl(0)\n"
        "endglobals\n"
        "function main takes nothing returns nothing\n"
        "local mapcontrol copied = USER\n"
        "call BJassAssert(copied == ConvertMapControl(0), \"declared enum type lost\")\n"
        "set copied = null\n"
        "call BJassAssert(copied == null, \"handle clear failed\")\n"
        "set copied = ConvertMapControl(7)\n"
        "call BJassAssert(copied == ConvertMapControl(7), \"reassigned enum type lost\")\n"
        "call BJassAssert(copied != USER, \"different enum values compare equal\")\n"
        "endfunction\n";
    jass_sethost(&(jassHost_t){ .MemAlloc = gi.MemAlloc, .MemFree = gi.MemFree, .natives = jass_funcs });
    jass_t *vm = jass_newstate();
    jass_dobuffer(vm, source);
    jass_callbyname(vm, "main", true);
    jass_runevents(vm);
    T_ASSERT(!jass_rterror_pending(vm));
    jass_close(vm);
}
#endif
