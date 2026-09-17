#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ANVC MAKEFOURCC('A', 'N', 'v', 'c') // rawcode; TFT Firelord Volcano
#define BZ_BSTU MAKEFOURCC('B', 's', 't', 'u') // rawcode; shared stun timed status

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock DataB/C/D/E so tests cannot pass on hardcoded retail 8/5/2/100. */
static LPCSTR volcano_slk =
    "ID;PWXL;N;EBB;Y2;X16\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
    "C;Y1;X10;K\"Area1\"\nC;Y1;X11;K\"DataA1\"\nC;Y1;X12;K\"DataB1\"\n"
    "C;Y1;X13;K\"DataC1\"\nC;Y1;X14;K\"DataD1\"\nC;Y1;X15;K\"DataE1\"\n"
    "C;Y1;X16;K\"DataF1\"\n"
    "C;Y2;X1;K\"ANvc\"\nC;Y2;X2;K\"ANvc\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X4;K\"ground,structure,notself,tree,debris\"\n"
    "C;Y2;X5;K\"50\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"800\"\n"
    "C;Y2;X8;K\"4\"\nC;Y2;X9;K\"1.5\"\nC;Y2;X10;K\"200\"\n"
    "C;Y2;X11;K\"3\"\nC;Y2;X12;K\"2\"\nC;Y2;X13;K\"1\"\n"
    "C;Y2;X14;K\"3\"\nC;Y2;X15;K\"40\"\nC;Y2;X16;K\"0.5\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    LPEDICT caster, enemy, building, far, hero;
    UnitBalance_t hero_bal, unit_bal, bldg_bal;
} VOLCFIX;

static LPEDICT volcano_thinker(LPEDICT caster) {
    FILTER_EDICTS(ent, ent->owner == caster && ent->think) return ent;
    return NULL;
}

static DWORD volcano_stun_ms(LPCEDICT unit) {
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BSTU)
            return unit->abilstatus[i].duration_ms;
    return 0;
}

static VOLCFIX volcano_setup(void) {
    VOLCFIX fix;
    reset_entities(); setup_test_world(); level.time = 1000;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix.rows = parse_slk_string(volcano_slk); fix.old = G_SetSLKRows("AbilityData", fix.rows);
    fix.unit_bal = MAKE(UnitBalance_t, .maxHealth = 500);
    fix.bldg_bal = MAKE(UnitBalance_t, .maxHealth = 1000, .isBuilding = true);
    fix.hero_bal = MAKE(UnitBalance_t, .maxHealth = 500, .strength = 20);
    fix.caster = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), 0, 0);
    fix.enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    fix.building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 96, 0);
    fix.far = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 400, 0);
    fix.hero = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), 128, 0);
    fix.caster->s.player = 0;
    fix.enemy->s.player = fix.building->s.player = fix.far->s.player = fix.hero->s.player = 1;
    fix.caster->svflags |= SVF_MONSTER; fix.enemy->svflags |= SVF_MONSTER;
    fix.building->svflags |= SVF_MONSTER; fix.far->svflags |= SVF_MONSTER; fix.hero->svflags |= SVF_MONSTER;
    fix.caster->targtype = fix.enemy->targtype = fix.far->targtype = fix.hero->targtype = TARG_GROUND;
    fix.building->targtype = TARG_STRUCTURE;
    fix.enemy->data.UnitBalance = &fix.unit_bal;
    fix.building->data.UnitBalance = &fix.bldg_bal;
    fix.far->data.UnitBalance = &fix.unit_bal;
    fix.hero->data.UnitBalance = &fix.hero_bal;
    fix.enemy->health.value = fix.enemy->health.max_value = 500;
    fix.building->health.value = fix.building->health.max_value = 1000;
    fix.far->health.value = fix.far->health.max_value = 500;
    fix.hero->health.value = fix.hero->health.max_value = 500;
    fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ANVC, .level = 1);
    fix.caster->mana.value = fix.caster->mana.max_value = 200;
    return fix;
}

static void volcano_done(VOLCFIX fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, volcano_procedure_is_channel_point_spell) {
    abilityitem_t item = S_AbilityItem(BZ_ANVC);
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityVolcano);
    T_ASSERT(item.ability->flags & AB_CHANNEL);
    T_EQ(item.ability->target_type, SPELL_TARGET_POINT);
}

/* First wave runs on cast: unit takes DataE, building takes DataE*DataD, stun uses Dur. */
TEST(wc3_spell, volcano_first_wave_damages_unit_and_building_with_factor) {
    VOLCFIX fix = volcano_setup();
    VECTOR2 point = fix.enemy->s.origin2;
    T_ASSERT(G_UnitIsBuilding(fix.building->class_id));
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANVC, &point));
    T_EQ(fix.caster->channel.code, BZ_ANVC);
    T_FEQ(fix.enemy->health.value, 460, 0.001f);
    T_FEQ(fix.building->health.value, 880, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BSTU), 1);
    T_EQ(volcano_stun_ms(fix.enemy), 4000);
    T_FEQ(fix.far->health.value, 500, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.far, BZ_BSTU), 0);
    volcano_done(fix);
}

TEST(wc3_spell, volcano_stun_uses_herodur_for_heroes) {
    VOLCFIX fix = volcano_setup();
    VECTOR2 point = fix.hero->s.origin2;
    T_ASSERT(G_UnitIsHero(fix.hero));
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANVC, &point));
    T_EQ(G_UnitStatusLevel(fix.hero, BZ_BSTU), 1);
    T_EQ(volcano_stun_ms(fix.hero), 1500);
    volcano_done(fix);
}

/* Second pulse waits for authored DataC, then fires through the entity scheduler. */
TEST(wc3_spell, volcano_second_wave_after_authored_interval) {
    VOLCFIX fix = volcano_setup();
    VECTOR2 point = fix.enemy->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANVC, &point));
    LPEDICT thinker = volcano_thinker(fix.caster);
    T_NOT_NULL(thinker);
    T_FEQ(fix.enemy->health.value, 460, 0.001f);
    level.time += FRAMETIME; G_RunEntities();
    T_FEQ(fix.enemy->health.value, 460, 0.001f);
    level.time = thinker->freetime; G_RunEntities();
    T_FEQ(fix.enemy->health.value, 420, 0.001f);
    T_EQ(fix.caster->channel.code, 0);
    T_ASSERT(!thinker->inuse);
    volcano_done(fix);
}

TEST(wc3_spell, volcano_caster_move_cancels_remaining_waves) {
    VOLCFIX fix = volcano_setup();
    VECTOR2 point = fix.enemy->s.origin2;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANVC, &point));
    LPEDICT thinker = volcano_thinker(fix.caster);
    T_NOT_NULL(thinker);
    fix.caster->s.origin2.x += 10; fix.caster->s.origin.x += 10;
    level.time = thinker->freetime; G_RunEntities();
    T_EQ(fix.caster->channel.code, 0);
    T_FEQ(fix.enemy->health.value, 460, 0.001f);
    T_ASSERT(!thinker->inuse);
    volcano_done(fix);
}

#endif
