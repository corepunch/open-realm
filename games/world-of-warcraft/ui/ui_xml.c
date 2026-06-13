/* ui_xml.c — WoW Glue FrameXML-style loader/runtime (TOC, Include/Script, frame registry, basic drawing). */
#include "ui_local.h"

#include <ctype.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <limits.h>

typedef enum { WOW_XML_FRAME, WOW_XML_MODEL, WOW_XML_TEXTURE, WOW_XML_FONTSTRING, WOW_XML_BUTTON, WOW_XML_EDITBOX } uiWowXmlType_t;
typedef struct {
    BOOL used;
    uiWowXmlType_t type;
    int parent;
    int relative_to;
    int draw_layer;
    int focusable;
    char name[128], parent_name[128], relative_name[128], file[PATH_MAX], normal_file[PATH_MAX], pushed_file[PATH_MAX], text[256], point[24], relative_point[24], backdrop_bg[PATH_MAX], backdrop_edge[PATH_MAX];
    char on_click[1024], on_load[1024], on_show[1024], on_enter[512], on_leave[512], on_enter_pressed[512], on_escape_pressed[512], on_tab_pressed[512];
    FLOAT x, y, w, h, ox, oy, alpha, edge_size, font_size, text_left, text_bottom;
    BOOL has_anchor, has_size, hidden, virtual_frame, password, enabled, set_all_points;
    COLOR32 backdrop_color, backdrop_border_color, text_color, vertex_color;
    LPMODEL model;
} uiWowXmlElem_t;

enum { WOW_XML_MAX_ELEMS = 1024, WOW_XML_LAYER_BACKGROUND = 0, WOW_XML_LAYER_BORDER = 1, WOW_XML_LAYER_ARTWORK = 2, WOW_XML_LAYER_OVERLAY = 3 };
static struct {
    uiWowXmlElem_t elems[WOW_XML_MAX_ELEMS];
    int count;
    int focus;
    BOOL lua_ready;
} wow_xml;

static FLOAT UIWow_XmlFloat(xmlChar const *s, FLOAT fallback) { return s && *s ? (FLOAT)atof((char const *)s) : fallback; }
static int UIWow_XmlLayer(LPCSTR level) {
    if (!level || !*level) return WOW_XML_LAYER_ARTWORK;
    if (!strcasecmp(level, "BACKGROUND")) return WOW_XML_LAYER_BACKGROUND;
    if (!strcasecmp(level, "BORDER")) return WOW_XML_LAYER_BORDER;
    if (!strcasecmp(level, "OVERLAY")) return WOW_XML_LAYER_OVERLAY;
    return WOW_XML_LAYER_ARTWORK;
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
    FOR_LOOP(i, wow_xml.count) if (wow_xml.elems[i].used && !strcmp(wow_xml.elems[i].name, name)) return i;
    return -1;
}

static int UIWow_XmlPushElem(uiWowXmlType_t type, LPCSTR name, int parent, int draw_layer) {
    uiWowXmlElem_t *e;
    if (wow_xml.count >= WOW_XML_MAX_ELEMS) return -1;
    e = &wow_xml.elems[wow_xml.count]; memset(e, 0, sizeof(*e));
    e->used = true; e->type = type; e->parent = parent; e->relative_to = parent; e->draw_layer = draw_layer; e->alpha = 1.0f; e->enabled = true;
    e->font_size = 14.0f; e->text_color = COLOR32_WHITE; e->vertex_color = COLOR32_WHITE;
    e->backdrop_color = MAKE(COLOR32, 23, 23, 23, 120); e->backdrop_border_color = MAKE(COLOR32, 204, 204, 204, 255);
    snprintf(e->name, sizeof(e->name), "%s", name && *name ? name : "");
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
        if (!e->has_size && src->has_size) { e->w = src->w; e->h = src->h; e->has_size = true; }
        if (!e->file[0] && src->file[0]) snprintf(e->file, sizeof(e->file), "%s", src->file);
        if (!e->normal_file[0] && src->normal_file[0]) snprintf(e->normal_file, sizeof(e->normal_file), "%s", src->normal_file);
        if (!e->pushed_file[0] && src->pushed_file[0]) snprintf(e->pushed_file, sizeof(e->pushed_file), "%s", src->pushed_file);
        if (!e->text[0] && src->text[0]) snprintf(e->text, sizeof(e->text), "%s", src->text);
        if (!e->backdrop_bg[0] && src->backdrop_bg[0]) snprintf(e->backdrop_bg, sizeof(e->backdrop_bg), "%s", src->backdrop_bg);
        if (!e->backdrop_edge[0] && src->backdrop_edge[0]) snprintf(e->backdrop_edge, sizeof(e->backdrop_edge), "%s", src->backdrop_edge);
        if (src->edge_size > 0.0f) e->edge_size = src->edge_size;
        if (src->font_size > 0.0f) e->font_size = src->font_size;
        e->text_color = src->text_color;
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
    uiWowXmlElem_t const *e = &wow_xml.elems[idx]; RECT parent = MAKE(RECT, 0, 0, 1, 1), out = MAKE(RECT, 0, 0, e->w > 0 ? e->w : 0.2f, e->h > 0 ? e->h : 0.05f);
    FLOAT ax, ay;
    if (e->parent >= 0 && e->parent < wow_xml.count) parent = UIWow_XmlComputeRect(e->parent);
    if (e->set_all_points) return parent;
    if (!e->has_anchor) {
        out.x = parent.x; out.y = parent.y; return out;
    }
    if (e->relative_to >= 0 && e->relative_to < wow_xml.count) parent = UIWow_XmlComputeRect(e->relative_to);
    UIWow_XmlRectPoint(&parent, e->relative_point[0] ? e->relative_point : e->point, &ax, &ay); ax += e->ox; ay += e->oy;
    if (!strcasecmp(e->point, "TOPLEFT")) out.x = ax, out.y = ay;
    else if (!strcasecmp(e->point, "TOP")) out.x = ax - out.w * 0.5f, out.y = ay;
    else if (!strcasecmp(e->point, "TOPRIGHT")) out.x = ax - out.w, out.y = ay;
    else if (!strcasecmp(e->point, "LEFT")) out.x = ax, out.y = ay - out.h * 0.5f;
    else if (!strcasecmp(e->point, "CENTER")) out.x = ax - out.w * 0.5f, out.y = ay - out.h * 0.5f;
    else if (!strcasecmp(e->point, "RIGHT")) out.x = ax - out.w, out.y = ay - out.h * 0.5f;
    else if (!strcasecmp(e->point, "BOTTOMLEFT")) out.x = ax, out.y = ay - out.h;
    else if (!strcasecmp(e->point, "BOTTOM")) out.x = ax - out.w * 0.5f, out.y = ay - out.h;
    else if (!strcasecmp(e->point, "BOTTOMRIGHT")) out.x = ax - out.w, out.y = ay - out.h;
    else out.x = ax - out.w * 0.5f, out.y = ay - out.h * 0.5f;
    return out;
}

static int UIWow_FrameFromSelf(lua_State *L) {
    int idx;
    luaL_checktype(L, 1, LUA_TTABLE); lua_getfield(L, 1, "__ow3_index"); idx = (int)luaL_optinteger(L, -1, -1); lua_pop(L, 1);
    return idx >= 0 && idx < wow_xml.count && wow_xml.elems[idx].used ? idx : -1;
}

static void UIWow_XmlPublishFrame(int idx);
static void UIWow_XMLRunFrameScript(int idx, LPCSTR script, LPCSTR event_name);

static int UIWow_LuaFrameShow(lua_State *L) {
    int i = UIWow_FrameFromSelf(L);
    if (i >= 0) {
        BOOL was_hidden = wow_xml.elems[i].hidden;
        wow_xml.elems[i].hidden = false;
        if (was_hidden && wow_xml.elems[i].on_show[0]) UIWow_XMLRunFrameScript(i, wow_xml.elems[i].on_show, "OnShow");
    }
    return 0;
}
static int UIWow_LuaFrameHide(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].hidden = true; return 0; }
static int UIWow_LuaFrameIsVisible(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushboolean(L, i >= 0 && !wow_xml.elems[i].hidden); return 1; }
static int UIWow_LuaFrameSetAlpha(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].alpha = (FLOAT)luaL_optnumber(L, 2, 1.0); return 0; }
static int UIWow_LuaFrameSetText(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) snprintf(wow_xml.elems[i].text, sizeof(wow_xml.elems[i].text), "%s", luaL_optstring(L, 2, "")); return 0; }
static int UIWow_LuaFrameGetText(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushstring(L, i >= 0 ? wow_xml.elems[i].text : ""); return 1; }
static int UIWow_LuaFrameGetName(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushstring(L, i >= 0 ? wow_xml.elems[i].name : ""); return 1; }
static int UIWow_LuaFrameGetParent(lua_State *L) { int i = UIWow_FrameFromSelf(L), p = i >= 0 ? wow_xml.elems[i].parent : -1; if (p >= 0) UIWow_XmlPublishFrame(p); else lua_pushnil(L); return p >= 0 ? (lua_getglobal(L, wow_xml.elems[p].name), 1) : 1; }
static int UIWow_LuaFrameEnable(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].enabled = true; return 0; }
static int UIWow_LuaFrameDisable(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].enabled = false; return 0; }
static int UIWow_LuaFrameIsEnabled(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushboolean(L, i >= 0 && wow_xml.elems[i].enabled); return 1; }
static int UIWow_LuaFrameSetVertexColor(lua_State *L) {
    int i = UIWow_FrameFromSelf(L); FLOAT r = (FLOAT)luaL_optnumber(L, 2, 1.0), g = (FLOAT)luaL_optnumber(L, 3, 1.0), b = (FLOAT)luaL_optnumber(L, 4, 1.0), a = (FLOAT)luaL_optnumber(L, 5, 1.0);
    if (i >= 0) wow_xml.elems[i].vertex_color = MAKE(COLOR32, (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f), (BYTE)(a * 255.0f));
    return 0;
}
static int UIWow_LuaFrameSetBackdropColor(lua_State *L) {
    int i = UIWow_FrameFromSelf(L); FLOAT r = (FLOAT)luaL_optnumber(L, 2, 0.09), g = (FLOAT)luaL_optnumber(L, 3, 0.09), b = (FLOAT)luaL_optnumber(L, 4, 0.09), a = (FLOAT)luaL_optnumber(L, 5, 0.5);
    if (i >= 0) wow_xml.elems[i].backdrop_color = MAKE(COLOR32, (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f), (BYTE)(a * 255.0f));
    return 0;
}
static int UIWow_LuaFrameSetBackdropBorderColor(lua_State *L) {
    int i = UIWow_FrameFromSelf(L); FLOAT r = (FLOAT)luaL_optnumber(L, 2, 0.8), g = (FLOAT)luaL_optnumber(L, 3, 0.8), b = (FLOAT)luaL_optnumber(L, 4, 0.8), a = (FLOAT)luaL_optnumber(L, 5, 1.0);
    if (i >= 0) wow_xml.elems[i].backdrop_border_color = MAKE(COLOR32, (BYTE)(r * 255.0f), (BYTE)(g * 255.0f), (BYTE)(b * 255.0f), (BYTE)(a * 255.0f));
    return 0;
}
static int UIWow_LuaFrameSetFocus(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.focus = i; return 0; }
static int UIWow_LuaFrameHighlightText(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameRegisterEvent(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetSequence(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetCamera(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetFogColor(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetFogNear(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameSetFogFar(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaFrameClearFog(lua_State *L) { (void)L; return 0; }
static int UIWow_LuaGetGlobalCompat(lua_State *L) { lua_getglobal(L, luaL_checkstring(L, 1)); return 1; }

static void UIWow_XMLInstallLuaCompat(void) {
    static luaL_Reg const methods[] = {
        { "Show", UIWow_LuaFrameShow }, { "Hide", UIWow_LuaFrameHide }, { "IsVisible", UIWow_LuaFrameIsVisible }, { "SetAlpha", UIWow_LuaFrameSetAlpha },
        { "SetText", UIWow_LuaFrameSetText }, { "GetText", UIWow_LuaFrameGetText }, { "SetBackdropColor", UIWow_LuaFrameSetBackdropColor }, { "SetBackdropBorderColor", UIWow_LuaFrameSetBackdropBorderColor },
        { "GetName", UIWow_LuaFrameGetName }, { "GetParent", UIWow_LuaFrameGetParent }, { "Enable", UIWow_LuaFrameEnable }, { "Disable", UIWow_LuaFrameDisable }, { "IsEnabled", UIWow_LuaFrameIsEnabled },
        { "SetVertexColor", UIWow_LuaFrameSetVertexColor }, { "SetFocus", UIWow_LuaFrameSetFocus }, { "HighlightText", UIWow_LuaFrameHighlightText }, { "RegisterEvent", UIWow_LuaFrameRegisterEvent }, { "SetSequence", UIWow_LuaFrameSetSequence },
        { "SetCamera", UIWow_LuaFrameSetCamera }, { "SetFogColor", UIWow_LuaFrameSetFogColor }, { "SetFogNear", UIWow_LuaFrameSetFogNear }, { "SetFogFar", UIWow_LuaFrameSetFogFar },
        { "ClearFog", UIWow_LuaFrameClearFog }, { NULL, NULL }
    };
    if (!wow_ui.lua) return;
    if (luaL_newmetatable(wow_ui.lua, "UIWow.Frame")) { lua_pushvalue(wow_ui.lua, -1); lua_setfield(wow_ui.lua, -2, "__index"); luaL_setfuncs(wow_ui.lua, methods, 0); }
    lua_pop(wow_ui.lua, 1); lua_pushcfunction(wow_ui.lua, UIWow_LuaGetGlobalCompat); lua_setglobal(wow_ui.lua, "getglobal");
    wow_xml.lua_ready = true;
}

static void UIWow_XmlPublishFrame(int idx) {
    uiWowXmlElem_t const *e = &wow_xml.elems[idx];
    if (!wow_ui.lua || !e->name[0]) return;
    lua_getglobal(wow_ui.lua, e->name);
    if (lua_istable(wow_ui.lua, -1)) { lua_pop(wow_ui.lua, 1); return; }
    lua_pop(wow_ui.lua, 1);
    lua_newtable(wow_ui.lua);
    lua_pushinteger(wow_ui.lua, idx); lua_setfield(wow_ui.lua, -2, "__ow3_index");
    lua_pushstring(wow_ui.lua, e->name); lua_setfield(wow_ui.lua, -2, "name");
    lua_pushboolean(wow_ui.lua, !e->hidden); lua_setfield(wow_ui.lua, -2, "shown");
    luaL_getmetatable(wow_ui.lua, "UIWow.Frame"); lua_setmetatable(wow_ui.lua, -2);
    lua_setglobal(wow_ui.lua, e->name);
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
            e->w = UIWow_XmlFloat(x, 0.0f) / 1024.0f; e->h = UIWow_XmlFloat(y, 0.0f) / 768.0f; e->has_size = (e->w > 0 || e->h > 0);
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
            snprintf(e->point, sizeof(e->point), "%s", point && *point ? (char const *)point : "CENTER");
            snprintf(e->relative_point, sizeof(e->relative_point), "%s", relative && *relative ? (char const *)relative : e->point);
            if (relative_to && *relative_to) { snprintf(e->relative_name, sizeof(e->relative_name), "%s", (char const *)relative_to); e->relative_to = UIWow_XmlFindByName(e->relative_name); }
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
                        e->ox = UIWow_XmlFloat(x, 0.0f) / 1024.0f; e->oy = -UIWow_XmlFloat(y, 0.0f) / 768.0f;
                        SAFE_DELETE(x, xmlFree); SAFE_DELETE(y, xmlFree);
                    }
                }
            }
            e->has_anchor = true; return;
        }
    }
}

static void UIWow_XmlReadBackdrop(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        xmlChar *bg, *edge;
        xmlNodePtr d;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "Backdrop")) continue;
        bg = xmlGetProp(c, BAD_CAST "bgFile"); edge = xmlGetProp(c, BAD_CAST "edgeFile");
        if (bg && *bg) snprintf(e->backdrop_bg, sizeof(e->backdrop_bg), "%s", (char const *)bg);
        if (edge && *edge) snprintf(e->backdrop_edge, sizeof(e->backdrop_edge), "%s", (char const *)edge);
        SAFE_DELETE(bg, xmlFree); SAFE_DELETE(edge, xmlFree);
        for (d = c->children; d; d = d->next) if (d->type == XML_ELEMENT_NODE && !xmlStrcasecmp(d->name, BAD_CAST "EdgeSize")) {
            xmlNodePtr v; for (v = d->children; v; v = v->next) if (v->type == XML_ELEMENT_NODE && !xmlStrcasecmp(v->name, BAD_CAST "AbsValue")) {
                xmlChar *val = xmlGetProp(v, BAD_CAST "val"); e->edge_size = UIWow_XmlFloat(val, 16.0f) / 1024.0f; SAFE_DELETE(val, xmlFree);
            }
        }
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
            e->text_color = MAKE(COLOR32, (BYTE)(UIWow_XmlFloat(r, 1.0f) * 255.0f), (BYTE)(UIWow_XmlFloat(g, 1.0f) * 255.0f), (BYTE)(UIWow_XmlFloat(b, 1.0f) * 255.0f), (BYTE)(UIWow_XmlFloat(a, 1.0f) * 255.0f));
            SAFE_DELETE(r, xmlFree); SAFE_DELETE(g, xmlFree); SAFE_DELETE(b, xmlFree); SAFE_DELETE(a, xmlFree);
        }
    }
}

static void UIWow_XmlReadTextInsets(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        xmlNodePtr a;
        if (c->type != XML_ELEMENT_NODE || xmlStrcasecmp(c->name, BAD_CAST "TextInsets")) continue;
        for (a = c->children; a; a = a->next) if (a->type == XML_ELEMENT_NODE && !xmlStrcasecmp(a->name, BAD_CAST "AbsInset")) {
            xmlChar *left = xmlGetProp(a, BAD_CAST "left"), *bottom = xmlGetProp(a, BAD_CAST "bottom");
            e->text_left = UIWow_XmlFloat(left, e->text_left) / 1024.0f; e->text_bottom = UIWow_XmlFloat(bottom, e->text_bottom) / 768.0f;
            SAFE_DELETE(left, xmlFree); SAFE_DELETE(bottom, xmlFree);
        }
    }
}

static void UIWow_XmlReadButtonPart(uiWowXmlElem_t *e, xmlNodePtr child) {
    xmlChar *file = xmlGetProp(child, BAD_CAST "file"), *inherits = xmlGetProp(child, BAD_CAST "inherits");
    uiWowXmlElem_t temp;
    memset(&temp, 0, sizeof(temp));
    UIWow_XmlInheritElem(&temp, (char const *)inherits);
    if (file && *file) snprintf(temp.file, sizeof(temp.file), "%s", (char const *)file);
    if (!xmlStrcasecmp(child->name, BAD_CAST "NormalTexture") && temp.file[0]) snprintf(e->normal_file, sizeof(e->normal_file), "%s", temp.file);
    else if (!xmlStrcasecmp(child->name, BAD_CAST "PushedTexture") && temp.file[0]) snprintf(e->pushed_file, sizeof(e->pushed_file), "%s", temp.file);
    SAFE_DELETE(file, xmlFree); SAFE_DELETE(inherits, xmlFree);
}

static void UIWow_XmlReadButton(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlNodePtr c;
    for (c = node->children; c; c = c->next) {
        if (c->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcasecmp(c->name, BAD_CAST "NormalTexture") || !xmlStrcasecmp(c->name, BAD_CAST "PushedTexture")) UIWow_XmlReadButtonPart(e, c);
        else if (!xmlStrcasecmp(c->name, BAD_CAST "NormalText") || !xmlStrcasecmp(c->name, BAD_CAST "HighlightText") || !xmlStrcasecmp(c->name, BAD_CAST "DisabledText")) {
            xmlChar *inherits = xmlGetProp(c, BAD_CAST "inherits"); UIWow_XmlInheritElem(e, (char const *)inherits); SAFE_DELETE(inherits, xmlFree);
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
            if (!xmlStrcasecmp(s->name, BAD_CAST "OnClick")) snprintf(e->on_click, sizeof(e->on_click), "%s", (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnLoad")) snprintf(e->on_load, sizeof(e->on_load), "%s", (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnShow")) snprintf(e->on_show, sizeof(e->on_show), "%s", (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnEnterPressed")) snprintf(e->on_enter_pressed, sizeof(e->on_enter_pressed), "%s", (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnEscapePressed")) snprintf(e->on_escape_pressed, sizeof(e->on_escape_pressed), "%s", (char const *)body);
            else if (!xmlStrcasecmp(s->name, BAD_CAST "OnTabPressed")) snprintf(e->on_tab_pressed, sizeof(e->on_tab_pressed), "%s", (char const *)body);
            SAFE_DELETE(body, xmlFree);
        }
    }
}

static void UIWow_XmlReadShared(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlChar *file = xmlGetProp(node, BAD_CAST "file"), *hidden = xmlGetProp(node, BAD_CAST "hidden"), *text = xmlGetProp(node, BAD_CAST "text"), *virt = xmlGetProp(node, BAD_CAST "virtual"), *all = xmlGetProp(node, BAD_CAST "setAllPoints"), *password = xmlGetProp(node, BAD_CAST "password");
    if (file && *file) snprintf(e->file, sizeof(e->file), "%s", (char const *)file);
    if (text && *text) snprintf(e->text, sizeof(e->text), "%s", (char const *)text);
    if (hidden && *hidden && !strcasecmp((char const *)hidden, "true")) e->hidden = true;
    if (virt && *virt && !strcasecmp((char const *)virt, "true")) e->virtual_frame = true;
    if (all && *all && !strcasecmp((char const *)all, "true")) e->set_all_points = true;
    if (password && *password && strcmp((char const *)password, "0")) e->password = true;
    SAFE_DELETE(file, xmlFree); SAFE_DELETE(hidden, xmlFree); SAFE_DELETE(text, xmlFree); SAFE_DELETE(virt, xmlFree); SAFE_DELETE(all, xmlFree); SAFE_DELETE(password, xmlFree);
    UIWow_XmlReadSize(e, node); UIWow_XmlReadAnchor(e, node); UIWow_XmlReadBackdrop(e, node); UIWow_XmlReadFont(e, node); UIWow_XmlReadTextInsets(e, node); UIWow_XmlReadButton(e, node); UIWow_XmlReadScripts(e, node);
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
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Button")) type = WOW_XML_BUTTON;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "EditBox")) type = WOW_XML_EDITBOX;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "NormalTexture") || !xmlStrcasecmp(node->name, BAD_CAST "PushedTexture") || !xmlStrcasecmp(node->name, BAD_CAST "DisabledTexture") || !xmlStrcasecmp(node->name, BAD_CAST "HighlightTexture")) type = WOW_XML_TEXTURE;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "NormalText") || !xmlStrcasecmp(node->name, BAD_CAST "DisabledText") || !xmlStrcasecmp(node->name, BAD_CAST "HighlightText")) type = WOW_XML_FONTSTRING;
    else return;
    name_attr = xmlGetProp(node, BAD_CAST "name"); parent_attr = xmlGetProp(node, BAD_CAST "parent"); inherits_attr = xmlGetProp(node, BAD_CAST "inherits");
    idx = UIWow_XmlPushElem(type, (char const *)name_attr, parent, draw_layer); SAFE_DELETE(name_attr, xmlFree);
    if (idx < 0) { SAFE_DELETE(parent_attr, xmlFree); UIWow_Printf("UIWow: XML element limit exceeded\n"); return; }
    UIWow_XmlInheritElem(&wow_xml.elems[idx], (char const *)inherits_attr); SAFE_DELETE(inherits_attr, xmlFree);
    if (parent_attr && *parent_attr) {
        uiWowXmlElem_t *e = &wow_xml.elems[idx]; int named_parent;
        snprintf(e->parent_name, sizeof(e->parent_name), "%s", (char const *)parent_attr);
        named_parent = UIWow_XmlFindByName(e->parent_name); if (named_parent >= 0) e->parent = named_parent;
    }
    SAFE_DELETE(parent_attr, xmlFree);
    if (type == WOW_XML_EDITBOX) wow_xml.elems[idx].focusable = 1;
    UIWow_XmlReadShared(&wow_xml.elems[idx], node); UIWow_XmlPublishFrame(idx); UIWow_XmlParseChildren(node, idx);
    if (wow_xml.elems[idx].on_load[0]) UIWow_XMLRunFrameScript(idx, wow_xml.elems[idx].on_load, "OnLoad");
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

void UIWow_XMLInitRuntime(void) { memset(&wow_xml, 0, sizeof(wow_xml)); wow_xml.focus = -1; UIWow_XMLInstallLuaCompat(); }
void UIWow_XMLShutdownRuntime(void) {
    if (wow_ui.renderer && wow_ui.renderer->ReleaseModel) FOR_LOOP(i, wow_xml.count) SAFE_DELETE(wow_xml.elems[i].model, wow_ui.renderer->ReleaseModel);
    memset(&wow_xml, 0, sizeof(wow_xml)); wow_xml.focus = -1;
}

BOOL UIWow_XMLLoadGlueFromToc(LPCSTR toc_path) {
    if (!wow_ui.lua) { UIWow_Printf("UIWow: XML runtime requires active lua_State\n"); return false; }
    if (!wow_xml.lua_ready) UIWow_XMLInstallLuaCompat();
    memset(wow_xml.elems, 0, sizeof(wow_xml.elems)); wow_xml.count = 0; wow_xml.focus = -1;
    if (!UIWow_XMLLoadFromToc(toc_path)) return false;
    FOR_LOOP(i, wow_xml.count) {
        uiWowXmlElem_t *e = &wow_xml.elems[i];
        if (e->parent_name[0]) {
            int parent = UIWow_XmlFindByName(e->parent_name);
            if (parent >= 0) e->parent = parent;
        }
        if (e->relative_name[0]) {
            int relative = UIWow_XmlFindByName(e->relative_name);
            if (relative >= 0) e->relative_to = relative;
        }
    }
    UIWow_Printf("UIWow: FrameXML loaded from %s (elements=%d)\n", toc_path, wow_xml.count);
    return wow_xml.count > 0;
}

static void UIWow_XMLRunFrameScript(int idx, LPCSTR script, LPCSTR event_name) {
    char chunk[192];
    if (!wow_ui.lua || idx < 0 || idx >= wow_xml.count || !script || !*script) return;
    UIWow_XmlPublishFrame(idx);
    lua_getglobal(wow_ui.lua, wow_xml.elems[idx].name); lua_setglobal(wow_ui.lua, "this");
    lua_pushstring(wow_ui.lua, event_name ? event_name : ""); lua_setglobal(wow_ui.lua, "event");
    snprintf(chunk, sizeof(chunk), "%s:%s", wow_xml.elems[idx].name[0] ? wow_xml.elems[idx].name : "<anon>", event_name ? event_name : "Script");
    if (luaL_loadbuffer(wow_ui.lua, script, strlen(script), chunk) != LUA_OK) {
        UIWow_Printf("UIWow Lua load: %s\n", lua_tostring(wow_ui.lua, -1)); lua_pop(wow_ui.lua, 1);
    } else {
        UIWow_LuaPCall(0);
    }
    lua_pushnil(wow_ui.lua); lua_setglobal(wow_ui.lua, "this");
}

static void UIWow_XMLDrawBackdrop(uiWowXmlElem_t const *e, LPCRECT r) {
    if (!wow_ui.renderer) return;
    if (e->backdrop_bg[0]) {
        LPTEXTURE bg = UIWow_LoadTexture(e->backdrop_bg); RECT uv = MAKE(RECT, 0, 0, 1, 1);
        if (bg) wow_ui.renderer->DrawImage(bg, r, &uv, e->backdrop_color);
    }
    if (e->backdrop_edge[0] && e->edge_size > 0.0f) {
        LPTEXTURE bd = UIWow_LoadTexture(e->backdrop_edge); RECT uv = MAKE(RECT, 0, 0, 1, 1); RECT top, bot, lft, rgt;
        if (!bd) return;
        top = MAKE(RECT, r->x, r->y, r->w, e->edge_size); bot = MAKE(RECT, r->x, r->y + r->h - e->edge_size, r->w, e->edge_size);
        lft = MAKE(RECT, r->x, r->y, e->edge_size, r->h); rgt = MAKE(RECT, r->x + r->w - e->edge_size, r->y, e->edge_size, r->h);
        wow_ui.renderer->DrawImage(bd, &top, &uv, e->backdrop_border_color); wow_ui.renderer->DrawImage(bd, &bot, &uv, e->backdrop_border_color);
        wow_ui.renderer->DrawImage(bd, &lft, &uv, e->backdrop_border_color); wow_ui.renderer->DrawImage(bd, &rgt, &uv, e->backdrop_border_color);
    }
}

static BOOL UIWow_XMLIsVisible(int idx) {
    while (idx >= 0 && idx < wow_xml.count) {
        uiWowXmlElem_t const *e = &wow_xml.elems[idx];
        if (!e->used || e->hidden || e->virtual_frame) return false;
        idx = e->parent;
    }
    return true;
}

static LPCSTR UIWow_XMLResolveText(uiWowXmlElem_t const *e, LPSTR out, size_t out_size) {
    if (!e || !e->text[0]) return "";
    if (wow_ui.lua) {
        lua_getglobal(wow_ui.lua, e->text);
        if (lua_isstring(wow_ui.lua, -1)) {
            snprintf(out, out_size, "%s", lua_tostring(wow_ui.lua, -1));
            lua_pop(wow_ui.lua, 1);
            return out;
        }
        lua_pop(wow_ui.lua, 1);
    }
    snprintf(out, out_size, "%s", e->text);
    return out;
}

static LPCSTR UIWow_XMLDisplayText(uiWowXmlElem_t const *e, LPSTR out, size_t out_size) {
    if (!e || !e->password) return UIWow_XMLResolveText(e, out, out_size);
    memset(out, '*', MIN(strlen(e->text), out_size - 1)); out[MIN(strlen(e->text), out_size - 1)] = '\0';
    return out;
}

void UIWow_XMLDraw(void) {
    UIWow_EnsureRenderer(); if (!wow_ui.renderer) return;
    for (int layer = WOW_XML_LAYER_BACKGROUND; layer <= WOW_XML_LAYER_OVERLAY; layer++) FOR_LOOP(i, wow_xml.count) {
        uiWowXmlElem_t *e = &wow_xml.elems[i]; RECT r; RECT uv = MAKE(RECT, 0, 0, 1, 1); char text[512];
        if (!e->used || e->draw_layer != layer || !UIWow_XMLIsVisible((int)i)) continue;
        r = UIWow_XmlComputeRect(i);
        if (e->type == WOW_XML_MODEL && e->file[0]) {
            if (!e->model && wow_ui.renderer->LoadModel) e->model = wow_ui.renderer->LoadModel(e->file);
            if (e->model && wow_ui.renderer->DrawPortrait) wow_ui.renderer->DrawPortrait(e->model, &r, "Stand");
            else if (!wow_ui.renderer->LoadModel) UIWow_WarnOnce(WOW_UI_WARN_NO_MODEL_LOADER, "UIWow: renderer has no model loader; XML model frames skipped\n");
        }
        if (e->type == WOW_XML_FRAME || e->type == WOW_XML_BUTTON || e->type == WOW_XML_EDITBOX) UIWow_XMLDrawBackdrop(e, &r);
        if ((e->file[0] && e->type == WOW_XML_TEXTURE) || (e->type == WOW_XML_BUTTON && (e->normal_file[0] || e->file[0]))) {
            LPTEXTURE t = UIWow_LoadTexture(e->type == WOW_XML_BUTTON && e->normal_file[0] ? e->normal_file : e->file);
            if (t) wow_ui.renderer->DrawImage(t, &r, &uv, MAKE(COLOR32, e->vertex_color.r, e->vertex_color.g, e->vertex_color.b, (BYTE)(e->vertex_color.a * e->alpha)));
        }
        if (e->text[0] && (e->type == WOW_XML_FONTSTRING || e->type == WOW_XML_EDITBOX || e->type == WOW_XML_BUTTON)) {
            LPCFONT f = UIWow_LoadFont((DWORD)e->font_size); RECT tr = MAKE(RECT, r.x + e->text_left, r.y, r.w - e->text_left, r.h - e->text_bottom);
            LPCSTR display = UIWow_XMLDisplayText(e, text, sizeof(text));
            if (f) wow_ui.renderer->DrawText(&MAKE(drawText_t, .font = f, .text = display, .rect = tr, .color = MAKE(COLOR32, e->text_color.r, e->text_color.g, e->text_color.b, (BYTE)(e->text_color.a * e->alpha)), .textWidth = tr.w, .lineHeight = tr.h, .wordWrap = false, .halign = e->type == WOW_XML_BUTTON ? FONT_JUSTIFYCENTER : FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYMIDDLE));
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
    if (!down || !wow_ui.lua) return false;
    hit = UIWow_XMLHitFrame(x / 1024.0f, y / 768.0f);
    if (hit < 0) return false;
    if (wow_xml.elems[hit].type == WOW_XML_EDITBOX) { wow_xml.focus = hit; return true; }
    if (wow_xml.elems[hit].type == WOW_XML_BUTTON && wow_xml.elems[hit].enabled && wow_xml.elems[hit].on_click[0]) {
        (void)button; UIWow_XMLRunFrameScript(hit, wow_xml.elems[hit].on_click, "OnClick"); return true;
    }
    return false;
}

BOOL UIWow_XMLTextInput(LPCSTR text) {
    uiWowXmlElem_t *e;
    if (wow_xml.focus < 0 || wow_xml.focus >= wow_xml.count || !text || !*text) return false;
    e = &wow_xml.elems[wow_xml.focus];
    if (e->type != WOW_XML_EDITBOX) return false;
    if (!strcmp(text, "\b")) {
        size_t n = strlen(e->text);
        if (n > 0) e->text[n - 1] = '\0';
        return true;
    }
    if (!strcmp(text, "\r") || !strcmp(text, "\n")) {
        if (e->on_enter_pressed[0]) UIWow_XMLRunFrameScript(wow_xml.focus, e->on_enter_pressed, "OnEnterPressed");
        return true;
    }
    if (strlen(e->text) + strlen(text) < sizeof(e->text)) strncat(e->text, text, sizeof(e->text) - strlen(e->text) - 1);
    return true;
}

BOOL UIWow_XMLKeyEvent(int key, BOOL down, DWORD time) {
    uiWowXmlElem_t *e;
    (void)time;
    if (!down || wow_xml.focus < 0 || wow_xml.focus >= wow_xml.count) return false;
    e = &wow_xml.elems[wow_xml.focus];
    if (e->type != WOW_XML_EDITBOX) return false;
    if (key == '\r' || key == '\n') { if (e->on_enter_pressed[0]) UIWow_XMLRunFrameScript(wow_xml.focus, e->on_enter_pressed, "OnEnterPressed"); return true; }
    if (key == '\b' || key == 127) {
        size_t n = strlen(e->text);
        if (n > 0) e->text[n - 1] = '\0';
        return true;
    }
    if (key == '\t') { if (e->on_tab_pressed[0]) UIWow_XMLRunFrameScript(wow_xml.focus, e->on_tab_pressed, "OnTabPressed"); return true; }
    if (key == 27) { if (e->on_escape_pressed[0]) UIWow_XMLRunFrameScript(wow_xml.focus, e->on_escape_pressed, "OnEscapePressed"); return true; }
    return false;
}
