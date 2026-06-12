#include "client/ui.h"
#include "common/wow_ui_shared.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdlib.h>

#define WOW_UI_MAX_TEXTURES 256
#define WOW_UI_MAX_FONTS 16

static uiImport_t uiimport;

typedef struct {
    char name[256];
    LPTEXTURE texture;
} uiWowTexture_t;

typedef struct {
    DWORD size;
    LPCFONT font;
} uiWowFont_t;

typedef struct {
    DWORD image;
    DWORD count;
    DWORD slot;
    char name[128];
} uiWowIcon_t;

typedef struct {
    LPRENDERER renderer;
    lua_State *lua;
    uiWowTexture_t textures[WOW_UI_MAX_TEXTURES];
    DWORD texture_recycle_index;
    uiWowFont_t fonts[WOW_UI_MAX_FONTS];
    uiWowIcon_t inventory[WOW_UI_INVENTORY_SLOTS];
    uiWowIcon_t actions[WOW_UI_ACTION_SLOTS];
    LPTEXTURE background;
    LPTEXTURE bar_background;
    LPTEXTURE bar_border;
    LPTEXTURE bar_fill;
    LPTEXTURE bar_glass;
    LPTEXTURE bar_glow;
    LPCFONT font_title;
    LPCFONT font_status;
    PATHSTR active_map;
    FLOAT displayed_progress;
    PATHSTR current_menu;
} uiWowState_t;

static uiWowState_t wow_ui;

static char const wow_default_hud_lua[] =
"local W = ow3\n"
"local VW, VH = 1024, 768\n"
"local function px(x, y, w, h) return x / VW, y / VH, w / VW, h / VH end\n"
"local function image(path, x, y, w, h) W.draw_image(path, px(x, y, w, h)) end\n"
"local function image_uv(path, x, y, w, h, l, r, t, b) W.draw_image_uv(path, x / VW, y / VH, w / VW, h / VH, l, r, t, b) end\n"
"local function icon(index, x, y, w, h) if index and index > 0 then W.draw_image_index(index, px(x, y, w, h)) end end\n"
"local function bar(x, y, w, h, value, maxvalue, r, g, b)\n"
"    W.draw_color(x / VW, y / VH, w / VW, h / VH, 12, 10, 8, 220)\n"
"    local p = maxvalue > 0 and value / maxvalue or 0\n"
"    if p < 0 then p = 0 elseif p > 1 then p = 1 end\n"
"    W.draw_color((x + 2) / VW, (y + 2) / VH, ((w - 4) * p) / VW, (h - 4) / VH, r, g, b, 235)\n"
"end\n"
"local function text(text, x, y, w, h, size, r, g, b, a, align)\n"
"    W.draw_text(text or '', x / VW, y / VH, w / VW, h / VH, size, r, g, b, a or 255, align or 'left')\n"
"end\n"
"local function action_button(action, x, y)\n"
"    image('Interface\\\\Buttons\\\\UI-Quickslot2.blp', x - 14, y - 13, 64, 64)\n"
"    icon(action and action.image, x + 2, y + 2, 32, 32)\n"
"    if action and action.count and action.count > 1 then text(tostring(action.count), x + 2, y + 23, 32, 10, 10, 255, 255, 255, 255, 'right') end\n"
"end\n"
"function ow3_draw()\n"
"    local p = W.player()\n"
"    W.draw_image_uv('Interface\\\\TargetingFrame\\\\UI-TargetingFrame.blp', -19 / VW, 4 / VH, 232 / VW, 100 / VH, 1.0, 0.09375, 0, 0.78125, 96, 92, 84, 230)\n"
"    W.draw_color(87 / VW, 22 / VH, 119 / VW, 41 / VH, 0, 0, 0, 128)\n"
"    text(p.name, 72, 18, 100, 12, 13, 255, 215, 120, 255, 'center')\n"
"    text('Lvl ' .. p.level, 24, 58, 42, 12, 11, 235, 225, 190, 255, 'center')\n"
"    bar(105, 41, 119, 12, p.health, p.healthMax, 20, 178, 48)\n"
"    bar(105, 54, 119, 11, p.power, p.powerMax, 26, 82, 210)\n"
"    image_uv('Interface\\\\MainMenuBar\\\\UI-MainMenuBar-Dwarf.blp', 0, 715, 256, 53, 0, 1.0, 0.79296875, 1.0)\n"
"    image_uv('Interface\\\\MainMenuBar\\\\UI-MainMenuBar-Dwarf.blp', 256, 715, 256, 53, 0, 1.0, 0.54296875, 0.75)\n"
"    image_uv('Interface\\\\MainMenuBar\\\\UI-MainMenuBar-Dwarf.blp', 512, 715, 256, 53, 0, 1.0, 0.29296875, 0.5)\n"
"    image_uv('Interface\\\\MainMenuBar\\\\UI-MainMenuBar-Dwarf.blp', 768, 715, 256, 53, 0, 1.0, 0.04296875, 0.25)\n"
"    image('Interface\\\\MainMenuBar\\\\UI-MainMenuBar-EndCap-Dwarf.blp', -96, 640, 128, 128)\n"
"    image_uv('Interface\\\\MainMenuBar\\\\UI-MainMenuBar-EndCap-Dwarf.blp', 992, 640, 128, 128, 1.0, 0.0, 0.0, 1.0)\n"
"    local actions = W.actions()\n"
"    for i = 0, 11 do\n"
"        action_button(actions[i + 1], 8 + i * 42, 728)\n"
"    end\n"
"    for i = 0, 3 do\n"
"        action_button(nil, 939 - i * 42, 728)\n"
"    end\n"
"    image('Interface\\\\Buttons\\\\Button-Backpack-Up.blp', 981, 729, 37, 37)\n"
"    image('Interface\\\\Minimap\\\\UI-Minimap-Border.blp', 879, 8, 128, 128)\n"
"    W.draw_minimap(896 / VW, 25 / VH, 91 / VW, 91 / VH)\n"
"    image('Interface\\\\QuestFrame\\\\UI-QuestLog-BookIcon.blp', 840, 162, 32, 32)\n"
"    text('Quests', 876, 164, 110, 20, 13, 255, 215, 120, 255)\n"
"    text('Copper ' .. p.copper, 816, 704, 150, 20, 12, 255, 210, 100, 255, 'right')\n"
"end\n";

static LPCSTR UIWow_DefaultLoadingBackground(void) {
    return "Interface\\Glues\\LoadingScreens\\LoadScreenEnviroment.blp";
}

static void UIWow_Printf(LPCSTR fmt, ...) {
    va_list args;
    char text[1024];

    if (!uiimport.Printf) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    uiimport.Printf("%s", text);
}

static void UIWow_EnsureRenderer(void) {
    if (!wow_ui.renderer && uiimport.GetRenderer) {
        wow_ui.renderer = uiimport.GetRenderer();
    }
}

static LPTEXTURE UIWow_LoadTexture(LPCSTR name) {
    int empty_slot = -1;

    if (!name || !*name) {
        return NULL;
    }
    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return NULL;
    }
    FOR_LOOP(i, WOW_UI_MAX_TEXTURES) {
        uiWowTexture_t *entry = &wow_ui.textures[i];

        if (entry->texture && !strcmp(entry->name, name)) {
            return entry->texture;
        }
        if (empty_slot < 0 && !entry->texture) {
            empty_slot = i;
        }
    }
    if (empty_slot >= 0) {
        uiWowTexture_t *entry = &wow_ui.textures[empty_slot];

        snprintf(entry->name, sizeof(entry->name), "%s", name);
        entry->texture = wow_ui.renderer->LoadTexture(name);
        return entry->texture;
    }

    {
        uiWowTexture_t *entry = &wow_ui.textures[wow_ui.texture_recycle_index % WOW_UI_MAX_TEXTURES];

        wow_ui.texture_recycle_index = (wow_ui.texture_recycle_index + 1) % WOW_UI_MAX_TEXTURES;
        SAFE_DELETE(entry->texture, wow_ui.renderer->ReleaseTexture);
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        entry->texture = wow_ui.renderer->LoadTexture(name);
        return entry->texture;
    }
}

static LPCFONT UIWow_LoadFont(DWORD size) {
    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return NULL;
    }
    FOR_LOOP(i, WOW_UI_MAX_FONTS) {
        uiWowFont_t *entry = &wow_ui.fonts[i];

        if (entry->font && entry->size == size) {
            return entry->font;
        }
        if (!entry->font) {
            entry->size = size;
            entry->font = wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", size);
            return entry->font;
        }
    }
    return wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", size);
}

static void UIWow_LoadStaticAssets(void) {
    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return;
    }

    if (!wow_ui.bar_background) {
        wow_ui.bar_background = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarBackground.blp");
    }
    if (!wow_ui.bar_border) {
        wow_ui.bar_border = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarBorder.blp");
    }
    if (!wow_ui.bar_fill) {
        wow_ui.bar_fill = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarFill.blp");
    }
    if (!wow_ui.bar_glass) {
        wow_ui.bar_glass = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarGlass.blp");
    }
    if (!wow_ui.bar_glow) {
        wow_ui.bar_glow = wow_ui.renderer->LoadTexture("Interface\\Glues\\LoadingBar\\Loading-BarGlow.blp");
    }
    if (!wow_ui.font_title) {
        wow_ui.font_title = wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", 22);
    }
    if (!wow_ui.font_status) {
        wow_ui.font_status = wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", 16);
    }
}

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
                UIWow_LuaColorByte(L, first, fallback.r),
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
    FLOAT left = (FLOAT)luaL_checknumber(L, 6);
    FLOAT right = (FLOAT)luaL_checknumber(L, 7);
    FLOAT top = (FLOAT)luaL_checknumber(L, 8);
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
    UIWow_LuaSetPlayerField(L, "health", WOW_STAT_HEALTH);
    UIWow_LuaSetPlayerField(L, "healthMax", WOW_STAT_HEALTH_MAX);
    UIWow_LuaSetPlayerField(L, "power", WOW_STAT_POWER);
    UIWow_LuaSetPlayerField(L, "powerMax", WOW_STAT_POWER_MAX);
    UIWow_LuaSetPlayerField(L, "level", WOW_STAT_LEVEL);
    UIWow_LuaSetPlayerField(L, "xp", WOW_STAT_XP);
    UIWow_LuaSetPlayerField(L, "xpMax", WOW_STAT_XP_MAX);
    UIWow_LuaSetPlayerField(L, "copper", WOW_STAT_COPPER);
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
    { "draw_image", UIWow_LuaDrawImage },
    { "draw_image_uv", UIWow_LuaDrawImageUV },
    { "draw_image_index", UIWow_LuaDrawImageIndex },
    { "draw_color", UIWow_LuaDrawColor },
    { "draw_minimap", UIWow_LuaDrawMinimap },
    { "draw_text", UIWow_LuaDrawText },
    { "stat", UIWow_LuaStat },
    { "text", UIWow_LuaText },
    { "player_name", UIWow_LuaPlayerName },
    { "player", UIWow_LuaPlayer },
    { "inventory", UIWow_LuaInventory },
    { "actions", UIWow_LuaActions },
    { "time", UIWow_LuaTime },
    { "command", UIWow_LuaCommand },
    { "load_map", UIWow_LuaLoadMap },
    { NULL, NULL },
};

static int UIWow_LuaTraceback(lua_State *L) {
    LPCSTR message = lua_tostring(L, 1);

    luaL_traceback(L, L, message ? message : "Lua error", 1);
    return 1;
}

static BOOL UIWow_LuaPCall(int nargs) {
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

static void UIWow_LoadAllMenuLua(void) {
    UIWow_LoadMenuLua("OW3Glue.lua");
    UIWow_LoadMenuLua("LoginScreen.lua");
    UIWow_LoadMenuLua("CharacterSelectScreen.lua");
    UIWow_LoadMenuLua("CharacterCreateScreen.lua");
}

static void UIWow_OpenLuaLib(lua_State *L, LPCSTR name, lua_CFunction openf) {
    luaL_requiref(L, name, openf, 1);
    lua_pop(L, 1);
}

static void UIWow_InitLua(void) {
    lua_State *L;

    wow_ui.lua = luaL_newstate();
    if (!wow_ui.lua) {
        UIWow_Printf("UIWow: luaL_newstate failed\n");
        return;
    }
    L = wow_ui.lua;
    UIWow_OpenLuaLib(L, LUA_GNAME, luaopen_base);
    UIWow_OpenLuaLib(L, LUA_TABLIBNAME, luaopen_table);
    UIWow_OpenLuaLib(L, LUA_STRLIBNAME, luaopen_string);
    UIWow_OpenLuaLib(L, LUA_MATHLIBNAME, luaopen_math);

    lua_newtable(L);
    luaL_setfuncs(L, wow_lua_funcs, 0);
    UIWow_LuaSetInteger(L, "STAT_HEALTH", WOW_STAT_HEALTH);
    UIWow_LuaSetInteger(L, "STAT_HEALTH_MAX", WOW_STAT_HEALTH_MAX);
    UIWow_LuaSetInteger(L, "STAT_POWER", WOW_STAT_POWER);
    UIWow_LuaSetInteger(L, "STAT_POWER_MAX", WOW_STAT_POWER_MAX);
    UIWow_LuaSetInteger(L, "STAT_LEVEL", WOW_STAT_LEVEL);
    UIWow_LuaSetInteger(L, "STAT_XP", WOW_STAT_XP);
    UIWow_LuaSetInteger(L, "STAT_XP_MAX", WOW_STAT_XP_MAX);
    UIWow_LuaSetInteger(L, "STAT_COPPER", WOW_STAT_COPPER);
    lua_setglobal(L, "ow3");

    UIWow_RunLuaBuffer("@default_wow_hud.lua",
                       wow_default_hud_lua,
                       sizeof(wow_default_hud_lua) - 1);
    UIWow_LoadAllMenuLua();
}

static void UIWow_UpdateMapBackground(LPCPLAYER ps) {
    LPCSTR map_path;
    LPCSTR screen_path;

    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return;
    }

    map_path = uiimport.GetLoadingMap ? uiimport.GetLoadingMap() : "";
    if (!map_path) {
        map_path = "";
    }
    screen_path = (ps && ps->texts[PLAYERTEXT_MAP_PREVIEW] && *ps->texts[PLAYERTEXT_MAP_PREVIEW])
        ? ps->texts[PLAYERTEXT_MAP_PREVIEW]
        : UIWow_DefaultLoadingBackground();

    if (!strcmp(wow_ui.active_map, map_path)) {
        return;
    }

    snprintf(wow_ui.active_map, sizeof(wow_ui.active_map), "%s", map_path);

    SAFE_DELETE(wow_ui.background, wow_ui.renderer->ReleaseTexture);
    wow_ui.background = wow_ui.renderer->LoadTexture(screen_path);
    if (!wow_ui.background) {
        wow_ui.background = wow_ui.renderer->LoadTexture(UIWow_DefaultLoadingBackground());
    }

    UIWow_Printf("UIWow: loading screen map=%s background=%s\n",
                 wow_ui.active_map[0] ? wow_ui.active_map : "<none>",
                 screen_path);
}

static FLOAT UIWow_GetLoadingProgress(void) {
    FLOAT target = uiimport.GetLoadingProgress ? uiimport.GetLoadingProgress() : 0.0f;

    if (target < 0.0f) {
        target = 0.0f;
    } else if (target > 1.0f) {
        target = 1.0f;
    }

    if (target < wow_ui.displayed_progress) {
        wow_ui.displayed_progress = target;
    } else {
        wow_ui.displayed_progress = wow_ui.displayed_progress * 0.82f + target * 0.18f;
        if (target - wow_ui.displayed_progress < 0.002f) {
            wow_ui.displayed_progress = target;
        }
    }
    return wow_ui.displayed_progress;
}

static void UIWow_DrawLoadingScreen(void) {
    RECT full = MAKE(RECT, 0, 0, 1, 1);
    RECT uv = MAKE(RECT, 0, 0, 1, 1);
    RECT bar = MAKE(RECT, 0.305f, 0.865f, 0.39f, 0.032f);
    RECT bar_border = MAKE(RECT, 0.285f, 0.848f, 0.43f, 0.067f);
    RECT bar_fill = bar;
    RECT title_rect = MAKE(RECT, 0.16f, 0.77f, 0.68f, 0.05f);
    RECT status_rect = MAKE(RECT, 0.16f, 0.818f, 0.68f, 0.04f);
    FLOAT progress = UIWow_GetLoadingProgress();
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;
    LPCSTR map_path = uiimport.GetLoadingMap ? uiimport.GetLoadingMap() : "";
    LPCSTR map_title = ps ? ps->texts[PLAYERTEXT_MAP_TITLE] : NULL;
    LPCSTR status = uiimport.GetLoadingStatus ? uiimport.GetLoadingStatus() : "";

    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return;
    }

    if (wow_ui.background) {
        wow_ui.renderer->DrawImage(wow_ui.background, &full, &uv, COLOR32_WHITE);
    }

    if (wow_ui.bar_background) {
        wow_ui.renderer->DrawImage(wow_ui.bar_background, &bar, &uv, COLOR32_WHITE);
    }
    if (wow_ui.bar_fill) {
        bar_fill.w *= progress;
        if (bar_fill.w > 0.0f) {
            RECT fill_uv = uv;
            fill_uv.w = progress;
            wow_ui.renderer->DrawImage(wow_ui.bar_fill, &bar_fill, &fill_uv, COLOR32_WHITE);
        }
    }
    if (wow_ui.bar_glow) {
        wow_ui.renderer->DrawImageEx(&MAKE(drawImage_t,
                                           .texture = wow_ui.bar_glow,
                                           .screen = bar_border,
                                           .uv = uv,
                                           .color = MAKE(COLOR32, 255, 255, 255, 200),
                                           .shader = SHADER_UI,
                                           .alphamode = BLEND_MODE_ADD));
    }
    if (wow_ui.bar_glass) {
        wow_ui.renderer->DrawImage(wow_ui.bar_glass, &bar_border, &uv, COLOR32_WHITE);
    }
    if (wow_ui.bar_border) {
        wow_ui.renderer->DrawImage(wow_ui.bar_border, &bar_border, &uv, COLOR32_WHITE);
    }

    if (!map_title || !*map_title) {
        map_title = map_path;
    }

    if (wow_ui.font_title && map_title && *map_title) {
        wow_ui.renderer->DrawText(&MAKE(drawText_t,
                                        .font = wow_ui.font_title,
                                        .text = map_title,
                                        .rect = title_rect,
                                        .color = MAKE(COLOR32, 235, 210, 160, 255),
                                        .textWidth = title_rect.w,
                                        .lineHeight = title_rect.h,
                                        .wordWrap = false,
                                        .halign = FONT_JUSTIFYCENTER,
                                        .valign = FONT_JUSTIFYMIDDLE));
    }
    if (wow_ui.font_status && status && *status) {
        wow_ui.renderer->DrawText(&MAKE(drawText_t,
                                        .font = wow_ui.font_status,
                                        .text = status,
                                        .rect = status_rect,
                                        .color = MAKE(COLOR32, 220, 220, 220, 255),
                                        .textWidth = status_rect.w,
                                        .lineHeight = status_rect.h,
                                        .wordWrap = false,
                                        .halign = FONT_JUSTIFYCENTER,
                                        .valign = FONT_JUSTIFYMIDDLE));
    }
}

static void UIWow_CallLuaDraw(void) {
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

static void UIWow_CallLuaUpdate(DWORD msec) {
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

static void UIWow_Init(void) {
    memset(&wow_ui, 0, sizeof(wow_ui));
    UIWow_EnsureRenderer();
    UIWow_InitLua();
}

static void UIWow_Shutdown(void) {
    if (wow_ui.lua) {
        lua_close(wow_ui.lua);
        wow_ui.lua = NULL;
    }
    if (wow_ui.renderer) {
        FOR_LOOP(i, WOW_UI_MAX_TEXTURES) {
            SAFE_DELETE(wow_ui.textures[i].texture, wow_ui.renderer->ReleaseTexture);
        }
        SAFE_DELETE(wow_ui.background, wow_ui.renderer->ReleaseTexture);
        SAFE_DELETE(wow_ui.bar_background, wow_ui.renderer->ReleaseTexture);
        SAFE_DELETE(wow_ui.bar_border, wow_ui.renderer->ReleaseTexture);
        SAFE_DELETE(wow_ui.bar_fill, wow_ui.renderer->ReleaseTexture);
        SAFE_DELETE(wow_ui.bar_glass, wow_ui.renderer->ReleaseTexture);
        SAFE_DELETE(wow_ui.bar_glow, wow_ui.renderer->ReleaseTexture);
    }
    memset(&wow_ui, 0, sizeof(wow_ui));
}

static void UIWow_Refresh(DWORD msec) {
    UIWow_CallLuaUpdate(msec);
}

static void UIWow_UpdateMouseHover(void) {
    static int last_x = -1, last_y = -1;
    DWORD mouse_button = uiimport.GetMouseButton ? uiimport.GetMouseButton() : 0;
    VECTOR2 mouse_pos = uiimport.GetMouseFdf ? uiimport.GetMouseFdf() : MAKE(VECTOR2, 0, 0);
    int mouse_x = (int)(mouse_pos.x * 1024.0f);
    int mouse_y = (int)(mouse_pos.y * 768.0f);

    if (mouse_x != last_x || mouse_y != last_y) {
        last_x = mouse_x;
        last_y = mouse_y;
        if (wow_ui.lua) {
            lua_getglobal(wow_ui.lua, "ow3_handle_mouse_move");
            if (lua_isfunction(wow_ui.lua, -1)) {
                lua_pushnumber(wow_ui.lua, mouse_pos.x);
                lua_pushnumber(wow_ui.lua, mouse_pos.y);
                UIWow_LuaPCall(2);
            } else {
                lua_pop(wow_ui.lua, 1);
            }
        }
    }
}

static void UIWow_DrawFrame(void) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    if (!ps) {
        return;
    }

    UIWow_EnsureRenderer();
    UIWow_UpdateMouseHover();

    if (ps->client_ui_state == CLIENT_UI_LOADING) {
        UIWow_LoadStaticAssets();
        UIWow_UpdateMapBackground(ps);
        UIWow_DrawLoadingScreen();
        return;
    }
    if (wow_ui.current_menu[0]) {
        UIWow_CallLuaDraw();
        return;
    }
    if (ps->client_ui_state == CLIENT_UI_GAME) {
        UIWow_CallLuaDraw();
    }
}

static void UIWow_KeyEvent(int key, BOOL down, DWORD time) {
    (void)key;
    (void)down;
    (void)time;
}

static void UIWow_TextInput(LPCSTR text) {
    if (!wow_ui.lua || !text) {
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_text_input");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        return;
    }
    lua_pushstring(wow_ui.lua, text);
    UIWow_LuaPCall(1);
}

static void UIWow_MouseEvent(int x, int y, int button, BOOL down) {
    if (!wow_ui.lua || !down) {
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_mouse_click");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        return;
    }
    lua_pushnumber(wow_ui.lua, x / 1024.0f);
    lua_pushnumber(wow_ui.lua, y / 768.0f);
    lua_pushinteger(wow_ui.lua, button);
    UIWow_LuaPCall(3);
}

static void UIWow_ShowLoginMenu(void) {
    snprintf(wow_ui.current_menu, sizeof(wow_ui.current_menu), "login");
    if (wow_ui.lua) {
        lua_getglobal(wow_ui.lua, "ow3_show_login");
        if (lua_isfunction(wow_ui.lua, -1)) {
            UIWow_LuaPCall(0);
        } else {
            lua_pop(wow_ui.lua, 1);
        }
    }
}

static void UIWow_ShowCharacterSelectMenu(void) {
    snprintf(wow_ui.current_menu, sizeof(wow_ui.current_menu), "character_select");
    if (wow_ui.lua) {
        lua_getglobal(wow_ui.lua, "ow3_show_character_select");
        if (lua_isfunction(wow_ui.lua, -1)) {
            UIWow_LuaPCall(0);
        } else {
            lua_pop(wow_ui.lua, 1);
        }
    }
}

static void UIWow_ShowCharacterCreateMenu(void) {
    snprintf(wow_ui.current_menu, sizeof(wow_ui.current_menu), "character_create");
    if (wow_ui.lua) {
        lua_getglobal(wow_ui.lua, "ow3_show_character_create");
        if (lua_isfunction(wow_ui.lua, -1)) {
            UIWow_LuaPCall(0);
        } else {
            lua_pop(wow_ui.lua, 1);
        }
    }
}

typedef struct {
    LPCSTR command;
    void (*function)(void);
} uiWowMenuCommandDef_t;

static uiWowMenuCommandDef_t const uiWow_menu_command_defs[] = {
    { "menu_login", UIWow_ShowLoginMenu },
    { "menu_character_select", UIWow_ShowCharacterSelectMenu },
    { "menu_character_create", UIWow_ShowCharacterCreateMenu },
    { NULL, NULL },
};

static void UIWow_MenuCommand(LPCSTR route) {
    if (!route || !*route) {
        return;
    }
    FOR_LOOP(i, sizeof(uiWow_menu_command_defs) / sizeof(uiWow_menu_command_defs[0])) {
        uiWowMenuCommandDef_t const *cmd = &uiWow_menu_command_defs[i];
        if (!cmd->command) {
            break;
        }
        if (!strcmp(cmd->command, route)) {
            if (cmd->function) {
                cmd->function();
            }
            return;
        }
    }
    UIWow_Printf("UIWow_MenuCommand: unknown route '%s'\n", route);
}

static DWORD UIWow_ImageIndex(LPCSTR art) {
    if (!art || !*art || !uiimport.ImageIndex) {
        return 0;
    }
    return (DWORD)uiimport.ImageIndex(art);
}

static DWORD UIWow_ParseCount(LPCSTR text) {
    DWORD count;

    if (!text || !*text) {
        return 0;
    }
    count = (DWORD)strtoul(text, NULL, 10);
    return count;
}

static void UIWow_UpdateUnitUI(DWORD num_units, uiUnitData_t *units) {
    uiUnitData_t *unit;

    memset(wow_ui.inventory, 0, sizeof(wow_ui.inventory));
    memset(wow_ui.actions, 0, sizeof(wow_ui.actions));
    if (num_units == 0 || !units) {
        return;
    }
    unit = &units[0];
    FOR_LOOP(i, MIN(unit->num_buttons, WOW_UI_ACTION_SLOTS)) {
        uiCommandButton_t const *button = &unit->buttons[i];
        uiWowIcon_t *icon = &wow_ui.actions[i];

        icon->image = UIWow_ImageIndex(button->art);
        icon->count = UIWow_ParseCount(button->ubertip);
        icon->slot = i;
        snprintf(icon->name, sizeof(icon->name), "%s", button->tooltip);
    }
    FOR_LOOP(i, MIN(unit->num_inventory, WOW_UI_INVENTORY_SLOTS)) {
        uiInventoryItem_t const *item = &unit->inventory[i];
        DWORD slot = item->slot < WOW_UI_INVENTORY_SLOTS ? item->slot : i;
        uiWowIcon_t *icon = &wow_ui.inventory[slot];

        icon->image = UIWow_ImageIndex(item->art);
        icon->count = UIWow_ParseCount(item->ubertip);
        icon->slot = slot;
        snprintf(icon->name, sizeof(icon->name), "%s", item->tooltip);
    }
}

static void UIWow_SetLayoutLayer(DWORD layer, HANDLE data) {
    (void)layer;
    (void)data;
}

static void UIWow_ClearLayoutLayer(DWORD layer) {
    (void)layer;
}

static BOOL UIWow_HitTestLayout(int x, int y) {
    (void)x;
    (void)y;
    return false;
}

uiExport_t UI_GetAPI(uiImport_t import) {
    uiimport = import;
    (void)uiimport;

    return (uiExport_t) {
        .Init = UIWow_Init,
        .Shutdown = UIWow_Shutdown,
        .Refresh = UIWow_Refresh,
        .DrawFrame = UIWow_DrawFrame,
        .KeyEvent = UIWow_KeyEvent,
        .TextInput = UIWow_TextInput,
        .MouseEvent = UIWow_MouseEvent,
        .MenuCommand = UIWow_MenuCommand,
        .UpdateUnitUI = UIWow_UpdateUnitUI,
        .SetLayoutLayer = UIWow_SetLayoutLayer,
        .ClearLayoutLayer = UIWow_ClearLayoutLayer,
        .HitTestLayout = UIWow_HitTestLayout,
    };
}
