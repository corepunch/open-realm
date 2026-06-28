/*
 * stb_fdf.h — Shared FDF types, frame API, and parser declarations.
 *
 * Both the UI library and game module include this header to share the
 * FRAMEDEF struct, frame creation/lookup functions, and FDF parsing API.
 * The actual parser implementation lives in ui_fdf.c; this header provides
 * the type definitions and function declarations needed by both sides.
 *
 * When STB_FDF_IMPLEMENTATION is defined before including this header in
 * exactly one translation unit, the full parser and frame API implementation
 * is compiled inline. Otherwise, this is a declarations-only header.
 */
#ifndef stb_fdf_h
#define stb_fdf_h

#include "common/shared.h"
#include "shared/types/rect.h"

/* -------------------------------------------------------------------------- */
/* Constants                                                                   */
/* -------------------------------------------------------------------------- */
#define MAX_BUILD_QUEUE 7
#ifndef MAX_UI_CLASSES
#define MAX_UI_CLASSES 4096
#endif
#define UI_BASE_WIDTH 0.8f
#define UI_BASE_HEIGHT 0.6f
#define UI_MIN_ASPECT (4.0f / 3.0f)
#define UI_MAX_MAP_LIST_ITEMS 1024
#define UI_MAX_MENU_ITEMS 32

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                        */
/* -------------------------------------------------------------------------- */
typedef struct uiFrameDef_s frameDef_t;
typedef frameDef_t FRAMEDEF;
typedef frameDef_t *LPFRAMEDEF;
typedef frameDef_t const *LPCFRAMEDEF;

/* -------------------------------------------------------------------------- */
/* Mouse event types (used by frame event_handler)                             */
/* -------------------------------------------------------------------------- */
#ifndef UI_MOUSE_EVENT_DEFINED
#define UI_MOUSE_EVENT_DEFINED
typedef enum {
    UI_MOUSE_MOVE,
    UI_MOUSE_DOWN,
    UI_MOUSE_UP,
    UI_MOUSE_SCROLL,
} uiMouseEvent_t;
#endif

/* -------------------------------------------------------------------------- */
/* Enums                                                                       */
/* -------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------- */
/* Small helper structs                                                        */
/* -------------------------------------------------------------------------- */
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

/* -------------------------------------------------------------------------- */
/* UI interaction flags                                                        */
/* -------------------------------------------------------------------------- */
#define UIFLAG_PRESSED  (1 << 0)
#define UIFLAG_HOVERED  (1 << 1)
#define UIFLAG_CHECKED  (1 << 2)
#define UIFLAG_DISABLED (1 << 3)
#define UIFLAG_ACTIVE   (1 << 4)
#define UIFLAG_VISIBLE  (1 << 5)
#define UIFLAG_PASSTHROUGH (1 << 6)

/* -------------------------------------------------------------------------- */
/* Frame template definition                                                   */
/* -------------------------------------------------------------------------- */
struct uiFrameDef_s {
    LPCFRAMEDEF Parent;
    FRAMETYPE Type;
    UINAME Name;
    UINAME TextStorage;
    UINAME OnClick;
    LPCSTR Text, Tip, Ubertip;
    FLOAT Width, Height;
    COLOR32 Color;
    BLEND_MODE AlphaMode;
    BOOL DecorateFileNames;
    BOOL inuse;
    BOOL AnyPointsSet;
    BOOL hidden;
    BOOL disabled;
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
    /* Interaction state — updated by event handler, read by draw */
    DWORD ui_flags;
    /* Per-type event handler: called from UI_MouseEventLocal */
    void (*event_handler)(LPFRAMEDEF frame, uiMouseEvent_t event, FLOAT fdf_x, FLOAT fdf_y, int32_t param);
    /* Per-type draw function: called from UI_DrawFrameOne */
    void (*draw)(LPCFRAMEDEF frame, LPCRECT rect);
};

/* -------------------------------------------------------------------------- */
/* Global frame table                                                          */
/* -------------------------------------------------------------------------- */
extern FRAMEDEF frames[MAX_UI_CLASSES];

/* -------------------------------------------------------------------------- */
/* Stub types for server-side code compatibility                               */
/* -------------------------------------------------------------------------- */
typedef struct {} uiTrigger_t;
typedef void *LPEDICT;

/* -------------------------------------------------------------------------- */
/* Convenience macros for frame lookup                                         */
/* -------------------------------------------------------------------------- */
#define UI_FRAME_GLOBAL(NAME) LPFRAMEDEF NAME = UI_FindFrame(#NAME);
#define UI_FRAME_CHILD(PARENT, NAME) LPFRAMEDEF NAME = UI_FindChildFrame(PARENT, #NAME);
#define UI_FRAME_SELECT(_1, _2, NAME, ...) NAME
#define UI_FRAME(...) UI_FRAME_SELECT(__VA_ARGS__, UI_FRAME_CHILD, UI_FRAME_GLOBAL)(__VA_ARGS__)
#define UI_CHILD_FRAME(NAME, PARENT) LPFRAMEDEF NAME = UI_FindChildFrame(PARENT, #NAME);

/* -------------------------------------------------------------------------- */
/* FDF bind macros (used by generated headers)                                 */
/* -------------------------------------------------------------------------- */
#ifndef BZ_FDF_REPORT_MISSING
#define BZ_FDF_REPORT_MISSING(NAME) \
    do { fprintf(stderr, "ERROR: missing FDF binding: %s\n", (NAME)); } while (0)
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

/* -------------------------------------------------------------------------- */
/* FDF parser API                                                              */
/* -------------------------------------------------------------------------- */
BOOL UI_EnsureFDF(LPCSTR filename);
void UI_ParseFDF(LPCSTR filename);
void UI_ParseFDF_Buffer(LPCSTR filename, LPSTR buffer);
void UI_ClearTemplates(void);

/* -------------------------------------------------------------------------- */
/* Frame creation and manipulation API                                         */
/* -------------------------------------------------------------------------- */
void UI_InitFrame(LPFRAMEDEF frame, FRAMETYPE type);
LPFRAMEDEF UI_Spawn(FRAMETYPE type, LPFRAMEDEF parent);
LPFRAMEDEF UI_FindFrame(LPCSTR name);
LPFRAMEDEF UI_FindFrameByNumber(DWORD number);
LPFRAMEDEF UI_FindFrameNear(LPCFRAMEDEF anchor, LPCSTR name);
LPFRAMEDEF UI_FindChildFrame(LPFRAMEDEF frame, LPCSTR name);
LPFRAMEDEF UI_CloneFrameTree(LPCFRAMEDEF source, LPFRAMEDEF parent);
DWORD UI_CollectFrameTree(LPCFRAMEDEF root, LPCFRAMEDEF *out, DWORD max);
DWORD UI_FindFrameNumber(LPCSTR name);

void UI_SetPoint(LPFRAMEDEF frame, UIFRAMEPOINT framePoint, LPCFRAMEDEF other, UIFRAMEPOINT otherPoint, FLOAT x, FLOAT y);
void UI_SetAllPoints(LPFRAMEDEF frame);
void UI_SetParent(LPFRAMEDEF frame, LPCFRAMEDEF parent);
void UI_SetText(LPFRAMEDEF frame, LPCSTR format, ...);
void UI_SetOnClick(LPFRAMEDEF frame, LPCSTR format, ...);
void UI_SetEnabled(LPFRAMEDEF frame, BOOL enabled);
void UI_SetTextPointer(LPFRAMEDEF frame, LPCSTR text);
void UI_SetSize(LPFRAMEDEF frame, FLOAT width, FLOAT height);
void UI_SetTexture(LPFRAMEDEF frame, LPCSTR name, BOOL decorate);
void UI_SetTexture2(LPFRAMEDEF frame, LPCSTR name, BOOL decorate);
void UI_SetHidden(LPFRAMEDEF frame, BOOL value);
void UI_InheritFrom(LPFRAMEDEF frame, LPCSTR inheritName);

/* -------------------------------------------------------------------------- */
/* Asset loading (implemented by host module)                                  */
/* -------------------------------------------------------------------------- */
DWORD UI_LoadTexture(LPCSTR file, BOOL decorate);
DWORD UI_LoadModel(LPCSTR file, BOOL decorate);
LPCSTR UI_GetString(LPCSTR textID);
LPCSTR UI_TextureName(DWORD index);
LPCTEXTURE UI_GetTexture(DWORD index);
LPCMODEL UI_GetModel(DWORD index);
DWORD UI_GetTime(void);

/* -------------------------------------------------------------------------- */
/* Theme functions (implemented by host module)                                */
/* -------------------------------------------------------------------------- */
LPCSTR Theme_String(LPCSTR key, LPCSTR fallback);
FLOAT Theme_Float(LPCSTR key, LPCSTR fallback);

/* -------------------------------------------------------------------------- */
/* Map list support                                                            */
/* -------------------------------------------------------------------------- */
void UI_BindMapList(LPFRAMEDEF frame, uiMapListState_t *state, LPCFRAMEDEF label, DWORD visible_rows, LPCSTR select_command);
void UI_MenuClearItems(LPFRAMEDEF frame);
void UI_MenuAddItem(LPFRAMEDEF frame, LPCSTR text, LONG value);

/* -------------------------------------------------------------------------- */
/* Layout serialization (implemented by host module — stubs in client UI)       */
/* -------------------------------------------------------------------------- */
void UI_WriteStart(DWORD layer);
void UI_WriteFrame(LPCFRAMEDEF frame);
void UI_WriteFrameWithChildren(LPCFRAMEDEF frame, LPCFRAMEDEF parent);
void UI_WriteLayout(LPEDICT ent, LPCFRAMEDEF root, DWORD layer);
void UI_WriteWithTriggers(LPEDICT ent, LPCFRAMEDEF root, DWORD layer, uiTrigger_t const *triggers);
void UI_WriteFrameWithChildrenWithTriggers(LPEDICT ent, LPCFRAMEDEF frame, LPCFRAMEDEF parent, uiTrigger_t const *triggers);

#endif /* stb_fdf_h */
