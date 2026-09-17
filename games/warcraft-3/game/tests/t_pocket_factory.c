#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

LPEDICT alloc_test_unit(DWORD code, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

static UnitAbilities_t factory_abilities = { .abilList = "ANsy" };
static char const factory_slk[] =
    "ID;PWXL;N;EBB;Y2;X30\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"targs\"\n"
    "C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"DataA1\"\n"
    "C;Y1;X9;K\"DataB1\"\nC;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"DataE1\"\n"
    "C;Y1;X13;K\"UnitID1\"\nC;Y1;X14;K\"Cost2\"\nC;Y1;X15;K\"Rng2\"\nC;Y1;X16;K\"Dur2\"\n"
    "C;Y1;X17;K\"DataA2\"\nC;Y1;X18;K\"DataB2\"\nC;Y1;X19;K\"DataC2\"\nC;Y1;X20;K\"DataD2\"\n"
    "C;Y1;X21;K\"DataE2\"\nC;Y1;X22;K\"UnitID2\"\nC;Y1;X23;K\"Cost3\"\nC;Y1;X24;K\"Rng3\"\n"
    "C;Y1;X25;K\"Dur3\"\nC;Y1;X26;K\"DataA3\"\nC;Y1;X27;K\"DataB3\"\nC;Y1;X28;K\"DataC3\"\n"
    "C;Y1;X29;K\"UnitID3\"\nC;Y1;X30;K\"DataD3\"\n"
    "C;Y2;X1;K\"ANsy\"\nC;Y2;X2;K\"ANsy\"\nC;Y2;X3;K3\nC;Y2;X4;K\"\"\n"
    "C;Y2;X5;K0\nC;Y2;X6;K500\nC;Y2;X7;K40\nC;Y2;X8;K5\nC;Y2;X9;K\"ncgb\"\n"
    "C;Y2;X10;K12\nC;Y2;X11;K200\nC;Y2;X12;K1100\nC;Y2;X13;K\"nfac\"\n"
    "C;Y2;X14;K0\nC;Y2;X15;K500\nC;Y2;X16;K40\nC;Y2;X17;K5\nC;Y2;X18;K\"ncg1\"\n"
    "C;Y2;X19;K12\nC;Y2;X20;K200\nC;Y2;X21;K1100\nC;Y2;X22;K\"nfa1\"\n"
    "C;Y2;X23;K0\nC;Y2;X24;K500\nC;Y2;X25;K40\nC;Y2;X26;K5\nC;Y2;X27;K\"ncg2\"\n"
    "C;Y2;X28;K12\nC;Y2;X29;K\"nfa2\"\nC;Y2;X30;K200\nE\n";

static LPEDICT factory_setup(DWORD level_number, slkTestData_t **rows, slkTestData_t **old) {
    LPEDICT caster;
    reset_entities(); setup_test_world(); level.time = 1000;
    caster = alloc_test_unit(MAKEFOURCC('N','t','i','n'), 0, 0);
    caster->s.player = 0; caster->mana.value = caster->mana.max_value = 1000;
    caster->health.value = caster->health.max_value = 1000; caster->data.UnitAbilities = &factory_abilities;
    caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','s','y'), .level = level_number);
    *rows = parse_slk_string(factory_slk); *old = G_SetSLKRows("AbilityData", *rows);
    level.started = true; level.scriptsStarted = true;
    return caster;
}

static void factory_cleanup(slkTestData_t *rows, slkTestData_t *old) {
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

static void factory_frame(DWORD elapsed) { level.time += elapsed; globals.RunFrame(); }

static LPEDICT find_class(DWORD code) {
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == code) return ent;
    return NULL;
}

static LPEDICT find_factory_thinker(LPEDICT factory) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == factory && ent->think && !ent->class_id) return ent;
    return NULL;
}

/* The point cast owns a timed factory, and only the real frame scheduler may produce its timed children. */
TEST(wc3_pocket_factory, cast_produces_owned_clockwerks_until_factory_expires) {
    slkTestData_t *rows, *old;
    LPEDICT caster = factory_setup(1, &rows, &old), factory, thinker, first;
    VECTOR2 point = { 256, 192 };
    BOOL cast = S_CastPointTargetSpell(caster, MAKEFOURCC('A','N','s','y'), &point);
    factory = find_class(MAKEFOURCC('n','f','a','c')); thinker = find_factory_thinker(factory);
    if (factory) factory->health.value = factory->health.max_value = 100;
    factory_frame(4900);
    T_NULL(find_class(MAKEFOURCC('n','c','g','b')));
    factory_frame(100); first = find_class(MAKEFOURCC('n','c','g','b'));
    if (first) first->health.value = first->health.max_value = 100;
    factory_frame(5000);
    DWORD second_count = 0;
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == MAKEFOURCC('n','c','g','b')) second_count++;
    factory_frame(7000);
    FLOAT first_health = first ? first->health.value : -1;
    factory_frame(23000);
    BOOL thinker_live = thinker && thinker->inuse;
    factory_cleanup(rows, old);
    T_ASSERT(cast); T_NOT_NULL(factory); T_NOT_NULL(thinker); T_NOT_NULL(first);
    T_EQ(factory->s.player, 0); T_EQ(factory->owner, caster); T_FEQ(factory->s.origin2.x, point.x, .001f);
    T_EQ(first->s.player, 0); T_EQ(first->owner, factory); T_EQ(second_count, 2);
    T_FEQ(first_health, 0, .001f); T_ASSERT(!thinker_live);
}

/* Each learned rank follows AbilityData's factory and clockwerk rawcodes rather than deriving IDs in code. */
TEST(wc3_pocket_factory, level_selects_authored_factory_and_clockwerk) {
    DWORD const factories[] = { MAKEFOURCC('n','f','a','1'), MAKEFOURCC('n','f','a','2') };
    DWORD const clockwerks[] = { MAKEFOURCC('n','c','g','1'), MAKEFOURCC('n','c','g','2') };
    FOR_LOOP(i, 2) {
        slkTestData_t *rows, *old;
        LPEDICT caster = factory_setup(i + 2, &rows, &old);
        VECTOR2 point = { 128, 128 };
        T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','N','s','y'), &point));
        LPEDICT factory = find_class(factories[i]);
        T_NOT_NULL(factory);
        if (factory) factory->health.value = factory->health.max_value = 100;
        factory_frame(5000);
        T_NOT_NULL(find_class(clockwerks[i]));
        factory_cleanup(rows, old);
    }
}

/* Removing the factory invalidates its thinker; a reused or absent factory must not continue production. */
TEST(wc3_pocket_factory, factory_removal_cancels_production) {
    slkTestData_t *rows, *old;
    LPEDICT caster = factory_setup(1, &rows, &old);
    VECTOR2 point = { 128, 128 };
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','N','s','y'), &point));
    LPEDICT factory = find_class(MAKEFOURCC('n','f','a','c'));
    LPEDICT thinker = find_factory_thinker(factory);
    T_NOT_NULL(factory); T_NOT_NULL(thinker);
    if (factory) G_FreeEdict(factory);
    factory_frame(5000);
    T_NULL(find_class(MAKEFOURCC('n','c','g','b'))); T_ASSERT(!thinker || !thinker->inuse);
    factory_cleanup(rows, old);
}

/* If only the factory slot is available, thinker allocation rolls the cast back instead of leaving an inert summon. */
TEST(wc3_pocket_factory, entity_limit_rolls_back_factory_without_fallback) {
    slkTestData_t *rows, *old;
    LPEDICT caster = factory_setup(1, &rows, &old);
    VECTOR2 point = { 128, 128 };
    DWORD max_edicts = globals.max_edicts;
    globals.max_edicts = globals.num_edicts + 1;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','N','s','y'), &point));
    globals.max_edicts = max_edicts;
    T_NULL(find_class(MAKEFOURCC('n','f','a','c')));
    factory_cleanup(rows, old);
}
#endif