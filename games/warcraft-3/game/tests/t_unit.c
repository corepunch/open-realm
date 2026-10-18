/*
 * t_unit.c — In-engine unit lifecycle and order tests.
 *
 * Runs inside the real game module via +dedicated 1 +test 'wc3_unit.*'.
 * The real gi, edict pool, and unit data tables are available.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"
#include "../game/skills/s_skills.h"

/* Helpers defined in t_utils.c */
edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
bool run_test_jass(cstring_t);
void CM_SetupTestPathmap(uint32_t width, uint32_t height, uint8_t const *cells);

/* Forward declarations for functions in m_unit.c without a public header. */
void unit_stand(edict_t *self);
void unit_birth(edict_t *self);
void unit_die(edict_t *self, edict_t *attacker);
void unit_begin_decay(edict_t *self);
void unit_decay_think(edict_t *self);
void unit_entercombat(edict_t *self, edict_t *target);
void unit_leavecombat(edict_t *self);
bool unit_affectingcombat(edict_t *self);
bool unit_issuetargetorder(edict_t *self, cstring_t order, edict_t *target);
bool unit_issueorder(edict_t *self, cstring_t order, vec2_t const *point);
bool unit_issueimmediateorder(edict_t *self, cstring_t order);
bool unit_additem(edict_t *edict, edict_t *item);
bool unit_additemtoslot(edict_t *edict, edict_t *item, uint32_t slot);
slkTestData_t *parse_slk_string(cstring_t slk_text);
void free_slk_rows(slkTestData_t *rows);

static uint32_t create_checkpoint_count;
static edict_t *create_checkpoint_unit;
static vec2_t create_checkpoint_point;
static void create_unit_checkpoint(void) {
    edict_t *unit = g_edicts + globals.num_edicts - 1;
    create_checkpoint_unit = unit;
    create_checkpoint_count++;
    T_ASSERT(unit->inuse);
    T_EQ(unit->class_id, MAKEFOURCC('h','f','o','o'));
    T_FEQ(unit->s.origin2.x, create_checkpoint_point.x, 0);
    T_FEQ(unit->s.origin2.y, create_checkpoint_point.y, 0);
    T_FEQ(unit->health.value, unit->health.max_value, 0);
    T_ASSERT(unit->own_seq != 0);
    T_NOT_NULL(unit->currentmove);
    if (unit->currentmove) T_STREQ(unit->currentmove->animation, "stand");
    T_EQ(level.time, 1234);
    T_EQ(level.pathing_random.sum, 2137);
}
TEST(wc3_unit, public_creation_checkpoint_sees_completed_unit_and_same_simulation_clock) {
    reset_entities();
    setup_test_world();
    struct game_import old = gi;
    gi.FrameCheckpoint = create_unit_checkpoint;
    create_checkpoint_count = 0;
    level.time = 1234;
    level.pathing_random.sum = 2137;
    FOR_LOOP(i, 8) {
        create_checkpoint_point = (vec2_t){64 + i * 128, 64};
        create_checkpoint_unit = NULL;
        edict_t *unit = unit_create(0, MAKEFOURCC('h','f','o','o'), &create_checkpoint_point, 37);
        T_NOT_NULL(unit);
        T_EQ(create_checkpoint_count, i + 1);
        T_EQ(unit, create_checkpoint_unit);
    }
    gi = old;
    reset_entities();
    setup_test_world();
}

static int selection_sound_index_77(cstring_t path) {
    (void)path;
    return 77;
}

TEST(wc3_unit, static_ability_membership_is_compiled_without_truncating_authored_list) {
    char list[1024] = {0}, code[5] = {0};
    FOR_LOOP(i, 96) {
        snprintf(code, sizeof(code), "A%03u", i);
        strlcat(list, code, sizeof(list));
        strlcat(list, ",", sizeof(list));
    }
    strlcat(list, "invalid,\"AHad\",  AInv,AHad", sizeof(list));
    UnitAbilities_t row = { .abilList = list };
    edict_t unit = { .data.UnitAbilities = &row };
    T_ASSERT(G_ActorHasSkill(&unit, "A095"));
    T_ASSERT(G_ActorHasSkill(&unit, "AHad"));
    T_ASSERT(G_ActorHasSkill(&unit, "AInv"));
    G_TestStaticAbilityTokens(true);
    FOR_LOOP(i, 1024) {
        T_ASSERT(G_ActorHasSkill(&unit, "A095"));
        T_ASSERT(!G_ActorHasSkill(&unit, "ZZZZ"));
    }
    T_EQ(G_TestStaticAbilityTokens(false), 0);
    unit.abilities.removed[unit.abilities.removed_count++] = MAKEFOURCC('A','0','9','5');
    T_ASSERT(!G_ActorHasSkill(&unit, "A095"));
    unit.abilities.added[unit.abilities.added_count++] = MAKEFOURCC('Z','Z','Z','Z');
    T_ASSERT(G_ActorHasSkill(&unit, "ZZZZ"));
    row.abilList = "Amgr,AInv";
    T_ASSERT(!G_ActorHasSkill(&unit, "AHad"));
    T_ASSERT(G_ActorHasSkill(&unit, "Amgr"));
}

extern uint32_t G_TestAuthoredMembershipVisits(bool reset);
TEST(wc3_unit, negative_authored_membership_has_a_constant_rejection_filter) {
    UnitAbilities_t row = { .abilList = "AInv,Amov,Aatk,AHad" };
    edict_t unit = { .data.UnitAbilities = &row };
    uint32_t count;
    uint32_t const *codes = G_UnitAbilityCodes(&row, &count);
    T_EQ(count, 4);
    G_TestAuthoredMembershipVisits(true);
    FOR_LOOP(i, 256) {
        uint32_t code = MAKEFOURCC('A', '0' + i / 100, '0' + (i / 10) % 10, '0' + i % 10);
        bool expected = false;
        FOR_LOOP(j, count) expected |= codes[j] == code;
        T_EQ(G_ActorHasAbilityCode(&unit, code), expected);
    }
    /* Collisions must confirm the original rawcode; most misses need no scan. */
    T_ASSERT(G_TestAuthoredMembershipVisits(false) < 128);
    FOR_LOOP(i, count) T_ASSERT(G_ActorHasAbilityCode(&unit, codes[i]));
    unit.abilities.removed[unit.abilities.removed_count++] = codes[0];
    T_ASSERT(!G_ActorHasAbilityCode(&unit, codes[0]));
    uint32_t added = MAKEFOURCC('Z','Z','Z','Z');
    unit.abilities.added[unit.abilities.added_count++] = added;
    T_ASSERT(G_ActorHasAbilityCode(&unit, added));
    row.abilList = "ZZZZ,Aslo";
    T_ASSERT(G_ActorHasSkill(&unit, "Aslo"));
    T_ASSERT(!G_ActorHasSkill(&unit, "AHad"));
}

TEST(wc3_unit, compiled_ability_arrays_survive_nested_lookups_and_preserve_outer_token) {
    UnitAbilities_t first = { .abilList = "AInv,Amgr,Ahar" };
    edict_t unit = { .data.UnitAbilities = &first };
    static cstring_t const expected[] = { "AInv", "Amgr", "Ahar" };
    uint32_t at = 0;
    PARSE_LIST(first.abilList, token, parse_segment) {
        T_ASSERT(G_ActorHasSkill(&unit, token));
        T_STREQ(token, expected[at++]);
    }
    T_EQ(at, 3);
    uint32_t count;
    uint32_t const *codes = G_UnitAbilityCodes(&first, &count);
    T_EQ(count, 3);
    UnitAbilities_t *other = calloc(1024, sizeof(*other));
    T_NOT_NULL(other);
    if (other) {
        FOR_LOOP(i, 1024) {
            other[i].abilList = "AHad,AOae";
            uint32_t other_count;
            uint32_t const *decoded = G_UnitAbilityCodes(other + i, &other_count);
            T_EQ(other_count, 2);
            T_EQ(decoded[0], MAKEFOURCC('A','H','a','d'));
            T_EQ(decoded[1], MAKEFOURCC('A','O','a','e'));
        }
        T_EQ(codes[0], MAKEFOURCC('A','I','n','v'));
        T_EQ(codes[1], MAKEFOURCC('A','m','g','r'));
        T_EQ(codes[2], MAKEFOURCC('A','h','a','r'));
        free(other);
    }
}

TEST(wc3_unit, scoped_ability_membership_rejects_misses_and_confirms_collisions) {
    UnitAbilities_t row = { .abilList = "AInv,Amov,Aatk,AHad" };
    edict_t unit = { .data.UnitAbilities = &row };
    unitStatusQuery_t scope;
    G_BeginUnitStatusQuery(&unit, &scope);
    T_EQ(scope.ability_count, 4);
    G_TestAuthoredMembershipVisits(true);
    FOR_LOOP(i, 256) {
        uint32_t code = MAKEFOURCC('A', '0' + i / 100, '0' + (i / 10) % 10, '0' + i % 10);
        T_ASSERT(!G_ActorHasAbilityCode(&unit, code));
    }
    T_ASSERT(G_TestAuthoredMembershipVisits(false) < 128);
    FOR_LOOP(i, scope.ability_count) T_ASSERT(G_ActorHasAbilityCode(&unit, scope.abilities[i]));
    G_EndUnitStatusQuery(&scope);
    /* Runtime ownership still takes precedence over authored rejection. */
    uint32_t added = MAKEFOURCC('Z','Z','Z','Z');
    unit.abilities.added[unit.abilities.added_count++] = added;
    unit.abilities.removed[unit.abilities.removed_count++] = MAKEFOURCC('A','I','n','v');
    G_BeginUnitStatusQuery(&unit, &scope);
    T_ASSERT(G_ActorHasAbilityCode(&unit, added));
    T_ASSERT(!G_ActorHasSkill(&unit, "AInv"));
    G_EndUnitStatusQuery(&scope);
    row.abilList = "ZZZZ,Aslo";
    G_BeginUnitStatusQuery(&unit, &scope);
    T_ASSERT(G_ActorHasSkill(&unit, "Aslo"));
    T_ASSERT(!G_ActorHasSkill(&unit, "AHad"));
    G_EndUnitStatusQuery(&scope);
}

TEST(wc3_unit, cargo_and_speed_queries_do_not_reparse_authored_ability_lists) {
    setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),64,64);
    UnitAbilities_t row={.abilList="AInv,Adef,Amov"};
    unit->data.UnitAbilities=&row;
    S_CargoInitUnit(unit);S_UnitMoveSpeed(unit);
    G_TestStaticAbilityTokens(true);
    for(uint32_t i=0;i<128;i++) {
        S_CargoInitUnit(unit);
        T_ASSERT(isfinite(S_UnitMoveSpeed(unit)));
    }
    T_EQ(G_TestStaticAbilityTokens(false),0);
}

TEST(wc3_unit, compiled_ability_tokens_keep_name_grammar_and_refresh_behavior_mapping) {
    UnitAbilities_t row={.abilList="A001,\"A001Suffix\",A,  AInv,A001"};
    AbilityData_t first={.id=MAKEFOURCC('A','0','0','1'),.code=MAKEFOURCC('A','g','l','d')};
    slkTestData_t table={.rows=&first,.count=1};
    slkTestData_t *old=G_SetSLKRows("AbilityData",&table);
    uint32_t count;
    unitAbilityToken_t const *tokens=G_UnitAbilityTokens(&row,&count);
    T_EQ(count,5);
    T_EQ(tokens[0].base,first.code);T_EQ(tokens[1].base,first.code);
    T_EQ(tokens[1].length,10);T_EQ(tokens[2].code,'A');T_EQ(tokens[2].length,1);
    T_EQ(tokens[3].code,MAKEFOURCC('A','I','n','v'));
    uint32_t const *codes=G_UnitAbilityCodes(&row,&count);
    T_EQ(count,3);T_EQ(codes[0],first.id);T_EQ(codes[2],first.id);
    AbilityData_t second={.id=first.id,.code=MAKEFOURCC('A','r','t','n')};
    slkTestData_t replacement={.rows=&second,.count=1};
    slkTestData_t *previous=G_SetSLKRows("AbilityData",&replacement);
    unitAbilityToken_t const *updated=G_UnitAbilityTokens(&row,&count);
    T_EQ(updated[0].base,second.code);
    T_EQ(tokens[0].base,first.code); /* Borrowed outer arrays survive a metadata callback. */
    G_SetSLKRows("AbilityData",old);free(previous);free(old);
}

TEST(wc3_unit, type_resource_and_blight_queries_share_compiled_authored_lists) {
    setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),64,64);
    /* A long token still selects AbilityData by its first four characters.
     * Literal Return Resources aliases require the entire four-character token. */
    UnitAbilities_t row={.abilList="AInv,Adef,Amov,ArgdSuffix"};
    row.id=unit->class_id;
    slkTestData_t table={.rows=&row,.count=1};
    slkTestData_t *old=G_SetSLKRows("UnitAbilities",&table);
    bool returns_gold=S_UnitTypeReturnsGold(unit->class_id);
    unit->data.UnitAbilities=&row;
    S_UnitTypeIsGoldMine(unit->class_id);S_UnitTypeReturnsGold(unit->class_id);
    S_UnitAbilityEvent(unit,A_UNIT_INIT);
    G_TestStaticAbilityTokens(true);
    FOR_LOOP(i,128) {
        T_ASSERT(!S_UnitTypeIsGoldMine(unit->class_id));
        T_EQ(S_UnitTypeReturnsGold(unit->class_id),returns_gold);
        S_UnitAbilityEvent(unit,A_UNIT_INIT);
    }
    T_EQ(G_TestStaticAbilityTokens(false),0);
    row.abilList="  AInv,\"Argd\",Amov";
    T_ASSERT(S_UnitTypeReturnsGold(unit->class_id));
    G_SetSLKRows("UnitAbilities",old);free(old);
}

TEST(wc3_unit, move_bonus_query_visits_only_subscribed_owners) {
    setup_test_world();
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','f','o','o'),64,64);
    UnitAbilities_t row={.abilList="AInv,Adef,Amov"};
    unit->data.UnitAbilities=&row;
    S_TestMoveBonusMessages(true);
    FOR_LOOP(i,128)T_FEQ(S_MoveSpeedBonus(unit),0,0);
    T_EQ(S_TestMoveBonusMessages(false),0);
    row.abilList="AInv,AIms,AIms";
    S_TestMoveBonusMessages(true);
    S_MoveSpeedBonus(unit);
    T_EQ(S_TestMoveBonusMessages(false),1);
    unit->abilities.removed[unit->abilities.removed_count++]=MAKEFOURCC('A','I','m','s');
    S_TestMoveBonusMessages(true);
    T_FEQ(S_MoveSpeedBonus(unit),0,0);
    T_EQ(S_TestMoveBonusMessages(false),0);
}

TEST(wc3_unit, numeric_ability_membership_preserves_four_character_validation) {
    edict_t unit={0};
    uint32_t codes[]={0x00000001,0x00766f41,0x416f0041,MAKEFOURCC('A','I','n','v')};
    char text[5]={0};
    unit.abilities.added_count=1;
    FOR_LOOP(i,sizeof(codes)/sizeof(*codes)) {
        unit.abilities.added[0]=codes[i];memcpy(text,codes+i,4);
        T_EQ(G_ActorHasAbilityCode(&unit,codes[i]),G_ActorHasSkill(&unit,text));
        T_EQ(G_UnitAbilityLevel(&unit,codes[i]),strlen(text)==4?1:0);
    }
}
TEST(wc3_unit, string_ability_membership_checks_exact_rawcode_length) {
    UnitAbilities_t row = { .abilList = "AInv" };
    edict_t unit = { .data.UnitAbilities = &row };
    cstring_t short_ids[] = { NULL, "", "A", "AI", "AIn" };
    FOR_LOOP(i, sizeof(short_ids) / sizeof(*short_ids)) T_ASSERT(!G_ActorHasSkill(&unit, short_ids[i]));
    T_ASSERT(G_ActorHasSkill(&unit, "AInv"));
    T_ASSERT(!G_ActorHasSkill(&unit, "AInvx"));
    T_ASSERT(!G_ActorHasSkill(&unit, "AInv_a_long_name_is_not_a_rawcode"));
}
TEST(wc3_unit, runtime_ability_tail_slots_preserve_removal_precedence) {
    UnitAbilities_t row = { .abilList = "AInv,Adef" };
    edict_t unit = { .data.UnitAbilities = &row };
    uint32_t code = MAKEFOURCC('A','h','a','r');
    unit.abilities.added_count = MAX_ABILITIES;
    unit.abilities.added[MAX_ABILITIES - 1] = code;
    T_ASSERT(G_ActorHasAbilityCode(&unit, code));
    T_EQ(G_UnitAbilityLevel(&unit, code), 1);
    unit.abilities.removed_count = MAX_ABILITIES;
    unit.abilities.removed[MAX_ABILITIES - 1] = code;
    T_ASSERT(!G_ActorHasAbilityCode(&unit, code));
    T_EQ(G_UnitAbilityLevel(&unit, code), 0);
    code = MAKEFOURCC('A','I','n','v');
    unit.abilities.removed[MAX_ABILITIES - 1] = code;
    T_ASSERT(!G_ActorHasAbilityCode(&unit, code));
    unit.abilities.added[0] = code;
    T_ASSERT(!G_ActorHasAbilityCode(&unit, code));
    unit.abilities.removed[MAX_ABILITIES - 1] = 0;
    T_ASSERT(G_ActorHasAbilityCode(&unit, code));
}
static int selection_sound_index_77_alias(cstring_t path, cstring_t alias) { (void)alias; return selection_sound_index_77(path); }

static uint32_t visual_resource_model_calls, visual_resource_file_calls;
static uint64_t visual_resource_revision;
static uint64_t visual_resource_generation(void) { return visual_resource_revision; }
static int visual_resource_model(cstring_t filename) {
    T_STREQ(filename, "Units\\Human\\Test\\Test.mdx");
    visual_resource_model_calls++;
    return visual_resource_revision == 17 ? 77 : 78;
}
static bool visual_resource_exists(cstring_t filename) {
    T_STREQ(filename, "ReplaceableTextures\\Shadows\\Probe.blp");
    visual_resource_file_calls++;
    return true;
}
static int visual_resource_image(cstring_t filename) {
    T_STREQ(filename, "ReplaceableTextures\\Shadows\\Probe.blp");
    return 33;
}
TEST(wc3_unit, unit_visual_resources_are_shared_until_media_revision_changes) {
    struct game_import old = gi;
    UnitUI_t ui = { .modelFile = "Units\\Human\\Test\\Test", .unitShadowTexture = "Probe",
        .shadowWidth = 96, .shadowHeight = 80, .shadowCenterX = 48, .shadowCenterY = 40 };
    gi.MediaRevision = visual_resource_generation;
    gi.ModelIndex = visual_resource_model;
    gi.FileExists = visual_resource_exists;
    gi.ImageIndex = visual_resource_image;
    visual_resource_revision = 17;
    visual_resource_model_calls = visual_resource_file_calls = 0;
    FOR_LOOP(i, 32) {
        edict_t unit = { .class_id = MAKEFOURCC('h','f','o','o'), .data.UnitUI = &ui };
        unit_register_visuals(&unit);
        T_EQ(unit.s.model, 77);
#ifndef USE_SHADOWMAPS
        T_EQ(unit.s.shadow, 33);
        T_EQ(unit.s.shadow_rect, ShadowPackRect(48, 40, 96, 80));
#endif
    }
    T_EQ(visual_resource_model_calls, 1);
    T_EQ(visual_resource_file_calls, 1);
    visual_resource_revision++;
    edict_t unit = { .class_id = MAKEFOURCC('h','f','o','o'), .data.UnitUI = &ui };
    unit_register_visuals(&unit);
    T_EQ(unit.s.model, 78);
    T_EQ(visual_resource_model_calls, 2);
    T_EQ(visual_resource_file_calls, 2);
    gi = old;
}

TEST(wc3_unit, spawn_classification_is_shared_and_refreshes_bound_rows_and_metadata) {
    setup_test_world();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 64);
    G_ResetUnitResources(); unit_spawn_trait_builds = 0;
    unitSpawnTraits_t first = *unit_spawn_type_traits(unit);
    FOR_LOOP(i, 128) {
        unitSpawnTraits_t const *traits = unit_spawn_type_traits(unit);
        T_EQ(traits->flags, first.flags); T_EQ(traits->collision, first.collision);
        T_EQ(traits->target, first.target);
    }
    T_EQ(unit_spawn_trait_builds, 1);
    UnitData_t data = *unit->data.UnitData;
    data.targetType = "air";
    unit->data.UnitData = &data;
    T_EQ(unit_spawn_type_traits(unit)->target, TARG_AIR);
    T_EQ(unit_spawn_trait_builds, 2);
    UnitBalance_t balance = *unit->data.UnitBalance;
    balance.id = unit->class_id; balance.isBuilding = true; balance.collision = 99; balance.has_collision = true;
    slkTestData_t table = { .rows = &balance, .count = 1 };
    slkTestData_t *old = G_SetSLKRows("UnitBalance", &table);
    unitSpawnTraits_t const *traits = unit_spawn_type_traits(unit);
    T_EQ(traits->runtime, UNIT_BALANCE_BUILDING);
    T_ASSERT(traits->flags & EF_BUILDING); T_EQ(traits->collision, 99);
    T_EQ(unit_spawn_trait_builds, 3);
    G_SetSLKRows("UnitBalance", old); free(old);
    reset_entities(); setup_test_world();
}

TEST(wc3_unit, combat_text_classification_refreshes_rows_and_columns) {
    UnitBalance_t balance = { .defenseType = "hero" };
    UnitWeapons_t weapons = { .attack1.attackType = "pierce", .attack1.weaponType = "missile",
        .attack2.attackType = "siege", .attack2.weaponType = "artillery" };
    G_ResetUnitResources(); unit_combat_type_builds = 0;
    FOR_LOOP(i, 64) {
        unitCombatTypes_t const *types = unit_spawn_combat_types(&balance, &weapons);
        T_EQ(types->defense, FindEnumValue(balance.defenseType, defense_type));
        T_EQ(types->attack[0], FindEnumValue(weapons.attack1.attackType, attack_type));
        T_EQ(types->attack[1], FindEnumValue(weapons.attack2.attackType, attack_type));
        T_EQ(types->weapon[0], FindEnumValue(weapons.attack1.weaponType, weapon_type));
        T_EQ(types->weapon[1], FindEnumValue(weapons.attack2.weaponType, weapon_type));
    }
    T_EQ(unit_combat_type_builds, 1);
    balance.defenseType = "fort";
    weapons.attack1.attackType = "magic"; weapons.attack2.attackType = NULL;
    weapons.attack1.weaponType = "normal"; weapons.attack2.weaponType = "unknown";
    unitCombatTypes_t const *types = unit_spawn_combat_types(&balance, &weapons);
    T_EQ(types->defense, FindEnumValue("fort", defense_type));
    T_EQ(types->attack[0], FindEnumValue("magic", attack_type));
    T_EQ(types->attack[1], 0); T_EQ(types->weapon[0], FindEnumValue("normal", weapon_type));
    T_EQ(types->weapon[1], 0); T_EQ(unit_combat_type_builds, 2);
    UnitWeapons_t other = weapons;
    other.attack2.weaponType = "missile";
    types = unit_spawn_combat_types(&balance, &other);
    T_EQ(types->weapon[1], FindEnumValue("missile", weapon_type));
    T_EQ(unit_combat_type_builds, 3);
    G_ResetUnitResources();
}

static int projectile_resource_model(cstring_t filename) {
    T_STREQ(filename, "Units\\Human\\Test\\Test.mdx");
    visual_resource_model_calls++;
    return visual_resource_revision == 17 ? 77 : 78;
}
TEST(wc3_unit, projectile_resources_share_registration_and_preserve_unassigned_attack_state) {
    struct game_import old_import = gi;
    UnitProfile_t profile = { .id = MAKEFOURCC('h','f','o','o') };
    profile.attack[0].art = "Units\\Human\\Test\\Test.mdx";
    profile.attack[0].arc = .25f; profile.attack[0].speed = 900;
    slkTestData_t table = { .rows = &profile, .count = 1 };
    slkTestData_t *old_profile = G_SetProfileRows(&table);
    gi.MediaRevision = visual_resource_generation; gi.ModelIndex = projectile_resource_model;
    visual_resource_revision = 17; visual_resource_model_calls = 0;
    G_ResetUnitResources(); unit_projectile_resource_builds = 0;
    FOR_LOOP(i, 128) {
        edict_t unit = { .class_id = profile.id };
        S_AttackProfileWrite(&unit, 0)->weapon = WPN_MISSILE;
        S_AttackProfileWrite(&unit, 0)->temporaryDamageBonus = 73;
        S_AttackProfileWrite(&unit, 1)->origin = (vec3_t){ 3, 4, 5 }; S_AttackProfileWrite(&unit, 1)->projectile.model = 119;
        unit_register_projectiles(&unit);
        T_EQ(S_AttackProfileRead(&unit, 0)->projectile.model, 77); T_EQ(S_AttackProfileRead(&unit, 0)->projectile.arc, .25f);
        T_EQ(S_AttackProfileRead(&unit, 0)->projectile.speed, 900); T_EQ(S_AttackProfileRead(&unit, 0)->temporaryDamageBonus, 73);
        T_EQ(S_AttackProfileRead(&unit, 1)->origin.x, 3); T_EQ(S_AttackProfileRead(&unit, 1)->projectile.model, 119);
        G_FreeAttackOne(&unit); G_FreeAttackTwo(&unit);
    }
    T_EQ(visual_resource_model_calls, 1); T_EQ(unit_projectile_resource_builds, 1);
    visual_resource_revision++;
    edict_t unit = { .class_id = profile.id }; S_AttackProfileWrite(&unit, 0)->weapon = WPN_ARTILLERY;
    unit_register_projectiles(&unit);
    T_EQ(S_AttackProfileRead(&unit, 0)->projectile.model, 78); T_EQ(visual_resource_model_calls, 2);
    profile.attack[0].speed = 450;
    slkTestData_t changed = { .rows = &profile, .count = 1 };
    slkTestData_t *previous = G_SetProfileRows(&changed);
    unit_register_projectiles(&unit);
    T_EQ(S_AttackProfileRead(&unit, 0)->projectile.speed, 450); T_EQ(visual_resource_model_calls, 3);
    G_FreeAttackOne(&unit); G_FreeAttackTwo(&unit);
    G_SetProfileRows(old_profile); free(previous); free(old_profile); gi = old_import;
    G_ResetUnitResources();
}


static char death_sound_path[256];
static cstring_t death_sound_existing = "Units\\Human\\Test\\TestDeath1.wav";
static uint32_t death_sound_reads;
static bool death_sound_exists(cstring_t path) {
    return !strcmp(path, death_sound_existing);
}
static handle_t death_sound_probe(cstring_t path, uint32_t *size) {
    death_sound_reads++;
    if (strcmp(path, death_sound_existing)) return NULL;
    *size = 1;
    return malloc(1);
}
static int death_sound_index(cstring_t path) {
    strlcpy(death_sound_path, path, sizeof(death_sound_path));
    return 77;
}

TEST(wc3_unit, identical_units_share_sound_resources_without_repeated_file_probes) {
    struct game_import old = gi;
    UnitUI_t ui = { .soundLabel = "Test", .modelFile = "Units\\Human\\Test\\Test" };
    UnitWeapons_t weapons = { 0 };
    death_sound_existing = "Units\\Human\\Test\\TestDeath1.wav";
    death_sound_reads = 0;
    unit_sound_resource_builds = 0;
    visual_resource_revision = 41;
    gi.MediaRevision = visual_resource_generation;
    gi.FileExists = death_sound_exists;
    gi.SoundIndex = death_sound_index;
    unitSoundProfile_t const *profile = NULL;
    FOR_LOOP(i, 32) {
        edict_t ent = { .class_id = MAKEFOURCC('h','f','o','o'), .data.UnitUI = &ui, .data.UnitWeapons = &weapons };
        unit_register_sounds(&ent);
        T_EQ(G_UnitSoundProfile(&ent)->death, 77);
        if (i) T_ASSERT(ent.sound_profile == profile);
        profile = ent.sound_profile;
    }
    /* Count actual VFS probes, independently of payload reads. */
    T_EQ(unit_sound_resource_builds, 1);
    visual_resource_revision++;
    edict_t ent = { .class_id = MAKEFOURCC('h','f','o','o'), .data.UnitUI = &ui, .data.UnitWeapons = &weapons };
    unit_register_sounds(&ent);
    T_EQ(G_UnitSoundProfile(&ent)->death, 77);
    T_EQ(unit_sound_resource_builds, 2);
    ent.sound.owner_pending = 117;
    unit_register_sounds(&ent);
    T_EQ(ent.sound.owner_pending, 117);
    T_EQ(unit_sound_resource_builds, 3);
    gi = old;
}

TEST(wc3_unit, death_sound_uses_existing_numbered_asset) {
    struct game_import old = gi;
    UnitUI_t ui = { .soundLabel = "Test", .modelFile = "Units\\Human\\Test\\Test" };
    UnitWeapons_t weapons = { 0 };
    edict_t ent = { .data.UnitUI = &ui, .data.UnitWeapons = &weapons };

    setup_test_world();
    death_sound_existing = "Units\\Human\\Test\\TestDeath1.wav";
    death_sound_path[0] = '\0';
    gi.ReadFile = death_sound_probe;
    gi.FileExists = death_sound_exists;
    gi.SoundIndex = death_sound_index;
    G_RegisterUnitSounds(&ent);
    T_STREQ(death_sound_path, "Units\\Human\\Test\\TestDeath1.wav");
    T_EQ(G_UnitSoundProfile(&ent)->death, 77);
    gi = old;
}

TEST(wc3_unit, scoped_status_queries_preserve_slots_deadlines_and_nested_owners) {
    reset_entities();setup_test_world();
    edict_t *first=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
    edict_t *second=alloc_test_unit(MAKEFOURCC('h','f','o','o'),0,0);
    uint32_t code=MAKEFOURCC('B','O','w','k');
    G_EnsureUnitStatusSlots(first); G_EnsureUnitStatusSlots(second);
    first->abilstatus[0]=(heroabilitystatus_t){.code=code,.level=1,.timestamp=100};
    first->abilstatus[MAX_UNIT_STATUSES-1]=(heroabilitystatus_t){.code=code,.level=3};
    second->abilstatus[1]=(heroabilitystatus_t){.code=code,.level=2};
    unitStatusQuery_t outer,inner;
    level.time=0;G_BeginUnitStatusQuery(first,&outer);
    T_EQ(G_UnitStatusLevel(first,code),1);
    T_EQ(G_UnitStatusLevel(first,MAKEFOURCC('B','s','l','o')),0);
    G_BeginUnitStatusQuery(second,&inner);
    T_EQ(G_UnitStatusLevel(second,code),2);
    T_EQ(G_UnitStatusLevel(first,code),1);
    G_EndUnitStatusQuery(&inner);
    level.time=100;
    T_EQ(G_UnitStatusLevel(first,code),3);
    T_EQ(G_UnitStatusLevel(second,code),2);
    G_EndUnitStatusQuery(&outer);
    first->abilstatus[2]=(heroabilitystatus_t){.code=MAKEFOURCC('B','s','l','o'),.level=4};
    G_BeginUnitStatusQuery(first,&outer);
    T_EQ(G_UnitStatusLevel(first,MAKEFOURCC('B','s','l','o')),4);
    G_EndUnitStatusQuery(&outer);
    reset_entities();setup_test_world();
}

extern uint32_t G_TestCompiledAbilityQueries(bool reset);
extern uint32_t G_TestUnitStatusQueries(bool reset);
extern uint32_t G_TestUnitStatusSlotVisits(bool reset);
TEST(wc3_unit, empty_unit_status_queries_do_not_walk_vacant_records) {
    reset_entities(); setup_test_world();
    edict_t *unit = unit_create(0, MAKEFOURCC('h','f','o','o'), &MAKE(vec2_t, 64, 64), 17);
    T_NOT_NULL(unit);
    T_NULL(unit->abilstatus);
    G_TestUnitStatusSlotVisits(true);
    FOR_LOOP(i, 128) T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','c','r','i')), 0);
    T_EQ(G_TestUnitStatusSlotVisits(false), 0);
    unit_refreshstatusflags(unit);
    unit_updatestatuses(unit);
    T_EQ(unit_removebuffs(unit, true, true, false, false, true, true, false), 0);
    T_NULL(unit->abilstatus);
    unit_addtimedstatus(unit, "Bcri", 3, 1);
    heroabilitystatus_t *slots = unit->abilstatus;
    T_NOT_NULL(slots);
    T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','c','r','i')), 3);
    level.time += 1000;
    T_EQ(G_UnitStatusLevel(unit, MAKEFOURCC('B','c','r','i')), 0);
    unit_updatestatuses(unit);
    T_EQ(unit->abilstatus, slots);
    G_FreeEdict(unit);
    T_NULL(unit->abilstatus);
    edict_t *next = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 64);
    G_EnsureUnitStatusSlots(next);
    T_EQ(next->abilstatus, slots);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        T_EQ(next->abilstatus[i].level, 0);
        T_NULL(next->abilstatus[i].source);
    }
    reset_entities(); setup_test_world();
}
TEST(wc3_unit, public_speed_uses_empty_status_discovery_and_live_custom_windwalk) {
    reset_entities(); setup_test_world();
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 64);
    UnitBalance_t balance = *unit->data.UnitBalance;
    balance.speed = 331.75f; balance.minSpeed = 1; balance.maxSpeed = 522;
    unit->data.UnitBalance = &balance; unit->unitinfo.MoveSpeed = balance.speed;
    AbilityData_t windwalk = { .id = MAKEFOURCC('A','O','w','k'), .code = MAKEFOURCC('A','O','w','k') };
    windwalk.level[0].data[0].number = 19;
    slkTestData_t table = { .rows = &windwalk, .count = 1 };
    slkTestData_t *old = G_SetSLKRows("AbilityData", &table);
    S_UnitMoveSpeed(unit);
    G_TestUnitStatusQueries(true);
    FOR_LOOP(i, 128) T_FEQ(S_UnitMoveSpeed(unit), 331.75f, 0);
    T_EQ(G_TestUnitStatusQueries(false), 0);
    G_EnsureUnitStatusSlots(unit);
    unit->abilstatus[MAX_UNIT_STATUSES - 1] = (heroabilitystatus_t){
        .code = MAKEFOURCC('B','O','w','k'), .level = 1, .timestamp = 100 };
    level.time = 0;
    T_FEQ(S_UnitMoveSpeed(unit), 331.75f * 1.19f, 0);
    T_ASSERT(G_TestUnitStatusQueries(false) > 0);
    level.time = 100;
    T_FEQ(S_UnitMoveSpeed(unit), 331.75f, 0);
    unit->abilstatus[MAX_UNIT_STATUSES - 1].level = 0;
    G_TestUnitStatusQueries(true);
    T_FEQ(S_UnitMoveSpeed(unit), 331.75f, 0);
    T_EQ(G_TestUnitStatusQueries(false), 0);
    G_SetSLKRows("AbilityData", old); free(old);
    reset_entities(); setup_test_world();
}
TEST(wc3_unit, pure_queries_share_ownership_discovery_and_preserve_rank_status_precedence) {
    UnitAbilities_t rows[] = { { .abilList = "Ahar,Amov" }, { .abilList = "AHhb" } };
    edict_t first = { .data.UnitAbilities = rows }, second = { .data.UnitAbilities = rows + 1 };
    uint32_t harvest = MAKEFOURCC('A','h','a','r'), heal = MAKEFOURCC('A','H','h','b');
    uint32_t slow = MAKEFOURCC('A','s','l','o');
    unitStatusQuery_t outer, inner;
    first.heroabilities[MAX_HERO_ABILITIES - 1] = (heroability_t){ .code = heal, .level = 2 };
    heroabilitystatus_t statuses[MAX_UNIT_STATUSES] = { 0 };
    first.abilstatus = statuses;
    first.abilstatus[0] = (heroabilitystatus_t){ .code = harvest, .level = 3, .timestamp = 10 };
    first.abilstatus[MAX_UNIT_STATUSES - 1] = (heroabilitystatus_t){ .code = harvest, .level = 4 };
    first.abilities.removed[first.abilities.removed_count++] = harvest;
    second.abilities.added[second.abilities.added_count++] = slow;
    uint32_t time = level.time;
    level.time = 0;
    G_BeginUnitStatusQuery(&first, &outer);
    G_TestCompiledAbilityQueries(true);
    FOR_LOOP(i, 128) {
        T_EQ(G_UnitAbilityLevel(&first, harvest), 3);
        T_EQ(G_UnitAbilityLevel(&first, heal), 2);
        T_EQ(G_UnitAbilityLevel(&first, slow), 0);
        T_ASSERT(G_ActorHasSkill(&first, "Amov"));
        T_ASSERT(!G_ActorHasSkill(&first, "Ahar"));
    }
    T_EQ(G_TestCompiledAbilityQueries(false), 0);
    G_BeginUnitStatusQuery(&second, &inner);
    G_TestCompiledAbilityQueries(true);
    T_EQ(G_UnitAbilityLevel(&second, heal), 1);
    T_EQ(G_UnitAbilityLevel(&second, slow), 1);
    T_EQ(G_TestCompiledAbilityQueries(false), 0);
    G_EndUnitStatusQuery(&inner);
    level.time = 10;
    T_EQ(G_UnitAbilityLevel(&first, harvest), 4);
    G_EndUnitStatusQuery(&outer);
    T_EQ(G_UnitAbilityLevel(&first, heal), 2);
    T_EQ(G_UnitAbilityLevel(&second, slow), 1);
    first.heroabilities[0] = (heroability_t){ .code = harvest, .level = 7 };
    rows[0].abilList = "Aslo";
    G_BeginUnitStatusQuery(&first, &outer);
    T_EQ(G_UnitAbilityLevel(&first, harvest), 7);
    T_EQ(G_UnitAbilityLevel(&first, slow), 1);
    T_ASSERT(!G_ActorHasSkill(&first, "Amov"));
    G_EndUnitStatusQuery(&outer);
    level.time = time;
}

TEST(wc3_unit, death_sound_uses_existing_unnumbered_asset) {
    struct game_import old = gi;
    UnitUI_t ui = { .soundLabel = "Test", .modelFile = "Units\\Human\\Test\\Test" };
    UnitWeapons_t weapons = { 0 };
    edict_t ent = { .data.UnitUI = &ui, .data.UnitWeapons = &weapons };

    setup_test_world();
    death_sound_existing = "Units\\Human\\Test\\TestDeath.wav";
    death_sound_path[0] = '\0';
    gi.ReadFile = death_sound_probe;
    gi.FileExists = death_sound_exists;
    gi.SoundIndex = death_sound_index;
    G_RegisterUnitSounds(&ent);
    T_STREQ(death_sound_path, death_sound_existing);
    T_EQ(G_UnitSoundProfile(&ent)->death, 77);
    gi = old;
}

TEST(wc3_unit, spawning_sound_registration_does_not_read_audio_payload) {
    struct game_import old = gi;
    UnitUI_t ui = { .soundLabel = "Test", .modelFile = "Units\\Human\\Test\\Test" };
    UnitWeapons_t weapons = {0};
    setup_test_world();
    death_sound_existing = "Units\\Human\\Test\\TestDeath1.wav";
    death_sound_reads = 0;
    gi.ReadFile = death_sound_probe;
    gi.FileExists = death_sound_exists;
    gi.SoundIndex = death_sound_index;
    FOR_LOOP(i,1024) {
        edict_t ent = { .data.UnitUI = &ui, .data.UnitWeapons = &weapons };
        G_RegisterUnitSounds(&ent);
        T_EQ(G_UnitSoundProfile(&ent)->death,77);
    }
    T_EQ(death_sound_reads,0);
    gi = old;
}

static int order_sound_calls, order_sound_index;
static void order_sound_write(pfWriteType_t type, void const *value) { (void)type; (void)value; }
static void order_sound_unicast(edict_t *ent) { (void)ent; }
static void order_sound_capture(edict_t *ent, int channel, int index, float volume, float attenuation, float offset) {
    (void)ent; (void)channel; (void)volume; (void)attenuation; (void)offset;
    order_sound_calls++; order_sound_index = index;
}

static void order_sound_policy_capture(vec3_t const *origin, edict_t *ent, int channel, int index,
                                       float volume, float attenuation, float offset, soundPolicy_t const *policy) {
    T_ASSERT(policy && policy->request);
    T_EQ(policy->request, G_UnitResponseRequest(ent, index));
    order_sound_capture(ent, channel, index, volume, attenuation, offset);
}

/* A right-click must survive command dispatch and the next entity update. */
TEST(wc3_unit, smart_move_emits_selected_unit_response) {
    struct game_import old = gi;
    cstring_t command[] = { "smartpoint", "256", "256" };
    setup_test_world();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 64);
    edict_t *clent = &g_edicts[0];
    ent->movetype = MOVETYPE_STEP;
    ent->stand = unit_stand;
    G_SetEntitySelectionMask(ent, 1);
    {
        unitSoundProfile_t value = *G_UnitSoundProfile(ent);
        value.yes[0] = 118; value.num_yes = 1;
        G_SetUnitSoundProfile(ent, &value);
    }
    unit_stand(ent);
    gi.Write = order_sound_write; gi.unicast = order_sound_unicast; gi.Sound = order_sound_capture; gi.SoundPolicy = order_sound_policy_capture;
    order_sound_calls = order_sound_index = 0;
    G_ClientCommand(clent, 3, command);
    T_NOT_NULL(ent->goalentity);
    T_EQ(ent->sound.pending, 118);
    G_RunEntities();
    T_EQ(order_sound_calls, 1);
    T_EQ(order_sound_index, 118);
    T_EQ(ent->sound.pending, 0);
    gi = old;
}

/* Reset the entity pool between tests. */
static void reset_test_entities(void) {
    G_ResetWaypointCache();
    G_ClearEdictStorage(globals.max_edicts);
    globals.num_edicts = 0;
    globals.edicts = g_edicts;
}

/* Create a minimal unit edict with lifecycle callbacks wired up. */
static edict_t *make_unit(float x, float y) {
    static UnitWeapons_t const test_weapons = { .attacksEnabled = 3 };
    edict_t *ent = G_Spawn();
    ent->class_id       = MAKEFOURCC('h','p','e','a');
    G_BindEntityData(ent);
    ent->data.UnitWeapons = &test_weapons;
    ent->s.origin2      = (vec2_t){x, y};
    ent->s.origin.x     = x;
    ent->s.origin.y     = y;
    ent->s.origin.z     = 0;
    ent->s.model        = 1; /* non-zero so IS_HOLLOW is false */
    ent->movetype       = MOVETYPE_STEP;
    ent->collision      = 16.0f;
    ent->stand          = unit_stand;
    ent->birth          = unit_birth;
    ent->die            = unit_die;
    ent->health.value   = G_UnitBalance(ent->class_id)->maxHealth;
    ent->health.max_value = G_UnitBalance(ent->class_id)->maxHealth;
    ent->unitinfo.MoveSpeed = G_UnitBalance(ent->class_id)->speed;
    S_AttackProfileWrite(ent, 0)->type = ATK_NORMAL;
    S_AttackProfileWrite(ent, 0)->targetsAllowed = WC3_TARGET_FLAG_GROUND;
    ent->targtype = TARG_GROUND;
    unit_stand(ent);
    return ent;
}

static edict_t *make_inventory_unit(float x, float y) {
    edict_t *ent = make_unit(x, y);
    ent->class_id = MAKEFOURCC('H','p','a','l');
    G_BindEntityData(ent);
    return ent;
}

static edict_t *make_world_item(uint32_t class_id) {
    edict_t *item = G_Spawn();
    item->class_id = class_id;
    G_BindEntityData(item);
    item->s.model = 1;
    item->targtype = TARG_ITEM;
    if (!item->item) item->item = G_AllocItem();
    assert(item->item);
    item->item->in_world = true;
    item->item->inventory_slot = -1;
    return item;
}

static edict_t *unit_make_harvest_tree(float x, float y) {
    edict_t *tree = G_Spawn();
    tree->s.origin2 = (vec2_t){x, y};
    tree->s.origin.x = x;
    tree->s.origin.y = y;
    tree->targtype = TARG_TREE;
    tree->health.value = tree->health.max_value = 100.0f;
    return tree;
}

static edict_t *unit_make_harvest_goldmine(float x, float y) {
    static UnitAbilities_t const abilities = { .abilList = "Agld" };
    edict_t *mine = G_Spawn();
    mine->s.origin2 = (vec2_t){x, y};
    mine->s.origin.x = x;
    mine->s.origin.y = y;
    mine->data.UnitAbilities = &abilities;
    mine->resources = 12500;
    mine->health.value = mine->health.max_value = 1000.0f;
    return mine;
}

TEST(wc3_unit, shared_test_unit_starts_alive) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);

    T_ASSERT(ent->data.UnitBalance->maxHealth > 0.0f);
    T_FEQ(ent->health.max_value, ent->data.UnitBalance->maxHealth, 0.001f);
    T_FEQ(ent->health.value, ent->health.max_value, 0.001f);
    T_ASSERT(!M_IsDead(ent));
}

TEST(wc3_unit, locust_ability_applies_untargetable_collisionless_traits) {
    static UnitAbilities_t const locust = { .abilList = "Aloc" };
    static UnitAbilities_t const ordinary = { .abilList = "Amov,Aatk" };
    edict_t unit = { .collision = 16.0f, .data.UnitAbilities = &locust };
    edict_t control = { .collision = 16.0f, .data.UnitAbilities = &ordinary };

    G_ApplyUnitAbilityTraits(&unit);
    T_ASSERT(unit.s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(unit.invulnerable);
    T_FEQ(unit.collision, 0.0f, 0.001f);
    T_ASSERT(unit.no_pathing);

    G_ApplyUnitAbilityTraits(&control);
    T_ASSERT(!(control.s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!control.invulnerable);
    T_FEQ(control.collision, 16.0f, 0.001f);
    T_ASSERT(!control.no_pathing);
}

TEST(wc3_unit, selection_sound_registration_caches_all_responses) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X6;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y1;X4;K\"Priority\"\n"
        "C;Y1;X5;K\"Channel\"\n"
        "C;Y1;X6;K\"Flags\"\n"
        "C;Y2;X5;K1\n"
        "C;Y2;X6;K\"WANT3D,NODUPEUSERNAMES,CHANNELFULLPREEMPT,RANDOMPITCH\"\n"
        "C;Y2;X4;K1731\n"
        "C;Y2;X1;K\"FootmanWhat\"\n"
        "C;Y2;X2;K\"FootmanWhat1.wav,FootmanWhat2.wav,FootmanWhat3.wav,FootmanWhat4.wav\"\n"
        "C;Y2;X3;K\"Units\\Human\\Footman\\\"\n"
        "E\n";
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    slkTestData_t *sounds = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("UnitAckSounds", sounds);
    G_RegisterSelectSounds(ent, "Footman");
    T_EQ(G_UnitSoundProfile(ent)->num_select, 4);
    FOR_LOOP(i, G_UnitSoundProfile(ent)->num_select) T_ASSERT(G_UnitSoundProfile(ent)->select[i]);
    FOR_LOOP(i, G_UnitSoundProfile(ent)->num_select)
        T_EQ(G_SoundIndexPolicy(G_UnitSoundProfile(ent)->select[i])->priority, 1731);
    soundPolicy_t const *policy = G_SoundIndexPolicy(G_UnitSoundProfile(ent)->select[0]);
    T_NOT_NULL(policy);
    if (policy) {
        T_EQ(policy->priority, 1731); T_EQ(policy->group, 1);
        T_EQ(policy->max_channel, 3); T_EQ(policy->max_total, 24);
        T_EQ(policy->flags, SOUND_NO_DUPLICATE_USERS | SOUND_CHANNEL_PREEMPT);
    }
    G_ResetSoundPresentationState();
    T_NULL(G_SoundIndexPolicy(G_UnitSoundProfile(ent)->select[0]));
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(sounds);
}

TEST(wc3_unit, shared_sound_file_keeps_label_policy_and_volume_independent) {
    cstring_t slk = "ID;PWXL;N;E\nB;X6;Y3;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\nC;X2;K\"FileNames\"\nC;X3;K\"Channel\"\n"
        "C;X4;K\"Priority\"\nC;X5;K\"Volume\"\nC;X6;K\"Flags\"\n"
        "C;Y2;X1;K\"AliasWhat\"\nC;X2;K\"shared.wav\"\nC;X3;K1\nC;X4;K1731\nC;X5;K127\nC;X6;K\"NODUPEUSERNAMES\"\n"
        "C;Y3;X1;K\"AliasReady\"\nC;X2;K\"shared.wav\"\nC;X3;K4\nC;X4;K900\nC;X5;K63.5\nC;X6;K\"IGNOREUSERNAME\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk), *old = G_SetSLKRows("UnitAckSounds", rows);
    int what = G_UnitAckSoundVariantIndex("Alias", "What", 0);
    int ready = G_UnitAckSoundVariantIndex("Alias", "Ready", 0);
    T_ASSERT(what > 0 && ready > 0); T_NE(what, ready);
    T_EQ(G_SoundIndexPolicy(what)->group, 1); T_EQ(G_SoundIndexPolicy(what)->priority, 1731);
    T_EQ(G_SoundIndexPolicy(ready)->group, 4); T_EQ(G_SoundIndexPolicy(ready)->priority, 900);
    T_FEQ(G_SoundIndexVolume(what), 1, .001f); T_FEQ(G_SoundIndexVolume(ready), .5f, .001f);
    T_EQ(G_UnitAckSoundVariantIndex("Alias", "What", 0), what);
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(rows);
}

TEST(wc3_unit, selecting_owned_unit_queues_one_ack_sound) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    {
        unitSoundProfile_t value = *G_UnitSoundProfile(ent);
        value.select[0] = 11;
        value.select[1] = 12;
        value.num_select = 2;
        G_SetUnitSoundProfile(ent, &value);
    }
    G_QueueSelectionSound(ent, true);
    T_ASSERT(ent->sound.pending == 11 || ent->sound.pending == 12);
    T_EQ(ent->sound.pending != 0, 1);
}

TEST(wc3_unit, selection_without_responses_does_not_queue_ack) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_QueueSelectionSound(ent, true);
    T_EQ(ent->sound.pending, 0);
}

TEST(wc3_unit, queued_response_does_not_start_portrait_before_playback_feedback) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    T_ASSERT(!G_UnitResponseTalking(ent));
}

TEST(wc3_unit, response_preserves_full_sound_configstring_index) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 731));
    T_EQ(ent->sound.pending, 731);
}

void test_sound_event(edict_t *ent, uint32_t request, uint32_t event);

static void response_test_finish(edict_t *ent) {
    uint32_t request = G_UnitResponseRequest(ent, ent->sound.pending);
    ent->sound.pending = 0;
    test_sound_event(ent, request, SOUND_ACCEPTED);
    test_sound_event(ent, request, SOUND_ACCEPTED); /* duplicate cannot advance twice */
    test_sound_event(ent, request, SOUND_STARTED);
    test_sound_event(ent, request, SOUND_ENDED);
}

TEST(wc3_unit, response_feedback_owns_portrait_lifetime_and_rejection) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    uint32_t first = G_UnitResponseRequest(ent, 11);
    ent->sound.pending = 0; /* packet sent */
    T_ASSERT(!G_UnitResponseTalking(ent));
    T_ASSERT(!G_QueueUnitResponseSound(ent, 12)); /* only one unanswered request */
    test_sound_event(ent, first, SOUND_REJECTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
    T_ASSERT(G_QueueUnitResponseSound(ent, 12));
    uint32_t second = G_UnitResponseRequest(ent, 12);
    T_NE(first, second);
    test_sound_event(ent, second, SOUND_STARTED); /* cannot start before admission */
    T_ASSERT(!G_UnitResponseTalking(ent));
    test_sound_event(ent, second, SOUND_ACCEPTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
    test_sound_event(ent, second, SOUND_STARTED);
    T_ASSERT(G_UnitResponseTalking(ent));
    level.time += 100000; /* neither accelerated game time nor authored length ends playback */
    T_ASSERT(G_UnitResponseTalking(ent));
    test_sound_event(ent, first, SOUND_ENDED); /* stale receipt */
    T_ASSERT(G_UnitResponseTalking(ent));
    test_sound_event(ent, second, SOUND_ENDED); /* completion OR preemption */
    T_ASSERT(!G_UnitResponseTalking(ent));
    test_sound_event(ent, second, SOUND_STARTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
}

TEST(wc3_unit, response_feedback_rejects_foreign_clients_resets_and_reused_units) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    uint32_t request = G_UnitResponseRequest(ent, 11);
    char user[16], token[16];
    snprintf(user, sizeof(user), "%u", ent->s.number); snprintf(token, sizeof(token), "%u", request);
    cstring_t accepted[] = {"sound_event", user, token, "1"}, started[] = {"sound_event", user, token, "2"};
    g_edicts[0].client = game.clients; game.clients[0].connected = true; game.clients[0].ps.number = 1;
    G_ClientCommand(g_edicts, 4, accepted); G_ClientCommand(g_edicts, 4, started);
    T_ASSERT(!G_UnitResponseTalking(ent));
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    T_NE(G_UnitResponseRequest(ent, 11), request);
    test_sound_event(ent, request, SOUND_ACCEPTED); test_sound_event(ent, request, SOUND_STARTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
    request = G_UnitResponseRequest(ent, 11);
    G_ClearUnitResponses(ent); /* same-slot, same-tick reuse must not match */
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    test_sound_event(ent, request, SOUND_ACCEPTED); test_sound_event(ent, request, SOUND_STARTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
}

TEST(wc3_unit, overlapping_response_feedback_keeps_portrait_until_last_voice_ends) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11)); uint32_t a = G_UnitResponseRequest(ent, 11);
    ent->sound.pending = 0;
    test_sound_event(ent, a, SOUND_ACCEPTED); test_sound_event(ent, a, SOUND_STARTED);
    T_ASSERT(G_QueueUnitResponseSound(ent, 12)); uint32_t b = G_UnitResponseRequest(ent, 12);
    test_sound_event(ent, b, SOUND_ACCEPTED); test_sound_event(ent, b, SOUND_STARTED);
    test_sound_event(ent, a, SOUND_ENDED); T_ASSERT(G_UnitResponseTalking(ent));
    test_sound_event(ent, b, SOUND_ENDED); T_ASSERT(!G_UnitResponseTalking(ent));
    T_ASSERT(G_QueueUnitResponseSound(ent, 13)); uint32_t c = G_UnitResponseRequest(ent, 13);
    game.clients[0].connected = false;
    G_UpdateUnitResponsePresentation();
    test_sound_event(ent, c, SOUND_ACCEPTED); test_sound_event(ent, c, SOUND_STARTED);
    T_ASSERT(!G_UnitResponseTalking(ent));
    G_ResetSelectionSoundState(); T_EQ(ent->sound.pending, 0);
}

TEST(wc3_unit, unused_client_slot_cannot_retire_another_clients_playback) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(ent, 11));
    uint32_t request = G_UnitResponseRequest(ent, 11);
    test_sound_event(ent, request, SOUND_ACCEPTED); test_sound_event(ent, request, SOUND_STARTED);
    G_UpdateUnitResponsePresentation();
    T_ASSERT(G_UnitResponseTalking(ent));
    G_ResetSelectionSoundState();
}

TEST(wc3_unit, selection_reset_ignores_late_response_admission) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    {
        unitSoundProfile_t value = *G_UnitSoundProfile(ent);
        value.select[0] = 731; value.num_select = 1;
        G_SetUnitSoundProfile(ent, &value);
    }
    G_ResetSelectionSoundState();
    G_QueueSelectionSound(ent, true); T_EQ(ent->sound.pending, 731);
    uint32_t request = G_UnitResponseRequest(ent, 731);
    G_QueueSelectionSound(ent, true); /* same unit reselected before old admission */
    test_sound_event(ent, request, SOUND_ACCEPTED);
    T_EQ(selection_sound_state[0].selected_sound_count, 0);
    test_sound_event(ent, request, SOUND_ENDED);
    G_QueueSelectionSound(ent, false); request = G_UnitResponseRequest(ent, 731);
    test_sound_event(ent, request, SOUND_ACCEPTED);
    test_sound_event(ent, request, SOUND_ACCEPTED);
    T_EQ(selection_sound_state[0].selected_sound_count, 1);
    G_ResetSelectionSoundState();
}

TEST(wc3_unit, response_variant_commits_on_acceptance_and_rejection_preserves_previous) {
    cstring_t text = "ID;PWXL;N;E\nB;X2;Y2;D0\nC;Y1;X1;K\"SoundLabel\"\nC;X2;K\"FileNames\"\n"
        "C;Y2;X1;K\"ProbeWhat\"\nC;X2;K\"one.wav,two.wav\"\nE\n";
    slkTestData_t *rows = parse_slk_string(text), *old = G_SetSLKRows("UnitAckSounds", rows);
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    G_ResetSelectionSoundState();
    int a = G_UnitAckSoundVariantIndex("Probe", "What", 0), b = G_UnitAckSoundVariantIndex("Probe", "What", 1);
    T_ASSERT(G_QueueUnitResponseSound(ent, a));
    uint32_t request = G_UnitResponseRequest(ent, a);
    T_ASSERT(!G_SoundVariantIsLast(a, 0));
    test_sound_event(ent, request, SOUND_ACCEPTED);
    T_ASSERT(G_SoundVariantIsLast(a, 0)); T_ASSERT(!G_SoundVariantIsLast(a, 1));
    test_sound_event(ent, request, SOUND_ENDED);
    T_ASSERT(G_QueueUnitResponseSound(ent, b)); request = G_UnitResponseRequest(ent, b);
    test_sound_event(ent, request, SOUND_REJECTED);
    T_ASSERT(G_SoundVariantIsLast(a, 0)); T_ASSERT(!G_SoundVariantIsLast(b, 0));
    T_ASSERT(G_QueueUnitResponseSound(ent, b)); request = G_UnitResponseRequest(ent, b);
    test_sound_event(ent, request, SOUND_ACCEPTED);
    T_ASSERT(!G_SoundVariantIsLast(a, 0)); T_ASSERT(G_SoundVariantIsLast(b, 0));
    G_ResetSelectionSoundState(); G_ResetSoundPresentationState();
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(rows);
}

TEST(wc3_unit, repeated_selection_walks_pissed_responses_after_three_what_lines) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X3;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y2;X1;K\"FootmanPissed\"\n"
        "C;Y2;X2;K\"Pissed1.wav,Pissed2.wav\"\n"
        "C;Y2;X3;K\"Units\\Human\\Footman\\\"\n"
        "E\n";
    UnitUI_t ui = { .soundLabel = "Footman" };
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    slkTestData_t *sounds = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("UnitAckSounds", sounds);
    int (*old_sound_index)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    ent->data.UnitUI = &ui;
    {
        unitSoundProfile_t value = *G_UnitSoundProfile(ent);
        value.select[0] = 11;
        value.num_select = 1;
        G_SetUnitSoundProfile(ent, &value);
    }
    gi.SoundIndex = selection_sound_index_77; gi.SoundIndexAlias = selection_sound_index_77_alias;
    G_ResetSelectionSoundState();

    G_QueueSelectionSound(ent, true); T_EQ(ent->sound.pending, 11);
    /* Rejected responses leave the three-What threshold untouched. */
    uint32_t rejected = G_UnitResponseRequest(ent, 11);
    ent->sound.pending = 0; test_sound_event(ent, rejected, SOUND_REJECTED);
    FOR_LOOP(i, 3) {
        G_QueueSelectionSound(ent, false); T_EQ(ent->sound.pending, 11);
        response_test_finish(ent);
    }
    G_QueueSelectionSound(ent, false); T_EQ(ent->sound.pending, 77);
    response_test_finish(ent);
    G_QueueSelectionSound(ent, false); T_EQ(ent->sound.pending, 77);
    response_test_finish(ent);

    gi.SoundIndex = old_sound_index; gi.SoundIndexAlias = old_sound_alias;
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(sounds);
}

TEST(wc3_unit, attack_order_uses_yesattack_instead_of_weapon_swing_slot) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X3;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y2;X1;K\"FootmanYesAttack\"\n"
        "C;Y2;X2;K\"Attack1.wav,Attack2.wav\"\n"
        "C;Y2;X3;K\"Units\\Human\\Footman\\\"\n"
        "E\n";
    UnitUI_t ui = { .soundLabel = "Footman" };
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    slkTestData_t *sounds = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("UnitAckSounds", sounds);
    int (*old_sound_index)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    ent->data.UnitUI = &ui;
    {
        unitSoundProfile_t value = *G_UnitSoundProfile(ent);
        value.attack = 0;
        G_SetUnitSoundProfile(ent, &value);
    }
    gi.SoundIndex = selection_sound_index_77; gi.SoundIndexAlias = selection_sound_index_77_alias;
    G_QueueAttackOrderSound(ent);
    T_EQ(ent->sound.pending, 77);
    T_EQ(G_UnitSoundProfile(ent)->attack, 0);

    gi.SoundIndex = old_sound_index; gi.SoundIndexAlias = old_sound_alias;
    G_SetSLKRows("UnitAckSounds", old); free_slk_rows(sounds);
}

TEST(wc3_unit, ready_sound_queues_owner_only_sound) {
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    {
        unitSoundProfile_t value = *G_UnitSoundProfile(ent);
        value.ready[0] = 21;
        value.ready[1] = 22;
        value.num_ready = 2;
        G_SetUnitSoundProfile(ent, &value);
    }

    G_QueueReadySound(ent);
    T_ASSERT(ent->sound.owner_pending == 21 || ent->sound.owner_pending == 22);
    T_EQ(ent->sound.owner_pending != 0, 1);
}

/* -----------------------------------------------------------------------
 * Birth tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, birth_sets_birth_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_birth(ent);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "birth");
}

TEST(wc3_unit, birth_sets_wait_from_build_time) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_birth(ent);
    T_ASSERT(ent->wait > 0);
    T_EQ((int)ent->wait, G_UnitBalance(ent->class_id)->buildTime);
}

TEST(wc3_unit, birth_sets_no_ubersplat_flag) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_birth(ent);
    T_ASSERT(ent->s.renderfx & RF_NO_UBERSPLAT);
}

/* -----------------------------------------------------------------------
 * Stand tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, stand_sets_stand_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_stand(ent);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, stand_uses_ready_animation_in_combat) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (vec2_t){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;

    unit_entercombat(ent, target);
    unit_stand(ent);

    T_ASSERT(unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "stand ready");
}

TEST(wc3_unit, stop_exits_ready_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (vec2_t){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;

    unit_entercombat(ent, target);
    unit_stand(ent);
    T_STREQ(ent->currentmove->animation, "stand ready");

    unit_issueimmediateorder(ent, "stop");

    T_ASSERT(!unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, stand_clears_build_pointer) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->build = ent;
    unit_stand(ent);
    T_NULL(ent->build);
}

TEST(wc3_unit, stand_clears_no_ubersplat_flag) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->s.renderfx |= RF_NO_UBERSPLAT;
    unit_stand(ent);
    T_ASSERT(!(ent->s.renderfx & RF_NO_UBERSPLAT));
}

TEST(wc3_unit, spawned_building_is_immobile) {
    T_ASSERT(unit_spawn_aiflags(MAKEFOURCC('h','b','a','r')) & AI_IMMOBILE);
}

TEST(wc3_unit, spawned_building_carries_renderer_building_flag) {
    T_ASSERT(G_UnitIsBuilding(MAKEFOURCC('h','b','a','r')));
}

TEST(wc3_unit, spawned_mobile_unit_is_not_immobile) {
    T_ASSERT(!(unit_spawn_aiflags(MAKEFOURCC('h','p','e','a')) & AI_IMMOBILE));
}

/* -----------------------------------------------------------------------
 * Die tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, die_sets_death_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    unit_die(ent, NULL);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, paused_death_animation_advances_after_kill) {
    animation_t death = { .name = "Death", .interval = { 0, 300 } };
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->animation = &death;
    ent->s.frame = 50;
    ent->paused = true;

    unit_die(ent, NULL);
    T_ASSERT(ent->animation_override);
    T_ASSERT(ent->currentmove && !strcmp(ent->currentmove->animation, "death"));
    ent->animation = &death;
    ent->s.frame = 50;
    monster_think(ent);
    T_EQ((int)ent->s.frame, 150);
}

TEST(wc3_unit, feral_spirit_death_uses_magic_burst_instead_of_blood) {
    reset_test_entities();
    edict_t *wolf = make_unit(64.0f, 96.0f);
    wolf->aiflags |= AI_SUMMON_EXPLODES_ON_DEATH;
    wolf->summon_ability = MAKEFOURCC('A','O','s','f');
    unitSoundProfile_t sounds = *G_UnitSoundProfile(wolf);
    sounds.death = 23;
    G_SetUnitSoundProfile(wolf, &sounds);

    unit_die(wolf, NULL);
    T_ASSERT(wolf->svflags & SVF_DEADMONSTER);
    T_ASSERT(wolf->s.renderfx & RF_HIDDEN);
    T_ASSERT(wolf->aiflags & AI_CORPSE_UNRAISABLE);
    T_ASSERT(wolf->aiflags & AI_CORPSE_NO_DECAY);
    T_EQ(wolf->sound.world_pending, 0);
    T_EQ(wolf->sound.world_pending_event, 0);
    T_FEQ(wolf->wait, FRAMETIME / 1000.0f, 0.0001f);
}

TEST(wc3_unit, die_emits_registered_death_sound) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    {
        unitSoundProfile_t value = *G_UnitSoundProfile(ent);
        value.death = 23;
        G_SetUnitSoundProfile(ent, &value);
    }

    unit_die(ent, NULL);
    T_EQ(ent->sound.world_pending, 23);
    T_EQ(ent->sound.world_pending_event, EV_DEATH);
    T_EQ(ent->sound.world_pending, 23);
}

TEST(wc3_unit, die_is_one_shot_after_dead_monster_flag) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    {
        unitSoundProfile_t value = *G_UnitSoundProfile(ent);
        value.death = 23;
        G_SetUnitSoundProfile(ent, &value);
    }

    unit_die(ent, NULL);
    T_ASSERT(ent->svflags & SVF_DEADMONSTER);

    /* A second lethal resolution must not replay death-side effects such as
     * sounds/events/loot-trigger publication. */
    ent->sound.world_pending = 0;
    ent->sound.world_pending_event = 0;
    unit_die(ent, NULL);

    T_EQ(ent->sound.world_pending, 0);
    T_EQ(ent->sound.world_pending_event, 0);
}

TEST(wc3_unit, die_raises_dead_monster_flag) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    T_ASSERT(!M_IsDead(ent));
    unit_die(ent, NULL);
    T_FEQ(ent->health.value, 0.0f, 0.001f);
    T_ASSERT(M_IsDead(ent));
    T_ASSERT(ent->svflags & SVF_DEADMONSTER);
}

TEST(wc3_unit, die_clears_selection_and_marks_corpse_unselectable) {
    reset_test_entities();
    gameClient_t *client = &game.clients[0];
    edict_t *ent = make_unit(0, 0);
    client->ps.number = 0;
    ent->s.player = 0;

    G_SelectEntity(client, ent);
    T_ASSERT(G_IsEntitySelected(client, ent));

    unit_die(ent, NULL);

    T_EQ(ent->selected, 0);
    T_ASSERT(ent->s.flags & EF_NOT_SELECTABLE);
    T_ASSERT(!G_IsEntitySelected(client, ent));

    G_SelectEntity(client, ent);
    T_ASSERT(!G_IsEntitySelected(client, ent));
}

TEST(wc3_unit, die_releases_held_frame_before_death_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    ent->aiflags |= AI_HOLD_FRAME;

    unit_die(ent, NULL);

    T_ASSERT(!(ent->aiflags & AI_HOLD_FRAME));
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, corpse_decay_uses_map_flesh_then_bone_constants) {
    static UnitBalance_t balance = { .maxHealth = 100.0f };
    static UnitData_t data = { .death = 1.0f, .deathType = 3 };
    edict_t *ent;

    reset_test_entities();
    game.constants.decayTime = 0.2f;
    game.constants.boneDecayTime = 0.3f;
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'f', 'o', 'o');
    ent->data.UnitBalance = &balance;
    ent->data.UnitData = &data;
    unit_begin_decay(ent);

    T_STREQ(ent->currentmove->animation, "decay flesh");
    T_STREQ(G_UnitAnimationRequest(ent), "decay flesh");
    T_NOT_NULL(ent->currentmove->animation_duration);
    T_FEQ(ent->currentmove->animation_duration(ent), 0.2f, 0.001f);
    T_FEQ(ent->wait, 0.2f, 0.001f);
    ent->currentmove->think(ent);
    ent->currentmove->think(ent);
    T_ASSERT(ent->inuse);
    T_STREQ(ent->currentmove->animation, "decay bone");
    T_STREQ(G_UnitAnimationRequest(ent), "decay bone");
    T_NOT_NULL(ent->currentmove->animation_duration);
    T_FEQ(ent->currentmove->animation_duration(ent), 0.3f, 0.001f);
    T_FEQ(ent->wait, 0.3f, 0.001f);
    FOR_LOOP(i, 4) if (ent->inuse) ent->currentmove->think(ent);
    T_ASSERT(!ent->inuse);
}

TEST(wc3_unit, suspend_decay_native_cancels_flesh_and_restarts_bones) {
    static UnitData_t data = { .deathType = 3 };
    float old_flesh = game.constants.decayTime;
    float old_bone = game.constants.boneDecayTime;
    reset_entities();
    setup_test_world();
    game.constants.decayTime = 2;
    game.constants.boneDecayTime = 4;
    T_ASSERT(run_test_jass("globals\nunit corpse=null\nendglobals\n"
        "function main takes nothing returns nothing\n"
        "set corpse=CreateUnit(Player(0),'hfoo',656,656,0)\nendfunction\n"
        "function suspend takes nothing returns nothing\ncall UnitSuspendDecay(corpse,true)\nendfunction\n"
        "function resume takes nothing returns nothing\ncall UnitSuspendDecay(corpse,false)\nendfunction\n"));
    edict_t *ent = NULL;
    FOR_LOOP(i, globals.num_edicts) {
        if (g_edicts[i].inuse && g_edicts[i].class_id == MAKEFOURCC('h','f','o','o')) ent = g_edicts+i;
    }
    T_NOT_NULL(ent);
    if (ent) {
        umove_t const *living_move = ent->currentmove;
        jass_callbyname(level.vm, "suspend", false);
        jass_callbyname(level.vm, "resume", false);
        T_ASSERT(ent->currentmove == living_move);
        T_ASSERT(!(ent->aiflags & AI_CORPSE_DECAY_SUSPENDED));
        ent->data.UnitData = &data;
        G_SetHealth(ent, 0);
        unit_die(ent, NULL);
        umove_t const *death_move = ent->currentmove;
        jass_callbyname(level.vm, "suspend", false);
        jass_callbyname(level.vm, "resume", false);
        T_ASSERT(ent->currentmove == death_move);
        T_ASSERT(!(ent->aiflags & AI_CORPSE_DECAY_SUSPENDED));
        unit_begin_decay(ent);
        ent->currentmove->think(ent);
        float remaining = ent->wait;
        jass_callbyname(level.vm, "suspend", false);
        T_ASSERT(WriteGame(Test_TempPath("order343-decay.bin")));
        T_ASSERT(ReadGame(Test_TempPath("order343-decay.bin")));
        remove(Test_TempPath("order343-decay.bin"));
        jass_callbyname(level.vm, "suspend", false);
        FOR_LOOP(i, 30) if (ent->inuse && ent->currentmove->think) ent->currentmove->think(ent);
        T_ASSERT(ent->inuse);
        T_STREQ(G_UnitAnimationRequest(ent), "decay flesh");
        T_FEQ(ent->wait, remaining, 0.0001f);
        jass_callbyname(level.vm, "resume", false);
        T_STREQ(G_UnitAnimationRequest(ent), "decay bone");
        T_FEQ(ent->wait, 4, 0.0001f);
        T_ASSERT(WriteGame(Test_TempPath("order343-resumed.bin")));
        T_ASSERT(ReadGame(Test_TempPath("order343-resumed.bin")));
        remove(Test_TempPath("order343-resumed.bin"));
        ent->currentmove->think(ent);
        jass_callbyname(level.vm, "resume", false);
        T_FEQ(ent->wait, 4, 0.0001f);
        ent->currentmove->think(ent);
        jass_callbyname(level.vm, "suspend", false);
        FOR_LOOP(i, 50) if (ent->inuse && ent->currentmove->think) ent->currentmove->think(ent);
        T_ASSERT(ent->inuse);
        jass_callbyname(level.vm, "resume", false);
        T_FEQ(ent->wait, 4, 0.0001f);
        FOR_LOOP(i, 41) if (ent->inuse && ent->currentmove->think) ent->currentmove->think(ent);
        T_ASSERT(!ent->inuse);
    }
    T_ASSERT(!jass_rterror_pending(level.vm));
    game.constants.decayTime = old_flesh;
    game.constants.boneDecayTime = old_bone;
    reset_entities();
}

TEST(wc3_unit, decaying_structure_uses_structure_decay_constant) {
    static UnitBalance_t balance = { .isBuilding = true, .maxHealth = 100.0f };
    static UnitData_t data = { .death = 1.0f, .deathType = 2 };
    edict_t *ent;

    reset_test_entities();
    game.constants.structureDecayTime = 0.3f;
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'b', 'a', 'r');
    ent->data.UnitBalance = &balance;
    ent->data.UnitData = &data;
    ent->s.flags |= EF_BUILDING;
    unit_begin_decay(ent);

    T_FEQ(ent->wait, 0.3f, 0.001f);
    FOR_LOOP(i, 4) if (ent->inuse) ent->currentmove->think(ent);
    T_ASSERT(!ent->inuse);
}

TEST(wc3_unit, corpse_reservation_suspends_decay_timer) {
    static UnitBalance_t balance = { .maxHealth = 100.0f };
    static UnitData_t data = { .deathType = 3 };
    edict_t *ent;

    reset_test_entities();
    game.constants.decayTime = 0.2f;
    game.constants.boneDecayTime = 0.3f;
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'f', 'o', 'o');
    ent->data.UnitBalance = &balance;
    ent->data.UnitData = &data;
    unit_begin_decay(ent);
    ent->aiflags |= AI_CORPSE_RESERVED;

    FOR_LOOP(i, 5) ent->currentmove->think(ent);
    T_ASSERT(ent->inuse);
    T_FEQ(ent->wait, 0.2f, 0.001f);

    ent->aiflags &= ~AI_CORPSE_RESERVED;
    ent->currentmove->think(ent);
    ent->currentmove->think(ent);
    T_ASSERT(ent->inuse);
    T_FEQ(ent->wait, 0.3f, 0.001f);
}

TEST(wc3_unit, raisable_corpse_requires_authored_raise_bit_and_no_reservation) {
    static UnitBalance_t balance = { .maxHealth = 100.0f };
    UnitData_t data = { .deathType = 2 };
    edict_t *ent;

    reset_test_entities();
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'f', 'o', 'o');
    ent->data.UnitBalance = &balance;
    ent->data.UnitData = &data;
    ent->health.value = 0.0f;
    ent->svflags |= SVF_MONSTER | SVF_DEADMONSTER;

    T_ASSERT(!G_UnitIsRaisableCorpse(ent));
    data.deathType = 3;
    T_ASSERT(G_UnitIsRaisableCorpse(ent));
    ent->aiflags |= AI_CORPSE_RESERVED;
    T_ASSERT(!G_UnitIsRaisableCorpse(ent));
}

TEST(wc3_unit, nondecaying_building_uses_authored_death_type) {
    static UnitBalance_t building_balance = { .isBuilding = true, .maxHealth = 100.0f };
    static UnitData_t building_data = { .death = 1.25f, .deathType = 0 };
    edict_t *ent;

    reset_test_entities();
    ent = make_unit(0, 0);
    ent->class_id = MAKEFOURCC('h', 'b', 'a', 'r');
    ent->data.UnitBalance = &building_balance;
    ent->data.UnitData = &building_data;
    unit_die(ent, NULL);
    unit_begin_decay(ent);

    T_ASSERT(ent->aiflags & AI_HOLD_FRAME);
    T_FEQ(ent->wait, FRAMETIME / 1000.0f, 0.001f);
    unit_decay_think(ent);
    T_ASSERT(!ent->inuse);
}

TEST(wc3_unit, dead_unit_rejects_orders_that_would_replace_death_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    vec2_t point = { 64.0f, 0.0f };

    ent->health.value = 0.0f;
    unit_die(ent, target);

    T_ASSERT(!unit_issueorder(ent, "move", &point));
    T_ASSERT(!unit_issueimmediateorder(ent, "stop"));
    T_ASSERT(!unit_issuetargetorder(ent, "smart", target));
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "death");
}

TEST(wc3_unit, smart_on_passive_ally_starts_persistent_follow) {
    reset_test_entities();
    edict_t *follower = make_unit(0, 0);
    edict_t *leader = make_unit(256, 0);
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = 0;
    leader->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

    T_ASSERT(unit_issuetargetorder(follower, "smart", leader));
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
    T_ASSERT(follower->currentmove && follower->currentmove->proc == CAbilityMove);
}

TEST(wc3_unit, target_move_on_unit_starts_persistent_follow) {
    reset_test_entities();
    edict_t *follower = make_unit(0, 0);
    edict_t *leader = make_unit(256, 0);
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = leader->s.player = 0;

    T_ASSERT(unit_issuetargetorder(follower, "move", leader));
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
    T_ASSERT(follower->currentmove && follower->currentmove->proc == CAbilityMove);
}

TEST(wc3_unit, follow_stop_range_uses_misc_data_not_acquisition_range) {
    float const old_follow = game.constants.followRange;
    float const old_structure = game.constants.structureFollowRange;
    edict_t *follower;
    edict_t *target;

    reset_test_entities();
    follower = make_unit(0, 0);
    target = make_unit(512, 0);
    follower->runtime.acquisition_range = 600.0f;
    follower->collision = 32.0f;
    target->collision = 0.0f;
    game.constants.followRange = 300.0f;
    game.constants.structureFollowRange = 100.0f;

    T_FEQ(G_AcquisitionRange(follower), 600.0f, 0.001f);
    T_FEQ(G_FollowStopRange(follower, target), 332.0f, 0.001f);

    target->s.flags |= EF_BUILDING;
    T_FEQ(G_FollowStopRange(follower, target), 32.0f, 0.001f);

    /* Collision remains a hard lower bound so large widgets never overlap
     * merely because Misc requests a smaller follow distance. */
    target->collision = 96.0f;
    T_FEQ(G_FollowStopRange(follower, target), 128.0f, 0.001f);

    game.constants.followRange = old_follow;
    game.constants.structureFollowRange = old_structure;
}

TEST(wc3_unit, smart_follow_building_uses_zero_range_physical_task) {
    enum { W = 8, H = 8 };
    float const old_structure = game.constants.structureFollowRange;
    size_t const pathtex_size = sizeof(pathTex_t) + W * H * sizeof(color32_t);
    pathTex_t *pathtex;
    edict_t *follower;
    edict_t *building;

    reset_test_entities();
    setup_test_world();
    follower = make_unit(200.0f, 0.0f);
    building = make_unit(0.0f, 0.0f);
    follower->svflags |= SVF_MONSTER;
    building->svflags |= SVF_MONSTER;
    follower->s.player = building->s.player = 0;
    building->s.flags |= EF_BUILDING;
    building->collision = 160.0f;
    game.constants.structureFollowRange = 100.0f;

    pathtex = gi.MemAlloc(pathtex_size);
    T_NOT_NULL(pathtex);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = W;
    pathtex->height = H;
    FOR_LOOP(i, W * H) pathtex->map[i].b = 0xff;
    building->pathtex = pathtex;

    /* Retail5fd270 selects d0174(range0), whose physical arrival range
     * includes both radii. A footprint does not add a Misc follow margin. */
    T_ASSERT(M_DistanceToGoal(follower) == 0.0f);
    T_ASSERT(unit_issuetargetorder(follower, "smart", building));
    T_ASSERT(follower->movement.follow_target == building);
    T_ASSERT(follower->goalentity == building);
    T_ASSERT(M_DistanceToGoal(follower) > G_FollowStopRange(follower, building));
    T_FEQ(G_FollowStopRange(follower, building), 176.0f, 0.001f);
    moveGroup_t const *group=move_unit_group(follower);T_NOT_NULL(group);
    if(group)T_FEQ(group->members[0].arrival_range,5.5f,0.0f);
    T_ASSERT(follower->currentmove->scheduled_think);
    T_ASSERT(CM_DistanceToPathingFootprint(building, &follower->s.origin2) <
             game.constants.structureFollowRange);

    follower->animation = &(animation_t){ .name = "stand", .interval = { 0, 300 } };
    follower->currentmove->think(follower);

    T_ASSERT(follower->movement.follow_target == building);
    T_ASSERT(G_AnimationHasPrimary(follower->animation, "stand"));

    building->pathtex = NULL;
    gi.MemFree(pathtex);
    game.constants.structureFollowRange = old_structure;
}

TEST(wc3_unit, queued_smart_on_passive_ally_revalidates_to_follow) {
    reset_test_entities();
    setup_test_world();
    edict_t *follower = make_unit(0, 0);
    edict_t *leader = make_unit(256, 0);
    vec2_t first = { 96.0f, 0.0f };
    follower->svflags |= SVF_MONSTER;
    leader->svflags |= SVF_MONSTER;
    follower->s.player = 0;
    leader->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    level.alliances[0][1] |= 1 << ALLIANCE_PASSIVE;

    T_ASSERT(G_IssueUnitPointOrder(follower, "move", &first, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitTargetOrder(follower, "smart", leader, true, 0));
    T_EQ(G_UnitQueuedOrderCount(follower), 1);

    unit_stand(follower);

    T_EQ(G_UnitQueuedOrderCount(follower), 0);
    T_ASSERT(follower->movement.follow_target == leader);
    T_ASSERT(follower->goalentity == leader);
}

TEST(wc3_unit, neutral_creep_natural_sleep_tracks_night_and_wakes_at_dawn) {
    reset_test_entities();
    setup_test_world();
    edict_t *creep = make_unit(0, 0);
    uint16_t *no_creep_sleep = &game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps.stats[WC3_PLAYERSTATE_NO_CREEP_SLEEP];
    UnitData_t data = *creep->data.UnitData;
    UnitBalance_t balance = *creep->data.UnitBalance;
    UnitUI_t ui = *creep->data.UnitUI;

    /* Spawn from authored eligibility, without an explicit ACsp ability-list entry. */
    data.canSleep = true;
    data.propWin = 23.0f;
    balance.maxHealth = 100;
    ui.modelFile = "Units\\Creeps\\Medivh\\Medivh.mdx";
    creep->data.UnitData = &data;
    creep->data.UnitBalance = &balance;
    creep->data.UnitUI = &ui;
    creep->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    SP_SpawnUnit(creep);
    T_FEQ(creep->unitinfo.PropWindow, DEG2RAD(23.0f), 0.001f);
    unit_stand(creep);
    T_ASSERT(G_UnitCanSleep(creep));
    *no_creep_sleep = 0;

    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(G_IsNight());
    ai_stand(creep);
    T_ASSERT(G_UnitIsSleeping(creep));
    T_STREQ(G_UnitAnimationRequest(creep), "sleep");
    ability_t const *ability = FindAbilityByClassname("ACsp");
    T_NOT_NULL(ability);
    T_ASSERT(ability && creep->currentmove->proc == ability->proc);

    G_SetTimeOfDay(game.constants.dawnTimeGameHours);
    G_UpdateTimeOfDay();
    T_ASSERT(!G_IsNight());
    monster_think(creep);
    T_ASSERT(!G_UnitIsSleeping(creep));
    T_STREQ(G_UnitAnimationRequest(creep), "stand");
    G_BindEntityData(creep);
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
}

TEST(wc3_unit, no_creep_sleep_player_state_blocks_new_natural_sleep) {
    reset_test_entities();
    setup_test_world();
    edict_t *creep = make_unit(0, 0);
    uint16_t *no_creep_sleep = &game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps.stats[WC3_PLAYERSTATE_NO_CREEP_SLEEP];

    creep->svflags |= SVF_MONSTER;
    creep->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    if (!creep->sleep) creep->sleep = G_AllocSleep();
    assert(creep->sleep);
    creep->sleep->can_sleep = true;
    *no_creep_sleep = 1;

    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    ai_stand(creep);
    T_ASSERT(!G_UnitIsSleeping(creep));

    *no_creep_sleep = 0;
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
}

TEST(wc3_unit, unit_add_sleep_policy_is_neutral_only_and_wakes_when_disabled) {
    reset_test_entities();
    setup_test_world();
    edict_t *neutral = make_unit(0, 0);
    edict_t *player_unit = make_unit(64, 0);

    neutral->svflags |= SVF_MONSTER;
    neutral->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    if (!neutral->sleep) neutral->sleep = G_AllocSleep();
    assert(neutral->sleep);
    neutral->sleep->can_sleep = false;
    player_unit->svflags |= SVF_MONSTER;
    player_unit->s.player = 0;
    if (!player_unit->sleep) player_unit->sleep = G_AllocSleep();
    assert(player_unit->sleep);
    player_unit->sleep->can_sleep = false;

    G_UnitSetCanSleep(neutral, true);
    G_UnitSetCanSleep(player_unit, true);
    T_ASSERT(G_UnitCanSleep(neutral));
    T_ASSERT(!G_UnitCanSleep(player_unit));
    T_ASSERT(!player_unit->sleep || !player_unit->sleep->can_sleep);

    game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps.stats[WC3_PLAYERSTATE_NO_CREEP_SLEEP] = 0;
    G_SetTimeOfDay(game.constants.duskTimeGameHours);
    G_UpdateTimeOfDay();
    ai_stand(neutral);
    T_ASSERT(G_UnitIsSleeping(neutral));

    G_UnitSetCanSleep(neutral, false);
    T_ASSERT(!G_UnitCanSleep(neutral));
    T_ASSERT(!G_UnitIsSleeping(neutral));
    G_SetTimeOfDay(12.0f);
    G_UpdateTimeOfDay();
}

TEST(wc3_unit, smart_on_neutral_aggressive_attacks_not_follows) {
    reset_test_entities();
    edict_t *unit = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    memset(level.alliances, 0, sizeof(level.alliances));

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->movement.follow_target == NULL);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityAttack);
}

TEST(wc3_unit, smart_on_neutral_aggressive_follows_after_passive_alliance) {
    reset_test_entities();
    edict_t *unit = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    memset(level.alliances, 0, sizeof(level.alliances));
    G_SetPlayerAlliance(&game.clients[0].ps,
                        &game.clients[PLAYER_NEUTRAL_AGGRESSIVE].ps,
                        ALLIANCE_PASSIVE, true);

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->movement.follow_target == target);
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_unit, smart_on_neutral_passive_uses_persistent_follow) {
    reset_test_entities();
    edict_t *unit = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = PLAYER_NEUTRAL_PASSIVE;
    G_InitPlayerAlliances(level.mapinfo);

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->movement.follow_target == target);
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityMove);
}

TEST(wc3_unit, smart_on_shared_vision_enemy_still_attacks) {
    reset_test_entities();
    edict_t *unit = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    unit->svflags |= SVF_MONSTER;
    target->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    target->s.player = 1;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.alliances[0][1] |= 1 << ALLIANCE_SHARED_VISION;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;

    T_ASSERT(unit_issuetargetorder(unit, "smart", target));
    T_ASSERT(unit->goalentity == target);
    T_ASSERT(unit->movement.follow_target == NULL);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityAttack);
}

TEST(wc3_unit, die_dispatches_death_event_synchronously) {
    reset_entities();
    setup_test_world();
    edict_t *ent = make_unit(0, 0);
    memset(level.events.queue, 0, sizeof(level.events.queue));
    memset(level.events.handlers, 0, sizeof(level.events.handlers));

    T_ASSERT(run_test_jass("globals\ninteger deaths=0\nendglobals\n"
        "function died takes nothing returns nothing\n"
        "call BJassAssert(IsUnitType(GetDyingUnit(),UNIT_TYPE_DEAD),\"death state committed before delivery\")\n"
        "set deaths=deaths+1\nendfunction\n"
        "function main takes nothing returns nothing\nlocal trigger t=CreateTrigger()\n"
        "call TriggerRegisterPlayerUnitEvent(t,Player(0),EVENT_PLAYER_UNIT_DEATH,null)\n"
        "call TriggerAddAction(t,function died)\nendfunction\n"
        "function check takes nothing returns nothing\n"
        "call BJassAssert(deaths==1,\"death delivered synchronously once\")\nendfunction\n"));
    G_SetHealth(ent, 0);
    unit_die(ent, NULL);
    jass_callbyname(level.vm, "check", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
}

static bool unit_datagram_tint(edict_t *client_ent, uint32_t entity_number, color32_t *out) {
    uint8_t data[1024];
    uint32_t size = G_WriteClientDatagram(client_ent, data, sizeof(data));
    uint32_t offset = 0;
    uint16_t header, count;

    if (!size || size < sizeof(header)) return false;
    memcpy(&header, data, sizeof(header));
    offset += sizeof(header) + (header & BZ_GAME_DATAGRAM_COUNT_MASK) * sizeof(wc3WeatherEffect_t);
    if (header & BZ_GAME_DATAGRAM_LIGHTNING) {
        uint16_t lightning_count;
        if (offset + sizeof(lightning_count) > size) return false;
        memcpy(&lightning_count, data + offset, sizeof(lightning_count));
        offset += sizeof(lightning_count) + lightning_count * sizeof(lightningEffect_t);
    }
    if (!(header & BZ_GAME_DATAGRAM_ENTITY_TINTS) || offset + sizeof(count) > size) return false;
    memcpy(&count, data + offset, sizeof(count));
    offset += sizeof(count);
    FOR_LOOP(i, count) {
        uint16_t number;
        color32_t color;
        if (offset + sizeof(number) + sizeof(color) > size) return false;
        memcpy(&number, data + offset, sizeof(number)); offset += sizeof(number);
        memcpy(&color, data + offset, sizeof(color)); offset += sizeof(color);
        if (number != entity_number) continue;
        if (out) *out = color;
        return true;
    }
    return false;
}

TEST(wc3_unit, hero_dissipate_alpha_tracks_timer_without_mutating_vertex_color) {
    edict_t *hero, *clent;
    color32_t color;

    reset_test_entities();
    setup_test_world();
    game.constants.dissipateTime = 1.0f;
    hero = make_inventory_unit(0, 0);
    hero->s.player = 0;
    hero->health.value = 0.0f;
    hero->svflags |= SVF_MONSTER | SVF_DEADMONSTER;
    hero->think = monster_think;
    hero->vertex_color = MAKE(color32_t, 210, 180, 150, 200);
    hero->vertex_color_set = true;
    clent = &g_edicts[0];
    clent->client = &game.clients[0];
    clent->client->ps.number = 0;

    level.time = 1000;
    unit_begin_decay(hero);
    T_FEQ(G_UnitDissipatePresentationAlpha(hero), 1.0f, 0.001f);
    FOR_LOOP(i, 5) {
        level.time += FRAMETIME;
        G_RunEntities();
    }
    T_FEQ(G_UnitDissipatePresentationAlpha(hero), 0.5f, 0.001f);
    T_ASSERT(unit_datagram_tint(clent, hero->s.number, &color));
    T_EQ(color.r, 210); T_EQ(color.g, 180); T_EQ(color.b, 150);
    T_EQ(color.a, 100);
    T_EQ(hero->vertex_color.a, 200);

    FOR_LOOP(i, 5) {
        level.time += FRAMETIME;
        G_RunEntities();
    }
    T_FEQ(G_UnitDissipatePresentationAlpha(hero), 0.0f, 0.001f);
    T_ASSERT(unit_datagram_tint(clent, hero->s.number, &color));
    T_EQ(color.a, 0);
    T_EQ(hero->vertex_color.a, 200);
    T_ASSERT(hero->revival->awaiting);
    T_ASSERT(hero->s.renderfx & RF_HIDDEN);
}

TEST(wc3_unit, hero_dissipation_marks_same_hero_revivable_and_hidden) {
    reset_test_entities();
    edict_t *hero = make_inventory_unit(0, 0);
    hero->health.value = hero->health.max_value = 500.0f;

    unit_die(hero, NULL);
    T_ASSERT(hero->svflags & SVF_DEADMONSTER);
    T_ASSERT(!hero->revival || !hero->revival->awaiting);

    unit_begin_decay(hero);
    /* Drive exactly one elapsed simulation step without depending on the
     * archive's configured DissipateTime in this unit test. */
    hero->wait = (float)FRAMETIME / 1000.0f;
    unit_decay_think(hero);

    T_ASSERT(hero->inuse);
    T_ASSERT(hero->revival->awaiting);
    T_ASSERT(hero->s.renderfx & RF_HIDDEN);
}

TEST(wc3_unit, scripted_revive_clears_altar_revival_state_on_same_hero) {
    reset_test_entities();
    gameClient_t *client = &game.clients[0];
    edict_t *altar = make_unit(0, 0);
    edict_t *hero = make_inventory_unit(0, 0);
    altar->s.player = hero->s.player = client->ps.number;
    altar->build = hero;
    hero->health.max_value = 500.0f;
    hero->health.value = 0.0f;
    hero->mana.max_value = 300.0f;
    hero->svflags |= SVF_DEADMONSTER;
    hero->s.flags |= EF_NOT_SELECTABLE;
    hero->s.renderfx |= RF_HIDDEN;
    if (!hero->revival) hero->revival = G_AllocRevival();
    assert(hero->revival);
    hero->revival->awaiting = true;
    hero->revival->reviving = true;
    hero->revival->producer = altar;
    hero->revival->player = client->ps.number;
    hero->revival->gold = 100;
    hero->revival->lumber = 50;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    T_ASSERT(G_ReviveHero(hero, 64.0f, 96.0f));

    T_NULL(altar->build);
    T_ASSERT(hero->inuse);
    T_ASSERT(!(hero->svflags & SVF_DEADMONSTER));
    T_ASSERT(!(hero->s.flags & EF_NOT_SELECTABLE));
    T_ASSERT(!(hero->s.renderfx & RF_HIDDEN));
    T_ASSERT(!hero->revival || !hero->revival->awaiting);
    T_ASSERT(!hero->revival || !hero->revival->reviving);
    T_NULL(hero->revival->producer);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 50);
    T_EQ((int)hero->s.origin2.x, 64);
    T_EQ((int)hero->s.origin2.y, 96);
    T_FEQ(hero->s.origin.x, 64.0f, 0.001f);
    T_FEQ(hero->s.origin.y, 96.0f, 0.001f);
    T_ASSERT(hero->health.value > 0.0f);
    G_SelectEntity(client, hero);
    T_ASSERT(G_IsEntitySelected(client, hero));
}

TEST(wc3_unit, removing_producer_cancels_mixed_revival_and_training_queue) {
    static UnitProfile_t const revive_profile = { .revive = "1" };
    reset_test_entities();
    gameClient_t *client = &game.clients[0];
    edict_t *altar = make_unit(0, 0);
    edict_t *hero = make_inventory_unit(0, 0);
    edict_t *trainee = make_unit(0, 0);
    int32_t gold = MAX(0, trainee->data.UnitBalance->goldCost);
    int32_t lumber = MAX(0, trainee->data.UnitBalance->lumberCost);

    altar->s.player = hero->s.player = trainee->s.player = client->ps.number;
    altar->data.UnitProfile = &revive_profile;
    altar->build = hero;
    if (!hero->revival) hero->revival = G_AllocRevival();
    assert(hero->revival);
    hero->revival->awaiting = true;
    hero->revival->reviving = true;
    hero->revival->producer = altar;
    hero->revival->queue_next = trainee;
    hero->revival->player = client->ps.number;
    hero->revival->gold = 100;
    hero->revival->lumber = 50;
    trainee->training = true;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    G_FreeEdict(altar);

    T_ASSERT(!altar->inuse);
    T_ASSERT(hero->inuse);
    T_ASSERT(!hero->revival || !hero->revival->reviving);
    T_NULL(hero->revival->producer);
    T_ASSERT(!trainee->inuse);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 100 + gold);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 50 + lumber);
}

TEST(wc3_unit, worker_death_does_not_walk_construction_target_as_production_queue) {
    reset_test_entities();
    edict_t *worker = make_unit(0, 0);
    edict_t *building = make_unit(0, 0);

    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->build = building;
    worker->build = building;

    unit_die(worker, NULL);

    T_ASSERT(worker->svflags & SVF_DEADMONSTER);
    T_ASSERT(building->construction);
    T_ASSERT(building->build == building);
}

TEST(wc3_unit, ownership_change_does_not_walk_constructing_revive_altar) {
    static UnitProfile_t const revive_profile = { .revive = "1" };
    reset_test_entities();
    gameClient_t *old_client = &game.clients[0];
    gameClient_t *new_client = &game.clients[1];
    edict_t *altar = make_unit(0, 0);

    altar->data.UnitProfile = &revive_profile;
    altar->s.player = old_client->ps.number;
    if (!altar->construction) altar->construction = G_AllocConstruction();
    assert(altar->construction);
    altar->build = altar;

    G_SetUnitPlayer(altar, new_client->ps.number);

    T_EQ(altar->s.player, new_client->ps.number);
    T_ASSERT(altar->construction);
    T_ASSERT(altar->build == altar);
}

TEST(wc3_unit, ownership_change_does_not_walk_legacy_constructing_revive_altar) {
    static UnitProfile_t const revive_profile = { .revive = "1" };
    reset_test_entities();
    gameClient_t *new_client = &game.clients[1];
    edict_t *altar = make_unit(0, 0);

    altar->data.UnitProfile = &revive_profile;
    altar->s.player = game.clients[0].ps.number;
    altar->build = altar;

    G_SetUnitPlayer(altar, new_client->ps.number);

    T_EQ(altar->s.player, new_client->ps.number);
    T_ASSERT(!altar->construction);
    T_ASSERT(altar->build == altar);
}

TEST(wc3_unit, hero_revive_cleanup_stops_on_cyclic_production_queue) {
    static UnitProfile_t const revive_profile = { .revive = "1" };
    reset_test_entities();
    edict_t *altar = make_unit(0, 0);
    edict_t *first = make_unit(0, 0);
    edict_t *second = make_unit(0, 0);

    altar->data.UnitProfile = &revive_profile;
    altar->build = first;
    first->training = second->training = true;
    first->build = second;
    second->build = first;

    G_CancelHeroRevives(altar);

    T_ASSERT(altar->build == first);
    T_ASSERT(first->build == second);
    T_ASSERT(second->build == first);
}

/* -----------------------------------------------------------------------
 * Order tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, issueorder_move_creates_waypoint) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vec2_t dest = {100.0f, 0.0f};
    bool result = unit_issueorder(ent, "move", &dest);
    T_ASSERT(result);
    T_NOT_NULL(ent->goalentity);
}

TEST(wc3_unit, issueorder_move_sets_walk_animation) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vec2_t dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);
    T_NOT_NULL(ent->currentmove);
    T_STREQ(ent->currentmove->animation, "walk");
}

TEST(wc3_unit, shift_move_starts_immediately_when_idle_then_queues_fifo) {
    reset_test_entities();
    setup_test_world();
    edict_t *ent = make_unit(0, 0);
    vec2_t a = { 96.0f, 0.0f };
    vec2_t b = { 192.0f, 0.0f };
    vec2_t c = { 288.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "walk");
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &c, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 2);

    unit_stand(ent);
    T_EQ(G_UnitQueuedOrderCount(ent), 1);
    T_FEQ(ent->goalentity->s.origin2.x, b.x, 0.01f);

    unit_stand(ent);
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_FEQ(ent->goalentity->s.origin2.x, c.x, 0.01f);
}

TEST(wc3_unit, nonqueued_move_replaces_pending_shift_orders) {
    reset_test_entities();
    setup_test_world();
    edict_t *ent = make_unit(0, 0);
    vec2_t a = { 96.0f, 0.0f };
    vec2_t b = { 192.0f, 0.0f };
    vec2_t replacement = { 320.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 1);

    T_ASSERT(unit_issueorder(ent, "move", &replacement));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_FEQ(ent->goalentity->s.origin2.x, replacement.x, 0.01f);
}

TEST(wc3_unit, militia_target_order_reaches_militia_behavior) {
    static UnitAbilities_t const worker_abilities = { .abilList = "Amil" };
    static UnitAbilities_t const hall_abilities = { .abilList = "Amic" };
    edict_t *worker;
    edict_t *hall;

    reset_test_entities();
    setup_test_world();
    worker = make_unit(0, 0);
    hall = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 256, 0);
    worker->data.UnitAbilities = &worker_abilities;
    hall->data.UnitAbilities = &hall_abilities;
    worker->s.player = hall->s.player = 1;
    hall->pathtex = NULL;

    T_ASSERT(G_IssueUnitTargetOrder(worker, "militia", hall, false, worker->s.player));
    T_ASSERT(worker->militia->partner == hall);
    T_NOT_NULL(worker->currentmove);
    T_ASSERT(worker->currentmove->proc == CAbilityMilitia);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, stale_queued_entity_target_is_skipped_for_next_order) {
    reset_test_entities();
    setup_test_world();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = make_unit(128, 0);
    vec2_t a = { 64.0f, 0.0f };
    vec2_t b = { 256.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitTargetOrder(ent, "attack", target, true, 0));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 2);

    /* Simulate recycling the target slot before the queued Attack begins. */
    target->spawn_time++;
    unit_stand(ent);

    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "walk");
    T_FEQ(ent->goalentity->s.origin2.x, b.x, 0.01f);
}

TEST(wc3_unit, stop_clears_pending_shift_orders) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vec2_t a = { 96.0f, 0.0f };
    vec2_t b = { 192.0f, 0.0f };

    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &a, true, 0, 0.0f));
    T_ASSERT(G_IssueUnitPointOrder(ent, "move", &b, true, 0, 0.0f));
    T_EQ(G_UnitQueuedOrderCount(ent), 1);

    T_ASSERT(unit_issueimmediateorder(ent, "stop"));
    T_EQ(G_UnitQueuedOrderCount(ent), 0);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, point_attack_order_uses_attack_move_behavior) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vec2_t dest = { 128.0f, 0.0f };

    T_ASSERT(unit_issueorder(ent, "attack", &dest));
    T_NOT_NULL(ent->movement.attackmove_waypoint);
    T_ASSERT(ent->goalentity == ent->movement.attackmove_waypoint);
}

TEST(wc3_unit, issueorder_move_preserves_combat_state) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    edict_t *target = G_Spawn();
    target->class_id = MAKEFOURCC('h','f','o','o');
    G_BindEntityData(target);
    target->s.origin2 = (vec2_t){50, 0};
    target->s.model = 1;
    target->inuse = true;
    target->health.value = 100.0f;
    target->health.max_value = 100.0f;
    vec2_t dest = {100.0f, 0.0f};

    unit_entercombat(ent, target);
    unit_issueorder(ent, "move", &dest);

    T_ASSERT(unit_affectingcombat(ent));
    T_STREQ(ent->currentmove->animation, "walk");
}

TEST(wc3_unit, issueorder_unknown_returns_false) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vec2_t dest = {100.0f, 0.0f};
    bool result = unit_issueorder(ent, "missingorder", &dest);
    T_ASSERT(!result);
}

TEST(wc3_unit, issueorder_null_inputs_return_false) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vec2_t dest = {100.0f, 0.0f};

    T_ASSERT(!unit_issueorder(NULL, "move", &dest));
    T_ASSERT(!unit_issueorder(ent, NULL, &dest));
    T_ASSERT(!unit_issueorder(ent, "move", NULL));
}

TEST(wc3_unit, issueimmediateorder_stop) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vec2_t dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);
    T_STREQ(ent->currentmove->animation, "walk");

    bool result = unit_issueimmediateorder(ent, "stop");
    T_ASSERT(result);
    T_STREQ(ent->currentmove->animation, "stand");
}

TEST(wc3_unit, issueimmediateorder_holdposition_uses_hold_state) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    vec2_t dest = {100.0f, 0.0f};
    unit_issueorder(ent, "move", &dest);

    T_ASSERT(unit_issueimmediateorder(ent, "holdposition"));
    T_ASSERT(ent->movement.holding_position);
    T_STREQ(ent->currentmove->animation, "stand");
}

static void install_raven_form_test_data(slkTestData_t **ability_rows, slkTestData_t **old_ability,
                                         slkTestData_t **ui_rows, slkTestData_t **old_ui,
                                         slkTestData_t **profile_rows, slkTestData_t **old_profile) {
    static cstring_t const ability_slk =
        "ID;PWXL;N;EBB;Y4;X4\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"UnitID1\"\n"
        "C;Y2;X1;K\"Amrf\"\n"
        "C;Y2;X2;K\"Amrf\"\n"
        "C;Y2;X3;K\"hpea\"\n"
        "C;Y2;X4;K\"hfoo\"\n"
        "C;Y3;X1;K\"Arav\"\n"
        "C;Y3;X2;K\"Arav\"\n"
        "C;Y3;X3;K\"edot\"\n"
        "C;Y3;X4;K\"edtm\"\n"
        "C;Y4;X1;K\"Astn\"\n"
        "C;Y4;X2;K\"Astn\"\n"
        "C;Y4;X3;K\"hpea\"\n"
        "C;Y4;X4;K\"hfoo\"\n"
        "E\n";
    static cstring_t const ui_slk =
        "ID;PWXL;N;EBB;Y3;X7\n"
        "C;Y1;X1;K\"unitUIID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"modelScale\"\n"
        "C;Y1;X4;K\"scale\"\n"
        "C;Y1;X5;K\"red\"\n"
        "C;Y1;X6;K\"green\"\n"
        "C;Y1;X7;K\"blue\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y2;X3;K1\n"
        "C;Y2;X4;K1\n"
        "C;Y2;X5;K224\n"
        "C;Y2;X6;K232\n"
        "C;Y2;X7;K255\n"
        "C;Y3;X1;K\"hfoo\"\n"
        "C;Y3;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y3;X3;K1\n"
        "C;Y3;X4;K1\n"
        "E\n";
    static cstring_t const profile_slk =
        "ID;PWXL;N;EBB;Y3;X2\n"
        "C;Y1;X1;K\"unitID\"\n"
        "C;Y1;X2;K\"animProps\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K\"\"\n"
        "C;Y3;X1;K\"hfoo\"\n"
        "C;Y3;X2;K\"alternateex\"\n"
        "E\n";

    *ability_rows = parse_slk_string(ability_slk);
    *ui_rows = parse_slk_string(ui_slk);
    *profile_rows = parse_slk_string(profile_slk);
    T_NOT_NULL(*ability_rows); T_NOT_NULL(*ui_rows); T_NOT_NULL(*profile_rows);
    *old_ability = G_SetSLKRows("AbilityData", *ability_rows);
    *old_ui = G_SetSLKRows("UnitUI", *ui_rows);
    *old_profile = G_SetProfileRows(*profile_rows);
    T_NOT_NULL(*old_ability); T_NOT_NULL(*old_ui); T_NOT_NULL(*old_profile);
}

static void restore_raven_form_test_data(slkTestData_t *ability_rows, slkTestData_t *old_ability,
                                         slkTestData_t *ui_rows, slkTestData_t *old_ui,
                                         slkTestData_t *profile_rows, slkTestData_t *old_profile) {
    G_SetProfileRows(old_profile);
    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(profile_rows);
    free_slk_rows(ui_rows);
    free_slk_rows(ability_rows);
}

/* Stock-shaped flight endpoint for the synthetic Medivh presentation model. */
static mapInfo_t const *install_raven_flight_endpoint(void) {
    static mapInfo_t info;
    static unitModification_t mod={.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data="fly"};
    static unitData_t alternate={.originalUnitID=MAKEFOURCC('h','f','o','o'),.numbeOfModifications=1,.modifications=&mod};
    mapInfo_t const *saved=level.mapinfo;info=*saved;
    info.num_originalUnits=1;info.originalUnits=&alternate;
    level.mapinfo=&info;G_SetMapUnitOverrides(&info);return saved;
}
static void finish_test_morph(edict_t *unit) {
    unsigned fired=0;
    while(unit->morph && unit->morph->transition.active) {
        abilityTimerRequest_t request;
        bool found=S_NextAbilityPrimaryTimer(&request);
        T_ASSERT(found);T_ASSERT(++fired<128);if(!found || fired>=128)break;
        level.pathing_clock=request.deadline;
        abilityCall_t call={.primary_timer=&request};request.proc(NULL,A_PRIMARY_TIMER_FIRE,&call);
    }
}

/* Native326: authored Dur owns the transition, with DataC added on landing.
 * Animation end and unit cast point do not substitute for this primary timer. */
TEST(wc3_unit, morph326_public_orders_retain_form_and_count_until_authored_deadline) {
    for(unsigned action=0;action<8;action++)for(unsigned schema=0;schema<2;schema++)for(unsigned raven=0;raven<2;raven++)for(unsigned reverse=0;reverse<2;reverse++) {
        slkTestData_t *ability_rows,*old_ability,*ui_rows,*old_ui,*profile_rows,*old_profile;
        reset_test_entities();setup_test_world();
        install_raven_form_test_data(&ability_rows,&old_ability,&ui_rows,&old_ui,&profile_rows,&old_profile);
        char data[1024];snprintf(data,sizeof(data),
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"%s\"\n"
            "C;X1;Y2;K\"%s\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\n"
            "C;X5;K0.375\nC;X6;K0.625\nE\n",schema ? "Data11" : "DataA1",schema ? "Data13" : "DataC1",
            raven ? "Amrf" : "Abrf",raven ? "Amrf" : "Abrf");
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        mapInfo_t const *saved_map=level.mapinfo;mapInfo_t info=*saved_map;
        unitModification_t mod={.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data="fly"};
        unitData_t alternate={.originalUnitID=MAKEFOURCC('h','f','o','o'),.numbeOfModifications=1,.modifications=&mod};
        if(raven){info.num_originalUnits=1;info.originalUnits=&alternate;level.mapinfo=&info;G_SetMapUnitOverrides(&info);}
        uint32_t base=MAKEFOURCC('h','p','e','a'),alt=MAKEFOURCC('h','f','o','o');
        uint32_t code=raven ? MAKEFOURCC('A','m','r','f') : MAKEFOURCC('A','b','r','f');
        edict_t *unit=alloc_test_unit(reverse ? alt : base,64,64);
        unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        T_ASSERT(G_ActorAddSkill(unit,code));
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        cstring_t order=raven ? (reverse ? "unravenform" : "ravenform") : (reverse ? "unbearform" : "bearform");
        uint32_t order_id=raven ? (reverse ? 852156 : 852155) : (reverse ? 852139 : 852138);
        T_ASSERT(unit_issueimmediateorder(unit,order));
        T_EQ(unit->class_id,raven && !reverse ? alt : reverse ? alt : base);
        T_EQ(unit->movement.repulse.disable_depth,1);
        T_EQ(unit->current_order_id,order_id);
        if(action==1 || action==5) {
            T_ASSERT(unit_issueimmediateorder(unit,"stop"));
            T_EQ(unit->current_order_id,order_id);
        } else if(action==2) {
            T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){256,64}));
            T_EQ(unit->current_order_id,order_id);
        } else if(action==7) {
            edict_t *follow=alloc_test_unit(base,256,64);
            follow->svflags|=SVF_MONSTER;follow->s.player=unit->s.player;
            T_ASSERT(unit_issuetargetorder(unit,"move",follow));
            T_EQ(unit->current_order_id,order_id);
        } else if(action==3) {
            S_SetUnitPaused(unit,true);T_ASSERT(unit->paused);
            T_ASSERT(!S_UnitExecutionPaused(unit));
            T_EQ(unit->movement.repulse.disable_depth,1);
        } else if(action==4)unit_die(unit,NULL);
        else if(action==6) {
            T_ASSERT(G_ActorRemoveSkill(unit,code));
            T_EQ(unit->class_id,base);
            T_EQ(unit->movement.repulse.disable_depth,0);
            T_EQ(unit->current_order_id,0);
            T_ASSERT(!unit->morph->transition.active);
        }
        if(action==5) {
            uint32_t slot=unit-g_edicts;cstring_t file=Test_TempPath("wc3-morph326.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+slot;
            T_EQ(unit->movement.repulse.disable_depth,1);T_EQ(unit->current_order_id,order_id);
        }
        float deadline=1.375f+(raven && reverse ? .625f : 0);
        abilityTimerRequest_t request;
        if(action!=4 && action!=6) {
            T_ASSERT(S_NextAbilityPrimaryTimer(&request));
            if(S_NextAbilityPrimaryTimer(&request))T_EQ(wc3_float_bits(request.deadline.time),wc3_float_bits(deadline));
        }
        unsigned fired=0;
        while(S_NextAbilityPrimaryTimer(&request) && request.deadline.time<=deadline) {
            T_ASSERT(++fired<128);if(fired>=128)break;
            level.pathing_clock=request.deadline;
            abilityCall_t call={.primary_timer=&request};request.proc(NULL,A_PRIMARY_TIMER_FIRE,&call);
        }
        T_EQ(unit->class_id,action==6 ? base : action==4 ? (raven && !reverse ? alt : reverse ? alt : base) : reverse ? base : alt);
        T_EQ(unit->movement.repulse.disable_depth,(action==3 || action==4) ? 1 : 0);
        T_EQ(unit->current_order_id,(action==2 || action==7) ? 851986 : action==3 ? 851973 : 0);
        if(action==3) {
            T_ASSERT(unit->paused);S_SetUnitPaused(unit,false);
            level.pathing_clock.time=deadline+.05f;S_RunMoveTimers();
            T_EQ(unit->movement.repulse.disable_depth,0);
        }
        reset_entities();level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
        restore_raven_form_test_data(ability_rows,old_ability,ui_rows,old_ui,profile_rows,old_profile);
        free_slk_rows(timing);
    }
}

/* Native330: normal Dur times both transitions; HeroDur starts at the forward
 * completion. The form lifetime is not a movement-suppression contribution. */
static void morph330_advance(float deadline) {
    abilityTimerRequest_t request;
    unsigned fired=0;
    while(S_NextAbilityPrimaryTimer(&request) && request.deadline.time<=deadline) {
        T_ASSERT(++fired<128);if(fired>=128)break;
        level.pathing_clock=request.deadline;
        abilityCall_t call={.primary_timer=&request};
        request.proc(NULL,A_PRIMARY_TIMER_FIRE,&call);
    }
    level.pathing_clock.time=deadline;
}

TEST(wc3_unit, morph330_timed_form_owns_both_transitions_and_saved_lifetime) {
    for(unsigned family=0;family<2;family++)for(unsigned schema=0;schema<2;schema++)for(unsigned action=0;action<3;action++) {
        slkTestData_t *ability_rows,*old_ability,*ui_rows,*old_ui,*profile_rows,*old_profile;
        reset_test_entities();setup_test_world();
        install_raven_form_test_data(&ability_rows,&old_ability,&ui_rows,&old_ui,&profile_rows,&old_profile);
        cstring_t code_name=family ? "ANcr" : "AEme";
        cstring_t order=family ? "chemicalrage" : "metamorphosis";
        uint32_t code=family ? MAKEFOURCC('A','N','c','r') : MAKEFOURCC('A','E','m','e');
        char data[1024];snprintf(data,sizeof(data),
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"%s\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\n"
            "C;X5;K0.375\nC;X6;K0.625\nE\n",schema ? "Data11" : "DataA1",code_name,code_name);
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        uint32_t base=MAKEFOURCC('h','p','e','a'),alt=MAKEFOURCC('h','f','o','o');
        edict_t *unit=alloc_test_unit(base,64,64);
        unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        T_ASSERT(G_ActorAddSkill(unit,code));
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(unit_issueimmediateorder(unit,order));
        T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,1);
        if(action==1) {
            T_ASSERT(unit_issueimmediateorder(unit,"stop"));
            T_EQ(unit->current_order_id,G_OrderId(order));
        }
        morph330_advance(1.374f);
        T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,1);
        morph330_advance(1.375f);
        T_EQ(unit->class_id,alt);T_EQ(unit->movement.repulse.disable_depth,0);
        if(action==2) {
            uint32_t slot=unit-g_edicts;cstring_t file=Test_TempPath("wc3-morph330.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+slot;
        }
        morph330_advance(1.999f);
        T_EQ(unit->class_id,alt);T_EQ(unit->movement.repulse.disable_depth,0);
        morph330_advance(2.0f);
        T_EQ(unit->class_id,alt);T_EQ(unit->movement.repulse.disable_depth,1);
        morph330_advance(2.374f);
        T_EQ(unit->class_id,alt);
        morph330_advance(2.375f);
        T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,0);
        reset_entities();
        restore_raven_form_test_data(ability_rows,old_ability,ui_rows,old_ui,profile_rows,old_profile);
        free_slk_rows(timing);
    }
}

/* Retail Morph330c: Burrow and Robo-Goblin retain the source type through
 * both counted transitions. Both directions are accepted when the player has
 * the required resources (the underfunded exploratory maps are not controls). */
TEST(wc3_unit, morph331_reversible_ground_orders_preserve_transition_and_save) {
    for(unsigned family=0;family<2;family++)for(unsigned schema=0;schema<2;schema++)for(unsigned action=0;action<4;action++) {
        slkTestData_t *ability_rows,*old_ability,*ui_rows,*old_ui,*profile_rows,*old_profile;
        reset_test_entities();setup_test_world();
        install_raven_form_test_data(&ability_rows,&old_ability,&ui_rows,&old_ui,&profile_rows,&old_profile);
        cstring_t parent=family ? "ANrg" : "Abur",alias=family ? "Ar33" : "Ab33";
        cstring_t orders[2]={family ? "robogoblin" : "burrow",family ? "unrobogoblin" : "unburrow"};
        uint32_t code=FS_SLKKey(alias),base=MAKEFOURCC('h','p','e','a'),alt=MAKEFOURCC('h','f','o','o');
        char data[1024];snprintf(data,sizeof(data),
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"%s\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\n"
            "C;X5;K0.375\nC;X6;K0\nE\n",schema ? "Data11" : "DataA1",alias,parent);
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        edict_t *unit=alloc_test_unit(base,64,64);
        unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        T_ASSERT(G_ActorAddSkill(unit,code));
        if(family && action==0) {
            edict_t *existing=alloc_test_unit(alt,512,512);existing->s.player=unit->s.player;
            T_ASSERT(G_BotConvertUnits(&game.clients[unit->s.player].ps,1,base));
            G_FreeEdict(existing);
        }
        for(unsigned reverse=0;reverse<2;reverse++) {
            level.pathing_clock=(wc3Clock_t){.time=1+reverse,.span=300};
            bool accepted=unit_issueimmediateorder(unit,orders[reverse]);
            T_ASSERT(accepted);
            uint32_t source=reverse ? alt : base,target=reverse ? base : alt;
            T_EQ(unit->class_id,source);T_EQ(unit->movement.repulse.disable_depth,1);
            T_EQ(unit->current_order_id,G_OrderId(orders[reverse]));
            T_EQ(S_UnitMoveCategory(unit),0xca);
            if(action==1) {
                T_ASSERT(unit_issueimmediateorder(unit,"stop"));
                T_EQ(unit->current_order_id,G_OrderId(orders[reverse]));
            } else if(action==3) {
                S_SetUnitPaused(unit,true);T_ASSERT(!S_UnitExecutionPaused(unit));
            } else if(action==2) {
                uint32_t slot=unit-g_edicts;cstring_t file=Test_TempPath("wc3-morph331.bin");
                T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+slot;
            }
            morph330_advance(1.374f+reverse);
            T_EQ(unit->class_id,source);T_EQ(unit->movement.repulse.disable_depth,1);
            morph330_advance(1.375f+reverse);
            T_EQ(unit->class_id,target);T_EQ(unit->movement.repulse.disable_depth,action==3 ? 1 : 0);
            T_EQ(S_UnitMoveCategory(unit),family || reverse ? 0xca : 0xc8);
            if(action==3) {
                T_ASSERT(S_UnitExecutionPaused(unit));S_SetUnitPaused(unit,false);
                morph330_advance(1.425f+reverse);S_RunMoveTimers();
                T_EQ(unit->movement.repulse.disable_depth,0);
            }
            T_EQ(unit->current_order_id,0);
            if(action==2 && !reverse) {
                uint32_t slot=unit-g_edicts;cstring_t file=Test_TempPath("wc3-morph331-form.bin");
                T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+slot;
                T_EQ(unit->class_id,alt);T_EQ(S_UnitMoveCategory(unit),family ? 0xca : 0xc8);
                T_EQ(unit->movement.repulse.disable_depth,0);
            }
        }
        reset_entities();
        restore_raven_form_test_data(ability_rows,old_ability,ui_rows,old_ui,profile_rows,old_profile);
        free_slk_rows(timing);
    }
}

/* Morph330c: flight takeoff rebinds before suppression; landing keeps the
 * flying type but publishes a ground mover until Dur+DataC has elapsed. */
TEST(wc3_unit, morph332_flight_forms_preserve_type_timing_and_saved_transition) {
    for(unsigned family=0;family<2;family++)for(unsigned schema=0;schema<2;schema++)for(unsigned action=0;action<4;action++) {
        slkTestData_t *ability_rows,*old_ability,*ui_rows,*old_ui,*profile_rows,*old_profile;
        reset_test_entities();setup_test_world();
        install_raven_form_test_data(&ability_rows,&old_ability,&ui_rows,&old_ui,&profile_rows,&old_profile);
        uint32_t base=MAKEFOURCC('h','p','e','a'),alt=MAKEFOURCC('h','f','o','o');
        cstring_t parent=family ? "Aave" : "Astn",alias=family ? "Aa32" : "As32";
        uint32_t code=FS_SLKKey(alias);
        char data[1024];snprintf(data,sizeof(data),
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"%s\"\nC;X7;K\"Cast1\"\n"
            "C;X1;Y2;K\"%s\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\n"
            "C;X5;K0.375\nC;X6;K0.625\nC;X7;K2.25\nE\n",
            schema ? "Data11" : "DataA1",schema ? "Data13" : "DataC1",alias,parent);
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        mapInfo_t const *saved_map=level.mapinfo;mapInfo_t info=*saved_map;
        unitModification_t mod={.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data="fly"};
        unitData_t flying={.originalUnitID=family ? alt : base,.numbeOfModifications=1,.modifications=&mod};
        info.num_originalUnits=1;info.originalUnits=&flying;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
        edict_t *unit=alloc_test_unit(base,64,64);
        unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        if(!family)unit->aiflags|=AI_FLYING;
        T_ASSERT(G_ActorAddSkill(unit,code));
        for(unsigned reverse=0;reverse<(family ? 1 : 2);reverse++) {
            float start=1+reverse*2,delay=family || reverse ? .375f : 1.0f;
            uint32_t source=reverse ? alt : base,target=reverse ? base : alt;
            cstring_t order=family ? "avengerform" : reverse ? "unstoneform" : "stoneform";
            level.pathing_clock=(wc3Clock_t){.time=start,.span=300};
            T_ASSERT(unit_issueimmediateorder(unit,order));
            T_EQ(unit->class_id,family || reverse ? target : source);
            T_EQ((unit->aiflags&AI_FLYING)!=0,(family || reverse)!=0);
            T_EQ(unit->movement.repulse.disable_depth,1);T_EQ(unit->current_order_id,G_OrderId(order));
            if(action==1) {
                T_ASSERT(unit_issueimmediateorder(unit,"stop"));T_EQ(unit->current_order_id,G_OrderId(order));
            } else if(action==2) {
                S_SetUnitPaused(unit,true);T_ASSERT(!S_UnitExecutionPaused(unit));
            } else if(action==3) {
                uint32_t slot=unit-g_edicts;cstring_t file=Test_TempPath("wc3-morph332.bin");
                T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+slot;
            }
            morph330_advance(start+delay-.001f);
            T_EQ(unit->class_id,family || reverse ? target : source);T_EQ(unit->movement.repulse.disable_depth,1);
            morph330_advance(start+delay);
            T_EQ(unit->class_id,target);T_EQ(unit->movement.repulse.disable_depth,action==2 ? 1 : 0);
            if(action==2) {
                T_ASSERT(S_UnitExecutionPaused(unit));S_SetUnitPaused(unit,false);
                morph330_advance(start+delay+.05f);S_RunMoveTimers();T_EQ(unit->movement.repulse.disable_depth,0);
            }
            T_EQ(unit->current_order_id,0);
        }
        reset_entities();level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
        restore_raven_form_test_data(ability_rows,old_ability,ui_rows,old_ui,profile_rows,old_profile);
        free_slk_rows(timing);
    }
}

/* Native334: DataB bit8 prevents removal from reversing the form. An early
 * removal cancels its timer without releasing the counted contribution;
 * ordinary completion removes the permanent morph ability itself. */
TEST(wc3_unit, morph334_permanent_flag_controls_removal_and_completion) {
    for(unsigned family=0;family<2;family++)for(unsigned schema=0;schema<2;schema++)
    for(unsigned permanent=0;permanent<2;permanent++)for(unsigned action=0;action<3;action++) {
        slkTestData_t *ability_rows,*old_ability,*ui_rows,*old_ui,*profile_rows,*old_profile;
        reset_test_entities();setup_test_world();
        install_raven_form_test_data(&ability_rows,&old_ability,&ui_rows,&old_ui,&profile_rows,&old_profile);
        uint32_t base=MAKEFOURCC('h','p','e','a'),alt=MAKEFOURCC('h','f','o','o');
        cstring_t parent=family ? "Aave" : "Astn",alias=family ? "Aa34" : "As34";
        uint32_t code=FS_SLKKey(alias);
        char data[1024];snprintf(data,sizeof(data),
            "ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"%s\"\nC;X7;K\"%s\"\n"
            "C;X1;Y2;K\"%s\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\n"
            "C;X5;K0.375\nC;X6;K0.625\nC;X7;K%u\nE\n",
            schema ? "Data11" : "DataA1",schema ? "Data13" : "DataC1",
            schema ? "Data12" : "DataB1",alias,parent,permanent ? 8u : 0u);
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        mapInfo_t const *saved_map=level.mapinfo;mapInfo_t info=*saved_map;
        float height=96;
        unitModification_t mods[]={
            {.modID=MAKEFOURCC('u','m','v','t'),.type=mod_string,.data="fly"},
            {.modID=MAKEFOURCC('u','m','v','h'),.type=mod_real,.data=&height}};
        unitData_t flying={.originalUnitID=family ? alt : base,.numbeOfModifications=2,.modifications=mods};
        info.num_originalUnits=1;info.originalUnits=&flying;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
        edict_t *unit=alloc_test_unit(base,64,64);
        unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        if(!family)unit->aiflags|=AI_FLYING;
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        if(action==0) {
            vec2_t destination={512,64};
            T_ASSERT(unit_issueorder(unit,"move",&destination));
            T_ASSERT(G_ActorRemoveSkill(unit,code));
            T_EQ(unit->current_order_id,G_OrderId("move"));
            T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        }
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(unit_issueimmediateorder(unit,family ? "avengerform" : "stoneform"));
        T_EQ(unit->movement.repulse.disable_depth,1);
        float delay=family ? .375f : 1.0f;
        if(action==1) {
            morph330_advance(1+delay);
            T_EQ(unit->class_id,alt);T_EQ(unit->movement.repulse.disable_depth,0);
            if(family) {
                T_NOT_NULL(unit->raven);
                T_EQ(unit->raven->rise_state,RAVEN_RISE_ACTIVE);
                /* Removing the completed permanent ability must not strand
                 * its independent height movement at ground level. */
                level.time=(uint32_t)unit->raven->rise_start+625;
                monster_think(unit);T_FEQ(unit->unitinfo.FlyHeight,96,.001f);
            }
            T_EQ(G_UnitAbilityLevel(unit,code)!=0,!permanent);
            T_EQ(G_ActorRemoveSkill(unit,code),!permanent);
        } else if(action==0) {
            morph330_advance(1.1f);T_ASSERT(G_ActorRemoveSkill(unit,code));
            T_EQ(unit->current_order_id,0);
        } else {
            unit_die(unit,NULL);T_ASSERT(M_IsDead(unit));
        }
        uint32_t expected_type=action==2 || (action==0 && permanent) ? (family ? alt : base) : permanent ? alt : base;
        int expected_depth=action==2 || (action==0 && permanent) ? 1 : 0;
        T_EQ(unit->class_id,expected_type);T_EQ(unit->movement.repulse.disable_depth,expected_depth);
        if(action==0) {
            uint32_t slot=unit-g_edicts;cstring_t file=Test_TempPath("wc3-morph334.bin");
            T_ASSERT(WriteGame(file));T_ASSERT(ReadGame(file));remove(file);unit=g_edicts+slot;
        }
        morph330_advance(2+delay);
        T_EQ(unit->class_id,expected_type);T_EQ(unit->movement.repulse.disable_depth,expected_depth);
        reset_entities();level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
        restore_raven_form_test_data(ability_rows,old_ability,ui_rows,old_ui,profile_rows,old_profile);
        free_slk_rows(timing);
    }
}

TEST(wc3_unit, morph326_remove_inactive_base_ability_preserves_move) {
    slkTestData_t *ability_rows,*old_ability,*ui_rows,*old_ui,*profile_rows,*old_profile;
    reset_test_entities();setup_test_world();
    install_raven_form_test_data(&ability_rows,&old_ability,&ui_rows,&old_ui,&profile_rows,&old_profile);
    edict_t *unit=alloc_test_unit(MAKEFOURCC('h','p','e','a'),64,64);
    T_ASSERT(G_ActorAddSkill(unit,MAKEFOURCC('A','m','r','f')));
    T_ASSERT(unit_issueorder(unit,"move",&(vec2_t){256,64}));
    umove_t const *move=unit->currentmove;edict_t *goal=unit->goalentity;
    T_EQ(unit->current_order_id,851986);
    T_ASSERT(G_ActorRemoveSkill(unit,MAKEFOURCC('A','m','r','f')));
    T_EQ(unit->current_order_id,851986);T_EQ(unit->currentmove,move);T_EQ(unit->goalentity,goal);
    restore_raven_form_test_data(ability_rows,old_ability,ui_rows,old_ui,profile_rows,old_profile);
}

TEST(wc3_unit, ravenform_immediate_orders_transform_between_ability_data_types) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    edict_t *ent;

    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    mapInfo_t const *saved_map=install_raven_flight_endpoint();
    ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    G_InitializeUnitVertexColor(ent);
    T_ASSERT(ent->vertex_color_set);
    T_EQ(ent->vertex_color.r, 224); T_EQ(ent->vertex_color.g, 232);
    T_EQ(ent->vertex_color.b, 255); T_EQ(ent->vertex_color.a, 255);
    T_ASSERT(G_ActorAddSkill(ent, MAKEFOURCC('A','m','r','f')));
    G_ApplyTemporaryMaxHealthBonus(ent, 123.0f);
    G_ApplyTemporaryMaxManaBonus(ent, 77.0f);

    T_ASSERT(unit_issueimmediateorder(ent, "ravenform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','f','o','o'));
    T_FEQ(ent->health.max_value, ent->data.UnitBalance->maxHealth + 123.0f, 0.001f);
    T_FEQ(ent->mana.max_value, ent->data.UnitBalance->maxMana + 77.0f, 0.001f);
    T_STREQ(G_UnitAnimationProperties(ent), "alternateex");
    T_ASSERT(!ent->vertex_color_set);
    T_EQ(ent->vertex_color.r, 255); T_EQ(ent->vertex_color.g, 255);
    T_EQ(ent->vertex_color.b, 255); T_EQ(ent->vertex_color.a, 255);
    finish_test_morph(ent);
    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    finish_test_morph(ent);
    T_EQ(ent->class_id, MAKEFOURCC('h','p','e','a'));
    T_FEQ(ent->health.max_value, ent->data.UnitBalance->maxHealth + 123.0f, 0.001f);
    T_FEQ(ent->mana.max_value, ent->data.UnitBalance->maxMana + 77.0f, 0.001f);
    T_STREQ(G_UnitAnimationProperties(ent), "");
    T_ASSERT(ent->vertex_color_set);
    T_EQ(ent->vertex_color.r, 224); T_EQ(ent->vertex_color.g, 232);
    T_EQ(ent->vertex_color.b, 255); T_EQ(ent->vertex_color.a, 255);

    level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

static void install_submerge_test_data(slkTestData_t **rows, slkTestData_t **old,
                                      slkTestData_t **ui_rows, slkTestData_t **old_ui) {
    static cstring_t const ability_slk =
        "ID;PWXL;N;EBB;Y2;X4\n"
        "C;Y1;X1;K\"alias\"\n"
        "C;Y1;X2;K\"code\"\n"
        "C;Y1;X3;K\"DataA1\"\n"
        "C;Y1;X4;K\"UnitID1\"\n"
        "C;Y2;X1;K\"Asb1\"\n"
        "C;Y2;X2;K\"Asb1\"\n"
        "C;Y2;X3;K\"hpea\"\n"
        "C;Y2;X4;K\"hfoo\"\n"
        "E\n";
    static cstring_t const ui_slk =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"unitUIID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"modelScale\"\n"
        "C;Y1;X4;K\"scale\"\n"
        "C;Y2;X1;K\"hpea\"\n"
        "C;Y2;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y2;X3;K1\n"
        "C;Y2;X4;K1\n"
        "C;Y3;X1;K\"hfoo\"\n"
        "C;Y3;X2;K\"Units\\Creeps\\Medivh\\Medivh.mdx\"\n"
        "C;Y3;X3;K1\n"
        "C;Y3;X4;K1\n"
        "E\n";
    *rows = parse_slk_string(ability_slk);
    *ui_rows = parse_slk_string(ui_slk);
    T_NOT_NULL(*rows); T_NOT_NULL(*ui_rows);
    *old = G_SetSLKRows("AbilityData", *rows);
    *old_ui = G_SetSLKRows("UnitUI", *ui_rows);
    T_NOT_NULL(*old); T_NOT_NULL(*old_ui);
}

static void restore_submerge_test_data(slkTestData_t *rows, slkTestData_t *old,
                                      slkTestData_t *ui_rows, slkTestData_t *old_ui) {
    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(ui_rows);
    free_slk_rows(rows);
}

TEST(wc3_unit, submerge_rejects_walkable_land_and_morphs_on_deep_water) {
    slkTestData_t *rows, *old, *ui_rows, *old_ui;
    uint8_t land[1] = { CM_PATHING_UNFLOATABLE };
    uint8_t water[1] = { CM_PATHING_UNWALKABLE };
    uint32_t const asb1 = MAKEFOURCC('A','s','b','1');
    edict_t *unit;

    reset_test_entities(); setup_test_world();
    install_submerge_test_data(&rows, &old, &ui_rows, &old_ui);
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.5f, 0.5f);
    unit->svflags |= SVF_MONSTER;
    T_ASSERT(G_ActorAddSkill(unit, asb1));

    CM_SetupTestPathmap(1, 1, land);
    T_ASSERT(!unit_issueimmediateorder(unit, "submerge"));
    T_EQ(unit->class_id, MAKEFOURCC('h','p','e','a'));
    T_ASSERT(!S_UnitIsSubmerged(unit));

    CM_SetupTestPathmap(1, 1, water);
    T_ASSERT(unit_issueimmediateorder(unit, "submerge"));
    morph330_advance(level.pathing_clock.time); /* Original zero-Dur completion runs after public return. */
    T_EQ(unit->class_id, MAKEFOURCC('h','f','o','o'));
    T_ASSERT(S_UnitIsSubmerged(unit));
    T_ASSERT(unit->s.renderfx & RF_HIDDEN);
    T_ASSERT(S_UnitUsesInvisibilityRenderFlag(unit));

    T_ASSERT(unit_issueimmediateorder(unit, "unsubmerge"));
    morph330_advance(level.pathing_clock.time); /* Original zero-Dur completion runs after public return. */
    T_EQ(unit->class_id, MAKEFOURCC('h','p','e','a'));
    T_ASSERT(!S_UnitIsSubmerged(unit));
    T_ASSERT(!(unit->s.renderfx & RF_HIDDEN));

    restore_submerge_test_data(rows, old, ui_rows, old_ui);
}

TEST(wc3_unit, submerge_toggle_state_selects_surface_button) {
    slkTestData_t *rows, *old, *ui_rows, *old_ui;
    uint8_t water[1] = { CM_PATHING_UNWALKABLE };
    uint32_t const asb1 = MAKEFOURCC('A','s','b','1');
    edict_t *unit;
    abilityitem_t item;
    abilityCall_t call;
    gameCommandButton_t button;

    reset_test_entities(); setup_test_world();
    install_submerge_test_data(&rows, &old, &ui_rows, &old_ui);
    CM_SetupTestPathmap(1, 1, water);
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.5f, 0.5f);
    unit->svflags |= SVF_MONSTER;
    T_ASSERT(G_ActorAddSkill(unit, asb1));
    T_ASSERT(G_BuildCommandButton(unit, "Asb1", false, 0, &button));
    T_STREQ(button.tooltip, "Submerge");
    T_STREQ(button.ubertip, "Hide beneath the water.");
    item = S_AbilityItem(asb1);
    call = MAKE(abilityCall_t, .item = &item);
    T_ASSERT(!S_AbilityMessage(unit, A_TOGGLE_ON, &call));
    T_ASSERT(unit_issueimmediateorder(unit, "submerge"));
    morph330_advance(level.pathing_clock.time); /* Original zero-Dur completion runs after public return. */
    T_ASSERT(S_AbilityMessage(unit, A_TOGGLE_ON, &call));
    T_ASSERT(G_BuildCommandButton(unit, "Asb1", false, 0, &button));
    T_STREQ(button.tooltip, "Surface");
    T_STREQ(button.ubertip, "Return to the surface.");
    T_ASSERT(unit_issueimmediateorder(unit, "unsubmerge"));
    morph330_advance(level.pathing_clock.time); /* Original zero-Dur completion runs after public return. */
    T_ASSERT(!S_AbilityMessage(unit, A_TOGGLE_ON, &call));
    restore_submerge_test_data(rows, old, ui_rows, old_ui);
}

TEST(wc3_unit, stoneform_order_requires_authored_ability_ownership) {
    edict_t *ent;
    reset_test_entities(); setup_test_world();
    ent = alloc_test_unit(MAKEFOURCC('u','g','a','r'), 64.0f, 64.0f);
    T_ASSERT(!G_UnitAbilityLevel(ent, MAKEFOURCC('A','s','t','n')));
    T_ASSERT(!unit_issueimmediateorder(ent, "stoneform"));
    T_EQ(ent->class_id, MAKEFOURCC('u','g','a','r'));
}

/* Morphing an existing creep must not treat its current position as a new
 * guard spawn. Exercise the production transform path with authored test data. */
TEST(wc3_unit, creep_transform_preserves_guard_anchor_and_active_phase) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    edict_t *creep = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    creep->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    G_CreepGuardInit(creep);
    creep->s.origin2.x = 512.0f;
    creep->movement.creep_guard_phase = CREEP_GUARD_COMBAT;
    T_ASSERT(G_TransformUnitType(creep, MAKEFOURCC('h','f','o','o')));
    T_ASSERT(creep->movement.creep_guard_enabled);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_COMBAT);
    T_FEQ(creep->movement.creep_guard_origin.x, 64.0f, 0.001f);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, creep_transform_does_not_reenable_disabled_guard) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    edict_t *creep = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    creep->s.player = PLAYER_NEUTRAL_AGGRESSIVE;
    G_CreepGuardInit(creep);
    G_CreepGuardSetEnabled(creep, false);
    creep->s.origin2.x = 512.0f;
    T_ASSERT(G_TransformUnitType(creep, MAKEFOURCC('h','f','o','o')));
    T_ASSERT(!creep->movement.creep_guard_enabled);
    T_EQ(creep->movement.creep_guard_phase, CREEP_GUARD_IDLE);
    T_FEQ(creep->movement.creep_guard_origin.x, 64.0f, 0.001f);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, stoneform_uses_authored_transform_endpoints_in_both_directions) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    ent->svflags |= SVF_MONSTER;
    ent->abilities.added[0] = MAKEFOURCC('A','s','t','n'); ent->abilities.added_count = 1;
    T_ASSERT(G_UnitAbilityLevel(ent, MAKEFOURCC('A','s','t','n')));
    T_NOT_NULL(S_SpellAbilityForCode(MAKEFOURCC('A','s','t','n')));
    T_EQ(G_AbilityData(MAKEFOURCC('A','s','t','n'))->level[0].data[0].id, MAKEFOURCC('h','p','e','a'));
    T_EQ(G_AbilityData(MAKEFOURCC('A','s','t','n'))->level[0].unitID, MAKEFOURCC('h','f','o','o'));
    T_ASSERT(unit_issueimmediateorder(ent, "stoneform"));
    finish_test_morph(ent);
    T_EQ(ent->class_id, MAKEFOURCC('h','f','o','o'));
    /* Stone Form must add its presentation tag even when the authored target
     * profile already contributes another alternate-family requirement. */
    T_ASSERT(strstr(G_UnitAnimationProperties(ent), ",alternate") != NULL);
    T_ASSERT(unit_issueimmediateorder(ent, "unstoneform"));
    finish_test_morph(ent);
    T_EQ(ent->class_id, MAKEFOURCC('h','p','e','a'));
    T_STREQ(G_UnitAnimationProperties(ent), "");
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, stoneform_immediate_direction_rejection_preserves_move_and_queue) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    uint32_t const astn = MAKEFOURCC('A','s','t','n');
    vec2_t destination = {256, 64}, successor = {320, 64};
    edict_t *unit;

    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 64);
    unit->svflags |= SVF_MONSTER;
    T_ASSERT(G_ActorAddSkill(unit, astn));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &destination, false, 0, false));
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &successor, true, 0, false));
    T_ASSERT(!unit_issueimmediateorder(unit, "unstoneform"));
    T_EQ(unit->class_id, MAKEFOURCC('h','p','e','a'));
    T_EQ(unit->current_order_id, 851986);
    T_EQ(G_UnitQueuedOrderCount(unit), 1);
    T_ASSERT(unit_issueimmediateorder(unit, "stoneform"));
    finish_test_morph(unit);
    T_EQ(unit->class_id, MAKEFOURCC('h','f','o','o'));
    T_ASSERT(!unit_issueimmediateorder(unit, "stoneform"));
    T_EQ(unit->class_id, MAKEFOURCC('h','f','o','o'));
    T_ASSERT(unit_issueimmediateorder(unit, "unstoneform"));
    finish_test_morph(unit);
    T_EQ(unit->class_id, MAKEFOURCC('h','p','e','a'));
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, stoneform_command_applies_to_focused_unit_type_subgroup) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    cstring_t command[] = { "button", "Astn" };
    uint32_t const astn = MAKEFOURCC('A','s','t','n');
    edict_t *first, *second, *other_form;

    reset_entities(); setup_test_world();
    g_edicts[0].client = &game.clients[0];
    game.clients[0].ps.number = 0;
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 96.0f, 64.0f);
    other_form = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128.0f, 64.0f);
    edict_t *units[] = { first, second, other_form };
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        units[i]->s.player = 0;
        units[i]->svflags |= SVF_MONSTER;
        G_SelectEntity(game.clients, units[i]);
        units[i]->abilities.added[0] = astn;
        units[i]->abilities.added_count = 1;
    }
    T_ASSERT(G_IsEntitySelected(game.clients, first));
    T_ASSERT(G_FocusSelectedUnit(game.clients, first));
    T_ASSERT(G_IsEntitySelected(game.clients, first));
    T_ASSERT(G_GetMainSelectedUnit(game.clients) == first);
    T_ASSERT(G_UnitCanControl(game.clients, first));
    T_ASSERT(G_ActorHasSkill(first, "Astn"));
    T_ASSERT(G_ActorHasSkill(second, "Astn"));

    G_ClientCommand(g_edicts, 2, command);

    finish_test_morph(first);finish_test_morph(second);
    T_EQ(first->class_id, MAKEFOURCC('h','f','o','o'));
    T_EQ(second->class_id, MAKEFOURCC('h','f','o','o'));
    /* This selected unit began in the other type subgroup and must not receive
     * the focused Gargoyle subgroup command. */
    T_EQ(other_form->class_id, MAKEFOURCC('h','f','o','o'));
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, unravenform_accepts_preplaced_alternate_form) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    edict_t *ent;

    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    mapInfo_t const *saved_map=install_raven_flight_endpoint();
    ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    G_ResetUnitAnimationProperties(ent);
    T_STREQ(G_UnitAnimationProperties(ent), "alternateex");

    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','f','o','o'));
    finish_test_morph(ent);
    T_EQ(ent->class_id, MAKEFOURCC('h','p','e','a'));
    T_STREQ(G_UnitAnimationProperties(ent), "");

    level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, unravenform_snaps_new_animation_frame_while_unit_is_paused) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    edict_t *ent;

    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    mapInfo_t const *saved_map=install_raven_flight_endpoint();
    ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    SP_SpawnUnit(ent);
    unit_stand(ent);
    T_NOT_NULL(ent->animation);
    ent->paused = true;
    ent->s.frame = 0x7fffffffu;

    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    T_EQ(ent->class_id, MAKEFOURCC('h','f','o','o'));

    T_NOT_NULL(ent->animation);
    T_ASSERT(G_AnimationHasPrimary(ent->animation, "morph"));
    if(ent->animation)T_EQ(ent->s.frame, ent->animation->interval[0]);
    finish_test_morph(ent);
    T_EQ(ent->class_id, MAKEFOURCC('h','p','e','a'));
    T_STREQ(G_UnitAnimationProperties(ent), "");

    level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

/* Both order entry points must resolve the registered handler, including preplaced forms. */
TEST(wc3_unit, raven_ability_dispatch_and_toggle) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    ability_t const *ability = FindAbilityByOrder("ravenform");
    abilityitem_t item;
    abilityCall_t call;
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    mapInfo_t const *saved_map=install_raven_flight_endpoint();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    item = MAKE(abilityitem_t, .ability = ability);
    call = MAKE(abilityCall_t, .item = &item);
    T_NOT_NULL(ability);
    T_EQ(ability->proc, FindAbilityByOrder("unravenform")->proc);
    T_EQ(ability->proc, FindAbilityByClassname("Amrf")->proc);
    T_EQ(ability->proc, FindAbilityByClassname("Arav")->proc);
    T_ASSERT(ability->flags & AB_COMMAND);
    T_NULL(FindAbilityByOrder("unrecognized"));
    T_NULL(FindAbilityByOrder(NULL));
    T_ASSERT(!S_AbilityMessage(ent, A_TOGGLE_ON, &call));
    T_ASSERT(unit_issueimmediateorder(ent, "ravenform"));
    T_ASSERT(S_AbilityMessage(ent, A_TOGGLE_ON, &call));
    T_ASSERT(ent->currentmove->proc == ability->proc);
    T_NULL(ent->currentmove->think); /* Morph cannot acquire enemies and replace itself with Attack. */
    T_ASSERT(!unit_issueimmediateorder(ent, "ravenform")); /* Active owner rejects recast. */
    finish_test_morph(ent);
    T_ASSERT(unit_issueimmediateorder(ent, "ravenform")); /* Completed form is idempotent. */
    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    finish_test_morph(ent);
    T_ASSERT(!S_AbilityMessage(ent, A_TOGGLE_ON, &call));
    T_EQ(ent->raven->rise_state, RAVEN_RISE_NONE);
    ent->class_id = MAKEFOURCC('o','g','r','u');
    T_ASSERT(!unit_issueimmediateorder(ent, "ravenform"));
    level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

/* Completing the ability's morph starts takeoff; an unrelated Move order must not stop its timer. */
TEST(wc3_unit, raven_morph_completion_and_takeoff_survive_move_order) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    vec2_t point = {128, 64};
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    mapInfo_t const *saved_map=install_raven_flight_endpoint();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    T_ASSERT(unit_issueimmediateorder(ent, "ravenform"));
    if (!ent->raven) ent->raven = G_AllocRaven();
    assert(ent->raven);
    ent->raven->fly_height = 100;
    ent->raven->rise_duration = 2;
    T_ASSERT(G_AnimationHasPrimary(ent->animation, "morph"));
    if(ent->animation)ent->s.frame = ent->animation->interval[1] - 1;
    level.time = 1000;
    monster_think(ent);
    T_EQ(ent->raven->rise_state, RAVEN_RISE_AFTER_MORPH);
    finish_test_morph(ent);
    T_EQ(ent->raven->rise_state, RAVEN_RISE_ACTIVE);
    T_FEQ(ent->unitinfo.FlyHeight, 0, 0.001f);
    T_ASSERT(unit_issueorder(ent, "move", &point));
    ent->paused = true;
    level.time = 2000;
    monster_think(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 50, 0.001f);
    T_FEQ(ent->s.origin2.x, 64, 0.001f); /* Pause stops walking, but preserves the PR's independent ascent. */
    T_ASSERT(unit_issueimmediateorder(ent, "unravenform"));
    finish_test_morph(ent);
    level.time = 3000;
    monster_think(ent);
    T_EQ(ent->raven->rise_state, RAVEN_RISE_NONE);
    T_FEQ(ent->unitinfo.FlyHeight, 0, 0.001f);
    level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

/* Prologue01 issues Move after 0.5 seconds, before Medivh's forward Morph clip ends. */
TEST(wc3_unit, raven_takeoff_survives_interrupted_morph) {
    slkTestData_t *ability_rows, *old_ability, *ui_rows, *old_ui, *profile_rows, *old_profile;
    vec2_t point = {128, 64};
    reset_test_entities(); setup_test_world();
    install_raven_form_test_data(&ability_rows, &old_ability, &ui_rows, &old_ui, &profile_rows, &old_profile);
    mapInfo_t const *saved_map=install_raven_flight_endpoint();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    T_ASSERT(unit_issueimmediateorder(ent, "ravenform"));
    if (!ent->raven) ent->raven = G_AllocRaven();
    assert(ent->raven);
    ent->raven->fly_height = 100;
    ent->raven->rise_duration = 2;
    T_EQ(ent->raven->rise_state, RAVEN_RISE_AFTER_MORPH);
    T_ASSERT(unit_issueorder(ent, "move", &point));
    T_EQ(ent->current_order_id,852155);
    T_EQ(ent->raven->rise_state,RAVEN_RISE_AFTER_MORPH);
    level.time = 1000;
    finish_test_morph(ent);
    T_EQ(ent->current_order_id,851986);
    umove_t const *walk=ent->currentmove;
    S_RunAbilityUpdates(ent);
    T_EQ(ent->raven->rise_state, RAVEN_RISE_ACTIVE);
    T_ASSERT(ent->currentmove == walk);
    level.time = 2000;
    S_RunAbilityUpdates(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 50, 0.001f);
    T_ASSERT(ent->currentmove == walk);
    level.mapinfo=saved_map;G_SetMapUnitOverrides(saved_map);
    restore_raven_form_test_data(ability_rows, old_ability, ui_rows, old_ui, profile_rows, old_profile);
}

TEST(wc3_unit, ability_updates_leave_ordinary_units_unchanged) {
    reset_test_entities(); setup_test_world();
    edict_t *ent = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    ent->unitinfo.FlyHeight = 37;
    M_SetMove(ent,NULL);
    monster_think(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 37, 0.001f);
    T_NULL(ent->raven);
}

TEST(wc3_unit, ravenForm_takeoff_interpolates_from_ground_to_authored_height) {
    edict_t *ent;

    reset_test_entities(); setup_test_world();
    ent = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    if (!ent->raven) ent->raven = G_AllocRaven();
    assert(ent->raven);
    ent->raven->fly_height = 100.0f;
    ent->raven->rise_start = 1000;
    ent->raven->rise_duration = 2.0f;
    ent->raven->rise_state = RAVEN_RISE_ACTIVE;
    level.time = 2000;
    monster_think(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 50.0f, 0.001f);
    T_EQ(ent->raven->rise_state, RAVEN_RISE_ACTIVE);
    level.time = 3000;
    monster_think(ent);
    T_FEQ(ent->unitinfo.FlyHeight, 100.0f, 0.001f);
    T_EQ(ent->raven->rise_state, RAVEN_RISE_NONE);
}

TEST(wc3_unit, issueimmediateorder_autoharvestlumber_uses_nearest_live_tree) {
    reset_test_entities();
    edict_t *worker = make_unit(0, 0);
    edict_t *far_tree = unit_make_harvest_tree(300.0f, 0.0f);
    edict_t *dead_tree = unit_make_harvest_tree(25.0f, 0.0f);
    edict_t *near_tree = unit_make_harvest_tree(100.0f, 0.0f);
    (void)far_tree;
    dead_tree->health.value = 0.0f;

    T_ASSERT(unit_issueimmediateorder(worker, "autoharvestlumber"));
    T_ASSERT(worker->goalentity == near_tree);
    T_ASSERT(worker->secondarygoal == near_tree);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, issueimmediateorder_autoharvestgold_uses_nearest_live_mine) {
    reset_test_entities();
    edict_t *worker = make_unit(0, 0);
    edict_t *far_mine = unit_make_harvest_goldmine(300.0f, 0.0f);
    edict_t *empty_mine = unit_make_harvest_goldmine(25.0f, 0.0f);
    edict_t *near_mine = unit_make_harvest_goldmine(100.0f, 0.0f);
    (void)far_mine;
    empty_mine->resources = 0;

    T_ASSERT(unit_issueimmediateorder(worker, "autoharvestgold"));
    T_ASSERT(worker->goalentity == near_mine);
    T_ASSERT(worker->secondarygoal == near_mine);
    T_STREQ(worker->currentmove->animation, "walk");
}

TEST(wc3_unit, issueimmediateorder_autoharvest_requires_resource_target) {
    reset_test_entities();
    edict_t *worker = make_unit(0, 0);

    T_ASSERT(!unit_issueimmediateorder(worker, "autoharvestlumber"));
    T_ASSERT(!unit_issueimmediateorder(worker, "autoharvestgold"));
    T_STREQ(worker->currentmove->animation, "stand");
}

TEST(wc3_unit, issueimmediateorder_unknown_returns_false) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);
    bool result = unit_issueimmediateorder(ent, "patrol");
    T_ASSERT(!result);
}

TEST(wc3_unit, issueimmediateorder_null_inputs_return_false) {
    reset_test_entities();
    edict_t *ent = make_unit(0, 0);

    T_ASSERT(!unit_issueimmediateorder(NULL, "stop"));
    T_ASSERT(!unit_issueimmediateorder(ent, NULL));
}

/* -----------------------------------------------------------------------
 * Inventory tests
 * --------------------------------------------------------------------- */

TEST(wc3_unit, additemtoslot_fills_empty_slot) {
    reset_test_entities();
    edict_t *ent  = make_inventory_unit(0, 0);
    edict_t *item = make_world_item(MAKEFOURCC('r','a','t','f'));
    bool ok = unit_additemtoslot(ent, item, 0);
    T_ASSERT(ok);
    T_ASSERT(ent->inventory[0] == item);
}

TEST(wc3_unit, additemtoslot_rejects_occupied_slot) {
    reset_test_entities();
    edict_t *ent   = make_inventory_unit(0, 0);
    edict_t *item1 = make_world_item(MAKEFOURCC('r','a','t','f'));
    edict_t *item2 = make_world_item(MAKEFOURCC('r','a','t','f'));
    unit_additemtoslot(ent, item1, 0);
    bool ok = unit_additemtoslot(ent, item2, 0);
    T_ASSERT(!ok);
}

TEST(wc3_unit, additem_fills_first_free_slot) {
    reset_test_entities();
    edict_t *ent   = make_inventory_unit(0, 0);
    edict_t *item1 = make_world_item(MAKEFOURCC('r','a','t','f'));
    edict_t *item2 = make_world_item(MAKEFOURCC('r','d','e','2'));
    unit_additemtoslot(ent, item1, 0);
    bool ok = unit_additem(ent, item2);
    T_ASSERT(ok);
    T_ASSERT(ent->inventory[1] == item2);
}

TEST(wc3_unit, additem_fails_when_inventory_full) {
    reset_test_entities();
    edict_t *ent = make_inventory_unit(0, 0);
    for (int i = 0; i < MAX_INVENTORY; i++) {
        edict_t *item = make_world_item(MAKEFOURCC('r','a','t','f'));
        unit_additemtoslot(ent, item, i);
    }
    edict_t *extra = make_world_item(MAKEFOURCC('r','d','e','2'));
    bool ok = unit_additem(ent, extra);
    T_ASSERT(!ok);
}


TEST(wc3_unit, different_units_have_independent_response_gates) {
    edict_t *a = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *b = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    a->s.player = b->s.player = 0;
    G_ResetSelectionSoundState();
    T_ASSERT(G_QueueUnitResponseSound(a, 11));
    T_ASSERT(G_QueueUnitResponseSound(b, 12));
    T_ASSERT(!G_QueueUnitResponseSound(a, 13));
    T_EQ(b->sound.pending, 12);
}


TEST(wc3_unit, sound_profiles_share_defaults_keep_pending_local_and_restore_logical_values) {
    reset_entities(); setup_test_world();
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    uint32_t a = first->s.number, b = second->s.number;
    unitSoundProfile_t value = {.select = {11,12}, .num_select = 2, .yes = {17}, .num_yes = 1,
        .ready = {21}, .num_ready = 1, .chop = {25,26,27}, .num_chop = 3, .attack = 31, .death = 37};
    G_SetUnitSoundProfile(first, &value); G_SetUnitSoundProfile(second, &value);
    T_ASSERT(first->sound_profile == second->sound_profile);
    unitSoundProfile_t const *shared = second->sound_profile;
    unitSoundProfile_t padded = value;
    for (size_t i = offsetof(unitSoundProfile_t, num_chop) + sizeof(padded.num_chop);
         i < offsetof(unitSoundProfile_t, attack); i++) ((uint8_t *)&padded)[i] = 0xa5;
    G_SetUnitSoundProfile(second, &padded);
    T_ASSERT(second->sound_profile == shared);
    first->sound.pending = 11; second->sound.owner_pending = 21;
    first->sound.world_pending = 37; first->sound.world_pending_event = EV_DEATH;
    value.select[0] = 43;
    G_SetUnitSoundProfile(first, &value);
    T_ASSERT(first->sound_profile != shared);
    T_EQ(G_UnitSoundProfile(first)->select[0], 43);
    T_EQ(G_UnitSoundProfile(second)->select[0], 11);
    T_EQ(first->sound.pending, 11); T_EQ(second->sound.pending, 0);
    G_ResetUnitResources();
    T_ASSERT(second->sound_profile == shared);
    T_EQ(G_UnitSoundProfile(first)->death, 37);
    cstring_t file = Test_TempPath("wc3-sound-profile-save.bin");
    T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file));
    first = g_edicts + a; second = g_edicts + b;
    T_EQ(G_UnitSoundProfile(first)->select[0], 43);
    T_EQ(G_UnitSoundProfile(second)->select[0], 11);
    T_EQ(G_UnitSoundProfile(first)->num_chop, 3);
    T_EQ(G_UnitSoundProfile(first)->chop[2], 27);
    T_EQ(G_UnitSoundProfile(first)->attack, 31);
    /* Selection acknowledgement requests are intentionally retired on load. */
    T_EQ(first->sound.pending, 0); T_EQ(second->sound.owner_pending, 21);
    T_EQ(first->sound.world_pending, 37); T_EQ(first->sound.world_pending_event, EV_DEATH);
    unitSoundProfile_t invalid = *G_UnitSoundProfile(first);
    invalid.num_yes = MAX_UNIT_SELECT_SOUNDS + 1;
    G_SetUnitSoundProfile(first, &invalid);
    T_ASSERT(!WriteGame(file));
    G_SetUnitSoundProfile(first, &value);
    FILE *stream = tmpfile();
    T_NOT_NULL(stream);
    if (stream) {
        T_EQ(fwrite(&invalid, sizeof(invalid), 1, stream), 1);
        rewind(stream);
        T_ASSERT(!ReadUnitSoundProfile(stream, first));
        T_EQ(G_UnitSoundProfile(first)->num_yes, 1);
        fclose(stream);
    }
    remove(file); reset_entities(); setup_test_world();
}
TEST(wc3_unit, animation_text_serializes_logical_values_without_sharing_mutations) {
    reset_entities(); setup_test_world();
    edict_t *first = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *second = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    uint32_t a = first->s.number, b = second->s.number;
    char long_text[256]; memset(long_text, 'a', sizeof(long_text)); long_text[255] = 0;
    G_StoreUnitAnimationRequest(first, long_text);
    G_StoreUnitAnimationProperties(first, long_text);
    G_StoreUnitAnimationRequest(second, long_text);
    G_StoreUnitAnimationProperties(second, long_text);
    T_EQ(strlen(G_UnitAnimationRequest(first)), WC3_ANIMATION_REQUEST_SIZE - 1);
    T_EQ(strlen(G_UnitAnimationProperties(first)), WC3_ANIMATION_PROPERTIES_SIZE - 1);
    T_ASSERT(first->animation_request == second->animation_request);
    T_ASSERT(first->animation_props == second->animation_props);
    cstring_t file = Test_TempPath("wc3-animation-text-save.bin");
    T_ASSERT(WriteGame(file)); T_ASSERT(ReadGame(file));
    first = g_edicts + a; second = g_edicts + b;
    T_ASSERT(first->animation_request == second->animation_request);
    T_ASSERT(first->animation_props == second->animation_props);
    T_EQ(strlen(G_UnitAnimationRequest(first)), WC3_ANIMATION_REQUEST_SIZE - 1);
    T_EQ(strlen(G_UnitAnimationProperties(first)), WC3_ANIMATION_PROPERTIES_SIZE - 1);
    G_StoreUnitAnimationRequest(first, "stand");
    G_StoreUnitAnimationProperties(first, "work");
    T_EQ(strlen(G_UnitAnimationRequest(second)), WC3_ANIMATION_REQUEST_SIZE - 1);
    T_EQ(strlen(G_UnitAnimationProperties(second)), WC3_ANIMATION_PROPERTIES_SIZE - 1);
    FOR_LOOP(invalid, 2) {
        FILE *stream = tmpfile(); T_NOT_NULL(stream);
        if (!stream) continue;
        char request[WC3_ANIMATION_REQUEST_SIZE], properties[WC3_ANIMATION_PROPERTIES_SIZE];
        memset(request, invalid ? 0 : 'x', sizeof(request));
        memset(properties, invalid ? 'x' : 0, sizeof(properties));
        T_EQ(fwrite(request, sizeof(request), 1, stream), 1);
        T_EQ(fwrite(properties, sizeof(properties), 1, stream), 1);
        rewind(stream);
        T_ASSERT(!ReadUnitAnimationText(stream, first));
        T_STREQ(G_UnitAnimationRequest(first), "stand");
        T_STREQ(G_UnitAnimationProperties(first), "work");
        fclose(stream);
    }
    remove(file); reset_entities(); setup_test_world();
}
TEST(wc3_unit, status_modifiers_stack_by_family_and_retire_with_slot) {
    unitStatusStorage_t storage = { 0 };
    edict_t unit = { .abilstatus = storage.slots };
    uint32_t old_time = level.time;
    heroabilitystatus_t *a = &unit.abilstatus[0];
    heroabilitystatus_t *b = &unit.abilstatus[1];
    heroabilitystatus_t *c = &unit.abilstatus[2];
    wc3_status_modifier_t weak = {
        .type = WC3_STATUS_MOD_MOVE_SPEED_PERCENT, .family = 1,
        .policy = WC3_STATUS_MOD_STRONGEST_NEGATIVE, .value = -0.2f
    };
    wc3_status_modifier_t strong = weak;
    wc3_status_modifier_t additive = {
        .type = WC3_STATUS_MOD_MOVE_SPEED_PERCENT, .policy = WC3_STATUS_MOD_ADD,
        .value = 0.1f
    };
    a->level = b->level = c->level = 1;
    strong.value = -0.5f;
    T_ASSERT(unit_status_setmodifier(a, 0, &weak));
    T_ASSERT(unit_status_setmodifier(b, 0, &strong));
    T_ASSERT(unit_status_setmodifier(c, 0, &additive));
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.4f, 0.001f);
    b->level = 0; /* simulate expired/removed owner */
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.1f, 0.001f);
    b->level = 1;
    b->timestamp = 1; /* expired strongest source falls back immediately */
    level.time = 2;
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.1f, 0.001f);
    level.time = old_time;
    T_ASSERT(!unit_status_setmodifier(a, WC3_STATUS_MAX_MODIFIERS, &weak));
}

/* Fast-path coverage: a unit with no descriptors and an additive-only
 * unit both bypass the strongest-wins table without changing arithmetic. */
TEST(wc3_unit, status_modifier_fast_path_preserves_additive_and_strongest_results) {
    unitStatusStorage_t storage = { 0 };
    edict_t unit = { .abilstatus = storage.slots };
    wc3_status_modifier_t add = {
        .type = WC3_STATUS_MOD_MOVE_SPEED_PERCENT, .policy = WC3_STATUS_MOD_ADD,
        .value = 0.125f
    };
    wc3_status_modifier_t weak = {
        .type = WC3_STATUS_MOD_MOVE_SPEED_PERCENT, .family = 77,
        .policy = WC3_STATUS_MOD_STRONGEST_NEGATIVE, .value = -0.20f
    };
    wc3_status_modifier_t strong = weak;
    uint32_t old_time = level.time;
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), 0.0f, 0.001f);
    unit.abilstatus[0].level = 1;
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), 0.0f, 0.001f);
    T_ASSERT(unit_status_setmodifier(&unit.abilstatus[0], 0, &add));
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), 0.125f, 0.001f);
    unit.abilstatus[1].level = unit.abilstatus[2].level = 1;
    strong.value = -0.45f;
    T_ASSERT(unit_status_setmodifier(&unit.abilstatus[1], 0, &weak));
    T_ASSERT(unit_status_setmodifier(&unit.abilstatus[2], 0, &strong));
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.325f, 0.001f);
    unit.abilstatus[2].timestamp = 1;
    level.time = 2;
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), -0.075f, 0.001f);
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_ARMOR_FLAT), 0.0f, 0.001f);
    level.time = old_time;
}

TEST(wc3_unit, status_modifiers_ignore_other_types_and_support_positive_families) {
    unitStatusStorage_t storage = { 0 };
    edict_t unit = { .abilstatus = storage.slots };
    wc3_status_modifier_t first = {
        .type = WC3_STATUS_MOD_ARMOR_FLAT, .family = 19,
        .policy = WC3_STATUS_MOD_STRONGEST_POSITIVE, .value = 2.0f
    };
    wc3_status_modifier_t second = first;
    unit.abilstatus[0].level = unit.abilstatus[1].level = 1;
    second.value = 4.0f;
    T_ASSERT(unit_status_setmodifier(&unit.abilstatus[0], 0, &first));
    T_ASSERT(unit_status_setmodifier(&unit.abilstatus[1], 0, &second));
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_ARMOR_FLAT), 4.0f, 0.001f);
    T_FEQ(unit_status_modifier_total(&unit, WC3_STATUS_MOD_MOVE_SPEED_PERCENT), 0.0f, 0.001f);
}

TEST(wc3_unit, status_periodic_scheduler_advances_before_callback_and_stops_at_expiry) {
    heroabilitystatus_t slot = {0};
    slot.level = 1;
    slot.timestamp = 5500;
    slot.next_tick = 1000;
    T_ASSERT(!unit_status_take_due_tick(&slot, 1000, 999));
    T_ASSERT(unit_status_take_due_tick(&slot, 1000, 1000));
    T_EQ(slot.next_tick, 2000);
    /* Recursive status updates during damage cannot consume the same pulse. */
    T_ASSERT(!unit_status_take_due_tick(&slot, 1000, 1000));
    T_ASSERT(unit_status_take_due_tick(&slot, 1000, 4500));
    T_EQ(slot.next_tick, 3000);
    T_ASSERT(unit_status_take_due_tick(&slot, 1000, 4500));
    T_ASSERT(unit_status_take_due_tick(&slot, 1000, 4500));
    T_ASSERT(!unit_status_take_due_tick(&slot, 1000, 4500));
    T_EQ(slot.next_tick, 5000);
    T_ASSERT(!unit_status_take_due_tick(&slot, 1000, 6000));
}

TEST(wc3_unit, status_periodic_scheduler_rejects_unscheduled_expired_or_invalid_ticks) {
    heroabilitystatus_t slot = {0};
    slot.level = 1;
    slot.timestamp = 3000;
    T_ASSERT(!unit_status_take_due_tick(&slot, 1000, 2000));
    slot.next_tick = 3000;
    T_ASSERT(!unit_status_take_due_tick(&slot, 1000, 3000));
    slot.next_tick = 2000;
    T_ASSERT(!unit_status_take_due_tick(&slot, 0, 2000));
    slot.level = 0;
    T_ASSERT(!unit_status_take_due_tick(&slot, 1000, 2000));
    T_ASSERT(!unit_status_take_due_tick(NULL, 1000, 2000));
}

/* Ethereal357 replays Audit333's public forward/reverse owner lifetime with
 * non-stock endpoints/duration in both real AbilityData column schemas. */
TEST(wc3_unit, ethereal357_forms_keep_counted_state_through_transition_save_and_inverse) {
    for(unsigned family=0;family<2;family++)for(unsigned schema=0;schema<2;schema++)for(unsigned action=0;action<5;action++) {
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        reset_test_entities();setup_test_world();
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char data[1100];cstring_t parent=family ? "Aetf" : "Acpf",alias=family ? "Ae57" : "Ac57";
        cstring_t orders[2]={family ? "etherealform" : "corporealform",family ? "unetherealform" : "uncorporealform"};
        snprintf(data,sizeof(data),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"%s\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K%s\nC;X1;Y3;K\"Aetl\"\nC;X2;K\"Aetl\"\nE\n",
            schema ? "Data11" : "DataA1",alias,parent,action==4 ? "0.625" : "0");
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        uint32_t code=FS_SLKKey(alias),base=FS_SLKKey("hpea"),alt=FS_SLKKey("hfoo");
        edict_t *unit=alloc_test_unit(base,64,64);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        T_ASSERT(G_ActorAddSkill(unit,code));
        edict_t *enemy=alloc_test_unit(alt,96,64);enemy->s.player=1;uint32_t enemy_slot=enemy-g_edicts;
        T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),!family);
        if(action==0) {
            T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Aetl")));
            T_ASSERT(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL));
            T_ASSERT(G_ActorRemoveSkill(unit,FS_SLKKey("Aetl")));
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),!family);
        }
        for(unsigned reverse=0;reverse<2;reverse++) {
            float start=1+reverse;level.pathing_clock=(wc3Clock_t){.time=start,.span=300};
            bool source_ethereal=family ? reverse : !reverse;
            T_ASSERT(unit_issueimmediateorder(unit,orders[reverse]));
            T_EQ(unit->current_order_id,G_OrderId(orders[reverse]));
            T_EQ(unit->movement.repulse.disable_depth,1);
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),source_ethereal);
            T_EQ(S_AttackHitAllowed(enemy,unit,unit),!source_ethereal);
            T_EQ(S_AttackHitAllowed(unit,enemy,enemy),!source_ethereal);
            if(action==1) {T_ASSERT(unit_issueimmediateorder(unit,"stop"));T_EQ(unit->current_order_id,G_OrderId(orders[reverse]));}
            if(action==3) {S_SetUnitPaused(unit,true);T_ASSERT(!S_UnitExecutionPaused(unit));}
            if(action==2) {uint32_t slot=unit-g_edicts;cstring_t path=Test_TempPath("ethereal357-transition.bin");
                T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;enemy=g_edicts+enemy_slot;}
            morph330_advance(start+.374f);
            T_EQ(unit->class_id,reverse ? alt : base);
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),source_ethereal);
            T_EQ(S_AttackHitAllowed(enemy,unit,unit),!source_ethereal);
            T_EQ(S_AttackHitAllowed(unit,enemy,enemy),!source_ethereal);
            morph330_advance(start+.375f);
            T_EQ(unit->class_id,reverse ? base : alt);
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),!source_ethereal);
            T_EQ(S_AttackHitAllowed(enemy,unit,unit),source_ethereal);
            T_EQ(S_AttackHitAllowed(unit,enemy,enemy),source_ethereal);
            T_EQ(unit->movement.repulse.disable_depth,action==3 ? 1 : 0);
            if(action==3) {T_ASSERT(S_UnitExecutionPaused(unit));S_SetUnitPaused(unit,false);
                morph330_advance(start+.425f);S_RunMoveTimers();T_EQ(unit->movement.repulse.disable_depth,0);}
            if(action==4 && !reverse) {
                bool pending=unit->morph && unit->morph->expiration.active;
                T_ASSERT(pending);if(pending)T_FEQ(unit->morph->expiration.deadline.time,2.0f,.000001f);
                morph330_advance(1.999f);T_EQ(unit->class_id,alt);
                uint32_t slot=unit-g_edicts;cstring_t path=Test_TempPath("ethereal357-lifetime.bin");
                T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;enemy=g_edicts+enemy_slot;
                morph330_advance(2.0f);T_EQ(unit->movement.repulse.disable_depth,1);
                T_EQ(unit->class_id,alt);T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),family!=0);
                morph330_advance(2.374f);T_EQ(unit->class_id,alt);
                morph330_advance(2.375f);T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,0);
                T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),!family);
                break;
            }
            if(action==2) {uint32_t slot=unit-g_edicts;cstring_t path=Test_TempPath("ethereal357-completed.bin");
                T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;enemy=g_edicts+enemy_slot;
                T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),!source_ethereal);
            T_EQ(S_AttackHitAllowed(enemy,unit,unit),source_ethereal);
            T_EQ(S_AttackHitAllowed(unit,enemy,enemy),source_ethereal);}
        }
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_ASSERT(!unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL));
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}


/* Submerge360: original Submerge inherits Morph's counted transition but
 * restores visibility/category at reverse admission, before the type rebind. */
TEST(wc3_unit, submerge360_authored_transition_keeps_mask_and_saved_inverse) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned action=0;action<4;action++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        mapInfo_t const *old_map=level.mapinfo;mapInfo_t info=*old_map;
        int enabled=2,selector=17;
        unitModification_t mods[]={
            {.modID=MAKEFOURCC('u','r','p','o'),.type=mod_int,.data=&enabled},
            {.modID=MAKEFOURCC('u','r','p','p'),.type=mod_int,.data=&selector},
            {.modID=MAKEFOURCC('u','r','p','g'),.type=mod_int,.data=&selector},
            {.modID=MAKEFOURCC('u','r','p','r'),.type=mod_int,.data=&selector}};
        unitData_t types[]={
            {.originalUnitID=FS_SLKKey("hpea"),.newUnitID=FS_SLKKey("hS60"),.numbeOfModifications=4,.modifications=mods},
            {.originalUnitID=FS_SLKKey("hfoo"),.newUnitID=FS_SLKKey("hT60"),.numbeOfModifications=4,.modifications=mods}};
        info.num_userCreatedUnits=2;info.userCreatedUnits=types;level.mapinfo=&info;G_SetMapUnitOverrides(&info);
        char data[1000];snprintf(data,sizeof(data),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Au60\"\nC;X2;K\"ANsu\"\nC;X3;K\"hS60\"\nC;X4;K\"hT60\"\nC;X5;K0.375\nC;X6;K0\nE\n",
            schema ? "Data11" : "DataA1");
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        uint8_t water[]={CM_PATHING_UNWALKABLE};CM_SetupTestPathmap(1,1,water);
        edict_t *unit=alloc_test_unit(types[0].newUnitID,.5f,.5f);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        S_UnitAbilityEvent(unit,A_UNIT_INIT);T_ASSERT(unit->movement.repulse.active);
        T_EQ(unit->movement.repulse.state.packed,0x10110000u);T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Au60")));
        for(unsigned reverse=0;reverse<2;reverse++) {
            float start=1+reverse;level.pathing_clock=(wc3Clock_t){.time=start,.span=300};
            cstring_t order=reverse ? "unsubmerge" : "submerge";
            T_ASSERT(unit_issueimmediateorder(unit,order));
            T_EQ(unit->current_order_id,G_OrderId(order));T_EQ(unit->class_id,types[reverse].newUnitID);
            T_EQ(unit->movement.repulse.disable_depth,1);T_ASSERT(!unit->movement.repulse.active);
            T_ASSERT(!S_UnitIsSubmerged(unit));T_ASSERT(!(unit->s.renderfx&RF_HIDDEN));
            T_EQ(S_UnitMoveCategory(unit),0xca);
            if(action==1) {T_ASSERT(unit_issueimmediateorder(unit,"stop"));T_EQ(unit->current_order_id,G_OrderId(order));}
            if(action==3) {S_SetUnitPaused(unit,true);T_ASSERT(!S_UnitExecutionPaused(unit));}
            if(action==2) {uint32_t slot=unit-g_edicts;cstring_t path=Test_TempPath("submerge360-transition.bin");
                T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;}
            morph330_advance(start+.374f);T_EQ(unit->class_id,types[reverse].newUnitID);
            T_EQ(unit->movement.repulse.disable_depth,1);T_ASSERT(!S_UnitIsSubmerged(unit));
            morph330_advance(start+.375f);T_EQ(unit->class_id,types[!reverse].newUnitID);
            T_EQ(S_UnitIsSubmerged(unit),!reverse);T_EQ((unit->s.renderfx&RF_HIDDEN)!=0,!reverse);
            T_EQ(S_UnitMoveCategory(unit),reverse ? 0xca : 8);
            T_EQ(unit->movement.repulse.disable_depth,action==3 ? 1 : 0);
            if(action==3) {T_ASSERT(S_UnitExecutionPaused(unit));S_SetUnitPaused(unit,false);
                morph330_advance(start+.425f);S_RunMoveTimers();T_EQ(unit->movement.repulse.disable_depth,0);}
            T_ASSERT(unit->movement.repulse.active);T_EQ(unit->movement.repulse.state.packed,0x10110000u);
            if(action==2) {uint32_t slot=unit-g_edicts;cstring_t path=Test_TempPath("submerge360-completed.bin");
                T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;
                T_EQ(S_UnitMoveCategory(unit),reverse ? 0xca : 8);T_EQ(S_UnitIsSubmerged(unit),!reverse);}
        }
        reset_entities();G_SetMapUnitOverrides(old_map);level.mapinfo=old_map;
        restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}


TEST(wc3_unit, submerge360_zero_duration_rebinds_after_public_return_at_same_clock) {
    reset_test_entities();setup_test_world();
    slkTestData_t *rows,*old,*ui,*old_ui;install_submerge_test_data(&rows,&old,&ui,&old_ui);
    uint8_t water[]={CM_PATHING_UNWALKABLE};CM_SetupTestPathmap(1,1,water);
    edict_t *unit=alloc_test_unit(FS_SLKKey("hpea"),.5f,.5f);unit->stand=unit_stand;unit->svflags|=SVF_MONSTER;
    T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Asb1")));
    level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
    T_ASSERT(unit_issueimmediateorder(unit,"submerge"));
    T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->current_order_id,0);
    T_EQ(unit->movement.repulse.disable_depth,1);T_ASSERT(!S_UnitIsSubmerged(unit));
    abilityTimerRequest_t request;bool pending=S_NextAbilityPrimaryTimer(&request);T_ASSERT(pending);
    if(pending)T_EQ(wc3_float_bits(request.deadline.time),wc3_float_bits(1));
    morph330_advance(1);T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_ASSERT(S_UnitIsSubmerged(unit));
    T_EQ(unit->movement.repulse.disable_depth,0);T_EQ(S_UnitMoveCategory(unit),8);
    T_ASSERT(unit_issueimmediateorder(unit,"unsubmerge"));
    T_EQ(unit->class_id,FS_SLKKey("hfoo"));T_EQ(unit->current_order_id,0);
    T_EQ(unit->movement.repulse.disable_depth,1);T_ASSERT(!S_UnitIsSubmerged(unit));
    T_EQ(S_UnitMoveCategory(unit),0xca);
    morph330_advance(1);T_EQ(unit->class_id,FS_SLKKey("hpea"));T_EQ(unit->movement.repulse.disable_depth,0);
    reset_entities();restore_submerge_test_data(rows,old,ui,old_ui);
}

/* Alternate364:55d380 initializes an existing alternate through an immediate
 * subclass inverse, with no transition contribution or d01c4 request. */
TEST(wc3_unit, alternate364_attachment_publishes_form_before_first_order_and_cold_save) {
    for(unsigned family=0;family<3;family++)for(unsigned schema=0;schema<2;schema++)for(unsigned saved=0;saved<3;saved++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        cstring_t parent=family==0 ? "Acpf" : family==1 ? "Aetf" : "ANsu";
        cstring_t reverse=family==0 ? "uncorporealform" : family==1 ? "unetherealform" : "unsubmerge";
        char data[1000];snprintf(data,sizeof(data),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Aa64\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K%s\nE\n",
            schema ? "Data11" : "DataA1",parent,saved==2 ? "0.625" : "0");
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        uint8_t water[]={CM_PATHING_UNWALKABLE};CM_SetupTestPathmap(1,1,water);
        uint32_t code=FS_SLKKey("Aa64"),base=FS_SLKKey("hpea"),alt=FS_SLKKey("hfoo");
        edict_t *unit=alloc_test_unit(alt,.5f,.5f);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        level.pathing_clock=(wc3Clock_t){.time=0,.span=300};
        T_ASSERT(G_ActorAddSkill(unit,code));T_EQ(unit->class_id,alt);
        T_EQ(unit->current_order_id,0);T_EQ(unit->movement.repulse.disable_depth,0);
        T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),family==1);
        if(family==2) {T_ASSERT(S_UnitIsSubmerged(unit));T_ASSERT(unit->s.renderfx&RF_HIDDEN);T_EQ(S_UnitMoveCategory(unit),8);}
        bool pending=unit->morph && unit->morph->expiration.active;T_EQ(pending,saved==2);
        if(pending)T_FEQ(unit->morph->expiration.deadline.time,.625f,.000001f);
        uint32_t slot=unit-g_edicts;
        if(saved) {cstring_t path=Test_TempPath("alternate364-attached.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),family==1);
            if(family==2) {T_ASSERT(S_UnitIsSubmerged(unit));T_EQ(S_UnitMoveCategory(unit),8);}}
        if(saved==2) {
            morph330_advance(.624f);T_EQ(unit->class_id,alt);T_EQ(unit->movement.repulse.disable_depth,0);
            morph330_advance(.625f);T_EQ(unit->class_id,alt);T_EQ(unit->movement.repulse.disable_depth,1);
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),family==1);
            if(family==2) {T_ASSERT(!S_UnitIsSubmerged(unit));T_EQ(S_UnitMoveCategory(unit),0xca);}
            morph330_advance(.999f);T_EQ(unit->class_id,alt);
            morph330_advance(1);T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,0);
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),family==0);
        } else {
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        bool accepted=unit_issueimmediateorder(unit,reverse);T_ASSERT(accepted);
        if(accepted) {
            T_EQ(unit->class_id,alt);T_EQ(unit->movement.repulse.disable_depth,1);
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),family==1);
            if(family==2) {T_ASSERT(!S_UnitIsSubmerged(unit));T_EQ(S_UnitMoveCategory(unit),0xca);}
            morph330_advance(1.374f);T_EQ(unit->class_id,alt);
            morph330_advance(1.375f);T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,0);
            T_EQ(unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL),family==0);
            if(family==2) {T_ASSERT(!S_UnitIsSubmerged(unit));T_EQ(S_UnitMoveCategory(unit),0xca);}
        }
        }
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_EQ(unit->class_id,base);
        T_ASSERT(!unit_hasstatusstate(unit,WC3_STATUS_STATE_ETHEREAL));T_ASSERT(!S_UnitIsSubmerged(unit));
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}

/* Phoenix365: original fatal-health admission keeps the same living handle,
 * protects the pending form, and owns both timed transitions and egg expiry. */
TEST(wc3_unit, phoenix365_fatal_admission_retains_live_identity_and_saved_rebirth) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned source=0;source<3;source++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char data[1400];snprintf(data,sizeof(data),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\nC;X7;K\"%s\"\nC;X8;K\"%s\"\n"
            "C;X1;Y2;K\"Ap65\"\nC;X2;K\"Aphx\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\n"
            "C;X5;K0.375\nC;X6;K0.625\nC;X7;K7\nC;X8;K0.125\nE\n",
            schema ? "Data11" : "DataA1",schema ? "Data12" : "DataB1",schema ? "Data13" : "DataC1");
        slkTestData_t *timing=parse_slk_string(data);G_SetSLKRows("AbilityData",timing);
        uint32_t code=FS_SLKKey("Ap65"),base=FS_SLKKey("hpea"),alt=FS_SLKKey("hfoo");
        edict_t *unit=alloc_test_unit(base,64,64);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        T_ASSERT(G_ActorAddSkill(unit,code));G_SetHealth(unit,unit->health.max_value);
        int const existing=source==2 ? 1 : 0;unit->invulnerability_depth=existing;unit->spell_immunity_depth=existing;
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        if(source==0)unit_die(unit,NULL);
        else if(source==1)T_Damage(unit,unit,10000);
        else G_SetHealth(unit,0);
        T_ASSERT(!M_IsDead(unit));T_ASSERT(!(unit->svflags&SVF_DEADMONSTER));T_EQ(unit->class_id,base);
        T_EQ(unit->current_order_id,G_OrderId("phoenixmorph"));
        T_EQ(unit->invulnerability_depth,existing+1);T_EQ(unit->spell_immunity_depth,existing+1);
        T_EQ(unit->movement.repulse.disable_depth,1);T_FEQ(unit->health.value,0,.000001f);
        bool pending=unit->morph && unit->morph->transition.active;T_ASSERT(pending);
        if(pending) {
            T_EQ(unit->morph->code,code);T_FEQ(unit->morph->transition.deadline.time,1.5f,.000001f);
            uint32_t slot=unit-g_edicts,identity=unit->spawn_time;
            cstring_t path=Test_TempPath("phoenix365-transition.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;
            T_ASSERT(!M_IsDead(unit));T_EQ(unit->spawn_time,identity);
            morph330_advance(1.499f);T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,1);
            morph330_advance(1.5f);T_EQ(unit->class_id,alt);T_ASSERT(!M_IsDead(unit));
            T_FEQ(unit->health.value,unit->health.max_value,.000001f);T_EQ(unit->movement.repulse.disable_depth,0);
            T_EQ(unit->invulnerability_depth,existing);T_EQ(unit->spell_immunity_depth,existing);
            pending=unit->morph->expiration.active;T_ASSERT(pending);if(pending)T_FEQ(unit->morph->expiration.deadline.time,2.125f,.000001f);
            path=Test_TempPath("phoenix365-egg.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;
            morph330_advance(2.124f);T_EQ(unit->class_id,alt);T_EQ(unit->movement.repulse.disable_depth,0);
            morph330_advance(2.125f);T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,1);
            T_EQ(unit->invulnerability_depth,existing);T_EQ(unit->spell_immunity_depth,existing+1);
            morph330_advance(2.499f);T_EQ(unit->class_id,base);
            morph330_advance(2.5f);T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,0);
            T_ASSERT(!M_IsDead(unit));T_FEQ(unit->health.value,unit->health.max_value,.000001f);
            T_EQ(unit->invulnerability_depth,existing);T_EQ(unit->spell_immunity_depth,existing);
            T_EQ(unit->spawn_time,identity);T_ASSERT(!unit->morph->transition.active && !unit->morph->expiration.active);
        }
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}

/* Burrow366: original forward/initial-alternate attachment publishes114 and
 * hidden state; reverse removes that source before the delayed type rebind. */
TEST(wc3_unit, burrow366_hidden_owner_survives_stop_pause_and_cold_save) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned initial=0;initial<2;initial++)for(unsigned saved=0;saved<2;saved++)for(unsigned action=0;action<3;action++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char text[1200];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Ab66\"\nC;X2;K\"Abur\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\nE\n",schema ? "Data11" : "DataA1");
        slkTestData_t *timing=parse_slk_string(text);G_SetSLKRows("AbilityData",timing);
        uint32_t code=FS_SLKKey("Ab66"),base=FS_SLKKey("hpea"),alt=FS_SLKKey("hfoo");
        edict_t *unit=alloc_test_unit(initial ? alt : base,64,64);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        uint32_t slot=unit-g_edicts;
        if(!initial) {
            T_ASSERT(unit_issueimmediateorder(unit,"burrow"));T_ASSERT(!S_UnitHasInvisibilityState(unit));
            if(action==0)T_ASSERT(unit_issueimmediateorder(unit,"stop"));
            if(action==1) {S_SetUnitPaused(unit,true);T_ASSERT(!S_UnitExecutionPaused(unit));}
            if(saved) {cstring_t path=Test_TempPath("burrow366-pending.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;}
            morph330_advance(1.375f);
        }
        T_EQ(unit->class_id,alt);T_EQ(S_UnitMoveCategory(unit),0xc8);T_ASSERT(S_UnitHasInvisibilityState(unit));
        T_EQ(S_UnitExecutionPaused(unit),action==1&&!initial);
        if(saved) {cstring_t path=Test_TempPath("burrow366-held.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;T_ASSERT(S_UnitHasInvisibilityState(unit));}
        if(action==1&&!initial) {S_SetUnitPaused(unit,false);morph330_advance(1.5f);S_RunMoveTimers();}
        level.pathing_clock.time=2;
        if(action==2) {
            T_ASSERT(G_ActorRemoveSkill(unit,code));T_EQ(unit->class_id,base);T_ASSERT(!S_UnitHasInvisibilityState(unit));
        } else {
            T_ASSERT(unit_issueimmediateorder(unit,"unburrow"));T_EQ(unit->class_id,alt);
            T_EQ(S_UnitMoveCategory(unit),0xca);T_ASSERT(!S_UnitHasInvisibilityState(unit));
            if(action==0)T_ASSERT(unit_issueimmediateorder(unit,"stop"));
            if(saved) {cstring_t path=Test_TempPath("burrow366-reverse.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;}
            morph330_advance(2.375f);T_EQ(unit->class_id,base);T_ASSERT(!S_UnitHasInvisibilityState(unit));
        }
        T_EQ(unit->movement.repulse.disable_depth,0);
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}


/* Native Burrow's41c=true runs the common active-form availability owner:
 * preserve Move/Attack/Morph, suspend Apiv until reverse completion. */
TEST(wc3_unit, burrow366_active_form_suspends_passives_until_reverse_completion) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned saved=0;saved<2;saved++)for(unsigned late=0;late<2;late++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char text[1800];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Ab66\"\nC;X2;K\"Abur\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\n"
            "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\nE\n",schema ? "Data11" : "DataA1");
        slkTestData_t *timing=parse_slk_string(text);G_SetSLKRows("AbilityData",timing);
        uint32_t code=FS_SLKKey("Ab66"),apiv=FS_SLKKey("Apiv");
        edict_t *unit=alloc_test_unit(FS_SLKKey("hpea"),64,64);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        if(!late) {T_ASSERT(G_ActorAddSkill(unit,apiv));T_ASSERT(G_ActorSetSkillPermanent(unit,apiv,true));T_ASSERT(unit->permanent_invisibility_fade.request.active);}
        T_ASSERT(unit_issueimmediateorder(unit,"burrow"));morph330_advance(1.375f);
        if(late) {T_ASSERT(G_ActorAddSkill(unit,apiv));T_ASSERT(G_ActorSetSkillPermanent(unit,apiv,true));}
        T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));T_ASSERT(!unit->permanent_invisibility_fade.request.active);
        T_ASSERT(!S_PermanentInvisibilityActive(unit));T_ASSERT(S_UnitHasInvisibilityState(unit));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,code));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,MAKEFOURCC('A','m','o','v')));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,MAKEFOURCC('A','a','t','k')));
        uint32_t slot=unit-g_edicts;
        if(saved) {cstring_t path=Test_TempPath("burrow366-passive.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));T_ASSERT(!unit->permanent_invisibility_fade.request.active);}
        level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(unit,"unburrow"));
        T_ASSERT(!S_UnitHasInvisibilityState(unit));T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));
        morph330_advance(2.374f);T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));
        morph330_advance(2.375f);T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,2.375f,.000001f);
        morph330_advance(3.05f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}

/* Original initial-alternate aliases each own114. Detaching the first
 * rebinds base; detaching the second from base does not run subtype inverse. */
TEST(wc3_unit, burrow366_alias_detach_from_base_retains_other_invisibility_source) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned frontier=0;frontier<3;frontier++)for(unsigned initial=0;initial<2;initial++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char text[1600];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Ab61\"\nC;X2;K\"Abur\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\n"
            "C;X1;Y3;K\"Ab62\"\nC;X2;K\"Abur\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\n"
            "C;X1;Y4;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.375\nE\n",schema ? "Data11" : "DataA1");
        slkTestData_t *timing=parse_slk_string(text);G_SetSLKRows("AbilityData",timing);
        uint32_t first=FS_SLKKey("Ab61"),second=FS_SLKKey("Ab62"),base=FS_SLKKey("hpea"),alt=FS_SLKKey("hfoo");
        edict_t *unit=alloc_test_unit(initial ? alt : base,64,64);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};uint32_t slot=unit-g_edicts;
        T_ASSERT(G_ActorAddSkill(unit,first));T_ASSERT(G_ActorSetSkillPermanent(unit,first,true));
        T_ASSERT(G_ActorAddSkill(unit,second));T_ASSERT(G_ActorSetSkillPermanent(unit,second,true));
        if(!initial) {T_ASSERT(!S_UnitHasInvisibilityState(unit));T_ASSERT(unit_issueimmediateorder(unit,"burrow"));morph330_advance(1.375f);}
        T_EQ(unit->class_id,alt);T_EQ(S_UnitMoveCategory(unit),0xc8);T_ASSERT(S_UnitHasInvisibilityState(unit));
        for(unsigned phase=0;phase<3;phase++) {
            if(phase==1) {
                if(!initial) {level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(unit,"unburrow"));morph330_advance(2.375f);}
                T_ASSERT(G_ActorRemoveSkill(unit,first));T_ASSERT(G_ActorHasAbilityCode(unit,second));}
            if(phase==2) {T_ASSERT(G_ActorRemoveSkill(unit,second));T_ASSERT(!G_ActorHasAbilityCode(unit,first));T_ASSERT(!G_ActorHasAbilityCode(unit,second));}
            if(phase==frontier) {cstring_t path=Test_TempPath("burrow366-alias.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;}
            T_EQ(unit->class_id,phase ? base : alt);T_EQ(S_UnitHasInvisibilityState(unit),initial || phase==0);
            T_EQ(S_UnitMoveCategory(unit),phase ? 0xca : 0xc8);T_EQ(unit->movement.repulse.disable_depth,0);
        }
        /* Native late Apiv inherits the retained Morph contribution only in
         * the initial-alternate/detach-from-base scene. */
        uint32_t apiv=FS_SLKKey("Apiv");level.pathing_clock.time=3;
        T_ASSERT(G_ActorAddSkill(unit,apiv));T_EQ(S_AbilityExecutionDisabled(unit,apiv),initial);
        T_EQ(unit->permanent_invisibility_fade.request.active,!initial);
        morph330_advance(3.5f);T_EQ(S_PermanentInvisibilityActive(unit),!initial);
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_EQ(S_UnitHasInvisibilityState(unit),initial);
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}

static unsigned burrow366_suspend_count,burrow366_premature_hidden;
static intptr_t burrow366_observe_suspend(edict_t *unit,abilityMsg_t msg,abilityCall_t const *call) {
    if(msg==A_UNIT_SUSPEND) {
        burrow366_suspend_count++;
        if(S_UnitIsBurrowed(unit))burrow366_premature_hidden++;
    }
    return CAbilityPermanentInvisibility(unit,msg,call);
}

/*54eb80 suspends old passive sources before subtype40c acquires Burrow114. */
TEST(wc3_unit, burrow366_passive_suspend_precedes_subtype_publication) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned initial=0;initial<2;initial++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char text[1400];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Ab66\"\nC;X2;K\"Abur\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\n"
            "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0\nE\n",schema ? "Data11" : "DataA1");
        slkTestData_t *timing=parse_slk_string(text);G_SetSLKRows("AbilityData",timing);
        ability_t *passive=(ability_t *)FindAbilityByClassname("Apiv");abilityProc_t original=passive->proc;
        passive->proc=burrow366_observe_suspend;
        edict_t *unit=alloc_test_unit(FS_SLKKey(initial ? "hfoo" : "hpea"),64,64);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Apiv")));T_ASSERT(G_ActorSetSkillPermanent(unit,FS_SLKKey("Apiv"),true));
        T_ASSERT(S_PermanentInvisibilityActive(unit));T_ASSERT(!S_UnitIsBurrowed(unit));
        burrow366_suspend_count=burrow366_premature_hidden=0;
        T_ASSERT(G_ActorAddSkill(unit,FS_SLKKey("Ab66")));T_ASSERT(G_ActorSetSkillPermanent(unit,FS_SLKKey("Ab66"),true));
        if(!initial) {T_ASSERT(unit_issueimmediateorder(unit,"burrow"));morph330_advance(1.375f);}
        T_EQ(burrow366_suspend_count,1);T_EQ(burrow366_premature_hidden,0);
        T_ASSERT(S_UnitIsBurrowed(unit));T_ASSERT(!S_PermanentInvisibilityActive(unit));
        passive->proc=original;
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}

/* Native detach completes a pending forward before its synchronous inverse;
 * the subtype source acquired by that completion must be released as well. */
TEST(wc3_unit, burrow366_early_detach_completes_forward_then_releases_subtype) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned reverse=0;reverse<2;reverse++)for(unsigned saved=0;saved<2;saved++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char text[1400];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Ab66\"\nC;X2;K\"Abur\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\n"
            "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.375\nE\n",schema ? "Data11" : "DataA1");
        slkTestData_t *timing=parse_slk_string(text);G_SetSLKRows("AbilityData",timing);
        uint32_t code=FS_SLKKey("Ab66"),apiv=FS_SLKKey("Apiv"),base=FS_SLKKey("hpea");
        edict_t *unit=alloc_test_unit(FS_SLKKey(reverse ? "hfoo" : "hpea"),64,64);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        T_ASSERT(unit_issueimmediateorder(unit,reverse ? "unburrow" : "burrow"));T_ASSERT(unit->morph->transition.active);
        level.pathing_clock.time=1.125f;uint32_t slot=unit-g_edicts;
        if(saved) {cstring_t path=Test_TempPath("burrow366-detach.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;}
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_EQ(unit->class_id,base);T_EQ(unit->movement.repulse.disable_depth,0);
        T_ASSERT(!unit->morph->transition.active);T_ASSERT(!S_UnitHasInvisibilityState(unit));T_ASSERT(!G_ActorHasAbilityCode(unit,code));
        T_ASSERT(G_ActorAddSkill(unit,apiv));T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));T_ASSERT(unit->permanent_invisibility_fade.request.active);
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}

/* Morph372: public Submerge/Apiv original and observer-free control match. */
TEST(wc3_unit, morph372_submerge_suspends_passive_until_reverse_completion) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned saved=0;saved<3;saved++)for(unsigned family=0;family<3;family++)for(unsigned mode=0;mode<3;mode++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char const *families[]={"Asb1","Asb2","Asb3"};
        char text[1800];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Ms72\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\n"
            "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\nE\n",schema ? "Data11" : "DataA1",families[family]);
        slkTestData_t *timing=parse_slk_string(text);G_SetSLKRows("AbilityData",timing);
        uint32_t code=FS_SLKKey("Ms72"),apiv=FS_SLKKey("Apiv");
        uint8_t water[]={CM_PATHING_UNWALKABLE};CM_SetupTestPathmap(1,1,water);
        edict_t *unit=alloc_test_unit(FS_SLKKey(mode==2 ? "hfoo" : "hpea"),.5f,.5f);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        if(mode==0) {T_ASSERT(G_ActorAddSkill(unit,apiv));T_ASSERT(G_ActorSetSkillPermanent(unit,apiv,true));T_ASSERT(unit->permanent_invisibility_fade.request.active);}
        if(mode==0) {morph330_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));}
        level.pathing_clock.time=2;
        if(mode!=2) {T_ASSERT(unit_issueimmediateorder(unit,"submerge"));morph330_advance(2.375f);}
        else {T_ASSERT(S_UnitIsSubmerged(unit));morph330_advance(2.375f);}
        if(mode!=0) {T_ASSERT(G_ActorAddSkill(unit,apiv));T_ASSERT(G_ActorSetSkillPermanent(unit,apiv,true));}
        T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));T_ASSERT(!unit->permanent_invisibility_fade.request.active);
        T_ASSERT(!S_PermanentInvisibilityActive(unit));T_ASSERT(S_UnitIsSubmerged(unit));T_ASSERT(S_UnitHasInvisibilityState(unit));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,code));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,MAKEFOURCC('A','m','o','v')));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,MAKEFOURCC('A','a','t','k')));
        uint32_t slot=unit-g_edicts;
        if(saved==1) {cstring_t path=Test_TempPath("morph372-passive.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));T_ASSERT(!unit->permanent_invisibility_fade.request.active);}
        level.pathing_clock.time=3;T_ASSERT(unit_issueimmediateorder(unit,"unsubmerge"));
        T_ASSERT(!S_UnitHasInvisibilityState(unit));T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));
        if(saved==2) {
            morph330_advance(3.125f);
            cstring_t path=Test_TempPath("morph372-reverse.bin");
            T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;
            T_ASSERT(unit->morph && unit->morph->transition.active && unit->morph->reverse);
            T_ASSERT(!S_UnitHasInvisibilityState(unit));T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));
            T_ASSERT(!unit->permanent_invisibility_fade.request.active);
        }
        morph330_advance(3.374f);T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));
        morph330_advance(3.375f);T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_ASSERT(unit->permanent_invisibility_fade.request.active);T_FEQ(unit->permanent_invisibility_fade.origin.time,3.375f,.000001f);
        morph330_advance(4.05f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}


/* Morph372: detach must retire the same active-form suspension source. */
TEST(wc3_unit, morph372_submerge_detach_releases_passive_owner) {
    for(unsigned schema=0;schema<2;schema++)for(unsigned saved=0;saved<2;saved++)for(unsigned family=0;family<3;family++)for(unsigned pending=0;pending<4;pending++) {
        reset_test_entities();setup_test_world();
        slkTestData_t *rows,*old_rows,*ui,*old_ui,*profile,*old_profile;
        install_raven_form_test_data(&rows,&old_rows,&ui,&old_ui,&profile,&old_profile);
        char const *families[]={"Asb1","Asb2","Asb3"};
        char text[1800];snprintf(text,sizeof(text),"ID;PWXL;N;E\nC;X1;Y1;K\"alias\"\nC;X2;K\"code\"\nC;X3;K\"%s\"\n"
            "C;X4;K\"UnitID1\"\nC;X5;K\"Dur1\"\nC;X6;K\"HeroDur1\"\n"
            "C;X1;Y2;K\"Ms72\"\nC;X2;K\"%s\"\nC;X3;K\"hpea\"\nC;X4;K\"hfoo\"\nC;X5;K0.375\nC;X6;K0\n"
            "C;X1;Y3;K\"Apiv\"\nC;X2;K\"Apiv\"\nC;X5;K0.625\nE\n",schema ? "Data11" : "DataA1",families[family]);
        slkTestData_t *timing=parse_slk_string(text);G_SetSLKRows("AbilityData",timing);
        uint32_t code=FS_SLKKey("Ms72"),apiv=FS_SLKKey("Apiv");
        uint8_t water[]={CM_PATHING_UNWALKABLE};CM_SetupTestPathmap(1,1,water);
        edict_t *unit=alloc_test_unit(FS_SLKKey(pending==3 ? "hfoo" : "hpea"),.5f,.5f);unit->stand=unit_stand;unit->die=unit_die;unit->svflags|=SVF_MONSTER;
        level.pathing_clock=(wc3Clock_t){.time=1,.span=300};
        T_ASSERT(G_ActorAddSkill(unit,code));T_ASSERT(G_ActorSetSkillPermanent(unit,code,true));
        T_ASSERT(G_ActorAddSkill(unit,apiv));T_ASSERT(G_ActorSetSkillPermanent(unit,apiv,true));
        if(pending!=3) {
            morph330_advance(1.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
            level.pathing_clock.time=2;T_ASSERT(unit_issueimmediateorder(unit,"submerge"));
        }
        morph330_advance(pending==1 ? 2.125f : 2.375f);
        if(pending==1) {
            T_ASSERT(unit->morph && unit->morph->transition.active);
            T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        } else {
            T_ASSERT(S_UnitIsSubmerged(unit));
            T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));
            T_ASSERT(!unit->permanent_invisibility_fade.request.active);
        }
        if(pending==2) {
            level.pathing_clock.time=2.5f;T_ASSERT(unit_issueimmediateorder(unit,"unsubmerge"));
            morph330_advance(2.625f);T_ASSERT(unit->morph->transition.active && unit->morph->reverse);
            T_ASSERT(!S_UnitIsSubmerged(unit));T_ASSERT(S_AbilityExecutionDisabled(unit,apiv));
        }
        uint32_t slot=unit-g_edicts;
        if(saved) {cstring_t path=Test_TempPath("morph372-detach.bin");T_ASSERT(WriteGame(path));T_ASSERT(ReadGame(path));remove(path);unit=g_edicts+slot;}
        float detached_at=pending==1 ? 2.125f : pending==2 ? 2.625f : 3.0f;level.pathing_clock.time=detached_at;
        T_ASSERT(G_ActorRemoveSkill(unit,code));T_ASSERT(!G_ActorHasAbilityCode(unit,code));
        T_EQ(unit->class_id,FS_SLKKey("hpea"));
        T_ASSERT(!unit->morph || !unit->morph->transition.active);
        T_EQ(unit->movement.repulse.disable_depth,0);
        T_ASSERT(!S_UnitIsSubmerged(unit));T_ASSERT(!(unit->s.renderfx&RF_HIDDEN));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_ASSERT(unit->permanent_invisibility_fade.request.active);
        T_FEQ(unit->permanent_invisibility_fade.origin.time,detached_at,.000001f);
        /* Removing and reattaching the passive must not inherit a leaked
         * whole-unit contribution after its Morph owner has detached. */
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        T_ASSERT(G_ActorAddSkill(unit,apiv));T_ASSERT(G_ActorSetSkillPermanent(unit,apiv,true));
        T_ASSERT(!S_AbilityExecutionDisabled(unit,apiv));
        T_ASSERT(unit->permanent_invisibility_fade.request.active);
        morph330_advance(detached_at+.75f);T_ASSERT(S_PermanentInvisibilityActive(unit));
        T_ASSERT(G_ActorRemoveSkill(unit,apiv));T_ASSERT(!S_UnitHasInvisibilityState(unit));
        reset_entities();restore_raven_form_test_data(rows,old_rows,ui,old_ui,profile,old_profile);free_slk_rows(timing);
    }
}

#endif /* BZ_TESTS */
