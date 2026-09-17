#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANDP MAKEFOURCC('A', 'N', 'd', 'p') // rawcode; Dark Portal
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // unit; fixture DataA (non-stock nbal)
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F') // buff; must stay off Dark Portal troops
#define BZ_DUR 0.5f // fixture Dur; exit interval seconds (not stock 1.0)
#define BZ_MIN 3 // fixture DataB; min count (not stock L1 3 alone — paired with max)
#define BZ_MAX 3 // fixture DataC; max==min so count is deterministic (not stock 5)

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);
void G_RunEntities(void);

/* Non-stock ANdp: DataA=hfoo, DataB=DataC=3, Dur=0.5, Rng=800. */
static char const andp_slk[] =
    "ID;PWXL;N;EBB;Y2;X11\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
    "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Area1\"\n"
    "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
    "C;Y1;X10;K\"Rng1\"\nC;Y1;X11;K\"Cost1\"\n"
    "C;Y2;X1;K\"ANdp\"\nC;Y2;X2;K\"ANdp\"\nC;Y2;X3;K\"\"\n"
    "C;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\n"
    "C;Y2;X7;K\"hfoo\"\nC;Y2;X8;K\"3\"\nC;Y2;X9;K\"3\"\n"
    "C;Y2;X10;K\"800\"\nC;Y2;X11;K\"0\"\nE\n";

typedef struct { slkTestData_t *rows, *old; LPEDICT caster; VECTOR2 point; } DPFIX;

static DPFIX dp_setup(void) {
    DPFIX fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    fix.rows = parse_slk_string(andp_slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.caster = alloc_test_unit(MAKEFOURCC('U', 'w', 'a', 'r'), 0, 0);
    fix.caster->s.player = 0; fix.caster->svflags |= SVF_MONSTER; fix.caster->targtype = TARG_GROUND;
    fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ANDP, .level = 1);
    fix.caster->mana.value = fix.caster->mana.max_value = 100;
    fix.point = MAKE(VECTOR2, .x = 128, .y = 96);
    return fix;
}

static void dp_done(DPFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

static LPEDICT dp_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->think == dark_portal_think)
        return ent;
    return NULL;
}

static DWORD dp_troops(LPEDICT caster) {
    DWORD count = 0;
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == BZ_HFOO && ent->s.player == caster->s.player) {
        T_ASSERT(!S_UnitHasStatus(ent, BZ_BTLF));
        T_NULL(ent->owner);
        count++;
    }
    return count;
}

TEST(wc3_spell, dark_portal_procedure_and_flags) {
    abilityitem_t andp = S_AbilityItem(BZ_ANDP);
    T_EQ(andp.ability->proc, CAbilityDarkPortal);
    T_ASSERT(andp.ability->flags & AB_SPELL);
    T_ASSERT(!(andp.ability->flags & AB_CHANNEL));
    T_EQ((int)andp.ability->target_type, (int)SPELL_TARGET_POINT);
}

TEST(wc3_spell, dark_portal_reads_authored_data) {
    DPFIX fix = dp_setup();
    T_EQ(S_SpellDataId(BZ_ANDP, 1, 1), BZ_HFOO);
    T_FEQ(S_SpellData(BZ_ANDP, 1, 2), (FLOAT)BZ_MIN, 0.01f);
    T_FEQ(S_SpellData(BZ_ANDP, 1, 3), (FLOAT)BZ_MAX, 0.01f);
    T_FEQ(S_SpellDuration(BZ_ANDP, 1, false), BZ_DUR, 0.01f);
    dp_done(fix);
}

/* Point cast owns exits on a thinker: not channeled, first exit immediate, then Dur spacing. */
TEST(wc3_spell, dark_portal_schedules_authored_exits) {
    DPFIX fix = dp_setup();
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANDP, &fix.point));
    T_EQ(fix.caster->channel.code, 0);
    T_EQ(dp_troops(fix.caster), 1);
    T_NOT_NULL(dp_thinker(fix.caster));

    level.time += (DWORD)(BZ_DUR * 1000.0f) - 1; G_RunEntities();
    T_EQ(dp_troops(fix.caster), 1);
    level.time += 1; G_RunEntities();
    T_EQ(dp_troops(fix.caster), 2);

    level.time += (DWORD)(BZ_DUR * 1000.0f); G_RunEntities();
    T_EQ(dp_troops(fix.caster), BZ_MAX);
    T_NULL(dp_thinker(fix.caster));
    dp_done(fix);
}

/* Moving the caster after cast must not cancel remaining portal exits. */
TEST(wc3_spell, dark_portal_continues_after_caster_moves) {
    DPFIX fix = dp_setup();
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANDP, &fix.point));
    T_EQ(dp_troops(fix.caster), 1);
    fix.caster->s.origin2.x += 400; fix.caster->s.origin.x += 400;
    T_EQ(fix.caster->channel.code, 0);
    level.time += (DWORD)(BZ_DUR * 1000.0f); G_RunEntities();
    T_EQ(dp_troops(fix.caster), 2);
    level.time += (DWORD)(BZ_DUR * 1000.0f); G_RunEntities();
    T_EQ(dp_troops(fix.caster), BZ_MAX);
    T_NULL(dp_thinker(fix.caster));
    dp_done(fix);
}

#endif
