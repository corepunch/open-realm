#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../skills/s_skills.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void reset_entities(void);
void setup_test_world(void);
slkTestData_t *parse_slk_string(char const *text);
void free_slk_rows(slkTestData_t *rows);

#define TEST_AROO MAKEFOURCC('A', 'r', 'o', 'o')
#define TEST_AHHB MAKEFOURCC('A', 'H', 'h', 'b')
#define TEST_HBAR MAKEFOURCC('h', 'b', 'a', 'r')

static UnitAbilities_t ancient_abilities = { .abilList = "Aroo" };

/* Distinct authored values prove the morph directions do not share a timer. */
static char const ancient_root_tft[] =
    "ID;PWXL;N;EBB;Y3;X10\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
    "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"DataD1\"\n"
    "C;Y2;X1;K\"Aroo\"\nC;Y2;X2;K\"Aroo\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X5;K\"2.25\"\nC;Y2;X6;K\"6.75\"\n"
    "C;Y2;X7;K\"3\"\nC;Y2;X8;K\"3\"\nC;Y2;X10;K\"1\"\n"
    "C;Y3;X1;K\"AHhb\"\nC;Y3;X2;K\"AHhb\"\nC;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"ground\"\nE\n";

/* ROC keeps AbilityData's row-major Data11..Data34 columns. */
static char const ancient_root_roc[] =
    "ID;PWXL;N;EBB;Y3;X9\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"targs\"\nC;Y1;X3;K\"Dur1\"\n"
    "C;Y1;X4;K\"HeroDur1\"\nC;Y1;X5;K\"Data11\"\nC;Y1;X6;K\"Data12\"\n"
    "C;Y1;X7;K\"Data13\"\nC;Y1;X8;K\"Data14\"\nC;Y1;X9;K\"levels\"\n"
    "C;Y2;X1;K\"Aroo\"\nC;Y2;X3;K\"2.25\"\nC;Y2;X4;K\"6.75\"\n"
    "C;Y2;X5;K\"3\"\nC;Y2;X6;K\"3\"\nC;Y2;X8;K\"1\"\nC;Y2;X9;K\"1\"\n"
    "C;Y3;X1;K\"AHhb\"\nC;Y3;X2;K\"ground\"\nC;Y3;X9;K\"1\"\nE\n";

static edict_t *ancient_test_unit(bool rooted) {
    edict_t *unit = alloc_test_unit(TEST_HBAR, 64.0f, 64.0f);
    unit->data.UnitAbilities = &ancient_abilities;
    unit->svflags |= SVF_MONSTER;
    unit->s.player = 0;
    unit->stand = unit_stand;
    unit->ancient_root.ability = TEST_AROO;
    unit->ancient_root.unit_type = unit->class_id;
    unit->ancient_root.rooted_defense_type = FindEnumValue(unit->data.UnitBalance->defenseType, defense_type);
    unit->ancient_root.mode = rooted ? ANCIENT_ROOTED : ANCIENT_UPROOTED;
    if (rooted) {
        unit->s.flags |= EF_BUILDING;
        unit->aiflags |= AI_IMMOBILE;
        unit->runtime.flags |= UNIT_BALANCE_BUILDING;
    } else {
        unit->s.flags &= ~EF_BUILDING;
        unit->aiflags &= ~AI_IMMOBILE;
        unit->runtime.flags &= ~UNIT_BALANCE_BUILDING;
        unit->movetype = MOVETYPE_STEP;
    }
    unit_stand(unit);
    return unit;
}

static void ancient_update(edict_t *unit) {
    abilityitem_t item = S_AbilityItem(TEST_AROO);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item);
    S_AbilityMessage(unit, A_UPDATE, &call);
}

static void ancient_assert_direction_durations(cstring_t slk) {
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit;
    uint32_t start;

    reset_entities();
    setup_test_world();
    level.time = 1000;
    unit = ancient_test_unit(false);
    start = G_Time();
    S_AncientBeginMorph(unit, true);
    T_EQ(unit->ancient_root.mode, ANCIENT_ROOTING);
    T_EQ(unit->ancient_root.transition_end_time, start + 2250);
    T_FEQ(unit->wait, 2.25f, 0.001f);
    T_FEQ(unit->currentmove->animation_duration(unit), 2.25f, 0.001f);

    unit->ancient_root.mode = ANCIENT_ROOTED;
    start = G_Time();
    S_AncientBeginMorph(unit, false);
    T_EQ(unit->ancient_root.mode, ANCIENT_UPROOTING);
    T_EQ(unit->ancient_root.transition_end_time, start + 6750);
    T_FEQ(unit->wait, 6.75f, 0.001f);
    T_FEQ(unit->currentmove->animation_duration(unit), 6.75f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_ancient_root, roc_and_tft_use_separate_root_and_uproot_durations) {
    ancient_assert_direction_durations(ancient_root_roc);
    ancient_assert_direction_durations(ancient_root_tft);
}

TEST(wc3_ancient_root, command_button_uses_uproot_art_while_rooted) {
    edict_t *unit;
    gameCommandButton_t button;

    reset_entities(); setup_test_world();
    unit = ancient_test_unit(false);
    T_ASSERT(G_BuildCommandButton(unit, "Aroo", false, 0, &button));
    T_STREQ(button.art, "TestUI\\Textures\\root.blp");

    unit->ancient_root.mode = ANCIENT_ROOTED;
    T_ASSERT(G_BuildCommandButton(unit, "Aroo", false, 0, &button));
    T_STREQ(button.art, "TestUI\\Textures\\uproot.blp");
    T_STREQ(button.tooltip, "Uproot");
}

TEST(wc3_ancient_root, uproot_morph_rejects_orders_until_authored_hero_duration) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit, *enemy;
    vec2_t destination = { 256.0f, 256.0f };
    uint32_t end_time;

    reset_entities(); setup_test_world(); level.time = 1000;
    unit = ancient_test_unit(true);
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    enemy->svflags |= SVF_MONSTER; enemy->s.player = 1;

    T_ASSERT(unit_issueimmediateorder(unit, "unroot"));
    end_time = unit->ancient_root.transition_end_time;
    T_EQ(end_time, G_Time() + 6750);
    T_ASSERT(!unit_issueimmediateorder(unit, "stop"));
    T_ASSERT(!unit_issueimmediateorder(unit, "unroot"));
    T_ASSERT(!G_IssueUnitPointOrder(unit, "move", &destination, false, 0, 0.0f));
    T_ASSERT(!G_IssueUnitTargetOrder(unit, "attack", enemy, false, 0));
    T_EQ(unit->currentmove->proc, CAbilityRoot);
    T_EQ(unit->ancient_root.transition_end_time, end_time);

    level.time = end_time - 1; ancient_update(unit);
    T_EQ(unit->ancient_root.mode, ANCIENT_UPROOTING);
    level.time = end_time; ancient_update(unit);
    T_EQ(unit->ancient_root.mode, ANCIENT_UPROOTED);
    T_ASSERT(!G_UnitIsStructure(unit));
    T_ASSERT(G_UnitIsBuilding(unit->class_id));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_ancient_root, root_morph_rejects_orders_until_authored_duration) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit, *enemy;
    vec2_t destination = { 256.0f, 256.0f };
    uint32_t end_time;

    reset_entities(); setup_test_world(); level.time = 2000;
    unit = ancient_test_unit(false);
    enemy = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    enemy->svflags |= SVF_MONSTER; enemy->s.player = 1;
    S_AncientBeginMorph(unit, true);
    end_time = unit->ancient_root.transition_end_time;
    T_EQ(end_time, G_Time() + 2250);
    T_ASSERT(!unit_issueimmediateorder(unit, "stop"));
    T_ASSERT(!G_IssueUnitPointOrder(unit, "move", &destination, false, 0, 0.0f));
    T_ASSERT(!G_IssueUnitTargetOrder(unit, "attack", enemy, false, 0));
    T_EQ(unit->currentmove->proc, CAbilityRoot);

    level.time = end_time - 1; ancient_update(unit);
    T_EQ(unit->ancient_root.mode, ANCIENT_ROOTING);
    level.time = end_time; ancient_update(unit);
    T_EQ(unit->ancient_root.mode, ANCIENT_ROOTED);
    T_ASSERT(G_UnitIsStructure(unit));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_ancient_root, approaching_root_remains_interruptible) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit;

    reset_entities(); setup_test_world(); level.time = 1000;
    unit = ancient_test_unit(false);
    unit->ancient_root.mode = ANCIENT_ROOTING;
    unit->ancient_root.approaching = true;
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &MAKE(vec2_t, .x = 256, .y = 256), false, 0, 0.0f));
    T_EQ(unit->ancient_root.mode, ANCIENT_ROOTING);
    T_ASSERT(unit_issueimmediateorder(unit, "stop"));
    T_EQ(unit->ancient_root.mode, ANCIENT_UPROOTED);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_ancient_root, ability_availability_is_enforced_by_simulation_dispatch) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit = NULL, *tree;
    abilityitem_t eat = S_AbilityItem(MAKEFOURCC('A','e','a','t'));
    spellTarget_t target;
    abilityCall_t call;

    reset_entities(); setup_test_world(); level.time = 1000;
    unit = ancient_test_unit(true);
    tree = alloc_test_unit(MAKEFOURCC('h','t','r','e'), 100.0f, 64.0f);
    tree->targtype = TARG_TREE;
    target = MAKE(spellTarget_t, .type = SPELL_TARGET_UNIT, .entity = tree);
    call = MAKE(abilityCall_t, .item = &eat, .target = &target);
    T_ASSERT(!S_AncientAbilityAvailable(unit, eat.ability));
    T_ASSERT(!S_AbilityMessage(unit, A_VALIDATE, &call));

    unit->ancient_root.mode = ANCIENT_UPROOTED;
    unit->s.flags &= ~EF_BUILDING;
    unit->aiflags &= ~AI_IMMOBILE;
    T_ASSERT(S_AncientAbilityAvailable(unit, eat.ability));
    T_ASSERT(S_AbilityMessage(unit, A_VALIDATE, &call));

    unit->ancient_root.mode = ANCIENT_UPROOTING;
    T_ASSERT(!S_AncientAbilityAvailable(unit, eat.ability));
    T_ASSERT(!S_AbilityMessage(unit, A_VALIDATE, &call));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_ancient_root, spell_structure_filter_tracks_runtime_mode) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster, *target;

    reset_entities(); setup_test_world(); level.time = 1000;
    caster = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    caster->svflags |= SVF_MONSTER; caster->s.player = 0;
    target = ancient_test_unit(true);
    target->s.player = 1;
    target->targtype = TARG_STRUCTURE;
    T_ASSERT(G_UnitIsBuilding(target->class_id));
    T_ASSERT(G_UnitIsStructure(target));
    T_ASSERT(!S_SpellAllowsTarget(TEST_AHHB, caster, target));

    target->ancient_root.mode = ANCIENT_UPROOTED;
    target->s.flags &= ~EF_BUILDING;
    target->aiflags &= ~AI_IMMOBILE;
    target->runtime.flags &= ~UNIT_BALANCE_BUILDING;
    T_ASSERT(G_UnitIsBuilding(target->class_id));
    T_ASSERT(!G_UnitIsStructure(target));
    T_ASSERT(S_SpellAllowsTarget(TEST_AHHB, caster, target));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

#endif
