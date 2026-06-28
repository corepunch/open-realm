/*
 * stb_fdf.h — Shared FDF types, frame API, and parser declarations.
 *
 * Both the UI library and game module include this header to share the
 * FRAMEDEF struct, frame creation/lookup functions, and FDF parsing API.
 *
 * Declarations-only mode (default):
 *   Include this header normally to get types and extern declarations.
 *
 * Implementation mode:
 *   #define STB_FDF_IMPLEMENTATION before including this header in exactly
 *   one .c file to get static inline implementations of pure frame helpers.
 *   Functions that depend on host-module services (uiimport, gi) remain as
 *   extern declarations — the host module provides those.
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
#ifndef FRAMEDEF_DEFINED
#define FRAMEDEF_DEFINED
typedef struct uiFrameDef_s frameDef_t;
typedef frameDef_t FRAMEDEF;
typedef frameDef_t *LPFRAMEDEF;
typedef frameDef_t const *LPCFRAMEDEF;
#endif

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
/* Enums — guarded for game module inclusion via g_local.h                     */
/* -------------------------------------------------------------------------- */
#ifndef UIFRAMEPOINT_DEFINED
#define UIFRAMEPOINT_DEFINED
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
#endif

#ifndef UIFONTFLAGS_DEFINED
#define UIFONTFLAGS_DEFINED
typedef enum {
    FONTFLAGS_FIXEDSIZE,
    FONTFLAGS_PASSWORDFIELD,
} UIFONTFLAGS;
#endif

#ifndef HIGHLIGHTTYPE_DEFINED
#define HIGHLIGHTTYPE_DEFINED
typedef enum {
    FILETEXTURE,
} HIGHLIGHTTYPE;
#endif

#ifndef CONTROLSTYLE_DEFINED
#define CONTROLSTYLE_DEFINED
typedef enum {
    AUTOTRACK = 1,
    HIGHLIGHTONFOCUS = 2,
    HIGHLIGHTONMOUSEOVER = 4,
} CONTROLSTYLE;
#endif

#ifndef LAYOUTDIRECTION_DEFINED
#define LAYOUTDIRECTION_DEFINED
typedef enum {
    LAYOUT_HORIZONTAL,
    LAYOUT_VERTICAL,
} LAYOUTDIRECTION;
#endif

/* -------------------------------------------------------------------------- */
/* Small helper structs                                                        */
/* -------------------------------------------------------------------------- */
#ifndef BUTTONTEXT_DEFINED
#define BUTTONTEXT_DEFINED
typedef struct {
    UINAME frame;
    UINAME text;
} BUTTONTEXT;
#endif

#ifndef FRAMEPOINT_DEFINED
#define FRAMEPOINT_DEFINED
typedef struct {
    uiFramePointPos_t targetPos;
    bool used;
    LPCFRAMEDEF relativeTo;
    FLOAT offset;
} FRAMEPOINT;
#endif

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
#ifndef UIFRAMEDEF_S_DEFINED
#define UIFRAMEDEF_S_DEFINED
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
#endif /* UIFRAMEDEF_S_DEFINED */

/* -------------------------------------------------------------------------- */
/* Global frame table                                                          */
/* -------------------------------------------------------------------------- */
extern FRAMEDEF frames[MAX_UI_CLASSES];

/* -------------------------------------------------------------------------- */
/* Types used only by the game module are declared in g_local.h.              */
/* -------------------------------------------------------------------------- */

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
/* Pure helpers are static inline under STB_FDF_IMPLEMENTATION (see below).    */
/* Host-dependent functions remain extern — each module provides its own.      */
/* -------------------------------------------------------------------------- */
LPFRAMEDEF UI_Spawn(FRAMETYPE type, LPFRAMEDEF parent);
LPFRAMEDEF UI_CloneFrameTree(LPCFRAMEDEF source, LPFRAMEDEF parent);
DWORD UI_FindFrameNumber(LPCSTR name);
void UI_SetText(LPFRAMEDEF frame, LPCSTR format, ...);
void UI_SetTextPointer(LPFRAMEDEF frame, LPCSTR text);
void UI_SetTexture(LPFRAMEDEF frame, LPCSTR name, BOOL decorate);
void UI_SetTexture2(LPFRAMEDEF frame, LPCSTR name, BOOL decorate);
void UI_InheritFrom(LPFRAMEDEF frame, LPCSTR inheritName);

/* -------------------------------------------------------------------------- */
/* Asset loading (implemented by host module)                                  */
/* -------------------------------------------------------------------------- */
DWORD UI_LoadTexture(LPCSTR file, BOOL decorate);
DWORD UI_LoadModel(LPCSTR file, BOOL decorate);
LPCSTR UI_GetString(LPCSTR textID);

/* -------------------------------------------------------------------------- */
/* FDF host services (implemented by host module — parser uses these)          */
/* -------------------------------------------------------------------------- */
HANDLE UI_FdfAlloc(long size);
void UI_FdfFree(HANDLE ptr);
DWORD UI_FdfFontIndex(LPCSTR name, DWORD size);
int UI_FdfReadFile(LPCSTR name, HANDLE *out);
void UI_FdfFreeFile(HANDLE buf);

/* -------------------------------------------------------------------------- */
/* Theme functions (implemented by host module)                                */
/* -------------------------------------------------------------------------- */
LPCSTR Theme_String(LPCSTR key, LPCSTR fallback);
FLOAT Theme_Float(LPCSTR key, LPCSTR fallback);

/* -------------------------------------------------------------------------- */
/* Map list support                                                            */
/* -------------------------------------------------------------------------- */
void UI_BindMapList(LPFRAMEDEF frame, uiMapListState_t *state, LPCFRAMEDEF label, DWORD visible_rows, LPCSTR select_command);

/* -------------------------------------------------------------------------- */
/* Layout serialization (implemented by host module — stubs in client UI)       */
/* -------------------------------------------------------------------------- */
void UI_WriteStart(DWORD layer);
void UI_WriteFrame(LPCFRAMEDEF frame);
void UI_WriteFrameWithChildren(LPCFRAMEDEF frame, LPCFRAMEDEF parent);
/* UI_WriteLayout and UI_WriteWithTriggers use LPEDICT and are declared
 * in g_local.h (game module) since they need game types. */

/* -------------------------------------------------------------------------- */
/* Tokenizer (merged from parser.h/parser.c)                                  */
/* -------------------------------------------------------------------------- */
#ifndef WORD_EXTRACTOR_DEFINED
#define WORD_EXTRACTOR_DEFINED
KNOWN_AS(word_extractor, PARSER);
struct word_extractor {
    LPCSTR buffer;
    const char *delimiters;
    BOOL error;
    BOOL eat_quotes;
};
#endif

LPCSTR parse_token(LPPARSER p);
LPCSTR parse_segment(LPPARSER p);
LPCSTR parse_segment2(LPPARSER p);
LPCSTR peek_token(LPPARSER p);
BOOL eat_token(LPPARSER p, LPCSTR value);
void parser_error(LPPARSER parser);

/* -------------------------------------------------------------------------- */
/* Pure frame helpers — extern declarations for non-implementation TUs          */
/* -------------------------------------------------------------------------- */
#ifndef STB_FDF_IMPLEMENTATION
LPFRAMEDEF UI_FindFrame(LPCSTR name);
LPFRAMEDEF UI_FindFrameByNumber(DWORD number);
LPFRAMEDEF UI_FindChildFrame(LPFRAMEDEF frame, LPCSTR name);
LPFRAMEDEF UI_FindFrameNear(LPCFRAMEDEF anchor, LPCSTR name);
void UI_InitFrame(LPFRAMEDEF frame, FRAMETYPE type);
void UI_SetPoint(LPFRAMEDEF frame, UIFRAMEPOINT framePoint, LPCFRAMEDEF other, UIFRAMEPOINT otherPoint, FLOAT x, FLOAT y);
void UI_SetAllPoints(LPFRAMEDEF frame);
void UI_SetParent(LPFRAMEDEF frame, LPCFRAMEDEF parent);
void UI_SetSize(LPFRAMEDEF frame, FLOAT width, FLOAT height);
void UI_SetEnabled(LPFRAMEDEF frame, BOOL enabled);
void UI_SetHidden(LPFRAMEDEF frame, BOOL value);
void UI_SetOnClick(LPFRAMEDEF frame, LPCSTR format, ...);
DWORD UI_CollectFrameTree(LPCFRAMEDEF root, LPCFRAMEDEF *out, DWORD max);
void UI_MenuClearItems(LPFRAMEDEF frame);
void UI_MenuAddItem(LPFRAMEDEF frame, LPCSTR text, LONG value);
#endif /* !STB_FDF_IMPLEMENTATION */

#ifdef STB_FDF_IMPLEMENTATION

#include <ctype.h>

/* ---- Tokenizer (merged from parser.c) ------------------------------------ */

#define PARSER_MAX_SEGMENT 1024

static void parser_skip_ws(LPPARSER p) {
    for (;;) {
        while (isspace((unsigned char)*p->buffer)) ++p->buffer;
        if (p->buffer[0] == '/' && p->buffer[1] == '/') {
            p->buffer += 2;
            while (*p->buffer && *p->buffer != '\n') ++p->buffer;
            continue;
        }
        if (p->buffer[0] == '/' && p->buffer[1] == '*') {
            p->buffer += 2;
            while (*p->buffer) {
                if (p->buffer[0] == '*' && p->buffer[1] == '/') { p->buffer += 2; break; }
                ++p->buffer;
            }
            continue;
        }
        break;
    }
}

static void parser_rtrim(LPSTR s) {
    LPSTR end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
}

LPCSTR parse_token(LPPARSER p) {
    static char word[PARSER_MAX_SEGMENT];
    parser_skip_ws(p);
    if (*p->buffer == '"') {
        LPCSTR close = strchr(p->buffer + 1, '"');
        size_t len = close - p->buffer + 1;
        if (p->eat_quotes) { p->buffer++; len -= 2; }
        memcpy(word, p->buffer, len);
        word[len] = '\0';
        p->buffer = ++close;
        return word;
    } else if (strchr(p->delimiters, *p->buffer)) {
        word[0] = *(p->buffer++);
        word[1] = '\0';
        return word;
    } else {
        size_t n = 0;
        while (*p->buffer && !isspace((unsigned char)*p->buffer) &&
               !strchr(p->delimiters, *p->buffer) && n < PARSER_MAX_SEGMENT - 1)
            word[n++] = *(p->buffer++);
        word[n] = '\0';
        return word;
    }
}

LPCSTR peek_token(LPPARSER p) {
    PARSER tmp = *p;
    LPCSTR tok = parse_token(p);
    *p = tmp;
    return tok;
}

BOOL eat_token(LPPARSER p, LPCSTR value) {
    if (!strcmp(peek_token(p), value)) { parse_token(p); return true; }
    return false;
}

LPCSTR parse_segment(LPPARSER p) {
    static char seg[PARSER_MAX_SEGMENT];
    memset(seg, 0, PARSER_MAX_SEGMENT);
    if (*p->buffer == '\0') return NULL;
    parser_skip_ws(p);
    if (*p->buffer == '\0') return NULL;
    LPCSTR start = p->buffer;
    if (*p->buffer == '"') {
        ++start;
        p->buffer = strchr(start, '"');
        memcpy(seg, start, p->buffer - start);
        seg[p->buffer - start] = '\0';
        p->buffer = strchr(p->buffer, ',');
    } else {
        p->buffer = strchr(p->buffer, ',');
        if (p->buffer) {
            memcpy(seg, start, p->buffer - start);
            seg[p->buffer - start] = '\0';
        } else {
            strcpy(seg, start);
            p->buffer = start + strlen(start);
            return seg;
        }
    }
    ++p->buffer;
    return seg;
}

LPCSTR parse_segment2(LPPARSER p) {
    static char seg[PARSER_MAX_SEGMENT];
    LPSTR out = seg;
    BOOL quoted = false, have = false;
    memset(seg, 0, PARSER_MAX_SEGMENT);
    if (*p->buffer == '\0') return NULL;
    parser_skip_ws(p);
    if (*p->buffer == '\0') return NULL;
    while (*p->buffer) {
        if (!quoted && *p->buffer == ',') { ++p->buffer; break; }
        if (!quoted && p->buffer[0] == '/' && p->buffer[1] == '/') {
            p->buffer += 2;
            while (*p->buffer && *p->buffer != '\n') ++p->buffer;
            if (have) { parser_skip_ws(p); if (*p->buffer == ',') ++p->buffer; break; }
            parser_skip_ws(p); continue;
        }
        if (!quoted && p->buffer[0] == '/' && p->buffer[1] == '*') {
            p->buffer += 2;
            while (*p->buffer) { if (p->buffer[0] == '*' && p->buffer[1] == '/') { p->buffer += 2; break; } ++p->buffer; }
            if (have) { parser_skip_ws(p); if (*p->buffer == ',') ++p->buffer; break; }
            parser_skip_ws(p); continue;
        }
        if (*p->buffer == '"') quoted = !quoted;
        if (out < seg + PARSER_MAX_SEGMENT - 1) *out++ = *p->buffer;
        if (!isspace((unsigned char)*p->buffer)) have = true;
        ++p->buffer;
    }
    *out = '\0';
    parser_rtrim(seg);
    return seg;
}

void parser_error(LPPARSER parser) { parser->error = true; }

void *find_in_array(void const *array, long sizeofelem, LPCSTR name) {
    LPSTR str = (LPSTR)array;
    while (*(LPCSTR *)str) {
        if (!strcmp(*(LPCSTR *)str, name)) return str;
        str += sizeofelem;
    }
    return NULL;
}

/* ---- Small pure helpers --------------------------------------------------- */

static inline BOOL UI_FrameNameEquals(LPCFRAMEDEF frame, LPCSTR name) {
    return frame && name && *name && !strcmp(frame->Name, name);
}

static inline BOOL UI_IsButtonFrameType(FRAMETYPE type) {
    return type == FT_BUTTON ||
           type == FT_TEXTBUTTON ||
           type == FT_GLUETEXTBUTTON ||
           type == FT_GLUEBUTTON ||
           type == FT_GLUEPOPUPMENU ||
           type == FT_SIMPLEBUTTON;
}

static inline BOOL UI_IsCheckBoxFrameType(FRAMETYPE type) {
    return type == FT_CHECKBOX ||
           type == FT_GLUECHECKBOX ||
           type == FT_SIMPLECHECKBOX;
}

static inline DWORD UI_DecodeFramePointY(DWORD framepoint) {
    return (framepoint >> 2) & 3;
}

/* ---- Frame lookup --------------------------------------------------------- */

static inline LPFRAMEDEF UI_FindFrame(LPCSTR name) {
    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (!strcmp(frames[i].Name, name)) {
            return frames + i;
        }
    }
    return NULL;
}

static inline LPFRAMEDEF UI_FindFrameByNumber(DWORD number) {
    if (number < MAX_UI_CLASSES && frames[number].inuse) {
        return &frames[number];
    }
    return NULL;
}

static inline LPFRAMEDEF UI_FindChildFrame(LPFRAMEDEF frame, LPCSTR name) {
    if (!strcmp(frame->Name, name))
        return frame;
    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (frames[i].Parent != frame)
            continue;
        LPFRAMEDEF found = UI_FindChildFrame(frames + i, name);
        if (found)
            return found;
    }
    return NULL;
}

static inline LPFRAMEDEF UI_FindFrameNear(LPCFRAMEDEF anchor, LPCSTR name) {
    if (!name || !*name) {
        return NULL;
    }
    if (!anchor || anchor < frames || anchor >= frames + MAX_UI_CLASSES) {
        return UI_FindFrame(name);
    }
    LPFRAMEDEF child = UI_FindChildFrame((LPFRAMEDEF)anchor, name);
    if (child) {
        return child;
    }
    DWORD const anchor_index = (DWORD)(anchor - frames);
    DWORD best_distance = MAX_UI_CLASSES;
    LPFRAMEDEF best = NULL;
    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (!strcmp(frames[i].Name, name)) {
            DWORD const distance = i > anchor_index ? i - anchor_index : anchor_index - i;
            if (!best || distance < best_distance) {
                best = frames + i;
                best_distance = distance;
            }
        }
    }
    return best;
}

/* ---- Frame initialization ------------------------------------------------- */

static inline void UI_InitFrame(LPFRAMEDEF frame, FRAMETYPE type) {
    memset(frame, 0, sizeof(FRAMEDEF));
    frame->inuse = true;
    frame->Type = type;
    frame->Color = COLOR32_WHITE;
    frame->Text = frame->TextStorage;
    switch (type) {
        case FT_TEXTURE:
        case FT_SIMPLESTATUSBAR:
        case FT_COMMANDBUTTON:
        case FT_BACKDROP:
            frame->Texture.TexCoord.max.x = 1;
            frame->Texture.TexCoord.max.y = 1;
            break;
        case FT_STRING:
        case FT_TEXT:
            frame->Font.Color = COLOR32_WHITE;
            break;
        default:
            break;
    }
}

/* ---- Point manipulation --------------------------------------------------- */

static inline void UI_ApplyFramePoints(LPFRAMEDEF frame) {
    if (!frame->AnyPointsSet) {
        memset(&frame->Points, 0, sizeof(frame->Points));
        frame->AnyPointsSet = true;
    }
    DWORD const x = frame->SetPoint.type & 3;
    FRAMEPOINT *xp = frame->Points.x;
    if (x != FPP_MID || (!xp[FPP_MIN].used && !xp[FPP_MAX].used)) {
        xp[FPP_MID].used = false;
        xp[x].used = true;
        xp[x].offset = frame->SetPoint.x;
        xp[x].targetPos = frame->SetPoint.target & 3;
        xp[x].relativeTo = frame->SetPoint.relativeTo;
    }
    DWORD y = UI_DecodeFramePointY(frame->SetPoint.type);
    FRAMEPOINT *yp = frame->Points.y;
    if (y != FPP_MID || (!yp[FPP_MIN].used && !yp[FPP_MAX].used)) {
        yp[FPP_MID].used = false;
        yp[y].used = true;
        yp[y].offset = frame->SetPoint.y;
        yp[y].targetPos = UI_DecodeFramePointY(frame->SetPoint.target);
        yp[y].relativeTo = frame->SetPoint.relativeTo;
    }
}

static inline void UI_SetPoint(LPFRAMEDEF frame,
                               UIFRAMEPOINT framePoint,
                               LPCFRAMEDEF other,
                               UIFRAMEPOINT otherPoint,
                               FLOAT x, FLOAT y)
{
    frame->SetPoint.type = framePoint;
    frame->SetPoint.relativeTo = other;
    frame->SetPoint.target = otherPoint;
    frame->SetPoint.x = x;
    frame->SetPoint.y = y;
    UI_ApplyFramePoints(frame);
}

static inline void UI_SetAllPoints(LPFRAMEDEF frame) {
    UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0, 0);
    UI_SetPoint(frame, FRAMEPOINT_BOTTOMRIGHT, NULL, FRAMEPOINT_BOTTOMRIGHT, 0, 0);
}

/* ---- Simple property setters ---------------------------------------------- */

static inline void UI_SetParent(LPFRAMEDEF frame, LPCFRAMEDEF parent) {
    frame->Parent = parent;
}

static inline void UI_SetSize(LPFRAMEDEF frame, FLOAT width, FLOAT height) {
    frame->Width = width;
    frame->Height = height;
}

static inline void UI_SetEnabled(LPFRAMEDEF frame, BOOL enabled) {
    if (!frame) return;
    frame->disabled = !enabled;
    if (frame->disabled) frame->ui_flags |= UIFLAG_DISABLED;
    else frame->ui_flags &= ~UIFLAG_DISABLED;
}

static inline void UI_SetHidden(LPFRAMEDEF frame, BOOL value) {
    if (!frame) return;
    frame->hidden = value;
    if (frame->hidden) frame->ui_flags &= ~UIFLAG_VISIBLE;
    else frame->ui_flags |= UIFLAG_VISIBLE;
}

static inline void UI_SetOnClick(LPFRAMEDEF frame, LPCSTR format, ...) {
    va_list argptr;
    if (!frame || !format) return;
    va_start(argptr, format);
    vsnprintf(frame->OnClick, sizeof(frame->OnClick), format, argptr);
    va_end(argptr);
}

/* ---- Embedded control detection ------------------------------------------- */

static inline BOOL UI_IsEmbeddedControlPart(LPCFRAMEDEF parent, LPCFRAMEDEF child) {
    if (!parent || !child) return false;
    if (child->Type == FT_BACKDROP) {
        return UI_FrameNameEquals(child, parent->Control.Backdrop.Normal) ||
               UI_FrameNameEquals(child, parent->Control.Backdrop.Pushed) ||
               UI_FrameNameEquals(child, parent->Control.Backdrop.Disabled) ||
               UI_FrameNameEquals(child, parent->Control.Backdrop.DisabledPushed);
    }
    if (child->Type == FT_HIGHLIGHT) {
        return UI_FrameNameEquals(child, parent->Control.Backdrop.MouseOver) ||
               (UI_IsCheckBoxFrameType(parent->Type) &&
                (UI_FrameNameEquals(child, parent->CheckBox.CheckHighlight) ||
                 UI_FrameNameEquals(child, parent->CheckBox.DisabledCheckHighlight)));
    }
    if (child->Type == FT_TEXT) {
        if (UI_IsButtonFrameType(parent->Type) &&
            (UI_FrameNameEquals(child, parent->Text) ||
             UI_FrameNameEquals(child, parent->Button.NormalText.frame))) {
            return true;
        }
        return UI_FrameNameEquals(child, parent->Edit.TextFrame);
    }
    if (parent->Type == FT_SLIDER && UI_IsButtonFrameType(child->Type)) {
        return UI_FrameNameEquals(child, parent->Slider.ThumbButtonFrame) ||
               UI_FrameNameEquals(child, parent->Slider.IncButtonFrame) ||
               UI_FrameNameEquals(child, parent->Slider.DecButtonFrame);
    }
    return false;
}

/* ---- Frame tree collection ------------------------------------------------ */

static inline DWORD UI_CollectFrameTreeRecursiveEx(LPCFRAMEDEF frame,
                                                    LPCFRAMEDEF *out,
                                                    DWORD max,
                                                    BOOL include_embedded)
{
    DWORD total = 0;
    if (!frame) return 0;
    if (out && total < max) out[total] = frame;
    total++;
    FOR_LOOP(i, MAX_UI_CLASSES) {
        LPCFRAMEDEF child = frames + i;
        if (child->Parent == frame &&
            (include_embedded || (!child->hidden && !UI_IsEmbeddedControlPart(frame, child)))) {
            DWORD emitted = UI_CollectFrameTreeRecursiveEx(child,
                                                           out ? out + total : NULL,
                                                           max > total ? max - total : 0,
                                                           include_embedded);
            total += emitted;
        }
    }
    return total;
}

static inline DWORD UI_CollectFrameTree(LPCFRAMEDEF root, LPCFRAMEDEF *out, DWORD max) {
    return UI_CollectFrameTreeRecursiveEx(root, out, max, false);
}

/* ---- Menu helpers --------------------------------------------------------- */

static inline void UI_MenuClearItems(LPFRAMEDEF frame) {
    if (!frame) return;
    frame->Menu.ItemCount = 0;
    memset(frame->Menu.Items, 0, sizeof(frame->Menu.Items));
}

static inline void UI_MenuAddItem(LPFRAMEDEF frame, LPCSTR text, LONG value) {
    if (!frame || frame->Menu.ItemCount >= UI_MAX_MENU_ITEMS) return;
    uiMenuItem_t *item = &frame->Menu.Items[frame->Menu.ItemCount++];
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
    item->value = value;
    snprintf(frame->Menu.Item.Text, sizeof(frame->Menu.Item.Text), "%s", item->text);
    frame->Menu.Item.Value = (DWORD)value;
}


#endif /* STB_FDF_IMPLEMENTATION */

#endif /* stb_fdf_h */
