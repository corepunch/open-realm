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
    int draw_layer;
    int focusable;
    char name[128], parent_name[128], file[PATH_MAX], text[256], point[24], relative_point[24], backdrop_bg[PATH_MAX], backdrop_edge[PATH_MAX];
    FLOAT x, y, w, h, ox, oy, alpha, edge_size;
    BOOL has_anchor, has_size, hidden;
    COLOR32 backdrop_color, backdrop_border_color;
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
    e->used = true; e->type = type; e->parent = parent; e->draw_layer = draw_layer; e->alpha = 1.0f;
    e->backdrop_color = MAKE(COLOR32, 23, 23, 23, 120); e->backdrop_border_color = MAKE(COLOR32, 204, 204, 204, 255);
    snprintf(e->name, sizeof(e->name), "%s", name && *name ? name : "");
    return wow_xml.count++;
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
    if (!e->has_anchor) {
        out.x = parent.x; out.y = parent.y; return out;
    }
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

static int UIWow_LuaFrameShow(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].hidden = false; return 0; }
static int UIWow_LuaFrameHide(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].hidden = true; return 0; }
static int UIWow_LuaFrameIsVisible(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushboolean(L, i >= 0 && !wow_xml.elems[i].hidden); return 1; }
static int UIWow_LuaFrameSetAlpha(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) wow_xml.elems[i].alpha = (FLOAT)luaL_optnumber(L, 2, 1.0); return 0; }
static int UIWow_LuaFrameSetText(lua_State *L) { int i = UIWow_FrameFromSelf(L); if (i >= 0) snprintf(wow_xml.elems[i].text, sizeof(wow_xml.elems[i].text), "%s", luaL_optstring(L, 2, "")); return 0; }
static int UIWow_LuaFrameGetText(lua_State *L) { int i = UIWow_FrameFromSelf(L); lua_pushstring(L, i >= 0 ? wow_xml.elems[i].text : ""); return 1; }
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
        { "SetFocus", UIWow_LuaFrameSetFocus }, { "HighlightText", UIWow_LuaFrameHighlightText }, { "RegisterEvent", UIWow_LuaFrameRegisterEvent }, { "SetSequence", UIWow_LuaFrameSetSequence },
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
            xmlChar *point, *relative;
            if (a->type != XML_ELEMENT_NODE || xmlStrcasecmp(a->name, BAD_CAST "Anchor")) continue;
            point = xmlGetProp(a, BAD_CAST "point"); relative = xmlGetProp(a, BAD_CAST "relativePoint");
            snprintf(e->point, sizeof(e->point), "%s", point && *point ? (char const *)point : "CENTER");
            snprintf(e->relative_point, sizeof(e->relative_point), "%s", relative && *relative ? (char const *)relative : e->point);
            SAFE_DELETE(point, xmlFree); SAFE_DELETE(relative, xmlFree);
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

static void UIWow_XmlReadShared(uiWowXmlElem_t *e, xmlNodePtr node) {
    xmlChar *file = xmlGetProp(node, BAD_CAST "file"), *hidden = xmlGetProp(node, BAD_CAST "hidden"), *text = xmlGetProp(node, BAD_CAST "text");
    if (file && *file) snprintf(e->file, sizeof(e->file), "%s", (char const *)file);
    if (text && *text) snprintf(e->text, sizeof(e->text), "%s", (char const *)text);
    if (hidden && *hidden && !strcasecmp((char const *)hidden, "true")) e->hidden = true;
    SAFE_DELETE(file, xmlFree); SAFE_DELETE(hidden, xmlFree); SAFE_DELETE(text, xmlFree);
    UIWow_XmlReadSize(e, node); UIWow_XmlReadAnchor(e, node); UIWow_XmlReadBackdrop(e, node);
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
    uiWowXmlType_t type; xmlChar *name_attr, *parent_attr; int idx;
    if (!node || node->type != XML_ELEMENT_NODE || !node->name) return;
    if (!xmlStrcasecmp(node->name, BAD_CAST "Layer")) { UIWow_XmlParseLayer(node, parent); return; }
    if (!xmlStrcasecmp(node->name, BAD_CAST "Frames") || !xmlStrcasecmp(node->name, BAD_CAST "Layers")) { UIWow_XmlParseChildren(node, parent); return; }
    if (!xmlStrcasecmp(node->name, BAD_CAST "Frame")) type = WOW_XML_FRAME;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Model")) type = WOW_XML_MODEL;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Texture")) type = WOW_XML_TEXTURE;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "FontString")) type = WOW_XML_FONTSTRING;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "Button")) type = WOW_XML_BUTTON;
    else if (!xmlStrcasecmp(node->name, BAD_CAST "EditBox")) type = WOW_XML_EDITBOX;
    else return;
    name_attr = xmlGetProp(node, BAD_CAST "name"); parent_attr = xmlGetProp(node, BAD_CAST "parent");
    idx = UIWow_XmlPushElem(type, (char const *)name_attr, parent, draw_layer); SAFE_DELETE(name_attr, xmlFree);
    if (idx < 0) { SAFE_DELETE(parent_attr, xmlFree); UIWow_Printf("UIWow: XML element limit exceeded\n"); return; }
    if (parent_attr && *parent_attr) {
        uiWowXmlElem_t *e = &wow_xml.elems[idx]; int named_parent;
        snprintf(e->parent_name, sizeof(e->parent_name), "%s", (char const *)parent_attr);
        named_parent = UIWow_XmlFindByName(e->parent_name); if (named_parent >= 0) e->parent = named_parent;
    }
    SAFE_DELETE(parent_attr, xmlFree);
    if (type == WOW_XML_EDITBOX) wow_xml.elems[idx].focusable = 1;
    UIWow_XmlReadShared(&wow_xml.elems[idx], node); UIWow_XmlPublishFrame(idx); UIWow_XmlParseChildren(node, idx);
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

static BOOL UIWow_XMLProcessFile(LPCSTR path, int depth) {
    LPCSTR ext = strrchr(path ? path : "", '.');
    if (!path || !*path) return false;
    if (ext && !strcasecmp(ext, ".lua")) return UIWow_LoadLuaFile(path, false);
    return UIWow_XMLProcessXml(path, depth);
}

static BOOL UIWow_XMLLoadFromToc(LPCSTR toc_path) {
    void *buf = NULL; int size; char *text, *cur;
    if (!uiimport.FS_ReadFile || !uiimport.FS_FreeFile) { UIWow_WarnOnce(WOW_UI_WARN_NO_INPUT_FS, "UIWow: FS API unavailable for TOC load\n"); return false; }
    size = uiimport.FS_ReadFile(toc_path, &buf);
    if (size <= 0 || !buf) { SAFE_DELETE(buf, uiimport.FS_FreeFile); UIWow_Printf("UIWow: missing TOC %s\n", toc_path); return false; }
    text = (char *)buf; cur = text;
    while (*cur) {
        char line[PATH_MAX], resolved[PATH_MAX]; char *end = cur; int n = 0, len;
        while (*end && *end != '\n' && *end != '\r') end++;
        len = (int)(end - cur); if (len > 0 && len < (int)sizeof(line)) { memcpy(line, cur, (size_t)len); line[len] = '\0'; while (line[n] && isspace((unsigned char)line[n])) n++; if (line[n] && line[n] != '#') {
            if (UIWow_XmlResolvePath(toc_path, line + n, resolved, sizeof(resolved))) UIWow_XMLProcessFile(resolved, 0);
            else UIWow_Printf("UIWow: TOC entry path too long in %s: %s\n", toc_path, line + n);
        }}
        while (*end == '\n' || *end == '\r') end++; cur = end;
    }
    uiimport.FS_FreeFile(buf);
    return true;
}

void UIWow_XMLInitRuntime(void) { memset(&wow_xml, 0, sizeof(wow_xml)); wow_xml.focus = -1; UIWow_XMLInstallLuaCompat(); }
void UIWow_XMLShutdownRuntime(void) { memset(&wow_xml, 0, sizeof(wow_xml)); wow_xml.focus = -1; }

BOOL UIWow_XMLLoadGlueFromToc(LPCSTR toc_path) {
    if (!wow_ui.lua) { UIWow_Printf("UIWow: XML runtime requires active lua_State\n"); return false; }
    if (!wow_xml.lua_ready) UIWow_XMLInstallLuaCompat();
    memset(wow_xml.elems, 0, sizeof(wow_xml.elems)); wow_xml.count = 0; wow_xml.focus = -1;
    if (!UIWow_XMLLoadFromToc(toc_path)) return false;
    UIWow_Printf("UIWow: FrameXML loaded from %s (elements=%d)\n", toc_path, wow_xml.count);
    return wow_xml.count > 0;
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

void UIWow_XMLDraw(void) {
    UIWow_EnsureRenderer(); if (!wow_ui.renderer) return;
    for (int layer = WOW_XML_LAYER_BACKGROUND; layer <= WOW_XML_LAYER_OVERLAY; layer++) FOR_LOOP(i, wow_xml.count) {
        uiWowXmlElem_t const *e = &wow_xml.elems[i]; RECT r; RECT uv = MAKE(RECT, 0, 0, 1, 1);
        if (!e->used || e->hidden || e->draw_layer != layer) continue;
        r = UIWow_XmlComputeRect(i);
        if (e->type == WOW_XML_MODEL || e->type == WOW_XML_FRAME || e->type == WOW_XML_BUTTON || e->type == WOW_XML_EDITBOX) UIWow_XMLDrawBackdrop(e, &r);
        if (e->file[0] && (e->type == WOW_XML_TEXTURE || e->type == WOW_XML_MODEL || e->type == WOW_XML_BUTTON)) {
            LPTEXTURE t = UIWow_LoadTexture(e->file); if (t) wow_ui.renderer->DrawImage(t, &r, &uv, MAKE(COLOR32, 255, 255, 255, (BYTE)(255.0f * e->alpha)));
        }
        if (e->text[0] && (e->type == WOW_XML_FONTSTRING || e->type == WOW_XML_EDITBOX || e->type == WOW_XML_BUTTON)) {
            LPCFONT f = UIWow_LoadFont(e->type == WOW_XML_FONTSTRING ? 14 : 12);
            if (f) wow_ui.renderer->DrawText(&MAKE(drawText_t, .font = f, .text = e->text, .rect = r, .color = MAKE(COLOR32, 255, 255, 255, (BYTE)(255.0f * e->alpha)), .textWidth = r.w, .lineHeight = r.h, .wordWrap = false, .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYMIDDLE));
        }
    }
}
