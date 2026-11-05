#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t, float, float);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *);
void free_slk_rows(slkTestData_t *);

typedef struct {
    int source_row;
    cstring_t creep, url, ability;
    float magnitude, duration;
    cstring_t unit_rawcode, ability_alias; /* NULL unless checked against local ROC/TFT report */
} creepCase_t;
#include "t_neutral_creep_contract_cases.inc"
#define COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

/* These are ability-contract fixtures, NOT verified retail unit/rawcode bindings:
 * the repository's tests.mpq contains only a small synthetic UnitAbilities.slk.
 * Keep source names/URLs on every case so subsequent stock-data integration can
 * replace the generic caster without silently dropping variants. */
typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster, *ally, *enemy, *summon;
    UnitAbilities_t abilities;
} creepContractFix_t;

static void contract_setup(creepContractFix_t *f, cstring_t ability, cstring_t slk) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    f->rows = parse_slk_string(slk);
    f->old = G_SetSLKRows("AbilityData", f->rows);
    f->caster = alloc_test_unit(FS_SLKKey("hpri"), 0, 0);
    f->ally = alloc_test_unit(FS_SLKKey("hfoo"), 60, 0);
    f->enemy = alloc_test_unit(FS_SLKKey("ogru"), 100, 0);
    f->summon = alloc_test_unit(FS_SLKKey("ogru"), 120, 0);
    f->abilities = (UnitAbilities_t){ .abilList = ability };
    f->caster->data.UnitAbilities = &f->abilities;
    f->caster->s.player = f->ally->s.player = 0;
    f->enemy->s.player = f->summon->s.player = 1;
    edict_t *units[] = { f->caster, f->ally, f->enemy, f->summon };
    for (size_t i = 0; i < COUNT_OF(units); i++) {
        edict_t *unit = units[i];
        /* Stop cleanup in charm and ambush order paths transitions units back
         * to their normal stand move just like a real spawned unit. */
        unit->stand = unit_stand;
        /* alloc_test_unit() deliberately does not install the live unit
         * thinker. Contract cases that advance simulation frames need it for
         * passive ability updates as well as animation/order lifecycle. */
        unit->think = monster_think;
        unit->svflags |= SVF_MONSTER;
        unit->targtype = TARG_GROUND;
        unit->health.value = unit->health.max_value = 500;
        unit->mana.value = unit->mana.max_value = 500;
        /* These are combat-contract fixtures. Real units have an authored
         * Attack 1 profile; initialize a stock-shaped melee attack so hit
         * hooks are reachable unless a case overrides it with a ranged mode. */
        unit->unit_state->attack1.type = ATK_NORMAL;
        unit->unit_state->attack1.weapon = WPN_NORMAL;
        unit->unit_state->attack1.damageBase = 10;
        unit->unit_state->attack1.numberOfDice = 1;
        unit->unit_state->attack1.sidesPerDie = 1;
        unit->unit_state->attack1.range = 128;
        unit->unit_state->attack1.cooldown = 1.0f;
    }
    f->ally->health.value = 100;
    /* Production dispel logic detects summoned units using their owner. */
    f->summon->owner = f->caster;
}

static void contract_done(creepContractFix_t *f) {
    G_SetSLKRows("AbilityData", f->old);
    free_slk_rows(f->rows);
}

/* Advance the real entity scheduler in fixed simulation frames so periodic
 * regeneration, thinker pulses and timed-status expiry observe elapsed time. */
static void contract_run_for(uint32_t millis) {
    while (millis) {
        uint32_t const frame_ms = (uint32_t)MAX(1.0f, FRAMETIME);
        uint32_t const step = MIN(millis, frame_ms);
        level.time += step;
        level.framenum++;
        G_RunEntities();
        millis -= step;
    }
}

static void contract_ability_slk(char *out, size_t len, cstring_t code,
                                  cstring_t parent, cstring_t targets,
                                  cstring_t buff, float amount, float duration,
                                  float area, float chance, float bonus) {
    snprintf(out, len,
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"levels\"\n"
        "C;Y1;X5;K\"BuffID1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
        "C;Y1;X11;K\"Area1\"\nC;Y1;X12;K\"Rng1\"\n"
        "C;Y1;X13;K\"Cost1\"\nC;Y1;X14;K\"Cool1\"\n"
        "C;Y2;X1;K\"%s\"\nC;Y2;X2;K\"%s\"\n"
        "C;Y2;X3;K\"%s\"\nC;Y2;X4;K\"1\"\n"
        "C;Y2;X5;K\"%s\"\nC;Y2;X6;K\"%g\"\n"
        "C;Y2;X7;K\"%g\"\nC;Y2;X8;K\"%g\"\n"
        "C;Y2;X9;K\"%g\"\nC;Y2;X10;K\"%g\"\n"
        "C;Y2;X11;K\"%g\"\nC;Y2;X12;K\"600\"\n"
        "C;Y2;X13;K\"0\"\nC;Y2;X14;K\"0\"\nE\n",
        code, parent, targets, buff ? buff : "", amount, chance, bonus,
        duration, duration, area);
}

/* Every entry drives production spell/combat code. For unverified rows the
 * values are synthetic contracts, not a claim about retail unit ownership. */
static cstring_t contract_alias(creepCase_t const *c, cstring_t fallback) {
    return c->ability_alias ? c->ability_alias : fallback;
}

static void contract_heal_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    cstring_t alias = contract_alias(c, "Anhe");
    contract_ability_slk(slk, sizeof(slk), alias, "Anhe", "air,ground,friend,organic", "", c->magnitude, 0, 0, 0, 0);
    contract_setup(&f, alias, slk);
    T_ASSERT(G_SetUnitAutocast(f.caster, FS_SLKKey(alias), true));
    T_ASSERT(G_TryUnitAutocast(f.caster));
    T_FEQ(f.ally->health.value, 100 + c->magnitude, 0.001f);
    T_ASSERT(G_SetUnitAutocast(f.caster, FS_SLKKey(alias), false));
    f.ally->health.value = 100;
    T_ASSERT(!G_TryUnitAutocast(f.caster));
    T_FEQ(f.ally->health.value, 100, 0.001f);
    T_ASSERT(!S_CastUnitTargetSpell(f.caster, FS_SLKKey(alias), f.enemy));
    T_FEQ(f.enemy->health.value, 500, 0.001f);
    contract_done(&f);
}

static void contract_rejuvenation_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    cstring_t alias = contract_alias(c, "Arej");
    contract_ability_slk(slk, sizeof(slk), alias, "Arej", "air,ground,friend", "Brej", c->magnitude, 12, 0, 0, 0);
    contract_setup(&f, alias, slk);
    T_ASSERT(!S_CastUnitTargetSpell(f.caster, FS_SLKKey(alias), f.enemy));
    T_ASSERT(S_CastUnitTargetSpell(f.caster, FS_SLKKey(alias), f.ally));
    T_ASSERT(S_UnitHasStatus(f.ally, FS_SLKKey("Brej")));
    T_FEQ(S_RejuvHealRate(f.ally), c->magnitude / 12.0f, 0.01f);
    contract_run_for(1000);
    T_ASSERT(f.ally->health.value > 100);
    contract_run_for(12000);
    T_ASSERT(!S_UnitHasStatus(f.ally, FS_SLKKey("Brej")));
    float hp = f.ally->health.value;
    contract_run_for(1000);
    T_FEQ(f.ally->health.value, hp, 0.001f);
    contract_done(&f);
}

static void contract_ensnare_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    cstring_t alias = contract_alias(c, "Aens");
    contract_ability_slk(slk, sizeof(slk), alias, "Aens", "ground,air,enemy,neutral", "Bena,Beng", 0, c->magnitude, 0, 0, 0);
    contract_setup(&f, alias, slk);
    T_ASSERT(!S_CastUnitTargetSpell(f.caster, FS_SLKKey(alias), f.ally));
    T_ASSERT(S_CastUnitTargetSpell(f.caster, FS_SLKKey(alias), f.enemy));
    T_ASSERT(S_UnitIsEnsnared(f.enemy));
    T_ASSERT(!S_UnitCanTranslate(f.enemy));
    level.time += (uint32_t)(1000 * c->magnitude - 1);
    unit_updatestatuses(f.enemy);
    T_ASSERT(S_UnitIsEnsnared(f.enemy));
    level.time++;
    unit_updatestatuses(f.enemy);
    T_ASSERT(!S_UnitIsEnsnared(f.enemy));
    T_ASSERT(S_UnitCanTranslate(f.enemy));
    contract_done(&f);
}

/* Chance-based effects use certain 0%/100% paths to prove the combat hooks,
 * while the authored 15% expectation remains in the source matrix. */
static void contract_evasion_variants(creepCase_t const *c) {
    (void)c;
    for (int proc = 0; proc < 2; proc++) {
        char slk[2048]; creepContractFix_t f;
        contract_ability_slk(slk, sizeof(slk), "ACes", "AEev", "", "", proc ? 1 : 0, 0, 0, 0, 0);
        contract_setup(&f, "ACes", slk);
        f.enemy->data.UnitAbilities = &f.abilities;
        f.enemy->health.value = 500;
        S_ResolveAttackHit(f.caster, f.enemy, 10);
        if (proc) T_FEQ(f.enemy->health.value, 500, 0.001f);
        else T_ASSERT(f.enemy->health.value < 500);
        contract_done(&f);
    }
}

static void contract_bash_variants(creepCase_t const *c) {
    float normal_damage = 0;
    for (int proc = 0; proc < 2; proc++) {
        char slk[2048]; creepContractFix_t f;
        contract_ability_slk(slk, sizeof(slk), "ACbh", "AHbh", "", "", proc ? 100 : 0, 2, 0, 0, c->magnitude);
        contract_setup(&f, "ACbh", slk);
        S_ResolveAttackHit(f.caster, f.enemy, 10);
        if (!proc) {
            normal_damage = 500 - f.enemy->health.value;
            T_EQ(G_UnitStatusLevel(f.enemy, FS_SLKKey("Bstu")), 0);
        } else {
            T_FEQ(500 - f.enemy->health.value - normal_damage, c->magnitude, 0.01f);
            T_EQ(G_UnitStatusLevel(f.enemy, FS_SLKKey("Bstu")), 1);
            level.time += 2000; unit_updatestatuses(f.enemy);
            T_EQ(G_UnitStatusLevel(f.enemy, FS_SLKKey("Bstu")), 0);
        }
        contract_done(&f);
    }
}

static void contract_abolish_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    cstring_t alias = contract_alias(c, "Aadm");
    /* Verified ACdm/ACd2: summon damage is DataB=300, DataC=1,
     * mana cost is 75, NOT summon damage. Fixture DataA=10 is an
     * artificial target cap; unverified rows exercise DataB=137. */
    contract_ability_slk(slk, sizeof(slk), alias, "Aadm", "air,ground,friend,enemy", "", 10, 0, 250, c->magnitude, 1);
    contract_setup(&f, alias, slk);
    T_ASSERT(unit_addtimedstatus(f.enemy, "Binf", 1, 30));
    T_ASSERT(unit_addtimedstatus(f.ally, "Bslo", 1, 30));
    T_ASSERT(unit_addtimedstatus(f.ally, "Binf", 1, 30));
    heroabilitystatus_t *s = unit_findstatus(f.enemy, FS_SLKKey("Binf"));
    T_NOT_NULL(s); if (s) s->buff_flags |= WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_POSITIVE;
    s = unit_findstatus(f.ally, FS_SLKKey("Bslo"));
    T_NOT_NULL(s); if (s) s->buff_flags |= WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_NEGATIVE;
    s = unit_findstatus(f.ally, FS_SLKKey("Binf"));
    T_NOT_NULL(s); if (s) s->buff_flags |= WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_POSITIVE;
    vec2_t point = f.caster->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(f.caster, FS_SLKKey(alias), &point));
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Binf")));
    T_ASSERT(!S_UnitHasStatus(f.ally, FS_SLKKey("Bslo")));
    T_ASSERT(S_UnitHasStatus(f.ally, FS_SLKKey("Binf")));
    T_FEQ(f.summon->health.value, 500 - c->magnitude, 0.001f);
    T_FEQ(f.enemy->health.value, 500, 0.001f);
    contract_done(&f);
}
static void contract_spell_immunity_variants(creepCase_t const *c) {
    (void)c;
    char slk[2048]; creepContractFix_t f;
    cstring_t alias = contract_alias(c, "Amim");
    contract_ability_slk(slk, sizeof(slk), alias, alias, "", "", 0, 0, 0, 0, 0);
    contract_setup(&f, alias, slk);
    f.enemy->data.UnitAbilities = &f.abilities;
    T_ASSERT(S_UnitSpellImmune(f.enemy));
    T_ASSERT(!S_SpellDamage(f.enemy, f.caster, 30));
    T_FEQ(f.enemy->health.value, 500, 0.001f);
    S_ResolveAttackHit(f.caster, f.enemy, 10);
    T_ASSERT(f.enemy->health.value < 500);  /* Not physical immunity. */
    float hp = f.enemy->health.value;
    T_ASSERT(G_ActorRemoveSkill(f.enemy, FS_SLKKey(alias)));
    T_ASSERT(!S_UnitSpellImmune(f.enemy));
    T_ASSERT(S_SpellDamage(f.enemy, f.caster, 30));
    T_FEQ(f.enemy->health.value, hp - 30, 0.001f);
    contract_done(&f);
}

static void contract_slow_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    /* DataA is movement reduction, DataB attack-rate reduction. */
    contract_ability_slk(slk, sizeof(slk), "Aslo", "Aslo", "ground,enemy", "Bslo", c->magnitude, c->duration, 0, 0.25f, 0);
    contract_setup(&f, "Aslo", slk);
    T_ASSERT(!S_CastUnitTargetSpell(f.caster, FS_SLKKey("Aslo"), f.ally));
    T_ASSERT(S_CastUnitTargetSpell(f.caster, FS_SLKKey("Aslo"), f.enemy));
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Bslo")));
    T_FEQ(S_HumanMoveFactor(f.enemy), 1.0f - c->magnitude, 0.001f);
    T_FEQ(unit_status_modifier_total(f.enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), -0.25f, 0.001f);
    level.time += (uint32_t)(1000 * c->duration);
    unit_updatestatuses(f.enemy);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Bslo")));
    T_FEQ(S_HumanMoveFactor(f.enemy), 1.0f, 0.001f);
    contract_done(&f);
}

static void contract_curse_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    contract_ability_slk(slk, sizeof(slk), "Acrs", "Acrs", "air,ground,enemy", "Bcrs", c->magnitude, 12, 0, 0, 0);
    contract_setup(&f, "Acrs", slk);
    T_ASSERT(!S_CastUnitTargetSpell(f.caster, FS_SLKKey("Acrs"), f.ally));
    T_ASSERT(S_CastUnitTargetSpell(f.caster, FS_SLKKey("Acrs"), f.enemy));
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Bcrs")));
    T_FEQ(S_CurseMissChance(f.enemy), c->magnitude, 0.001f);
    level.time += 12000; unit_updatestatuses(f.enemy);
    T_FEQ(S_CurseMissChance(f.enemy), 0.0f, 0.001f);
    contract_done(&f);
}

static void contract_faerie_fire_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    contract_ability_slk(slk, sizeof(slk), "Afae", "Afae", "air,ground,enemy", "Bfae", c->magnitude, 12, 0, 0, 0);
    contract_setup(&f, "Afae", slk);
    T_ASSERT(!S_CastUnitTargetSpell(f.caster, FS_SLKKey("Afae"), f.ally));
    float armor = G_UnitArmorValue(f.enemy);
    T_ASSERT(S_CastUnitTargetSpell(f.caster, FS_SLKKey("Afae"), f.enemy));
    T_ASSERT(S_UnitHasStatus(f.enemy, FS_SLKKey("Bfae")));
    T_FEQ(G_UnitArmorValue(f.enemy), armor - c->magnitude, 0.001f);
    level.time += 12000; unit_updatestatuses(f.enemy);
    T_FEQ(G_UnitArmorValue(f.enemy), armor, 0.001f);
    contract_done(&f);
}

static void contract_roar_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    contract_ability_slk(slk, sizeof(slk), "Aroa", "Aroa", "air,ground,friend", "Broa", c->magnitude, 12, 300, 0, 0);
    contract_setup(&f, "Aroa", slk);
    T_ASSERT(S_CastNoTargetSpell(f.caster, FS_SLKKey("Aroa")));
    T_ASSERT(S_UnitHasStatus(f.ally, FS_SLKKey("Broa")));
    T_FEQ(S_RoarDamageBonus(f.ally), c->magnitude, 0.001f);
    T_ASSERT(!S_UnitHasStatus(f.enemy, FS_SLKKey("Broa")));
    level.time += 12000; unit_updatestatuses(f.ally);
    T_FEQ(S_RoarDamageBonus(f.ally), 0.0f, 0.001f);
    contract_done(&f);
}

static void contract_bloodlust_variants(creepCase_t const *c) {
    char slk[2048]; creepContractFix_t f;
    contract_ability_slk(slk, sizeof(slk), "Ablo", "Ablo", "air,ground,friend", "Bblo", c->magnitude, c->duration, 0, 0.25f, 0);
    contract_setup(&f, "Ablo", slk);
    T_ASSERT(!S_CastUnitTargetSpell(f.caster, FS_SLKKey("Ablo"), f.enemy));
    T_ASSERT(S_CastUnitTargetSpell(f.caster, FS_SLKKey("Ablo"), f.ally));
    T_ASSERT(S_UnitHasStatus(f.ally, FS_SLKKey("Bblo")));
    T_FEQ(S_BloodlustAttackBonus(f.ally), c->magnitude, 0.001f);
    T_FEQ(S_BloodlustMoveBonus(f.ally), 0.25f, 0.001f);
    level.time += (uint32_t)(1000 * c->duration);
    unit_updatestatuses(f.ally);
    T_FEQ(S_BloodlustAttackBonus(f.ally), 0.0f, 0.001f);
    contract_done(&f);
}

/* Self-register one test per named creep–ability pair: failure reports identify
 * the source spreadsheet row, not only a shared test function line. */
#define CREEP_CASE_TEST(ROW, INDEX, RUNNER) \
    TEST(wc3_creep_contract, row_##ROW) { contract_##RUNNER(&cases[INDEX]); }
CREEP_CASE_TEST(4, 0, ensnare_variants)
CREEP_CASE_TEST(6, 1, heal_variants)
CREEP_CASE_TEST(8, 2, heal_variants)
CREEP_CASE_TEST(12, 3, rejuvenation_variants)
CREEP_CASE_TEST(13, 4, rejuvenation_variants)
CREEP_CASE_TEST(15, 5, faerie_fire_variants)
CREEP_CASE_TEST(21, 6, spell_immunity_variants)
CREEP_CASE_TEST(28, 7, ensnare_variants)
CREEP_CASE_TEST(35, 8, bloodlust_variants)
CREEP_CASE_TEST(41, 9, curse_variants)
CREEP_CASE_TEST(43, 10, ensnare_variants)
CREEP_CASE_TEST(46, 11, ensnare_variants)
CREEP_CASE_TEST(50, 12, roar_variants)
CREEP_CASE_TEST(56, 13, spell_immunity_variants)
CREEP_CASE_TEST(61, 14, bloodlust_variants)
CREEP_CASE_TEST(62, 15, abolish_variants)
CREEP_CASE_TEST(67, 16, faerie_fire_variants)
CREEP_CASE_TEST(68, 17, curse_variants)
CREEP_CASE_TEST(70, 18, rejuvenation_variants)
CREEP_CASE_TEST(75, 19, bash_variants)
CREEP_CASE_TEST(77, 20, evasion_variants)
CREEP_CASE_TEST(94, 21, evasion_variants)
CREEP_CASE_TEST(106, 22, curse_variants)
CREEP_CASE_TEST(110, 23, ensnare_variants)
CREEP_CASE_TEST(113, 24, heal_variants)
CREEP_CASE_TEST(114, 25, abolish_variants)
CREEP_CASE_TEST(115, 26, heal_variants)
CREEP_CASE_TEST(116, 27, abolish_variants)
CREEP_CASE_TEST(119, 28, bash_variants)
CREEP_CASE_TEST(121, 29, bash_variants)
CREEP_CASE_TEST(122, 30, slow_variants)
CREEP_CASE_TEST(123, 31, abolish_variants)
CREEP_CASE_TEST(124, 32, slow_variants)
CREEP_CASE_TEST(125, 33, spell_immunity_variants)
CREEP_CASE_TEST(127, 34, spell_immunity_variants)
CREEP_CASE_TEST(130, 35, spell_immunity_variants)
CREEP_CASE_TEST(131, 36, spell_immunity_variants)
CREEP_CASE_TEST(132, 37, spell_immunity_variants)
CREEP_CASE_TEST(133, 38, spell_immunity_variants)
CREEP_CASE_TEST(141, 39, spell_immunity_variants)
CREEP_CASE_TEST(143, 40, spell_immunity_variants)
CREEP_CASE_TEST(159, 41, spell_immunity_variants)
CREEP_CASE_TEST(164, 42, slow_variants)
CREEP_CASE_TEST(165, 43, slow_variants)
CREEP_CASE_TEST(166, 44, slow_variants)
CREEP_CASE_TEST(177, 45, spell_immunity_variants)
CREEP_CASE_TEST(178, 46, evasion_variants)
CREEP_CASE_TEST(179, 47, rejuvenation_variants)
CREEP_CASE_TEST(180, 48, evasion_variants)
CREEP_CASE_TEST(181, 49, evasion_variants)
CREEP_CASE_TEST(183, 50, evasion_variants)
CREEP_CASE_TEST(185, 51, evasion_variants)
CREEP_CASE_TEST(187, 52, curse_variants)
CREEP_CASE_TEST(194, 53, ensnare_variants)
CREEP_CASE_TEST(197, 54, heal_variants)
CREEP_CASE_TEST(199, 55, heal_variants)
CREEP_CASE_TEST(200, 56, abolish_variants)
CREEP_CASE_TEST(207, 57, abolish_variants)
CREEP_CASE_TEST(219, 58, abolish_variants)
CREEP_CASE_TEST(223, 59, slow_variants)
CREEP_CASE_TEST(226, 60, spell_immunity_variants)
CREEP_CASE_TEST(228, 61, spell_immunity_variants)
CREEP_CASE_TEST(231, 62, spell_immunity_variants)
CREEP_CASE_TEST(234, 63, abolish_variants)
CREEP_CASE_TEST(235, 64, heal_variants)
CREEP_CASE_TEST(237, 65, ensnare_variants)
CREEP_CASE_TEST(241, 66, ensnare_variants)
CREEP_CASE_TEST(250, 67, bloodlust_variants)
CREEP_CASE_TEST(257, 68, spell_immunity_variants)
CREEP_CASE_TEST(261, 69, spell_immunity_variants)
CREEP_CASE_TEST(265, 70, curse_variants)
CREEP_CASE_TEST(274, 71, bash_variants)
CREEP_CASE_TEST(276, 72, bash_variants)
CREEP_CASE_TEST(278, 73, roar_variants)
CREEP_CASE_TEST(279, 74, rejuvenation_variants)
CREEP_CASE_TEST(281, 75, ensnare_variants)
CREEP_CASE_TEST(284, 76, heal_variants)
CREEP_CASE_TEST(285, 77, bloodlust_variants)
CREEP_CASE_TEST(287, 78, slow_variants)
CREEP_CASE_TEST(290, 79, abolish_variants)
CREEP_CASE_TEST(296, 80, bash_variants)
CREEP_CASE_TEST(307, 81, spell_immunity_variants)
CREEP_CASE_TEST(325, 82, faerie_fire_variants)
CREEP_CASE_TEST(333, 83, heal_variants)
CREEP_CASE_TEST(342, 84, heal_variants)
CREEP_CASE_TEST(344, 85, slow_variants)
CREEP_CASE_TEST(345, 86, abolish_variants)
CREEP_CASE_TEST(347, 87, curse_variants)
CREEP_CASE_TEST(350, 88, bash_variants)
CREEP_CASE_TEST(353, 89, ensnare_variants)
CREEP_CASE_TEST(377, 90, bloodlust_variants)
CREEP_CASE_TEST(383, 91, bash_variants)
CREEP_CASE_TEST(384, 92, roar_variants)
CREEP_CASE_TEST(385, 93, rejuvenation_variants)
#undef CREEP_CASE_TEST
/* Synthetic alias with 0% then 100% chance; the parent's stock fields must
 * never override the caster's own Critical Strike DataA/DataB. */
TEST(wc3_creep_contract, critical_strike_uses_owned_alias_values) {
    for (int certain = 0; certain < 2; certain++) {
        char slk[2048]; creepContractFix_t f;
        contract_ability_slk(slk, sizeof(slk), "ACct", "AOcr", "", "",
                             certain ? 100.0f : 0.0f, 0, 0, 3.0f, 0);
        contract_setup(&f, "ACct", slk);
        T_EQ(S_ResolveAbilityAlias(f.caster, MAKEFOURCC('A','O','c','r')).alias,
             MAKEFOURCC('A','C','c','t'));
        T_EQ(S_CriticalStrikeDamage(f.caster, 20), certain ? 60 : 20);
        contract_done(&f);
    }
}
#include "t_neutral_creep_contract_extended_impl.inc"
#include "t_neutral_creep_contract_extended_cases.inc"
#endif
