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

void UIWow_EnsureRenderer(void) {
    if (!wow_ui.renderer && uiimport.GetRenderer) {
        wow_ui.renderer = uiimport.GetRenderer();
    }
}

LPTEXTURE UIWow_LoadTexture(LPCSTR name) {
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
            return entry->font;
        }
    }
    return wow_ui.renderer->LoadFont("Fonts\\FRIZQT__.TTF", size);
}

/* -------------------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------------- */

static void UIWow_Init(void) {
    memset(&wow_ui, 0, sizeof(wow_ui));
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
        return;
    }
    lua_getglobal(wow_ui.lua, "ow3_handle_mouse_move");
    if (lua_isfunction(wow_ui.lua, -1)) {
        lua_pushnumber(wow_ui.lua, mouse_pos.x);
        lua_pushnumber(wow_ui.lua, mouse_pos.y);
        UIWow_LuaPCall(2);
    } else {
        lua_pop(wow_ui.lua, 1);
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
        UIWow_UpdateMapBackground(ps);
        lua_getglobal(wow_ui.lua, "ow3_draw_loading_screen");
        if (lua_isfunction(wow_ui.lua, -1)) {
            UIWow_LuaPCall(0);
        } else {
            lua_pop(wow_ui.lua, 1);
        }
        return;
    }
    if (wow_ui.current_menu[0] || ps->client_ui_state == CLIENT_UI_GAME) {
        UIWow_CallLuaDraw();
    }
}

/* -------------------------------------------------------------------------
 * Input routing
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * Glue-menu commands
 * ---------------------------------------------------------------------- */

static void UIWow_CallLuaShow(LPCSTR menu_name, LPCSTR lua_func) {
    snprintf(wow_ui.current_menu, sizeof(wow_ui.current_menu), "%s", menu_name);
    if (!wow_ui.lua) {
        return;
    }
    lua_getglobal(wow_ui.lua, lua_func);
    if (lua_isfunction(wow_ui.lua, -1)) {
        UIWow_LuaPCall(0);
    } else {
        lua_pop(wow_ui.lua, 1);
    }
}

static void UIWow_ShowLoginMenu(void)          { UIWow_CallLuaShow("login",            "ow3_show_login"); }
static void UIWow_ShowCharacterSelectMenu(void){ UIWow_CallLuaShow("character_select", "ow3_show_character_select"); }
static void UIWow_ShowCharacterCreateMenu(void){ UIWow_CallLuaShow("character_create", "ow3_show_character_create"); }

typedef struct { LPCSTR command; void (*function)(void); } uiWowMenuCommandDef_t;

static uiWowMenuCommandDef_t const uiWow_menu_command_defs[] = {
    { "menu_login",            UIWow_ShowLoginMenu },
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
        .MenuCommand      = UIWow_MenuCommand,
        .UpdateUnitUI     = UIWow_UpdateUnitUI,
        .SetLayoutLayer   = UIWow_SetLayoutLayer,
        .ClearLayoutLayer = UIWow_ClearLayoutLayer,
        .HitTestLayout    = UIWow_HitTestLayout,
    };
}
