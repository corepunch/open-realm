/* ui_xml.c — WoW Glue FrameXML-style loader/runtime (TOC, Include/Script, frame registry, basic drawing). */
#include "ui_local.h"

#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>

/* -------------------------------------------------------------------------
 * Local float-pair types used throughout the elem struct.
 * fpoint — a screen-space position or offset (x, y).
 * fsize  — a screen-space extent (w, h).
 * ------------------------------------------------------------------------- */
typedef struct { FLOAT x, y; } fpoint_t;
typedef struct { FLOAT w, h; } fsize_t;

/* -------------------------------------------------------------------------
 * String field enum — indexes into uiWowXmlElem_t::texts[].
 * Cleanup is a single loop: for (i) free(e->texts[i]).
 * ------------------------------------------------------------------------- */
typedef enum {
    ELEM_NAME = 0,
    ELEM_PARENT_NAME,
    ELEM_RELATIVE_NAME,
    ELEM_FILE,
    ELEM_NORMAL_FILE,
    ELEM_PUSHED_FILE,
    ELEM_HIGHLIGHT_FILE,
    ELEM_TEXT,
    ELEM_POINT,
    ELEM_RELATIVE_POINT,
    ELEM_BACKDROP_BG,
    ELEM_BACKDROP_EDGE,
    ELEM_ON_CLICK,
    ELEM_ON_LOAD,
    ELEM_ON_SHOW,
    ELEM_ON_ENTER,
    ELEM_ON_LEAVE,
    ELEM_ON_ENTER_PRESSED,
    ELEM_ON_ESCAPE_PRESSED,
    ELEM_ON_TAB_PRESSED,
    ELEM_STRING_COUNT
} uiWowXmlStr_t;

typedef enum {
    ELEM_COLOR_BACKDROP = 0,
    ELEM_COLOR_BACKDROP_BORDER,
    ELEM_COLOR_TEXT,
    ELEM_COLOR_VERTEX,
    ELEM_COLOR_COUNT
} uiWowXmlColor_t;

typedef enum {
    WOW_XML_BUTTON_TEXT_NORMAL = 0,
    WOW_XML_BUTTON_TEXT_DISABLED,
    WOW_XML_BUTTON_TEXT_HIGHLIGHT,
    WOW_XML_BUTTON_TEXT_COUNT
} uiWowXmlButtonTextState_t;

typedef enum { 
    WOW_XML_FRAME, 
    WOW_XML_MODEL, 
    WOW_XML_TEXTURE, 
    WOW_XML_FONTSTRING, 
    WOW_XML_BUTTON, 
    WOW_XML_EDITBOX 
} uiWowXmlType_t;
typedef enum {
    EF_USED          = 1 << 0,
    EF_HAS_ANCHOR    = 1 << 1,
    EF_HAS_SIZE      = 1 << 2,
    EF_HAS_TEXCOORD  = 1 << 3,
    EF_HIDDEN        = 1 << 4,
    EF_VIRTUAL       = 1 << 5,
    EF_PASSWORD      = 1 << 6,
    EF_ENABLED       = 1 << 7,
    EF_CHECKED       = 1 << 8,
    EF_SET_ALL_PTS   = 1 << 9,
    EF_BACKDROP_TILE = 1 << 10,
    EF_HAS_HALIGN    = 1 << 11,
    EF_HAS_VALIGN    = 1 << 12,
    EF_FOCUSABLE     = 1 << 13,
    EF_HAS_HIGHLIGHT_TEXCOORD = 1 << 14,
    EF_HAS_BUTTON_TEXT_COLORS = 1 << 15,
} uiWowXmlElemFlag_t;

typedef struct {
    DWORD flags;
    uiWowXmlType_t type;
    int id, parent, relative_to, draw_layer;
    char *texts[ELEM_STRING_COUNT];
    fpoint_t pos, offset, text_off; /* pos(x,y), anchor offset(ox,oy), text offset(text_ox,text_oy) */
    fsize_t size, edge, tile, text_inset; /* size(w,h), border edge, tile size, text inset */
    FLOAT alpha, font_size;
    FLOAT backdrop_insets[4];
    uiFontJustificationH_t halign;
    uiFontJustificationV_t valign;
    COLOR32 colors[ELEM_COLOR_COUNT];
    COLOR32 button_text_colors[WOW_XML_BUTTON_TEXT_COUNT];
    RECT texcoord;
    RECT highlight_texcoord;
    LPMODEL model;
} uiWowXmlElem_t;

enum {
    WOW_XML_MAX_ELEMS = 2048,
    WOW_XML_LAYER_BACKGROUND = 0,
    WOW_XML_LAYER_BORDER = 1,
    WOW_XML_LAYER_ARTWORK = 2,
    WOW_XML_LAYER_OVERLAY = 3,
    WOW_XML_BACKDROP_LEFT = 0,
    WOW_XML_BACKDROP_RIGHT,
    WOW_XML_BACKDROP_TOP,
    WOW_XML_BACKDROP_BOTTOM,
    WOW_XML_NUM_BACKDROP_CORNERS = 8
};
static struct {
    uiWowXmlElem_t elems[WOW_XML_MAX_ELEMS];
    int count;
    int focus;
    int pressed_button;
    BOOL lua_ready;
} wow_xml;

/* -------------------------------------------------------------------------
 * String field helpers
 * ------------------------------------------------------------------------- */

static LPCSTR UIWow_ElemStr(uiWowXmlElem_t const *e, uiWowXmlStr_t f) {
    return (e->texts[f] && e->texts[f][0]) ? e->texts[f] : NULL;
}

/* Set a string field, freeing the previous value. */
static void UIWow_ElemSetStr(uiWowXmlElem_t *e, uiWowXmlStr_t f, LPCSTR s) {
    free(e->texts[f]);
    e->texts[f] = (s && *s) ? strdup(s) : NULL;
}

/* Append to a string field (used for script bodies). */
static void UIWow_ElemAppendStr(uiWowXmlElem_t *e, uiWowXmlStr_t f, LPCSTR s) {
    if (!s || !*s) return;
    if (!e->texts[f] || !e->texts[f][0]) {
        UIWow_ElemSetStr(e, f, s);
        return;
    }
    size_t old = strlen(e->texts[f]), add = strlen(s);
    char *buf = realloc(e->texts[f], old + add + 1);
    if (!buf) return;
    memcpy(buf + old, s, add + 1);
    e->texts[f] = buf;
}

static void UIWow_ElemFreeStrings(uiWowXmlElem_t *e) {
    FOR_LOOP(f, ELEM_STRING_COUNT) { free(e->texts[f]); e->texts[f] = NULL; }
}

/* -------------------------------------------------------------------------
 * Float helpers
 * ------------------------------------------------------------------------- */
static FLOAT UIWow_XmlFloat(xmlChar const *s, FLOAT fallback) { return s && *s ? (FLOAT)atof((char const *)s) : fallback; }
static FLOAT UIWow_XmlX(FLOAT pixels) { return pixels / 1024.0f; }
static FLOAT UIWow_XmlY(FLOAT pixels) { return pixels / 768.0f; }

static int UIWow_XmlLayer(LPCSTR level) {
    if (!level || !*level) return WOW_XML_LAYER_ARTWORK;
    if (!strcasecmp(level, "BACKGROUND")) return WOW_XML_LAYER_BACKGROUND;
    if (!strcasecmp(level, "BORDER")) return WOW_XML_LAYER_BORDER;
    if (!strcasecmp(level, "OVERLAY")) return WOW_XML_LAYER_OVERLAY;
    return WOW_XML_LAYER_ARTWORK;
}

static uiFontJustificationH_t UIWow_XmlHAlign(LPCSTR value, uiFontJustificationH_t fallback) {
    if (!value || !*value) return fallback;
    if (!strcasecmp(value, "LEFT")) return FONT_JUSTIFYLEFT;
    if (!strcasecmp(value, "RIGHT")) return FONT_JUSTIFYRIGHT;
    return FONT_JUSTIFYCENTER;
}

static uiFontJustificationV_t UIWow_XmlVAlign(LPCSTR value, uiFontJustificationV_t fallback) {
    if (!value || !*value) return fallback;
    if (!strcasecmp(value, "TOP")) return FONT_JUSTIFYTOP;
    if (!strcasecmp(value, "BOTTOM")) return FONT_JUSTIFYBOTTOM;
    return FONT_JUSTIFYMIDDLE;
}

static BOOL UIWow_XmlResolvePath(LPCSTR base_path, LPCSTR rel, LPSTR out, size_t n) {
    LPCSTR slash; size_t prefix;
    if (!rel || !*rel || !out || n == 0) return false;
    if (strchr(rel, '\\')) { snprintf(out, n, "%s", rel); return true; }
    slash = strrchr(base_path, '\\');
    if (!slash) { snprintf(out, n, "%s", rel); return true; }
    prefix = (size_t)(slash - base_path + 1);
    if (prefix + strlen(rel) + 1 > n) return false;
    memcpy(out, base_path, prefix); out[prefix] = '\0'; strncat(out, rel, n - strlen(out) - 1);
    return true;
}

static int UIWow_XmlFindByName(LPCSTR name) {
    if (!name || !*name) return -1;
    FOR_LOOP(i, wow_xml.count) {
        if ((wow_xml.elems[i].flags & EF_USED) && wow_xml.elems[i].texts[ELEM_NAME] &&
            !strcmp(wow_xml.elems[i].texts[ELEM_NAME], name)) return i;
    }
    return -1;
}

static int UIWow_XmlPushElem(uiWowXmlType_t type, LPCSTR name, int parent, int draw_layer) {
    uiWowXmlElem_t *e;
    if (wow_xml.count >= WOW_XML_MAX_ELEMS) return -1;
    e = &wow_xml.elems[wow_xml.count]; memset(e, 0, sizeof(*e));
    e->flags = EF_USED | EF_ENABLED;
    e->type = type; e->parent = parent; e->relative_to = parent; e->draw_layer = draw_layer;
    e->alpha = 1.0f;
    e->font_size = 14.0f; e->colors[ELEM_COLOR_TEXT] = COLOR32_WHITE; e->colors[ELEM_COLOR_VERTEX] = COLOR32_WHITE;
    e->halign = FONT_JUSTIFYCENTER; e->valign = FONT_JUSTIFYMIDDLE;
    e->colors[ELEM_COLOR_BACKDROP] = MAKE(COLOR32, 23, 23, 23, 120); e->colors[ELEM_COLOR_BACKDROP_BORDER] = MAKE(COLOR32, 204, 204, 204, 255);
    e->button_text_colors[WOW_XML_BUTTON_TEXT_NORMAL] = e->colors[ELEM_COLOR_TEXT];
    e->button_text_colors[WOW_XML_BUTTON_TEXT_DISABLED] = e->colors[ELEM_COLOR_TEXT];
    e->button_text_colors[WOW_XML_BUTTON_TEXT_HIGHLIGHT] = e->colors[ELEM_COLOR_TEXT];
    e->texcoord = MAKE(RECT, 0, 0, 1, 1);
    e->highlight_texcoord = MAKE(RECT, 0, 0, 1, 1);
    UIWow_ElemSetStr(e, ELEM_NAME, name);
    return wow_xml.count++;
}

static void UIWow_XmlInheritElem(uiWowXmlElem_t *e, LPCSTR inherits) {
    char names[256], *tok, *save = NULL;
    if (!e || !inherits || !*inherits) return;
    snprintf(names, sizeof(names), "%s", inherits);
    for (tok = strtok_r(names, " ,", &save); tok; tok = strtok_r(NULL, " ,", &save)) {
        int idx = UIWow_XmlFindByName(tok);
        if (idx < 0) continue;
        uiWowXmlElem_t const *src = &wow_xml.elems[idx];
        if (!(e->flags & EF_HAS_SIZE) && (src->flags & EF_HAS_SIZE)) { e->size = src->size; e->flags |= EF_HAS_SIZE; }
        if (!UIWow_ElemStr(e, ELEM_FILE) && UIWow_ElemStr(src, ELEM_FILE))
            UIWow_ElemSetStr(e, ELEM_FILE, src->texts[ELEM_FILE]);
        if (!UIWow_ElemStr(e, ELEM_NORMAL_FILE) && UIWow_ElemStr(src, ELEM_NORMAL_FILE))
            UIWow_ElemSetStr(e, ELEM_NORMAL_FILE, src->texts[ELEM_NORMAL_FILE]);
        if (!UIWow_ElemStr(e, ELEM_PUSHED_FILE) && UIWow_ElemStr(src, ELEM_PUSHED_FILE))
            UIWow_ElemSetStr(e, ELEM_PUSHED_FILE, src->texts[ELEM_PUSHED_FILE]);
        if (!UIWow_ElemStr(e, ELEM_HIGHLIGHT_FILE) && UIWow_ElemStr(src, ELEM_HIGHLIGHT_FILE))
            UIWow_ElemSetStr(e, ELEM_HIGHLIGHT_FILE, src->texts[ELEM_HIGHLIGHT_FILE]);
        if (!UIWow_ElemStr(e, ELEM_TEXT) && UIWow_ElemStr(src, ELEM_TEXT))
            UIWow_ElemSetStr(e, ELEM_TEXT, src->texts[ELEM_TEXT]);
        if (src->flags & EF_HIDDEN) e->flags |= EF_HIDDEN;
        if (!UIWow_ElemStr(e, ELEM_BACKDROP_BG) && UIWow_ElemStr(src, ELEM_BACKDROP_BG))
            UIWow_ElemSetStr(e, ELEM_BACKDROP_BG, src->texts[ELEM_BACKDROP_BG]);
        if (!UIWow_ElemStr(e, ELEM_BACKDROP_EDGE) && UIWow_ElemStr(src, ELEM_BACKDROP_EDGE))
            UIWow_ElemSetStr(e, ELEM_BACKDROP_EDGE, src->texts[ELEM_BACKDROP_EDGE]);
        if (src->flags & EF_HAS_TEXCOORD) { e->texcoord = src->texcoord; e->flags |= EF_HAS_TEXCOORD; }
        if (src->flags & EF_HAS_HIGHLIGHT_TEXCOORD) {
            e->highlight_texcoord = src->highlight_texcoord;
            e->flags |= EF_HAS_HIGHLIGHT_TEXCOORD;
        }
        if (src->edge.w > 0.0f) e->edge = src->edge;
        if (src->tile.w > 0.0f) e->tile = src->tile;
        if (src->flags & EF_BACKDROP_TILE) e->flags |= EF_BACKDROP_TILE;
        memcpy(e->backdrop_insets, src->backdrop_insets, sizeof(e->backdrop_insets));
        if (src->font_size > 0.0f) e->font_size = src->font_size;
        if (src->text_off.x != 0.0f || src->text_off.y != 0.0f) e->text_off = src->text_off;
        if (src->flags & EF_HAS_HALIGN) { e->halign = src->halign; e->flags |= EF_HAS_HALIGN; }
        if (src->flags & EF_HAS_VALIGN) { e->valign = src->valign; e->flags |= EF_HAS_VALIGN; }
        e->colors[ELEM_COLOR_TEXT] = src->colors[ELEM_COLOR_TEXT];
        if (src->flags & EF_HAS_BUTTON_TEXT_COLORS) {
            e->button_text_colors[WOW_XML_BUTTON_TEXT_NORMAL] = src->button_text_colors[WOW_XML_BUTTON_TEXT_NORMAL];
            e->button_text_colors[WOW_XML_BUTTON_TEXT_DISABLED] = src->button_text_colors[WOW_XML_BUTTON_TEXT_DISABLED];
            e->button_text_colors[WOW_XML_BUTTON_TEXT_HIGHLIGHT] = src->button_text_colors[WOW_XML_BUTTON_TEXT_HIGHLIGHT];
            e->flags |= EF_HAS_BUTTON_TEXT_COLORS;
        }
    }
}

static void UIWow_XmlRectPoint(LPCRECT r, LPCSTR point, LPFLOAT x, LPFLOAT y) {
    if (!strcasecmp(point, "TOPLEFT")) { *x = r->x; *y = r->y; return; }
    if (!strcasecmp(point, "TOP")) { *x = r->x + r->w * 0.5f; *y = r->y; return; }
    if (!strcasecmp(point, "TOPRIGHT")) { *x = r->x + r->w; *y = r->y; return; }
    if (!strcasecmp(point, "LEFT")) { *x = r->x; *y = r->y + r->h * 0.5f; return; }
    if (!strcasecmp(point, "RIGHT")) { *x = r->x + r->w; *y = r->y + r->h * 0.5f; return; }
    if (!strcasecmp(point, "BOTTOMLEFT")) { *x = r->x; *y = r->y + r->h; return; }
    if (!strcasecmp(point, "BOTTOM")) { *x = r->x + r->w * 0.5f; *y = r->y + r->h; return; }
    if (!strcasecmp(point, "BOTTOMRIGHT")) { *x = r->x + r->w; *y = r->y + r->h; return; }
    *x = r->x + r->w * 0.5f; *y = r->y + r->h * 0.5f;
}

static RECT UIWow_XmlComputeRect(int idx) {
    uiWowXmlElem_t const *e = &wow_xml.elems[idx]; RECT parent = MAKE(RECT, 0, 0, 1, 1);
    LPCSTR point = e->texts[ELEM_POINT];
    LPCSTR rel_point = e->texts[ELEM_RELATIVE_POINT];
    FLOAT default_h = e->type == WOW_XML_FONTSTRING ? UIWow_XmlY(e->font_size > 0.0f ? e->font_size : 14.0f) : 0.05f;
    RECT out = MAKE(RECT, 0, 0, e->size.w > 0 ? e->size.w : 0.2f, e->size.h > 0 ? e->size.h : default_h);
    FLOAT ax, ay;
    if (e->parent >= 0 && e->parent < wow_xml.count) parent = UIWow_XmlComputeRect(e->parent);
    if (e->flags & EF_SET_ALL_PTS) return parent;
    if (!(e->flags & EF_HAS_ANCHOR)) {
        out.x = parent.x; out.y = parent.y; return out;
    }
    if (e->relative_to >= 0 && e->relative_to < wow_xml.count) parent = UIWow_XmlComputeRect(e->relative_to);
    UIWow_XmlRectPoint(&parent, (rel_point && rel_point[0]) ? rel_point : (point ? point : "CENTER"), &ax, &ay);
    ax += e->offset.x; ay += e->offset.y;
    if (!point) point = "CENTER";
    if (!strcasecmp(point, "TOPLEFT")) out.x = ax, out.y = ay;
    else if (!strcasecmp(point, "TOP")) out.x = ax - out.w * 0.5f, out.y = ay;
    else if (!strcasecmp(point, "TOPRIGHT")) out.x = ax - out.w, out.y = ay;
    else if (!strcasecmp(point, "LEFT")) out.x = ax, out.y = ay - out.h * 0.5f;
    else if (!strcasecmp(point, "CENTER")) out.x = ax - out.w * 0.5f, out.y = ay - out.h * 0.5f;
    else if (!strcasecmp(point, "RIGHT")) out.x = ax - out.w, out.y = ay - out.h * 0.5f;
    else if (!strcasecmp(point, "BOTTOMLEFT")) out.x = ax, out.y = ay - out.h;
    else if (!strcasecmp(point, "BOTTOM")) out.x = ax - out.w * 0.5f, out.y = ay - out.h;
    else if (!strcasecmp(point, "BOTTOMRIGHT")) out.x = ax - out.w, out.y = ay - out.h;
    else out.x = ax - out.w * 0.5f, out.y = ay - out.h * 0.5f;
    return out;
}

static int UIWow_FrameFromSelf(lua_State *L) {
    int idx;
    luaL_checktype(L, 1, LUA_TTABLE); lua_getfield(L, 1, "__ow3_index"); idx = (int)luaL_optinteger(L, -1, -1); lua_pop(L, 1);
    return idx >= 0 && idx < wow_xml.count && (wow_xml.elems[idx].flags & EF_USED) ? idx : -1;
}

static void UIWow_XmlPublishFrame(int idx);
static void UIWow_XMLRunFrameScript(int idx, LPCSTR script, LPCSTR event_name);

static void UIWow_XMLSetShown(int idx, BOOL shown) {
    if (idx < 0 || idx >= wow_xml.count) return;
    if (shown) {
        BOOL was_hidden = (wow_xml.elems[idx].flags & EF_HIDDEN) != 0;
        wow_xml.elems[idx].flags &= ~EF_HIDDEN;
        if (was_hidden && UIWow_ElemStr(&wow_xml.elems[idx], ELEM_ON_SHOW))
            UIWow_XMLRunFrameScript(idx, wow_xml.elems[idx].texts[ELEM_ON_SHOW], "OnShow");
    } else {
        wow_xml.elems[idx].flags |= EF_HIDDEN;
    }
}

static int UIWow_LuaFrameShow(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) UIWow_XMLSetShown(i, true);
    return 0;
}
static int UIWow_LuaFrameHide(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) UIWow_XMLSetShown(i, false);
    return 0;
}
static int UIWow_LuaFrameIsVisible(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushboolean(L, i >= 0 && !(wow_xml.elems[i].flags & EF_HIDDEN)); return 1; }
static int UIWow_LuaFrameSetAlpha(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].alpha = (FLOAT)luaL_optnumber(L, 2, 1.0); return 0; }
static int UIWow_LuaFrameSetText(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) UIWow_ElemSetStr(&wow_xml.elems[i], ELEM_TEXT, luaL_optstring(L, 2, "")); return 0; }
static int UIWow_LuaFrameGetText(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushstring(L, i >= 0 && wow_xml.elems[i].texts[ELEM_TEXT] ? wow_xml.elems[i].texts[ELEM_TEXT] : ""); return 1; }
static int UIWow_LuaFrameGetName(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushstring(L, i >= 0 && wow_xml.elems[i].texts[ELEM_NAME] ? wow_xml.elems[i].texts[ELEM_NAME] : ""); return 1; }
static int UIWow_LuaFrameGetParent(lua_State *L) {
    int i = UIWow_FrameFromSelf(L), p = i >= 0 ? wow_xml.elems[i].parent : -1;
    if (p >= 0) UIWow_XmlPublishFrame(p); else lua_pushnil(L);
    return p >= 0 ? (lua_getglobal(L, wow_xml.elems[p].texts[ELEM_NAME] ? wow_xml.elems[p].texts[ELEM_NAME] : ""), 1) : 1;
}
static int UIWow_LuaFrameSetHeight(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) { wow_xml.elems[i].size.h = UIWow_XmlY((FLOAT)luaL_checknumber(L, 2)); wow_xml.elems[i].flags |= EF_HAS_SIZE; }
    return 0;
}

static int UIWow_LuaFrameSetWidth(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) { wow_xml.elems[i].size.w = UIWow_XmlX((FLOAT)luaL_checknumber(L, 2)); wow_xml.elems[i].flags |= EF_HAS_SIZE; }
    return 0;
}

static int UIWow_LuaFrameGetHeight(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    RECT r = i >= 0 ? UIWow_XmlComputeRect(i) : MAKE(RECT, 0, 0, 0, 0);
    lua_pushnumber(L, r.h * 768.0f);
    return 1;
}

static int UIWow_LuaFrameGetWidth(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    RECT r = i >= 0 ? UIWow_XmlComputeRect(i) : MAKE(RECT, 0, 0, 0, 0);
    lua_pushnumber(L, r.w * 1024.0f);
    return 1;
}
static int UIWow_LuaFrameSetID(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].id = (int)luaL_checkinteger(L, 2); return 0; }
static int UIWow_LuaFrameEnable(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].flags |= EF_ENABLED; return 0; }
static int UIWow_LuaFrameDisable(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].flags &= ~EF_ENABLED; return 0; }
static int UIWow_LuaFrameIsEnabled(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushboolean(L, i >= 0 && (wow_xml.elems[i].flags & EF_ENABLED)); return 1; }
static int UIWow_LuaFrameSetChecked(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) { if (lua_toboolean(L, 2)) wow_xml.elems[i].flags |= EF_CHECKED; else wow_xml.elems[i].flags &= ~EF_CHECKED; }
    return 0;
}

static int UIWow_LuaFrameGetChecked(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    lua_pushboolean(L, i >= 0 && (wow_xml.elems[i].flags & EF_CHECKED));
    return 1;
}

static int UIWow_LuaFrameGetID(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    lua_pushinteger(L, i >= 0 ? wow_xml.elems[i].id : 0);
    return 1;
}
static int UIWow_LuaFrameNoop(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameGetButtonState(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    lua_pushstring(L, (i >= 0 && wow_xml.pressed_button == i) ? "PUSHED" : "NORMAL");
    return 1;
}
static int UIWow_LuaFrameSetVertexColor(lua_State *L) {
    int i = UIWow_FrameFromSelf(L); FLOAT r = (FLOAT)luaL_optnumber(L, 2, 1.0), g = (FLOAT)luaL_optnumber(L, 3, 1.0), b = (FLOAT)luaL_optnumber(L, 4, 1.0), a = (FLOAT)luaL_optnumber(L, 5, 1.0);
    if (i >= 0) wow_xml.elems[i].colors[ELEM_COLOR_VERTEX] = MAKE(COLOR32, (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f), (BYTE)(a * 255.0f));
    return 0;
}
static int UIWow_LuaFrameSetBackdropColor(lua_State *L) {
    int i = UIWow_FrameFromSelf(L); FLOAT r = (FLOAT)luaL_optnumber(L, 2, 0.09), g = (FLOAT)luaL_optnumber(L, 3, 0.09), b = (FLOAT)luaL_optnumber(L, 4, 0.09), a = (FLOAT)luaL_optnumber(L, 5, 0.5);
    if (i >= 0) wow_xml.elems[i].colors[ELEM_COLOR_BACKDROP] = MAKE(COLOR32, (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f), (BYTE)(a * 255.0f));
    return 0;
}
static int UIWow_LuaFrameSetBackdropBorderColor(lua_State *L) {
    int i = UIWow_FrameFromSelf(L); FLOAT r = (FLOAT)luaL_optnumber(L, 2, 0.8), g = (FLOAT)luaL_optnumber(L, 3, 0.8), b = (FLOAT)luaL_optnumber(L, 4, 0.8), a = (FLOAT)luaL_optnumber(L, 5, 1.0);
    if (i >= 0) wow_xml.elems[i].colors[ELEM_COLOR_BACKDROP_BORDER] = MAKE(COLOR32, (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f), (BYTE)(a * 255.0f));
    return 0;
}
static int UIWow_LuaFrameSetFocus(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.focus = i; return 0; }
static int UIWow_LuaFrameHighlightText(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameRegisterEvent(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetSequence(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetCamera(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetModel(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) {
        UIWow_ElemSetStr(&wow_xml.elems[i], ELEM_FILE, luaL_optstring(L, 2, ""));
        if (wow_xml.elems[i].model && wow_ui.renderer && wow_ui.renderer->ReleaseModel) {
            wow_ui.renderer->ReleaseModel(wow_xml.elems[i].model);
            wow_xml.elems[i].model = NULL;
        }
    }
    return 0;
}
static int UIWow_LuaFrameAdvanceTime(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetFogColor(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetFogNear(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetFogFar(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameClearFog(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaGetGlobalCompat(lua_State *L) { lua_getglobal(L, luaL_checkstring(L, 1)); return 1; }

static int UIWow_LuaFrameClick(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0 && UIWow_ElemStr(&wow_xml.elems[i], ELEM_ON_CLICK))
        UIWow_XMLRunFrameScript(i, wow_xml.elems[i].texts[ELEM_ON_CLICK], "OnClick");
    return 0;
}

static int UIWow_LuaSetGlueScreen(lua_State *L) {
    LPCSTR screen = luaL_checkstring(L, 1);
    int target = -1;

    lua_getglobal(L, "GlueScreenInfo");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        LPCSTR key = lua_tostring(L, -2), frame_name = lua_tostring(L, -1);
        int idx = UIWow_XmlFindByName(frame_name);
        if (idx >= 0) {
            UIWow_XMLSetShown(idx, false);
            if (key && !strcmp(key, screen)) target = idx;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    if (target >= 0) UIWow_XMLSetShown(target, true);
    lua_pushstring(L, screen);
    lua_setglobal(L, "CURRENT_GLUE_SCREEN");
    return 0;
}

static void UIWow_XMLInstallScreenShim(void) {
    if (!wow_ui.lua) return;
    lua_getglobal(wow_ui.lua, "GlueScreenInfo");
    if (!lua_istable(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        return;
    }
    lua_pop(wow_ui.lua, 1);
    lua_pushcfunction(wow_ui.lua, UIWow_LuaSetGlueScreen);
    lua_setglobal(wow_ui.lua, "SetGlueScreen");
}

static void UIWow_XMLInstallLuaCompat(void) {
    static luaL_Reg const methods[] = {
        { "Show", UIWow_LuaFrameShow }, { "Hide", UIWow_LuaFrameHide }, { "IsVisible", UIWow_LuaFrameIsVisible }, { "SetAlpha", UIWow_LuaFrameSetAlpha },
        { "SetText", UIWow_LuaFrameSetText }, { "GetText", UIWow_LuaFrameGetText }, { "SetBackdropColor", UIWow_LuaFrameSetBackdropColor }, { "SetBackdropBorderColor", UIWow_LuaFrameSetBackdropBorderColor },
        { "GetName", UIWow_LuaFrameGetName }, { "GetParent", UIWow_LuaFrameGetParent }, { "SetID", UIWow_LuaFrameSetID },
        { "SetHeight", UIWow_LuaFrameSetHeight }, { "SetWidth", UIWow_LuaFrameSetWidth },
        { "GetHeight", UIWow_LuaFrameGetHeight }, { "GetWidth", UIWow_LuaFrameGetWidth },
        { "Enable", UIWow_LuaFrameEnable }, { "Disable", UIWow_LuaFrameDisable },
        { "IsEnabled", UIWow_LuaFrameIsEnabled }, { "SetChecked", UIWow_LuaFrameSetChecked },
        { "GetChecked", UIWow_LuaFrameGetChecked }, { "GetID", UIWow_LuaFrameGetID },
        { "Click", UIWow_LuaFrameClick },
        { "LockHighlight", UIWow_LuaFrameNoop }, { "UnlockHighlight", UIWow_LuaFrameNoop },
        { "GetButtonState", UIWow_LuaFrameGetButtonState }, { "IsShown", UIWow_LuaFrameIsVisible },
        { "GetFrameLevel", UIWow_LuaFrameGetID }, { "SetFrameLevel", UIWow_LuaFrameNoop },
        { "SetPoint", UIWow_LuaFrameNoop }, { "ClearAllPoints", UIWow_LuaFrameNoop },
        { "Raise", UIWow_LuaFrameNoop }, { "Lower", UIWow_LuaFrameNoop },
        { "GetTextWidth", UIWow_LuaFrameGetWidth }, { "GetTextHeight", UIWow_LuaFrameGetHeight },
        { "SetVertexColor", UIWow_LuaFrameSetVertexColor }, { "SetFocus", UIWow_LuaFrameSetFocus }, { "HighlightText", UIWow_LuaFrameHighlightText }, { "RegisterEvent", UIWow_LuaFrameRegisterEvent }, { "SetSequence", UIWow_LuaFrameSetSequence },
        { "SetCamera", UIWow_LuaFrameSetCamera }, { "SetModel", UIWow_LuaFrameSetModel }, { "AdvanceTime", UIWow_LuaFrameAdvanceTime },
        { "SetFogColor", UIWow_LuaFrameSetFogColor }, { "SetFogNear", UIWow_LuaFrameSetFogNear }, { "SetFogFar", UIWow_LuaFrameSetFogFar },
        { "ClearFog", UIWow_LuaFrameClearFog }, { NULL, NULL }
    };
    if (!wow_ui.lua) return;
    if (luaL_newmetatable(wow_ui.lua, "UIWow.Frame")) { lua_pushvalue(wow_ui.lua, -1); lua_setfield(wow_ui.lua, -2, "__index"); luaL_setfuncs(wow_ui.lua, methods, 0); }
    lua_pop(wow_ui.lua, 1); lua_pushcfunction(wow_ui.lua, UIWow_LuaGetGlobalCompat); lua_setglobal(wow_ui.lua, "getglobal");
    wow_xml.lua_ready = true;
}

static void UIWow_XmlPublishFrame(int idx) {
    uiWowXmlElem_t const *e = &wow_xml.elems[idx];
    LPCSTR name = e->texts[ELEM_NAME];
    if (!wow_ui.lua || !name || !name[0]) return;
    lua_getglobal(wow_ui.lua, name);
    if (lua_istable(wow_ui.lua, -1)) { lua_pop(wow_ui.lua, 1); return; }
    lua_pop(wow_ui.lua, 1);
    lua_newtable(wow_ui.lua);
    lua_pushinteger(wow_ui.lua, idx); lua_setfield(wow_ui.lua, -2, "__ow3_index");
    lua_pushstring(wow_ui.lua, name); lua_setfield(wow_ui.lua, -2, "name");
    lua_pushboolean(wow_ui.lua, !(e->flags & EF_HIDDEN)); lua_setfield(wow_ui.lua, -2, "shown");
    luaL_getmetatable(wow_ui.lua, "UIWow.Frame"); lua_setmetatable(wow_ui.lua, -2);
    lua_setglobal(wow_ui.lua, name);
}

static void UIWow_XmlReadSize(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        xmlChar *x, *y;
        xmlNodePtr d;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Size")) continue;
        for (d = c->children; d; d = d->next) {
            if (d->type != XML_ELEMENT_NODE || xmlStrcasecmp(d->name, BAD_CAST "AbsDimension")) continue;
            x = xmlGetProp(d, BAD_CAST "x"); y = xmlGetProp(d, BAD_CAST "y");
            e->size.w = UIWow_XmlX(UIWow_XmlFloat(x, 0.0f));
            e->size.h = UIWow_XmlY(UIWow_XmlFloat(y, 0.0f));
            if (e->size.w > 0 || e->size.h > 0) e->flags |= EF_HAS_SIZE;
            SAFE_DELETE(x, xmlFree); SAFE_DELETE(y, xmlFree); return;
        }
    }
}

static void UIWow_XmlReadAnchor(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        xmlNodePtr a;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Anchors")) continue;
        for (a = c->children; a; a = a->next) {
            xmlChar *point, *relative, *relative_to;
            if (a->type != XML_ELEMENT_NODE || xmlStrcasecmp(a->name, BAD_CAST "Anchor")) continue;
            point = xmlGetProp(a, BAD_CAST "point"); relative = xmlGetProp(a, BAD_CAST "relativePoint"); relative_to = xmlGetProp(a, BAD_CAST "relativeTo");
            UIWow_ElemSetStr(e, ELEM_POINT, point && *point ? (char const *)point : "CENTER");
            UIWow_ElemSetStr(e, ELEM_RELATIVE_POINT, relative && *relative ? (char const *)relative : e->texts[ELEM_POINT]);
            if (relative_to && *relative_to) {
                UIWow_ElemSetStr(e, ELEM_RELATIVE_NAME, (char const *)relative_to);
                e->relative_to = UIWow_XmlFindByName(e->texts[ELEM_RELATIVE_NAME]);
            }
            SAFE_DELETE(point, xmlFree); SAFE_DELETE(relative, xmlFree); SAFE_DELETE(relative_to, xmlFree);
            {
                xmlNodePtr off;
                for (off = a->children; off; off = off->next) {
                    xmlNodePtr abs;
                    if (off->type != XML_ELEMENT_NODE || xmlStrcasecmp(off->name, BAD_CAST "Offset")) continue;
                    for (abs = off->children; abs; abs = abs->next) {
                        xmlChar *x, *y;
                        if (abs->type != XML_ELEMENT_NODE || xmlStrcasecmp(abs->name, BAD_CAST "AbsDimension")) continue;
                        x = xmlGetProp(abs, BAD_CAST "x"); y = xmlGetProp(abs, BAD_CAST "y");
                        e->offset.x = UIWow_XmlX(UIWow_XmlFloat(x, 0.0f));
                        e->offset.y = -UIWow_XmlY(UIWow_XmlFloat(y, 0.0f));
                        SAFE_DELETE(x, xmlFree); SAFE_DELETE(y, xmlFree);
                    }
                }
            }
            e->flags |= EF_HAS_ANCHOR; return;
        }
    }
}

static void UIWow_XmlReadBackdrop(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        xmlChar *bg, *edge, *tile;
        xmlNodePtr d;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Backdrop")) continue;
        bg = xmlGetProp(c, BAD_CAST "bgFile");
        edge = xmlGetProp(c, BAD_CAST "edgeFile");
        tile = xmlGetProp(c, BAD_CAST "tile");
        if (bg && *bg) UIWow_ElemSetStr(e, ELEM_BACKDROP_BG, (char const *)bg);
        if (edge && *edge) UIWow_ElemSetStr(e, ELEM_BACKDROP_EDGE, (char const *)edge);
        if (tile && *tile && !strcasecmp((char const *)tile, "true")) e->flags |= EF_BACKDROP_TILE;
        SAFE_DELETE(bg, xmlFree); SAFE_DELETE(edge, xmlFree); SAFE_DELETE(tile, xmlFree);
        for (d = c->children; d; d = d->next) if (d->type == XML_ELEMENT_NODE && !xmlStrcasecmp(d->name, BAD_CAST "EdgeSize")) {
            xmlNodePtr v;
            for (v = d->children; v; v = v->next) {
                if (v->type != XML_ELEMENT_NODE || xmlStrcasecmp(v->name, BAD_CAST "AbsValue")) continue;
                xmlChar *val = xmlGetProp(v, BAD_CAST "val"); FLOAT px = UIWow_XmlFloat(val, 16.0f);
                e->edge.w = UIWow_XmlX(px); e->edge.h = UIWow_XmlY(px); SAFE_DELETE(val, xmlFree);
            }
        } else if (d->type == XML_ELEMENT_NODE && !xmlStrcasecmp(d->name, BAD_CAST "TileSize")) {
            xmlNodePtr v;
            for (v = d->children; v; v = v->next) {
                if (v->type != XML_ELEMENT_NODE || xmlStrcasecmp(v->name, BAD_CAST "AbsValue")) continue;
                xmlChar *val = xmlGetProp(v, BAD_CAST "val"); FLOAT px = UIWow_XmlFloat(val, 16.0f);
                e->tile.w = UIWow_XmlX(px); e->tile.h = UIWow_XmlY(px); SAFE_DELETE(val, xmlFree);
            }
        } else if (d->type == XML_ELEMENT_NODE && !xmlStrcasecmp(d->name, BAD_CAST "BackgroundInsets")) {
            xmlNodePtr v;
            for (v = d->children; v; v = v->next) {
                if (v->type != XML_ELEMENT_NODE || xmlStrcasecmp(v->name, BAD_CAST "AbsInset")) continue;
                xmlChar *l = xmlGetProp(v, BAD_CAST "left"), *r = xmlGetProp(v, BAD_CAST "right");
                xmlChar *t = xmlGetProp(v, BAD_CAST "top"), *b = xmlGetProp(v, BAD_CAST "bottom");
                e->backdrop_insets[WOW_XML_BACKDROP_LEFT] = UIWow_XmlX(UIWow_XmlFloat(l, 0.0f));
                e->backdrop_insets[WOW_XML_BACKDROP_RIGHT] = UIWow_XmlX(UIWow_XmlFloat(r, 0.0f));
                e->backdrop_insets[WOW_XML_BACKDROP_TOP] = UIWow_XmlY(UIWow_XmlFloat(t, 0.0f));
                e->backdrop_insets[WOW_XML_BACKDROP_BOTTOM] = UIWow_XmlY(UIWow_XmlFloat(b, 0.0f));
                SAFE_DELETE(l, xmlFree); SAFE_DELETE(r, xmlFree); SAFE_DELETE(t, xmlFree); SAFE_DELETE(b, xmlFree);
            }
        }
        return;
    }
}

static void UIWow_XmlReadTexCoords(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        xmlChar *l, *r, *t, *b;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "TexCoords")) continue;
        l = xmlGetProp(c, BAD_CAST "left"); r = xmlGetProp(c, BAD_CAST "right");
        t = xmlGetProp(c, BAD_CAST "top"); b = xmlGetProp(c, BAD_CAST "bottom");
        e->texcoord.x = UIWow_XmlFloat(l, 0.0f); e->texcoord.y = UIWow_XmlFloat(t, 0.0f);
        e->texcoord.w = UIWow_XmlFloat(r, 1.0f) - e->texcoord.x;
        e->texcoord.h = UIWow_XmlFloat(b, 1.0f) - e->texcoord.y;
        e->flags |= EF_HAS_TEXCOORD;
        SAFE_DELETE(l, xmlFree); SAFE_DELETE(r, xmlFree); SAFE_DELETE(t, xmlFree); SAFE_DELETE(b, xmlFree);
        return;
    }
}

static void UIWow_XmlReadFont(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcasecmp(c->name, BAD_CAST "FontHeight")) {
            xmlNodePtr v; for (v = c->children; v; v = v->next) if (v->type == XML_ELEMENT_NODE && !xmlStrcasecmp(v->name, BAD_CAST "AbsValue")) {
                xmlChar *val = xmlGetProp(v, BAD_CAST "val"); e->font_size = UIWow_XmlFloat(val, e->font_size); SAFE_DELETE(val, xmlFree);
            }
        } else if (!xmlStrcasecmp(c->name, BAD_CAST "Color")) {
            xmlChar *r = xmlGetProp(c, BAD_CAST "r"), *g = xmlGetProp(c, BAD_CAST "g"), *b = xmlGetProp(c, BAD_CAST "b"), *a = xmlGetProp(c, BAD_CAST "a");
            e->colors[ELEM_COLOR_TEXT] = MAKE(COLOR32, (BYTE)(UIWow_XmlFloat(r, 1.0f) * 255.0f), (BYTE)(UIWow_XmlFloat(g, 1.0f) * 255.0f), (BYTE)(UIWow_XmlFloat(b, 1.0f) * 255.0f), (BYTE)(UIWow_XmlFloat(a, 1.0f) * 255.0f));
            SAFE_DELETE(r, xmlFree); SAFE_DELETE(g, xmlFree); SAFE_DELETE(b, xmlFree); SAFE_DELETE(a, xmlFree);
        }
    }
}

static void UIWow_XmlReadJustify(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlChar *h = xmlGetProp(node, BAD_CAST "justifyH"), *v = xmlGetProp(node, BAD_CAST "justifyV");
    if (h && *h) { e->halign = UIWow_XmlHAlign((char const *)h, e->halign); e->flags |= EF_HAS_HALIGN; }
    if (v && *v) { e->valign = UIWow_XmlVAlign((char const *)v, e->valign); e->flags |= EF_HAS_VALIGN; }
    SAFE_DELETE(h, xmlFree); SAFE_DELETE(v, xmlFree);
}

static void UIWow_XmlReadTextInsets(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        xmlNodePtr a;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "TextInsets")) continue;
        for (a = c->children; a; a = a->next) if (a->type == XML_ELEMENT_NODE && !xmlStrcasecmp(a->name, BAD_CAST "AbsInset")) {
            xmlChar *left = xmlGetProp(a, BAD_CAST "left"), *bottom = xmlGetProp(a, BAD_CAST "bottom");
            e->text_inset.w = UIWow_XmlFloat(left, e->text_inset.w) / 1024.0f;
            e->text_inset.h = UIWow_XmlFloat(bottom, e->text_inset.h) / 768.0f;
            SAFE_DELETE(left, xmlFree); SAFE_DELETE(bottom, xmlFree);
        }
    }
}

static void UIWow_XmlReadButtonPart(uiWowXmlElem_t *e, xmlNodePtr child) {
    xmlChar *file = xmlGetProp(child, BAD_CAST "file"), *inherits = xmlGetProp(child, BAD_CAST "inherits");
    uiWowXmlElem_t temp;
    memset(&temp, 0, sizeof(temp));
    temp.texcoord = MAKE(RECT, 0, 0, 1, 1);
    UIWow_XmlInheritElem(&temp, (char const *)inherits);
    if (file && *file) UIWow_ElemSetStr(&temp, ELEM_FILE, (char const *)file);
    UIWow_XmlReadTexCoords(&temp, child);
    if (!xmlStrcasecmp(child->name, BAD_CAST "NormalTexture") && UIWow_ElemStr(&temp, ELEM_FILE)) {
        UIWow_ElemSetStr(e, ELEM_NORMAL_FILE, temp.texts[ELEM_FILE]);
        if (temp.flags & EF_HAS_TEXCOORD) { e->texcoord = temp.texcoord; e->flags |= EF_HAS_TEXCOORD; }
    } else if (!xmlStrcasecmp(child->name, BAD_CAST "PushedTexture") && UIWow_ElemStr(&temp, ELEM_FILE)) {
        UIWow_ElemSetStr(e, ELEM_PUSHED_FILE, temp.texts[ELEM_FILE]);
    } else if (!xmlStrcasecmp(child->name, BAD_CAST "HighlightTexture") && UIWow_ElemStr(&temp, ELEM_FILE)) {
        UIWow_ElemSetStr(e, ELEM_HIGHLIGHT_FILE, temp.texts[ELEM_FILE]);
        if (temp.flags & EF_HAS_TEXCOORD) {
            e->highlight_texcoord = temp.texcoord;
            e->flags |= EF_HAS_HIGHLIGHT_TEXCOORD;
        }
    }
    SAFE_DELETE(file, xmlFree); SAFE_DELETE(inherits, xmlFree);
    UIWow_ElemFreeStrings(&temp);
}

static void UIWow_XmlReadButton(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcasecmp(c->name, BAD_CAST "NormalTexture") || !xmlStrcasecmp(c->name, BAD_CAST "PushedTexture") ||
            !xmlStrcasecmp(c->name, BAD_CAST "HighlightTexture")) UIWow_XmlReadButtonPart(e, c);
        else if (!xmlStrcasecmp(c->name, BAD_CAST "NormalText") || !xmlStrcasecmp(c->name, BAD_CAST "HighlightText") || !xmlStrcasecmp(c->name, BAD_CAST "DisabledText")) {
            uiWowXmlElem_t temp;
            uiWowXmlButtonTextState_t text_state = WOW_XML_BUTTON_TEXT_NORMAL;
            xmlChar *inherits = xmlGetProp(c, BAD_CAST "inherits"), *text = xmlGetProp(c, BAD_CAST "text");
            memset(&temp, 0, sizeof(temp)); temp.halign = e->halign; temp.valign = e->valign;
            UIWow_XmlInheritElem(&temp, (char const *)inherits);
            UIWow_XmlReadAnchor(&temp, c); UIWow_XmlReadJustify(&temp, c); UIWow_XmlReadFont(&temp, c);
            UIWow_XmlInheritElem(e, (char const *)inherits);
            if (!xmlStrcasecmp(c->name, BAD_CAST "DisabledText")) text_state = WOW_XML_BUTTON_TEXT_DISABLED;
            else if (!xmlStrcasecmp(c->name, BAD_CAST "HighlightText")) text_state = WOW_XML_BUTTON_TEXT_HIGHLIGHT;
            if (text && *text) UIWow_ElemSetStr(e, ELEM_TEXT, (char const *)text);
            e->button_text_colors[text_state] = temp.colors[ELEM_COLOR_TEXT];
            e->flags |= EF_HAS_BUTTON_TEXT_COLORS;
            if (text_state == WOW_XML_BUTTON_TEXT_NORMAL) e->colors[ELEM_COLOR_TEXT] = temp.colors[ELEM_COLOR_TEXT];
            if (temp.flags & EF_HAS_ANCHOR) e->text_off = temp.offset;
            if (temp.flags & EF_HAS_HALIGN) { e->halign = temp.halign; e->flags |= EF_HAS_HALIGN; }
            if (temp.flags & EF_HAS_VALIGN) { e->valign = temp.valign; e->flags |= EF_HAS_VALIGN; }
            SAFE_DELETE(inherits, xmlFree); SAFE_DELETE(text, xmlFree);
            UIWow_ElemFreeStrings(&temp);
        }
    }
}

static void UIWow_XmlReadScripts(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        xmlNodePtr s;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Scripts")) continue;
        for (s = c->children; s; s = s->next) {
            xmlChar *body;
            if (s->type != XML_ELEMENT_NODE) continue;
            body = xmlNodeGetContent(s);
            if (!body) continue;
            if (!xmlStrcasecmp(s->name, BAD_CAST "OnClick")) UIWow_ElemSetStr(e, ELEM_ON_CLICK, (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnLoad")) UIWow_ElemSetStr(e, ELEM_ON_LOAD, (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnShow")) {
                UIWow_ElemSetStr(e, ELEM_ON_SHOW, (char const *)body);
            }
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnEnter")) UIWow_ElemSetStr(e, ELEM_ON_ENTER, (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnLeave")) UIWow_ElemSetStr(e, ELEM_ON_LEAVE, (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnEnterPressed")) UIWow_ElemSetStr(e, ELEM_ON_ENTER_PRESSED, (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnEscapePressed")) UIWow_ElemSetStr(e, ELEM_ON_ESCAPE_PRESSED, (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnTabPressed")) UIWow_ElemSetStr(e, ELEM_ON_TAB_PRESSED, (char const *)body);
            SAFE_DELETE(body, xmlFree);
        }
    }
}

static void UIWow_XmlReadShared(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlChar *file = xmlGetProp(node, BAD_CAST "file"), *hidden = xmlGetProp(node, BAD_CAST "hidden");
    xmlChar *text = xmlGetProp(node, BAD_CAST "text"), *virt = xmlGetProp(node, BAD_CAST "virtual");
    xmlChar *all = xmlGetProp(node, BAD_CAST "setAllPoints"), *password = xmlGetProp(node, BAD_CAST "password");
    xmlChar *id = xmlGetProp(node, BAD_CAST "id");
    if (file && *file) UIWow_ElemSetStr(e, ELEM_FILE, (char const *)file);
    if (text && *text) UIWow_ElemSetStr(e, ELEM_TEXT, (char const *)text);
    if (hidden && *hidden && !strcasecmp((char const *)hidden, "true")) e->flags |= EF_HIDDEN;
    if (virt && *virt && !strcasecmp((char const *)virt, "true")) e->flags |= EF_VIRTUAL;
    if (all && *all && !strcasecmp((char const *)all, "true")) e->flags |= EF_SET_ALL_PTS;
    if (password && *password && strcmp((char const *)password, "0")) e->flags |= EF_PASSWORD;
    if (id && *id) e->id = atoi((char const *)id);
    SAFE_DELETE(file, xmlFree); SAFE_DELETE(hidden, xmlFree); SAFE_DELETE(text, xmlFree); SAFE_DELETE(virt, xmlFree);
    SAFE_DELETE(all, xmlFree); SAFE_DELETE(password, xmlFree); SAFE_DELETE(id, xmlFree);
    UIWow_XmlReadSize(e, node); UIWow_XmlReadAnchor(e, node); UIWow_XmlReadBackdrop(e, node);
    UIWow_XmlReadTexCoords(e, node); UIWow_XmlReadFont(e, node); UIWow_XmlReadJustify(e, node);
    UIWow_XmlReadTextInsets(e, node);
    UIWow_XmlReadButton(e, node); UIWow_XmlReadScripts(e, node);
}

static void UIWow_XmlParseNode(xmlNodePtr node, int parent, int draw_layer);

static void UIWow_XmlParseLayer(xmlNodePtr node, int parent) {
    xmlNodePtr c; xmlChar *level = xmlGetProp(node, BAD_CAST "level"); int layer = UIWow_XmlLayer((char const *)level);
    SAFE_DELETE(level, xmlFree);
    for (c = node->children; c; c = c->next) UIWow_XmlParseNode(c, parent, layer);
}

static void UIWow_XmlParseChildren(xmlNodePtr node, int parent) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcasecmp(c->name, BAD_CAST "Layers")) {
            xmlNodePtr l; for (l = c->children; l; l = l->next) if (l->type == XML_ELEMENT_NODE && !xmlStrcasecmp(l->name, BAD_CAST "Layer")) UIWow_XmlParseLayer(l, parent);
            continue;
        }
        if (!xmlStrcasecmp(c->name, BAD_CAST "Frames")) {
            xmlNodePtr f; for (f = c->children; f; f = f->next) UIWow_XmlParseNode(f, parent, WOW_XML_LAYER_ARTWORK);
            continue;
        }
    }
}

static void UIWow_XmlParseNode(xmlNodePtr node, int parent, int draw_layer) {
    uiWowXmlType_t type; xmlChar *name_attr, *parent_attr, *inherits_attr; int idx;
    if (!node || node->type != XML_ELEMENT_NODE || !node->name) return;
    if (!xmlStrcasecmp(node->name, BAD_CAST "Layer")) { UIWow_XmlParseLayer(node, parent); return; }
    if (!xmlStrcasecmp(node->name, BAD_CAST "Frames") || !xmlStrcasecmp(node->name, BAD_CAST "Layers")) { UIWow_XmlParseChildren(node, parent); return; }
    if (!xmlStrcasecmp(node->name, BAD_CAST "Frame")) type = WOW_XML_FRAME;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Model")) type = WOW_XML_MODEL;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Texture")) type = WOW_XML_TEXTURE;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "FontString")) type = WOW_XML_FONTSTRING;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Button") ||
             !xmlStrcasecmp(node->name, BAD_CAST "CheckButton")) type = WOW_XML_BUTTON;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "EditBox")) type = WOW_XML_EDITBOX;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "NormalTexture") || !xmlStrcasecmp(node->name, BAD_CAST "PushedTexture") || !xmlStrcasecmp(node->name, BAD_CAST "DisabledTexture") || !xmlStrcasecmp(node->name, BAD_CAST "HighlightTexture")) type = WOW_XML_TEXTURE;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "NormalText") || !xmlStrcasecmp(node->name, BAD_CAST "DisabledText") || !xmlStrcasecmp(node->name, BAD_CAST "HighlightText")) type = WOW_XML_FONTSTRING;
    else return;
    name_attr = xmlGetProp(node, BAD_CAST "name"); parent_attr = xmlGetProp(node, BAD_CAST "parent"); inherits_attr = xmlGetProp(node, BAD_CAST "inherits");
    /* Substitute $parent with the parent frame's name (WoW template convention). */
    char resolved_name[256] = "";
    if (name_attr && *name_attr) {
        LPCSTR pname = (parent >= 0 && parent < wow_xml.count) ? wow_xml.elems[parent].texts[ELEM_NAME] : NULL;
        LPCSTR raw = (char const *)name_attr;
        LPCSTR dollar = strstr(raw, "$parent");
        if (dollar && pname && *pname) {
            snprintf(resolved_name, sizeof(resolved_name), "%.*s%s%s",
                     (int)(dollar - raw), raw, pname, dollar + 7);
        } else {
            snprintf(resolved_name, sizeof(resolved_name), "%s", raw);
        }
    }
    idx = UIWow_XmlPushElem(type, resolved_name[0] ? resolved_name : NULL, parent, draw_layer); SAFE_DELETE(name_attr, xmlFree);
    if (idx < 0) { SAFE_DELETE(parent_attr, xmlFree); UIWow_Printf("UIWow: XML element limit exceeded\n"); return; }
    UIWow_XmlInheritElem(&wow_xml.elems[idx], (char const *)inherits_attr); SAFE_DELETE(inherits_attr, xmlFree);
    if (parent_attr && *parent_attr) {
        uiWowXmlElem_t *e = &wow_xml.elems[idx]; int named_parent;
        UIWow_ElemSetStr(e, ELEM_PARENT_NAME, (char const *)parent_attr);
        named_parent = UIWow_XmlFindByName(e->texts[ELEM_PARENT_NAME]); if (named_parent >= 0) e->parent = named_parent;
    }
    SAFE_DELETE(parent_attr, xmlFree);
    if (type == WOW_XML_EDITBOX) wow_xml.elems[idx].flags |= EF_FOCUSABLE;
    UIWow_XmlReadShared(&wow_xml.elems[idx], node); UIWow_XmlPublishFrame(idx); UIWow_XmlParseChildren(node, idx);
    if (UIWow_ElemStr(&wow_xml.elems[idx], ELEM_ON_LOAD))
        UIWow_XMLRunFrameScript(idx, wow_xml.elems[idx].texts[ELEM_ON_LOAD], "OnLoad");
}

static BOOL UIWow_XMLProcessFile(LPCSTR path, int depth);

static void UIWow_XMLProcessTopLevel(LPCSTR path, xmlNodePtr root, int depth) {
    xmlNodePtr n;
    for (n = root->children; n; n = n->next) {
        if (n->type != XML_ELEMENT_NODE || !n->name) continue;
        if (!xmlStrcasecmp(n->name, BAD_CAST "Include")) {
            xmlChar *f = xmlGetProp(n, BAD_CAST "file"); char resolved[PATH_MAX];
            if (!f || !*f) { UIWow_Printf("UIWow: %s has <Include> without file\n", path); SAFE_DELETE(f, xmlFree); continue; }
            if (!UIWow_XmlResolvePath(path, (char const *)f, resolved, sizeof(resolved))) UIWow_Printf("UIWow: include path too long: %s\n", (char const *)f);
            else UIWow_XMLProcessFile(resolved, depth + 1);
            SAFE_DELETE(f, xmlFree); continue;
        }
        if (!xmlStrcasecmp(n->name, BAD_CAST "Script")) {
            xmlChar *f = xmlGetProp(n, BAD_CAST "file"), *body = xmlNodeGetContent(n); char resolved[PATH_MAX], chunk[512];
            if (f && *f) {
                if (!UIWow_XmlResolvePath(path, (char const *)f, resolved, sizeof(resolved))) UIWow_Printf("UIWow: script path too long: %s\n", (char const *)f);
                else if (!UIWow_LoadLuaFile(resolved, false)) UIWow_Printf("UIWow: missing script %s referenced by %s\n", resolved, path);
            }
            if (body && *body) { snprintf(chunk, sizeof(chunk), "%s:<Script>", path); UIWow_RunLuaString(chunk, (char const *)body); }
            SAFE_DELETE(f, xmlFree); SAFE_DELETE(body, xmlFree); continue;
        }
        UIWow_XmlParseNode(n, -1, WOW_XML_LAYER_ARTWORK);
    }
}

/* Load and parse one XML document, then process top-level Include/Script/frame nodes. */
static BOOL UIWow_XMLProcessXml(LPCSTR path, int depth) {
    void *buf = NULL; int size; xmlDocPtr doc; xmlNodePtr root;
    if (depth > 32) { UIWow_Printf("UIWow: XML include recursion too deep at %s\n", path); return false; }
    if (!uiimport.FS_ReadFile || !uiimport.FS_FreeFile) { UIWow_WarnOnce(WOW_UI_WARN_NO_INPUT_FS, "UIWow: FS API unavailable for XML load\n"); return false; }
    size = uiimport.FS_ReadFile(path, &buf);
    if (size <= 0 || !buf) { SAFE_DELETE(buf, uiimport.FS_FreeFile); UIWow_Printf("UIWow: missing XML %s\n", path); return false; }
    doc = xmlReadMemory((char const *)buf, size, path, NULL, XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING); uiimport.FS_FreeFile(buf);
    if (!doc) { UIWow_Printf("UIWow: parse failed for %s\n", path); return false; }
    root = xmlDocGetRootElement(doc);
    if (!root) { xmlFreeDoc(doc); UIWow_Printf("UIWow: empty XML root in %s\n", path); return false; }
    UIWow_XMLProcessTopLevel(path, root, depth); xmlFreeDoc(doc);
    return true;
}

/* Dispatch one FrameXML path by extension: Lua files execute, others parse as XML. */
static BOOL UIWow_XMLProcessFile(LPCSTR path, int depth) {
    LPCSTR ext = strrchr(path ? path : "", '.');
    if (!path || !*path) return false;
    if (ext && !strcasecmp(ext, ".lua")) return UIWow_LoadLuaFile(path, false);
    return UIWow_XMLProcessXml(path, depth);
}

/* Read Glue TOC entries line-by-line, ignore comments, resolve relative paths, and process each entry. */
static BOOL UIWow_XMLLoadFromToc(LPCSTR toc_path) {
    void *buf = NULL; int size; char *text, *cur;
    if (!uiimport.FS_ReadFile || !uiimport.FS_FreeFile) { UIWow_WarnOnce(WOW_UI_WARN_NO_INPUT_FS, "UIWow: FS API unavailable for TOC load\n"); return false; }
    size = uiimport.FS_ReadFile(toc_path, &buf);
    if (size <= 0 || !buf) { SAFE_DELETE(buf, uiimport.FS_FreeFile); UIWow_Printf("UIWow: missing TOC %s\n", toc_path); return false; }
    text = (char *)buf; cur = text;
    while (*cur) {
        char line[PATH_MAX], resolved[PATH_MAX];
        char *end = cur;
        int n = 0, len;
        while (*end && *end != '\n' && *end != '\r') end++;
        len = (int)(end - cur);
        if (len > 0 && len < (int)sizeof(line)) {
            memcpy(line, cur, (size_t)len);
            line[len] = '\0';
            while (line[n] && isspace((unsigned char)line[n])) n++;
            if (line[n] && line[n] != '#') {
                if (UIWow_XmlResolvePath(toc_path, line + n, resolved, sizeof(resolved))) {
                    UIWow_XMLProcessFile(resolved, 0);
                } else {
                    UIWow_Printf("UIWow: TOC entry path too long in %s: %s\n", toc_path, line + n);
                }
            }
        }

        while (*end == '\n' || *end == '\r') end++;
        cur = end;
    }
    uiimport.FS_FreeFile(buf);
    return true;
}

static void UIWow_LuaSetGlueScreen_named(LPCSTR screen) {
    if (!wow_ui.lua || !screen || !*screen) return;
    lua_getglobal(wow_ui.lua, "SetGlueScreen");
    if (lua_isfunction(wow_ui.lua, -1)) {
        lua_pushstring(wow_ui.lua, screen);
        UIWow_LuaPCall(1);
    } else {
        lua_pop(wow_ui.lua, 1);
    }
}

static void UIWow_XMLFreeElems(void) {
    FOR_LOOP(i, wow_xml.count) UIWow_ElemFreeStrings(&wow_xml.elems[i]);
}

void UIWow_XMLInitRuntime(void) { memset(&wow_xml, 0, sizeof(wow_xml)); wow_xml.focus = -1; wow_xml.pressed_button = -1; UIWow_XMLInstallLuaCompat(); }
void UIWow_XMLShutdownRuntime(void) {
    if (wow_ui.renderer && wow_ui.renderer->ReleaseModel) FOR_LOOP(i, wow_xml.count) SAFE_DELETE(wow_xml.elems[i].model, wow_ui.renderer->ReleaseModel);
    UIWow_XMLFreeElems();
    memset(&wow_xml, 0, sizeof(wow_xml)); wow_xml.focus = -1; wow_xml.pressed_button = -1;
}

BOOL UIWow_XMLLoadGlueFromToc(LPCSTR toc_path) {
    if (!wow_ui.lua) { UIWow_Printf("UIWow: XML runtime requires active lua_State\n"); return false; }
    if (!wow_xml.lua_ready) UIWow_XMLInstallLuaCompat();
    UIWow_XMLFreeElems();
    memset(wow_xml.elems, 0, sizeof(wow_xml.elems)); wow_xml.count = 0; wow_xml.focus = -1; wow_xml.pressed_button = -1;
    if (!UIWow_XMLLoadFromToc(toc_path)) return false;
    UIWow_XMLInstallScreenShim();
    FOR_LOOP(i, wow_xml.count) {
        uiWowXmlElem_t *e = &wow_xml.elems[i];
        if (UIWow_ElemStr(e, ELEM_PARENT_NAME)) {
            int p = UIWow_XmlFindByName(e->texts[ELEM_PARENT_NAME]);
            if (p >= 0) e->parent = p;
        }
        if (UIWow_ElemStr(e, ELEM_RELATIVE_NAME)) {
            int rel = UIWow_XmlFindByName(e->texts[ELEM_RELATIVE_NAME]);
            if (rel >= 0) e->relative_to = rel;
        }
    }
    UIWow_Printf("UIWow: FrameXML loaded from %s (elements=%d)\n", toc_path, wow_xml.count);
    return wow_xml.count > 0;
}

static void UIWow_XMLRunFrameScript(int idx, LPCSTR script, LPCSTR event_name) {
    char chunk[192];
    LPCSTR name;
    if (!wow_ui.lua || idx < 0 || idx >= wow_xml.count || !script || !*script) return;
    UIWow_XmlPublishFrame(idx);
    name = wow_xml.elems[idx].texts[ELEM_NAME];
    lua_getglobal(wow_ui.lua, name ? name : ""); lua_setglobal(wow_ui.lua, "this");
    lua_pushstring(wow_ui.lua, event_name ? event_name : ""); lua_setglobal(wow_ui.lua, "event");
    snprintf(chunk, sizeof(chunk), "%s:%s", name && name[0] ? name : "<anon>", event_name ? event_name : "Script");
    if (luaL_loadbuffer(wow_ui.lua, script, strlen(script), chunk) != LUA_OK) {
        UIWow_Printf("UIWow Lua load: %s\n", lua_tostring(wow_ui.lua, -1)); lua_pop(wow_ui.lua, 1);
    } else {
        UIWow_LuaPCall(0);
    }
    lua_pushnil(wow_ui.lua); lua_setglobal(wow_ui.lua, "this");
}

static int UIWow_XMLHitFrame(FLOAT x, FLOAT y);

static void UIWow_XMLDrawImage(LPTEXTURE tex, LPCRECT screen, LPCRECT uv, COLOR32 color, BOOL rotate, BLEND_MODE mode) {
    if (!wow_ui.renderer || !tex) return;
    if (wow_ui.renderer->DrawImageEx) {
        wow_ui.renderer->DrawImageEx(&MAKE(drawImage_t,
                                           .texture = tex,
                                           .shader = SHADER_UI,
                                           .alphamode = mode,
                                           .screen = *screen,
                                           .uv = *uv,
                                           .color = color,
                                           .rotate = rotate));
    } else if (wow_ui.renderer->DrawImage) {
        wow_ui.renderer->DrawImage(tex, screen, uv, color);
    }
}

static void UIWow_XMLBackdropRects(LPCRECT screen, LPRECT rects, FLOAT edge_w, FLOAT edge_h) {
    FLOAT ew = MIN(edge_w, screen->w * 0.5f), eh = MIN(edge_h, screen->h * 0.5f);
    FLOAT x[] = { 0, ew, screen->w - ew, screen->w };
    FLOAT y[] = { 0, eh, screen->h - eh, screen->h };

    FOR_LOOP(i, BACKDROP_SIZE) {
        rects[i].x = screen->x + x[i % 3];
        rects[i].y = screen->y + y[i / 3];
        rects[i].w = x[(i % 3) + 1] - x[i % 3];
        rects[i].h = y[(i / 3) + 1] - y[i / 3];
    }
}

static FLOAT UIWow_XMLBackdropTile(LPCRECT rect, BACKDROPCORNER edge, FLOAT image_w, FLOAT image_h) {
    switch (edge) {
        case BACKDROP_LEFT_EDGE:
        case BACKDROP_RIGHT_EDGE:
            return image_h > 0.0f ? rect->h / image_h : 1.0f;
        case BACKDROP_TOP_EDGE:
        case BACKDROP_BOTTOM_EDGE:
            return image_w > 0.0f ? rect->w / image_w : 1.0f;
        default:
            return 1.0f;
    }
}

static BOOL UIWow_XMLBackdropRotate(BACKDROPCORNER edge) {
    return edge == BACKDROP_TOP_EDGE || edge == BACKDROP_BOTTOM_EDGE;
}

static void UIWow_XMLDrawBackdropBorder(uiWowXmlElem_t const *e, LPCRECT r, LPTEXTURE border) {
    static BACKDROPCORNER const corners[WOW_XML_NUM_BACKDROP_CORNERS] = {
        BACKDROP_LEFT_EDGE,        BACKDROP_RIGHT_EDGE,
        BACKDROP_TOP_EDGE,         BACKDROP_BOTTOM_EDGE,
        BACKDROP_TOP_LEFT_CORNER,  BACKDROP_TOP_RIGHT_CORNER,
        BACKDROP_BOTTOM_LEFT_CORNER, BACKDROP_BOTTOM_RIGHT_CORNER,
    };
    RECT rects[BACKDROP_SIZE];
    size2_t size = wow_ui.renderer->GetTextureSize ? wow_ui.renderer->GetTextureSize(border) : MAKE(size2_t, 0, 0);
    FLOAT image_w = UIWow_XmlX((FLOAT)size.height), image_h = UIWow_XmlY((FLOAT)size.height);

    UIWow_XMLBackdropRects(r, rects, e->edge.w, e->edge.h);
    FOR_LOOP(i, WOW_XML_NUM_BACKDROP_CORNERS) {
        BACKDROPCORNER corner = corners[i];
        FLOAT const k = 1.0f / WOW_XML_NUM_BACKDROP_CORNERS;
        FLOAT const tile = UIWow_XMLBackdropTile(rects + corner, corner, image_w, image_h);
        RECT const uv = MAKE(RECT, i * k, 0, k, tile);
        UIWow_XMLDrawImage(border, rects + corner, &uv, e->colors[ELEM_COLOR_BACKDROP_BORDER],
                   UIWow_XMLBackdropRotate(corner), BLEND_MODE_BLEND);
    }
}

static void UIWow_XMLDrawBackdrop(uiWowXmlElem_t const *e, LPCRECT r) {
    LPCSTR bg_path = e->texts[ELEM_BACKDROP_BG];
    LPCSTR edge_path = e->texts[ELEM_BACKDROP_EDGE];
    if (!wow_ui.renderer) return;
    if (bg_path && bg_path[0]) {
        LPTEXTURE bg = UIWow_LoadTexture(bg_path);
        RECT bg_rect = *r, uv = MAKE(RECT, 0, 0, 1, 1);
        bg_rect.x += e->backdrop_insets[WOW_XML_BACKDROP_LEFT];
        bg_rect.y += e->backdrop_insets[WOW_XML_BACKDROP_TOP];
        bg_rect.w -= e->backdrop_insets[WOW_XML_BACKDROP_LEFT] + e->backdrop_insets[WOW_XML_BACKDROP_RIGHT];
        bg_rect.h -= e->backdrop_insets[WOW_XML_BACKDROP_TOP] + e->backdrop_insets[WOW_XML_BACKDROP_BOTTOM];
        if (bg && (e->flags & EF_BACKDROP_TILE)) {
            size2_t size = wow_ui.renderer->GetTextureSize ? wow_ui.renderer->GetTextureSize(bg) : MAKE(size2_t, 0, 0);
            FLOAT tw = e->tile.w > 0.0f ? e->tile.w : UIWow_XmlX((FLOAT)size.width);
            FLOAT th = e->tile.h > 0.0f ? e->tile.h : UIWow_XmlY((FLOAT)size.height);
            if (tw > 0.0f) uv.w = bg_rect.w / tw;
            if (th > 0.0f) uv.h = bg_rect.h / th;
        }
        if (bg && bg_rect.w > 0.0f && bg_rect.h > 0.0f) {
            UIWow_XMLDrawImage(bg, &bg_rect, &uv, e->colors[ELEM_COLOR_BACKDROP], false, BLEND_MODE_BLEND);
        }
    }
    if (edge_path && edge_path[0] && e->edge.w > 0.0f && e->edge.h > 0.0f) {
        LPTEXTURE border = UIWow_LoadTexture(edge_path);
        if (border) UIWow_XMLDrawBackdropBorder(e, r, border);
    }
}

static BOOL UIWow_XMLIsVisible(int idx) {
    while (idx >= 0 && idx < wow_xml.count) {
        uiWowXmlElem_t const *e = &wow_xml.elems[idx];
        if (!(e->flags & EF_USED) || (e->flags & EF_HIDDEN) || (e->flags & EF_VIRTUAL)) return false;
        idx = e->parent;
    }
    return true;
}

static LPCSTR UIWow_XMLResolveText(uiWowXmlElem_t const *e, LPSTR out, size_t out_size) {
    LPCSTR t = e->texts[ELEM_TEXT];
    if (!e || !t || !t[0]) return "";
    if (wow_ui.lua) {
        lua_getglobal(wow_ui.lua, t);
        if (lua_isstring(wow_ui.lua, -1)) {
            snprintf(out, out_size, "%s", lua_tostring(wow_ui.lua, -1));
            lua_pop(wow_ui.lua, 1);
            return out;
        }
        lua_pop(wow_ui.lua, 1);
    }
    snprintf(out, out_size, "%s", t);
    return out;
}

static LPCSTR UIWow_XMLDisplayText(uiWowXmlElem_t const *e, LPSTR out, size_t out_size) {
    LPCSTR t = e->texts[ELEM_TEXT];
    if (!e || !(e->flags & EF_PASSWORD)) return UIWow_XMLResolveText(e, out, out_size);
    size_t n = t ? MIN(strlen(t), out_size - 1) : 0;
    memset(out, '*', n); out[n] = '\0';
    return out;
}

void UIWow_XMLDraw(void) {
    int hovered_button = -1;
    if (uiimport.GetMouseFdf) {
        VECTOR2 m = uiimport.GetMouseFdf();
        int hit = UIWow_XMLHitFrame(m.x, m.y);
        if (hit >= 0 && wow_xml.elems[hit].type == WOW_XML_BUTTON) hovered_button = hit;
    }

    UIWow_EnsureRenderer(); if (!wow_ui.renderer) return;
    for (int layer = WOW_XML_LAYER_BACKGROUND; layer <= WOW_XML_LAYER_OVERLAY; layer++) FOR_LOOP(i, wow_xml.count) {
        uiWowXmlElem_t *e = &wow_xml.elems[i]; RECT r; RECT uv = MAKE(RECT, 0, 0, 1, 1); char text[512];
        COLOR32 text_color = e->colors[ELEM_COLOR_TEXT];
        BOOL pressed = e->type == WOW_XML_BUTTON && wow_xml.pressed_button == (int)i;
        BOOL hovered = e->type == WOW_XML_BUTTON && hovered_button == (int)i;
        LPCSTR file = e->texts[ELEM_FILE], normal_file = e->texts[ELEM_NORMAL_FILE], pushed_file = e->texts[ELEM_PUSHED_FILE];
        LPCSTR highlight_file = e->texts[ELEM_HIGHLIGHT_FILE], elem_text = e->texts[ELEM_TEXT];
        if (!(e->flags & EF_USED) || e->draw_layer != layer || !UIWow_XMLIsVisible((int)i)) continue;
        r = UIWow_XmlComputeRect(i);
        if (e->type == WOW_XML_BUTTON) {
            text_color = !(e->flags & EF_ENABLED) ? e->button_text_colors[WOW_XML_BUTTON_TEXT_DISABLED] :
                         (hovered ? e->button_text_colors[WOW_XML_BUTTON_TEXT_HIGHLIGHT] : e->button_text_colors[WOW_XML_BUTTON_TEXT_NORMAL]);
        }
        if (pressed) {
            r.x += UIWow_XmlX(1.0f);
            r.y += UIWow_XmlY(1.0f);
        }
        if (e->type == WOW_XML_MODEL && file && file[0]) {
            if (!e->model && wow_ui.renderer->LoadModel) e->model = wow_ui.renderer->LoadModel(file);
            if (e->model && wow_ui.renderer->DrawPortrait) wow_ui.renderer->DrawPortrait(e->model, &r, "Stand");
            else if (!wow_ui.renderer->LoadModel) UIWow_WarnOnce(WOW_UI_WARN_NO_MODEL_LOADER, "UIWow: renderer has no model loader; XML model frames skipped\n");
        }
        if (e->type == WOW_XML_FRAME || e->type == WOW_XML_BUTTON || e->type == WOW_XML_EDITBOX) UIWow_XMLDrawBackdrop(e, &r);
        if ((file && file[0] && e->type == WOW_XML_TEXTURE) || (e->type == WOW_XML_BUTTON && ((normal_file && normal_file[0]) || (file && file[0])))) {
            LPCSTR src = (e->type == WOW_XML_BUTTON && pressed && pushed_file && pushed_file[0]) ? pushed_file :
                         ((e->type == WOW_XML_BUTTON && normal_file && normal_file[0]) ? normal_file : file);
            LPTEXTURE t = UIWow_LoadTexture(src);
            if (e->flags & EF_HAS_TEXCOORD) uv = e->texcoord;
            if (t) {
                UIWow_XMLDrawImage(t,
                                   &r,
                                   &uv,
                                   MAKE(COLOR32,
                                        e->colors[ELEM_COLOR_VERTEX].r,
                                        e->colors[ELEM_COLOR_VERTEX].g,
                                        e->colors[ELEM_COLOR_VERTEX].b,
                                        (BYTE)(e->colors[ELEM_COLOR_VERTEX].a * e->alpha)),
                                   false,
                                   BLEND_MODE_BLEND);
            }
            if (e->type == WOW_XML_BUTTON && hovered && highlight_file && highlight_file[0]) {
                LPTEXTURE ht = UIWow_LoadTexture(highlight_file);
                RECT huv = MAKE(RECT, 0, 0, 1, 1);
                if (e->flags & EF_HAS_HIGHLIGHT_TEXCOORD) huv = e->highlight_texcoord;
                if (ht) UIWow_XMLDrawImage(ht, &r, &huv, COLOR32_WHITE, false, BLEND_MODE_ADD);
            }
        }
        if (elem_text && elem_text[0] && (e->type == WOW_XML_FONTSTRING || e->type == WOW_XML_EDITBOX || e->type == WOW_XML_BUTTON)) {
            LPCFONT f = UIWow_LoadFont((DWORD)e->font_size);
            RECT tr = MAKE(RECT,
                           r.x + e->text_inset.w + e->text_off.x,
                           r.y + e->text_off.y,
                           r.w - e->text_inset.w,
                           r.h - e->text_inset.h);
            LPCSTR display = UIWow_XMLDisplayText(e, text, sizeof(text));
            if (f) {
                wow_ui.renderer->DrawText(&MAKE(drawText_t,
                                                .font = f,
                                                .text = display,
                                                .rect = tr,
                                                .color = MAKE(COLOR32,
                                                              text_color.r,
                                                              text_color.g,
                                                              text_color.b,
                                                              (BYTE)(text_color.a * e->alpha)),
                                                .textWidth = tr.w,
                                                .lineHeight = tr.h,
                                                .wordWrap = false,
                                                .halign = e->type == WOW_XML_EDITBOX
                                                          ? FONT_JUSTIFYLEFT
                                                          : e->halign,
                                                .valign = e->valign));
            }
        }
    }
}

static BOOL UIWow_XMLPointInRect(FLOAT x, FLOAT y, LPCRECT r) {
    return r && x >= r->x && y >= r->y && x <= r->x + r->w && y <= r->y + r->h;
}

static int UIWow_XMLHitFrame(FLOAT x, FLOAT y) {
    for (int i = wow_xml.count - 1; i >= 0; i--) {
        uiWowXmlElem_t const *e = &wow_xml.elems[i]; RECT r;
        if (!UIWow_XMLIsVisible(i) || (e->type != WOW_XML_BUTTON && e->type != WOW_XML_EDITBOX)) continue;
        r = UIWow_XmlComputeRect(i);
        if (UIWow_XMLPointInRect(x, y, &r)) return i;
    }
    return -1;
}

BOOL UIWow_XMLMouseEvent(int x, int y, int button, BOOL down) {
    int hit;
    hit = UIWow_XMLHitFrame(x / 1024.0f, y / 768.0f);
    if (!down) {
        int pressed = wow_xml.pressed_button;
        wow_xml.pressed_button = -1;
        if (button == 1 && pressed >= 0 && hit == pressed && wow_xml.elems[pressed].type == WOW_XML_BUTTON &&
            (wow_xml.elems[pressed].flags & EF_ENABLED) && wow_ui.lua &&
            UIWow_ElemStr(&wow_xml.elems[pressed], ELEM_ON_CLICK)) {
            UIWow_XMLRunFrameScript(pressed, wow_xml.elems[pressed].texts[ELEM_ON_CLICK], "OnClick");
            return true;
        }
        return hit >= 0 || pressed >= 0;
    }
    if (hit < 0) {
        wow_xml.pressed_button = -1;
        return false;
    }
    if (wow_xml.elems[hit].type == WOW_XML_EDITBOX) {
        wow_xml.focus = hit;
        wow_xml.pressed_button = -1;
        return true;
    }
    if (wow_xml.elems[hit].type == WOW_XML_BUTTON && (wow_xml.elems[hit].flags & EF_ENABLED)) {
        wow_xml.pressed_button = hit;
        (void)button;
        return true;
    }
    wow_xml.pressed_button = -1;
    return false;
}

BOOL UIWow_XMLTextInput(LPCSTR text) {
    uiWowXmlElem_t *e;
    if (wow_xml.focus < 0 || wow_xml.focus >= wow_xml.count || !text || !*text) return false;
    e = &wow_xml.elems[wow_xml.focus];
    if (e->type != WOW_XML_EDITBOX) return false;
    if (!strcmp(text, "\b")) {
        LPCSTR t = e->texts[ELEM_TEXT];
        if (t && t[0]) {
            size_t n = strlen(t);
            e->texts[ELEM_TEXT][n - 1] = '\0';
        }
        return true;
    }
    if (!strcmp(text, "\r") || !strcmp(text, "\n")) {
        if (UIWow_ElemStr(e, ELEM_ON_ENTER_PRESSED))
            UIWow_XMLRunFrameScript(wow_xml.focus, e->texts[ELEM_ON_ENTER_PRESSED], "OnEnterPressed");
        return true;
    }
    UIWow_ElemAppendStr(e, ELEM_TEXT, text);
    return true;
}

BOOL UIWow_XMLKeyEvent(int key, BOOL down, DWORD time) {
    uiWowXmlElem_t *e;
    (void)time;
    if (!down || wow_xml.focus < 0 || wow_xml.focus >= wow_xml.count) return false;
    e = &wow_xml.elems[wow_xml.focus];
    if (e->type != WOW_XML_EDITBOX) return false;
    if (key == '\r' || key == '\n') {
        if (UIWow_ElemStr(e, ELEM_ON_ENTER_PRESSED))
            UIWow_XMLRunFrameScript(wow_xml.focus, e->texts[ELEM_ON_ENTER_PRESSED], "OnEnterPressed");
        return true;
    }
    if (key == '\b' || key == 127) {
        LPCSTR t = e->texts[ELEM_TEXT];
        if (t && t[0]) e->texts[ELEM_TEXT][strlen(t) - 1] = '\0';
        return true;
    }
    if (key == '\t') {
        if (UIWow_ElemStr(e, ELEM_ON_TAB_PRESSED))
            UIWow_XMLRunFrameScript(wow_xml.focus, e->texts[ELEM_ON_TAB_PRESSED], "OnTabPressed");
        return true;
    }
    if (key == 27) {
        if (UIWow_ElemStr(e, ELEM_ON_ESCAPE_PRESSED))
            UIWow_XMLRunFrameScript(wow_xml.focus, e->texts[ELEM_ON_ESCAPE_PRESSED], "OnEscapePressed");
        return true;
    }
    return false;
}
