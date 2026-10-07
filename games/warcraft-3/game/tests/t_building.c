#ifdef BZ_TESTS
#include "test.h"
#include "../g_local.h"
#include "../hud/hud_local.h"
#include "../skills/s_skills.h"
#include "jass/jass.h"

edict_t *alloc_test_unit(uint32_t class_id, float x, float y);
void setup_test_world(void);
void setup_test_pathmap(uint32_t width, uint32_t height, uint8_t const *cells);
void repair_build_primary(edict_t *ent, edict_t *building);
void repair_build_legacy(edict_t *ent, edict_t *building);
void build_build(edict_t *ent);
void ai_train_build(edict_t *ent);
bool build_menu_send_builder(edict_t *clent, vec2_t const *location);
void build_menu_selectlocation(edict_t *ent, uint32_t building_id);
slkTestData_t *parse_slk_string(char const *slk_text);
void free_slk_rows(slkTestData_t *rows);
bool run_test_jass(cstring_t src);

static uint32_t building_stand_calls;
static uiFrame_t building_command_frame;
static uiCommandButton_t building_command_state;
static bool building_command_frame_seen;
static uiFrame_t building_command_number_frame;
static uiLabel_t building_command_number_label;
static char building_command_number_text[16];
static bool building_command_number_seen;
static bool building_cursor_opcode_seen;
static bool building_cursor_clear_seen;
static bool building_cursor_entity_seen;
static uint32_t building_cursor_player;
static uint16_t building_cursor_effect_flags;
static PATHSTR building_image_path;
static uint32_t building_queue_frame_count;
static uint16_t building_queue_buildtimer;
static uint8_t building_queue_numitems;
static uint32_t building_queue_starttime;
static uint32_t building_queue_endtime;
static PATHSTR building_sound_path;
static uint32_t building_commandbar_layouts;
static int32_t building_last_byte;

static int building_test_sound_index(cstring_t path) {
    snprintf(building_sound_path, sizeof(building_sound_path), "%s", path ? path : "");
    return 91;
}
static int building_test_sound_index_alias(cstring_t path, cstring_t alias) { (void)alias; return building_test_sound_index(path); }


static void building_test_stand(edict_t *ent) {
    (void)ent;
    building_stand_calls++;
}

static int building_test_image_index(cstring_t name) {
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
        building_cursor_opcode_seen = *(int32_t const *)value == svc_cursor;
        return;
    }
    if (type == PF_ENTITY && building_cursor_opcode_seen) {
        entityState_t const *cursor = value;
        building_cursor_clear_seen = cursor->model == 0;
        if (cursor->model != 0) {
            building_cursor_entity_seen = true;
            building_cursor_player = cursor->player;
            building_cursor_effect_flags = cursor->effect_flags;
        }
        building_cursor_opcode_seen = false;
    }
}

static void building_queue_capture_write(pfWriteType_t type, void const *value) {
    uiFrame_t const *frame;
    uiBuildQueue_t const *queue;

    if (type != PF_UIFRAME || !value) return;
    frame = value;
    if (frame->flags.type != FT_BUILDQUEUE || !frame->buffer.data ||
        frame->buffer.size < sizeof(uiBuildQueue_t) + sizeof(uiBuildQueueItem_t)) return;
    queue = frame->buffer.data;
    building_queue_frame_count++;
    building_queue_buildtimer = queue->buildtimer;
    building_queue_numitems = queue->numitems;
    building_queue_starttime = queue->items[0].starttime;
    building_queue_endtime = queue->items[0].endtime;
}

static void building_test_unicast(edict_t *ent) { (void)ent; }

/* Counts svc_layout payloads that open the command bar: the only writer of
 * that pair during spell targeting setup is UI_AddCancelButton. */
static void building_layout_capture_write(pfWriteType_t type, void const *value) {
    int32_t byte;
    if (type != PF_BYTE || !value) { building_last_byte = -1; return; }
    byte = *(int32_t const *)value;
    if (building_last_byte == svc_layout && byte == LAYER_COMMANDBAR) building_commandbar_layouts++;
    building_last_byte = byte;
}

static cstring_t building_all_cvar(cstring_t name, cstring_t fallback) {
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

static cstring_t const building_repair_stock_targets =
    "ground,air,structure,mechanical,friend,nonancient,invulnerable,vulnerable";
static cstring_t const building_renew_stock_targets =
    "ground,air,structure,mechanical,friend,invulnerable,vulnerable";

static const char building_upgrade_slk[] =
    "ID;PWXL;N;E\n"
    "B;X18;Y12;D0\n"
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
    "C;X18;K\"comments\"\n"
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
    "C;X1;Y9;K\"Rhri\"\n"
    "C;X2;K\"range\"\n"
    "C;X3;K3\n"
    "C;X10;K\"ratr\"\n"
    "C;X11;K137\n"
    "C;X12;K11\n"
    "C;X1;Y10;K\"Rhan\"\n"
    "C;X2;K\"health\"\n"
    "C;X3;K3\n"
    "C;X10;K\"rhpx\"\n"
    "C;X11;K73\n"
    "C;X12;K17\n"
    "C;X1;Y11;K\"Rhde\"\n"
    "C;X2;K\"ability\"\n"
    "C;X3;K1\n"
    "C;X10;K\"rlev\"\n"
    "C;X11;K0\n"
    "C;X12;K0\n"
    "C;X13;K\"Adef\"\n"
    "C;X14;K\"rlev\"\n"
    "C;X15;K0\n"
    "C;X16;K0\n"
    "C;X17;K\"Amic\"\n"
    "C;X1;Y12;K\"Ruac\"\n"
    "C;X2;K\"ability\"\n"
    "C;X3;K1\n"
    "C;X10;K\"_\"\n"
    "C;X18;K\"undead ghoul cannibalize\"\n"
    "E\n";

static const char building_dependency_ability_slk[] =
    "ID;PWXL;N;E\n"
    "B;X4;Y3;D0\n"
    "C;X1;Y1;K\"alias\"\n"
    "C;X3;K\"checkDep\"\n"
    "C;X4;K\"comments\"\n"
    "C;X1;Y2;K\"Acan\"\n"
    "C;X3;K1\n"
    "C;X4;K\"Cannibalize\"\n"
    "C;X1;Y3;K\"Axyz\"\n"
    "C;X3;K1\n"
    "C;X4;K\"Cannibalize\"\n"
    "E\n";

/* These stock caster abilities and upgrades are absent from the compact test
 * archive. Install rows so the research gate tests exercise parsed game data. */
static const char building_caster_ability_slk[] =
    "ID;PWXL;N;E\n"
    "B;X4;Y5;D0\n"
    "C;X1;Y1;K\"alias\"\n"
    "C;X2;K\"code\"\n"
    "C;X3;K\"checkDep\"\n"
    "C;X4;K\"comments\"\n"
    "C;X1;Y2;K\"Aivs\"\nC;X2;K\"Aivs\"\nC;X3;K1\nC;X4;K\"Invisibility\"\n"
    "C;X1;Y3;K\"Aply\"\nC;X2;K\"Aply\"\nC;X3;K1\nC;X4;K\"Polymorph\"\n"
    "C;X1;Y4;K\"Adis\"\nC;X2;K\"Adis\"\nC;X3;K1\nC;X4;K\"Dispel Magic\"\n"
    "C;X1;Y5;K\"Ainf\"\nC;X2;K\"Ainf\"\nC;X3;K1\nC;X4;K\"Inner Fire\"\n"
    "E\n";

static const char building_caster_upgrade_slk[] =
    "ID;PWXL;N;E\n"
    "B;X3;Y3;D0\n"
    "C;X1;Y1;K\"upgradeid\"\nC;X3;K\"maxlevel\"\n"
    "C;X1;Y2;K\"Rhst\"\nC;X3;K2\n"
    "C;X1;Y3;K\"Rhpt\"\nC;X3;K2\n"
    "E\n";

typedef struct {
    slkTestData_t *old_abilities;
    slkTestData_t *old_upgrades;
    slkTestData_t *abilities;
    slkTestData_t *upgrades;
} buildingCasterRows_t;

static buildingCasterRows_t building_install_caster_data(void) {
    buildingCasterRows_t rows = {0};
    rows.abilities = parse_slk_string(building_caster_ability_slk);
    rows.upgrades = parse_slk_string(building_caster_upgrade_slk);
    rows.old_abilities = G_SetSLKRows("AbilityData", rows.abilities);
    rows.old_upgrades = G_SetSLKRows("UpgradeData", rows.upgrades);
    return rows;
}

static void building_restore_caster_data(buildingCasterRows_t rows) {
    G_SetSLKRows("UpgradeData", rows.old_upgrades);
    G_SetSLKRows("AbilityData", rows.old_abilities);
    free_slk_rows(rows.upgrades);
    free_slk_rows(rows.abilities);
}

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

TEST(wc3_building, tiny_structure_resolves_stock_endpoints_and_great_hall_by_race) {
    const char ability_slk[] =
        "ID;PWXL;N;EBB;Y9;X4\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\nC;Y1;X4;K\"UnitID1\"\n"
        "C;Y2;X1;K\"AIbl\"\nC;Y2;X2;K\"AIbl\"\nC;Y2;X3;K1\nC;Y2;X4;K\"hcas\"\n"
        "C;Y3;X1;K\"AIbg\"\nC;Y3;X2;K\"AIbl\"\nC;Y3;X3;K1\nC;Y3;X4;K\"htow\"\n"
        "C;Y4;X1;K\"AIbt\"\nC;Y4;X2;K\"AIbl\"\nC;Y4;X3;K1\nC;Y4;X4;K\"hwtw\"\n"
        "C;Y5;X1;K\"AIbb\"\nC;Y5;X2;K\"AIbl\"\nC;Y5;X3;K1\nC;Y5;X4;K\"hbla\"\n"
        "C;Y6;X1;K\"AIbf\"\nC;Y6;X2;K\"AIbl\"\nC;Y6;X3;K1\nC;Y6;X4;K\"hhou\"\n"
        "C;Y7;X1;K\"AIbr\"\nC;Y7;X2;K\"AIbl\"\nC;Y7;X3;K1\nC;Y7;X4;K\"hlum\"\n"
        "C;Y8;X1;K\"AIbs\"\nC;Y8;X2;K\"AIbl\"\nC;Y8;X3;K1\nC;Y8;X4;K\"hbar\"\n"
        "C;Y9;X1;K\"AIbh\"\nC;Y9;X2;K\"AIbl\"\nC;Y9;X3;K1\nC;Y9;X4;K\"halt\"\nE\n";
    const char missing_unit_id_slk[] =
        "ID;PWXL;N;EBB;Y2;X3\n"
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y2;X1;K\"AIbg\"\nC;Y2;X2;K\"AIbl\"\nC;Y2;X3;K1\nE\n";
    uint32_t const great_hall = MAKEFOURCC('A','I','b','g');
    uint32_t const great_hall_races[] = {
        MAKEFOURCC('h','t','o','w'), MAKEFOURCC('o','g','r','e'),
        MAKEFOURCC('u','n','p','l'), MAKEFOURCC('e','t','o','l')
    };
    uint32_t const races[] = {
        kPlayerRaceHuman, kPlayerRaceOrc, kPlayerRaceUndead, kPlayerRaceNightElf
    };
    uint32_t const codes[] = {
        MAKEFOURCC('A','I','b','l'), MAKEFOURCC('A','I','b','t'),
        MAKEFOURCC('A','I','b','b'), MAKEFOURCC('A','I','b','f'),
        MAKEFOURCC('A','I','b','r'), MAKEFOURCC('A','I','b','s'),
        MAKEFOURCC('A','I','b','h')
    };
    uint32_t const structures[] = {
        MAKEFOURCC('h','c','a','s'), MAKEFOURCC('h','w','t','w'),
        MAKEFOURCC('h','b','l','a'), MAKEFOURCC('h','h','o','u'),
        MAKEFOURCC('h','l','u','m'), MAKEFOURCC('h','b','a','r'),
        MAKEFOURCC('h','a','l','t')
    };
    slkTestData_t *rows, *old;
    slkTestData_t *missing_rows, *saved_rows;
    edict_t *caster;

    setup_test_world();
    rows = parse_slk_string(ability_slk);
    old = G_SetSLKRows("AbilityData", rows);
    caster = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0.0f, 0.0f);
    caster->s.player = 0;
    for (uint32_t race = 0; race < sizeof(races) / sizeof(races[0]); race++) {
        game.clients[0].ps.race = races[race];
        T_EQ(S_TinyStructureUnitId(caster, great_hall, 1), great_hall_races[race]);
        T_EQ(S_TinyStructureUnitId(caster, codes[0], 1), structures[0]);
    }
    for (uint32_t i = 1; i < sizeof(codes) / sizeof(codes[0]); i++)
        T_EQ(S_TinyStructureUnitId(caster, codes[i], 1), structures[i]);

    /* Missing authored endpoints stay unresolved; race handling must not
     * silently invent a building type. */
    missing_rows = parse_slk_string(missing_unit_id_slk);
    saved_rows = G_SetSLKRows("AbilityData", missing_rows);
    T_EQ(S_TinyStructureUnitId(caster, great_hall, 1), 0);
    G_SetSLKRows("AbilityData", saved_rows);
    free_slk_rows(missing_rows);

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_building, hud_texture_paths_are_authored_per_recipient) {
    stbIniCache_t old = game.config.theme, custom = {0};
    gameClient_t *previous = ui_current_client;
    gameClient_t client = { .ps.race = kPlayerRaceHuman };
    int (*old_index)(cstring_t) = gi.ImageIndex;
    PATHSTR old_key, old_name;
    bool old_dec = hud.image_decorated[1];
    strcpy(old_key, hud.image_key[1]); strcpy(old_name, hud.image_name[1]);
    T_ASSERT(Stb_IniCacheLoad(&custom, "TestData\\HudSkin.txt"));
    game.config.theme = custom; gi.ImageIndex = building_test_image_index;
    hud.image_key[1][0] = 0; UI_SetCurrentClient(&client);
    uint32_t image = UI_LoadTexture("Background", true);
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

TEST(wc3_building, construction_and_upgrade_keep_progress_queue_transport) {
    gameClient_t *client = &game.clients[0];
    edict_t *building;
    UnitBalance_t balance;
    umove_t birth = { .animation = "birth", .think = ai_birth };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;

    setup_test_world();
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    balance = *building->data.UnitBalance;
    balance.buildTime = 100;
    building->data.UnitBalance = &balance;
    building->s.player = client->ps.number;
    building->build = building;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->currentmove = &birth;
    building->health.value = building->health.max_value * 0.5f;
    gi.Write = building_queue_capture_write;
    gi.ImageIndex = building_test_image_index;

    /* The active construction hides queue slots but must still publish the
     * FT_BUILDQUEUE payload that drives the client progress bar. */
    building_queue_frame_count = 0;
    UI_WriteStart(LAYER_INFOPANEL);
    UI_WriteBuildQueue(building, client);
    T_EQ(building_queue_frame_count, 1);
    T_EQ(building_queue_numitems, 1);
    T_ASSERT(building_queue_buildtimer != 0);
    T_ASSERT(building_queue_endtime > building_queue_starttime);

    building->build = NULL;
    G_FreeConstruction(building);
    building->currentmove = NULL;
    if (!building->research) building->research = G_AllocResearch();
    assert(building->research);
    building->research->upgrade = building->class_id;
    building->research->duration = 100.0f;
    building->research->progress = 50.0f;

    /* In-place upgrades use the same transport even though their waiting
     * queue backdrop is hidden. */
    building_queue_frame_count = 0;
    UI_WriteStart(LAYER_INFOPANEL);
    UI_WriteBuildQueue(building, client);
    T_EQ(building_queue_frame_count, 1);
    T_EQ(building_queue_numitems, 1);
    T_ASSERT(building_queue_buildtimer != 0);
    T_ASSERT(building_queue_endtime > building_queue_starttime);

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, selected_building_rebuilds_info_panel_for_construction_and_upgrade) {
    gameClient_t *client = &game.clients[0];
    edict_t *player = &g_edicts[0];
    edict_t *building;
    UnitBalance_t balance;
    umove_t birth = { .animation = "birth", .think = ai_birth };
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;

    reset_entities();
    setup_test_world();
    player->client = client;
    client->connected = true;
    client->ps.number = 0;
    building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 0, 0);
    balance = *building->data.UnitBalance;
    balance.isBuilding = true;
    balance.buildTime = 100;
    building->data.UnitBalance = &balance;
    building->s.player = client->ps.number;
    G_SelectEntity(client, building);

    /* This is the cache state of an ordinary selected-unit panel immediately
     * before the selected building enters construction. */
    client->infopanel.entity = building->s.number;
    client->infopanel.hp = (int32_t)building->health.value;
    client->infopanel.xp = 0;
    building->build = building;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->currentmove = &birth;
    G_InvalidateUnitInfoPanel(building);
    T_ASSERT(G_GetMainSelectedUnit(client) == building);
    T_ASSERT(UI_TestUsesBuildingQueuePanel(client, building));

    building_queue_frame_count = 0;
    gi.Write = building_queue_capture_write;
    gi.unicast = building_test_unicast;
    gi.ImageIndex = building_test_image_index;
    G_RefreshInfoPanel(player);
    T_EQ(building_queue_frame_count, 1);

    building->build = NULL;
    G_FreeConstruction(building);
    building->currentmove = NULL;
    if (!building->research) building->research = G_AllocResearch();
    assert(building->research);
    building->research->upgrade = building->class_id;
    building->research->duration = 100.0f;
    building->research->progress = 50.0f;
    G_InvalidateUnitInfoPanel(building);

    building_queue_frame_count = 0;
    G_RefreshInfoPanel(player);
    T_EQ(building_queue_frame_count, 1);

    gi.Write = old_write;
    gi.unicast = old_unicast;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, unsummoning_refreshes_training_queue_progress_panel) {
    gameClient_t *client = &game.clients[0];
    edict_t *player = &g_edicts[0];
    edict_t *building;
    edict_t *trainee;
    UnitBalance_t balance;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;

    reset_entities();
    setup_test_world();
    player->client = client;
    client->connected = true;
    client->ps.number = 0;
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    trainee = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    balance = *building->data.UnitBalance;
    balance.isBuilding = true;
    balance.buildTime = 100;
    building->data.UnitBalance = &balance;
    building->s.player = trainee->s.player = client->ps.number;
    building->build = trainee;
    trainee->training = true;
    if (!trainee->food) trainee->food = G_AllocFood();
    assert(trainee->food);
    trainee->food->used = 1;
    trainee->health.max_value = 100.0f;
    trainee->health.value = 50.0f;
    building->abilstatus[0] = (heroabilitystatus_t){
        .code = MAKEFOURCC('B','u','n','s'), .level = 1 };
    G_SelectEntity(client, building);
    client->infopanel.entity = 0;
    client->infopanel.hp = -1;

    building_queue_frame_count = 0;
    gi.Write = building_queue_capture_write;
    gi.ImageIndex = building_test_image_index;
    G_RefreshInfoPanel(player);
    T_EQ(building_queue_frame_count, 1);
    level.time += 1000;
    G_RefreshInfoPanel(player);
    T_EQ(building_queue_frame_count, 2);
    T_ASSERT(building_queue_endtime > building_queue_starttime);

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, player_tech_state_tracks_max_and_researched_levels) {
    gameClient_t *client = &game.clients[0];
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

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
    gameClient_t *client = &game.clients[0];
    uint32_t const source_id = MAKEFOURCC('h','b','a','r');
    uint32_t const target_id = MAKEFOURCC('o','t','r','b');
    UnitProfile_t profile = { .upgrade = "otrb" };
    buildingMorphRows_t rows;
    edict_t *building;
    int32_t gold = 0, lumber = 0, food = 0;
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
    T_EQ(building->research->upgrade, target_id);
    T_FEQ(building->research->duration, 140.0f, 0.001f);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_GOLD], 280);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER], 340);
    T_EQ(building->food->used, 3);
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
    T_EQ(building->food->used, 2);
    T_EQ(G_GetPlayerTechInProgress(client, target_id), 0);
    T_ASSERT(!(building->aiflags & AI_HOLD_FRAME));
    T_EQ(level.events.write, 4);
    T_EQ(level.events.queue[2].type, EVENT_PLAYER_UNIT_UPGRADE_CANCEL);
    T_EQ(level.events.queue[3].type, EVENT_UNIT_UPGRADE_CANCEL);

    building_restore_morph_data(rows);
}

TEST(wc3_building, instant_build_cheat_completes_building_upgrade_on_next_frame) {
    gameClient_t *client = &game.clients[0];
    uint32_t const source_id = MAKEFOURCC('h','b','a','r');
    uint32_t const target_id = MAKEFOURCC('o','t','r','b');
    UnitProfile_t profile = { .upgrade = "otrb" };
    buildingMorphRows_t rows;
    edict_t *building;

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
    T_FEQ(building->research->progress, 0.0f, 0.001f);

    G_RunBuildingUpgradeFrame(building);

    T_ASSERT(!G_BuildingUpgradeActive(building));
    T_EQ(building->class_id, target_id);
    T_EQ(G_GetPlayerTechInProgress(client, target_id), 0);

    client->cheat_instant_build = false;
    building_restore_morph_data(rows);
}

TEST(wc3_building, building_upgrade_completion_morphs_in_place_and_preserves_health_ratio) {
    gameClient_t *client = &game.clients[0];
    uint32_t const source_id = MAKEFOURCC('h','b','a','r');
    uint32_t const target_id = MAKEFOURCC('o','t','r','b');
    UnitProfile_t profile = { .upgrade = "otrb" };
    buildingMorphRows_t rows;
    edict_t *building;
    edict_t *identity;

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
    if (!building->research) building->research = G_AllocResearch();
    assert(building->research);
    building->research->progress = building->research->duration;
    G_RunBuildingUpgradeFrame(building);

    T_ASSERT(building == identity);
    T_ASSERT(!G_BuildingUpgradeActive(building));
    T_EQ(building->class_id, target_id);
    T_FEQ(building->health.max_value, 1500.0f, 0.001f);
    T_FEQ(building->health.value, 750.0f, 0.001f);
    T_EQ(building->food->used, 3);
    T_EQ(building->food->made, 14);
    T_EQ(G_GetPlayerTechInProgress(client, target_id), 0);
    T_EQ(G_GetPlayerTechCountValue(client, target_id), 1);
    T_EQ(level.events.write, 4);
    T_EQ(level.events.queue[2].type, EVENT_PLAYER_UNIT_UPGRADE_FINISH);
    T_EQ(level.events.queue[3].type, EVENT_UNIT_UPGRADE_FINISH);

    building_restore_morph_data(rows);
}

TEST(wc3_building, research_state_uses_upgrade_cost_progression_and_player_lock) {
    gameClient_t *client = &game.clients[0];
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    UnitProfile_t profile = { .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const upgrade = MAKEFOURCC('R','h','m','e');
    int32_t next_level = 0;
    char reason[128];

    memset(client->tech, 0, sizeof(client->tech));
    producer->data.UnitProfile = &profile;
    producer->s.flags |= EF_BUILDING;
    producer->runtime.flags |= UNIT_BALANCE_BUILDING;
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
    gameClient_t *client = &game.clients[0];
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const upgrade = MAKEFOURCC('R','h','m','e');
    char tooltip[1024];
    int (*old_image_index)(cstring_t) = gi.ImageIndex;

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
    gameClient_t *client = &game.clients[0];
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    UnitProfile_t profile = { .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const upgrade = MAKEFOURCC('R','h','m','e');

    memset(client->tech, 0, sizeof(client->tech));
    producer->data.UnitProfile = &profile;
    producer->s.flags |= EF_BUILDING;
    producer->runtime.flags |= UNIT_BALANCE_BUILDING;
    producer->s.player = client->ps.number;
    producer->stand = building_test_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 500;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 500;

    T_ASSERT(G_QueueResearch(producer, upgrade));
    T_NOT_NULL(producer->build);
    T_ASSERT(producer->build->research->upgrade != 0);
    T_EQ(producer->build->research->upgrade, upgrade);
    T_EQ(producer->build->research->level, 1);
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

TEST(wc3_building, production_uses_work_animation) {
    edict_t *producer;

    setup_test_world();
    producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);

    TrainSetBuildMove(producer);

    T_NOT_NULL(producer->currentmove);
    T_STREQ(producer->currentmove->animation, "stand work");
}

TEST(wc3_building, instant_build_cheat_completes_research_on_next_tick) {
    gameClient_t *client = &game.clients[0];
    edict_t *producer;
    UnitProfile_t profile = { .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old;
    uint32_t const upgrade = MAKEFOURCC('R','h','m','e');

    setup_test_world();
    producer = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    producer->s.flags |= EF_BUILDING;
    producer->runtime.flags |= UNIT_BALANCE_BUILDING;
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
    T_ASSERT(producer->build->research->duration > 0.0f);
    T_NOT_NULL(producer->currentmove);
    T_NOT_NULL(producer->currentmove->think);

    producer->currentmove->think(producer);

    T_NULL(producer->build);
    T_EQ(G_GetPlayerTechInProgress(client, upgrade), 0);
    T_EQ(G_GetPlayerTechResearchedLevel(client, upgrade), 1);

    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, research_events_publish_producer_and_rawcode_context) {
    gameClient_t *client = &game.clients[0];
    edict_t *producer;
    UnitProfile_t profile = { .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old;
    uint32_t const upgrade = MAKEFOURCC('R','h','m','e');

    setup_test_world();
    producer = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    producer->s.flags |= EF_BUILDING;
    producer->runtime.flags |= UNIT_BALANCE_BUILDING;
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
    T_EQ((uint32_t)level.events.queue[0].value, upgrade);
    T_EQ((uint32_t)level.events.queue[1].value, upgrade);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "VerifyStart", false);
    T_ASSERT(!jass_rterror_pending(level.vm));

    T_ASSERT(G_CancelTrainingQueueItem(producer, 0, true));
    T_EQ(level.events.write, 4);
    T_EQ(level.events.queue[2].type, EVENT_PLAYER_UNIT_RESEARCH_CANCEL);
    T_EQ(level.events.queue[3].type, EVENT_UNIT_RESEARCH_CANCEL);
    T_EQ((uint32_t)level.events.queue[2].value, upgrade);
    T_EQ((uint32_t)level.events.queue[3].value, upgrade);
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
    if (!producer->build->research) producer->build->research = G_AllocResearch();
    assert(producer->build->research);
    producer->build->research->duration = 0.0f;
    T_NOT_NULL(producer->currentmove);
    T_NOT_NULL(producer->currentmove->think);
    producer->currentmove->think(producer);
    T_EQ(level.events.write, 8);
    T_EQ(level.events.queue[6].type, EVENT_PLAYER_UNIT_RESEARCH_FINISH);
    T_EQ(level.events.queue[7].type, EVENT_UNIT_RESEARCH_FINISH);
    T_EQ((uint32_t)level.events.queue[6].value, upgrade);
    T_EQ((uint32_t)level.events.queue[7].value, upgrade);
    G_RunEvents();
    jass_runevents(level.vm);
    jass_callbyname(level.vm, "VerifyFinish", false);
    T_ASSERT(!jass_rterror_pending(level.vm));
    T_EQ(G_GetPlayerTechResearchedLevel(client, upgrade), 1);

    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, researched_blacksmith_effects_update_existing_and_future_units) {
    gameClient_t *client = &game.clients[0];
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *future;
    UnitBalance_t balance = {
        .upgrades = "Rhme,Rhar",
        .armorPerUpgrade = 2.0f
    };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const weapon = MAKEFOURCC('R','h','m','e');
    uint32_t const armor = MAKEFOURCC('R','h','a','r');

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
    gameClient_t *client = &game.clients[0];
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    UnitBalance_t balance = { .upgrades = "Rhat" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const attack_damage = MAKEFOURCC('R','h','a','t');

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

TEST(wc3_building, researched_attack_range_effect_updates_existing_and_future_units) {
    gameClient_t *client = &game.clients[0];
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 0, 0);
    edict_t *future;
    UnitBalance_t balance = { .upgrades = "Rhri" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const long_rifles = MAKEFOURCC('R','h','r','i');

    memset(client->tech, 0, sizeof(client->tech));
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->attack1.numberOfDice = 1;
    unit->attack1.range = 400.0f;
    unit->attack2.numberOfDice = 1;
    unit->attack2.range = 250.0f;

    G_SetPlayerTechResearched(client, long_rifles, 1);
    T_FEQ(unit->attack1.range, 537.0f, 0.001f);
    T_FEQ(unit->attack2.range, 387.0f, 0.001f);

    G_SetPlayerTechResearched(client, long_rifles, 3);
    T_FEQ(unit->attack1.range, 559.0f, 0.001f);
    T_FEQ(unit->attack2.range, 409.0f, 0.001f);

    future = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 0, 0);
    future->s.player = client->ps.number;
    future->data.UnitBalance = &balance;
    future->attack1.numberOfDice = 1;
    future->attack1.range = 400.0f;
    G_ApplyPlayerUpgradesToUnit(future);
    T_FEQ(future->attack1.range, 559.0f, 0.001f);

    G_SetPlayerTechResearched(client, long_rifles, 0);
    T_FEQ(unit->attack1.range, 400.0f, 0.001f);
    T_FEQ(unit->attack2.range, 250.0f, 0.001f);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, researched_hit_points_effect_preserves_health_ratio) {
    gameClient_t *client = &game.clients[0];
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 0, 0);
    edict_t *future;
    UnitBalance_t balance = { .upgrades = "Rhan" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const animal_war_training = MAKEFOURCC('R','h','a','n');

    memset(client->tech, 0, sizeof(client->tech));
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->health.max_value = 100.0f;
    unit->health.value = 50.0f;

    G_SetPlayerTechResearched(client, animal_war_training, 1);
    T_FEQ(unit->health.max_value, 173.0f, 0.001f);
    T_FEQ(unit->health.value, 86.5f, 0.001f);
    T_FEQ(unit->permanent_health_bonus, 73.0f, 0.001f);

    G_SetPlayerTechResearched(client, animal_war_training, 2);
    T_FEQ(unit->health.max_value, 190.0f, 0.001f);
    T_FEQ(unit->health.value, 95.0f, 0.001f);
    T_FEQ(unit->permanent_health_bonus, 90.0f, 0.001f);

    future = alloc_test_unit(MAKEFOURCC('h','k','n','i'), 0, 0);
    future->s.player = client->ps.number;
    future->data.UnitBalance = &balance;
    future->health.max_value = 200.0f;
    future->health.value = 100.0f;
    G_ApplyPlayerUpgradesToUnit(future);
    T_FEQ(future->health.max_value, 290.0f, 0.001f);
    T_FEQ(future->health.value, 145.0f, 0.001f);
    T_FEQ(future->permanent_health_bonus, 90.0f, 0.001f);

    G_SetPlayerTechResearched(client, animal_war_training, 0);
    T_FEQ(unit->health.max_value, 100.0f, 0.001f);
    T_FEQ(unit->health.value, 50.0f, 0.001f);
    T_FEQ(unit->permanent_health_bonus, 0.0f, 0.001f);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, researched_spell_level_effect_gates_and_levels_unit_ability) {
    static const char ability_slk[] =
        "ID;PWXL;N;E\nB;X2;Y3;D0\n"
        "C;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X1;Y2;K\"Adef\"\nC;X2;K\"Adef\"\n"
        "C;X1;Y3;K\"Amic\"\nC;X2;K\"Amic\"\nE\n";
    gameClient_t *client = &game.clients[0];
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    UnitBalance_t balance = { .upgrades = "Rhde" };
    UnitAbilities_t abilities = { .abilList = "Adef", .heroAbilList = "" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    slkTestData_t *ability_rows = parse_slk_string(ability_slk);
    slkTestData_t *old_abilities = G_SetSLKRows("AbilityData", ability_rows);
    uint32_t const defend_research = MAKEFOURCC('R','h','d','e');
    uint32_t const defend = MAKEFOURCC('A','d','e','f');

    memset(client->tech, 0, sizeof(client->tech));
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->data.UnitAbilities = &abilities;

    T_EQ(G_UnitAbilityLevel(unit, defend), 1);
    T_ASSERT(!G_UnitAbilityResearchAvailable(unit, defend));

    G_SetPlayerTechResearched(client, defend_research, 1);
    T_ASSERT(G_UnitAbilityResearchAvailable(unit, defend));
    T_EQ(G_UnitAbilityLevel(unit, defend), 2);

    G_SetPlayerTechResearched(client, defend_research, 0);
    T_ASSERT(!G_UnitAbilityResearchAvailable(unit, defend));
    T_EQ(G_UnitAbilityLevel(unit, defend), 1);

    G_SetSLKRows("AbilityData", old_abilities);
    free_slk_rows(ability_rows);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, unresearched_unit_ability_remains_visible_but_disabled) {
    gameClient_t *client;
    edict_t *unit;
    UnitBalance_t balance = { .upgrades = "Rhde" };
    UnitAbilities_t abilities = { .abilList = "Amic" };
    gameCommandButton_t buttons[16];
    slkTestData_t *rows = NULL, *old;
    uint32_t defend_research = MAKEFOURCC('R','h','d','e');
    uint8_t count;
    bool found;

    setup_test_world();
    old = building_install_upgrade_data(&rows);
    client = &game.clients[0];
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->data.UnitAbilities = &abilities;
    memset(client->tech, 0, sizeof(client->tech));

    count = G_GetCommandButtons(unit, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Amic")) {
        found = true;
        T_ASSERT(buttons[i].disabled);
    }
    T_ASSERT(found);

    G_SetPlayerTechResearched(client, defend_research, 1);
    count = G_GetCommandButtons(unit, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Amic")) {
        found = true;
        T_ASSERT(!buttons[i].disabled);
    }
    T_ASSERT(found);

    G_SetPlayerAbilityAvailable(client, FS_SLKKey("Amic"), false);
    count = G_GetCommandButtons(unit, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Amic")) found = true;
    T_ASSERT(!found);

    G_SetPlayerAbilityAvailable(client, FS_SLKKey("Amic"), true);
    count = G_GetCommandButtons(unit, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Amic")) {
        found = true;
        T_ASSERT(!buttons[i].disabled);
    }
    T_ASSERT(found);

    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, naga_production_buttons_have_distinct_command_card_cells) {
    static uint32_t const producers[] = {
        MAKEFOURCC('n','n','t','t'), MAKEFOURCC('n','n','s','g'), MAKEFOURCC('n','n','s','a')
    };
    static cstring_t const expected_trains[] = {
        "nmpe,nnmg", "nmyr,nsnp,nhyc", "nnsw,nwgs"
    };
    gameClient_t *client;
    gameCommandButton_t buttons[16];

    setup_test_world();
    client = &game.clients[0];
    client->connected = true;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 100000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 100000;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    for (uint32_t p = 0; p < sizeof(producers) / sizeof(producers[0]); p++) {
        edict_t *producer = alloc_test_unit(producers[p], 0.0f, 0.0f);
        UnitProfile_t const *profile = G_UnitProfile(producers[p]);
        uint8_t count;
        producer->s.player = client->ps.number;
        T_STREQ(profile->trains, expected_trains[p]);
        count = G_GetCommandButtons(producer, buttons, sizeof(buttons) / sizeof(buttons[0]));

        char expected[4][5] = {{0}};
        uint32_t expected_count = 0;
        cstring_t cursor = expected_trains[p];
        while (*cursor && expected_count < sizeof(expected) / sizeof(expected[0])) {
            uint32_t n = 0;
            while (*cursor && *cursor != ',' && n < 4) expected[expected_count][n++] = *cursor++;
            if (*cursor == ',') cursor++;
            if (n == 4) expected_count++;
        }
        for (uint32_t i = 0; i < expected_count; i++) {
            bool found = false;
            FOR_LOOP(j, count) {
                if (!strcmp(buttons[j].command, expected[i])) {
                    found = true;
                    break;
                }
            }
            T_ASSERT(found);
        }
        FOR_LOOP(i, count) FOR_LOOP(j, i) {
            T_ASSERT(buttons[i].x != buttons[j].x || buttons[i].y != buttons[j].y);
        }
    }
}

TEST(wc3_building, setplayerabilityavailable_hides_human05_polymorph_command) {
    gameClient_t *client;
    edict_t *sorceress;
    UnitAbilities_t abilities = { .abilList = "Aply" };
    gameCommandButton_t buttons[16];
    uint8_t count;
    bool found;

    setup_test_world();
    client = &game.clients[0];
    client->ps.number = 0;
    sorceress = alloc_test_unit(MAKEFOURCC('h','s','o','r'), 0, 0);
    sorceress->s.player = client->ps.number;
    sorceress->data.UnitAbilities = &abilities;

    count = G_GetCommandButtons(sorceress, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aply")) found = true;
    T_ASSERT(found);

    G_SetPlayerAbilityAvailable(client, FS_SLKKey("Aply"), false);
    {
        abilityitem_t item = S_AbilityItem(FS_SLKKey("Aply"));
        abilityCall_t call = MAKE(abilityCall_t, .item = &item);
        T_ASSERT(!S_AbilityMessage(sorceress, A_EXECUTE, &call));
        T_ASSERT(!S_AbilityMessage(sorceress, A_ORDER, &call));
    }
    count = G_GetCommandButtons(sorceress, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aply")) found = true;
    T_ASSERT(!found);

    G_SetPlayerAbilityAvailable(client, FS_SLKKey("Aply"), true);
    count = G_GetCommandButtons(sorceress, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aply")) found = true;
    T_ASSERT(found);
}

TEST(wc3_building, player_ability_availability_does_not_leak_to_unmapped_owner) {
    gameClient_t *fallback;
    edict_t *unit;
    UnitAbilities_t abilities = { .abilList = "Aply" };
    gameCommandButton_t buttons[16];
    uint8_t count;
    bool found;

    setup_test_world();
    fallback = &game.clients[MAX_PLAYERS - 1];
    unit = alloc_test_unit(MAKEFOURCC('h','s','o','r'), 0, 0);
    unit->s.player = MAX_PLAYERS; /* intentionally not mapped to a real client */
    unit->data.UnitAbilities = &abilities;

    G_SetPlayerAbilityAvailable(fallback, FS_SLKKey("Aply"), false);
    T_ASSERT(G_IsUnitAbilityAvailable(unit, FS_SLKKey("Aply")));
    count = G_GetCommandButtons(unit, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aply")) found = true;
    T_ASSERT(found);
    G_SetPlayerAbilityAvailable(fallback, FS_SLKKey("Aply"), true);
}

TEST(wc3_building, player_ability_availability_grows_past_initial_capacity) {
    static char const digits[] = "0123456789";
    enum { ABILITY_COUNT = 96 };
    gameClient_t *client;
    uint32_t abilities[ABILITY_COUNT];

    setup_test_world();
    client = &game.clients[0];
    for (uint32_t i = 0; i < ABILITY_COUNT; i++) {
        abilities[i] = MAKEFOURCC('A', '0', digits[i / 10], digits[i % 10]);
        G_SetPlayerAbilityAvailable(client, abilities[i], false);
        T_ASSERT(!G_IsPlayerAbilityAvailable(client, abilities[i]));
    }
    T_EQ(client->jass.disabled_ability_count, (uint32_t)ABILITY_COUNT);
    T_ASSERT(client->jass.disabled_ability_capacity >= ABILITY_COUNT);

    G_SetPlayerAbilityAvailable(client, abilities[32], false);
    T_EQ(client->jass.disabled_ability_count, (uint32_t)ABILITY_COUNT);
    G_SetPlayerAbilityAvailable(client, abilities[32], true);
    T_ASSERT(G_IsPlayerAbilityAvailable(client, abilities[32]));
    T_ASSERT(!G_IsPlayerAbilityAvailable(client, abilities[95]));

    FOR_LOOP(i, ABILITY_COUNT) G_SetPlayerAbilityAvailable(client, abilities[i], true);
    T_EQ(client->jass.disabled_ability_count, 0u);
}

TEST(wc3_building, caster_training_gates_sorceress_and_priest_spell_tiers) {
    buildingCasterRows_t caster_rows;
    gameClient_t *client;
    edict_t *sorceress, *priest;
    UnitBalance_t sorceress_balance = { .upgrades = "Rhst" };
    UnitBalance_t priest_balance = { .upgrades = "Rhpt" };
    UnitAbilities_t sorceress_abilities = { .abilList = "Aivs,Aply" };
    UnitAbilities_t priest_abilities = { .abilList = "Adis,Ainf" };
    gameCommandButton_t buttons[16];
    uint32_t invisibility = MAKEFOURCC('A','i','v','s');
    uint32_t polymorph = MAKEFOURCC('A','p','l','y');
    uint32_t dispel = MAKEFOURCC('A','d','i','s');
    uint32_t inner_fire = MAKEFOURCC('A','i','n','f');
    uint32_t sorceress_training = MAKEFOURCC('R','h','s','t');
    uint32_t priest_training = MAKEFOURCC('R','h','p','t');
    uint8_t count;
    bool found;

    setup_test_world();
    caster_rows = building_install_caster_data();
    client = &game.clients[0];
    client->ps.number = 0;
    memset(client->tech, 0, sizeof(client->tech));
    G_SetPlayerTechMaxAllowed(client, MAKEFOURCC('R','h','s','t'), 1);
    G_SetPlayerTechMaxAllowed(client, MAKEFOURCC('R','h','p','t'), 1);
    sorceress = alloc_test_unit(MAKEFOURCC('h','s','o','r'), 0, 0);
    sorceress->s.player = client->ps.number;
    sorceress->data.UnitBalance = &sorceress_balance;
    sorceress->data.UnitAbilities = &sorceress_abilities;
    priest = alloc_test_unit(MAKEFOURCC('h','m','p','r'), 64, 0);
    priest->s.player = client->ps.number;
    priest->data.UnitBalance = &priest_balance;
    priest->data.UnitAbilities = &priest_abilities;

    count = G_GetCommandButtons(sorceress, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aivs")) {
        found = true;
        T_ASSERT(buttons[i].disabled);
    }
    T_ASSERT(found);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aply")) found = true;
    T_ASSERT(!found);

    count = G_GetCommandButtons(priest, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Adis")) {
        found = true;
        T_ASSERT(buttons[i].disabled);
    }
    T_ASSERT(found);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Ainf")) found = true;
    T_ASSERT(!found);

    G_SetPlayerTechMaxAllowed(client, sorceress_training, 1);
    G_SetPlayerTechMaxAllowed(client, priest_training, 1);
    T_ASSERT(G_UnitAbilityResearchVisible(sorceress, invisibility));
    T_ASSERT(!G_UnitAbilityResearchVisible(sorceress, polymorph));
    T_ASSERT(G_UnitAbilityResearchVisible(priest, dispel));
    T_ASSERT(!G_UnitAbilityResearchVisible(priest, inner_fire));
    G_SetPlayerTechResearched(client, sorceress_training, 1);
    G_SetPlayerTechResearched(client, priest_training, 1);
    T_ASSERT(G_UnitAbilityResearchAvailable(sorceress, invisibility));
    T_ASSERT(G_UnitAbilityResearchAvailable(priest, dispel));
    G_SetPlayerTechResearched(client, sorceress_training, 0);
    G_SetPlayerTechResearched(client, priest_training, 0);

    G_SetPlayerTechMaxAllowed(client, sorceress_training, 2);
    G_SetPlayerTechMaxAllowed(client, priest_training, 2);
    T_ASSERT(G_UnitAbilityResearchVisible(sorceress, polymorph));
    T_ASSERT(!G_UnitAbilityResearchAvailable(sorceress, polymorph));
    T_ASSERT(G_UnitAbilityResearchVisible(priest, inner_fire));
    T_ASSERT(!G_UnitAbilityResearchAvailable(priest, inner_fire));
    count = G_GetCommandButtons(sorceress, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aply")) {
        found = true;
        T_ASSERT(buttons[i].disabled);
    }
    T_ASSERT(found);
    count = G_GetCommandButtons(priest, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Ainf")) {
        found = true;
        T_ASSERT(buttons[i].disabled);
    }
    T_ASSERT(found);
    {
        abilityitem_t item = S_AbilityItem(polymorph);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item);
        T_ASSERT(!S_AbilityMessage(sorceress, A_EXECUTE, &call));
    }

    G_SetPlayerTechResearched(client, sorceress_training, 2);
    G_SetPlayerTechResearched(client, priest_training, 2);
    T_ASSERT(G_UnitAbilityResearchAvailable(sorceress, polymorph));
    T_ASSERT(G_UnitAbilityResearchAvailable(priest, inner_fire));
    count = G_GetCommandButtons(sorceress, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aply")) {
        found = true;
        T_ASSERT(!buttons[i].disabled);
    }
    T_ASSERT(found);
    count = G_GetCommandButtons(priest, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Ainf")) {
        found = true;
        T_ASSERT(!buttons[i].disabled);
    }
    T_ASSERT(found);

    /* A completed tier remains unusable when the map lowers its maximum. */
    G_SetPlayerTechMaxAllowed(client, sorceress_training, 1);
    G_SetPlayerTechMaxAllowed(client, priest_training, 1);
    T_ASSERT(!G_UnitAbilityResearchVisible(sorceress, polymorph));
    T_ASSERT(!G_UnitAbilityResearchAvailable(sorceress, polymorph));
    T_ASSERT(!G_UnitAbilityResearchVisible(priest, inner_fire));
    T_ASSERT(!G_UnitAbilityResearchAvailable(priest, inner_fire));
    {
        abilityitem_t item = S_AbilityItem(polymorph);
        abilityCall_t call = MAKE(abilityCall_t, .item = &item);
        T_ASSERT(!S_AbilityMessage(sorceress, A_EXECUTE, &call));
    }
    count = G_GetCommandButtons(sorceress, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Aply")) found = true;
    T_ASSERT(!found);

    G_SetPlayerTechMaxAllowed(client, sorceress_training, 2);
    G_SetPlayerTechMaxAllowed(client, priest_training, 2);
    G_SetPlayerTechResearched(client, sorceress_training, 0);
    G_SetPlayerTechResearched(client, priest_training, 0);
    G_SetPlayerTechMaxAllowed(client, sorceress_training, -1);
    G_SetPlayerTechMaxAllowed(client, priest_training, -1);
    building_restore_caster_data(caster_rows);
}

TEST(wc3_building, rlev_dependency_uses_custom_upgrade_and_ability_rawcodes) {
    static const char upgrade_slk[] =
        "ID;PWXL;N;E\nB;X13;Y2;D0\n"
        "C;X1;Y1;K\"upgradeid\"\nC;X3;K\"maxlevel\"\n"
        "C;X10;K\"effect1\"\nC;X13;K\"code1\"\n"
        "C;X1;Y2;K\"Rxyz\"\nC;X3;K2\nC;X10;K\"rlev\"\nC;X13;K\"Axyz\"\nE\n";
    static const char ability_slk[] =
        "ID;PWXL;N;E\nB;X2;Y2;D0\n"
        "C;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X1;Y2;K\"Axyz\"\nC;X2;K\"Axyz\"\nE\n";
    UnitBalance_t balance = { .upgrades = "Rxyz" };
    UnitAbilities_t abilities = { .abilList = "Axyz" };
    edict_t *unit;
    slkTestData_t *rows = parse_slk_string(upgrade_slk), *old;
    slkTestData_t *ability_rows = parse_slk_string(ability_slk), *old_ability;
    uint32_t const training = MAKEFOURCC('R','x','y','z');
    uint32_t const custom_ability = MAKEFOURCC('A','x','y','z');

    setup_test_world();
    old = G_SetSLKRows("UpgradeData", rows);
    old_ability = G_SetSLKRows("AbilityData", ability_rows);
    unit = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    unit->s.player = game.clients[0].ps.number;
    unit->data.UnitBalance = &balance;
    unit->data.UnitAbilities = &abilities;
    T_ASSERT(!G_UnitAbilityResearchAvailable(unit, custom_ability));
    G_SetPlayerTechResearched(&game.clients[0], training, 1);
    T_ASSERT(G_UnitAbilityResearchAvailable(unit, custom_ability));
    G_SetPlayerTechResearched(&game.clients[0], training, 0);
    G_SetSLKRows("AbilityData", old_ability);
    G_SetSLKRows("UpgradeData", old);
    free_slk_rows(ability_rows);
    free_slk_rows(rows);
}

TEST(wc3_building, training_research_state_does_not_leak_to_unmapped_owner) {
    buildingCasterRows_t caster_rows;
    gameClient_t *fallback;
    edict_t *unit;
    UnitBalance_t balance = { .upgrades = "Rhst" };
    uint32_t const invisibility = MAKEFOURCC('A','i','v','s');
    uint32_t const training = MAKEFOURCC('R','h','s','t');

    setup_test_world();
    caster_rows = building_install_caster_data();
    fallback = &game.clients[MAX_PLAYERS - 1];
    unit = alloc_test_unit(MAKEFOURCC('h','s','o','r'), 0, 0);
    unit->s.player = MAX_PLAYERS;
    unit->data.UnitBalance = &balance;
    G_SetPlayerTechResearched(fallback, training, 1);
    T_ASSERT(!G_UnitAbilityResearchAvailable(unit, invisibility));
    G_SetPlayerTechResearched(fallback, training, 0);
    building_restore_caster_data(caster_rows);
}

TEST(wc3_building, town_hall_and_tree_of_life_show_train_and_upgrade_buttons) {
    static const char balance_slk[] =
        "ID;PWXL;N;E\nB;X5;Y7;D0\n"
        "C;X1;Y1;K\"unitBalanceID\"\nC;X2;K\"realHP\"\n"
        "C;X3;K\"goldcost\"\nC;X4;K\"lumbercost\"\nC;X5;K\"isbldg\"\n"
        "C;X1;Y2;K\"htow\"\nC;X2;K1000\nC;X5;K1\n"
        "C;X1;Y3;K\"hpea\"\nC;X2;K250\n"
        "C;X1;Y4;K\"hkee\"\nC;X2;K1400\nC;X5;K1\n"
        "C;X1;Y5;K\"etol\"\nC;X2;K1300\nC;X5;K1\n"
        "C;X1;Y6;K\"ewsp\"\nC;X2;K120\n"
        "C;X1;Y7;K\"etoa\"\nC;X2;K1600\nC;X5;K1\nE\n";
    static const char ui_slk[] =
        "ID;PWXL;N;E\nB;X3;Y5;D0\n"
        "C;X1;Y1;K\"unitUIID\"\nC;X2;K\"isbldg\"\nC;X3;K\"file\"\n"
        "C;X1;Y2;K\"htow\"\nC;X2;K1\nC;X3;K\"TestUI\\\\Models\\\\ui_panel.mdx\"\n"
        "C;X1;Y3;K\"hkee\"\nC;X2;K1\nC;X3;K\"TestUI\\\\Models\\\\ui_panel.mdx\"\n"
        "C;X1;Y4;K\"etol\"\nC;X2;K1\nC;X3;K\"TestUI\\\\Models\\\\ui_panel.mdx\"\n"
        "C;X1;Y5;K\"etoa\"\nC;X2;K1\nC;X3;K\"TestUI\\\\Models\\\\ui_panel.mdx\"\nE\n";
    static const char profile_slk[] =
        "ID;PWXL;N;E\nB;X3;Y5;D0\n"
        "C;X1;Y1;K\"id\"\nC;X2;K\"Trains\"\nC;X3;K\"Upgrade\"\n"
        "C;X1;Y2;K\"htow\"\nC;X2;K\"hpea\"\nC;X3;K\"hkee\"\n"
        "C;X1;Y3;K\"hpea\"\nC;X1;Y4;K\"hkee\"\n"
        "C;X1;Y5;K\"etol\"\nC;X2;K\"ewsp\"\nC;X3;K\"etoa\"\n"
        "C;X1;Y6;K\"ewsp\"\nC;X1;Y7;K\"etoa\"\nE\n";
    uint32_t const town_hall_id = MAKEFOURCC('h','t','o','w');
    uint32_t const tree_id = MAKEFOURCC('e','t','o','l');
    uint32_t const peasant_id = MAKEFOURCC('h','p','e','a');
    uint32_t const keep_id = MAKEFOURCC('h','k','e','e');
    uint32_t const wisp_id = MAKEFOURCC('e','w','s','p');
    uint32_t const ages_id = MAKEFOURCC('e','t','o','a');
    gameClient_t *client;
    edict_t *town_hall, *tree;
    /* Aent precedes Aro1 so rooting eligibility performs a nested ability-list
     * scan while the HUD is consuming this same parser's segment buffer. */
    UnitAbilities_t tree_abilities = { .abilList = "Aent,Aro1" };
    umove_t birth = { .think = ai_birth };
    gameCommandButton_t buttons[16];
    slkTestData_t *balance_rows, *old_balance, *ui_rows, *old_ui, *profile_rows, *old_profile;
    uint8_t count;
    bool found_train, found_upgrade;

    setup_test_world();
    balance_rows = parse_slk_string(balance_slk);
    ui_rows = parse_slk_string(ui_slk);
    profile_rows = parse_slk_string(profile_slk);
    old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    old_ui = G_SetSLKRows("UnitUI", ui_rows);
    old_profile = G_SetProfileRows(profile_rows);
    client = &game.clients[0];
    client->connected = true;
    client->ps.number = 0;
    town_hall = alloc_test_unit(town_hall_id, 0, 0);
    tree = alloc_test_unit(tree_id, 128, 0);
    town_hall->s.player = tree->s.player = client->ps.number;
    town_hall->data.UnitProfile = (UnitProfile_t *)G_UnitProfile(town_hall_id);
    tree->data.UnitProfile = (UnitProfile_t *)G_UnitProfile(tree_id);
    tree->data.UnitAbilities = &tree_abilities;
    tree->s.flags |= EF_BUILDING;
    tree->aiflags |= AI_IMMOBILE;
    town_hall->currentmove = tree->currentmove = &birth;

    T_ASSERT(G_ProducerCanTrain(town_hall, peasant_id));
    T_ASSERT(G_ProducerCanUpgrade(town_hall, keep_id));
    count = G_GetCommandButtons(town_hall, buttons, 16);
    found_train = found_upgrade = false;
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "hpea")) found_train = true;
        if (!strcmp(buttons[i].command, "hkee")) found_upgrade = true;
    }
    T_ASSERT(found_train);
    T_ASSERT(found_upgrade);

    T_ASSERT(S_AncientIsRooted(tree));
    T_STREQ(G_UnitProfile(tree_id)->trains, "ewsp");
    T_STREQ(G_UnitProfile(tree_id)->upgrade, "etoa");
    T_ASSERT(G_ProducerCanTrain(tree, wisp_id));
    T_ASSERT(G_ProducerCanUpgrade(tree, ages_id));
    T_EQ(G_GetTrainCommandState(client, tree, wisp_id, NULL, 0), BUILD_COMMAND_AVAILABLE);
    T_EQ(G_GetBuildingUpgradeCommandState(&(buildingUpgradeCommandParams_t){
        .client = client, .producer = tree, .unit_id = ages_id }), BUILD_COMMAND_AVAILABLE);
    count = G_GetCommandButtons(tree, buttons, 16);
    found_train = found_upgrade = false;
    FOR_LOOP(i, count) {
        if (!strcmp(buttons[i].command, "ewsp")) found_train = true;
        if (!strcmp(buttons[i].command, "etoa")) found_upgrade = true;
    }
    T_ASSERT(found_train);
    T_ASSERT(found_upgrade);

    G_SetSLKRows("UnitUI", old_ui);
    G_SetSLKRows("UnitBalance", old_balance);
    G_SetProfileRows(old_profile);
    free_slk_rows(ui_rows);
    free_slk_rows(balance_rows);
    free_slk_rows(profile_rows);
}

TEST(wc3_building, gate_only_dependency_disables_cannibalize_until_researched) {
    gameClient_t *client;
    edict_t *unit;
    UnitBalance_t balance = { .upgrades = "Ruac" };
    UnitAbilities_t abilities = { .abilList = "Acan" };
    gameCommandButton_t buttons[16];
    slkTestData_t *rows = NULL, *old, *ability_rows, *old_ability;
    uint32_t const cannibalize_research = MAKEFOURCC('R','u','a','c');
    uint32_t const cannibalize = MAKEFOURCC('A','c','a','n');
    uint8_t count;
    bool found;

    setup_test_world();
    old = building_install_upgrade_data(&rows);
    ability_rows = parse_slk_string(building_dependency_ability_slk);
    old_ability = G_SetSLKRows("AbilityData", ability_rows);
    client = &game.clients[0];
    unit = alloc_test_unit(MAKEFOURCC('u','g','h','o'), 0, 0);
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->data.UnitAbilities = &abilities;
    memset(client->tech, 0, sizeof(client->tech));

    T_ASSERT(!G_UnitAbilityResearchAvailable(unit, cannibalize));
    /* Shared comment words cannot create a relation for an unrelated rawcode. */
    T_ASSERT(G_UnitAbilityResearchAvailable(unit, MAKEFOURCC('A','x','y','z')));
    count = G_GetCommandButtons(unit, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Acan")) {
        found = true;
        T_ASSERT(buttons[i].disabled);
    }
    T_ASSERT(found);

    G_SetPlayerTechResearched(client, cannibalize_research, 1);
    T_ASSERT(G_UnitAbilityResearchAvailable(unit, cannibalize));
    count = G_GetCommandButtons(unit, buttons, 16);
    found = false;
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "Acan")) {
        found = true;
        T_ASSERT(!buttons[i].disabled);
    }
    T_ASSERT(found);

    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(ability_rows);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, researched_caster_mana_effects_update_existing_units) {
    gameClient_t *client = &game.clients[0];
    edict_t *unit = alloc_test_unit(MAKEFOURCC('h','s','o','r'), 0, 0);
    UnitBalance_t balance = { .upgrades = "Rhst", .manaRegen = 0.5f };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const training = MAKEFOURCC('R','h','s','t');

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

TEST(wc3_building, researched_mana_survives_hero_recompute_with_item_bonus) {
    gameClient_t *client = &game.clients[0];
    edict_t *unit, *future;
    UnitBalance_t balance;
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const training = MAKEFOURCC('R','h','s','t');

    setup_test_world();
    unit = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    balance = *unit->data.UnitBalance;
    balance.maxMana = 200.0f;
    balance.upgrades = "Rhst";
    memset(client->tech, 0, sizeof(client->tech));
    unit->s.player = client->ps.number;
    unit->data.UnitBalance = &balance;
    unit->hero.intel = balance.intelligence;
    unit->mana.max_value = 200.0f;
    unit->mana.value = 0.0f;

    G_SetPlayerTechResearched(client, training, 1);
    T_FEQ(unit->mana.max_value, 300.0f, 0.001f);
    G_ApplyTemporaryMaxManaBonus(unit, 50.0f);
    T_FEQ(unit->mana.max_value, 350.0f, 0.001f);

    future = alloc_test_unit(MAKEFOURCC('H','p','a','l'), 0, 0);
    future->s.player = client->ps.number;
    future->data.UnitBalance = &balance;
    future->hero.intel = balance.intelligence;
    future->mana.max_value = 200.0f;
    G_RecomputeHeroStats(future);
    G_ApplyPlayerUpgradesToUnit(future);
    T_FEQ(future->mana.max_value, 300.0f, 0.001f);

    unit->hero.intel++;
    G_RecomputeHeroStats(unit);
    T_FEQ(unit->mana.max_value, 365.0f, 0.001f);

    G_SetPlayerTechResearched(client, training, 0);
    T_FEQ(unit->mana.max_value, 265.0f, 0.001f);
    T_FEQ(future->mana.max_value, 200.0f, 0.001f);
    G_ApplyTemporaryMaxManaBonus(unit, -50.0f);
    T_FEQ(unit->mana.max_value, 215.0f, 0.001f);
    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, status_upgrade_families_follow_unit_upgrades_used) {
    gameClient_t *client = &game.clients[0];
    edict_t *footman = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 0, 0);
    edict_t *rifleman = alloc_test_unit(MAKEFOURCC('h','r','i','f'), 0, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','l','a'), 0, 0);
    edict_t *peasant = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    UnitBalance_t footman_balance = { .upgrades = "Rhme,Rhar" };
    UnitBalance_t rifleman_balance = { .upgrades = "Rhla,Rhra,Rhri,Rhpm,Rguv" };
    UnitBalance_t building_balance = { .upgrades = "Rhac,Rgfo" };
    UnitBalance_t peasant_balance = { .upgrades = "Rhlh,Rguv" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    uint32_t const melee = MAKEFOURCC('R','h','m','e');
    uint32_t const heavy_armor = MAKEFOURCC('R','h','a','r');
    uint32_t const ranged = MAKEFOURCC('R','h','r','a');
    uint32_t const light_armor = MAKEFOURCC('R','h','l','a');
    uint32_t const building_armor = MAKEFOURCC('R','h','a','c');

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
    gameClient_t *client = &game.clients[0];
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    edict_t *building = alloc_test_unit(barracks, 0, 0);

    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    G_SetPlayerTechResearched(client, barracks, 1);

    T_EQ(G_GetPlayerTechCountValue(client, barracks), 2);

    building->svflags |= SVF_DEADMONSTER;
    T_EQ(G_GetPlayerTechCountValue(client, barracks), 1);
}

TEST(wc3_building, building_charge_checks_and_deducts_resources) {
    gameClient_t *client = &game.clients[0];
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
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
    gameClient_t *client = &game.clients[0];
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
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
    T_STREQ(reason, "Nogold");

    worker_profile.builds = "hfoo";
    T_EQ(G_GetBuildCommandState(client, worker, barracks, reason, sizeof(reason)), BUILD_COMMAND_ABSENT);
}

TEST(wc3_building, mobile_builders_keep_race_build_menu_button) {
    static char const balance_slk[] =
        "ID;PWXL;N;E\nB;X2;Y5;D0\n"
        "C;X1;Y1;K\"unitBalanceID\"\nC;X2;K\"isbldg\"\n"
        "C;X1;Y2;K\"hbar\"\nC;X2;K1\n"
        "C;X1;Y3;K\"obar\"\nC;X2;K1\n"
        "C;X1;Y4;K\"uzig\"\nC;X2;K1\n"
        "C;X1;Y5;K\"earc\"\nC;X2;K1\nE\n";
    static char const profile_slk[] =
        "ID;PWXL;N;EBB;Y5;X2\n"
        "C;Y1;X1;K\"id\"\nC;Y1;X2;K\"Builds\"\n"
        "C;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"hbar\"\n"
        "C;Y3;X1;K\"opeo\"\nC;Y3;X2;K\"obar\"\n"
        "C;Y4;X1;K\"uaco\"\nC;Y4;X2;K\"uzig\"\n"
        "C;Y5;X1;K\"ewsp\"\nC;Y5;X2;K\"earc\"\nE\n";
    uint32_t const builders[] = {
        MAKEFOURCC('h','p','e','a'), MAKEFOURCC('o','p','e','o'),
        MAKEFOURCC('u','a','c','o'), MAKEFOURCC('e','w','s','p')
    };
    uint32_t const builders_count = sizeof(builders) / sizeof(builders[0]);
    slkTestData_t *balance_rows = parse_slk_string(balance_slk);
    slkTestData_t *old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    slkTestData_t *rows = parse_slk_string(profile_slk);
    slkTestData_t *old = G_SetProfileRows(rows);

    setup_test_world();
    game.clients[0].connected = true;
    game.clients[0].ps.number = 0;
    FOR_LOOP(i, builders_count) {
        edict_t *worker = alloc_test_unit(builders[i], 0.0f, 0.0f);
        worker->data.UnitProfile = (UnitProfile_t *)G_UnitProfile(builders[i]);
        worker->s.player = game.clients[0].ps.number;
        T_ASSERT(!(worker->runtime.flags & UNIT_BALANCE_BUILDING));
        T_ASSERT(G_UnitHasBuildMenu(worker));
    }

    G_SetProfileRows(old);
    G_SetSLKRows("UnitBalance", old_balance);
    free_slk_rows(rows);
    free_slk_rows(balance_rows);
}

TEST(wc3_building, build_menu_button_is_hidden_when_every_build_is_hidden) {
    static char const profile_slk[] =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"id\"\nC;Y1;X2;K\"Builds\"\n"
        "C;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"hbar\"\nE\n";
    gameClient_t *client = &game.clients[0];
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    slkTestData_t *rows = parse_slk_string(profile_slk);
    slkTestData_t *old = G_SetProfileRows(rows);
    gameCommandButton_t buttons[16];
    uint32_t const button_capacity = sizeof(buttons) / sizeof(buttons[0]);
    edict_t *worker;
    uint8_t count;
    bool found_build = false;

    setup_test_world();
    client->ps.number = 0;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0.0f, 0.0f);
    worker->s.player = client->ps.number;
    G_SetPlayerTechMaxAllowed(client, barracks, 0);

    T_EQ(G_GetBuildCommandState(client, worker, barracks, NULL, 0), BUILD_COMMAND_HIDDEN);
    T_ASSERT(!G_UnitHasBuildMenu(worker));
    count = G_GetCommandButtons(worker, buttons, button_capacity);
    FOR_LOOP(i, count) if (!strcmp(buttons[i].command, "CmdBuild")) found_build = true;
    T_ASSERT(!found_build);

    G_SetPlayerTechMaxAllowed(client, barracks, -1);
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;
    T_EQ(G_GetBuildCommandState(client, worker, barracks, NULL, 0), BUILD_COMMAND_UNAFFORDABLE);
    T_ASSERT(G_UnitHasBuildMenu(worker));

    G_SetProfileRows(old);
    free_slk_rows(rows);
}

TEST(wc3_building, race_building_commands_use_producer_profile_state) {
    uint32_t const producers[] = {
        MAKEFOURCC('h','b','a','r'), MAKEFOURCC('o','b','a','r'),
        MAKEFOURCC('u','z','i','g'), MAKEFOURCC('e','a','r','c')
    };
    uint32_t const producers_count = sizeof(producers) / sizeof(producers[0]);
    UnitProfile_t profile = { .trains = "hfoo", .researches = "Rhme" };

    setup_test_world();
    FOR_LOOP(i, producers_count) {
        edict_t *producer = alloc_test_unit(producers[i], 0.0f, 0.0f);
        producer->data.UnitProfile = &profile;
        /* A unit's authored producer profile remains the source for its
         * command card. EF_BUILDING is the live structure presentation bit;
         * it can change during Ancient mode transitions and must not erase
         * unrelated race building commands. Ancient availability is gated
         * separately by Root state. */
        producer->s.flags &= ~EF_BUILDING;
        T_ASSERT(G_ProducerCanTrain(producer, MAKEFOURCC('h','f','o','o')));
        T_ASSERT(G_ProducerCanResearch(producer, MAKEFOURCC('R','h','m','e')));
    }
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
    gameClient_t *client = &game.clients[0];
    edict_t *worker, *keep, *castle;
    UnitProfile_t worker_profile = { .builds = "hbar" };
    slkTestData_t *profile_rows = parse_slk_string(profile_slk);
    slkTestData_t *old_profile = G_SetProfileRows(profile_rows);
    uint32_t const blacksmith = MAKEFOURCC('h','b','a','r');
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
    gameClient_t *client = &game.clients[0];
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    uint32_t const trainee = MAKEFOURCC('u','0','0','1');
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
    gameClient_t *client = &game.clients[0];
    edict_t *producer, *hero1, *hero2, *keep, *castle;
    UnitProfile_t producer_profile = { .trains = "H001" };
    slkTestData_t *balance_rows = parse_slk_string(balance_slk);
    slkTestData_t *profile_rows = parse_slk_string(profile_slk);
    slkTestData_t *old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    slkTestData_t *old_profile = G_SetProfileRows(profile_rows);
    uint32_t const hero = MAKEFOURCC('H','0','0','1');
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
    gameClient_t *client = &game.clients[0];
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    uint32_t const trainee = MAKEFOURCC('h','f','o','o');
    UnitBalance_t const *balance = G_UnitBalance(trainee);
    UnitProfile_t producer_profile = { .trains = "hfoo" };
    cstring_t (*saved_cvar)(cstring_t, cstring_t) = gi.CvarString;
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
    T_STREQ(reason, "Nofood");

    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = balance->foodUsed;
    T_EQ(G_GetTrainCommandState(client, producer, trainee, reason, sizeof(reason)), BUILD_COMMAND_AVAILABLE);

    gi.CvarString = saved_cvar;
}

TEST(wc3_building, build_all_cvar_bypasses_training_tech_gates_but_not_trains_list) {
    cstring_t (*old_cvar)(cstring_t, cstring_t) = gi.CvarString;
    gameClient_t *client = &game.clients[0];
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    uint32_t const trainee = MAKEFOURCC('u','0','0','1');
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
    gameClient_t *client = &game.clients[0];
    edict_t *producer = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    edict_t *queued = alloc_test_unit(MAKEFOURCC('u','0','0','1'), 0, 0);
    uint32_t const trainee = MAKEFOURCC('u','0','0','1');
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
    gameClient_t *owner = &game.clients[0];
    gameClient_t *viewer = &game.clients[1];
    edict_t *producer;

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

TEST(wc3_building, advanced_control_grant_and_revoke_dirty_viewer_card) {
    gameClient_t *owner = &game.clients[0];
    gameClient_t *viewer = &game.clients[1];
    uint32_t const original = level.alliances[viewer->ps.number][owner->ps.number];
    bool const was_connected = viewer->connected;
    bool const was_dirty = viewer->commands_dirty;

    viewer->connected = true;
    level.alliances[viewer->ps.number][owner->ps.number] = (1u << ALLIANCE_PASSIVE) |
        (1u << ALLIANCE_SHARED_CONTROL);
    viewer->commands_dirty = false;

    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_SHARED_ADVANCED_CONTROL, true);
    T_ASSERT(viewer->commands_dirty);
    viewer->commands_dirty = false;
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_SHARED_ADVANCED_CONTROL, false);
    T_ASSERT(viewer->commands_dirty);
    viewer->commands_dirty = false;
    G_SetPlayerAlliance(&viewer->ps, &owner->ps, ALLIANCE_SHARED_ADVANCED_CONTROL, false);
    T_ASSERT(!viewer->commands_dirty);

    level.alliances[viewer->ps.number][owner->ps.number] = original;
    viewer->connected = was_connected;
    viewer->commands_dirty = was_dirty;
}

/* Shared-control fixtures: client 0 owns the units, client 1 is the allied
 * viewer.  Captured command buttons are keyed by their registered art so a
 * disabled (DIS-skinned) button can still be matched to its command. */
#define SHARED_MAX_BUTTONS 24 // FT_COMMANDBUTTON frames one command card can emit
#define SHARED_MAX_IMAGES 64  // distinct image registrations one capture tracks

typedef struct {
    gameClient_t *owner;
    gameClient_t *viewer;
    edict_t *owner_ent;
    edict_t *viewer_ent;
} sharedControlFixture_t;

static PATHSTR shared_image_paths[SHARED_MAX_IMAGES];
static uint32_t shared_image_count;
static uint32_t shared_button_image[SHARED_MAX_BUTTONS];
static float shared_button_x[SHARED_MAX_BUTTONS];
static float shared_button_y[SHARED_MAX_BUTTONS];
static bool shared_button_has_onclick[SHARED_MAX_BUTTONS];
static char shared_button_onclick[SHARED_MAX_BUTTONS][64];
static uint32_t shared_button_count;
static uint32_t shared_cancel_train_targets;

static int shared_image_index(cstring_t name) {
    FOR_LOOP(i, shared_image_count) if (!strcmp(shared_image_paths[i], name)) return (int)i + 1;
    if (shared_image_count >= SHARED_MAX_IMAGES) return 0;
    snprintf(shared_image_paths[shared_image_count], sizeof(shared_image_paths[0]), "%s", name);
    return (int)++shared_image_count;
}

static void shared_reset_capture(void) {
    memset(shared_image_paths, 0, sizeof(shared_image_paths));
    memset(shared_button_image, 0, sizeof(shared_button_image));
    memset(shared_button_x, 0, sizeof(shared_button_x));
    memset(shared_button_y, 0, sizeof(shared_button_y));
    memset(shared_button_has_onclick, 0, sizeof(shared_button_has_onclick));
    memset(shared_button_onclick, 0, sizeof(shared_button_onclick));
    shared_image_count = shared_button_count = shared_cancel_train_targets = 0;
}

static void shared_capture_write(pfWriteType_t type, void const *value) {
    uiFrame_t const *frame;
    if (type != PF_UIFRAME || !value) return;
    frame = value;
    if (frame->flags.type == FT_COMMANDBUTTON && shared_button_count < SHARED_MAX_BUTTONS) {
        shared_button_image[shared_button_count] = frame->tex.index;
        /* Serialized points are int16 UI_FRAMEPOINT_SCALE units, y negated. */
        shared_button_x[shared_button_count] = (float)frame->points.x[FPP_MIN].offset / UI_FRAMEPOINT_SCALE;
        shared_button_y[shared_button_count] = -(float)frame->points.y[FPP_MIN].offset / UI_FRAMEPOINT_SCALE;
        shared_button_has_onclick[shared_button_count] = frame->onclick != NULL;
        snprintf(shared_button_onclick[shared_button_count], sizeof(shared_button_onclick[0]),
                 "%s", frame->onclick ? frame->onclick : "");
        shared_button_count++;
    } else if (frame->flags.type == FT_SIMPLEFRAME && frame->onclick &&
               !strncmp(frame->onclick, "canceltrain ", 12)) {
        shared_cancel_train_targets++;
    }
}

/* Captured button authored at command-card cell (x, y); the fixture gives
 * every command the same art, so the cell is the identity.  Mirrors the
 * UI_WriteCommandButtonFrame grid origin and pitch. */
static int shared_button_for_cell(uint8_t x, uint8_t y) {
    float const fx = 0.6175f + (float)x * 0.0434f;
    float const fy = 0.4660f + (float)y * 0.0440f;
    float const eps = 1.5f / UI_FRAMEPOINT_SCALE; /* int16 quantization */
    FOR_LOOP(i, shared_button_count)
        if (fabsf(shared_button_x[i] - fx) < eps && fabsf(shared_button_y[i] - fy) < eps) return (int)i;
    return -1;
}

static sharedControlFixture_t shared_control_fixture(bool advanced) {
    sharedControlFixture_t f = { &game.clients[0], &game.clients[1], &g_edicts[0], &g_edicts[1] };
    setup_test_world();
    G_SetClientConnected(f.owner_ent, true);
    G_SetClientConnected(f.viewer_ent, true);
    f.owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    f.owner->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    f.owner->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    G_SetPlayerAlliance(&f.viewer->ps, &f.owner->ps, ALLIANCE_PASSIVE, true);
    G_SetPlayerAlliance(&f.viewer->ps, &f.owner->ps, ALLIANCE_SHARED_CONTROL, true);
    G_SetPlayerAlliance(&f.viewer->ps, &f.owner->ps, ALLIANCE_SHARED_ADVANCED_CONTROL, advanced);
    return f;
}

static edict_t *shared_owned_unit(sharedControlFixture_t const *f, uint32_t class_id, UnitProfile_t *profile) {
    edict_t *unit = alloc_test_unit(class_id, 0.0f, 0.0f);
    unit->data.UnitProfile = profile;
    unit->s.player = f->owner->ps.number;
    G_SelectEntity(f->viewer, unit);
    return unit;
}

static void shared_check_build_menu_button(bool advanced) {
    /* G_UnitHasBuildMenu reads the profile table, not the entity's row. */
    static char const profile_slk[] =
        "ID;PWXL;N;EBB;Y2;X2\n"
        "C;Y1;X1;K\"id\"\nC;Y1;X2;K\"Builds\"\n"
        "C;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"hbar\"\nE\n";
    slkTestData_t *rows = parse_slk_string(profile_slk);
    slkTestData_t *old_rows = G_SetProfileRows(rows);
    sharedControlFixture_t f = shared_control_fixture(advanced);
    UnitProfile_t profile = { .builds = "hbar" };
    gameCommandButton_t buttons[12];
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *worker = shared_owned_unit(&f, MAKEFOURCC('h','p','e','a'), &profile);
    gameCommandButton_t const *build = NULL;
    int index;

    T_ASSERT(G_UnitHasBuildMenu(worker));
    FOR_LOOP(i, G_GetCommandButtons(worker, buttons, 12))
        if (!strcmp(buttons[i].command, STR_CmdBuild)) build = &buttons[i];
    T_NOT_NULL(build);
    if (!build) { G_SetProfileRows(old_rows); free_slk_rows(rows); return; }

    shared_reset_capture();
    gi.Write = shared_capture_write;
    gi.ImageIndex = shared_image_index;
    Get_Commands_f(f.viewer_ent);
    gi.Write = old_write;
    gi.ImageIndex = old_image_index;

    /* The Build submenu spends the owner's resources (ui_builds is gated on
     * advanced sharing), so the card must agree with that authority. */
    index = shared_button_for_cell(build->x, build->y);
    T_ASSERT(index >= 0);
    if (index >= 0) {
        T_EQ(shared_button_has_onclick[index], advanced);
        if (advanced) T_STREQ(shared_button_onclick[index], "button " STR_CmdBuild);
        else T_NOT_NULL(strstr(shared_image_paths[shared_button_image[index] - 1], "DIS"));
    }
    G_SetProfileRows(old_rows);
    free_slk_rows(rows);
}

TEST(wc3_building, basic_control_ally_sees_build_menu_disabled) { shared_check_build_menu_button(false); }
TEST(wc3_building, advanced_control_ally_sees_build_menu_enabled) { shared_check_build_menu_button(true); }

static void shared_check_queue_cancel_targets(bool advanced) {
    sharedControlFixture_t f = shared_control_fixture(advanced);
    UnitProfile_t profile = { .trains = "hpea" };
    UnitBalance_t balance;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    edict_t *barracks = shared_owned_unit(&f, MAKEFOURCC('h','b','a','r'), &profile);

    /* The fixture Barracks row carries no building flag; the queue panel needs one. */
    balance = *barracks->data.UnitBalance;
    balance.isBuilding = true;
    barracks->data.UnitBalance = &balance;
    T_ASSERT(SP_TrainUnit(barracks, MAKEFOURCC('h','p','e','a')));
    T_ASSERT(SP_TrainUnit(barracks, MAKEFOURCC('h','p','e','a')));
    T_EQ(G_ProductionQueueCount(barracks), 2);
    T_ASSERT(UI_TestUsesBuildingQueuePanel(f.viewer, barracks));

    gi.Write = shared_capture_write;
    gi.ImageIndex = shared_image_index;
    /* The owner always gets one cancel target per queued item. */
    shared_reset_capture();
    UI_SendInfoPanel(f.owner_ent, &barracks, 1);
    T_EQ(shared_cancel_train_targets, 2);
    /* CancelTrain is denied server-side without advanced sharing, so the
     * viewer's panel offers the hit targets only when the command would work. */
    shared_reset_capture();
    UI_SendInfoPanel(f.viewer_ent, &barracks, 1);
    T_EQ(shared_cancel_train_targets, advanced ? 2 : 0);
    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, basic_control_ally_sees_no_queue_cancel_targets) { shared_check_queue_cancel_targets(false); }
TEST(wc3_building, advanced_control_ally_sees_queue_cancel_targets) { shared_check_queue_cancel_targets(true); }

static void shared_check_spending_commands(bool advanced) {
    sharedControlFixture_t f = shared_control_fixture(advanced);
    UnitProfile_t profile = { .trains = "hpea", .researches = "Rhme" };
    slkTestData_t *rows = NULL;
    slkTestData_t *old = building_install_upgrade_data(&rows);
    edict_t *barracks;
    uint32_t gold, queued;

    memset(f.owner->tech, 0, sizeof(f.owner->tech));
    barracks = shared_owned_unit(&f, MAKEFOURCC('h','b','a','r'), &profile);
    T_ASSERT(G_UnitCanControl(f.viewer, barracks));
    T_EQ(G_UnitCanSpendResources(f.viewer, barracks), advanced);
    gold = f.owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD];

    G_ClientCommand(f.viewer_ent, 2, (cstring_t[]){ "button", "hpea" });
    T_EQ(G_ProductionQueueCount(barracks), advanced ? 1 : 0);
    G_ClientCommand(f.viewer_ent, 2, (cstring_t[]){ "research", "Rhme" });
    T_EQ(G_ProductionQueueCount(barracks), advanced ? 2 : 0);
    if (advanced) T_ASSERT(f.owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD] < gold);
    else T_EQ(f.owner->ps.stats[PLAYERSTATE_RESOURCE_GOLD], gold);

    /* The owner's own production can only be cancelled with advanced sharing. */
    T_ASSERT(SP_TrainUnit(barracks, MAKEFOURCC('h','p','e','a')));
    queued = G_ProductionQueueCount(barracks);
    G_ClientCommand(f.viewer_ent, 2, (cstring_t[]){ "canceltrain", "0" });
    T_EQ(G_ProductionQueueCount(barracks), advanced ? queued - 1 : queued);

    building_restore_upgrade_data(old, rows);
}

TEST(wc3_building, basic_control_ally_commands_cannot_spend_owner_resources) { shared_check_spending_commands(false); }
TEST(wc3_building, advanced_control_ally_commands_spend_owner_resources) { shared_check_spending_commands(true); }

TEST(wc3_building, enable_user_ui_does_not_block_build_command_button) {
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    UnitProfile_t worker_profile = { .builds = "hbar" };
    cstring_t button[] = { "button", "CmdBuild" };

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
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
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
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
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
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
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
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
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

TEST(wc3_building, command_button_serializes_engaged_edge_glow_without_autocast_state) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
    gameCommandButton_t button = { .art = "test", .engaged = 1 };

    gi.Write = building_capture_write;
    gi.ImageIndex = building_test_image_index;
    building_command_frame_seen = false;
    UI_WriteCommandButtonFrame(&button);

    T_ASSERT(building_command_frame_seen);
    T_ASSERT(building_command_frame.flagsvalue & UIFLAG_ABILITY_ENGAGED);
    T_ASSERT(!(building_command_frame.flagsvalue & UIFLAG_ALTERNATE_ACTIVE));
    T_ASSERT(!(building_command_frame.flagsvalue & UIFLAG_ALERT_RED_PULSE));

    button.engaged = 0;
    button.alternate_active = 0;
    UI_WriteCommandButtonFrame(&button);
    T_ASSERT(!(building_command_frame.flagsvalue & UIFLAG_ABILITY_ENGAGED));
    button.engaged = 1;
    UI_WriteCommandButtonFrame(&button);
    T_ASSERT(building_command_frame.flagsvalue & UIFLAG_ABILITY_ENGAGED);
    T_ASSERT(!(building_command_frame.flagsvalue & UIFLAG_ALERT_RED_PULSE));

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}


TEST(wc3_building, command_button_geometry_matches_warcraft_grid) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
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
    T_FEQ((float)building_command_frame.points.x[FPP_MIN].offset / UI_FRAMEPOINT_SCALE,
          0.7477f, 0.0001f);
    T_FEQ(-(float)building_command_frame.points.y[FPP_MIN].offset / UI_FRAMEPOINT_SCALE,
          0.5540f, 0.0001f);
    T_FEQ(building_command_frame.size.width, 0.039f, 0.0001f);
    T_FEQ(building_command_frame.size.height, 0.039f, 0.0001f);

    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

TEST(wc3_building, command_button_number_draws_bottom_right_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;
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
    gameClient_t *client = &game.clients[0];

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
        uint32_t const techid = 0x44000000u + i + 1;
        G_SetPlayerTechMaxAllowed(client, techid, 2);
    }
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        uint32_t const techid = 0x44000000u + i + 1;
        T_EQ(G_GetPlayerTechMaxAllowed(client, techid), 2);
    }
}

TEST(wc3_building, tech_state_capacity_is_bounded_without_clobbering_existing_entries) {
    gameClient_t *client = &game.clients[0];

    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        uint32_t const techid = 0x41000000u + i + 1;
        G_SetPlayerTechMaxAllowed(client, techid, (int32_t)i);
    }
    FOR_LOOP(i, MAX_PLAYER_TECH_STATE) {
        uint32_t const techid = 0x41000000u + i + 1;
        T_EQ(G_GetPlayerTechMaxAllowed(client, techid), (int32_t)i);
    }

    G_SetPlayerTechMaxAllowed(client, 0x42000001u, 7);
    T_EQ(G_GetPlayerTechMaxAllowed(client, 0x42000001u), -1);
    T_EQ(G_GetPlayerTechMaxAllowed(client, 0x41000001u), 0);
}

TEST(wc3_building, building_charge_rejects_short_gold_and_refund_restores_resources) {
    gameClient_t *client = &game.clients[0];
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
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
    edict_t *builder;
    edict_t *worker;
    edict_t *blocker;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    vec2_t requested = { 64.0f, 64.0f };
    vec2_t snapped;
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

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

    requested = (vec2_t){ 100000.0f, 100000.0f };
    T_EQ(G_EvaluateBuildPlacement(builder, barracks, &requested, &snapped), PLACE_OUT_OF_BOUNDS);
}

TEST(wc3_building, gold_return_building_respects_gold_mine_exclusion_radius) {
    edict_t *builder;
    edict_t *mine;
    UnitAbilities_t mine_abilities = { .abilList = "Abgm" };
    vec2_t requested = { 0.0f, 0.0f };
    vec2_t snapped;
    uint32_t const gold_return_building = MAKEFOURCC('h','T','S','T');
    uint32_t const ordinary_building = MAKEFOURCC('h','b','a','r');

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

TEST(wc3_building, placement_flags_decode_warsmash_pathing_predicates) {
    uint8_t const flags = G_PlacementFlags("unflyable, blockvision, unfloat, unamph");
    T_ASSERT(flags & CM_PATHING_UNFLYABLE);
    T_ASSERT(flags & WC3_PATH_BLOCKVISION);
    T_ASSERT(flags & WC3_PATH_UNFLOAT);
    T_ASSERT(flags & WC3_PATH_UNAMPH);
}

TEST(wc3_building, placement_unamph_is_compound_walk_and_swim_blocker) {
    uint8_t const land = CM_PATHING_UNSWIMMABLE;
    uint8_t const water = CM_PATHING_UNWALKABLE;
    uint8_t const neither = CM_PATHING_UNWALKABLE | CM_PATHING_UNSWIMMABLE;

    T_ASSERT(!G_PlacementPathingPrevented(land, WC3_PATH_UNAMPH, false));
    T_ASSERT(!G_PlacementPathingPrevented(water, WC3_PATH_UNAMPH, false));
    T_ASSERT(G_PlacementPathingPrevented(neither, WC3_PATH_UNAMPH, false));
    T_ASSERT(!G_PlacementPathingRequired(land, WC3_PATH_UNAMPH));
    T_ASSERT(!G_PlacementPathingRequired(water, WC3_PATH_UNAMPH));
    T_ASSERT(G_PlacementPathingRequired(neither, WC3_PATH_UNAMPH));
}

TEST(wc3_building, naga_shallow_override_only_clears_unbuildable_on_water) {
    uint8_t const unbuildable = WC3_PATH_UNBUILDABLE | WC3_PATH_NAGA_SHALLOW;
    uint8_t const blocked_cliff = WC3_PATH_UNBUILDABLE | CM_PATHING_UNFLYABLE |
                                  WC3_PATH_NAGA_SHALLOW;

    T_ASSERT(!G_PlacementPathingPrevented(WC3_PATH_UNBUILDABLE, unbuildable, true));
    T_ASSERT(G_PlacementPathingPrevented(WC3_PATH_UNBUILDABLE, unbuildable, false));
    T_ASSERT(G_PlacementPathingPrevented(WC3_PATH_UNBUILDABLE | CM_PATHING_UNFLYABLE,
                                         blocked_cliff, true));
}

static void building_set_test_water_surface(bool present) {
    war3mapVertex_t *vertices = (war3mapVertex_t *)world.map->vertices;
    uint16_t const waterlevel = present ? (uint16_t)(0x2000 + 4 * 32) : 0;
    CM_W3SetWaterHeight(0.0f);
    FOR_LOOP(i, world.map->width * world.map->height) vertices[i].waterlevel = waterlevel;
}

TEST(wc3_building, naga_structures_allow_land_and_shallow_but_reject_deep_water) {
    enum { CELLS = 512 };
    static uint8_t pathmap[CELLS * CELLS];
    static cstring_t const data_slk =
        "ID;PWXL;N;EBB;Y7;X2\n"
        "C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"race\"\n"
        "C;Y2;X1;K\"nntt\"\nC;Y2;X2;K\"naga\"\n"
        "C;Y3;X1;K\"nnfm\"\nC;Y3;X2;K\"naga\"\n"
        "C;Y4;X1;K\"nnsg\"\nC;Y4;X2;K\"naga\"\n"
        "C;Y5;X1;K\"nnsa\"\nC;Y5;X2;K\"naga\"\n"
        "C;Y6;X1;K\"nntg\"\nC;Y6;X2;K\"naga\"\n"
        "C;Y7;X1;K\"nnad\"\nC;Y7;X2;K\"naga\"\n"
        "E\n";
    static cstring_t const balance_slk =
        "ID;PWXL;N;EBB;Y7;X2\n"
        "C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"isbldg\"\n"
        "C;Y2;X1;K\"nntt\"\nC;Y2;X2;K1\n"
        "C;Y3;X1;K\"nnfm\"\nC;Y3;X2;K1\n"
        "C;Y4;X1;K\"nnsg\"\nC;Y4;X2;K1\n"
        "C;Y5;X1;K\"nnsa\"\nC;Y5;X2;K1\n"
        "C;Y6;X1;K\"nntg\"\nC;Y6;X2;K1\n"
        "C;Y7;X1;K\"nnad\"\nC;Y7;X2;K1\n"
        "E\n";
    static uint32_t const naga_buildings[] = {
        MAKEFOURCC('n','n','t','t'), MAKEFOURCC('n','n','f','m'),
        MAKEFOURCC('n','n','s','g'), MAKEFOURCC('n','n','s','a'),
        MAKEFOURCC('n','n','t','g'), MAKEFOURCC('n','n','a','d')
    };
    slkTestData_t *data_rows, *balance_rows;
    slkTestData_t *old_data, *old_balance;
    slkTestData_t *new_data, *new_balance;
    uint8_t prevented = 0, required = 0;
    vec2_t const requested = { 64.0f, 64.0f };
    edict_t *builder;

    setup_test_world();
    data_rows = parse_slk_string(data_slk);
    balance_rows = parse_slk_string(balance_slk);
    old_data = G_SetSLKRows("UnitData", data_rows);
    old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    builder = alloc_test_unit(MAKEFOURCC('n','m','p','e'), -128.0f, -128.0f);
    for (uint32_t i = 0; i < sizeof(naga_buildings) / sizeof(naga_buildings[0]); i++) {
        uint32_t const building = naga_buildings[i];
        G_GetBuildPlacementPathingFlags(building, &prevented, &required);
        T_ASSERT(prevented & WC3_PATH_UNBUILDABLE);
        T_ASSERT(prevented & WC3_PATH_UNAMPH);
        T_ASSERT(!(prevented & WC3_PATH_UNWALKABLE));
        T_ASSERT(prevented & WC3_PATH_NAGA_SHALLOW);
        T_EQ(required, 0);

        building_set_test_water_surface(false);
        memset(pathmap, CM_PATHING_UNSWIMMABLE, sizeof(pathmap));
        setup_test_pathmap(CELLS, CELLS, pathmap);
        T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, NULL), PLACE_OK);

        /* Retail shallow-water WPM cells include both UNWALKABLE and
         * UNBUILDABLE, but remain swimmable. */
        building_set_test_water_surface(true);
        memset(pathmap, CM_PATHING_UNWALKABLE | WC3_PATH_UNBUILDABLE, sizeof(pathmap));
        setup_test_pathmap(CELLS, CELLS, pathmap);
        T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, NULL), PLACE_OK);

        memset(pathmap, CM_PATHING_UNWALKABLE | WC3_PATH_UNBUILDABLE | CM_PATHING_UNSWIMMABLE,
               sizeof(pathmap));
        setup_test_pathmap(CELLS, CELLS, pathmap);
        T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, NULL), PLACE_TERRAIN_BLOCKED);

        memset(pathmap, CM_PATHING_UNWALKABLE | WC3_PATH_UNBUILDABLE | CM_PATHING_UNFLYABLE,
               sizeof(pathmap));
        setup_test_pathmap(CELLS, CELLS, pathmap);
        T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, NULL), PLACE_TERRAIN_BLOCKED);

        memset(pathmap, WC3_PATH_UNBUILDABLE, sizeof(pathmap));
        building_set_test_water_surface(false);
        setup_test_pathmap(CELLS, CELLS, pathmap);
        T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, NULL), PLACE_TERRAIN_BLOCKED);
    }

    new_balance = G_SetSLKRows("UnitBalance", old_balance);
    new_data = G_SetSLKRows("UnitData", old_data);
    free_slk_rows(new_balance);
    free_slk_rows(new_data);
}

TEST(wc3_building, melee_race_buildings_reject_naga_shallow_water_cells) {
    enum { CELLS = 512 };
    static uint8_t pathmap[CELLS * CELLS];
    static cstring_t const data_slk =
        "ID;PWXL;N;EBB;Y5;X2\n"
        "C;Y1;X1;K\"unitID\"\nC;Y1;X2;K\"race\"\n"
        "C;Y2;X1;K\"hbar\"\nC;Y2;X2;K\"human\"\n"
        "C;Y3;X1;K\"obar\"\nC;Y3;X2;K\"orc\"\n"
        "C;Y4;X1;K\"usep\"\nC;Y4;X2;K\"undead\"\n"
        "C;Y5;X1;K\"eaom\"\nC;Y5;X2;K\"nightelf\"\n"
        "E\n";
    static cstring_t const balance_slk =
        "ID;PWXL;N;EBB;Y5;X2\n"
        "C;Y1;X1;K\"unitBalanceID\"\nC;Y1;X2;K\"isbldg\"\n"
        "C;Y2;X1;K\"hbar\"\nC;Y2;X2;K1\n"
        "C;Y3;X1;K\"obar\"\nC;Y3;X2;K1\n"
        "C;Y4;X1;K\"usep\"\nC;Y4;X2;K1\n"
        "C;Y5;X1;K\"eaom\"\nC;Y5;X2;K1\n"
        "E\n";
    static struct {
        uint32_t building;
        cstring_t race;
    } const melee_buildings[] = {
        { MAKEFOURCC('h','b','a','r'), STR_HUMAN },
        { MAKEFOURCC('o','b','a','r'), STR_ORC },
        { MAKEFOURCC('u','s','e','p'), STR_UNDEAD },
        { MAKEFOURCC('e','a','o','m'), STR_NIGHTELF }
    };
    slkTestData_t *data_rows, *balance_rows;
    slkTestData_t *old_data, *old_balance;
    slkTestData_t *new_data, *new_balance;
    vec2_t const requested = { 64.0f, 64.0f };
    uint8_t prevented = 0, required = 0;
    edict_t *builder;

    setup_test_world();
    data_rows = parse_slk_string(data_slk);
    balance_rows = parse_slk_string(balance_slk);
    old_data = G_SetSLKRows("UnitData", data_rows);
    old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128.0f, -128.0f);

    building_set_test_water_surface(true);
    memset(pathmap, CM_PATHING_UNWALKABLE | WC3_PATH_UNBUILDABLE, sizeof(pathmap));
    setup_test_pathmap(CELLS, CELLS, pathmap);
    for (uint32_t i = 0; i < sizeof(melee_buildings) / sizeof(melee_buildings[0]); i++) {
        uint32_t const building = melee_buildings[i].building;
        UnitData_t const *data = G_UnitData(building);

        T_ASSERT(G_UnitIsBuilding(building));
        T_STREQ(data->race, melee_buildings[i].race);
        G_GetBuildPlacementPathingFlags(building, &prevented, &required);
        T_ASSERT(prevented & WC3_PATH_UNWALKABLE);
        T_ASSERT(prevented & WC3_PATH_UNBUILDABLE);
        T_ASSERT(!(prevented & WC3_PATH_NAGA_SHALLOW));
        T_EQ(required, 0);
        T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, NULL),
             PLACE_TERRAIN_BLOCKED);
    }

    new_balance = G_SetSLKRows("UnitBalance", old_balance);
    new_data = G_SetSLKRows("UnitData", old_data);
    free_slk_rows(new_balance);
    free_slk_rows(new_data);
}

TEST(wc3_building, placement_unfloat_uses_unswimmable_channel) {
    T_ASSERT(G_PlacementPathingPrevented(CM_PATHING_UNSWIMMABLE, WC3_PATH_UNFLOAT, false));
    T_ASSERT(!G_PlacementPathingPrevented(CM_PATHING_UNWALKABLE, WC3_PATH_UNFLOAT, false));
    T_ASSERT(G_PlacementPathingRequired(CM_PATHING_UNSWIMMABLE, WC3_PATH_UNFLOAT));
    T_ASSERT(!G_PlacementPathingRequired(CM_PATHING_UNWALKABLE, WC3_PATH_UNFLOAT));
}

TEST(wc3_building, blight_required_placement_tracks_runtime_blight) {
    static const char balance_slk[] =
        "C;Y1;X1;K\"unitBalanceID\"\n"
        "C;Y1;X2;K\"isbldg\"\n"
        "C;Y1;X3;K\"requirePlace\"\n"
        "C;Y1;X4;K\"preventPlace\"\n"
        "C;Y2;X1;K\"uBlt\"\n"
        "C;Y2;X2;K1\n"
        "C;Y2;X3;K\"blighted\"\n"
        "C;Y2;X4;K\"_\"\n"
        "C;Y3;X1;K\"uNoB\"\n"
        "C;Y3;X2;K1\n"
        "C;Y3;X3;K\"_\"\n"
        "C;Y3;X4;K\"blighted\"\n"
        "E\n";
    uint32_t const building = MAKEFOURCC('u','B','l','t');
    uint32_t const anti_blight = MAKEFOURCC('u','N','o','B');
    vec2_t requested = { 32.0f, 32.0f }, snapped;
    slkTestData_t *rows;
    slkTestData_t *old;
    edict_t *builder;

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('u','a','c','o'), -128.0f, -128.0f);
    rows = parse_slk_string(balance_slk); old = G_SetSLKRows("UnitBalance", rows);
    T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, &snapped), PLACE_REQUIRES_BLIGHT);
    T_EQ(G_EvaluateBuildPlacement(builder, anti_blight, &requested, &snapped), PLACE_OK);
    G_SetBlightPoint(&requested, true);
    T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, &snapped), PLACE_OK);
    T_EQ(G_EvaluateBuildPlacement(builder, anti_blight, &requested, &snapped), PLACE_TERRAIN_BLOCKED);
    G_SetBlightPoint(&requested, false);
    T_EQ(G_EvaluateBuildPlacement(builder, building, &requested, &snapped), PLACE_REQUIRES_BLIGHT);

    G_SetSLKRows("UnitBalance", old); free_slk_rows(rows);
}

TEST(wc3_building, placement_preview_uses_authoritative_pathing_flags) {
    uint8_t prevented = 0, required = 0;
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

    G_GetBuildPlacementPathingFlags(barracks, &prevented, &required);
    T_ASSERT(prevented & WC3_PATH_UNWALKABLE);
    T_ASSERT(prevented & WC3_PATH_UNBUILDABLE);
}

TEST(wc3_building, spawned_unit_exports_gameplay_collision_radius) {
    edict_t *unit;

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
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    UnitProfile_t profile = { .builds = "hbar" };
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

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

/* Build orders install their own walk without G_IssueUnitPointOrder; they must still retire a Stop guard point. */
TEST(wc3_building, build_order_clears_stop_guard) {
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    UnitProfile_t profile = { .builds = "hbar" };
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128, -128);
    builder->s.player = client->ps.number; builder->data.UnitProfile = &profile;
    builder->stand = unit_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    order_stop(builder);
    T_ASSERT(builder->movement.guard_state == GUARD_IDLE);

    T_ASSERT(G_IssueBuildOrder(builder, barracks, &point));

    T_EQ(builder->build_project, barracks);
    T_ASSERT(builder->movement.guard_state == GUARD_NONE);
}

/* Human04's opening sends these three preplaced Peasants to the centres of
 * BuildFarm (-1360,-4608), BuildBarracks (-1744,-3536), and BuildTownHall
 * (-2208,-4048).  Keep the authored starts, region-entry order, and build
 * centers here. Retail's Construction Site Indicator is non-blocking, so the
 * Barracks peasant may occupy the Town Hall footprint without cancelling it. */
TEST(wc3_building, human04_opening_positions_keep_townhall_build) {
    enum { CELLS = 512 };
    static uint8_t pathmap[CELLS * CELLS];
    static UnitProfile_t const profile = { .builds = "hhou,hbar,htow" };
    static UnitAbilities_t const abilities = { .abilList = "Arep" };
    static UnitData_t const worker_data = { .moveTypeName = "foot", .race = STR_HUMAN };
    edict_t *farm, *barracks, *townhall;
    vec2_t const farm_point = { -1360.0f, -4608.0f };
    vec2_t const barracks_point = { -1744.0f, -3536.0f };
    vec2_t const townhall_point = { -2208.0f, -4048.0f };
    edict_t *workers[3];
    uint32_t const worker_ids[3] = {
        MAKEFOURCC('h','p','e','a'), MAKEFOURCC('h','p','e','a'), MAKEFOURCC('h','p','e','a')
    };
    vec2_t const starts[3] = {
        { -3709.839f, -6104.534f },
        { -3814.181f, -6050.230f },
        { -3692.568f, -5993.369f }
    };
    vec2_t const points[3] = { farm_point, barracks_point, townhall_point };
    vec2_t const region_min[3] = {
        { -1440.0f, -4768.0f }, { -1856.0f, -3648.0f }, { -2336.0f, -4192.0f }
    };
    vec2_t const region_max[3] = {
        { -1280.0f, -4448.0f }, { -1632.0f, -3424.0f }, { -2080.0f, -3904.0f }
    };
    uint32_t const buildings[3] = {
        MAKEFOURCC('h','h','o','u'), MAKEFOURCC('h','b','a','r'), MAKEFOURCC('h','t','o','w')
    };
    bool issued[3] = { false, false, false };
    bool barracks_spawned = false;
    bool townhall_spawned = false;
    gameClient_t *client;

    setup_test_world();
    memset(pathmap, 0, sizeof(pathmap));
    setup_test_pathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = { -8192.0f, -8192.0f },
                                  .max = { 8192.0f, 8192.0f }));
    client = &game.clients[0];
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    FOR_LOOP(i, 3) {
        workers[i] = alloc_test_unit(worker_ids[i], starts[i].x, starts[i].y);
        workers[i]->s.player = client->ps.number;
        workers[i]->svflags |= SVF_MONSTER;
        workers[i]->movetype = MOVETYPE_STEP;
        workers[i]->stand = unit_stand;
        workers[i]->think = monster_think;
        workers[i]->collision = 16.0f;
        workers[i]->data.UnitData = &worker_data;
        workers[i]->data.UnitProfile = &profile;
        workers[i]->data.UnitAbilities = &abilities;
        gi.LinkEntity(workers[i]);
    }
    farm = workers[0]; barracks = workers[1]; townhall = workers[2];

    FOR_LOOP(i, 3)
        T_EQ(G_GetBuildCommandState(client, workers[i], buildings[i], NULL, 0), BUILD_COMMAND_AVAILABLE);

    FOR_LOOP(i, 3) order_move(workers[i], Waypoint_add(&points[i]));

    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started = true;
    level.scriptsStarted = true;
    FOR_LOOP(frame, 240) {
        globals.RunFrame();
        FOR_LOOP(i, 3) {
            if (issued[i] || workers[i]->s.origin2.x < region_min[i].x ||
                workers[i]->s.origin2.x > region_max[i].x ||
                workers[i]->s.origin2.y < region_min[i].y ||
                workers[i]->s.origin2.y > region_max[i].y) continue;
            issued[i] = true;
            if (i == 2) {
                /* Leave the mobile Barracks peasant in the Town Hall footprint.
                 * Retail accepts the order and displaces it when construction
                 * materializes; it is not a placement-time hard blocker. */
                edict_t *const barracks_goal = workers[1]->goalentity;
                vec2_t const before_preview = workers[1]->s.origin2;
                workers[1]->s.origin2 = points[i];
                gi.LinkEntity(workers[1]);
                T_ASSERT(G_IssueBuildOrder(workers[i], buildings[i], &points[i]));
                T_NOT_NULL(workers[i]->build_preview);
                T_ASSERT(workers[i]->build_preview->svflags & SVF_OWNER_ONLY);
                T_EQ(workers[i]->build_preview->s.player, workers[i]->s.player);
                T_ASSERT(Vector2_distance(&workers[1]->s.origin2, &before_preview) > 1.0f);
                T_ASSERT(workers[1]->goalentity == barracks_goal);
                T_ASSERT(move_is_active_order_walk(workers[1]));
                T_ASSERT(workers[i]->build_preview->aiflags & AI_HOLD_FRAME);
                T_STREQ(workers[i]->build_preview->animation_request, "stand");
                if (workers[i]->build_preview->animation) {
                    T_ASSERT(G_AnimationHasPrimary(workers[i]->build_preview->animation, "stand"));
                    T_EQ(workers[i]->build_preview->s.frame,
                         workers[i]->build_preview->animation->interval[0]);
                }
                T_ASSERT(workers[i]->build_preview->vertex_color_set);
                T_EQ(workers[i]->build_preview->vertex_color.r, 255);
                T_EQ(workers[i]->build_preview->vertex_color.g, 255);
                T_EQ(workers[i]->build_preview->vertex_color.b, 255);
                T_EQ(workers[i]->build_preview->vertex_color.a, 128);
            } else {
                T_ASSERT(G_IssueBuildOrder(workers[i], buildings[i], &points[i]));
            }
        }
    }

    FOR_LOOP(i, 3) T_ASSERT(issued[i]);
    T_ASSERT(!farm->build_project);
    T_ASSERT(!barracks->build_project);
    T_ASSERT(!townhall->build_project);
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == buildings[1])
            barracks_spawned = true;
    FOR_LOOP(i, globals.num_edicts)
        if (g_edicts[i].inuse && g_edicts[i].class_id == buildings[2])
            townhall_spawned = true;
    T_ASSERT(barracks_spawned);
    T_ASSERT(townhall_spawned);
}

TEST(wc3_building, construction_blocks_after_site_indicator) {
    enum { CELLS = 64 };
    static uint8_t pathmap[CELLS * CELLS];
    size_t const pathtex_size = sizeof(pathTex_t) + sizeof(color32_t);
    edict_t *building;
    pathTex_t *pathtex;
    vec2_t point = { 0.0f, 0.0f };

    setup_test_world();
    memset(pathmap, 0, sizeof(pathmap));
    setup_test_pathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = { -1024.0f, -1024.0f },
                                  .max = { 1024.0f, 1024.0f }));
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), point.x, point.y);
    building->s.flags |= EF_BUILDING | EF_NOT_SELECTABLE;
    pathtex = gi.MemAlloc(pathtex_size);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = pathtex->height = 1;
    pathtex->map[0].b = 0xff;
    building->pathtex = pathtex;
    gi.LinkEntity(building);
    CM_BakeStaticObstacles();
    T_ASSERT(CM_PointIsPathableForRadius(&point, 0.0f));

    building->s.flags &= ~EF_NOT_SELECTABLE;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    CM_BakeStaticObstacles();
    T_ASSERT(!CM_PointIsPathableForRadius(&point, 0.0f));
    building->pathtex = NULL;
    gi.MemFree(pathtex);
}

/* A construction can invalidate the old straight route while a second worker
 * is still travelling to its later build site.  This is the Human04 failure:
 * the worker is in the reservation/approach lane, not yet overlapping the
 * Town Hall footprint, and must be displaced without consuming its move order. */
TEST(wc3_building, construction_displacement_preserves_later_build_route) {
    enum { CELLS = 128, FOOTPRINT = 9 };
    static uint8_t pathmap[CELLS * CELLS];
    size_t const pathtex_size = sizeof(pathTex_t) + FOOTPRINT * FOOTPRINT * sizeof(color32_t);
    edict_t *builder, *worker, *building, *waypoint;
    pathTex_t *pathtex;
    vec2_t const building_point = { 0.0f, 0.0f };
    vec2_t const worker_start = { 0.0f, -192.0f };
    vec2_t const later_build = { 0.0f, 512.0f };
    vec2_t before_displace, after_first_step;
    float normal_step;

    setup_test_world();
    memset(pathmap, 0, sizeof(pathmap));
    setup_test_pathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = { -2048.0f, -2048.0f },
                                  .max = { 2048.0f, 2048.0f }));
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -512.0f, -512.0f);
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), worker_start.x, worker_start.y);
    building = alloc_test_unit(MAKEFOURCC('h','t','o','w'), building_point.x, building_point.y);
    builder->s.player = worker->s.player = building->s.player = game.clients[0].ps.number;
    builder->svflags |= SVF_MONSTER; worker->svflags |= SVF_MONSTER; building->svflags |= SVF_MONSTER;
    builder->stand = worker->stand = building->stand = unit_stand;
    worker->movetype = MOVETYPE_STEP; worker->think = monster_think; worker->collision = 16.0f;
    building->s.flags |= EF_BUILDING | EF_NOT_SELECTABLE;
    pathtex = gi.MemAlloc(pathtex_size);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = pathtex->height = FOOTPRINT;
    FOR_LOOP(i, FOOTPRINT * FOOTPRINT) pathtex->map[i].b = 0xff;
    building->pathtex = pathtex;
    normal_step = unit_movedistance(worker);
    gi.LinkEntity(builder); gi.LinkEntity(worker); gi.LinkEntity(building);
    CM_BakeStaticObstacles();

    waypoint = Waypoint_add(&later_build);
    order_move(worker, waypoint);
    before_displace = worker->s.origin2;
    T_ASSERT(G_DisplaceBuildOccupants(builder, building));
    T_FEQ(worker->s.origin2.x, before_displace.x, 0.001f);
    T_FEQ(worker->s.origin2.y, before_displace.y, 0.001f);
    T_ASSERT(move_displacement_active(worker));
    T_STREQ(worker->animation_request, "walk");
    T_ASSERT(worker->goalentity == waypoint);
    T_ASSERT(move_is_active_order_walk(worker));

    building->s.flags &= ~EF_NOT_SELECTABLE;
    T_ASSERT(G_StartHumanConstruction(builder, building));
    T_ASSERT(!CM_PointIsPathableForRadius(&building_point, 0.0f));
    T_ASSERT(worker->goalentity == waypoint);
    T_ASSERT(move_is_active_order_walk(worker));
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started = true;
    level.scriptsStarted = true;
    globals.RunFrame();
    after_first_step = worker->s.origin2;
    T_FEQ(Vector2_distance(&after_first_step, &before_displace), normal_step, 0.001f);
    /* Sample mid-displacement. The route-heading stepper covers the 192-unit exit in about eight 27-unit
     * steps, so the old eight-frame sample sat on the arrival threshold and flipped with libm rounding. */
    FOR_LOOP(frame, 3) globals.RunFrame();
    T_ASSERT(Vector2_distance(&worker->s.origin2, &before_displace) > 1.0f);
    T_ASSERT(move_displacement_active(worker));
    T_STREQ(worker->animation_request, "walk");
    FOR_LOOP(frame, 240) globals.RunFrame();
    T_ASSERT(Vector2_distance(&worker->s.origin2, &later_build) <= worker->collision + 64.0f);
    T_ASSERT(!move_displacement_active(worker));
    T_ASSERT(worker->goalentity == waypoint);
    T_FEQ(waypoint->s.origin2.x, later_build.x, 0.001f);
    T_FEQ(waypoint->s.origin2.y, later_build.y, 0.001f);

    building->pathtex = NULL;
    gi.MemFree(pathtex);
}

TEST(wc3_building, construction_start_and_completion_clear_occupants) {
    enum { CELLS = 128, FOOTPRINT = 9 };
    static uint8_t pathmap[CELLS * CELLS];
    size_t const pathtex_size = sizeof(pathTex_t) + FOOTPRINT * FOOTPRINT * sizeof(color32_t);
    edict_t *builder, *occupant, *occupant2, *building;
    pathTex_t *pathtex;
    vec2_t const center = { 0.0f, 0.0f };

    setup_test_world();
    memset(pathmap, 0, sizeof(pathmap));
    setup_test_pathmap(CELLS, CELLS, pathmap);
    CM_SetupTestWorldBounds(&MAKE(box2_t, .min = { -2048.0f, -2048.0f },
                                  .max = { 2048.0f, 2048.0f }));
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -512.0f, -512.0f);
    occupant = alloc_test_unit(MAKEFOURCC('h','f','o','o'), center.x, center.y);
    occupant2 = alloc_test_unit(MAKEFOURCC('h','f','o','o'), center.x, center.y);
    building = alloc_test_unit(MAKEFOURCC('h','t','o','w'), center.x, center.y);
    occupant->svflags |= SVF_MONSTER;
    occupant->s.model = 1;
    occupant->movetype = MOVETYPE_STEP;
    occupant->collision = 16.0f;
    occupant2->svflags |= SVF_MONSTER;
    occupant2->s.model = 1;
    occupant2->movetype = MOVETYPE_STEP;
    occupant2->collision = 16.0f;
    building->s.flags |= EF_BUILDING | EF_NOT_SELECTABLE;
    building->stand = unit_stand;
    pathtex = gi.MemAlloc(pathtex_size);
    memset(pathtex, 0, pathtex_size);
    pathtex->width = pathtex->height = FOOTPRINT;
    FOR_LOOP(i, FOOTPRINT * FOOTPRINT) pathtex->map[i].b = 0xff;
    building->pathtex = pathtex;
    gi.LinkEntity(builder); gi.LinkEntity(occupant); gi.LinkEntity(occupant2); gi.LinkEntity(building);
    CM_BakeStaticObstacles();

    T_ASSERT(G_StartHumanConstruction(builder, building));
    T_ASSERT(Vector2_distance(&occupant->s.origin2, &center) >= occupant->collision);
    T_ASSERT(Vector2_distance(&occupant2->s.origin2, &center) >= occupant2->collision);
    T_ASSERT(Vector2_distance(&occupant->s.origin2, &occupant2->s.origin2) >=
             occupant->collision + occupant2->collision);

    /* Walk-through construction allows a unit to enter again. Completion
     * must clear it after the finished footprint has been baked. */
    occupant->s.origin2 = center;
    occupant->s.origin.x = center.x;
    occupant->s.origin.y = center.y;
    gi.LinkEntity(occupant);
    occupant2->s.origin2 = center;
    occupant2->s.origin.x = center.x;
    occupant2->s.origin.y = center.y;
    gi.LinkEntity(occupant2);
    G_CompleteConstruction(building);
    T_ASSERT(Vector2_distance(&occupant->s.origin2, &center) >= occupant->collision);
    T_ASSERT(Vector2_distance(&occupant2->s.origin2, &center) >= occupant2->collision);
    T_ASSERT(Vector2_distance(&occupant->s.origin2, &occupant2->s.origin2) >=
             occupant->collision + occupant2->collision);

    building->pathtex = NULL;
    gi.MemFree(pathtex);
}

TEST(wc3_building, acolyte_places_haunted_mine_on_off_grid_gold_mine) {
    gameClient_t *client = &game.clients[0];
    edict_t *worker, *mine;
    UnitProfile_t profile = { .builds = "ugol" };
    vec2_t requested = { 101.0f, 99.0f }, snapped;
    uint32_t const haunted = MAKEFOURCC('u','g','o','l');

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
    gameClient_t *client = &game.clients[0];
    edict_t *builder, *mine;
    UnitProfile_t profile = { .builds = "hbar" };
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -128, -128);
    mine = alloc_test_unit(MAKEFOURCC('n','g','o','l'), -64, -64);
    builder->s.player = client->ps.number; builder->data.UnitProfile = &profile;
    if (!builder->goldmine) builder->goldmine = G_AllocGoldMine();
    assert(builder->goldmine);
    builder->goldmine->mine = mine; builder->goldmine->mine_spawn_time = mine->spawn_time;
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
    vec2_t point = { 47.0f, 79.0f };

    G_SnapBuildingPoint(MAKEFOURCC('h','p','e','a'), &point);

    T_FEQ(point.x, 32.0f, 0.001f);
    T_FEQ(point.y, 64.0f, 0.001f);
}

TEST(wc3_building, human_construction_start_sets_explicit_state_and_start_life) {
    edict_t *builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);

    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;

    T_ASSERT(!G_StartHumanConstruction(builder, builder));
    T_ASSERT(G_StartHumanConstruction(builder, building));
    T_ASSERT(building->construction);
    T_ASSERT(building->construction->paused);
    T_EQ(building->construction->type, CONSTRUCTION_HUMAN);
    T_ASSERT(building->construction->primary_builder == builder);
    T_FEQ(building->construction->progress, 0.0f, 0.001f);
    T_ASSERT(!building->construction || !building->construction->paid);
    T_ASSERT(!building->construction || building->construction->payer == 0);
    T_ASSERT(!building->construction || building->construction->gold == 0);
    T_ASSERT(!building->construction || building->construction->lumber == 0);
    T_ASSERT(building->aiflags & AI_HOLD_FRAME);
    T_FEQ(building->health.value, 100.0f, 0.001f);
}

TEST(wc3_building, construction_sound_label_drives_snapshot_loop_until_stop) {
    static cstring_t const slk =
        "ID;PWXL;N;E\n"
        "B;X4;Y2;D0\n"
        "C;Y1;X1;K\"SoundLabel\"\n"
        "C;Y1;X2;K\"FileNames\"\n"
        "C;Y1;X3;K\"DirectoryBase\"\n"
        "C;Y1;X4;K\"Volume\"\n"
        "C;Y2;X1;K\"BuildingConstructionLoop\"\n"
        "C;Y2;X2;K\"loop.wav\"\n"
        "C;Y2;X3;K\"Sound\\Buildings\\\"\n"
        "C;Y2;X4;K\"96\"\n"
        "E\n";
    UnitProfile_t profile = { .buildingSoundLabel = "BuildingConstructionLoop" };
    edict_t *builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *old_rows = G_SetSLKRows("AmbienceSounds", rows);
    int (*old_soundindex)(cstring_t) = gi.SoundIndex;
    __typeof__(gi.SoundIndexAlias) old_sound_alias = gi.SoundIndexAlias;

    building->data.UnitProfile = &profile;
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;
    building_sound_path[0] = '\0';
    gi.SoundIndex = building_test_sound_index; gi.SoundIndexAlias = building_test_sound_index_alias;
    G_ResetSoundPresentationState();

    T_ASSERT(G_StartHumanConstruction(builder, building));
    T_EQ(building->s.sound, 91);
    T_STREQ(building_sound_path, "Sound\\Buildings\\loop.wav");
    T_FEQ(G_SoundIndexVolume(91), 96.0f / 127.0f, 0.001f);

    G_StopConstruction(building);
    T_NULL(building->construction);
    T_EQ(building->s.sound, 0);

    T_ASSERT(G_StartHumanConstruction(builder, building));
    T_NOT_NULL(building->construction);
    T_FEQ(building->construction->progress, 0.0f, 0.001f);
    T_ASSERT(!building->construction->paid);
    T_EQ(building->s.sound, 91);
    G_StopConstruction(building);
    T_NULL(building->construction);

    gi.SoundIndex = old_soundindex; gi.SoundIndexAlias = old_sound_alias;
    G_SetSLKRows("AmbienceSounds", old_rows); free_slk_rows(rows);
}

TEST(wc3_building, instant_build_cheat_completes_started_human_construction_on_next_frame) {
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    edict_t *building;
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
    T_ASSERT(building->construction);
    T_ASSERT(building->construction->paused);

    G_RunConstructionFrame(building);

    T_ASSERT(!building->construction);
    T_FEQ(building->health.value, building->health.max_value, 0.001f);

    client->cheat_instant_build = false;
}

TEST(wc3_building, instant_build_cheat_completes_autonomous_construction_on_next_frame) {
    gameClient_t *client = &game.clients[0];
    edict_t *worker;
    edict_t *building;
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
    T_ASSERT(building->construction);

    G_RunConstructionFrame(building);

    T_ASSERT(!building->construction);
    T_FEQ(building->health.value, building->health.max_value, 0.001f);
    T_NULL(worker->build);

    client->cheat_instant_build = false;
}

TEST(wc3_building, removing_construction_releases_repair_worker) {
    edict_t *builder;
    edict_t *building;

    setup_test_world();
    builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;
    T_ASSERT(G_StartHumanConstruction(builder, building));
    /* Keep this lifecycle test independent of the optional Repair SLK fixture. */
    builder->build = building;
    if (!builder->buildwork) builder->buildwork = G_AllocBuildwork();
    assert(builder->buildwork);
    builder->buildwork->ability = MAKEFOURCC('A','r','e','p');
    builder->buildwork->primary = true;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->construction->primary_builder = builder;
    T_ASSERT(builder->build == building);

    G_FreeEdict(building);
    T_NULL(builder->build);
    T_ASSERT(!builder->buildwork || builder->buildwork->ability == 0);
}

TEST(wc3_building, orc_construction_hides_worker_and_progresses_autonomously) {
    edict_t *worker;
    edict_t *building;
    UnitBalance_t balance;
    float hp_before;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    balance = *building->data.UnitBalance;
    balance.buildTime = 10;
    building->data.UnitBalance = &balance;
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;

    T_ASSERT(G_StartOrcConstruction(worker, building));
    T_EQ(building->construction->type, CONSTRUCTION_ORC);
    T_ASSERT(building->construction);
    T_ASSERT(!building->construction || !building->construction->paused);
    T_ASSERT(building->construction->worker == worker);
    T_ASSERT(building->construction->worker_inside);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_ASSERT(worker->paused);
    T_ASSERT(worker->invulnerable);
    T_ASSERT(worker->build == building);
    T_FEQ(building->health.value, 100.0f, 0.001f);

    hp_before = building->health.value;
    G_RunConstructionFrame(building);
    T_FEQ(building->construction->progress, (float)FRAMETIME, 0.001f);
    T_ASSERT(building->health.value > hp_before);
    T_ASSERT(building->health.value < building->health.max_value);
}

TEST(wc3_building, naga_construction_hides_worker_and_progresses_autonomously) {
    edict_t *worker;
    edict_t *building;
    UnitBalance_t balance;
    float hp_before;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    balance = *building->data.UnitBalance;
    balance.buildTime = 10;
    building->data.UnitBalance = &balance;
    building->health.max_value = 1000.0f;
    building->health.value = 1000.0f;

    T_ASSERT(G_StartNagaConstruction(worker, building));
    T_EQ(building->construction->type, CONSTRUCTION_NAGA);
    T_ASSERT(!building->construction->paused);
    T_ASSERT(building->construction->worker == worker);
    T_ASSERT(building->construction->worker_inside);
    T_ASSERT(!building->construction->consumes_worker);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
    T_ASSERT(worker->paused);
    T_ASSERT(worker->invulnerable);
    T_FEQ(building->health.value, 100.0f, 0.001f);

    hp_before = building->health.value;
    G_RunConstructionFrame(building);
    T_FEQ(building->construction->progress, (float)FRAMETIME, 0.001f);
    T_ASSERT(building->health.value > hp_before);
}

TEST(wc3_building, naga_build_dispatch_uses_worker_inside_construction) {
    gameClient_t *client = &game.clients[0];
    UnitData_t worker_data;
    UnitProfile_t profile = { .builds = "hbar" };
    edict_t *worker, *building = NULL;
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h', 'b', 'a', 'r');

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), -128.0f, -128.0f);
    worker_data = *worker->data.UnitData;
    worker_data.race = STR_NAGA;
    worker->data.UnitData = &worker_data;
    worker->data.UnitProfile = &profile;
    worker->s.player = client->ps.number;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_ASSERT(G_IssueBuildOrder(worker, barracks, &point));
    worker->s.origin2 = worker->goalentity->s.origin2;
    build_build(worker);
    FILTER_EDICTS(ent, ent->inuse && ent->s.class_id == barracks && ent != worker) building = ent;
    T_NOT_NULL(building);
    T_EQ(building->construction->type, CONSTRUCTION_NAGA);
    T_ASSERT(building->construction->worker == worker);
    T_ASSERT(building->construction->worker_inside);
}

TEST(wc3_building, naga_build_custom_alias_resolves_to_agbu_semantics) {
    static const char ability_slk[] =
        "ID;PWXL;N;E\nB;X2;Y2;D0\n"
        "C;X1;Y1;K\"alias\"\nC;X2;K\"code\"\n"
        "C;X1;Y2;K\"A0NB\"\nC;X2;K\"AGbu\"\nE\n";
    UnitAbilities_t abilities = { .abilList = "A0NB" };
    slkTestData_t *rows = parse_slk_string(ability_slk);
    slkTestData_t *old = G_SetSLKRows("AbilityData", rows);
    edict_t *worker;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0, 0);
    worker->data.UnitAbilities = &abilities;

    T_EQ(G_AbilityCode(MAKEFOURCC('A', '0', 'N', 'B')), BZ_NAGA_BUILD);
    T_ASSERT(build_actor_has_base_ability(worker, BZ_NAGA_BUILD));

    G_SetSLKRows("AbilityData", old);
    free_slk_rows(rows);
}

TEST(wc3_building, naga_build_ability_overrides_non_naga_race_dispatch) {
    gameClient_t *client = &game.clients[0];
    UnitData_t worker_data;
    UnitProfile_t profile = { .builds = "hbar" };
    UnitAbilities_t abilities = { .abilList = "AGbu" };
    edict_t *worker, *building = NULL;
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h', 'b', 'a', 'r');

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), -128.0f, -128.0f);
    worker_data = *worker->data.UnitData;
    worker_data.race = STR_HUMAN;
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
    T_EQ(building->construction->type, CONSTRUCTION_NAGA);
}

TEST(wc3_building, orc_build_dispatch_hides_peon_with_shared_repair_ability) {
    gameClient_t *client = &game.clients[0];
    UnitData_t worker_data;
    UnitProfile_t profile = { .builds = "hbar" };
    UnitAbilities_t abilities = { .abilList = "Arep" };
    edict_t *worker, *building = NULL;
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h', 'b', 'a', 'r');

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
    T_EQ(building->construction->type, CONSTRUCTION_ORC);
    T_ASSERT(building->construction->worker == worker);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);
}

TEST(wc3_building, legacy_custom_worker_construction_progresses_from_zero_health) {
    gameClient_t *client = &game.clients[0];
    UnitData_t worker_data;
    UnitBalance_t building_balance;
    UnitProfile_t profile = { .builds = "hbar" };
    edict_t *worker, *building;
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h', 'b', 'a', 'r');

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), -128.0f, -128.0f);
    worker_data = *worker->data.UnitData;
    worker_data.race = "custom";
    worker->data.UnitData = &worker_data;
    worker->data.UnitProfile = &profile;
    worker->s.player = client->ps.number;
    worker->stand = unit_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;

    T_ASSERT(G_IssueBuildOrder(worker, barracks, &point));
    worker->s.origin2 = worker->goalentity->s.origin2;
    build_build(worker);
    building = worker->build;
    T_NOT_NULL(building);
    if (building) {
        building_balance = *building->data.UnitBalance;
        building_balance.buildTime = 1000;
        building->data.UnitBalance = &building_balance;
    }
    T_ASSERT(building && building->build == building);
    T_ASSERT(building && M_IsDead(building));
    T_ASSERT(worker->currentmove && worker->currentmove->think);
    T_ASSERT(building && building->data.UnitBalance && building->data.UnitBalance->buildTime > 0);
    T_ASSERT(building && building->health.max_value > 0.0f);

    if (worker->currentmove && worker->currentmove->think)
        worker->currentmove->think(worker);
    T_ASSERT(building && !M_IsDead(building));
    T_ASSERT(building && building->health.value > 0.0f);
}

TEST(wc3_building, removing_orc_construction_releases_internal_worker) {
    edict_t *worker;
    edict_t *building;

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
    edict_t *worker;
    edict_t *building;

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
    edict_t *worker;
    edict_t *building;
    UnitBalance_t balance;
    uint32_t release_time;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    balance = *building->data.UnitBalance;
    balance.buildTime = 60;
    building->data.UnitBalance = &balance;
    level.time = 1000;

    T_ASSERT(G_StartUndeadConstruction(worker, building));
    release_time = building->construction->worker_release_time;
    T_EQ(building->construction->type, CONSTRUCTION_UNDEAD);
    T_ASSERT(building->construction->worker == worker);
    T_ASSERT(!building->construction || !building->construction->worker_inside);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(!worker->paused);
    T_ASSERT(!worker->invulnerable);
    T_ASSERT(release_time > level.time);

    level.time = release_time;
    G_RunConstructionFrame(building);
    T_ASSERT(building->construction);
    T_NULL(building->construction->worker);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);
    T_FEQ(building->construction->progress, (float)FRAMETIME, 0.001f);
}

TEST(wc3_building, acolyte_builds_ziggurat_then_can_move_away) {
    static char const unsummon_ability_slk[] =
        "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
        "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
        "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"DataA1\"\nC;Y1;X9;K\"DataB1\"\n"
        "C;Y2;X1;K\"Auns\"\nC;Y2;X2;K\"Auns\"\nC;Y2;X3;K\"1\"\n"
        "C;Y2;X4;K\"structure,player\"\nC;Y2;X5;K\"15\"\nC;Y2;X6;K\"0\"\n"
        "C;Y2;X7;K\"0\"\nC;Y2;X8;K\"0.25\"\nC;Y2;X9;K\"80\"\nE\n";
    static char const ziggurat_balance_slk[] =
        "C;Y1;X1;K\"unitBalanceID\"\n"
        "C;Y1;X2;K\"goldcost\"\nC;Y1;X3;K\"lumbercost\"\n"
        "C;Y1;X4;K\"realHP\"\nC;Y1;X5;K\"bldtm\"\n"
        "C;Y1;X6;K\"fused\"\nC;Y1;X7;K\"fmade\"\n"
        "C;Y1;X8;K\"isbldg\"\n"
        "C;Y2;X1;K\"uzig\"\nC;Y2;X2;K100\nC;Y2;X3;K50\n"
        "C;Y2;X4;K1000\nC;Y2;X5;K60\nC;Y2;X6;K0\n"
        "C;Y2;X7;K0\nC;Y2;X8;K1\nE\n";
    gameClient_t *client;
    UnitData_t acolyte_data;
    UnitProfile_t acolyte_profile = { .builds = "uzig" };
    UnitBalance_t ziggurat_balance;
    edict_t *acolyte, *ziggurat = NULL;
    slkTestData_t *ability_rows, *old_ability, *balance_rows, *old_balance;
    vec2_t const build_point = { 64.0f, 0.0f };
    vec2_t const move_point = { -128.0f, 0.0f };
    uint32_t const acolyte_id = MAKEFOURCC('u', 'a', 'c', 'o');
    uint32_t const ziggurat_id = MAKEFOURCC('u', 'z', 'i', 'g');

    setup_test_world();
    client = &game.clients[0];
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 10000;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 10000;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    acolyte = alloc_test_unit(acolyte_id, 0.0f, 0.0f);
    ability_rows = parse_slk_string(unsummon_ability_slk);
    old_ability = G_SetSLKRows("AbilityData", ability_rows);
    balance_rows = parse_slk_string(ziggurat_balance_slk);
    old_balance = G_SetSLKRows("UnitBalance", balance_rows);
    acolyte_data = *acolyte->data.UnitData;
    acolyte_data.race = STR_UNDEAD;
    acolyte->data.UnitData = &acolyte_data;
    acolyte->data.UnitProfile = &acolyte_profile;
    T_ASSERT(G_UnitIsBuilding(ziggurat_id));
    T_ASSERT(G_WorkerCanBuild(acolyte, ziggurat_id));
    acolyte->s.player = client->ps.number;
    acolyte->svflags |= SVF_MONSTER;
    acolyte->movetype = MOVETYPE_STEP;
    acolyte->stand = unit_stand;
    acolyte->think = monster_think;
    acolyte->collision = 16.0f;
    acolyte->unitinfo.MoveSpeed = 190.0f;
    gi.LinkEntity(acolyte);

    T_ASSERT(G_IssueBuildOrder(acolyte, ziggurat_id, &build_point));
    FOR_LOOP(i, 120) {
        level.time += FRAMETIME;
        G_RunEntities();
        CM_ProcessPathJobs(65536);
        if (!ziggurat) FILTER_EDICTS(ent, ent->inuse && ent->class_id == ziggurat_id) {
            ziggurat = ent;
            ziggurat_balance = *ent->data.UnitBalance;
            ziggurat_balance.buildTime = 1;
            ent->data.UnitBalance = &ziggurat_balance;
            ent->svflags |= SVF_MONSTER;
            ent->health.max_value = 1000.0f;
            ent->health.value = ent->health.max_value;
            break;
        }
    }
    T_NOT_NULL(ziggurat);
    if (!ziggurat) {
        G_SetSLKRows("AbilityData", old_ability);
        free_slk_rows(ability_rows);
        G_SetSLKRows("UnitBalance", old_balance);
        free_slk_rows(balance_rows);
        return;
    }
    FOR_LOOP(i, 20) {
        level.time += FRAMETIME;
        G_RunEntities();
        CM_ProcessPathJobs(65536);
        if (!ziggurat->construction) break;
    }
    T_ASSERT(!ziggurat->construction);
    T_ASSERT(acolyte->inuse);
    T_NULL(acolyte->build);
    T_ASSERT(acolyte->goalentity && acolyte->goalentity->s.origin2.x == build_point.x);
    T_ASSERT(!(acolyte->s.renderfx & RF_HIDDEN));
    T_ASSERT(!acolyte->paused);
    T_ASSERT(!acolyte->invulnerable);

    order_move(acolyte, Waypoint_add(&move_point));
    FOR_LOOP(i, 20) {
        level.time += FRAMETIME;
        G_RunEntities();
        CM_ProcessPathJobs(65536);
        if (acolyte->s.origin2.x < -64.0f) break;
    }
    T_ASSERT(acolyte->s.origin2.x < -64.0f);
    T_ASSERT(acolyte->goalentity == NULL || acolyte->goalentity->s.origin2.x < -64.0f);

    acolyte->heroabilities[0] = MAKE(heroability_t, .code = MAKEFOURCC('A','u','n','s'), .level = 1);
    acolyte->mana.value = acolyte->mana.max_value = 100.0f;
    ziggurat->s.player = client->ps.number;
    ziggurat->svflags |= SVF_MONSTER;
    ziggurat->s.flags |= EF_BUILDING;
    ziggurat->targtype = TARG_STRUCTURE;
    ziggurat->die = unit_die;
    T_EQ(G_UnitAbilityLevel(acolyte, MAKEFOURCC('A','u','n','s')), 1);
    T_ASSERT(ziggurat->inuse);
    T_ASSERT(!M_IsDead(ziggurat));
    T_ASSERT(!S_UnitSpellImmune(ziggurat));
    T_ASSERT(S_SpellIsFriend(acolyte, ziggurat));
    T_ASSERT(S_SpellAllowsTarget(MAKEFOURCC('A','u','n','s'), acolyte, ziggurat));
    T_ASSERT(S_CastUnitTargetSpell(acolyte, MAKEFOURCC('A','u','n','s'), ziggurat));
    FOR_LOOP(i, 40) {
        level.time += FRAMETIME;
        G_RunEntities();
        CM_ProcessPathJobs(65536);
        if (G_UnitStatusLevel(ziggurat, MAKEFOURCC('B','u','n','s'))) break;
    }
    T_ASSERT(G_UnitStatusLevel(ziggurat, MAKEFOURCC('B','u','n','s')));
    T_ASSERT(ziggurat->health.value < ziggurat->health.max_value);
    G_SetSLKRows("AbilityData", old_ability);
    free_slk_rows(ability_rows);
    G_SetSLKRows("UnitBalance", old_balance);
    free_slk_rows(balance_rows);
}

TEST(wc3_building, cancelling_undead_construction_releases_summoner) {
    edict_t *worker;
    edict_t *building;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 64, 0);
    T_ASSERT(G_StartUndeadConstruction(worker, building));
    T_ASSERT(building->construction->worker == worker);

    G_StopConstruction(building);
    T_NULL(building->construction);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);
}

TEST(wc3_building, night_elf_ancient_cancel_restores_wisp_and_food) {
    edict_t *worker;
    edict_t *building;
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
    T_EQ(building->construction->type, CONSTRUCTION_NIGHTELF);
    T_ASSERT(building->construction->consumes_worker);
    T_ASSERT(!worker->food || worker->food->used == 0);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);

    G_StopConstruction(building);
    T_ASSERT(worker->inuse);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(!worker->paused);
    T_EQ(worker->food->used, worker_balance.foodUsed);
    T_NULL(building->construction);
}

TEST(wc3_building, night_elf_ancient_completion_consumes_wisp) {
    gameClient_t *saved_client;
    edict_t *worker;
    edict_t *building;
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
    T_ASSERT(building->construction->consumes_worker);
    saved_client = g_edicts[0].client;
    g_edicts[0].client = NULL;
    G_CompleteConstruction(building);
    g_edicts[0].client = saved_client;

    T_ASSERT(!worker->inuse);
    T_NULL(building->construction);
    T_EQ(building_stand_calls, 1);
}

TEST(wc3_building, night_elf_non_ancient_completion_releases_wisp) {
    edict_t *worker;
    edict_t *building;

    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h', 'p', 'e', 'a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h', 'b', 'a', 'r'), 64, 0);
    building->stand = building_test_stand;
    T_ASSERT(G_StartNightElfConstruction(worker, building));
    T_ASSERT(!building->construction || !building->construction->consumes_worker);
    T_ASSERT(worker->s.renderfx & RF_HIDDEN);

    G_CompleteConstruction(building);
    T_ASSERT(worker->inuse);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_ASSERT(!worker->paused);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);
}

TEST(wc3_building, cancelling_night_elf_non_ancient_construction_releases_wisp) {
    edict_t *worker;
    edict_t *building;

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
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    UnitProfile_t profile = { .builds = "hbar" };
    vec2_t point = { 64.0f, 64.0f };
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

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
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    edict_t *building;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    UnitBalance_t balance;
    slkTestData_t *rows, *old_abilities;
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

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
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->construction->paid = true;
    building->construction->payer = client->ps.number;
    building->construction->gold = balance.goldCost;
    building->construction->lumber = balance.lumberCost;
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
    T_NULL(building->construction);
    T_NULL(building->build);
    T_NULL(builder->build);
    T_ASSERT(!builder->buildwork || builder->buildwork->ability == 0);
    T_ASSERT(!building->food || building->food->used == 0);
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
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    edict_t *building;

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
    edict_t *clent;
    gameClient_t *client;
    edict_t *building;
    bool was_connected;

    setup_test_world();
    clent = &g_edicts[0];
    client = clent->client;
    was_connected = client->connected;
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 0);
    building->s.player = client->ps.number;
    building->svflags |= SVF_MONSTER;
    building->stand = unit_stand;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->construction->paid = true;
    building->construction->payer = client->ps.number;
    building->construction->gold = 100;
    building->construction->lumber = 80;
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
    edict_t *building;
    pathTex_t *pathtex;
    vec2_t point = { 0.0f, 0.0f };
    size_t const pathtex_size = sizeof(*pathtex) + sizeof(color32_t);

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
    gameClient_t *client = &game.clients[0];
    edict_t *builder;
    edict_t *building;
    UnitBalance_t balance;
    gameClient_t *saved_client;

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
    T_EQ(building->food->made, 10);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 10);
    T_EQ(level.events.write, 2);
    T_EQ(level.events.queue[0].type, EVENT_PLAYER_UNIT_CONSTRUCT_FINISH);
    T_ASSERT(level.events.queue[0].edict == building);
    T_EQ(level.events.queue[1].type, EVENT_UNIT_CONSTRUCT_FINISH);
    T_ASSERT(level.events.queue[1].edict == building);
}

TEST(wc3_building, completing_construction_clears_state_publishes_once_and_grants_food_once) {
    gameClient_t *client = &game.clients[0];
    edict_t *builder = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    UnitBalance_t balance = *building->data.UnitBalance;

    balance.foodMade = 6;
    building->data.UnitBalance = &balance;
    building->s.player = client->ps.number;
    building->health.max_value = 1000.0f;
    building->health.value = 400.0f;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->construction->paused = true;
    building->construction->type = CONSTRUCTION_HUMAN;
    building->construction->primary_builder = builder;
    building->construction->progress = 500.0f;
    building->construction->paid = true;
    building->construction->payer = client->ps.number;
    building->construction->gold = 100;
    building->construction->lumber = 50;
    building->aiflags |= AI_HOLD_FRAME;
    building->s.sound = 91;
    building->stand = building_test_stand;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 10;
    level.events.write = 0;
    level.events.read = 0;
    building_stand_calls = 0;

    {
        /* Prevent G_CompleteConstruction from invoking HUD refresh (which
         * requires FDF/UI state unavailable in tests) by hiding the player
         * entity from G_GetPlayerEntityByNumber while the call runs. */
        gameClient_t *saved_client = g_edicts[0].client;
        g_edicts[0].client = NULL;
        G_CompleteConstruction(building);
        g_edicts[0].client = saved_client;
    }

    T_NULL(building->construction);
    T_ASSERT(!(building->aiflags & AI_HOLD_FRAME));
    T_EQ(building->s.sound, 0);
    T_FEQ(building->health.value, building->health.max_value, 0.001f);
    T_EQ(client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP], 16);
    T_EQ(building->food->made, 6);
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

TEST(wc3_building, legacy_construction_death_clears_snapshot_loop) {
    setup_test_world();
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);

    building->build = building;
    building->s.sound = 91;
    unit_die(building, NULL);

    T_EQ(building->s.sound, 0);
}

TEST(wc3_building, plain_build_error_text_is_not_resolved_as_trigger_string_zero) {
    mapTrigStr_t zero = { .id = 0 };

    snprintf(zero.text, sizeof(zero.text), "Human02");
    ((mapInfo_t *)level.mapinfo)->strings = &zero;

    T_STREQ(G_LevelString("Unable to build there."), "Unable to build there.");
    T_STREQ(G_LevelString("TRIGSTR_0"), "Human02");
    T_STREQ(G_LevelString("TRIGSTR_bad"), "TRIGSTR_bad");
}

TEST(wc3_building, human_repair_capability_comes_from_unit_ability_list) {
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
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
    edict_t *builder;
    edict_t *building;
    pathTex_t *pathtex;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    slkTestData_t *rows, *old_abilities;
    size_t const pathtex_size = sizeof(*pathtex) +
                                FOOTPRINT_W * FOOTPRINT_H * sizeof(color32_t);

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
    gameClient_t *client = &game.clients[0];
    edict_t *worker;
    edict_t *building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    UnitBalance_t balance;
    gameClient_t *saved_client;
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
    edict_t *worker;
    edict_t *mechanical;
    edict_t *organic;
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
    edict_t *worker;
    edict_t *mechanical;
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
    edict_t *worker;
    edict_t *building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
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
    edict_t *primary;
    edict_t *ally;
    edict_t *building;
    UnitAbilities_t human_repair = { .abilList = "Arep" };
    AbilityData_t *repair;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
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
    T_ASSERT(building->construction->primary_builder == primary);
    T_ASSERT(!S_OrderRepair(ally, building, MAKEFOURCC('A','r','e','p')));
    T_ASSERT(building->construction->primary_builder == primary);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_datae_adds_range_only_for_floating_mechanical_units) {
    edict_t *worker;
    edict_t *floating;
    edict_t *ground;
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
    edict_t *worker;
    edict_t *ancient;
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
    edict_t *worker;
    edict_t *building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    vec2_t start;
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
    T_ASSERT(!worker->buildwork || worker->buildwork->ability == 0);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_button_then_target_issues_repair_order) {
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker;
    edict_t *building;
    UnitAbilities_t abilities = { .abilList = "Arep" };
    slkTestData_t *rows, *old_abilities;
    char target_number[16];
    cstring_t button[] = { "button", "Arep" };
    cstring_t select_target[] = { "select", target_number };

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
    T_EQ(worker->buildwork->ability, MAKEFOURCC('A','r','e','p'));
    T_NOT_NULL(worker->currentmove);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, spawn_initializes_authored_default_repair_autocast) {
    UnitAbilities_t abilities = {
        .abilList = "Aren",
        .defaultActiveAbility = MAKEFOURCC('A','r','e','n')
    };
    gameCommandButton_t button;
    edict_t *worker;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    worker->data.UnitAbilities = &abilities;

    SP_SpawnUnit(worker);

    T_EQ(worker->autocast_code, MAKEFOURCC('A','r','e','n'));
    T_ASSERT(worker->aiflags & AI_AUTOCAST_ACTIVE);
    T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
    T_ASSERT(G_UnitAutocastIsOn(worker, MAKEFOURCC('A','r','e','n')));
    T_ASSERT(G_BuildCommandButton(worker, "Aren", false, 0, &button));
    T_EQ(button.alternate_active, 1);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, spawn_without_default_active_repair_stays_disabled) {
    UnitAbilities_t abilities = { .abilList = "Aren" };
    gameCommandButton_t button;
    edict_t *worker, *building;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 96, 0);
    worker->data.UnitAbilities = &abilities;
    building->s.player = worker->s.player;
    building->health.max_value = 1000.0f;
    building->health.value = 500.0f;

    SP_SpawnUnit(worker);
    worker->runtime.acquisition_range = 400.0f;
    gi.LinkEntity(worker);
    gi.LinkEntity(building);

    T_EQ(worker->autocast_code, 0);
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_ACTIVE));
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_REPAIR));
    T_ASSERT(!G_UnitAutocastIsOn(worker, MAKEFOURCC('A','r','e','n')));
    T_ASSERT(!G_TryUnitAutocast(worker));
    T_NULL(worker->build);
    T_ASSERT(G_BuildCommandButton(worker, "Aren", false, 0, &button));
    T_EQ(button.alternate_active, 0);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, default_self_rally_does_not_autorepair_producer) {
    UnitBalance_t worker_balance = { .buildTime = 0, .foodUsed = 0, .foodMade = 0 };
    UnitAbilities_t worker_abilities = { .abilList = "Arep" };
    UnitProfile_t producer_profile = { .trains = "hpea" };
    bool old_instant_build;
    edict_t *worker, *producer;
    slkTestData_t *rows, *old_abilities;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    game.clients[0].ps.number = 0;
    old_instant_build = game.clients[0].cheat_instant_build;
    game.clients[0].cheat_instant_build = true;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    producer = alloc_test_unit(MAKEFOURCC('h','t','o','w'), 64, 0);
    worker->data.UnitAbilities = &worker_abilities;
    worker->s.player = game.clients[0].ps.number;
    producer->data.UnitProfile = &producer_profile;
    producer->s.flags |= EF_BUILDING;
    producer->svflags |= SVF_MONSTER;
    producer->s.player = worker->s.player;
    producer->targtype = TARG_STRUCTURE;
    producer->collision = 32.0f;
    producer->movetype = MOVETYPE_NONE;
    producer->stand = unit_stand;
    producer->health.max_value = 1000.0f;
    producer->health.value = 500.0f;
    worker->data.UnitBalance = &worker_balance;
    worker->collision = 16.0f;
    worker->stand = unit_stand;
    worker->training = true;
    worker->s.renderfx |= RF_HIDDEN;
    producer->build = worker;

    /* The unit starts production with Auto Repair off. Completion must not
     * Smart-interact with the damaged producer as though it were a rally order. */
    SP_SpawnUnit(worker);
    T_EQ(worker->autocast_code, 0);
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_REPAIR));
    ai_train_build(producer);
    T_ASSERT(!worker->training);
    T_ASSERT(!(worker->s.renderfx & RF_HIDDEN));
    T_EQ(worker->autocast_code, 0);
    T_ASSERT(!(worker->aiflags & AI_AUTOCAST_REPAIR));
    T_FEQ(producer->health.value, 500.0f, 0.01f);
    T_NULL(worker->movement.follow_target);
    T_ASSERT(worker->build != producer);
    T_ASSERT(worker->goalentity != producer);
    T_ASSERT(!worker->buildwork || worker->buildwork->ability == 0);

    game.clients[0].cheat_instant_build = old_instant_build;
    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_autocast_toggle_is_unit_state) {
    edict_t *worker;
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

TEST(wc3_building, autocast_command_updates_only_focused_unit_type_subgroup) {
    UnitAbilities_t abilities = { .abilList = "Aren" };
    cstring_t command[] = { "autocast", "Aren" };
    uint32_t const code = MAKEFOURCC('A','r','e','n');
    slkTestData_t *rows, *old_abilities;
    edict_t *first, *second, *other_type;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32, 0);
    other_type = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64, 0);
    edict_t *units[] = { first, second, other_type };
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        units[i]->data.UnitAbilities = &abilities;
        units[i]->s.player = 0;
        units[i]->selected = 1;
    }
    T_ASSERT(G_FocusSelectedUnit(game.clients, first));

    G_ClientCommand(g_edicts, 2, command);
    T_ASSERT(G_UnitAutocastIsOn(first, code));
    T_ASSERT(G_UnitAutocastIsOn(second, code));
    T_ASSERT(!G_UnitAutocastIsOn(other_type, code));

    G_ClientCommand(g_edicts, 2, command);
    T_ASSERT(!G_UnitAutocastIsOn(first, code));
    T_ASSERT(!G_UnitAutocastIsOn(second, code));
    T_ASSERT(!G_UnitAutocastIsOn(other_type, code));
    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, autocast_mixed_focused_subgroup_displays_off_and_normalizes_on) {
    UnitAbilities_t abilities = { .abilList = "Aren" };
    cstring_t command[] = { "autocast", "Aren" };
    uint32_t const code = MAKEFOURCC('A','r','e','n');
    gameCommandButton_t button;
    slkTestData_t *rows, *old_abilities;
    edict_t *first, *second;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    first = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    second = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 32, 0);
    edict_t *units[] = { first, second };
    FOR_LOOP(i, sizeof(units) / sizeof(units[0])) {
        units[i]->data.UnitAbilities = &abilities;
        units[i]->s.player = 0;
        units[i]->selected = 1;
    }
    T_ASSERT(G_FocusSelectedUnit(game.clients, first));
    T_ASSERT(G_SetUnitAutocast(first, code, true));
    T_ASSERT(!G_UnitAutocastIsOn(second, code));

    T_ASSERT(G_BuildCommandButton(first, "Aren", false, 0, &button));
    T_EQ(button.alternate_active, 0);

    G_ClientCommand(g_edicts, 2, command);
    T_ASSERT(G_UnitAutocastIsOn(first, code));
    T_ASSERT(G_UnitAutocastIsOn(second, code));
    T_ASSERT(G_BuildCommandButton(first, "Aren", false, 0, &button));
    T_EQ(button.alternate_active, 1);

    G_ClientCommand(g_edicts, 2, command);
    T_ASSERT(!G_UnitAutocastIsOn(first, code));
    T_ASSERT(!G_UnitAutocastIsOn(second, code));
    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repairon_and_repairoff_immediate_orders_toggle_without_starting_repair) {
    edict_t *worker;
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
    edict_t *worker;
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
    edict_t *worker;
    edict_t *near_building;
    edict_t *far_building;
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
    edict_t *worker;
    edict_t *mechanical;
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
    edict_t *worker;
    edict_t *building;
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
    edict_t *worker;
    edict_t *building;
    edict_t *waypoint;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;
    vec2_t destination = { 512.0f, 0.0f };

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
    T_NE(worker->buildwork->ability, 0);

    waypoint = Waypoint_add(&destination);
    T_NOT_NULL(waypoint);
    order_move(worker, waypoint);

    T_ASSERT(worker->goalentity == waypoint);
    T_NULL(worker->build);
    T_ASSERT(!worker->buildwork || worker->buildwork->ability == 0);
    T_ASSERT(worker->aiflags & AI_AUTOCAST_REPAIR);
    T_ASSERT(worker->aiflags & AI_AUTOCAST_ACTIVE);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, idle_acquisition_prefers_auto_repair_over_auto_attack) {
    edict_t *worker;
    edict_t *building;
    edict_t *enemy;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    ability_t const *repair;
    slkTestData_t *rows, *old_abilities;
    uint32_t stagger;

    old_abilities = building_install_repair_data(&rows);
    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
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
    stagger = (uint32_t)(worker - g_edicts) % 300;
    level.time = (300 - stagger) % 300;
    ai_stand(worker);

    T_ASSERT(worker->build == building);
    T_ASSERT(worker->goalentity == building);
    T_ASSERT(worker->goalentity != enemy);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, idle_acquisition_without_autocast_still_auto_attacks) {
    edict_t *worker;
    edict_t *enemy;
    uint32_t stagger;

    setup_test_world();
    ((mapInfo_t *)level.mapinfo)->players[0].playerType = kPlayerTypeHuman;
    ((mapInfo_t *)level.mapinfo)->players[1].playerType = kPlayerTypeHuman;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    enemy = alloc_test_unit(MAKEFOURCC('o','g','r','u'), 64, 0);
    worker->svflags |= SVF_MONSTER;
    worker->runtime.acquisition_range = 400.0f;
    worker->attack1.type = ATK_NORMAL;
    worker->attack1.cooldown = 1.0f;
    worker->attack1.damageBase = 1;
    worker->attack1.targetsAllowed = WC3_TARGET_FLAG_GROUND;
    enemy->s.player = 1;
    enemy->svflags |= SVF_MONSTER;
    enemy->targtype = TARG_GROUND;
    gi.LinkEntity(worker);
    gi.LinkEntity(enemy);

    stagger = (uint32_t)(worker - g_edicts) % 300;
    level.time = (300 - stagger) % 300;
    ai_stand(worker);

    T_ASSERT(worker->goalentity == enemy);
}

TEST(wc3_building, repair_autocast_ignores_full_health_nearer_building) {
    edict_t *worker;
    edict_t *full_building;
    edict_t *damaged_building;
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
    edict_t *worker;
    edict_t *building;
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
    T_EQ(worker->buildwork->ability, MAKEFOURCC('A','r','e','n'));

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, smart_order_repairs_damaged_owned_building) {
    edict_t *worker;
    edict_t *building;
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
    T_NE(worker->buildwork->ability, 0);
    T_ASSERT(worker->build == building);
    T_STREQ(worker->currentmove->animation, "stand work");

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_stops_and_releases_state_when_target_dies) {
    edict_t *worker;
    edict_t *building;
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

    T_ASSERT(!worker->buildwork || worker->buildwork->ability == 0);
    T_NULL(worker->build);
    T_NULL(worker->goalentity);

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, standard_repair_rejects_construction_and_human_requires_paused_state) {
    edict_t *human;
    edict_t *standard;
    edict_t *building;
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
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->construction->paused = true;
    building->construction->type = CONSTRUCTION_ORC;

    T_ASSERT(!S_OrderRepair(standard, building, MAKEFOURCC('A','r','e','n')));
    T_ASSERT(!S_OrderRepair(human, building, MAKEFOURCC('A','r','e','p')));
    building->construction->type = CONSTRUCTION_HUMAN;
    building->construction->paused = false;
    T_ASSERT(!S_OrderRepair(human, building, MAKEFOURCC('A','r','e','p')));

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, placement_cursor_uses_configured_player_color) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    gameClient_t *client;
    edict_t *worker;
    UnitProfile_t worker_profile = { .builds = "hbar" };
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');

    reset_entities();
    setup_test_world();
    clent = &g_edicts[0];
    client = &game.clients[0];
    clent->inuse = true;
    clent->client = client;
    client->connected = true;
    client->ps.number = 0;
    client->ps.color = 6;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    worker->data.UnitProfile = &worker_profile;
    worker->s.player = 0;
    worker->svflags |= SVF_MONSTER;
    G_SelectEntity(client, worker);

    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    building_cursor_entity_seen = false;
    building_cursor_player = MAX_PLAYERS;
    building_cursor_effect_flags = 0;
    gi.Write = building_capture_write;
    build_menu_selectlocation(clent, barracks);
    gi.Write = old_write;

    T_ASSERT(building_cursor_entity_seen);
    T_EQ(building_cursor_player, 0);
    T_EQ((building_cursor_effect_flags & EFX_TEAM_COLOR_MASK) >> EFX_TEAM_COLOR_SHIFT, 7);
}

static edict_t *building_queued_build_preview(edict_t *worker, uint32_t queue_offset) {
    unitOrder_t const *queued;
    uint32_t slot;

    if (!worker || queue_offset >= worker->order_queue.count) return NULL;
    slot = (worker->order_queue.head + queue_offset) % MAX_UNIT_ORDER_QUEUE;
    queued = &worker->order_queue.entries[slot];
    if (queued->target_type != UNIT_ORDER_TARGET_BUILD || !queued->target_number ||
        queued->target_number >= globals.num_edicts) {
        return NULL;
    }
    if (!g_edicts[queued->target_number].inuse ||
        g_edicts[queued->target_number].spawn_time != queued->target_spawn_time) {
        return NULL;
    }
    return &g_edicts[queued->target_number];
}

static edict_t *building_begin_barracks_placement(edict_t * *out_clent) {
    static UnitProfile_t const worker_profile = { .builds = "hbar" };
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    edict_t *clent;
    gameClient_t *client;
    edict_t *worker;

    setup_test_world();
    clent = &g_edicts[0];
    client = clent->client;
    clent->inuse = true;
    client->connected = true;
    client->ps.number = 0;
    client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = G_UnitBalance(barracks)->goldCost * 4;
    client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = G_UnitBalance(barracks)->lumberCost * 4;
    client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP] = 100;
    worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), -256, -256);
    worker->data.UnitProfile = &worker_profile;
    worker->s.player = 0;
    worker->svflags |= SVF_MONSTER;
    worker->stand = unit_stand;
    G_SelectEntity(client, worker);
    build_menu_selectlocation(clent, barracks);
    if (out_clent) *out_clent = clent;
    return worker;
}

TEST(wc3_building, build_placement_enables_shift_queue_targeting) {
    edict_t *clent;

    building_begin_barracks_placement(&clent);

    T_ASSERT(clent->client->menu.supports_order_queue);
    T_ASSERT(clent->client->menu.on_location_selected == build_menu_send_builder);
    T_ASSERT(!clent->client->menu.order_queue_chained);
}

TEST(wc3_building, normal_build_click_clears_placement_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    cstring_t command[] = { "point", "64", "64" };

    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    G_ClientCommand(clent, 3, command);
    gi.Write = old_write;

    T_EQ(clent->build_project, 0);
    T_NULL(clent->client->menu.on_location_selected);
    T_ASSERT(building_cursor_clear_seen);
    T_EQ(worker->build_project, MAKEFOURCC('h','b','a','r'));
}

TEST(wc3_building, shift_build_click_keeps_overlay_and_queues_followup_site) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    cstring_t first[] = { "point", "64", "64", "queue" };
    cstring_t second[] = { "point", "512", "64", "queue" };

    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    G_ClientCommand(clent, 4, first);

    T_EQ(clent->build_project, barracks);
    T_ASSERT(clent->client->menu.on_location_selected == build_menu_send_builder);
    T_ASSERT(clent->client->menu.order_queue_chained);
    T_ASSERT(!building_cursor_clear_seen);
    T_EQ(worker->build_project, barracks);
    T_EQ(G_UnitQueuedOrderCount(worker), 0);

    G_ClientCommand(clent, 4, second);
    gi.Write = old_write;

    T_EQ(clent->build_project, barracks);
    T_ASSERT(clent->client->menu.on_location_selected == build_menu_send_builder);
    T_ASSERT(!building_cursor_clear_seen);
    T_EQ(G_UnitQueuedOrderCount(worker), 1);
    T_EQ(worker->order_queue.entries[worker->order_queue.head].target_type, UNIT_ORDER_TARGET_BUILD);
    T_EQ(worker->order_queue.entries[worker->order_queue.head].order_id, barracks);
    T_FEQ(worker->order_queue.entries[worker->order_queue.head].point.x, 512.0f, 0.01f);
    T_FEQ(worker->order_queue.entries[worker->order_queue.head].point.y, 64.0f, 0.01f);
    {
        edict_t *preview = building_queued_build_preview(worker, 0);
        T_NOT_NULL(preview);
        T_ASSERT(preview != worker->build_preview);
        T_EQ(preview->class_id, barracks);
        T_EQ(preview->s.player, worker->s.player);
        T_ASSERT(preview->svflags & SVF_OWNER_ONLY);
        T_ASSERT(preview->s.flags & EF_NOT_SELECTABLE);
        T_FEQ(preview->s.origin2.x, 512.0f, 0.01f);
        T_FEQ(preview->s.origin2.y, 64.0f, 0.01f);
        T_ASSERT(preview->vertex_color_set);
        T_EQ(preview->vertex_color.a, 128);
    }
}

TEST(wc3_building, clearing_build_queue_removes_only_queued_placeholders) {
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    cstring_t first[] = { "point", "64", "64", "queue" };
    cstring_t second[] = { "point", "512", "64", "queue" };
    edict_t *active_preview;
    edict_t *queued_preview;
    uint32_t queued_spawn_time;

    G_ClientCommand(clent, 4, first);
    G_ClientCommand(clent, 4, second);
    active_preview = worker->build_preview;
    queued_preview = building_queued_build_preview(worker, 0);
    T_NOT_NULL(active_preview);
    T_NOT_NULL(queued_preview);
    queued_spawn_time = queued_preview->spawn_time;

    G_ClearUnitOrderQueue(worker);

    T_EQ(G_UnitQueuedOrderCount(worker), 0);
    T_ASSERT(active_preview->inuse);
    T_ASSERT(worker->build_preview == active_preview);
    T_ASSERT(!queued_preview->inuse || queued_preview->spawn_time != queued_spawn_time);
}

TEST(wc3_building, starting_queued_build_replaces_queued_placeholder_with_active_indicator) {
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    cstring_t first[] = { "point", "64", "64", "queue" };
    cstring_t second[] = { "point", "512", "64", "queue" };
    edict_t *queued_preview;
    uint32_t queued_spawn_time;

    G_ClientCommand(clent, 4, first);
    G_ClientCommand(clent, 4, second);
    queued_preview = building_queued_build_preview(worker, 0);
    T_NOT_NULL(queued_preview);
    queued_spawn_time = queued_preview->spawn_time;

    /* Simulate completion/cancellation of the current pre-spawn leg; the
     * normal stand edge then starts the next queued construction order. */
    G_ClearBuildPreview(worker);
    worker->build_project = 0;
    T_ASSERT(G_UnitStartNextQueuedOrder(worker));

    T_EQ(G_UnitQueuedOrderCount(worker), 0);
    T_ASSERT(!queued_preview->inuse || queued_preview->spawn_time != queued_spawn_time);
    T_NOT_NULL(worker->build_preview);
    T_ASSERT(worker->build_preview != queued_preview);
    T_FEQ(worker->build_preview->s.origin2.x, 512.0f, 0.01f);
    T_FEQ(worker->build_preview->s.origin2.y, 64.0f, 0.01f);
}

TEST(wc3_building, scheduler_starts_queued_build_after_current_move_completes) {
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    vec2_t const move_target = { 0.0f, 0.0f };
    cstring_t build[] = { "point", "512", "64", "queue" };
    edict_t *queued_preview;
    uint32_t queued_spawn_time;

    worker->movetype = MOVETYPE_STEP;
    worker->think = monster_think;
    worker->unitinfo.MoveSpeed = 190.0f;
    T_ASSERT(G_IssueUnitPointOrder(worker, "move", &move_target, false, 0, 0.0f));
    G_ClientCommand(clent, 4, build);
    queued_preview = building_queued_build_preview(worker, 0);
    T_NOT_NULL(queued_preview);
    if (!queued_preview) return;
    queued_spawn_time = queued_preview ? queued_preview->spawn_time : 0;
    T_NOT_NULL(worker->goalentity);
    T_NOT_NULL(worker->currentmove);
    if (!worker->currentmove) return;
    T_STREQ(worker->currentmove->animation, "walk");
    T_ASSERT(G_UnitHasActiveOrder(worker));
    T_EQ(G_UnitQueuedOrderCount(worker), 1);

    FOR_LOOP(i, 120) {
        level.time += FRAMETIME;
        G_RunEntities();
        CM_ProcessPathJobs(65536);
        if (!G_UnitQueuedOrderCount(worker)) break;
    }

    T_EQ(G_UnitQueuedOrderCount(worker), 0);
    T_EQ(worker->build_project, barracks);
    T_NOT_NULL(worker->build_preview);
    if (!worker->build_preview) return;
    T_ASSERT(worker->build_preview != queued_preview);
    T_FEQ(worker->build_preview->s.origin2.x, 512.0f, 0.01f);
    T_FEQ(worker->build_preview->s.origin2.y, 64.0f, 0.01f);
    T_ASSERT(!queued_preview->inuse || queued_preview->spawn_time != queued_spawn_time);
}

TEST(wc3_building, scheduler_discards_queued_build_that_loses_its_resources) {
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    vec2_t const move_target = { 0.0f, 0.0f };
    cstring_t build[] = { "point", "512", "64", "queue" };
    edict_t *queued_preview;
    uint32_t queued_spawn_time;

    worker->movetype = MOVETYPE_STEP;
    worker->think = monster_think;
    worker->unitinfo.MoveSpeed = 190.0f;
    T_ASSERT(G_IssueUnitPointOrder(worker, "move", &move_target, false, 0, 0.0f));
    G_ClientCommand(clent, 4, build);
    queued_preview = building_queued_build_preview(worker, 0);
    T_NOT_NULL(queued_preview);
    if (!queued_preview) return;
    queued_spawn_time = queued_preview ? queued_preview->spawn_time : 0;
    T_NOT_NULL(worker->goalentity);
    T_NOT_NULL(worker->currentmove);
    if (!worker->currentmove) return;
    T_STREQ(worker->currentmove->animation, "walk");
    T_ASSERT(G_UnitHasActiveOrder(worker));
    T_EQ(G_UnitQueuedOrderCount(worker), 1);
    clent->client->ps.stats[PLAYERSTATE_RESOURCE_GOLD] = 0;
    clent->client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER] = 0;

    FOR_LOOP(i, 120) {
        level.time += FRAMETIME;
        G_RunEntities();
        CM_ProcessPathJobs(65536);
        if (!G_UnitQueuedOrderCount(worker)) break;
    }

    T_EQ(G_UnitQueuedOrderCount(worker), 0);
    T_EQ(worker->build_project, 0);
    T_NULL(worker->build_preview);
    T_ASSERT(!queued_preview->inuse || queued_preview->spawn_time != queued_spawn_time);
}

TEST(wc3_building, queued_build_payload_and_indicator_survive_save_load) {
    cstring_t filename = Test_TempPath("wc3-save-queued-build.bin");
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    vec2_t const point = { 512.0f, 64.0f };
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    edict_t *preview = G_CreateBuildPreview(worker, barracks, &point);
    uint32_t const worker_number = worker->s.number;
    uint32_t const preview_number = preview ? preview->s.number : 0;
    uint32_t const preview_spawn_time = preview ? preview->spawn_time : 0;

    T_ASSERT(preview != NULL);
    if (!preview) return;
    T_ASSERT(G_QueueUnitOrder(worker, "build", UNIT_ORDER_TARGET_BUILD, &point,
                              preview, clent->client->ps.number, 0.0f, barracks));
    strlcpy(level.map_path, "Maps\\Campaign\\QueuedBuildSaveTest.w3m", sizeof(level.map_path));
    T_ASSERT(WriteGame(filename));

    memset(&worker->order_queue, 0, sizeof(worker->order_queue));
    preview->inuse = false;
    T_ASSERT(ReadGame(filename));
    worker = g_edicts + worker_number;
    preview = g_edicts + preview_number;
    T_EQ(G_UnitQueuedOrderCount(worker), 1);
    T_EQ(worker->order_queue.entries[worker->order_queue.head].target_type, UNIT_ORDER_TARGET_BUILD);
    T_EQ(worker->order_queue.entries[worker->order_queue.head].order_id, barracks);
    T_EQ(worker->order_queue.entries[worker->order_queue.head].target_number, preview_number);
    T_EQ(worker->order_queue.entries[worker->order_queue.head].target_spawn_time, preview_spawn_time);
    T_ASSERT(preview->inuse);
    G_ClearUnitOrderQueue(worker);
    T_EQ(G_UnitQueuedOrderCount(worker), 0);
    T_ASSERT(!preview->inuse || preview->spawn_time != preview_spawn_time);
    remove(filename);
}

TEST(wc3_building, removing_worker_clears_active_and_queued_build_placeholders) {
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    cstring_t first[] = { "point", "64", "64", "queue" };
    cstring_t second[] = { "point", "512", "64", "queue" };
    edict_t *active_preview;
    edict_t *queued_preview;
    uint32_t active_spawn_time, queued_spawn_time;

    G_ClientCommand(clent, 4, first);
    G_ClientCommand(clent, 4, second);
    active_preview = worker->build_preview;
    queued_preview = building_queued_build_preview(worker, 0);
    T_NOT_NULL(active_preview);
    T_NOT_NULL(queued_preview);
    active_spawn_time = active_preview->spawn_time;
    queued_spawn_time = queued_preview->spawn_time;

    G_FreeEdict(worker);

    T_ASSERT(!active_preview->inuse || active_preview->spawn_time != active_spawn_time);
    T_ASSERT(!queued_preview->inuse || queued_preview->spawn_time != queued_spawn_time);
}

TEST(wc3_building, shift_release_before_success_does_not_cancel_build_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    cstring_t release[] = { "orderqueuerelease" };

    building_begin_barracks_placement(&clent);
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    G_ClientCommand(clent, 1, release);
    gi.Write = old_write;

    T_EQ(clent->build_project, barracks);
    T_ASSERT(clent->client->menu.on_location_selected == build_menu_send_builder);
    T_ASSERT(!building_cursor_clear_seen);
}

TEST(wc3_building, final_shift_release_clears_overlay_without_discarding_build_orders) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    cstring_t first[] = { "point", "64", "64", "queue" };
    cstring_t second[] = { "point", "512", "64", "queue" };
    cstring_t release[] = { "orderqueuerelease" };

    G_ClientCommand(clent, 4, first);
    G_ClientCommand(clent, 4, second);
    T_EQ(G_UnitQueuedOrderCount(worker), 1);
    edict_t *queued_preview = building_queued_build_preview(worker, 0);
    T_NOT_NULL(queued_preview);
    uint32_t const queued_preview_spawn_time = queued_preview->spawn_time;

    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    G_ClientCommand(clent, 1, release);
    gi.Write = old_write;

    T_EQ(clent->build_project, 0);
    T_NULL(clent->client->menu.on_location_selected);
    T_ASSERT(building_cursor_clear_seen);
    T_EQ(worker->build_project, barracks);
    T_EQ(G_UnitQueuedOrderCount(worker), 1);
    T_ASSERT(queued_preview->inuse);
    T_EQ(queued_preview->spawn_time, queued_preview_spawn_time);
}

TEST(wc3_building, invalid_nonshift_build_click_keeps_overlay_armed) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    cstring_t command[] = { "point", "5000", "5000" };

    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    G_ClientCommand(clent, 3, command);
    gi.Write = old_write;

    T_EQ(clent->build_project, barracks);
    T_ASSERT(clent->client->menu.on_location_selected == build_menu_send_builder);
    T_ASSERT(!clent->client->menu.order_queue_chained);
    T_ASSERT(!building_cursor_clear_seen);
    T_EQ(worker->build_project, 0);
    T_EQ(G_UnitQueuedOrderCount(worker), 0);
}

TEST(wc3_building, invalid_shift_build_click_keeps_overlay_armed) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    uint32_t const barracks = MAKEFOURCC('h','b','a','r');
    cstring_t command[] = { "point", "5000", "5000", "queue" };

    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    G_ClientCommand(clent, 4, command);
    gi.Write = old_write;

    T_EQ(clent->build_project, barracks);
    T_ASSERT(clent->client->menu.on_location_selected == build_menu_send_builder);
    T_ASSERT(!clent->client->menu.order_queue_chained);
    T_ASSERT(!building_cursor_clear_seen);
    T_EQ(worker->build_project, 0);
    T_EQ(G_UnitQueuedOrderCount(worker), 0);
}

TEST(wc3_building, selection_replacement_cancels_build_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);
    edict_t *other = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 128);
    char number[16];
    cstring_t command[] = { "select", number };

    other->s.player = 0;
    other->svflags |= SVF_MONSTER;
    snprintf(number, sizeof(number), "%u", (unsigned)other->s.number);
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    G_ClientCommand(clent, 2, command);
    gi.Write = old_write;

    T_EQ(clent->build_project, 0);
    T_NULL(clent->client->menu.on_location_selected);
    T_ASSERT(building_cursor_clear_seen);
    T_ASSERT(!G_IsEntitySelected(clent->client, worker));
    T_ASSERT(G_IsEntitySelected(clent->client, other));
}

TEST(wc3_building, command_card_refresh_cancels_build_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;

    building_begin_barracks_placement(&clent);
    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    Get_Commands_f(clent);
    gi.Write = old_write;

    T_EQ(clent->build_project, 0);
    T_NULL(clent->client->menu.on_location_selected);
    T_ASSERT(building_cursor_clear_seen);
}

TEST(wc3_building, selected_worker_death_cancels_build_overlay) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent;
    edict_t *worker = building_begin_barracks_placement(&clent);

    building_cursor_opcode_seen = false;
    building_cursor_clear_seen = false;
    gi.Write = building_capture_write;
    unit_die(worker, NULL);
    gi.Write = old_write;

    T_EQ(clent->build_project, 0);
    T_NULL(clent->client->menu.on_location_selected);
    T_ASSERT(building_cursor_clear_seen);
    T_ASSERT(!G_IsEntitySelected(clent->client, worker));
}

TEST(wc3_building, cancel_command_clears_active_build_placement_cursor) {
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;

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
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    cstring_t command[] = { "smartpoint", "256", "256" };

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
    edict_t *clent = &g_edicts[0];
    gameClient_t *client = clent->client;
    edict_t *worker = alloc_test_unit(MAKEFOURCC('h','p','e','a'), 0, 0);
    edict_t *target = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 128, 0);
    char target_number[16];
    cstring_t command[] = { "smart", target_number };

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
    edict_t *worker;
    edict_t *building;
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
    T_EQ(worker->buildwork->ability, MAKEFOURCC('A','r','e','n'));
    T_ASSERT(worker->build == building);
    T_ASSERT(worker->goalentity == building);
    T_STREQ(worker->currentmove->animation, "walk");

    building_restore_repair_data(old_abilities, rows);
}

TEST(wc3_building, repair_walk_handoff_requires_actual_contact) {
    gameClient_t *client = &game.clients[0];
    edict_t *worker;
    edict_t *building;
    UnitAbilities_t abilities = { .abilList = "Aren" };
    AbilityData_t *repair;
    UnitBalance_t balance;
    slkTestData_t *rows, *old_abilities;
    float interaction;
    float step;
    float hp_before;

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
    edict_t *building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 64, 64);
    UnitBalance_t balance = *building->data.UnitBalance;
    animation_t birth = {
        .name = "birth",
        .interval = { 1000, 3000 },
    };

    balance.buildTime = 10;
    building->data.UnitBalance = &balance;
    building->animation = &birth;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->construction->paused = true;
    building->aiflags |= AI_HOLD_FRAME;

    building->construction->progress = 2500.0f;
    M_MoveFrame(building);
    T_EQ(building->s.frame, 1500);

    building->construction->progress = 7500.0f;
    M_MoveFrame(building);
    T_EQ(building->s.frame, 2500);

    /* No progress means no visual drift while construction is paused. */
    M_MoveFrame(building);
    T_EQ(building->s.frame, 2500);
}

TEST(wc3_building, primary_human_builder_ignores_datad_but_extra_builder_requires_it) {
    edict_t *primary;
    edict_t *extra;
    edict_t *building;
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
    T_ASSERT(primary->buildwork->primary);
    T_ASSERT(primary->build == building);
    T_ASSERT(building->construction->primary_builder == primary);

    T_ASSERT(!S_OrderRepair(extra, building, MAKEFOURCC('A','r','e','p')));
    T_ASSERT(!extra->buildwork || extra->buildwork->ability == 0);
    T_NULL(extra->build);

    building_restore_repair_data(old_abilities, rows);
}

/* Stock Build Tiny data shape: the item carries one AbilityData alias whose
 * UnitID1 names the structure and Dur1 the autonomous build time. AIbt is a
 * zero-duration row and AIbg omits UnitID1 so the cursor cannot resolve. */
static char const building_tiny_ability_slk[] =
    "ID;PWXL;N;EBB;Y5;X14\n"
    "C;Y1;X1;K\"alias\"\nC;Y1;X2;K\"code\"\nC;Y1;X3;K\"levels\"\n"
    "C;Y1;X4;K\"targs\"\nC;Y1;X5;K\"Cost1\"\nC;Y1;X6;K\"Cool1\"\n"
    "C;Y1;X7;K\"Rng1\"\nC;Y1;X8;K\"Dur1\"\nC;Y1;X9;K\"HeroDur1\"\n"
    "C;Y1;X10;K\"DataA1\"\nC;Y1;X11;K\"DataB1\"\nC;Y1;X12;K\"DataC1\"\n"
    "C;Y1;X13;K\"DataD1\"\nC;Y1;X14;K\"UnitID1\"\n"
    "C;Y2;X1;K\"AInv\"\nC;Y2;X2;K\"AInv\"\nC;Y2;X3;K\"1\"\n"
    "C;Y2;X10;K\"6\"\nC;Y2;X12;K\"1\"\n"
    "C;Y3;X1;K\"AIbl\"\nC;Y3;X2;K\"AIbl\"\nC;Y3;X3;K\"1\"\n"
    "C;Y3;X4;K\"ground\"\nC;Y3;X5;K\"0\"\nC;Y3;X6;K\"0\"\n"
    "C;Y3;X7;K\"600\"\nC;Y3;X8;K\"3\"\nC;Y3;X14;K\"hbar\"\n"
    "C;Y4;X1;K\"AIbt\"\nC;Y4;X2;K\"AIbl\"\nC;Y4;X3;K\"1\"\n"
    "C;Y4;X4;K\"ground\"\nC;Y4;X7;K\"600\"\nC;Y4;X8;K\"0\"\nC;Y4;X14;K\"hbar\"\n"
    "C;Y5;X1;K\"AIbg\"\nC;Y5;X2;K\"AIbl\"\nC;Y5;X3;K\"1\"\n"
    "C;Y5;X4;K\"ground\"\nC;Y5;X7;K\"600\"\nC;Y5;X8;K\"3\"\nE\n";

typedef struct {
    edict_t *player, *hero, *item;
    slkTestData_t *rows, *old;
} buildingTinyFixture_t;

/* A selected inventory unit carrying one two-charge Tiny Structure item,
 * standing in cast range of the (64,64) footprint the tests place on. A
 * non-hero carrier keeps the headless info panel off the hero attribute
 * frames, which only exist once the HUD FDF is loaded. */
static buildingTinyFixture_t building_tiny_fixture(cstring_t ability) {
    static UnitAbilities_t const abilities = { .abilList = "AInv", .heroAbilList = "" };
    static ItemData_t item_data;
    buildingTinyFixture_t f;

    setup_test_world();
    f.rows = parse_slk_string(building_tiny_ability_slk);
    f.old = G_SetSLKRows("AbilityData", f.rows);
    f.player = &g_edicts[0]; f.player->client->ps.number = 0;
    f.hero = alloc_test_unit(MAKEFOURCC('h','f','o','o'), -200.0f, -200.0f);
    f.hero->data.UnitAbilities = &abilities; f.hero->s.player = 0; f.hero->svflags |= SVF_MONSTER;
    f.hero->targtype = TARG_GROUND; f.hero->health.value = f.hero->health.max_value = 100;
    f.hero->think = monster_think; f.hero->stand = unit_stand; f.hero->movetype = MOVETYPE_STEP;
    f.hero->collision = 16.0f; unit_stand(f.hero); gi.LinkEntity(f.hero);
    item_data = MAKE(ItemData_t, .abilList = ability, .uses = 2, .perishable = false);
    f.item = alloc_test_unit(MAKEFOURCC('s','t','w','p'), -200.0f, -200.0f);
    f.item->s.model = 1; f.item->movetype = MOVETYPE_NONE; f.item->targtype = TARG_ITEM;
    if (!f.item->item) f.item->item = G_AllocItem();
    assert(f.item->item);
    f.item->item->carrier = NULL; f.item->item->inventory_slot = -1; f.item->item->in_world = true;
    f.item->data.ItemData = &item_data; f.item->item->charges = 2; f.item->spawn_time = 1234;
    gi.LinkEntity(f.item);
    T_ASSERT(G_AddItemToSlot(f.hero, f.item, 0));
    G_SelectEntity(f.player->client, f.hero);
    return f;
}

static void building_tiny_restore(buildingTinyFixture_t const *f) {
    G_SetSLKRows("AbilityData", f->old);
    free_slk_rows(f->rows);
}

static edict_t *building_find_tiny_structure(void) {
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == MAKEFOURCC('h','b','a','r')) return ent;
    return NULL;
}

static uint32_t building_count_inuse_edicts(void) {
    uint32_t count = 0;
    FILTER_EDICTS(ent, ent->inuse) count++;
    return count;
}

TEST(wc3_building, tiny_structure_item_spawns_tiny_construction_and_completes_on_authored_duration) {
    buildingTinyFixture_t f = building_tiny_fixture("AIbl");
    cstring_t click[] = { "point", "64", "64" };
    edict_t *building;
    UnitBalance_t balance;

    G_UseItem(f.hero, 0);
    T_NOT_NULL(f.player->client->menu.on_location_selected);
    T_EQ(f.player->client->menu.ability_item, f.item);
    G_ClientCommand(f.player, 3, click);

    building = building_find_tiny_structure();
    T_NOT_NULL(building);
    if (!building) { building_tiny_restore(&f); return; }
    T_EQ(building->s.player, 0);
    T_NOT_NULL(building->construction);
    if (!building->construction) { building_tiny_restore(&f); return; }
    T_EQ(building->construction->type, CONSTRUCTION_TINY);
    T_FEQ(building->construction->duration_ms, 3000.0f, 0.001f);
    T_ASSERT(building->build == building);
    T_ASSERT(building->health.value < building->health.max_value);
    /* Only a successful A_EXECUTE consumes the item; the targeting state is released with it. */
    T_EQ(G_ItemCharges(f.item), 1);
    T_NULL(f.player->client->menu.on_location_selected);
    T_NULL(f.player->client->menu.ability_item);

    /* The structure's normal build time must not drive the Tiny clock. */
    balance = *building->data.UnitBalance;
    balance.buildTime = 100;
    building->data.UnitBalance = &balance;
    T_ASSERT(run_test_jass("function main takes nothing returns nothing\nendfunction\n"));
    level.started = true;
    level.scriptsStarted = true;
    FOR_LOOP(frame, 15) globals.RunFrame();
    T_NOT_NULL(building->construction);
    if (building->construction) T_FEQ(building->construction->progress, 15.0f * FRAMETIME, 0.001f);
    FOR_LOOP(frame, 15) globals.RunFrame();
    T_ASSERT(building->inuse);
    T_NULL(building->construction);
    T_NULL(building->build);
    T_FEQ(building->health.value, building->health.max_value, 0.001f);
    building->data.UnitBalance = NULL;
    building_tiny_restore(&f);
}

TEST(wc3_building, tiny_structure_blocked_footprint_keeps_item_and_leaves_no_structure) {
    buildingTinyFixture_t f = building_tiny_fixture("AIbl");
    cstring_t click[] = { "point", "64", "64" };
    edict_t *blocker = alloc_test_unit(MAKEFOURCC('h','f','o','o'), 64.0f, 64.0f);
    uint32_t inuse;

    blocker->svflags |= SVF_MONSTER;
    blocker->collision = 16.0f;
    gi.LinkEntity(blocker);
    G_UseItem(f.hero, 0);
    T_NOT_NULL(f.player->client->menu.on_location_selected);
    inuse = building_count_inuse_edicts();
    G_ClientCommand(f.player, 3, click);
    T_NULL(building_find_tiny_structure());
    T_EQ(building_count_inuse_edicts(), inuse);
    T_EQ(G_ItemCharges(f.item), 2);
    building_tiny_restore(&f);
}

TEST(wc3_building, tiny_structure_zero_duration_completes_immediately) {
    buildingTinyFixture_t f = building_tiny_fixture("AIbt");
    cstring_t click[] = { "point", "64", "64" };
    edict_t *building;

    G_UseItem(f.hero, 0);
    G_ClientCommand(f.player, 3, click);
    building = building_find_tiny_structure();
    T_NOT_NULL(building);
    if (building) {
        T_NULL(building->construction);
        T_NULL(building->build);
        T_FEQ(building->health.value, building->health.max_value, 0.001f);
    }
    T_EQ(G_ItemCharges(f.item), 1);
    building_tiny_restore(&f);
}

/* An unresolvable Tiny cursor (AIbg without UnitID1) must not leave the
 * client in a half-open target mode where only a dead Cancel button exists. */
TEST(wc3_building, tiny_structure_without_cursor_adds_no_cancel_button) {
    buildingTinyFixture_t f = building_tiny_fixture("AIbg");
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    void (*old_unicast)(edict_t *) = gi.unicast;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;

    building_commandbar_layouts = 0; building_last_byte = -1;
    gi.Write = building_layout_capture_write; gi.unicast = building_test_unicast;
    gi.ImageIndex = building_test_image_index;
    G_UseItem(f.hero, 0);
    gi.Write = old_write; gi.unicast = old_unicast; gi.ImageIndex = old_image_index;
    T_NULL(f.player->client->menu.on_location_selected);
    T_NULL(f.player->client->menu.ability_item);
    T_EQ(building_commandbar_layouts, 0);
    T_EQ(G_ItemCharges(f.item), 2);
    building_tiny_restore(&f);
}

/* The info-panel build timer follows the Tiny clock, not the structure's buildTime. */
TEST(wc3_building, tiny_construction_queue_timer_uses_authored_duration) {
    gameClient_t *client = &game.clients[0];
    edict_t *building;
    UnitBalance_t balance;
    void (*old_write)(pfWriteType_t, void const *) = gi.Write;
    int (*old_image_index)(cstring_t) = gi.ImageIndex;

    setup_test_world();
    building = alloc_test_unit(MAKEFOURCC('h','b','a','r'), 0, 0);
    balance = *building->data.UnitBalance;
    balance.buildTime = 100;
    building->data.UnitBalance = &balance;
    building->s.player = client->ps.number;
    building->build = building;
    if (!building->construction) building->construction = G_AllocConstruction();
    assert(building->construction);
    building->construction->type = CONSTRUCTION_TINY;
    building->construction->duration_ms = 9000.0f;
    building->health.value = building->health.max_value * 0.5f;
    gi.Write = building_queue_capture_write;
    gi.ImageIndex = building_test_image_index;
    building_queue_frame_count = 0;
    UI_WriteStart(LAYER_INFOPANEL);
    UI_WriteBuildQueue(building, client);
    T_EQ(building_queue_frame_count, 1);
    T_EQ(building_queue_numitems, 1);
    T_EQ(building_queue_endtime - building_queue_starttime, 9000);

    building->build = NULL;
    G_FreeConstruction(building);
    building->data.UnitBalance = NULL;
    gi.Write = old_write;
    gi.ImageIndex = old_image_index;
}

#endif
