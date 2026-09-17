#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANIN MAKEFOURCC('A', 'N', 'i', 'n') // rawcode; creep Inferno
#define BZ_ANRC MAKEFOURCC('A', 'N', 'r', 'c') // rawcode; Rain of Chaos
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // unit; fixture Inferno UnitID (non-stock ninf)
#define BZ_BSTU MAKEFOURCC('B', 's', 't', 'u') // buff; shared stun
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F') // buff; timed life on summons
#define BZ_DMG 40.0f // fixture DataA; not stock 50
#define BZ_LIFE 12.0f // fixture DataB; summon life (not stock 360)
#define BZ_DELAY 0.5f // fixture DataC; impact delay (not stock 1.0)
#define BZ_STUN 3.0f // fixture Dur; unit stun seconds (not stock 4)
#define BZ_HERO 1.5f // fixture HeroDur; hero stun seconds (not stock 2)
#define BZ_AREA 200.0f // fixture Area; blast radius (not stock 250)

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);
void G_RunEntities(void);

/* Non-stock ANin (+ ANrc link): DataA=40, DataB=12, DataC=0.5, Dur=3, HeroDur=1.5, Area=200. */
static char const inferno_slk[] =
    "ID;PWXL;N;EBB;Y3;X14\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Area1\"\n"
    "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
    "C;Y1;X10;K\"UnitID1\"\nC;Y1;X11;K\"Rng1\"\nC;Y1;X12;K\"Cost1\"\n"
    "C;Y1;X13;K\"Cool1\"\nC;Y1;X14;K\"levels\"\n"
    "C;Y2;X1;K\"ANin\"\nC;Y2;X2;K\"ANin\"\n"
    "C;Y2;X3;K\"ground,structure,debris,enemy,neutral\"\n"
    "C;Y2;X4;K\"3\"\nC;Y2;X5;K\"1.5\"\nC;Y2;X6;K\"200\"\n"
    "C;Y2;X7;K\"40\"\nC;Y2;X8;K\"12\"\nC;Y2;X9;K\"0.5\"\n"
    "C;Y2;X10;K\"hfoo\"\nC;Y2;X11;K\"900\"\nC;Y2;X12;K\"0\"\n"
    "C;Y2;X13;K\"0\"\nC;Y2;X14;K\"1\"\n"
    "C;Y3;X1;K\"ANrc\"\nC;Y3;X2;K\"ANrc\"\nC;Y3;X3;K\"\"\n"
    "C;Y3;X4;K\"0.5\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"200\"\n"
    "C;Y3;X7;K\"ANin\"\nC;Y3;X8;K\"1\"\nC;Y3;X11;K\"1000\"\n"
    "C;Y3;X12;K\"0\"\nC;Y3;X13;K\"0\"\nC;Y3;X14;K\"1\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, enemy, far, hero;
    UnitBalance_t unit_bal, hero_bal;
    VECTOR2 point;
} INFIX;

static DWORD stun_ms(LPCEDICT unit) {
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BSTU)
            return unit->abilstatus[i].duration_ms;
    return 0;
}

static LPEDICT inferno_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->think == inferno_think)
        return ent;
    return NULL;
}

static LPEDICT inferno_summon(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->class_id == BZ_HFOO)
        return ent;
    return NULL;
}

static INFIX inferno_setup(DWORD code) {
    INFIX fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix.rows = parse_slk_string(inferno_slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.unit_bal = MAKE(UnitBalance_t, .maxHealth = 500);
    fix.hero_bal = MAKE(UnitBalance_t, .maxHealth = 500, .strength = 20);
    fix.caster = alloc_test_unit(MAKEFOURCC('U', 'w', 'a', 'r'), 0, 0);
    fix.enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    fix.far = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 400, 0);
    fix.hero = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), 96, 0);
    fix.caster->s.player = 0;
    fix.enemy->s.player = fix.far->s.player = fix.hero->s.player = 1;
    fix.caster->svflags |= SVF_MONSTER; fix.enemy->svflags |= SVF_MONSTER;
    fix.far->svflags |= SVF_MONSTER; fix.hero->svflags |= SVF_MONSTER;
    fix.caster->targtype = fix.enemy->targtype = fix.far->targtype = fix.hero->targtype = TARG_GROUND;
    fix.enemy->data.UnitBalance = &fix.unit_bal;
    fix.far->data.UnitBalance = &fix.unit_bal;
    fix.hero->data.UnitBalance = &fix.hero_bal;
    fix.enemy->health.value = fix.enemy->health.max_value = 500;
    fix.far->health.value = fix.far->health.max_value = 500;
    fix.hero->health.value = fix.hero->health.max_value = 500;
    fix.caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
    fix.caster->mana.value = fix.caster->mana.max_value = 200;
    fix.point = fix.enemy->s.origin2;
    return fix;
}

static void inferno_done(INFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, inferno_procedure_and_flags) {
    abilityitem_t item = S_AbilityItem(BZ_ANIN);
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityInferno);
    T_ASSERT(item.ability->flags & AB_SPELL);
    T_ASSERT(!(item.ability->flags & AB_CHANNEL));
    T_EQ((int)item.ability->target_type, (int)SPELL_TARGET_POINT);
}

/* DataC delays blast: no damage/stun/summon until the authored delay elapses. */
TEST(wc3_spell, inferno_impact_after_authored_delay) {
    INFIX fix = inferno_setup(BZ_ANIN);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANIN, &fix.point));
    T_EQ(fix.caster->channel.code, 0);
    T_FEQ(fix.enemy->health.value, 500, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BSTU), 0);
    T_NULL(inferno_summon(fix.caster));
    T_NOT_NULL(inferno_thinker(fix.caster));

    level.time += (DWORD)(BZ_DELAY * 1000.0f) - 1; G_RunEntities();
    T_FEQ(fix.enemy->health.value, 500, 0.001f);
    T_NULL(inferno_summon(fix.caster));

    level.time += 1; G_RunEntities();
    T_FEQ(fix.enemy->health.value, 500.0f - BZ_DMG, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BSTU), 1);
    T_EQ(stun_ms(fix.enemy), (DWORD)(BZ_STUN * 1000.0f));
    T_NOT_NULL(inferno_summon(fix.caster));
    T_ASSERT(S_UnitHasStatus(inferno_summon(fix.caster), BZ_BTLF));
    T_EQ(stun_ms(inferno_summon(fix.caster)), 0);
    T_NULL(inferno_thinker(fix.caster));
    inferno_done(fix);
}

TEST(wc3_spell, inferno_out_of_area_untouched) {
    INFIX fix = inferno_setup(BZ_ANIN);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANIN, &fix.point));
    level.time += (DWORD)(BZ_DELAY * 1000.0f); G_RunEntities();
    T_FEQ(fix.far->health.value, 500, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.far, BZ_BSTU), 0);
    inferno_done(fix);
}

TEST(wc3_spell, inferno_summon_uses_datab_life) {
    INFIX fix = inferno_setup(BZ_ANIN);
    LPEDICT summon;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANIN, &fix.point));
    level.time += (DWORD)(BZ_DELAY * 1000.0f); G_RunEntities();
    summon = inferno_summon(fix.caster);
    T_NOT_NULL(summon);
    T_EQ(summon->class_id, BZ_HFOO);
    T_EQ(summon->s.player, fix.caster->s.player);
    T_ASSERT(S_UnitHasStatus(summon, BZ_BTLF));
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (summon->abilstatus[i].level && summon->abilstatus[i].code == BZ_BTLF)
            T_EQ(summon->abilstatus[i].duration_ms, (DWORD)(BZ_LIFE * 1000.0f));
    inferno_done(fix);
}

TEST(wc3_spell, inferno_stun_uses_herodur_for_heroes) {
    INFIX fix = inferno_setup(BZ_ANIN);
    VECTOR2 point = fix.hero->s.origin2;
    T_ASSERT(G_UnitIsHero(fix.hero));
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANIN, &point));
    level.time += (DWORD)(BZ_DELAY * 1000.0f); G_RunEntities();
    T_EQ(G_UnitStatusLevel(fix.hero, BZ_BSTU), 1);
    T_EQ(stun_ms(fix.hero), (DWORD)(BZ_HERO * 1000.0f));
    T_FEQ(fix.hero->health.value, 500.0f - BZ_DMG, 0.001f);
    inferno_done(fix);
}

/* Rain of Chaos DataA still resolves the Inferno row for the landing blast+summon. */
TEST(wc3_spell, inferno_rain_of_chaos_lands_via_inferno_row) {
    INFIX fix = inferno_setup(BZ_ANRC);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANRC, &fix.point));
    T_EQ(fix.caster->channel.code, 0);
    /* Fixture DataC=0.5 so the RoC landing schedules Inferno delay before summon. */
    T_NULL(inferno_summon(fix.caster));
    level.time += (DWORD)(BZ_DELAY * 1000.0f); G_RunEntities();
    T_NOT_NULL(inferno_summon(fix.caster));
    T_ASSERT(S_UnitHasStatus(inferno_summon(fix.caster), BZ_BTLF));
    inferno_done(fix);
}

#endif
