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

TEST(retail_item_effect, pres_restores_authored_health_and_mana) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"6\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AIre\"\nC;Y3;X2;K\"AIre\"\n"
        "C;Y3;X3;K\"500\"\nC;Y3;X4;K\"200\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"perishable\"\n"
        "C;Y2;X1;K\"pres\"\nC;Y2;X2;K\"AIre\"\nC;Y2;X3;K\"true\"\nE\n";
    retailEffectFixture_t f;
    static ItemData_t item_data = { .abilList = "AIre", .perishable = true };
    edict_t *item, *player;
    re_init(&f, ability_slk, items);
    /* This contract is independent of the inventory permission schema. Use
     * the classic Hero default slots so the test isolates AIre's use effect. */
    ((mapInfo_t *)level.mapinfo)->fileFormat = 0;
    player = &g_edicts[0];
    player->client->ps.number = 0;
    f.hero->s.player = 0;
    G_SelectEntity(player->client, f.hero);
    T_EQ(G_GetMainSelectedUnit(player->client), f.hero);
    f.hero->health.value = 100;
    f.hero->mana.value = 350;
    item = re_equip(&f, "pres", 0);
    item->data.ItemData = &item_data;
    G_GetItem(item)->charges = 1;
    G_UseItem(f.hero, 0);
    T_FEQ(f.hero->health.value, 500, 0.001f);
    T_FEQ(f.hero->mana.value, 500, 0.001f);
    T_NULL(G_InventoryItem(f.hero, 0));
    re_done(&f);
}

TEST(retail_item_effect, healing_salve_targets_friendly_unit_and_interrupts_on_damage) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"Range1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"targs1\"\nC;Y1;X10;K\"DataC1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"6\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\nC;Y2;X10;K\"1\"\n"
        "C;Y3;X1;K\"AIrl\"\nC;Y3;X2;K\"AIrl\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"400\"\nC;Y3;X5;K\"45\"\nC;Y3;X6;K\"45\"\n"
        "C;Y3;X7;K\"50\"\nC;Y3;X8;K\"BIrg\"\nC;Y3;X9;K\"air,ground,friend,organic\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"hslv\"\nC;Y2;X2;K\"AIrl\"\nC;Y2;X3;K\"3\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *ally, *attacker, *player, *salve;
    gameClient_t *client;
    re_init(&f, ability_slk, items);
    f.hero->s.player = 0;
    f.enemy->s.player = 0;
    f.enemy->health.value = 100;
    ally = f.enemy;
    attacker = alloc_test_unit(FS_SLKKey("hfoo"), 128, 0);
    attacker->s.player = 1;
    attacker->svflags |= SVF_MONSTER;
    attacker->spawn_time = level.time;
    player = &g_edicts[0];
    static UnitBalance_t no_regen_balance;
    memset(&no_regen_balance, 0, sizeof(no_regen_balance));
    no_regen_balance.healthRegenType = "none";
    ally->data.UnitBalance = &no_regen_balance;
    player->client->ps.number = 0;
    client = player->client;
    G_SelectEntity(client, f.hero);
    salve = re_equip(&f, "hslv", 0);
    G_GetItem(salve)->charges = 3;
    G_UseItem(f.hero, 0);
    T_NOT_NULL(client->menu.on_entity_selected);
    if (client->menu.on_entity_selected) client->menu.on_entity_selected(player, ally);
    T_NOT_NULL(unit_findstatus(ally, FS_SLKKey("BIrg")));
    T_EQ(G_GetItem(salve)->charges, 2);
    float const after_cast = ally->health.value;
    re_run_frame_at(ally, level.time + 1000);
    T_FEQ(ally->health.value, after_cast + 400.0f / 45.0f, 0.01f);
    S_ResolveAttackHit(attacker, ally, 10);
    T_NULL(unit_findstatus(ally, FS_SLKKey("BIrg")));
    float const after_interrupt = ally->health.value;
    re_run_frame_at(ally, level.time + 1000);
    T_FEQ(ally->health.value, after_interrupt, 0.001f);
    re_done(&f);
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
    static UnitWeapons_t ground_only_weapons = { .attacksEnabled = 1 };
    retailEffectFixture_t f;
    memset(&target_balance, 0, sizeof(target_balance));
    re_init(&f, ability_slk, items);
    f.enemy->data.UnitBalance = &target_balance;
    f.hero->data.UnitWeapons = &ground_only_weapons;
    f.hero->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    memset(&f.hero->unit_state->attack2, 0, sizeof(f.hero->unit_state->attack2));
    f.enemy->targtype = TARG_AIR;
    T_ASSERT(!S_AttackCanTarget(f.hero, f.enemy));
    re_equip(&f, "oven", 0);
    T_FEQ(f.hero->unit_state->attack1.temporaryDamageBonus, 5, 0.001f);
    T_ASSERT(S_AttackCanTarget(f.hero, f.enemy));
    f.enemy->targtype = TARG_GROUND;
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpb")));
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("BIpd")));
    uint32_t const first_poison_tick = unit_findstatus(f.enemy, FS_SLKKey("BIpb"))->next_tick;
    S_ResolveAttackHit(f.hero, f.enemy, 20);
    T_EQ(unit_findstatus(f.enemy, FS_SLKKey("BIpb"))->next_tick, first_poison_tick);
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

TEST(retail_item_effect, necklace_of_spell_immunity_protects_its_carrier) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"6\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"nspi\"\nC;Y2;X2;K\"AImx\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    re_equip(&f, "nspi", 0);
    T_ASSERT(S_UnitSpellImmune(f.hero));
    T_ASSERT(!S_SpellDamage(f.hero, f.enemy, 40));
    T_FEQ(f.hero->health.value, 500, 0.001f);
    T_ASSERT(G_DropItem(f.hero, 0));
    T_ASSERT(!S_UnitSpellImmune(f.hero));
    re_done(&f);
}

TEST(retail_item_effect, runed_bracers_reduce_spell_damage_from_authored_datab) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\n"
        "C;Y2;X3;K\"6\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AIsr\"\nC;Y3;X2;K\"AIsr\"\n"
        "C;Y3;X3;K\"0\"\nC;Y3;X4;K\"0.33\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"brac\"\nC;Y2;X2;K\"AIsr\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    float held_damage, unheld_damage;
    re_init(&f, ability_slk, items);
    re_equip(&f, "brac", 0);
    T_FEQ(S_ItemSpellDamageReduction(f.hero), 0.33f, 0.001f);
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 100));
    held_damage = 500.0f - f.hero->health.value;
    T_ASSERT(G_DropItem(f.hero, 0));
    T_FEQ(S_ItemSpellDamageReduction(f.hero), 0.0f, 0.001f);
    T_ASSERT(S_SpellDamage(f.hero, f.enemy, 100));
    unheld_damage = 500.0f - held_damage - f.hero->health.value;
    T_FEQ(held_damage / unheld_damage, 0.67f, 0.02f);
    re_done(&f);
}

TEST(retail_item_effect, shield_of_honor_provides_authored_command_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"targs1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\n"
        "C;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AIcd\"\nC;Y3;X2;K\"AOac\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\nC;Y3;X5;K\"1\"\nC;Y3;X6;K\"1\"\n"
        "C;Y3;X7;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X8;K\"BOac\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"shhn\"\nC;Y2;X2;K\"AIcd\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "shhn", 0);
    level.framenum++;
    T_FEQ(S_CommandAuraAttackBonus(f.enemy), 0.1f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_CommandAuraAttackBonus(f.enemy), 0.0f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 64, 0);
    T_ASSERT(G_DropItem(f.hero, 0));
    level.framenum++;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_CommandAuraAttackBonus(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, warsong_battle_drums_provide_authored_command_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\n"
        "C;Y1;X6;K\"targs1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"AIcd\"\nC;Y3;X2;K\"AOac\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\n"
        "C;Y3;X6;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X7;K\"BOac\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"ward\"\nC;Y2;X2;K\"AIcd\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "ward", 0);
    level.framenum++;
    T_FEQ(S_CommandAuraAttackBonus(f.enemy), 0.1f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, legion_doom_horn_provides_authored_unholy_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"targs1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"AIau\"\nC;Y3;X2;K\"AUau\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\nC;Y3;X5;K\"0.5\"\n"
        "C;Y3;X7;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X8;K\"BUau\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"droppable\"\n"
        "C;Y2;X1;K\"lgdh\"\nC;Y2;X2;K\"AIau\"\nC;Y2;X3;K\"1\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "lgdh", 0);
    level.framenum++;
    T_FEQ(S_UnholyMoveBonus(f.enemy), 0.1f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(f.enemy), 0.5f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(f.enemy), 0.0f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(f.enemy), 0.0f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 64, 0);
    T_ASSERT(G_DropItem(f.hero, 0));
    level.framenum++;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_UnholyMoveBonus(f.enemy), 0.0f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, allerias_flute_boosts_nearby_ranged_units) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"targs1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"AIar\"\nC;Y3;X2;K\"AEar\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\n"
        "C;Y3;X6;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X7;K\"BEar\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"afac\"\nC;Y2;X2;K\"AIar\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    f.enemy->unit_state->attack1.type = ATK_NORMAL;
    re_equip(&f, "afac", 0);
    level.framenum++;
    T_FEQ(S_TrueshotAttackBonus(f.enemy), 0.0f, 0.001f);
    f.enemy->unit_state->attack1.weapon = WPN_MISSILE;
    T_FEQ(S_TrueshotAttackBonus(f.enemy), 0.1f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_TrueshotAttackBonus(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, mindstaff_provides_authored_brilliance_aura) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"targs1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X4;K\"6\"\nC;Y2;X5;K\"1\"\n"
        "C;Y3;X1;K\"AIba\"\nC;Y3;X2;K\"AHab\"\n"
        "C;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.75\"\n"
        "C;Y3;X6;K\"air,ground,friend,self,vuln,invu\"\nC;Y3;X7;K\"BHab\"\n"
        "C;Y4;X1;K\"AI2m\"\nC;Y4;X2;K\"AImm\"\nC;Y4;X4;K\"200\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"mnsf\"\nC;Y2;X2;K\"AI2m,AIba\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->s.player = 0;
    re_equip(&f, "mnsf", 0);
    level.framenum++;
    T_FEQ(S_BrillianceManaRegen(f.enemy), 0.75f, 0.001f);
    Vector2_set(&f.enemy->s.origin2, 1000, 0);
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_BrillianceManaRegen(f.enemy), 0.0f, 0.001f);
    re_done(&f);
}

TEST(retail_item_effect, rusty_mining_pick_bash_stuns_on_attack) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y4;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataC1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"DataB1\"\n"
        "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"6\"\nC;Y2;X4;K\"1\"\n"
        "C;Y3;X1;K\"AItg\"\nC;Y3;X2;K\"AItg\"\nC;Y3;X3;K\"1\"\n"
        "C;Y4;X1;K\"AIbx\"\nC;Y4;X2;K\"AHbh\"\nC;Y4;X3;K\"100\"\n"
        "C;Y4;X4;K\"25\"\nC;Y4;X5;K\"2\"\nC;Y4;X6;K\"2\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"rump\"\nC;Y2;X2;K\"AItg,AIbx\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->unit_state->attack1.type = ATK_NORMAL;
    f.enemy->targtype = TARG_GROUND;
    re_equip(&f, "rump", 0);
    S_ResolveAttackHit(f.hero, f.enemy, 10);
    T_ASSERT(f.enemy->health.value < 500.0f);
    T_NOT_NULL(unit_findstatus(f.enemy, MAKEFOURCC('B','s','t','u')));
    T_EQ(unit_findstatus(f.enemy, MAKEFOURCC('B','s','t','u'))->timestamp, level.time + 2000);
    re_done(&f);
}


TEST(retail_item_effect, item_cooldown_id_blocks_shared_item_use) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
        "C;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"Cool1\"\nC;Y1;X8;K\"CasterArt\"\n"
        "C;Y2;X1;K\"AItb\"\nC;Y2;X2;K\"AItb\"\n"
        "C;Y2;X3;K\"1\"\nC;Y2;X4;K\"air,ground,enemy\"\n"
        "C;Y2;X5;K\"100\"\nC;Y2;X6;K\"20\"\n"
        "C;Y2;X7;K\"30\"\n"
        "C;Y2;X8;K\"Abilities\\\\Spells\\\\Items\\\\AItb\\\\AItbTarget.mdl\"\nE\n";
    char const *items = "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"dust\"\nC;Y2;X2;K\"AItb\"\nE\n";
    static ItemData_t item_data = { .abilList = "AItb", .cooldownID = "A001" };
    retailEffectFixture_t f;
    edict_t *player, *dust;
    gameClient_t *client;
    uint32_t const shared_cooldown = MAKEFOURCC('A','0','0','1');

    re_init(&f, ability_slk, items);
    player = G_GetPlayerEntityByNumber(0);
    client = player ? player->client : NULL;
    T_NOT_NULL(client);
    if (!client) { re_done(&f); return; }
    f.hero->s.player = 0;
    G_SelectEntity(client, f.hero);
    dust = re_equip(&f, "dust", 0);
    dust->data.ItemData = &item_data;

    G_UseItem(f.hero, 0);
    T_NOT_NULL(unit_findstatus(f.enemy, FS_SLKKey("Bdet")));
    T_ASSERT(!S_SpellCooldownReady(f.hero, shared_cooldown));
    unit_removestatus(f.enemy, unit_findstatus(f.enemy, FS_SLKKey("Bdet")), STATUS_REMOVE_SCRIPT);
    G_UseItem(f.hero, 0);
    T_NULL(unit_findstatus(f.enemy, FS_SLKKey("Bdet")));
    T_ASSERT(!S_SpellCooldownReady(f.hero, shared_cooldown));

    level.time += 30000;
    G_UseItem(f.hero, 0);
    T_NOT_NULL(unit_findstatus(f.enemy, FS_SLKKey("Bdet")));
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
    T_FEQ(f.hero->health.value, 401, 0.001f);

    /* Damage at the exact expiry timestamp interrupts before the scheduler can
     * apply the final due regeneration pulse. */
    item = S_AbilityItem(FS_SLKKey("AIsl")); call.item = &item;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    float const before_expiry_hit = f.hero->health.value;
    level.time += 10000;
    S_ResolveAttackHit(f.enemy, f.hero, 5);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    T_FEQ(f.hero->health.value, before_expiry_hit - 5, 0.001f);

    re_run_frame_at(ally, level.time + 1000);
    T_ASSERT(ally->health.value > 403.0f);

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


/* AIp1-AIp4 are caster-only replenishment potions. AIp5-AIp6 are area
 * replenishment scrolls. The retail AbilityData rows provide each amount,
 * duration, area, and BIrg lifecycle. */
TEST(retail_item_effect, replenishment_potions_and_scrolls_use_authored_targets_and_interrupt) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y7;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"Dur1\"\nC;Y1;X7;K\"HeroDur1\"\n"
        "C;Y1;X8;K\"Area1\"\nC;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"targs1\"\n"
        "C;Y2;X1;K\"AIp1\"\nC;Y2;X2;K\"AIp1\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"25\"\nC;Y2;X6;K\"30\"\nC;Y2;X7;K\"30\"\nC;Y2;X8;K\"0\"\nC;Y2;X9;K\"BIrg\"\nC;Y2;X10;K\"friend,self,organic\"\n"
        "C;Y3;X1;K\"AIp5\"\nC;Y3;X2;K\"AIp5\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"250\"\nC;Y3;X5;K\"100\"\nC;Y3;X6;K\"45\"\nC;Y3;X7;K\"45\"\nC;Y3;X8;K\"100\"\nC;Y3;X9;K\"BIrg\"\nC;Y3;X10;K\"friend,self,organic\"\n"
        "C;Y4;X1;K\"AIp6\"\nC;Y4;X2;K\"AIp6\"\nC;Y4;X3;K\"1\"\n"
        "C;Y4;X4;K\"450\"\nC;Y4;X5;K\"150\"\nC;Y4;X6;K\"45\"\nC;Y4;X7;K\"45\"\nC;Y4;X8;K\"100\"\nC;Y4;X9;K\"BIrg\"\nC;Y4;X10;K\"friend,self,organic\"\nE\n";
    retailEffectFixture_t f;
    edict_t *nearby, *outside, *target;
    abilityitem_t item;
    abilityCall_t call;
    static UnitBalance_t no_regen;
    memset(&no_regen, 0, sizeof(no_regen));
    no_regen.healthRegenType = "none";
    re_init(&f, ability_slk, "ID;PWXL;N;EBB;Y2;X2\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y2;X1;K\"test\"\nC;Y2;X2;K\"AIp1,AIp5\"\nE\n");
    f.hero->data.UnitBalance = &no_regen;
    f.hero->health.value = 100; f.hero->health.max_value = 500;
    f.hero->mana.value = 50; f.hero->mana.max_value = 250;
    nearby = alloc_test_unit(FS_SLKKey("hfoo"), 40, 0);
    outside = alloc_test_unit(FS_SLKKey("hfoo"), 160, 0);
    nearby->s.player = outside->s.player = 0;
    nearby->svflags |= SVF_MONSTER; outside->svflags |= SVF_MONSTER;
    nearby->spawn_time = outside->spawn_time = level.time;
    nearby->targtype = outside->targtype = TARG_GROUND;
    nearby->data.UnitBalance = outside->data.UnitBalance = &no_regen;
    nearby->health.value = 100; nearby->health.max_value = 500;
    nearby->mana.value = 50; nearby->mana.max_value = 250;
    outside->health.value = 100; outside->health.max_value = 500;
    outside->mana.value = 50; outside->mana.max_value = 250;

    item = S_AbilityItem(FS_SLKKey("AIp1"));
    T_EQ(item.ability ? item.ability->proc : 0, CAbilityItemRegeneration);
    for (uint32_t i = 2; i <= 4; i++) { char code[] = {'A', 'I', 'p', (char)('0' + i), '\0'}; T_EQ(S_AbilityItem(FS_SLKKey(code)).ability->proc, CAbilityItemRegeneration); }
    call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(G_AbilityLevel(FS_SLKKey("AIp1"), 1) != NULL);
    T_ASSERT(S_SpellIsAliveTarget(f.hero));
    T_ASSERT(S_SpellIsFriend(f.hero, f.hero));
    T_ASSERT(S_SpellAllowsAreaTarget(FS_SLKKey("AIp1"), f.hero, f.hero));
    T_FEQ(S_SpellHeroDuration(FS_SLKKey("AIp1"), 1, f.hero), 30.0f, 0.001f);
    T_ASSERT(G_AbilityLevel(FS_SLKKey("AIp1"), 1)->area >= 0.0f);
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->health.value, 100.0f + 100.0f / 30.0f, 0.001f);
    T_FEQ(f.hero->mana.value, 50.0f + 25.0f / 30.0f, 0.001f);
    T_NULL(unit_findstatus(nearby, FS_SLKKey("BIrg")));
    S_ResolveAttackHit(f.enemy, f.hero, 5);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));

    target = nearby;
    item = S_AbilityItem(FS_SLKKey("AIp5")); call.item = &item;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_NOT_NULL(unit_findstatus(nearby, FS_SLKKey("BIrg")));
    T_FEQ(nearby->health.value, 100.0f + 250.0f / 45.0f, 0.001f);
    T_FEQ(nearby->mana.value, 50.0f + 100.0f / 45.0f, 0.001f);
    T_ASSERT(S_SpellIsAliveTarget(nearby));
    T_ASSERT(S_SpellIsFriend(f.hero, nearby));
    T_ASSERT(S_SpellAllowsAreaTarget(FS_SLKKey("AIp5"), f.hero, nearby));
    T_NULL(unit_findstatus(outside, FS_SLKKey("BIrg")));
    S_ResolveAttackHit(f.enemy, target, 5);
    T_NULL(unit_findstatus(target, FS_SLKKey("BIrg")));
    float const after_damage = target->health.value;
    re_run_frame_at(target, level.time + 1000);
    T_FEQ(target->health.value, after_damage, 0.001f);
    target->health.value = 100; target->mana.value = 50;
    item = S_AbilityItem(FS_SLKKey("AIp6")); call.item = &item;
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(target->health.value, 100.0f + 450.0f / 45.0f, 0.001f);
    T_FEQ(target->mana.value, 50.0f + 150.0f / 45.0f, 0.001f);
    re_done(&f);
}


/* AIpr is the full Clarity Potion: caster-only mana restoration over 45 s. */
TEST(retail_item_effect, clarity_potion_regenerates_mana_and_interrupts_on_damage) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIpr\"\nC;Y2;X2;K\"AIpr\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"200\"\nC;Y2;X5;K\"45\"\nC;Y2;X6;K\"45\"\nC;Y2;X7;K\"BIrg\"\nE\n";
    retailEffectFixture_t f;
    static UnitBalance_t no_regen;
    memset(&no_regen, 0, sizeof(no_regen));
    no_regen.healthRegenType = "none";
    re_init(&f, ability_slk, "ID;PWXL;N;EBB;Y2;X2\nC;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y2;X1;K\"pclr\"\nC;Y2;X2;K\"AIpr\"\nE\n");
    f.hero->data.UnitBalance = &no_regen;
    f.hero->mana.value = 100; f.hero->mana.max_value = 500;
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AIpr"));
    T_EQ(item.ability ? item.ability->proc : 0, CAbilityItemRegeneration);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(S_AbilityMessage(f.hero, A_ITEM_USE, &call));
    T_FEQ(f.hero->health.value, 500.0f, 0.001f);
    T_FEQ(f.hero->mana.value, 100.0f + 200.0f / 45.0f, 0.001f);
    T_NOT_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    S_ResolveAttackHit(f.enemy, f.hero, 10);
    T_NULL(unit_findstatus(f.hero, FS_SLKKey("BIrg")));
    float const after_interrupt = f.hero->mana.value;
    re_run_frame_at(f.hero, level.time + 1000);
    T_FEQ(f.hero->mana.value, after_interrupt, 0.001f);
    re_done(&f);
}


/* Retail Flare Gun (fgun/AIfa) is a charged, point-targeted Flare. */
TEST(retail_item_effect, flare_gun_targets_a_point_and_reveals_authored_radius) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"Area1\"\n"
        "C;Y2;X1;K\"AIfa\"\nC;Y2;X2;K\"AIfa\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"20\"\nC;Y2;X5;K\"100\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"fgun\"\nC;Y2;X2;K\"AIfa\"\nC;Y2;X3;K\"3\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player, *item;
    vec2_t center = { 128, 128 }, nearby = { 180, 128 }, outside = { 512, 128 };
    cstring_t point_click[] = { "point", "128", "128" };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    game.clients[0].ps.rdflags &= ~RDF_NOFOG;
    f.hero->s.player = 0; f.hero->svflags |= SVF_MONSTER; f.hero->targtype = TARG_GROUND;
    f.hero->runtime.sight_radius.day = f.hero->runtime.sight_radius.night = 0;
    G_SelectEntity(player->client, f.hero);
    item = re_equip(&f, "fgun", 0);
    G_GetItem(item)->charges = 3;
    G_FowInit(); G_FowConnectPlayer(0);
    G_FowUpdate();
    abilityitem_t flare = S_AbilityItem(FS_SLKKey("AIfa"));
    T_NOT_NULL(flare.ability);
    if (!flare.ability) { G_FowShutdown(); re_done(&f); return; }
    T_EQ(flare.ability->proc, CAbilityFlare);
    G_UseItem(f.hero, 0);
    T_NOT_NULL(player->client->menu.on_location_selected);
    T_EQ(G_ItemCharges(item), 3);
    G_ClientCommand(player, 3, point_click);
    T_NULL(player->client->menu.on_location_selected);
    T_EQ(G_ItemCharges(item), 2);
    T_ASSERT(G_FowPointIsVisible(0, &center));
    T_ASSERT(G_FowPointIsVisible(0, &nearby));
    T_ASSERT(!G_FowPointIsVisible(0, &outside));
    FILTER_EDICTS(thinker, thinker->inuse && thinker->owner == f.hero &&
                  thinker->class_id == FS_SLKKey("AIfa") && G_GetChannel(thinker)) {
        T_FEQ(thinker->s.origin2.x, center.x, 0.001f);
        T_FEQ(thinker->s.origin2.y, center.y, 0.001f);
        T_EQ(thinker->spawn_time, level.time + 20000);
        G_FowShutdown(); re_done(&f); return;
    }
    T_ASSERT(false);
    G_FowShutdown(); re_done(&f);
}

/* Retail Crystal Ball (crys/AIta) reveals the selected point for ten seconds. */
TEST(retail_item_effect, crystal_ball_targets_a_point_and_reveals_authored_radius) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AIta\"\nC;Y2;X2;K\"AIta\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"10\"\nC;Y2;X5;K\"900\"\nC;Y2;X6;K\"3\"\nC;Y2;X7;K\"Bdet\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"perishable\"\n"
        "C;Y2;X1;K\"crys\"\nC;Y2;X2;K\"AIta\"\nC;Y2;X3;K\"false\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player;
    vec2_t center = { 128, 128 }, nearby = { 768, 128 }, outside = { -900, 128 };
    cstring_t point_click[] = { "point", "128", "128" };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    game.clients[0].ps.rdflags &= ~RDF_NOFOG;
    f.hero->s.player = 0; f.hero->svflags |= SVF_MONSTER; f.hero->targtype = TARG_GROUND;
    f.hero->runtime.sight_radius.day = f.hero->runtime.sight_radius.night = 0;
    G_SelectEntity(player->client, f.hero);
    re_equip(&f, "crys", 0);
    f.enemy->s.origin2 = nearby;
    f.enemy->s.origin.x = nearby.x; f.enemy->s.origin.y = nearby.y;
    f.enemy->runtime.sight_radius.day = f.enemy->runtime.sight_radius.night = 0;
    f.enemy->s.renderfx |= RF_HIDDEN;
    T_ASSERT(unit_addtimedstatus(f.enemy, "Binv", 1, 20.0f));
    T_ASSERT(!S_UnitIsDetectedByPlayer(f.enemy, 0));
    G_FowInit(); G_FowConnectPlayer(0); G_FowUpdate();
    T_ASSERT(!G_FowPointIsVisible(0, &outside));
    abilityitem_t reveal = S_AbilityItem(FS_SLKKey("AIta"));
    T_NOT_NULL(reveal.ability);
    if (!reveal.ability) { G_FowShutdown(); re_done(&f); return; }
    T_EQ(reveal.ability->proc, CAbilityItemDetectAoe);
    G_UseItem(f.hero, 0);
    T_NOT_NULL(player->client->menu.on_location_selected);
    G_ClientCommand(player, 3, point_click);
    T_NULL(player->client->menu.on_location_selected);
    T_ASSERT(G_FowPointIsVisible(0, &center));
    T_ASSERT(G_FowPointIsVisible(0, &nearby));
    T_ASSERT(!G_FowPointIsVisible(0, &outside));
    T_ASSERT(S_UnitIsDetectedByPlayer(f.enemy, 0));
    FILTER_EDICTS(thinker, thinker->inuse && thinker->owner == f.hero &&
                  thinker->class_id == FS_SLKKey("AIta") && G_GetChannel(thinker)) {
        T_FEQ(thinker->s.origin2.x, center.x, 0.001f);
        T_FEQ(thinker->s.origin2.y, center.y, 0.001f);
        T_EQ(thinker->spawn_time, level.time + 10000);
        level.time += 10000;
        G_RunEntity(thinker);
        G_RunEntity(f.enemy);
        G_FowUpdate();
        T_ASSERT(!S_UnitIsDetectedByPlayer(f.enemy, 0));
        T_ASSERT(!G_FowPointIsVisible(0, &center));
        G_FowShutdown(); re_done(&f); return;
    }
    T_ASSERT(false);
    G_FowShutdown(); re_done(&f);
}

TEST(retail_item_effect, diamond_of_summoning_targets_area_and_relocates_up_to_twelve_owned_units) {
    static UnitData_t const foot_data = { .moveTypeName = "foot" };
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"targs1\"\n"
        "C;Y2;X1;K\"AUds\"\nC;Y2;X2;K\"AUds\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"700\"\nC;Y2;X5;K\"12\"\nC;Y2;X6;K\"99999\"\n"
        "C;Y2;X7;K\"air,ground,vuln,invu,player\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"dsum\"\nC;Y2;X2;K\"AUds\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player, *item, *owned[13];
    vec2_t source = { 1024, 0 };
    cstring_t point_click[] = { "point", "1024", "0" };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    game.clients[0].ps.rdflags &= ~RDF_NOFOG;
    f.hero->s.player = 0; f.hero->svflags |= SVF_MONSTER; f.hero->targtype = TARG_GROUND;
    f.hero->collision = 32;
    f.hero->data.UnitData = &foot_data;
    G_SelectEntity(player->client, f.hero);
    item = re_equip(&f, "dsum", 0);
    G_GetItem(item)->charges = 1;
    FOR_LOOP(i, 13) {
        owned[i] = alloc_test_unit(FS_SLKKey("hfoo"), source.x, source.y);
        owned[i]->s.player = 0; owned[i]->svflags |= SVF_MONSTER;
        owned[i]->targtype = TARG_GROUND;
        owned[i]->collision = 32;
        owned[i]->data.UnitData = &foot_data;
        owned[i]->movetype = MOVETYPE_STEP;
        owned[i]->spawn_time = level.time;
        gi.LinkEntity(owned[i]);
    }
    f.enemy->s.origin2 = source;
    f.enemy->s.player = 1;
    f.enemy->collision = 32;
    f.enemy->data.UnitData = &foot_data;
    gi.LinkEntity(f.hero);
    gi.LinkEntity(f.enemy);
    G_FowInit(); G_FowConnectPlayer(0);
    G_UseItem(f.hero, 0);
    T_NOT_NULL(player->client->menu.on_location_selected);
    if (!player->client->menu.on_location_selected) { G_FowShutdown(); re_done(&f); return; }
    G_ClientCommand(player, 3, point_click);
    T_NULL(player->client->menu.on_location_selected);
    T_EQ(G_ItemCharges(item), 0);
    uint32_t moved = 0;
    FOR_LOOP(i, 13) if (Vector2_distance(&owned[i]->s.origin2, &source) > 500.0f) moved++;
    T_EQ(moved, 12);
    T_FEQ(Vector2_distance(&owned[12]->s.origin2, &source), 0.0f, 0.001f);
    T_FEQ(f.enemy->s.origin2.x, source.x, 0.001f);
    T_FEQ(f.enemy->s.origin2.y, source.y, 0.001f);
    for (uint32_t i = 0; i < 12; i++) for (uint32_t j = i + 1; j < 12; j++)
        T_ASSERT(Vector2_distance(&owned[i]->s.origin2, &owned[j]->s.origin2) > 0.01f);
    G_FowShutdown(); re_done(&f);
}

TEST(retail_item_effect, spider_silk_targets_and_ensnares_an_enemy_flyer) {
    static UnitData_t const flyer_data = { .moveTypeName = "fly", .moveHeight = 180.0f };
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"BuffID1\"\n"
        "C;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\nC;Y1;X10;K\"DataC1\"\n"
        "C;Y2;X1;K\"AIwb\"\nC;Y2;X2;K\"AIwb\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"20\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"400\"\nC;Y2;X7;K\"Bwea,Bweb\"\n"
        "C;Y2;X8;K\"0.6\"\nC;Y2;X9;K\"200\"\nC;Y2;X10;K\"128\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\nC;Y1;X3;K\"uses\"\nC;Y1;X4;K\"perishable\"\n"
        "C;Y2;X1;K\"silk\"\nC;Y2;X2;K\"AIwb\"\nC;Y2;X3;K\"1\"\nC;Y2;X4;K\"true\"\nE\n";
    retailEffectFixture_t f;
    edict_t *player, *item;
    char number[16];
    cstring_t select[] = { "select", number };
    re_init(&f, ability_slk, items);
    player = &g_edicts[0];
    player->client->ps.number = 0;
    f.hero->s.player = 0;
    f.enemy->s.player = 1;
    f.enemy->targtype = TARG_AIR;
    f.enemy->aiflags |= AI_FLYING;
    f.enemy->unitinfo.FlyHeight = 180.0f;
    f.enemy->data.UnitData = &flyer_data;
    item = re_equip(&f, "silk", 0);
    G_GetItem(item)->charges = 1;
    G_SelectEntity(player->client, f.hero);
    abilityitem_t web = S_AbilityItem(FS_SLKKey("AIwb"));
    T_NOT_NULL(web.ability);
    if (!web.ability) { re_done(&f); return; }
    T_EQ(web.ability->proc, CAbilityWeb);
    G_UseItem(f.hero, 0);
    T_NOT_NULL(player->client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", f.enemy->s.number);
    G_ClientCommand(player, 2, select);
    T_NULL(player->client->menu.on_entity_selected);
    T_ASSERT(S_UnitIsEnsnared(f.enemy));
    T_EQ(G_UnitStatusLevel(f.enemy, FS_SLKKey("Bwea")), 1);
    T_ASSERT(!(f.enemy->aiflags & AI_FLYING));
    T_EQ(f.enemy->targtype, TARG_GROUND);
    level.time += 1000;
    S_RunAbilityUpdates(f.enemy);
    T_FEQ(f.enemy->unitinfo.FlyHeight, 0.0f, 0.001f);
    T_EQ(G_ItemCharges(item), 0);
    re_done(&f);
}

TEST(retail_item_effect, talisman_of_evasion_causes_attacks_to_miss_using_item_data) {
    char const *ability_slk =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AIev\"\nC;Y2;X2;K\"AEev\"\nC;Y2;X3;K\"1\"\nE\n";
    char const *items =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"evtl\"\nC;Y2;X2;K\"AIev\"\nE\n";
    retailEffectFixture_t f;
    re_init(&f, ability_slk, items);
    f.enemy->unit_state->attack1.type = ATK_NORMAL;
    f.enemy->unit_state->attack1.targetsAllowed = TARG_GROUND;
    float const initial_health = f.hero->health.value;
    S_ResolveAttackHit(f.enemy, f.hero, 10);
    T_FEQ(f.hero->health.value, initial_health - 10.0f, 0.001f);
    re_equip(&f, "evtl", 0);
    f.hero->health.value = initial_health;
    S_ResolveAttackHit(f.enemy, f.hero, 10);
    T_FEQ(f.hero->health.value, initial_health, 0.001f);
    re_done(&f);
}

#endif /* BZ_TESTS */
