#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
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

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);
void G_RunEntities(void);

/* Non-stock ANin (+ ANrc link): DataA=40, DataB=12, DataC=0.5, Dur=3, HeroDur=1.5, Area=200. */
static char const inferno_slk[] =
    "ID;PWXL;N;EBB;Y4;X14\n"
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
    "C;Y3;X12;K\"0\"\nC;Y3;X13;K\"0\"\nC;Y3;X14;K\"1\"\n"
    "C;Y4;X1;K\"AUin\"\nC;Y4;X2;K\"AUin\"\n"
    "C;Y4;X3;K\"ground,structure,debris,enemy,neutral\"\n"
    "C;Y4;X4;K\"3\"\nC;Y4;X5;K\"1.5\"\nC;Y4;X6;K\"200\"\n"
    "C;Y4;X7;K\"40\"\nC;Y4;X8;K\"12\"\nC;Y4;X9;K\"0.5\"\n"
    "C;Y4;X10;K\"hfoo\"\nC;Y4;X11;K\"900\"\nC;Y4;X12;K\"0\"\n"
    "C;Y4;X13;K\"0\"\nC;Y4;X14;K\"1\"\nE\n";

typedef struct {
    slkTestData_t *rows, *old;
    edict_t *caster, *enemy, *far, *hero;
    UnitBalance_t unit_bal, hero_bal;
    vec2_t point;
} inFix_t;

static uint32_t stun_ms(edict_t const *unit) {
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BSTU)
            return unit->abilstatus[i].duration_ms;
    return 0;
}

static edict_t *inferno_thinker(edict_t *caster) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->think == inferno_think)
        return ent;
    return NULL;
}

static edict_t *inferno_summon(edict_t *caster) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->class_id == BZ_HFOO)
        return ent;
    return NULL;
}

static edict_t *spell_approach(edict_t *caster, uint32_t code) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->class_id == code &&
                  ent->think == S_SpellTargetApproachThink)
        return ent;
    return NULL;
}

/* Fill the caller's INFIX. Returning a copy would dangle UnitBalance pointers
 * (&local.unit_bal) after return; Linux then misreads G_UnitIsHero (HeroDur vs Dur). */
static void inferno_setup(inFix_t *fix, uint32_t code) {
    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    fix->rows = parse_slk_string(inferno_slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
    fix->unit_bal = MAKE(UnitBalance_t, .maxHealth = 500);
    fix->hero_bal = MAKE(UnitBalance_t, .maxHealth = 500, .strength = 20);
    fix->caster = alloc_test_unit(MAKEFOURCC('U', 'w', 'a', 'r'), 0, 0);
    fix->enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
    fix->far = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 400, 0);
    fix->hero = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), 96, 0);
    fix->caster->s.player = 0;
    fix->enemy->s.player = fix->far->s.player = fix->hero->s.player = 1;
    fix->caster->svflags |= SVF_MONSTER; fix->enemy->svflags |= SVF_MONSTER;
    fix->far->svflags |= SVF_MONSTER; fix->hero->svflags |= SVF_MONSTER;
    fix->caster->targtype = fix->enemy->targtype = fix->far->targtype = fix->hero->targtype = TARG_GROUND;
    fix->enemy->data.UnitBalance = &fix->unit_bal;
    fix->far->data.UnitBalance = &fix->unit_bal;
    fix->hero->data.UnitBalance = &fix->hero_bal;
    fix->enemy->health.value = fix->enemy->health.max_value = 500;
    fix->far->health.value = fix->far->health.max_value = 500;
    fix->hero->health.value = fix->hero->health.max_value = 500;
    fix->caster->heroabilities[0] = MAKE(heroability_t, .code = code, .level = 1);
    fix->caster->mana.value = fix->caster->mana.max_value = 200;
    fix->point = fix->enemy->s.origin2;
}

static void inferno_done(inFix_t fix) { G_SetSLKRows("AbilityData", fix.old); free_slk_rows(fix.rows); }

TEST(wc3_spell, inferno_procedure_and_flags) {
    abilityitem_t item = S_AbilityItem(BZ_ANIN);
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityInferno);
    T_ASSERT(item.ability->flags & AB_SPELL);
    T_ASSERT(!(item.ability->flags & AB_CHANNEL));
    T_EQ((int)item.ability->target_type, (int)SPELL_TARGET_POINT);
}

TEST(wc3_spell, point_spell_order_approach_round_trips_save) {
    cstring_t const path = "/tmp/openwarcraft3-point-spell-approach-save.bin";
    inFix_t fix; edict_t *approach, *summon; vec2_t point = { 1200, 0 };
    uint32_t const code = MAKEFOURCC('A','U','i','n');
    uint32_t caster_slot, approach_slot, frame;
    bool accepted, saved;

    inferno_setup(&fix, code);
    caster_slot = (uint32_t)(fix.caster - g_edicts);
    fix.caster->think = monster_think; fix.caster->movetype = MOVETYPE_STEP;
    fix.caster->collision = 16.0f; fix.caster->unitinfo.MoveSpeed = 300.0f;
    fix.caster->stand = unit_stand; unit_stand(fix.caster); gi.LinkEntity(fix.caster);
    fix.enemy->s.origin2.y = fix.far->s.origin2.y = fix.hero->s.origin2.y = 1000;
    fix.enemy->s.origin.y = fix.far->s.origin.y = fix.hero->s.origin.y = 1000;
    gi.LinkEntity(fix.enemy); gi.LinkEntity(fix.far); gi.LinkEntity(fix.hero);

    accepted = G_IssueUnitPointOrder(fix.caster, "inferno", &point, false, 0, 0.0f);
    T_ASSERT(accepted);
    approach = spell_approach(fix.caster, code);
    T_NOT_NULL(approach);
    if (!accepted || !approach) goto cleanup_point_spell_approach;
    T_ASSERT(fix.caster->goalentity == approach);
    T_FEQ(approach->s.origin2.x, point.x, 0.001f);
    T_FEQ(approach->s.origin2.y, point.y, 0.001f);
    approach_slot = (uint32_t)(approach - g_edicts);

    saved = WriteGame(path);
    T_ASSERT(saved);
    if (saved) {
        T_ASSERT(ReadGame(path));
        fix.caster = &globals.edicts[caster_slot];
        approach = &globals.edicts[approach_slot];
        T_ASSERT(approach->inuse && approach->think == S_SpellTargetApproachThink);
        T_ASSERT(approach->goalentity == approach);
        T_ASSERT(fix.caster->goalentity == approach);
        T_ASSERT(move_is_active_order_walk(fix.caster));
        T_FEQ(approach->s.origin2.x, point.x, 0.001f);
        T_FEQ(approach->s.origin2.y, point.y, 0.001f);

        for (frame = 0; frame < 200 && approach->inuse; frame++) {
            level.time += FRAMETIME;
            G_RunEntities();
        }
        T_ASSERT(frame < 200);
        T_ASSERT(!approach->inuse);
        T_FEQ(fix.caster->s.origin2.x, point.x - S_SpellRange(code, 1), 100.0f);
        summon = inferno_summon(fix.caster);
        T_NOT_NULL(summon);
        if (summon) T_FEQ(summon->s.origin2.x, point.x, 0.001f);
    }

cleanup_point_spell_approach:
    remove(path);
    inferno_done(fix);
}

/* DataC delays blast: no damage/stun/summon until the authored delay elapses. */
TEST(wc3_spell, inferno_impact_after_authored_delay) {
    inFix_t fix; inferno_setup(&fix, BZ_ANIN);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANIN, &fix.point));
    T_EQ(fix.caster->channel.code, 0);
    T_FEQ(fix.enemy->health.value, 500, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BSTU), 0);
    T_NULL(inferno_summon(fix.caster));
    T_NOT_NULL(inferno_thinker(fix.caster));

    level.time += (uint32_t)(BZ_DELAY * 1000.0f) - 1; G_RunEntities();
    T_FEQ(fix.enemy->health.value, 500, 0.001f);
    T_NULL(inferno_summon(fix.caster));

    level.time += 1; G_RunEntities();
    T_FEQ(fix.enemy->health.value, 500.0f - BZ_DMG, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BSTU), 1);
    T_EQ(stun_ms(fix.enemy), (uint32_t)(BZ_STUN * 1000.0f));
    T_NOT_NULL(inferno_summon(fix.caster));
    T_ASSERT(S_UnitHasStatus(inferno_summon(fix.caster), BZ_BTLF));
    T_EQ(stun_ms(inferno_summon(fix.caster)), 0);
    T_NULL(inferno_thinker(fix.caster));
    inferno_done(fix);
}

TEST(wc3_spell, inferno_out_of_area_untouched) {
    inFix_t fix; inferno_setup(&fix, BZ_ANIN);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANIN, &fix.point));
    level.time += (uint32_t)(BZ_DELAY * 1000.0f); G_RunEntities();
    T_FEQ(fix.far->health.value, 500, 0.001f);
    T_EQ(G_UnitStatusLevel(fix.far, BZ_BSTU), 0);
    inferno_done(fix);
}

TEST(wc3_spell, inferno_summon_uses_datab_life) {
    inFix_t fix; inferno_setup(&fix, BZ_ANIN);
    edict_t *summon;
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANIN, &fix.point));
    level.time += (uint32_t)(BZ_DELAY * 1000.0f); G_RunEntities();
    summon = inferno_summon(fix.caster);
    T_NOT_NULL(summon);
    T_EQ(summon->class_id, BZ_HFOO);
    T_EQ(summon->s.player, fix.caster->s.player);
    T_ASSERT(S_UnitHasStatus(summon, BZ_BTLF));
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (summon->abilstatus[i].level && summon->abilstatus[i].code == BZ_BTLF)
            T_EQ(summon->abilstatus[i].duration_ms, (uint32_t)(BZ_LIFE * 1000.0f));
    inferno_done(fix);
}

TEST(wc3_spell, inferno_stun_uses_herodur_for_heroes) {
    inFix_t fix; inferno_setup(&fix, BZ_ANIN);
    vec2_t point = fix.hero->s.origin2;
    T_ASSERT(G_UnitIsHero(fix.hero));
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANIN, &point));
    level.time += (uint32_t)(BZ_DELAY * 1000.0f); G_RunEntities();
    T_EQ(G_UnitStatusLevel(fix.hero, BZ_BSTU), 1);
    T_EQ(stun_ms(fix.hero), (uint32_t)(BZ_HERO * 1000.0f));
    T_FEQ(fix.hero->health.value, 500.0f - BZ_DMG, 0.001f);
    inferno_done(fix);
}

/* Rain of Chaos DataA still resolves the Inferno row for the landing blast+summon. */
TEST(wc3_spell, inferno_rain_of_chaos_lands_via_inferno_row) {
    inFix_t fix; inferno_setup(&fix, BZ_ANRC);
    T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ANRC, &fix.point));
    T_EQ(fix.caster->channel.code, 0);
    /* Fixture DataC=0.5 so the RoC landing schedules Inferno delay before summon. */
    T_NULL(inferno_summon(fix.caster));
    level.time += (uint32_t)(BZ_DELAY * 1000.0f); G_RunEntities();
    T_NOT_NULL(inferno_summon(fix.caster));
    T_ASSERT(S_UnitHasStatus(inferno_summon(fix.caster), BZ_BTLF));
    inferno_done(fix);
}

#endif
