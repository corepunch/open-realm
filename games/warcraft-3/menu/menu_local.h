/*
 * menu_local.h — UI library internal types and declarations.
 *
 * This file contains the internal data structures, function prototypes, and
 * constants used within the UI library. External code should only include
 * menu.h, never this file.
 *
 * Frame template structures (FRAMEDEF) are defined in stb_fdf.h which is
 * shared with the game module. This header adds UI-specific extensions.
 */
#ifndef menu_local_h
#define menu_local_h

#include <stdio.h>

#include "common/stb_fdf.h"
#include "client/menu.h"
#include "common/mapinfo.h"

/* Forward declarations */
typedef struct uiScreen_s uiScreen_t;  /* Defined in menu_screen.h */

/* Global import callbacks (filled by M_GetAPI) */
extern menuImport_t mi;

/* Internal function prototypes */

/* menu_main.c */
void M_Init(void);
void M_SetActive(bool active);
void M_Shutdown(void);
void M_Refresh(uint32_t time);
uint32_t M_Time(void);
void M_TransitionToAction(void (*action)(void));
bool M_IsTransitioning(void);

/* scene.c */
typedef enum {
    UI_GLUE_NONE,
    UI_GLUE_MAIN_MENU,
    UI_GLUE_REALM_SELECTION,
    UI_GLUE_SINGLE_PLAYER,
    UI_GLUE_OPTIONS,
    UI_GLUE_MULTIPLAYER_PRE_GAME_CHAT,
    UI_GLUE_BATTLENET_CUSTOM,
    UI_GLUE_PANEL_COUNT,
} uiGluePanel_t;

typedef struct { uiGluePanel_t panel; int tab, page; } GLUEDEST;
typedef GLUEDEST *LPGLUEDEST;
typedef const GLUEDEST *LPCGLUEDEST;

void UI_ResetGlueSceneModels(void);
void UI_ResetGlueTransitions(void);
void UI_ReleaseGlueSceneModels(void);
void UI_PreloadGlueSceneModels(void);
typedef void (*uiGluePanelChanged_f)(void);
void UI_GotoGluePanel(GLUEDEST dest, uiGluePanelChanged_f exited, uiGluePanelChanged_f changed);
void UI_CloseGluePanel(uiGluePanelChanged_f changed);
void UI_DrawGlueScene(void);
typedef enum { UI_GLUE_LEFT, UI_GLUE_RIGHT, UI_GLUE_SIDE_COUNT } uiGlueSide_t;
bool UI_GlueSideReady(uiGlueSide_t side);
bool UI_GlueIsTransitioning(void);
float UI_ScreenFrameOffset(LPCFRAMEDEF frame);
float UI_GlueSideOffset(uiGlueSide_t side);

/* menu_fdf.c — FDF parsing (moved from game/menu/menu_fdf.c) */
bool UI_EnsureFDF(cstring_t filename);
void UI_ParseFDF(cstring_t filename);
void UI_ParseFDF_Buffer(cstring_t filename, string_t buffer);
void UI_ClearTemplates(void);
void UI_ReleaseAssets(void);
void UI_WireFrameTypeFunctions(LPFRAMEDEF frame);
void UI_SetText(LPFRAMEDEF, cstring_t, ...);
void UI_SetTextPointer(LPFRAMEDEF, cstring_t);
void UI_SetTexture(LPFRAMEDEF, cstring_t, bool);
void UI_SetTexture2(LPFRAMEDEF, cstring_t, bool);
void UI_InheritFrom(LPFRAMEDEF, cstring_t);
void UI_LoadTheme(cstring_t fileName);
void UI_ClearTheme(void);
void UI_QueueCommand(cstring_t command);
LPCFRAMEDEF UI_HitTest(float fdf_x, float fdf_y);
rect_t UI_GetSceneRect(void);
rect_t UI_GetCenteredSceneRect(void);
void UI_DrawFrameInScene(LPCFRAMEDEF frame, rect_t const * scene);
void UI_DrawFramesInScene(LPCFRAMEDEF const *roots, uint32_t num_roots, rect_t const * scene);
void UI_TogglePopup(LPCFRAMEDEF frame);
void UI_SliderBeginDrag(LPCFRAMEDEF frame, float fdf_x, float fdf_y);
void UI_SliderUpdateDrag(LPCFRAMEDEF frame, float fdf_x, float fdf_y);
void UI_SliderEndDrag(LPCFRAMEDEF frame);
bool UI_SliderIsDragging(void);
LPCFRAMEDEF UI_SliderActiveFrame(void);
bool UI_HasActivePopup(void);
void UI_EditboxFocusOnHit(LPCFRAMEDEF frame);
void UI_EditboxClearFocusOnMiss(void);
void UI_MapListSelectRow(LPCFRAMEDEF frame, float fdf_x, float fdf_y);
void UI_MapListScroll(LPCFRAMEDEF frame, bool scroll_up);
void UI_PopupCloseOnMiss(void);
bool UI_PopupPointInside(float fdf_x, float fdf_y);
void UI_PopupMenuScroll(bool scroll_up);
void UI_PopupMenuHover(float fdf_x, float fdf_y);
void UI_PopupSelectItem(float fdf_x, float fdf_y);
uint32_t UI_LoadTexture(cstring_t, bool);
cstring_t UI_TextureName(uint32_t index);
LPCTEXTURE UI_GetTexture(uint32_t index);
LPCMODEL UI_GetModel(uint32_t index);
uint32_t UI_LoadModel(cstring_t file, bool decorate);
cstring_t UI_GetString(cstring_t);
LPFRAMEDEF UI_Spawn(FRAMETYPE, LPFRAMEDEF);
LPFRAMEDEF UI_CloneFrameTree(LPCFRAMEDEF source, LPFRAMEDEF parent);

#ifndef BZ_FDF_REPORT_MISSING
#define BZ_FDF_REPORT_MISSING(NAME) \
    do { \
        fprintf(stderr, "ERROR: missing FDF binding: %s\n", (NAME)); \
        if (mi.Printf) mi.Printf("ERROR: missing FDF binding: %s\n", (NAME)); \
    } while (0)
#endif

#ifndef BZ_FDF_BIND_ROOT
#define BZ_FDF_BIND_ROOT(OUT, FIELD, NAME) \
    do { (OUT)->FIELD = UI_FindFrame((NAME)); if (!(OUT)->FIELD) { BZ_FDF_REPORT_MISSING((NAME)); ok = false; } } while (0)
#endif

#ifndef BZ_FDF_BIND_ROOT_OPTIONAL
#define BZ_FDF_BIND_ROOT_OPTIONAL(OUT, FIELD, NAME) \
    do { (OUT)->FIELD = UI_FindFrame((NAME)); } while (0)
#endif

#ifndef BZ_FDF_BIND_CHILD
#define BZ_FDF_BIND_CHILD(OUT, FIELD, PARENT, NAME) \
    do { (OUT)->FIELD = (PARENT) ? UI_FindChildFrame((PARENT), (NAME)) : NULL; if (!(OUT)->FIELD) { BZ_FDF_REPORT_MISSING((NAME)); ok = false; } } while (0)
#endif

#ifndef BZ_FDF_BIND_CHILD_OPTIONAL
#define BZ_FDF_BIND_CHILD_OPTIONAL(OUT, FIELD, PARENT, NAME) \
    do { (OUT)->FIELD = (PARENT) ? UI_FindChildFrame((PARENT), (NAME)) : NULL; } while (0)
#endif

void UI_BindMapList(LPFRAMEDEF frame,
                    uiMapListState_t *state,
                    LPCFRAMEDEF label,
                    uint32_t visible_rows,
                    cstring_t select_command);
void UI_LayoutMapInfoPane(LPFRAMEDEF frame);
bool UI_ReadMapInfo(cstring_t mapFilename, LPMAPINFO info);
bool UI_FindMapPreviewTexture(cstring_t mapFilename, string_t out, uint32_t out_size);
void UI_FreeMapInfo(LPMAPINFO info);
void UI_DefaultMapName(cstring_t path, string_t out, uint32_t out_size);
void UI_ResolveMapInfoString(LPCMAPINFO info, cstring_t text, string_t out, uint32_t out_size);
bool UI_MapNameMatchesFile(cstring_t name, cstring_t path);
cstring_t UI_MapTilesetName(uint8_t tileset);
cstring_t UI_MapSizeName(uint32_t width, uint32_t height);
void UI_SanitizeMapListField(string_t text);
void UI_SanitizeMapInfoText(string_t text);
cstring_t Theme_String(cstring_t, cstring_t);
float Theme_Float(cstring_t, cstring_t);
COLOR32 Theme_ListBoxSelectionColor(void);
COLOR32 Theme_ListBoxTextColor(void);
COLOR32 Theme_ListBoxSelectedTextColor(void);
COLOR32 Theme_ListBoxIconTextColor(void);

/* ui_frame.c — Frame tree manipulation (to be created) */
// Additional frame management functions will be declared here

/* menu_render.c — Frame rendering */
void UI_DrawFrame(LPCFRAMEDEF frame);
void UI_DrawFrames(LPCFRAMEDEF const *roots, uint32_t num_roots);
bool M_EditKey(int key);
bool M_MouseEvent(menuMouseEvent_t event, int x, int y, int32_t param);
void M_TextInput(cstring_t text);
void UI_EditTextInput(cstring_t text);
bool UI_EditHasFocus(LPCFRAMEDEF frame);
cstring_t UI_EditValue(LPCFRAMEDEF frame);
void UI_SetEditValue(LPFRAMEDEF frame, cstring_t text);
void UI_ClearEditFocus(void);

uiScreen_t *UI_GetCurrentScreen(void);

#endif
