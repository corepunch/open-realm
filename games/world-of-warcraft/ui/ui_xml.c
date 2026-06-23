/* ui_xml.c — WoW Glue FrameXML-style loader/runtime (TOC, Include/Script, frame registry, basic drawing). */
#include "ui_local.h"
#include "ui_dbc.h"
#include "client/ui_text_input.h"

#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>
#if defined(__has_include)
#if __has_include(<SDL2/SDL_keycode.h>)
#include <SDL2/SDL_keycode.h>
#endif
#endif

#ifndef SDLK_BACKSPACE
#define SDLK_BACKSPACE 8
#define SDLK_DELETE 127
#define SDLK_LEFT 1073741904
#define SDLK_RIGHT 1073741903
#define SDLK_HOME 1073741898
#define SDLK_END 1073741901
#define SDLK_RETURN 13
#define SDLK_KP_ENTER 1073741912
#define SDLK_TAB 9
#define SDLK_ESCAPE 27
#endif

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
    ELEM_ON_MOUSE_WHEEL,
    ELEM_ON_UPDATE_MODEL,
    ELEM_ON_UPDATE,
    ELEM_SOURCE_FILE,
    ELEM_NORMAL_NAME,
    ELEM_PUSHED_NAME,
    ELEM_HIGHLIGHT_NAME,
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
    EF_PENDING_ONLOAD = 1 << 16,
    EF_HAS_ANCHOR2    = 1 << 17,
    EF_WORD_WRAP      = 1 << 18,
    EF_IS_SCROLLFRAME = 1 << 19,
    EF_SCROLLBAR_PART = 1 << 20,
    EF_CHECKBUTTON    = 1 << 21,
} uiWowXmlElemFlag_t;

typedef struct {
    DWORD flags;
    uiWowXmlType_t type;
    int id, parent, relative_to, relative_to2, draw_layer;
    char *texts[ELEM_STRING_COUNT];
    fpoint_t pos, offset, text_off, offset2; /* pos(x,y), anchor offset(ox,oy), text offset, second anchor offset */
    char *point2, *relative_point2, *relative_name2; /* second anchor point/relativeTo names (owned strings) */
    fsize_t size, edge, tile, text_inset; /* size(w,h), border edge, tile size, text inset */
    FLOAT measured_h; /* renderer-measured text height; replaces size.h for FontStrings with y=0 */
    FLOAT alpha, font_size;
    FLOAT backdrop_insets[4];
    uiFontJustificationH_t halign;
    uiFontJustificationV_t valign;
    COLOR32 colors[ELEM_COLOR_COUNT];
    COLOR32 button_text_colors[WOW_XML_BUTTON_TEXT_COUNT];
    RECT texcoord;
    RECT highlight_texcoord;
    LPMODEL model;
    DWORD sequence, frame, oldframe, anim_start; /* Model animation sequence and time */
    COLOR32 fog_color;
    FLOAT fog_near;
    FLOAT fog_far;
    BOOL has_fog;
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
    uiTextInput_t text_input;
    int pressed_button;
    int hovered_button;
    BOOL lua_ready;
    struct {
        FLOAT scroll_y;
        FLOAT scroll_range;
        int   scrollbar_child; /* index of child ScrollBar/Slider, -1 if none */
    } scroll[WOW_XML_MAX_ELEMS];
    struct {
        int   scrollbar_idx;   /* scrollbar being dragged */
        FLOAT start_mouse_y;  /* mouse Y at drag start */
        FLOAT start_value;    /* scrollbar scroll_y at drag start */
    } drag;
} wow_xml;

/* XML file currently being parsed — set by UIWow_XMLProcessTopLevel, used by
   UIWow_XmlParseNode to stamp ELEM_SOURCE_FILE on each newly created element. */
static char s_current_xml_path[PATH_MAX];

/* -------------------------------------------------------------------------
 * String field helpers
 * ------------------------------------------------------------------------- */

static LPCSTR UIWow_ElemStr(uiWowXmlElem_t const *e, uiWowXmlStr_t f) {
    return (e->texts[f] && e->texts[f][0]) ? e->texts[f] : NULL;
}

static uiWowXmlStr_t const uiwow_button_part_name_fields[] = {
    ELEM_NORMAL_NAME,
    ELEM_PUSHED_NAME,
    ELEM_HIGHLIGHT_NAME
};

/* Set a string field, freeing the previous value. */
static void UIWow_ElemSetStr(uiWowXmlElem_t *e, uiWowXmlStr_t f, LPCSTR s) {
    free(e->texts[f]);
    e->texts[f] = (s && *s) ? strdup(s) : NULL;
    if (f == ELEM_TEXT) e->measured_h = 0; /* invalidate cached text height */
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
    free(e->point2); e->point2 = NULL;
    free(e->relative_point2); e->relative_point2 = NULL;
    free(e->relative_name2); e->relative_name2 = NULL;
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
    if (wow_xml.count >= WOW_XML_MAX_ELEMS) { fprintf(stderr, "UIWow: XML ELEMENT LIMIT HIT (%d/%d) name=%s parent=%d\n", wow_xml.count, WOW_XML_MAX_ELEMS, name ? name : "<anon>", parent); return -1; }
    e = &wow_xml.elems[wow_xml.count]; memset(e, 0, sizeof(*e));
    e->flags = EF_USED | EF_ENABLED | EF_WORD_WRAP; /* word-wrap on by default, same as CSimpleFontString */
    e->type = type; e->parent = parent; e->relative_to = parent; e->draw_layer = draw_layer;
    e->alpha = 1.0f; e->id = 0;
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
    static uiWowXmlStr_t const script_fields[] = {
        ELEM_ON_CLICK,
        ELEM_ON_LOAD,
        ELEM_ON_SHOW,
        ELEM_ON_ENTER,
        ELEM_ON_LEAVE,
        ELEM_ON_ENTER_PRESSED,
        ELEM_ON_ESCAPE_PRESSED,
        ELEM_ON_TAB_PRESSED,
        ELEM_ON_MOUSE_WHEEL,
        ELEM_ON_UPDATE_MODEL,
    };
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
        FOR_LOOP(i, sizeof(uiwow_button_part_name_fields) / sizeof(uiwow_button_part_name_fields[0])) {
            uiWowXmlStr_t f = uiwow_button_part_name_fields[i];
            if (!UIWow_ElemStr(e, f) && UIWow_ElemStr(src, f)) UIWow_ElemSetStr(e, f, src->texts[f]);
        }
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
        FOR_LOOP(i, sizeof(script_fields) / sizeof(script_fields[0])) {
            uiWowXmlStr_t f = script_fields[i];
            if (!UIWow_ElemStr(e, f) && UIWow_ElemStr(src, f)) UIWow_ElemSetStr(e, f, src->texts[f]);
        }
        if (src->flags & EF_WORD_WRAP) e->flags |= EF_WORD_WRAP;
    }
}

static void UIWow_XmlPublishFrame(int idx);

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
    uiWowXmlElem_t const *e = &wow_xml.elems[idx];
    RECT parent = MAKE(RECT, 0, 0, 1, 1);
    LPCSTR point = e->texts[ELEM_POINT];
    LPCSTR rel_point = e->texts[ELEM_RELATIVE_POINT];
    FLOAT default_h = e->type == WOW_XML_FONTSTRING ? UIWow_XmlY(e->font_size > 0.0f ? e->font_size : 14.0f) : 0.05f;
    /* FontStrings with no authored height use the renderer-measured text height when available. */
    FLOAT eff_h = e->size.h > 0 ? e->size.h : (e->type == WOW_XML_FONTSTRING && e->measured_h > 0 ? e->measured_h : default_h);
    RECT out = MAKE(RECT, 0, 0, e->size.w > 0 ? e->size.w : 0.2f, eff_h);
    FLOAT ax, ay;
    if (e->parent >= 0 && e->parent < wow_xml.count) parent = UIWow_XmlComputeRect(e->parent);
    if (e->flags & EF_SET_ALL_PTS) return parent;
    if (!(e->flags & EF_HAS_ANCHOR)) { out.x = parent.x; out.y = parent.y; return out; }
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
    /* Second anchor: pin a specific edge of the output rect, deriving width or height.
       point2 names the edge of THIS frame that is being pinned:
         right edge  if point2 contains "RIGHT"  → out.w = bx - out.x
         bottom edge if point2 contains "BOTTOM" → out.h = by - out.y
         left edge   if point2 contains "LEFT"   → out.x = bx (width from first anchor's right, unusual)
         top edge    if point2 contains "TOP" (but not BOTTOM) → out.y = by (unusual)
       Corner points (TOPLEFT, BOTTOMRIGHT, etc.) affect both axes. */
    if ((e->flags & EF_HAS_ANCHOR2) && e->point2 && e->relative_point2) {
        RECT ref2 = (e->relative_to2 >= 0 && e->relative_to2 < wow_xml.count)
                    ? UIWow_XmlComputeRect(e->relative_to2) : parent;
        FLOAT bx, by;
        LPCSTR p2 = e->point2;
        UIWow_XmlRectPoint(&ref2, e->relative_point2, &bx, &by);
        bx += e->offset2.x; by += e->offset2.y;
        if (strcasestr(p2, "RIGHT"))  { out.w = bx - out.x; if (out.w < 0) { out.x += out.w; out.w = -out.w; } }
        else if (strcasestr(p2, "LEFT"))   { FLOAT r = out.x + out.w; out.x = bx; out.w = r - bx; if (out.w < 0) out.w = 0; }
        if (strcasestr(p2, "BOTTOM")) { out.h = by - out.y; if (out.h < 0) { out.y += out.h; out.h = -out.h; } }
        else if (!strcasecmp(p2, "TOP"))   { FLOAT b = out.y + out.h; out.y = by; out.h = b - by; if (out.h < 0) out.h = 0; }
    }
    return out;
}

static int UIWow_FrameFromSelf(lua_State *L) {
    int idx;
    luaL_checktype(L, 1, LUA_TTABLE); lua_getfield(L, 1, "__ow3_index"); idx = (int)luaL_optinteger(L, -1, -1); lua_pop(L, 1);
    return idx >= 0 && idx < wow_xml.count && (wow_xml.elems[idx].flags & EF_USED) ? idx : -1;
}

static void UIWow_XmlPublishFrame(int idx);

static void UIWow_XmlPublishSyntheticFrame(LPCSTR name) {
    if (!wow_ui.lua || !name || !name[0]) return;
    lua_getglobal(wow_ui.lua, name);
    if (lua_istable(wow_ui.lua, -1)) { lua_pop(wow_ui.lua, 1); return; }
    lua_pop(wow_ui.lua, 1);
    lua_newtable(wow_ui.lua);
    lua_pushinteger(wow_ui.lua, -1); lua_setfield(wow_ui.lua, -2, "__ow3_index");
    lua_pushstring(wow_ui.lua, name); lua_setfield(wow_ui.lua, -2, "name");
    lua_pushboolean(wow_ui.lua, true); lua_setfield(wow_ui.lua, -2, "shown");
    luaL_getmetatable(wow_ui.lua, "UIWow.Frame"); lua_setmetatable(wow_ui.lua, -2);
    lua_setglobal(wow_ui.lua, name);
}
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
static BOOL UIWow_LuaToBool(lua_State *L, int idx) {
    if (lua_isnil(L, idx)) return false;
    if (lua_isnumber(L, idx)) return lua_tonumber(L, idx) != 0.0;
    return lua_toboolean(L, idx) != 0;
}
static int UIWow_LuaFrameSetChecked(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) { if (UIWow_LuaToBool(L, 2)) wow_xml.elems[i].flags |= EF_CHECKED; else wow_xml.elems[i].flags &= ~EF_CHECKED; }
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
static int UIWow_LuaFrameGetZero(lua_State *L) { (void)L; lua_pushnumber(L, 0); return 1; }
static int UIWow_LuaFrameGetMinMax(lua_State *L) { (void)L; lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
static int UIWow_LuaFrameGetButtonState(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    lua_pushstring(L, (i >= 0 && wow_xml.pressed_button == i) ? "PUSHED" : "NORMAL");
    return 1;
}
static int UIWow_LuaFrameSetVerticalScroll(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) {
        FLOAT val = (FLOAT)luaL_optnumber(L, 2, 0.0);
        wow_xml.scroll[i].scroll_y = MAX(0, MIN(val, wow_xml.scroll[i].scroll_range));
    }
    return 0;
}
static int UIWow_LuaFrameGetVerticalScroll(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    lua_pushnumber(L, i >= 0 ? wow_xml.scroll[i].scroll_y : 0.0);
    return 1;
}
static int UIWow_LuaFrameGetVerticalScrollRange(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    FLOAT range = i >= 0 ? wow_xml.scroll[i].scroll_range : 0.0;
    /* Return frame height in pixels when no scroll range is computed yet,
       matching the previous GetVerticalScrollRange → GetHeight fallback. */
    if (range <= 0.0f && i >= 0) {
        RECT r = UIWow_XmlComputeRect(i);
        range = r.h * 768.0f;
    }
    lua_pushnumber(L, range);
    return 1;
}
static int UIWow_LuaFrameSetVertexColor(lua_State *L) {
    int i = UIWow_FrameFromSelf(L); FLOAT r = (FLOAT)luaL_optnumber(L, 2, 1.0), g = (FLOAT)luaL_optnumber(L, 3, 1.0), b = (FLOAT)luaL_optnumber(L, 4, 1.0), a = (FLOAT)luaL_optnumber(L, 5, 1.0);
    if (i >= 0) wow_xml.elems[i].colors[ELEM_COLOR_VERTEX] = MAKE(COLOR32, (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f), (BYTE)(a * 255.0f));
    return 0;
}
static int UIWow_LuaFrameSetTexCoord(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    FLOAT left = (FLOAT)luaL_checknumber(L, 2), right = (FLOAT)luaL_checknumber(L, 3);
    FLOAT top  = (FLOAT)luaL_checknumber(L, 4), bot   = (FLOAT)luaL_checknumber(L, 5);
    RECT tc = MAKE(RECT, left, top, right - left, bot - top);
    if (i >= 0) {
        wow_xml.elems[i].texcoord = tc;
        wow_xml.elems[i].flags |= EF_HAS_TEXCOORD;
        return 0;
    }
    /* Called on a synthetic NormalTexture child — find parent button by name suffix. */
    lua_getfield(L, 1, "name");
    if (lua_isstring(L, -1)) {
        LPCSTR full = lua_tostring(L, -1);
        static LPCSTR const suffixes[] = { "NormalTexture", "PushedTexture", "HighlightTexture", NULL };
        for (int s = 0; suffixes[s]; s++) {
            size_t slen = strlen(suffixes[s]), flen = strlen(full);
            if (flen > slen && !strcmp(full + flen - slen, suffixes[s])) {
                char parent_name[256];
                snprintf(parent_name, sizeof(parent_name), "%.*s", (int)(flen - slen), full);
                int pi = UIWow_XmlFindByName(parent_name);
                if (pi >= 0) {
                    if (s == 0) { /* NormalTexture */
                        wow_xml.elems[pi].texcoord = tc;
                        wow_xml.elems[pi].flags |= EF_HAS_TEXCOORD;
                    } else if (s == 2) { /* HighlightTexture */
                        wow_xml.elems[pi].highlight_texcoord = tc;
                        wow_xml.elems[pi].flags |= EF_HAS_HIGHLIGHT_TEXCOORD;
                    }
                }
                break;
            }
        }
    }
    lua_pop(L, 1);
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
static int UIWow_LuaFrameSetFocus(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i < 0 || i >= wow_xml.count) return 0;
    wow_xml.focus = i;
    if (wow_xml.elems[i].type == WOW_XML_EDITBOX) {
        LPCSTR t = wow_xml.elems[i].texts[ELEM_TEXT];
        if (!t) {
            wow_xml.elems[i].texts[ELEM_TEXT] = calloc(1, 256);
            t = wow_xml.elems[i].texts[ELEM_TEXT];
        }
        wow_xml.text_input.text = (char *)t;
        wow_xml.text_input.size = 256;
        wow_xml.text_input.max_chars = 255;
        wow_xml.text_input.cursor = (DWORD)strlen(t ? t : "");
    }
    return 0;
}
static int UIWow_LuaFrameHighlightText(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameRegisterEvent(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetSequence(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    DWORD now = wow_ui.time;

    if (i >= 0) {
        wow_xml.elems[i].sequence = (DWORD)luaL_optinteger(L, 2, 0);
        wow_xml.elems[i].frame = 0;
        wow_xml.elems[i].oldframe = 0;
        wow_xml.elems[i].anim_start = now;
    }
    return 0;
}
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

static int UIWow_LuaFrameAdvanceTime(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) {
        uiWowXmlElem_t *e = &wow_xml.elems[i];
        DWORD now = wow_ui.time;

        e->oldframe = e->frame;
        e->frame = (now >= e->anim_start ? now - e->anim_start : 0);
    }
    return 0;
}

static int UIWow_LuaFrameSetFogColor(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) {
        BYTE r = (BYTE)(255.0f * (FLOAT)luaL_optnumber(L, 2, 0.0));
        BYTE g = (BYTE)(255.0f * (FLOAT)luaL_optnumber(L, 3, 0.0));
        BYTE b = (BYTE)(255.0f * (FLOAT)luaL_optnumber(L, 4, 0.0));
        wow_xml.elems[i].fog_color.r = r;
        wow_xml.elems[i].fog_color.g = g;
        wow_xml.elems[i].fog_color.b = b;
        wow_xml.elems[i].has_fog = true;
    }
    return 0;
}

static int UIWow_LuaFrameSetFogNear(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) {
        wow_xml.elems[i].fog_near = (FLOAT)luaL_optnumber(L, 2, 0.0f);
    }
    return 0;
}

static int UIWow_LuaFrameSetFogFar(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) {
        wow_xml.elems[i].fog_far = (FLOAT)luaL_optnumber(L, 2, 0.0f);
    }
    return 0;
}

static int UIWow_LuaFrameClearFog(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) {
        wow_xml.elems[i].has_fog = false;
    }
    return 0;
}

static int UIWow_LuaGetGlobalCompat(lua_State *L) {
    LPCSTR name = luaL_checkstring(L, 1);
    lua_getglobal(L, name);
    /* WoW Lua 5.1 formats integers without decimal (e.g. "Button1"), but
       Lua 5.2+/5.3+ number-to-string produces "Button1.0".  When the raw
       lookup misses, strip a trailing ".0" so WoW scripts that concatenate
       loop counters (e.g. "Button"..i) resolve correctly. */
    if (lua_isnil(L, -1)) {
        size_t len = strlen(name);
        if (len > 2 && name[len - 2] == '.' && name[len - 1] == '0') {
            char buf[256];
            memcpy(buf, name, len - 2); buf[len - 2] = '\0';
            lua_getglobal(L, buf);
            if (!lua_isnil(L, -1)) return 1;
            lua_pop(L, 1);
        }
    }
    return 1;
}

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
        { "SetValue", UIWow_LuaFrameNoop }, { "GetValue", UIWow_LuaFrameGetZero },
        { "SetMinMaxValues", UIWow_LuaFrameNoop }, { "GetMinMaxValues", UIWow_LuaFrameGetMinMax },
        { "UpdateScrollChildRect", UIWow_LuaFrameNoop }, { "SetScrollChild", UIWow_LuaFrameNoop },
        { "GetVerticalScrollRange", UIWow_LuaFrameGetVerticalScrollRange },
        { "SetVerticalScroll", UIWow_LuaFrameSetVerticalScroll }, { "GetVerticalScroll", UIWow_LuaFrameGetVerticalScroll },
        { "GetTextWidth", UIWow_LuaFrameGetWidth }, { "GetTextHeight", UIWow_LuaFrameGetHeight },
        { "GetStringWidth", UIWow_LuaFrameGetWidth }, { "GetStringHeight", UIWow_LuaFrameGetHeight },
        { "SetTexCoord", UIWow_LuaFrameSetTexCoord },
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
    lua_pushinteger(wow_ui.lua, e->id); lua_setfield(wow_ui.lua, -2, "id");
    luaL_getmetatable(wow_ui.lua, "UIWow.Frame"); lua_setmetatable(wow_ui.lua, -2);
    lua_setglobal(wow_ui.lua, name);
    if (e->type == WOW_XML_BUTTON) {
        char child_name[256];

        snprintf(child_name, sizeof(child_name), "%sText", name); UIWow_XmlPublishSyntheticFrame(child_name);
        snprintf(child_name, sizeof(child_name), "%sHighlightText", name); UIWow_XmlPublishSyntheticFrame(child_name);
        snprintf(child_name, sizeof(child_name), "%sNormalTexture", name); UIWow_XmlPublishSyntheticFrame(child_name);
        snprintf(child_name, sizeof(child_name), "%sPushedTexture", name); UIWow_XmlPublishSyntheticFrame(child_name);
        snprintf(child_name, sizeof(child_name), "%sHighlightTexture", name); UIWow_XmlPublishSyntheticFrame(child_name);
        snprintf(child_name, sizeof(child_name), "%sDisabledTexture", name); UIWow_XmlPublishSyntheticFrame(child_name);
        FOR_LOOP(i, sizeof(uiwow_button_part_name_fields) / sizeof(uiwow_button_part_name_fields[0])) {
            LPCSTR raw = e->texts[uiwow_button_part_name_fields[i]], dollar;
            if (!raw || !*raw) continue;
            dollar = strstr(raw, "$parent");
            if (dollar)
                snprintf(child_name, sizeof(child_name), "%.*s%s%s", (int)(dollar - raw), raw, name, dollar + 7);
            else
                snprintf(child_name, sizeof(child_name), "%s", raw);
            UIWow_XmlPublishSyntheticFrame(child_name);
        }
    }
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

/* Resolve $parent references inside an anchor relativeTo attribute. */
static void UIWow_XmlResolveRelativeTo(uiWowXmlElem_t *e, LPCSTR raw, LPCSTR parent_name) {
    char resolved[256];
    LPCSTR dollar = raw ? strstr(raw, "$parent") : NULL;
    if (dollar && parent_name && *parent_name) {
        snprintf(resolved, sizeof(resolved), "%.*s%s%s",
                 (int)(dollar - raw), raw, parent_name, dollar + 7);
        UIWow_ElemSetStr(e, ELEM_RELATIVE_NAME, resolved);
    } else if (raw && *raw) {
        UIWow_ElemSetStr(e, ELEM_RELATIVE_NAME, raw);
    }
    if (UIWow_ElemStr(e, ELEM_RELATIVE_NAME))
        e->relative_to = UIWow_XmlFindByName(e->texts[ELEM_RELATIVE_NAME]);
}

static void UIWow_XmlReadAnchor(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    /* $parent in relativeTo expands to the parent frame's name, not this frame's name. */
    LPCSTR parent_name = (e->parent >= 0 && e->parent < wow_xml.count)
                         ? wow_xml.elems[e->parent].texts[ELEM_NAME] : NULL;
    int anchor_index = 0;
    for (c = node->children; c; c = c->next) {
        xmlNodePtr a;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Anchors")) continue;
        for (a = c->children; a; a = a->next) {
            xmlChar *point, *relative, *relative_to;
            fpoint_t off = {0, 0};
            if (a->type != XML_ELEMENT_NODE || xmlStrcasecmp(a->name, BAD_CAST "Anchor")) continue;
            point = xmlGetProp(a, BAD_CAST "point");
            relative = xmlGetProp(a, BAD_CAST "relativePoint");
            relative_to = xmlGetProp(a, BAD_CAST "relativeTo");
            /* Parse offset. */
            for (xmlNodePtr o = a->children; o; o = o->next) {
                if (o->type != XML_ELEMENT_NODE || xmlStrcasecmp(o->name, BAD_CAST "Offset")) continue;
                for (xmlNodePtr abs = o->children; abs; abs = abs->next) {
                    xmlChar *x, *y;
                    if (abs->type != XML_ELEMENT_NODE || xmlStrcasecmp(abs->name, BAD_CAST "AbsDimension")) continue;
                    x = xmlGetProp(abs, BAD_CAST "x"); y = xmlGetProp(abs, BAD_CAST "y");
                    off.x = UIWow_XmlX(UIWow_XmlFloat(x, 0.0f));
                    off.y = -UIWow_XmlY(UIWow_XmlFloat(y, 0.0f));
                    SAFE_DELETE(x, xmlFree); SAFE_DELETE(y, xmlFree);
                }
            }
            if (anchor_index == 0) {
                UIWow_ElemSetStr(e, ELEM_POINT, point && *point ? (char const *)point : "CENTER");
                UIWow_ElemSetStr(e, ELEM_RELATIVE_POINT, relative && *relative ? (char const *)relative : e->texts[ELEM_POINT]);
                UIWow_XmlResolveRelativeTo(e, relative_to ? (char const *)relative_to : NULL, parent_name);
                e->offset = off;
                e->flags |= EF_HAS_ANCHOR;
            } else if (anchor_index == 1) {
                free(e->point2); e->point2 = (point && *point) ? strdup((char const *)point) : NULL;
                free(e->relative_point2); e->relative_point2 = (relative && *relative) ? strdup((char const *)relative) : (e->point2 ? strdup(e->point2) : NULL);
                e->offset2 = off;
                e->flags |= EF_HAS_ANCHOR2;
                /* Store the second anchor's relativeTo so the layout can reference a different frame. */
                if (relative_to && *relative_to) {
                    char resolved2[256];
                    LPCSTR dollar2 = strstr((char const *)relative_to, "$parent");
                    if (dollar2 && parent_name && *parent_name)
                        snprintf(resolved2, sizeof(resolved2), "%.*s%s%s",
                                 (int)(dollar2 - (char const *)relative_to), (char const *)relative_to,
                                 parent_name, dollar2 + 7);
                    else
                        snprintf(resolved2, sizeof(resolved2), "%s", (char const *)relative_to);
                    free(e->relative_name2); e->relative_name2 = strdup(resolved2);
                    e->relative_to2 = UIWow_XmlFindByName(resolved2);
                } else {
                    e->relative_to2 = e->relative_to; /* default: same frame as anchor 1 */
                }
            }
            SAFE_DELETE(point, xmlFree); SAFE_DELETE(relative, xmlFree); SAFE_DELETE(relative_to, xmlFree);
            anchor_index++;
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
    xmlChar *name = xmlGetProp(child, BAD_CAST "name");
    uiWowXmlElem_t temp;
    memset(&temp, 0, sizeof(temp));
    temp.texcoord = MAKE(RECT, 0, 0, 1, 1);
    UIWow_XmlInheritElem(&temp, (char const *)inherits);
    if (file && *file) UIWow_ElemSetStr(&temp, ELEM_FILE, (char const *)file);
    UIWow_XmlReadTexCoords(&temp, child);
    if (!xmlStrcasecmp(child->name, BAD_CAST "NormalTexture") && UIWow_ElemStr(&temp, ELEM_FILE)) {
        UIWow_ElemSetStr(e, ELEM_NORMAL_FILE, temp.texts[ELEM_FILE]);
        if (name && *name) UIWow_ElemSetStr(e, ELEM_NORMAL_NAME, (char const *)name);
        if (temp.flags & EF_HAS_TEXCOORD) { e->texcoord = temp.texcoord; e->flags |= EF_HAS_TEXCOORD; }
    } else if (!xmlStrcasecmp(child->name, BAD_CAST "PushedTexture") && UIWow_ElemStr(&temp, ELEM_FILE)) {
        UIWow_ElemSetStr(e, ELEM_PUSHED_FILE, temp.texts[ELEM_FILE]);
        if (name && *name) UIWow_ElemSetStr(e, ELEM_PUSHED_NAME, (char const *)name);
    } else if (!xmlStrcasecmp(child->name, BAD_CAST "HighlightTexture") && UIWow_ElemStr(&temp, ELEM_FILE)) {
        UIWow_ElemSetStr(e, ELEM_HIGHLIGHT_FILE, temp.texts[ELEM_FILE]);
        if (name && *name) UIWow_ElemSetStr(e, ELEM_HIGHLIGHT_NAME, (char const *)name);
        if (temp.flags & EF_HAS_TEXCOORD) {
            e->highlight_texcoord = temp.texcoord;
            e->flags |= EF_HAS_HIGHLIGHT_TEXCOORD;
        }
    }
    SAFE_DELETE(file, xmlFree); SAFE_DELETE(inherits, xmlFree); SAFE_DELETE(name, xmlFree);
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
            xmlChar *body; uiWowXmlStr_t field;
            if (s->type != XML_ELEMENT_NODE) continue;
            body = xmlNodeGetContent(s);
            if (!body) continue;
            if      (!xmlStrcasecmp(s->name, BAD_CAST "OnClick"))         field = ELEM_ON_CLICK;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnLoad"))          field = ELEM_ON_LOAD;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnShow"))          field = ELEM_ON_SHOW;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnEnter"))         field = ELEM_ON_ENTER;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnLeave"))         field = ELEM_ON_LEAVE;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnEnterPressed"))  field = ELEM_ON_ENTER_PRESSED;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnEscapePressed")) field = ELEM_ON_ESCAPE_PRESSED;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnTabPressed"))    field = ELEM_ON_TAB_PRESSED;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnMouseWheel"))   field = ELEM_ON_MOUSE_WHEEL;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnUpdateModel"))   field = ELEM_ON_UPDATE_MODEL;
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnUpdate"))       field = ELEM_ON_UPDATE;
            else { SAFE_DELETE(body, xmlFree); continue; }
            UIWow_ElemSetStr(e, field, (char const *)body);
            SAFE_DELETE(body, xmlFree);
        }
    }
}

static void UIWow_XmlReadShared(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlChar *file = xmlGetProp(node, BAD_CAST "file"), *hidden = xmlGetProp(node, BAD_CAST "hidden");
    xmlChar *text = xmlGetProp(node, BAD_CAST "text"), *virt = xmlGetProp(node, BAD_CAST "virtual");
    xmlChar *all = xmlGetProp(node, BAD_CAST "setAllPoints"), *password = xmlGetProp(node, BAD_CAST "password");
    xmlChar *id = xmlGetProp(node, BAD_CAST "id"), *wordwrap = xmlGetProp(node, BAD_CAST "wordWrap");
    if (file && *file) UIWow_ElemSetStr(e, ELEM_FILE, (char const *)file);
    if (text && *text) UIWow_ElemSetStr(e, ELEM_TEXT, (char const *)text);
    if (hidden && *hidden && !strcasecmp((char const *)hidden, "true")) e->flags |= EF_HIDDEN;
    if (virt && *virt && !strcasecmp((char const *)virt, "true")) e->flags |= EF_VIRTUAL;
    if (all && *all && !strcasecmp((char const *)all, "true")) e->flags |= EF_SET_ALL_PTS;
    if (password && *password && strcmp((char const *)password, "0")) e->flags |= EF_PASSWORD;
    if (id && *id) e->id = atoi((char const *)id);
    if (wordwrap && *wordwrap && !strcasecmp((char const *)wordwrap, "true")) e->flags |= EF_WORD_WRAP;
    SAFE_DELETE(file, xmlFree); SAFE_DELETE(hidden, xmlFree); SAFE_DELETE(text, xmlFree); SAFE_DELETE(virt, xmlFree);
    SAFE_DELETE(all, xmlFree); SAFE_DELETE(password, xmlFree); SAFE_DELETE(id, xmlFree); SAFE_DELETE(wordwrap, xmlFree);
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
        if (!xmlStrcasecmp(c->name, BAD_CAST "Frames") || !xmlStrcasecmp(c->name, BAD_CAST "ScrollChild")) {
            xmlNodePtr f; for (f = c->children; f; f = f->next) UIWow_XmlParseNode(f, parent, WOW_XML_LAYER_ARTWORK);
            continue;
        }
        /* ThumbTexture sits directly under Slider, outside <Frames>. */
        if (!xmlStrcasecmp(c->name, BAD_CAST "ThumbTexture")) {
            UIWow_XmlParseNode(c, parent, WOW_XML_LAYER_ARTWORK);
            continue;
        }
    }
}

/* Clone direct children of each inherited template into dst frame, re-substituting
   $parent (or the template name prefix) with the concrete frame's name.
   Recurses into each cloned child so that nested template hierarchies (e.g. a
   ScrollFrame template whose Slider child itself inherits a ScrollBar template
   with ScrollUpButton / ScrollDownButton children) are fully expanded. */
static void UIWow_XmlCloneTemplateChildren(LPCSTR inherits, int dst, LPCSTR dst_name) {
    char inames[256], *tok, *save = NULL;
    if (!inherits || !*inherits || !dst_name || !*dst_name) return;
    snprintf(inames, sizeof(inames), "%s", inherits);
    for (tok = strtok_r(inames, " ,", &save); tok; tok = strtok_r(NULL, " ,", &save)) {
        int tmpl = UIWow_XmlFindByName(tok);
        if (tmpl < 0) continue;
        LPCSTR tmpl_name = wow_xml.elems[tmpl].texts[ELEM_NAME];
        size_t tmpl_len = tmpl_name ? strlen(tmpl_name) : 0;
        int src_limit = wow_xml.count;
        FOR_LOOP(ci, src_limit) {
            uiWowXmlElem_t const *csrc = &wow_xml.elems[ci];
            char child_name[256] = "";
            if (!(csrc->flags & EF_USED) || csrc->parent != tmpl) continue;
            LPCSTR src_name = csrc->texts[ELEM_NAME];
            if (src_name && *src_name) {
                LPCSTR dollar = strstr(src_name, "$parent");
                if (dollar)
                    snprintf(child_name, sizeof(child_name), "%.*s%s%s",
                             (int)(dollar - src_name), src_name, dst_name, dollar + 7);
                else if (tmpl_len > 0 && strncmp(src_name, tmpl_name, tmpl_len) == 0)
                    snprintf(child_name, sizeof(child_name), "%s%s", dst_name, src_name + tmpl_len);
                else
                    snprintf(child_name, sizeof(child_name), "%s", src_name);
            } else {
                /* Nameless children (e.g. ThumbTexture under Slider) get a
                   synthetic $parent-prefixed name so they can be resolved. */
                snprintf(child_name, sizeof(child_name), "%s%s", dst_name, csrc->type == WOW_XML_TEXTURE ? "ThumbTexture" : "Child");
            }
            if (!child_name[0] || UIWow_XmlFindByName(child_name) >= 0) continue;
            int clone = UIWow_XmlPushElem(csrc->type, child_name, dst, csrc->draw_layer);
            if (clone < 0) continue;
            wow_xml.elems[clone] = *csrc;
            wow_xml.elems[clone].parent = dst;
            wow_xml.elems[clone].model = NULL;
            /* If the child anchored to the template parent, re-anchor to the concrete parent. */
            if (wow_xml.elems[clone].relative_to == tmpl) wow_xml.elems[clone].relative_to = dst;
            wow_xml.elems[clone].point2 = csrc->point2 ? strdup(csrc->point2) : NULL;
            wow_xml.elems[clone].relative_point2 = csrc->relative_point2 ? strdup(csrc->relative_point2) : NULL;
            wow_xml.elems[clone].relative_name2 = csrc->relative_name2 ? strdup(csrc->relative_name2) : NULL;
            FOR_LOOP(f, ELEM_STRING_COUNT) wow_xml.elems[clone].texts[f] = csrc->texts[f] ? strdup(csrc->texts[f]) : NULL;
            free(wow_xml.elems[clone].texts[ELEM_NAME]);
            wow_xml.elems[clone].texts[ELEM_NAME] = strdup(child_name);
            /* Re-resolve relativeTo: substitute the template-name prefix with the concrete name so
               intra-template references like "$parentRightButton" point to the cloned sibling. */
            if (tmpl_len > 0 && wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME]) {
                LPCSTR rel = wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME];
                if (strncmp(rel, tmpl_name, tmpl_len) == 0) {
                    char resolved_rel[256];
                    snprintf(resolved_rel, sizeof(resolved_rel), "%s%s", dst_name, rel + tmpl_len);
                    free(wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME]);
                    wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME] = strdup(resolved_rel);
                }
                int rel_idx = UIWow_XmlFindByName(wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME]);
                if (rel_idx >= 0) wow_xml.elems[clone].relative_to = rel_idx;
            }
            /* Re-resolve second anchor's relativeTo the same way. */
            if (tmpl_len > 0 && wow_xml.elems[clone].relative_name2) {
                LPCSTR rel2 = wow_xml.elems[clone].relative_name2;
                if (strncmp(rel2, tmpl_name, tmpl_len) == 0) {
                    char resolved_rel2[256];
                    snprintf(resolved_rel2, sizeof(resolved_rel2), "%s%s", dst_name, rel2 + tmpl_len);
                    free(wow_xml.elems[clone].relative_name2);
                    wow_xml.elems[clone].relative_name2 = strdup(resolved_rel2);
                }
                int rel2_idx = UIWow_XmlFindByName(wow_xml.elems[clone].relative_name2);
                if (rel2_idx >= 0) wow_xml.elems[clone].relative_to2 = rel2_idx;
            } else if (wow_xml.elems[clone].relative_to2 == tmpl) {
                wow_xml.elems[clone].relative_to2 = dst;
            }
            UIWow_XmlPublishFrame(clone);
            /* Recurse: if the template child itself has sub-children (e.g. a Slider
               template that owns ScrollUpButton/ScrollDownButton), clone those too
               so that getglobal("...ScrollBarScrollDownButton") resolves correctly.
               Use the original template child's name as the inherits key so we find
               grandchildren parented to that template child in wow_xml.elems. */
            if (src_name && *src_name)
                UIWow_XmlCloneTemplateChildren(src_name, clone, child_name);
        }
    }
}

static void UIWow_XmlParseNode(xmlNodePtr node, int parent, int draw_layer) {
    uiWowXmlType_t type; xmlChar *name_attr, *parent_attr, *inherits_attr; int idx;
    BOOL is_scrollframe = false, is_checkbutton = false;
    if (!node || node->type != XML_ELEMENT_NODE || !node->name) return;
    if (!xmlStrcasecmp(node->name, BAD_CAST "Layer")) { UIWow_XmlParseLayer(node, parent); return; }
    if (!xmlStrcasecmp(node->name, BAD_CAST "Frames") || !xmlStrcasecmp(node->name, BAD_CAST "Layers")) { UIWow_XmlParseChildren(node, parent); return; }
    if (!xmlStrcasecmp(node->name, BAD_CAST "Frame") ||
        !xmlStrcasecmp(node->name, BAD_CAST "ScrollFrame") ||
        !xmlStrcasecmp(node->name, BAD_CAST "Slider")) {
        type = WOW_XML_FRAME;
        if (!xmlStrcasecmp(node->name, BAD_CAST "ScrollFrame")) is_scrollframe = true;
    } else if (!xmlStrcasecmp(node->name, BAD_CAST "Model")) type = WOW_XML_MODEL;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Texture")) type = WOW_XML_TEXTURE;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "FontString")) type = WOW_XML_FONTSTRING;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Button") ||
             !xmlStrcasecmp(node->name, BAD_CAST "CheckButton")) {
        type = WOW_XML_BUTTON;
        is_checkbutton = !xmlStrcasecmp(node->name, BAD_CAST "CheckButton");
    }
    else if (!xmlStrcasecmp(node->name, BAD_CAST "EditBox")) type = WOW_XML_EDITBOX;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "NormalTexture") || !xmlStrcasecmp(node->name, BAD_CAST "PushedTexture") || !xmlStrcasecmp(node->name, BAD_CAST "DisabledTexture") || !xmlStrcasecmp(node->name, BAD_CAST "HighlightTexture") || !xmlStrcasecmp(node->name, BAD_CAST "ThumbTexture")) type = WOW_XML_TEXTURE;
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
    if (is_scrollframe) wow_xml.elems[idx].flags |= EF_IS_SCROLLFRAME;
    if (is_checkbutton) wow_xml.elems[idx].flags |= EF_CHECKBUTTON;
    UIWow_XmlCloneTemplateChildren((char const *)inherits_attr, idx, resolved_name[0] ? resolved_name : NULL);
    UIWow_XmlInheritElem(&wow_xml.elems[idx], (char const *)inherits_attr); SAFE_DELETE(inherits_attr, xmlFree);
    if (parent_attr && *parent_attr) {
        uiWowXmlElem_t *e = &wow_xml.elems[idx]; int named_parent;
        UIWow_ElemSetStr(e, ELEM_PARENT_NAME, (char const *)parent_attr);
        named_parent = UIWow_XmlFindByName(e->texts[ELEM_PARENT_NAME]); if (named_parent >= 0) e->parent = named_parent;
    }
    SAFE_DELETE(parent_attr, xmlFree);
    if (type == WOW_XML_EDITBOX) wow_xml.elems[idx].flags |= EF_FOCUSABLE;
    if (s_current_xml_path[0]) UIWow_ElemSetStr(&wow_xml.elems[idx], ELEM_SOURCE_FILE, s_current_xml_path);
    UIWow_XmlReadShared(&wow_xml.elems[idx], node); UIWow_XmlPublishFrame(idx); UIWow_XmlParseChildren(node, idx);
    /* After parsing a ScrollFrame, mark its Slider child and all descendants as scrollbar parts
       so they are excluded from scroll offset and clipping. */
    if (is_scrollframe) {
        FOR_LOOP(j, wow_xml.count) {
            uiWowXmlElem_t *c = &wow_xml.elems[j];
            if (!(c->flags & EF_USED) || c->parent != idx) continue;
            LPCSTR cn = c->texts[ELEM_NAME];
            BOOL is_scroll_child = (cn && strstr(cn, "ScrollChild"));
            if (is_scroll_child) continue;
            c->flags |= EF_SCROLLBAR_PART;
            FOR_LOOP(k, wow_xml.count) {
                int pp = k;
                while (pp >= 0 && pp < wow_xml.count) {
                    if (pp == j) { wow_xml.elems[k].flags |= EF_SCROLLBAR_PART; break; }
                    pp = wow_xml.elems[pp].parent;
                }
            }
        }
    }
    if (UIWow_ElemStr(&wow_xml.elems[idx], ELEM_ON_LOAD))
        wow_xml.elems[idx].flags |= EF_PENDING_ONLOAD;
}

static BOOL UIWow_XMLProcessFile(LPCSTR path, int depth);

static void UIWow_XMLProcessTopLevel(LPCSTR path, xmlNodePtr root, int depth) {
    xmlNodePtr n;
    snprintf(s_current_xml_path, sizeof(s_current_xml_path), "%s", path ? path : "");
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

int UIWow_XmlFindByNamePub(LPCSTR name) { return UIWow_XmlFindByName(name); }
void UIWow_XmlComputeRectPub(int idx, FLOAT *x, FLOAT *y, FLOAT *w, FLOAT *h) {
    RECT r = (idx >= 0 && idx < wow_xml.count) ? UIWow_XmlComputeRect(idx) : MAKE(RECT, 0, 0, 0, 0);
    if (x) *x = r.x; if (y) *y = r.y; if (w) *w = r.w; if (h) *h = r.h;
}

/* Drop the injected character-create model when XML runtime state is rebuilt. */
static void UIWow_XMLReleaseCharCustomizeModel(void) {
    if (wow_ui.renderer && wow_ui.renderer->ReleaseModel)
        SAFE_DELETE(wow_ui.char_customize_model, wow_ui.renderer->ReleaseModel);
    wow_ui.char_customize_model_path[0] = '\0';
    wow_ui.char_customize_frame_idx = -1;
    wow_ui.char_select_frame_idx = -1;
    wow_ui.selected_char_idx = -1;
}

void UIWow_XMLInvalidateCharCustomizeModel(void) {
    if (wow_ui.renderer && wow_ui.renderer->ReleaseModel)
        SAFE_DELETE(wow_ui.char_customize_model, wow_ui.renderer->ReleaseModel);
    wow_ui.char_customize_model_path[0] = '\0';
}

void UIWow_XMLInitRuntime(void) {
    memset(&wow_xml, 0, sizeof(wow_xml)); wow_xml.focus = -1; wow_xml.pressed_button = -1; wow_xml.hovered_button = -1;
    wow_xml.drag.scrollbar_idx = -1; wow_ui.char_customize_frame_idx = -1;
    wow_ui.char_select_frame_idx = -1; wow_ui.selected_char_idx = -1;
    UIWow_XMLInstallLuaCompat();
}
void UIWow_XMLShutdownRuntime(void) {
    if (wow_ui.renderer && wow_ui.renderer->ReleaseModel) FOR_LOOP(i, wow_xml.count) SAFE_DELETE(wow_xml.elems[i].model, wow_ui.renderer->ReleaseModel);
    UIWow_XMLReleaseCharCustomizeModel();
    UIWow_XMLFreeElems();
    memset(&wow_xml, 0, sizeof(wow_xml)); wow_xml.focus = -1; wow_xml.pressed_button = -1;
    wow_xml.hovered_button = -1; wow_xml.drag.scrollbar_idx = -1;
}

BOOL UIWow_XMLLoadGlueFromToc(LPCSTR toc_path) {
    if (!wow_ui.lua) { UIWow_Printf("UIWow: XML runtime requires active lua_State\n"); return false; }
    if (!wow_xml.lua_ready) UIWow_XMLInstallLuaCompat();
    UIWow_XMLFreeElems();
    UIWow_XMLReleaseCharCustomizeModel();
    memset(wow_xml.elems, 0, sizeof(wow_xml.elems)); wow_xml.count = 0; wow_xml.focus = -1;
    wow_xml.pressed_button = -1; wow_xml.hovered_button = -1; wow_xml.drag.scrollbar_idx = -1;
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
        if (e->relative_name2) {
            int rel2 = UIWow_XmlFindByName(e->relative_name2);
            if (rel2 >= 0) e->relative_to2 = rel2;
        }
    }
    /* Fire OnLoad for every frame that registered one, now that all Lua files are loaded. */
    FOR_LOOP(i, wow_xml.count) {
        uiWowXmlElem_t *e = &wow_xml.elems[i];
        if (e->flags & EF_PENDING_ONLOAD) {
            e->flags &= ~EF_PENDING_ONLOAD;
            UIWow_XMLRunFrameScript(i, e->texts[ELEM_ON_LOAD], "OnLoad");
        }
    }
    /* Show the hidden random-name button so players can generate names. */
    {
        int rn = UIWow_XmlFindByName("CharacterCreateRandomName");
        if (rn >= 0) UIWow_XMLSetShown(rn, true);
    }
    UIWow_Printf("UIWow: FrameXML loaded from %s (elements=%d)\n", toc_path, wow_xml.count);
    return wow_xml.count > 0;
}

static void UIWow_XMLRunFrameScript(int idx, LPCSTR script, LPCSTR event_name) {
    char chunk[512];
    LPCSTR name, src_file;
    if (!wow_ui.lua || idx < 0 || idx >= wow_xml.count || !script || !*script) return;
    UIWow_XmlPublishFrame(idx);
    name     = wow_xml.elems[idx].texts[ELEM_NAME];
    src_file = wow_xml.elems[idx].texts[ELEM_SOURCE_FILE];
    lua_getglobal(wow_ui.lua, name ? name : ""); lua_setglobal(wow_ui.lua, "this");
    lua_pushstring(wow_ui.lua, event_name ? event_name : ""); lua_setglobal(wow_ui.lua, "event");
    if (src_file)
        snprintf(chunk, sizeof(chunk), "=%s:%s (%s)",
                 name && name[0] ? name : "<anon>", event_name ? event_name : "Script", src_file);
    else
        snprintf(chunk, sizeof(chunk), "=%s:%s",
                 name && name[0] ? name : "<anon>", event_name ? event_name : "Script");
    if (luaL_loadbuffer(wow_ui.lua, script, strlen(script), chunk) != LUA_OK) {
        UIWow_Printf("UIWow Lua load: %s\n", lua_tostring(wow_ui.lua, -1)); lua_pop(wow_ui.lua, 1);
    } else {
        UIWow_LuaPCall(0);
    }
    lua_pushnil(wow_ui.lua); lua_setglobal(wow_ui.lua, "this");
}

static int UIWow_XMLHitFrame(FLOAT x, FLOAT y);

/* Compute cumulative vertical scroll offset for element idx by walking up the
   parent chain and summing all ScrollFrame scroll_y values. */
static FLOAT UIWow_XMLScrollOffset(int idx) {
    FLOAT total = 0.0f;
    int p = idx >= 0 && idx < wow_xml.count ? wow_xml.elems[idx].parent : -1;
    while (p >= 0 && p < wow_xml.count) {
        if (wow_xml.elems[p].flags & EF_IS_SCROLLFRAME)
            total += wow_xml.scroll[p].scroll_y;
        p = wow_xml.elems[p].parent;
    }
    return total;
}

/* Find the nearest ancestor ScrollFrame for element idx, or return -1.
   When found, *clip is set to that ScrollFrame's computed rect. */
static int UIWow_XMLScrollClipAncestor(int idx, RECT *clip) {
    int p = idx >= 0 && idx < wow_xml.count ? wow_xml.elems[idx].parent : -1;
    while (p >= 0 && p < wow_xml.count) {
        if (wow_xml.elems[p].flags & EF_IS_SCROLLFRAME) {
            if (clip) *clip = UIWow_XmlComputeRect(p);
            return p;
        }
        p = wow_xml.elems[p].parent;
    }
    return -1;
}

/* Current scroll clip state, set per-element in UIWow_XMLDraw. */
static BOOL s_has_scroll_clip;
static RECT s_scroll_clip;

/* Compute scroll range for all ScrollFrames: the vertical extent of their
   children minus the viewport height. Called once per frame. */
/* Recursively expand content bounds for element idx and all descendants. */
static void UIWow_XMLExpandContentBounds(int idx, FLOAT *min_y, FLOAT *max_y) {
    RECT cr = UIWow_XmlComputeRect(idx);
    if (cr.y < *min_y) *min_y = cr.y;
    if (cr.y + cr.h > *max_y) *max_y = cr.y + cr.h;
    FOR_LOOP(j, wow_xml.count) {
        uiWowXmlElem_t const *c = &wow_xml.elems[j];
        if (!(c->flags & EF_USED) || c->parent != idx) continue;
        UIWow_XMLExpandContentBounds(j, min_y, max_y);
    }
}

static void UIWow_XMLComputeScrollRanges(void) {
    FOR_LOOP(i, wow_xml.count) {
        uiWowXmlElem_t *e = &wow_xml.elems[i];
        RECT vr;
        FLOAT min_y, max_y;
        if (!(e->flags & EF_USED) || !(e->flags & EF_IS_SCROLLFRAME)) continue;
        vr = UIWow_XmlComputeRect(i);
        min_y = vr.y + vr.h; /* start at viewport bottom */
        max_y = vr.y;         /* start at viewport top */
        FOR_LOOP(j, wow_xml.count) {
            uiWowXmlElem_t *c = &wow_xml.elems[j];
            if (!(c->flags & EF_USED) || c->parent != (int)i) continue;
            /* Skip the ScrollBar child (Slider with narrow width). */
            if (c->type == WOW_XML_FRAME && c->size.w > 0 && c->size.h > 0 && c->size.w < c->size.h * 0.5f)
                continue;
            UIWow_XMLExpandContentBounds(j, &min_y, &max_y);
        }
        wow_xml.scroll[i].scroll_range = MAX(0.0f, (max_y - min_y) - vr.h);
        /* Clamp scroll_y to valid range. */
        if (wow_xml.scroll[i].scroll_y > wow_xml.scroll[i].scroll_range)
            wow_xml.scroll[i].scroll_y = wow_xml.scroll[i].scroll_range;
    }
}

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
                                           .rotate = rotate,
                                           .hasClip = s_has_scroll_clip,
                                           .clip = s_scroll_clip));
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
            FLOAT tw_px = e->tile.w > 0.0f ? e->tile.w * 1024.0f : (FLOAT)size.width;
            FLOAT th_px = e->tile.h > 0.0f ? e->tile.h * 1024.0f : (FLOAT)size.height;
            if (size.width > 0) uv.w = tw_px / (FLOAT)size.width;
            if (size.height > 0) uv.h = th_px / (FLOAT)size.height;
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

/* Return the live character actor for Blizzard's background model scene.
   Handles both char-create (customize) and char-select screens. */
static LPMODEL UIWow_XMLCharCustomizeModel(int i) {
    char path[MAX_PATHLEN];
    BOOL is_char_select = (i == wow_ui.char_select_frame_idx);
    BOOL is_char_customize = (i == wow_ui.char_customize_frame_idx);

    if ((!is_char_select && !is_char_customize) || !wow_ui.renderer || !wow_ui.renderer->LoadModel)
        return NULL;
    if (is_char_select)
        UIWow_GetCharacterSelectModelPath(path, sizeof(path));
    else
        UIWow_GetCharacterCreateModelPath(path, sizeof(path));
    if (!path[0]) return NULL;
    if (!wow_ui.char_customize_model || strcmp(wow_ui.char_customize_model_path, path)) {
        if (wow_ui.char_customize_model && wow_ui.renderer->ReleaseModel)
            wow_ui.renderer->ReleaseModel(wow_ui.char_customize_model);
        wow_ui.char_customize_model = wow_ui.renderer->LoadModel(path);
        snprintf(wow_ui.char_customize_model_path, sizeof(wow_ui.char_customize_model_path), "%s", path);
        if (!wow_ui.char_customize_model)
            UIWow_WarnOnce(WOW_UI_WARN_NO_CHAR_MODEL,
                           "UIWow: failed to load character model %s\n", path);
    }
    return wow_ui.char_customize_model;
}

/* Draw one XML frame's own layer. whoa draws a frame's batches before recursing into child frames. */
static void UIWow_XMLDrawElementLayer(int i, int layer, int hovered_button) {
        uiWowXmlElem_t *e = &wow_xml.elems[i]; RECT r; RECT uv = MAKE(RECT, 0, 0, 1, 1); char text[512];
        COLOR32 text_color = e->colors[ELEM_COLOR_TEXT];
        BOOL pressed = e->type == WOW_XML_BUTTON && wow_xml.pressed_button == i;
        BOOL hovered = e->type == WOW_XML_BUTTON && hovered_button == i;
        int draw_layer = e->type == WOW_XML_MODEL ? WOW_XML_LAYER_BACKGROUND : e->draw_layer;
        LPCSTR file = e->texts[ELEM_FILE], normal_file = e->texts[ELEM_NORMAL_FILE], pushed_file = e->texts[ELEM_PUSHED_FILE];
        LPCSTR highlight_file = e->texts[ELEM_HIGHLIGHT_FILE], elem_text = e->texts[ELEM_TEXT];
        FLOAT scroll_off_y = 0.0f;
        RECT clip_rect = {0};
        BOOL has_clip = false;
        if (!(e->flags & EF_USED) || !UIWow_XMLIsVisible(i)) return;
        /* Backdrops draw at BACKGROUND layer regardless of the frame's own draw_layer. */
        if (layer == WOW_XML_LAYER_BACKGROUND && (e->type == WOW_XML_FRAME || e->type == WOW_XML_BUTTON || e->type == WOW_XML_EDITBOX)) {
            r = UIWow_XmlComputeRect(i);
            s_has_scroll_clip = false;
            UIWow_XMLDrawBackdrop(e, &r);
        }
        if (draw_layer != layer) return;
        r = UIWow_XmlComputeRect(i);
        /* Compute scroll offset for descendants of ScrollFrames. Skip the
           ScrollFrame itself (it is the viewport) and its direct ScrollBar
           child (Slider) which must remain fixed. */
        /* Walk parent chain for scroll offset and clip. */
        {
            int anc = -1;
            FLOAT total_off = 0.0f;
            int p = e->parent;
            while (p >= 0 && p < wow_xml.count) {
                if (wow_xml.elems[p].flags & EF_IS_SCROLLFRAME) {
                    total_off += wow_xml.scroll[p].scroll_y;
                    if (anc < 0) anc = p;
                }
                p = wow_xml.elems[p].parent;
            }
            /* Don't scroll the ScrollFrame itself, its direct Slider child, or the Slider's children
               (ThumbTexture, UpButton, DownButton). They must remain fixed. */
            BOOL is_scrollbar_part = (e->flags & EF_SCROLLBAR_PART) != 0;
            if ((e->flags & EF_IS_SCROLLFRAME) || is_scrollbar_part) {
                scroll_off_y = 0.0f;
            } else {
                scroll_off_y = total_off;
            }
            if (anc >= 0 && !is_scrollbar_part) {
                clip_rect = UIWow_XmlComputeRect(anc);
                has_clip = true;
            }
        }
        r.y -= scroll_off_y;
        s_has_scroll_clip = has_clip;
        s_scroll_clip = clip_rect;
        if (e->type == WOW_XML_BUTTON) {
            text_color = !(e->flags & EF_ENABLED) ? e->button_text_colors[WOW_XML_BUTTON_TEXT_DISABLED] :
                         (hovered ? e->button_text_colors[WOW_XML_BUTTON_TEXT_HIGHLIGHT] : e->button_text_colors[WOW_XML_BUTTON_TEXT_NORMAL]);
        }
        if (pressed) {
            r.x += UIWow_XmlX(1.0f);
            r.y += UIWow_XmlY(1.0f);
        }
        if (e->type == WOW_XML_BUTTON && UIWow_ElemStr(e, ELEM_ON_UPDATE))
            UIWow_XMLRunFrameScript(i, e->texts[ELEM_ON_UPDATE], "OnUpdate");
        if (e->type == WOW_XML_MODEL && file && file[0]) {
            if (UIWow_ElemStr(e, ELEM_ON_UPDATE_MODEL))
                UIWow_XMLRunFrameScript(i, e->texts[ELEM_ON_UPDATE_MODEL], "OnUpdateModel");
            if (!e->model && wow_ui.renderer->LoadModel) e->model = wow_ui.renderer->LoadModel(file);
            if (e->model && wow_ui.renderer->RenderFrame) {
                BOOL is_char_select = (i == wow_ui.char_select_frame_idx);
                renderEntity_t entity = {0};
                entity.model = e->model;
                entity.attached_model = UIWow_XMLCharCustomizeModel(i);
                entity.appearance = is_char_select ? UIWow_GetCharacterSelectAppearance()
                                                   : UIWow_GetCharacterCreateAppearance();
                entity.frame = e->frame;
                entity.oldframe = e->oldframe;
                entity.scale = 1.0f;
                entity.angle = is_char_select ? 0.0f
                                              : (FLOAT)DEG2RAD(UIWow_GetCharacterCreateFacing());
                entity.flags = RF_NO_SHADOW | RF_NO_FOGOFWAR | RF_NO_LIGHTING;
                if (wow_ui.renderer->SetEntityAnimFrame) {
                    char anim[16];

                    snprintf(anim, sizeof(anim), "%u", (unsigned)e->sequence);
                    wow_ui.renderer->SetEntityAnimFrame(entity.model, anim, &entity);
                }

                viewDef_t viewdef = {0};
                viewdef.viewport = r;
                viewdef.rdflags = RDF_NOWORLDMODEL | RDF_NOFRUSTUMCULL | RDF_NOFOG |
                                  RDF_USE_ENTITY_CAMERA;
                viewdef.num_entities = 1;
                viewdef.entities = &entity;

                wow_ui.renderer->RenderFrame(&viewdef);
            }
            else if (!wow_ui.renderer->LoadModel)
                UIWow_WarnOnce(WOW_UI_WARN_NO_MODEL_LOADER,
                               "UIWow: renderer has no model loader; XML model frames skipped\n");
            else if (!wow_ui.renderer->RenderFrame)
                UIWow_WarnOnce(WOW_UI_WARN_NO_MODEL_LOADER,
                               "UIWow: renderer has no frame renderer; XML model frames skipped\n");
        }
        if ((file && file[0] && e->type == WOW_XML_TEXTURE) || (e->type == WOW_XML_BUTTON && ((normal_file && normal_file[0]) || (file && file[0])))) {
            LPCSTR src = (e->type == WOW_XML_BUTTON && pressed && pushed_file && pushed_file[0]) ? pushed_file :
                         ((e->type == WOW_XML_BUTTON && normal_file && normal_file[0]) ? normal_file : file);
            LPTEXTURE t = UIWow_LoadTexture(src);
            if (e->flags & EF_HAS_TEXCOORD) uv = e->texcoord;
            /* Scrollbar thumb: reposition ThumbTexture based on scroll_y / scroll_range. */
            if (e->type == WOW_XML_TEXTURE && e->parent >= 0 && e->parent < wow_xml.count) {
                uiWowXmlElem_t *par = &wow_xml.elems[e->parent];
                if (par->parent >= 0 && par->parent < wow_xml.count && (wow_xml.elems[par->parent].flags & EF_IS_SCROLLFRAME)) {
                    int sf = par->parent;
                    FLOAT range = wow_xml.scroll[sf].scroll_range;
                    if (range > 0.0f) {
                        RECT pr = UIWow_XmlComputeRect(e->parent);
                        FLOAT track_h = pr.h;
                        FLOAT thumb_h = r.h;
                        if (track_h > thumb_h) {
                            FLOAT frac = wow_xml.scroll[sf].scroll_y / range;
                            r.y = pr.y + frac * (track_h - thumb_h);
                        }
                    }
                }
            }
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
        if (((elem_text && elem_text[0]) || (e->type == WOW_XML_EDITBOX && wow_xml.focus == i)) &&
            (e->type == WOW_XML_FONTSTRING || e->type == WOW_XML_EDITBOX || e->type == WOW_XML_BUTTON)) {
            LPCFONT f = UIWow_LoadFont((DWORD)e->font_size);
            /* For FontStrings with no authored height, measure the wrapped text height so that
               sibling FontStrings anchored to BOTTOMLEFT of this one are positioned correctly. */
            if (f && e->type == WOW_XML_FONTSTRING && e->size.h == 0 && wow_ui.renderer->GetTextSize) {
                LPCSTR display = UIWow_XMLDisplayText(e, text, sizeof(text));
                VECTOR2 sz = wow_ui.renderer->GetTextSize(&MAKE(drawText_t,
                                                               .font = f, .text = display,
                                                               .rect = r, .textWidth = r.w,
                                                               .lineHeight = 1.33f,
                                                               .wordWrap = (e->flags & EF_WORD_WRAP) != 0));
                e->measured_h = sz.y;
                /* Recompute r now that measured_h is known, so tr below uses the updated height. */
                r = UIWow_XmlComputeRect(i);
                r.y -= scroll_off_y;
            }
            RECT tr = MAKE(RECT,
                           r.x + e->text_inset.w + e->text_off.x,
                           r.y + e->text_off.y,
                           r.w - e->text_inset.w,
                           r.h - e->text_inset.h);
            LPCSTR display = UIWow_XMLDisplayText(e, text, sizeof(text));
            if (f) {
                drawText_t dt = MAKE(drawText_t,
                                     .font = f,
                                     .text = display,
                                     .rect = tr,
                                     .color = MAKE(COLOR32,
                                                   text_color.r,
                                                   text_color.g,
                                                   text_color.b,
                                                   (BYTE)(text_color.a * e->alpha)),
                                     .textWidth = tr.w,
                                     .lineHeight = 1.33f,
                                     .wordWrap = (e->flags & EF_WORD_WRAP) != 0,
                                     .halign = e->type == WOW_XML_EDITBOX ? FONT_JUSTIFYLEFT : e->halign,
                                     .valign = e->valign,
                                     .hasClip = has_clip,
                                     .clip = clip_rect);
                wow_ui.renderer->DrawText(&dt);
                if (e->type == WOW_XML_EDITBOX && wow_xml.focus == i)
                    UI_DrawTextInputCursor(wow_ui.renderer, &dt, display, wow_xml.text_input.cursor, text_color);
            }
        }
}

static void UIWow_XMLDrawTreeLayer(int i, int layer, int hovered_button) {
    if (!(wow_xml.elems[i].flags & EF_USED) || !UIWow_XMLIsVisible(i)) return;
    UIWow_XMLDrawElementLayer(i, layer, hovered_button);
    FOR_LOOP(j, wow_xml.count) {
        if (wow_xml.elems[j].parent == i)
            UIWow_XMLDrawTreeLayer((int)j, layer, hovered_button);
    }
}

static void UIWow_XMLDrawTree(int i, int hovered_button) {
    for (int layer = WOW_XML_LAYER_BACKGROUND; layer <= WOW_XML_LAYER_OVERLAY; layer++)
        UIWow_XMLDrawTreeLayer(i, layer, hovered_button);
}

void UIWow_XMLDraw(void) {
    UIWow_EnsureRenderer(); if (!wow_ui.renderer) return;
    UIWow_XMLComputeScrollRanges();
    FOR_LOOP(i, wow_xml.count) {
        if (wow_xml.elems[i].parent < 0)
            UIWow_XMLDrawTree((int)i, wow_xml.hovered_button);
    }
}

static BOOL UIWow_XMLPointInRect(FLOAT x, FLOAT y, LPCRECT r) {
    return r && x >= r->x && y >= r->y && x <= r->x + r->w && y <= r->y + r->h;
}

/* Find the ScrollFrame under the mouse position (in FDF coords). */
static int UIWow_XMLHitScrollFrame(FLOAT x, FLOAT y) {
    for (int i = wow_xml.count - 1; i >= 0; i--) {
        uiWowXmlElem_t const *e = &wow_xml.elems[i];
        RECT r;
        if (!(e->flags & EF_USED) || !(e->flags & EF_IS_SCROLLFRAME)) continue;
        if (!UIWow_XMLIsVisible(i)) continue;
        r = UIWow_XmlComputeRect(i);
        if (UIWow_XMLPointInRect(x, y, &r)) return i;
    }
    return -1;
}

/* Detect scrollbar sub-parts by name suffix convention:
   *ScrollUpButton → increment (scroll up), *ScrollDownButton → decrement (scroll down),
   *Thumb → draggable thumb. Returns 1=up, 2=down, 3=thumb, 0=not a scrollbar part. */
static int UIWow_XMLScrollBarPart(uiWowXmlElem_t const *e) {
    LPCSTR name = e->texts[ELEM_NAME];
    if (!name || !*name) return 0;
    size_t len = strlen(name);
    if (len >= 14 && !strcmp(name + len - 14, "ScrollUpButton")) return 1;
    if (len >= 16 && !strcmp(name + len - 16, "ScrollDownButton")) return 2;
    if (len >= 12 && !strcmp(name + len - 12, "ThumbTexture")) return 3;
    if (len >= 5 && !strcmp(name + len - 5, "Thumb")) return 3;
    return 0;
}

/* Find the ScrollFrame ancestor for a scrollbar part element (walks up through Slider). */
static int UIWow_XMLScrollBarParent(int idx) {
    int p = idx >= 0 && idx < wow_xml.count ? wow_xml.elems[idx].parent : -1;
    while (p >= 0 && p < wow_xml.count) {
        if (wow_xml.elems[p].flags & EF_IS_SCROLLFRAME)
            return p;
        p = wow_xml.elems[p].parent;
    }
    return -1;
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

BOOL UIWow_XMLMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 mouse = UIWow_MouseFdf(x, y);
    FLOAT fdf_x = mouse.x, fdf_y = mouse.y;
    int wheel_y = event == UI_MOUSE_SCROLL ? UI_MOUSE_PARAM_Y(param) : 0;
    int hit;

    wow_xml.hovered_button = -1;
    hit = UIWow_XMLHitFrame(fdf_x, fdf_y);
    if (hit >= 0 && wow_xml.elems[hit].type == WOW_XML_BUTTON) {
        wow_xml.hovered_button = hit;
    }

    /* Mouse wheel: scroll the ScrollFrame under the cursor. */
    if (event == UI_MOUSE_SCROLL && wheel_y) {
        int sf = UIWow_XMLHitScrollFrame(fdf_x, fdf_y);
        if (sf >= 0) {
            RECT vr = UIWow_XmlComputeRect(sf);
            FLOAT step = vr.h * 0.3f; /* scroll by 30% of viewport per notch */
            if (wheel_y > 0) wow_xml.scroll[sf].scroll_y = MAX(0.0f, wow_xml.scroll[sf].scroll_y - step);
            else wow_xml.scroll[sf].scroll_y = MIN(wow_xml.scroll[sf].scroll_range, wow_xml.scroll[sf].scroll_y + step);
            /* Run OnMouseWheel script if present. */
            if (UIWow_ElemStr(&wow_xml.elems[sf], ELEM_ON_MOUSE_WHEEL))
                UIWow_XMLRunFrameScript(sf, wow_xml.elems[sf].texts[ELEM_ON_MOUSE_WHEEL], "OnMouseWheel");
            return true;
        }
        return false;
    }

    /* Mouse motion: handle scrollbar thumb drag, then return.
     * Must not fall through to the mouse-up block — that clears pressed_button,
     * which would drop a button press if a motion event arrives between DOWN and UP. */
    if (event == UI_MOUSE_MOVE) {
        if (wow_xml.drag.scrollbar_idx >= 0) {
            int sf = wow_xml.drag.scrollbar_idx;
            RECT vr = UIWow_XmlComputeRect(sf);
            FLOAT mouse_delta = fdf_y - wow_xml.drag.start_mouse_y;
            FLOAT scroll_range = wow_xml.scroll[sf].scroll_range;
            if (vr.h > 0.0f && scroll_range > 0.0f) {
                FLOAT scroll_delta = (mouse_delta / vr.h) * scroll_range;
                wow_xml.scroll[sf].scroll_y = MIN(scroll_range, MAX(0.0f, wow_xml.drag.start_value + scroll_delta));
            }
            return true;
        }
        return false;
    }

    /* Also check if we hit a scrollbar part (thumb, up/down button). */
    if (hit < 0) {
        for (int i = wow_xml.count - 1; i >= 0; i--) {
            uiWowXmlElem_t const *e = &wow_xml.elems[i];
            if (!UIWow_XMLIsVisible(i)) continue;
            if (e->type == WOW_XML_BUTTON || e->type == WOW_XML_TEXTURE) {
                int part = UIWow_XMLScrollBarPart(e);
                if (part) {
                    RECT r = UIWow_XmlComputeRect(i);
                    if (UIWow_XMLPointInRect(fdf_x, fdf_y, &r)) { hit = i; break; }
                }
            }
        }
    }

    if (event == UI_MOUSE_UP) {
        int pressed = wow_xml.pressed_button;
        wow_xml.pressed_button = -1;
        /* End scrollbar drag. */
        if (wow_xml.drag.scrollbar_idx >= 0) {
            wow_xml.drag.scrollbar_idx = -1;
            return true;
        }
        if (param == 1 && pressed >= 0 && hit == pressed && wow_xml.elems[pressed].type == WOW_XML_BUTTON &&
            (wow_xml.elems[pressed].flags & EF_ENABLED) && wow_ui.lua &&
            UIWow_ElemStr(&wow_xml.elems[pressed], ELEM_ON_CLICK)) {
            UIWow_Printf("UIWow: OnClick dispatch idx=%d name='%s' checkbtn=%d checked=%d\n",
                         pressed, wow_xml.elems[pressed].texts[ELEM_NAME] ? wow_xml.elems[pressed].texts[ELEM_NAME] : "?",
                         !!(wow_xml.elems[pressed].flags & EF_CHECKBUTTON),
                         !!(wow_xml.elems[pressed].flags & EF_CHECKED));
            if (wow_xml.elems[pressed].flags & EF_CHECKBUTTON)
                wow_xml.elems[pressed].flags ^= EF_CHECKED;
            UIWow_XMLRunFrameScript(pressed, wow_xml.elems[pressed].texts[ELEM_ON_CLICK], "OnClick");
            return true;
        }
        if (param == 1 && pressed >= 0 && hit == pressed) {
            UIWow_Printf("UIWow: OnClick MISS idx=%d name='%s' type=%d enabled=%d has_onclick=%d\n",
                         pressed, wow_xml.elems[pressed].texts[ELEM_NAME] ? wow_xml.elems[pressed].texts[ELEM_NAME] : "?",
                         wow_xml.elems[pressed].type,
                         !!(wow_xml.elems[pressed].flags & EF_ENABLED),
                         !!UIWow_ElemStr(&wow_xml.elems[pressed], ELEM_ON_CLICK));
        }
        return hit >= 0 || pressed >= 0;
    }
    if (event != UI_MOUSE_DOWN) {
        return hit >= 0;
    }
    if (hit < 0) {
        wow_xml.pressed_button = -1;
        wow_xml.drag.scrollbar_idx = -1;
        return false;
    }

    /* Scrollbar part interaction. */
    {
        int part = UIWow_XMLScrollBarPart(&wow_xml.elems[hit]);
        if (part) {
            int sf = UIWow_XMLScrollBarParent(hit);
            if (sf >= 0) {
                if (part == 3) {
                    /* Thumb: start drag. */
                    wow_xml.drag.scrollbar_idx = sf;
                    wow_xml.drag.start_mouse_y = fdf_y;
                    wow_xml.drag.start_value = wow_xml.scroll[sf].scroll_y;
                    return true;
                }
                if (part == 1 || part == 2) {
                    /* Up/Down button: step scroll. */
                    RECT vr = UIWow_XmlComputeRect(sf);
                    FLOAT step = vr.h * 0.3f;
                    if (part == 1) wow_xml.scroll[sf].scroll_y = MAX(0.0f, wow_xml.scroll[sf].scroll_y - step);
                    else wow_xml.scroll[sf].scroll_y = MIN(wow_xml.scroll[sf].scroll_range, wow_xml.scroll[sf].scroll_y + step);
                    wow_xml.pressed_button = hit;
                    return true;
                }
            }
        }
    }

    if (wow_xml.elems[hit].type == WOW_XML_EDITBOX) {
        LPCSTR t = wow_xml.elems[hit].texts[ELEM_TEXT];
        wow_xml.focus = hit;
        /* Ensure element has a buffer for text editing. */
        if (!t) {
            wow_xml.elems[hit].texts[ELEM_TEXT] = calloc(1, 256);
            t = wow_xml.elems[hit].texts[ELEM_TEXT];
        }
        wow_xml.text_input.text = (char *)t;
        wow_xml.text_input.size = 256;
        wow_xml.text_input.max_chars = 255;
        wow_xml.text_input.cursor = (DWORD)strlen(t ? t : "");
        wow_xml.pressed_button = -1;
        return true;
    }
    if (wow_xml.elems[hit].type == WOW_XML_BUTTON && (wow_xml.elems[hit].flags & EF_ENABLED)) {
        wow_xml.pressed_button = hit;
        return true;
    }
    wow_xml.pressed_button = -1;
    return false;
}

static void UIWow_XMLEditInsert(uiWowXmlElem_t *e, LPCSTR text) {
    LPCSTR old = e->texts[ELEM_TEXT] ? e->texts[ELEM_TEXT] : "";
    size_t len = strlen(old), add = text ? strlen(text) : 0;
    if (!add) return;
    /* Grow buffer if needed. */
    if (len + add + 1 > wow_xml.text_input.size) {
        DWORD new_size = (DWORD)(len + add + 256);
        char *buf = realloc(e->texts[ELEM_TEXT], new_size);
        if (!buf) return;
        e->texts[ELEM_TEXT] = buf;
        wow_xml.text_input.text = buf;
        wow_xml.text_input.size = new_size;
    }
    UI_TextInput_Insert(&wow_xml.text_input, text);
    e->measured_h = 0;
}

static void UIWow_XMLEditBackspace(uiWowXmlElem_t *e) {
    if (UI_TextInput_Backspace(&wow_xml.text_input))
        e->measured_h = 0;
}

static void UIWow_XMLEditDelete(uiWowXmlElem_t *e) {
    if (UI_TextInput_Delete(&wow_xml.text_input))
        e->measured_h = 0;
}

BOOL UIWow_XMLTextInput(LPCSTR text) {
    uiWowXmlElem_t *e;
    if (wow_xml.focus < 0 || wow_xml.focus >= wow_xml.count || !text || !*text) return false;
    e = &wow_xml.elems[wow_xml.focus];
    if (e->type != WOW_XML_EDITBOX) return false;
    if (!strcmp(text, "\b")) {
        UIWow_XMLEditBackspace(e);
        return true;
    }
    if (!strcmp(text, "\r") || !strcmp(text, "\n")) {
        if (UIWow_ElemStr(e, ELEM_ON_ENTER_PRESSED))
            UIWow_XMLRunFrameScript(wow_xml.focus, e->texts[ELEM_ON_ENTER_PRESSED], "OnEnterPressed");
        return true;
    }
    UIWow_XMLEditInsert(e, text);
    return true;
}

BOOL UIWow_XMLKeyEvent(int key, BOOL down, DWORD time) {
    uiWowXmlElem_t *e;
    int result;
    (void)time;
    if (!down || wow_xml.focus < 0 || wow_xml.focus >= wow_xml.count) return false;
    e = &wow_xml.elems[wow_xml.focus];
    if (e->type != WOW_XML_EDITBOX) return false;
    result = UI_TextInput_Key(&wow_xml.text_input, key);
    switch (result) {
        case UI_TEXTINPUT_CONSUMED:
            e->measured_h = 0;
            return true;
        case UI_TEXTINPUT_ENTER:
            if (UIWow_ElemStr(e, ELEM_ON_ENTER_PRESSED))
                UIWow_XMLRunFrameScript(wow_xml.focus, e->texts[ELEM_ON_ENTER_PRESSED], "OnEnterPressed");
            return true;
        case UI_TEXTINPUT_ESCAPE:
            if (UIWow_ElemStr(e, ELEM_ON_ESCAPE_PRESSED))
                UIWow_XMLRunFrameScript(wow_xml.focus, e->texts[ELEM_ON_ESCAPE_PRESSED], "OnEscapePressed");
            return true;
        case UI_TEXTINPUT_TAB:
            if (UIWow_ElemStr(e, ELEM_ON_TAB_PRESSED))
                UIWow_XMLRunFrameScript(wow_xml.focus, e->texts[ELEM_ON_TAB_PRESSED], "OnTabPressed");
            return true;
        default:
            return false;
    }
}

void UIWow_XmlSetFrameModel(int idx, LPCSTR model_path) {
    if (idx < 0 || idx >= wow_xml.count || !model_path) return;
    uiWowXmlElem_t *e = &wow_xml.elems[idx];
    if (e->model && wow_ui.renderer && wow_ui.renderer->ReleaseModel) {
        wow_ui.renderer->ReleaseModel(e->model);
        e->model = NULL;
    }
    free(e->texts[ELEM_FILE]);
    e->texts[ELEM_FILE] = model_path && *model_path ? strdup(model_path) : NULL;
}
