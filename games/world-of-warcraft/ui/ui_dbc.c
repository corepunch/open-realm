/*
 * ui_dbc.c — Character-creation DBC loader and Lua bindings.
 *
 * Reads ChrRaces, ChrClasses, CharBaseInfo, FactionTemplate, FactionGroup
 * on first use and exposes the data to Lua via the functions declared in
 * ui_dbc.h.  All DBC buffers are kept alive for the lifetime of the process
 * because string pointers point directly into them.
 */
#include "ui_dbc.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Record types
 * ---------------------------------------------------------------------- */

typedef struct {
    DWORD id; DWORD faction_id; DWORD flags; DWORD male_id; DWORD female_id;
    LPCSTR client_prefix; LPCSTR client_file; LPCSTR name; LPCSTR name_female;
    LPCSTR hair_custom; LPCSTR facial_custom[2]; DWORD required_exp;
} wowRaceRec_t;

typedef struct { DWORD id; LPCSTR name; LPCSTR filename; DWORD required_exp; } wowClassRec_t;
typedef struct { BYTE race_id; BYTE class_id; } wowCharBaseInfoRec_t;
typedef struct { DWORD id; DWORD faction; DWORD flags; DWORD faction_group; } wowFactionTemplateRec_t;
typedef struct { DWORD id; DWORD mask_id; LPCSTR internal_name; LPCSTR name; } wowFactionGroupRec_t;

#define WOW_MAX_DBC_RACES   16
#define WOW_MAX_DBC_CLASSES 16
#define WOW_MAX_CHAR_BASE   64
#define WOW_MAX_FACTION_TPL 256
#define WOW_MAX_FACTION_GRP 8

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
    /* raw buffers kept alive for string pointers */
    void *races_buf, *classes_buf, *base_buf, *ftpl_buf, *fgrp_buf;
    BOOL loaded;
    /* character creation selection state */
    int   sel_race;   /* 0-based playable index */
    int   sel_sex;    /* 1 = male, 2 = female (Lua convention) */
    int   sel_class;  /* 1-based class index */
    float facing;
    /* playable race list: indices into races[] in Alliance-first order */
    int   playable[WOW_MAX_DBC_RACES];
    int   num_playable;
} wow_charcreate;

/* -------------------------------------------------------------------------
 * DBC read helpers
 * ---------------------------------------------------------------------- */

static DWORD UIWow_DbcU32(BYTE const *rec, int field) {
    DWORD v; memcpy(&v, rec + field * 4, 4); return v;
}

static LPCSTR UIWow_DbcStr(BYTE const *strings, DWORD ssize, DWORD off) {
    if (off == 0 || off >= ssize) return "";
    return (LPCSTR)strings + off;
}

/* -------------------------------------------------------------------------
 * DBC load
 * ---------------------------------------------------------------------- */

static void UIWow_LoadCharCreateDbc(void) {
    if (wow_charcreate.loaded) return;
    wow_charcreate.loaded = true;

    DWORD size, records, fields, rsize, ssize;
    BYTE *data; BYTE const *rb, *sb;

    /* ChrRaces
       0=id, 1=flags, 2=factionID, 3=explorationSnd, 4=maleDID, 5=femaleDID,
       6=clientPrefix(str), 7=baseLang, 8=creatureType, 9=resSickSpell, 10=splashSnd,
       11=clientFileString(str), 12=cinematicSeq, 13=alliance,
       14=name(str), 15=nameFemale(str), 16=nameMale(str),
       17=facialHairCustom0(str), 18=facialHairCustom1(str), 19=hairCustom(str),
       20=requiredExpansion */
    { void *_b = NULL; size = uiimport.FS_ReadFile ? (DWORD)uiimport.FS_ReadFile("DBFilesClient\\ChrRaces.dbc", &_b) : 0; data = (BYTE*)_b; }
    if (data) {
        records = UIWow_DbcU32(data,1); fields = UIWow_DbcU32(data,2);
        rsize   = UIWow_DbcU32(data,3); ssize  = UIWow_DbcU32(data,4);
        rb = data + 20; sb = rb + records * rsize;
        wow_charcreate.races_buf = data;
        FOR_LOOP(i, (int)records) {
            if (wow_charcreate.num_races >= WOW_MAX_DBC_RACES) break;
            BYTE const *r = rb + i * rsize;
            if (fields < 20) continue;
            DWORD flags = UIWow_DbcU32(r, 1);
            if (flags & 0x1) continue; /* NPC-only */
            wowRaceRec_t *rec = &wow_charcreate.races[wow_charcreate.num_races++];
            rec->id              = UIWow_DbcU32(r, 0);
            rec->flags           = flags;
            rec->faction_id      = UIWow_DbcU32(r, 2);
            rec->male_id         = UIWow_DbcU32(r, 4);
            rec->female_id       = UIWow_DbcU32(r, 5);
            rec->client_prefix   = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r,  6));
            rec->client_file     = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 11));
            rec->name            = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 14));
            rec->name_female     = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 15));
            rec->hair_custom     = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 19));
            rec->facial_custom[0]= UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 17));
            rec->facial_custom[1]= UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 18));
            rec->required_exp    = UIWow_DbcU32(r, 20);
        }
    }

    /* ChrClasses
       0=id, 1=damageBonusStat, 2=displayPower, 3=petNameToken(str),
       4=name(str), 5=nameFemale(str), 6=nameMale(str), 7=filename(str),
       8=spellClassSet, 9=flags, 10=cinematicSeq, 11=requiredExpansion */
    { void *_b = NULL; size = uiimport.FS_ReadFile ? (DWORD)uiimport.FS_ReadFile("DBFilesClient\\ChrClasses.dbc", &_b) : 0; data = (BYTE*)_b; }
    if (data) {
        records = UIWow_DbcU32(data,1); fields = UIWow_DbcU32(data,2);
        rsize   = UIWow_DbcU32(data,3); ssize  = UIWow_DbcU32(data,4);
        rb = data + 20; sb = rb + records * rsize;
        wow_charcreate.classes_buf = data;
        FOR_LOOP(i, (int)records) {
            if (wow_charcreate.num_classes >= WOW_MAX_DBC_CLASSES) break;
            BYTE const *r = rb + i * rsize;
            if (fields < 8) continue;
            wowClassRec_t *rec = &wow_charcreate.classes[wow_charcreate.num_classes++];
            rec->id           = UIWow_DbcU32(r, 0);
            rec->name         = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 4));
            rec->filename     = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 7));
            rec->required_exp = (fields >= 12) ? UIWow_DbcU32(r, 11) : 0;
        }
    }

    /* CharBaseInfo — 2-byte records: raceID (byte), classID (byte) */
    { void *_b = NULL; size = uiimport.FS_ReadFile ? (DWORD)uiimport.FS_ReadFile("DBFilesClient\\CharBaseInfo.dbc", &_b) : 0; data = (BYTE*)_b; }
    if (data) {
        wow_charcreate.base_buf = data;
        records = UIWow_DbcU32(data,1);
        rb = data + 20;
        FOR_LOOP(i, (int)records) {
            if (wow_charcreate.num_base_info >= WOW_MAX_CHAR_BASE) break;
            BYTE const *r = rb + i * 2;
            wow_charcreate.base_info[wow_charcreate.num_base_info].race_id  = r[0];
            wow_charcreate.base_info[wow_charcreate.num_base_info].class_id = r[1];
            wow_charcreate.num_base_info++;
        }
    }

    /* FactionTemplate — 0=id, 1=faction, 2=flags, 3=factionGroup, ... */
    { void *_b = NULL; size = uiimport.FS_ReadFile ? (DWORD)uiimport.FS_ReadFile("DBFilesClient\\FactionTemplate.dbc", &_b) : 0; data = (BYTE*)_b; }
    if (data) {
        records = UIWow_DbcU32(data,1); fields = UIWow_DbcU32(data,2);
        rsize   = UIWow_DbcU32(data,3);
        rb = data + 20;
        wow_charcreate.ftpl_buf = data;
        FOR_LOOP(i, (int)records) {
            if (wow_charcreate.num_ftpl >= WOW_MAX_FACTION_TPL) break;
            BYTE const *r = rb + i * rsize;
            if (fields < 4) continue;
            wowFactionTemplateRec_t *rec = &wow_charcreate.ftpl[wow_charcreate.num_ftpl++];
            rec->id            = UIWow_DbcU32(r, 0);
            rec->faction       = UIWow_DbcU32(r, 1);
            rec->flags         = UIWow_DbcU32(r, 2);
            rec->faction_group = UIWow_DbcU32(r, 3);
        }
    }

    /* FactionGroup — 0=id, 1=maskID, 2=internalName(str), 3=name(str) */
    { void *_b = NULL; size = uiimport.FS_ReadFile ? (DWORD)uiimport.FS_ReadFile("DBFilesClient\\FactionGroup.dbc", &_b) : 0; data = (BYTE*)_b; }
    if (data) {
        records = UIWow_DbcU32(data,1); fields = UIWow_DbcU32(data,2);
        rsize   = UIWow_DbcU32(data,3); ssize  = UIWow_DbcU32(data,4);
        rb = data + 20; sb = rb + records * rsize;
        wow_charcreate.fgrp_buf = data;
        FOR_LOOP(i, (int)records) {
            if (wow_charcreate.num_fgrp >= WOW_MAX_FACTION_GRP) break;
            BYTE const *r = rb + i * rsize;
            if (fields < 4) continue;
            wowFactionGroupRec_t *rec = &wow_charcreate.fgrp[wow_charcreate.num_fgrp++];
            rec->id            = UIWow_DbcU32(r, 0);
            rec->mask_id       = UIWow_DbcU32(r, 1);
            rec->internal_name = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 2));
            rec->name          = UIWow_DbcStr(sb, ssize, UIWow_DbcU32(r, 3));
        }
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

/* -------------------------------------------------------------------------
 * Lua bindings
 * ---------------------------------------------------------------------- */

int UIWow_LuaGetAvailableRaces(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    FOR_LOOP(i, wow_charcreate.num_playable) {
        wowRaceRec_t const *r = &wow_charcreate.races[wow_charcreate.playable[i]];
        LPCSTR name = (wow_charcreate.sel_sex == 1) ? r->name : (r->name_female[0] ? r->name_female : r->name);
        lua_pushstring(L, name[0] ? name : r->client_file);
        lua_pushstring(L, r->client_file);
        lua_pushnumber(L, r->required_exp == 0 ? 1 : 0);
    }
    return wow_charcreate.num_playable * 3;
}

int UIWow_LuaGetAvailableClasses(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    FOR_LOOP(i, wow_charcreate.num_classes) {
        wowClassRec_t const *c = &wow_charcreate.classes[i];
        lua_pushstring(L, c->name[0] ? c->name : c->filename);
        lua_pushstring(L, c->filename);
        lua_pushnumber(L, c->required_exp == 0 ? 1 : 0);
    }
    return wow_charcreate.num_classes * 3;
}

int UIWow_LuaGetClassesForRace(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int pi = wow_charcreate.sel_race;
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
    if (!name) { lua_pushnil(L); lua_pushnil(L); return 2; }
    lua_pushstring(L, name);
    lua_pushstring(L, internal ? internal : "");
    return 2;
}

int UIWow_LuaGetNameForRace(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int pi = wow_charcreate.sel_race;
    if (pi < 0 || pi >= wow_charcreate.num_playable) { lua_pushnil(L); lua_pushnil(L); return 2; }
    wowRaceRec_t const *r = &wow_charcreate.races[wow_charcreate.playable[pi]];
    LPCSTR name = (wow_charcreate.sel_sex == 1) ? r->name : (r->name_female[0] ? r->name_female : r->name);
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
    int ci = wow_charcreate.sel_class - 1;
    if (ci < 0 || ci >= wow_charcreate.num_classes) return 0;
    wowClassRec_t const *c = &wow_charcreate.classes[ci];
    lua_pushstring(L, c->name[0] ? c->name : c->filename);
    lua_pushstring(L, c->filename);
    lua_pushnumber(L, wow_charcreate.sel_class);
    lua_pushboolean(L, 0); /* tank */
    lua_pushboolean(L, 0); /* healer */
    lua_pushboolean(L, 1); /* damage */
    return 6;
}

int UIWow_LuaSetSelectedRace(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int v = (int)luaL_checknumber(L, 1) - 1;
    if (v >= 0 && v < wow_charcreate.num_playable) wow_charcreate.sel_race = v;
    return 0;
}

int UIWow_LuaSetSelectedSex(lua_State *L) {
    int v = (int)luaL_checknumber(L, 1);
    if (v == 1 || v == 2) wow_charcreate.sel_sex = v;
    return 0;
}

int UIWow_LuaSetSelectedClass(lua_State *L) {
    UIWow_LoadCharCreateDbc();
    int v = (int)luaL_checknumber(L, 1);
    if (v >= 1 && v <= wow_charcreate.num_classes) wow_charcreate.sel_class = v;
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

int UIWow_LuaResetCharCustomize(lua_State *L) {
    (void)L;
    UIWow_LoadCharCreateDbc();
    wow_charcreate.sel_race  = 0;
    wow_charcreate.sel_sex   = 1;
    wow_charcreate.sel_class = 1;
    return 0;
}

int UIWow_LuaGetRandomName(lua_State *L) {
    (void)L; lua_pushstring(L, ""); return 1;
}

int UIWow_LuaCreateCharacter(lua_State *L) {
    (void)L; return 0;
}
