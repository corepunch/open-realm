#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ASTA MAKEFOURCC('A', 's', 't', 'a') // Stasis Trap
#define BZ_AEYE MAKEFOURCC('A', 'e', 'y', 'e') // Sentry Ward
#define BZ_AISW MAKEFOURCC('A', 'I', 's', 'w') // item Sentry Ward alias
#define BZ_BSTA MAKEFOURCC('B', 's', 't', 'a') // Stasis Trap stun buff
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F') // timed life
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // fixture stasis UnitID (non-stock otot)
#define BZ_OGRU MAKEFOURCC('o', 'g', 'r', 'u') // fixture sentry UnitID (non-stock oeye)
#define BZ_ARM 2.0f // fixture DataA; not stock 10
#define BZ_STA_STUN 4.0f // fixture DataD; not stock 6
#define BZ_STA_HERO 1.5f // fixture HeroDur; not stock 2.5
#define BZ_SIGHT 350.0f // fixture Adt1 Rng; not stock 1100

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);
void G_RunEntities(void);

static char const wards_slk[] =
	"ID;PWXL;N;EBB;Y5;X16\n"
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
	"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
	"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
	"C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
	"C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"UnitID1\"\nC;Y1;X15;K\"BuffID1\"\n"
	"C;Y1;X16;K\"Area1\"\n"
	"C;Y2;X1;K\"Asta\"\nC;Y2;X2;K\"Asta\"\nC;Y2;X3;K\"1\"\n"
	"C;Y2;X4;K\"ground,neutral,enemy\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"0\"\n"
	"C;Y2;X7;K\"500\"\nC;Y2;X8;K\"20\"\nC;Y2;X9;K\"1.5\"\n"
	"C;Y2;X10;K\"2\"\nC;Y2;X11;K\"150\"\nC;Y2;X12;K\"300\"\n"
	"C;Y2;X13;K\"4\"\nC;Y2;X14;K\"hfoo\"\nC;Y2;X15;K\"Bsta\"\nC;Y2;X16;K\"0\"\n"
	"C;Y3;X1;K\"Aeye\"\nC;Y3;X2;K\"Aeye\"\nC;Y3;X3;K\"1\"\n"
	"C;Y3;X4;K\"\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\n"
	"C;Y3;X7;K\"500\"\nC;Y3;X8;K\"15\"\nC;Y3;X9;K\"15\"\n"
	"C;Y3;X14;K\"ogru\"\nC;Y3;X15;K\"Beye\"\n"
	"C;Y4;X1;K\"AIsw\"\nC;Y4;X2;K\"Aeye\"\nC;Y4;X3;K\"1\"\n"
	"C;Y4;X4;K\"\"\nC;Y4;X5;K\"0\"\nC;Y4;X6;K\"0\"\n"
	"C;Y4;X7;K\"500\"\nC;Y4;X8;K\"15\"\nC;Y4;X9;K\"15\"\n"
	"C;Y4;X14;K\"ogru\"\nC;Y4;X15;K\"Beye\"\n"
	"C;Y5;X1;K\"Adt1\"\nC;Y5;X2;K\"Adet\"\nC;Y5;X3;K\"1\"\n"
	"C;Y5;X4;K\"vuln,invu\"\nC;Y5;X5;K\"0\"\nC;Y5;X6;K\"0\"\n"
	"C;Y5;X7;K\"350\"\nC;Y5;X10;K\"3\"\nE\n";

typedef struct {
	slkTestData_t *rows, *old;
	LPEDICT caster, enemy, far, hero, air;
	UnitBalance_t unit_bal, hero_bal;
} WARDFIX;

static void ward_setup(WARDFIX *fix) {
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	fix->rows = parse_slk_string(wards_slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
	fix->unit_bal = MAKE(UnitBalance_t, .maxHealth = 500);
	fix->hero_bal = MAKE(UnitBalance_t, .maxHealth = 500, .strength = 20);
	fix->caster = alloc_test_unit(MAKEFOURCC('o', 's', 'h', 'm'), 0, 0);
	fix->enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
	fix->far = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 500, 0);
	fix->hero = alloc_test_unit(MAKEFOURCC('H', 'p', 'a', 'l'), 80, 0);
	fix->air = alloc_test_unit(MAKEFOURCC('h', 'g', 'y', 'r'), 48, 0);
	fix->caster->s.player = 0;
	fix->enemy->s.player = fix->far->s.player = fix->hero->s.player = fix->air->s.player = 1;
	fix->caster->svflags |= SVF_MONSTER; fix->enemy->svflags |= SVF_MONSTER;
	fix->far->svflags |= SVF_MONSTER; fix->hero->svflags |= SVF_MONSTER; fix->air->svflags |= SVF_MONSTER;
	fix->caster->targtype = fix->enemy->targtype = fix->far->targtype = fix->hero->targtype = TARG_GROUND;
	fix->air->targtype = TARG_AIR;
	fix->enemy->data.UnitBalance = &fix->unit_bal;
	fix->far->data.UnitBalance = &fix->unit_bal;
	fix->hero->data.UnitBalance = &fix->hero_bal;
	fix->air->data.UnitBalance = &fix->unit_bal;
	fix->enemy->health.value = fix->enemy->health.max_value = 500;
	fix->far->health.value = fix->far->health.max_value = 500;
	fix->hero->health.value = fix->hero->health.max_value = 500;
	fix->air->health.value = fix->air->health.max_value = 500;
	fix->caster->mana.value = fix->caster->mana.max_value = 500;
	fix->caster->health.value = fix->caster->health.max_value = 1000;
}

static void ward_done(WARDFIX *fix) {
	G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows);
}

static void ward_tick(DWORD ms) { level.time += ms; G_RunEntities(); }

static LPEDICT ward_find(DWORD class_id) {
	FILTER_EDICTS(ent, ent->inuse && ent->class_id == class_id && ent->owner) return ent;
	return NULL;
}

static DWORD ward_count(DWORD class_id) {
	DWORD n = 0;
	FILTER_EDICTS(ent, ent->inuse && ent->class_id == class_id && ent->summon_ability) n++;
	return n;
}

static DWORD stasis_stun_ms(LPCEDICT unit) {
	FOR_LOOP(i, MAX_UNIT_STATUSES)
		if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BSTA)
			return unit->abilstatus[i].duration_ms;
	return 0;
}

TEST(wc3_spell, stasis_trap_procedure_is_point_spell) {
	abilityitem_t item = S_AbilityItem(BZ_ASTA);
	T_NOT_NULL(item.ability);
	T_EQ(item.ability->proc, CAbilityStasisTrap);
	T_EQ(item.ability->target_type, SPELL_TARGET_POINT);
}

TEST(wc3_spell, stasis_trap_cast_creates_owned_timed_invisible_ward) {
	WARDFIX fix; VECTOR2 point = { 128, 96 }; LPEDICT ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASTA, .level = 1);
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &point));
	ward = ward_find(BZ_HFOO);
	T_NOT_NULL(ward);
	T_EQ(ward->owner, fix.caster);
	T_EQ(ward->summon_ability, BZ_ASTA);
	T_ASSERT(ward->s.renderfx & RF_HIDDEN);
	T_EQ(G_UnitStatusLevel(ward, BZ_BTLF), 1);
	T_FEQ(ward->s.origin2.x, point.x, .001f);
	ward_done(&fix);
}

/* Arm delay is DataA; stun uses DataD for units and HeroDur for heroes. */
TEST(wc3_spell, stasis_trap_arms_then_stuns_land_enemies_in_datac) {
	WARDFIX fix; VECTOR2 point = { 64, 0 }; LPEDICT ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASTA, .level = 1);
	fix.enemy->s.origin2 = point; fix.hero->s.origin2 = (VECTOR2){ 80, 0 };
	fix.far->s.origin2 = (VECTOR2){ 500, 0 };
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &point));
	ward = ward_find(BZ_HFOO); T_NOT_NULL(ward);
	ward->health.value = ward->health.max_value = 100;
	ward_tick((DWORD)(BZ_ARM * 1000.0f) - 1);
	T_EQ(stasis_stun_ms(fix.enemy), 0); T_ASSERT(ward->inuse);
	ward_tick(1);
	T_EQ(stasis_stun_ms(fix.enemy), (DWORD)(BZ_STA_STUN * 1000.0f));
	T_EQ(stasis_stun_ms(fix.hero), (DWORD)(BZ_STA_HERO * 1000.0f));
	T_EQ(stasis_stun_ms(fix.far), 0);
	T_ASSERT(fix.enemy->stunned);
	T_ASSERT(!ward->inuse);
	ward_done(&fix);
}

TEST(wc3_spell, stasis_trap_ignores_air_units) {
	WARDFIX fix; VECTOR2 point = { 0, 0 }; LPEDICT ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASTA, .level = 1);
	fix.air->s.origin2 = point;
	fix.enemy->s.origin2 = fix.hero->s.origin2 = fix.far->s.origin2 = (VECTOR2){ 500, 0 };
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &point));
	ward = ward_find(BZ_HFOO); T_NOT_NULL(ward);
	ward_tick((DWORD)(BZ_ARM * 1000.0f));
	T_ASSERT(ward->inuse);
	T_EQ(stasis_stun_ms(fix.air), 0);
	ward_done(&fix);
}

TEST(wc3_spell, stasis_trap_destroys_peer_wards_in_detonation_radius) {
	WARDFIX fix; VECTOR2 a = { 0, 0 }, b = { 100, 0 };
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASTA, .level = 1);
	fix.enemy->s.origin2 = (VECTOR2){ 40, 0 };
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &a));
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &b));
	T_EQ(ward_count(BZ_HFOO), 2);
	ward_tick((DWORD)(BZ_ARM * 1000.0f));
	T_EQ(ward_count(BZ_HFOO), 0);
	T_EQ(stasis_stun_ms(fix.enemy), (DWORD)(BZ_STA_STUN * 1000.0f));
	ward_done(&fix);
}

TEST(wc3_spell, sentry_ward_aliases_share_procedure) {
	T_EQ(S_AbilityItem(BZ_AEYE).ability->proc, CAbilityEvilEye);
	T_EQ(S_AbilityItem(BZ_AISW).ability->proc, CAbilityEvilEye);
	T_EQ(S_AbilityItem(BZ_AEYE).ability->target_type, SPELL_TARGET_POINT);
}

TEST(wc3_spell, sentry_ward_cast_creates_owned_timed_ward_and_detects_hidden) {
	WARDFIX fix; VECTOR2 point = { 128, 128 }; LPEDICT ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AEYE, .level = 1);
	fix.enemy->s.origin2 = (VECTOR2){ 200, 128 };
	fix.enemy->s.renderfx |= RF_HIDDEN;
	fix.far->s.origin2 = (VECTOR2){ 900, 128 };
	fix.far->s.renderfx |= RF_HIDDEN;
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_AEYE, &point));
	ward = ward_find(BZ_OGRU);
	T_NOT_NULL(ward);
	T_EQ(ward->owner, fix.caster);
	T_EQ(ward->summon_ability, BZ_AEYE);
	T_ASSERT(ward->s.renderfx & RF_HIDDEN);
	T_EQ(G_UnitStatusLevel(ward, BZ_BTLF), 1);
	T_FEQ(ward->wait, BZ_SIGHT, .001f);
	T_ASSERT(S_UnitIsDetected(fix.enemy));
	T_ASSERT(!S_UnitIsDetected(fix.far));
	ward_done(&fix);
}

TEST(wc3_spell, sentry_ward_aisw_uses_alias_unitid) {
	WARDFIX fix; VECTOR2 point = { 64, 64 }; LPEDICT ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AISW, .level = 1);
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_AISW, &point));
	ward = ward_find(BZ_OGRU);
	T_NOT_NULL(ward);
	T_EQ(ward->summon_ability, BZ_AISW);
	ward_done(&fix);
}

#endif
