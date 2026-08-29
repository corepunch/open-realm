/*
 * t_slk.c — In-engine SLK data reading and unit-stat tests.
 *
 * Part 1 (pure functions): FS_FindSheetCell linked-list traversal tests.
 * Part 2 (unit stats): real archive data for hpea/hfoo.
 * Part 3 (typed table replacement): G_SetSLKRows tests mana/armor edge cases.
 */
#ifdef BZ_TESTS

#include "test.h"
#include "../g_local.h"
#include "common/stb_slk.h"

void setup_test_world(void);

/* parse_slk_string / free_slk_rows: defined in test_harness.c, reproduced
 * here so t_slk.c is self-contained without the old harness. */
#define MAX_SLK_COLS 64
#define MAX_SLK_LINE 512

sheetRow_t *parse_slk_string(const char *slk_text) {
    if (!slk_text) return NULL;
    typedef struct raw_cell { int x, y; char *text; struct raw_cell *next; } raw_cell_t;
    raw_cell_t *cells = NULL, *cells_tail = NULL;
    int max_row = 0, max_col = 0;
    sheetRow_t *head = NULL, *tail = NULL;

    const char *p = slk_text;
    while (*p) {
        char line[MAX_SLK_LINE];
        int  len = 0;
        while (*p && *p != '\n' && len < MAX_SLK_LINE - 1)
            line[len++] = *p++;
        if (*p == '\n') p++;
        line[len] = '\0';

        if (line[0] != 'C' && line[0] != 'F') continue;

        char tokens[MAX_SLK_LINE] = {0};
        memcpy(tokens, line, len + 1);
        for (int i = 0; tokens[i]; i++)
            if (tokens[i] == ';') tokens[i] = '\0';

        int cx = 0, cy = 0;
        char *kval = NULL;
        const char *tok = tokens;
        tok += strlen(tok) + 1;
        while (*tok) {
            if (*tok == 'X') cx = atoi(tok + 1);
            else if (*tok == 'Y') cy = atoi(tok + 1);
            else if (*tok == 'K') kval = (char *)(tok + 1);
            tok += strlen(tok) + 1;
        }
        if (!kval) continue;

        char text[MAX_SLK_LINE] = {0};
        int ti = 0;
        for (const char *c = kval; *c; c++) {
            if (*c == '"' || *c == '\r' || *c == '\n') continue;
            text[ti++] = *c;
        }
        text[ti] = '\0';

        raw_cell_t *cell = malloc(sizeof(*cell));
        cell->x    = cx;
        cell->y    = cy;
        cell->text = strdup(text);
        cell->next = NULL;
        if (!cells) cells = cell; else cells_tail->next = cell;
        cells_tail = cell;

        if (cy > max_row) max_row = cy;
        if (cx > max_col) max_col = cx;
    }
    if (!cells || max_row < 2) goto cleanup;

    char *col_names[MAX_SLK_COLS] = {NULL};
    for (raw_cell_t *c = cells; c; c = c->next) {
        if (c->y == 1 && c->x < MAX_SLK_COLS)
            col_names[c->x] = c->text;
    }

    for (int row = 2; row <= max_row; row++) {
        char *row_key = NULL;
        for (raw_cell_t *c = cells; c; c = c->next) {
            if (c->y == row && c->x == 1) { row_key = c->text; break; }
        }
        if (!row_key) continue;

        sheetRow_t *sr = malloc(sizeof(*sr));
        sr->name   = strdup(row_key);
        sr->fields = NULL;
        sr->next   = NULL;

        for (raw_cell_t *c = cells; c; c = c->next) {
            if (c->y != row || c->x <= 1 || c->x >= MAX_SLK_COLS) continue;
            if (!col_names[c->x]) continue;
            sheetField_t *sf = malloc(sizeof(*sf));
            sf->name  = strdup(col_names[c->x]);
            sf->value = strdup(c->text);
            sf->next  = sr->fields;
            sr->fields = sf;
        }

        if (!head) head = sr; else tail->next = sr;
        tail = sr;
    }

cleanup:
    for (raw_cell_t *c = cells; c; ) {
        raw_cell_t *nx = c->next;
        free(c->text);
        free(c);
        c = nx;
    }
    return head;
}

void free_slk_rows(sheetRow_t *rows) {
    while (rows) {
        sheetRow_t *next_row = rows->next;
        free((void *)rows->name);
        for (sheetField_t *f = rows->fields; f; ) {
            sheetField_t *nf = f->next;
            free((void *)f->name);
            free((void *)f->value);
            free(f);
            f = nf;
        }
        free(rows);
        rows = next_row;
    }
}

/* -----------------------------------------------------------------------
 * 1.  FS_FindSheetCell
 * --------------------------------------------------------------------- */

TEST(wc3_slk, find_cell_existing_row_and_column) {
    sheetField_t f = {"spd", "270", NULL};
    sheetRow_t   r = {"hpea", &f, NULL};
    T_STREQ(FS_FindSheetCell(&r, "hpea", "spd"), "270");
}

TEST(wc3_slk, find_cell_missing_row_returns_null) {
    sheetField_t f = {"spd", "270", NULL};
    sheetRow_t   r = {"hpea", &f, NULL};
    T_NULL(FS_FindSheetCell(&r, "hfoo", "spd"));
}

TEST(wc3_slk, find_cell_missing_column_returns_null) {
    sheetField_t f = {"spd", "270", NULL};
    sheetRow_t   r = {"hpea", &f, NULL};
    T_NULL(FS_FindSheetCell(&r, "hpea", "hp"));
}

TEST(wc3_slk, find_cell_case_insensitive_column) {
    sheetField_t f = {"RealHP", "250", NULL};
    sheetRow_t   r = {"hpea", &f, NULL};
    T_STREQ(FS_FindSheetCell(&r, "hpea", "realHP"), "250");
    T_STREQ(FS_FindSheetCell(&r, "hpea", "REALHP"), "250");
}

TEST(wc3_slk, find_cell_multiple_rows) {
    sheetField_t fa = {"spd", "270", NULL};
    sheetField_t fb = {"spd", "300", NULL};
    sheetRow_t   rb = {"hfoo", &fb, NULL};
    sheetRow_t   ra = {"hpea", &fa, &rb};
    T_STREQ(FS_FindSheetCell(&ra, "hpea", "spd"), "270");
    T_STREQ(FS_FindSheetCell(&ra, "hfoo", "spd"), "300");
}

TEST(wc3_slk, find_cell_multiple_fields) {
    sheetField_t fb = {"realHP", "250", NULL};
    sheetField_t fa = {"spd",    "270", &fb};
    sheetRow_t   r  = {"hpea", &fa, NULL};
    T_STREQ(FS_FindSheetCell(&r, "hpea", "spd"),    "270");
    T_STREQ(FS_FindSheetCell(&r, "hpea", "realHP"), "250");
}

TEST(wc3_slk, find_cell_null_sheet_returns_null) {
    T_NULL(FS_FindSheetCell(NULL, "hpea", "spd"));
}

TEST(wc3_slk, typed_strings_are_owned_and_alias_safe) {
    typedef struct { LPCSTR name; } testRow_t;
    static slkField_t const schema[] = {
        { "Name", offsetof(testRow_t, name), STB_SLK_STR },
        { "name", offsetof(testRow_t, name), STB_SLK_STR },
        { NULL, 0, 0 }
    };
    char first[] = "Footman", second[] = "Knight";
    sheetField_t alias = { "name", second, NULL };
    sheetField_t field = { "Name", first, &alias };
    sheetRow_t source = { "hfoo", &field, NULL };
    testRow_t *rows = calloc(1, sizeof(*rows));

    FS_SLKDecodeRow(&source, schema, rows);
    first[0] = 'X'; second[0] = 'Y';
    T_STREQ(rows->name, "Knight");
    FS_SLKFreeRows(schema, rows, 1, sizeof(*rows));
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
    sheetRow_t *rows = parse_slk_string(slk_two_units);
    T_NOT_NULL(rows);
    free_slk_rows(rows);
}

TEST(wc3_slk, parse_row_names) {
    sheetRow_t *rows = parse_slk_string(slk_two_units);
    T_NOT_NULL(rows);
    T_STREQ(rows->name, "hpea");
    T_NOT_NULL(rows->next);
    T_STREQ(rows->next->name, "hfoo");
    free_slk_rows(rows);
}

TEST(wc3_slk, parse_field_values) {
    sheetRow_t *rows = parse_slk_string(slk_two_units);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "hpea", "spd"),    "270");
    T_STREQ(FS_FindSheetCell(rows, "hpea", "realHP"), "250");
    T_STREQ(FS_FindSheetCell(rows, "hpea", "bldtm"),  "45");
    T_STREQ(FS_FindSheetCell(rows, "hfoo", "realHP"), "420");
    T_STREQ(FS_FindSheetCell(rows, "hfoo", "bldtm"),  "60");
    free_slk_rows(rows);
}

TEST(wc3_slk, parse_missing_cell_returns_null) {
    sheetRow_t *rows = parse_slk_string(slk_two_units);
    T_NOT_NULL(rows);
    T_NULL(FS_FindSheetCell(rows, "hkni", "spd"));
    T_NULL(FS_FindSheetCell(rows, "hpea", "armor"));
    free_slk_rows(rows);
}

TEST(wc3_slk, parse_empty_string_returns_null) {
    sheetRow_t *rows = parse_slk_string("ID;PWXL\nE\n");
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
    T_ASSERT(ent.balance == G_UnitBalance(ent.class_id));
    T_ASSERT(ent.data == G_UnitData(ent.class_id));
    T_ASSERT(ent.ui == G_UnitUI(ent.class_id));
    T_ASSERT(ent.weapons == G_UnitWeapons(ent.class_id));
    T_ASSERT(ent.abilities == G_UnitAbil(ent.class_id));
}

TEST(wc3_slk, weapon_columns_decode_into_attack_records) {
    LPCSTR slk =
        "ID;PWXL;N;E\n"
        "C;Y1;X1;K\"unitWeaponID\"\nC;Y1;X2;K\"dmgplus1\"\nC;Y1;X3;K\"dmgplus2\"\n"
        "C;Y1;X4;K\"rangeN1\"\nC;Y1;X5;K\"rangeN2\"\n"
        "C;Y2;X1;K\"hfoo\"\nC;Y2;X2;K12\nC;Y2;X3;K34\nC;Y2;X4;K90\nC;Y2;X5;K600\nE\n";
    sheetRow_t *rows = parse_slk_string(slk);
    sheetRow_t *saved = G_SetSLKRows("UnitWeapons", rows);
    UnitWeapons_t const *weapons = G_UnitWeapons(MAKEFOURCC('h','f','o','o'));
    T_ASSERT(weapons == g_UnitWeapons);
    T_EQ(weapons->attack1.damageBase, 12); T_EQ(weapons->attack2.damageBase, 34);
    T_FEQ(weapons->attack1.range, 90.0f, 0.01f); T_FEQ(weapons->attack2.range, 600.0f, 0.01f);
    G_SetSLKRows("UnitWeapons", saved); free_slk_rows(rows);
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
    sheetRow_t *rows = parse_slk_string(slk_mana);
    T_NOT_NULL(rows);
    sheetRow_t *saved_mana = G_SetSLKRows("UnitBalance", rows);

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
    sheetRow_t *rows = parse_slk_string(slk_armor);
    T_NOT_NULL(rows);
    sheetRow_t *saved_arm = G_SetSLKRows("UnitBalance", rows);

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
    sheetRow_t *saved = NULL;
    int (*old_index)(LPCSTR) = gi.ImageIndex;
    setup_test_world();
    gi.ImageIndex = capture_spawn_image;
    FOR_LOOP(i, sizeof(names) / sizeof(names[0])) {
        char slk[1024];
        snprintf(slk, sizeof(slk), "ID;PWXL;N;E\nC;Y1;X1;K\"ID\"\nC;Y1;X2;K\"file\"\nC;Y1;X3;K\"texFile\"\nC;Y1;X4;K\"targType\"\nC;Y2;X1;K\"LT05\"\nC;Y2;X2;K\"Cliff\"\nC;Y2;X3;K\"%s\"\nC;Y2;X4;K\"debris\"\nE\n", names[i]);
        sheetRow_t *rows = parse_slk_string(slk);
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
    sheetRow_t *rows = parse_slk_string(slk);
    sheetRow_t *saved = G_SetSLKRows("Doodads", rows);
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
    sheetRow_t *rows = parse_slk_string(slk);
    sheetRow_t *saved = G_SetSLKRows("UberSplatData", rows);
    UberSplatData_t const *row = G_UberSplat(MAKEFOURCC('H','M','t','p'));
    T_STREQ(row->Dir, "Splats"); T_STREQ(row->file, "TownHall"); T_FEQ(row->Scale, 4.0f, 0.01f);
    G_SetSLKRows("UberSplatData", saved); free_slk_rows(rows);
}

/* -----------------------------------------------------------------------
 * 5.  Production SLK buffer parser (FS_ParseSLK_Buffer)
 *     These tests exercise the real parser path, not parse_slk_string.
 * --------------------------------------------------------------------- */

/* Omitted Y uses stateful current row; ROC tables rely on this heavily. */
TEST(wc3_slk, prod_stateful_y) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"HP\"\n"
        "C;Y2;X1;K\"hero\"\n"
        "C;X2;K\"500\"\n"   /* Y omitted: Y=2 carries from previous C */
        "E\n";
    sheetRow_t *rows = FS_ParseSLK_Buffer(src);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "hero", "HP"), "500");
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
    sheetRow_t *rows = FS_ParseSLK_Buffer(src);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "mage", "HP"), "500");
    T_STREQ(FS_FindSheetCell(rows, "mage", "MP"), "200");
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
    sheetRow_t *rows = FS_ParseSLK_Buffer(src);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "mage", "HP"), "400");
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
    sheetRow_t *rows = FS_ParseSLK_Buffer(src);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "unit", "HP"), "300");
}

/* Semicolons inside a quoted K value must not split the field. */
TEST(wc3_slk, prod_quoted_semicolon) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"path\"\n"
        "C;Y2;X1;K\"itm1\"\n"
        "C;Y2;X2;K\"foo;bar\"\n"  /* semicolon inside quotes */
        "E\n";
    sheetRow_t *rows = FS_ParseSLK_Buffer(src);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "itm1", "path"), "foo;bar");
}

/* "" inside a quoted K value decodes to a single literal double-quote. */
TEST(wc3_slk, prod_quote_escape) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"note\"\n"
        "C;Y2;X1;K\"obj1\"\n"
        "C;Y2;X2;K\"foo\"\"bar\"\n"  /* SLK: K"foo""bar" → foo"bar */
        "E\n";
    sheetRow_t *rows = FS_ParseSLK_Buffer(src);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "obj1", "note"), "foo\"bar");
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
    sheetRow_t *rows = FS_ParseSLK_Buffer(src);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "unit", "HP"), "100");
    T_NULL(rows->next); /* only one data row; the Y=0 cell must not add a row */
}

/* File without an ID;PWXL magic line is parsed without error. */
TEST(wc3_slk, prod_no_magic_line) {
    static const char src[] =
        "C;Y1;X1;K\"id\"\n"
        "C;Y1;X2;K\"spd\"\n"
        "C;Y2;X1;K\"foo\"\n"
        "C;Y2;X2;K\"270\"\n"
        "E\n";
    sheetRow_t *rows = FS_ParseSLK_Buffer(src);
    T_NOT_NULL(rows);
    T_STREQ(FS_FindSheetCell(rows, "foo", "spd"), "270");
}

#endif /* BZ_TESTS */
