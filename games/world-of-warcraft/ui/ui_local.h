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

#define WOW_UI_WARN_FLAG(x) (1u << (x))

#define WOW_UI_WARN_NO_RENDERER            WOW_UI_WARN_FLAG(0)
#define WOW_UI_WARN_NO_LUA_STATE           WOW_UI_WARN_FLAG(1)
#define WOW_UI_WARN_NO_DRAW_HANDLER        WOW_UI_WARN_FLAG(2)
#define WOW_UI_WARN_NO_UPDATE_HANDLER      WOW_UI_WARN_FLAG(3)
#define WOW_UI_WARN_NO_TEXT_HANDLER        WOW_UI_WARN_FLAG(4)
#define WOW_UI_WARN_NO_MOUSE_HANDLER       WOW_UI_WARN_FLAG(5)
#define WOW_UI_WARN_NO_MENU_HANDLER        WOW_UI_WARN_FLAG(6)
#define WOW_UI_WARN_NO_SETGLUESCREEN       WOW_UI_WARN_FLAG(7)
#define WOW_UI_WARN_NO_MOUSEMOVE_HANDLER   WOW_UI_WARN_FLAG(8)
#define WOW_UI_WARN_NO_INPUT_FS            WOW_UI_WARN_FLAG(9)
#define WOW_UI_WARN_NO_GLUE_BOOTSTRAP      WOW_UI_WARN_FLAG(10)
#define WOW_UI_WARN_NO_LOADING_DRAW        WOW_UI_WARN_FLAG(11)
#define WOW_UI_WARN_NO_LOAD_BACKGROUND     WOW_UI_WARN_FLAG(12)
#define WOW_UI_WARN_NO_MODEL_LOADER        WOW_UI_WARN_FLAG(13)

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
    DWORD warn_once_mask;
    uiWowTexture_t textures[WOW_UI_MAX_TEXTURES];
    DWORD texture_recycle_index;
    uiWowFont_t fonts[WOW_UI_MAX_FONTS];
    uiWowIcon_t inventory[WOW_UI_INVENTORY_SLOTS];
    uiWowIcon_t actions[WOW_UI_ACTION_SLOTS];
    LPTEXTURE background;
    PATHSTR active_map;
    FLOAT displayed_progress;  /* smoothed progress owned by get_loading_progress() */
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
BOOL UIWow_RunLuaString(LPCSTR name, LPCSTR script);
BOOL UIWow_LoadLuaFile(LPCSTR path, BOOL noisy_missing);

/* ui_xml.c */
void UIWow_XMLInitRuntime(void);
void UIWow_XMLShutdownRuntime(void);
BOOL UIWow_XMLLoadGlueFromToc(LPCSTR toc_path);
void UIWow_XMLDraw(void);

/* ui_loading.c */
void UIWow_UpdateMapBackground(LPCPLAYER ps);

/* Shared helpers (defined in ui_main.c) */
void UIWow_EnsureRenderer(void);
void UIWow_Printf(LPCSTR fmt, ...);
void UIWow_WarnOnce(DWORD flag, LPCSTR fmt, ...);
LPTEXTURE UIWow_LoadTexture(LPCSTR name);
LPCFONT UIWow_LoadFont(DWORD size);

/* XML runtime input hooks. */
BOOL UIWow_XMLMouseEvent(int x, int y, int button, BOOL down);
BOOL UIWow_XMLTextInput(LPCSTR text);
BOOL UIWow_XMLKeyEvent(int key, BOOL down, DWORD time);

#endif /* wow_ui_local_h */
