/*
 * sc2_layout.h — SC2 .SC2Layout XML parser and frame builder.
 *
 * Parses StarCraft II UI layout files (.SC2Layout) into uiBaseFrame_t arrays.
 * SC2 layouts use XML with <Desc> root, <Frame type="..." name="..."> elements,
 * <Anchor side="..." relative="..." pos="..." offset="..."/> for positioning,
 * and template inheritance via template="Path/TemplateName".
 *
 * The parser resolves <Include path="..."/> directives, resolves template
 * inheritance by cloning children, evaluates constants (##Name → value),
 * and produces a flat array of uiBaseFrame_t that the client can iterate.
 */
#ifndef sc2_layout_h
#define sc2_layout_h

#include "common/shared.h"
#include "client/ui.h"

/* Maximum frames in the layout system */
#define SC2_MAX_FRAMES     16384
#define SC2_MAX_TEMPLATES  16384
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
    BOOL has_anchor;        /* TRUE if this anchor point is set */
} sc2Anchor_t;

/* SC2 texture layer */
typedef struct {
    UINAME resource;        /* @@UI/TextureName or @UI/TextureName */
    UINAME texture_type;    /* Normal, Border, HorizontalBorder, EndCap, None */
    int layer;              /* draw layer index */
    BOOL tiled;
    BOOL has_texture;
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
    BOOL has_width, has_height;
    BOOL visible;
    BOOL accepts_mouse;
    BOOL has_visible;
    COLOR32 color;
    BOOL has_color;
    FLOAT alpha;
    BOOL has_alpha;
    BOOL collapse_layout;
    BOOL highlight_on_hover;
    BOOL highlight_on_focus;
    BOOL batch_images;
    BOOL batch_text;
    BOOL has_desc_flags;
    BOOL desc_flags_internal;

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

    /* Template resolution tracking */
    BOOL template_resolved;

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
    uiBaseFrame_t frames[SC2_MAX_FRAMES];
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
uiBaseFrame_t *SC2_LayoutGetFrames(DWORD *count);

/* Build and flatten the main game UI from the standard SC2Layout files */
BOOL SC2_LayoutBuildGameUI(void);

/* Find a template by name */
sc2Frame_t *SC2_LayoutFindTemplate(LPCSTR name);

/* Find the first resolved uiBaseFrame_t with the given SC2 frame type */
uiBaseFrame_t *SC2_LayoutFindFrameByType(sc2FrameType type);

/* Template accessors for iteration */
int SC2_LayoutNumTemplates(void);
sc2Frame_t *SC2_LayoutGetTemplate(int index);

/* Resolve a constant value (##Name → val) */
LPCSTR SC2_LayoutResolveConstant(LPCSTR name);

#endif
