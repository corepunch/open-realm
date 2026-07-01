/*
 * stb_sc2layout.h — Shared SC2 .SC2Layout types and frame API.
 *
 * Both the UI library and game module include this header to share the
 * SC2FRAMEDEF struct, frame lookup functions, and .SC2Layout parsing API.
 *
 * Declarations-only mode (default):
 *   Include this header normally to get types and extern declarations.
 *
 * Implementation mode:
 *   #define STB_SC2LAYOUT_IMPLEMENTATION before including this header to get
 *   static inline implementations of pure frame helpers (SC2_InitFrame,
 *   SC2_SetPoint, etc.).  Functions that touch global state or need host
 *   services remain as extern declarations.
 */
#ifndef stb_sc2layout_h
#define stb_sc2layout_h

#include "common/shared.h"

/* -------------------------------------------------------------------------- */
/* Constants                                                                   */
/* -------------------------------------------------------------------------- */
/* SC2 virtual screen resolution — native layout coordinate space. */
#define SC2_VIRT_W 1600.0f
#define SC2_VIRT_H 1200.0f

/* -------------------------------------------------------------------------- */
/* Forward declarations                                                        */
/* -------------------------------------------------------------------------- */
#ifndef SC2FRAMEDEF_DEFINED
#define SC2FRAMEDEF_DEFINED
typedef struct sc2FrameDef_s sc2FrameDef_t;
typedef sc2FrameDef_t SC2FRAMEDEF;
typedef sc2FrameDef_t *LPSC2FRAMEDEF;
typedef sc2FrameDef_t const *LPCSC2FRAMEDEF;
#endif

/* -------------------------------------------------------------------------- */
/* SC2 anchor enums                                                            */
/* -------------------------------------------------------------------------- */
#ifndef SC2ANCHORSIDE_DEFINED
#define SC2ANCHORSIDE_DEFINED
typedef enum {
    SC2_ANCHOR_TOP,
    SC2_ANCHOR_BOTTOM,
    SC2_ANCHOR_LEFT,
    SC2_ANCHOR_RIGHT,
    SC2_ANCHOR_COUNT,
} SC2ANCHORSIDE;
#endif

#ifndef SC2ANCHORPOS_DEFINED
#define SC2ANCHORPOS_DEFINED
typedef enum {
    SC2_APOS_MIN,
    SC2_APOS_MID,
    SC2_APOS_MAX,
    SC2_APOS_COUNT,
} SC2ANCHORPOS;
#endif

/* -------------------------------------------------------------------------- */
/* SC2 anchor point (one of 4: Top/Bottom/Left/Right side)                     */
/* -------------------------------------------------------------------------- */
typedef struct {
    BOOL used;
    SC2ANCHORSIDE side;
    SC2ANCHORPOS pos;
    int16_t offset;
    LPCSC2FRAMEDEF relative;
} sc2Anchor_t;

/* -------------------------------------------------------------------------- */
/* SC2 texture reference                                                       */
/* -------------------------------------------------------------------------- */
typedef struct {
    UINAME resource;
    DWORD image;            /* resolved image index */
    int layer;
    BOOL tiled;
    BOOL has_texture;
} sc2Texture_t;

/* -------------------------------------------------------------------------- */
/* SC2 backdrop                                                                */
/* -------------------------------------------------------------------------- */
typedef struct {
    DWORD bg;               /* background image index */
    DWORD edge;             /* border image index */
    BOOL tile;
    FLOAT insets[4];
} sc2Backdrop_t;

/* -------------------------------------------------------------------------- */
/* SC2 font info                                                               */
/* -------------------------------------------------------------------------- */
typedef struct {
    UINAME name;
    FLOAT size;
    DWORD index;
    COLOR32 color;
    COLOR32 highlight_color;
    COLOR32 disabled_color;
    struct {
        uiFontJustificationH_t horizontal;
        uiFontJustificationV_t vertical;
        VECTOR2 offset;
    } justify;
} sc2Font_t;

/* -------------------------------------------------------------------------- */
/* Frame template definition (mirrors FRAMEDEF from stb_fdf.h)                 */
/* -------------------------------------------------------------------------- */
#ifndef SC2FRAMEDEF_S_DEFINED
#define SC2FRAMEDEF_S_DEFINED
struct sc2FrameDef_s {
    LPCSC2FRAMEDEF Parent;
    FRAMETYPE Type;
    UINAME Name;
    UINAME OnClick;
    LPCSTR Text, Tip, Ubertip;
    FLOAT Width, Height;
    COLOR32 Color;
    FLOAT Alpha;
    BOOL inuse;
    BOOL hidden;
    BOOL disabled;
    DWORD TextLength;
    DWORD Stat;                         /* playerState_t.stats[] index for dynamic data */
    LPSTR DynamicText;
    DWORD DynamicTextCapacity;

    /* Anchors (Min/Mid/Max x/y like FRAMEDEF) */
    struct {
        struct {
            uiFramePointPos_t targetPos;
            BOOL used;
            LPCSC2FRAMEDEF relativeTo;
            FLOAT offset;
        } x[FPP_COUNT], y[FPP_COUNT];
    } Points;

    /* Raw SC2 anchors (before conversion to Points) */
    sc2Anchor_t anchors[4];
    int num_anchors;

    /* Textures */
    sc2Texture_t textures[4];
    int num_textures;

    /* Backdrop */
    sc2Backdrop_t backdrop;

    /* Font */
    sc2Font_t font;

    /* Template path for inheritance */
    UINAME template_path;

    /* Resolved index for wire transmission */
    int resolved_index;
};
#endif /* SC2FRAMEDEF_S_DEFINED */

/* -------------------------------------------------------------------------- */
/* UI flags                                                                    */
/* -------------------------------------------------------------------------- */
#define SC2_UIF_HIDDEN   (1 << 10)   /* bit 10+ to avoid overlap with uiFrame_t.flags.type (0-7) and alphaMode (8-9) */
#define SC2_UIF_DISABLED (1 << 11)
#define SC2_UIF_PRESSED  (1 << 12)
#define SC2_UIF_HOVERED  (1 << 13)

/* -------------------------------------------------------------------------- */
/* Bind macros                                                                 */
/* -------------------------------------------------------------------------- */
#ifndef BZ_SC2_REPORT_MISSING
#define BZ_SC2_REPORT_MISSING(NAME) \
    fprintf(stderr, "SC2_Layout: missing frame '%s' in %s\n", (NAME), __FUNCTION__)
#endif

#ifndef BZ_SC2_BIND_ROOT
#define BZ_SC2_BIND_ROOT(OUT, FIELD, NAME) \
    do { (OUT)->FIELD = SC2_FindFrame((NAME)); if (!(OUT)->FIELD) { BZ_SC2_REPORT_MISSING((NAME)); ok = false; } } while (0)
#endif

#ifndef BZ_SC2_BIND_CHILD
#define BZ_SC2_BIND_CHILD(OUT, FIELD, PARENT, NAME) \
    do { (OUT)->FIELD = (PARENT) ? SC2_FindChildFrame((PARENT), (NAME)) : NULL; if (!(OUT)->FIELD) { BZ_SC2_REPORT_MISSING((NAME)); ok = false; } } while (0)
#endif

#ifndef BZ_SC2_BIND_CHILD_OPTIONAL
#define BZ_SC2_BIND_CHILD_OPTIONAL(OUT, FIELD, PARENT, NAME) \
    do { (OUT)->FIELD = (PARENT) ? SC2_FindChildFrame((PARENT), (NAME)) : NULL; } while (0)
#endif

/* -------------------------------------------------------------------------- */
/* Host service declarations (provided by host module)                         */
/* -------------------------------------------------------------------------- */
HANDLE SC2_Alloc(long size);
void   SC2_Free(HANDLE ptr);
DWORD  SC2_FontIndex(LPCSTR name, DWORD size);
int    SC2_ReadFile(LPCSTR name, HANDLE *out);
void   SC2_FreeFile(HANDLE buf);

/* -------------------------------------------------------------------------- */
/* API declarations                                                           */
/* -------------------------------------------------------------------------- */
BOOL         SC2_EnsureLayout(LPCSTR filename);
LPSC2FRAMEDEF  SC2_FindFrame(LPCSTR name);
LPSC2FRAMEDEF  SC2_FindChildFrame(LPCSC2FRAMEDEF parent, LPCSTR name);
LPSC2FRAMEDEF  SC2_FindFrameByNumber(DWORD number);
void         SC2_InitFrame(LPSC2FRAMEDEF frame, FRAMETYPE type);
void         SC2_SetPoint(LPSC2FRAMEDEF frame, uiFramePointPos_t framePointX, uiFramePointPos_t framePointY, LPCSC2FRAMEDEF other, uiFramePointPos_t otherPoint, FLOAT x, FLOAT y);
void         SC2_SetAllPoints(LPSC2FRAMEDEF frame);
void         SC2_SetParent(LPSC2FRAMEDEF frame, LPCSC2FRAMEDEF parent);
void         SC2_SetSize(LPSC2FRAMEDEF frame, FLOAT width, FLOAT height);
void         SC2_SetHidden(LPSC2FRAMEDEF frame, BOOL value);
void         SC2_SetEnabled(LPSC2FRAMEDEF frame, BOOL enabled);
void         SC2_SetText(LPSC2FRAMEDEF frame, LPCSTR format, ...);

/* -------------------------------------------------------------------------- */
/* Pure frame helpers — extern declarations for non-implementation TUs          */
/* -------------------------------------------------------------------------- */
#ifndef STB_SC2LAYOUT_IMPLEMENTATION
void SC2_InitFrame(LPSC2FRAMEDEF frame, FRAMETYPE type);
void SC2_SetPoint(LPSC2FRAMEDEF frame, uiFramePointPos_t framePointX, uiFramePointPos_t framePointY, LPCSC2FRAMEDEF other, uiFramePointPos_t otherPoint, FLOAT x, FLOAT y);
void SC2_SetAllPoints(LPSC2FRAMEDEF frame);
void SC2_SetParent(LPSC2FRAMEDEF frame, LPCSC2FRAMEDEF parent);
void SC2_SetSize(LPSC2FRAMEDEF frame, FLOAT width, FLOAT height);
void SC2_SetHidden(LPSC2FRAMEDEF frame, BOOL value);
void SC2_SetEnabled(LPSC2FRAMEDEF frame, BOOL enabled);
void SC2_SetText(LPSC2FRAMEDEF frame, LPCSTR format, ...);
#endif /* !STB_SC2LAYOUT_IMPLEMENTATION */

#ifdef STB_SC2LAYOUT_IMPLEMENTATION

static void SC2_InitFrame(LPSC2FRAMEDEF frame, FRAMETYPE type) {
    memset(frame, 0, sizeof(*frame));
    frame->Type = type;
    frame->Alpha = 1.0f;
    frame->Color = (COLOR32){255, 255, 255, 255};
    FOR_LOOP(i, FPP_COUNT) {
        frame->Points.x[i].targetPos = (uiFramePointPos_t)i;
        frame->Points.y[i].targetPos = (uiFramePointPos_t)i;
    }
}

static void SC2_SetPoint(LPSC2FRAMEDEF frame, uiFramePointPos_t fpx, uiFramePointPos_t fpy, LPCSC2FRAMEDEF other, uiFramePointPos_t otherPoint, FLOAT x, FLOAT y) {
    frame->Points.x[fpx].used = true;
    frame->Points.x[fpx].targetPos = otherPoint;
    frame->Points.x[fpx].relativeTo = other;
    frame->Points.x[fpx].offset = x;
    frame->Points.y[fpy].used = true;
    frame->Points.y[fpy].targetPos = otherPoint;
    frame->Points.y[fpy].relativeTo = other;
    frame->Points.y[fpy].offset = y;
}

static void SC2_SetAllPoints(LPSC2FRAMEDEF frame) {
    SC2_SetPoint(frame, FPP_MIN, FPP_MIN, frame->Parent, FPP_MIN, 0, 0);
    SC2_SetPoint(frame, FPP_MAX, FPP_MAX, frame->Parent, FPP_MAX, 0, 0);
}

static void SC2_SetParent(LPSC2FRAMEDEF frame, LPCSC2FRAMEDEF parent) {
    frame->Parent = parent;
}

static void SC2_SetSize(LPSC2FRAMEDEF frame, FLOAT width, FLOAT height) {
    frame->Width = width;
    frame->Height = height;
}

static void SC2_SetHidden(LPSC2FRAMEDEF frame, BOOL value) {
    frame->hidden = value;
}

static void SC2_SetEnabled(LPSC2FRAMEDEF frame, BOOL enabled) {
    frame->disabled = !enabled;
}

static void SC2_SetText(LPSC2FRAMEDEF frame, LPCSTR format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(frame->DynamicText, frame->DynamicTextCapacity, format, args);
    va_end(args);
    frame->TextLength = (DWORD)strlen(frame->DynamicText);
    frame->Text = frame->DynamicText;
}

#endif /* STB_SC2LAYOUT_IMPLEMENTATION */

#endif /* stb_sc2layout_h */
