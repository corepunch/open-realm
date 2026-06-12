/*
 * ui_local.h — WoW UI library internal types and declarations.
 *
 * Internal data structures shared across ui_main.c, ui_lua.c, and
 * ui_loading.c.  External code should only include client/ui.h.
 */
#ifndef wow_ui_local_h
#define wow_ui_local_h

#include "client/ui.h"
#include "common/wow_ui_shared.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WOW_UI_MAX_TEXTURES 256
#define WOW_UI_MAX_FONTS    16

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

extern uiImport_t uiimport;
extern uiWowState_t wow_ui;

/* ui_lua.c */
void UIWow_InitLua(void);
void UIWow_ShutdownLua(void);
BOOL UIWow_LuaPCall(int nargs);
void UIWow_CallLuaDraw(void);
void UIWow_CallLuaUpdate(DWORD msec);

/* ui_loading.c */
void UIWow_LoadStaticAssets(void);
void UIWow_UpdateMapBackground(LPCPLAYER ps);
void UIWow_DrawLoadingScreen(void);

/* Shared helpers (defined in ui_main.c) */
void UIWow_EnsureRenderer(void);
void UIWow_Printf(LPCSTR fmt, ...);
LPTEXTURE UIWow_LoadTexture(LPCSTR name);
LPCFONT UIWow_LoadFont(DWORD size);

#endif /* wow_ui_local_h */
