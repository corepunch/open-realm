#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

void attack_melee(edict_t *self);
void attack_ranged(edict_t *self);
void whirlwind_think(edict_t *ent);
void morph_end(edict_t *thinker);

uint32_t S_TestHeroAuraAliasResolves(void);
void S_TestResetHeroAuraAliasResolves(void);
bool S_TestAuraDiscoveryMatchesResolver(edict_t *ent);
void S_TestResetAuraClassificationCache(void);
uint32_t S_TestAuraClassResolves(void);
uint32_t S_TestAuraOwnershipChecks(void);
void S_TestResetMoonWellEffectReleaseCalls(void);
uint32_t S_TestMoonWellEffectReleaseCalls(void);
void S_TestResetMoonWellMissingDataWarningCalls(void);
uint32_t S_TestMoonWellMissingDataWarningCalls(void);
uint32_t S_TestMoonWellAlias(edict_t const *);
void S_TestResetMoonWellUpdateCalls(void);
uint32_t S_TestMoonWellUpdateCalls(void);
void S_TestResetDiseaseCloudTargetValidations(void);
uint32_t S_TestDiseaseCloudTargetValidations(void);
void S_TestResetMoonWellClassResolves(void);
uint32_t S_TestMoonWellClassResolves(void);

static pfWriteType_t terrain_deform_capture_types[32];
static int32_t terrain_deform_capture_values[32];
static float terrain_deform_capture_floats[16];
static uint32_t terrain_deform_capture_count, terrain_deform_capture_float_count;
void UI_TestFormatAttackDamageWithStatus(char *, size_t, edict_t const *, int32_t, int32_t, float, float);

static bool spell_cursor_clear_seen;
static bool spell_cursor_splat_seen;
static bool spell_cursor_image_clear;
static float spell_cursor_capture_radius;

static void spell_cursor_capture_write(pfWriteType_t type, void const *data) {
    if (type == PF_BYTE) {
        spell_cursor_splat_seen = *(int32_t const *)data == svc_cursor_splat;
    } else if (spell_cursor_splat_seen && type == PF_SHORT) {
        spell_cursor_image_clear = *(int32_t const *)data == 0;
    } else if (spell_cursor_splat_seen && type == PF_FLOAT) {
        spell_cursor_capture_radius = *(float const *)data;
        spell_cursor_clear_seen = spell_cursor_image_clear && spell_cursor_capture_radius == 0.0f;
        spell_cursor_splat_seen = false;
    }
}

static char const spell_cursor_test_slk[] =
    "ID;PWXL;N;EBB;Y2;X9\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
    "C;Y1;X4;K\"Cool1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"DataA1\"\n"
    "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"levels\"\nC;Y1;X9;K\"Area1\"\n"
    "C;Y2;X1;K\"AEbl\"\nC;Y2;X2;K\"AEbl\"\nC;Y2;X3;K\"10\"\n"
    "C;Y2;X4;K\"1\"\nC;Y2;X5;K\"1000\"\nC;Y2;X6;K\"1000\"\n"
    "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"1\"\nC;Y2;X9;K\"200\"\nE\n";

static edict_t *spell_cursor_test_begin(slkTestData_t **rows, slkTestData_t **old) {
    static UnitAbilities_t abilities;
    edict_t *clent, *caster;

    reset_entities(); setup_test_world();
    *rows = parse_slk_string(spell_cursor_test_slk);
    *old = G_SetSLKRows("AbilityData", *rows);
    abilities = (UnitAbilities_t){ .abilList = "AEbl" };
    clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->inuse = true;
    clent->client->connected = true;
    clent->client->ps.number = 0;
    caster = alloc_test_unit(MAKEFOURCC('E','w','d','n'), 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = clent->client->ps.number;
    caster->svflags |= SVF_MONSTER;
    caster->stand = unit_stand;
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','b','l'), .level = 1);
    G_SelectEntity(clent->client, caster);
    clent->client->menu.ability_code = MAKEFOURCC('A','E','b','l');
    spell_cursor_clear_seen = false;
    spell_cursor_splat_seen = false;
    spell_cursor_image_clear = false;
    spell_cursor_capture_radius = 0.0f;
    gi.Write = spell_cursor_capture_write;
    G_ClientCommand(clent, 2, (cstring_t[]){ "button", "AEbl" });
    return clent;
}

static void terrain_deform_capture_write(pfWriteType_t type, void const *data) {
    uint32_t slot = terrain_deform_capture_count++;
    if (slot < sizeof(terrain_deform_capture_types) / sizeof(terrain_deform_capture_types[0])) {
        terrain_deform_capture_types[slot] = type;
        if (type == PF_BYTE || type == PF_LONG) terrain_deform_capture_values[slot] = *(int32_t const *)data;
    }
    if (type == PF_FLOAT && terrain_deform_capture_float_count <
        sizeof(terrain_deform_capture_floats) / sizeof(terrain_deform_capture_floats[0]))
        terrain_deform_capture_floats[terrain_deform_capture_float_count++] = *(float const *)data;
}

static void terrain_deform_capture_multicast(vec3_t const *origin, multicast_t to) {
    (void)origin; (void)to;
}

static void spell_panel_test_write(pfWriteType_t type, void const *data) {
    (void)type;
    (void)data;
}

static void spell_panel_test_unicast(edict_t *ent) { (void)ent; }

static char dark_ritual_command_error[64];
static void capture_dark_ritual_command_error(pfWriteType_t type, void const *data) {
    if (type != PF_UIFRAME || !data) return;
    uiFrame_t const *frame = data;
    if (frame->flags.type == FT_STRING && frame->text && frame->textLength < sizeof(dark_ritual_command_error))
        strlcpy(dark_ritual_command_error, frame->text, sizeof(dark_ritual_command_error));
}

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void AbilitySummonMisha_Execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell);
void AbilityHowlOfTerror_Execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *slk_text);
void free_slk_rows(slkTestData_t *rows);
void SV_Physics_Toss(edict_t *ent);
static edict_t *make_hero(uint32_t class_id, float hp, float mana, float x, float y);

static intptr_t test_ability_message(edict_t *ent, abilityMsg_t msg, abilityitem_t const *item, spellTarget_t const *target) {
    abilityCall_t call = MAKE(abilityCall_t, .item = item, .target = target);
    return S_AbilityMessage(ent, msg, &call);
}

static intptr_t test_execute_code(edict_t *ent, cstring_t code, spellTarget_t target) {
    abilityitem_t item = S_AbilityItem(FS_SLKKey(code));
    return test_ability_message(ent, A_EXECUTE, &item, &target);
}

TEST(wc3_spell, mana_burn_drains_mana_and_deals_equal_health_damage_at_each_rank) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataA2\"\nC;Y1;X6;K\"DataA3\"\n"
        "C;Y2;X1;K\"AEmb\"\nC;Y2;X2;K\"AEmb\"\nC;Y2;X3;K\"3\"\n"
        "C;Y2;X4;K\"50\"\nC;Y2;X5;K\"100\"\nC;Y2;X6;K\"150\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *target;
    uint32_t rank;
    float const amounts[] = { 50.0f, 100.0f, 150.0f };

    old = G_SetSLKRows("AbilityData", rows);
    reset_entities();
    setup_test_world();
    caster = make_hero(MAKEFOURCC('E','d','e','m'), 500, 0, 0, 0);
    caster->s.player = 0;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

    for (rank = 1; rank <= 3; rank++) {
        caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','m','b'), .level = rank);
        target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
        target->s.player = 1;
        target->svflags |= SVF_MONSTER;
        target->health.value = target->health.max_value = 500.0f;
        target->mana.value = target->mana.max_value = 300.0f;
        T_ASSERT(test_execute_code(caster, "AEmb", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
        if (rank == 1) {
            T_EQ(G_AbilityLightningId(MAKEFOURCC('A','E','m','b'), 0), MAKEFOURCC('M','B','U','R'));
            T_STREQ(G_AbilityEffectArt(MAKEFOURCC('A','E','m','b'), WC3_EFFECT_TARGET, 0), "TestUI\\Models\\anim_pulse.mdx");
            T_ASSERT(level.lightning_effects[0].inuse);
            T_EQ(level.lightning_effects[0].state.effect_id, MAKEFOURCC('M','B','U','R'));
            T_EQ(level.lightning_effects[0].state.end_time, 300);
        }
        T_FEQ(target->mana.value, 300.0f - amounts[rank - 1], 0.001f);
        T_FEQ(target->health.value, 500.0f - amounts[rank - 1], 0.001f);
    }

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, dark_ritual_full_mana_command_shows_error_without_targeting) {
    static UnitAbilities_t const abilities = { .abilList = "AUdr" };
    edict_t *caster, *clent;
    gameClient_t *client = &game.clients[0];
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AUdr"));
    abilityCall_t call;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    clent = &g_edicts[0];
    call = (abilityCall_t){ .item = &item, .entity = clent };
    caster = make_hero(MAKEFOURCC('U','l','i','c'), 500, 300, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->mana.value = caster->mana.max_value = 300.0f;
    caster->s.player = client->ps.number = 0;
    clent->client = client;
    G_SetClientConnected(clent, true);
    G_SelectEntity(client, caster);
    dark_ritual_command_error[0] = '\0';
    gi.Write = capture_dark_ritual_command_error;
    gi.unicast = spell_panel_test_unicast;

    T_ASSERT(S_AbilityMessage(caster, A_COMMAND, &call));
    T_STREQ(dark_ritual_command_error, "Already at full mana.");
    T_NULL(client->menu.on_entity_selected);
    T_FEQ(caster->mana.value, caster->mana.max_value, 0.001f);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_spell, death_pact_full_health_command_uses_localized_error) {
    static UnitAbilities_t const abilities = { .abilList = "AUdp" };
    edict_t *caster, *clent;
    gameClient_t *client = &game.clients[0];
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AUdp"));
    abilityCall_t call;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;

    reset_entities();
    setup_test_world();
    clent = &g_edicts[0];
    call = (abilityCall_t){ .item = &item, .entity = clent };
    caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 300, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->health.value = caster->health.max_value;
    caster->s.player = client->ps.number = 0;
    clent->client = client;
    G_SetClientConnected(clent, true);
    G_SelectEntity(client, caster);
    dark_ritual_command_error[0] = '\0';
    gi.Write = capture_dark_ritual_command_error;
    gi.unicast = spell_panel_test_unicast;

    T_ASSERT(S_AbilityMessage(caster, A_COMMAND, &call));
    T_STREQ(dark_ritual_command_error, "Hero has full health.");
    T_NULL(client->menu.on_entity_selected);

    gi.Write = old_write;
    gi.unicast = old_unicast;
}

TEST(wc3_spell, dark_ritual_requests_caster_spell_animation) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"AUdr\"\nC;Y2;X2;K\"AUdr\"\nC;Y2;X3;K\"0.\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster, *target;
    abilityitem_t spell;
    spellTarget_t target_arg;

    reset_entities();
    setup_test_world();
    caster = make_hero(MAKEFOURCC('U','l','i','c'), 500, 0, 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    caster->s.player = target->s.player = 0;
    target->health.value = target->health.max_value = 100.0f;
    target->die = unit_die;
    spell = S_AbilityItem(FS_SLKKey("AUdr"));
    target_arg = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target);

    T_ASSERT(CAbilityDarkRitual(caster, A_EXECUTE, &(abilityCall_t){ .item = &spell, .target = &target_arg }));
    T_STREQ(caster->animation_request, "spell");
    T_NOT_NULL(caster->currentmove->endfunc);
    caster->currentmove->endfunc(caster);
    T_ASSERT(strcmp(caster->animation_request, "spell") != 0);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
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

static edict_t *make_hero(uint32_t class_id, float hp, float mana, float x, float y) {
	reset_entities();
	setup_test_world();
	edict_t *ent = alloc_test_unit(class_id, x, y);
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
    uint32_t first = MAKEFOURCC('A','0','0','1'), second = MAKEFOURCC('A','0','0','2');
    UnitAbilities_t abilities = { .abilList = "A001,A002" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

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

TEST(wc3_spell, wind_walk_delays_authored_cooldown_until_end_and_round_trips_origin) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOwk\"\nC;Y2;X2;K\"AOwk\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"17\"\nC;Y2;X5;K\"13\"\nC;Y2;X6;K\"2.75\"\n"
        "C;Y2;X7;K\"2.75\"\nC;Y2;X8;K\"33\"\nC;Y2;X9;K\"77\"\nE\n";
    cstring_t save = Test_TempPath("wind-walk-save.bin");
    UnitAbilities_t abilities = { .abilList = "AOwk" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    edict_t *target;
    heroabilitystatus_t *status;
    uint32_t number;

    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    T_EQ(S_AbilityItem(MAKEFOURCC('A','O','w','k')).ability->proc, CAbilityWindWalk);
    T_EQ(S_AbilityItem(MAKEFOURCC('A','N','w','k')).ability->proc, CAbilityWindWalk);
    T_ASSERT(S_CastNoTargetSpell(caster, MAKEFOURCC('A','O','w','k')));
    status = unit_findstatus(caster, MAKEFOURCC('B','O','w','k'));
    T_NOT_NULL(status);
    T_EQ(status->duration_ms, 2750);
    T_EQ(status->data, MAKEFOURCC('A','O','w','k'));
    T_ASSERT(S_StatusIsUndispellable(status));
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','O','w','k')));

    number = caster->s.number;
    T_ASSERT(WriteGame(save));
    memset(caster->unit_state->statuses, 0, sizeof(caster->unit_state->statuses));
    T_ASSERT(ReadGame(save));
    caster = g_edicts + number;
    status = unit_findstatus(caster, MAKEFOURCC('B','O','w','k'));
    T_NOT_NULL(status);
    T_EQ(status->data, MAKEFOURCC('A','O','w','k'));
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','O','w','k')));

    S_ResolveAttackHit(caster, target, 10);
    T_NULL(unit_findstatus(caster, MAKEFOURCC('B','O','w','k')));
    T_ASSERT(target->health.value < 100.0f);
    T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','O','w','k')));
    T_FEQ(S_SpellCooldownLength(caster, MAKEFOURCC('A','O','w','k')), 13.0f, 0.001f);

    remove(save);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, wind_walk_and_temporary_invisibility_keep_hidden_until_both_expire) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOwk\"\nC;Y2;X2;K\"AOwk\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"13\"\nC;Y2;X6;K\"2.75\"\n"
        "C;Y2;X7;K\"2.75\"\nC;Y2;X8;K\"77\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOwk" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *walk_first, *walk_second;
    heroabilitystatus_t *wind, *invis;

    setup_test_world();
    walk_first = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    walk_first->data.UnitAbilities = &abilities;
    walk_first->s.player = 0;
    T_ASSERT(S_CastNoTargetSpell(walk_first, MAKEFOURCC('A','O','w','k')));
    unit_addtimedstatus(walk_first, "Bixx", 1, 10.0f);
    unit_findstatus(walk_first, MAKEFOURCC('B','i','x','x'))->data = MAKEFOURCC('A','I','v','i');
    wind = unit_findstatus(walk_first, MAKEFOURCC('B','O','w','k'));
    invis = unit_findstatus(walk_first, MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(wind); T_NOT_NULL(invis);
    level.time = wind->timestamp;
    unit_updatestatuses(walk_first);
    T_NULL(unit_findstatus(walk_first, MAKEFOURCC('B','O','w','k')));
    T_ASSERT(G_UnitStatusLevel(walk_first, MAKEFOURCC('B','i','x','x')));
    T_ASSERT(walk_first->s.renderfx & RF_HIDDEN);

    level.time = invis->timestamp;
    unit_updatestatuses(walk_first);
    T_ASSERT(!(walk_first->s.renderfx & RF_HIDDEN));

    walk_second = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    walk_second->data.UnitAbilities = &abilities;
    walk_second->s.player = 0;
    T_ASSERT(S_CastNoTargetSpell(walk_second, MAKEFOURCC('A','O','w','k')));
    unit_addtimedstatus(walk_second, "Bixx", 1, 1.0f);
    unit_findstatus(walk_second, MAKEFOURCC('B','i','x','x'))->data = MAKEFOURCC('A','I','v','i');
    wind = unit_findstatus(walk_second, MAKEFOURCC('B','O','w','k'));
    invis = unit_findstatus(walk_second, MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(wind); T_NOT_NULL(invis);
    level.time = invis->timestamp;
    unit_updatestatuses(walk_second);
    T_ASSERT(G_UnitStatusLevel(walk_second, MAKEFOURCC('B','O','w','k')));
    T_ASSERT(walk_second->s.renderfx & RF_HIDDEN);

    wind = unit_findstatus(walk_second, MAKEFOURCC('B','O','w','k'));
    level.time = wind->timestamp;
    unit_updatestatuses(walk_second);
    T_ASSERT(!(walk_second->s.renderfx & RF_HIDDEN));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, invisibility_spell_uses_authored_buff_and_breaks_through_status_owner) {
    char slk[1024];
    cstring_t targs_column = atoi(gi.CvarString("fs_expansion", "0")) ? "targs1" : "targs";
    snprintf(slk, sizeof(slk),
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"%s\"\n"
        "C;Y2;X1;K\"Aivs\"\nC;Y2;X2;K\"Aivs\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"9\"\nC;Y2;X8;K\"9\"\nC;Y2;X9;K\"Bixx\"\n"
        "C;Y2;X10;K\"air,ground,friend\"\nE\n", targs_column);
    UnitAbilities_t abilities = { .abilList = "Aivs" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *target, *enemy;
    heroabilitystatus_t *status;

    setup_test_world();
    caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 0;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    target->svflags |= SVF_MONSTER;
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->health.value = enemy->health.max_value = 100.0f;
    enemy->svflags |= SVF_MONSTER;

    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','i','v','s'), target));
    status = unit_findstatus(target, MAKEFOURCC('B','i','x','x'));
    T_NOT_NULL(status);
    if (status) T_EQ(status->data, MAKEFOURCC('A','i','v','s'));
    T_ASSERT(target->s.renderfx & RF_HIDDEN);
    T_ASSERT(S_UnitUsesInvisibilityRenderFlag(target));
    T_ASSERT(S_UnitIsInvisibleToPlayer(target, 1));

    S_ResolveAttackHit(target, enemy, 10);
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','i','x','x')));
    T_ASSERT(!(target->s.renderfx & RF_HIDDEN));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, stock_invisibility_spell_applies_and_expires_binv) {
    char slk[1024];
    cstring_t targs_column = atoi(gi.CvarString("fs_expansion", "0")) ? "targs1" : "targs";
    snprintf(slk, sizeof(slk),
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"%s\"\n"
        "C;Y2;X1;K\"Aivs\"\nC;Y2;X2;K\"Aivs\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"120\"\nC;Y2;X8;K\"120\"\nC;Y2;X9;K\"Binv\"\n"
        "C;Y2;X10;K\"air,ground,friend\"\nE\n", targs_column);
    UnitAbilities_t abilities = { .abilList = "Aivs" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *target;
    heroabilitystatus_t *status;

    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    target->s.player = 0;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    target->svflags |= SVF_MONSTER;

    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','i','v','s'), target));
    status = unit_findstatus(target, MAKEFOURCC('B','i','n','v'));
    T_NOT_NULL(status);
    if (status) {
        T_EQ(status->duration_ms, 120000);
        T_EQ(status->data, MAKEFOURCC('A','i','v','s'));
        T_ASSERT(target->s.renderfx & RF_HIDDEN);
        level.time = status->timestamp;
        unit_updatestatuses(target);
        T_NULL(unit_findstatus(target, MAKEFOURCC('B','i','n','v')));
        T_ASSERT(!(target->s.renderfx & RF_HIDDEN));
    }

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, invisibility_spell_breaks_on_committed_spell) {
    char slk[1024];
    cstring_t targs_column = atoi(gi.CvarString("fs_expansion", "0")) ? "targs1" : "targs";
    snprintf(slk, sizeof(slk),
        "ID;PWXL;N;EBB;Y3;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"%s\"\n"
        "C;Y2;X1;K\"Aivs\"\nC;Y2;X2;K\"Aivs\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"600\"\n"
        "C;Y2;X7;K\"9\"\nC;Y2;X8;K\"9\"\nC;Y2;X9;K\"Bixx\"\n"
        "C;Y2;X10;K\"air,ground,friend\"\n"
        "C;Y3;X1;K\"AOwk\"\nC;Y3;X2;K\"AOwk\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"0\"\nC;Y3;X5;K\"13\"\nC;Y3;X7;K\"2.75\"\n"
        "C;Y3;X8;K\"2.75\"\nE\n", targs_column);
    UnitAbilities_t abilities = { .abilList = "Aivs" };
    UnitAbilities_t walk_abilities = { .abilList = "AOwk,Aivs" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *unit;

    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    unit->data.UnitAbilities = &walk_abilities;
    unit->s.player = 0;
    unit->targtype = TARG_GROUND;
    unit->health.value = unit->health.max_value = 100.0f;
    unit->svflags |= SVF_MONSTER;

    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','i','v','s'), unit));
    T_NOT_NULL(unit_findstatus(unit, MAKEFOURCC('B','i','x','x')));
    T_ASSERT(S_CastNoTargetSpell(unit, MAKEFOURCC('A','O','w','k')));
    T_NULL(unit_findstatus(unit, MAKEFOURCC('B','i','x','x')));
    T_NOT_NULL(unit_findstatus(unit, MAKEFOURCC('B','O','w','k')));
    T_ASSERT(unit->s.renderfx & RF_HIDDEN);
    T_ASSERT(S_CastUnitTargetSpell(unit, MAKEFOURCC('A','i','v','s'), unit));
    T_NULL(unit_findstatus(unit, MAKEFOURCC('B','O','w','k')));
    T_NOT_NULL(unit_findstatus(unit, MAKEFOURCC('B','i','x','x')));
    T_ASSERT(unit->s.renderfx & RF_HIDDEN);
    T_ASSERT(!S_SpellCooldownReady(unit, MAKEFOURCC('A','O','w','k')));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, numeric_status_payload_is_not_dispatched_as_wind_walk_owner) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOwk\"\nC;Y2;X2;K\"AOwk\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"13\"\nC;Y2;X6;K\"10\"\n"
        "C;Y2;X7;K\"10\"\nC;Y2;X8;K\"77\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOwk" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    heroabilitystatus_t *payload;

    unit->data.UnitAbilities = &abilities;
    unit->s.player = 0;
    unit_addtimedstatus(unit, "Bams", 1, 30.0f);
    payload = unit_findstatus(unit, MAKEFOURCC('B','a','m','s'));
    T_NOT_NULL(payload);
    if (payload) payload->data = MAKEFOURCC('A','O','w','k');
    T_ASSERT(S_CastNoTargetSpell(unit, MAKEFOURCC('A','O','w','k')));

    T_EQ(S_UnitStatusAbilityEvent(unit, A_ATTACK_DAMAGE_BONUS, NULL), 77);
    T_NOT_NULL(unit_findstatus(unit, MAKEFOURCC('B','O','w','k')));
    T_EQ(payload ? payload->data : 0, MAKEFOURCC('A','O','w','k'));

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
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    vec2_t point = {50, 0};

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    T_ASSERT(!S_CastPointTargetSpell(caster, MAKEFOURCC('A','0','0','3'), &point));
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_FEQ(caster->s.origin2.x, 0, 0.001f);
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','0','0','4'), &point));
    T_FEQ(caster->mana.value, 180, 0.001f);
    edict_t *thinker = NULL;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].owner == caster && g_edicts[i].think) { thinker = g_edicts + i; break; }
    T_NOT_NULL(thinker);
    level.time = thinker->freetime; G_RunEntity(thinker);
    level.time = thinker->freetime; G_RunEntity(thinker);
    T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code == 0);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, registry_keeps_identity_outside_shared_handlers) {
    cstring_t const fires[] = { "Afih", "Afin", "Afio", "Afir", "Afiu" };
    cstring_t const abstract[] = { "abil", "AAin", "AAbt", "AAsp", "AAsm", "Amor", "ABon", "ATrn" };
    InitAbilities();
    T_EQ(FindAbilityByClassname(STR_CmdTrains)->proc, CAbilityTrain);
    T_EQ(GetAbilityByIndex(GetAbilityIndex(CAbilityTrain))->proc, CAbilityTrain);
    FOR_LOOP(i, sizeof(fires) / sizeof(*fires)) {
        uint32_t code = FS_SLKKey(fires[i]);
        abilityitem_t item = S_AbilityItem(code);
        T_EQ(item.code, code);
        T_EQ(item.ability->proc, CAbilityOnFireHuman);
    }
    FOR_LOOP(i, sizeof(abstract) / sizeof(*abstract)) T_NULL(FindAbilityByClassname(abstract[i]));
    T_EQ(FindAbilityByClassname("AIco")->proc, CAbilityCharm);
    T_EQ(FindAbilityByClassname("Afbk")->proc, CAbilityFeedback);
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

/* Barkskin is a friendly target spell whose modal parent owns autocast selection, not a self-toggle. */
static void test_barkskin_contract(cstring_t armor_field) {
	char slk[1024];
	uint32_t const barkskin = MAKEFOURCC('A','b','a','r'), buff = MAKEFOURCC('B','b','a','r');
	UnitAbilities_t abilities = { .abilList = "Abar" };
	slkTestData_t *rows, *old;
	edict_t *caster = make_hero(MAKEFOURCC('e','d','o','c'), 500, 200, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('e','a','r','c'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 75, 0);
	ability_t const *ability;

	snprintf(slk, sizeof(slk),
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Rng1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
		"C;Y1;X7;K\"%s\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"levels\"\n"
		"C;Y2;X1;K\"Abar\"\nC;Y2;X2;K\"Abar\"\nC;Y2;X3;K\"air,ground,friend\"\n"
		"C;Y2;X4;K\"600\"\nC;Y2;X5;K\"10\"\nC;Y2;X6;K\"4\"\n"
		"C;Y2;X7;K\"3\"\nC;Y2;X8;K\"Bbar\"\nC;Y2;X9;K\"1\"\nE\n", armor_field);
	rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
	caster->data.UnitAbilities = &abilities;
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	ally->targtype = enemy->targtype = TARG_GROUND;
	ally->health.value = ally->health.max_value = enemy->health.value = enemy->health.max_value = 100;
	ally->unit_state->armor_value = 2.0f; level.time = 1000;

	ability = FindAbilityByClassname("Abar");
	T_NOT_NULL(ability); T_EQ(ability->proc, CAbilityBarkskin);
	T_ASSERT(ability->flags & AB_SPELL); T_ASSERT(ability->flags & AB_AUTOCAST);
	T_ASSERT(!(ability->flags & AB_TOGGLE)); T_EQ(ability->target_type, SPELL_TARGET_UNIT);
	T_ASSERT(!G_UnitAutocastIsOn(caster, barkskin));
	T_ASSERT(G_SetUnitAutocast(caster, barkskin, true)); T_ASSERT(G_UnitAutocastIsOn(caster, barkskin));
	T_ASSERT(test_ability_message(caster, A_AUTOCAST_ACQUIRE, &(abilityitem_t){
		.code = barkskin, .ability = ability
	}, NULL));
	T_EQ(G_UnitStatusLevel(ally, buff), 1); T_FEQ(G_UnitArmorValue(ally), 5.0f, 0.001f);
	T_ASSERT(!test_ability_message(caster, A_AUTOCAST_ACQUIRE, &(abilityitem_t){
		.code = barkskin, .ability = ability
	}, NULL));
	T_ASSERT(!S_CastUnitTargetSpell(caster, barkskin, enemy));
	T_ASSERT(S_CastUnitTargetSpell(caster, barkskin, ally));
	T_EQ(G_UnitStatusLevel(ally, buff), 1); T_FEQ(G_UnitArmorValue(ally), 5.0f, 0.001f);
	T_ASSERT(G_SetUnitAutocast(caster, barkskin, false)); T_ASSERT(!G_UnitAutocastIsOn(caster, barkskin));
	T_EQ(G_UnitStatusLevel(ally, buff), 1);

	level.time = 6000;
	T_ASSERT(S_CastUnitTargetSpell(caster, barkskin, ally));
	level.time = 15000;
	T_EQ(G_UnitStatusLevel(ally, buff), 1); T_FEQ(G_UnitArmorValue(ally), 5.0f, 0.001f);
	level.time = 16001; unit_updatestatuses(ally);
	T_EQ(G_UnitStatusLevel(ally, buff), 0); T_FEQ(G_UnitArmorValue(ally), 2.0f, 0.001f);
	ally->health.value = 0;
	T_ASSERT(!S_CastUnitTargetSpell(caster, barkskin, ally));

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, barkskin_roc_schema_uses_timed_friendly_armor_buff) { test_barkskin_contract("Data11"); }
TEST(wc3_spell, barkskin_tft_schema_uses_timed_friendly_armor_buff) { test_barkskin_contract("DataA1"); }

TEST(wc3_spell, relationship_uses_passive_alliance_not_other_flags) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	caster->s.player = 0;
	target->s.player = 1;
	target->svflags |= SVF_MONSTER;
	target->health.value = target->health.max_value = 100;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
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
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	G_AddChannel(caster);
	G_GetChannel(caster)->code = MAKEFOURCC('A','H','b','z');
	G_GetChannel(caster)->origin = caster->s.origin2;
	unit_addtimedstatus(caster, "Bstu", 1, 10.0f);
	spell_run_frame(caster);
	T_EQ((int)G_GetChannel(caster)->code, 0);
}

/* Expired status slots must stop contributing before the status cleanup tick. */
TEST(wc3_spell, expired_stun_does_not_cancel_channel_before_cleanup) {
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    level.time = 1000;
    unit_addtimedstatus(caster, "Bstu", 1, 0.1f);
    G_AddChannel(caster);
    G_GetChannel(caster)->code = MAKEFOURCC('A','H','b','z');
    G_GetChannel(caster)->origin = caster->s.origin2;
    level.time = 1100;
    spell_run_frame(caster);
    T_EQ(G_GetChannel(caster)->code, MAKEFOURCC('A','H','b','z'));
}

TEST(wc3_spell, channel_cancel_death) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	G_AddChannel(caster);
	G_GetChannel(caster)->code = MAKEFOURCC('A','H','b','z');
	G_GetChannel(caster)->origin = caster->s.origin2;
	caster->health.value = 0;
	spell_run_frame(caster);
	T_EQ((int)G_GetChannel(caster)->code, 0);
}

TEST(wc3_spell, channel_cancel_movement) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	G_AddChannel(caster);
	G_GetChannel(caster)->code = MAKEFOURCC('A','H','b','z');
	G_GetChannel(caster)->origin.x = 100; G_GetChannel(caster)->origin.y = 100;
	caster->s.origin.x = 101; caster->s.origin.y = 100;
	caster->s.origin2.x = 101; caster->s.origin2.y = 100;
	spell_run_frame(caster);
	T_EQ((int)G_GetChannel(caster)->code, 0);
}

TEST(wc3_spell, channel_persists_no_movement) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	G_AddChannel(caster);
	G_GetChannel(caster)->code = MAKEFOURCC('A','H','b','z');
	G_GetChannel(caster)->origin = caster->s.origin2;
	spell_run_frame(caster);
	T_EQ((int)G_GetChannel(caster)->code, (int)MAKEFOURCC('A','H','b','z'));
}

TEST(wc3_spell, channel_cancel_clears_code) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	G_AddChannel(caster);
	G_GetChannel(caster)->code = MAKEFOURCC('A','H','b','z');
	S_SpellCancelChannel(caster);
	T_EQ((int)G_GetChannel(caster)->code, 0);
	T_STREQ(caster->currentmove->animation, "stand");
}

TEST(wc3_spell, channel_cancel_noop_empty) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	G_AddChannel(caster);
	G_GetChannel(caster)->code = 0;
	S_SpellCancelChannel(caster);
	T_EQ((int)G_GetChannel(caster)->code, 0);
}

TEST(wc3_spell, channel_run_frame_noop_when_idle) {
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	G_AddChannel(caster);
	G_GetChannel(caster)->code = 0;
	unit_addtimedstatus(caster, "Bstu", 1, 10.0f);
	spell_run_frame(caster);
	T_EQ((int)G_GetChannel(caster)->code, 0);
}

/* ---- Toggle bypass ---- */

TEST(wc3_spell, immolation_command_toggle_damages_enemy_land_units_until_mana_or_toggle_off) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\n"
        "C;Y1;X8;K\"Cost2\"\nC;Y1;X9;K\"Area2\"\nC;Y1;X10;K\"DataA2\"\nC;Y1;X11;K\"DataB2\"\n"
        "C;Y2;X1;K\"AEim\"\nC;Y2;X2;K\"AEim\"\nC;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"25\"\nC;Y2;X5;K\"160\"\nC;Y2;X6;K\"10\"\nC;Y2;X7;K\"7\"\n"
        "C;Y2;X8;K\"25\"\nC;Y2;X9;K\"160\"\nC;Y2;X10;K\"15\"\nC;Y2;X11;K\"7\"\nE\n";
    static UnitAbilities_t const abilities = { .abilList = "AEim" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *enemy_land, *friend_land, *enemy_air;
    edict_t *clent;
    gameClient_t *client;
    cstring_t button[] = { "button", "AEim" };
    gameCommandButton_t command_button;
    uint32_t const buff = MAKEFOURCC('B','E','i','m');
    heroabilitystatus_t *immolation_status;
    edict_t *caster_effect;

    old = G_SetSLKRows("AbilityData", rows);
    reset_entities();
    setup_test_world();
    caster = make_hero(MAKEFOURCC('E','d','e','m'), 1000, 24, 0, 0);
    enemy_land = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    friend_land = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 20);
    enemy_air = alloc_test_unit(MAKEFOURCC('h','g','r','y'), 100, -20);
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->client = client;
    client->connected = true;
    client->ps.number = 0;
    caster->data.UnitAbilities = &abilities;
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','i','m'), .level = 2);
    caster->s.player = friend_land->s.player = 0;
    enemy_land->s.player = enemy_air->s.player = 1;
    enemy_land->svflags |= SVF_MONSTER;
    enemy_air->svflags |= SVF_MONSTER;
    enemy_land->targtype = friend_land->targtype = TARG_GROUND;
    enemy_air->targtype = TARG_AIR;
    enemy_land->health.value = friend_land->health.value = enemy_air->health.value = 100;
    enemy_land->health.max_value = friend_land->health.max_value = enemy_air->health.max_value = 100;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    T_ASSERT(S_SpellIsAliveTarget(enemy_land));
    T_ASSERT(S_SpellIsEnemy(caster, enemy_land));
    T_EQ(G_UnitTargetType(enemy_land), TARG_GROUND);
    T_FEQ(G_AbilityLevel(MAKEFOURCC('A','E','i','m'), 1)->area, 160.0f, 0.001f);
    G_SelectEntity(client, caster);
    T_ASSERT(G_BuildCommandButton(caster, "AEim", false, 0, &command_button));
    T_STREQ(command_button.art, "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");

    /* Cost1 is a distinct upfront charge; insufficient mana must leave the
     * toggle off. The 7 mana/sec drain is delivered one point at a time. */
    G_ClientCommand(clent, 2, button);
    T_NULL(unit_findstatus(caster, buff));
    T_FEQ(caster->mana.value, 24.0f, 0.001f);
    caster->mana.value = 39.0f;
    G_ClientCommand(clent, 2, button);
    immolation_status = unit_findstatus(caster, buff);
    T_NOT_NULL(immolation_status);
    T_ASSERT(G_BuildCommandButton(caster, "AEim", false, 0, &command_button));
    T_STREQ(command_button.art, "ReplaceableTextures\\CommandButtons\\BTNImmolationOff.blp");
    T_EQ(G_UnitStatusBuffCode(immolation_status), buff);
    T_STREQ(G_AbilityBuffArt(buff), "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");
    T_STREQ(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0), "TestUI\\Models\\anim_pulse.mdx");
    caster_effect = NULL;
    FILTER_EDICTS(effect, effect->status_effect_code == buff && effect->goalentity == caster) {
        caster_effect = effect;
        break;
    }
    T_NOT_NULL(caster_effect);
    T_FEQ(caster->mana.value, 14.0f, 0.001f);
    level.time = 142;
    S_RunAbilityUpdates(caster);
    T_FEQ(caster->mana.value, 14.0f, 0.001f);
    level.time = 143;
    S_RunAbilityUpdates(caster);
    T_FEQ(caster->mana.value, 13.0f, 0.001f);
    level.time = 500;
    S_RunAbilityUpdates(caster);
    T_FEQ(caster->mana.value, 11.0f, 0.001f);
    level.time = 1000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 85.0f, 0.001f);
    T_FEQ(friend_land->health.value, 100.0f, 0.001f);
    T_FEQ(enemy_air->health.value, 100.0f, 0.001f);
    T_FEQ(caster->mana.value, 7.0f, 0.001f);
    client->commands_dirty = false;
    level.time = 2000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 70.0f, 0.001f);
    T_FEQ(caster->mana.value, 0.0f, 0.001f);
    T_NULL(unit_findstatus(caster, buff));
    T_ASSERT(client->commands_dirty);
    T_ASSERT(G_BuildCommandButton(caster, "AEim", false, 0, &command_button));
    T_STREQ(command_button.art, "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");
    caster_effect = NULL;
    FILTER_EDICTS(effect, effect->status_effect_code == buff && effect->goalentity == caster) {
        caster_effect = effect;
        break;
    }
    T_NULL(caster_effect);
    level.time = 3000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 70.0f, 0.001f);

    /* It can also be switched off before its mana runs out. */
    caster->mana.value = 39.0f;
    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(unit_findstatus(caster, buff));
    T_FEQ(caster->mana.value, 14.0f, 0.001f);
    level.time = 4000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 55.0f, 0.001f);
    T_FEQ(caster->mana.value, 7.0f, 0.001f);
    G_ClientCommand(clent, 2, button);
    T_NULL(unit_findstatus(caster, buff));
    T_ASSERT(G_BuildCommandButton(caster, "AEim", false, 0, &command_button));
    T_STREQ(command_button.art, "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");
    caster_effect = NULL;
    FILTER_EDICTS(effect, effect->status_effect_code == buff && effect->goalentity == caster) {
        caster_effect = effect;
        break;
    }
    T_NULL(caster_effect);
    level.time = 5000;
    S_RunAbilityUpdates(caster);
    T_FEQ(enemy_land->health.value, 55.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Both schemas must honor fractional and zero DataB without changing DPS. */
TEST(wc3_spell, immolation_custom_mana_rate_keeps_one_second_damage_pulses) {
    float const rates[] = { 3.5f, 0.0f, 14.0f };
    for (uint32_t roc = 0; roc < 2; roc++) {
        FOR_LOOP(i, sizeof(rates) / sizeof(rates[0])) {
            char slk[1024];
            snprintf(slk, sizeof(slk),
                "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
                "C;Y1;X4;K\"%s\"\nC;Y1;X5;K\"%s\"\nC;Y1;X6;K\"%s\"\n"
                "C;Y2;X1;K\"AEim\"\nC;Y2;X2;K\"AEim\"\nC;Y2;X3;K1\n"
                "C;Y2;X4;K160\nC;Y2;X5;K13\nC;Y2;X6;K%g\nE\n",
                "Area1", roc ? "Data11" : "DataA1", roc ? "Data12" : "DataB1", rates[i]);
            slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
            reset_entities(); setup_test_world(); level.time = 0;
            edict_t *caster = make_hero(MAKEFOURCC('E','d','e','m'), 1000, 100, 0, 0);
            edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
            static UnitAbilities_t const abilities = { .abilList = "AEim" };
            caster->data.UnitAbilities = &abilities;
            caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','i','m'), .level = 1);
            caster->s.player = 0; enemy->s.player = 1;
            enemy->svflags |= SVF_MONSTER;
            enemy->targtype = TARG_GROUND;
            enemy->health.value = enemy->health.max_value = 100;
            memset(level.alliances, 0, sizeof(level.alliances));
            ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
            ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
            abilityitem_t const item = S_AbilityItem(MAKEFOURCC('A','E','i','m'));
            abilityCall_t const call = MAKE(abilityCall_t, .item = &item);
            T_ASSERT(S_AbilityMessage(caster, A_EXECUTE, &call));
            level.time = 999; S_RunAbilityUpdates(caster);
            T_FEQ(enemy->health.value, 100, 0.001f);
            level.time = 1000; S_RunAbilityUpdates(caster);
            T_FEQ(enemy->health.value, 87, 0.001f);
            T_FEQ(caster->mana.value, 100 - rates[i], 0.001f);
            cstring_t const filename = Test_TempPath("immolation-substeps.bin");
            T_ASSERT(WriteGame(filename));
            T_ASSERT(ReadGame(filename));
            remove(filename);
            level.time = 2000; S_RunAbilityUpdates(caster);
            T_FEQ(enemy->health.value, 74, 0.001f);
            T_FEQ(caster->mana.value, 100 - 2 * rates[i], 0.001f);
            T_ASSERT(S_AbilityMessage(caster, A_EXECUTE, &call));
            level.time = 3000; S_RunAbilityUpdates(caster);
            T_FEQ(enemy->health.value, 74, 0.001f);
            G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
        }
    }
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
		"ID;PWXL;N;EBB;Y9;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y2;X1;K\"AHab\"\nC;Y2;X2;K\"AHab\"\nC;Y2;X3;K\"900\"\nC;Y2;X4;K\"0.75\"\n"
		"C;Y3;X1;K\"AUau\"\nC;Y3;X2;K\"AUau\"\nC;Y3;X3;K\"900\"\nC;Y3;X4;K\"0.1\"\nC;Y3;X5;K\"0.5\"\n"
		"C;Y4;X1;K\"AUav\"\nC;Y4;X2;K\"AUav\"\nC;Y4;X3;K\"900\"\nC;Y4;X4;K\"0.2\"\n"
		"C;Y5;X1;K\"AOcr\"\nC;Y5;X2;K\"AOcr\"\nC;Y5;X4;K\"100\"\nC;Y5;X5;K\"2\"\n"
		"C;Y6;X1;K\"AEev\"\nC;Y6;X2;K\"AEev\"\nC;Y6;X4;K\"1\"\n"
		"C;Y7;X1;K\"AUts\"\nC;Y7;X2;K\"AUts\"\nC;Y7;X4;K\"0.15\"\nC;Y7;X5;K\"1\"\nC;Y7;X6;K\"3\"\n"
		"C;Y8;X1;K\"ACct\"\nC;Y8;X2;K\"ACct\"\nC;Y8;X4;K\"100\"\nC;Y8;X5;K\"2\"\n"
		"C;Y9;X1;K\"AHad\"\nC;Y9;X2;K\"AHad\"\nC;Y9;X3;K\"900\"\nC;Y9;X4;K\"3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *source = make_hero(MAKEFOURCC('H','a','m','g'), 500, 300, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	level.time = 0;
	source->s.player = target->s.player = 0;
	target->unit_state->attack1.type = ATK_NORMAL;
	target->unit_state->attack1.weapon = WPN_NORMAL;
	target->unit_state->attack1.range = 90.0f;
	source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','b'), .level = 1);
	source->unit_state->ranks[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','u'), .level = 1);
	source->unit_state->ranks[2] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','v'), .level = 1);
	source->unit_state->ranks[3] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','d'), .level = 1);
	target->unit_state->armor_value = 2.0f;
	target->mana.value = target->mana.max_value = 100.0f;
	T_FEQ(S_BrillianceManaRegen(target), 0.75f, 0.001f);
	T_FEQ(G_UnitArmorValue(target), 5.0f, 0.001f);
	T_FEQ(S_UnholyMoveBonus(target), 0.1f, 0.001f);
	T_FEQ(S_UnholyHealthRegen(target), 0.5f, 0.001f);
	T_FEQ(S_VampiricLifeSteal(target), 0.2f, 0.001f);
	level.time = AURA_UPDATE_MS / 2;
	target->s.origin2.x = 901.0f;
	T_FEQ(S_BrillianceManaRegen(target), 0.75f, 0.001f);
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_BrillianceManaRegen(target), 0.0f, 0.001f);
	T_FEQ(S_UnholyMoveBonus(target), 0.0f, 0.001f);
	T_FEQ(G_UnitArmorValue(target), 2.0f, 0.001f);

	target->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','r'), .level = 1);
	target->unit_state->ranks[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','e','v'), .level = 1);
	target->unit_state->ranks[2] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','t','s'), .level = 1);
	T_EQ(S_CriticalStrikeDamage(target, 10), 20);
	T_ASSERT(S_EvasionRoll(target));
	T_FEQ(G_UnitArmorValue(target), 5.0f, 0.001f);
	T_FEQ(S_SpikedDamageReturn(target, 2.0f), 1.0f, 0.001f);
	T_FEQ(S_SpikedDamageReturn(target, 20.0f), 3.0f, 0.001f);
	memset(target->unit_state->ranks, 0, sizeof(target->unit_state->ranks));
	T_EQ(S_CriticalStrikeDamage(target, 10), 10);
	T_ASSERT(!S_EvasionRoll(target));
	T_FEQ(G_UnitArmorValue(target), 2.0f, 0.001f);

	/* Feral Spirit's Dire/Shadow Wolves own the creep Critical Strike ability, ACct. */
	UnitAbilities_t creep_abilities = { .abilList = "ACct" };
	target->data.UnitAbilities = &creep_abilities;
	T_EQ(S_CriticalStrikeDamage(target, 10), 20);
	target->data.UnitAbilities = NULL;

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, naga_dragon_turtle_spiked_shell_returns_thirty_percent_of_melee_hit_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"levels\"\n"
		"C;Y2;X1;K\"ANt2\"\nC;Y2;X2;K\"AUts\"\nC;Y2;X3;K0.30\n"
		"C;Y2;X4;K0\nC;Y2;X5;K0\nC;Y2;X6;K1\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker, *defender;
	UnitAbilities_t const dragon_turtle_abilities = {
		.abilList = "ACc2,ACdv,Advc,ANt2"
	};
	float attacker_health, defender_health;

	reset_entities();
	setup_test_world();
	attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
	defender = alloc_test_unit(MAKEFOURCC('n','t','r','d'), 50, 0);
	defender->data.UnitAbilities = (UnitAbilities_t *)&dragon_turtle_abilities;
	attacker->health.value = attacker->health.max_value = 500;
	defender->health.value = defender->health.max_value = 500;
	attacker->s.player = 0;
	defender->s.player = 1;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	defender->targtype = TARG_GROUND;
	attacker->unit_state->attack1.type = ATK_NORMAL;
	attacker->unit_state->attack1.weapon = WPN_NORMAL;
	defender->unit_state->attack1.type = ATK_NORMAL;
	defender->unit_state->attack1.weapon = WPN_NORMAL;
	attacker->unit_state->armor_value = defender->unit_state->armor_value = 0;
	T_ASSERT(G_ActorHasSkill(defender, "ANt2"));
	T_ASSERT(S_SpellIsEnemy(defender, attacker));
	T_FEQ(S_SpikedCarapaceDamageReturn(defender, 100.0f), 30.0f, 0.001f);

	attacker_health = attacker->health.value;
	defender_health = defender->health.value;
	S_ResolveAttackHit(attacker, defender, 100);
	T_FEQ(defender_health - defender->health.value, 100.0f, 0.001f);
	T_FEQ(attacker_health - attacker->health.value, 30.0f, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, dragon_turtle_hardened_skin_reduces_all_attacks_but_keeps_three_damage_minimum) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"levels\"\n"
		"C;Y2;X1;K\"Ansk\"\nC;Y2;X2;K\"Assk\"\nC;Y2;X3;K100\n"
		"C;Y2;X4;K3\nC;Y2;X5;K12\nC;Y2;X6;K1\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker, *dragon_turtle;
	UnitAbilities_t const dragon_turtle_abilities = { .abilList = "Ansk" };
	float health_before;

	reset_entities();
	setup_test_world();
	attacker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
	dragon_turtle = alloc_test_unit(MAKEFOURCC('n','t','r','d'), 50, 0);
	dragon_turtle->data.UnitAbilities = (UnitAbilities_t *)&dragon_turtle_abilities;
	attacker->health.value = attacker->health.max_value = 500;
	dragon_turtle->health.value = dragon_turtle->health.max_value = 500;
	attacker->s.player = 0;
	dragon_turtle->s.player = 1;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	dragon_turtle->targtype = TARG_GROUND;
	attacker->unit_state->armor_value = dragon_turtle->unit_state->armor_value = 0;

	attacker->unit_state->attack1.type = ATK_NORMAL;
	attacker->unit_state->attack1.weapon = WPN_NORMAL;
	health_before = dragon_turtle->health.value;
	S_ResolveAttackHit(attacker, dragon_turtle, 100);
	T_FEQ(health_before - dragon_turtle->health.value, 88.0f, 0.001f);

	attacker->unit_state->attack1.type = ATK_PIERCE;
	attacker->unit_state->attack1.weapon = WPN_MISSILE;
	health_before = dragon_turtle->health.value;
	S_ResolveAttackHit(attacker, dragon_turtle, 10);
	T_FEQ(health_before - dragon_turtle->health.value, 3.0f, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, creep_pulverize_and_spiked_aliases_use_authored_data) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\nC;Y1;X7;K\"DataD1\"\nC;Y1;X8;K\"DataE1\"\n"
		"C;Y2;X1;K\"Awar\"\nC;Y2;X2;K\"Awar\"\nC;Y2;X3;K\"0\"\nC;Y2;X4;K\"100\"\nC;Y2;X5;K\"25\"\nC;Y2;X6;K\"32\"\nC;Y2;X7;K\"48\"\n"
		"C;Y3;X1;K\"Aspi\"\nC;Y3;X2;K\"Aspi\"\nC;Y3;X4;K\"0.15\"\nC;Y3;X5;K\"3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
	edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 10, 0);
	edict_t *splash = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 30, 0);
	UnitAbilities_t abilities = { .abilList = "Awar,Aspi" };

	attacker->data.UnitAbilities = &abilities;
	attacker->s.player = 0; primary->s.player = splash->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
	primary->svflags |= SVF_MONSTER; splash->svflags |= SVF_MONSTER;
	primary->targtype = splash->targtype = TARG_GROUND;
	primary->health.value = primary->health.max_value = 100;
	splash->health.value = splash->health.max_value = 100;
	T_EQ(G_UnitAbilityLevel(attacker, MAKEFOURCC('A','w','a','r')), 1);
	T_FEQ(S_SpellData(MAKEFOURCC('A','w','a','r'), 1, 1), 100, 0.001f);
	S_PulverizeAttack(attacker, primary);
	T_FEQ(splash->health.value, 75, 0.001f);
	T_FEQ(S_SpikedDamageReturn(attacker, 20), 3, 0.001f);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, creep_incinerate_stacks_and_explodes_from_authored_rows) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"BuffID1\"\n"
		"C;Y1;X7;K\"DataC1\"\n"
		"C;Y2;X1;K\"ANia\"\nC;Y2;X2;K\"ANia\"\nC;Y2;X3;K\"0\"\nC;Y2;X4;K\"2\"\nC;Y2;X5;K\"20\"\nC;Y2;X6;K\"BNic\"\nC;Y2;X7;K\"80\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
	edict_t *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
	edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	UnitAbilities_t abilities = { .abilList = "ANia" };

	attacker->data.UnitAbilities = &abilities;
	attacker->s.player = 0; victim->s.player = nearby->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
	victim->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER;
	victim->die = unit_die; nearby->die = unit_die;
	victim->targtype = nearby->targtype = TARG_GROUND;
	victim->health.value = victim->health.max_value = 100;
	nearby->health.value = nearby->health.max_value = 100;
	S_IncinerateOnHit(attacker, victim);
	T_FEQ(victim->health.value, 98, 0.001f);
	T_EQ(G_UnitStatusLevel(victim, FS_SLKKey("BNic")), 1);
	S_IncinerateOnHit(attacker, victim);
	T_FEQ(victim->health.value, 94, 0.001f);
	T_EQ(G_UnitStatusLevel(victim, FS_SLKKey("BNic")), 2);
	victim->health.value = 1;
	S_IncinerateOnHit(attacker, victim);
	T_ASSERT(M_IsDead(victim));
	T_FEQ(nearby->health.value, 80, 0.001f);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, creep_attack_passives_and_death_aoe_use_authored_rows) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y5;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"DataD1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y2;X1;K\"ANmr\"\nC;Y2;X2;K\"ANmr\"\nC;Y2;X5;K\"17\"\n"
        "C;Y3;X1;K\"Aliq\"\nC;Y3;X2;K\"Aliq\"\nC;Y3;X3;K\"100\"\nC;Y3;X4;K\"5\"\nC;Y3;X7;K\"0.25\"\nC;Y3;X9;K\"Bliq\"\n"
        "C;Y4;X1;K\"Acor\"\nC;Y4;X2;K\"Acor\"\nC;Y4;X3;K\"100\"\nC;Y4;X4;K\"5\"\nC;Y4;X9;K\"Bcor\"\n"
        "C;Y5;X1;K\"Adda\"\nC;Y5;X2;K\"Adda\"\nC;Y5;X3;K\"40\"\nC;Y5;X5;K\"30\"\nC;Y5;X6;K\"25\"\nC;Y5;X7;K\"80\"\nC;Y5;X8;K\"10\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    edict_t *dead = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 0, 0);
    edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 40, 0);
    UnitAbilities_t attack_abilities = { .abilList = "ANmr,Aliq,Acor" };
    UnitAbilities_t death_abilities = { .abilList = "Adda" };
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Adda"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    attacker->data.UnitAbilities = &attack_abilities; dead->data.UnitAbilities = &death_abilities;
    attacker->s.player = dead->s.player = 0; target->s.player = nearby->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    target->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER;
    target->mana.value = target->mana.max_value = 100;
    target->health.value = target->health.max_value = 100;
    target->s.flags |= EF_BUILDING;
    target->targtype = TARG_STRUCTURE;
    nearby->health.value = nearby->health.max_value = 100;
    dead->die = unit_die; nearby->die = unit_die;
    S_CreepAttackOnHit(attacker, target);
    T_FEQ(target->mana.value, 83, 0.001f);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("Bliq")), 1);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("Bcor")), 1);
    T_FEQ(S_CreepAttackSpeedReduction(target), 0.25f, 0.001f);
    S_AbilityMessage(dead, A_DEATH, &call);
    T_FEQ(nearby->health.value, 90, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_bash_alias_uses_its_own_authored_attack_data) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"targs\"\n"
        "C;Y2;X1;K\"ACbh\"\nC;Y2;X2;K\"AHbh\"\nC;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"ground,enemy\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    UnitAbilities_t abilities = { .abilList = "ACbh" };

    attacker->data.UnitAbilities = &abilities; attacker->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE; target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND; target->health.value = target->health.max_value = 100;
    S_ResolveAttackHit(attacker, target, 10);
    T_FEQ(target->health.value, 83, 0.001f);
    T_EQ(G_UnitStatusLevel(target, FS_SLKKey("Bstu")), 1);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_slow_aura_reads_authored_move_and_attack_factors) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"Aasl\"\nC;Y2;X2;K\"Aasl\"\nC;Y2;X3;K\"ground,enemy\"\n"
        "C;Y2;X4;K\"300\"\nC;Y2;X5;K\"0.4\"\nC;Y2;X6;K\"0.25\"\nC;Y2;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    UnitAbilities_t abilities = { .abilList = "Aasl" };

    source->data.UnitAbilities = &abilities;
    source->s.player = 0; enemy->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    source->targtype = enemy->targtype = TARG_GROUND;
    T_FEQ(S_SlowAuraMoveReduction(enemy), 0.4f, 0.001f);
    T_FEQ(S_SlowAuraAttackReduction(enemy), 0.25f, 0.001f);
    T_FEQ(S_HumanMoveFactor(enemy), 0.6f, 0.001f);
    enemy->s.origin2.x = 400;
    T_FEQ(S_SlowAuraMoveReduction(enemy), 0.0f, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_command_and_war_drums_auras_share_damage_consumer) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"ACac\"\nC;Y2;X2;K\"ACac\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"300\"\nC;Y2;X5;K\"0.2\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"Aakb\"\nC;Y3;X2;K\"Aakb\"\nC;Y3;X3;K\"ground,friend\"\n"
        "C;Y3;X4;K\"300\"\nC;Y3;X5;K\"0.15\"\nC;Y3;X6;K\"1\"\n"
        "C;Y4;X1;K\"AOac\"\nC;Y4;X2;K\"AOac\"\nC;Y4;X3;K\"ground,friend\"\n"
        "C;Y4;X4;K\"300\"\nC;Y4;X5;K\"0.1\"\nC;Y4;X6;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    UnitAbilities_t abilities = { .abilList = "ACac,Aakb,AOac" };

    source->data.UnitAbilities = &abilities;
    source->s.player = ally->s.player = 0;
    source->targtype = ally->targtype = TARG_GROUND;
    T_FEQ(S_CommandAuraAttackBonus(ally), 0.2f, 0.001f);
    T_FEQ(S_WarDrumsAttackBonus(ally), 0.15f, 0.001f);
    ally->s.origin2.x = 400;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_CommandAuraAttackBonus(ally), 0.0f, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock creep Command Aura carries air,ground,friend,self and no `neutral`
 * token. The token describes a target that is neutral relative to the source
 * (another owner's Neutral-typed slot); the source's own slot type must not
 * turn its same-owner creeps into non-friends. */
TEST(wc3_spell, neutral_owned_aura_source_still_buffs_same_owner_creeps) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"ACac\"\nC;Y2;X2;K\"ACac\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"300\"\nC;Y2;X5;K\"0.2\"\nC;Y2;X6;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *fellow = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *sheep = alloc_test_unit(MAKEFOURCC('n','s','h','e'), 120, 0);
    edict_t *human = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 140, 0);
    UnitAbilities_t abilities = { .abilList = "ACac" };

    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_AGGRESSIVE].playerType = kPlayerTypeNeutral;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_PASSIVE].playerType = kPlayerTypeNeutral;
    source->data.UnitAbilities = &abilities;
    source->s.player = fellow->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    sheep->s.player = PLAYER_NEUTRAL_PASSIVE;
    human->s.player = 0;
    source->targtype = fellow->targtype = sheep->targtype = human->targtype = TARG_GROUND;

    T_FEQ(S_CommandAuraAttackBonus(fellow), 0.2f, 0.001f);
    T_FEQ(S_CommandAuraAttackBonus(source), 0.2f, 0.001f);
    /* Another owner's Neutral slot is still neutral relative to the creep, even
     * though Neutral Passive is passive-allied with every slot by default. */
    T_FEQ(S_CommandAuraAttackBonus(sheep), 0.0f, 0.001f);
    /* Neutral Hostile has no alliance with player slots. */
    T_FEQ(S_CommandAuraAttackBonus(human), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, hero_aura_aliases_honor_authored_target_masks) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"900\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"Biml\"\nC;Y3;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    target->unit_state->armor_value = 2.0f;

    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(target), 6.0f, 0.001f);
    target->targtype = TARG_MECHANICAL;
    level.time = AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(target), 2.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, aura_targets_follow_source_team_alliances_and_neutral_mask) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K900\nC;Y2;X5;K4\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K1\n"
        "C;Y3;X1;K\"YHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X3;K\"ground,neutral,organic\"\n"
        "C;Y3;X4;K900\nC;Y3;X5;K7\nC;Y3;X6;K\"Biml\"\nC;Y3;X7;K1\nE\n";
    uint32_t const ally_code = MAKEFOURCC('X','H','a','d');
    uint32_t const neutral_code = MAKEFOURCC('Y','H','a','d');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *neutral_source = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 2000, 200);
    edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
    edict_t *neutral = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 140, 0);
    edict_t *neutral_outside_mask = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 200);

    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[2].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[3].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_PASSIVE].playerType = kPlayerTypeNeutral;
    G_SetPlayerAlliance(&game.clients[1].ps, &game.clients[2].ps, ALLIANCE_PASSIVE, true);
    source->s.player = 1;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = ally_code, .level = 1);
    neutral_source->s.player = 1;
    neutral_source->svflags |= SVF_MONSTER;
    neutral_source->unit_state->ranks[0] = MAKE(heroability_t, .code = neutral_code, .level = 1);
    ally->s.player = 2;
    enemy->s.player = 3;
    neutral->s.player = PLAYER_NEUTRAL_PASSIVE;
    neutral->s.origin2.x = 2000;
    neutral_outside_mask->s.player = PLAYER_NEUTRAL_PASSIVE;
    ally->targtype = enemy->targtype = neutral->targtype = neutral_outside_mask->targtype = TARG_GROUND;
    ally->unit_state->armor_value = enemy->unit_state->armor_value = neutral->unit_state->armor_value = neutral_outside_mask->unit_state->armor_value = 2.0f;

    T_FEQ(S_DevotionArmorBonus(ally), 4.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(enemy), 0.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(neutral), 7.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(neutral_outside_mask), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Human02 and Human03 create these recipients under Player(PLAYER_NEUTRAL_PASSIVE).
 * Keep their campaign regression tied to Devotion's stock friend/self mask. */
static void devotion_aura_does_not_target_campaign_neutral(uint32_t hero_code, uint8_t owner, uint32_t unit_code) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area3\"\nC;Y1;X5;K\"DataA3\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AHad\"\n"
        "C;Y2;X3;K\"air,ground,friend,self,vuln,invu\"\n"
        "C;Y2;X4;K900\nC;Y2;X5;K3\nC;Y2;X6;K3\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *devotion = make_hero(hero_code, 500, 200, 0, 0);
    edict_t *recipient = alloc_test_unit(unit_code, 100, 0);
    edict_t *friendly = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);

    ((mapInfo_t *)level.mapinfo)->players[owner].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[PLAYER_NEUTRAL_PASSIVE].playerType = kPlayerTypeNeutral;
    devotion->s.player = friendly->s.player = owner;
    devotion->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','d'), .level = 3);
    recipient->s.player = PLAYER_NEUTRAL_PASSIVE;
    recipient->targtype = friendly->targtype = TARG_GROUND;

    T_FEQ(S_DevotionArmorBonus(friendly), 3.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(recipient), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, human02_devotion_aura_excludes_neutral_sheep) {
    devotion_aura_does_not_target_campaign_neutral(
        MAKEFOURCC('H','u','t','h'), 9, MAKEFOURCC('n','s','h','e'));
}

TEST(wc3_spell, human03_devotion_aura_excludes_neutral_villagers) {
    devotion_aura_does_not_target_campaign_neutral(
        MAKEFOURCC('H','a','r','t'), 1, MAKEFOURCC('n','v','i','l'));
}

TEST(wc3_spell, devotion_aura_does_not_affect_static_scenery) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Area3\"\nC;Y1;X5;K\"DataA3\"\nC;Y1;X6;K\"BuffID3\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K3\n"
        "C;Y2;X4;K900\nC;Y2;X5;K3\nC;Y2;X6;K\"Biml\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 40, 0);
    edict_t *crate = alloc_test_unit(MAKEFOURCC('L','T','c','r'), 50, 0);
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 60, 0);

    source->s.player = footman->s.player = 0;
    crate->s.player = tree->s.player = PLAYER_NEUTRAL_PASSIVE;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','d'), .level = 3);
    footman->targtype = TARG_GROUND;
    crate->targtype = TARG_DEBRIS; tree->targtype = TARG_TREE;
    crate->svflags |= SVF_STATIC_SCENERY; tree->svflags |= SVF_STATIC_SCENERY;
    G_AddDestructable(crate);
    G_AddDestructable(tree);

    T_FEQ(S_DevotionArmorBonus(footman), 3.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(crate), 0.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(tree), 0.0f, 0.001f);

    monster_think(footman); monster_think(crate); monster_think(tree);
    T_EQ(S_DevotionAuraBuff(footman), MAKEFOURCC('B','i','m','l'));
    T_EQ(S_DevotionAuraBuff(crate), 0);
    T_EQ(S_DevotionAuraBuff(tree), 0);
    uint32_t footman_overlays = 0, scenery_overlays = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->goalentity == footman) footman_overlays++;
        if (effect->goalentity == crate || effect->goalentity == tree) scenery_overlays++;
    }
    T_EQ(footman_overlays, 1); T_EQ(scenery_overlays, 0);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, roc_rank_three_devotion_uses_shared_target_mask) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"uberAlias\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Area3\"\nC;Y1;X6;K\"Data31\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K3\n"
        "C;Y2;X4;K\"air,ground,friend,self,vuln,invu\"\n"
        "C;Y2;X5;K900\nC;Y2;X6;K3\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 40, 0);
    edict_t *debris = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    source->s.player = footman->s.player = debris->s.player = 0;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','a','d'), .level = 3);
    footman->targtype = TARG_GROUND; debris->targtype = TARG_DEBRIS;
    T_FEQ(S_DevotionArmorBonus(footman), 3.0f, 0.001f);
    T_FEQ(S_DevotionArmorBonus(debris), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, auras_ignore_hidden_and_invisible_sources_and_recipients) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"900\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"Biml\"\nC;Y3;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);

    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    source->s.renderfx |= RF_HIDDEN;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    source->s.renderfx &= ~RF_HIDDEN;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    target->s.renderfx |= RF_HIDDEN;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    target->s.renderfx &= ~RF_HIDDEN;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    source->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    G_InitVisibility(source);
    source->vision->reveal_until = 0;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    source->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    target->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    G_InitVisibility(target);
    target->vision->reveal_until = 0;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    target->runtime.flags &= ~UNIT_BALANCE_PERMANENT_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    source->runtime.flags |= UNIT_BALANCE_GHOST_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    source->runtime.flags &= ~UNIT_BALANCE_GHOST_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    target->runtime.flags |= UNIT_BALANCE_GHOST_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    target->runtime.flags &= ~UNIT_BALANCE_GHOST_INVISIBLE;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    source->unit_state->overrides.added[0] = MAKEFOURCC('A', 's', 'h', 'm');
    ARRAY_COUNT(source->unit_state->overrides.added) = 1;
    shadowMeld_t *meld = G_AddShadowMeld(source);
    meld->active = true;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);
    meld->active = false;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 4.0f, 0.001f);

    target->unit_state->overrides.added[0] = MAKEFOURCC('A', 's', 'h', 'm');
    ARRAY_COUNT(target->unit_state->overrides.added) = 1;
    meld = G_AddShadowMeld(target);
    meld->active = true;
    level.time += AURA_UPDATE_MS;
    T_FEQ(S_DevotionArmorBonus(target), 0.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, devotion_aura_percent_bonus_uses_authored_base_defense) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"900\"\nC;Y2;X5;K\"0.4\"\nC;Y2;X6;K\"1\"\n"
        "C;Y2;X7;K\"Biml\"\nC;Y2;X8;K\"1\"\nE\n";
    static UnitBalance_t const target_balance = { .baseArmor = 5 };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    target->data.UnitBalance = &target_balance;
    target->unit_state->armor_value = 12.0f; /* runtime/real armor deliberately differs from authored def */

    T_FEQ(S_DevotionArmorBonus(target), 2.0f, 0.001f);
    T_FEQ(G_UnitArmorValue(target), 14.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, devotion_aura_recipient_presents_authored_buff_and_target_art) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"Biml\"\nC;Y3;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *overlay = NULL;

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    target->think = monster_think;

    level.framenum = 0; G_RunEntities();
    T_EQ(S_DevotionAuraBuff(target), MAKEFOURCC('B','i','m','l'));
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (effect->inuse && effect->owner == target && effect->goalentity == target &&
            effect->summon_ability == MAKEFOURCC('A','H','a','d')) {
            overlay = effect;
            break;
        }
    }
    T_NOT_NULL(overlay);
    T_ASSERT(overlay->s.model != 0);

    target->s.origin2.x = 501.0f;
    level.time = AURA_UPDATE_MS;
    level.framenum++; G_RunEntities();
    T_EQ(S_DevotionAuraBuff(target), 0);
    T_NULL(overlay->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, selected_devotion_recipient_invalidates_status_panel_on_aura_transition) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K900\nC;Y2;X5;K3\nC;Y2;X6;K\"Biml\"\nC;Y2;X7;K1\nE\n";
    slkTestData_t *rows, *old;
    edict_t *source;
    edict_t *target;
    edict_t *player = &g_edicts[0];

    reset_entities();
    setup_test_world();
    rows = parse_slk_string(slk);
    old = G_SetSLKRows("AbilityData", rows);
    source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    player->client = &game.clients[0];
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    target->think = monster_think;
    target->svflags |= SVF_MONSTER;
    target->inuse = true;
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    G_SelectEntity(&game.clients[0], target);
    game.clients[0].infopanel.entity = target->s.number;
    gi.Write = spell_panel_test_write;
    gi.unicast = spell_panel_test_unicast;

    level.time = 0;
    S_RunAbilityUpdates(target);
    T_EQ(S_DevotionAuraBuff(target), MAKEFOURCC('B','i','m','l'));
    T_EQ(game.clients[0].infopanel.entity, UINT32_MAX);
    G_RefreshInfoPanel(player);
    T_EQ(game.clients[0].infopanel.entity, target->s.number);

    level.time = AURA_UPDATE_MS;
    S_RunAbilityUpdates(target);
    T_EQ(S_DevotionAuraBuff(target), MAKEFOURCC('B','i','m','l'));
    T_EQ(game.clients[0].infopanel.entity, target->s.number);

    target->s.origin2.x = 2000;
    level.time = AURA_UPDATE_MS * 2;
    S_RunAbilityUpdates(target);
    T_EQ(S_DevotionAuraBuff(target), 0);
    T_EQ(game.clients[0].infopanel.entity, UINT32_MAX);
    G_RefreshInfoPanel(player);
    T_EQ(game.clients[0].infopanel.entity, target->s.number);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* TFT AHad.BuffID is BHad. BHad.TargetArt is the soft recipient glow;
 * AHad.TargetArt is the rune that stays on the aura source. */
TEST(wc3_spell, devotion_aura_source_shows_pattern_and_recipient_keeps_glow) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"BHad\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"BHad\"\nC;Y3;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    cstring_t pattern_art = G_AbilityEffectArt(MAKEFOURCC('A','H','a','d'), WC3_EFFECT_TARGET, 0);
    cstring_t glow_art = G_AbilityEffectArt(MAKEFOURCC('B','H','a','d'), WC3_EFFECT_TARGET, 0);
    int pattern = G_RegisterModel(pattern_art);
    int glow = G_RegisterModel(glow_art);
    int source_pattern, source_glow, target_pattern, target_glow;

    T_NOT_NULL(pattern_art);
    T_NOT_NULL(glow_art);
    T_ASSERT(pattern > 0 && glow > 0 && pattern != glow);
    level.time = level.framenum = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    source->think = target->think = monster_think;

    G_RunEntities();
    source_pattern = source_glow = target_pattern = target_glow = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->goalentity != effect->owner ||
            effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->owner == source && effect->s.model == pattern) source_pattern++;
        if (effect->owner == source && effect->s.model == glow) source_glow++;
        if (effect->owner == target && effect->s.model == pattern) target_pattern++;
        if (effect->owner == target && effect->s.model == glow) target_glow++;
    }
    T_EQ(source_pattern, 1);
    T_EQ(source_glow, 1);
    T_EQ(target_pattern, 0);
    T_EQ(target_glow, 1);

    /* A second refresh rebinds live effects and must not stack another copy. */
    level.time = AURA_UPDATE_MS;
    level.framenum++;
    G_RunEntities();
    source_pattern = source_glow = target_glow = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->goalentity != effect->owner ||
            effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->owner == source && effect->s.model == pattern) source_pattern++;
        if (effect->owner == source && effect->s.model == glow) source_glow++;
        if (effect->owner == target && effect->s.model == glow) target_glow++;
    }
    T_EQ(source_pattern, 1);
    T_EQ(source_glow, 1);
    T_EQ(target_glow, 1);

    target->s.origin2.x = 2000.0f;
    level.time = AURA_UPDATE_MS * 2;
    level.framenum++;
    G_RunEntities();
    target_glow = source_pattern = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->owner == target && effect->goalentity == target) target_glow++;
        if (effect->owner == source && effect->s.model == pattern && effect->goalentity == source) source_pattern++;
    }
    T_EQ(target_glow, 0);
    T_EQ(source_pattern, 1);

    source->unit_state->ranks[0].level = 0;
    level.time = AURA_UPDATE_MS * 3;
    level.framenum++;
    G_RunEntities();
    source_pattern = source_glow = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (!effect->inuse || effect->owner != source || effect->goalentity != source ||
            effect->summon_ability != MAKEFOURCC('A','H','a','d')) continue;
        if (effect->s.model == pattern) source_pattern++;
        if (effect->s.model == glow) source_glow++;
    }
    T_EQ(source_pattern, 0);
    T_EQ(source_glow, 0);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Count attached presentation effects by model and owner. */
static uint32_t hero_aura_art_count(edict_t *unit, uint32_t base, uint32_t model) {
    uint32_t count = 0;
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *fx = g_edicts + i;
        if (fx->inuse && fx->owner == unit && fx->goalentity == unit &&
            fx->summon_ability == base && (uint32_t)fx->s.model == model) count++;
    }
    return count;
}

TEST(wc3_spell, vampiric_aura_status_only_applies_to_melee_units) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"AUav\"\nC;Y2;X2;K\"AUav\"\nC;Y2;X3;K\"ground,air,friend,self,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.2\"\nC;Y2;X6;K\"BUav\"\nC;Y2;X7;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *melee = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *ranged = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 100, 0);
    source->s.player = melee->s.player = ranged->s.player = 0;
    source->targtype = melee->targtype = ranged->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','v'), .level = 1);
    melee->unit_state->attack1.type = ATK_NORMAL;
    melee->unit_state->attack1.weapon = WPN_NORMAL;
    melee->unit_state->attack1.range = 90.0f;
    ranged->unit_state->attack1.type = ATK_PIERCE;
    ranged->unit_state->attack1.weapon = WPN_INSTANT;
    ranged->unit_state->attack1.range = 400.0f;

    S_UpdateHeroAuraEffects(melee);
    S_UpdateHeroAuraEffects(ranged);
    T_EQ(S_VampiricAuraBuff(melee), MAKEFOURCC('B','U','a','v'));
    T_EQ(S_VampiricAuraBuff(ranged), 0u);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

#if BZ_COMPAT_WORKAROUNDS
/* RoC omits BuffID on Cairne's AOr2 alias. Resolve recipients to BOae's
 * authored buff art so they receive the glow/icon, not AOae's caster rune. */
TEST(wc3_spell, endurance_aura_roc_compat_uses_boae_recipient_art) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\nC;Y1;X7;K\"TargetArt\"\n"
        "C;Y2;X1;K\"AOr2\"\nC;Y2;X2;K\"AOae\"\nC;Y2;X3;K\"ground,air,friend,self\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.3\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"TestUI\\\\Models\\\\caster_pattern.mdx\"\n"
        "C;Y3;X1;K\"AOae\"\nC;Y3;X2;K\"AOae\"\nC;Y3;X6;K\"1\"\nC;Y3;X7;K\"TestUI\\\\Models\\\\caster_pattern.mdx\"\nE\n";
    const char buff_slk[] =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"TargetArt\"\nC;Y1;X4;K\"Buffart\"\n"
        "C;Y2;X1;K\"BOae\"\nC;Y2;X2;K\"BOae\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
        "C;Y2;X4;K\"ReplaceableTextures\\\\CommandButtons\\\\BTNEnduranceAura.blp\"\n"
        "C;Y3;X1;K\"AOae\"\nC;Y3;X2;K\"AOae\"\n"
        "C;Y3;X3;K\"TestUI\\\\Models\\\\caster_pattern.mdx\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOr2" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    edict_t *source = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
    uint32_t const base = MAKEFOURCC('A','O','a','e');
    uint32_t const buff = MAKEFOURCC('B','O','a','e');
    int const pattern = G_RegisterModel(G_AbilityEffectArt(base, WC3_EFFECT_TARGET, 0));
    int const glow = G_RegisterModel(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));

    T_ASSERT(pattern > 0 && glow > 0 && pattern != glow);
    source->data.UnitAbilities = &abilities;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->think = target->think = monster_think;
    level.time = level.framenum = 0;
    G_RunEntities();
    T_EQ(S_EnduranceAuraBuff(target), buff);
    T_EQ(hero_aura_art_count(target, base, pattern), 0);
    T_EQ(hero_aura_art_count(target, base, glow), 1);

    G_SetSLKRows("AbilityData", old);
    G_SetSLKRows("AbilityBuffData", old_buff);
    free_slk_rows(rows);
    free_slk_rows(buff_rows);
}

/* RoC omits BuffID; the compatibility table resolves stock AHad to BHad,
 * preserving separate caster pattern and recipient glow. */
TEST(wc3_spell, devotion_aura_roc_compat_uses_bhad_recipient_art) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"XHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"1\"\nE\n";
    const char buff_slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\n"
        "C;Y2;X1;K\"BHad\"\nC;Y2;X2;K\"BHad\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff;
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    uint32_t base = MAKEFOURCC('A','H','a','d');
    uint32_t buff = MAKEFOURCC('B','H','a','d');
    int pattern = G_RegisterModel(G_AbilityEffectArt(base, WC3_EFFECT_TARGET, 0));
    int glow;

    old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    glow = G_RegisterModel(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));

    T_ASSERT(pattern > 0);
    T_ASSERT(glow > 0);
    T_NE(pattern, glow);
    level.time = level.framenum = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','H','a','d'), .level = 1);
    source->think = target->think = monster_think;

    G_RunEntities();
    T_EQ(hero_aura_art_count(source, base, pattern), 1);
    T_EQ(hero_aura_art_count(source, base, glow), 1);
    T_EQ(hero_aura_art_count(target, base, glow), 1);
    T_EQ(S_DevotionAuraBuff(source), buff);
    T_EQ(S_DevotionAuraBuff(target), buff);

    level.time = AURA_UPDATE_MS;
    level.framenum++;
    G_RunEntities();
    T_EQ(hero_aura_art_count(source, base, pattern), 1);
    T_EQ(hero_aura_art_count(source, base, glow), 1);
    T_EQ(hero_aura_art_count(target, base, glow), 1);

    G_SetSLKRows("AbilityData", old);
    G_SetSLKRows("AbilityBuffData", old_buff);
    free_slk_rows(rows);
    free_slk_rows(buff_rows);
}
#else
/* With compatibility workarounds disabled, missing BuffID keeps the generic
 * ability-art recipient fallback. */
TEST(wc3_spell, devotion_aura_without_compat_uses_ability_art_fallback) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AHad\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"1\"\n"
        "C;Y3;X1;K\"AHad\"\nC;Y3;X2;K\"AHad\"\nC;Y3;X6;K\"1\"\nE\n";
    uint32_t const base = MAKEFOURCC('A','H','a','d');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    int const model = G_RegisterModel(G_AbilityEffectArt(base, WC3_EFFECT_TARGET, 0));

    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = base, .level = 1);
    source->think = target->think = monster_think;
    G_RunEntities();

    T_EQ(hero_aura_art_count(source, base, model), 1);
    T_EQ(hero_aura_art_count(target, base, model), 1);
    T_EQ(S_DevotionAuraBuff(source), 0);
    T_EQ(S_DevotionAuraBuff(target), 0);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}
#endif

/* Removing a custom alias must retire its rune while a nearby aura keeps the recipient glow alive. */
static void custom_hero_aura_removal(cstring_t base, cstring_t alias, cstring_t buff) {
    static cstring_t const format =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"%s\"\nC;Y2;X2;K\"%s\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"%s\"\nC;Y2;X7;K\"1\"\n"
        "C;Y3;X1;K\"%s\"\nC;Y3;X2;K\"%s\"\nC;Y3;X6;K\"%s\"\nC;Y3;X7;K\"1\"\nE\n";
    char slk[1024];
    uint32_t const code = FS_SLKKey(base), rawcode = FS_SLKKey(alias);
    cstring_t path = Test_TempPath("custom-hero-aura.bin");

    snprintf(slk, sizeof(slk), format, alias, base, buff, base, base, buff);
    FOR_LOOP(saved, 2) {
        slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
        edict_t *source = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
        edict_t *other = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 100, 0);
        uint32_t pattern = G_RegisterModel(G_AbilityEffectArt(rawcode, WC3_EFFECT_TARGET, 0));
        uint32_t glow = G_RegisterModel(G_AbilityEffectArt(FS_SLKKey(buff), WC3_EFFECT_TARGET, 0));

        T_ASSERT(pattern && glow && pattern != glow);
        T_NE(pattern, (uint32_t)G_RegisterModel(G_AbilityEffectArt(code, WC3_EFFECT_TARGET, 0)));
        level.time = level.framenum = 0;
        source->s.player = other->s.player = 0;
        source->targtype = other->targtype = TARG_GROUND;
        other->svflags |= SVF_MONSTER;
        T_ASSERT(G_ActorAddSkill(source, rawcode));
        T_ASSERT(G_ActorAddSkill(other, rawcode));
        source->think = other->think = monster_think;
        G_RunEntities();
        T_EQ(hero_aura_art_count(source, code, pattern), 1);
        T_EQ(hero_aura_art_count(source, code, glow), 1);

        if (saved) {
            T_ASSERT(WriteGame(path));
            G_ResetHeroPassiveCaches();
            T_ASSERT(ReadGame(path));
            remove(path);
        }
        T_ASSERT(G_ActorRemoveSkill(source, rawcode));
        FOR_LOOP(step, 3) {
            level.time += AURA_UPDATE_MS; level.framenum++;
            G_RunEntities();
            T_EQ(hero_aura_art_count(source, code, pattern), 0);
            T_EQ(hero_aura_art_count(source, code, glow), 1);
            T_EQ(hero_aura_art_count(other, code, pattern), 1);
        }
        T_ASSERT(G_ActorRemoveSkill(other, rawcode));
        level.time += AURA_UPDATE_MS; level.framenum++;
        G_RunEntities();
        T_EQ(hero_aura_art_count(source, code, glow), 0);
        T_EQ(hero_aura_art_count(other, code, pattern), 0);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

TEST(wc3_spell, devotion_aura_custom_art_is_removed_while_another_source_keeps_glow) {
    custom_hero_aura_removal("AHad", "XHfx", "BHad");
}

TEST(wc3_spell, unholy_aura_custom_art_is_removed_while_another_source_keeps_glow) {
    custom_hero_aura_removal("AUau", "XUfx", "BUau");
}

TEST(wc3_spell, unholy_aura_percent_regen_and_recipient_presentation) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"levels\"\n"
        "C;Y2;X1;K\"XUau\"\nC;Y2;X2;K\"AUau\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.1\"\nC;Y2;X6;K\"0.01\"\n"
        "C;Y2;X7;K\"1\"\nC;Y2;X8;K\"Biml\"\nC;Y2;X9;K\"1\"\n"
        "C;Y3;X1;K\"AUau\"\nC;Y3;X2;K\"AUau\"\nC;Y3;X8;K\"Biml\"\nC;Y3;X9;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 100, 0);
    edict_t *overlay = NULL;

    level.time = 0;
    source->s.player = target->s.player = 0;
    source->targtype = target->targtype = TARG_GROUND;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('X','U','a','u'), .level = 1);
    target->health.max_value = 1000.0f; target->health.value = 500.0f;
    target->think = monster_think;

    T_FEQ(S_UnholyMoveBonus(target), 0.1f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(target), 10.0f, 0.001f);
    level.framenum = 0; G_RunEntities();
    T_EQ(S_UnholyAuraBuff(target), MAKEFOURCC('B','i','m','l'));
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *effect = g_edicts + i;
        if (effect->inuse && effect->owner == target && effect->goalentity == target &&
            effect->summon_ability == MAKEFOURCC('A','U','a','u')) {
            overlay = effect;
            break;
        }
    }
    T_NOT_NULL(overlay);
    T_ASSERT(overlay->s.model != 0);

    target->s.origin2.x = 501.0f;
    level.time = AURA_UPDATE_MS;
    level.framenum++; G_RunEntities();
    T_FEQ(S_UnholyMoveBonus(target), 0.0f, 0.001f);
    T_FEQ(S_UnholyHealthRegen(target), 0.0f, 0.001f);
    T_EQ(S_UnholyAuraBuff(target), 0);
    T_NULL(overlay->goalentity);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, combat_aura_alias_resolution_scales_with_edicts) {
    enum { AURA_SOURCES = 32, AURA_TARGETS = 96 };
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"levels\"\n"
        "C;Y2;X1;K\"XUau\"\nC;Y2;X2;K\"AUau\"\nC;Y2;X3;K\"ground,friend,organic\"\n"
        "C;Y2;X4;K\"500\"\nC;Y2;X5;K\"0.1\"\nC;Y2;X6;K\"0.01\"\n"
        "C;Y2;X7;K\"1\"\nC;Y2;X8;K\"Biml\"\nC;Y2;X9;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "XUau" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *sources[AURA_SOURCES], *targets[AURA_TARGETS];

    reset_entities();
    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    level.time = level.framenum = 0;
    FOR_LOOP(i, AURA_SOURCES) {
        sources[i] = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
        sources[i]->data.UnitAbilities = &abilities;
        sources[i]->s.player = 0;
        sources[i]->targtype = TARG_GROUND;
    }
    FOR_LOOP(i, AURA_TARGETS) {
        targets[i] = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 0, 0);
        targets[i]->s.player = 0;
        targets[i]->targtype = TARG_GROUND;
        targets[i]->health.max_value = 1000.0f;
        targets[i]->health.value = 500.0f;
    }

    S_TestResetHeroAuraAliasResolves();
    FOR_LOOP(i, AURA_TARGETS)
        T_FEQ(S_UnholyHealthRegen(targets[i]), 10.0f, 0.001f);
    /* A single ownership walk per in-use provider, not 14 independent scans. */
    T_ASSERT(S_TestHeroAuraAliasResolves() > 0);
    T_ASSERT(S_TestHeroAuraAliasResolves() <= globals.num_edicts);

    FOR_LOOP(i, AURA_SOURCES) T_ASSERT(G_ActorRemoveSkill(sources[i], MAKEFOURCC('X','U','a','u')));
    level.time = AURA_UPDATE_MS; level.framenum++;
    T_FEQ(S_UnholyHealthRegen(targets[0]), 0.0f, 0.001f);
    T_ASSERT(G_ActorAddSkill(sources[0], MAKEFOURCC('X','U','a','u')));
    level.time += AURA_UPDATE_MS; level.framenum++;
    T_FEQ(S_UnholyHealthRegen(targets[0]), 10.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, aura_provider_discovery_preserves_alias_precedence_and_ranks) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X2\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"XUau\"\nC;Y2;X2;K\"AUau\"\n"
        "C;Y3;X1;K\"XHad\"\nC;Y3;X2;K\"AHad\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "XUau,XHad" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *source;

    reset_entities();
    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    source->data.UnitAbilities = &abilities;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','u'), .level = 3);
    source->unit_state->ranks[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','a','s','l'), .level = 2);
    source->unit_state->overrides.added[0] = MAKEFOURCC('A','C','a','c');
    source->unit_state->overrides.added_count = 1;

    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','U','a','u')).alias,
         MAKEFOURCC('X','U','a','u'));
    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','U','a','u')).level, 1);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));

    /* A removed native alias falls back to a ranked hero ability. */
    source->unit_state->overrides.removed[0] = MAKEFOURCC('X','U','a','u');
    source->unit_state->overrides.removed_count = 1;
    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','U','a','u')).level, 3);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));

    /* Removing the added ability exposes the still-learned hero and native
     * entries; an unrelated direct list edit must not leave a stale snapshot. */
    source->unit_state->overrides.added[0] = 0;
    source->unit_state->overrides.added_count = 0;
    source->data.UnitAbilities = NULL;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->unit_state->ranks[0].level = 0;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Unrelated native/added abilities must not trigger expensive live ownership
 * checks. Positive and negative classifications are shared between passes,
 * while removals and hero rank changes are evaluated afresh. */
TEST(wc3_spell, aura_classification_cache_filters_unrelated_ownership_checks) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"XUau\"\nC;Y2;X2;K\"AUau\"\n"
        "C;Y3;X1;K\"XHad\"\nC;Y3;X2;K\"AHad\"\n"
        "C;Y4;X1;K\"Afoo\"\nC;Y4;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Afoo,XUau" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *source;
    uint32_t class_resolves;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    source->data.UnitAbilities = &abilities;
    source->unit_state->overrides.added[0] = MAKEFOURCC('X','H','a','d');
    source->unit_state->overrides.added_count = 1;
    source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','U','a','u'), .level = 3);
    S_TestResetAuraClassificationCache();
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraOwnershipChecks(), 2u); /* Native XUau, added XHad only. */
    class_resolves = S_TestAuraClassResolves();
    T_ASSERT(class_resolves >= 3u);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), class_resolves); /* Reused both sorts of cache hit. */
    T_EQ(S_TestAuraOwnershipChecks(), 4u);

    source->unit_state->overrides.removed[0] = MAKEFOURCC('X','U','a','u');
    source->unit_state->overrides.removed_count = 1;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->data.UnitAbilities = NULL;
    source->unit_state->overrides.added[0] = 0;
    source->unit_state->overrides.added_count = 0;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->unit_state->ranks[0].level = 0;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Classification for the same rawcode must flip in both directions after
 * AbilityData replacement, including when an earlier result was negative. */
TEST(wc3_spell, aura_classification_cache_invalidates_on_metadata_generation) {
    const char aura_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"AUau\"\nE\n";
    const char other_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AqW1" };
    slkTestData_t *aura = parse_slk_string(aura_slk);
    slkTestData_t *other = parse_slk_string(other_slk);
    slkTestData_t *old;
    edict_t *source;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", aura);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    source->data.UnitAbilities = &abilities;
    S_TestResetAuraClassificationCache();
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), 1u);
    G_SetSLKRows("AbilityData", other);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), 2u);
    G_SetSLKRows("AbilityData", aura);
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), 3u);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(aura);
    free_slk_rows(other);
}

/* Hash collisions must compare full rawcodes, not just slot/generation.
 * AqW1 and Agha collide under the production 256-entry hash. */
TEST(wc3_spell, aura_classification_cache_checks_hash_collisions) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"AUau\"\n"
        "C;Y3;X1;K\"Agha\"\nC;Y3;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t aura = { .abilList = "AqW1" }, other = { .abilList = "Agha" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *source;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    S_TestResetAuraClassificationCache();
    source->data.UnitAbilities = &aura;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->data.UnitAbilities = &other;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    source->data.UnitAbilities = &aura;
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_TestAuraClassResolves(), 3u);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* An alias can itself be an aura rawcode while inheriting a different aura
 * base. The old resolver matched both; classification must preserve both. */
TEST(wc3_spell, aura_classification_cache_preserves_two_family_rawcode) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AHad\"\nC;Y2;X2;K\"AUau\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHad" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *source;
    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    source = alloc_test_unit(MAKEFOURCC('U','d','e','a'), 0, 0);
    source->data.UnitAbilities = &abilities;
    S_TestResetAuraClassificationCache();
    T_ASSERT(S_TestAuraDiscoveryMatchesResolver(source));
    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','H','a','d')).alias,
         MAKEFOURCC('A','H','a','d'));
    T_EQ(S_ResolveAbilityAlias(source, MAKEFOURCC('A','U','a','u')).alias,
         MAKEFOURCC('A','H','a','d'));
    T_EQ(S_TestAuraOwnershipChecks(), 1u);
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
	edict_t *health_source = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	edict_t *mana_source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	edict_t *blight_source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

	G_InitUnitOrders(health_source);

	health_source->svflags |= SVF_MONSTER; mana_source->svflags |= SVF_MONSTER;
	blight_source->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	UnitAbilities_t static_abilities = { .abilList = "ACnr" };
	health_source->s.player = mana_source->s.player = blight_source->s.player = target->s.player = 0;
	health_source->targtype = mana_source->targtype = blight_source->targtype = target->targtype = TARG_GROUND;
	health_source->data.UnitAbilities = &static_abilities;
	mana_source->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','r','e'), .level = 1);
	blight_source->unit_state->overrides.added[0] = MAKEFOURCC('A','a','b','r'); ARRAY_COUNT(blight_source->unit_state->overrides.added) = 1;
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
	edict_t *first = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	edict_t *second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);

	G_InitUnitOrders(first);

	first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	first->s.player = second->s.player = target->s.player = 0;
	first->targtype = second->targtype = target->targtype = TARG_GROUND;
	first->unit_state->overrides.added[0] = MAKEFOURCC('A','C','n','1'); ARRAY_COUNT(first->unit_state->overrides.added) = 1;
	second->unit_state->overrides.added[0] = MAKEFOURCC('A','C','n','2'); ARRAY_COUNT(second->unit_state->overrides.added) = 1;
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
	edict_t *source = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *overlay = NULL;

	source->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	source->s.player = target->s.player = 0;
	source->targtype = target->targtype = TARG_GROUND;
	source->unit_state->overrides.added[0] = MAKEFOURCC('A','C','n','r');
	ARRAY_COUNT(source->unit_state->overrides.added) = 1;
	target->health.max_value = 1000.0f;
	target->health.value = 900.0f;

	S_UpdateRegenerationAuraEffects(target);
	FOR_LOOP(i, globals.num_edicts) {
		edict_t *effect = g_edicts + i;
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
		edict_t *effect = g_edicts + i;
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
	static cstring_t const codes[] = { "Aoar", "Aabr", "Aarm" };
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
	edict_t *aura = make_hero(MAKEFOURCC('E', 'd', 'r', 'u'), 500, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h', 'f', 'o', 'o'), 100, 0);
	edict_t *attacker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 100, 0);
	level.time = 0;
	aura->s.player = target->s.player = attacker->s.player = 0;
	aura->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A', 'E', 'a', 'h'), .level = 1);
	target->unit_state->attack1.weapon = WPN_NORMAL;
	attacker->unit_state->attack1.weapon = WPN_NORMAL;
	T_FEQ(S_ThornsDamageReturn(target, target, 100.0f), 10.0f, 0.001f);
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 10.0f, 0.001f);
	attacker->unit_state->attack1.weapon = WPN_MISSILE;
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 0.0f, 0.001f);
	attacker->unit_state->attack1.weapon = WPN_NORMAL;
	target->s.origin2.x = 901.0f;
	level.time = AURA_UPDATE_MS;
	T_FEQ(S_ThornsDamageReturn(target, attacker, 100.0f), 0.0f, 0.001f);
	ability_t const *ability = FindAbilityByClassname("AEah");
	T_NOT_NULL(ability);
	T_ASSERT(ability->flags & AB_PASSIVE);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, thorns_aura_shows_recipient_status_and_buff_art) {
	const char ability_slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\n"
		"C;Y1;X7;K\"levels\"\n"
		"C;Y2;X1;K\"AEah\"\nC;Y2;X2;K\"AEah\"\nC;Y2;X3;K\"ground,air,friend,self,organic\"\n"
		"C;Y2;X4;K900\nC;Y2;X5;K0.1\nC;Y2;X6;K\"BEah\"\nC;Y2;X7;K1\nE\n";
	const char buff_slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\nC;Y1;X4;K\"Buffart\"\n"
		"C;Y2;X1;K\"BEah\"\nC;Y2;X2;K\"BEah\"\n"
		"C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
		"C;Y2;X4;K\"ReplaceableTextures\\\\CommandButtons\\\\BTNThornsAura.blp\"\nE\n";
	slkTestData_t *rows = parse_slk_string(ability_slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
	edict_t *source = make_hero(MAKEFOURCC('E','d','r','u'), 500, 200, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	uint32_t const base = MAKEFOURCC('A','E','a','h');
	uint32_t const buff = MAKEFOURCC('B','E','a','h');
	int const glow = G_RegisterModel(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));

	T_NOT_NULL(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));
	T_ASSERT(glow > 0);
	source->s.player = target->s.player = 0;
	source->targtype = target->targtype = TARG_GROUND;
	source->unit_state->ranks[0] = MAKE(heroability_t, .code = base, .level = 1);
	level.time = level.framenum = 0;
	S_UpdateHeroAuraEffects(target);
	T_EQ(S_ThornsAuraBuff(target), buff);
	T_EQ(hero_aura_art_count(target, base, glow), 1);

	target->s.origin2.x = 901.0f;
	level.time = AURA_UPDATE_MS;
	S_UpdateHeroAuraEffects(target);
	T_EQ(S_ThornsAuraBuff(target), 0u);
	T_EQ(hero_aura_art_count(target, base, glow), 0);

	G_SetSLKRows("AbilityData", old);
	G_SetSLKRows("AbilityBuffData", old_buff);
	free_slk_rows(rows);
	free_slk_rows(buff_rows);
}

TEST(wc3_spell, trueshot_aura_shows_recipient_status_art_and_ranged_bonus) {
	const char ability_slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
		"C;Y2;X1;K\"AEar\"\nC;Y2;X2;K\"AEar\"\nC;Y2;X3;K\"air,ground,friend,self,vuln,invu\"\n"
		"C;Y2;X4;K900\nC;Y2;X5;K0.1\nC;Y2;X6;K\"BEar\"\nC;Y2;X7;K1\nE\n";
	const char buff_slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\nC;Y1;X4;K\"Buffart\"\n"
		"C;Y2;X1;K\"BEar\"\nC;Y2;X2;K\"BEar\"\n"
		"C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
		"C;Y2;X4;K\"ReplaceableTextures\\\\CommandButtons\\\\BTNAEar.blp\"\nE\n";
	slkTestData_t *rows = parse_slk_string(ability_slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
	edict_t *source = make_hero(MAKEFOURCC('E','d','r','u'), 500, 200, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 100, 0);
	edict_t *melee = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 100);
	edict_t *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
	uint32_t const base = MAKEFOURCC('A','E','a','r');
	uint32_t const buff = MAKEFOURCC('B','E','a','r');
	int const glow = G_RegisterModel(G_AbilityEffectArt(buff, WC3_EFFECT_TARGET, 0));

	T_ASSERT(glow > 0);
	source->s.player = target->s.player = melee->s.player = 0;
	victim->s.player = 1;
	source->targtype = target->targtype = melee->targtype = victim->targtype = TARG_GROUND;
	source->unit_state->ranks[0] = MAKE(heroability_t, .code = base, .level = 1);
	target->unit_state->attack1.type = ATK_PIERCE;
	target->unit_state->attack1.weapon = WPN_INSTANT;
	target->unit_state->attack1.range = 400.0f;
	melee->unit_state->attack1.type = ATK_NORMAL;
	melee->unit_state->attack1.weapon = WPN_NORMAL;
	melee->unit_state->attack1.range = 90.0f;
	level.time = level.framenum = 0;
	S_UpdateHeroAuraEffects(target);
	S_UpdateHeroAuraEffects(melee);
	T_EQ(S_TrueshotAuraBuff(target), buff);
	T_FEQ(S_TrueshotAttackBonus(target), 0.1f, 0.001f);
	T_EQ(hero_aura_art_count(target, base, glow), 1);
	T_EQ(S_TrueshotAuraBuff(melee), 0u);
	T_FEQ(S_TrueshotAttackBonus(melee), 0.0f, 0.001f);
	/* Exercise the landed-hit damage path: a 100 damage ranged hit gains 10%.
	 * The victim has no armor, so its health delta is the resolved damage. */
	victim->unit_state->armor_value = 0.0f;
	{
		float const health_before = victim->health.value;
		S_ResolveAttackHit(target, victim, 100);
		T_FEQ(health_before - victim->health.value, 110.0f, 0.001f);
	}

	target->unit_state->attack1.type = ATK_NORMAL;
	target->unit_state->attack1.weapon = WPN_NORMAL;
	target->unit_state->attack1.range = 90.0f;
	T_FEQ(S_TrueshotAttackBonus(target), 0.0f, 0.001f);
	S_UpdateHeroAuraEffects(target);
	T_EQ(S_TrueshotAuraBuff(target), 0u);
	{
		float const health_before = victim->health.value;
		S_ResolveAttackHit(target, victim, 100);
		T_FEQ(health_before - victim->health.value, 100.0f, 0.001f);
	}
	target->s.origin2.x = 901.0f;
	level.time = AURA_UPDATE_MS;
	S_UpdateHeroAuraEffects(target);
	T_EQ(S_TrueshotAuraBuff(target), 0u);
	T_EQ(hero_aura_art_count(target, base, glow), 0);

	G_SetSLKRows("AbilityData", old);
	G_SetSLKRows("AbilityBuffData", old_buff);
	free_slk_rows(rows);
	free_slk_rows(buff_rows);
}

TEST(wc3_spell, requested_thirty_have_concrete_handlers) {
	static cstring_t const rawcodes[] = {
		"AHab", "AHmt", "ANst", "ANsg", "ANsq", "ANsw", "AOww", "AOcr", "AHbn", "AHfs",
		"AHdr", "AHpx", "AUcb", "AUim", "AUls", "AUts", "ANba", "ANsi", "AUan", "AUdc",
		"AUdp", "AUau", "AEev", "AEme", "AUsl", "AUav", "AUin", "AOcl", "AOeq", "AOfs",
	};
	static uint32_t const passives[] = {
		MAKEFOURCC('A','H','a','b'), MAKEFOURCC('A','O','c','r'), MAKEFOURCC('A','U','t','s'),
		MAKEFOURCC('A','U','a','u'), MAKEFOURCC('A','E','e','v'), MAKEFOURCC('A','U','a','v'),
	};

	FOR_LOOP(i, sizeof(rawcodes) / sizeof(rawcodes[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(rawcodes[i]));
		ability_t const *ability = ability_item.ability;
		uint32_t code = MAKEFOURCC(rawcodes[i][0], rawcodes[i][1], rawcodes[i][2], rawcodes[i][3]);
		bool passive = false;
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
	static cstring_t const rawcodes[] = {
		"Aamk", "ACtn", "ANav", "ANsh", "AOw2", "ACs7", "ACs8", "ANr2", "Afbb", "Andm",
		"Asb1", "Asb2", "Asb3", "ANha", "ANen", "ACfu", "ANpa", "Acny", "Ahnl", "Arsq",
		"Arsg", "Arsp", "ANbr", "ANsb", "ANcf", "Acdh", "Acef", "ANhw", "ANhx", "Arsw",
		"AOs2", "AOr2", "AOr3", "AOls",
	};

	FOR_LOOP(i, sizeof(rawcodes) / sizeof(rawcodes[0])) {
		abilityitem_t ability_item = S_AbilityItem(FS_SLKKey(rawcodes[i]));
		ability_t const *ability = ability_item.ability;
		uint32_t code = MAKEFOURCC(rawcodes[i][0], rawcodes[i][1], rawcodes[i][2], rawcodes[i][3]);
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
	edict_t *caster = make_hero(MAKEFOURCC('u','d','e','a'), 200, 300, 0, 0);
	edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *third = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 700, 0);
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	caster->s.player = 0; caster->health.max_value = 500; caster->health.value = 100;
	first->s.player = second->s.player = third->s.player = 1;
	first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; third->svflags |= SVF_MONSTER;
	first->die = unit_die; second->die = unit_die; third->die = unit_die;
	first->health.value = first->health.max_value = 500;
	second->health.value = second->health.max_value = 500;
	third->health.value = third->health.max_value = 500;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

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
	/* Banish makes the first target Ethereal, so Chain Lightning deals 166% damage. */
	T_ASSERT(unit_hasstatusstate(first, WC3_STATUS_STATE_ETHEREAL));
	T_FEQ(first->health.value, 333.0f, 0.001f);
	T_FEQ(second->health.value, 500.0f, 0.001f);
	{
		edict_t *thinker = NULL;
		FILTER_EDICTS(ent, ent->think == chain_lightning_think) { thinker = ent; break; }
		T_NOT_NULL(thinker);
		level.time = thinker->freetime;
		chain_lightning_think(thinker);
		T_FEQ(second->health.value, 450.0f, 0.001f);
	}

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

TEST(wc3_spell, simple_ability_summons_record_owner_ability_and_timed_life) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"Dur1\"\n"
        "C;Y1;X5;K\"UnitID1\"\n"
        "C;Y1;X6;K\"targs\"\n"
        "C;Y2;X1;K\"A0S1\"\n"
        "C;Y2;X2;K\"A0S1\"\n"
        "C;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"7\"\n"
        "C;Y2;X5;K\"hfoo\"\n"
        "C;Y2;X6;K\"ground\"\n"
        "E\n";
    uint32_t const code = MAKEFOURCC('A','0','S','1');
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = { 128.0f, 64.0f });
    uint32_t count = 0;

    S_SummonAbilityUnits(caster, code, &target);
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster && unit->class_id == MAKEFOURCC('h','f','o','o')) {
        T_EQ(unit->summon_ability, code);
        T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','T','L','F')), 1);
        count++;
    }
    T_EQ(count, 2);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* OrcX01 issues the inherited "spiritwolf" order after removing the
 * preplaced Misha. Arsg must resolve to its authored Misha data, then retire
 * that Misha when the ability is cast again. */
TEST(wc3_spell, rexxar_spiritwolf_order_uses_learned_summon_misha_alias) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"UnitID1\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y2;X1;K\"Arsg\"\nC;Y2;X2;K\"AOsf\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K2\nC;Y2;X5;K\"ngzc\"\nC;Y2;X6;K1\n"
        "C;Y3;X1;K\"AOsf\"\nC;Y3;X2;K\"AOsf\"\nC;Y3;X3;K1\n"
        "C;Y3;X4;K2\nC;Y3;X5;K\"nwlg\"\nC;Y3;X6;K1\nE\n";
    UnitAbilities_t abilities = { .abilList = "", .heroAbilList = "Arsg" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *rexxar;
    uint32_t misha_count = 0, wolf_count = 0;
    edict_t *first_misha = NULL;

    reset_entities();
    setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    rexxar = make_hero(MAKEFOURCC('O','r','e','x'), 500.0f, 200.0f, 0.0f, 0.0f);
    rexxar->data.UnitAbilities = &abilities;
    rexxar->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','r','s','g'), .level = 1);
    rexxar->s.player = 0;

    T_EQ(G_AbilityCode(MAKEFOURCC('A','r','s','g')), MAKEFOURCC('A','O','s','f'));
    T_EQ(G_UnitAbilityLevel(rexxar, MAKEFOURCC('A','O','s','f')), 1);
    T_ASSERT(S_SpellAbilityForCode(MAKEFOURCC('A','r','s','g')) != NULL);
    T_EQ(S_SpellUnitId(MAKEFOURCC('A','r','s','g'), 1), MAKEFOURCC('n','g','z','c'));
    T_EQ(S_SpellData(MAKEFOURCC('A','r','s','g'), 1, 1), 1.0f);
    T_ASSERT(G_UnitAbilityResearchAvailable(rexxar, MAKEFOURCC('A','r','s','g')));
    T_ASSERT(G_IsUnitAbilityAvailable(rexxar, MAKEFOURCC('A','r','s','g')));
    T_ASSERT(unit_issueimmediateorder(rexxar, "spiritwolf"));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == rexxar) {
        if (unit->class_id == MAKEFOURCC('n','g','z','c')) {
            T_EQ(unit->summon_ability, MAKEFOURCC('A','r','s','g'));
            first_misha = unit;
            misha_count++;
        }
        if (unit->class_id == MAKEFOURCC('n','w','l','g')) wolf_count++;
    }
    T_EQ(misha_count, 1);
    T_EQ(wolf_count, 0);

    S_SpellResetCooldowns(rexxar);
    T_NOT_NULL(first_misha);
    AbilitySummonMisha_Execute(rexxar, (spellTarget_t){ .type = SPELL_TARGET_NONE },
        &(abilityitem_t){ .code = MAKEFOURCC('A','r','s','g'),
                          .ability = S_SpellAbilityForCode(MAKEFOURCC('A','r','s','g')) });
    T_ASSERT(first_misha && M_IsDead(first_misha));
    T_ASSERT(first_misha && first_misha->inuse);
    T_EQ(first_misha ? first_misha->class_id : 0, MAKEFOURCC('n','g','z','c'));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == rexxar && !M_IsDead(unit))
        T_ASSERT(unit->class_id != MAKEFOURCC('n','w','l','g'));

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
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
	T_ASSERT(!S_SpellIsChanneling(caster));
	G_AddChannel(caster);
	G_GetChannel(caster)->code = MAKEFOURCC('A','H','b','z');
	T_ASSERT(S_SpellIsChanneling(caster));
	G_GetChannel(caster)->code = 0;
	T_ASSERT(!S_SpellIsChanneling(caster));
}

TEST(wc3_spell, identity_thinker_captures_owner_and_target_incarnations) {
    edict_t *owner = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    edict_t *thinker = S_SpellIdentityThinker(owner, MAKEFOURCC('A','l','s','h'), target);
    uint32_t owner_spawn = owner->spawn_time, target_spawn = target->spawn_time;

    T_NOT_NULL(thinker);
    T_ASSERT(S_SpellChannelOwner(thinker) == owner);
    T_ASSERT(S_SpellChannelTarget(thinker) == target);
    owner->spawn_time++;
    T_NULL(S_SpellChannelOwner(thinker));
    owner->spawn_time = owner_spawn;
    target->spawn_time++;
    T_NULL(S_SpellChannelTarget(thinker));
    target->spawn_time = target_spawn;
    G_FreeEdict(thinker);
}

TEST(wc3_spell, channel_owner_resolves_only_captured_incarnation) {
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    edict_t *thinker;
    uint32_t spawn_time;

    G_AddChannel(caster);
    G_GetChannel(caster)->serial = 6;
    thinker = S_SpellChannelThinker(caster, MAKEFOURCC('A','H','d','r'));
    T_ASSERT(S_SpellChannelOwner(thinker) == caster);
    spawn_time = caster->spawn_time;
    caster->spawn_time++;
    T_NULL(S_SpellChannelOwner(thinker));
    caster->spawn_time = spawn_time;
    G_FreeEdict(thinker);
}

TEST(wc3_spell, channel_target_resolves_only_captured_incarnation) {
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    edict_t *thinker;
    uint32_t spawn_time;

    G_AddChannel(caster);
    G_GetChannel(caster)->serial = 7;
    thinker = S_SpellChannelTargetThinker(caster, MAKEFOURCC('A','H','d','r'), target);
    T_ASSERT(S_SpellChannelTarget(thinker) == target);
    spawn_time = target->spawn_time;
    target->spawn_time++;
    T_NULL(S_SpellChannelTarget(thinker));
    target->spawn_time = spawn_time;
    G_FreeEdict(thinker);
}

TEST(wc3_spell, projectile_owner_resolves_only_captured_incarnation) {
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    edict_t *missile = G_Spawn();
    uint32_t spawn_time = caster->spawn_time;

    missile->owner = caster;
    G_AddChannel(missile);
    G_GetChannel(missile)->owner_spawn_time = caster->spawn_time;
    T_ASSERT(S_SpellProjectileOwner(missile) == caster);
    caster->spawn_time++;
    T_NULL(S_SpellProjectileOwner(missile));
    caster->spawn_time = spawn_time;
    G_FreeEdict(missile);
}

TEST(wc3_spell, status_source_resolves_only_captured_incarnation) {
    edict_t *source = make_hero(MAKEFOURCC('h','p','e','a'), 250, 0, 0, 0);
    heroabilitystatus_t slot = { 0 };
    uint32_t spawn_time = source->spawn_time;

    slot.source = source;
    slot.source_spawn_time = source->spawn_time;
    T_ASSERT(S_SpellStatusSource(&slot) == source);
    source->spawn_time++;
    T_NULL(S_SpellStatusSource(&slot));
    source->spawn_time = spawn_time;
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
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	edict_t *image;
	uint32_t before;

	caster->unit_state->ranks[0].code = MAKEFOURCC('A','O','m','i');
	caster->unit_state->ranks[0].level = 1;
	caster->unit_state->hero.level = 3;
	memset(&level.events, 0, sizeof(level.events));
	before = globals.num_edicts;

	T_ASSERT(unit_issueimmediateorder(caster, "mirrorimage"));
	T_EQ((int)globals.num_edicts, (int)before + 1);
	image = &globals.edicts[before];
	T_ASSERT(image->inuse);
	T_ASSERT(image != caster);
	T_EQ((int)image->class_id, (int)caster->class_id);
	T_ASSERT(image->aiflags & AI_ILLUSION);
	T_EQ((int)image->unit_state->hero.level, (int)caster->unit_state->hero.level);
	T_ASSERT(image->unit_state->hero.suspend_xp);
	T_EQ((int)level.events.queue[2].type, (int)EVENT_PLAYER_UNIT_SUMMON);
	T_ASSERT(level.events.queue[2].edict == caster);
	T_ASSERT(level.events.queue[2].source == image);
	T_EQ((int)level.events.queue[3].type, (int)EVENT_UNIT_SUMMON);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_replenishes_life_then_mana_using_authored_ratios) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"2.0\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('h','b','a','r'), 100, 50, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    well->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
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
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"2.0\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('h','b','a','r'), 100, 10, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    well->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
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


TEST(wc3_spell, moon_well_autocast_uses_threshold_and_nearest_valid_target) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"Rng1\"\nC;Y1;X9;K\"DataE1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"ground,friend\"\n"
        "C;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"2.0\"\nC;Y2;X6;K\"25\"\n"
        "C;Y2;X7;K\"100\"\nC;Y2;X8;K\"99999\"\nC;Y2;X9;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t * well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 80, 0, 0);
    edict_t * near = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 80, 0);
    edict_t * far = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    well->data.UnitAbilities = &abilities;
    well->s.player = near->s.player = far->s.player = 0;
    near->svflags |= SVF_MONSTER;
    far->svflags |= SVF_MONSTER;
    near->targtype = far->targtype = TARG_GROUND;
    near->health.max_value = far->health.max_value = 100.0f;
    near->health.value = far->health.value = 50.0f;
    near->mana.max_value = far->mana.max_value = 0.0f;

    T_ASSERT(item.ability && (item.ability->flags & AB_AUTOCAST));
    T_ASSERT(item.ability && (item.ability->flags & AB_UPDATE));
    T_ASSERT(S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(near->health.value, 90.0f, 0.001f); /* 80 well mana / DataB 2 = 40 life */
    T_FEQ(far->health.value, 50.0f, 0.001f);
    T_FEQ(well->mana.value, 0.0f, 0.001f);

    well->mana.value = 25.0f; /* DataC is a strict minimum threshold. */
    near->health.value = 50.0f;
    T_ASSERT(!S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(near->health.value, 50.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_autocast_does_not_substitute_cast_range_for_area) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"Area1\"\nC;Y1;X7;K\"Rng1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\n"
        "C;Y2;X3;K2\nC;Y2;X4;K0.5\nC;Y2;X5;K10\n"
        "C;Y2;X6;K0\nC;Y2;X7;K99999\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 80, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    well->data.UnitAbilities = &abilities;
    well->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.max_value = 100.0f;
    target->health.value = 50.0f;

    T_ASSERT(!S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(target->health.value, 50.0f, 0.001f);
    T_FEQ(well->mana.value, 80.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Stock ROC and TFT Ambt values from ability_audit -raw Ambt. Area=400 is the
 * autocast acquisition radius; Rng=99999 is the manual cast range. */
TEST(wc3_spell, moon_well_stock_data_uses_area_for_autocast) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"Rng1\"\nC;Y1;X9;K\"DataE1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"air,ground,invu,vuln,friend,organic\"\n"
        "C;Y2;X4;K2\nC;Y2;X5;K0.5\nC;Y2;X6;K10\n"
        "C;Y2;X7;K400\nC;Y2;X8;K99999\nC;Y2;X9;K1\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 80, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    well->data.UnitAbilities = &abilities;
    well->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.max_value = 100.0f;
    target->health.value = 50.0f;
    T_EQ((int)G_AbilityLevel(item.code, 1)->area, 400);
    T_EQ((int)G_AbilityLevel(item.code, 1)->range, 99999);
    T_ASSERT(!S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(target->health.value, 50.0f, 0.001f);

    target->s.origin2.x = 300.0f;
    target->s.origin.x = 300.0f;
    T_ASSERT(S_AbilityMessage(well, A_AUTOCAST_ACQUIRE, &call));
    T_FEQ(target->health.value, 100.0f, 0.001f);
    T_FEQ(well->mana.value, 55.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_datae_gates_only_natural_mana_regen_to_night) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataE1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t * well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);
    UnitBalance_t balance = *well->data.UnitBalance;
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    well->data.UnitAbilities = &abilities;
    balance.manaRegen = 2.0f;
    well->data.UnitBalance = &balance;
    well->mana.max_value = 100.0f;
    well->mana.value = 50.0f;
    well->unit_state->mana_regen_bonus = 1.0f;

    T_ASSERT(G_ActorHasSkill(well, "Ambt"));
    T_ASSERT(item.ability && item.ability->proc == CAbilityManaBattery);
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    T_EQ(G_AbilityData(item.code)->id, item.code);
    T_EQ(G_AbilityCode(item.code), MAKEFOURCC('A','m','b','t'));
    T_FEQ(G_AbilityData(item.code)->level[0].data[4].number, 1.0f, 0.001f);
    T_ASSERT(!G_IsNight());
    T_ASSERT(S_AbilityMessage(well, A_NATURAL_MANA_REGEN_BLOCKED, &call));
    T_ASSERT(S_UnitAbilityEvent(well, A_NATURAL_MANA_REGEN_BLOCKED));
    G_RunEntity(well);
    T_FEQ(well->mana.value, 50.1f, 0.001f); /* external +1/sec remains active */

    well->mana.value = 50.0f;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(!S_UnitAbilityEvent(well, A_NATURAL_MANA_REGEN_BLOCKED));
    G_RunEntity(well);
    T_FEQ(well->mana.value, 50.3f, 0.001f); /* natural 2/sec + bonus 1/sec */

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_natural_mana_regen_starts_after_construction) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataE1\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 0, 0, 0);
    UnitBalance_t balance = *well->data.UnitBalance;

    well->data.UnitAbilities = &abilities;
    balance.manaRegen = 2.0f;
    balance.buildTime = 1;
    well->data.UnitBalance = &balance;
    well->mana.max_value = 100.0f;
    well->mana.value = 50.0f;
    G_AddConstruction(well);
    G_GetConstruction(well)->type = CONSTRUCTION_NIGHTELF;

    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(G_IsNight());

    FOR_LOOP(tick, 9) G_RunEntity(well);
    T_ASSERT(G_GetConstruction(well));
    T_FEQ(well->mana.value, 50.0f, 0.001f);
    G_RunEntity(well);
    T_ASSERT(!G_GetConstruction(well));
    T_FEQ(well->mana.value, 50.2f, 0.001f);

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Registration-order updates must not invoke the expensive Moon Well
 * ownership resolver on units with unrelated native abilities. Dynamic unit
 * data must be reflected on the very next tick, without a saveable cache. */
TEST(wc3_spell, moon_well_dispatch_skips_unrelated_native_abilities) {
    UnitAbilities_t plain = { .abilList = "Aatk,Afoo" };
    UnitAbilities_t well_abilities = { .abilList = "Aatk,Ambt" };
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);

    unit->data.UnitAbilities = &plain;
    unit->health.value = 0.0f; /* no presentation effect spawned by the test */
    S_TestResetMoonWellUpdateCalls();
    T_ASSERT(!S_MoonWellMayUpdate(unit));
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 0);

    unit->data.UnitAbilities = &well_abilities;
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellAlias(unit), MAKEFOURCC('A','m','b','t'));
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);

    unit->data.UnitAbilities = &plain;
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);
}

/* A custom Moon Well inherits from Ambt even when neither its alias nor its
 * parent is a native ability on other units. Runtime adds/removes and native
 * removals must not return a stale cached ownership decision. */
TEST(wc3_spell, moon_well_dispatch_preserves_custom_alias_and_runtime_changes) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Ambt\"\n"
        "C;Y3;X1;K\"Afoo\"\nC;Y3;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t native = { .abilList = "Afoo,AqW1" };
    UnitAbilities_t unrelated = { .abilList = "Afoo" };
    uint32_t const custom = MAKEFOURCC('A','q','W','1');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);
    edict_t *runtime;

    unit->health.value = 0.0f;
    unit->data.UnitAbilities = &native;
    T_EQ(G_AbilityCode(custom), MAKEFOURCC('A','m','b','t'));
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellAlias(unit), custom);
    S_TestResetMoonWellUpdateCalls();
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);

    T_ASSERT(G_ActorRemoveSkill(unit, custom));
    T_ASSERT(S_MoonWellMayUpdate(unit)); /* harmless conservative positive */
    T_EQ(S_TestMoonWellAlias(unit), 0);
    unit->data.UnitAbilities = &unrelated;
    T_ASSERT(!S_MoonWellMayUpdate(unit));
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);

    runtime = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);
    runtime->health.value = 0.0f;
    runtime->data.UnitAbilities = &unrelated;
    T_ASSERT(G_ActorAddSkill(runtime, custom));
    T_ASSERT(S_MoonWellMayUpdate(runtime));
    T_EQ(S_TestMoonWellAlias(runtime), custom);
    S_RunAbilityUpdates(runtime);
    T_EQ(S_TestMoonWellUpdateCalls(), 2);

    T_ASSERT(G_ActorRemoveSkill(runtime, custom));
    T_ASSERT(!S_MoonWellMayUpdate(runtime));
    T_EQ(S_TestMoonWellAlias(runtime), 0);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* The expensive G_AbilityCode lookup is shared between different units
 * with the same custom or unrelated ability. The unit-list scan remains live. */
TEST(wc3_spell, moon_well_class_cache_reuses_positive_and_negative_rawcodes) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Ambt\"\n"
        "C;Y3;X1;K\"Afoo\"\nC;Y3;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t native = { .abilList = "Afoo,AqW1" };
    UnitAbilities_t unrelated = { .abilList = "Afoo" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *first, *second;

    reset_entities(); setup_test_world();
    first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);

    first->data.UnitAbilities = &native;
    second->data.UnitAbilities = &unrelated;
    S_TestResetMoonWellClassResolves();
    T_ASSERT(S_MoonWellMayUpdate(first));
    T_EQ(S_TestMoonWellClassResolves(), 2); /* Afoo and AqW1 */
    T_ASSERT(!S_MoonWellMayUpdate(second));
    T_ASSERT(S_MoonWellMayUpdate(first));
    T_EQ(S_TestMoonWellAlias(first), MAKEFOURCC('A','q','W','1'));
    T_EQ(S_TestMoonWellClassResolves(), 2); /* no repeat metadata lookups */
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Changing a custom ability's parent requires a fresh classification even
 * for rawcodes cached earlier; the generation also invalidates negatives. */
TEST(wc3_spell, moon_well_class_cache_invalidates_on_ability_data_replacement) {
    const char moon_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Ambt\"\nE\n";
    const char other_slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Afoo\"\nE\n";
    uint32_t const custom = MAKEFOURCC('A','q','W','1');
    UnitAbilities_t abilities = { .abilList = "AqW1" };
    slkTestData_t *moon = parse_slk_string(moon_slk);
    slkTestData_t *other = parse_slk_string(other_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", moon);
    slkTestData_t *replaced, *discarded_other, *discarded_moon;
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);

    unit->data.UnitAbilities = &abilities;
    S_TestResetMoonWellClassResolves();
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellClassResolves(), 1);
    replaced = G_SetSLKRows("AbilityData", other);
    T_ASSERT(!S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellClassResolves(), 2);
    T_EQ(G_AbilityCode(custom), MAKEFOURCC('A','f','o','o'));
    discarded_other = G_SetSLKRows("AbilityData", replaced);
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellClassResolves(), 3);
    discarded_moon = G_SetSLKRows("AbilityData", old);
    free_slk_rows(discarded_other);
    free_slk_rows(discarded_moon);
    free_slk_rows(replaced);
    free_slk_rows(old);
    free_slk_rows(moon);
    free_slk_rows(other);
}

/* These distinct rawcodes collide in the direct-mapped hash. The key check
 * must prevent a cached non-Moon-Well alias from hiding the Moon Well. */
TEST(wc3_spell, moon_well_class_cache_handles_hash_collisions) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"AqW1\"\nC;Y2;X2;K\"Ambt\"\n"
        "C;Y3;X1;K\"AqWa\"\nC;Y3;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t moon_list = { .abilList = "AqW1" };
    UnitAbilities_t other_list = { .abilList = "AqWa" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);

    S_TestResetMoonWellClassResolves();
    unit->data.UnitAbilities = &moon_list;
    T_ASSERT(S_MoonWellMayUpdate(unit));
    unit->data.UnitAbilities = &other_list;
    T_ASSERT(!S_MoonWellMayUpdate(unit));
    unit->data.UnitAbilities = &moon_list;
    T_ASSERT(S_MoonWellMayUpdate(unit));
    T_EQ(S_TestMoonWellClassResolves(), 3); /* same hash, different keys */
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_dispatch_keeps_construction_effect_cleanup) {
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);
    edict_t *effect = G_Spawn();

    well->data.UnitAbilities = &abilities;
    G_AddConstruction(well);
    T_NOT_NULL(G_GetConstruction(well));
    G_GetConstruction(well)->paused = true;
    G_GetConstruction(well)->type = CONSTRUCTION_NIGHTELF;
    T_NOT_NULL(effect);
    effect->owner = well;
    effect->summon_ability = MAKEFOURCC('A','m','b','t');
    effect->s.flags |= EF_NOT_SELECTABLE;
    S_TestResetMoonWellUpdateCalls();
    S_RunAbilityUpdates(well);
    T_EQ(S_TestMoonWellUpdateCalls(), 1);
    T_ASSERT(!effect->inuse);
}

TEST(wc3_spell, non_moon_well_updates_do_not_scan_for_effect_cleanup) {
    edict_t *unit = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);

    S_TestResetMoonWellEffectReleaseCalls();
    S_RunAbilityUpdates(unit);
    T_EQ(S_TestMoonWellEffectReleaseCalls(), 0);
}

TEST(wc3_spell, moon_well_ability_disable_still_releases_its_effect) {
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);
    edict_t *effect = G_Spawn();
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Ambt"));
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);

    T_NOT_NULL(effect);
    T_EQ(item.ability->proc, CAbilityManaBattery);
    well->data.UnitAbilities = &abilities;
    effect->owner = well;
    effect->summon_ability = item.code;
    effect->s.flags |= EF_NOT_SELECTABLE;
    S_TestResetMoonWellEffectReleaseCalls();

    T_ASSERT(S_AbilityMessage(well, A_DISABLE, &call));
    T_EQ(S_TestMoonWellEffectReleaseCalls(), 1);
    T_ASSERT(!effect->inuse);
}

TEST(wc3_spell, moon_well_missing_ability_data_does_not_restore_natural_regen) {
    const char slk[] =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"Afoo\"\nC;Y2;X2;K\"Afoo\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);
    UnitBalance_t balance = *well->data.UnitBalance;

    well->data.UnitAbilities = &abilities;
    balance.manaRegen = 2.0f;
    well->data.UnitBalance = &balance;
    well->mana.max_value = 100.0f;
    well->mana.value = 50.0f;
    G_AddConstruction(well);
    G_GetConstruction(well)->paused = true;
    G_GetConstruction(well)->type = CONSTRUCTION_NIGHTELF;
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    S_TestResetMoonWellMissingDataWarningCalls();
    T_ASSERT(S_UnitAbilityEvent(well, A_NATURAL_MANA_REGEN_BLOCKED));
    T_EQ(S_TestMoonWellMissingDataWarningCalls(), 1);
    G_RunEntity(well);
    T_FEQ(well->mana.value, 50.0f, 0.001f);

    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, moon_well_roc_data11_does_not_alias_datae) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Data11\"\n"
        "C;Y2;X1;K\"Ambt\"\nC;Y2;X2;K\"Ambt\"\nC;Y2;X3;K1\nE\n";
    UnitAbilities_t abilities = { .abilList = "Ambt" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *well = make_hero(MAKEFOURCC('e','m','o','w'), 100, 50, 0, 0);

    well->data.UnitAbilities = &abilities;
    T_FEQ(G_AbilityLevel(MAKEFOURCC('A','m','b','t'), 1)->data[0].number, 1.0f, 0.001f);
    T_FEQ(G_AbilityLevel(MAKEFOURCC('A','m','b','t'), 1)->data[4].number, 0.0f, 0.001f);
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
    T_ASSERT(!S_UnitAbilityEvent(well, A_NATURAL_MANA_REGEN_BLOCKED));
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();

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

TEST(wc3_spell, forked_lightning_hits_three_units_in_aimed_cone_within_range) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X15\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataA2\"\nC;Y1;X5;K\"DataA3\"\nC;Y1;X6;K\"DataB1\"\n"
		"C;Y1;X7;K\"DataB2\"\nC;Y1;X8;K\"DataB3\"\n"
		"C;Y1;X9;K\"Rng1\"\nC;Y1;X10;K\"Rng2\"\nC;Y1;X11;K\"Rng3\"\n"
		"C;Y1;X12;K\"Area1\"\nC;Y1;X13;K\"Cool1\"\nC;Y1;X14;K\"Cost1\"\nC;Y1;X15;K\"Dur1\"\n"
		"C;Y2;X1;K\"ANfl\"\nC;Y2;X2;K\"ANfl\"\nC;Y2;X3;K85\nC;Y2;X4;K160\nC;Y2;X5;K250\n"
		"C;Y2;X6;K3\nC;Y2;X7;K3\nC;Y2;X8;K3\n"
		"C;Y2;X9;K600\nC;Y2;X10;K600\nC;Y2;X11;K600\nC;Y2;X12;K125\nC;Y2;X13;K11\nC;Y2;X14;K110\nC;Y2;X15;K0\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster, *first, *selected, *second, *third, *flank, *out_of_range;
	abilityitem_t spell_item = S_AbilityItem(FS_SLKKey("ANfl"));
	static int const damage[] = { 85, 160, 250 };

	reset_entities(); setup_test_world();
	caster = make_hero(MAKEFOURCC('n','n','s','s'), 250, 500, 0, 0);
	first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	selected = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
	second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 250, 0);
	third = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 400, -20);
	flank = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 300, 180);
	out_of_range = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 700, 0);

	caster->s.player = 0;
	first->s.player = selected->s.player = second->s.player = third->s.player = flank->s.player = out_of_range->s.player = 1;
	first->svflags |= SVF_MONSTER; selected->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER;
	third->svflags |= SVF_MONSTER; flank->svflags |= SVF_MONSTER; out_of_range->svflags |= SVF_MONSTER;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	T_FEQ(S_SpellNumber(spell_item.code, ABILITY_NUMBER_COOLDOWN, 1), 11.0f, 0.001f);
	T_FEQ(S_SpellNumber(spell_item.code, ABILITY_NUMBER_COST, 1), 110.0f, 0.001f);
	T_FEQ(S_SpellRange(spell_item.code, 1), 600.0f, 0.001f);
	FOR_LOOP(i, 6) {
		edict_t *unit[] = { first, selected, second, third, flank, out_of_range };
		unit[i]->health.value = unit[i]->health.max_value = 1000;
	}
	FOR_LOOP(i, 3) {
		edict_t *unit[] = { first, selected, second, third, flank, out_of_range };
		caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','f','l'), .level = i + 1);
		FOR_LOOP(j, 6) unit[j]->health.value = 1000;
		test_ability_message(caster, A_EXECUTE, &spell_item,
			&(spellTarget_t){ .type = SPELL_TARGET_UNIT, .entity = selected });
		T_FEQ(first->health.value, 1000 - damage[i], 0.01f);
		T_FEQ(selected->health.value, 1000 - damage[i], 0.01f);
		T_FEQ(second->health.value, 1000 - damage[i], 0.01f);
		T_FEQ(third->health.value, 1000.0f, 0.01f); /* fourth eligible cone unit exceeds the hit cap */
		T_FEQ(flank->health.value, 1000.0f, 0.01f);
		T_FEQ(out_of_range->health.value, 1000.0f, 0.01f);
	}

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, frost_arrows_apply_rank_damage_slow_duration_and_mana_cost) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X22\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"Cost2\"\nC;Y1;X5;K\"Cost3\"\n"
		"C;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataA2\"\nC;Y1;X8;K\"DataA3\"\n"
		"C;Y1;X9;K\"DataB1\"\nC;Y1;X10;K\"DataB2\"\nC;Y1;X11;K\"DataB3\"\n"
		"C;Y1;X12;K\"DataC1\"\nC;Y1;X13;K\"DataC2\"\nC;Y1;X14;K\"DataC3\"\n"
		"C;Y1;X15;K\"Dur1\"\nC;Y1;X16;K\"Dur2\"\nC;Y1;X17;K\"Dur3\"\n"
		"C;Y1;X18;K\"HeroDur1\"\nC;Y1;X19;K\"Rng1\"\nC;Y1;X20;K\"BuffID1\"\n"
		"C;Y1;X21;K\"BuffID2\"\nC;Y1;X22;K\"BuffID3\"\n"
		"C;Y2;X1;K\"ANfa\"\nC;Y2;X2;K\"AHca\"\n"
		"C;Y2;X3;K10\nC;Y2;X4;K10\nC;Y2;X5;K10\n"
		"C;Y2;X6;K5\nC;Y2;X7;K10\nC;Y2;X8;K15\n"
		"C;Y2;X9;K0.3\nC;Y2;X10;K0.5\nC;Y2;X11;K0.7\n"
		"C;Y2;X12;K0.3\nC;Y2;X13;K0.5\nC;Y2;X14;K0.7\n"
		"C;Y2;X15;K5\nC;Y2;X16;K5\nC;Y2;X17;K5\nC;Y2;X18;K1.5\nC;Y2;X19;K70\n"
		"C;Y2;X20;K\"Bcsd\"\nC;Y2;X21;K\"Bcsd\"\nC;Y2;X22;K\"Bcsd\"\nE\n";
	slkTestData_t *rows, *old;
	edict_t *attacker, *target;
	static float const expected_bonus[] = { 5, 10, 15 };
	static float const expected_slow[] = { -0.3f, -0.5f, -0.7f };

	reset_entities(); setup_test_world();
	rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
	attacker = make_hero(MAKEFOURCC('n','n','s','s'), 100, 100, 0, 0);
	target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	attacker->s.player = 0; target->s.player = 1;
	target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
	target->health.value = target->health.max_value = 1000;
	attacker->unit_state->attack1.weapon = WPN_MISSILE;
	attacker->unit_state->attack1.type = ATK_NORMAL;
	attacker->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
	attacker->mana.value = attacker->mana.max_value = 100;

	FOR_LOOP(rank, 3) {
		uint32_t const level = rank + 1;
		uint32_t const ability = MAKEFOURCC('A','N','f','a');
		heroabilitystatus_t *status;
		attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = ability, .level = level);
		target->health.value = 1000.0f;
		attacker->mana.value = 100.0f;
		T_ASSERT(S_ColdArrowsIssueOrder(attacker, target, ability));
		S_ResolveAttackHit(attacker, target, 100);
		T_FEQ(target->health.value, 1000 - (100 + expected_bonus[rank]), 0.01f);
		T_FEQ(attacker->mana.value, 90.0f, 0.001f);
		T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','c','s','d')));
		T_FEQ(unit_status_modifier_total(target, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), expected_slow[rank], 0.001f);
		T_FEQ(unit_status_modifier_total(target, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), expected_slow[rank], 0.001f);
		status = unit_findstatus(target, MAKEFOURCC('B','c','s','d'));
		T_NOT_NULL(status);
		T_EQ(status->timestamp, G_Time() + 5000);
		unit_removestatus(target, status, STATUS_REMOVE_SCRIPT);
	}
	T_FEQ(S_SpellNumber(MAKEFOURCC('A','N','f','a'), ABILITY_NUMBER_RANGE, 1), 70.0f, 0.001f);
	T_FEQ(S_SpellDuration(MAKEFOURCC('A','N','f','a'), 1, true), 1.5f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
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
	abilityitem_t devotion_aura_item = S_AbilityItem(FS_SLKKey("AHad"));
	ability_t const *devotion_aura = devotion_aura_item.ability;
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
	T_NOT_NULL(devotion_aura);
	T_ASSERT(devotion_aura->flags & AB_PASSIVE);
	T_ASSERT(!(devotion_aura->flags & AB_SPELL));
	T_EQ((int)devotion_aura_item.code, (int)MAKEFOURCC('A', 'H', 'a', 'd'));
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

TEST(wc3_spell, carrion_swarm_travels_authored_distance_and_hits_once_with_expanding_width) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"Cost1\"\n"
        "C;Y1;X7;K\"Cool1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
        "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"levels\"\n"
        "C;Y2;X1;K\"AUcs\"\nC;Y2;X2;K\"AUcs\"\nC;Y2;X3;K\"ground,air,structure,enemy\"\n"
        "C;Y2;X4;K10\nC;Y2;X5;K700\nC;Y2;X6;K0\nC;Y2;X7;K0\n"
        "C;Y2;X8;K50\nC;Y2;X9;K0\nC;Y2;X10;K800\nC;Y2;X11;K100\nC;Y2;X12;K1\nE\n";
    static cstring_t const ability_config =
        "[AUcs]\nMissileArt=TestUI\\Models\\anim_projectile.mdx\nMissilespeed=1000\n";
    static UnitBalance_t const no_regen = { .healthRegenType = "none" };
    slkTestData_t *rows, *old;
    edict_t *caster, *near, *middle, *far, *outside, *behind, *beyond, *wave = NULL;

    reset_entities(); setup_test_world();
    T_ASSERT(G_TestPushAbilityConfig(ability_config));
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('A','U','c','s'), WC3_EFFECT_MISSILE, 0),
            "TestUI\\Models\\anim_projectile.mdx");
    T_FEQ(S_SpellData(MAKEFOURCC('A','U','c','s'), 1, 3), 800.0f, 0.001f);
    caster = make_hero(MAKEFOURCC('U','d','r','e'), 500, 200, 0, 0);
    near = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    middle = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 400, 70);
    far = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 650, 0);
    middle->collision = 16.0f; /* Production units contribute their collision radius to the wave edge. */
    outside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 650, 180);
    behind = alloc_test_unit(MAKEFOURCC('h','f','o','o'), -80, 0);
    beyond = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 900, 0);
    caster->s.player = 0;
    behind->s.player = beyond->s.player = 1;
    behind->targtype = beyond->targtype = TARG_GROUND;
    behind->svflags |= SVF_MONSTER; beyond->svflags |= SVF_MONSTER;
    behind->health.value = behind->health.max_value = 1000;
    beyond->health.value = beyond->health.max_value = 1000;
    behind->data.UnitBalance = beyond->data.UnitBalance = (UnitBalance_t *)&no_regen;
    near->s.player = middle->s.player = far->s.player = outside->s.player = 1;
    near->targtype = middle->targtype = far->targtype = outside->targtype = TARG_GROUND;
    near->svflags |= SVF_MONSTER; middle->svflags |= SVF_MONSTER;
    far->svflags |= SVF_MONSTER; outside->svflags |= SVF_MONSTER;
    near->health.value = near->health.max_value = 1000;
    middle->health.value = middle->health.max_value = 1000;
    far->health.value = far->health.max_value = 1000;
    outside->health.value = outside->health.max_value = 1000;
    near->data.UnitBalance = middle->data.UnitBalance = far->data.UnitBalance =
        outside->data.UnitBalance = (UnitBalance_t *)&no_regen;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','U','c','s'), .level = 1);
    T_ASSERT(S_SpellAllowsAreaTarget(MAKEFOURCC('A','U','c','s'), caster, near));
    T_ASSERT(S_SpellIsAliveTarget(near));

    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','U','c','s'), &(vec2_t){200, 0}));
    T_FEQ(near->health.value, 1000, 0.001f); /* travel is asynchronous */
    FILTER_EDICTS(ent, ent->class_id == MAKEFOURCC('A','U','c','s') && G_GetCarrionWave(ent)) wave = ent;
    T_NOT_NULL(wave);
    if (wave) { T_NOT_NULL(wave->think); T_FEQ(G_GetCarrionWave(wave)->speed, 1000.0f, 0.001f); }
    T_EQ(wave->s.model, G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
    T_FEQ(G_GetCarrionWave(wave)->previous_position.x, 0.0f, 0.001f);
    T_FEQ(G_GetCarrionWave(wave)->direction.x, 1.0f, 0.001f);

    T_ASSERT(wave->inuse);
    level.time += FRAMETIME; G_RunEntities();
    T_FEQ(G_GetCarrionWave(wave)->travelled, 100.0f, 0.001f);
    /* Neither a future section nor the extension behind the caster is hit. */
    T_FEQ(far->health.value, 1000, 0.001f);
    T_FEQ(behind->health.value, 1000, 0.001f);
    T_FEQ(beyond->health.value, 1000, 0.001f);
    FOR_LOOP(i, 6) { level.time += FRAMETIME; G_RunEntities(); }
    T_FEQ(near->health.value, 950, 0.001f);
    T_FEQ(middle->health.value, 950, 0.001f);
    FOR_LOOP(i, 35) { level.time += FRAMETIME; G_RunEntities(); }
    T_FEQ(middle->health.value, 950, 0.001f);
    T_FEQ(far->health.value, 950, 0.001f);
    T_FEQ(outside->health.value, 1000, 0.001f);
    T_FEQ(behind->health.value, 1000, 0.001f);
    T_FEQ(beyond->health.value, 1000, 0.001f);
    /* The wave advances past the short clicked point and still reaches x=650. */
    FOR_LOOP(i, 10) { level.time += FRAMETIME; G_RunEntities(); }
    T_FEQ(near->health.value, 950, 0.001f);
    T_FEQ(middle->health.value, 950, 0.001f);
    T_FEQ(far->health.value, 950, 0.001f);

    G_SetSLKRows("AbilityData", old);
    G_TestPopAbilityConfig();
    free_slk_rows(rows);
}

TEST(wc3_spell, crushing_wave_without_missile_art_or_speed_still_damages) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"Cost1\"\n"
        "C;Y1;X7;K\"Cool1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
        "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"levels\"\n"
        "C;Y2;X1;K\"ACcv\"\nC;Y2;X2;K\"ACcv\"\nC;Y2;X3;K\"ground,enemy\"\n"
        "C;Y2;X4;K90\nC;Y2;X5;K700\nC;Y2;X6;K0\nC;Y2;X7;K0\n"
        "C;Y2;X8;K63\nC;Y2;X9;K0\nC;Y2;X10;K600\nC;Y2;X11;K90\nC;Y2;X12;K1\nE\n";
    static cstring_t const ability_config = "[ACcv]\nMissileArt=\nMissilespeed=0\n";
    static UnitBalance_t const no_regen = { .healthRegenType = "none" };
    slkTestData_t *rows, *old;
    edict_t *caster, *enemy, *wave = NULL;
    uint32_t const code = MAKEFOURCC('A','C','c','v');

    reset_entities(); setup_test_world();
    T_ASSERT(G_TestPushAbilityConfig(ability_config));
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = make_hero(MAKEFOURCC('U','d','r','e'), 500, 200, 0, 0);
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 300, 0);
    caster->s.player = 0; enemy->s.player = 1;
    enemy->targtype = TARG_GROUND;
    enemy->svflags |= SVF_MONSTER;
    enemy->health.value = enemy->health.max_value = 1000;
    enemy->data.UnitBalance = (UnitBalance_t *)&no_regen;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);

    T_ASSERT(S_CastPointTargetSpell(caster, code, &(vec2_t){200, 0}));
    FILTER_EDICTS(ent, ent->class_id == code && G_GetCarrionWave(ent)) wave = ent;
    T_NOT_NULL(wave);
    if (wave) {
        T_EQ(wave->s.model, 0);
        T_FEQ(G_GetCarrionWave(wave)->speed, 1100.0f, 0.001f);
        T_NOT_NULL(wave->think);
        T_EQ(wave->movetype, MOVETYPE_FLYMISSILE);
    }
    T_FEQ(enemy->health.value, 1000, 0.001f);
    FOR_LOOP(i, 8) { level.time += FRAMETIME; G_RunEntities(); }
    T_FEQ(enemy->health.value, 937, 0.001f);

    G_SetSLKRows("AbilityData", old);
    G_TestPopAbilityConfig();
    free_slk_rows(rows);
}

TEST(wc3_spell, carrion_swarm_spends_authored_maximum_damage_in_travel_order) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"Cost1\"\n"
        "C;Y1;X7;K\"Cool1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
        "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataD1\"\nC;Y1;X12;K\"levels\"\n"
        "C;Y2;X1;K\"AUcs\"\nC;Y2;X2;K\"AUcs\"\nC;Y2;X3;K\"ground,air,structure,enemy\"\n"
        "C;Y2;X4;K100\nC;Y2;X5;K700\nC;Y2;X6;K0\nC;Y2;X7;K0\n"
        "C;Y2;X8;K50\nC;Y2;X9;K120\nC;Y2;X10;K800\nC;Y2;X11;K100\nC;Y2;X12;K1\nE\n";
    static cstring_t const ability_config =
        "[AUcs]\nMissileArt=TestUI\\Models\\anim_projectile.mdx\nMissilespeed=1000\n";
    static UnitBalance_t const no_regen = { .healthRegenType = "none" };
    status_application_t const immunity = {
        .buff = "Biyy", .level = 1, .duration = 10.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    };
    slkTestData_t *rows, *old;
    edict_t *caster, *ally, *immune, *invulnerable, *first, *second, *third;
    uint32_t code = MAKEFOURCC('A','U','c','s');

    reset_entities(); setup_test_world();
    T_ASSERT(G_TestPushAbilityConfig(ability_config));
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = make_hero(MAKEFOURCC('U','d','r','e'), 500, 200, 0, 0);
    ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    immune = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 200, 0);
    invulnerable = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 250, 0);
    /* Allocate in the reverse of encounter order, all within one sweep. */
    third = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 280, 0);
    second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 250, 0);
    first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 220, 0);
    caster->s.player = 0;
    ally->s.player = 0;
    immune->s.player = invulnerable->s.player = 1;
    first->s.player = second->s.player = third->s.player = 1;
    ally->targtype = immune->targtype = invulnerable->targtype = TARG_GROUND;
    first->targtype = second->targtype = third->targtype = TARG_GROUND;
    ally->svflags |= SVF_MONSTER; immune->svflags |= SVF_MONSTER; invulnerable->svflags |= SVF_MONSTER;
    first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER; third->svflags |= SVF_MONSTER;
    ally->health.value = ally->health.max_value = 1000;
    immune->health.value = immune->health.max_value = 1000;
    invulnerable->health.value = invulnerable->health.max_value = 1000;
    first->health.value = first->health.max_value = 1000;
    second->health.value = second->health.max_value = 1000;
    third->health.value = third->health.max_value = 1000;
    ally->data.UnitBalance = immune->data.UnitBalance = invulnerable->data.UnitBalance = (UnitBalance_t *)&no_regen;
    first->data.UnitBalance = second->data.UnitBalance = third->data.UnitBalance = (UnitBalance_t *)&no_regen;
    G_SetUnitFlag(invulnerable, UNIT_FLAG_INVULNERABLE, true);
    T_NOT_NULL(unit_applystatus(immune, &immunity));
    T_ASSERT(S_UnitSpellImmune(immune));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);

    T_ASSERT(S_CastPointTargetSpell(caster, code, &(vec2_t){200, 0}));
    FOR_LOOP(i, 3) { level.time += FRAMETIME; G_RunEntities(); }
    /* All three targets are struck in the same tick. The 120 cap is spent
     * in nearest-first order even though their entity slots are reversed. */
    T_FEQ(ally->health.value, 1000, 0.001f);
    T_FEQ(immune->health.value, 1000, 0.001f);
    T_FEQ(invulnerable->health.value, 1000, 0.001f);
    T_FEQ(first->health.value, 950, 0.001f);
    T_FEQ(second->health.value, 950, 0.001f);
    T_FEQ(third->health.value, 980, 0.001f);

    G_SetSLKRows("AbilityData", old);
    G_TestPopAbilityConfig();
    free_slk_rows(rows);
}

TEST(wc3_spell, bladestorm_deals_110_damage_to_nearby_enemy_ground_units) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Area1\"\n"
		"C;Y2;X1;K\"AOww\"\nC;Y2;X2;K\"AOww\"\n"
		"C;Y2;X3;K\"110\"\nC;Y2;X4;K\"200\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('O','b','l','m'), 500, 200, 0, 0);
	edict_t *ground_enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *air_enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 80, 0);
	edict_t *distant_enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 250, 0);
	edict_t *thinker;
	uint32_t const code = MAKEFOURCC('A','O','w','w');

	caster->s.player = 0;
	caster->unit_state->ranks[0].code = code;
	caster->unit_state->ranks[0].level = 1;
	G_AddChannel(caster);
	G_GetChannel(caster)->code = code;
	G_GetChannel(caster)->serial = 1;

	ground_enemy->s.player = air_enemy->s.player = distant_enemy->s.player = 1;
	ally->s.player = 0;
	ground_enemy->targtype = TARG_GROUND;
	air_enemy->targtype = TARG_AIR;
	distant_enemy->targtype = TARG_GROUND;
	FOR_LOOP(i, 4) {
		edict_t *unit = i == 0 ? ground_enemy : i == 1 ? air_enemy : i == 2 ? ally : distant_enemy;
		unit->svflags |= SVF_MONSTER;
		unit->health.value = unit->health.max_value = 300;
	}
	memset(level.alliances, 0, sizeof(level.alliances));
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	thinker = S_SpellChannelThinker(caster, code);
	thinker->spawn_time = G_Time() + 5000;
	whirlwind_think(thinker);

	T_FEQ(ground_enemy->health.value, 190.0f, 0.001f);
	T_FEQ(air_enemy->health.value, 300.0f, 0.001f);
	T_FEQ(ally->health.value, 300.0f, 0.001f);
	T_FEQ(distant_enemy->health.value, 300.0f, 0.001f);

	G_FreeEdict(thinker);
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, shockwave_target_mask_filters_damage_and_cap) {
    static cstring_t const codes[] = { "AOsh", "ANsh", "AOs2" };
    FOR_LOOP(edition, 2) FOR_LOOP(variant, 3) {
        char slk[2048];
        snprintf(slk, sizeof(slk),
            "ID;PWXL;N;EBB;Y2;X7\n"
            "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"targs\"\n"
            "C;X4;K\"%s\"\nC;X5;K\"%s\"\nC;X6;K\"%s\"\nC;X7;K\"Area1\"\n"
            "C;Y2;X1;K\"%s\"\nC;X2;K\"%s\"\nC;X3;K\"ground,enemy\"\n"
            "C;X4;K75\nC;X5;K90\nC;X6;K600\nC;X7;K100\nE\n",
            edition ? "DataA1" : "Data11", edition ? "DataB1" : "Data12",
            edition ? "DataC1" : "Data13", codes[variant], codes[variant]);
        reset_entities(); setup_test_world();
        slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
        edict_t *caster = make_hero(MAKEFOURCC('O','t','c','h'), 500, 300, 0, 0);
        edict_t *targets[5];
        caster->s.player = 0;
        memset(level.alliances, 0, sizeof(level.alliances));
        ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
        ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
        FOR_LOOP(i, 5) {
            targets[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100 + i * 50, 0);
            targets[i]->s.player = i == 3 ? 0 : 1;
            targets[i]->targtype = i == 2 ? TARG_AIR : TARG_GROUND;
            targets[i]->svflags |= SVF_MONSTER;
            targets[i]->health.value = targets[i]->health.max_value = 300;
        }
        targets[4]->s.origin2.y = 200; /* outside the corridor */
        T_ASSERT(test_execute_code(caster, codes[variant],
            MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = { 600, 0 })));
        T_FEQ(targets[0]->health.value, 255.0f, 0.001f);
        T_FEQ(targets[1]->health.value, 255.0f, 0.001f);
        FOR_LOOP(i, 3) T_FEQ(targets[i + 2]->health.value, 300.0f, 0.001f);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

void S_TestTornadoBuildingPulse(edict_t *tornado);

TEST(wc3_spell, tornado_has_separate_summon_and_unit_passive_handlers) {
    abilityitem_t cast = S_AbilityItem(FS_SLKKey("ANto"));
    abilityitem_t damage = S_AbilityItem(FS_SLKKey("Atdg"));
    abilityitem_t spin = S_AbilityItem(FS_SLKKey("Atsp"));
    abilityitem_t wander = S_AbilityItem(FS_SLKKey("Atwa"));
    T_NOT_NULL(cast.ability);
    T_NOT_NULL(damage.ability);
    T_NOT_NULL(spin.ability);
    T_NOT_NULL(wander.ability);
    T_ASSERT(cast.ability->proc == CAbilityTornado);
    T_ASSERT(cast.ability->flags & AB_CHANNEL);
    T_ASSERT(cast.ability->flags & AB_SPELL);
    T_EQ(cast.ability->target_type, SPELL_TARGET_POINT);
    T_ASSERT(damage.ability->proc == CAbilityTornadoDamage);
    T_ASSERT(damage.ability->flags & AB_PASSIVE);
    T_ASSERT(spin.ability->proc == CAbilityTornadoSpin);
    T_ASSERT(spin.ability->flags & AB_PASSIVE);
    T_ASSERT(!(spin.ability->flags & AB_CHANNEL));
    T_ASSERT(wander.ability->proc == CAbilityWander);
    T_ASSERT(wander.ability->flags & AB_PASSIVE);
}

TEST(wc3_spell, tornado_summons_at_selected_point) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Cost1\"\n"
        "C;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\n"
        "C;Y1;X9;K\"UnitID1\"\nC;Y1;X10;K\"Area1\"\n"
        "C;Y2;X1;K\"ANto\"\nC;Y2;X2;K\"ANto\"\n"
        "C;Y2;X3;K\"ground\"\nC;Y2;X4;K\"0\"\n"
        "C;Y2;X5;K\"0\"\nC;Y2;X6;K\"700\"\n"
        "C;Y2;X7;K\"40\"\nC;Y2;X8;K\"40\"\n"
        "C;Y2;X9;K\"hfoo\"\nC;Y2;X10;K\"200\"\nE\n";
    static UnitAbilities_t const abilities = { .abilList = "ANto" };
    vec2_t const target = { 320.0f, 192.0f };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *summon = NULL;
    edict_t *clent;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    caster = make_hero(MAKEFOURCC('H','p','a','l'), 500.0f, 200.0f, 0.0f, 0.0f);
    caster->data.UnitAbilities = &abilities;
    clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->inuse = true;
    clent->client->connected = true;
    clent->client->ps.number = 0;
    clent->client->menu.ability_code = FS_SLKKey("ANto");
    caster->s.player = 0;
    G_SelectEntity(clent->client, caster);
    spell_cursor_capture_radius = 0.0f;
    spell_cursor_splat_seen = false;
    spell_cursor_image_clear = false;
    gi.Write = spell_cursor_capture_write;
    spell_cmd(clent);
    gi.Write = old_write;
    T_NOT_NULL(clent->client->menu.on_location_selected);
    T_ASSERT(spell_cursor_capture_radius > 0.0f);

    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("ANto"), &target));
    FILTER_EDICTS(unit, unit->owner == caster && unit->summon_ability == FS_SLKKey("ANto")) {
        summon = unit;
        break;
    }
    T_NOT_NULL(summon);
    if (summon) {
        T_FEQ(summon->s.origin2.x, target.x, 0.001f);
        T_FEQ(summon->s.origin2.y, target.y, 0.001f);
    }

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, tornado_spin_lift_lasts_ten_seconds) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Area1\"\n"
        "C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Atsp\"\nC;Y2;X2;K\"Atsp\"\n"
        "C;Y2;X3;K\"ground,enemy\"\nC;Y2;X4;K\"500\"\n"
        "C;Y2;X5;K\"20\"\nC;Y2;X6;K\"4\"\n"
        "C;Y2;X7;K\"1\"\nC;Y2;X8;K\"1\"\nC;Y2;X9;K\"BNto\"\n"
        "E\n";
    const char buff_slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"EffectArt\"\n"
        "C;Y2;X1;K\"Bcyc\"\nC;Y2;X2;K\"Bcyc\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
    static UnitAbilities_t const tornado_abilities = { .abilList = "Atsp" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buffs;
    edict_t *caster, *tornado, *target, *target2, *thinker, *first_target = NULL, *other_target;
    channel_t *cast;
    heroabilitystatus_t *lift;
    edict_t *lift_effect = NULL;
    edict_t *removed_effect = NULL;

    reset_entities(); setup_test_world();
    old = G_SetSLKRows("AbilityData", rows);
    old_buffs = G_SetSLKRows("AbilityBuffData", buff_rows);
    caster = make_hero(MAKEFOURCC('H','p','a','l'), 500.0f, 200.0f, 0.0f, 0.0f);
    tornado = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 0.0f);
    target2 = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 160.0f, 0.0f);
    tornado->data.UnitAbilities = &tornado_abilities;
    tornado->summon_ability = FS_SLKKey("ANto");
    tornado->owner = caster;
    tornado->s.player = caster->s.player = 0;
    tornado->svflags |= SVF_MONSTER;
    tornado->targtype = TARG_GROUND;
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target2->s.player = 1;
    target2->svflags |= SVF_MONSTER;
    target2->targtype = TARG_GROUND;
    target->unitinfo.FlyHeight = 25.0f;
    target2->unitinfo.FlyHeight = 25.0f;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

    level.time = 5000;
    cast = G_AddChannel(caster);
    cast->code = FS_SLKKey("ANto");
    cast->serial = 1;
    cast->origin = caster->s.origin2;
    thinker = S_SpellChannelThinker(caster, cast->code);
    T_NOT_NULL(thinker);
    if (!thinker) goto tornado_lift_cleanup;
    thinker->goalentity = tornado;
    G_GetChannel(thinker)->target_spawn_time = tornado->spawn_time;
    thinker->resources = G_Time() + 35000;
    thinker->freetime = thinker->resources;
    thinker->damage = G_Time();
    thinker->think = tornado_think;

    tornado_think(thinker);
    lift = unit_findstatus(target, MAKEFOURCC('B','c','y','c'));
    if (lift) first_target = target;
    else if (unit_findstatus(target2, MAKEFOURCC('B','c','y','c'))) first_target = target2;
    other_target = first_target == target ? target2 : target;
    T_NOT_NULL(first_target);
    if (!lift && first_target == target2) lift = unit_findstatus(target2, MAKEFOURCC('B','c','y','c'));
    T_NOT_NULL(lift);
    if (lift) {
        T_EQ(lift->duration_ms, 10000u);
        T_EQ(lift->timestamp, G_Time() + 10000u);
        T_ASSERT(lift->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
    }
    T_FEQ(first_target ? first_target->unitinfo.FlyHeight : 0.0f, 200.0f, 0.001f);
    FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','y','c') &&
                  effect->goalentity == first_target) { lift_effect = effect; break; }
    T_NOT_NULL(lift_effect);
    if (lift_effect) {
        T_ASSERT(!(lift_effect->s.flags & EF_NAMED_ATTACHMENT));
        T_FEQ(first_target->s.ground_offset, 200.0f, 0.001f);
        /* The target was lifted after its status effect was spawned. */
        G_RunEntity(lift_effect);
        T_FEQ(lift_effect->s.origin.z,
              first_target->s.origin.z - first_target->s.ground_offset, 0.001f);
        /* A linked status visual stays on the support plane across frames. */
        G_RunEntity(lift_effect);
        T_FEQ(lift_effect->s.origin.z,
              first_target->s.origin.z - first_target->s.ground_offset, 0.001f);
    }
    T_EQ(thinker->damage, 9000u); /* Non-stock DataB=4 seconds, not DataA=20 seconds. */
    {
        edict_t *target2_effect = NULL;
        level.time = 9000;
        tornado_think(thinker);
        T_NOT_NULL(unit_findstatus(other_target, MAKEFOURCC('B','c','y','c')));
        FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','y','c') &&
                      effect->goalentity == other_target) { target2_effect = effect; break; }
        T_NOT_NULL(target2_effect);
    }
    level.time = 13000;
    tornado_think(thinker);
    T_NOT_NULL(unit_findstatus(target, MAKEFOURCC('B','c','y','c')));
    T_NOT_NULL(unit_findstatus(target2, MAKEFOURCC('B','c','y','c')));
    T_EQ(thinker->damage, 17000u);
    level.time = 19000;
    unit_updatestatuses(target);
    unit_updatestatuses(target2);
    T_ASSERT(!unit_findstatus(target, MAKEFOURCC('B','c','y','c')));
    T_ASSERT(!unit_findstatus(target2, MAKEFOURCC('B','c','y','c')));
    tornado_think(thinker);
    T_ASSERT(!unit_findstatus(target, MAKEFOURCC('B','c','y','c')));
    T_ASSERT(!unit_findstatus(target2, MAKEFOURCC('B','c','y','c')));
    T_EQ(thinker->damage, 23000u);
    level.time = 25000;
    tornado_think(thinker);
    T_NOT_NULL(unit_findstatus(first_target, MAKEFOURCC('B','c','y','c'))); /* Non-stock DataA=20 seconds. */
    T_EQ(thinker->damage, 29000u);
    {
        heroabilitystatus_t *target_lift = unit_findstatus(target, MAKEFOURCC('B','c','y','c'));
        heroabilitystatus_t *target2_lift = unit_findstatus(target2, MAKEFOURCC('B','c','y','c'));
        if (target_lift) unit_removestatus(target, target_lift, STATUS_REMOVE_EXPIRE);
        if (target2_lift) unit_removestatus(target2, target2_lift, STATUS_REMOVE_EXPIRE);
    }
    T_FEQ(target->unitinfo.FlyHeight, 25.0f, 0.001f);
    T_FEQ(target2->unitinfo.FlyHeight, 25.0f, 0.001f);
    FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','y','c') &&
                  effect->goalentity == target) { removed_effect = effect; break; }
    T_NULL(removed_effect);

tornado_lift_cleanup:
    G_SetSLKRows("AbilityBuffData", old_buffs);
    free_slk_rows(buff_rows);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, tornado_damage_uses_three_authored_building_bands_only) {
    const char data[] =
        "ID;PWXL;N;EBB;Y2;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Area1\"\n"
        "C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"DataD1\"\n"
        "C;Y1;X9;K\"DataE1\"\nC;Y1;X10;K\"levels\"\n"
        "C;Y2;X1;K\"Atdg\"\nC;Y2;X2;K\"Atdg\"\n"
        "C;Y2;X3;K\"structure,enemy\"\nC;Y2;X4;K600\n"
        "C;Y2;X5;K7\nC;Y2;X6;K300\nC;Y2;X7;K13\n"
        "C;Y2;X8;K90\nC;Y2;X9;K50\nC;Y2;X10;K1\nE\n";
    UnitAbilities_t abilities = { .abilList = "Atdg" };
    slkTestData_t *rows, *old;
    edict_t *storm, *close, *medium, *outer, *friend, *infantry;
    reset_entities(); setup_test_world();
    rows = parse_slk_string(data); old = G_SetSLKRows("AbilityData", rows);
    storm = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    close = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 40, 0);
    medium = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 180, 0);
    outer = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 470, 0);
    friend = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 75, 0);
    infantry = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 30, 30);
    storm->data.UnitAbilities = &abilities;
    storm->summon_ability = MAKEFOURCC('A','N','t','o');
    storm->s.player = 0;
    close->s.player = medium->s.player = outer->s.player = infantry->s.player = 1;
    friend->s.player = 0;
    close->targtype = medium->targtype = outer->targtype = friend->targtype = TARG_STRUCTURE;
    close->s.flags |= EF_BUILDING; medium->s.flags |= EF_BUILDING;
    outer->s.flags |= EF_BUILDING; friend->s.flags |= EF_BUILDING;
    close->svflags |= SVF_MONSTER; medium->svflags |= SVF_MONSTER;
    outer->svflags |= SVF_MONSTER; friend->svflags |= SVF_MONSTER;
    infantry->svflags |= SVF_MONSTER; infantry->targtype = TARG_GROUND;
    edict_t *units[] = {close, medium, outer, friend, infantry};
    FOR_LOOP(i, 5) units[i]->health.value = units[i]->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    S_TestTornadoBuildingPulse(storm);
    T_FEQ(close->health.value, 450, 0.01f);
    T_FEQ(medium->health.value, 487, 0.01f);
    T_FEQ(outer->health.value, 493, 0.01f);
    T_FEQ(friend->health.value, 500, 0.01f);
    T_FEQ(infantry->health.value, 500, 0.01f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}


/* Doom's buff must outlive the caster and its explicit tick deadline must
 * survive a status-mask rebuild. The synthetic unit ID exercises the stock
 * summon path without requiring any installed MPQ. */
static char const pit_lord_doom_slk[] =
    "ID;PWXL;N;EBB;Y2;X14\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"BuffID1\"\nC;Y1;X6;K\"DataA1\"\n"
    "C;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"DataC1\"\nC;Y1;X9;K\"DataD1\"\n"
    "C;Y1;X10;K\"UnitID1\"\nC;Y1;X11;K\"Rng1\"\nC;Y1;X12;K\"Cost1\"\n"
    "C;Y1;X13;K\"Cool1\"\nC;Y1;X14;K\"Dur1\"\n"
    "C;Y2;X1;K\"ANdo\"\nC;Y2;X2;K\"ANdo\"\nC;Y2;X3;K1\n"
    "C;Y2;X4;K\"air,ground,enemy,nonhero,organic\"\n"
    "C;Y2;X5;K\"BNdo,BNdi\"\nC;Y2;X6;K40\nC;Y2;X7;K1\n"
    "C;Y2;X8;K120\nC;Y2;X9;K5\nC;Y2;X10;K\"hfoo\"\n"
    "C;Y2;X11;K650\nC;Y2;X12;K0\nC;Y2;X13;K0\nC;Y2;X14;K0\nE\n";

static void pit_lord_doom_setup(slkTestData_t **rows, slkTestData_t **old,
                                edict_t **caster, edict_t **target) {
    *rows = parse_slk_string(pit_lord_doom_slk);
    *old = G_SetSLKRows("AbilityData", *rows);
    *caster = make_hero(MAKEFOURCC('N','p','l','h'), 500, 400, 0, 0);
    *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    (*caster)->s.player = 0;
    (*target)->s.player = 1;
    (*target)->svflags |= SVF_MONSTER;
    (*target)->targtype = TARG_GROUND;
    (*target)->die = unit_die;
    (*target)->health.value = (*target)->health.max_value = 400.0f;
    (*caster)->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','N','d','o'), .level = 1);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.time = 1000;
}

TEST(wc3_spell, pit_lord_doom_persists_and_ticks_without_dispel_or_expiry) {
    slkTestData_t *rows, *old;
    edict_t *caster, *victim;
    heroabilitystatus_t *slot;
    pit_lord_doom_setup(&rows, &old, &caster, &victim);
    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','N','d','o'), victim));
    slot = unit_findstatus(victim, MAKEFOURCC('B','N','d','o'));
    T_NOT_NULL(slot);
    if (slot) {
        T_EQ(slot->timestamp, 0u);
        T_EQ(slot->next_tick, 2000u);
        T_ASSERT(!unit_status_can_magic_dispel(slot));
        T_ASSERT(slot->buff_flags & WC3_STATUS_BUFF_UNDISPELLABLE);
    }
    T_ASSERT(S_UnitIsSilenced(victim));
    unit_status_invalidate_update_mask(victim);
    level.time = 1999; unit_updatestatuses(victim);
    T_FEQ(victim->health.value, 400.0f, 0.001f);
    level.time = 2000; unit_updatestatuses(victim);
    T_FEQ(victim->health.value, 360.0f, 0.001f);
    level.time = 3000; unit_updatestatuses(victim);
    T_FEQ(victim->health.value, 320.0f, 0.001f);
    T_ASSERT(unit_findstatus(victim, MAKEFOURCC('B','N','d','o')) != NULL);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, pit_lord_doom_death_summons_once_with_caster_ownership) {
    slkTestData_t *rows, *old;
    edict_t *caster, *victim, *guard = NULL;
    pit_lord_doom_setup(&rows, &old, &caster, &victim);
    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','N','d','o'), victim));
    T_ASSERT(unit_findstatus(victim, MAKEFOURCC('B','N','d','o')) != NULL);
    /* Any cause of death triggers Doom; not only Doom damage. */
    unit_die(victim, caster);
    T_ASSERT(!unit_findstatus(victim, MAKEFOURCC('B','N','d','o')));
    FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == MAKEFOURCC('A','N','d','o') &&
                  unit != victim && unit->s.player == 0) {
        guard = unit;
        break;
    }
    T_NOT_NULL(guard);
    if (guard) {
        T_EQ(guard->class_id, MAKEFOURCC('h','f','o','o'));
        T_EQ(guard->owner, caster);
        T_NOT_NULL(unit_findstatus(guard, MAKEFOURCC('B','T','L','F')));
    }
    unit_statusdeath(victim);
    {
        uint32_t count = 0;
        FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == MAKEFOURCC('A','N','d','o')) count++;
        T_EQ(count, 1u);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, pit_lord_doom_rejects_resistant_heroes_mechanical_and_overlevel_creeps) {
    slkTestData_t *rows, *old;
    edict_t *caster, *victim;
    abilityitem_t item = S_AbilityItem(MAKEFOURCC('A','N','d','o'));
    spellTarget_t target;
    pit_lord_doom_setup(&rows, &old, &caster, &victim);
    target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = victim);
    T_ASSERT(test_ability_message(caster, A_VALIDATE, &item, &target));
    victim->targtype = TARG_MECHANICAL;
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &item, &target));
    victim->targtype = TARG_GROUND;
    victim->aiflags |= AI_ILLUSION;
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &item, &target));
    victim->aiflags &= ~AI_ILLUSION;
    { heroability_t *r = &victim->unit_state->ranks[0];
      *r = MAKE(heroability_t, .code = MAKEFOURCC('A','C','s','k'), .level = 1); }
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &item, &target));
    memset(victim->unit_state->ranks, 0, sizeof(victim->unit_state->ranks));
    {
        UnitBalance_t level_data = *victim->data.UnitBalance;
        victim->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
        level_data.level = 6;
        victim->data.UnitBalance = &level_data;
        T_ASSERT(!test_ability_message(caster, A_VALIDATE, &item, &target));
        level_data.level = 5;
        if (S_SpellIsEnemy(caster, victim))
            T_ASSERT(test_ability_message(caster, A_VALIDATE, &item, &target));
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, pit_lord_doom_status_capacity_is_preflighted) {
    slkTestData_t *rows, *old;
    edict_t *caster, *victim;
    char buff[5] = "Z000";
    pit_lord_doom_setup(&rows, &old, &caster, &victim);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        buff[1] = (char)('a' + i);
        T_ASSERT(unit_addstatus(victim, buff, 1));
    }
    T_ASSERT(!S_CastUnitTargetSpell(caster, MAKEFOURCC('A','N','d','o'), victim));
    T_ASSERT(!unit_findstatus(victim, MAKEFOURCC('B','N','d','o')));
    T_FEQ(caster->mana.value, 400.0f, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, pit_lord_doom_caster_disappearance_keeps_damage_and_summon_player) {
    slkTestData_t *rows, *old;
    edict_t *caster, *victim, *guard = NULL;
    pit_lord_doom_setup(&rows, &old, &caster, &victim);
    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','N','d','o'), victim));
    caster->inuse = false; /* caster's original incarnation no longer exists */
    victim->health.value = 20.0f;
    level.time = 2000;
    unit_updatestatuses(victim);
    T_ASSERT(M_IsDead(victim));
    FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == MAKEFOURCC('A','N','d','o') &&
                  unit->class_id == MAKEFOURCC('h','f','o','o') && unit != victim) {
        guard = unit;
        break;
    }
    T_NOT_NULL(guard);
    if (guard) {
        T_EQ(guard->s.player, 0u);
        T_NOT_NULL(unit_findstatus(guard, MAKEFOURCC('B','T','L','F')));
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, pit_lord_doom_save_load_preserves_permanent_tick) {
    cstring_t const path = Test_TempPath("pit-lord-doom-save.bin");
    slkTestData_t *rows, *old;
    edict_t *caster, *victim;
    heroabilitystatus_t *slot;
    pit_lord_doom_setup(&rows, &old, &caster, &victim);
    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','N','d','o'), victim));
    T_ASSERT(WriteGame(path));
    slot = unit_findstatus(victim, MAKEFOURCC('B','N','d','o'));
    T_NOT_NULL(slot);
    if (slot) { slot->next_tick = 0; slot->state_mask = 0; }
    T_ASSERT(ReadGame(path));
    slot = unit_findstatus(victim, MAKEFOURCC('B','N','d','o'));
    T_NOT_NULL(slot);
    if (slot) {
        T_EQ(slot->timestamp, 0u);
        T_EQ(slot->next_tick, 2000u);
        T_EQ(slot->source_ability, MAKEFOURCC('A','N','d','o'));
    }
    T_ASSERT(S_UnitIsSilenced(victim));
    level.time = 2000;
    unit_updatestatuses(victim);
    T_FEQ(victim->health.value, 360.0f, 0.001f);
    remove(path);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
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
	T_EQ(mana_shield->proc, CAbilityManaShield);
	T_ASSERT(mana_shield->flags & AB_SPELL);
	T_ASSERT(mana_shield->flags & AB_TOGGLE);
	T_EQ((int)mana_shield->target_type, (int)SPELL_TARGET_NONE);
	T_ASSERT(brawler->flags & AB_PASSIVE);
	T_ASSERT(cleave->flags & AB_PASSIVE);
	T_EQ((int)revive->target_type, (int)SPELL_TARGET_NONE);
	T_EQ((int)breath->target_type, (int)SPELL_TARGET_POINT);
	T_EQ((int)haze->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)doom->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ((int)howl->target_type, (int)SPELL_TARGET_NONE);
}

TEST(wc3_spell, howl_of_terror_reduces_enemy_attack_damage_by_authored_25_percent) {
	char const slk[] =
		"ID;PWXL;N;EBB;Y2;X15\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Area1\"\n"
		"C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"BuffID1\"\n"
		"C;Y1;X9;K\"DataA2\"\nC;Y1;X10;K\"DataA3\"\nC;Y1;X11;K\"DataA4\"\n"
		"C;Y1;X12;K\"BuffID2\"\nC;Y1;X13;K\"BuffID3\"\nC;Y1;X14;K\"BuffID4\"\n"
		"C;Y2;X1;K\"ANht\"\nC;Y2;X2;K\"ANht\"\nC;Y2;X3;K\"ground,enemy\"\n"
		"C;Y2;X4;K\"10\"\nC;Y2;X5;K\"10\"\nC;Y2;X6;K\"500\"\n"
		"C;Y2;X7;K\"0.25\"\nC;Y2;X8;K\"BNht\"\nE\n";
	static UnitAbilities_t const caster_abilities = { .abilList = "ANht" };
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *enemy, *victim;
	reset_entities(); setup_test_world();
	caster = G_Spawn(); caster->class_id = MAKEFOURCC('H','d','r','o'); G_InitUnitState(caster); G_InitUnitOrders(caster);
	caster->s.origin2 = (vec2_t){0, 0};
	old = G_SetSLKRows("AbilityData", rows);
	caster->health.value = caster->health.max_value = 300;
	caster->mana.value = caster->mana.max_value = 300;
	caster->data.UnitAbilities = &caster_abilities;
	enemy = G_Spawn(); enemy->class_id = MAKEFOURCC('H','d','r','o'); G_InitUnitState(enemy); G_InitUnitOrders(enemy);
	enemy->s.origin2 = (vec2_t){100, 0};
	victim = G_Spawn(); victim->class_id = MAKEFOURCC('h','f','o','o'); G_InitUnitState(victim); G_InitUnitOrders(victim);
	victim->s.origin2 = (vec2_t){200, 0};
	caster->s.player = 0; enemy->s.player = 1; victim->s.player = 1;
	memset(level.alliances, 0, sizeof(level.alliances));
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	enemy->svflags |= SVF_MONSTER; victim->svflags |= SVF_MONSTER;
	enemy->targtype = victim->targtype = TARG_GROUND;
	enemy->health.value = enemy->health.max_value = 500;
	victim->health.value = victim->health.max_value = 500;
	enemy->unit_state->attack1.type = ATK_NORMAL;
	{
		abilityitem_t const howl = S_AbilityItem(FS_SLKKey("ANht"));
		spellTarget_t const no_target = MAKE(spellTarget_t, .type = SPELL_TARGET_NONE, .point = { 0, 0 });
		AbilityHowlOfTerror_Execute(caster, no_target, &howl);
	}
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','N','h','t')));
	T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT), -0.25f, 0.001f);
	S_ResolveAttackHit(enemy, victim, 100);
	T_FEQ(victim->health.value, 425.0f, 0.001f);
	unit_removestatus(enemy, unit_findstatus(enemy, MAKEFOURCC('B','N','h','t')), STATUS_REMOVE_DISPEL);
	S_ResolveAttackHit(enemy, victim, 100);
	T_FEQ(victim->health.value, 325.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock BNht owns HowlTarget, with no explicit Targetattach: the status
 * attachment helper uses the origin default. Recasts must not accumulate
 * target models, and expiration/dispel must retire their identity markers. */
TEST(wc3_spell, pit_lord_howl_target_art_tracks_status_refresh_and_expiry) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"Area1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"levels\"\n"
        "C;Y2;X1;K\"ANht\"\nC;Y2;X2;K\"ANht\"\nC;Y2;X3;K\"ground,enemy\"\n"
        "C;Y2;X4;K\"7\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"250\"\n"
        "C;Y2;X7;K\"0.30\"\nC;Y2;X8;K\"BNht\"\nC;Y2;X9;K\"1\"\nE\n";
    static cstring_t const config =
        "[ANht]\nCasterArt=TestUI\\Models\\anim_pulse.mdx\n"
        "[BNht]\nTargetArt=TestUI\\Models\\anim_pulse.mdx\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *enemy, *ally, *air, *outside, *effect = NULL;
    abilityitem_t howl;
    spellTarget_t const st = MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    uint32_t const buff = MAKEFOURCC('B','N','h','t');
    uint32_t count;

    reset_entities(); setup_test_world(); level.time = 1000;
    T_ASSERT(G_TestPushAbilityConfig(config));
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = make_hero(MAKEFOURCC('N','p','l','h'), 100, 0, 0, 0);
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    air = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    outside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    caster->s.player = ally->s.player = 0;
    enemy->s.player = air->s.player = outside->s.player = 1;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    caster->svflags |= SVF_MONSTER;
    enemy->svflags |= SVF_MONSTER; ally->svflags |= SVF_MONSTER;
    air->svflags |= SVF_MONSTER; outside->svflags |= SVF_MONSTER;
    caster->targtype = enemy->targtype = ally->targtype = outside->targtype = TARG_GROUND;
    air->targtype = TARG_AIR;
    enemy->s.origin2 = (vec2_t){80, 0};
    ally->s.origin2 = (vec2_t){60, 0};
    air->s.origin2 = (vec2_t){70, 0};
    outside->s.origin2 = (vec2_t){600, 0};
    enemy->health.value = enemy->health.max_value = 500;
    ally->health.value = ally->health.max_value = 500;
    air->health.value = air->health.max_value = 500;
    outside->health.value = outside->health.max_value = 500;
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','h','t'), .level = 1);
    howl = S_AbilityItem(MAKEFOURCC('A','N','h','t'));
    T_ASSERT(G_RegisterModel("TestUI\\Models\\anim_pulse.mdx") > 0);
    AbilityHowlOfTerror_Execute(caster, st, &howl);
    T_ASSERT(S_UnitHasStatus(enemy, buff));
    T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT), -0.30f, 0.001f);
    T_ASSERT(!S_UnitHasStatus(ally, buff));
    T_ASSERT(!S_UnitHasStatus(air, buff));
    T_ASSERT(!S_UnitHasStatus(outside, buff));
    count = 0;
    FILTER_EDICTS(found, found->status_effect_code == buff && found->goalentity == enemy) {
        count++; effect = found;
    }
    T_EQ(count, 1u);
    T_NOT_NULL(effect);
    if (effect) T_ASSERT(!(effect->s.flags & EF_NAMED_ATTACHMENT));
    heroabilitystatus_t *status = unit_findstatus(enemy, buff);
    T_NOT_NULL(status);
    if (status) {
        T_ASSERT(status->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
        T_EQ(status->timestamp, 8000u);
    }
    level.time += 1000;
    AbilityHowlOfTerror_Execute(caster, st, &howl);
    count = 0;
    FILTER_EDICTS(found, found->status_effect_code == buff && found->goalentity == enemy) count++;
    T_EQ(count, 1u);
    status = unit_findstatus(enemy, buff);
    T_NOT_NULL(status);
    if (status) T_EQ(status->timestamp, 9000u);
    if (status) unit_removestatus(enemy, status, STATUS_REMOVE_DISPEL);
    count = 0;
    FILTER_EDICTS(found, found->status_effect_code == buff && found->goalentity == enemy) count++;
    T_EQ(count, 0u);
    T_ASSERT(!S_UnitHasStatus(enemy, buff));
    AbilityHowlOfTerror_Execute(caster, st, &howl);
    level.time += 7000; unit_updatestatuses(enemy);
    count = 0;
    FILTER_EDICTS(found, found->status_effect_code == buff && found->goalentity == enemy) count++;
    T_EQ(count, 0u);
    T_ASSERT(!S_UnitHasStatus(enemy, buff));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    G_TestPopAbilityConfig();
}

TEST(wc3_spell, pit_lord_cleave_hits_only_eligible_ground_secondaries_with_chest_art) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"targs\"\nC;Y1;X6;K\"levels\"\n"
        "C;Y1;X7;K\"DataA2\"\n"
        "C;Y2;X1;K\"ANca\"\nC;Y2;X2;K\"ANca\"\nC;Y2;X3;K\"0.3\"\n"
        "C;Y2;X4;K\"200\"\nC;Y2;X5;K\"ground,enemy,neutral\"\n"
        "C;Y2;X6;K\"1\"\nE\n";
    static cstring_t const config =
        "[ANca]\nSpecialArt=TestUI\\Models\\anim_pulse.mdx\nSpecialattach=chest\n";
    edict_t *attacker, *primary, *ground, *immune, *air, *structure, *ally, *far, *effect = NULL;
    slkTestData_t *rows, *old;
    uint32_t art_model;
    uint32_t art_count;
    reset_entities(); setup_test_world(); level.time = 1000;
    T_ASSERT(G_TestPushAbilityConfig(config));
    art_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    attacker = make_hero(MAKEFOURCC('N','p','l','h'), 200, 0, 0, 0);
    primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    ground = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    immune = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    air = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    structure = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    far = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    attacker->s.player = ally->s.player = 0;
    primary->s.player = ground->s.player = immune->s.player = air->s.player = structure->s.player = far->s.player = 1;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    edict_t *units[] = { primary, ground, immune, air, structure, ally, far };
    FOR_LOOP(i, 7) {
        units[i]->health.value = units[i]->health.max_value = 500;
        units[i]->svflags |= SVF_MONSTER;
        units[i]->targtype = TARG_GROUND;
        units[i]->s.origin2 = (vec2_t){50, 0};
    }
    air->targtype = TARG_AIR;
    structure->targtype = TARG_STRUCTURE; structure->s.flags |= EF_BUILDING;
    far->s.origin2 = (vec2_t){500, 0};
    T_NOT_NULL(unit_applystatus(immune, &(status_application_t){
        .buff = "BImm", .level = 1, .duration = 10.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    }));
    T_ASSERT(S_UnitSpellImmune(immune));
    attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','N','c','a'), .level = 1);
    attacker->unit_state->attack1.type = ATK_NORMAL;
    attacker->unit_state->attack1.weapon = WPN_NORMAL;
    attacker->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
    T_ASSERT(art_model > 0);
    S_ResolveAttackHit(attacker, primary, 100);
    T_FEQ(primary->health.value, 400, 0.001f);
    T_FEQ(ground->health.value, 470, 0.001f);
    T_FEQ(immune->health.value, 470, 0.001f); /* physical splash bypasses spell immunity */
    T_FEQ(air->health.value, 500, 0.001f);
    T_FEQ(structure->health.value, 500, 0.001f);
    T_FEQ(ally->health.value, 500, 0.001f);
    T_FEQ(far->health.value, 500, 0.001f);
    art_count = 0;
    FILTER_EDICTS(found, found->s.model == art_model && found->goalentity == ground) {
        effect = found; art_count++;
    }
    T_EQ(art_count, 1u);
    T_NOT_NULL(effect);
    if (effect) {
        T_ASSERT(effect->s.flags & EF_NAMED_ATTACHMENT);
        T_EQ(effect->s.hover_value, ground->s.number + 1);
    }
    art_count = 0;
    FILTER_EDICTS(found, found->s.model == art_model && found->goalentity == immune) art_count++;
    T_EQ(art_count, 1u);
    FILTER_EDICTS(found, found->s.model == art_model && found->goalentity != ground &&
                  found->goalentity != immune)
        T_ASSERT(false); /* no art on air, structure, primary, or ally */
    /* Cleave belongs to melee weapon hits; a ranged attack must not trigger it. */
    attacker->unit_state->attack1.weapon = WPN_MISSILE;
    S_ResolveAttackHit(attacker, primary, 100);
    T_FEQ(ground->health.value, 470, 0.001f);
    T_FEQ(immune->health.value, 470, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    G_TestPopAbilityConfig();
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
	static cstring_t const spells[] = {
		"Amls", "Acmg", "Amdf", "Asps", "Aclf", "Adef", "Afla", "Ainf", "Adis", "Ahea", "Aslo", "Aivs", "Aply", "AHav",
	};
	static cstring_t const passives[] = {
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
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->health.value = 60; ally->health.max_value = 100; ally->unit_state->armor_value = 2;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	test_execute_code(caster, "Ahea", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_FEQ(ally->health.value, 85.0f, 0.001f);
	test_execute_code(caster, "Ainf", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','i','n','f'))); T_FEQ(G_UnitArmorValue(ally), 7.0f, 0.001f);
	enemy->health.value = enemy->health.max_value = 500;
	S_ResolveAttackHit(ally, enemy, 100);
	T_FEQ(enemy->health.value, 390.0f, 0.001f);
	unit_removestatus(ally, unit_findstatus(ally, MAKEFOURCC('B','i','n','f')), STATUS_REMOVE_DISPEL);
	S_ResolveAttackHit(ally, enemy, 100);
	T_FEQ(enemy->health.value, 290.0f, 0.001f);
	enemy->unit_state->attack1.cooldown = 1.0f;
	enemy->unit_state->attack1.damagePoint = 0.3f;
	enemy->unitinfo.MoveSpeed = 270.0f;
	float const normal_move_distance = unit_movedistance(enemy);
	attack_melee(enemy);
	T_FEQ(enemy->wait, 0.3f, 0.001f);
	enemy->unit_state->ranks[0] = MAKE(heroability_t,
		.code = MAKEFOURCC('A','C','s','k'), .level = 1);
	T_ASSERT(S_UnitIsResistant(enemy));
	test_execute_code(caster, "Aslo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','s','l','o')));
	T_EQ(unit_findstatus(enemy, MAKEFOURCC('B','s','l','o'))->duration_ms, 10000u);
	T_FEQ(S_HumanMoveFactor(enemy), 0.4f, 0.001f);
	T_FEQ(unit_movedistance(enemy), normal_move_distance * 0.4f, 0.01f);
	T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), -0.25f, 0.001f);
	attack_melee(enemy);
	T_FEQ(enemy->wait, 0.4f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_healing_wave_bounces_to_four_distinct_units_with_falling_heal) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"targs1\"\nC;Y1;X6;K\"Area1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y1;X10;K\"Cost1\"\nC;Y1;X11;K\"Cool1\"\nC;Y1;X12;K\"Rng1\"\n"
        "C;Y2;X1;K\"AChv\"\nC;Y2;X2;K\"AOhw\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K\"air,ground,friend,self,vuln,invu,organic\"\n"
        "C;Y2;X5;K\"air,ground,friend,self,vuln,invu,organic\"\n"
        "C;Y2;X6;K100\nC;Y2;X7;K215\nC;Y2;X8;K4\nC;Y2;X9;K0.25\n"
        "C;Y2;X10;K0\nC;Y2;X11;K0\nC;Y2;X12;K700\nE\n";
    static UnitAbilities_t const abilities = { .abilList = "AChv" };
    static cstring_t const ability_config =
        "[AChV]\nTargetart=TestUI\\Models\\anim_pulse.mdx\n"
        "LightningEffect=HWPB,HWSB\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *targets[5];
    uint32_t target_art_model, primary_lightning = 0, secondary_lightning = 0;
    uint32_t target_art_count = 0;

    reset_entities();
    setup_test_world();
    T_ASSERT(G_TestPushAbilityConfig(ability_config));
    old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('n','d','r','s'), 0, 0);
    caster->data.UnitAbilities = (UnitAbilities_t *)&abilities;
    caster->s.player = 0;
    for (uint32_t i = 0; i < 5; i++) {
        targets[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1000.0f + 40.0f * i, 0);
        targets[i]->s.player = 0;
        targets[i]->health.max_value = 1000.0f;
        targets[i]->health.value = 500.0f;
        targets[i]->svflags |= SVF_MONSTER;
        targets[i]->targtype = TARG_GROUND;
    }

    T_ASSERT(S_SpellIsAliveTarget(targets[1]));
    T_ASSERT(S_SpellIsFriend(caster, targets[1]));
    T_ASSERT(S_SpellAllowsTarget(MAKEFOURCC('A','C','h','v'), caster, targets[1]));
    T_ASSERT(Vector2_distance(&targets[0]->s.origin2, &targets[1]->s.origin2) <= 100.0f);

    T_ASSERT(test_execute_code(caster, "AChv",
        MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = targets[0])));

    T_EQ(G_AbilityLightningId(MAKEFOURCC('A','C','h','v'), 0), MAKEFOURCC('H','W','P','B'));
    T_EQ(G_AbilityLightningId(MAKEFOURCC('A','C','h','v'), 1), MAKEFOURCC('H','W','S','B'));
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('A','C','h','v'), WC3_EFFECT_TARGET, 0),
            "TestUI\\Models\\anim_pulse.mdx");
    FOR_LOOP(i, MAX_LIGHTNING_EFFECTS) if (level.lightning_effects[i].inuse) {
        if (level.lightning_effects[i].state.effect_id == MAKEFOURCC('H','W','P','B'))
            primary_lightning++;
        else if (level.lightning_effects[i].state.effect_id == MAKEFOURCC('H','W','S','B'))
            secondary_lightning++;
    }
    T_EQ(primary_lightning, 1);
    T_EQ(secondary_lightning, 3);
    target_art_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
    FILTER_EDICTS(effect, effect->s.model == target_art_model && effect->goalentity) {
        target_art_count++;
    }
    T_EQ(target_art_count, 4);

    T_FEQ(targets[0]->health.value, 715.0f, 0.01f);
    T_FEQ(targets[1]->health.value, 661.25f, 0.01f);
    T_FEQ(targets[2]->health.value, 620.9375f, 0.01f);
    T_FEQ(targets[3]->health.value, 590.703125f, 0.01f);
    T_FEQ(targets[4]->health.value, 500.0f, 0.01f);

    G_SetSLKRows("AbilityData", old);
    G_TestPopAbilityConfig();
    free_slk_rows(rows);
}

/* Stock BuffIDs may be shared by custom abilities; both data schemas must
 * retain the casting alias through modifier installation and removal. */
TEST(wc3_spell, human_support_aliases_preserve_authored_modifiers) {
    FOR_LOOP(edition, 2) {
        char slk[4096];
        snprintf(slk, sizeof(slk),
            "ID;PWXL;N;EBB;Y5;X9\n"
            "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"targs\"\nC;X4;K\"Rng1\"\n"
            "C;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\nC;X8;K\"%s\"\nC;X9;K\"BuffID1\"\n"
            "C;Y2;X1;K\"Ainf\"\nC;X2;K\"Ainf\"\nC;X7;K0.1\nC;X8;K2\n"
            "C;Y3;X1;K\"Aslo\"\nC;X2;K\"Aslo\"\nC;X7;K0.2\nC;X8;K0.25\n"
            "C;Y4;X1;K\"C001\"\nC;X2;K\"Ainf\"\nC;X3;K\"ground,friend\"\n"
            "C;X4;K700\nC;X5;K30\nC;X6;K30\nC;X7;K0.35\nC;X8;K9\nC;X9;K\"Binf\"\n"
            "C;Y5;X1;K\"C002\"\nC;X2;K\"Aslo\"\nC;X3;K\"ground,enemy\"\n"
            "C;X4;K700\nC;X5;K30\nC;X6;K30\nC;X7;K0.6\nC;X8;K0.45\nC;X9;K\"Bslo\"\nE\n",
            edition ? "DataA1" : "Data11", edition ? "DataB1" : "Data12");
        reset_entities(); setup_test_world();
        slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
        edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
        edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
        edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
        caster->s.player = ally->s.player = 0; enemy->s.player = 1;
        ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
        ally->unit_state->armor_value = 2;
        enemy->health.value = enemy->health.max_value = 500;
        T_ASSERT(test_execute_code(caster, "C001", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally)));
        T_FEQ(G_UnitArmorValue(ally), 11.0f, 0.001f);
        T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT), 0.35f, 0.001f);
        S_ResolveAttackHit(ally, enemy, 100);
        T_FEQ(enemy->health.value, 365.0f, 0.001f);
        unit_removestatus(ally, unit_findstatus(ally, MAKEFOURCC('B','i','n','f')), STATUS_REMOVE_DISPEL);
        T_FEQ(G_UnitArmorValue(ally), 2.0f, 0.001f);
        S_ResolveAttackHit(ally, enemy, 100);
        T_FEQ(enemy->health.value, 265.0f, 0.001f);
        enemy->unit_state->attack1.cooldown = 1.0f;
        enemy->unit_state->attack1.damagePoint = 0.3f;
        enemy->unitinfo.MoveSpeed = 270.0f;
        float const normal_move_distance = unit_movedistance(enemy);
        T_ASSERT(test_execute_code(caster, "C002", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy)));
        T_FEQ(S_HumanMoveFactor(enemy), 0.4f, 0.001f);
        T_FEQ(unit_movedistance(enemy), normal_move_distance * 0.4f, 0.01f);
        attack_melee(enemy);
        T_FEQ(enemy->wait, 0.3f / 0.55f, 0.001f);
        unit_removestatus(enemy, unit_findstatus(enemy, MAKEFOURCC('B','s','l','o')), STATUS_REMOVE_DISPEL);
        attack_melee(enemy);
        T_FEQ(enemy->wait, 0.3f, 0.001f);
        T_FEQ(unit_movedistance(enemy), normal_move_distance, 0.01f);
        G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
    }
}

TEST(wc3_spell, human_attack_passives_and_defend_change_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\nC;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Afbk\"\nC;Y2;X2;K\"Afbk\"\nC;Y2;X3;K\"20\"\nC;Y2;X4;K\"1\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"1\"\n"
		"C;Y3;X1;K\"Adef\"\nC;Y3;X2;K\"Adef\"\nC;Y3;X3;K\"0.5\"\nC;Y3;X4;K\"1\"\nC;Y3;X5;K\"0.3\"\nC;Y3;X8;K\"0\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 100, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','f','b','k'), .level = 1);
	target->mana.value = target->mana.max_value = 50; attacker->unit_state->attack1.type = ATK_NORMAL;
	T_EQ(S_HumanAttackDamage(attacker, target, 10), 30); T_FEQ(target->mana.value, 30.0f, 0.001f);
	unit_addstatus(target, "Adef", 1); attacker->unit_state->attack1.type = ATK_PIERCE;
	T_FEQ(S_HumanMoveFactor(target), 0.7f, 0.001f);
	T_EQ(S_HumanAttackDamage(attacker, target, 100), 60);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, feedback_hit_uses_hero_drain_and_skips_spell_immune_targets) {
	static UnitAbilities_t const feedback = { .abilList = "Afbk" };
	static UnitAbilities_t const spell_immunity = { .abilList = "Amim" };
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\n"
		"C;Y2;X1;K\"Afbk\"\nC;Y2;X2;K\"Afbk\"\nC;Y2;X3;K\"20\"\n"
		"C;Y2;X4;K\"1\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker, *regular, *hero, *immune;
	UnitBalance_t hero_balance = MAKE(UnitBalance_t, .strength = 20, .maxHealth = 500);

	reset_entities(); setup_test_world();
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	attacker = alloc_test_unit(MAKEFOURCC('h','b','r','e'), 0, 0);
	regular = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
	hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 32, 32);
	immune = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, -32);
	attacker->data.UnitAbilities = &feedback;
	attacker->s.player = 0;
	attacker->svflags |= SVF_MONSTER;
	attacker->unit_state->attack1.type = ATK_NORMAL;
	regular->svflags |= SVF_MONSTER; hero->svflags |= SVF_MONSTER; immune->svflags |= SVF_MONSTER;
	regular->s.player = hero->s.player = immune->s.player = 1;
	regular->health.value = hero->health.value = immune->health.value = 500.0f;
	regular->health.max_value = hero->health.max_value = immune->health.max_value = 500.0f;
	regular->mana.value = regular->mana.max_value = 50.0f;
	hero->mana.value = hero->mana.max_value = 50.0f;
	immune->mana.value = immune->mana.max_value = 50.0f;
	hero->data.UnitBalance = &hero_balance;
	immune->data.UnitAbilities = &spell_immunity;

	T_ASSERT(G_UnitIsHero(hero));
	T_ASSERT(S_UnitSpellImmune(immune));
	S_ResolveAttackHit(attacker, regular, 10);
	T_FEQ(regular->mana.value, 30.0f, 0.001f);
	T_FEQ(regular->health.value, 470.0f, 0.001f);
	S_ResolveAttackHit(attacker, hero, 10);
	T_FEQ(hero->mana.value, 46.0f, 0.001f);
	T_FEQ(hero->health.value, 486.0f, 0.001f);
	S_ResolveAttackHit(attacker, immune, 10);
	T_FEQ(immune->mana.value, 50.0f, 0.001f);
	T_FEQ(immune->health.value, 490.0f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_feedback_alias_reads_its_own_mana_burn_data) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\n"
        "C;Y2;X1;K\"Afbt\"\nC;Y2;X2;K\"Afbt\"\nC;Y2;X3;K\"9\"\n"
        "C;Y2;X4;K\"2\"\nC;Y2;X5;K\"3\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    UnitAbilities_t abilities = { .abilList = "Afbt" };
    attacker->data.UnitAbilities = &abilities; target->mana.value = target->mana.max_value = 20;
    T_EQ(S_FeedbackDamage(attacker, target, 10), 28);
    T_FEQ(target->mana.value, 11, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_hardened_skin_uses_chance_floor_and_reduction) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"DataC1\"\n"
        "C;Y2;X1;K\"Ansk\"\nC;Y2;X2;K\"Ansk\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"12\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *target = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    UnitAbilities_t abilities = { .abilList = "Ansk" };
    target->data.UnitAbilities = &abilities;
    T_EQ(S_HardenedSkinDamage(target, 40), 28);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_disease_cloud_alias_ticks_authored_area_damage) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"targs\"\n"
        "C;Y1;X6;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aap1\"\nC;Y2;X2;K\"Aapl\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"ground,enemy,organic\"\nC;Y2;X6;K\"13\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    UnitAbilities_t abilities = { .abilList = "Aap1" };
    source->data.UnitAbilities = &abilities; source->s.player = 0;
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','p','c'), 1);
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE; target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND; target->health.value = target->health.max_value = 100;
    S_RunAbilityUpdates(source);
    {
        heroabilitystatus_t *slot = unit_findstatus(target, MAKEFOURCC('B','a','p','l'));
        T_NOT_NULL(slot);
        T_ASSERT(slot->buff_flags & WC3_STATUS_BUFF_PHYSICAL);
        T_ASSERT(!(slot->buff_flags & WC3_STATUS_BUFF_MAGICAL));
        T_ASSERT(!unit_status_can_magic_dispel(slot));
    }
    level.time += 1000; unit_updatestatuses(target);
    T_FEQ(target->health.value, 87, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, abomination_disease_cloud_waits_for_research_and_shows_emitter) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"targs\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aap1\"\nC;Y2;X2;K\"Aapl\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"ground,enemy,organic\"\nC;Y2;X6;K\"13\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    UnitAbilities_t abilities = { .abilList = "Aap1" };
    edict_t *abomination = alloc_test_unit(MAKEFOURCC('u','a','b','o'), 0, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    uint32_t emitter_count = 0;

    abomination->data.UnitAbilities = &abilities;
    abomination->s.player = 0;
    enemy->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    enemy->svflags |= SVF_MONSTER; enemy->targtype = TARG_GROUND;
    enemy->health.value = enemy->health.max_value = 100;
    S_RunAbilityUpdates(abomination);
    T_NULL(unit_findstatus(enemy, MAKEFOURCC('B','a','p','l')));
    FILTER_EDICTS(effect, effect->owner == abomination && effect->summon_ability == MAKEFOURCC('A','a','p','1'))
        emitter_count++;
    T_EQ(emitter_count, 0);

    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','p','c'), 1);
    S_RunAbilityUpdates(abomination);
    T_NOT_NULL(unit_findstatus(enemy, MAKEFOURCC('B','a','p','l')));
    emitter_count = 0;
    FILTER_EDICTS(effect, effect->owner == abomination &&
                         effect->summon_ability == MAKEFOURCC('A','a','p','1') &&
                         effect->goalentity == abomination && effect->s.model)
        emitter_count++;
    T_EQ(emitter_count, 1);
    S_RunAbilityUpdates(abomination);
    emitter_count = 0;
    FILTER_EDICTS(effect, effect->owner == abomination && effect->summon_ability == MAKEFOURCC('A','a','p','1'))
        emitter_count++;
    T_EQ(emitter_count, 1);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A distant unit must never pay for a full targeting/immunity check. Check
 * both edges of the square prefilter and the exact circular distance, while
 * retaining authored enemy targeting and an explicitly magic-immune unit. */
TEST(wc3_spell, disease_cloud_rejects_distant_edicts_before_spell_eligibility) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"targs\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aap1\"\nC;Y2;X2;K\"Aapl\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"ground,enemy,organic\"\nC;Y2;X6;K\"13\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *source = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 4096, 4096);
    edict_t *edge = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4196, 4096);
    edict_t *diagonal_inside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4156, 4176);
    edict_t *diagonal_outside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4167, 4167);
    edict_t *axis_outside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4197, 4096);
    edict_t *far = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 9096, 9096);
    edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4116, 4106);
    edict_t *immune = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 4136, 4146);
    edict_t *targets[] = { edge, diagonal_inside, diagonal_outside, axis_outside, far, ally, immune };
    UnitAbilities_t abilities = { .abilList = "Aap1" };
    status_application_t immunity = {
        .buff = "Biyy", .level = 1, .duration = 10.0f,
        .state_mask = WC3_STATUS_STATE_MAGIC_IMMUNE
    };
    source->data.UnitAbilities = &abilities;
    source->s.player = 0;
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','p','c'), 1);
    FOR_LOOP(i, sizeof(targets) / sizeof(targets[0])) {
        targets[i]->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
        targets[i]->svflags |= SVF_MONSTER;
        targets[i]->targtype = TARG_GROUND;
        targets[i]->health.value = targets[i]->health.max_value = 100;
    }
    ally->s.player = 0;
    T_NOT_NULL(unit_applystatus(immune, &immunity));
    T_ASSERT(S_UnitSpellImmune(immune));
    S_TestResetDiseaseCloudTargetValidations();
    S_RunAbilityUpdates(source);
    /* Only two valid enemies, an ally and an immune unit enter the costly
     * eligibility routine. The other three lie outside the exact circle. */
    T_EQ(S_TestDiseaseCloudTargetValidations(), 4);
    T_NOT_NULL(unit_findstatus(edge, MAKEFOURCC('B','a','p','l')));
    T_NOT_NULL(unit_findstatus(diagonal_inside, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(diagonal_outside, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(axis_outside, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(far, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(ally, MAKEFOURCC('B','a','p','l')));
    T_NULL(unit_findstatus(immune, MAKEFOURCC('B','a','p','l')));
    /* Target movement must take effect on the next update; there is no
     * persistent range membership to invalidate. */
    diagonal_outside->s.origin2.x = 4146;
    diagonal_outside->s.origin2.y = 4146;
    S_TestResetDiseaseCloudTargetValidations();
    S_RunAbilityUpdates(source);
    T_EQ(S_TestDiseaseCloudTargetValidations(), 5);
    T_NOT_NULL(unit_findstatus(diagonal_outside, MAKEFOURCC('B','a','p','l')));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Two Disease Cloud emitters must own different Bapl slots and retain their
 * own tick phases; leaving the aura must not remove an existing infection. */
TEST(wc3_spell, disease_cloud_independent_sources_and_refresh) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Area1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"targs\"\n"
        "C;Y1;X6;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aap1\"\nC;Y2;X2;K\"Aapl\"\nC;Y2;X3;K\"100\"\n"
        "C;Y2;X4;K\"5\"\nC;Y2;X5;K\"ground,enemy,organic\"\nC;Y2;X6;K\"13\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *first = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 40, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
    UnitAbilities_t abilities = { .abilList = "Aap1" };
    heroabilitystatus_t *a, *b;
    uint32_t next_a;
    second->health.value = second->health.max_value = 100;
    second->svflags |= SVF_MONSTER;
    second->stand = unit_stand;
    second->movetype = MOVETYPE_NONE;
    unit_stand(second);
    first->data.UnitAbilities = second->data.UnitAbilities = &abilities;
    first->s.player = second->s.player = 0;
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','p','c'), 1);
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100;
    S_RunAbilityUpdates(first);
    a = unit_findstatussource(target, MAKEFOURCC('B','a','p','l'), first);
    T_NOT_NULL(a);
    T_EQ(a->stack_policy, WC3_STATUS_STACK_INDEPENDENT);
    next_a = a->next_tick;
    S_RunAbilityUpdates(second);
    b = unit_findstatussource(target, MAKEFOURCC('B','a','p','l'), second);
    T_NOT_NULL(b);
    T_NE(a, b);
    T_NE(a->instance_id, b->instance_id);
    T_EQ(a->next_tick, next_a);
    S_RunAbilityUpdates(first);
    T_EQ(unit_findstatussource(target, a->code, first), a);
    T_EQ(a->next_tick, next_a); /* Same-source exposure does not reset DPS. */
    unit_updatestatuses(target); /* Build the derived index before direct refresh. */
    T_ASSERT(target->unit_state->timed_status_update_mask & (1u << (a - target->unit_state->statuses)));
    /* Model a previously valid index that omitted this still-live status. */
    target->unit_state->timed_status_update_mask = (1u << MAX_UNIT_STATUSES) | (1u << (b - target->unit_state->statuses));
    a->timestamp = G_Time() + 1500;
    CAbilityDiseaseCloud(first, A_UPDATE, NULL); /* Refresh must invalidate the timed index. */
    T_EQ(a->timestamp, G_Time() + 5000);
    T_EQ(target->unit_state->timed_status_update_mask, 0u); /* Invalidated for a rebuild. */
    unit_updatestatuses(target);
    T_ASSERT(target->unit_state->timed_status_update_mask & (1u << (a - target->unit_state->statuses)));
    level.time += 1000;
    unit_updatestatuses(target);
    T_FEQ(target->health.value, 74.0f, 0.001f);
    unit_removestatus(target, a, STATUS_REMOVE_DISPEL);
    T_NULL(unit_findstatussource(target, MAKEFOURCC('B','a','p','l'), first));
    T_EQ(unit_findstatussource(target, MAKEFOURCC('B','a','p','l'), second), b);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, creep_orb_of_annihilation_adds_authored_attack_damage) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"ANak\"\nC;Y2;X2;K\"ANak\"\nC;Y2;X3;K\"19\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    UnitAbilities_t abilities = { .abilList = "ANak" };
    attacker->data.UnitAbilities = &abilities;
    T_EQ(S_OrbAnnihilationDamage(attacker, 23), 42);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, destroyer_afak_does_not_inherit_creep_anak_passive_bonus) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y2;X1;K\"ANak\"\nC;Y2;X2;K\"ANak\"\nC;Y2;X3;K\"19\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = make_hero(MAKEFOURCC('o','g','r','u'), 100, 0, 0, 0);
    UnitAbilities_t abilities = { .abilList = "Afak" };
    attacker->data.UnitAbilities = &abilities;
    T_EQ(S_OrbAnnihilationDamage(attacker, 23), 23);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_data_b_and_e_scale_outgoing_and_magic_attack_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"0.75\"\n"
		"C;Y2;X5;K\"0.3\"\nC;Y2;X7;K\"0.6\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

	attacker->unit_state->attack1.type = ATK_NORMAL; unit_addstatus(attacker, "Adef", 1);
	T_EQ(S_HumanAttackDamage(attacker, target, 100), 75);
	memset(attacker->unit_state->statuses, 0, sizeof(attacker->unit_state->statuses));
	attacker->unit_state->attack1.type = ATK_MAGIC; unit_addstatus(target, "Adef", 1);
	T_EQ(S_HumanAttackDamage(attacker, target, 100), 60);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_toggle_reports_state_and_updates_animation_properties) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataC1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0\"\nC;Y2;X4;K\"0.5\"\nC;Y2;X5;K\"0.3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *footman = make_hero(MAKEFOURCC('h','f','o','o'), 100, 0, 0, 0);
	abilityitem_t item = S_AbilityItem(FS_SLKKey("Adef"));
	spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);

	T_ASSERT(!test_ability_message(footman, A_TOGGLE_ON, &item, &target));
	T_ASSERT(test_execute_code(footman, "Adef", target));
	T_ASSERT(test_ability_message(footman, A_TOGGLE_ON, &item, &target));
	T_ASSERT(S_UnitHasStatus(footman, MAKEFOURCC('A','d','e','f')));
	T_STREQ(footman->animation_props, "defend");
	T_FEQ(S_HumanMoveFactor(footman), 0.7f, 0.001f);

	T_ASSERT(test_execute_code(footman, "Adef", target));
	T_ASSERT(!test_ability_message(footman, A_TOGGLE_ON, &item, &target));
	T_ASSERT(!S_UnitHasStatus(footman, MAKEFOURCC('A','d','e','f')));
	T_STREQ(footman->animation_props, "");
	T_FEQ(S_HumanMoveFactor(footman), 1.0f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_projectile_retargets_to_unit_source_and_cannot_reflect_twice) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\n"
		"C;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"1\"\n"
		"C;Y2;X5;K\"0.3\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"100\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	bool const old_loaded = game.constants.combatConstantsLoaded, old_deflect = game.constants.defendDeflection;
	edict_t *attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *missile = G_Spawn();
	float const target_hp = target->health.value, attacker_hp = attacker->health.value;

	game.constants.combatConstantsLoaded = true; game.constants.defendDeflection = true;
	attacker->unit_state->attack1.type = ATK_PIERCE; unit_addstatus(target, "Adef", 1);
	missile->owner = attacker; missile->goalentity = target; missile->movetype = MOVETYPE_FLYMISSILE;
	G_InitMissile(missile); missile->missile->attack_type = ATK_PIERCE;
	missile->velocity = 100000.0f; missile->damage = 100; missile->s.origin = target->s.origin;
	SV_Physics_Toss(missile);
	T_ASSERT(missile->inuse); T_ASSERT(missile->missile->reflected); T_ASSERT(missile->goalentity == attacker);
	T_FEQ(target->health.value, target_hp, 0.001f);

	missile->s.origin = attacker->s.origin;
	SV_Physics_Toss(missile);
	T_ASSERT(!missile->inuse); T_ASSERT(attacker->health.value < attacker_hp);

	game.constants.combatConstantsLoaded = old_loaded; game.constants.defendDeflection = old_deflect;
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_rejects_recycled_projectile_owner_before_reaction) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"DataA1\"\nC;X4;K\"DataF1\"\n"
        "C;X5;K\"DataG1\"\nC;X6;K\"DataH1\"\n"
        "C;Y2;X1;K\"Adef\"\nC;X2;K\"Adef\"\nC;X3;K0.5\nC;X4;K100\nC;X5;K0.25\nC;X6;K1\nE\n";
    reset_entities(); setup_test_world();
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    bool const old_loaded = game.constants.combatConstantsLoaded, old_deflect = game.constants.defendDeflection;
    edict_t *attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 0, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *missile = G_Spawn();
    float const hp = target->health.value;
    game.constants.combatConstantsLoaded = true; game.constants.defendDeflection = true;
    attacker->spawn_time = 200;
    attacker->unit_state->attack1.type = ATK_PIERCE;
    unit_addstatus(target, "Adef", 1);
    missile->owner = attacker; missile->goalentity = target; missile->movetype = MOVETYPE_FLYMISSILE;
    G_InitMissile(missile);
    missile->missile->attack_type = ATK_PIERCE;
    missile->missile->owner_spawn_time = 100;
    missile->velocity = 100000.0f; missile->damage = 100; missile->s.origin = target->s.origin;
    SV_Physics_Toss(missile);
    T_ASSERT(!missile->inuse);
    T_FEQ(target->health.value, hp, 0.001f);
    game.constants.combatConstantsLoaded = old_loaded; game.constants.defendDeflection = old_deflect;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_attack2_projectile_uses_launch_type_after_target_morph) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\n"
		"C;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"1\"\n"
		"C;Y2;X5;K\"0.3\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"100\"\nC;Y2;X9;K\"0.5\"\nC;Y2;X10;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	bool const old_loaded = game.constants.combatConstantsLoaded, old_deflect = game.constants.defendDeflection;
	UnitWeapons_t weapons = { .attacksEnabled = 3 };
	edict_t *attacker = make_hero(MAKEFOURCC('h','b','r','e'), 300, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
	edict_t *missile = G_Spawn();

	game.constants.combatConstantsLoaded = true; game.constants.defendDeflection = true;
	attacker->data.UnitWeapons = &weapons;
	attacker->unit_state->attack1.type = ATK_NORMAL; attacker->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
	attacker->unit_state->attack2.type = ATK_PIERCE; attacker->unit_state->attack2.targetsAllowed = WC3_TARGET_FLAG_AIR;
	target->targtype = TARG_AIR;
	G_InitMissile(missile); missile->missile->attack_type = attacker->unit_state->attack2.type; /* Launch-time snapshot. */
	target->targtype = TARG_GROUND; target->unit_state->defense_type = 0; /* Target morphed before impact. */
	unit_addstatus(target, "Adef", 1);
	missile->owner = attacker; missile->goalentity = target; missile->movetype = MOVETYPE_FLYMISSILE;
	missile->velocity = 100000.0f; missile->damage = 100; missile->s.origin = target->s.origin;
	SV_Physics_Toss(missile);

	T_ASSERT(missile->inuse); T_ASSERT(missile->missile->reflected); T_ASSERT(missile->goalentity == attacker);

	game.constants.combatConstantsLoaded = old_loaded; game.constants.defendDeflection = old_deflect;
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, defend_consumes_deflected_building_projectile_without_return_damage) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataB1\"\n"
		"C;Y1;X5;K\"DataC1\"\nC;Y1;X6;K\"DataD1\"\nC;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"DataF1\"\n"
		"C;Y1;X9;K\"DataG1\"\nC;Y1;X10;K\"DataH1\"\n"
		"C;Y2;X1;K\"Adef\"\nC;Y2;X2;K\"Adef\"\nC;Y2;X3;K\"0.5\"\nC;Y2;X4;K\"1\"\n"
		"C;Y2;X5;K\"0.3\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"100\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	bool const old_loaded = game.constants.combatConstantsLoaded, old_deflect = game.constants.defendDeflection;
	edict_t *tower = make_hero(MAKEFOURCC('h','t','o','w'), 500, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	edict_t *missile = G_Spawn();
	float const tower_hp = tower->health.value, target_hp = target->health.value;

	game.constants.combatConstantsLoaded = true; game.constants.defendDeflection = true;
	tower->unit_state->attack1.type = ATK_PIERCE; unit_addstatus(target, "Adef", 1);
	missile->owner = tower; missile->goalentity = target; missile->movetype = MOVETYPE_FLYMISSILE;
	G_InitMissile(missile); missile->missile->attack_type = ATK_PIERCE;
	missile->velocity = 100000.0f; missile->damage = 100; missile->s.origin = target->s.origin;
	SV_Physics_Toss(missile);
	T_ASSERT(!missile->inuse); T_FEQ(target->health.value, target_hp, 0.001f); T_FEQ(tower->health.value, tower_hp, 0.001f);

	game.constants.combatConstantsLoaded = old_loaded; game.constants.defendDeflection = old_deflect;
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, selected_common_ability_contracts_are_registered) {
	static cstring_t const passives[] = {
		"Abdt", "Arev", "Aawa", "Adet", "AHer", "Aalr", "Afih", "Afin", "Afio", "Afir", "Afiu", "Aloc", "Attu",
	};
	static struct { cstring_t code; abilityProc_t proc; } const commands[] = {
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
	uint32_t const code = MAKEFOURCC('A', 'f', 'i', 'r');

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
	edict_t *attacker = make_hero(MAKEFOURCC('N', 'n', 's', 'w'), 100, 100, 0, 0);

	attacker->unit_state->attack1.weapon = WPN_MISSILE;
	unit_addstatus(attacker, "AEpa", 1);
	T_EQ(S_SearingArrowDamage(attacker, 20), 33);
	attacker->unit_state->attack1.weapon = WPN_NORMAL;
	T_EQ(S_SearingArrowDamage(attacker, 20), 20);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, searing_arrows_adds_authored_damage_and_spends_mana_per_attack) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHfa\"\nC;Y2;X2;K\"AHfa\"\nC;Y2;X3;K\"7\"\nC;Y2;X4;K\"20\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker;
    edict_t *target;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    /* AHfa is a learned Hero ability, so it is owned through hero ranks and
     * absent from the unit's ordinary UnitAbilities list. */
    UnitAbilities_t abilities = { .abilList = "" };
    char target_number[16];
    cstring_t button[] = { "button", "AHfa" };
    cstring_t focus_target[] = { "focus", target_number };

    reset_entities(); setup_test_world();
    /* Allocate actors after installing the world fixture. */
    attacker = make_hero(MAKEFOURCC('n','n','s','w'), 100, 100, 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->client = client;
    client->ps.number = 0;
    attacker->data.UnitAbilities = &abilities;
    attacker->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','H','f','a'), .level = 1);
    attacker->s.player = 0;
    attacker->mana.value = attacker->mana.max_value = 30;
    attacker->unit_state->attack1.weapon = WPN_MISSILE;
    attacker->unit_state->attack1.type = ATK_NORMAL;
    attacker->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 1000;

    /* Clicking AHfa and choosing a target issues an attack order. The shot is
     * paid for at the attack damage point, and manual targeting does not turn
     * ongoing autocast on. */
    G_SelectEntity(client, attacker);
    G_SelectEntity(client, target);
    T_ASSERT(G_FocusSelectedUnit(client, attacker));
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);
    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    G_ClientCommand(clent, 2, focus_target);
    T_NULL(client->menu.on_entity_selected);
    T_ASSERT(attacker->goalentity == target);
    T_ASSERT(!G_UnitAutocastIsOn(attacker, MAKEFOURCC('A','H','f','a')));
    T_STREQ(S_SearingArrowsMissileArt(attacker), "Abilities\\Weapons\\SearingArrow\\Missile.mdl");
    T_FEQ(attacker->mana.value, 30.0f, 0.001f);
    S_ResolveAttackHit(attacker, target, 100);
    T_FEQ(attacker->mana.value, 23.0f, 0.001f);
    T_FEQ(target->health.value, 880.0f, 0.001f);
    T_NULL(S_SearingArrowsMissileArt(attacker));
    S_ResolveAttackHit(attacker, target, 100);
    T_FEQ(attacker->mana.value, 23.0f, 0.001f);
    T_FEQ(target->health.value, 780.0f, 0.001f);

    /* Right-click autocast applies the modifier on each shot while mana is
     * available; with no mana, the same target still takes a normal attack. */
    G_ClientCommand(clent, 2, (cstring_t[]){ "autocast", "AHfa" });
    T_ASSERT(G_UnitAutocastIsOn(attacker, MAKEFOURCC('A','H','f','a')));
    G_ClientCommand(clent, 2, (cstring_t[]){ "autocast", "AHfa" });
    T_ASSERT(!G_UnitAutocastIsOn(attacker, MAKEFOURCC('A','H','f','a')));
    G_ClientCommand(clent, 2, (cstring_t[]){ "autocast", "AHfa" });
    T_ASSERT(G_UnitAutocastIsOn(attacker, MAKEFOURCC('A','H','f','a')));
    attacker->mana.value = 7.0f;
    T_STREQ(S_SearingArrowsMissileArt(attacker), "Abilities\\Weapons\\SearingArrow\\Missile.mdl");
    S_ResolveAttackHit(attacker, target, 100);
    T_FEQ(attacker->mana.value, 0.0f, 0.001f);
    T_FEQ(target->health.value, 660.0f, 0.001f);
    S_ResolveAttackHit(attacker, target, 100);
    T_FEQ(attacker->mana.value, 0.0f, 0.001f);
    T_FEQ(target->health.value, 560.0f, 0.001f);
    T_NULL(S_SearingArrowsMissileArt(attacker));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, mana_shield_rank_ratios_absorb_damage_and_drain_authored_mana) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X14\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"DataA2\"\nC;Y1;X5;K\"DataA3\"\n"
		"C;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataB2\"\nC;Y1;X8;K\"DataB3\"\n"
		"C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"BuffID2\"\nC;Y1;X11;K\"BuffID3\"\n"
		"C;Y1;X12;K\"Cost1\"\nC;Y1;X13;K\"Cool1\"\nC;Y1;X14;K\"Area1\"\n"
		"C;Y2;X1;K\"ANms\"\nC;Y2;X2;K\"ANms\"\n"
		"C;Y2;X3;K1\nC;Y2;X4;K1.5\nC;Y2;X5;K2\n"
		"C;Y2;X6;K1\nC;Y2;X7;K1\nC;Y2;X8;K1\n"
		"C;Y2;X9;K\"BNms\"\nC;Y2;X10;K\"BNms\"\nC;Y2;X11;K\"BNms\"\n"
		"C;Y2;X12;K25\nC;Y2;X13;K10\nC;Y2;X14;K128\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = alloc_test_unit(MAKEFOURCC('o', 'g', 'r', 'u'), 50, 0);
	edict_t *caster = make_hero(MAKEFOURCC('h', 'p', 'e', 'a'), 1000, 200, 0, 0);
	static float const mana_after_hit[] = { 0.0f, 100.0f - 100.0f / 1.5f, 50.0f };
	static float const ratio[] = { 1.0f, 1.5f, 2.0f };
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A', 'N', 'm', 's'), .level = 1);
	T_FEQ(S_SpellNumber(MAKEFOURCC('A','N','m','s'), ABILITY_NUMBER_COST, 1), 25.0f, 0.001f);
	T_FEQ(S_SpellNumber(MAKEFOURCC('A','N','m','s'), ABILITY_NUMBER_COOLDOWN, 1), 10.0f, 0.001f);
	FOR_LOOP(rank, 3) {
		caster->unit_state->ranks[0].level = rank + 1;
		caster->health.value = 1000.0f;
		caster->mana.value = 100.0f;
		T_ASSERT(test_execute_code(caster, "ANms", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE)));
		T_ASSERT(S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
		T_Damage(caster, attacker, 100);
		T_FEQ(caster->health.value, 1000.0f, 0.01f);
		T_FEQ(caster->mana.value, mana_after_hit[rank], 0.01f);
		if (rank == 0) {
			T_ASSERT(!S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
		} else {
			T_ASSERT(S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
			T_Damage(caster, attacker, 200);
			T_FEQ(caster->mana.value, 0.0f, 0.01f);
			T_FEQ(caster->health.value, 1000.0f - (200.0f - mana_after_hit[rank] * ratio[rank]), 0.01f);
			T_ASSERT(!S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
		}
	}
	caster->unit_state->ranks[0].level = 1;
	caster->health.value = 1000.0f;
	caster->mana.value = 100.0f;
	T_ASSERT(S_CastNoTargetSpell(caster, MAKEFOURCC('A','N','m','s')));
	T_FEQ(caster->mana.value, 75.0f, 0.001f);
	T_ASSERT(!S_SpellCooldownReady(caster, MAKEFOURCC('A','N','m','s')));
	T_ASSERT(!S_CastNoTargetSpell(caster, MAKEFOURCC('A','N','m','s')));
	T_ASSERT(unit_issueimmediateorder(caster, "manashieldoff"));
	T_ASSERT(!S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	caster->mana.value = 100.0f;
	T_ASSERT(unit_issueimmediateorder(caster, "manashieldon"));
	T_ASSERT(S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	T_ASSERT(unit_issueimmediateorder(caster, "manashieldon"));
	T_ASSERT(S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	T_ASSERT(unit_issueimmediateorder(caster, "manashieldoff"));
	T_ASSERT(!S_UnitHasStatus(caster, MAKEFOURCC('B', 'N', 'm', 's')));
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, mana_shield_orders_require_ability_ownership) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\nC;Y1;X4;K\"DataA1\"\n"
		"C;Y2;X1;K\"ANms\"\nC;Y2;X2;K\"ANms\"\nC;Y2;X3;K\"BNms\"\nC;Y2;X4;K2\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	unit->mana.value = 10.0f;

	T_EQ(G_UnitAbilityLevel(unit, MAKEFOURCC('A','N','m','s')), 0);
	T_ASSERT(!unit_issueimmediateorder(unit, "manashieldon"));
	T_ASSERT(!S_UnitHasStatus(unit, MAKEFOURCC('B','N','m','s')));
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

static edict_t *force_nature_test_tree(float x, float y) {
	edict_t *tree = G_Spawn();
	tree->class_id = MAKEFOURCC('B','0','0','X');
	tree->s.class_id = tree->class_id;
	G_BindEntityData(tree);
	tree->s.origin = MAKE(vec3_t, x, y, 0);
	tree->s.origin2 = MAKE(vec2_t, x, y);
	tree->s.scale = 1.0f;
	tree->targtype = TARG_TREE;
	tree->health.value = tree->health.max_value = 200.0f;
	G_AddDestructable(tree);
	G_GetDestructable(tree)->item_table = (uint32_t)-1;
	G_GetDestructable(tree)->placement_solid = false;
	G_GetDestructable(tree)->pathing_active = false;
	return tree;
}

TEST(wc3_spell, force_of_nature_consumes_required_area_trees_for_treants) {
	static const char ability_slk[] =
		"ID;PWXL;N;EBB;Y2;X12\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
		"C;Y1;X4;K\"DataA2\"\nC;Y1;X5;K\"DataA3\"\n"
		"C;Y1;X6;K\"Area1\"\nC;Y1;X7;K\"Area2\"\nC;Y1;X8;K\"Area3\"\n"
		"C;Y1;X9;K\"Rng1\"\nC;Y1;X10;K\"Dur1\"\n"
		"C;Y1;X11;K\"Dur2\"\nC;Y1;X12;K\"Dur3\"\n"
		"C;Y2;X1;K\"AEfn\"\nC;Y2;X2;K\"AEfn\"\n"
		"C;Y2;X3;K2\nC;Y2;X4;K3\nC;Y2;X5;K4\n"
		"C;Y2;X6;K150\nC;Y2;X7;K225\nC;Y2;X8;K300\n"
		"C;Y2;X9;K800\nC;Y2;X10;K60\nC;Y2;X11;K60\nC;Y2;X12;K60\nE\n";
	static const char unit_ui_slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"unitUIID\"\nC;Y1;X2;K\"file\"\n"
		"C;Y1;X3;K\"modelScale\"\nC;Y1;X4;K\"scale\"\n"
		"C;Y2;X1;K\"efon\"\nC;Y2;X2;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\n"
		"C;Y2;X3;K1\nC;Y2;X4;K1\nE\n";
	static const char unit_data_slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"targType\"\nC;Y1;X3;K\"movetp\"\n"
		"C;Y1;X4;K\"collision\"\nC;Y1;X5;K\"propWin\"\n"
		"C;Y2;X1;K\"efon\"\nC;Y2;X2;K\"ground\"\nC;Y2;X3;K\"foot\"\n"
		"C;Y2;X4;K16\nC;Y2;X5;K60\nE\n";
	static const char unit_balance_slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"realHP\"\n"
		"C;Y1;X3;K\"realM\"\nC;Y1;X4;K\"mana0\"\nC;Y1;X5;K\"spd\"\n"
		"C;Y2;X1;K\"efon\"\nC;Y2;X2;K300\nC;Y2;X3;K0\n"
		"C;Y2;X4;K0\nC;Y2;X5;K270\nE\n";
	slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui;
	slkTestData_t *data_rows, *old_data, *balance_rows, *old_balance;
	edict_t *caster;
	abilityitem_t spell;
	abilityCall_t target_mode;
	static uint32_t const counts[] = { 2, 3, 4 };
	static float const areas[] = { 150.0f, 225.0f, 300.0f };
	uint32_t rank;

	reset_entities();
	setup_test_world();
	ability_rows = parse_slk_string(ability_slk); old_ability = G_SetSLKRows("AbilityData", ability_rows);
	ui_rows = parse_slk_string(unit_ui_slk); old_ui = G_SetSLKRows("UnitUI", ui_rows);
	data_rows = parse_slk_string(unit_data_slk); old_data = G_SetSLKRows("UnitData", data_rows);
	balance_rows = parse_slk_string(unit_balance_slk); old_balance = G_SetSLKRows("UnitBalance", balance_rows);
	caster = alloc_test_unit(MAKEFOURCC('E','k','g','r'), 0, 0);
	caster->health.value = caster->health.max_value = 500.0f;
	caster->mana.value = caster->mana.max_value = 500.0f;
	caster->svflags |= SVF_MONSTER;
	caster->stand = unit_stand;
	caster->s.player = 0;
	spell = S_AbilityItem(FS_SLKKey("AEfn"));
	T_EQ(spell.ability->target_type, SPELL_TARGET_POINT);
	target_mode = MAKE(abilityCall_t, .item = &spell);
	spell.ability->proc(caster, A_SPELL_TARGET_MODE, &target_mode);
	T_ASSERT(target_mode.spell_entity_target_mode);
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;

	for (rank = 1; rank <= 3; rank++) {
		float const center_x = (float)rank * 2000.0f;
		vec2_t const center = { center_x, 0.0f };
		spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = center);
		abilityCall_t call = MAKE(abilityCall_t, .item = &spell, .target = &target);
		spellTarget_t tree_target;
		abilityCall_t tree_call;
		edict_t *trees[4] = { NULL, NULL, NULL, NULL };
		edict_t *outside;
		uint32_t i, summon_count = 0;
		float const spacing = rank == 1 ? 30.0f : 36.0f;
		caster->unit_state->ranks[0] = MAKE(heroability_t,
			.code = MAKEFOURCC('A','E','f','n'), .level = rank);

		for (i = 0; i + 1 < counts[rank - 1]; i++)
			trees[i] = force_nature_test_tree(center_x + ((float)i - 1.0f) * spacing, 0.0f);
		T_ASSERT(!S_AbilityMessage(caster, A_VALIDATE, &call));
		trees[counts[rank - 1] - 1] = force_nature_test_tree(center_x + areas[rank - 1] * 0.5f, 0.0f);
		outside = force_nature_test_tree(center_x + areas[rank - 1] + 32.0f, 0.0f);
#if !BZ_COMPAT_WORKAROUNDS
        /* Without the omitted RoC UnitID fallback, no tree may be consumed. */
        T_ASSERT(!S_AbilityMessage(caster, A_VALIDATE, &call));
        S_AbilityMessage(caster, A_EXECUTE, &call);
        for (i = 0; i < counts[rank - 1]; i++) T_ASSERT(!G_GetDestructable(trees[i])->dead);
        continue;
#endif
		T_ASSERT(S_AbilityMessage(caster, A_VALIDATE, &call));
		tree_target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = trees[0]);
		tree_call = MAKE(abilityCall_t, .item = &spell, .target = &tree_target);
		T_ASSERT(S_AbilityMessage(caster, A_VALIDATE, &tree_call));
		tree_target.point = center;
		tree_target.type = SPELL_TARGET_POINT;
		tree_call.target = &tree_target;
		T_ASSERT(S_AbilityMessage(caster, A_VALIDATE, &tree_call));
		S_AbilityMessage(caster, A_EXECUTE, &call);

		for (i = 0; i < counts[rank - 1]; i++)
			T_ASSERT(G_GetDestructable(trees[i])->dead);
		T_ASSERT(!G_GetDestructable(outside)->dead);
		FILTER_EDICTS(summon, summon->summon_ability == spell.code && summon->class_id == MAKEFOURCC('e','f','o','n') &&
			Vector2_distance(&summon->s.origin2, &center) <= areas[rank - 1] + 64.0f) {
			bool near_consumed_tree = false;
			for (i = 0; i < counts[rank - 1]; i++)
				if (Vector2_distance(&summon->s.origin2, &trees[i]->s.origin2) <= 128.0f) near_consumed_tree = true;
			T_ASSERT(near_consumed_tree);
			summon_count++;
		}
		T_EQ(summon_count, counts[rank - 1]);
	}

	reset_entities();
	G_SetSLKRows("UnitBalance", old_balance); free_slk_rows(balance_rows);
	G_SetSLKRows("UnitData", old_data); free_slk_rows(data_rows);
	G_SetSLKRows("UnitUI", old_ui); free_slk_rows(ui_rows);
	G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

TEST(wc3_spell, entangling_roots_is_a_timed_unit_spell) {
	abilityitem_t roots_item = S_AbilityItem(FS_SLKKey("AEer"));
	ability_t const *roots = roots_item.ability;

	T_NOT_NULL(roots);
	T_ASSERT(roots->flags & AB_SPELL);
	T_EQ((int)roots_item.code, (int)MAKEFOURCC('A', 'E', 'e', 'r'));
	T_EQ((int)roots->target_type, (int)SPELL_TARGET_UNIT);
}

TEST(wc3_spell, frost_nova_spawns_target_effect_on_primary_victim) {
    static char const ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"DataB1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"HeroDur1\"\n"
        "C;Y2;X1;K\"AUfn\"\nC;Y2;X2;K\"AUfn\"\nC;Y2;X3;K\"10\"\n"
        "C;Y2;X4;K\"20\"\nC;Y2;X5;K\"300\"\nC;Y2;X6;K\"Bfro\"\nC;Y2;X7;K\"5\"\nE\n";
    static char const buff_slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\n"
        "C;Y2;X1;K\"Bfro\"\nC;Y2;X2;K\"Bfro\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
    slkTestData_t *ability_rows = parse_slk_string(ability_slk), *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','n','e','c'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 72, 0);
    uint32_t target_effects = 0;

    caster->s.player = 0;
    target->s.player = nearby->s.player = 1;
    target->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER;
    target->health.value = target->health.max_value = 100;
    nearby->health.value = nearby->health.max_value = 100;
    nearby->s.origin2.x = 100;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));

    T_ASSERT(test_execute_code(caster, "AUfn", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    T_FEQ(target->health.value, 70.0f, 0.01f);
    T_FEQ(nearby->health.value, 90.0f, 0.01f);
    FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','f','r','o') && effect->goalentity) {
        T_ASSERT(effect->s.model > 0);
        target_effects++;
    }
    T_EQ(target_effects, 2);
    T_ASSERT(unit_findstatus(target, MAKEFOURCC('B','f','r','o'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
    T_ASSERT(unit_findstatus(nearby, MAKEFOURCC('B','f','r','o'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
    T_EQ(G_UnitStatusLevel(target, MAKEFOURCC('B','f','r','o')), 1);
    T_EQ(G_UnitStatusLevel(nearby, MAKEFOURCC('B','f','r','o')), 1);

    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
    G_SetSLKRows("AbilityBuffData", old_buff); free_slk_rows(buff_rows);
}

TEST(wc3_spell, frost_armor_enables_buff_target_art) {
    static char const ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\n"
        "C;Y2;X1;K\"AUfa\"\nC;Y2;X2;K\"AUfa\"\nC;Y2;X3;K\"BUfa\"\n"
        "C;Y2;X4;K\"45\"\nC;Y2;X5;K\"3\"\nE\n";
    slkTestData_t *rows = parse_slk_string(ability_slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','n','e','c'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    heroabilitystatus_t *status;

    caster->s.player = 0; target->s.player = 0;
    T_ASSERT(test_execute_code(caster, "AUfa", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    status = unit_findstatus(target, MAKEFOURCC('B','U','f','a'));
    T_NOT_NULL(status);
    if (status) T_ASSERT(status->buff_flags & WC3_STATUS_BUFF_TARGET_ART);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, entangling_roots_visual_resolves_from_buff_target_art) {
	cstring_t art = G_AbilityEffectArt(MAKEFOURCC('B', 'E', 'e', 'r'), WC3_EFFECT_TARGET, 0);

	T_STREQ(art, "Abilities\\Spells\\NightElf\\EntanglingRoots\\EntanglingRootsTarget.mdl");
}

TEST(wc3_spell, entangling_roots_visual_follows_status_through_recast_and_save_load) {
    static char const ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y2;X1;K\"AEer\"\nC;Y2;X2;K\"AEer\"\nC;Y2;X3;K\"BEer\"\n"
        "C;Y2;X4;K\"8\"\nC;Y2;X5;K\"8\"\nC;Y2;X6;K\"4\"\nE\n";
    static char const buff_slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\n"
        "C;Y2;X1;K\"BEer\"\nC;Y2;X2;K\"BEer\"\n"
        "C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
    cstring_t const save_path = Test_TempPath("roots-visual-save.bin");
    slkTestData_t *ability_rows = parse_slk_string(ability_slk), *old_ability;
    slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff;
    edict_t *caster, *target, *effect = NULL;
    uint32_t target_number;

    reset_entities(); setup_test_world(); level.time = 1000;
    old_ability = G_SetSLKRows("AbilityData", ability_rows);
    old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
    caster = make_hero(MAKEFOURCC('E','k','e','e'), 500, 200, 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    caster->s.player = 0; target->s.player = 1;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;

    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) {
        T_NULL(effect);
        effect = found;
    }
    T_NOT_NULL(effect);
    T_ASSERT(unit_findstatus(target, MAKEFOURCC('B','E','e','r'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
    target_number = target->s.number;

    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    effect = NULL;
    FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) {
        effect = found;
        break;
    }
    T_NOT_NULL(effect);
    {
        uint32_t count = 0;
        FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) count++;
        T_EQ(count, 1);
    }

    T_ASSERT(WriteGame(save_path));
    T_ASSERT(ReadGame(save_path));
    target = g_edicts + target_number;
    caster = g_edicts + caster->s.number;
    {
        uint32_t count = 0;
        FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) {
            effect = found;
            count++;
        }
        T_EQ(count, 1);
    }
    {
        edict_t *second_source = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
        status_application_t independent = {
            .buff = "BEer", .level = 1, .duration = 8.0f,
            .stack_policy = WC3_STATUS_STACK_INDEPENDENT, .source = second_source,
            .buff_flags = WC3_STATUS_BUFF_TARGET_ART
        };
        heroabilitystatus_t *second = unit_applystatus(target, &independent);
        T_NOT_NULL(second);
        unit_removestatus(target, unit_findstatus(target, MAKEFOURCC('B','E','e','r')), STATUS_REMOVE_SCRIPT);
        T_EQ(effect->status_effect_code, MAKEFOURCC('B','E','e','r'));
        unit_removestatus(target, second, STATUS_REMOVE_SCRIPT);
    }
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','E','e','r')));
    T_EQ(effect->status_effect_code, 0);

    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    effect = NULL;
    FILTER_EDICTS(found, found->status_effect_code == MAKEFOURCC('B','E','e','r') && found->goalentity == target) {
        T_NULL(effect);
        effect = found;
    }
    T_NOT_NULL(effect);
    unit_statusdeath(target);
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','E','e','r')));
    T_EQ(effect->status_effect_code, 0);

    remove(save_path);
    G_SetSLKRows("AbilityBuffData", old_buff); free_slk_rows(buff_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

TEST(wc3_spell, entangling_roots_tracks_source_interrupts_channel_and_ticks_authored_dps) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"targs\"\nC;Y1;X8;K\"Rng1\"\n"
        "C;Y2;X1;K\"AEer\"\nC;Y2;X2;K\"AEer\"\nC;Y2;X3;K\"BEer\"\n"
        "C;Y2;X4;K\"3\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"12\"\n"
        "C;Y2;X7;K\"ground,enemy,neutral,organic\"\nC;Y2;X8;K\"600\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('E','k','e','e'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    heroabilitystatus_t *slot;

    old = G_SetSLKRows("AbilityData", rows);
    level.time = 1000;
    caster->s.player = 0; target->s.player = 1;
    target->health.value = target->health.max_value = 100.0f;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    G_AddChannel(target);
    G_GetChannel(target)->code = MAKEFOURCC('A','H','m','t');

    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    slot = unit_findstatus(target, MAKEFOURCC('B','E','e','r'));
    T_NOT_NULL(slot);
    T_EQ(slot->data, MAKEFOURCC('A','E','e','r'));
    T_EQ(slot->rank, 1);
    T_EQ(slot->source, caster);
    T_EQ(slot->source_spawn_time, caster->spawn_time);
    T_ASSERT(!G_GetChannel(target) || G_GetChannel(target)->code == 0);
    T_ASSERT(!S_UnitCanTranslate(target));

    level.time += 1000;
    unit_updatestatuses(target);
    T_FEQ(target->health.value, 88.0f, 0.001f);

    unit_expirestatus(target, slot);
    T_ASSERT(S_UnitCanTranslate(target));
    level.time += 1000;
    unit_updatestatuses(target);
    T_FEQ(target->health.value, 88.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


TEST(wc3_spell, entangling_roots_death_cleanup_and_failed_status_do_not_interrupt) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y2;X1;K\"AEer\"\nC;Y2;X2;K\"AEer\"\nC;Y2;X3;K\"BEer\"\n"
        "C;Y2;X4;K\"3\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"12\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('E','k','e','e'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    heroabilitystatus_t *slot;

    old = G_SetSLKRows("AbilityData", rows);
    level.time = 1000; caster->s.player = 0; target->s.player = 1;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    slot = unit_findstatus(target, MAKEFOURCC('B','E','e','r'));
    T_NOT_NULL(slot);
    unit_statusdeath(target);
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','E','e','r')));

    memset(target->unit_state->statuses, 0, sizeof(target->unit_state->statuses));
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        target->unit_state->statuses[i].code = MAKEFOURCC('X','0' + (i / 10), '0' + (i % 10), 'x');
        target->unit_state->statuses[i].level = 1;
        target->unit_state->statuses[i].timestamp = G_Time() + 60000;
    }
    G_AddChannel(target);
    G_GetChannel(target)->code = MAKEFOURCC('A','H','m','t');
    T_ASSERT(test_execute_code(caster, "AEer", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    T_NULL(unit_findstatus(target, MAKEFOURCC('B','E','e','r')));
    T_EQ(G_GetChannel(target)->code, MAKEFOURCC('A','H','m','t'));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, entangling_roots_alias_uses_own_data_and_hero_duration) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y2;X1;K\"Aenr\"\nC;Y2;X2;K\"Aenr\"\nC;Y2;X3;K\"BEer\"\n"
        "C;Y2;X4;K\"7\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"19\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('E','k','e','e'), 500, 200, 0, 0);
    edict_t *target = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 64, 0);
    edict_t *resistant = alloc_test_unit(MAKEFOURCC('n','n','r','g'), 96, 0);
    heroabilitystatus_t *slot;

    old = G_SetSLKRows("AbilityData", rows);
    level.time = 1000; caster->s.player = 0; target->s.player = 1;
    G_InitUnitOrders(target);
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    resistant->svflags |= SVF_MONSTER; resistant->targtype = TARG_GROUND;
    resistant->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','C','s','k'), .level = 1);
    resistant->health.value = resistant->health.max_value = 100.0f;
    T_ASSERT(test_execute_code(caster, "Aenr", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    slot = unit_findstatus(target, MAKEFOURCC('B','E','e','r'));
    T_NOT_NULL(slot);
    T_EQ(slot->data, MAKEFOURCC('A','e','n','r'));
    T_EQ(slot->rank, 1);
    T_EQ(slot->duration_ms, 2000);
    level.time += 1000; unit_updatestatuses(target);
    T_FEQ(target->health.value, 81.0f, 0.001f);

    T_ASSERT(S_UnitIsResistant(resistant));
    T_ASSERT(test_execute_code(caster, "Aenr",
        MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = resistant)));
    slot = unit_findstatus(resistant, MAKEFOURCC('B','E','e','r'));
    T_NOT_NULL(slot);
    if (slot) T_EQ(slot->duration_ms, 2000);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, death_and_decay_uses_percentage_damage_and_enemy_filter) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\n"
		"C;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"DataA1\"\n"
		"C;Y1;X5;K\"DataB1\"\n"
		"C;Y1;X6;K\"Area1\"\n"
		"C;Y1;X7;K\"EfctID1\"\n"
		"C;Y1;X8;K\"HeroDur1\"\n"
		"C;Y2;X1;K\"AUdd\"\n"
		"C;Y2;X2;K\"AUdd\"\n"
		"C;Y2;X3;K\"35\"\n"
		"C;Y2;X4;K\".04\"\n"
		"C;Y2;X5;K\"1\"\n"
		"C;Y2;X6;K\"300\"\n"
		"C;Y2;X7;K\"XUdd\"\n"
		"C;Y2;X8;K\"35\"\n"
		"E\n";
	const char effect_slk[] =
		"ID;PWXL;N;EBB;Y2;X3\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"EffectArt\"\n"
		"C;Y2;X1;K\"XUdd\"\nC;Y2;X2;K\"XUdd\"\n"
		"C;Y2;X3;K\"TestUI\\\\Models\\\\anim_oneshot.mdx\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk);
	slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *effect_rows = parse_slk_string(effect_slk);
	slkTestData_t *old_effects = G_SetSLKRows("AbilityBuffData", effect_rows);
	uint32_t area_effect_model = G_RegisterModel(G_AbilityEffectArt(MAKEFOURCC('X','U','d','d'), WC3_EFFECT_EFFECT, 0));
	edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 250, 100, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
	edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 33, 0);
	edict_t *thinker, *area_effects[20] = { NULL };
	uint32_t thinker_slot = globals.num_edicts;
	uint32_t area_effect_count = 0;
	spellTarget_t st = { .type = SPELL_TARGET_POINT, .point = caster->s.origin2 };

	caster->s.player = 0;
	enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	enemy->health.value = enemy->health.max_value = 100;
	G_AddDestructable(tree);
	tree->svflags &= ~SVF_MONSTER; tree->targtype = TARG_TREE;
	tree->health.value = tree->health.max_value = 500;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
    UnitAbilities_t abilities = { .abilList = "AUdd" };
    caster->data.UnitAbilities = &abilities;
    T_ASSERT(S_CastPointTargetSpell(caster, FS_SLKKey("AUdd"), &st.point));
	thinker = &globals.edicts[thinker_slot];
	T_FEQ(enemy->health.value, 96.0f, 0.01f);
	T_FEQ(tree->health.value, 480.0f, 0.01f);
	T_FEQ(caster->health.value, 250.0f, 0.01f);
	T_ASSERT(thinker->inuse);
	T_EQ(thinker->spawn_time, G_Time() + 35000);
	T_ASSERT(area_effect_model > 0);
	FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) {
		T_ASSERT(area_effect_count < 20);
		T_EQ(effect->s.model, area_effect_model);
		T_FEQ(effect->s.scale, 1.0f, 0.001f);
		T_ASSERT(Vector2_distance(&effect->s.origin2, &st.point) <= 300.0f);
		T_ASSERT(effect->currentmove == &death_and_decay_area_effect_move);
		T_ASSERT(effect->currentmove->endfunc == G_FreeEdict);
		area_effects[area_effect_count++] = effect;
	}
	T_EQ(area_effect_count, 10);
	T_ASSERT(Vector2_distance(&area_effects[0]->s.origin2, &area_effects[1]->s.origin2) > 0.001f);
	/* Effects are emitted only with damage pulses, once per authored second. */
	level.time = 999;
	death_and_decay_think(thinker);
	T_FEQ(tree->health.value, 480.0f, 0.01f);
	area_effect_count = 0;
	FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) area_effect_count++;
	T_EQ(area_effect_count, 10);
	level.time = 1000;
	death_and_decay_think(thinker);
	T_FEQ(tree->health.value, 460.0f, 0.01f);
	area_effect_count = 0;
	FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) {
		T_ASSERT(area_effect_count < 20);
		area_effects[area_effect_count++] = effect;
	}
	T_EQ(area_effect_count, 20);
	level.time = thinker->spawn_time;
	thinker->think(thinker);
	T_ASSERT(!thinker->inuse);
	area_effect_count = 0;
	FILTER_EDICTS(effect, effect->owner == thinker && effect->s.model) area_effect_count++;
	T_EQ(area_effect_count, 0);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
	G_SetSLKRows("AbilityBuffData", old_effects);
	free_slk_rows(effect_rows);
}

TEST(wc3_spell, death_and_decay_roc_effect_id_uses_authored_effect_object) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X2\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y2;X1;K\"AUdd\"\nC;Y2;X2;K\"AUdd\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
#if BZ_COMPAT_WORKAROUNDS
	T_EQ(G_AbilityEffectCode(MAKEFOURCC('A','U','d','d'), 1), MAKEFOURCC('X','U','d','d'));
#else
	T_EQ(G_AbilityEffectCode(MAKEFOURCC('A','U','d','d'), 1), MAKEFOURCC('A','U','d','d'));
#endif
	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, holy_light_rawcode_lookup_is_nul_safe) {
	uint32_t code = MAKEFOURCC('A','H','h','b');
	ability_t const *spell = S_SpellAbilityForCode(code);
	ability_t const *abil = FindAbilityForCommand(GetClassName(code));

	/* Runtime spell dispatch starts from a uint32_t rawcode. This specifically
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


/* Holy Light must not commit mana/cooldown when the friendly target cannot be healed. */
TEST(wc3_spell, holy_light_rejects_full_health_friendly_target) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = 0;
    target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500;
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AHhb"), target));
    T_FEQ(caster->mana.value, 200, 0.001f);
    T_ASSERT(S_SpellCooldownReady(caster, FS_SLKKey("AHhb")));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Death Coil is a homing spell missile; healing/damage occurs on impact, not cast commitment. */
TEST(wc3_spell, death_coil_uses_projectile_and_rejects_self_or_full_health_ally) {
    static UnitData_t undead_data = { .race = STR_UNDEAD };
    static UnitData_t human_data = { .race = STR_HUMAN };
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AUdc\"\nC;Y2;X2;K\"AUdc\"\nC;Y2;X3;K\"air,ground,friend,enemy\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"800\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AUdc" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *ally = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 120, 0);
    edict_t *missile = NULL;

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities; caster->data.UnitData = &undead_data; caster->s.player = 0;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    enemy->data.UnitData = &human_data; enemy->s.player = 1; enemy->svflags |= SVF_MONSTER; enemy->targtype = TARG_GROUND;
    enemy->health.value = enemy->health.max_value = 500;
    ally->data.UnitData = &undead_data; ally->s.player = 0; ally->svflags |= SVF_MONSTER; ally->targtype = TARG_GROUND;
    ally->health.value = ally->health.max_value = 500;

    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), caster));
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), ally));
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), enemy));
    T_FEQ(enemy->health.value, 500, 0.001f);
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    if (missile) missile->currentmove->endfunc(missile);
    T_FEQ(enemy->health.value, 400, 0.001f);

    /* Mixed friend/enemy target masks still block enemy Death Coil at impact. */
    unit_addstatus(enemy, "BNss", 1);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), enemy)); missile = NULL;
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    T_EQ(G_UnitStatusLevel(enemy, MAKEFOURCC('B','N','s','s')), 1);
    if (missile) missile->currentmove->endfunc(missile);
    T_FEQ(enemy->health.value, 400, 0.001f);
    T_EQ(G_UnitStatusLevel(enemy, MAKEFOURCC('B','N','s','s')), 0);

    /* A reused target edict must not let an in-flight coil affect a new incarnation. */
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), enemy)); missile = NULL;
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    if (missile) { enemy->spawn_time++; missile->currentmove->endfunc(missile); T_ASSERT(!missile->inuse); }
    T_FEQ(enemy->health.value, 400, 0.001f);

    ally->health.value = 250; missile = NULL;
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdc"), ally));
    T_FEQ(ally->health.value, 250, 0.001f);
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) { missile = ent; break; }
    T_NOT_NULL(missile);
    if (missile) missile->currentmove->endfunc(missile);
    T_FEQ(ally->health.value, 450, 0.001f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Divine Shield exposes the authored buff while the same timer owns invulnerability. */
TEST(wc3_spell, divine_shield_applies_authored_buff_for_its_duration) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AHds\"\nC;Y2;X2;K\"AHds\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"2\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"BHds\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHds" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *thinker = NULL;

    old = G_SetSLKRows("AbilityData", rows); caster->data.UnitAbilities = &abilities;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHds")));
    T_ASSERT(G_UnitHasFlag(caster, UNIT_FLAG_INVULNERABLE)); T_EQ(G_UnitStatusLevel(caster, FS_SLKKey("BHds")), 1);
    FILTER_EDICTS(ent, ent->owner == caster && ent->think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_ASSERT(WriteGame(Test_TempPath("divine-shield-save.bin")));
    G_AddChannel(caster);
    G_GetChannel(caster)->code = 0; if (thinker) thinker->think = NULL;
    T_ASSERT(ReadGame(Test_TempPath("divine-shield-save.bin")));
    FILTER_EDICTS(ent, ent->owner == caster && ent->think == divine_shield_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    if (thinker) { level.time = thinker->spawn_time; G_RunEntity(thinker); }
    unit_updatestatuses(caster);
    T_ASSERT(!G_UnitHasFlag(caster, UNIT_FLAG_INVULNERABLE)); T_EQ(G_UnitStatusLevel(caster, FS_SLKKey("BHds")), 0);
    remove(Test_TempPath("divine-shield-save.bin"));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, divine_shield_expiry_rejects_recycled_caster_slot) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AHds\"\nC;Y2;X2;K\"AHds\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"2\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"BHds\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHds" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *thinker = NULL;

    old = G_SetSLKRows("AbilityData", rows); caster->data.UnitAbilities = &abilities;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHds")));
    FILTER_EDICTS(ent, ent->owner == caster && ent->think == divine_shield_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_NOT_NULL(thinker ? G_GetChannel(thinker) : NULL);
    caster->spawn_time++;
    G_SetUnitFlag(caster, UNIT_FLAG_INVULNERABLE, true);
    if (thinker) { level.time = thinker->spawn_time; divine_shield_think(thinker); }
    T_ASSERT(G_UnitHasFlag(caster, UNIT_FLAG_INVULNERABLE));
    T_ASSERT(!thinker || !thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Animate Dead rejects an empty cast, prefers the higher-level corpse, and
 * consumes that corpse into non-raisable temporary summon state. */
TEST(wc3_spell, animate_dead_prefers_higher_level_corpse_and_restores_temporary_state) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"targs\"\n"
        "C;Y2;X1;K\"AUan\"\nC;Y2;X2;K\"AUan\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"40\"\nC;Y2;X5;K\"900\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"1\"\nC;Y2;X8;K\"ground,dead\"\nE\n";
    static UnitBalance_t const low_balance = { .level = 1 }, high_balance = { .level = 5 };
    UnitData_t corpse_data = { .deathType = 3 };
    UnitAbilities_t abilities = { .abilList = "AUan" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *low = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *high = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 120, 0);

    old = G_SetSLKRows("AbilityData", rows); caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    low->data.UnitBalance = &low_balance; high->data.UnitBalance = &high_balance;
    low->data.UnitData = high->data.UnitData = &corpse_data;
    high->unit_state->overrides.added[0] = MAKEFOURCC('A','I','n','v'); ARRAY_COUNT(high->unit_state->overrides.added) = 1;
    edict_t *corpses[] = { low, high };
    FOR_LOOP(i, 2) {
        corpses[i]->s.player = 1; corpses[i]->svflags |= SVF_MONSTER;
        corpses[i]->stand = unit_stand; corpses[i]->die = unit_die;
        corpses[i]->health.value = corpses[i]->health.max_value = 500;
    }
    T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("AUan")));
    unit_die(low, caster); unit_die(high, caster); high->aiflags |= AI_HOLD_FRAME;
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AUan")));
    T_ASSERT(M_IsDead(low)); T_ASSERT(!M_IsDead(high)); T_EQ(high->s.player, 0); T_ASSERT(high->owner == caster);
    T_ASSERT(!(high->aiflags & AI_HOLD_FRAME)); T_EQ(G_UnitStatusLevel(high, FS_SLKKey("BTLF")), 1);
    T_ASSERT(G_UnitHasFlag(high, UNIT_FLAG_VERTEX_COLOR_SET));
    T_EQ(high->vertex_color.r, 255); T_EQ(high->vertex_color.g, 0);
    T_EQ(high->vertex_color.b, 0); T_EQ(high->vertex_color.a, 255);
    {
        uint32_t target_effects = 0;
        FILTER_EDICTS(effect, effect->goalentity == high && effect->damage == high->spawn_time && effect->s.model)
            target_effects++;
        T_EQ(target_effects, 1);
    }
    T_ASSERT(!G_GetFood(high) || G_GetFood(high)->used == 0); T_EQ(high->summon_ability, FS_SLKKey("AUan"));
    T_ASSERT(high->aiflags & AI_CORPSE_UNRAISABLE); T_ASSERT(high->aiflags & AI_CORPSE_NO_DECAY); T_ASSERT(G_UnitHasFlag(high, UNIT_FLAG_INVULNERABLE));
    T_EQ(ARRAY_COUNT(high->unit_state->overrides.added), 1); T_EQ(high->unit_state->overrides.added[0], MAKEFOURCC('A','I','n','v'));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Resurrection uses the same high-level-first corpse preference while retaining
 * the original owner and ordinary permanent corpse revival contract. */
TEST(wc3_spell, resurrection_prefers_higher_level_friendly_corpse) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Cost1\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"targs\"\n"
        "C;Y2;X1;K\"AHre\"\nC;Y2;X2;K\"AHre\"\nC;Y2;X3;K\"0\"\n"
        "C;Y2;X4;K\"900\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\nC;Y2;X7;K\"ground,friend,dead\"\nE\n";
    static UnitBalance_t const low_balance = { .level = 1 }, high_balance = { .level = 5 };
    UnitData_t corpse_data = { .deathType = 3 };
    UnitAbilities_t abilities = { .abilList = "AHre" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *low = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *high_far = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 300, 0);
    edict_t *high_near = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 120, 0);

    old = G_SetSLKRows("AbilityData", rows); caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    low->data.UnitBalance = &low_balance;
    high_far->data.UnitBalance = high_near->data.UnitBalance = &high_balance;
    low->data.UnitData = high_far->data.UnitData = high_near->data.UnitData = &corpse_data;
    edict_t *corpses[] = { low, high_far, high_near };
    FOR_LOOP(i, 3) {
        corpses[i]->s.player = 0; corpses[i]->svflags |= SVF_MONSTER;
        corpses[i]->stand = unit_stand; corpses[i]->die = unit_die;
        corpses[i]->health.value = corpses[i]->health.max_value = 500;
        unit_die(corpses[i], caster);
    }
    T_ASSERT(S_CastNoTargetSpell(caster, FS_SLKKey("AHre")));
    T_ASSERT(M_IsDead(low)); T_ASSERT(M_IsDead(high_far)); T_ASSERT(!M_IsDead(high_near));
    T_EQ(high_near->s.player, 0); T_ASSERT(high_near->owner != caster);
    T_ASSERT(!(high_near->aiflags & AI_CORPSE_UNRAISABLE)); T_ASSERT(!(high_near->aiflags & AI_CORPSE_NO_DECAY)); T_ASSERT(G_UnitHasFlag(high_near, UNIT_FLAG_INVULNERABLE));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Warcraft III 2.0.3 added AUa2 as the ability-preserving Animate Dead rawcode. */
TEST(wc3_spell, animate_dead_modern_aua2_registration) {
    abilityitem_t item = S_AbilityItem(FS_SLKKey("AUa2"));
    T_NOT_NULL(item.ability);
    T_EQ(item.ability->proc, CAbilityAnimateDead);
}

/* Stock-style Death Pact requires a friendly non-Hero Undead victim and useful restoration. */
TEST(wc3_spell, death_pact_validates_undead_nonhero_and_full_resources) {
    static UnitData_t undead_data = { .race = STR_UNDEAD };
    static UnitData_t human_data = { .race = STR_HUMAN };
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\n"
        "C;Y2;X1;K\"AUdp\"\nC;Y2;X2;K\"AUdp\"\nC;Y2;X3;K\"air,ground,friend,invu\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"800\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"2\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AUdp" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *victim = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 100, 0);
    edict_t *living = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities; caster->data.UnitData = &undead_data; caster->s.player = 0;
    victim->data.UnitData = &undead_data; victim->s.player = 0; victim->svflags |= SVF_MONSTER; victim->targtype = TARG_GROUND;
    victim->health.value = victim->health.max_value = 100; victim->die = unit_die;
    living->data.UnitData = &human_data; living->s.player = 0; living->svflags |= SVF_MONSTER; living->targtype = TARG_GROUND;
    living->health.value = living->health.max_value = 100;

    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    caster->health.value = 100;
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), living));
    G_SetUnitFlag(victim, UNIT_FLAG_INVULNERABLE, true);
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    T_FEQ(caster->health.value, 300, 0.001f);
    T_ASSERT(M_IsDead(victim)); T_ASSERT(victim->aiflags & AI_CORPSE_UNRAISABLE); T_ASSERT(victim->aiflags & AI_CORPSE_NO_DECAY);
    T_ASSERT(!G_UnitIsRaisableCorpse(victim));
    /* Death animation completion must skip the ordinary 90-second corpse window. */
    T_NOT_NULL(victim->currentmove);
    if (victim->currentmove && victim->currentmove->endfunc) victim->currentmove->endfunc(victim);
    T_ASSERT(victim->inuse);
    level.time += FRAMETIME;
    if (victim->currentmove && victim->currentmove->think) victim->currentmove->think(victim);
    T_ASSERT(!victim->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, death_pact_datae_can_leave_target_alive) {
    static UnitData_t undead_data = { .race = STR_UNDEAD };
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y1;X10;K\"DataD1\"\nC;Y1;X11;K\"DataE1\"\n"
        "C;Y2;X1;K\"AUdp\"\nC;Y2;X2;K\"AUdp\"\nC;Y2;X3;K\"air,ground,friend,invu\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"800\"\n"
        "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\nC;Y2;X10;K\"0\"\nC;Y2;X11;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AUdp" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *victim = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 100, 0);

    caster->data.UnitAbilities = &abilities; caster->data.UnitData = &undead_data; caster->s.player = 0;
    caster->health.value = 100;
    victim->data.UnitData = &undead_data; victim->s.player = 0; victim->svflags |= SVF_MONSTER;
    victim->targtype = TARG_GROUND; victim->health.value = victim->health.max_value = 100; victim->die = unit_die;

    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    T_FEQ(caster->health.value, 200.0f, 0.001f);
    T_FEQ(victim->health.value, 100.0f, 0.001f);
    T_ASSERT(!M_IsDead(victim));
    T_ASSERT(!(victim->aiflags & AI_CORPSE_UNRAISABLE));
    T_ASSERT(!(victim->aiflags & AI_CORPSE_NO_DECAY));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}


/* DataC/DataD switch Death Pact from current-life percentages to Warsmash's
 * authored fixed-value conversion mode. The fixed values are removed from the
 * caster resources and their sum is removed from the victim's life. */
TEST(wc3_spell, death_pact_datac_datad_fixed_value_conversion) {
    static UnitData_t undead_data = { .race = STR_UNDEAD };
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y1;X10;K\"DataD1\"\nC;Y1;X11;K\"DataE1\"\n"
        "C;Y2;X1;K\"AUdp\"\nC;Y2;X2;K\"AUdp\"\nC;Y2;X3;K\"air,ground,friend\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"800\"\n"
        "C;Y2;X7;K\"25\"\nC;Y2;X8;K\"40\"\nC;Y2;X9;K\"1\"\n"
        "C;Y2;X10;K\"1\"\nC;Y2;X11;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AUdp" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','d','e','a'), 500, 200, 0, 0);
    edict_t *victim = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 100, 0);

    caster->data.UnitAbilities = &abilities; caster->data.UnitData = &undead_data; caster->s.player = 0;
    caster->health.value = 300.0f; caster->mana.value = 150.0f;
    victim->data.UnitData = &undead_data; victim->s.player = 0; victim->svflags |= SVF_MONSTER;
    victim->targtype = TARG_GROUND; victim->health.value = victim->health.max_value = 200.0f;
    victim->mana.value = victim->mana.max_value = 100.0f; victim->die = unit_die;

    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    T_FEQ(caster->health.value, 260.0f, 0.001f);
    T_FEQ(caster->mana.value, 125.0f, 0.001f);
    T_FEQ(victim->health.value, 135.0f, 0.001f);
    T_ASSERT(!M_IsDead(victim));

    /* DataC value-mode has the Warcraft/Warsmash unit-with-mana target gate. */
    victim->mana.value = 0.0f;
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("AUdp"), victim));
    T_FEQ(caster->health.value, 260.0f, 0.001f);
    T_FEQ(caster->mana.value, 125.0f, 0.001f);
    T_FEQ(victim->health.value, 135.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
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
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0), *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    char number[16];
    cstring_t button[] = { "button", "AHhb" }, select[] = { "select", number };

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

TEST(wc3_spell, banish_accepts_self_friendly_and_enemy_command_targets) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X13;K\"DataE1\"\n"
        "C;Y1;X14;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AHbn\"\nC;Y2;X2;K\"AHbn\"\n"
        "C;Y2;X3;K\"air,ground,nonsapper,organic\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X6;K\"800\"\nC;Y2;X7;K\"12\"\nC;Y2;X8;K\"4\"\n"
        "C;Y2;X9;K\"0.5\"\nC;Y2;X14;K\"BHbn\"\n"
        "C;Y3;X1;K\"AHdr\"\nC;Y3;X2;K\"AHdr\"\n"
        "C;Y3;X3;K\"air,ground,organic,notself\"\nC;Y3;X4;K\"10\"\n"
        "C;Y3;X5;K\"0\"\nC;Y3;X6;K\"600\"\nC;Y3;X7;K\"6\"\n"
        "C;Y3;X8;K\"6\"\nC;Y3;X9;K\"800\"\nC;Y3;X11;K\"15\"\n"
        "C;Y3;X12;K\"1\"\nC;Y3;X13;K\"30\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHbn,AHdr" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('H','b','l','m'), 500, 200, 0, 0);
    edict_t *clent = &g_edicts[0], *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 75, 0);
    gameClient_t *client = &game.clients[0];
    char target_number[16];
    cstring_t button[] = { "button", "AHbn" }, siphon_button[] = { "button", "AHdr" };
    cstring_t select_target[] = { "select", target_number };

    clent->client = client;
    caster->data.UnitAbilities = &abilities;
    caster->s.player = ally->s.player = 0;
    caster->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 500;
    enemy->s.player = 1;
    ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
    ally->targtype = enemy->targtype = TARG_GROUND;
    ally->health.value = ally->health.max_value = 500;
    enemy->health.value = enemy->health.max_value = 500;
    ally->mana.value = 50; ally->mana.max_value = 100;
    enemy->mana.value = enemy->mana.max_value = 100;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SelectEntity(client, caster);

    snprintf(target_number, sizeof(target_number), "%u", (unsigned)caster->s.number);
    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    G_ClientCommand(clent, 2, select_target);
    T_ASSERT(S_UnitHasStatus(caster, MAKEFOURCC('B','H','b','n')));

    snprintf(target_number, sizeof(target_number), "%u", (unsigned)ally->s.number);
    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    G_ClientCommand(clent, 2, select_target);
    T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','H','b','n')));

    snprintf(target_number, sizeof(target_number), "%u", (unsigned)enemy->s.number);
    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    G_ClientCommand(clent, 2, select_target);
    T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','H','b','n')));

    snprintf(target_number, sizeof(target_number), "%u", (unsigned)caster->s.number);
    G_ClientCommand(clent, 2, siphon_button);
    T_NOT_NULL(client->menu.on_entity_selected);
    G_ClientCommand(clent, 2, select_target);
    T_ASSERT(client->menu.on_entity_selected); /* Siphon Mana cannot target self. */
    T_ASSERT(!G_GetChannel(caster) || G_GetChannel(caster)->code != MAKEFOURCC('A','H','d','r'));

    snprintf(target_number, sizeof(target_number), "%u", (unsigned)ally->s.number);
    G_ClientCommand(clent, 2, select_target);
    T_NULL(client->menu.on_entity_selected);
    T_NOT_NULL(G_GetChannel(caster));
    if (!G_GetChannel(caster)) goto cleanup_banish_targets;
    T_EQ(G_GetChannel(caster)->code, MAKEFOURCC('A','H','d','r'));
    S_SpellCancelChannel(caster);

    snprintf(target_number, sizeof(target_number), "%u", (unsigned)enemy->s.number);
    G_ClientCommand(clent, 2, siphon_button);
    T_NOT_NULL(client->menu.on_entity_selected);
    G_ClientCommand(clent, 2, select_target);
    T_NOT_NULL(G_GetChannel(caster));
    if (!G_GetChannel(caster)) goto cleanup_banish_targets;
    T_EQ(G_GetChannel(caster)->code, MAKEFOURCC('A','H','d','r'));

cleanup_banish_targets:
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, banish_matches_ethereal_combat_healing_and_movement_rules) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X14;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AHbn\"\nC;Y2;X2;K\"AHbn\"\nC;Y2;X3;K\"air,ground,organic\"\n"
        "C;Y2;X4;K\"12\"\nC;Y2;X5;K\"4\"\nC;Y2;X14;K\"BHbn\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('H','b','l','m'), 500, 200, 0, 0);
    edict_t *banished = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    edict_t *physical = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 75, 0);
    edict_t *magic = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 125, 0);
    float normal_distance;

    caster->s.player = banished->s.player = 0;
    physical->s.player = magic->s.player = enemy->s.player = 1;
    physical->unit_state->attack1.type = ATK_NORMAL;
    physical->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
    magic->unit_state->attack1.type = ATK_MAGIC;
    magic->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
    edict_t *units[] = { banished, physical, magic, enemy };
    FOR_LOOP(i, 4) {
        units[i]->svflags |= SVF_MONSTER;
        units[i]->targtype = TARG_GROUND;
        units[i]->health.value = 500;
        units[i]->health.max_value = 1000;
    }

    /* Apply the actual Banish callback to the allied target. */
    test_execute_code(caster, "AHbn", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = banished));
    T_ASSERT(S_UnitHasStatus(banished, MAKEFOURCC('B','H','b','n')));
    T_ASSERT(unit_hasstatusstate(banished, WC3_STATUS_STATE_ETHEREAL));

    /* Physical weapons cannot hit an ethereal unit; magic weapons can. */
    T_ASSERT(!S_AttackCanTarget(physical, banished));
    T_ASSERT(S_AttackCanTarget(magic, banished));
    S_ResolveAttackHit(physical, banished, 100);
    T_FEQ(banished->health.value, 500, 0.001f);
    int magic_base_damage = G_AttackDamageWithType(magic, enemy, 100, ATK_MAGIC);
    int magic_ethereal_damage = G_AttackDamageWithType(magic, banished, 100, ATK_MAGIC);
    T_EQ(magic_ethereal_damage, (int)((float)magic_base_damage * 1.66f));
    T_ASSERT(S_SpellDamage(banished, enemy, 100));
    T_FEQ(banished->health.value, 334, 0.001f);

    /* A banished unit cannot attack, but can still deal spell damage. */
    T_ASSERT(!S_AttackCanTarget(banished, enemy));
    T_ASSERT(S_SpellDamage(enemy, banished, 25));
    T_FEQ(enemy->health.value, 475, 0.001f);

    /* Spell healing on an ethereal unit gets the same 66% amplification. */
    banished->health.value = 100;
    S_SpellHeal(banished, 100);
    T_FEQ(banished->health.value, 266, 0.001f);

    /* Movement step is halved for the duration of Banish. */
    unit_removestatus(banished, unit_findstatus(banished, MAKEFOURCC('B','H','b','n')), STATUS_REMOVE_SCRIPT);
    normal_distance = unit_movedistance(banished);
    test_execute_code(caster, "AHbn", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = banished));
    T_FEQ(unit_movedistance(banished), normal_distance * 0.5f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, banish_uses_classic_rank_costs_and_unit_durations) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X17\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\nC;Y1;X4;K\"levels\"\n"
        "C;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cost2\"\nC;Y1;X7;K\"Cost3\"\n"
        "C;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"Dur2\"\nC;Y1;X10;K\"Dur3\"\n"
        "C;Y1;X11;K\"HeroDur1\"\nC;Y1;X12;K\"HeroDur2\"\nC;Y1;X13;K\"HeroDur3\"\n"
        "C;Y1;X17;K\"BuffID1\"\nC;Y1;X18;K\"BuffID2\"\nC;Y1;X19;K\"BuffID3\"\n"
        "C;Y2;X1;K\"AHbn\"\nC;Y2;X2;K\"AHbn\"\nC;Y2;X3;K\"air,ground,organic\"\nC;Y2;X4;K\"3\"\n"
        "C;Y2;X5;K\"75\"\nC;Y2;X6;K\"60\"\nC;Y2;X7;K\"50\"\n"
        "C;Y2;X8;K\"12\"\nC;Y2;X9;K\"15\"\nC;Y2;X10;K\"18\"\n"
        "C;Y2;X11;K\"4\"\nC;Y2;X12;K\"5\"\nC;Y2;X13;K\"6\"\n"
        "C;Y2;X17;K\"BHbn\"\nC;Y2;X18;K\"BHbn\"\nC;Y2;X19;K\"BHbn\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHbn" };
    uint32_t const banish = MAKEFOURCC('A','H','b','n');
    float const costs[] = { 75, 60, 50 };
    float const durations[] = { 12, 15, 18 };
    float const hero_durations[] = { 4, 5, 6 };
    float expected_mana = 300.0f;
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('H','b','l','m'), 500, 300, 0, 0);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;

    FOR_LOOP(i, 3) {
        edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50 + i * 25, 0);
        caster->unit_state->ranks[0] = MAKE(heroability_t, .code = banish, .level = i + 1);
        target->s.player = 1;
        target->svflags |= SVF_MONSTER;
        target->targtype = TARG_GROUND;
        target->health.value = target->health.max_value = 500;
        T_ASSERT(S_CastUnitTargetSpell(caster, banish, target));
        expected_mana -= costs[i];
        T_FEQ(caster->mana.value, expected_mana, 0.001f);
        heroabilitystatus_t *status = unit_findstatus(target, MAKEFOURCC('B','H','b','n'));
        T_NOT_NULL(status);
        if (!status) break;
        T_EQ(status->timestamp, (uint32_t)(level.time + durations[i] * 1000.0f));
        S_SpellEndCooldown(caster, banish);
    }

    {
        edict_t *hero = make_hero(MAKEFOURCC('H','p','a','l'), 200, 0, 100, 0);
        caster->unit_state->ranks[0] = MAKE(heroability_t, .code = banish, .level = 1);
        hero->s.player = 1;
        test_execute_code(caster, "AHbn", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = hero));
        heroabilitystatus_t *status = unit_findstatus(hero, MAKEFOURCC('B','H','b','n'));
        T_NOT_NULL(status);
        T_EQ(status->timestamp, (uint32_t)(level.time + hero_durations[0] * 1000.0f));
    }

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* A command-card cast bypasses G_IssueUnitTargetOrder; it must still retire a Stop guard point. */
TEST(wc3_spell, command_card_cast_clears_stop_guard) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0), *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    char number[16];
    cstring_t button[] = { "button", "AHhb" }, select[] = { "select", number };

    old = G_SetSLKRows("AbilityData", rows);
    clent->client = client;
    caster->data.UnitAbilities = &abilities;
    caster->s.player = target->s.player = client->ps.number;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = 100; target->health.max_value = 500;
    G_SelectEntity(client, caster);
    order_stop(caster);
    T_ASSERT(caster->orders->guard_state == GUARD_IDLE);

    G_ClientCommand(clent, 2, button);
    snprintf(number, sizeof(number), "%u", (unsigned)target->s.number);
    G_ClientCommand(clent, 2, select);

    T_FEQ(target->health.value, 300, 0.001f);
    T_ASSERT(caster->orders->guard_state == GUARD_NONE);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, unavailable_spell_button_does_not_arm_targeting) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
        "C;Y2;X1;K\"AHhb\"\nC;Y2;X2;K\"AHhb\"\nC;Y2;X3;K\"air,ground,friend,self\"\n"
        "C;Y2;X4;K\"65\"\nC;Y2;X5;K\"5\"\nC;Y2;X6;K\"600\"\nC;Y2;X7;K\"200\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AHhb" };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    cstring_t button[] = { "button", "AHhb" };

    old = G_SetSLKRows("AbilityData", rows);
    clent->client = client;
    caster->data.UnitAbilities = &abilities;
    caster->s.player = client->ps.number;
    G_SelectEntity(client, caster);
    G_SetPlayerAbilityAvailable(client, MAKEFOURCC('A','H','h','b'), false);

    G_ClientCommand(clent, 2, button);
    T_NULL(client->menu.on_entity_selected);
    T_NULL(client->menu.on_location_selected);

    G_SetPlayerAbilityAvailable(client, MAKEFOURCC('A','H','h','b'), true);
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
    edict_t *caster = make_hero(MAKEFOURCC('H','p','a','l'), 500, 200, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = &game.clients[0];
    char caster_number[16], target_number[16];
    cstring_t button[] = { "button", "AHhb" };
    cstring_t focus_caster[] = { "focus", caster_number };
    cstring_t focus_target[] = { "focus", target_number };

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
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 300, 0, 0);
    edict_t *clent = &g_edicts[0];
    gameClient_t *client;
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
    edict_t *thinker;
    uint32_t thinker_slot;
    char target_number[16];
    cstring_t button[] = { "button", "AOcl" };
    cstring_t select_target[] = { "select", target_number };

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
    ((mapInfo_t *)level.mapinfo)->players[caster->s.player].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[target->s.player].playerType = kPlayerTypeHuman;
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

TEST(wc3_spell, unit_target_approach_replaces_same_target_cast) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\n"
        "C;Y3;X1;K\"AHtb\"\nC;Y3;X2;K\"AHtb\"\n"
        "C;Y3;X3;K\"air,ground,enemy,neutral\"\nC;Y3;X4;K\"75\"\n"
        "C;Y3;X5;K\"9\"\nC;Y3;X6;K\"100\"\nC;Y3;X7;K\"100\"\n"
        "C;Y3;X8;K\"1\"\nC;Y3;X9;K\"55\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *target, *first, *latest;
    uint32_t first_slot, latest_slot;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    caster->s.player = 0; target->s.player = 1;
    caster->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
    caster->targtype = target->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 300;
    target->health.value = target->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.events.read = level.events.write = 0;
    memset(level.events.queue, 0, sizeof(level.events.queue));
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);
    caster->unit_state->ranks[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','t','b'), .level = 1);

    first_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','O','c','l'), target));
    first = &globals.edicts[first_slot];
    T_ASSERT(first->inuse && first->think == S_SpellTargetApproachThink);

    latest_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','H','t','b'), target));
    latest = &globals.edicts[latest_slot];
    T_ASSERT(!first->inuse);
    T_ASSERT(latest->inuse && latest->think == S_SpellTargetApproachThink);
    T_EQ(latest->class_id, MAKEFOURCC('A','H','t','b'));

    caster->s.origin2.x = caster->s.origin.x = 500;
    if (latest->think) latest->think(latest);
    T_EQ((uint32_t)level.events.queue[0].value, MAKEFOURCC('A','H','t','b'));
    T_ASSERT(!latest->inuse);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, unit_target_approach_rejects_reused_edict_slots) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *target, *replacement, *approach;
    uint32_t approach_slot, old_spawn_time;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    level.time = 100;
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    caster->spawn_time = 100; target->spawn_time = 100;
    caster->s.player = 0; target->s.player = 1;
    caster->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
    caster->targtype = target->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 300;
    target->health.value = target->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);

    approach_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','O','c','l'), target));
    approach = &globals.edicts[approach_slot];
    T_ASSERT(approach->inuse && approach->think == S_SpellTargetApproachThink);
    old_spawn_time = target->spawn_time;
    G_FreeEdict(target);
    level.time = 1101;
    replacement = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    T_ASSERT(replacement == target);
    replacement->spawn_time = G_Time();
    replacement->s.player = 1;
    replacement->svflags |= SVF_MONSTER;
    replacement->targtype = TARG_GROUND;
    replacement->health.value = replacement->health.max_value = 500;
    T_NE(replacement->spawn_time, old_spawn_time);
    caster->s.origin2.x = caster->s.origin.x = 500;
    if (approach->think) approach->think(approach);
    T_ASSERT(!approach->inuse);
    T_FEQ(replacement->health.value, 500.0f, 0.001f);
    T_FEQ(caster->mana.value, 300.0f, 0.001f);
    T_ASSERT(S_SpellCooldownReady(caster, MAKEFOURCC('A','O','c','l')));

    reset_entities(); setup_test_world();
    level.time = 100;
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    caster->spawn_time = 100; target->spawn_time = 100;
    caster->s.player = 0; target->s.player = 1;
    caster->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
    caster->targtype = target->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 300;
    target->health.value = target->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);

    approach_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','O','c','l'), target));
    approach = &globals.edicts[approach_slot];
    G_FreeEdict(caster);
    level.time = 1101;
    replacement = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 500, 0);
    T_ASSERT(replacement == caster);
    replacement->spawn_time = G_Time();
    replacement->s.player = 0;
    replacement->svflags |= SVF_MONSTER;
    replacement->targtype = TARG_GROUND;
    replacement->mana.value = replacement->mana.max_value = 300;
    replacement->stand = unit_stand;
    replacement->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);
    order_move(replacement, target);
    replacement->s.origin2.x = replacement->s.origin.x = 500;
    T_ASSERT(move_is_active_order_walk(replacement));
    if (approach->think) approach->think(approach);
    T_ASSERT(!approach->inuse);
    T_FEQ(target->health.value, 500.0f, 0.001f);
    T_FEQ(replacement->mana.value, 300.0f, 0.001f);
    T_ASSERT(S_SpellCooldownReady(replacement, MAKEFOURCC('A','O','c','l')));
    T_ASSERT(replacement->goalentity == target);
    T_ASSERT(move_is_active_order_walk(replacement));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, committed_cast_cancels_unit_target_approach_and_move) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\n"
        "C;Y2;X3;K\"air,ground,enemy,neutral\"\nC;Y2;X4;K\"75\"\n"
        "C;Y2;X5;K\"9\"\nC;Y2;X6;K\"100\"\nC;Y2;X7;K\"100\"\n"
        "C;Y2;X8;K\"1\"\nC;Y2;X9;K\"0\"\n"
        "C;Y3;X1;K\"AHtb\"\nC;Y3;X2;K\"AHtb\"\n"
        "C;Y3;X3;K\"air,ground,enemy,neutral\"\nC;Y3;X4;K\"75\"\n"
        "C;Y3;X5;K\"9\"\nC;Y3;X6;K\"100\"\nC;Y3;X7;K\"100\"\n"
        "C;Y3;X8;K\"1\"\nC;Y3;X9;K\"55\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *far_target, *near_target, *approach;
    uint32_t approach_slot;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    far_target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 600, 0);
    near_target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    caster->s.player = 0; far_target->s.player = near_target->s.player = 1;
    caster->svflags |= SVF_MONSTER;
    far_target->svflags |= SVF_MONSTER; near_target->svflags |= SVF_MONSTER;
    caster->targtype = far_target->targtype = near_target->targtype = TARG_GROUND;
    caster->mana.value = caster->mana.max_value = 300;
    far_target->health.value = far_target->health.max_value = 500;
    near_target->health.value = near_target->health.max_value = 500;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','O','c','l'), .level = 1);
    caster->unit_state->ranks[1] = MAKE(heroability_t, .code = MAKEFOURCC('A','H','t','b'), .level = 1);

    approach_slot = globals.num_edicts;
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','O','c','l'), far_target));
    approach = &globals.edicts[approach_slot];
    T_ASSERT(approach->inuse && move_is_active_order_walk(caster));
    T_ASSERT(S_IssueUnitTargetSpell(caster, MAKEFOURCC('A','H','t','b'), near_target));
    T_ASSERT(!approach->inuse);
    T_ASSERT(!move_is_active_order_walk(caster));
    T_NULL(caster->goalentity);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
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
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 300, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500.0f;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
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
    T_EQ((uint32_t)level.events.queue[0].value, MAKEFOURCC('A','O','c','l'));
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
    edict_t *caster = make_hero(MAKEFOURCC('E','w','d','n'), 500, 300, 0, 0);
    vec2_t point = { 64.0f, 32.0f };

    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0;
    caster->collision = 16.0f;
    level.events.read = level.events.write = 0;
    memset(level.events.queue, 0, sizeof(level.events.queue));

    T_ASSERT(unit_issueorder(caster, "blink", &point));
    T_FEQ(caster->s.origin2.x, point.x, 0.001f);
    T_FEQ(caster->s.origin2.y, point.y, 0.001f);
    T_FEQ(caster->s.origin.x, point.x, 0.001f);
    T_FEQ(caster->s.origin.y, point.y, 0.001f);
    T_EQ(G_GetIssuedOrderId(caster), 852525);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_SPELL_EFFECT);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_SPELL_EFFECT);
    T_ASSERT(level.events.queue[0].has_point);
    T_FEQ(level.events.queue[0].point.x, point.x, 0.001f);
    T_FEQ(level.events.queue[0].point.y, point.y, 0.001f);
    T_EQ((uint32_t)level.events.queue[0].value, MAKEFOURCC('A','E','b','l'));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, escape_cancel_command_clears_spell_cursor_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    slkTestData_t *rows, *old;
    edict_t *clent = spell_cursor_test_begin(&rows, &old);

    T_ASSERT(S_SpellPointTargetMode(clent));
    T_ASSERT(spell_cursor_capture_radius > 0.0f);
    spell_cursor_clear_seen = false;
    gi.Write = spell_cursor_capture_write;
    CMD_CancelCommand(clent);
    gi.Write = old_write;

    T_ASSERT(spell_cursor_clear_seen);
    T_NULL(clent->client->menu.on_location_selected);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, command_card_refresh_clears_spell_cursor_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    slkTestData_t *rows, *old;
    edict_t *clent = spell_cursor_test_begin(&rows, &old);

    T_ASSERT(S_SpellPointTargetMode(clent));
    T_ASSERT(spell_cursor_capture_radius > 0.0f);
    spell_cursor_clear_seen = false;
    gi.Write = spell_cursor_capture_write;
    Get_Commands_f(clent);
    gi.Write = old_write;

    T_ASSERT(spell_cursor_clear_seen);
    T_NULL(clent->client->menu.on_location_selected);
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
    edict_t *caster = make_hero(MAKEFOURCC('h','p','e','a'), 500, 500, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    spellTarget_t st = { .type = SPELL_TARGET_UNIT, .entity = target };

    T_NOT_NULL(rows); T_NOT_NULL(ability); T_NOT_NULL(ability ? ability->proc : NULL);
    old = G_SetSLKRows("AbilityData", rows);
    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    game.clients[0].ps.number = 0;
    target->data.UnitData = &ground; target->data.UnitBalance = &creep;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE; target->svflags |= SVF_MONSTER;
    memset(level.alliances, 0, sizeof(level.alliances));

    G_SetPlayerAbilityAvailable(&game.clients[0], FS_SLKKey("Aply"), false);
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    G_SetPlayerAbilityAvailable(&game.clients[0], FS_SLKKey("Aply"), true);
    T_ASSERT(test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    target->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','C','s','k'), .level = 1);
    T_ASSERT(S_UnitIsResistant(target));
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    memset(target->unit_state->ranks, 0, sizeof(target->unit_state->ranks));
    T_ASSERT(test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    creep.level = 6; T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    creep.level = 5; target->summon_ability = MAKEFOURCC('A','O','s','f');
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));
    target->summon_ability = 0; target->aiflags |= AI_ILLUSION;
    T_ASSERT(!test_ability_message(caster, A_VALIDATE, &ability_item, &st));

    target->aiflags &= ~AI_ILLUSION;
    polymorph_t *morph = G_AddPolymorph(target);
    *morph = MAKE(polymorph_t, .component = morph->component,
        .ability = MAKEFOURCC('A','p','l','y'), .buff = MAKEFOURCC('B','p','l','y'),
        .form_type = MAKEFOURCC('o','p','e','o'), .original_model = 17,
        .original_scale = 1.25f, .original_move_speed = 234.0f);
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

/* Resistant Skin gives a non-Hero the Hero targeting class and Hero duration
 * for hostile control effects. */
TEST(wc3_spell, resistant_skin_uses_hero_targeting_and_ensnare_duration) {
    static cstring_t const slk =
        "ID;PWXL;N;EBB;Y3;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"Cost1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"Rng1\"\n"
        "C;Y2;X1;K\"Aens\"\nC;Y2;X2;K\"Aens\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"ground,air,enemy,neutral\"\nC;Y2;X5;K\"9\"\n"
        "C;Y2;X6;K\"3\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"Bens\"\nC;Y2;X9;K\"500\"\n"
        "C;Y3;X1;K\"Adev\"\nC;Y3;X2;K\"Adev\"\nC;Y3;X3;K\"1\"\n"
        "C;Y3;X4;K\"ground,enemy,nonhero\"\nC;Y3;X5;K\"9\"\n"
        "C;Y3;X6;K\"3\"\nC;Y3;X7;K\"0\"\nC;Y3;X8;K\"\"\nC;Y3;X9;K\"500\"\n"
        "C;Y4;X1;K\"ANdo\"\nC;Y4;X2;K\"ANdo\"\nC;Y4;X3;K\"1\"\n"
        "C;Y4;X4;K\"ground,enemy\"\nC;Y4;X5;K\"9\"\nC;Y4;X6;K\"3\"\n"
        "C;Y4;X7;K\"0\"\nC;Y4;X8;K\"BDom\"\nC;Y4;X9;K\"500\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster, *ordinary, *guard;

    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('o','r','a','i'), 0, 0);
    ordinary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    guard = alloc_test_unit(MAKEFOURCC('n','n','r','g'), 96, 0);
    caster->s.player = 0; ordinary->s.player = guard->s.player = 1;
    caster->svflags |= SVF_MONSTER; ordinary->svflags |= SVF_MONSTER; guard->svflags |= SVF_MONSTER;
    caster->targtype = ordinary->targtype = guard->targtype = TARG_GROUND;
    caster->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','e','n','s'), .level = 1);
    caster->unit_state->ranks[1] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','d','e','v'), .level = 1);
    caster->unit_state->ranks[2] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','N','d','o'), .level = 1);
    guard->unit_state->ranks[0] = MAKE(heroability_t,
        .code = MAKEFOURCC('A','C','s','k'), .level = 1);
    caster->mana.value = caster->mana.max_value = 100;
    ordinary->health.value = guard->health.value = 500;

    T_ASSERT(!G_UnitIsHero(guard));
    T_ASSERT(S_UnitIsResistant(guard));
    T_ASSERT(!S_UnitIsResistant(ordinary));
    T_ASSERT(S_SpellAllowsTarget(MAKEFOURCC('A','d','e','v'), caster, ordinary));
    T_ASSERT(!S_SpellAllowsTarget(MAKEFOURCC('A','d','e','v'), caster, guard));
    T_ASSERT(!S_CastUnitTargetSpell(caster, MAKEFOURCC('A','N','d','o'), guard));
    T_FEQ(S_SpellResistantDuration(MAKEFOURCC('A','e','n','s'), 1, ordinary), 9.0f, 0.001f);
    T_FEQ(S_SpellResistantDuration(MAKEFOURCC('A','e','n','s'), 1, guard), 3.0f, 0.001f);
    T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','e','n','s'), guard));
    heroabilitystatus_t *status = unit_findstatus(guard, MAKEFOURCC('B','e','n','s'));
    T_NOT_NULL(status);
    if (status) T_EQ(status->duration_ms, 3000u);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

/* Roar is one area cast, not an all-or-nothing status transaction.
 * A capacity-full ally must not prevent a second ally receiving the buff. */
TEST(wc3_spell, roar_skips_full_ally_and_buffs_other_targets) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\n"
        "C;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"Area1\"\n"
        "C;Y2;X1;K\"Aroa\"\nC;Y2;X2;K\"Aroa\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"30\"\nC;Y2;X5;K\"0.37\"\n"
        "C;Y2;X6;K\"Broa\"\nC;Y2;X7;K\"300\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
    edict_t *full = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 60, 0);
    edict_t *free_ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
    edict_t *roar_effect = NULL, *caster_effect = NULL;
    caster->s.player = full->s.player = free_ally->s.player = 0;
    full->svflags |= SVF_MONSTER;
    free_ally->svflags |= SVF_MONSTER;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        char buff[5] = { 'B', 'q', '0', '0', 0 };
        buff[2] = (char)('0' + i / 10);
        buff[3] = (char)('0' + i % 10);
        T_ASSERT(unit_addtimedstatus(full, buff, 1, 30.0f));
    }
    test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_EQ(G_UnitStatusLevel(full, MAKEFOURCC('B','r','o','a')), 0);
    T_EQ(G_UnitStatusLevel(free_ally, MAKEFOURCC('B','r','o','a')), 1);
    T_FEQ(S_RoarDamageBonus(free_ally), 0.37f, 0.001f);
    T_EQ(G_UnitStatusLevel(caster, MAKEFOURCC('B','r','o','a')), 1);
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','r','o','a'), WC3_EFFECT_TARGET, 0), "TestUI\\Models\\anim_pulse.mdx");
    FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','r','o','a') && effect->goalentity == free_ally) {
        roar_effect = effect;
        break;
    }
    FILTER_EDICTS(effect, effect->goalentity == caster && effect->s.model == G_RegisterModel("TestUI\\Models\\anim_oneshot.mdx")) {
        caster_effect = effect;
        break;
    }
    T_NOT_NULL(roar_effect);
    T_NOT_NULL(caster_effect);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, roar_increases_attack_damage_by_authored_bonus) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"Area1\"\n"
        "C;Y2;X1;K\"Aroa\"\nC;Y2;X2;K\"Aroa\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"30\"\nC;Y2;X5;K\"0.25\"\nC;Y2;X6;K\"Broa\"\nC;Y2;X7;K\"300\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster, *enemy;

    reset_entities();
    setup_test_world();
    caster = make_hero(MAKEFOURCC('E','d','e','m'), 500, 200, 0, 0);
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
    caster->s.player = 0;
    enemy->s.player = 1;
    caster->unit_state->attack1.type = ATK_CHAOS;
    enemy->health.value = enemy->health.max_value = 1000.0f;

    S_ResolveAttackHit(caster, enemy, 100);
    T_FEQ(enemy->health.value, 900.0f, 0.001f);
    test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_FEQ(S_RoarDamageBonus(caster), 0.25f, 0.001f);
    S_ResolveAttackHit(caster, enemy, 100);
    T_FEQ(enemy->health.value, 775.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, roar_damage_bonus_is_shown_as_green_flat_damage_in_status_panel) {
    char const slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aroa\"\nC;Y2;X2;K\"Aroa\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"0.25\"\nC;Y2;X5;K\"Broa\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit;
    char display[128];

    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    unit->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    unit->unit_state->attack1.damageBase = 24;
    unit->unit_state->attack1.numberOfDice = 1;
    unit->unit_state->attack1.sidesPerDie = 1;
    T_ASSERT(unit_addtimedstatus(unit, "Broa", 1, 30.0f));
    UI_TestFormatAttackDamageWithStatus(display, sizeof(display), unit, 25, 25, 0.0f, 24.0f);
    T_STREQ(display, "25 - 25 |cff00ff00+6|r");

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, metamorphosis_alias_applies_timer_and_reverts_bonus_health) {
    char const ability_data[] =
        "ID;PWXL;N;EBB;Y6;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"UnitID1\"\nC;Y1;X5;K\"DataE1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"BuffID1\"\n"
        "C;Y2;X1;K\"AEme\"\nC;Y2;X2;K\"AEme\"\nC;Y2;X3;K1\nC;Y2;X4;K\"Edmm\"\nC;Y2;X5;K500\nC;Y2;X6;K30\nC;Y2;X7;K\"BEme\"\n"
        "C;Y3;X1;K\"AEmb\"\nC;Y3;X2;K\"AEmb\"\nC;Y3;X3;K1\n"
        "C;Y4;X1;K\"AEim\"\nC;Y4;X2;K\"AEim\"\nC;Y4;X3;K1\n"
        "C;Y5;X1;K\"AEev\"\nC;Y5;X2;K\"AEev\"\nC;Y5;X3;K1\n"
        "C;Y6;X1;K\"AEvi\"\nC;Y6;X2;K\"AEme\"\nC;Y6;X3;K1\nC;Y6;X4;K\"Edmm\"\nC;Y6;X5;K500\nC;Y6;X6;K30\nC;Y6;X7;K\"BEme\"\nE\n";
    char const unit_abilities[] =
        "ID;PWXL;N;EBB;Y3;X2\nC;Y1;X1;K\"unitAbilID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K\"AEvi\"\nC;Y3;X1;K\"Edmm\"\nC;Y3;X2;K\"AEvi\"\nE\n";
    char const unit_ui[] =
        "ID;PWXL;N;EBB;Y3;X3\nC;Y1;X1;K\"unitUIID\"\nC;Y1;X2;K\"file\"\nC;Y1;X3;K\"modelScale\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K\"Units\\NightElf\\DemonHunter\\DemonHunter.mdx\"\nC;Y2;X3;K1\n"
        "C;Y3;X1;K\"Edmm\"\nC;Y3;X2;K\"Units\\NightElf\\DemonHunter\\DemonHunter.mdx\"\nC;Y3;X3;K1\nE\n";
    char const unit_balance[] =
        "ID;PWXL;N;EBB;Y3;X5\nC;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"realHP\"\nC;Y1;X3;K\"spd\"\n"
        "C;Y1;X4;K\"STR\"\nC;Y1;X5;K\"STRplus\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K575\nC;Y2;X3;K300\nC;Y2;X4;K19\nC;Y2;X5;K2.4\n"
        "C;Y3;X1;K\"Edmm\"\nC;Y3;X2;K750\nC;Y3;X3;K300\nC;Y3;X4;K26\nC;Y3;X5;K2.4\nE\n";
    char const unit_data[] =
        "ID;PWXL;N;EBB;Y3;X3\nC;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"targType\"\nC;Y1;X3;K\"movetp\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K\"ground\"\nC;Y2;X3;K\"foot\"\n"
        "C;Y3;X1;K\"Edmm\"\nC;Y3;X2;K\"ground\"\nC;Y3;X3;K\"foot\"\nE\n";
    char const unit_weapons[] =
        "ID;PWXL;N;EBB;Y3;X6\nC;Y1;X1;K\"unitWeaponsID\"\nC;Y1;X2;K\"weapsOn\"\n"
        "C;Y1;X3;K\"atkType1\"\nC;Y1;X4;K\"weapTp1\"\nC;Y1;X5;K\"rangeN1\"\nC;Y1;X6;K\"dice1\"\n"
        "C;Y2;X1;K\"Edem\"\nC;Y2;X2;K1\nC;Y2;X3;K\"hero\"\nC;Y2;X4;K\"mwc\"\nC;Y2;X5;K100\nC;Y2;X6;K1\n"
        "C;Y3;X1;K\"Edmm\"\nC;Y3;X2;K1\nC;Y3;X3;K\"chaos\"\nC;Y3;X4;K\"msplash\"\nC;Y3;X5;K600\nC;Y3;X6;K1\nE\n";
    slkTestData_t *ability_rows = parse_slk_string(ability_data), *old_ability = G_SetSLKRows("AbilityData", ability_rows);
    slkTestData_t *abilities_rows = parse_slk_string(unit_abilities), *old_abilities = G_SetSLKRows("UnitAbilities", abilities_rows);
    slkTestData_t *ui_rows = parse_slk_string(unit_ui), *old_ui = G_SetSLKRows("UnitUI", ui_rows);
    slkTestData_t *balance_rows = parse_slk_string(unit_balance), *old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    slkTestData_t *data_rows = parse_slk_string(unit_data), *old_data = G_SetSLKRows("UnitData", data_rows);
    slkTestData_t *weapons_rows = parse_slk_string(unit_weapons), *old_weapons = G_SetSLKRows("UnitWeapons", weapons_rows);
    edict_t *demon, *morph = NULL;

    reset_entities(); setup_test_world();
    demon = alloc_test_unit(MAKEFOURCC('E','d','e','m'), 64.0f, 64.0f);
    demon->s.player = 0;
    demon->svflags |= SVF_MONSTER;
    G_HeroApplyLevel(demon, 10);
    demon->health.max_value = demon->health.value = 1100.0f;
    T_EQ(demon->unit_state->hero.level, 10u);
    demon->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','E','v','i'), .level = 1);
    abilityitem_t const spell = S_AbilityItem(MAKEFOURCC('A','E','v','i'));
    spellTarget_t const target = MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    abilityCall_t const call = MAKE(abilityCall_t, .item = &spell, .target = &target);
    T_ASSERT(S_AbilityMessage(demon, A_EXECUTE, &call));
    T_EQ(demon->class_id, MAKEFOURCC('E','d','m','m'));
    /* At level 10 Edem has 1100 HP. Edmm's higher base STR and HP retain
     * that level progression; Metamorphosis DataE then adds 500 more. */
    T_EQ(demon->unit_state->hero.level, 10u);
    T_EQ(demon->unit_state->hero.str, 47u);
    T_FEQ(demon->health.max_value, 1775.0f, 0.001f);
    T_EQ(demon->unit_state->attack1.projectile.model,
         G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
    T_FEQ(demon->unit_state->attack1.range, 600.0f, 0.001f);
    T_EQ(demon->unit_state->attack1.weapon, WPN_MSPLASH);
    T_ASSERT(unit_findstatus(demon, MAKEFOURCC('B','E','m','e')) != NULL);
    T_ASSERT(unit_statusshowstimedbar(MAKEFOURCC('B','E','m','e')));
    T_FEQ(unit_statusremainingfraction(unit_findstatus(demon, MAKEFOURCC('B','E','m','e'))), 1.0f, 0.001f);
    T_ASSERT(G_ActorHasSkill(demon, "AEmb"));
    T_ASSERT(G_ActorHasSkill(demon, "AEim"));
    T_ASSERT(G_ActorHasSkill(demon, "AEev"));
    {
        edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500.0f, 64.0f);
        edict_t *missile = NULL;
        demon->data.UnitProfile = (UnitProfile_t *)G_UnitProfile(demon->class_id);
        demon->goalentity = enemy;
        enemy->targtype = TARG_GROUND;
        enemy->unit_state->armor_value = 0.0f;
        demon->s.player = 0;
        enemy->s.player = 1;
        demon->svflags |= SVF_MONSTER;
        demon->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
        enemy->targtype = TARG_GROUND;
        enemy->health.value = enemy->health.max_value = 1000.0f;
        demon->goalentity = enemy;
        demon->wait = 0.0f;
        T_ASSERT(S_AttackCanTarget(demon, enemy));
        attack_ranged(demon);
        demon->wait = 0.01f;
        demon->currentmove->think(demon);
        FILTER_EDICTS(candidate, candidate->owner == demon && candidate->movetype == MOVETYPE_FLYMISSILE) {
            missile = candidate;
            break;
        }
        T_NOT_NULL(missile);
        if (missile) {
            T_EQ(missile->s.model, G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
            T_NOT_NULL(missile->animation);
            T_STREQ(missile->animation->name, "Birth");
        }
    }
    FOR_LOOP(i, globals.num_edicts) {
        edict_t *candidate = g_edicts + i;
        if (candidate->inuse && candidate->think == morph_end && candidate->owner == demon) morph = candidate;
    }
    T_NOT_NULL(morph);
    if (morph) {
        level.time = morph->spawn_time;
        morph_end(morph);
    }
    T_EQ(demon->class_id, MAKEFOURCC('E','d','e','m'));
    T_EQ(demon->unit_state->hero.level, 10u);
    T_EQ(demon->unit_state->hero.str, 40u);
    T_FEQ(demon->health.max_value, 1100.0f, 0.001f);
    T_NULL(unit_findstatus(demon, MAKEFOURCC('B','E','m','e')));

    G_SetSLKRows("UnitWeapons", old_weapons); free_slk_rows(weapons_rows);
    G_SetSLKRows("UnitData", old_data); free_slk_rows(data_rows);
    G_SetSLKRows("UnitBalance", old_balance); free_slk_rows(balance_rows);
    G_SetSLKRows("UnitUI", old_ui); free_slk_rows(ui_rows);
    G_SetSLKRows("UnitAbilities", old_abilities); free_slk_rows(abilities_rows);
    G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}

TEST(wc3_spell, bear_form_hides_normal_form_spells_and_uses_reverse_button_art) {
    static cstring_t const unit_ui_slk =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"unitUIID\"\nC;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"modelScale\"\nC;Y1;X4;K\"scale\"\n"
        "C;Y2;X1;K\"edoc\"\nC;Y2;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y2;X3;K1\nC;Y2;X4;K1\n"
        "C;Y3;X1;K\"edcm\"\nC;Y3;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y3;X3;K1\nC;Y3;X4;K1\nE\n";
    char const roar_data[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aroa\"\nC;Y2;X2;K\"Aroa\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K\"air,ground,friend,self\"\nC;Y2;X5;K300\nC;Y2;X6;K30\n"
        "C;Y2;X7;K0.25\nC;Y2;X8;K\"Broa\"\nE\n";
    char const roar_art[] =
        "[Aroa]\nArt=ReplaceableTextures\\CommandButtons\\BTNRoar.blp\n"
        "Buttonpos=0,2\nTip=Roar\n";
    slkTestData_t *ui_rows, *old_ui;
    slkTestData_t *roar_rows = parse_slk_string(roar_data), *old_roar = G_SetSLKRows("AbilityData", roar_rows);
    /* Stock Bear Form omits Roar; TFT exposes it through Mark of the Claw. */
    static UnitAbilities_t const bear_abilities = { .abilList = "Abrf,Ara2,Arej" };
    static UnitBalance_t const bear_balance = { .speed = 270.0f };
    edict_t *bear;
    gameCommandButton_t buttons[16];
    uint8_t count;
    bool bear_form_button = false;
    bool roar_button = false;
    bool const expansion = gi.CvarString && atoi(gi.CvarString("fs_expansion", "0")) != 0;
    gameClient_t *client;
    abilityitem_t bear_form = S_AbilityItem(FS_SLKKey("Abrf"));
    abilityitem_t rejuv = S_AbilityItem(FS_SLKKey("Arej"));
    abilityCall_t toggle_call = MAKE(abilityCall_t, .item = &bear_form);
    edict_t *ally;
    spellTarget_t ally_target;

    setup_test_world();
    T_ASSERT(G_TestPushAbilityConfig(roar_art));
    ui_rows = parse_slk_string(unit_ui_slk);
    old_ui = G_SetSLKRows("UnitUI", ui_rows);
    bear = alloc_test_unit(MAKEFOURCC('e','d','c','m'), 0, 0);
    bear->s.player = 0;
    bear->svflags |= SVF_MONSTER;
    bear->targtype = TARG_GROUND;
    bear->health.value = bear->health.max_value = 500;
    bear->mana.value = bear->mana.max_value = 500;
    bear->data.UnitAbilities = &bear_abilities;
    bear->data.UnitBalance = &bear_balance;
    client = &game.clients[0];
    client->ps.number = 0;
    memset(client->tech, 0, sizeof(client->tech));
    G_SetPlayerTechMaxAllowed(client, MAKEFOURCC('R','e','e','b'), 1);
    count = G_GetCommandButtons(bear, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "Abrf")) {
            bear_form_button = true;
            T_STREQ(buttons[i].art, "ReplaceableTextures\\CommandButtons\\BTNDruidOfTheClaw.blp");
        }
        T_ASSERT(strcmp(buttons[i].command, "Ara2") != 0);
        T_ASSERT(strcmp(buttons[i].command, "Arej") != 0);
        if (!strcmp(buttons[i].command, "Aroa")) {
            roar_button = true;
            T_EQ(buttons[i].disabled, expansion ? 1 : 0);
        }
    }
    T_ASSERT(bear_form_button);
    T_EQ(roar_button, expansion);
    T_ASSERT(!S_CastNoTargetSpell(bear, FS_SLKKey("Aroa")));
    T_EQ(G_UnitStatusLevel(bear, MAKEFOURCC('B','r','o','a')), 0);
    if (expansion) {
        uint32_t const roar_code = FS_SLKKey("Aroa");
        abilityitem_t roar_item = S_AbilityItem(roar_code);
        spellTarget_t roar_target = MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        abilityCall_t roar_call = MAKE(abilityCall_t, .item = &roar_item, .target = &roar_target);
        G_SetPlayerTechResearched(client, MAKEFOURCC('R','e','e','b'), 1);
        T_ASSERT(G_UnitAbilityResearchAvailable(bear, roar_code));
        T_EQ(G_UnitAbilityLevel(bear, roar_code), 1u);
        T_NOT_NULL(roar_item.ability);
        T_ASSERT(S_SpellIsAliveTarget(bear));
        T_ASSERT(!S_UnitPolymorphed(bear));
        T_ASSERT(!S_UnitIsCycloned(bear));
        T_ASSERT(!S_UnitIsSilenced(bear));
        T_ASSERT(S_SpellCooldownReady(bear, roar_code));
        T_ASSERT(S_SpellCanPay(bear, roar_code, 1));
        T_ASSERT(S_SpellCanActivate(bear, roar_code, 1, false));
        T_ASSERT(S_AbilityMessage(bear, A_VALIDATE, &roar_call));
        count = G_GetCommandButtons(bear, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
        roar_button = false;
        FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aroa")) {
            roar_button = true;
            T_ASSERT(!buttons[i].disabled);
        }
        T_ASSERT(roar_button);
        T_ASSERT(S_CastNoTargetSpell(bear, roar_code));
        T_EQ(G_UnitStatusLevel(bear, MAKEFOURCC('B','r','o','a')), 1);
    }
    T_ASSERT(S_AbilityMessage(bear, A_TOGGLE_ON, &toggle_call));

    ally = alloc_test_unit(MAKEFOURCC('e','d','o','c'), 64.0f, 0.0f);
    ally_target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally);
    T_ASSERT(!test_ability_message(bear, A_VALIDATE, &rejuv, &ally_target));

    /* The query drives the command button's Unart while the normal form is
     * active. Keep the fixture ability list attached to exercise HUD art. */
    bear->class_id = MAKEFOURCC('e','d','o','c');
    T_ASSERT(!S_AbilityMessage(bear, A_TOGGLE_ON, &toggle_call));
    bear_form_button = false;
    count = G_GetCommandButtons(bear, buttons, (uint8_t)(sizeof(buttons) / sizeof(buttons[0])));
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "Abrf")) {
            bear_form_button = true;
            T_STREQ(buttons[i].art, "ReplaceableTextures\\CommandButtons\\BTNBearForm.blp");
        }
    }
    T_ASSERT(bear_form_button);

    test_execute_code(bear, "Abrf", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_EQ(bear->class_id, MAKEFOURCC('e','d','c','m'));
    T_STREQ(bear->animation_request, "morph");
    test_execute_code(bear, "Abrf", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_EQ(bear->class_id, MAKEFOURCC('e','d','o','c'));
    T_STREQ(bear->animation_request, "morph alternate");
    G_SetSLKRows("UnitUI", old_ui);
    free_slk_rows(ui_rows);
    G_SetSLKRows("AbilityData", old_roar);
    free_slk_rows(roar_rows);
    G_TestPopAbilityConfig();
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
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	abilityitem_t blo = S_AbilityItem(FS_SLKKey("Ablo"));
	abilityitem_t fae = S_AbilityItem(FS_SLKKey("Afae"));
	abilityitem_t rej = S_AbilityItem(FS_SLKKey("Arej"));
	abilityitem_t roa = S_AbilityItem(FS_SLKKey("Aroa"));
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->unit_state->armor_value = 2; enemy->unit_state->armor_value = 6;
	ally->health.value = 60; ally->health.max_value = 100;
	ally->unitinfo.MoveSpeed = 270.0f;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	T_EQ(blo.ability->proc, CAbilityBloodlust); T_ASSERT(blo.ability->flags & AB_AUTOCAST);
	T_EQ(fae.ability->proc, CAbilityFaerieFire); T_ASSERT(fae.ability->flags & AB_AUTOCAST);
	T_EQ(rej.ability->proc, CAbilityRejuvination);
	T_EQ(roa.ability->proc, CAbilityRoar);
	float const normal_move_distance = unit_movedistance(ally);
	test_execute_code(caster, "Ablo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','b','l','o')));
	T_ASSERT(unit_status_can_steal(unit_findstatus(ally, MAKEFOURCC('B','b','l','o'))));
	T_FEQ(S_BloodlustAttackBonus(ally), 0.4f, 0.001f); T_FEQ(S_BloodlustMoveBonus(ally), 0.25f, 0.001f);
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0.4f, 0.001f);
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), 0.25f, 0.001f);
	T_FEQ(unit_movedistance(ally), normal_move_distance * 1.25f, 0.01f);
	{
		ally->unit_state->attack1.cooldown = 1.0f;
		ally->unit_state->attack1.damagePoint = 0.3f;
		attack_melee(ally);
		T_FEQ(unit_movedistance(ally), normal_move_distance * 1.25f, 0.01f);
		T_FEQ(ally->wait, 0.3f / 1.4f, 0.001f);
	}
	test_execute_code(caster, "Afae", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','f','a','e')));
	T_ASSERT(unit_status_can_steal(unit_findstatus(enemy, MAKEFOURCC('B','f','a','e'))));
	T_FEQ(G_UnitArmorValue(enemy), 2.0f, 0.001f);
	test_execute_code(caster, "Arej", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','e','j'))); T_FEQ(S_RejuvHealRate(ally), 400.0f / 12.0f, 0.01f);
	T_EQ(unit_findstatus(ally, MAKEFOURCC('B','r','e','j'))->buff_flags &
		(WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL),
		WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL);
	T_ASSERT(!unit_status_can_steal(unit_findstatus(ally, MAKEFOURCC('B','r','e','j'))));
	test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','o','a'))); T_FEQ(S_RoarDamageBonus(ally), 0.25f, 0.001f);
	T_EQ(unit_findstatus(ally, MAKEFOURCC('B','r','o','a'))->buff_flags &
		(WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL),
		WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Spell Steal transfers a positive enemy buff to a friendly unit in the
 * authored effect area and preserves the buff's remaining runtime state. */
TEST(wc3_spell, spell_steal_transfers_positive_enemy_buff_to_nearby_ally) {
	static UnitAbilities_t const abilities = { .abilList = "Asps" };
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
		"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Area1\"\n"
		"C;Y2;X1;K\"Asps\"\nC;Y2;X2;K\"Asps\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,friend,enemy,neutral,self\"\n"
		"C;Y2;X5;K\"75\"\nC;Y2;X6;K\"3\"\nC;Y2;X7;K\"70\"\nC;Y2;X8;K\"32\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *enemy, *ally;
	heroabilitystatus_t *source, *stolen;
	status_application_t app = {
		.buff = "Bblo", .level = 2, .duration = 20.0f,
		.source_ability = MAKEFOURCC('A','b','l','o'), .data = MAKEFOURCC('A','b','l','o'),
		.rank = 2,
		.buff_flags = WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL |
		              WC3_STATUS_BUFF_TRANSFERABLE
	};
	wc3_status_modifier_t modifier = {
		.type = WC3_STATUS_MOD_ATTACK_SPEED_PERCENT,
		.policy = WC3_STATUS_MOD_ADD, .value = 0.4f
	};
	uint32_t const code = MAKEFOURCC('A','s','p','s');
	uint32_t source_expiry = 0;

	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 80, 0);
	enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
	caster = alloc_test_unit(MAKEFOURCC('h','p','r','i'), 0, 0);
	caster->data.UnitAbilities = &abilities;
	caster->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER; ally->svflags |= SVF_MONSTER;
	caster->targtype = enemy->targtype = ally->targtype = TARG_GROUND;
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	caster->mana.value = caster->mana.max_value = 300;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
	source = unit_applystatus(enemy, &app);
	T_NOT_NULL(source);
	if (source) {
		T_ASSERT(unit_status_setmodifier(source, 0, &modifier));
		source_expiry = source->timestamp;
	}

	T_FEQ(S_SpellRange(code, 1), 70.0f, 0.001f);
	T_FEQ(S_SpellNumber(code, ABILITY_NUMBER_COST, 1), 75.0f, 0.001f);
	T_FEQ(S_SpellNumber(code, ABILITY_NUMBER_COOLDOWN, 1), 3.0f, 0.001f);
	enemy->s.origin.x = enemy->s.origin2.x = 71.0f;
	T_ASSERT(!S_CastUnitTargetSpell(caster, code, enemy)); /* outside authored Range */
	T_FEQ(caster->mana.value, 300.0f, 0.001f);
	enemy->s.origin.x = enemy->s.origin2.x = 64.0f;
	T_ASSERT(S_SpellAllowsTarget(code, caster, enemy));
	T_ASSERT(test_ability_message(caster, A_VALIDATE, &(abilityitem_t){ .code = code, .ability = S_SpellAbilityForCode(code) }, &(spellTarget_t){ .type = SPELL_TARGET_UNIT, .entity = enemy }));
	T_ASSERT(S_CastUnitTargetSpell(caster, code, enemy));
	T_FEQ(caster->mana.value, 225.0f, 0.001f);
	T_ASSERT(!S_SpellCooldownReady(caster, code));
	T_FEQ(S_SpellCooldownRemaining(caster, code), 3.0f, 0.001f);
	T_ASSERT(!S_UnitHasStatus(enemy, MAKEFOURCC('B','b','l','o')));
	stolen = unit_findstatus(ally, MAKEFOURCC('B','b','l','o'));
	T_NOT_NULL(stolen);
	if (stolen) {
		T_EQ(stolen->level, 2u);
		T_EQ(stolen->timestamp, source_expiry);
		T_EQ(stolen->source_ability, app.source_ability);
		T_EQ(stolen->data, app.data);
		T_EQ(stolen->source, caster);
		T_EQ(stolen->rank, 2u);
		T_EQ(stolen->modifier_count, 1u);
		T_FEQ(stolen->modifiers[0].value, 0.4f, 0.001f);
	}
	T_ASSERT(!S_CastUnitTargetSpell(caster, code, enemy));
	T_FEQ(caster->mana.value, 225.0f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* A negative status on a friendly unit transfers to a nearby enemy. */
TEST(wc3_spell, spell_steal_transfers_negative_friendly_debuff_to_nearby_enemy) {
	static UnitAbilities_t const abilities = { .abilList = "Asps" };
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
		"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Area1\"\n"
		"C;Y2;X1;K\"Asps\"\nC;Y2;X2;K\"Asps\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,friend,enemy,neutral,self\"\n"
	"C;Y2;X5;K\"75\"\nC;Y2;X6;K\"3\"\nC;Y2;X7;K\"70\"\nC;Y2;X8;K\"32\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *ally, *enemy;
	heroabilitystatus_t *stolen;
	status_application_t app = {
		.buff = "Bfae", .level = 1, .duration = 12.0f,
		.source_ability = MAKEFOURCC('A','f','a','e'),
		.buff_flags = WC3_STATUS_BUFF_NEGATIVE | WC3_STATUS_BUFF_MAGICAL |
		              WC3_STATUS_BUFF_TRANSFERABLE
	};
	uint32_t const code = MAKEFOURCC('A','s','p','s');

	reset_entities(); setup_test_world(); level.time = 2000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	caster = alloc_test_unit(MAKEFOURCC('h','p','r','i'), 0, 0);
	caster->data.UnitAbilities = &abilities;
	ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 200, 0);
	caster->svflags |= SVF_MONSTER; ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	caster->targtype = ally->targtype = enemy->targtype = TARG_GROUND;
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	caster->mana.value = caster->mana.max_value = 300;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
	T_NOT_NULL(unit_applystatus(ally, &app));
	T_ASSERT(!S_CastUnitTargetSpell(caster, code, ally)); /* opposing recipient outside Area */
	T_FEQ(caster->mana.value, 300.0f, 0.001f);
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','f','a','e')));
	enemy->s.origin.x = enemy->s.origin2.x = 80.0f;
	T_ASSERT(S_CastUnitTargetSpell(caster, code, ally));
	T_ASSERT(!S_UnitHasStatus(ally, MAKEFOURCC('B','f','a','e')));
	stolen = unit_findstatus(enemy, MAKEFOURCC('B','f','a','e'));
	T_NOT_NULL(stolen);
	if (stolen) {
		T_EQ(stolen->level, 1u);
		T_EQ(stolen->source_ability, app.source_ability);
		T_EQ(stolen->source, caster);
		T_ASSERT(stolen->timestamp > G_Time());
		T_ASSERT(stolen->timestamp <= G_Time() + 12000u);
	}
	T_FEQ(caster->mana.value, 225.0f, 0.001f);
	unit_removestatus(enemy, unit_findstatus(enemy, MAKEFOURCC('B','f','a','e')), STATUS_REMOVE_SCRIPT);
	level.time += 3000;
	T_NOT_NULL(unit_applystatus(caster, &app));
	enemy->s.origin.x = enemy->s.origin2.x = 20.0f;
	T_ASSERT(S_CastUnitTargetSpell(caster, code, caster)); /* Self is an authored valid source target. */
	T_ASSERT(!S_UnitHasStatus(caster, MAKEFOURCC('B','f','a','e')));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','f','a','e')));

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Autocast only commits for an explicitly transferable source status with a
 * valid opposing recipient in the source unit's authored effect area. */
TEST(wc3_spell, spell_steal_autocast_requires_transferable_buff_and_recipient) {
	static UnitAbilities_t const abilities = { .abilList = "Asps" };
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
		"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Area1\"\n"
		"C;Y2;X1;K\"Asps\"\nC;Y2;X2;K\"Asps\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,friend,enemy,neutral,self\"\n"
		"C;Y2;X5;K\"75\"\nC;Y2;X6;K\"3\"\nC;Y2;X7;K\"70\"\nC;Y2;X8;K\"32\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *enemy, *ally;
	abilityitem_t item;
	abilityCall_t call;
	status_application_t special[] = {
		{ .buff = "Bams", .level = 1, .duration = 10.0f },
		{ .buff = "Buhf", .level = 1, .duration = 10.0f },
		{ .buff = "Bcyc", .level = 1, .duration = 10.0f }
	};
	status_application_t positive = {
		.buff = "Bblo", .level = 1, .duration = 10.0f,
		.buff_flags = WC3_STATUS_BUFF_POSITIVE | WC3_STATUS_BUFF_MAGICAL |
		              WC3_STATUS_BUFF_TRANSFERABLE
	};
	uint32_t const code = MAKEFOURCC('A','s','p','s');

	reset_entities(); setup_test_world(); level.time = 3000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 80, 0);
	enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
	caster = alloc_test_unit(MAKEFOURCC('h','p','r','i'), 0, 0);
	caster->data.UnitAbilities = &abilities;
	caster->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER; ally->svflags |= SVF_MONSTER;
	caster->targtype = enemy->targtype = ally->targtype = TARG_GROUND;
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	caster->mana.value = caster->mana.max_value = 300;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
	item = S_AbilityItem(code); call = MAKE(abilityCall_t, .item = &item);
	T_ASSERT(G_SetUnitAutocast(caster, code, true));
	for (uint32_t i = 0; i < sizeof(special) / sizeof(special[0]); i++) {
		uint32_t buff = FS_SLKKey(special[i].buff);
		T_NOT_NULL(unit_applystatus(enemy, &special[i]));
		T_ASSERT(!S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
		T_FEQ(caster->mana.value, 300.0f, 0.001f);
		T_ASSERT(!S_UnitHasStatus(ally, buff));
		unit_removestatus(enemy, unit_findstatus(enemy, buff), STATUS_REMOVE_SCRIPT);
	}
	T_NOT_NULL(unit_applystatus(enemy, &positive));
	T_ASSERT(S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_FEQ(caster->mana.value, 225.0f, 0.001f);
	T_ASSERT(!S_UnitHasStatus(enemy, MAKEFOURCC('B','b','l','o')));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','b','l','o')));

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, control_magic_costs_current_health_and_transfers_summon) {
	static UnitAbilities_t const abilities = { .abilList = "Acmg" };
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
		"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"DataB1\"\n"
		"C;Y2;X1;K\"Acmg\"\nC;Y2;X2;K\"Acmg\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,friend,enemy,neutral,summoned,ward\"\n"
		"C;Y2;X5;K\"0\"\nC;Y2;X6;K\"5\"\nC;Y2;X7;K\"70\"\n"
		"C;Y2;X8;K\"60\"\nC;Y2;X9;K\"0.45\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *enemy_summoner, *summon, *second_summon;
	uint32_t const code = MAKEFOURCC('A','c','m','g');

	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	caster = alloc_test_unit(MAKEFOURCC('h','b','l','m'), 0, 0);
	enemy_summoner = alloc_test_unit(MAKEFOURCC('o','sh','m',' '), 256, 0);
	summon = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	second_summon = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 32, 0);
	caster->data.UnitAbilities = &abilities;
	caster->svflags |= SVF_MONSTER;
	enemy_summoner->svflags |= SVF_MONSTER;
	summon->svflags |= SVF_MONSTER; second_summon->svflags |= SVF_MONSTER;
	caster->s.player = 0;
	enemy_summoner->s.player = summon->s.player = second_summon->s.player = 1;
	caster->targtype = summon->targtype = second_summon->targtype = TARG_GROUND;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
	caster->mana.value = caster->mana.max_value = 100.0f;
	summon->health.max_value = 500.0f;
	summon->health.value = 200.0f; /* Cost follows current HP, not max HP: 200 x 0.45 = 90. */
	summon->summon_ability = MAKEFOURCC('A','s','u','m');
	summon->owner = enemy_summoner;
	summon->combatentity = enemy_summoner;
	second_summon->summon_ability = MAKEFOURCC('A','s','u','m');
	second_summon->owner = enemy_summoner;
	second_summon->health.value = 100.0f;

	T_FEQ(S_SpellData(code, 1, 2), 0.45f, 0.001f);
	T_FEQ(S_SpellRange(code, 1), 70.0f, 0.001f);
	T_FEQ(S_SpellNumber(code, ABILITY_NUMBER_COOLDOWN, 1), 5.0f, 0.001f);
	T_FEQ(S_SpellDuration(code, 1, false), 60.0f, 0.001f);
	/* Without a summoner owner, an ordinary enemy unit is not controllable. */
	summon->owner = NULL;
	T_ASSERT(!S_CastUnitTargetSpell(caster, code, summon));
	T_FEQ(caster->mana.value, 100.0f, 0.001f);
	T_EQ(summon->s.player, 1u);
	summon->owner = enemy_summoner;

	/* Insufficient mana and out-of-range attempts must be side-effect-free. */
	caster->mana.value = 89.0f;
	T_ASSERT(!S_CastUnitTargetSpell(caster, code, summon));
	T_FEQ(caster->mana.value, 89.0f, 0.001f);
	caster->mana.value = 100.0f;
	summon->s.origin.x = summon->s.origin2.x = 71.0f;
	T_ASSERT(!S_CastUnitTargetSpell(caster, code, summon));
	T_FEQ(caster->mana.value, 100.0f, 0.001f);
	summon->s.origin.x = summon->s.origin2.x = 64.0f;

	T_ASSERT(S_CastUnitTargetSpell(caster, code, summon));
	T_FEQ(caster->mana.value, 10.0f, 0.001f);
	T_EQ(summon->s.player, 0u);
	T_EQ(summon->owner, caster);
	T_EQ(summon->combatentity, NULL);
	T_ASSERT(!S_SpellCooldownReady(caster, code));
	T_FEQ(S_SpellCooldownRemaining(caster, code), 5.0f, 0.001f);

	/* The cooldown blocks a second eligible summon even with enough mana. */
	caster->mana.value = 100.0f;
	T_ASSERT(!S_CastUnitTargetSpell(caster, code, second_summon));
	T_FEQ(caster->mana.value, 100.0f, 0.001f);
	T_EQ(second_summon->s.player, 1u);
	level.time += 5000;
	T_ASSERT(S_CastUnitTargetSpell(caster, code, second_summon));
	T_FEQ(caster->mana.value, 55.0f, 0.001f); /* 100 HP x 0.45 mana. */
	T_EQ(second_summon->s.player, 0u);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, control_magic_can_target_enemy_wards) {
	static UnitAbilities_t const abilities = { .abilList = "Acmg" };
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
		"C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
		"C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataB1\"\n"
		"C;Y2;X1;K\"Acmg\"\nC;Y2;X2;K\"Acmg\"\nC;Y2;X3;K\"1\"\n"
		"C;Y2;X4;K\"air,ground,friend,enemy,neutral,summoned,ward\"\n"
		"C;Y2;X5;K\"0\"\nC;Y2;X6;K\"5\"\nC;Y2;X7;K\"70\"\n"
		"C;Y2;X8;K\"0.45\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *summoner, *ward;
	uint32_t const code = MAKEFOURCC('A','c','m','g');

	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	caster = alloc_test_unit(MAKEFOURCC('h','b','l','m'), 0, 0);
	summoner = alloc_test_unit(MAKEFOURCC('o','sh','m',' '), 128, 0);
	ward = alloc_test_unit(MAKEFOURCC('o','w','a','r'), 64, 0);
	caster->data.UnitAbilities = &abilities;
	caster->svflags |= SVF_MONSTER; summoner->svflags |= SVF_MONSTER; ward->svflags |= SVF_MONSTER;
	caster->s.player = 0; summoner->s.player = ward->s.player = 1;
	caster->targtype = TARG_GROUND; ward->targtype = TARG_WARD;
	caster->mana.value = caster->mana.max_value = 100.0f;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
	ward->health.value = 100.0f;
	ward->owner = summoner;
	ward->summon_ability = MAKEFOURCC('A','s','u','m');
	T_ASSERT(S_SpellAllowsTarget(code, caster, ward));
	T_ASSERT(S_CastUnitTargetSpell(caster, code, ward));
	T_EQ(ward->s.player, 0u);
	T_EQ(ward->owner, caster);
	T_FEQ(caster->mana.value, 55.0f, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Afae lends the marked unit's sight to its caster, follows movement, and
 * ends with Bfae without leaking vision to unrelated players. */
TEST(wc3_spell, faerie_fire_shares_target_sight_for_buff_lifetime) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\n"
        "C;Y1;X6;K\"BuffID1\"\nC;Y1;X7;K\"levels\"\n"
        "C;Y2;X1;K\"Afae\"\nC;Y2;X2;K\"Afae\"\n"
        "C;Y2;X3;K\"5\"\nC;Y2;X4;K\"5\"\n"
        "C;Y2;X5;K\"6\"\nC;Y2;X6;K\"Bfae\"\nC;Y2;X7;K\"1\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *second_caster, *target;
    heroabilitystatus_t *status;

    reset_entities(); setup_test_world(); level.time = 1000;
    memset(level.alliances, 0, sizeof(level.alliances));
    G_FowInit(); G_FowConnectPlayer(0); G_FowConnectPlayer(2);
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('e','d','o','t'), 0, 0);
    second_caster = alloc_test_unit(MAKEFOURCC('e','d','o','t'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 512, 0);
    caster->s.player = 0; second_caster->s.player = 2; target->s.player = 1;
    caster->health.value = second_caster->health.value = target->health.value = 100;
    caster->health.max_value = second_caster->health.max_value = target->health.max_value = 100;
    target->unit_state->armor_value = 8.0f;
    target->runtime.sight_radius.day = target->runtime.sight_radius.night = 600.0f;
    caster->runtime.sight_radius.day = caster->runtime.sight_radius.night = 0.0f;
    second_caster->runtime.sight_radius.day = second_caster->runtime.sight_radius.night = 0.0f;
    G_SetPlayerAlliance(G_GetPlayerByNumber(0), G_GetPlayerByNumber(2), ALLIANCE_SHARED_VISION, true);
    G_FowUpdate();
    T_ASSERT(!G_FowPointIsVisible(0, &target->s.origin2));
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, target));

    T_ASSERT(test_execute_code(caster, "Afae", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    status = unit_findstatus(target, MAKEFOURCC('B','f','a','e'));
    T_NOT_NULL(status);
    T_EQ(status->data, 1);
    T_FEQ(G_UnitArmorValue(target), 2.0f, 0.01f); /* fixture base armor 8, DataA=6 */
    T_ASSERT(G_FowPlayerCanSeeEntity(0, target));
    T_ASSERT(G_FowPlayerCanHoverEntity(0, target));
    T_ASSERT(G_FowPlayerCanSeeEntity(2, target)); /* caster shares vision outward */
    T_ASSERT(!G_FowPlayerCanSeeEntity(3, target));
    G_FowUpdate();
    T_ASSERT(G_FowPointIsVisible(0, &target->s.origin2));
    T_ASSERT(G_FowPointIsVisible(2, &target->s.origin2));
    T_ASSERT(!G_FowPointIsVisible(3, &target->s.origin2));
    target->s.origin2.x = target->s.origin.x = 768.0f;
    G_FowUpdate();
    T_ASSERT(G_FowPlayerCanSeeEntity(0, target));
    T_ASSERT(G_FowPointIsVisible(0, &target->s.origin2));

    /* A new caster owns a refreshed mark, not the previous player's reveal. */
    T_ASSERT(test_execute_code(second_caster, "Afae", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    T_EQ(unit_findstatus(target, MAKEFOURCC('B','f','a','e'))->data, 3);
    G_FowUpdate(); /* Rebuild viewer grids after the reveal's owner changes. */
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, target));
    T_ASSERT(G_FowPlayerCanSeeEntity(2, target));
    T_ASSERT(!G_FowPointIsVisible(0, &target->s.origin2));
    T_ASSERT(G_FowPointIsVisible(2, &target->s.origin2));
    unit_removestatus(target, unit_findstatus(target, MAKEFOURCC('B','f','a','e')), STATUS_REMOVE_DISPEL);
    G_FowUpdate(); /* Expired/dispelled status no longer contributes target sight. */
    T_ASSERT(!G_FowPlayerCanSeeEntity(2, target));
    T_ASSERT(!G_FowPointIsVisible(2, &target->s.origin2));

    T_ASSERT(test_execute_code(caster, "Afae", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    caster->health.value = 0; /* the target's mark survives the caster's death */
    level.time = 5999;
    T_ASSERT(G_FowPlayerCanSeeEntity(0, target));
    level.time = 6000;
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, target));
    unit_updatestatuses(target);
    T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','f','a','e')));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows); G_FowShutdown();
}

/* ROC AbilityData lacks BuffID; alternate/crow and creep Faerie Fire still
 * need Bfae so their casts grant the same unit vision as Afae. */
TEST(wc3_spell, faerie_fire_roc_fallback_covers_alternate_and_creep) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\n"
        "C;Y2;X1;K\"Afa2\"\nC;Y2;X2;K\"Afa2\"\nC;Y2;X3;K\"4\"\nC;Y2;X4;K\"4\"\nC;Y2;X5;K\"5\"\n"
        "C;Y3;X1;K\"ACff\"\nC;Y3;X2;K\"ACff\"\nC;Y3;X3;K\"3\"\nC;Y3;X4;K\"3\"\nC;Y3;X5;K\"6\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *target;

    reset_entities(); setup_test_world(); level.time = 1000;
    memset(level.alliances, 0, sizeof(level.alliances));
    G_FowInit(); G_FowConnectPlayer(0);
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('e','d','o','t'), 0, 0);
    target = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 512, 0);
    caster->s.player = 0; target->s.player = 1;
    caster->runtime.sight_radius.day = caster->runtime.sight_radius.night = 0.0f;
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, target));
    T_ASSERT(test_execute_code(caster, "Afa2", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','f','a','e')));
    T_ASSERT(G_FowPlayerCanSeeEntity(0, target));
    unit_removestatus(target, unit_findstatus(target, MAKEFOURCC('B','f','a','e')), STATUS_REMOVE_DISPEL);
    T_ASSERT(!G_FowPlayerCanSeeEntity(0, target));
    T_ASSERT(test_execute_code(caster, "ACff", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = target)));
    T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','f','a','e')));
    T_ASSERT(G_FowPlayerCanSeeEntity(0, target));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows); G_FowShutdown();
}

TEST(wc3_spell, bloodlust_autocast_waits_for_spell_cooldown) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
		"C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Ablo\"\nC;Y2;X2;K\"Ablo\"\nC;Y2;X3;K\"air,ground,friend\"\n"
		"C;Y2;X4;K\"50\"\nC;Y2;X5;K\"10\"\nC;Y2;X6;K\"600\"\n"
		"C;Y2;X7;K\"30\"\nC;Y2;X8;K\"30\"\nC;Y2;X9;K\"Bblo\"\nE\n";
	static UnitAbilities_t abilities = { .abilList = "Ablo" };
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *ally, *other_ally, *enemy;
	abilityitem_t item;
	abilityCall_t call;
	float mana_after_cast;

	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	caster = alloc_test_unit(MAKEFOURCC('o','s','h','m'), 0, 0);
	ally = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
	other_ally = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 96, 0);
	enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
	caster->data.UnitAbilities = &abilities;
	caster->s.player = ally->s.player = other_ally->s.player = 0;
	enemy->s.player = 1;
	caster->mana.value = caster->mana.max_value = 300;
	caster->svflags |= SVF_MONSTER;
	ally->svflags |= SVF_MONSTER;
	other_ally->svflags |= SVF_MONSTER;
	enemy->svflags |= SVF_MONSTER;
	ally->targtype = TARG_GROUND;
	other_ally->targtype = TARG_GROUND;
	enemy->targtype = TARG_GROUND;
	ally->unit_state->attack1.type = other_ally->unit_state->attack1.type = ATK_NORMAL;
	ally->unit_state->attack1.range = other_ally->unit_state->attack1.range = 100.0f;
	ally->unit_state->attack1.targetsAllowed = other_ally->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
	ally->health.value = ally->health.max_value = 500;
	other_ally->health.value = other_ally->health.max_value = 500;
	item = S_AbilityItem(FS_SLKKey("Ablo"));
	call = MAKE(abilityCall_t, .item = &item);

	T_ASSERT(G_SetUnitAutocast(caster, item.code, true));
	order_attack(ally, enemy);
	T_ASSERT(S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_ASSERT(!S_SpellCooldownReady(caster, item.code));
	mana_after_cast = caster->mana.value;
	T_FEQ(mana_after_cast, 250, 0.001f);
	T_ASSERT(!S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_FEQ(caster->mana.value, mana_after_cast, 0.001f);

	level.time += 10000;
	order_attack(other_ally, enemy);
	T_ASSERT(S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_FEQ(caster->mana.value, mana_after_cast - 50, 0.001f);
	T_ASSERT(S_UnitHasStatus(other_ally, MAKEFOURCC('B','b','l','o')));

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, bloodlust_autocast_skips_unit_already_bloodlusted) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
		"C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Ablo\"\nC;Y2;X2;K\"Ablo\"\nC;Y2;X3;K\"air,ground,friend\"\n"
		"C;Y2;X4;K\"50\"\nC;Y2;X5;K\"10\"\nC;Y2;X6;K\"600\"\n"
		"C;Y2;X7;K\"30\"\nC;Y2;X8;K\"30\"\nC;Y2;X9;K\"Bblo\"\nE\n";
	static UnitAbilities_t abilities = { .abilList = "Ablo" };
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster, *buffed, *eligible, *enemy;
	abilityitem_t item;
	abilityCall_t call;

	reset_entities(); setup_test_world(); level.time = 1000;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	old = G_SetSLKRows("AbilityData", rows);
	caster = alloc_test_unit(MAKEFOURCC('o','s','h','m'), 0, 0);
	buffed = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 32, 0);
	eligible = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
	enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
	caster->data.UnitAbilities = &abilities;
	caster->s.player = buffed->s.player = eligible->s.player = 0;
	enemy->s.player = 1;
	caster->mana.value = caster->mana.max_value = 300;
	caster->svflags |= SVF_MONSTER;
	buffed->svflags |= eligible->svflags |= SVF_MONSTER;
	enemy->svflags |= SVF_MONSTER;
	buffed->targtype = eligible->targtype = TARG_GROUND;
	enemy->targtype = TARG_GROUND;
	eligible->unit_state->attack1.type = ATK_NORMAL;
	eligible->unit_state->attack1.range = 100.0f;
	eligible->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
	buffed->health.value = buffed->health.max_value = 500;
	eligible->health.value = eligible->health.max_value = 500;
	item = S_AbilityItem(FS_SLKKey("Ablo"));
	call = MAKE(abilityCall_t, .item = &item);

	T_ASSERT(test_execute_code(caster, "Ablo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = buffed)));
	T_ASSERT(S_UnitHasStatus(buffed, MAKEFOURCC('B','b','l','o')));
	T_ASSERT(G_SetUnitAutocast(caster, item.code, true));
	T_ASSERT(!S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	order_attack(eligible, enemy);
	T_ASSERT(S_AbilityMessage(caster, A_AUTOCAST_ACQUIRE, &call));
	T_ASSERT(S_UnitHasStatus(eligible, MAKEFOURCC('B','b','l','o')));
	T_FEQ(caster->mana.value, 250, 0.001f);

	G_SetSLKRows("AbilityData", old);
	free_slk_rows(rows);
}

TEST(wc3_spell, frenzy_unholy_frenzy_curse_use_authored_status_and_bonus_values) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\nC;Y1;X8;K\"BuffID1\"\n"
		"C;Y1;X9;K\"Area1\"\nC;Y1;X10;K\"targs\"\n"
		"C;Y2;X1;K\"Afzy\"\nC;Y2;X2;K\"Afzy\"\nC;Y2;X4;K\"20\"\nC;Y2;X5;K\"20\"\n"
		"C;Y2;X6;K\"0.5\"\nC;Y2;X7;K\"3\"\nC;Y2;X8;K\"Bfzy\"\n"
		"C;Y3;X1;K\"Auhf\"\nC;Y3;X2;K\"Auhf\"\nC;Y3;X4;K\"15\"\nC;Y3;X5;K\"15\"\n"
		"C;Y3;X6;K\"0.5\"\nC;Y3;X7;K\"2\"\nC;Y3;X8;K\"Buhf\"\n"
		"C;Y4;X1;K\"Acrs\"\nC;Y4;X2;K\"Acrs\"\nC;Y4;X4;K\"60\"\nC;Y4;X5;K\"60\"\n"
		"C;Y4;X6;K\"0.33\"\nC;Y4;X8;K\"Bcrs\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	abilityitem_t fzy = S_AbilityItem(FS_SLKKey("Afzy"));
	abilityitem_t uhf = S_AbilityItem(FS_SLKKey("Auhf"));
	abilityitem_t crs = S_AbilityItem(FS_SLKKey("Acrs"));
	edict_t *curse_effect = NULL;
	uint32_t curse_effect_count = 0;
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->unit_state->armor_value = 5; ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	ally->health.value = ally->health.max_value = 100;
	T_EQ(fzy.ability->proc, CAbilityFrenzy); T_ASSERT(fzy.ability->flags & AB_AUTOCAST);
	T_EQ(uhf.ability->proc, CAbilityUnholyFrenzy); T_ASSERT(!(uhf.ability->flags & AB_AUTOCAST));
	T_EQ(crs.ability->proc, CAbilityCurse); T_ASSERT(crs.ability->flags & AB_AUTOCAST);
	test_execute_code(caster, "Afzy", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','f','z','y')));
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0.5f, 0.001f);
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ARMOR_FLAT), -3.0f, 0.001f);
	T_FEQ(G_UnitArmorValue(ally), 5.0f - 3.0f, 0.001f);
	test_execute_code(caster, "Auhf", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','u','h','f')));
	T_FEQ(unit_status_modifier_total(ally, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 1.0f, 0.001f);
	T_FEQ(S_UnholyFrenzyLifeDrain(ally), 2.0f, 0.001f);
	test_execute_code(caster, "Acrs", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','s'))); T_FEQ(S_CurseMissChance(enemy), 0.33f, 0.001f);
	T_ASSERT(unit_findstatus(enemy, MAKEFOURCC('B','c','r','s'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
	FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','r','s') && effect->goalentity == enemy) {
		curse_effect = effect;
		T_EQ(effect->s.model, G_RegisterModel("TestUI\\Models\\anim_pulse.mdx"));
		curse_effect_count++;
	}
	T_EQ(curse_effect_count, 1);
	unit_removestatus(enemy, unit_findstatus(enemy, MAKEFOURCC('B','c','r','s')), STATUS_REMOVE_SCRIPT);
	T_EQ(curse_effect->status_effect_code, 0);

	/* Death keeps timed status state for corpse presentation but ends its art. */
	T_ASSERT(test_execute_code(caster, "Acrs", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy)));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','s')));
	unit_die(enemy, caster);
	curse_effect_count = 0;
	FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','r','s') && effect->goalentity == enemy)
		curse_effect_count++;
	T_EQ(curse_effect_count, 0);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, banshee_curse_blocks_attack_damage_at_authored_miss_chance) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\n"
		"C;Y1;X7;K\"targs\"\nC;Y1;X8;K\"levels\"\n"
		"C;Y2;X1;K\"Acrs\"\nC;Y2;X2;K\"Acrs\"\nC;Y2;X3;K\"60\"\n"
		"C;Y2;X4;K\"60\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"Bcrs\"\n"
		"C;Y2;X7;K\"ground,enemy,organic,nonhero\"\nC;Y2;X8;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *banshee = make_hero(MAKEFOURCC('u','b','a','n'), 300, 100, 0, 0);
	edict_t *attacker = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
	edict_t *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
	banshee->s.player = victim->s.player = 0; attacker->s.player = 1;
	attacker->svflags |= SVF_MONSTER; victim->svflags |= SVF_MONSTER;
	attacker->unit_state->attack1.type = ATK_NORMAL;
	attacker->targtype = victim->targtype = TARG_GROUND;
	attacker->health.value = attacker->health.max_value = 100;
	victim->health.value = victim->health.max_value = 100;

	/* Establish that this attacker damages the victim before Curse is applied. */
	S_ResolveAttackHit(attacker, victim, 25);
	T_ASSERT(victim->health.value < 100);
	victim->health.value = victim->health.max_value = 100;

	T_ASSERT(test_execute_code(banshee, "Acrs",
		MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = attacker)));
	T_ASSERT(S_UnitHasStatus(attacker, MAKEFOURCC('B','c','r','s')));
	T_FEQ(S_CurseMissChance(attacker), 1.0f, 0.001f);
	S_ResolveAttackHit(attacker, victim, 25);
	T_FEQ(victim->health.value, 100, 0.001f);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ROC omits BuffID; apply the TFT fourcc so Bloodlust/Faerie/Roar/Curse still land. */
TEST(wc3_spell, melee_status_roc_empty_buffid_applies_default_fourcc) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y7;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
		"C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"targs\"\n"
		"C;Y2;X1;K\"Ablo\"\nC;Y2;X2;K\"Ablo\"\nC;Y2;X3;K\"12\"\nC;Y2;X4;K\"12\"\nC;Y2;X5;K\"0.31\"\nC;Y2;X6;K\"0.17\"\n"
		"C;Y3;X1;K\"Afae\"\nC;Y3;X2;K\"Afae\"\nC;Y3;X3;K\"12\"\nC;Y3;X4;K\"12\"\nC;Y3;X5;K\"3\"\n"
		"C;Y4;X1;K\"Arej\"\nC;Y4;X2;K\"Arej\"\nC;Y4;X3;K\"8\"\nC;Y4;X4;K\"8\"\nC;Y4;X5;K\"80\"\n"
		"C;Y5;X1;K\"Aroa\"\nC;Y5;X2;K\"Aroa\"\nC;Y5;X3;K\"12\"\nC;Y5;X4;K\"12\"\nC;Y5;X5;K\"0.19\"\nC;Y5;X7;K\"400\"\n"
		"C;Y6;X1;K\"Acrs\"\nC;Y6;X2;K\"Acrs\"\nC;Y6;X3;K\"12\"\nC;Y6;X4;K\"12\"\nC;Y6;X5;K\"0.21\"\n"
		"C;Y7;X1;K\"Auhf\"\nC;Y7;X2;K\"Auhf\"\nC;Y7;X3;K\"12\"\nC;Y7;X4;K\"12\"\nC;Y7;X5;K\"0.37\"\nC;Y7;X6;K\"3\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
	caster->s.player = ally->s.player = 0; enemy->s.player = 1;
	ally->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	ally->health.value = ally->health.max_value = 100;
	test_execute_code(caster, "Ablo", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','b','l','o')));
	T_FEQ(S_BloodlustAttackBonus(ally), 0.31f, 0.001f);
	test_execute_code(caster, "Afae", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','f','a','e')));
	test_execute_code(caster, "Arej", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','e','j')));
	test_execute_code(caster, "Aroa", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','r','o','a')));
	test_execute_code(caster, "Acrs", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','s')));
	test_execute_code(caster, "Auhf", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','U','h','f')));
	T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0.37f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* targs have no allegiance token; TFT BuffID BUhf must drive the attack/drain consumers. */
TEST(wc3_spell, unholy_frenzy_accepts_enemy_and_reads_tft_buffid) {
	/* Retail AbilityData: +75% attack rate, 4 HP/sec drain, 45-second duration. */
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
		"C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"Rng1\"\n"
		"C;Y2;X1;K\"Auhf\"\nC;Y2;X2;K\"Auhf\"\nC;Y2;X3;K\"air,ground,organic\"\n"
		"C;Y2;X4;K\"50\"\nC;Y2;X5;K\"45\"\nC;Y2;X6;K\"45\"\n"
		"C;Y2;X7;K\"0.75\"\nC;Y2;X8;K\"4\"\nC;Y2;X9;K\"BUhf\"\nC;Y2;X10;K\"500\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	UnitBalance_t enemy_balance = { .healthRegenType = "none" };
	edict_t *caster = make_hero(MAKEFOURCC('u','n','e','c'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	caster->s.player = 0; enemy->s.player = 1;
	G_InitUnitOrders(caster);
	caster->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
	caster->targtype = enemy->targtype = TARG_GROUND;
	enemy->data.UnitBalance = &enemy_balance;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','u','h','f'), .level = 1);
	caster->mana.value = caster->mana.max_value = 100;
	enemy->health.value = enemy->health.max_value = 200;
	T_ASSERT(S_CastUnitTargetSpell(caster, MAKEFOURCC('A','u','h','f'), enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','U','h','f')));
	T_EQ(unit_findstatus(enemy, MAKEFOURCC('B','U','h','f'))->duration_ms, 45000u);
	T_EQ(G_UnitStatusBuffCode(unit_findstatus(enemy, MAKEFOURCC('B','U','h','f'))), MAKEFOURCC('B','u','h','f'));
	T_STREQ(G_AbilityBuffArt(MAKEFOURCC('B','U','h','f')),
	        "ReplaceableTextures\\CommandButtons\\BTNUnholyFrenzy.blp");
	T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','U','h','f'), WC3_EFFECT_TARGET, 0),
	        "Abilities\\Spells\\Undead\\UnholyFrenzy\\UnholyFrenzyTarget.mdx");
	T_ASSERT(unit_findstatus(enemy, MAKEFOURCC('B','U','h','f'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
	{
		uint32_t target_art_count = 0;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','U','h','f') && effect->goalentity == enemy) {
			T_EQ(effect->s.model, G_RegisterModel("Abilities\\Spells\\Undead\\UnholyFrenzy\\UnholyFrenzyTarget.mdx"));
			target_art_count++;
		}
		T_EQ(target_art_count, 1u);
	}
	T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_ATTACK_SPEED_PERCENT), 0.75f, 0.001f);
	T_FEQ(S_UnholyFrenzyLifeDrain(enemy), 4.0f, 0.001f);
	enemy->unit_state->attack1.cooldown = 1.0f;
	enemy->unit_state->attack1.damagePoint = 0.3f;
	attack_melee(enemy);
	T_FEQ(enemy->wait, 0.3f / 1.75f, 0.001f);
	FOR_LOOP(i, 10) G_RunEntity(enemy);
	T_FEQ(enemy->health.value, 196.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, cripple_and_soul_burn_apply_status_and_read_authored_consumers) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\nC;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\n"
		"C;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"Area1\"\n"
		"C;Y2;X1;K\"Acri\"\nC;Y2;X2;K\"Acri\"\nC;Y2;X4;K\"30\"\nC;Y2;X5;K\"15\"\n"
		"C;Y2;X6;K\"0.5\"\nC;Y2;X7;K\"0.4\"\nC;Y2;X8;K\"Bcri\"\nC;Y2;X9;K\"0.3\"\n"
		"C;Y3;X1;K\"ANso\"\nC;Y3;X2;K\"ANso\"\nC;Y3;X4;K\"10\"\nC;Y3;X5;K\"10\"\n"
		"C;Y3;X6;K\"5\"\nC;Y3;X8;K\"BNso\"\nC;Y3;X9;K\"0.25\"\n"
		"C;Y4;X1;K\"Atau\"\nC;Y4;X2;K\"Atau\"\nC;Y4;X10;K\"400\"\nE\n";
	const char buff_slk[] =
		"ID;PWXL;N;EBB;Y2;X13\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"comments\"\n"
		"C;Y1;X4;K\"isEffect\"\nC;Y1;X5;K\"version\"\nC;Y1;X6;K\"useInEditor\"\n"
		"C;Y1;X7;K\"sort\"\nC;Y1;X8;K\"race\"\nC;Y1;X9;K\"Buffart\"\n"
		"C;Y1;X10;K\"Bufftip\"\nC;Y1;X11;K\"Buffubertip\"\nC;Y1;X12;K\"TargetArt\"\n"
		"C;Y1;X13;K\"SpecialArt\"\n"
		"C;Y2;X1;K\"Bcri\"\nC;Y2;X2;K\"Bcri\"\nC;Y2;X9;K\"ReplaceableTextures\\CommandButtons\\BTNCripple.blp\"\n"
		"C;Y2;X12;K\"Abilities\\Spells\\Undead\\Cripple\\CrippleTarget.mdx\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	abilityitem_t cri = S_AbilityItem(FS_SLKKey("Acri"));
	abilityitem_t nso = S_AbilityItem(FS_SLKKey("ANso"));
	abilityitem_t tau = S_AbilityItem(FS_SLKKey("Atau"));
	caster->s.player = 0; enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	enemy->health.value = enemy->health.max_value = 500;
	T_EQ(cri.ability->proc, CAbilityCripple);
	T_EQ(nso.ability->proc, CAbilitySoulBurn);
	T_EQ(tau.ability->proc, CAbilityTaunt);
	test_execute_code(caster, "Acri", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	T_STREQ(G_AbilityBuffArt(MAKEFOURCC('B','c','r','i')),
	        "ReplaceableTextures\\CommandButtons\\BTNCripple.blp");
	T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','c','r','i'), WC3_EFFECT_TARGET, 0),
	        "Abilities\\Spells\\Undead\\Cripple\\CrippleTarget.mdx");
	T_ASSERT(unit_findstatus(enemy, MAKEFOURCC('B','c','r','i'))->buff_flags & WC3_STATUS_BUFF_TARGET_ART);
	{
		uint32_t target_art_count = 0;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','c','r','i') && effect->goalentity == enemy) {
			T_EQ(effect->s.model, G_RegisterModel("Abilities\\Spells\\Undead\\Cripple\\CrippleTarget.mdx"));
			target_art_count++;
		}
		T_EQ(target_art_count, 1u);
	}
	T_FEQ(S_CrippleMoveReduction(enemy), 0.5f, 0.001f);
	T_FEQ(unit_status_modifier_total(enemy, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.5f, 0.001f);
	T_FEQ(S_CrippleAttackReduction(enemy), 0.4f, 0.001f);
	T_FEQ(S_CrippleDamageReduction(enemy), 0.3f, 0.001f);
	test_execute_code(caster, "ANso", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','N','s','o')));
	T_FEQ(S_SoulBurnDamageRate(enemy), 5.0f, 0.001f);
	T_FEQ(S_SoulBurnDamageReduction(enemy), 0.25f, 0.001f);
	test_execute_code(caster, "Atau", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
	G_SetSLKRows("AbilityBuffData", old_buff); free_slk_rows(buff_rows);
}

TEST(wc3_spell, mountain_giant_taunt_orders_ten_closest_enemies_in_authored_area) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"levels\"\n"
		"C;Y2;X1;K\"Atau\"\nC;Y2;X2;K\"Atau\"\nC;Y2;X3;K\"air,ground,enemy,vuln,invu\"\n"
		"C;Y2;X4;K\"450\"\nC;Y2;X5;K\"99\"\nC;Y2;X6;K\"1\"\nE\n";
	static UnitAbilities_t const spell_immunity = { .abilList = "Amim" };
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
	edict_t *caster, *enemy[12], *friendly, *outside;

	reset_entities(); setup_test_world();
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	caster = alloc_test_unit(MAKEFOURCC('n','t','r','d'), 0, 0);
	caster->s.player = 0;
	caster->svflags |= SVF_MONSTER;
	caster->targtype = TARG_GROUND;
	caster->health.value = caster->health.max_value = 1000;
	FOR_LOOP(i, 12) {
		enemy[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), (float)(i + 1) * 40.0f, 0);
		enemy[i]->s.player = 1;
		enemy[i]->svflags |= SVF_MONSTER;
		enemy[i]->targtype = TARG_GROUND;
		if (i != 0) {
			enemy[i]->data.UnitWeapons = &weapons;
			enemy[i]->unit_state->attack1.type = ATK_NORMAL;
			enemy[i]->unit_state->attack1.weapon = WPN_NORMAL;
			enemy[i]->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
		}
	}
	/* Taunt redirects attack orders, so spell immunity by itself is not an
	 * exclusion from its authored enemy target mask. */
	enemy[1]->data.UnitAbilities = &spell_immunity;
	friendly = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 20, 0);
	friendly->s.player = 0;
	friendly->svflags |= SVF_MONSTER;
	friendly->targtype = TARG_GROUND;
	friendly->data.UnitWeapons = &weapons;
	friendly->unit_state->attack1.type = ATK_NORMAL;
	friendly->unit_state->attack1.weapon = WPN_NORMAL;
	friendly->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
	outside = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 460, 0);
	outside->s.player = 1;
	outside->svflags |= SVF_MONSTER;
	outside->targtype = TARG_GROUND;
	outside->data.UnitWeapons = &weapons;
	outside->unit_state->attack1.type = ATK_NORMAL;
	outside->unit_state->attack1.weapon = WPN_NORMAL;
	outside->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);

	T_EQ((int)S_SpellNumber(MAKEFOURCC('A','t','a','u'), ABILITY_NUMBER_AREA, 1), 450);
	T_EQ((int)S_SpellData(MAKEFOURCC('A','t','a','u'), 1, 1), 99);
	T_ASSERT(S_SpellIsEnemy(caster, enemy[0]));
	T_ASSERT(S_SpellAllowsAreaTarget(MAKEFOURCC('A','t','a','u'), caster, enemy[0]));
	T_ASSERT(S_UnitSpellImmune(enemy[1]));
	T_ASSERT(S_SpellAllowsAreaTarget(MAKEFOURCC('A','t','a','u'), caster, enemy[1]));
	T_ASSERT(!S_AttackCanTarget(enemy[0], caster));
	T_ASSERT(S_AttackCanTarget(enemy[1], caster));
	T_ASSERT(test_execute_code(caster, "Atau", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE)));
	T_ASSERT(enemy[0]->goalentity != caster);
	FOR_LOOP(i, 10) T_ASSERT(enemy[i + 1]->goalentity == caster);
	T_ASSERT(enemy[11]->goalentity != caster);
	T_ASSERT(friendly->goalentity != caster);
	T_ASSERT(outside->goalentity != caster);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, mountain_giant_taunt_preserves_channel_and_repair_orders) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
		"C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"levels\"\n"
		"C;Y2;X1;K\"Atau\"\nC;Y2;X2;K\"Atau\"\nC;Y2;X3;K\"air,ground,enemy,vuln,invu\"\n"
		"C;Y2;X4;K\"450\"\nC;Y2;X5;K\"1\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	static UnitWeapons_t const weapons = { .attacksEnabled = 1 };
	edict_t *caster, *channeling, *repairing, *manual_target;
	buildwork_t *work;

	reset_entities(); setup_test_world();
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	caster = alloc_test_unit(MAKEFOURCC('n','t','r','d'), 0, 0);
	channeling = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	repairing = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 75, 0);
	manual_target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100, 0);
	caster->s.player = 0; caster->svflags |= SVF_MONSTER; caster->targtype = TARG_GROUND;
	caster->health.value = caster->health.max_value = 1000;
	channeling->s.player = repairing->s.player = manual_target->s.player = 1;
	channeling->svflags |= repairing->svflags |= manual_target->svflags |= SVF_MONSTER;
	channeling->targtype = repairing->targtype = manual_target->targtype = TARG_GROUND;
	channeling->health.value = repairing->health.value = manual_target->health.value = 500;
	channeling->health.max_value = repairing->health.max_value = manual_target->health.max_value = 500;
	channeling->data.UnitWeapons = repairing->data.UnitWeapons = manual_target->data.UnitWeapons = &weapons;
	channeling->unit_state->attack1.type = repairing->unit_state->attack1.type = manual_target->unit_state->attack1.type = ATK_NORMAL;
	channeling->unit_state->attack1.weapon = repairing->unit_state->attack1.weapon = manual_target->unit_state->attack1.weapon = WPN_NORMAL;
	channeling->unit_state->attack1.targetsAllowed = repairing->unit_state->attack1.targetsAllowed =
		manual_target->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
	G_AddChannel(channeling);
	G_GetChannel(channeling)->code = MAKEFOURCC('A','H','b','z');
	work = G_AddBuildwork(repairing);
	T_NOT_NULL(work);
	if (work) work->ability = MAKEFOURCC('A','r','e','p');

	T_ASSERT(S_SpellAllowsAreaTarget(MAKEFOURCC('A','t','a','u'), caster, manual_target));
	T_ASSERT(S_AttackCanTarget(manual_target, caster));
	T_ASSERT(test_execute_code(caster, "Atau", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE)));
	T_ASSERT(channeling->goalentity != caster);
	T_ASSERT(repairing->goalentity != caster);
	T_ASSERT(manual_target->goalentity == caster);
	/* Taunt is not a persistent lock; a fresh player order can replace it. */
	order_attack(manual_target, channeling);
	T_ASSERT(manual_target->goalentity == channeling);

	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, cripple_reduces_enemy_movement_attack_rate_and_damage_from_data) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\n"
		"C;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\n"
		"C;Y1;X8;K\"DataC1\"\nC;Y1;X9;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Acri\"\nC;Y2;X2;K\"Acri\"\n"
		"C;Y2;X3;K\"ground,enemy,organic\"\nC;Y2;X4;K\"60\"\nC;Y2;X5;K\"60\"\n"
		"C;Y2;X6;K\"0.75\"\nC;Y2;X7;K\"0.5\"\n"
		"C;Y2;X8;K\"0.5\"\nC;Y2;X9;K\"Bcri\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	edict_t *victim = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 500, 0);
	float const normal_move_distance = 10.0f * 270.0f / (float)FRAMETIME;
	caster->s.player = victim->s.player = 0; enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	enemy->health.value = enemy->health.max_value = 500;
	enemy->unitinfo.MoveSpeed = 270.0f;
	enemy->unit_state->attack1.type = ATK_NORMAL;
	enemy->unit_state->attack1.cooldown = 1.0f;
	enemy->unit_state->attack1.damagePoint = 0.3f;
	victim->health.value = victim->health.max_value = 200;
	test_execute_code(caster, "Acri", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	T_FEQ(S_CrippleMoveReduction(enemy), 0.75f, 0.001f);
	T_FEQ(S_CrippleAttackReduction(enemy), 0.5f, 0.001f);
	T_FEQ(S_CrippleDamageReduction(enemy), 0.5f, 0.001f);
	T_FEQ(unit_movedistance(enemy), normal_move_distance * 0.25f, 0.01f);
	attack_melee(enemy);
	T_FEQ(enemy->wait, 0.6f, 0.001f);
	S_ResolveAttackHit(enemy, victim, 40);
	T_FEQ(victim->health.value, 180.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, cripple_roc_empty_buffid_applies_bcri) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataC1\"\n"
		"C;Y2;X1;K\"Acri\"\nC;Y2;X2;K\"Acri\"\nC;Y2;X3;K\"14\"\nC;Y2;X4;K\"7\"\n"
		"C;Y2;X5;K\"0.41\"\nC;Y2;X6;K\"0.27\"\nC;Y2;X7;K\"0.13\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('h','p','r','i'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	caster->s.player = 0; enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER;
	test_execute_code(caster, "Acri", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	T_FEQ(S_CrippleMoveReduction(enemy), 0.41f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, alias_registrations_resolve_correct_procs) {
	T_EQ(S_AbilityItem(FS_SLKKey("ACtb")).ability->proc, CAbilityThunderBolt);
	T_EQ(S_AbilityItem(FS_SLKKey("Aens")).ability->proc, CAbilityEnsnare);
	T_EQ(S_AbilityItem(FS_SLKKey("Adch")).ability->proc, CAbilityDispelMagic);
	T_EQ(S_AbilityItem(FS_SLKKey("Advm")).ability->proc, CAbilityDispelMagic);
	T_EQ(S_AbilityItem(FS_SLKKey("ACcs")).ability->proc, CAbilityCurse);
	T_EQ(S_AbilityItem(FS_SLKKey("ACcr")).ability->proc, CAbilityCripple);
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
    uint32_t const ability = MAKEFOURCC('S','N','d','c');
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','C','0','2'), 1000, 1000, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('n','v','i','l'), 64, 0);
    edict_t *summon;
    uint32_t summon_num;

    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = ability, .level = 1);
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

TEST(wc3_spell, purge_removes_status_applies_slow_and_damages_summoned) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X9\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Rng1\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"Area1\"\nC;Y1;X9;K\"targs\"\n"
		/* Purge: slow factor 0.5, summoned damage 200, dur 15, buff Bprg */
		"C;Y2;X1;K\"Aprg\"\nC;Y2;X2;K\"Aprg\"\nC;Y2;X3;K\"600\"\n"
		"C;Y2;X4;K\"15\"\nC;Y2;X5;K\"0.5\"\nC;Y2;X6;K\"200\"\n"
		"C;Y2;X7;K\"Bprg\"\nC;Y2;X8;K\"0\"\nC;Y2;X9;K\"air,ground,enemy,neutral\"\n"
		/* Cripple: so we can verify Purge clears it */
		"C;Y3;X1;K\"Acri\"\nC;Y3;X2;K\"Acri\"\nC;Y3;X3;K\"600\"\n"
		"C;Y3;X4;K\"30\"\nC;Y3;X5;K\"0.5\"\nC;Y3;X6;K\"0\"\n"
		"C;Y3;X7;K\"Bcri\"\nC;Y3;X8;K\"0\"\nC;Y3;X9;K\"air,ground,enemy\"\n"
		/* Item Purge: shares CAbilityPurge */
		"C;Y4;X1;K\"AIlp\"\nC;Y4;X2;K\"Aprg\"\nC;Y4;X3;K\"600\"\n"
		"C;Y4;X4;K\"10\"\nC;Y4;X5;K\"0.5\"\nC;Y4;X6;K\"100\"\n"
		"C;Y4;X7;K\"Bprg\"\nC;Y4;X8;K\"0\"\nC;Y4;X9;K\"air,ground,enemy\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('o','s', 'h','m'), 300, 300, 0, 0);
	edict_t *enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	caster->s.player = 0; enemy->s.player = 1;
	enemy->svflags |= SVF_MONSTER; enemy->health.value = enemy->health.max_value = 500;
	T_EQ(S_AbilityItem(FS_SLKKey("Aprg")).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(FS_SLKKey("AIlp")).ability->proc, CAbilityPurge);
	/* Apply Cripple buff, then Purge it away and check Purge slow replaces it. */
	test_execute_code(caster, "Acri", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	test_execute_code(caster, "Aprg", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = enemy));
	T_ASSERT(!S_UnitHasStatus(enemy, MAKEFOURCC('B','c','r','i')));
	T_ASSERT(S_UnitHasStatus(enemy, MAKEFOURCC('B','p','r','g')));
	T_FEQ(S_PurgeMoveReduction(enemy), 0.5f, 0.001f);
	/* No extra damage when target is not a summoned unit (no owner). */
	T_FEQ(enemy->health.value, 500, 0.001f);
	/* Summoned unit (owner set) takes DataC damage. */
	edict_t *summon = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 300, 0);
	summon->s.player = 1; summon->svflags |= SVF_MONSTER;
	summon->health.value = summon->health.max_value = 500; summon->owner = caster;
	test_execute_code(caster, "Aprg", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = summon));
	T_ASSERT(summon->health.value < 500);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, lightning_shield_damages_nearby_units_each_second) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"Area1\"\n"
		"C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"Rng1\"\n"
		"C;Y2;X1;K\"Alsh\"\nC;Y2;X2;K\"Alsh\"\nC;Y2;X3;K\"air,ground,friend,enemy\"\n"
		"C;Y2;X4;K\"15\"\nC;Y2;X5;K\"25\"\nC;Y2;X6;K\"200\"\n"
		"C;Y2;X7;K\"Blsh\"\nC;Y2;X8;K\"600\"\nE\n";
	const char buff_slk[] =
		"ID;PWXL;N;EBB;Y2;X3\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"TargetArt\"\n"
		"C;Y2;X1;K\"Blsh\"\nC;Y2;X2;K\"Blsh\"\n"
		"C;Y2;X3;K\"TestUI\\\\Models\\\\anim_pulse.mdx\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *buff_rows = parse_slk_string(buff_slk), *old_buff = G_SetSLKRows("AbilityBuffData", buff_rows);
	edict_t *caster = make_hero(MAKEFOURCC('o','s','h','m'), 300, 300, 0, 0);
	/* Carrier is far from caster (0,0) so caster is outside the 200-unit Area. */
	edict_t *carrier = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 500, 0);
	edict_t *nearby = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 600, 0);  /* 100 from carrier, within Area=200 */
	edict_t *far_unit = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 800, 0);  /* 300 from carrier, beyond Area=200 */
	carrier->s.player = 0; carrier->svflags |= SVF_MONSTER; carrier->targtype = TARG_GROUND;
	carrier->health.value = carrier->health.max_value = 1000;
	nearby->s.player = 1; nearby->svflags |= SVF_MONSTER; nearby->targtype = TARG_GROUND;
	nearby->health.value = nearby->health.max_value = 1000;
	far_unit->s.player = 1; far_unit->svflags |= SVF_MONSTER; far_unit->targtype = TARG_GROUND;
	far_unit->health.value = far_unit->health.max_value = 1000;
	T_EQ(S_AbilityItem(FS_SLKKey("Alsh")).ability->proc, CAbilityLightningShield);
	test_execute_code(caster, "Alsh", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = carrier));
	T_ASSERT(S_UnitHasStatus(carrier, MAKEFOURCC('B','l','s','h')));
	T_NOT_NULL(G_AbilityEffectArt(MAKEFOURCC('B','l','s','h'), WC3_EFFECT_TARGET, 0));
	{
		bool attached_lightning_shield = false;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','l','s','h') &&
		              effect->goalentity == carrier && effect->damage == carrier->spawn_time)
			attached_lightning_shield = true;
		T_ASSERT(attached_lightning_shield);
	}
	/* Find the thinker and run it — nearby unit takes the initial tick of damage. */
	edict_t *thinker = NULL;
	FILTER_EDICTS(ent, ent->owner == carrier && ent->think == lsh_think) thinker = ent;
	T_NOT_NULL(thinker);
	level.time = 1000; G_RunEntity(thinker);
	T_ASSERT(nearby->health.value < 1000);    /* took damage */
	T_FEQ(far_unit->health.value, 1000, 0.001f);  /* too far, untouched */
	/* Second tick at 2000ms deals another round. */
	float hp = nearby->health.value;
	level.time = 2000; G_RunEntity(thinker);
	T_ASSERT(nearby->health.value < hp);
	/* Past expiry the thinker stops and the carrier no longer has the buff (status expired). */
	level.time = 30000; G_RunEntity(thinker);
	T_ASSERT(!thinker->inuse);
	G_SetSLKRows("AbilityData", old); G_SetSLKRows("AbilityBuffData", old_buff);
	free_slk_rows(rows); free_slk_rows(buff_rows);
}

/* A failed status insertion must not create Lightning Shield's damage thinker. */
TEST(wc3_spell, lightning_shield_full_status_capacity_rejects_cast_and_thinker) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"BuffID1\"\n"
        "C;Y1;X4;K\"Dur1\"\n"
        "C;Y2;X1;K\"Alsh\"\nC;Y2;X2;K\"Alsh\"\nC;Y2;X3;K\"Blsh\"\n"
        "C;Y2;X4;K\"15\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('o','s','h','m'), 300, 0, 0, 0);
    edict_t *carrier = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 300, 0);
    abilityitem_t spell = S_AbilityItem(MAKEFOURCC('A','l','s','h'));
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = carrier);
    carrier->s.player = caster->s.player;
    carrier->svflags |= SVF_MONSTER;
    carrier->targtype = TARG_GROUND;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        char id[5] = { 'B', 'q', (char)('A' + i / 10), (char)('0' + i % 10), 0 };
        T_ASSERT(unit_addtimedstatus(carrier, id, 1, 20.0f));
    }
    T_ASSERT(!CAbilityLightningShield(caster, A_VALIDATE, &(abilityCall_t){ .item = &spell, .target = &target }));
    /* Drive execute directly as well: a delayed impact can race with capacity. */
    T_ASSERT(CAbilityLightningShield(caster, A_EXECUTE, &(abilityCall_t){ .item = &spell, .target = &target }));
    T_NULL(unit_findstatus(carrier, MAKEFOURCC('B','l','s','h')));
    FILTER_EDICTS(ent, ent->owner == carrier && ent->think == lsh_think) T_ASSERT(false);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, lightning_shield_roc_missing_buff_id_uses_compat_fallback) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"DataA1\"\n"
        "C;Y2;X1;K\"Alsh\"\nC;Y2;X2;K\"Alsh\"\n"
        "C;Y2;X3;K\"15\"\nC;Y2;X4;K\"200\"\nC;Y2;X5;K\"25\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    static UnitAbilities_t const abilities = { .abilList = "Alsh" };
    edict_t *caster = make_hero(MAKEFOURCC('o','s','h','m'), 300, 300, 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
    target->s.player = caster->s.player;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500;
    caster->data.UnitAbilities = &abilities;

#if BZ_COMPAT_WORKAROUNDS
    T_ASSERT(S_CastUnitTargetSpell(caster, FS_SLKKey("Alsh"), target));
    T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','l','s','h')));
#else
    T_ASSERT(!S_CastUnitTargetSpell(caster, FS_SLKKey("Alsh"), target));
    T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','l','s','h')));
#endif

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, nightelf_passive_abilities_are_registered_with_correct_handlers) {
	InitAbilities();
	ability_t const *vengeance = FindAbilityByClassname("Avng");
	ability_t const *moon_glaive = FindAbilityByClassname("Amgl");
	ability_t const *moon_glaive_r = FindAbilityByClassname("Amgr");
	ability_t const *slow_poison = FindAbilityByClassname("Aspo");
	T_NOT_NULL(vengeance); T_NOT_NULL(moon_glaive); T_NOT_NULL(moon_glaive_r); T_NOT_NULL(slow_poison);
	T_EQ(vengeance->proc, CAbilityVengeance); T_ASSERT(vengeance->flags & AB_SPELL);
	T_EQ((int)vengeance->target_type, (int)SPELL_TARGET_UNIT);
	T_EQ(moon_glaive->proc, CAbilityMoonGlaive); T_ASSERT(moon_glaive->flags & AB_PASSIVE);
	T_EQ(moon_glaive_r->proc, CAbilityMoonGlaive); T_ASSERT(moon_glaive_r->flags & AB_PASSIVE);
	T_EQ(slow_poison->proc, CAbilitySlowPoison); T_ASSERT(slow_poison->flags & AB_PASSIVE);
}

TEST(wc3_spell, avatar_of_vengeance_spirit_raises_invulnerable_spirit_manually_and_autocasts) {
    const char ability_slk[] =
        "ID;PWXL;N;EBB;Y2;X11\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs1\"\nC;Y1;X5;K\"Rng1\"\nC;Y1;X6;K\"Cost1\"\n"
        "C;Y1;X7;K\"Cool1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"BuffID1\"\n"
        "C;Y1;X10;K\"DataC1\"\nC;Y1;X11;K\"DataF1\"\n"
        "C;Y2;X1;K\"Avng\"\nC;Y2;X2;K\"Avng\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K\"air,ground,dead\"\nC;Y2;X5;K600\nC;Y2;X6;K0\n"
        "C;Y2;X7;K0\nC;Y2;X8;K50\nC;Y2;X9;K\"Bvng\"\n"
        "C;Y2;X10;K\"even\"\nC;Y2;X11;K1\nE\n";
    static UnitAbilities_t const spirit_abilities = { .abilList = "Avng" };
    static UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(ability_slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *avatar = alloc_test_unit(MAKEFOURCC('e','s','p','v'), 0, 0);
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','g','r','u'), 128, 0);
    edict_t *raised = NULL;
    uint32_t raised_count = 0;
    ability_t const *ability = FindAbilityForCommand("Avng");
    abilityitem_t item = S_AbilityItem(MAKEFOURCC('A','v','n','g'));

    avatar->s.player = 0; avatar->data.UnitAbilities = &spirit_abilities;
    avatar->svflags |= SVF_MONSTER; avatar->health.value = avatar->health.max_value = 300;
    avatar->mana.value = avatar->mana.max_value = 300;
    first->s.player = second->s.player = 0;
    first->data.UnitData = second->data.UnitData = &corpse_data;
    first->svflags |= SVF_MONSTER; second->svflags |= SVF_MONSTER;
    first->targtype = second->targtype = TARG_GROUND;
    first->health.value = first->health.max_value = 100; first->die = unit_die;
    second->health.value = second->health.max_value = 100; second->die = unit_die;
    first->stand = second->stand = unit_stand;
    unit_die(first, avatar); unit_die(second, avatar);

    T_NOT_NULL(ability); T_ASSERT(ability->flags & AB_AUTOCAST);
    T_EQ((int)ability->target_type, (int)SPELL_TARGET_UNIT);
    T_ASSERT(G_UnitIsRaisableCorpse(first));
    T_ASSERT(G_UnitAbilityLevel(avatar, item.code) > 0);
    T_ASSERT(S_SpellAllowsTarget(item.code, avatar, first));
    T_ASSERT(G_SetUnitAutocast(avatar, item.code, true));
    T_ASSERT(G_UnitAutocastIsOn(avatar, item.code));

    T_ASSERT(S_CastUnitTargetSpell(avatar, item.code, first));
    T_ASSERT(!first->inuse);
    FILTER_EDICTS(unit, unit->inuse && unit->class_id == MAKEFOURCC('e','v','e','n') &&
                  unit->owner == avatar) { raised = unit; break; }
    T_NOT_NULL(raised);
    if (raised) {
        T_EQ(raised->class_id, MAKEFOURCC('e','v','e','n'));
        T_ASSERT(G_UnitHasFlag(raised, UNIT_FLAG_INVULNERABLE));
        T_EQ(raised->owner, avatar); T_EQ(raised->summon_ability, item.code);
    }

    T_ASSERT(test_ability_message(avatar, A_AUTOCAST_ACQUIRE, &item, NULL));
    T_ASSERT(!second->inuse);
    raised = NULL;
    FILTER_EDICTS(unit, unit->inuse && unit->class_id == MAKEFOURCC('e','v','e','n') &&
                  unit->owner == avatar && unit->summon_ability == item.code) {
        if (!raised) raised = unit;
        raised_count++;
    }
    T_NOT_NULL(raised);
    T_EQ(raised_count, 2u);
    if (raised) {
        T_EQ(raised->class_id, MAKEFOURCC('e','v','e','n'));
        T_ASSERT(G_UnitHasFlag(raised, UNIT_FLAG_INVULNERABLE));
        T_EQ(raised->owner, avatar); T_EQ(raised->summon_ability, item.code);
    }

    T_ASSERT(G_SetUnitAutocast(avatar, item.code, false));
    T_ASSERT(!G_UnitAutocastIsOn(avatar, item.code));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, slow_poison_on_hit_applies_buff_and_returns_authored_reduction) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X6\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y2;X1;K\"Aspo\"\nC;Y2;X2;K\"Aspo\"\nC;Y2;X3;K\"10\"\n"
		"C;Y2;X4;K\"5\"\nC;Y2;X5;K\"0.25\"\nC;Y2;X6;K\"0.15\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	level.time = 0;
	attacker->s.player = 0; target->s.player = 1;
	target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
	target->health.value = target->health.max_value = 200.0f;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','s','p','o'), .level = 1);
	T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','s','p','o')));
	T_FEQ(S_SlowPoisonMoveReduction(target), 0.0f, 0.001f);
	S_SlowPoisonOnHit(attacker, target);
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','s','p','o')));
	T_FEQ(S_SlowPoisonMoveReduction(target), 0.25f, 0.001f);
	T_FEQ(S_SlowPoisonAttackReduction(target), 0.15f, 0.001f);
	{
		bool target_art = false;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','s','p','o') &&
		              effect->goalentity == target) target_art = true;
		T_ASSERT(target_art);
	}
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, dryad_landed_attack_applies_slow_poison_status) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\nC;Y1;X7;K\"targs\"\n"
		"C;Y2;X1;K\"Aspo\"\nC;Y2;X2;K\"Aspo\"\nC;Y2;X3;K\"10\"\n"
		"C;Y2;X4;K\"5\"\nC;Y2;X5;K\"0.25\"\nC;Y2;X6;K\"0.15\"\nC;Y2;X7;K\"air,ground,organic\"\nE\n";
	static UnitAbilities_t const dryad_abilities = { .abilList = "Aadm,Amim,Aspo,Aien,Ault" };
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker, *target, *missile, *ally, *allied;

	reset_entities(); setup_test_world();
	attacker = alloc_test_unit(MAKEFOURCC('e','d','r','y'), 0, 0);
	attacker->data.UnitAbilities = &dryad_abilities;
	attacker->unit_state->attack1.type = ATK_PIERCE;
	attacker->unit_state->attack1.weapon = WPN_MISSILE;
	target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
	target->health.value = target->health.max_value = 200.0f;
	target->targtype = TARG_GROUND;
	attacker->s.player = 0; target->s.player = 1;
	attacker->svflags |= SVF_MONSTER; target->svflags |= SVF_MONSTER;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[2].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	level.alliances[0][2] = 1 << ALLIANCE_PASSIVE;
	missile = G_Spawn();
	missile->owner = attacker;
	missile->goalentity = target;
	missile->s.origin = target->s.origin;
	missile->velocity = 1.0f;
	missile->damage = 10;
	G_InitMissile(missile);
	missile->missile->attack_type = ATK_PIERCE;
	missile->missile->owner_spawn_time = attacker->spawn_time;

	T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','s','p','o')));
	SV_Physics_Toss(missile);
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','s','p','o')));
	T_STREQ(G_AbilityBuffArt(MAKEFOURCC('B','s','p','o')),
	        "ReplaceableTextures\\CommandButtons\\BTNSlowPoison.blp");
	T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','s','p','o'), WC3_EFFECT_TARGET, 0),
	        "TestUI\\Models\\anim_pulse.mdx");
	T_FEQ(S_SlowPoisonMoveReduction(target), 0.25f, 0.001f);
	T_FEQ(S_SlowPoisonAttackReduction(target), 0.15f, 0.001f);
	{
		bool target_art = false;
		FILTER_EDICTS(effect, effect->status_effect_code == MAKEFOURCC('B','s','p','o') &&
		              effect->goalentity == target) target_art = true;
		T_ASSERT(target_art);
	}
	ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
	ally->health.value = ally->health.max_value = 200.0f;
	ally->targtype = TARG_GROUND;
	ally->s.player = attacker->s.player;
	ally->svflags |= SVF_MONSTER;
	missile = G_Spawn();
	missile->owner = attacker;
	missile->goalentity = ally;
	missile->s.origin = ally->s.origin;
	missile->velocity = 1.0f;
	missile->damage = 10;
	G_InitMissile(missile);
	missile->missile->attack_type = ATK_PIERCE;
	missile->missile->owner_spawn_time = attacker->spawn_time;
	T_ASSERT(!S_UnitHasStatus(ally, MAKEFOURCC('B','s','p','o')));
	SV_Physics_Toss(missile);
	T_ASSERT(S_UnitHasStatus(ally, MAKEFOURCC('B','s','p','o')));
	allied = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
	allied->health.value = allied->health.max_value = 200.0f;
	allied->targtype = TARG_GROUND;
	allied->s.player = 2;
	allied->svflags |= SVF_MONSTER;
	missile = G_Spawn();
	missile->owner = attacker;
	missile->goalentity = allied;
	missile->s.origin = allied->s.origin;
	missile->velocity = 1.0f;
	missile->damage = 10;
	G_InitMissile(missile);
	missile->missile->attack_type = ATK_PIERCE;
	missile->missile->owner_spawn_time = attacker->spawn_time;
	T_ASSERT(!S_UnitHasStatus(allied, MAKEFOURCC('B','s','p','o')));
	SV_Physics_Toss(missile);
	T_ASSERT(S_UnitHasStatus(allied, MAKEFOURCC('B','s','p','o')));
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* HeroDur is 1s vs Dur 10s; DataB/C stay fractions. */
TEST(wc3_spell, slow_poison_uses_herodur_and_fraction_data) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"DataC1\"\n"
		"C;Y2;X1;K\"Aspo\"\nC;Y2;X2;K\"Aspo\"\nC;Y2;X3;K\"10\"\n"
		"C;Y2;X4;K\"1\"\nC;Y2;X5;K\"4\"\nC;Y2;X6;K\"0.5\"\nC;Y2;X7;K\"0.25\"\nE\n";
	static UnitBalance_t hero_bal;
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
	edict_t *hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 50, 0);
	level.time = 1000;
	attacker->s.player = 0; hero->s.player = 1;
	hero->svflags |= SVF_MONSTER; hero->targtype = TARG_GROUND;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	memset(&hero_bal, 0, sizeof(hero_bal));
	hero_bal.strength = 20; hero_bal.maxHealth = 400;
	hero->data.UnitBalance = &hero_bal;
	hero->health.value = hero->health.max_value = 400;
	attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','s','p','o'), .level = 1);
	S_SlowPoisonOnHit(attacker, hero);
	T_ASSERT(S_UnitHasStatus(hero, MAKEFOURCC('B','s','p','o')));
	T_FEQ(S_SlowPoisonMoveReduction(hero), 0.5f, 0.001f);
	T_FEQ(S_SlowPoisonAttackReduction(hero), 0.25f, 0.001f);
	level.time += 1000; unit_updatestatuses(hero);
	T_ASSERT(!S_UnitHasStatus(hero, MAKEFOURCC('B','s','p','o')));
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ACvs shares code=Aven, so the Aven row repairs its dispatch like Anhe/ACtc repair Anh1/ACt2. */
TEST(wc3_spell, poison_attacks_share_poison_procedure) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y3;X2\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y2;X1;K\"Aven\"\nC;Y2;X2;K\"Aven\"\n"
		"C;Y3;X1;K\"ACvs\"\nC;Y3;X2;K\"Aven\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	T_EQ(S_AbilityItem(MAKEFOURCC('A','v','e','n')).ability->proc, CAbilityPoisonAttack);
	T_EQ(S_AbilityItem(MAKEFOURCC('A','p','o','i')).ability->proc, CAbilityPoisonAttack);
	T_EQ(S_AbilityItem(MAKEFOURCC('A','p','o','2')).ability->proc, CAbilityPoisonAttack);
	T_EQ(S_AbilityItem(MAKEFOURCC('A','C','v','s')).ability->proc, CAbilityPoisonAttack);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* ROC omits poison BuffID; fallbacks apply the Bpoi/Bpsd and BIpb/BIpd pairs
 * with authored durations. Covers native ownership and a held Apo2 item; the
 * item also carries Aven, so the same poison from both sources applies once. */
TEST(wc3_spell, poison_on_hit_applies_buff_pair_with_authored_duration) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
		"C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataD1\"\n"
		"C;Y1;X7;K\"DataC1\"\n"
		"C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"0\"\n"
		"C;Y2;X4;K\"0\"\nC;Y2;X5;K\"6\"\nC;Y2;X6;K\"0\"\nC;Y2;X7;K\"1\"\n"
		"C;Y3;X1;K\"Aven\"\nC;Y3;X2;K\"Aven\"\nC;Y3;X3;K\"7\"\n"
		"C;Y3;X4;K\"2\"\nC;Y3;X5;K\"6\"\nC;Y3;X6;K\"0\"\n"
		"C;Y4;X1;K\"Apo2\"\nC;Y4;X2;K\"Apo2\"\nC;Y4;X3;K\"6\"\n"
		"C;Y4;X4;K\"6\"\nC;Y4;X5;K\"9\"\nC;Y4;X6;K\"1\"\nE\n";
	const char items[] =
		"ID;PWXL;N;EBB;Y2;X2\n"
		"C;Y1;X1;K\"itemID\"\nC;Y1;X2;K\"abilList\"\n"
		"C;Y2;X1;K\"orpv\"\nC;Y2;X2;K\"Aven,Apo2\"\nE\n";
	UnitAbilities_t abilities = { .abilList = "AInv,Aven" };
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	slkTestData_t *idata = parse_slk_string(items), *olditem = G_SetSLKRows("ItemData", idata);
	edict_t *attacker = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 300, 0);
	edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *hero = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 90, 0);
	edict_t *item = alloc_test_unit(MAKEFOURCC('o','r','p','v'), 32, 0);
	level.time = 0;
	attacker->data.UnitAbilities = &abilities; attacker->s.player = 0;
	attacker->svflags |= SVF_MONSTER;
	target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
	hero->s.player = 1; hero->svflags |= SVF_MONSTER;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	item->targtype = TARG_ITEM;
	G_AddItem(item);
	G_GetItem(item)->in_world = true;
	G_GetItem(item)->inventory_slot = -1;
	T_ASSERT(G_AddItemToSlot(attacker, item, 0));
	S_PoisonOnHit(attacker, target);
	S_PoisonOnHit(attacker, hero);
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','p','o','i')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','p','s','d')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','I','p','b')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','I','p','d')));
	{
		static uint32_t const physical_buffs[] = {
			MAKEFOURCC('B','p','o','i'), MAKEFOURCC('B','p','s','d'),
			MAKEFOURCC('B','I','p','b'), MAKEFOURCC('B','I','p','d')
		};
		FOR_LOOP(i, sizeof(physical_buffs) / sizeof(physical_buffs[0])) {
			heroabilitystatus_t *slot = unit_findstatus(target, physical_buffs[i]);
			T_NOT_NULL(slot);
			T_ASSERT(slot->buff_flags & WC3_STATUS_BUFF_PHYSICAL);
			T_ASSERT(!(slot->buff_flags & WC3_STATUS_BUFF_MAGICAL));
			T_ASSERT(!unit_status_can_magic_dispel(slot));
		}
	}
	T_ASSERT(S_UnitHasStatus(hero, MAKEFOURCC('B','p','o','i')));
	level.time += 3000; unit_updatestatuses(target); unit_updatestatuses(hero);
	T_ASSERT(!S_UnitHasStatus(hero, MAKEFOURCC('B','p','o','i')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','p','o','i')));
	T_ASSERT(S_UnitHasStatus(target, MAKEFOURCC('B','I','p','b')));
	level.time += 5000; unit_updatestatuses(target);
	T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','p','o','i')));
	T_ASSERT(!S_UnitHasStatus(target, MAKEFOURCC('B','I','p','b')));
	G_SetSLKRows("ItemData", olditem); free_slk_rows(idata);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, poison_dataa_ticks_through_entity_scheduler_and_preserves_same_source_phase) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"BuffID1\"\n"
        "C;Y2;X1;K\"Aven\"\nC;Y2;X2;K\"Aven\"\nC;Y2;X3;K\"4\"\n"
        "C;Y2;X4;K\"4\"\nC;Y2;X5;K\"7\"\nC;Y2;X6;K\"Bpoi,Bpsd\"\nE\n";
    static UnitBalance_t target_balance;
    static UnitWeapons_t no_weapons;
    UnitAbilities_t abilities = { .abilList = "Aven" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *attacker = alloc_test_unit(MAKEFOURCC('e','a','r','c'), 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 32, 0);
    float before;

    memset(&target_balance, 0, sizeof(target_balance));
    level.time = 0;
    attacker->data.UnitAbilities = &abilities;
    attacker->s.player = 0; attacker->spawn_time = 1;
    target->data.UnitBalance = &target_balance;
    target->data.UnitWeapons = &no_weapons;
    target->s.player = 1; target->svflags |= SVF_MONSTER; target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 100.0f;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));

    S_ResolveAttackHit(attacker, target, 1);
    before = target->health.value;
    level.time = 999; G_RunEntity(target);
    T_FEQ(target->health.value, before, 0.001f);
    level.time = 1000; G_RunEntity(target);
    T_FEQ(target->health.value, before - 7.0f, 0.001f);

    level.time = 1500; S_ResolveAttackHit(attacker, target, 1);
    level.time = 1999; G_RunEntity(target);
    T_FEQ(target->health.value, before - 8.0f, 0.001f);
    level.time = 2000; G_RunEntity(target);
    T_FEQ(target->health.value, before - 15.0f, 0.001f);

    level.time = 5499; G_RunEntity(target);
    T_FEQ(target->health.value, before - 36.0f, 0.001f);
    level.time = 5500; G_RunEntity(target);
    T_FEQ(target->health.value, before - 36.0f, 0.001f);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, ethereal_secondary_targets_reject_physical_attacks) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y4;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Area1\"\n"
        "C;Y2;X1;K\"Amgl\"\nC;Y2;X2;K\"Amgl\"\nC;Y2;X3;K\"2\"\nC;Y2;X4;K\"200\"\n"
        "C;Y3;X1;K\"ANca\"\nC;Y3;X2;K\"ANca\"\nC;Y3;X3;K\"0.4\"\nC;Y3;X4;K\"200\"\n"
        "C;Y4;X1;K\"Asth\"\nC;Y4;X2;K\"Asth\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    uint32_t codes[] = { MAKEFOURCC('A','m','g','l'), MAKEFOURCC('A','N','c','a'), MAKEFOURCC('A','s','t','h') };
    FOR_LOOP(i, 3) {
        edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
        edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
        edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 60, 0);
        attacker->s.player = 0; primary->s.player = nearby->s.player = 1;
        attacker->unit_state->attack1.type = ATK_NORMAL;
        /* Cleave is a melee passive; the shared ranged Huntress fixture
         * must use a melee weapon in the Cleave iteration. */
        if (i == 1) attacker->unit_state->attack1.weapon = WPN_NORMAL;
        attacker->unit_state->attack1.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
        attacker->unit_state->attack2.type = ATK_MAGIC;
        attacker->unit_state->attack2.targetsAllowed = G_TargetFlagForType(TARG_GROUND);
        attacker->unit_state->attack1.areaFull = attacker->unit_state->attack1.areaMedium = attacker->unit_state->attack1.areaSmall = 200;
        primary->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER;
        primary->targtype = nearby->targtype = TARG_GROUND;
        ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
        ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
        memset(level.alliances, 0, sizeof(level.alliances));
        primary->health.value = primary->health.max_value = 500;
        nearby->health.value = nearby->health.max_value = 500;
        attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = codes[i], .level = 1);
        heroabilitystatus_t *slot = S_SpellApplyTimedStatus(nearby, "BHbn", 1, 10);
        T_NOT_NULL(slot);
        T_ASSERT(unit_hasstatusstate(nearby, WC3_STATUS_STATE_ETHEREAL));
        S_ResolveAttackHit(attacker, primary, 50);
        T_FEQ(primary->health.value, 450, 0.001f);
        T_FEQ(nearby->health.value, 500, 0.001f);
        unit_expirestatus(nearby, slot);
        S_ResolveAttackHit(attacker, primary, 50);
        T_FEQ(primary->health.value, 400, 0.001f);
        T_FEQ(nearby->health.value, i == 0 ? 475 : (i == 1 ? 480 : 450), 0.001f);
        T_NOT_NULL(S_SpellApplyTimedStatus(nearby, "BHbn", 1, 10));
        attacker->unit_state->attack1.type = ATK_MAGIC;
        S_ResolveAttackHit(attacker, primary, 50);
        T_FEQ(primary->health.value, 350, 0.001f);
        T_FEQ(nearby->health.value, i == 0 ? 450 : (i == 1 ? 460 : 400), 0.001f);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, moon_glaive_bounces_attack_to_nearby_enemy) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X4\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"DataA1\"\nC;Y1;X4;K\"Area1\"\n"
		"C;Y2;X1;K\"Amgl\"\nC;Y2;X2;K\"Amgl\"\nC;Y2;X3;K\"2\"\nC;Y2;X4;K\"200\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
	edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *far_away = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	attacker->s.player = 0;
	primary->s.player = nearby->s.player = far_away->s.player = 1;
	primary->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER; far_away->svflags |= SVF_MONSTER;
	primary->targtype = nearby->targtype = far_away->targtype = TARG_GROUND;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	primary->health.value = primary->health.max_value = 500.0f;
	nearby->health.value = nearby->health.max_value = 500.0f;
	far_away->health.value = far_away->health.max_value = 500.0f;
	primary->die = nearby->die = far_away->die = unit_die;
	primary->s.origin2.x = 0; primary->s.origin2.y = 0;
	nearby->s.origin2.x = 50; nearby->s.origin2.y = 0;
	far_away->s.origin2.x = 5000; far_away->s.origin2.y = 0;
	attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','m','g','l'), .level = 1);
	S_MoonGlaiveAttack(attacker, primary, 50);
	T_FEQ(nearby->health.value, 475.0f, 0.001f);
	T_FEQ(far_away->health.value, 500.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Stock Amgl DataA/Area are 0: one bounce without Remg, 50% damage loss. */
TEST(wc3_spell, moon_glaive_stock_zeros_bounce_inside_attack_range) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X2\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y2;X1;K\"Amgl\"\nC;Y2;X2;K\"Amgl\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *attacker = make_hero(MAKEFOURCC('e','h','u','n'), 300, 0, 0, 0);
	edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *nearby = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	edict_t *far_away = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
	attacker->s.player = 0;
	primary->s.player = nearby->s.player = far_away->s.player = 1;
	primary->svflags |= SVF_MONSTER; nearby->svflags |= SVF_MONSTER; far_away->svflags |= SVF_MONSTER;
	primary->targtype = nearby->targtype = far_away->targtype = TARG_GROUND;
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	memset(level.alliances, 0, sizeof(level.alliances));
	primary->health.value = primary->health.max_value = 500.0f;
	nearby->health.value = nearby->health.max_value = 500.0f;
	far_away->health.value = far_away->health.max_value = 500.0f;
	primary->s.origin2.x = 0; primary->s.origin2.y = 0;
	nearby->s.origin2.x = 50; nearby->s.origin2.y = 0;
	far_away->s.origin2.x = 5000; far_away->s.origin2.y = 0;
	attacker->unit_state->attack1.range = 200;
	attacker->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','m','g','l'), .level = 1);
	S_MoonGlaiveAttack(attacker, primary, 50);
	T_FEQ(nearby->health.value, 475.0f, 0.001f);
	T_FEQ(far_away->health.value, 500.0f, 0.001f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Remg adds a second bounce; the original is always active when the Huntress
 * has Amgl. UnitBalance Upgrades Used and player tech ownership are required. */
TEST(wc3_spell, moon_glaive_research_grants_and_revokes_second_bounce) {
    UnitBalance_t huntress_balance = { .upgrades = "Remg" };
    edict_t *huntress = make_hero(MAKEFOURCC('e','h','u','n'), 500, 0, 0, 0);
    edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 120, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 240, 0);
    edict_t *out_of_range = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 1000, 0);
    edict_t *targets[] = { primary, first, second, out_of_range };
    huntress->data.UnitBalance = &huntress_balance;
    huntress->s.player = 0;
    huntress->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','m','g','l'), .level = 1);
    huntress->unit_state->attack1.weapon = WPN_MBOUNCE;
    huntress->unit_state->attack1.maxTargets = 2;
    huntress->unit_state->attack1.damageLoss = 0.5f;
    huntress->unit_state->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    FOR_LOOP(i, 4) {
        targets[i]->s.player = 1;
        targets[i]->svflags |= SVF_MONSTER;
        targets[i]->targtype = TARG_GROUND;
        targets[i]->health.value = targets[i]->health.max_value = 500;
    }
    /* Before research, attack 50 -> primary, first bounce 25. */
    S_MoonGlaiveAttack(huntress, primary, 50);
    T_FEQ(first->health.value, 475, 0.001f);
    T_FEQ(second->health.value, 500, 0.001f);
    T_FEQ(out_of_range->health.value, 500, 0.001f);

    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','e','m','g'), 1);
    T_EQ(G_MoonGlaiveResearchLevel(huntress), 1);
    first->health.value = second->health.value = 500;
    S_MoonGlaiveAttack(huntress, primary, 50);
    T_FEQ(first->health.value, 475, 0.001f);
    T_FEQ(second->health.value, 488, 0.001f); /* floor(50 * 0.5 * 0.5) */
    T_FEQ(out_of_range->health.value, 500, 0.001f);

    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','e','m','g'), 0);
    T_EQ(G_MoonGlaiveResearchLevel(huntress), 0);
    first->health.value = second->health.value = 500;
    S_MoonGlaiveAttack(huntress, primary, 50);
    T_FEQ(first->health.value, 475, 0.001f);
    T_FEQ(second->health.value, 500, 0.001f);
}

/* Naisha's Amgr doesn't consume Remg; custom units opt out without the
 * Upgrades Used entry. An authored utc1=1 means no bounce before research. */
TEST(wc3_spell, moon_glaive_custom_targets_and_no_research_variant) {
    UnitBalance_t huntress_balance = { .upgrades = "Remg" };
    edict_t *huntress = make_hero(MAKEFOURCC('e','h','u','n'), 500, 0, 0, 0);
    edict_t *primary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *secondary = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 50, 0);
    huntress->data.UnitBalance = &huntress_balance;
    huntress->s.player = 0;
    primary->s.player = secondary->s.player = 1;
    primary->svflags |= SVF_MONSTER; secondary->svflags |= SVF_MONSTER;
    primary->targtype = secondary->targtype = TARG_GROUND;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    secondary->health.value = secondary->health.max_value = 500;
    huntress->unit_state->attack1.maxTargets = 1;
    huntress->unit_state->attack1.damageLoss = 0.25f;
    huntress->unit_state->ranks[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','m','g','l'), .level = 1);
    S_MoonGlaiveAttack(huntress, primary, 40);
    T_FEQ(secondary->health.value, 500, 0.001f);
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','e','m','g'), 1);
    S_MoonGlaiveAttack(huntress, primary, 40);
    T_FEQ(secondary->health.value, 470, 0.001f); /* 25% lost */
    huntress->unit_state->ranks[0].code = MAKEFOURCC('A','m','g','r');
    secondary->health.value = 500;
    S_MoonGlaiveAttack(huntress, primary, 40);
    T_FEQ(secondary->health.value, 500, 0.001f); /* Amgr ignores Remg */
    G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','e','m','g'), 0);
}

TEST(wc3_spell, healing_ward_records_concrete_summon_ability) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X5\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"Dur1\"\nC;Y1;X4;K\"UnitID1\"\nC;Y1;X5;K\"targs\"\n"
        "C;Y2;X1;K\"Ahwd\"\nC;Y2;X2;K\"Ahwd\"\n"
        "C;Y2;X3;K\"7\"\nC;Y2;X4;K\"hfoo\"\nC;Y2;X5;K\"point\"\nE\n";
    uint32_t const code = MAKEFOURCC('A','h','w','d');
    slkTestData_t *rows, *old;
    edict_t *caster, *ward = NULL;
    spellTarget_t target = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = { 128.0f, 64.0f });

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    caster->s.player = 0; caster->svflags |= SVF_MONSTER;
    T_ASSERT(test_execute_code(caster, "Ahwd", target));
    FILTER_EDICTS(unit, unit->inuse && unit->owner == caster && unit->class_id == MAKEFOURCC('h','f','o','o')) {
        ward = unit; break;
    }
    T_NOT_NULL(ward);
    if (ward) {
        T_EQ(ward->summon_ability, code);
        T_EQ(G_UnitStatusLevel(ward, MAKEFOURCC('B','T','L','F')), 1);
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, purge_and_lightning_shield_registration_aliases) {
	T_EQ(S_AbilityItem(FS_SLKKey("Aprg")).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(FS_SLKKey("Apg2")).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(FS_SLKKey("AIlp")).ability->proc, CAbilityPurge);
	T_EQ(S_AbilityItem(FS_SLKKey("Alsh")).ability->proc, CAbilityLightningShield);
	T_EQ(S_AbilityItem(FS_SLKKey("Ahwd")).ability->proc, CAbilityHealingWard);
	T_EQ(S_AbilityItem(FS_SLKKey("Aoar")).ability->proc, CAbilityAuraRegenLife);
	T_EQ(S_AbilityItem(FS_SLKKey("Aabr")).ability->proc, CAbilityAuraRegenLife);
	T_EQ(S_AbilityItem(FS_SLKKey("Arpb")).ability->proc, CAbilityReplenish);
	T_EQ(S_AbilityItem(FS_SLKKey("Arpl")).ability->proc, CAbilityReplenishLife);
	T_EQ(S_AbilityItem(FS_SLKKey("Arpm")).ability->proc, CAbilityReplenishMana);
	T_EQ(S_AbilityItem(FS_SLKKey("Arai")).ability->proc, CAbilityRaiseDead);
	T_EQ(S_AbilityItem(FS_SLKKey("AIrd")).ability->proc, CAbilityRaiseDead);
}

TEST(wc3_spell, replenish_restores_health_and_mana_essence_heals_area_spirit_touch_restores_mana) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y4;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"Rng1\"\nC;Y1;X4;K\"Area1\"\n"
		"C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\nC;Y1;X7;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Arpb\"\nC;Y2;X2;K\"Arpb\"\nC;Y2;X3;K\"500\"\n"
		"C;Y2;X5;K\"100\"\nC;Y2;X6;K\"50\"\nC;Y2;X7;K\"Brpb\"\n"
		"C;Y3;X1;K\"Arpl\"\nC;Y3;X2;K\"Arpl\"\nC;Y3;X4;K\"600\"\n"
		"C;Y3;X5;K\"80\"\n"
		"C;Y4;X1;K\"Arpm\"\nC;Y4;X2;K\"Arpm\"\nC;Y4;X4;K\"600\"\n"
		"C;Y4;X6;K\"40\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
	edict_t *ally = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 50, 0);
	abilityitem_t rpb = S_AbilityItem(FS_SLKKey("Arpb"));
	abilityitem_t rpl = S_AbilityItem(FS_SLKKey("Arpl"));
	abilityitem_t rpm = S_AbilityItem(FS_SLKKey("Arpm"));
	caster->s.player = ally->s.player = 0;
	ally->svflags |= SVF_MONSTER;
	ally->health.value = 60; ally->health.max_value = 200;
	ally->mana.value = 10; ally->mana.max_value = 200;
	T_EQ(rpb.ability->proc, CAbilityReplenish); T_ASSERT(rpb.ability->flags & AB_AUTOCAST);
	T_EQ(rpl.ability->proc, CAbilityReplenishLife); T_ASSERT(rpl.ability->flags & AB_AUTOCAST);
	T_EQ(rpm.ability->proc, CAbilityReplenishMana); T_ASSERT(rpm.ability->flags & AB_AUTOCAST);
	test_execute_code(caster, "Arpb", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally));
	T_FEQ(ally->health.value, 160.0f, 0.001f);
	T_FEQ(ally->mana.value, 60.0f, 0.001f);
	ally->health.value = 50; ally->health.max_value = 200;
	test_execute_code(caster, "Arpl", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(ally->health.value > 50.0f);
	ally->mana.value = 5; ally->mana.max_value = 200;
	test_execute_code(caster, "Arpm", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(ally->mana.value > 5.0f);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, raise_dead_uses_two_authored_summon_groups_and_marks_summons) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X10\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Dur1\"\n"
		"C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
		"C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"DataD1\"\n"
		"C;Y1;X9;K\"BuffID1\"\nC;Y1;X10;K\"UnitID1\"\n"
		"C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\n"
		"C;Y2;X3;K\"dead\"\nC;Y2;X4;K\"30\"\n"
		"C;Y2;X5;K\"1\"\nC;Y2;X6;K\"1\"\n"
		"C;Y2;X7;K\"uske\"\nC;Y2;X8;K\"hfoo\"\n"
		"C;Y2;X9;K\"Brai\"\nC;Y2;X10;K\"ogru\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
	edict_t *corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100, 0);
	UnitData_t corpse_data = { .deathType = 3 };
	edict_t *warrior = NULL, *second = NULL;
	abilityitem_t rai = S_AbilityItem(FS_SLKKey("Arai"));
	caster->s.player = 0; caster->runtime.acquisition_range = 800.0f;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
	g_edicts[0].client = &game.clients[0]; game.clients[0].connected = true; game.clients[0].ps.number = 0;
	corpse->data.UnitData = &corpse_data;
	corpse->health.value = 0; corpse->health.max_value = 100;
    corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
	T_EQ(rai.ability->proc, CAbilityRaiseDead); T_ASSERT(rai.ability->flags & AB_AUTOCAST);
	T_ASSERT(G_UnitIsRaisableCorpse(corpse));
	test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(!corpse->inuse);
	FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == FS_SLKKey("Arai")) {
		if (unit->class_id == MAKEFOURCC('u','s','k','e')) warrior = unit;
		if (unit->class_id == MAKEFOURCC('h','f','o','o')) second = unit;
	}
	T_NOT_NULL(warrior); T_NOT_NULL(second);
	T_EQ(G_UnitStatusLevel(warrior, MAKEFOURCC('B','r','a','i')), 1);
	T_EQ(G_UnitStatusLevel(second, MAKEFOURCC('B','r','a','i')), 1);
	T_ASSERT(!S_CastNoTargetSpell(caster, FS_SLKKey("Arai")));
	T_STREQ(G_ResolveCommandErrorText(&game.clients[0], "Cantfindcorpse"), "There are no usable corpses nearby.");
	T_STREQ(game.clients[0].message.text, "");
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

static cstring_t spell_tft_expansion_cvar(cstring_t name, cstring_t fallback) {
    return !strcmp(name, "fs_expansion") ? "1" : fallback;
}

TEST(wc3_spell, tft_skeletal_mastery_changes_raise_dead_second_summon_to_mage) {
    static UnitAbilities_t const abilities = { .abilList = "Arai" };
    static UnitBalance_t const balance = { .upgrades = "Rusm" };
    static UnitData_t const corpse_data = { .deathType = 3 };
    static cstring_t const ability_slk =
        "ID;PWXL;N;EBB;Y2;X18\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
        "C;Y1;X4;K\"levels\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"Dur2\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\n"
        "C;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"DataD1\"\n"
        "C;Y1;X11;K\"DataA2\"\nC;Y1;X12;K\"DataB2\"\n"
        "C;Y1;X13;K\"DataC2\"\nC;Y1;X14;K\"DataD2\"\n"
        "C;Y1;X15;K\"BuffID1\"\nC;Y1;X16;K\"BuffID2\"\n"
        "C;Y1;X17;K\"UnitID1\"\nC;Y1;X18;K\"UnitID2\"\n"
        "C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\n"
        "C;Y2;X3;K\"dead\"\nC;Y2;X4;K2\nC;Y2;X5;K30\nC;Y2;X6;K30\n"
        "C;Y2;X7;K2\nC;Y2;X8;K0\nC;Y2;X9;K\"uske\"\n"
        "C;Y2;X10;K\"-\"\nC;Y2;X11;K1\nC;Y2;X12;K1\n"
        "C;Y2;X13;K\"uske\"\nC;Y2;X14;K\"uskm\"\n"
        "C;Y2;X15;K\"Brai\"\nC;Y2;X16;K\"Brai\"\n"
        "C;Y2;X17;K\"uske\"\nC;Y2;X18;K\"uske\"\nE\n";
    static cstring_t const upgrade_slk =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"upgradeid\"\nC;Y1;X2;K\"effect1\"\n"
        "C;Y1;X3;K\"code1\"\nC;Y1;X4;K\"maxlevel\"\n"
        "C;Y2;X1;K\"Rusm\"\nC;Y2;X2;K\"rlev\"\n"
        "C;Y2;X3;K\"Arai\"\nC;Y2;X4;K1\nE\n";
    cstring_t (*previous_cvar)(cstring_t, cstring_t) = gi.CvarString;
    uint32_t const rai = MAKEFOURCC('A','r','a','i');
    uint32_t const mastery = MAKEFOURCC('R','u','s','m');
    slkTestData_t *abilities_rows = parse_slk_string(ability_slk);
    slkTestData_t *upgrade_rows = parse_slk_string(upgrade_slk);
    slkTestData_t *old_abilities_rows, *old_upgrade_rows;
    abilityitem_t item = S_AbilityItem(rai);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    edict_t *caster, *corpse;
    uint32_t warriors = 0, mages = 0;

    reset_entities(); setup_test_world();
    old_abilities_rows = G_SetSLKRows("AbilityData", abilities_rows);
    old_upgrade_rows = G_SetSLKRows("UpgradeData", upgrade_rows);
    gi.CvarString = spell_tft_expansion_cvar;
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    G_SetPlayerTechMaxAllowed(&game.clients[0], mastery, 1);
    caster = alloc_test_unit(MAKEFOURCC('u','n','e','c'), 500, 500);
    caster->s.player = 0;
    caster->data.UnitAbilities = (UnitAbilities_t *)&abilities;
    caster->data.UnitBalance = (UnitBalance_t *)&balance;
    caster->runtime.acquisition_range = 800.0f;

    corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 600, 500);
    corpse->data.UnitData = (UnitData_t *)&corpse_data;
    corpse->health.value = 0; corpse->health.max_value = 100;
    corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
    T_ASSERT(S_AbilityMessage(caster, A_EXECUTE, &call));
    FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == rai) {
        if (unit->class_id == MAKEFOURCC('u','s','k','e')) warriors++;
        if (unit->class_id == MAKEFOURCC('u','s','k','m')) mages++;
    }
    T_EQ(warriors, 2u);
    T_EQ(mages, 0u);

    G_SetPlayerTechResearched(&game.clients[0], mastery, 1);
    T_EQ(G_UnitAbilityLevel(caster, rai), 2u);
    /* A map-created unit can retain a stale runtime rank; researched TFT
     * mastery must still select the authored Warrior + Mage groups. */
    T_EQ(G_UnitSetAbilityLevel(caster, rai, 1), 1u);
    corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 620, 500);
    corpse->data.UnitData = (UnitData_t *)&corpse_data;
    corpse->health.value = 0; corpse->health.max_value = 100;
    corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
    T_ASSERT(S_AbilityMessage(caster, A_EXECUTE, &call));
    warriors = mages = 0;
    FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == rai) {
        if (unit->class_id == MAKEFOURCC('u','s','k','e')) warriors++;
        if (unit->class_id == MAKEFOURCC('u','s','k','m')) mages++;
    }
    T_EQ(warriors, 3u);
    T_EQ(mages, 1u);

    G_SetSLKRows("UpgradeData", old_upgrade_rows);
    G_SetSLKRows("AbilityData", old_abilities_rows);
    free_slk_rows(upgrade_rows); free_slk_rows(abilities_rows);
    gi.CvarString = previous_cvar;
}

TEST(wc3_spell, raise_dead_roc_missing_unit_id_spawns_authored_skeleton_count) {
#if BZ_COMPAT_WORKAROUNDS
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
		"C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\n"
		"C;Y1;X6;K\"DataC1\"\nC;Y1;X7;K\"BuffID1\"\n"
		"C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\n"
		"C;Y2;X3;K\"dead\"\nC;Y2;X4;K\"40\"\nC;Y2;X5;K\"2\"\n"
		"C;Y2;X6;K\"-\"\nC;Y2;X7;K\"-\"\nE\n";
	slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
	static UnitData_t corpse_data = { .deathType = 3 };
	edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
	edict_t *corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100, 0);
	uint32_t skeleton_count = 0;
	caster->s.player = 0;
	caster->runtime.acquisition_range = 800.0f;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
	g_edicts[0].client = &game.clients[0];
	game.clients[0].connected = true;
	game.clients[0].ps.number = 0;
	corpse->data.UnitData = &corpse_data;
	corpse->health.value = 0; corpse->health.max_value = 100;
	corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;

	test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	T_ASSERT(!corpse->inuse);
	FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == FS_SLKKey("Arai") &&
	              unit->class_id == MAKEFOURCC('u','s','k','e')) {
		heroabilitystatus_t *life = unit_findstatus(unit, MAKEFOURCC('B','T','L','F'));
		T_NOT_NULL(life);
		T_EQ(life->duration_ms, 40000u);
		T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','r','a','i')), 1u);
		skeleton_count++;
	}
	T_EQ(skeleton_count, 2u);
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
#else
	T_ASSERT(true); /* This classic-data fallback is intentionally compatibility-gated. */
#endif
}

TEST(wc3_spell, skeletal_longevity_extends_raise_dead_timed_life) {
	const char ability_slk[] =
		"ID;PWXL;N;EBB;Y2;X8\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
		"C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"Dur1\"\n"
		"C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataC1\"\n"
		"C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"UnitID1\"\n"
		"C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\n"
		"C;Y2;X3;K\"dead\"\nC;Y2;X4;K\"30\"\n"
		"C;Y2;X5;K\"1\"\nC;Y2;X6;K\"uske\"\n"
		"C;Y2;X7;K\"Brai\"\nC;Y2;X8;K\"ogru\"\nE\n";
	const char upgrade_slk[] =
		"ID;PWXL;N;EBB;Y2;X5\n"
		"C;Y1;X1;K\"upgradeid\"\nC;Y1;X2;K\"effect1\"\n"
		"C;Y1;X3;K\"base1\"\nC;Y1;X4;K\"mod1\"\n"
		"C;Y1;X5;K\"maxlevel\"\n"
		"C;Y2;X1;K\"Rusl\"\nC;Y2;X2;K\"rrai\"\n"
		"C;Y2;X3;K\"15\"\nC;Y2;X4;K\"0\"\nC;Y2;X5;K\"1\"\nE\n";
	slkTestData_t *ability_rows, *old_ability, *upgrade_rows, *old_upgrade;
	static UnitBalance_t balance = { .upgrades = "Rusl" };
	static UnitData_t corpse_data = { .deathType = 3 };
	edict_t *caster;
	edict_t *corpse, *summon = NULL;
	heroabilitystatus_t *timed_life;
	uint32_t base_duration_ms;

	reset_entities(); setup_test_world();
	ability_rows = parse_slk_string(ability_slk); old_ability = G_SetSLKRows("AbilityData", ability_rows);
	upgrade_rows = parse_slk_string(upgrade_slk); old_upgrade = G_SetSLKRows("UpgradeData", upgrade_rows);
	caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
	caster->s.player = 0;
	caster->runtime.acquisition_range = 800.0f;
	caster->data.UnitBalance = &balance;
	caster->unit_state->ranks[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
	g_edicts[0].client = &game.clients[0];
	game.clients[0].connected = true;
	game.clients[0].ps.number = 0;

	corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100, 0);
	corpse->data.UnitData = &corpse_data;
	corpse->health.value = 0; corpse->health.max_value = 100;
	corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
	test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == FS_SLKKey("Arai") &&
	              unit->class_id == MAKEFOURCC('u','s','k','e')) summon = unit;
	T_NOT_NULL(summon);
	timed_life = unit_findstatus(summon, MAKEFOURCC('B','T','L','F'));
	T_NOT_NULL(timed_life);
	base_duration_ms = timed_life->duration_ms;
	T_EQ(base_duration_ms, 30000u);

	G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','s','l'), 1);
	corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100, 0);
	corpse->data.UnitData = &corpse_data;
	corpse->health.value = 0; corpse->health.max_value = 100;
	corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
	test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
	summon = NULL;
	FILTER_EDICTS(unit, unit->inuse && unit->summon_ability == FS_SLKKey("Arai") &&
	              unit->class_id == MAKEFOURCC('u','s','k','e')) {
		heroabilitystatus_t *status = unit_findstatus(unit, MAKEFOURCC('B','T','L','F'));
		if (status && status->duration_ms == base_duration_ms + 15000u) summon = unit;
	}
	T_NOT_NULL(summon);
	timed_life = unit_findstatus(summon, MAKEFOURCC('B','T','L','F'));
	T_NOT_NULL(timed_life);
	T_EQ(timed_life->duration_ms, base_duration_ms + 15000u);

	G_SetPlayerTechResearched(&game.clients[0], MAKEFOURCC('R','u','s','l'), 0);
	G_SetSLKRows("UpgradeData", old_upgrade); free_slk_rows(upgrade_rows);
	G_SetSLKRows("AbilityData", old_ability); free_slk_rows(ability_rows);
}


/* Retail Raise Dead spends the lowest-ranked corpse first even when a stronger
 * corpse is closer, preserving high-value remains for Animate Dead. */
TEST(wc3_spell, raise_dead_prefers_lower_ranked_corpse_before_distance) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"BuffID1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\nC;Y2;X3;K\"dead\"\n"
        "C;Y2;X4;K\"30\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"uske\"\n"
        "C;Y2;X7;K\"Brai\"\nC;Y2;X8;K\"1\"\nE\n";
    UnitData_t corpse_data = { .deathType = 3 };
    UnitBalance_t weak_balance = { .level = 1 }, strong_balance = { .level = 5 };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
    edict_t *strong = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 25, 0);
    edict_t *weak = alloc_test_unit(MAKEFOURCC('n','s','h','e'), 150, 0);

    caster->s.player = 0; caster->runtime.acquisition_range = 800.0f;
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
    strong->data.UnitData = weak->data.UnitData = &corpse_data;
    strong->data.UnitBalance = &strong_balance; weak->data.UnitBalance = &weak_balance;
    strong->health.value = weak->health.value = 0.0f;
    strong->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
    weak->svflags |= SVF_MONSTER | SVF_DEADMONSTER;

    test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_ASSERT(strong->inuse); T_ASSERT(!weak->inuse);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}


/* Raiu/UnitID is Raise Dead's authored unit type for the fixed retail limit
 * check.  Crossing 25 retires the oldest living summon of that type while the
 * newly raised unit survives. */
TEST(wc3_spell, raise_dead_unitid_limit_check_retires_oldest_summon) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X9\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"DataA1\"\n"
        "C;Y1;X7;K\"DataC1\"\nC;Y1;X8;K\"BuffID1\"\nC;Y1;X9;K\"UnitID1\"\n"
        "C;Y2;X1;K\"Arai\"\nC;Y2;X2;K\"Arai\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"dead\"\nC;Y2;X5;K\"30\"\nC;Y2;X6;K\"1\"\n"
        "C;Y2;X7;K\"hfoo\"\nC;Y2;X8;K\"Brai\"\nC;Y2;X9;K\"hfoo\"\nE\n";
    UnitData_t corpse_data = { .deathType = 3 };
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster = make_hero(MAKEFOURCC('U','D','k','i'), 500, 500, 0, 0);
    edict_t *oldest = NULL, *corpse;
    uint32_t living = 0;

    /* make_hero resets the test world, so install the fixture afterward. */
    old = G_SetSLKRows("AbilityData", rows);
    caster->s.player = 0; caster->runtime.acquisition_range = 800.0f;
    caster->unit_state->ranks[0] = MAKE(heroability_t, .code = FS_SLKKey("Arai"), .level = 1);
    level.time = 10000;
    FOR_LOOP(i, 25) {
        edict_t *summon = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 300.0f + (float)i, 0.0f);
        summon->s.player = 0; summon->svflags |= SVF_MONSTER;
        summon->summon_ability = FS_SLKKey("Arai"); summon->spawn_time = 100 + i;
        if (i == 0) oldest = summon;
    }
    corpse = alloc_test_unit(MAKEFOURCC('u','d','e','a'), 100.0f, 0.0f);
    corpse->data.UnitData = &corpse_data; corpse->health.value = 0.0f;
    corpse->svflags |= SVF_MONSTER | SVF_DEADMONSTER;

    test_execute_code(caster, "Arai", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE));
    T_NOT_NULL(oldest); T_ASSERT(oldest && M_IsDead(oldest)); T_ASSERT(!corpse->inuse);
    FILTER_EDICTS(unit, unit->inuse && !M_IsDead(unit) && unit->s.player == 0 &&
                  unit->class_id == MAKEFOURCC('h','f','o','o') && unit->summon_ability) living++;
    T_EQ(living, 25);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Aast's `player,dead` target contract selects the nearest owned ordinary Tauren and restores its original edict. */
TEST(wc3_spell, ancestral_spirit_revives_nearest_owned_nonhero_tauren) {
	const char slk[] =
		"ID;PWXL;N;EBB;Y2;X7\n"
		"C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs1\"\n"
		"C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\nC;Y1;X7;K\"DataA1\"\n"
		"C;Y2;X1;K\"Aast\"\nC;Y2;X2;K\"Aast\"\nC;Y2;X3;K\"ground,player,dead\"\n"
		"C;Y2;X4;K\"250\"\nC;Y2;X5;K\"30\"\nC;Y2;X6;K\"350\"\nC;Y2;X7;K\"0.4\"\nE\n";
	static UnitBalance_t const tauren = { .foodUsed = 5 }, hero = { .strength = 1 };
	UnitData_t corpse_data = { .deathType = 3 };
	UnitAbilities_t abilities = { .abilList = "Aast" };
	slkTestData_t *rows = parse_slk_string(slk), *old;
	edict_t *caster = make_hero(MAKEFOURCC('o','s','p','m'), 500, 1000, 0, 0);
	edict_t *zero_health = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 10, 0);
	edict_t *living = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 20, 0);
	edict_t *hero_corpse = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 30, 0);
	edict_t *other_corpse = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 40, 0);
	edict_t *allied_corpse = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 50, 0);
	edict_t *nearest = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 100, 0);
	edict_t *next = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 200, 0);
	edict_t *distant = alloc_test_unit(MAKEFOURCC('o','t','a','u'), 351, 0);
	uint32_t nearest_spawn = nearest->spawn_time;

	old = G_SetSLKRows("AbilityData", rows);
	caster->data.UnitAbilities = &abilities; caster->s.player = 0;
	zero_health->data.UnitBalance = &tauren; zero_health->s.player = 0;
	zero_health->svflags |= SVF_MONSTER; zero_health->health.value = 0; zero_health->health.max_value = 400;
	living->data.UnitBalance = nearest->data.UnitBalance = next->data.UnitBalance = distant->data.UnitBalance = &tauren;
	living->s.player = 0; living->svflags |= SVF_MONSTER;
	living->health.value = living->health.max_value = 400;
	hero_corpse->data.UnitBalance = &hero; other_corpse->data.UnitBalance = &tauren;
	allied_corpse->data.UnitBalance = &tauren;
	edict_t *corpses[] = { hero_corpse, other_corpse, allied_corpse, nearest, next, distant };
	FOR_LOOP(i, sizeof(corpses) / sizeof(*corpses)) {
		corpses[i]->data.UnitData = &corpse_data;
		corpses[i]->s.player = corpses[i] == allied_corpse ? 1 : 0;
		corpses[i]->svflags |= SVF_MONSTER; corpses[i]->stand = unit_stand;
		corpses[i]->health.value = corpses[i]->health.max_value = 400;
		unit_die(corpses[i], caster);
	}
	((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
	((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
	level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

	T_EQ(S_AbilityItem(MAKEFOURCC('A','a','s','t')).ability->proc, CAbilityAncestralSpirit);
	T_ASSERT(S_CastNoTargetSpell(caster, MAKEFOURCC('A','a','s','t')));
	T_EQ(nearest->spawn_time, nearest_spawn); T_EQ(nearest->s.player, 0);
	T_FEQ(nearest->health.value, 160.0f, .001f);
	T_ASSERT(!(nearest->svflags & SVF_DEADMONSTER)); T_ASSERT(!(nearest->s.flags & EF_NOT_SELECTABLE));
	T_EQ(G_GetFood(nearest)->used, 5); T_ASSERT(M_IsDead(next)); T_ASSERT(M_IsDead(allied_corpse));
	S_SpellEndCooldown(caster, MAKEFOURCC('A','a','s','t'));
	T_ASSERT(S_CastNoTargetSpell(caster, MAKEFOURCC('A','a','s','t'))); T_ASSERT(!M_IsDead(next));
	S_SpellEndCooldown(caster, MAKEFOURCC('A','a','s','t'));
	float mana = caster->mana.value;
	T_ASSERT(!S_CastNoTargetSpell(caster, MAKEFOURCC('A','a','s','t'))); T_FEQ(caster->mana.value, mana, .001f);
	level.alliances[0][1] &= ~(1 << ALLIANCE_PASSIVE);
	T_ASSERT(!S_CastNoTargetSpell(caster, MAKEFOURCC('A','a','s','t'))); T_FEQ(caster->mana.value, mana, .001f);
	T_ASSERT(M_IsDead(zero_health)); T_ASSERT(!(zero_health->svflags & SVF_DEADMONSTER));
	T_ASSERT(!M_IsDead(living)); T_ASSERT(M_IsDead(hero_corpse)); T_ASSERT(M_IsDead(other_corpse));
	T_ASSERT(M_IsDead(allied_corpse)); T_ASSERT(M_IsDead(distant));
	G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, chain_lightning_bounces_respect_authored_target_mask) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\nC;Y2;X3;K\"air,enemy\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"0\"\n"
        "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0);
    edict_t *air = alloc_test_unit(MAKEFOURCC('o','w','y','v'), 100, 0);
    edict_t *ground = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 150, 0);

    caster->s.player = 0;
    air->s.player = ground->s.player = 1;
    air->targtype = TARG_AIR; ground->targtype = TARG_GROUND;
    air->health.value = air->health.max_value = 500;
    ground->health.value = ground->health.max_value = 500;
    test_execute_code(caster, "AOcl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = air));
    T_FEQ(air->health.value, 400.0f, 0.001f);
    {
        edict_t *thinker = NULL;
        FILTER_EDICTS(ent, ent->think == chain_lightning_think) { thinker = ent; break; }
        T_NOT_NULL(thinker);
        level.time = thinker->freetime;
        chain_lightning_think(thinker);
        T_ASSERT(!thinker->inuse);
    }
    T_FEQ(ground->health.value, 500.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, chain_lightning_delays_each_jump_and_never_rehits_previous_targets) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\nC;Y2;X3;K\"ground,enemy\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"3\"\nC;Y2;X6;K\"0.5\"\n"
        "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0);
    edict_t *first = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 150, 0);
    edict_t *third = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 200, 0);
    edict_t *thinker = NULL;

    caster->s.player = 0;
    first->s.player = second->s.player = third->s.player = 1;
    first->targtype = second->targtype = third->targtype = TARG_GROUND;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    first->health.value = first->health.max_value = 500;
    second->health.value = second->health.max_value = 500;
    third->health.value = third->health.max_value = 500;
    first->svflags |= SVF_MONSTER;
    second->svflags |= SVF_MONSTER;
    third->svflags |= SVF_MONSTER;
    level.time = 0;
    test_execute_code(caster, "AOcl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
    T_FEQ(first->health.value, 400.0f, 0.001f);
    T_FEQ(second->health.value, 500.0f, 0.001f);
    T_FEQ(third->health.value, 500.0f, 0.001f);
    FILTER_EDICTS(ent, ent->think == chain_lightning_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);

    level.time = 249; G_RunEntities();
    T_FEQ(second->health.value, 500.0f, 0.001f);
    T_FEQ(third->health.value, 500.0f, 0.001f);

    level.time = 250; G_RunEntities();
    T_FEQ(first->health.value, 400.0f, 0.001f);
    T_FEQ(second->health.value + third->health.value, 950.0f, 0.001f);
    T_ASSERT(thinker->inuse);

    level.time = 499; G_RunEntities();
    T_FEQ(second->health.value + third->health.value, 950.0f, 0.001f);
    level.time = 500; G_RunEntities();
    T_FEQ(first->health.value, 400.0f, 0.001f);
    T_FEQ(second->health.value + third->health.value, 925.0f, 0.001f);
    T_ASSERT(!thinker->inuse);

    /* The nearest unvisited unit is second at 250 ms, then third at 500 ms. */
    T_FEQ(second->health.value, 450.0f, 0.001f);
    T_FEQ(third->health.value, 475.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, chain_lightning_stops_if_caster_slot_is_reused) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"DataA1\"\nC;Y1;X5;K\"DataB1\"\nC;Y1;X6;K\"DataC1\"\n"
        "C;Y1;X7;K\"Area1\"\nC;Y1;X8;K\"levels\"\n"
        "C;Y2;X1;K\"AOcl\"\nC;Y2;X2;K\"AOcl\"\nC;Y2;X3;K\"ground,enemy\"\n"
        "C;Y2;X4;K\"100\"\nC;Y2;X5;K\"2\"\nC;Y2;X6;K\"0.5\"\n"
        "C;Y2;X7;K\"500\"\nC;Y2;X8;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0);
    edict_t *first = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 100, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 150, 0);
    edict_t *thinker = NULL;

    caster->s.player = 0;
    first->s.player = second->s.player = 1;
    first->targtype = second->targtype = TARG_GROUND;
    first->health.value = first->health.max_value = 500;
    second->health.value = second->health.max_value = 500;
    level.time = 100; caster->spawn_time = 100;
    test_execute_code(caster, "AOcl", MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = first));
    FILTER_EDICTS(ent, ent->think == chain_lightning_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_EQ(G_GetChannel(thinker)->owner_spawn_time, 100);

    /* Reusing the caster edict slot must not let the old delayed cast continue. */
    caster->spawn_time = 101;
    level.time = thinker->freetime;
    chain_lightning_think(thinker);
    T_ASSERT(!thinker->inuse);
    T_FEQ(second->health.value, 500.0f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_spell, far_sight_reapplies_visibility_until_authored_duration_expires) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X6\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"Dur1\"\n"
        "C;Y1;X4;K\"Area1\"\nC;Y1;X5;K\"levels\"\nC;Y1;X6;K\"targs\"\n"
        "C;Y2;X1;K\"AOfs\"\nC;Y2;X2;K\"AOfs\"\nC;Y2;X3;K\"2\"\n"
        "C;Y2;X4;K\"128\"\nC;Y2;X5;K\"1\"\nC;Y2;X6;K\"\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old;
    edict_t *caster; edict_t *thinker = NULL;
    vec2_t point = { 256.0f, 0.0f };
    uint32_t cell;

    reset_entities(); setup_test_world(); G_FowInit(); G_FowConnectPlayer(0);
    old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('O','f','a','r'), 0, 0);
    caster->health.value = caster->health.max_value = 500; caster->s.player = 0;
    level.time = 0;
    test_execute_code(caster, "AOfs", MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = point));
    FILTER_EDICTS(ent, ent->think == far_sight_think) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    cell = G_FowWorldToCellY(point.y) * level.fow.width + G_FowWorldToCellX(point.x);
    T_EQ(level.fow.players[0].visible[cell], 1);

    /* Current visibility is rebuilt each fog update; the active spell reveal is
     * re-applied after that clear, independently of the caster's continued life. */
    caster->inuse = false;
    G_FowUpdate();
    T_EQ(level.fow.players[0].visible[cell], 1);
    level.time = 1999;
    G_FowUpdate();
    T_EQ(level.fow.players[0].visible[cell], 1);
    level.time = 2000; far_sight_think(thinker);
    G_FowUpdate();
    T_EQ(level.fow.players[0].visible[cell], 0);
    T_ASSERT(!thinker->inuse);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows); G_FowShutdown();
}

TEST(wc3_spell, earthquake_waits_for_effect_delay_slows_ground_and_damages_structures_and_trees) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\nC;Y2;X3;K\"ground,structure,debris,enemy,tree\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\nC;Y2;X9;K\"2\"\n"
        "C;Y2;X10;K\"10\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"300\"\n"
        "C;Y2;X13;K\"BOeq\"\nC;Y2;X14;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0);
    edict_t *ground = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 50, 0);
    edict_t *air = alloc_test_unit(MAKEFOURCC('o','w','y','v'), 60, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 70, 0);
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 80, 0);
    edict_t *rooted_ancient = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 90, 0);
    edict_t *uprooted_ancient = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 100, 0);
    edict_t *thinker = NULL;
    vec2_t point = { 0, 0 };

    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    ground->s.player = air->s.player = building->s.player = 1;
    rooted_ancient->s.player = uprooted_ancient->s.player = 1;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    ground->targtype = TARG_GROUND; ground->unitinfo.MoveSpeed = 300.0f;
    air->targtype = TARG_AIR; building->targtype = TARG_STRUCTURE; building->s.flags |= EF_BUILDING;
    ground->svflags |= SVF_MONSTER; air->svflags |= SVF_MONSTER; building->svflags |= SVF_MONSTER;
    rooted_ancient->targtype = uprooted_ancient->targtype = TARG_STRUCTURE;
    G_AddAncientRoot(rooted_ancient);
    G_AddAncientRoot(uprooted_ancient);
    G_GetAncientRoot(rooted_ancient)->ability = G_GetAncientRoot(uprooted_ancient)->ability = MAKEFOURCC('A','r','o','o');
    G_GetAncientRoot(rooted_ancient)->mode = ANCIENT_ROOTED;
    rooted_ancient->s.flags |= EF_BUILDING;
    G_GetAncientRoot(uprooted_ancient)->mode = ANCIENT_UPROOTED;
    uprooted_ancient->s.flags &= ~EF_BUILDING;
    rooted_ancient->svflags |= SVF_MONSTER; uprooted_ancient->svflags |= SVF_MONSTER;
    ground->health.value = ground->health.max_value = 500;
    air->health.value = air->health.max_value = 500;
    building->health.value = building->health.max_value = 500;
    rooted_ancient->health.value = rooted_ancient->health.max_value = 500;
    uprooted_ancient->health.value = uprooted_ancient->health.max_value = 500;
    G_AddDestructable(tree);
    tree->svflags &= ~SVF_MONSTER; tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = 500;
    level.time = 0;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_FEQ(building->health.value, 500.0f, 0.001f);
    T_FEQ(tree->health.value, 500.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(ground, MAKEFOURCC('B','O','e','q')), 0);

    level.time = 1999; earthquake_think(thinker);
    T_FEQ(building->health.value, 500.0f, 0.001f);
    level.time = 2000; earthquake_think(thinker);
    T_FEQ(building->health.value, 490.0f, 0.001f);
    T_FEQ(rooted_ancient->health.value, 490.0f, 0.001f);
    T_FEQ(uprooted_ancient->health.value, 500.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(uprooted_ancient, MAKEFOURCC('B','O','e','q')), 1);
    T_FEQ(tree->health.value, 490.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(ground, MAKEFOURCC('B','O','e','q')), 1);
    T_EQ(G_UnitStatusLevel(air, MAKEFOURCC('B','O','e','q')), 0);
    T_FEQ(S_EarthquakeMoveReduction(ground), 0.75f, 0.001f);
    T_FEQ(unit_movedistance(ground), 10.0f * 140.0f / (float)FRAMETIME, 0.001f);

    S_SpellCancelChannel(caster);
    earthquake_think(thinker);
    T_ASSERT(!thinker->inuse);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, earthquake_emits_provisional_random_terrain_pulses_after_effect_delay) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Dur1\"\nC;Y1;X5;K\"HeroDur1\"\n"
        "C;Y1;X6;K\"DataA1\"\nC;Y1;X7;K\"DataB1\"\n"
        "C;Y1;X8;K\"DataC1\"\nC;Y1;X9;K\"Area1\"\nC;Y1;X10;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\n"
        "C;Y2;X3;K\"ground,structure\"\nC;Y2;X4;K\"4\"\nC;Y2;X5;K\"4\"\n"
        "C;Y2;X6;K\"0.5\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"0.75\"\n"
        "C;Y2;X9;K\"250\"\nC;Y2;X10;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_multicast)(vec3_t const *, multicast_t) = gi.multicast;
    uint32_t first_deformation_id;
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 0, 0), *thinker = NULL;
    vec2_t point = { 500, 700 };

    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    terrain_deform_capture_count = terrain_deform_capture_float_count = 0;
    gi.Write = terrain_deform_capture_write; gi.multicast = terrain_deform_capture_multicast;
    level.time = 0;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    T_EQ(terrain_deform_capture_count, 0);
    level.time = 499; earthquake_think(thinker);
    T_EQ(terrain_deform_capture_count, 0);
    level.time = 500; earthquake_think(thinker);
    T_EQ(terrain_deform_capture_count, 16);
    T_EQ(terrain_deform_capture_types[0], PF_BYTE);
    T_EQ(terrain_deform_capture_types[1], PF_BYTE);
    T_EQ(terrain_deform_capture_types[2], PF_LONG);
    T_EQ(terrain_deform_capture_types[3], PF_BYTE);
    T_EQ(terrain_deform_capture_values[0], svc_temp_entity);
    T_EQ(terrain_deform_capture_values[1], TE_TERRAIN_DEFORM);
    T_ASSERT(terrain_deform_capture_values[2] > 0);
    first_deformation_id = (uint32_t)terrain_deform_capture_values[2];
    T_EQ(terrain_deform_capture_values[3], TERRAIN_DEFORM_RANDOM);
    T_EQ(terrain_deform_capture_float_count, 8);
    T_FEQ(terrain_deform_capture_floats[0], 500, 0.001f);
    T_FEQ(terrain_deform_capture_floats[1], 700, 0.001f);
    T_FEQ(terrain_deform_capture_floats[2], 250, 0.001f);
    T_FEQ(terrain_deform_capture_floats[3], -48, 0.001f);
    T_FEQ(terrain_deform_capture_floats[4], 48, 0.001f);
    T_EQ(terrain_deform_capture_types[12], PF_LONG);
    T_EQ(terrain_deform_capture_types[15], PF_BYTE);
    T_EQ(terrain_deform_capture_values[12], 1000);
    T_EQ(terrain_deform_capture_values[14], 200);
    level.time = 1500; earthquake_think(thinker);
    T_EQ(terrain_deform_capture_count, 32);
    T_EQ(terrain_deform_capture_float_count, 16);
    T_EQ(terrain_deform_capture_values[17], TE_TERRAIN_DEFORM);
    T_ASSERT(terrain_deform_capture_values[18] > 0);
    T_ASSERT((uint32_t)terrain_deform_capture_values[18] != first_deformation_id);
    S_SpellCancelChannel(caster); earthquake_think(thinker);
    T_ASSERT(!thinker->inuse);
    gi.Write = old_write; gi.multicast = old_multicast;
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, earthquake_respects_authored_relationship_and_destructable_tokens) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\nC;Y2;X3;K\"ground,structure,friend,tree\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\nC;Y2;X9;K\"0\"\n"
        "C;Y2;X10;K\"10\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"300\"\n"
        "C;Y2;X13;K\"BOeq\"\nC;Y2;X14;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 500, 500);
    edict_t *friend_ground = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 50, 0);
    edict_t *friend_building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 60, 0);
    edict_t *enemy_ground = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 70, 0);
    edict_t *enemy_building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 80, 0);
    edict_t *tree = alloc_test_unit(MAKEFOURCC('L','T','l','t'), 90, 0);
    edict_t *debris = alloc_test_unit(MAKEFOURCC('B','0','0','0'), 100, 0);
    edict_t *thinker = NULL;
    vec2_t point = { 0, 0 };

    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    friend_ground->s.player = friend_building->s.player = 0;
    enemy_ground->s.player = enemy_building->s.player = 1;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    friend_ground->targtype = enemy_ground->targtype = TARG_GROUND;
    friend_building->targtype = enemy_building->targtype = TARG_STRUCTURE;
    friend_building->s.flags |= EF_BUILDING; enemy_building->s.flags |= EF_BUILDING;
    friend_ground->svflags |= SVF_MONSTER; friend_building->svflags |= SVF_MONSTER;
    enemy_ground->svflags |= SVF_MONSTER; enemy_building->svflags |= SVF_MONSTER;
    friend_ground->health.value = friend_ground->health.max_value = 500;
    friend_building->health.value = friend_building->health.max_value = 500;
    enemy_ground->health.value = enemy_ground->health.max_value = 500;
    enemy_building->health.value = enemy_building->health.max_value = 500;
    G_AddDestructable(tree);
    tree->svflags &= ~SVF_MONSTER; tree->targtype = TARG_TREE;
    G_AddDestructable(debris);
    debris->svflags &= ~SVF_MONSTER; debris->targtype = TARG_DEBRIS;
    tree->health.value = tree->health.max_value = 500;
    debris->health.value = debris->health.max_value = 500;

    level.time = 0;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    earthquake_think(thinker);

    T_EQ(G_UnitStatusLevel(friend_ground, MAKEFOURCC('B','O','e','q')), 1);
    T_FEQ(friend_building->health.value, 490.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(enemy_ground, MAKEFOURCC('B','O','e','q')), 0);
    T_FEQ(enemy_building->health.value, 500.0f, 0.001f);
    T_FEQ(tree->health.value, 490.0f, 0.001f);
    T_FEQ(debris->health.value, 500.0f, 0.001f);

    S_SpellCancelChannel(caster); earthquake_think(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, earthquake_uses_shared_mechanical_target_filter) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\nC;Y2;X3;K\"ground,structure,friend,mechanical\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\nC;Y2;X9;K\"0\"\n"
        "C;Y2;X10;K\"10\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"300\"\n"
        "C;Y2;X13;K\"BOeq\"\nC;Y2;X14;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 500, 500);
    edict_t *mechanical = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 60, 0);
    edict_t *organic = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 70, 0);
    edict_t *thinker = NULL;
    vec2_t point = { 0, 0 };
    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    mechanical->s.player = organic->s.player = 0;
    mechanical->targtype = TARG_MECHANICAL; organic->targtype = TARG_ORGANIC;
    mechanical->s.flags |= EF_BUILDING; organic->s.flags |= EF_BUILDING;
    mechanical->svflags |= SVF_MONSTER; organic->svflags |= SVF_MONSTER;
    mechanical->health.value = mechanical->health.max_value = 500;
    organic->health.value = organic->health.max_value = 500;
    level.time = 0;
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    earthquake_think(thinker);
    T_FEQ(mechanical->health.value, 490.0f, 0.001f);
    T_FEQ(organic->health.value, 500.0f, 0.001f);
    S_SpellCancelChannel(caster); earthquake_think(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* The retail mask names no relationship: Earthquake stays enemy-only, and as an area
 * effect it still slows enemies the caster cannot see. */
TEST(wc3_spell, earthquake_retail_mask_is_enemy_only_and_reaches_invisible_units) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X14\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"targs\"\n"
        "C;Y1;X4;K\"Cost1\"\nC;Y1;X5;K\"Cool1\"\nC;Y1;X6;K\"Rng1\"\n"
        "C;Y1;X7;K\"Dur1\"\nC;Y1;X8;K\"HeroDur1\"\nC;Y1;X9;K\"DataA1\"\n"
        "C;Y1;X10;K\"DataB1\"\nC;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"Area1\"\n"
        "C;Y1;X13;K\"BuffID1\"\nC;Y1;X14;K\"levels\"\n"
        "C;Y2;X1;K\"AOeq\"\nC;Y2;X2;K\"AOeq\"\nC;Y2;X3;K\"ground,structure,debris,tree\"\n"
        "C;Y2;X4;K\"0\"\nC;Y2;X5;K\"0\"\nC;Y2;X6;K\"1000\"\n"
        "C;Y2;X7;K\"5\"\nC;Y2;X8;K\"5\"\nC;Y2;X9;K\"0\"\n"
        "C;Y2;X10;K\"10\"\nC;Y2;X11;K\"0.75\"\nC;Y2;X12;K\"300\"\n"
        "C;Y2;X13;K\"BOeq\"\nC;Y2;X14;K\"1\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "AOeq" };
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster = make_hero(MAKEFOURCC('O','f','a','r'), 500, 500, 500, 500);
    edict_t *friend_building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 60, 0);
    edict_t *enemy_building = alloc_test_unit(MAKEFOURCC('o','b','u','r'), 80, 0);
    edict_t *hidden_enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 70, 0);
    edict_t *thinker = NULL;
    vec2_t point = { 0, 0 };

    caster->data.UnitAbilities = &abilities; caster->s.player = 0;
    friend_building->s.player = 0;
    enemy_building->s.player = hidden_enemy->s.player = 1;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    friend_building->targtype = enemy_building->targtype = TARG_STRUCTURE;
    friend_building->s.flags |= EF_BUILDING; enemy_building->s.flags |= EF_BUILDING;
    hidden_enemy->targtype = TARG_GROUND;
    friend_building->svflags |= SVF_MONSTER; enemy_building->svflags |= SVF_MONSTER;
    hidden_enemy->svflags |= SVF_MONSTER;
    friend_building->health.value = friend_building->health.max_value = 500;
    enemy_building->health.value = enemy_building->health.max_value = 500;
    hidden_enemy->health.value = hidden_enemy->health.max_value = 500;
    hidden_enemy->runtime.flags |= UNIT_BALANCE_PERMANENT_INVISIBLE;
    G_InitVisibility(hidden_enemy);
    hidden_enemy->vision->reveal_until = 0;

    level.time = 0;
    T_ASSERT(S_UnitIsInvisibleToPlayer(hidden_enemy, 0));
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','O','e','q'), &point));
    FILTER_EDICTS(ent, ent->think == earthquake_think && ent->owner == caster) { thinker = ent; break; }
    T_NOT_NULL(thinker);
    earthquake_think(thinker);

    /* The caster's own building is the one target the two builds disagree on. */
#ifdef WC3_EARTHQUAKE_FRIENDLY_FIRE
    T_FEQ(friend_building->health.value, 490.0f, 0.001f);
#else
    T_FEQ(friend_building->health.value, 500.0f, 0.001f);
#endif
    T_FEQ(enemy_building->health.value, 490.0f, 0.001f);
    T_EQ(G_UnitStatusLevel(hidden_enemy, MAKEFOURCC('B','O','e','q')), 1);

    S_SpellCancelChannel(caster); earthquake_think(thinker);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* Phoenix ability registration must route to simulation handlers, not the
 * old passive placeholders. The spell handler still reads authored UnitID. */
TEST(wc3_spell, phoenix_registered_ability_contracts) {
    abilityitem_t summon = S_AbilityItem(MAKEFOURCC('A','H','p','x'));
    abilityitem_t morph = S_AbilityItem(MAKEFOURCC('A','p','h','x'));
    abilityitem_t fire = S_AbilityItem(MAKEFOURCC('A','p','x','f'));
    T_NOT_NULL(summon.ability); T_NOT_NULL(morph.ability); T_NOT_NULL(fire.ability);
    T_ASSERT(summon.ability->flags & AB_SPELL);
    T_ASSERT(morph.ability->flags & AB_UPDATE);
    T_ASSERT(fire.ability->flags & AB_UPDATE);
    T_ASSERT(morph.ability->flags & AB_PASSIVE);
    T_ASSERT(fire.ability->flags & AB_PASSIVE);
    T_ASSERT(morph.ability->proc == CAbilityPhoenixMorphing);
    T_ASSERT(fire.ability->proc == CAbilityPhoenixFire);
}

/* Drive Flame Strike through normal spell dispatch and its scheduled thinker.
 * The non-stock damage/radius values ensure the test observes authored data,
 * not a retail constant hidden in the implementation. */
TEST(wc3_spell, flame_strike_uses_authored_point_and_pulse_data) {
    static cstring_t const slk =
        "ID;PWXL;N;EBB;Y2;X16\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs1\"\nC;Y1;X5;K\"Cast1\"\nC;Y1;X6;K\"Dur1\"\n"
        "C;Y1;X7;K\"HeroDur1\"\nC;Y1;X8;K\"Area1\"\n"
        "C;Y1;X9;K\"DataA1\"\nC;Y1;X10;K\"DataB1\"\n"
        "C;Y1;X11;K\"DataC1\"\nC;Y1;X12;K\"DataD1\"\n"
        "C;Y1;X13;K\"DataE1\"\nC;Y1;X14;K\"DataF1\"\n"
        "C;Y1;X15;K\"BuffID1\"\nC;Y1;X16;K\"EfctID1\"\n"
        "C;Y2;X1;K\"AHfs\"\nC;Y2;X2;K\"AHfs\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K\"ground,enemy\"\nC;Y2;X5;K1\nC;Y2;X6;K8\n"
        "C;Y2;X7;K2\nC;Y2;X8;K180\nC;Y2;X9;K37\n"
        "C;Y2;X10;K1\nC;Y2;X11;K11\nC;Y2;X12;K2\n"
        "C;Y2;X13;K1\nC;Y2;X14;K1000\nC;Y2;X15;K\"BHfs\"\n"
        "C;Y2;X16;K\"XHfs\"\nE\n";
    static UnitAbilities_t const abilities = { .abilList = "AHfs" };
    slkTestData_t *rows, *old;
    edict_t *caster, *target, *thinker = NULL;
    vec2_t point = { 64.0f, 0.0f };

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('H','b','l','m'), 0.0f, 0.0f);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 0.0f);
    caster->data.UnitAbilities = &abilities;
    caster->s.player = 0; caster->svflags |= SVF_MONSTER;
    target->s.player = 1; target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500.0f;
    target->unit_state->armor_value = 0.0f;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

    level.time = 0;
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('A','H','f','s'), WC3_EFFECT_EFFECT, 0),
            "TestUI\\Models\\flame-strike-warning.mdx");
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('A','H','f','s'), WC3_EFFECT_SPECIAL, 0),
            "TestUI\\Models\\flame-strike-impact-1.mdx");
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('X','H','f','s'), WC3_EFFECT_EFFECT, 0),
            "TestUI\\Models\\flame-strike-embers.mdx");
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','H','f','s'), WC3_EFFECT_TARGET, 0),
            "TestUI\\Models\\flame-strike-damage.mdx");
    T_ASSERT(S_CastPointTargetSpell(caster, MAKEFOURCC('A','H','f','s'), &point));
    T_NOT_NULL(G_AbilityEffectArt(MAKEFOURCC('A','H','f','s'), WC3_EFFECT_EFFECT, 0));
    {
        bool warning_spawned = false;
        int const warning_model = G_RegisterModel("TestUI\\Models\\flame-strike-warning.mdx");
        FILTER_EDICTS(effect, effect->inuse && effect->s.model == warning_model &&
                      effect->s.origin2.x == point.x && effect->s.origin2.y == point.y)
            warning_spawned = true;
        T_ASSERT(warning_spawned);
    }
    FILTER_EDICTS(ent, ent->inuse && ent->owner == caster && ent->class_id == MAKEFOURCC('A','H','f','s') && ent->think) {
        thinker = ent; break;
    }
    T_NOT_NULL(thinker);
    if (thinker) {
        float const before = target->health.value;
        level.time = 999; G_RunEntity(thinker);
        T_FEQ(target->health.value, before, 0.001f);
        level.time = 1000; G_RunEntity(thinker);
        T_FEQ(before - target->health.value, 37.0f, 0.001f);
        {
            bool impact_spawned = false, field_spawned = false, target_art_spawned = false;
            int const impact_model = G_RegisterModel("TestUI\\Models\\flame-strike-impact-1.mdx");
            int const field_model = G_RegisterModel("TestUI\\Models\\flame-strike-embers.mdx");
            int const target_model = G_RegisterModel("TestUI\\Models\\flame-strike-damage.mdx");
            FILTER_EDICTS(effect, effect->inuse && effect->s.model == impact_model) impact_spawned = true;
            FILTER_EDICTS(effect, effect->inuse && effect->s.model == field_model) field_spawned = true;
            FILTER_EDICTS(effect, effect->inuse && effect->s.model == target_model && effect->goalentity == target)
                target_art_spawned = true;
            T_ASSERT(impact_spawned);
            T_ASSERT(field_spawned);
            T_ASSERT(target_art_spawned);
        }
    }

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, phoenix_fire_uses_authored_projectile_damage_burn_and_cooldown) {
    static cstring_t const slk =
        "ID;PWXL;N;EBB;Y2;X12\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs1\"\nC;Y1;X5;K\"Area1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\n"
        "C;Y1;X9;K\"Dur1\"\nC;Y1;X10;K\"HeroDur1\"\n"
        "C;Y1;X11;K\"BuffID1\"\nC;Y1;X12;K\"Missilespeed\"\n"
        "C;Y2;X1;K\"Apxf\"\nC;Y2;X2;K\"Apxf\"\nC;Y2;X3;K1\n"
        "C;Y2;X4;K\"ground,air,enemy\"\nC;Y2;X5;K500\nC;Y2;X6;K2.5\n"
        "C;Y2;X7;K23\nC;Y2;X8;K7\nC;Y2;X9;K3\nC;Y2;X10;K2\n"
        "C;Y2;X11;K\"Bpxf\"\nC;Y2;X12;K900\nE\n";
    static UnitAbilities_t const abilities = { .abilList = "Aphx,Apxf" };
    slkTestData_t *rows, *old;
    edict_t *phoenix, *target, *missile = NULL, *second_missile = NULL;
    float health_after_hit;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    phoenix = alloc_test_unit(MAKEFOURCC('h','p','h','x'), 0.0f, 0.0f);
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 100.0f, 0.0f);
    phoenix->data.UnitAbilities = &abilities;
    phoenix->s.player = 0; phoenix->svflags |= SVF_MONSTER;
    phoenix->health.value = phoenix->health.max_value = 1000.0f;
    target->s.player = 1; target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    target->health.value = target->health.max_value = 500.0f;
    target->unit_state->armor_value = 0.0f;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('A','p','x','f'), WC3_EFFECT_MISSILE, 0),
            "TestUI\\Models\\anim_projectile.mdx");
    T_STREQ(G_AbilityEffectArt(MAKEFOURCC('B','p','x','f'), WC3_EFFECT_TARGET, 0),
            "TestUI\\Models\\anim_pulse.mdx");

    level.time = 1000;
    S_RunAbilityUpdates(phoenix);
    T_EQ(phoenix->phoenix_fire_next_ms, 3500u);
    FILTER_EDICTS(ent, ent->inuse && ent->owner == phoenix && ent->class_id == MAKEFOURCC('A','p','x','f') &&
                  ent->goalentity == target && ent->currentmove) { missile = ent; break; }
    T_NOT_NULL(missile);
    if (missile) {
        T_EQ(missile->s.model, G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
        T_FEQ(target->health.value, 500.0f, 0.001f);
        missile->currentmove->endfunc(missile);
    }
    health_after_hit = target->health.value;
    T_FEQ(health_after_hit, 477.0f, 0.001f);
    T_NOT_NULL(unit_findstatus(target, MAKEFOURCC('B','p','x','f')));
    {
        bool burn_art_spawned = false;
        int const burn_model = G_RegisterModel("TestUI\\Models\\anim_pulse.mdx");
        FILTER_EDICTS(effect, effect->inuse && effect->s.model == burn_model && effect->goalentity == target)
            burn_art_spawned = true;
        T_ASSERT(burn_art_spawned);
    }

    level.time = 2000; unit_updatestatuses(target);
    T_FEQ(health_after_hit - target->health.value, 7.0f, 0.001f);
    level.time = 3000; unit_updatestatuses(target);
    T_FEQ(health_after_hit - target->health.value, 14.0f, 0.001f);
    level.time = 3499; S_RunAbilityUpdates(phoenix);
    FILTER_EDICTS(ent, ent->inuse && ent->owner == phoenix && ent->class_id == MAKEFOURCC('A','p','x','f'))
        second_missile = ent;
    T_NULL(second_missile);
    level.time = 3500; S_RunAbilityUpdates(phoenix);
    FILTER_EDICTS(ent, ent->inuse && ent->owner == phoenix && ent->class_id == MAKEFOURCC('A','p','x','f'))
        second_missile = ent;
    T_NOT_NULL(second_missile);
    level.time = 4000; unit_updatestatuses(target);
    T_FEQ(health_after_hit - target->health.value, 14.0f, 0.001f);

    reset_entities();
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* AHpx is a per-caster unit, not a per-player singleton. Use a non-stock
 * UnitID to exercise the same spawned-unit path without Phoenix-only death
 * and rebirth art. Both casters share one player, so the previous implementation
 * would incorrectly kill the other caster's unit on recast. */
TEST(wc3_spell, phoenix_recast_preserves_other_casters_and_old_incarnations) {
    static cstring_t const slk =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"UnitID1\"\n"
        "C;Y2;X1;K\"AHpx\"\nC;Y2;X2;K\"AHpx\"\n"
        "C;Y2;X3;K1\nC;Y2;X4;K\"hfoo\"\nE\n";
    static UnitAbilities_t const phoenix_ability = { .abilList = "AHpx" };
    uint32_t const code = MAKEFOURCC('A','H','p','x');
    slkTestData_t *rows, *old_rows;
    edict_t *first, *second, *summon_first = NULL, *summon_second = NULL;
    edict_t *stale, *recast = NULL;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old_rows = G_SetSLKRows("AbilityData", rows);
    first = alloc_test_unit(MAKEFOURCC('H','b','l','m'), 0.0f, 0.0f);
    second = alloc_test_unit(MAKEFOURCC('H','b','l','m'), 0.0f, 128.0f);
    first->s.player = second->s.player = 0;
    first->data.UnitAbilities = second->data.UnitAbilities = &phoenix_ability;
    first->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
    second->unit_state->ranks[0] = MAKE(heroability_t, .code = code, .level = 1);
    /* Distinct spawn incarnations remain meaningful even when the same player
     * casts both spells and the game later reuses the caster's edict slot. */
    first->spawn_time = 100;
    second->spawn_time = 101;
    T_EQ(S_SpellLevel(first, code), 1u);
    T_EQ(S_SpellUnitId(code, 1), MAKEFOURCC('h','f','o','o'));

    S_SummonPhoenix(first, code);
    S_SummonPhoenix(second, code);
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == MAKEFOURCC('h','f','o','o') && !M_IsDead(ent)) {
        if (ent->owner == first) summon_first = ent;
        if (ent->owner == second) summon_second = ent;
    }
    T_NOT_NULL(summon_first);
    T_NOT_NULL(summon_second);
    if (summon_first && summon_second) {
        /* A stale caster slot with matching pointer must not match its old
         * summon. Nor should changing the summoned unit's player ownership
         * silently delete a captured unit on the next cast. */
        stale = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 256.0f, 0.0f);
        stale->owner = first;
        stale->s.player = 0;
        stale->phoenix_owner_spawn_time = first->spawn_time - 1;
        S_SummonPhoenix(first, code);
        T_ASSERT(M_IsDead(summon_first) || !summon_first->inuse);
        T_ASSERT(summon_second->inuse && !M_IsDead(summon_second));
        T_ASSERT(stale->inuse && !M_IsDead(stale));
        FILTER_EDICTS(ent, ent->inuse && ent != summon_first && ent->class_id == MAKEFOURCC('h','f','o','o') &&
                      ent->owner == first && ent->phoenix_owner_spawn_time == first->spawn_time &&
                      !M_IsDead(ent)) { recast = ent; break; }
        T_NOT_NULL(recast);
        if (recast) {
            recast->s.player = 1; /* Simulate control transferred to an enemy. */
            S_SummonPhoenix(first, code);
            T_ASSERT(recast->inuse && !M_IsDead(recast));
        }
    }
    reset_entities();
    G_SetSLKRows("AbilityData", old_rows); free_slk_rows(rows);
}

TEST(wc3_spell, phoenix_death_creates_egg_that_hatches_on_authored_deadline) {
    static cstring_t const ui_slk =
        "ID;PWXL;N;EBB;Y3;X3\nC;Y1;X1;K\"unitUIID\"\nC;Y1;X2;K\"file\"\nC;Y1;X3;K\"modelScale\"\n"
        "C;Y2;X1;K\"hphx\"\nC;Y2;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\nC;Y2;X3;K1\n"
        "C;Y3;X1;K\"hpxe\"\nC;Y3;X2;K\"TestUI\\Models\\anim_pulse.mdx\"\nC;Y3;X3;K1\nE\n";
    static cstring_t const balance_slk =
        "ID;PWXL;N;EBB;Y3;X4\nC;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"realHP\"\n"
        "C;Y1;X3;K\"regenHP\"\nC;Y1;X4;K\"regenType\"\n"
        "C;Y2;X1;K\"hphx\"\nC;Y2;X2;K1250\nC;Y2;X3;K-25\nC;Y2;X4;K\"always\"\n"
        "C;Y3;X1;K\"hpxe\"\nC;Y3;X2;K200\nC;Y3;X3;K0\nC;Y3;X4;K\"none\"\nE\n";
    static cstring_t const data_slk =
        "ID;PWXL;N;EBB;Y3;X3\nC;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"movetp\"\nC;Y1;X3;K\"propWin\"\n"
        "C;Y2;X1;K\"hphx\"\nC;Y2;X2;K\"fly\"\nC;Y2;X3;K60\n"
        "C;Y3;X1;K\"hpxe\"\nC;Y3;X2;K\"foot\"\nC;Y3;X3;K60\nE\n";
    static cstring_t const abilities_slk =
        "ID;PWXL;N;EBB;Y3;X2\nC;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"abilList\"\n"
        "C;Y2;X1;K\"hphx\"\nC;Y2;X2;K\"Aphx\"\n"
        "C;Y3;X1;K\"hpxe\"\nC;Y3;X2;K\"Aphx\"\nE\n";
    slkTestData_t *ui, *old_ui, *balance, *old_balance, *data, *old_data, *abilities, *old_abilities;
    edict_t *caster, *phoenix, *egg = NULL, *phoenix_after_rebirth, *killed_egg = NULL;

    reset_entities(); setup_test_world();
    ui = parse_slk_string(ui_slk); old_ui = G_SetSLKRows("UnitUI", ui);
    balance = parse_slk_string(balance_slk); old_balance = G_SetSLKRows("UnitBalance", balance);
    data = parse_slk_string(data_slk); old_data = G_SetSLKRows("UnitData", data);
    abilities = parse_slk_string(abilities_slk); old_abilities = G_SetSLKRows("UnitAbilities", abilities);
    caster = alloc_test_unit(MAKEFOURCC('H','b','l','m'), 32.0f, 64.0f);
    caster->s.player = 0;
    phoenix = alloc_test_unit(MAKEFOURCC('h','p','h','x'), 96.0f, 64.0f);
    phoenix->owner = caster;
    phoenix->phoenix_owner_spawn_time = caster->spawn_time;
    phoenix->s.player = 0; phoenix->svflags |= SVF_MONSTER; phoenix->die = unit_die;
    T_EQ(G_UnitAbilityLevel(phoenix, MAKEFOURCC('A','p','h','x')), 1u);

    unit_die(phoenix, NULL);
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == MAKEFOURCC('h','p','x','e') &&
                  ent->phoenix_hatch_ms) { egg = ent; break; }
    T_NOT_NULL(egg);
    if (egg) {
        T_EQ(egg->phoenix_hatch_ms, 10000u);
        T_ASSERT(egg->owner == caster);
        T_EQ(egg->phoenix_owner_spawn_time, caster->spawn_time);
        T_FEQ(egg->health.value, 200.0f, 0.001f);
        T_ASSERT(phoenix->aiflags & AI_CORPSE_UNRAISABLE);
        T_ASSERT(phoenix->s.renderfx & RF_HIDDEN);
        level.time = 9999; S_RunAbilityUpdates(egg);
        T_EQ(egg->class_id, MAKEFOURCC('h','p','x','e'));
        level.time = 10000; S_RunAbilityUpdates(egg);
        T_STREQ(G_UnitUI(MAKEFOURCC('h','p','h','x'))->modelFile, "TestUI\\Models\\anim_pulse.mdx");
        T_EQ(G_UnitBalance(MAKEFOURCC('h','p','h','x'))->maxHealth, 1250.0f);
        T_EQ(egg->class_id, MAKEFOURCC('h','p','h','x'));
        T_ASSERT(egg->owner == caster);
        T_EQ(egg->phoenix_owner_spawn_time, caster->spawn_time);
        T_EQ(egg->phoenix_hatch_ms, 0u);
        T_FEQ(egg->health.value, egg->health.max_value, 0.001f);
    }

    /* A second life cycle verifies the inverse: a killed egg cannot hatch at
     * its deadline even though its saved hatch timestamp has not elapsed. */
    level.time = 0;
    phoenix_after_rebirth = alloc_test_unit(MAKEFOURCC('h','p','h','x'), 160.0f, 64.0f);
    phoenix_after_rebirth->s.player = 0;
    phoenix_after_rebirth->svflags |= SVF_MONSTER;
    phoenix_after_rebirth->die = unit_die;
    unit_die(phoenix_after_rebirth, NULL);
    FILTER_EDICTS(ent, ent->inuse && ent != egg && ent->class_id == MAKEFOURCC('h','p','x','e') &&
                  ent->phoenix_hatch_ms) { killed_egg = ent; break; }
    T_NOT_NULL(killed_egg);
    if (killed_egg) {
        T_EQ(killed_egg->phoenix_hatch_ms, 10000u);
        level.time = 9900; unit_die(killed_egg, NULL);
        level.time = 10000; S_RunAbilityUpdates(killed_egg);
        T_EQ(killed_egg->class_id, MAKEFOURCC('h','p','x','e'));
        T_EQ(killed_egg->phoenix_hatch_ms, 10000u);
        T_ASSERT(M_IsDead(killed_egg));
    }

    reset_entities();
    G_SetSLKRows("UnitAbilities", old_abilities); free_slk_rows(abilities);
    G_SetSLKRows("UnitData", old_data); free_slk_rows(data);
    G_SetSLKRows("UnitBalance", old_balance); free_slk_rows(balance);
    G_SetSLKRows("UnitUI", old_ui); free_slk_rows(ui);
}

/* Obsidian support: recipients are selected from real world entities in stable
 * encounter order, with unit-type masks applied before the six-unit cap. */
TEST(wc3_spell, obsidian_essence_caps_organic_allies_and_skips_mechanical_units) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X7\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"Area1\"\nC;Y1;X4;K\"DataA1\"\n"
        "C;Y1;X5;K\"targs\"\nC;Y1;X6;K\"Cost1\"\nC;Y1;X7;K\"Dur1\"\n"
        "C;Y2;X1;K\"Arpl\"\nC;Y2;X2;K\"Arpl\"\n"
        "C;Y2;X3;K\"500\"\nC;Y2;X4;K\"33\"\n"
        "C;Y2;X5;K\"ground,air,friend,self,organic,vulnerable,invulnerable\"\n"
        "C;Y2;X6;K\"2\"\nC;Y2;X7;K\"1\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *allies[8], *mechanical;
    uint32_t changed = 0;

    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('u','o','b','s'), 0, 0);
    caster->s.player = 0; caster->svflags |= SVF_MONSTER;
    caster->health.value = caster->health.max_value;
    caster->mana.value = caster->mana.max_value = 100;
    FOR_LOOP(i, 8) {
        allies[i] = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64 + 32 * i, 0);
        allies[i]->s.player = 0; allies[i]->svflags |= SVF_MONSTER;
        allies[i]->targtype = TARG_GROUND;
        allies[i]->health.value = 50; allies[i]->health.max_value = 200;
    }
    mechanical = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 320, 0);
    mechanical->s.player = 0; mechanical->svflags |= SVF_MONSTER;
    mechanical->targtype = TARG_MECHANICAL;
    mechanical->health.value = 50; mechanical->health.max_value = 200;
    T_ASSERT(test_execute_code(caster, "Arpl", MAKE(spellTarget_t, .type = SPELL_TARGET_NONE)));
    FOR_LOOP(i, 8) if (allies[i]->health.value > 50) changed++;
    T_EQ(changed, 6u);
    T_FEQ(mechanical->health.value, 50.0f, 0.01f);
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}
TEST(wc3_spell, destroyer_absorb_mana_is_an_active_friendly_unit_spell) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"targs\"\nC;Y1;X4;K\"DataB1\"\n"
        "C;Y2;X1;K\"Aabs\"\nC;Y2;X2;K\"Aabs\"\n"
        "C;Y2;X3;K\"player,vulnerable,invulnerable\"\n"
        "C;Y2;X4;K\"99999\"\nE\n";
    slkTestData_t *rows, *old;
    abilityitem_t absorb = S_AbilityItem(FS_SLKKey("Aabs"));
    abilityCall_t call;
    spellTarget_t st;
    edict_t *caster, *ally, *enemy;
    T_NOT_NULL(absorb.ability);
    if (!absorb.ability) return;
    T_EQ(absorb.ability->proc, CAbilityAbsorb);
    T_EQ(absorb.ability->target_type, SPELL_TARGET_UNIT);
    call = MAKE(abilityCall_t, .item = &absorb, .spell_approach_range = 70.0f);
    T_ASSERT(CAbilityAbsorb(NULL, A_SPELL_APPROACH_RANGE, &call));
    T_FEQ(call.spell_approach_range, 70.0f, 0.001f);
    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('u','b','s','p'), 0, 0);
    ally = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 64);
    caster->s.player = ally->s.player = 0; enemy->s.player = 1;
    caster->svflags |= SVF_MONSTER;
    ally->svflags |= SVF_MONSTER;
    enemy->svflags |= SVF_MONSTER;
    caster->mana.max_value = 100; caster->mana.value = 0;
    ally->mana.max_value = ally->mana.value = 80;
    enemy->mana.max_value = enemy->mana.value = 80;
    st = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = ally);
    call = MAKE(abilityCall_t, .item = &absorb, .target = &st);
    T_ASSERT(CAbilityAbsorb(caster, A_VALIDATE, &call));
    st.entity = enemy;
    T_ASSERT(!CAbilityAbsorb(caster, A_VALIDATE, &call));
    st.entity = caster;
    T_ASSERT(!CAbilityAbsorb(caster, A_VALIDATE, &call));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_spell, obsidian_statue_spirit_touch_jass_autocast_orders_toggle_state) {
    const char slk[] =
        "ID;PWXL;N;EBB;Y3;X2\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y2;X1;K\"Arpm\"\nC;Y2;X2;K\"Arpm\"\n"
        "C;Y3;X1;K\"Arpl\"\nC;Y3;X2;K\"Arpl\"\nE\n";
    uint32_t const mana_code = MAKEFOURCC('A','r','p','m');
    uint32_t const life_code = MAKEFOURCC('A','r','p','l');
    slkTestData_t *rows, *old;
    ability_t const *mana_on = FindAbilityByOrder("replenishmanaon");
    ability_t const *mana_off = FindAbilityByOrder("replenishmanaoff");
    ability_t const *life_on = FindAbilityByOrder("replenishlifeon");
    ability_t const *life_off = FindAbilityByOrder("replenishlifeoff");
    edict_t *statue;

    T_NOT_NULL(mana_on); T_NOT_NULL(mana_off);
    T_NOT_NULL(life_on); T_NOT_NULL(life_off);
    if (!mana_on || !mana_off || !life_on || !life_off) return;
    T_EQ(mana_on->proc, CAbilityReplenishMana);
    T_EQ(mana_off->proc, CAbilityReplenishMana);
    T_EQ(life_on->proc, CAbilityReplenishLife);
    T_EQ(life_off->proc, CAbilityReplenishLife);
    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    statue = alloc_test_unit(MAKEFOURCC('u','o','b','s'), 0, 0);
    T_ASSERT(G_ActorAddSkill(statue, mana_code));
    T_ASSERT(G_ActorAddSkill(statue, life_code));
    T_ASSERT(unit_issueimmediateorder(statue, "replenishmanaon"));
    T_EQ(statue->unit_state->autocast, mana_code);
    T_ASSERT(G_UnitAutocastIsOn(statue, mana_code));
    T_ASSERT(unit_issueimmediateorder(statue, "replenishmanaoff"));
    T_EQ(statue->unit_state->autocast, 0u);
    T_ASSERT(!G_UnitAutocastIsOn(statue, mana_code));
    T_ASSERT(unit_issueimmediateorder(statue, "replenishlifeon"));
    T_EQ(statue->unit_state->autocast, life_code);
    T_ASSERT(G_UnitAutocastIsOn(statue, life_code));
    T_ASSERT(unit_issueimmediateorder(statue, "replenishlifeoff"));
    T_EQ(statue->unit_state->autocast, 0u);
    T_ASSERT(!G_UnitAutocastIsOn(statue, life_code));
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

/* This is a real A_EXECUTE + fixed-point missile + impact lifecycle. Devour
 * must not dispel a unit at cast time while its authored missile is in flight. */
TEST(wc3_spell, destroyer_devour_magic_dispels_only_when_projectile_arrives) {
    static UnitAbilities_t abilities = { .abilList = "Advm" };
    static char const slk[] =
        "ID;PWXL;N;EBB;Y2;X8\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"levels\"\nC;Y1;X4;K\"Area1\"\n"
        "C;Y1;X5;K\"DataA1\"\nC;Y1;X6;K\"DataB1\"\n"
        "C;Y1;X7;K\"DataE1\"\nC;Y1;X8;K\"targs\"\n"
        "C;Y2;X1;K\"Advm\"\nC;Y2;X2;K\"Advm\"\n"
        "C;Y2;X3;K1\nC;Y2;X4;K160\nC;Y2;X5;K25\n"
        "C;Y2;X6;K35\nC;Y2;X7;K90\n"
        "C;Y2;X8;K\"air,ground,enemy,friend\"\nE\n";
    slkTestData_t *rows, *old;
    edict_t *caster, *enemy, *missile = NULL;
    abilityitem_t item = S_AbilityItem(FS_SLKKey("Advm"));
    spellTarget_t st;
    abilityCall_t call;
    uint32_t const buff = FS_SLKKey("Binf");
    reset_entities(); setup_test_world();
    rows = parse_slk_string(slk); old = G_SetSLKRows("AbilityData", rows);
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    caster = alloc_test_unit(MAKEFOURCC('u','b','s','p'), 0, 0);
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 250, 0);
    caster->s.player = 0; enemy->s.player = 1;
    caster->data.UnitAbilities = &abilities;
    caster->svflags |= SVF_MONSTER; enemy->svflags |= SVF_MONSTER;
    caster->mana.value = 0; caster->mana.max_value = 100;
    enemy->health.value = enemy->health.max_value = 400;
    T_NOT_NULL(unit_applystatus(enemy, &(status_application_t){
        .buff = "Binf", .level = 1, .duration = 30.0f,
        .buff_flags = WC3_STATUS_BUFF_MAGICAL | WC3_STATUS_BUFF_POSITIVE }));
    T_EQ(G_UnitStatusLevel(enemy, buff), 1u);
    st = MAKE(spellTarget_t, .type = SPELL_TARGET_POINT, .point = enemy->s.origin2);
    call = MAKE(abilityCall_t, .item = &item, .target = &st);
    T_ASSERT(S_AbilityMessage(caster, A_EXECUTE, &call));
    FILTER_EDICTS(ent, ent->owner == caster && ent->movetype == MOVETYPE_FLYMISSILE) {
        missile = ent;
        break;
    }
    T_NOT_NULL(missile);
    T_EQ(G_UnitStatusLevel(enemy, buff), 1u);
    if (missile) {
        T_ASSERT(missile->aiflags & AI_PROJECTILE_FIXED_TARGET);
        T_EQ(missile->s.model, G_RegisterModel("TestUI\\Models\\anim_projectile.mdx"));
        FOR_LOOP(i, 20) {
            if (!missile->inuse || missile->movetype != MOVETYPE_FLYMISSILE) break;
            G_RunEntity(missile);
        }
        T_EQ(G_UnitStatusLevel(enemy, buff), 0u);
        T_FEQ(caster->mana.value, 35, 0.001f);
        if (missile->inuse) {
            T_ASSERT(missile->aiflags & AI_PROJECTILE_DEATH_PRESENTATION);
            T_STREQ(missile->animation->name, "Death");
        }
    }
    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

#endif /* BZ_TESTS */
