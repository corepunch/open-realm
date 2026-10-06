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
#define TEST_ARO2 MAKEFOURCC('A', 'r', 'o', '2')
#define TEST_AHHB MAKEFOURCC('A', 'H', 'h', 'b')
#define TEST_HBAR MAKEFOURCC('h', 'b', 'a', 'r')

static UnitAbilities_t ancient_abilities = { .abilList = "Aroo" };
static bool ancient_root_button_seen;
static int16_t ancient_root_button_x, ancient_root_button_y;

static void ancient_capture_command_button(pfWriteType_t type, void const *value) {
    uiFrame_t const *frame;
    if (type != PF_UIFRAME || !value) return;
    frame = value;
    if (frame->flags.type != FT_COMMANDBUTTON || !frame->onclick ||
        strcmp(frame->onclick, "button Aroo")) return;
    ancient_root_button_seen = true;
    ancient_root_button_x = frame->points.x[FPP_MIN].offset;
    ancient_root_button_y = frame->points.y[FPP_MIN].offset;
}

/* Distinct authored values prove the morph directions do not share a timer. */
static char const ancient_root_tft[] =
    "ID;PWXL;N;EBB;Y5;X10\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
    "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"DataD1\"\n"
    "C;Y2;X1;K\"Aroo\"\nC;Y2;X2;K\"Aroo\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X5;K\"2.25\"\nC;Y2;X6;K\"6.75\"\n"
    "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"3\"\nC;Y2;X10;K\"1\"\n"
    "C;Y3;X1;K\"AHhb\"\nC;Y3;X2;K\"AHhb\"\nC;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"ground\"\n"
    "C;Y4;X1;K\"AHst\"\nC;Y4;X2;K\"AHst\"\nC;Y4;X3;K\"1\"\n"
    "C;Y4;X4;K\"structure\"\n"
    "C;Y5;X1;K\"Aro2\"\nC;Y5;X2;K\"Aro2\"\nC;Y5;X3;K\"1\"\n"
    "C;Y5;X5;K\"2.25\"\nC;Y5;X6;K\"6.75\"\n"
    "C;Y5;X7;K\"2\"\nC;Y5;X8;K\"1\"\nC;Y5;X10;K\"1\"\nE\n";

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
    if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
    assert(unit->ancient_root);
    unit->ancient_root->ability = TEST_AROO;
    unit->ancient_root->unit_type = unit->class_id;
    unit->ancient_root->rooted_defense_type = FindEnumValue(unit->data.UnitBalance->defenseType, defense_type);
    unit->ancient_root->mode = rooted ? ANCIENT_ROOTED : ANCIENT_UPROOTED;
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
    T_EQ(unit->ancient_root->mode, ANCIENT_ROOTING);
    T_EQ(unit->ancient_root->transition_end_time, start + 2250);
    T_FEQ(unit->wait, 2.25f, 0.001f);
    T_FEQ(unit->currentmove->animation_duration(unit), 2.25f, 0.001f);

    if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
    assert(unit->ancient_root);
    unit->ancient_root->mode = ANCIENT_ROOTED;
    start = G_Time();
    S_AncientBeginMorph(unit, false);
    T_EQ(unit->ancient_root->mode, ANCIENT_UPROOTING);
    T_EQ(unit->ancient_root->transition_end_time, start + 6750);
    T_FEQ(unit->wait, 6.75f, 0.001f);
    T_FEQ(unit->currentmove->animation_duration(unit), 6.75f, 0.001f);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_ancient_root, roc_and_tft_use_separate_root_and_uproot_durations) {
    ancient_assert_direction_durations(ancient_root_roc);
    ancient_assert_direction_durations(ancient_root_tft);
}

TEST(wc3_ancient_root, missing_ability_data_does_not_start_morph) {
    static char const missing_aro2[] =
        "ID;PWXL;N;EBB;Y4;X10\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Dur1\"\nC;Y1;X6;K\"HeroDur1\"\n"
        "C;Y1;X7;K\"DataA1\"\nC;Y1;X8;K\"DataB1\"\nC;Y1;X9;K\"DataC1\"\nC;Y1;X10;K\"DataD1\"\n"
        "C;Y2;X1;K\"Aroo\"\nC;Y2;X2;K\"Aroo\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X5;K\"2.25\"\nC;Y2;X6;K\"6.75\"\nC;Y2;X7;K\"0\"\nC;Y2;X8;K\"3\"\nC;Y2;X10;K\"1\"\nE\n";
    slkTestData_t *rows = parse_slk_string(missing_aro2);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit;
    reset_entities(); setup_test_world(); level.time = 1000;
    unit = ancient_test_unit(false);
    if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
    assert(unit->ancient_root);
    unit->ancient_root->ability = MAKEFOURCC('A','r','o','2');

    S_AncientBeginMorph(unit, true);
    T_EQ(unit->ancient_root->mode, ANCIENT_UPROOTED);
    T_ASSERT(!unit->ancient_root || unit->ancient_root->transition_end_time == 0);
    T_EQ(S_AncientAttackMask(unit), 3);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_ancient_root, command_button_uses_uproot_art_while_rooted) {
    edict_t *unit;
    gameCommandButton_t button;

    reset_entities(); setup_test_world();
    unit = ancient_test_unit(false);
    T_ASSERT(G_BuildCommandButton(unit, "Aroo", false, 0, &button));
    T_STREQ(button.art, "TestUI\\Textures\\root.blp");
    T_EQ(button.alternate_active, 0);
    T_EQ(button.engaged, 0);

    if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
    assert(unit->ancient_root);
    unit->ancient_root->mode = ANCIENT_ROOTED;
    T_ASSERT(G_BuildCommandButton(unit, "Aroo", false, 0, &button));
    T_STREQ(button.art, "TestUI\\Textures\\uproot.blp");
    T_STREQ(button.tooltip, "Uproot");
    T_EQ(button.alternate_active, 0);
    T_EQ(button.engaged, 1);
}

TEST(wc3_ancient_root, shop_command_card_keeps_uproot_position_and_click_dispatches) {
    static UnitProfile_t shop_profile = { .makeItems = "spro" };
    static UnitWeapons_t weapons = { .attacksEnabled = 3, .attack1 = { .damageDice = 1 } };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old_rows = G_SetSLKRows("AbilityData", rows);
    edict_t *clent, *unit, *target;
    gameCommandButton_t expected, buttons[12];
    uint8_t count;
    bool attack_button;
    cstring_t click[] = { "button", "Aroo" };

    reset_entities(); setup_test_world(); level.time = 1000;
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    clent = &g_edicts[0];
    clent->inuse = true;
    clent->client = game.clients;
    clent->client->connected = true;
    clent->client->ps.number = 0;
    unit = ancient_test_unit(true);
    unit->s.player = 0;
    unit->data.UnitProfile = &shop_profile;
    unit->data.UnitWeapons = &weapons;
    unit->attack1.type = ATK_NORMAL;
    unit->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;
    T_ASSERT(G_CanUseItemShop(clent->client, unit));
    G_SetStockSlots(unit, true, 1);
    G_SelectEntity(clent->client, unit);

    T_ASSERT(G_BuildCommandButton(unit, "Aroo", false, 0, &expected));
    T_EQ(expected.x, 1);
    T_EQ(expected.y, 1);
    count = G_GetCommandButtons(unit, buttons, 12);
    FOR_LOOP(i, count) T_ASSERT(strcmp(buttons[i].command, STR_CmdAttack));
    T_ASSERT(!S_AttackCanTarget(unit, target));
    T_ASSERT(!S_OrderAttack(unit, target));

    ancient_root_button_seen = false;
    gi.Write = ancient_capture_command_button;
    Get_Commands_f(clent);
    gi.Write = old_write;
    T_ASSERT(ancient_root_button_seen);
    T_EQ(ancient_root_button_x, (int16_t)((0.6175f + expected.x * 0.0434f) * UI_FRAMEPOINT_SCALE));
    T_EQ(ancient_root_button_y, (int16_t)(-(0.4660f + expected.y * 0.0440f) * UI_FRAMEPOINT_SCALE));

    G_ClientCommand(clent, 2, click);
    T_EQ(unit->ancient_root->mode, ANCIENT_UPROOTING);

    unit->ancient_root->mode = ANCIENT_UPROOTED;
    unit->s.flags &= ~EF_BUILDING;
    unit->aiflags &= ~AI_IMMOBILE;
    unit->runtime.flags &= ~UNIT_BALANCE_BUILDING;
    unit->movetype = MOVETYPE_STEP;
    count = G_GetCommandButtons(unit, buttons, 12);
    attack_button = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, STR_CmdAttack)) attack_button = true;
    T_ASSERT(attack_button);
    T_ASSERT(S_UnitAttackSlotEnabled(unit, 0));
    T_ASSERT(S_AttackCanTarget(unit, target));
    T_ASSERT(S_OrderAttack(unit, target));

    G_SetSLKRows("AbilityData", old_rows);
    free_slk_rows(rows);
}

TEST(wc3_ancient_root, protector_uses_authored_rooted_attack_mask) {
    static UnitWeapons_t weapons = { .attacksEnabled = 3, .attack1 = { .damageDice = 1 }, .attack2 = { .damageDice = 1 } };
    static UnitAbilities_t protector_abilities = { .abilList = "Aro2" };
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old_rows = G_SetSLKRows("AbilityData", rows);
    edict_t *unit, *target;
    gameCommandButton_t buttons[12];
    uint8_t count;
    bool attack_button = false;

    reset_entities(); setup_test_world(); level.time = 1000;
    unit = ancient_test_unit(true);
    unit->ancient_root->ability = TEST_ARO2;
    unit->data.UnitWeapons = &weapons;
    unit->attack1.type = ATK_NORMAL;
    unit->attack2.type = ATK_NORMAL;
    unit->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    unit->attack2.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96.0f, 64.0f);
    target->s.player = 1;
    target->svflags |= SVF_MONSTER;
    target->targtype = TARG_GROUND;

    T_EQ(S_AncientAttackMask(unit), 2);
    T_ASSERT(!S_UnitAttackSlotEnabled(unit, 0));
    T_ASSERT(S_UnitAttackSlotEnabled(unit, 1));
    T_ASSERT(S_AttackCanTarget(unit, target));
    T_ASSERT(S_OrderAttack(unit, target));
    count = G_GetCommandButtons(unit, buttons, 12);
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, STR_CmdAttack)) attack_button = true;
    T_ASSERT(attack_button);

    unit->ancient_root->mode = ANCIENT_ROOTING;
    T_ASSERT(!S_UnitAttackSlotEnabled(unit, 1));
    unit->ancient_root->mode = ANCIENT_UPROOTING;
    T_ASSERT(!S_UnitAttackSlotEnabled(unit, 1));

    /* Map-start rooted Protectors can be queried before the A_UPDATE hook has
     * allocated ancient_root runtime state. Their authored Aro2 rooted attack
     * mask must still govern server validation and the command card. */
    G_FreeAncientRoot(unit);
    unit->data.UnitAbilities = &protector_abilities;
    T_ASSERT(S_AncientIsRooted(unit));
    T_ASSERT(S_UnitAttackSlotEnabled(unit, 1));
    T_ASSERT(!S_UnitAttackSlotEnabled(unit, 0));
    T_ASSERT(S_AttackCanTarget(unit, target));
    T_ASSERT(S_OrderAttack(unit, target));
    count = G_GetCommandButtons(unit, buttons, 12);
    attack_button = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, STR_CmdAttack)) attack_button = true;
    T_ASSERT(attack_button);

    G_SetSLKRows("AbilityData", old_rows);
    free_slk_rows(rows);
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
    end_time = unit->ancient_root->transition_end_time;
    T_EQ(end_time, G_Time() + 6750);
    T_ASSERT(!unit_issueimmediateorder(unit, "stop"));
    T_ASSERT(!unit_issueimmediateorder(unit, "unroot"));
    T_ASSERT(!G_IssueUnitPointOrder(unit, "move", &destination, false, 0, 0.0f));
    T_ASSERT(!G_IssueUnitTargetOrder(unit, "attack", enemy, false, 0));
    T_EQ(unit->currentmove->proc, CAbilityRoot);
    T_EQ(unit->ancient_root->transition_end_time, end_time);

    level.time = end_time - 1; ancient_update(unit);
    T_EQ(unit->ancient_root->mode, ANCIENT_UPROOTING);
    level.time = end_time; ancient_update(unit);
    T_EQ(unit->ancient_root->mode, ANCIENT_UPROOTED);
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
    end_time = unit->ancient_root->transition_end_time;
    T_EQ(end_time, G_Time() + 2250);
    T_ASSERT(!unit_issueimmediateorder(unit, "stop"));
    T_ASSERT(!G_IssueUnitPointOrder(unit, "move", &destination, false, 0, 0.0f));
    T_ASSERT(!G_IssueUnitTargetOrder(unit, "attack", enemy, false, 0));
    T_EQ(unit->currentmove->proc, CAbilityRoot);

    level.time = end_time - 1; ancient_update(unit);
    T_EQ(unit->ancient_root->mode, ANCIENT_ROOTING);
    level.time = end_time; ancient_update(unit);
    T_EQ(unit->ancient_root->mode, ANCIENT_ROOTED);
    T_ASSERT(G_UnitIsStructure(unit));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

static void ancient_assert_morph_save_restore(bool rooted) {
    cstring_t filename = rooted ?
        "/tmp/openwarcraft3-wc3-save-ancient-rooting.bin" :
        "/tmp/openwarcraft3-wc3-save-ancient-uprooting.bin";
    edict_t *unit, *goal;
    uint32_t end_time;

    reset_entities(); setup_test_world(); level.time = 1000;
    unit = ancient_test_unit(!rooted);
    goal = alloc_test_unit(TEST_HBAR, 96.0f, 64.0f);
    if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
    assert(unit->ancient_root);
    unit->ancient_root->destination = (vec2_t){ 320.0f, 192.0f };
    unit->ancient_root->approach_goal = goal;
    unit->ancient_root->approach_goal_spawn_time = goal->spawn_time;
    S_AncientBeginMorph(unit, rooted);
    end_time = unit->ancient_root->transition_end_time;

    T_ASSERT(WriteGame(filename));
    unit->ancient_root->mode = ANCIENT_ROOT_UNINITIALIZED;
    unit->ancient_root->approach_goal = NULL;
    unit->ancient_root->transition_end_time = 0;
    unit->currentmove = NULL;
    T_ASSERT(ReadGame(filename));

    T_EQ(unit->ancient_root->mode, rooted ? ANCIENT_ROOTING : ANCIENT_UPROOTING);
    T_EQ(unit->ancient_root->transition_end_time, end_time);
    T_ASSERT(unit->ancient_root->approach_goal == goal);
    T_EQ(unit->ancient_root->approach_goal_spawn_time, goal->spawn_time);
    T_FEQ(unit->ancient_root->destination.x, 320.0f, 0.001f);
    T_FEQ(unit->ancient_root->destination.y, 192.0f, 0.001f);
    T_ASSERT(unit->currentmove && unit->currentmove->proc == CAbilityRoot);

    level.time = end_time;
    ancient_update(unit);
    T_EQ(unit->ancient_root->mode, rooted ? ANCIENT_ROOTED : ANCIENT_UPROOTED);
    T_EQ(G_UnitIsStructure(unit), rooted);
    T_EQ(!!(unit->aiflags & AI_IMMOBILE), rooted);
    remove(filename);
}

TEST(wc3_ancient_root, root_and_uproot_morphs_resume_after_save_load) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);

    ancient_assert_morph_save_restore(true);
    ancient_assert_morph_save_restore(false);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_ancient_root, approaching_root_remains_interruptible) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *unit;

    reset_entities(); setup_test_world(); level.time = 1000;
    unit = ancient_test_unit(false);
    if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
    assert(unit->ancient_root);
    unit->ancient_root->mode = ANCIENT_ROOTING;
    unit->ancient_root->approaching = true;
    T_ASSERT(G_IssueUnitPointOrder(unit, "move", &MAKE(vec2_t, .x = 256, .y = 256), false, 0, 0.0f));
    T_EQ(unit->ancient_root->mode, ANCIENT_ROOTING);
    T_ASSERT(unit_issueimmediateorder(unit, "stop"));
    T_EQ(unit->ancient_root->mode, ANCIENT_UPROOTED);

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

static void ancient_begin_root_placement(edict_t *player, edict_t *unit) {
    abilityitem_t item = S_AbilityItem(TEST_AROO);
    abilityCall_t call = MAKE(abilityCall_t, .item = &item, .client = player);
    G_SelectEntity(player->client, unit);
    T_ASSERT(S_AbilityMessage(unit, A_COMMAND, &call));
    T_NOT_NULL(player->client->menu.on_location_selected);
}

TEST(wc3_ancient_root, command_rejects_blocked_placement_and_keeps_cursor_active) {
    edict_t *player, *unit, *blocker;
    vec2_t requested = { 320.0f, 320.0f }, snapped;
    gameClient_t *client = &game.clients[0];

    reset_entities(); setup_test_world();
    memset(client, 0, sizeof(*client));
    player = &g_edicts[0]; player->client = client;
    client->connected = true; client->ps.number = 0;
    unit = ancient_test_unit(false);
    unit->movetype = MOVETYPE_STEP;
    unit->collision = 16.0f;
    T_EQ(G_EvaluateRootPlacement(unit, &requested, &snapped), PLACE_OK);
    ancient_begin_root_placement(player, unit);
    blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), snapped.x, snapped.y);
    blocker->svflags |= SVF_MONSTER;
    blocker->movetype = MOVETYPE_STEP;
    blocker->collision = 16.0f;

    T_ASSERT(!client->menu.on_location_selected(player, &requested));
    T_EQ(unit->ancient_root->mode, ANCIENT_UPROOTED);
    T_ASSERT(client->menu.on_location_selected != NULL);
    player->client = NULL;
    memset(client, 0, sizeof(*client));
}

TEST(wc3_ancient_root, placement_order_walks_then_starts_root_morph_on_arrival) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *player, *unit;
    vec2_t requested = { 320.0f, 320.0f }, snapped;
    gameClient_t *client = &game.clients[0];

    reset_entities(); setup_test_world(); level.time = 1000;
    memset(client, 0, sizeof(*client));
    player = &g_edicts[0]; player->client = client;
    client->connected = true; client->ps.number = 0;
    unit = ancient_test_unit(false);
    unit->movetype = MOVETYPE_STEP;
    unit->collision = 16.0f;
    T_EQ(G_EvaluateRootPlacement(unit, &requested, &snapped), PLACE_OK);
    ancient_begin_root_placement(player, unit);
    T_ASSERT(client->menu.on_location_selected(player, &requested));
    T_EQ(unit->ancient_root->mode, ANCIENT_ROOTING);
    T_ASSERT(unit->ancient_root->approaching);
    T_EQ(unit->currentmove->proc, CAbilityMove);
    T_ASSERT(unit->goalentity == unit->ancient_root->approach_goal);
    T_EQ(unit->ancient_root->approach_goal_spawn_time, unit->goalentity->spawn_time);
    T_NULL(client->menu.on_location_selected);

    unit->s.origin2 = unit->ancient_root->destination;
    T_ASSERT(S_UnitAbilityMoveArrive(unit));
    T_EQ(unit->ancient_root->mode, ANCIENT_ROOTING);
    T_ASSERT(!unit->ancient_root || !unit->ancient_root->approaching);
    T_EQ(unit->currentmove->proc, CAbilityRoot);
    T_EQ(unit->ancient_root->transition_end_time, G_Time() + 2250);

    player->client = NULL;
    memset(client, 0, sizeof(*client));
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
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

    if (!unit->ancient_root) unit->ancient_root = G_AllocAncientRoot();
    assert(unit->ancient_root);
    unit->ancient_root->mode = ANCIENT_UPROOTED;
    unit->s.flags &= ~EF_BUILDING;
    unit->aiflags &= ~AI_IMMOBILE;
    T_ASSERT(S_AncientAbilityAvailable(unit, eat.ability));
    T_ASSERT(S_AbilityMessage(unit, A_VALIDATE, &call));

    unit->ancient_root->mode = ANCIENT_UPROOTING;
    T_ASSERT(!S_AncientAbilityAvailable(unit, eat.ability));
    T_ASSERT(!S_AbilityMessage(unit, A_VALIDATE, &call));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

TEST(wc3_ancient_root, spell_structure_filter_tracks_runtime_mode) {
    slkTestData_t *rows = parse_slk_string(ancient_root_tft);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *caster, *target, *corpse;
    UnitData_t corpse_data;

    reset_entities(); setup_test_world(); level.time = 1000;
    caster = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    caster->svflags |= SVF_MONSTER; caster->s.player = 0;
    target = ancient_test_unit(true);
    target->s.player = 1;
    target->targtype = TARG_STRUCTURE;
    T_ASSERT(G_UnitIsBuilding(target->class_id));
    T_ASSERT(G_UnitIsStructure(target));
    T_ASSERT(!S_SpellAllowsTarget(TEST_AHHB, caster, target));
    T_ASSERT(S_SpellAllowsTarget(MAKEFOURCC('A','H','s','t'), caster, target));

    if (!target->ancient_root) target->ancient_root = G_AllocAncientRoot();
    assert(target->ancient_root);
    target->ancient_root->mode = ANCIENT_UPROOTED;
    target->s.flags &= ~EF_BUILDING;
    target->aiflags &= ~AI_IMMOBILE;
    target->runtime.flags &= ~UNIT_BALANCE_BUILDING;
    T_ASSERT(G_UnitIsBuilding(target->class_id));
    T_ASSERT(!G_UnitIsStructure(target));
    T_ASSERT(S_SpellAllowsTarget(TEST_AHHB, caster, target));
    T_ASSERT(!S_SpellAllowsTarget(MAKEFOURCC('A','H','s','t'), caster, target));

    corpse = ancient_test_unit(false);
    corpse->s.player = 1;
    corpse->targtype = TARG_STRUCTURE;
    corpse->svflags |= SVF_DEADMONSTER;
    corpse->health.value = 0.0f;
    corpse_data = *corpse->data.UnitData;
    corpse_data.deathType |= UNIT_DEATH_TYPE_RAISE;
    corpse->data.UnitData = &corpse_data;
    T_ASSERT(!S_SpellAllowsCorpseTarget(MAKEFOURCC('A','H','s','t'), caster, corpse));
    if (!corpse->ancient_root) corpse->ancient_root = G_AllocAncientRoot();
    assert(corpse->ancient_root);
    corpse->ancient_root->mode = ANCIENT_ROOTED;
    corpse->s.flags |= EF_BUILDING;
    T_ASSERT(S_SpellAllowsCorpseTarget(MAKEFOURCC('A','H','s','t'), caster, corpse));

    G_SetSLKRows("AbilityData", old); free_slk_rows(rows);
}

#endif
