#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../hud/hud_local.h"
#include "jass/jass.h"

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y);
void setup_test_world(void);
void repair_build_primary(LPEDICT ent, LPEDICT building);
void repair_build_legacy(LPEDICT ent, LPEDICT building);
void build_build(LPEDICT ent);
BOOL build_menu_send_builder(LPEDICT clent, LPCVECTOR2 location);
slkTestData_t *parse_slk_string(const char *slk_text);
void free_slk_rows(slkTestData_t *rows);
BOOL run_test_jass(LPCSTR src);

static DWORD building_stand_calls;
static uiFrame_t building_command_frame;
static uiCommandButton_t building_command_state;
static BOOL building_command_frame_seen;
static uiFrame_t building_command_number_frame;
static uiLabel_t building_command_number_label;
static char building_command_number_text[16];
static BOOL building_command_number_seen;
static BOOL building_cursor_opcode_seen;
static BOOL building_cursor_clear_seen;
static PATHSTR building_image_path;

static void building_test_stand(LPEDICT ent) {
    (void)ent;
    building_stand_calls++;
}

static int building_test_image_index(LPCSTR name) {
    snprintf(building_image_path, sizeof(building_image_path), "%s", name);
    return 1;
}

static void building_capture_write(pfWriteType_t type, void const *value) {
    if (!value) return;
    if (type == PF_UIFRAME) {
        uiFrame_t const *frame = value;
        if (frame->flags.type == FT_SPRITE) return; /* autocast overlay; do not overwrite command capture */
        if (frame->flags.type == FT_STRING && frame->text && frame->buffer.size == sizeof(uiLabel_t)) {
            building_command_number_frame = *frame;
            building_command_number_label = *(uiLabel_t const *)frame->buffer.data;
            snprintf(building_command_number_text, sizeof(building_command_number_text), "%s", frame->text);
            building_command_number_seen = true;
        } else {
            building_command_frame = *frame;
            memset(&building_command_state, 0, sizeof(building_command_state));
            if (frame->buffer.size == sizeof(building_command_state) && frame->buffer.data)
                building_command_state = *(uiCommandButton_t const *)frame->buffer.data;
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

static LPCSTR const building_repair_stock_targets =
    "ground,air,structure,mechanical,friend,nonancient,invulnerable,vulnerable";
static LPCSTR const building_renew_stock_targets =
    "ground,air,structure,mechanical,friend,invulnerable,vulnerable";

static const char building_upgrade_slk[] =
    "ID;PWXL;N;E\n"
    "B;X17;Y8;D0\n"
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
    "C;X14;K\"effect2\"\n"
    "C;X15;K\"base2\"\n"
    "C;X16;K\"mod2\"\n"
    "C;X17;K\"code2\"\n"
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
    "C;X1;Y8;K\"Rhst\"\n"
    "C;X2;K\"caster\"\n"
    "C;X3;K2\n"
    "C;X10;K\"rmnx\"\n"
    "C;X11;K100\n"
    "C;X12;K100\n"
    "C;X13;K\"hsor\"\n"
    "C;X14;K\"rmnr\"\n"
    "C;X15;K0.325\n"
    "C;X16;K0.325\n"
    "C;X17;K\"hsor\"\n"
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

static const char building_morph_balance_slk[] =
    "ID;PWXL;N;E\n"
    "B;X8;Y3;D0\n"
    "C;X1;Y1;K\"unitBalanceID\"\n"
    "C;X2;K\"goldcost\"\n"
    "C;X3;K\"lumbercost\"\n"
    "C;X4;K\"realHP\"\n"
    "C;X5;K\"bldtm\"\n"
    "C;X6;K\"fused\"\n"
    "C;X7;K\"fmade\"\n"
    "C;X8;K\"isbldg\"\n"
    "C;X1;Y2;K\"hbar\"\n"
    "C;X2;K100\n"
    "C;X3;K50\n"
    "C;X4;K1000\n"
    "C;X5;K60\n"
    "C;X6;K2\n"
    "C;X7;K12\n"
    "C;X8;K1\n"
    "C;X1;Y3;K\"otrb\"\n"
    "C;X2;K320\n"
    "C;X3;K210\n"
    "C;X4;K1500\n"
    "C;X5;K140\n"
    "C;X6;K3\n"
    "C;X7;K14\n"
    "C;X8;K1\n"
    "E\n";

static const char building_morph_ui_slk[] =
    "ID;PWXL;N;E\n"
    "B;X4;Y3;D0\n"
    "C;X1;Y1;K\"unitUIID\"\n"
    "C;X2;K\"isbldg\"\n"
    "C;X4;K\"file\"\n"
    "C;X1;Y2;K\"hbar\"\n"
    "C;X2;K1\n"
    "C;X4;K\"UI\\Glues\\SpriteLayers\\TopLeftPanel.mdx\"\n"
    "C;X1;Y3;K\"otrb\"\n"
    "C;X2;K1\n"
    "C;X4;K\"UI\\Glues\\SpriteLayers\\TopLeftPanel.mdx\"\n"
    "E\n";

typedef struct {
    slkTestData_t *old_balance;
    slkTestData_t *old_ui;
    slkTestData_t *balance_rows;
    slkTestData_t *ui_rows;
} buildingMorphRows_t;

static buildingMorphRows_t building_install_morph_data(void) {
    buildingMorphRows_t rows = {0};
    rows.balance_rows = parse_slk_string(building_morph_balance_slk);
    rows.ui_rows = parse_slk_string(building_morph_ui_slk);
    rows.old_balance = G_SetSLKRows("UnitBalance", rows.balance_rows);
    rows.old_ui = G_SetSLKRows("UnitUI", rows.ui_rows);
    return rows;
}

static void building_restore_morph_data(buildingMorphRows_t rows) {
    G_SetSLKRows("UnitUI", rows.old_ui);
    G_SetSLKRows("UnitBalance", rows.old_balance);
    free_slk_rows(rows.ui_rows);
    free_slk_rows(rows.balance_rows);
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

TEST(wc3_building, hud_texture_paths_are_authored_per_recipient) {
    stbIniCache_t old = game.config.theme, custom = {0};
    LPGAMECLIENT previous = ui_current_client;
    GAMECLIENT client = { .ps.race = kPlayerRaceHuman };
    int (*old_index)(LPCSTR) = gi.ImageIndex;
    PATHSTR old_key, old_name;
    BOOL old_dec = hud.image_decorated[1];
    strcpy(old_key, hud.image_key[1]); strcpy(old_name, hud.image_name[1]);
    T_ASSERT(Stb_IniCacheLoad(&custom, "TestData\\HudSkin.txt"));
    game.config.theme = custom; gi.ImageIndex = building_test_image_index;
    hud.image_key[1][0] = 0; UI_SetCurrentClient(&client);
    DWORD image = UI_LoadTexture("Background", true);
    T_STREQ(building_image_path, "Human.blp");
    T_EQ(UI_LiveImage(image), 1); T_STREQ(building_image_path, "Human.blp");
    T_STREQ(hud.image_key[image], "Background");
    client.ps.race = kPlayerRaceOrc;
    T_EQ(UI_LiveImage(image), 1); T_STREQ(building_image_path, "Orc.blp");
    UI_SetCurrentClient(NULL);
    T_EQ(UI_LiveImage(image), 1); T_STREQ(building_image_path, "Default.blp");
    T_STREQ(UI_ThemeImagePath("ConsoleTexture05"), "Custom05.blp");
    T_STREQ(UI_ThemeImagePath("ConsoleTexture06"), "Custom06.blp");
    T_STREQ(UI_ThemeImagePath("UI\\Textures\\fixed.blp"), "UI\\Textures\\fixed.blp");
    strcpy(hud.image_key[1], old_key); strcpy(hud.image_name[1], old_name); hud.image_decorated[1] = old_dec;
    UI_SetCurrentClient(previous); gi.ImageIndex = old_index; game.config.theme = old; Stb_IniCacheFree(&custom);
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

TEST(wc3_building, building_upgrade_uses_relative_unit_costs_and_cancel_restores_state) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const source_id = MAKEFOURCC('h','b','a','r');
    DWORD const target_id = MAKEFOURCC('o','t','r','b');
    UnitProfile_t profile = { .upgrade = "otrb" };
    buildingMorphRows_t rows;
    LPEDICT building;
    LONG gold = 0, lumber = 0, food = 0;
    char reason[128];
    buildingUpgradeCommandParams_t params;

    setup_test_world();
    rows = building_install_morph_data();
    building = alloc_test_unit(source_id, 0, 0);
    building->data.UnitProfile = &profile;
    building->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    G_SetUnitFoodUsed(building, 2);
    G_SetUnitFoodMade(building, 12);
    memset(client->tech, 0, sizeof(client->tech));
    level.events.read = level.events.write = 0;

    G_GetBuildingUpgradeCosts(&(buildingUpgradeCostParams_t){
        .building = building, .unit_id = target_id, .gold = &gold, .lumber = &lumber, .food = &food });
    T_EQ(gold, 220);
    T_EQ(lumber, 160);
    T_EQ(food, 1);
    params = (buildingUpgradeCommandParams_t){
        .client = client, .producer = building, .unit_id = target_id, .reason = reason, .reason_size = sizeof(reason) };
    T_EQ(G_GetBuildingUpgradeCommandState(&params), BUILD_COMMAND_AVAILABLE);

    T_ASSERT(!UI_TestUsesBuildingQueuePanel(client, building));
    T_ASSERT(G_StartBuildingUpgrade(building, target_id));
    T_ASSERT(G_BuildingUpgradeActive(building));
    T_ASSERT(UI_TestUsesBuildingQueuePanel(client, building));
    T_EQ(building->class_id, source_id);
    T_EQ(building->research.upgrade, target_id);
    T_FEQ(building->research.duration, 140.0f, 0.001f);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 280);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 340);
    T_EQ(building->food.used, 3);
    T_EQ(G_GetPlayerTechInProgress(client, target_id), 1);
    T_ASSERT(building->aiflags & AI_HOLD_FRAME);
    T_EQ(level.events.write, 2);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_UPGRADE_START);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_UPGRADE_START);

    T_ASSERT(G_CancelBuildingUpgrade(building));
    T_ASSERT(!G_BuildingUpgradeActive(building));
    T_ASSERT(!UI_TestUsesBuildingQueuePanel(client, building));
    T_EQ(building->class_id, source_id);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 500);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 500);
    T_EQ(building->food.used, 2);
    T_EQ(G_GetPlayerTechInProgress(client, target_id), 0);
    T_ASSERT(!(building->aiflags & AI_HOLD_FRAME));
    T_EQ(level.events.write, 4);
    T_EQ(level.events.queue[2].type, EVENT_PLAYER_UNIT_UPGRADE_CANCEL);
    T_EQ(level.events.queue[3].type, EVENT_UNIT_UPGRADE_CANCEL);

    building_restore_morph_data(rows);
}

TEST(wc3_building, instant_build_cheat_completes_building_upgrade_on_next_frame) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const source_id = MAKEFOURCC('h','b','a','r');
    DWORD const target_id = MAKEFOURCC('o','t','r','b');
    UnitProfile_t profile = { .upgrade = "otrb" };
    buildingMorphRows_t rows;
    LPEDICT building;

    setup_test_world();
    rows = building_install_morph_data();
    building = alloc_test_unit(source_id, 64, 64);
    building->data.UnitProfile = &profile;
    building->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 20;
    client->cheat_instant_build = true;
    memset(client->tech, 0, sizeof(client->tech));

    T_ASSERT(G_StartBuildingUpgrade(building, target_id));
    T_ASSERT(G_BuildingUpgradeActive(building));
    T_FEQ(building->research.progress, 0.0f, 0.001f);

    G_RunBuildingUpgradeFrame(building);

    T_ASSERT(!G_BuildingUpgradeActive(building));
    T_EQ(building->class_id, target_id);
    T_EQ(G_GetPlayerTechInProgress(client, target_id), 0);

    client->cheat_instant_build = false;
    building_restore_morph_data(rows);
}

TEST(wc3_building, building_upgrade_completion_morphs_in_place_and_preserves_health_ratio) {
    LPGAMECLIENT client = &game.clients[0];
    DWORD const source_id = MAKEFOURCC('h','b','a','r');
    DWORD const target_id = MAKEFOURCC('o','t','r','b');
    UnitProfile_t profile = { .upgrade = "otrb" };
    buildingMorphRows_t rows;
    LPEDICT building;
    LPEDICT identity;

    setup_test_world();
    rows = building_install_morph_data();
    building = alloc_test_unit(source_id, 64, 64);
    identity = building;
    building->data.UnitProfile = &profile;
    building->s.player = client->ps.number;
    building->health.value = 500.0f;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 20;
    G_SetUnitFoodUsed(building, 2);
    G_SetUnitFoodMade(building, 12);
    memset(client->tech, 0, sizeof(client->tech));
    level.events.read = level.events.write = 0;

    T_ASSERT(G_StartBuildingUpgrade(building, target_id));
    building->research.progress = building->research.duration;
    G_RunBuildingUpgradeFrame(building);

    T_ASSERT(building == identity);
    T_ASSERT(!G_BuildingUpgradeActive(building));
    T_EQ(building->class_id, target_id);
    T_FEQ(building->health.max_value, 1500.0f, 0.001f);
    T_FEQ(building->health.value, 750.0f, 0.001f);
    T_EQ(building->food.used, 3);
    T_EQ(building->food.made, 14);
    T_EQ(G_GetPlayerTechInProgress(client, target_id), 0);
    T_EQ(G_GetPlayerTechCountValue(client, target_id), 1);
    T_EQ(level.events.write, 4);
    T_EQ(level.events.queue[2].type, EVENT_PLAYER_UNIT_UPGRADE_FINISH);
    T_EQ(level.events.queue[3].type, EVENT_UNIT_UPGRADE_FINISH);

    building_restore_morph_data(rows);
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

TEST(wc3_building, instant_build_cheat_completes_research_on_next_tick) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer;
    UnitProfile_t profile = { .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old;
    DWORD const upgrade = MAKEFOURCC('R','h','m','e');

    setup_test_world();
    producer = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    old = building_install_upgrade_data(&rows);
    memset(client->tech, 0, sizeof(client->tech));
    producer->data.UnitProfile = &profile;
    producer->s.player = client->ps.number;
    producer->stand = building_test_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    client->connected = false;
    client->cheat_instant_build = true;

    T_ASSERT(G_QueueResearch(producer, upgrade));
    T_NOT_NULL(producer->build);
    T_ASSERT(producer->build->research.duration > 0.0f);
    T_NOT_NULL(producer->currentmove);
    T_NOT_NULL(producer->currentmove->think);

    producer->currentmove->think(producer);

    T_NULL(producer->build);
    T_EQ(G_GetPlayerTechInProgress(client, upgrade), 0);
    T_EQ(G_GetPlayerTechResearchedLevel(client, upgrade), 1);

    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, research_events_publish_producer_and_rawcode_context) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer;
    UnitProfile_t profile = { .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old;
    DWORD const upgrade = MAKEFOURCC('R','h','m','e');

    setup_test_world();
    producer = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    old = building_install_upgrade_data(&rows);
    memset(client->tech, 0, sizeof(client->tech));
    producer->data.UnitProfile = &profile;
    producer->s.player = client->ps.number;
    producer->stand = building_test_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 1000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 1000;
    client->connected = false; /* suppress HUD/audio presentation in this engine/JASS contract test */
    level.events.read = level.events.write = 0;

    T_ASSERT(run_test_jass(
        "globals\n"
        "  integer researchStarts = 0\n"
        "  integer researchCancels = 0\n"
        "  integer researchFinishes = 0\n"
        "endglobals\n"
        "function CheckResearchContext takes nothing returns nothing\n"
        "  call BJassAssert(GetUnitTypeId(GetResearchingUnit()) == 'hbla', \"researching unit must be producer\")\n"
        "  call BJassAssert(GetResearched() == 'Rhme', \"research callback must expose upgrade rawcode\")\n"
        "endfunction\n"
        "function OnResearchStart takes nothing returns nothing\n"
        "  call CheckResearchContext()\n"
        "  set researchStarts = researchStarts + 1\n"
        "endfunction\n"
        "function OnResearchCancel takes nothing returns nothing\n"
        "  call CheckResearchContext()\n"
        "  set researchCancels = researchCancels + 1\n"
        "endfunction\n"
        "function OnResearchFinish takes nothing returns nothing\n"
        "  call CheckResearchContext()\n"
        "  set researchFinishes = researchFinishes + 1\n"
        "endfunction\n"
        "function VerifyStart takes nothing returns nothing\n"
        "  call BJassAssert(researchStarts == 1, \"research start must fire once\")\n"
        "  call BJassAssert(researchCancels == 0, \"research cancel fired early\")\n"
        "  call BJassAssert(researchFinishes == 0, \"research finish fired early\")\n"
        "endfunction\n"
        "function VerifyCancel takes nothing returns nothing\n"
        "  call BJassAssert(researchStarts == 1, \"unexpected extra research start\")\n"
        "  call BJassAssert(researchCancels == 1, \"research cancel must fire once\")\n"
        "  call BJassAssert(researchFinishes == 0, \"research finish fired on cancel\")\n"
        "endfunction\n"
        "function VerifyFinish takes nothing returns nothing\n"
        "  call BJassAssert(researchStarts == 2, \"second research start missing\")\n"
        "  call BJassAssert(researchCancels == 1, \"unexpected extra research cancel\")\n"
        "  call BJassAssert(researchFinishes == 1, \"research finish must fire once\")\n"
        "endfunction\n"
        "function main takes nothing returns nothing\n"
        "  local trigger startTrig = CreateTrigger()\n"
        "  local trigger cancelTrig = CreateTrigger()\n"
        "  local trigger finishTrig = CreateTrigger()\n"
        "  call TriggerRegisterPlayerUnitEvent(startTrig, Player(0), EVENT_PLAYER_UNIT_RESEARCH_START, null)\n"
        "  call TriggerRegisterPlayerUnitEvent(cancelTrig, Player(0), EVENT_PLAYER_UNIT_RESEARCH_CANCEL, null)\n"
        "  call TriggerRegisterPlayerUnitEvent(finishTrig, Player(0), EVENT_PLAYER_UNIT_RESEARCH_FINISH, null)\n"
        "  call TriggerAddAction(startTrig, function OnResearchStart)\n"
        "  call TriggerAddAction(cancelTrig, function OnResearchCancel)\n"
        "  call TriggerAddAction(finishTrig, function OnResearchFinish)\n"
        "endfunction\n"));

    T_ASSERT(G_QueueResearch(producer, upgrade));
    T_EQ(level.events.write, 2);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_RESEARCH_START);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_RESEARCH_START);
    T_ASSERT(level.events.queue[0].edict == producer && level.events.queue[1].edict == producer);
    T_EQ((DWORD)level.events.queue[0].value, upgrade);
    T_EQ((DWORD)level.events.queue[1].value, upgrade);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "VerifyStart", false);
    T_ASSERT(!jass_rterror_pending(level.vm));

    T_ASSERT(G_CancelTrainingQueueItem(producer, 0, true));
    T_EQ(level.events.write, 4);
    T_EQ(level.events.queue[2].type, EVENT_PLAYER_UNIT_RESEARCH_CANCEL);
    T_EQ(level.events.queue[3].type, EVENT_UNIT_RESEARCH_CANCEL);
    T_EQ((DWORD)level.events.queue[2].value, upgrade);
    T_EQ((DWORD)level.events.queue[3].value, upgrade);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "VerifyCancel", false);
    T_ASSERT(!jass_rterror_pending(level.vm));

    T_ASSERT(G_QueueResearch(producer, upgrade));
    T_EQ(level.events.write, 6);
    T_EQ(level.events.queue[4].type, EVENT_PLAYER_UNIT_RESEARCH_START);
    T_EQ(level.events.queue[5].type, EVENT_UNIT_RESEARCH_START);
    G_RunEvents();
    jass_runevents(level.vm);
    T_NOT_NULL(producer->build);
    producer->build->research.duration = 0.0f;
    T_NOT_NULL(producer->currentmove);
    T_NOT_NULL(producer->currentmove->think);
    producer->currentmove->think(producer);
    T_EQ(level.events.write, 8);
    T_EQ(level.events.queue[6].type, EVENT_PLAYER_UNIT_RESEARCH_FINISH);
    T_EQ(level.events.queue[7].type, EVENT_UNIT_RESEARCH_FINISH);
    T_EQ((DWORD)level.events.queue[6].value, upgrade);
    T_EQ((DWORD)level.events.queue[7].value, upgrade);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "VerifyFinish", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(G_GetPlayerTechResearchedLevel(client, upgrade), 1);

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

TEST(wc3_building, researched_caster_mana_effects_update_existing_units) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT unit = alloc_test_unit(MAKEFOURCC('h','s','o','r'), 0, 0);
    UnitBalance_t balance = { .upgrades = "Rhst", .manaRegen = 0.5f };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    DWORD const training = MAKEFOURCC('R','h','s','t');

    memset(client->tech, 0, sizeof(client->tech));
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->mana.max_value = 200.0f;
    unit->mana.value = 0.0f;

    G_SetPlayerTechResearched(client, training, 1);
    T_FEQ(unit->mana.max_value, 300.0f, 0.001f);
    T_FEQ(unit->mana.value, 100.0f, 0.001f);
    T_FEQ(unit->mana_regen_bonus, 0.325f, 0.001f);

    G_SetPlayerTechResearched(client, training, 2);
    T_FEQ(unit->mana.max_value, 400.0f, 0.001f);
    T_FEQ(unit->mana.value, 200.0f, 0.001f);
    T_FEQ(unit->mana_regen_bonus, 0.65f, 0.001f);

    G_SetPlayerTechResearched(client, training, 0);
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

TEST(wc3_building, upgraded_buildings_satisfy_predecessor_requirements) {
    static const char profile_slk[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"Upgrade\"\n"
        "C;Y1;X3;K\"Requires\"\n"
        "C;Y2;X1;K\"htow\"\n"
        "C;Y2;X2;K\"hkee\"\n"
        "C;Y3;X1;K\"hkee\"\n"
        "C;Y3;X2;K\"hcas\"\n"
        "C;Y4;X1;K\"hcas\"\n"
        "C;Y5;X1;K\"hbar\"\n"
        "C;Y5;X3;K\"htow\"\n"
        "E\n";
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker, keep, castle;
    UnitProfile_t worker_profile = { .builds = "hbar" };
    slkTestData_t *profile_rows = parse_slk_string(profile_slk);
    slkTestData_t *old_profile = G_SetProfileRows(profile_rows);
    DWORD const blacksmith = MAKEFOURCC('h','b','a','r');
    char reason[128];

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    worker->data.UnitProfile = &worker_profile;
    worker->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 60000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 60000;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_EQ(G_GetBuildCommandState(client, worker, blacksmith, reason, sizeof(reason)), BUILD_COMMAND_DISABLED);
    T_STREQ(reason, "Requires htow");

    keep = alloc_test_unit(MAKEFOURCC('h','k','e','e'), 0, 0);
    keep->s.player = client->ps.number;
    T_EQ(G_GetBuildCommandState(client, worker, blacksmith, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    keep->inuse = false;
    castle = alloc_test_unit(MAKEFOURCC('h','c','a','s'), 0, 0);
    castle->s.player = client->ps.number;
    T_EQ(G_GetBuildCommandState(client, worker, blacksmith, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    castle->inuse = false;
    G_SetProfileRows(old_profile);
    free_slk_rows(profile_rows);
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

TEST(wc3_building, hero_train_requirements_follow_owner_hero_tiers) {
    static const char balance_slk[] =
        "C;Y1;X1;K\"unitBalanceID\"\n"
        "C;Y1;X2;K\"STR\"\n"
        "C;Y2;X1;K\"H001\"\n"
        "C;Y2;X2;K1\n"
        "E\n";
    static const char profile_slk[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"Requirescount\"\n"
        "C;Y1;X3;K\"Requires1\"\n"
        "C;Y1;X4;K\"Requires2\"\n"
        "C;Y2;X1;K\"H001\"\n"
        "C;Y2;X2;K\"3\"\n"
        "C;Y2;X3;K\"hkee\"\n"
        "C;Y2;X4;K\"hcas\"\n"
        "E\n";
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT producer, hero1, hero2, keep, castle;
    UnitProfile_t producer_profile = { .trains = "H001" };
    slkTestData_t *balance_rows = parse_slk_string(balance_slk);
    slkTestData_t *profile_rows = parse_slk_string(profile_slk);
    slkTestData_t *old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    slkTestData_t *old_profile = G_SetProfileRows(profile_rows);
    DWORD const hero = MAKEFOURCC('H','0','0','1');
    char reason[128];

    producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    producer->data.UnitProfile = &producer_profile;
    producer->s.player = client->ps.number;
    T_EQ(G_GetTrainCommandState(client, producer, hero, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    hero1 = alloc_test_unit(hero, 0, 0); hero1->s.player = client->ps.number;
    T_EQ(G_GetTrainCommandState(client, producer, hero, reason, sizeof(reason)), BUILD_COMMAND_DISABLED);
    T_STREQ(reason, "Requires hkee");

    keep = alloc_test_unit(MAKEFOURCC('h','k','e','e'), 0, 0); keep->s.player = client->ps.number;
    T_EQ(G_GetTrainCommandState(client, producer, hero, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    hero2 = alloc_test_unit(hero, 0, 0); hero2->s.player = client->ps.number;
    T_EQ(G_GetTrainCommandState(client, producer, hero, reason, sizeof(reason)), BUILD_COMMAND_DISABLED);
    T_STREQ(reason, "Requires hcas");

    castle = alloc_test_unit(MAKEFOURCC('h','c','a','s'), 0, 0); castle->s.player = client->ps.number;
    T_EQ(G_GetTrainCommandState(client, producer, hero, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    G_SetProfileRows(old_profile); G_SetSLKRows("UnitBalance", old_balance);
    free_slk_rows(profile_rows); free_slk_rows(balance_rows);
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

TEST(wc3_building, shared_controller_command_card_invalidates_with_owner_state) {
    LPGAMECLIENT owner = &game.clients[0];
    LPGAMECLIENT viewer = &game.clients[1];
    LPEDICT producer;

    setup_test_world();
    owner->connected = viewer->connected = true;
    producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    producer->s.player = owner->ps.number;
    producer->selected |= 1 << viewer->ps.number;
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_SHARED_CONTROL, true);
    owner->commands_dirty = viewer->commands_dirty = false;

    G_InvalidateCommands(owner);
    T_ASSERT(owner->commands_dirty);
    T_ASSERT(viewer->commands_dirty);
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
    snprintf(button.art, sizeof(button.art), "ReplaceableTextures\\CommandButtons\\BTNWorkshop.blp");
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
    T_EQ(building_command_frame.color.r, 255);
    T_EQ(building_command_frame.color.g, 255);
    T_EQ(building_command_frame.color.b, 255);
    T_EQ(building_command_frame.tex.index, 1);
    T_STREQ(building_image_path, "ReplaceableTextures\\CommandButtonsDisabled\\DISBTNWorkshop.blp");

    building_command_frame_seen = false;
    button.disabled = 0;
    UI_WriteCommandButtonFrame(&button);
    T_ASSERT(building_command_frame_seen);
    T_EQ(building_command_frame.hotkey, 'B');
    T_NOT_NULL(building_command_frame.onclick);
    T_STREQ(building_image_path, button.art);

    /* Warsmash also accepts a basename without a directory. */
    snprintf(button.art, sizeof(button.art), "BTNWorkshop.blp");
    button.disabled = 1;
    UI_WriteCommandButtonFrame(&button);
    T_STREQ(building_image_path, "ReplaceableTextures\\CommandButtonsDisabled\\DISBTNWorkshop.blp");

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, disabled_command_button_rejects_missing_skin_and_overlong_path) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    stbIniCache_t theme = game.config.theme;
    gameCommandButton_t button = { .disabled = 1, .art = "BTNWorkshop.blp" };

    gi.Write = building_capture_write;
    gi.ImageIndex = building_test_image_index;
    game.config.theme = (stbIniCache_t){ 0 };
    building_image_path[0] = '\0';
    UI_WriteCommandButtonFrame(&button);
    T_EQ(building_command_frame.tex.index, 0);
    T_STREQ(building_image_path, "");

    game.config.theme = theme;
    memset(button.art, 'x', sizeof(button.art) - 1);
    button.art[sizeof(button.art) - 1] = '\0';
    UI_WriteCommandButtonFrame(&button);
    T_EQ(building_command_frame.tex.index, 0);
    T_STREQ(building_image_path, "");

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, command_button_serializes_radial_cooldown_window) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(LPCSTR) = gi.ImageIndex;
    gameCommandButton_t button = {
        .art = "test", .cooldown = 0.75f,
        .cooldown_start_time = 1000, .cooldown_end_time = 5000
    };

    gi.Write = building_capture_write;
    gi.ImageIndex = building_test_image_index;
    building_command_frame_seen = false;
    UI_WriteCommandButtonFrame(&button);

    T_ASSERT(building_command_frame_seen);
    T_FEQ(building_command_frame.value, 0.75f, 0.001f);
    T_ASSERT(building_command_frame.flagsvalue & UIFLAG_RADIAL_SHADE);
    T_EQ(building_command_state.radialStartTime, 1000);
    T_EQ(building_command_state.radialEndTime, 5000);

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

TEST(wc3_building, tech_state_default_values_do_not_consume_slots) {
    LPGAMECLIENT client = &game.clients[0];

    memset(client->tech, 0, sizeof(client->tech));
    /* Default max_allowed (-1/unlimited) and researched/in-progress 0 must not
     * allocate entries; NightElfX02 issues 137 distinct non-default techs and
     * must not lose slots to default-valued writes. */
    G_SetPlayerTechMaxAllowed(client, 0x43000001u, -1);
    G_SetPlayerTechResearched(client, 0x43000002u, 0);
    G_AddPlayerTechResearched(client, 0x43000003u, 0);
    G_AddPlayerTechInProgress(client, 0x43000004u, 0);
    T_EQ(G_GetPlayerTechMaxAllowed(client, 0x43000001u), -1);
    T_EQ(G_GetPlayerTechResearchedLevel(client, 0x43000002u), 0);
    /* A full table of real entries must still fit after the default writes. */
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        DWORD const techid = 0x44000000u + i + 1;
        G_SetPlayerTechMaxAllowed(client, techid, 2);
    }
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        DWORD const techid = 0x44000000u + i + 1;
        T_EQ(G_GetPlayerTechMaxAllowed(client, techid), 2);
    }
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
    LPEDICT worker;
    LPEDICT blocker;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    VECTOR2 requested = { 64.0f, 64.0f };
    VECTOR2 snapped;
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64.0f, 64.0f);
    builder->data.UnitAbilities = &abilities;
    builder->svflags |= SVF_MONSTER;
    builder->collision = 16.0f;

    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_OK);

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), snapped.x, snapped.y);
    worker->data.UnitAbilities = &abilities;
    worker->svflags |= SVF_MONSTER;
    worker->movetype = MOVETYPE_STEP;
    worker->collision = 16.0f;
    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_OK);

    blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), snapped.x, snapped.y);
    blocker->svflags |= SVF_MONSTER;
    blocker->collision = 16.0f;
    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_UNIT_BLOCKED);

    requested = (VECTOR2){ 100000.0f, 100000.0f };
    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_OUT_OF_BOUNDS);
}

TEST(wc3_building, gold_return_building_respects_gold_mine_exclusion_radius) {
    LPEDICT builder;
    LPEDICT mine;
    UnitAbilities_t mine_abilities = { .abilList = "Abgm" };
    VECTOR2 requested = { 0.0f, 0.0f };
    VECTOR2 snapped;
    DWORD const gold_return_building = MAKEFOURCC('h','T','S','T');
    DWORD const ordinary_building = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('n','T','S','T'),
                           WC3_GOLD_MINE_MIN_DISTANCE - 1.0f, 0.0f);
    mine->data.UnitAbilities = &mine_abilities;

    T_ASSERT(S_UnitTypeReturnsGold(gold_return_building));
    T_ASSERT(S_GoldMineIsMine(mine));
    T_EQ(G_EvaluateBuildPlacement(builder, gold_return_building, &requested, &snapped),
         PLACE_TOO_CLOSE_TO_GOLD_MINE);
    T_EQ(G_EvaluateBuildPlacement(builder, ordinary_building, &requested, &snapped), PLACE_OK);

    mine->s.origin2.x = WC3_GOLD_MINE_MIN_DISTANCE;
    mine->s.origin.x = mine->s.origin2.x;
    T_EQ(G_EvaluateBuildPlacement(builder, gold_return_building, &requested, &snapped), PLACE_OK);
}

TEST(wc3_building, placement_flags_treat_slk_sentinel_as_empty) {
    T_EQ(G_PlacementFlags(NULL), 0);
    T_EQ(G_PlacementFlags("_"), 0);
    T_ASSERT(G_PlacementFlags("unwalkable") & WC3_PATH_UNWALKABLE);
}

TEST(wc3_building, placement_preview_uses_authoritative_pathing_flags) {
    BYTE prevented = 0, required = 0;
    DWORD const barracks = MAKEFOURCC('h','b','a','r');

    G_GetBuildPlacementPathingFlags(barracks, &prevented, &required);
    T_ASSERT(prevented & WC3_PATH_UNWALKABLE);
    T_ASSERT(prevented & WC3_PATH_UNBUILDABLE);
}

TEST(wc3_building, spawned_unit_exports_gameplay_collision_radius) {
    LPEDICT unit;

    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    /* alloc_test_unit() binds object data but deliberately does not run the
     * production spawn initializer.  Exercise SP_SpawnUnit() here because it
     * is the code that resolves collisionSize and exports it to entityState_t.
     * Use hpea because the generated ROC/TFT test data explicitly guarantees
     * its gameplay collision radius (16); the hfoo fixture does not. */
    SP_SpawnUnit(unit);
    T_FEQ(unit->collision, 16.0f, 0.001f);
    T_FEQ(unit->s.collision, 16.0f, 0.001f);
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

TEST(wc3_building, acolyte_places_haunted_mine_on_off_grid_gold_mine) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker, mine;
    UnitProfile_t profile = { .builds = "ugol" };
    VECTOR2 requested = { 101.0f, 99.0f }, snapped;
    DWORD const haunted = MAKEFOURCC('u','g','o','l');

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('u','a','c','o'), 0.0f, 0.0f);
    mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), 100.0f, 100.0f);
    worker->s.player = client->ps.number;
    worker->data.UnitProfile = &profile;
    mine->s.player = PLAYER_NEUTRAL_PASSIVE;
    mine->resources = 12500;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_EQ(G_EvaluateBuildPlacement(worker, haunted, &requested, &snapped), PLACE_OK);
    T_FEQ(snapped.x, mine->s.origin2.x, 0.001f);
    T_FEQ(snapped.y, mine->s.origin2.y, 0.001f);
    T_ASSERT(G_IssueBuildOrder(worker, haunted, &requested));
    T_NOT_NULL(worker->goalentity);
    T_FEQ(worker->goalentity->s.origin2.x, mine->s.origin2.x, 0.001f);
    T_FEQ(worker->goalentity->s.origin2.y, mine->s.origin2.y, 0.001f);
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
    T_EQ(building->construction.type, CONSTRUCTION_HUMAN);
    T_ASSERT(building->construction.primary_builder == builder);
    T_FEQ(building->construction.progress, 0.0f, 0.001f);
    T_ASSERT(!building->construction.paid);
    T_EQ(building->construction.payer, 0);
    T_EQ(building->construction.gold, 0);
    T_EQ(building->construction.lumber, 0);
    T_ASSERT(building->aiflags & AI_HOLD_FRAME);
    T_FEQ(building->health.value, 100.0f, 0.001f);
}

TEST(wc3_building, instant_build_cheat_completes_started_human_construction_on_next_frame) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder;
    LPEDICT building;
    UnitBalance_t balance;

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    balance = *building->data.UnitBalance;
    balance.buildTime = 60;
    building->data.UnitBalance = &balance;
    building->s.player = client->ps.number;
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;
    client->cheat_instant_build = true;

    T_ASSERT(G_StartHumanConstruction(builder, building));
    T_ASSERT(building->construction.active);
    T_ASSERT(building->construction.paused);

    G_RunConstructionFrame(building);

    T_ASSERT(!building->construction.active);
    T_FEQ(building->health.value, building->health.max_value, 0.001f);

    client->cheat_instant_build = false;
}

TEST(wc3_building, instant_build_cheat_completes_autonomous_construction_on_next_frame) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT worker;
    LPEDICT building;
    UnitBalance_t balance;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    balance = *building->data.UnitBalance;
    balance.buildTime = 60;
    building->data.UnitBalance = &balance;
    building->s.player = client->ps.number;
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;
    client->cheat_instant_build = true;

    T_ASSERT(G_StartOrcConstruction(worker, building));
    T_ASSERT(building->construction.active);

    G_RunConstructionFrame(building);

    T_ASSERT(!building->construction.active);
    T_FEQ(building->health.value, building->health.max_value, 0.001f);
    T_NULL(worker->build);

    client->cheat_instant_build = false;
}

TEST(wc3_building, removing_construction_releases_repair_worker) {
    LPEDICT builder;
    LPEDICT building;

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;
    T_ASSERT(G_StartHumanConstruction(builder, building));
    /* Keep this lifecycle test independent of the optional Repair SLK fixture. */
    builder->build = building;
    builder->buildwork.ability = MAKEFOURCC('A','r','e','p');
    builder->buildwork.primary = true;
    building->construction.primary_builder = builder;
    T_ASSERT(builder->build == building);

    G_FreeEdict(building);
    T_NULL(builder->build);
    T_EQ(builder->buildwork.ability, 0);
}

TEST(wc3_building, orc_construction_hides_worker_and_progresses_autonomously) {
    LPEDICT worker;
    LPEDICT building;
    UnitBalance_t balance;
    FLOAT hp_before;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    balance = *building->data.UnitBalance;
    balance.buildTime = 10;
    building->data.UnitBalance = &balance;
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;

    T_ASSERT(G_StartOrcConstruction(worker, building));
    T_EQ(building->construction.type, CONSTRUCTION_ORC);
    T_ASSERT(building->construction.active);
    T_ASSERT(!building->construction.paused);
    T_ASSERT(building->construction.worker == worker);
    T_ASSERT(building->construction.worker_inside);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_ASSERT(worker->paused);
    T_ASSERT(worker->invulnerable);
    T_ASSERT(worker->build == building);
    T_FEQ(building->health.value, 100.0f, 0.001f);

    hp_before = building->health.value;
    G_RunConstructionFrame(building);
    T_FEQ(building->construction.progress, (FLOAT)FRAMETIME, 0.001f);
    T_ASSERT(building->health.value > hp_before);
    T_ASSERT(building->health.value < building->health.max_value);
}

TEST(wc3_building, orc_build_dispatch_hides_peon_with_shared_repair_ability) {
    LPGAMECLIENT client = &game.clients[0];
    UnitData_t worker_data;
    UnitProfile_t profile = { .builds = "hbar" };
    UnitAbilities_t abilities = { .abilList = "Arep" };
    LPEDICT worker, building = NULL;
    VECTOR2 point = { 64.0f, 64.0f };
    DWORD const barracks = MAKEFOURCC('h', 'b', 'a', 'r');

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), -128.0f, -128.0f);
    worker_data = *worker->data.UnitData;
    worker_data.race = STR_ORC;
    worker->data.UnitData = &worker_data;
    worker->data.UnitProfile = &profile;
    worker->data.UnitAbilities = &abilities;
    worker->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_ASSERT(G_IssueBuildOrder(worker, barracks, &point));
    worker->s.origin2 = worker->goalentity->s.origin2;
    build_build(worker);
    FILTER_EDICTS(ent, ent->inuse && ent->s.class_id == barracks && ent != worker) building = ent;
    T_NOT_NULL(building);
    T_EQ(building->construction.type, CONSTRUCTION_ORC);
    T_ASSERT(building->construction.worker == worker);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
}

TEST(wc3_building, removing_orc_construction_releases_internal_worker) {
    LPEDICT worker;
    LPEDICT building;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->stand = unit_stand;

    T_ASSERT(G_StartOrcConstruction(worker, building));
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    G_FreeEdict(building);

    T_ASSERT(!building->inuse);
    T_ASSERT(worker->inuse);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(!worker->paused);
    T_ASSERT(!worker->invulnerable);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);
}

TEST(wc3_building, orc_construction_restores_worker_state_after_cancel) {
    LPEDICT worker;
    LPEDICT building;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 64, 0);
    worker->paused = true;
    worker->invulnerable = true;
    worker->s.renderfx |= RF_HIDDEN;

    T_ASSERT(G_StartOrcConstruction(worker, building));
    T_ASSERT(worker->paused);
    T_ASSERT(worker->invulnerable);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    G_StopConstruction(building);

    T_ASSERT(worker->paused);
    T_ASSERT(worker->invulnerable);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
}

TEST(wc3_building, undead_construction_releases_summoner_and_keeps_progressing) {
    LPEDICT worker;
    LPEDICT building;
    UnitBalance_t balance;
    DWORD release_time;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    balance = *building->data.UnitBalance;
    balance.buildTime = 60;
    building->data.UnitBalance = &balance;
    level.time = 1000;

    T_ASSERT(G_StartUndeadConstruction(worker, building));
    release_time = building->construction.worker_release_time;
    T_EQ(building->construction.type, CONSTRUCTION_UNDEAD);
    T_ASSERT(building->construction.worker == worker);
    T_ASSERT(!building->construction.worker_inside);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(!worker->paused);
    T_ASSERT(!worker->invulnerable);
    T_ASSERT(release_time > level.time);

    level.time = release_time;
    G_RunConstructionFrame(building);
    T_ASSERT(building->construction.active);
    T_NULL(building->construction.worker);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);
    T_FEQ(building->construction.progress, (FLOAT)FRAMETIME, 0.001f);
}

TEST(wc3_building, cancelling_undead_construction_releases_summoner) {
    LPEDICT worker;
    LPEDICT building;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 64, 0);
    T_ASSERT(G_StartUndeadConstruction(worker, building));
    T_ASSERT(building->construction.worker == worker);

    G_StopConstruction(building);
    T_ASSERT(!building->construction.active);
    T_NULL(building->construction.worker);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);
}

TEST(wc3_building, night_elf_ancient_cancel_restores_wisp_and_food) {
    LPEDICT worker;
    LPEDICT building;
    UnitBalance_t worker_balance;
    UnitBalance_t building_balance;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker_balance = *worker->data.UnitBalance;
    worker_balance.foodUsed = 2;
    worker->data.UnitBalance = &worker_balance;
    building_balance = *building->data.UnitBalance;
    building_balance.type = "ancient";
    building_balance.buildTime = 60;
    building->data.UnitBalance = &building_balance;
    G_SetUnitFoodUsed(worker, worker_balance.foodUsed);

    T_ASSERT(G_StartNightElfConstruction(worker, building));
    T_EQ(building->construction.type, CONSTRUCTION_NIGHTELF);
    T_ASSERT(building->construction.consumes_worker);
    T_EQ(worker->food.used, 0);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);

    G_StopConstruction(building);
    T_ASSERT(worker->inuse);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(!worker->paused);
    T_EQ(worker->food.used, worker_balance.foodUsed);
    T_EQ(building->construction.type, CONSTRUCTION_NONE);
}

TEST(wc3_building, night_elf_ancient_completion_consumes_wisp) {
    LPGAMECLIENT saved_client;
    LPEDICT worker;
    LPEDICT building;
    UnitBalance_t building_balance;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    building_balance = *building->data.UnitBalance;
    building_balance.type = "ancient";
    building->data.UnitBalance = &building_balance;
    building->stand = building_test_stand;
    building_stand_calls = 0;

    T_ASSERT(G_StartNightElfConstruction(worker, building));
    T_ASSERT(building->construction.consumes_worker);
    saved_client = g_edicts[0].client;
    g_edicts[0].client = NULL;
    G_CompleteConstruction(building);
    g_edicts[0].client = saved_client;

    T_ASSERT(!worker->inuse);
    T_ASSERT(!building->construction.active);
    T_EQ(building->construction.type, CONSTRUCTION_NONE);
    T_EQ(building_stand_calls, 1);
}

TEST(wc3_building, night_elf_non_ancient_completion_releases_wisp) {
    LPEDICT worker;
    LPEDICT building;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 64, 0);
    building->stand = building_test_stand;
    T_ASSERT(G_StartNightElfConstruction(worker, building));
    T_ASSERT(!building->construction.consumes_worker);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);

    G_CompleteConstruction(building);
    T_ASSERT(worker->inuse);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(!worker->paused);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);
}

TEST(wc3_building, cancelling_night_elf_non_ancient_construction_releases_wisp) {
    LPEDICT worker;
    LPEDICT building;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 64, 0);
    T_ASSERT(G_StartNightElfConstruction(worker, building));
    G_StopConstruction(building);

    T_ASSERT(worker->inuse);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(!worker->paused);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);
}

TEST(wc3_building, cancel_build_command_resolves_to_shared_cancel_handler) {
    T_EQ(FindAbilityForCommand(STR_CmdCancelBuild)->proc, FindAbilityForCommand(STR_CmdCancel)->proc);
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

TEST(wc3_building, legacy_orc_burrow_completion_publishes_construct_finish_and_grants_food) {
    LPGAMECLIENT client = &game.clients[0];
    LPEDICT builder;
    LPEDICT building;
    UnitBalance_t balance;
    LPGAMECLIENT saved_client;

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('o','p','e','o'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('o','t','r','b'), 64, 0);
    balance = *building->data.UnitBalance;
    builder->s.player = client->ps.number;
    builder->stand = unit_stand;
    builder->collision = 16.0f;
    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    building->stand = building_test_stand;
    building->collision = 32.0f;
    balance.buildTime = 1;
    balance.foodMade = 10;
    building->data.UnitBalance = &balance;
    building->health.max_value = 1000.0f;
    building->health.value = 999.0f;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 0;
    level.events.read = level.events.write = 0;
    building_stand_calls = 0;

    repair_build_legacy(builder, building);
    building->build = building;
    T_ASSERT(builder->build == building);
    T_ASSERT(building->build == building);

    /* Avoid HUD/FDF refresh in this engine-level test while preserving the
     * owning game client used for food accounting. */
    saved_client = g_edicts[0].client;
    g_edicts[0].client = NULL;
    builder->currentmove->think(builder);
    g_edicts[0].client = saved_client;

    T_NULL(builder->build);
    T_NULL(building->build);
    T_EQ(building_stand_calls, 1);
    T_FEQ(building->health.value, building->health.max_value, 0.001f);
    T_EQ(building->food.made, 10);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 10);
    T_EQ(level.events.write, 2);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_CONSTRUCT_FINISH);
    T_ASSERT(level.events.queue[0].edict == building);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_CONSTRUCT_FINISH);
    T_ASSERT(level.events.queue[1].edict == building);
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
    building->construction.type = CONSTRUCTION_HUMAN;
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
    T_EQ(building->construction.type, CONSTRUCTION_NONE);
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
    T_EQ(level.events.write, 2);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_CONSTRUCT_FINISH);
    T_ASSERT(level.events.queue[0].edict == building);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_CONSTRUCT_FINISH);
    T_ASSERT(level.events.queue[1].edict == building);

    G_CompleteConstruction(building);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 16);
    T_EQ(building_stand_calls, 1);
    T_EQ(level.events.write, 2);
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

TEST(wc3_building, repair_accepts_authored_mechanical_unit_and_rejects_organic_unit) {
    LPEDICT worker;
    LPEDICT mechanical;
    LPEDICT organic;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    AbilityData_t *repair;
    UnitBalance_t mechanical_balance;
    UnitBalance_t organic_balance;
    UnitData_t mechanical_data;
    UnitData_t organic_data;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    repair = (AbilityData_t *)G_AbilityData(MAKEFOURCC('A','r','e','n'));
    T_NOT_NULL(repair);
    repair->level[0].targs = building_repair_stock_targets;

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    mechanical = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    organic = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 64);
    worker->data.UnitAbilities = &abilities;
    worker->collision = 16.0f;
    worker->stand = unit_stand;

    mechanical_balance = *mechanical->data.UnitBalance;
    mechanical_balance.type = "mechanical";
    mechanical_balance.reptm = 10;
    mechanical_balance.goldRep = 0;
    mechanical_balance.lumberRep = 0;
    mechanical->data.UnitBalance = &mechanical_balance;
    mechanical_data = *mechanical->data.UnitData;
    mechanical_data.moveTypeName = "foot";
    mechanical->data.UnitData = &mechanical_data;
    mechanical->targtype = TARG_GROUND;
    mechanical->s.player = worker->s.player;
    mechanical->collision = 16.0f;
    mechanical->health.max_value = 1000.0f;
    mechanical->health.value = 500.0f;

    organic_balance = *organic->data.UnitBalance;
    organic_balance.type = "";
    organic_balance.reptm = 10;
    organic->data.UnitBalance = &organic_balance;
    organic_data = *organic->data.UnitData;
    organic_data.unitClassification = "";
    organic_data.moveTypeName = "foot";
    organic->data.UnitData = &organic_data;
    organic->targtype = TARG_GROUND;
    organic->s.player = worker->s.player;
    organic->collision = 16.0f;
    organic->health.max_value = 1000.0f;
    organic->health.value = 500.0f;

    T_ASSERT(S_OrderRepair(worker, mechanical, MAKEFOURCC('A','r','e','n')));
    T_ASSERT(worker->build == mechanical);
    T_STREQ(worker->currentmove->animation, "stand work");
    worker->currentmove->think(worker);
    T_ASSERT(mechanical->health.value > 500.0f);

    unit_stand(worker);
    T_ASSERT(!S_OrderRepair(worker, organic, MAKEFOURCC('A','r','e','n')));
    T_NULL(worker->build);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_mobile_unit_requires_mechanical_target_mask) {
    LPEDICT worker;
    LPEDICT mechanical;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    UnitBalance_t balance;
    UnitData_t data;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    mechanical = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    balance = *mechanical->data.UnitBalance;
    balance.type = "mechanical";
    balance.reptm = 10;
    mechanical->data.UnitBalance = &balance;
    data = *mechanical->data.UnitData;
    data.moveTypeName = "foot";
    mechanical->data.UnitData = &data;
    mechanical->targtype = TARG_GROUND;
    mechanical->s.player = worker->s.player;
    mechanical->health.max_value = 1000.0f;
    mechanical->health.value = 500.0f;

    /* The synthetic Aren row intentionally omits mechanical from targs. A
     * mobile unit must not become repairable merely because its classification
     * says mechanical; the Repair row must authorize that category too. */
    T_ASSERT(!S_OrderRepair(worker, mechanical, MAKEFOURCC('A','r','e','n')));

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_friend_target_allows_allied_completed_structure) {
    LPEDICT worker;
    LPEDICT building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    worker->s.player = 0;
    building->s.player = 1;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;

    T_ASSERT(!S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    level.alliances[0][1] |= 1u << ALLIANCE_PASSIVE;
    T_ASSERT(S_OrderRepair(worker, building, MAKEFOURCC('A','r','e','n')));
    T_ASSERT(worker->build == building);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, allied_repair_does_not_take_over_active_human_construction) {
    LPEDICT primary;
    LPEDICT ally;
    LPEDICT building;
    UnitAbilities_t human_repair = { .abilList = "Arep" };
    AbilityData_t *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    ((LPMAPINFO)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((LPMAPINFO)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    memset(level.alliances, 0, sizeof(level.alliances));
    level.alliances[1][0] |= 1u << ALLIANCE_PASSIVE;

    repair = (AbilityData_t *)G_AbilityData(MAKEFOURCC('A','r','e','p'));
    T_NOT_NULL(repair);
    repair->level[0].targs = building_repair_stock_targets;

    primary = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ally = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 64, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 96, 0);
    primary->data.UnitAbilities = &human_repair;
    ally->data.UnitAbilities = &human_repair;
    primary->s.player = building->s.player = 0;
    ally->s.player = 1;

    T_ASSERT(G_StartHumanConstruction(primary, building));
    T_ASSERT(building->construction.primary_builder == primary);
    T_ASSERT(!S_OrderRepair(ally, building, MAKEFOURCC('A','r','e','p')));
    T_ASSERT(building->construction.primary_builder == primary);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_datae_adds_range_only_for_floating_mechanical_units) {
    LPEDICT worker;
    LPEDICT floating;
    LPEDICT ground;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    AbilityData_t *repair;
    UnitBalance_t floating_balance;
    UnitBalance_t ground_balance;
    UnitData_t floating_data;
    UnitData_t ground_data;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    repair = (AbilityData_t *)G_AbilityData(MAKEFOURCC('A','r','e','n'));
    T_NOT_NULL(repair);
    repair->level[0].targs = building_repair_stock_targets;
    repair->level[0].range = 50.0f;
    repair->level[0].data[4].number = 75.0f;

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    floating = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 130, 0);
    ground = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 130, 64);
    worker->data.UnitAbilities = &abilities;
    worker->stand = unit_stand;
    worker->collision = 16.0f;

    floating_balance = *floating->data.UnitBalance;
    floating_balance.type = "mechanical";
    floating_balance.reptm = 10;
    floating->data.UnitBalance = &floating_balance;
    floating_data = *floating->data.UnitData;
    floating_data.moveTypeName = "float";
    floating->data.UnitData = &floating_data;
    floating->targtype = TARG_GROUND;
    floating->s.player = worker->s.player;
    floating->collision = 16.0f;
    floating->health.max_value = 1000.0f;
    floating->health.value = 500.0f;

    ground_balance = *ground->data.UnitBalance;
    ground_balance.type = "mechanical";
    ground_balance.reptm = 10;
    ground->data.UnitBalance = &ground_balance;
    ground_data = *ground->data.UnitData;
    ground_data.moveTypeName = "foot";
    ground->data.UnitData = &ground_data;
    ground->targtype = TARG_GROUND;
    ground->s.player = worker->s.player;
    ground->collision = 16.0f;
    ground->health.max_value = 1000.0f;
    ground->health.value = 500.0f;

    /* 130 > 16+16+50, but 130 <= 16+16+50+75. */
    T_ASSERT(S_OrderRepair(worker, floating, MAKEFOURCC('A','r','e','n')));
    T_STREQ(worker->currentmove->animation, "stand work");

    unit_stand(worker);
    T_ASSERT(S_OrderRepair(worker, ground, MAKEFOURCC('A','r','e','n')));
    T_STREQ(worker->currentmove->animation, "walk");

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_nonancient_mask_rejects_ancient_but_renew_can_accept_it) {
    LPEDICT worker;
    LPEDICT ancient;
    UnitAbilities_t abilities = { .abilList = "Arep,Arst" };
    AbilityData_t *repair;
    AbilityData_t *renew;
    UnitBalance_t balance;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    repair = (AbilityData_t *)G_AbilityData(MAKEFOURCC('A','r','e','p'));
    renew = (AbilityData_t *)G_AbilityData(MAKEFOURCC('A','r','s','t'));
    T_NOT_NULL(repair);
    T_NOT_NULL(renew);
    repair->level[0].targs = building_repair_stock_targets;
    renew->level[0].targs = building_renew_stock_targets;

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    ancient = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    worker->data.UnitAbilities = &abilities;
    balance = *ancient->data.UnitBalance;
    balance.type = "ancient";
    balance.reptm = 10;
    ancient->data.UnitBalance = &balance;
    ancient->s.player = worker->s.player;
    ancient->health.max_value = 1000.0f;
    ancient->health.value = 500.0f;

    T_ASSERT(!S_OrderRepair(worker, ancient, MAKEFOURCC('A','r','e','p')));
    T_ASSERT(S_OrderRepair(worker, ancient, MAKEFOURCC('A','r','s','t')));
    T_ASSERT(worker->build == ancient);

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
    T_ASSERT(!G_UnitAutocastIsOn(worker, FS_SLKKey(repair->classname)));
    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), true));
    T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
    T_ASSERT(worker->aiflags & AI_AUTOCAST_ACTIVE);
    T_ASSERT(G_UnitAutocastIsOn(worker, FS_SLKKey(repair->classname)));
    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), false));
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_REPAIR));
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_ACTIVE));
    T_ASSERT(!G_UnitAutocastIsOn(worker, FS_SLKKey(repair->classname)));

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
    T_EQ(G_OrderId("repair"), 852024);
    T_EQ(G_OrderId("repairon"), 852025);
    T_EQ(G_OrderId("repairoff"), 852026);
    T_STREQ(G_OrderId2String(852024), "repair");
    T_STREQ(G_OrderId2String(852025), "repairon");
    T_STREQ(G_OrderId2String(852026), "repairoff");
    T_ASSERT(unit_issueimmediateorder(worker, G_OrderId2String(852025)));
    T_ASSERT(G_UnitAutocastIsOn(worker, FS_SLKKey(repair->classname)));
    T_NULL(worker->build);
    T_ASSERT(unit_issueimmediateorder(worker, G_OrderId2String(852026)));
    T_ASSERT(!G_UnitAutocastIsOn(worker, FS_SLKKey(repair->classname)));
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

    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), true));
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
    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), true));
    T_ASSERT(G_TryUnitAutocast(worker));
    T_ASSERT(worker->build == near_building);
    T_ASSERT(worker->build != far_building);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_autocast_can_choose_damaged_mechanical_unit) {
    LPEDICT worker;
    LPEDICT mechanical;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    AbilityData_t *repair_data;
    ability_t const *repair;
    UnitBalance_t balance;
    UnitData_t data;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    repair_data = (AbilityData_t *)G_AbilityData(MAKEFOURCC('A','r','e','n'));
    T_NOT_NULL(repair_data);
    repair_data->level[0].targs = building_repair_stock_targets;

    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    mechanical = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 96, 0);
    worker->data.UnitAbilities = &abilities;
    worker->runtime.acquisition_range = 400.0f;
    worker->collision = 16.0f;

    balance = *mechanical->data.UnitBalance;
    balance.type = "mechanical";
    balance.reptm = 10;
    mechanical->data.UnitBalance = &balance;
    data = *mechanical->data.UnitData;
    data.moveTypeName = "foot";
    mechanical->data.UnitData = &data;
    mechanical->targtype = TARG_GROUND;
    mechanical->s.player = worker->s.player;
    mechanical->collision = 16.0f;
    mechanical->health.max_value = 1000.0f;
    mechanical->health.value = 500.0f;

    gi.LinkEntity(worker);
    gi.LinkEntity(mechanical);
    repair = FindAbilityForCommand("Aren");
    T_NOT_NULL(repair);
    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), true));
    T_ASSERT(G_TryUnitAutocast(worker));
    T_ASSERT(worker->build == mechanical);

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
    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), true));
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
    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), true));
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
    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), true));
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
    T_ASSERT(G_SetUnitAutocast(worker, FS_SLKKey(repair->classname), true));
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
    building->construction.type = CONSTRUCTION_ORC;

    T_ASSERT(!S_OrderRepair(standard, building, MAKEFOURCC('A','r','e','n')));
    T_ASSERT(!S_OrderRepair(human, building, MAKEFOURCC('A','r','e','p')));
    building->construction.type = CONSTRUCTION_HUMAN;
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
    repair->level[0].range = 50.0f;

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

    interaction = worker->collision + building->collision + repair->level[0].range;
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

    /* No progress means no visual drift while construction is paused. */
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
    repair->level[0].data[3].number = 0.0f;

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
