/*
 * menu_main.c — WoW UI library entry point and lifecycle management.
 *
 * Owns the global state definitions, shared asset helpers, per-frame
 * dispatch, input routing, glue-menu commands, unit icon sync, and the
 * UI_GetAPI entry point.  Rendering detail lives in menu_loading.c;
 * Lua VM and bindings live in menu_lua.c.
 */
#include "menu_local.h"

#include <stdarg.h>

/* -------------------------------------------------------------------------
 * Global state (declared extern in menu_local.h)
 * ---------------------------------------------------------------------- */

uiImport_t uiimport;

static BOOL UIWow_GameOverlayMouseEvent(uiMouseEvent_t event, int x, int y);
uiWowState_t wow_ui;

static BOOL uiWow_menu_commands_registered;

#define WOW_TIP_ALERT_Y 671.0f // UI pixels; TutorialFrame.lua reanchors the first alert 55px above the bottom edge
#define WOW_TIP_ALERT_W 34.0f // UI pixels; native TutorialFrameAlert visible crop width
#define WOW_TIP_ALERT_H 42.0f // UI pixels; native TutorialFrameAlert visible crop height
#define WOW_TIP_ALERT_STEP 36.0f // UI pixels; TutorialFrame_NewTutorial horizontal sibling offset

/* -------------------------------------------------------------------------
 * Shared helpers used by menu_lua.c and menu_loading.c
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
        UIWow_WarnOnce(WOW_UI_WARN_NO_RENDERER, "UIWow: renderer is unavailable (GetRenderer returned NULL)\n");
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

LPCFONT UIWow_LoadFont(DWORD size) {
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
    UIWow_ShutdownWindows();
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

static void UIWow_Refresh(DWORD time) {
    wow_ui.time = time;
    if (!wow_ui.game_mode)
        UIWow_CallLuaUpdate(time);

    LPCPLAYER ps = uiimport.GetPlayerState ? uiimport.GetPlayerState() : NULL;

    UIWow_EnsureRenderer();

    if (ps && ps->client_ui_state == CLIENT_UI_LOADING) {
        UIWow_UpdateMapBackground(ps);
        UIWow_DrawLoadingScreenC(NULL, NULL, 0.0f);
        return;
    }
    if (wow_ui.current_menu[0]) {
        UIWow_XMLDraw();
        UIWow_CallLuaDraw();
        return;
    }
    if (ps && ps->client_ui_state == CLIENT_UI_GAME && !wow_ui.game_mode) {
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

static void UIWow_RecreateLuaStateForMenu(LPCSTR menu_name) {
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
    return MAKE(VECTOR2, x / (FLOAT)window.width, y / (FLOAT)window.height);
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

static void UIWow_KeyEvent(int key, BOOL down, DWORD time) {
    if (wow_ui.game_mode) {
        return;
    }
    if (UIWow_XMLKeyEvent(key, down, time)) {
        return;
    }
}

static void UIWow_TextInput(LPCSTR text) {
    if (wow_ui.game_mode) {
        return;
    }
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

static BOOL UIWow_MouseEvent(uiMouseEvent_t event, int x, int y, int32_t param) {
    VECTOR2 mouse_pos;
    if (wow_ui.game_mode)
        return UIWow_GameOverlayMouseEvent(event, x, y);
    if (UIWow_XMLMouseEvent(event, x, y, param)) {
        return true;
    }
    if (event == UI_MOUSE_MOVE) {
        UIWow_LuaMouseMove(x, y);
        return false;
    }
    if (!wow_ui.lua || event != UI_MOUSE_DOWN) {
        if (!wow_ui.lua && event == UI_MOUSE_DOWN) {
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

/* TutorialFrame remains a legacy client-owned game window until its layout is server-authored. */
static BOOL UIWow_GameOverlayMouseEvent(uiMouseEvent_t event, int x, int y) {
    VECTOR2 pos = UIWow_MouseFdf(x, y);
    DWORD unread = 0;

    if (event == UI_MOUSE_UP) return UIWow_WindowMouseUp(pos.x, pos.y);
    if (event != UI_MOUSE_DOWN) return false;
    if (UIWow_WindowMouseDown(pos.x, pos.y)) return true;
    FOR_LOOP(i, wow_ui.tutorial_alert_count) {
        FLOAT icon_x = 0.5f-WOW_TIP_ALERT_W/2048.0f + unread++*WOW_TIP_ALERT_STEP/1024.0f;
        if (pos.x < icon_x || pos.x > icon_x+WOW_TIP_ALERT_W/1024.0f || pos.y < WOW_TIP_ALERT_Y/768.0f || pos.y > (WOW_TIP_ALERT_Y+WOW_TIP_ALERT_H)/768.0f) continue;
        UIWow_ShowTip(wow_ui.tutorial_alerts[i]);
        memmove(&wow_ui.tutorial_alerts[i], &wow_ui.tutorial_alerts[i+1], (wow_ui.tutorial_alert_count-i-1)*sizeof(wow_ui.tutorial_alerts[0]));
        wow_ui.tutorial_alert_count--;
        return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * Glue-menu commands
 * ---------------------------------------------------------------------- */

static void UIWow_CallLuaShow(LPCSTR menu_name, LPCSTR lua_func, LPCSTR glue_screen) {
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

void UIWow_EnterGameMode(void) {
    wow_ui.game_mode = true;
    wow_ui.current_menu[0] = '\0';
    UIWow_XMLClearFrames();  /* drop glue-screen elements; native game FrameXML loads on demand */
}

typedef struct { LPCSTR command; void (*function)(void); } uiWowMenuCommandDef_t;

static uiWowMenuCommandDef_t const uiWow_menu_command_defs[] = {
    { "menu_login",            UIWow_ShowLoginMenu },
    { "menu_character_select", UIWow_ShowCharacterSelectMenu },
    { "menu_character_create", UIWow_ShowCharacterCreateMenu },
    { "menu_ingame",           UIWow_EnterGameMode },
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

/* Route reliable server payloads to the WoW UI data model; gameplay handlers
 * must validate and mutate state on the server instead of in this callback. */
static void UIWow_GameCommand(LPCSTR command, void const *data, DWORD size) {
    BYTE const *payload = data;

    if (!command || !*command) {
        fprintf(stderr, "UIWow: received game command with no command name\n");
        return;
    }
    if (!data && size) {
        fprintf(stderr, "UIWow: received game command '%s' with NULL payload\n", command);
        return;
    }
    if (!strcmp(command, "wow_tutorial")) {
        if (size != 2 || payload[0] != 1 || !payload[1]) {
            UIWow_Printf("UIWow: invalid wow_tutorial payload (%u bytes)\n", (unsigned)size);
            return;
        }
        UIWow_QueueTip(payload[1]);
        return;
    }
    if (!strncasecmp(command, "wow_", 4))
        UIWow_Printf("UIWow: unsupported game command '%s' (%u bytes)\n", command, (unsigned)size);
}

/* -------------------------------------------------------------------------
 * Entry point
 * ---------------------------------------------------------------------- */

static void UIWow_UpdateLobbySetup(lobbyState_t const *state) { (void)state; }

uiExport_t UI_GetAPI(uiImport_t import) {
    uiimport = import;

    return (uiExport_t) {
        .Init             = UIWow_Init,
        .Shutdown         = UIWow_Shutdown,
        .Refresh          = UIWow_Refresh,
        .KeyEvent         = UIWow_KeyEvent,
        .TextInput        = UIWow_TextInput,
        .MouseEvent       = UIWow_MouseEvent,
        .UpdateUnitUI     = UIWow_UpdateUnitUI,
        .UpdateLobbySetup = UIWow_UpdateLobbySetup,
        .GameCommand      = UIWow_GameCommand,
        .ShowWindow       = UIWow_ShowWindow,
    };
}
