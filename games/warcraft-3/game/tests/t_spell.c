#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);

static intptr_t test_ability_message(LPEDICT ent, abilityMsg_t msg, abilityitem_t const *item, spellTarget_t const *target) {
    abilityCall_t call = MAKE(abilityCall_t, .item = item, .target = target);
    return S_AbilityMessage(ent, msg, &call);
}

static intptr_t test_execute_code(LPEDICT ent, LPCSTR code, spellTarget_t target) {
    abilityitem_t item = S_AbilityItem(FS_SLKKey(code));
    return test_ability_message(ent, A_EXECUTE, &item, &target);
}

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

/* Two authored rawcodes share Holy Bolt's callbacks but must read their own effect and resource data. */
TEST(wc3_spell, shared_handler_uses_each_requested_rawcode) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\n"
        "C;Y3;X1;K\"A001\"\nC;Y3;X2;K\"AHhb\"\nC;Y3;X3;K\"air,ground,friend\"\n"
        "C;Y3;X4;K\"13\"\nC;Y3;X5;K\"7\"\nC;Y3;X6;K\"600\"\nC;Y3;X7;K\"37\"\n"
        "C;Y4;X1;K\"A002\"\nC;Y4;X2;K\"AHhb\"\nC;Y4;X3;K\"air,ground,friend\"\n"
        "C;Y4;X4;K\"23\"\nC;Y4;X5;K\"11\"\nC;Y4;X6;K\"600\"\nC;Y4;X7;K\"89\"\nE\n";
    DWORD first = MAKEFOURCC('A','0','0','1'), second = MAKEFOURCC('A','0','0','2');
    UnitAbilities_t abilities = { .abilList = "A001,A002" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100; target->health.max_value = 1000;
    T_ASSERT(FindAbilityForCommand("A001")->proc == CAbilityHolyBolt);
    T_ASSERT(FindAbilityForCommand("A002")->proc == CAbilityHolyBolt);
    T_ASSERT(S_CastUnitTargetSpell(caster, first, target));
    T_FEQ(target->health.value, 137, 0.001f);
    T_FEQ(caster->mana.value, 187, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, first));
    T_FEQ(S_SpellCooldownLength(caster, first), 7, 0.001f);
    S_SpellEndCooldown(caster, first); /* Existing policy groups cooldowns by base code. */
    InitAbilities();
    T_ASSERT(S_CastUnitTargetSpell(caster, second, target));
    T_FEQ(target->health.value, 226, 0.001f);
    T_FEQ(caster->mana.value, 164, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, second));
    T_FEQ(S_SpellCooldownLength(caster, second), 11, 0.001f);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Validation and deferred completion must use the issued alias as well as the effect callback. */
TEST(wc3_spell, custom_spells_keep_identity_in_validation_and_channel_completion) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"Rng1\"\n"
        "C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"Area1\"\n"
        "C;Y2;X1;K\"AEbl\"\nC;Y2;X2;K\"AEbl\"\nC;Y2;X3;K\"10\"\nC;Y2;X4;K\"600\"\n"
        "C;Y2;X5;K\"600\"\nC;Y2;X6;K\"0\"\n"
        "C;Y3;X1;K\"A003\"\nC;Y3;X2;K\"AEbl\"\nC;Y3;X3;K\"10\"\nC;Y3;X4;K\"600\"\n"
        "C;Y3;X5;K\"600\"\nC;Y3;X6;K\"100\"\n"
        "C;Y4;X1;K\"A004\"\nC;Y4;X2;K\"AHbz\"\nC;Y4;X3;K\"20\"\nC;Y4;X4;K\"600\"\n"
        "C;Y4;X5;K\"1\"\nC;Y4;X6;K\"10\"\nC;Y4;X7;K\"1\"\nC;Y4;X8;K\"80\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "A003,A004" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    VECTOR2 point = {50, 0};

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    T_ASSERT(!S_CastPointTargetSpell(caster, MAKEFOURCC('A','0','0','3'), &point));
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_FEQ(caster->s.origin2.x, 0, 0.001f);
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','0','0','4'), &point));
    T_FEQ(caster->mana.value, 180, 0.001f);
    T_EQ(caster->channel.code, 0); /* Its only wave has completed. */
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, registry_keeps_identity_outside_shared_handlers) {
    LPCSTR const fires[] = { "Afih", "Afin", "Afio", "Afir", "Afiu" };
    LPCSTR const abstract[] = { "abil", "AAin", "AAbt", "AAsp", "AAsm", "Amor", "ABon", "ATrn" };
    InitAbilities();
    T_EQ(FindAbilityByClassname(STR_CmdTrains)->proc, CAbilityTrain);
    T_EQ(GetAbilityByIndex(GetAbilityIndex(CAbilityTrain))->proc, CAbilityTrain);
    FOR_LOOP(i, sizeof(fires) / sizeof(*fires)) {
        DWORD code = FS_SLKKey(fires[i]);
        abilityitem_t item = S_AbilityItem(code);
        T_EQ(item.code, code);
        T_EQ(item.ability->proc, CAbilityOnFireHuman);
    }
    FOR_LOOP(i, sizeof(abstract) / sizeof(*abstract)) T_NULL(FindAbilityByClassname(abstract[i]));
    T_EQ(FindAbilityByClassname("AIco")->proc, CAbilityCharm);
    T_EQ(FindAbilityByClassname("Afbk")->proc, CAbilityPassive);
    T_ASSERT(!(FindAbilityByClassname("Afbk")->flags & AB_SPELL));
    T_EQ(FindAbilityByClassname("Abtl")->proc, CAbilityBattlestations);
    T_ASSERT(FindAbilityByClassname("Abtl")->flags & AB_COMMAND);
    FOR_LOOP(i, game.num_abilities) {
        ability_t const *abil = GetAbilityByIndex(i);
        T_NOT_NULL(abil);
        T_EQ(GetAbilityByIndex(GetAbilityIndex(abil->proc))->proc, abil->proc);
        if (abil->flags & AB_SPELL) {
            T_NOT_NULL(abil->proc); T_ASSERT(S_AbilityHasCommand(abil));
        }
    }
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
	G_SetPlayerAlliance(&game.clients[0].ps,
	                    &game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps,
	                    ALLIANCE_PASSIVE, true);
	T_ASSERT(!S_SpellIsEnemy(caster, target));
	T_ASSERT(S_SpellIsFriend(caster, target));

	target->s.player = PLAYER_NEUTRAL_PASSIVE;
	G_SetPlayerAlliance(&game.clients[0].ps,
	                    &game.clients[PLAYER_NEUTRAL_PASSIVE].ps,
	                    ALLIANCE_PASSIVE, true);
	T_ASSERT(!S_SpellIsEnemy(caster, target));
	T_ASSERT(S_SpellIsFriend(caster, target));
	G_SetPlayerAlliance(&game.clients[0].ps,
	                    &game.clients[PLAYER_NEUTRAL_PASSIVE].ps,
	                    ALLIANCE_PASSIVE, false);
	T_ASSERT(S_SpellIsEnemy(caster, target));
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
	abilityitem_t abil_item = S_AbilityItem(FS_SLKKey("AEim"));
	ability_t const *abil = abil_item.ability;
	T_NOT_NULL(abil); T_ASSERT(abil->flags & AB_SPELL);
	spellTarget_t st = { .type = SPELL_TARGET_NONE };
	test_ability_message(caster, A_EXECUTE, &abil_item, &st);
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

TEST(wc3_spell, hero_passives_use_authored_data_and_runtime_consumers) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y7;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y2;X1;K\"AHab\"\nC;Y2;X2;K\"AHab\"\nC;Y2;X3;K\"900\"\nC;Y2;X4;K\"0.75\"\n"
		"C;Y3;X1;K\"AUau\"\nC;Y3;X2;K\"AUau\"\nC;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\nC;Y3;X5;K\"0.5\"\n"
		"C;Y4;X1;K\"AUav\"\nC;Y4;X2;K\"AUav\"\nC;Y4;X3;K\"900\"\nC;Y4;X4;K\"0.2\"\n"
		"C;Y5;X1;K\"AOcr\"\nC;Y5;X2;K\"AOcr\"\nC;Y5;X4;K\"100\"\nC;Y5;X5;K\"2\"\n"
		"C;Y6;X1;K\"AEev\"\nC;Y6;X2;K\"AEev\"\nC;Y6;X4;K\"1\"\n"
		"C;Y7;X1;K\"AUts\"\nC;Y7;X2;K\"AUts\"\nC;Y7;X4;K\"0.15\"\nC;Y7;X5;K\"1\"\nC;Y7;X6;K\"3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT source = make_hero(MAKEFOURCC('H','a','m','g'), 500, 300, 0, 0);
	LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	level.time = 0;
	source->s.player = target->s.player = 0;
	source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','b'), .level = 1);
	source->heroabilities[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','u'), .level = 1);
	source->heroabilities[2] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','v'), .level = 1);
	T_FEQ(S_BrillianceManaRegen(target), 0.75f, 0.001f);
	T_FEQ(S_UnholyMoveBonus(target), 0.1f, 0.001f);
	T_FEQ(S_UnholyHealthRegen(target), 0.5f, 0.001f);
	T_FEQ(S_VampiricLifeSteal(target), 0.2f, 0.001f);
	level.time = AURA_UPDATE_MS / 2;
	target->s.origin2.x = 901.0f;
	T_FEQ(S_BrillianceManaRegen(target), 0.75f, 0.001f);
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_BrillianceManaRegen(target), 0.0f, 0.001f);
	T_FEQ(S_UnholyMoveBonus(target), 0.0f, 0.001f);

	target->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','r'), .level = 1);
	target->heroabilities[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','e','v'), .level = 1);
	target->heroabilities[2] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','t','s'), .level = 1);
	target->armor_value = 2.0f;
	T_EQ(S_CriticalStrikeDamage(target, 10), 20);
	T_ASSERT(S_EvasionRoll(target));
	T_FEQ(G_UnitArmorValue(target), 5.0f, 0.001f);
	T_FEQ(S_SpikedDamageReturn(target, 2.0f), 1.0f, 0.001f);
	T_FEQ(S_SpikedDamageReturn(target, 20.0f), 3.0f, 0.001f);
	memset(target->heroabilities, 0, sizeof(target->heroabilities));
	T_EQ(S_CriticalStrikeDamage(target, 10), 10);
	T_ASSERT(!S_EvasionRoll(target));
	T_FEQ(G_UnitArmorValue(target), 2.0f, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, regeneration_auras_use_alias_object_data_and_maximum_resources) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y6;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"levels\"\n"
		"C;Y2;X1;K\"ACnr\"\nC;Y2;X2;K\"Aoar\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
		"C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.01\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
		"C;Y3;X1;K\"ANre\"\nC;Y3;X2;K\"Aarm\"\nC;Y3;X3;K\"ground,friend,organic\"\n"
		"C;Y3;X4;K\"500\"\nC;Y3;X5;K\"0.02\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"1\"\n"
		"C;Y4;X1;K\"Aabr\"\nC;Y4;X2;K\"Aabr\"\nC;Y4;X3;K\"ground,friend,organic\"\n"
		"C;Y4;X4;K\"500\"\nC;Y4;X5;K\"3\"\nC;Y4;X6;K\"0\"\nC;Y4;X7;K\"1\"\n"
		"C;Y5;X1;K\"Aoar\"\nC;Y5;X2;K\"Aoar\"\n"
		"C;Y6;X1;K\"Aarm\"\nC;Y6;X2;K\"Aarm\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	reset_entities();
	setup_test_world();
	level.time = 0;
	LPEDICT health_source = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	LPEDICT mana_source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	LPEDICT blight_source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

	health_source->svflags |= SVF_MONSTER; mana_source->svflags |= SVF_MONSTER;
	blight_source->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	UnitAbilities_t static_abilities = { .abilList = "ACnr" };
	health_source->s.player = mana_source->s.player = blight_source->s.player = target->s.player = 0;
	health_source->targtype = mana_source->targtype = blight_source->targtype = target->targtype = TARG_GROUND;
	health_source->data.UnitAbilities = &static_abilities;
	mana_source->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','r','e'), .level = 1);
	blight_source->abilities.added[0] = MAKEFOURCC('A','a','b','r'); ARRAY_COUNT(blight_source->abilities.added) = 1;
	target->health.max_value = 1000.0f; target->health.value = 500.0f;
	target->mana.max_value = 400.0f; target->mana.value = 100.0f;

	T_FEQ(S_RegenerationHealthAura(target), 13.0f, 0.001f);
	T_FEQ(S_RegenerationManaAura(target), 8.0f, 0.001f);
	level.time = AURA_UPDATE_MS / 2;
	target->s.origin2.x = 501.0f;
	T_FEQ(S_RegenerationHealthAura(target), 13.0f, 0.001f);
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_RegenerationHealthAura(target), 0.0f, 0.001f);
	T_FEQ(S_RegenerationManaAura(target), 0.0f, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, regeneration_aura_filters_mechanical_targets_and_uses_strongest_family_member) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"levels\"\n"
		"C;Y2;X1;K\"ACn1\"\nC;Y2;X2;K\"Aoar\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
		"C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.01\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\n"
		"C;Y3;X1;K\"ACn2\"\nC;Y3;X2;K\"Aoar\"\nC;Y3;X3;K\"ground,friend,organic\"\n"
		"C;Y3;X4;K\"500\"\nC;Y3;X5;K\"0.02\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"1\"\n"
		"C;Y4;X1;K\"Aoar\"\nC;Y4;X2;K\"Aoar\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	reset_entities();
	setup_test_world();
	level.time = 0;
	LPEDICT first = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	LPEDICT second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

	first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	first->s.player = second->s.player = target->s.player = 0;
	first->targtype = second->targtype = target->targtype = TARG_GROUND;
	first->abilities.added[0] = MAKEFOURCC('A','C','n','1'); ARRAY_COUNT(first->abilities.added) = 1;
	second->abilities.added[0] = MAKEFOURCC('A','C','n','2'); ARRAY_COUNT(second->abilities.added) = 1;
	target->health.max_value = 1000.0f; target->health.value = 500.0f;

	T_FEQ(S_RegenerationHealthAura(target), 20.0f, 0.001f);
	target->targtype = TARG_MECHANICAL;
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_RegenerationHealthAura(target), 0.0f, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, regeneration_aura_target_art_persists_while_recipient_is_in_range) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
		"C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"levels\"\n"
		"C;Y2;X1;K\"ACnr\"\nC;Y2;X2;K\"Aoar\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
		"C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.01\"\nC;Y2;X6;K\"1\"\n"
		"C;Y2;X7;K\"Biml\"\nC;Y2;X8;K\"1\"\n"
		"C;Y3;X1;K\"Aoar\"\nC;Y3;X2;K\"Aoar\"\nC;Y3;X7;K\"Biml\"\nC;Y3;X8;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	reset_entities();
	setup_test_world();
	level.time = 0;
	LPEDICT source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	LPEDICT overlay = NULL;

	source->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	source->s.player = target->s.player = 0;
	source->targtype = target->targtype = TARG_GROUND;
	source->abilities.added[0] = MAKEFOURCC('A','C','n','r');
	ARRAY_COUNT(source->abilities.added) = 1;
	target->health.max_value = 1000.0f;
	target->health.value = 900.0f;

	S_UpdateRegenerationAuraEffects(target);
	FOR_LOOP(i, globals.num_edicts) {
		LPEDICT effect = g_edicts + i;
		if (effect->inuse && effect->owner == target && effect->goalentity == target &&
			effect->summon_ability == MAKEFOURCC('A','o','a','r')) {
			overlay = effect;
			break;
		}
	}
	T_NOT_NULL(overlay);
	T_ASSERT(overlay->s.model != 0);
	T_EQ(overlay->movetype, MOVETYPE_LINK);

	target->health.value = target->health.max_value;
	S_UpdateRegenerationAuraEffects(target);
	T_NULL(overlay->goalentity);

	target->health.value = 900.0f;
	S_UpdateRegenerationAuraEffects(target);
	overlay = NULL;
	FOR_LOOP(i, globals.num_edicts) {
		LPEDICT effect = g_edicts + i;
		if (effect->inuse && effect->owner == target && effect->goalentity == target &&
			effect->summon_ability == MAKEFOURCC('A','o','a','r')) {
			overlay = effect;
			break;
		}
	}
	T_NOT_NULL(overlay);
	T_NOT_NULL(overlay->goalentity);

	target->s.origin2.x = 501.0f;
	level.time = AURA_UPDATE_MS;
	S_UpdateRegenerationAuraEffects(target);
	T_ASSERT(overlay->goalentity == NULL);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, regeneration_aura_base_codes_are_registered_passives) {
	static LPCSTR const codes[] = { "Aoar", "Aabr", "Aarm" };
	FOR_LOOP(i, sizeof(codes) / sizeof(codes[0])) {
		ability_t const *ability = FindAbilityByClassname(codes[i]);
		T_NOT_NULL(ability);
		T_ASSERT(ability->flags & AB_PASSIVE);
	}
}

TEST(wc3_spell, thorns_aura_returns_authored_fraction_for_melee_hits) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\nC;Y1;X4;K\"DataA1\"\n"
		"C;Y2;X1;K\"AEah\"\nC;Y2;X2;K\"AEah\"\nC;Y2;X3;K\"900\"\nC;Y2;X4;K\"0.1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT aura = make_hero(MAKEFOURCC('E', 'd', 'r', 'u'), 500, 0, 0, 0);
	LPEDICT target = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 100, 0);
	LPEDICT attacker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 100, 0);
	level.time = 0;
	aura->s.player = target->s.player = attacker->s.player = 0;
	aura->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A', 'E', 'a', 'h'), .level = 1);
	target->attack1.weapon = WPN_NORMAL;
	attacker->attack1.weapon = WPN_NORMAL;
	T_FEQ(S_ThornsDamageReturn(target, target, 100.0f), 10.0f, 0.001f);
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 10.0f, 0.001f);
	attacker->attack1.weapon = WPN_MISSILE;
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 0.0f, 0.001f);
	attacker->attack1.weapon = WPN_NORMAL;
	target->s.origin2.x = 901.0f;
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 0.0f, 0.001f);
	ability_t const *ability = FindAbilityByClassname("AEah");
	T_NOT_NULL(ability);
	T_ASSERT(ability->flags & AB_PASSIVE);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, requested_thirty_have_concrete_handlers) {
	static LPCSTR const rawcodes[] = {
		"AHab", "AHmt", "ANst", "ANsg", "ANsq", "ANsw", "AOww", "AOcr", "AHbn", "AHfs",
		"AHdr", "AHpx", "AUcb", "AUim", "AUls", "AUts", "ANba", "ANsi", "AUan", "AUdc",
		"AUdp", "AUau", "AEev", "AEme", "AUsl", "AUav", "AUin", "AOcl", "AOeq", "AOfs",
	};
	static DWORD const passives[] = {
		MAKEFOURCC('A','H','a','b'), MAKEFOURCC('A','O','c','r'), MAKEFOURCC('A','U','t','s'),
		MAKEFOURCC('A','U','a','u'), MAKEFOURCC('A','E','e','v'), MAKEFOURCC('A','U','a','v'),
	};

	FOR_LOOP(i, sizeof(rawcodes) / sizeof(rawcodes[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(rawcodes[i]));
		ability_t const *ability = ability_item.ability;
		DWORD code = MAKEFOURCC(rawcodes[i][0], rawcodes[i][1], rawcodes[i][2], rawcodes[i][3]);
		BOOL passive = false;
		FOR_LOOP(j, sizeof(passives) / sizeof(passives[0])) passive |= code == passives[j];
		T_NOT_NULL(ability);
		if (passive) {
			T_ASSERT(ability->flags & AB_PASSIVE);
		} else {
			T_ASSERT(S_AbilityHasCommand(ability));
			T_ASSERT(ability->flags & AB_SPELL);
			T_NOT_NULL(ability->proc);
			T_EQ((int)ability_item.code, (int)code);
		}
	}
}

TEST(wc3_spell, campaign_ability_rawcodes_are_registered_explicitly) {
	static LPCSTR const rawcodes[] = {
		"Aamk", "ACtn", "ANav", "ANsh", "AOw2", "ACs7", "ACs8", "ANr2", "Afbb", "Andm",
		"Asb1", "Asb2", "Asb3", "ANha", "ANen", "ACfu", "ANpa", "Acny", "Ahnl", "Arsq",
		"Arsg", "Arsp", "ANbr", "ANsb", "ANcf", "Acdh", "Acef", "ANhw", "ANhx", "Arsw",
		"AOs2", "AOr2", "AOr3", "AOls",
	};

	FOR_LOOP(i, sizeof(rawcodes) / sizeof(rawcodes[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(rawcodes[i]));
		ability_t const *ability = ability_item.ability;
		DWORD code = MAKEFOURCC(rawcodes[i][0], rawcodes[i][1], rawcodes[i][2], rawcodes[i][3]);
		T_NOT_NULL(ability);
		if (!strcmp(rawcodes[i], "ANha")) T_EQ(ability->proc, CAbilityHarvest);
		else {
			T_ASSERT(S_AbilityHasCommand(ability));
			T_ASSERT(ability->flags & AB_SPELL);
			T_NOT_NULL(ability->proc);
			T_EQ((int)ability_item.code, (int)code);
		}
	}
}

TEST(wc3_spell, requested_active_callback_families_change_simulation) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y6;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\nC;Y1;X4;K\"Dur1\"\n"
		"C;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataC1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"AHbn\"\nC;Y2;X2;K\"AHbn\"\nC;Y2;X4;K\"12\"\nC;Y2;X5;K\"4\"\nC;Y2;X9;K\"BHbn\"\n"
		"C;Y3;X1;K\"ANsi\"\nC;Y3;X2;K\"ANsi\"\nC;Y3;X3;K\"200\"\nC;Y3;X4;K\"16\"\nC;Y3;X5;K\"8\"\nC;Y3;X9;K\"BNsi\"\n"
		"C;Y4;X1;K\"AUsl\"\nC;Y4;X2;K\"AUsl\"\nC;Y4;X4;K\"15\"\nC;Y4;X5;K\"4\"\nC;Y4;X9;K\"BUsl\"\n"
		"C;Y5;X1;K\"AUdp\"\nC;Y5;X2;K\"AUdp\"\nC;Y5;X7;K\"2\"\n"
		"C;Y6;X1;K\"AOcl\"\nC;Y6;X2;K\"AOcl\"\nC;Y6;X3;K\"500\"\nC;Y6;X6;K\"100\"\nC;Y6;X7;K\"3\"\nC;Y6;X8;K\"0.5\"\nE\n";
	LPEDICT caster = make_hero(MAKEFOURCC('u','d','e','a'), 200, 300, 0, 0);
	LPEDICT first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	LPEDICT second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	LPEDICT third = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 700, 0);
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	caster->s.player = 0; caster->health.max_value = 500; caster->health.value = 100;
	first->s.player = second->s.player = third->s.player = 1;
	first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; third->svflags |= SVF_MONSTER;
	first->die = unit_die; second->die = unit_die; third->die = unit_die;
	first->health.value = first->health.max_value = 500;
	second->health.value = second->health.max_value = 500;
	third->health.value = third->health.max_value = 500;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

	test_execute_code(caster, "AHbn", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
	T_ASSERT(S_UnitHasStatus(first, MAKEFOURCC('B','H','b','n')));
	test_execute_code(caster, "ANsi", MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = first->s.origin2));
	T_ASSERT(S_UnitHasStatus(first, MAKEFOURCC('B','N','s','i')));
	T_ASSERT(S_UnitHasStatus(second, MAKEFOURCC('B','N','s','i')));
	T_ASSERT(!S_UnitHasStatus(third, MAKEFOURCC('B','N','s','i')));

	test_execute_code(caster, "AUsl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
	T_ASSERT(S_UnitHasStatus(first, MAKEFOURCC('B','U','s','l')));
	T_Damage(first, caster, 1);
	T_ASSERT(!S_UnitHasStatus(first, MAKEFOURCC('B','U','s','l')));

	test_execute_code(caster, "AUdp", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = third));
	T_FEQ(caster->health.value, 500.0f, 0.001f);
	T_ASSERT(M_IsDead(third));

	test_execute_code(caster, "AOcl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
	T_FEQ(first->health.value, 399.0f, 0.001f);
	T_FEQ(second->health.value, 450.0f, 0.001f);

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
	T_EQ((int)level.events.queue[2].type, (int)EVENT_PLAYER_UNIT_SUMMON);
	T_ASSERT(level.events.queue[2].edict == caster);
	T_ASSERT(level.events.queue[2].source == image);
	T_EQ((int)level.events.queue[3].type, (int)EVENT_UNIT_SUMMON);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_replenishes_life_then_mana_using_authored_ratios) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"2.0\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    LPEDICT well = make_hero(MAKEFOURCC('h','b','a','r'), 100, 50, 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    well->s.player = target->s.player = 0;
    target->health.max_value = 100.0f; target->health.value = 80.0f;
    target->mana.max_value = 100.0f; target->mana.value = 50.0f;
    T_NOT_NULL(item.ability);
    T_ASSERT(test_ability_message(well, A_VALIDATE, &item, &st));
    T_ASSERT(test_ability_message(well, A_EXECUTE, &item, &st));
    T_FEQ(target->health.value, 100.0f, 0.001f);
    T_FEQ(target->mana.value, 70.0f, 0.001f);
    T_FEQ(well->mana.value, 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_accepts_mana_only_replenishment) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"2.0\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    LPEDICT well = make_hero(MAKEFOURCC('h','b','a','r'), 100, 10, 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    well->s.player = target->s.player = 0;
    target->health.max_value = target->health.value = 100.0f;
    target->mana.max_value = 100.0f; target->mana.value = 50.0f;
    T_ASSERT(test_ability_message(well, A_VALIDATE, &item, &st));
    T_ASSERT(test_ability_message(well, A_EXECUTE, &item, &st));
    T_FEQ(target->health.value, 100.0f, 0.001f);
    T_FEQ(target->mana.value, 70.0f, 0.001f);
    T_FEQ(well->mana.value, 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* ---- ability_t registration ---- */

TEST(wc3_spell, spell_fields_belong_to_ability) {
	abilityitem_t abil_item = S_AbilityItem(FS_SLKKey("AHtb"));
	ability_t const *abil = abil_item.ability;
	T_NOT_NULL(abil);
	T_ASSERT(abil->flags & AB_SPELL);
	T_EQ((int)abil_item.code, (int)MAKEFOURCC('A','H','t','b'));
	T_EQ((int)abil->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, forked_lightning_bounces_without_damage_decay) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"Dur1\"\n"
		"C;Y2;X1;K\"ANfl\"\nC;Y2;X2;K\"ANfl\"\nC;Y2;X3;K\"85\"\n"
		"C;Y2;X4;K\"3\"\nC;Y2;X5;K\"125\"\nC;Y2;X6;K\"0.7\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	LPEDICT first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	LPEDICT second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
	LPEDICT third = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 300, 0);
	LPEDICT fourth = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 400, 0);
	abilityitem_t spell_item = S_AbilityItem(FS_SLKKey("ANfl"));
	spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = first };

	caster->s.player = 0;
	caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','f','l'), .level = 1);
	first->s.player = second->s.player = third->s.player = fourth->s.player = 1;
	    first->svflags |= SVF_MONSTER;
	    second->svflags |= SVF_MONSTER;
	    third->svflags |= SVF_MONSTER;
	    fourth->svflags |= SVF_MONSTER;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	first->health.value = second->health.value = third->health.value = fourth->health.value = 100;
	test_ability_message(caster, A_EXECUTE, &spell_item, &st);
	T_FEQ(first->health.value, 15.0f, 0.01f);
	T_FEQ(second->health.value, 15.0f, 0.01f);
	T_FEQ(third->health.value, 15.0f, 0.01f);
	T_FEQ(fourth->health.value, 100.0f, 0.01f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, first_new_ability_handlers_are_real_spells) {
	abilityitem_t force_item = S_AbilityItem(FS_SLKKey("AEfn"));
	ability_t const *force = force_item.ability;
	abilityitem_t starfall_item = S_AbilityItem(FS_SLKKey("AEsf"));
	ability_t const *starfall = starfall_item.ability;
	abilityitem_t shockwave_item = S_AbilityItem(FS_SLKKey("AOsh"));
	ability_t const *shockwave = shockwave_item.ability;
	abilityitem_t rain_of_fire_item = S_AbilityItem(FS_SLKKey("ANrf"));
	ability_t const *rain_of_fire = rain_of_fire_item.ability;
	abilityitem_t tranquility_item = S_AbilityItem(FS_SLKKey("AEtq"));
	ability_t const *tranquility = tranquility_item.ability;
	abilityitem_t dark_ritual_item = S_AbilityItem(FS_SLKKey("AUdr"));
	ability_t const *dark_ritual = dark_ritual_item.ability;
	abilityitem_t frost_armor_item = S_AbilityItem(FS_SLKKey("AUfa"));
	ability_t const *frost_armor = frost_armor_item.ability;
	abilityitem_t frost_armor_variant_item = S_AbilityItem(FS_SLKKey("AUfu"));
	ability_t const *frost_armor_variant = frost_armor_variant_item.ability;
	abilityitem_t divine_shield_item = S_AbilityItem(FS_SLKKey("AHds"));
	ability_t const *divine_shield = divine_shield_item.ability;
	abilityitem_t death_and_decay_item = S_AbilityItem(FS_SLKKey("AUdd"));
	ability_t const *death_and_decay = death_and_decay_item.ability;
	abilityitem_t frost_nova_item = S_AbilityItem(FS_SLKKey("AUfn"));
	ability_t const *frost_nova = frost_nova_item.ability;
	abilityitem_t thunder_clap_item = S_AbilityItem(FS_SLKKey("AHtc"));
	ability_t const *thunder_clap = thunder_clap_item.ability;

	T_NOT_NULL(force);
	T_ASSERT(force->flags & AB_SPELL);
	T_EQ((int)force_item.code, (int)MAKEFOURCC('A', 'E', 'f', 'n'));
	T_NOT_NULL(starfall);
	T_ASSERT(starfall->flags & AB_SPELL);
	T_EQ((int)starfall_item.code, (int)MAKEFOURCC('A', 'E', 's', 'f'));
	T_ASSERT(starfall->flags & AB_CHANNEL);
	T_NOT_NULL(shockwave);
	T_ASSERT(shockwave->flags & AB_SPELL);
	T_EQ((int)shockwave_item.code, (int)MAKEFOURCC('A', 'O', 's', 'h'));
	T_NOT_NULL(rain_of_fire);
	T_ASSERT(rain_of_fire->flags & AB_SPELL);
	T_EQ((int)rain_of_fire_item.code, (int)MAKEFOURCC('A', 'N', 'r', 'f'));
	T_ASSERT(rain_of_fire->flags & AB_CHANNEL);
	T_NOT_NULL(tranquility);
	T_ASSERT(tranquility->flags & AB_SPELL);
	T_EQ((int)tranquility_item.code, (int)MAKEFOURCC('A', 'E', 't', 'q'));
	T_ASSERT(tranquility->flags & AB_CHANNEL);
	T_NOT_NULL(dark_ritual);
	T_ASSERT(dark_ritual->flags & AB_SPELL);
	T_EQ((int)dark_ritual_item.code, (int)MAKEFOURCC('A', 'U', 'd', 'r'));
	T_EQ((int)dark_ritual->target_type, (int)SPELL_TARGET_UNIT);
	T_NOT_NULL(frost_armor);
	T_ASSERT(frost_armor->flags & AB_SPELL);
	T_EQ((int)frost_armor_item.code, (int)MAKEFOURCC('A', 'U', 'f', 'a'));
	T_NOT_NULL(frost_armor_variant);
	T_ASSERT(frost_armor_variant->flags & AB_SPELL);
	T_EQ((int)frost_armor_variant_item.code, (int)MAKEFOURCC('A', 'U', 'f', 'u'));
	T_NOT_NULL(divine_shield);
	T_ASSERT(divine_shield->flags & AB_SPELL);
	T_EQ((int)divine_shield_item.code, (int)MAKEFOURCC('A', 'H', 'd', 's'));
	T_EQ((int)divine_shield->target_type, (int)SPELL_TARGET_NONE);
	T_NOT_NULL(death_and_decay);
	T_ASSERT(death_and_decay->flags & AB_SPELL);
	T_EQ((int)death_and_decay_item.code, (int)MAKEFOURCC('A', 'U', 'd', 'd'));
	T_ASSERT(death_and_decay->flags & AB_CHANNEL);
	T_NOT_NULL(frost_nova);
	T_ASSERT(frost_nova->flags & AB_SPELL);
	T_EQ((int)frost_nova_item.code, (int)MAKEFOURCC('A', 'U', 'f', 'n'));
	T_NOT_NULL(thunder_clap);
	T_ASSERT(thunder_clap->flags & AB_SPELL);
	T_EQ((int)thunder_clap_item.code, (int)MAKEFOURCC('A', 'H', 't', 'c'));
}

TEST(wc3_spell, tornado_uses_whirlwind_channel_handler) {
	abilityitem_t tornado_item = S_AbilityItem(FS_SLKKey("ANto"));
	ability_t const *tornado = tornado_item.ability;

	T_NOT_NULL(tornado);
	T_ASSERT(tornado->flags & AB_SPELL);
	T_NOT_NULL(tornado->proc);
	T_EQ((int)tornado_item.code, (int)MAKEFOURCC('A', 'N', 't', 'o'));
	T_EQ((int)tornado->target_type, (int)SPELL_TARGET_NONE);
	T_ASSERT(tornado->flags & AB_CHANNEL);
}

TEST(wc3_spell, requested_neutral_hero_abilities_have_contracts) {
	ability_t const *mana_shield = FindAbilityByClassname("ANms");
	ability_t const *revive = FindAbilityByClassname("AHre");
	ability_t const *breath = FindAbilityByClassname("ANbf");
	ability_t const *brawler = FindAbilityByClassname("ANdb");
	ability_t const *haze = FindAbilityByClassname("ANdh");
	ability_t const *doom = FindAbilityByClassname("ANdo");
	ability_t const *howl = FindAbilityByClassname("ANht");
	ability_t const *cleave = FindAbilityByClassname("ANca");

	T_NOT_NULL(mana_shield);
	T_NOT_NULL(revive);
	T_NOT_NULL(breath);
	T_NOT_NULL(brawler);
	T_NOT_NULL(haze);
	T_NOT_NULL(doom);
	T_NOT_NULL(howl);
	T_NOT_NULL(cleave);
	T_ASSERT(mana_shield->flags & AB_PASSIVE);
	T_ASSERT(brawler->flags & AB_PASSIVE);
	T_ASSERT(cleave->flags & AB_PASSIVE);
	T_EQ((int)revive->target_type, (int)SPELL_TARGET_NONE);
	T_EQ((int)breath->target_type, (int)SPELL_TARGET_POINT);
	T_EQ((int)haze->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)doom->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)howl->target_type, (int)SPELL_TARGET_NONE);
}

TEST(wc3_spell, selected_hero_ability_contracts_are_registered) {
	ability_t const *searing = FindAbilityByClassname("AHfa");
	ability_t const *trueshot = FindAbilityByClassname("AEar");
	ability_t const *reincarnation = FindAbilityByClassname("AOre");
	ability_t const *wave = FindAbilityByClassname("AOhw");
	ability_t const *hex = FindAbilityByClassname("AOhx");
	ability_t const *voodoo = FindAbilityByClassname("AOvd");
	ability_t const *vengeance = FindAbilityByClassname("AEsv");
	ability_t const *acid = FindAbilityByClassname("ANab");

	T_NOT_NULL(searing); T_NOT_NULL(trueshot); T_NOT_NULL(reincarnation); T_NOT_NULL(wave);
	T_NOT_NULL(hex); T_NOT_NULL(voodoo); T_NOT_NULL(vengeance); T_NOT_NULL(acid);
	T_ASSERT(searing->flags & AB_TOGGLE);
	T_ASSERT(trueshot->flags & AB_PASSIVE);
	T_ASSERT(reincarnation->flags & AB_PASSIVE);
	T_EQ((int)wave->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)hex->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)voodoo->target_type, (int)SPELL_TARGET_NONE);
	T_EQ((int)vengeance->target_type, (int)SPELL_TARGET_NONE);
	T_EQ((int)acid->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, human_ability_rawcodes_have_concrete_contracts) {
	static LPCSTR const spells[] = {
		"Amls", "Acmg", "Amdf", "Asps", "Aclf", "Adef", "Afla", "Ainf", "Adis", "Ahea", "Aslo", "Aivs", "Aply", "AHav",
	};
	static LPCSTR const passives[] = {
		"Afbk", "Aflk", "Afsh", "Aroc", "Asph", "Aphx", "Agyb", "Asth", "Agyv", "Adts",
	};

	FOR_LOOP(i, sizeof(spells) / sizeof(spells[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(spells[i]));
		ability_t const *ability = ability_item.ability;
		T_NOT_NULL(ability); T_ASSERT(S_AbilityHasCommand(ability));
		T_ASSERT(ability->flags & AB_SPELL); T_NOT_NULL(ability->proc);
		T_EQ((int)ability_item.code, (int)MAKEFOURCC(spells[i][0], spells[i][1], spells[i][2], spells[i][3]));
	}
	FOR_LOOP(i, sizeof(passives) / sizeof(passives[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(passives[i]));
		ability_t const *ability = ability_item.ability;
		T_NOT_NULL(ability); T_ASSERT(ability->flags & AB_PASSIVE);
	}
}

TEST(wc3_spell, human_support_spells_use_authored_status_and_heal_values) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Rng1\"\n"
		"C;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\nC;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Ahea\"\nC;Y2;X2;K\"Ahea\"\nC;Y2;X3;K\"ground,friend\"\nC;Y2;X4;K\"250\"\nC;Y2;X7;K\"25\"\nC;Y2;X9;K\"Bhea\"\n"
		"C;Y3;X1;K\"Ainf\"\nC;Y3;X2;K\"Ainf\"\nC;Y3;X3;K\"ground,friend\"\nC;Y3;X4;K\"500\"\nC;Y3;X5;K\"60\"\nC;Y3;X6;K\"60\"\nC;Y3;X7;K\"0.1\"\nC;Y3;X8;K\"5\"\nC;Y3;X9;K\"Binf\"\n"
		"C;Y4;X1;K\"Aslo\"\nC;Y4;X2;K\"Aslo\"\nC;Y4;X3;K\"ground,enemy\"\nC;Y4;X4;K\"700\"\nC;Y4;X5;K\"60\"\nC;Y4;X6;K\"10\"\nC;Y4;X7;K\"0.6\"\nC;Y4;X8;K\"0.25\"\nC;Y4;X9;K\"Bslo\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	LPEDICT ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	LPEDICT enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->health.value = 60; ally->health.max_value = 100; ally->armor_value = 2;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	test_execute_code(caster, "Ahea", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_FEQ(ally->health.value, 85.0f, 0.001f);
	test_execute_code(caster, "Ainf", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','i','n','f'))); T_FEQ(G_UnitArmorValue(ally), 7.0f, 0.001f);
	test_execute_code(caster, "Aslo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','s','l','o'))); T_FEQ(S_HumanMoveFactor(enemy), 0.4f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, human_attack_passives_and_defend_change_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\nC;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Afbk\"\nC;Y2;X2;K\"Afbk\"\nC;Y2;X3;K\"20\"\nC;Y2;X4;K\"1\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"1\"\n"
		"C;Y3;X1;K\"Adef\"\nC;Y3;X2;K\"Adef\"\nC;Y3;X3;K\"0.5\"\nC;Y3;X4;K\"1\"\nC;Y3;X5;K\"0.3\"\nC;Y3;X8;K\"0\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 100, 0, 0);
	LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	attacker->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','f','b','k'), .level = 1);
	target->mana.value = target->mana.max_value = 50; attacker->attack1.type = ATK_NORMAL;
	T_EQ(S_HumanAttackDamage(attacker, target, 10), 30); T_FEQ(target->mana.value, 30.0f, 0.001f);
	unit_addstatus(target, "Adef", 1); attacker->attack1.type = ATK_PIERCE;
	T_EQ(S_HumanAttackDamage(attacker, target, 100), 60);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_fractional_chance_can_guarantee_reflection) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"1\"\nC;Y2;X5;K\"0.3\"\nC;Y2;X8;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 100, 0, 0);
	LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	attacker->health.value = 200; attacker->attack1.type = ATK_PIERCE; unit_addstatus(target, "Adef", 1);
	T_EQ(S_HumanAttackDamage(attacker, target, 100), 0); T_FEQ(attacker->health.value, 100.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, selected_common_ability_contracts_are_registered) {
	static LPCSTR const passives[] = {
		"Abdt", "Arev", "Aawa", "Adet", "AHer", "Aalr", "Afih", "Afin", "Afio", "Afir", "Afiu", "Aloc", "Attu",
	};
	static struct { LPCSTR code; abilityProc_t proc; } const commands[] = {
		{ "AEbu", CAbilityBuild }, { "AGbu", CAbilityBuild }, { "AHbu", CAbilityBuild }, { "ANbu", CAbilityBuild },
		{ "AObu", CAbilityBuild }, { "ARal", CAbilityRally }, { "AUbu", CAbilityBuild }, { "Aatk", CAbilityAttack },
		{ "Amov", CAbilityMove }, { "Atdp", CAbilityCargoDrop }, { "Atlp", CAbilityCargoLoad },
	};
	abilityitem_t poison_item = S_AbilityItem(FS_SLKKey("AEpa"));
	ability_t const *poison = poison_item.ability;

	FOR_LOOP(i, sizeof(passives) / sizeof(passives[0])) {
		ability_t const *ability = FindAbilityByClassname(passives[i]);
		T_NOT_NULL(ability);
		T_ASSERT(ability->flags & AB_PASSIVE);
	}
	FOR_LOOP(i, sizeof(commands) / sizeof(commands[0]))
		T_EQ(FindAbilityByClassname(commands[i].code)->proc, commands[i].proc);
	T_EQ(FindAbilityByClassname("Afih")->proc, CAbilityOnFireHuman);
	T_EQ(FindAbilityByClassname("Afin")->proc, CAbilityOnFireHuman);
	T_EQ(FindAbilityByClassname("Afio")->proc, CAbilityOnFireHuman);
	T_EQ(FindAbilityByClassname("Afir")->proc, CAbilityOnFireHuman);
	T_EQ(FindAbilityByClassname("Afiu")->proc, CAbilityOnFireHuman);
	T_NOT_NULL(poison);
	T_ASSERT(poison->flags & AB_SPELL);
	T_EQ((int)poison_item.code, (int)MAKEFOURCC('A', 'E', 'p', 'a'));
	T_ASSERT(poison->flags & AB_TOGGLE);
	T_ASSERT(poison->flags & AB_AUTOCAST);
}

TEST(wc3_spell, intrinsic_on_fire_level_zero_clears_effect) {
	edict_t building = {0};

	building.inuse = true;
	building.s.effect = 77;
	building.s.effect_flags = EFX_MODEL;
	building.s.flags = EF_BUILDING;
	building.health.value = building.health.max_value = 1000.0f;
	S_RefreshAbilityLevel(&building, FindAbilityByClassname("Afih"));
	T_EQ(building.s.effect, 0);
	T_EQ(building.s.effect_flags, 0);
}

TEST(wc3_spell, runtime_ability_membership_calls_enable_and_disable) {
	UnitAbilities_t abilities = { .abilList = "Afir" };
	edict_t unit = { .inuse = true, .s.effect = 77, .s.effect_flags = EFX_MODEL };
	DWORD const code = MAKEFOURCC('A', 'f', 'i', 'r');

	unit.data.UnitAbilities = &abilities;
	T_ASSERT(G_ActorRemoveSkill(&unit, code));
	T_EQ(unit.s.effect, 0);
	T_EQ(unit.s.effect_flags, 0);
	unit.s.effect = 77; unit.s.effect_flags = EFX_MODEL;
	T_ASSERT(G_ActorAddSkill(&unit, code));
	T_EQ(unit.s.effect, 0);
	T_EQ(unit.s.effect_flags, 0);
}

TEST(wc3_spell, poison_arrows_uses_its_own_authored_bonus_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X3\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y2;X1;K\"AEpa\"\nC;Y2;X2;K\"AEpa\"\nC;Y2;X3;K\"13\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT attacker = make_hero(MAKEFOURCC('N', 'n', 's', 'w'), 100, 100, 0, 0);

	attacker->attack1.weapon = WPN_MISSILE;
	unit_addstatus(attacker, "AEpa", 1);
	T_EQ(S_SearingArrowDamage(attacker, 20), 33);
	attacker->attack1.weapon = WPN_NORMAL;
	T_EQ(S_SearingArrowDamage(attacker, 20), 20);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, mana_shield_consumes_authored_mana_before_life) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Area1\"\n"
		"C;Y2;X1;K\"ANms\"\nC;Y2;X2;K\"ANms\"\nC;Y2;X3;K\"2\"\nC;Y2;X4;K\"128\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT caster = make_hero(MAKEFOURCC('h', 'p', 'e', 'a'), 100, 10, 0, 0);
	caster->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A', 'N', 'm', 's'), .level = 1);
	T_EQ(S_ManaShieldDamage(caster, 4), 0);
	T_FEQ(caster->mana.value, 2.0f, 0.01f);
	T_EQ(S_ManaShieldDamage(caster, 4), 3);
	T_FEQ(caster->mana.value, 0.0f, 0.01f);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, beastmaster_summons_use_force_of_nature_contract) {
	abilityitem_t bear_item = S_AbilityItem(FS_SLKKey("ANsg"));
	ability_t const *bear = bear_item.ability;
	abilityitem_t quilbeast_item = S_AbilityItem(FS_SLKKey("ANsq"));
	ability_t const *quilbeast = quilbeast_item.ability;
	abilityitem_t hawk_item = S_AbilityItem(FS_SLKKey("ANsw"));
	ability_t const *hawk = hawk_item.ability;

	T_EQ(bear->proc, CAbilitySummonGrizzly);
	T_EQ(quilbeast->proc, CAbilitySummonQuillbeast);
	T_EQ(hawk->proc, CAbilitySummonWarEagle);
	T_EQ((int)bear_item.code, (int)MAKEFOURCC('A', 'N', 's', 'g'));
	T_EQ((int)quilbeast_item.code, (int)MAKEFOURCC('A', 'N', 's', 'q'));
	T_EQ((int)hawk_item.code, (int)MAKEFOURCC('A', 'N', 's', 'w'));
	T_ASSERT((bear->flags & AB_SPELL) && (quilbeast->flags & AB_SPELL) && (hawk->flags & AB_SPELL));
}

TEST(wc3_spell, entangling_roots_is_a_timed_unit_spell) {
	abilityitem_t roots_item = S_AbilityItem(FS_SLKKey("AEer"));
	ability_t const *roots = roots_item.ability;

	T_NOT_NULL(roots);
	T_ASSERT(roots->flags & AB_SPELL);
	T_EQ((int)roots_item.code, (int)MAKEFOURCC('A', 'E', 'e', 'r'));
	T_EQ((int)roots->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, death_and_decay_uses_percentage_damage_and_enemy_filter) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"DataA1\"\n"
		"C;Y1;X5;K\"DataB1\"\n"
		"C;Y1;X6;K\"Area1\"\n"
		"C;Y2;X1;K\"AUdd\"\n"
		"C;Y2;X2;K\"AUdd\"\n"
		"C;Y2;X3;K\"2\"\n"
		"C;Y2;X4;K\".04\"\n"
		"C;Y2;X5;K\"1\"\n"
		"C;Y2;X6;K\"128\"\n"
		"E\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	LPEDICT enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
	LPEDICT thinker;
	DWORD thinker_slot = globals.num_edicts;
	spellTarget_t st = { .type = SPELL_TARGET_POINT, .point = caster->s.origin2 };

	caster->s.player = 0;
	enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	enemy->health.value = enemy->health.max_value = 100;
	((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
    UnitAbilities_t abilities = { .abilList = "AUdd" };
    caster->data.UnitAbilities = &abilities;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AUdd"), &st.point));
	thinker = &globals.edicts[thinker_slot];
	T_FEQ(enemy->health.value, 96.0f, 0.01f);
	T_FEQ(caster->health.value, 250.0f, 0.01f);
	T_ASSERT(thinker->inuse);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, holy_light_rawcode_lookup_is_nul_safe) {
	DWORD code = MAKEFOURCC('A','H','h','b');
	ability_t const *spell = S_SpellAbilityForCode(code);
	ability_t const *abil = FindAbilityForCommand(GetClassName(code));

	/* Runtime spell dispatch starts from a DWORD rawcode. This specifically
	 * guards against treating &code as a C string: that only worked when the
	 * unrelated byte after the four rawcode bytes happened to be zero. */
    T_NOT_NULL(abil);
    T_ASSERT(abil->proc == CAbilityHolyBolt);
    T_ASSERT(abil->flags & AB_SPELL); T_NOT_NULL(abil->proc);
	T_NOT_NULL(spell);
	T_ASSERT(S_AbilityItem(code).ability == spell);
	T_EQ((int)spell->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, blizzard_is_channel) {
	ability_t const *abil = FindAbilityByClassname("AHbz");
	T_NOT_NULL(abil);
	T_ASSERT(abil->flags & AB_SPELL);
	T_ASSERT(abil->flags & AB_CHANNEL);
	T_EQ((int)abil->target_type, (int)SPELL_TARGET_POINT);
}

TEST(wc3_spell, cold_arrows_has_autocast_flag) {
	ability_t const *abil = FindAbilityByClassname("AHca");
	T_NOT_NULL(abil);
	T_ASSERT(abil->flags & AB_SPELL);
	T_ASSERT(abil->flags & AB_AUTOCAST);
	T_ASSERT(abil->flags & AB_TOGGLE);
}

TEST(wc3_spell, non_spell_ability_uses_explicit_command) {
	ability_t const *abil = FindAbilityByClassname(STR_CmdMove);
	T_NOT_NULL(abil);
	T_ASSERT(!(abil->flags & AB_SPELL));  /* move is not a spell */
	T_ASSERT(S_AbilityHasCommand(abil));
	T_NULL(FindAbilityByClassname("AAsm"));
	T_ASSERT(!S_AbilityHasCommand(FindAbilityByClassname("Afbk")));
}

/* Exercise the player command route with only the flag and leaf callbacks on Holy Bolt. */
TEST(wc3_spell, holy_light_flag_dispatch_validates_and_heals) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0), clent = &g_edicts[0];
    LPGAMECLIENT client = &game.clients[0];
    char number[16];
    LPCSTR button[] = { "button", "AHhb" }, select[] = { "select", number };

    old = G_SetSLKRows("AbilityData", rows);
    clent->client = client;
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = client->ps.number;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100; target->health.max_value = 500;
    G_SelectEntity(client, caster);

    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    snprintf(number, sizeof(number), "%u", (unsigned)caster->s.number);
    G_ClientCommand(clent, 2, select);
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','H','h','b')));

    G_ClientCommand(clent, 2, button);
    snprintf(number, sizeof(number), "%u", (unsigned)target->s.number);
    G_ClientCommand(clent, 2, select);
    T_FEQ(target->health.value, 300, 0.001f);
    T_FEQ(caster->mana.value, 135, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','H','h','b')));
    T_NULL(client->menu.on_entity_selected);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


/* The retained multiselect panel sends `focus <entity>` for portrait clicks.
 * While a unit-target spell is armed, Focus must feed that selected unit into
 * the target callback instead of changing subgroup focus. */
TEST(wc3_spell, holy_light_multiselect_portrait_targets_without_changing_focus) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    LPEDICT caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = &game.clients[0];
    char caster_number[16], target_number[16];
    LPCSTR button[] = { "button", "AHhb" };
    LPCSTR focus_caster[] = { "focus", caster_number };
    LPCSTR focus_target[] = { "focus", target_number };

    old = G_SetSLKRows("AbilityData", rows);
    clent->client = client;
    G_ResetSelectionFocus(client);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = client->ps.number;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100;
    target->health.max_value = 500;
    G_SelectEntity(client, caster);
    G_SelectEntity(client, target);
    T_ASSERT(G_FocusSelectedUnit(client, caster));
    T_ASSERT(G_GetMainSelectedUnit(client) == caster);
    snprintf(caster_number, sizeof(caster_number), "%u", (unsigned)caster->s.number);
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);

    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);

    /* Clicking Arthas's own portrait is an invalid Holy Light target. The
     * spell stays armed and the focused subgroup must remain Arthas. */
    G_ClientCommand(clent, 2, focus_caster);
    T_NOT_NULL(client->menu.on_entity_selected);
    T_ASSERT(G_GetMainSelectedUnit(client) == caster);
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','H','h','b')));
    T_FEQ(target->health.value, 100, 0.001f);

    /* Clicking the selected Footman's portrait targets him rather than
     * focusing his subgroup. Successful casting exits target mode. */
    G_ClientCommand(clent, 2, focus_target);
    T_FEQ(target->health.value, 300, 0.001f);
    T_FEQ(caster->mana.value, 135, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','H','h','b')));
    T_NULL(client->menu.on_entity_selected);
    T_ASSERT(G_GetMainSelectedUnit(client) == caster);
    T_ASSERT(G_IsEntitySelected(client, caster));
    T_ASSERT(G_IsEntitySelected(client, target));

    G_ResetSelectionFocus(client);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, unit_target_click_accepts_out_of_range_target_and_casts_after_approach) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOcl" };
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old;
    LPEDICT caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 300, 0, 0);
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client;
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
    LPEDICT thinker;
    DWORD thinker_slot;
    char target_number[16];
    LPCSTR button[] = { "button", "AOcl" };
    LPCSTR select_target[] = { "select", target_number };

    /* make_hero resets edicts, so restore the player-slot client for commands. */
    clent->client = client = &game.clients[0];
    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = client->ps.number;
    caster->collision = 16.0f;
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500.0f;
    ((LPMAPINFO)level.mapinfo)->players[caster->s.player].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[target->s.player].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SelectEntity(client, caster);
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);

    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    thinker_slot = globals.num_edicts;
    G_ClientCommand(clent, 2, select_target);

    T_NULL(client->menu.on_entity_selected);
    T_ASSERT(caster->goalentity == target);
    T_ASSERT(move_is_active_order_walk(caster));
    T_FEQ(target->health.value, 500.0f, 0.001f);
    T_FEQ(caster->mana.value, 300.0f, 0.001f);
    thinker = &globals.edicts[thinker_slot];
    T_ASSERT(thinker->inuse);
    T_NOT_NULL(thinker->think);

    caster->s.origin2.x = caster->s.origin.x = 425.0f;
    caster->s.origin2.y = caster->s.origin.y = 0.0f;
    thinker->think(thinker);

    T_ASSERT(!thinker->inuse);
    T_FEQ(target->health.value, 400.0f, 0.001f);
    T_FEQ(caster->mana.value, 225.0f, 0.001f);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','O','c','l')));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, target_order_name_routes_chain_lightning_through_spell_pipeline) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOcl" };
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old;
    LPEDICT caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 300, 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500.0f;
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.events.read = level.events.write = 0;
    memset(level.events.queue, 0, sizeof(level.events.queue));

    T_EQ(G_OrderId("chainlightning"), 852119);
    T_STREQ(G_OrderId2String(852119), "chainlightning");
    T_ASSERT(unit_issuetargetorder(caster, "chainlightning", target));
    T_FEQ(target->health.value, 400.0f, 0.001f);
    T_FEQ(caster->mana.value, 225.0f, 0.001f);
    T_EQ(G_GetIssuedOrderId(caster), 852119);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_SPELL_EFFECT);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_SPELL_EFFECT);
    T_EQ((DWORD)level.events.queue[0].value, MAKEFOURCC('A','O','c','l'));
    T_ASSERT(level.events.queue[0].source == target);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, point_order_name_routes_blink_and_carries_spell_point) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Cool1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AEbl\"\nC;Y2;X2;K\"AEbl\"\nC;Y2;X3;K\"10\"\n"
        "C;Y2;X4;K\"1\"\nC;Y2;X5;K\"1000\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AEbl" };
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old;
    LPEDICT caster = make_hero(MAKEFOURCC('E','w','d','n'), 500, 300, 0, 0);
    VECTOR2 point = { 64.0f, 32.0f };

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    caster->collision = 16.0f;
    level.events.read = level.events.write = 0;
    memset(level.events.queue, 0, sizeof(level.events.queue));

    T_ASSERT(unit_issueorder(caster, "blink", &point));
    T_EQ(G_GetIssuedOrderId(caster), 852525);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_SPELL_EFFECT);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_SPELL_EFFECT);
    T_ASSERT(level.events.queue[0].has_point);
    T_FEQ(level.events.queue[0].point.x, point.x, 0.001f);
    T_FEQ(level.events.queue[0].point.y, point.y, 0.001f);
    T_EQ((DWORD)level.events.queue[0].value, MAKEFOURCC('A','E','b','l'));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, polymorph_validates_creep_limit_summons_and_restores_runtime_state) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"DataD1\"\n"
        "C;Y1;X13;K\"DataE1\"\nC;Y1;X14;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aply\"\nC;Y2;X2;K\"Aply\"\nC;Y2;X3;K\"air,ground,enemy\"\n"
        "C;Y2;X4;K\"220\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"500\"\n"
        "C;Y2;X7;K\"60\"\nC;Y2;X8;K\"60\"\nC;Y2;X9;K\"5\"\n"
        "C;Y2;X10;K\"opeo\"\nC;Y2;X11;K\"opeo\"\nC;Y2;X12;K\"opeo\"\n"
        "C;Y2;X13;K\"opeo\"\nC;Y2;X14;K\"Bply\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Aply" };
    UnitData_t ground = { .moveTypeName = "foot" };
    UnitBalance_t creep = { .level = 5 };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    abilityitem_t ability_item = S_AbilityItem(FS_SLKKey("Aply"));
    ability_t const *ability = ability_item.ability;
    LPEDICT caster = make_hero(MAKEFOURCC('h','p','e','a'), 500, 500, 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    T_NOT_NULL(rows); T_NOT_NULL(ability); T_NOT_NULL(ability ? ability->proc : NULL);
    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    target->data.UnitData = &ground; target->data.UnitBalance = &creep;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE; target->svflags |= SVF_MONSTER;
    memset(level.alliances, 0, sizeof(level.alliances));

    T_ASSERT(test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    creep.level = 6; T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    creep.level = 5; target->summon_ability = MAKEFOURCC('A','O','s','f');
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    target->summon_ability = 0; target->aiflags |= AI_ILLUSION;
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));

    target->aiflags &= ~AI_ILLUSION;
    target->polymorph = MAKE(struct edictPolymorph_s,
        .ability = MAKEFOURCC('A','p','l','y'), .buff = MAKEFOURCC('B','p','l','y'),
        .form_type = MAKEFOURCC('o','p','e','o'), .original_model = 17,
        .original_scale = 1.25f, .original_move_speed = 234.0f, .active = true);
    target->s.model = 99; target->s.scale = 0.75f; target->unitinfo.MoveSpeed = 120.0f;
    T_ASSERT(S_UnitPolymorphed(target));
    S_PolymorphRemove(target);
    T_ASSERT(!S_UnitPolymorphed(target));
    T_EQ(target->s.model, 17); T_FEQ(target->s.scale, 1.25f, 0.001f);
    T_FEQ(target->unitinfo.MoveSpeed, 234.0f, 0.001f);
    T_EQ(target->class_id, MAKEFOURCC('h','f','o','o'));
    T_EQ(G_OrderId("polymorph"), WC3_ORDER_ID_POLYMORPH);
    T_STREQ(G_OrderId2String(WC3_ORDER_ID_POLYMORPH), "polymorph");

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, melee_spells_use_authored_status_and_bonus_values) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y5;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"BuffID1\"\n"
		"C;Y1;X9;K\"Area1\"\nC;Y1;X10;K\"targs\"\n"
		"C;Y2;X1;K\"Ablo\"\nC;Y2;X2;K\"Ablo\"\nC;Y2;X4;K\"60\"\nC;Y2;X5;K\"60\"\n"
		"C;Y2;X6;K\"0.4\"\nC;Y2;X7;K\"0.25\"\nC;Y2;X8;K\"Bblo\"\n"
		"C;Y3;X1;K\"Afae\"\nC;Y3;X2;K\"Afae\"\nC;Y3;X4;K\"90\"\nC;Y3;X5;K\"60\"\n"
		"C;Y3;X6;K\"4\"\nC;Y3;X8;K\"Bfae\"\n"
		"C;Y4;X1;K\"Arej\"\nC;Y4;X2;K\"Arej\"\nC;Y4;X4;K\"12\"\nC;Y4;X5;K\"12\"\n"
		"C;Y4;X6;K\"400\"\nC;Y4;X8;K\"Brej\"\n"
		"C;Y5;X1;K\"Aroa\"\nC;Y5;X2;K\"Aroa\"\nC;Y5;X4;K\"45\"\nC;Y5;X5;K\"45\"\n"
		"C;Y5;X6;K\"0.25\"\nC;Y5;X8;K\"Broa\"\nC;Y5;X9;K\"500\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	LPEDICT caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	LPEDICT ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	LPEDICT enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	abilityitem_t blo = S_AbilityItem(FS_SLKKey("Ablo"));
	abilityitem_t fae = S_AbilityItem(FS_SLKKey("Afae"));
	abilityitem_t rej = S_AbilityItem(FS_SLKKey("Arej"));
	abilityitem_t roa = S_AbilityItem(FS_SLKKey("Aroa"));
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->armor_value = 2; enemy->armor_value = 6;
	ally->health.value = 60; ally->health.max_value = 100;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	T_EQ(blo.ability->proc, CAbilityBloodlust); T_ASSERT(blo.ability->flags & AB_AUTOCAST);
	T_EQ(fae.ability->proc, CAbilityFaerieFire); T_ASSERT(fae.ability->flags & AB_AUTOCAST);
	T_EQ(rej.ability->proc, CAbilityRejuvination);
	T_EQ(roa.ability->proc, CAbilityRoar);
	test_execute_code(caster, "Ablo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','b','l','o')));
	T_FEQ(S_BloodlustAttackBonus(ally), 0.4f, 0.001f); T_FEQ(S_BloodlustMoveBonus(ally), 0.25f, 0.001f);
	test_execute_code(caster, "Afae", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','f','a','e'))); T_FEQ(G_UnitArmorValue(enemy), 2.0f, 0.001f);
	test_execute_code(caster, "Arej", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','e','j'))); T_FEQ(S_RejuvHealRate(ally), 400.0f / 12.0f, 0.01f);
	test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','o','a'))); T_FEQ(S_RoarDamageBonus(ally), 0.25f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Human05 turns each converted villager into a summon, then its JASS trigger
 * owns the final zombie replacement, allegiance, and attack order. */
TEST(wc3_spell, dark_conversion_consumes_target_and_publishes_zombie_summon) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"UnitID1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
        "C;Y2;X1;K\"SNdc\"\nC;Y2;X2;K\"ANdc\"\n"
        "C;Y2;X3;K\"air,ground,organic,nonhero\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"nzom\"\nC;Y2;X8;K\"BNdc\"\n"
        "C;Y2;X9;K\"6\"\nC;Y2;X10;K\"6\"\nE\n";
    DWORD const ability = MAKEFOURCC('S','N','d','c');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    LPEDICT caster = make_hero(MAKEFOURCC('U','C','0','2'), 1000, 1000, 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('n','v','i','l'), 64, 0);
    LPEDICT summon;
    DWORD summon_num;

    caster->heroabilities[0] = MAKE(heroability_t, .code = ability, .level = 1);
    caster->s.player = PLAYER_NEUTRAL_PASSIVE;
    target->s.player = 0; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100;
    memset(&level.events, 0, sizeof(level.events));
    summon_num = globals.num_edicts;

    T_NOT_NULL(S_SpellAbilityForCode(ability));
    T_ASSERT(S_AbilityHasCommand(S_SpellAbilityForCode(ability)));
    T_EQ(G_OrderId("darkconversion"), 852228);
    T_ASSERT(unit_issuetargetorder(caster, "darkconversion", target));
    summon = &globals.edicts[summon_num];
    T_ASSERT(!target->inuse);
    T_ASSERT(summon->inuse);
    T_EQ(summon->class_id, MAKEFOURCC('n','z','o','m'));
    T_EQ(summon->s.player, PLAYER_NEUTRAL_PASSIVE);
    T_FEQ(summon->s.origin2.x, 64, 0.001f); T_FEQ(summon->s.origin2.y, 0, 0.001f);
    T_EQ(level.events.queue[2].type, EVENT_PLAYER_UNIT_SUMMON);
    T_ASSERT(level.events.queue[2].edict == caster);
    T_ASSERT(level.events.queue[2].source == summon);
    T_EQ(level.events.queue[3].type, EVENT_UNIT_SUMMON);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

#endif /* BZ_TESTS */
