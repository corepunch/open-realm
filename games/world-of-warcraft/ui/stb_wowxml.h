/*
 * stb_wowxml.h — WoW FrameXML parser, STB-style single-header library.
 *
 * Parses WoW FrameXML (.xml) files into an internal element registry.
 * The runtime layer (ui_xml.c) provides drawing, Lua bindings, and mouse
 * handling on top of this parser.
 *
 * Declarations-only mode (default):
 *   Include normally to get public types and extern declarations.
 *
 * Implementation mode:
 *   #define STB_WOW_XML_IMPLEMENTATION before including this header in exactly
 *   one .c file (ui_xml.c) to get the full parser implementation.
 *
 * Host services (defined by ui_xml.c, no-op stubs in test builds):
 *   int  UI_XmlFsReadFile(LPCSTR path, void **buf) — read file; returns size
 *   void UI_XmlFsFreeFile(void *buf)               — free file buffer
 *   void UI_XmlPrintf(LPCSTR fmt, ...)             — diagnostic output
 *   void UI_XmlOnFramePublish(int idx)             — frame created (for Lua)
 *   void UI_XmlOnShow(int idx)                     — frame shown (for Lua)
 *   void UI_XmlOnScriptBody(LPCSTR path, LPCSTR b) — inline Script body
 *   void UI_XmlLoadScriptFile(LPCSTR path)         — Script file= attribute
 */
#ifndef stb_wowxml_h
#define stb_wowxml_h

#include "common/shared.h"
#include "shared/types/rect.h"
#include "client/ui_text_input.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Constants                                                                   */
/* -------------------------------------------------------------------------- */
#define WOW_XML_MAX_ELEMS        2048
#define WOW_XML_LAYER_BACKGROUND 0
#define WOW_XML_LAYER_BORDER     1
#define WOW_XML_LAYER_ARTWORK    2
#define WOW_XML_LAYER_OVERLAY    3
#define WOW_XML_BACKDROP_LEFT    0
#define WOW_XML_BACKDROP_RIGHT   1
#define WOW_XML_BACKDROP_TOP     2
#define WOW_XML_BACKDROP_BOTTOM  3

/* -------------------------------------------------------------------------- */
/* Types and enums                                                             */
/* -------------------------------------------------------------------------- */
#ifndef UIWOW_XML_TYPE_DEFINED
#define UIWOW_XML_TYPE_DEFINED
typedef enum {
    WOW_XML_FRAME = 0,
    WOW_XML_MODEL,
    WOW_XML_TEXTURE,
    WOW_XML_FONTSTRING,
    WOW_XML_BUTTON,
    WOW_XML_EDITBOX,
} uiWowXmlType_t;
#endif

typedef struct { FLOAT x, y; } fpoint_t;
typedef struct { FLOAT w, h; } fsize_t;

typedef enum {
    ELEM_NAME = 0,
    ELEM_PARENT_NAME,
    ELEM_RELATIVE_NAME,
    ELEM_FILE,
    ELEM_NORMAL_FILE,
    ELEM_PUSHED_FILE,
    ELEM_HIGHLIGHT_FILE,
    ELEM_CHECKED_FILE,
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
    EF_USED           = 1 << 0,
    EF_HAS_ANCHOR     = 1 << 1,
    EF_HAS_SIZE       = 1 << 2,
    EF_HAS_TEXCOORD   = 1 << 3,
    EF_HIDDEN         = 1 << 4,
    EF_VIRTUAL        = 1 << 5,
    EF_PASSWORD       = 1 << 6,
    EF_ENABLED        = 1 << 7,
    EF_CHECKED        = 1 << 8,
    EF_SET_ALL_PTS    = 1 << 9,
    EF_BACKDROP_TILE  = 1 << 10,
    EF_HAS_HALIGN     = 1 << 11,
    EF_HAS_VALIGN     = 1 << 12,
    EF_FOCUSABLE      = 1 << 13,
    EF_HAS_HIGHLIGHT_TEXCOORD = 1 << 14,
    EF_HAS_BUTTON_TEXT_COLORS = 1 << 15,
    EF_PENDING_ONLOAD = 1 << 16,
    EF_HAS_ANCHOR2    = 1 << 17,
    EF_WORD_WRAP      = 1 << 18,
    EF_IS_SCROLLFRAME = 1 << 19,
    EF_SCROLLBAR_PART = 1 << 20,
    EF_CHECKBUTTON    = 1 << 21,
    EF_LOGGED_GEOMETRY = 1 << 22,
} uiWowXmlElemFlag_t;

typedef struct {
    DWORD flags;
    uiWowXmlType_t type;
    int id, parent, relative_to, relative_to2, draw_layer;
    char *texts[ELEM_STRING_COUNT];
    fpoint_t pos, offset, text_off, offset2;
    char *point2, *relative_point2, *relative_name2;
    fsize_t size, edge, tile, text_inset;
    fsize_t measured;
    FLOAT alpha, font_size;
    FLOAT backdrop_insets[4];
    uiFontJustificationH_t halign;
    uiFontJustificationV_t valign;
    COLOR32 colors[ELEM_COLOR_COUNT];
    COLOR32 button_text_colors[WOW_XML_BUTTON_TEXT_COUNT];
    RECT texcoord, highlight_texcoord;
    LPMODEL model;
    DWORD sequence, frame, oldframe, anim_start;
    COLOR32 fog_color;
    FLOAT fog_near, fog_far;
    BOOL has_fog;
} uiWowXmlElem_t;

struct wowXmlRuntime_s {
    uiWowXmlElem_t elems[WOW_XML_MAX_ELEMS];
    int count, focus, pressed_button, hovered_button;
    BOOL lua_ready;
    uiTextInput_t text_input;
    struct { FLOAT scroll_y, scroll_range; int scrollbar_child; } scroll[WOW_XML_MAX_ELEMS];
    struct { int scrollbar_idx; FLOAT start_mouse_y, start_value; } drag;
};

#ifndef STB_WOW_XML_GLOBALS
extern struct wowXmlRuntime_s wow_xml;
#else
struct wowXmlRuntime_s wow_xml = { 0 };
#endif

static const uiWowXmlStr_t uiwow_button_part_name_fields[] = {
    ELEM_NORMAL_NAME, ELEM_PUSHED_NAME, ELEM_HIGHLIGHT_NAME
};

/* -------------------------------------------------------------------------- */
/* Host services (defined by embedding module)                                 */
/* -------------------------------------------------------------------------- */
extern int  UI_XmlFsReadFile(LPCSTR path, void **buf);
extern void UI_XmlFsFreeFile(void *buf);
extern void UI_XmlPrintf(LPCSTR fmt, ...);
extern void UI_XmlOnFramePublish(int idx);
extern void UI_XmlOnShow(int idx);
extern void UI_XmlOnScriptBody(LPCSTR path, LPCSTR body);
extern void UI_XmlLoadScriptFile(LPCSTR path);

/* -------------------------------------------------------------------------- */
/* Pure element helpers & public API                                          */
/* -------------------------------------------------------------------------- */
LPCSTR UIWow_ElemStr(uiWowXmlElem_t const *e, uiWowXmlStr_t f);
void   UIWow_ElemSetStr(uiWowXmlElem_t *e, uiWowXmlStr_t f, LPCSTR s);
void   UIWow_ElemAppendStr(uiWowXmlElem_t *e, uiWowXmlStr_t f, LPCSTR s);
void   UIWow_ElemFreeStrings(uiWowXmlElem_t *e);
RECT   UIWow_XmlComputeRect(int idx);
int    UIWow_XmlFindByName(LPCSTR name);
BOOL   UIWow_XMLIsVisible(int idx);
void   UIWow_XMLSetShown(int idx, BOOL shown);
int    UIWow_XMLHitFrame(FLOAT x, FLOAT y);
void   UIWow_XMLFreeElems(void);
FLOAT  UIWow_XmlX(FLOAT pixels);
FLOAT  UIWow_XmlY(FLOAT pixels);
BOOL   UIWow_XmlResolvePath(LPCSTR base, LPCSTR rel, LPSTR out, size_t n);
BOOL   UIWow_XMLProcessFile(LPCSTR path, int depth);

static inline BOOL UIWow_XMLPointInRect(FLOAT x, FLOAT y, LPCRECT r) {
    return r && x >= r->x && y >= r->y && x <= r->x + r->w && y <= r->y + r->h;
}

/* Load a single FrameXML file into the elem registry (no Lua required). */
BOOL UIWow_XMLLoadFile(LPCSTR path);

/* Parse an in-memory FrameXML buffer (for unit tests). */
BOOL UIWow_XMLLoadBuffer(LPCSTR buf, int size, LPCSTR debug_name);

/* Show or hide a named top-level frame by setting/clearing EF_HIDDEN. */
void UIWow_XMLSetFrameVisible(LPCSTR name, BOOL visible);

/* Bind dynamic text without replacing XML-authored geometry or presentation. */
BOOL UIWow_XMLSetFrameText(LPCSTR name, LPCSTR text);

/* Bind client-owned press state while XML continues to own button presentation. */
BOOL UIWow_XMLSetButtonPressed(LPCSTR name, BOOL pressed);

/* Reset the elem registry (call when entering game mode). */
void UIWow_XMLClearFrames(void);

/* Hit-test for game-mode button clicks; returns OnClick script or NULL. */
LPCSTR UIWow_XMLHitButton(FLOAT nx, FLOAT ny);

/* Elem lookup and inspectors. */
int    UIWow_XmlFindByNamePub(LPCSTR name);
void   UIWow_XmlComputeRectPub(int idx, FLOAT *x, FLOAT *y, FLOAT *w, FLOAT *h);
int    UIWow_XmlElemCount(void);
int    UIWow_XmlElemType(int idx);
LPCSTR UIWow_XmlElemName(int idx);
LPCSTR UIWow_XmlElemText(int idx);
LPCSTR UIWow_XmlElemOnClick(int idx);
LPCSTR UIWow_XmlElemPoint(int idx);
int    UIWow_XmlElemHidden(int idx);
LPCSTR UIWow_XmlElemParent(int idx);

/* -------------------------------------------------------------------------- */
/* Implementation                                                              */
/* -------------------------------------------------------------------------- */
#ifdef STB_WOW_XML_IMPLEMENTATION

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "common/tinyxml.h"

#ifndef STB_WOW_XML_GLOBALS
struct wowXmlRuntime_s wow_xml;
#endif

static char s_current_xml_path[PATH_MAX];

static const uiWowXmlStr_t uiwow_copy_str_fields[] = {
    ELEM_FILE, ELEM_NORMAL_FILE, ELEM_PUSHED_FILE, ELEM_HIGHLIGHT_FILE, ELEM_CHECKED_FILE,
    ELEM_TEXT, ELEM_BACKDROP_BG, ELEM_BACKDROP_EDGE,
    ELEM_NORMAL_NAME, ELEM_PUSHED_NAME, ELEM_HIGHLIGHT_NAME
};

/* ---- DDX Schema Tables --------------------------------------------------- */

static const struct {
    LPCSTR name;
    FLOAT  x_factor, y_factor;
} uiwow_point_factors[] = {
    { "TOPLEFT",     0.0f, 0.0f },
    { "TOP",         0.5f, 0.0f },
    { "TOPRIGHT",    1.0f, 0.0f },
    { "LEFT",        0.0f, 0.5f },
    { "CENTER",      0.5f, 0.5f },
    { "RIGHT",       1.0f, 0.5f },
    { "BOTTOMLEFT",  0.0f, 1.0f },
    { "BOTTOM",      0.5f, 1.0f },
    { "BOTTOMRIGHT", 1.0f, 1.0f },
    { NULL,          0.5f, 0.5f }
};

static const struct {
    LPCSTR name;
    int    layer;
} uiwow_layer_levels[] = {
    { "BACKGROUND", WOW_XML_LAYER_BACKGROUND },
    { "BORDER",     WOW_XML_LAYER_BORDER },
    { "OVERLAY",    WOW_XML_LAYER_OVERLAY },
    { "ARTWORK",    WOW_XML_LAYER_ARTWORK },
    { NULL,         WOW_XML_LAYER_ARTWORK }
};

static const struct {
    LPCSTR                 name;
    uiFontJustificationH_t align;
} uiwow_justify_h[] = {
    { "LEFT",   FONT_JUSTIFYLEFT },
    { "RIGHT",  FONT_JUSTIFYRIGHT },
    { "CENTER", FONT_JUSTIFYCENTER },
    { NULL,     FONT_JUSTIFYCENTER }
};

static const struct {
    LPCSTR                 name;
    uiFontJustificationV_t align;
} uiwow_justify_v[] = {
    { "TOP",    FONT_JUSTIFYTOP },
    { "BOTTOM", FONT_JUSTIFYBOTTOM },
    { "MIDDLE", FONT_JUSTIFYMIDDLE },
    { "CENTER", FONT_JUSTIFYMIDDLE },
    { NULL,     FONT_JUSTIFYMIDDLE }
};

static const struct {
    LPCSTR        name;
    uiWowXmlStr_t field;
} uiwow_script_tags[] = {
    { "OnClick",         ELEM_ON_CLICK },
    { "OnLoad",          ELEM_ON_LOAD },
    { "OnShow",          ELEM_ON_SHOW },
    { "OnEnter",         ELEM_ON_ENTER },
    { "OnLeave",         ELEM_ON_LEAVE },
    { "OnEnterPressed",  ELEM_ON_ENTER_PRESSED },
    { "OnEscapePressed", ELEM_ON_ESCAPE_PRESSED },
    { "OnTabPressed",    ELEM_ON_TAB_PRESSED },
    { "OnMouseWheel",    ELEM_ON_MOUSE_WHEEL },
    { "OnUpdateModel",   ELEM_ON_UPDATE_MODEL },
    { "OnUpdate",        ELEM_ON_UPDATE },
    { NULL,              ELEM_STRING_COUNT }
};

static const struct {
    LPCSTR        tag;
    uiWowXmlStr_t file_field;
    uiWowXmlStr_t name_field;
    DWORD         texcoord_flag;
} uiwow_button_part_tags[] = {
    { "NormalTexture",    ELEM_NORMAL_FILE,    ELEM_NORMAL_NAME,    EF_HAS_TEXCOORD },
    { "PushedTexture",    ELEM_PUSHED_FILE,    ELEM_PUSHED_NAME,    0 },
    { "HighlightTexture", ELEM_HIGHLIGHT_FILE, ELEM_HIGHLIGHT_NAME, EF_HAS_HIGHLIGHT_TEXCOORD },
    { "CheckedTexture",   ELEM_CHECKED_FILE,   ELEM_STRING_COUNT,   0 },
    { NULL,               ELEM_STRING_COUNT,   ELEM_STRING_COUNT,   0 }
};

static const struct {
    LPCSTR                    tag;
    uiWowXmlButtonTextState_t state;
} uiwow_button_text_tags[] = {
    { "NormalText",    WOW_XML_BUTTON_TEXT_NORMAL },
    { "HighlightText", WOW_XML_BUTTON_TEXT_HIGHLIGHT },
    { "DisabledText",  WOW_XML_BUTTON_TEXT_DISABLED },
    { NULL,            WOW_XML_BUTTON_TEXT_NORMAL }
};

static const struct {
    LPCSTR         tag;
    uiWowXmlType_t type;
    DWORD          flags;
} uiwow_node_types[] = {
    { "Frame",            WOW_XML_FRAME,      0 },
    { "ScrollFrame",      WOW_XML_FRAME,      EF_IS_SCROLLFRAME },
    { "Slider",           WOW_XML_FRAME,      0 },
    { "Model",            WOW_XML_MODEL,      0 },
    { "Texture",          WOW_XML_TEXTURE,    0 },
    { "FontString",       WOW_XML_FONTSTRING, 0 },
    { "Button",           WOW_XML_BUTTON,     0 },
    { "CheckButton",      WOW_XML_BUTTON,     EF_CHECKBUTTON },
    { "EditBox",          WOW_XML_EDITBOX,    EF_FOCUSABLE },
    { "NormalTexture",    WOW_XML_TEXTURE,    0 },
    { "PushedTexture",    WOW_XML_TEXTURE,    0 },
    { "DisabledTexture",  WOW_XML_TEXTURE,    0 },
    { "HighlightTexture", WOW_XML_TEXTURE,    0 },
    { "ThumbTexture",     WOW_XML_TEXTURE,    0 },
    { "NormalText",       WOW_XML_FONTSTRING, 0 },
    { "DisabledText",     WOW_XML_FONTSTRING, 0 },
    { "HighlightText",    WOW_XML_FONTSTRING, 0 },
    { NULL,               WOW_XML_FRAME,      0 }
};

typedef enum {
    WOW_ATTR_STR_FIELD,
    WOW_ATTR_BOOL_FLAG,
    WOW_ATTR_PASSWORD_FLAG,
    WOW_ATTR_INT_ID,
} uiWowAttrType_t;

static const struct {
    LPCSTR          name;
    uiWowAttrType_t type;
    DWORD           field_or_flag;
} uiwow_shared_attrs[] = {
    { "file",          WOW_ATTR_STR_FIELD,      ELEM_FILE },
    { "text",          WOW_ATTR_STR_FIELD,      ELEM_TEXT },
    { "hidden",        WOW_ATTR_BOOL_FLAG,      EF_HIDDEN },
    { "virtual",       WOW_ATTR_BOOL_FLAG,      EF_VIRTUAL },
    { "setAllPoints",  WOW_ATTR_BOOL_FLAG,      EF_SET_ALL_PTS },
    { "password",      WOW_ATTR_PASSWORD_FLAG,  EF_PASSWORD },
    { "id",            WOW_ATTR_INT_ID,         0 },
    { "wordWrap",      WOW_ATTR_BOOL_FLAG,      EF_WORD_WRAP },
    { "checked",       WOW_ATTR_BOOL_FLAG,      EF_CHECKED },
    { NULL,            0,                       0 }
};

/* ---- String helpers ---- */

LPCSTR UIWow_ElemStr(uiWowXmlElem_t const *e, uiWowXmlStr_t f) {
    return (e->texts[f] && e->texts[f][0]) ? e->texts[f] : NULL;
}

void UIWow_ElemSetStr(uiWowXmlElem_t *e, uiWowXmlStr_t f, LPCSTR s) {
    free(e->texts[f]);
    e->texts[f] = (s && *s) ? strdup(s) : NULL;
    if (f == ELEM_TEXT) e->measured = MAKE(fsize_t, 0, 0);
}

void UIWow_ElemAppendStr(uiWowXmlElem_t *e, uiWowXmlStr_t f, LPCSTR s) {
    if (!s || !*s) return;
    if (!e->texts[f] || !e->texts[f][0]) { UIWow_ElemSetStr(e, f, s); return; }
    size_t old = strlen(e->texts[f]), add = strlen(s);
    char *buf = realloc(e->texts[f], old + add + 1);
    if (!buf) return;
    memcpy(buf + old, s, add + 1);
    e->texts[f] = buf;
}

void UIWow_ElemFreeStrings(uiWowXmlElem_t *e) {
    FOR_LOOP(f, ELEM_STRING_COUNT) { free(e->texts[f]); e->texts[f] = NULL; }
    free(e->point2); e->point2 = NULL;
    free(e->relative_point2); e->relative_point2 = NULL;
    free(e->relative_name2); e->relative_name2 = NULL;
}

/* ---- Coordinate helpers ---- */

static FLOAT UIWow_XmlFloat(xmlChar const *s, FLOAT fallback) { return s && *s ? (FLOAT)atof((char const *)s) : fallback; }
FLOAT UIWow_XmlX(FLOAT pixels) { return pixels / 1024.0f; }
FLOAT UIWow_XmlY(FLOAT pixels) { return pixels / 768.0f; }

static int UIWow_XmlLayer(LPCSTR level) {
    if (!level || !*level) return WOW_XML_LAYER_ARTWORK;
    for (int i = 0; uiwow_layer_levels[i].name; i++)
        if (!strcasecmp(level, uiwow_layer_levels[i].name))
            return uiwow_layer_levels[i].layer;
    return WOW_XML_LAYER_ARTWORK;
}

static uiFontJustificationH_t UIWow_XmlHAlign(LPCSTR v, uiFontJustificationH_t fallback) {
    if (!v || !*v) return fallback;
    for (int i = 0; uiwow_justify_h[i].name; i++)
        if (!strcasecmp(v, uiwow_justify_h[i].name))
            return uiwow_justify_h[i].align;
    return fallback;
}

static uiFontJustificationV_t UIWow_XmlVAlign(LPCSTR v, uiFontJustificationV_t fallback) {
    if (!v || !*v) return fallback;
    for (int i = 0; uiwow_justify_v[i].name; i++)
        if (!strcasecmp(v, uiwow_justify_v[i].name))
            return uiwow_justify_v[i].align;
    return fallback;
}

BOOL UIWow_XmlResolvePath(LPCSTR base, LPCSTR rel, LPSTR out, size_t n) {
    LPCSTR slash; size_t prefix;
    if (!rel || !*rel || !out || n == 0) return false;
    if (strchr(rel, '\\')) { snprintf(out, n, "%s", rel); return true; }
    slash = strrchr(base, '\\');
    if (!slash) { snprintf(out, n, "%s", rel); return true; }
    prefix = (size_t)(slash - base + 1);
    if (prefix + strlen(rel) + 1 > n) return false;
    memcpy(out, base, prefix); out[prefix] = '\0'; strncat(out, rel, n - strlen(out) - 1);
    return true;
}

/* ---- Element registry ---- */

int UIWow_XmlFindByName(LPCSTR name) {
    if (!name || !*name) return -1;
    FOR_LOOP(i, wow_xml.count) {
        if ((wow_xml.elems[i].flags & EF_USED) && wow_xml.elems[i].texts[ELEM_NAME] &&
            !strcmp(wow_xml.elems[i].texts[ELEM_NAME], name)) return i;
    }
    return -1;
}

static int UIWow_XmlPushElem(uiWowXmlType_t type, LPCSTR name, int parent, int draw_layer) {
    uiWowXmlElem_t *e;
    if (wow_xml.count >= WOW_XML_MAX_ELEMS) {
        UI_XmlPrintf("UIWow: XML element limit hit (%d) name=%s\n", WOW_XML_MAX_ELEMS, name ? name : "<anon>");
        return -1;
    }
    e = &wow_xml.elems[wow_xml.count];
    memset(e, 0, sizeof(*e));
    e->flags = EF_USED | EF_ENABLED | EF_WORD_WRAP;
    e->type = type; e->parent = parent; e->relative_to = parent; e->draw_layer = draw_layer;
    e->alpha = 1.0f; e->font_size = 14.0f;
    e->colors[ELEM_COLOR_TEXT] = COLOR32_WHITE;
    e->colors[ELEM_COLOR_VERTEX] = COLOR32_WHITE;
    e->colors[ELEM_COLOR_BACKDROP] = MAKE(COLOR32, 23, 23, 23, 120);
    e->colors[ELEM_COLOR_BACKDROP_BORDER] = MAKE(COLOR32, 204, 204, 204, 255);
    e->halign = FONT_JUSTIFYCENTER; e->valign = FONT_JUSTIFYMIDDLE;
    e->button_text_colors[WOW_XML_BUTTON_TEXT_NORMAL]    = e->colors[ELEM_COLOR_TEXT];
    e->button_text_colors[WOW_XML_BUTTON_TEXT_DISABLED]  = e->colors[ELEM_COLOR_TEXT];
    e->button_text_colors[WOW_XML_BUTTON_TEXT_HIGHLIGHT] = e->colors[ELEM_COLOR_TEXT];
    e->texcoord           = MAKE(RECT, 0, 0, 1, 1);
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
        FOR_LOOP(i, sizeof(uiwow_button_part_name_fields) / sizeof(uiwow_button_part_name_fields[0])) {
            uiWowXmlStr_t f = uiwow_button_part_name_fields[i];
            if (!UIWow_ElemStr(e, f) && UIWow_ElemStr(src, f)) UIWow_ElemSetStr(e, f, src->texts[f]);
        }
        FOR_LOOP(i, sizeof(uiwow_copy_str_fields) / sizeof(uiwow_copy_str_fields[0])) {
            uiWowXmlStr_t f = uiwow_copy_str_fields[i];
            if (!UIWow_ElemStr(e, f) && UIWow_ElemStr(src, f))
                UIWow_ElemSetStr(e, f, src->texts[f]);
        }
        if (src->flags & EF_HIDDEN) e->flags |= EF_HIDDEN;
        if (src->flags & EF_HAS_TEXCOORD) { e->texcoord = src->texcoord; e->flags |= EF_HAS_TEXCOORD; }
        if (src->flags & EF_HAS_HIGHLIGHT_TEXCOORD) { e->highlight_texcoord = src->highlight_texcoord; e->flags |= EF_HAS_HIGHLIGHT_TEXCOORD; }
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
            memcpy(e->button_text_colors, src->button_text_colors, sizeof(e->button_text_colors));
            e->flags |= EF_HAS_BUTTON_TEXT_COLORS;
        }
        for (int i = 0; uiwow_script_tags[i].name; i++) {
            uiWowXmlStr_t f = uiwow_script_tags[i].field;
            if (!UIWow_ElemStr(e, f) && UIWow_ElemStr(src, f)) UIWow_ElemSetStr(e, f, src->texts[f]);
        }
        if (src->flags & EF_WORD_WRAP) e->flags |= EF_WORD_WRAP;
    }
}

static void UIWow_XmlPointFactors(LPCSTR point, LPFLOAT fx, LPFLOAT fy) {
    if (!point || !*point) point = "CENTER";
    for (int i = 0; uiwow_point_factors[i].name; i++) {
        if (!strcasecmp(point, uiwow_point_factors[i].name)) {
            *fx = uiwow_point_factors[i].x_factor;
            *fy = uiwow_point_factors[i].y_factor;
            return;
        }
    }
    *fx = 0.5f; *fy = 0.5f;
}

static void UIWow_XmlRectPoint(LPCRECT r, LPCSTR point, LPFLOAT x, LPFLOAT y) {
    FLOAT fx, fy;
    UIWow_XmlPointFactors(point, &fx, &fy);
    *x = r->x + r->w * fx;
    *y = r->y + r->h * fy;
}

RECT UIWow_XmlComputeRect(int idx) {
    uiWowXmlElem_t const *e = &wow_xml.elems[idx];
    RECT parent = MAKE(RECT, 0, 0, 1, 1);
    LPCSTR point = e->texts[ELEM_POINT], rel_point = e->texts[ELEM_RELATIVE_POINT];
    FLOAT w = e->size.w > 0 ? e->size.w : (e->type == WOW_XML_FONTSTRING ? e->measured.w : 0.0f);
    FLOAT h = e->size.h > 0 ? e->size.h : (e->type == WOW_XML_FONTSTRING ? e->measured.h : 0.0f);
    RECT out = MAKE(RECT, 0, 0, w, h);
    FLOAT ax, ay, fx, fy;
    if (e->parent >= 0 && e->parent < wow_xml.count) parent = UIWow_XmlComputeRect(e->parent);
    if (e->flags & EF_SET_ALL_PTS) return parent;
    if (!(e->flags & EF_HAS_ANCHOR)) { out.x = parent.x; out.y = parent.y; return out; }
    if (e->relative_to >= 0 && e->relative_to < wow_xml.count) parent = UIWow_XmlComputeRect(e->relative_to);
    UIWow_XmlRectPoint(&parent, (rel_point && rel_point[0]) ? rel_point : (point ? point : "CENTER"), &ax, &ay);
    ax += e->offset.x; ay += e->offset.y;
    UIWow_XmlPointFactors(point, &fx, &fy);
    out.x = ax - out.w * fx;
    out.y = ay - out.h * fy;
    if ((e->flags & EF_HAS_ANCHOR2) && e->point2 && e->relative_point2) {
        RECT ref2 = (e->relative_to2 >= 0 && e->relative_to2 < wow_xml.count)
                    ? UIWow_XmlComputeRect(e->relative_to2) : parent;
        FLOAT bx, by; LPCSTR p2 = e->point2;
        UIWow_XmlRectPoint(&ref2, e->relative_point2, &bx, &by);
        bx += e->offset2.x; by += e->offset2.y;
        if      (strcasestr(p2, "RIGHT"))  { out.w = bx - out.x; if (out.w < 0) { out.x += out.w; out.w = -out.w; } }
        else if (strcasestr(p2, "LEFT"))   { FLOAT r = out.x + out.w; out.x = bx; out.w = r - bx; if (out.w < 0) out.w = 0; }
        if      (strcasestr(p2, "BOTTOM")) { out.h = by - out.y; if (out.h < 0) { out.y += out.h; out.h = -out.h; } }
        else if (!strcasecmp(p2, "TOP"))   { FLOAT b = out.y + out.h; out.y = by; out.h = b - by; if (out.h < 0) out.h = 0; }
    }
    return out;
}

BOOL UIWow_XMLIsVisible(int idx) {
    while (idx >= 0 && idx < wow_xml.count) {
        uiWowXmlElem_t const *e = &wow_xml.elems[idx];
        if (!(e->flags & EF_USED) || (e->flags & EF_HIDDEN) || (e->flags & EF_VIRTUAL)) return false;
        idx = e->parent;
    }
    return true;
}

int UIWow_XMLHitFrame(FLOAT x, FLOAT y) {
    for (int i = wow_xml.count - 1; i >= 0; i--) {
        uiWowXmlElem_t const *e = &wow_xml.elems[i]; RECT r;
        if (!UIWow_XMLIsVisible(i) || (e->type != WOW_XML_BUTTON && e->type != WOW_XML_EDITBOX)) continue;
        r = UIWow_XmlComputeRect(i);
        if (UIWow_XMLPointInRect(x, y, &r)) return i;
    }
    return -1;
}

/* ---- Visibility with show-callback ---- */

void UIWow_XMLSetShown(int idx, BOOL shown) {
    if (idx < 0 || idx >= wow_xml.count) return;
    if (shown) {
        BOOL was_hidden = (wow_xml.elems[idx].flags & EF_HIDDEN) != 0;
        wow_xml.elems[idx].flags &= ~EF_HIDDEN;
        if (was_hidden && UIWow_ElemStr(&wow_xml.elems[idx], ELEM_ON_SHOW))
            UI_XmlOnShow(idx);
    } else {
        wow_xml.elems[idx].flags |= EF_HIDDEN;
    }
}

/* ---- XML attribute readers ---- */

static void UIWow_XmlReadSize(uiWowXmlElem_t *e, xmlNodePtr node) {
    for (xmlNodePtr c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Size")) continue;
        for (xmlNodePtr d = c->children; d; d = d->next) {
            if (d->type != XML_ELEMENT_NODE || xmlStrcasecmp(d->name, BAD_CAST "AbsDimension")) continue;
            xmlChar *x = xmlGetProp(d, BAD_CAST "x"), *y = xmlGetProp(d, BAD_CAST "y");
            e->size.w = UIWow_XmlX(UIWow_XmlFloat(x, 0.0f));
            e->size.h = UIWow_XmlY(UIWow_XmlFloat(y, 0.0f));
            if (e->size.w > 0 || e->size.h > 0) e->flags |= EF_HAS_SIZE;
            SAFE_DELETE(x, xmlFree); SAFE_DELETE(y, xmlFree);
            return;
        }
    }
}

static void UIWow_XmlResolveRelativeTo(uiWowXmlElem_t *e, LPCSTR raw, LPCSTR parent_name) {
    char resolved[256];
    LPCSTR dollar = raw ? strstr(raw, "$parent") : NULL;
    if (dollar && parent_name && *parent_name) {
        snprintf(resolved, sizeof(resolved), "%.*s%s%s", (int)(dollar - raw), raw, parent_name, dollar + 7);
        UIWow_ElemSetStr(e, ELEM_RELATIVE_NAME, resolved);
    } else if (raw && *raw) {
        UIWow_ElemSetStr(e, ELEM_RELATIVE_NAME, raw);
    }
    if (UIWow_ElemStr(e, ELEM_RELATIVE_NAME))
        e->relative_to = UIWow_XmlFindByName(e->texts[ELEM_RELATIVE_NAME]);
}

static void UIWow_XmlReadAnchor(uiWowXmlElem_t *e, xmlNodePtr node) {
    LPCSTR parent_name = (e->parent >= 0 && e->parent < wow_xml.count)
                         ? wow_xml.elems[e->parent].texts[ELEM_NAME] : NULL;
    int anchor_index = 0;
    for (xmlNodePtr c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Anchors")) continue;
        for (xmlNodePtr a = c->children; a; a = a->next) {
            xmlChar *point, *relative, *relative_to;
            fpoint_t off = {0, 0};
            if (a->type != XML_ELEMENT_NODE || xmlStrcasecmp(a->name, BAD_CAST "Anchor")) continue;
            point = xmlGetProp(a, BAD_CAST "point");
            relative = xmlGetProp(a, BAD_CAST "relativePoint");
            relative_to = xmlGetProp(a, BAD_CAST "relativeTo");
            for (xmlNodePtr o = a->children; o; o = o->next) {
                if (o->type != XML_ELEMENT_NODE || xmlStrcasecmp(o->name, BAD_CAST "Offset")) continue;
                for (xmlNodePtr abs = o->children; abs; abs = abs->next) {
                    xmlChar *x, *y;
                    if (abs->type != XML_ELEMENT_NODE || xmlStrcasecmp(abs->name, BAD_CAST "AbsDimension")) continue;
                    x = xmlGetProp(abs, BAD_CAST "x"); y = xmlGetProp(abs, BAD_CAST "y");
                    off.x =  UIWow_XmlX(UIWow_XmlFloat(x, 0.0f));
                    off.y = -UIWow_XmlY(UIWow_XmlFloat(y, 0.0f));
                    SAFE_DELETE(x, xmlFree); SAFE_DELETE(y, xmlFree);
                }
            }
            if (anchor_index == 0) {
                UIWow_ElemSetStr(e, ELEM_POINT, point && *point ? (char const *)point : "CENTER");
                UIWow_ElemSetStr(e, ELEM_RELATIVE_POINT, relative && *relative ? (char const *)relative : e->texts[ELEM_POINT]);
                UIWow_XmlResolveRelativeTo(e, relative_to ? (char const *)relative_to : NULL, parent_name);
                e->offset = off; e->flags |= EF_HAS_ANCHOR;
            } else if (anchor_index == 1) {
                free(e->point2); e->point2 = (point && *point) ? strdup((char const *)point) : NULL;
                free(e->relative_point2); e->relative_point2 = (relative && *relative) ? strdup((char const *)relative) : (e->point2 ? strdup(e->point2) : NULL);
                e->offset2 = off; e->flags |= EF_HAS_ANCHOR2;
                if (relative_to && *relative_to) {
                    char resolved2[256];
                    LPCSTR d2 = strstr((char const *)relative_to, "$parent");
                    if (d2 && parent_name && *parent_name)
                        snprintf(resolved2, sizeof(resolved2), "%.*s%s%s", (int)(d2-(char const *)relative_to), (char const *)relative_to, parent_name, d2+7);
                    else
                        snprintf(resolved2, sizeof(resolved2), "%s", (char const *)relative_to);
                    free(e->relative_name2); e->relative_name2 = strdup(resolved2);
                    e->relative_to2 = UIWow_XmlFindByName(resolved2);
                } else {
                    e->relative_to2 = e->relative_to;
                }
            }
            SAFE_DELETE(point, xmlFree); SAFE_DELETE(relative, xmlFree); SAFE_DELETE(relative_to, xmlFree);
            anchor_index++;
        }
    }
}

static void UIWow_XmlReadBackdrop(uiWowXmlElem_t *e, xmlNodePtr node) {
    for (xmlNodePtr c = node->children; c; c = c->next) {
        xmlChar *bg, *edge, *tile;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Backdrop")) continue;
        bg = xmlGetProp(c, BAD_CAST "bgFile"); edge = xmlGetProp(c, BAD_CAST "edgeFile"); tile = xmlGetProp(c, BAD_CAST "tile");
        if (bg && *bg) UIWow_ElemSetStr(e, ELEM_BACKDROP_BG, (char const *)bg);
        if (edge && *edge) UIWow_ElemSetStr(e, ELEM_BACKDROP_EDGE, (char const *)edge);
        if (tile && *tile && !strcasecmp((char const *)tile, "true")) e->flags |= EF_BACKDROP_TILE;
        SAFE_DELETE(bg, xmlFree); SAFE_DELETE(edge, xmlFree); SAFE_DELETE(tile, xmlFree);
        for (xmlNodePtr d = c->children; d; d = d->next) {
            if (d->type != XML_ELEMENT_NODE) continue;
            if (!xmlStrcasecmp(d->name, BAD_CAST "EdgeSize")) {
                for (xmlNodePtr v = d->children; v; v = v->next) {
                    if (v->type != XML_ELEMENT_NODE || xmlStrcasecmp(v->name, BAD_CAST "AbsValue")) continue;
                    xmlChar *val = xmlGetProp(v, BAD_CAST "val"); FLOAT px = UIWow_XmlFloat(val, 16.0f);
                    e->edge.w = UIWow_XmlX(px); e->edge.h = UIWow_XmlY(px); SAFE_DELETE(val, xmlFree);
                }
            } else if (!xmlStrcasecmp(d->name, BAD_CAST "TileSize")) {
                for (xmlNodePtr v = d->children; v; v = v->next) {
                    if (v->type != XML_ELEMENT_NODE || xmlStrcasecmp(v->name, BAD_CAST "AbsValue")) continue;
                    xmlChar *val = xmlGetProp(v, BAD_CAST "val"); FLOAT px = UIWow_XmlFloat(val, 16.0f);
                    e->tile.w = UIWow_XmlX(px); e->tile.h = UIWow_XmlY(px); SAFE_DELETE(val, xmlFree);
                }
            } else if (!xmlStrcasecmp(d->name, BAD_CAST "BackgroundInsets")) {
                for (xmlNodePtr v = d->children; v; v = v->next) {
                    if (v->type != XML_ELEMENT_NODE || xmlStrcasecmp(v->name, BAD_CAST "AbsInset")) continue;
                    static const struct { LPCSTR attr; int idx; BOOL is_y; } insets[] = {
                        { "left",   WOW_XML_BACKDROP_LEFT,   false },
                        { "right",  WOW_XML_BACKDROP_RIGHT,  false },
                        { "top",    WOW_XML_BACKDROP_TOP,    true },
                        { "bottom", WOW_XML_BACKDROP_BOTTOM, true },
                    };
                    FOR_LOOP(i, sizeof(insets)/sizeof(insets[0])) {
                        xmlChar *val = xmlGetProp(v, BAD_CAST insets[i].attr);
                        FLOAT px = UIWow_XmlFloat(val, 0.0f);
                        e->backdrop_insets[insets[i].idx] = insets[i].is_y ? UIWow_XmlY(px) : UIWow_XmlX(px);
                        SAFE_DELETE(val, xmlFree);
                    }
                }
            }
        }
        return;
    }
}

static void UIWow_XmlReadTexCoords(uiWowXmlElem_t *e, xmlNodePtr node) {
    for (xmlNodePtr c = node->children; c; c = c->next) {
        xmlChar *l, *r, *t, *b;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "TexCoords")) continue;
        l = xmlGetProp(c, BAD_CAST "left"); r = xmlGetProp(c, BAD_CAST "right");
        t = xmlGetProp(c, BAD_CAST "top");  b = xmlGetProp(c, BAD_CAST "bottom");
        e->texcoord.x = UIWow_XmlFloat(l, 0.0f); e->texcoord.y = UIWow_XmlFloat(t, 0.0f);
        e->texcoord.w = UIWow_XmlFloat(r, 1.0f) - e->texcoord.x;
        e->texcoord.h = UIWow_XmlFloat(b, 1.0f) - e->texcoord.y;
        e->flags |= EF_HAS_TEXCOORD;
        SAFE_DELETE(l, xmlFree); SAFE_DELETE(r, xmlFree); SAFE_DELETE(t, xmlFree); SAFE_DELETE(b, xmlFree);
        return;
    }
}

static void UIWow_XmlReadFont(uiWowXmlElem_t *e, xmlNodePtr node) {
    for (xmlNodePtr c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcasecmp(c->name, BAD_CAST "FontHeight")) {
            for (xmlNodePtr v = c->children; v; v = v->next) {
                if (v->type != XML_ELEMENT_NODE || xmlStrcasecmp(v->name, BAD_CAST "AbsValue")) continue;
                xmlChar *val = xmlGetProp(v, BAD_CAST "val"); e->font_size = UIWow_XmlFloat(val, e->font_size); SAFE_DELETE(val, xmlFree);
            }
        } else if (!xmlStrcasecmp(c->name, BAD_CAST "Color")) {
            xmlChar *r = xmlGetProp(c, BAD_CAST "r"), *g = xmlGetProp(c, BAD_CAST "g"),
                    *b = xmlGetProp(c, BAD_CAST "b"), *a = xmlGetProp(c, BAD_CAST "a");
            e->colors[ELEM_COLOR_TEXT] = MAKE(COLOR32,
                (BYTE)(UIWow_XmlFloat(r,1.f)*255.f), (BYTE)(UIWow_XmlFloat(g,1.f)*255.f),
                (BYTE)(UIWow_XmlFloat(b,1.f)*255.f), (BYTE)(UIWow_XmlFloat(a,1.f)*255.f));
            SAFE_DELETE(r,xmlFree); SAFE_DELETE(g,xmlFree); SAFE_DELETE(b,xmlFree); SAFE_DELETE(a,xmlFree);
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
    for (xmlNodePtr c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "TextInsets")) continue;
        for (xmlNodePtr a = c->children; a; a = a->next) {
            if (a->type != XML_ELEMENT_NODE || xmlStrcasecmp(a->name, BAD_CAST "AbsInset")) continue;
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
    uiWowXmlElem_t temp; memset(&temp, 0, sizeof(temp)); temp.texcoord = MAKE(RECT, 0, 0, 1, 1);
    UIWow_XmlInheritElem(&temp, (char const *)inherits);
    if (file && *file) UIWow_ElemSetStr(&temp, ELEM_FILE, (char const *)file);
    UIWow_XmlReadTexCoords(&temp, child);

    for (int i = 0; uiwow_button_part_tags[i].tag; i++) {
        if (!xmlStrcasecmp(child->name, BAD_CAST uiwow_button_part_tags[i].tag) && UIWow_ElemStr(&temp, ELEM_FILE)) {
            UIWow_ElemSetStr(e, uiwow_button_part_tags[i].file_field, temp.texts[ELEM_FILE]);
            if (name && *name && uiwow_button_part_tags[i].name_field < ELEM_STRING_COUNT)
                UIWow_ElemSetStr(e, uiwow_button_part_tags[i].name_field, (char const *)name);
            if (temp.flags & EF_HAS_TEXCOORD) {
                if (uiwow_button_part_tags[i].texcoord_flag == EF_HAS_HIGHLIGHT_TEXCOORD) {
                    e->highlight_texcoord = temp.texcoord;
                    e->flags |= EF_HAS_HIGHLIGHT_TEXCOORD;
                } else if (uiwow_button_part_tags[i].texcoord_flag == EF_HAS_TEXCOORD) {
                    e->texcoord = temp.texcoord;
                    e->flags |= EF_HAS_TEXCOORD;
                }
            }
            break;
        }
    }
    SAFE_DELETE(file, xmlFree); SAFE_DELETE(inherits, xmlFree); SAFE_DELETE(name, xmlFree);
    UIWow_ElemFreeStrings(&temp);
}

static void UIWow_XmlReadButton(uiWowXmlElem_t *e, xmlNodePtr node) {
    for (xmlNodePtr c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        BOOL is_button_part = false;
        for (int i = 0; uiwow_button_part_tags[i].tag; i++) {
            if (!xmlStrcasecmp(c->name, BAD_CAST uiwow_button_part_tags[i].tag)) {
                UIWow_XmlReadButtonPart(e, c);
                is_button_part = true;
                break;
            }
        }
        if (is_button_part) continue;

        for (int i = 0; uiwow_button_text_tags[i].tag; i++) {
            if (!xmlStrcasecmp(c->name, BAD_CAST uiwow_button_text_tags[i].tag)) {
                uiWowXmlElem_t temp;
                uiWowXmlButtonTextState_t ts = uiwow_button_text_tags[i].state;
                xmlChar *inherits = xmlGetProp(c, BAD_CAST "inherits"), *text = xmlGetProp(c, BAD_CAST "text");
                memset(&temp, 0, sizeof(temp)); temp.halign = e->halign; temp.valign = e->valign;
                UIWow_XmlInheritElem(&temp, (char const *)inherits);
                UIWow_XmlReadAnchor(&temp, c); UIWow_XmlReadJustify(&temp, c); UIWow_XmlReadFont(&temp, c);
                UIWow_XmlInheritElem(e, (char const *)inherits);
                if (text && *text) UIWow_ElemSetStr(e, ELEM_TEXT, (char const *)text);
                e->button_text_colors[ts] = temp.colors[ELEM_COLOR_TEXT];
                e->flags |= EF_HAS_BUTTON_TEXT_COLORS;
                if (ts == WOW_XML_BUTTON_TEXT_NORMAL) e->colors[ELEM_COLOR_TEXT] = temp.colors[ELEM_COLOR_TEXT];
                if (temp.flags & EF_HAS_ANCHOR) e->text_off = temp.offset;
                if (temp.flags & EF_HAS_HALIGN) { e->halign = temp.halign; e->flags |= EF_HAS_HALIGN; }
                if (temp.flags & EF_HAS_VALIGN) { e->valign = temp.valign; e->flags |= EF_HAS_VALIGN; }
                SAFE_DELETE(inherits, xmlFree); SAFE_DELETE(text, xmlFree);
                UIWow_ElemFreeStrings(&temp);
                break;
            }
        }
    }
}

static void UIWow_XmlReadScripts(uiWowXmlElem_t *e, xmlNodePtr node) {
    for (xmlNodePtr c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Scripts")) continue;
        for (xmlNodePtr s = c->children; s; s = s->next) {
            if (s->type != XML_ELEMENT_NODE) continue;
            for (int i = 0; uiwow_script_tags[i].name; i++) {
                if (!xmlStrcasecmp(s->name, BAD_CAST uiwow_script_tags[i].name)) {
                    xmlChar *body = xmlNodeGetContent(s);
                    if (body) {
                        UIWow_ElemSetStr(e, uiwow_script_tags[i].field, (char const *)body);
                        SAFE_DELETE(body, xmlFree);
                    }
                    break;
                }
            }
        }
    }
}

static void UIWow_XmlReadShared(uiWowXmlElem_t *e, xmlNodePtr node) {
    for (int i = 0; uiwow_shared_attrs[i].name; i++) {
        xmlChar *val = xmlGetProp(node, BAD_CAST uiwow_shared_attrs[i].name);
        if (!val) continue;
        switch (uiwow_shared_attrs[i].type) {
            case WOW_ATTR_STR_FIELD:
                if (*val) UIWow_ElemSetStr(e, (uiWowXmlStr_t)uiwow_shared_attrs[i].field_or_flag, (char const *)val);
                break;
            case WOW_ATTR_BOOL_FLAG:
                if (!strcasecmp((char const *)val, "true") || !strcasecmp((char const *)val, "1"))
                    e->flags |= uiwow_shared_attrs[i].field_or_flag;
                break;
            case WOW_ATTR_PASSWORD_FLAG:
                if (strcmp((char const *)val, "0"))
                    e->flags |= uiwow_shared_attrs[i].field_or_flag;
                break;
            case WOW_ATTR_INT_ID:
                if (*val) e->id = atoi((char const *)val);
                break;
        }
        SAFE_DELETE(val, xmlFree);
    }
    UIWow_XmlReadSize(e, node); UIWow_XmlReadAnchor(e, node); UIWow_XmlReadBackdrop(e, node);
    UIWow_XmlReadTexCoords(e, node); UIWow_XmlReadFont(e, node); UIWow_XmlReadJustify(e, node);
    UIWow_XmlReadTextInsets(e, node);
    UIWow_XmlReadButton(e, node); UIWow_XmlReadScripts(e, node);
}

/* ---- Node parser (forward declarations for mutual recursion) ---- */
static void UIWow_XmlParseNode(xmlNodePtr node, int parent, int draw_layer);

static void UIWow_XmlParseLayer(xmlNodePtr node, int parent) {
    xmlChar *level = xmlGetProp(node, BAD_CAST "level");
    int layer = UIWow_XmlLayer((char const *)level);
    SAFE_DELETE(level, xmlFree);
    for (xmlNodePtr c = node->children; c; c = c->next) UIWow_XmlParseNode(c, parent, layer);
}

static void UIWow_XmlParseChildren(xmlNodePtr node, int parent) {
    for (xmlNodePtr c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcasecmp(c->name, BAD_CAST "Layers")) {
            for (xmlNodePtr l = c->children; l; l = l->next)
                if (l->type == XML_ELEMENT_NODE && !xmlStrcasecmp(l->name, BAD_CAST "Layer"))
                    UIWow_XmlParseLayer(l, parent);
            continue;
        }
        if (!xmlStrcasecmp(c->name, BAD_CAST "Frames") || !xmlStrcasecmp(c->name, BAD_CAST "ScrollChild")) {
            for (xmlNodePtr f = c->children; f; f = f->next) UIWow_XmlParseNode(f, parent, WOW_XML_LAYER_ARTWORK);
            continue;
        }
        if (!xmlStrcasecmp(c->name, BAD_CAST "ThumbTexture")) {
            UIWow_XmlParseNode(c, parent, WOW_XML_LAYER_ARTWORK);
            continue;
        }
    }
}

static void UIWow_XmlCloneTemplateChildren(LPCSTR inherits, int dst, LPCSTR dst_name) {
    char inames[256], *tok, *save = NULL;
    if (!inherits || !*inherits || !dst_name || !*dst_name) return;
    snprintf(inames, sizeof(inames), "%s", inherits);
    for (tok = strtok_r(inames, " ,", &save); tok; tok = strtok_r(NULL, " ,", &save)) {
        int tmpl = UIWow_XmlFindByName(tok);
        if (tmpl < 0) continue;
        LPCSTR tmpl_name = wow_xml.elems[tmpl].texts[ELEM_NAME];
        size_t tmpl_len  = tmpl_name ? strlen(tmpl_name) : 0;
        int src_limit = wow_xml.count;
        FOR_LOOP(ci, src_limit) {
            uiWowXmlElem_t const *csrc = &wow_xml.elems[ci];
            char child_name[256] = "";
            if (!(csrc->flags & EF_USED) || csrc->parent != tmpl) continue;
            LPCSTR src_name = csrc->texts[ELEM_NAME];
            if (src_name && *src_name) {
                LPCSTR dollar = strstr(src_name, "$parent");
                if (dollar)
                    snprintf(child_name, sizeof(child_name), "%.*s%s%s", (int)(dollar - src_name), src_name, dst_name, dollar + 7);
                else if (tmpl_len > 0 && strncmp(src_name, tmpl_name, tmpl_len) == 0)
                    snprintf(child_name, sizeof(child_name), "%s%s", dst_name, src_name + tmpl_len);
                else
                    snprintf(child_name, sizeof(child_name), "%s", src_name);
            } else {
                snprintf(child_name, sizeof(child_name), "%s%s", dst_name, csrc->type == WOW_XML_TEXTURE ? "ThumbTexture" : "Child");
            }
            if (!child_name[0] || UIWow_XmlFindByName(child_name) >= 0) continue;
            int clone = UIWow_XmlPushElem(csrc->type, child_name, dst, csrc->draw_layer);
            if (clone < 0) continue;
            wow_xml.elems[clone] = *csrc;
            wow_xml.elems[clone].parent = dst; wow_xml.elems[clone].model = NULL;
            if (wow_xml.elems[clone].relative_to == tmpl) wow_xml.elems[clone].relative_to = dst;
            wow_xml.elems[clone].point2           = csrc->point2           ? strdup(csrc->point2)           : NULL;
            wow_xml.elems[clone].relative_point2  = csrc->relative_point2  ? strdup(csrc->relative_point2)  : NULL;
            wow_xml.elems[clone].relative_name2   = csrc->relative_name2   ? strdup(csrc->relative_name2)   : NULL;
            FOR_LOOP(f, ELEM_STRING_COUNT) wow_xml.elems[clone].texts[f] = csrc->texts[f] ? strdup(csrc->texts[f]) : NULL;
            free(wow_xml.elems[clone].texts[ELEM_NAME]);
            wow_xml.elems[clone].texts[ELEM_NAME] = strdup(child_name);
            if (tmpl_len > 0 && wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME]) {
                LPCSTR rel = wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME];
                if (strncmp(rel, tmpl_name, tmpl_len) == 0) {
                    char res[256]; snprintf(res, sizeof(res), "%s%s", dst_name, rel + tmpl_len);
                    free(wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME]);
                    wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME] = strdup(res);
                }
                int ri = UIWow_XmlFindByName(wow_xml.elems[clone].texts[ELEM_RELATIVE_NAME]);
                if (ri >= 0) wow_xml.elems[clone].relative_to = ri;
            }
            if (tmpl_len > 0 && wow_xml.elems[clone].relative_name2) {
                LPCSTR r2 = wow_xml.elems[clone].relative_name2;
                if (strncmp(r2, tmpl_name, tmpl_len) == 0) {
                    char res2[256]; snprintf(res2, sizeof(res2), "%s%s", dst_name, r2 + tmpl_len);
                    free(wow_xml.elems[clone].relative_name2);
                    wow_xml.elems[clone].relative_name2 = strdup(res2);
                }
                int r2i = UIWow_XmlFindByName(wow_xml.elems[clone].relative_name2);
                if (r2i >= 0) wow_xml.elems[clone].relative_to2 = r2i;
            } else if (wow_xml.elems[clone].relative_to2 == tmpl) {
                wow_xml.elems[clone].relative_to2 = dst;
            }
            UI_XmlOnFramePublish(clone);
            if (src_name && *src_name) UIWow_XmlCloneTemplateChildren(src_name, clone, child_name);
        }
    }
}

static void UIWow_XmlParseNode(xmlNodePtr node, int parent, int draw_layer) {
    uiWowXmlType_t type = WOW_XML_FRAME;
    DWORD node_flags = 0;
    BOOL recognized = false;
    xmlChar *name_attr, *parent_attr, *inherits_attr;
    int idx;

    if (!node || node->type != XML_ELEMENT_NODE || !node->name) return;
    if (!xmlStrcasecmp(node->name, BAD_CAST "Layer"))  { UIWow_XmlParseLayer(node, parent); return; }
    if (!xmlStrcasecmp(node->name, BAD_CAST "Frames") || !xmlStrcasecmp(node->name, BAD_CAST "Layers")) { UIWow_XmlParseChildren(node, parent); return; }

    for (int i = 0; uiwow_node_types[i].tag; i++) {
        if (!xmlStrcasecmp(node->name, BAD_CAST uiwow_node_types[i].tag)) {
            type = uiwow_node_types[i].type;
            node_flags = uiwow_node_types[i].flags;
            recognized = true;
            break;
        }
    }
    if (!recognized) return;

    name_attr     = xmlGetProp(node, BAD_CAST "name");
    parent_attr   = xmlGetProp(node, BAD_CAST "parent");
    inherits_attr = xmlGetProp(node, BAD_CAST "inherits");
    char resolved_name[256] = "";
    if (name_attr && *name_attr) {
        LPCSTR pname = (parent >= 0 && parent < wow_xml.count) ? wow_xml.elems[parent].texts[ELEM_NAME] : NULL;
        LPCSTR raw = (char const *)name_attr;
        LPCSTR dollar = strstr(raw, "$parent");
        if (dollar && pname && *pname)
            snprintf(resolved_name, sizeof(resolved_name), "%.*s%s%s", (int)(dollar - raw), raw, pname, dollar + 7);
        else
            snprintf(resolved_name, sizeof(resolved_name), "%s", raw);
    }
    idx = UIWow_XmlPushElem(type, resolved_name[0] ? resolved_name : NULL, parent, draw_layer);
    SAFE_DELETE(name_attr, xmlFree);
    if (idx < 0) { SAFE_DELETE(parent_attr, xmlFree); SAFE_DELETE(inherits_attr, xmlFree); UI_XmlPrintf("UIWow: XML element limit exceeded\n"); return; }
    wow_xml.elems[idx].flags |= node_flags;

    UIWow_XmlCloneTemplateChildren((char const *)inherits_attr, idx, resolved_name[0] ? resolved_name : NULL);
    UIWow_XmlInheritElem(&wow_xml.elems[idx], (char const *)inherits_attr);
    SAFE_DELETE(inherits_attr, xmlFree);
    if (parent_attr && *parent_attr) {
        uiWowXmlElem_t *e = &wow_xml.elems[idx];
        UIWow_ElemSetStr(e, ELEM_PARENT_NAME, (char const *)parent_attr);
        int np = UIWow_XmlFindByName(e->texts[ELEM_PARENT_NAME]);
        if (np >= 0) e->parent = np;
    }
    SAFE_DELETE(parent_attr, xmlFree);
    if (s_current_xml_path[0]) UIWow_ElemSetStr(&wow_xml.elems[idx], ELEM_SOURCE_FILE, s_current_xml_path);
    UIWow_XmlReadShared(&wow_xml.elems[idx], node);
    UI_XmlOnFramePublish(idx);
    UIWow_XmlParseChildren(node, idx);
    if (wow_xml.elems[idx].flags & EF_IS_SCROLLFRAME) {
        FOR_LOOP(j, wow_xml.count) {
            uiWowXmlElem_t *c = &wow_xml.elems[j];
            LPCSTR cn;
            if (!(c->flags & EF_USED) || c->parent != idx) continue;
            cn = c->texts[ELEM_NAME];
            if (cn && strstr(cn, "ScrollChild")) continue;
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

/* ---- Top-level XML processor ---- */

static BOOL UIWow_XMLProcessXml(LPCSTR path, int depth);

BOOL UIWow_XMLProcessFile(LPCSTR path, int depth) {
    LPCSTR ext = strrchr(path ? path : "", '.');
    if (!path || !*path) return false;
    if (ext && !strcasecmp(ext, ".lua")) { UI_XmlLoadScriptFile(path); return true; }
    return UIWow_XMLProcessXml(path, depth);
}

static void UIWow_XMLProcessTopLevel(LPCSTR path, xmlNodePtr root, int depth) {
    snprintf(s_current_xml_path, sizeof(s_current_xml_path), "%s", path ? path : "");
    for (xmlNodePtr n = root->children; n; n = n->next) {
        if (n->type != XML_ELEMENT_NODE || !n->name) continue;
        if (!xmlStrcasecmp(n->name, BAD_CAST "Include")) {
            xmlChar *f = xmlGetProp(n, BAD_CAST "file"); char resolved[PATH_MAX];
            if (!f || !*f) { UI_XmlPrintf("UIWow: %s has <Include> without file\n", path); SAFE_DELETE(f, xmlFree); continue; }
            if (!UIWow_XmlResolvePath(path, (char const *)f, resolved, sizeof(resolved)))
                UI_XmlPrintf("UIWow: include path too long: %s\n", (char const *)f);
            else
                UIWow_XMLProcessFile(resolved, depth + 1);
            SAFE_DELETE(f, xmlFree); continue;
        }
        if (!xmlStrcasecmp(n->name, BAD_CAST "Script")) {
            xmlChar *f = xmlGetProp(n, BAD_CAST "file"), *body = xmlNodeGetContent(n);
            char resolved[PATH_MAX], chunk[512];
            if (f && *f) {
                if (!UIWow_XmlResolvePath(path, (char const *)f, resolved, sizeof(resolved)))
                    UI_XmlPrintf("UIWow: script path too long: %s\n", (char const *)f);
                else
                    UI_XmlLoadScriptFile(resolved);
            }
            if (body && *body) {
                snprintf(chunk, sizeof(chunk), "%s:<Script>", path ? path : "");
                UI_XmlOnScriptBody(chunk, (char const *)body);
            }
            SAFE_DELETE(f, xmlFree); SAFE_DELETE(body, xmlFree); continue;
        }
        UIWow_XmlParseNode(n, -1, WOW_XML_LAYER_ARTWORK);
    }
}

static BOOL UIWow_XMLProcessXml(LPCSTR path, int depth) {
    void *buf = NULL; int size; xmlDocPtr doc; xmlNodePtr root;
    if (depth > 32) { UI_XmlPrintf("UIWow: XML include recursion too deep at %s\n", path); return false; }
    size = UI_XmlFsReadFile(path, &buf);
    if (size <= 0 || !buf) { UI_XmlFsFreeFile(buf); UI_XmlPrintf("UIWow: missing XML %s\n", path); return false; }
    doc = xmlReadMemory((char const *)buf, size, path, NULL, XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    UI_XmlFsFreeFile(buf);
    if (!doc) { UI_XmlPrintf("UIWow: parse failed for %s\n", path); return false; }
    root = xmlDocGetRootElement(doc);
    if (!root) { xmlFreeDoc(doc); UI_XmlPrintf("UIWow: empty XML root in %s\n", path); return false; }
    UIWow_XMLProcessTopLevel(path, root, depth); xmlFreeDoc(doc);
    return true;
}

/* ---- Registry management ---- */

void UIWow_XMLFreeElems(void) {
    FOR_LOOP(i, wow_xml.count) UIWow_ElemFreeStrings(&wow_xml.elems[i]);
}

/* ---- Public API implementations ---- */

BOOL UIWow_XMLLoadFile(LPCSTR path) { return UIWow_XMLProcessXml(path, 0); }

BOOL UIWow_XMLLoadBuffer(LPCSTR buf, int size, LPCSTR debug_name) {
    xmlDocPtr doc; xmlNodePtr root;
    if (!buf || size <= 0) return false;
    doc = xmlReadMemory(buf, size, debug_name ? debug_name : "buffer", NULL,
                        XML_PARSE_NONET | XML_PARSE_NOBLANKS | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return false;
    root = xmlDocGetRootElement(doc);
    if (root) UIWow_XMLProcessTopLevel(debug_name ? debug_name : "buffer", root, 0);
    xmlFreeDoc(doc);
    return true;
}

void UIWow_XMLSetFrameVisible(LPCSTR name, BOOL visible) {
    UIWow_XMLSetShown(UIWow_XmlFindByName(name), visible);
}

/* Named runtime values bind dynamic game state without overriding authored XML geometry. */
BOOL UIWow_XMLSetFrameText(LPCSTR name, LPCSTR text) {
    int idx = UIWow_XmlFindByName(name);
    if (idx < 0)
        return false;
    UIWow_ElemSetStr(&wow_xml.elems[idx], ELEM_TEXT, text ? text : "");
    return true;
}

/* The renderer selects NormalTexture/PushedTexture from this single press state. */
BOOL UIWow_XMLSetButtonPressed(LPCSTR name, BOOL pressed) {
    int idx = UIWow_XmlFindByName(name);
    if (idx < 0 || wow_xml.elems[idx].type != WOW_XML_BUTTON) return false;
    if (pressed) wow_xml.pressed_button = idx;
    else if (wow_xml.pressed_button == idx) wow_xml.pressed_button = -1;
    return true;
}

void UIWow_XMLClearFrames(void) {
    UIWow_XMLFreeElems();
    memset(wow_xml.elems, 0, sizeof(wow_xml.elems));
    wow_xml.count = 0; wow_xml.focus = -1; wow_xml.pressed_button = -1;
    wow_xml.hovered_button = -1; wow_xml.drag.scrollbar_idx = -1;
}

LPCSTR UIWow_XMLHitButton(FLOAT nx, FLOAT ny) {
    int hit = UIWow_XMLHitFrame(nx, ny);
    if (hit < 0) return NULL;
    uiWowXmlElem_t *e = &wow_xml.elems[hit];
    if (e->type != WOW_XML_BUTTON || !(e->flags & EF_ENABLED)) return NULL;
    return UIWow_ElemStr(e, ELEM_ON_CLICK);
}

int    UIWow_XmlFindByNamePub(LPCSTR name)   { return UIWow_XmlFindByName(name); }
void   UIWow_XmlComputeRectPub(int idx, FLOAT *x, FLOAT *y, FLOAT *w, FLOAT *h) {
    RECT r = (idx >= 0 && idx < wow_xml.count) ? UIWow_XmlComputeRect(idx) : MAKE(RECT, 0,0,0,0);
    if (x) *x = r.x; if (y) *y = r.y; if (w) *w = r.w; if (h) *h = r.h;
}
int    UIWow_XmlElemCount(void)              { return wow_xml.count; }
int    UIWow_XmlElemType(int idx)            { return (idx>=0&&idx<wow_xml.count) ? (int)wow_xml.elems[idx].type : -1; }
LPCSTR UIWow_XmlElemName(int idx)            { return (idx>=0&&idx<wow_xml.count) ? UIWow_ElemStr(&wow_xml.elems[idx], ELEM_NAME) : NULL; }
LPCSTR UIWow_XmlElemText(int idx)            { return (idx>=0&&idx<wow_xml.count) ? UIWow_ElemStr(&wow_xml.elems[idx], ELEM_TEXT) : NULL; }
LPCSTR UIWow_XmlElemOnClick(int idx)         { return (idx>=0&&idx<wow_xml.count) ? UIWow_ElemStr(&wow_xml.elems[idx], ELEM_ON_CLICK) : NULL; }
LPCSTR UIWow_XmlElemPoint(int idx)           { return (idx>=0&&idx<wow_xml.count) ? UIWow_ElemStr(&wow_xml.elems[idx], ELEM_POINT) : NULL; }
int    UIWow_XmlElemHidden(int idx)          { return (idx>=0&&idx<wow_xml.count) && (wow_xml.elems[idx].flags&EF_HIDDEN) ? 1 : 0; }
LPCSTR UIWow_XmlElemParent(int idx)          { return (idx>=0&&idx<wow_xml.count) ? UIWow_ElemStr(&wow_xml.elems[idx], ELEM_PARENT_NAME) : NULL; }

#endif /* STB_WOW_XML_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif
#endif /* stb_wowxml_h */
