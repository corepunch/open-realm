#ifdef BZ_TESTS
#include "test.h"
#include "../skills/s_skills.h"

#define BZ_AMFL MAKEFOURCC('A', 'm', 'f', 'l') // rawcode; Faerie Dragon Mana Flare
#define BZ_BMFL MAKEFOURCC('B', 'm', 'f', 'l') // rawcode; Mana Flare caster buff
#define BZ_AHTB MAKEFOURCC('A', 'H', 't', 'b') // rawcode; Storm Bolt probe for enemy casts
#define BZ_APSH MAKEFOURCC('A', 'p', 's', 'h') // rawcode; Faerie Dragon Phase Shift

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

/* Non-stock DataA=7 / DataC=70 / DataE=9 / Cost=25 so tests cannot pass on retail 3/90/12/50. */
#define MFL_SLK_WITH_COOL(COOL) \
	"ID;PWXL;N;EBB;Y4;X18\n" \
	"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n" \
	"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n" \
	"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n" \
	"C;Y1;X10;K\"Area1\"\nC;Y1;X11;K\"Cast1\"\nC;Y1;X12;K\"DataA1\"\n" \
	"C;Y1;X13;K\"DataB1\"\nC;Y1;X14;K\"DataC1\"\nC;Y1;X15;K\"DataD1\"\n" \
	"C;Y1;X16;K\"DataE1\"\nC;Y1;X17;K\"DataF1\"\nC;Y1;X18;K\"BuffID1\"\n" \
	"C;Y2;X1;K\"Amfl\"\nC;Y2;X2;K\"Amfl\"\nC;Y2;X3;K\"1\"\n" \
	"C;Y2;X4;K\"air,ground,enemy\"\nC;Y2;X5;K\"25\"\nC;Y2;X6;K\"" COOL "\"\n" \
	"C;Y2;X7;K\"200\"\nC;Y2;X8;K\"30\"\nC;Y2;X9;K\"30\"\n" \
	"C;Y2;X10;K\"750\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"7\"\n" \
	"C;Y2;X13;K\"2\"\nC;Y2;X14;K\"70\"\nC;Y2;X15;K\"40\"\n" \
	"C;Y2;X16;K\"9\"\nC;Y2;X17;K\"1\"\nC;Y2;X18;K\"Bmfl,Bmfa\"\n" \
	"C;Y3;X1;K\"AHtb\"\nC;Y3;X2;K\"AHtb\"\nC;Y3;X3;K\"1\"\n" \
	"C;Y3;X4;K\"air,ground,enemy\"\nC;Y3;X5;K\"10\"\nC;Y3;X6;K\"0\"\n" \
	"C;Y3;X7;K\"600\"\nC;Y3;X8;K\"5\"\nC;Y3;X9;K\"3\"\n" \
	"C;Y3;X10;K\"0\"\nC;Y3;X11;K\"0\"\nC;Y3;X12;K\"100\"\n" \
	"C;Y3;X13;K\"0\"\nC;Y3;X14;K\"0\"\nC;Y3;X15;K\"0\"\n" \
	"C;Y3;X16;K\"0\"\nC;Y3;X17;K\"0\"\nC;Y3;X18;K\"BHtb\"\n" \
	"C;Y4;X1;K\"Apsh\"\nC;Y4;X2;K\"Apsh\"\nC;Y4;X3;K\"1\"\n" \
	"C;Y4;X5;K\"15\"\nC;Y4;X6;K\"0\"\nC;Y4;X8;K\"2\"\n" \
	"C;Y4;X9;K\"2\"\nC;Y4;X18;K\"Bpsh\"\nE\n"

#define MFL_SLK MFL_SLK_WITH_COOL("0")

typedef struct {
	slkTestData_t *rows, *old;
	edict_t *flare, *enemy, *ally, *far_enemy, *far_bait, *splash;
	UnitBalance_t enemy_bal, hero_bal;
} mflFix_t;

/* Fill FIX in place so UnitBalance pointers stay live (Linux CI HeroDur). */
static void mfl_setup(mflFix_t *fix, cstring_t slk) {
	memset(fix, 0, sizeof(*fix));
	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
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
	fix->flare->unit_state->ranks[0] = MAKE(heroability_t, .code = BZ_AMFL, .level = 1);
	fix->flare->unit_state->ranks[1] = MAKE(heroability_t, .code = BZ_APSH, .level = 1);
	fix->enemy->unit_state->ranks[0] = MAKE(heroability_t, .code = BZ_AHTB, .level = 1);
	fix->ally->unit_state->ranks[0] = MAKE(heroability_t, .code = BZ_AHTB, .level = 1);
	fix->far_enemy->unit_state->ranks[0] = MAKE(heroability_t, .code = BZ_AHTB, .level = 1);
	fix->flare->mana.value = fix->flare->mana.max_value = 200;
	fix->enemy->mana.value = fix->enemy->mana.max_value = 100;
	fix->ally->mana.value = fix->ally->mana.max_value = 100;
	fix->far_enemy->mana.value = fix->far_enemy->mana.max_value = 100;
	fix->splash->mana.value = fix->splash->mana.max_value = 100;
	fix->flare->health.value = fix->flare->health.max_value = 450;
	fix->flare->unit_state->armor_value = 0;
	fix->enemy->health.value = fix->enemy->health.max_value = 500;
	fix->ally->health.value = fix->ally->health.max_value = 500;
	fix->far_enemy->health.value = fix->far_enemy->health.max_value = 500;
	fix->far_bait->health.value = fix->far_bait->health.max_value = 500;
	fix->splash->health.value = fix->splash->health.max_value = 500;
	fix->flare->stand = unit_stand; fix->enemy->stand = unit_stand;
}

static void mfl_done(mflFix_t *fix) {
	G_SetSLKRows("AbilityData", fix->old); free_slk_rows(fix->rows);
}

TEST(wc3_spell, mana_flare_registers_timed_status_procedure) {
	abilityitem_t item = S_AbilityItem(BZ_AMFL);
	T_EQ(item.ability->proc, CAbilityManaFlare);
	T_ASSERT(item.ability->flags & AB_SPELL);
	T_ASSERT(!(item.ability->flags & AB_CHANNEL));
	T_ASSERT(item.ability->flags & AB_UPDATE);
	T_EQ(item.ability->target_type, SPELL_TARGET_NONE);
	T_EQ(S_AbilityItem(BZ_APSH).ability->proc, CAbilityPhaseShift);
}

/* Activation applies timed Bmfl, spends Cost, locks orders, and adds authored DataE armor. */
TEST(wc3_spell, mana_flare_activates_status_and_armor) {
	mflFix_t fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_FEQ(fix.flare->mana.value, 175, 0.001f);
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	T_ASSERT(!G_GetChannel(fix.flare) || G_GetChannel(fix.flare)->code != BZ_AMFL);
	T_FEQ(G_UnitArmorValue(fix.flare), 9, 0.001f);
	mfl_done(&fix);
}

/* With every status slot occupied, attempting Mana Flare cannot charge its
 * mana cost or start its authored cooldown. A freed slot accepts the cast. */
TEST(wc3_spell, mana_flare_full_status_list_does_not_consume_resources) {
	mflFix_t fix;
	char buff[5] = "Z000";
	mfl_setup(&fix, MFL_SLK_WITH_COOL("5"));
	FOR_LOOP(i, MAX_UNIT_STATUSES) {
		buff[1] = (char)('a' + i);
		T_ASSERT(unit_addtimedstatus(fix.flare, buff, 1, 30.0f));
	}
	T_ASSERT(!S_ManaFlareActive(fix.flare));
	T_ASSERT(S_SpellCooldownReady(fix.flare, BZ_AMFL));
	T_ASSERT(!S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_FEQ(fix.flare->mana.value, 200.0f, 0.001f);
	T_ASSERT(S_SpellCooldownReady(fix.flare, BZ_AMFL));
	T_FEQ(S_SpellCooldownRemaining(fix.flare, BZ_AMFL), 0.0f, 0.001f);
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 0);

	unit_removestatus(fix.flare, fix.flare->unit_state->statuses, STATUS_REMOVE_SCRIPT);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	T_FEQ(fix.flare->mana.value, 175.0f, 0.001f);
	T_ASSERT(!S_SpellCooldownReady(fix.flare, BZ_AMFL));
	mfl_done(&fix);
}

/* An active toggle never requires another status slot to switch off. */
TEST(wc3_spell, mana_flare_toggle_off_works_at_status_capacity) {
	mflFix_t fix;
	char buff[5] = "Z000";
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	FOR_LOOP(i, MAX_UNIT_STATUSES - 1) {
		buff[1] = (char)('a' + i);
		T_ASSERT(unit_addtimedstatus(fix.flare, buff, 1, 30.0f));
	}
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 0);
	mfl_done(&fix);
}

/* Enemy cast inside Area: damage = min(DataC, cost * DataA) = min(70, 10*7) = 70. */
TEST(wc3_spell, mana_flare_damages_enemy_caster_from_authored_data) {
	mflFix_t fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f); /* 500 - 70 */
	mfl_done(&fix);
}

TEST(wc3_spell, mana_flare_ignores_out_of_area_and_friendly_casts) {
	mflFix_t fix;
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
	mflFix_t fix;
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

/* Movement and channel cleanup do not end Mana Flare; toggle-off and expiry do. */
TEST(wc3_spell, mana_flare_persists_through_movement_and_expires) {
	mflFix_t fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	S_SpellCancelChannel(fix.flare);
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	T_ASSERT(!S_ManaFlareAllowsOrder(fix.flare, "move"));
	T_ASSERT(S_ManaFlareAllowsOrder(fix.flare, "Amfl"));
	T_FEQ(G_UnitArmorValue(fix.flare), 9, 0.001f);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL)); /* toggle off */
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 0);
	fix.flare->mana.value = 200;
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	level.time += 30000; unit_updatestatuses(fix.flare);
	S_RunAbilityUpdates(fix.flare);
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 0);
	T_ASSERT(!G_GetChannel(fix.flare) || G_GetChannel(fix.flare)->code == 0);
	T_FEQ(G_UnitArmorValue(fix.flare), 0, 0.001f);
	mfl_done(&fix);
}

/* DataF splash hits a nearby mana-pool enemy within Rng of the primary victim. */
TEST(wc3_spell, mana_flare_splash_hits_mana_units_in_range) {
	mflFix_t fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f);
	T_FEQ(fix.splash->health.value, 430, 0.001f);
	mfl_done(&fix);
}

/* The first flare may occur at game time zero, without losing its interval gate. */
TEST(wc3_spell, mana_flare_time_zero_cooldown_and_status_identity) {
	mflFix_t fix;
	heroabilitystatus_t *status;
	mfl_setup(&fix, MFL_SLK);
	level.time = 0;
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	status = unit_findstatus(fix.flare, BZ_BMFL);
	T_NOT_NULL(status);
	T_EQ(status->source_ability, BZ_AMFL);
	T_EQ(status->data, 0);
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f);
	T_EQ(status->next_tick, 750);
	fix.enemy->mana.value = 100;
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f);
	level.time = 749;
	fix.enemy->mana.value = 100;
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f);
	level.time = 750;
	fix.enemy->mana.value = 100;
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 360, 0.001f);
	mfl_done(&fix);
}

/* Timed Mana Flare continues retaliating without a spell channel. */
TEST(wc3_spell, mana_flare_status_retaliates_without_channel) {
	mflFix_t fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f);
	mfl_done(&fix);
}

/* Phase Shift must remain unavailable while Mana Flare is active. */
TEST(wc3_spell, mana_flare_blocks_phase_shift_without_cancel) {
	mflFix_t fix;
	mfl_setup(&fix, MFL_SLK);
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(!S_CastNoTargetSpell(fix.flare, BZ_APSH));
	T_FEQ(fix.flare->mana.value, 175, 0.001f);
	T_ASSERT(!G_GetChannel(fix.flare) || G_GetChannel(fix.flare)->code != BZ_AMFL);
	T_EQ(G_UnitStatusLevel(fix.flare, BZ_BMFL), 1);
	T_FEQ(G_UnitArmorValue(fix.flare), 9, 0.001f);
	mfl_done(&fix);
}

/* Splash cannot harm mana-less units, even when they are eligible enemies. */
TEST(wc3_spell, mana_flare_splash_excludes_manaless_units) {
	mflFix_t fix;
	mfl_setup(&fix, MFL_SLK);
	fix.splash->mana.max_value = 0;
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(S_CastUnitTargetSpell(fix.enemy, BZ_AHTB, fix.flare));
	T_FEQ(fix.enemy->health.value, 430, 0.001f);
	T_FEQ(fix.splash->health.value, 500, 0.001f);
	mfl_done(&fix);
}


/* Disabling an active stance is not a second cast. In particular, it must
 * succeed before Cool expires and when current mana has fallen to zero. */
TEST(wc3_spell, mana_flare_direct_deactivation_ignores_mana_and_cooldown) {
	mflFix_t fix;
	float cooldown;
	mfl_setup(&fix, MFL_SLK_WITH_COOL("5"));
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(S_ManaFlareActive(fix.flare));
	cooldown = S_SpellCooldownRemaining(fix.flare, BZ_AMFL);
	T_ASSERT(cooldown > 0.0f);
	fix.flare->mana.value = 0.0f;
	T_ASSERT(S_CastNoTargetSpell(fix.flare, BZ_AMFL));
	T_ASSERT(!S_ManaFlareActive(fix.flare));
	T_FEQ(fix.flare->mana.value, 0.0f, 0.001f);
	T_FEQ(S_SpellCooldownRemaining(fix.flare, BZ_AMFL), cooldown, 0.001f);
	T_ASSERT(!S_CastNoTargetSpell(fix.flare, BZ_AMFL)); /* cannot reactivate */
	mfl_done(&fix);
}

/* Native IssueImmediateOrder and IssueImmediateOrderById both delegate to
 * unit_issueimmediateorder after resolving the stock Warcraft order IDs. */
TEST(wc3_spell, mana_flare_order_ids_and_idempotent_state_requests) {
	mflFix_t fix;
	ability_t const *on = FindAbilityByOrder("manaflareon");
	ability_t const *off = FindAbilityByOrder("manaflareoff");
	float cooldown;
	T_NOT_NULL(on);
	T_NOT_NULL(off);
	if (!on || !off) return;
	T_EQ(on->proc, CAbilityManaFlare);
	T_EQ(off->proc, CAbilityManaFlare);
	T_EQ(G_OrderId("manaflareon"), 852512u);
	T_EQ(G_OrderId("manaflareoff"), 852513u);
	T_STREQ(G_OrderId2String(852512), "manaflareon");
	T_STREQ(G_OrderId2String(852513), "manaflareoff");

	mfl_setup(&fix, MFL_SLK_WITH_COOL("5"));
	T_ASSERT(!unit_issueimmediateorder(fix.enemy, "manaflareon"));
	T_ASSERT(!unit_issueimmediateorder(fix.enemy, "manaflareoff"));
	T_ASSERT(unit_issueimmediateorder(fix.flare, "manaflareoff")); /* already off */
	T_FEQ(fix.flare->mana.value, 200.0f, 0.001f);
	T_ASSERT(S_SpellCooldownReady(fix.flare, BZ_AMFL));

	T_ASSERT(unit_issueimmediateorder(fix.flare, G_OrderId2String(852512)));
	T_ASSERT(S_ManaFlareActive(fix.flare));
	T_FEQ(fix.flare->mana.value, 175.0f, 0.001f);
	cooldown = S_SpellCooldownRemaining(fix.flare, BZ_AMFL);
	T_ASSERT(cooldown > 0.0f);
	T_ASSERT(unit_issueimmediateorder(fix.flare, "manaflareon")); /* already on */
	T_FEQ(fix.flare->mana.value, 175.0f, 0.001f);
	T_FEQ(S_SpellCooldownRemaining(fix.flare, BZ_AMFL), cooldown, 0.001f);

	fix.flare->mana.value = 0.0f;
	T_ASSERT(unit_issueimmediateorder(fix.flare, G_OrderId2String(852513)));
	T_ASSERT(!S_ManaFlareActive(fix.flare));
	T_ASSERT(unit_issueimmediateorder(fix.flare, "manaflareoff")); /* still off */
	T_FEQ(fix.flare->mana.value, 0.0f, 0.001f);
	T_FEQ(S_SpellCooldownRemaining(fix.flare, BZ_AMFL), cooldown, 0.001f);
	T_ASSERT(!unit_issueimmediateorder(fix.flare, "manaflareon")); /* still cooling down */
	T_ASSERT(!S_ManaFlareActive(fix.flare));
	mfl_done(&fix);
}

/* Command-issued activation must use the same capacity and resource preflight
 * as clicking the Mana Flare button; a rejected cast cannot spend resources. */
TEST(wc3_spell, mana_flare_issued_activation_checks_status_capacity) {
	mflFix_t fix;
	char buff[5] = "Z000";
	mfl_setup(&fix, MFL_SLK_WITH_COOL("5"));
	FOR_LOOP(i, MAX_UNIT_STATUSES) {
		buff[1] = (char)('a' + i);
		T_ASSERT(unit_addtimedstatus(fix.flare, buff, 1, 30.0f));
	}
	T_ASSERT(!unit_issueimmediateorder(fix.flare, "manaflareon"));
	T_ASSERT(!S_ManaFlareActive(fix.flare));
	T_FEQ(fix.flare->mana.value, 200.0f, 0.001f);
	T_ASSERT(S_SpellCooldownReady(fix.flare, BZ_AMFL));
	unit_removestatus(fix.flare, fix.flare->unit_state->statuses, STATUS_REMOVE_SCRIPT);
	T_ASSERT(unit_issueimmediateorder(fix.flare, "manaflareon"));
	T_ASSERT(S_ManaFlareActive(fix.flare));
	T_FEQ(fix.flare->mana.value, 175.0f, 0.001f);
	mfl_done(&fix);
}

#endif
