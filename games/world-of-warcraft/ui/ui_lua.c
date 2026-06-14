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

#ifndef LUA_OK
#define LUA_OK 0
#endif
#ifndef LUA_GNAME
#define LUA_GNAME "_G"
#endif

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
    LPCSTR bg_path     = luaL_optstring(L, 1, "");
    LPCSTR border_path = luaL_optstring(L, 2, "");
    RECT sc            = UIWow_LuaRect(L, 3);
    FLOAT e            = (FLOAT)luaL_optnumber(L, 7, 0.0);
    RECT fuv           = MAKE(RECT, 0, 0, 1, 1);

    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return 0;
    }

    /* Background — stretched inside the inset */
    if (bg_path && *bg_path) {
        LPTEXTURE bg = UIWow_LoadTexture(bg_path);
        if (bg) {
            RECT inner = MAKE(RECT, sc.x + e, sc.y + e,
                              sc.w - e * 2.0f, sc.h - e * 2.0f);
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
            FLOAT r = sc.x + sc.w; /* right edge */
            FLOAT b = sc.y + sc.h; /* bottom edge */
            /* UVs: WoW border textures place TL corner in top-left quadrant,
             * TR in top-right, BL in bottom-left, BR in bottom-right.
             * Each quadrant = 0.5 × 0.5 of the texture. */
            RECT uv_tl = MAKE(RECT, 0.0f, 0.0f, 0.5f, 0.5f);
            RECT uv_tr = MAKE(RECT, 0.5f, 0.0f, 0.5f, 0.5f);
            RECT uv_bl = MAKE(RECT, 0.0f, 0.5f, 0.5f, 0.5f);
            RECT uv_br = MAKE(RECT, 0.5f, 0.5f, 0.5f, 0.5f);
            /* corners */
            RECT tl = MAKE(RECT, sc.x,     sc.y,     e, e);
            RECT tr = MAKE(RECT, r - e,    sc.y,     e, e);
            RECT bl = MAKE(RECT, sc.x,     b - e,    e, e);
            RECT br = MAKE(RECT, r - e,    b - e,    e, e);
            /* edges */
            RECT top_e = MAKE(RECT, sc.x + e, sc.y,   sc.w - e*2, e);
            RECT bot_e = MAKE(RECT, sc.x + e, b - e,  sc.w - e*2, e);
            RECT lft_e = MAKE(RECT, sc.x,     sc.y+e, e, sc.h - e*2);
            RECT rgt_e = MAKE(RECT, r - e,    sc.y+e, e, sc.h - e*2);
            /* top/bottom edges use top strip UV (y=0..0.5, full x) */
            RECT uv_top = MAKE(RECT, 0.0f, 0.0f, 1.0f, 0.5f);
            RECT uv_bot = MAKE(RECT, 0.0f, 0.5f, 1.0f, 0.5f);
            /* left/right edges use left strip UV (x=0..0.5, full y) */
            RECT uv_lft = MAKE(RECT, 0.0f, 0.0f, 0.5f, 1.0f);
            RECT uv_rgt = MAKE(RECT, 0.5f, 0.0f, 0.5f, 1.0f);

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
    lua_pushinteger(L, (ps && ps->client_ui_state == CLIENT_UI_GAME) ? 1 : 0);
    lua_setfield(L, -2, "client_ui_state");
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

static int UIWow_LuaDefaultServerLogin(lua_State *L) {
    (void)L;
    lua_getglobal(wow_ui.lua, "SetGlueScreen");
    if (lua_isfunction(wow_ui.lua, -1)) {
        lua_pushstring(wow_ui.lua, "charselect");
        UIWow_LuaPCall(1);
    } else {
        lua_pop(wow_ui.lua, 1);
        if (uiimport.Cmd_ExecuteText) uiimport.Cmd_ExecuteText("menu_character_select\n");
    }
    return 0;
}

static int UIWow_LuaNoop(lua_State *L) { (void)L; return 0; }

#include "ui_dbc.h"

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

static int UIWow_LuaCharacterInfo(lua_State *L) {
    lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); lua_pushinteger(L, 0); lua_pushnil(L);
    lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); lua_pushnil(L); lua_pushnil(L);
    return 10;
}

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
    { "PlaySound",         UIWow_LuaNoop },
    { "PlayGlueMusic",     UIWow_LuaNoop },
    { "StopGlueMusic",     UIWow_LuaNoop },
    { "PlayCreditsMusic",  UIWow_LuaNoop },
    { "DisconnectFromServer", UIWow_LuaNoop },
    { "IsConnectedToServer", UIWow_LuaTrue },
    { "GetBillingTimeRemaining", UIWow_LuaZero },
    { "EnterWorld",        UIWow_LuaNoop },
    { "GetRealmCategories",UIWow_LuaRealmCategories },
    { "GetSelectedCategory", UIWow_LuaZero },
    { "GetNumRealms",      UIWow_LuaZero },
    { "GetRealmInfo",      UIWow_LuaRealmInfo },
    { "RequestRealmList",  UIWow_LuaNoop },
    { "CancelRealmListQuery", UIWow_LuaNoop },
    { "ChangeRealm",       UIWow_LuaNoop },
    { "SetPreferredInfo",  UIWow_LuaNoop },
    { "SortRealms",        UIWow_LuaNoop },
    { "SetCharSelectModelFrame",  UIWow_LuaNoop },
    { "SetCharSelectBackground",  UIWow_LuaNoop },
    { "SetCharCustomizeFrame",    UIWow_LuaNoop },
    { "SetCharCustomizeBackground", UIWow_LuaNoop },
    { "ResetCharCustomize",       UIWow_LuaResetCharCustomize },
    { "GetAvailableRaces",        UIWow_LuaGetAvailableRaces },
    { "GetAvailableClasses",      UIWow_LuaGetAvailableClasses },
    { "GetClassesForRace",        UIWow_LuaGetClassesForRace },
    { "GetFactionForRace",        UIWow_LuaGetFactionForRace },
    { "GetNameForRace",           UIWow_LuaGetNameForRace },
    { "GetSelectedRace",          UIWow_LuaGetSelectedRace },
    { "GetSelectedSex",           UIWow_LuaGetSelectedSex },
    { "GetSelectedClass",         UIWow_LuaGetSelectedClass },
    { "SetSelectedRace",          UIWow_LuaSetSelectedRace },
    { "SetSelectedSex",           UIWow_LuaSetSelectedSex },
    { "SetSelectedClass",         UIWow_LuaSetSelectedClass },
    { "IsRaceClassValid",         UIWow_LuaIsRaceClassValid },
    { "IsRaceClassRestricted",    UIWow_LuaNoop },
    { "GetHairCustomization",     UIWow_LuaGetHairCustomization },
    { "GetFacialHairCustomization", UIWow_LuaGetFacialHairCustomization },
    { "GetCharacterCreateFacing", UIWow_LuaGetCharacterCreateFacing },
    { "SetCharacterCreateFacing", UIWow_LuaSetCharacterCreateFacing },
    { "CycleCharCustomization",   UIWow_LuaNoop },
    { "RandomizeCharCustomization", UIWow_LuaNoop },
    { "GetRandomName",            UIWow_LuaGetRandomName },
    { "CreateCharacter",          UIWow_LuaCreateCharacter },
    { "UpdateCustomizationBackground", UIWow_LuaNoop },
    { "UpdateSelectionCustomizationScene", UIWow_LuaNoop },
    { "GetCreateBackgroundModel", UIWow_LuaNil },
    { "GetCharacterListUpdate",   UIWow_LuaGetCharacterListUpdate },
    { "GetNumCharacters",  UIWow_LuaZero },
    { "GetCharacterInfo",  UIWow_LuaCharacterInfo },
    { "SelectCharacter",   UIWow_LuaNoop },
    { "GetCharacterSelectFacing", UIWow_LuaZero },
    { "SetCharacterSelectFacing", UIWow_LuaNoop },
    { "SetCurrentScreen",  UIWow_LuaNoop },
    { "SetCurrentGlueScreenName", UIWow_LuaNoop },
    { "QuitGame",          UIWow_LuaNoop },
    { "Screenshot",        UIWow_LuaNoop },
    { NULL, NULL },
};

typedef struct {
    LPCSTR table;
    LPCSTR field;
    LPCSTR global;
} uiWowLuaAlias_t;

static uiWowLuaAlias_t const wow_lua_aliases[] = {
    { "string", "format", "format" },
    { "string", "len",    "strlen" },
    { "string", "upper",  "strupper" },
    { "table",  "insert", "tinsert" },
    { "table",  "remove", "tremove" },
    { NULL, NULL, NULL },
};

static void UIWow_SetGlobalFunc(lua_State *L, LPCSTR name, lua_CFunction func) {
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
        LPCSTR msg = lua_tostring(wow_ui.lua, -1);
        UIWow_Printf("UIWow Lua: %s\n", msg);
        fprintf(stderr, "UIWow Lua: %s\n", msg ? msg : "(null)");
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

static char *UIWow_LuaCompatBuffer(LPCSTR script, size_t len) {
    static LPCSTR needles[] = { " in GlueScreenInfo do", " in FRAMES_TO_BACKDROP_COLOR do", NULL };
    static LPCSTR replacements[] = { " in pairs(GlueScreenInfo) do", " in pairs(FRAMES_TO_BACKDROP_COLOR) do", NULL };
    size_t extra = 0; char *out, *dst; LPCSTR src = script;
    FOR_LOOP(i, sizeof(needles) / sizeof(needles[0])) {
        LPCSTR p = script;
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
        BOOL replaced = false;
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

BOOL UIWow_RunLuaString(LPCSTR name, LPCSTR script) {
    if (!script) {
        return false;
    }
    return UIWow_RunLuaBuffer(name, script, strlen(script));
}

BOOL UIWow_LoadLuaFile(LPCSTR path, BOOL noisy_missing) {
    void *buf = NULL;
    char *compat;
    int size;

    if (!uiimport.FS_ReadFile || !uiimport.FS_FreeFile || !path) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_INPUT_FS,
                       "UIWow: FS_ReadFile/FS_FreeFile unavailable; cannot load Lua files\n");
        return false;
    }
    size = uiimport.FS_ReadFile(path, &buf);
    if (size <= 0 || !buf) {
        if (noisy_missing) {
            UIWow_Printf("UIWow: could not load '%s'\n", path);
        }
        SAFE_DELETE(buf, uiimport.FS_FreeFile);
        return false;
    }
    compat = UIWow_LuaCompatBuffer(buf, (size_t)size);
    UIWow_RunLuaBuffer(path, compat ? compat : buf, compat ? strlen(compat) : (size_t)size);
    SAFE_DELETE(compat, free);
    uiimport.FS_FreeFile(buf);
    return true;
}

static BOOL UIWow_HasArchiveFile(LPCSTR path) {
    void *buf = NULL;
    int size;

    if (!uiimport.FS_ReadFile || !uiimport.FS_FreeFile || !path) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_INPUT_FS,
                       "UIWow: FS_ReadFile/FS_FreeFile unavailable; cannot probe archive files\n");
        return false;
    }
    size = uiimport.FS_ReadFile(path, &buf);
    if (size > 0 && buf) {
        uiimport.FS_FreeFile(buf);
        return true;
    }
    SAFE_DELETE(buf, uiimport.FS_FreeFile);
    return false;
}

static void UIWow_LoadLegacyMenuLua(void) {
    UIWow_LoadLuaFile("Interface\\FrameXML\\OW3Glue.lua", true);
    UIWow_LoadLuaFile("Interface\\FrameXML\\GameHUD.lua", true);
    UIWow_LoadLuaFile("Interface\\FrameXML\\LoadingScreen.lua", false);
    UIWow_LoadLuaFile("Interface\\FrameXML\\LoginScreen.lua", false);
    UIWow_LoadLuaFile("Interface\\FrameXML\\CharacterSelectScreen.lua", false);
    UIWow_LoadLuaFile("Interface\\FrameXML\\CharacterCreateScreen.lua", false);
}

static LPCSTR const WOW_GLUE_XML_TOC = "Interface\\GlueXML\\GlueXML.toc";

static BOOL UIWow_LoadGlueFrameXml(void) {
    if (!UIWow_LoadLuaFile("Interface\\GlueXML\\GlueStrings.lua", false)) {
        UIWow_Printf("UIWow: missing Glue prerequisite 'Interface\\GlueXML\\GlueStrings.lua'\n");
    }
    if (!UIWow_LoadLuaFile("Interface\\FrameXML\\GlobalStrings.lua", false)) {
        UIWow_Printf("UIWow: missing Glue prerequisite 'Interface\\FrameXML\\GlobalStrings.lua'\n");
    }
    return UIWow_XMLLoadGlueFromToc(WOW_GLUE_XML_TOC);
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
    UIWow_XMLInitRuntime();
    UIWow_SetGlobalFunc(L, "TEXT", UIWow_LuaTextCompat);
    UIWow_RegisterGlobalAliases(L);
    UIWow_RegisterGlobalFuncs(L, wow_global_funcs);

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

    if (UIWow_HasArchiveFile("Interface\\FrameXML\\OW3Glue.lua")) {
        UIWow_Printf("UIWow: using legacy FrameXML menu Lua bootstrap\n");
        UIWow_LoadLegacyMenuLua();
    } else if (UIWow_LoadGlueFrameXml()) {
        UIWow_Printf("UIWow: using GlueXML FrameXML bootstrap\n");
        lua_getglobal(L, "SetGlueScreen");
        if (lua_isfunction(L, -1)) {
            lua_pushstring(L, "login");
            UIWow_LuaPCall(1);
        } else {
            lua_pop(L, 1);
            UIWow_WarnOnce(WOW_UI_WARN_NO_GLUE_BOOTSTRAP,
                           "UIWow: Glue bootstrap missing 'SetGlueScreen'\n");
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
        UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE,
                       "UIWow: Lua state is not initialized; draw callback skipped\n");
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_draw");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_DRAW_HANDLER,
                       "UIWow: missing Lua function 'ow3_draw'\n");
        return;
    }
    UIWow_LuaPCall(0);
}

void UIWow_CallLuaUpdate(DWORD msec) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    if (!wow_ui.lua || !ps || ps->client_ui_state != CLIENT_UI_GAME) {
        if (!wow_ui.lua) {
            UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE,
                           "UIWow: Lua state is not initialized; update callback skipped\n");
        }
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_update");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_UPDATE_HANDLER,
                       "UIWow: missing Lua function 'ow3_update'\n");
        return;
    }
    lua_pushinteger(wow_ui.lua, msec);
    UIWow_LuaPCall(1);
}
