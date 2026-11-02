/* Retail-first item effect regressions. These intentionally use real gameplay
 * entry points and assert observable results, including currently failing
 * behavior, not only inventory aliases or presence of BuffID.  The synthetic
 * SLKs are minimal transports for the audited rawcodes and data fields. */
#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

typedef struct {
    slkTestData_t *ability_rows, *item_rows, *old_abilities, *old_items;
    UnitAbilities_t hero_abilities;
    edict_t *hero, *enemy;
} retailEffectFixture_t;

static void re_init(retailEffectFixture_t *f, char const *ability_slk, char const *item_slk) {
    memset(f, 0, sizeof(*f));
    reset_entities(); setup_test_world(); level.time = 1000;
    f->ability_rows = parse_slk_string(ability_slk);
    f->item_rows = parse_slk_string(item_slk);
    T_NOT_NULL(f->ability_rows); T_NOT_NULL(f->item_rows);
    f->old_abilities = G_SetSLKRows("AbilityData", f->ability_rows);
    f->old_items = G_SetSLKRows("ItemData", f->item_rows);
    f->hero_abilities = (UnitAbilities_t){ .abilList = "AInv" };
    f->hero = alloc_test_unit(FS_SLKKey("Hpal"), 0, 0);
    f->enemy = alloc_test_unit(FS_SLKKey("hfoo"), 64, 0);
    f->hero->data.UnitAbilities = &f->hero_abilities;
    f->hero->s.player = 0;
    f->enemy->s.player = 1;
    f->hero->svflags |= SVF_MONSTER;
    f->enemy->svflags |= SVF_MONSTER;
    f->hero->targtype = f->enemy->targtype = TARG_GROUND;
    f->hero->health.value = f->hero->health.max_value = 500;
    f->enemy->health.value = f->enemy->health.max_value = 500;
    f->hero->mana.value = f->hero->mana.max_value = 500;
    f->enemy->mana.value = f->enemy->mana.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
}
static edict_t *re_equip(retailEffectFixture_t *f, cstring_t item_id, uint32_t slot) {
    edict_t *item = alloc_test_unit(FS_SLKKey(item_id), 16 + slot, 0);
    item->targtype = TARG_ITEM;
    G_AddItem(item);
    G_GetItem(item)->in_world = true;
    G_GetItem(item)->inventory_slot = -1;
    T_ASSERT(G_AddItemToSlot(f->hero, item, slot));
    T_ASSERT(G_InventoryItem(f->hero, slot) == item);
    return item;
}
static void re_done(retailEffectFixture_t *f) {
    G_SetSLKRows("ItemData", f->old_items);
    G_SetSLKRows("AbilityData", f->old_abilities);
    free_slk_rows(f->item_rows);
    free_slk_rows(f->ability_rows);
}

/* Known RED (see item-orbs.md): BIcb exists but combat/HUD armor ignores the
 * five-point reduction. On-hit application must use real held ocor/AIcb and
 * G_UnitArmorValue, not an artificial armor subtraction in the fixture. */
TEST(retail_item_effect, ocor_reduces_effective_armor_and_expires) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataE1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"6\"\n"
        "C;Y3;X1;K\"AIcb\"\nC;Y3;X2;K\"AIcb\"\n"
        "C;Y3;X3;K\"1\"\nC;Y3;X4;K\"5\"\n"
        "C;Y3;X5;K\"5\"\nC;Y3;X6;K\"2\"\n"
        "C;Y3;X7;K\"5\"\nC;Y3;X8;K\"5\"\n"
        "C;Y3;X9;K\"BIcb\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"ocor\"\nC;Y2;X2;K\"AIcb\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    float const before = G_UnitArmorValue(f.enemy);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    re_equip(&f, "ocor", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 5, 0.001f);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIcb")));
    T_FEQ(G_UnitArmorValue(f.enemy), before - 5, 0.001f);
    level.time += 4999; unit_updatestatuses(f.enemy);
    T_FEQ(G_UnitArmorValue(f.enemy), before - 5, 0.001f);
    level.time += 1; unit_updatestatuses(f.enemy);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("BIcb")));
    T_FEQ(G_UnitArmorValue(f.enemy), before, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    re_done(&f);
}

/* The published item carries TWO authored abilities; a status-only check
 * is inadequate. Verify native hit, one (not two) DPS pulse, 6s expiry and
 * the independent five-point passive attack bonus. */
TEST(retail_item_effect, oven_combines_attack_bonus_and_single_poison_pulse) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"DataD1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y1;X9;K\"DataE1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"6\"\n"
        "C;Y3;X1;K\"AIpb\"\nC;Y3;X2;K\"AIpb\"\n"
        "C;Y3;X3;K\"1\"\nC;Y3;X4;K\"5\"\n"
        "C;Y3;X9;K\"2\"\n"
        "C;Y4;X1;K\"Apo2\"\nC;Y4;X2;K\"Apo2\"\n"
        "C;Y4;X3;K\"1\"\nC;Y4;X4;K\"9\"\n"
        "C;Y4;X5;K\"1\"\nC;Y4;X6;K\"6\"\n"
        "C;Y4;X7;K\"6\"\nC;Y4;X8;K\"BIpb,BIpd\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"oven\"\nC;Y2;X2;K\"AIpb,Apo2\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "oven", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 5, 0.001f);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpb")));
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpd")));
    float const post_hit_hp = f.enemy->health.value;
    level.time += 1000; unit_updatestatuses(f.enemy);
    T_FEQ(f.enemy->health.value, post_hit_hp - 9, 0.001f);
    level.time += 1000; unit_updatestatuses(f.enemy);
    T_FEQ(f.enemy->health.value, post_hit_hp - 18, 0.001f);
    level.time += 4000; unit_updatestatuses(f.enemy);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("BIpb")));
    float const after = f.enemy->health.value;
    level.time += 1000; unit_updatestatuses(f.enemy);
    T_FEQ(f.enemy->health.value, after, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    re_done(&f);
}

/* Stock 15s AIxs/Aami contract from both audited archives.  A separate
 * deliberately nonstock 17s check already exists in t_anti_magic_shell.c. */
TEST(retail_item_effect, pams_blocks_hostile_magic_until_15_seconds) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"Cool1\"\n"
        "C;Y2;X1;K\"AIxs\"\nC;Y2;X2;K\"Aami\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"air,ground\"\n"
        "C;Y2;X5;K\"15\"\nC;Y2;X6;K\"15\"\n"
        "C;Y2;X7;K\"10\"\nC;Y2;X8;K\"0\"\n"
        "C;Y2;X9;K\"Bams,Bam2\"\nC;Y2;X10;K\"30\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"pams\"\nC;Y2;X2;K\"AIxs\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "pams", 0);
    T_ASSERT(S_CastUnitTargetSpell(f.hero, FS_SLKKey("AIxs"), f.hero));
    T_ASSERT(S_UnitSpellImmune(f.hero));
    T_ASSERT(!S_SpellDamage(f.hero, f.enemy, 40));
    T_FEQ(f.hero->health.value, 500, 0.001f);
    level.time += 14999; unit_updatestatuses(f.hero);
    T_ASSERT(S_UnitSpellImmune(f.hero));
    level.time++; unit_updatestatuses(f.hero);
    T_ASSERT(!S_UnitSpellImmune(f.hero));
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 40));
    T_ASSERT(f.hero->health.value < 500);
    re_done(&f);
}

/* This item is stocked as oli2 -> AIll, with parent AIsb and the authored
 * purge chance in DataB=35. The legacy AIlb orb is NOT this item. This is a
 * red alias-dispatch expectation until OpenRealm implements the parent. */
TEST(retail_item_effect, oli2_AIll_has_retail_orb_slow_purge_dispatch) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"UnitID1\"\n"
        "C;Y2;X1;K\"AIll\"\nC;Y2;X2;K\"AIsb\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"35\"\n"
        "C;Y2;X5;K\"10\"\nC;Y2;X6;K\"35\"\n"
        "C;Y2;X7;K\"2\"\nC;Y2;X8;K\"AIpg\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"oli2\"\nC;Y2;X2;K\"AIll\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    edict_t *item = re_equip(&f, "oli2", 0);
    T_STREQ(G_ItemAbilityList(item), "AIll");
    abilityitem_t const ability = S_AbilityItem(FS_SLKKey("AIll"));
    T_NOT_NULL(ability.ability);
    /* The authored parent must dispatch something other than the default
     * passive/no-op handler; implementing this is expected to turn RED GREEN. */
    if (ability.ability) T_ASSERT(ability.ability->proc != CAbilityPassive);
    re_done(&f);
}

/* Non-stock DataB catches hard-coded '-5' armor consumers. The authoritative
 * retail row is -5; -7 is deliberately only a fixture perturbation. */
TEST(retail_item_effect, ocor_nonstock_armor_reduction_uses_DataB_not_constant) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"DataE1\"\n"
        "C;Y2;X1;K\"AIcb\"\nC;Y2;X2;K\"AIcb\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"7\"\n"
        "C;Y2;X5;K\"3\"\nC;Y2;X6;K\"3\"\n"
        "C;Y2;X7;K\"BIcb\"\nC;Y2;X8;K\"2\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"ocor\"\nC;Y2;X2;K\"AIcb\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    float const base = G_UnitArmorValue(f.enemy);
    re_equip(&f, "ocor", 0);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIcb")));
    T_FEQ(G_UnitArmorValue(f.enemy), base - 7, 0.001f);
    level.time += 3000; unit_updatestatuses(f.enemy);
    T_FEQ(G_UnitArmorValue(f.enemy), base, 0.001f);
    re_done(&f);
}

/* Check the on-hit result through real attack dispatch rather than merely
 * finding AIll in an ability table. In stock the chance is 35%; here DataB
 * is deliberately 100 to force the proc deterministically. */
TEST(retail_item_effect, oli2_nonstock_guaranteed_proc_dispels_enemy_positive_buff) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"UnitID1\"\n"
        "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIll\"\nC;Y2;X2;K\"AIsb\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"100\"\n"
        "C;Y2;X5;K\"10\"\nC;Y2;X6;K\"35\"\n"
        "C;Y2;X7;K\"2\"\nC;Y2;X8;K\"AIpg\"\n"
        "C;Y2;X9;K\"3\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"oli2\"\nC;Y2;X2;K\"AIll\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "oli2", 0);
    unit_addtimedstatus(f.enemy, "Binf", 1, 30.0f);
    heroabilitystatus_t *buff = unit_findstatus(f.enemy, FS_SLKKey("Binf"));
    T_NOT_NULL(buff);
    if (buff) buff->buff_flags |= WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_POSITIVE;
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
    re_done(&f);
}

#endif /* BZ_TESTS */
