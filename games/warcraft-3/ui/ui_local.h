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
#include "common/mapinfo.h"

/* Forward declarations */
typedef struct uiScreen_s uiScreen_t;  /* Defined in ui_screen.h */

/* Global import callbacks (filled by UI_GetAPI) */
extern uiImport_t uiimport;

/* Internal function prototypes */

/* ui_main.c */
void UI_InitLocal(void);
void UI_SetActive(bool active);
void UI_ShutdownLocal(void);
void UI_RefreshLocal(uint32_t time);

/* ROC rows are label,sequence,model; TFT prepends a numeric expansion category (even for ROC campaigns). */
static inline bool UI_ParseLoadingRow(cstring_t row, uint32_t * sequence, string_t model) {
    int offset = 0;
    *sequence = 0; model[0] = 0;
    if (!row) return false;
    sscanf(row, "%*u,%n", &offset);
    /* The old fixed column indices read TFT's sequence as a filename; 255 bounds the PATHSTR output. */
    return sscanf(row + offset, "%*[^,],%u,%255[^,\r\n]", sequence, model) == 2;
}

/* ui_glue_scene.c */
void UI_ResetGlueSceneModels(void);
void UI_PreloadGlueSceneModels(void);
void UI_DrawGlueScene(cstring_t panel_anim);
void UI_DrawGlueSceneLayers(cstring_t left_panel_anim, cstring_t right_panel_anim);

/* ui_fdf.c — FDF parsing (moved from game/ui/ui_fdf.c) */
bool UI_EnsureFDF(cstring_t filename);
void UI_ParseFDF(cstring_t filename);
void UI_ParseFDF_Buffer(cstring_t filename, string_t buffer);
void UI_ClearTemplates(void);
void UI_WireFrameTypeFunctions(LPFRAMEDEF frame);
void UI_SetText(LPFRAMEDEF, cstring_t, ...);
void UI_SetTextPointer(LPFRAMEDEF, cstring_t);
void UI_SetTexture(LPFRAMEDEF, cstring_t, bool);
void UI_SetTexture2(LPFRAMEDEF, cstring_t, bool);
void UI_InheritFrom(LPFRAMEDEF, cstring_t);
void UI_LoadTheme(cstring_t fileName);
void UI_ClearTheme(void);
void UI_MenuCommandLocal(cstring_t command);
LPCFRAMEDEF UI_HitTest(float fdf_x, float fdf_y);
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

/* ui_render.c — Frame rendering */
void UI_DrawFrame(LPCFRAMEDEF frame);
void UI_DrawGamePortraitInFrame(LPCFRAMEDEF frame, uint32_t modelIndex, cstring_t anim);
void UI_DrawFrames(LPCFRAMEDEF const *roots, uint32_t num_roots);
bool UI_EditKey(int key);
bool UI_MouseEventLocal(uiMouseEvent_t event, int x, int y, int32_t param);
void UI_TextInputLocal(cstring_t text);
bool UI_EditHasFocus(LPCFRAMEDEF frame);
cstring_t UI_EditValue(LPCFRAMEDEF frame);
void UI_SetEditValue(LPFRAMEDEF frame, cstring_t text);
void UI_ClearEditFocus(void);

uiScreen_t *UI_GetCurrentScreen(void);

#endif
