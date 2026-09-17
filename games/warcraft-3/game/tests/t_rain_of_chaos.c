#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANRC MAKEFOURCC('A', 'N', 'r', 'c') // rawcode; Rain of Chaos
#define BZ_ANR3 MAKEFOURCC('A', 'N', 'r', '3') // rawcode; Rain of Chaos button alias
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // unit; fixture Inferno UnitID (non-stock ninf)
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F') // buff; timed life on summons
#define BZ_AREA 200.0f // fixture Area; scatter bound for landings
#define BZ_DUR 0.5f // fixture Dur; landing interval seconds (not stock 1.0)
#define BZ_COUNT 3 // fixture DataB; landings (not stock 2)

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);
void G_RunEntities(void);

/* Non-stock ANrc/ANin/ANr3 rows: DataB=3, Dur=0.5, Area=200, UnitID=hfoo, summon Dur=6. */
static char const roc_slk[] =
    "ID;PWXL;N;EBB;Y4;X12\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Area1\"\n"
    "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"UnitID1\"\n"
    "C;Y1;X10;K\"Rng1\"\nC;Y1;X11;K\"Cost1\"\nC;Y1;X12;K\"Cool1\"\n"
    "C;Y2;X1;K\"ANrc\"\nC;Y2;X2;K\"ANrc\"\nC;Y2;X3;K\"\"\n"
    "C;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"200\"\n"
    "C;Y2;X7;K\"ANin\"\nC;Y2;X8;K\"3\"\nC;Y2;X10;K\"1000\"\nC;Y2;X11;K\"0\"\nC;Y2;X12;K\"0\"\n"
    "C;Y3;X1;K\"ANr3\"\nC;Y3;X2;K\"ANrc\"\nC;Y3;X3;K\"\"\n"
    "C;Y3;X4;K\"0.5\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"200\"\n"
    "C;Y3;X7;K\"ANin\"\nC;Y3;X8;K\"3\"\nC;Y3;X10;K\"1000\"\nC;Y3;X11;K\"0\"\nC;Y3;X12;K\"0\"\n"
    "C;Y4;X1;K\"ANin\"\nC;Y4;X2;K\"ANin\"\n"
    "C;Y4;X3;K\"ground,structure,debris,enemy,neutral\"\n"
    "C;Y4;X4;K\"6\"\nC;Y4;X5;K\"6\"\nC;Y4;X6;K\"250\"\n"
    "C;Y4;X7;K\"50\"\nC;Y4;X8;K\"360\"\nC;Y4;X9;K\"hfoo\"\n"
    "C;Y4;X10;K\"900\"\nC;Y4;X11;K\"175\"\nC;Y4;X12;K\"0\"\nE\n";

typedef struct { slkTestData_t *rows, *old; LPEDICT caster; VECTOR2 point; } ROCFIX;

static ROCFIX roc_setup(DWORD code) {
    ROCFIX fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    fix.rows = parse_slk_string(roc_slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(MAKEFOURCC('U', 'w', 'a', 'r'), 0, 0);
    fix.caster->s.player = 0; fix.caster->svflags |= SVF_MONSTER; fix.caster->targtype = TARG_GROUND;
    fix.caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
    fix.caster->mana.value = fix.caster->mana.max_value = 100;
    fix.point = MAKE(VECTOR2, .x = 128, .y = 96);
    return fix;
}

static void roc_done(ROCFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

static LPEDICT roc_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->think == rain_of_chaos_think)
        return ent;
    return NULL;
}

static DWORD roc_summons(LPEDICT caster, VECTOR2 point, FLOAT area) {
    DWORD count = 0;
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->class_id == BZ_HFOO) {
        T_ASSERT(Vector2_distance(&ent->s.origin2, &point) <= area + 0.5f);
        T_EQ(ent->s.player, caster->s.player);
        T_ASSERT(S_UnitHasStatus(ent, BZ_BTLF));
        count++;
    }
    return count;
}

TEST(wc3_spell, rain_of_chaos_procedure_and_flags) {
    abilityitem_t anrc = S_AbilityItem(BZ_ANRC), anr3 = S_AbilityItem(BZ_ANR3);
    T_EQ(anrc.ability->proc, CAbilityRainOfChaos);
    T_EQ(anr3.ability->proc, CAbilityRainOfChaos);
    T_ASSERT(anrc.ability->flags & AB_SPELL);
    T_ASSERT(!(anrc.ability->flags & AB_CHANNEL));
    T_EQ((int)anrc.ability->target_type, (int)SPELL_TARGET_POINT);
    T_ASSERT(anr3.ability->flags & AB_SPELL);
    T_ASSERT(!(anr3.ability->flags & AB_CHANNEL));
    T_EQ((int)anr3.ability->target_type, (int)SPELL_TARGET_POINT);
}

/* Point cast owns landings on a thinker: not channeled, first landing immediate, then Dur spacing. */
TEST(wc3_spell, rain_of_chaos_schedules_authored_landings) {
    ROCFIX fix = roc_setup(BZ_ANRC);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANRC, &fix.point));
    T_EQ(fix.caster->channel.code, 0);
    T_EQ(roc_summons(fix.caster, fix.point, BZ_AREA), 1);
    T_NOT_NULL(roc_thinker(fix.caster));

    level.time += (DWORD)(BZ_DUR * 1000.0f) - 1; G_RunEntities();
    T_EQ(roc_summons(fix.caster, fix.point, BZ_AREA), 1);
    level.time += 1; G_RunEntities();
    T_EQ(roc_summons(fix.caster, fix.point, BZ_AREA), 2);

    level.time += (DWORD)(BZ_DUR * 1000.0f); G_RunEntities();
    T_EQ(roc_summons(fix.caster, fix.point, BZ_AREA), BZ_COUNT);
    T_NULL(roc_thinker(fix.caster));
    roc_done(fix);
}

/* Moving the caster after cast must not cancel remaining landings (unlike Rain of Fire). */
TEST(wc3_spell, rain_of_chaos_continues_after_caster_moves) {
    ROCFIX fix = roc_setup(BZ_ANRC);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANRC, &fix.point));
    T_EQ(roc_summons(fix.caster, fix.point, BZ_AREA), 1);
    fix.caster->s.origin2.x += 400; fix.caster->s.origin.x += 400;
    T_EQ(fix.caster->channel.code, 0);
    level.time += (DWORD)(BZ_DUR * 1000.0f); G_RunEntities();
    T_EQ(roc_summons(fix.caster, fix.point, BZ_AREA), 2);
    level.time += (DWORD)(BZ_DUR * 1000.0f); G_RunEntities();
    T_EQ(roc_summons(fix.caster, fix.point, BZ_AREA), BZ_COUNT);
    T_NULL(roc_thinker(fix.caster));
    roc_done(fix);
}

TEST(wc3_spell, rain_of_chaos_anr3_shares_procedure_cast) {
    ROCFIX fix = roc_setup(BZ_ANR3);
    T_EQ(S_AbilityItem(BZ_ANR3).ability->proc, CAbilityRainOfChaos);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANR3, &fix.point));
    T_EQ(fix.caster->channel.code, 0);
    T_EQ(roc_summons(fix.caster, fix.point, BZ_AREA), 1);
    T_NOT_NULL(roc_thinker(fix.caster));
    roc_done(fix);
}

#endif
