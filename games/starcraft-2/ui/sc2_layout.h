/*
 * sc2_layout.h — SC2 .SC2Layout XML parser and frame builder.
 *
 * Parses StarCraft II UI layout files (.SC2Layout) into sc2BaseFrame_t arrays.
 * SC2 layouts use XML with <Desc> root, <Frame type="..." name="..."> elements,
 * <Anchor side="..." relative="..." pos="..." offset="..."/> for positioning,
 * and template inheritance via template="Path/TemplateName".
 *
 * The parser resolves <Include path="..."/> directives, resolves template
 * inheritance by cloning children, evaluates constants (##Name → value),
 * and produces a flat array of sc2BaseFrame_t that the client can iterate.
 */
#ifndef sc2_layout_h
#define sc2_layout_h

#include "common/shared.h"
#include "client/ui.h"

/* SC2 UI frame types — isolated from shared engine types */
typedef struct {
    uiFramePointPos_t targetPos;
    BOOL used;
    DWORD relative_index;           /* index into exported frame array, -1 = scene */
    FLOAT offset;
} sc2BaseFramePoint_t;

typedef sc2BaseFramePoint_t sc2BaseFramePoints_t[FPP_COUNT];

/* SC2 UI interaction flags */
#define SC2_UIFLAG_PRESSED              (1 << 0)
#define SC2_UIFLAG_HOVERED              (1 << 1)
#define SC2_UIFLAG_CHECKED              (1 << 2)
#define SC2_UIFLAG_HIDDEN               (1 << 3)
#define SC2_UIFLAG_DISABLED             (1 << 4)
#define SC2_UIFLAG_HIDDEN_IN_HIERARCHY  (1 << 5)

/* SC2 frame flags (sc2Frame_t.flags) */
#define SC2_FRAME_HAS_WIDTH             (1 << 0)
#define SC2_FRAME_HAS_HEIGHT            (1 << 1)
#define SC2_FRAME_VISIBLE               (1 << 2)
#define SC2_FRAME_HAS_VISIBLE           (1 << 3)
#define SC2_FRAME_ACCEPTS_MOUSE         (1 << 4)
#define SC2_FRAME_HAS_COLOR             (1 << 5)
#define SC2_FRAME_HAS_ALPHA             (1 << 6)
#define SC2_FRAME_COLLAPSE_LAYOUT       (1 << 7)
#define SC2_FRAME_HIGHLIGHT_ON_HOVER    (1 << 8)
#define SC2_FRAME_HIGHLIGHT_ON_FOCUS    (1 << 9)
#define SC2_FRAME_BATCH_IMAGES          (1 << 10)
#define SC2_FRAME_BATCH_TEXT            (1 << 11)
#define SC2_FRAME_HAS_DESC_FLAGS        (1 << 12)
#define SC2_FRAME_DESC_FLAGS_INTERNAL   (1 << 13)

/* SC2 texture flags (sc2Texture_t.flags) */
#define SC2_TEX_TILED                   (1 << 0)
#define SC2_TEX_HAS_TEXTURE             (1 << 1)

/* SC2 anchor flags (sc2Anchor_t.flags) */
#define SC2_ANCHOR_HAS                  (1 << 0)

/* SC2 backdrop flags (sc2BaseFrame_t.backdrop.flags) */
#define SC2_BACKDROP_TILE               (1 << 0)

/* SC2 flattened UI frame struct — produced by SC2_LayoutFlatten */
struct sc2BaseFrame_s;
typedef struct sc2BaseFrame_s {
    DWORD number;
    FRAMETYPE type;
    void *parent;                   /* game resolves: pointer or index */
    DWORD parent_index;             /* index into frames array, -1 = root */
    struct { sc2BaseFramePoints_t x, y; } points;
    RECT screen_rect;               /* computed screen-space AABB, filled by client layout */
    COLOR32 color;
    FLOAT alpha;
    struct { FLOAT width, height; } size;
    DWORD image;                    /* primary texture/resource index */
    RECT texcoord;                  /* texture UV rect */
    LPCSTR text;
    COLOR32 text_color;
    struct {                        /* backdrop */
        DWORD bg;
        DWORD edge;
        DWORD flags;                /* SC2_BACKDROP_* */
        FLOAT insets[4];
    } backdrop;
    DWORD ui_flags;                 /* SC2_UIFLAG_* bitmask */
    void (*on_event)(struct sc2BaseFrame_s *frame, FLOAT x, FLOAT y, int button, BOOL down);
    void (*on_draw)(struct sc2BaseFrame_s *frame, LPCRECT rect);
} sc2BaseFrame_t;

typedef sc2BaseFrame_t *LPSC2BASEFRAME;
typedef sc2BaseFrame_t const *LPCSC2BASEFRAME;

/* Maximum frames in the layout system */
#define SC2_MAX_FRAMES     4096
#define SC2_MAX_TEMPLATES  1024
#define SC2_MAX_INCLUDES   128
#define SC2_MAX_ANCHORS    4       /* per frame: Top, Bottom, Left, Right */
#define SC2_MAX_CONSTANTS  256
#define SC2_MAX_TEXTURES   8       /* per frame */
#define SC2_MAX_CHILDREN   64      /* per frame */

/* SC2 anchor side indices (matching <Anchor side="..."> values) */
typedef enum {
    SC2_SIDE_TOP,
    SC2_SIDE_BOTTOM,
    SC2_SIDE_LEFT,
    SC2_SIDE_RIGHT,
    SC2_SIDE_COUNT,
} SC2Side;

/* SC2 anchor position (matching <Anchor pos="..."> values) */
typedef enum {
    SC2_POS_MIN,    /* Top/Left edge */
    SC2_POS_MID,    /* Center */
    SC2_POS_MAX,    /* Bottom/Right edge */
    SC2_POS_COUNT,
} SC2Pos;

/* SC2 anchor definition */
typedef struct {
    SC2Side side;           /* Top, Bottom, Left, Right */
    SC2Pos pos;             /* Min, Mid, Max */
    int16_t offset;         /* pixel offset */
    UINAME relative;        /* relative frame name ("$parent", "$root", or name) */
    DWORD flags;            /* SC2_ANCHOR_HAS */
} sc2Anchor_t;

/* SC2 texture layer */
typedef struct {
    UINAME resource;        /* @@UI/TextureName or @UI/TextureName */
    UINAME texture_type;    /* Normal, Border, HorizontalBorder, EndCap, None */
    int layer;              /* draw layer index */
    DWORD flags;            /* SC2_TEX_* */
} sc2Texture_t;

/* SC2 frame type (matching <Frame type="..."> values) */
typedef enum {
    SC2_FRAMETYPE_FRAME,
    SC2_FRAMETYPE_BUTTON,
    SC2_FRAMETYPE_IMAGE,
    SC2_FRAMETYPE_LABEL,
    SC2_FRAMETYPE_EDITBOX,
    SC2_FRAMETYPE_TOOLTIP,
    SC2_FRAMETYPE_MODEL,
    SC2_FRAMETYPE_CONSOLE_PANEL,
    SC2_FRAMETYPE_COMMAND_PANEL,
    SC2_FRAMETYPE_COMMAND_BUTTON,
    SC2_FRAMETYPE_COMMAND_TOOLTIP,
    SC2_FRAMETYPE_INFO_PANEL,
    SC2_FRAMETYPE_MINIMAP_PANEL,
    SC2_FRAMETYPE_MINIMAP,
    SC2_FRAMETYPE_RESOURCE_PANEL,
    SC2_FRAMETYPE_PORTRAIT_PANEL,
    SC2_FRAMETYPE_GAME_UI,
    SC2_FRAMETYPE_WORLD_PANEL,
    SC2_FRAMETYPE_COUNTDOWN_LABEL,
    SC2_FRAMETYPE_HERO_PANEL,
    SC2_FRAMETYPE_INVENTORY_PANEL,
    SC2_FRAMETYPE_IDLE_BUTTON,
    SC2_FRAMETYPE_AI_BUTTON,
    SC2_FRAMETYPE_PYLON_BUTTON,
    SC2_FRAMETYPE_MESSAGE_DISPLAY,
    SC2_FRAMETYPE_ALERT_PANEL,
    SC2_FRAMETYPE_ALERT_DISPLAY,
    SC2_FRAMETYPE_CHAT_BAR,
    SC2_FRAMETYPE_MENU_BAR,
    SC2_FRAMETYPE_SUBTITLE_PANEL,
    SC2_FRAMETYPE_TIME_PANEL,
    SC2_FRAMETYPE_CREDITS_PANEL,
    SC2_FRAMETYPE_CONTROL_GROUP_PANEL,
    SC2_FRAMETYPE_MINIMAP_PANEL_TOOLTIP,
    SC2_FRAMETYPE_REVEAL_PANEL,
    SC2_FRAMETYPE_ALLIANCE_PANEL,
    SC2_FRAMETYPE_TEAM_RESOURCE_PANEL,
    SC2_FRAMETYPE_LEADER_PANEL,
    SC2_FRAMETYPE_OBSERVER_PANEL,
    SC2_FRAMETYPE_REPLAY_PANEL,
    SC2_FRAMETYPE_TALKER_PANEL,
    SC2_FRAMETYPE_PURCHASE_PANEL,
    SC2_FRAMETYPE_RESEARCH_PANEL,
    SC2_FRAMETYPE_PLANET_PANEL,
    SC2_FRAMETYPE_CASH_PANEL,
    SC2_FRAMETYPE_RESOURCE_REQUEST_ALERT,
    SC2_FRAMETYPE_PAUSE_PANEL,
    SC2_FRAMETYPE_CONVERSATION_PANEL,
    SC2_FRAMETYPE_OBJECTIVE_PANEL,
    SC2_FRAMETYPE_TRIGGER_DIALOG,
    SC2_FRAMETYPE_TRIGGER_WINDOW,
    SC2_FRAMETYPE_SYSTEM_ALERT_PANEL,
    SC2_FRAMETYPE_TIP_ALERT,
    SC2_FRAMETYPE_TIP_ALERT_MOVING,
    SC2_FRAMETYPE_CINEMATIC_TEXT,
    SC2_FRAMETYPE_TEXT_CRAWL,
    SC2_FRAMETYPE_FLASH_FRAME,
    SC2_FRAMETYPE_LEROY_BUTTON,
    SC2_FRAMETYPE_MAP_INFO_PANEL,
    SC2_FRAMETYPE_PERF_OVERLAY,
    SC2_FRAMETYPE_PROFILER,
    SC2_FRAMETYPE_ACHIEVEMENT_PANEL,
    SC2_FRAMETYPE_SCREEN_CREDITS,
    SC2_FRAMETYPE_UNKNOWN,
} sc2FrameType;

/* SC2 constant definition */
typedef struct {
    UINAME name;
    char val[64];
} sc2Constant_t;

/* SC2 layout frame definition (parsed from XML, before resolution) */
typedef struct sc2Frame_s {
    UINAME name;                        /* name="..." */
    sc2FrameType type;                  /* type="..." */
    UINAME template_path;               /* template="Path/Name" */
    UINAME image_ref;                   /* Image val="$root/..." (tooltip border) */

    /* Properties */
    FLOAT width, height;
    DWORD flags;                        /* SC2_FRAME_* */
    COLOR32 color;
    FLOAT alpha;

    /* Anchors */
    sc2Anchor_t anchors[SC2_MAX_ANCHORS];
    int num_anchors;

    /* Textures (up to SC2_MAX_TEXTURES layers) */
    sc2Texture_t textures[SC2_MAX_TEXTURES];
    int num_textures;

    /* Children (inline child frames) */
    struct sc2Frame_s *children[SC2_MAX_CHILDREN];
    int num_children;

    /* Parent pointer (set during tree assembly) */
    struct sc2Frame_s *parent;

    /* Resolved index into flat frame array (-1 = not yet resolved) */
    int resolved_index;

    /* Source file for debugging */
    PATHSTR source_file;
} sc2Frame_t;

/* SC2 layout context — holds all parsed state */
typedef struct {
    /* Parsed templates (indexed by name hash) */
    sc2Frame_t templates[SC2_MAX_TEMPLATES];
    int num_templates;

    /* Constants */
    sc2Constant_t constants[SC2_MAX_CONSTANTS];
    int num_constants;

    /* Resolved frame array (flat, for client consumption) */
    sc2BaseFrame_t frames[SC2_MAX_FRAMES];
    int num_frames;

    /* Root frame */
    sc2Frame_t *root;

    /* Include tracking */
    PATHSTR included_files[SC2_MAX_INCLUDES];
    int num_includes;
} sc2Layout_t;

/* Initialize the SC2 layout system */
void SC2_LayoutInit(void);

/* Shutdown and free all layout data */
void SC2_LayoutShutdown(void);

/* Parse a .SC2Layout file from MPQ or filesystem */
BOOL SC2_LayoutParseFile(LPCSTR filename);

/* Flatten parsed templates into the frame array (call after parsing) */
BOOL SC2_LayoutFlatten(LPCSTR root_name);

/* Get the resolved frame array for client rendering */
sc2BaseFrame_t *SC2_LayoutGetFrames(DWORD *count);

/* Build and flatten the main menu from glue SC2Layout files */
BOOL SC2_LayoutBuildMainMenu(void);

/* Build and flatten the main game UI from the standard SC2Layout files */
BOOL SC2_LayoutBuildGameUI(void);

/* Find a template by name */
sc2Frame_t *SC2_LayoutFindTemplate(LPCSTR name);

/* Find the first resolved sc2BaseFrame_t with the given SC2 frame type */
sc2BaseFrame_t *SC2_LayoutFindFrameByType(sc2FrameType type);

/* Template accessors for iteration */
int SC2_LayoutNumTemplates(void);
sc2Frame_t *SC2_LayoutGetTemplate(int index);

/* Resolve a constant value (##Name → val) */
LPCSTR SC2_LayoutResolveConstant(LPCSTR name);

#endif
