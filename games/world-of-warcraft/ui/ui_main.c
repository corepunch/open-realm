/*
 * ui_main.c — WoW UI library entry point and lifecycle management.
 *
 * Owns the global state definitions, shared asset helpers, per-frame
 * dispatch, input routing, glue-menu commands, unit icon sync, and the
 * UI_GetAPI entry point.  Rendering detail lives in ui_loading.c;
 * Lua VM and bindings live in ui_lua.c.
 */
#include "ui_local.h"

#include <stdarg.h>

/* -------------------------------------------------------------------------
 * Global state (declared extern in ui_local.h)
 * ---------------------------------------------------------------------- */

uiImport_t uiimport;
uiWowState_t wow_ui;

static BOOL uiWow_menu_commands_registered;

/* -------------------------------------------------------------------------
 * Shared helpers used by ui_lua.c and ui_loading.c
 * ---------------------------------------------------------------------- */

void UIWow_Printf(LPCSTR fmt, ...) {
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

void UIWow_WarnOnce(DWORD flag, LPCSTR fmt, ...) {
    va_list args;
    char text[1024];

    if (wow_ui.warn_once_mask & flag) {
        return;
    }
    wow_ui.warn_once_mask |= flag;
    if (!uiimport.Printf) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    uiimport.Printf("%s", text);
}

void UIWow_EnsureRenderer(void) {
    if (!wow_ui.renderer && uiimport.GetRenderer) {
        wow_ui.renderer = uiimport.GetRenderer();
    }
    if (!wow_ui.renderer) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_RENDERER,
                       "UIWow: renderer is unavailable (GetRenderer returned NULL)\n");
    }
}

static BOOL UIWow_TexturePathHasExt(LPCSTR name) {
    LPCSTR slash;
    if (!name || !*name) return false;
    slash = strrchr(name, '\\');
    if (!slash) slash = strrchr(name, '/');
    return strchr(slash ? slash + 1 : name, '.') != NULL;
}

static BOOL UIWow_MainHasArchiveFile(LPCSTR path) {
    void *buf = NULL;
    int size;
    if (!path || !*path || !uiimport.FS_ReadFile || !uiimport.FS_FreeFile) return false;
    size = uiimport.FS_ReadFile(path, &buf);
    if (size > 0 && buf) { uiimport.FS_FreeFile(buf); return true; }
    SAFE_DELETE(buf, uiimport.FS_FreeFile);
    return false;
}

static void UIWow_ResolveTexturePath(LPCSTR in, LPSTR out, size_t out_size) {
    static LPCSTR exts[] = { ".blp", ".tga", ".dds", NULL };
    static LPCSTR splash_prefix = "Interface\\Glues\\Common\\Glues-Splash-";
    static LPCSTR splash_fallback = "Interface\\Glues\\Common\\Glues-Logo.blp";
    static LPCSTR disabled_down_fallback = "Interface\\Glues\\Common\\Glue-Panel-Button-Disabled.blp";
    PATHSTR candidate;
    snprintf(out, out_size, "%s", in ? in : "");
    if (!in || !*in || UIWow_TexturePathHasExt(in)) return;
    if (UIWow_MainHasArchiveFile(in)) return;
    FOR_LOOP(i, sizeof(exts) / sizeof(exts[0])) {
        if (!exts[i]) break;
        snprintf(candidate, sizeof(candidate), "%s%s", in, exts[i]);
        if (UIWow_MainHasArchiveFile(candidate)) { snprintf(out, out_size, "%s", candidate); return; }
    }
    if (!strncasecmp(in, splash_prefix, strlen(splash_prefix)) && UIWow_MainHasArchiveFile(splash_fallback)) {
        snprintf(out, out_size, "%s", splash_fallback);
        return;
    }
    if (!strcasecmp(in, "Interface\\Glues\\Common\\Glue-Panel-Button-Disabled-Down") && UIWow_MainHasArchiveFile(disabled_down_fallback)) {
        snprintf(out, out_size, "%s", disabled_down_fallback);
        return;
    }
}

LPTEXTURE UIWow_LoadTexture(LPCSTR name) {
    int empty_slot = -1;
    PATHSTR resolved;

    if (!name || !*name) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_LOAD_BACKGROUND,
                       "UIWow: attempted to load texture with empty name\n");
        return NULL;
    }
    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return NULL;
    }
    /* Fast path: input name already cached — skip MPQ resolution entirely. */
    FOR_LOOP(i, WOW_UI_MAX_TEXTURES) {
        uiWowTexture_t *entry = &wow_ui.textures[i];

        if (entry->input_name[0] && !strcasecmp(entry->input_name, name)) {
            return entry->texture;
        }
        if (empty_slot < 0 && !entry->input_name[0]) {
            empty_slot = i;
        }
    }
    /* Slow path: resolve once (MPQ probe), then store both names in the slot. */
    UIWow_ResolveTexturePath(name, resolved, sizeof(resolved));
    if (empty_slot >= 0) {
        uiWowTexture_t *entry = &wow_ui.textures[empty_slot];

        snprintf(entry->input_name, sizeof(entry->input_name), "%s", name);
        snprintf(entry->name, sizeof(entry->name), "%s", resolved);
        entry->texture = wow_ui.renderer->LoadTexture(resolved);
        if (!entry->texture) {
            UIWow_Printf("UIWow: renderer failed to load texture '%s' (from '%s')\n", resolved, name);
        }
        return entry->texture;
    }

    {
        uiWowTexture_t *entry = &wow_ui.textures[wow_ui.texture_recycle_index % WOW_UI_MAX_TEXTURES];

        wow_ui.texture_recycle_index = (wow_ui.texture_recycle_index + 1) % WOW_UI_MAX_TEXTURES;
        SAFE_DELETE(entry->texture, wow_ui.renderer->ReleaseTexture);
        snprintf(entry->input_name, sizeof(entry->input_name), "%s", name);
        snprintf(entry->name, sizeof(entry->name), "%s", resolved);
        entry->texture = wow_ui.renderer->LoadTexture(resolved);
        if (!entry->texture) {
            UIWow_Printf("UIWow: renderer failed to load texture '%s' (from '%s')\n", resolved, name);
        }
        return entry->texture;
    }
}

LPCFONT UIWow_LoadFont(DWORD size) {
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
            if (!entry->font) {
                UIWow_Printf("UIWow: renderer failed to load font '%s' size=%u\n",
                             "Fonts\\FRIZQT__.TTF", size);
            }
            return entry->font;
        }
    }
    {
        LPCFONT font = wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", size);
        if (!font) {
            UIWow_Printf("UIWow: renderer failed to load font '%s' size=%u\n",
                         "Fonts\\FRIZQT__.TTF", size);
        }
        return font;
    }
}

static void UIWow_RegisterMenuCommands(void);

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

static void UIWow_Init(void) {
    memset(&wow_ui, 0, sizeof(wow_ui));
    uiWow_menu_commands_registered = false;
    UIWow_RegisterMenuCommands();
    UIWow_EnsureRenderer();
    UIWow_InitLua();
}

static void UIWow_Shutdown(void) {
    UIWow_ShutdownLua();
    if (wow_ui.renderer) {
        FOR_LOOP(i, WOW_UI_MAX_TEXTURES) {
            SAFE_DELETE(wow_ui.textures[i].texture, wow_ui.renderer->ReleaseTexture);
        }
        SAFE_DELETE(wow_ui.background, wow_ui.renderer->ReleaseTexture);
    }
    memset(&wow_ui, 0, sizeof(wow_ui));
}

static void UIWow_Refresh(DWORD msec) {
    UIWow_CallLuaUpdate(msec);
}

static void UIWow_ReleaseScreenAssets(void) {
    if (!wow_ui.renderer) {
        return;
    }

    FOR_LOOP(i, WOW_UI_MAX_TEXTURES) {
        SAFE_DELETE(wow_ui.textures[i].texture, wow_ui.renderer->ReleaseTexture);
        wow_ui.textures[i].input_name[0] = '\0';
        wow_ui.textures[i].name[0] = '\0';
    }
    FOR_LOOP(i, WOW_UI_MAX_FONTS) {
        /* Renderer API does not expose font destruction; drop cached handles on screen switch. */
        wow_ui.fonts[i].font = NULL;
        wow_ui.fonts[i].size = 0;
    }
    SAFE_DELETE(wow_ui.background, wow_ui.renderer->ReleaseTexture);
    wow_ui.texture_recycle_index = 0;
}

static void UIWow_RecreateLuaStateForMenu(LPCSTR menu_name) {
    if (!menu_name || !*menu_name) {
        return;
    }
    if (wow_ui.lua && wow_ui.current_menu[0] && !strcmp(wow_ui.current_menu, menu_name)) {
        return;
    }

    if (wow_ui.lua) {
        UIWow_Printf("UIWow: switching menu '%s' -> '%s'; recreating Lua state\n",
                     wow_ui.current_menu[0] ? wow_ui.current_menu : "<none>",
                     menu_name);
        UIWow_ShutdownLua();
    } else {
        UIWow_Printf("UIWow: creating Lua state for menu '%s'\n", menu_name);
    }

    UIWow_ReleaseScreenAssets();
    UIWow_InitLua();
}

/* -------------------------------------------------------------------------
 * Per-frame draw dispatch
 * ---------------------------------------------------------------------- */

static void UIWow_UpdateMouseHover(void) {
    static int last_x = -1, last_y = -1;
    VECTOR2 mouse_pos = uiimport.GetMouseFdf ? uiimport.GetMouseFdf() : MAKE(VECTOR2, 0, 0);
    int mouse_x = (int)(mouse_pos.x * 1024.0f);
    int mouse_y = (int)(mouse_pos.y * 768.0f);

    if (mouse_x == last_x && mouse_y == last_y) {
        return;
    }
    last_x = mouse_x;
    last_y = mouse_y;
    if (!wow_ui.lua) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE,
                       "UIWow: Lua state is not initialized; mouse hover ignored\n");
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_mouse_move");
    if (lua_isfunction(wow_ui.lua, -1)) {
        lua_pushnumber(wow_ui.lua, mouse_pos.x);
        lua_pushnumber(wow_ui.lua, mouse_pos.y);
        UIWow_LuaPCall(2);
    } else {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_MOUSEMOVE_HANDLER,
                       "UIWow: missing Lua function 'ow3_handle_mouse_move'\n");
    }
}

static void UIWow_DrawFrame(void) {
    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    UIWow_EnsureRenderer();
    UIWow_UpdateMouseHover();

    if (wow_ui.current_menu[0]) {
        UIWow_XMLDraw();
        UIWow_CallLuaDraw();
        return;
    }
    if (ps && ps->client_ui_state == CLIENT_UI_LOADING) {
        UIWow_UpdateMapBackground(ps);
        lua_getglobal(wow_ui.lua, "ow3_draw_loading_screen");
        if (lua_isfunction(wow_ui.lua, -1)) {
            UIWow_LuaPCall(0);
        } else {
            lua_pop(wow_ui.lua, 1);
            UIWow_WarnOnce(WOW_UI_WARN_NO_LOADING_DRAW,
                           "UIWow: missing Lua function 'ow3_draw_loading_screen'\n");
        }
        return;
    }
    if (ps && ps->client_ui_state == CLIENT_UI_GAME) {
        UIWow_XMLDraw();
        UIWow_CallLuaDraw();
    }
}

/* -------------------------------------------------------------------------
 * Input routing
 * ---------------------------------------------------------------------- */

static void UIWow_KeyEvent(int key, BOOL down, DWORD time) {
    if (UIWow_XMLKeyEvent(key, down, time)) {
        return;
    }
}

static void UIWow_TextInput(LPCSTR text) {
    if (UIWow_XMLTextInput(text)) {
        return;
    }
    if (!wow_ui.lua || !text) {
        if (!wow_ui.lua) {
            UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE,
                           "UIWow: Lua state is not initialized; text input ignored\n");
        }
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_text_input");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_TEXT_HANDLER,
                       "UIWow: missing Lua function 'ow3_handle_text_input'\n");
        return;
    }
    lua_pushstring(wow_ui.lua, text);
    UIWow_LuaPCall(1);
}

static void UIWow_MouseEvent(int x, int y, int button, BOOL down) {
    if (UIWow_XMLMouseEvent(x, y, button, down)) {
        return;
    }
    if (!wow_ui.lua || !down) {
        if (!wow_ui.lua && down) {
            UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE,
                           "UIWow: Lua state is not initialized; mouse click ignored\n");
        }
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_mouse_click");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_MOUSE_HANDLER,
                       "UIWow: missing Lua function 'ow3_handle_mouse_click'\n");
        return;
    }
    lua_pushnumber(wow_ui.lua, x / 1024.0f);
    lua_pushnumber(wow_ui.lua, y / 768.0f);
    lua_pushinteger(wow_ui.lua, button);
    UIWow_LuaPCall(3);
}

/* -------------------------------------------------------------------------
 * Glue-menu commands
 * ---------------------------------------------------------------------- */

static void UIWow_CallLuaShow(LPCSTR menu_name, LPCSTR lua_func, LPCSTR glue_screen) {
    UIWow_RecreateLuaStateForMenu(menu_name);
    snprintf(wow_ui.current_menu, sizeof(wow_ui.current_menu), "%s", menu_name);
    if (!wow_ui.lua) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE,
                       "UIWow: Lua state is not initialized; menu command '%s' ignored\n",
                       menu_name ? menu_name : "<unknown>");
        return;
    }

    lua_getglobal(wow_ui.lua, lua_func);
    if (lua_isfunction(wow_ui.lua, -1)) {
        UIWow_LuaPCall(0);
        return;
    }
    lua_pop(wow_ui.lua, 1);

    if (!glue_screen || !*glue_screen) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_MENU_HANDLER,
                       "UIWow: missing Lua handler '%s' and no Glue fallback for menu '%s'\n",
                       lua_func ? lua_func : "<unknown>", menu_name ? menu_name : "<unknown>");
        return;
    }
    lua_getglobal(wow_ui.lua, "SetGlueScreen");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_SETGLUESCREEN,
                       "UIWow: missing Lua function 'SetGlueScreen' for menu '%s' fallback '%s'\n",
                       menu_name ? menu_name : "<unknown>", glue_screen);
        return;
    }
    lua_pushstring(wow_ui.lua, glue_screen);
    UIWow_LuaPCall(1);
}

static void UIWow_ShowLoginMenu(void)          { UIWow_CallLuaShow("login",            "ow3_show_login",            "login"); }
static void UIWow_ShowCharacterSelectMenu(void){ UIWow_CallLuaShow("character_select", "ow3_show_character_select", "charselect"); }
static void UIWow_ShowCharacterCreateMenu(void){ UIWow_CallLuaShow("character_create", "ow3_show_character_create", "charcreate"); }

typedef struct { LPCSTR command; void (*function)(void); } uiWowMenuCommandDef_t;

static uiWowMenuCommandDef_t const uiWow_menu_command_defs[] = {
    { "menu_login",            UIWow_ShowLoginMenu },
    { "menu_character_select", UIWow_ShowCharacterSelectMenu },
    { "menu_character_create", UIWow_ShowCharacterCreateMenu },
    { NULL, NULL },
};

static void UIWow_RegisterMenuCommands(void) {
    if (uiWow_menu_commands_registered || !uiimport.Cmd_AddCommand) {
        return;
    }
    for (uiWowMenuCommandDef_t const *cmd = uiWow_menu_command_defs; cmd->command; cmd++) {
        uiimport.Cmd_AddCommand(cmd->command, cmd->function);
    }
    uiWow_menu_commands_registered = true;
}

/* -------------------------------------------------------------------------
 * Unit UI (inventory / action bar icon sync)
 * ---------------------------------------------------------------------- */

static DWORD UIWow_ImageIndex(LPCSTR art) {
    if (!art || !*art || !uiimport.ImageIndex) {
        return 0;
    }
    return (DWORD)uiimport.ImageIndex(art);
}

static DWORD UIWow_ParseCount(LPCSTR text) {
    if (!text || !*text) {
        return 0;
    }
    return (DWORD)strtoul(text, NULL, 10);
}

static void UIWow_UpdateUnitUI(DWORD num_units, uiUnitData_t *units) {
    uiUnitData_t *unit;

    memset(wow_ui.inventory, 0, sizeof(wow_ui.inventory));
    memset(wow_ui.actions,   0, sizeof(wow_ui.actions));
    if (num_units == 0 || !units) {
        return;
    }
    unit = &units[0];
    FOR_LOOP(i, MIN(unit->num_buttons, WOW_UI_ACTION_SLOTS)) {
        uiCommandButton_t const *button = &unit->buttons[i];
        uiWowIcon_t *icon = &wow_ui.actions[i];

        icon->image = UIWow_ImageIndex(button->art);
        icon->count = UIWow_ParseCount(button->ubertip);
        icon->slot  = i;
        snprintf(icon->name, sizeof(icon->name), "%s", button->tooltip);
    }
    FOR_LOOP(i, MIN(unit->num_inventory, WOW_UI_INVENTORY_SLOTS)) {
        uiInventoryItem_t const *item = &unit->inventory[i];
        DWORD slot = item->slot < WOW_UI_INVENTORY_SLOTS ? item->slot : i;
        uiWowIcon_t *icon = &wow_ui.inventory[slot];

        icon->image = UIWow_ImageIndex(item->art);
        icon->count = UIWow_ParseCount(item->ubertip);
        icon->slot  = slot;
        snprintf(icon->name, sizeof(icon->name), "%s", item->tooltip);
    }
}

/* -------------------------------------------------------------------------
 * Layout layer stubs (not used by WoW UI)
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

uiExport_t UI_GetAPI(uiImport_t import) {
    uiimport = import;

    return (uiExport_t) {
        .Init             = UIWow_Init,
        .Shutdown         = UIWow_Shutdown,
        .Refresh          = UIWow_Refresh,
        .DrawFrame        = UIWow_DrawFrame,
        .KeyEvent         = UIWow_KeyEvent,
        .TextInput        = UIWow_TextInput,
        .MouseEvent       = UIWow_MouseEvent,
        .UpdateUnitUI     = UIWow_UpdateUnitUI,
        .SetLayoutLayer   = UIWow_SetLayoutLayer,
        .ClearLayoutLayer = UIWow_ClearLayoutLayer,
        .HitTestLayout    = UIWow_HitTestLayout,
    };
}
