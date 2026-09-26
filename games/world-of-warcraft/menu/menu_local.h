/*
 * menu_local.h — WoW UI library internal types and declarations.
 *
 * Internal data structures shared across menu_main.c, menu_lua.c, and
 * menu_loading.c.  External code should only include client/menu.h.
 */
#ifndef wow_menu_local_h
#define wow_menu_local_h

#include "client/menu.h"
#include "common/wow_ui_shared.h"
#include "../common/wow_config.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WOW_UI_MAX_TEXTURES 256
#define WOW_UI_MAX_FONTS    16

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
    texture_t *texture;
} uiWowTexture_t;

typedef struct {
    uint32_t size;
    font_t const *font;
} uiWowFont_t;

typedef struct wowXmlPoint_s {
    cstring_t point, rel, rel_point;
    float x, y;
} wowXmlPoint_t;



typedef struct {
    refExport_t *renderer;
    lua_State *lua;
    uint32_t warn_once_mask;
    uiWowTexture_t tex_cache[WOW_UI_MAX_TEXTURES];
    uint32_t texture_recycle_index;
    uiWowFont_t font_cache[WOW_UI_MAX_FONTS];
    texture_t *textures[WOW_UI_TEX_COUNT];
    PATHSTR active_map;
    PATHSTR current_menu;
    int model_frame_idx;      /* frame index for SetCharSelectModelFrame */
    int char_customize_frame_idx;
    int char_select_frame_idx;
    int selected_char_idx;    /* 0-based index into wow_charlist for char-select screen */
    model_t *char_customize_model;
    PATHSTR char_customize_model_path;
    uint32_t time;
} uiWowState_t;

extern menuImport_t mi;
extern uiWowState_t wow_ui;

/* menu_lua.c */
void UIWow_InitLua(void);
void UIWow_ShutdownLua(void);
bool UIWow_LuaPCall(int nargs);
void UIWow_CallLuaDraw(void);
void UIWow_CallLuaUpdate(uint32_t msec);
bool UIWow_RunLuaString(cstring_t name, cstring_t script);
bool UIWow_LoadLuaFile(cstring_t path, bool noisy_missing);

/* stb_wowxml.h provides uiWowXmlType_t, wowXmlRuntime_t, and the parser API. */
#include "stb_wowxml.h"

/* ui_xml.c */
void UIWow_XMLInitRuntime(void);
void UIWow_XMLShutdownRuntime(void);
bool UIWow_XMLLoadGlueFromToc(cstring_t toc_path);
bool UIWow_XMLLoadFile(cstring_t path);
bool UIWow_XMLLoadBuffer(cstring_t buf, int size, cstring_t debug_name);
void UIWow_XMLSetFrameVisible(cstring_t name, bool visible);
bool UIWow_XMLSetFrameText(cstring_t name, cstring_t text);
bool UIWow_XMLSetButtonPressed(cstring_t name, bool pressed);
bool UIWow_XMLSetButtonChecked(cstring_t name, bool checked);
bool UIWow_XMLSetFramePoint(cstring_t name, wowXmlPoint_t const *point);
bool UIWow_XMLSizeFrameToText(cstring_t frame, cstring_t text, float padding);
bool UIWow_XMLDrawFrame(cstring_t name);
void UIWow_XMLClearFrames(void);
cstring_t UIWow_XMLHitButton(float nx, float ny);
void UIWow_XMLDraw(void);
int  UIWow_XmlFindByNamePub(cstring_t name);
void UIWow_XmlComputeRectPub(int idx, float *x, float *y, float *w, float *h);
int    UIWow_XmlElemCount(void);
int    UIWow_XmlElemType(int idx);
cstring_t UIWow_XmlElemName(int idx);
cstring_t UIWow_XmlElemText(int idx);
cstring_t UIWow_XmlElemOnClick(int idx);
cstring_t UIWow_XmlElemPoint(int idx);
int    UIWow_XmlElemHidden(int idx);
cstring_t UIWow_XmlElemParent(int idx);
void UIWow_XMLSetFrameModel(int idx, cstring_t model_path);
void UIWow_XMLInvalidateCharCustomizeModel(void);
void UIWow_XmlSetFrameModel(int idx, cstring_t model_path);

/* menu_loading.c */
void UIWow_DrawLoadingScreenC(cstring_t map, cstring_t status, float progress);

/* Shared helpers (defined in menu_main.c) */
void UIWow_EnsureRenderer(void);
void UIWow_Printf(cstring_t fmt, ...);
void UIWow_WarnOnce(uint32_t flag, cstring_t fmt, ...);
vector2_t UIWow_MouseFdf(int x, int y);
texture_t *UIWow_LoadTexture(cstring_t name);
font_t const *UIWow_LoadFont(uint32_t size);

/* XML runtime input hooks. */
bool UIWow_XMLMouseEvent(menuMouseEvent_t event, int x, int y, int32_t param);
bool UIWow_XMLTextInput(cstring_t text);
bool UIWow_XMLKeyEvent(int key, bool down, uint32_t time);

#endif /* wow_menu_local_h */
