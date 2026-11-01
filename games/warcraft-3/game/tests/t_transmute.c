#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANTM MAKEFOURCC('A', 'N', 't', 'm') // rawcode; TFT Alchemist Transmute

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);
void unit_die(edict_t *self, edict_t *attacker);

/* Non-stock Cost/DataA/DataC so tests cannot pass on retail 150/0.8/5. */
static cstring_t transmute_slk =
    "ID;PWXL;N;EBB;Y2;X12\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
    "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"BuffID1\"\n"
    "C;Y2;X1;K\"ANtm\"\nC;Y2;X2;K\"ANtm\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"air,ground,enemy,neutral,nonhero\"\n"
    "C;Y2;X5;K\"40\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"650\"\n"
    "C;Y2;X8;K\"0.5\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"3\"\n"
    "C;Y2;X11;K\"1\"\nC;Y2;X12;K\"BNtm\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster, *enemy, *ally, *hero;
    UnitBalance_t enemy_bal, ally_bal, hero_bal;
} tmFix_t;

/* Fill caller in place: edict UnitBalance pointers must not dangle. */
static void tm_setup(tmFix_t *fix) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(transmute_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->enemy_bal = MAKE(UnitBalance_t, .maxHealth = 500, .level = 2, .goldCost = 200, .lumberCost = 40);
    fix->ally_bal = MAKE(UnitBalance_t, .maxHealth = 500, .level = 2, .goldCost = 200);
    fix->hero_bal = MAKE(UnitBalance_t, .maxHealth = 500, .strength = 20, .level = 1, .goldCost = 425);
    fix->caster = alloc_test_unit(MAKEFOURCC('N', 'a', 'l', 'c'), 0, 0);
    fix->enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    fix->ally = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 96, 0);
    fix->hero = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), 128, 0);
    fix->caster->s.player = fix->ally->s.player = 0;
    fix->enemy->s.player = fix->hero->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->enemy->svflags |= SVF_MONSTER;
    fix->ally->svflags |= SVF_MONSTER; fix->hero->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->enemy->targtype = fix->ally->targtype = fix->hero->targtype = TARG_GROUND;
    fix->enemy->data.UnitBalance = &fix->enemy_bal;
    fix->ally->data.UnitBalance = &fix->ally_bal;
    fix->hero->data.UnitBalance = &fix->hero_bal;
    fix->caster->unit_state->ranks[0] = MAKE(heroability_t, .code = BZ_ANTM, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 200;
    fix->enemy->health.value = fix->enemy->health.max_value = 500;
    fix->ally->health.value = fix->ally->health.max_value = 500;
    fix->hero->health.value = fix->hero->health.max_value = 500;
    fix->enemy->die = unit_die; fix->ally->die = unit_die; fix->hero->die = unit_die;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 100;
    game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 50;
    game.clients[0].ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE] = 100;
    game.clients[0].ps.stats[PLAYERSTATE_LUMBER_UPKEEP_RATE] = 100;
}

static void tm_done(tmFix_t *fix) { G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows); }

TEST(wc3_spell, transmute_procedure_is_unit_spell) {
    abilityitem_t item = S_AbilityItem(BZ_ANTM);
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityTransmute);
    T_ASSERT(!(item.ability->flags & AB_CHANNEL));
    T_EQ(item.ability->target_type, SPELL_TARGET_UNIT);
}

/* Kills the target and credits goldCost * DataA (200 * 0.5 = 100). */
TEST(wc3_spell, transmute_kills_and_credits_gold_factor) {
    tmFix_t fix; tm_setup(&fix);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_FEQ(fix.caster->mana.value, 160, 0.001f);
    T_ASSERT(M_IsDead(fix.enemy));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 200);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 50);
    tm_done(&fix);
}

/* DataB lumberCostFactor credits lumber when authored non-zero. */
TEST(wc3_spell, transmute_credits_lumber_when_datab_set) {
    tmFix_t fix; tm_setup(&fix);
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
        "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"BuffID1\"\n"
        "C;Y2;X1;K\"ANtm\"\nC;Y2;X2;K\"ANtm\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"air,ground,enemy,neutral,nonhero\"\n"
        "C;Y2;X5;K\"40\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"650\"\n"
        "C;Y2;X8;K\"0.5\"\nC;Y2;X9;K\"0.5\"\nC;Y2;X10;K\"3\"\n"
        "C;Y2;X11;K\"1\"\nC;Y2;X12;K\"BNtm\"\nE\n";
    G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows);
    fix.rows = parse_slk_string(slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 200);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 70); /* 50 + 40*0.5 */
    tm_done(&fix);
}

/* Rejects ally/dead/hero/over-level without spending mana. */
TEST(wc3_spell, transmute_rejects_invalid_targets_without_mana_spend) {
    tmFix_t fix; tm_setup(&fix);
    float mana = fix.caster->mana.value;

    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.ally));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);

    fix.enemy->health.value = 0;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    fix.enemy->health.value = 500;

    T_ASSERT(G_UnitIsHero(fix.hero));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.hero));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);

    fix.enemy->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','C','s','k'), .level = 1);
    T_ASSERT(S_UnitIsResistant(fix.enemy));
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    memset(fix.enemy->unit_state->ranks, 0, sizeof(fix.enemy->unit_state->ranks));

    fix.enemy_bal.level = 4;
    T_ASSERT(!S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_FEQ(fix.caster->mana.value, mana, 0.001f);
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    tm_done(&fix);
}


/* Stock-like ANab with non-stock damage/armor and a short duration.  Both
 * projectile and status paths run against synthetic archive definitions. */
static cstring_t alchemist_acid_slk =
    "ID;PWXL;N;EBB;Y2;X14\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
    "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
    "C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Area1\"\n"
    "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
    "C;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"DataD1\"\n"
    "C;Y1;X13;K\"DataE1\"\nC;Y1;X14;K\"BuffID1\"\n"
    "C;Y2;X1;K\"ANab\"\nC;Y2;X2;K\"ANab\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K\"air,ground,enemy,neutral,organic\"\n"
    "C;Y2;X5;K0\nC;Y2;X6;K0\nC;Y2;X7;K700\n"
    "C;Y2;X8;K120\nC;Y2;X9;K4\nC;Y2;X10;K4\n"
    "C;Y2;X11;K4\nC;Y2;X12;K9\nC;Y2;X13;K5.5\n"
    "C;Y2;X14;K\"BNab\"\nE\n";

TEST(wc3_spell, alchemist_acid_bomb_splashes_debuffs_and_stops_after_dispel) {
    tmFix_t fix; tm_setup(&fix);
    edict_t *splash = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 82, 0);
    edict_t *missile = NULL, *primary_dot = NULL;
    float const full_health = 500;
    slkTestData_t *acid = parse_slk_string(alchemist_acid_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", acid);
    fix.caster->unit_state->ranks[0] = MAKE(heroability_t, .code = FS_SLKKey("ANab"), .level = 1);
    splash->s.player = 1;
    splash->svflags |= SVF_MONSTER;
    splash->targtype = TARG_GROUND;
    splash->data.UnitBalance = &fix.enemy_bal;
    splash->health.value = splash->health.max_value = full_health;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, FS_SLKKey("ANab"), fix.enemy));
    T_FEQ(fix.enemy->health.value, full_health, 0.001f); /* impact deferred */
    FILTER_EDICTS(ent, ent->owner == fix.caster && ent->class_id == FS_SLKKey("ANab") &&
                  ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    if (missile && missile->currentmove && missile->currentmove->endfunc)
        missile->currentmove->endfunc(missile);
    T_FEQ(unit_status_modifier_total(fix.enemy, WC3_STATUS_MOD_ARMOR_FLAT), -4, 0.001f);
    T_FEQ(unit_status_modifier_total(splash, WC3_STATUS_MOD_ARMOR_FLAT), -4, 0.001f);
    T_FEQ(unit_status_modifier_total(fix.ally, WC3_STATUS_MOD_ARMOR_FLAT), 0, 0.001f);
    FILTER_EDICTS(ent, ent->owner == fix.caster && ent->goalentity == fix.enemy &&
                  ent->think == acid_bomb_think) { primary_dot = ent; break; }
    T_NOT_NULL(primary_dot);
    level.time += 1000;
    if (primary_dot) G_RunEntity(primary_dot);
    T_ASSERT(fix.enemy->health.value < full_health);
    T_FEQ(fix.ally->health.value, full_health, 0.001f);
    heroabilitystatus_t *status = unit_findstatus(fix.enemy, FS_SLKKey("BNab"));
    T_NOT_NULL(status);
    if (status) unit_removestatus(fix.enemy, status, STATUS_REMOVE_DISPEL);
    T_FEQ(unit_status_modifier_total(fix.enemy, WC3_STATUS_MOD_ARMOR_FLAT), 0, 0.001f);
    float const after_dispel = fix.enemy->health.value;
    level.time += 1000;
    if (primary_dot && primary_dot->inuse) G_RunEntity(primary_dot);
    T_FEQ(fix.enemy->health.value, after_dispel, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(acid);
    tm_done(&fix);
}

TEST(wc3_spell, alchemist_acid_bomb_thinker_survives_save_load_with_status_identity) {
    cstring_t const filename = Test_TempPath("alchemist-acid-bomb.bin");
    tmFix_t fix; tm_setup(&fix);
    slkTestData_t *acid = parse_slk_string(alchemist_acid_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", acid);
    edict_t *missile = NULL, *dot = NULL;
    fix.caster->unit_state->ranks[0] = MAKE(heroability_t, .code = FS_SLKKey("ANab"), .level = 1);
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, FS_SLKKey("ANab"), fix.enemy));
    FILTER_EDICTS(ent, ent->owner == fix.caster && ent->movetype == MOVETYPE_FLYMISSILE)
        { missile = ent; break; }
    if (missile && missile->currentmove && missile->currentmove->endfunc)
        missile->currentmove->endfunc(missile);
    FILTER_EDICTS(ent, ent->think == acid_bomb_think && ent->goalentity == fix.enemy)
        { dot = ent; break; }
    T_NOT_NULL(dot);
    if (dot) {
        uint32_t const instance = dot->variation;
        T_ASSERT(WriteGame(filename));
        dot->variation = 0;
        dot->think = NULL;
        T_ASSERT(ReadGame(filename));
        T_ASSERT(dot->think == acid_bomb_think);
        T_EQ(dot->variation, instance);
        T_FEQ(unit_status_modifier_total(fix.enemy, WC3_STATUS_MOD_ARMOR_FLAT), -4, 0.001f);
    }
    remove(filename);
    G_SetSLKRows("AbilityData", old); free_slk_rows(acid);
    tm_done(&fix);
}

TEST(wc3_spell, alchemist_transmute_datad_zero_suppresses_normal_kill_bounty) {
    tmFix_t fix; tm_setup(&fix);
    char slk[1024];
    char *flag;
    slkTestData_t *rows, *old;
    strlcpy(slk, transmute_slk, sizeof(slk));
    flag = strstr(slk, "C;Y2;X11;K\"1\"");
    T_NOT_NULL(flag);
    if (!flag) { tm_done(&fix); return; }
    flag[strlen("C;Y2;X11;K\"")] = '0';
    rows = parse_slk_string(slk);
    old = G_SetSLKRows("AbilityData", rows);
    fix.enemy_bal.goldBountyBase = 31;
    game.clients[1].ps.stats[PLAYERSTATE_GIVES_BOUNTY] = 1;
    T_ASSERT(S_CastUnitTargetSpell(fix.caster, BZ_ANTM, fix.enemy));
    T_EQ(game.clients[0].ps.stats[PLAYERSTATE_RESOURCE_GOLD], 200);
    T_ASSERT(M_IsDead(fix.enemy));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    tm_done(&fix);
}

#endif
