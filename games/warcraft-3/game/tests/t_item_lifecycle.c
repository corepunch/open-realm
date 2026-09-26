#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

LPEDICT alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

TEST(wc3_item_lifecycle, all_item_attack_bonus_aliases_resolve_to_one_handler) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y20;X2\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AIat\"\nC;Y2;X2;K\"AIat\"\n"
        "C;Y3;X1;K\"AIt6\"\nC;Y3;X2;K\"AIat\"\n"
        "C;Y4;X1;K\"AIt9\"\nC;Y4;X2;K\"AIat\"\n"
        "C;Y5;X1;K\"AItc\"\nC;Y5;X2;K\"AIat\"\n"
        "C;Y6;X1;K\"AItf\"\nC;Y6;X2;K\"AIat\"\n"
        "C;Y7;X1;K\"AItg\"\nC;Y7;X2;K\"AIat\"\n"
        "C;Y8;X1;K\"AIth\"\nC;Y8;X2;K\"AIat\"\n"
        "C;Y9;X1;K\"AIti\"\nC;Y9;X2;K\"AIat\"\n"
        "C;Y10;X1;K\"AItj\"\nC;Y10;X2;K\"AIat\"\n"
        "C;Y11;X1;K\"AItk\"\nC;Y11;X2;K\"AIat\"\n"
        "C;Y12;X1;K\"AItl\"\nC;Y12;X2;K\"AIat\"\n"
        "C;Y13;X1;K\"AItn\"\nC;Y13;X2;K\"AIat\"\n"
        "C;Y14;X1;K\"AItx\"\nC;Y14;X2;K\"AIat\"\n"
        "C;Y15;X1;K\"AIfb\"\nC;Y15;X2;K\"AIfb\"\n"
        "C;Y16;X1;K\"AIlb\"\nC;Y16;X2;K\"AIlb\"\n"
        "C;Y17;X1;K\"AIob\"\nC;Y17;X2;K\"AIob\"\n"
        "C;Y18;X1;K\"AIpb\"\nC;Y18;X2;K\"AIpb\"\n"
        "C;Y19;X1;K\"AIcb\"\nC;Y19;X2;K\"AIcb\"\n"
        "C;Y20;X1;K\"AIzb\"\nC;Y20;X2;K\"AIzb\"\nE\n";
    static char const *const aliases[] = {
        "AIat", "AIt6", "AIt9", "AItc", "AItf", "AItg", "AIth",
        "AIti", "AItj", "AItk", "AItl", "AItn", "AItx",
        "AIfb", "AIlb", "AIob", "AIpb", "AIcb", "AIzb"
    };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    FOR_LOOP(i, sizeof(aliases) / sizeof(aliases[0])) {
        abilityitem_t item = S_AbilityItem(FS_SLKKey(aliases[i]));
        T_ASSERT(item.ability);
        T_ASSERT(item.ability->proc == CAbilityAttackBonus);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Distinct stock aliases stack and reverse after reload without depending on a family-wide cache. */
TEST(wc3_item_lifecycle, passive_item_alias_applies_authored_attack_bonus) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y6;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataC1\"\n"
        "C;Y2;X1;K\"AIat\"\nC;Y2;X2;K\"AIat\"\nC;Y2;X3;K\"3\"\n"
        "C;Y3;X1;K\"AItg\"\nC;Y3;X2;K\"AIat\"\nC;Y3;X3;K\"1\"\n"
        "C;Y4;X1;K\"AInv\"\nC;Y4;X2;K\"AInv\"\nC;Y4;X3;K\"6\"\nC;Y4;X4;K\"1\"\n"
        "C;Y5;X1;K\"AIt6\"\nC;Y5;X2;K\"AIat\"\nC;Y5;X3;K\"6\"\n"
        "C;Y6;X1;K\"AId1\"\nC;Y6;X2;K\"AIde\"\nC;Y6;X3;K\"1\"\nE\n";
    const char items[] =
        "ID;PWXL;N;EBB;Y4;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"ratf\"\nC;Y2;X2;K\"AItg\"\n"
        "C;Y3;X1;K\"rde2\"\nC;Y3;X2;K\"AIt6\"\n"
        "C;Y4;X1;K\"spro\"\nC;Y4;X2;K\"AId1\"\nE\n";
    cstring_t path = "/tmp/openwarcraft3-item-alias-save.bin";
    uint32_t codes[] = { MAKEFOURCC('r','a','t','f'), MAKEFOURCC('r','d','e','2'), MAKEFOURCC('s','p','r','o') };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    slkTestData_t *idata = parse_slk_string(items), *olditem = G_SetSLKRows("ItemData", idata);
    setup_test_world();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    unit->attack1.temporaryDamageBonus = unit->attack2.temporaryDamageBonus = 0;
    unit->temporary_armor_bonus = 0;
    FOR_LOOP(i, 3) {
        LPEDICT item = alloc_test_unit(codes[i], 32, 0);
        item->targtype = TARG_ITEM;
        item->item.in_world = true;
        item->item.inventory_slot = -1;
        T_ASSERT(G_AddItemToSlot(unit, item, i));
    }
    T_FEQ(unit->attack1.temporaryDamageBonus, 7, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 7, 0.001f);
    T_FEQ(unit->temporary_armor_bonus, 1, 0.001f);
    uint32_t index = unit->s.number;
    T_ASSERT(WriteGame(path));
    T_ASSERT(ReadGame(path));
    unit = g_edicts + index;
    T_FEQ(unit->attack1.temporaryDamageBonus, 7, 0.001f);
    T_FEQ(unit->temporary_armor_bonus, 1, 0.001f);
    T_ASSERT(G_DropItem(unit, 1));
    T_FEQ(unit->attack1.temporaryDamageBonus, 1, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 1, 0.001f);
    T_ASSERT(G_DropItem(unit, 2));
    T_FEQ(unit->temporary_armor_bonus, 0, 0.001f);
    T_ASSERT(G_DropItem(unit, 0));
    T_FEQ(unit->attack1.temporaryDamageBonus, 0, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 0, 0.001f);
    remove(path);
    G_SetSLKRows("ItemData", olditem); free_slk_rows(idata);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Losing item-use permission while carrying an already-applied passive must
 * not make that bonus permanent when the item leaves inventory. */
TEST(wc3_item_lifecycle, passive_item_removal_ignores_current_can_use_permission) {
    const char enabled_slk[] =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\nC;Y2;X4;K\"1\"\n"
        "C;Y3;X1;K\"AIat\"\nC;Y3;X2;K\"AIat\"\nC;Y3;X3;K\"3\"\nE\n";
    const char disabled_slk[] =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\nC;Y2;X4;K\"0\"\n"
        "C;Y3;X1;K\"AIat\"\nC;Y3;X2;K\"AIat\"\nC;Y3;X3;K\"3\"\nE\n";
    const char items[] =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"ratf\"\nC;Y2;X2;K\"AIat\"\nE\n";
    slkTestData_t *enabled = parse_slk_string(enabled_slk);
    slkTestData_t *old_abilities = G_SetSLKRows("AbilityData", enabled);
    slkTestData_t *idata = parse_slk_string(items);
    slkTestData_t *old_items = G_SetSLKRows("ItemData", idata);
    LPEDICT unit, item;

    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    unit->attack1.temporaryDamageBonus = unit->attack2.temporaryDamageBonus = 0;
    item = alloc_test_unit(MAKEFOURCC('r','a','t','f'), 32, 0);
    item->targtype = TARG_ITEM;
    item->item.in_world = true;
    item->item.inventory_slot = -1;

    T_ASSERT(G_InventoryCanUseItems(unit));
    T_ASSERT(G_AddItemToSlot(unit, item, 0));
    T_FEQ(unit->attack1.temporaryDamageBonus, 3, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 3, 0.001f);

    {
        slkTestData_t *disabled = parse_slk_string(disabled_slk);
        slkTestData_t *replaced = G_SetSLKRows("AbilityData", disabled);
        T_ASSERT(!G_InventoryCanUseItems(unit));
        T_ASSERT(G_DropItem(unit, 0));
        T_FEQ(unit->attack1.temporaryDamageBonus, 0, 0.001f);
        T_FEQ(unit->attack2.temporaryDamageBonus, 0, 0.001f);
        free_slk_rows(disabled);
        free_slk_rows(replaced);
    }

    {
        slkTestData_t *current_items = G_SetSLKRows("ItemData", old_items);
        slkTestData_t *current_abilities = G_SetSLKRows("AbilityData", old_abilities);
        free_slk_rows(current_items);
        free_slk_rows(current_abilities);
    }
    free_slk_rows(old_items); free_slk_rows(idata);
    free_slk_rows(old_abilities); free_slk_rows(enabled);
}

/* Max-life/mana item bonuses must survive a derived-stat recompute and reverse cleanly. */
TEST(wc3_item_lifecycle, max_resource_item_bonuses_survive_hero_recompute) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AIml\"\nC;Y2;X2;K\"AIml\"\nC;Y2;X3;K\"123\"\n"
        "C;Y3;X1;K\"AImm\"\nC;Y3;X2;K\"AImm\"\nC;Y3;X3;K\"77\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    UnitBalance_t balance;
    setup_test_world();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    balance = *unit->data.UnitBalance;
    unit->data.UnitBalance = &balance;
    unit->hero.str = balance.strength;
    unit->hero.agi = balance.agility;
    unit->hero.intel = balance.intelligence;
    unit->health.max_value = balance.maxHealth;
    unit->health.value = balance.maxHealth;
    unit->mana.max_value = balance.maxMana;
    unit->mana.value = balance.maxMana;
    {
        uint32_t codes[] = { MAKEFOURCC('A','I','m','l'), MAKEFOURCC('A','I','m','m') };
        abilityitem_t items[2] = { S_AbilityItem(codes[0]), S_AbilityItem(codes[1]) };
        abilityCall_t calls[2] = { { .item = &items[0] }, { .item = &items[1] } };
        FOR_LOOP(i, 2) T_ASSERT(S_AbilityMessage(unit, A_ITEM_ADD, calls + i));
        T_FEQ(unit->health.max_value, balance.maxHealth + 123, 0.001f);
        T_FEQ(unit->mana.max_value, balance.maxMana + 77, 0.001f);

        unit->hero.str++;
        unit->hero.intel++;
        G_RecomputeHeroStats(unit);
        T_FEQ(unit->health.max_value, balance.maxHealth + 25 + 123, 0.001f);
        T_FEQ(unit->mana.max_value, balance.maxMana + 15 + 77, 0.001f);

        FOR_LOOP(i, 2) T_ASSERT(S_AbilityMessage(unit, A_ITEM_REMOVE, calls + i));
        T_FEQ(unit->health.max_value, balance.maxHealth + 25, 0.001f);
        T_FEQ(unit->mana.max_value, balance.maxMana + 15, 0.001f);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* All three tomes and their passive equivalents use Agility/Intelligence/Strength data order. */
TEST(wc3_item_lifecycle, strength_tome_modifies_strength) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y7;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\n"
        "C;Y2;X1;K\"AIsm\"\nC;Y2;X2;K\"AIsm\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"AIam\"\nC;Y3;X2;K\"AIam\"\nC;Y3;X3;K\"1\"\n"
        "C;Y4;X1;K\"AIim\"\nC;Y4;X2;K\"AIim\"\nC;Y4;X4;K\"1\"\n"
        "C;Y5;X1;K\"AIs1\"\nC;Y5;X2;K\"AIab\"\nC;Y5;X5;K\"1\"\n"
        "C;Y6;X1;K\"AIa1\"\nC;Y6;X2;K\"AIab\"\nC;Y6;X3;K\"1\"\n"
        "C;Y7;X1;K\"AIi1\"\nC;Y7;X2;K\"AIab\"\nC;Y7;X4;K\"1\"\nE\n";
    uint32_t codes[][2] = {
        { MAKEFOURCC('A','I','s','m'), MAKEFOURCC('A','I','s','1') },
        { MAKEFOURCC('A','I','a','m'), MAKEFOURCC('A','I','a','1') },
        { MAKEFOURCC('A','I','i','m'), MAKEFOURCC('A','I','i','1') },
    };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    setup_test_world();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    LPEDICT clent = g_edicts;
    unit->s.player = 0;
    G_SelectEntity(clent->client, unit);
    FOR_LOOP(i, 3) {
        unit->hero.str = unit->hero.agi = unit->hero.intel = 10;
        clent->client->menu.ability_code = codes[i][0];
        abilityitem_t item = S_AbilityItem(codes[i][0]);
        abilityCall_t call = { .item = &item, .client = clent };
        T_ASSERT(S_AbilityMessage(clent, A_ITEM_USE, &call));
        T_EQ(unit->hero.str, 10 + (i == 0));
        T_EQ(unit->hero.agi, 10 + (i == 1));
        T_EQ(unit->hero.intel, 10 + (i == 2));
        item = S_AbilityItem(codes[i][1]);
        T_ASSERT(S_AbilityMessage(unit, A_ITEM_ADD, &call));
        T_EQ(unit->hero.str, 10 + 2 * (i == 0));
        T_EQ(unit->hero.agi, 10 + 2 * (i == 1));
        T_EQ(unit->hero.intel, 10 + 2 * (i == 2));
        T_ASSERT(S_AbilityMessage(unit, A_ITEM_REMOVE, &call));
        T_EQ(unit->hero.str, 10 + (i == 0));
        T_EQ(unit->hero.agi, 10 + (i == 1));
        T_EQ(unit->hero.intel, 10 + (i == 2));
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Orb data is non-stock (ROC AIfb DataA=12, AIob DataA=6); pickup must apply the
 * item's own row and drop must reverse it, like CAbilityAttackBonus aliases. */
TEST(wc3_item_lifecycle, orb_pickup_applies_authored_bonus_damage) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataC1\"\n"
        "C;Y2;X1;K\"AIfb\"\nC;Y2;X2;K\"AIfb\"\nC;Y2;X3;K\"11\"\n"
        "C;Y3;X1;K\"AIob\"\nC;Y3;X2;K\"AIob\"\nC;Y3;X3;K\"13\"\n"
        "C;Y4;X1;K\"AInv\"\nC;Y4;X2;K\"AInv\"\nC;Y4;X3;K\"6\"\nC;Y4;X4;K\"1\"\nE\n";
    const char items[] =
        "ID;PWXL;N;EBB;Y3;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"orbf\"\nC;Y2;X2;K\"AIfb\"\n"
        "C;Y3;X1;K\"orbr\"\nC;Y3;X2;K\"AIob\"\nE\n";
    uint32_t codes[] = { MAKEFOURCC('o','r','b','f'), MAKEFOURCC('o','r','b','r') };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    slkTestData_t *idata = parse_slk_string(items), *olditem = G_SetSLKRows("ItemData", idata);
    setup_test_world();
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    unit->attack1.temporaryDamageBonus = unit->attack2.temporaryDamageBonus = 0;
    FOR_LOOP(i, 2) {
        LPEDICT item = alloc_test_unit(codes[i], 32, 0);
        item->targtype = TARG_ITEM;
        item->item.in_world = true;
        item->item.inventory_slot = -1;
        T_ASSERT(G_AddItemToSlot(unit, item, i));
    }
    T_FEQ(unit->attack1.temporaryDamageBonus, 24, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 24, 0.001f);
    T_ASSERT(G_DropItem(unit, 0));
    T_FEQ(unit->attack1.temporaryDamageBonus, 13, 0.001f);
    T_ASSERT(G_DropItem(unit, 1));
    T_FEQ(unit->attack1.temporaryDamageBonus, 0, 0.001f);
    T_FEQ(unit->attack2.temporaryDamageBonus, 0, 0.001f);
    G_SetSLKRows("ItemData", olditem); free_slk_rows(idata);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ROC omits orb BuffID; AIob/AIcb/AIzb must apply Bfro/BIcb/Bfre with authored
 * durations while AIfb/AIlb/AIpb stay damage-only. Covers native ownership and
 * held orb items; the same orb from both sources applies once. */
TEST(wc3_item_lifecycle, orb_on_hit_applies_buff_state) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y7;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"6\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AIob\"\nC;Y3;X2;K\"AIob\"\nC;Y3;X3;K\"3\"\n"
        "C;Y3;X4;K\"1\"\nC;Y3;X5;K\"6\"\nC;Y3;X6;K\"0\"\n"
        "C;Y4;X1;K\"AIcb\"\nC;Y4;X2;K\"AIcb\"\nC;Y4;X3;K\"5\"\n"
        "C;Y4;X4;K\"5\"\nC;Y4;X5;K\"5\"\nC;Y4;X6;K\"5\"\n"
        "C;Y5;X1;K\"AIzb\"\nC;Y5;X2;K\"AIzb\"\nC;Y5;X3;K\"0\"\n"
        "C;Y5;X4;K\"0\"\nC;Y5;X5;K\"9\"\nC;Y5;X6;K\"0\"\n"
        "C;Y6;X1;K\"AIfb\"\nC;Y6;X2;K\"AIfb\"\nC;Y6;X3;K\"0\"\n"
        "C;Y6;X4;K\"0\"\nC;Y6;X5;K\"12\"\nC;Y6;X6;K\"0\"\n"
        "C;Y7;X1;K\"AIpb\"\nC;Y7;X2;K\"AIpb\"\nC;Y7;X3;K\"0\"\n"
        "C;Y7;X4;K\"0\"\nC;Y7;X5;K\"5\"\nC;Y7;X6;K\"0\"\nE\n";
    const char items[] =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"orbc\"\nC;Y2;X2;K\"AIob,AIcb\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AInv,AIob,AIfb" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    slkTestData_t *idata = parse_slk_string(items), *olditem = G_SetSLKRows("ItemData", idata);
    setup_test_world();
    level.time = 0;
    LPEDICT attacker = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 300, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    LPEDICT item = alloc_test_unit(MAKEFOURCC('o','r','b','c'), 32, 0);
    attacker->data.UnitAbilities = &abilities; attacker->s.player = 0;
    attacker->svflags |= SVF_MONSTER;
    target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 200.0f;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    item->targtype = TARG_ITEM;
    item->item.in_world = true;
    item->item.inventory_slot = -1;
    T_ASSERT(G_AddItemToSlot(attacker, item, 0));
    T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','f','r','o')));
    S_OrbOnHit(attacker, target);
    T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','f','r','o')));
    T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','I','c','b')));
    T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','f','r','e')));
    level.time += 4000; unit_updatestatuses(target);
    T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','f','r','o')));
    T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','I','c','b')));
    G_SetSLKRows("ItemData", olditem); free_slk_rows(idata);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A regeneration aura uses owner+goalentity too; natural sleep must remove only its own art. */
TEST(wc3_item_lifecycle, waking_creep_preserves_regeneration_overlay) {
    setup_test_world();
    LPEDICT creep = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    creep->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    creep->svflags |= SVF_MONSTER;
    creep->sleep.can_sleep = true;
    creep->stand = unit_stand;
    unit_stand(creep);
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    LPEDICT effect = G_Spawn();
    effect->owner = effect->goalentity = creep;
    effect->summon_ability = MAKEFOURCC('A','o','a','r');
    FOR_LOOP(i, 3) {
        ai_stand(creep);
        T_ASSERT(G_UnitIsSleeping(creep));
        LPEDICT sleep = NULL;
        FOR_LOOP(j, globals.num_edicts)
            if (g_edicts[j].inuse && g_edicts[j].owner == creep &&
                g_edicts[j].summon_ability == MAKEFOURCC('A','C','s','p')) sleep = g_edicts + j;
        T_NOT_NULL(sleep);
        if (i == 0) G_UnitWakeUp(creep);
        else if (i == 1) unit_stand(creep);
        else G_UnitSetCanSleep(creep, false);
        T_ASSERT(!G_UnitIsSleeping(creep));
        T_ASSERT(effect->inuse && effect->goalentity == creep);
        T_ASSERT(sleep && (!sleep->inuse || sleep->goalentity != creep));
    }
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
}
#endif
