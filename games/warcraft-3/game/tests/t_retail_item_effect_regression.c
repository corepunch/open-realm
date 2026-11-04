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
    ((mapInfo_t *)level.mapinfo)->fileFormat = 24;
    f->hero = alloc_test_unit(FS_SLKKey("Hpal"), 0, 0);
    f->enemy = alloc_test_unit(FS_SLKKey("hfoo"), 64, 0);
    f->hero->data.UnitAbilities = &f->hero_abilities;
    f->hero->s.player = 0;
    f->enemy->s.player = 1;
    f->hero->svflags |= SVF_MONSTER;
    f->enemy->svflags |= SVF_MONSTER;
    /* Allocator-only test units do not go through SP_SpawnUnit, which stamps
     * their generation time. Status source validation needs that identity. */
    f->hero->spawn_time = f->enemy->spawn_time = level.time;
    f->hero->unit_state->attack1.type = ATK_NORMAL;
    f->hero->unit_state->attack1.targetsAllowed = TARG_GROUND;
    f->hero->targtype = f->enemy->targtype = TARG_GROUND;
    f->hero->health.value = f->hero->health.max_value = 500;
    f->enemy->health.value = f->enemy->health.max_value = 500;
    f->hero->mana.value = f->hero->mana.max_value = 500;
    f->enemy->mana.value = f->enemy->mana.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
}
/* Drive the production per-entity frame scheduler so status owners, rather
 * than a direct update helper, process ticks and expiry. */
static void re_run_frame_at(edict_t *unit, uint32_t time) {
    level.time = time;
    G_RunEntity(unit);
}
static edict_t *re_equip(retailEffectFixture_t *f, cstring_t item_id, uint32_t slot) {
    edict_t *item = alloc_test_unit(FS_SLKKey(item_id), 16 + slot, 0);
    T_ASSERT(G_InventoryCapacity(f->hero) >= MAX_INVENTORY);
    T_ASSERT(G_InventoryCanUseItems(f->hero));
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

/* BIcb is currently a timed state without an armor consumer. Verify that the
 * held retail orb applies and expires its authored corruption status. */
TEST(retail_item_effect, ocor_applies_timed_corruption_status) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataE1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"6\"\n"
        "C;Y2;X6;K\"1\"\nC;Y2;X10;K\"1\"\nC;Y2;X11;K\"1\"\n"
        "C;Y3;X1;K\"AIcb\"\nC;Y3;X2;K\"AIcb\"\n"
        "C;Y3;X3;K\"1\"\nC;Y3;X4;K\"5\"\n"
        "C;Y3;X5;K\"5\"\nC;Y3;X6;K\"2\"\n"
        "C;Y3;X7;K\"5\"\nC;Y3;X8;K\"5\"\n"
        "C;Y3;X9;K\"BIcb\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"ocor\"\nC;Y2;X2;K\"AIcb\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    float const before = G_UnitArmorValue(f.enemy);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    re_equip(&f, "ocor", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 5, 0.001f);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIcb")));
    T_FEQ(G_UnitArmorValue(f.enemy), before, 0.001f);
    re_run_frame_at(f.enemy, level.time + 4999);
    T_FEQ(G_UnitArmorValue(f.enemy), before, 0.001f);
    re_run_frame_at(f.enemy, level.time + 1);
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
        "ID;PWXL;N;EBB;Y4;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"DataD1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y1;X9;K\"DataE1\"\nC;Y1;X10;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"6\"\n"
        "C;Y2;X5;K\"1\"\nC;Y2;X9;K\"1\"\nC;Y2;X10;K\"1\"\n"
        "C;Y3;X1;K\"AIpb\"\nC;Y3;X2;K\"AIpb\"\n"
        "C;Y3;X3;K\"1\"\nC;Y3;X4;K\"5\"\n"
        "C;Y3;X9;K\"2\"\n"
        "C;Y4;X1;K\"Apo2\"\nC;Y4;X2;K\"Apo2\"\n"
        "C;Y4;X3;K\"1\"\nC;Y4;X4;K\"9\"\n"
        "C;Y4;X5;K\"1\"\nC;Y4;X6;K\"6\"\n"
        "C;Y4;X7;K\"6\"\nC;Y4;X8;K\"BIpb,BIpd\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"oven\"\nC;Y2;X2;K\"AIpb,Apo2\"\nC;Y2;X3;K\"true\"\nE\n";
    static UnitBalance_t target_balance;
    retailEffectFixture_t f;
    memset(&target_balance, 0, sizeof(target_balance));
    re_init(&f, ability_slk, items);
    f.enemy->data.UnitBalance = &target_balance;
    re_equip(&f, "oven", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 5, 0.001f);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpb")));
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpd")));
    float const post_hit_hp = f.enemy->health.value;
    re_run_frame_at(f.enemy, level.time + 1000);
    T_FEQ(f.enemy->health.value, post_hit_hp - 9, 0.001f);
    re_run_frame_at(f.enemy, level.time + 1000);
    T_FEQ(f.enemy->health.value, post_hit_hp - 18, 0.001f);
    re_run_frame_at(f.enemy, level.time + 4000);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("BIpb")));
    float const after = f.enemy->health.value;
    re_run_frame_at(f.enemy, level.time + 1000);
    T_FEQ(f.enemy->health.value, after, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 0, 0.001f);
    re_done(&f);
}

/* Stock 15s AIxs/Aami contract from both audited archives.  A separate
 * deliberately nonstock 17s check already exists in t_anti_magic_shell.c. */
TEST(retail_item_effect, pams_blocks_hostile_magic_until_15_seconds) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"Cool1\"\n"
        "C;Y1;X11;K\"DataA1\"\n"
        "C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"DataE1\"\n"
        "C;Y2;X1;K\"AIxs\"\nC;Y2;X2;K\"Aami\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"air,ground,friend,self\"\n"
        "C;Y2;X5;K\"15\"\nC;Y2;X6;K\"15\"\n"
        "C;Y2;X7;K\"10\"\nC;Y2;X8;K\"0\"\n"
        "C;Y2;X9;K\"Bams,Bam2\"\nC;Y2;X10;K\"30\"\n"
        "C;Y3;X1;K\"AInv\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X11;K\"6\"\n"
        "C;Y3;X8;K\"1\"\nC;Y3;X13;K\"1\"\nC;Y3;X14;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"pams\"\nC;Y2;X2;K\"AIxs\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    /* Direct cast helper requires the alias on the synthetic unit ability list;
     * G_UseItem's UI target-selection path is covered separately. */
    f.hero_abilities.abilList = "AInv,AIxs";
    re_equip(&f, "pams", 0);
    T_ASSERT(S_CastUnitTargetSpell(f.hero, FS_SLKKey("AIxs"), f.hero));
    T_ASSERT(S_UnitSpellImmune(f.hero));
    T_ASSERT(!S_SpellDamage(f.hero, f.enemy, 40));
    T_FEQ(f.hero->health.value, 500, 0.001f);
    re_run_frame_at(f.hero, level.time + 14999);
    T_ASSERT(S_UnitSpellImmune(f.hero));
    re_run_frame_at(f.hero, level.time + 1);
    T_ASSERT(!S_UnitSpellImmune(f.hero));
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 40));
    T_ASSERT(f.hero->health.value < 500);
    re_done(&f);
}

/* This item is stocked as oli2 -> AIll, with parent AIsb. The legacy AIlb orb
 * is not this item. Verify the retail binding while the proc handler remains
 * unimplemented. */
TEST(retail_item_effect, oli2_AIll_retail_alias_has_no_registered_handler) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"UnitID1\"\n"
        "C;Y1;X11;K\"DataA1\"\n"
        "C;Y2;X1;K\"AIll\"\nC;Y2;X2;K\"AIsb\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"35\"\n"
        "C;Y2;X5;K\"10\"\nC;Y2;X6;K\"35\"\n"
        "C;Y2;X7;K\"2\"\nC;Y2;X8;K\"AIpg\"\n"
        "C;Y3;X1;K\"AInv\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X3;K\"6\"\n"
        "C;Y3;X5;K\"1\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"1\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"oli2\"\nC;Y2;X2;K\"AIll\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    edict_t *item = re_equip(&f, "oli2", 0);
    T_STREQ(G_ItemAbilityList(item), "AIll");
    abilityitem_t const ability = S_AbilityItem(FS_SLKKey("AIll"));
    T_EQ(ability.code, FS_SLKKey("AIll"));
    T_ASSERT(!ability.ability);
    T_EQ(G_AbilityCode(FS_SLKKey("AIll")), FS_SLKKey("AIsb"));
    re_done(&f);
}

/* Exercise nonstock DataB while documenting the current state-only contract:
 * DataB does not alter armor because BIcb has no armor consumer yet. */
TEST(retail_item_effect, ocor_nonstock_data_keeps_status_without_armor_consumer) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"DataE1\"\n"
        "C;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"DataD1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"6\"\nC;Y2;X8;K\"1\"\nC;Y2;X9;K\"1\"\nC;Y2;X10;K\"1\"\n"
        "C;Y3;X1;K\"AIcb\"\nC;Y3;X2;K\"AIcb\"\n"
        "C;Y3;X3;K\"5\"\nC;Y3;X4;K\"7\"\n"
        "C;Y3;X5;K\"3\"\nC;Y3;X6;K\"3\"\n"
        "C;Y3;X7;K\"BIcb\"\nC;Y3;X8;K\"2\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"ocor\"\nC;Y2;X2;K\"AIcb\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    float const base = G_UnitArmorValue(f.enemy);
    re_equip(&f, "ocor", 0);
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIcb")));
    T_FEQ(G_UnitArmorValue(f.enemy), base, 0.001f);
    re_run_frame_at(f.enemy, level.time + 3000);
    T_FEQ(G_UnitArmorValue(f.enemy), base, 0.001f);
    re_done(&f);
}

/* The engine does not yet dispatch AIll's AIsb behavior. Keep the ordinary
 * attack path covered and make the current unsupported proc explicit. */
TEST(retail_item_effect, oli2_AIll_does_not_purge_until_handler_exists) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"UnitID1\"\n"
        "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIll\"\nC;Y2;X2;K\"AIsb\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"100\"\n"
        "C;Y2;X5;K\"10\"\nC;Y2;X6;K\"35\"\n"
        "C;Y2;X7;K\"2\"\nC;Y2;X8;K\"AIpg\"\n"
        "C;Y2;X9;K\"3\"\n"
        "C;Y3;X1;K\"AInv\"\nC;Y3;X2;K\"AInv\"\nC;Y3;X3;K\"6\"\n"
        "C;Y3;X5;K\"1\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"1\"\nE\n";
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
    float const before = f.enemy->health.value;
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(f.enemy->health.value < before);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
    re_done(&f);
}

/* AIpl is caster-only; AIsl applies in its authored area. Both stop on damage
 * through the ordinary attack event, and recovery ticks through G_RunEntity. */
TEST(retail_item_effect, regeneration_items_follow_target_damage_and_scheduler) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIsl\"\nC;Y2;X2;K\"AIsl\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"30\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"10\"\nC;Y2;X7;K\"80\"\nC;Y2;X8;K\"BIrg\"\n"
        "C;Y3;X1;K\"AIpl\"\nC;Y3;X2;K\"AIpl\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"0\"\nC;Y3;X5;K\"100\"\nC;Y3;X6;K\"10\"\nC;Y3;X7;K\"80\"\nC;Y3;X8;K\"BIrg\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y2;X1;K\"test\"\nC;Y2;X2;K\"AIsl,AIpl\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    edict_t *ally = alloc_test_unit(FS_SLKKey("hfoo"), 40, 0);
    ally->s.player = 0; ally->svflags |= SVF_MONSTER; ally->spawn_time = level.time;
    f.hero->health.value = 400; ally->health.value = 400;
    f.hero->mana.value = 300; ally->mana.value = 300;
    static UnitBalance_t hero_balance;
    memset(&hero_balance, 0, sizeof(hero_balance));
    hero_balance.strength = 10;
    f.hero->data.UnitBalance = ally->data.UnitBalance = &hero_balance;
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AIsl"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->health.value, 403, 0.001f);
    T_FEQ(ally->health.value, 403, 0.001f);
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->health.value, 406, 0.001f);
    S_ResolveAttackHit(f.enemy, f.hero, 5);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    T_NOT_NULL(unit_findstatus(ally, FS_SLKKey("BIrg")));

    item = S_AbilityItem(FS_SLKKey("AIpl")); call.item = &item;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->mana.value, 310, 0.001f);
    T_FEQ(ally->mana.value, 300, 0.001f);
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->mana.value, 320, 0.001f);
    S_ResolveAttackHit(f.enemy, f.hero, 5);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->mana.value, 320, 0.001f);

    item = S_AbilityItem(FS_SLKKey("AIsl")); call.item = &item;
    float const before_final_regen = f.hero->health.value;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->health.value, before_final_regen + 3, 0.001f);
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 5));
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    float const after_spell_interrupt = f.hero->health.value;
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->health.value, after_spell_interrupt, 0.001f);
    re_done(&f);
}

#endif /* BZ_TESTS */
