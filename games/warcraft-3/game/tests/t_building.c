#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../hud/hud_local.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void setup_test_world(void);
void repair_build_primary(LPEDICT ent, LPEDICT building);
BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location);
slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);

static DWORD building_stand_calls;
static uiFrame_t building_command_frame;
static BOOL building_command_frame_seen;
static uiFrame_t building_command_number_frame;
static uiLabel_t building_command_number_label;
static char building_command_number_text[16];
static BOOL building_command_number_seen;
static BOOL building_cursor_opcode_seen;
static BOOL building_cursor_clear_seen;

static void building_test_stand(LPEDICT ent) {
    (void)ent;
    building_stand_calls++;
}

static int building_test_image_index(LPCSTR name) {
    (void)name;
    return 1;
}

static void building_capture_write(pfWriteType_t type, void const *value) {
    if (!value) return;
    if (type == PF_UIFRAME) {
        uiFrame_t const *frame = value;
        if (frame->flags.type == FT_STRING && frame->text && frame->buffer.size == sizeof(uiLabel_t)) {
            building_command_number_frame = *frame;
            building_command_number_label = *(uiLabel_t const *)frame->buffer.data;
            snprintf(building_command_number_text, sizeof(building_command_number_text), "%s", frame->text);
            building_command_number_seen = true;
        } else {
            building_command_frame = *frame;
            building_command_frame_seen = true;
        }
        return;
    }
    if (type == PF_BYTE) {
        building_cursor_opcode_seen = *(LONG const *)value == svc_cursor;
        return;
    }
    if (type == PF_ENTITY && building_cursor_opcode_seen) {
        entityState_t const *cursor = value;
        building_cursor_clear_seen = cursor->model == 0;
        building_cursor_opcode_seen = false;
    }
}

static LPCSTR building_all_cvar(LPCSTR name, LPCSTR fallback) {
    return !strcmp(name, "wc3_build_all") ? "1" : fallback;
}

static const char building_repair_slk[] =
    "ID;PWXL;N;E\n"
    "B;X9;Y4;D0\n"
    "C;X1;Y1;K\"alias\"\n"
    "C;X2;K\"code\"\n"
    "C;X3;K\"DataA1\"\n"
    "C;X4;K\"DataB1\"\n"
    "C;X5;K\"DataC1\"\n"
    "C;X6;K\"DataD1\"\n"
    "C;X7;K\"DataE1\"\n"
    "C;X8;K\"Rng1\"\n"
    "C;X9;K\"targs1\"\n"
    "C;X1;Y2;K\"Arep\"\n"
    "C;X2;K\"Arep\"\n"
    "C;X3;K1\n"
    "C;X4;K1\n"
    "C;X5;K0.5\n"
    "C;X6;K0.5\n"
    "C;X7;K0\n"
    "C;X8;K128\n"
    "C;X9;K\"ground,structure,friend\"\n"
    "C;X1;Y3;K\"Aren\"\n"
    "C;X2;K\"Aren\"\n"
    "C;X3;K1\n"
    "C;X4;K2\n"
    "C;X5;K0\n"
    "C;X6;K0\n"
    "C;X7;K0\n"
    "C;X8;K128\n"
    "C;X9;K\"ground,structure,friend\"\n"
    "C;X1;Y4;K\"Arst\"\n"
    "C;X2;K\"Arst\"\n"
    "C;X3;K0.75\n"
    "C;X4;K1\n"
    "C;X5;K0\n"
    "C;X6;K0\n"
    "C;X7;K0\n"
    "C;X8;K96\n"
    "C;X9;K\"ground,structure,friend\"\n"
    "E\n";

static const char building_upgrade_slk[] =
    "ID;PWXL;N;E\n"
    "B;X13;Y7;D0\n"
    "C;X1;Y1;K\"upgradeid\"\n"
    "C;X2;K\"class\"\n"
    "C;X3;K\"maxlevel\"\n"
    "C;X4;K\"goldbase\"\n"
    "C;X5;K\"goldmod\"\n"
    "C;X6;K\"lumberbase\"\n"
    "C;X7;K\"lumbermod\"\n"
    "C;X8;K\"timebase\"\n"
    "C;X9;K\"timemod\"\n"
    "C;X10;K\"effect1\"\n"
    "C;X11;K\"base1\"\n"
    "C;X12;K\"mod1\"\n"
    "C;X13;K\"code1\"\n"
    "C;X1;Y2;K\"Rhme\"\n"
    "C;X2;K\"melee\"\n"
    "C;X3;K3\n"
    "C;X4;K100\n"
    "C;X5;K75\n"
    "C;X6;K50\n"
    "C;X7;K125\n"
    "C;X8;K60\n"
    "C;X9;K15\n"
    "C;X10;K\"ratd\"\n"
    "C;X11;K1\n"
    "C;X12;K1\n"
    "C;X13;K\"hfoo\"\n"
    "C;X1;Y3;K\"Rhar\"\n"
    "C;X2;K\"armor\"\n"
    "C;X3;K3\n"
    "C;X4;K125\n"
    "C;X5;K25\n"
    "C;X6;K75\n"
    "C;X7;K100\n"
    "C;X8;K60\n"
    "C;X9;K15\n"
    "C;X10;K\"rarm\"\n"
    "C;X11;K0\n"
    "C;X12;K0\n"
    "C;X1;Y4;K\"Rhra\"\n"
    "C;X2;K\"ranged\"\n"
    "C;X3;K3\n"
    "C;X10;K\"ratd\"\n"
    "C;X11;K1\n"
    "C;X12;K1\n"
    "C;X1;Y5;K\"Rhla\"\n"
    "C;X2;K\"armor\"\n"
    "C;X3;K3\n"
    "C;X10;K\"rarm\"\n"
    "C;X11;K0\n"
    "C;X12;K0\n"
    "C;X1;Y6;K\"Rhac\"\n"
    "C;X2;K\"armor\"\n"
    "C;X3;K3\n"
    "C;X10;K\"rarm\"\n"
    "C;X11;K0\n"
    "C;X12;K0\n"
    "C;X1;Y7;K\"Rhat\"\n"
    "C;X2;K\"melee\"\n"
    "C;X3;K3\n"
    "C;X10;K\"ratx\"\n"
    "C;X11;K2\n"
    "C;X12;K1\n"
    "E\n";

static slkTestData_t *building_install_upgrade_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(building_upgrade_slk);
    slkTestData_t *old = G_SetSLKRows("UpgradeData", rows);
    if (rows_out) *rows_out = rows;
    return old;
}

static void building_restore_upgrade_data(slkTestData_t *old, slkTestData_t *rows) {
    G_SetSLKRows("UpgradeData", old);
    free_slk_rows(rows);
}

static slkTestData_t *building_install_repair_data(slkTestData_t **rows_out) {
    slkTestData_t *rows = parse_slk_string(building_repair_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    if (rows_out) *rows_out = rows;
    return old;
}

static void building_restore_repair_data(slkTestData_t *old, slkTestData_t *rows) {
    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_building, player_tech_state_tracks_max_and_researched_levels) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    T_EQ(G_GetPlayerTechMaxAllowed(client, barracks), -1);
    T_EQ(G_GetPlayerTechResearchedLevel(client, barracks), 0);

    G_SetPlayerTechMaxAllowed(client, barracks, 2);
    G_SetPlayerTechResearched(client, barracks, 1);
    G_AddPlayerTechResearched(client, barracks, 2);

    T_EQ(G_GetPlayerTechMaxAllowed(client, barracks), 2);
    T_EQ(G_GetPlayerTechResearchedLevel(client, barracks), 3);
    T_ASSERT(client->commands_dirty);

    G_SetPlayerTechMaxAllowed(client, barracks, -1);
    T_EQ(G_GetPlayerTechMaxAllowed(client, barracks), -1);
}

TEST(wc3_building, research_state_uses_upgrade_cost_progression_and_player_lock) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    UnitProfile_t profile = { .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    DWORD const upgrade = MAKEFOURCC('R','h','m','e');
    LONG next_level = 0;
    char reason[128];

    memset(client->tech, 0, sizeof(client->tech));
    producer->data.UnitProfile = &profile;
    producer->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;

    T_EQ(G_GetResearchCommandState(client, producer, upgrade, &next_level, reason, sizeof(reason)),
         BUILD_COMMAND_AVAILABLE);
    T_EQ(next_level, 1);
    T_EQ(G_UpgradeGoldCost(upgrade, 1), 100);
    T_EQ(G_UpgradeLumberCost(upgrade, 1), 50);
    T_FEQ(G_UpgradeResearchTime(upgrade, 1), 60.0f, 0.001f);
    T_EQ(G_UpgradeData(upgrade)->effect[0], MAKEFOURCC('r','a','t','d'));
    T_EQ(G_UpgradeData(upgrade)->effectCode[0], MAKEFOURCC('h','f','o','o'));

    G_SetPlayerTechResearched(client, upgrade, 1);
    T_EQ(G_GetResearchCommandState(client, producer, upgrade, &next_level, reason, sizeof(reason)),
         BUILD_COMMAND_AVAILABLE);
    T_EQ(next_level, 2);
    T_EQ(G_UpgradeGoldCost(upgrade, 2), 175);
    T_EQ(G_UpgradeLumberCost(upgrade, 2), 175);
    T_FEQ(G_UpgradeResearchTime(upgrade, 2), 75.0f, 0.001f);

    G_AddPlayerTechInProgress(client, upgrade, 1);
    T_EQ(G_GetResearchCommandState(client, producer, upgrade, &next_level, reason, sizeof(reason)),
         BUILD_COMMAND_HIDDEN);
    G_AddPlayerTechInProgress(client, upgrade, -1);

    G_SetPlayerTechResearched(client, upgrade, 3);
    T_EQ(G_GetResearchCommandState(client, producer, upgrade, &next_level, reason, sizeof(reason)),
         BUILD_COMMAND_HIDDEN);

    G_SetPlayerTechResearched(client, upgrade, 0);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, research_tooltip_formats_next_level_resource_costs) {
    LPGAMECLIENT client = &game.clients[0];
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    DWORD const upgrade = MAKEFOURCC('R','h','m','e');
    char tooltip[1024];
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;

    memset(client->tech, 0, sizeof(client->tech));
    gi.ImageIndex = building_test_image_index;
    UI_SetCurrentClient(client);

    UI_FormatTooltip("Rhme", "Iron Forged Swords", "Upgrade melee damage.", 0,
                     tooltip, sizeof(tooltip));
    T_NOT_NULL(strstr(tooltip, "<Icon,1> 100   <Icon,1> 50   "));

    G_SetPlayerTechResearched(client, upgrade, 1);
    UI_FormatTooltip("Rhme", "Steel Forged Swords", "Upgrade melee damage.", 0,
                     tooltip, sizeof(tooltip));
    /* Level 2 is 175 gold / 175 lumber in the fixture; both values must
     * follow the requested player's researched level rather than UnitBalance
     * or a global default. */
    T_EQ(G_UpgradeLumberCost(upgrade, 2), 175);
    T_NOT_NULL(strstr(tooltip, "<Icon,1> 175   <Icon,1> 175   "));

    UI_SetCurrentClient(NULL);
    gi.ImageIndex = old_image_index;
    G_SetPlayerTechResearched(client, upgrade, 0);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, queued_research_charges_locks_and_cancel_refunds) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    UnitProfile_t profile = { .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    DWORD const upgrade = MAKEFOURCC('R','h','m','e');

    memset(client->tech, 0, sizeof(client->tech));
    producer->data.UnitProfile = &profile;
    producer->s.player = client->ps.number;
    producer->stand = building_test_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;

    T_ASSERT(G_QueueResearch(producer, upgrade));
    T_NOT_NULL(producer->build);
    T_ASSERT(producer->build->research.upgrade != 0);
    T_EQ(producer->build->research.upgrade, upgrade);
    T_EQ(producer->build->research.level, 1);
    T_EQ(G_GetPlayerTechInProgress(client, upgrade), 1);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 400);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 450);

    T_ASSERT(G_CancelTrainingQueueItem(producer, 0, true));
    T_NULL(producer->build);
    T_EQ(G_GetPlayerTechInProgress(client, upgrade), 0);
    T_EQ(G_GetPlayerTechResearchedLevel(client, upgrade), 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 500);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 500);

    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, researched_blacksmith_effects_update_existing_and_future_units) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT future;
    UnitBalance_t balance = {
        .upgrades = "Rhme,Rhar",
        .armorPerUpgrade = 2.0f
    };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    DWORD const weapon = MAKEFOURCC('R','h','m','e');
    DWORD const armor = MAKEFOURCC('R','h','a','r');

    memset(client->tech, 0, sizeof(client->tech));
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->attack1.numberOfDice = 2;
    unit->armor_value = 3.0f;

    G_SetPlayerTechResearched(client, weapon, 1);
    T_EQ(unit->attack1.numberOfDice, 3);
    G_SetPlayerTechResearched(client, weapon, 2);
    T_EQ(unit->attack1.numberOfDice, 4);

    G_SetPlayerTechResearched(client, armor, 1);
    T_FEQ(unit->armor_value, 5.0f, 0.001f);
    G_SetPlayerTechResearched(client, armor, 2);
    T_FEQ(unit->armor_value, 7.0f, 0.001f);

    future = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    future->s.player = client->ps.number;
    future->data.UnitBalance = &balance;
    future->attack1.numberOfDice = 2;
    future->armor_value = 3.0f;
    G_ApplyPlayerUpgradesToUnit(future);
    T_EQ(future->attack1.numberOfDice, 4);
    T_FEQ(future->armor_value, 7.0f, 0.001f);

    G_SetPlayerTechResearched(client, weapon, 0);
    G_SetPlayerTechResearched(client, armor, 0);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, researched_attack_damage_effect_tracks_level_delta) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    UnitBalance_t balance = { .upgrades = "Rhat" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    DWORD const attack_damage = MAKEFOURCC('R','h','a','t');

    memset(client->tech, 0, sizeof(client->tech));
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->attack1.numberOfDice = 1;
    unit->attack1.damageBase = 10;
    unit->attack2.numberOfDice = 1;
    unit->attack2.damageBase = 20;

    G_SetPlayerTechResearched(client, attack_damage, 1);
    T_EQ(unit->attack1.damageBase, 12);
    T_EQ(unit->attack2.damageBase, 22);
    T_FEQ(unit->attack1.permanentDamageBonus, 2.0f, 0.001f);
    T_FEQ(unit->attack2.permanentDamageBonus, 2.0f, 0.001f);

    G_SetPlayerTechResearched(client, attack_damage, 3);
    T_EQ(unit->attack1.damageBase, 14);
    T_EQ(unit->attack2.damageBase, 24);
    T_FEQ(unit->attack1.permanentDamageBonus, 4.0f, 0.001f);
    T_FEQ(unit->attack2.permanentDamageBonus, 4.0f, 0.001f);

    G_SetPlayerTechResearched(client, attack_damage, 0);
    T_EQ(unit->attack1.damageBase, 10);
    T_EQ(unit->attack2.damageBase, 20);
    T_FEQ(unit->attack1.permanentDamageBonus, 0.0f, 0.001f);
    T_FEQ(unit->attack2.permanentDamageBonus, 0.0f, 0.001f);

    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, status_upgrade_families_follow_unit_upgrades_used) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    LPEDICT rifleman = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 0, 0);
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    LPEDICT peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    UnitBalance_t footman_balance = { .upgrades = "Rhme,Rhar" };
    UnitBalance_t rifleman_balance = { .upgrades = "Rhla,Rhra,Rhri,Rhpm,Rguv" };
    UnitBalance_t building_balance = { .upgrades = "Rhac,Rgfo" };
    UnitBalance_t peasant_balance = { .upgrades = "Rhlh,Rguv" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    DWORD const melee = MAKEFOURCC('R','h','m','e');
    DWORD const heavy_armor = MAKEFOURCC('R','h','a','r');
    DWORD const ranged = MAKEFOURCC('R','h','r','a');
    DWORD const light_armor = MAKEFOURCC('R','h','l','a');
    DWORD const building_armor = MAKEFOURCC('R','h','a','c');

    memset(client->tech, 0, sizeof(client->tech));
    footman->data.UnitBalance = &footman_balance;
    rifleman->data.UnitBalance = &rifleman_balance;
    building->data.UnitBalance = &building_balance;
    peasant->data.UnitBalance = &peasant_balance;

    T_EQ(G_GetUnitUpgradeForClass(footman, "melee"), melee);
    T_EQ(G_GetUnitUpgradeForClass(footman, "armor"), heavy_armor);
    T_EQ(G_GetUnitUpgradeForClass(rifleman, "ranged"), ranged);
    T_EQ(G_GetUnitUpgradeForClass(rifleman, "armor"), light_armor);
    T_EQ(G_GetUnitUpgradeForClass(building, "armor"), building_armor);
    T_EQ(G_GetUnitUpgradeForClass(building, "melee"), 0);
    T_EQ(G_GetUnitUpgradeForClass(peasant, "melee"), 0);
    T_EQ(G_GetUnitUpgradeForClass(peasant, "armor"), 0);

    G_SetPlayerTechResearched(client, heavy_armor, 1);
    T_EQ(G_GetPlayerTechResearchedLevel(client, heavy_armor), 1);
    T_EQ(G_GetPlayerTechResearchedLevel(client, building_armor), 0);
    T_EQ(G_GetPlayerTechResearchedLevel(client, light_armor), 0);

    G_SetPlayerTechResearched(client, heavy_armor, 0);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, tech_count_includes_owned_structures_and_research) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    LPEDICT building = alloc_test_unit(barracks, 0, 0);

    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    G_SetPlayerTechResearched(client, barracks, 1);

    T_EQ(G_GetPlayerTechCountValue(client, barracks), 2);

    building->svflags |= SVF_DEADMONSTER;
    T_EQ(G_GetPlayerTechCountValue(client, barracks), 1);
}

TEST(wc3_building, building_charge_checks_and_deducts_resources) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    UnitBalance_t const *balance = G_UnitBalance(barracks);

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = balance->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = balance->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;

    T_ASSERT(G_ChargeBuilding(client, barracks));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 0);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 0);
}

TEST(wc3_building, build_command_state_covers_available_hidden_unaffordable_and_absent) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    UnitProfile_t worker_profile = { .builds = "hbar" };
    char reason[128];

    worker->data.UnitProfile = &worker_profile;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    G_SetPlayerTechMaxAllowed(client, barracks, 0);
    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_HIDDEN);

    memset(client->tech, 0, sizeof(client->tech));
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_UNAFFORDABLE);
    T_STREQ(reason, "Not enough gold");

    worker_profile.builds = "hfoo";
    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_ABSENT);
}

TEST(wc3_building, train_command_state_uses_trains_list_and_player_maximum) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    DWORD const trainee = MAKEFOURCC('u','0','0','1');
    UnitProfile_t producer_profile = { .trains = "u001" };
    char reason[128];

    producer->data.UnitProfile = &producer_profile;
    producer->s.player = client->ps.number;

    T_ASSERT(G_ProducerCanTrain(producer, trainee));
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    G_SetPlayerTechMaxAllowed(client, trainee, 0);
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_HIDDEN);

    G_SetPlayerTechMaxAllowed(client, trainee, -1);
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    producer_profile.trains = "u002";
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_ABSENT);
}

TEST(wc3_building, train_command_state_reports_food_shortage) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    DWORD const trainee = MAKEFOURCC('h','f','o','o');
    UnitBalance_t const *balance = G_UnitBalance(trainee);
    UnitProfile_t producer_profile = { .trains = "hfoo" };
    LPCSTR (*saved_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    char reason[128];

    producer->data.UnitProfile = &producer_profile;
    producer->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = balance->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = balance->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = MAX(0, balance->foodUsed - 1);
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED] = 0;
    gi.CvarString = building_all_cvar;

    T_ASSERT(balance->foodUsed > 0);
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_UNAFFORDABLE);
    T_STREQ(reason, "Not enough food");

    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = balance->foodUsed;
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    gi.CvarString = saved_cvar;
}

TEST(wc3_building, build_all_cvar_bypasses_training_tech_gates_but_not_trains_list) {
    LPCSTR (*old_cvar)(LPCSTR, LPCSTR) = gi.CvarString;
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    DWORD const trainee = MAKEFOURCC('u','0','0','1');
    UnitProfile_t producer_profile = { .trains = "u001" };

    producer->data.UnitProfile = &producer_profile;
    producer->s.player = client->ps.number;
    G_SetPlayerTechMaxAllowed(client, trainee, 0);

    gi.CvarString = building_all_cvar;
    T_EQ(G_GetTrainCommandState(client, producer, trainee, NULL, 0), BUILD_COMMAND_AVAILABLE);

    producer_profile.trains = "u002";
    T_EQ(G_GetTrainCommandState(client, producer, trainee, NULL, 0), BUILD_COMMAND_ABSENT);
    gi.CvarString = old_cvar;
}

TEST(wc3_building, queued_training_counts_against_player_tech_maximum) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    LPEDICT queued = alloc_test_unit(MAKEFOURCC('u','0','0','1'), 0, 0);
    DWORD const trainee = MAKEFOURCC('u','0','0','1');
    UnitProfile_t producer_profile = { .trains = "u001" };

    producer->data.UnitProfile = &producer_profile;
    producer->s.player = client->ps.number;
    queued->s.player = client->ps.number;
    queued->training = true;
    G_SetPlayerTechMaxAllowed(client, trainee, 1);

    T_EQ(G_GetPlayerTechCountValue(client, trainee), 1);
    T_EQ(G_GetTrainCommandState(client, producer, trainee, NULL, 0), BUILD_COMMAND_HIDDEN);
}

TEST(wc3_building, enable_user_ui_does_not_block_build_command_button) {
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    UnitProfile_t worker_profile = { .builds = "hbar" };
    LPCSTR button[] = { "button", "CmdBuild" };

    setup_test_world();
    worker->data.UnitProfile = &worker_profile;
    worker->s.player = client->ps.number;
    client->no_ui = true;
    client->menu.cmdbutton = NULL;
    G_SelectEntity(client, worker);

    G_ClientCommand(clent, 2, button);

    T_NOT_NULL(client->menu.cmdbutton);
    client->no_ui = false;
}

TEST(wc3_building, disabled_command_button_is_inert_and_available_button_is_clickable) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    gameCommandButton_t button;

    memset(&button, 0, sizeof(button));
    snprintf(button.command, sizeof(button.command), "CmdBuild");
    snprintf(button.art, sizeof(button.art), "test");
    button.hotkey = 'B';
    button.x = 0;
    button.y = 0;

    gi.Write = building_capture_write;
    gi.ImageIndex = building_test_image_index;

    building_command_frame_seen = false;
    button.disabled = 1;
    UI_WriteCommandButtonFrame(&button);
    T_ASSERT(building_command_frame_seen);
    T_EQ(building_command_frame.flags.type, FT_COMMANDBUTTON);
    T_EQ(building_command_frame.hotkey, 0);
    T_NULL(building_command_frame.onclick);
    T_EQ(building_command_frame.color.r, 128);
    T_EQ(building_command_frame.color.g, 128);
    T_EQ(building_command_frame.color.b, 128);

    building_command_frame_seen = false;
    button.disabled = 0;
    UI_WriteCommandButtonFrame(&button);
    T_ASSERT(building_command_frame_seen);
    T_EQ(building_command_frame.hotkey, 'B');
    T_NOT_NULL(building_command_frame.onclick);

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, command_button_serializes_secondary_autocast_command_and_state) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    gameCommandButton_t button;

    memset(&button, 0, sizeof(button));
    snprintf(button.command, sizeof(button.command), "Arep");
    snprintf(button.alternate, sizeof(button.alternate), "autocast Arep");
    snprintf(button.art, sizeof(button.art), "test");
    button.alternate_active = 1;

    gi.Write = building_capture_write;
    gi.ImageIndex = building_test_image_index;
    building_command_frame_seen = false;

    UI_WriteCommandButtonFrame(&button);

    T_ASSERT(building_command_frame_seen);
    T_STREQ(building_command_frame.onclick, "button Arep");
    T_STREQ(building_command_frame.text, "autocast Arep");
    T_ASSERT(building_command_frame.flagsvalue & UIFLAG_ALTERNATE_ACTIVE);

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}


TEST(wc3_building, command_button_geometry_matches_warcraft_grid) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    gameCommandButton_t button;

    memset(&button, 0, sizeof(button));
    snprintf(button.command, sizeof(button.command), "CmdBuild");
    snprintf(button.art, sizeof(button.art), "test");
    button.x = 3;
    button.y = 2;

    gi.Write = building_capture_write;
    gi.ImageIndex = building_test_image_index;
    building_command_frame_seen = false;

    UI_WriteCommandButtonFrame(&button);

    T_ASSERT(building_command_frame_seen);
    T_FEQ((FLOAT)building_command_frame.points.x[FPP_MIN].offset / UI_FRAMEPOINT_SCALE,
          0.7477f, 0.0001f);
    T_FEQ(-(FLOAT)building_command_frame.points.y[FPP_MIN].offset / UI_FRAMEPOINT_SCALE,
          0.5540f, 0.0001f);
    T_FEQ(building_command_frame.size.width, 0.039f, 0.0001f);
    T_FEQ(building_command_frame.size.height, 0.039f, 0.0001f);

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, command_button_number_draws_bottom_right_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    gameCommandButton_t button;

    memset(&button, 0, sizeof(button));
    snprintf(button.command, sizeof(button.command), "CmdSelectSkill");
    snprintf(button.art, sizeof(button.art), "test");
    button.x = 0;
    button.y = 0;
    button.number = 1;

    gi.Write = building_capture_write;
    gi.ImageIndex = building_test_image_index;
    building_command_frame_seen = false;
    building_command_number_seen = false;
    building_command_number_text[0] = '\0';

    UI_WriteCommandButtonFrame(&button);

    T_ASSERT(building_command_frame_seen);
    T_ASSERT(building_command_number_seen);
    T_EQ(building_command_number_frame.flags.type, FT_STRING);
    T_STREQ(building_command_number_text, "1");
    T_EQ(building_command_number_label.textalignx, FONT_JUSTIFYRIGHT);
    T_EQ(building_command_number_label.textaligny, FONT_JUSTIFYBOTTOM);

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, tech_state_capacity_is_bounded_without_clobbering_existing_entries) {
    LPGAMECLIENT client = &game.clients[0];

    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        DWORD const techid = 0x41000000u + i + 1;
        G_SetPlayerTechMaxAllowed(client, techid, (LONG)i);
    }
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        DWORD const techid = 0x41000000u + i + 1;
        T_EQ(G_GetPlayerTechMaxAllowed(client, techid), (LONG)i);
    }

    G_SetPlayerTechMaxAllowed(client, 0x42000001u, 7);
    T_EQ(G_GetPlayerTechMaxAllowed(client, 0x42000001u), -1);
    T_EQ(G_GetPlayerTechMaxAllowed(client, 0x41000001u), 0);
}

TEST(wc3_building, building_charge_rejects_short_gold_and_refund_restores_resources) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const barracks = MAKEFOURCC('h','b','a','r');
    UnitBalance_t const *balance = G_UnitBalance(barracks);

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = MAX(0, balance->goldCost - 1);
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = balance->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    T_ASSERT(!G_ChargeBuilding(client, barracks));

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = balance->goldCost;
    T_ASSERT(G_ChargeBuilding(client, barracks));
    G_RefundBuilding(client, barracks);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], balance->goldCost);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], balance->lumberCost);
}

TEST(wc3_building, placement_accepts_open_ground_rejects_live_unit_and_map_edge) {
    LPEDICT builder;
    LPEDICT blocker;
    VECTOR2 requested = { 64.0f, 64.0f };
    VECTOR2 snapped;
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128, -128);

    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_OK);

    blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), snapped.x, snapped.y);
    blocker->svflags |= SVF_MONSTER;
    blocker->collision = 16.0f;
    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_UNIT_BLOCKED);

    requested = (VECTOR2){ 100000.0f, 100000.0f };
    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_OUT_OF_BOUNDS);
}

TEST(wc3_building, placement_flags_treat_slk_sentinel_as_empty) {
    T_EQ(G_PlacementFlags(NULL), 0);
    T_EQ(G_PlacementFlags("_"), 0);
    T_ASSERT(G_PlacementFlags("unwalkable") & WC3_PATH_UNWALKABLE);
}

TEST(wc3_building, shared_build_order_uses_authoritative_validation) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder;
    UnitProfile_t profile = { .builds = "hbar" };
    VECTOR2 point = { 64.0f, 64.0f };
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128, -128);
    builder->s.player = client->ps.number; builder->data.UnitProfile = &profile;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_ASSERT(G_IssueBuildOrder(builder, barracks, &point));
    T_EQ(builder->build_project, barracks); T_NOT_NULL(builder->goalentity);
    T_ASSERT(!G_IssueBuildOrder(builder, MAKEFOURCC('h','f','o','o'), &point));
}

TEST(wc3_building, shared_build_order_releases_builder_from_gold_mine) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder, mine;
    UnitProfile_t profile = { .builds = "hbar" };
    VECTOR2 point = { 64.0f, 64.0f };
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128, -128);
    mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -64, -64);
    builder->s.player = client->ps.number; builder->data.UnitProfile = &profile;
    builder->goldmine.mine = mine; builder->goldmine.mine_spawn_time = mine->spawn_time;
    builder->invulnerable = true; builder->s.renderfx |= RF_HIDDEN; mine->peonsinside = 1;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_ASSERT(G_IssueBuildOrder(builder, barracks, &point));
    T_ASSERT(!S_GoldMineWorkerIsInside(builder)); T_EQ(mine->peonsinside, 0);
    T_ASSERT(!(builder->s.renderfx & RF_HIDDEN)); T_ASSERT(!builder->invulnerable);
    T_EQ(builder->build_project, barracks); T_NOT_NULL(builder->goalentity);
}

TEST(wc3_building, building_snap_without_authored_pathing_uses_32_unit_grid) {
    VECTOR2 point = { 47.0f, 79.0f };

    G_SnapBuildingPoint(MAKEFOURCC('h','p','e','a'), &point);

    T_FEQ(point.x, 32.0f, 0.001f);
    T_FEQ(point.y, 64.0f, 0.001f);
}

TEST(wc3_building, human_construction_start_sets_explicit_state_and_start_life) {
    LPEDICT builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);

    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;

    T_ASSERT(!G_StartHumanConstruction(builder, builder));
    T_ASSERT(G_StartHumanConstruction(builder, building));
    T_ASSERT(building->construction.active);
    T_ASSERT(building->construction.paused);
    T_ASSERT(building->construction.primary_builder == builder);
    T_FEQ(building->construction.progress, 0.0f, 0.001f);
    T_ASSERT(!building->construction.paid);
    T_EQ(building->construction.payer, 0);
    T_EQ(building->construction.gold, 0);
    T_EQ(building->construction.lumber, 0);
    T_ASSERT(building->aiflags & AI_HOLD_FRAME);
    T_FEQ(building->health.value, 100.0f, 0.001f);
}

TEST(wc3_building, cancel_build_command_resolves_to_shared_cancel_handler) {
    T_ASSERT(FindAbilityForCommand(STR_CmdCancelBuild) == FindAbilityForCommand(STR_CmdCancel));
}

TEST(wc3_building, replacing_pre_spawn_build_order_clears_project) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder;
    UnitProfile_t profile = { .builds = "hbar" };
    VECTOR2 point = { 64.0f, 64.0f };
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128, -128);
    builder->s.player = client->ps.number;
    builder->data.UnitProfile = &profile;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_ASSERT(G_IssueBuildOrder(builder, barracks, &point));
    T_EQ(builder->build_project, barracks);
    unit_stand(builder);
    T_EQ(builder->build_project, 0);
}

TEST(wc3_building, cancel_human_construction_refunds_releases_and_publishes) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    UnitBalance_t balance;
    slkTestData_t *rows, *old_abilities;
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(barracks, 64, 0);
    builder->data.UnitAbilities = &abilities;
    builder->stand = unit_stand;
    builder->collision = 16.0f;
    builder->s.player = client->ps.number;
    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    building->stand = unit_stand;
    balance = *building->data.UnitBalance;
    balance.goldCost = 100;
    balance.lumberCost = 80;
    balance.foodUsed = 2;
    building->data.UnitBalance = &balance;
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;

    T_ASSERT(G_StartHumanConstruction(builder, building));
    building->construction.paid = true;
    building->construction.payer = client->ps.number;
    building->construction.gold = balance.goldCost;
    building->construction.lumber = balance.lumberCost;
    building->build = building;
    G_SetUnitFoodUsed(building, balance.foodUsed);
    repair_build_primary(builder, building);
    T_ASSERT(builder->build == building);
    T_EQ(G_GetPlayerTechCountValue(client, barracks), 1);

    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;
    level.events.read = level.events.write = 0;

    T_ASSERT(G_CancelStructureConstruction(building));

    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 75);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 60);
    T_ASSERT(building->svflags & SVF_DEADMONSTER);
    T_ASSERT(!building->construction.active);
    T_NULL(building->construction.primary_builder);
    T_NULL(building->build);
    T_NULL(builder->build);
    T_EQ(builder->buildwork.ability, 0);
    T_EQ(building->food.used, 0);
    T_EQ(G_GetPlayerTechCountValue(client, barracks), 0);
    T_EQ(level.events.write, 4);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_CONSTRUCT_CANCEL);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_CONSTRUCT_CANCEL);
    T_EQ(level.events.queue[2].type, EVENT_UNIT_DEATH);
    T_EQ(level.events.queue[3].type, EVENT_PLAYER_UNIT_DEATH);
    T_ASSERT(level.events.queue[0].edict == building);
    T_ASSERT(!G_CancelStructureConstruction(building));
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 75);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 60);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, cancel_human_construction_without_payment_does_not_refund) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder;
    LPEDICT building;

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    builder->s.player = client->ps.number;
    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    building->stand = unit_stand;
    T_ASSERT(G_StartHumanConstruction(builder, building));
    building->build = building;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 7;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 9;

    T_ASSERT(G_CancelStructureConstruction(building));

    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 7);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 9);
    T_ASSERT(building->svflags & SVF_DEADMONSTER);
}

TEST(wc3_building, cancel_command_cancels_selected_spawned_construction) {
    LPEDICT clent;
    LPGAMECLIENT client;
    LPEDICT building;
    BOOL was_connected;

    setup_test_world();
    clent = &g_edicts[0];
    client = clent->client;
    was_connected = client->connected;
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    building->stand = unit_stand;
    building->construction.active = true;
    building->construction.paid = true;
    building->construction.payer = client->ps.number;
    building->construction.gold = 100;
    building->construction.lumber = 80;
    building->build = building;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;
    G_SelectEntity(client, building);
    client->connected = false;

    CMD_CancelCommand(clent);

    T_ASSERT(building->svflags & SVF_DEADMONSTER);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 75);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 60);
    client->connected = was_connected;
}

TEST(wc3_building, dead_building_releases_baked_static_pathing) {
    LPEDICT building;
    pathTex_t *pathtex;
    VECTOR2 point = { 0.0f, 0.0f };
    size_t const pathtex_size = sizeof(*pathtex) + sizeof(COLOR32);

    setup_test_world();
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), point.x, point.y);
    building->svflags |= SVF_MONSTER;
    building->stand = unit_stand;
    pathtex = gi.MemAlloc(pathtex_size);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = 1;
    pathtex->height = 1;
    pathtex->map[0].b = 0xff;
    building->pathtex = pathtex;
    gi.LinkEntity(building);
    CM_BakeStaticObstacles();

    T_ASSERT(!CM_PointIsPathableForRadius(&point, 0.0f));
    unit_die(building, NULL);
    T_ASSERT(CM_PointIsPathableForRadius(&point, 0.0f));

    building->pathtex = NULL;
    gi.MemFree(pathtex);
}

TEST(wc3_building, completing_construction_clears_state_publishes_once_and_grants_food_once) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    UnitBalance_t balance = *building->data.UnitBalance;

    balance.foodMade = 6;
    building->data.UnitBalance = &balance;
    building->s.player = client->ps.number;
    building->health.max_value = 1000.0f;
    building->health.value = 400.0f;
    building->construction.active = true;
    building->construction.paused = true;
    building->construction.primary_builder = builder;
    building->construction.progress = 500.0f;
    building->construction.paid = true;
    building->construction.payer = client->ps.number;
    building->construction.gold = 100;
    building->construction.lumber = 50;
    building->aiflags |= AI_HOLD_FRAME;
    building->stand = building_test_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    level.events.write = 0;
    level.events.read = 0;
    building_stand_calls = 0;

    {
        /* Prevent G_CompleteConstruction from invoking HUD refresh (which
         * requires FDF/UI state unavailable in tests) by hiding the player
         * entity from G_GetPlayerEntityByNumber while the call runs. */
        LPGAMECLIENT saved_client = g_edicts[0].client;
        g_edicts[0].client = NULL;
        G_CompleteConstruction(building);
        g_edicts[0].client = saved_client;
    }

    T_ASSERT(!building->construction.active);
    T_ASSERT(!building->construction.paused);
    T_NULL(building->construction.primary_builder);
    T_FEQ(building->construction.progress, 0.0f, 0.001f);
    T_ASSERT(!building->construction.paid);
    T_EQ(building->construction.gold, 0);
    T_EQ(building->construction.lumber, 0);
    T_ASSERT(!(building->aiflags & AI_HOLD_FRAME));
    T_FEQ(building->health.value, building->health.max_value, 0.001f);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 16);
    T_EQ(building->food.made, 6);
    T_EQ(building_stand_calls, 1);
    T_ASSERT(building->sound.owner_pending != 0);
    T_EQ(level.events.write, 1);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_CONSTRUCT_FINISH);
    T_ASSERT(level.events.queue[0].edict == building);

    G_CompleteConstruction(building);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 16);
    T_EQ(building_stand_calls, 1);
    T_EQ(level.events.write, 1);
}

TEST(wc3_building, plain_build_error_text_is_not_resolved_as_trigger_string_zero) {
    mapTrigStr_t zero = { .id = 0 };

    snprintf(zero.text, sizeof(zero.text), "Human02");
    ((LPMAPINFO)level.mapinfo)->strings = &zero;

    T_STREQ(G_LevelString("Unable to build there."), "Unable to build there.");
    T_STREQ(G_LevelString("TRIGSTR_0"), "Human02");
    T_STREQ(G_LevelString("TRIGSTR_bad"), "TRIGSTR_bad");
}

TEST(wc3_building, human_repair_capability_comes_from_unit_ability_list) {
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    UnitAbilities_t human_repair = { .abilList = "Arep" };
    UnitAbilities_t generic_repair = { .abilList = "Aren" };

    worker->data.UnitAbilities = &human_repair;
    T_ASSERT(G_UnitHasHumanRepair(worker));

    worker->data.UnitAbilities = &generic_repair;
    T_ASSERT(!G_UnitHasHumanRepair(worker));

    worker->data.UnitAbilities = NULL;
    T_ASSERT(!G_UnitHasHumanRepair(worker));
}

TEST(wc3_building, human_builder_exit_is_outside_baked_building_footprint) {
    enum { FOOTPRINT_W = 10, FOOTPRINT_H = 10 };
    LPEDICT builder;
    LPEDICT building;
    pathTex_t *pathtex;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    slkTestData_t *rows, *old_abilities;
    size_t const pathtex_size = sizeof(*pathtex) +
                                FOOTPRINT_W * FOOTPRINT_H * sizeof(COLOR32);

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    builder->data.UnitAbilities = &abilities;
    builder->collision = 16.0f;
    builder->s.model = 1;
    building->s.model = 1;
    building->movetype = MOVETYPE_NONE;
    building->svflags |= SVF_MONSTER;
    building->health.max_value = 1200.0f;
    building->health.value = 1200.0f;
    T_ASSERT(G_StartHumanConstruction(builder, building));

    pathtex = gi.MemAlloc(pathtex_size);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = FOOTPRINT_W;
    pathtex->height = FOOTPRINT_H;
    FOR_LOOP(i, FOOTPRINT_W * FOOTPRINT_H) pathtex->map[i].b = 0xff;
    building->pathtex = pathtex;

    gi.LinkEntity(builder);
    gi.LinkEntity(building);
    CM_BakeStaticObstacles();

    repair_build_primary(builder, building);

    T_ASSERT(builder->build == building);
    T_ASSERT(CM_PointIsPathableForRadius(&builder->s.origin2, builder->collision));
    T_ASSERT(fabsf(builder->s.origin2.x - building->s.origin2.x) >= 160.0f ||
             fabsf(builder->s.origin2.y - building->s.origin2.y) >= 160.0f);

    building->pathtex = NULL;
    gi.MemFree(pathtex);
    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, completed_repair_uses_repair_time_ratios_and_fractional_costs) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    UnitBalance_t balance;
    LPGAMECLIENT saved_client;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    worker->s.player = client->ps.number;
    building->s.player = client->ps.number;
    balance = *building->data.UnitBalance;
    balance.reptm = 10;
    balance.buildTime = 100;
    balance.goldRep = 5;
    balance.lumberRep = 3;
    building->data.UnitBalance = &balance;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    building->svflags |= SVF_MONSTER;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100;

    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    T_NOT_NULL(worker->currentmove);
    T_STREQ(worker->currentmove->animation, "stand work");

    saved_client = g_edicts[0].client;
    g_edicts[0].client = NULL;
    FOR_LOOP(i, 20) worker->currentmove->think(worker);
    g_edicts[0].client = saved_client;

    /* Aren fixture: DataA=1, DataB=2, reptm=10. Over two seconds the
     * building gains 400 HP. Costs accumulate at 1 gold/sec and 0.6
     * lumber/sec, proving sub-unit tick costs are retained. buildTime=100
     * also proves reptm wins as the completed-repair duration source. */
    T_FEQ(building->health.value, 900.0f, 0.001f);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 98);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 99);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_order_walks_to_remote_target_without_teleporting) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    VECTOR2 start;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -256, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 256, 0);
    worker->data.UnitAbilities = &abilities;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    building->svflags |= SVF_MONSTER;
    building->s.player = worker->s.player;
    start = worker->s.origin2;

    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    T_FEQ(worker->s.origin2.x, start.x, 0.001f);
    T_FEQ(worker->s.origin2.y, start.y, 0.001f);
    T_ASSERT(worker->goalentity == building);
    T_STREQ(worker->currentmove->animation, "walk");

    unit_stand(worker);
    T_EQ(worker->buildwork.ability, 0);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_button_then_target_issues_repair_order) {
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    slkTestData_t *rows, *old_abilities;
    char target_number[16];
    LPCSTR button[] = { "button", "Arep" };
    LPCSTR select_target[] = { "select", target_number };

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->svflags |= SVF_MONSTER;
    building->targtype = TARG_STRUCTURE;
    worker->s.player = client->ps.number;
    building->s.player = client->ps.number;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    snprintf(target_number, sizeof(target_number), "%u", (unsigned)building->s.number);
    G_SelectEntity(client, worker);

    G_ClientCommand(clent, 2, button);
    T_NOT_NULL(client->menu.on_entity_selected);
    T_EQ(client->menu.ability_code, MAKEFOURCC('A','r','e','p'));
    T_ASSERT(client->menu.supports_order_queue);

    G_ClientCommand(clent, 2, select_target);

    T_ASSERT(worker->build == building);
    T_EQ(worker->buildwork.ability, MAKEFOURCC('A','r','e','p'));
    T_NOT_NULL(worker->currentmove);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_autocast_toggle_is_unit_state) {
    LPEDICT worker;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    worker->data.UnitAbilities = &abilities;
    repair = FindAbilityForCommand("Aren");

    T_NOT_NULL(repair);
    T_ASSERT(!G_UnitAutocastIsOn(worker, repair));
    T_ASSERT(G_SetUnitAutocast(worker, repair, true));
    T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
    T_ASSERT(worker->aiflags & AI_AUTOCAST_ACTIVE);
    T_ASSERT(G_UnitAutocastIsOn(worker, repair));
    T_ASSERT(G_SetUnitAutocast(worker, repair, false));
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_REPAIR));
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_ACTIVE));
    T_ASSERT(!G_UnitAutocastIsOn(worker, repair));

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repairon_and_repairoff_immediate_orders_toggle_without_starting_repair) {
    LPEDICT worker;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    worker->data.UnitAbilities = &abilities;
    repair = FindAbilityForCommand("Aren");

    T_NOT_NULL(repair);
    T_ASSERT(unit_issueimmediateorder(worker, "repairon"));
    T_ASSERT(G_UnitAutocastIsOn(worker, repair));
    T_NULL(worker->build);
    T_ASSERT(unit_issueimmediateorder(worker, "repairoff"));
    T_ASSERT(!G_UnitAutocastIsOn(worker, repair));
    T_NULL(worker->build);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_command_button_exposes_autocast_secondary_command) {
    LPEDICT worker;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    gameCommandButton_t button;
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    worker->data.UnitAbilities = &abilities;
    repair = FindAbilityForCommand("Arep");

    T_NOT_NULL(repair);
    T_ASSERT(G_BuildCommandButton(worker, "Arep", false, 0, &button));
    T_STREQ(button.alternate, "autocast Arep");
    T_EQ(button.alternate_active, 0);

    T_ASSERT(G_SetUnitAutocast(worker, repair, true));
    T_ASSERT(G_BuildCommandButton(worker, "Arep", false, 0, &button));
    T_STREQ(button.alternate, "autocast Arep");
    T_EQ(button.alternate_active, 1);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_autocast_chooses_nearest_valid_damaged_building) {
    LPEDICT worker;
    LPEDICT near_building;
    LPEDICT far_building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    near_building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 96, 0);
    far_building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 256, 0);
    worker->data.UnitAbilities = &abilities;
    worker->runtime.acquisition_range = 400.0f;
    worker->collision = 16.0f;
    near_building->collision = far_building->collision = 32.0f;
    near_building->s.player = far_building->s.player = worker->s.player;
    near_building->health.max_value = far_building->health.max_value = 1000.0f;
    near_building->health.value = 900.0f;
    far_building->health.value = 100.0f;
    gi.LinkEntity(worker);
    gi.LinkEntity(near_building);
    gi.LinkEntity(far_building);
    repair = FindAbilityForCommand("Aren");

    T_NOT_NULL(repair);
    T_ASSERT(G_SetUnitAutocast(worker, repair, true));
    T_ASSERT(G_TryUnitAutocast(worker));
    T_ASSERT(worker->build == near_building);
    T_ASSERT(worker->build != far_building);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_autocast_uses_collision_aware_nearest_valid_distance) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 410, 0);
    worker->data.UnitAbilities = &abilities;
    worker->runtime.acquisition_range = 400.0f;
    worker->collision = 16.0f;
    building->collision = 64.0f;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    gi.LinkEntity(worker);
    gi.LinkEntity(building);
    repair = FindAbilityForCommand("Aren");

    T_NOT_NULL(repair);
    T_ASSERT(G_SetUnitAutocast(worker, repair, true));
    /* Center distance is 410 (> uacq 400), but edge distance is only 330.
     * Warsmash expands from the caster collision rectangle and compares unit
     * edge distance, so the nearby building remains a valid acquisition. */
    T_ASSERT(G_TryUnitAutocast(worker));
    T_ASSERT(worker->build == building);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, moving_away_while_repairing_preserves_replacement_goal) {
    LPEDICT worker;
    LPEDICT building;
    LPEDICT waypoint;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;
    VECTOR2 destination = { 512.0f, 0.0f };

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    worker->stand = unit_stand;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    gi.LinkEntity(worker);
    gi.LinkEntity(building);
    repair = FindAbilityForCommand("Aren");

    T_NOT_NULL(repair);
    T_ASSERT(G_SetUnitAutocast(worker, repair, true));
    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    T_ASSERT(worker->build == building);
    T_NE(worker->buildwork.ability, 0);

    waypoint = Waypoint_add(&destination);
    T_NOT_NULL(waypoint);
    order_move(worker, waypoint);

    T_ASSERT(worker->goalentity == waypoint);
    T_NULL(worker->build);
    T_EQ(worker->buildwork.ability, 0);
    T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
    T_ASSERT(worker->aiflags & AI_AUTOCAST_ACTIVE);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, idle_acquisition_prefers_auto_repair_over_auto_attack) {
    LPEDICT worker;
    LPEDICT building;
    LPEDICT enemy;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;
    DWORD stagger;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 160, 0);
    enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    worker->svflags |= SVF_MONSTER;
    worker->runtime.acquisition_range = 400.0f;
    worker->attack1.cooldown = 1.0f;
    worker->attack1.damageBase = 1;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    enemy->s.player = 1;
    enemy->svflags |= SVF_MONSTER;
    gi.LinkEntity(worker);
    gi.LinkEntity(building);
    gi.LinkEntity(enemy);
    repair = FindAbilityForCommand("Aren");

    T_NOT_NULL(repair);
    T_ASSERT(G_SetUnitAutocast(worker, repair, true));
    stagger = (DWORD)(worker - g_edicts) % 300;
    level.time = (300 - stagger) % 300;
    ai_stand(worker);

    T_ASSERT(worker->build == building);
    T_ASSERT(worker->goalentity == building);
    T_ASSERT(worker->goalentity != enemy);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, idle_acquisition_without_autocast_still_auto_attacks) {
    LPEDICT worker;
    LPEDICT enemy;
    DWORD stagger;

    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
    worker->svflags |= SVF_MONSTER;
    worker->runtime.acquisition_range = 400.0f;
    worker->attack1.cooldown = 1.0f;
    worker->attack1.damageBase = 1;
    enemy->s.player = 1;
    enemy->svflags |= SVF_MONSTER;
    gi.LinkEntity(worker);
    gi.LinkEntity(enemy);

    stagger = (DWORD)(worker - g_edicts) % 300;
    level.time = (300 - stagger) % 300;
    ai_stand(worker);

    T_ASSERT(worker->goalentity == enemy);
}

TEST(wc3_building, repair_autocast_ignores_full_health_nearer_building) {
    LPEDICT worker;
    LPEDICT full_building;
    LPEDICT damaged_building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    full_building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    damaged_building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 192, 0);
    worker->data.UnitAbilities = &abilities;
    worker->runtime.acquisition_range = 400.0f;
    worker->collision = 16.0f;
    full_building->collision = damaged_building->collision = 32.0f;
    full_building->s.player = damaged_building->s.player = worker->s.player;
    full_building->health.max_value = full_building->health.value = 1000.0f;
    damaged_building->health.max_value = 1000.0f;
    damaged_building->health.value = 500.0f;
    gi.LinkEntity(worker);
    gi.LinkEntity(full_building);
    gi.LinkEntity(damaged_building);
    repair = FindAbilityForCommand("Aren");

    T_NOT_NULL(repair);
    T_ASSERT(G_SetUnitAutocast(worker, repair, true));
    T_ASSERT(G_TryUnitAutocast(worker));
    T_ASSERT(worker->build == damaged_building);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, normal_target_order_routes_repair_through_repair_behavior) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;

    T_ASSERT(G_IssueUnitTargetOrder(worker, "repair", building, false, worker->s.player));
    T_ASSERT(worker->build == building);
    T_EQ(worker->buildwork.ability, MAKEFOURCC('A','r','e','n'));

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, smart_order_repairs_damaged_owned_building) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->svflags |= SVF_MONSTER;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;

    T_ASSERT(unit_issuetargetorder(worker, "smart", building));
    T_NE(worker->buildwork.ability, 0);
    T_ASSERT(worker->build == building);
    T_STREQ(worker->currentmove->animation, "stand work");

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_stops_and_releases_state_when_target_dies) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    worker->stand = unit_stand;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->svflags |= SVF_MONSTER;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;

    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    building->health.value = 0.0f;
    worker->currentmove->think(worker);

    T_EQ(worker->buildwork.ability, 0);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, standard_repair_rejects_construction_and_human_requires_paused_state) {
    LPEDICT human;
    LPEDICT standard;
    LPEDICT building;
    UnitAbilities_t human_abilities = { .abilList = "Arep" };
    UnitAbilities_t standard_abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    human = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    standard = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 32);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    human->data.UnitAbilities = &human_abilities;
    standard->data.UnitAbilities = &standard_abilities;
    human->s.player = 0;
    standard->s.player = 0;
    building->s.player = 0;
    building->health.max_value = 1000.0f;
    building->health.value = 100.0f;
    building->svflags |= SVF_MONSTER;
    building->construction.active = true;
    building->construction.paused = true;

    T_ASSERT(!S_OrderRepair(standard, building, MAKEFOURCC('A','r','e','n')));
    building->construction.paused = false;
    T_ASSERT(!S_OrderRepair(human, building, MAKEFOURCC('A','r','e','p')));

    building_restore_repair_data(old_abilities, rows);
}


TEST(wc3_building, cancel_command_clears_active_build_placement_cursor) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;

    client->menu.on_location_selected = build_menu_send_builder;
    clent->build_project = MAKEFOURCC('h','b','a','r');
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;

    CMD_CancelCommand(clent);

    T_EQ(clent->build_project, 0);
    T_NULL(client->menu.on_location_selected);
    T_ASSERT(building_cursor_clear_seen);

    gi.Write = old_write;
}

TEST(wc3_building, smartpoint_cancels_build_placement_without_moving_selected_worker) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPCSTR command[] = { "smartpoint", "256", "256" };

    G_SelectEntity(client, worker);
    client->menu.on_location_selected = build_menu_send_builder;
    clent->build_project = MAKEFOURCC('h','b','a','r');
    worker->goalentity = NULL;
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;

    G_ClientCommand(clent, 3, command);

    T_EQ(clent->build_project, 0);
    T_NULL(client->menu.on_location_selected);
    T_NULL(worker->goalentity);
    T_ASSERT(building_cursor_clear_seen);

    gi.Write = old_write;
}

TEST(wc3_building, smart_target_cancels_build_placement_before_issuing_order) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    LPEDICT clent = &g_edicts[0];
    LPGAMECLIENT client = clent->client;
    LPEDICT worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    LPEDICT target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    char target_number[16];
    LPCSTR command[] = { "smart", target_number };

    snprintf(target_number, sizeof(target_number), "%u", (unsigned)target->s.number);
    G_SelectEntity(client, worker);
    client->menu.on_location_selected = build_menu_send_builder;
    clent->build_project = MAKEFOURCC('h','b','a','r');
    worker->goalentity = NULL;
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;

    G_ClientCommand(clent, 2, command);

    T_EQ(clent->build_project, 0);
    T_NULL(client->menu.on_location_selected);
    T_NULL(worker->goalentity);
    T_ASSERT(building_cursor_clear_seen);

    gi.Write = old_write;
}


TEST(wc3_building, repair_order_from_stand_keeps_staged_target_and_walks) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -256, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 256, 0);
    worker->data.UnitAbilities = &abilities;
    worker->stand = unit_stand;
    worker->collision = 16.0f;
    building->collision = 32.0f;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    building->svflags |= SVF_MONSTER;
    building->s.player = worker->s.player;

    /* Real spawned workers already have a stand move. Entering Repair from
     * that move must not make unit_setmove() cancel the newly staged state. */
    unit_stand(worker);
    T_NOT_NULL(worker->currentmove);
    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    T_EQ(worker->buildwork.ability, MAKEFOURCC('A','r','e','n'));
    T_ASSERT(worker->build == building);
    T_ASSERT(worker->goalentity == building);
    T_STREQ(worker->currentmove->animation, "walk");

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_walk_handoff_requires_actual_contact) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    AbilityData_t *repair;
    UnitBalance_t balance;
    slkTestData_t *rows, *old_abilities;
    FLOAT interaction;
    FLOAT step;
    FLOAT hp_before;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    repair = (AbilityData_t *)G_AbilityData(MAKEFOURCC('A','r','e','n'));
    T_NOT_NULL(repair);
    repair->range[0] = 50.0f;

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 200, 0);
    worker->data.UnitAbilities = &abilities;
    worker->stand = unit_stand;
    worker->collision = 16.0f;
    worker->unitinfo.MoveSpeed = 190.0f;
    worker->s.player = client->ps.number;
    building->collision = 32.0f;
    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;
    /* The minimal hbar fixture has no repair/build duration. Give this test
     * an explicit Repair duration so its work tick exercises HP progression. */
    balance = *building->data.UnitBalance;
    balance.reptm = 10;
    balance.buildTime = 100;
    balance.goldRep = 5;
    balance.lumberRep = 3;
    building->data.UnitBalance = &balance;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 100;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100;

    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    T_STREQ(worker->currentmove->animation, "walk");

    interaction = worker->collision + building->collision + repair->range[0];
    step = unit_movedistance(worker);
    worker->s.origin2.x = building->s.origin2.x - interaction - step * 0.5f;
    worker->s.origin2.y = building->s.origin2.y;
    gi.LinkEntity(worker);

    /* This is the runtime failure from Human02: close enough that one more
     * movement step will make contact, but not actually in Repair range yet.
     * The worker must take that step instead of starting/restarting work. */
    T_ASSERT(M_DistanceToGoal(worker) > interaction);
    T_ASSERT(M_DistanceToGoal(worker) <= interaction + step);
    worker->currentmove->think(worker);
    T_STREQ(worker->currentmove->animation, "walk");

    /* The movement tick has now crossed the interaction boundary.  The next
     * AI tick may enter work, and the following work tick must change HP. */
    T_ASSERT(M_DistanceToGoal(worker) <= interaction);
    worker->currentmove->think(worker);
    T_STREQ(worker->currentmove->animation, "stand work");
    hp_before = building->health.value;
    worker->currentmove->think(worker);
    T_ASSERT(building->health.value > hp_before);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, held_construction_birth_animation_tracks_progress) {
    LPEDICT building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    UnitBalance_t balance = *building->data.UnitBalance;
    animation_t birth = {
        .name = "birth",
        .interval = { 1000, 3000 },
    };

    balance.buildTime = 10;
    building->data.UnitBalance = &balance;
    building->animation = &birth;
    building->construction.active = true;
    building->construction.paused = true;
    building->aiflags |= AI_HOLD_FRAME;

    building->construction.progress = 2500.0f;
    M_MoveFrame(building);
    T_EQ(building->s.frame, 1500);

    building->construction.progress = 7500.0f;
    M_MoveFrame(building);
    T_EQ(building->s.frame, 2500);

    /* No progress means no visual drift while Human construction is paused. */
    M_MoveFrame(building);
    T_EQ(building->s.frame, 2500);
}

TEST(wc3_building, primary_human_builder_ignores_datad_but_extra_builder_requires_it) {
    LPEDICT primary;
    LPEDICT extra;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    AbilityData_t *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    repair = (AbilityData_t *)G_AbilityData(MAKEFOURCC('A','r','e','p'));
    T_NOT_NULL(repair);
    repair->data[0][3] = 0.0f;

    primary = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    extra = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 64);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    primary->data.UnitAbilities = &abilities;
    extra->data.UnitAbilities = &abilities;
    primary->stand = unit_stand;
    extra->stand = unit_stand;
    primary->collision = 16.0f;
    extra->collision = 16.0f;
    building->collision = 32.0f;
    primary->s.player = 0;
    extra->s.player = 0;
    building->s.player = 0;
    building->svflags |= SVF_MONSTER;
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;

    T_ASSERT(G_StartHumanConstruction(primary, building));
    T_ASSERT(S_OrderRepair(primary, building, MAKEFOURCC('A','r','e','p')));
    T_ASSERT(primary->buildwork.primary);
    T_ASSERT(primary->build == building);
    T_ASSERT(building->construction.primary_builder == primary);

    T_ASSERT(!S_OrderRepair(extra, building, MAKEFOURCC('A','r','e','p')));
    T_EQ(extra->buildwork.ability, 0);
    T_NULL(extra->build);

    building_restore_repair_data(old_abilities, rows);
}

#endif
