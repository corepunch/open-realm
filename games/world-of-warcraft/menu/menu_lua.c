/*
 * menu_lua.c — Lua VM initialisation, C→Lua bindings, and script execution.
 *
 * Owns the lua_State lifecycle and every ow3.* function exposed to Lua.
 * The public surface is small: UIWow_InitLua / UIWow_ShutdownLua boot the
 * VM, UIWow_LuaPCall runs a function already on the stack, and the three
 * Call* helpers drive the per-frame callbacks.
 */
#include "menu_local.h"




#include <strings.h>

#ifndef LUA_OK
#define LUA_OK 0
#endif
#ifndef LUA_GNAME
#define LUA_GNAME "_G"
#endif

/* -------------------------------------------------------------------------
 * Lua argument helpers
 * ---------------------------------------------------------------------- */

static uint8_t UIWow_LuaColorByte(lua_State *L, int index, uint8_t fallback) {
    lua_Number value;

    if (!lua_isnumber(L, index)) {
        return fallback;
    }
    value = lua_tonumber(L, index);
    if (value <= 1.0) {
        value *= 255.0;
    }
    if (value < 0.0) {
        value = 0.0;
    } else if (value > 255.0) {
        value = 255.0;
    }
    return (uint8_t)(value + 0.5);
}

static COLOR32 UIWow_LuaColor(lua_State *L, int first, COLOR32 fallback) {
    return MAKE(COLOR32, UIWow_LuaColorByte(L, first,     fallback.r), UIWow_LuaColorByte(L, first + 1, fallback.g), UIWow_LuaColorByte(L, first + 2, fallback.b), UIWow_LuaColorByte(L, first + 3, fallback.a));
}

static rect_t UIWow_LuaRect(lua_State *L, int first) {
    return MAKE(rect_t, (float)luaL_checknumber(L, first), (float)luaL_checknumber(L, first + 1), (float)luaL_checknumber(L, first + 2), (float)luaL_checknumber(L, first + 3));
}

/* -------------------------------------------------------------------------
 * ow3.draw_* bindings
 * ---------------------------------------------------------------------- */

static int UIWow_LuaDrawImage(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    rect_t screen = UIWow_LuaRect(L, 2);
    rect_t uv = MAKE(rect_t, 0, 0, 1, 1);
    COLOR32 color = UIWow_LuaColor(L, 6, COLOR32_WHITE);
    LPTEXTURE texture = UIWow_LoadTexture(name);

    if (wow_ui.renderer && texture) {
        wow_ui.renderer->DrawImage(texture, &screen, &uv, color);
    }
    lua_pushboolean(L, texture != NULL);
    return 1;
}

static int UIWow_LuaDrawImageUV(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    rect_t screen = UIWow_LuaRect(L, 2);
    float left   = (float)luaL_checknumber(L, 6);
    float right  = (float)luaL_checknumber(L, 7);
    float top    = (float)luaL_checknumber(L, 8);
    float bottom = (float)luaL_checknumber(L, 9);
    rect_t uv = MAKE(rect_t, left, top, right - left, bottom - top);
    COLOR32 color = UIWow_LuaColor(L, 10, COLOR32_WHITE);
    LPTEXTURE texture = UIWow_LoadTexture(name);

    if (wow_ui.renderer && texture) {
        wow_ui.renderer->DrawImage(texture, &screen, &uv, color);
    }
    lua_pushboolean(L, texture != NULL);
    return 1;
}

static int UIWow_LuaDrawColor(lua_State *L) {
    rect_t screen = UIWow_LuaRect(L, 1);
    COLOR32 color = UIWow_LuaColor(L, 5, COLOR32_WHITE);

    UIWow_EnsureRenderer();
    if (wow_ui.renderer && wow_ui.renderer->DrawFill) {
        wow_ui.renderer->DrawFill(&screen, color);
    }
    return 0;
}

/* draw_backdrop(bg_path, border_path, x, y, w, h, edge_size)
 * Draws a WoW-style 9-slice backdrop.
 *   bg_path     — background texture tiled/stretched inside the inset region
 *   border_path — border texture rendered as 9 pieces: 4 corners (edge×edge),
 *                 4 edges stretched between them, matching WoW SetBackdrop
 *   edge_size   — corner/edge size in 0-1 screen space (e.g. n(32) for a
 *                 256px texture with EdgeSize=32)
 * The border texture is assumed to be laid out with corners in the four
 * quadrants and edges along the four sides, matching the WoW atlas format.
 * Either path may be nil/"" to skip that layer. */
static int UIWow_LuaDrawBackdrop(lua_State *L) {
    cstring_t bg_path     = luaL_optstring(L, 1, "");
    cstring_t border_path = luaL_optstring(L, 2, "");
    rect_t sc            = UIWow_LuaRect(L, 3);
    float e            = (float)luaL_optnumber(L, 7, 0.0);
    rect_t fuv           = MAKE(rect_t, 0, 0, 1, 1);

    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return 0;
    }

    /* Background — stretched inside the inset */
    if (bg_path && *bg_path) {
        LPTEXTURE bg = UIWow_LoadTexture(bg_path);
        if (bg) {
            rect_t inner = MAKE(rect_t, sc.x + e, sc.y + e, sc.w - e * 2.0f, sc.h - e * 2.0f);
            wow_ui.renderer->DrawImage(bg, &inner, &fuv, COLOR32_WHITE);
        }
    }

    /* Border — 9-slice: UVs divide the texture into a 3×3 grid where
     * each corner occupies 1/4 of the texture (the WoW atlas is 2×2 tiles
     * each holding one corner/edge piece at half-texture size).
     * UV layout: corners at the four quadrants, edges along each side. */
    if (border_path && *border_path && e > 0.0f) {
        LPTEXTURE border = UIWow_LoadTexture(border_path);
        if (border) {
            float r = sc.x + sc.w; /* right edge */
            float b = sc.y + sc.h; /* bottom edge */
            /* UVs: WoW border textures place TL corner in top-left quadrant,
             * TR in top-right, BL in bottom-left, BR in bottom-right.
             * Each quadrant = 0.5 × 0.5 of the texture. */
            rect_t uv_tl = MAKE(rect_t, 0.0f, 0.0f, 0.5f, 0.5f);
            rect_t uv_tr = MAKE(rect_t, 0.5f, 0.0f, 0.5f, 0.5f);
            rect_t uv_bl = MAKE(rect_t, 0.0f, 0.5f, 0.5f, 0.5f);
            rect_t uv_br = MAKE(rect_t, 0.5f, 0.5f, 0.5f, 0.5f);
            /* corners */
            rect_t tl = MAKE(rect_t, sc.x,     sc.y,     e, e);
            rect_t tr = MAKE(rect_t, r - e,    sc.y,     e, e);
            rect_t bl = MAKE(rect_t, sc.x,     b - e,    e, e);
            rect_t br = MAKE(rect_t, r - e,    b - e,    e, e);
            /* edges */
            rect_t top_e = MAKE(rect_t, sc.x + e, sc.y,   sc.w - e*2, e);
            rect_t bot_e = MAKE(rect_t, sc.x + e, b - e,  sc.w - e*2, e);
            rect_t lft_e = MAKE(rect_t, sc.x,     sc.y+e, e, sc.h - e*2);
            rect_t rgt_e = MAKE(rect_t, r - e,    sc.y+e, e, sc.h - e*2);
            /* top/bottom edges use top strip UV (y=0..0.5, full x) */
            rect_t uv_top = MAKE(rect_t, 0.0f, 0.0f, 1.0f, 0.5f);
            rect_t uv_bot = MAKE(rect_t, 0.0f, 0.5f, 1.0f, 0.5f);
            /* left/right edges use left strip UV (x=0..0.5, full y) */
            rect_t uv_lft = MAKE(rect_t, 0.0f, 0.0f, 0.5f, 1.0f);
            rect_t uv_rgt = MAKE(rect_t, 0.5f, 0.0f, 0.5f, 1.0f);

            wow_ui.renderer->DrawImage(border, &tl,    &uv_tl,  COLOR32_WHITE);
            wow_ui.renderer->DrawImage(border, &tr,    &uv_tr,  COLOR32_WHITE);
            wow_ui.renderer->DrawImage(border, &bl,    &uv_bl,  COLOR32_WHITE);
            wow_ui.renderer->DrawImage(border, &br,    &uv_br,  COLOR32_WHITE);
            wow_ui.renderer->DrawImage(border, &top_e, &uv_top, COLOR32_WHITE);
            wow_ui.renderer->DrawImage(border, &bot_e, &uv_bot, COLOR32_WHITE);
            wow_ui.renderer->DrawImage(border, &lft_e, &uv_lft, COLOR32_WHITE);
            wow_ui.renderer->DrawImage(border, &rgt_e, &uv_rgt, COLOR32_WHITE);
        }
    }
    return 0;
}



static int UIWow_LuaDrawText(lua_State *L) {
    cstring_t text = luaL_checkstring(L, 1);
    rect_t screen = UIWow_LuaRect(L, 2);
    uint32_t size = (uint32_t)luaL_optinteger(L, 6, 14);
    COLOR32 color = UIWow_LuaColor(L, 7, COLOR32_WHITE);
    cstring_t align = luaL_optstring(L, 11, "left");
    LPCFONT font = UIWow_LoadFont(size);
    uiFontJustificationH_t halign = FONT_JUSTIFYLEFT;

    if (!strcasecmp(align, "center")) {
        halign = FONT_JUSTIFYCENTER;
    } else if (!strcasecmp(align, "right")) {
        halign = FONT_JUSTIFYRIGHT;
    }

    if (wow_ui.renderer && font) {
        wow_ui.renderer->DrawText(&MAKE(drawText_t, .font = font, .text = text, .rect = screen, .color = color, .textWidth = screen.w, .lineHeight = screen.h, .halign = halign, .valign = FONT_JUSTIFYMIDDLE));
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Glue utility bindings
 * ---------------------------------------------------------------------- */

static int UIWow_LuaTime(lua_State *L) {
    lua_pushinteger(L, wow_ui.time);
    return 1;
}

static int UIWow_LuaCommand(lua_State *L) {
    cstring_t text = luaL_checkstring(L, 1);

    if (mi.ServerCommand && text && *text) {
        mi.ServerCommand(text);
    }
    return 0;
}

/* draw_loading_background() — draws the current map background texture fullscreen */
static int UIWow_LuaDrawLoadingBackground(lua_State *L) {
    rect_t full = MAKE(rect_t, 0, 0, 1, 1);

    (void)L;
    UIWow_EnsureRenderer();
    if (wow_ui.renderer && wow_ui.textures[WOW_UI_TEX_BACKGROUND]) {
        wow_ui.renderer->DrawImage(wow_ui.textures[WOW_UI_TEX_BACKGROUND], &full, &full, COLOR32_WHITE);
    }
    return 0;
}

/* draw_image_additive(path, x, y, w, h) — draw texture with additive blending (e.g. glow) */
static int UIWow_LuaDrawImageAdditive(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    rect_t screen = UIWow_LuaRect(L, 2);
    rect_t uv = MAKE(rect_t, 0, 0, 1, 1);
    COLOR32 color = UIWow_LuaColor(L, 6, COLOR32_WHITE);
    LPTEXTURE texture = UIWow_LoadTexture(name);

    if (wow_ui.renderer && texture) {
        wow_ui.renderer->DrawImageEx(&MAKE(drawImage_t, .texture   = texture, .screen    = screen, .uv        = uv, .color     = color, .shader    = SHADER_UI, .alphamode = BLEND_MODE_ADD));
    }
    lua_pushboolean(L, texture != NULL);
    return 1;
}

static int UIWow_LuaGetLoadingProgress(lua_State *L) {
    lua_pushnumber(L, 1.0);
    return 1;
}

static int UIWow_LuaGetLoadingTitle(lua_State *L) {
    cstring_t info = mi.GetConfigString(WOW_CS_MAPINFO);
    lua_pushstring(L, Wow_InfoValueForKey(info, "title", ""));
    return 1;
}

static int UIWow_LuaGetLoadingStatus(lua_State *L) {
    lua_pushstring(L, "");
    return 1;
}

static void Wow_ResolveMapPath(cstring_t name, string_t out, uint32_t out_size) {
    if (!name || !*name) {
        snprintf(out, out_size, "World/Maps/Azeroth/Azeroth.wdt");
    } else {
        cstring_t dot = strrchr(name, '.');
        if (dot && !strcasecmp(dot, ".wdt"))
            snprintf(out, out_size, "%s", name);
        else
            snprintf(out, out_size, "World/Maps/%s/%s.wdt", name, name);
    }
}

static int UIWow_LuaLoadMap(lua_State *L) {
    cstring_t map_name = luaL_optstring(L, 1, "Azeroth");
    PATHSTR resolved;
    char cmd[512];

    if (!map_name || !*map_name) {
        map_name = "Azeroth";
    }
    Wow_ResolveMapPath(map_name, resolved, sizeof(resolved));
    snprintf(cmd, sizeof(cmd), "map %s", resolved);
    if (mi.Cmd_ExecuteText) {
        mi.Cmd_ExecuteText(cmd);
    }
    return 0;
}

static int UIWow_LuaDefaultServerLogin(lua_State *L) {
    (void)L;
    lua_getglobal(wow_ui.lua, "SetGlueScreen");
    if (lua_isfunction(wow_ui.lua, -1)) {
        lua_pushstring(wow_ui.lua, "charselect");
        UIWow_LuaPCall(1);
    } else {
        lua_pop(wow_ui.lua, 1);
        if (mi.Cmd_ExecuteText) mi.Cmd_ExecuteText("menu_character_select\n");
    }
    return 0;
}

static int UIWow_LuaNoop(lua_State *L) { (void)L; return 0; }

static int UIWow_LuaPlaySound(lua_State *L) {
    if (lua_isnumber(L, 1) && mi.PlaySound) {
        mi.PlaySound((uint32_t)lua_tointeger(L, 1));
    } else if (lua_isstring(L, 1) && mi.PlaySoundByName) {
        mi.PlaySoundByName(lua_tostring(L, 1));
    }
    return 0;
}

#include "menu_dbc.h"

static int UIWow_LuaGetCharacterListUpdate(lua_State *L) {
    /* No server — call UpdateCharacterList directly so the UI reflects local state. */
    lua_getglobal(L, "UpdateCharacterList");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 0; }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        fprintf(stderr, "UIWow GetCharacterListUpdate: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return 0;
}
static int UIWow_LuaTrue(lua_State *L) { lua_pushboolean(L, 1); return 1; }
static int UIWow_LuaFalse(lua_State *L) { lua_pushboolean(L, 0); return 1; }
static int UIWow_LuaNil(lua_State *L) { lua_pushnil(L); return 1; }
static int UIWow_LuaZero(lua_State *L) { lua_pushinteger(L, 0); return 1; }

static int UIWow_LuaRealmCategories(lua_State *L) {
    lua_pushstring(L, "Test");
    return 1;
}

static int UIWow_LuaRealmInfo(lua_State *L) {
    lua_pushnil(L); lua_pushinteger(L, 0); lua_pushboolean(L, 0); lua_pushboolean(L, 0);
    lua_pushboolean(L, 0); lua_pushboolean(L, 0); lua_pushboolean(L, 0); lua_pushinteger(L, 0);
    return 8;
}

/* GetCharacterInfo and GetNumCharacters are implemented in menu_dbc.c */

static int UIWow_LuaTextCompat(lua_State *L) {
    if (lua_isnil(L, 1)) lua_pushstring(L, "");
    else lua_pushvalue(L, 1);
    return 1;
}

static int UIWow_LuaGetBuildInfo(lua_State *L) {
    lua_pushstring(L, "OpenWoW");
    lua_pushstring(L, "Debug");
    lua_pushstring(L, "0.0.0");
    lua_pushinteger(L, 1);
    lua_pushstring(L, "Jun 13 2026");
    return 5;
}

static int UIWow_LuaSetCharSelectModelFrame(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    int idx = UIWow_XmlFindByNamePub(name);
    if (idx >= 0) {
        wow_ui.model_frame_idx = idx;
        wow_ui.char_select_frame_idx = idx;
    }
    return 0;
}

static int UIWow_LuaSetCharSelectBackground(lua_State *L) {
    cstring_t model_path = luaL_checkstring(L, 1);
    int idx = wow_ui.model_frame_idx;
    if (idx >= 0) {
        UIWow_XmlSetFrameModel(idx, model_path);
    }
    return 0;
}

static int UIWow_LuaSelectCharacter(lua_State *L) {
    wow_ui.selected_char_idx = (int)luaL_checkinteger(L, 1) - 1; /* Lua is 1-based */
    return 0;
}

static int UIWow_LuaEnterWorld(lua_State *L) {
    (void)L;
    UIWow_SetSelectedCharCvars();
    /* The server playercreateinfo table owns race/class -> map; Map.dbc then resolves its client directory. */
    if (mi.Cmd_ExecuteText)
        mi.Cmd_ExecuteText("map playercreate");
    return 0;
}

static int UIWow_LuaSetCharCustomizeFrame(lua_State *L) {
    cstring_t name = luaL_checkstring(L, 1);
    int idx = UIWow_XmlFindByNamePub(name);
    if (idx >= 0) {
        wow_ui.model_frame_idx = idx;
        wow_ui.char_customize_frame_idx = idx;
    }
    return 0;
}

static int UIWow_LuaSetCharCustomizeBackground(lua_State *L) {
    cstring_t model_path = luaL_checkstring(L, 1);
    int idx = wow_ui.model_frame_idx;
    if (idx >= 0) {
        UIWow_XmlSetFrameModel(idx, model_path);
    }
    return 0;
}

static int UIWow_LuaUpdateCustomizationScene(lua_State *L) {
    (void)L;
    return 0;
}

static int UIWow_LuaSetSelectedRaceChanged(lua_State *L) {
    if (UIWow_SetSelectedRace((int)luaL_checknumber(L, 1)))
        UIWow_XMLInvalidateCharCustomizeModel();
    return 0;
}

static int UIWow_LuaSetSelectedSexChanged(lua_State *L) {
    if (UIWow_SetSelectedSex((int)luaL_checknumber(L, 1)))
        UIWow_XMLInvalidateCharCustomizeModel();
    return 0;
}

static int UIWow_LuaSetSelectedClassChanged(lua_State *L) {
    UIWow_SetSelectedClass((int)luaL_checknumber(L, 1));
    return 0;
}

static luaL_Reg const wow_lua_funcs[] = {
    { "draw_loading_background", UIWow_LuaDrawLoadingBackground },
    { "draw_image",          UIWow_LuaDrawImage },
    { "draw_image_uv",       UIWow_LuaDrawImageUV },
    { "draw_image_additive", UIWow_LuaDrawImageAdditive },
    { "draw_color",          UIWow_LuaDrawColor },
    { "draw_backdrop",       UIWow_LuaDrawBackdrop },
    { "draw_text",           UIWow_LuaDrawText },
    { "get_loading_progress",UIWow_LuaGetLoadingProgress },
    { "get_loading_title",   UIWow_LuaGetLoadingTitle },
    { "get_loading_status",  UIWow_LuaGetLoadingStatus },
    { "time",             UIWow_LuaTime },
    { "command",          UIWow_LuaCommand },
    { "load_map",         UIWow_LuaLoadMap },
    { NULL, NULL },
};

static luaL_Reg const wow_global_funcs[] = {
    { "DefaultServerLogin", UIWow_LuaDefaultServerLogin },
    { "TEXT",              UIWow_LuaTextCompat },
    { "GetBuildInfo",      UIWow_LuaGetBuildInfo },
    { "TOSAccepted",       UIWow_LuaTrue },
    { "ShowTOSNotice",     UIWow_LuaFalse },
    { "GetLastAccountName",UIWow_LuaNil },
    { "GetServerName",     UIWow_LuaNil },
    { "GetDataInterface",  UIWow_LuaZero },
    { "GetCodeInterface",  UIWow_LuaZero },
    { "AcceptTOS",         UIWow_LuaNoop },
    { "GlueDialog_Show",   UIWow_LuaNoop },
    { "LaunchURL",         UIWow_LuaNoop },
    { "PlaySound",         UIWow_LuaPlaySound },
    { "PlayGlueMusic",     UIWow_LuaNoop },
    { "StopGlueMusic",     UIWow_LuaNoop },
    { "PlayCreditsMusic",  UIWow_LuaNoop },
    { "DisconnectFromServer", UIWow_LuaNoop },
    { "IsConnectedToServer", UIWow_LuaTrue },
    { "GetBillingTimeRemaining", UIWow_LuaZero },
    { "EnterWorld",        UIWow_LuaEnterWorld },
    { "GetRealmCategories",UIWow_LuaRealmCategories },
    { "GetSelectedCategory", UIWow_LuaZero },
    { "GetNumRealms",      UIWow_LuaZero },
    { "GetRealmInfo",      UIWow_LuaRealmInfo },
    { "RequestRealmList",  UIWow_LuaNoop },
    { "CancelRealmListQuery", UIWow_LuaNoop },
    { "ChangeRealm",       UIWow_LuaNoop },
    { "SetPreferredInfo",  UIWow_LuaNoop },
    { "SortRealms",        UIWow_LuaNoop },
    { "SetCharSelectModelFrame",  UIWow_LuaSetCharSelectModelFrame },
    { "SetCharSelectBackground",  UIWow_LuaSetCharSelectBackground },
    { "SetCharCustomizeFrame",    UIWow_LuaSetCharCustomizeFrame },
    { "SetCharCustomizeBackground", UIWow_LuaSetCharCustomizeBackground },
    { "ResetCharCustomize",       UIWow_LuaResetCharCustomize },
    { "GetAvailableRaces",        UIWow_LuaGetAvailableRaces },
    { "GetAvailableClasses",      UIWow_LuaGetAvailableClasses },
    { "GetClassesForRace",        UIWow_LuaGetClassesForRace },
    { "GetFactionForRace",        UIWow_LuaGetFactionForRace },
    { "GetNameForRace",           UIWow_LuaGetNameForRace },
    { "GetSelectedRace",          UIWow_LuaGetSelectedRace },
    { "GetSelectedSex",           UIWow_LuaGetSelectedSex },
    { "GetSelectedClass",         UIWow_LuaGetSelectedClass },
    { "SetSelectedRace",          UIWow_LuaSetSelectedRaceChanged },
    { "SetSelectedSex",           UIWow_LuaSetSelectedSexChanged },
    { "SetSelectedClass",         UIWow_LuaSetSelectedClassChanged },
    { "IsRaceClassValid",         UIWow_LuaIsRaceClassValid },
    { "IsRaceClassRestricted",    UIWow_LuaNoop },
    { "GetHairCustomization",     UIWow_LuaGetHairCustomization },
    { "GetFacialHairCustomization", UIWow_LuaGetFacialHairCustomization },
    { "GetCharacterCreateFacing", UIWow_LuaGetCharacterCreateFacing },
    { "SetCharacterCreateFacing", UIWow_LuaSetCharacterCreateFacing },
    { "CycleCharCustomization",   UIWow_LuaCycleCharCustomization },
    { "RandomizeCharCustomization", UIWow_LuaRandomizeCharCustomization },
    { "GetRandomName",            UIWow_LuaGetRandomName },
    { "CreateCharacter",          UIWow_LuaCreateCharacter },
    { "CharacterCreateResult",    UIWow_LuaCharacterCreateResult },
    { "UpdateCustomizationBackground", UIWow_LuaNoop },
    { "UpdateSelectionCustomizationScene", UIWow_LuaUpdateCustomizationScene },
    { "UpdateCustomizationScene",   UIWow_LuaUpdateCustomizationScene },
    { "GetCreateBackgroundModel", UIWow_LuaNil },
    { "GetCharacterListUpdate",   UIWow_LuaGetCharacterListUpdate },
    { "GetNumCharacters",  UIWow_LuaGetNumCharacters },
    { "GetCharacterInfo",  UIWow_LuaGetCharacterInfo },
    { "SelectCharacter",   UIWow_LuaSelectCharacter },
    { "GetCharacterSelectFacing", UIWow_LuaZero },
    { "SetCharacterSelectFacing", UIWow_LuaNoop },
    { "SetCurrentScreen",  UIWow_LuaNoop },
    { "SetCurrentGlueScreenName", UIWow_LuaNoop },
    { "QuitGame",          UIWow_LuaNoop },
    { "Screenshot",        UIWow_LuaNoop },
    { "CharacterCreate_UpdateModel", UIWow_LuaNoop },
    { "CharacterCreateRotateLeft_OnUpdate", UIWow_LuaNoop },
    { "CharacterCreateRotateRight_OnUpdate", UIWow_LuaNoop },
    { NULL, NULL },
};

typedef struct {
    cstring_t table;
    cstring_t field;
    cstring_t global;
} uiWowLuaAlias_t;

static uiWowLuaAlias_t const wow_lua_aliases[] = {
    { "string", "format", "format" },
    { "string", "len",    "strlen" },
    { "string", "upper",  "strupper" },
    { "table",  "insert", "tinsert" },
    { "table",  "remove", "tremove" },
    { "math",   "floor",  "floor" },
    { "math",   "ceil",   "ceil" },
    { "math",   "abs",    "abs" },
    { "math",   "mod",    "mod" },
    { "math",   "sqrt",   "sqrt" },
    { "math",   "max",    "max" },
    { "math",   "min",    "min" },
    { NULL, NULL, NULL },
};

static void UIWow_SetGlobalFunc(lua_State *L, cstring_t name, lua_CFunction func) {
    lua_pushcfunction(L, func);
    lua_setglobal(L, name);
}

static void UIWow_SetGlobalAlias(lua_State *L, uiWowLuaAlias_t const *alias) {
    lua_getglobal(L, alias->table);
    lua_getfield(L, -1, alias->field);
    lua_setglobal(L, alias->global);
    lua_pop(L, 1);
}

static void UIWow_RegisterGlobalFuncs(lua_State *L, luaL_Reg const *funcs) {
    for (; funcs && funcs->name; funcs++)
        UIWow_SetGlobalFunc(L, funcs->name, funcs->func);
}

static void UIWow_RegisterGlobalAliases(lua_State *L) {
    for (uiWowLuaAlias_t const *alias = wow_lua_aliases; alias->global; alias++)
        UIWow_SetGlobalAlias(L, alias);
}

/* -------------------------------------------------------------------------
 * VM bootstrap
 * ---------------------------------------------------------------------- */

static int UIWow_LuaTraceback(lua_State *L) {
    cstring_t message = lua_tostring(L, 1);

    luaL_traceback(L, L, message ? message : "Lua error", 1);
    return 1;
}

bool UIWow_LuaPCall(int nargs) {
    int traceback = lua_gettop(wow_ui.lua) - nargs;
    int status;

    lua_pushcfunction(wow_ui.lua, UIWow_LuaTraceback);
    lua_insert(wow_ui.lua, traceback);
    status = lua_pcall(wow_ui.lua, nargs, 0, traceback);
    lua_remove(wow_ui.lua, traceback);
    if (status != LUA_OK) {
        cstring_t msg = lua_tostring(wow_ui.lua, -1);
        UIWow_Printf("UIWow Lua: %s\n", msg);
        fprintf(stderr, "UIWow Lua: %s\n", msg ? msg : "(null)");
        lua_pop(wow_ui.lua, 1);
        return false;
    }
    return true;
}

static bool UIWow_RunLuaBuffer(cstring_t name, cstring_t script, size_t len) {
    if (!wow_ui.lua || !script || len == 0) {
        return false;
    }
    if (luaL_loadbuffer(wow_ui.lua, script, len, name) != LUA_OK) {
        UIWow_Printf("UIWow Lua load: %s\n", lua_tostring(wow_ui.lua, -1));
        lua_pop(wow_ui.lua, 1);
        return false;
    }
    return UIWow_LuaPCall(0);
}

static char *UIWow_LuaCompatBuffer(cstring_t script, size_t len) {
    static cstring_t needles[] = {
        " in GlueScreenInfo do",
        " in FRAMES_TO_BACKDROP_COLOR do",
        NULL
    };
    static cstring_t replacements[] = {
        " in pairs(GlueScreenInfo) do",
        " in pairs(FRAMES_TO_BACKDROP_COLOR) do",
        NULL
    };
    size_t extra = 0; char *out, *dst; cstring_t src = script;
    FOR_LOOP(i, sizeof(needles) / sizeof(needles[0])) {
        cstring_t p = script;
        if (!needles[i]) break;
        while ((p = strstr(p, needles[i])) != NULL) {
            extra += strlen(replacements[i]) - strlen(needles[i]);
            p += strlen(needles[i]);
        }
    }
    if (!extra) return NULL;
    out = malloc(len + extra + 1);
    if (!out) return NULL;
    dst = out;
    while (*src) {
        bool replaced = false;
        FOR_LOOP(i, sizeof(needles) / sizeof(needles[0])) {
            if (!needles[i]) break;
            if (!strncmp(src, needles[i], strlen(needles[i]))) {
                size_t n = strlen(replacements[i]);
                memcpy(dst, replacements[i], n);
                dst += n; src += strlen(needles[i]); replaced = true; break;
            }
        }
        if (!replaced) *dst++ = *src++;
    }
    *dst = '\0';
    return out;
}

static char *UIWow_LuaCompatVarargs(cstring_t script, size_t len) {
    static cstring_t insert = "    local arg = { ... }; arg.n = select('#', ...)\n";
    size_t extra = 0; char *out, *dst; cstring_t src = script;

    while (*src) {
        cstring_t line = src, end = src;
        while (*end && *end != '\n' && *end != '\r') end++;
        if (!strncmp(line, "function ", 9) && memmem(line, (size_t)(end - line), "(...)", 5) && extra < SIZE_MAX - strlen(insert))
            extra += strlen(insert);
        src = end;
        while (*src == '\n' || *src == '\r') src++;
    }
    if (!extra) return NULL;
    out = malloc(len + extra + 1);
    if (!out) return NULL;
    src = script; dst = out;
    while (*src) {
        cstring_t line = src, end = src;
        while (*end && *end != '\n' && *end != '\r') end++;
        memcpy(dst, line, (size_t)(end - line));
        dst += end - line;
        while (*end == '\r' || *end == '\n') *dst++ = *end++;
        if (!strncmp(line, "function ", 9) && memmem(line, (size_t)(end - line), "(...)", 5)) {
            size_t n = strlen(insert);
            memcpy(dst, insert, n);
            dst += n;
        }
        src = end;
    }
    *dst = '\0';
    return out;
}

bool UIWow_RunLuaString(cstring_t name, cstring_t script) {
    if (!script) {
        return false;
    }
    return UIWow_RunLuaBuffer(name, script, strlen(script));
}

bool UIWow_LoadLuaFile(cstring_t path, bool noisy_missing) {
    void *buf = NULL;
    char *compat, *compat_varargs, *script;
    int size;

    if (!mi.FS_ReadFile || !mi.FS_FreeFile || !path) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_INPUT_FS, "UIWow: FS_ReadFile/FS_FreeFile unavailable; cannot load Lua files\n");
        return false;
    }
    size = mi.FS_ReadFile(path, &buf);
    if (size <= 0 || !buf) {
        if (noisy_missing) {
            UIWow_Printf("UIWow: could not load '%s'\n", path);
        }
        SAFE_DELETE(buf, mi.FS_FreeFile);
        return false;
    }
    compat = UIWow_LuaCompatBuffer(buf, (size_t)size);
    script = compat ? compat : (char *)buf;
    compat_varargs = UIWow_LuaCompatVarargs(script, compat ? strlen(compat) : (size_t)size);
    UIWow_RunLuaBuffer(path, compat_varargs ? compat_varargs : script, compat_varargs ? strlen(compat_varargs) : (compat ? strlen(compat) : (size_t)size));
    SAFE_DELETE(compat, free);
    SAFE_DELETE(compat_varargs, free);
    mi.FS_FreeFile(buf);
    return true;
}

static bool UIWow_HasArchiveFile(cstring_t path) {
    void *buf = NULL;
    int size;

    if (!mi.FS_ReadFile || !mi.FS_FreeFile || !path) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_INPUT_FS, "UIWow: FS_ReadFile/FS_FreeFile unavailable; cannot probe archive files\n");
        return false;
    }
    size = mi.FS_ReadFile(path, &buf);
    if (size > 0 && buf) {
        mi.FS_FreeFile(buf);
        return true;
    }
    SAFE_DELETE(buf, mi.FS_FreeFile);
    return false;
}

static void UIWow_LoadLegacyMenuLua(void) {
    UIWow_LoadLuaFile("Interface\\FrameXML\\OW3Glue.lua", true);
    UIWow_LoadLuaFile("Interface\\FrameXML\\LoadingScreen.lua", false);
    UIWow_LoadLuaFile("Interface\\FrameXML\\LoginScreen.lua", false);
    UIWow_LoadLuaFile("Interface\\FrameXML\\CharacterSelectScreen.lua", false);
    UIWow_LoadLuaFile("Interface\\FrameXML\\CharacterCreateScreen.lua", false);
}

static cstring_t const WOW_GLUE_XML_TOC = "Interface\\GlueXML\\GlueXML.toc";

static bool UIWow_LoadGlueFrameXml(void) {
    if (!UIWow_LoadLuaFile("Interface\\GlueXML\\GlueStrings.lua", false)) {
        UIWow_Printf("UIWow: missing Glue prerequisite 'Interface\\GlueXML\\GlueStrings.lua'\n");
    }
    if (!UIWow_LoadLuaFile("Interface\\FrameXML\\GlobalStrings.lua", false)) {
        UIWow_Printf("UIWow: missing Glue prerequisite 'Interface\\FrameXML\\GlobalStrings.lua'\n");
    }
    return UIWow_XMLLoadGlueFromToc(WOW_GLUE_XML_TOC);
}

static void UIWow_OpenLuaLib(lua_State *L, cstring_t name, lua_CFunction openf) {
    luaL_requiref(L, name, openf, 1);
    lua_pop(L, 1);
}

void UIWow_InitLua(void) {
    lua_State *L;

    wow_ui.lua = luaL_newstate();
    if (!wow_ui.lua) {
        UIWow_Printf("UIWow: luaL_newstate failed\n");
        return;
    }
    L = wow_ui.lua;
    UIWow_OpenLuaLib(L, LUA_GNAME,    luaopen_base);
    UIWow_OpenLuaLib(L, LUA_TABLIBNAME, luaopen_table);
    UIWow_OpenLuaLib(L, LUA_STRLIBNAME, luaopen_string);
    UIWow_OpenLuaLib(L, LUA_MATHLIBNAME, luaopen_math);
    UIWow_XMLInitRuntime();
    UIWow_SetGlobalFunc(L, "TEXT", UIWow_LuaTextCompat);
    UIWow_RegisterGlobalAliases(L);
    UIWow_RegisterGlobalFuncs(L, wow_global_funcs);

    lua_newtable(L);
    luaL_setfuncs(L, wow_lua_funcs, 0);
    lua_setglobal(L, "ow3");

    if (UIWow_HasArchiveFile("Interface\\FrameXML\\OW3Glue.lua")) {
        UIWow_Printf("UIWow: using legacy FrameXML menu Lua bootstrap\n");
        UIWow_LoadLegacyMenuLua();
        snprintf(wow_ui.current_menu, sizeof(wow_ui.current_menu), "%s", "login");
    } else if (UIWow_LoadGlueFrameXml()) {
        UIWow_Printf("UIWow: using GlueXML FrameXML bootstrap\n");
        lua_getglobal(L, "SetGlueScreen");
        if (lua_isfunction(L, -1)) {
            lua_pushstring(L, "login");
            UIWow_LuaPCall(1);
        } else {
            lua_pop(L, 1);
            UIWow_WarnOnce(WOW_UI_WARN_NO_GLUE_BOOTSTRAP, "UIWow: Glue bootstrap missing 'SetGlueScreen'\n");
        }
        snprintf(wow_ui.current_menu, sizeof(wow_ui.current_menu), "%s", "login");
    } else {
        UIWow_Printf("UIWow: no legacy OW3 FrameXML or GlueXML Lua bootstrap found\n");
    }
}

void UIWow_ShutdownLua(void) {
    UIWow_XMLShutdownRuntime();
    if (wow_ui.lua) {
        lua_close(wow_ui.lua);
        wow_ui.lua = NULL;
    }
}

/* -------------------------------------------------------------------------
 * Per-frame Lua callbacks
 * ---------------------------------------------------------------------- */

void UIWow_CallLuaDraw(void) {
    if (!wow_ui.lua) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE, "UIWow: Lua state is not initialized; draw callback skipped\n");
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_draw");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_DRAW_HANDLER, "UIWow: missing Lua function 'ow3_draw'\n");
        return;
    }
    UIWow_LuaPCall(0);
}

void UIWow_CallLuaUpdate(uint32_t msec) {
    if (!wow_ui.lua) {
        if (!wow_ui.lua) {
            UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE, "UIWow: Lua state is not initialized; update callback skipped\n");
        }
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_update");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_UPDATE_HANDLER, "UIWow: missing Lua function 'ow3_update'\n");
        return;
    }
    lua_pushinteger(wow_ui.lua, msec);
    UIWow_LuaPCall(1);
}
