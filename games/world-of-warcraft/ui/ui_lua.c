/*
 * ui_lua.c — Lua VM initialisation, C→Lua bindings, and script execution.
 *
 * Owns the lua_State lifecycle and every ow3.* function exposed to Lua.
 * The public surface is small: UIWow_InitLua / UIWow_ShutdownLua boot the
 * VM, UIWow_LuaPCall runs a function already on the stack, and the three
 * Call* helpers drive the per-frame callbacks.
 */
#include "ui_local.h"

#include <strings.h>

/* -------------------------------------------------------------------------
 * Lua argument helpers
 * ---------------------------------------------------------------------- */

static BYTE UIWow_LuaColorByte(lua_State *L, int index, BYTE fallback) {
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
    return (BYTE)(value + 0.5);
}

static COLOR32 UIWow_LuaColor(lua_State *L, int first, COLOR32 fallback) {
    return MAKE(COLOR32,
                UIWow_LuaColorByte(L, first,     fallback.r),
                UIWow_LuaColorByte(L, first + 1, fallback.g),
                UIWow_LuaColorByte(L, first + 2, fallback.b),
                UIWow_LuaColorByte(L, first + 3, fallback.a));
}

static RECT UIWow_LuaRect(lua_State *L, int first) {
    return MAKE(RECT,
                (FLOAT)luaL_checknumber(L, first),
                (FLOAT)luaL_checknumber(L, first + 1),
                (FLOAT)luaL_checknumber(L, first + 2),
                (FLOAT)luaL_checknumber(L, first + 3));
}

/* -------------------------------------------------------------------------
 * ow3.draw_* bindings
 * ---------------------------------------------------------------------- */

static int UIWow_LuaDrawImage(lua_State *L) {
    LPCSTR name = luaL_checkstring(L, 1);
    RECT screen = UIWow_LuaRect(L, 2);
    RECT uv = MAKE(RECT, 0, 0, 1, 1);
    COLOR32 color = UIWow_LuaColor(L, 6, COLOR32_WHITE);
    LPTEXTURE texture = UIWow_LoadTexture(name);

    if (wow_ui.renderer && texture) {
        wow_ui.renderer->DrawImage(texture, &screen, &uv, color);
    }
    lua_pushboolean(L, texture != NULL);
    return 1;
}

static int UIWow_LuaDrawImageUV(lua_State *L) {
    LPCSTR name = luaL_checkstring(L, 1);
    RECT screen = UIWow_LuaRect(L, 2);
    FLOAT left   = (FLOAT)luaL_checknumber(L, 6);
    FLOAT right  = (FLOAT)luaL_checknumber(L, 7);
    FLOAT top    = (FLOAT)luaL_checknumber(L, 8);
    FLOAT bottom = (FLOAT)luaL_checknumber(L, 9);
    RECT uv = MAKE(RECT, left, top, right - left, bottom - top);
    COLOR32 color = UIWow_LuaColor(L, 10, COLOR32_WHITE);
    LPTEXTURE texture = UIWow_LoadTexture(name);

    if (wow_ui.renderer && texture) {
        wow_ui.renderer->DrawImage(texture, &screen, &uv, color);
    }
    lua_pushboolean(L, texture != NULL);
    return 1;
}

static int UIWow_LuaDrawImageIndex(lua_State *L) {
    DWORD index = (DWORD)luaL_checkinteger(L, 1);
    RECT screen = UIWow_LuaRect(L, 2);
    RECT uv = MAKE(RECT, 0, 0, 1, 1);
    COLOR32 color = UIWow_LuaColor(L, 6, COLOR32_WHITE);
    LPCTEXTURE texture = uiimport.GetTexture ? uiimport.GetTexture(index) : NULL;

    UIWow_EnsureRenderer();
    if (wow_ui.renderer && texture) {
        wow_ui.renderer->DrawImage((LPTEXTURE)texture, &screen, &uv, color);
    }
    lua_pushboolean(L, texture != NULL);
    return 1;
}

static int UIWow_LuaDrawColor(lua_State *L) {
    RECT screen = UIWow_LuaRect(L, 1);
    COLOR32 color = UIWow_LuaColor(L, 5, COLOR32_WHITE);

    UIWow_EnsureRenderer();
    if (wow_ui.renderer && wow_ui.renderer->DrawFill) {
        wow_ui.renderer->DrawFill(&screen, color);
    }
    return 0;
}

/* draw_backdrop(bg_path, border_path, x, y, w, h, edge_size)
 * Draws a WoW-style backdrop.
 *   bg_path     — tiled background texture drawn inside the inset region
 *   border_path — border texture drawn as a full-size overlay; WoW border
 *                 textures are pre-composed with all four corners and edges
 *                 and are designed to be stretched over the entire frame rect
 *   edge_size   — inset in 0-1 screen space applied to the bg only; the
 *                 border always covers the full rect
 * Either path may be nil/"" to skip that layer. */
static int UIWow_LuaDrawBackdrop(lua_State *L) {
    LPCSTR bg_path     = luaL_optstring(L, 1, "");
    LPCSTR border_path = luaL_optstring(L, 2, "");
    RECT screen        = UIWow_LuaRect(L, 3);
    FLOAT esz          = (FLOAT)luaL_optnumber(L, 7, 0.0);
    RECT full_uv       = MAKE(RECT, 0, 0, 1, 1);

    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return 0;
    }

    if (bg_path && *bg_path) {
        LPTEXTURE bg = UIWow_LoadTexture(bg_path);
        if (bg) {
            RECT inner = MAKE(RECT, screen.x + esz, screen.y + esz,
                              screen.w - esz * 2.0f, screen.h - esz * 2.0f);
            wow_ui.renderer->DrawImage(bg, &inner, &full_uv, COLOR32_WHITE);
        }
    }

    if (border_path && *border_path) {
        LPTEXTURE border = UIWow_LoadTexture(border_path);
        if (border) {
            wow_ui.renderer->DrawImage(border, &screen, &full_uv, COLOR32_WHITE);
        }
    }
    return 0;
}

static int UIWow_LuaDrawMinimap(lua_State *L) {
    RECT screen = UIWow_LuaRect(L, 1);

    UIWow_EnsureRenderer();
    if (wow_ui.renderer && wow_ui.renderer->DrawMinimap) {
        wow_ui.renderer->DrawMinimap(&screen);
    }
    return 0;
}

static int UIWow_LuaDrawText(lua_State *L) {
    LPCSTR text = luaL_checkstring(L, 1);
    RECT screen = UIWow_LuaRect(L, 2);
    DWORD size = (DWORD)luaL_optinteger(L, 6, 14);
    COLOR32 color = UIWow_LuaColor(L, 7, COLOR32_WHITE);
    LPCSTR align = luaL_optstring(L, 11, "left");
    LPCFONT font = UIWow_LoadFont(size);
    uiFontJustificationH_t halign = FONT_JUSTIFYLEFT;

    if (!strcasecmp(align, "center")) {
        halign = FONT_JUSTIFYCENTER;
    } else if (!strcasecmp(align, "right")) {
        halign = FONT_JUSTIFYRIGHT;
    }

    if (wow_ui.renderer && font) {
        wow_ui.renderer->DrawText(&MAKE(drawText_t,
                                        .font = font,
                                        .text = text,
                                        .rect = screen,
                                        .color = color,
                                        .textWidth = screen.w,
                                        .lineHeight = screen.h,
                                        .wordWrap = false,
                                        .halign = halign,
                                        .valign = FONT_JUSTIFYMIDDLE));
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * ow3.player / inventory / actions / misc bindings
 * ---------------------------------------------------------------------- */

static int UIWow_LuaStat(lua_State *L) {
    DWORD index = (DWORD)luaL_checkinteger(L, 1);
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    lua_pushinteger(L, ps && index < MAX_STATS ? ps->stats[index] : 0);
    return 1;
}

static int UIWow_LuaText(lua_State *L) {
    DWORD index = (DWORD)luaL_checkinteger(L, 1);
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    LPCSTR text = ps && index < MAX_STATS && ps->texts[index] ? ps->texts[index] : "";

    lua_pushstring(L, text);
    return 1;
}

static int UIWow_LuaPlayerName(lua_State *L) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    LPCSTR name = ps && ps->name && *ps->name ? ps->name : "Player";

    lua_pushstring(L, name);
    return 1;
}

static void UIWow_LuaSetInteger(lua_State *L, LPCSTR name, lua_Integer value) {
    lua_pushinteger(L, value);
    lua_setfield(L, -2, name);
}

static void UIWow_LuaSetPlayerField(lua_State *L, LPCSTR name, int stat) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    lua_pushinteger(L, ps ? ps->stats[stat] : 0);
    lua_setfield(L, -2, name);
}

static int UIWow_LuaPlayer(lua_State *L) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    lua_newtable(L);
    lua_pushstring(L, ps && ps->name && *ps->name ? ps->name : "Player");
    lua_setfield(L, -2, "name");
    UIWow_LuaSetPlayerField(L, "health",    WOW_STAT_HEALTH);
    UIWow_LuaSetPlayerField(L, "healthMax", WOW_STAT_HEALTH_MAX);
    UIWow_LuaSetPlayerField(L, "power",     WOW_STAT_POWER);
    UIWow_LuaSetPlayerField(L, "powerMax",  WOW_STAT_POWER_MAX);
    UIWow_LuaSetPlayerField(L, "level",     WOW_STAT_LEVEL);
    UIWow_LuaSetPlayerField(L, "xp",        WOW_STAT_XP);
    UIWow_LuaSetPlayerField(L, "xpMax",     WOW_STAT_XP_MAX);
    UIWow_LuaSetPlayerField(L, "copper",    WOW_STAT_COPPER);
    return 1;
}

static void UIWow_LuaPushIcon(lua_State *L, uiWowIcon_t const *icon) {
    lua_newtable(L);
    lua_pushinteger(L, icon ? icon->image : 0);
    lua_setfield(L, -2, "image");
    lua_pushinteger(L, icon ? icon->count : 0);
    lua_setfield(L, -2, "count");
    lua_pushinteger(L, icon ? icon->slot : 0);
    lua_setfield(L, -2, "slot");
    lua_pushstring(L, icon && icon->name[0] ? icon->name : "");
    lua_setfield(L, -2, "name");
}

static int UIWow_LuaInventory(lua_State *L) {
    lua_newtable(L);
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        UIWow_LuaPushIcon(L, &wow_ui.inventory[i]);
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

static int UIWow_LuaActions(lua_State *L) {
    lua_newtable(L);
    FOR_LOOP(i, WOW_UI_ACTION_SLOTS) {
        UIWow_LuaPushIcon(L, &wow_ui.actions[i]);
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

static int UIWow_LuaTime(lua_State *L) {
    lua_pushinteger(L, uiimport.GetClientTime ? uiimport.GetClientTime() : 0);
    return 1;
}

static int UIWow_LuaCommand(lua_State *L) {
    LPCSTR text = luaL_checkstring(L, 1);

    if (uiimport.ServerCommand && text && *text) {
        uiimport.ServerCommand(text);
    }
    return 0;
}

/* draw_loading_background() — draws the current map background texture fullscreen */
static int UIWow_LuaDrawLoadingBackground(lua_State *L) {
    RECT full = MAKE(RECT, 0, 0, 1, 1);

    (void)L;
    UIWow_EnsureRenderer();
    if (wow_ui.renderer && wow_ui.background) {
        wow_ui.renderer->DrawImage(wow_ui.background, &full, &full, COLOR32_WHITE);
    }
    return 0;
}

/* draw_image_additive(path, x, y, w, h) — draw texture with additive blending (e.g. glow) */
static int UIWow_LuaDrawImageAdditive(lua_State *L) {
    LPCSTR name = luaL_checkstring(L, 1);
    RECT screen = UIWow_LuaRect(L, 2);
    RECT uv = MAKE(RECT, 0, 0, 1, 1);
    COLOR32 color = UIWow_LuaColor(L, 6, COLOR32_WHITE);
    LPTEXTURE texture = UIWow_LoadTexture(name);

    if (wow_ui.renderer && texture) {
        wow_ui.renderer->DrawImageEx(&MAKE(drawImage_t,
                                           .texture   = texture,
                                           .screen    = screen,
                                           .uv        = uv,
                                           .color     = color,
                                           .shader    = SHADER_UI,
                                           .alphamode = BLEND_MODE_ADD));
    }
    lua_pushboolean(L, texture != NULL);
    return 1;
}

static int UIWow_LuaGetLoadingProgress(lua_State *L) {
    FLOAT target = uiimport.GetLoadingProgress ? uiimport.GetLoadingProgress() : 0.0f;

    if (target < 0.0f) { target = 0.0f; }
    else if (target > 1.0f) { target = 1.0f; }

    if (target < wow_ui.displayed_progress) {
        wow_ui.displayed_progress = target;
    } else {
        wow_ui.displayed_progress = wow_ui.displayed_progress * 0.82f + target * 0.18f;
        if (target - wow_ui.displayed_progress < 0.002f) {
            wow_ui.displayed_progress = target;
        }
    }
    lua_pushnumber(L, (lua_Number)wow_ui.displayed_progress);
    return 1;
}

static int UIWow_LuaGetLoadingTitle(lua_State *L) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    LPCSTR title = ps ? ps->texts[PLAYERTEXT_MAP_TITLE] : NULL;

    if (!title || !*title) {
        title = uiimport.GetLoadingMap ? uiimport.GetLoadingMap() : "";
    }
    lua_pushstring(L, title ? title : "");
    return 1;
}

static int UIWow_LuaGetLoadingStatus(lua_State *L) {
    LPCSTR status = uiimport.GetLoadingStatus ? uiimport.GetLoadingStatus() : "";

    lua_pushstring(L, status ? status : "");
    return 1;
}

static int UIWow_LuaLoadMap(lua_State *L) {
    LPCSTR map_name = luaL_optstring(L, 1, "Maps\\Campaign\\Default.w3m");
    char cmd[512];

    if (!map_name || !*map_name) {
        map_name = "Maps\\Campaign\\Default.w3m";
    }
    snprintf(cmd, sizeof(cmd), "+map %s", map_name);
    if (uiimport.Cmd_ExecuteText) {
        uiimport.Cmd_ExecuteText(cmd);
    }
    return 0;
}

static luaL_Reg const wow_lua_funcs[] = {
    { "draw_loading_background", UIWow_LuaDrawLoadingBackground },
    { "draw_image",          UIWow_LuaDrawImage },
    { "draw_image_uv",       UIWow_LuaDrawImageUV },
    { "draw_image_index",    UIWow_LuaDrawImageIndex },
    { "draw_image_additive", UIWow_LuaDrawImageAdditive },
    { "draw_color",          UIWow_LuaDrawColor },
    { "draw_backdrop",       UIWow_LuaDrawBackdrop },
    { "draw_minimap",        UIWow_LuaDrawMinimap },
    { "draw_text",           UIWow_LuaDrawText },
    { "get_loading_progress",UIWow_LuaGetLoadingProgress },
    { "get_loading_title",   UIWow_LuaGetLoadingTitle },
    { "get_loading_status",  UIWow_LuaGetLoadingStatus },
    { "stat",             UIWow_LuaStat },
    { "text",             UIWow_LuaText },
    { "player_name",      UIWow_LuaPlayerName },
    { "player",           UIWow_LuaPlayer },
    { "inventory",        UIWow_LuaInventory },
    { "actions",          UIWow_LuaActions },
    { "time",             UIWow_LuaTime },
    { "command",          UIWow_LuaCommand },
    { "load_map",         UIWow_LuaLoadMap },
    { NULL, NULL },
};

/* -------------------------------------------------------------------------
 * VM bootstrap
 * ---------------------------------------------------------------------- */

static int UIWow_LuaTraceback(lua_State *L) {
    LPCSTR message = lua_tostring(L, 1);

    luaL_traceback(L, L, message ? message : "Lua error", 1);
    return 1;
}

BOOL UIWow_LuaPCall(int nargs) {
    int traceback = lua_gettop(wow_ui.lua) - nargs;
    int status;

    lua_pushcfunction(wow_ui.lua, UIWow_LuaTraceback);
    lua_insert(wow_ui.lua, traceback);
    status = lua_pcall(wow_ui.lua, nargs, 0, traceback);
    lua_remove(wow_ui.lua, traceback);
    if (status != LUA_OK) {
        UIWow_Printf("UIWow Lua: %s\n", lua_tostring(wow_ui.lua, -1));
        lua_pop(wow_ui.lua, 1);
        return false;
    }
    return true;
}

static BOOL UIWow_RunLuaBuffer(LPCSTR name, LPCSTR script, size_t len) {
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

static void UIWow_LoadMenuLua(LPCSTR filename) {
    void *buf = NULL;
    int size;
    char path[512];

    if (!uiimport.FS_ReadFile || !uiimport.FS_FreeFile || !filename) {
        return;
    }
    snprintf(path, sizeof(path), "Interface\\FrameXML\\%s", filename);
    size = uiimport.FS_ReadFile(path, &buf);
    if (size <= 0 || !buf) {
        fprintf(stderr, "UIWow: could not load '%s'\n", path);
        SAFE_DELETE(buf, uiimport.FS_FreeFile);
        return;
    }
    UIWow_RunLuaBuffer(path, buf, (size_t)size);
    uiimport.FS_FreeFile(buf);
}

static void UIWow_OpenLuaLib(lua_State *L, LPCSTR name, lua_CFunction openf) {
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

    lua_newtable(L);
    luaL_setfuncs(L, wow_lua_funcs, 0);
    UIWow_LuaSetInteger(L, "STAT_HEALTH",     WOW_STAT_HEALTH);
    UIWow_LuaSetInteger(L, "STAT_HEALTH_MAX", WOW_STAT_HEALTH_MAX);
    UIWow_LuaSetInteger(L, "STAT_POWER",      WOW_STAT_POWER);
    UIWow_LuaSetInteger(L, "STAT_POWER_MAX",  WOW_STAT_POWER_MAX);
    UIWow_LuaSetInteger(L, "STAT_LEVEL",      WOW_STAT_LEVEL);
    UIWow_LuaSetInteger(L, "STAT_XP",         WOW_STAT_XP);
    UIWow_LuaSetInteger(L, "STAT_XP_MAX",     WOW_STAT_XP_MAX);
    UIWow_LuaSetInteger(L, "STAT_COPPER",     WOW_STAT_COPPER);
    lua_setglobal(L, "ow3");

    UIWow_LoadMenuLua("OW3Glue.lua");
    UIWow_LoadMenuLua("LoadingScreen.lua");
    UIWow_LoadMenuLua("LoginScreen.lua");
    UIWow_LoadMenuLua("CharacterSelectScreen.lua");
    UIWow_LoadMenuLua("CharacterCreateScreen.lua");
    UIWow_LoadMenuLua("GameHUD.lua");
}

void UIWow_ShutdownLua(void) {
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
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_draw");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        return;
    }
    UIWow_LuaPCall(0);
}

void UIWow_CallLuaUpdate(DWORD msec) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    if (!wow_ui.lua || !ps || ps->client_ui_state != CLIENT_UI_GAME) {
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_update");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        return;
    }
    lua_pushinteger(wow_ui.lua, msec);
    UIWow_LuaPCall(1);
}
