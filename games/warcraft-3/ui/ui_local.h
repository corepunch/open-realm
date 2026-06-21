/*
 * ui_local.h — UI library internal types and declarations.
 *
 * This file contains the internal data structures, function prototypes, and
 * constants used within the UI library. External code should only include
 * ui.h, never this file.
 *
 * Frame template structures (FRAMEDEF, frameDef_t) are defined here.
 * These are used internally by the UI library to build frame hierarchies
 * from FDF files.
 */
#ifndef ui_local_h
#define ui_local_h

#include <stdio.h>

#include "client/ui.h"
#include "common/shared.h"  /* For PLAYERSTATE/PLAYERTEXT enums */
#include "games/warcraft-3/common/mapinfo.h"

/* Constants */
#define MAX_BUILD_QUEUE 7
/* MAX_SELECTED_ENTITIES is defined in common/shared.h */
#ifndef MAX_UI_CLASSES
#define MAX_UI_CLASSES 4096
#endif

#define UI_BASE_WIDTH 0.8f
#define UI_BASE_HEIGHT 0.6f
#define UI_MIN_ASPECT (4.0f / 3.0f)

/* Forward declarations */
typedef struct uiFrameDef_s frameDef_t;
typedef frameDef_t FRAMEDEF;  /* Also available as non-pointer type */
typedef frameDef_t *LPFRAMEDEF;
typedef frameDef_t const *LPCFRAMEDEF;
typedef struct uiScreen_s uiScreen_t;  /* Defined in ui_screen.h */

/* Global import callbacks (filled by UI_GetAPI) */
extern uiImport_t uiimport;

/* Frame point positioning */
typedef enum {
    FRAMEPOINT_TOPLEFT,
    FRAMEPOINT_TOP,
    FRAMEPOINT_TOPRIGHT,
    FRAMEPOINT_UNUSED1,
    FRAMEPOINT_LEFT,
    FRAMEPOINT_CENTER,
    FRAMEPOINT_RIGHT,
    FRAMEPOINT_UNUSED2,
    FRAMEPOINT_BOTTOMLEFT,
    FRAMEPOINT_BOTTOM,
    FRAMEPOINT_BOTTOMRIGHT,
    FRAMEPOINT_UNUSED3,
} UIFRAMEPOINT;

typedef enum {
    FONTFLAGS_FIXEDSIZE,
    FONTFLAGS_PASSWORDFIELD,
} UIFONTFLAGS;

typedef enum {
    FILETEXTURE,
} HIGHLIGHTTYPE;

typedef enum {
    AUTOTRACK = 1,
    HIGHLIGHTONFOCUS = 2,
    HIGHLIGHTONMOUSEOVER = 4,
} CONTROLSTYLE;

typedef enum {
    LAYOUT_HORIZONTAL,
    LAYOUT_VERTICAL,
} LAYOUTDIRECTION;

typedef struct {
    UINAME frame;
    UINAME text;
} BUTTONTEXT;

typedef struct {
    uiFramePointPos_t targetPos;
    bool used;
    LPCFRAMEDEF relativeTo;
    FLOAT offset;
} FRAMEPOINT;

typedef FRAMEPOINT const *LPCFRAMEPOINT;

#define UI_MAX_MAP_LIST_ITEMS 1024
#define UI_MAX_MENU_ITEMS 32

typedef struct {
    UINAME text;
    LONG value;
} uiMenuItem_t;

typedef struct {
    PATHSTR path;
    char name[128];
    char description[512];
    char suggestedPlayers[96];
    char mapSize[32];
    char tileset[64];
    DWORD players;
    DWORD flags;
} uiMapListItem_t;

typedef struct {
    uiMapListItem_t items[UI_MAX_MAP_LIST_ITEMS];
    DWORD count;
    DWORD selected;
    DWORD scroll;
    FLOAT visualScroll;
} uiMapListState_t;

typedef struct {
    uiMapListState_t *State;
    DWORD VisibleRows;
    FLOAT RowHeight;
    FLOAT InsetX;
    FLOAT InsetY;
    UINAME SelectCommand;
    UINAME FontName;
    FLOAT FontSize;
    COLOR32 TextColor;
    COLOR32 SelectedTextColor;
} uiMapListControl_t;

/* Frame template definition (server-side/library-side only) */
struct uiFrameDef_s {
    uiBaseFrame_t base;             /* shared engine frame — keep in sync with common/shared.h */
    LPCFRAMEDEF Parent;
    UINAME Name;
    UINAME TextStorage;
    UINAME OnClick;
    LPCSTR Tip, Ubertip;
    FLOAT Width, Height;           /* mirrored in base.size — kept for FDF parser offset macro */
    BLEND_MODE AlphaMode;
    BOOL DecorateFileNames;
    BOOL inuse;
    BOOL AnyPointsSet;
    DWORD TextLength;
    DWORD Stat;
    LPSTR DynamicText;
    DWORD DynamicTextCapacity;
    struct {
        FRAMEPOINT x[FPP_COUNT];
        FRAMEPOINT y[FPP_COUNT];
    } Points;
    struct {
        DWORD Image;
        DWORD Image2;
        BOX2 TexCoord;
    } Texture;
    struct {
        BOOL TileBackground;
        DWORD Background;
        DWORD CornerFlags;
        FLOAT CornerSize;
        FLOAT BackgroundSize;
        FLOAT BackgroundInsets[4];
        DWORD EdgeFile;
        BOOL BlendAll;
        BOOL Mirrored;
    } Backdrop;
    UINAME DialogBackdropName;
    LPCFRAMEDEF DialogBackdrop;
    struct {
        DWORD model;
    } Portrait;
    struct {
        UIFRAMEPOINT corner;
        FLOAT x, y;
    } Anchor;
    struct {
        UIFRAMEPOINT type;
        LPCFRAMEDEF relativeTo;
        UIFRAMEPOINT target;
        FLOAT x, y;
    } SetPoint;
    struct {
        UINAME Name;
        UINAME Unknown;
        UIFONTFLAGS FontFlags;
        FLOAT Size;
        DWORD Index;
        COLOR32 Color;
        COLOR32 HighlightColor;
        COLOR32 DisabledColor;
        COLOR32 ShadowColor;
        VECTOR2 ShadowOffset;
        struct {
            VECTOR2 Offset;
            uiFontJustificationH_t Horizontal;
            uiFontJustificationV_t Vertical;
        } Justification;
    } Font;
    struct {
        HIGHLIGHTTYPE Type;
        DWORD AlphaFile;
        BLEND_MODE AlphaMode;
        COLOR32 Color;
    } Highlight;
    struct {
        VECTOR2 PushedTextOffset;
        UINAME NormalTexture;
        UINAME PushedTexture;
        UINAME DisabledTexture;
        UINAME UseHighlight;
        BUTTONTEXT NormalText;
        BUTTONTEXT DisabledText;
        BUTTONTEXT HighlightText;
    } Button;
    struct {
        DWORD Style;
        struct {
            UINAME Normal;
            UINAME Pushed;
            UINAME Disabled;
            UINAME MouseOver;
            UINAME DisabledPushed;
            UINAME Focus;
        } Backdrop;
        UINAME ShortcutKey;
        UINAME TabFocusNext;
        BOOL TabFocusDefault;
    } Control;
    struct {
        FLOAT InitialValue;
        LAYOUTDIRECTION Layout;
        FLOAT MaxValue;
        FLOAT MinValue;
        FLOAT StepSize;
        UINAME ThumbButtonFrame;
        UINAME IncButtonFrame;
        UINAME DecButtonFrame;
    } Slider;
    struct {
        FLOAT Border;
        UINAME ScrollBar;
        UINAME FetchCommand;
    } ListBox;
    uiMapListControl_t MapListControl;
    struct {
        FLOAT Border;
        struct {
            UINAME Text;
            DWORD Value;
            FLOAT Height;
        } Item;
        DWORD ItemCount;
        uiMenuItem_t Items[UI_MAX_MENU_ITEMS];
        COLOR32 TextHighlightColor;
    } Menu;
    struct {
        FLOAT BorderSize;
        COLOR32 CursorColor;
        COLOR32 HighlightColor;
        BOOL HighlightInitial;
        DWORD MaxChars;
        BOOL Focus;
        UINAME Text;
        COLOR32 TextColor;
        UINAME TextFrame;
        VECTOR2 TextOffset;
    } Edit;
    struct {
        UINAME ArrowFrame;
        UINAME MenuFrame;
        UINAME TitleFrame;
        FLOAT ButtonInset;
    } Popup;
    struct {
        FLOAT LineHeight;
        FLOAT LineGap;
        FLOAT Inset;
        DWORD MaxLines;
        UINAME ScrollBar;
    } TextArea;
    struct {
        UINAME CheckHighlight;
        UINAME DisabledCheckHighlight;
        BOOL Checked;
    } CheckBox;
    struct {
        LPCFRAMEDEF FirstItem;
        LPCFRAMEDEF BuildTimer;
        FLOAT ItemOffset;
        DWORD NumQueue;
        uiBuildQueueItem_t Queue[MAX_BUILD_QUEUE];
    } BuildQueue;
    struct {
        DWORD HpBar;
        DWORD ManaBar;
        VECTOR2 Offset;
        DWORD NumColumns;
        DWORD NumItems;
        uiMultiselectItem_t Items[MAX_SELECTED_ENTITIES];
    } Multiselect;
};

/* Global parsed FDF frame table. */
extern FRAMEDEF frames[MAX_UI_CLASSES];

/* Stub types for server-side code compatibility (not used in client UI library) */
typedef struct {} uiTrigger_t;
typedef void *LPEDICT;

/* Convenience macros for frame lookup */
#define UI_FRAME_GLOBAL(NAME) LPFRAMEDEF NAME = UI_FindFrame(#NAME);
#define UI_FRAME_CHILD(PARENT, NAME) LPFRAMEDEF NAME = UI_FindChildFrame(PARENT, #NAME);
#define UI_FRAME_SELECT(_1, _2, NAME, ...) NAME
#define UI_FRAME(...) UI_FRAME_SELECT(__VA_ARGS__, UI_FRAME_CHILD, UI_FRAME_GLOBAL)(__VA_ARGS__)
#define UI_CHILD_FRAME(NAME, PARENT) LPFRAMEDEF NAME = UI_FindChildFrame(PARENT, #NAME);

/* Internal function prototypes */

/* ui_main.c */
void UI_InitLocal(void);
void UI_ShutdownLocal(void);
void UI_RefreshLocal(DWORD msec);
void UI_LayoutSetMouseState(VECTOR2 fdf_pos, BOOL button_held);
void UI_SanitizeMapListField(LPSTR text);
void UI_SanitizeMapInfoText(LPSTR text);
LPCSTR UI_MapTilesetName(BYTE tileset);
LPCSTR UI_MapSizeName(DWORD width, DWORD height);
void UI_LayoutDrawOverlays(void);
void UI_LayoutSetMouseState(VECTOR2 fdf_pos, BOOL button_held);
void UI_LayoutSetLayer(DWORD layer, HANDLE data);
void UI_LayoutClearLayer(DWORD layer);
BOOL UI_LayoutHitTest(int x, int y);

/* ui_glue_scene.c */
void UI_ResetGlueSceneModels(void);
void UI_PreloadGlueSceneModels(void);
void UI_DrawGlueScene(LPCSTR panel_anim);
void UI_DrawGlueSceneLayers(LPCSTR left_panel_anim, LPCSTR right_panel_anim);
LPCSTR UI_GlueBackgroundPath(void);
LPCSTR UI_GlueTopLeftPanelPath(void);
LPCSTR UI_GlueTopRightPanelPath(void);
void UI_SpawnGlueSceneFrames(LPFRAMEDEF root, LPCSTR left_anim, LPCSTR right_anim,
                              LPFRAMEDEF *out_bg, LPFRAMEDEF *out_left, LPFRAMEDEF *out_right);
void UI_SetGlueSceneAnim(LPFRAMEDEF bg, LPFRAMEDEF left, LPFRAMEDEF right,
                          LPCSTR left_anim, LPCSTR right_anim);

/* ui_fdf.c — FDF parsing (moved from game/ui/ui_fdf.c) */
BOOL UI_EnsureFDF(LPCSTR filename);
void UI_ParseFDF(LPCSTR filename);
void UI_ParseFDF_Buffer(LPCSTR filename, LPSTR buffer);
void UI_ClearTemplates(void);
void UI_InitFrame(LPFRAMEDEF, FRAMETYPE);
void UI_WireFrameTypeFunctions(LPFRAMEDEF frame);
void UI_GenericOnDraw(struct uiBaseFrame_s *base, LPCRECT rect);
void UI_SetAllPoints(LPFRAMEDEF);
void UI_SetParent(LPFRAMEDEF, LPCFRAMEDEF);
void UI_SetText(LPFRAMEDEF, LPCSTR, ...);
void UI_SetOnClick(LPFRAMEDEF, LPCSTR, ...);
void UI_SetEnabled(LPFRAMEDEF, BOOL);
void UI_SetTextPointer(LPFRAMEDEF, LPCSTR);
void UI_SetSize(LPFRAMEDEF, FLOAT, FLOAT);
void UI_SetPoint(LPFRAMEDEF, UIFRAMEPOINT, LPCFRAMEDEF, UIFRAMEPOINT, FLOAT, FLOAT);
void UI_SetTexture(LPFRAMEDEF, LPCSTR, BOOL);
void UI_SetTexture2(LPFRAMEDEF, LPCSTR, BOOL);
void UI_SetHidden(LPFRAMEDEF, BOOL);
void UI_SetRootFramesHidden(BOOL);
void UI_InheritFrom(LPFRAMEDEF, LPCSTR);
void UI_UpdateFrameHierarchyFlags(void);
void UI_UpdateStandaloneFrameDraws(void);
void UI_LoadTheme(LPCSTR fileName);
void UI_ClearTheme(void);
void UI_MenuCommandLocal(LPCSTR command);
VECTOR2 UI_MouseToFdf(void);
BOOL UI_MouseContains(LPCRECT rect);
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
void UI_PopupSelectItem(FLOAT fdf_x, FLOAT fdf_y);
DWORD UI_FindFrameNumber(LPCSTR);
DWORD UI_CollectFrameTree(LPCFRAMEDEF root, LPCFRAMEDEF *out, DWORD max);
DWORD UI_LoadTexture(LPCSTR, BOOL);
LPCSTR UI_TextureName(DWORD index);
LPCTEXTURE UI_GetTexture(DWORD index);
LPCMODEL UI_GetModel(DWORD index);
DWORD UI_LoadModel(LPCSTR file, BOOL decorate);
LPCSTR UI_GetString(LPCSTR);
LPFRAMEDEF UI_Spawn(FRAMETYPE, LPFRAMEDEF);
LPFRAMEDEF UI_FindFrame(LPCSTR);
LPFRAMEDEF UI_FindFrameNear(LPCFRAMEDEF, LPCSTR);
LPFRAMEDEF UI_FindChildFrame(LPFRAMEDEF, LPCSTR);
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
void UI_MenuClearItems(LPFRAMEDEF frame);
void UI_MenuAddItem(LPFRAMEDEF frame, LPCSTR text, LONG value);
void UI_LayoutMapInfoPane(LPFRAMEDEF frame);
LPCSTR Theme_String(LPCSTR, LPCSTR);
FLOAT Theme_Float(LPCSTR, LPCSTR);
COLOR32 Theme_ListBoxSelectionColor(void);
COLOR32 Theme_ListBoxTextColor(void);
COLOR32 Theme_ListBoxSelectedTextColor(void);
COLOR32 Theme_ListBoxIconTextColor(void);

/* ui_frame.c — Frame tree manipulation (to be created) */
// Additional frame management functions will be declared here

/* ui_render.c — Frame rendering */
void UI_UpdateFrameScreenRects(void);
void UI_DrawFrame(LPCFRAMEDEF frame);
void UI_DrawFrames(LPCFRAMEDEF const *roots, DWORD num_roots);
BOOL UI_EditKey(int key);
void UI_TextInputLocal(LPCSTR text);
BOOL UI_EditHasFocus(LPCFRAMEDEF frame);
LPCSTR UI_EditValue(LPCFRAMEDEF frame);
void UI_SetEditValue(LPFRAMEDEF frame, LPCSTR text);
void UI_ClearEditFocus(void);

uiScreen_t *UI_GetCurrentScreen(void);

#endif
