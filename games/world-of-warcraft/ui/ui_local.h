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
#define WOW_UI_MAX_TUTORIAL_ALERTS 10 // alerts; MAX_TUTORIAL_ALERTS in TutorialFrame.lua

typedef enum {
    WOW_UI_TEX_BACKGROUND = 0,
    WOW_UI_TEX_COUNT
} uiWowTexId_t;

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
#define WOW_UI_WARN_NO_LOAD_BACKGROUND     WOW_UI_WARN_FLAG(12)
#define WOW_UI_WARN_NO_MODEL_LOADER        WOW_UI_WARN_FLAG(13)
#define WOW_UI_WARN_NO_CHAR_MODEL          WOW_UI_WARN_FLAG(14)

typedef struct {
    char input_name[256]; /* as passed to UIWow_LoadTexture, used for cache lookup */
    char name[256];       /* resolved path (with extension), used for loading */
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
    char name[256];
} uiWowIcon_t;

typedef struct {
    LPRENDERER renderer;
    lua_State *lua;
    BOOL game_mode;
    DWORD warn_once_mask;
    uiWowTexture_t tex_cache[WOW_UI_MAX_TEXTURES];
    DWORD texture_recycle_index;
    uiWowFont_t font_cache[WOW_UI_MAX_FONTS];
    uiWowIcon_t inventory[WOW_UI_INVENTORY_SLOTS];
    uiWowIcon_t actions[WOW_UI_ACTION_SLOTS];
    wowUiMessage_t messages[WOW_UI_MAX_MESSAGES];
    DWORD message_count;
    DWORD open_message_id;
    DWORD tutorial_id;
    DWORD tutorial_alerts[WOW_UI_MAX_TUTORIAL_ALERTS];
    DWORD tutorial_alert_count;
    BOOL tutorial_open;
    BOOL tutorial_okay_pressed;
    char tutorial_title[WOW_UI_MESSAGE_TITLE];
    char tutorial_body[WOW_UI_MESSAGE_BODY];
    char tutorial_check[64];
    char tutorial_okay[32];
    LPTEXTURE textures[WOW_UI_TEX_COUNT];
    PATHSTR active_map;
    PATHSTR current_menu;
    int model_frame_idx;      /* frame index for SetCharSelectModelFrame */
    int char_customize_frame_idx;
    int char_select_frame_idx;
    int selected_char_idx;    /* 0-based index into wow_charlist for char-select screen */
    LPMODEL char_customize_model;
    PATHSTR char_customize_model_path;
    DWORD time;
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

/* stb_wowxml.h provides uiWowXmlType_t, wowXmlRuntime_t, and the parser API. */
#include "stb_wowxml.h"

/* ui_xml.c */
void UIWow_XMLInitRuntime(void);
void UIWow_XMLShutdownRuntime(void);
BOOL UIWow_XMLLoadGlueFromToc(LPCSTR toc_path);
BOOL UIWow_XMLLoadFile(LPCSTR path);
BOOL UIWow_XMLLoadBuffer(LPCSTR buf, int size, LPCSTR debug_name);
void UIWow_XMLSetFrameVisible(LPCSTR name, BOOL visible);
BOOL UIWow_XMLSetFrameText(LPCSTR name, LPCSTR text);
BOOL UIWow_XMLSetButtonPressed(LPCSTR name, BOOL pressed);
BOOL UIWow_XMLDrawFrame(LPCSTR name);
void UIWow_XMLClearFrames(void);
LPCSTR UIWow_XMLHitButton(FLOAT nx, FLOAT ny);
void UIWow_XMLDraw(void);
int  UIWow_XmlFindByNamePub(LPCSTR name);
void UIWow_XmlComputeRectPub(int idx, FLOAT *x, FLOAT *y, FLOAT *w, FLOAT *h);
int    UIWow_XmlElemCount(void);
int    UIWow_XmlElemType(int idx);
LPCSTR UIWow_XmlElemName(int idx);
LPCSTR UIWow_XmlElemText(int idx);
LPCSTR UIWow_XmlElemOnClick(int idx);
LPCSTR UIWow_XmlElemPoint(int idx);
int    UIWow_XmlElemHidden(int idx);
LPCSTR UIWow_XmlElemParent(int idx);
void UIWow_XMLSetFrameModel(int idx, LPCSTR model_path);
void UIWow_XMLInvalidateCharCustomizeModel(void);
void UIWow_XmlSetFrameModel(int idx, LPCSTR model_path);

/* ui_loading.c */
void UIWow_UpdateMapBackground(LPCPLAYER ps);
void UIWow_DrawLoadingScreenC(LPCSTR map, LPCSTR status, FLOAT progress);

/* ui_windows.c */
void UIWow_ShowWindow(const char *window_id, int show);
void UIWow_DrawWindows(void);
BOOL UIWow_WindowMouseDown(float nx, float ny);
BOOL UIWow_WindowMouseUp(float nx, float ny);
void UIWow_TutorialOkayRect(RECT *out);
void UIWow_ShutdownWindows(void);
BOOL UIWow_TipsEnabled(void);
BOOL UIWow_ShowTip(DWORD id);
BOOL UIWow_QueueTip(DWORD id);

/* Shared helpers (defined in ui_main.c) */
void UIWow_EnterGameMode(void);
void UIWow_EnsureRenderer(void);
void UIWow_Printf(LPCSTR fmt, ...);
void UIWow_WarnOnce(DWORD flag, LPCSTR fmt, ...);
VECTOR2 UIWow_MouseFdf(int x, int y);
LPTEXTURE UIWow_LoadTexture(LPCSTR name);
LPCFONT UIWow_LoadFont(DWORD size);

/* XML runtime input hooks. */
BOOL UIWow_XMLMouseEvent(uiMouseEvent_t event, int x, int y, int32_t param);
BOOL UIWow_XMLTextInput(LPCSTR text);
BOOL UIWow_XMLKeyEvent(int key, BOOL down, DWORD time);

#endif /* wow_ui_local_h */
