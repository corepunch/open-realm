/*
 * ui_local.h — UI library internal types and declarations.
 *
 * This file contains the internal data structures, function prototypes, and
 * constants used within the UI library. External code should only include
 * ui.h, never this file.
 *
 * Frame template structures (FRAMEDEF) are defined in stb_fdf.h which is
 * shared with the game module. This header adds UI-specific extensions.
 */
#ifndef ui_local_h
#define ui_local_h

#include <stdio.h>

#include "common/stb_fdf.h"
#include "client/ui.h"
#include "games/warcraft-3/common/mapinfo.h"

/* Forward declarations */
typedef struct uiScreen_s uiScreen_t;  /* Defined in ui_screen.h */

/* Global import callbacks (filled by UI_GetAPI) */
extern uiImport_t uiimport;

/* Internal function prototypes */

/* ui_main.c */
void UI_InitLocal(void);
void UI_SetActive(BOOL active);
void UI_ShutdownLocal(void);
void UI_RefreshLocal(DWORD time);

/* ui_glue_scene.c */
void UI_ResetGlueSceneModels(void);
void UI_PreloadGlueSceneModels(void);
void UI_DrawGlueScene(LPCSTR panel_anim);
void UI_DrawGlueSceneLayers(LPCSTR left_panel_anim, LPCSTR right_panel_anim);

/* ui_fdf.c — FDF parsing (moved from game/ui/ui_fdf.c) */
BOOL UI_EnsureFDF(LPCSTR filename);
void UI_ParseFDF(LPCSTR filename);
void UI_ParseFDF_Buffer(LPCSTR filename, LPSTR buffer);
void UI_ClearTemplates(void);
void UI_WireFrameTypeFunctions(LPFRAMEDEF frame);
void UI_SetText(LPFRAMEDEF, LPCSTR, ...);
void UI_SetTextPointer(LPFRAMEDEF, LPCSTR);
void UI_SetTexture(LPFRAMEDEF, LPCSTR, BOOL);
void UI_SetTexture2(LPFRAMEDEF, LPCSTR, BOOL);
void UI_InheritFrom(LPFRAMEDEF, LPCSTR);
void UI_LoadTheme(LPCSTR fileName);
void UI_ClearTheme(void);
void UI_MenuCommandLocal(LPCSTR command);
DWORD UI_GetTime(void);
LPCFRAMEDEF UI_HitTest(FLOAT fdf_x, FLOAT fdf_y);
void UI_TogglePopup(LPCFRAMEDEF frame);
void UI_SliderBeginDrag(LPCFRAMEDEF frame, FLOAT fdf_x, FLOAT fdf_y);
void UI_SliderUpdateDrag(LPCFRAMEDEF frame, FLOAT fdf_x, FLOAT fdf_y);
void UI_SliderEndDrag(LPCFRAMEDEF frame);
BOOL UI_SliderIsDragging(void);
LPCFRAMEDEF UI_SliderActiveFrame(void);
BOOL UI_HasActivePopup(void);
void UI_EditboxFocusOnHit(LPCFRAMEDEF frame);
void UI_EditboxClearFocusOnMiss(void);
void UI_MapListSelectRow(LPCFRAMEDEF frame, FLOAT fdf_x, FLOAT fdf_y);
void UI_MapListScroll(LPCFRAMEDEF frame, BOOL scroll_up);
void UI_PopupCloseOnMiss(void);
BOOL UI_PopupPointInside(FLOAT fdf_x, FLOAT fdf_y);
void UI_PopupMenuScroll(BOOL scroll_up);
void UI_PopupMenuHover(FLOAT fdf_x, FLOAT fdf_y);
void UI_PopupSelectItem(FLOAT fdf_x, FLOAT fdf_y);
DWORD UI_LoadTexture(LPCSTR, BOOL);
LPCSTR UI_TextureName(DWORD index);
LPCTEXTURE UI_GetTexture(DWORD index);
LPCMODEL UI_GetModel(DWORD index);
DWORD UI_LoadModel(LPCSTR file, BOOL decorate);
LPCSTR UI_GetString(LPCSTR);
LPFRAMEDEF UI_Spawn(FRAMETYPE, LPFRAMEDEF);
LPFRAMEDEF UI_CloneFrameTree(LPCFRAMEDEF source, LPFRAMEDEF parent);

#ifndef BZ_FDF_REPORT_MISSING
#define BZ_FDF_REPORT_MISSING(NAME) \
    do { \
        fprintf(stderr, "ERROR: missing FDF binding: %s\n", (NAME)); \
        if (uiimport.Printf) uiimport.Printf("ERROR: missing FDF binding: %s\n", (NAME)); \
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
                    DWORD visible_rows,
                    LPCSTR select_command);
void UI_LayoutMapInfoPane(LPFRAMEDEF frame);
BOOL UI_ReadMapInfo(LPCSTR mapFilename, LPMAPINFO info);
BOOL UI_FindMapPreviewTexture(LPCSTR mapFilename, LPSTR out, DWORD out_size);
void UI_FreeMapInfo(LPMAPINFO info);
void UI_DefaultMapName(LPCSTR path, LPSTR out, DWORD out_size);
void UI_ResolveMapInfoString(LPCMAPINFO info, LPCSTR text, LPSTR out, DWORD out_size);
BOOL UI_MapNameMatchesFile(LPCSTR name, LPCSTR path);
LPCSTR UI_MapTilesetName(BYTE tileset);
LPCSTR UI_MapSizeName(DWORD width, DWORD height);
void UI_SanitizeMapListField(LPSTR text);
void UI_SanitizeMapInfoText(LPSTR text);
LPCSTR Theme_String(LPCSTR, LPCSTR);
FLOAT Theme_Float(LPCSTR, LPCSTR);
COLOR32 Theme_ListBoxSelectionColor(void);
COLOR32 Theme_ListBoxTextColor(void);
COLOR32 Theme_ListBoxSelectedTextColor(void);
COLOR32 Theme_ListBoxIconTextColor(void);

/* ui_frame.c — Frame tree manipulation (to be created) */
// Additional frame management functions will be declared here

/* ui_render.c — Frame rendering */
void UI_DrawFrame(LPCFRAMEDEF frame);
void UI_DrawGamePortraitInFrame(LPCFRAMEDEF frame, DWORD modelIndex, LPCSTR anim);
void UI_DrawFrames(LPCFRAMEDEF const *roots, DWORD num_roots);
BOOL UI_EditKey(int key);
BOOL UI_MouseEventLocal(uiMouseEvent_t event, int x, int y, int32_t param);
void UI_TextInputLocal(LPCSTR text);
BOOL UI_EditHasFocus(LPCFRAMEDEF frame);
LPCSTR UI_EditValue(LPCFRAMEDEF frame);
void UI_SetEditValue(LPFRAMEDEF frame, LPCSTR text);
void UI_ClearEditFocus(void);

uiScreen_t *UI_GetCurrentScreen(void);

#endif
