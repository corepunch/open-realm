#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AMFL MAKEFOURCC('A', 'm', 'f', 'l') // rawcode; Faerie Dragon Mana Flare
#define BZ_BMFL MAKEFOURCC('B', 'm', 'f', 'l') // rawcode; Mana Flare caster buff
#define BZ_AHTB MAKEFOURCC('A', 'H', 't', 'b') // rawcode; Storm Bolt probe for enemy casts

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock DataA=7 / DataC=70 / DataE=9 / Cost=25 so tests cannot pass on retail 3/90/12/50. */
#define MFL_SLK \
	"ID;PWXL;N;EBB;Y3;X18\n" \
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
	"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
	"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
	"C;Y1;X10;K\"Area1\"\nC;Y1;X11;K\"Cast1\"\nC;Y1;X12;K\"DataA1\"\n" \
	"C;Y1;X13;K\"DataB1\"\nC;Y1;X14;K\"DataC1\"\nC;Y1;X15;K\"DataD1\"\n" \
	"C;Y1;X16;K\"DataE1\"\nC;Y1;X17;K\"DataF1\"\nC;Y1;X18;K\"BuffID1\"\n" \
	"C;Y2;X1;K\"Amfl\"\nC;Y2;X2;K\"Amfl\"\nC;Y2;X3;K\"1\"\n" \
	"C;Y2;X4;K\"air,ground,enemy\"\nC;Y2;X5;K\"25\"\nC;Y2;X6;K\"0\"\n" \
	"C;Y2;X7;K\"200\"\nC;Y2;X8;K\"30\"\nC;Y2;X9;K\"30\"\n" \
	"C;Y2;X10;K\"750\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"7\"\n" \
	"C;Y2;X13;K\"2\"\nC;Y2;X14;K\"70\"\nC;Y2;X15;K\"40\"\n" \
	"C;Y2;X16;K\"9\"\nC;Y2;X17;K\"1\"\nC;Y2;X18;K\"Bmfl,Bmfa\"\n" \
	"C;Y3;X1;K\"AHtb\"\nC;Y3;X2;K\"AHtb\"\nC;Y3;X3;K\"1\"\n" \
	"C;Y3;X4;K\"air,ground,enemy\"\nC;Y3;X5;K\"10\"\nC;Y3;X6;K\"0\"\n" \
	"C;Y3;X7;K\"600\"\nC;Y3;X8;K\"5\"\nC;Y3;X9;K\"3\"\n" \
	"C;Y3;X10;K\"0\"\nC;Y3;X11;K\"0\"\nC;Y3;X12;K\"100\"\n" \
	"C;Y3;X13;K\"0\"\nC;Y3;X14;K\"0\"\nC;Y3;X15;K\"0\"\n" \
	"C;Y3;X16;K\"0\"\nC;Y3;X17;K\"0\"\nC;Y3;X18;K\"BHtb\"\nE\n"

typedef struct {
	slkTestData_t *rows, *old;
	LPEDICT flare, enemy, ally, far_enemy, far_bait, splash;
	UnitBalance_t enemy_bal, hero_bal;
} MFLFIX;

/* Fill FIX in place so UnitBalance pointers stay live (Linux CI HeroDur). */
static void mfl_setup(MFLFIX *fix, LPCSTR slk) {
	memset(fix, 0, sizeof(*fix));
	reset_entities(); setup_test_world(); level.time = 1000;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	fix->rows = parse_slk_string(slk); fix->old = G_SetSLKRows("AbilityData", fix->rows);
	fix->enemy_bal = MAKE(UnitBalance_t, .maxHealth = 500, .level = 2);
	fix->hero_bal = MAKE(UnitBalance_t, .maxHealth = 500, .strength = 20, .level = 1);
	fix->flare = alloc_test_unit(MAKEFOURCC('e', 'f', 'd', 'r'), 0, 0);
	fix->enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 64, 0);
	fix->ally = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 96, 0);
	/* Outside Area(750) but with a local bait so AHtb Rng(600) still reaches. */
	fix->far_enemy = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 900, 0);
	fix->far_bait = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 950, 0);
	fix->splash = alloc_test_unit(MAKEFOURCC('h', 'p', 'r', 'i'), 100, 0);
	fix->flare->s.player = fix->ally->s.player = fix->far_bait->s.player = 0;
	fix->enemy->s.player = fix->far_enemy->s.player = fix->splash->s.player = 1;
	fix->flare->svflags |= SVF_MONSTER; fix->enemy->svflags |= SVF_MONSTER;
	fix->ally->svflags |= SVF_MONSTER; fix->far_enemy->svflags |= SVF_MONSTER;
	fix->far_bait->svflags |= SVF_MONSTER; fix->splash->svflags |= SVF_MONSTER;
	fix->flare->targtype = fix->enemy->targtype = fix->ally->targtype = TARG_GROUND;
	fix->far_enemy->targtype = fix->far_bait->targtype = fix->splash->targtype = TARG_GROUND;
	fix->enemy->data.UnitBalance = &fix->enemy_bal;
	fix->flare->heroabilities[0] = MAKE(heroability_t, .code = BZ_AMFL, .level = 1);
	fix->enemy->heroabilities[0] = MAKE(heroability_t, .code = BZ_AHTB, .level = 1);
	fix->ally->heroabilities[0] = MAKE(heroability_t, .code = BZ_AHTB, .level = 1);
	fix->far_enemy->heroabilities[0] = MAKE(heroability_t, .code = BZ_AHTB, .level = 1);
	fix->flare->mana.value = fix->flare->mana.max_value = 200;
	fix->enemy->mana.value = fix->enemy->mana.max_value = 100;
	fix->ally->mana.value = fix->ally->mana.max_value = 100;
	fix->far_enemy->mana.value = fix->far_enemy->mana.max_value = 100;
	fix->splash->mana.value = fix->splash->mana.max_value = 100;
	fix->flare->health.value = fix->flare->health.max_value = 450;
	fix->flare->armor_value = 0;
	fix->enemy->health.value = fix->enemy->health.max_value = 500;
	fix->ally->health.value = fix->ally->health.max_value = 500;
	fix->far_enemy->health.value = fix->far_enemy->health.max_value = 500;
	fix->far_bait->health.value = fix->far_bait->health.max_value = 500;
	fix->splash->health.value = fix->splash->health.max_value = 500;
	fix->flare->stand = unit_stand; fix->enemy->stand = unit_stand;
}

static void mfl_done(MFLFIX *fix) {
	G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows);
}

TEST(wc3_spell, mana_flare_registers_channel_procedure) {
	abilityitem_t item = S_AbilityItem(BZ_AMFL);
	T_EQ(item.ability->proc, CAbilityManaFlare);
	T_ASSERT(item.ability->flags & AB_SPELL);
	T_ASSERT(item.ability->flags & AB_CHANNEL);
	T_ASSERT(item.ability->flags & AB_UPDATE);
	T_EQ(item.ability->target_type, SPELL_TARGET_NONE);
}

/* Activation applies Bmfl, spends Cost, locks channel, and adds authored DataE armor. */
TEST(wc3_spell, mana_flare_activates_buff_channel_and_armor) {
	MFLFIX fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_FEQ(fix.flare->mana.value, 175, 0.001f);
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	T_EQ(fix.flare->channel.code, BZ_AMFL);
	T_FEQ(G_UnitArmorValue(fix.flare), 9, 0.001f);
	mfl_done(&fix);
}

/* Enemy cast inside Area: damage = min(DataC, cost * DataA) = min(70, 10*7) = 70. */
TEST(wc3_spell, mana_flare_damages_enemy_caster_from_authored_data) {
	MFLFIX fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f); /* 500 - 70 */
	mfl_done(&fix);
}

TEST(wc3_spell, mana_flare_ignores_out_of_area_and_friendly_casts) {
	MFLFIX fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(S_CastUnitTargetSpell(fix.far_enemy, BZ_AHTB, fix.far_bait));
	T_FEQ(fix.far_enemy->health.value, 500, 0.001f);
	T_ASSERT(S_CastUnitTargetSpell(fix.ally, BZ_AHTB, fix.enemy));
	T_FEQ(fix.ally->health.value, 500, 0.001f);
	mfl_done(&fix);
}

/* Cast interval gates a second flare until Cast seconds elapse. */
TEST(wc3_spell, mana_flare_respects_cast_interval_between_flares) {
	MFLFIX fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f);
	fix.enemy->mana.value = 100;
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f); /* still gated */
	level.time += 750;
	fix.enemy->mana.value = 100;
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 360, 0.001f);
	mfl_done(&fix);
}

/* Channel cancel strips Bmfl and armor; natural expiry does the same. */
TEST(wc3_spell, mana_flare_cancel_and_expiry_clear_buff) {
	MFLFIX fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	S_SpellCancelChannel(fix.flare);
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 0);
	T_FEQ(G_UnitArmorValue(fix.flare), 0, 0.001f);
	fix.flare->mana.value = 200;
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	level.time += 30000; unit_updatestatuses(fix.flare);
	S_RunAbilityUpdates(fix.flare);
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 0);
	T_EQ(fix.flare->channel.code, 0);
	T_FEQ(G_UnitArmorValue(fix.flare), 0, 0.001f);
	mfl_done(&fix);
}

/* DataF splash hits a nearby mana-pool enemy within Rng of the primary victim. */
TEST(wc3_spell, mana_flare_splash_hits_mana_units_in_range) {
	MFLFIX fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f);
	T_FEQ(fix.splash->health.value, 430, 0.001f);
	mfl_done(&fix);
}

#endif
