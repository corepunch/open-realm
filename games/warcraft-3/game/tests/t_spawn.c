#ifdef BZ_TESTS
#include "shared/test.h"
#include "../g_local.h"

TEST(wc3_spawn, map_script_object_filter_keeps_units_and_items_only) {
    T_ASSERT(G_TestMapObjectCreatedByMapScript(MAKEFOURCC('o', 'p', 'e', 'o')));
    T_ASSERT(G_TestMapObjectCreatedByMapScript(MAKEFOURCC('s', 'p', 'r', 'o')));
    T_ASSERT(!G_TestMapObjectCreatedByMapScript(MAKEFOURCC('s', 'l', 'o', 'c')));
}

TEST(wc3_spawn, map_placed_units_and_buildings_start_with_full_mana) {
    edict_t unit = { .mana = { .value = 20.0f, .max_value = 125.0f } };
    edict_t building = { .mana = { .value = 0.0f, .max_value = 300.0f } };
    edict_t no_mana = { .mana = { .value = 0.0f, .max_value = 0.0f } };

    unit.s.flags |= EF_UNIT;
    building.s.flags |= EF_UNIT | EF_BUILDING;
    no_mana.s.flags |= EF_UNIT;
    G_FillMapPlacedUnitMana(&unit);
    G_FillMapPlacedUnitMana(&building);
    G_FillMapPlacedUnitMana(&no_mana);

    T_FEQ(unit.mana.value, 125.0f, 0.001f);
    T_FEQ(building.mana.value, 300.0f, 0.001f);
    T_FEQ(no_mana.mana.value, 0.0f, 0.001f);
}
#endif
