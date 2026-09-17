#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANHS MAKEFOURCC('A', 'N', 'h', 's') // rawcode; TFT Alchemist Healing Spray

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock DataA/B/D/F so tests cannot pass on retail 40/1/280/3. */
static LPCSTR healing_spray_slk =
    "ID;PWXL;N;EBB;Y2;X16\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
    "C;Y1;X10;K\"Area1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"DataB1\"\n"
    "C;Y1;X13;K\"DataC1\"\nC;Y1;X14;K\"DataD1\"\nC;Y1;X15;K\"DataE1\"\n"
    "C;Y1;X16;K\"DataF1\"\n"
    "C;Y2;X1;K\"ANhs\"\nC;Y2;X2;K\"ANhs\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"friend,self,ground,air,organic\"\n"
    "C;Y2;X5;K\"50\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"800\"\n"
    "C;Y2;X8;K\"0\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"50\"\n"
    "C;Y2;X11;K\"25\"\nC;Y2;X12;K\"1\"\nC;Y2;X13;K\"6\"\n"
    "C;Y2;X14;K\"200\"\nC;Y2;X15;K\"1\"\nC;Y2;X16;K\"2\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, ally, ally2, enemy, mech, far;
    UnitBalance_t unit_bal, mech_bal;
} HSFIX;

static LPEDICT hs_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->owner == caster && ent->think) return ent;
    return NULL;
}

/* Fill caller in place: edict UnitBalance pointers must not dangle. */
static void hs_setup(HSFIX *fix) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(healing_spray_slk);
    fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->unit_bal = MAKE(UnitBalance_t, .maxHealth = 500);
    fix->mech_bal = MAKE(UnitBalance_t, .maxHealth = 500);
    /* Caster stays outside Area=50 of the spray point at (64,0). */
    fix->caster = alloc_test_unit(MAKEFOURCC('N', 'a', 'l', 'c'), 0, 0);
    fix->ally = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    fix->ally2 = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 80, 0);
    fix->enemy = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 96, 0);
    fix->mech = alloc_test_unit(MAKEFOURCC('h', 'm', 't', 't'), 70, 20);
    fix->far = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 400, 0);
    fix->caster->s.player = fix->ally->s.player = fix->ally2->s.player = fix->mech->s.player = fix->far->s.player = 0;
    fix->enemy->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->ally->svflags |= SVF_MONSTER;
    fix->ally2->svflags |= SVF_MONSTER; fix->enemy->svflags |= SVF_MONSTER;
    fix->mech->svflags |= SVF_MONSTER; fix->far->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->ally->targtype = fix->ally2->targtype = TARG_GROUND;
    fix->enemy->targtype = fix->far->targtype = TARG_GROUND;
    fix->mech->targtype = TARG_MECHANICAL;
    fix->ally->data.UnitBalance = &fix->unit_bal;
    fix->ally2->data.UnitBalance = &fix->unit_bal;
    fix->enemy->data.UnitBalance = &fix->unit_bal;
    fix->far->data.UnitBalance = &fix->unit_bal;
    fix->mech->data.UnitBalance = &fix->mech_bal;
    fix->ally->health.value = 100; fix->ally->health.max_value = 500;
    fix->ally2->health.value = 100; fix->ally2->health.max_value = 500;
    fix->enemy->health.value = 100; fix->enemy->health.max_value = 500;
    fix->mech->health.value = 100; fix->mech->health.max_value = 500;
    fix->far->health.value = 100; fix->far->health.max_value = 500;
    fix->caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ANHS, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 200;
    fix->caster->health.value = fix->caster->health.max_value = 500;
}

static void hs_done(HSFIX *fix) { G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows); }

TEST(wc3_spell, healing_spray_procedure_is_channel_point_spell) {
    abilityitem_t item = S_AbilityItem(BZ_ANHS);
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityHealingSpray);
    T_ASSERT(item.ability->flags & AB_CHANNEL);
    T_EQ(item.ability->target_type, SPELL_TARGET_POINT);
}

/* First wave heals friendlies in Area by DataA; enemy/mech/far are untouched. */
TEST(wc3_spell, healing_spray_first_wave_heals_friendlies_in_area) {
    HSFIX fix; hs_setup(&fix);
    VECTOR2 point = fix.ally->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANHS, &point));
    T_EQ(fix.caster->channel.code, BZ_ANHS);
    T_FEQ(fix.ally->health.value, 125, 0.001f);
    T_FEQ(fix.ally2->health.value, 125, 0.001f);
    T_FEQ(fix.enemy->health.value, 100, 0.001f);
    T_FEQ(fix.mech->health.value, 100, 0.001f);
    T_FEQ(fix.far->health.value, 100, 0.001f);
    hs_done(&fix);
}

/* DataD caps the wave: two allies at DataA=25 would be 50, so each gets 20. */
TEST(wc3_spell, healing_spray_scales_heal_to_max_gained_hp) {
    HSFIX fix; hs_setup(&fix);
    VECTOR2 point = fix.ally->s.origin2;
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X16\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
        "C;Y1;X10;K\"Area1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"DataB1\"\n"
        "C;Y1;X13;K\"DataC1\"\nC;Y1;X14;K\"DataD1\"\nC;Y1;X15;K\"DataE1\"\n"
        "C;Y1;X16;K\"DataF1\"\n"
        "C;Y2;X1;K\"ANhs\"\nC;Y2;X2;K\"ANhs\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"friend,self,ground,air,organic\"\n"
        "C;Y2;X5;K\"50\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"800\"\n"
        "C;Y2;X8;K\"0\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"50\"\n"
        "C;Y2;X11;K\"25\"\nC;Y2;X12;K\"1\"\nC;Y2;X13;K\"6\"\n"
        "C;Y2;X14;K\"40\"\nC;Y2;X15;K\"1\"\nC;Y2;X16;K\"2\"\nE\n";
    G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows);
    fix.rows = parse_slk_string(slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANHS, &point));
    T_FEQ(fix.ally->health.value, 120, 0.001f); /* 100 + 40/2 */
    T_FEQ(fix.ally2->health.value, 120, 0.001f);
    hs_done(&fix);
}

/* Second pulse waits for authored DataB, then fires through the entity scheduler. */
TEST(wc3_spell, healing_spray_second_wave_after_authored_interval) {
    HSFIX fix; hs_setup(&fix);
    VECTOR2 point = fix.ally->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANHS, &point));
    LPEDICT thinker = hs_thinker(fix.caster);
    T_NOT_NULL(thinker);
    T_FEQ(fix.ally->health.value, 125, 0.001f);
    level.time += FRAMETIME; G_RunEntities();
    T_FEQ(fix.ally->health.value, 125, 0.001f);
    level.time = thinker->freetime; G_RunEntities();
    T_FEQ(fix.ally->health.value, 150, 0.001f);
    T_EQ(fix.caster->channel.code, 0);
    T_ASSERT(!thinker->inuse);
    hs_done(&fix);
}

TEST(wc3_spell, healing_spray_caster_move_cancels_remaining_waves) {
    HSFIX fix; hs_setup(&fix);
    VECTOR2 point = fix.ally->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANHS, &point));
    LPEDICT thinker = hs_thinker(fix.caster);
    T_NOT_NULL(thinker);
    fix.caster->s.origin2.x += 10; fix.caster->s.origin.x += 10;
    level.time = thinker->freetime; G_RunEntities();
    T_EQ(fix.caster->channel.code, 0);
    T_FEQ(fix.ally->health.value, 125, 0.001f);
    T_ASSERT(!thinker->inuse);
    hs_done(&fix);
}

#endif
