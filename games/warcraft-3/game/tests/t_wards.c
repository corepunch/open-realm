#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_ASTA MAKEFOURCC('A', 's', 't', 'a') // Stasis Trap
#define BZ_AEYE MAKEFOURCC('A', 'e', 'y', 'e') // Sentry Ward
#define BZ_AISW MAKEFOURCC('A', 'I', 's', 'w') // item Sentry Ward alias
#define BZ_AIPM MAKEFOURCC('A', 'I', 'p', 'm') // Item Place Goblin Land Mine
#define BZ_AMIN MAKEFOURCC('A', 'm', 'i', 'n') // Mine - exploding
#define BZ_AMNX MAKEFOURCC('A', 'm', 'n', 'x') // mine AOE damage upon death alias
#define BZ_APIV MAKEFOURCC('A', 'p', 'i', 'v') // Permanent Invisibility
#define BZ_ATRU MAKEFOURCC('A', 't', 'r', 'u') // Undead True Sight
#define BZ_BINV MAKEFOURCC('B', 'i', 'n', 'v') // Invisibility buff
#define BZ_BSTA MAKEFOURCC('B', 's', 't', 'a') // Stasis Trap stun buff
#define BZ_BTLF MAKEFOURCC('B', 'T', 'L', 'F') // timed life
#define BZ_HFOO MAKEFOURCC('h', 'f', 'o', 'o') // fixture stasis UnitID (non-stock otot)
#define BZ_OGRU MAKEFOURCC('o', 'g', 'r', 'u') // fixture sentry UnitID (non-stock oeye)
#define BZ_ARM 2.0f // fixture DataA; not stock 10
#define BZ_STA_STUN 4.0f // fixture DataD; not stock 6
#define BZ_STA_HERO 1.5f // fixture HeroDur; not stock 2.5
#define BZ_SIGHT 350.0f // fixture Adt1 Rng; not stock 1100
#define BZ_MINE_ARM 1.0f
#define BZ_MINE_INVIS 0.5f
#define BZ_MINE_TRIGGER 120.0f
#define BZ_MINE_DELAY 0.3f

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);
void G_RunEntities(void);

static char const wards_slk[] =
	"ID;PWXL;N;EBB;Y11;X16\n"
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
	"C;Y5;X7;K\"350\"\nC;Y5;X10;K\"3\"\n"
	"C;Y6;X1;K\"Apiv\"\nC;Y6;X2;K\"Apiv\"\nC;Y6;X3;K\"1\"\n"
	"C;Y6;X8;K\"2\"\n"
	"C;Y7;X1;K\"Atru\"\nC;Y7;X2;K\"Atru\"\nC;Y7;X3;K\"1\"\n"
	"C;Y7;X7;K\"275\"\n"
	"C;Y8;X1;K\"AIpm\"\nC;Y8;X2;K\"AIpm\"\nC;Y8;X3;K\"1\"\n"
	"C;Y8;X4;K\"ground\"\nC;Y8;X7;K\"500\"\nC;Y8;X8;K\"0\"\nC;Y8;X14;K\"hfoo\"\n"
	"C;Y9;X1;K\"Amin\"\nC;Y9;X2;K\"Amin\"\nC;Y9;X3;K\"1\"\n"
	"C;Y9;X4;K\"ground,enemy\"\nC;Y9;X7;K\"120\"\nC;Y9;X10;K\"1\"\nC;Y9;X11;K\"0.5\"\n"
	"C;Y10;X1;K\"Adda\"\nC;Y10;X2;K\"Adda\"\nC;Y10;X3;K\"1\"\n"
	"C;Y10;X4;K\"ground,structure,tree,debris,enemy\"\nC;Y10;X8;K\"0.3\"\nC;Y10;X10;K\"100\"\nC;Y10;X11;K\"40\"\nC;Y10;X12;K\"250\"\nC;Y10;X13;K\"20\"\n"
	"C;Y11;X1;K\"Amnx\"\nC;Y11;X2;K\"Adda\"\nC;Y11;X3;K\"1\"\n"
	"C;Y11;X4;K\"ground,structure,tree,debris,enemy\"\nC;Y11;X8;K\"0.3\"\nC;Y11;X10;K\"100\"\nC;Y11;X11;K\"40\"\nC;Y11;X12;K\"250\"\nC;Y11;X13;K\"20\"\nE\n";

typedef struct {
	slkTestData_t *rows, *old;
	edict_t *caster, *enemy, *far, *hero, *air;
	UnitBalance_t unit_bal, hero_bal;
} wardFix_t;

static void ward_setup(wardFix_t *fix) {
	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
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

static void ward_done(wardFix_t *fix) {
	G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows);
}

static void ward_tick(uint32_t ms) { level.time += ms; G_RunEntities(); }

static edict_t *ward_find(uint32_t class_id) {
	FILTER_EDICTS(ent, ent->inuse && ent->class_id == class_id && ent->owner) return ent;
	return NULL;
}

static uint32_t ward_count(uint32_t class_id) {
	uint32_t n = 0;
	FILTER_EDICTS(ent, ent->inuse && ent->class_id == class_id && ent->summon_ability) n++;
	return n;
}

static uint32_t stasis_stun_ms(edict_t const *unit) {
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
	wardFix_t fix; vec2_t point = { 128, 96 }; edict_t *ward;
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
	wardFix_t fix; vec2_t point = { 64, 0 }; edict_t *ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASTA, .level = 1);
	fix.enemy->s.origin2 = point; fix.hero->s.origin2 = (vec2_t){ 80, 0 };
	fix.far->s.origin2 = (vec2_t){ 500, 0 };
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &point));
	ward = ward_find(BZ_HFOO); T_NOT_NULL(ward);
	ward->health.value = ward->health.max_value = 100;
	ward_tick((uint32_t)(BZ_ARM * 1000.0f) - 1);
	T_EQ(stasis_stun_ms(fix.enemy), 0); T_ASSERT(ward->inuse);
	ward_tick(1);
	T_EQ(stasis_stun_ms(fix.enemy), (uint32_t)(BZ_STA_STUN * 1000.0f));
	T_EQ(stasis_stun_ms(fix.hero), (uint32_t)(BZ_STA_HERO * 1000.0f));
	T_EQ(stasis_stun_ms(fix.far), 0);
	T_ASSERT(fix.enemy->stunned);
	T_ASSERT(!ward->inuse);
	ward_done(&fix);
}

TEST(wc3_spell, stasis_trap_ignores_air_units) {
	wardFix_t fix; vec2_t point = { 0, 0 }; edict_t *ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASTA, .level = 1);
	fix.air->s.origin2 = point;
	fix.enemy->s.origin2 = fix.hero->s.origin2 = fix.far->s.origin2 = (vec2_t){ 500, 0 };
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &point));
	ward = ward_find(BZ_HFOO); T_NOT_NULL(ward);
	ward_tick((uint32_t)(BZ_ARM * 1000.0f));
	T_ASSERT(ward->inuse);
	T_EQ(stasis_stun_ms(fix.air), 0);
	ward_done(&fix);
}

TEST(wc3_spell, stasis_trap_destroys_peer_wards_in_detonation_radius) {
	wardFix_t fix; vec2_t a = { 0, 0 }, b = { 100, 0 };
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASTA, .level = 1);
	fix.enemy->s.origin2 = (vec2_t){ 40, 0 };
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &a));
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &b));
	T_EQ(ward_count(BZ_HFOO), 2);
	ward_tick((uint32_t)(BZ_ARM * 1000.0f));
	T_EQ(ward_count(BZ_HFOO), 0);
	T_EQ(stasis_stun_ms(fix.enemy), (uint32_t)(BZ_STA_STUN * 1000.0f));
	ward_done(&fix);
}

static edict_t *mine_uninitialized(float x, float y) {
	static UnitAbilities_t abilities = { .abilList = "Amin,Amnx", .heroAbilList = "" };
	edict_t *mine = alloc_test_unit(MAKEFOURCC('n', 'g', 'l', 'm'), x, y);
	mine->data.UnitAbilities = &abilities;
	mine->s.player = 0; mine->svflags |= SVF_MONSTER; mine->targtype = TARG_GROUND;
	mine->health.value = mine->health.max_value = 100; mine->collision = 16.0f; mine->die = unit_die;
	return mine;
}

static edict_t *mine_fixture(float x, float y) {
	edict_t *mine = mine_uninitialized(x, y);
	S_UnitAbilityEvent(mine, A_UNIT_INIT);
	return mine;
}

static edict_t *mine_timer(edict_t const *mine) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == mine && ent->think == land_mine_think) return ent;
    return NULL;
}

static edict_t *mine_destructable(float life, float x, float y, TARGTYPE type) {
    edict_t *ent = G_Spawn();
	ent->class_id = MAKEFOURCC('B', '0', '0', 'X');
	ent->s.class_id = ent->class_id;
	G_BindEntityData(ent);
	ent->s.model = 1;
	ent->s.scale = 1.0f;
	ent->s.origin = MAKE(vec3_t, x, y, 0);
	ent->s.origin2 = MAKE(vec2_t, x, y);
	ent->targtype = type;
	ent->health.value = ent->health.max_value = life;
	ent->destructable.initialized = true;
	ent->destructable.item_table = (uint32_t)-1;
	return ent;
}

TEST(wc3_spell, goblin_land_mine_registry_uses_separate_place_and_intrinsic_handlers) {
	wardFix_t fix;
	abilityitem_t place, mine, death;
	ward_setup(&fix);
	place = S_AbilityItem(BZ_AIPM); mine = S_AbilityItem(BZ_AMIN); death = S_AbilityItem(BZ_AMNX);
	T_NOT_NULL(place.ability); T_NOT_NULL(mine.ability); T_NOT_NULL(death.ability);
	T_EQ(place.ability->proc, CAbilityPlaceMine); T_EQ(place.ability->target_type, SPELL_TARGET_POINT);
	T_EQ(mine.ability->proc, CAbilityLandMine);
	T_EQ(death.ability->proc, CAbilityDeathDamageAoe);
	ward_done(&fix);
}

TEST(wc3_spell, item_place_mine_spawns_real_owned_unit_at_point) {
	wardFix_t fix; vec2_t point = { 192, 96 }; edict_t *mine;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AIPM, .level = 1);
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_AIPM, &point));
	mine = ward_find(BZ_HFOO); T_NOT_NULL(mine);
	T_EQ(mine->owner, fix.caster); T_EQ(mine->s.player, fix.caster->s.player);
	T_EQ(mine->summon_ability, BZ_AIPM);
	T_FEQ(mine->s.origin2.x, point.x, .001f); T_FEQ(mine->s.origin2.y, point.y, .001f);
	ward_done(&fix);
}

TEST(wc3_spell, goblin_land_mine_arms_and_transitions_to_viewer_specific_invisibility) {
	wardFix_t fix; edict_t *mine;
	ward_setup(&fix);
	fix.enemy->s.origin2 = fix.hero->s.origin2 = fix.far->s.origin2 = (vec2_t){ 500, 0 };
	mine = mine_fixture(0, 0);
	T_FEQ(mine->collision, 0.0f, .001f); T_ASSERT(!(mine->s.renderfx & RF_HIDDEN));
	ward_tick((uint32_t)(BZ_MINE_INVIS * 1000.0f) - 1); T_ASSERT(!(mine->s.renderfx & RF_HIDDEN));
	ward_tick(1); T_ASSERT(mine->s.renderfx & RF_HIDDEN);
	T_ASSERT(!S_UnitIsInvisibleToPlayer(mine, 0)); T_ASSERT(S_UnitIsInvisibleToPlayer(mine, 1));
	fix.enemy->heroabilities[0] = MAKE(heroability_t, .code = BZ_ATRU, .level = 1);
	fix.enemy->s.origin2 = (vec2_t){ 100, 0 };
	T_ASSERT(S_UnitIsDetectedByPlayer(mine, 1)); T_ASSERT(!S_UnitIsInvisibleToPlayer(mine, 1));
	T_ASSERT(!M_IsDead(mine)); /* detection does not arm/trigger before DataA */
	ward_done(&fix);
}

TEST(wc3_spell, goblin_land_mine_thinker_allocation_failure_keeps_unit_functional) {
	wardFix_t fix; edict_t *mine; uint32_t max_edicts;
	ward_setup(&fix); mine = mine_uninitialized(0, 0);
	max_edicts = globals.max_edicts; globals.max_edicts = globals.num_edicts;
	S_UnitAbilityEvent(mine, A_UNIT_INIT);
	globals.max_edicts = max_edicts;
	T_FEQ(mine->collision, 16.0f, .001f);
	T_ASSERT(!(mine->s.renderfx & RF_HIDDEN));
	ward_done(&fix);
}

TEST(wc3_spell, goblin_land_mine_disable_restores_collision_and_clears_timer) {
	wardFix_t fix; edict_t *mine;
	ward_setup(&fix); mine = mine_fixture(0, 0);
	T_FEQ(mine->collision, 0.0f, .001f); T_NOT_NULL(mine_timer(mine));
	T_ASSERT(G_ActorRemoveSkill(mine, BZ_AMIN));
	T_ASSERT(!G_ActorHasSkill(mine, "Amin"));
	T_FEQ(mine->collision, 16.0f, .001f);
	T_FEQ(mine->s.collision, 16.0f, .001f);
	T_ASSERT(!(mine->s.renderfx & RF_HIDDEN)); T_NULL(mine_timer(mine));
	ward_done(&fix);
}

TEST(wc3_spell, goblin_land_mine_reinitialize_keeps_existing_timer_when_edicts_are_exhausted) {
	wardFix_t fix; edict_t *mine, *timer; uint32_t max_edicts;
	ward_setup(&fix); mine = mine_fixture(0, 0); timer = mine_timer(mine);
	T_NOT_NULL(timer); if (!timer) { ward_done(&fix); return; }
	max_edicts = globals.max_edicts; globals.max_edicts = timer->s.number;
	T_ASSERT(S_UnitAbilityEvent(mine, A_LEVEL_CHANGED));
	globals.max_edicts = max_edicts;
	T_ASSERT(timer->inuse); T_EQ(mine_timer(mine), timer); T_FEQ(mine->collision, 0.0f, .001f);
	ward_done(&fix);
}

TEST(wc3_spell, goblin_land_mine_trigger_uses_cast_range_and_normal_death_path) {
	wardFix_t fix; edict_t *mine;
	ward_setup(&fix);
	fix.enemy->s.origin2 = (vec2_t){ BZ_MINE_TRIGGER - 1.0f, 0 };
	fix.hero->s.origin2 = fix.far->s.origin2 = (vec2_t){ 500, 0 };
	mine = mine_fixture(0, 0);
	ward_tick((uint32_t)(BZ_MINE_ARM * 1000.0f) - 1); T_ASSERT(!M_IsDead(mine));
	ward_tick(1); T_ASSERT(M_IsDead(mine));
	T_ASSERT(!(mine->s.renderfx & RF_HIDDEN));
	T_STREQ(mine->animation_request, "death spell");
	ward_done(&fix);
}

TEST(wc3_spell, goblin_land_mine_ignores_air_and_structures_as_trigger_units) {
	wardFix_t fix; edict_t *mine, *structure;
	ward_setup(&fix);
	fix.enemy->s.origin2 = fix.hero->s.origin2 = fix.far->s.origin2 = (vec2_t){ 500, 0 };
	fix.air->s.origin2 = (vec2_t){ 20, 0 };
	structure = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 40, 0);
	structure->s.player = 1; structure->svflags |= SVF_MONSTER; structure->targtype = TARG_STRUCTURE;
	structure->health.value = structure->health.max_value = 500;
	mine = mine_fixture(0, 0);
	ward_tick((uint32_t)(BZ_MINE_ARM * 1000.0f));
	T_ASSERT(!M_IsDead(mine));
	ward_done(&fix);
}

TEST(wc3_spell, goblin_land_mine_ignores_rooted_ancient_but_triggers_on_uprooted_form) {
	static UnitAbilities_t root_abilities = { .abilList = "Aroo", .heroAbilList = "" };
	wardFix_t fix; edict_t *mine, *ancient;
	ward_setup(&fix);
	fix.enemy->s.origin2 = fix.hero->s.origin2 = fix.far->s.origin2 = fix.air->s.origin2 = (vec2_t){ 500, 0 };
	ancient = alloc_test_unit(MAKEFOURCC('e','a','o','m'), 40, 0);
	ancient->data.UnitAbilities = &root_abilities;
	ancient->s.player = 1; ancient->svflags |= SVF_MONSTER; ancient->targtype = TARG_STRUCTURE;
	ancient->health.value = ancient->health.max_value = 500;
	ancient->movetype = MOVETYPE_NONE; /* rooted */
	mine = mine_fixture(0, 0);
	ward_tick((uint32_t)(BZ_MINE_ARM * 1000.0f));
	T_ASSERT(!M_IsDead(mine));
	ancient->movetype = MOVETYPE_STEP; /* uprooted/mobile */
	ward_tick(1);
	T_ASSERT(M_IsDead(mine));
	ward_done(&fix);
}

TEST(wc3_spell, mine_death_damage_waits_duration_and_enumerates_victims_at_resolution_time) {
	wardFix_t fix; edict_t *mine; float enemy_health;
	ward_setup(&fix);
	fix.enemy->s.origin2 = (vec2_t){ 50, 0 };
	fix.far->s.origin2 = (vec2_t){ 200, 0 };
	fix.hero->s.origin2 = (vec2_t){ 500, 0 };
	mine = mine_fixture(0, 0);
	enemy_health = fix.enemy->health.value;
	ward_tick((uint32_t)(BZ_MINE_ARM * 1000.0f));
	T_ASSERT(M_IsDead(mine)); T_FEQ(fix.enemy->health.value, enemy_health, .001f);
	/* A unit fast enough to leave before Amnx Duration resolves escapes. */
	fix.enemy->s.origin2 = (vec2_t){ 400, 0 };
	ward_tick((uint32_t)(BZ_MINE_DELAY * 1000.0f) - 1); T_FEQ(fix.enemy->health.value, enemy_health, .001f);
	ward_tick(1);
	T_FEQ(fix.enemy->health.value, enemy_health, .001f);
	T_FEQ(fix.far->health.value, 480.0f, .001f); /* partial ring */
	ward_done(&fix);
}

TEST(wc3_spell, mine_death_damage_allocation_failure_resolves_immediately) {
	wardFix_t fix; edict_t *mine; uint32_t max_edicts;
	ward_setup(&fix); fix.enemy->s.origin2 = (vec2_t){ 50, 0 };
	mine = mine_fixture(0, 0);
	max_edicts = globals.max_edicts; globals.max_edicts = globals.num_edicts;
	unit_die(mine, fix.enemy);
	globals.max_edicts = max_edicts;
	T_FEQ(fix.enemy->health.value, 460.0f, .001f);
	ward_done(&fix);
}

TEST(wc3_spell, mine_death_damage_uses_full_and_partial_authored_rings) {
	wardFix_t fix; edict_t *mine;
	ward_setup(&fix);
	fix.enemy->s.origin2 = (vec2_t){ 50, 0 };
	fix.far->s.origin2 = (vec2_t){ 200, 0 };
	fix.hero->s.origin2 = (vec2_t){ 500, 0 };
	mine = mine_fixture(0, 0);
	/* Manual destruction still owns Amnx because damage is attached to death,
	 * but it keeps the ordinary Death presentation rather than detonation. */
	unit_die(mine, fix.enemy);
	T_STREQ(mine->animation_request, "death");
	T_FEQ(fix.enemy->health.value, 500.0f, .001f); T_FEQ(fix.far->health.value, 500.0f, .001f);
	ward_tick((uint32_t)(BZ_MINE_DELAY * 1000.0f));
	T_FEQ(fix.enemy->health.value, 460.0f, .001f);
	T_FEQ(fix.far->health.value, 480.0f, .001f);
	ward_done(&fix);
}

TEST(wc3_spell, mine_death_damage_uses_authored_tree_and_debris_targets) {
	wardFix_t fix; edict_t *mine, *tree, *debris;
	ward_setup(&fix);
	fix.enemy->s.origin2 = fix.hero->s.origin2 = fix.far->s.origin2 = fix.air->s.origin2 = (vec2_t){ 500, 0 };
	tree = mine_destructable(200, 50, 0, TARG_TREE);
	debris = mine_destructable(200, 200, 0, TARG_DEBRIS);
	mine = mine_fixture(0, 0);
	unit_die(mine, fix.enemy);
	T_FEQ(tree->health.value, 200.0f, .001f); T_FEQ(debris->health.value, 200.0f, .001f);
	ward_tick((uint32_t)(BZ_MINE_DELAY * 1000.0f));
	T_FEQ(tree->health.value, 160.0f, .001f);
	T_FEQ(debris->health.value, 180.0f, .001f);
	ward_done(&fix);
}

TEST(wc3_spell, sentry_ward_aliases_share_procedure) {
	T_EQ(S_AbilityItem(BZ_AEYE).ability->proc, CAbilityEvilEye);
	T_EQ(S_AbilityItem(BZ_AISW).ability->proc, CAbilityEvilEye);
	T_EQ(S_AbilityItem(BZ_AEYE).ability->target_type, SPELL_TARGET_POINT);
	T_EQ(S_AbilityItem(BZ_APIV).ability->proc, CAbilityPermanentInvisibility);
}

TEST(wc3_spell, sentry_ward_cast_creates_owned_timed_ward_and_detects_hidden) {
	wardFix_t fix; vec2_t point = { 128, 128 }; edict_t *ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AEYE, .level = 1);
	fix.enemy->s.origin2 = (vec2_t){ 200, 128 };
	fix.enemy->s.renderfx |= RF_HIDDEN;
	fix.far->s.origin2 = (vec2_t){ 900, 128 };
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
	T_ASSERT(S_UnitIsDetectedByPlayer(fix.enemy, 0));
	T_ASSERT(!S_UnitIsDetectedByPlayer(fix.enemy, 2));
	T_ASSERT(!S_UnitIsDetected(fix.far));
	ward_done(&fix);
}



TEST(wc3_spell, undead_true_sight_uses_authored_range) {
	wardFix_t fix;
	abilityitem_t item = S_AbilityItem(BZ_ATRU);
	ward_setup(&fix);
	T_NOT_NULL(item.ability);
	T_EQ(item.ability->proc, CAbilityTrueSight);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ATRU, .level = 1);
	fix.enemy->s.origin2 = (vec2_t){ 250, 0 };
	fix.enemy->s.renderfx |= RF_HIDDEN;
	unit_addtimedstatus(fix.enemy, "Binv", 1, 5.0f);
	T_ASSERT(S_UnitIsDetectedByPlayer(fix.enemy, 0));
	fix.enemy->s.origin2 = (vec2_t){ 300, 0 };
	T_ASSERT(!S_UnitIsDetectedByPlayer(fix.enemy, 0));
	ward_done(&fix);
}

TEST(wc3_spell, sentry_true_sight_makes_known_rf_hidden_invisibility_selectable_for_viewer) {
	wardFix_t fix; vec2_t point = { 128, 128 };
	ward_setup(&fix);
	game.clients[0].ps.number = 0;
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AEYE, .level = 1);
	fix.enemy->s.origin2 = (vec2_t){ 200, 128 };
	fix.enemy->s.renderfx |= RF_HIDDEN;
	unit_addtimedstatus(fix.enemy, "Binv", 1, 5.0f);
	T_ASSERT(!G_UnitCanBeSelected(&game.clients[0], fix.enemy));
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_AEYE, &point));
	T_ASSERT(S_UnitIsDetectedByPlayer(fix.enemy, 0));
	T_ASSERT(G_UnitCanBeSelected(&game.clients[0], fix.enemy));
	ward_done(&fix);
}


TEST(wc3_spell, true_sight_snapshot_and_selection_are_viewer_local) {
	wardFix_t fix; vec2_t point = { 128, 128 }; entityState_t state;
	ward_setup(&fix);
	game.clients[0].ps.number = 0; game.clients[1].ps.number = 1; game.clients[2].ps.number = 2;
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AEYE, .level = 1);
	fix.enemy->s.origin2 = (vec2_t){ 200, 128 };
	fix.enemy->s.renderfx |= RF_HIDDEN;
	unit_addtimedstatus(fix.enemy, "Binv", 1, 5.0f);
	T_NOT_NULL(globals.CustomizeEntity);
	if (!globals.CustomizeEntity) { ward_done(&fix); return; }

	state = fix.enemy->s; globals.CustomizeEntity(1, fix.enemy, &state);
	T_ASSERT(!(state.renderfx & RF_HIDDEN));
	T_ASSERT(G_UnitCanBeSelected(&game.clients[1], fix.enemy));
	state = fix.enemy->s; globals.CustomizeEntity(0, fix.enemy, &state);
	T_ASSERT(state.renderfx & RF_HIDDEN);
	T_ASSERT(!G_UnitCanBeSelected(&game.clients[0], fix.enemy));

	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_AEYE, &point));
	state = fix.enemy->s; globals.CustomizeEntity(0, fix.enemy, &state);
	T_ASSERT(!(state.renderfx & RF_HIDDEN));
	T_ASSERT(G_UnitCanBeSelected(&game.clients[0], fix.enemy));
	state = fix.enemy->s; globals.CustomizeEntity(2, fix.enemy, &state);
	T_ASSERT(state.renderfx & RF_HIDDEN);
	T_ASSERT(fix.enemy->s.renderfx & RF_HIDDEN); /* snapshot customization is local only */
	ward_done(&fix);
}

TEST(wc3_spell, permanent_invisibility_blocks_hostile_acquisition_and_spell_targets_until_detected) {
	wardFix_t fix; vec2_t point = { 64, 0 };
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AEYE, .level = 1);
	fix.enemy->heroabilities[0] = MAKE(heroability_t, .code = BZ_APIV, .level = 1);
	level.time = 1000; S_UnitAbilityEvent(fix.enemy, A_UNIT_INIT);
	level.time = 3000;
	gi.LinkEntity(fix.caster); gi.LinkEntity(fix.enemy);
	T_ASSERT(S_PermanentInvisibilityActive(fix.enemy));
	T_ASSERT(S_UnitIsInvisibleToPlayer(fix.enemy, 0));
	T_NULL(G_FindNearestEnemy(fix.caster, 128.0f));
	T_ASSERT(!S_SpellAllowsTarget(BZ_AEYE, fix.caster, fix.enemy));

	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_AEYE, &point));
	T_ASSERT(!S_UnitIsInvisibleToPlayer(fix.enemy, 0));
	T_ASSERT(G_FindNearestEnemy(fix.caster, 128.0f) == fix.enemy);
	T_ASSERT(S_SpellAllowsTarget(BZ_AEYE, fix.caster, fix.enemy));
	ward_done(&fix);
}

TEST(wc3_spell, permanent_invisibility_uses_authored_transition_after_spawn_and_reveal) {
	wardFix_t fix;
	ward_setup(&fix);
	fix.enemy->heroabilities[0] = MAKE(heroability_t, .code = BZ_APIV, .level = 1);
	level.time = 1000;
	S_UnitAbilityEvent(fix.enemy, A_UNIT_INIT);
	T_EQ(fix.enemy->permanent_invisibility_reveal_until, 3000);
	T_ASSERT(!S_PermanentInvisibilityActive(fix.enemy));
	level.time = 3000;
	T_ASSERT(S_PermanentInvisibilityActive(fix.enemy));
	S_PermanentInvisibilityReveal(fix.enemy);
	T_EQ(fix.enemy->permanent_invisibility_reveal_until, 5000);
	T_ASSERT(!S_PermanentInvisibilityActive(fix.enemy));
	level.time = 5000;
	T_ASSERT(S_PermanentInvisibilityActive(fix.enemy));
	ward_done(&fix);
}


TEST(wc3_spell, active_spell_commit_restarts_permanent_invisibility_transition) {
	wardFix_t fix; vec2_t point = { 128, 96 };
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_ASTA, .level = 1);
	fix.caster->heroabilities[1] = MAKE(heroability_t, .code = BZ_APIV, .level = 1);
	level.time = 1000; S_UnitAbilityEvent(fix.caster, A_UNIT_INIT);
	level.time = 3000;
	T_ASSERT(S_PermanentInvisibilityActive(fix.caster));
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_ASTA, &point));
	T_EQ(fix.caster->permanent_invisibility_reveal_until, 5000);
	T_ASSERT(!S_PermanentInvisibilityActive(fix.caster));
	ward_done(&fix);
}

TEST(wc3_spell, far_sight_detects_permanent_invisibility_only_for_its_viewers) {
	wardFix_t fix; edict_t *sight;
	ward_setup(&fix);
	G_FowInit(); G_FowConnectPlayer(0); G_FowConnectPlayer(1); G_FowConnectPlayer(2);
	fix.caster->runtime.sight_radius.day = 256.0f;
	fix.enemy->heroabilities[0] = MAKE(heroability_t, .code = BZ_APIV, .level = 1);
	level.time = 1000; S_UnitAbilityEvent(fix.enemy, A_UNIT_INIT);
	level.time = 3000;
	G_FowUpdate();
	T_ASSERT(S_PermanentInvisibilityActive(fix.enemy));
	T_ASSERT(!G_FowPlayerCanSeeEntity(0, fix.enemy));
	T_ASSERT(G_FowPlayerCanSeeEntity(1, fix.enemy));

	sight = G_Spawn(); T_NOT_NULL(sight);
	sight->svflags |= SVF_NOCLIENT; sight->think = far_sight_think; sight->s.player = 0;
	sight->s.origin2 = fix.enemy->s.origin2; sight->collision = 128.0f; sight->spawn_time = 5000;
	T_ASSERT(S_UnitIsDetectedByPlayer(fix.enemy, 0));
	T_ASSERT(!S_UnitIsDetectedByPlayer(fix.enemy, 2));
	T_ASSERT(G_FowPlayerCanSeeEntity(0, fix.enemy));

	G_FowShutdown(); ward_done(&fix);
}

TEST(wc3_spell, true_sight_only_reveals_rf_hidden_states_known_to_be_invisibility) {
	wardFix_t fix;
	ward_setup(&fix);
	fix.enemy->s.renderfx |= RF_HIDDEN;
	T_ASSERT(!S_UnitUsesInvisibilityRenderFlag(fix.enemy));
	unit_addtimedstatus(fix.enemy, "Binv", 1, 5.0f);
	T_ASSERT(S_UnitUsesInvisibilityRenderFlag(fix.enemy));
	T_EQ(G_UnitStatusLevel(fix.enemy, BZ_BINV), 1);
	ward_done(&fix);
}

TEST(wc3_spell, sentry_ward_aisw_uses_alias_unitid) {
	wardFix_t fix; vec2_t point = { 64, 64 }; edict_t *ward;
	ward_setup(&fix);
	fix.caster->heroabilities[0] = MAKE(heroability_t, .code = BZ_AISW, .level = 1);
	T_ASSERT(S_CastPointTargetSpell(fix.caster, BZ_AISW, &point));
	ward = ward_find(BZ_OGRU);
	T_NOT_NULL(ward);
	T_EQ(ward->summon_ability, BZ_AISW);
	ward_done(&fix);
}

#endif
