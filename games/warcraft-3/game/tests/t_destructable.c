/*
 * t_destructable.c â€” Breakable destructable lifecycle tests.
 *
 * Covers placement life/flags, normal combat damage, callback-independent
 * one-time death, targetability, death animation state, and reversible static
 * pathing footprints.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"

void setup_test_pathmap(DWORD width, DWORD height, BYTE const *cells);
void setup_test_world(void);
BOOL unit_issuetargetorder(LPEDICT self, LPCSTR order, LPEDICT target);
void T_Damage(LPEDICT target, LPEDICT attacker, int damage);
BOOL run_test_jass(LPCSTR src);
sheetRow_t *parse_slk_string(const char *slk_text);
void free_slk_rows(sheetRow_t *rows);

typedef struct {
    WORD width;
    WORD height;
    COLOR32 map[1];
} one_cell_pathtex_t;

static one_cell_pathtex_t destructable_blocked_death_pathtex = {
    .width = 1,
    .height = 1,
    .map = { { 0, 0, 1, 255 } },
};

static LPEDICT make_test_destructable(FLOAT life, FLOAT x, FLOAT y) {
    LPEDICT ent = G_Spawn();

    ent->class_id = MAKEFOURCC('B', '0', '0', 'X');
    ent->s.class_id = ent->class_id;
    ent->s.model = 1;
    ent->s.scale = 1.0f;
    ent->s.origin = (VECTOR3){ x, y, 0.0f };
    ent->targtype = TARG_DEBRIS;
    ent->health.value = life;
    ent->health.max_value = life;
    ent->destructable.initialized = true;
    ent->destructable.placement_solid = true;
    ent->destructable.alive_collision = 1.0f;
    ent->destructable.pathing_active = true;
    ent->collision = 1.0f;
    return ent;
}

static LPEDICT make_destructable_test_attacker(FLOAT x, FLOAT y) {
    LPEDICT ent = G_Spawn();

    ent->class_id = MAKEFOURCC('h', 'f', 'o', 'o');
    ent->s.class_id = ent->class_id;
    ent->s.model = 1;
    ent->s.origin = (VECTOR3){ x, y, 0.0f };
    ent->health.value = 100.0f;
    ent->health.max_value = 100.0f;
    ent->svflags |= SVF_MONSTER;
    return ent;
}

TEST(wc3_destructable, placement_applies_life_flags_and_editor_id) {
    LPEDICT dest = make_test_destructable(200.0f, 0.0f, 0.0f);
    DOODAD placement = {
        .flags = 2,
        .treeLife = 40,
        .unitID = 12345,
    };

    G_InitializeDestructablePlacement(dest, &placement);

    T_FEQ(dest->health.value, 80.0f, 0.01f);
    T_EQ(dest->destructable.editor_id, 12345);
    T_ASSERT(dest->destructable.placement_solid);
    T_ASSERT(dest->destructable.pathing_active);
    T_ASSERT(!(dest->s.renderfx & RF_HIDDEN));
    T_ASSERT(G_DestructableIsAttackable(dest));
}

TEST(wc3_destructable, hidden_nonsolid_placement_is_not_targetable) {
    LPEDICT dest = make_test_destructable(100.0f, 0.0f, 0.0f);
    DOODAD placement = { .flags = 0, .treeLife = 100 };

    G_InitializeDestructablePlacement(dest, &placement);

    T_ASSERT(dest->s.renderfx & RF_HIDDEN);
    T_ASSERT(dest->s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(!dest->destructable.placement_solid);
    T_ASSERT(!dest->destructable.pathing_active);
    T_ASSERT(!G_DestructableIsAttackable(dest));
}

TEST(wc3_destructable, lethal_damage_does_not_require_die_callback) {
    LPEDICT dest = make_test_destructable(25.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_destructable_test_attacker(10.0f, 0.0f);

    dest->die = NULL;
    T_Damage(dest, attacker, 25);

    T_ASSERT(dest->destructable.dead);
    T_FEQ(dest->health.value, 0.0f, 0.01f);
    T_ASSERT(dest->svflags & SVF_DEADMONSTER);
    T_ASSERT(dest->s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(!(dest->s.renderfx & RF_HIDDEN));
    T_ASSERT(!G_DestructableIsAttackable(dest));
    T_NOT_NULL(dest->currentmove);
    T_STREQ(dest->currentmove->animation, "death");
}

static int death_callback_count;

static void count_death_callback(LPEDICT self, LPEDICT attacker) {
    (void)self;
    (void)attacker;
    death_callback_count++;
}

TEST(wc3_destructable, death_transition_event_and_callback_fire_once) {
    LPEDICT dest = make_test_destructable(10.0f, 0.0f, 0.0f);
    LPEDICT attacker = make_destructable_test_attacker(10.0f, 0.0f);

    death_callback_count = 0;
    dest->die = count_death_callback;
    T_Damage(dest, attacker, 10);
    T_Damage(dest, attacker, 10);
    G_KillDestructable(dest, attacker);

    T_EQ(death_callback_count, 1);
    T_EQ(level.events.write, 1);
    T_EQ(level.events.queue[0].type, EVENT_UNIT_DEATH);
    T_ASSERT(level.events.queue[0].edict == dest);
    T_ASSERT(level.events.queue[0].source == attacker);
}

TEST(wc3_destructable, smart_order_attacks_neutral_destructable) {
    LPEDICT attacker = make_destructable_test_attacker(0.0f, 0.0f);
    LPEDICT dest = make_test_destructable(50.0f, 32.0f, 0.0f);

    dest->s.player = PLAYER_NEUTRAL_PASSIVE;

    T_ASSERT(unit_issuetargetorder(attacker, "smart", dest));
    T_ASSERT(attacker->goalentity == dest);
    T_ASSERT(attacker->combatentity == dest);
}

TEST(wc3_destructable, dead_remains_reject_attack_orders) {
    LPEDICT attacker = make_destructable_test_attacker(0.0f, 0.0f);
    LPEDICT dest = make_test_destructable(1.0f, 32.0f, 0.0f);

    G_KillDestructable(dest, attacker);

    T_ASSERT(!unit_issuetargetorder(attacker, "smart", dest));
    T_ASSERT(!unit_issuetargetorder(attacker, "attack", dest));
}

TEST(wc3_destructable, death_removes_alive_static_footprint) {
    BYTE cells[8 * 8] = { 0 };
    VECTOR2 center = { 4.0f, 4.0f };
    LPEDICT dest;

    setup_test_pathmap(8, 8, cells);
    dest = make_test_destructable(10.0f, center.x, center.y);
    CM_BakeStaticObstacles();
    T_ASSERT(!CM_PointIsPathableForRadius(&center, 0.0f));

    G_KillDestructable(dest, NULL);
    T_ASSERT(CM_PointIsPathableForRadius(&center, 0.0f));
}

TEST(wc3_destructable, death_replacement_pathing_remains_blocking) {
    BYTE cells[8 * 8] = { 0 };
    VECTOR2 center = { 4.0f, 4.0f };
    LPEDICT dest;

    setup_test_pathmap(8, 8, cells);
    dest = make_test_destructable(10.0f, center.x, center.y);
    dest->destructable.death_pathtex = (pathTex_t *)&destructable_blocked_death_pathtex;

    G_KillDestructable(dest, NULL);

    T_ASSERT(dest->destructable.pathing_active);
    T_ASSERT(dest->pathtex == (pathTex_t *)&destructable_blocked_death_pathtex);
    T_ASSERT(!CM_PointIsPathableForRadius(&center, 0.0f));
}

TEST(wc3_destructable, placement_retains_inline_drop_sets) {
    droppableItem_t entries[] = {
        { MAKEFOURCC('r', 'a', 't', 'f'), 100 },
    };
    droppableItemSet_t sets[] = {
        { 1, entries },
    };
    DOODAD placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = (DWORD)-1,
        .num_droppedItemSets = 1,
        .droppableItemSets = sets,
    };
    LPEDICT dest = make_test_destructable(100.0f, 0.0f, 0.0f);

    G_InitializeDestructablePlacement(dest, &placement);

    T_ASSERT(dest->destructable.drop_sets == sets);
    T_EQ(ARRAY_COUNT(dest->destructable.drop_sets), 1);
    T_EQ(dest->destructable.item_table, (DWORD)-1);
    T_ASSERT(!dest->destructable.loot_processed);
}

TEST(wc3_destructable, weighted_inline_drop_selection_honors_boundaries_and_remainder) {
    droppableItem_t entries[] = {
        { MAKEFOURCC('r', 'a', 't', 'f'), 30 },
        { MAKEFOURCC('r', 'd', 'e', '2'), 20 },
    };

    T_EQ(G_SelectDropItem(entries, 2, 0), entries[0].itemID);
    T_EQ(G_SelectDropItem(entries, 2, 29), entries[0].itemID);
    T_EQ(G_SelectDropItem(entries, 2, 30), entries[1].itemID);
    T_EQ(G_SelectDropItem(entries, 2, 49), entries[1].itemID);
    T_EQ(G_SelectDropItem(entries, 2, 50), 0);
    T_EQ(G_SelectDropItem(entries, 2, 99), 0);
    T_EQ(G_SelectDropItem(entries, 2, 100), 0);
}

TEST(wc3_destructable, death_spawns_each_inline_result_once_as_world_item) {
    droppableItem_t first_entries[] = {
        { MAKEFOURCC('r', 'a', 't', 'f'), 100 },
    };
    droppableItem_t second_entries[] = {
        { MAKEFOURCC('r', 'd', 'e', '2'), 100 },
    };
    droppableItemSet_t sets[] = {
        { 1, first_entries },
        { 1, second_entries },
    };
    DOODAD placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = (DWORD)-1,
        .num_droppedItemSets = 2,
        .droppableItemSets = sets,
    };
    LPEDICT dest;
    LPEDICT first;
    LPEDICT second;
    DWORD first_item;

    setup_test_world();
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    first_item = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, first_item + 2);
    first = &g_edicts[first_item];
    second = &g_edicts[first_item + 1];
    T_EQ(first->class_id, first_entries[0].itemID);
    T_EQ(second->class_id, second_entries[0].itemID);
    T_ASSERT(G_IsItem(first) && G_IsItem(second));
    T_ASSERT(first->item.in_world && second->item.in_world);
    T_EQ(first->s.player, PLAYER_NEUTRAL_PASSIVE);
    T_EQ(second->s.player, PLAYER_NEUTRAL_PASSIVE);
    T_ASSERT(Vector2_distance(&first->s.origin2, &second->s.origin2) > 0.0f);
    T_ASSERT(dest->destructable.loot_processed);

    T_ASSERT(!G_KillDestructable(dest, NULL));
    G_SpawnDestructableLoot(dest);
    T_EQ(globals.num_edicts, first_item + 2);
}

TEST(wc3_destructable, empty_probability_remainder_spawns_no_item) {
    droppableItem_t entries[] = {
        { MAKEFOURCC('r', 'a', 't', 'f'), 0 },
    };
    droppableItemSet_t sets[] = {
        { 1, entries },
    };
    DOODAD placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = (DWORD)-1,
        .num_droppedItemSets = 1,
        .droppableItemSets = sets,
    };
    LPEDICT dest;
    DWORD before;

    setup_test_world();
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    before = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, before);
    T_ASSERT(dest->destructable.loot_processed);
}

TEST(wc3_destructable, weighted_random_table_selection_honors_boundaries_and_remainder) {
    mapRandomItem_t entries[] = {
        { 30, MAKEFOURCC('r', 'a', 't', 'f') },
        { 20, MAKEFOURCC('r', 'd', 'e', '2') },
    };
    mapRandomItem_t saturated[] = {
        { 150, MAKEFOURCC('r', 'a', 't', 'f') },
    };

    T_EQ(G_SelectRandomTableItem(entries, 2, 0), entries[0].itemID);
    T_EQ(G_SelectRandomTableItem(entries, 2, 29), entries[0].itemID);
    T_EQ(G_SelectRandomTableItem(entries, 2, 30), entries[1].itemID);
    T_EQ(G_SelectRandomTableItem(entries, 2, 49), entries[1].itemID);
    T_EQ(G_SelectRandomTableItem(entries, 2, 50), 0);
    T_EQ(G_SelectRandomTableItem(entries, 2, 99), 0);
    T_EQ(G_SelectRandomTableItem(entries, 2, 100), 0);
    T_EQ(G_SelectRandomTableItem(saturated, 1, 99), saturated[0].itemID);
}

TEST(wc3_destructable, random_item_table_lookup_uses_table_number) {
    mapRandomItemTable_t tables[] = {
        { .tableNumber = 7 },
        { .tableNumber = 42 },
    };
    LPMAPINFO mapinfo;

    setup_test_world();
    mapinfo = (LPMAPINFO)level.mapinfo;
    mapinfo->num_randomItems = 2;
    mapinfo->randomItems = tables;

    T_ASSERT(G_FindRandomItemTable(42) == &tables[1]);
    T_NULL(G_FindRandomItemTable(1));
    T_NULL(G_FindRandomItemTable((DWORD)-1));
}

TEST(wc3_destructable, death_spawns_map_table_sets_once_as_world_items) {
    mapRandomItem_t first_items[] = {
        { 100, MAKEFOURCC('r', 'a', 't', 'f') },
    };
    mapRandomItem_t second_items[] = {
        { 100, MAKEFOURCC('r', 'd', 'e', '2') },
    };
    mapRandomItemSet_t sets[] = {
        { 1, first_items },
        { 1, second_items },
    };
    mapRandomItemTable_t tables[] = {
        { .tableNumber = 3 },
        { .tableNumber = 42, .num_sets = 2, .sets = sets },
    };
    DOODAD placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = 42,
    };
    LPMAPINFO mapinfo;
    LPEDICT dest;
    DWORD first_item;

    setup_test_world();
    mapinfo = (LPMAPINFO)level.mapinfo;
    mapinfo->num_randomItems = 2;
    mapinfo->randomItems = tables;
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    first_item = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, first_item + 2);
    T_EQ(g_edicts[first_item].class_id, first_items[0].itemID);
    T_EQ(g_edicts[first_item + 1].class_id, second_items[0].itemID);
    T_ASSERT(G_IsItem(&g_edicts[first_item]));
    T_ASSERT(G_IsItem(&g_edicts[first_item + 1]));

    G_SpawnDestructableLoot(dest);
    T_EQ(globals.num_edicts, first_item + 2);
}

TEST(wc3_destructable, missing_random_item_table_spawns_nothing) {
    DOODAD placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = 999,
    };
    LPEDICT dest;
    DWORD before;

    setup_test_world();
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    before = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, before);
    T_ASSERT(dest->destructable.loot_processed);
}

TEST(wc3_destructable, empty_encoded_and_invalid_table_entries_spawn_nothing) {
    mapRandomItem_t encoded_items[] = {
        { 100, MAKEFOURCC('Y', 'Y', 'I', '0') },
    };
    mapRandomItem_t invalid_items[] = {
        { 100, MAKEFOURCC('z', 'z', 'z', 'z') },
    };
    mapRandomItemSet_t sets[] = {
        { 0, NULL },
        { 1, encoded_items },
        { 1, invalid_items },
    };
    mapRandomItemTable_t table = {
        .tableNumber = 9,
        .num_sets = 3,
        .sets = sets,
    };
    DOODAD placement = {
        .flags = 2,
        .treeLife = 100,
        .droppedItemSetPtr = 9,
    };
    LPMAPINFO mapinfo;
    LPEDICT dest;
    DWORD before;

    setup_test_world();
    mapinfo = (LPMAPINFO)level.mapinfo;
    mapinfo->num_randomItems = 1;
    mapinfo->randomItems = &table;
    dest = make_test_destructable(10.0f, 100.0f, 200.0f);
    G_InitializeDestructablePlacement(dest, &placement);
    before = globals.num_edicts;

    T_ASSERT(G_KillDestructable(dest, NULL));
    T_EQ(globals.num_edicts, before);
    T_ASSERT(dest->destructable.loot_processed);
}

TEST(wc3_destructable, silent_dead_state_has_no_event_or_loot) {
    LPEDICT dest = make_test_destructable(100.0f, 0.0f, 0.0f);

    T_ASSERT(G_SetDestructableDeadState(dest, false));
    T_ASSERT(dest->destructable.dead);
    T_ASSERT(dest->destructable.loot_processed);
    T_EQ(level.events.write, 0);
    T_FEQ(dest->health.value, 0.0f, 0.01f);
}

TEST(wc3_destructable, restore_reenables_targeting_pathing_and_second_death) {
    LPEDICT dest = make_test_destructable(100.0f, 4.0f, 4.0f);
    LPEDICT attacker = make_destructable_test_attacker(8.0f, 4.0f);

    T_ASSERT(G_KillDestructable(dest, attacker));
    T_ASSERT(G_RestoreDestructable(dest, 150.0f, true));
    T_ASSERT(!dest->destructable.dead);
    T_ASSERT(!dest->destructable.loot_processed);
    T_ASSERT(!(dest->svflags & SVF_DEADMONSTER));
    T_ASSERT(!(dest->s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(dest->destructable.pathing_active);
    T_ASSERT(G_DestructableIsAttackable(dest));
    T_FEQ(dest->health.value, 100.0f, 0.01f);
    T_NOT_NULL(dest->currentmove);
    T_STREQ(dest->currentmove->animation, "birth");

    T_ASSERT(G_KillDestructable(dest, attacker));
    T_EQ(level.events.write, 2);
    T_ASSERT(level.events.queue[1].source == attacker);
}

TEST(wc3_destructable, set_life_uses_death_and_restore_transitions) {
    LPEDICT dest = make_test_destructable(100.0f, 0.0f, 0.0f);

    T_ASSERT(G_SetDestructableLife(dest, 0.0f));
    T_ASSERT(dest->destructable.dead);
    T_EQ(level.events.write, 1);
    T_NULL(level.events.queue[0].source);

    T_ASSERT(G_SetDestructableLife(dest, 40.0f));
    T_ASSERT(!dest->destructable.dead);
    T_FEQ(dest->health.value, 40.0f, 0.01f);
    T_STREQ(dest->currentmove->animation, "stand");
    T_EQ(level.events.write, 1);
}

TEST(wc3_destructable, remove_bypasses_death_event_and_loot) {
    LPEDICT dest = make_test_destructable(100.0f, 0.0f, 0.0f);

    T_ASSERT(G_RemoveDestructable(dest));
    T_ASSERT(!dest->inuse);
    T_EQ(level.events.write, 0);
}

TEST(wc3_destructable, scripted_lifecycle_natives_use_authoritative_state) {
    static LPCSTR const slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"ID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"targType\"\n"
        "C;Y1;X4;K\"HP\"\n"
        "C;Y1;X5;K\"radius\"\n"
        "C;Y2;X1;K\"B004\"\n"
        "C;Y2;X2;K\"Doodads\\Test\\Test\"\n"
        "C;Y2;X3;K\"debris\"\n"
        "C;Y2;X4;K100\n"
        "C;Y2;X5;K16\n"
        "E\n";
    sheetMetaData_t *meta = G_FindMetaData(DestructableMetaData, "bfil");
    sheetRow_t *saved = meta->table;
    sheetRow_t *rows = parse_slk_string(slk);
    LPEDICT dest;

    setup_test_world();
    G_SetConfigTable(DestructableMetaData, "DestructableData", rows);
    T_ASSERT(run_test_jass(
        "globals\n"
        "  destructable scriptedDest = null\n"
        "  integer scriptedDeaths = 0\n"
        "endglobals\n"
        "function onScriptedDeath takes nothing returns nothing\n"
        "  set scriptedDeaths = scriptedDeaths + 1\n"
        "  call BJassAssert(GetTriggerWidget() == scriptedDest, \"wrong destructable widget\")\n"
        "  call BJassAssert(GetTriggerDestructable() == scriptedDest, \"wrong trigger destructable\")\n"
        "  call BJassAssert(GetKillingUnit() == null, \"scripted kill has a killer\")\n"
        "endfunction\n"
        "function verifyScriptedDeath takes nothing returns nothing\n"
        "  call BJassAssert(scriptedDeaths == 1, \"scripted death event count\")\n"
        "endfunction\n"
        "function removeScriptedDest takes nothing returns nothing\n"
        "  call RemoveDestructable(scriptedDest)\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger t = CreateTrigger()\n"
        "  set scriptedDest = CreateDeadDestructable('B004', 64.0, 64.0, 0.0, 1.0, 0)\n"
        "  call BJassAssert(scriptedDest != null, \"dead creation returned null\")\n"
        "  call BJassAssert(GetDestructableLife(scriptedDest) == 0.0, \"dead creation has life\")\n"
        "  call DestructableRestoreLife(scriptedDest, 60.0, true)\n"
        "  call BJassAssert(GetDestructableLife(scriptedDest) == 60.0, \"restore life failed\")\n"
        "  call TriggerRegisterDeathEvent(t, scriptedDest)\n"
        "  call TriggerAddAction(t, function onScriptedDeath)\n"
        "  call KillDestructable(scriptedDest)\n"
        "endfunction\n"));

    dest = NULL;
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].class_id == MAKEFOURCC('B', '0', '0', '4')) {
            dest = &g_edicts[i];
            break;
        }
    }

    T_NOT_NULL(dest);
    T_ASSERT(dest->destructable.dead);
    T_ASSERT(dest->destructable.loot_processed);
    T_FEQ(dest->health.value, 0.0f, 0.01f);
    T_EQ(level.events.write, 1);

    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "verifyScriptedDeath", true);
    jass_runevents(level.vm);
    T_ASSERT(!jass_rterror_pending(level.vm));

    jass_callbyname(level.vm, "removeScriptedDest", true);
    jass_runevents(level.vm);
    T_ASSERT(!dest->inuse);

    G_SetConfigTable(DestructableMetaData, "DestructableData", saved);
    free_slk_rows(rows);
}

#endif /* BZ_TESTS */
