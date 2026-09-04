/* ui_xml.c — WoW Glue FrameXML runtime: drawing, Lua bindings, mouse events, TOC loader.
 * Parsing lives in stb_wowxml.h. */
#ifndef STB_WOW_XML_IMPLEMENTATION
#define STB_WOW_XML_IMPLEMENTATION
#endif
#include "menu_local.h"
#include "menu_dbc.h"
#include "client/ui_text_input.h"

#include <ctype.h>
#include "common/tinyxml.h"
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

/* Types, helpers, and parser now live in stb_wowxml.h (compiled via -DSTB_WOW_XML_IMPLEMENTATION).
 * The Lua runtime below accesses wow_xml, ELEM_*, EF_*, etc. via the header. */

/* Forward declarations for Lua code below (both build modes). */
static void UIWow_XmlPublishFrame(int idx);
static void UIWow_XMLRunFrameScript(int idx, LPCSTR script, LPCSTR event_name);

/* Host services for stb_wowxml.h — only compiled in the unity production build
 * where -DSTB_WOW_XML_IMPLEMENTATION is set globally. */
int  UI_XmlFsReadFile(LPCSTR p, void **b) { return uiimport.FS_ReadFile ? uiimport.FS_ReadFile(p, b) : -1; }
void UI_XmlFsFreeFile(void *b) { if (uiimport.FS_FreeFile) uiimport.FS_FreeFile(b); }
void UI_XmlPrintf(LPCSTR fmt, ...) { va_list ap; if (!uiimport.Printf) return; va_start(ap, fmt); uiimport.Printf(fmt); va_end(ap); }
void UI_XmlOnFramePublish(int idx)  { UIWow_XmlPublishFrame(idx); }
void UI_XmlOnShow(int idx) {
    if (idx >= 0 && idx < wow_xml.count && UIWow_ElemStr(&wow_xml.elems[idx], ELEM_ON_SHOW))
        UIWow_XMLRunFrameScript(idx, wow_xml.elems[idx].texts[ELEM_ON_SHOW], "OnShow");
}
void UI_XmlOnScriptBody(LPCSTR path, LPCSTR body) {
    char chunk[512];
    snprintf(chunk, sizeof(chunk), "%s:<Script>", path ? path : "");
    UIWow_RunLuaString(chunk, body);
}
void UI_XmlLoadScriptFile(LPCSTR path) { UIWow_LoadLuaFile(path, false); }


static int UIWow_FrameFromSelf(lua_State *L) {
    int idx;
    luaL_checktype(L, 1, LUA_TTABLE); lua_getfield(L, 1, "__ow3_index"); idx = (int)luaL_optinteger(L, -1, -1); lua_pop(L, 1);
    return idx >= 0 && idx < wow_xml.count && (wow_xml.elems[idx].flags & EF_USED) ? idx : -1;
}

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

/* Frame:SetPoint replaces the primary anchor, including runtime anchors authored by native Lua. */
static int UIWow_LuaFrameSetPoint(lua_State *L) {
    int i = UIWow_FrameFromSelf(L), ri = -1;
    WOWXMLPOINT in = { luaL_checkstring(L, 2), NULL, NULL, 0, 0 };
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "__ow3_index"); ri = (int)luaL_optinteger(L, -1, -1); lua_pop(L, 1);
        if (ri >= 0 && ri < wow_xml.count) in.rel = UIWow_ElemStr(&wow_xml.elems[ri], ELEM_NAME);
    } else if (!lua_isnoneornil(L, 3)) in.rel = luaL_checkstring(L, 3);
    in.rel_point = luaL_optstring(L, 4, in.point);
    in.x = (FLOAT)luaL_optnumber(L, 5, 0.0); in.y = (FLOAT)luaL_optnumber(L, 6, 0.0);
    if (i >= 0) UIWow_XMLSetFramePoint(UIWow_ElemStr(&wow_xml.elems[i], ELEM_NAME), &in);
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
        { "SetPoint", UIWow_LuaFrameSetPoint }, { "ClearAllPoints", UIWow_LuaFrameNoop },
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


/* CheckButton checked state selects the native CheckedTexture parsed from its Blizzard template. */
BOOL UIWow_XMLSetButtonChecked(LPCSTR name, BOOL checked) {
    int idx = UIWow_XmlFindByName(name);
    if (idx < 0 || wow_xml.elems[idx].type != WOW_XML_BUTTON || !(wow_xml.elems[idx].flags & EF_CHECKBUTTON)) return false;
    if (checked) wow_xml.elems[idx].flags |= EF_CHECKED;
    else wow_xml.elems[idx].flags &= ~EF_CHECKED;
    return true;
}

/* Runtime FrameXML scripts use the same native point and positive-Y-up coordinate contract as XML anchors. */
BOOL UIWow_XMLSetFramePoint(LPCSTR name, LPCWOWXMLPOINT in) {
    int idx = UIWow_XmlFindByName(name); uiWowXmlElem_t *e;
    if (idx < 0 || !in || !in->point || !in->point[0]) return false;
    e = &wow_xml.elems[idx];
    UIWow_ElemSetStr(e, ELEM_POINT, in->point);
    UIWow_ElemSetStr(e, ELEM_RELATIVE_POINT, in->rel_point && in->rel_point[0] ? in->rel_point : in->point);
    UIWow_ElemSetStr(e, ELEM_RELATIVE_NAME, in->rel);
    e->relative_to = in->rel && in->rel[0] ? UIWow_XmlFindByName(in->rel) : -1;
    e->offset = MAKE(fpoint_t, UIWow_XmlX(in->x), -UIWow_XmlY(in->y)); e->flags |= EF_HAS_ANCHOR;
    return true;
}

/* Reproduce FrameXML's GetHeight + SetHeight sizing before the parent backdrop is drawn. */
BOOL UIWow_XMLSizeFrameToText(LPCSTR frame, LPCSTR text, FLOAT padding) {
    int fi = UIWow_XmlFindByName(frame), ti = UIWow_XmlFindByName(text); RECT r; LPCFONT font; VECTOR2 sz;
    if (fi < 0 || ti < 0 || wow_xml.elems[ti].type != WOW_XML_FONTSTRING || !wow_ui.renderer || !wow_ui.renderer->GetTextSize)
        return false;
    r = UIWow_XmlComputeRect(ti); font = UIWow_LoadFont((DWORD)wow_xml.elems[ti].font_size);
    if (!font || r.w <= 0.0f) return false;
    sz = wow_ui.renderer->GetTextSize(&MAKE(drawText_t, .font = font, .text = UIWow_ElemStr(&wow_xml.elems[ti], ELEM_TEXT),
        .rect = r, .textWidth = r.w, .lineHeight = 1.33f, .flags = DRAW_WORD_WRAP));
    wow_xml.elems[ti].measured.h = sz.y;
    /* TutorialFrame.lua uses TutorialFrameText:GetHeight() + 62; retaining that formula fixes the old fixed-height overflow. */
    wow_xml.elems[fi].size.h = sz.y + UIWow_XmlY(padding); wow_xml.elems[fi].flags |= EF_HAS_SIZE;
    return true;
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
        snprintf(chunk, sizeof(chunk), "=%s:%s (%s)", name && name[0] ? name : "<anon>", event_name ? event_name : "Script", src_file);
    else
        snprintf(chunk, sizeof(chunk), "=%s:%s", name && name[0] ? name : "<anon>", event_name ? event_name : "Script");
    if (luaL_loadbuffer(wow_ui.lua, script, strlen(script), chunk) != LUA_OK) {
        UIWow_Printf("UIWow Lua load: %s\n", lua_tostring(wow_ui.lua, -1)); lua_pop(wow_ui.lua, 1);
    } else {
        UIWow_LuaPCall(0);
    }
    lua_pushnil(wow_ui.lua); lua_setglobal(wow_ui.lua, "this");
}

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

static void UIWow_XMLDrawImage(LPTEXTURE tex, LPCRECT screen, LPCRECT uv, COLOR32 color, BLEND_MODE mode) {
    if (!wow_ui.renderer || !tex) return;
    if (wow_ui.renderer->DrawImageEx) {
        wow_ui.renderer->DrawImageEx(&MAKE(drawImage_t, .texture = tex, .shader = SHADER_UI, .alphamode = mode, .screen = *screen, .uv = *uv, .color = color, .flags = s_has_scroll_clip ? DRAW_CLIP : 0, .clip = s_scroll_clip));
    } else if (wow_ui.renderer->DrawImage) {
        wow_ui.renderer->DrawImage(tex, screen, uv, color);
    }
}

static void UIWow_XMLDrawBackdrop(uiWowXmlElem_t const *e, LPCRECT r) {
    LPCSTR bg_path = e->texts[ELEM_BACKDROP_BG];
    LPCSTR edge_path = e->texts[ELEM_BACKDROP_EDGE];
    LPCTEXTURE bg_tex = NULL;
    LPCTEXTURE edge_tex = NULL;
    drawBackdrop_t db;

    if (!wow_ui.renderer || !wow_ui.renderer->DrawBackdrop) return;

    if (bg_path && bg_path[0]) {
        bg_tex = UIWow_LoadTexture(bg_path);
    }
    if (edge_path && edge_path[0] && e->edge.w > 0.0f && e->edge.h > 0.0f) {
        edge_tex = UIWow_LoadTexture(edge_path);
    }
    if (!bg_tex && !edge_tex) {
        return;
    }

    memset(&db, 0, sizeof(db));
    db.screen = *r;
    db.bg.texture = bg_tex;
    db.bg.color = e->colors[ELEM_COLOR_BACKDROP];
    db.edge.texture = edge_tex;
    db.edge.color = e->colors[ELEM_COLOR_BACKDROP_BORDER];
    db.corner.flags = edge_tex ? 0x1ff : 0; /* all 9 bits if edge present */
    db.corner.size = (e->edge.w + e->edge.h) * 0.5f;
    /* uiBackdrop_t inset order: right=0, top=1, bottom=2, left=3 */
    db.insets.right = e->backdrop_insets[WOW_XML_BACKDROP_RIGHT];
    db.insets.top = e->backdrop_insets[WOW_XML_BACKDROP_TOP];
    db.insets.bottom = e->backdrop_insets[WOW_XML_BACKDROP_BOTTOM];
    db.insets.left = e->backdrop_insets[WOW_XML_BACKDROP_LEFT];
    if (e->flags & EF_BACKDROP_TILE) db.flags |= DRAW_TILE;

    wow_ui.renderer->DrawBackdrop(&db);
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
            UIWow_WarnOnce(WOW_UI_WARN_NO_CHAR_MODEL, "UIWow: failed to load character model %s\n", path);
    }
    return wow_ui.char_customize_model;
}

/* Report unresolved authored geometry once without fabricating a drawable or clickable rectangle. */
static void UIWow_XMLWarnGeometry(uiWowXmlElem_t *e, LPCRECT r) {
    if ((r->w > 0.0f && r->h > 0.0f) || e->flags & EF_LOGGED_GEOMETRY) return;
    uiimport.Printf("UIWow: unresolved FrameXML geometry frame=%s source=%s width=%g height=%g\n",
        UIWow_ElemStr(e, ELEM_NAME) ? e->texts[ELEM_NAME] : "<unnamed>",
        UIWow_ElemStr(e, ELEM_SOURCE_FILE) ? e->texts[ELEM_SOURCE_FILE] : "<buffer>", r->w, r->h);
    e->flags |= EF_LOGGED_GEOMETRY;
}

/* Draw one XML frame's own layer. whoa draws a frame's batches before recursing into child frames. */
static void UIWow_XMLDrawElementLayer(int i, int layer, int hovered_button) {
        uiWowXmlElem_t *e = &wow_xml.elems[i]; RECT r; RECT uv = MAKE(RECT, 0, 0, 1, 1); char text[512];
        COLOR32 text_color = e->colors[ELEM_COLOR_TEXT];
        BOOL pressed = e->type == WOW_XML_BUTTON && wow_xml.pressed_button == i;
        BOOL hovered = e->type == WOW_XML_BUTTON && hovered_button == i;
        int draw_layer = e->type == WOW_XML_MODEL ? WOW_XML_LAYER_BACKGROUND : e->draw_layer;
        LPCSTR file = e->texts[ELEM_FILE], normal_file = e->texts[ELEM_NORMAL_FILE], pushed_file = e->texts[ELEM_PUSHED_FILE];
        LPCSTR highlight_file = e->texts[ELEM_HIGHLIGHT_FILE], checked_file = e->texts[ELEM_CHECKED_FILE], elem_text = e->texts[ELEM_TEXT];
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
        if (e->type != WOW_XML_FONTSTRING) UIWow_XMLWarnGeometry(e, &r);
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
                UIWow_WarnOnce(WOW_UI_WARN_NO_MODEL_LOADER, "UIWow: renderer has no model loader; XML model frames skipped\n");
            else if (!wow_ui.renderer->RenderFrame)
                UIWow_WarnOnce(WOW_UI_WARN_NO_MODEL_LOADER, "UIWow: renderer has no frame renderer; XML model frames skipped\n");
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
                UIWow_XMLDrawImage(t, &r, &uv, MAKE(COLOR32, e->colors[ELEM_COLOR_VERTEX].r, e->colors[ELEM_COLOR_VERTEX].g, e->colors[ELEM_COLOR_VERTEX].b, (BYTE)(e->colors[ELEM_COLOR_VERTEX].a * e->alpha)), BLEND_MODE_BLEND);
            }
            if (e->type == WOW_XML_BUTTON && e->flags & EF_CHECKED && checked_file) {
                LPTEXTURE ct = UIWow_LoadTexture(checked_file);
                if (ct) UIWow_XMLDrawImage(ct, &r, &MAKE(RECT,0,0,1,1), COLOR32_WHITE, BLEND_MODE_BLEND);
            }
            if (e->type == WOW_XML_BUTTON && hovered && highlight_file && highlight_file[0]) {
                LPTEXTURE ht = UIWow_LoadTexture(highlight_file);
                RECT huv = MAKE(RECT, 0, 0, 1, 1);
                if (e->flags & EF_HAS_HIGHLIGHT_TEXCOORD) huv = e->highlight_texcoord;
                if (ht) UIWow_XMLDrawImage(ht, &r, &huv, COLOR32_WHITE, BLEND_MODE_ADD);
            }
        }
        if (((elem_text && elem_text[0]) || (e->type == WOW_XML_EDITBOX && wow_xml.focus == i)) &&
            (e->type == WOW_XML_FONTSTRING || e->type == WOW_XML_EDITBOX || e->type == WOW_XML_BUTTON)) {
            LPCFONT f = UIWow_LoadFont((DWORD)e->font_size);
            /* FrameXML may leave either FontString axis to its renderer-measured natural size. */
            if (f && e->type == WOW_XML_FONTSTRING && (e->size.w == 0 || e->size.h == 0) && wow_ui.renderer->GetTextSize) {
                LPCSTR display = UIWow_XMLDisplayText(e, text, sizeof(text));
                /* When width is unconstrained, measure at full virtual width to get the natural line width.
                   Passing r.w=0 would wrap every character at column 0 and freeze measured.w near zero. */
                FLOAT measure_w = e->size.w > 0 ? r.w : 1.0f;
                VECTOR2 sz = wow_ui.renderer->GetTextSize(&MAKE(drawText_t, .font = f, .text = display, .rect = r, .textWidth = measure_w, .lineHeight = 1.33f, .flags = (e->flags & EF_WORD_WRAP) ? DRAW_WORD_WRAP : 0));
                if (e->size.w == 0) e->measured.w = sz.x;
                if (e->size.h == 0) e->measured.h = sz.y;
                r = UIWow_XmlComputeRect(i);
                r.y -= scroll_off_y;
            }
            UIWow_XMLWarnGeometry(e, &r);
            RECT tr = MAKE(RECT, r.x + e->text_inset.w + e->text_off.x, r.y + e->text_off.y, r.w - e->text_inset.w, r.h - e->text_inset.h);
            LPCSTR display = UIWow_XMLDisplayText(e, text, sizeof(text));
            if (f) {
                drawText_t dt = MAKE(drawText_t, .font = f, .text = display, .rect = tr, .color = MAKE(COLOR32, text_color.r, text_color.g, text_color.b, (BYTE)(text_color.a * e->alpha)), .textWidth = tr.w, .lineHeight = 1.33f, .flags = ((e->flags & EF_WORD_WRAP) ? DRAW_WORD_WRAP : 0) | (has_clip ? DRAW_CLIP : 0), .halign = e->type == WOW_XML_EDITBOX ? FONT_JUSTIFYLEFT : e->halign, .valign = e->valign, .clip = clip_rect);
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

BOOL UIWow_XMLDrawFrame(LPCSTR name) {
    int idx = UIWow_XmlFindByName(name);
    if (idx < 0) return false;
    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) return false;
    UIWow_XMLComputeScrollRanges();
    UIWow_XMLDrawTree(idx, wow_xml.hovered_button);
    return true;
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
            UIWow_Printf("UIWow: OnClick dispatch idx=%d name='%s' checkbtn=%d checked=%d\n", pressed, wow_xml.elems[pressed].texts[ELEM_NAME] ? wow_xml.elems[pressed].texts[ELEM_NAME] : "?", !!(wow_xml.elems[pressed].flags & EF_CHECKBUTTON), !!(wow_xml.elems[pressed].flags & EF_CHECKED));
            if (wow_xml.elems[pressed].flags & EF_CHECKBUTTON)
                wow_xml.elems[pressed].flags ^= EF_CHECKED;
            UIWow_XMLRunFrameScript(pressed, wow_xml.elems[pressed].texts[ELEM_ON_CLICK], "OnClick");
            return true;
        }
        if (param == 1 && pressed >= 0 && hit == pressed) {
            UIWow_Printf("UIWow: OnClick MISS idx=%d name='%s' type=%d enabled=%d has_onclick=%d\n", pressed, wow_xml.elems[pressed].texts[ELEM_NAME] ? wow_xml.elems[pressed].texts[ELEM_NAME] : "?", wow_xml.elems[pressed].type, !!(wow_xml.elems[pressed].flags & EF_ENABLED), !!UIWow_ElemStr(&wow_xml.elems[pressed], ELEM_ON_CLICK));
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
    e->measured = MAKE(fsize_t, 0, 0);
}

static void UIWow_XMLEditBackspace(uiWowXmlElem_t *e) {
    if (UI_TextInput_Backspace(&wow_xml.text_input))
        e->measured = MAKE(fsize_t, 0, 0);
}

static void UIWow_XMLEditDelete(uiWowXmlElem_t *e) {
    if (UI_TextInput_Delete(&wow_xml.text_input))
        e->measured = MAKE(fsize_t, 0, 0);
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
            e->measured = MAKE(fsize_t, 0, 0);
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
