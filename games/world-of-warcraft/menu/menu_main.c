/*
 * menu_main.c — WoW UI library entry point and lifecycle management.
 *
 * Owns the global state definitions, shared asset helpers, per-frame
 * dispatch, input routing, glue-menu commands, unit icon sync, and the
 * M_GetAPI entry point.  Rendering detail lives in menu_loading.c;
 * Lua VM and bindings live in menu_lua.c.
 */
#include "menu_local.h"

#include <stdarg.h>

/* -------------------------------------------------------------------------
 * Global state (declared extern in menu_local.h)
 * ---------------------------------------------------------------------- */

menuImport_t mi;


uiWowState_t wow_ui;

static bool uiWow_menu_commands_registered;


/* -------------------------------------------------------------------------
 * Shared helpers used by menu_lua.c and menu_loading.c
 * ---------------------------------------------------------------------- */

void UIWow_Printf(cstring_t fmt, ...) {
    va_list args;
    char text[1024];

    if (!mi.Printf) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    mi.Printf("%s", text);
}

void UIWow_WarnOnce(uint32_t flag, cstring_t fmt, ...) {
    va_list args;
    char text[1024];

    if (wow_ui.warn_once_mask & flag) {
        return;
    }
    wow_ui.warn_once_mask |= flag;
    if (!mi.Printf) {
        return;
    }
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    mi.Printf("%s", text);
}

void UIWow_EnsureRenderer(void) {
    if (!wow_ui.renderer && mi.GetRenderer) {
        wow_ui.renderer = mi.GetRenderer();
    }
    if (!wow_ui.renderer) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_RENDERER, "UIWow: renderer is unavailable (GetRenderer returned NULL)\n");
    }
}

static bool UIWow_TexturePathHasExt(cstring_t name) {
    cstring_t slash;
    if (!name || !*name) return false;
    slash = strrchr(name, '\\');
    if (!slash) slash = strrchr(name, '/');
    return strchr(slash ? slash + 1 : name, '.') != NULL;
}

static bool UIWow_MainHasArchiveFile(cstring_t path) {
    void *buf = NULL;
    int size;
    if (!path || !*path || !mi.FS_ReadFile || !mi.FS_FreeFile) return false;
    size = mi.FS_ReadFile(path, &buf);
    if (size > 0 && buf) { mi.FS_FreeFile(buf); return true; }
    SAFE_DELETE(buf, mi.FS_FreeFile);
    return false;
}

/* Both fallbacks below compensate for textures that the 1.0 vanilla MPQ never shipped — not bugs in
 * this code. The whoa-master UI XML was likely written against a later/more complete asset set.
 *
 * Glues-Splash-* → Glues-Logo.blp: XML requests per-realm/locale splash backgrounds
 * (e.g. Glues-Splash-US, Glues-Splash-EU) but none of those exist in the MPQ; only
 * Glues-Logo.blp is present. Any missing splash falls back to the generic logo.
 *
 * Glue-Panel-Button-Disabled-Down: the MPQ has Glue-Panel-Button-Disabled.blp and
 * Glue-Panel-Button-Down.blp as separate states but no combined disabled+pressed variant.
 * The UI XML references this composite path for the pushed state of a disabled button, so
 * we fall back to Disabled.blp — treating disabled+down identically to disabled. */
static void UIWow_ResolveTexturePath(cstring_t in, string_t out, size_t out_size) {
    static cstring_t exts[] = { ".blp", ".tga", ".dds", NULL };
    static cstring_t splash_prefix = "Interface\\Glues\\Common\\Glues-Splash-";
    static cstring_t splash_fallback = "Interface\\Glues\\Common\\Glues-Logo.blp";
    static cstring_t disabled_down_fallback = "Interface\\Glues\\Common\\Glue-Panel-Button-Disabled.blp";
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

LPTEXTURE UIWow_LoadTexture(cstring_t name) {
    int empty_slot = -1;
    PATHSTR resolved;

    if (!name || !*name) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_LOAD_BACKGROUND, "UIWow: attempted to load texture with empty name\n");
        return NULL;
    }
    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return NULL;
    }
    /* Fast path: input name already cached — skip MPQ resolution entirely. */
    FOR_LOOP(i, WOW_UI_MAX_TEXTURES) {
        uiWowTexture_t *entry = &wow_ui.tex_cache[i];

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
        uiWowTexture_t *entry = &wow_ui.tex_cache[empty_slot];

        snprintf(entry->input_name, sizeof(entry->input_name), "%s", name);
        snprintf(entry->name, sizeof(entry->name), "%s", resolved);
        entry->texture = wow_ui.renderer->LoadTexture(resolved);
        if (!entry->texture) {
            UIWow_Printf("UIWow: renderer failed to load texture '%s' (from '%s')\n", resolved, name);
        }
        return entry->texture;
    }

    {
        uiWowTexture_t *entry = &wow_ui.tex_cache[wow_ui.texture_recycle_index % WOW_UI_MAX_TEXTURES];

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

LPCFONT UIWow_LoadFont(uint32_t size) {
    UIWow_EnsureRenderer();
    if (!wow_ui.renderer) {
        return NULL;
    }
    FOR_LOOP(i, WOW_UI_MAX_FONTS) {
        uiWowFont_t *entry = &wow_ui.font_cache[i];

        if (entry->font && entry->size == size) {
            return entry->font;
        }
        if (!entry->font) {
            entry->size = size;
            entry->font = wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", size);
            if (!entry->font) {
                UIWow_Printf("UIWow: renderer failed to load font '%s' size=%u\n", "Fonts\\FRIZQT__.TTF", size);
            }
            return entry->font;
        }
    }
    {
        LPCFONT font = wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", size);
        if (!font) {
            UIWow_Printf("UIWow: renderer failed to load font '%s' size=%u\n", "Fonts\\FRIZQT__.TTF", size);
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
            SAFE_DELETE(wow_ui.tex_cache[i].texture, wow_ui.renderer->ReleaseTexture);
        }
        FOR_LOOP(i, WOW_UI_TEX_COUNT) {
            SAFE_DELETE(wow_ui.textures[i], wow_ui.renderer->ReleaseTexture);
        }
    }
    memset(&wow_ui, 0, sizeof(wow_ui));
}

static void UIWow_Refresh(uint32_t time) {
    wow_ui.time = time;
    UIWow_CallLuaUpdate(time);
    UIWow_EnsureRenderer();
    if (wow_ui.current_menu[0]) {
        UIWow_XMLDraw();
        UIWow_CallLuaDraw();
    }
}

static void UIWow_ReleaseScreenAssets(void) {
    if (!wow_ui.renderer) {
        return;
    }

    FOR_LOOP(i, WOW_UI_MAX_TEXTURES) {
        SAFE_DELETE(wow_ui.tex_cache[i].texture, wow_ui.renderer->ReleaseTexture);
        wow_ui.tex_cache[i].input_name[0] = '\0';
        wow_ui.tex_cache[i].name[0] = '\0';
    }
    FOR_LOOP(i, WOW_UI_MAX_FONTS) {
        wow_ui.font_cache[i].font = NULL;
        wow_ui.font_cache[i].size = 0;
    }
    FOR_LOOP(i, WOW_UI_TEX_COUNT) {
        SAFE_DELETE(wow_ui.textures[i], wow_ui.renderer->ReleaseTexture);
    }
    wow_ui.texture_recycle_index = 0;
}

static void UIWow_RecreateLuaStateForMenu(cstring_t menu_name) {
    if (!menu_name || !*menu_name) {
        return;
    }
    if (wow_ui.lua && wow_ui.current_menu[0] && !strcmp(wow_ui.current_menu, menu_name)) {
        return;
    }

    if (wow_ui.lua) {
        UIWow_Printf("UIWow: switching menu '%s' -> '%s'; recreating Lua state\n", wow_ui.current_menu[0] ? wow_ui.current_menu : "<none>", menu_name);
        UIWow_ShutdownLua();
    } else {
        UIWow_Printf("UIWow: creating Lua state for menu '%s'\n", menu_name);
    }

    UIWow_ReleaseScreenAssets();
    UIWow_InitLua();
}

/* Convert event pixels into FDF space. The WoW UI scene is drawn full-window
 * in normalized [0,1] space (R_UISceneRect → UI_BASE_WIDTH/HEIGHT = 1), so
 * mouse pixels divide by the current window size. The old fixed 1024x768
 * baseline left clicks landing wrong at any other resolution. */
VECTOR2 UIWow_MouseFdf(int x, int y) {
    size2_t window = { 1024, 768 };
    if (wow_ui.renderer && wow_ui.renderer->GetWindowSize) {
        window = wow_ui.renderer->GetWindowSize();
    }
    if (window.width == 0 || window.height == 0) {
        window = (size2_t){ 1024, 768 };
    }
    return MAKE(VECTOR2, x / (float)window.width, y / (float)window.height);
}

/* Forward mouse motion to Lua when XML does not own the hovered frame. */
static void UIWow_LuaMouseMove(int x, int y) {
    VECTOR2 mouse_pos = UIWow_MouseFdf(x, y);
    if (!wow_ui.lua) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE, "UIWow: Lua state is not initialized; mouse hover ignored\n");
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_mouse_move");
    if (lua_isfunction(wow_ui.lua, -1)) {
        lua_pushnumber(wow_ui.lua, mouse_pos.x);
        lua_pushnumber(wow_ui.lua, mouse_pos.y);
        UIWow_LuaPCall(2);
    } else {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_MOUSEMOVE_HANDLER, "UIWow: missing Lua function 'ow3_handle_mouse_move'\n");
    }
}

/* -------------------------------------------------------------------------
 * Input routing
 * ---------------------------------------------------------------------- */

static void UIWow_KeyEvent(int key, bool down, uint32_t time) {
    if (UIWow_XMLKeyEvent(key, down, time)) {
        return;
    }
}

static void UIWow_TextInput(cstring_t text) {
    if (UIWow_XMLTextInput(text)) {
        return;
    }
    if (!wow_ui.lua || !text) {
        if (!wow_ui.lua) {
            UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE, "UIWow: Lua state is not initialized; text input ignored\n");
        }
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_text_input");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_TEXT_HANDLER, "UIWow: missing Lua function 'ow3_handle_text_input'\n");
        return;
    }
    lua_pushstring(wow_ui.lua, text);
    UIWow_LuaPCall(1);
}

static bool UIWow_MouseEvent(menuMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 mouse_pos;
    if (UIWow_XMLMouseEvent(event, x, y, param)) {
        return true;
    }
    if (event == MENU_MOUSE_MOVE) {
        UIWow_LuaMouseMove(x, y);
        return false;
    }
    if (!wow_ui.lua || event != MENU_MOUSE_DOWN) {
        if (!wow_ui.lua && event == MENU_MOUSE_DOWN) {
            UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE, "UIWow: Lua state is not initialized; mouse click ignored\n");
        }
        return false;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_mouse_click");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_MOUSE_HANDLER, "UIWow: missing Lua function 'ow3_handle_mouse_click'\n");
        return false;
    }
    mouse_pos = UIWow_MouseFdf(x, y);
    lua_pushnumber(wow_ui.lua, mouse_pos.x);
    lua_pushnumber(wow_ui.lua, mouse_pos.y);
    lua_pushinteger(wow_ui.lua, param);
    UIWow_LuaPCall(3);
    return true;
}

/* -------------------------------------------------------------------------
 * Glue-menu commands
 * ---------------------------------------------------------------------- */

static void UIWow_CallLuaShow(cstring_t menu_name, cstring_t lua_func, cstring_t glue_screen) {
    UIWow_RecreateLuaStateForMenu(menu_name);
    snprintf(wow_ui.current_menu, sizeof(wow_ui.current_menu), "%s", menu_name);
    if (!wow_ui.lua) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_LUA_STATE, "UIWow: Lua state is not initialized; menu command '%s' ignored\n", menu_name ? menu_name : "<unknown>");
        return;
    }

    lua_getglobal(wow_ui.lua, lua_func);
    if (lua_isfunction(wow_ui.lua, -1)) {
        UIWow_LuaPCall(0);
        return;
    }
    lua_pop(wow_ui.lua, 1);

    if (!glue_screen || !*glue_screen) {
        UIWow_WarnOnce(WOW_UI_WARN_NO_MENU_HANDLER, "UIWow: missing Lua handler '%s' and no Glue fallback for menu '%s'\n", lua_func ? lua_func : "<unknown>", menu_name ? menu_name : "<unknown>");
        return;
    }
    lua_getglobal(wow_ui.lua, "SetGlueScreen");
    if (!lua_isfunction(wow_ui.lua, -1)) {
        lua_pop(wow_ui.lua, 1);
        UIWow_WarnOnce(WOW_UI_WARN_NO_SETGLUESCREEN, "UIWow: missing Lua function 'SetGlueScreen' for menu '%s' fallback '%s'\n", menu_name ? menu_name : "<unknown>", glue_screen);
        return;
    }
    lua_pushstring(wow_ui.lua, glue_screen);
    UIWow_LuaPCall(1);
}

static void UIWow_ShowLoginMenu(void)          { UIWow_CallLuaShow("login",            "ow3_show_login",            "login"); }
static void UIWow_ShowCharacterSelectMenu(void){ UIWow_CallLuaShow("character_select", "ow3_show_character_select", "charselect"); }
static void UIWow_ShowCharacterCreateMenu(void){ UIWow_CallLuaShow("character_create", "ow3_show_character_create", "charcreate"); }

typedef struct { cstring_t command; void (*function)(void); } uiWowMenuCommandDef_t;

static uiWowMenuCommandDef_t const uiWow_menu_command_defs[] = {
    { "menu_login",            UIWow_ShowLoginMenu },
    { "menu_character_select", UIWow_ShowCharacterSelectMenu },
    { "menu_character_create", UIWow_ShowCharacterCreateMenu },
    { NULL, NULL },
};

static void UIWow_RegisterMenuCommands(void) {
    if (uiWow_menu_commands_registered || !mi.Cmd_AddCommand) {
        return;
    }
    for (uiWowMenuCommandDef_t const *cmd = uiWow_menu_command_defs; cmd->command; cmd++) {
        mi.Cmd_AddCommand(cmd->command, cmd->function);
    }
    uiWow_menu_commands_registered = true;
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

static void UIWow_UpdateLobbySetup(lobbyState_t const *state) { (void)state; }

menuExport_t M_GetAPI(menuImport_t import) {
    mi = import;

    return (menuExport_t) {
        .Init             = UIWow_Init,
        .Shutdown         = UIWow_Shutdown,
        .Refresh          = UIWow_Refresh,
        .KeyEvent         = UIWow_KeyEvent,
        .TextInput        = UIWow_TextInput,
        .MouseEvent       = UIWow_MouseEvent,
        .UpdateLobbySetup = UIWow_UpdateLobbySetup,
    };
}
