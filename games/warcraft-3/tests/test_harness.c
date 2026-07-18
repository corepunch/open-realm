/*
 * test_harness.c — Mock game-import interface, global game state, and
 * helper functions for the OpenWarcraft3 test suite.
 *
 * This file owns every global variable that g_main.c normally defines
 * (gi, globals, game, level, g_edicts) so the test binary does not link
 * against that module.  All gi function-pointers are set to lightweight
 * stubs that are safe to call without the archive backend, SDL2, or OpenGL.
 *
 * Stubs for symbols defined in game source files that we do not compile
 * into the test binary (g_spawn.c, hud/, s_attack.c, etc.) are also
 * provided here.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "test_harness.h"

void test_client_stubs_init(void);

/* ge is declared extern in server/server.h and defined in server/sv_main.c.
 * We re-declare it here so this file can set it in setup_game(). */
extern struct game_export *ge;

/* Forward-declare functions defined in game .c files but not in any header. */
void G_SetConfigTable(sheetMetaData_t *metadatas, LPCSTR slk, sheetRow_t *table);

/* =======================================================================
 * Global game state (normally owned by g_main.c)
 * ===================================================================== */

struct game_export globals;
struct game_import gi;
struct game_locals  game;
struct level_locals level;
struct edict_s     *g_edicts;

static edict_t      _test_edicts[MAX_ENTITIES];
static MAPINFO      _test_mapinfo;
static struct client_s _test_clients[MAX_CLIENTS];

/* =======================================================================
 * Mock gi function implementations
 * ===================================================================== */

static HANDLE mock_MemAlloc(long size) {
    void *p = malloc((size_t)size);
    if (p) memset(p, 0, (size_t)size);
    return p;
}

static void mock_MemFree(HANDLE p) {
    free(p);
}

/* Global MemAlloc / MemFree used by common modules (e.g. msg.c). */
HANDLE MemAlloc(long size) { return mock_MemAlloc(size); }
void   MemFree(HANDLE p)   { mock_MemFree(p); }

static void mock_LinkEntity(LPEDICT ent) {
    if (!ent) return;
    /* Keep a simple AABB around the entity's 2-D origin. */
    ent->bounds.min.x = ent->s.origin2.x - ent->collision;
    ent->bounds.min.y = ent->s.origin2.y - ent->collision;
    ent->bounds.max.x = ent->s.origin2.x + ent->collision;
    ent->bounds.max.y = ent->s.origin2.y + ent->collision;
}

static void mock_UnlinkEntity(LPEDICT ent) {
    (void)ent;
}

/*
 * Simple spatial query: iterate all live edicts and return those whose
 * bounding box overlaps 'area' and pass the predicate.
 */
static DWORD mock_BoxEdicts(LPCBOX2 area,
                             LPEDICT *list,
                             DWORD    maxcount,
                             BOOL   (*pred)(LPCEDICT))
{
    DWORD count = 0;
    for (int i = 0; i < globals.num_edicts && count < maxcount; i++) {
        LPEDICT ent = &g_edicts[i];
        if (!ent->inuse) continue;
        if (area &&
            (ent->bounds.max.x < area->min.x ||
             ent->bounds.min.x > area->max.x ||
             ent->bounds.max.y < area->min.y ||
             ent->bounds.min.y > area->max.y))
            continue;
        if (pred && !pred(ent)) continue;
        list[count++] = ent;
    }
    return count;
}


static DWORD mock_GetTime(void) {
    return level.time;
}

static int    mock_ModelIndex(LPCSTR name)              { (void)name; return 0; }
static int    mock_SoundIndex(LPCSTR name)              { (void)name; return 0; }
static int    mock_ImageIndex(LPCSTR name)              { (void)name; return 0; }
static int    mock_FontIndex(LPCSTR n, DWORD s)         { (void)n; (void)s; return 0; }
static HANDLE mock_ReadFile(LPCSTR f, LPDWORD s)        { (void)f; if (s) *s=0; return NULL; }
static void   mock_multicast(LPCVECTOR3 o, multicast_t t) { (void)o; (void)t; }
static void   mock_unicast(edict_t *e)                  { (void)e; }
static void   mock_Write(pfWriteType_t type, void const *value) { (void)type; (void)value; }
static void   mock_configstring(DWORD i, LPCSTR s)      { (void)i; (void)s; }
static void   mock_confignstring(DWORD i, LPCSTR s, DWORD l) { (void)i; (void)s; (void)l; }
static void   mock_error(LPCSTR fmt, ...)               { (void)fmt; }

void Get_Portrait_f(LPEDICT ent) {
    (void)ent;
}

BOOL G_ActorHasSkill(LPEDICT ent, LPCSTR id) {
    (void)ent;
    (void)id;
    return false;
}

void harvest_start(LPEDICT self, LPEDICT target) {
    (void)self;
    (void)target;
}

void harvest_gold_start(LPEDICT self, LPEDICT target) {
    (void)self;
    (void)target;
}

/* =======================================================================
 * Test unit-data tables (normally loaded from MPQ by InitUnitData)
 *
 * Two test units:
 *   hpea — Human Peasant: speed 270, HP 250, build 45 s, collision 16
 *   hfoo — Human Footman: speed 270, HP 420, build 60 s, collision 16
 * ===================================================================== */

static sheetField_t hpea_balance_fields[7];
static sheetField_t hfoo_balance_fields[7];
static sheetField_t Hpal_balance_fields[12];
static sheetRow_t   test_balance_rows[4];

static sheetField_t hpea_data_fields[2];
static sheetField_t hfoo_data_fields[2];
static sheetField_t hbar_data_fields[2];
static sheetField_t Hpal_data_fields[2];
static sheetRow_t   test_data_rows[4];

static sheetField_t hpea_ui_fields[1];
static sheetField_t hfoo_ui_fields[1];
static sheetField_t hbar_ui_fields[1];
static sheetField_t Hpal_ui_fields[1];
static sheetRow_t   test_ui_rows[4];

void setup_test_unit_data(void) {
    /* --- UnitBalance table (speed, HP, build time, gold/lumber cost) --- */
    hpea_balance_fields[0] = (sheetField_t){"spd",    "270", &hpea_balance_fields[1]};
    hpea_balance_fields[1] = (sheetField_t){"realHP", "250", &hpea_balance_fields[2]};
    hpea_balance_fields[2] = (sheetField_t){"bldtm",     "45",  &hpea_balance_fields[3]};
    hpea_balance_fields[3] = (sheetField_t){"goldcost",  "75",  &hpea_balance_fields[4]};
    hpea_balance_fields[4] = (sheetField_t){"lumbercost","0",   &hpea_balance_fields[5]};
    hpea_balance_fields[5] = (sheetField_t){"regenHP",   "0.25",&hpea_balance_fields[6]};
    hpea_balance_fields[6] = (sheetField_t){"regenType", "always", NULL};

    hfoo_balance_fields[0] = (sheetField_t){"spd",    "270", &hfoo_balance_fields[1]};
    hfoo_balance_fields[1] = (sheetField_t){"realHP", "420", &hfoo_balance_fields[2]};
    hfoo_balance_fields[2] = (sheetField_t){"bldtm",     "60",  &hfoo_balance_fields[3]};
    hfoo_balance_fields[3] = (sheetField_t){"goldcost",  "135", &hfoo_balance_fields[4]};
    hfoo_balance_fields[4] = (sheetField_t){"lumbercost","20",  &hfoo_balance_fields[5]};
    hfoo_balance_fields[5] = (sheetField_t){"regenHP",   "0.5", &hfoo_balance_fields[6]};
    hfoo_balance_fields[6] = (sheetField_t){"regenType", "always", NULL};

    static sheetField_t hbar_balance_fields[7];
    hbar_balance_fields[0] = (sheetField_t){"spd",    "0",    &hbar_balance_fields[1]};
    hbar_balance_fields[1] = (sheetField_t){"realHP", "1500", &hbar_balance_fields[2]};
    hbar_balance_fields[2] = (sheetField_t){"bldtm",  "120",  &hbar_balance_fields[3]};
    hbar_balance_fields[3] = (sheetField_t){"goldcost", "385", &hbar_balance_fields[4]};
    hbar_balance_fields[4] = (sheetField_t){"lumbercost", "185", &hbar_balance_fields[5]};
    hbar_balance_fields[5] = (sheetField_t){"regenHP",   "5",  &hbar_balance_fields[6]};
    hbar_balance_fields[6] = (sheetField_t){"regenType", "none", NULL};

    /* Hpal — Paladin hero with real WC3 base values: realHP 650, realM 255,
     * realdef 3.9, STR 22 / INT 17 / AGI 13. Used for hero attribute scaling. */
    Hpal_balance_fields[0] = (sheetField_t){"spd",     "270", &Hpal_balance_fields[1]};
    Hpal_balance_fields[1] = (sheetField_t){"realHP",  "650", &Hpal_balance_fields[2]};
    Hpal_balance_fields[2] = (sheetField_t){"realM",   "255", &Hpal_balance_fields[3]};
    Hpal_balance_fields[3] = (sheetField_t){"realdef", "3.9", &Hpal_balance_fields[4]};
    Hpal_balance_fields[4] = (sheetField_t){"STR",     "22",  &Hpal_balance_fields[5]};
    Hpal_balance_fields[5] = (sheetField_t){"INT",     "17",  &Hpal_balance_fields[6]};
    Hpal_balance_fields[6] = (sheetField_t){"AGI",     "13",  &Hpal_balance_fields[7]};
    Hpal_balance_fields[7] = (sheetField_t){"manaN",   "0",   &Hpal_balance_fields[8]};
    Hpal_balance_fields[8] = (sheetField_t){"STRplus", "2.7", &Hpal_balance_fields[9]};
    Hpal_balance_fields[9] = (sheetField_t){"INTplus", "1.8", &Hpal_balance_fields[10]};
    Hpal_balance_fields[10]= (sheetField_t){"AGIplus", "1.5", &Hpal_balance_fields[11]};
    Hpal_balance_fields[11]= (sheetField_t){"Primary", "STR", NULL};

    test_balance_rows[0] = (sheetRow_t){"hpea", hpea_balance_fields, &test_balance_rows[1]};
    test_balance_rows[1] = (sheetRow_t){"hfoo", hfoo_balance_fields, &test_balance_rows[2]};
    test_balance_rows[2] = (sheetRow_t){"hbar", hbar_balance_fields, &test_balance_rows[3]};
    test_balance_rows[3] = (sheetRow_t){"Hpal", Hpal_balance_fields, NULL};

    /* --- UnitData table (collision radius, move type) --- */
    hpea_data_fields[0] = (sheetField_t){"collision", "16",   &hpea_data_fields[1]};
    hpea_data_fields[1] = (sheetField_t){"movetp",    "foot", NULL};

    hfoo_data_fields[0] = (sheetField_t){"collision", "16",   &hfoo_data_fields[1]};
    hfoo_data_fields[1] = (sheetField_t){"movetp",    "foot", NULL};

    hbar_data_fields[0] = (sheetField_t){"collision", "64",   &hbar_data_fields[1]};
    hbar_data_fields[1] = (sheetField_t){"movetp",    "",     NULL};

    Hpal_data_fields[0] = (sheetField_t){"collision", "16",   &Hpal_data_fields[1]};
    Hpal_data_fields[1] = (sheetField_t){"movetp",    "foot", NULL};

    test_data_rows[0] = (sheetRow_t){"hpea", hpea_data_fields, &test_data_rows[1]};
    test_data_rows[1] = (sheetRow_t){"hfoo", hfoo_data_fields, &test_data_rows[2]};
    test_data_rows[2] = (sheetRow_t){"hbar", hbar_data_fields, &test_data_rows[3]};
    test_data_rows[3] = (sheetRow_t){"Hpal", Hpal_data_fields, NULL};

    hpea_ui_fields[0] = (sheetField_t){"isbldg", "0", NULL};
    hfoo_ui_fields[0] = (sheetField_t){"isbldg", "0", NULL};
    hbar_ui_fields[0] = (sheetField_t){"isbldg", "1", NULL};
    Hpal_ui_fields[0] = (sheetField_t){"isbldg", "0", NULL};

    test_ui_rows[0] = (sheetRow_t){"hpea", hpea_ui_fields, &test_ui_rows[1]};
    test_ui_rows[1] = (sheetRow_t){"hfoo", hfoo_ui_fields, &test_ui_rows[2]};
    test_ui_rows[2] = (sheetRow_t){"hbar", hbar_ui_fields, &test_ui_rows[3]};
    test_ui_rows[3] = (sheetRow_t){"Hpal", Hpal_ui_fields, NULL};

    G_SetConfigTable(UnitsMetaData, "UnitBalance", test_balance_rows);
    G_SetConfigTable(UnitsMetaData, "UnitData",    test_data_rows);
    G_SetConfigTable(UnitsMetaData, "UnitUI",      test_ui_rows);
}

/* =======================================================================
 * Harness lifecycle
 * ===================================================================== */

void setup_game(void) {
    memset(&globals,      0, sizeof(globals));
    memset(&gi,           0, sizeof(gi));
    memset(&game,         0, sizeof(game));
    memset(&level,        0, sizeof(level));
    memset(_test_edicts,  0, sizeof(_test_edicts));
    memset(&_test_mapinfo,0, sizeof(_test_mapinfo));
    memset(_test_clients, 0, sizeof(_test_clients));

    g_edicts = _test_edicts;

    /* Wire up gi mock functions. */
    gi.MemAlloc            = mock_MemAlloc;
    gi.MemFree             = mock_MemFree;
    gi.ModelIndex          = mock_ModelIndex;
    gi.SoundIndex          = mock_SoundIndex;
    gi.ImageIndex          = mock_ImageIndex;
    gi.FontIndex           = mock_FontIndex;
    gi.LinkEntity          = mock_LinkEntity;
    gi.UnlinkEntity        = mock_UnlinkEntity;
    gi.BoxEdicts           = mock_BoxEdicts;
    gi.ReadFile            = mock_ReadFile;
    gi.GetTime             = mock_GetTime;
    gi.multicast           = mock_multicast;
    gi.unicast             = mock_unicast;
    gi.Write              = mock_Write;
    gi.configstring        = mock_configstring;
    gi.confignstring       = mock_confignstring;
    gi.error               = mock_error;
    gi.CvarString          = NULL; /* tests don't use in-game test runner */
    test_client_stubs_init();

    /* Initialise game-export fields and the ge pointer (declared extern in
     * server/server.h, defined in server/sv_main.c) so routing.c can access
     * ge->num_edicts in non-GAME_WORLD builds (tool_common test builds). */
    ge                  = &globals;
    globals.edicts      = g_edicts;
    globals.num_edicts  = 0;
    globals.max_edicts  = MAX_ENTITIES;
    globals.max_clients = MAX_CLIENTS;
    game.max_clients    = MAX_CLIENTS;
    game.clients        = _test_clients;
    /* Assign each player a unique number matching its slot index. */
    FOR_LOOP(i, MAX_CLIENTS) {
        _test_clients[i].ps.number = (DWORD)i;
    }

    /* Provide a minimal mapinfo so level.mapinfo is never NULL. */
    level.mapinfo = &_test_mapinfo;

    /* Set up unit stats tables with test data. */
    setup_test_unit_data();

    /* Register ability list (s_skills.c). */
    InitAbilities();
}

void teardown_game(void) {
    /* Nothing to free — all test edicts are in _test_edicts[].
     * Clear the pool so subsequent setup_game() / reset_entities()
     * calls start from a fully zeroed state. */
    memset(_test_edicts, 0, sizeof(_test_edicts));
    globals.num_edicts = 0;
}

/*
 * Reset the entity pool between individual tests that run within the
 * same test suite (i.e. share a single setup_game() / teardown_game()
 * call).  Only the entity slot array and the count are reset; all other
 * game globals (gi, game, level) remain unchanged.
 */
void reset_entities(void) {
    memset(_test_edicts, 0, sizeof(_test_edicts));
    globals.num_edicts = 0;
    globals.edicts     = g_edicts;
}

/* =======================================================================
 * Edict helper
 * ===================================================================== */

LPEDICT alloc_test_unit(DWORD class_id, FLOAT x, FLOAT y) {
    int idx = globals.num_edicts++;
    LPEDICT ent = &g_edicts[idx];
    memset(ent, 0, sizeof(*ent));
    ent->inuse      = true;
    ent->s.number   = idx;
    ent->class_id   = class_id;
    ent->s.origin2  = (VECTOR2){x, y};
    ent->s.origin.x = x;
    ent->s.origin.y = y;
    ent->s.origin.z = 0;
    mock_LinkEntity(ent);
    return ent;
}

/* =======================================================================
 * FS_FindSheetCell — pure-C SLK cell lookup (no archive backend)
 * ===================================================================== */

LPCSTR FS_FindSheetCell(sheetRow_t *sheet, LPCSTR row, LPCSTR column) {
    if (!sheet || !row || !column) return NULL;
    for (sheetRow_t const *srow = sheet; srow; srow = srow->next) {
        if (!srow->name || strcmp(srow->name, row) != 0) continue;
        for (sheetField_t const *scol = srow->fields; scol; scol = scol->next) {
            if (!scol->name) continue;
            if (strcasecmp(scol->name, column) == 0) return scol->value;
        }
    }
    return NULL;
}

/* =======================================================================
 * parse_slk_string — in-memory SLK parser
 *
 * Parses the SLK spreadsheet format from a NUL-terminated string and
 * returns a heap-allocated chain of sheetRow_t / sheetField_t nodes.
 * Row 1 of the spreadsheet is treated as column headers; column 1 is
 * the row key.  All returned strings are heap-allocated copies.
 * ===================================================================== */

#define MAX_SLK_COLS 64
#define MAX_SLK_LINE 512

sheetRow_t *parse_slk_string(const char *slk_text) {
    if (!slk_text) return NULL;

    /* Two-pass: first pass collects raw cells, second builds rows.
     * head/tail are declared before any goto so they are always
     * initialised when cleanup: is reached. */
    typedef struct raw_cell { int x, y; char *text; struct raw_cell *next; } raw_cell_t;
    raw_cell_t *cells = NULL, *cells_tail = NULL;
    int max_row = 0, max_col = 0;
    sheetRow_t *head = NULL, *tail = NULL;

    /* --- Pass 1: parse all C/F lines into raw cells --- */
    const char *p = slk_text;
    while (*p) {
        /* Skip to end of line, collecting characters. */
        char line[MAX_SLK_LINE];
        int  len = 0;
        while (*p && *p != '\n' && len < MAX_SLK_LINE - 1)
            line[len++] = *p++;
        if (*p == '\n') p++;
        line[len] = '\0';

        if (line[0] != 'C' && line[0] != 'F') continue;

        /* Tokenise by ';'.  Zero-init so the token walker's final
         * tok += strlen(tok) + 1 lands on a NUL byte instead of
         * reading uninitialized stack memory past the last token. */
        char tokens[MAX_SLK_LINE] = {0};
        memcpy(tokens, line, len + 1);
        for (int i = 0; tokens[i]; i++)
            if (tokens[i] == ';') tokens[i] = '\0';

        int cx = 0, cy = 0;
        char *kval = NULL;
        const char *tok = tokens;
        tok += strlen(tok) + 1; /* skip 'C' or 'F' */
        while (*tok) {
            if (*tok == 'X') cx = atoi(tok + 1);
            else if (*tok == 'Y') cy = atoi(tok + 1);
            else if (*tok == 'K') kval = (char *)(tok + 1);
            tok += strlen(tok) + 1;
        }
        if (!kval) continue;

        /* Strip surrounding quotes. */
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

    /* --- Build column-name lookup from row 1 --- */
    char *col_names[MAX_SLK_COLS] = {NULL};
    for (raw_cell_t *c = cells; c; c = c->next) {
        if (c->y == 1 && c->x < MAX_SLK_COLS)
            col_names[c->x] = c->text;
    }

    /* --- Pass 2: build sheetRow_t linked list --- */
    for (int row = 2; row <= max_row; row++) {
        /* Find the row key (column 1). */
        char *row_key = NULL;
        for (raw_cell_t *c = cells; c; c = c->next) {
            if (c->y == row && c->x == 1) { row_key = c->text; break; }
        }
        if (!row_key) continue;

        sheetRow_t *sr = malloc(sizeof(*sr));
        sr->name   = strdup(row_key);
        sr->fields = NULL;
        sr->next   = NULL;

        /* Attach all non-key columns as fields. */
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
    /* Free raw cells. */
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
        /* Row name was strdup'd. */
        free((void *)rows->name);
        /* Free all fields. */
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

/* =======================================================================
 * Stubs for game symbols that are not compiled into the test binary
 * ===================================================================== */

/* g_main.c — G_PublishEvent stores events in the level queue.
 * Matches the production implementation (circular ring buffer). */
GAMEEVENT *G_PublishEvent(LPEDICT edict, EVENTTYPE type) {
    GAMEEVENT *evt = &level.events.queue[level.events.write++ % MAX_EVENT_QUEUE];
    evt->type  = type;
    evt->edict = edict;
    return evt;
}

/* g_main.c — Player lookup helpers used by jdo.c and g_utils.c. */
LPPLAYER G_GetPlayerByNumber(DWORD number) {
    FOR_LOOP(i, game.max_clients) {
        if (game.clients[i].ps.number == number) {
            return &game.clients[i].ps;
        }
    }
    return &game.clients[MAX_PLAYERS - 1].ps;
}

LPEDICT G_GetPlayerEntityByNumber(DWORD number) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = &g_edicts[i];
        if (ent->client && ent->client->ps.number == number) {
            return ent;
        }
    }
    return NULL;
}

LPGAMECLIENT G_GetPlayerClientByNumber(DWORD number) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT cl = &game.clients[i];
        if (cl->ps.number == number) {
            return cl;
        }
    }
    return &game.clients[MAX_PLAYERS - 1];
}

LPCSTR G_LevelString(LPCSTR name) { return name; }

/* g_spawn.c — target-type string parser. */
TARGTYPE G_GetTargetType(LPCSTR str) { (void)str; return TARG_NONE; }

/* g_spawn.c — G_Spawn allocates the next free edict slot.
 * Used by fire_rocket() in s_attack.c to spawn projectile entities. */
LPEDICT G_Spawn(void) {
    if (globals.num_edicts >= globals.max_edicts) {
        assert(0 && "G_Spawn: _test_edicts[] exhausted");
        return NULL;
    }
    int idx = globals.num_edicts++;
    LPEDICT ent = &g_edicts[idx];
    memset(ent, 0, sizeof(*ent));
    ent->inuse    = true;
    ent->s.number = idx;
    return ent;
}

/* g_spawn.c */
LPEDICT SP_SpawnAtLocation(DWORD class_id, DWORD player, LPCVECTOR2 location) {
    (void)class_id; (void)player; (void)location;
    return NULL;
}
BOOL SP_FindEmptySpaceAround(LPEDICT ent, DWORD class_id, LPVECTOR2 out, FLOAT *angle) {
    (void)ent; (void)class_id;
    if (out)   { out->x = 0.0f; out->y = 0.0f; }
    if (angle) { *angle = 0.0f; }
    return true;
}

/* g_commands.c */
void CMD_CancelCommand(LPEDICT ent) { (void)ent; }

/* p_hud.c — used by SP_TrainUnit after building. */
void Get_Commands_f(LPEDICT ent) { (void)ent; }

/* hud/ */
void UI_AddCancelButton(LPEDICT ent) { (void)ent; }

/* g_main.c — selection helper used by FOR_SELECTED_UNITS macro */
BOOL G_IsEntitySelected(LPGAMECLIENT client, LPEDICT ent) {
    return client && ent && (ent->selected & (1 << client->ps.number));
}

LPEDICT G_GetMainSelectedUnit(LPGAMECLIENT client) {
    FOR_SELECTED_UNITS(client, ent) {
        return ent;
    }
    return NULL;
}

/* Stubs for abilities defined in skill files not included in the test binary.
 * s_skills.c references all of these via its abilitylist[] table.
 * (a_attack is the real symbol from s_attack.c, a_stop/a_move from their
 * respective .c files, a_patrol from s_patrol.c — only the remaining
 * abilities need stubs here.) */
ability_t a_build       = {0};
ability_t a_holdpos     = {0};
ability_t a_cancel      = {0};
ability_t a_selectskill = {0};
ability_t a_harvest     = {0};
ability_t a_militia     = {0};
ability_t a_repair      = {0};
ability_t a_goldmine    = {0};
ability_t a_devotionaura = {0};
ability_t a_holylight   = {0};
