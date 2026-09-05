/*
 * t_slk.c — In-engine SLK data reading and unit-stat tests.
 *
 * Part 1 (pure functions): typed cache decode and lookup tests.
 * Part 2 (unit stats): real archive data for hpea/hfoo.
 * Part 3 (typed table replacement): G_SetSLKRows tests mana/armor edge cases.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"
#include "common/stb_slk.h"

void setup_test_world(void);

slkTestData_t *parse_slk_string(const char *slk_text) {
    static slkField_t const schema[] = { { NULL, 0, 0 } };
    void *rows = NULL;
    DWORD count = Stb_SlkLoadBuffer(slk_text, schema, &rows, sizeof(DWORD));
    slkTestData_t *data;
    if (!count) return NULL;
    FS_SLKFreeRows(schema, rows, count, sizeof(DWORD));
    data = calloc(1, sizeof(*data));
    if (data) data->text = slk_text;
    return data;
}

void free_slk_rows(slkTestData_t *data) {
    if (!data) return;
    free(data->rows); free(data);
}

static LPCSTR find_slk_value(slkTestData_t const *data, LPCSTR row, LPCSTR column) {
    typedef struct { DWORD id; LPCSTR value; } row_t;
    slkField_t schema[] = {
        { "", offsetof(row_t, id), STB_SLK_FOURCC },
        { column, offsetof(row_t, value), STB_SLK_STR },
        { NULL, 0, 0 }
    };
    row_t *rows = NULL;
    DWORD count, key;
    static char value[1024];
    bool has_value = false;
    if (!data) return NULL;
    count = Stb_SlkLoadBuffer(data->text, schema, (void **)&rows, sizeof(row_t));
    if (!count) return NULL;
    key = FS_SLKKey(row);
    FOR_LOOP(i, count) {
        if (rows[i].id == key && rows[i].value) {
            snprintf(value, sizeof(value), "%s", rows[i].value);
            has_value = true; break;
        }
    }
    FS_SLKFreeRows(schema, rows, count, sizeof(row_t));
    return has_value ? value : NULL;
}

/* -----------------------------------------------------------------------
 * 1.  Typed cache lookup
 * --------------------------------------------------------------------- */

TEST(wc3_slk, find_cell_existing_row_and_column) {
    slkTestData_t *rows = parse_slk_string("C;Y1;X1;K\"id\"\nC;Y1;X2;K\"spd\"\nC;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"270\"\nE\n");
    T_STREQ(find_slk_value(rows, "hpea", "spd"), "270");
}

TEST(wc3_slk, find_cell_missing_row_returns_null) {
    slkTestData_t *rows = parse_slk_string("C;Y1;X1;K\"id\"\nC;Y1;X2;K\"spd\"\nC;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"270\"\nE\n");
    T_NULL(find_slk_value(rows, "hfoo", "spd"));
}

TEST(wc3_slk, find_cell_missing_column_returns_null) {
    slkTestData_t *rows = parse_slk_string("C;Y1;X1;K\"id\"\nC;Y1;X2;K\"spd\"\nC;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"270\"\nE\n");
    T_NULL(find_slk_value(rows, "hpea", "hp"));
}

TEST(wc3_slk, find_cell_case_insensitive_column) {
    slkTestData_t *rows = parse_slk_string("C;Y1;X1;K\"id\"\nC;Y1;X2;K\"RealHP\"\nC;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"250\"\nE\n");
    T_STREQ(find_slk_value(rows, "hpea", "realHP"), "250");
    T_STREQ(find_slk_value(rows, "hpea", "REALHP"), "250");
}

TEST(wc3_slk, find_cell_multiple_rows) {
    slkTestData_t *rows = parse_slk_string("C;Y1;X1;K\"id\"\nC;Y1;X2;K\"spd\"\nC;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"270\"\nC;Y3;X1;K\"hfoo\"\nC;Y3;X2;K\"300\"\nE\n");
    T_STREQ(find_slk_value(rows, "hpea", "spd"), "270");
    T_STREQ(find_slk_value(rows, "hfoo", "spd"), "300");
}

TEST(wc3_slk, find_cell_multiple_fields) {
    slkTestData_t *rows = parse_slk_string("C;Y1;X1;K\"id\"\nC;Y1;X2;K\"spd\"\nC;Y1;X3;K\"realHP\"\nC;Y2;X1;K\"hpea\"\nC;Y2;X2;K\"270\"\nC;Y2;X3;K\"250\"\nE\n");
    T_STREQ(find_slk_value(rows, "hpea", "spd"), "270");
    T_STREQ(find_slk_value(rows, "hpea", "realHP"), "250");
}

TEST(wc3_slk, find_cell_null_sheet_returns_null) {
    T_NULL(find_slk_value(NULL, "hpea", "spd"));
}

TEST(wc3_slk, typed_strings_are_owned_and_alias_safe) {
    typedef struct { LPCSTR name; } testRow_t;
    static slkField_t const schema[] = {
        { "Name", offsetof(testRow_t, name), STB_SLK_STR },
        { "name", offsetof(testRow_t, name), STB_SLK_STR },
        { NULL, 0, 0 }
    };
    char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"Name\"\n"
        "C;Y1;X3;K\"name\"\n"
        "C;Y2;X1;K\"hfoo\"\n"
        "C;Y2;X2;K\"Footman\"\n"
        "C;Y2;X3;K\"Knight\"\n"
        "E\n";
    testRow_t *rows = NULL;
    LPSTR alias = strstr(src, "Knight");
    DWORD count = Stb_SlkLoadBuffer(src, schema, (void **)&rows, sizeof(testRow_t));

    T_ASSERT(count == 1);
    T_NOT_NULL(alias);
    alias[0] = 'Y';
    T_STREQ(rows[0].name, "Knight");
    FS_SLKFreeRows(schema, rows, count, sizeof(testRow_t));
}

TEST(wc3_slk, profile_ddx_and_fourcc_metadata_share_typed_row) {
    slkTestData_t *row = parse_slk_string("C;Y1;X1;K\"id\"\nC;Y1;X2;K\"Name\"\nC;Y1;X3;K\"Missilespeed\"\nC;Y1;X4;K\"MissileHoming\"\nC;Y2;X1;K\"hrif\"\nC;Y2;X2;K\"Rifleman\"\nC;Y2;X3;K\"900\"\nC;Y2;X4;K\"TRUE\"\nE\n");
    slkTestData_t *old = G_SetProfileRows(row);
    DWORD id = MAKEFOURCC('h','r','i','f');
    edict_t unit = { .class_id = id };
    G_BindEntityData(&unit);

    T_STREQ(G_UnitProfile(id)->name, "Rifleman");
    T_FEQ(G_UnitProfile(id)->attack[0].speed, 900.f, 0.01f);
    T_STREQ(UnitMetaString(&unit, MAKEFOURCC('u','n','a','m')), "Rifleman");
    T_FEQ(UnitMetaReal(&unit, MAKEFOURCC('u','a','1','z')), 900.f, 0.01f);
    T_ASSERT(UnitMetaBoolean(&unit, MAKEFOURCC('u','m','h','1')));
    G_SetProfileRows(old);
}

TEST(wc3_slk, slk_fourcc_metadata_reads_typed_row) {
    DWORD id = MAKEFOURCC('h','p','e','a');
    edict_t unit = { .class_id = id };
    G_BindEntityData(&unit);
    T_FEQ(UnitMetaReal(&unit, MAKEFOURCC('u','m','v','s')), unit.data.UnitBalance->speed, 0.01f);
}

TEST(wc3_slk, fourcc_metadata_reads_rows_cached_on_edict) {
    UnitProfile_t profile = { .name = "Cached Unit" };
    UnitBalance_t balance = { .level = 17, .speed = 321.5f };
    UnitUI_t ui = { .hideHeroBar = true };
    edict_t unit = { .data.UnitProfile = &profile, .data.UnitBalance = &balance, .data.UnitUI = &ui };

    T_STREQ(UnitMetaString(&unit, MAKEFOURCC('u','n','a','m')), "Cached Unit");
    T_EQ(UnitMetaInteger(&unit, MAKEFOURCC('u','l','e','v')), 17);
    T_FEQ(UnitMetaReal(&unit, MAKEFOURCC('u','m','v','s')), 321.5f, 0.01f);
    T_ASSERT(UnitMetaBoolean(&unit, MAKEFOURCC('u','h','h','b')));
}

/* -----------------------------------------------------------------------
 * 2.  In-memory SLK parsing (parse_slk_string)
 * --------------------------------------------------------------------- */

static const char slk_two_units[] =
    "ID;PWXL;N;EBB;Y3;X4\n"
    "C;Y1;X1;K\"unitBalanceID\"\n"
    "C;Y1;X2;K\"spd\"\n"
    "C;Y1;X3;K\"realHP\"\n"
    "C;Y1;X4;K\"bldtm\"\n"
    "C;Y2;X1;K\"hpea\"\n"
    "C;Y2;X2;K\"270\"\n"
    "C;Y2;X3;K\"250\"\n"
    "C;Y2;X4;K\"45\"\n"
    "C;Y3;X1;K\"hfoo\"\n"
    "C;Y3;X2;K\"270\"\n"
    "C;Y3;X3;K\"420\"\n"
    "C;Y3;X4;K\"60\"\n"
    "E\n";

TEST(wc3_slk, parse_returns_non_null) {
    slkTestData_t *rows = parse_slk_string(slk_two_units);
    T_NOT_NULL(rows);
    free_slk_rows(rows);
}

TEST(wc3_slk, parse_row_names) {
    slkTestData_t *rows = parse_slk_string(slk_two_units);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "hpea", "spd"), "270");
    T_STREQ(find_slk_value(rows, "hfoo", "spd"), "270");
    free_slk_rows(rows);
}

TEST(wc3_slk, parse_field_values) {
    slkTestData_t *rows = parse_slk_string(slk_two_units);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "hpea", "spd"),    "270");
    T_STREQ(find_slk_value(rows, "hpea", "realHP"), "250");
    T_STREQ(find_slk_value(rows, "hpea", "bldtm"),  "45");
    T_STREQ(find_slk_value(rows, "hfoo", "realHP"), "420");
    T_STREQ(find_slk_value(rows, "hfoo", "bldtm"),  "60");
    free_slk_rows(rows);
}

TEST(wc3_slk, parse_missing_cell_returns_null) {
    slkTestData_t *rows = parse_slk_string(slk_two_units);
    T_NOT_NULL(rows);
    T_NULL(find_slk_value(rows, "hkni", "spd"));
    T_NULL(find_slk_value(rows, "hpea", "armor"));
    free_slk_rows(rows);
}

TEST(wc3_slk, parse_empty_string_returns_null) {
    slkTestData_t *rows = parse_slk_string("ID;PWXL\nE\n");
    T_NULL(rows);
}

/* -----------------------------------------------------------------------
 * 3.  Unit stat accessors — real archive data
 * --------------------------------------------------------------------- */

TEST(wc3_slk, unit_speed_peasant) {
    setup_test_world();
    FLOAT speed = G_UnitBalance(MAKEFOURCC('h','p','e','a'))->speed;
    T_ASSERT(speed == 190.0f || speed == 270.0f); /* TFT / ROC */
}

TEST(wc3_slk, unit_speed_footman) {
    T_EQ(G_UnitBalance(MAKEFOURCC('h','f','o','o'))->id, MAKEFOURCC('h','f','o','o'));
    T_FEQ(G_UnitBalance(MAKEFOURCC('h','f','o','o'))->speed, 270.0f, 0.01f);
}

TEST(wc3_slk, unit_hp_peasant) {
    FLOAT hp = G_UnitBalance(MAKEFOURCC('h','p','e','a'))->maxHealth;
    T_ASSERT(hp == 220.0f || hp == 250.0f); /* TFT / ROC */
}

TEST(wc3_slk, unit_hp_footman) {
    T_FEQ(G_UnitBalance(MAKEFOURCC('h','f','o','o'))->maxHealth, 420.0f, 0.01f);
}

TEST(wc3_slk, unit_build_time_peasant) {
    LONG build = G_UnitBalance(MAKEFOURCC('h','p','e','a'))->buildTime;
    T_ASSERT(build == 15 || build == 45); /* TFT / ROC */
}

TEST(wc3_slk, unit_build_time_footman) {
    LONG build = G_UnitBalance(MAKEFOURCC('h','f','o','o'))->buildTime;
    T_ASSERT(build == 20 || build == 60); /* TFT / ROC */
}

TEST(wc3_slk, unit_collision_peasant) {
    T_EQ(G_UnitCollision(MAKEFOURCC('h','p','e','a')), 16);
}

TEST(wc3_slk, global_array_backs_spawned_unit) {
    edict_t ent = { .class_id = MAKEFOURCC('h','f','o','o') };
    T_NOT_NULL(g_UnitBalance);
    T_ASSERT(g_UnitBalanceCount > 0);
    SP_CallSpawn(&ent);
    T_ASSERT(ent.data.UnitProfile == G_UnitProfile(ent.class_id));
    T_ASSERT(ent.data.UnitBalance == G_UnitBalance(ent.class_id));
    T_ASSERT(ent.data.UnitData == G_UnitData(ent.class_id));
    T_ASSERT(ent.data.UnitUI == G_UnitUI(ent.class_id));
    T_ASSERT(ent.data.UnitWeapons == G_UnitWeapons(ent.class_id));
    T_ASSERT(ent.data.UnitAbilities == G_UnitAbil(ent.class_id));
}

TEST(wc3_slk, unit_model_filename_preserves_authored_extension) {
    PATHSTR path;

    G_NormalizeModelFilename("Units\\Campaign\\Hero\\Hero.mdl", path, sizeof(path));
    T_STREQ(path, "Units\\Campaign\\Hero\\Hero.mdl");
    G_NormalizeModelFilename("war3mapImported/CustomHero.mdx", path, sizeof(path));
    T_STREQ(path, "war3mapImported/CustomHero.mdx");
}

TEST(wc3_slk, unit_model_filename_adds_mdx_to_base_slk_stem) {
    PATHSTR path;

    G_NormalizeModelFilename("Units\\Human\\Footman\\Footman", path, sizeof(path));
    T_STREQ(path, "Units\\Human\\Footman\\Footman.mdx");
}

TEST(wc3_slk, map_custom_unit_ui_overrides_model_and_scale) {
    static const char slk_ui[] =
        "C;Y1;X1;K\"unitUIID\"\n"
        "C;Y1;X2;K\"file\"\n"
        "C;Y1;X3;K\"modelScale\"\n"
        "C;Y2;X1;K\"hfoo\"\n"
        "C;Y2;X2;K\"Units\\Human\\Footman\\Footman\"\n"
        "C;Y2;X3;K\"1.0\"\n"
        "E\n";
    DWORD const base_id = MAKEFOURCC('h','f','o','o');
    DWORD const custom_id = MAKEFOURCC('x','f','o','o');
    LPCMAPINFO saved_mapinfo;
    slkTestData_t *rows = parse_slk_string(slk_ui);
    slkTestData_t *saved_ui;
    slkTestData_t *replaced_ui;
    UnitUI_t const *base;
    FLOAT scale = 1.75f;
    unitModification_t mods[] = {
        { .modID = MAKEFOURCC('u','m','d','l'), .type = mod_string, .data = (HANDLE)"Units\\Campaign\\CorrectHero\\CorrectHero" },
        { .modID = MAKEFOURCC('u','s','c','a'), .type = mod_real, .data = &scale },
    };
    unitData_t custom = {
        .originalUnitID = base_id, .newUnitID = custom_id,
        .numbeOfModifications = 2, .modifications = mods
    };
    MAPINFO mapinfo = { .num_userCreatedUnits = 1, .userCreatedUnits = &custom };

    setup_test_world();
    T_NOT_NULL(rows);
    saved_ui = G_SetSLKRows("UnitUI", rows);
    T_NOT_NULL(saved_ui);
    saved_mapinfo = level.mapinfo;
    base = G_UnitUI(base_id);
    T_STREQ(base->modelFile, "Units\\Human\\Footman\\Footman");
    level.mapinfo = &mapinfo;
    G_SetMapUnitOverrides(&mapinfo);

    T_EQ(G_UnitUI(custom_id)->id, custom_id);
    T_STREQ(G_UnitUI(custom_id)->modelFile, "Units\\Campaign\\CorrectHero\\CorrectHero");
    T_FEQ(G_UnitUI(custom_id)->modelScale, 1.75f, 0.001f);
    T_STREQ(G_UnitUI(base_id)->modelFile, base->modelFile);
    {
        edict_t unit = { .class_id = custom_id };
        G_BindEntityData(&unit);
        T_ASSERT(unit.data.UnitUI == G_UnitUI(custom_id));
        T_STREQ(unit.data.UnitUI->modelFile, "Units\\Campaign\\CorrectHero\\CorrectHero");
    }

    G_SetMapUnitOverrides(NULL);
    level.mapinfo = saved_mapinfo;
    replaced_ui = G_SetSLKRows("UnitUI", saved_ui);
    free_slk_rows(replaced_ui);
    free_slk_rows(saved_ui);
    free_slk_rows(rows);
}

TEST(wc3_slk, map_original_unit_ui_override_is_custom_inheritance_source) {
    DWORD const base_id = MAKEFOURCC('h','f','o','o');
    DWORD const custom_id = MAKEFOURCC('x','f','o','o');
    LPCMAPINFO saved_mapinfo;
    unitModification_t model = {
        .modID = MAKEFOURCC('u','m','d','l'), .type = mod_string,
        .data = (HANDLE)"Units\\Campaign\\OriginalOverride\\OriginalOverride"
    };
    unitData_t original = {
        .originalUnitID = base_id, .numbeOfModifications = 1, .modifications = &model
    };
    unitData_t custom = { .originalUnitID = base_id, .newUnitID = custom_id };
    MAPINFO mapinfo = {
        .num_originalUnits = 1, .originalUnits = &original,
        .num_userCreatedUnits = 1, .userCreatedUnits = &custom
    };

    setup_test_world();
    saved_mapinfo = level.mapinfo;
    level.mapinfo = &mapinfo;
    G_SetMapUnitOverrides(&mapinfo);

    T_STREQ(G_UnitUI(base_id)->modelFile, "Units\\Campaign\\OriginalOverride\\OriginalOverride");
    T_STREQ(G_UnitUI(custom_id)->modelFile, "Units\\Campaign\\OriginalOverride\\OriginalOverride");

    G_SetMapUnitOverrides(NULL);
    level.mapinfo = saved_mapinfo;
}

TEST(wc3_slk, map_custom_unit_ui_rows_are_stable_per_unit) {
    DWORD const base_id = MAKEFOURCC('h','f','o','o');
    DWORD const first_id = MAKEFOURCC('x','f','o','1');
    DWORD const second_id = MAKEFOURCC('x','f','o','2');
    LPCMAPINFO saved_mapinfo;
    unitModification_t models[] = {
        { .modID = MAKEFOURCC('u','m','d','l'), .type = mod_string, .data = (HANDLE)"Units\\Campaign\\First\\First" },
        { .modID = MAKEFOURCC('u','m','d','l'), .type = mod_string, .data = (HANDLE)"Units\\Campaign\\Second\\Second" },
    };
    unitData_t custom[] = {
        { .originalUnitID = base_id, .newUnitID = first_id, .numbeOfModifications = 1, .modifications = &models[0] },
        { .originalUnitID = base_id, .newUnitID = second_id, .numbeOfModifications = 1, .modifications = &models[1] },
    };
    MAPINFO mapinfo = { .num_userCreatedUnits = 2, .userCreatedUnits = custom };
    UnitUI_t const *first;
    UnitUI_t const *second;

    setup_test_world();
    saved_mapinfo = level.mapinfo;
    level.mapinfo = &mapinfo;
    G_SetMapUnitOverrides(&mapinfo);
    first = G_UnitUI(first_id);
    second = G_UnitUI(second_id);

    T_ASSERT(first != second);
    T_STREQ(first->modelFile, "Units\\Campaign\\First\\First");
    T_STREQ(second->modelFile, "Units\\Campaign\\Second\\Second");
    T_STREQ(first->modelFile, "Units\\Campaign\\First\\First");

    G_SetMapUnitOverrides(NULL);
    level.mapinfo = saved_mapinfo;
}


TEST(wc3_slk, required_animation_names_select_matching_alternate_sequence) {
    animation_t animations[] = {
        { .name = "Stand" },
        { .name = "Stand Alternate" },
        { .name = "Walk" },
        { .name = "Walk Alternate" },
    };
    LPCANIMATION selected;

    selected = G_SelectAnimationForProperties(animations, 4, "stand", "alternate");
    T_NOT_NULL(selected);
    if (selected) T_STREQ(selected->name, "Stand Alternate");

    selected = G_SelectAnimationForProperties(animations, 4, "walk", "alternate");
    T_NOT_NULL(selected);
    if (selected) T_STREQ(selected->name, "Walk Alternate");

    selected = G_SelectAnimationForProperties(animations, 4, "stand", "");
    T_NOT_NULL(selected);
    if (selected) T_STREQ(selected->name, "Stand");
}

TEST(wc3_slk, required_animation_names_alternateex_falls_back_to_alternate_sequences) {
    animation_t animations[] = {
        { .name = "Stand" },
        { .name = "Stand Alternate" },
        { .name = "Walk" },
        { .name = "Walk Alternate" },
    };
    LPCANIMATION selected;

    selected = G_SelectAnimationForProperties(animations, 4, "stand", "alternateex");
    T_NOT_NULL(selected);
    if (selected) T_STREQ(selected->name, "Stand Alternate");

    selected = G_SelectAnimationForProperties(animations, 4, "walk", "alternateex");
    T_NOT_NULL(selected);
    if (selected) T_STREQ(selected->name, "Walk Alternate");
}

TEST(wc3_slk, required_animation_names_alternateex_prefers_real_alternateex_sequence) {
    animation_t animations[] = {
        { .name = "Stand" },
        { .name = "Stand Alternate" },
        { .name = "Stand AlternateEx" },
    };
    LPCANIMATION selected = G_SelectAnimationForProperties(animations, 3, "stand", "alternateex");

    T_NOT_NULL(selected);
    if (selected) T_STREQ(selected->name, "Stand AlternateEx");
}

TEST(wc3_slk, required_animation_names_combine_order_tags_with_unit_tags) {
    animation_t animations[] = {
        { .name = "Stand Ready" },
        { .name = "Stand Alternate" },
        { .name = "Stand Ready Alternate" },
    };
    LPCANIMATION selected = G_SelectAnimationForProperties(animations, 3, "stand ready", "alternate");

    T_NOT_NULL(selected);
    if (selected) T_STREQ(selected->name, "Stand Ready Alternate");
}

TEST(wc3_slk, unit_animation_properties_add_and_remove_persistent_tags) {
    UnitProfile_t profile = { .animProps = "alternate" };
    edict_t unit = { .class_id = MAKEFOURCC('n','m','d','m'), .data.UnitProfile = &profile };

    G_ResetUnitAnimationProperties(&unit);
    T_STREQ(unit.animation_props, "alternate");

    G_AddUnitAnimationProperties(&unit, "work", true);
    T_STREQ(unit.animation_props, "alternate,work");
    G_AddUnitAnimationProperties(&unit, "alternate", false);
    T_STREQ(unit.animation_props, "work");
}

TEST(wc3_slk, map_original_required_animation_names_feed_custom_inheritance) {
    DWORD const base_id = MAKEFOURCC('h','f','o','o');
    DWORD const custom_id = MAKEFOURCC('x','f','o','b');
    LPCMAPINFO saved_mapinfo;
    unitModification_t anim_props = {
        .modID = MAKEFOURCC('u','a','n','i'), .type = mod_string, .data = (HANDLE)"alternate"
    };
    unitData_t original = {
        .originalUnitID = base_id, .numbeOfModifications = 1, .modifications = &anim_props
    };
    unitData_t custom = { .originalUnitID = base_id, .newUnitID = custom_id };
    MAPINFO mapinfo = {
        .num_originalUnits = 1, .originalUnits = &original,
        .num_userCreatedUnits = 1, .userCreatedUnits = &custom
    };

    setup_test_world();
    saved_mapinfo = level.mapinfo;
    level.mapinfo = &mapinfo;
    G_SetMapUnitOverrides(&mapinfo);

    T_STREQ(G_UnitProfile(base_id)->animProps, "alternate");
    T_STREQ(G_UnitProfile(custom_id)->animProps, "alternate");

    G_SetMapUnitOverrides(NULL);
    level.mapinfo = saved_mapinfo;
}

TEST(wc3_slk, map_custom_unit_profile_overrides_required_animation_names) {
    DWORD const base_id = MAKEFOURCC('h','f','o','o');
    DWORD const custom_id = MAKEFOURCC('x','f','o','a');
    LPCMAPINFO saved_mapinfo;
    unitModification_t anim_props = {
        .modID = MAKEFOURCC('u','a','n','i'), .type = mod_string, .data = (HANDLE)"alternate"
    };
    unitData_t custom = {
        .originalUnitID = base_id, .newUnitID = custom_id,
        .numbeOfModifications = 1, .modifications = &anim_props
    };
    MAPINFO mapinfo = { .num_userCreatedUnits = 1, .userCreatedUnits = &custom };
    edict_t unit = { .class_id = custom_id };

    setup_test_world();
    saved_mapinfo = level.mapinfo;
    level.mapinfo = &mapinfo;
    G_SetMapUnitOverrides(&mapinfo);

    T_EQ(G_UnitProfile(custom_id)->id, custom_id);
    T_STREQ(G_UnitProfile(custom_id)->animProps, "alternate");
    G_BindEntityData(&unit);
    T_STREQ(UnitMetaString(&unit, MAKEFOURCC('u','a','n','i')), "alternate");
    G_ResetUnitAnimationProperties(&unit);
    T_STREQ(unit.animation_props, "alternate");

    G_SetMapUnitOverrides(NULL);
    level.mapinfo = saved_mapinfo;
}

TEST(wc3_slk, weapon_columns_decode_into_attack_records) {
    LPCSTR slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"unitWeaponID\"\nC;Y1;X2;K\"dmgplus1\"\nC;Y1;X3;K\"dmgplus2\"\n"
        "C;Y1;X4;K\"rangeN1\"\nC;Y1;X5;K\"rangeN2\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;Y2;X2;K12\nC;Y2;X3;K34\nC;Y2;X4;K90\nC;Y2;X5;K600\nE\n";
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *saved = G_SetSLKRows("UnitWeapons", rows);
    UnitWeapons_t const *weapons = G_UnitWeapons(MAKEFOURCC('h','f','o','o'));
    T_ASSERT(weapons == g_UnitWeapons);
    T_EQ(weapons->attack1.damageBase, 12); T_EQ(weapons->attack2.damageBase, 34);
    T_FEQ(weapons->attack1.range, 90.0f, 0.01f); T_FEQ(weapons->attack2.range, 600.0f, 0.01f);
    G_SetSLKRows("UnitWeapons", saved); free_slk_rows(rows);
}

TEST(wc3_slk, ability_buff_ui_columns_decode) {
    AbilityBuffData_t const *buff = G_AbilityBuffData(MAKEFOURCC('B','i','m','l'));
    T_EQ(buff->id, MAKEFOURCC('B','i','m','l'));
    T_STREQ(buff->buffArt, "ReplaceableTextures\\CommandButtons\\BTNImmolationOn.blp");
    T_STREQ(buff->buffTip, "Immolation");
    T_STREQ(buff->targetArt, "TestUI\\Models\\anim_pulse.mdx");
    T_STREQ(buff->specialArt, "TestUI\\Models\\panel_sprite.mdx");
    T_STREQ(buff->effectArt, "TestUI\\Models\\quad_sprite.mdx");
    T_STREQ(buff->missileArt, "TestUI\\Models\\ui_panel.mdx");
}

TEST(wc3_slk, upgrade_class_column_decodes) {
    UpgradeData_t const *upgrade = G_UpgradeData(MAKEFOURCC('R','h','m','e'));
    T_EQ(upgrade->id, MAKEFOURCC('R','h','m','e'));
    T_STREQ(upgrade->upgradeClass, "melee");
}

TEST(wc3_slk, unit_unknown_id_returns_zero) {
    T_FEQ(G_UnitBalance(MAKEFOURCC('x','x','x','x'))->speed,      0.0f, 0.01f);
    T_FEQ(G_UnitBalance(MAKEFOURCC('x','x','x','x'))->maxHealth,  0.0f, 0.01f);
    T_EQ  (G_UnitBalance(MAKEFOURCC('x','x','x','x'))->buildTime, 0);
}

/* -----------------------------------------------------------------------
 * 4.  Mana / armor edge cases via typed table replacement
 * --------------------------------------------------------------------- */

TEST(wc3_slk, mana_uses_realM_not_manaN) {
    static const char slk_mana[] =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"unitBalanceID\"\n"
        "C;Y1;X2;K\"manaN\"\n"
        "C;Y1;X3;K\"realM\"\n"
        "C;Y1;X4;K\"mana0\"\n"
        "C;Y2;X1;K\"Ewar\"\n"
        "C;Y2;X2;K\"0\"\n"
        "C;Y2;X3;K\"225\"\n"
        "C;Y2;X4;K\"100\"\n"
        "C;Y3;X1;K\"hsor\"\n"
        "C;Y3;X2;K\"200\"\n"
        "C;Y3;X3;K\"200\"\n"
        "C;Y3;X4;K\"75\"\n"
        "E\n";
    slkTestData_t *rows = parse_slk_string(slk_mana);
    T_NOT_NULL(rows);
    slkTestData_t *saved_mana = G_SetSLKRows("UnitBalance", rows);

    T_FEQ(G_UnitBalance(MAKEFOURCC('E','w','a','r'))->maxMana, 225.0f, 0.01f);
    T_FEQ(G_UnitBalance(MAKEFOURCC('E','w','a','r'))->initialMana, 100.0f, 0.01f);
    T_FEQ(G_UnitBalance(MAKEFOURCC('h','s','o','r'))->maxMana, 200.0f, 0.01f);
    T_FEQ(G_UnitBalance(MAKEFOURCC('h','s','o','r'))->initialMana, 75.0f, 0.01f);

    /* Restore the archive table before freeing the temporary rows; later suites share this metadata. */
    G_SetSLKRows("UnitBalance", saved_mana);
    free_slk_rows(rows);
}

TEST(wc3_slk, armor_uses_realdef_not_def) {
    static const char slk_armor[] =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"unitBalanceID\"\n"
        "C;Y1;X2;K\"def\"\n"
        "C;Y1;X3;K\"realdef\"\n"
        "C;Y1;X4;K\"spd\"\n"
        "C;Y2;X1;K\"Ewar\"\n"
        "C;Y2;X2;K\"0\"\n"
        "C;Y2;X3;K\"4\"\n"
        "C;Y2;X4;K\"270\"\n"
        "C;Y3;X1;K\"hfoo\"\n"
        "C;Y3;X2;K\"2\"\n"
        "C;Y3;X3;K\"2\"\n"
        "C;Y3;X4;K\"270\"\n"
        "E\n";
    slkTestData_t *rows = parse_slk_string(slk_armor);
    T_NOT_NULL(rows);
    slkTestData_t *saved_arm = G_SetSLKRows("UnitBalance", rows);

    T_FEQ(G_UnitBalance(MAKEFOURCC('E','w','a','r'))->armor, 4.0f, 0.01f);
    T_FEQ(G_UnitBalance(MAKEFOURCC('h','f','o','o'))->armor, 2.0f, 0.01f);

    G_SetSLKRows("UnitBalance", saved_arm);
    free_slk_rows(rows);
}


static PATHSTR spawn_tex;
static DWORD spawn_images;
static int capture_spawn_image(LPCSTR name) {
    snprintf(spawn_tex, sizeof(spawn_tex), "%s", name); spawn_images++; return 42;
}

/* Drive the real spawn path with SLK texFile values: no replacement, extensionless art, and a source TGA name. */
TEST(wc3_slk, destructable_texture_preserves_extension_and_absent_sentinel) {
    static LPCSTR const names[] = { "_", "", "ReplaceableTextures\\Cliff\\Cliff0.tga",
                                   "ReplaceableTextures\\LordaeronTree\\LordaeronSummerTree" };
    slkTestData_t *saved = NULL;
    int (*old_index)(LPCSTR) = gi.ImageIndex;
    setup_test_world();
    gi.ImageIndex = capture_spawn_image;
    FOR_LOOP(i, sizeof(names) / sizeof(names[0])) {
        char slk[1024];
        snprintf(slk, sizeof(slk), "ID;PWXL;N;E\nC;Y1;X1;K\"ID\"\nC;Y1;X2;K\"file\"\nC;Y1;X3;K\"texFile\"\nC;Y1;X4;K\"targType\"\nC;Y2;X1;K\"LT05\"\nC;Y2;X2;K\"Cliff\"\nC;Y2;X3;K\"%s\"\nC;Y2;X4;K\"debris\"\nE\n", names[i]);
        slkTestData_t *rows = parse_slk_string(slk);
        if (!saved) saved = G_SetSLKRows("DestructableData", rows);
        else G_SetSLKRows("DestructableData", rows);
        edict_t ent = { .class_id = MAKEFOURCC('L','T','0','5') };
        spawn_images = 0;
        SP_CallSpawn(&ent);
        T_EQ(ent.s.image, i < 2 ? 0 : 42);
        T_EQ(spawn_images, i < 2 ? 0 : 1);
        if (i >= 2) T_STREQ(spawn_tex, names[i]);
        G_SetSLKRows("DestructableData", saved);
        free_slk_rows(rows);
    }
    gi.ImageIndex = old_index;
}

TEST(wc3_slk, doodad_fields_use_typed_row) {
    LPCSTR slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"doodID\"\nC;Y1;X2;K\"dir\"\nC;Y1;X3;K\"file\"\n"
        "C;Y2;X1;K\"LTlt\"\nC;Y2;X2;K\"Doodads\\Terrain\"\nC;Y2;X3;K\"Tree\"\nE\n";
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *saved = G_SetSLKRows("Doodads", rows);
    Doodads_t const *row = G_Doodad(MAKEFOURCC('L','T','l','t'));
    T_EQ(row->id, MAKEFOURCC('L','T','l','t'));
    T_STREQ(row->dir, "Doodads\\Terrain"); T_STREQ(row->file, "Tree");
    G_SetSLKRows("Doodads", saved); free_slk_rows(rows);
}

TEST(wc3_slk, uber_splat_fields_use_typed_row) {
    LPCSTR slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"Name\"\nC;Y1;X2;K\"Dir\"\nC;Y1;X3;K\"file\"\nC;Y1;X4;K\"Scale\"\n"
        "C;Y2;X1;K\"HMtp\"\nC;Y2;X2;K\"Splats\"\nC;Y2;X3;K\"TownHall\"\nC;Y2;X4;K4\nE\n";
    slkTestData_t *rows = parse_slk_string(slk);
    slkTestData_t *saved = G_SetSLKRows("UberSplatData", rows);
    UberSplatData_t const *row = G_UberSplat(MAKEFOURCC('H','M','t','p'));
    T_STREQ(row->Dir, "Splats"); T_STREQ(row->file, "TownHall"); T_FEQ(row->Scale, 4.0f, 0.01f);
    G_SetSLKRows("UberSplatData", saved); free_slk_rows(rows);
}

/* -----------------------------------------------------------------------
 * 5.  Production SLK buffer parser through Stb_SlkLoadBuffer
 * --------------------------------------------------------------------- */

/* Omitted Y uses stateful current row; ROC tables rely on this heavily. */
TEST(wc3_slk, prod_stateful_y) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"HP\"\n"
        "C;Y2;X1;K\"hero\"\n"
        "C;X2;K\"500\"\n"   /* Y omitted: Y=2 carries from previous C */
        "E\n";
    slkTestData_t *rows = parse_slk_string(src);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "hero", "HP"), "500");
}

/* Multiple columns populated using only omitted Y (stateful row context). */
TEST(wc3_slk, prod_stateful_xy) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"HP\"\n"
        "C;Y1;X3;K\"MP\"\n"
        "C;Y2;X1;K\"mage\"\n"
        "C;X2;K\"500\"\n"   /* Y=2 carries; X=2: HP */
        "C;X3;K\"200\"\n"   /* Y=2 carries; X=3: MP */
        "E\n";
    slkTestData_t *rows = parse_slk_string(src);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "mage", "HP"), "500");
    T_STREQ(find_slk_value(rows, "mage", "MP"), "200");
}

/* F record advances X without creating a cell; next C inherits the updated X. */
TEST(wc3_slk, prod_f_advances_x) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"HP\"\n"
        "C;Y2;X1;K\"mage\"\n"
        "F;X2\n"            /* advance X to 2, no K → no cell */
        "C;Y2;K\"400\"\n"   /* X=2 from F, Y=2 explicit: HP=400 */
        "E\n";
    slkTestData_t *rows = parse_slk_string(src);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "mage", "HP"), "400");
}

/* B record dimension bounds are advisory; wrong values must not corrupt output. */
TEST(wc3_slk, prod_b_record_advisory) {
    static const char src[] =
        "B;Y999;X999\n"     /* wildly overstated bounds, must be ignored */
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"HP\"\n"
        "C;Y2;X1;K\"unit\"\n"
        "C;Y2;X2;K\"300\"\n"
        "E\n";
    slkTestData_t *rows = parse_slk_string(src);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "unit", "HP"), "300");
}

/* Semicolons inside a quoted K value must not split the field. */
TEST(wc3_slk, prod_quoted_semicolon) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"path\"\n"
        "C;Y2;X1;K\"itm1\"\n"
        "C;Y2;X2;K\"foo;bar\"\n"  /* semicolon inside quotes */
        "E\n";
    slkTestData_t *rows = parse_slk_string(src);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "itm1", "path"), "foo;bar");
}

/* "" inside a quoted K value decodes to a single literal double-quote. */
TEST(wc3_slk, prod_quote_escape) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"note\"\n"
        "C;Y2;X1;K\"obj1\"\n"
        "C;Y2;X2;K\"foo\"\"bar\"\n"  /* SLK: K"foo""bar" → foo"bar */
        "E\n";
    slkTestData_t *rows = parse_slk_string(src);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "obj1", "note"), "foo\"bar");
}

/* Cells with Y=0 must be skipped; valid rows below must still parse. */
TEST(wc3_slk, prod_zero_y_ignored) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"HP\"\n"
        "C;Y0;X2;K\"BAD\"\n"    /* invalid Y=0, must be skipped */
        "C;Y2;X1;K\"unit\"\n"
        "C;Y2;X2;K\"100\"\n"
        "E\n";
    slkTestData_t *rows = parse_slk_string(src);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "unit", "HP"), "100");
    T_NULL(find_slk_value(rows, "BAD", "HP"));
}

/* File without an ID;PWXL magic line is parsed without error. */
TEST(wc3_slk, prod_no_magic_line) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"spd\"\n"
        "C;Y2;X1;K\"foo\"\n"
        "C;Y2;X2;K\"270\"\n"
        "E\n";
    slkTestData_t *rows = parse_slk_string(src);
    T_NOT_NULL(rows);
    T_STREQ(find_slk_value(rows, "foo", "spd"), "270");
}

#endif /* BZ_TESTS */
