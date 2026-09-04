/*
 * menu_dbc.c — Character-creation DBC loader and Lua bindings.
 *
 * Reads ChrRaces, ChrClasses, CharBaseInfo, FactionTemplate, FactionGroup
 * on first use and exposes the data to Lua via the functions declared in
 * menu_dbc.h.  All DBC buffers are kept alive for the lifetime of the process
 * because string pointers point directly into them.
 */
#include "menu_dbc.h"
#include "common/stb_dbc.h"

#include <string.h>
#include <time.h>

/* UI adapts uiimport.* onto stb_dbc.h's shared cache I/O table. */
static void *UI_DbcRead(LPCSTR filename, DWORD *size) {
    void *data = NULL;
    int s = uiimport.FS_ReadFile ? uiimport.FS_ReadFile(filename, &data) : -1;
    if (size) *size = (DWORD)(s > 0 ? s : 0);
    return s > 0 ? data : NULL;
}
static void UI_DbcFreeFile(void *p) { uiimport.FS_FreeFile(p); }
static void *UI_DbcAlloc(size_t n) { return uiimport.MemAlloc ? uiimport.MemAlloc((long)n) : NULL; }
static void UI_DbcFreeMem(void *p) { uiimport.MemFree(p); }
static stbDbcIO_t const ui_dbc_io = { UI_DbcRead, UI_DbcFreeFile, UI_DbcAlloc, UI_DbcFreeMem };

/* -------------------------------------------------------------------------
 * Record types
 * ---------------------------------------------------------------------- */

typedef struct {
    DWORD id, flags, faction_id, male_id, female_id;
    LPCSTR client_file, name, hair_custom, facial_custom[2];
} wowRaceRec_t;

typedef struct { DWORD id; LPCSTR name, filename; } wowClassRec_t;
typedef struct { BYTE race_id, class_id; } wowCharBaseInfoRec_t;
typedef struct { DWORD id, faction, flags, faction_group; } wowFactionTemplateRec_t;
typedef struct { DWORD id, mask_id; LPCSTR internal_name, name; } wowFactionGroupRec_t;

/* Column→field schemas. The DBC stays a table of columns; the struct matches the
 * consumed subset of a row, and Stb_DbcParseRows fills it index by index. */
static stbDbcField_t const race_schema[] = {
    {  0, offsetof(wowRaceRec_t, id),              STB_DBC_U32 },
    {  1, offsetof(wowRaceRec_t, flags),           STB_DBC_U32 },
    {  2, offsetof(wowRaceRec_t, faction_id),      STB_DBC_U32 },
    {  4, offsetof(wowRaceRec_t, male_id),         STB_DBC_U32 },
    {  5, offsetof(wowRaceRec_t, female_id),       STB_DBC_U32 },
    { 15, offsetof(wowRaceRec_t, client_file),     STB_DBC_STR },
    { 17, offsetof(wowRaceRec_t, name),            STB_DBC_STR },
    { 26, offsetof(wowRaceRec_t, hair_custom),     STB_DBC_STR },
    { 27, offsetof(wowRaceRec_t, facial_custom),   STB_DBC_STR, 2 },
};
static stbDbcField_t const class_schema[] = {
    {  0, offsetof(wowClassRec_t, id),       STB_DBC_U32 },
    {  5, offsetof(wowClassRec_t, name),     STB_DBC_STR },
    { 14, offsetof(wowClassRec_t, filename), STB_DBC_STR },
};
static stbDbcField_t const faction_tpl_schema[] = {
    { 0, offsetof(wowFactionTemplateRec_t, id),            STB_DBC_U32 },
    { 1, offsetof(wowFactionTemplateRec_t, faction),       STB_DBC_U32 },
    { 2, offsetof(wowFactionTemplateRec_t, flags),         STB_DBC_U32 },
    { 3, offsetof(wowFactionTemplateRec_t, faction_group), STB_DBC_U32 },
};
static stbDbcField_t const faction_grp_schema[] = {
    { 0, offsetof(wowFactionGroupRec_t, id),            STB_DBC_U32 },
    { 1, offsetof(wowFactionGroupRec_t, mask_id),       STB_DBC_U32 },
    { 2, offsetof(wowFactionGroupRec_t, internal_name), STB_DBC_STR },
    { 3, offsetof(wowFactionGroupRec_t, name),          STB_DBC_STR },
};

#define WOW_MAX_DBC_RACES   16
#define WOW_MAX_DBC_CLASSES 16
#define WOW_MAX_CHAR_BASE   64
#define WOW_MAX_FACTION_TPL 256
#define WOW_MAX_FACTION_GRP 8

/* BZ_HARDCODED_DATA_FALLBACK: used only when test/interface fixtures omit the DBC files. */
#define BZ_WOW_CHARCREATE_FALLBACK_RACE_NAME  "Human"
#define BZ_WOW_CHARCREATE_FALLBACK_CLASS_NAME "Warrior"
#define BZ_WOW_CHARCREATE_FALLBACK_CLASS_FILE "WARRIOR"
#define BZ_WOW_CHARCREATE_FALLBACK_FACTION    "Alliance"

/* -------------------------------------------------------------------------
 * Shared state
 * ---------------------------------------------------------------------- */

static struct {
    wowRaceRec_t            races[WOW_MAX_DBC_RACES];
    int                     num_races;
    wowClassRec_t           classes[WOW_MAX_DBC_CLASSES];
    int                     num_classes;
    wowCharBaseInfoRec_t    base_info[WOW_MAX_CHAR_BASE];
    int                     num_base_info;
    wowFactionTemplateRec_t ftpl[WOW_MAX_FACTION_TPL];
    int                     num_ftpl;
    wowFactionGroupRec_t    fgrp[WOW_MAX_FACTION_GRP];
    int                     num_fgrp;
    BOOL loaded;
    /* character creation selection state */
    int   sel_race;   /* 0-based playable index */
    int   sel_sex;    /* 1 = male, 2 = female (Lua convention) */
    int   sel_class;  /* class ID */
    BYTE  skin, face, hair_style, hair_color, facial_hair;
    float facing;
    /* playable race list: indices into races[] in Alliance-first order */
    int   playable[WOW_MAX_DBC_RACES];
    int   num_playable;
} wow_charcreate;

/* Resident DBC caches (shared stb_dbc.h cache) keep string pointers alive. */
static stbDbcCache_t races_dbc, classes_dbc, ftpl_dbc, fgrp_dbc;
static void *base_info_buf; /* CharBaseInfo 2-byte records keep no string pointers */

/* -------------------------------------------------------------------------
 * Local character list
 * ---------------------------------------------------------------------- */

#define WOW_MAX_CHARACTERS 10

/* Build the writable saved-characters path: ~/.<game>/characters.xml, falling
 * back to <base>/<game>/characters.xml when $HOME is unavailable. */
static void CharList_Path(char *out, int sz) {
    LPCSTR home = uiimport.Cvar_String("fs_homepath", "");

    if (home && *home) {
        snprintf(out, sz, "%s/characters.xml", home);
    } else {
        snprintf(out, sz, "%s/%s/characters.xml", uiimport.Cvar_String("fs_basepath", "share"), BZ_GAME);
    }
}

typedef struct {
    char  name[64];
    DWORD race_id;
    DWORD sex_id;
    DWORD class_id;
    DWORD appearance;
} wowCharEntry_t;

static struct {
    wowCharEntry_t entries[WOW_MAX_CHARACTERS];
    int            count;
    BOOL           loaded;
} wow_charlist;

/* Trim leading whitespace from s, return pointer past it. */
static LPCSTR CharList_TrimLeft(LPCSTR s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

/* Find attr="value" in a tag string, copy value into buf (max len).
   Returns true on success. */
static BOOL CharList_XmlAttr(LPCSTR tag, LPCSTR attr, char *buf, int len) {
    char needle[64];
    LPCSTR p;
    char q;

    snprintf(needle, sizeof(needle), "%s=", attr);
    p = strstr(tag, needle);
    if (!p) return false;
    p += strlen(needle);
    q = *p;
    if (q != '"' && q != '\'') return false;
    p++;
    int i = 0;
    while (*p && *p != q && i < len - 1)
        buf[i++] = *p++;
    buf[i] = '\0';
    return true;
}

static void CharList_Load(void) {
    void *buf = NULL;
    int   size;
    LPCSTR p;

    wow_charlist.count  = 0;
    wow_charlist.loaded = true;

    if (!uiimport.FS_ReadFile) return;
    char path[512];
    CharList_Path(path, sizeof(path));
    size = uiimport.FS_ReadFile(path, &buf);
    if (size <= 0 || !buf) { SAFE_DELETE(buf, uiimport.FS_FreeFile); return; }

    p = (LPCSTR)buf;
    while (*p && wow_charlist.count < WOW_MAX_CHARACTERS) {
        p = CharList_TrimLeft(p);
        if (*p != '<') { p++; continue; }
        p++;
        if (strncmp(p, "Character", 9) != 0) {
            while (*p && *p != '>') p++;
            if (*p) p++;
            continue;
        }
        /* find end of tag */
        LPCSTR tag_end = strchr(p, '>');
        if (!tag_end) break;
        int tag_len = (int)(tag_end - p);
        char tag[512];
        if (tag_len >= (int)sizeof(tag)) { p = tag_end + 1; continue; }
        memcpy(tag, p, (size_t)tag_len);
        tag[tag_len] = '\0';

        wowCharEntry_t *e = &wow_charlist.entries[wow_charlist.count];
        char tmp[32];
        if (!CharList_XmlAttr(tag, "name", e->name, sizeof(e->name))) { p = tag_end + 1; continue; }
        e->race_id    = CharList_XmlAttr(tag, "race",       tmp, sizeof(tmp)) ? (DWORD)atoi(tmp) : 1;
        e->sex_id     = CharList_XmlAttr(tag, "sex",        tmp, sizeof(tmp)) ? (DWORD)atoi(tmp) : 1;
        e->class_id   = CharList_XmlAttr(tag, "class",      tmp, sizeof(tmp)) ? (DWORD)atoi(tmp) : 1;
        e->appearance = CharList_XmlAttr(tag, "appearance", tmp, sizeof(tmp)) ? (DWORD)atoi(tmp) : 0;
        wow_charlist.count++;
        p = tag_end + 1;
    }
    uiimport.FS_FreeFile(buf);
}

static void CharList_Save(void) {
    char xml[4096];
    int  pos = 0;

    if (!uiimport.FS_WriteFile) return;

    pos += snprintf(xml + pos, sizeof(xml) - (size_t)pos, "<Characters>\n");
    for (int i = 0; i < wow_charlist.count; i++) {
        wowCharEntry_t *e = &wow_charlist.entries[i];
        pos += snprintf(xml + pos, sizeof(xml) - (size_t)pos, "  <Character name=\"%s\" race=\"%u\" sex=\"%u\"" " class=\"%u\" appearance=\"%u\" />\n", e->name, e->race_id, e->sex_id, e->class_id, e->appearance);
    }
    pos += snprintf(xml + pos, sizeof(xml) - (size_t)pos, "</Characters>\n");
    {
        char path[512];
        CharList_Path(path, sizeof(path));
        uiimport.FS_WriteFile(path, xml, pos);
    }
}

/* -------------------------------------------------------------------------
 * DBC load
 * ---------------------------------------------------------------------- */

static void UIWow_LoadCharCreateDbc(void) {
    if (wow_charcreate.loaded) return;
    wow_charcreate.loaded = true;

    /* ChrRaces (29-field 1.x) */
    if (Stb_DbcCacheLoad(&races_dbc, "DBFilesClient\\ChrRaces.dbc", &ui_dbc_io) &&
        Stb_DbcCacheDecode(&races_dbc, race_schema, sizeof(race_schema) / sizeof(race_schema[0]), sizeof(wowRaceRec_t), &ui_dbc_io)) {
        DWORD count = MIN(races_dbc.records, WOW_MAX_DBC_RACES);
        Stb_DbcNormalizeStrings(races_dbc.rows, count, sizeof(wowRaceRec_t), race_schema, sizeof(race_schema) / sizeof(race_schema[0]));
        wowRaceRec_t const *rows = races_dbc.rows;
        FOR_LOOP(i, count) {
            if (rows[i].flags & 0x1) continue; /* NPC-only */
            if (wow_charcreate.num_races >= WOW_MAX_DBC_RACES) break;
            wow_charcreate.races[wow_charcreate.num_races++] = rows[i];
        }
    }

    /* ChrClasses (16-field 1.x): 0=id, 5=name(str), 14=filename(str) */
    if (Stb_DbcCacheLoad(&classes_dbc, "DBFilesClient\\ChrClasses.dbc", &ui_dbc_io) &&
        Stb_DbcCacheDecode(&classes_dbc, class_schema, sizeof(class_schema) / sizeof(class_schema[0]), sizeof(wowClassRec_t), &ui_dbc_io)) {
        DWORD count = MIN(classes_dbc.records, WOW_MAX_DBC_CLASSES);
        Stb_DbcNormalizeStrings(classes_dbc.rows, count, sizeof(wowClassRec_t), class_schema, sizeof(class_schema) / sizeof(class_schema[0]));
        memcpy(wow_charcreate.classes, classes_dbc.rows, count * sizeof(wow_charcreate.classes[0]));
        wow_charcreate.num_classes = (int)count;
    }

    /* CharBaseInfo — 2-byte records: raceID (byte), classID (byte). record_size
     * 2 < 4 fails Stb_DbcValid's envelope floor, so decode it directly. */
    {
        int size = 0;
        void *data = NULL;
        if (uiimport.FS_ReadFile) size = uiimport.FS_ReadFile("DBFilesClient\\CharBaseInfo.dbc", &data);
        if (data && size >= 20) {
            BYTE const *rb = (BYTE const *)data + 20;
            DWORD count = MIN(Stb_DbcRead32((BYTE const *)data + 4), WOW_MAX_CHAR_BASE);
            base_info_buf = data;
            FOR_LOOP(i, count) {
                wow_charcreate.base_info[wow_charcreate.num_base_info].race_id  = rb[i * 2];
                wow_charcreate.base_info[wow_charcreate.num_base_info].class_id = rb[i * 2 + 1];
                wow_charcreate.num_base_info++;
            }
        } else if (data) {
            uiimport.FS_FreeFile(data);
        }
    }

    /* FactionTemplate: 0=id, 1=faction, 2=flags, 3=factionGroup */
    if (Stb_DbcCacheLoad(&ftpl_dbc, "DBFilesClient\\FactionTemplate.dbc", &ui_dbc_io) &&
        Stb_DbcCacheDecode(&ftpl_dbc, faction_tpl_schema, sizeof(faction_tpl_schema) / sizeof(faction_tpl_schema[0]), sizeof(wowFactionTemplateRec_t), &ui_dbc_io)) {
        DWORD count = MIN(ftpl_dbc.records, WOW_MAX_FACTION_TPL);
        memcpy(wow_charcreate.ftpl, ftpl_dbc.rows, count * sizeof(wow_charcreate.ftpl[0]));
        wow_charcreate.num_ftpl = (int)count;
    }

    /* FactionGroup: 0=id, 1=maskID, 2=internalName(str), 3=name(str) */
    if (Stb_DbcCacheLoad(&fgrp_dbc, "DBFilesClient\\FactionGroup.dbc", &ui_dbc_io) &&
        Stb_DbcCacheDecode(&fgrp_dbc, faction_grp_schema, sizeof(faction_grp_schema) / sizeof(faction_grp_schema[0]), sizeof(wowFactionGroupRec_t), &ui_dbc_io)) {
        DWORD count = MIN(fgrp_dbc.records, WOW_MAX_FACTION_GRP);
        Stb_DbcNormalizeStrings(fgrp_dbc.rows, count, sizeof(wowFactionGroupRec_t), faction_grp_schema, sizeof(faction_grp_schema) / sizeof(faction_grp_schema[0]));
        memcpy(wow_charcreate.fgrp, fgrp_dbc.rows, count * sizeof(wow_charcreate.fgrp[0]));
        wow_charcreate.num_fgrp = (int)count;
    }

    /* Build playable race list: Alliance first, then Horde */
    static LPCSTR const sides[] = { "Alliance", "Horde" };
    FOR_LOOP(side, 2) {
        FOR_LOOP(ri, wow_charcreate.num_races) {
            wowRaceRec_t const *race = &wow_charcreate.races[ri];
            wowFactionTemplateRec_t const *ftpl = NULL;
            FOR_LOOP(fi, wow_charcreate.num_ftpl) {
                if (wow_charcreate.ftpl[fi].id == race->faction_id) { ftpl = &wow_charcreate.ftpl[fi]; break; }
            }
            if (!ftpl) continue;
            FOR_LOOP(gi, wow_charcreate.num_fgrp) {
                wowFactionGroupRec_t const *grp = &wow_charcreate.fgrp[gi];
                if (!grp->mask_id) continue;
                if (!((1u << grp->mask_id) & ftpl->faction_group)) continue;
                if (strcasecmp(grp->internal_name, sides[side]) == 0) {
                    if (wow_charcreate.num_playable < WOW_MAX_DBC_RACES)
                        wow_charcreate.playable[wow_charcreate.num_playable++] = ri;
                }
            }
        }
    }

    wow_charcreate.sel_race  = 0;
    wow_charcreate.sel_sex   = 1;
    wow_charcreate.sel_class = 1;
    wow_charcreate.facing    = -15.0f;
}

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

static LPCSTR UIWow_FactionNameForRace(int race_idx_1based, LPCSTR *internal_out) {
    UIWow_LoadCharCreateDbc();
    int pi = race_idx_1based - 1;
    if (pi < 0 || pi >= wow_charcreate.num_playable) return NULL;
    wowRaceRec_t const *race = &wow_charcreate.races[wow_charcreate.playable[pi]];
    FOR_LOOP(fi, wow_charcreate.num_ftpl) {
        if (wow_charcreate.ftpl[fi].id != race->faction_id) continue;
        DWORD fg = wow_charcreate.ftpl[fi].faction_group;
        FOR_LOOP(gi, wow_charcreate.num_fgrp) {
            wowFactionGroupRec_t const *grp = &wow_charcreate.fgrp[gi];
            if (grp->mask_id && ((1u << grp->mask_id) & fg)) {
                if (internal_out) *internal_out = grp->internal_name;
                return grp->name;
            }
        }
    }
    return NULL;
}

static wowClassRec_t const *UIWow_ClassByID(int class_id) {
    FOR_LOOP(i, wow_charcreate.num_classes)
        if ((int)wow_charcreate.classes[i].id == class_id) return &wow_charcreate.classes[i];
    return NULL;
}

static wowClassRec_t const *UIWow_ClassForRaceButton(int button_index) {
    int pi = wow_charcreate.sel_race, count = 0;
    if (button_index < 1 || pi < 0 || pi >= wow_charcreate.num_playable) return NULL;
    int race_id = (int)wow_charcreate.races[wow_charcreate.playable[pi]].id;
    FOR_LOOP(bi, wow_charcreate.num_base_info) {
        if (wow_charcreate.base_info[bi].race_id != race_id) continue;
        int class_id = wow_charcreate.base_info[bi].class_id;
        FOR_LOOP(ci, wow_charcreate.num_classes) {
            if ((int)wow_charcreate.classes[ci].id != class_id) continue;
            if (++count == button_index) return &wow_charcreate.classes[ci];
            break;
        }
    }
    return NULL;
}

static void UIWow_SelectFirstClassForRace(void) {
    wowClassRec_t const *c = UIWow_ClassForRaceButton(1);
    if (c) wow_charcreate.sel_class = (int)c->id;
}

/* -------------------------------------------------------------------------
 * Lua bindings
 * ---------------------------------------------------------------------- */

int UIWow_LuaGetAvailableRaces(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    if (!wow_charcreate.num_playable) {
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_RACE_NAME);
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_RACE_NAME);
        return 2;
    }
    FOR_LOOP(i, wow_charcreate.num_playable) {
        wowRaceRec_t const *r = &wow_charcreate.races[wow_charcreate.playable[i]];
        LPCSTR name = r->name;
        lua_pushstring(L, name[0] ? name : r->client_file);
        lua_pushstring(L, r->client_file);
    }
    return wow_charcreate.num_playable * 2;
}

int UIWow_LuaGetAvailableClasses(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    if (!wow_charcreate.num_classes) {
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_CLASS_NAME);
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_CLASS_FILE);
        return 2;
    }
    FOR_LOOP(i, wow_charcreate.num_classes) {
        wowClassRec_t const *c = &wow_charcreate.classes[i];
        lua_pushstring(L, c->name[0] ? c->name : c->filename);
        lua_pushstring(L, c->filename);
    }
    return wow_charcreate.num_classes * 2;
}

int UIWow_LuaGetClassesForRace(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int pi = wow_charcreate.sel_race;
    if (!wow_charcreate.num_playable || !wow_charcreate.num_classes) {
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_CLASS_NAME);
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_CLASS_FILE);
        return 2;
    }
    if (pi < 0 || pi >= wow_charcreate.num_playable) return 0;
    int race_id = (int)wow_charcreate.races[wow_charcreate.playable[pi]].id;
    int count = 0;
    FOR_LOOP(bi, wow_charcreate.num_base_info) {
        if (wow_charcreate.base_info[bi].race_id != race_id) continue;
        int class_id = wow_charcreate.base_info[bi].class_id;
        FOR_LOOP(ci, wow_charcreate.num_classes) {
            if ((int)wow_charcreate.classes[ci].id != class_id) continue;
            wowClassRec_t const *c = &wow_charcreate.classes[ci];
            lua_pushstring(L, c->name[0] ? c->name : c->filename);
            lua_pushstring(L, c->filename);
            count++;
            break;
        }
    }
    return count * 2;
}

int UIWow_LuaGetFactionForRace(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    LPCSTR internal = NULL;
    LPCSTR name = UIWow_FactionNameForRace(wow_charcreate.sel_race + 1, &internal);
    if (!wow_charcreate.num_playable) {
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_FACTION);
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_FACTION);
        return 2;
    }
    if (!name) { lua_pushstring(L, ""); lua_pushstring(L, ""); return 2; }
    lua_pushstring(L, name);
    lua_pushstring(L, internal ? internal : "");
    return 2;
}

int UIWow_LuaGetNameForRace(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int pi = wow_charcreate.sel_race;
    if (!wow_charcreate.num_playable) {
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_RACE_NAME);
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_RACE_NAME);
        return 2;
    }
    if (pi < 0 || pi >= wow_charcreate.num_playable) { lua_pushstring(L, ""); lua_pushstring(L, ""); return 2; }
    wowRaceRec_t const *r = &wow_charcreate.races[wow_charcreate.playable[pi]];
    LPCSTR name = r->name;
    lua_pushstring(L, name[0] ? name : r->client_file);
    lua_pushstring(L, r->client_file);
    return 2;
}

int UIWow_LuaGetSelectedRace(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    lua_pushnumber(L, wow_charcreate.sel_race + 1);
    return 1;
}

int UIWow_LuaGetSelectedSex(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    lua_pushnumber(L, wow_charcreate.sel_sex);
    return 1;
}

int UIWow_LuaGetSelectedClass(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    wowClassRec_t const *c = UIWow_ClassByID(wow_charcreate.sel_class);
    if (!wow_charcreate.num_classes) {
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_CLASS_NAME);
        lua_pushstring(L, BZ_WOW_CHARCREATE_FALLBACK_CLASS_FILE);
        lua_pushnumber(L, 1);
        lua_pushboolean(L, 0); lua_pushboolean(L, 0); lua_pushboolean(L, 1);
        return 6;
    }
    if (!c) return 0;
    lua_pushstring(L, c->name[0] ? c->name : c->filename);
    lua_pushstring(L, c->filename);
    lua_pushnumber(L, c->id);
    lua_pushboolean(L, 0); /* tank */
    lua_pushboolean(L, 0); /* healer */
    lua_pushboolean(L, 1); /* damage */
    return 6;
}

BOOL UIWow_SetSelectedRace(int race_index) {
    UIWow_LoadCharCreateDbc();
    int v = race_index - 1;

    if (v < 0 || v >= wow_charcreate.num_playable || v == wow_charcreate.sel_race)
        return false;
    wow_charcreate.sel_race = v;
    UIWow_SelectFirstClassForRace();
    return true;
}

int UIWow_LuaSetSelectedRace(lua_State *L) {
    UIWow_SetSelectedRace((int)luaL_checknumber(L, 1));
    return 0;
}

BOOL UIWow_SetSelectedSex(int sex) {
    UIWow_LoadCharCreateDbc();
    if ((sex != 1 && sex != 2) || sex == wow_charcreate.sel_sex)
        return false;
    wow_charcreate.sel_sex = sex;
    return true;
}

int UIWow_LuaSetSelectedSex(lua_State *L) {
    UIWow_SetSelectedSex((int)luaL_checknumber(L, 1));
    return 0;
}

BOOL UIWow_SetSelectedClass(int class_index) {
    UIWow_LoadCharCreateDbc();
    wowClassRec_t const *c = UIWow_ClassForRaceButton(class_index);
    if (!c && class_index >= 1 && class_index <= wow_charcreate.num_classes)
        c = &wow_charcreate.classes[class_index - 1];
    if (!c || (int)c->id == wow_charcreate.sel_class)
        return false;
    wow_charcreate.sel_class = (int)c->id;
    return true;
}

int UIWow_LuaSetSelectedClass(lua_State *L) {
    UIWow_SetSelectedClass((int)luaL_checknumber(L, 1));
    return 0;
}

int UIWow_LuaIsRaceClassValid(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int ri = (int)luaL_checknumber(L, 1) - 1;
    int ci = (int)luaL_checknumber(L, 2) - 1;
    if (ri < 0 || ri >= wow_charcreate.num_playable || ci < 0 || ci >= wow_charcreate.num_classes)
        { lua_pushnil(L); return 1; }
    int race_id  = (int)wow_charcreate.races[wow_charcreate.playable[ri]].id;
    int class_id = (int)wow_charcreate.classes[ci].id;
    FOR_LOOP(bi, wow_charcreate.num_base_info) {
        if (wow_charcreate.base_info[bi].race_id == race_id &&
            wow_charcreate.base_info[bi].class_id == class_id)
            { lua_pushnumber(L, 1); return 1; }
    }
    lua_pushnil(L); return 1;
}

int UIWow_LuaGetHairCustomization(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int pi = wow_charcreate.sel_race;
    LPCSTR s = (pi >= 0 && pi < wow_charcreate.num_playable)
        ? wow_charcreate.races[wow_charcreate.playable[pi]].hair_custom : "";
    lua_pushstring(L, (s && s[0]) ? s : "NORMAL");
    return 1;
}

int UIWow_LuaGetFacialHairCustomization(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int pi = wow_charcreate.sel_race;
    int sex = (wow_charcreate.sel_sex == 1) ? 0 : 1;
    LPCSTR s = (pi >= 0 && pi < wow_charcreate.num_playable)
        ? wow_charcreate.races[wow_charcreate.playable[pi]].facial_custom[sex] : "";
    lua_pushstring(L, (s && s[0]) ? s : "NORMAL");
    return 1;
}

int UIWow_LuaGetCharacterCreateFacing(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    lua_pushnumber(L, wow_charcreate.facing);
    return 1;
}

int UIWow_LuaSetCharacterCreateFacing(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    wow_charcreate.facing = (float)luaL_checknumber(L, 1);
    return 0;
}

/* Format the selected race/gender character M2 used by the glue create scene. */
void UIWow_GetCharacterCreateModelPath(LPSTR out, size_t out_size) {
    LPCSTR race = BZ_WOW_CHARCREATE_FALLBACK_RACE_NAME;
    LPCSTR gender = wow_charcreate.sel_sex == 2 ? "Female" : "Male";

    if (!out || out_size == 0) return;
    UIWow_LoadCharCreateDbc();
    if (wow_charcreate.sel_race >= 0 && wow_charcreate.sel_race < wow_charcreate.num_playable) {
        LPCSTR client_file = wow_charcreate.races[wow_charcreate.playable[wow_charcreate.sel_race]].client_file;
        if (client_file && client_file[0]) race = client_file;
    }
    snprintf(out, out_size, "Character\\%s\\%s\\%s%s.m2", race, gender, race, gender);
}

DWORD UIWow_GetCharacterCreateAppearance(void) {
    UIWow_LoadCharCreateDbc();
    return Wow_PackAppearance(wow_charcreate.skin, wow_charcreate.face, wow_charcreate.hair_style, wow_charcreate.hair_color, wow_charcreate.facial_hair, (BYTE)wow_charcreate.sel_class, 0);
}

FLOAT UIWow_GetCharacterCreateFacing(void) {
    UIWow_LoadCharCreateDbc();
    return wow_charcreate.facing;
}

/* Format the character M2 path from a saved character entry (char-select screen). */
void UIWow_GetCharacterSelectModelPath(LPSTR out, size_t out_size) {
    LPCSTR race = BZ_WOW_CHARCREATE_FALLBACK_RACE_NAME;
    LPCSTR gender = "Male";
    wowCharEntry_t const *e;

    if (!out || out_size == 0) return;
    if (!wow_charlist.loaded) CharList_Load();
    if (wow_ui.selected_char_idx < 0 || wow_ui.selected_char_idx >= wow_charlist.count) {
        out[0] = '\0';
        return;
    }
    e = &wow_charlist.entries[wow_ui.selected_char_idx];
    gender = (e->sex_id == 2) ? "Female" : "Male";
    UIWow_LoadCharCreateDbc();
    FOR_LOOP(ri, wow_charcreate.num_races) {
        if (wow_charcreate.races[ri].id == (int)e->race_id) {
            if (wow_charcreate.races[ri].client_file && wow_charcreate.races[ri].client_file[0])
                race = wow_charcreate.races[ri].client_file;
            break;
        }
    }
    snprintf(out, out_size, "Character\\%s\\%s\\%s%s.m2", race, gender, race, gender);
}

/* Return packed appearance from a saved character entry (char-select screen). */
DWORD UIWow_GetCharacterSelectAppearance(void) {
    if (!wow_charlist.loaded) CharList_Load();
    if (wow_ui.selected_char_idx >= 0 && wow_ui.selected_char_idx < wow_charlist.count)
        return wow_charlist.entries[wow_ui.selected_char_idx].appearance;
    return 0;
}

/* Pack the selected character's race/sex/class/appearance into a single
   userinfo-style cvar so the game module can read it during Wow_Init and
   store in CS_PLAYERSKINS + client number.  Format: \race\Human\sex\Male\class\1\appearance\12345 */
void UIWow_SetSelectedCharCvars(void) {
    wowCharEntry_t const *e;
    LPCSTR race = "Orc";
    LPCSTR sex = "Male";
    char userinfo[MAX_PATHLEN];

    if (!wow_charlist.loaded) CharList_Load();
    if (wow_ui.selected_char_idx < 0 || wow_ui.selected_char_idx >= wow_charlist.count)
        return;
    e = &wow_charlist.entries[wow_ui.selected_char_idx];
    sex = (e->sex_id == 2) ? "Female" : "Male";
    UIWow_LoadCharCreateDbc();
    FOR_LOOP(ri, wow_charcreate.num_races) {
        if (wow_charcreate.races[ri].id == (int)e->race_id) {
            if (wow_charcreate.races[ri].client_file && wow_charcreate.races[ri].client_file[0])
                race = wow_charcreate.races[ri].client_file;
            break;
        }
    }
    snprintf(userinfo, sizeof(userinfo), "\\race\\%s\\sex\\%s\\class\\%u\\appearance\\%u", race, sex, (unsigned)e->class_id, (unsigned)e->appearance);
    uiimport.Cvar_Set(WOW_CVAR_PLAYERINFO, userinfo);
}

int UIWow_LuaResetCharCustomize(lua_State *L) {
    (void)L;
    UIWow_LoadCharCreateDbc();
    wow_charcreate.sel_race  = 0;
    wow_charcreate.sel_sex   = 1;
    wow_charcreate.sel_class = 1;
    wow_charcreate.skin = wow_charcreate.face = wow_charcreate.hair_style = 0;
    wow_charcreate.hair_color = wow_charcreate.facial_hair = 0;
    return 0;
}

int UIWow_LuaCycleCharCustomization(lua_State *L) {
    BYTE *field = NULL;
    int id = (int)luaL_checknumber(L, 1), delta = (int)luaL_checknumber(L, 2);

    UIWow_LoadCharCreateDbc();
    switch (id) {
        case 1: field = &wow_charcreate.skin; break;
        case 2: field = &wow_charcreate.face; break;
        case 3: field = &wow_charcreate.hair_style; break;
        case 4: field = &wow_charcreate.hair_color; break;
        case 5: field = &wow_charcreate.facial_hair; break;
        default: return 0;
    }
    *field = (BYTE)((*field + (delta < 0 ? 4 : 1)) % 5);
    return 0;
}

int UIWow_LuaRandomizeCharCustomization(lua_State *L) {
    (void)L;
    UIWow_LoadCharCreateDbc();
    wow_charcreate.skin = (wow_charcreate.skin + 1) % 5;
    wow_charcreate.face = (wow_charcreate.face + 2) % 5;
    wow_charcreate.hair_style = (wow_charcreate.hair_style + 3) % 5;
    wow_charcreate.hair_color = (wow_charcreate.hair_color + 4) % 5;
    wow_charcreate.facial_hair = (wow_charcreate.facial_hair + 1) % 5;
    return 0;
}

int UIWow_LuaGetRandomName(lua_State *L) {
    static char const *prefix[] = {
        "Ara", "Bel", "Cor", "Dal", "Eir", "Fal", "Gar", "Hal",
        "Ili", "Jar", "Kal", "Lor", "Mer", "Nor", "Orl", "Pyr",
        "Rhi", "Ser", "Tal", "Uri", "Val", "Wen", "Xan", "Zal",
    };
    static char const *suffix[] = {
        "andas", "eth", "in", "ius", "ond", "ara", "ius", "wen",
        "ath", "ric", "nor", "wyn", "gor", "ash", "den", "mir",
        "ath", "lor", "rin", "ith", "ore", "ane", "wyn", "us",
    };
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)clock();
    int p = (int)(seed % 24); seed /= 24;
    int s = (int)(seed % 24); seed /= 24;
    int n = (int)(seed % 100);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s%d", prefix[p], suffix[s], n);
    lua_pushstring(L, buf);
    return 1;
}

int UIWow_LuaCreateCharacter(lua_State *L) {
    LPCSTR name = luaL_checkstring(L, 1);
    wowCharEntry_t *e;
    int pi, race_id, sex_id;
    char generated[64];

    UIWow_LoadCharCreateDbc();

    if (!wow_charlist.loaded)
        CharList_Load();

    if (wow_charlist.count >= WOW_MAX_CHARACTERS) {
        lua_pushstring(L, "ERR_CHAR_CREATE_LIST_FULL");
        lua_setglobal(L, "CharacterCreateResult");
        return 0;
    }

    if (!name || !name[0]) {
        UIWow_LuaGetRandomName(L);
        name = lua_tostring(L, -1);
        snprintf(generated, sizeof(generated), "%s", name);
        lua_pop(L, 1);
        name = generated;
    }

    pi      = wow_charcreate.sel_race;
    race_id = (pi >= 0 && pi < wow_charcreate.num_playable)
                  ? (int)wow_charcreate.races[wow_charcreate.playable[pi]].id : 1;
    sex_id  = wow_charcreate.sel_sex;

    e = &wow_charlist.entries[wow_charlist.count++];
    snprintf(e->name, sizeof(e->name), "%s", name);
    e->race_id    = (DWORD)race_id;
    e->sex_id     = (DWORD)sex_id;
    e->class_id   = (DWORD)wow_charcreate.sel_class;
    e->appearance = UIWow_GetCharacterCreateAppearance();

    CharList_Save();

    /* Fire CharacterCreateResult("OKAY") so the Lua UI advances. */
    lua_getglobal(L, "CharacterCreateResult");
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, "OKAY");
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            fprintf(stderr, "UIWow CreateCharacter: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    return 0;
}

int UIWow_LuaCharacterCreateResult(lua_State *L) {
    LPCSTR result = luaL_checkstring(L, 1);
    lua_getglobal(L, "SetGlueScreen");
    if (lua_isfunction(L, -1)) {
        lua_pushstring(L, (result && strcmp(result, "OKAY") == 0) ? "charselect" : "charcreate");
        lua_pcall(L, 1, 0, 0);
    } else {
        lua_pop(L, 1);
    }
    return 0;
}

int UIWow_LuaGetNumCharacters(lua_State *L) {
    (void)L;
    if (!wow_charlist.loaded)
        CharList_Load();
    lua_pushinteger(L, wow_charlist.count);
    return 1;
}

int UIWow_LuaGetCharacterInfo(lua_State *L) {
    int index = (int)luaL_checkinteger(L, 1) - 1; /* Lua is 1-based */
    wowCharEntry_t const *e;
    wowClassRec_t const *cls;
    wowRaceRec_t const *race = NULL;
    LPCSTR race_name = "", race_file = "", class_name = "";

    UIWow_LoadCharCreateDbc();
    if (!wow_charlist.loaded)
        CharList_Load();

    if (index < 0 || index >= wow_charlist.count) {
        for (int i = 0; i < 10; i++) lua_pushnil(L);
        return 10;
    }

    e = &wow_charlist.entries[index];

    /* Resolve race name and client file from DBC. */
    FOR_LOOP(ri, wow_charcreate.num_races) {
        if (wow_charcreate.races[ri].id == e->race_id) {
            race = &wow_charcreate.races[ri];
            race_name = race->name;
            race_file = race->client_file ? race->client_file : "";
            break;
        }
    }

    cls = UIWow_ClassByID((int)e->class_id);
    if (cls) class_name = cls->name ? cls->name : "";

    /* Return: name, race, class, level, zone, fileString, gender,
               guild, status, className, raceFileName */
    lua_pushstring(L, e->name);       /* 1  name */
    lua_pushstring(L, race_name);     /* 2  race  (localised) */
    lua_pushstring(L, class_name);    /* 3  class (localised) */
    lua_pushinteger(L, 1);            /* 4  level */
    lua_pushstring(L, "");            /* 5  zone */
    lua_pushstring(L, race_file);     /* 6  fileString */
    lua_pushinteger(L, (int)e->sex_id - 1); /* 7  gender (0=male,1=female) */
    lua_pushstring(L, "");            /* 8  guild */
    lua_pushstring(L, "");            /* 9  status */
    lua_pushstring(L, class_name);    /* 10 className */
    lua_pushstring(L, race_file);     /* 11 raceFileName */
    return 11;
}
