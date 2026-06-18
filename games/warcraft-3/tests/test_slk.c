/*
 * test_slk.c — Tests for SLK data reading and unit-stat lookup.
 *
 * Two families of tests:
 *
 *  1. FS_FindSheetCell — verifies the pure-C linked-list traversal of
 *     sheetRow_t / sheetField_t, which is the fundamental operation
 *     underlying all unit-stat reads.
 *
 *  2. In-memory SLK parsing — verifies that parse_slk_string() correctly
 *     converts the SLK spreadsheet text format into the same linked-list
 *     structures (column headers, row keys, field values).
 *
 *  3. Unit stat accessors — verifies UnitIntegerField / UnitRealField
 *     through the mock UnitsMetaData tables set up by the test harness,
 *     covering the Peasant (hpea) and Footman (hfoo) test units.
 */

#include "test_framework.h"
#include "test_harness.h"

/* Defined in g_metadata.c; swaps the sheet backing one metadata table. */
void G_SetConfigTable(sheetMetaData_t *metadatas, LPCSTR slk, sheetRow_t *table);

/* -----------------------------------------------------------------------
 * 1.  FS_FindSheetCell
 * --------------------------------------------------------------------- */

static void test_find_cell_existing_row_and_column(void) {
    sheetField_t f = {"spd", "270", NULL};
    sheetRow_t   r = {"hpea", &f, NULL};

    ASSERT_STR_EQ(FS_FindSheetCell(&r, "hpea", "spd"), "270");
}

static void test_find_cell_missing_row_returns_null(void) {
    sheetField_t f = {"spd", "270", NULL};
    sheetRow_t   r = {"hpea", &f, NULL};

    ASSERT_NULL(FS_FindSheetCell(&r, "hfoo", "spd"));
}

static void test_find_cell_missing_column_returns_null(void) {
    sheetField_t f = {"spd", "270", NULL};
    sheetRow_t   r = {"hpea", &f, NULL};

    ASSERT_NULL(FS_FindSheetCell(&r, "hpea", "hp"));
}

static void test_find_cell_case_insensitive_column(void) {
    /* Column names are matched case-insensitively per FS_FindSheetCell. */
    sheetField_t f = {"RealHP", "250", NULL};
    sheetRow_t   r = {"hpea", &f, NULL};

    ASSERT_STR_EQ(FS_FindSheetCell(&r, "hpea", "realHP"), "250");
    ASSERT_STR_EQ(FS_FindSheetCell(&r, "hpea", "REALHP"), "250");
}

static void test_find_cell_multiple_rows(void) {
    sheetField_t fa = {"spd", "270", NULL};
    sheetField_t fb = {"spd", "300", NULL};
    sheetRow_t   rb = {"hfoo", &fb, NULL};
    sheetRow_t   ra = {"hpea", &fa, &rb};

    ASSERT_STR_EQ(FS_FindSheetCell(&ra, "hpea", "spd"), "270");
    ASSERT_STR_EQ(FS_FindSheetCell(&ra, "hfoo", "spd"), "300");
}

static void test_find_cell_multiple_fields(void) {
    sheetField_t fb = {"realHP", "250", NULL};
    sheetField_t fa = {"spd",    "270", &fb};
    sheetRow_t   r  = {"hpea", &fa, NULL};

    ASSERT_STR_EQ(FS_FindSheetCell(&r, "hpea", "spd"),    "270");
    ASSERT_STR_EQ(FS_FindSheetCell(&r, "hpea", "realHP"), "250");
}

static void test_find_cell_null_sheet_returns_null(void) {
    ASSERT_NULL(FS_FindSheetCell(NULL, "hpea", "spd"));
}

/* -----------------------------------------------------------------------
 * 2.  In-memory SLK parsing (parse_slk_string)
 * --------------------------------------------------------------------- */

/* Minimal SLK snippet: two data rows, three columns. */
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

static void test_slk_parse_returns_non_null(void) {
    sheetRow_t *rows = parse_slk_string(slk_two_units);
    ASSERT_NOT_NULL(rows);
    free_slk_rows(rows);
}

static void test_slk_parse_row_names(void) {
    sheetRow_t *rows = parse_slk_string(slk_two_units);
    ASSERT_NOT_NULL(rows);
    ASSERT_STR_EQ(rows->name, "hpea");
    ASSERT_NOT_NULL(rows->next);
    ASSERT_STR_EQ(rows->next->name, "hfoo");
    free_slk_rows(rows);
}

static void test_slk_parse_field_values(void) {
    sheetRow_t *rows = parse_slk_string(slk_two_units);
    ASSERT_NOT_NULL(rows);

    ASSERT_STR_EQ(FS_FindSheetCell(rows, "hpea", "spd"),    "270");
    ASSERT_STR_EQ(FS_FindSheetCell(rows, "hpea", "realHP"), "250");
    ASSERT_STR_EQ(FS_FindSheetCell(rows, "hpea", "bldtm"),  "45");
    ASSERT_STR_EQ(FS_FindSheetCell(rows, "hfoo", "realHP"), "420");
    ASSERT_STR_EQ(FS_FindSheetCell(rows, "hfoo", "bldtm"),  "60");

    free_slk_rows(rows);
}

static void test_slk_parse_missing_cell_returns_null(void) {
    sheetRow_t *rows = parse_slk_string(slk_two_units);
    ASSERT_NOT_NULL(rows);
    ASSERT_NULL(FS_FindSheetCell(rows, "hkni", "spd"));  /* row absent */
    ASSERT_NULL(FS_FindSheetCell(rows, "hpea", "armor")); /* column absent */
    free_slk_rows(rows);
}

static void test_slk_parse_empty_string_returns_null(void) {
    /* No C/F lines means no data rows. */
    sheetRow_t *rows = parse_slk_string("ID;PWXL\nE\n");
    ASSERT_NULL(rows);
}

/* -----------------------------------------------------------------------
 * 3.  Unit stat accessors via mock metadata tables
 * --------------------------------------------------------------------- */

static void test_unit_speed_peasant(void) {
    ASSERT_FLOAT_EQ(UNIT_SPEED(UNIT_ID("hpea")), 270.0f);
}

static void test_unit_speed_footman(void) {
    ASSERT_FLOAT_EQ(UNIT_SPEED(UNIT_ID("hfoo")), 270.0f);
}

static void test_unit_hp_peasant(void) {
    ASSERT_FLOAT_EQ(UNIT_HP(UNIT_ID("hpea")), 250.0f);
}

static void test_unit_hp_footman(void) {
    ASSERT_FLOAT_EQ(UNIT_HP(UNIT_ID("hfoo")), 420.0f);
}

static void test_unit_build_time_peasant(void) {
    ASSERT_EQ_INT(UNIT_BUILD_TIME(UNIT_ID("hpea")), 45);
}

static void test_unit_build_time_footman(void) {
    ASSERT_EQ_INT(UNIT_BUILD_TIME(UNIT_ID("hfoo")), 60);
}

static void test_unit_collision_peasant(void) {
    ASSERT_EQ_INT(UNIT_COLLISION(UNIT_ID("hpea")), 16);
}

static void test_unit_unknown_id_returns_zero(void) {
    /* Unknown unit ID must not crash and must return 0 / 0.0. */
    ASSERT_FLOAT_EQ(UNIT_SPEED(UNIT_ID("xxxx")),      0.0f);
    ASSERT_FLOAT_EQ(UNIT_HP(UNIT_ID("xxxx")),         0.0f);
    ASSERT_EQ_INT  (UNIT_BUILD_TIME(UNIT_ID("xxxx")), 0);
}

/*
 * Max mana must come from the computed 'realM' column, not the base 'manaN'.
 * Heroes carry manaN == 0 (their pool is derived from Intelligence) but a
 * non-zero realM, mirroring how max HP uses 'realHP' rather than a base column.
 * Reading manaN left Maiev (Ewar: manaN 0 / realM 225) showing no mana.
 */
static void test_unit_mana_uses_realM_not_manaN(void) {
    static const char slk_mana[] =
        "ID;PWXL;N;EBB;Y3;X4\n"
        "C;Y1;X1;K\"unitBalanceID\"\n"
        "C;Y1;X2;K\"manaN\"\n"
        "C;Y1;X3;K\"realM\"\n"
        "C;Y1;X4;K\"mana0\"\n"
        "C;Y2;X1;K\"Ewar\"\n"   /* Maiev: hero, manaN 0, realM 225, mana0 100 */
        "C;Y2;X2;K\"0\"\n"
        "C;Y2;X3;K\"225\"\n"
        "C;Y2;X4;K\"100\"\n"
        "C;Y3;X1;K\"hsor\"\n"   /* Sorceress: caster, manaN == realM == 200 */
        "C;Y3;X2;K\"200\"\n"
        "C;Y3;X3;K\"200\"\n"
        "C;Y3;X4;K\"75\"\n"
        "E\n";
    sheetRow_t *rows = parse_slk_string(slk_mana);

    ASSERT_NOT_NULL(rows);
    G_SetConfigTable(UnitsMetaData, "UnitBalance", rows);

    /* The fix: a hero with manaN 0 still reports its real mana pool. */
    ASSERT_FLOAT_EQ(UNIT_MANA_MAXIMUM(UNIT_ID("Ewar")), 225.0f);
    ASSERT_FLOAT_EQ(UNIT_MANA_INITIAL(UNIT_ID("Ewar")), 100.0f);
    /* Regular casters (manaN == realM) are unaffected. */
    ASSERT_FLOAT_EQ(UNIT_MANA_MAXIMUM(UNIT_ID("hsor")), 200.0f);
    ASSERT_FLOAT_EQ(UNIT_MANA_INITIAL(UNIT_ID("hsor")), 75.0f);

    /* Restore the shared fixture tables for the rest of the suite. */
    setup_test_unit_data();
    free_slk_rows(rows);
}

/* -----------------------------------------------------------------------
 * Suite runner
 * --------------------------------------------------------------------- */

BEGIN_SUITE(slk)
    RUN_TEST(test_find_cell_existing_row_and_column);
    RUN_TEST(test_find_cell_missing_row_returns_null);
    RUN_TEST(test_find_cell_missing_column_returns_null);
    RUN_TEST(test_find_cell_case_insensitive_column);
    RUN_TEST(test_find_cell_multiple_rows);
    RUN_TEST(test_find_cell_multiple_fields);
    RUN_TEST(test_find_cell_null_sheet_returns_null);

    RUN_TEST(test_slk_parse_returns_non_null);
    RUN_TEST(test_slk_parse_row_names);
    RUN_TEST(test_slk_parse_field_values);
    RUN_TEST(test_slk_parse_missing_cell_returns_null);
    RUN_TEST(test_slk_parse_empty_string_returns_null);

    RUN_TEST(test_unit_speed_peasant);
    RUN_TEST(test_unit_speed_footman);
    RUN_TEST(test_unit_hp_peasant);
    RUN_TEST(test_unit_hp_footman);
    RUN_TEST(test_unit_build_time_peasant);
    RUN_TEST(test_unit_build_time_footman);
    RUN_TEST(test_unit_collision_peasant);
    RUN_TEST(test_unit_unknown_id_returns_zero);
    RUN_TEST(test_unit_mana_uses_realM_not_manaN);
END_SUITE()
