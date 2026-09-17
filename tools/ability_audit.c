/*
 * ability_audit — Audit WC3 AbilityData.slk against implemented ability handlers.
 *
 * Reads AbilityData.slk and *AbilityStrings.txt from the MPQ archives,
 * then prints every ability NOT registered in s_skills.c's abilitylist[].
 *
 * Usage:
 *   ability_audit -data 'data/Warcraft III' -raw Aams   # ROC then TFT agent brief
 *   ability_audit -data 'data/Warcraft III' -roc -raw Aams
 *   ability_audit -data 'data/Warcraft III' -tft -raw Aams
 *   ability_audit -data 'data/Warcraft III' -tft        # missing-ability list, TFT overlay
 */
#include "tool_common.h"
#include "common/stb_slk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ---- AbilityData_t (mirrors g_unitrow.h — avoid circular game deps) ---- */
typedef struct {
    DWORD id, code, uberAlias;
    LPCSTR comments, sort, race;
    LONG version, levels, reqLevel, levelSkip, priority;
    BOOL useInEditor, hero, item, checkDep, InBeta;
    LPCSTR targs[4];
    FLOAT cast[4], dur[4], heroDur[4], cool[4], cost[4], area[4], range[4];
    FLOAT data[4][9];
    DWORD dataId[4][9];
    DWORD unitID[4];
    LPCSTR buffID[4], efctID[4];
    LPCSTR castCheck, durCheck, heroDurCheck, coolCheck, costCheck, areaCheck, rangeCheck;
} AbilityData_t;

/* ---- DDX schema (mirrors g_metadata.c ability_schema) ---- */
#define AB_F(NAME, FIELD, LEVEL, TYPE) { NAME, offsetof(AbilityData_t, FIELD[LEVEL]), TYPE }
#define AB_D(NAME, LEVEL, SLOT) { NAME, offsetof(AbilityData_t, data[LEVEL][SLOT]), STB_SLK_FLOAT }
#define AB_ID(NAME, LEVEL, SLOT) { NAME, offsetof(AbilityData_t, dataId[LEVEL][SLOT]), STB_SLK_FOURCC }
static slkField_t const ability_schema[] = {
    { "",            offsetof(AbilityData_t, id),          STB_SLK_FOURCC },
    { "code",        offsetof(AbilityData_t, code),        STB_SLK_FOURCC },
    { "uberAlias",   offsetof(AbilityData_t, uberAlias),   STB_SLK_FOURCC },
    { "comments",    offsetof(AbilityData_t, comments),    STB_SLK_STR    },
    { "version",     offsetof(AbilityData_t, version),     STB_SLK_INT    },
    { "useInEditor", offsetof(AbilityData_t, useInEditor), STB_SLK_BOOL   },
    { "hero",        offsetof(AbilityData_t, hero),        STB_SLK_BOOL   },
    { "item",        offsetof(AbilityData_t, item),        STB_SLK_BOOL   },
    { "sort",        offsetof(AbilityData_t, sort),        STB_SLK_STR    },
    { "race",        offsetof(AbilityData_t, race),        STB_SLK_STR    },
    { "checkDep",    offsetof(AbilityData_t, checkDep),    STB_SLK_BOOL   },
    { "levels",      offsetof(AbilityData_t, levels),      STB_SLK_INT    },
    { "reqLevel",    offsetof(AbilityData_t, reqLevel),    STB_SLK_INT    },
    { "levelSkip",   offsetof(AbilityData_t, levelSkip),   STB_SLK_INT    },
    { "priority",    offsetof(AbilityData_t, priority),    STB_SLK_INT    },
    { "targs",       offsetof(AbilityData_t, targs[0]),    STB_SLK_STR    },
    AB_F("targs1", targs, 0, STB_SLK_STR), AB_F("targs2", targs, 1, STB_SLK_STR),
    AB_F("targs3", targs, 2, STB_SLK_STR), AB_F("targs4", targs, 3, STB_SLK_STR),
    AB_F("Cast1", cast, 0, STB_SLK_FLOAT), AB_F("Cast2", cast, 1, STB_SLK_FLOAT),
    AB_F("Cast3", cast, 2, STB_SLK_FLOAT), AB_F("Cast4", cast, 3, STB_SLK_FLOAT),
    AB_F("Dur1", dur, 0, STB_SLK_FLOAT), AB_F("Dur2", dur, 1, STB_SLK_FLOAT),
    AB_F("Dur3", dur, 2, STB_SLK_FLOAT), AB_F("Dur4", dur, 3, STB_SLK_FLOAT),
    AB_F("HeroDur1", heroDur, 0, STB_SLK_FLOAT), AB_F("HeroDur2", heroDur, 1, STB_SLK_FLOAT),
    AB_F("HeroDur3", heroDur, 2, STB_SLK_FLOAT), AB_F("HeroDur4", heroDur, 3, STB_SLK_FLOAT),
    AB_F("Cool1", cool, 0, STB_SLK_FLOAT), AB_F("Cool2", cool, 1, STB_SLK_FLOAT),
    AB_F("Cool3", cool, 2, STB_SLK_FLOAT), AB_F("Cool4", cool, 3, STB_SLK_FLOAT),
    AB_F("Cost1", cost, 0, STB_SLK_FLOAT), AB_F("Cost2", cost, 1, STB_SLK_FLOAT),
    AB_F("Cost3", cost, 2, STB_SLK_FLOAT), AB_F("Cost4", cost, 3, STB_SLK_FLOAT),
    AB_F("Area1", area, 0, STB_SLK_FLOAT), AB_F("Area2", area, 1, STB_SLK_FLOAT),
    AB_F("Area3", area, 2, STB_SLK_FLOAT), AB_F("Area4", area, 3, STB_SLK_FLOAT),
    AB_F("Rng1", range, 0, STB_SLK_FLOAT), AB_F("Rng2", range, 1, STB_SLK_FLOAT),
    AB_F("Rng3", range, 2, STB_SLK_FLOAT), AB_F("Rng4", range, 3, STB_SLK_FLOAT),
    AB_D("Data11", 0, 0), AB_D("Data12", 0, 1), AB_D("Data13", 0, 2), AB_D("Data14", 0, 3),
    AB_D("Data21", 1, 0), AB_D("Data22", 1, 1), AB_D("Data23", 1, 2), AB_D("Data24", 1, 3),
    AB_D("Data31", 2, 0), AB_D("Data32", 2, 1), AB_D("Data33", 2, 2), AB_D("Data34", 2, 3),
    AB_D("DataA1", 0, 0), AB_D("DataB1", 0, 1), AB_D("DataC1", 0, 2), AB_D("DataD1", 0, 3),
    AB_D("DataE1", 0, 4), AB_D("DataF1", 0, 5), AB_D("DataG1", 0, 6), AB_D("DataH1", 0, 7), AB_D("DataI1", 0, 8),
    AB_D("DataA2", 1, 0), AB_D("DataB2", 1, 1), AB_D("DataC2", 1, 2), AB_D("DataD2", 1, 3),
    AB_D("DataE2", 1, 4), AB_D("DataF2", 1, 5), AB_D("DataG2", 1, 6), AB_D("DataH2", 1, 7), AB_D("DataI2", 1, 8),
    AB_D("DataA3", 2, 0), AB_D("DataB3", 2, 1), AB_D("DataC3", 2, 2), AB_D("DataD3", 2, 3),
    AB_D("DataE3", 2, 4), AB_D("DataF3", 2, 5), AB_D("DataG3", 2, 6), AB_D("DataH3", 2, 7), AB_D("DataI3", 2, 8),
    AB_D("DataA4", 3, 0), AB_D("DataB4", 3, 1), AB_D("DataC4", 3, 2), AB_D("DataD4", 3, 3),
    AB_D("DataE4", 3, 4), AB_D("DataF4", 3, 5), AB_D("DataG4", 3, 6), AB_D("DataH4", 3, 7), AB_D("DataI4", 3, 8),
    AB_ID("DataA1", 0, 0), AB_ID("DataB1", 0, 1), AB_ID("DataC1", 0, 2), AB_ID("DataD1", 0, 3),
    AB_ID("DataE1", 0, 4), AB_ID("DataF1", 0, 5), AB_ID("DataG1", 0, 6), AB_ID("DataH1", 0, 7), AB_ID("DataI1", 0, 8),
    AB_ID("DataA2", 1, 0), AB_ID("DataB2", 1, 1), AB_ID("DataC2", 1, 2), AB_ID("DataD2", 1, 3),
    AB_ID("DataE2", 1, 4), AB_ID("DataF2", 1, 5), AB_ID("DataG2", 1, 6), AB_ID("DataH2", 1, 7), AB_ID("DataI2", 1, 8),
    AB_ID("DataA3", 2, 0), AB_ID("DataB3", 2, 1), AB_ID("DataC3", 2, 2), AB_ID("DataD3", 2, 3),
    AB_ID("DataE3", 2, 4), AB_ID("DataF3", 2, 5), AB_ID("DataG3", 2, 6), AB_ID("DataH3", 2, 7), AB_ID("DataI3", 2, 8),
    AB_ID("DataA4", 3, 0), AB_ID("DataB4", 3, 1), AB_ID("DataC4", 3, 2), AB_ID("DataD4", 3, 3),
    AB_ID("DataE4", 3, 4), AB_ID("DataF4", 3, 5), AB_ID("DataG4", 3, 6), AB_ID("DataH4", 3, 7), AB_ID("DataI4", 3, 8),
    AB_F("UnitID1", unitID, 0, STB_SLK_FOURCC), AB_F("UnitID2", unitID, 1, STB_SLK_FOURCC),
    AB_F("UnitID3", unitID, 2, STB_SLK_FOURCC), AB_F("UnitID4", unitID, 3, STB_SLK_FOURCC),
    AB_F("BuffID1", buffID, 0, STB_SLK_STR), AB_F("BuffID2", buffID, 1, STB_SLK_STR),
    AB_F("BuffID3", buffID, 2, STB_SLK_STR), AB_F("BuffID4", buffID, 3, STB_SLK_STR),
    AB_F("EfctID1", efctID, 0, STB_SLK_STR), AB_F("EfctID2", efctID, 1, STB_SLK_STR),
    AB_F("EfctID3", efctID, 2, STB_SLK_STR), AB_F("EfctID4", efctID, 3, STB_SLK_STR),
    { "CastCheck",    offsetof(AbilityData_t, castCheck),    STB_SLK_STR },
    { "DurCheck",     offsetof(AbilityData_t, durCheck),     STB_SLK_STR },
    { "HeroDurCheck", offsetof(AbilityData_t, heroDurCheck), STB_SLK_STR },
    { "CoolCheck",    offsetof(AbilityData_t, coolCheck),    STB_SLK_STR },
    { "CostCheck",    offsetof(AbilityData_t, costCheck),    STB_SLK_STR },
    { "AreaCheck",    offsetof(AbilityData_t, areaCheck),    STB_SLK_STR },
    { "RngCheck",     offsetof(AbilityData_t, rangeCheck),   STB_SLK_STR },
    { "InBeta",       offsetof(AbilityData_t, InBeta),       STB_SLK_BOOL },
    { NULL, 0, 0 }
};
#undef AB_ID
#undef AB_D
#undef AB_F

/* ---- Ability string files to load for Name/Ubertip lookups ---- */
static LPCSTR const ability_string_files[] = {
    "Units\\HumanAbilityStrings.txt",
    "Units\\OrcAbilityStrings.txt",
    "Units\\UndeadAbilityStrings.txt",
    "Units\\NightElfAbilityStrings.txt",
    "Units\\NeutralAbilityStrings.txt",
    "Units\\CommonAbilityStrings.txt",
    "Units\\ItemAbilityStrings.txt",
    "Units\\CampaignAbilityStrings.txt",
    NULL
};

typedef struct {
    DWORD id, code;
    LPCSTR comments, sort, race, buffArt, buffTip, buffUberTip;
    LPCSTR targetArt, specialArt, effectArt, missileArt;
} AbilityBuffData_t;

static slkField_t const ability_buff_schema[] = {
    { "",            offsetof(AbilityBuffData_t, id),          STB_SLK_FOURCC },
    { "code",        offsetof(AbilityBuffData_t, code),        STB_SLK_FOURCC },
    { "comments",    offsetof(AbilityBuffData_t, comments),    STB_SLK_STR    },
    { "sort",        offsetof(AbilityBuffData_t, sort),        STB_SLK_STR    },
    { "race",        offsetof(AbilityBuffData_t, race),        STB_SLK_STR    },
    { "Buffart",     offsetof(AbilityBuffData_t, buffArt),     STB_SLK_STR    },
    { "Bufftip",     offsetof(AbilityBuffData_t, buffTip),     STB_SLK_STR    },
    { "Buffubertip", offsetof(AbilityBuffData_t, buffUberTip), STB_SLK_STR    },
    { "TargetArt",   offsetof(AbilityBuffData_t, targetArt),   STB_SLK_STR    },
    { "SpecialArt",  offsetof(AbilityBuffData_t, specialArt),  STB_SLK_STR    },
    { "EffectArt",   offsetof(AbilityBuffData_t, effectArt),   STB_SLK_STR    },
    { "Missileart",  offsetof(AbilityBuffData_t, missileArt),  STB_SLK_STR    },
    { NULL, 0, 0 }
};

#define MAX_AUDIT_MPQ 16 // paths; War3.mpq + War3x.mpq plus locals fit with margin
static char archive_paths[MAX_AUDIT_MPQ][1024];
static DWORD archive_n;

typedef struct { DWORD id; char cls[48]; char parent[8]; } classRow_t;
static classRow_t classes[1200];
static DWORD class_n;

/* Helper: build a FOURCC key from a string literal at init time. */
#define RK(s) ((DWORD)((unsigned char)(s)[0] | ((unsigned char)(s)[1] << 8) | \
                        ((unsigned char)(s)[2] << 16) | ((unsigned char)(s)[3] << 24)))

/* ---- Rawcodes with implemented handlers in s_skills.c abilitylist[] ---- */
static DWORD const implemented_rawcodes[] = {
    /* Engine commands (string-keyed, skip in output) */
    /* Human */
    RK("AHhb"), RK("AHad"), RK("AHwe"), RK("AHbz"),
    RK("AHtb"), RK("AHca"),
    /* Orc */
    RK("AOsf"), RK("AOmi"),
    /* Night Elf */
    RK("AEbl"), RK("AEfk"), RK("AEsh"), RK("AEim"),
    RK("Aeat"), RK("Ambt"), RK("Aroo"),
    /* Undead */
    RK("AUcs"),
    /* Neutral */
    RK("ANfb"), RK("ANfs"), RK("ANdr"), RK("ANch"),
    RK("AIco"), RK("ANcl"),
    /* Resource / gathering */
    RK("Ahar"), RK("Amic"), RK("Amil"), RK("Arep"),
    RK("Agld"), RK("Agl2"), RK("Abgm"), RK("Abli"),
    RK("Aaha"), RK("Artn"), RK("Awha"), RK("Ahrl"),
    RK("Aent"), RK("Aegm"),
    /* Cargo / transport */
    RK("Acar"), RK("Abun"), RK("Aenc"), RK("Aloa"),
    RK("Adro"), RK("Adri"), RK("Astd"),
    /* Infrastructure / passive */
    RK("Avul"), RK("AInv"), RK("Aneu"), RK("Apit"),
    RK("Aall"), RK("Acoi"), RK("Apxf"), RK("Aren"),
    RK("Arst"),
    /* Items */
    RK("AIhe"), RK("AIma"), RK("AIat"), RK("AIab"),
    RK("AIim"), RK("AIsm"), RK("AIam"), RK("AIxm"),
    RK("AIde"), RK("AIml"), RK("AImm"), RK("AIfs"),
    RK("AImi"), RK("AIem"), RK("AIlm"), RK("AIda"),
    RK("AIct"),
};
#define NUM_IMPLEMENTED (sizeof(implemented_rawcodes) / sizeof(implemented_rawcodes[0]))

static bool is_implemented(DWORD key) {
    for (size_t i = 0; i < NUM_IMPLEMENTED; i++)
        if (implemented_rawcodes[i] == key) return true;
    return false;
}

static char fourcc_buf[5];
static LPCSTR fourcc(DWORD key) {
    memcpy(fourcc_buf, &key, 4);
    fourcc_buf[4] = '\0';
    return fourcc_buf;
}

/* Strip leading/trailing quotes from a tooltip string. */
static LPCSTR strip_quotes(LPCSTR s) {
    if (!s) return NULL;
    while (*s == '"') s++;
    size_t len = strlen(s);
    while (len > 0 && s[len - 1] == '"') len--;
    /* Return a static buffer copy. */
    static char buf[2048];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    return buf;
}

/* Extract first level from comma-separated tooltip. */
static LPCSTR first_level_tip(LPCSTR s) {
    if (!s) return NULL;
    LPCSTR end = strchr(s, ',');
    if (end) {
        static char buf[1024];
        size_t len = (size_t)(end - s);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, s, len);
        buf[len] = '\0';
        return strip_quotes(buf);
    }
    return strip_quotes(s);
}

static void add_archive_cb(const char *path, void *ud) {
    (void)ud;
    if (archive_n >= MAX_AUDIT_MPQ) return;
    snprintf(archive_paths[archive_n++], sizeof(archive_paths[0]), "%s", path);
}

/* War3x.mpq and Frozen Throne overlays replace ROC AbilityData rows; they must not load under -roc. */
static BOOL is_tft_archive(LPCSTR path) {
    LPCSTR p;
    if (strstr(path, "Frozen")) return true;
    for (p = path; p[0] && p[1] && p[2] && p[3] && p[4]; p++)
        if ((p[0] == 'W' || p[0] == 'w') && (p[1] == 'a' || p[1] == 'A') && (p[2] == 'r' || p[2] == 'R') &&
            p[3] == '3' && (p[4] == 'x' || p[4] == 'X'))
            return true;
    return false;
}

static void load_classes(void) {
    FILE *f = fopen("games/warcraft-3/tft-ability-classes.txt", "r");
    char line[512];
    if (!f) return;
    while (fgets(line, sizeof(line), f) && class_n < sizeof(classes) / sizeof(classes[0])) {
        char raw[8] = {0}, cls[48] = {0}, parent[8] = {0};
        LPCSTR p;
        if (sscanf(line, "{ \"%4[^\"]\", \"%47[^\"]\"", raw, cls) != 2) continue;
        p = strstr(line, "parent=\"");
        if (p) sscanf(p + 8, "%7[^\"]", parent);
        classes[class_n].id = RK(raw);
        snprintf(classes[class_n].cls, sizeof(classes[0].cls), "%s", cls);
        snprintf(classes[class_n].parent, sizeof(classes[0].parent), "%s", parent);
        class_n++;
    }
    fclose(f);
}

static classRow_t const *find_class(DWORD id) {
    FOR_LOOP(i, class_n) if (classes[i].id == id) return classes + i;
    return NULL;
}

static DWORD open_archive_set(HANDLE *archives, BOOL tft) {
    DWORD n = 0;
    memset(archives, 0, sizeof(HANDLE) * 8);
    /* First-wins file search: expansion archives must precede ROC. */
    if (tft)
        FOR_LOOP(i, archive_n)
            if (is_tft_archive(archive_paths[i])) Tool_AddArchive(archives, 8, archive_paths[i]);
    FOR_LOOP(i, archive_n)
        if (!is_tft_archive(archive_paths[i])) Tool_AddArchive(archives, 8, archive_paths[i]);
    for (n = 0; n < 8 && archives[n]; n++);
    return n;
}

static void print_fourcc(LPCSTR label, DWORD id) {
    if (!id) { printf("%s=", label); return; }
    printf("%s=%.4s", label, (char const *)&id);
}

static void print_ini_field(stbIniCache_t const *ini, LPCSTR section, LPCSTR key) {
    LPCSTR value = Stb_IniCacheFind(ini, section, key);
    if (value && *value) printf("  %s=%s\n", key, strip_quotes(value));
}

static AbilityBuffData_t const *find_buff(AbilityBuffData_t const *rows, DWORD count, DWORD id) {
    FOR_LOOP(i, count) if (rows[i].id == id) return rows + i;
    return NULL;
}

/* BuffID is a comma list (Bams,Bam2); each token is its own strings/buff-data row. */
static void dump_buff_tokens(LPCSTR list, AbilityBuffData_t const *buffs, DWORD buff_count, stbIniCache_t const *ini) {
    if (!list || !*list) return;
    printf("buffs:\n");
    while (*list) {
        char token[8] = {0};
        DWORD n = 0, id;
        AbilityBuffData_t const *row;
        while (*list && *list != ',' && n < 4) token[n++] = *list++;
        if (*list == ',') list++;
        if (n < 4) continue;
        id = RK(token);
        printf("  %s", token);
        row = find_buff(buffs, buff_count, id);
        if (row && row->comments) printf(" comments=%s", row->comments);
        printf("\n");
        print_ini_field(ini, token, "Bufftip");
        print_ini_field(ini, token, "Buffubertip");
        if (row) {
            if (row->targetArt) printf("    TargetArt=%s\n", row->targetArt);
            if (row->missileArt) printf("    Missileart=%s\n", row->missileArt);
            if (row->effectArt) printf("    EffectArt=%s\n", row->effectArt);
            if (row->specialArt) printf("    SpecialArt=%s\n", row->specialArt);
        }
    }
}

static void dump_level(AbilityData_t const *row, DWORD level) {
    static LPCSTR const data_name[] = { "DataA", "DataB", "DataC", "DataD", "DataE", "DataF", "DataG", "DataH", "DataI" };
    printf("  L%u targs=%s cost=%g cool=%g rng=%g dur=%g heroDur=%g area=%g cast=%g\n",
           level + 1, row->targs[level] ? row->targs[level] : "", row->cost[level], row->cool[level],
           row->range[level], row->dur[level], row->heroDur[level], row->area[level], row->cast[level]);
    printf("     DataA-I=");
    FOR_LOOP(slot, 9) printf("%s%g", slot ? "," : "", row->data[level][slot]);
    printf(" ");
    print_fourcc("UnitID", row->unitID[level]);
    printf(" BuffID=%s EfctID=%s\n", row->buffID[level] ? row->buffID[level] : "",
           row->efctID[level] ? row->efctID[level] : "");
    FOR_LOOP(slot, 9)
        if (row->data[level][slot] != 0.0f) printf("     %s=%g\n", data_name[slot], row->data[level][slot]);
}

static void dump_row(AbilityData_t const *row, AbilityData_t const *rows, DWORD count,
                     AbilityBuffData_t const *buffs, DWORD buff_count, stbIniCache_t const *ini) {
    char id[5] = {0};
    classRow_t const *cls;
    memcpy(id, &row->id, 4);
    print_fourcc("id", row->id); printf(" ");
    print_fourcc("code", row->code); printf(" ");
    print_fourcc("uberAlias", row->uberAlias);
    printf(" comments=%s race=%s levels=%d hero=%d item=%d version=%d\n",
           row->comments ? row->comments : "", row->race ? row->race : "", row->levels, row->hero, row->item, row->version);
    cls = find_class(row->id);
    if (!cls) cls = find_class(row->code);
    if (cls) printf("class: %s parent=%s\n", cls->cls, cls->parent[0] ? cls->parent : "?");
    FOR_LOOP(level, MIN(row->levels > 0 ? row->levels : 1, 4)) dump_level(row, level);
    printf("strings:\n");
    print_ini_field(ini, id, "Name");
    print_ini_field(ini, id, "Ubertip");
    print_ini_field(ini, id, "Untip");
    print_ini_field(ini, id, "Unubertip");
    print_ini_field(ini, id, "Hotkey");
    dump_buff_tokens(row->buffID[0], buffs, buff_count, ini);
    printf("aliases of code=%.4s:\n", (char const *)&row->code);
    FOR_LOOP(i, count) {
        AbilityData_t const *alias = rows + i;
        if (!alias->id || alias->id == row->id) continue;
        if (alias->code != row->code && alias->code != row->id) continue;
        printf("  %.4s", (char const *)&alias->id);
        if (alias->comments) printf(" comments=%s", alias->comments);
        if (alias->targs[0]) printf(" targs=%s", alias->targs[0]);
        printf(" cost=%g", alias->cost[0]);
        FOR_LOOP(slot, 9)
            if (alias->data[0][slot] != 0.0f)
                printf(" Data%c=%g", (char)('A' + slot), alias->data[0][slot]);
        if (alias->buffID[0]) printf(" BuffID=%s", alias->buffID[0]);
        printf("\n");
    }
}

static int dump_raw(DWORD raw, BOOL tft, LPCSTR label) {
    HANDLE archives[8] = {0};
    AbilityData_t *rows = NULL;
    AbilityBuffData_t *buffs = NULL;
    DWORD count, buff_count, n = open_archive_set(archives, tft);
    stbIniCache_t ini = {0};
    AbilityData_t const *hit = NULL;
    if (!n) { fprintf(stderr, "%s: no archives\n", label); return 1; }
    Tool_SetSheetHost(archives, n);
    count = Stb_SlkLoad("Units\\AbilityData.slk", ability_schema, (void **)&rows, sizeof(AbilityData_t));
    if (!count || !rows) { fprintf(stderr, "%s: failed to load AbilityData.slk\n", label); Tool_CloseArchives(archives, 8); return 1; }
    buff_count = Stb_SlkLoad("Units\\AbilityBuffData.slk", ability_buff_schema, (void **)&buffs, sizeof(AbilityBuffData_t));
    Stb_IniCacheLoadFiles(&ini, ability_string_files);
    FOR_LOOP(i, count) if (rows[i].id == raw) { hit = rows + i; break; }
    printf("=== %.4s %s ===\n", (char const *)&raw, label);
    if (tft)
        FOR_LOOP(i, archive_n)
            if (is_tft_archive(archive_paths[i])) printf("archive: %s\n", archive_paths[i]);
    FOR_LOOP(i, archive_n)
        if (!is_tft_archive(archive_paths[i])) printf("archive: %s\n", archive_paths[i]);
    printf("opened %u mpq(s) tft_overlay=%d\n", n, tft);
    if (!hit) printf("AbilityData: not found\n\n");
    else { dump_row(hit, rows, count, buffs, buff_count, &ini); printf("\n"); }
    Stb_IniCacheFree(&ini);
    if (buffs) FS_SLKFreeRows(ability_buff_schema, buffs, buff_count, sizeof(AbilityBuffData_t));
    FS_SLKFreeRows(ability_schema, rows, count, sizeof(AbilityData_t));
    Tool_CloseArchives(archives, 8);
    return hit ? 0 : 1;
}

int main(int argc, char **argv) {
    HANDLE archives[8] = {0};
    size_t archive_count = 0;
    LPCSTR data_dir = NULL;
    bool tft = false, roc_only = false, dump_all = false, have_mpq = false;
    DWORD raw = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-data") && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (!strcmp(argv[i], "-mpq") && i + 1 < argc) {
            add_archive_cb(argv[++i], NULL);
            have_mpq = true;
        } else if (!strcmp(argv[i], "-tft")) {
            tft = true;
        } else if (!strcmp(argv[i], "-roc")) {
            roc_only = true;
        } else if (!strcmp(argv[i], "-all")) {
            dump_all = true;
        } else if (!strcmp(argv[i], "-raw") && i + 1 < argc && strlen(argv[i + 1]) == 4) {
            LPCSTR id = argv[++i];
            raw = RK(id);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            fprintf(stderr, "Usage: %s [-data <dir>] [-mpq <file>] [-tft] [-roc] [-all] [-raw <id>]\n", argv[0]);
            fprintf(stderr, "  -data <dir>  Data directory containing MPQ archives\n");
            fprintf(stderr, "  -mpq <file>  Open a specific MPQ archive\n");
            fprintf(stderr, "  -tft         TFT overlay (War3.mpq + War3x.mpq)\n");
            fprintf(stderr, "  -roc         ROC archives only (no War3x overlay)\n");
            fprintf(stderr, "  -all         Dump all abilities, not just missing ones\n");
            fprintf(stderr, "  -raw <id>    Agent brief: row, strings, buffs, aliases, class\n");
            fprintf(stderr, "               Without -roc/-tft, -raw prints ROC then TFT.\n");
            return 0;
        }
    }

    if (data_dir) Tool_ForEachArchive(data_dir, add_archive_cb, NULL);
    if (!archive_n) {
        fprintf(stderr, "No archives found. Use -data <dir> or -mpq <file>.\n");
        return 1;
    }
    load_classes();

    if (raw) {
        int missing = 0;
        if (have_mpq && !data_dir) return dump_raw(raw, true, "mpq");
        if (roc_only) return dump_raw(raw, false, "ROC");
        if (tft) return dump_raw(raw, true, "TFT");
        missing += dump_raw(raw, false, "ROC");
        missing += dump_raw(raw, true, "TFT");
        return missing == 2 ? 1 : 0;
    }

    archive_count = open_archive_set(archives, !roc_only);
    if (!archive_count) {
        fprintf(stderr, "No archives found. Use -data <dir> or -mpq <file>.\n");
        return 1;
    }
    Tool_SetSheetHost(archives, archive_count);

    /* Load AbilityData.slk. */
    AbilityData_t *rows = NULL;
    DWORD count = Stb_SlkLoad("Units\\AbilityData.slk", ability_schema, (void **)&rows, sizeof(AbilityData_t));
    if (!count || !rows) {
        fprintf(stderr, "Failed to load Units\\AbilityData.slk\n");
        return 1;
    }

    /* Load ability string files for tooltip lookups. */
    stbIniCache_t ini = {0};
    Stb_IniCacheLoadFiles(&ini, ability_string_files);

    /* Build a sorted index for dedup. */
    slkIndex_t idx = {0};
    FS_SLKBuildIndex(&idx, rows, count, sizeof(AbilityData_t));

    /* Collect unique rawcodes (AbilityData has alias rows that map to the same code). */
    typedef struct { DWORD key; DWORD code; LPCSTR name; LPCSTR tip; LONG version; bool hero; bool item; LPCSTR sort; LPCSTR race; int levels; } abilityInfo_t;
    abilityInfo_t *abilities = NULL;
    DWORD ability_count = 0;
    DWORD ability_cap = 0;

    for (DWORD i = 0; i < count; i++) {
        AbilityData_t *row = rows + i;
        if (!row->id) continue;

        /* Resolve the actual code via uberAlias if present. */
        DWORD code = row->code ? row->code : row->id;
        if (row->uberAlias) code = row->uberAlias;

        /* Skip if we already have this code. */
        bool dup = false;
        for (DWORD j = 0; j < ability_count; j++) {
            if (abilities[j].code == code) { dup = true; break; }
        }
        if (dup) continue;

        /* TFT filter. */
        if (!tft && row->version > 0) continue;

        /* Get name from INI strings. */
        char key5[5] = {0};
        memcpy(key5, &row->id, 4);
        LPCSTR name = Stb_IniCacheFind(&ini, key5, "Name");
        if (!name) name = row->comments;
        LPCSTR tip = Stb_IniCacheFind(&ini, key5, "Tip");
        LPCSTR ubertip = Stb_IniCacheFind(&ini, key5, "Ubertip");
        LPCSTR display_tip = ubertip ? ubertip : tip;

        /* Grow array. */
        if (ability_count >= ability_cap) {
            ability_cap = ability_cap ? ability_cap * 2 : 256;
            abilities = realloc(abilities, ability_cap * sizeof(abilityInfo_t));
        }
        abilities[ability_count++] = (abilityInfo_t){
            .key = row->id, .code = code, .name = name,
            .tip = display_tip ? strdup(first_level_tip(display_tip)) : NULL,
            .version = row->version, .hero = row->hero != 0, .item = row->item != 0,
            .sort = row->sort, .race = row->race, .levels = row->levels,
        };
    }

    /* Print results. */
    DWORD missing = 0, implemented = 0;

    if (dump_all) {
        printf("/* All %lu abilities from AbilityData.slk */\n", (unsigned long)ability_count);
        printf("/* Format: rawcode  Name  [HERO|ITEM]  tooltip */\n\n");
    } else {
        printf("/* Missing WC3 abilities — generated by ability_audit */\n");
        printf("/* Add these to abilitylist[] in s_skills.c with handler stubs. */\n\n");
    }

    for (DWORD i = 0; i < ability_count; i++) {
        abilityInfo_t *a = &abilities[i];
        bool impl = is_implemented(a->code);

        if (dump_all) {
            printf("%s  %-4s %-30s %s%s  %s\n",
                   impl ? "  " : "!!",
                   fourcc(a->key),
                   a->name ? a->name : "(unnamed)",
                   a->hero ? "[HERO] " : "",
                   a->item ? "[ITEM] " : "",
                   a->tip ? a->tip : "");
        } else {
            if (impl) { implemented++; continue; }

            /* Classname suggestion: lowercase first letter + rest. */
            char classname[32];
            char raw5[5] = {0};
            memcpy(raw5, &a->key, 4);
            snprintf(classname, sizeof(classname), "a_%s", raw5);

            printf("// TODO: { \"%s\", &%s },  // %s%s",
                   raw5, classname,
                   a->name ? a->name : "(unnamed)",
                   a->hero ? " [HERO]" : "");
            if (a->item) printf(" [ITEM]");
            if (a->sort) printf(" sort=%s", a->sort);
            if (a->race) printf(" race=%s", a->race);
            if (a->levels > 1) printf(" levels=%d", a->levels);
            printf("\n");
            if (a->tip) {
                /* Trim tooltip to 100 chars for readability. */
                char tipbuf[120];
                snprintf(tipbuf, sizeof(tipbuf), "%.100s", a->tip);
                if (strlen(a->tip) > 100) strcat(tipbuf, "...");
                printf("//   %s\n", tipbuf);
            }
            missing++;
        }
    }

    fprintf(stderr, "Total abilities: %lu | Implemented: %lu | Missing: %lu\n",
            (unsigned long)ability_count, (unsigned long)implemented, (unsigned long)missing);

    /* Cleanup. */
    for (DWORD i = 0; i < ability_count; i++) free((void *)abilities[i].tip);
    Stb_IniCacheFree(&ini);
    FS_SLKFreeIndex(&idx);
    FS_SLKFreeRows(ability_schema, rows, count, sizeof(AbilityData_t));
    free(abilities);
    return 0;
}
