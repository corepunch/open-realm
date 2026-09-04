#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);

static const char slk_spell_data[] =
	"ID;PWXL;N;EBB;Y2;X11\n"
	"C;Y1;X1;K\"alias\"\n"
	"C;Y1;X2;K\"code\"\n"
	"C;Y1;X3;K\"targs\"\n"
	"C;Y1;X4;K\"Cost1\"\n"
	"C;Y1;X5;K\"Cool1\"\n"
	"C;Y1;X6;K\"Rng1\"\n"
	"C;Y1;X7;K\"Dur1\"\n"
	"C;Y1;X8;K\"HeroDur1\"\n"
	"C;Y1;X9;K\"DataA1\"\n"
	"C;Y1;X10;K\"DataB1\"\n"
	"C;Y1;X11;K\"Area1\"\n"
	"C;Y2;X1;K\"AHtb\"\n"
	"C;Y2;X2;K\"AHtb\"\n"
	"C;Y2;X3;K\"air,ground,enemy,neutral\"\n"
	"C;Y2;X4;K\"75\"\n"
	"C;Y2;X5;K\"9\"\n"
	"C;Y2;X6;K\"600\"\n"
	"C;Y2;X7;K\"5\"\n"
	"C;Y2;X8;K\"3\"\n"
	"C;Y2;X9;K\"100\"\n"
	"C;Y2;X10;K\"55\"\n"
	"E\n";

static LPEDICT make_hero(DWORD class_id, FLOAT hp, FLOAT mana, FLOAT x, FLOAT y) {
	reset_entities();
	setup_test_world();
	LPEDICT ent = alloc_test_unit(class_id, x, y);
	ent->health.value = hp;
	ent->health.max_value = hp;
	ent->mana.value = mana;
	ent->mana.max_value = mana;
	ent->svflags |= SVF_MONSTER;
	ent->stand = unit_stand;
	ent->movetype = MOVETYPE_NONE;
	unit_stand(ent);
	return ent;
}

TEST(wc3_spell, relationship_uses_passive_alliance_not_other_flags) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	caster->s.player = 0;
	target->s.player = 1;
	target->svflags |= SVF_MONSTER;
	target->health.value = target->health.max_value = 100;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

	memset(level.alliances, 0, sizeof(level.alliances));
	level.alliances[0][1] = 1 << ALLIANCE_SHARED_VISION;
	T_ASSERT(S_SpellIsEnemy(caster, target));
	T_ASSERT(!S_SpellIsFriend(caster, target));

	level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;
	T_ASSERT(!S_SpellIsEnemy(caster, target));
	T_ASSERT(S_SpellIsFriend(caster, target));

	target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
	T_ASSERT(S_SpellIsEnemy(caster, target));
	T_ASSERT(!S_SpellIsFriend(caster, target));
	target->s.player = PLAYER_NEUTRAL_PASSIVE;
	T_ASSERT(!S_SpellIsEnemy(caster, target));
	T_ASSERT(!S_SpellIsFriend(caster, target));
}

/* ---- Channel enforcement ---- */

TEST(wc3_spell, channel_cancel_stun) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	caster->channel.code = MAKEFOURCC('A','H','b','z');
	caster->channel.origin = caster->s.origin2;
	caster->stunned = true;
	spell_run_frame(caster);
	T_EQ((int)caster->channel.code, 0);
}

TEST(wc3_spell, channel_cancel_death) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	caster->channel.code = MAKEFOURCC('A','H','b','z');
	caster->channel.origin = caster->s.origin2;
	caster->health.value = 0;
	spell_run_frame(caster);
	T_EQ((int)caster->channel.code, 0);
}

TEST(wc3_spell, channel_cancel_movement) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	caster->channel.code = MAKEFOURCC('A','H','b','z');
	caster->channel.origin.x = 100; caster->channel.origin.y = 100;
	caster->s.origin.x = 101; caster->s.origin.y = 100;
	caster->s.origin2.x = 101; caster->s.origin2.y = 100;
	spell_run_frame(caster);
	T_EQ((int)caster->channel.code, 0);
}

TEST(wc3_spell, channel_persists_no_movement) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	caster->channel.code = MAKEFOURCC('A','H','b','z');
	caster->channel.origin = caster->s.origin2;
	spell_run_frame(caster);
	T_EQ((int)caster->channel.code, (int)MAKEFOURCC('A','H','b','z'));
}

TEST(wc3_spell, channel_cancel_clears_code) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	caster->channel.code = MAKEFOURCC('A','H','b','z');
	S_SpellCancelChannel(caster);
	T_EQ((int)caster->channel.code, 0);
	T_STREQ(caster->currentmove->animation, "stand");
}

TEST(wc3_spell, channel_cancel_noop_empty) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	caster->channel.code = 0;
	S_SpellCancelChannel(caster);
	T_EQ((int)caster->channel.code, 0);
}

TEST(wc3_spell, channel_run_frame_noop_when_idle) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	caster->channel.code = 0;
	caster->stunned = true;
	spell_run_frame(caster);
	T_EQ((int)caster->channel.code, 0);
}

/* ---- Toggle bypass ---- */

TEST(wc3_spell, toggle_immolation_no_mana_spend) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 200, 0, 0);
	level.time = 1000;
	caster->mana.value = 150;
	ability_t const *abil = FindAbilityByClassname("AEim");
	T_NOT_NULL(abil); T_NOT_NULL(abil->spell);
	spellTarget_t st = { .type = SPELL_TARGET_NONE };
	abil->spell->execute(caster, st, abil->spell);
	T_FEQ(caster->mana.value, 150, 0.01f);
	T_EQ((int)caster->abilstatus[0].code, (int)MAKEFOURCC('B','i','m','l'));
}

/* ---- Spell helpers ---- */

TEST(wc3_spell, spell_code_to_string) {
	char buf[8];
	S_SpellCodeString(MAKEFOURCC('A','H','t','b'), buf);
	T_STREQ(buf, "AHtb");
}

TEST(wc3_spell, generic_data_id_keeps_rawcode_view) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataB1\"\n"
		"C;Y2;X1;K\"Amil\"\n"
		"C;Y2;X2;K\"Amil\"\n"
		"C;Y2;X3;K\"hpea\"\n"
		"C;Y2;X4;K\"hmil\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	T_EQ((int)S_SpellDataId(MAKEFOURCC('A','m','i','l'), 1, 1), (int)MAKEFOURCC('h','p','e','a'));
	T_EQ((int)S_SpellDataId(MAKEFOURCC('A','m','i','l'), 1, 2), (int)MAKEFOURCC('h','m','i','l'));
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}


TEST(wc3_spell, militia_zero_pair_area_means_unbounded_search) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X3\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Area1\"\n"
		"C;Y2;X1;K\"Amil\"\n"
		"C;Y2;X2;K\"Amil\"\n"
		"C;Y2;X3;K\"0\"\n"
		"C;Y3;X1;K\"Amic\"\n"
		"C;Y3;X2;K\"Amic\"\n"
		"C;Y3;X3;K\"600\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);

	T_FEQ(S_MilitiaPairSearchRadius(MAKEFOURCC('A','m','i','l')), FLT_MAX, 1.0f);
	T_FEQ(S_MilitiaPairSearchRadius(MAKEFOURCC('A','m','i','c')), 600.0f, 0.01f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, spell_unit_id_from_slk) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X3\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"UnitID1\"\n"
		"C;Y2;X1;K\"AHwe\"\n"
		"C;Y2;X2;K\"AHwe\"\n"
		"C;Y2;X3;K\"hwat\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	T_EQ((int)S_SpellUnitId(MAKEFOURCC('A','H','w','e'), 1), (int)MAKEFOURCC('h','w','a','t'));
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, hero_duration_uses_herodur_col) {
	slkTestData_t *rows = parse_slk_string(slk_spell_data);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	T_FEQ(S_SpellDuration(MAKEFOURCC('A','H','t','b'), 1, true), 3.0f, 0.01f);
	T_FEQ(S_SpellDuration(MAKEFOURCC('A','H','t','b'), 1, false), 5.0f, 0.01f);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, spell_is_channeling_detects_active) {
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	T_ASSERT(!S_SpellIsChanneling(caster));
	caster->channel.code = MAKEFOURCC('A','H','b','z');
	T_ASSERT(S_SpellIsChanneling(caster));
	caster->channel.code = 0;
	T_ASSERT(!S_SpellIsChanneling(caster));
}

TEST(wc3_spell, mirror_image_immediate_order_spawns_summoned_illusion) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Cost1\"\n"
		"C;Y1;X4;K\"Cool1\"\n"
		"C;Y1;X5;K\"Dur1\"\n"
		"C;Y1;X6;K\"DataA1\"\n"
		"C;Y2;X1;K\"AOmi\"\n"
		"C;Y2;X2;K\"AOmi\"\n"
		"C;Y2;X3;K\"0\"\n"
		"C;Y2;X4;K\"0\"\n"
		"C;Y2;X5;K\"0\"\n"
		"C;Y2;X6;K\"1\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	LPEDICT image;
	DWORD before;

	caster->heroabilities[0].code = MAKEFOURCC('A','O','m','i');
	caster->heroabilities[0].level = 1;
	caster->hero.level = 3;
	memset(&level.events, 0, sizeof(level.events));
	before = globals.num_edicts;

	T_ASSERT(unit_issueimmediateorder(caster, "mirrorimage"));
	T_EQ((int)globals.num_edicts, (int)before + 1);
	image = &globals.edicts[before];
	T_ASSERT(image->inuse);
	T_ASSERT(image != caster);
	T_EQ((int)image->class_id, (int)caster->class_id);
	T_ASSERT(image->aiflags & AI_ILLUSION);
	T_EQ((int)image->hero.level, (int)caster->hero.level);
	T_ASSERT(image->hero.suspend_xp);
	T_EQ((int)level.events.queue[0].type, (int)EVENT_PLAYER_UNIT_SUMMON);
	T_ASSERT(level.events.queue[0].edict == caster);
	T_ASSERT(level.events.queue[0].source == image);
	T_EQ((int)level.events.queue[1].type, (int)EVENT_UNIT_SUMMON);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

/* ---- spell_info_t registration ---- */

TEST(wc3_spell, spell_info_attached_to_ability) {
	ability_t const *abil = FindAbilityByClassname("AHtb");
	T_NOT_NULL(abil);
	T_NOT_NULL(abil->spell);
	T_EQ((int)abil->spell->code, (int)MAKEFOURCC('A','H','t','b'));
	T_EQ((int)abil->spell->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, holy_light_rawcode_lookup_is_nul_safe) {
	DWORD code = MAKEFOURCC('A','H','h','b');
	spell_info_t const *spell = S_SpellInfoForCode(code);
	ability_t const *abil = FindAbilityForCommand(GetClassName(code));

	/* Runtime spell dispatch starts from a DWORD rawcode. This specifically
	 * guards against treating &code as a C string: that only worked when the
	 * unrelated byte after the four rawcode bytes happened to be zero. */
	T_NOT_NULL(abil);
	T_ASSERT(abil->cmd == spell_cmd);
	T_NOT_NULL(spell);
	T_EQ((int)spell->code, (int)code);
	T_EQ((int)spell->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, blizzard_is_channel) {
	ability_t const *abil = FindAbilityByClassname("AHbz");
	T_NOT_NULL(abil);
	T_NOT_NULL(abil->spell);
	T_ASSERT(abil->spell->flags & SPELL_CHANNEL);
	T_EQ((int)abil->spell->target_type, (int)SPELL_TARGET_POINT);
}

TEST(wc3_spell, cold_arrows_has_autocast_flag) {
	ability_t const *abil = FindAbilityByClassname("AHca");
	T_NOT_NULL(abil);
	T_NOT_NULL(abil->spell);
	T_ASSERT(abil->spell->flags & SPELL_AUTOCAST);
	T_ASSERT(abil->spell->flags & SPELL_TOGGLE);
}

TEST(wc3_spell, non_spell_ability_has_null_spell) {
	ability_t const *abil = FindAbilityByClassname(STR_CmdMove);
	T_NOT_NULL(abil);
	T_NULL(abil->spell);  /* move is not a spell */
}

#endif /* BZ_TESTS */
